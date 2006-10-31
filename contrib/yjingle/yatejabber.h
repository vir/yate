/**
 * yatejabber.h
 * Yet Another Jabber Component Protocol Stack
 * This file is part of the YATE Project http://YATE.null.ro
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

#ifndef __YATEJABBER_H
#define __YATEJABBER_H

#include <xmpputils.h>
#include <xmlparser.h>

/**
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

class JBEvent;
class JBComponentStream;
class JBServerInfo;
class JBEngine;
class JBClient;
class JBPresence;

/**
 * This class holds a Jabber Component stream event.
 * @short A Jabber Component event.
 */
class YJINGLE_API JBEvent : public RefObject
{
    friend class JBComponentStream;
public:
    enum Type {
	// Stream events
	Terminated              = 1,     // Stream terminated. Try to connect
	Destroy                 = 2,     // Stream is destroying
	// Result events
	WriteFail               = 10,    // Write failed
	                                 //  m_element is the element
	                                 //  m_id is the id set by the sender
	// Stanza events
	Presence                = 20,    // m_element is a 'presence' stanza
	Message                 = 30,    // m_element is a 'message' stanza
	Iq                      = 50,    // m_element is an unknown 'iq' element
	                                 //  m_child may be an unexpected element
	IqError                 = 51,    // m_element is an 'iq' error
	                                 //  m_child is the 'error' child if any
	IqResult                = 52,    // m_element is an 'iq' result
	IqDiscoGet              = 60,    // m_element is an 'iq' get
	                                 //  m_child is a 'query' element qualified by
	                                 //  XMPPNamespace::DiscoInfo namespace
	IqDiscoSet              = 61,    // m_element is an 'iq' set
	                                 //  m_child is a 'query' element qualified by
	                                 //  XMPPNamespace::DiscoInfo namespace
	IqDiscoRes              = 62,    // m_element is an 'iq' result
	                                 //  m_child is a 'query' element qualified by
	                                 //  XMPPNamespace::DiscoInfo namespace
	IqJingleGet             = 100,   // m_element is an 'iq' get
	                                 //  m_child is a 'jingle' element
	IqJingleSet             = 101,   // m_element is an 'iq' set
	                                 //  m_child is a 'jingle' element
	// Invalid
	Unhandled               = 200,   // m_element is an unhandled element
	Invalid                 = 500,   // m_element is 0
    };

    /**
     * Destructor.
     * Delete the XML element if valid.
     */
    virtual ~JBEvent();

    /**
     * Get the event type.
     * @return the type of this event as enumeration.
     */
    inline Type type() const
	{ return m_type; }

    /**
     * Get the element's 'type' attribute if any.
     * @return The  element's 'type' attribute.
     */
    inline const String& stanzaType() const
	{ return m_stanzaType; }

    /**
     * Get the 'from' data.
     * @return The 'from' data.
     */
    inline const String& from() const
	{ return m_from; }

    /**
     * Get the 'to' data.
     * @return The 'to' data.
     */
    inline const String& to() const
	{ return m_to; }

    /**
     * Get the 'id' data.
     * @return The 'id' data.
     */
    inline const String& id() const
	{ return m_id; }

    /**
     * Get the stream.
     * @return The stream.
     */
    inline JBComponentStream* stream() const
	{ return m_stream; }

    /**
     * Get the underlying XMLElement.
     * @return XMLElement pointer or 0.
     */
    inline XMLElement* element() const
	{ return m_element; }

    /**
     * Get the first child of the underlying element if any.
     * @return XMLElement pointer or 0.
     */
    inline XMLElement* child() const
	{ return m_child; }

    /**
     * Get the underlying XMLElement. Release the ownership.
     * The caller is responsable of returned pointer.
     * @return XMLElement pointer or 0.
     */
    inline XMLElement* releaseXML() {
	    XMLElement* tmp = m_element;
	    m_element = 0;
	    return tmp;
	}

    /**
     * Release the link with the stream to let the stream continue with events.
     */
    void releaseStream();

protected:
    /**
     * Constructor.
     * Constructs an internal stream event.
     * @param type Type of this event.
     * @param stream The stream that generated the event.
     */
    JBEvent(Type type, JBComponentStream* stream);

    /**
     * Constructor.
     * Constructs an event from a stream.
     * @param type Type of this event.
     * @param stream The stream that generated the event.
     * @param element Element that generated the event.
     * @param child Optional type depending element's child.
     */
    JBEvent(Type type, JBComponentStream* stream,
	XMLElement* element, XMLElement* child = 0);

    /**
     * Constructor.
     * Constructs a WriteSuccess/WriteFail event from a stream.
     * @param type Type of this event.
     * @param stream The stream that generated the event.
     * @param element Element that generated the event.
     * @param senderID Sender's id.
     */
    JBEvent(Type type, JBComponentStream* stream, XMLElement* element,
	const String& senderID);

private:
    JBEvent() {}                         // Don't use it!
    bool init(JBComponentStream* stream, XMLElement* element);

    Type m_type;                         // Type of this event
    JBComponentStream* m_stream;         // The stream that generated this event
    bool m_link;                         // Stream link state
    XMLElement* m_element;               // Received XML element, if any
    XMLElement* m_child;                 // The first child element for 'iq' elements
    String m_stanzaType;                 // Stanza's 'type' attribute
    String m_from;                       // Stanza's 'from' attribute
    String m_to;                         // Stanza's 'to' attribute
    String m_id;                         // Sender's id for Write... events
                                         // 'id' attribute if the received stanza has one
};

/**
 * This class holds a Jabber Component stream (implements the Jabber Component Protocol).
 * @short A Jabber Component stream.
 */
class YJINGLE_API JBComponentStream : public RefObject, public Mutex
{
public:
    friend class JBEngine;
public:
    /**
     * Stream state enumeration.
     */
    enum State {
	WaitToConnect,                   // Outgoing stream is waiting for the socket to connect
	Started,                         // Stream start sent
	Auth,                            // Authentication (handshake) sent
	Running,                         // Authenticated. Allow any XML element to pass over the stream
	Terminated,                      // Stream is terminated. Wait to be restarted or destroyed
	Destroy,                         // Stream is destroying. No more traffic allowed
    };

    /**
     * Values returned by send() methods.
     */
    enum Error {
	ErrorNone = 0,                   // No error
	ErrorContext,                    // Invalid stream context (state) or parameters.
	ErrorPending,                    // The operation is pending in the stream's queue.
	ErrorNoSocket,                   // Unrecoverable socket error.
	                                 // The stream will be terminated.
    };

    /**
     * Destructor.
     * Close the stream and the socket.
     */
    virtual ~JBComponentStream();

    /**
     * Get the stream state.
     * @return The stream state as enumeration.
     */
    inline State state() const
	{ return m_state; }

    /**
     * Get the local name.
     * @return The local name.
     */
    inline const String& localName() const
	{ return m_localName; }

    /**
     * Get the remote server name.
     * @return The remote server name.
     */
    inline const String& remoteName() const
	{ return m_remoteName; }

    /**
     * Get the local name.
     * @return The local name.
     */
    inline const SocketAddr& remoteAddr() const
	{ return m_remoteAddr; }

    /**
     * Get the stream id.
     * @return The stream id.
     */
    inline const String& id() const
	{ return m_id; }

    /**
     * Get the stream's connection.
     * @return The stream connection's pointer.
     */
    inline JBEngine* engine() const
	{ return m_engine; }

    /**
     * Check if the caller of connect() should wait before.
     * @return True to wait.
     */
    inline bool waitBeforeConnect() const
	{ return m_waitBeforeConnect; }

    /**
     * Connect the stream to the server.
     */
    void connect();

    /**
     * Cleanup the stream. Terminate/Destroy it. Raise the appropriate event.
     * This method is thread safe.
     * @param destroy True to destroy. False to terminate.
     * @param sendEnd True to send stream termination tag.
     * @param error Optional XML element to send before termination tag.
     * @param sendError True to send the error element.
     */
    void terminate(bool destroy, bool sendEnd = false,
	XMLElement* error = 0, bool sendError = false);

    /**
     * Read data from socket and pass it to the parser.
     * Raise event on bad XML.
     * This method is thread safe.
     * @return True if data was received.
     */
    bool receive();

    /**
     * Send a stanza.
     * This method is thread safe.
     * @param stanza Element to send.
     * @param senderId Optional sender's id. Used for notification events.
     * @return The result of posting the stanza.
     */
    Error sendStanza(XMLElement* stanza, const char* senderId = 0);

    /**
     * Extract an element from parser and construct an event.
     * This method is thread safe.
     * @param time Current time.
     * @return XMPPEvent pointer or 0.
     */
    JBEvent* getEvent(u_int64_t time);

    /**
     * Cancel pending outgoing elements.
     * @param raise True to raise WriteFail events.
     * @param id Optional 'id' to cancel. 0 to cancel all elements without id.
     */
    void cancelPending(bool raise, const String* id = 0);

    /**
     * Event termination notification.
     * @param event The notifier. Must be m_lastEvent.
     */
    void eventTerminated(const JBEvent* event);

protected:
    /**
     * Constructor.
     * Constructs an outgoing stream.
     * @param connection The engine that owns this stream.
     * @param remoteName The remote domain name.
     * @param remoteAddr The remote address to connect.
     */
    JBComponentStream(JBEngine* engine, const String& remoteName,
	const SocketAddr& remoteAddr);

    /**
     * Send data without passing it through the outgoing queue.
     * Change state if operation succeeds. Terminate stream if it fails.
     * This method is thread safe.
     * @param element The XML element to send.
     * @param newState The new state if the operation succeeds.
     * @param newState Optional XML element to send before element.
     * @return False if the write operation fails.
     */
    bool sendStreamXML(XMLElement* element, State newState,
	XMLElement* before = 0);

    /**
     * Send a 'stream:error' element. Terminate the stream.
     * @param error The XMPP defined condition.
     * @param text Optional text to add to the error.
     */
    inline void sendStreamError(XMPPError::Type error, const char* text = 0)
	{ terminate(false,true,XMPPUtils::createStreamError(error,text)); }

    /**
     * Send an 'iq' of type 'error'.
     * @param stanza Element that generated the error.
     * @param eType The error type.
     * @param eCond The error condition.
     * @param eText Optional text to add to the error stanza.
     * @return The result of posting the stanza.
     */
    Error sendIqError(XMLElement* stanza, XMPPError::ErrorType eType,
	XMPPError::Type eCond, const char* eText = 0);
    /**
     * Cleanup stream.
     * Remove the first element from the outgoing list if partially sent.
     * Send stream termination tag if requested.
     * Cancel all outgoing elements without id. Destroy the socket.
     * This method is thread safe.
     * @param endStream True to send stream termination tag.
     * @param element Optional element to send before.
     */
    void cleanup(bool endStream, XMLElement* element = 0);

    /**
     * Post an XMLElement in the outgoing queue. Send the first element in the queue.
     * This method is thread safe.
     * @param element The XMLElementOut to post.
     * @return ErrorNone, ErrorContext, ErrorPending, ErrorNoSocket.
     */
    Error postXML(XMLElementOut* element);

    /**
     * Send the first element from the outgoing queue if state is Running.
     * Raise WriteFail event if operation fails. Adjust buffer on partial writes.
     * @return ErrorNone, ErrorContext, ErrorPending, ErrorNoSocket.
     */
    Error sendXML();

    /**
     * Process the received XML elements. Validate them. Add events.
     * @return True if generated any event(s).
     */
    bool processIncomingXML();

    /**
     * Process a received element in state Started.
     * @param e The element to process.
     * @return True if generated any event(s).
     */
    bool processStateStarted(XMLElement* e);

    /**
     * Process a received element in state Auth.
     * @param e The element to process.
     * @return True if generated any event(s).
     */
    bool processStateAuth(XMLElement* e);

    /**
     * Process a received element in state Running.
     * @param e The element to process.
     * @return True if generated any event(s).
     */
    bool processStateRunning(XMLElement* e);

    /**
     * Process a received 'iq' element.
     * @param e The element to process.
     * @return True if generated any event(s).
     */
    bool processIncomingIq(XMLElement* e);

    /**
     * Add an event to the list.
     * @param type Event type.
     * @param element Optional XML element.
     * @param child Optional child element.
     * @return The added event.
     */
    JBEvent* addEvent(JBEvent::Type type, XMLElement* element = 0,
	XMLElement* child = 0);

    /**
     * Add a notification event to the list if the element has an id.
     * Remove the element from the outgoing queue.
     * @param type Event type.
     * @param element XMLElementOut element that generated the event.
     * @return True if the event was added.
     */
    bool addEventNotify(JBEvent::Type type, XMLElementOut* element);

    /**
     * Actions to take when an invalid element is received.
     * Delete element. Send a stream error to remote peer. Raise a Terminated event.
     * @param e The invalid element.
     * @param type Stream error type.
     * @param text Optional text to add to the stream error.
     * @return True.
     */
    bool invalidElement(XMLElement* e, XMPPError::Type type,
	const char* text = 0);

    /**
     * Actions to take when an unexpected element is received.
     * Delete element.
     * @param e The unexpected element.
     * @return False.
     */
    bool unexpectedElement(XMLElement* e);

    /**
     * Check if a given element is a termination one (stream error or stream end).
     * Raise a Terminated event if so.
     * If true is returned the element is still valid.
     * @param e The element to check.
     * @return True if a Terminated event was raised.
     */
    bool isStreamEnd(XMLElement* e);

    /**
     * Read data from socket and pass it to the parser. Terminate on error.
     * @param data The destination buffer.
     * @param len The maximum number of bytes to read. Bytes read on exit.
     * @return True if data was received.
     */
    bool readSocket(char* data, u_int32_t& len);

    /**
     * Write data to socket. Terminate on error.
     * @param data The source buffer.
     * @param len The buffer length on input. Bytes written on output.
     * @return False on socket error. State is Terminated on unrecoverable socket error.
     */
    bool writeSocket(const char* data, u_int32_t& len);

private:
    JBComponentStream() {}               // Don't use it!

    // State
    State m_state;                       // Stream state
    // Info
    String m_id;                         // Stream id
    String m_localName;                  // Local name received from the remote peer
    String m_remoteName;                 // Remote peer's domain name
    SocketAddr m_remoteAddr;             // Socket's address
    String m_password;                   // Password used for authentication
    // Data
    JBEngine* m_engine;                  // The owner of this stream
    Socket* m_socket;                    // The socket used by this stream
    XMLParser m_parser;                  // XML parser
    Mutex m_receiveMutex;                // Ensure serialization of received data
    ObjList m_outXML;                    // Outgoing XML elements
    ObjList m_events;                    // Event queue
    JBEvent* m_lastEvent;                // Last generated event
    JBEvent* m_terminateEvent;           // Destroy/Terminate event
    int m_partialRestart;                // Partial outgoing stream restart attempts counter
    int m_totalRestart;                  // Total outgoing stream restart attempts counter
    bool m_waitBeforeConnect;            // Wait before trying to connect
};

/**
 * This class holds info about a component server used by the Jabber engine.
 * @short Server info.
 */
class YJINGLE_API JBServerInfo : public RefObject
{
public:
    inline JBServerInfo(const char* name, const char* address, int port,
	const char* password, const char* identity)
	: m_name(name), m_address(address),
	m_port(port), m_password(password), m_identity(identity)
	{}
    virtual ~JBServerInfo() {}
    inline const String& address() const
	{ return m_address; }
    inline const String& name() const
	{ return m_name; }
    inline const int port() const
	{ return m_port; }
    inline const String& password() const
	{ return m_password; }
    inline const String& identity() const
	{ return m_identity; }
private:
    String m_name;                       // Domain name
    String m_address;                    // IP address
    int m_port;                          // Port
    String m_password;                   // Authentication data
    String m_identity;                   // Identity. Used for Jabber Component protocol
};

/**
 * This class holds a Jabber engine.
 * @short A Jabber engine.
 */
class YJINGLE_API JBEngine : public DebugEnabler, public Mutex, public RefObject
{
    friend class JBEvent;
    friend class JBComponentStream;
    friend class JBClient;
    friend class JBPresence;
public:
    /**
     * Jabber protocol type.
     */
    enum Protocol {
	Component,                       // Use Jabber Component protocol
    };

    /**
     * Constructor.
     * Constructs a Jabber engine.
     */
    JBEngine();

    /**
     * Destructor.
     */
    virtual ~JBEngine();

    /**
     * Get the Jabber protocol this engine is using.
     * @return The Jabber protocol as enumeration.
     */
    inline Protocol jabberProtocol() const
	{ return Component; }

    /**
     * Initialize the engine's parameters.
     * Parameters:
     * 	stream_partialrestart : int Partial stream restart counter for outgoing streams. Defaults to 3.
     * 	stream_totalrestart : int Total stream restart counter for outgoing streams. Defaults to -1 (no limit).
     * 	xmlparser_maxbuffer : int The maximum allowed xml buffer length. Defaults to 8192.
     * @param params Engine's parameters.
     */
    void initialize(const NamedList& params);

    /**
     * Terminate all stream.
     */
    void cleanup();

    /**
     * Set the default component server to use.
     * The domain must be in the server list.
     * Choose the first one from the server list if the given one doesn't exists.
     * @param domain Domain name of the server.
     */
    void setComponentServer(const char* domain);

    /**
     * Get the default component server.
     * @return The default component server.
     */
    const String& componentServer()
	{ return m_componentDomain; }

    /**
     * Get the partial stream restart attempts counter.
     * @return The partial stream restart attempts counter.
     */
    inline int partialStreamRestartAttempts() const
	{ return m_partialStreamRestart; }

    /**
     * Get the total stream restart attempts counter.
     * @return The total stream restart attempts counter.
     */
    inline int totalStreamRestartAttempts() const
	{ return m_totalStreamRestart; }

    /**
     * Get the time to wait after m_partialStreamRestart reaches 0.
     * @return time to wait after m_partialStreamRestart reaches 0.
     */
    inline u_int32_t waitStreamRestart() const
	{ return m_waitStreamRestart; }

    /**
     * Check if a stream to the given server exists.
     * If the stream doesn't exists creates it.
     * This method is thread safe.
     * @param domain The domain name to check. 0 to use the default server.
     * @param create True to create a stream to the specified domain if none exists.
     * @return Pointer to a JBComponentStream or 0.
     */
    JBComponentStream* getStream(const char* domain = 0, bool create = true);

    /**
     * Keep calling receive() for each stream until no data is received.
     * @return True if data was received.
     */
    bool receive();

    /**
     * Keep calling receive().
     */
    void runReceive();

    /**
     * Get events from the streams owned by this engine.
     * This method is thread safe.
     * @param time Current time.
     * @return JBEvent pointer or 0.
     */
    JBEvent* getEvent(u_int64_t time);

    /**
     * Check if an outgoing stream exists with the same id and remote peer.
     * @param connection The calling stream.
     * @return True if found.
     */
    bool remoteIdExists(const JBComponentStream* stream);

    /**
     * Create an SHA1 value from 'id' + 'password'.
     * @param sha Destination string.
     * @param id First element (stream id).
     * @param password The second element (stream password).
     */
    void createSHA1(String& sha, const String& id, const String& password);

    /**
     * Check if a received value is correct.
     * @param sha Destination string.
     * @param id First element (stream id).
     * @param password The second element (stream password).
     * @return True if equal.
     */
    bool checkSHA1(const String& sha, const String& id, const String& password);

    /**
     * Call the connect method of the given stream.
     * @param stream The stream to connect.
     * @return False if stream is 0.
     */
    virtual bool connect(JBComponentStream* stream);

    /**
     * Return a non processed event to this engine.
     * @param event The returned event.
     */
    virtual void returnEvent(JBEvent* event);

    /**
     * Accept an outgoing stream. If accepted, deliver a password.
     * @param remoteAddr The remote address.
     * @param password Password to use.
     * @return True if accepted. False to terminate the stream.
     */
    virtual bool acceptOutgoing(const String& remoteAddr, String& password);

    /**
     * Deliver a port for an outgoing connection.
     * @param remoteAddr The remote address.
     * @return True if accepted. False to terminate the stream.
     */
    virtual int getPort(const String& remoteAddr);

    /**
     * Append a servr info element to the list.
     * @param server The object to add.
     * @param open True to open the stream.
     */
    void appendServer(JBServerInfo* server, bool open);

    /**
     * Get the identity of the given server for a component server.
     * @param destination The destination.
     * @param token The search string. If 0 the default server will be used.
     * @param domain True to find by domain name. False to find by address.
     * @return False if server doesn't exists.
     */
    bool getServerIdentity(String& destination, const char* token = 0,
	bool domain = true);

    /**
     * Get the full identity of the given server.
     * @param destination The destination.
     * @param token The search string. If 0 the default server will be used.
     * @param domain True to find by domain name. False to find by address.
     * @return False if server doesn't exists.
     */
    bool getFullServerIdentity(String& destination, const char* token = 0,
	bool domain = true);

protected:
    /**
     * Process a DiscoInfo event.
     * @param event The received event.
     * @return True if processed.
     */
    bool processDiscoInfo(JBEvent* event);

    /**
     * Check if a stream with a given remote address exists.
     * @param remoteName The remote address to find.
     * @return Stream pointer or 0.
     */
    JBComponentStream* findStream(const String& remoteName);

    /**
     * Remove a stream from the list.
     * @param stream The stream to remove.
     * @param del Delete flag. If true the stream will be deleted.
     */
    void removeStream(JBComponentStream* stream, bool del);

    /**
     * Add a client to this engine if not in it.
     * @param client The client to add.
     */
    void addClient(JBClient* client);

    /**
     * Remove a client to this engine if not in it.
     * @param client The client to remove.
     */
    void removeClient(JBClient* client);

    /**
     * Find server info object.
     * @param token The search string. If 0 the default server will be used.
     * @param domain True to find by domain name. False to find by address.
     * @return JBServerInfo pointer or 0.
     */
    JBServerInfo* getServer(const char* token = 0, bool domain = true);

    /**
     * Clear the server list.
     */
    inline void clearServerList()
	{ Lock lock(m_serverMutex); m_server.clear(); }

    /**
     * Process a message event.
     * @param event The event to process. Always a valid message event.
     * @return True if the event was processed (kept). False to destroy it.
     */
    virtual bool processMessage(JBEvent* event);

private:
    void processEventNew(JBEvent* event);
    void processEventAuth(JBEvent* event);
    bool getServerPassword(String& destination, const char* token = 0,
	bool domain = true);
    bool getServerPort(int& destination, const char* token = 0,
	bool domain = true);
    void setPresenceServer(JBPresence* presence);
    void unsetPresenceServer(JBPresence* presence);

    ObjList m_streams;                   // Streams belonging to this engine
    Mutex m_clientsMutex;                // Lock clients list
    ObjList m_clients;                   // XMPP clients list
    JBPresence* m_presence;              // The presence server
    Mutex m_featuresMutex;               // Lock m_feature
    ObjList m_features;                  // Remote peers' features
    int m_partialStreamRestart;          // Partial outgoing stream restart attempts counter
    int m_totalStreamRestart;            // Total outgoing stream restart attempts counter
    u_int32_t m_waitStreamRestart;       // How much time to wait after m_partialStreamRestart reaches 0
    // ID generation data
    u_int64_t m_streamID;                // Stream id counter
    // Server list
    String m_componentDomain;            // Default server domain name
    String m_componentAddr;              // Default server address
    ObjList m_server;                    // Server list
    Mutex m_serverMutex;                 // Lock server list
};

/**
 * This class is the base class for a Jabber client who wants
 *  to deliver protocol specific data to the engine.
 * @short An Jabber client.
 */
class YJINGLE_API JBClient : public RefObject
{
public:
    /**
     * Constructor.
     * @param engine The Jabber engine.
     */
    JBClient(JBEngine* engine);

    /**
     * Destructor.
     */
    virtual ~JBClient();

    /**
     * Get the Jabber engine.
     * @return The Jabber engine.
     */
    JBEngine* engine()
	{ return m_engine; }

protected:
    JBEngine* m_engine;                  // The Jabber Component engine

private:
    inline JBClient() {}                 // Don't use it !
};

/**
 * This class is the presence server for Jabber.
 * @short A Jabber presence server.
 */
class YJINGLE_API JBPresence : public DebugEnabler, public JBClient, public Mutex
{
public:
    /**
     * Presence enumeration.
     */
    enum Presence {
	Error,                           // error
	Probe,                           // probe
	Subscribe,                       // subscribe request
	Subscribed,                      // subscribe accepted
	Unavailable,                     // unavailable
	Unsubscribe,                     // unsubscribe request
	Unsubscribed,                    // unsubscribe accepted
	None,
    };

    /**
     * Constructor.
     * Constructs an Jabber Component presence server.
     * @param engine The Jabber Component engine.
     */
    JBPresence(JBEngine* engine);

    /**
     * Destructor.
     */
    virtual ~JBPresence();

    /**
     * Add an event to the list.
     * This method is thread safe.
     * @param event The event to add.
     * @return False if the event is not a Presence one.
     */
    bool receive(JBEvent* event);

    /**
     * Process an event from the receiving list.
     * This method is thread safe.
     * @return False if the list is empty.
     */
    bool process();

    /**
     * Keep calling process().
     */
    void runProcess();

    /**
     * Process disco info elements.
     * @param event The event with the element.
     */
    virtual void processDisco(JBEvent* event);

    /**
     * Process a presence error element.
     * @param event The event with the element.
     */
    virtual void processError(JBEvent* event);

    /**
     * Process a presence probe element.
     * @param event The event with the element.
     */
    virtual void processProbe(JBEvent* event);

    /**
     * Process a presence subscribe element.
     * @param event The event with the element.
     */
    virtual void processSubscribe(JBEvent* event);

    /**
     * Process a presence subscribed element.
     * @param event The event with the element.
     */
    virtual void processSubscribed(JBEvent* event);

    /**
     * Process a presence unsubscribe element.
     * @param event The event with the element.
     */
    virtual void processUnsubscribe(JBEvent* event);

    /**
     * Process a presence unsubscribed element.
     * @param event The event with the element.
     */
    virtual void processUnsubscribed(JBEvent* event);

    /**
     * Process a presence unavailable element.
     * @param event The event with the element.
     */
    virtual void processUnavailable(JBEvent* event);

    /**
     * Process a presence element.
     * @param event The event with the element.
     */
    virtual void processUnknown(JBEvent* event);

    /**
     * Create an 'presence' element.
     * @param from The 'from' attribute.
     * @param to The 'to' attribute.
     * @param type Presence type as enumeration.
     * @return A valid XMLElement pointer.
     */
    static XMLElement* createPresence(const char* from,
	const char* to, Presence type = None);

    /**
     * Decode an error element.
     * @param element The XML element.
     * @param code The 'code' attribute.
     * @param type The 'type' attribute.
     * @param error The name of the 'error' child.
     * @return False if 'element' is 0 or is not a presence one.
     */
    static bool decodeError(const XMLElement* element,
	String& code, String& type, String& error);

    /**
     * Get the type of a 'presence' stanza as enumeration.
     * @param text The text to check.
     * @return Presence type as enumeration.
     */
    static inline Presence presenceType(const char* txt)
	{ return (Presence)lookup(txt,s_presence,None); }
    /**
     * Get the text from a presence type.
     * @param presence The presence type.
     * @return The associated text or 0.
     */
    static inline const char* presenceText(Presence presence)
	{ return lookup(presence,s_presence,0); }

protected:
    ObjList m_events;                    // Incoming events from Jabber engine
    static TokenDict s_presence[];       // Keep the types of 'presence'
};

};

#endif /* __YATEJABBER_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
