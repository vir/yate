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
 * This class holds an XML element
 * @short An XML element
 */
class YJINGLE_API XMLElement : public GenObject
{
    friend class XMLParser;
public:
    /**
     * Element type as enumeration
     */
    enum Type {
	// *** Stream related elements
	StreamStart,                     // stream:stream
	StreamEnd,                       // /stream:stream
	StreamError,                     // stream::error
	StreamFeatures,                  // stream::features
	Register,                        // register
	Starttls,                        // starttls
	Handshake,                       // handshake
	Auth,                            // auth
	Challenge,                       // challenge	
	Abort,                           // abort
	Aborted,                         // aborted
	Response,                        // response
	Proceed,                         // proceed
	Success,                         // success
	Failure,                         // failure 
	Mechanisms,                      // mechanisms
	Mechanism,                       // mechanism
	Session,                         // session
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
	Subject,                         // subject
	// Resources
	Feature,                         // feature
	Bind,                            // bind
	Resource,                        // resource
	// Miscellanous
	Registered,                      // registered
	Remove,                          // remove
	Jid,                             // jid
	Username,                        // username
	Password,                        // password
	Digest,                          // digest
	Required,                        // required
	Dtmf,                            // dtmf
	DtmfMethod,                      // dtmf-method
	Command,                         // command
	Text,                            // text
	Item,                            // item
	Group,                           // group
	Unknown,                         // Any text
	Invalid,                         // m_element is 0
    };

    /**
     * Constructor.
     * Constructs a StreamEnd element
     */
    XMLElement();

    /**
     * Copy constructor
     * @param src Source element
     */
    XMLElement(const XMLElement& src);

    /**
     * Constructor. Partially build this element from another one.
     * Copy name and 'to', 'from', 'type', 'id' attributes
     * @param src Source element
     * @param response True to reverse 'to' and 'from' attributes
     * @param result True to set type to "result", false to set it to "error".
     *  Ignored if response is false
     */
    XMLElement(const XMLElement& src, bool response, bool result);

    /**
     * Constructor.
     * Constructs an XML element with a TiXmlElement element with the given name.
     * Used for outgoing elements
     * @param name The element's name
     * @param attributes Optional list of attributes
     * @param text Optional text for the XML element
     */
    XMLElement(const char* name, NamedList* attributes = 0, const char* text = 0);

    /**
     * Constructor.
     * Constructs an XML element with a TiXmlElement element with the given type's name.
     * Used for outgoing elements
     * @param type The element's type
     * @param attributes Optional list of attributes
     * @param text Optional text for the XML element
     */
    XMLElement(Type type, NamedList* attributes = 0, const char* text = 0);

    /**
     * Constructor.
     * Build this XML element from a list containing name, attributes and text.
     * Element's name must be a parameter whose name must be equal to prefix.
     * Element's text must be a parameter whose name is prefix followed by a dot.
     * The list of attributes will be built from parameters starting with prefix.attributename
     * @param src The list containing data used to build this XML element
     * @param prefix The prefix used to search the list of parameters
     */
    XMLElement(NamedList& src, const char* prefix);

    /**
     * Destructor. Deletes the underlying TiXmlElement if owned
     */
    virtual ~XMLElement();

    /**
     * Get the type of this object
     * @return The type of this object as enumeration
     */
    inline Type type() const
	{ return m_type; }

    /**
     * Get the TiXmlElement's name
     * @return The name of the TiXmlElement object or 0
     */
    inline const char* name() const
	{ return valid() ? m_element->Value() : 0; }

    /**
     * Check if the TiXmlElement's name is the given text
     * @param text Text to compare with
     * @return False if text is 0 or not equal to name
     */
    inline bool nameIs(const char* text) const
	{ return (text && name() && (0 == ::strcmp(name(),text))); }

    /**
     * Get the validity of this object
     * @return True if m_element is non null
     */
    inline bool valid() const
	{ return m_element != 0; }

    /**
     * Change the type of this object
     * @param t The new type of this object
     */
    inline void changeType(Type t)
	{ m_type = t; }

    /**
     * Put the element in a buffer
     * @param dest Destination string
     * @param unclose True to leave the tag unclosed
     */
    void toString(String& dest, bool unclose = false) const;

    /**
     * Put this element's name, text and attributes to a list of parameters
     * @param dest Destination list
     * @param prefix Prefix to add to parameters
     */
    void toList(NamedList& dest, const char* prefix);

    /**
     * Set the value of an existing attribute or adds a new one
     * @param name Attribute's name
     * @param value Attribute's value
     */
    void setAttribute(const char* name, const char* value);

    /**
     * Set the value of an existing attribute or adds a new one if the value's length is not 0
     * @param name Attribute's name
     * @param value Attribute's value
     */
    inline void setAttributeValid(const char* name, const String& value) {
	    if (value)
		setAttribute(name,value);
	}

    /**
     * Set the value of an existing attribute or adds a new one from an integer
     * @param name Attribute's name
     * @param value Attribute's value
     */
    inline void setAttribute(const char* name, int value) {
	    String s(value);
	    setAttribute(name,s);
	}

    /**
     * Get the value of an attribute
     * @param name Attribute's name
     * @return Attribute's value. May be 0 if doesn't exists or empty
     */
    const char* getAttribute(const char* name) const;

    /**
     * Get the value of an attribute
     * @param name Attribute's name
     * @param value Destination string
     * @return True if attribute with the given name exists and is not empty
     */
    inline bool getAttribute(const char* name, String& value) const {
	    value = getAttribute(name);
	    return 0 != value.length();
	}

    /**
     * Check if an attribute with the given name and value exists
     * @param name Attribute's name
     * @param value Attribute's value
     * @return True/False
     */
    bool hasAttribute(const char* name, const char* value) const;

    /**
     * Get the text of this XML element
     * @return Pointer to the text of this XML element or 0
     */
    const char* getText() const;

    /**
     * Add a child to this object. Release the received element
     * @param element XMLElement to add
     */
    void addChild(XMLElement* element);

    /**
     * Find the first child element of this one.
     * If an element is returned, it is a newly allocated one, not owning its TiXmlElement pointer
     * @param name Optional name of the child
     * @return Pointer to an XMLElement or 0 if not found
     */
    XMLElement* findFirstChild(const char* name = 0);

    /**
     * Find the first child element of the given type.
     * If an element is returned, it is a newly allocated one, not owning its TiXmlElement pointer
     * @param type Child's type to find
     * @return Pointer to an XMLElement or 0 if not found
     */
    inline XMLElement* findFirstChild(Type type)
	{ return findFirstChild(typeName(type)); }

    /**
     * Check if this element has a given child
     * @param name Optional name of the child (check for the first one if 0)
     * @return True if this element has the desired child
     */
    inline bool hasChild(const char* name) {
	    XMLElement* tmp = findFirstChild(name);
	    bool ok = (0 != tmp);
	    TelEngine::destruct(tmp);
	    return ok;
	}

    /**
     * Check if this element has a given child
     * @param type Child's type to find
     * @return True if this element has the desired child
     */
    inline bool hasChild(Type type)
	{ return hasChild(typeName(type)); }

    /**
     * Find the next child element. Delete the starting element if not 0.
     * If an element is returned, it is a newly allocated one, not owning its TiXmlElement pointer
     * @param element Starting XMLElement. O to find from the beginning
     * @param name Optional name of the child
     * @return Pointer to an XMLElement or 0 if not found
     */
    XMLElement* findNextChild(XMLElement* element, const char* name = 0);

    /**
     * Find the next child element of the given type. Delete the starting element if not 0.
     * If an element is returned, it is a newly allocated one, not owning its TiXmlElement pointer
     * @param element Starting XMLElement. O to find from the beginning
     * @param type Child's type to find
     * @return Pointer to an XMLElement or 0 if not found
     */
    inline XMLElement* findNextChild(XMLElement* element, Type type)
	{ return findNextChild(element,typeName(type)); }

    /**
     * Find the first attribute
     * @return Pointer to the first attribute or 0
     */
    inline const TiXmlAttribute* firstAttribute() const
	{ return valid() ? m_element->FirstAttribute() : 0; }

    /**
     * Get the name associated with the given type
     * @param type Element type as enumeration
     * @return Pointer to the name or 0
     */
    static inline const char* typeName(Type type)
	{ return lookup(type,s_names); }

    /**
     * Check if the given text is equal to the one associated with the given type
     * @param txt Text to compare
     * @param type Element type as enumeration
     * @return True if txt equals the text associated with the given type
     */
    static inline bool isType(const char* txt, Type type) {
	    const char* s = typeName(type);
	    return (txt && s && (0 == ::strcmp(txt,s)));
	}

    /**
     * Get a pointer to this object
     */
    virtual void* getObject(const String& name) const {
	    if (name == "XMLElement")
		return (void*)this;
	    return GenObject::getObject(name);
	}

    /**
     * Release memory
     */
    virtual const String& toString() const
	{ return m_name; }

    /**
     * Release memory
     */
    virtual void destruct() {
	    if (m_owner && m_element)
		delete m_element;
	    m_element = 0;
	    GenObject::destruct();
	}

    /**
     * Get an xml element from a list's parameter
     * @param list The list to be searched for the given parameter
     * @param stole True to release parameter ownership (defaults to false)
     * @param name Parameter name (defaults to 'xml')
     * @param value Optional parameter value to check
     * @return XMLElement pointer or 0. If a valid pointer is returned and
     *  stole is true the caller will own the pointer
     */
    static XMLElement* getXml(NamedList& list, bool stole = false,
	const char* name = "xml", const char* value = 0);

protected:
    /**
     * Constructor.
     * Constructs an XML element from a TiXmlElement.
     * Used to extract elements from parser and access the children.
     * When extracting elements from parser the object will own the TiXmlElement.
     * When accessing the children, the object will not own the TiXmlElement
     * @param element Pointer to a valid TiXmlElement
     * @param owner Owner flag
     */
    XMLElement(TiXmlElement* element, bool owner);

    /**
     * Get the underlying TiXmlElement
     * @return The underlying TiXmlElement object or 0
     */
    inline TiXmlElement* get() const
	{ return m_element; }

    /**
     * Release the ownership of the underlying TiXmlElement
     *  and returns it if the object owns it
     * @return The underlying TiXmlElement object or 0 if not owned or 0
     */
    TiXmlElement* releaseOwnership();

    /**
     * Associations between XML element name and type
     */
    static TokenDict s_names[];

private:
    // Set this object's type from m_element's name
    inline void setType() {
	    m_name = name();
	    m_type = (Type)lookup(name(),s_names,Unknown);
	}

    Type m_type;                         // Element's type
    bool m_owner;                        // Owner flag. If true, this object owns the XML element
    String m_name;                       // The name of this element
    TiXmlElement* m_element;             // The underlying XML element
};

/**
 * This class is responsable of parsing incoming data.
 * Keeps the resulting XML elements and the input buffer
 * @short An XML parser
 */
class YJINGLE_API XMLParser : public TiXmlDocument, public Mutex
{
public:
    /**
     * Constructor.
     * Constructs an XML parser
     */
    inline XMLParser()
	: TiXmlDocument(), Mutex(true), m_findstart(true)
	{}

    /**
     * Destructor
     */
    virtual ~XMLParser()
	{}

    /**
     * Add data to buffer. Parse the buffer.
     * On success, the already parsed data is removed from buffer.
     * This method is thread safe
     * @param data Pointer to the data to consume
     * @param len Data length
     * @return True on successfully parsed
     */
    bool consume(const char* data, u_int32_t len);

    /**
     * Extract the first XML element from document.
     * Remove non-element children of the document (e.g. declaration).
     * This method is thread safe
     * @return Pointer to an XMLElement or 0 if the document is empty
     */
    XMLElement* extract();

    /**
     * Get the buffer length (incomplete data)
     * @return The number of bytes belonging to an incomplete XML element
     */
    inline unsigned int bufLen() const
	{ return m_buffer.length(); }

    /**
     * Get a copy of the parser's buffer
     * @param dest Destination string
     */
    inline void getBuffer(String& dest) const
	{ dest = m_buffer; }

    /**
     * Clear the parser's input buffer and already parsed elements. Reset data
     */
    void reset();

    /**
     * The maximum allowed buffer length
     */
    static u_int32_t s_maxDataBuffer;

    /**
     * The XML encoding
     */
    static TiXmlEncoding s_xmlEncoding;

private:
    String m_buffer;                     // Input data buffer
    bool m_findstart;                    // Search for stream start tag or not
};

/**
 * This class holds an XML element to be sent through a stream
 * @short An outgoing XML element
 */
class YJINGLE_API XMLElementOut : public RefObject
{
public:
    /**
     * Constructor
     * @param element The XML element
     * @param senderID Optional sender id
     * @param unclose True to not close the tag when building the buffer
     */
    inline XMLElementOut(XMLElement* element, const char* senderID = 0,
	bool unclose = false)
	: m_element(element), m_offset(0), m_id(senderID), m_unclose(unclose),
	m_sent(false)
	{}

    /**
     * Destructor
     * Delete m_element if not 0
     */
    virtual ~XMLElementOut()
	{ TelEngine::destruct(m_element); }

    /**
     * Get the underlying element
     * @return The underlying element
     */
    inline XMLElement* element() const
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
    inline String& buffer()
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
    inline u_int32_t dataCount()
	{ return m_buffer.length() - m_offset; }

    /**
     * Get the remainig data to send. Set the buffer if not already set
     * @param nCount The number of unsent bytes
     * @return Pointer to the remaining data or 0
     */
    inline const char* getData(u_int32_t& nCount) {
	    if (!m_buffer)
		prepareToSend();
	    nCount = dataCount();
	    return m_buffer.c_str() + m_offset;
	}

    /**
     * Increase the offset with nCount bytes. Set the sent flag
     * @param nCount The number of bytes sent
     */
    inline void dataSent(u_int32_t nCount) {
	    m_sent = true;
	    m_offset += nCount;
	    if (m_offset > m_buffer.length())
		m_offset = m_buffer.length();
	}

    /**
     * Release the ownership of m_element
     * The caller is responsable of returned pointer
     * @return XMLElement pointer or 0
     */
    inline XMLElement* release() {
	    XMLElement* e = m_element;
	    m_element = 0;
	    return e;
	}

    /**
     * Fill a buffer with the XML element to send
     * @param buffer The buffer to fill
     */
    inline void toBuffer(String& buffer)
	{ if (m_element) m_element->toString(buffer,m_unclose); }

    /**
     * Fill the buffer with the XML element to send
     */
    inline void prepareToSend()
	{ toBuffer(m_buffer); }

private:
    XMLElement* m_element;               // The XML element
    String m_buffer;                     // Data to send
    u_int32_t m_offset;                  // Offset to send
    String m_id;                         // Sender's id
    bool m_unclose;                      // Close or not the element's tag
    bool m_sent;                         // Sent flag (true if an attempt to send was done)
};

};

#endif /* __XMLPARSER_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
