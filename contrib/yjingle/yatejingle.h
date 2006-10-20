/**
 * yatejingle.h
 * Yet Another Jingle Stack
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

#ifndef __YATEJINGLE_H
#define __YATEJINGLE_H

#include <yateclass.h>
#include <yatejabber.h>

/**
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

class JGAudio;
class JGTransport;
class JGSession;
class JGEvent;
class JGEngine;
class JGSentStanza;

// Defines
#define JGSESSION_ENDTIMEOUT          2  // Time to wait before destroing a session after hangup
#define JGSESSION_STANZATIMEOUT      10  // Time to wait for a response

/**
 * This class holds a Jingle audio payload description.
 * @short A Jingle audio payload.
 */
class YJINGLE_API JGAudio : public RefObject
{
public:
    /**
     * Constructor.
     * Fill this object from the given attributes.
     * @param id The 'id' attribute.
     * @param name The 'name' attribute.
     * @param clocrate The 'clockrate' attribute.
     * @param bitrate The 'bitrate' attribute.
     */
    inline JGAudio(const char* id, const char* name, const char* clockrate,
	const char* bitrate = 0)
	{ set(id,name,clockrate,bitrate); }

    /**
     * Copy constructor.
     */
    inline JGAudio(const JGAudio& src)
	{ set(src.m_id,src.m_name,src.m_clockrate,src.m_bitrate); }

    /**
     * Constructor.
     * Fill this object from an XML element.
     * @param element The element to fill from.
     */
    inline JGAudio(XMLElement* element)
	{ fromXML(element); }

    /**
     * Destructor.
     */
    virtual ~JGAudio()
	{}

    /**
     * Create a 'description' element.
     * @return Valid XMLElement pointer.
     */
    static XMLElement* createDescription();

    /**
     * Create a 'payload-type' element from this object.
     * @return Valid XMLElement pointer.
     */
    XMLElement* toXML();

    /**
     * Fill this object from a given element.
     * @param element The element.
     */
    void fromXML(XMLElement* element);

    /**
     * Create and add a 'payload-type' child to the given element.
     * @param description The element.
     */
    inline void addTo(XMLElement* description)
	{ if (description) description->addChild(toXML()); }

    /**
     * Set the data.
     * @param id The 'id' attribute.
     * @param name The 'name' attribute.
     * @param clocrate The 'clockrate' attribute.
     * @param bitrate The 'bitrate' attribute.
     */
    void set(const char* id, const char* name, const char* clockrate,
	const char* bitrate = 0);

    // Attributes
    String m_id;
    String m_name;
    String m_clockrate;
    String m_bitrate;
};

/**
 * This class holds a Jingle transport method.
 * @short A Jingle transport.
 */
class YJINGLE_API JGTransport : public RefObject
{
public:
    /**
     * Constructor.
     */
    inline JGTransport()
	{}

    /**
     * Copy constructor.
     */
    JGTransport(const JGTransport& src);

    /**
     * Constructor.
     * Fill this object from an XML element.
     * @param element The element to fill from.
     */
    inline JGTransport(XMLElement* element)
	{ fromXML(element); }

    /**
     * Destructor.
     */
    virtual ~JGTransport()
	{}

    /**
     * Create a 'transport' element.
     * @return Valid XMLElement pointer.
     */
    static XMLElement* createTransport();

    /**
     * Create a 'candidate' element from this object.
     * @return Valid XMLElement pointer.
     */
    XMLElement* toXML();

    /**
     * Fill this object from a given element.
     * @param element The element.
     */
    void fromXML(XMLElement* element);

    /**
     * Create and add a 'candidate' child to the given element.
     * @param description The element.
     */
    inline void addTo(XMLElement* transport)
	{ if (transport) transport->addChild(toXML()); }

    // Attributes
    String m_name;
    String m_address;
    String m_port;
    String m_preference;
    String m_username;
    String m_protocol;
    String m_generation;
    String m_password;
    String m_type;
    String m_network;
};

/**
 * This class does the management of a Jingle session.
 * @short A Jingle session.
 */
class YJINGLE_API JGSession : public RefObject, public Mutex
{
    friend class JGEvent;
    friend class JGEngine;
public:
    /**
     * Session state enumeration.
     */
    enum State {
	Idle,                            // Outgoing stream is waiting for 
	Pending,                         // Session is pending, session-initiate sent/received
	Active,                          // Session is active, session-accept sent/received
	Ending,                          // Session terminated: Wait for write result
	Destroy,                         // The session will be destroyed
    };

    /**
     * Jingle action enumeration.
     */
    enum Action {
	ActAccept,                       // accept
	ActInitiate,                     // initiate
	ActModify,                       // modify
	ActRedirect,                     // redirect
	ActReject,                       // reject
	ActTerminate,                    // terminate
	ActTransportInfo,                // transport-info
	ActTransportAccept,              // transport-accept
	ActCount,
    };

    /**
     * Destructor.
     * Send SessionTerminate if Pending or Active.
     * Notify the owner of termination. Deref the owner.
     */
    virtual ~JGSession();

    /**
     * Get the session direction.
     * @return True if it is an incoming session.
     */
    inline bool incoming() const
	{ return m_incoming; }

    /**
     * Get the session id.
     * @return The session id.
     */
    const String& sid() const
	{ return m_sid; }

    /**
     * Get the local peer's JID.
     * @return The local peer's JID.
     */
    const JabberID& local() const
	{ return m_localJID; }

    /**
     * Get the remote peer's JID.
     * @return The remote peer's JID.
     */
    const JabberID& remote() const
	{ return m_remoteJID; }

    /**
     * Get the initiator of this session.
     * @return The initiator of this session.
     */
    const String& initiator() const
	{ return m_incoming ? m_remoteJID : m_localJID; }

    /**
     * Get the session state.
     * @return The session state as enumeration.
     */
    inline State state() const
	{ return m_state; }

    inline const JBComponentStream* stream() const
	{ return m_stream; }

    /**
     * Get the arbitrary user data of this session.
     * @return The arbitrary user data of this session.
     */
    inline void* jingleConn()
	{ return m_private; }

    /**
     * Set the arbitrary user data of this session.
     * @param jingleconn The new arbitrary user data's value.
     */
    inline void jingleConn(void* jingleconn)
	{ m_private = jingleconn; }

    /**
     * Send a message to the remote peer.
     * This method is thread safe.
     * @param message The message to send.
     * @return False on socket error.
     */
    bool sendMessage(const char* message);

    /**
     * Send an 'iq' of type 'result' to the remote peer.
     * This method is thread safe.
     * @param id The element's id attribute.
     * @return False if send failed.
     */
    bool sendResult(const char* id);

    /**
     * Create and sent an 'error' element.
     * This method is thread safe.
     * @param element The element to respond to.
     * @param error The error.
     * @param type Error type.
     * @param text Optional text to add to the error element.
     * @return False if send failed or element is 0.
     */
    bool sendError(XMLElement* element, XMPPError::Type error,
	XMPPError::ErrorType type = XMPPError::TypeModify,
	const char* text = 0);

    /**
     * Send a 'transport-info' element to the remote peer.
     * This method is thread safe.
     * @param transport The transport data.
     * @return False if send failed.
     */
    inline bool requestTransport(JGTransport* transport)
	{ return sendTransport(transport,ActTransportInfo); }

    /**
     * Send a 'transport-accept' element to the remote peer.
     * This method is thread safe.
     * @param transport Optional transport data to send.
     * @return False if send failed.
     */
    inline bool acceptTransport(JGTransport* transport = 0)
	{ return sendTransport(transport,ActTransportAccept); }

    /**
     * Accept a session.
     * This method is thread safe.
     * @param description The media description element to send.
     * @return False if send failed.
     */
    bool accept(XMLElement* description);

    /**
     * Send a session terminate element.
     * Requirements: .
     * This method is thread safe.
     * @param reject True to reject.
     * @param message Optional message to send before hangup.
     * @return False if the requirements are not met.
     */
    bool hangup(bool reject = false, const char* message = 0);

    /**
     * Check if this session is a destination for an event.
     * Process it if it is.
     * This method is thread safe.
     * @param event The event event to process.
     * @return False if the event doesn't belong to this session.
     */
    bool receive(JBEvent* event);

    /**
     * Get a Jingle event from the queue.
     * This method is thread safe.
     * @param time Current time in miliseconds.
     * @return JGEvent pointer or 0.
     */
    JGEvent* getEvent(u_int64_t time);

    /**
     * Get the jingle action as enumeration from the given text.
     * @param txt Text to check.
     * @return The jingle action as enumeration from the given text.
     */
    static inline Action action(const char* txt)
	{ return (Action)lookup(txt,s_actions,ActCount); }

    /**
     * Get the text associated with an action.
     * @param action The action to find.
     * @return Pointer to the text or 0.
     */
    static inline const char* actionText(Action action)
	{ return lookup(action,s_actions); }

protected:
    /**
     * Constructor.
     * Create an outgoing session.
     * @param engine The engine that owns this session.
     * @param stream The stream this session is bound to.
     * @param callerJID The caller's full JID.
     * @param calledJID The called party's full JID.
     */
    JGSession(JGEngine* engine, JBComponentStream* stream,
	const String& callerJID, const String& calledJID);

    /**
     * Constructor.
     * Create an incoming session.
     * @param engine The engine that owns this session.
     * @param event A valid Jabber Jingle event with action session initiate.
     */
    JGSession(JGEngine* engine, JBEvent* event);

    /**
     * Send a bad-request error. Delete event.
     * @param event An already generated event.
     * @return 0.
     */
    JGEvent* badRequest(JGEvent* event);

    /**
     * Process a received event.
     * @param jbev The Jabber Component event to process.
     * @param time Current time.
     * @return The Jingle event or 0.
     */
    JGEvent* processEvent(JBEvent* jbev, u_int64_t time);

    /**
     * Process received elements in state Pending.
     * @param jbev The Jabber Component event to process.
     * @param event The jingle event to construct.
     * @return The event parameter on success. 0 on failure.
     */
    JGEvent* processStatePending(JBEvent* jbev, JGEvent* event);

    /**
     * Process received elements in state Active.
     * @param jbev The Jabber Component event to process.
     * @param event The jingle event to construct.
     * @return The event parameter on success. 0 on failure.
     */
    JGEvent* processStateActive(JBEvent* jbev, JGEvent* event);

    /**
     * Process received elements in state Idle.
     * @param jbev The Jabber Component event to process.
     * @param event The jingle event to construct.
     * @return The event parameter on success. 0 on failure.
     */
    JGEvent* processStateIdle(JBEvent* jbev, JGEvent* event);

    /**
     * Check if a given event is a valid Jingle one. Send an error if not.
     * Set the event's data on success.
     * @param event The event to check.
     * @return True on success.
     */
    bool decodeJingle(JGEvent* event);

    /**
     * Check if a given event is a valid Error one. Send an error if not.
     * Set the event's data on success.
     * @param event The event to check.
     * @return True on success.
     */
    bool decodeError(JGEvent* event);

    /**
     * Send a 'service-unavailable' error to the remote peer.
     * @param element The element that generated the error.
     * @return True on success.
     */
    inline bool sendEServiceUnavailable(XMLElement* element)
	{ return sendError(element,XMPPError::SServiceUnavailable); }

    /**
     * Send a 'bad-request' error to the remote peer.
     * @param element The element that generated the error.
     * @return True on success.
     */
    inline bool sendEBadRequest(XMLElement* element)
	{ return sendError(element,XMPPError::SBadRequest); }

    /**
     * Send a transport related element to the remote peer.
     * @param transport Transport data to send.
     * @param act The element's type (info, accept, etc).
     * @return True on success.
     */
    bool sendTransport(JGTransport* transport, Action act);

    /**
     * Initiate an outgoing call.
     * @param media Media description element.
     * @param transport Transport description element.
     * @return True on success.
     */
    bool initiate(XMLElement* media, XMLElement* transport);

    /**
     * Send an XML element to remote peer.
     * @param e The element to send.
     * @param addId True to add 'id' attribute.
     * @return True on success.
     */
    bool sendXML(XMLElement* e, bool addId = true);

    /**
     * Create an event.
     * @param jbev Optional Jabber event that generated the event.
     * @return Valid JGEvent pointer.
     */
    JGEvent* createEvent(JBEvent* jbev);

    /**
     * Set last event. Change state. Deref this session if event type is Destroy.
     * @param event Event to raise.
     * @return Valid JGEvent pointer.
     */
    JGEvent* raiseEvent(JGEvent* event);

    /**
     * Create an 'iq' of type 'set' or 'get' with a 'jingle' child.
     * @param action The action of the Jingle stanza.
     * @param media Media description element.
     * @param transport Transport description element.
     * @return Valid XMLElement pointer.
     */
    XMLElement* createJingleSet(Action action,
	XMLElement* media = 0, XMLElement* transport = 0);

    /**
     * Confirm the given element if has to.
     * @param element The element to confirm.
     */
    void confirmIq(XMLElement* element);

    /**
     * Terminate notification from an event. Reset the last generated event.
     * @param event The notifier.
     */
    void eventTerminated(JGEvent* event);

    /**
     * Check if a received event contains a confirmation of a sent stanza.
     * @param jbev The received Jabber Component event.
     * @return The confirmed element or 0.
     */
    JGSentStanza* isResponse(const JBEvent* jbev);

    /**
     * Check if any element timed out.
     * @return True if timeout.
     */
    bool timeout(u_int64_t time);

    /**
     * Keeps the associations between jingle actions and their text.
     */
    static TokenDict s_actions[];

private:
    JGSession() {}                       // Don't use it
    void appendSent(XMLElement* element);

    // State info
    State m_state;                       // Session state
    // Links
    JGEngine* m_engine;                  // The engine that owns this session
    JBComponentStream* m_stream;         // The stream this session is bound to
    // Session info
    bool m_incoming;                     // Session direction
    String m_sid;                        // Session id
    JabberID m_localJID;                 // Local peer's JID
    JabberID m_remoteJID;                // Remote peer's JID
    // Session data
    ObjList m_events;                    // Incoming events from Jabber engine
    JGEvent* m_lastEvent;                // Last generated event
    void* m_private;                     // Arbitrary user data
    // Sent stanzas id generation
    String m_localSid;                   // Local session id (used to generate element's id)
    u_int32_t m_stanzaId;                // Sent stanza id counter
    // Timeout
    u_int64_t m_timeout;                 // Timeout period for Ending state or for sent stanzas
    ObjList m_sentStanza;                // Sent stanzas' id
};

/**
 * This class holds an event generated by a Jingle session.
 * @short A Jingle event
 */
class YJINGLE_API JGEvent
{
    friend class JGSession;
public:
    enum Type {
	Jingle,                          // Action: All, except ActReject, ActTerminate
	Error,
	Unexpected,                      // Unexpected or invalid element
	// Final
	Terminated,                      // m_element is the element that caused the termination
	                                 //  m_reason contains the reason
	Destroy,                         // The engine sould delete the event (causing session destruction)
    };

    /**
     * Destructor.
     * Deref the session. Delete the XML element.
     */
    virtual ~JGEvent();

    /**
     * Get the type of this event.
     * @return The type of this event as enumeration.
     */
    inline Type type() const
	{ return m_type; }

    /**
     * Get the session that generated this event.
     * @return The session that generated this event.
     */
    inline JGSession* session() const
	{ return m_session; }

    /**
     * Get the XML element that generated this event.
     * @return The XML element that generated this event.
     */
    inline XMLElement* element() const
	{ return m_element; }

    /**
     * Get the jingle action as enumeration.
     * @return The jingle action as enumeration.
     */
    inline JGSession::Action action() const
	{ return m_action; }

    /**
     * Get the audio payloads list.
     * @return The audio payloads list.
     */
    inline ObjList& audio()
	{ return m_audio; }

    /**
     * Get the transports list.
     * @return The transports list.
     */
    inline ObjList& transport()
	{ return m_transport; }

    /**
     * Get the id.
     * @return The id.
     */
    inline const String& id()
	{ return m_id; }

    /**
     * Get the reason.
     * @return The reason.
     */
    inline const String& reason()
	{ return m_reason; }

    /**
     * Get the text.
     * @return The text.
     */
    inline const String& text()
	{ return m_text; }

    /**
     * Get the XML element that generated this event and set it to 0.
     * @return The XML element that generated this event.
     */
    inline XMLElement* releaseXML() {
	    XMLElement* tmp = m_element;
	    m_element = 0;
	    return tmp;
	 }

    /**
     * Check if this event is a final one (Terminated or Destroy).
     * @return True if it is.
     */
    bool final();

protected:
    /**
     * Constructor.
     * @param type Event type.
     * @param session The session that generated this event.
     * @param element Optional XML element that generated this event.
     */
    JGEvent(Type type, JGSession* session, XMLElement* element = 0);

private:
    JGEvent() {}                         // Don't use it

    Type m_type;                         // The type of this event
    JGSession* m_session;                // Jingle session that generated this event
    XMLElement* m_element;               // XML element that generated this event
    // Event specific
    JGSession::Action m_action;          // The action if type is Jingle
    ObjList m_audio;                     // The received audio payloads
    ObjList m_transport;                 // The received transport data
    String m_id;                         // The element's id attribute
    String m_reason;                     // The reason if type is Error or Terminated 
    String m_text;                       // The text if type is Error
};

/**
 * This class holds the Jingle engine.
 * @short The Jingle engine.
 */
class YJINGLE_API JGEngine : public JBClient, public DebugEnabler, public Mutex
{
    friend class JGSession;
public:
    /**
     * Constructor.
     * Constructs a Jingle engine.
     * @param jb The JBEngine.
     * @param params Engine's parameters.
     */
    JGEngine(JBEngine* jb, const NamedList& params);

    /**
     * Destructor.
     * Terminates all active sessions. Delete the XMPP engine.
     */
    virtual ~JGEngine();

    /**
     * Initialize this engine.
     * Parameters: None
     * @param params Engine's parameters.
     */
    void initialize(const NamedList& params);

    /**
     * Get events from the Jabber engine.
     * This method is thread safe.
     * @return True if data was received.
     */
    bool receive();

    /**
     * Keep calling receive().
     */
    void runReceive();

    /**
     * Keep calling getEvent() for each session list until no more event is generated.
     * Call processEvent if needded.
     * @return True if at least one event was generated.
     */
    bool process();

    /**
     * Keep calling process().
     */
    void runProcess();

    /**
     * Call getEvent() for each session list until an event is generated or the end is reached.
     * This method is thread safe.
     * @param time Current time in miliseconds.
     * @return The first generated event.
     */
    JGEvent* getEvent(u_int64_t time);

    /**
     * Make an outgoing call.
     * 'media' and 'transport' will be invalid on exit. Don't delete them.
     * @param callerName The local peer's username.
     * @param remoteJID The remote peer's JID.
     * @param media A valid 'description' XML element.
     * @param transport A valid 'transport' XML element.
     * @param message Optional message to send before call.
     * @return Valid JGSession pointer (referenced) on success.
     */
    JGSession* call(const String& callerName, const String& remoteJID,
	XMLElement* media, XMLElement* transport, const char* message = 0);

    /**
     * Default event processor.
     * Action: Delete event.
     * @param event The event to process.
     */
    void defProcessEvent(JGEvent* event);

    /**
     * Create a session id for an outgoing one.
     * @param id Destination string.
     */
    void createSessionId(String& id);

protected:
    /**
     * Process events from the sessions.
     * Default action: Delete event.
     * Descendants must override this method.
     * @param event The event to process.
     */
    virtual void processEvent(JGEvent* event);

    /**
     * Remove a session from the list.
     * @param session Session to remove.
     */
    void removeSession(JGSession* session);

private:
    ObjList m_sessions;                  // List of sessions
    Mutex m_sessionIdMutex;              // Session id counter lock
    u_int32_t m_sessionId;               // Session id counter
};

/**
 * This class holds sent stanzas info used for timeout checking.
 * @short Timeout info.
 */
class YJINGLE_API JGSentStanza : public RefObject
{
    friend class JGSession;
public:
    /**
     * Constructor.
     * @param id The sent stanza's id.
     * @param time The sent time.
     */
    JGSentStanza(const char* id, u_int64_t time = Time::msecNow())
	: m_id(id), m_time(time + JGSESSION_STANZATIMEOUT * 1000)
	{}

    /**
     * Destructor.
     */
    virtual ~JGSentStanza()
	{}

    /**
     * Check if a received element is an iq result or error with the given id or
     *  a sent element failed to be written to socket.
     * @param jbev The received Jabber Component event.
     * @return False if the given element is not a response one or is 0.
     */
    bool isResponse(const JBEvent* jbev) {
	    if (jbev &&
		(jbev->type() == JBEvent::IqResult ||
		 jbev->type() == JBEvent::IqError ||
		 jbev->type() == JBEvent::WriteFail) &&
		m_id == jbev->id())
		return true;
	    return false;
	}

    /**
     * Check if this element timed out.
     * @return True if timeout.
     */
    inline bool timeout(u_int64_t time) const
	{ return time > m_time; }

private:
    String m_id;                         // Sent stanza's id
    u_int64_t m_time;                    // Timeout
};

}

#endif /* __YATEJINGLE_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
