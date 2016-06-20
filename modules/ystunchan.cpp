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

  The message's userdata must be a RefObject with the socket to filter

*/

class YStunError;                        // STUN errors
class YStunAttribute;                    // Message attributes
class YStunAttributeError;               //    ERROR-CODE
class YStunAttributeChangeReq;           //    CHANGE-REQUEST
class YStunAttributeAuth;                //    USERNAME, PASSWORD
class YStunAttributeAddr;                //    MAPPED-ADDRESS, RESPONSE-ADDRESS,
                                         //    SOURCE-ADDRESS, CHANGED-ADDRESS, REFLECTED-FROM
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

/**
 * YStunError
 */
class YStunError
{
public:
    enum Type {
	BadReq = 144,                    // BAD REQUEST
	Auth   = 174,                    // STALE CREDENDIALS
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
	ResponseAddress =     0x0002,    // RESPONSE-ADDRESS
	ChangeRequest =       0x0003,    // CHANGE-REQUEST
	SourceAddress =       0x0004,    // SOURCE-ADDRESS
	ChangedAddress =      0x0005,    // CHANGED-ADDRESS
	Username =            0x0006,    // USERNAME
	Password =            0x0007,    // PASSWORD
	MessageIntegrity =    0x0008,    // MESSAGE-INTEGRITY
	ErrorCode =           0x0009,    // ERROR-CODE
	UnknownAttributes =   0x000a,    // UNKNOWN-ATTRIBUTES
	ReflectedFrom =       0x000b,    // REFLECTED-FROM

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
	BindRsp =   0x0101,              // Binding Response
	BindErr =   0x0111,              // Binding Error Response
	SecretReq = 0x0002,              // Shared Secret Request
	SecretRsp = 0x0102,              // Shared Secret Response
	SecretErr = 0x0112,              // Shared Secret Error Response
    };
    YStunMessage(Type type, const char* id = 0);
    virtual ~YStunMessage() {}
    inline Type type() const
	{ return m_type; }
    inline const String& id() const
	{ return m_id; }
    const char* text() const
	{ return lookup(m_type,s_tokens); }
    inline void addAttribute(YStunAttribute* attr)
	{ m_attributes.append(attr); }
    YStunAttribute* getAttribute(u_int16_t attrType, bool remove = false);
    void toMessage(Message& msg) const;
    bool toBuffer(DataBlock& buffer) const;
    void print();
    static TokenDict s_tokens[];
private:
    Type m_type;                         // Message type
    String m_id;                         // Message id
    ObjList m_attributes;                // Message attributes
};

/**
 * YStunUtils
 */
class YStunUtils
{
public:
    YStunUtils();
    static bool isStun(const void* data, u_int32_t len, YStunMessage::Type& type);
    static YStunMessage* decode(const void* data, u_int32_t len, bool& isStun);
    // Create an id used to send a binding request
    static void createId(String& id);
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
	    return msg->id().endsWith(m_msg->id().c_str() + FILTER_SECURITYLENGTH);
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
};

/**
 * YStunPlugin
 */
class YStunPlugin : public Module
{
public:
    YStunPlugin();
    virtual ~YStunPlugin();
    virtual void initialize();

    inline u_int64_t bindInterval()
	{ return m_bindInterval; }
private:
    u_int32_t m_bindInterval;            // Bind request interval
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
	0,0,(u_int8_t)(m_code / 100),(u_int8_t)(m_code % 100)};
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
	(u_int8_t)(m_flags >> 24),(u_int8_t)(m_flags >> 16),
	(u_int8_t)(m_flags >> 8),(u_int8_t)m_flags};
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
    if (!(buffer && (len % 4) == 0))
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
    m_port = buffer[2] << 8 | buffer[3];
    m_addr = "";
    m_addr << buffer[4] << "." << buffer[5] << "." << buffer[6] << "."
	<< buffer[7];
    return true;
}

void YStunAttributeAddr::toBuffer(DataBlock& buffer)
{
    u_int8_t header[12] = {0,0,0,0,
	0,STUN_ATTR_ADDR_IPV4,(u_int8_t)(m_port >> 8),(u_int8_t)m_port,
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
    DataBlock tmp(header,sizeof(header));
    buffer += tmp;
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
	{0,0}
	};

YStunMessage::YStunMessage(Type type, const char* id)
    : m_type(type),
      m_id(id)
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
    msg.addParam("message_type",text());
    msg.addParam("message_id",m_id);
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
    // Create attributes
    DataBlock attr_buffer;
    ObjList* obj = m_attributes.skipNull();
    for(; obj; obj = obj->skipNext()) {
	YStunAttribute* attr = static_cast<YStunAttribute*>(obj->get());
	attr->toBuffer(attr_buffer);
    }
    // Set message buffer
    u_int8_t header[4];
    setHeader(header,m_type,attr_buffer.length());
    buffer.assign(header,sizeof(header));
    buffer.append(m_id);
    buffer.append(attr_buffer);
    return true;
}

void YStunMessage::print()
{
    Debug(&iplugin,DebugAll,"YStunMessage [%p]. Type: '%s'. ID: '%s'.",
	this,text(),m_id.c_str());
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

/**
 * YStunUtils
 */
unsigned int YStunUtils::m_id = 1;
Mutex YStunUtils::s_idMutex(true,"YStunUtils::id");

YStunUtils::YStunUtils()
{
}

bool YStunUtils::isStun(const void* data, u_int32_t len,
	YStunMessage::Type& type)
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
    //TODO: Check if the message contains a message integrity attribute: Size is different
    if (msg_len != len - STUN_MSG_HEADERLENGTH)
	return false;
    // Check type
    switch (msg_type) {
	case YStunMessage::BindReq:
	case YStunMessage::BindRsp:
	case YStunMessage::BindErr:
	case YStunMessage::SecretReq:
	case YStunMessage::SecretRsp:
	case YStunMessage::SecretErr:
	    break;
	default:
	    return false;
    }
    type = (YStunMessage::Type)msg_type;
    // OK: go on!
    return true;
}

YStunMessage* YStunUtils::decode(const void* data, u_int32_t len, bool& isStun)
{
    u_int8_t* buffer = (u_int8_t*)data;
    YStunMessage::Type type;
    isStun = YStunUtils::isStun(data,len,type);
    if (!isStun)
	return 0;
    // Get ID
    String id((const char*)buffer + 4,STUN_MSG_IDLENGTH);
    YStunMessage* msg = new YStunMessage(type,id);
    // Get attributes
    u_int32_t i = STUN_MSG_HEADERLENGTH;
    for (; i < len;) {
	// Check if we have an attribute header
	if (i + 4 > len)
	    break;
	// Get type & length
	u_int16_t attr_type, attr_len;
	getHeader(buffer+i,attr_type,attr_len);
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
	    //
	    case YStunAttribute::MessageIntegrity:
	    case YStunAttribute::UnknownAttributes:
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
    }
    if (i < len) {
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
      m_notFound(true)
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
    bool isStun = false;
    YStunMessage* msg = YStunUtils::decode(buffer,length,isStun);
    if (!isStun) {
#ifdef XDEBUG
	SocketAddr tmp(addr,addrlen);
	Debug(&iplugin,DebugAll,"Non-STUN from '%s:%d' length %d [%p]",
	    tmp.host().c_str(),tmp.port(),length,this);
#endif
	return false;
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
    m_useLocalUsername = msg->getBoolValue("uselocalusername",true);
    m_localUsername = msg->getValue("localusername");
    m_useRemoteUsername = msg->getBoolValue("useremoteusername",true);
    m_remoteUsername = msg->getValue("remoteusername");
    m_remoteAddr.host(msg->getValue("remoteip"));
    m_remoteAddr.port(msg->getIntValue("remoteport"));
    m_userId = msg->getValue("userid");
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
    u_int64_t time = when.msec();
    Lock lock(m_bindReqMutex);
    // Send another request ?
    if (!m_bindReq) {
	// It's time to send ?
	if (time < m_bindReqNext)
	    return;
	String id;
	YStunUtils::createId(id);
	id = id.substr(0,FILTER_SECURITYLENGTH) + m_security;
	YStunMessage* req = new YStunMessage(YStunMessage::BindReq,id);
	if (m_useLocalUsername)
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
    DDebug(&iplugin,DebugAll,
	"Filter received %s (%p) from '%s:%d'. Id: '%s'. [%p]",
	msg->text(),msg,m_remoteAddr.host().c_str(),
	m_remoteAddr.port(),msg->id().c_str(),this);
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
    if (m_useRemoteUsername &&
	(!YStunUtils::getAttrAuth(msg,YStunAttribute::Username,username) ||
	username != m_remoteUsername))
	response = YStunMessage::BindErr;
    // Create response
    YStunMessage* rspMsg = new YStunMessage(response,msg->id());
    rspMsg->addAttribute(new YStunAttributeAuth(username));
    if (response == YStunMessage::BindErr) {
	Debug(&iplugin,DebugInfo,
	    "Filter: Bind request (%p) has invalid username. [%p]",msg,this);
	// Add error attribute
	rspMsg->addAttribute(new YStunAttributeError(YStunError::Auth,
	    lookup(YStunError::Auth,YStunError::s_tokens)));
    }
    else {
	// Add mapped address attribute
	rspMsg->addAttribute(new YStunAttributeAddr(
	    YStunAttribute::MappedAddress,m_remoteAddr.host(),
	    m_remoteAddr.port()));
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
	    Message* m = new Message("chan.rtp");
	    m->addParam("direction","bidir");
	    m->addParam("remoteip",m_remoteAddr.host());
	    m->addParam("remoteport",String(m_remoteAddr.port()));
	    m->addParam("rtpid",m_userId);
	    Engine::enqueue(m);
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
    // Do the first time jobs
    if (notFirst)
	return;
    notFirst = true;
    // Install message handlers
    Engine::install(new StunHandler);
    setup();
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
