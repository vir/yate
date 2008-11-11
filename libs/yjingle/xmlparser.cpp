/**
 * xmlparser.cpp
 * Yet Another XMPP Stack
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

#include <xmlparser.h>
#include <string.h>

using namespace TelEngine;

/**
 * XMLElement
 */
TokenDict XMLElement::s_names[] = {
    {"stream:stream",        StreamStart},
    {"/stream:stream",       StreamEnd},
    {"stream:error",         StreamError},
    {"stream:features",      StreamFeatures},
    {"register",             Register},
    {"starttls",             Starttls},
    {"handshake",            Handshake},
    {"auth",                 Auth},
    {"challenge",            Challenge},
    {"abort",                Abort},
    {"aborted",              Aborted},
    {"response",             Response},
    {"proceed",              Proceed},
    {"success",              Success},
    {"failure",              Failure},
    {"mechanisms",           Mechanisms},
    {"mechanism",            Mechanism},
    {"session",              Session},
    {"iq",                   Iq},
    {"message",              Message},
    {"presence",             Presence},
    {"error",                Error},
    {"query",                Query},
    {"vCard",                VCard},
    {"session",              Jingle},
    {"description",          Description},
    {"payload-type",         PayloadType},
    {"transport",            Transport},
    {"candidate",            Candidate},
    {"body",                 Body},
    {"subject",              Subject},
    {"feature",              Feature},
    {"bind",                 Bind},
    {"resource",             Resource},
    {"transfer",             Transfer},
    {"hold",                 Hold},
    {"active",               Active},
    {"ringing",              Ringing},
    {"mute",                 Mute},
    {"registered",           Registered},
    {"remove",               Remove},
    {"jid",                  Jid},
    {"username",             Username},
    {"password",             Password},
    {"digest",               Digest},
    {"required",             Required},
    {"dtmf",                 Dtmf},
    {"dtmf-method",          DtmfMethod},
    {"command",              Command},
    {"text",                 Text},
    {"item",                 Item},
    {"group",                Group},
    {"reason",               Reason},
    {0,0}
};

XMLElement::XMLElement()
    : m_type(StreamEnd), m_owner(true), m_element(0)
{
    m_element = new TiXmlElement(typeName(m_type));
//    XDebug(DebugAll,"XMLElement::XMLElement(%s) [%p]",name(),this);
}

XMLElement::XMLElement(const XMLElement& src)
    : m_type(Invalid), m_owner(true), m_element(0)
{
    TiXmlElement* e = src.get();
    if (!e)
	return;
    m_element = new TiXmlElement(*e);
    setType();
//    XDebug(DebugAll,"XMLElement::XMLElement(%s) [%p]",name(),this);
}

// Partially build this element from another one.
// Copy name and 'to', 'from', 'type', 'id' attributes
XMLElement::XMLElement(const XMLElement& src, bool response, bool result)
    : m_type(src.type()), m_owner(true), m_element(0)
{
    m_element = new TiXmlElement(src.name());
    if (response) {
	setAttributeValid("from",src.getAttribute("to"));
	setAttributeValid("to",src.getAttribute("from"));
	setAttribute("type",result?"result":"error");
    }
    else {
	setAttributeValid("from",src.getAttribute("from"));
	setAttributeValid("to",src.getAttribute("to"));
	setAttributeValid("type",src.getAttribute("type"));
    }
    setAttributeValid("id",src.getAttribute("id"));
//    XDebug(DebugAll,"XMLElement::XMLElement(%s) [%p]",name(),this);
}

XMLElement::XMLElement(const char* name, NamedList* attributes,
    const char* text)
    : m_type(Unknown), m_owner(true), m_element(0)
{
    m_element = new TiXmlElement(name);
    // Set text
    if (text)
	m_element->LinkEndChild(new TiXmlText(text));
    // Add attributes
    if (attributes) {
	u_int32_t attr_count = attributes->length();
	for (u_int32_t i = 0; i < attr_count; i++) {
	    NamedString* ns = attributes->getParam(i);
	    if (!ns)
		continue;
	    m_element->SetAttribute(ns->name().c_str(),ns->c_str());
	}
    }
    setType();
//    XDebug(DebugAll,"XMLElement::XMLElement(%s) [%p]",this->name(),this);
}

XMLElement::XMLElement(Type type, NamedList* attributes,
    const char* text)
    : m_type(type), m_owner(true), m_element(0)
{
    m_element = new TiXmlElement(typeName(m_type));
    // Set text
    if (text)
	m_element->LinkEndChild(new TiXmlText(text));
    // Add attributes
    if (attributes) {
	u_int32_t attr_count = attributes->length();
	for (u_int32_t i = 0; i < attr_count; i++) {
	    NamedString* ns = attributes->getParam(i);
	    if (!ns)
		continue;
	    m_element->SetAttribute(ns->name().c_str(),ns->c_str());
	}
    }
//    XDebug(DebugAll,"XMLElement::XMLElement(%s) [%p]",name(),this);
}

XMLElement::XMLElement(TiXmlElement* element, bool owner)
    : m_type(Unknown), m_owner(owner), m_element(element)
{
    setType();
//    XDebug(DebugAll,"XMLElement::XMLElement(%s) owner=%u [%p]",
//	name(),m_owner,this);
}

// Build this XML element from a list containing name, attributes and text.
XMLElement::XMLElement(NamedList& src, const char* prefix)
    : m_type(Unknown), m_owner(true), m_element(0)
{
    m_element = new TiXmlElement(src.getValue(prefix));
    DDebug(DebugAll,"XMLElement(%s) src=%s prefix=%s [%p]",
	m_name.c_str(),src.c_str(),prefix,this);
    String pref(String(prefix) + ".");
    // Set text
    const char* text = src.getValue(pref);
    if (text)
	m_element->LinkEndChild(new TiXmlText(text));
    // Add attributes
    unsigned int n = src.count();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = src.getParam(i);
	if (ns && ns->name().startsWith(pref))
	    setAttribute(ns->name().substr(pref.length()),*ns);
    }
    setType();
}

XMLElement::~XMLElement()
{
    if (m_owner && m_element)
	delete m_element;
//    XDebug(DebugAll,"XMLElement::~XMLElement(%s) owner=%u [%p]",
//	m_name.c_str(),m_owner,this);
}

void XMLElement::toString(String& dest, bool unclose) const
{
    dest.clear();
    if (valid()) {
	TIXML_OSTREAM xmlStr;
	m_element->StreamOut(&xmlStr,unclose);
	dest.assign(xmlStr.c_str(),xmlStr.length());
    }
}

// Put this element's name, text and attributes to a list of parameters
void XMLElement::toList(NamedList& dest, const char* prefix)
{
    XDebug(DebugAll,"XMLElement(%s) to list=%s prefix=%s [%p]",
	m_name.c_str(),dest.c_str(),prefix,this);
    dest.addParam(prefix,name());
    String pref(String(prefix) + ".");
    const char* tmp = getText();
    if (tmp)
	dest.addParam(pref,tmp);
    for (const TiXmlAttribute* a = firstAttribute(); a; a = a->Next())
	dest.addParam(pref + a->Name(),a->Value());
}

void XMLElement::setAttribute(const char* name, const char* value)
{
    if (!(valid() && name && value))
	return;
    m_element->SetAttribute(name,value);
}

const char* XMLElement::getAttribute(const char* name) const
{
    if (valid() && name)
	return m_element->Attribute(name);
    return 0;
}

bool XMLElement::hasAttribute(const char* name, const char* value) const
{
    String tmp;
    if (getAttribute(name,tmp))
	return tmp == value;
    return false;
}

const char* XMLElement::getText() const
{
    if (valid())
	return m_element->GetText();
    return 0;
}

void XMLElement::addChild(XMLElement* element)
{
    if (valid() && element) {
	TiXmlElement* tiElement = element->releaseOwnership();
	if (tiElement)
	    m_element->LinkEndChild(tiElement);
    }
    TelEngine::destruct(element);
}

// Find the first child element of this one.
// Remove it from the children list.
// This element must own its TiXmlElement pointer.
XMLElement* XMLElement::removeChild(const char* name)
{
    if (!valid() && m_owner)
	return 0;
    TiXmlElement* element;
    if (name && *name)
	element = ((TiXmlNode*)m_element)->FirstChildElement(name);
    else
	element = ((TiXmlNode*)m_element)->FirstChildElement();
    if (!element)
	return 0;
    m_element->RemoveChild(element,false);
    return new XMLElement(element,true);
}

XMLElement* XMLElement::findFirstChild(const char* name)
{
    if (!valid())
	return 0;
    TiXmlElement* element;
    if (name && *name)
	element = ((TiXmlNode*)m_element)->FirstChildElement(name);
    else
	element = ((TiXmlNode*)m_element)->FirstChildElement();
    if (element)
	return new XMLElement(element,false);
    return 0;
}

XMLElement* XMLElement::findNextChild(XMLElement* element, const char* name)
{
    if (!valid()) {
	TelEngine::destruct(element);
	return 0;
    }
    TiXmlElement* tiElement = element ? element->get() : 0;
    XMLElement* result = 0;
    if (tiElement) {
	if (name && *name)
	    tiElement = tiElement->NextSiblingElement(name);
	else
	    tiElement = tiElement->NextSiblingElement();
	if (tiElement)
	    result = new XMLElement(tiElement,false);
    }
    else
	result = findFirstChild(name);
    TelEngine::destruct(element);
    return result;
}

// Get an xml element from a list's parameter
XMLElement* XMLElement::getXml(NamedList& list, bool stole,
	const char* name, const char* value)
{
    NamedString* ns = list.getParam(name);
    if (!ns)
	return 0;
    NamedPointer* np = static_cast<NamedPointer*>(ns->getObject("NamedPointer"));
    if (!(np && np->userObject("XMLElement")) || (value && *np != value))
	return 0;
    if (stole)
	return static_cast<XMLElement*>(np->takeData());
    return static_cast<XMLElement*>(np->userData());
}

TiXmlElement* XMLElement::releaseOwnership()
{
    if (!(m_owner && m_element))
	return 0;
    TiXmlElement* tiElement = m_element;
    m_element = 0;
    m_owner = false;
    return tiElement;
}
	
/**
 * XMLParser
 */
const char* skipBlanks(const char* p)
{
    for (; *p == ' ' || *p == '\r' || *p == '\n' || *p == '\t'; p++);
    return p;
}

u_int32_t XMLParser::s_maxDataBuffer = XMLPARSER_MAXDATABUFFER;
TiXmlEncoding XMLParser::s_xmlEncoding = TIXML_ENCODING_UTF8;

bool XMLParser::consume(const char* data, u_int32_t len)
{
    // * Input serialization is assumed to be done by the source
    // * Lock is necessary only when modifying TiXMLDocument
    // 1. Add data to buffer. Adjust stream start tag
    // 2. Call TiXMLDocument::Parse()
    // 3. Check result

    // 1
    String tmp(data,len);
    m_buffer << tmp;
//    XDebug(DebugAll,"XMLParser::consume. Buffer: '%s'.",m_buffer.c_str());
    if (m_buffer.length() > s_maxDataBuffer) {
	SetError(TIXML_ERROR_BUFFEROVERRUN,0,0,s_xmlEncoding);
	return false;
    }
    // Check for start element.
    // Adjust termination tag in order to pass correct data to the parser
    if (m_findstart) {
	int start = m_buffer.find("stream:stream");
	// Don't process until found: 'stream:stream' ... '>'
	if (start == -1)
	    return true;
	int end = m_buffer.find(">",start);
	if (end == -1)
	    return true;
	// Check if we received an end stream
	// Search for a '/' before 'stream:stream'
	int i_end = m_buffer.find("/");
	bool b_end = false;
	if (i_end != -1 && i_end < start) {
	    const char* pEnd = m_buffer.c_str() + i_end + 1;
	    const char* pStart = m_buffer.c_str() + start;
	    if (pStart == skipBlanks(pEnd))
		b_end = true;
	}
	if (!b_end) {
	    m_findstart = false;
	    if (i_end > start && i_end < end) {
		// We found a '/' between 'stream:stream' and '>'
		// Do nothing: The tag is already closed or the element
                // is invalid and the parser will fail
	    }
	    // Found: insert '/'
	    String tmp = m_buffer.substr(end,m_buffer.length() - end);
	    m_buffer = m_buffer.substr(0,end) << " /" << tmp;
	}
	else 
	    // Received end stream tag before start stream tag
	    // The element will be parsed: the upper layer will deal with it
	    ;
    }
    if (!m_buffer)
	return true;
    lock();
    const char* src = m_buffer.c_str();
    const char* ret = Parse(src,0,s_xmlEncoding);
    unlock();
    // Remove processed data from bufer
    if (ret > src && ret <= src + m_buffer.length())
	m_buffer = m_buffer.substr(ret - src);
//    XDebug(DebugAll,
//	"XMLParser::consume. Data parsed. Buffer: '%s'. Error: '%s'.",
//	m_buffer.c_str(),ErrorDesc());
    // 3
    if (ErrorId() && ErrorId() != TIXML_ERROR_INCOMPLETE)
	return false;
    return true;
}

XMLElement* XMLParser::extract()
{
    // Find the first TiXMLElement
    // Check if we received stream end
    // Remove any other object type
    for (;;) {
	TiXmlNode* node = FirstChild();
	if (!node)
	    break;
	// Check for XML elements
	if (node->ToElement()) {
	    RemoveChild(node,false);
	    return new XMLElement(node->ToElement(),true);
	}
	// Check for end stream
	// For Tiny XML '</...>' is an unknown element
	if (node->ToUnknown() &&
	    XMLElement::isType(node->Value(),XMLElement::StreamEnd)) {
	    RemoveChild(node,true);
	    return new XMLElement();
	}
	// Remove non-element
	RemoveChild(node,true);
    }
    return 0;
}

void XMLParser::reset()
{
    Lock lock(this);
    TiXmlDocument::Clear();
    m_buffer.clear();
    m_findstart = true;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
