/**
 * session.cpp
 * Yet Another Jingle Stack
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

#include <yatejingle.h>
#include <stdlib.h>

using namespace TelEngine;

static String s_bandwidth = "bandwidth";

const TokenDict JGRtpMediaList::s_media[] = {
    {"audio",     Audio},
    {0,0}
};

const TokenDict JGRtpCandidates::s_type[] = {
    {"ice-udp", RtpIceUdp},
    {"raw-udp", RtpRawUdp},
    {"p2p",     RtpP2P},
    {"google-raw-udp", RtpGoogleRawUdp},
    {0,0},
};

// The list containing the text values for Senders enumeration
const TokenDict JGSessionContent::s_senders[] = {
    {"both",       SendBoth},
    {"initiator",  SendInitiator},
    {"responder",  SendResponder},
    {0,0}
};

// The list containing the text values for Creator enumeration
const TokenDict JGSessionContent::s_creator[] = {
    {"initiator",  CreatorInitiator},
    {"responder",  CreatorResponder},
    {0,0}
};

// Jingle versions
const TokenDict JGSession::s_versions[] = {
    {"0",  Version0},
    {"1",  Version1},
    {0,0}
};

// Jingle session states
const TokenDict JGSession::s_states[] = {
    {"Idle",     Idle},
    {"Pending",  Pending},
    {"Active",   Active},
    {"Ending",   Ending},
    {"Destroy",  Destroy},
    {0,0}
};

// Jingle termination reasons
const TokenDict JGSession::s_reasons[] = {
    // Session terminate
    {"success",                  ReasonOk},
    {"busy",                     ReasonBusy},
    {"decline",                  ReasonDecline},
    {"cancel",                   ReasonCancel},
    {"expired",                  ReasonExpired},
    {"connectivity-error",       ReasonConn},
    {"failed-application",       ReasonFailApp},
    {"failed-transport",         ReasonFailTransport},
    {"gone",                     ReasonGone},
    {"incompatible-parameters",  ReasonParams},
    {"media-error",              ReasonMedia},
    {"unsupported-transports",   ReasonTransport},
    {"unsupported-applications", ReasonApp},
    {"general-error",            ReasonUnknown},
    {"general-error",            ReasonGeneral},
    {"alternative-session",      ReasonAltSess},
    {"timeout",                  ReasonTimeout},
    {"security-error",           ReasonSecurity},
    // Session transfer (XEP 0251)
    {"transferred",              Transferred},
    // RTP errors
    {"crypto-required",          CryptoRequired},
    {"invalid-crypto",           InvalidCrypto},
    {0,0}
};

// RTP session info (XEP 0167)
const TokenDict JGSession::s_rtpInfo[] = {
    {"active",                   RtpActive},
    {"hold",                     RtpHold},
    {"mute",                     RtpMute},
    {"ringing",                  RtpRinging},
    {0,0}
};

// Jingle actions for version 0
const TokenDict JGSession::s_actions0[] = {
    {"accept",                ActAccept},
    {"initiate",              ActInitiate},
    {"terminate",             ActTerminate},
    {"reject",                ActReject},
    {"info",                  ActInfo},
    {"transport-info",        ActTransportInfo},
    {"transport-accept",      ActTransportAccept},
    {"content-info",          ActContentInfo},
    {"candidates",            ActCandidates},
    {"DTMF",                  ActDtmf},
    {"ringing",               ActRinging},
    {"mute",                  ActMute},
    {0,0}
};

// Jingle actions for version 1
const TokenDict JGSession::s_actions1[] = {
    {"session-accept",        ActAccept},
    {"session-initiate",      ActInitiate},
    {"session-terminate",     ActTerminate},
    {"session-info",          ActInfo},
    {"description-info",      ActDescriptionInfo},
    {"transport-info",        ActTransportInfo},
    {"transport-accept",      ActTransportAccept},
    {"transport-reject",      ActTransportReject},
    {"transport-replace",     ActTransportReplace},
    {"content-accept",        ActContentAccept},
    {"content-add",           ActContentAdd},
    {"content-modify",        ActContentModify},
    {"content-reject",        ActContentReject},
    {"content-remove",        ActContentRemove},
    {"transfer",              ActTransfer},
    {"DTMF",                  ActDtmf},
    {"ringing",               ActRinging},
    {"trying",                ActTrying},
    {"received",              ActReceived},
    {"hold",                  ActHold},
    {"active",                ActActive},
    {"mute",                  ActMute},
    {"streamhost",            ActStreamHost},
    {0,0}
};

// Session flag names
const TokenDict JGSession::s_flagName[] = {
    {"noping",                FlagNoPing},
    {"ringnsrtp",             FlagRingNsRtp},
    {"nookinitiate",          FlagNoOkInitiate},
    {0,0}
};

// Output a debug message on unhandled actions
// Confirm received element
static void unhandledAction(JGSession* sess, XmlElement*& xml, int act,
    XmlElement* ch = 0)
{
    Debug(sess->engine(),DebugNote,
	"Call(%s). Unhandled action '%s' child=(%p,%s,%s) [%p]",
	sess->sid().c_str(),JGSession::lookupAction(act,sess->version()),
	ch,ch ? ch->tag() : 0,ch ? TelEngine::c_safe(ch->xmlns()) : 0,sess);
    sess->confirmError(xml,XMPPError::FeatureNotImpl);
}

// Decode a jingle termination reason
static void decodeJingleReason(XmlElement& xml, const char*& reason, const char*& text)
{
    String* ns = xml.xmlns();
    if (!ns)
	return;
    XmlElement* res = xml.findFirstChild(&XMPPUtils::s_tag[XmlTag::Reason],ns);
    if (!res)
	return;
    for (XmlElement* r = res->findFirstChild(); r; r = res->findNextChild(r)) {
	const String* t;
	const String* n;
	if (!(r->getTag(t,n) && n && *n == *ns))
	    continue;
	if (*t != XMPPUtils::s_tag[XmlTag::Text])
	    reason = *t;
	else
	    text = r->getText();
	if (reason && text)
	    return;
    }
}

// Utility: add session content(s) to an already created stanza's jingle child
static void addJingleContents(XmlElement* xml, const ObjList& contents, bool minimum,
    bool addDesc, bool addTrans, bool addCandidates, bool addAuth = true)
{
    if (!xml)
	return;
    XmlElement* jingle = XMPPUtils::findFirstChild(*xml,XmlTag::Jingle);
    if (!jingle)
	return;
    for (ObjList* o = contents.skipNull(); o; o = o->skipNext()) {
	JGSessionContent* c = static_cast<JGSessionContent*>(o->get());
	jingle->addChild(c->toXml(minimum,addDesc,addTrans,addCandidates,addAuth));
    }
}

// Utility: add session content(s) to an already created stanza's jingle child
// This method is used by the version 0 of the session
static void addJingleContents0(String& name, XmlElement* xml, const ObjList& contents, bool minimal,
    bool addDesc, bool addTrans, int action = JGSession::ActCount)
{
    if (!xml)
	return;
    XmlElement* jingle = XMPPUtils::findFirstChild(*xml,XmlTag::Session);
    if (!jingle)
	return;
    for (ObjList* o = contents.skipNull(); o; o = o->skipNext()) {
	JGSessionContent* c = static_cast<JGSessionContent*>(o->get());
	if (c->type() != JGSessionContent::RtpIceUdp)
	    continue;
	name = c->toString();
	if (addDesc) {
	    XmlElement* desc = XMPPUtils::createElement(XmlTag::Description,
		XMPPNamespace::JingleAudio);
	    for (ObjList* o = c->m_rtpMedia.skipNull(); o; o = o->skipNext()) {
		JGRtpMedia* a = static_cast<JGRtpMedia*>(o->get());
		desc->addChild(a->toXml());
	    }
	    c->m_rtpMedia.addTelEvent(desc);
	    jingle->addChild(desc);
	}
	if (addTrans) {
	    XmlElement* parent = 0;
	    if (action == JGSession::ActTransportInfo) {
		parent = XMPPUtils::createElement(XmlTag::Transport,
		    XMPPNamespace::JingleTransport);
		jingle->addChild(parent);
	    }
	    else if (action == JGSession::ActCandidates)
		parent = jingle;
	    if (!minimal && parent) {
		for (ObjList* o = c->m_rtpLocalCandidates.skipNull(); o; o = o->skipNext()) {
		    JGRtpCandidate* rc = static_cast<JGRtpCandidate*>(o->get());
		    XmlElement* xml = XMPPUtils::createElement(XmlTag::Candidate);
		    xml->setAttribute("name","rtp");
		    xml->setAttributeValid("generation",rc->m_generation);
		    xml->setAttributeValid("address",rc->m_address);
		    xml->setAttributeValid("port",rc->m_port);
		    xml->setAttributeValid("network","0");
		    xml->setAttributeValid("protocol",rc->m_protocol);
		    xml->setAttribute("username",c->m_rtpLocalCandidates.m_ufrag);
		    xml->setAttribute("password",c->m_rtpLocalCandidates.m_password);
		    xml->setAttributeValid("type","local");
		    xml->setAttributeValid("preference","1");
		    parent->addChild(xml);
		}
	    }
	}
    }
}

// Utility: add xml element child to an already created stanza's jingle child
static void addJingleChild(XmlElement* xml, XmlElement* child)
{
    if (!(xml && child))
	return;
    XmlElement* jingle = XMPPUtils::findFirstChild(*xml,XmlTag::Jingle);
    if (jingle)
	jingle->addChild(child);
    else
	TelEngine::destruct(child);
}

// Utility: add xml element child to an already created stanza's jingle child
static void addJingleChild0(XmlElement* xml, XmlElement* child)
{
    if (!(xml && child))
	return;
    XmlElement* jingle = XMPPUtils::findFirstChild(*xml,XmlTag::Session);
    if (jingle)
	jingle->addChild(child);
    else
	TelEngine::destruct(child);
}

// Utility: add NamedList param only if not empty
static inline void addParamValid(NamedList& list, const char* param, const char* value)
{
    if (null(param) || null(value))
	return;
    list.addParam(param,value);
}


/*
 * JGRtpMedia
 */
XmlElement* JGRtpMedia::toXml() const
{
    XmlElement* p = XMPPUtils::createElement(XmlTag::PayloadType);
    p->setAttribute("id",m_id);
    p->setAttributeValid("name",m_name);
    p->setAttributeValid("clockrate",m_clockrate);
    p->setAttributeValid("channels",m_channels);
    p->setAttributeValid("ptime",m_pTime);
    p->setAttributeValid("maxptime",m_maxPTime);
    if (m_bitRate) {
	p->setAttributeValid("bitrate",m_bitRate);
	p->addChild(XMPPUtils::createParameter("bitrate",m_bitRate));
    }
    unsigned int n = m_params.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* s = m_params.getParam(i);
	if (s)
	    p->addChild(XMPPUtils::createParameter(*s));
    }
    return p;
}

void JGRtpMedia::fromXml(XmlElement* xml)
{
    if (!xml) {
	set("","","");
	return;
    }
    set(xml->attribute("id"),xml->attribute("name"),
	xml->attribute("clockrate"),"",xml->attribute("channels"),
	xml->attribute("ptime"),xml->attribute("maxptime"),
	xml->attribute("bitrate"));
    XmlElement* param = XMPPUtils::findFirstChild(*xml,XmlTag::Parameter);
    for (; param; param = XMPPUtils::findNextChild(*xml,param,XmlTag::Parameter)) {
	const String* name = param->getAttribute(YSTRING("name"));
	if (!name)
	    continue;
	if (*name == YSTRING("bitrate"))
	    m_bitRate = param->attribute(YSTRING("value"));
	else
	    m_params.addParam(*name,param->attribute(YSTRING("value")));
    }
}


/*
 * JGCrypto
 */
XmlElement* JGCrypto::toXml() const
{
    XmlElement* xml = XMPPUtils::createElement(XmlTag::Crypto);
    xml->setAttributeValid("crypto-suite",m_suite);
    xml->setAttributeValid("key-params",m_keyParams);
    xml->setAttributeValid("session-params",m_sessionParams);
    xml->setAttributeValid("tag",toString());
    return xml;
}

void JGCrypto::fromXml(const XmlElement* xml)
{
    if (!xml)
	return;
    m_suite = xml->getAttribute("crypto-suite");
    m_keyParams = xml->getAttribute("key-params");
    m_sessionParams = xml->getAttribute("session-params");
    assign(xml->attribute("tag"));
}

// Build an 'encryption' element from a list of crypto objects
// XEP 0167 Section 7
XmlElement* JGCrypto::buildEncryption(const ObjList& list, bool required)
{
    ObjList* c = list.skipNull();
    if (!c)
	return 0;
    XmlElement* enc = XMPPUtils::createElement(XmlTag::Encryption);
    enc->setAttribute("required",String::boolText(required));
    for (; c; c = c->skipNext())
	enc->addChild((static_cast<JGCrypto*>(c->get()))->toXml());
    return enc;
}

// Decode an 'encryption' element. Clear the list before starting
// XEP 0167 Section 7
void JGCrypto::decodeEncryption(const XmlElement* xml, ObjList& list, bool& required)
{
    list.clear();
    required = false;
    XmlElement* c = xml ? XMPPUtils::findFirstChild(*xml,XmlTag::Crypto) : 0;
    if (!c)
	return;
    String* req = xml->getAttribute("required");
    if (req)
	required = (*req == "true") || (*req == "1");
    else
	required = false;
    for (; c; c = XMPPUtils::findNextChild(*xml,c,XmlTag::Crypto))
	list.append(new JGCrypto(c));
}


/*
 * JGRtpMediaList
 */
// Reset the list and data
void JGRtpMediaList::reset()
{
    clear();
    m_ready = false;
    m_media = MediaMissing;
    m_cryptoRequired = false;
    m_cryptoLocal.clear();
    m_cryptoRemote.clear();
    m_ssrc.clear();
    TelEngine::destruct(m_bandwidth);
}

// Copy media type and payloads from another list
void JGRtpMediaList::setMedia(const JGRtpMediaList& src, const String& only)
{
    clear();
    m_media = src.m_media;
    m_telEvent = src.m_telEvent;
    if (only) {
	// Copy media types in synonym order
	ObjList* f = only.split(',',false);
	for (ObjList* o = f->skipNull(); o; o = o->skipNext()) {
	    JGRtpMedia* media = src.findSynonym(o->get()->toString());
	    if (!media || find(media->toString()))
		continue;
	    append(new JGRtpMedia(*media));
	}
	TelEngine::destruct(f);
    }
    else {
	// Copy media in source order
	for (ObjList* o = src.skipNull(); o; o = o->skipNext()) {
	    JGRtpMedia* media = static_cast<JGRtpMedia*>(o->get());
	    if (find(media->toString()))
		continue;
	    append(new JGRtpMedia(*media));
	}
    }
}

// Filter media list, remove unwanted types
void JGRtpMediaList::filterMedia(const String& only)
{
    if (only.null())
	return;
    ObjList* f = only.split(',',false);
    ListIterator iter(*this);
    while (JGRtpMedia* media = static_cast<JGRtpMedia*>(iter.get())) {
	const String& name = media->m_synonym.null() ? media->m_name : media->m_synonym;
	if (!(f->find(name)))
	    remove(media);
    }
    TelEngine::destruct(f);
}

// Find a data payload by its id
JGRtpMedia* JGRtpMediaList::findMedia(const String& id)
{
    ObjList* obj = find(id);
    return obj ? static_cast<JGRtpMedia*>(obj->get()) : 0;
}

// Find a data payload by its synonym
JGRtpMedia* JGRtpMediaList::findSynonym(const String& value) const
{
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	JGRtpMedia* a = static_cast<JGRtpMedia*>(o->get());
	if (value == a->m_synonym)
	    return a;
    }
    return 0;
}

// Create a 'description' element and add payload children to it
XmlElement* JGRtpMediaList::toXml() const
{
    if (m_media != Audio)
	return 0;
    XmlElement* desc = XMPPUtils::createElement(XmlTag::Description,
       XMPPNamespace::JingleAppsRtp);
    desc->setAttributeValid("media",lookup(m_media,s_media));
    desc->setAttributeValid("ssrc",m_ssrc);
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
       JGRtpMedia* a = static_cast<JGRtpMedia*>(o->get());
       desc->addChild(a->toXml());
    }
    addTelEvent(desc);
    // Bandwidth
    if (m_bandwidth && *m_bandwidth) {
	XmlElement* b = XMPPUtils::createElement(s_bandwidth,*m_bandwidth);
	b->setAttribute("type",m_bandwidth->name());
	desc->addChild(b);
    }
    // Encryption
    XmlElement* enc = JGCrypto::buildEncryption(m_cryptoLocal,m_cryptoRequired);
    if (enc)
	desc->addChild(enc);
    return desc;
}

// Fill this list from an XML element's children. Clear before attempting to fill
void JGRtpMediaList::fromXml(XmlElement* xml)
{
    reset();
    if (!xml)
	return;
    m_media = (Media)lookup(xml->attribute("media"),s_media,MediaUnknown);
    m_ssrc = xml->getAttribute("ssrc");
    String* ns = xml->xmlns();
    if (!ns)
	return;
    XmlElement* x = 0;
    while (0 != (x = xml->findNextChild(x))) {
	const String* tag = 0;
	const String* n = 0;
	if (!(x->getTag(tag,n) && n && *n == *ns))
	    continue;
	if (*tag == XMPPUtils::s_tag[XmlTag::PayloadType])
	    ObjList::append(new JGRtpMedia(x));
	else if (*tag == XMPPUtils::s_tag[XmlTag::Encryption])
	    JGCrypto::decodeEncryption(x,m_cryptoRemote,m_cryptoRequired);
	else if (*tag == s_bandwidth) {
	    if (m_bandwidth)
		continue;
	    String* type = x->getAttribute("type");
	    if (!TelEngine::null(type))
		m_bandwidth = new NamedString(*type,x->getText());
	}
    }
}

// Create a list from data payloads
bool JGRtpMediaList::createList(String& dest, bool synonym, const char* sep)
{
    dest = "";
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	JGRtpMedia* a = static_cast<JGRtpMedia*>(o->get());
	dest.append(synonym ? a->m_synonym : a->m_name,sep);
    }
    return (0 != dest.length());
}

// Build and add telephone-event media child to a parent xml element
void JGRtpMediaList::addTelEvent(XmlElement* xml, const char* name) const
{
    if (!xml)
	return;
    if (TelEngine::null(name))
	name = m_telEventName;
    if (m_telEvent < 96 || m_telEvent > 127)
	return;
    String id(m_telEvent);
    if (!TelEngine::null(name)) {
	JGRtpMedia* m = new JGRtpMedia(id,name,"8000","");
	xml->addChild(m->toXml());
	TelEngine::destruct(m);
    }
    if (m_telEventName2 && m_telEventName2 != name) {
	JGRtpMedia* m = new JGRtpMedia(id,m_telEventName2,"8000","");
	xml->addChild(m->toXml());
	TelEngine::destruct(m);
    }
}


/*
 * JGRtpCandidate
 */
// Create a 'candidate' element from this object
XmlElement* JGRtpCandidate::toXml(const JGRtpCandidates& container) const
{
    if (container.m_type == JGRtpCandidates::Unknown)
	return 0;
    XmlElement* xml = XMPPUtils::createElement(XmlTag::Candidate);
    xml->setAttributeValid("component",m_component);
    xml->setAttributeValid("generation",m_generation);
    if (container.m_type == JGRtpCandidates::RtpIceUdp)
	xml->setAttributeValid("foundation",toString());
    else if (container.m_type == JGRtpCandidates::RtpRawUdp)
	xml->setAttributeValid("id",toString());
    xml->setAttributeValid("ip",m_address);
    xml->setAttributeValid("port",m_port);
    if (container.m_type == JGRtpCandidates::RtpIceUdp) {
	xml->setAttributeValid("network",m_network);
	xml->setAttributeValid("priority",m_priority);
	xml->setAttributeValid("protocol",m_protocol);
	xml->setAttributeValid("type",m_type);
    }
    return xml;
}

// Fill this object from a candidate element
void JGRtpCandidate::fromXml(XmlElement* xml, const JGRtpCandidates& container)
{
    if (!xml || container.m_type == JGRtpCandidates::Unknown)
	return;
    if (container.m_type == JGRtpCandidates::RtpIceUdp)
	assign(xml->attribute("foundation"));
    else if (container.m_type == JGRtpCandidates::RtpRawUdp)
	assign(xml->attribute("id"));
    m_component = xml->getAttribute("component");
    m_generation = xml->getAttribute("generation");
    m_address = xml->getAttribute("ip");
    m_port = xml->getAttribute("port");
    if (container.m_type == JGRtpCandidates::RtpIceUdp) {
	m_network = xml->getAttribute("network");
	m_priority = xml->getAttribute("priority");
	m_protocol = xml->getAttribute("protocol");
	m_type = xml->getAttribute("type");
    }
}


/*
 * JGRtpCandidateP2P
 */
// Create a 'candidate' element from this object
XmlElement* JGRtpCandidateP2P::toXml(const JGRtpCandidates& container) const
{
    if (container.m_type != JGRtpCandidates::RtpP2P &&
	container.m_type != JGRtpCandidates::RtpGoogleRawUdp)
	return 0;
    int ns = XMPPNamespace::Count;
    if (container.m_type != JGRtpCandidates::RtpP2P)
	ns = XMPPNamespace::JingleTransport;
    XmlElement* xml = XMPPUtils::createElement(XmlTag::Candidate,ns);
    xml->setAttribute("name","rtp");
    xml->setAttributeValid("generation",m_generation);
    xml->setAttributeValid("address",m_address);
    xml->setAttributeValid("port",m_port);
    xml->setAttributeValid("network","0");
    xml->setAttributeValid("protocol",m_protocol);
    xml->setAttribute("username",m_username);
    xml->setAttribute("password",m_password);
    xml->setAttributeValid("type","local");
    xml->setAttributeValid("preference","1");
    return xml;
}

// Fill this object from a candidate element
void JGRtpCandidateP2P::fromXml(XmlElement* xml, const JGRtpCandidates& container)
{
    if (!xml || (container.m_type != JGRtpCandidates::RtpP2P &&
	container.m_type != JGRtpCandidates::RtpGoogleRawUdp))
	return;
    m_component = "1";
    m_generation = xml->attribute("generation");
    m_address = xml->attribute("address");
    m_port = xml->attribute("port");
    m_protocol = xml->attribute("protocol");;
    m_generation = xml->attribute("generation");
    m_type = xml->attribute("type");
    m_username = xml->attribute("username");
    m_password = xml->attribute("password");
}


/*
 * JGRtpCandidates
 */
// Create a 'transport' element from this object. Add
XmlElement* JGRtpCandidates::toXml(bool addCandidates, bool addAuth) const
{
    XMPPNamespace::Type ns;
    if (m_type == RtpIceUdp)
	ns = XMPPNamespace::JingleTransportIceUdp;
    else if (m_type == RtpRawUdp)
	ns = XMPPNamespace::JingleTransportRawUdp;
    else if (m_type == RtpP2P)
	ns = XMPPNamespace::JingleTransport;
    else if (m_type == RtpGoogleRawUdp)
	ns = XMPPNamespace::JingleTransportGoogleRawUdp;
    else
	return 0;
    XmlElement* trans = XMPPUtils::createElement(XmlTag::Transport,ns);
    if (addAuth && m_type == RtpIceUdp) {
	trans->setAttributeValid("pwd",m_password);
	trans->setAttributeValid("ufrag",m_ufrag);
    }
    if (addCandidates)
	for (ObjList* o = skipNull(); o; o = o->skipNext())
	    trans->addChild((static_cast<JGRtpCandidate*>(o->get()))->toXml(*this));
    return trans;
}

// Fill this object from a given element
void JGRtpCandidates::fromXml(XmlElement* element)
{
    clear();
    m_type = Unknown;
    m_password = "";
    m_ufrag = "";
    if (!element)
	return;
    // Set transport data
    int ns = XMPPUtils::xmlns(*element);
    int candidateNs = ns;
    if (ns == XMPPNamespace::JingleTransportIceUdp)
	m_type = RtpIceUdp;
    else if (ns == XMPPNamespace::JingleTransportRawUdp)
	m_type = RtpRawUdp;
    else if (ns == XMPPNamespace::JingleTransport)
	m_type = RtpP2P;
    else if (ns == XMPPNamespace::JingleTransportGoogleRawUdp) {
	m_type = RtpGoogleRawUdp;
	candidateNs = XMPPNamespace::JingleTransport;
    }
    else
	return;
    if (m_type != RtpP2P && m_type != RtpGoogleRawUdp) {
	m_password = element->getAttribute("pwd");
	m_ufrag = element->getAttribute("ufrag");
    }
    // Get candidates
    XmlElement* c = XMPPUtils::findFirstChild(*element,XmlTag::Candidate,candidateNs);
    for (; c; c = XMPPUtils::findNextChild(*element,c,XmlTag::Candidate,candidateNs))
	if (candidateNs != XMPPNamespace::JingleTransport)
	    append(new JGRtpCandidate(c,*this));
	else
	    append(new JGRtpCandidateP2P(c,*this));
}

// Find a candidate by its component value
JGRtpCandidate* JGRtpCandidates::findByComponent(unsigned int component)
{
    String tmp(component);
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	JGRtpCandidate* c = static_cast<JGRtpCandidate*>(o->get());
	if (c->m_component == tmp)
	    return c;
    }
    return 0;
}

// Generate a random password or username to be used with ICE-UDP transport
void JGRtpCandidates::generateIceToken(String& dest, bool pwd, unsigned int max)
{
    if (pwd) {
	if (max < 22)
	    max = 22;
    }
    else if (max < 4)
	max = 4;
    if (max > 256)
	max = 256;
    dest = "";
    while (dest.length() < max)
 	dest << (int)Random::random();
    dest = dest.substr(0,max);
}

// Generate a random password or username to be used with old ICE-UDP transport
void JGRtpCandidates::generateOldIceToken(String& dest)
{
    dest = "";
    while (dest.length() < 16)
 	dest << (int)Random::random();
    dest = dest.substr(0,16);
}


/*
 * JGSessionContent
 */
// Constructor
JGSessionContent::JGSessionContent(Type t, const char* name, Senders senders,
    Creator creator, const char* disposition)
    : m_fileTransfer(""),
    m_type(t), m_name(name), m_senders(senders), m_creator(creator),
    m_disposition(disposition)
{
}

// Build a 'content' XML element from this object
XmlElement* JGSessionContent::toXml(bool minimum, bool addDesc,
    bool addTrans, bool addCandidates, bool addAuth) const
{
    XmlElement* xml = XMPPUtils::createElement(XmlTag::Content);
    xml->setAttributeValid("name",m_name);
    xml->setAttributeValid("creator",lookup(m_creator,s_creator));
    if (!minimum) {
	xml->setAttributeValid("senders",lookup(m_senders,s_senders));
	xml->setAttributeValid("disposition",m_disposition);
    }
    // Add description and transport
    XmlElement* desc = 0;
    XmlElement* trans = 0;
    if (m_type == RtpIceUdp || m_type == RtpRawUdp || m_type == RtpP2P ||
	m_type == RtpGoogleRawUdp) {
	// Audio content
	if (addDesc)
	    desc = m_rtpMedia.toXml();
	if (addTrans)
	    trans = m_rtpLocalCandidates.toXml(addCandidates,addAuth);
    }
    else if (m_type == FileBSBOffer || m_type == FileBSBRequest) {
	// File transfer content
	XmlElement* file = XMPPUtils::createElement(XmlTag::File,
	    XMPPNamespace::SIProfileFileTransfer);
	unsigned int n = m_fileTransfer.length();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* ns = m_fileTransfer.getParam(i);
	    if (ns)
		file->setAttributeValid(ns->name(),*ns);
	}
	XmlElement* child = 0;
	if (m_type == FileBSBOffer)
	    child = XMPPUtils::createElement(XmlTag::Offer);
	else
	    child = XMPPUtils::createElement(XmlTag::Request);
	child->addChild(file);
	desc = XMPPUtils::createElement(XmlTag::Description,
	    XMPPNamespace::JingleAppsFileTransfer);
	desc->addChild(child);
	trans = XMPPUtils::createElement(XmlTag::Transport,
	    XMPPNamespace::JingleTransportByteStreams);
    }
    xml->addChild(desc);
    xml->addChild(trans);
    return xml;
}

// Build a content object from an XML element
JGSessionContent* JGSessionContent::fromXml(XmlElement* xml, XMPPError::Type& err,
    String& error)
{
    static const char* errAttr = "Required attribute is missing: ";
    static const char* errAttrValue = "Invalid attribute value: ";

    if (!xml) {
	err = XMPPError::Internal;
	return 0;
    }

    err = XMPPError::NotAcceptable;

    const char* name = xml->attribute("name");
    if (!(name && *name)) {
	error << errAttr << "name";
	return 0;
    }
    // Creator (default: initiator)
    Creator creator = CreatorInitiator;
    const char* tmp = xml->attribute("creator");
    if (tmp)
	creator = (Creator)lookup(tmp,s_creator,CreatorUnknown);
    if (creator == CreatorUnknown) {
	error << errAttrValue << "creator";
	return 0;
    }
    // Senders (default: both)
    Senders senders = SendBoth;
    tmp = xml->attribute("senders");
    if (tmp)
	senders = (Senders)lookup(tmp,s_senders,SendUnknown);
    if (senders == SendUnknown) {
	error << errAttrValue << "senders";
	return 0;
    }

    JGSessionContent* content = new JGSessionContent(Unknown,name,senders,creator,
	xml->attribute("disposition"));
    err = XMPPError::NoError;
    // Use a while() to go to end and cleanup data
    while (true) {
	int offer = -1;
	// Check description
	XmlElement* desc = XMPPUtils::findFirstChild(*xml,XmlTag::Description);
	if (desc) {
	    if (XMPPUtils::hasXmlns(*desc,XMPPNamespace::JingleAppsRtp))
		content->m_rtpMedia.fromXml(desc);
	    else if (XMPPUtils::hasXmlns(*desc,XMPPNamespace::JingleAppsFileTransfer)) {
		content->m_type = UnknownFileTransfer;
		// Get file and type
		XmlElement* dir = XMPPUtils::findFirstChild(*desc,XmlTag::Offer);
		if (dir)
		    offer = 1;
		else {
		    dir = XMPPUtils::findFirstChild(*desc,XmlTag::Request);
		    if (dir)
			offer = 0;
		}
		if (dir) {
		    XmlElement* file = XMPPUtils::findFirstChild(*dir,XmlTag::File);
		    if (file && XMPPUtils::hasXmlns(*file,XMPPNamespace::SIProfileFileTransfer)) {
			addParamValid(content->m_fileTransfer,"name",file->attribute("name"));
			addParamValid(content->m_fileTransfer,"size",file->attribute("size"));
			addParamValid(content->m_fileTransfer,"hash",file->attribute("hash"));
			addParamValid(content->m_fileTransfer,"date",file->attribute("date"));
		    }
		    else
			offer = -1;
		}
	    }
	    else
		content->m_rtpMedia.m_media = JGRtpMediaList::MediaUnknown;
	}
	else
	    content->m_rtpMedia.m_media = JGRtpMediaList::MediaMissing;

	// Check transport
	XmlElement* trans = XMPPUtils::findFirstChild(*xml,XmlTag::Transport);
	if (trans) {
	    if (content->type() != UnknownFileTransfer) {
		content->m_rtpRemoteCandidates.fromXml(trans);
		if (content->m_rtpRemoteCandidates.m_type == JGRtpCandidates::RtpIceUdp)
		    content->m_type = RtpIceUdp;
		else if (content->m_rtpRemoteCandidates.m_type == JGRtpCandidates::RtpRawUdp)
		    content->m_type = RtpRawUdp;
		else if (content->m_rtpRemoteCandidates.m_type == JGRtpCandidates::RtpP2P)
		    content->m_type = RtpP2P;
		else if (content->m_rtpRemoteCandidates.m_type == JGRtpCandidates::RtpGoogleRawUdp)
		    content->m_type = RtpGoogleRawUdp;
	    }
	    else {
		if (offer >= 0) {
		    if (XMPPUtils::hasXmlns(*trans,XMPPNamespace::JingleTransportByteStreams))
			content->m_type = offer ? FileBSBOffer : FileBSBRequest;
		}
	    }
	}
	else
	    content->m_rtpRemoteCandidates.m_type = JGRtpCandidates::Unknown;

	break;
    }
    if (err == XMPPError::NoError)
	return content;
    TelEngine::destruct(content);
    return 0;
}


/*
 * JGStreamHost
 */

// Build an XML element from this stream host
XmlElement* JGStreamHost::toXml()
{
    if (!length())
	return 0;
    XmlElement* xml = XMPPUtils::createElement(XmlTag::StreamHost);
    xml->setAttribute("jid",c_str());
    if (m_zeroConf.null()) {
	xml->setAttribute("host",m_address);
	xml->setAttribute("port",String(m_port));
    }
    else
	xml->setAttribute("zeroconf",m_zeroConf);
    return xml;
}

// Build a stream host from an XML element
JGStreamHost* JGStreamHost::fromXml(XmlElement* xml)
{
    if (!xml)
	return 0;
    const char* jid = xml->attribute("jid");
    if (TelEngine::null(jid))
	return 0;
    return new JGStreamHost(false,jid,xml->attribute("host"),
	String(xml->attribute("port")).toInteger(-1),xml->attribute("zeroconf"));
}

// Build a query XML element carrying a list of stream hosts
XmlElement* JGStreamHost::buildHosts(const ObjList& hosts, const char* sid,
    const char* mode)
{
    XmlElement* xml = XMPPUtils::createElement(XmlTag::Query,
	XMPPNamespace::ByteStreams);
    xml->setAttribute("sid",sid);
    xml->setAttribute("mode",mode);
    for (ObjList* o = hosts.skipNull(); o; o = o->skipNext())
	xml->addChild((static_cast<JGStreamHost*>(o->get()))->toXml());
    return xml;
}

// Build a query XML element with a streamhost-used child
XmlElement* JGStreamHost::buildRsp(const char* jid)
{
    XmlElement* xml = XMPPUtils::createElement(XmlTag::Query,
	XMPPNamespace::ByteStreams);
    XmlElement* used = XMPPUtils::createElement(XmlTag::StreamHostUsed);
    used->setAttribute("jid",jid);
    xml->addChild(used);
    return xml;
}


/*
 * JGSession
 */
// Create an outgoing session
JGSession::JGSession(Version ver, JGEngine* engine,
    const JabberID& caller, const JabberID& called)
    : Mutex(true,"JGSession"),
    m_version(ver),
    m_state(Idle),
    m_flags(engine->sessionFlags()),
    m_timeToPing(0),
    m_engine(engine),
    m_outgoing(true),
    m_local(caller),
    m_remote(called),
    m_lastEvent(0),
    m_recvTerminate(false),
    m_private(0),
    m_stanzaId(1)
{
    // Make sure we don't ping before session-initiate times out
    if (m_engine->pingInterval())
	m_timeToPing = Time::msecNow() + m_engine->stanzaTimeout() + m_engine->pingInterval();
    m_engine->createSessionId(m_localSid);
    m_sid = m_localSid;
    Debug(m_engine,DebugAll,"Call(%s). Outgoing from=%s to=%s [%p]",
	m_sid.c_str(),m_local.c_str(),m_remote.c_str(),this);
}

// Create an incoming session
JGSession::JGSession(Version ver, JGEngine* engine, const JabberID& caller,
	const JabberID& called, XmlElement* xml, const String& id)
    : Mutex(true,"JGSession"),
    m_version(ver),
    m_state(Idle),
    m_flags(engine->sessionFlags()),
    m_timeToPing(0),
    m_engine(engine),
    m_outgoing(false),
    m_sid(id),
    m_local(caller),
    m_remote(called),
    m_lastEvent(0),
    m_recvTerminate(false),
    m_private(0),
    m_stanzaId(1)
{
    if (m_engine->pingInterval())
	m_timeToPing = Time::msecNow() + m_engine->pingInterval();
    m_queue.addChild(xml);
    m_engine->createSessionId(m_localSid);
    Debug(m_engine,DebugAll,"Call(%s). Incoming from=%s to=%s [%p]",
	m_sid.c_str(),m_remote.c_str(),m_local.c_str(),this);
}

// Destructor: hangup, cleanup, remove from engine's list
JGSession::~JGSession()
{
    XDebug(m_engine,DebugAll,"JGSession::~JGSession() [%p]",this);
}

// Get an action (jingle element type) from a jingle element
JGSession::Action JGSession::getAction(XmlElement* xml)
{
    if (!xml)
	return ActCount;
    const char* act = xml->attribute("action");
    if (!act)
	act = xml->attribute("type");
    return lookupAction(act,m_version);
}

// Ask this session to accept an incoming xml element
bool JGSession::acceptIq(XMPPUtils::IqType type, const JabberID& from, const JabberID& to,
    const String& id, XmlElement* xml)
{
    if (!(xml && id))
	return false;
    // Check to/from
    if (m_local != to || m_remote != from)
	return false;
    // Requests must match the session id
    // Responses' id must start with session's local id (this is the way we generate the stanza id)
    switch (type) {
	case XMPPUtils::IqSet:
	    if (id != m_sid)
		return false;
	    break;
	case XMPPUtils::IqResult:
	case XMPPUtils::IqError:
	    if (!id.startsWith(m_localSid))
		return false;
	    // TODO: check sent stanzas queue to match the id
	    break;
	default:
	    return false;
    }
    // Ok
    Lock lock(this);
    m_queue.addChild(xml);
    DDebug(m_engine,DebugAll,"Call(%s). Accepted xml (%p,%s) [%p]",
	m_sid.c_str(),xml,xml->tag(),this);
    return true;
}

// Confirm (send result) a received element
bool JGSession::confirmResult(XmlElement* xml)
{
    if (!xml)
	return false;
    const char* id = xml->attribute("id");
    XmlElement* iq = XMPPUtils::createIqResult(m_local,m_remote,id);
    // The receiver will detect which stanza is confirmed by id
    // If missing, make a copy of the received element and attach it to the error
    if (TelEngine::null(id)) {
	XmlElement* copy = new XmlElement(*xml);
	iq->addChild(copy);
    }
    return sendStanza(iq,0,false);
}

// Confirm (send error) a received element
bool JGSession::confirmError(XmlElement*& xml, XMPPError::Type error,
    const char* text, XMPPError::ErrorType type)
{
    XmlElement* iq = XMPPUtils::createIqError(m_local,m_remote,xml,type,error,text);
    return sendStanza(iq,0,false);
}

// Close a Pending or Active session
bool JGSession::hangup(XmlElement* reason)
{
    Lock lock(this);
    if (state() != Pending && state() != Active) {
	TelEngine::destruct(reason);
	return false;
    }
    DDebug(m_engine,DebugAll,"Call(%s). Hangup(%p) [%p]",m_sid.c_str(),reason,this);
    // Clear sent stanzas list. We will wait for this element to be confirmed
    m_sentStanza.clear();
    XmlElement* xml = createJingle(ActTerminate,reason);
    bool ok = sendStanza(xml);
    changeState(Ending);
    return ok;
}

// Build SOCKS SHA1 dst.addr used by file transfer
void JGSession::buildSocksDstAddr(String& buf)
{
    SHA1 sha(m_sid);
    if (outgoing())
	sha << m_local << m_remote;
    else
	sha << m_remote << m_local;
    buf = sha.hexDigest();
}

// Send a session info element to the remote peer
bool JGSession::sendInfo(XmlElement* xml, String* stanzaId, XmlElement* extra)
{
    if (!xml) {
	TelEngine::destruct(extra);
	return false;
    }
    // Make sure we dont't terminate the session if info fails
    String tmp;
    if (!stanzaId) {
	tmp = "Info" + String(Time::secNow());
	stanzaId = &tmp;
    }
    return sendStanza(createJingle(ActInfo,xml,extra),stanzaId);
}

// Send a dtmf string to remote peer
bool JGSession::sendDtmf(const char* dtmf, unsigned int msDuration, String* stanzaId)
{
    if (!(dtmf && *dtmf))
	return false;

    XmlElement* iq = createJingle(version() != Version0 ? ActInfo : ActContentInfo);
    XmlElement* sess = iq->findFirstChild();
    if (!sess) {
	TelEngine::destruct(iq);
	return false;
    }
    char s[2] = {0,0};
    while (*dtmf) {
	s[0] = *dtmf++;
	sess->addChild(createDtmf(s,msDuration));
    }
    return sendStanza(iq,stanzaId);
}

// Check if the remote party supports a given feature
bool JGSession::hasFeature(XMPPNamespace::Type feature)
{
    return false;
}

// Build a transfer element
XmlElement* JGSession::buildTransfer(const String& transferTo,
    const String& transferFrom, const String& sid)
{
    XmlElement* transfer = XMPPUtils::createElement(XmlTag::Transfer,
	XMPPNamespace::JingleTransfer);
    transfer->setAttributeValid("from",transferFrom);
    transfer->setAttributeValid("to",transferTo);
    transfer->setAttributeValid("sid",sid);
    return transfer;
}

// Process received events. Generate Jingle events
JGEvent* JGSession::getEvent(u_int64_t time)
{
    Lock lock(this);
    if (m_lastEvent)
	return 0;
    if (state() == Destroy)
	return 0;
    // Deque and process xml
    // Loop until a jingle event is generated or no more xml in queue
    XmlElement* xml = 0;
    while (true) {
	TelEngine::destruct(xml);
	xml = static_cast<XmlElement*>(m_queue.pop());
	if (!xml)
	    break;

	DDebug(m_engine,DebugAll,"Call(%s). Dequeued xml (%p,%s) ns=%s in state %s [%p]",
	    m_sid.c_str(),xml,xml->tag(),TelEngine::c_safe(xml->xmlns()),
	    lookupState(state()),this);

	// Update ping interval
	if (m_engine->pingInterval())
	    m_timeToPing = time + m_engine->pingInterval();
	else
	    m_timeToPing = 0;

	XMPPUtils::IqType t = XMPPUtils::iqType(xml->attribute("type"));
	// Process Jingle 'set' stanzas and file transfer
	if (t == XMPPUtils::IqSet || t == XMPPUtils::IqGet) {
	    XmlElement* child = xml->findFirstChild();
	    if (!child || t == XMPPUtils::IqGet) {
		confirmError(xml,XMPPError::BadRequest);
		if (!outgoing() && state() == Idle) {
		    m_lastEvent = new JGEvent(JGEvent::Destroy,this);
		    break;
		}
	    }
	    int ns = XMPPUtils::xmlns(*child);
	    // Jingle
	    if (ns == XMPPNamespace::Jingle || ns == XMPPNamespace::JingleSession) {
		// Filter some conditions in which we can't accept any jingle stanza
		// Outgoing idle sessions are waiting for the user to initiate them
		if (state() == Idle && outgoing()) {
		    confirmError(xml,XMPPError::Request);
		    continue;
		}
		JGEvent* event = decodeJingle(xml,child);
		if (!event) {
		    // Destroy incoming session if session initiate stanza contains errors
		    if (!outgoing() && state() == Idle) {
			m_lastEvent = new JGEvent(JGEvent::Destroy,this);
			// TODO: hangup
			break;
		    }
		    continue;
		}
		if (event->action() != ActInfo) {
		    m_lastEvent = processJingleSetEvent(event);
		    if (m_lastEvent)
			break;
		}
		else {
		    // ActInfo with empty session info: PING
		    XDebug(m_engine,DebugAll,"Call(%s). Received empty '%s' (ping) [%p]",
			m_sid.c_str(),event->actionName(),this);
		    event->confirmElement();
		    delete event;
		}
		continue;
	    }
	    // File transfer iq
	    if (ns == XMPPNamespace::ByteStreams) {
		m_lastEvent = processFileTransfer(t == XMPPUtils::IqSet,xml,child);
		if (m_lastEvent)
		    break;
	    }
	    else
		DDebug(m_engine,DebugStub,"Call(%s). Unhandled ns=%s [%p]",
		    m_sid.c_str(),TelEngine::c_safe(xml->xmlns()),this);
	    confirmError(xml,XMPPError::ServiceUnavailable);
	    if (!outgoing() && state() == Idle) {
		m_lastEvent = new JGEvent(JGEvent::Destroy,this);
		break;
	    }
	    continue;
	}

	// Process Jingle 'set' stanzas and file transfer
	if (t == XMPPUtils::IqResult || t == XMPPUtils::IqError) {
	    m_lastEvent = processJabberIqResponse(t == XMPPUtils::IqResult,xml);
	    if (m_lastEvent)
		break;
	    continue;
	}

	confirmError(xml,XMPPError::ServiceUnavailable);
	continue;
    }
    TelEngine::destruct(xml);

    // No event: check first sent stanza's timeout
    if (!m_lastEvent) {
	ObjList* o = m_sentStanza.skipNull();
	JGSentStanza* tmp = o ? static_cast<JGSentStanza*>(o->get()) : 0;
	if (tmp && tmp->timeout(time)) {
	    Debug(m_engine,DebugNote,"Call(%s). Sent stanza ('%s') timed out [%p]",
		m_sid.c_str(),tmp->c_str(),this);
	    // Don't terminate if the sender requested to be notified
	    m_lastEvent = new JGEvent(tmp->notify() ? JGEvent::ResultTimeout : JGEvent::Terminated,
		this,0,"timeout");
	    m_lastEvent->m_id = *tmp;
	    o->remove();
	    if (m_lastEvent->final())
		hangup(createReason(ReasonTimeout,"Stanza timeout"));
	}
    }

    if (m_lastEvent) {
	// Deref the session for final events
	if (m_lastEvent->final()) {
	    changeState(Destroy);
	    deref();
	}
	DDebug(m_engine,DebugAll,
	    "Call(%s). Raising event (%p,%u) action=%s final=%s [%p]",
	    m_sid.c_str(),m_lastEvent,m_lastEvent->type(),
	    m_lastEvent->actionName(),String::boolText(m_lastEvent->final()),this);
	return m_lastEvent;
    }

    // Ping the remote party
    if (!flag(FlagNoPing))
	sendPing(time);

    return 0;
}

// Release this session and its memory
void JGSession::destroyed()
{
    hangup();
    // Remove from engine
    if (m_engine) {
	Lock lock(m_engine);
	m_engine->m_sessions.remove(this,false);
    }
    DDebug(m_engine,DebugInfo,"Call(%s). Destroyed [%p]",m_sid.c_str(),this);
}

// Send a stanza to the remote peer
bool JGSession::sendStanza(XmlElement* stanza, String* stanzaId, bool confirmation,
    bool ping, unsigned int toutMs)
{
    if (!stanza)
	return false;
    Lock lock(this);
    // confirmation=true: this is not a response, don't allow if terminated
    bool terminated = (state() == Ending || state() == Destroy);
    if (terminated && confirmation) {
#ifdef DEBUG
	Debug(m_engine,DebugNote,
	    "Call(%s). Can't send stanza (%p,'%s') in state %s [%p]",
	    m_sid.c_str(),stanza,stanza->tag(),lookupState(m_state),this);
#endif
	TelEngine::destruct(stanza);
	return false;
    }
    DDebug(m_engine,DebugAll,"Call(%s). Sending stanza (%p,'%s') id=%s [%p]",
	m_sid.c_str(),stanza,stanza->tag(),String::boolText(stanzaId != 0),this);
    // Check if the stanza should be added to the list of stanzas requiring confirmation
    if (confirmation && XMPPUtils::isUnprefTag(*stanza,XmlTag::Iq)) {
	Action act = ActCount;
	XmlElement* child = stanza->findFirstChild();
	if (child) {
	    act = lookupAction(child->attribute("action"),m_version);
	    if (act == ActInfo) {
		child = child->findFirstChild();
		if (child) {
		    Action over = lookupAction(child->unprefixedTag(),m_version);
		    if (over != ActCount)
			act = over;
		}
	    }
	}
	String id = m_localSid;
	id << "_" << (unsigned int)m_stanzaId++;
	u_int64_t tout = Time::msecNow() + (toutMs ? toutMs : m_engine->stanzaTimeout());
	JGSentStanza* sent = new JGSentStanza(id,tout,stanzaId != 0,ping,act);
	stanza->setAttribute("id",*sent);
	if (stanzaId)
	    *stanzaId = *sent;
	// Insert stanza in timeout ascending order
	ObjList* last = &m_sentStanza;
	for (ObjList* o = last->skipNull(); o; o = o->skipNext()) {
	    JGSentStanza* tmp = static_cast<JGSentStanza*>(o->get());
	    if (tout < tmp->timeout()) {
		o->insert(sent);
		sent = 0;
		break;
	    }
	    last = o;
	}
	if (sent)
	    last->append(sent);
    }
    return m_engine->sendStanza(this,stanza);
}

// Send a ping (empty session info) stanza to the remote peer if it's time to do it
bool JGSession::sendPing(u_int64_t msecNow)
{
    if (!m_timeToPing || m_timeToPing > msecNow)
	return false;
    // Update ping interval
    if (m_engine && m_engine->pingInterval() && msecNow)
	m_timeToPing = msecNow + m_engine->pingInterval();
    else
	m_timeToPing = 0;
    // Send empty info
    return sendStanza(createJingle(ActInfo),0,true,true);
}

// Method called in getEvent() to process a last event decoded from a
// received jingle element
JGEvent* JGSession::processJingleSetEvent(JGEvent*& ev)
{
    if (!ev)
	return 0;
    DDebug(m_engine,DebugInfo,"Call(%s). Processing action (%u,'%s') state=%s [%p]",
	m_sid.c_str(),ev->action(),ev->actionName(),lookupState(state()),this);

    // Check for termination events
    if (ev->final())
	return ev;

    bool error = false;
    bool fatal = false;
    switch (state()) {
	case Active:
	    error = ev->action() == ActAccept || ev->action() == ActInitiate ||
		ev->action() == ActRinging;
	    break;
	case Pending:
	    // Accept session-accept, transport, content and ringing stanzas
	    switch (ev->action()) {
		case ActAccept:
		    if (outgoing()) {
			// XEP-0166 7.2.6: responder may be overridden
			if (ev->jingle()) {
			    JabberID rsp(ev->jingle()->attribute("responder"));
			    if (!rsp.null() && m_remote != rsp) {
				m_remote.set(rsp);
				Debug(m_engine,DebugInfo,
				    "Call(%s). Remote jid changed to '%s' [%p]",
				    m_sid.c_str(),rsp.c_str(),this);
			    }
			}
			changeState(Active);
		    }
		    else
			error = true;
		    break;
		case ActTransportInfo:
		case ActTransportAccept:
		case ActTransportReject:
		case ActTransportReplace:
		case ActContentAccept:
		case ActContentAdd:
		case ActContentModify:
		case ActContentReject:
		case ActContentRemove:
		case ActInfo:
		case ActDescriptionInfo:
		case ActRinging:
		case ActTrying:
		case ActReceived:
		case ActCandidates:
		    break;
		default:
		    error = true;
	    }
	    break;
	case Idle:
	    // Update data. Terminate if not a session initiating event
	    if (ev->action() == ActInitiate) {
//		m_local.set(ev.to());
//		m_remote.set(ev.from());
		changeState(Pending);
	    }
	    else
		error = fatal = true;
	    break;
	default:
	    error = true;
    }
    if (!error) {
	// Don't confirm actions that need session user's interaction
	switch (ev->action()) {
	    case ActInitiate:
	    case ActTransportInfo:
	    case ActTransportAccept:
	    case ActTransportReject:
	    case ActTransportReplace:
	    case ActContentAccept:
	    case ActContentAdd:
	    case ActContentModify:
	    case ActContentReject:
	    case ActContentRemove:
	    case ActTransfer:
	    case ActRinging:
	    case ActHold:
	    case ActActive:
	    case ActMute:
	    case ActTrying:
	    case ActReceived:
	    case ActDescriptionInfo:
	    case ActCandidates:
		break;
	    default:
		ev->confirmElement();
	}
	return ev;
    }
    ev->confirmElement(XMPPError::Request);
    delete ev;
    ev = 0;
    if (fatal)
	ev = new JGEvent(JGEvent::Destroy,this);
    return ev;
}

// Method called in getEvent() to process a jabber event carrying a response
JGEvent* JGSession::processJabberIqResponse(bool result, XmlElement*& xml)
{
    if (!xml)
	return 0;
    JGSentStanza* sent = 0;
    String id(xml->getAttribute("id"));
    if (TelEngine::null(id)) {
	TelEngine::destruct(xml);
	return 0;
    }
    // Find a sent stanza to match the event's id
    for (ObjList* o = m_sentStanza.skipNull(); o; o = o->skipNext()) {
	sent = static_cast<JGSentStanza*>(o->get());
	if (*sent == id)
	    break;
	sent = 0;
    }
    if (!sent) {
	TelEngine::destruct(xml);
	return 0;
    }
    // Check termination conditions
    // Always terminate when receiving responses in Ending state
    bool terminateEnding = (state() == Ending);
    // Terminate pending outgoing if no notification required
    // (Initial session request is sent without notification required)
    bool terminatePending = false;
    if (state() == Pending && outgoing() && !result)
	terminatePending = !sent->notify();
    bool notify = sent->action() == ActInitiate && result && !flag(FlagNoOkInitiate);
    // Generate event
    JGEvent* ev = 0;
    String text;
    String reason;
    if (!result)
	XMPPUtils::decodeError(xml,reason,text);
    if (terminateEnding)
	ev = new JGEvent(JGEvent::Destroy,this,xml,reason,text);
    else if (terminatePending)
	ev = new JGEvent(JGEvent::Terminated,this,xml,reason,text);
    else if (sent->notify() || notify) {
	if (result)
	    ev = new JGEvent(JGEvent::ResultOk,this,xml);
	else
	    ev = new JGEvent(JGEvent::ResultError,this,xml,text);
	ev->setAction(sent->action());
	ev->setConfirmed();
    }
    else {
	// Terminate on ping error
	if (sent->ping() && !result)
	    ev = new JGEvent(JGEvent::Terminated,this,xml,text);
    }
    if (ev)
	xml = 0;
    else
	TelEngine::destruct(xml);

    String error;
#ifdef DEBUG
    if (reason || text) {
	error << " (";
	error << reason;
	error.append(text,reason ? ": " : "");
	error << ")";
    }
#endif
    bool terminate = (ev && ev->final());
    Debug(m_engine,terminatePending ? DebugNote : DebugAll,
	"Call(%s). Sent %selement with id=%s confirmed by %s%s%s [%p]",
	m_sid.c_str(),sent->ping() ? "ping " : "",sent->c_str(),
	result ? "result" : "error",error.safe(),terminate ? ". Terminating": "",this);
    m_sentStanza.remove(sent,true);
    // Gracefully terminate
    if (terminate && state() != Ending)
	hangup();
    return ev;
}

// Decode a file transfer element
JGEvent* JGSession::processFileTransfer(bool set, XmlElement*& xml, XmlElement* child)
{
    if (xml)
	confirmError(xml,XMPPError::FeatureNotImpl);
    return 0;
}

// Event termination notification
void JGSession::eventTerminated(JGEvent* event)
{
    lock();
    if (event == m_lastEvent) {
	DDebug(m_engine,DebugAll,"Call(%s). Event (%p,%u) terminated [%p]",
	    m_sid.c_str(),event,event->type(),this);
	m_lastEvent = 0;
    }
    else if (m_lastEvent)
	Debug(m_engine,DebugNote,
	    "Call(%s). Event (%p,%u) replaced while processed [%p]",
	    m_sid.c_str(),event,event->type(),this);
    unlock();
}

// Change session state
void JGSession::changeState(State newState)
{
    if (m_state == newState)
	return;
    Debug(m_engine,DebugInfo,"Call(%s). Changing state from %s to %s [%p]",
	m_sid.c_str(),lookup(m_state,s_states),lookup(newState,s_states),this);
    m_state = newState;
}

// Get the name of an action
const char* JGSession::lookupAction(int act, Version ver)
{
    switch (ver) {
	case Version1:
	    return lookup(act,s_actions1);
	case Version0:
	    return lookup(act,s_actions0);
	case VersionUnknown:
	    ;
    }
    return 0;
}

// Get the action associated with a given string
JGSession::Action JGSession::lookupAction(const char* str, Version ver)
{
    switch (ver) {
	case Version1:
	    return (Action)lookup(str,s_actions1,ActCount);
	case Version0:
	    return (Action)lookup(str,s_actions0,ActCount);
	case VersionUnknown:
	    ;
    }
    return ActCount;
}


/*
 * JGSession0
 */
// Create an outgoing session
JGSession0::JGSession0(JGEngine* engine, const JabberID& caller, const JabberID& called)
    : JGSession(Version0,engine,caller,called),
    m_candidatesAction(ActCount)
{
}

// Create an incoming session
JGSession0::JGSession0(JGEngine* engine, const JabberID& caller, const JabberID& called,
    XmlElement* xml, const String& id)
    : JGSession(Version0,engine,caller,called,xml,id),
    m_candidatesAction(ActCount)
{
    m_sessContentName = m_localSid + "_content";
}

// Destructor
JGSession0::~JGSession0()
{
}

// Accept a Pending incoming session
bool JGSession0::accept(const ObjList& contents, String* stanzaId)
{
    Lock lock(this);
    if (outgoing() || state() != Pending)
	return false;
    XmlElement* xml = createJingle(ActAccept);
    addJingleContents0(m_sessContentName,xml,contents,true,true,true);
    if (!sendStanza(xml,stanzaId))
	return false;
    changeState(Active);
    return true;
}

// Send a stanza with session content(s)
bool JGSession0::sendContent(Action action, const ObjList& contents, String* stanzaId)
{
    Lock lock(this);
    if (state() != Pending && state() != Active)
	return false;
    bool minimal = false;
    bool addDesc = true;
    bool addTrans = true;
    switch (action) {
	case ActTransportInfo:
	    addDesc = false;
	    break;
	case ActTransportAccept:
	    // Old candidates: don't send it
	    if (m_candidatesAction != ActTransportInfo)
		return true;
	    minimal = true;
	    addDesc = false;
	    addTrans = true;
	    break;
	default:
	    return false;
    };
    // Make sure we dont't terminate the session on failure
    String tmp;
    if (!stanzaId) {
	tmp = "Content" + String(Time::secNow());
	stanzaId = &tmp;
    }
    if (action != ActTransportInfo || m_candidatesAction != ActCount) {
	Action a = (action == ActTransportInfo) ? m_candidatesAction : action;
	XmlElement* xml = createJingle(a);
	addJingleContents0(m_sessContentName,xml,contents,minimal,addDesc,addTrans,
	    m_candidatesAction);
	return sendStanza(xml,stanzaId);
    }
    // Send both transports
    XmlElement* xml = createJingle(ActTransportInfo);
    addJingleContents0(m_sessContentName,xml,contents,minimal,addDesc,addTrans,
	ActTransportInfo);
    bool ok = sendStanza(xml,stanzaId);
    tmp << *stanzaId << "_1";
    xml = createJingle(ActCandidates);
    addJingleContents0(m_sessContentName,xml,contents,minimal,addDesc,addTrans,
	ActCandidates);
    return sendStanza(xml,&tmp) || ok;
}

// Build and send the initial message on an outgoing session
bool JGSession0::initiate(const ObjList& contents, XmlElement* extra, const char* subject)
{
    XmlElement* xml = createJingle(ActInitiate);
    addJingleContents0(m_sessContentName,xml,contents,true,true,true);
    addJingleChild0(xml,extra);
    if (!null(subject))
	addJingleChild0(xml,XMPPUtils::createSubject(subject));
    if (sendStanza(xml)) {
	changeState(Pending);
	return true;
    }
    changeState(Destroy);
    return false;
}

// Decode a valid jingle set event. Set the event's data on success
JGEvent* JGSession0::decodeJingle(XmlElement*& xml, XmlElement* child)
{
    if (!xml)
	return 0;
    if (!child) {
	confirmError(xml,XMPPError::BadRequest);
	return 0;
    }
    Action act = getAction(child);
    if (act == ActCount) {
	confirmError(xml,XMPPError::ServiceUnavailable,"Unknown session action");
	return 0;
    }

    // *** ActTerminate, ActReject
    if (act == ActTerminate || act == ActReject) {
	// Confirm here: this is a final event,
	//  stanza won't be confirmed in getEvent()
	m_recvTerminate = true;
	const char* reason = 0;
	const char* text = 0;
	decodeJingleReason(*xml,reason,text);
	JGEvent* ev = new JGEvent(JGEvent::Terminated,this,xml,reason,text);
	if (!ev->m_reason && act == ActReject)
	    ev->m_reason = lookupReason(ReasonDecline);
	ev->setAction(act);
	ev->confirmElement();
	xml = 0;
	return ev;
    }

    // *** ActContentInfo --> ActDtmf
    if (act == ActContentInfo) {
	// Check dtmf
	// Expect more then 1 'dtmf' child
	XmlElement* tmp = XMPPUtils::findFirstChild(*child,XmlTag::Dtmf);
	String text;
	for (; tmp; tmp = XMPPUtils::findNextChild(*child,tmp,XmlTag::Dtmf)) {
	    String reason = tmp->attribute("action");
	    if (reason == "button-up")
		text << tmp->attribute("code");
	}
	JGEvent* ev = 0;
	if (text) {
	    ev = new JGEvent(ActDtmf,this,xml,0,text);
	    xml = 0;
	}
	else
	    unhandledAction(this,xml,act);
	return ev;
    }

    // *** ActInfo
    if (act == ActInfo) {
	// Return ActInfo event to signal ping (XEP-0166 6.8)
	JGEvent* ev = 0;
	XmlElement* ch = child->findFirstChild();
	if (ch) {
	    int t = XmlTag::Count,n;
	    XMPPUtils::getTag(*child,t,n);
	    switch (t) {
		case XmlTag::Ringing:
		    if (n == XMPPNamespace::JingleRtpInfoOld)
			ev = new JGEvent(ActRinging,this,xml);
		    break;
		case XmlTag::Mute:
		    if (n == XMPPNamespace::JingleRtpInfoOld)
			ev = new JGEvent(ActMute,this,xml);
		    break;
		default: ;
	    }
	}
	else
	    ev = new JGEvent(ActInfo,this,xml);
	if (ev)
	    xml = 0;
	else
	    unhandledAction(this,xml,act,ch);
	return ev;
    }

    if (act == ActTransportAccept) {
	confirmResult(xml);
	TelEngine::destruct(xml);
	return 0;
    }

    // Update candidates action
    if (m_candidatesAction == ActCount &&
	(act == ActCandidates || act == ActTransportInfo)) {
	m_candidatesAction = act;
	Debug(m_engine,DebugAll,"Call(%s). Candidates action set to %s [%p]",
	    m_sid.c_str(),lookupAction(m_candidatesAction,version()),this);
    }
    if (act == ActCandidates)
	act = ActTransportInfo;

    // Get transport
    // Get media description
    // Create event, update transport and media
    JGSessionContent* c = 0;
    JGEvent* event = 0;
    while (true) {
	c = new JGSessionContent(JGSessionContent::RtpIceUdp,m_sessContentName,
	    JGSessionContent::SendBoth,JGSessionContent::CreatorInitiator);
	c->m_rtpRemoteCandidates.m_type = JGRtpCandidates::RtpIceUdp;
	// Build media
	if (act == ActInitiate || act == ActAccept) {
	    XmlElement* media = XMPPUtils::findFirstChild(*child,XmlTag::Description,
		XMPPNamespace::JingleAudio);
	    if (media) {
		c->m_rtpMedia.fromXml(media);
		c->m_rtpMedia.m_media = JGRtpMediaList::Audio;
	    }
	    else {
		Debug(m_engine,DebugInfo,"Call(%s). No media description for action=%s [%p]",
		    m_sid.c_str(),lookupAction(act,version()),this);
		break;
	    }
	}
	// Build transport
	XmlElement* trans = 0;
	if (m_candidatesAction != ActCandidates)
	    trans = XMPPUtils::findFirstChild(*child,XmlTag::Transport,
		XMPPNamespace::JingleTransport);
	else
	    trans = child;
	if (act == ActInitiate && m_candidatesAction == ActCount) {
	    if (trans && trans != child)
		m_candidatesAction = ActTransportInfo;
	    else
		m_candidatesAction = ActCandidates;
	    Debug(m_engine,DebugAll,"Call(%s). Candidates action set to %s [%p]",
		m_sid.c_str(),lookupAction(m_candidatesAction,version()),this);
	}
	XmlElement* t = 0;
	if (trans) {
	    String* ns = trans->xmlns();
	    t = trans->findFirstChild(&XMPPUtils::s_tag[XmlTag::Candidate],ns);
	}
	if (t) {
	    JGRtpCandidate* cd = new JGRtpCandidate(m_localSid + "_transport");
	    cd->m_component = "1";
	    cd->m_generation = t->attribute("generation");
	    cd->m_address = t->attribute("address");
	    cd->m_port = t->attribute("port");
	    cd->m_protocol = t->attribute("protocol");;
	    cd->m_generation = t->attribute("generation");
	    cd->m_type = t->attribute("type");
	    c->m_rtpRemoteCandidates.m_ufrag = t->attribute("username");
	    c->m_rtpRemoteCandidates.m_password = t->attribute("password");
	    c->m_rtpRemoteCandidates.append(cd);
	}
	else if (act == ActTransportInfo) {
	    Debug(m_engine,DebugInfo,"Call(%s). No transport candidates for action=%s [%p]",
		    m_sid.c_str(),lookupAction(act,version()),this);
	    break;
	}
	// Don't set the event's element yet: this would invalidate the 'jingle' variable
	event = new JGEvent(act,this,xml);
	event->m_contents.append(c);
	xml = 0;
	break;
    }
    if (event)
	return event;
    TelEngine::destruct(c);
    confirmError(xml,XMPPError::ServiceUnavailable);
    return 0;
}

// Create an 'iq' stanza with a 'jingle' child
XmlElement* JGSession0::createJingle(Action action, XmlElement* element1,
    XmlElement* element2, XmlElement* element3)
{
    XmlElement* iq = XMPPUtils::createIq(XMPPUtils::IqSet,m_local,m_remote,0);
    XmlElement* jingle = XMPPUtils::createElement(XmlTag::Session,
	XMPPNamespace::JingleSession);
    if (action < ActCount) {
	const char* s = lookupAction(action,version());
	jingle->setAttribute("type",s);
	jingle->setAttribute("action",s);
    }
    jingle->setAttribute("initiator",outgoing() ? m_local : m_remote);
    jingle->setAttribute("responder",outgoing() ? m_remote : m_local);
    jingle->setAttribute("id",m_sid);
    jingle->addChild(element1);
    jingle->addChild(element2);
    jingle->addChild(element3);
    iq->addChild(jingle);
    return iq;
}

// Create a dtmf XML element
XmlElement* JGSession0::createDtmf(const char* dtmf, unsigned int msDuration)
{
    XmlElement* xml = XMPPUtils::createElement(XmlTag::Dtmf,XMPPNamespace::DtmfOld);
    xml->setAttribute("action","button-up");
    xml->setAttribute("code",dtmf);
    return xml;
}


/**
 * JGSession1
 */

// Create an outgoing session
JGSession1::JGSession1(JGEngine* engine, const JabberID& caller, const JabberID& called)
    : JGSession(Version1,engine,caller,called)
{
}

// Create an incoming session
JGSession1::JGSession1(JGEngine* engine, const JabberID& caller, const JabberID& called,
    XmlElement* xml, const String& id)
    : JGSession(Version1,engine,caller,called,xml,id)
{
}

// Destructor
JGSession1::~JGSession1()
{
}

// Build and send the initial message on an outgoing session
bool JGSession1::initiate(const ObjList& contents, XmlElement* extra, const char* subject)
{
    XmlElement* xml = createJingle(ActInitiate);
    addJingleContents(xml,contents,false,true,true,true);
    addJingleChild(xml,extra);
    if (!null(subject))
	addJingleChild(xml,XMPPUtils::createSubject(subject));
    if (sendStanza(xml)) {
	changeState(Pending);
	return true;
    }
    changeState(Destroy);
    return false;
}

// Accept a Pending incoming session
bool JGSession1::accept(const ObjList& contents, String* stanzaId)
{
    Lock lock(this);
    if (outgoing() || state() != Pending)
	return false;
    XmlElement* xml = createJingle(ActAccept);
    addJingleContents(xml,contents,false,true,true,true,true);
    if (!sendStanza(xml,stanzaId))
	return false;
    changeState(Active);
    return true;
}

// Create a RTP info child to be added to a session-info element
XmlElement* JGSession1::createRtpInfoXml(RtpInfo info)
{
    const char* tag = lookup(info,s_rtpInfo);
    if (!TelEngine::null(tag)) {
	if (info != RtpRinging || !flag(FlagRingNsRtp))
	    return XMPPUtils::createElement(tag,XMPPNamespace::JingleAppsRtpInfo);
	return XMPPUtils::createElement(tag,XMPPNamespace::JingleAppsRtp);
    }
    return 0;
}

// Create a termination reason element
XmlElement* JGSession1::createReason(int reason, const char* text, XmlElement* child)
{
    const char* res = lookup(reason,s_reasons);
    if (TelEngine::null(res)) {
	TelEngine::destruct(child);
	return 0;
    }
    XmlElement* r = XMPPUtils::createElement(XmlTag::Reason);
    r->addChild(new XmlElement(res));
    if (!TelEngine::null(text))
	r->addChild(XMPPUtils::createElement(XmlTag::Text,text));
    if (child)
	r->addChild(child);
    return r;
}

// Create a transfer reason element
XmlElement* JGSession1::createTransferReason(int reason)
{
    const char* res = lookup(reason,s_reasons);
    if (!TelEngine::null(res))
	return XMPPUtils::createElement(res,XMPPNamespace::JingleTransfer);
    return 0;
}

XmlElement* JGSession1::createRtpSessionReason(int reason)
{
    const char* res = lookup(reason,s_reasons);
    if (!TelEngine::null(res))
	return XMPPUtils::createElement(res,XMPPNamespace::JingleAppsRtpError);
    return 0;
}

// Send a stanza with session content(s)
bool JGSession1::sendContent(Action action, const ObjList& contents, String* stanzaId)
{
    Lock lock(this);
    if (state() != Pending && state() != Active)
	return false;
    // XEP-0176 5.2: add ICE auth only for content-add, transport-replace, transport-info
    bool addIceAuth = false;
    bool addCandidates = false;
    bool minimal = false;
    bool addDesc = true;
    bool addTrans = true;
    switch (action) {
	case ActContentAdd:
	    addCandidates = true;
	    addIceAuth = true;
	    break;
	case ActTransportInfo:
	    addCandidates = true;
	    addIceAuth = true;
	    addDesc = false;
	    break;
	case ActTransportReplace:
	    addIceAuth = true;
	    break;
	case ActTransportAccept:
	case ActTransportReject:
	case ActContentAccept:
	case ActContentModify:
	    break;
	case ActContentReject:
	case ActContentRemove:
	    minimal = true;
	    addDesc = false;
	    addTrans = false;
	    break;
	default:
	    return false;
    };
    // Make sure we dont't terminate the session on failure
    String tmp;
    if (!stanzaId) {
	tmp = "Content" + String(Time::secNow());
	stanzaId = &tmp;
    }
    XmlElement* xml = createJingle(action);
    addJingleContents(xml,contents,minimal,addDesc,addTrans,addCandidates,addIceAuth);
    return sendStanza(xml,stanzaId);
}

// Send a stanza with stream hosts
bool JGSession1::sendStreamHosts(const ObjList& hosts, String* stanzaId)
{
    Lock lock(this);
    if (state() != Pending)
	return false;
    XmlElement* xml = XMPPUtils::createIq(XMPPUtils::IqSet,m_local,m_remote,0);
    xml->addChild(JGStreamHost::buildHosts(hosts,m_sid));
    return sendStanza(xml,stanzaId,true,false,(unsigned int)m_engine->streamHostTimeout());
}

// Send a stanza with a stream host used
bool JGSession1::sendStreamHostUsed(const char* jid, const char* stanzaId)
{
    Lock lock(this);
    if (state() != Pending)
	return false;
    bool ok = !null(jid);
    XmlElement* xml = XMPPUtils::createIq(ok ? XMPPUtils::IqResult : XMPPUtils::IqError,
	m_local,m_remote,stanzaId);
    if (ok)
	xml->addChild(JGStreamHost::buildRsp(jid));
    else
	xml->addChild(XMPPUtils::createError(XMPPError::TypeModify,
	    XMPPError::ItemNotFound));
    return sendStanza(xml,0,false);
}

// Decode a jingle stanza
JGEvent* JGSession1::decodeJingle(XmlElement*& xml, XmlElement* child)
{
    if (!child) {
	confirmError(xml,XMPPError::BadRequest);
	return 0;
    }

    Action act = getAction(child);
    if (act == ActCount) {
	confirmError(xml,XMPPError::ServiceUnavailable,"Unknown session action");
	return 0;
    }

    // *** ActTerminate
    if (act == ActTerminate) {
	// Confirm here: this is a final event,
	//  stanza won't be confirmed in getEvent()
	m_recvTerminate = true;
	const char* reason = 0;
	const char* text = 0;
	decodeJingleReason(*xml,reason,text);
	JGEvent* ev = new JGEvent(JGEvent::Terminated,this,xml,reason,text);
	ev->setAction(act);
	ev->confirmElement();
	xml = 0;
	return ev;
    }

    // *** ActInfo
    if (act == ActInfo) {
        // Check info element
	// Return ActInfo event to signal ping (XEP-0166 6.8)
	XmlElement* ch = child->findFirstChild();
	if (!ch) {
	    JGEvent* ev = new JGEvent(ActInfo,this,xml);
	    xml = 0;
	    return ev;
	}
	JGEvent* ev = 0;
	// Check namespace and build event
	switch (XMPPUtils::tag(*ch)) {
	    case XmlTag::Dtmf:
		if (XMPPUtils::hasXmlns(*ch,XMPPNamespace::JingleDtmf)) {
		    String text;
		    const char* reason = 0;
		    // Expect more then 1 'dtmf' child
		    for (; ch; ch = XMPPUtils::findNextChild(*child,ch,XmlTag::Dtmf)) {
			if (XMPPUtils::hasXmlns(*ch,XMPPNamespace::JingleDtmf))
			    text << ch->attribute("code");
			else
			    break;
		    }
		    if (ch)
			reason = "Bad dtmf namespace";
		    else if (!text)
			reason = "Empty dtmf(s)";
		    if (reason) {
			confirmError(xml,XMPPError::BadRequest,reason);
			xml = 0;
			return 0;
		    }
		    ev = new JGEvent(ActDtmf,this,xml,0,text);
		}
		break;
	    case XmlTag::Transfer:
		if (XMPPUtils::hasXmlns(*ch,XMPPNamespace::JingleTransfer))
		    ev = new JGEvent(ActTransfer,this,xml);
		break;
	    case XmlTag::Hold:
		if (XMPPUtils::hasXmlns(*ch,XMPPNamespace::JingleAppsRtpInfo))
		    ev = new JGEvent(ActHold,this,xml);
		break;
	    case XmlTag::Active:
		if (XMPPUtils::hasXmlns(*ch,XMPPNamespace::JingleAppsRtpInfo))
		    ev = new JGEvent(ActActive,this,xml);
		break;
	    case XmlTag::Ringing:
		if (XMPPUtils::hasXmlns(*ch,XMPPNamespace::JingleAppsRtpInfo))
		    ev = new JGEvent(ActRinging,this,xml);
		break;
	    case XmlTag::Trying:
		if (XMPPUtils::hasXmlns(*ch,XMPPNamespace::JingleTransportRawUdpInfo))
		    ev = new JGEvent(ActTrying,this,xml);
		break;
	    case XmlTag::Received:
		if (XMPPUtils::hasXmlns(*ch,XMPPNamespace::JingleTransportRawUdpInfo))
		    ev = new JGEvent(ActReceived,this,xml);
		break;
	    case XmlTag::Mute:
		if (XMPPUtils::hasXmlns(*ch,XMPPNamespace::JingleAppsRtpInfo))
		    ev = new JGEvent(ActMute,this,xml);
		break;
	    default: ;
	}
	if (ev)
	    xml = 0;
	else
	    confirmError(xml,XMPPError::FeatureNotImpl);
	return ev;
    }

    // *** Elements carrying contents
    switch (act) {
	case ActTransportInfo:
	case ActTransportAccept:
	case ActTransportReject:
	case ActTransportReplace:
	case ActContentAccept:
	case ActContentAdd:
	case ActContentModify:
	case ActContentReject:
	case ActContentRemove:
	case ActInitiate:
	case ActAccept:
	case ActDescriptionInfo:
	    break;
	default:
	    confirmError(xml,XMPPError::ServiceUnavailable);
	    return 0;
    }

    JGEvent* event = new JGEvent(act,this,xml);
    XMPPError::Type err = XMPPError::NoError;
    String text;
    XmlElement* c = XMPPUtils::findFirstChild(*child,XmlTag::Content);
    for (; c; c = XMPPUtils::findNextChild(*child,c,XmlTag::Content)) {
	JGSessionContent* content = JGSessionContent::fromXml(c,err,text);
	if (content) {
	    DDebug(m_engine,DebugAll,
		"Call(%s). Found content='%s' in '%s' stanza [%p]",
		m_sid.c_str(),content->toString().c_str(),event->actionName(),this);
	    event->m_contents.append(content);
	    continue;
	}
	if (err == XMPPError::NoError) {
	    Debug(m_engine,DebugInfo,
		"Call(%s). Ignoring content='%s' in '%s' stanza [%p]",
		m_sid.c_str(),c->attribute("name"),event->actionName(),this);
	    continue;
	}
	break;
    }
    xml = 0;
    if (!c)
	return event;
    TelEngine::destruct(c);
    event->confirmElement(err,text);
    delete event;
    return 0;
}

// Create an 'iq' stanza with a 'jingle' child
XmlElement* JGSession1::createJingle(Action action, XmlElement* element1,
    XmlElement* element2, XmlElement* element3)
{
    XmlElement* iq = XMPPUtils::createIq(XMPPUtils::IqSet,m_local,m_remote,0);
    XmlElement* jingle = XMPPUtils::createElement(XmlTag::Jingle,
	XMPPNamespace::Jingle);
    if (action < ActCount) {
	const char* s = lookupAction(action,version());
	jingle->setAttribute("action",s);
	jingle->setAttribute("type",s);
    }
    jingle->setAttribute("initiator",outgoing() ? m_local : m_remote);
    jingle->setAttribute("responder",outgoing() ? m_remote : m_local);
    jingle->setAttribute("sid",m_sid);
    jingle->addChild(element1);
    jingle->addChild(element2);
    jingle->addChild(element3);
    iq->addChild(jingle);
    return iq;
}

// Create a dtmf XML element
XmlElement* JGSession1::createDtmf(const char* dtmf, unsigned int msDuration)
{
    XmlElement* xml = XMPPUtils::createElement(XmlTag::Dtmf,XMPPNamespace::JingleDtmf);
    xml->setAttribute("code",dtmf);
    if (msDuration > 0)
	xml->setAttribute("duration",String(msDuration));
    return xml;
}

// Decode a file transfer element
JGEvent* JGSession1::processFileTransfer(bool set, XmlElement*& xml, XmlElement* child)
{
    JGEvent* ev = 0;
    if (xml && child && XMPPUtils::isTag(*child,XmlTag::Query,XMPPNamespace::ByteStreams)) {
	ev = new JGEvent(ActStreamHost,this,xml);
	XmlElement* sh = XMPPUtils::findFirstChild(*child,XmlTag::StreamHost,XMPPNamespace::ByteStreams);
	for (; sh; sh = XMPPUtils::findNextChild(*child,sh,XmlTag::StreamHost,XMPPNamespace::ByteStreams)) {
	    JGStreamHost* s = JGStreamHost::fromXml(sh);
	    if (s)
		ev->m_streamHosts.append(s);
	}
	xml = 0;
    }
    else {
	confirmError(xml,XMPPError::FeatureNotImpl);
	TelEngine::destruct(xml);
    }
    return ev;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
