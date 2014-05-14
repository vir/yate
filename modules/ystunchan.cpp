/**
 * ystunchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * STUN support module
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 * Author: Marian Podgoreanu
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
#include <yateversn.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

using namespace TelEngine;

namespace { // anonymous

/*
  socket.stun parameters

  uselocalusername        Add USERNAME attribute when sending requests
                          Defaults to true
  localusername           The USERNAME attribute for outgoing requests
  useremoteusername       Check USERNAME attribute when receiving requests
                          Defaults to true
  remoteusername          The USERNAME attribute for incoming requests
  remoteip                The initial remote address
  remoteport              The initial remote port
  userid                  The id of the user that requested the filter
                          Defaults to 'UNKNOWN'
  rfc5389                 New STUN

  The message's userdata must be a RefObject with the socket to filter

*/

class YStunError;                        // STUN errors
class YStunAttribute;                    // Message attributes
class YStunAttributeError;               //    ERROR-CODE
class YStunAttributeChangeReq;           //    CHANGE-REQUEST
class YStunAttributeAuth;                //    USERNAME, PASSWORD
class YStunAttributeAddr;                //    MAPPED-ADDRESS, RESPONSE-ADDRESS,
                                         //    SOURCE-ADDRESS, CHANGED-ADDRESS, REFLECTED-FROM
class YStunAttributeSoftware;            //    SOFTWARE
class YStunAttributeUnknown;             //    The others
class YStunMessage;                      // STUN message
class YStunUtils;                        // General usefull functions
class YStunMessageOut;                   // Outgoing STUN message (message + retransmission info)
class YStunSocketFilter;                 // Socket filter for STUN
class StunHandler;                       // socket.stun handler
class YStunPlugin;                       // The plugin

/**
 * Defines
 */
// *** Message
#define STUN_MSG_IDLENGTH            16  // Size in bytes of message id
#define STUN_MSG_HEADERLENGTH        20  // Size in bytes of message header (type+length+id)

// *** Attributes
#define STUN_ATTR_OPTIONAL       0x7fff  // Start value for optional attributes
#define STUN_ATTR_HEADERLENGTH        4  // Size in bytes of attribute header (type+length)
#define STUN_ATTR_ADDR_IPV4        0x01  // IPv4 type address for address attributes
#define STUN_ATTR_CHGREQ_PORT         2  // CHANGE-REQUEST: Change port flag
#define STUN_ATTR_CHGREQ_ADDR         4  // CHANGE-REQUEST: Change address flag
#define STUN_ATTR_MI_LENGTH           (STUN_ATTR_HEADERLENGTH + 20) // Size of MESSAGE-INTEGRITY attribute in bytes

// *** Filters
#define FILTER_SECURITYLENGTH         8  // The length of the string used in id generation
                                         //  to validate binding responses
// *** Plugin
#define STUN_SERVER_DEFAULTPORT    3478  // Server port

// Bind request
#define STUN_BINDINTERVAL_MIN      5000  // Bind request interval margins
#define STUN_BINDINTERVAL_MAX     60000
#define STUN_BINDINTERVAL         15000  // Bind request interval in miliseconds

// Message retransmission
#define STUN_RETRANS_COUNT            5  // Retransmission counter
#define STUN_RETRANS_INTERVAL       500  // Starting value (in miliseconds) for retransmission interval

// Set message header values (type+length)
inline void setHeader(u_int8_t* buffer, u_int16_t type, u_int16_t len)
{
    buffer[0] = type >> 8;
    buffer[1] = (u_int8_t)type;
    buffer[2] = len >> 8;
    buffer[3] = (u_int8_t)len;
}

// Get message header values (type+length)
inline void getHeader(const u_int8_t* buffer, u_int16_t& type, u_int16_t& len)
{
    type = buffer[0] << 8 | buffer[1];
    len = buffer[2] << 8 | buffer[3];
}

static union { // Magic cookie
    u_int8_t u8[4];
    u_int16_t u16[2];
    u_int32_t u32;
} magic_cookie = {
    { 0x21, 0x12, 0xA4, 0x42 }
};

/**
 * CRC32 calculator
 */
class CRC32
{
public:
    enum Crc32Poly {
	Crc32  = 0xEDB88320, // HDLC, ANSI X3.66, ITU-T V.42, Ethernet, Serial ATA, MPEG-2, PKZIP, Gzip, Bzip2, PNG
	Crc32C = 0x82F63B78, // iSCSI, SCTP, G.hn payload, SSE4.2, Btrfs, ext4
	Crc32K = 0xEB31D82E,
	Crc32Q = 0xD5828281,
    };
    /**
     * Construct CRC32 calculator.
     * @param polynom CRC32 polynomial to use
     */
    CRC32(Crc32Poly polynom = Crc32)
    {
	u_int32_t poly = polynom;
	for (size_t i = 0; i < 256; ++i) {
	    u_int32_t crc = i;
	    for (size_t j = 0; j < 8; ++j) {
		if (crc & 1)
		    crc = (crc >> 1) ^ poly;
		else
		    crc >>= 1;
	    }
	    table[i] = crc;
	}
    }
    /**
     * Calculate CRC32 of specified block
     * @param buf address of data block
     * @param size size of data block
     * @param crc CRC32 of previous data block or zero
     * @return CRC32 of data block
     */
    u_int32_t crc32(const u_int8_t *buf, size_t size, u_int32_t crc = 0) const
    {
	crc = ~crc;
	for (size_t i = 0; i < size; ++i)
	    crc = table[buf[i] ^ (crc & 0xFF)] ^ (crc >> 8);
	return ~crc;
    }
    /**
     * Fancy way to call 'crc32' method
     */
    inline u_int32_t operator()(const uint8_t *buf, size_t size, u_int32_t crc = 0)
    {
	return crc32(buf, size, crc);
    }
private:
    u_int32_t table[256];
};


/**
 * YStunError
 */
class YStunError
{
public:
    enum Type {
	BadReq = 144,                    // BAD REQUEST
	Auth   = 174,                    // STALE CREDENDIALS
	RoleConflict = 487,              // rfc5245 section 19.2
    };
    static TokenDict s_tokens[];         // Error strings
};

/**
 * Attributes
 */
class YStunAttribute : public RefObject
{
public:
    enum Type {
	MappedAddress =       0x0001,    // MAPPED-ADDRESS
	ResponseAddress =     0x0002,    // RESPONSE-ADDRESS    (Reserved in rfc5389)
	ChangeRequest =       0x0003,    // CHANGE-REQUEST      (Reserved in rfc5389)
	SourceAddress =       0x0004,    // SOURCE-ADDRESS      (Reserved in rfc5389)
	ChangedAddress =      0x0005,    // CHANGED-ADDRESS     (Reserved in rfc5389)
	Username =            0x0006,    // USERNAME
	Password =            0x0007,    // PASSWORD            (Reserved in rfc5389)
	MessageIntegrity =    0x0008,    // MESSAGE-INTEGRITY
	ErrorCode =           0x0009,    // ERROR-CODE
	UnknownAttributes =   0x000a,    // UNKNOWN-ATTRIBUTES
	ReflectedFrom =       0x000b,    // REFLECTED-FROM      (Reserved in rfc5389)
	// rfc5389 (new STUN) and others
	ChannelNumber =       0x000c,    // CHANNEL-NUMBER      (rfc5766 - TURN)
	Lifetime =            0x000d,    // LIFETIME            (rfc5766 - TURN)
	XorPeerAddress =      0x0012,    // XOR-PEER-ADDRESS    (rfc5766 - TURN)
	Data =                0x0013,    // DATA                (rfc5766 - TURN)
	Realm =               0x0014,    // REALM
	Nonce =               0x0015,    // NONCE
	XorRelayedAddress =   0x0016,    // XOR-RELAYED-ADDRESS (rfc5766 - TURN)
	EvenPort =            0x0018,    // EVEN-PORT           (rfc5766 - TURN)
	RequestedTransport =  0x0019,    // REQUESTED-TRANSPORT (rfc5766 - TURN)
	DontFragment =        0x001a,    // DONT-FRAGMENT       (rfc5766 - TURN)
	XorMappedAddress =    0x0020,    // XOR-MAPPED-ADDRESS
	ReservationToken =    0x0022,    // RESERVATION-TOKEN   (rfc5766 - TURN)
	Software =            0x8022,    // SOFTWARE
	AlternateServer =     0x8023,    // ALTERNATE-SERVER
	Priority =            0x0024,    // PRIORITY            (rfc5245 - ICE)
	UseCandidate =        0x0025,    // USE-CANDIDATE       (rfc5245 - ICE)
	Fingerprint =         0x8028,    // FINGERPRINT
	IceControlled =       0x8029,    // ICE-CONTROLLED      (rfc5245 - ICE)
	IceControlling =      0x802A,    // ICE-CONTROLLING     (rfc5245 - ICE)
	Unknown,                         // None of the above
    };

    inline YStunAttribute(u_int16_t type) : m_type(type) {}
    virtual ~YStunAttribute() {}
    inline u_int16_t type() const
	{ return m_type; }
    const char* text() const
	{ return lookup(m_type,s_tokens); }
    /**
     * Add the attribute to a string.
     * @param dest The destination.
     */
    virtual void toString(String& dest) = 0;
    /**
     * Create from received buffer.
     * @param buffer Data to process. Doesn't include the attribute header (type+length).
     * @param len Data length (for this attribute).
     * @return False on invalid data.
     */
    virtual bool fromBuffer(u_int8_t* buffer, u_int16_t len) = 0;
    /**
     * Append this attribute to a buffer to be sent.
     * @param buffer The destination buffer.
     */
    virtual void toBuffer(DataBlock& buffer) = 0;

    static TokenDict s_tokens[];

private:
    u_int16_t m_type;                    // Attribute type
};

class YStunAttributeError : public YStunAttribute
{
public:
    inline YStunAttributeError(u_int16_t code = 0, const char* text = 0)
	: YStunAttribute(ErrorCode), m_code(code), m_text(text)
	{}
    virtual ~YStunAttributeError() {}
    virtual void toString(String& dest);
    virtual bool fromBuffer(u_int8_t* buffer, u_int16_t len);
    virtual void toBuffer(DataBlock& buffer);
private:
    u_int16_t m_code;                    // Error code
    String m_text;                       // Error string
};

// Change request
// 4 bytes. Bits 1 and 2 are used
class YStunAttributeChangeReq : public YStunAttribute
{
public:
    inline YStunAttributeChangeReq(bool chg_port = false, bool chg_addr = false)
	: YStunAttribute(ChangeRequest), m_flags(0)
	{
	    if (chg_port)
		m_flags |= STUN_ATTR_CHGREQ_PORT;
	    if (chg_addr)
		m_flags |= STUN_ATTR_CHGREQ_ADDR;
	}
    virtual ~YStunAttributeChangeReq() {}
    virtual void toString(String& dest);
    virtual bool fromBuffer(u_int8_t* buffer, u_int16_t len);
    virtual void toBuffer(DataBlock& buffer);
private:
    u_int32_t m_flags;                   // Change request flags
};

// Username or Password
// The length MUST be a multiple of 4
class YStunAttributeAuth : public YStunAttribute
{
public:
    inline YStunAttributeAuth(u_int16_t type)
	: YStunAttribute(type)
	{}
    inline YStunAttributeAuth(const char* value, bool username = true)
	: YStunAttribute(username ? Username : Password), m_auth(value)
	{}
    virtual ~YStunAttributeAuth() {}
    virtual void toString(String& dest);
    virtual bool fromBuffer(u_int8_t* buffer, u_int16_t len);
    virtual void toBuffer(DataBlock& buffer);
private:
    String m_auth;                       // Username/Password value
};

// IP Address + port
class YStunAttributeAddr : public YStunAttribute
{
public:
    inline YStunAttributeAddr(u_int16_t type)
	: YStunAttribute(type), m_port(0)
	{}
    inline YStunAttributeAddr(u_int16_t type, const String& addr,
	u_int16_t port)
	: YStunAttribute(type), m_addr(addr), m_port(port)
	{}
    virtual ~YStunAttributeAddr() {}
    virtual void toString(String& dest);
    virtual bool fromBuffer(u_int8_t* buffer, u_int16_t len);
    virtual void toBuffer(DataBlock& buffer);
private:
    String m_addr;                       // Address
    u_int16_t m_port;                    // Port
};

// Software + version
class YStunAttributeSoftware : public YStunAttribute
{
public:
    inline YStunAttributeSoftware()
	: YStunAttribute(Software)
	{}
    inline YStunAttributeSoftware(const String& soft)
	: YStunAttribute(Software), m_soft(soft)
	{}
    virtual ~YStunAttributeSoftware() {}
    virtual void toString(String& dest);
    virtual bool fromBuffer(u_int8_t* buffer, u_int16_t len);
    virtual void toBuffer(DataBlock& buffer);
private:
    String m_soft;
};

// Message Integrity (rfc5389 section 15.4)
class YStunAttributeMessageIntegrity: public YStunAttribute
{
public:
    inline YStunAttributeMessageIntegrity()
	: YStunAttribute(MessageIntegrity)
	, m_pos(0)
	{}
    inline YStunAttributeMessageIntegrity(const String& password)
	: YStunAttribute(MessageIntegrity)
	, m_password(password)
	, m_pos(0)
	{}
    virtual ~YStunAttributeMessageIntegrity() {}
    virtual void toString(String& dest);
    virtual bool fromBuffer(u_int8_t* buffer, u_int16_t len);
    virtual void toBuffer(DataBlock& buffer);
    void updateMsg(DataBlock& msg) const;
public:
    DataBlock m_mac;
    String m_password;
    u_int32_t m_pos;
};

// Fingerprint (rfc5389 section 15.4)
class YStunAttributeFingerprint: public YStunAttribute
{
public:
    inline YStunAttributeFingerprint()
	: YStunAttribute(Fingerprint)
	, m_pos(0)
	{}
    virtual ~YStunAttributeFingerprint() {}
    virtual void toString(String& dest);
    virtual bool fromBuffer(u_int8_t* buffer, u_int16_t len);
    virtual void toBuffer(DataBlock& buffer);
    void updateMsg(DataBlock& msg) const;
public:
    u_int32_t m_pos;
    u_int32_t m_value;
};

// UseCandidate (ICE)
class YStunAttributeUseCandidate: public YStunAttribute
{
public:
    inline YStunAttributeUseCandidate()
	: YStunAttribute(UseCandidate)
	{}
    virtual ~YStunAttributeUseCandidate() {}
    virtual void toString(String& dest) { }
    virtual bool fromBuffer(u_int8_t* buffer, u_int16_t len) { return len == 0; }
    virtual void toBuffer(DataBlock& buffer)
    {
	DataBlock tmp;
	tmp.resize(4);
	setHeader((u_int8_t*)tmp.data(), type(), 0);
	buffer += tmp;
    }
};

// Unknown
class YStunAttributeUnknown : public YStunAttribute
{
public:
    inline YStunAttributeUnknown(u_int16_t type)
	: YStunAttribute(Unknown), m_unknownType(type)
	{}
    virtual ~YStunAttributeUnknown() {}
    virtual void toString(String& dest);
    virtual bool fromBuffer(u_int8_t* buffer, u_int16_t len);
    virtual void toBuffer(DataBlock& buffer);
private:
    u_int16_t m_unknownType;            // The unknown type
    DataBlock m_data;                   // Data
};

/**
 * YStunMessage
 */
class YStunMessage : public RefObject
{
public:
    enum Type {
	BindReq =   0x0001,              // Binding Request
	Allocate =  0x0003,              // TURN (rfc5766) Allocate
	Refresh =   0x0004,              // TURN (rfc5766) Refresh
	Send =      0x0006,              // TURN (rfc5766) Send
	Data =      0x0007,              // TURN (rfc5766) Data
	CreatePermission = 0x0008,       // TURN (rfc5766) CreatePermission
	ChannelBind = 0x0009,            // TURN (rfc5766) ChannelBind
	BindRsp =   0x0101,              // Binding Response
	BindErr =   0x0111,              // Binding Error Response
	SecretReq = 0x0002,              // Shared Secret Request
	SecretRsp = 0x0102,              // Shared Secret Response
	SecretErr = 0x0112,              // Shared Secret Error Response
    };
    YStunMessage(Type type, void* id, size_t id_len);
    virtual ~YStunMessage() {}
    inline Type type() const
	{ return m_type; }
    inline const DataBlock& id() const
	{ return m_id; }
    const char* text() const
	{ return lookup(m_type,s_tokens); }
    inline void addAttribute(YStunAttribute* attr)
	{ m_attributes.append(attr); }
    bool checkIntegrity(const u_int8_t* data, const String& password) const;
    YStunAttribute* getAttribute(u_int16_t attrType, bool remove = false);
    void toMessage(Message& msg) const;
    bool toBuffer(DataBlock& buffer) const;
    void print();
    static TokenDict s_tokens[];
private:
    Type m_type;                         // Message type
    DataBlock m_id;                         // Message id
    ObjList m_attributes;                // Message attributes
};

/**
 * YStunUtils
 */
class YStunUtils
{
public:
    YStunUtils();
    static bool isStun(const void* data, u_int32_t len, YStunMessage::Type& type, bool& isRfc5766);
    static YStunMessage* decode(const void* data, u_int32_t len, YStunMessage::Type type);
    // Create an id used to send a binding request
    static void createId(String& id);
    static void createId(DataBlock& id);
    // Send a message through the given socket
    static bool sendMessage(Socket* socket, const YStunMessage* msg,
	const SocketAddr& addr, void* sender = 0);
    // Get the error attribute of a message.
    // Return false if the message doesn't have one
    static bool getAttrError(YStunMessage* msg, String& errStr);
    // Get an address attribute of a message.
    // Return false if the message doesn't have this attribute
    static bool getAttrAddr(YStunMessage* msg, YStunAttribute::Type type,
	String& addr);
    // Get an auth attribute of a message.
    // Return false if the message doesn't have this attribute
    static bool getAttrAuth(YStunMessage* msg, YStunAttribute::Type type,
	String& auth);
    // Calculate FINGERPRINT value
    static u_int32_t calcFingerprint(const void * data, u_int32_t len);
    // Calculate HMAC-SHA1 MESSAGE-INTEGRITY value
    static bool calcMessageIntegrity(const String& password, const u_int8_t * data, u_int32_t m_i_attr_pos, DataBlock& result);
protected:
    static Mutex s_idMutex;              // Lock id changes
    static unsigned int m_id;            // Used to generate unique id for requests
};

/**
 * YStunMessageOut
 */
class YStunMessageOut : public RefObject
{
    friend class YStunSocketFilter;
public:
    YStunMessageOut(YStunMessage* msg, const SocketAddr addr, void* sender = 0);
    virtual ~YStunMessageOut();
    inline bool isId(const YStunMessage* msg) const {
	    if (!(m_msg && msg))
		return false;
	    String id;
	    id.assign((char*)msg->id().data(), msg->id().length());
	    return id.endsWith(id.c_str() + FILTER_SECURITYLENGTH);
	}
    inline bool timeToSend(u_int64_t time)
	{ return time >= m_next; }
    inline bool timeout()
	{ return !m_count; }
    inline bool send(Socket* socket, u_int64_t time) {
	    update(time);
	    return YStunUtils::sendMessage(socket,m_msg,m_addr,m_sender);
	}
    // Reset retransmission info: m_count, m_interval, m_next
    // Set the address to the new one
    void reset(const SocketAddr& addr);

protected:
    inline void update(u_int64_t time) {
	    m_count--;
	    m_interval *= 2;
	    m_next = time + m_interval;
	}
private:
    YStunMessage* m_msg;                 // The message
    SocketAddr m_addr;                   // Remote peer's address
    void* m_sender;                      // The sender
    u_int16_t m_count;                   // Retransmission counter
    u_int64_t m_interval;                // Retransmission interval
    u_int64_t m_next;                    // Time for next retransmission
};

/**
 * YStunSocketFilter
 */
class YStunSocketFilter : public SocketFilter
{
    friend class YStunPlugin;
public:
    YStunSocketFilter();
    virtual ~YStunSocketFilter();
    // Received: call by the socket
    virtual bool received(void* buffer, int length, int flags,
	const struct sockaddr* addr, socklen_t addrlen);
    // Timer handler: Handle retransmission for binding request
    virtual void timerTick(const Time& when);
    // Install the filter. Return false if it fails
    bool install(Socket* sock, const Message* msg);
protected:
    // Process a received message. Destroy it after processing
    bool processMessage(YStunMessage* msg);
    // Process a received binding request
    // Respond to it
    void processBindReq(YStunMessage* msg);
    // Process a received binding response (error or response)
    void processBindResult(YStunMessage* msg);
    // Send chan.rtp message to update RTP peer address
    void dispatchChanRtp();
private:
    SocketAddr m_remoteAddr;             // Last received packet's address
    bool m_useLocalUsername;             // True to use local username when sending bind request
    bool m_useRemoteUsername;            // True to use remote username to check bind requests
    String m_localUsername;              // Local username
    String m_remoteUsername;             // Remote username
    // Filter's user
    String m_userId;                     // User's id
    // Bind request
    YStunMessageOut* m_bindReq;          // The message
    Mutex m_bindReqMutex;                // Lock message operations
    u_int64_t m_bindReqNext;             // Time for the next request
    // Address authenticated ?
    bool m_notFound;                     // Flag set when we found the right address for the remote peer
    String m_security;                   // Random string used to build id for a binding request
    bool m_rfc5389;                      // RFC5389 - "new" stun (XOR mapped addr and others - used in ICE).
    String m_localPassword, m_remotePassword; // passwords used in message integrity checks
    bool m_passive;                      // Do not send any bind requests, just reply on incoming
};

/**
 * YStunListener
 */
class YStunListener: public GenObject, public Thread, public Mutex
{
public:
    YStunListener(const String& name, Thread::Priority prio = Thread::Normal);
    ~YStunListener();
    void init(const NamedList& params);
    virtual const String& toString()
	{ return m_name; }
    const String& addr() const
	{ return m_addr.addr(); }
protected:
    virtual void run();
    bool received(DataBlock& pkt, const SocketAddr& remote);
private:
    String m_name;
    SocketAddr m_addr;                   // Address to bind socket to
    Socket* m_sock;
    unsigned int m_maxpkt;               // Max receive packet length
};

/**
 * YStunPlugin
 */
class YStunPlugin : public Module
{
    friend class YStunListener;
    enum Relay {
	Stop = Private,
    };
public:
    YStunPlugin();
    virtual ~YStunPlugin();
    virtual void initialize();

    inline u_int64_t bindInterval()
	{ return m_bindInterval; }
    inline const String& software()
        { return m_software; }
protected:
    bool received(Message& msg, int id);
    void cancelAllListeners();
    void setupListener(const String& name, const NamedList& params);
private:
    u_int32_t m_bindInterval;            // Bind request interval
    String m_software;
    ObjList m_listeners;
    Mutex m_mutex;
};

/**
 * Local data
 */
static YStunPlugin iplugin;
static Configuration s_cfg;

/**
 * YStunError
 */
TokenDict YStunError::s_tokens[] = {
	{"BAD REQUEST",        BadReq},
	{"STALE CREDENDIALS",  Auth},
	{"ROLE CONFLICT",      RoleConflict},
	{0,0}
	};

/**
 * Attributes
 */
TokenDict YStunAttribute::s_tokens[] = {
	{"MAPPED-ADDRESS",     MappedAddress},
	{"RESPONSE-ADDRESS",   ResponseAddress},
	{"CHANGE-REQUEST",     ChangeRequest},
	{"SOURCE-ADDRESS",     SourceAddress},
	{"CHANGED-ADDRESS",    ChangedAddress},
	{"USERNAME",           Username},
	{"PASSWORD",           Password},
	{"MESSAGE-INTEGRITY",  MessageIntegrity},
	{"ERROR-CODE",         ErrorCode},
	{"UNKNOWN-ATTRIBUTES", UnknownAttributes},
	{"REFLECTED-FROM",     ReflectedFrom},
	{"UNKNOWN",            Unknown},
	{"CHANNEL-NUMBER",     ChannelNumber},         // (rfc5766 - TURN)
	{"LIFETIME",           Lifetime},              // (rfc5766 - TURN)
	{"XOR-PEER-ADDRESS",   XorPeerAddress},        // (rfc5766 - TURN)
	{"DATA",               Data},                  // (rfc5766 - TURN)
	{"REALM",              Realm},
	{"NONCE",              Nonce},
	{"XOR-RELAYED-ADDRESS", XorRelayedAddress},    // (rfc5766 - TURN)
	{"EVEN-PORT",          EvenPort},              // (rfc5766 - TURN)
	{"REQUESTED-TRANSPORT", RequestedTransport},   // (rfc5766 - TURN)
	{"DONT-FRAGMENT",      DontFragment},          // (rfc5766 - TURN)
	{"XOR-MAPPED-ADDRESS", XorMappedAddress},
	{"RESERVATION-TOKEN",  ReservationToken},      // (rfc5766 - TURN)
	{"SOFTWARE",           Software},
	{"ALTERNATE-SERVER",   AlternateServer},
	{"PRIORITY",           Priority},              // (rfc5245 - ICE)
	{"USE-CANDIDATE",      UseCandidate},          // (rfc5245 - ICE)
	{"FINGERPRINT",        Fingerprint},
	{"ICE-CONTROLLED",     IceControlled},         // (rfc5245 - ICE)
	{"ICE-CONTROLLING",    IceControlling},        // (rfc5245 - ICE)
	{0,0}
	};

/**
 * socket.stun message handler
 */
class StunHandler : public MessageHandler
{
public:
    StunHandler()
	: MessageHandler("socket.stun",100,iplugin.name())
	{}
    // Process message. Create and install filter.
    virtual bool received(Message &msg);
};

// YStunAttributeError
void YStunAttributeError::toString(String& dest)
{
    dest = "";
    dest << m_code << ":" << m_text;
}

bool YStunAttributeError::fromBuffer(u_int8_t* buffer, u_int16_t len)
{
// 'len' must be at least 4 and a multiple of 4
// buffer[2]: Error class (3 bits)
// buffer[3]: Error code modulo 100 (Values: 0..99)
    if (!(buffer && len >= 4 && (len % 4) == 0))
	return false;
    m_code = (buffer[2] & 0x07) * 100 + (buffer[3] < 100 ? buffer[3] : 0);
    if (len > 4)
	m_text.assign((const char*)buffer + 4,len - 4);
    return true;
}

void YStunAttributeError::toBuffer(DataBlock& buffer)
{
    u_int8_t header[8] = {0,0,0,0,
		0,0,u_int8_t(m_code / 100),u_int8_t(m_code % 100)};
    setHeader(header,type(),4 + m_text.length());
    DataBlock tmp(header,sizeof(header));
    buffer += tmp;
    buffer.append(m_text);
}

// YStunAttributeChangeReq
void YStunAttributeChangeReq::toString(String& dest)
{
    dest = (unsigned int)m_flags;
}

bool YStunAttributeChangeReq::fromBuffer(u_int8_t* buffer, u_int16_t len)
{
    if (!(buffer && len == 4))
	return false;
    m_flags = buffer[0] << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3];
    return true;
}

void YStunAttributeChangeReq::toBuffer(DataBlock& buffer)
{
    u_int8_t header[8] = {0,0,0,0,
		u_int8_t(m_flags >> 24),u_int8_t(m_flags >> 16),u_int8_t(m_flags >> 8),(u_int8_t)m_flags};
    setHeader(header,type(),4);
    DataBlock tmp(header,sizeof(header));
    buffer += tmp;
}

// YStunAttributeAuth
void YStunAttributeAuth::toString(String& dest)
{
    dest = m_auth;
}

bool YStunAttributeAuth::fromBuffer(u_int8_t* buffer, u_int16_t len)
{
    if (!(buffer && len))
	return false;
    m_auth.assign((const char*)buffer,len);
    return true;
}

void YStunAttributeAuth::toBuffer(DataBlock& buffer)
{
    u_int8_t header[4];
    setHeader(header,type(),m_auth.length());
    DataBlock tmp(header,sizeof(header));
    buffer += tmp;
    buffer.append(m_auth);
}

// YStunAttributeAddr
void YStunAttributeAddr::toString(String& dest)
{
    dest = "";
    dest << m_addr << ":" << m_port;
}

bool YStunAttributeAddr::fromBuffer(u_int8_t* buffer, u_int16_t len)
{
    if (!(buffer && len == 8 && buffer[1] == STUN_ATTR_ADDR_IPV4))
	return false;
    if(type() == XorMappedAddress) {
	uint16_t p = *(uint16_t*)&buffer[2];
	uint32_t a = *(uint32_t*)&buffer[4];
	p ^= magic_cookie.u16[0];
	a ^= magic_cookie.u32; // IPV4 only
	m_port = ntohs(p);
	m_addr = "";
	u_int8_t* tmp = (u_int8_t*)&a;
	m_addr << tmp[0] << "." << tmp[1] << "." << tmp[2] << "." << tmp[3];
    }
    else {
	m_port = buffer[2] << 8 | buffer[3];
	m_addr = "";
	m_addr << buffer[4] << "." << buffer[5] << "." << buffer[6] << "."
	    << buffer[7];
    }
    return true;
}

void YStunAttributeAddr::toBuffer(DataBlock& buffer)
{
    u_int8_t header[12] = {0,0,0,0,
		0,STUN_ATTR_ADDR_IPV4,u_int8_t(m_port >> 8),(u_int8_t)m_port,
		0,0,0,0};
    setHeader(header,type(),8);
    for (int start = 0, i = 8; i < 12; i++) {
	int end = m_addr.find('.',start);
	if (end == -1)
	    end = m_addr.length();
	if (end != start)
	    header[i] = m_addr.substr(start,end - start).toInteger();
	if (end == (int)m_addr.length())
	    break;
	start = end + 1;
    }
    if(type() == XorMappedAddress) {
	uint16_t* p = (uint16_t*)&header[6];
	uint32_t* a = (uint32_t*)&header[8];
	*p ^= magic_cookie.u16[0];
	*a ^= magic_cookie.u32; // IPV4 only
    }
    DataBlock tmp(header,sizeof(header));
    buffer += tmp;
}

// YStunAttributeSoftware
void YStunAttributeSoftware::toString(String& dest)
{
    dest = m_soft;
}

bool YStunAttributeSoftware::fromBuffer(u_int8_t* buffer, u_int16_t len)
{
    if (!(buffer && len))
	return false;
    m_soft.assign((const char*)buffer,len);
    return true;
}

void YStunAttributeSoftware::toBuffer(DataBlock& buffer)
{
    u_int8_t header[4];
    setHeader(header,type(),m_soft.length());
    DataBlock tmp(header,sizeof(header));
    buffer += tmp;
    buffer.append(m_soft);
}

// YStunAttributeMessageIntegrity
void YStunAttributeMessageIntegrity::toString(String& dest)
{
    dest.hexify(m_mac.data(), m_mac.length());
}

bool YStunAttributeMessageIntegrity::fromBuffer(u_int8_t* buffer, u_int16_t len)
{
    if (!(buffer && len == 20))
	return false;
    m_mac.assign((char*)buffer, len);
    return true;
}

void YStunAttributeMessageIntegrity::toBuffer(DataBlock& buffer)
{
    u_int8_t header[4];
    if(m_mac.length() != 20)
	m_mac.resize(20); // MUST be 20 bytes long
    setHeader(header,type(),m_mac.length());
    DataBlock tmp(header,sizeof(header));
    buffer += tmp;
    buffer += m_mac;
}

void YStunAttributeMessageIntegrity::updateMsg(DataBlock& msg) const
{
    DataBlock mac;
    YStunUtils::calcMessageIntegrity(m_password, (u_int8_t*)msg.data(), m_pos, mac);
    memcpy(msg.data(m_pos + 4), mac.data(), mac.length());
}

// YStunAttributeFingerprint
void YStunAttributeFingerprint::toString(String& dest)
{
    dest.hexify(&m_value, sizeof(m_value));
}

bool YStunAttributeFingerprint::fromBuffer(u_int8_t* buffer, u_int16_t len)
{
    if (!(buffer && len == 4))
	return false;
    m_value = ntohl(*(u_int32_t*)buffer);
    return true;
}

void YStunAttributeFingerprint::toBuffer(DataBlock& buffer)
{
    DataBlock tmp;
    tmp.resize(8);
    setHeader((u_int8_t*)tmp.data(), type(), 4);
    *(u_int32_t*)tmp.data(4) = htonl(m_value);
    buffer += tmp;
}

void YStunAttributeFingerprint::updateMsg(DataBlock& msg) const
{
    u_int32_t fp = YStunUtils::calcFingerprint(msg.data(), m_pos);
    *(u_int32_t*)msg.data(m_pos + 4) = htonl(fp);
}

// YStunAttributeUnknown
void YStunAttributeUnknown::toString(String& dest)
{
    dest = "";
    dest << "Data length: " << m_data.length();
}

bool YStunAttributeUnknown::fromBuffer(u_int8_t* buffer, u_int16_t len)
{
    if (!(buffer && (len % 4) == 0))
	return false;
    DataBlock tmp(buffer,len);
    m_data = tmp;
    return true;
}

void YStunAttributeUnknown::toBuffer(DataBlock& buffer)
{
    u_int8_t header[4];
    setHeader(header,m_unknownType,m_data.length());
    DataBlock tmp(header,sizeof(header));
    buffer += tmp;
    buffer += m_data;
}

/**
 * YStunMessage
 */
TokenDict YStunMessage::s_tokens[] = {
	{"BindReq",   BindReq},
	{"BindRsp",   BindRsp},
	{"BindErr",   BindErr},
	{"SecretReq", SecretReq},
	{"SecretRsp", SecretRsp},
	{"SecretErr", SecretErr},
	// TURN methods
	{"Allocate",  Allocate},
	{"Refresh",   Refresh},
	{"Send",      Send},
	{"Data",      Data},
	{"CreatePermission", CreatePermission},
	{"ChannelBind", ChannelBind},
	{0,0}
	};

YStunMessage::YStunMessage(Type type, void* id, size_t id_len)
    : m_type(type),
      m_id(id, id_len)
{
    if (!id)
	YStunUtils::createId(m_id);
}

YStunAttribute* YStunMessage::getAttribute(u_int16_t attrType, bool remove)
{
    ObjList* obj = m_attributes.skipNull();
    for (; obj; obj = obj->skipNext()) {
	YStunAttribute* attr = static_cast<YStunAttribute*>(obj->get());
	if (attr->type() == attrType) {
	    if (remove)
		m_attributes.remove(attr,false);
	    return attr;
	}
    }
    return 0;
}

void YStunMessage::toMessage(Message& msg) const
{
    String id;
    id.hexify(m_id.data(), m_id.length());
    msg.addParam("message_type",text());
    msg.addParam("message_id",id);
    // Add attributes
    ObjList* obj = m_attributes.skipNull();
    for (; obj; obj = obj->skipNext()) {
	YStunAttribute* attr = static_cast<YStunAttribute*>(obj->get());
	String tmp;
	attr->toString(tmp);
	msg.addParam(attr->text(),tmp);
    }
}

bool YStunMessage::toBuffer(DataBlock& buffer) const
{
    YStunAttributeMessageIntegrity* mi = NULL;
    YStunAttributeFingerprint* fp = NULL;
    // Create attributes
    DataBlock attr_buffer;
    ObjList* obj = m_attributes.skipNull();
    for(; obj; obj = obj->skipNext()) {
	YStunAttribute* attr = static_cast<YStunAttribute*>(obj->get());
	switch(attr->type()) {
	case YStunAttribute::MessageIntegrity:
	    (mi = static_cast<YStunAttributeMessageIntegrity*>(attr))->m_pos = STUN_MSG_HEADERLENGTH + attr_buffer.length();
	    break;
	case YStunAttribute::Fingerprint:
	    (fp = static_cast<YStunAttributeFingerprint*>(attr))->m_pos = STUN_MSG_HEADERLENGTH + attr_buffer.length();
	    break;
	default:
	    break;
	}
	attr->toBuffer(attr_buffer);
	size_t padding = attr_buffer.length() % 4;
	if (padding)
	    attr_buffer.append(const_cast<char*>("\0\0\0\0"), 4 - padding);
    }
    // Set message buffer
    u_int8_t header[4];
    setHeader(header,m_type,attr_buffer.length());
    buffer.assign(header,sizeof(header));
    buffer.append(m_id);
    buffer.append(attr_buffer);
    if (mi)
	mi->updateMsg(buffer);
    if (fp)
	fp->updateMsg(buffer);
    return true;
}

void YStunMessage::print()
{
    String id;
    id.hexify(m_id.data(), m_id.length());
    Debug(&iplugin,DebugAll,"YStunMessage [%p]. Type: '%s'. ID: '%s'.",
	this,text(),id.c_str());
    // Print attributes
    ObjList* obj = m_attributes.skipNull();
    for (; obj; obj = obj->skipNext()) {
	YStunAttribute* attr = static_cast<YStunAttribute*>(obj->get());
	String tmp;
	attr->toString(tmp);
	Debug(&iplugin,DebugAll,"YStunMessage [%p]. Attribute: %s=%s",
	    this,attr->text(),tmp.c_str());
    }
}

bool YStunMessage::checkIntegrity(const u_int8_t* data, const String& password) const
{
    const YStunAttributeMessageIntegrity* mia = static_cast<YStunAttributeMessageIntegrity*>(const_cast<YStunMessage*>(this)->getAttribute(YStunAttribute::MessageIntegrity));
    if (! mia)
	return false;
    DataBlock mac;
    YStunUtils::calcMessageIntegrity(password, data, mia->m_pos, mac);
    return mac.length() == mia->m_mac.length() && 0 == memcmp(mac.data(), mia->m_mac.data(), mac.length());
}


/**
 * YStunUtils
 */
unsigned int YStunUtils::m_id = 1;
Mutex YStunUtils::s_idMutex(true,"YStunUtils::id");

YStunUtils::YStunUtils()
{
}

bool YStunUtils::isStun(const void* data, u_int32_t len,
	YStunMessage::Type& type, bool& isRfc576)
{
// Check if received buffer is a STUN message:
//	- Length:      Greater then or equal to STUN_MSG_HEADERLENGTH
//	               Multiple of 4
//	               Match the length field of the header
//	- Type:        YStunMessage::Type
    const u_int8_t* buffer = (const u_int8_t*)data;
    // Check length
    if (!(data && len >= STUN_MSG_HEADERLENGTH && !(len % 4)))
	return false;
    u_int16_t msg_type, msg_len;
    getHeader(buffer,msg_type,msg_len);
    if(msg_type & 0xC000) // fixed message type bits
	return false;
    isRfc576 = 0 == memcmp(buffer + 4, magic_cookie.u8, sizeof(magic_cookie));

    if (msg_len != len - STUN_MSG_HEADERLENGTH)
	return false;
    if (buffer[len - 8] == 0x80 && buffer[len - 8 + 1] == 0x28) {
	u_int32_t c1 = YStunUtils::calcFingerprint(buffer, len - 8);
	u_int32_t c2 = ntohl(*(u_int32_t*)&buffer[len - 4]);
	if(c1 != c2) {
	    DDebug(&iplugin, DebugAll, "Fingerprint verification failed, calc=%08X, got=%08X", c1, c2);
	    return false;
	}
    }

    // Check type
    switch (msg_type) {
	case YStunMessage::BindReq:
	case YStunMessage::BindRsp:
	case YStunMessage::BindErr:
	case YStunMessage::SecretReq:
	case YStunMessage::SecretRsp:
	case YStunMessage::SecretErr:
	case YStunMessage::Allocate:
	case YStunMessage::Refresh:
	case YStunMessage::Send:
	case YStunMessage::Data:
	case YStunMessage::CreatePermission:
	case YStunMessage::ChannelBind:
	    break;
	default:
	    return false;
    }
    type = (YStunMessage::Type)msg_type;
    // OK: go on!
    return true;
}

YStunMessage* YStunUtils::decode(const void* data, u_int32_t len, YStunMessage::Type type)
{
    u_int8_t* buffer = (u_int8_t*)data;
    YStunMessage* msg = new YStunMessage(type, (char*)buffer + 4, STUN_MSG_IDLENGTH);
    // Get attributes
    u_int32_t i = STUN_MSG_HEADERLENGTH;
    for (; i < len;) {
	// Check if we have an attribute header
	if (i + 4 > len)
	    break;
	// Get type & length
	u_int16_t attr_type, attr_len;
	getHeader(buffer+i,attr_type,attr_len);
#ifdef XDEBUG
	Debug(&iplugin,DebugAll,"Parsing at offset %u attribute %04X (%d bytes)", i, attr_type, attr_len);
#endif
	i += 4;
	// Check if length matches
	if (i + attr_len > len)
	    break;
	// Create object
	YStunAttribute* attr = 0;
	switch (attr_type) {
	    // Addresses
	    case YStunAttribute::MappedAddress:
	    case YStunAttribute::ResponseAddress:
	    case YStunAttribute::SourceAddress:
	    case YStunAttribute::ChangedAddress:
	    case YStunAttribute::ReflectedFrom:
	    case YStunAttribute::XorMappedAddress:
		attr = new YStunAttributeAddr(attr_type);
		break;
	    // Error
	    case YStunAttribute::ErrorCode:
		attr = new YStunAttributeError(attr_type);
		break;
	    // Flags
	    case YStunAttribute::ChangeRequest:
		attr = new YStunAttributeChangeReq();
		break;
	    // Auth
	    case YStunAttribute::Username:
	    case YStunAttribute::Password:
		attr = new YStunAttributeAuth(attr_type);
		break;
	    // Message Integrity
	    case YStunAttribute::MessageIntegrity:
		attr = new YStunAttributeMessageIntegrity();
		static_cast<YStunAttributeMessageIntegrity*>(attr)->m_pos = i - 4; // remember attribute offset
		break;
	    case YStunAttribute::UseCandidate:
		attr = new YStunAttributeUseCandidate();
		break;
	    case YStunAttribute::UnknownAttributes:
	    case YStunAttribute::Realm:
	    case YStunAttribute::Nonce:
		attr = new YStunAttributeUnknown(attr_type);
		break;
	    default:
		attr = new YStunAttributeUnknown(attr_type);
	}
	// Parse attribute. Add on success
	if (!attr->fromBuffer(buffer + i,attr_len)) {
	    attr->deref();
	    break;
	}
	msg->addAttribute(attr);
	// Skip processed data
	i += attr_len;
	// Skip padding
	i += (4 - (i % 4)) % 4;
    }
    if (i < len) {
	DDebug(&iplugin,DebugWarn,"Error parsing attribute at packet offset %u", i);
	msg->deref();
	return 0;
    }
    return msg;
}

void YStunUtils::createId(String& id)
{
    id = "";
    s_idMutex.lock();
    id << m_id++ << "_";
    s_idMutex.unlock();
    for (; id.length() < STUN_MSG_IDLENGTH;)
	id << (int)Random::random();
    id = id.substr(0,STUN_MSG_IDLENGTH);
}

void YStunUtils::createId(DataBlock& id)
{
    String s;
    createId(s);
    id.assign(const_cast<char*>(s.c_str()), s.length());
}

bool YStunUtils::sendMessage(Socket* socket, const YStunMessage* msg,
	const SocketAddr& addr, void* sender)
{
    if (!(socket && msg))
	return false;
    DDebug(&iplugin,DebugAll,"Send message ('%s') to '%s:%d'. [%p]",
	msg->text(),addr.host().c_str(),addr.port(),sender);
    DataBlock buffer;
    msg->toBuffer(buffer);
    int result = socket->sendTo(buffer.data(),buffer.length(),addr);
    if (result != Socket::socketError())
	return true;
    if (!socket->canRetry())
	Debug(&iplugin,DebugWarn,"Socket write error: '%s' (%d). [%p]",
	    ::strerror(socket->error()),socket->error(),sender);
#ifdef DEBUG
    else
	Debug(&iplugin,DebugMild,"Socket temporary unavailable: '%s' (%d). [%p]",
	    ::strerror(socket->error()),socket->error(),sender);
#endif
    return false;
}

bool YStunUtils::getAttrError(YStunMessage* msg, String& errStr)
{
    if (!msg)
	return false;
    YStunAttributeError* errAttr = static_cast<YStunAttributeError*>
	(msg->getAttribute(YStunAttribute::ErrorCode));
    if (!errAttr)
	return false;
    errAttr->toString(errStr);
    return true;
}

bool YStunUtils::getAttrAddr(YStunMessage* msg, YStunAttribute::Type type,
	String& addr)
{
    if (!msg)
	return false;
    YStunAttributeAddr* attr = 0;
    switch (type) {
	case YStunAttribute::MappedAddress:
	case YStunAttribute::ResponseAddress:
	case YStunAttribute::SourceAddress:
	case YStunAttribute::ChangedAddress:
	case YStunAttribute::ReflectedFrom:
	    attr = static_cast<YStunAttributeAddr*>(msg->getAttribute(type));
	    break;
	default:
	    return false;
    }
    if (!attr)
	return false;
    attr->toString(addr);
    return true;
}

bool YStunUtils::getAttrAuth(YStunMessage* msg, YStunAttribute::Type type,
	String& auth)
{
    if (!msg)
	return false;
    YStunAttributeAuth* attr = 0;
    switch (type) {
	case YStunAttribute::Username:
	case YStunAttribute::Password:
	    attr = static_cast<YStunAttributeAuth*>(msg->getAttribute(type));
	    break;
	default:
	    return false;
    }
    if (!attr)
	return false;
    attr->toString(auth);
    return true;
}

u_int32_t YStunUtils::calcFingerprint(const void * data, u_int32_t fppos)
{
    static CRC32 crc32;
    u_int32_t c = crc32((u_int8_t*)data, fppos /*len - 8*/);
    return c ^ 0x5354554e;
}

bool YStunUtils::calcMessageIntegrity(const String& password, const u_int8_t * data, u_int32_t m_i_attr_pos, DataBlock& result)
{
#ifdef XDEBUG
    String d;
    d.hexify(const_cast<u_int8_t*>(data), m_i_attr_pos);
    Debug(&iplugin,DebugAll,"calcMessageIntegrity(%s, %s, %d)",password.c_str(), d.c_str(), m_i_attr_pos);
#endif
    DataBlock key;
    key += password;

    u_int16_t msg_type, msg_len;
    getHeader(data, msg_type, msg_len);
    u_int8_t fake_header[4];
    setHeader(fake_header, msg_type, m_i_attr_pos + STUN_ATTR_MI_LENGTH - STUN_MSG_HEADERLENGTH);

    SHA1 h;
    DataBlock pad;
    if (! h.hmacStart(pad, key))
	return false;
    if (! h.update(fake_header, sizeof(fake_header)))
	return false;
    if (! h.update(data + sizeof(fake_header), m_i_attr_pos - sizeof(fake_header)))
	return false;
    if (! h.hmacFinal(pad))
	return false;
    result.assign(const_cast<u_int8_t*>(h.rawDigest()), h.hashLength());

#ifdef XDEBUG
    d.hexify(const_cast<u_int8_t*>(h.rawDigest()), h.hashLength());
    Debug(&iplugin,DebugAll,"calcMessageIntegrity: %s", d.c_str());
#endif
    return true;
}

/**
 * YStunMessageOut
 */
YStunMessageOut::YStunMessageOut(YStunMessage* msg, const SocketAddr addr,
	void* sender)
    : m_msg(msg),
      m_addr(addr),
      m_sender(sender),
      m_count(STUN_RETRANS_COUNT),
      m_interval(STUN_RETRANS_INTERVAL),
      m_next(0)
{
}

YStunMessageOut::~YStunMessageOut()
{
    if (m_msg)
	m_msg->deref();
}

void YStunMessageOut::reset(const SocketAddr& addr)
{
    m_addr = addr;
    m_count = STUN_RETRANS_COUNT;
    m_interval = STUN_RETRANS_INTERVAL;
    m_next = 0;
}

/**
 * YStunSocketFilter
 */
YStunSocketFilter::YStunSocketFilter()
    : SocketFilter(),
      m_remoteAddr(AF_INET),
      m_useLocalUsername(false),
      m_useRemoteUsername(false),
      m_bindReq(0),
      m_bindReqMutex(true,"YStunSocketFilter::bindReq"),
      m_bindReqNext(0),
      m_notFound(true),
      m_rfc5389(false),
      m_passive(false)
{
    DDebug(&iplugin,DebugAll,"YStunSocketFilter. [%p]",this);
    for (; m_security.length() < FILTER_SECURITYLENGTH; )
	m_security << (int)Random::random();
    m_security = m_security.substr(0,FILTER_SECURITYLENGTH);
}

YStunSocketFilter::~YStunSocketFilter()
{
    DDebug(&iplugin,DebugAll,"~YStunSocketFilter. [%p]",this);
}

bool YStunSocketFilter::received(void* buffer, int length, int flags,
	const struct sockaddr* addr, socklen_t addrlen)
{
    YStunMessage* msg = NULL;
    YStunMessage::Type type;
    bool rfc5389;
    bool ok = YStunUtils::isStun(buffer, length, type, rfc5389);
    if (!ok || (m_rfc5389 && !rfc5389)) {
#ifdef XDEBUG
	SocketAddr tmp(addr,addrlen);
	Debug(&iplugin,DebugAll,"Non-STUN from '%s:%d' length %d [%p]",
	    tmp.host().c_str(),tmp.port(),length,this);
#endif
	return false;
    }
    else
	msg = YStunUtils::decode(buffer,length,type);

    switch(msg->type()) {
	case YStunMessage::BindReq:
	    if (msg && !m_localPassword.null() && !msg->checkIntegrity((u_int8_t*)buffer, m_localPassword)) {
		Debug(&iplugin,DebugInfo, "Filter ignoring message - failed integrity check. [%p]", this);
		msg->deref();
		return true;
	    }
	    break;
	case YStunMessage::BindRsp:
	    if (msg && !m_remotePassword.null() && !msg->checkIntegrity((u_int8_t*)buffer, m_remotePassword)) {
		Debug(&iplugin,DebugInfo, "Filter ignoring message - failed integrity check. [%p]", this);
		msg->deref();
		return true;
	    }
	    break;
	default:
	    break;
    }

    if (msg) {
	SocketAddr tmp(addr,addrlen);
	if (m_remoteAddr != tmp) {
	    if (m_notFound) {
		Debug(&iplugin,DebugNote,
		    "Filter remote address changed from '%s:%d' to '%s:%d'. [%p]",
		    m_remoteAddr.host().c_str(),m_remoteAddr.port(),
		    tmp.host().c_str(),tmp.port(),this);
		m_remoteAddr = tmp;
		// Remote address changed: reset bind request
		m_bindReqMutex.lock();
		if (m_bindReq)
		    m_bindReq->reset(m_remoteAddr);
		else
		    timerTick(Time());
		m_bindReqMutex.unlock();
	    }
	    else {
		Debug(&iplugin,DebugInfo,
		    "Filter ignoring message from invalid address '%s:%d'. [%p]",
		    tmp.host().c_str(),tmp.port(),this);
		msg->deref();
		return true;
	    }
	}
	processMessage(msg);
    }
    return true;
}

bool YStunSocketFilter::install(Socket* sock, const Message* msg)
{
    if (socket() || !(sock && msg))
	return false;
    // Set data
    m_localUsername = msg->getValue("localusername");
    m_useLocalUsername = msg->getBoolValue("uselocalusername", !m_localUsername.null());
    m_remoteUsername = msg->getValue("remoteusername");
    m_useRemoteUsername = msg->getBoolValue("useremoteusername", !m_remoteUsername.null());
    m_remoteAddr.host(msg->getValue("remoteip"));
    m_remoteAddr.port(msg->getIntValue("remoteport"));
    m_userId = msg->getValue("userid");
    m_rfc5389 = msg->getBoolValue("rfc5389");
    m_passive = msg->getBoolValue("passive", m_rfc5389);
    if(m_rfc5389) {
	m_localPassword = msg->getValue("localpassword");
	m_remotePassword = msg->getValue("remotepassword");
    }
    // Install
    if (!sock->installFilter(this)) {
	Debug(&iplugin,DebugGoOn,
	    "Error installing filter for '%s'. [%p]",
	    m_userId.c_str(),this);
	return false;
    }
    DDebug(&iplugin,DebugAll,
	"Filter installed for '%s'. [%p]",
	m_userId.c_str(),this);
    // Send first bind request
    Time when;
    timerTick(when);
    return true;
}

void YStunSocketFilter::timerTick(const Time& when)
{
    if (m_passive)
	return;
    if (m_rfc5389 && !(m_localUsername && m_remoteUsername && m_remotePassword))
	return;
    u_int64_t time = when.msec();
    Lock lock(m_bindReqMutex);
    // Send another request ?
    if (!m_bindReq) {
	// It's time to send ?
	if (time < m_bindReqNext)
	    return;
	DataBlock id;
	YStunUtils::createId(id);
	id.resize(FILTER_SECURITYLENGTH);
	id.append(m_security);
	if (m_rfc5389)
	    memcpy(id.data(), magic_cookie.u8, sizeof(magic_cookie));
	YStunMessage* req = new YStunMessage(YStunMessage::BindReq,id.data(), id.length());
	if (m_rfc5389) {
	    req->addAttribute(new YStunAttributeAuth(m_remoteUsername + ":" + m_localUsername));
	    // TODO: priority && ice-controlled
	    req->addAttribute(new YStunAttributeSoftware(iplugin.software()));
	    req->addAttribute(new YStunAttributeMessageIntegrity(m_remotePassword));
	    req->addAttribute(new YStunAttributeFingerprint);
	}
	else if (m_useLocalUsername)
	    req->addAttribute(new YStunAttributeAuth(m_localUsername));
	m_bindReq = new YStunMessageOut(req,m_remoteAddr,this);
	m_bindReq->send(socket(),time);
	m_bindReqNext = time + iplugin.bindInterval();
	return;
    }
    // We have a pending request
    // Time to send ?
    if (!m_bindReq->timeToSend(time))
	return;
    // Timeout or resend
    if (m_bindReq->timeout()) {
	processBindResult(0);
	m_bindReq->deref();
	m_bindReq = 0;
    }
    else
	m_bindReq->send(socket(),time);
    m_bindReqNext = time + iplugin.bindInterval();
}

bool YStunSocketFilter::processMessage(YStunMessage* msg)
{
    if (!msg)
	return false;
    String id;
    id.hexify(msg->id().data(), msg->id().length());
    Debug(&iplugin,DebugAll,
	"Filter received %s (%p) from '%s:%d'. Id: '%s'. [%p]",
	msg->text(),msg,m_remoteAddr.host().c_str(),
	m_remoteAddr.port(),id.c_str(),this);
    switch(msg->type()) {
	case YStunMessage::BindReq:
	    processBindReq(msg);
	    break;
	case YStunMessage::BindRsp:
	case YStunMessage::BindErr:
	    m_bindReqMutex.lock();
	    if (m_bindReq && m_bindReq->isId(msg)) {
		processBindResult(msg);
		m_bindReqMutex.unlock();
		break;
	    }
	    m_bindReqMutex.unlock();
	    DDebug(&iplugin,DebugNote,
		"Filter: (%p) is a response to a non existing request. [%p]",
		msg,this);
	    break;
	default:
	    Debug(&iplugin,DebugNote,
		"Filter got unexpected message (%p). [%p]",msg,this);
    }
    msg->deref();
    return true;
}

void YStunSocketFilter::processBindReq(YStunMessage* msg)
{
    YStunMessage::Type response = YStunMessage::BindRsp;
    String username;
    // Check username
    if (m_rfc5389) {
	if (m_useLocalUsername &&
	    (!YStunUtils::getAttrAuth(msg,YStunAttribute::Username,username) ||
	    ! username.startSkip(m_localUsername + ":", false)))
	    response = YStunMessage::BindErr;
	else if (m_useRemoteUsername && username != m_remoteUsername)
	    response = YStunMessage::BindErr;
    }
    else if (m_useRemoteUsername &&
	(!YStunUtils::getAttrAuth(msg,YStunAttribute::Username,username) ||
	username != m_remoteUsername))
	response = YStunMessage::BindErr;
    // Create response
    YStunMessage* rspMsg = new YStunMessage(response, msg->id().data(), msg->id().length());
    if(! m_rfc5389) // in fact, this attribute should not be added anyway, but leave it here to be backward-compatible
	rspMsg->addAttribute(new YStunAttributeAuth(username));
    rspMsg->addAttribute(new YStunAttributeSoftware(iplugin.software()));
    if (response == YStunMessage::BindErr) {
	Debug(&iplugin,DebugInfo,
	    "Filter: Bind request (%p) has invalid username. Expected %s:%s [%p]",msg,m_localUsername.c_str(),m_remoteUsername.c_str(),this);
	// Add error attribute
	rspMsg->addAttribute(new YStunAttributeError(YStunError::Auth,
	    lookup(YStunError::Auth,YStunError::s_tokens)));
    }
    else {
	if (m_notFound && msg->getAttribute(YStunAttribute::UseCandidate)) {
	    Debug(&iplugin, DebugInfo, "Got valid bind request with USE-CANDIDATE attribute, updating rtp %s address to %s", m_userId.c_str(), m_remoteAddr.addr().c_str());
	    m_notFound = false;
	    dispatchChanRtp();
	}
	// Add mapped address attribute
	rspMsg->addAttribute(new YStunAttributeAddr(
	    m_rfc5389
		? YStunAttribute::XorMappedAddress
		: YStunAttribute::MappedAddress,
	    m_remoteAddr.host(), m_remoteAddr.port()));
	if(m_rfc5389) {
	    rspMsg->addAttribute(new YStunAttributeMessageIntegrity(m_localPassword));
	    rspMsg->addAttribute(new YStunAttributeFingerprint);
	}
    }
    YStunUtils::sendMessage(socket(),rspMsg,m_remoteAddr,this);
    rspMsg->deref();
}

void YStunSocketFilter::processBindResult(YStunMessage* msg)
{
    // msg is 0: timeout
    if (!msg) {
	Debug(&iplugin,DebugNote,
	    "Filter: Bind request to '%s:%d' timed out. [%p]",
	    m_bindReq->m_addr.host().c_str(),m_bindReq->m_addr.port(),this);
	// TODO: WHAT ????
	return;
    }
    // Check username
    if (m_useLocalUsername) {
	String username;
	YStunUtils::getAttrAuth(msg,YStunAttribute::Username,username);
	if (username != m_localUsername) {
	    Debug(&iplugin,DebugInfo,
		"Filter: Bind response with bad username from '%s:%d'. We expect '%s' and received '%s'. [%p]",
		m_remoteAddr.host().c_str(),m_remoteAddr.port(),m_remoteUsername.c_str(),username.c_str(),this);
	}
	// Authenticated: notify RTP chan
	else if (m_notFound) {
	    Debug(&iplugin,DebugNote,
		"Filter: Response authenticated for '%s:%d' - notifying RTP. [%p]",
		m_remoteAddr.host().c_str(),m_remoteAddr.port(),this);
	    m_notFound = false;
	    dispatchChanRtp();
	}
    }
    if (msg->type() == YStunMessage::BindRsp) {
	// Mandatory: MappedAddress, SourceAddress, ChangedAddress
	// Conditional: ReflectedFrom
	// Optional: MessageIntegrity
	String mapped;
	if (YStunUtils::getAttrAddr(msg,YStunAttribute::MappedAddress,mapped))
	    DDebug(&iplugin,DebugAll,
		"Filter mapped address: '%s'. [%p]",mapped.c_str(),this);
	else
	    Debug(&iplugin,DebugAll,
		"Filter: Invalid message: No MAPPED-ADDRESS attribute. [%p]",
		this);
    }
    else if (msg->type() == YStunMessage::BindErr) {
	// Mandatory: ErrorCode
	// Conditional: UnknownAttributes
	String errStr;
	if (YStunUtils::getAttrError(msg,errStr))
	    Debug(&iplugin,DebugAll,
		"Filter: Received error: '%s'. [%p]",errStr.c_str(),this);
	else
	    Debug(&iplugin,DebugAll,
		"Filter: Invalid message (%p): No ERROR-CODE attribute. [%p]",
		msg,this);
    }
    else
	return;
    // Result is: error, response
    // TODO: WHAT ????
    // Remove request
    m_bindReq->deref();
    m_bindReq = 0;
}

void YStunSocketFilter::dispatchChanRtp()
{
    Message* m = new Message("chan.rtp");
    m->addParam("direction","bidir");
    m->addParam("remoteip",m_remoteAddr.host());
    m->addParam("remoteport",String(m_remoteAddr.port()));
    m->addParam("rtpid",m_userId);
    Engine::enqueue(m);
}

/**
 * StunHandler
 */
bool StunHandler::received(Message& msg)
{
    Socket* socket = static_cast<Socket*>(msg.userObject(YATOM("Socket")));
    if (!socket) {
	Debug(&iplugin,DebugGoOn,"StunHandler: No socket to install filter for.");
	return true;
    }
    YStunSocketFilter* filter = new YStunSocketFilter();
    if (!filter->install(socket,&msg))
	filter->destruct();
    return true;
}

/**
 * YStunListener
 */
YStunListener::YStunListener(const String& name, Thread::Priority prio)
    : Thread("YStunListener", prio)
    , m_name(name)
    , m_sock(NULL)
    , m_maxpkt(1500)
{
}

YStunListener::~YStunListener()
{
    if(m_sock)
	m_sock->setLinger(-1);
    delete m_sock;
    Lock lck(iplugin.m_mutex);
    iplugin.m_listeners.remove(this, false);
}

void YStunListener::init(const NamedList& params)
{
    String addr = params.getValue("addr", "0.0.0.0");
    int port = params.getIntValue("port", 3478);

    m_addr.assign(SocketAddr::IPv4);
    if (addr && !m_addr.host(addr)) {
	Debug(&iplugin,DebugConf,"Invalid address '%s' configured", addr.c_str());
	return;
    }
    m_addr.port(port);
    m_sock = new Socket(m_addr.family(), SOCK_DGRAM, IPPROTO_UDP);
    if (!m_sock->valid()) {
	Debug(&iplugin,DebugWarn,"Listener %s: Create socket failed (%s:%d)", m_name.c_str(), addr.c_str(), port);
	return;
    }
#if 0
    if (!udp)
	sock->setReuse();
#endif
    bool ok = m_sock->bind(m_addr);
    if (!ok) {
	Debug(&iplugin,DebugWarn,"Listener %s: Socket bind failed (%s:%d)", m_name.c_str(), addr.c_str(), port);
	return;
    }
    if (!m_sock->setBlocking(false)) {
	Debug(&iplugin,DebugWarn,"Listener %s: Failed to set non-blocking mode (%s:%d)", m_name.c_str(), addr.c_str(), port);
	return;
    }
#if 0
    if (!udp && !m_sock->listen(backLogBuffer)) {
	Debug(&iplugin,DebugWarn,"Listener %s: Socket listen failed (%s:%d)", m_name.c_str(), addr.c_str(), port);
	break;
    }
#endif

    startup();
}

void YStunListener::run()
{
    DataBlock buffer;                  // Read buffer
    DDebug(&iplugin,DebugAll,"Listener %s start running [%p]", m_name.c_str(), this);
    while (true) {
	if (Thread::check(false))
	    break;

	if (m_sock->canSelect()) {
	    bool ok = false;
	    if (m_sock->select(&ok,0,0,Thread::idleUsec())) {
		if (!ok)
		    continue;
	    }
	    else {
		// Select failed
		if (! m_sock->canRetry()) {
		    String tmp;
		    Thread::errorString(tmp,m_sock->error());
		    Debug(&iplugin,DebugWarn,"Listener %s: select failed: %d '%s' [%p]",
			m_name.c_str(), m_sock->error(), tmp.c_str(), this);
		}
	    }
	}

	buffer.resize(m_maxpkt);
	SocketAddr remote;
	int res = m_sock->recvFrom((void*)buffer.data(), buffer.length() - 1, remote);
	if (res <= 0) {
	    Thread::usleep(Thread::idleUsec());
	    continue;
	}
	buffer.truncate(res);

	DDebug(&iplugin, DebugAll, "Listener %s got %d bytes packet from %s:%d [%p]", m_name.c_str(), res, remote.host().c_str(), remote.port(), this);
#ifdef XDEBUG
	String tmp;
	tmp.hexify(buffer.data(), buffer.length());
	Debug(&iplugin, DebugAll, "Packet content: %s", tmp.c_str());
#endif
	bool ok = received(buffer, remote);
	if(! ok) {
	    String tmp;
	    tmp.hexify(buffer.data(), buffer.length());
	    Debug(&iplugin, DebugWarn, "Listener %s got invalid %d bytes packet from %s:%d: %s [%p]", m_name.c_str(), buffer.length(), remote.host().c_str(), remote.port(), tmp.c_str(), this);
	}
    }
}

bool YStunListener::received(DataBlock& pkt, const SocketAddr& remote)
{
    bool rfc5389;
    YStunMessage::Type type;
    if (! YStunUtils::isStun(pkt.data(), pkt.length(), type, rfc5389))
	return false;

    // Looks like a real STUN message

    if (type != YStunMessage::BindReq) // Process only bind requests
	return false;

    YStunMessage* msg = YStunUtils::decode(pkt.data(), pkt.length(), type);
    if (! msg)
	return false;

    String id;
    id.hexify(msg->id().data(), msg->id().length());
    Debug(&iplugin,DebugAll,
	"Listener %s received BindReq %s (%p) from '%s:%d'. Id: '%s'. [%p]",
	m_name.c_str(), msg->text(), msg, remote.host().c_str(),
	remote.port(), id.c_str(), this);

    // Create response
    YStunMessage* rspMsg = new YStunMessage(YStunMessage::BindRsp, msg->id().data(), msg->id().length());
    rspMsg->addAttribute(new YStunAttributeSoftware(iplugin.software()));
    rspMsg->addAttribute(new YStunAttributeAddr(
	rfc5389
	    ? YStunAttribute::XorMappedAddress
	    : YStunAttribute::MappedAddress,
	remote.host(), remote.port()));
    YStunUtils::sendMessage(m_sock, rspMsg, remote, this);
    rspMsg->deref();
    msg->deref();
    return true;
}


/**
 * YStunPlugin
 */
YStunPlugin::YStunPlugin()
    : Module("stun","misc"),
      m_bindInterval(STUN_BINDINTERVAL)
{
    Output("Loaded module YSTUN");
}

YStunPlugin::~YStunPlugin()
{
    Output("Unloading module YSTUN");
}

void YStunPlugin::initialize()
{
    static bool notFirst = false;
    Output("Initializing module YSTUN");
    // Load configuration
    s_cfg = Engine::configFile("ystunchan");
    s_cfg.load();
    // Bind request interval
    m_bindInterval = s_cfg.getIntValue("filters","bindrequest_interval",
	STUN_BINDINTERVAL);
    if (m_bindInterval < STUN_BINDINTERVAL_MIN)
	m_bindInterval = STUN_BINDINTERVAL_MIN;
    else if (m_bindInterval > STUN_BINDINTERVAL_MAX)
	m_bindInterval = STUN_BINDINTERVAL_MAX;
    Debug(this,DebugAll,"Bind request interval set to %u msec.",
	m_bindInterval);

    m_software = s_cfg.getValue("general", "software", "YATE/" YATE_VERSION);

    // Do the first time jobs
    if (notFirst)
	return;
    notFirst = true;
    installRelay(Stop,"engine.stop");
    // Install message handlers
    Engine::install(new StunHandler);
    // Setup listeners
    unsigned int n = s_cfg.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* nl = s_cfg.getSection(i);
	String name = nl ? nl->c_str() : "";
	if (!name.startSkip("listener ",false))
	    continue;
	name.trimBlanks();
	if (name)
	    setupListener(name, *nl);
    }
    setup();
}

bool YStunPlugin::received(Message& msg, int id)
{
    switch(id) {
    case Stop:
	cancelAllListeners();
	return true;
    }
    return Module::received(msg,id);
}

void YStunPlugin::cancelAllListeners()
{
    {
	Lock lck(m_mutex);
	for (ObjList* l = m_listeners.skipNull(); l; l = l->skipNext()) {
	    YStunListener* t = static_cast<YStunListener*>(l->get());
	    if(! t)
		continue;
	    t->cancel();
	}
    }
    while (true) {
	Thread::idle();
	Lock lck(m_mutex);
	if (! m_listeners.skipNull())
	    break;
    }
}

void YStunPlugin::setupListener(const String& name, const NamedList& params)
{
    bool enabled = params.getBoolValue(YSTRING("enable"),true);
    if (!enabled)
	return;

    const String& type = params[YSTRING("type")];
    if(type != "udp") {
	Debug(this,DebugConf,"Invalid listener type '%s' in section '%s': defaults to %s",
	    type.c_str(), params.c_str(), "udp");
    }

    Lock lock(m_mutex);
    YStunListener* sl = new YStunListener(name, Thread::priority(params.getValue("thread")));
    sl->init(params);
    if (sl->startup()) {
	m_listeners.append(sl);
	Debug(&iplugin,DebugNote, "Added listener %p '%s' at %s", sl, sl->toString().c_str(), sl->addr().c_str());
    }
    else
	Alarm(&iplugin,"config",DebugWarn,"Failed to start listener thread name='%s'", name.c_str());
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
