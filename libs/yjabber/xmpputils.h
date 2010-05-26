/**
 * xmpputils.h
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

#ifndef __XMPPUTILS_H
#define __XMPPUTILS_H

#include <yateclass.h>
#include <yatexml.h>

#ifdef _WINDOWS

#ifdef LIBYJABBER_EXPORTS
#define YJABBER_API __declspec(dllexport)
#else
#ifndef LIBYJABBER_STATIC
#define YJABBER_API __declspec(dllimport)
#endif
#endif

#endif /* _WINDOWS */

#ifndef YJABBER_API
#define YJABBER_API
#endif

// Support old RFC 3920
// If not defined RFC 3920bis changes will be used
#define RFC3920

/**
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

class StringArray;                       // A String array
class XMPPNamespace;                     // XMPP namespaces
class XMPPError;                         // XMPP errors
class JabberID;                          // A Jabber ID (JID)
class JIDIdentity;                       // A JID's identity
class JIDIdentityList;                   // A list of JID identities
class XMPPFeature;                       // A feature (stream or JID)
class XMPPFeatureSasl;                   // A SASL feature
class XMPPFeatureList;                   // Feature list
class XMPPUtils;                         // Utilities
class XMPPDirVal;                        // Direction flags
class XmlElementOut;                     // An outgoing xml element

/**
 * This class holds a SRV record returned by a query
 * The String holds the domain/ip
 * @short A SRV record
 */
class YJABBER_API SrvRecord : public String
{
public:
    inline SrvRecord(const char* name, int port, int prio, int weight)
	: String(name), m_port(port), m_priority(prio), m_weight(weight)
	{}

    /**
     * Insert a SrvRecord into a list in the proper location
     * @param list Destination list
     * @param rec The item to insert
     */
    static void insert(ObjList& list, SrvRecord* rec);

    int m_port;
    int m_priority;
    int m_weight;
};

class YJABBER_API Resolver
{
public:
    /**
     * Make a SRV query
     * @param query The query content
     * @param result List of resulting SrvRecord items
     * @param error Optional string to be filled with error string
     * @return 0 on success, error code otherwise (h_errno value on Linux)
     */
    static int srvQuery(const char* query, ObjList& result, String* error = 0);
};


/**
 * Implements a String array set from an already allocated
 * @short A String array
 */
class YJABBER_API StringArray
{
public:
    /**
     * Constructor
     * @param array The array
     * @param len Array length
     */
    inline StringArray(const String* array, unsigned int len)
	: m_array((String*)array), m_length(len)
	{}

    /**
     * Return the string at a given index (safe)
     * @param index The index in the array
     * @return The String at the requested index or an empty one if the index is invalid
     */
    inline const String& at(unsigned int index) const
	{ return index < m_length ? m_array[index] : String::empty(); }

    /**
     * Return the string at a given index (unsafe)
     * @param index The index in the array
     * @return The String at the requested index
     */
    inline const String& operator[](unsigned int index) const
	{ return m_array[index]; }

    /**
     * Lookup for an integer associated with a given String
     * @param token The String find
     * @return Token value or 0 if not found
     */
    inline int operator[](const String& token) {
	    unsigned int i = 0;
	    for (; i < m_length; i++)
		if (m_array[i] == token)
		    return i;
	    return m_length;
	}

protected:
    String* m_array;
    unsigned int m_length;

private:
    StringArray() {}
};


/**
 * This class holds the XMPP/Jabber/Jingle namespace enumerations and the associated strings
 * @short XMPP namespaces
 */
class YJABBER_API XMPPNamespace : public StringArray
{
public:
    /**
     * Namespace type enumeration
     */
    enum Type {
	Stream = 0,                      // http://etherx.jabber.org/streams
	Client = 1,                      // jabber:client
	Server = 2,                      // jabber:server
	Dialback = 3,                    // jabber:server:dialback
	StreamError = 4,                 // urn:ietf:params:xml:ns:xmpp-streams
	StanzaError = 5,                 // urn:ietf:params:xml:ns:xmpp-stanzas
	Ping = 6,                        // urn:xmpp:ping
	Register = 7,                    // http://jabber.org/features/iq-register
	IqRegister = 8,                  // jabber:iq:register
	IqPrivate = 9,                   // jabber:iq:private
	IqAuth = 10,                     // jabber:iq:auth
	IqAuthFeature = 11,              // http://jabber.org/features/iq-auth
	IqVersion = 12,                  // jabber:iq:version
	Delay = 13,                      // urn:xmpp:delay
	Tls = 14,                        // urn:ietf:params:xml:ns:xmpp-tls
	Sasl = 15,                       // urn:ietf:params:xml:ns:xmpp-sasl
	Session = 16,                    // urn:ietf:params:xml:ns:xmpp-session
	Bind = 17,                       // urn:ietf:params:xml:ns:xmpp-bind
	Roster = 18,                     // jabber:iq:roster
	DynamicRoster = 19,              // jabber:iq:roster-dynamic
	DiscoInfo = 20,                  // http://jabber.org/protocol/disco#info
	DiscoItems = 21,                 // http://jabber.org/protocol/disco#items
	EntityCaps = 22,                 // http://jabber.org/protocol/caps
	VCard = 23,                      // vcard-temp
	SIProfileFileTransfer = 24,      // http://jabber.org/protocol/si/profile/file-transfer
	ByteStreams = 25,                // http://jabber.org/protocol/bytestreams
	Jingle = 26,                     // urn:xmpp:jingle:1
	JingleError = 27,                // urn:xmpp:jingle:errors:1
	JingleAppsRtp = 28,              // urn:xmpp:jingle:apps:rtp:1
	JingleAppsRtpError = 29,         // urn:xmpp:jingle:apps:rtp:errors:1
	JingleAppsRtpInfo = 30,          // urn:xmpp:jingle:apps:rtp:info:1
	JingleAppsRtpAudio = 31,         // urn:xmpp:jingle:apps:rtp:audio
	JingleAppsFileTransfer = 32,     // urn:xmpp:jingle:apps:file-transfer:1
	JingleTransportIceUdp = 33,      // urn:xmpp:jingle:transports:ice-udp:1
	JingleTransportRawUdp = 34,      // urn:xmpp:jingle:transports:raw-udp:1
	JingleTransportRawUdpInfo = 35,  // urn:xmpp:jingle:transports:raw-udp:info:1
	JingleTransportByteStreams = 36, // urn:xmpp:jingle:transports:bytestreams:1
	JingleTransfer = 37,             // urn:xmpp:jingle:transfer:0
	JingleDtmf = 38,                 // urn:xmpp:jingle:dtmf:0
	JingleSession = 39,              // http://www.google.com/session
	JingleAudio = 40,                // http://www.google.com/session/phone
	JingleTransport = 41,            // http://www.google.com/transport/p2p
	JingleRtpInfoOld = 42,           // urn:xmpp:jingle:apps:rtp:info
	DtmfOld = 43,                    // http://jabber.org/protocol/jingle/info/dtmf
	XOob = 44,                       // jabber:x:oob
	Command= 45,                     // http://jabber.org/protocol/command
	MsgOffline= 46,                  // msgoffline
	ComponentAccept = 47,            // jabber:component:accept
	Muc = 48,                        // http://jabber.org/protocol/muc
	MucAdmin = 49,                   // http://jabber.org/protocol/muc#admin
	MucOwner = 50,                   // http://jabber.org/protocol/muc#owner
	MucUser = 51,                    // http://jabber.org/protocol/muc#user
	DialbackFeature = 52,            // urn:xmpp:features:dialback
	Count = 53,
    };

    /**
     * Constructor
     */
    inline XMPPNamespace()
	: StringArray(s_array,Count)
	{}

private:
    static const String s_array[Count];  // Namespace list
};


/**
 * This class holds the XMPP error type, error enumerations and associated strings
 * @short XMPP errors
 */
class YJABBER_API XMPPError : public StringArray
{
public:
    /**
     * Error condition enumeration
     */
    enum Type {
	NoError = 0,
	BadFormat = 1,                   // bad-format
	BadNamespace = 2,                // bad-namespace-prefix
	Conflict = 3,                    // conflict
	ConnTimeout = 4,                 // connection-timeout
	HostGone = 5,                    // host-gone
	HostUnknown = 6,                 // host-unknown
	BadAddressing = 7,               // improper-addressing
	Internal = 8,                    // internal-server-error
	InvalidFrom = 9,                 // invalid-from
	InvalidId = 10,                  // invalid-id
	InvalidNamespace = 11,           // invalid-namespace
	InvalidXml = 12,                 // invalid-xml
	NotAuth = 13,                    // not-authorized
	Policy = 14,                     // policy-violation
	RemoteConn = 15,                 // remote-connection-failed
	ResConstraint = 16,              // resource-constraint
	RestrictedXml = 17,              // restricted-xml
	SeeOther = 18,                   // see-other-host
	Shutdown = 19,                   // system-shutdown
	UndefinedCondition = 20,         // undefined-condition
	UnsupportedEnc = 21,             // unsupported-encoding
	UnsupportedStanza = 22,          // unsupported-stanza-type
	UnsupportedVersion = 23,         // unsupported-version
	Xml = 24,                        // xml-not-well-formed
	Aborted = 25,                    // aborted
	AccountDisabled = 26,            // account-disabled
	CredentialsExpired = 27,         // credentials-expired
	EncryptionRequired = 28,         // encryption-required
	IncorrectEnc = 29,               // incorrect-encoding
	InvalidAuth = 30,                // invalid-authzid
	InvalidMechanism = 31,           // invalid-mechanism
	MalformedRequest = 32,           // malformed-request
	MechanismTooWeak = 33,           // mechanism-too-weak
	NotAuthorized = 34,              // not-authorized
	TempAuthFailure = 35,            // temporary-auth-failure
	TransitionNeeded = 36,           // transition-needed
	ResourceConstraint = 37,         // resource-constraint
	NotAllowed = 38,                 // not-allowed
	BadRequest = 39,                 // bad-request
	FeatureNotImpl = 40,             // feature-not-implemented
	Forbidden = 41,                  // forbidden
	Gone = 42,                       // gone
	ItemNotFound = 43,               // item-not-found
	BadJid = 44,                     // jid-malformed
	NotAcceptable = 45,              // not-acceptable
	Payment = 46,                    // payment-required
	Unavailable = 47,                // recipient-unavailable
	Redirect = 48,                   // redirect
	Reg = 49,                        // registration-required
	NoRemote = 50,                   // remote-server-not-found
	RemoteTimeout = 51,              // remote-server-timeout
	ServiceUnavailable = 52,         // service-unavailable
	Subscription = 53,               // subscription-required
	Request = 54,                    // unexpected-request
	SocketError = 55,                // Don't send any error or stream end tag to remote party
	TypeCount = 56
    };

    /**
     * Error type enumeration
     */
    enum ErrorType {
	TypeCancel = TypeCount,          // do not retry (the error is unrecoverable)
	TypeContinue = TypeCount + 1,    // proceed (the condition was only a warning)
	TypeModify = TypeCount + 2,      // retry after changing the data sent
	TypeAuth = TypeCount + 3,        // retry after providing credentials
	TypeWait = TypeCount + 4,        // retry after waiting (the error is temporary)
	Count = TypeCount + 5
    };

    /**
     * Constructor
     */
    inline XMPPError()
	: StringArray(s_array,Count)
	{}

private:
    static const String s_array[Count];  // Error list
};

/**
 * This class holds a list of XML tags
 * @short XML known tags array
 */
class YJABBER_API XmlTag : public StringArray
{
public:
    /**
     * Element tag enumeration
     */
    enum Type {
	Stream = 0,                      // stream
	Error = 1,                       // error
	Features = 2,                    // features
	Register = 3,                    // register
	Starttls = 4,                    // starttls
	Auth = 5,                        // auth
	Challenge = 6,                   // challenge	
	Abort = 7,                       // abort
	Aborted = 8,                     // aborted
	Response = 9,                    // response
	Proceed = 10,                    // proceed
	Success = 11,                    // success
	Failure = 12,                    // failure 
	Mechanisms = 13,                 // mechanisms
	Mechanism = 14,                  // mechanism
	Session = 15,                    // session
	Iq = 16,                         // iq
	Message = 17,                    // message
	Presence = 18,                   // presence
	Query = 19,                      // query
	VCard = 20,                      // vCard
	Jingle = 21,                     // jingle
	Description = 22,                // description
	PayloadType = 23,                // payload-type
	Transport = 24,                  // transport
	Candidate = 25,                  // candidate
	Body = 26,                       // body
	Subject = 27,                    // subject
	Feature = 28,                    // feature
	Bind = 29,                       // bind
	Resource = 30,                   // resource
	Transfer = 31,                   // transfer
	Hold = 32,                       // hold
	Active = 33,                     // active
	Ringing = 34,                    // ringing
	Mute = 35,                       // mute
	Registered = 36,                 // registered
	Remove = 37,                     // remove
	Jid = 38,                        // jid
	Username = 39,                   // username
	Password = 40,                   // password
	Digest = 41,                     // digest
	Required = 42,                   // required
	Optional = 43,                   // optional
	Dtmf = 44,                       // dtmf
	DtmfMethod = 45,                 // dtmf-method
	Command = 46,                    // command
	Text = 47,                       // text
	Item = 48,                       // item
	Group = 49,                      // group
	Reason = 50,                     // reason
	Content = 51,                    // content
	Trying = 52,                     // trying
	Received = 53,                   // received
	File = 54,                       // file
	Offer = 55,                      // offer
	Request = 56,                    // request
	StreamHost = 57,                 // streamhost
	StreamHostUsed = 58,             // streamhost-used
	Ping = 59,                       // ping
	Encryption = 60,                 // encryption
	Crypto = 61,                     // crypto
	Parameter = 62,                  // parameter
	Identity = 63,                   // identity
	Priority = 64,                   // priority
	EntityCapsTag = 65,              // c
	Handshake = 66,                  // handshake
	Dialback = 67,                   // dialback
	Count = 68
    };

    /**
     * Constructor
     */
    inline XmlTag()
	: StringArray(s_array,Count)
	{}

private:
    static const String s_array[Count];  // Tag list
};


/**
 * This class holds a Jabber ID
 * @short A Jabber ID
 */
class YJABBER_API JabberID : public String
{
    YCLASS(JabberID,String)
public:
    /**
     * Constructor
     */
    inline JabberID() {}

    /**
     * Constructor. Constructs a JID from a given string
     * @param jid The JID string
     */
    inline JabberID(const char* jid)
	{ set(jid); }

    /**
     * Constructor. Constructs a JID from a given string
     * @param jid The JID string
     */
    inline JabberID(const String& jid)
	{ set(jid); }

    /**
     * Constructor. Constructs a JID from a given string
     * @param jid The JID string
     */
    inline JabberID(const String* jid)
	{ set(TelEngine::c_safe(jid)); }

    /**
     * Constructor. Constructs a JID from user, domain, resource
     * @param node The node
     * @param domain The domain
     * @param resource The resource
     */
    inline JabberID(const char* node, const char* domain, const char* resource = 0)
	{ set(node,domain,resource); }

    /**
     * Copy constructor
     * @param src Source Jabber ID
     */
    inline JabberID(const JabberID& src)
	{ *this = src; }

    /**
     * Check if this is a valid JID
     * @return True if this JID is a valid one
     */
    inline bool valid() const
	{ return null() || m_domain; }

    /**
     * Get the node part of the JID
     * @return The node part of the JID
     */
    inline const String& node() const
	{ return m_node; }

    /**
     * Get the bare JID: "node@domain"
     * @return The bare JID
     */
    inline const String& bare() const
	{ return m_bare; }

    /**
     * Get the domain part of the JID
     * @return The domain part of the JID
     */
    inline const String& domain() const
	{ return m_domain; }

    /**
     * Set the domain part of the JID.
     * @param d The new domain part of the JID.
     */
    inline void domain(const char* d)
	{ set(m_node.c_str(),d,m_resource.c_str()); }

    /**
     * Get the resource part of the JID
     * @return The resource part of the JID
     */
    inline const String& resource() const
	{ return m_resource; }

    /**
     * Check if this is a full JID
     * @return True if this is a full JID
     */
    inline bool isFull() const
	{ return m_node && m_domain && m_resource; }

    /**
     * Clear content
     */
    inline void clear() {
	    String::clear();
	    m_node.clear();
	    m_domain.clear();
	    m_resource.clear();
	    m_bare.clear();
	}

    /**
     * Try to match another JID to this one. If src has a resource compare it too
     * (case sensitive). Otherwise compare just the bare JID (case insensitive)
     * @param src The JID to match
     * @return True if matched
     */
    inline bool match(const JabberID& src) const
	{ return (src.resource().null() || (resource() == src.resource())) && (bare() &= src.bare()); }

    /**
     * Assignement operator from JabberID
     * @param src The JID to copy from
     * @return This object
     */
    JabberID& operator=(const JabberID& src);

    /**
     * Assignement operator from String
     * @param src The string
     * @return This object
     */
    inline JabberID& operator=(const String& src)
	{ set(src); return *this; }

    /**
     * Assignement operator from String pointer
     * @param src The string
     * @return This object
     */
    inline JabberID& operator=(const String* src)
	{ set(TelEngine::c_safe(src)); return *this; }

    /**
     * Equality operator. Do a case senitive resource comparison and a case
     *  insensitive bare jid comparison
     * @param src The JID to compare with
     * @return True if equal
     */
    inline bool operator==(const JabberID& src) const
	{ return (resource() == src.resource()) && (bare() &= src.bare()); }

    /**
     * Equality operator. Build a temporary JID and compare with it
     * @param src The string to compare with
     * @return True if equal
     */
    inline bool operator==(const String& src) const
	{ JabberID tmp(src); return operator==(tmp); }

    /**
     * Inequality operator
     * @param src The JID to compare with
     * @return True if not equal
     */
    inline bool operator!=(const JabberID& src) const
	{ return !operator==(src); }

    /**
     * Inequality operator
     * @param src The string to compare with
     * @return True if not equal
     */
    inline bool operator!=(const String& src) const
	{ return !operator==(src); }

    /**
     * Set the resource part of the JID
     * @param res The new resource part of the JID
     */
    inline void resource(const char* res)
	{ set(m_node.c_str(),m_domain.c_str(),res); }

    /**
     * Set the data
     * @param jid The JID string to assign
     */
    void set(const char* jid);

    /**
     * Set the data
     * @param node The node
     * @param domain The domain
     * @param resource The resource
     */
    void set(const char* node, const char* domain, const char* resource = 0);

    /**
     * Get an empty JabberID
     * @return A global empty JabberID
     */
    static const JabberID& empty();

    /**
     * Check if the given string contains valid characters
     * @param value The string to check
     * @return True if value is valid or 0. False if value is a non empty invalid string
     */
    static bool valid(const String& value);

    /**
     * Keep the regexp used to check the validity of a string
     */
    static Regexp s_regExpValid;

private:
    void parse();                        // Parse the string. Set the data 

    String m_node;                       // The node part
    String m_domain;                     // The domain part
    String m_resource;                   // The resource part
    String m_bare;                       // The bare JID: node@domain
};


/**
 * This class holds an identity for a JID
 * See http://xmpp.org/registrar/disco-categories.html for identity categories
 *  and associated types
 * @short A JID identity
 */
class YJABBER_API JIDIdentity : public GenObject
{
    YCLASS(JIDIdentity,GenObject)
public:
    /**
     * Constructor. Build a JID identity
     * @param c The JID's category
     * @param t The JID's type
     * @param name Optional identity (JID) name
     */
    inline JIDIdentity(const char* c, const char* t, const char* name = 0)
	: m_category(c), m_type(t), m_name(name)
	{}

    /**
     * Constructor. Build a JID identity from xml
     * @param identity The identity element
     */
    inline JIDIdentity(XmlElement* identity)
	{ fromXml(identity); }

    /**
     * Build an XML element from this identity
     * @return XmlElement pointer or 0 if category or type are empty
     */
    inline XmlElement* toXml() const {
	    if (!(m_category && m_type))
		return 0;
	    return createIdentity(m_category,m_type,m_name);
	}

    /**
     * Update this identity from an XML element
     * @param identity The source element
     */
    void fromXml(XmlElement* identity);

    /**
     * Create an 'identity' element
     * @param category The 'category' attribute
     * @param type The 'type' attribute
     * @param name The 'name' attribute
     * @return A valid XmlElement pointer
     */
    static XmlElement* createIdentity(const char* category,
	const char* type, const char* name);

    String m_category;
    String m_type;
    String m_name;
};


/**
 * This class holds a list of JID identities
 * @short A list of JID identities
 */
class YJABBER_API JIDIdentityList : public ObjList
{
    YCLASS(JIDIdentityList,ObjList)
public:
    /**
     * Fill an xml element with identities held by this list
     * @param parent The parent element to fill
     */
    void toXml(XmlElement* parent) const;

    /**
     * Add identity children from an xml element
     * @param parent The element containing the identity children
     */
    void fromXml(XmlElement* parent);
};

/**
 * This class holds an XMPP feature
 * @short A feature
 */
class YJABBER_API XMPPFeature : public String
{
    YCLASS(XMPPFeature,GenObject)
public:
    /**
     * Constructor
     * @param xml XML element tag as enumeration
     * @param feature The feature (namespace) index
     * @param required True if this feature is required
     */
    inline XMPPFeature(int xml, int feature, bool required = false)
	: m_xml(xml), m_required(required)
	{ setFeature(feature); }

    /**
     * Constructor
     * @param xml XML element tag as enumeration
     * @param feature The feature name
     * @param required True if this feature is required
     */
    inline XMPPFeature(int xml, const char* feature, bool required = false)
	: String(feature), m_xml(xml), m_required(required)
	{}

    /**
     * Constructor. Build from feature index
     * @param feature The feature
     */
    inline XMPPFeature(int feature)
	: m_xml(XmlTag::Count), m_required(false)
	{ setFeature(feature); }

    /**
     * Constructor. Build from feature name
     * @param feature The feature
     */
    inline XMPPFeature(const char* feature)
	: String(feature), m_xml(XmlTag::Count), m_required(false)
	{}

    /**
     * Destructor
     */
    virtual ~XMPPFeature()
	{}

    /**
     * Check if this feature is a required one
     * @return True if this feature is a required one
     */
    inline bool required() const
	{ return m_required; }

    /**
     * Build an xml element from this feature
     * @param addReq True to add the required/optional child
     * @return XmlElement pointer or 0
     */
    virtual XmlElement* build(bool addReq = true);

    /**
     * Build a feature element from this one
     * @return XmlElement pointer
     */
    virtual XmlElement* buildFeature();

    /**
     * Add a required/optional child to an element
     * @param xml Destination element
     */
    void addReqChild(XmlElement& xml);

    /**
     * Build a feature from a stream:features child
     * @param xml The feature element to parse
     * @return XMPPFeature pointer or 0 if unknown
     */
    static XMPPFeature* fromStreamFeature(XmlElement& xml);

private:
    void setFeature(int feature);

    int m_xml;                           // Element tag as enumeration
    bool m_required;                     // Required flag
};


/**
 * This class holds a SASL feature along with authentication mechanisms
 * @short A SASL feature
 */
class YJABBER_API XMPPFeatureSasl : public XMPPFeature
{
    YCLASS(XMPPFeatureSasl,XMPPFeature)
public:
    /**
     * Constructor
     * @param mech Authentication mechanism(s)
     * @param required Required flag
     */
    inline XMPPFeatureSasl(int mech, bool required = false)
	: XMPPFeature(XmlTag::Mechanisms,XMPPNamespace::Sasl,required),
	m_mechanisms(mech)
	{}

    /**
     * Get the authentication mechanisms
     * @return The authentication mechanisms used by the JID
     */
    inline int mechanisms() const
	{ return m_mechanisms; }

    /**
     * Check if a given mechanism is allowed
     * @return True if the given mechanism is allowed
     */
    inline bool mechanism(int mech) const
	{ return 0 != (m_mechanisms & mech); }

    /**
     * Build an xml element from this feature
     * @param addReq True to add the required/optional child
     * @return XmlElement pointer or 0
     */
    virtual XmlElement* build(bool addReq = true);

private:
    int m_mechanisms;                    // Authentication mechanisms
};


/**
 * This class holds a list of JID features
 * @short JID feature list
 */
class YJABBER_API XMPPFeatureList : public ObjList
{
    YCLASS(XMPPFeatureList,ObjList)
public:
    /**
     * Add a feature to the list
     * @param xml XML element tag as enumeration
     * @param feature The feature to add as enumeration
     * @param required True if this feature is required
     * @return False if the given feature already exists
     */
    inline bool add(int xml, int feature, bool required = false) {
	    if (get(feature))
		return false;
	    append(new XMPPFeature(xml,feature,required));
	    return true;
	}

    /**
     * Add a feature to the list
     * @param feature The feature to add as enumeration
     * @return False if the given feature already exists
     */
    inline bool add(int feature) {
	    if (get(feature))
		return false;
	    append(new XMPPFeature(feature));
	    return true;
	}

    /**
     * Add a feature to the list. Destroy the received parameter if already in the list
     * @param feature The feature to add
     * @return False if the given feature already exists
     */
    inline bool add(XMPPFeature* feature) {
	    if (!feature || get(*feature)) {
		TelEngine::destruct(feature);
		return false;
	    }
	    append(feature);
	    return true;
	}

    /**
     * Clear data
     */
    inline void reset() {
	    clear();
	    m_identities.clear();
	    m_entityCapsHash.clear();
	}

    /**
     * Move a list of features to this list. Don't check duplicates
     * @param list The source list
     */
    void add(XMPPFeatureList& list);

    /**
     * Re-build this list from stream features
     * @param xml The features element to parse
     */
    void fromStreamFeatures(XmlElement& xml);

    /**
     * Re-build this list from disco info responses
     * @param xml The element to parse
     */
    void fromDiscoInfo(XmlElement& xml);

    /**
     * Remove a feature from the list
     * @param feature The feature to remove
     */
    inline void remove(int feature)
	{ ObjList::remove(get(feature),true); }

    /**
     * Get a feature from the list
     * @param feature The feature to get
     * @return Pointer to the feature or 0 if it doesn't exists
     */
    XMPPFeature* get(int feature);

    /**
     * Get a feature from the list
     * @param feature The feature name to find
     * @return Pointer to the feature or 0 if it doesn't exists
     */
    inline XMPPFeature* get(const String& feature) {
	    ObjList* o = find(feature);
	    return o ? static_cast<XMPPFeature*>(o->get()) : 0;
	}

    /**
     * Build stream features from this list
     * @return XmlElement pointer
     */
    XmlElement* buildStreamFeatures();

    /**
     * Build an iq query disco info result from this list
     * @param from The 'from' attribute
     * @param to The 'to' attribute
     * @param id The 'id' attribute
     * @param node Optional 'node' attribute
     * @param cap Optional capability to be set as 'node' suffix
     * @return XmlElement pointer
     */
    XmlElement* buildDiscoInfo(const char* from, const char* to, const char* id,
	const char* node = 0, const char* cap = 0);

    /**
     * Add this list to an xml element
     * @param xml Destination element
     */
    void add(XmlElement& xml);

    /**
     * Update the entity capabilities hash
     */
    void updateEntityCaps();

    JIDIdentityList m_identities;
    String m_entityCapsHash;             // SHA-1 entity caps as defined in XEP 0115
};


/**
 * This class is a general XMPP utilities
 * @short General XMPP utilities
 */
class YJABBER_API XMPPUtils
{
public:
    /**
     * Presence type enumeration
     */
    enum Presence {
	Probe,                           // probe
	Subscribe,                       // subscribe request
	Subscribed,                      // subscribe accepted
	Unavailable,                     // unavailable
	Unsubscribe,                     // unsubscribe request
	Unsubscribed,                    // unsubscribe accepted
	PresenceError,                   // error
	PresenceNone
    };

    /**
     * Message type enumeration
     */
    enum MsgType {
	Chat,                            // chat
	GroupChat,                       // groupchat
	HeadLine,                        // headline
	Normal,                          // normal
	MsgError,                        // error
    };

    /**
     * Iq type enumeration
     */
    enum IqType {
	IqSet,                           // set
	IqGet,                           // get
	IqResult,                        // result
	IqError,                         // error
	IqCount
    };

    /**
     * Command action enumeration
     */
    enum CommandAction {
	CommExecute,
	CommCancel,
	CommPrev,
	CommNext,
	CommComplete,
    };

    /**
     * Command status enumeration
     */
    enum CommandStatus {
	CommExecuting,
	CommCompleted,
	CommCancelled,
    };

    /**
     * Authentication methods
     */
    enum AuthMethod {
	AuthNone     = 0x00,             // No authentication mechanism
	AuthSHA1     = 0x01,             // SHA1 digest
	AuthMD5      = 0x02,             // MD5 digest
	AuthPlain    = 0x04,             // Plain text password
	AuthDialback = 0x08,             // Dialback authentication
    };

    /**
     * Check if an xml element has type 'result' or 'error'
     * @param xml The element to check
     * @return True if the element is a response one
     */
    static inline bool isResponse(const XmlElement& xml) {
	    String* tmp = xml.getAttribute("type");
	    return tmp && (*tmp == "result" || *tmp == "error");
	}

    /**
     * Create an XML element
     * @param name Element's name
     * @param text Optional text for the element
     * @param ns Optional element namespace
     * @return A valid XmlElement pointer
     */
    static inline XmlElement* createElement(const char* name, const char* text = 0,
	    const String& ns = String::empty()) {
	    XmlElement* xml = new XmlElement(String(name),true);
	    if (!TelEngine::null(text))
		xml->addText(text);
	    if (ns)
		xml->setXmlns(String::empty(),true,ns);
	    return xml;
	}

    /**
     * Create an XML element
     * @param type Element's type
     * @param text Optional text for the element
     * @return A valid XmlElement pointer
     */
    static inline XmlElement* createElement(int type, const char* text = 0)
	{ return createElement(s_tag[type],text); }

    /**
     * Create an XML element with an 'xmlns' attribute
     * @param name Element's name
     * @param ns Optional 'xmlns' attribute as enumeration
     * @param text Optional text for the element
     * @return A valid XmlElement pointer
     */
    static inline XmlElement* createElement(const char* name, int ns,
	const char* text = 0) {
	    XmlElement* xml = createElement(name,text);
	    setXmlns(*xml,String::empty(),true,ns);
	    return xml;
	}

    /**
     * Create an XML element with an 'xmlns' attribute
     * @param type Element's type
     * @param ns 'xmlns' attribute as enumeration
     * @param text Optional text for the element
     * @return A valid XmlElement pointer
     */
    static inline XmlElement* createElement(int type, int ns, const char* text = 0)
	{ return createElement(s_tag[type],ns,text); }

    /**
     * Partially build an XML element from another one.
     * Copy tag and 'to', 'from', 'type', 'id' attributes
     * @param src Source element
     * @param response True to reverse 'to' and 'from' attributes
     * @param result True to set type to "result", false to set it to "error".
     *  Ignored if response is false
     */
    static XmlElement* createElement(const XmlElement& src, bool response, bool result);

    /**
     * Create an 'iq' element
     * @param type Iq type as enumeration
     * @param from The 'from' attribute
     * @param to The 'to' attribute
     * @param id The 'id' attribute
     * @return A valid XmlElement pointer
     */
    static XmlElement* createIq(IqType type, const char* from = 0,
	const char* to = 0, const char* id = 0);

    /**
     * Create an 'iq' result element
     * @param from The 'from' attribute
     * @param to The 'to' attribute
     * @param id The 'id' attribute
     * @param child Optional element child (will be consumed)
     * @return A valid XmlElement pointer
     */
    static inline XmlElement* createIqResult(const char* from, const char* to,
	const char* id, XmlElement* child = 0) {
	    XmlElement* xml = createIq(IqResult,from,to,id);
	    if (child)
		xml->addChild(child);
	    return xml;
	}

    /**
     * Create an 'iq' error from a received element. Consume the received element.
     * Add the given element to the error stanza if the 'id' attribute is missing
     * @param from The 'from' attribute
     * @param to The 'to' attribute
     * @param xml Received element
     * @param type Error type
     * @param error The error
     * @param text Optional text to add to the error element
     * @return A valid XmlElement pointer or 0 if xml
     */
    static XmlElement* createIqError(const char* from, const char* to, XmlElement*& xml,
	int type, int error, const char* text = 0);

    /**
     * Create an 'iq' element with a 'vcard' child
     * @param get True to set the iq's type to 'get', false to set it to 'set'
     * @param from The 'from' attribute
     * @param to The 'to' attribute
     * @param id The 'id' attribute
     * @return A valid XmlElement pointer
     */
    static XmlElement* createVCard(bool get, const char* from, const char* to, const char* id);

    /**
     * Create a 'command' element
     * @param action The command action
     * @param node The command
     * @param sessionId Optional session ID for the command
     * @return A valid XmlElement pointer
     */
    static XmlElement* createCommand(CommandAction action, const char* node,
	const char* sessionId = 0);

    /**
     * Create a disco info/items 'iq' element with a 'query' child
     * @param info True to create a query info request. False to create a query items request
     * @param req True to create a request (type=get), false to create a response (type=result)
     * @param from The 'from' attribute
     * @param to The 'to' attribute
     * @param id The 'id' attribute
     * @param node Optional 'node' attribute
     * @param cap Optional capability to be set as 'node' suffix
     * @return A valid XmlElement pointer
     */
    static XmlElement* createIqDisco(bool info, bool req, const char* from, const char* to,
	const char* id, const char* node = 0, const char* cap = 0);

    /**
     * Create a version 'iq' result as defined in XEP-0092
     * @param from The 'from' attribute
     * @param to The 'to' attribute
     * @param id The 'id' attribute
     * @param name Program name
     * @param version Program version
     * @param os Optional operating system
     * @return A valid XmlElement pointer
     */
    static XmlElement* createIqVersionRes(const char* from, const char* to,
	const char* id, const char* name, const char* version, const char* os = 0);

    /**
     * Create a 'error' element
     * @param type Error type
     * @param error The error
     * @param text Optional text to add to the error element
     * @return A valid XmlElement pointer
     */
    static XmlElement* createError(int type, int error, const char* text = 0);

    /**
     * Create an error from a received element. Consume the received element.
     * Reverse 'to' and 'from' attributes
     * @param xml Received element
     * @param type Error type
     * @param error The error
     * @param text Optional text to add to the error element
     * @return A valid XmlElement pointer or 0 if xml is 0
     */
    static XmlElement* createError(XmlElement* xml, int type, int error,
	const char* text = 0);

    /**
     * Create a 'stream:error' element
     * @param error The XMPP defined condition
     * @param text Optional text to add to the error
     * @return A valid XmlElement pointer
     */
    static XmlElement* createStreamError(int error, const char* text = 0);

    /**
     * Build a register query element
     * @param type Iq type as enumeration
     * @param from The 'from' attribute
     * @param to The 'to' attribute
     * @param id The 'id' attribute
     * @param child1 Optional child of query element
     * @param child2 Optional child of query element
     * @param child3 Optional child of query element
     * @return Valid XmlElement pointer
     */
    static XmlElement* createRegisterQuery(IqType type, const char* from,
	const char* to, const char* id,
	XmlElement* child1 = 0, XmlElement* child2 = 0, XmlElement* child3 = 0);

    /**
     * Build a jabber:iq:auth 'iq' get element
     * @param id Element 'id' attribute
     * @return A valid XmlElement pointer
     */
    static inline XmlElement* createIqAuthGet(const char* id) {
	    XmlElement* iq = createIq(IqGet,0,0,id);
	    iq->addChild(createElement(XmlTag::Query,XMPPNamespace::IqAuth));
	    return iq;
	}

    /**
     * Build a jabber:iq:auth 'iq' set element
     * @param id Element 'id' attribute
     * @param username The username
     * @param resource The resource
     * @param authStr Authentication string
     * @param digest True if authentication string is a digest, false if it's a plain password
     * @return A valid XmlElement pointer
     */
    static XmlElement* createIqAuthSet(const char* id, const char* username,
	const char* resource, const char* authStr, bool digest);

    /**
     * Build a jabber:iq:auth 'iq' offer in response to a 'get' request
     * @param id Element 'id' attribute
     * @param digest Offer digest authentication
     * @param plain Offer plain password authentication
     * @return A valid XmlElement pointer
     */
    static XmlElement* createIqAuthOffer(const char* id, bool digest = true,
	bool plain = false);

    /**
     * Build an register query element used to create/set username/password
     * @param from The 'from' attribute
     * @param to The 'to' attribute
     * @param id The 'id' attribute
     * @param username The username
     * @param password The password
     * @return Valid XmlElement pointer
     */
    static inline XmlElement* createRegisterQuery(const char* from,
	const char* to, const char* id,
	const char* username, const char* password) {
	    return createRegisterQuery(XMPPUtils::IqSet,from,to,id,
		createElement(XmlTag::Username,username),
		createElement(XmlTag::Password,password));
	}

    /**
     * Create a failure element
     * @param ns Element namespace
     * @param error Optional error
     * @return XmlElement pointer
     */
    static inline XmlElement* createFailure(XMPPNamespace::Type ns,
	XMPPError::Type error = XMPPError::NoError) {
	    XmlElement* xml = createElement(XmlTag::Failure,ns);
	    if (error != XMPPError::NoError)
		xml->addChild(new XmlElement(s_error[error]));
	    return xml;
	}

    /**
     * Create an 'x' jabber:x:oob url element as described in XEP-0066
     * @param url The URL
     * @param desc Optional description
     * @return XmlElement pointer
     */
    static inline XmlElement* createXOobUrl(const char* url, const char* desc = 0) {
	    XmlElement* xml = createElement("x",XMPPNamespace::XOob);
	    xml->addChild(createElement("url",url));
	    if (desc)
		xml->addChild(createElement("desc",desc));
	    return xml;
	}

    /**
     * Create a 'delay' element as defined in XEP-0203
     * @param timeSec The time to encode (in seconds)
     * @param from Optional 'from' attribute
     * @param fractions Optional second fractions
     * @param text Optional xml element text
     * @return XmlElement pointer
     */
    static XmlElement* createDelay(unsigned int timeSec, const char* from = 0,
	unsigned int fractions = 0, const char* text = 0);

    /**
     * Check if an element has a child with 'remove' tag
     * @param xml The element to check
     * @return True if the element has a child with 'remove' tag
     */
    static inline bool remove(XmlElement& xml)
	{ return 0 != findFirstChild(xml,XmlTag::Remove); }

    /**
     * Check if an element has a child with 'required' tag
     * @param xml The element to check
     * @return True if the element has a child with 'required' tag
     */
    static inline bool required(XmlElement& xml)
	{ return 0 != findFirstChild(xml,XmlTag::Required); }

    /**
     * Check if an element has a child with 'priority' tag
     * @param xml The element to check
     * @param defVal Default value to return if not found or invalid integer
     * @return Element priority
     */
    static int priority(XmlElement& xml, int defVal = 0);

    /**
     * Add a 'priority' child to an element
     * @param xml The element to set
     * @param prio Priority text
     */
    static inline void setPriority(XmlElement& xml, const char* prio)
	{ xml.addChild(createElement(XmlTag::Priority,prio)); }

    /**
     * Get an element's namespace
     * @param xml Element
     * @return Element namespace as enumeration
     */
    static inline int xmlns(XmlElement& xml) {
	    String* x = xml.xmlns();
	    return x ? s_ns[*x] : XMPPNamespace::Count;
	}

    /**
     * Check if the given element has a given default namespace
     * @param xml Element to check
     * @param ns Namespace value to check
     * @return True if the given element has the requested default namespace
     */
    static inline bool hasDefaultXmlns(const XmlElement& xml, int ns) {
	    String* s = xml.xmlnsAttribute(XmlElement::s_ns);
	    return s && *s == s_ns[ns];
	}

    /**
     * Check if the given element has a given namespace
     * @param xml Element to check
     * @param ns Namespace value to check
     * @return True if the given element is in the requested namespace
     */
    static inline bool hasXmlns(const XmlElement& xml, int ns)
	{ return xml.hasXmlns(s_ns[ns]); }

    /**
     * Set an element's namespace
     * @param xml Element
     * @param name Namespace attribute name
     * @param addAttr True to add the namespace attribute value
     * @param ns Namespace value as enumeration
     * @return True on success
     */
    static inline bool setXmlns(XmlElement& xml, const String& name = String::empty(),
	bool addAttr = false, int ns = XMPPNamespace::Count) {
	    if (ns < XMPPNamespace::Count)
		return xml.setXmlns(name,addAttr,s_ns[ns]);
	    return xml.setXmlns(name);
	}

    /**
     * Set the 'stream' namespace to an element
     * @param xml Element
     * @param addAttr True to add the xmlns attribute
     * @return True on success
     */
    static inline bool setStreamXmlns(XmlElement& xml, bool addAttr = true)
	{ return setXmlns(xml,"stream",addAttr,XMPPNamespace::Stream); }

    /**
     * Set the 'db' namespace to an element
     * @param xml Element
     * @return True on success
     */
    static inline bool setDbXmlns(XmlElement& xml)
	{ return setXmlns(xml,"db",true,XMPPNamespace::Dialback); }

    /**
     * Find an element's first child element in a given namespace
     * @param xml Element
     * @param t Optional element tag as enumeration
     * @param ns Optional element namespace as enumeration
     * @return XmlElement pointer or 0 if not found
     */
    static XmlElement* findFirstChild(const XmlElement& xml, int t = XmlTag::Count,
	int ns = XMPPNamespace::Count);

    /**
     * Find an element's next child element
     * @param xml Element
     * @param start Starting child
     * @param t Optional element tag as enumeration
     * @param ns Optional element namespace as enumeration
     * @return XmlElement pointer or 0 if not found
     */
    static XmlElement* findNextChild(const XmlElement& xml, XmlElement* start,
	int t = XmlTag::Count, int ns = XMPPNamespace::Count);

    /**
     * Find an error child of a given element and decode it
     * @param xml The element
     * @param ns Expected error condition namespace. If not set, defaults to stream error
     *  namespace if the element is a stream error or to stanza error namespace otherwise
     * @param error Optional string to be filled with error tag
     * @param text Optional string to be filled with error text
     */
    static void decodeError(XmlElement* xml, int ns = XMPPNamespace::Count,
	String* error = 0, String* text = 0);

    /**
     * Decode a stream error or stanza error
     * @param xml The element
     * @param error The error condition
     * @param text The stanza's error or error text
     */
    static void decodeError(XmlElement* xml, String& error, String& text);

    /**
     * Encode EPOCH time given in seconds to a date/time profile as defined in
     *  XEP-0082 and XML Schema Part 2: Datatypes Second Edition
     * @param buf Destination string
     * @param timeSec The time to encode (in seconds)
     * @param fractions Optional second fractions
     */
    static void encodeDateTimeSec(String& buf, unsigned int timeSec,
	unsigned int fractions = 0);

    /**
     * Decode a date/time profile as defined in XEP-0082
     *  and XML Schema Part 2: Datatypes Second Edition to EPOCH time
     * @param time The date/time string
     * @param fractions Pointer to integer to be filled with second fractions, if present
     * @return The decoded time in seconds, -1 on error
     */
    static unsigned int decodeDateTimeSec(const String& time, unsigned int* fractions = 0);

    /**
     * Decode a date/time stamp as defined in XEP-0091 (jabber:x:delay)
     * @param time The date/time string
     * @return The decoded time in seconds, -1 on error
     */
    static unsigned int decodeDateTimeSecXDelay(const String& time);

    /**
     * Print an XmlElement to a string
     * @param xmlStr The destination string
     * @param xml The xml to print
     * @param verbose True to print XML data on multiple lines
     */
    static void print(String& xmlStr, XmlChild& xml, bool verbose);

    /**
     * Put an element's name, text and attributes to a list of parameters
     * @param xml The element
     * @param dest Destination list
     * @param prefix Prefix to add to parameters
     */
    static void toList(XmlElement& xml, NamedList& dest, const char* prefix);

    /**
     * Split a string at a delimiter character and fills a named list with its parts
     * Skip empty parts
     * @param dest The destination NamedList
     * @param src Pointer to the string
     * @param sep The delimiter
     * @param nameFirst True to add the parts as name and index as value.
     *  False to do the other way
     */
    static bool split(NamedList& dest, const char* src, const char sep,
	bool nameFirst);

    /**
     * Decode a comma separated list of flags and put them into an integer mask
     * @param src Source string
     * @param dict Dictionary containing flag names and values
     * @return The mask of found flags
     */
    static int decodeFlags(const String& src, const TokenDict* dict);

    /**
     * Encode a mask of flags to a comma separated list of names
     * @param dest Destination string
     * @param src Source mask
     * @param dict Dictionary containing flag names and values
     */
    static void buildFlags(String& dest, int src, const TokenDict* dict);

    /**
     * Add child elements from a list to a destination element
     * @param dest Destination XmlElement
     * @param list A list containing XML elements
     * @return True if at least one child was added
     */
    static bool addChidren(XmlElement* dest, ObjList& list);

    /**
     * Create a 'c' entity capability element as defined in XEP 0115
     * @param hash The 'ver' attribute
     * @param node The 'node' attribute
     * @return XmlElement pointer or 0 on failure
     */
    static XmlElement* createEntityCaps(const String& hash, const char* node);

    /**
     * Create a 'c' entity capability element as defined by GTalk
     * @return A valid XmlElement pointer
     */
    static XmlElement* createEntityCapsGTalkV1();

    /**
     * Create an 'presence' element
     * @param from The 'from' attribute
     * @param to The 'to' attribute
     * @param type Presence type as enumeration
     * @return A valid XmlElement pointer
     */
    static XmlElement* createPresence(const char* from,
	const char* to, Presence type = PresenceNone);

    /**
     * Create a 'message' element
     * @param type Message type string
     * @param from The 'from' attribute
     * @param to The 'to' attribute
     * @param id The 'id' attribute
     * @param body The message body
     * @return A valid XmlElement pointer
     */
    static XmlElement* createMessage(const char* type, const char* from,
	const char* to, const char* id, const char* body);

    /**
     * Create a 'message' element
     * @param type Message type as enumeration
     * @param from The 'from' attribute
     * @param to The 'to' attribute
     * @param id The 'id' attribute
     * @param body The message body
     * @return A valid XmlElement pointer
     */
    static inline XmlElement* createMessage(MsgType type, const char* from,
	const char* to, const char* id, const char* body)
	{ return createMessage(msgText(type),from,to,id,body); }

    /**
     * Build a dialback 'db:result' xml element used to send a dialback key
     * @param from The sender
     * @param to The recipient
     * @param key The dialback key
     * @return XmlElement pointer
     */
    static XmlElement* createDialbackKey(const char* from, const char* to,
	const char* key);

    /**
     * Build a dialback 'db:result' xml element used to send a dialback key response
     * @param from The sender
     * @param to The recipient
     * @param rsp The response as enumeration: set it to NoError if valid,
     *  NotAuthorized if invalid or any other error to send a db:result error type
     * @return XmlElement pointer
     */
    static XmlElement* createDialbackResult(const char* from, const char* to,
	XMPPError::Type rsp = XMPPError::NoError);

    /**
     * Build a dialback 'db:verify' xml element
     * @param from The sender
     * @param to The recipient
     * @param id The 'id' attribute (stream id)
     * @param key The dialback key
     * @return XmlElement pointer
     */
    static XmlElement* createDialbackVerify(const char* from, const char* to,
	const char* id, const char* key);

    /**
     * Build a dialback 'db:verify' response xml element
     * @param from The sender
     * @param to The recipient
     * @param id The 'id' attribute (stream id)
     * @param rsp The response as enumeration: set it to NoError if valid,
     *  NotAuthorized if invalid or any other error to send a db:verify error type
     * @return XmlElement pointer
     */
    static XmlElement* createDialbackVerifyRsp(const char* from, const char* to,
	const char* id, XMPPError::Type rsp = XMPPError::NoError);

    /**
     * Decode a dialback verify or result response element
     * @param xml The element
     * @return The response as enumeration: NoError if valid, NotAuthorized if invalid or
     *  any other error if set in the response
     */
    static int decodeDbRsp(XmlElement* xml);

    /**
     * Build a 'subject' xml element
     * @param subject Element text
     * @return XmlElement pointer
     */
    static inline XmlElement* createSubject(const char* subject)
	{ return createElement(XmlTag::Subject,subject); }

    /**
     * Get an element's subject (the text of the first 'subject' child)
     * @param xml The element
     * @return Element subject or an empty string
     */
    static inline const String& subject(XmlElement& xml) {
	    XmlElement* s = findFirstChild(xml,XmlTag::Subject);
	    return s ? s->getText() : String::empty();
	}

    /**
     * Build a 'body' xml element
     * @param body Element text
     * @param ns Optional namespace
     * @return XmlElement pointer
     */
    static inline XmlElement* createBody(const char* body, int ns = XMPPNamespace::Count)
	{ return createElement(XmlTag::Body,ns,body); }

    /**
     * Retrieve the text of an element's body child
     * @param xml The element
     * @param ns Optional body namespace to match (default: match parent's namespace)
     * @return Body or empty string
     */
    static const String& body(XmlElement& xml, int ns = XMPPNamespace::Count);

    /**
     * Build a name/value parameter xml element
     * @param name The 'name' attribute
     * @param value The value parameter
     * @param tag Optional element tag (defaults to 'parameter')
     * @return XmlElement pointer
     */
    static inline XmlElement* createParameter(const char* name, const char* value,
	const char* tag = "parameter") {
	    XmlElement* tmp = new XmlElement(tag);
	    tmp->setAttributeValid("name",name);
	    tmp->setAttributeValid("value",value);
	    return tmp;
	}

    /**
     * Build a name/value parameter xml element
     * @param pair The name/value pair
     * @param tag Optional element tag (defaults to 'parameter')
     * @return XmlElement pointer
     */
    static inline XmlElement* createParameter(const NamedString& pair,
	const char* tag = "parameter")
	{ return createParameter(pair.name(),pair,tag); }

    /**
     * Get an element's namespace
     * @param xml The element
     * @return The namespace integer value as XMPPNamespace value
     */
    static inline int ns(const XmlElement& xml) {
	    String* xmlns = xml.xmlns();
	    return xmlns ? s_ns[*xmlns] : XMPPNamespace::Count;
	}

    /**
     * Get an XML tag enumeration value associated with an element's tag
     * @param xml The element to check
     * @return Xml tag as enumeration
     */
    static inline int tag(const XmlElement& xml)
	{ return s_tag[xml.getTag()]; }

    /**
     * Get an XML element's tag and namespace
     * @param xml The element to check
     * @param tag Element tag as enumeration
     * @param ns Element namespace as enumeration
     * @return True if data was succesfully retrieved
     */
    static inline bool getTag(const XmlElement& xml, int& tag, int& ns) {
	     const String* t = 0;
	     const String* n = 0;
	     if (!xml.getTag(t,n))
		return false;
	     tag = s_tag[*t];
	     ns = n ? s_ns[*n] : XMPPNamespace::Count;
	     return tag != XmlTag::Count;
	}

    /**
     * Check if an xml element has a given tag (without prefix) and namespace
     * @param xml The element to check
     * @param tag Tag to check
     * @param ns Namespace to check
     * @return True if the element has the requested tag and namespace
     */
    static inline bool isTag(const XmlElement& xml, int tag, int ns) {
	    int t,n;
	    return getTag(xml,t,n) && tag == t && n == ns;
	}

    /**
     * Check if an xml element has a given tag (without prefix)
     * @param xml The element to check
     * @param tag Tag to check
     * @return True if the element has the requested tag
     */
    static inline bool isUnprefTag(const XmlElement& xml, int tag)
	{ return xml.unprefixedTag() == s_tag[tag]; }

    /**
     * Check if a given element is a stanza one ('iq', 'message' or 'presence')
     * @param xml The element to check
     * @return True if the element is a stanza
     */
    static inline bool isStanza(const XmlElement& xml) {
	    int t,n;
	    return getTag(xml,t,n) &&
		(t == XmlTag::Iq || t == XmlTag::Presence || t == XmlTag::Message);
	}

    /**
     * Retrieve an xml element from a NamedPointer.
     * Release NamedPointer ownership if found
     * @param gen The object to be processed
     * @return XmlElement pointer or 0
     */
    static XmlElement* getXml(GenObject* gen);

    /**
     * Parse a string to an XmlElement
     * @param data XML data to parse
     * @return XmlElement pointer or 0 if the string is an invalid xml or contains more
     *  then one element
     */
    static XmlElement* getXml(const String& data);

    /**
     * Retrieve an xml element from a list parameter.
     * Clear the given parameter from list if an XmlElement is found
     * Try to build (parse) from an extra parameter if not found
     * @param list The list of parameters
     * @param param The name of the parameter with the xml element
     * @param extra Optional parameter containing xml string data
     * @return XmlElement pointer or 0
     */
    static XmlElement* getXml(NamedList& list, const char* param = "xml",
	const char* extra = "data");

    /**
     * Retrieve a presence xml element from a list parameter.
     * Clear the given parameter from list if an XmlElement is found.
     * Try to build (parse) from an extra parameter if not found.
     * Build a presence stanza from parameters if an element is not found
     * @param list The list of parameters
     * @param param The name of the parameter with the xml element
     * @param extra Optional parameter containing xml string data
     * @param type Presence type to build
     * @param build True to build a message stanza if an element is not found
     * @return XmlElement pointer or 0
     */
    static XmlElement* getPresenceXml(NamedList& list, const char* param = "xml",
	const char* extra = "data", Presence type = PresenceNone, bool build = true);

    /**
     * Retrieve a chat (message) xml element from a list parameter.
     * Clear the given parameter from list if an XmlElement is found.
     * Try to build (parse) from an extra parameter if not found.
     * Build a message stanza from parameters if an element is not found
     * @param list The list of parameters
     * @param param The name of the parameter with the xml element
     * @param extra Optional parameter containing xml string data
     * @param build True to build a message stanza if an element is not found
     * @return XmlElement pointer or 0
     */
    static XmlElement* getChatXml(NamedList& list, const char* param = "xml",
	const char* extra = "data", bool build = true);

    /**
     * Get the type of a 'presence' stanza as enumeration
     * @param text The text to check
     * @return Presence type as enumeration
     */
    static inline Presence presenceType(const char* text)
	{ return (Presence)lookup(text,s_presence,PresenceNone); }

    /**
     * Get the text from a presence type
     * @param presence The presence type
     * @return The associated text or 0
     */
    static inline const char* presenceText(Presence presence)
	{ return lookup(presence,s_presence,0); }

    /**
     * Get the type of a 'message' stanza
     * @param text The text to check
     * @return Message type as enumeration
     */
    static inline MsgType msgType(const char* text)
	{ return (MsgType)lookup(text,s_msg,Normal); }

    /**
     * Get the text from a message type
     * @param msg The message type
     * @return The associated text or 0
     */
    static inline const char* msgText(MsgType msg)
	{ return lookup(msg,s_msg,0); }

    /**
     * Get the type of an 'iq' stanza as enumeration
     * @param text The text to check
     * @return Iq type as enumeration
     */
    static inline IqType iqType(const char* text)
	{ return (IqType)lookup(text,s_iq,IqCount); }

    /**
     * Get the authentication method associated with a given text
     * @param text The text to check
     * @param defVal Default value to return if not found
     * @return Authentication method
     */
    static inline int authMeth(const char* text, int defVal = AuthNone)
	{ return lookup(text,s_authMeth,defVal); }

    /**
     * Namespaces
     */
    static XMPPNamespace s_ns;

    /**
     * Errors
     */
    static XMPPError s_error;

    /**
     * XML tags
     */
    static XmlTag s_tag;

    /**
     * Keep the types of 'presence' stanzas
     */
    static const TokenDict s_presence[];

    /**
     * Keep the types of 'message' stanzas
     */
    static const TokenDict s_msg[];

    /**
     * Keep the types of 'iq' stanzas
     */
    static const TokenDict s_iq[];

    /**
     * Keep the command actions
     */
    static const TokenDict s_commandAction[];

    /**
     * Keep the command status
     */
    static const TokenDict s_commandStatus[];

    /**
     * Authentication methods names
     */
    static const TokenDict s_authMeth[];
};

/**
 * This class holds a direction flags (such as subscription states)
 * @short Direction flags
 */
class YJABBER_API XMPPDirVal
{
public:
    /**
     * Direction flags enumeration
     */
    enum Direction {
	None = 0x00,
	To = 0x01,
	From = 0x02,
	PendingIn = 0x10,
	PendingOut = 0x20,
	// Masks
	Both = 0x03,
	Pending = 0x30
    };

    /**
     * Constructor
     * @param flags Flag(s) to set
     */
    inline XMPPDirVal(int flags = None)
	: m_value(flags)
	{}

    /**
     * Constructor
     * @param flags Comma separated list of flags
     */
    inline XMPPDirVal(const String& flags)
	: m_value(0)
	{ replace(flags); }

    /**
     * Replace all flags
     * @param flag The new value of the flags
     */
    inline void replace(int flag)
	{ m_value = flag; }

    /**
     * Replace all flags from a list
     * @param flags Comma separated list of flags
     */
    inline void replace(const String& flags)
	{ m_value = XMPPUtils::decodeFlags(flags,s_names); }

    /**
     * Build a string representation of this object
     * @param buf Destination string
     * @param full True to add all flags, false to ignore pending flags
     */
    void toString(String& buf, bool full) const;

    /**
     * Build a subscription state string representation of this object
     * @param buf Destination string
     */
    void toSubscription(String& buf) const;

    /**
     * Set one or more flags
     * @param flag Flag(s) to set
     */
    inline void set(int flag)
	{ m_value |= flag; }

    /**
     * Reset one or more flags
     * @param flag Flag(s) to reset
     */
    inline void reset(int flag)
	{ m_value &= ~flag; }

    /**
     * Check if a given bit mask is set
     * @param mask Bit mask to check
     * @return True if the given bit mask is set
     */
    inline bool test(int mask) const
	{ return (m_value & mask) != 0; }

    /**
     * Check if the 'To' flag is set
     * @return True if the 'To' flag is set
     */
    inline bool to() const
	{ return test(To); }

    /**
     * Check if the 'From' flag is set
     * @return True if the 'From' flag is set
     */
    inline bool from() const
	{ return test(From); }

    /**
     * Cast operator
     */
    inline operator int()
	{ return m_value; }

    /**
     * Keep the flag names
     */
    static const TokenDict s_names[];

private:
    int m_value;                         // The value
};

/**
 * This class holds an XML element to be sent through a stream
 * @short An outgoing XML element
 */
class YJABBER_API XmlElementOut : public RefObject
{
public:
    /**
     * Constructor
     * @param element The XML element
     * @param senderID Optional sender id
     * @param unclose True to not close the tag when building the buffer
     */
    inline XmlElementOut(XmlElement* element, const char* senderID = 0,
	bool unclose = false)
	: m_element(element), m_offset(0), m_id(senderID), m_unclose(unclose),
	m_sent(false)
	{}

    /**
     * Destructor
     * Delete m_element if not 0
     */
    virtual ~XmlElementOut()
	{ TelEngine::destruct(m_element); }

    /**
     * Get the underlying element
     * @return The underlying element
     */
    inline XmlElement* element() const
	{ return m_element; }

    /**
     * Check if this element was (partially) sent
     * @return True if an attempt to send this element was already done
     */
    inline bool sent() const
	{ return m_sent; }

    /**
     * Get the data buffer
     * @return The data buffer
     */
    inline const String& buffer()
	{ return m_buffer; }

    /**
     * Get the id member
     * @return The id member
     */
    inline const String& id() const
	{ return m_id; }

    /**
     * Get the remainig byte count to send
     * @return The unsent number of bytes
     */
    inline unsigned int dataCount()
	{ return m_buffer.length() - m_offset; }

    /**
     * Get the remainig data to send. Set the buffer if not already set
     * @param nCount The number of unsent bytes
     * @return Pointer to the remaining data or 0
     */
    inline const char* getData(unsigned int& nCount) {
	    if (!m_buffer)
		prepareToSend();
	    nCount = dataCount();
	    return m_buffer.c_str() + m_offset;
	}

    /**
     * Increase the offset with nCount bytes. Set the sent flag
     * @param nCount The number of bytes sent
     */
    inline void dataSent(unsigned int nCount) {
	    m_sent = true;
	    m_offset += nCount;
	    if (m_offset > m_buffer.length())
		m_offset = m_buffer.length();
	}

    /**
     * Release the ownership of m_element
     * The caller is responsable of returned pointer
     * @return XmlElement pointer or 0
     */
    inline XmlElement* release() {
	    XmlElement* e = m_element;
	    m_element = 0;
	    return e;
	}

    /**
     * Fill a buffer with the XML element to send
     * @param buffer The buffer to fill
     */
    inline void toBuffer(String& buffer) {
	    if (m_element)
		m_element->toString(buffer,true,String::empty(),String::empty(),!m_unclose);
	}

    /**
     * Fill the buffer with the XML element to send
     */
    inline void prepareToSend()
	{ toBuffer(m_buffer); }

private:
    XmlElement* m_element;               // The XML element
    String m_buffer;                     // Data to send
    unsigned int m_offset;               // Offset to send
    String m_id;                         // Sender's id
    bool m_unclose;                      // Close or not the element's tag
    bool m_sent;                         // Sent flag (true if an attempt to send was done)
};

};

#endif /* __XMPPUTILS_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
