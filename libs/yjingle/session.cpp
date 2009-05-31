/**
 * session.cpp
 * Yet Another Jingle Stack
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

#include <yatejingle.h>

using namespace TelEngine;

// Utility: add session content(s) to an already created stanza's jingle child
static void addJingleContents(XMLElement* xml, const ObjList& contents, bool minimum,
    bool addDesc, bool addTrans, bool addCandidates, bool addAuth = true)
{
    if (!xml)
	return;
    XMLElement* jingle = xml->findFirstChild(XMLElement::Jingle);
    if (!jingle)
	return;
    for (ObjList* o = contents.skipNull(); o; o = o->skipNext()) {
	JGSessionContent* c = static_cast<JGSessionContent*>(o->get());
	jingle->addChild(c->toXml(minimum,addDesc,addTrans,addCandidates,addAuth));
    }
    TelEngine::destruct(jingle);
}

// Utility: add session content(s) to an already created stanza's jingle child
// This method is used by the version 0 of the session
static void addJingleContents0(String& name, XMLElement* xml, const ObjList& contents, bool minimal,
    bool addDesc, bool addTrans)
{
    if (!xml)
	return;
    XMLElement* jingle = xml->findFirstChild(XMLElement::Session);
    if (!jingle)
	return;
    for (ObjList* o = contents.skipNull(); o; o = o->skipNext()) {
	JGSessionContent* c = static_cast<JGSessionContent*>(o->get());
	if (c->type() != JGSessionContent::RtpIceUdp)
	    continue;
	name = c->toString();
	if (addDesc) {
	    XMLElement* desc = XMPPUtils::createElement(XMLElement::Description,
		XMPPNamespace::JingleAudio);
	    for (ObjList* o = c->m_rtpMedia.skipNull(); o; o = o->skipNext()) {
		JGRtpMedia* a = static_cast<JGRtpMedia*>(o->get());
		desc->addChild(a->toXML());
	    }
	   JGRtpMedia* te = new JGRtpMedia("106","telephone-event","8000","","");
	   desc->addChild(te->toXML());
	   TelEngine::destruct(te);
	   jingle->addChild(desc);
	}
	if (addTrans) {
	    XMLElement* trans = XMPPUtils::createElement(XMLElement::Transport,
		XMPPNamespace::JingleTransport);
	    if (!minimal) {
		for (ObjList* o = c->m_rtpLocalCandidates.skipNull(); o; o = o->skipNext()) {
		    JGRtpCandidate* rc = static_cast<JGRtpCandidate*>(o->get());
		    XMLElement* xml = new XMLElement(XMLElement::Candidate);
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
		    trans->addChild(xml);
		}
	    }
	    jingle->addChild(trans);
	}
    }
    TelEngine::destruct(jingle);
}

// Utility: add xml element child to an already created stanza's jingle child
static void addJingleChild(XMLElement* xml, XMLElement* child)
{
    if (!(xml && child))
	return;
    XMLElement* jingle = xml->findFirstChild(XMLElement::Jingle);
    if (jingle)
	jingle->addChild(child);
    else
	TelEngine::destruct(child);
    TelEngine::destruct(jingle);
}

// Utility: add xml element child to an already created stanza's jingle child
static void addJingleChild0(XMLElement* xml, XMLElement* child)
{
    if (!(xml && child))
	return;
    XMLElement* jingle = xml->findFirstChild(XMLElement::Session);
    if (jingle)
	jingle->addChild(child);
    else
	TelEngine::destruct(child);
    TelEngine::destruct(jingle);
}

// Utility: add NamedList param only if not empty
static inline void addParamValid(NamedList& list, const char* param, const char* value)
{
    if (null(param) || null(value))
	return;
    list.addParam(param,value);
}


/**
 * JGRtpMedia
 */
XMLElement* JGRtpMedia::toXML() const
{
    XMLElement* p = new XMLElement(XMLElement::PayloadType);
    p->setAttribute("id",m_id);
    p->setAttributeValid("name",m_name);
    p->setAttributeValid("clockrate",m_clockrate);
    p->setAttributeValid("channels",m_channels);
    unsigned int n = m_params.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* s = m_params.getParam(i);
	if (!s)
	    continue;
        XMLElement* param = new XMLElement(XMLElement::Parameter);
	param->setAttributeValid("name",s->name());
	param->setAttributeValid("value",*s);
	p->addChild(param);
    }
    return p;
}

void JGRtpMedia::fromXML(XMLElement* xml)
{
    if (!xml) {
	set("","","","","");
	return;
    }
    set(xml->getAttribute("id"),xml->getAttribute("name"),
	xml->getAttribute("clockrate"),xml->getAttribute("channels"),"");
    XMLElement* param = xml->findFirstChild(XMLElement::Parameter);
    for (; param; param = xml->findNextChild(param,XMLElement::Parameter))
	m_params.addParam(param->getAttribute("name"),param->getAttribute("value"));
}


/**
 * JGCrypto
 */

XMLElement* JGCrypto::toXML() const
{
    XMLElement* xml = new XMLElement(XMLElement::Crypto);
    xml->setAttributeValid("crypto-suite",m_suite);
    xml->setAttributeValid("key-params",m_keyParams);
    xml->setAttributeValid("session-params",m_sessionParams);
    xml->setAttributeValid("tag",toString());
    return xml;
}

void JGCrypto::fromXML(const XMLElement* xml)
{
    if (!xml)
	return;
    m_suite = xml->getAttribute("crypto-suite");
    m_keyParams = xml->getAttribute("key-params");
    m_sessionParams = xml->getAttribute("session-params");
    assign(xml->getAttribute("tag"));
}


/**
 * JGRtpMediaList
 */

TokenDict JGRtpMediaList::s_media[] = {
    {"audio",     Audio},
    {0,0}
};

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
XMLElement* JGRtpMediaList::toXML(bool telEvent) const
{
    if (m_media != Audio)
	return 0;
    XMLElement* desc = XMPPUtils::createElement(XMLElement::Description,
       XMPPNamespace::JingleAppsRtp);
    desc->setAttributeValid("media",lookup(m_media,s_media));
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
       JGRtpMedia* a = static_cast<JGRtpMedia*>(o->get());
       desc->addChild(a->toXML());
    }
    if (telEvent) {
       JGRtpMedia* te = new JGRtpMedia("106","telephone-event","8000","","");
       desc->addChild(te->toXML());
       TelEngine::destruct(te);
    }
    ObjList* c = m_cryptoLocal.skipNull();
    if (c) {
       if (m_cryptoMandatory)
           desc->addChild(new XMLElement(XMLElement::CryptoRequired));
       for (; c; c = c->skipNext())
           desc->addChild((static_cast<JGCrypto*>(c->get()))->toXML());
    }
    return desc;
}

// Fill this list from an XML element's children. Clear before attempting to fill
void JGRtpMediaList::fromXML(XMLElement* xml)
{
    clear();
    m_cryptoMandatory = false;
    m_cryptoRemote.clear();
    if (!xml)
	return;
    m_media = (Media)lookup(xml->getAttribute("media"),s_media,MediaUnknown);
    XMLElement* m = xml->findFirstChild(XMLElement::PayloadType);
    for (; m; m = xml->findNextChild(m,XMLElement::PayloadType))
	ObjList::append(new JGRtpMedia(m));
    // Check crypto
    XMLElement* c = xml->findFirstChild(XMLElement::Crypto);
    if (c) {
	XMLElement* mandatory = xml->findFirstChild(XMLElement::CryptoRequired);
	if (mandatory) {
	    m_cryptoMandatory = true;
	    TelEngine::destruct(mandatory);
	}
	for (; c; c = xml->findNextChild(c,XMLElement::Crypto))
	    m_cryptoRemote.append(new JGCrypto(c));
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


/**
 * JGRtpCandidate
 */

// Create a 'candidate' element from this object
XMLElement* JGRtpCandidate::toXml(const JGRtpCandidates& container) const
{
    if (container.m_type == JGRtpCandidates::Unknown)
	return 0;

    XMLElement* xml = new XMLElement(XMLElement::Candidate);

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
void JGRtpCandidate::fromXml(XMLElement* xml, const JGRtpCandidates& container)
{
    if (!xml || container.m_type == JGRtpCandidates::Unknown)
	return;

    if (container.m_type == JGRtpCandidates::RtpIceUdp)
	assign(xml->getAttribute("foundation"));
    else if (container.m_type == JGRtpCandidates::RtpRawUdp)
	assign(xml->getAttribute("id"));

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


/**
 * JGRtpCandidates
 */

TokenDict JGRtpCandidates::s_type[] = {
    {"ice-udp", RtpIceUdp},
    {"raw-udp", RtpRawUdp},
    {0,0},
};

// Create a 'transport' element from this object. Add 
XMLElement* JGRtpCandidates::toXML(bool addCandidates, bool addAuth) const
{
    XMPPNamespace::Type ns;
    if (m_type == RtpIceUdp)
	ns = XMPPNamespace::JingleTransportIceUdp;
    else if (m_type == RtpRawUdp)
	ns = XMPPNamespace::JingleTransportRawUdp;
    else
	return 0;
    XMLElement* trans = XMPPUtils::createElement(XMLElement::Transport,ns);
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
void JGRtpCandidates::fromXML(XMLElement* element)
{
    clear();
    m_type = Unknown;
    m_password = "";
    m_ufrag = "";
    if (!element)
	return;
    // Set transport data
    if (XMPPUtils::hasXmlns(*element,XMPPNamespace::JingleTransportIceUdp))
	m_type = RtpIceUdp;
    else if (XMPPUtils::hasXmlns(*element,XMPPNamespace::JingleTransportRawUdp))
	m_type = RtpRawUdp;
    else
	return;
    m_password = element->getAttribute("pwd");
    m_ufrag = element->getAttribute("ufrag");
    // Get candidates
    XMLElement* c = element->findFirstChild(XMLElement::Candidate);
    for (; c; c = element->findNextChild(c,XMLElement::Candidate))
	append(new JGRtpCandidate(c,*this));
}

// Find a candidate by its component value
JGRtpCandidate* JGRtpCandidates::findByComponent(unsigned int component)
{
    String tmp = component;
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	JGRtpCandidate* c = static_cast<JGRtpCandidate*>(o->get());
	if (c->m_component == tmp)
	    return c;
    }
    return 0;
}

// Generate a random password or username to be used with ICE-UDP transport
// Maximum number of characters. The maxmimum value is 256.
// The minimum value is 22 for password and 4 for username
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
 	dest << (int)random();
    dest = dest.substr(0,max);
}

// Generate a random password or username to be used with old ICE-UDP transport
void JGRtpCandidates::generateOldIceToken(String& dest)
{
    dest = "";
    while (dest.length() < 16)
 	dest << (int)random();
    dest = dest.substr(0,16);
}


/**
 * JGSessionContent
 */

// The list containing the text values for Senders enumeration
TokenDict JGSessionContent::s_senders[] = {
    {"both",       SendBoth},
    {"initiator",  SendInitiator},
    {"responder",  SendResponder},
    {0,0}
};

// The list containing the text values for Creator enumeration
TokenDict JGSessionContent::s_creator[] = {
    {"initiator",  CreatorInitiator},
    {"responder",  CreatorResponder},
    {0,0}
};

// Constructor
JGSessionContent::JGSessionContent(Type t, const char* name, Senders senders,
    Creator creator, const char* disposition)
    : m_fileTransfer(""),
    m_type(t), m_name(name), m_senders(senders), m_creator(creator),
    m_disposition(disposition)
{
}

// Build a 'content' XML element from this object
XMLElement* JGSessionContent::toXml(bool minimum, bool addDesc,
    bool addTrans, bool addCandidates, bool addAuth) const
{
    XMLElement* xml = new XMLElement(XMLElement::Content);
    xml->setAttributeValid("name",m_name);
    xml->setAttributeValid("creator",lookup(m_creator,s_creator));
    if (!minimum) {
	xml->setAttributeValid("senders",lookup(m_senders,s_senders));
	xml->setAttributeValid("disposition",m_disposition);
    }
    // Add description and transport
    XMLElement* desc = 0;
    XMLElement* trans = 0;
    if (m_type == RtpIceUdp || m_type == RtpRawUdp) {
	// Audio content
	if (addDesc)
	    desc = m_rtpMedia.toXML();
	if (addTrans)
	    trans = m_rtpLocalCandidates.toXML(addCandidates,addAuth);
    }
    else if (m_type == FileBSBOffer || m_type == FileBSBRequest) {
	// File transfer content
	XMLElement* file = XMPPUtils::createElement(XMLElement::File,
	    XMPPNamespace::SIProfileFileTransfer);
	unsigned int n = m_fileTransfer.length();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* ns = m_fileTransfer.getParam(i);
	    if (ns)
		file->setAttributeValid(ns->name(),*ns);
	}
	XMLElement* child = 0;
	if (m_type == FileBSBOffer)
	    child = new XMLElement(XMLElement::Offer);
	else
	    child = new XMLElement(XMLElement::Request);
	child->addChild(file);
	desc = XMPPUtils::createElement(XMLElement::Description,
	    XMPPNamespace::JingleAppsFileTransfer);
	desc->addChild(child);
	trans = XMPPUtils::createElement(XMLElement::Transport,
	    XMPPNamespace::JingleTransportByteStreams);
    }
    xml->addChild(desc);
    xml->addChild(trans);
    return xml;
}

// Build a content object from an XML element
JGSessionContent* JGSessionContent::fromXml(XMLElement* xml, XMPPError::Type& err,
	String& error)
{
    static const char* errAttr = "Required attribute is missing: ";
    static const char* errAttrValue = "Invalid attribute value: ";

    if (!xml) {
	err = XMPPError::SInternal;
	return 0;
    }

    err = XMPPError::SNotAcceptable;

    const char* name = xml->getAttribute("name");
    if (!(name && *name)) {
	error << errAttr << "name";
	return 0;
    }
    // Creator (default: initiator)
    Creator creator = CreatorInitiator;
    const char* tmp = xml->getAttribute("creator");
    if (tmp)
	creator = (Creator)lookup(tmp,s_creator,CreatorUnknown);
    if (creator == CreatorUnknown) {
	error << errAttrValue << "creator";
	return 0;
    }
    // Senders (default: both)
    Senders senders = SendBoth;
    tmp = xml->getAttribute("senders");
    if (tmp)
	senders = (Senders)lookup(tmp,s_senders,SendUnknown);
    if (senders == SendUnknown) {
	error << errAttrValue << "senders";
	return 0;
    }

    JGSessionContent* content = new JGSessionContent(Unknown,name,senders,creator,
	xml->getAttribute("disposition"));
    XMLElement* desc = 0;
    XMLElement* trans = 0;
    err = XMPPError::NoError;
    // Use a while() to go to end and cleanup data
    while (true) {
	int offer = -1;
	// Check description
	desc = xml->findFirstChild(XMLElement::Description);
	if (desc) {
	    if (XMPPUtils::hasXmlns(*desc,XMPPNamespace::JingleAppsRtp)) {
		content->m_rtpMedia.fromXML(desc);
	    }
	    else if (XMPPUtils::hasXmlns(*desc,XMPPNamespace::JingleAppsFileTransfer)) {
		content->m_type = UnknownFileTransfer;
		// Get file and type
		XMLElement* dir = desc->findFirstChild(XMLElement::Offer);
		if (dir)
		    offer = 1;
		else {
		    dir = desc->findFirstChild(XMLElement::Request);
		    if (dir)
			offer = 0;
		}
		if (dir) {
		    XMLElement* file = dir->findFirstChild(XMLElement::File);
		    if (file && XMPPUtils::hasXmlns(*file,XMPPNamespace::SIProfileFileTransfer)) {
			addParamValid(content->m_fileTransfer,"name",file->getAttribute("name"));
			addParamValid(content->m_fileTransfer,"size",file->getAttribute("size"));
			addParamValid(content->m_fileTransfer,"hash",file->getAttribute("hash"));
			addParamValid(content->m_fileTransfer,"date",file->getAttribute("date"));
		    }
		    else
			offer = -1;
		    TelEngine::destruct(file);
		    TelEngine::destruct(dir);
		}
	    }
	    else
		content->m_rtpMedia.m_media = JGRtpMediaList::MediaUnknown;
	}
	else
	    content->m_rtpMedia.m_media = JGRtpMediaList::MediaMissing;

	// Check transport
	trans = xml->findFirstChild(XMLElement::Transport);
	if (trans) {
	    if (content->type() != UnknownFileTransfer) {
		content->m_rtpRemoteCandidates.fromXML(trans);
		if (content->m_rtpRemoteCandidates.m_type == JGRtpCandidates::RtpIceUdp)
		    content->m_type = RtpIceUdp;
		else if (content->m_rtpRemoteCandidates.m_type == JGRtpCandidates::RtpRawUdp)
		    content->m_type = RtpRawUdp;
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

    TelEngine::destruct(desc);
    TelEngine::destruct(trans);
    if (err == XMPPError::NoError)
	return content;
    TelEngine::destruct(content);
    return 0;
}


/**
 * JGStreamHost
 */

// Build an XML element from this stream host
XMLElement* JGStreamHost::toXml()
{
    if (!length())
	return 0;
    XMLElement* xml = new XMLElement(XMLElement::StreamHost);
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
JGStreamHost* JGStreamHost::fromXml(XMLElement* xml)
{
    if (!xml)
	return 0;
    const char* jid = xml->getAttribute("jid");
    if (TelEngine::null(jid))
	return 0;
    return new JGStreamHost(jid,xml->getAttribute("host"),
	String(xml->getAttribute("port")).toInteger(-1),xml->getAttribute("zeroconf"));
}

// Build a query XML element carrying a list of stream hosts
XMLElement* JGStreamHost::buildHosts(const ObjList& hosts, const char* sid,
    const char* mode)
{
    XMLElement* xml = XMPPUtils::createElement(XMLElement::Query,
	XMPPNamespace::ByteStreams);
    xml->setAttribute("sid",sid);
    xml->setAttribute("mode",mode);
    for (ObjList* o = hosts.skipNull(); o; o = o->skipNext())
	xml->addChild((static_cast<JGStreamHost*>(o->get()))->toXml());
    return xml;
}

// Build a query XML element with a streamhost-used child
XMLElement* JGStreamHost::buildRsp(const char* jid)
{
    XMLElement* xml = XMPPUtils::createElement(XMLElement::Query,
	XMPPNamespace::ByteStreams);
    XMLElement* used = new XMLElement(XMLElement::StreamHostUsed);
    used->setAttribute("jid",jid);
    xml->addChild(used);
    return xml;
}


/**
 * JGSession
 */

TokenDict JGSession::s_versions[] = {
    {"0",  Version0},
    {"1",  Version1},
    {0,0}
};

TokenDict JGSession::s_states[] = {
    {"Idle",     Idle},
    {"Pending",  Pending},
    {"Active",   Active},
    {"Ending",   Ending},
    {"Destroy",  Destroy},
    {0,0}
};

TokenDict JGSession::s_reasons[] = {
    {"busy",                     ReasonBusy},
    {"decline",                  ReasonDecline},
    {"connectivity-error",       ReasonConn},
    {"media-error",              ReasonMedia},
    {"unsupported-transports",   ReasonTransport},
    {"no-error",                 ReasonNoError},
    {"success",                  ReasonOk},
    {"unsupported-applications", ReasonNoApp},
    {"alternative-session",      ReasonAltSess},
    {"general-error",            ReasonUnknown},
    {"transferred",              ReasonTransfer},
    {0,0}
};

TokenDict JGSession::s_actions0[] = {
    {"accept",                ActAccept},
    {"initiate",              ActInitiate},
    {"terminate",             ActTerminate},
    {"info",                  ActInfo},
    {"transport-info",        ActTransportInfo},
    {"transport-accept",      ActTransportAccept},
    {"content-info",          ActContentInfo},
    {"DTMF",                  ActDtmf},
    {"ringing",               ActRinging},
    {"mute",                  ActMute},
    {0,0}
};

TokenDict JGSession::s_actions1[] = {
    {"session-accept",        ActAccept},
    {"session-initiate",      ActInitiate},
    {"session-terminate",     ActTerminate},
    {"session-info",          ActInfo},
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

// Create an outgoing session
JGSession::JGSession(Version ver, JGEngine* engine, JBStream* stream,
	const String& callerJID, const String& calledJID, const char* msg)
    : Mutex(true,"JGSession"),
    m_version(ver),
    m_state(Idle),
    m_timeToPing(0),
    m_engine(engine),
    m_stream(0),
    m_outgoing(true),
    m_localJID(callerJID),
    m_remoteJID(calledJID),
    m_lastEvent(0),
    m_recvTerminate(false),
    m_private(0),
    m_stanzaId(1)
{
    if (stream && stream->ref())
	m_stream = stream;
    // Make sure we don't ping before session-initiate times out
    if (m_engine && m_engine->pingInterval())
	m_timeToPing = Time::msecNow() + m_engine->stanzaTimeout() + m_engine->pingInterval();
    m_engine->createSessionId(m_localSid);
    m_sid = m_localSid;
    Debug(m_engine,DebugAll,"Call(%s). Outgoing msg=%s [%p]",m_sid.c_str(),msg,this);
    if (msg)
	sendMessage(msg);
}

// Create an incoming session
JGSession::JGSession(Version ver, JGEngine* engine, JBEvent* event, const String& id)
    : Mutex(true,"JGSession"),
    m_version(ver),
    m_state(Idle),
    m_timeToPing(0),
    m_engine(engine),
    m_stream(0),
    m_outgoing(false),
    m_sid(id),
    m_lastEvent(0),
    m_recvTerminate(false),
    m_private(0),
    m_stanzaId(1)
{
    if (event->stream() && event->stream()->ref())
	m_stream = event->stream();
    if (m_engine && m_engine->pingInterval())
	m_timeToPing = Time::msecNow() + m_engine->pingInterval();
    m_events.append(event);
    m_engine->createSessionId(m_localSid);
    Debug(m_engine,DebugAll,"Call(%s). Incoming [%p]",m_sid.c_str(),this);
}

// Destructor: hangup, cleanup, remove from engine's list
JGSession::~JGSession()
{
    XDebug(m_engine,DebugAll,"JGSession::~JGSession() [%p]",this);
}

// Ask this session to accept an event
bool JGSession::acceptEvent(JBEvent* event, const String& sid)
{
    if (!event)
	return false;

    // Requests must match the session id
    // Responses' id must start with session's local id (this is the way we generate the stanza id)
    if (sid) {
	if (sid != m_sid)
	    return false;
    }
    else if (!event->id().startsWith(m_localSid))
	return false;
    // Check to/from
    if (m_localJID != event->to() || m_remoteJID != event->from())
	return false;

    // Ok: keep a referenced event
    if (event->ref())
	enqueue(event);
    return true;
}

// Confirm a received element. If the error is NoError a result stanza will be sent
// Otherwise, an error stanza will be created and sent
bool JGSession::confirm(XMLElement* xml, XMPPError::Type error,
	const char* text, XMPPError::ErrorType type)
{
    if (!xml)
	return false;
    XMLElement* iq = 0;
    if (error == XMPPError::NoError) {
	String id = xml->getAttribute("id");
	iq = XMPPUtils::createIq(XMPPUtils::IqResult,m_localJID,m_remoteJID,id);
	// The receiver will detect which stanza is confirmed by id
	// If missing, make a copy of the received element and attach it to the error
	if (!id) {
	    XMLElement* copy = new XMLElement(*xml);
	    iq->addChild(copy);
	}
    }
    else
	iq = XMPPUtils::createError(xml,type,error,text);
    return sendStanza(iq,0,false);
}

// Close a Pending or Active session
bool JGSession::hangup(int reason, const char* msg)
{
    Lock lock(this);
    if (state() != Pending && state() != Active)
	return false;
    DDebug(m_engine,DebugAll,"Call(%s). Hangup('%s') [%p]",m_sid.c_str(),msg,this);
    // Clear sent stanzas list. We will wait for this element to be confirmed
    m_sentStanza.clear();
    const char* tmp = lookupReason(reason);
    XMLElement* res = 0;
    if (tmp || msg) {
	res = new XMLElement(XMLElement::Reason);
	if (tmp)
	    res->addChild(new XMLElement(tmp));
	if (msg)
	    res->addChild(new XMLElement(XMLElement::Text,0,msg));
    }
    XMLElement* xml = createJingle(ActTerminate,res);
    bool ok = sendStanza(xml);
    changeState(Ending);
    return ok;
}

// Build SOCKS SHA1 dst.addr used by file transfer
void JGSession::buildSocksDstAddr(String& buf)
{
    SHA1 sha(m_sid);
    if (outgoing())
	sha << m_localJID << m_remoteJID;
    else
	sha << m_remoteJID << m_localJID;
    buf = sha.hexDigest();
}

// Send a session info element to the remote peer
bool JGSession::sendInfo(XMLElement* xml, String* stanzaId)
{
    if (!xml)
	return false;
    // Make sure we dont't terminate the session if info fails
    String tmp;
    if (!stanzaId) {
	tmp = "Info" + String(Time::secNow());
	stanzaId = &tmp;
    }
    return sendStanza(createJingle(ActInfo,xml),stanzaId);
}

// Send a dtmf string to remote peer
bool JGSession::sendDtmf(const char* dtmf, unsigned int msDuration, String* stanzaId)
{
    if (!(dtmf && *dtmf))
	return false;

    XMLElement* iq = createJingle(version() != Version0 ? ActInfo : ActContentInfo);
    XMLElement* sess = iq->findFirstChild();
    if (!sess) {
	TelEngine::destruct(iq);
	return false;
    }
    char s[2] = {0,0};
    while (*dtmf) {
	s[0] = *dtmf++;
	sess->addChild(createDtmf(s,msDuration));
    }
    TelEngine::destruct(sess);
    return sendStanza(iq,stanzaId);
}

// Check if the remote party supports a given feature
bool JGSession::hasFeature(XMPPNamespace::Type feature)
{
    if (!m_stream)
	return false;
    JBClientStream* cStream = static_cast<JBClientStream*>(m_stream->getObject("JBClientStream"));
    if (cStream) {
	XMPPUser* user = cStream->getRemote(remote());
	if (!user)
	    return false;
	bool ok = false;
	user->lock();
	JIDResource* res = user->remoteRes().get(remote().resource());
	ok = res && res->features().get(feature);
	user->unlock();
	TelEngine::destruct(user);
	return ok;
    }
    return false;
}

// Build a transfer element
XMLElement* JGSession::buildTransfer(const String& transferTo,
    const String& transferFrom, const String& sid)
{
    XMLElement* transfer = XMPPUtils::createElement(XMLElement::Transfer,
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
    // Deque and process event(s)
    // Loop until a jingle event is generated or no more events in queue
    JBEvent* jbev = 0;
    while (true) {
	TelEngine::destruct(jbev);
	jbev = static_cast<JBEvent*>(m_events.remove(false));
	if (!jbev)
	    break;

	DDebug(m_engine,DebugAll,
	    "Call(%s). Dequeued Jabber event (%p,%s) in state %s [%p]",
	    m_sid.c_str(),jbev,jbev->name(),lookupState(state()),this);

	// Update ping interval
	if (m_engine && m_engine->pingInterval())
	    m_timeToPing = time + m_engine->pingInterval();
	else
	    m_timeToPing = 0;

	// Process Jingle 'set' stanzas
	if (jbev->type() == JBEvent::IqJingleSet) {
	    // Filter some conditions in which we can't accept any jingle stanza
	    // Outgoing idle sessions are waiting for the user to initiate them
	    if (state() == Idle && outgoing()) {
		confirm(jbev->releaseXML(),XMPPError::SRequest);
		continue;
	    }

	    m_lastEvent = decodeJingle(jbev);

	    if (!m_lastEvent) {
		// Destroy incoming session if session initiate stanza contains errors
		if (!outgoing() && state() == Idle) {
		    m_lastEvent = new JGEvent(JGEvent::Destroy,this,0,"failure");
		    // TODO: hangup
		    break;
		}
		continue;
	    }

	    // ActInfo: empty session info
	    if (m_lastEvent->action() == ActInfo) {
	        XDebug(m_engine,DebugAll,"Call(%s). Received empty '%s' (ping) [%p]",
		    m_sid.c_str(),m_lastEvent->actionName(),this);
		m_lastEvent->confirmElement();
		delete m_lastEvent;
		m_lastEvent = 0;
		continue;
	    }

	    processJingleSetLastEvent(*jbev);
	    if (!m_lastEvent)
		continue;
	    break;
	}

	if (jbev->type() == JBEvent::Iq) {
	    processJabberIqEvent(*jbev);
	    if (m_lastEvent)
		break;
	    continue;
	}

	// Check for responses or failures
	if (jbev->type() == JBEvent::IqJingleRes ||
	    jbev->type() == JBEvent::IqJingleErr ||
	    jbev->type() == JBEvent::IqResult ||
	    jbev->type() == JBEvent::IqError ||
	    jbev->type() == JBEvent::WriteFail) {
	    if (!processJabberIqResponse(*jbev) || m_lastEvent)
		break;
	    continue;
	}

	// Silently ignore temporary stream down
	if (jbev->type() == JBEvent::Terminated) {
	    DDebug(m_engine,DebugInfo,
		"Call(%s). Stream disconnected in state %s [%p]",
		m_sid.c_str(),lookupState(state()),this);
	    continue;
	}

	// Terminate on stream destroy
	if (jbev->type() == JBEvent::Destroy) {
	    Debug(m_engine,DebugInfo,
		"Call(%s). Stream destroyed in state %s [%p]",
		m_sid.c_str(),lookupState(state()),this);
	    m_lastEvent = new JGEvent(JGEvent::Terminated,this,0,"noconn");
	    break;
	}

	Debug(m_engine,DebugStub,"Call(%s). Unhandled event type %u '%s' [%p]",
	    m_sid.c_str(),jbev->type(),jbev->name(),this);
	continue;
    }
    TelEngine::destruct(jbev);

    // No event: check first sent stanza's timeout
    if (!m_lastEvent) {
	ObjList* o = m_sentStanza.skipNull();
	JGSentStanza* tmp = o ? static_cast<JGSentStanza*>(o->get()) : 0;
	while (tmp && tmp->timeout(time)) {
	    Debug(m_engine,DebugNote,"Call(%s). Sent stanza ('%s') timed out [%p]",
		m_sid.c_str(),tmp->c_str(),this);
	    // Don't terminate if the sender requested to be notified
	    m_lastEvent = new JGEvent(tmp->notify() ? JGEvent::ResultTimeout : JGEvent::Terminated,
		this,0,"timeout");
	    m_lastEvent->m_id = *tmp;
	    o->remove();
	    if (m_lastEvent->final())
		hangup(false,"Timeout");
	    break;
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
    sendPing(time);

    return 0;
}

// Release this session and its memory
void JGSession::destroyed()
{
    // Remove from engine
    if (m_engine) {
	Lock lock(m_engine);
	m_engine->m_sessions.remove(this,false);
    }
    lock();
    // Cleanup. Respond to events in queue
    if (m_stream) {
	hangup(ReasonUnknown);
	for (ObjList* o = m_events.skipNull(); o; o = o->skipNext()) {
	    JBEvent* jbev = static_cast<JBEvent*>(o->get());
	    // Skip events generated by the stream
	    if (jbev->type() == JBEvent::WriteFail ||
		jbev->type() == JBEvent::Terminated ||
		jbev->type() == JBEvent::Destroy)
		continue;
	    // Respond to non error/result IQs
	    XMLElement* xml = jbev->element();
	    if (!(xml && xml->type() == XMLElement::Iq))
		continue;
	    XMPPUtils::IqType t = XMPPUtils::iqType(xml->getAttribute("type"));
	    if (t == XMPPUtils::IqError || t == XMPPUtils::IqResult)
		continue;
	    // Respond
	    if (m_recvTerminate)
		confirm(jbev->releaseXML(),XMPPError::SRequest,
		    "Session terminated",XMPPError::TypeCancel);
	    else {
		XMLElement* jingle = checkJingle(jbev->child());
		m_recvTerminate = jingle &&  ActTerminate == getAction(jingle);
		confirm(jbev->element());
	    }
	}
	TelEngine::destruct(m_stream);
    }
    m_events.clear();
    unlock();
    DDebug(m_engine,DebugInfo,"Call(%s). Destroyed [%p]",m_sid.c_str(),this);
}

// Enqueue a Jabber engine event
void JGSession::enqueue(JBEvent* event)
{
    Lock lock(this);
    if (event->type() == JBEvent::Terminated || event->type() == JBEvent::Destroy)
	m_events.insert(event);
    else
	m_events.append(event);
    DDebug(m_engine,DebugAll,"Call(%s). Accepted event (%p,%s) [%p]",
	m_sid.c_str(),event,event->name(),this);
}

// Send a stanza to the remote peer
bool JGSession::sendStanza(XMLElement* stanza, String* stanzaId, bool confirmation,
    bool ping)
{
    if (!stanza)
	return false;
    Lock lock(this);
    // confirmation=true: this is not a response, don't allow if terminated
    bool terminated = (state() == Ending || state() == Destroy);
    if (!m_stream || (terminated && confirmation)) {
#ifdef DEBUG
	Debug(m_engine,DebugNote,
	    "Call(%s). Can't send stanza (%p,'%s') in state %s [%p]",
	    m_sid.c_str(),stanza,stanza->name(),lookupState(m_state),this);
#endif
	TelEngine::destruct(stanza);
	return false;
    }
    DDebug(m_engine,DebugAll,"Call(%s). Sending stanza (%p,'%s') id=%s [%p]",
	m_sid.c_str(),stanza,stanza->name(),String::boolText(stanzaId != 0),this);
    const char* senderId = m_localSid;
    // Check if the stanza should be added to the list of stanzas requiring confirmation
    if (confirmation && stanza->type() == XMLElement::Iq) {
	String id = m_localSid;
	id << "_" << (unsigned int)m_stanzaId++;
	JGSentStanza* sent = new JGSentStanza(id,
	    m_engine->stanzaTimeout() + Time::msecNow(),stanzaId != 0,ping);
	stanza->setAttribute("id",*sent);
	senderId = *sent;
	if (stanzaId)
	    *stanzaId = *sent;
	m_sentStanza.append(sent);
    }
    // Send. If it fails leave it in the sent items to timeout
    JBStream::Error res = m_stream->sendStanza(stanza,senderId);
    if (res == JBStream::ErrorNoSocket || res == JBStream::ErrorContext)
	return false;
    return true;
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

// Method called in getEvent() to process a last event set from a jingle set jabber event
void JGSession::processJingleSetLastEvent(JBEvent& ev)
{
    if (!m_lastEvent)
	return;
    DDebug(m_engine,DebugInfo,"Call(%s). Processing action (%u,'%s') state=%s [%p]",
	m_sid.c_str(),m_lastEvent->action(),m_lastEvent->actionName(),
	lookupState(state()),this);

    // Check for termination events
    if (m_lastEvent->final())
	return;

    bool error = false;
    bool fatal = false;
    switch (state()) {
	case Active:
	    error = m_lastEvent->action() == ActAccept ||
		m_lastEvent->action() == ActInitiate ||
		m_lastEvent->action() == ActRinging;
	    break;
	case Pending:
	    // Accept session-accept, transport, content and ringing stanzas
	    switch (m_lastEvent->action()) {
		case ActAccept:
		    if (outgoing()) {
			// XEP-0166 7.2.6: responder may be overridden
			if (m_lastEvent->jingle()) {
			    JabberID rsp = m_lastEvent->jingle()->getAttribute("responder");
			    if (!rsp.null() && m_remoteJID != rsp) {
				m_remoteJID.set(rsp);
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
		case ActRinging:
		case ActTrying:
		case ActReceived:
		    break;
		default:
		    error = true;
	    }
	    break;
	case Idle:
	    // Update data. Terminate if not a session initiating event
	    if (m_lastEvent->action() == ActInitiate) {
		m_localJID.set(ev.to());
		m_remoteJID.set(ev.from());
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
	switch (m_lastEvent->action()) {
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
		break;
	    default:
		m_lastEvent->confirmElement();
	}
	return;
    }
    m_lastEvent->confirmElement(XMPPError::SRequest);
    delete m_lastEvent;
    m_lastEvent = 0;
    if (fatal)
	m_lastEvent = new JGEvent(JGEvent::Destroy,this);
}

// Method called in getEvent() to process a jabber event carrying a response
bool JGSession::processJabberIqResponse(JBEvent& ev)
{
    JGSentStanza* sent = 0;
    // Find a sent stanza to match the event's id
    for (ObjList* o = m_sentStanza.skipNull(); o; o = o->skipNext()) {
	sent = static_cast<JGSentStanza*>(o->get());
	if (ev.id() == *sent)
	    break;
	sent = 0;
    }
    if (!sent)
	return true;
    // Check termination conditions
    // Always terminate when receiving responses in Ending state
    bool terminateEnding = (state() == Ending);
    // Terminate pending outgoing if no notification required
    // (Initial session request is sent without notification required)
    bool terminatePending = false;
    if (state() == Pending && outgoing() &&
	(ev.type() == JBEvent::IqJingleErr || ev.type() == JBEvent::WriteFail))
	terminatePending = !sent->notify();
    // Write fail: Terminate if failed stanza is a Jingle one and the sender
    //  didn't requested notification
    bool terminateFail = false;
    if (!(terminateEnding || terminatePending) && ev.type() == JBEvent::WriteFail)
	terminateFail = !sent->notify();
    // Generate event
    if (terminateEnding)
	m_lastEvent = new JGEvent(JGEvent::Destroy,this);
    else if (terminatePending || terminateFail)
	m_lastEvent = new JGEvent(JGEvent::Terminated,this,
	    ev.type() != JBEvent::WriteFail ? ev.releaseXML() : 0,
	    ev.text() ? ev.text().c_str() : "failure");
    else if (sent->notify())
	switch (ev.type()) {
	    case JBEvent::IqJingleRes:
	    case JBEvent::IqResult:
		m_lastEvent = new JGEvent(JGEvent::ResultOk,this,ev.releaseXML());
		break;
	    case JBEvent::IqJingleErr:
	    case JBEvent::IqError:
		m_lastEvent = new JGEvent(JGEvent::ResultError,this,ev.releaseXML(),ev.text());
		break;
	    case JBEvent::WriteFail:
		m_lastEvent = new JGEvent(JGEvent::ResultWriteFail,this,ev.releaseXML(),ev.text());
		break;
	    default:
		DDebug(m_engine,DebugStub,"Call(%s). Unhandled response event (%p,%u,%s) [%p]",
		    m_sid.c_str(),&ev,ev.type(),ev.name(),this);
	}
    else {
	// Terminate on ping error
	if (sent->ping()) {
	    terminateFail = ev.type() == JBEvent::IqJingleErr ||
		ev.type() == JBEvent::WriteFail || ev.type() == JBEvent::IqError;
	    if (terminateFail)
		m_lastEvent = new JGEvent(JGEvent::Terminated,this,
		    ev.type() != JBEvent::WriteFail ? ev.releaseXML() : 0,
		    ev.text() ? ev.text().c_str() : "failure");
	}
    }
    if (m_lastEvent && !m_lastEvent->m_id)
	m_lastEvent->m_id = *sent;

    String error;
#ifdef DEBUG
    if (ev.type() == JBEvent::IqJingleErr && ev.text())
	error << " (error='" << ev.text() << "')";
#endif
    bool terminate = (m_lastEvent && m_lastEvent->final());
    Debug(m_engine,(terminatePending || terminateFail) ? DebugNote : DebugAll,
	"Call(%s). Sent %selement with id=%s confirmed by event=%s%s%s [%p]",
	m_sid.c_str(),sent->ping() ? "ping " : "",ev.id().c_str(),
	ev.name(),error.safe(),terminate ? ". Terminating": "",this);
    m_sentStanza.remove(sent,true);
    // Gracefully terminate
    if (terminate && state() != Ending) {
	hangup(ReasonUnknown);
	return false;
    }
    return true;
}

// Method called in getEvent() to process a generic jabber iq event
void JGSession::processJabberIqEvent(JBEvent& ev)
{
    confirm(ev.releaseXML(),XMPPError::SFeatureNotImpl);
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


/**
 * JGSession0
 */

// Create an outgoing session
JGSession0::JGSession0(JGEngine* engine, JBStream* stream,
	const String& callerJID, const String& calledJID, const char* msg)
    : JGSession(Version0,engine,stream,callerJID,calledJID,msg)
{
}
 
// Create an incoming session
JGSession0::JGSession0(JGEngine* engine, JBEvent* event, const String& id)
    : JGSession(Version0,engine,event,id)
{
    m_sessContentName = m_localSid + "_content";
}

// Destructor
JGSession0::~JGSession0()
{
}

// Check if a given XML element is valid jingle one
XMLElement* JGSession0::checkJingle(XMLElement* xml)
{
    if (xml && xml->type() == XMLElement::Session &&
	XMPPUtils::hasXmlns(*xml,XMPPNamespace::JingleSession))
	return xml;
    return 0;
}

// Accept a Pending incoming session
bool JGSession0::accept(const ObjList& contents, String* stanzaId)
{
    Lock lock(this);
    if (outgoing() || state() != Pending)
	return false;
    XMLElement* xml = createJingle(ActAccept);
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
    XMLElement* xml = createJingle(action);
    addJingleContents0(m_sessContentName,xml,contents,minimal,addDesc,addTrans);
    return sendStanza(xml,stanzaId);
}

// Build and send the initial message on an outgoing session
bool JGSession0::initiate(const ObjList& contents, XMLElement* extra, const char* subject)
{
    XMLElement* xml = createJingle(ActInitiate);
    addJingleContents0(m_sessContentName,xml,contents,true,true,true);
    addJingleChild0(xml,extra);
    if (!null(subject))
	addJingleChild0(xml,new XMLElement(XMLElement::Subject,0,subject));
    if (sendStanza(xml)) {
	changeState(Pending);
	return true;
    }
    changeState(Destroy);
    return false;
}

// Decode a valid jingle set event. Set the event's data on success
JGEvent* JGSession0::decodeJingle(JBEvent* jbev)
{
    XMLElement* jingle = jbev->child();
    if (!jingle) {
	confirm(jbev->releaseXML(),XMPPError::SBadRequest);
	return 0;
    }

    Action act = getAction(jingle);
    if (act == ActCount) {
	confirm(jbev->releaseXML(),XMPPError::SServiceUnavailable,
	    "Unknown session action");
	return 0;
    }

    // *** ActTerminate
    if (act == ActTerminate) {
	// Confirm here: this is a final event,
	//  stanza won't be confirmed in getEvent()
	m_recvTerminate = true;
	const char* reason = 0;
	const char* text = 0;
	XMLElement* res = jingle->findFirstChild(XMLElement::Reason);
	if (res) {
	    XMLElement* tmp = res->findFirstChild();
	    if (tmp && tmp->type() != XMLElement::Text)
		reason = tmp->name();
	    TelEngine::destruct(tmp);
	    tmp = res->findFirstChild(XMLElement::Text);
	    if (tmp)
		text = tmp->getText();
	    TelEngine::destruct(tmp);
	    TelEngine::destruct(res);
	}
	JGEvent* ev = new JGEvent(JGEvent::Terminated,this,jbev->releaseXML(),reason,text);
	ev->setAction(act);
	ev->confirmElement();
	return ev;
    }

    // *** ActContentInfo --> ActDtmf
    if (act == ActContentInfo) {
	// Check dtmf
	// Expect more then 1 'dtmf' child
	XMLElement* tmp = jingle->findFirstChild(XMLElement::Dtmf);
	String text;
	for (; tmp; tmp = jingle->findNextChild(tmp,XMLElement::Dtmf)) {
	    String reason = tmp->getAttribute("action");
	    if (reason == "button-up")
		text << tmp->getAttribute("code");
	}
	if (text)
	    return new JGEvent(ActDtmf,this,jbev->releaseXML(),0,text);
	confirm(jbev->releaseXML(),XMPPError::SServiceUnavailable);
	return 0;
    }
 
    // *** ActInfo
    if (act == ActInfo) {
        // Check info element
	// Return ActInfo event to signal ping (XEP-0166 6.8)
	XMLElement* child = jingle->findFirstChild();
	if (!child)
	    return new JGEvent(ActInfo,this,jbev->releaseXML());
 
	JGEvent* event = 0;
	Action a = ActCount;
	XMPPNamespace::Type ns = XMPPNamespace::Count;
	// Check namespace and build event
	switch (child->type()) {
	    case XMLElement::Ringing:
		a = ActRinging;
		ns = XMPPNamespace::JingleRtpInfoOld;
		break;
	    case XMLElement::Mute:
		a = ActMute;
		ns = XMPPNamespace::JingleRtpInfoOld;
		break;
	    default: ;
	}
	if (a != ActCount && XMPPUtils::hasXmlns(*child,ns))
	    event = new JGEvent(a,this,jbev->releaseXML());
	else
	    confirm(jbev->releaseXML(),XMPPError::SFeatureNotImpl);
        TelEngine::destruct(child);
	return event;
    }
 
    if (act == ActTransportAccept) {
	confirm(jbev->element());
	return 0;
    }

    // Get transport
    // Get media description
    // Create event, update transport and media
    XMLElement* media = 0;
    XMLElement* trans = jingle;
    JGSessionContent* c = 0;
    JGEvent* event = 0;
    while (true) {
	c = new JGSessionContent(JGSessionContent::RtpIceUdp,m_sessContentName,
	    JGSessionContent::SendBoth,JGSessionContent::CreatorInitiator);
	c->m_rtpRemoteCandidates.m_type = JGRtpCandidates::RtpIceUdp;
	// Build media
	if (act == ActInitiate || act == ActAccept) {
	    media = jingle->findFirstChild(XMLElement::Description);
	    if (media && XMPPUtils::hasXmlns(*media,XMPPNamespace::JingleAudio)) {
		c->m_rtpMedia.fromXML(media);
		c->m_rtpMedia.m_media = JGRtpMediaList::Audio;
	    }
	    else
		break;
	}
	// Build transport
	trans = trans->findFirstChild(XMLElement::Transport);
	if (trans && !XMPPUtils::hasXmlns(*trans,XMPPNamespace::JingleTransport)) {
	    if (trans != jingle)
		TelEngine::destruct(trans);
	    else
		trans = 0;
	}
	XMLElement* t = trans ? trans->findFirstChild(XMLElement::Candidate) : 0;
	if (t) {
	    JGRtpCandidate* cd = new JGRtpCandidate(m_localSid + "_transport");
	    cd->m_component = "1";
	    cd->m_generation = t->getAttribute("generation");
	    cd->m_address = t->getAttribute("address");
	    cd->m_port = t->getAttribute("port");
	    cd->m_protocol = t->getAttribute("protocol");;
	    cd->m_generation = t->getAttribute("generation");
	    cd->m_type = t->getAttribute("type");
	    c->m_rtpRemoteCandidates.m_ufrag = t->getAttribute("username");
	    c->m_rtpRemoteCandidates.m_password = t->getAttribute("password");
	    c->m_rtpRemoteCandidates.append(cd);
	    TelEngine::destruct(t);
	}
	else if (act == ActTransportInfo)
	    break;
	// Don't set the event's element yet: this would invalidate the 'jingle' variable
	event = new JGEvent(act,this,jbev->releaseXML());
	event->m_contents.append(c);
	break;
    }
    if (trans != jingle)
	TelEngine::destruct(trans);
    TelEngine::destruct(media);
    if (!event) {
	TelEngine::destruct(c);
	confirm(jbev->releaseXML(),XMPPError::SServiceUnavailable);
    }
    return event;
}

// Create an 'iq' stanza with a 'jingle' child
XMLElement* JGSession0::createJingle(Action action, XMLElement* element1,
    XMLElement* element2, XMLElement* element3)
{
    XMLElement* iq = XMPPUtils::createIq(XMPPUtils::IqSet,m_localJID,m_remoteJID,0);
    XMLElement* jingle = XMPPUtils::createElement(XMLElement::Session,
	XMPPNamespace::JingleSession);
    if (action < ActCount)
	jingle->setAttribute("type",lookupAction(action,version()));
    jingle->setAttribute("initiator",outgoing() ? m_localJID : m_remoteJID);
    jingle->setAttribute("responder",outgoing() ? m_remoteJID : m_localJID);
    jingle->setAttribute("id",m_sid);
    jingle->addChild(element1);
    jingle->addChild(element2);
    jingle->addChild(element3);
    iq->addChild(jingle);
    return iq;
}

// Create a dtmf XML element
XMLElement* JGSession0::createDtmf(const char* dtmf, unsigned int msDuration)
{
    XMLElement* xml = XMPPUtils::createElement(XMLElement::Dtmf,XMPPNamespace::DtmfOld);
    xml->setAttribute("action","button-up");
    xml->setAttribute("code",dtmf);
    return xml;
}


/**
 * JGSession1
 */

// Create an outgoing session
JGSession1::JGSession1(JGEngine* engine, JBStream* stream,
	const String& callerJID, const String& calledJID, const char* msg)
    : JGSession(Version1,engine,stream,callerJID,calledJID,msg)
{
}
 
// Create an incoming session
JGSession1::JGSession1(JGEngine* engine, JBEvent* event, const String& id)
    : JGSession(Version1,engine,event,id)
{
}

// Destructor
JGSession1::~JGSession1()
{
}

// Build and send the initial message on an outgoing session
bool JGSession1::initiate(const ObjList& contents, XMLElement* extra, const char* subject)
{
    XMLElement* xml = createJingle(ActInitiate);
    addJingleContents(xml,contents,false,true,true,true);
    addJingleChild(xml,extra);
    if (!null(subject))
	addJingleChild(xml,new XMLElement(XMLElement::Subject,0,subject));
    if (sendStanza(xml)) {
	changeState(Pending);
	return true;
    }
    changeState(Destroy);
    return false;
}

// Check if a given XML element is valid jingle one
XMLElement* JGSession1::checkJingle(XMLElement* xml)
{
    if (xml && xml->type() == XMLElement::Jingle &&
	XMPPUtils::hasXmlns(*xml,XMPPNamespace::Jingle))
	return xml;
    return 0;
}

// Accept a Pending incoming session
bool JGSession1::accept(const ObjList& contents, String* stanzaId)
{
    Lock lock(this);
    if (outgoing() || state() != Pending)
	return false;
    XMLElement* xml = createJingle(ActAccept);
    addJingleContents(xml,contents,false,true,true,true,true);
    if (!sendStanza(xml,stanzaId))
	return false;
    changeState(Active);
    return true;
}

// Create a 'hold' child to be added to a session-info element
XMLElement* JGSession1::createHoldXml()
{
    return XMPPUtils::createElement(XMLElement::Hold,XMPPNamespace::JingleAppsRtpInfo);
}

XMLElement* JGSession1::createActiveXml()
{
    return XMPPUtils::createElement(XMLElement::Active,XMPPNamespace::JingleAppsRtpInfo);
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
    XMLElement* xml = createJingle(action);
    addJingleContents(xml,contents,minimal,addDesc,addTrans,addCandidates,addIceAuth);
    return sendStanza(xml,stanzaId);
}

// Send a stanza with stream hosts
bool JGSession1::sendStreamHosts(const ObjList& hosts, String* stanzaId)
{
    Lock lock(this);
    if (state() != Pending)
	return false;
    XMLElement* xml = XMPPUtils::createIq(XMPPUtils::IqSet,m_localJID,m_remoteJID,0);
    xml->addChild(JGStreamHost::buildHosts(hosts,m_sid));
    return sendStanza(xml,stanzaId);
}

// Send a stanza with a stream host used
bool JGSession1::sendStreamHostUsed(const char* jid, const char* stanzaId)
{
    Lock lock(this);
    if (state() != Pending)
	return false;
    bool ok = !null(jid);
    XMLElement* xml = XMPPUtils::createIq(ok ? XMPPUtils::IqResult : XMPPUtils::IqError,
	m_localJID,m_remoteJID,stanzaId);
    if (ok)
	xml->addChild(JGStreamHost::buildRsp(jid));
    else
	xml->addChild(XMPPUtils::createError(XMPPError::TypeModify,
	    XMPPError::ItemNotFound));
    return sendStanza(xml,0,false);
}

// Decode a jingle stanza
JGEvent* JGSession1::decodeJingle(JBEvent* jbev)
{
    XMLElement* jingle = jbev->child();
    if (!jingle) {
	confirm(jbev->releaseXML(),XMPPError::SBadRequest);
	return 0;
    }

    Action act = getAction(jingle);
    if (act == ActCount) {
	confirm(jbev->releaseXML(),XMPPError::SServiceUnavailable,
	    "Unknown session action");
	return 0;
    }

    // *** ActTerminate
    if (act == ActTerminate) {
	// Confirm here: this is a final event, 
	//  stanza won't be confirmed in getEvent()
	m_recvTerminate = true;
	const char* reason = 0;
	const char* text = 0;
	XMLElement* res = jingle->findFirstChild(XMLElement::Reason);
	if (res) {
	    XMLElement* tmp = res->findFirstChild();
	    if (tmp && tmp->type() != XMLElement::Text)
		reason = tmp->name();
	    TelEngine::destruct(tmp);
	    tmp = res->findFirstChild(XMLElement::Text);
	    if (tmp)
		text = tmp->getText();
	    TelEngine::destruct(tmp);
	    TelEngine::destruct(res);
	}
	if (!reason)
	    reason = act==ActTerminate ? "hangup" : "rejected";
	JGEvent* ev = new JGEvent(JGEvent::Terminated,this,jbev->releaseXML(),reason,text);
	ev->setAction(act);
	ev->confirmElement();
	return ev;
    }

    // *** ActInfo
    if (act == ActInfo) {
        // Check info element
	// Return ActInfo event to signal ping (XEP-0166 6.8)
	XMLElement* child = jingle->findFirstChild();
	if (!child)
	    return new JGEvent(ActInfo,this,jbev->releaseXML());

	JGEvent* event = 0;
	Action a = ActCount;
	XMPPNamespace::Type ns = XMPPNamespace::Count;
	// Check namespace and build event
	switch (child->type()) {
	    case XMLElement::Dtmf:
		a = ActDtmf;
		ns = XMPPNamespace::Dtmf;
		break;
	    case XMLElement::Transfer:
		a = ActTransfer;
		ns = XMPPNamespace::JingleTransfer;
		break;
	    case XMLElement::Hold:
		a = ActHold;
		ns = XMPPNamespace::JingleAppsRtpInfo;
		break;
	    case XMLElement::Active:
		a = ActActive;
		ns = XMPPNamespace::JingleAppsRtpInfo;
		break;
	    case XMLElement::Ringing:
		a = ActRinging;
		ns = XMPPNamespace::JingleAppsRtpInfo;
		break;
	    case XMLElement::Trying:
		a = ActTrying;
		ns = XMPPNamespace::JingleTransportRawUdpInfo;
		break;
	    case XMLElement::Received:
		a = ActReceived;
		ns = XMPPNamespace::JingleTransportRawUdpInfo;
		break;
	    case XMLElement::Mute:
		a = ActMute;
		ns = XMPPNamespace::JingleAppsRtpInfo;
		break;
	    default: ;
	}
	if (a != ActCount && XMPPUtils::hasXmlns(*child,ns)) {
	    String text;
	    // Add Dtmf
	    if (a == ActDtmf) {
		// Expect more then 1 'dtmf' child
		for (; child; child = jingle->findNextChild(child,XMLElement::Dtmf))
		    text << child->getAttribute("code");
		if (!text) {
		    confirm(jbev->releaseXML(),XMPPError::SBadRequest,"Empty dtmf(s)");
		    return 0;
		}
	    }
	    event = new JGEvent(a,this,jbev->releaseXML(),"",text);
	}
	else
	    confirm(jbev->releaseXML(),XMPPError::SFeatureNotImpl);
        TelEngine::destruct(child);
	return event;
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
	    break;
	default:
	    confirm(jbev->releaseXML(),XMPPError::SServiceUnavailable);
	    return 0;
    }

    JGEvent* event = new JGEvent(act,this,jbev->releaseXML());
    jingle = event->jingle();
    if (!jingle) {
	event->confirmElement(XMPPError::SInternal);
	delete event;
	return 0;
    }
    XMPPError::Type err = XMPPError::NoError;
    String text;
    XMLElement* c = jingle->findFirstChild(XMLElement::Content);
    for (; c; c = jingle->findNextChild(c,XMLElement::Content)) {
	JGSessionContent* content = JGSessionContent::fromXml(c,err,text);
	if (content) {
	    DDebug(m_engine,DebugAll,
		"Call(%s). Found content='%s' in '%s' stanza [%p]",
		m_sid.c_str(),content->toString().c_str(),event->actionName(),this);
	    event->m_contents.append(content);
	    continue;
	}
	if (err == XMPPError::NoError) {
	    DDebug(m_engine,DebugAll,
		"Call(%s). Ignoring content='%s' in '%s' stanza [%p]",
		m_sid.c_str(),c->getAttribute("name"),event->actionName(),this);
	    continue;
	}
	// Error
	TelEngine::destruct(c);
	event->confirmElement(err,text);
	delete event;
	return 0;
    }
    return event;
}

// Create an 'iq' stanza with a 'jingle' child
XMLElement* JGSession1::createJingle(Action action, XMLElement* element1,
    XMLElement* element2, XMLElement* element3)
{
    XMLElement* iq = XMPPUtils::createIq(XMPPUtils::IqSet,m_localJID,m_remoteJID,0);
    XMLElement* jingle = XMPPUtils::createElement(XMLElement::Jingle,
	XMPPNamespace::Jingle);
    if (action < ActCount)
	jingle->setAttribute("type",lookupAction(action,version()));
    jingle->setAttribute("initiator",outgoing() ? m_localJID : m_remoteJID);
    jingle->setAttribute("responder",outgoing() ? m_remoteJID : m_localJID);
    jingle->setAttribute("sid",m_sid);
    jingle->addChild(element1);
    jingle->addChild(element2);
    jingle->addChild(element3);
    iq->addChild(jingle);
    return iq;
}

// Create a dtmf XML element
XMLElement* JGSession1::createDtmf(const char* dtmf, unsigned int msDuration)
{
    XMLElement* xml = XMPPUtils::createElement(XMLElement::Dtmf,XMPPNamespace::Dtmf);
    xml->setAttribute("code",dtmf);
    if (msDuration > 0)
	xml->setAttribute("duration",String(msDuration));
    return xml;
}

// Method called in getEvent() to process a generic jabber iq event
void JGSession1::processJabberIqEvent(JBEvent& ev)
{
    // File transfer
    XMLElement* child = ev.child();
    if (child && child->type() == XMLElement::Query &&
	XMPPUtils::hasXmlns(*child,XMPPNamespace::ByteStreams)) {
	m_lastEvent = new JGEvent(ActStreamHost,this,ev.releaseXML());
	child = m_lastEvent->element()->findFirstChild(XMLElement::Query);
	XMLElement* sh = child->findFirstChild(XMLElement::StreamHost);
	for (; sh; sh = child->findNextChild(sh,XMLElement::StreamHost)) {
	    JGStreamHost* s = JGStreamHost::fromXml(sh);
	    if (s)
		m_lastEvent->m_streamHosts.append(s);
	}
	TelEngine::destruct(child);
    }
    else
	confirm(ev.releaseXML(),XMPPError::SFeatureNotImpl);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
