/**
 * ysip.h
 * Yet Another SIP Stack
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004 Null Team
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <telengine.h>
#include <telephony.h>

/** 
 * We use Telephony Engine namespace, which in fact holds just the 
 * generic classes 
*/
namespace TelEngine {

class SIPEngine;
class SIPEvent;

class SIPParty : public RefObject
{
public:
    SIPParty();
    SIPParty(bool reliable);
    virtual ~SIPParty();
    virtual void transmit(SIPEvent* event) = 0;
    inline bool isReliable() const
	{ return m_reliable; }
protected:
    bool m_reliable;
};

class SIPBody
{
public:
    SIPBody(const String& type);
    virtual ~SIPBody();
    inline const String& getType() const
	{ return m_type; }
    static SIPBody* build(const char *buf, int len, const String& type);
    const DataBlock& getBody() const;
    virtual bool isSDP() const
	{ return false; }
    virtual SIPBody* clone() const = 0;
protected:
    virtual void buildBody() const = 0;
    String m_type;
    mutable DataBlock m_body;
};

class SDPBody : public SIPBody
{
public:
    SDPBody();
    SDPBody(const String& type, const char *buf, int len);
    virtual ~SDPBody();
    virtual bool isSDP() const
	{ return true; }
    virtual SIPBody* clone() const;
    inline const ObjList& lines() const
	{ return m_lines; }
    inline void addLine(const char *name, const char *value = 0)
	{ m_lines.append(new NamedString(name,value)); }
    const NamedString* getLine(const char *name) const;
protected:
    SDPBody(const SDPBody& original);
    virtual void buildBody() const;
    ObjList m_lines;
};

class BinaryBody : public SIPBody
{
public:
    BinaryBody(const String& type, const char *buf, int len);
    virtual ~BinaryBody();
    virtual SIPBody* clone() const;
protected:
    BinaryBody(const BinaryBody& original);
    virtual void buildBody() const;
};

class StringBody : public SIPBody
{
public:
    StringBody(const String& type, const char *buf, int len);
    virtual ~StringBody();
    virtual SIPBody* clone() const;
protected:
    StringBody(const StringBody& original);
    virtual void buildBody() const;
    String m_text;
};

class HeaderLine : public NamedString
{
public:
    HeaderLine(const char *name, const String& value);
    HeaderLine(const HeaderLine& original);
    virtual ~HeaderLine();
    inline const ObjList& params() const
	{ return m_params; }
    inline void addParam(const char *name, const char *value = 0)
	{ m_params.append(new NamedString(name,value)); }
    const NamedString* getParam(const char *name) const;
protected:
    ObjList m_params;
};

/**
 * An object that holds the sip message parsed into this library model.
 * This class can be used to parse a sip message from a text buffer, or it
 * can be used to create a text buffer from a sip message.
 */
class SIPMessage : public RefObject
{
public:
    /**
     * Creates a new, empty, outgoing SIPMessage.
     */
    SIPMessage(const char* _method, const char* _uri, const char* _version = "SIP/2.0");

    /**
     * Creates a new SIPMessage from parsing a text buffer.
     */
    SIPMessage(SIPParty* ep, const char *buf, int len = -1);

    /**
     * Creates a new SIPMessage as answer to another message.
     */
    SIPMessage(const SIPMessage* message, int _code, const char* _reason);

    /**
     * Creates an ACK message from the initial message.
     */
    SIPMessage(const SIPMessage* message);

    /**
     * Destroy the message and all
     */
    virtual ~SIPMessage();

    /**
     * Construct a new SIP message by parsing a text buffer
     * @return A pointer to a valid new message or NULL
     */
    static SIPMessage* fromParsing(SIPParty* ep, const char *buf, int len = -1);

    /**
     * Complete missing fields with defaults taken from a SIP engine
     */
    void complete(SIPEngine* engine);

    /**
     * Copy an entire header line (including all parameters) from another message
     * @param message Pointer to the message to copy the header from
     * @param name Name of the header to copy
     * @return True if the header was found and copied
     */
    bool copyHeader(const SIPMessage* message, const char* name);

    /**
     * Copy multiple header lines (including all parameters) from another message
     * @param message Pointer to the message to copy the header from
     * @param name Name of the headers to copy
     * @return Number of headers found and copied
     */
    int copyAllHeaders(const SIPMessage* message, const char* name);

    /**
     * Get the endpoint this message uses
     * @return Pointer to the endpoint of this message
     */
    inline SIPParty* getParty() const
	{ return m_ep; }

    /**
     * Check if this message is valid as result of the parsing
     */
    inline bool isValid() const
	{ return m_valid; }

    /**
     * Check if this message is an answer or a request
     */
    inline bool isAnswer() const
	{ return m_answer; }

    /**
     * Check if this message is an outgoing message
     * @return True if this message should be sent to remote
     */
    inline bool isOutgoing() const
	{ return m_outgoing; }

    /**
     * Check if this message is handled by a reliable protocol
     * @return True if a reliable protocol (TCP, SCTP) is used
     */
    inline bool isReliable() const
	{ return m_ep ? m_ep->isReliable() : false; }

    /**
     *
     */
    inline int getCSeq() const
	{ return m_cseq; }

    /**
     * Find a header line by name
     * @param name Name of the header to locate
     * @return A pointer to the first matching header line or 0 if not found
     */
    const HeaderLine* getHeader(const char* name) const;

    /**
     * Find a header parameter by name
     * @param name Name of the header to locate
     * @param param Name of the parameter to locate in the tag
     * @return A pointer to the first matching header line or 0 if not found
     */
    const NamedString* getParam(const char* name, const char* param) const;

    /**
     * Append a new header line constructed from name and content
     */
    inline void addHeader(const char* name, const char* value = 0)
	{ header.append(new HeaderLine(name,value)); }

    /**
     * Creates a binary buffer from a SIPMessage.
     */
    const DataBlock& getBuffer() const;

    /**
     * Creates a text buffer from the headers.
     */
    const String& getHeaders() const;

    /**
     * Set a new body for this message
     */
    void setBody(SIPBody* newbody = 0);

    /**
     * Sip Version
     */
    String version;

    /**
     * This holds the method name of the message.
     */
    String method;

    /**
     * URI of the request
     */
    String uri;

    /**
     * Status code
     */
    int code;

    /**
     * Reason Phrase
     */
    String reason;

    /**
     * All the headers should be in this list.
     */
    ObjList header;

    /**
     * All the body realted things should be here, including the entire body and
     * the parsed body.
     */
    SIPBody* body;

protected:
    bool parse(const char* buf, int len);
    bool parseFirst(String& line);
    SIPParty* m_ep;
    bool m_valid;
    bool m_answer;
    bool m_outgoing;
    int m_cseq;
    String m_branch;
    mutable String m_string;
    mutable DataBlock m_data;
};

/**
 * All informaton related to a SIP transaction, starting with 1st message
 */
class SIPTransaction : public RefObject
{
public:
    enum State {
	/**
	 * Invalid state - before constructor or after destructor
	 */
	Invalid,

	/**
	 * Initial state - after the initial message was inserted
	 */
	Initial,

	/**
	 * Trying state - got the message but no decision made yet
	 */
	Trying,

	/**
	 * Process state - while locally processing the event
	 */
	Process,

	/**
	 * Retrans state - waiting for cleanup, retransmits latest message
	 */
	Retrans,

	/**
	 * Timeout state - generates a timeout messages and cleans up
	 */
	Timeout,

	/**
	 * Finish state - transmits the last message and goes to Retrans
	 */
	Finish,

	/**
	 * Cleared state - removed from engine, awaiting destruction
	 */
	Cleared,
    };
    /**
     * Constructor from first message
     * @param message A pointer to the initial message, should not be used
     *  afterwards as the transaction takes ownership
     * @param engine A pointer to the SIP engine this transaction belongs
     * @param outgoing True if this transaction is for an outgoing request
     */
    SIPTransaction(SIPMessage* message, SIPEngine* engine, bool outgoing = true);

    /**
     * Destructor - clears all held objects
     */
    virtual ~SIPTransaction();

    /**
     * The current state of the transaction
     */
    inline int getState() const
	{ return m_state; }

    /**
     * The first message that created this transaction
     */
    inline const SIPMessage* initialMessage() const
	{ return m_firstMessage; }

    /**
     * The last message (re)sent by this transaction
     */
    inline const SIPMessage* latestMessage() const
	{ return m_lastMessage; }

    /**
     * The SIPEngine this transaction belongs to
     */
    inline SIPEngine* getEngine() const
	{ return m_engine; }

    /**
     * Check if this transaction was initiated by the remote peer or locally
     * @return True if the transaction was created by an outgoing message
     */
    inline bool isOutgoing() const
	{ return m_outgoing; }

    /**
     * Check if this transaction is an INVITE transaction or not
     * @return True if the transaction is an INVITE
     */
    inline bool isInvite() const
	{ return m_invite; }

    /**
     * Check if this transaction is handled by a reliable protocol
     * @return True if a reliable protocol (TCP, SCTP) is used
     */
    inline bool isReliable() const
	{ return m_firstMessage ? m_firstMessage->isReliable() : false; }

    /**
     * The SIP method this transaction handles
     */
    inline const String& getMethod() const
	{ return m_firstMessage ? m_firstMessage->method : String::empty(); }

    /**
     * The SIP URI this transaction handles
     */
    inline const String& getURI() const
	{ return m_firstMessage ? m_firstMessage->uri : String::empty(); }

    /**
     * The Via branch that may uniquely identify this transaction
     * @return The branch parameter taken from the Via header
     */
    inline const String& getBranch() const
	{ return m_branch; }

    /**
     * The call ID may identify this transaction
     * @return The Call-ID parameter taken from the message
     */
    inline const String& getCallID() const
	{ return m_callid; }

    /**
     * The local tag that may identify this transaction
     * @return The local tag parameter
     */
    inline const String& getLocalTag() const
	{ return m_tag; }

    /**
     * Set the (re)transmission flag that allows the latest outgoing message
     *  to be send over the wire
     */
    inline void setTransmit()
	{ m_transmit = true; }

    /**
     * Check if a message belongs to this transaction and process it if so
     * @param message A pointer to the message to check, should not be used
     *  afterwards if this method returned True
     * @param branch The branch parameter extracted from first Via header
     * @return True if the message was handled by this transaction, in
     *  which case it takes ownership over the message
     */
    virtual bool processMessage(SIPMessage* message, const String& branch);

    /**
     * Get an event for this transaction if any is available.
     * It provides default handling for invalid states, otherwise calls
     *  the more specific version below.
     * You may override this method if you need processing of invalid states.
     * @return A newly allocated event or NULL if none is needed
     */
    virtual SIPEvent* getEvent();

    /**
     * Get an event for this transaction if any is available.
     * This method looks at the transaction's variables and builds an event
     *  for incoming messages, timers, outgoing messages
     * @param state The current state of the transaction
     * @param timeout If timeout occured, number of remaining timeouts,
     *  otherwise -1
     * @return A newly allocated event or NULL if none is needed
     */
    virtual SIPEvent* getEvent(int state, int timeout);

    /**
     * Creates and transmits a final response message
     */
    void setResponse(int code, const char* reason);

    /**
     * Transmits a final response message
     */
    void setResponse(SIPMessage* message);

    /**
     * Set an arbitrary pointer as user specific data
     */
    inline void setUserData(void* data)
	{ m_private = data; }

    /**
     * Return the opaque user data
     */
    inline void* getUserData() const
	{ return m_private; }

protected:
    /**
     * Change the transaction state
     * @param newstate The desired new state
     * @return True if state change occured
     */
    virtual bool changeState(int newstate);

    /**
     * Set the latest message sent by this transaction
     * @param message Pointer to the latest message
     */
    void setLatestMessage(SIPMessage* message = 0);

    /**
     * Set a repetitive timeout
     * @param delay How often (in microseconds) to fire the timeout
     * @param count How many times to keep firing the timeout
     */
    void setTimeout(unsigned long long delay = 0, unsigned int count = 1);

    bool m_outgoing;
    bool m_invite;
    bool m_transmit;
    int m_state;
    unsigned int m_timeouts;
    unsigned long long m_delay;
    unsigned long long m_timeout;
    SIPMessage* m_firstMessage;
    SIPMessage* m_lastMessage;
    SIPEngine* m_engine;
    String m_branch;
    String m_callid;
    String m_tag;
    void *m_private;
};

/**
 * This object is an event that will be taken from SIPEngine
 */ 
class SIPEvent
{
    friend class SIPTransaction;
public:

    SIPEvent()
	: m_message(0), m_transaction(0)
	{ }

    SIPEvent(SIPMessage* message, SIPTransaction* transaction = 0);

    ~SIPEvent();

    /**
     * The SIPEngine this event belongs to
     */
    inline SIPEngine* getEngine() const
	{ return m_transaction ? m_transaction->getEngine() : 0; }

    inline const SIPMessage* getMessage() const
	{ return m_message; }

    inline SIPTransaction* getTransaction() const
	{ return m_transaction; }

    /**
     * Check if the message is an outgoing message
     * @return True if the message should be sent to remote
     */
    inline bool isOutgoing() const
	{ return m_message && m_message->isOutgoing(); }

    /**
     * Check if the message is an incoming message
     * @return True if the message is coming from remote
     */
    inline bool isIncoming() const
	{ return m_message && !m_message->isOutgoing(); }

    /**
     * Get the pointer to the endpoint this event uses
     */
    inline SIPParty* getParty() const
	{ return m_message ? m_message->getParty() : 0; }

    /**
     * Return the opaque user data stored in the transaction
     */
    inline void* getUserData() const
	{ return m_transaction ? m_transaction->getUserData() : 0; }

    /**
     * The state of the transaction when the event was generated
     */
    inline int getState() const
	{ return m_state; }

protected:
    SIPMessage* m_message;
    SIPTransaction* m_transaction;
    int m_state;
};

/**
 * This object can be one for each SIPListener.
 */
class SIPEngine
{
public:
    /**
     * Create the SIP Engine
     */
    SIPEngine(const char* userAgent = 0);

    /**
     * Destroy the SIP Engine
     */
    virtual ~SIPEngine();

    /**
     * Add a message into the transaction list
     * @param buf A buffer containing the SIP message text
     * @param len The length of the message or -1 to interpret as C string
     * @return Pointer to the transaction or NULL if message was invalid
     */
    SIPTransaction* addMessage(SIPParty* ep, const char *buf, int len = -1);

    /**
     * Add a message into the transaction list
     * This method is thread safe
     * @param message A parsed SIP message to add to the transactions
     * @return Pointer to the transaction or NULL if message was invalid
     */
    SIPTransaction* addMessage(SIPMessage* message);

    /**
     * Get a SIPEvent from the queue. 
     * This method mainly looks into the transaction list and get all kind of 
     * events, like an incoming request (INVITE, REGISTRATION), a timer, an
     * outgoing message.
     * This method is thread safe
     */
    SIPEvent *getEvent();

    /**
     * This method should be called very often to get the events from the list and 
     * to send them to processEvent method.
     * @return True if some events were processed this turn
     */
    bool process();

    /**
     * Default handling for events.
     * This method should be overriden for what you need and at the end you
     * should call this default one
     * This method is thread safe
     */
    virtual void processEvent(SIPEvent *event);

    /**
     * Get the length of a timer
     * @param which A one-character constant that selects which timer to return
     * @param reliable Whether we request the timer value for a reliable protocol
     * @return Duration of the selected timer or 0 if invalid
     */
    unsigned long long getTimer(char which, bool reliable = false) const;

    /**
     * Get the default value of the Max-Forwards header for this engine
     * @return The maximum number of hops the request is allowed to pass
     */
    inline unsigned int getMaxForwards() const
	{ return m_maxForwards; }

    /**
     * Get the User agent for this SIP engine
     */
    inline const String& getUserAgent() const
	{ return m_userAgent; }

    /**
     * Get a CSeq value suitable for use in a new request
     */
    inline int getNextCSeq()
	{ return ++m_cseq; }

    /**
     * TransList is the key. 
     * Is the list that holds all the transactions.
     */
    ObjList TransList;

protected:
    Mutex m_mutex;
    unsigned long long m_t1;
    unsigned long long m_t4;
    unsigned int m_maxForwards;
    int m_cseq;
    String m_userAgent;
};

}

/* vi: set ts=8 sw=4 sts=4 noet: */
