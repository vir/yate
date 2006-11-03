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

class XMPPNamespace;
class XMPPErrorCode;
class XMPPError;
class JabberID;
class JIDFeatures;
class JIDResource;
class JIDResourceList;
class XMPPUtils;

/**
 * This class holds the XMPP/JabberComponent/Jingle namespace enumerations and the associated strings.
 * @short XMPP namespaces.
 */
class YJINGLE_API XMPPNamespace
{
public:
    enum Type {
	Stream,                          // http://etherx.jabber.org/streams
	ComponentAccept,                 // jabber:component:accept
	ComponentConnect,                // jabber:component:connect
	StreamError,                     // urn:ietf:params:xml:ns:xmpp-streams
	StanzaError,                     // urn:ietf:params:xml:ns:xmpp-stanzas
	Bind,                            // urn:ietf:params:xml:ns:xmpp-bind
	DiscoInfo,                       // http://jabber.org/protocol/disco#info
	DiscoItems,                      // http://jabber.org/protocol/disco#items
	Jingle,                          // http://www.google.com/session
	JingleAudio,                     // http://www.google.com/session/phone
	JingleTransport,                 // http://www.google.com/transport/p2p
	Count,
    };

    inline const char* operator[](Type index)
	{ return lookup(index,s_value); }

    static bool isText(Type index, const char* txt);

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
    // Error type
    enum ErrorType {
	TypeCancel = 0,                  // do not retry (the error is unrecoverable)
	TypeContinue,                    // proceed (the condition was only a warning)
	TypeModify,                      // retry after changing the data sent
	TypeAuth,                        // retry after providing credentials
	TypeWait,                        // retry after waiting (the error is temporary)
	TypeCount,
    };

    enum Type {
	// Stream errors
	BadFormat = TypeCount + 1,       // bad-format
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
	SNotAuth,                        // not-authorized
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
	Count,
    };

    inline const char* operator[](int index)
	{ return lookup(index,s_value); }

    static bool isText(int index, const char* txt);

    static inline int type(const char* txt)
	{ return lookup(txt,s_value,Count); }

private:
    static TokenDict s_value[];          // Error list
};

/**
 * This class holds a Jabber ID in form 'node@domain/resource' or 'node@domain'.
 * @short A Jabber ID.
 */
class YJINGLE_API JabberID : public String
{
public:
    /**
     * Constructor.
     */
    inline JabberID() {}

    /**
     * Constructor.
     * Constructs a JID from a given string.
     * @param jid The JID string.
     */
    inline JabberID(const char* jid)
	{ set(jid); }

    /**
     * Constructor.
     * Constructs a JID from user, domain, resource.
     * @param node The node.
     * @param domain The domain.
     * @param resource The resource.
     */
    JabberID(const char* node, const char* domain, const char* resource = 0)
	{ set(node,domain,resource); }

    /**
     * Get the node part of the JID.
     * @return The node part of the JID.
     */
    inline const String& node() const
	{ return m_node; }

    /**
     * Get the bare JID (node@domain).
     * @return The bare JID.
     */
    inline const String& bare() const
	{ return m_bare; }

    /**
     * Get the domain part of the JID.
     * @return The domain part of the JID.
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
     * Get the resource part of the JID.
     * @return The resource part of the JID.
     */
    inline const String& resource() const
	{ return m_resource; }

    /**
     * Try to match another JID to this one. If src has a resource compare with
     *  the full JID. Otherwise compare the bare JID.
     * @param src The JID to match.
     * @return True if matched.
     */
    inline bool match(const JabberID& src) const
	{ return (src.resource() ? src == *this : bare() == src.bare()); }

    /**
     * Set the resource part of the JID.
     * @param d The new resource part of the JID.
     */
    inline void resource(const char* r)
	{ set(m_node.c_str(),m_domain.c_str(),r); }

    /**
     * Set the data.
     * @param jid The JID string to assign.
     */
    void set(const char* jid);

    /**
     * Set the data.
     * @param node The node.
     * @param domain The domain.
     * @param resource The resource.
     */
    void set(const char* node, const char* domain, const char* resource = 0);

private:
    void parse();                        // Parse the string. Set the data 

    String m_node;                       // The node part
    String m_domain;                     // The domain part
    String m_resource;                   // The resource part
    String m_bare;                       // The bare JID: node@domain
};

/**
 * This class holds an identity list for a JID as described in XEP-0030.
 * @short JID identity.
 */
class YJINGLE_API JIDIdentity : public RefObject
{
public:
    enum Category {
	Account,                         // account
	Client,                          // client
	Component,                       // component
	Gateway,                         // gateway
	CategoryUnknown
    };

    enum Type {
	AccountRegistered,               // registered
	ClientPhone,                     // phone
	ComponentGeneric,                // generic
	ComponentPresence,               // presence
	GatewayGeneric,                  // generic
	TypeUnknown
    };

    inline JIDIdentity(Category c, Type t, const char* name = 0)
	: m_category(c), m_type(t), m_name(name)
	{}

    virtual ~JIDIdentity()
	{}

    XMLElement* toXML();

    bool fromXML(const XMLElement* element);

    static inline const char* categoryText(Category c)
	{ return lookup(c,s_category); }

    static inline Category categoryValue(const char* c)
	{ return (Category)lookup(c,s_category,CategoryUnknown); }

    static inline const char* typeText(Type t)
	{ return lookup(t,s_type); }

    static inline Type typeValue(const char* t)
	{ return (Type)lookup(t,s_category,TypeUnknown); }

private:
    static TokenDict s_category[];
    static TokenDict s_type[];

    Category m_category;                 // Category
    Type m_type;                         // Type
    String m_name;                       // Name
};

/**
 * This class holds a features list for a JID.
 * @short JID features.
 */
class YJINGLE_API JIDFeatures : public RefObject
{
public:
    /**
     * Constructor.
     */
    inline JIDFeatures()
	: m_features(0), m_count(0)
	{}

    /**
     * Destructor.
     * Delete the features list.
     */
    virtual ~JIDFeatures()
	{ release(); }

    /**
     * Init from a 'query' element.
     * @param element The query element.
     * @return False if the element is not a query one or has an incorrect namespace.
     */
    bool create(XMLElement* element);

    /**
     * Init from a list of features.
     * @param features The features list.
     * @param count List count.
     * @param copy True to copy the list. False to keep the pointer and length.
     */
    void create(XMPPNamespace::Type* features, u_int32_t count, bool copy = true);

    /**
     * Add 'feature' children to the given element.
     * @param element The target XMLElement.
     * @return The given element.
     */
    XMLElement* addTo(XMLElement* element);

    /**
     * Create a 'query' element from this object.
     * @return A valid XMLElement pointer.
     */
    XMLElement* query();

    /**
     * Create an 'iq' element from this object.
     * @param from The 'from' attribute.
     * @param from The 'to' attribute.
     * @param id The 'id' attribute.
     * @param get The 'type' attribute (True for 'get', false for "result").
     * @return A valid XMLElement pointer.
     */
    XMLElement* iq(const char* from, const char* to, const char* id, bool get = true);

protected:
    /**
     * Release the allocated memory. Reset count.
     */
    inline void release() {
	if (m_features) {
	    delete[] m_features;
	    m_features = 0;
	    m_count = 0;
	}
    }

private:
    XMPPNamespace::Type* m_features;     // The features
    u_int32_t m_count;                   // Feature count
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
	CapNone                =  0,     // No capability
	CapAudio               =  1,     // Jingle capability
	CapChat                =  2,     // Message capability
    };

    /**
     * Resource presence enumeration.
     */
    enum Presence {
	Unknown,
	Available,
	Unavailable,
    };

    /**
     * Constructor. Set data members.
     * @param name The resource name.
     * @param presence The resource presence.
     * @param capability The resource capability.
     */
    inline JIDResource(const char* name, Presence presence = Unknown,
	u_int32_t capability = CapNone)
	: m_name(name), m_presence(presence), m_capability(capability)
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
     * Check if the resource is available.
     * @return True if the resource is available.
     */
    inline bool available() const
	{ return (m_presence == Available); }

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
	{ return (m_capability & capability); }

private:
    String m_name;                       // Resource name
    Presence m_presence;                 // Resorce presence
    u_int32_t m_capability;              // Resource capabilities
};

/**
 * This class holds resource list.
 * @short A resource list.
 */
class YJINGLE_API JIDResourceList : public Mutex
{
public:
    /**
     * Add a resource to the list. If a resource with the given name
     *  already exists modify it's presence.
     * @param name The resource name.
     * @param presence The resource presence.
     * @param capability The resource capability.
     * @return False if the the resource was added or the result of presence change.
     */
    bool add(const String& name, JIDResource::Presence presence,
	u_int32_t caps);

    /**
     * Remove a resource from the list.
     * @param name The resource name.
     * @param del True to delete the resource.
     */
    void remove(const String& name, bool del = true);

    /**
     * Remove all resources.
     */
    void clear();

    /**
     * Get a resource with the given name.
     * @param name The resource name.
     * @return A referenced pointer to the resource or 0.
     */
    JIDResource* get(const String& name);

    /**
     * Get the first resource with audio capability.
     * @return A referenced pointer to the resource or 0.
     */
    JIDResource* getAudio();

private:
    ObjList m_resource;                  // The resources list
};

/**
 * This class holds a remote XMPP user along with his resources and subscribe state.
 * @short An XMPP remote user.
 */
class YJINGLE_API XMPPUser : public RefObject, public Mutex
{
public:
    enum Subscription {
	None = 0,
	To   = 1,
	From = 2,
	Both = 3,
    };

    XMPPUser(const char* node, const char* domain, Subscription sub);

    virtual ~XMPPUser();

    const JabberID& jid() const
	{ return m_jid; }

    JIDResourceList& resources()
	{ return m_resource; }

    inline bool subscribedTo() const
	{ return (m_subscription & To); }

    inline bool subscribedFrom() const
	{ return (m_subscription & From); }

private:
    JabberID m_jid;                      // User's JID
    JIDResourceList m_resource;          // Resources
    u_int8_t m_subscription;             // Subscription state
};

/**
 * This class holds the roster for a local user.
 * @short The roster of a local user.
 */
class YJINGLE_API XMPPUserRoster : public RefObject, public Mutex
{
public:
    XMPPUserRoster(const char* node, const char* domain);

    virtual ~XMPPUserRoster();

    const JabberID& jid() const
	{ return m_jid; }

    void addUser(const char* bareJID);

    JIDResourceList& resources()
	{ return m_resource; }

private:
    JabberID m_jid;                      // User's JID
    JIDResourceList m_resource;          // Resources
    ObjList m_remote;                    // Remote users
};

/**
 * This class is a general XMPP utilities.
 * @short General XMPP utilities.
 */
class YJINGLE_API XMPPUtils
{
public:
    /**
     * Iq type enumeration.
     */
    enum IqType {
	IqSet,                           // set
	IqGet,                           // get
	IqResult,                        // result
	IqError,                         // error
	IqCount,
    };

    /**
     * Message type enumeration.
     */
    enum MsgType {
	MsgChat,                         // chat
	MsgCount,
    };

    /**
     * Create an XML element with an 'xmlns' attribute.
     * @param name Element's name.
     * @param ns 'xmlns' attribute.
     * @param text Optional text for the element.
     * @return A valid XMLElement pointer.
     */
    static XMLElement* createElement(const char* name, XMPPNamespace::Type ns,
	const char* text = 0);

    /**
     * Create an XML element with an 'xmlns' attribute.
     * @param type Element's type.
     * @param ns 'xmlns' attribute.
     * @param text Optional text for the element.
     * @return A valid XMLElement pointer.
     */
    static XMLElement* createElement(XMLElement::Type type, XMPPNamespace::Type ns,
	const char* text = 0);

    /**
     * Create a 'message' element.
     * @param type Message type as enumeration
     * @param from The 'from' attribute.
     * @param to The 'to' attribute.
     * @param id The 'id' attribute.
     * @param message The message body.
     * @return A valid XMLElement pointer.
     */
    static XMLElement* createMessage(MsgType type, const char* from,
	const char* to, const char* id, const char* message);

    /**
     * Create an 'iq' element.
     * @param type Iq type as enumeration
     * @param from The 'from' attribute.
     * @param to The 'to' attribute.
     * @param id The 'id' attribute.
     * @return A valid XMLElement pointer.
     */
    static XMLElement* createIq(IqType type, const char* from,
	const char* to, const char* id);

    /**
     * Create an 'iq' element with a 'bind' child containing the resources.
     * @param from The 'from' attribute.
     * @param to The 'to' attribute.
     * @param id The 'id' attribute.
     * @param resources The resources to bind (strings).
     * @return A valid XMLElement pointer.
     */
    static XMLElement* createIqBind(const char* from,
	const char* to, const char* id, const ObjList& resources);

    /**
     * Create an 'identity' element.
     * @param category The 'category' attribute.
     * @param type The 'type' attribute.
     * @param name The 'name' attribute.
     * @return A valid XMLElement pointer.
     */
    static XMLElement* createIdentity(const char* category,
	const char* type, const char* name);

    /**
     * Create an 'iq' of type 'get' element with a 'query' child.
     * @param from The 'from' attribute.
     * @param to The 'to' attribute.
     * @param id The 'id' attribute.
     * @param info True to create a query info request. False to create a query items request.
     * @return A valid XMLElement pointer.
     */
    static XMLElement* createIqDisco(const char* from, const char* to,
	const char* id, bool info = true);

    /**
     * Create a 'error' element.
     * @param type Error type.
     * @param error The error.
     * @param text Optional text to add to the error element.
     * @return A valid XMLElement pointer.
     */
    static XMLElement* createError(XMPPError::ErrorType type,
	XMPPError::Type error, const char* text = 0);

    /**
     * Create a 'stream:error' element.
     * @param error The XMPP defined condition.
     * @param text Optional text to add to the error.
     * @return A valid XMLElement pointer.
     */
    static XMLElement* createStreamError(XMPPError::Type error,
	const char* text = 0);

    /**
     * Print an XMLElement to a string.
     * @param xmlStr The destination string.
     * @param element The element to print.
     * @param indent The indent. 0 if it is the root element.
     */
    static void print(String& xmlStr, XMLElement* element, const char* indent = 0);

    /**
     * Get the type of an 'iq' stanza as enumeration.
     * @param text The text to check.
     * @return Iq type as enumeration.
     */
    static inline IqType iqType(const char* txt)
	{ return (IqType)lookup(txt,s_iq,IqCount); }

    /**
     * Keep the types of 'iq' stanzas.
     */
    static TokenDict s_iq[];

    /**
     * Keep the types of 'message' stanzas.
     */
    static TokenDict s_msg[];
};

};

#endif /* __XMPPUTILS_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
