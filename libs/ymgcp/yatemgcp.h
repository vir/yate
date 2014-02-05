/**
 * yatemgcp.h
 * Yet Another MGCP Stack
 * This file is part of the YATE Project http://YATE.null.ro
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

#ifndef __YATEMGCP_H
#define __YATEMGCP_H

#include <yateclass.h>
#include <yatemime.h>

#ifdef _WINDOWS

#ifdef LIBYMGCP_EXPORTS
#define YMGCP_API __declspec(dllexport)
#else
#ifndef LIBYMGCP_STATIC
#define YMGCP_API __declspec(dllimport)
#endif
#endif

#endif /* _WINDOWS */

#ifndef YMGCP_API
#define YMGCP_API
#endif

/**
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

class MGCPMessage;
class MGCPTransaction;
class MGCPEpInfo;
class MGCPEndpoint;
class MGCPEvent;
class MGCPEngine;

/**
 * This class holds an MGCP message, either command or response, along with
 *  its parameters. The
 * @short An MGCP command or response
 */
class YMGCP_API MGCPMessage : public RefObject
{
    friend class MGCPTransaction;
public:
    /**
     * Constructor. Construct an outgoing command message.
     * A transaction id will be requested from the endpoint's engine.
     * The message will be invalidated if failed to get a transaction id or the
     *  command name is unknown
     * @param engine The engine sending this message
     * @param name Command name
     * @param ep The id of the endpoint issuing this command
     * @param ver The protocol version to use
     */
    MGCPMessage(MGCPEngine* engine, const char* name, const char* ep, const char* ver = "MGCP 1.0");

    /**
     * Constructor. Construct an outgoing response message
     * The message will be invalidated if failed to get a transaction id or the
     *  code is greater then 999
     * @param trans The transaction to respond
     * @param code The response code ranging from 0 to 999
     * @param comment Optional response comment
     */
    MGCPMessage(MGCPTransaction* trans, unsigned int code, const char* comment = 0);

    /**
     * Destructor
     */
    virtual ~MGCPMessage();

    /**
     * Check if this is a valid message
     * @return True if this is a valid message
     */
    inline bool valid() const
	{ return m_valid; }

    /**
     * Get the command name or response code text representation of this message
     * @return The command name or response text representation of this message
     */
    inline const String& name() const
	{ return m_name; }

    /**
     * Get the response code if this is a response message
     * @return The response code contained in this message
     */
    inline int code() const
	{ return m_code; }

    /**
     * Get the protocol version of a command message
     * @return The protocol version of this message
     */
    inline const String& version() const
	{ return m_version; }

    /**
     * Get the comment from a response message
     * @return The comment of this message
     */
    inline const String& comment() const
	{ return m_comment; }

    /**
     * Check if this is a command (code is a negative value)
     * @return True if this message is a command
     */
    inline bool isCommand() const
	{ return code() < 0; }

    /**
     * Check if this is a response message (code is greater then or equal to 100)
     * @return True if this message is a response
     */
    inline bool isResponse() const
	{ return 100 <= code(); }

    /**
     * Check if this message is a response ACK (code is between 0 and 99, including the margins)
     * @return True if this message is a response ACK
     */
    inline bool isAck() const
	{ return 0 <= code() && code() <= 99; }

    /**
     * Get the message's transaction id
     * @return The message's transaction id
     */
    inline unsigned int transactionId() const
	{ return m_transaction; }

    /**
     * Get the message's endpoint id if this is a command
     * @return The message's endpoint id if this is a command
     */
    inline const String& endpointId() const
	{ return m_endpoint; }

    /**
     * Convert this message to a string representation to be sent to the remote
     *  endpoint or printed to output
     * @param dest Destination string
     */
    void toString(String& dest) const;

    /**
     * Parse a received buffer according to RFC 3435. Command and protocol names are converted to upper case.
     *  The enpoint id is converted to lower case. Message parameter names are converted to lower case if
     *  the engine's flag is set. Message parameter values and SDP(s) are stored unchanged
     * @param engine The receiving engine
     * @param dest The list of received messages
     * @param buffer The buffer to parse
     * @param len The buffer length
     * @param sdpType The MIME SDP content type if the message contains any SDP body
     * @return False on failure, true on success. If failed, the destination
     *  list may contain a response message to be sent
     */
    static bool parse(MGCPEngine* engine, ObjList& dest,
	const unsigned char* buffer, unsigned int len,
	const char* sdpType = "application/sdp");

    /**
     * Keep the message parameters
     */
    NamedList params;

    /**
     * Keep the SDP(s) carried by this message as MimeSdpBody object(s)
     */
    ObjList sdp;

protected:
    /**
     * Constructor. Used by the parser to construct an incoming message
     * @param engine The engine receiving this message
     * @param name Command name or response comment
     * @param code The response code in the range 0 to 999 or -1 if the received
     *  message is a command
     * @param transId The id of the transaction owning this message
     * @param epId The id of the endpoint issuing this command
     * @param ver The protocol version
     */
    MGCPMessage(MGCPEngine* engine, const char* name, int code,
	unsigned int transId, const char* epId, const char* ver);

private:
    MGCPMessage() : params("") {}        // Avoid using default constructor
    // Decode the message line
    static MGCPMessage* decodeMessage(const char* line, unsigned int len, unsigned int& trans,
	String& error, MGCPEngine* engine);
    // Decode message parameters. Return true if found a line containing a dot
    static bool decodeParams(const unsigned char* buffer, unsigned int len,
	unsigned int& crt, MGCPMessage* msg, String& error, MGCPEngine* engine);

    String m_name;                       // Command or string representation of response code
    bool m_valid;                        // False if this message is invalid
    int m_code;                          // Response code or -1 if this is a command
    unsigned int m_transaction;          // The id of the transaction this message belongs to
    String m_endpoint;                   // The id of the endpoint issuing this message
    String m_version;                    // The protocol version
    String m_comment;                    // The comment attached to a response message
};

/**
 * This class implements an MGCP transaction
 * @short An MGCP transaction
 */
class YMGCP_API MGCPTransaction : public RefObject, public Mutex
{
    friend class MGCPEngine;             // Process a received message
    friend class MGCPEvent;              // Access to event termination notification
public:
    /**
     * Transaction state enumeration
     */
    enum State {
	Invalid      = 0,                // This is an invalid transaction (constructor failed)
	Initiated    = 1,                // An initial command message was sent/received
	Trying       = 2,                // Sent or received a provisional response to the initial message
	Responded    = 3,                // Sent or received a final response to the initial message
	Ack          = 4,                // Response was ack'd
	Destroying   = 5,                // Waiting to be removed from the engine
    };

    /**
     * Constructor. Construct a transaction from its first message
     * @param engine The engine owning this transaction
     * @param msg The command creating this transaction
     * @param outgoing The direction of this transaction
     * @param address Remote enpoint's address
     * @param engineProcess Use engine processor thread for this transaction
     */
    MGCPTransaction(MGCPEngine* engine, MGCPMessage* msg, bool outgoing,
	const SocketAddr& address, bool engineProcess = true);

    /**
     * Destructor
     */
    virtual ~MGCPTransaction();

    /**
     * Get the current transaction's state
     * @return The transaction state as enumeration
     */
    inline State state() const
	{ return m_state; }

    /**
     * Get the id of this transaction
     * @return The id of this transaction
     */
    inline unsigned int id() const
	{ return m_id; }

    /**
     * Get the direction of this transaction
     * @return True if this is an outgoing transaction
     */
    inline bool outgoing() const
	{ return m_outgoing; }

    /**
     * Get the id of the endpoint owning this transaction
     * @return The id of the endpoint owning this transaction
     */
    inline const String& ep() const
	{ return m_endpoint; }

    /**
     * Get the remote endpoint's IP address
     * @return The remote endpoint's IP address
     */
    const SocketAddr& addr() const
	{ return m_address; }

    /**
     * Get the engine owning this transaction
     * @return The engine owning this transaction
     */
    inline MGCPEngine* engine()
	{ return m_engine; }

    /**
     * Get the initial command message sent or received by this transaction
     * @return The transaction's initial message
     */
    inline const MGCPMessage* initial() const
	{ return m_cmd; }

    /**
     * Get the provisional response message sent or received by this transaction
     * @return The transaction's provisional response message
     */
    inline const MGCPMessage* msgProvisional() const
	{ return m_provisional; }

    /**
     * Get the final response message sent or received by this transaction
     * @return The transaction's final response message
     */
    inline const MGCPMessage* msgResponse() const
	{ return m_response; }

    /**
     * Get the response aknowledgement message sent or received by this transaction
     * @return The transaction's response aknowledgement message
     */
    inline const MGCPMessage* msgAck() const
	{ return m_ack; }

    /**
     * Check if this transaction timed out
     * @return True if this transaction timed out
     */
    inline bool timeout() const
	{ return m_timeout; }

    /**
     * Set the remote ACK request flag
     * @param request False if remote is not required to send an ACK
     */
    inline void ackRequest(bool request)
	{ m_ackRequest = request; }

    /**
     * Get the private user data of this transaction
     * @return The private user data of this transaction
     */
    inline void* userData() const
	{ return m_private; }

    /**
     * Set the private user data of this transaction
     * @param data The new private user data of this transaction
     */
    inline void userData(void* data)
	{ m_private = data; }

    /**
     * Set the engine process flag. Allow the engine to process this transaction
     * (call getEvent() from engine process thread)
     */
    inline void setEngineProcess()
	{ m_engineProcess = true; }

    /**
     * Get an event from this transaction. Check timeouts
     * @param time Current time in microseconds
     * @return MGCPEvent pointer or 0 if none
     */
    MGCPEvent* getEvent(u_int64_t time = Time());

    /**
     * Explicitely transmits a provisional code
     * @param code Provisional response code to send, must be in range 100-199
     * @param comment Optional response comment text
     * @return True if the provisional response was sent
     */
    bool sendProvisional(int code = 100, const char* comment = 0);

    /**
     * Creates and transmits a final response (code must at least 200) message if
     *  this is an incoming transaction
     * @param code Response code to send
     * @param comment Optional response comment text
     * @return True if the message was queued for transmission
     */
    inline bool setResponse(int code, const char* comment = 0)
	{ return setResponse(new MGCPMessage(this,code,comment)); }

    /**
     * Creates and transmits a final response (code must at least 200) message if
     *  this is an incoming transaction.
     * The SDP(s) will be consumed (appended to the message or destroyed)
     * @param code Response code to send
     * @param params Parameters to set in response, name will be set as comment
     * @param sdp1 Optional SDP to be added to the response
     * @param sdp2 Optional second SDP to be added to the response if the first one is not 0
     * @return True if the message was queued for transmission
     */
    bool setResponse(int code, const NamedList* params, MimeSdpBody* sdp1 = 0,
	MimeSdpBody* sdp2 = 0);

    /**
     * Transmits a final response (code must at least 200)
     *  message if this is an incoming transaction
     * @param msg The message to transmit
     * @return True if the message was queued for transmission
     */
    bool setResponse(MGCPMessage* msg);

protected:
    /**
     * Gracefully terminate this transaction. Release memory
     */
    virtual void destroyed();

    /**
     * Consume (process) a received message, other then the initiating one
     * @param msg The received message
     */
    void processMessage(MGCPMessage* msg);

    /**
     * Check timeouts. Manage retransmissions
     * @param time Current time in milliseconds
     * @return MGCPEvent pointer if timeout
     */
    MGCPEvent* checkTimeout(u_int64_t time);

    /**
     * Event termination notification
     * @param event The notifier
     */
    void eventTerminated(MGCPEvent* event);

    /**
     * Change transaction's state if the new state is a valid one
     * @param newState The new state of this transaction
     */
    void changeState(State newState);

    /**
     * Set and send the provisional response (codes between 100 and 199)
     * @param code The response code
     */
    void setProvisional(int code = 100);

    /**
     * (Re)send one the initial, provisional or final response. Change transaction's state
     * @param msg The message to send
     */
    void send(MGCPMessage* msg);

private:
    MGCPTransaction() {}                 // Avoid using default constructor
    // Check if received any final response. Create an event. Init timeout.
    // Send a response ACK if requested by the response
    MGCPEvent* checkResponse(u_int64_t time);
    // Init timeout for retransmission or transaction termination
    void initTimeout(u_int64_t time, bool extra);
    // Remove from engine. Create event. Deref the transaction
    MGCPEvent* terminate();

    State m_state;                       // Current state
    unsigned int m_id;                   // Transaction id
    bool m_outgoing;                     // Transaction direction
    SocketAddr m_address;                // Remote andpoint's address
    MGCPEngine* m_engine;                // The engine owning this transaction
    MGCPMessage* m_cmd;                  // The command that created this transaction
    MGCPMessage* m_provisional;          // The provisional response to the command that created this transaction
    MGCPMessage* m_response;             // The response to the command that created this transaction
    MGCPMessage* m_ack;                  // The response aknowledgement message sent or received
    MGCPEvent* m_lastEvent;              // The last generated event
    String m_endpoint;                   // The endpoint owning this transaction
    u_int64_t m_nextRetrans;             // Retransission or destroy time
    unsigned int m_crtRetransInterval;   // Current retransmission interval
    unsigned int m_retransCount;         // Remainig number of retransmissions
    bool m_timeout;                      // Transaction timeout flag
    bool m_ackRequest;                   // Remote is requested to send ACK
    void* m_private;                     // Data used by this transaction's user
    String m_debug;                      // String used to identify the transaction in debug messages
    bool m_engineProcess;                // Process transaction (getEvent) from engine processor
};

/**
 * This class holds an endpoint id in the form "endpoint@host:port"
 * @short An endpoint id
 */
class YMGCP_API MGCPEndpointId
{
public:
    /**
     * Constructor
     */
    inline MGCPEndpointId()
	: m_port(0)
	{}

    /**
     * Constructor. Construct this endpoint id from a string
     * @param src The string to construct from
     */
    inline MGCPEndpointId(const String& src)
	: m_port(0)
	{ set(src); }

    /**
     * Copy constructor
     * @param value Original Endpoint ID to copy
     */
    inline MGCPEndpointId(const MGCPEndpointId& value)
	: m_id(value.id()), m_endpoint(value.user()),
	  m_host(value.host()), m_port(value.port())
	{ }

    /**
     * Constructor. Construct this endpoint id
     * @param endpoint The user part of the endpoint's URI
     * @param host The IP address of the endpoint's URI
     * @param port The port used by the endpoint to receive data
     * @param addPort Add :port at end of id only if port is not zero
     */
    inline MGCPEndpointId(const char* endpoint, const char* host, int port, bool addPort = true)
	: m_port(0)
	{ set(endpoint,host,port,addPort); }

    /**
     * Get the full id of the endpoint
     * @return The full id of the endpoint
     */
    inline const String& id() const
	{ return m_id; }

    /**
     * Get the user part of the endpoint URI
     * @return The user part of the endpoint URI
     */
    inline const String& user() const
	{ return m_endpoint; }

    /**
     * Get the host part of the endpoint URI
     * @return The host part of the endpoint URI
     */
    inline const String& host() const
	{ return m_host; }

    /**
     * Get the port used by this endpoint
     * @return The port used by this endpoint
     */
    inline int port() const
	{ return m_port; }

    /**
     * Set the port used by this endpoint
     * @param newPort The new port used by this endpoint
     * @param addPort Add :port at end of id only if port is not zero
     */
    inline void port(int newPort, bool addPort = true)
	{ set(m_endpoint,m_host,newPort,addPort); }

    /**
     * Set this endpoint id. Convert it to lower case
     * @param endpoint The user part of the endpoint's URI
     * @param host The IP address of the endpoint's URI
     * @param port The port used by the endpoint to receive data
     * @param addPort Add :port at end of id only if port is not zero
     */
    void set(const char* endpoint, const char* host, int port, bool addPort = true);

    /**
     * Set this endpoint id. Convert it to lower case
     * @param src The string to construct from
     */
    inline void set(const String& src) {
	    URI uri(src);
	    set(uri.getUser(),uri.getHost(),uri.getPort());
	}

    /**
     * Check if this is a valid endpoint id as defined in RFC 3435 3.2.1.3.
     * It is considerred valid if the user and host part lengths are between
     *  1 and 255 and the port is not 0
     * @return True if this is a valid endpoint id
     */
    inline bool valid() const {
	    return m_endpoint && m_endpoint.length() < 256 &&
		m_host && m_host.length() < 256;
	}

private:
    String m_id;                         // The complete id
    String m_endpoint;                   // The endpoint's name inside the host
    String m_host;                       // Host of this endpoint
    int m_port;                          // Port used by this endpoint
};

/**
 * This class holds data about a remote endpoint (id and address)
 * @short Remote endpoint info class
 */
class YMGCP_API MGCPEpInfo : public MGCPEndpointId, public GenObject
{
public:
    /**
     * Constructor. Construct this endpoint info
     * @param endpoint The endpoint part of the endpoint's id
     * @param host The IP address of this endpoint
     * @param port The port used to send data to this endpoint
     * @param addPort Add :port at end of id only if port is not zero
     */
    inline MGCPEpInfo(const char* endpoint, const char* host, int port, bool addPort = true)
	: MGCPEndpointId(endpoint,host,port,addPort),
	  m_address(AF_INET), m_resolve(true) {
	    m_address.port(port);
	}

    /**
     * Get a string representation of this object
     * @return The endpoint's id
     */
    virtual const String& toString() const
	{ return id(); }

    /**
     * Retrieve the current address for this endpoint information
     * @return Address and port of this endpoint info
     */
    inline const SocketAddr& address() const
	{ return m_address; }

    /**
     * Retrieve the address for this endpoint information, resolve name if needed
     * @return Address and port of this endpoint info
     */
    const SocketAddr& address();

    /**
     * Set a new socket address in the endpoint info
     * @param addr New address and port to set in the endpoint
     */
    inline void address(const SocketAddr& addr)
	{ m_resolve = false; m_address = addr; }

    /**
     * An alias name of the remote endpoint, may be empty
     */
    String alias;

private:
    SocketAddr m_address;
    bool m_resolve;
};

/**
 * This class holds a local MGCP endpoint (either gateway or call agent) along
 *  with its remote peer(s).
 * If the engine owning this endpoint is an MGCP gateway, only 1 remote peer (Call Agent) is allowed
 * @short An MGCP endpoint
 */
class YMGCP_API MGCPEndpoint : public RefObject, public MGCPEndpointId, public Mutex
{
public:
    /**
     * Constructor. Construct this endpoint. Append itself to the engine's list.
     * The endpoint's id will be created from the received user and engine's address
     * @param engine The engine owning this endpoint
     * @param user The user part of the endpoint's id
     * @param host The host part of the endpoint's id
     * @param port The port part of the endpoint's id
     * @param addPort Add :port at end of id only if port is not zero
     */
    MGCPEndpoint(MGCPEngine* engine, const char* user, const char* host, int port, bool addPort = true);

    /**
     * Destructor. Remove itself from engine's list
     */
    virtual ~MGCPEndpoint();

    /**
     * Get a string representation of this endpoint
     * @return A string representation of this endpoint
     */
    virtual const String& toString() const
	{ return MGCPEndpointId::id(); }

    /**
     * Get the engine owning this endpoint
     * @return The engine owning this endpoint
     */
    inline MGCPEngine* engine()
	{ return m_engine; }

    /**
     * Append info about a remote endpoint controlled by or controlling this endpoint.
     * If the engine owning this endpoint is an MGCP gateway, only 1 remote peer (Call Agent) is allowed
     * @param endpoint The endpoint part of the remote endpoint's id
     * @param host The IP address of the remote endpoint
     * @param port The port used to send data to this endpoint.
     *  Set to 0 to set it to the default port defined by the protocol and the
     *  opposite of the engine's mode
     *  A value of -1 uses the default but doesn't add :port at end of ID
     *  Other negative values use specified port but don't add :port at end
     * @return Valid MGCPEpInfo pointer or 0 if the data wasn't added
     */
    MGCPEpInfo* append(const char* endpoint, const char* host, int port = 0);

    /**
     * Clear the list or remote endpoints
     */
    inline void clear()
	{ lock(); m_remote.clear(); unlock(); }

    /**
     * Find the info object associated with a remote peer
     * @param epId The remote endpoint's id to find
     * @return MGCPEpInfo pointer or 0 if not found
     */
    MGCPEpInfo* find(const String& epId);

    /**
     * Find an info object by remote peer alias
     * @param alias Alias of the remote endpoint's id to find
     * @return MGCPEpInfo pointer or 0 if not found
     */
    MGCPEpInfo* findAlias(const String& alias);

    /**
     * Find the info object associated with an unique remote peer
     * @return MGCPEpInfo pointer or 0 if not exactly one peer
     */
    MGCPEpInfo* peer();

private:
    MGCPEngine* m_engine;                // The engine owning this endpoint
    ObjList m_remote;                    // The remote endpoints
};

/**
 * This class carries a copy of the message received by a transaction or a transaction state
 *  change notification (such as timeout or destroy)
 * @short An MGCP event
 */
class YMGCP_API MGCPEvent
{
    friend class MGCPTransaction;
public:
    /**
     * Destructor. Delete the message. Notify and deref the transaction
     */
    ~MGCPEvent();

    /**
     * Get the transaction that generated this event
     * @return The transaction that generated this event
     */
    inline MGCPTransaction* transaction()
	{ return m_transaction; }

    /**
     * Get the message carried by this event
     * @return The message carried by this event or 0 if none
     */
    inline MGCPMessage* message() const
	{ return m_message; }

protected:
    /**
     * Constructor. Constructs an event from a transaction
     * @param trans The transaction that generated this event
     * @param msg The message carried by this event, if any
     */
    MGCPEvent(MGCPTransaction* trans, MGCPMessage* msg = 0);

private:
    MGCPTransaction* m_transaction;      // The transaction that generated this event
    MGCPMessage* m_message;              // The message carried by this event, if any
};

class MGCPPrivateThread;

/**
 * The engine may keep gateway endpoints or call agents
 * Keep the transaction list and manage it (create/delete/modify/timeout...)
 * Keep a list with the endpoints it services
 * Generate transaction numbers (IDs)
 * Parse received messages, validate and send them to the appropriate transaction
 * Send MGCP messages to remote addresses
 * @short An MGCP engine
 */
class YMGCP_API MGCPEngine : public DebugEnabler, public Mutex
{
    friend class MGCPPrivateThread;
    friend class MGCPTransaction;
public:
    /**
     * Constructor. Construct the engine and, optionally, initialize it
     * @param gateway Engine's mode: true if this engine is an MGCP Gateway,
     *  false if it's a collection of Call Agents
     * @param name Optional debug name for this engine
     * @param params Optional parameters used to initialize this engine
     */
    MGCPEngine(bool gateway, const char* name = 0, const NamedList* params = 0);

    /**
     * Destructor. Clear all lists
     */
    virtual ~MGCPEngine();

    /**
     * Check if this engine is an MGCP Gateway or a collection of Call Agents
     * @return True if this engine is an MGCP Gateway, false if it's a
     *  collection of Call Agents
     */
    inline bool gateway() const
	{ return m_gateway; }

    /**
     * Get the IP address used by this engine to receive data
     * @return The IP address used by this engine to receive data
     */
    inline const SocketAddr& address() const
	{ return m_address; }

    /**
     * Get the maximum length or received packets. This is the size of the buffer used by
     *  this engine to read data from the socket
     * @return The maximum length or received packets
     */
    inline unsigned int maxRecvPacket() const
	{ return m_maxRecvPacket; }

    /**
     * Check if this engine is allowed to send/accept unknown commands
     * @return True if this engine is allowed to send/accept unknown commands
     */
    inline bool allowUnkCmd() const
	{ return m_allowUnkCmd; }

    /**
     * Get the message retransmission interval
     * @return The message retransmission interval
     */
    inline unsigned int retransInterval() const
	{ return m_retransInterval; }

    /**
     * Get the maximum number of retransmissions for a message
     * @return The maximum number of retransmissions for a message
     */
    inline unsigned int retransCount() const
	{ return m_retransCount; }

    /**
     * Get the time to live after the transaction terminated gracefully
     * @return The time to live after the transaction terminated gracefully
     */
    inline u_int64_t extraTime() const
	{ return m_extraTime; }

    /**
     * Check if the parser should convert received messages' parameters to lower case
     * @return True if the parser should convert received messages' parameters to lower case
     */
    inline bool parseParamToLower() const
	{ return m_parseParamToLower; }

    /**
     * Check if incoming transactions would send provisional responses
     * @return True if incoming transactions would send provisional responses
     */
    inline bool provisional() const
	{ return m_provisional; }

    /**
     * Get the remote ACK request flag
     * @return True if remote will be requested to send an ACK
     */
    inline bool ackRequest() const
	{ return m_ackRequest; }

    /**
     * Set the remote ACK request flag
     * @param request False to not request from remote to send an ACK
     */
    inline void ackRequest(bool request)
	{ m_ackRequest = request; }

    /**
     * Initialize this engine
     * @param params Engine's parameters
     */
    virtual void initialize(const NamedList& params);

    /**
     * Check if a command is known by this engine
     * @param cmd The command name to check
     * @return True if the given command is known by this engine
     */
    inline bool knownCommand(const String& cmd)
	{ Lock lock(this); return (m_knownCommands.find(cmd) != 0); }

    /**
     * Add a command to the list of known commands
     * @param cmd The command name to add
     */
    void addCommand(const char* cmd);

    /**
     * Append an endpoint to this engine if not already done
     * @param ep The endpoint to append
     */
    void attach(MGCPEndpoint* ep);

    /**
     * Remove an endpoint from this engine and, optionally, remove all its transactions
     * @param ep The endpoint to remove
     * @param del True to delete it, false to just remove it from the list
     * @param delTrans True to remove all its transactions.
     *  Forced to true if the endpoint is deleted
     */
    void detach(MGCPEndpoint* ep, bool del = false, bool delTrans = false);

    /**
     * Find an endpoint by its pointer
     * @param ep The endpoint to find
     * @return MGCPEndpoint pointer or 0 if not found
     */
    MGCPEndpoint* findEp(MGCPEndpoint* ep);

    /**
     * Find an endpoint by its id
     * @param epId The endpoint's id to find
     * @return MGCPEndpoint pointer or 0 if not found
     */
    MGCPEndpoint* findEp(const String& epId);

    /**
     * Find a transaction by its id
     * @param id The id of the transaction to find
     * @param outgoing The transaction direction. True for outgoing, false for incoming
     * @return MGCPTransaction pointer or 0 if not found
     */
    MGCPTransaction* findTrans(unsigned int id, bool outgoing);

    /**
     * Generate a new id for an outgoing transaction
     * @return An id for an outgoing transaction
     */
    unsigned int getNextId();

    /**
     * Send a command message. Create a transaction for it.
     * The method will fail if the message is not a valid one or isn't a valid command
     * @param cmd The message containig the command
     * @param address The destination IP address
     * @param engineProcess Use engine private processor thread for the new transaction.
     *  If false the caller is responsable with transaction processing
     * @return MGCPTransaction pointer or 0 if failed to create a transaction
     */
    MGCPTransaction* sendCommand(MGCPMessage* cmd, const SocketAddr& address,
	bool engineProcess = true);

    /**
     * Read data from the socket. Parse and process the received message
     * @param buffer Buffer used for read operation. The buffer must be large enough
     *  to keep the maximum packet length returned by @ref maxRecvPacket()
     * @param addr The sender's address if received any data
     * @return True if received any data (a message was successfully parsed)
     */
    bool receive(unsigned char* buffer, SocketAddr& addr);

    /**
     * Try to get an event from a transaction.
     * If the event contains an unknown command and this engine is not allowed
     *  to process such commands, calls the @ref returnEvent() method, otherwise,
     *  calls the @ref processEvent() method
     * @param time Current time in microseconds
     * @return True if an event was processed
     */
    bool process(u_int64_t time = Time());

    /**
     * Try to get an event from a given transaction.
     * If the event contains an unknown command and this engine is not allowed
     *  to process such commands, calls the @ref returnEvent() method, otherwise,
     *  calls the @ref processEvent() method
     * @param tr Transaction to process
     * @param time Current time in microseconds
     * @return True if an event was processed
     */
    bool processTransaction(MGCPTransaction* tr, u_int64_t time = Time());

    /**
     * Repeatedly calls @ref receive() until the calling thread terminates
     * @param addr The sender's address if received any data
     */
    void runReceive(SocketAddr& addr);

    /**
     * Repeatedly calls @ref receive() until the calling thread terminates
     */
    void runReceive();

    /**
     * Repeatedly calls @ref process() until the calling thread terminates
     */
    void runProcess();

    /**
     * Try to get an event from a transaction
     * @param time Current time in microseconds
     * @return MGCPEvent pointer or 0 if none
     */
    MGCPEvent* getEvent(u_int64_t time = Time());

    /**
     * Process an event generated by a transaction. Descendants must override this
     *  method if they want to process events. By default it calls the version
     *  of processEvent that accepts separate parameters of event
     * @param event The event to process
     * @return True if the event was processed. If the event carry a received
     *  command and it's not processed the transaction will receive an 'unknown command' response
     */
    virtual bool processEvent(MGCPEvent* event);

    /**
     * Process an event generated by a transaction. Descendants must override this
     *  method if they want to process events
     * @param trans Pointer to the transaction that generated the event
     * @param msg MGCP message of the event, may be NULL
     * @return True if the event was processed. If the event carry a received
     *  command and it's not processed the transaction will receive an 'unknown command' response
     */
    virtual bool processEvent(MGCPTransaction* trans, MGCPMessage* msg);

    /**
     * Returns an unprocessed event to this engine to be deleted.
     * Incoming transactions will be responded. Unknown commands will receive a
     *  504 Unknown Command response, the others will receive a 507 Unsupported Functionality one
     * @param event The event to return
     */
    void returnEvent(MGCPEvent* event);

    /**
     * Terminate all transactions. Cancel all private threads if any and
     *  wait for them to terminate
     * @param gracefully If true, all incoming transaction will be responded and private
     *  threads will be gently cancelled. If false, all transactions will be deleted and
     *  threads will be cancelled the hard way
     * @param text Optional text to be sent with the response code of the incoming
     *  transactions on gracefully cleanup
     */
    void cleanup(bool gracefully = true, const char* text = "Shutdown");

    /**
     * Get the default port defined by the protocol
     * @param gateway True to get the default Gateway port,
     *  false to get the default port for the Call Agent
     * @return The default port defined by the protocol
     */
    static inline int defaultPort(bool gateway)
	{ return gateway ? 2427 : 2727; }

    /**
     * Handle a transaction that has timed out
     * @param tr The transaction that has timed out
     */
    virtual void timeout(MGCPTransaction* tr)
	{ }

    /**
     * The list of commands defined in RFC 3435
     */
    static TokenDict mgcp_commands[];

    /**
     * The list of known responses defined in RFC 3435 2.4
     */
    static TokenDict mgcp_responses[];

    /**
     * The list of known reason codes defined in RFC 3435 2.5.
     * Reason codes are used by the gateway when deleting a connection to
     *  inform the Call Agent about the reason for deleting the connection.
     *  They may also be used in a RestartInProgress command to inform the
     *  Call Agent of the reason for the RestartInProgress
     */
    static TokenDict mgcp_reasons[];

protected:
    /**
     * Send a string buffer through the socket
     * @param msg The buffer to send
     * @param address The destination IP address
     * @return False if the operation failed
     */
    bool sendData(const String& msg, const SocketAddr& address);

    /**
     * Append a transaction to the list
     * @param trans The transaction to append
     */
    void appendTrans(MGCPTransaction* trans);

    /**
     * Remove a transaction from the list
     * @param trans The transaction to remove
     * @param del True to delete it, false to just remove it from list
     */
    void removeTrans(MGCPTransaction* trans, bool del);

    /**
     * The list of endpoints attached to this engine
     */
    ObjList m_endpoints;

    /**
     * The transaction list
     */
    ObjList m_transactions;

    /**
     * The transaction list iterator used to get events
     */
    ListIterator m_iterator;

private:
    // Append a private thread to the list
    void appendThread(MGCPPrivateThread* thread);
    // Remove private thread from the list without deleting it
    void removeThread(MGCPPrivateThread* thread);
    // Process ACK received with a message or response
    // Return a list of ack'd transactions or 0 if the parameter is incorrect
    unsigned int* decodeAck(const String& param, unsigned int & count);

    bool m_gateway;                      // True if this engine is an MGCP gateway, false if call agent
    bool m_initialized;                  // True if the engine was already initialized
    unsigned int m_nextId;               // Next outgoing transaction id
    Socket m_socket;                     // The socket used to send/receive data
    SocketAddr m_address;                // The IP address used by this engine
    unsigned int m_maxRecvPacket;        // The maximum length or received packets
    unsigned char* m_recvBuf;            // Receiving buffer
    bool m_allowUnkCmd;                  // Allow this engine to send/accept unknown commands
    unsigned int m_retransInterval;      // Message retransmission interval
    unsigned int m_retransCount;         // Maximum number of retransmissions for a message
    u_int64_t m_extraTime;               // Time to live after the transaction terminated gracefully
    bool m_parseParamToLower;            // Convert received messages' params to lower case
    bool m_provisional;                  // Send provisional responses flag
    bool m_ackRequest;                   // Remote is requested to send ACK
    ObjList m_knownCommands;             // The list of known commands
    ObjList m_threads;
};

}

#endif /* __YATEMGCP_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
