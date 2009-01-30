/**
 * utils.cpp
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

#include <xmpputils.h>
#include <string.h>

using namespace TelEngine;

#include <time.h>

static XMPPNamespace s_ns;
static XMPPError s_err;

TokenDict XMPPServerInfo::s_flagName[] = {
    {"noautorestart",    NoAutoRestart},
    {"keeproster",       KeepRoster},
    {"tlsrequired",      TlsRequired},
    {"oldstyleauth",     OldStyleAuth},
    {"allowplainauth",   AllowPlainAuth},
    {"allowunsafesetup", AllowUnsafeSetup},
    {0,0}
};

TokenDict XMPPDirVal::s_names[] = {
    {"none", None},
    {"to",   To},
    {"from", From},
    {"both", Both},
    {0,0},
};

/**
 * XMPPNamespace
 */
TokenDict XMPPNamespace::s_value[] = {
    {"http://etherx.jabber.org/streams",                   Stream},
    {"jabber:client",                                      Client},
    {"jabber:server",                                      Server},
    {"jabber:component:accept",                            ComponentAccept},
    {"jabber:component:connect",                           ComponentConnect},
    {"urn:ietf:params:xml:ns:xmpp-streams",                StreamError},
    {"urn:ietf:params:xml:ns:xmpp-stanzas",                StanzaError},
    {"http://jabber.org/features/iq-register",             Register},
    {"jabber:iq:register",                                 IqRegister},
    {"jabber:iq:auth",                                     IqAuth},
    {"http://jabber.org/features/iq-auth",                 IqAuthFeature},
    {"urn:ietf:params:xml:ns:xmpp-tls",                    Starttls},
    {"urn:ietf:params:xml:ns:xmpp-sasl",                   Sasl},
    {"urn:ietf:params:xml:ns:xmpp-session",                Session},
    {"urn:ietf:params:xml:ns:xmpp-bind",                   Bind},
    {"jabber:iq:roster",                                   Roster},
    {"jabber:iq:roster-dynamic",                           DynamicRoster},
    {"http://jabber.org/protocol/disco#info",              DiscoInfo},
    {"http://jabber.org/protocol/disco#items",             DiscoItems},
    {"vcard-temp",                                         VCard},
    {"http://jabber.org/protocol/si/profile/file-transfer",SIProfileFileTransfer},
    {"http://jabber.org/protocol/bytestreams",             ByteStreams},
    {"urn:xmpp:jingle:0",                                  Jingle},
    {"urn:xmpp:jingle:errors:0",                           JingleError},
    {"urn:xmpp:jingle:apps:rtp:0",                         JingleAppsRtp},
    {"urn:xmpp:jingle:apps:rtp:info:0",                    JingleAppsRtpInfo},
    {"urn:xmpp:jingle:apps:file-transfer:0",               JingleAppsFileTransfer},
    {"urn:xmpp:jingle:transports:ice-udp:0",               JingleTransportIceUdp},
    {"urn:xmpp:jingle:transports:raw-udp:0",               JingleTransportRawUdp},
    {"urn:xmpp:jingle:transports:raw-udp:info:0",          JingleTransportRawUdpInfo},
    {"urn:xmpp:jingle:transports:bytestreams:0",           JingleTransportByteStreams},
    {"urn:xmpp:jingle:transfer:0",                         JingleTransfer},
    {"urn:xmpp:jingle:dtmf:0",                             Dtmf},
    {"http://jabber.org/protocol/commands",                Command},
    {"http://www.google.com/xmpp/protocol/voice/v1",       CapVoiceV1},
    {0,0}
};

bool XMPPNamespace::isText(Type index, const char* txt)
{
    const char* tmp = lookup(index,s_value,0);
    if (!(txt && tmp))
	return false;
    return 0 == strcmp(tmp,txt);
}

/**
 * XMPPError
 */
TokenDict XMPPError::s_value[] = {
    {"cancel",                   TypeCancel},
    {"continue",                 TypeContinue},
    {"modify",                   TypeModify},
    {"auth",                     TypeAuth},
    {"wait",                     TypeWait},
    {"bad-format",               BadFormat},
    {"bad-namespace-prefix",     BadNamespace},
    {"connection-timeout",       ConnTimeout},
    {"host-gone",                HostGone},
    {"host-unknown",             HostUnknown},
    {"improper-addressing",      BadAddressing},
    {"internal-server-error",    Internal},
    {"invalid-from",             InvalidFrom},
    {"invalid-id",               InvalidId},
    {"invalid-namespace",        InvalidNamespace},
    {"invalid-xml",              InvalidXml},
    {"not-authorized",           NotAuth},
    {"policy-violation",         Policy},
    {"remote-connection-failed", RemoteConn},
    {"resource-constraint",      ResConstraint},
    {"restricted-xml",           RestrictedXml},
    {"see-other-host",           SeeOther},
    {"system-shutdown",          Shutdown},
    {"undefined-condition",      UndefinedCondition},
    {"unsupported-encoding",     UnsupportedEnc},
    {"unsupported-stanza-type",  UnsupportedStanza},
    {"unsupported-version",      UnsupportedVersion},
    {"xml-not-well-formed",      Xml},
    // Auth failures
    {"aborted",                  Aborted},
    {"incorrect-encoding",       IncorrectEnc},
    {"invalid-authzid",          InvalidAuth},
    {"invalid-mechanism",        InvalidMechanism},
    {"mechanism-too-weak",       MechanismTooWeak},
    {"not-authorized",           NotAuthorized},
    {"temporary-auth-failure",   TempAuthFailure},
    // Stanza errors
    {"bad-request",              SBadRequest},
    {"conflict",                 SConflict},
    {"feature-not-implemented",  SFeatureNotImpl},
    {"forbidden",                SForbidden},
    {"gone",                     SGone},
    {"internal-server-error",    SInternal},
    {"item-not-found",           SItemNotFound},
    {"jid-malformed",            SBadJid},
    {"not-acceptable",           SNotAcceptable},
    {"not-allowed",              SNotAllowed},
    {"payment-required",         SPayment},
    {"recipient-unavailable",    SUnavailable},
    {"redirect",                 SRedirect},
    {"registration-required",    SReg},
    {"remote-server-not-found",  SNoRemote},
    {"remote-server-timeout",    SRemoteTimeout},
    {"resource-constraint",      SResource},
    {"service-unavailable",      SServiceUnavailable},
    {"subscription-required",    SSubscription},
    {"undefined-condition",      SUndefinedCondition},
    {"unexpected-request",       SRequest},
    {"unsupported-dtmf-method",  DtmfNoMethod},
    {"item-not-found",           ItemNotFound},
    {0,0}
};

bool XMPPError::isText(int index, const char* txt)
{
    const char* tmp = lookup(index,s_value,0);
    if (!(txt && tmp))
	return false;
    return 0 == strcmp(tmp,txt);
}

/**
 * JabberID
 */
void JabberID::set(const char* jid)
{
    this->assign(jid);
    parse();
}

void JabberID::set(const char* node, const char* domain, const char* resource)
{
    if (node != m_node.c_str())
	m_node = node;
    if (domain != m_domain.c_str())
	m_domain = domain;
    if (resource != m_resource.c_str())
	m_resource = resource;
    clear();
    if (m_node)
	*this << m_node << "@";
    *this << m_domain;
    m_bare = *this;
    if (m_node && m_resource)
	*this << "/" << m_resource;
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
    // Set bare JID
    m_bare = "";
    if (m_node)
	m_bare << m_node << "@";
    m_bare << m_domain;
}

/**
 * JIDIdentity
 */
TokenDict JIDIdentity::s_category[] = {
	{"account",   Account},
	{"client",    Client},
	{"component", Component},
	{"gateway",   Gateway},
	{0,0},
	};

TokenDict JIDIdentity::s_type[] = {
	{"registered", AccountRegistered},
	{"phone",      ClientPhone},
	{"generic",    ComponentGeneric},
	{"presence",   ComponentPresence},
	{"generic",    GatewayGeneric},
	{0,0},
	};

XMLElement* JIDIdentity::toXML()
{
    return XMPPUtils::createIdentity(categoryText(m_category),typeText(m_type),m_name);
}

bool JIDIdentity::fromXML(const XMLElement* element)
{
    if (!element)
	return false;
    XMLElement* id = ((XMLElement*)element)->findFirstChild("identity");
    if (!id)
	return false;
    m_category = categoryValue(id->getAttribute("category"));
    m_type = typeValue(id->getAttribute("type"));
    id->getAttribute("name",m_name);
    TelEngine::destruct(id);
    return true;
}


/**
 * JIDFeatureSasl
 */
TokenDict JIDFeatureSasl::s_authMech[] = {
    {"DIGEST-MD5",  MechMD5},
    {"DIGEST-SHA1", MechSHA1},
    {"PLAIN",       MechPlain},
    {0,0}
};

/**
 * JIDFeatureList
 */

// Find a specific feature
JIDFeature* JIDFeatureList::get(XMPPNamespace::Type feature)
{
    ObjList* obj = m_features.skipNull();
    for (; obj; obj = obj->skipNext()) {
	JIDFeature* f = static_cast<JIDFeature*>(obj->get());
	if (*f == feature)
	    return f;
    }
    return 0;
}

// Build an XML element and add it to the destination
XMLElement* JIDFeatureList::addTo(XMLElement* element)
{
    if (!element)
	return 0;
    ObjList* obj = m_features.skipNull();
    for (; obj; obj = obj->skipNext()) {
	JIDFeature* f = static_cast<JIDFeature*>(obj->get());
	XMLElement* feature = new XMLElement(XMLElement::Feature);
	feature->setAttribute("var",s_ns[*f]);
	element->addChild(feature);
    }
    return element;
}

// Update the list from 'feature' children of the given element
void JIDFeatureList::fromXml(XMLElement* element, bool reset)
{
    if (reset)
	clear();
    if (!element)
	return;

    XMLElement* x = 0;
    while (0 != (x = element->findNextChild(x,XMLElement::Feature))) {
	XMPPNamespace::Type t = XMPPNamespace::type(x->getAttribute("var"));
	if (t != XMPPNamespace::Count && !get(t))
	    add(t);
    }
}


/**
 * XMPPUtils
 */
TokenDict XMPPUtils::s_iq[] = {
	{"set",     IqSet},
	{"get",     IqGet},
	{"result",  IqResult},
	{"error",   IqError},
	{0,0}
	};

TokenDict XMPPUtils::s_commandAction[] = {
	{"execute",  CommExecute},
	{"cancel",   CommCancel},
	{"prev",     CommPrev},
	{"next",     CommNext},
	{"complete", CommComplete},
	{0,0}
	};

TokenDict XMPPUtils::s_commandStatus[] = {
	{"executing", CommExecuting},
	{"completed", CommCompleted},
	{"cancelled", CommCancelled},
	{0,0}
	};

XMLElement* XMPPUtils::createElement(const char* name, XMPPNamespace::Type ns,
	const char* text)
{
    XMLElement* element = new XMLElement(name,0,text);
    element->setAttribute("xmlns",s_ns[ns]);
    return element;
}

XMLElement* XMPPUtils::createElement(XMLElement::Type type, XMPPNamespace::Type ns,
	const char* text)
{
    XMLElement* element = new XMLElement(type,0,text);
    element->setAttribute("xmlns",s_ns[ns]);
    return element;
}

XMLElement* XMPPUtils::createIq(IqType type, const char* from,
	const char* to, const char* id)
{
    XMLElement* iq = new XMLElement(XMLElement::Iq);
    iq->setAttribute("type",lookup(type,s_iq,""));
    iq->setAttributeValid("from",from);
    iq->setAttributeValid("to",to);
    iq->setAttributeValid("id",id);
    return iq;
}

XMLElement* XMPPUtils::createIqBind( const char* from,
	const char* to, const char* id, const ObjList& resources)
{
    XMLElement* iq = createIq(IqSet,from,to,id);
    XMLElement* bind = createElement(XMLElement::Bind,XMPPNamespace::Bind);
    ObjList* obj = resources.skipNull();
    for (; obj; obj = resources.skipNext()) {
	String* s = static_cast<String*>(obj->get());
	if (!(s && s->length()))
	    continue;
	XMLElement* res = new XMLElement(XMLElement::Resource,0,*s);
	bind->addChild(res);
    }
    iq->addChild(bind);
    return iq;
}

// Create an 'iq' element of type 'get' with a 'vcard' child
XMLElement* XMPPUtils::createVCard(bool get, const char* from, const char* to, const char* id)
{
    XMLElement* xml = createIq(get ? IqGet : IqSet,from,to,id);
    xml->addChild(createElement(XMLElement::VCard,XMPPNamespace::VCard));
    return xml;
}

XMLElement* XMPPUtils::createCommand(CommandAction action, const char* node,
	const char* sessionId)
{
    XMLElement* command = createElement(XMLElement::Command,XMPPNamespace::Command);
    if (sessionId)
	command->setAttribute("sessionid",sessionId);
    command->setAttribute("node",node);
    command->setAttribute("action",lookup(action,s_commandAction));
    return command;
}

XMLElement* XMPPUtils::createIdentity(const char* category, const char* type,
	const char* name)
{
    XMLElement* id = new XMLElement("identity");
    id->setAttribute("category",category);
    id->setAttribute("type",type);
    id->setAttribute("name",name);
    return id;
}

XMLElement* XMPPUtils::createIqDisco(const char* from, const char* to,
	const char* id, bool info)
{
    XMLElement* xml = createIq(IqGet,from,to,id);
    xml->addChild(createElement(XMLElement::Query,
	info ? XMPPNamespace::DiscoInfo : XMPPNamespace::DiscoItems));
    return xml;
}

XMLElement* XMPPUtils::createDiscoInfoRes(const char* from, const char* to,
    const char* id, JIDFeatureList* features, JIDIdentity* identity)
{
    XMLElement* iq = XMPPUtils::createIq(XMPPUtils::IqResult,from,to,id);
    XMLElement* query = XMPPUtils::createElement(XMLElement::Query,XMPPNamespace::DiscoInfo);
    if (identity)
	query->addChild(identity->toXML());
    if (features)
	features->addTo(query);
    iq->addChild(query);
    return iq;
}

XMLElement* XMPPUtils::createError(XMPPError::ErrorType type,
	XMPPError::Type condition, const char* text)
{
    XMLElement* err = new XMLElement("error");
    err->setAttribute("type",s_err[type]);
    XMLElement* tmp = createElement(s_err[condition],XMPPNamespace::StanzaError);
    err->addChild(tmp);
    if (text) {
	tmp = createElement(XMLElement::Text,XMPPNamespace::StanzaError,text);
	err->addChild(tmp);
    }
    return err;
}

// Create an error from a received element. Consume the received element
XMLElement* XMPPUtils::createError(XMLElement* xml, XMPPError::ErrorType type,
	XMPPError::Type error, const char* text)
{
    if (!xml)
	return 0;
    XMLElement* err = new XMLElement(*xml,true,false);
    // Copy children from xml to the error element
    XMLElement* child = 0;
    while (0 != (child = xml->removeChild()))
	err->addChild(child);
    TelEngine::destruct(xml);
    // Create the error
    err->addChild(createError(type,error,text));
    return err;
}

XMLElement* XMPPUtils::createStreamError(XMPPError::Type error, const char* text)
{
    XMLElement* element = new XMLElement(XMLElement::StreamError);
    XMLElement* err = createElement(s_err[error],XMPPNamespace::StreamError);
    element->addChild(err);
    if (text) {
	XMLElement* txt = createElement(XMLElement::Text,XMPPNamespace::StreamError,text);
	element->addChild(txt);
    }
    return element;
}

// Build a register query element
XMLElement* XMPPUtils::createRegisterQuery(IqType type, const char* from,
	const char* to, const char* id,
	XMLElement* child1, XMLElement* child2, XMLElement* child3)
{
    XMLElement* iq = createIq(type,from,to,id);
    XMLElement* q = XMPPUtils::createElement(XMLElement::Query,XMPPNamespace::IqRegister);
    if (child1)
	q->addChild(child1);
    if (child2)
	q->addChild(child2);
    if (child3)
	q->addChild(child3);
    iq->addChild(q);
    return iq;
}

// Check if the given element has an attribute 'xmlns' equal to a given value
bool XMPPUtils::hasXmlns(XMLElement& element, XMPPNamespace::Type ns)
{
    return element.hasAttribute("xmlns",s_ns[ns]);
}

// Decode an 'error' XML element
void XMPPUtils::decodeError(XMLElement* element, String& error, String& text)
{
    if (!element)
	return;

    XMPPNamespace::Type nsErr;
    error = "";
    text = "";
    XMLElement* child = 0;
    switch (element->type()) {
	case XMLElement::StreamError:
	    nsErr = XMPPNamespace::StreamError;
	    break;
	case XMLElement::Error:
	    nsErr = XMPPNamespace::StanzaError;
	    break;
	case XMLElement::Iq:
	case XMLElement::Presence:
	case XMLElement::Message:
	    child = element->findFirstChild(XMLElement::Error);
	    decodeError(child,error,text);
	    TelEngine::destruct(child);
	default:
	    return;
    }

    while (0 != (child = element->findNextChild(child)))
	if (hasXmlns(*child,nsErr)) {
	    error = child->name();
	    TelEngine::destruct(child);
	    break;
	}
    child = element->findFirstChild(XMLElement::Text);
    if (child) {
	text = child->getText();
	TelEngine::destruct(child);
    }
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
	    DDebug(DebugNote,
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
	    valid = (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59 && ss >= 0 && ss <= 59) ||
		(hh == 24 && mm == 0 && ss == 0);
	}
	TelEngine::destruct(list);
	if (valid)
	    DDebug(DebugAll,
		"XMPPUtils::decodeDateTimeSec() decoded hour=%u minute=%u sec=%u from '%s'",
		hh,mm,ss,time.c_str());
	else {
	    DDebug(DebugNote,
		"XMPPUtils::decodeDateTimeSec() incorrect time=%s in '%s'",
		t.c_str(),time.c_str());
	    break;
	}
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
		    DDebug(DebugNote,
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
		    DDebug(DebugNote,
			"XMPPUtils::decodeDateTimeSec() incorrect time offset=%s in '%s'",
			offs.c_str(),time.c_str());
		    break;
		}
		unsigned int hhOffs = (unsigned int)offs.substr(0,2).toInteger(-1,10);
		unsigned int mmOffs = (unsigned int)offs.substr(3,2).toInteger(-1,10);
		// XML Schema Part 2 3.2.7.3: the hour may be 0..13. It can be 14 if minute is 0
		if (mmOffs > 59 || (hhOffs > 13 && !mmOffs)) {
		    DDebug(DebugNote,
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
	    DDebug(DebugNote,
		"XMPPUtils::decodeDateTimeSec() failed to convert '%s'",
		time.c_str());
	break;
    }

    if (ret != (unsigned int)-1) {
	if (fractions)
	    *fractions = timeFractions;
    }
    return ret;
}

// Check if an element or attribute name restricts value output
static const char* check(const String& name, const char* ok)
{
#define RESTRICT_LEN 2
    static String restrict[RESTRICT_LEN] = {"auth", "password"};
    static const char* pwd = "********";
    for (unsigned int i = 0; i < RESTRICT_LEN; i++)
	if (restrict[i] == name)
	    return pwd;
    return ok;
#undef RESTRICT_LEN
}

void XMPPUtils::print(String& xmlStr, XMLElement& element, const char* indent)
{
#define STARTLINE(indent) "\r\n" << indent
    const char* enclose = "-----";
    bool hasAttr = (0 != element.firstAttribute());
    bool hasChild = element.hasChild(0);
    const char* txt = element.getText();
    bool root = false;
    if (!(indent && *indent)) {
	indent = "";
	root = true;
    }
    if (root)
	xmlStr << STARTLINE(indent) << enclose;
    // Name
    if (!(hasAttr || hasChild || txt)) {
	// indent<element.name()/>
	xmlStr << STARTLINE(indent) << '<' << element.name();
	if ((element.name())[0] != '/')
	    xmlStr << '/';
	xmlStr << '>';
	if (root)
	    xmlStr << STARTLINE(indent) << enclose;
	return;
    }
    // <element.name()> or <element.name()
    xmlStr << STARTLINE(indent) << '<' << element.name();
    if (hasChild)
	xmlStr << '>';
    String sindent = indent;
    sindent << "  ";
    // Attributes
    const TiXmlAttribute* attr = element.firstAttribute();
    for (; attr; attr = attr->Next())
	xmlStr << STARTLINE(sindent) << attr->Name() << "=\""
	    << check(attr->Name(),attr->Value()) << '"';
    // Text. Filter some known elements to avoid output of passwords
    if (txt)
	xmlStr << STARTLINE(sindent) << check(element.name(),txt);
    // Children
    XMLElement* child = element.findFirstChild();
    String si = sindent;
    for (; child; child = element.findNextChild(child))
	print(xmlStr,*child,si);
    // End tag
    if (hasChild)
	xmlStr << STARTLINE(indent) << "</" << element.name() << '>';
    else
	xmlStr << STARTLINE(indent) << "/>";
    if (root)
	xmlStr << STARTLINE(indent) << enclose;
#undef STARTLINE
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
bool XMPPUtils::addChidren(XMLElement* dest, ObjList& list)
{
    if (!dest)
	return false;
    ObjList* o = list.skipNull();
    bool added = (0 != o);
    for (; o; o = o->skipNext()) {
	XMLElement* xml = static_cast<XMLElement*>(o->get());
	dest->addChild(new XMLElement(*xml));
    }
    return added;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
