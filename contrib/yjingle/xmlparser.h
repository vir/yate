/**
 * xmlparser.h
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

#ifndef __XMLPARSER_H
#define __XMLPARSER_H

#include <yateclass.h>
#include <tinyxml.h>

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

#define XMLPARSER_MAXDATABUFFER 8192     // Default max data buffer

class XMLElement;
class XMLParser;
class XMLElementOut;

/**
 * This class holds an XML element.
 * @short An XML element.
 */
class YJINGLE_API XMLElement
{
    friend class XMLParser;
public:
    enum Type {
	// *** Stream related elements
	StreamStart,                     // stream:stream
	StreamEnd,                       // /stream:stream
	StreamError,                     // stream::error
	Handshake,                       // handshake
	// *** Stanzas
	Iq,                              // iq
	Message,                         // message
	Presence,                        // presence
	// *** Stanza children
	Error,                           // error
	Query,                           // query
	Jingle,                          // session
	// Description
	Description,                     // description
	PayloadType,                     // payload-type
	// Transport
	Transport,                       // transport
	Candidate,                       // candidate
	// Message
	Body,                            // body
	// Resources
	Feature,                         // feature
	Bind,                            // bind
	Resource,                        // resource
	// Miscellanous
	Unknown,                         // Any text
	Invalid,                         // m_element is 0
    };

    /**
     * Constructor.
     * Constructs an StreamEnd element.
     */
    XMLElement();

    /**
     * Constructor.
     * Constructs an XML element with a TiXmlElement element with the given name.
     * Used for outgoing elements.
     * @param name The element's name.
     * @param atributes Optional list of attributes.
     * @param text Optional text for the XML element.
     */
    XMLElement(const char* name, NamedList* attributes = 0, const char* text = 0);

    /**
     * Constructor.
     * Constructs an XML element with a TiXmlElement element with the given type's name.
     * Used for outgoing elements.
     * @param tyte The element's type.
     * @param atributes Optional list of attributes.
     * @param text Optional text for the XML element.
     */
    XMLElement(Type type, NamedList* attributes = 0, const char* text = 0);

    /**
     * Destructor. Deletes the underlying TiXmlElement if owned.
     */
    virtual ~XMLElement();

    /**
     * Get the type of this object.
     * @return The type of this object as enumeration.
     */
    inline Type type() const
	{ return m_type; }

    /**
     * Get the TiXmlElement's name.
     * @return The name of the TiXmlElement object or 0.
     */
    inline const char* name() const
	{ return valid() ? m_element->Value() : 0; }

    /**
     * Check if the TiXmlElement's name is the given text.
     * @param text Text to compare with.
     * @return False if text is 0 or not equal to name.
     */
    inline bool nameIs(const char* text) const
	{ return (text && name() && (0 == ::strcmp(name(),text))); }

    /**
     * Get the validity of this object.
     * @return True if m_element is non null.
     */
    inline bool valid() const
	{ return m_element != 0; }

    /**
     * Put the element in a buffer.
     * @param dest Destination string.
     * @param unclose True to leave the tag unclosed.
     */
    void toString(String& dest, bool unclose = false) const;

    /**
     * Set the value of an existing attribute or adds a new one.
     * @param name Attribute's name.
     * @param value Attribute's value.
     */
    void setAttribute(const char* name, const char* value);

    /**
     * Set the value of an existing attribute or adds a new one if the value's length is not 0.
     * @param name Attribute's name.
     * @param value Attribute's value.
     */
    inline void setAttributeValid(const char* name, const String& value) {
	    if (value)
		setAttribute(name,value);
	}

    /**
     * Set the value of an existing attribute or adds a new one from an integer.
     * @param name Attribute's name.
     * @param value Attribute's value.
     */
    inline void setAttribute(const char* name, int value) {
	    String s(value);
	    setAttribute(name,s);
	}

    /**
     * Get the value of an attribute.
     * @param name Attribute's name.
     * @return Attribute's value. May be 0 if doesn't exists or empty.
     */
    const char* getAttribute(const char* name);

    /**
     * Get the value of an attribute.
     * @param name Attribute's name.
     * @param value Destination string.
     * @return True if attribute with the given name exists and is not empty.
     */
    inline bool getAttribute(const char* name, String& value) {
	    value = getAttribute(name);
	    return 0 != value.length();
	}

    /**
     * Check if an attribute with the given name and value exists.
     * @param name Attribute's name.
     * @param value Attribute's value.
     * @return True/False.
     */
    bool hasAttribute(const char* name, const char* value);

    /**
     * Get the text of this XML element.
     * @return Pointer to the text of this XML element or 0.
     */
    const char* getText();

    /**
     * Add a child to this object. Release the received element.
     *  On exit 'element' will be invalid if the operation succeedded.
     *  To succeed, 'element' MUST own his 'm_element'.
     * @param element XMLElement to add.
     */
    void addChild(XMLElement* element);

    /**
     * Find the first child element.
     * @param name Optional name of the child.
     * @return Pointer to an XMLElement or 0 if not found.
     */
    XMLElement* findFirstChild(const char* name = 0);

    /**
     * Find the first child element of the given type.
     * @param type Child's type to find.
     * @return Pointer to an XMLElement or 0 if not found.
     */
    inline XMLElement* findFirstChild(Type type)
	{ return findFirstChild(typeName(type)); }

    /**
     * Find the next child element.
     * @param element Starting XMLElement. O to find from the beginning.
     * @param name Optional name of the child.
     * @return Pointer to an XMLElement or 0 if not found.
     */
    XMLElement* findNextChild(const XMLElement* element, const char* name = 0);

    /**
     * Find the next child element of the given type.
     * @param element Starting XMLElement. O to find from the beginning.
     * @param type Child's type to find.
     * @return Pointer to an XMLElement or 0 if not found.
     */
    inline XMLElement* findNextChild(const XMLElement* element, Type type)
	{ return findNextChild(element,typeName(type)); }

    /**
     * Find the first attribute.
     * @return Pointer to the first attribute or 0.
     */
    inline const TiXmlAttribute* firstAttribute() const
	{ return valid() ? m_element->FirstAttribute() : 0; }

    /**
     * Get the name associated with the given type.
     * @param type Element type as enumeration.
     * @return Pointer to the name or 0.
     */
    static inline const char* typeName(Type type)
	{ return lookup(type,s_names); }

    /**
     * check if the given text is equal to the one associated with the given type.
     * @param txt Text to compare.
     * @param type Element type as enumeration.
     * @return True if txt equals the text associated with the given type.
     */
    static inline bool isType(const char* txt, Type type) {
	    const char* s = typeName(type);
	    return (txt && s && (0 == ::strcmp(txt,s)));
	}

protected:
    /**
     * Constructor.
     * Constructs an XML element from a TiXmlElement.
     * Used to extract elements from parser and access the children.
     * When extracting elements from parser the object will own the TiXmlElement.
     * When accessing the children, the object will not own the TiXmlElement.
     * @param element Pointer to a valid TiXmlElement.
     * @param owner Owner flag.
     */
    XMLElement(TiXmlElement* element, bool owner);

    /**
     * Get the underlying TiXmlElement.
     * @return The underlying TiXmlElement object or 0.
     */
    inline TiXmlElement* get() const
	{ return m_element; }

    /**
     * Release the ownership of the underlying TiXmlElement 
     *  and returns it if the object owns it.
     * @return The underlying TiXmlElement object or 0 if not owned or 0.
     */
    TiXmlElement* releaseOwnership();

    /**
     * Associations between XML element name and type.
     */
    static TokenDict s_names[];

private:
    // Set this object's type from m_element's name
    inline void setType()
	{ m_type = (Type)lookup(name(),s_names,Unknown); }

    Type m_type;                         // Element's type
    bool m_owner;                        // Owner flag. If true, this object owns the XML element
    TiXmlElement* m_element;             // The underlying XML element
};

/**
 * This class is responsable of parsing incoming data.
 * Keeps the resulting XML elements and the input buffer.
 * @short An XML parser.
 */
class YJINGLE_API XMLParser : public TiXmlDocument, public Mutex
{
public:
    /**
     * Constructor.
     * Constructs an XML parser.
     */
    inline XMLParser()
	: TiXmlDocument(), Mutex(true), m_findstart(true)
	{}

    /**
     * Destructor.
     */
    virtual ~XMLParser()
	{}

    /**
     * Add data to buffer. Parse the buffer.
     * On success, the already parsed data is removed from buffer.
     * This method is thread safe.
     * @param data Pointer to the data to consume.
     * @param len Data length.
     * @return True on successfully parsed.
     */
    bool consume(const char* data, u_int32_t len);

    /**
     * Extract the first XML element from document.
     * Remove non-element children of the document (e.g. declaration).
     * This method is thread safe.
     * @return Pointer to an XMLElement or 0 if the document is empty.
     */
    XMLElement* extract();

    /**
     * Get a copy of the parser's buffer.
     * @param dest Destination string.
     */
    inline void getBuffer(String& dest) const
	{ dest = m_buffer; }

    /**
     * Clear the parser's input buffer and already parsed elements. Reset data.
     */
    void reset();

    /**
     * The maximum allowed buffer length.
     */
    static u_int32_t s_maxDataBuffer;

    /**
     * The XML encoding.
     */
    static TiXmlEncoding s_xmlEncoding;

private:
    String m_buffer;                     // Input data buffer
    bool m_findstart;                    // Search for stream start tag or not
};

/**
 * This class holds an XML element to be sent through a stream.
 * @short An outgoing XML element.
 */
class YJINGLE_API XMLElementOut : public RefObject
{
public:
    /**
     * Constructor.
     * @param element The XML element.
     * @param senderID Optional sender id.
     */
    inline XMLElementOut(XMLElement* element, const char* senderID = 0)
	: m_element(element), m_offset(0), m_id(senderID)
	{}

    /**
     * Destructor.
     * Delete m_element if not 0.
     */
    virtual ~XMLElementOut() {
	    if (m_element)
		delete m_element;
	}

    /**
     * Get the underlying element.
     * @return The underlying element.
     */
    inline XMLElement* element() const
	{ return m_element; }

    /**
     * Get the data buffer.
     * @return The data buffer.
     */
    inline String& buffer()
	{ return m_buffer; }

    /**
     * Get the id member.
     * @return The id member.
     */
    inline const String& id() const
	{ return m_id; }

    /**
     * Get the remainig byte count to send.
     * @return The unsent number of bytes.
     */
    inline u_int32_t dataCount()
	{ return m_buffer.length() - m_offset; }

    /**
     * Get the remainig data to send. Set the buffer if not already set.
     * @param nCount The number of unsent bytes.
     * @return Pointer to the remaining data or 0.
     */
    inline const char* getData(u_int32_t& nCount) {
	    if (!m_buffer)
		prepareToSend();
	    nCount = dataCount();
	    return m_buffer.c_str() + m_offset;
	}

    /**
     * Increase the offset with nCount bytes.
     * @param nCount The number of bytes sent.
     */
    inline void dataSent(u_int32_t nCount) {
	    m_offset += nCount;
	    if (m_offset > m_buffer.length())
		m_offset = m_buffer.length();
	}

    /**
     * Release the ownership of m_element.
     * The caller is responsable of returned pointer.
     * @return XMLElement pointer or 0.
     */
    inline XMLElement* release() {
	    XMLElement* e = m_element;
	    m_element = 0;
	    return e;
	}

    /**
     * Fill a buffer with the XML element to send.
     * @param buffer The buffer to fill.
     */
    inline void toBuffer(String& buffer)
	{ if (m_element) m_element->toString(buffer); }

    /**
     * Fill the buffer with the XML element to send.
     */
    inline void prepareToSend()
	{ toBuffer(m_buffer); }

private:
    XMLElement* m_element;               // The XML element
    String m_buffer;                     // Data to send
    u_int32_t m_offset;                  // Offset to send
    String m_id;                         // Sender's id
};

};

#endif /* __XMLPARSER_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
