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
#include <xmlparser.h>

#ifdef _WINDOWS

#ifdef LIBYJINGLE_EXPORTS
#define YJINGLE_API __declspec(dllexport)
#else
#ifndef LIBYJINGLE_STATIC
#define YJINGLE_API __declspec(dllimport)
#endif
#endif

#endif /* _WINDOWS */

#ifndef YJINGLE_API
#define YJINGLE_API
#endif

/**
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

class XMPPServerInfo;                    // Server info class
class XMPPNamespace;                     // XMPP namespaces
class XMPPError;                         // XMPP errors
class JabberID;                          // A Jabber ID (JID)
class JIDIdentity;                       // A JID's identity
class JIDFeature;                        // A JID's feature
class JIDFeatureSasl;                    // A JID's SASL feature
class JIDFeatureList;                    // Feature list
class XMPPUtils;                         // Utilities


/**
 * This class holds informations about a server
 * @short Server info class
 */
class YJINGLE_API XMPPServerInfo : public RefObject
{
public:
    /**
     * Server flags
     */
    enum ServerFlag {
	NoAutoRestart     = 0x0001,      // Don't auto restart streams when down
	KeepRoster        = 0x0002,      // Tell the presence service to keep the roster for this server
	NoVersion1        = 0x0004,      // The server doesn't support RFC 3920 TLS/SASL ...
	TlsRequired       = 0x0008,      // The server always require connection encryption
	Sasl              = 0x0010,      // Server supports RFC 3920 SASL authentication
	AllowPlainAuth    = 0x0020,      // Allow plain password authentication
    };

    /**
     * Constructor. Construct a full server info object
     * @param name Server domain name
     * @param address IP address
     * @param port IP port
     * @param password  Component only: Password used for authentication
     * @param identity Component only: The stream identity used when connecting
     * @param fullidentity Component only: The user identity
     * @param flags Server flags
     */
    inline XMPPServerInfo(const char* name, const char* address, int port,
	const char* password, const char* identity, const char* fullidentity,
	int flags)
	: m_name(name), m_address(address), m_port(port), m_password(password),
	m_identity(identity), m_fullIdentity(fullidentity), m_flags(flags)
	{}

    /**
     * Constructor. Construct a partial server info object
     * @param name Server domain name
     * @param port IP port
     */
    inline XMPPServerInfo(const char* name, int port)
	: m_name(name), m_port(port)
	{}

    /**
     * Get the server's address
     * @return The server's address
     */
    inline const String& address() const
	{ return m_address; }

    /**
     * Get the server's domain name
     * @return The server's domain name
     */
    inline const String& name() const
	{ return m_name; }

    /**
     * Get the server's port used to connect to
     * @return The server's port used to connect to
     */
    inline const int port() const
	{ return m_port; }

    /**
     * Get the server's port used to connect to
     * @return The server's port used to connect to
     */
    inline const String& password() const
	{ return m_password; }

    /**
     * Get the server's identity
     * @return The server's identity
     */
    inline const String& identity() const
	{ return m_identity; }

    /**
     * Get the server's full identity
     * @return The server's full identity
     */
    inline const String& fullIdentity() const
	{ return m_fullIdentity; }

    /**
     * Check if a given flag (or mask) is set
     * @return True if the flag is set
     */
    inline bool flag(int mask) const
	{ return 0 != (m_flags & mask); }

    /**
     * Flag names dictionary
     */
    static TokenDict s_flagName[];

private:
    String m_name;                       // Domain name
    String m_address;                    // IP address
    int m_port;                          // Port
    String m_password;                   // Authentication data
    String m_identity;                   // Identity. Used for Jabber Component protocol
    String m_fullIdentity;               // Full identity for this server
    int m_flags;                         // Server flags
};


/**
 * This class holds the XMPP/JabberComponent/Jingle namespace enumerations and the associated strings
 * @short XMPP namespaces
 */
class YJINGLE_API XMPPNamespace
{
public:
    enum Type {
	Stream,                          // http://etherx.jabber.org/streams
	Client,                          // jabber:client
	Server,                          // jabber:server
	ComponentAccept,                 // jabber:component:accept
	ComponentConnect,                // jabber:component:connect
	StreamError,                     // urn:ietf:params:xml:ns:xmpp-streams
	StanzaError,                     // urn:ietf:params:xml:ns:xmpp-stanzas
	Register,                        // http://jabber.org/features/iq-register
	IqAuth,                          // jabber:iq:auth
	IqAuthFeature,                   // http://jabber.org/features/iq-auth
	Starttls,                        // urn:ietf:params:xml:ns:xmpp-tls
	Sasl,                            // urn:ietf:params:xml:ns:xmpp-sasl
	Session,                         // urn:ietf:params:xml:ns:xmpp-session
	Bind,                            // urn:ietf:params:xml:ns:xmpp-bind
	Roster,                          // jabber:iq:roster
	DiscoInfo,                       // http://jabber.org/protocol/disco#info
	DiscoItems,                      // http://jabber.org/protocol/disco#items
	Jingle,                          // http://www.google.com/session
	JingleAudio,                     // http://www.google.com/session/phone
	JingleTransport,                 // http://www.google.com/transport/p2p
	Dtmf,                            // http://jabber.org/protocol/jingle/info/dtmf
	DtmfError,                       // http://jabber.org/protocol/jingle/info/dtmf#errors
	Command,                         // http://jabber.org/protocol/command
	CapVoiceV1,                      // http://www.google.com/xmpp/protocol/voice/v1
	Count,
    };

    /**
     * Get the string representation of a namespace value
     */
    inline const char* operator[](Type index)
	{ return lookup(index,s_value); }

    /**
     * Check if a text is a known namespace
     */
    static bool isText(Type index, const char* txt);

    /**
     * Get the type associated with a given namespace text
     */
    static inline Type type(const char* txt) {
	    int tmp = lookup(txt,s_value,Count);
	    return tmp ? (Type)tmp : Count;
	}

private:
    static TokenDict s_value[];          // Namespace list
};


/**
 * This class holds the XMPP error type, error enumerations and associated strings
 * @short XMPP errors.
 */
class YJINGLE_API XMPPError
{
public:
    /**
     * Error condition enumeration
     */
    enum Type {
	NoError = 0,
	// Stream errors
	BadFormat,                       // bad-format
	BadNamespace,                    // bad-namespace-prefix
	ConnTimeout,                     // connection-timeout
	HostGone,                        // host-gone
	HostUnknown,                     // host-unknown
	BadAddressing,                   // improper-addressing
	Internal,                        // internal-server-error
	InvalidFrom,                     // invalid-from
	InvalidId,                       // invalid-id
	InvalidNamespace,                // invalid-namespace
	InvalidXml,                      // invalid-xml
	NotAuth,                         // not-authorized
	Policy,                          // policy-violation
	RemoteConn,                      // remote-connection-failed
	ResConstraint,                   // resource-constraint
	RestrictedXml,                   // restricted-xml
	SeeOther,                        // see-other-host
	Shutdown,                        // system-shutdown
	UndefinedCondition,              // undefined-condition
	UnsupportedEnc,                  // unsupported-encoding
	UnsupportedStanza,               // unsupported-stanza-type
	UnsupportedVersion,              // unsupported-version
	Xml,                             // xml-not-well-formed
	// Auth failures
	Aborted,                         // aborted
	IncorrectEnc,                    // incorrect-encoding
	InvalidAuth,                     // invalid-authzid
	InvalidMechanism,                // invalid-mechanism
	MechanismTooWeak,                // mechanism-too-weak
	NotAuthorized,                   // not-authorized
	TempAuthFailure,                 // temporary-auth-failure
	// Stanza errors
	SBadRequest,                     // bad-request
	SConflict,                       // conflict
	SFeatureNotImpl,                 // feature-not-implemented
	SForbidden,                      // forbidden
	SGone,                           // gone
	SInternal,                       // internal-server-error
	SItemNotFound,                   // item-not-found
	SBadJid,                         // jid-malformed
	SNotAcceptable,                  // not-acceptable
	SNotAllowed,                     // not-allowed
	SPayment,                        // payment-required
	SUnavailable,                    // recipient-unavailable
	SRedirect,                       // redirect
	SReg,                            // registration-required
	SNoRemote,                       // remote-server-not-found
	SRemoteTimeout,                  // remote-server-timeout
	SResource,                       // resource-constraint
	SServiceUnavailable,             // service-unavailable
	SSubscription,                   // subscription-required
	SUndefinedCondition,             // undefined-condition
	SRequest,                        // unexpected-request
	// Dtmf
	DtmfNoMethod,                    // unsupported-dtmf-method
	Count,
    };

    /**
     * Error type enumeration
     */
    enum ErrorType {
	TypeCancel = 1000,               // do not retry (the error is unrecoverable)
	TypeContinue,                    // proceed (the condition was only a warning)
	TypeModify,                      // retry after changing the data sent
	TypeAuth,                        // retry after providing credentials
	TypeWait,                        // retry after waiting (the error is temporary)
	TypeCount,
    };

    /**
     * Get the text representation of a given error value
     */
    inline const char* operator[](int index)
	{ return lookup(index,s_value); }

    /**
     * Check if a given text is a valid error
     */
    static bool isText(int index, const char* txt);

    /**
     * Get the type associated with a given error text
     */
    static inline int type(const char* txt)
	{ return lookup(txt,s_value,Count); }

private:
    static TokenDict s_value[];          // Error list
};


/**
 * This class holds a Jabber ID in form "node@domain/resource" or "node@domain"
 * @short A Jabber ID
 */
class YJINGLE_API JabberID : public String
{
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
     * Constructor. Constructs a JID from user, domain, resource
     * @param node The node
     * @param domain The domain
     * @param resource The resource
     */
    JabberID(const char* node, const char* domain, const char* resource = 0)
	{ set(node,domain,resource); }

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
     * Try to match another JID to this one. If src has a resource compare it too
     * (case sensitive). Otherwise compare just the bare JID (case insensitive)
     * @param src The JID to match
     * @return True if matched
     */
    inline bool match(const JabberID& src) const
	{ return (src.resource().null() || (resource() == src.resource())) && (bare() &= src.bare()); }

    /**
     * Equality operator. Do a case senitive resource comparison and a case insensitive bare jid comparison
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
 * @short A JID identity
 */
class YJINGLE_API JIDIdentity : public RefObject
{
public:
    /**
     * JID category enumeration
     */
    enum Category {
	Account,                         // account
	Client,                          // client
	Component,                       // component
	Gateway,                         // gateway
	CategoryUnknown
    };

    /**
     * JID type enumeration
     */
    enum Type {
	AccountRegistered,               // registered
	ClientPhone,                     // phone
	ComponentGeneric,                // generic
	ComponentPresence,               // presence
	GatewayGeneric,                  // generic
	TypeUnknown
    };

    /**
     * Constructor. Build a JID identity
     * @param c The JID's category
     * @param t The JID's type
     * @param name The name of this identity
     */
    inline JIDIdentity(Category c, Type t, const char* name = 0)
	: m_name(name), m_category(c), m_type(t)
	{}

    /**
     * Destructor
     */
    virtual ~JIDIdentity()
	{}

    /**
     * Build an XML element from this identity
     * @return A valid XML element
     */
    XMLElement* toXML();

    /**
     * Build this identity from an XML element
     * @return True on succes
     */
    bool fromXML(const XMLElement* element);

    /**
     * Get a string representation of this object
     * @return This object's name
     */
    virtual const String& toString() const
	{ return m_name; }

    /**
     * Get a pointer from this object
     * @param name The requested pointer's name
     * @return Requested pointer or 0
     */
    virtual void* getObject(const String& name) const {
	    if (name == "JIDIdentity")
		return (void*)this;
	    return RefObject::getObject(name);
	}

    /**
     * Set the name of this identity
     * @param name New identity name
     */
    inline void setName(const char* name)
	{ if (name) m_name = name; }

    /**
     * Lookup for a text associated with a given category
     * @return The category's name
     */
    static inline const char* categoryText(Category c)
	{ return lookup(c,s_category); }

    /**
     * Lookup for a value associated with a given category name
     * @return The category's value
     */
    static inline Category categoryValue(const char* c)
	{ return (Category)lookup(c,s_category,CategoryUnknown); }

    /**
     * Lookup for a text associated with a given category type
     * @return The category's type name
     */
    static inline const char* typeText(Type t)
	{ return lookup(t,s_type); }

    /**
     * Lookup for a value associated with a given category type
     * @return The category's type value
     */
    static inline Type typeValue(const char* t)
	{ return (Type)lookup(t,s_category,TypeUnknown); }

private:
    static TokenDict s_category[];
    static TokenDict s_type[];

    String m_name;
    Category m_category;                 // Category
    Type m_type;                         // Type
};


/**
 * This class holds a JID feature
 * @short A JID feature
 */
class YJINGLE_API JIDFeature : public RefObject
{
public:
    /**
     * Constructor
     * @param feature The feature to add
     * @param required True if this feature is required
     */
    inline JIDFeature(XMPPNamespace::Type feature, bool required = false)
	: m_feature(feature),
	m_required(required)
	{}

    /**
     * Destructor
     */
    virtual ~JIDFeature()
	{}

    /**
     * Check if this feature is a required one
     * @return True if this feature is a required one
     */
    inline bool required() const
	{ return m_required; }

    /**
     * XMPPNamespace::Type conversion operator
     */
    inline operator XMPPNamespace::Type()
	{ return m_feature; }

private:
    XMPPNamespace::Type m_feature;       // The feature
    bool m_required;                     // Required flag
};


/**
 * This class holds a JID SASL feature (authentication methods)
 * @short A JID's SASL feature
 */
class YJINGLE_API JIDFeatureSasl : public JIDFeature
{
public:
    /**
     * Mechanisms used to authenticate a stream
     */
    enum Mechanism {
	MechNone     = 0x00,             // No authentication mechanism
	MechMD5      = 0x01,             // MD5 digest
	MechSHA1     = 0x02,             // SHA1 digest
	MechPlain    = 0x04,             // Plain text password
    };

    /**
     * Constructor
     * @param mech Authentication mechanisms used by the JID
     * @param required Required flag
     */
    inline JIDFeatureSasl(int mech, bool required = false)
	: JIDFeature(XMPPNamespace::Sasl,required),
	m_mechanism(mech)
	{}

    /**
     * Get the authentication mechanisms used by the JID
     * @return The authentication mechanisms used by the JID
     */
    inline int mechanism() const
	{ return m_mechanism; }

    /**
     * Check if a given mechanism is allowed
     * @return True if the given mechanism is allowed
     */
    inline bool mechanism(Mechanism mech) const
	{ return 0 != (m_mechanism & mech); }

    /**
     * XMPPNamespace::Type conversion operator
     */
    inline operator XMPPNamespace::Type()
	{ return JIDFeature::operator XMPPNamespace::Type(); }

    /**
     * Authentication mechanism names
     */
    static TokenDict s_authMech[];

private:
    int m_mechanism;                     // Authentication mechanisms
};


/**
 * This class holds a list of JID features
 * @short JID feature list
 */
class YJINGLE_API JIDFeatureList
{
public:
    /**
     * Add a feature to the list
     * @param feature The feature to add
     * @param required True if this feature is required
     * @return False if the given feature already exists
     */
    inline bool add(XMPPNamespace::Type feature, bool required = false) {
	    if (get(feature))
		return false;
	    m_features.append(new JIDFeature(feature,required));
	    return true;
	}

    /**
     * Add a feature to the list. Destroy the received parameter if already in the list
     * @param feature The feature to add
     * @return False if the given feature already exists
     */
    inline bool add(JIDFeature* feature) {
	    if (!feature || get(*feature)) {
		TelEngine::destruct(feature);
		return false;
	    }
	    m_features.append(feature);
	    return true;
	}

    /**
     * Remove a feature from the list
     * @param feature The feature to remove
     */
    inline void remove(XMPPNamespace::Type feature)
	{ m_features.remove(get(feature),true); }

    /**
     * Get a feature from the list
     * @param feature The feature to get
     * @return Pointer to the feature or 0 if it doesn't exists
     */
    JIDFeature* get(XMPPNamespace::Type feature);

    /**
     * Add 'feature' children to the given element
     * @param element The target XMLElement
     * @return The given element
     */
    XMLElement* addTo(XMLElement* element);

    /**
     * Clear the feature list
     */
    inline void clear()
	{ m_features.clear(); }

private:
    ObjList m_features;                  // The features
};

/**
 * This class is a general XMPP utilities
 * @short General XMPP utilities
 */
class YJINGLE_API XMPPUtils
{
public:
    /**
     * Iq type enumeration
     */
    enum IqType {
	IqSet,                           // set
	IqGet,                           // get
	IqResult,                        // result
	IqError,                         // error
	IqCount,
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
     * Create an XML element with an 'xmlns' attribute
     * @param name Element's name
     * @param ns 'xmlns' attribute
     * @param text Optional text for the element
     * @return A valid XMLElement pointer
     */
    static XMLElement* createElement(const char* name, XMPPNamespace::Type ns,
	const char* text = 0);

    /**
     * Create an XML element with an 'xmlns' attribute
     * @param type Element's type
     * @param ns 'xmlns' attribute
     * @param text Optional text for the element
     * @return A valid XMLElement pointer
     */
    static XMLElement* createElement(XMLElement::Type type, XMPPNamespace::Type ns,
	const char* text = 0);

    /**
     * Create an 'iq' element
     * @param type Iq type as enumeration
     * @param from The 'from' attribute
     * @param to The 'to' attribute
     * @param id The 'id' attribute
     * @return A valid XMLElement pointer
     */
    static XMLElement* createIq(IqType type, const char* from,
	const char* to, const char* id);

    /**
     * Create an 'iq' element with a 'bind' child containing the resources
     * @param from The 'from' attribute
     * @param to The 'to' attribute
     * @param id The 'id' attribute
     * @param resources The resources to bind (strings)
     * @return A valid XMLElement pointer
     */
    static XMLElement* createIqBind(const char* from,
	const char* to, const char* id, const ObjList& resources);

    /**
     * Create a 'command' element
     * @param action The command action
     * @param node The command
     * @param sessionId Optional session ID for the command
     * @return A valid XMLElement pointer
     */
    static XMLElement* createCommand(CommandAction action, const char* node,
	const char* sessionId = 0);

    /**
     * Create an 'identity' element
     * @param category The 'category' attribute
     * @param type The 'type' attribute
     * @param name The 'name' attribute
     * @return A valid XMLElement pointer
     */
    static XMLElement* createIdentity(const char* category,
	const char* type, const char* name);

    /**
     * Create an 'iq' of type 'get' element with a 'query' child
     * @param from The 'from' attribute
     * @param to The 'to' attribute
     * @param id The 'id' attribute
     * @param info True to create a query info request. False to create a query items request
     * @return A valid XMLElement pointer
     */
    static XMLElement* createIqDisco(const char* from, const char* to,
	const char* id, bool info = true);

    /**
     * Create a 'error' element
     * @param type Error type
     * @param error The error
     * @param text Optional text to add to the error element
     * @return A valid XMLElement pointer
     */
    static XMLElement* createError(XMPPError::ErrorType type,
	XMPPError::Type error, const char* text = 0);

    /**
     * Create an error from a received element. Consume the received element
     * Reverse 'to' and 'from' attributes
     * @param xml Received element
     * @param type Error type
     * @param error The error
     * @param text Optional text to add to the error element
     * @return A valid XMLElement pointer or 0 if xml is 0
     */
    static XMLElement* createError(XMLElement* xml, XMPPError::ErrorType type,
	XMPPError::Type error, const char* text = 0);

    /**
     * Create a 'stream:error' element
     * @param error The XMPP defined condition
     * @param text Optional text to add to the error
     * @return A valid XMLElement pointer
     */
    static XMLElement* createStreamError(XMPPError::Type error,
	const char* text = 0);

    /**
     * Check if the given element has an attribute 'xmlns' equal to a given value
     * @param element Element to check
     * @param ns Namespace value to check
     * @return True if the given element has the requested namespace
     */
    static bool hasXmlns(XMLElement& element, XMPPNamespace::Type ns);

    /**
     * Decode a received stream error or stanza error
     * @param element The received element
     * @param error The error condition
     * @param text The stanza's error or error text
     */
    static void decodeError(XMLElement* element, String& error, String& text);

    /**
     * Print an XMLElement to a string
     * @param xmlStr The destination string
     * @param element The element to print
     * @param indent The indent. 0 if it is the root element
     */
    static void print(String& xmlStr, XMLElement& element, const char* indent = 0);

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
     * Get the type of an 'iq' stanza as enumeration
     * @param text The text to check
     * @return Iq type as enumeration
     */
    static inline IqType iqType(const char* text)
	{ return (IqType)lookup(text,s_iq,IqCount); }

    /**
     * Keep the types of 'iq' stanzas
     */
    static TokenDict s_iq[];

    /**
     * Keep the command actions
     */
    static TokenDict s_commandAction[];

    /**
     * Keep the command status
     */
    static TokenDict s_commandStatus[];
};

};

#endif /* __XMPPUTILS_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
