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

class JBEvent;                           // A Jabber event
class JBStream;                          // A Jabber stream
class JBComponentStream;                 // A Jabber Component stream
class JBThread;                          // Base class for private threads
class JBThreadList;                      // A list of threads
class JBEngine;                          // A Jabber engine
class JBService;                         // A Jabber service
class JBPresence;                        // A Jabber presence service
class JIDResource;                       // A JID resource
class JIDResourceList;                   // Resource list
class XMPPUser;                          // An XMPP user (JID, resources, capabilities etc)
class XMPPUserRoster;                    // An XMPP user's roster

/**
 * This class holds a Jabber stream event. Stream events are raised by streams
 *  and sent by the engine to the proper service
 * @short A Jabber stream event
 */
class YJINGLE_API JBEvent : public RefObject
{
    friend class JBStream;
public:
    /**
     * Event type enumeration
     */
    enum Type {
	// Stream events
	Terminated              = 1,     // Stream terminated. Try to connect
	Destroy                 = 2,     // Stream is destroying
	Running                 = 3,     // Stream is running (stable state: can send/recv stanzas)
	// Result events
	WriteFail               = 10,    // Write failed. m_element is the element, m_id is the id set by the sender
	// Stanza events: m_element is always valid
	Presence                = 20,    // m_element is a 'presence' stanza
	Message                 = 30,    // m_element is a 'message' stanza
	Iq                      = 50,    // m_element is an 'iq' set/get, m_child is it's first child
	IqError                 = 51,    // m_element is an 'iq' error, m_child is the 'error' child if any
	IqResult                = 52,    // m_element is an 'iq' result, m_child is it's first child
	// Disco: m_child is a 'query' element qualified by DiscoInfo/DiscoItems namespaces
	// IqDisco error: m_child is the 'error' child, m_element has a 'query' child
	IqDiscoInfoGet          = 60,
	IqDiscoInfoSet          = 61,
	IqDiscoInfoRes          = 62,
	IqDiscoInfoErr          = 63,
	IqDiscoItemsGet         = 64,
	IqDiscoItemsSet         = 65,
	IqDiscoItemsRes         = 66,
	IqDiscoItemsErr         = 67,
	// Command: m_child is a 'command' element qualified by Command namespace
	// IqCommandError: m_child is the 'error' child, m_element has a 'command' child
	IqCommandGet            = 70,
	IqCommandSet            = 71,
	IqCommandRes            = 72,
	IqCommandErr            = 73,
	// Jingle: m_child is a 'jingle' element qualified by Jingle namespace
	// IqJingleError: m_child is the 'error' child, m_element has a 'jingle' child
	IqJingleGet             = 80,
	IqJingleSet             = 81,
	IqJingleRes             = 82,
	IqJingleErr             = 83,
	// Invalid
	Unhandled               = 200,   // m_element is an unhandled element
	Invalid                 = 500,   // m_element is 0
    };

    /**
     * Constructor. Constructs an event from a stream
     * @param type Type of this event
     * @param stream The stream that generated the event
     * @param element Element that generated the event
     * @param child Optional type depending element's child
     */
    JBEvent(Type type, JBStream* stream, XMLElement* element, XMLElement* child = 0);

    /**
     * Constructor. Constructs a WriteSuccess/WriteFail event from a stream
     * @param type Type of this event
     * @param stream The stream that generated the event
     * @param element Element that generated the event
     * @param senderID Sender's id
     */
    JBEvent(Type type, JBStream* stream, XMLElement* element, const String& senderID);

    /**
     * Destructor. Delete the XML element if valid
     */
    virtual ~JBEvent();

    /**
     * Get the event type
     * @return The type of this event as enumeration
     */
    inline int type() const
	{ return m_type; }

    /**
     * Get the event name
     * @return The name of this event
     */
    inline const char* name() const
	{ return lookup(type()); }

    /**
     * Get the element's 'type' attribute if any
     * @return The  element's 'type' attribute
     */
    inline const String& stanzaType() const
	{ return m_stanzaType; }

    /**
     * Get the 'from' attribute of a received stanza
     * @return The 'from' attribute
     */
    inline const String& from() const
	{ return m_from; }

    /**
     * Get the 'to' attribute of a received stanza
     * @return The 'to' attribute
     */
    inline const String& to() const
	{ return m_to; }

    /**
     * Get the sender's id for Write... events or the 'id' attribute if the
     *  event carries a received stanza
     * @return The event id
     */
    inline const String& id() const
	{ return m_id; }

    /**
     * The stanza's text or termination reason for Terminated/Destroy events
     * @return The event's text
     */
    inline const String& text() const
	{ return m_text; }

    /**
     * Get the stream that generated this event
     * @return The stream that generated this event
     */
    inline JBStream* stream() const
	{ return m_stream; }

    /**
     * Get the underlying XMLElement
     * @return XMLElement pointer or 0
     */
    inline XMLElement* element() const
	{ return m_element; }

    /**
     * Get the first child of the underlying element if any
     * @return XMLElement pointer or 0
     */
    inline XMLElement* child() const
	{ return m_child; }

    /**
     * Get the underlying XMLElement. Release the ownership.
     * The caller is responsable of returned pointer
     * @return XMLElement pointer or 0
     */
    inline XMLElement* releaseXML() {
	    XMLElement* tmp = m_element;
	    m_element = 0;
	    return tmp;
	}

    /**
     * Release the link with the stream to let the stream continue with events
     */
    void releaseStream();

    /**
     * Get the name of an event type
     * @return The name an event type
     */
    inline static const char* lookup(int type)
	{ return TelEngine::lookup(type,s_type); }

private:
    static TokenDict s_type[];           // Event names
    JBEvent() {}                         // Don't use it!
    bool init(JBStream* stream, XMLElement* element);

    Type m_type;                         // Type of this event
    JBStream* m_stream;                  // The stream that generated this event
    bool m_link;                         // Stream link state
    XMLElement* m_element;               // Received XML element, if any
    XMLElement* m_child;                 // The first child element for 'iq' elements
    String m_stanzaType;                 // Stanza's 'type' attribute
    String m_from;                       // Stanza's 'from' attribute
    String m_to;                         // Stanza's 'to' attribute
    String m_id;                         // Sender's id for Write... events
                                         // 'id' attribute if the received stanza has one
    String m_text;                       // The stanza's text or termination reason for
                                         //  Terminated/Destroy events
};


/**
 * A socket used used to transport data for a Jabber stream
 * @short A Jabber streams's socket
 */
class YJINGLE_API JBSocket
{
    friend class JBStream;
public:
    /**
     * Constructor
     * @param engine The Jabber engine
     * @param stream The stream owning this socket
     */
    inline JBSocket(JBEngine* engine, JBStream* stream)
	: m_engine(engine), m_stream(stream), m_socket(0),
	  m_streamMutex(true), m_receiveMutex(true)
	{}

    /**
     * Destructor. Close the socket
     */
    inline ~JBSocket()
	{ terminate(); }

    /**
     * Check if the socket is valid
     * @return True if the socket is valid.
     */
    inline bool valid() const
	{ return m_socket && m_socket->valid(); }

    /**
     * Connect the socket
     * @param addr Remote address to connect to
     * @param terminated True if false is returned and the socket was terminated
     *  while connecting
     * @return False on failure
     */
    bool connect(const SocketAddr& addr, bool& terminated);

    /**
     * Terminate the socket
     */
    void terminate();

    /**
     * Read data from socket
     * @param buffer Destination buffer
     * @param len The number of bytes to read. On exit contains the number of
     *  bytes actually read
     * @return False on socket error
     */
    bool recv(char* buffer, unsigned int& len);

    /**
     * Write data to socket
     * @param buffer Source buffer
     * @param len The number of bytes to send
     * @return False on socket error
     */
    bool send(const char* buffer, unsigned int& len);

private:
    JBEngine* m_engine;                  // The Jabber engine
    JBStream* m_stream;                  // Stream owning this socket
    Socket* m_socket;                    // The socket
    Mutex m_streamMutex;                 // Lock stream
    Mutex m_receiveMutex;                // Lock receive
};

/**
 * Base class for all Jabber streams. Basic stream data processing: send/receive
 *  XML elements, keep stream state, generate events
 * @short A Jabber stream
 */
class YJINGLE_API JBStream : public RefObject
{
    friend class JBEngine;
    friend class JBEvent;
public:
    /**
     * Stream state enumeration.
     */
    enum State {
	Idle        = 0,                 // Stream is waiting to be connected or destroyed
	Connecting  = 1,                 // Stream is waiting for the socket to connect
	Started     = 2,                 // Stream start tag sent
	Securing    = 3,                 // Stream is currently negotiating the TLS
	Auth        = 4,                 // Stream is currently authenticating
	Running     = 5,                 // Established. Allow XML stanzas to pass over the stream
	Destroy     = 6,                 // Stream is destroying. No more traffic allowed
    };

    /**
     * Values returned by send() methods.
     */
    enum Error {
	ErrorNone = 0,                   // No error (stanza enqueued/sent)
	ErrorContext,                    // Invalid stream context (state) or parameters
	ErrorPending,                    // The operation is pending in the stream's queue
	ErrorNoSocket,                   // Unrecoverable socket error. The stream will be terminated
    };

    /**
     * Constructor
     * @param engine The engine that owns this stream
     * @param localJid Local party's JID
     * @param remoteJid Remote party's JID
     * @param password Password used for authentication
     * @param address The remote address to connect to
     * @param autoRestart True to auto restart the stream
     * @param maxRestart The maximum restart attempts allowed
     * @param incRestartInterval The interval to increase the restart counter
     * @param outgoing Stream direction
     * @param type Stream type. Set to -1 to use the engine's protocol type
     */
    JBStream(JBEngine* engine, const JabberID& localJid, const JabberID& remoteJid,
	const String& password, const SocketAddr& address,
	bool autoRestart, unsigned int maxRestart,
	u_int64_t incRestartInterval, bool outgoing, int type = -1);

    /**
     * Destructor.
     * Gracefully close the stream and the socket
     */
    virtual ~JBStream();

    /**
     * Get the type of this stream. See the protocol enumeration of the engine
     * @return The type of this stream
     */
    inline int type() const
	{ return m_type; }

    /**
     * Get the stream state
     * @return The stream state as enumeration.
     */
    inline State state() const
	{ return m_state; }

    /**
     * Get the stream direction
     * @return True if the stream is an outgoing one
     */
    inline bool outgoing() const
	{ return m_outgoing; }

    /**
     * Get the stream id
     * @return The stream id
     */
    inline const String& id() const
	{ return m_id; }

    /**
     * Get the stream's owner
     * @return Pointer to the engine owning this stream
     */
    inline JBEngine* engine() const
	{ return m_engine; }

    /**
     * Get the JID of the local side of this stream
     * @return The JID of the local side of this stream
     */
    inline const JabberID& local() const
	{ return m_local; }

    /**
     * Get the JID of the remote side of this stream
     * @return The JID of the remote side of this stream
     */
    inline const JabberID& remote() const
	{ return m_remote; }

    /**
     * Get the remote perr's address
     * @return The remote perr's address
     */
    inline const SocketAddr& addr() const
	{ return m_address; }

    /**
     * Connect the stream. Send stream start tag on success
     * This method is thread safe
     */
    void connect();

    /**
     * Read data from socket and pass it to the parser. Terminate stream on
     *  socket or parser error.
     * This method is thread safe
     * @return True if data was received
     */
    bool receive();

    /**
     * Send a stanza.
     * This method is thread safe
     * @param stanza Element to send
     * @param senderId Optional sender's id. Used for notification events
     * @return The result of posting the stanza
     */
    Error sendStanza(XMLElement* stanza, const char* senderId = 0);

    /**
     * Stream state and data processor. Increase restart counter.
     * Restart stream if idle and auto restart.
     * Extract an element from parser and construct an event.
     * This method is thread safe
     * @param time Current time
     * @return JBEvent pointer or 0
     */
    JBEvent* getEvent(u_int64_t time);

    /**
     * Terminate stream. Send stream end tag or error.
     * Remove pending stanzas without id. Deref stream if destroying.
     * This method is thread safe
     * @param destroy True to destroy. False to terminate
     * @param recvStanza Received stanza, if any
     * @param error Termination reason. Set it to NoError to send stream end tag
     * @param reason Optional text to be added to the error stanza
     * @param send True to send the error element (ignored if error is NoError)
     * @param final True if called from destructor
     */
    void terminate(bool destroy, XMLElement* recvStanza, XMPPError::Type error, const char* reason,
	bool send, bool final = false);

    /**
     * Remove pending stanzas with a given id.
     * This method is thread safe
     * @param id The id of stanzas to remove
     * @param notify True to raise an event for each removed stanza
     */
    inline void removePending(const String& id, bool notify = false) {
	    Lock lock(m_socket.m_streamMutex);
	    removePending(notify,&id,false);
	}

    /**
     * Cleanup the stream before destroying
     * This method is thread safe
     */
    inline void cleanup() {
	    Lock lock(m_socket.m_streamMutex);
	    m_events.clear();
	    TelEngine::destruct(m_terminateEvent);
	    TelEngine::destruct(m_startEvent);
	}

    /**
     * Get the name of a stream state
     * @param state The requested state number
     * @return The name of the requested state
     */
    static const char* lookupState(int state);

protected:
    /**
     * Default constructor
     */
    inline JBStream()
	: m_socket(0,0)
	{}

    /**
     * Close the stream. Release memory
     */
    virtual void destroyed();

    /**
     * Check the 'to' attribute of a received element
     * @param xml The received element
     * @return False to reject it. If the stream is not in Running state,
     *  it will be terminated
     */
    virtual bool checkDestination(XMLElement* xml)
	{ return true; }

    /**
     * Get the starting stream element to be sent after stream connected
     * @return XMLElement pointer
     */
    virtual XMLElement* getStreamStart() = 0;

    /**
     * Process a received stanza in Running state
     * @param xml Valid XMLElement pointer
     */
    virtual void processRunning(XMLElement* xml);

    /**
     * Process a received element in Auth state. Descendants MUST consume the data
     * @param xml Valid XMLElement pointer
     */
    virtual void processAuth(XMLElement* xml)
	{ dropXML(xml,false); }

    /**
     * Process a received element in Securing state. Descendants MUST consume the data
     * @param xml Valid XMLElement pointer
     */
    virtual void processSecuring(XMLElement* xml)
	{ dropXML(xml,false); }

    /**
     * Process a received element in Started state. Descendants MUST consume the data
     * @param xml Valid XMLElement pointer
     */
    virtual void processStarted(XMLElement* xml)
	{ dropXML(xml,false); }

    /**
     * Create an iq event from a received iq stanza
     * @param xml Received element
     * @param iqType The iq type
     * @param error Error type if 0 is returned
     * @return JBEvent pointer or 0
     */
    JBEvent* getIqEvent(XMLElement* xml, int iqType, XMPPError::Type& error);

    /**
     * Send stream XML elements through the socket
     * @param e The element to send
     * @param newState The new stream state on success
     * @param streamEnd Stream element is a stream ending one: end tag or stream error
     * @return False if send failed (stream termination was initiated)
     */
    bool sendStreamXML(XMLElement* e, State newState, bool streamEnd);

    /**
     * Terminate stream on receiving invalid elements
     * @param xml Received element
     * @param error Termination reason
     * @param reason Optional text to be added to the error stanza
     */
    void invalidStreamXML(XMLElement* xml, XMPPError::Type error, const char* reason);

    /**
     * Drop an unexpected or unhandled element
     * @param xml Received element
     * @param unexpected True if unexpected
     */
    void dropXML(XMLElement* xml, bool unexpected = true);

    /**
     * Change stream's state
     * @param newState the new stream state
     */
    void changeState(State newState);

    /**
     * The password used for authentication
     */
    String m_password;

private:
    // Event termination notification
    // @param event The notifier. Ignored if it's not m_lastEvent
    void eventTerminated(const JBEvent* event);
    // Try to send the first element in pending outgoing stanzas list
    // If ErrorNoSocket is returned, stream termination was initiated
    Error sendPending();
    // Send XML elements through the socket
    // @param e The element to send (used to print it to output)
    // @param stream True if this is a stream element. If true, try to send any partial data
    //  remaining from the last stanza transmission
    // @param streamEnd Stream element is a stream ending one: end tag or stream error
    // @return The result of the transmission. For stream elements, stream termination was initiated
    //  if returned value is not ErrorNone
    Error sendXML(XMLElement* e, const char* buffer, unsigned int& len,
	bool stream = false, bool streamEnd = false);
    // Remove pending elements with id if id is not 0
    // Remove all elements without id if id is 0
    // Set force to true to remove the first element even if partially sent
    void removePending(bool notify, const String* id, bool force);

    int m_type;                          // Stream type
    State m_state;                       // Stream state
    bool m_outgoing;                     // Stream direction
    bool m_autoRestart;                  // Auto restart flag
    unsigned int m_restart;              // Remaining restart attempts
    unsigned int m_restartMax;           // Max restart attempts
    u_int64_t m_timeToFillRestart;       // Next time to increase the restart counter
    u_int64_t m_fillRestartInterval;     // Interval to increase the restart counter
    String m_id;                         // Stream id
    JabberID m_local;                    // Local peer's jid
    JabberID m_remote;                   // Remote peer's jid
    JBEngine* m_engine;                  // The owner of this stream
    JBSocket m_socket;                   // The socket used by this stream
    SocketAddr m_address;                // Remote peer's address
    XMLParser m_parser;                  // XML parser
    ObjList m_outXML;                    // Outgoing XML elements
    ObjList m_events;                    // Event queue
    JBEvent* m_lastEvent;                // Last generated event
    JBEvent* m_terminateEvent;           // Destroy/Terminate event
    JBEvent* m_startEvent;               // Running event
};

/**
 * This class holds a Jabber Component stream (implements the Jabber Component Protocol).
 * @short A Jabber Component stream
 */
class YJINGLE_API JBComponentStream : public JBStream
{
    friend class JBEngine;
public:
    /**
     * Destructor
     */
    virtual ~JBComponentStream()
	{}

protected:
    /**
     * Constructor
     * @param engine The engine that owns this stream
     * @param localJid Local party's JID
     * @param remoteJid Remote party's JID
     * @param password Password used for authentication
     * @param address The remote address to connect to
     * @param autoRestart True to auto restart the stream
     * @param maxRestart The maximum restart attempts allowed
     * @param incRestartInterval The interval to increase the restart counter
     * @param outgoing Stream direction
     */
    JBComponentStream(JBEngine* engine,
	const JabberID& localJid, const JabberID& remoteJid,
	const String& password, const SocketAddr& address,
	bool autoRestart, unsigned int maxRestart,
	u_int64_t incRestartInterval, bool outgoing = true);

    /**
     * Get the starting stream element to be sent after stream connected
     * @return XMLElement pointer
     */
    virtual XMLElement* getStreamStart();

    /**
     * Process a received element in Auth state
     * @param xml Valid XMLElement pointer
     */
    virtual void processAuth(XMLElement* xml);

    /**
     * Process a received element in Started state
     * @param xml Valid XMLElement pointer
     */
    virtual void processStarted(XMLElement* xml);

private:
    // Default constructor is private to avoid unwanted use
    JBComponentStream() {}

    bool m_shaAuth;                      // Use SHA1/MD5 digest authentication
};

/**
 * This class holds encapsulates a private library thread
 * @short A Jabber thread that can be added to a list of threads
 */
class YJINGLE_API JBThread : public GenObject
{
public:
    /**
     * Thread type enumeration. Used to do a specific client processing
     */
    enum Type {
	StreamConnect,                   // Asynchronously connect a stream's socket
	EngineReceive,                   // Read all streams sockets
	EngineProcess,                   // Get events from sockets and send them to
	                                 //  registered services
	Presence,                        // Presence service processor
	Jingle,                          // Jingle service processor
	Message                          // Message service processor
    };

    /**
     * Destructor. Remove itself from the owner's list
     */
    virtual ~JBThread();

    /**
     * Cancel (terminate) this thread
     * @param hard Kill the thread the hard way rather than just setting
     *  an exit check marker
     */
    virtual void cancelThread(bool hard = false) = 0;

    /**
     * Create and start a private thread
     * @param type Thread type
     * @param list The list owning this thread
     * @param client The client to process
     * @param sleep Time to sleep if there is nothing to do
     * @param prio Thread priority
     * @return False if failed to start the requested thread
     */
    static bool start(Type type, JBThreadList* list, void* client, int sleep, int prio);

protected:
    /**
     * Constructor. Append itself to the owner's list
     * @param type Thread type
     * @param owner The list owning this thread
     * @param client The client to process
     * @param sleep Time to sleep if there is nothing to do
     */
    JBThread(Type type, JBThreadList* owner, void* client, int sleep = 2);

    /**
     * Process the client
     */
    void runClient();

private:
    Type m_type;                         // Thread type
    JBThreadList* m_owner;               // List owning this thread
    void* m_client;                      // The client to process
    int m_sleep;                         // Time to sleep if there is nothing to do
};


/**
 * This class holds a list of private threads for an object that wants to terminate them on destroy
 * @short A list of private threads
 */
class YJINGLE_API JBThreadList
{
    friend class JBThread;
public:
    /**
     * Cancel all threads
     * This method is thread safe
     * @param wait True to wait for the threads to terminate
     * @param hard Kill the threads the hard way rather than just setting an exit check marker
     */
    void cancelThreads(bool wait = true, bool hard = false);

protected:
    /**
     * Constructor. Set the auto delete flag of the list to false
     */
    JBThreadList()
	: m_mutex(true), m_cancelling(false)
	{ m_threads.setDelete(false); }

private:
    Mutex m_mutex;                       // Lock list operations
    ObjList m_threads;                   // Private threads list
    bool m_cancelling;                   // Cancelling threads operation in progress
};


/**
 * This class holds a Jabber engine
 * @short A Jabber engine
 */
class YJINGLE_API JBEngine : public DebugEnabler, public Mutex,
	public GenObject, public JBThreadList
{
    friend class JBStream;
public:
    /**
     * Jabber protocol type
     */
    enum Protocol {
	Component = 1,                   // Use Jabber Component protocol
	Client    = 2,                   // Use client streams
    };

    /**
     * Service type enumeration
     */
    enum Service {
	ServiceJingle   = 0,             // Receive Jingle events
	ServiceIq       = 1,             // Receive generic Iq events
	ServiceMessage  = 2,             // Receive Message events
	ServicePresence = 3,             // Receive Presence events
	ServiceCommand  = 4,             // Receive Command events
	ServiceDisco    = 5,             // Receive Disco events
	ServiceStream   = 6,             // Receive stream Terminated or Destroy events
	ServiceWriteFail = 7,            // Receive WriteFail events
	ServiceCount = 8
    };

    /**
     * Constructor
     * @param proto The protocol used by the streams belonging to this engine
     */
    JBEngine(Protocol proto);

    /**
     * Destructor
     */
    virtual ~JBEngine();

    /**
     * Get the Jabber protocol this engine is using
     * @return The Jabber protocol as enumeration
     */
    inline Protocol protocol() const
	{ return m_protocol; }

    /**
     * Check if a sender or receiver of XML elements should print them to output
     * @return True to print XML element to output
     */
    inline bool printXml() const
	{ return m_printXml; }

    /**
     * Get the default component server
     * @return The default component server
     */
    inline const JabberID& componentServer() const
	{ return m_componentDomain; }

    /**
     * Set the alternate domain name
     * @param domain Name of an acceptable alternate domain
     */
    inline void setAlternateDomain(const char* domain = 0)
	{ m_alternateDomain = domain; }

    /**
     * Get the alternate domain name
     * @return the alternate domain name
     */
    inline const JabberID& getAlternateDomain() const
	{ return m_alternateDomain; }

    /**
     * Get the default resource name.
     * @return The default resource name.
     */
    inline const String& defaultResource() const
	{ return m_defaultResource; }

    /**
     * Get the stream list
     * @return The list of streams belonging to this engine
     */
    inline const ObjList& streams() const
	{ return m_streams; }

    /**
     * Cleanup streams. Stop all threads owned by this engine. Release memory
     */
    virtual void destruct();

    /**
     * Initialize the engine's parameters. Start private streams if requested
     * @param params Engine's parameters
     */
    virtual void initialize(const NamedList& params);

    /**
     * Terminate all streams
     */
    void cleanup();

    /**
     * Set the default component server to use. The domain must be in the server list.
     * Choose the first one from the server list if the given one doesn't exists.
     * Do nothing if the protocol is not Component
     * @param domain Domain name of the server
     */
    void setComponentServer(const char* domain);

    /**
     * Get a stream. Create it not found and requested.
     * For the component protocol, the jid parameter may contain the domain to find,
     *  otherwise, the default component will be used.
     * For the client protocol, the jid parameter must contain the full user's
     *  jid (including the resource).
     * This method is thread safe.
     * @param jid Optional jid to use to find or create the stream
     * @param create True to create a stream if don't exist
     * @param pwd Password used to authenticate the user if the client protocol
     *  is used and a new stream is going to be created. Set it to empty string
     *  to create the stream without password
     * @return Referenced JBStream pointer or 0
     */
    JBStream* getStream(const JabberID* jid = 0, bool create = true, const char* pwd = 0);

    /**
     * Try to get a stream if stream parameter is 0
     * @param stream Stream to check
     * @param release Set to true on exit if the caller must deref the stream
     * @param pwd Password used to authenticate the user if the client protocol
     *  is used and a new stream is going to be created. Set it to empty string
     *  to create the stream without password
     * @return True if stream is valid
     */
    bool getStream(JBStream*& stream, bool& release, const char* pwd = 0);

    /**
     * Keep calling receive() for each stream until no data is received or the
     *  thread is terminated
     * @return True if data was received
     */
    bool receive();

    /**
     * Get events from the streams owned by this engine and send them to a service.
     * Delete them if not processed by a service
     * @param time Current time
     * @return True if an event was generated by any stream
     */
    bool process(u_int64_t time);

    /**
     * Check if an outgoing stream exists with the same id and remote peer
     * @param stream The calling stream
     * @return True if found
     */
    bool checkDupId(const JBStream* stream);

    /**
     * Check the 'from' attribute received by a Component stream at startup
     * @param stream The calling stream
     * @param from The from attribute to check
     * @return True if valid
     */
    bool checkComponentFrom(JBComponentStream* stream, const char* from);

    /**
     * Asynchronously call the connect method of the given stream if the stream is idle
     * @param stream The stream to connect
     */
    virtual void connect(JBStream* stream);

    /**
     * Check if this engine is exiting
     * @return True is terminating
     */
    virtual bool exiting() const
	{ return false; }

    /**
     * Append a server info element to the list
     * @param server The object to add
     * @param open True to open the stream, if in component mode
     */
    void appendServer(XMPPServerInfo* server, bool open);

    /**
     * Get the identity of the given server
     * @param destination The destination buffer
     * @param full True to get the full identity
     * @param token The search string. If 0 and the component protocol is used,
     *  the default server will be used
     * @param domain True to find by domain name. False to find by address
     * @return False if server doesn't exists
     */
    bool getServerIdentity(String& destination, bool full, const char* token = 0,
	bool domain = true);

    /**
     * Find server info object
     * @param token The search string. If 0 and the Component protocol is used,
     *  the default component server will be used
     * @param domain True to find by domain name. False to find by address
     * @return XMPPServerInfo pointer or 0 if not found
     */
    XMPPServerInfo* findServerInfo(const char* token, bool domain);

    /**
     * Attach a service to this engine.
     * This method is thread safe
     * @param service The service to attach
     * @param type Service type
     * @param prio Service priority. Set to -1 to use the givent service's priority.
     *  A lower values indicates a service with higher priority
     */
    void attachService(JBService* service, Service type, int prio = -1);

    /**
     * Remove a service from all event handlers of this engine.
     * This method is thread safe
     * @param service The service to detach
     */
    void detachService(JBService* service);

    /**
     * Get the name of a protocol
     * @return The name of the requested protocol or the default value
     */
    inline static const char* lookupProto(int proto, const char* def = 0)
	{ return lookup(proto,s_protoName,def); }

    /**
     * Get the value associated with a protocol name
     * @return The value associated with a protocol name
     */
    inline static int lookupProto(const char* proto, int def = 0)
	{ return lookup(proto,s_protoName,def); }

private:
    // Process a Disco... events
    bool processDisco(JBEvent* event);
    // Process a Command events
    bool processCommand(JBEvent* event);
    // Pass events to services
    bool received(Service service, JBEvent* event);
    static TokenDict s_protoName[];      // Protocol names

    Protocol m_protocol;                 // The protocol to use
    u_int32_t m_restartUpdateInterval;   // Update interval for restart counter of all streams
    u_int32_t m_restartCount;            // The default restart counter value
    bool m_printXml;                     // Print XML data to output
    ObjList m_streams;                   // Streams belonging to this engine
    JIDIdentity* m_identity;             // Engine's identity
    JIDFeatureList m_features;           // Engine's features
    JabberID m_componentDomain;          // Default server domain name
    String m_componentAddr;              // Default server address
    int m_componentCheckFrom;            // The behaviour when checking the 'from' attribute for a component stream
                                         // 0: no check 1: local identity 2: remote identity
    JabberID m_alternateDomain;          // Alternate acceptable domain
    String m_defaultResource;            // Default name for missing resources
    Mutex m_serverMutex;                 // Lock server info list
    ObjList m_server;                    // Server info list
    Mutex m_servicesMutex;               // Lock service list
    ObjList m_services[ServiceCount];    // Services list
    bool m_initialized;                  // True if already initialized
};


/**
 * This class is the base class for a Jabber service who wants
 *  to get specific protocol data from the Jabber engine
 * @short A Jabber service
 */
class YJINGLE_API JBService : public DebugEnabler, public Mutex, public GenObject
{
public:
    /**
     * Constructor
     * @param engine The Jabber engine
     * @param name This service's name
     * @param params Service's parameters
     * @param prio The priority of this service
     */
    JBService(JBEngine* engine, const char* name, const NamedList* params, int prio);

    /**
     * Destructor. Remove from engine
     */
    virtual ~JBService();

    /**
     * Get the Jabber engine
     * @return The Jabber engine
     */
    inline JBEngine* engine()
	{ return m_engine; }

    /**
     * Get the Jabber engine
     * @return The Jabber engine
     */
    inline int priority() const
	{ return m_priority; }

    /**
     * Accept an event from the engine. If accepted, the event is enqueued
     *  and the stream that generated the event is notified on event
     *  terminated to allow it to process other data.
     * This method is thread safe
     * @param event The event to accept
     * @return False if not accepted, let the engine try another service
     */
    bool received(JBEvent* event);

    /**
     * Initialize the service
     * @param params Service's parameters
     */
    virtual void initialize(const NamedList& params)
	{}

    /**
     * Remove from engine. Release memory
     */
    virtual void destruct();

protected:
    /**
     * Accept an event from the engine
     * @param event The event to accept
     * @param processed Set to true on exit to signal that the event was
     *  already processed
     * @param insert Set to true if accepted to insert on top of the event queue
     * @return False if not accepted, let the engine try another service
     */
    virtual bool accept(JBEvent* event, bool& processed, bool& insert);

    /**
     * Get an event from queue
     * @return JBEvent pointer or 0 if queue is empty
     */
    JBEvent* deque();

    /**
     * True if already initialized
     */
    bool m_initialized;

private:
    inline JBService() {}                // Don't use it !
    JBEngine* m_engine;                  // The Jabber Component engine
    int m_priority;                      // Service priority
    ObjList m_events;                    // Events received from engine
};


/**
 * This class is a message receiver service for the Jabber engine
 * @short A Jabber message service
 */
class YJINGLE_API JBMessage : public JBService, public JBThreadList
{
public:
    /**
     * Constructor. Constructs a Jabber message service
     * @param engine The Jabber engine
     * @param params Service's parameters
     * @param prio The priority of this service
     */
    inline JBMessage(JBEngine* engine, const NamedList* params, int prio = 0)
	: JBService(engine,"jbmsgrecv",params,prio), m_syncProcess(true)
	{}

    /**
     * Destructor. Cancel private thread(s)
     */
    virtual ~JBMessage()
	{ cancelThreads(); }

    /**
     * Initialize the service
     * @param params Service's parameters
     */
    virtual void initialize(const NamedList& params);

    /**
     * Get a message from queue
     * @return JBEvent pointer or 0 if no messages
     */
    inline JBEvent* getMessage()
	{ return deque(); }

    /**
     * Message processor. The derived classes must override this method
     *  to process received messages
     * @param event The event to process
     */
    virtual void processMessage(JBEvent* event);

protected:
    /**
     * Accept an event from the engine and process it if configured to do that
     * @param event The event to accept
     * @param processed Set to true on exit to signal that the event was already processed
     * @param insert Set to true if accepted to insert on top of the event queue
     * @return False if not accepted, let the engine try another service
     */
    virtual bool accept(JBEvent* event, bool& processed, bool& insert);

private:
    bool m_syncProcess;                  // Process messages on accept
};


/**
 * This class is a presence service for Jabber engine. Handle presence stanzas and
 *  iq query info or items with destination containing a node and a valid domain
 * @short A Jabber presence service
 */
class YJINGLE_API JBPresence : public JBService, public JBThreadList
{
    friend class XMPPUserRoster;
public:
    /**
     * Presence type enumeration
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
     * Constructor. Constructs a Jabber Component presence service
     * @param engine The Jabber engine
     * @param params Service's parameters
     * @param prio The priority of this service
     */
    JBPresence(JBEngine* engine, const NamedList* params, int prio = 0);

    /**
     * Destructor
     */
    virtual ~JBPresence();

    /**
     * Get the auto subscribe parameter
     * @return The auto subscribe parameter
     */
    inline int autoSubscribe() const
	{ return m_autoSubscribe; }

    /**
     * Check if the unavailable resources must be deleted
     * @return The delete unavailable parameter
     */
    inline bool delUnavailable() const
	{ return m_delUnavailable; }

    /**
     * Check if this server should add new users when receiving subscribe stanzas
     * @return True if should add a new user when receiving subscribe stanzas
     */
    inline bool addOnSubscribe() const
	{ return m_addOnSubscribe; }

    /**
     * Check if this server should add new users when receiving presence probes
     * @return True if should add a new user when receiving presence probes
     */
    inline bool addOnProbe() const
	{ return m_addOnProbe; }

    /**
     * Check if this server should add new users when receiving presence
     * @return True if should add a new user when receiving presence stanzas
     */
    inline bool addOnPresence() const
	{ return m_addOnPresence; }

    /**
     * Check if this server should add new users when receiving presence, probe or subscribe
     * @return True if should add a new user when receiving presence, probe or subscribe
     */
    inline bool autoRoster() const
	{ return m_autoRoster; }

    /**
     * Get the probe interval. Time to send a probe if nothing was received from that user
     * @return The probe interval
     */
    inline u_int32_t probeInterval()
	{ return m_probeInterval; }

    /**
     * Get the expire after probe interval
     * @return The expire after probe interval
     */
    inline u_int32_t expireInterval()
	{ return m_expireInterval; }

    /**
     * Initialize the presence service
     * @param params Service's parameters
     */
    virtual void initialize(const NamedList& params);

    /**
     * Process an event from the receiving list
     * This method is thread safe
     * @return False if the list is empty
     */
    virtual bool process();

    /**
     * Check presence timeout
     * This method is thread safe
     * @param time Current time
     */
    virtual void checkTimeout(u_int64_t time);

    /**
     * Process disco info elements
     * @param event The event with the element
     * @param local The local (destination) user
     * @param remote The remote (source) user
     */
    virtual void processDisco(JBEvent* event, const JabberID& local,
	const JabberID& remote);

    /**
     * Process a presence error element
     * @param event The event with the element
     * @param local The local (destination) user
     * @param remote The remote (source) user
     */
    virtual void processError(JBEvent* event, const JabberID& local,
	const JabberID& remote);

    /**
     * Process a presence probe element
     * @param event The event with the element
     * @param local The local (destination) user
     * @param remote The remote (source) user
     */
    virtual void processProbe(JBEvent* event, const JabberID& local,
	const JabberID& remote);

    /**
     * Process a presence subscribe element
     * @param event The event with the element
     * @param presence Presence type: Subscribe,Subscribed,Unsubscribe,Unsubscribed
     * @param local The local (destination) user
     * @param remote The remote (source) user
     */
    virtual void processSubscribe(JBEvent* event, Presence presence,
	const JabberID& local, const JabberID& remote);

    /**
     * Process a presence unavailable element
     * @param event The event with the element
     * @param local The local (destination) user
     * @param remote The remote (source) user
     */
    virtual void processUnavailable(JBEvent* event, const JabberID& local,
	const JabberID& remote);

    /**
     * Process a presence element
     * @param event The event with the element
     * @param local The local (destination) user
     * @param remote The remote (source) user
     */
    virtual void processPresence(JBEvent* event, const JabberID& local,
	const JabberID& remote);

    /**
     * Notify on probe request with users we don't know about
     * @param event The event with the element
     * @param local The local (destination) user
     * @param remote The remote (source) user
     * @return False to send item-not-found error
     */
    virtual bool notifyProbe(JBEvent* event, const JabberID& local,
	const JabberID& remote);

    /**
     * Notify on subscribe event with users we don't know about
     * @param event The event with the element
     * @param local The local (destination) user
     * @param remote The remote (source) user
     * @param presence Presence type: Subscribe,Subscribed,Unsubscribe,Unsubscribed
     * @return False to send item-not-found error
     */
    virtual bool notifySubscribe(JBEvent* event,
	const JabberID& local, const JabberID& remote, Presence presence);

    /**
     * Notify on subscribe event
     * @param user The user that received the event
     * @param presence Presence type: Subscribe,Subscribed,Unsubscribe,Unsubscribed
     */
    virtual void notifySubscribe(XMPPUser* user, Presence presence);

    /**
     * Notify on presence event with users we don't know about or presence unavailable
     *  received without resource (the remote user is entirely unavailable)
     * @param event The event with the element
     * @param local The local (destination) user
     * @param remote The remote (source) user
     * @param available The availability of the remote user
     * @return False to send item-not-found error
     */
    virtual bool notifyPresence(JBEvent* event, const JabberID& local,
	const JabberID& remote, bool available);

    /**
     * Notify on state/capabilities change
     * @param user The user that received the event
     * @param resource The resource that changet its state or capabilities
     */
    virtual void notifyPresence(XMPPUser* user, JIDResource* resource);

    /**
     * Notify when a new user is added
     * Used basically to add a local resource
     * @param user The new user
     */
    virtual void notifyNewUser(XMPPUser* user);

    /**
     * Get a roster. Add a new one if requested.
     * This method is thread safe
     * @param jid The user's jid
     * @param add True to add the user if doesn't exists
     * @param added Optional parameter to be set if a new user was added
     * @return Referenced pointer or 0 if none
     */
    XMPPUserRoster* getRoster(const JabberID& jid, bool add, bool* added);

    /**
     * Get a remote peer of a local one. Add a new one if requested.
     * This method is thread safe
     * @param local The local peer
     * @param remote The remote peer
     * @param addLocal True to add the local user if doesn't exists
     * @param addedLocal Optional parameter to be set if a new local user was added
     * @param addRemote True to add the remote user if doesn't exists
     * @param addedRemote Optional parameter to be set if a new remote user was added
     * @return Referenced pointer or 0 if none
     */
    XMPPUser* getRemoteUser(const JabberID& local, const JabberID& remote,
	bool addLocal, bool* addedLocal, bool addRemote, bool* addedRemote);

    /**
     * Remove a remote peer of a local one
     * This method is thread safe
     * @param local The local peer
     * @param remote The remote peer
     */
    void removeRemoteUser(const JabberID& local, const JabberID& remote);

    /**
     * Check if the given domain is a valid (known) one
     * @param domain The domain name to check
     * @return True if the given domain is a valid one
     */
    bool validDomain(const String& domain);

    /**
     * Send an element through the given stream.
     * If the stream is 0 try to get one from the engine.
     * In any case the element is consumed (deleted)
     * @param element Element to send
     * @param stream The stream to send through
     * @return The result of send operation. False if element is 0
     */
    bool sendStanza(XMLElement* element, JBStream* stream);

    /**
     * Send an error. Error type is 'modify'.
     * If id is 0 sent element will be of type 'presence'. Otherwise: 'iq'
     * @param type The error
     * @param from The from attribute
     * @param to The to attribute
     * @param element The element that generated the error
     * @param stream Optional stream to use
     * @param id Optional id. If present (even if empty) the error element will be of type 'iq'
     * @return The result of send operation
     */
    bool sendError(XMPPError::Type type, const String& from, const String& to,
	XMLElement* element, JBStream* stream = 0, const String* id = 0);

    /**
     * Create an 'presence' element
     * @param from The 'from' attribute
     * @param to The 'to' attribute
     * @param type Presence type as enumeration
     * @return A valid XMLElement pointer
     */
    static XMLElement* createPresence(const char* from,
	const char* to, Presence type = None);

    /**
     * Decode an error element
     * @param element The XML element
     * @param code The 'code' attribute
     * @param type The 'type' attribute
     * @param error The name of the 'error' child
     * @return False if 'element' is 0 or is not a presence one
     */
    static bool decodeError(const XMLElement* element,
	String& code, String& type, String& error);

    /**
     * Get the type of a 'presence' stanza as enumeration
     * @param text The text to check
     * @return Presence type as enumeration
     */
    static inline Presence presenceType(const char* text)
	{ return (Presence)lookup(text,s_presence,None); }

    /**
     * Get the text from a presence type
     * @param presence The presence type
     * @return The associated text or 0
     */
    static inline const char* presenceText(Presence presence)
	{ return lookup(presence,s_presence,0); }

    /**
     * Cleanup rosters
     */
    void cleanup();

protected:
    /**
     * Accept an event from the engine
     * @param event The event to accept
     * @param processed Set to true on exit to signal that the event was already processed
     * @param insert Set to true if accepted to insert on top of the event queue
     * @return False if not accepted, let the engine try another service
     */
    virtual bool accept(JBEvent* event, bool& processed, bool& insert);

    static TokenDict s_presence[];       // Keep the types of 'presence'
    int m_autoSubscribe;                 // Auto subscribe state 
    bool m_delUnavailable;               // Delete unavailable user or resource
    bool m_autoRoster;                   // True if this service make an automatically roster management
    bool m_addOnSubscribe;               // Add new user on subscribe request
    bool m_addOnProbe;                   // Add new user on probe request
    bool m_addOnPresence;                // Add new user on presence
    bool m_autoProbe;                    // Automatically respond to probe requests
    u_int32_t m_probeInterval;           // Interval to probe a remote user
    u_int32_t m_expireInterval;          // Expire interval after probe
    ObjList m_rosters;                   // The rosters

private:
    // Wrapper for getRemoteUser() used when receiving presence
    // Show a debug message if not found
    XMPPUser* recvGetRemoteUser(const char* type, const JabberID& local, const JabberID& remote,
	bool addLocal = false, bool* addedLocal = 0,
	bool addRemote = false, bool* addedRemote = 0);
    void addRoster(XMPPUserRoster* ur);
    void removeRoster(XMPPUserRoster* ur);
};


/**
 * This class holds a JID resource (name,presence,capabilities)
 * @short A JID resource
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
     * Resource presence enumeration
     */
    enum Presence {
	Unknown                = 0,      // unknown
	Available              = 1,      // available
	Unavailable            = 2,      // unavailable
    };

    /**
     * Values of the 'show' child of a presence element
     */
    enum Show {
	ShowAway,                        // away : Temporarily away
	ShowChat,                        // chat : Actively interested in chatting
	ShowDND,                         // dnd  : Busy
	ShowXA,                          // xa   : Extended away
	ShowNone,                        //      : Missing or no text
    };

    /**
     * Constructor. Set data members
     * @param name The resource name
     * @param presence The resource presence
     * @param capability The resource capability
     */
    inline JIDResource(const char* name, Presence presence = Unknown,
	u_int32_t capability = CapChat)
	: m_name(name), m_presence(presence),
	  m_capability(capability), m_show(ShowNone)
	{}

    /**
     * Destructor
     */
    inline virtual ~JIDResource()
	{}

    /**
     * Get the resource name
     * @return The resource name
     */
    inline const String& name() const
	{ return m_name; }

    /**
     * Get the presence attribute
     * @return The presence attribute
     */
    inline Presence presence() const
	{ return m_presence; }

    /**
     * Check if the resource is available
     * @return True if the resource is available
     */
    inline bool available() const
	{ return (m_presence == Available); }

    /**
     * Get the show attribute as enumeration
     * @return The show attribute as enumeration
     */
    inline Show show() const
	{ return m_show; }

    /**
     * Set the show attribute
     * @param s The new show attribute
     */
    inline void show(Show s)
	{ m_show = s; }

    /**
     * Get the status of this resource
     * @return The status of this resource
     */
    inline const String& status() const
	{ return m_status; }

    /**
     * Set the status of this resource
     * @param s The new status of this resource
     */
    inline void status(const char* s)
	{ m_status = s; }

    /**
     * Set the presence information
     * @param value True if available, False if not
     * @return True if presence changed
     */
    bool setPresence(bool value);

    /**
     * Check if the resource has the required capability
     * @param capability The required capability
     * @return True if the resource has the required capability
     */
    inline bool hasCap(Capability capability) const
	{ return (m_capability & capability) != 0; }

    /**
     * Update resource from a presence element
     * @param element A presence element
     * @return True if presence or capability changed changed
     */
    bool fromXML(XMLElement* element);

    /**
     * Add capabilities to a presence element
     * @param element The target presence element
     */
    void addTo(XMLElement* element);

    /**
     * Get the 'show' child of a presence element
     * @param element The XML element
     * @return The text or 0
     */
    static const char* getShow(XMLElement* element);

    /**
     * Get the 'show' child of a presence element
     * @param element The XML element
     * @return The text or 0
     */
    static const char* getStatus(XMLElement* element);

    /**
     * Get the type of a 'show' element as enumeration
     * @param text The text to check
     * @return Show type as enumeration
     */
    static inline Show showType(const char* text)
	{ return (Show)lookup(text,s_show,ShowNone); }

    /**
     * Get the text from a show type
     * @param show The type to get text for
     * @return The associated text or 0
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
 * This class holds a resource list
 * @short A resource list
 */
class YJINGLE_API JIDResourceList : public Mutex
{
    friend class XMPPUser;
public:
    /**
     * Constructor
     */
    inline JIDResourceList()
	: Mutex(true)
	{}

    /**
     * Add a resource to the list if a resource with the given name
     *  doesn't exists
     * @param name The resource name
     * @return False if the the resource already exists in the list
     */
    bool add(const String& name);

    /**
     * Add a resource to the list if not already there.
     * Destroy the received resource if not added
     * @param resource The resource to add
     * @return False if the the resource already exists in the list
     */
    bool add(JIDResource* resource);

    /**
     * Remove a resource from the list
     * @param resource The resource to remove
     * @param del True to delete the resource
     */
    inline void remove(JIDResource* resource, bool del = true)
	{ Lock lock(this); m_resources.remove(resource,del); }

    /**
     * Clear the list
     */
    inline void clear()
	{ Lock lock(this); m_resources.clear(); }

    /**
     * Get a resource with the given name
     * @param name The resource name
     * @return A pointer to the resource or 0
     */
    JIDResource* get(const String& name);

    /**
     * Get the first resource from the list
     * @return A pointer to the resource or 0
     */
    inline JIDResource* getFirst() {
	    Lock lock(this);
	    ObjList* obj = m_resources.skipNull();
	    return obj ? static_cast<JIDResource*>(obj->get()) : 0;
	}

    /**
     * Get the first resource with audio capability
     * @param availableOnly True to get only if available
     * @return A pointer to the resource or 0
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
    bool probe(JBStream* stream, u_int64_t time = Time::msecNow());

    /**
     * Send subscription to remote peer.
     * This method is thread safe.
     * @param type The subscription type: subscribe/unsubscribe/subscribed/unsubscribed.
     * @param stream Optional stream to use to send the data.
     * @return True if send succeedded.
     */
    bool sendSubscribe(JBPresence::Presence type, JBStream* stream);

    /**
     * Send unavailable to remote peer.
     * This method is thread safe.
     * @param stream Optional stream to use to send the data.
     * @return True if send succeedded.
     */
    bool sendUnavailable(JBStream* stream);

    /**
     * Send presence to remote peer.
     * This method is thread safe.
     * @param resource The resource to send from.
     * @param stream Optional stream to use to send the data.
     * @param force True to send even if we've already sent presence from this resource.
     * @return True if send succeedded.
     */
    bool sendPresence(JIDResource* resource, JBStream* stream = 0, bool force = false);

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
	JBStream* stream = 0, bool force = false);

    /**
     * Notify the state of all resources.
     * This method is thread safe.
     * @param remote True for remote resources: notify the presence engine.
     *  False for local resources: send presence to remote user.
     * @param stream Optional stream to use to send the data if remote is false.
     * @param force True to send even if we've already sent presence from a resource.
     */
    void notifyResources(bool remote, JBStream* stream = 0, bool force = false);

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
    void updateSubscription(bool from, bool value, JBStream* stream);

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
