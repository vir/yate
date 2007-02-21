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
class JIDIdentity;
class JIDFeature;
class JIDFeatureList;
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
	Dtmf,                            // http://jabber.org/protocol/jingle/info/dtmf
	DtmfError,                       // http://jabber.org/protocol/jingle/info/dtmf#errors
	Command,                         // http://jabber.org/protocol/command
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
	// Dtmf
	DtmfNoMethod,                    // unsupported-dtmf-method
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

    /**
     * Check if the given string contains valid characters.
     * @param value The string to check.
     * @return True if value is valid or 0. False if value is a non empty invalid string.
     */
    static bool valid(const String& value);

    /**
     * Keep the regexp used to check the validity of a string.
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
 * This class holds an identity for a JID.
 * @short JID identity.
 */
class YJINGLE_API JIDIdentity : public String, public RefObject
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
	: String(name), m_category(c), m_type(t)
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
};

/**
 * This class holds a JID feature.
 * @short JID feature.
 */
class YJINGLE_API JIDFeature : public RefObject
{
public:
    /**
     * Constructor.
     */
    inline JIDFeature(XMPPNamespace::Type feature)
	: m_feature(feature)
	{}

    /**
     * Destructor.
     */
    virtual ~JIDFeature()
	{}

    /**
     * XMPPNamespace::Type conversion operator.
     */
    inline operator XMPPNamespace::Type()
	{ return m_feature; }

private:
    XMPPNamespace::Type m_feature;       // The feature
};

/**
 * This class holds a list of features.
 * @short Feature list.
 */
class YJINGLE_API JIDFeatureList
{
public:
    /**
     * Add a feature to the list.
     * @param feature The feature to add.
     * @return False if the given feature already exists.
     */
    inline bool add(XMPPNamespace::Type feature) {
	    if (get(feature))
		return false;
	    m_features.append(new JIDFeature(feature));
	    return true;
	}

    /**
     * Remove a feature from the list.
     * @param feature The feature to remove.
     */
    inline void remove(XMPPNamespace::Type feature)
	{ m_features.remove(get(feature),true); }

    /**
     * Get a feature from the list.
     * @param feature The feature to get.
     * @return Pointer to the feature or 0 if it doesn't exists.
     */
    JIDFeature* get(XMPPNamespace::Type feature);

    /**
     * Add 'feature' children to the given element.
     * @param element The target XMLElement.
     * @return The given element.
     */
    XMLElement* addTo(XMLElement* element);

private:
    ObjList m_features;                  // The features
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
     * Command action enumeration.
     */
    enum CommandAction {
	CommExecute,
	CommCancel,
	CommPrev,
	CommNext,
	CommComplete,
    };

    /**
     * Command status enumeration.
     */
    enum CommandStatus {
	CommExecuting,
	CommCompleted,
	CommCancelled,
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
     * Create a 'command' element
     * @param action The command action.
     * @param node The command.
     * @param sessionId Optional session ID for the command.
     * @return A valid XMLElement pointer.
     */
    static XMLElement* createCommand(CommandAction action, const char* node,
	const char* sessionId = 0);

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
     * Split a string at a delimiter character and fills a named list with its parts.
     * Skip empty parts.
     * @param dest The destination NamedList.
     * @param src Pointer to the string.
     * @param sep The delimiter.
     * @param indent True to add the parts as name and index as value.
     *  False to do the other way.
     */
    static bool split(NamedList& dest, const char* src, const char sep,
	bool nameFirst);

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
