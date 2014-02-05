/**
 * xmpputils.cpp
 * Yet Another Jabber Component Protocol Stack
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
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

#include <xmpputils.h>
#include <string.h>
#include <time.h>

using namespace TelEngine;

static const JabberID s_emptyJid;
static String s_auth[] = {"password", "auth", ""};

const String XMPPNamespace::s_array[Count] = {
    "http://etherx.jabber.org/streams",                    // Stream
    "jabber:client",                                       // Client
    "jabber:server",                                       // Server
    "jabber:server:dialback",                              // Dialback
    "urn:ietf:params:xml:ns:xmpp-streams",                 // StreamError
    "urn:ietf:params:xml:ns:xmpp-stanzas",                 // StanzaError
    "urn:xmpp:ping",                                       // Ping
    "http://jabber.org/features/iq-register",              // Register
    "jabber:iq:register",                                  // IqRegister
    "jabber:iq:private",                                   // IqPrivate
    "jabber:iq:auth",                                      // IqAuth
    "http://jabber.org/features/iq-auth",                  // IqAuthFeature
    "jabber:iq:version",                                   // IqVersion
    "urn:xmpp:delay",                                      // Delay
    "urn:ietf:params:xml:ns:xmpp-tls",                     // Tls
    "urn:ietf:params:xml:ns:xmpp-sasl",                    // Sasl
    "urn:ietf:params:xml:ns:xmpp-session",                 // Session
    "urn:ietf:params:xml:ns:xmpp-bind",                    // Bind
    "jabber:iq:roster",                                    // Roster
    "jabber:iq:roster-dynamic",                            // DynamicRoster
    "http://jabber.org/protocol/disco#info",               // DiscoInfo
    "http://jabber.org/protocol/disco#items",              // DiscoItems
    "http://jabber.org/protocol/caps",                     // EntityCaps
    "vcard-temp",                                          // VCard
    "http://jabber.org/protocol/si/profile/file-transfer", // SIProfileFileTransfer
    "http://jabber.org/protocol/bytestreams",              // ByteStreams
#if 0
    "urn:xmpp:jingle:0",                                   // Jingle
    "urn:xmpp:jingle:errors:0",                            // JingleError
    "urn:xmpp:jingle:apps:rtp:0",                          // JingleAppsRtp
    "urn:xmpp:jingle:apps:rtp:errors:0",                   // JingleAppsRtpError
    "urn:xmpp:jingle:apps:rtp:info:0",                     // JingleAppsRtpInfo
    "urn:xmpp:jingle:apps:rtp:audio",                      // JingleAppsRtpAudio
    "urn:xmpp:jingle:apps:file-transfer:0",                // JingleAppsFileTransfer
    "urn:xmpp:jingle:transports:ice-udp:0",                // JingleTransportIceUdp
    "urn:xmpp:jingle:transports:raw-udp:0",                // JingleTransportRawUdp
    "urn:xmpp:jingle:transports:raw-udp:info:0",           // JingleTransportRawUdpInfo
    "urn:xmpp:jingle:transports:bytestreams:0",            // JingleTransportByteStreams
    "urn:xmpp:jingle:transfer:0",                          // JingleTransfer
    "urn:xmpp:jingle:dtmf:0",                              // Dtmf
#else
    "urn:xmpp:jingle:1",                                   // Jingle
    "urn:xmpp:jingle:errors:1",                            // JingleError
    "urn:xmpp:jingle:apps:rtp:1",                          // JingleAppsRtp
    "urn:xmpp:jingle:apps:rtp:errors:1",                   // JingleAppsRtpError
    "urn:xmpp:jingle:apps:rtp:info:1",                     // JingleAppsRtpInfo
    "urn:xmpp:jingle:apps:rtp:audio",                      // JingleAppsRtpAudio
    "urn:xmpp:jingle:apps:file-transfer:1",                // JingleAppsFileTransfer
    "urn:xmpp:jingle:transports:ice-udp:1",                // JingleTransportIceUdp
    "urn:xmpp:jingle:transports:raw-udp:1",                // JingleTransportRawUdp
    "urn:xmpp:jingle:transports:raw-udp:info:1",           // JingleTransportRawUdpInfo
    "urn:xmpp:jingle:transports:bytestreams:1",            // JingleTransportByteStreams
    "urn:xmpp:jingle:transfer:0",                          // JingleTransfer
    "urn:xmpp:jingle:dtmf:0",                              // JingleDtmf
#endif
    "http://www.google.com/session",                       // JingleSession
    "http://www.google.com/session/phone",                 // JingleAudio
    "http://www.google.com/transport/p2p",                 // JingleTransport
    "http://www.google.com/xmpp/protocol/voice/v1",        // JingleVoiceV1
    "urn:xmpp:jingle:apps:rtp:info",                       // JingleRtpInfoOld
    "http://jabber.org/protocol/jingle/info/dtmf",         // DtmfOld
    "jabber:x:oob",                                        // XOob
    "http://jabber.org/protocol/command",                  // Command
    "msgoffline",                                          // MsgOffline
    "jabber:component:accept",                             // ComponentAccept
    "http://jabber.org/protocol/muc",                      // Muc
    "http://jabber.org/protocol/muc#admin",                // MucAdmin
    "http://jabber.org/protocol/muc#owner",                // MucOwner
    "http://jabber.org/protocol/muc#user",                 // MucUser
    "urn:xmpp:features:dialback",                          // DialbackFeature
    "http://jabber.org/protocol/compress",                 // Compress
    "http://jabber.org/features/compress",                 // CompressFeature
    "jabber:x:data",                                       // XData
    "http://jabber.org/protocol/chatstates",               // ChatStates
    "http://yate.null.ro/yate/cluster",                    // YateCluster
    "http://www.google.com/transport/raw-udp",             // JingleTransportGoogleRawUdp
    "urn:xmpp:hashes:1",                                   // Hash
    "http://jabber.org/protocol/rsm",                      // ResultSetMngt
    "urn:xmpp.mam",                                        // FileInfoShare
};

const String XMPPError::s_array[Count] = {
    "",                                  // NoError
    "bad-format",                        // BadFormat
    "bad-namespace-prefix",              // BadNamespace
    "conflict",                          // Conflict
    "connection-timeout",                // ConnTimeout
    "host-gone",                         // HostGone
    "host-unknown",                      // HostUnknown
    "improper-addressing",               // BadAddressing
    "internal-server-error",             // Internal
    "invalid-from",                      // InvalidFrom
    "invalid-id",                        // InvalidId
    "invalid-namespace",                 // InvalidNamespace
    "invalid-xml",                       // InvalidXml
    "not-authorized",                    // NotAuth
    "policy-violation",                  // Policy
    "remote-connection-failed",          // RemoteConn
    "resource-constraint",               // ResConstraint
    "restricted-xml",                    // RestrictedXml
    "see-other-host",                    // SeeOther
    "system-shutdown",                   // Shutdown
    "undefined-condition",               // UndefinedCondition
    "unsupported-encoding",              // UnsupportedEnc
    "unsupported-stanza-type",           // UnsupportedStanza
    "unsupported-version",               // UnsupportedVersion
    "xml-not-well-formed",               // Xml
    "aborted",                           // Aborted
    "account-disabled",                  // AccountDisabled
    "credentials-expired",               // CredentialsExpired
    "encryption-required",               // EncryptionRequired
    "incorrect-encoding",                // IncorrectEnc
    "invalid-authzid",                   // InvalidAuth
    "invalid-mechanism",                 // InvalidMechanism
    "malformed-request",                 // MalformedRequest
    "mechanism-too-weak",                // MechanismTooWeak
    "not-authorized",                    // NotAuthorized
    "temporary-auth-failure",            // TempAuthFailure
    "transition-needed",                 // TransitionNeeded
    "resource-constraint",               // ResourceConstraint
    "not-allowed",                       // NotAllowed
    "bad-request",                       // BadRequest
    "feature-not-implemented",           // FeatureNotImpl
    "forbidden",                         // Forbidden
    "gone",                              // Gone
    "item-not-found",                    // ItemNotFound
    "jid-malformed",                     // BadJid
    "not-acceptable",                    // NotAcceptable
    "payment-required",                  // Payment
    "recipient-unavailable",             // Unavailable
    "redirect",                          // Redirect
    "registration-required",             // Reg
    "remote-server-not-found",           // NoRemote
    "remote-server-timeout",             // RemoteTimeout
    "service-unavailable",               // ServiceUnavailable
    "subscription-required",             // Subscription
    "unexpected-request",                // Request
    "",                                  // SocketError
    "unsupported-method",                // UnsupportedMethod
    "setup-failed",                      // SetupFailed
    "cancel",                            // TypeCancel
    "continue",                          // TypeContinue
    "modify",                            // TypeModify
    "auth",                              // TypeAuth
    "wait",                              // TypeWait
};

const String XmlTag::s_array[Count] = {
    "stream",                            // Stream
    "error",                             // Error
    "features",                          // Features
    "register",                          // Register
    "starttls",                          // Starttls
    "auth",                              // Auth
    "challenge",                         // Challenge
    "abort",                             // Abort
    "aborted",                           // Aborted
    "response",                          // Response
    "proceed",                           // Proceed
    "success",                           // Success
    "failure",                           // Failure
    "mechanisms",                        // Mechanisms
    "mechanism",                         // Mechanism
    "session",                           // Session
    "iq",                                // Iq
    "message",                           // Message
    "presence",                          // Presence
    "query",                             // Query
    "vCard",                             // VCard
    "jingle",                            // Jingle
    "description",                       // Description
    "payload-type",                      // PayloadType
    "transport",                         // Transport
    "candidate",                         // Candidate
    "body",                              // Body
    "subject",                           // Subject
    "feature",                           // Feature
    "bind",                              // Bind
    "resource",                          // Resource
    "transfer",                          // Transfer
    "hold",                              // Hold
    "active",                            // Active
    "ringing",                           // Ringing
    "mute",                              // Mute
    "registered",                        // Registered
    "remove",                            // Remove
    "jid",                               // Jid
    "username",                          // Username
    "password",                          // Password
    "digest",                            // Digest
    "required",                          // Required
    "optional",                          // Optional
    "dtmf",                              // Dtmf
    "dtmf-method",                       // DtmfMethod
    "command",                           // Command
    "text",                              // Text
    "item",                              // Item
    "group",                             // Group
    "reason",                            // Reason
    "content",                           // Content
    "trying",                            // Trying
    "received",                          // Received
    "file",                              // File
    "offer",                             // Offer
    "request",                           // Request
    "streamhost",                        // StreamHost
    "streamhost-used",                   // StreamHostUsed
    "ping",                              // Ping
    "encryption",                        // Encryption
    "crypto",                            // Crypto
    "parameter",                         // Parameter
    "identity",                          // Identity
    "priority",                          // Priority
    "c",                                 // EntityCapsTag
    "handshake",                         // Handshake
    "dialback",                          // Dialback
    "method",                            // Method
    "compress",                          // Compress
    "compressed",                        // Compressed
    "compression",                       // Compression
    "x",                                 // X
    "hash",                              // Hash
    "algo",                              // Algo
    "size",                              // Size
    "date",                              // Date
    "desc",                              // Desc
    "set",                               // Set
    "after",                             // After
    "before",                            // Before
    "count",                             // CountTag
    "first",                             // First
    "index",                             // Index
    "last",                              // Last
    "max",                               // Max
    "match",                             // Match
    "directory",                         // Directory
    "name",                              // Name
    "changed",                           // Changed
};

XMPPNamespace XMPPUtils::s_ns;
XMPPError XMPPUtils::s_error;
XmlTag XMPPUtils::s_tag;

const TokenDict XMPPUtils::s_presence[] = {
    {"probe",         Probe},
    {"subscribe",     Subscribe},
    {"subscribed",    Subscribed},
    {"unavailable",   Unavailable},
    {"unsubscribe",   Unsubscribe},
    {"unsubscribed",  Unsubscribed},
    {"error",         PresenceError},
    {0,0}
};

const TokenDict XMPPUtils::s_msg[] = {
    {"chat",      Chat},
    {"groupchat", GroupChat},
    {"headline",  HeadLine},
    {"normal",    Normal},
    {"error",     MsgError},
    {0,0}
};

const TokenDict XMPPUtils::s_iq[] = {
    {"set",     IqSet},
    {"get",     IqGet},
    {"result",  IqResult},
    {"error",   IqError},
    {0,0}
};

const TokenDict XMPPUtils::s_commandAction[] = {
    {"execute",  CommExecute},
    {"cancel",   CommCancel},
    {"prev",     CommPrev},
    {"next",     CommNext},
    {"complete", CommComplete},
    {0,0}
};

const TokenDict XMPPUtils::s_commandStatus[] = {
    {"executing", CommExecuting},
    {"completed", CommCompleted},
    {"cancelled", CommCancelled},
    {0,0}
};

const TokenDict XMPPUtils::s_authMeth[] = {
    {"DIGEST-SHA1", AuthSHA1},
    {"DIGEST-MD5",  AuthMD5},
    {"PLAIN",       AuthPlain},
    {"DIALBACK",    AuthDialback},
    {0,0}
};

const TokenDict XMPPDirVal::s_names[] = {
    {"none",        None},
    {"to",          To},
    {"from",        From},
    {"pending_in",  PendingIn},
    {"pending_out", PendingOut},
    {0,0},
};


/*
 * JabberID
 */
// Assignement operator from JabberID
JabberID& JabberID::operator=(const JabberID& src)
{
    assign(src.c_str());
    m_node = src.node();
    m_domain = src.domain();
    m_resource = src.resource();
    m_bare = src.bare();
    return *this;
}

void JabberID::set(const char* jid)
{
    this->assign(jid);
    parse();
}

void JabberID::set(const char* node, const char* domain, const char* resource)
{
    m_node = node;
    m_domain = domain;
    m_resource = resource;
    normalize();
}

bool JabberID::valid(const String& value)
{
    if (value.null())
	return true;
    return s_regExpValid.matches(value);
}

Regexp JabberID::s_regExpValid("^\\([[:alnum:]]*\\)");

#if 0
~`!#$%^*_-+=()[]{}|\;?.
#endif

void JabberID::parse()
{
    String tmp = *this;
    int i = tmp.find('@');
    if (i == -1)
	m_node = "";
    else {
	m_node = tmp.substr(0,i);
	tmp = tmp.substr(i+1,tmp.length()-i-1);
    }
    i = tmp.find('/');
    if (i == -1) {
	m_domain = tmp;
	m_resource = "";
    }
    else {
	m_domain = tmp.substr(0,i);
	m_resource = tmp.substr(i+1,tmp.length()-i-1);
    }
    normalize();
}

void JabberID::normalize()
{
    m_node.toLower();
    m_domain.toLower();
    m_bare.clear();
    if (m_node)
	m_bare << m_node << "@";
    m_bare << m_domain;
    assign(m_bare);
    if (m_resource)
	*this << "/" << m_resource;
}

// Get an empty JabberID
const JabberID& JabberID::empty()
{
    return s_emptyJid;
}


/*
 * JIDIdentity
 */
void JIDIdentity::fromXml(XmlElement* identity)
{
    if (!identity)
	return;
    m_category = identity->getAttribute("category");
    m_type = identity->getAttribute("type");
    m_name = identity->getAttribute("name");
}

XmlElement* JIDIdentity::createIdentity(const char* category, const char* type,
    const char* name)
{
    XmlElement* id = XMPPUtils::createElement(XmlTag::Identity);
    id->setAttribute("category",category);
    id->setAttribute("type",type);
    id->setAttribute("name",name);
    return id;
}


/*
 * JIDIdentityList
 */
// Fill an xml element with identities held by this list
void JIDIdentityList::toXml(XmlElement* parent) const
{
    if (!parent)
	return;
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	JIDIdentity* id = static_cast<JIDIdentity*>(o->get());
	parent->addChild(id->toXml());
    }
}

// Add identity children from an xml element
void JIDIdentityList::fromXml(XmlElement* parent)
{
    if (!parent)
	return;
    XmlElement* id = XMPPUtils::findFirstChild(*parent,XmlTag::Identity);
    for (; id; id = XMPPUtils::findNextChild(*parent,id,XmlTag::Identity))
	append(new JIDIdentity(id));
}


/*
 * XMPPFeature
 */
// Build an xml element from this feature
XmlElement* XMPPFeature::build(bool addReq)
{
    XmlElement* xml = XMPPUtils::createElement(m_xml);
    // Set default namespace
    xml->setXmlns(String::empty(),true,*this);
    if (addReq)
	addReqChild(*xml);
    return xml;
}

// Build a feature element from this one
XmlElement* XMPPFeature::buildFeature()
{
    XmlElement* x = XMPPUtils::createElement(XmlTag::Feature);
    x->setAttribute("var",c_str());
    return x;
}

// Add a required/optional child to an element
void XMPPFeature::addReqChild(XmlElement& xml)
{
#ifndef RFC3920
    if (m_required)
	xml.addChild(XMPPUtils::createElement(XmlTag::Required));
    else
	xml.addChild(XMPPUtils::createElement(XmlElement::Optional));
#endif
}

// Build a feature from a stream:features child
XMPPFeature* XMPPFeature::fromStreamFeature(XmlElement& xml)
{
    int t = XmlTag::Count;
    int n = XMPPNamespace::Count;
    XMPPUtils::getTag(xml,t,n);
    if (t == XmlTag::Count) {
	DDebug(DebugStub,"XMPPFeature::fromStreamFeature() unhandled tag '%s'",
	    xml.tag());
	return 0;
    }
    XMPPFeature* f = 0;
    bool required = XMPPUtils::required(xml);
    DDebug(DebugAll,"XMPPFeature::fromStreamFeature() processing '%s' ns=%s",
	xml.tag(),TelEngine::c_safe(xml.xmlns()));
    if (t == XmlTag::Mechanisms && n == XMPPNamespace::Sasl) {
	int mech = 0;
	// Get mechanisms
	XmlElement* x = XMPPUtils::findFirstChild(xml,XmlTag::Mechanism);
	for (; x; x = XMPPUtils::findNextChild(xml,x,XmlTag::Mechanism)) {
	    const String& n = x->getText();
	    if (!n)
		continue;
	    int m = XMPPUtils::authMeth(n);
	    if (m)
		mech |= m;
	    else
		DDebug(DebugStub,"XMPPFeature::fromStreamFeature() Unhandled mechanism '%s'",
		    n.c_str());
	}
	f = new XMPPFeatureSasl(mech,required);
    }
    else if (t == XmlTag::Compression && n == XMPPNamespace::CompressFeature) {
	String meth;
	// Get methods
	XmlElement* x = 0;
	while (0 != (x = XMPPUtils::findNextChild(xml,x,XmlTag::Method)))
	    meth.append(x->getText(),",",false);
	f = new XMPPFeatureCompress(meth,required);
    }
    else {
	String* xmlns = xml.xmlns();
	if (!TelEngine::null(xmlns))
	    f = new XMPPFeature(t,xmlns->c_str(),required);
    }
    return f;
}

void XMPPFeature::setFeature(int feature)
{
    assign(XMPPUtils::s_ns.at(feature));
}


/*
 * XMPPFeatureSasl
 */
// Build an xml element from this feature
XmlElement* XMPPFeatureSasl::build(bool addReq)
{
    if (!m_mechanisms)
	return 0;
    XmlElement* xml = XMPPFeature::build(false);
    for (const TokenDict* t = XMPPUtils::s_authMeth; t->value; t++)
	if (mechanism(t->value))
	    xml->addChild(XMPPUtils::createElement(XmlTag::Mechanism,t->token));
    if (addReq)
	addReqChild(*xml);
    return xml;
}


/*
 * XMPPFeatureCompress
 */
// Check if a given method is supported by this feature
bool XMPPFeatureCompress::hasMethod(const String& method) const
{
    ObjList* list = m_methods.split(',',false);
    bool ok = 0 != list->find(method);
    TelEngine::destruct(list);
    return ok;
}

// Build an xml element from this feature
XmlElement* XMPPFeatureCompress::build(bool addReq)
{
    if (!m_methods)
	return 0;
    XmlElement* xml = XMPPFeature::build(false);
    ObjList* list = m_methods.split(',',false);
    for (ObjList* o = list->skipNull(); o; o = o->skipNext())
	xml->addChild(XMPPUtils::createElement(XmlTag::Method,o->get()->toString()));
    TelEngine::destruct(list);
    if (addReq)
	addReqChild(*xml);
    return xml;
}


/*
 * XMPPFeatureList
 */
// Add a list of features to this list. Don't check duplicates
void XMPPFeatureList::add(XMPPFeatureList& list)
{
    ObjList* o = list.skipNull();
    while (o) {
	append(o->remove(false));
	o = list.skipNull();
    }
}

// Re-build this list from stream features
void XMPPFeatureList::fromStreamFeatures(XmlElement& xml)
{
    reset();
    m_identities.fromXml(&xml);
    for (XmlElement* x = xml.findFirstChild(); x; x = xml.findNextChild(x)) {
	// Process only elements in default namespace
	if (!x->isDefaultNs())
	    continue;
	// Skip identities
	if (x->toString() == XMPPUtils::s_tag[XmlTag::Identity])
	    continue;
	XMPPFeature* f = XMPPFeature::fromStreamFeature(*x);
	if (f)
	    append(f);
    }
}

// Re-build this list from disco info responses
void XMPPFeatureList::fromDiscoInfo(XmlElement& xml)
{
    reset();
    m_identities.fromXml(&xml);
    XmlElement* x = XMPPUtils::findFirstChild(xml,XmlTag::Feature);
    for (; x; x = XMPPUtils::findNextChild(xml,x,XmlTag::Feature)) {
	// Process only elements in default namespace
	if (!x->isDefaultNs())
	    continue;
	const char* var = x->attribute("var");
	if (!TelEngine::null(var))
	    append(new XMPPFeature(XmlTag::Feature,var));
    }
}

// Find a specific feature
XMPPFeature* XMPPFeatureList::get(int feature)
{
    const String& name = XMPPUtils::s_ns.at(feature);
    return name ? get(name) : 0;
}

// Build stream features from this list
XmlElement* XMPPFeatureList::buildStreamFeatures()
{
    XmlElement* xml = XMPPUtils::createElement(XmlTag::Features);
    XMPPUtils::setStreamXmlns(*xml,false);
    for (ObjList* o = skipNull(); o; o = o->skipNext())
	xml->addChild((static_cast<XMPPFeature*>(o->get()))->build());
    return xml;
}

// Build an iq query disco info result from this list
XmlElement* XMPPFeatureList::buildDiscoInfo(const char* from, const char* to,
    const char* id, const char* node, const char* cap)
{
    XmlElement* res = XMPPUtils::createIqDisco(true,false,from,to,id,node,cap);
    XmlElement* query = XMPPUtils::findFirstChild(*res,XmlTag::Query);
    if (query)
	add(*query);
    return res;
}

// Add this list to an xml element
void XMPPFeatureList::add(XmlElement& xml)
{
    m_identities.toXml(&xml);
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	XMPPFeature* f = static_cast<XMPPFeature*>(o->get());
	xml.addChild(f->buildFeature());
    }
}

// Update the entity capabilities hash
void XMPPFeatureList::updateEntityCaps()
{
    m_entityCapsHash.clear();
    ObjList* o = 0;
    // Sort identities by category/type/name
    ObjList i;
    for (o = m_identities.skipNull(); o; o = o->skipNext()) {
	JIDIdentity* id = static_cast<JIDIdentity*>(o->get());
	ObjList* oi = i.skipNull();
	for (; oi; oi = oi->skipNext()) {
	    JIDIdentity* crt = static_cast<JIDIdentity*>(oi->get());
	    #define CMP_IDENT(a,b) { \
		int res = XMPPUtils::cmpBytes(a,b); \
		if (res < 0) \
		    break; \
		if (res > 0) \
		    continue; \
	    }
	    CMP_IDENT(id->m_category,crt->m_category)
	    // Equal category: check type
	    CMP_IDENT(id->m_type,crt->m_type)
	    // Equal category and type: check name
	    CMP_IDENT(id->m_name,crt->m_name)
	    // All fields are equal, continue (keep the original order)
	    #undef CMP_IDENT
	}
	if (oi)
	    oi->insert(id)->setDelete(false);
	else
	    i.append(id)->setDelete(false);
    }

    // Sort features
    ObjList f;
    for (o = skipNull(); o; o = o->skipNext()) {
	XMPPFeature* feature = static_cast<XMPPFeature*>(o->get());
	ObjList* of = f.skipNull();
	for (; of; of = of->skipNext()) {
	    String* crt = static_cast<String*>(of->get());
	    if (XMPPUtils::cmpBytes(*feature,*crt) < 0)
		break;
	}
	if (of)
	    of->insert(feature)->setDelete(false);
	else
	    f.append(feature)->setDelete(false);
    }

    // Build SHA
    SHA1 sha;
    for (o = i.skipNull(); o; o = o->skipNext()) {
	JIDIdentity* id = static_cast<JIDIdentity*>(o->get());
	sha << id->m_category << "/" << id->m_type << "//" << id->m_name << "<";
    }
    for (o = f.skipNull(); o; o = o->skipNext()) {
	XMPPFeature* tmp = static_cast<XMPPFeature*>(o->get());
	sha << tmp->c_str() << "<";
    }
    Base64 b((void*)sha.rawDigest(),20);
    b.encode(m_entityCapsHash);
}


/*
 * XMPPUtils
 */
// Find an xml tag in an array terminated with XmlTag::Count
int XMPPUtils::findTag(const String& tag, int* tags)
{
    if (!(tag && tags))
	return XmlTag::Count;
    for (; *tags != XmlTag::Count; tags++)
	if (tag == s_tag[*tags])
	    return *tags;
    return XmlTag::Count;
}

// Partially build an XML element from another one.
XmlElement* XMPPUtils::createElement(const XmlElement& src, bool response, bool result)
{
    XmlElement* xml = new XmlElement(src.toString());
    if (response) {
	xml->setAttributeValid("from",src.attribute("to"));
	xml->setAttributeValid("to",src.attribute("from"));
	xml->setAttribute("type",result ? "result" : "error");
    }
    else {
	xml->setAttributeValid("from",src.attribute("from"));
	xml->setAttributeValid("to",src.attribute("to"));
	xml->setAttributeValid("type",src.attribute("type"));
    }
    xml->setAttributeValid("id",src.attribute("id"));
    return xml;
}

XmlElement* XMPPUtils::createIq(IqType type, const char* from,
    const char* to, const char* id)
{
    XmlElement* iq = createElement(XmlTag::Iq);
    iq->setAttributeValid("type",lookup(type,s_iq,""));
    iq->setAttributeValid("from",from);
    iq->setAttributeValid("to",to);
    iq->setAttributeValid("id",id);
    return iq;
}

// Create an 'iq' error from a received element. Consume the received element
// Add the given element to the error stanza if the 'id' attribute is missing
XmlElement* XMPPUtils::createIqError(const char* from, const char* to, XmlElement*& xml,
    int type, int error, const char* text)
{
    const char* id = xml ? xml->attribute("id") : 0;
    XmlElement* iq = createIq(XMPPUtils::IqError,from,to,id);
    if (TelEngine::null(id) && xml) {
	iq->addChild(xml);
	xml = 0;
    }
    TelEngine::destruct(xml);
    iq->addChild(createError(type,error,text));
    return iq;
}

// Create an 'iq' element with a ping child
XmlElement* XMPPUtils::createPing(const char* id, const char* from, const char* to)
{
    XmlElement* iq = XMPPUtils::createIq(XMPPUtils::IqGet,from,to,id);
    iq->addChild(XMPPUtils::createElement(XmlTag::Ping,XMPPNamespace::Ping));
    return iq;
}

// Create an 'iq' element of type 'get' with a 'vcard' child
XmlElement* XMPPUtils::createVCard(bool get, const char* from, const char* to, const char* id)
{
    XmlElement* xml = createIq(get ? IqGet : IqSet,from,to,id);
    xml->addChild(createElement(XmlTag::VCard,XMPPNamespace::VCard));
    return xml;
}

XmlElement* XMPPUtils::createCommand(CommandAction action, const char* node,
    const char* sessionId)
{
    XmlElement* command = createElement(XmlTag::Command,XMPPNamespace::Command);
    if (sessionId)
	command->setAttribute("sessionid",sessionId);
    command->setAttribute("node",node);
    command->setAttribute("action",lookup(action,s_commandAction));
    return command;
}

// Create a disco info/items 'iq' element with a 'query' child
XmlElement* XMPPUtils::createIqDisco(bool info, bool req, const char* from,
    const char* to, const char* id, const char* node, const char* cap)
{
    XmlElement* xml = createIq(req ? IqGet : IqResult,from,to,id);
    XmlElement* query = createElement(XmlTag::Query,
	info ? XMPPNamespace::DiscoInfo : XMPPNamespace::DiscoItems);
    if (!TelEngine::null(node)) {
	if (TelEngine::null(cap))
	    query->setAttribute("node",node);
	else
	    query->setAttribute("node",String(node) + "#" + cap);
    }
    xml->addChild(query);
    return xml;
}

// Create a version 'iq' result as defined in XEP-0092
XmlElement* XMPPUtils::createIqVersionRes(const char* from, const char* to,
    const char* id, const char* name, const char* version, const char* os)
{
    XmlElement* query = createElement(XmlTag::Query,XMPPNamespace::IqVersion);
    query->addChild(createElement("name",name));
    query->addChild(createElement("version",version));
    if (os)
	query->addChild(createElement("os",os));
    return createIqResult(from,to,id,query);
}

XmlElement* XMPPUtils::createError(int type, int condition, const char* text)
{
    XmlElement* err = createElement(XmlTag::Error);
    err->setAttribute("type",s_error[type]);
    err->addChild(createElement(s_error[condition],XMPPNamespace::StanzaError));
    if (!TelEngine::null(text))
	err->addChild(createElement(XmlTag::Text,XMPPNamespace::StanzaError,text));
    return err;
}

// Create an error from a received element. Consume the received element
XmlElement* XMPPUtils::createError(XmlElement* xml, int type, int error,
    const char* text)
{
    if (!xml)
	return 0;
    XmlElement* err = createElement(*xml,true,false);
    err->addChild(createError(type,error,text));
    TelEngine::destruct(xml);
    return err;
}

// Build a stream error element
XmlElement* XMPPUtils::createStreamError(int error, const char* text, const char* content)
{
    XmlElement* xml = createElement(XmlTag::Error);
    setStreamXmlns(*xml,false);
    XmlElement* err = createElement(s_error[error],XMPPNamespace::StreamError,content);
    xml->addChild(err);
    if (!TelEngine::null(text))
	xml->addChild(createElement(XmlTag::Text,XMPPNamespace::StreamError,text));
    return xml;
}

// Build a register query element
XmlElement* XMPPUtils::createRegisterQuery(IqType type, const char* from,
    const char* to, const char* id,
    XmlElement* child1, XmlElement* child2, XmlElement* child3)
{
    XmlElement* iq = createIq(type,from,to,id);
    XmlElement* q = XMPPUtils::createElement(XmlTag::Query,XMPPNamespace::IqRegister);
    if (child1)
	q->addChild(child1);
    if (child2)
	q->addChild(child2);
    if (child3)
	q->addChild(child3);
    iq->addChild(q);
    return iq;
}

// Build a jabber:iq:auth 'iq' set element
XmlElement* XMPPUtils::createIqAuthSet(const char* id, const char* username,
    const char* resource, const char* authStr, bool digest)
{
    XmlElement* iq = createIq(IqSet,0,0,id);
    XmlElement* q = createElement(XmlTag::Query,XMPPNamespace::IqAuth);
    iq->addChild(q);
    q->addChild(createElement(XmlTag::Username,username));
    q->addChild(createElement(XmlTag::Resource,resource));
    q->addChild(createElement(digest ? XmlTag::Digest : XmlTag::Password,authStr));
    return iq;
}

// Build a jabber:iq:auth 'iq' offer in response to a 'get' request
XmlElement* XMPPUtils::createIqAuthOffer(const char* id, bool digest, bool plain)
{
    XmlElement* iq = createIq(IqResult,0,0,id);
    XmlElement* q = createElement(XmlTag::Query,XMPPNamespace::IqAuth);
    iq->addChild(q);
    q->addChild(createElement(XmlTag::Username));
    q->addChild(createElement(XmlTag::Resource));
    if (digest)
	q->addChild(createElement(XmlTag::Digest));
    if (plain)
	q->addChild(createElement(XmlTag::Password));
    return iq;
}

// Find an element's first child element in a given namespace
XmlElement* XMPPUtils::findFirstChild(const XmlElement& xml, int t, int ns)
{
    if (t < XmlTag::Count)
	if (ns < XMPPNamespace::Count)
	    return xml.findFirstChild(&s_tag[t],&s_ns[ns]);
	else
	    return xml.findFirstChild(&s_tag[t],0);
    else if (ns < XMPPNamespace::Count)
	return xml.findFirstChild(0,&s_ns[ns]);
    return xml.findFirstChild();
}

// Find an element's next child element
XmlElement* XMPPUtils::findNextChild(const XmlElement& xml, XmlElement* start,
    int t, int ns)
{
    if (t < XmlTag::Count)
	if (ns < XMPPNamespace::Count)
	    return xml.findNextChild(start,&s_tag[t],&s_ns[ns]);
	else
	    return xml.findNextChild(start,&s_tag[t],0);
    else if (ns < XMPPNamespace::Count)
	return xml.findNextChild(start,0,&s_ns[ns]);
    return xml.findNextChild(start);
}

// Find an error child of a given element and return the associated code
void XMPPUtils::decodeError(XmlElement* xml, int ns, String* error, String* text, String* content)
{
    if (!(xml && (error || text)))
	return;
    XmlElement* errParent = xml;
    int xmlNs = xmlns(*xml);
    if (xmlNs < XMPPNamespace::Count && xmlNs != XMPPNamespace::Stream) {
	// Non stream error: find the 'error' child
	// Check if there is one in the default namespace or in the element's namespace
	errParent = findFirstChild(*xml,XmlTag::Error,XMPPNamespace::Count);
	if (!errParent)
	    errParent = findFirstChild(*xml,XmlTag::Error,xmlNs);
	if (!errParent)
	    return;
    }
    // Adjust error condition namespace if not set
    if (ns >= XMPPNamespace::Count) {
	if (xmlNs != XMPPNamespace::Stream)
	    ns = XMPPNamespace::StanzaError;
	else
	    ns = XMPPNamespace::StreamError;
    }
    // Retrieve the first error condition and text
    if (error) {
	XmlElement* ch = XMPPUtils::findFirstChild(*errParent,XmlTag::Count,ns);
	for (; ch; ch = XMPPUtils::findNextChild(*errParent,ch,XmlTag::Count,ns)) {
	    if (ch->unprefixedTag() == XMPPUtils::s_tag[XmlTag::Text])
		continue;
	    *error = ch->unprefixedTag();
	    if (content)
		*content = ch->getText();
	    else if (text) {
		*text = ch->getText();
		if (!TelEngine::null(text))
		    return;
	    }
	    break;
	}
    }
    if (text) {
	XmlElement* ch = findFirstChild(*errParent,XmlTag::Text,XMPPNamespace::Count);
	if (ch)
	    *text = ch->getText();
    }
}

// Decode an 'error' XML element
void XMPPUtils::decodeError(XmlElement* xml, String& error, String& text)
{
    if (!xml)
	return;
    error = "";
    text = "";
    int t;
    int ns;
    if (!getTag(*xml,t,ns))
	return;
    switch (t) {
	case XmlTag::Error:
	    // Stream error
	    if (ns == XMPPNamespace::Stream)
		decodeError(xml,XMPPNamespace::StreamError,&error,&text);
	    break;
	case XmlTag::Iq:
	case XmlTag::Presence:
	case XmlTag::Message:
	    // Stanza in stream namespace
	    if (ns == XMPPNamespace::Server || ns == XMPPNamespace::Client ||
		ns == XMPPNamespace::ComponentAccept)
		decodeError(xml,XMPPNamespace::StanzaError,&error,&text);
	    break;
	case XmlTag::Failure:
	    if (ns != XMPPNamespace::Count) {
		// Find the first error condition in element's namespace
		XmlElement* ch = 0;
		while (0 != (ch = findNextChild(*xml,ch,XmlTag::Count,ns))) {
		    const String& err = ch->unprefixedTag();
		    if (s_error[err] < XMPPError::TypeCount) {
			error = err;
			return;
		    }
		}
	    }
	    break;
    }
}

// Create a 'delay' element as defined in XEP-0203
XmlElement* XMPPUtils::createDelay(unsigned int timeSec, const char* from,
    unsigned int fractions, const char* text)
{
    XmlElement* x = createElement("delay",XMPPNamespace::Delay,text);
    x->setAttributeValid("from",from);
    String time;
    encodeDateTimeSec(time,timeSec,fractions);
    x->setAttributeValid("stamp",time);
    return x;
}

// Create a 'file' element in file transfer namespace, add the name child if not empty
XmlElement* XMPPUtils::createFileNsTransfer(const char* name)
{
    XmlElement* x = createElement(XmlTag::File,XMPPNamespace::JingleAppsFileTransfer);
    if (!TelEngine::null(name))
	x->addChildSafe(createElement(XmlTag::Name,name));
    return x;
}

// Create a 'hash' element as defined in XEP-0300
XmlElement* XMPPUtils::createHash(const char* name, const char* value)
{
    XmlElement* x = createElement(XmlTag::Hash,XMPPNamespace::Hash,value);
    x->setAttribute(s_tag[XmlTag::Algo],name);
    return x;
}

// Decode a 'hash' element as defined in XEP-0300
void XMPPUtils::decodeHash(XmlElement& xml, const char*& name, const char*& value)
{
    name = xml.attribute(s_tag[XmlTag::Algo]);
    value = xml.getText();
}

// Build a Result Set Management (XEP-0059) set element from a parameter list
XmlElement* XMPPUtils::createRSM(const NamedList& params, const String& prefix)
{
    static int s_rsmTags[] = {XmlTag::After,XmlTag::Before,XmlTag::CountTag,XmlTag::First,
	XmlTag::Index,XmlTag::Last,XmlTag::Max,XmlTag::Count};
    XmlElement* rsm = 0;
    NamedIterator iter(params);
    for (const NamedString* ns = 0; 0 != (ns = iter.get());) {
	if (!ns->name().startsWith(prefix))
	    continue;
	String tmp = ns->name().substr(prefix.length());
	int tag = findTag(tmp,s_rsmTags);
	if (tag == XmlTag::Count)
	    continue;
	XmlElement* x = createElement(tmp,*ns);
	if (tag == XmlTag::First) {
	    String* index = params.getParam(ns->name() + ".index");
	    if (!TelEngine::null(index))
		x->setAttribute(s_tag[XmlTag::Index],*index);
	}
	if (!rsm)
	    rsm = createElement(XmlTag::Set,XMPPNamespace::ResultSetMngt);
	rsm->addChildSafe(x);
    }
    return rsm;
}

// Add Result Set Management (XEP-0059) set element child data to a parameter list
void XMPPUtils::addRSM(XmlElement* rsm, NamedList& params, const String& prefix)
{
    if (!rsm)
	return;
    for (XmlElement* c = 0; 0 != (c = rsm->findNextChild(c));) {
	int tag = XmlTag::Count;
	int ns = XMPPNamespace::Count;
	getTag(*c,tag,ns);
	String p = prefix + c->unprefixedTag();
	params.addParam(p,c->getText());
	if (tag == XmlTag::First && ns == XMPPNamespace::ResultSetMngt) {
	    const char* index = c->attribute(s_ns[XmlTag::Index]);
	    if (index)
		params.addParam(p + ".index",index);
	}
    }
}

// Check if an element has a child with 'priority' tag
int XMPPUtils::priority(XmlElement& xml, int defVal)
{
    XmlElement* p = findFirstChild(xml,XmlTag::Priority);
    if (!p)
	return defVal;
    String prio(p->getText());
    prio.trimBlanks();
    return prio.toInteger(defVal);
}

inline void addPaddedVal(String& buf, int val, const char* sep)
{
    if (val < 10)
	buf << "0";
    buf << val << sep;
}

// Encode EPOCH time given in seconds to a date/time profile as defined in
//  XEP-0082
void XMPPUtils::encodeDateTimeSec(String& buf, unsigned int timeSec,
	unsigned int fractions)
{
    int y;
    unsigned int m,d,hh,mm,ss;
    if (!Time::toDateTime(timeSec,y,m,d,hh,mm,ss))
	return;
    buf << y << "-";
    addPaddedVal(buf,m,"-");
    addPaddedVal(buf,d,"T");
    addPaddedVal(buf,hh,":");
    addPaddedVal(buf,mm,":");
    addPaddedVal(buf,ss,"");
    if (fractions)
	buf << "." << fractions;
    buf << "Z";
}

// Decode a date/time profile as defined in XEP-0082 and
//  XML Schema Part 2: Datatypes Second Edition to EPOCH time
unsigned int XMPPUtils::decodeDateTimeSec(const String& time, unsigned int* fractions)
{
    // XML Schema Part 2: Datatypes Second Edition
    // (see http://www.w3.org/TR/xmlschema-2/#dateTime)
    // Section 3.2.7: dateTime
    // Format: [-]yyyy[y+]-mm-ddThh:mm:ss[.s+][Z|[+|-]hh:mm]
    // NOTE: The document specify that yyyy may be negative and may have more then 4 digits:
    //       for now we only accept positive years greater then 1970

    unsigned int ret = (unsigned int)-1;
    unsigned int timeFractions = 0;
    while (true) {
	// Split date/time
	int pos = time.find('T');
	if (pos == -1)
	    return (unsigned int)-1;
	// Decode date
	if (time.at(0) == '-')
	    break;
	int year = 0;
	unsigned int month = 0;
	unsigned int day = 0;
	String date = time.substr(0,pos);
	ObjList* list = date.split('-');
	bool valid = (list->length() == 3 && list->count() == 3);
	if (valid) {
	    year = (*list)[0]->toString().toInteger(-1,10);
	    month = (unsigned int)(*list)[1]->toString().toInteger(-1,10);
	    day = (unsigned int)(*list)[2]->toString().toInteger(-1,10);
	    valid = year >= 1970 && month && month <= 12 && day && day <= 31;
	}
	TelEngine::destruct(list);
	if (valid)
	    DDebug(DebugAll,
		"XMPPUtils::decodeDateTimeSec() decoded year=%d month=%u day=%u from '%s'",
		year,month,day,time.c_str());
	else {
	    Debug(DebugNote,
		"XMPPUtils::decodeDateTimeSec() incorrect date=%s in '%s'",
		date.c_str(),time.c_str());
	    break;
	}
	// Decode Time
	String t = time.substr(pos + 1,8);
	if (t.length() != 8)
	    break;
	unsigned int hh = 0;
	unsigned int mm = 0;
	unsigned int ss = 0;
	int offsetSec = 0;
	list = t.split(':');
	valid = (list->length() == 3 && list->count() == 3);
	if (valid) {
	    hh = (unsigned int)(*list)[0]->toString().toInteger(-1,10);
	    mm = (unsigned int)(*list)[1]->toString().toInteger(-1,10);
	    ss = (unsigned int)(*list)[2]->toString().toInteger(-1,10);
	    valid = (hh <= 23 && mm <= 59 && ss <= 59) ||
		(hh == 24 && mm == 0 && ss == 0);
	}
	TelEngine::destruct(list);
	if (!valid) {
	    Debug(DebugNote,
		"XMPPUtils::decodeDateTimeSec() incorrect time=%s in '%s'",
		t.c_str(),time.c_str());
	    break;
	}
#ifdef DEBUG
	else
	    Debug(DebugAll,
		"XMPPUtils::decodeDateTimeSec() decoded hour=%u minute=%u sec=%u from '%s'",
		hh,mm,ss,time.c_str());
#endif
	// Get the rest
	unsigned int parsed = date.length() + t.length() + 1;
	unsigned int len = time.length() - parsed;
	const char* buf = time.c_str() + parsed;
	if (len > 1) {
	    // Get time fractions
	    if (buf[0] == '.') {
		unsigned int i = 1;
		// FIXME: Trailing 0s are not allowed in fractions
		for (; i < len && buf[i] >= '0' && buf[i] <= '9'; i++)
		    ;
		String fr(buf + 1,i - 1);
		if (i > 2)
		    timeFractions = (unsigned int)fr.toInteger(-1);
		else
		    timeFractions = (unsigned int)-1;
		if (timeFractions != (unsigned int)-1)
		    DDebug(DebugAll,
			"XMPPUtils::decodeDateTimeSec() decoded fractions=%u from '%s'",
			timeFractions,time.c_str());
		else {
		    Debug(DebugNote,
			"XMPPUtils::decodeDateTimeSec() incorrect fractions=%s in '%s'",
			fr.c_str(),time.c_str());
		    break;
		}
		len -= i;
		buf += i;
	    }
	    // Get offset
	    if (len > 1) {
		int sign = 1;
		if (*buf == '-' || *buf == '+') {
		    if (*buf == '-')
			sign = -1;
		    buf++;
		    len--;
		}
		String offs(buf,5);
		// We should have at least 5 bytes: hh:ss
		if (len < 5 || buf[2] != ':') {
		    Debug(DebugNote,
			"XMPPUtils::decodeDateTimeSec() incorrect time offset=%s in '%s'",
			offs.c_str(),time.c_str());
		    break;
		}
		unsigned int hhOffs = (unsigned int)offs.substr(0,2).toInteger(-1,10);
		unsigned int mmOffs = (unsigned int)offs.substr(3,2).toInteger(-1,10);
		// XML Schema Part 2 3.2.7.3: the hour may be 0..13. It can be 14 if minute is 0
		if (mmOffs > 59 || (hhOffs > 13 && !mmOffs)) {
		    Debug(DebugNote,
			"XMPPUtils::decodeDateTimeSec() incorrect time offset values hour=%u minute=%u in '%s'",
			hhOffs,mmOffs,time.c_str());
		    break;
		}
		DDebug(DebugAll,
		    "XMPPUtils::decodeDateTimeSec() decoded time offset '%c' hour=%u minute=%u from '%s'",
		    sign > 0 ? '+' : '-',hhOffs,mmOffs,time.c_str());
		offsetSec = sign * (hhOffs * 3600 + mmOffs * 60);
		buf += 5;
		len -= 5;
	    }
	}
	// Check termination markup
	if (len && (len != 1 || *buf != 'Z')) {
	    DDebug(DebugNote,
		"XMPPUtils::decodeDateTimeSec() '%s' is incorrectly terminated '%s'",
		time.c_str(),buf);
	    break;
	}
	ret = Time::toEpoch(year,month,day,hh,mm,ss,offsetSec);
	if (ret == (unsigned int)-1)
	    Debug(DebugNote,"XMPPUtils::decodeDateTimeSec() failed to convert '%s'",
		time.c_str());
	break;
    }

    if (ret != (unsigned int)-1) {
	if (fractions)
	    *fractions = timeFractions;
    }
    return ret;
}

// Decode a date/time stamp as defined in XEP-0091 (jabber:x:delay)
// Format: YYYYMMDDThh:mm:ss
unsigned int XMPPUtils::decodeDateTimeSecXDelay(const String& time)
{
    int year = -1;
    unsigned int month = (unsigned int)-1;
    unsigned int day = (unsigned int)-1;
    unsigned int hh = (unsigned int)-1;
    unsigned int mm = (unsigned int)-1;
    unsigned int ss = (unsigned int)-1;
    while (true) {
	// Split date/time
	int pos = time.find('T');
	if (pos == -1)
	    break;
	// Decode date
	if (pos == 8) {
	    year = time.substr(0,4).toInteger(-1,10);
	    month = (unsigned int)time.substr(4,2).toInteger(-1,10);
	    day = (unsigned int)time.substr(6,2).toInteger(-1,10);
	}
	else
	    break;
	// Decode time
	ObjList* list = time.substr(pos + 1).split(':');
	if (list->length() == 3 && list->count() == 3) {
	    hh = (unsigned int)(*list)[0]->toString().toInteger(-1,10);
	    mm = (unsigned int)(*list)[1]->toString().toInteger(-1,10);
	    ss = (unsigned int)(*list)[2]->toString().toInteger(-1,10);
	}
	TelEngine::destruct(list);
	break;
    }
    if (!(year != -1 && month && month <= 12 && day && day <= 31 &&
	((hh <= 23 && mm <= 59 && ss <= 59) || (hh == 24 && mm == 0 && ss == 0)))) {
	Debug(DebugNote,
	    "XMPPUtils::decodeDateTimeSecXDelay() incorrect stamp '%s'",time.c_str());
	return (unsigned int)-1;
    }
    unsigned int ret = Time::toEpoch(year,month,day,hh,mm,ss);
    if (ret == (unsigned int)-1)
	Debug(DebugNote,"XMPPUtils::decodeDateTimeSecXDelay() failed to convert '%s'",
	    time.c_str());
    return ret;
}

void XMPPUtils::print(String& xmlStr, XmlChild& xml, bool verbose)
{
    const XmlElement* element = xml.xmlElement();
    if (element) {
	String indent;
	String origindent;
	if (verbose) {
	    indent << "\r\n";
	    origindent << "  ";
	}
	element->toString(xmlStr,false,indent,origindent,false,s_auth);
    }
    else if (xml.xmlDeclaration()) {
	if (verbose)
	    xmlStr << "\r\n";
	xml.xmlDeclaration()->toString(xmlStr,false);
    }
    else
	DDebug(DebugStub,"XMPPUtils::print() not implemented for this type!");
}

// Put an element's name, text and attributes to a list of parameters
void XMPPUtils::toList(XmlElement& xml, NamedList& dest, const char* prefix)
{
    dest.addParam(prefix,xml.tag());
    String pref(String(prefix) + ".");
    const String& tmp = xml.getText();
    if (tmp)
	dest.addParam(pref,tmp);
    unsigned int n = xml.attributes().length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = xml.attributes().getParam(i);
	if (ns)
	    dest.addParam(pref + ns->name(),*ns);
    }
}

bool XMPPUtils::split(NamedList& dest, const char* src, const char sep,
    bool nameFirst)
{
    if (!src)
	return false;
    unsigned int index = 1;
    String s = src;
    ObjList* obj = s.split(sep,false);
    for (ObjList* o = obj->skipNull(); o; o = o->skipNext(), index++) {
	String* tmp = static_cast<String*>(o->get());
	if (nameFirst)
	    dest.addParam(*tmp,String(index));
	else
	    dest.addParam(String(index),*tmp);
    }
    TelEngine::destruct(obj);
    return true;
}

// Decode a comma separated list of flags and put them into an integr mask
int XMPPUtils::decodeFlags(const String& src, const TokenDict* dict)
{
    if (!dict)
	return 0;
    int mask = 0;
    ObjList* obj = src.split(',',false);
    for (ObjList* o = obj->skipNull(); o; o = o->skipNext())
	mask |= lookup(static_cast<String*>(o->get())->c_str(),dict);
    TelEngine::destruct(obj);
    return mask;
}

// Encode a mask of flags to a comma separated list.
void XMPPUtils::buildFlags(String& dest, int src, const TokenDict* dict)
{
    if (!dict)
	return;
    for (; dict->token; dict++)
	if (0 != (src & dict->value))
	    dest.append(dict->token,",");
}

// Add child elements from a list to a destination element
bool XMPPUtils::addChidren(XmlElement* dest, ObjList& list)
{
    if (!dest)
	return false;
    ObjList* o = list.skipNull();
    bool added = (0 != o);
    for (; o; o = o->skipNext()) {
	XmlElement* xml = static_cast<XmlElement*>(o->get());
	dest->addChild(new XmlElement(*xml));
    }
    return added;
}

// Create a 'c' entity capability element as defined in XEP 0115
// See XEP 0115 Section 5
XmlElement* XMPPUtils::createEntityCaps(const String& hash, const char* node)
{
    XmlElement* c = createElement(XmlTag::EntityCapsTag,XMPPNamespace::EntityCaps);
    c->setAttributeValid("node",node);
    c->setAttribute("hash","sha-1");
    c->setAttribute("ver",hash);
    return c;
}

// Create a 'c' entity capability element as defined by GTalk
XmlElement* XMPPUtils::createEntityCapsGTalkV1(const char* node, bool muc)
{
    XmlElement* c = createElement(XmlTag::EntityCapsTag,XMPPNamespace::EntityCaps);
    if (node)
	c->setAttributeValid("node",node);
    else
	c->setAttribute("node","http://www.google.com/xmpp/client/caps");
    c->setAttribute("ver","1.0");
    String caps = "voice-v1";
    if (muc)
	caps.append("pmuc-v1"," ");
    c->setAttribute("ext",caps);
    return c;
}

// Create a presence stanza
XmlElement* XMPPUtils::createPresence(const char* from,
    const char* to, Presence type)
{
    XmlElement* presence = createElement(XmlTag::Presence);
    presence->setAttributeValid("type",presenceText(type));
    presence->setAttributeValid("from",from);
    presence->setAttributeValid("to",to);
    return presence;
}

// Create a message element
XmlElement* XMPPUtils::createMessage(const char* type, const char* from,
    const char* to, const char* id, const char* body)
{
    XmlElement* msg = createElement(XmlTag::Message);
    msg->setAttributeValid("type",type);
    msg->setAttributeValid("from",from);
    msg->setAttributeValid("to",to);
    msg->setAttributeValid("id",id);
    if (body)
	msg->addChild(createElement(XmlTag::Body,body));
    return msg;
}

// Build a dialback 'db:result' xml element used to send the dialback key or
XmlElement* XMPPUtils::createDialbackKey(const char* from, const char* to,
    const char* key)
{
    XmlElement* db = createElement("result",key);
    setDbXmlns(*db);
    db->setAttribute("from",from);
    db->setAttribute("to",to);
    return db;
}

// Build a dialback 'db:result' xml element used to send a dialback key response
XmlElement* XMPPUtils::createDialbackResult(const char* from, const char* to,
    XMPPError::Type rsp)
{
    XmlElement* db = createElement("result");
    setDbXmlns(*db);
    db->setAttribute("from",from);
    db->setAttribute("to",to);
    if (rsp == XMPPError::NoError)
	db->setAttribute("type","valid");
    else if (rsp == XMPPError::NotAuthorized)
	db->setAttribute("type","invalid");
    else {
	db->setAttribute("type","error");
	db->addChild(createError(XMPPError::TypeCancel,rsp));
    }
    return db;
}

// Build a dialback 'db:verify' xml element
XmlElement* XMPPUtils::createDialbackVerify(const char* from, const char* to,
    const char* id, const char* key)
{
    XmlElement* db = createElement("verify",key);
    setDbXmlns(*db);
    db->setAttribute("from",from);
    db->setAttribute("to",to);
    db->setAttribute("id",id);
    return db;
}

// Build a dialback 'db:verify' response xml element
XmlElement* XMPPUtils::createDialbackVerifyRsp(const char* from, const char* to,
    const char* id, XMPPError::Type rsp)
{
    XmlElement* db = createElement("verify");
    setDbXmlns(*db);
    db->setAttribute("from",from);
    db->setAttribute("to",to);
    db->setAttribute("id",id);
    if (rsp == XMPPError::NoError)
	db->setAttribute("type","valid");
    else if (rsp == XMPPError::NotAuthorized)
	db->setAttribute("type","invalid");
    else {
	db->setAttribute("type","error");
	db->addChild(createError(XMPPError::TypeCancel,rsp));
    }
    return db;
}

// Decode a dialback verify or result response element
int XMPPUtils::decodeDbRsp(XmlElement* xml)
{
    if (!xml)
	return XMPPError::NotAuthorized;
    String* type = xml->getAttribute("type");
    if (TelEngine::null(type))
	return XMPPError::NotAuthorized;
    if (*type == "valid")
	return XMPPError::NoError;
    if (*type == "valid")
	return XMPPError::NotAuthorized;
    if (*type == "error") {
	String error;
	decodeError(xml,XMPPNamespace::StanzaError,&error);
	if (error) {
	    int rsp = s_error[error];
	    if (rsp > XMPPError::NoError && rsp < XMPPError::TypeCount)
		return rsp;
	}
    }
    return XMPPError::NotAuthorized;
}

// Retrieve the text of an element's body child
const String& XMPPUtils::body(XmlElement& xml, int ns)
{
    if (ns == XMPPNamespace::Count)
	ns = xmlns(xml);
    int t,n;
    for (XmlElement* b = xml.findFirstChild(); b; b = xml.findNextChild(b)) {
	if (getTag(*b,t,n) && t == XmlTag::Body && ns == n)
	    return b->getText();
    }
    return String::empty();
}

// Retrieve an xml element from a NamedPointer. Release its ownership
XmlElement* XMPPUtils::getXml(GenObject* gen, bool takeOwnerShip)
{
    if (!gen)
	return 0;
    NamedPointer* np = static_cast<NamedPointer*>(gen->getObject(YATOM("NamedPointer")));
    XmlElement* xml = np ? static_cast<XmlElement*>(np->userObject(YATOM("XmlElement"))) : 0;
    if (xml && takeOwnerShip)
	np->takeData();
    return xml;
}

// Retrieve an xml element from a Message parameter
// Try to build (parse) from an extra parameter if not found
XmlElement* XMPPUtils::getXml(NamedList& list, const char* param, const char* extra)
{
    if (!TelEngine::null(param)) {
	XmlElement* xml = getXml(list.getParam(param));
	if (xml) {
	    list.clearParam(param);
	    return xml;
	}
    }
    if (TelEngine::null(extra))
	return 0;
    String* data = list.getParam(extra);
    if (!data)
	return 0;
    XmlElement* xml = getXml(*data);
    if (!xml)
	DDebug(DebugInfo,"getXml(%s) invalid xml parameter %s='%s",
	    list.c_str(),extra,data->c_str());
    return xml;
}

// Retrieve a presence xml element from a list parameter.
// Build a presence stanza from parameters if an element is not found
XmlElement* XMPPUtils::getPresenceXml(NamedList& list, const char* param,
    const char* extra, Presence type, bool build)
{
    XmlElement* xml = getXml(list,param,extra);
    if (xml || !build)
	return xml;
    xml = createPresence(0,0,type);
#define SET_TEXT_CHILD(param) { \
	const char* tmp = list.getValue(param); \
	if (tmp) \
	    xml->addChild(createElement(param,tmp)); \
    }
    SET_TEXT_CHILD("priority")
    SET_TEXT_CHILD("show")
    SET_TEXT_CHILD("status")
#undef SET_TEXT_CHILD
    return xml;
}

// Retrieve a chat (message) xml element from a list parameter.
// Build a message stanza from parameters if an element is not found
XmlElement* XMPPUtils::getChatXml(NamedList& list, const char* param,
    const char* extra, bool build)
{
    XmlElement* xml = getXml(list,param,extra);
    if (xml || !build)
	return xml;
    String* type = list.getParam("type");
    if (TelEngine::null(type))
	xml = createMessage(Chat,0,0,list.getValue("id"),0);
    else
	xml = createMessage(*type,0,0,list.getValue("id"),0);
    const char* subject = list.getValue("subject");
    if (!TelEngine::null(subject))
	xml->addChild(createSubject(subject));
    const char* body = list.getValue("body");
    if (!TelEngine::null(body))
	xml->addChild(createBody(body));
    const String& state = list["chatstate"];
    if (state && XmlSaxParser::validTag(state))
	xml->addChild(createElement(state,XMPPNamespace::ChatStates));
    return xml;
}

// Compare 2 Strings. Return negative if s1<s2, positive if s1>s2 or 0
int XMPPUtils::cmpBytes(const String& s1, const String& s2)
{
    if (s1 && s2) {
	if (s1.length() == s2.length())
	    return ::memcmp(s1.c_str(),s2.c_str(),s1.length());
	if (s1.length() < s2.length()) {
	    int res = ::memcmp(s1.c_str(),s2.c_str(),s1.length());
	    if (res)
		return res;
	    return -1;
	}
	int res = ::memcmp(s1.c_str(),s2.c_str(),s2.length());
	return res ? res : 1;
    }
    if (s1 || s2)
	return s1 ? 1 : -1;
    return 0;
}

// Parse a string to an XmlElement
XmlElement* XMPPUtils::getXml(const String& data)
{
    XmlDomParser dom("XMPPUtils::getXml()",true);
    dom.parse(data);
    XmlFragment* frag = dom.fragment();
    if (!(frag && frag->getChildren().count() == 1))
	return 0;
    XmlChild* child = static_cast<XmlChild*>(frag->getChildren().skipNull()->get());
    XmlElement* element = child->xmlElement();
    if (element) {
	frag->removeChild(child,false);
	return element;
    }
    return 0;
}


/*
 * XMPPDirVal
 */
// Build a string representation of this object
void XMPPDirVal::toString(String& buf, bool full) const
{
    if (m_value)
	if (full)
	    XMPPUtils::buildFlags(buf,m_value,s_names);
	else
	    XMPPUtils::buildFlags(buf,m_value & ~Pending,s_names);
    else
	buf << lookup(None,s_names);
}

// Build a subscription state string representation of this object
void XMPPDirVal::toSubscription(String& buf) const
{
    int val = m_value & ~Pending;
    val &= Both;
    if (val == Both)
	buf << "both";
    else
	buf << lookup(val,s_names);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
