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
class JIDResource;
class JIDResourceList;
class XMPPUser;
class XMPPUserRoster;

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
	IqCommandGet            = 70,    // m_element is an 'iq' of type get
	                                 //  m_child is a 'command' element qualified by
	                                 //  XMPPNamespace::Command namespace
	IqCommandSet            = 71,    // m_element is an 'iq' of type set
	                                 //  m_child is a 'command' element qualified by
	                                 //  XMPPNamespace::Command namespace
	IqCommandRes            = 72,    // m_element is an 'iq' result
	                                 //  m_child is a 'command' element qualified by
	                                 //  XMPPNamespace::Command namespace
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
     * @param engine The engine that owns this stream.
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
     * @param before Optional XML element to send before element.
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
};

/**
 * This class holds info about a component server used by the Jabber engine.
 * @short Server info.
 */
class YJINGLE_API JBServerInfo : public RefObject
{
public:
    inline JBServerInfo(const char* name, const char* address, int port,
	const char* password, const char* identity, const char* fullidentity,
	bool roster, bool autoRestart, u_int32_t restartCount)
	: m_name(name), m_address(address), m_port(port), m_password(password),
	m_identity(identity), m_fullIdentity(fullidentity), m_roster(roster),
	m_autoRestart(autoRestart), m_restartCount(restartCount)
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
    inline const String& fullIdentity() const
	{ return m_fullIdentity; }
    inline bool roster() const
	{ return m_roster; }
    inline bool autoRestart() const
	{ return m_autoRestart; }
    inline u_int32_t restartCount() const
	{ return m_restartCount; }
    inline void incRestart()
	{ m_restartCount++; }
    inline bool getRestart() {
	    if (!restartCount())
		return false;
	    m_restartCount--;
	    return true;
	}
private:
    String m_name;                       // Domain name
    String m_address;                    // IP address
    int m_port;                          // Port
    String m_password;                   // Authentication data
    String m_identity;                   // Identity. Used for Jabber Component protocol
    String m_fullIdentity;               // Full identity for this server
    bool m_roster;                       // Keep roster for this server
    bool m_autoRestart;                  // Automatically restart stream
    u_int32_t m_restartCount;            // Restart counter
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
     * 	stream_restartupdateinterval : int Interval to update (increase) the stream restart counter. Defaults to 15000.
     * 	stream_restartcount : int Max stream restart counter. Defaults to 4.
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
     * Set the alternate domain name
     * @param domain Name of an acceptable alternate domain
     */
    inline void setAlternateDomain(const char* domain = 0)
	{ m_alternateDomain = domain; }

    /**
     * Get the default stream restart count.
     * @return The default stream restart count.
     */
    inline u_int32_t restartCount() const
	{ return m_restartCount; }

    /**
     * Get the default resource name.
     * @return The default resource name.
     */
    inline const String& defaultResource() const
	{ return m_defaultResource; }

    /**
     * Check if a sender or receiver of XML elements should print them to output.
     * @return True to print XML element to output.
     */
    inline bool printXml() const
	{ return m_printXml; }

    /**
     * Set/reset print XML elements to output permission.
     * @param print True to allow XML elements printing to output.
     */
    inline void printXml(bool print)
	{ m_printXml = print; }

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
     * @param stream The calling stream.
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
     * Called to update time dependent values
     * @param time Current time.
     */
    virtual void timerTick(u_int64_t time);

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
     * Check if the process is terminating.
     * @return True if the process is terminating.
     */
    virtual bool exiting()
	{ return false; }

    /**
     * Append a servr info element to the list.
     * @param server The object to add.
     * @param open True to open the stream.
     */
    void appendServer(JBServerInfo* server, bool open);

    /**
     * Find server info object.
     * @param token The search string. If 0 the default server will be used.
     * @param domain True to find by domain name. False to find by address.
     * @return Referenced JBServerInfo pointer or 0.
     */
    JBServerInfo* getServer(const char* token = 0, bool domain = true);

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

    /**
     * Get the name of the alternate domain - if any
     * @return Alternate domain name, empty string if not set
     */
    inline const String& getAlternateDomain() const
	{ return m_alternateDomain; }

    /**
     * Check if a stream to a remote server can be restarted.
     * If true is returned, the stream restart counter has been decreased.
     * @param token The remote server name or address.
     * @param domain True to find by domain name. False to find by address.
     * @return False if server doesn't exists.
     */
    bool getStreamRestart(const char* token, bool domain = true);

protected:
    /**
     * Process a DiscoInfo event.
     * @param event The received event.
     * @return True if processed.
     */
    bool processDiscoInfo(JBEvent* event);

    /**
     * Process a Command event.
     * @param event The received event.
     * @return True if processed.
     */
    bool processCommand(JBEvent* event);

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
    JIDIdentity* m_identity;             // Engine's identity
    JIDFeatureList m_features;           // Engine's features
    u_int64_t m_restartUpdateTime;       // Time to update the restart counter of all streams
    u_int32_t m_restartUpdateInterval;   // Update interval for restart counter of all streams
    u_int32_t m_restartCount;            // The default restart counter value
    bool m_printXml;                     // Print XML data to output
    // ID generation data
    u_int64_t m_streamID;                // Stream id counter
    // Server list
    String m_componentDomain;            // Default server domain name
    String m_componentAddr;              // Default server address
    ObjList m_server;                    // Server list
    Mutex m_serverMutex;                 // Lock server list
    String m_alternateDomain;            // Alternate acceptable domain
    // Misc
    String m_defaultResource;            // Default name for missing resources
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
    friend class XMPPUserRoster;
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
     * @param params Engine's parameters.
     */
    JBPresence(JBEngine* engine, const NamedList& params);

    /**
     * Destructor.
     */
    virtual ~JBPresence();

    /**
     * Get the auto subscribe parameter.
     * @return The auto subscribe parameter.
     */
    inline int autoSubscribe() const
	{ return m_autoSubscribe; }

    /**
     * Check if the unavailable resources must be deleted.
     * @return The delete unavailable parameter.
     */
    inline bool delUnavailable() const
	{ return m_delUnavailable; }

    /**
     * Check if this server should add new users when receiving subscribe stanzas.
     * @return True if should add a new user.
     */
    inline bool addOnSubscribe() const
	{ return m_addOnSubscribe; }

    /**
     * Check if this server should add new users when receiving presence probes.
     * @return True if should add a new user.
     */
    inline bool addOnProbe() const
	{ return m_addOnProbe; }

    /**
     * Check if this server should add new users when receiving presence.
     * @return True if should add a new user
     */
    inline bool addOnPresence() const
	{ return m_addOnPresence; }

    /**
     * Get the probe interval. Time to send a probe if nothing was received from that user.
     * @return The probe interval.
     */
    inline u_int32_t probeInterval()
	{ return m_probeInterval; }

    /**
     * Get the expire after probe interval.
     * @return The expire after probe interval.
     */
    inline u_int32_t expireInterval()
	{ return m_expireInterval; }

    /**
     * Initialize the engine.
     * @param params Engine's parameters.
     */
    void initialize(const NamedList& params);

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
     * @param local The local (destination) user.
     * @param remote The remote (source) user.
     */
    virtual void processDisco(JBEvent* event, const JabberID& local,
	const JabberID& remote);

    /**
     * Process a presence error element.
     * @param event The event with the element.
     * @param local The local (destination) user.
     * @param remote The remote (source) user.
     */
    virtual void processError(JBEvent* event, const JabberID& local,
	const JabberID& remote);

    /**
     * Process a presence probe element.
     * @param event The event with the element.
     * @param local The local (destination) user.
     * @param remote The remote (source) user.
     */
    virtual void processProbe(JBEvent* event, const JabberID& local,
	const JabberID& remote);

    /**
     * Process a presence subscribe element.
     * @param event The event with the element.
     * @param presence Presence type: Subscribe,Subscribed,Unsubscribe,Unsubscribed.
     * @param local The local (destination) user.
     * @param remote The remote (source) user.
     */
    virtual void processSubscribe(JBEvent* event, Presence presence,
	const JabberID& local, const JabberID& remote);

    /**
     * Process a presence unavailable element.
     * @param event The event with the element.
     * @param local The local (destination) user.
     * @param remote The remote (source) user.
     */
    virtual void processUnavailable(JBEvent* event, const JabberID& local,
	const JabberID& remote);

    /**
     * Process a presence element.
     * @param event The event with the element.
     * @param local The local (destination) user.
     * @param remote The remote (source) user.
     */
    virtual void processPresence(JBEvent* event, const JabberID& local,
	const JabberID& remote);

    /**
     * Notify on probe request with users we don't know about.
     * @param event The event with the element.
     * @param local The local (destination) user.
     * @param remote The remote (source) user.
     * @return False to send item-not-found error.
     */
    virtual bool notifyProbe(JBEvent* event, const JabberID& local,
	const JabberID& remote);

    /**
     * Notify on subscribe event with users we don't know about.
     * @param event The event with the element.
     * @param local The local (destination) user.
     * @param remote The remote (source) user.
     * @param presence Presence type: Subscribe,Subscribed,Unsubscribe,Unsubscribed.
     * @return False to send item-not-found error.
     */
    virtual bool notifySubscribe(JBEvent* event,
	const JabberID& local, const JabberID& remote, Presence presence);

    /**
     * Notify on subscribe event.
     * @param user The user that received the event.
     * @param presence Presence type: Subscribe,Subscribed,Unsubscribe,Unsubscribed.
     */
    virtual void notifySubscribe(XMPPUser* user, Presence presence);

    /**
     * Notify on presence event with users we don't know about or presence unavailable
     *  received without resource (the remote user is entirely unavailable).
     * @param event The event with the element.
     * @param local The local (destination) user.
     * @param remote The remote (source) user.
     * @param available The availability of the remote user.
     * @return False to send item-not-found error.
     */
    virtual bool notifyPresence(JBEvent* event, const JabberID& local,
	const JabberID& remote, bool available);

    /**
     * Notify on state/capabilities change.
     * @param user The user that received the event.
     * @param resource The resource that changet its state or capabilities.
     */
    virtual void notifyPresence(XMPPUser* user, JIDResource* resource);

    /**
     * Notify when a new user is added.
     * Used basically to add a local resource.
     * @param user The new user.
     */
    virtual void notifyNewUser(XMPPUser* user);

    /**
     * Get a roster. Add a new one if requested.
     * This method is thread safe.
     * @param jid The user's jid.
     * @param add True to add the user if doesn't exists.
     * @param added Optional parameter to be set if a new user was added.
     * @return Referenced pointer or 0 if none.
     */
    XMPPUserRoster* getRoster(const JabberID& jid, bool add, bool* added);

    /**
     * Get a remote peer of a local one. Add a new one if requested.
     * This method is thread safe.
     * @param local The local peer.
     * @param remote The remote peer.
     * @param addLocal True to add the local user if doesn't exists.
     * @param addedLocal Optional parameter to be set if a new local user was added.
     * @param addRemote True to add the remote user if doesn't exists.
     * @param addedRemote Optional parameter to be set if a new remote user was added.
     * @return Referenced pointer or 0 if none.
     */
    XMPPUser* getRemoteUser(const JabberID& local, const JabberID& remote,
	bool addLocal, bool* addedLocal, bool addRemote, bool* addedRemote);

    /**
     * Remove a remote peer of a local one.
     * This method is thread safe.
     * @param local The local peer.
     * @param remote The remote peer.
     */
    void removeRemoteUser(const JabberID& local, const JabberID& remote);

    /**
     * Check if the given domain is a valid (known) one.
     * @param domain The domain name to check.
     * @return True if the given domain is a valid one.
     */
    bool validDomain(const String& domain);

    /**
     * Try to get a stream from Jabber engine if stream parameter is 0.
     * @param stream Stream to check.
     * @param release Set to true on exit if the caller must deref the stream.
     * @return True if stream is valid.
     */
    bool getStream(JBComponentStream*& stream, bool& release);

    /**
     * Send an element through the given stream.
     * If the stream is 0 try to get one from the engine.
     * In any case the element is consumed (deleted).
     * @param element Element to send.
     * @param stream The stream to send through.
     * @return The result of send operation. False if element is 0.
     */
    bool sendStanza(XMLElement* element, JBComponentStream* stream);

    /**
     * Send an error. Error type is 'modify'.
     * If id is 0 sent element will be of type 'presence'. Otherwise: 'iq'.
     * @param type The error.
     * @param from The from attribute.
     * @param to The to attribute.
     * @param element The element that generated the error.
     * @param stream Optional stream to use.
     * @param id Optional id. If present (even if empty) the error element will be of type 'iq'.
     * @return The result of send operation.
     */
    bool sendError(XMPPError::Type type, const String& from, const String& to,
	XMLElement* element, JBComponentStream* stream = 0, const String* id = 0);

    /**
     * Check timeout.
     * This method is thread safe.
     * @param time Current time.
     */
    void checkTimeout(u_int64_t time);

    /**
     * Keep calling checkTimeout().
     */
    void runCheckTimeout();

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
    static inline Presence presenceType(const char* text)
	{ return (Presence)lookup(text,s_presence,None); }

    /**
     * Get the text from a presence type.
     * @param presence The presence type.
     * @return The associated text or 0.
     */
    static inline const char* presenceText(Presence presence)
	{ return lookup(presence,s_presence,0); }

    /**
     * Cleanup rosters.
     */
    void cleanup();

protected:
    /**
     * Check if the given jid has a valid domain. Send error if not.
     * @param event The event with element.
     * @param jid The destination jid.
     * @return True if jid has a valid domain.
     */
    bool checkDestination(JBEvent* event, const JabberID& jid);

    static TokenDict s_presence[];       // Keep the types of 'presence'
    int m_autoSubscribe;                 // Auto subscribe state 
    bool m_delUnavailable;               // Delete unavailable user or resource
    bool m_addOnSubscribe;               // Add new user on subscribe request
    bool m_addOnProbe;                   // Add new user on probe request
    bool m_addOnPresence;                // Add new user on presence
    bool m_autoProbe;                    // Automatically respond to probe requests
    u_int32_t m_probeInterval;           // Interval to probe a remote user
    u_int32_t m_expireInterval;          // Expire interval after probe
    ObjList m_events;                    // Incoming events from Jabber engine
    ObjList m_rosters;                   // The rosters

private:
    void addRoster(XMPPUserRoster* ur);
    void removeRoster(XMPPUserRoster* ur);
};

/**
 * This class holds a JID resource (name,presence,capabilities).
 * @short A JID resource.
 */
class YJINGLE_API JIDResource : public RefObject
{
public:
    /**
     * Resource capabilities enumeration.
     */
    enum Capability {
	CapChat                = 1,      // Chat capability
	CapAudio               = 2,      // Jingle capability
    };

    /**
     * Resource presence enumeration.
     */
    enum Presence {
	Unknown                = 0,      // unknown
	Available              = 1,      // available
	Unavailable            = 2,      // unavailable
    };

    /**
     * Values of the 'show' child of a presence element.
     */
    enum Show {
	ShowAway,                        // away : Temporarily away
	ShowChat,                        // chat : Actively interested in chatting
	ShowDND,                         // dnd  : Busy
	ShowXA,                          // xa   : Extended away
	ShowNone,                        //      : Missing or no text
    };

    /**
     * Constructor. Set data members.
     * @param name The resource name.
     * @param presence The resource presence.
     * @param capability The resource capability.
     */
    inline JIDResource(const char* name, Presence presence = Unknown,
	u_int32_t capability = CapChat)
	: m_name(name), m_presence(presence),
	  m_capability(capability), m_show(ShowNone)
	{}

    /**
     * Destructor.
     */
    inline virtual ~JIDResource()
	{}

    /**
     * Get the resource name.
     * @return The resource name.
     */
    inline const String& name() const
	{ return m_name; }

    /**
     * Get the presence attribute.
     * @return The presence attribute.
     */
    inline Presence presence() const
	{ return m_presence; }

    /**
     * Check if the resource is available.
     * @return True if the resource is available.
     */
    inline bool available() const
	{ return (m_presence == Available); }

    /**
     * Get the show attribute as enumeration.
     * @return The show attribute as enumeration.
     */
    inline Show show() const
	{ return m_show; }

    /**
     * Set the show attribute.
     * @param s The new show attribute.
     */
    inline void show(Show s)
	{ m_show = s; }

    /**
     * Get the status of this resource.
     * @return The status of this resource.
     */
    inline const String& status() const
	{ return m_status; }

    /**
     * Set the status of this resource.
     * @param s The new status of this resource.
     */
    inline void status(const char* s)
	{ m_status = s; }

    /**
     * Set the presence information.
     * @param value True if available, False if not.
     * @return True if presence changed.
     */
    bool setPresence(bool value);

    /**
     * Check if the resource has the required capability.
     * @param capability The required capability.
     * @return True if the resource has the required capability.
     */
    inline bool hasCap(Capability capability) const
	{ return (m_capability & capability) != 0; }

    /**
     * Update resource from a presence element.
     * @param element A presence element.
     * @return True if presence or capability changed changed.
     */
    bool fromXML(XMLElement* element);

    /**
     * Add capabilities to a presence element.
     * @param element The target presence element.
     */
    void addTo(XMLElement* element);

    /**
     * Get the 'show' child of a presence element.
     * @param element The XML element.
     * @return The text or 0.
     */
    static const char* getShow(XMLElement* element);

    /**
     * Get the 'show' child of a presence element.
     * @param element The XML element.
     * @return The text or 0.
     */
    static const char* getStatus(XMLElement* element);

    /**
     * Get the type of a 'show' element as enumeration.
     * @param text The text to check.
     * @return Show type as enumeration.
     */
    static inline Show showType(const char* text)
	{ return (Show)lookup(text,s_show,ShowNone); }

    /**
     * Get the text from a show type.
     * @param show The type to get text for.
     * @return The associated text or 0.
     */
    static inline const char* showText(Show show)
	{ return lookup(show,s_show,0); }

protected:
    static TokenDict s_show[];           // Show texts

private:
    String m_name;                       // Resource name
    Presence m_presence;                 // Resorce presence
    u_int32_t m_capability;              // Resource capabilities
    Show m_show;                         // Show attribute
    String m_status;                     // Status attribute
};

/**
 * This class holds a resource list.
 * @short A resource list.
 */
class YJINGLE_API JIDResourceList : public Mutex
{
    friend class XMPPUser;
public:
    /**
     * Constructor.
     */
    inline JIDResourceList()
	: Mutex(true)
	{}

    /**
     * Add a resource to the list if a resource with the given name
     *  doesn't exists.
     * @param name The resource name.
     * @return False if the the resource already exists in the list.
     */
    bool add(const String& name);

    /**
     * Add a resource to the list if a resource with the same doesn't already
     *  exists. Destroy the received resource if not added.
     * @param resource The resource to add.
     * @return False if the the resource already exists in the list.
     */
    bool add(JIDResource* resource);

    /**
     * Remove a resource from the list.
     * @param resource The resource to remove.
     * @param del True to delete the resource.
     */
    inline void remove(JIDResource* resource, bool del = true)
	{ m_resources.remove(resource,del); }

    /**
     * Clear the list.
     */
    inline void clear()
	{ m_resources.clear(); }

    /**
     * Get a resource with the given name.
     * @param name The resource name.
     * @return A pointer to the resource or 0.
     */
    JIDResource* get(const String& name);

    /**
     * Get the first resource from the list.
     * @return A pointer to the resource or 0.
     */
    inline JIDResource* getFirst() {
	    ObjList* obj = m_resources.skipNull();
	    return obj ? static_cast<JIDResource*>(obj->get()) : 0;
	}

    /**
     * Get the first resource with audio capability.
     * @param availableOnly True to get only if available.
     * @return A pointer to the resource or 0.
     */
    JIDResource* getAudio(bool availableOnly = true);

private:
    ObjList m_resources;                 // The resources list
};

/**
 * This class holds a remote XMPP user along with his resources and subscribe state.
 * @short An XMPP remote user.
 */
class YJINGLE_API XMPPUser : public RefObject, public Mutex
{
    friend class XMPPUserRoster;
public:
    enum Subscription {
	None = 0,
	To   = 1,
	From = 2,
	Both = 3,
    };

    /**
     * Create a remote user.
     * @param local The local (owner) user peer.
     * @param node The node (username) of the remote peer.
     * @param domain The domain of the remote peer.
     * @param sub The subscription state.
     * @param subTo True to force a subscribe request to remote peer if not subscribed.
     * @param sendProbe True to probe the new user.
     */
    XMPPUser(XMPPUserRoster* local, const char* node, const char* domain,
	Subscription sub, bool subTo = true, bool sendProbe = true);

    /**
     * Destructor.
     * Send unavailable if not already done.
     */
    virtual ~XMPPUser();

    /**
     * Get the jid of this user.
     * @return The jid of this user.
     */
    const JabberID& jid() const
	{ return m_jid; }

    /**
     * Get the roster this user belongs to.
     * @return Pointer to the roster this user belongs to.
     */
    inline XMPPUserRoster* local() const
	{ return m_local; }

    /**
     * Add a local resource to the list.
     * Send presence if the remote peer is subscribed to the local one.
     * This method is thread safe.
     * @param resource The resource to add.
     * @return False if the the resource already exists in the list.
     */
    bool addLocalRes(JIDResource* resource);

    /**
     * Remove a local resource from the list.
     * Send unavailable if the remote peer is subscribed to the local one.
     * This method is thread safe.
     * @param resource The resource to remove.
     */
    void removeLocalRes(JIDResource* resource);

    /**
     * Remove all local resources.
     * Send unavailable if the remote peer is subscribed to the local one.
     * This method is thread safe.
     */
    void clearLocalRes();

    /**
     * Get the first remote resource with audio capability.
     * @param local True to request a local resource, false for a remote one.
     * @param availableOnly True to get only if available.
     * @return A pointer to the resource or 0.
     */
    inline JIDResource* getAudio(bool local, bool availableOnly = true) {
	    return local ? m_localRes.getAudio(availableOnly) :
		m_remoteRes.getAudio(availableOnly);
	}

    /**
     * Check if the local user is subscribed to the remote one.
     * @return True if the local user is subscribed to the remote one.
     */
    inline bool subscribedTo() const
	{ return (m_subscription & To) != 0; }

    /**
     * Check if the remote user is subscribed to the local one.
     * @return True if the remote user is subscribed to the local one.
     */
    inline bool subscribedFrom() const
	{ return (m_subscription & From) != 0; }

    /**
     * Process received error elements.
     * This method is thread safe.
     * @param event The event with the element.
     */
    void processError(JBEvent* event);

    /**
     * Process received probe from remote peer.
     * This method is thread safe.
     * @param event The event with the element.
     * @param resName The probed resource if any.
     */
    void processProbe(JBEvent* event, const String* resName = 0);

    /**
     * Process received presence from remote peer.
     * This method is thread safe.
     * @param event The event with the element.
     * @param available The availability of the user.
     * @param from The sender's jid.
     * @return False if remote user has no more resources available.
     */
    bool processPresence(JBEvent* event, bool available, const JabberID& from);

    /**
     * Process received subscription from remote peer.
     * This method is thread safe.
     * @param event The event with the element.
     * @param type The subscription type: subscribe/unsubscribe/subscribed/unsubscribed.
     */
    void processSubscribe(JBEvent* event, JBPresence::Presence type);

    /**
     * Probe the remote user.
     * This method is thread safe.
     * @param stream Optional stream to use to send the request.
     * @param time Probe time.
     * @return True if send succeedded.
     */
    bool probe(JBComponentStream* stream, u_int64_t time = Time::msecNow());

    /**
     * Send subscription to remote peer.
     * This method is thread safe.
     * @param type The subscription type: subscribe/unsubscribe/subscribed/unsubscribed.
     * @param stream Optional stream to use to send the data.
     * @return True if send succeedded.
     */
    bool sendSubscribe(JBPresence::Presence type, JBComponentStream* stream);

    /**
     * Send unavailable to remote peer.
     * This method is thread safe.
     * @param stream Optional stream to use to send the data.
     * @return True if send succeedded.
     */
    bool sendUnavailable(JBComponentStream* stream);

    /**
     * Send presence to remote peer.
     * This method is thread safe.
     * @param resource The resource to send from.
     * @param stream Optional stream to use to send the data.
     * @param force True to send even if we've already sent presence from this resource.
     * @return True if send succeedded.
     */
    bool sendPresence(JIDResource* resource, JBComponentStream* stream = 0, bool force = false);

    /**
     * Check if this user sent us any presence data for a given interval.
     * This method is thread safe.
     * @param time Current time.
     * @return True if the user timed out.
     */
    bool timeout(u_int64_t time);

    /**
     * Notify the state of a resource.
     * This method is thread safe.
     * @param remote True for a remote resource: notify the presence engine.
     *  False for a local resource: send presence to remote user.
     * @param name Resource name.
     * @param stream Optional stream to use to send the data if remote is false.
     * @param force True to send even if we've already sent presence from this resource.
     */
    void notifyResource(bool remote, const String& name,
	JBComponentStream* stream = 0, bool force = false);

    /**
     * Notify the state of all resources.
     * This method is thread safe.
     * @param remote True for remote resources: notify the presence engine.
     *  False for local resources: send presence to remote user.
     * @param stream Optional stream to use to send the data if remote is false.
     * @param force True to send even if we've already sent presence from a resource.
     */
    void notifyResources(bool remote, JBComponentStream* stream = 0, bool force = false);

    /**
     * Get the string associated with a subscription enumeration value.
     * @param value The subscription enumeration to get string for.
     * @return Pointer to the string associated with the given subscription enumeration or 0 if none.
     */
    static inline const char* subscribeText(int value)
	{ return lookup(value,s_subscription); }

    /**
     * Get the subscription enumeration value associated with the given string.
     * @param value The subscription string.
     * @return the subscription as enumeration.
     */
    static inline int subscribeType(const char* value)
	{ return lookup(value,s_subscription,None); }

protected:
    /**
     * Update subscription state.
     * @param from True for subscription from remote user. False for subscription to the remote user.
     * @param value True if subscribed. False is unsubscribed.
     * @param stream Optional stream to use to send presence if subscription from remote user changed to true.
     */
    void updateSubscription(bool from, bool value, JBComponentStream* stream);

    /**
     * Update user timeout data.
     * @param from True if the update is made on incoming data. False if timeout is made on outgoing probe.
     * @param time Current time.
     */
    void updateTimeout(bool from, u_int64_t time = Time::msecNow());

    /**
     * Keep the association between subscription enumeration and strings.
     */
    static TokenDict s_subscription[];

private:
    XMPPUserRoster* m_local;             // Local user
    JabberID m_jid;                      // User's JID
    u_int8_t m_subscription;             // Subscription state
    JIDResourceList m_localRes;          // Local user's resources
    JIDResourceList m_remoteRes;         // Remote user's resources
    u_int64_t m_nextProbe;               // Time to probe
    u_int64_t m_expire;                  // Expire time
};

/**
 * This class holds the roster for a local user.
 * @short The roster of a local user.
 */
class YJINGLE_API XMPPUserRoster : public RefObject, public Mutex
{
    friend class JBPresence;
    friend class XMPPUser;
public:
    /**
     * Destructor.
     * Remove this roster from engine's queue.
     */
    virtual ~XMPPUserRoster();

    /**
     * Get the local user's jid.
     * @return The local user's jid.
     */
    const JabberID& jid() const
	{ return m_jid; }

    /**
     * Get the presence engine this user belongs to.
     * @return Pointer to the presence engine this user belongs to.
     */
    JBPresence* engine()
	{ return m_engine; }

    /**
     * Get a remote user.
     * This method is thread safe.
     * @param jid User's jid.
     * @param add True to add if not found.
     * @param added Optional flag to set if added a new user.
     * @return Referenced pointer to the user or 0.
     */
    XMPPUser* getUser(const JabberID& jid, bool add = false, bool* added = 0);

    /**
     * Remove a remote user.
     * This method is thread safe.
     * @param remote The user to remove.
     * @return False if no more users.
     */
    bool removeUser(const JabberID& remote);

    /**
     * Clear remote user list.
     */
    inline void cleanup() {
	    Lock lock(this);
	    m_remote.clear();
	}

    /**
     * Check timeout.
     * This method is thread safe.
     * @param time Current time.
     * @return True to remove the roster.
     */
    bool timeout(u_int64_t time);

protected:
    /**
     * Constructor.
     * @param engine Pointer to the presence engine this user belongs to.
     * @param node User's name.
     * @param domain User's domain.
     */
    XMPPUserRoster(JBPresence* engine, const char* node, const char* domain);

private:
    inline void addUser(XMPPUser* u) {
	    Lock lock(this);
	    m_remote.append(u);
	}
    void removeUser(XMPPUser* u) {
	    Lock lock(this);
	    m_remote.remove(u,false);
	}

    JabberID m_jid;                      // User's bare JID
    ObjList m_remote;                    // Remote users
    JBPresence* m_engine;                // Presence engine
};

};

#endif /* __YATEJABBER_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
