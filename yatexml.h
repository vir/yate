/**
 * yatexml.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * XML Parser and support classes
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

#ifndef __YATEXML_H
#define __YATEXML_H

#ifndef __cplusplus
#error C++ is required
#endif

#include <yateclass.h>

/**
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

class XmlSaxParser;
class XmlDomParser;
class XmlDeclaration;
class XmlFragment;
class XmlChild;
class XmlParent;
class XmlDocument;
class XmlElement;
class XmlComment;
class XmlCData;
class XmlText;
class XmlDoctype;


struct YATE_API XmlEscape {
    /**
     * Value to match
     */
    const char* value;

    /**
     * Character replacement for value
     */
    char replace;
};

/**
 * A Serial Access Parser (SAX) for arbitrary XML data
 * @short Serial Access XML Parser
 */
class YATE_API XmlSaxParser : public DebugEnabler
{
public:
    enum Error {
	NoError = 0,
	NotWellFormed,
	Unknown,
	IOError,
	ElementParse,
	ReadElementName,
	InvalidElementName,
	ReadingAttributes,
	CommentParse,
	DeclarationParse,
	DefinitionParse,
	CDataParse,
	ReadingEndTag,
	Incomplete,
	InvalidEncoding,
	UnsupportedEncoding,
	UnsupportedVersion,
    };
    enum Type {
	None           = 0,
	Text           = 1,
	CData          = 2,
	Element        = 3,
	Doctype        = 4,
	Comment        = 5,
	Declaration    = 6,
	Instruction    = 7,
	EndTag         = 8,
	Special        = 9
    };

    /**
     * Destructor
     */
    virtual ~XmlSaxParser();

    /**
     * Get the number of bytes successfully parsed
     * @return The number of bytes successfully parsed
     */
    inline unsigned int offset() const
	{ return m_offset; }

    /**
     * Get the row where the parser has found an error
     * @return The row number
     */
    inline unsigned int row() const
	{ return m_row; }

    /**
     * Get the column where the parser has found an error
     * @return The column number
     */
    inline unsigned int column() const
	{ return m_column; }

    /**
     * Retrieve the parser's buffer
     * @return The parser's buffer
     */
    inline const String& buffer() const
	{ return m_buf; }

    /**
     * Parse a given string
     * @param data The data to parse
     * @return True if all data was successfully parsed
     */
    bool parse(const char* data);

    /**
     * Process incomplete text if the parser is completed.
     * This method should be called to complete text after
     *  all data was pushed into the parser
     * @return True if all data was successfully parsed
     */
    bool completeText();

    /**
     * Get the error code found while parsing
     * @return Error code
     */
    inline Error error()
	{ return m_error; }

    /**
     * Set the error code and destroys a child if error code is not NoError
     * @param error The error found
     * @param child Child to destroy
     * @return False on error
     */
    bool setError(Error error, XmlChild* child = 0);

    /**
     * Retrieve the error string associated with current error status
     * @param defVal Value to return if not found
     * @return The error string
     */
    inline const char* getError(const char* defVal = "Xml error")
	{ return getError(m_error,defVal); }

    /**
     * @return The last xml type that we were parsing, but we have not finished
     */
    inline Type unparsed()
	{ return m_unparsed; }

    /**
     * Set the last xml type that we were parsing, but we have not finished
     * @param id The xml type that we haven't finish to parse
     */
    inline void setUnparsed(Type id)
	{ m_unparsed = id;}

    /**
     * Reset error flag
     */
    virtual void reset();

    /**
     * @return The internal buffer
     */
    const String& getBuffer() const
	{ return m_buf; }

    /**
     * Retrieve the error string associated with a given error code
     * @param code Code of the error to look up
     * @param defVal Value to return if not found
     * @return The error string
     */
    static inline const char* getError(int code, const char* defVal = "Xml error")
	{ return lookup(code,s_errorString,defVal); }

    /**
     * Check if the given character is blank
     * @param c The character to verify
     * @return True if c is blank
     */
    static inline bool blank(char c)
	{ return (c == 0x20) || (c == 0x09) || (c == 0x0d) || (c == 0x0a); }

    /**
     * Verify if the given character is in the range allowed
     * to be first character from a xml tag
     * @param ch The character to check
     * @return True if the character is in range
     */
    static bool checkFirstNameCharacter(unsigned char ch);

    /**
     * Check if the given character is in the range allowed for an xml char
     * @param c The character to check
     * @return True if the character is in range
     */
    static bool checkDataChar(unsigned char c);

    /**
     * Verify if the given character is in the range allowed for a xml name
     * @param ch The character to check
     * @return True if the character is in range
     */
    static bool checkNameCharacter(unsigned char ch);

    /**
     * Check if a given string is a valid xml tag name
     * @param buf The string to check
     * @return True if the string is a valid xml tag name
     */
    static bool validTag(const String& buf);

    /**
     * XmlEscape the given text
     * @param buf Destination buffer
     * @param text The text to escape
     */
    static void escape(String& buf, const String& text);

    /**
     * Errors dictionary
     */
    static const TokenDict s_errorString[];

    /**
     * Escaped strings dictionary
     */
    static const XmlEscape s_escape[];

protected:
    /**
     * Constructor
     * @param name Debug name
     */
    XmlSaxParser(const char* name = "XmlSaxParser");

    /**
     * Parse an instruction form the main buffer.
     * Extracts the parsed string from buffer if returns true
     * @return True if the instruction was parsed successfully
     */
    bool parseInstruction();

    /**
     * Parse a CData section form the main buffer.
     * Extracts the parsed string from buffer if returns true
     * @return True if the CData section was parsed successfully
     */
    bool parseCData();

    /**
     * Parse a comment form the main buffer.
     * Extracts the parsed string from buffer if returns true
     * @return True if the comment was parsed successfully
     */
    bool parseComment();

    /**
     * Parse an element form the main buffer.
     * Extracts the parsed string from buffer if returns true
     * @return True if the element was parsed successfully
     */
    bool parseElement();

    /**
     * Parse a declaration form the main buffer.
     * Extracts the parsed string from buffer if returns true
     * @return True if the declaration was parsed successfully
     */
    bool parseDeclaration();

    /**
     * Helper method to classify the Xml objects starting with "<!" sequence.
     * Extracts the parsed string from buffer if returns true
     * @return True if a corresponding xml object was found and parsed successfully
     */
    bool parseSpecial();

    /**
     * Parse an endtag form the main buffer.
     * Extracts the parsed string from buffer if returns true
     * @return True if the endtag was parsed successfully
     */
    bool parseEndTag();

    /**
     * Parse a doctype form the main buffer.
     * Extracts the parsed string from buffer if returns true.
     * Warning: This is a stub implementation
     * @return True if the doctype was parsed successfully
     */
    bool parseDoctype();

    /**
     * Parse an unfinished xml object.
     * Extracts the parsed string from buffer if returns true
     * @return True if the object was parsed successfully
     */
    bool auxParse();

    /**
     * Unescape the given text.
     * Handled: &amp;lt; &amp;gt; &amp;apos; &amp;quot; &amp;amp;
     *  &amp;\#DecimalNumber; &amp;\#xHexNumber;
     * @param text The requested text to unescape
     */
    void unEscape(String& text);

    /**
     * Remove blank characters from the begining of the buffer
     */
    void skipBlanks();

    /**
     * Check if a character is an angle bracket
     * @param c The character to verify
     * @return True if c is an angle bracket
     */
    inline bool badCharacter(char c)
	{ return c == '<' || c == '>'; }

    /**
     * Reset the error
     */
    inline void resetError()
	{ m_error = NoError; }

    /**
     * Reset parsed value and parameters
     */
    inline void resetParsed()
	{ m_parsed.clear(); m_parsed.clearParams(); }

    /**
     * Extract the name of an element or instruction
     * @return The extracted string or 0
     */
    String* extractName(bool& empty);

    /**
     * Extract an attribute
     * @return The attribute value or 0
     */
    NamedString* getAttribute();

    /**
     * Callback method. Is called when a comment was successfully parsed.
     * Default implementation does nothing
     * @param text The comment content
     */
    virtual void gotComment(const String& text)
	{ }

    /**
     * Callback method. Is called when an instruction was successfully parsed.
     * Default implementation does nothing
     * @param instr The instruction content
     */
    virtual void gotProcessing(const NamedString& instr)
	{ }

    /**
     * Callback method. Is called when a declaration was successfully parsed.
     * Default implementation does nothing
     * @param decl The declaration content
     */
    virtual void gotDeclaration(const NamedList& decl)
	{ }

    /**
     * Callback method. Is called when a text was successfully parsed.
     * Default implementation does nothing
     * @param text The text content
     */
    virtual void gotText(const String& text)
	{ }

    /**
     * Callback method. Is called when a CData section was successfully parsed.
     * Default implementation does nothing
     * @param data The CData content
     */
    virtual void gotCdata(const String& data)
	{ }

    /**
     * Callback method. Is called when an element was successfully parsed.
     * Default implementation does nothing
     * @param element The element content
     * @param empty True if the element does not have attributes
     */
    virtual void gotElement(const NamedList& element, bool empty)
	{ }

    /**
     * Callback method. Is called when a end tag was successfully parsed.
     * Default implementation does nothing
     * @param name The end tag name
     */
    virtual void endElement(const String& name)
	{ }

    /**
     * Callback method. Is called when a doctype was successfully parsed.
     * Default implementation does nothing
     * @param doc The doctype content
     */
    virtual void gotDoctype(const String& doc)
	{ }

    /**
     * Callback method. Is called to check if we have an incomplete element.
     * Default implementation returns always true
     * @return True
     */
    virtual bool completed()
	{ return true; }

    /**
     * Calls gotElement() and eset parsed on success
     * @param list The list element and its attributes
     * @param empty True if the element does not have attributes
     * @return True if there is no error
     */
    bool processElement(NamedList& list, bool empty);

    /**
     * Unescape text, call gotText() and reset parsed on success
     * @param text The text to process
     * @return True if there is no error
     */
    bool processText(String& text);

    /**
     * The offset where the parser was stop
     */
    unsigned int m_offset;

    /**
     * The row where the parser was stop
     */
    unsigned int m_row;

    /**
     * The column where the parser was stop
     */
    unsigned int m_column;

    /**
     * The error code found while parsing data
     */
    Error m_error;

    /**
     * The main buffer
     */
    String m_buf;

    /**
     * The parser data holder.
     * Keeps the parsed data when an incomplete xml object is found
     */
    NamedList m_parsed;

    /**
     * The last parsed xml object code
     */
    Type m_unparsed;
};

/**
 * Xml Parent for a Xml child
 * @short Xml Parent
 */
class YATE_API XmlParent
{
public:
    /**
     * Constructor
     */
    XmlParent()
	{ }

    /**
     * Destructor
     */
    virtual ~XmlParent()
	{ }

    /**
     * Get an XmlDocument object from this XmlParent.
     * Default implementation return 0
     * @return 0
     */
    virtual XmlDocument* document()
	{ return 0; }

    /**
     * Get an XmlFragment object from this XmlParent.
     * Default implementation return 0
     * @return 0
     */
    virtual XmlFragment* fragment()
	{ return 0; }

    /**
     * Get an XmlElement object from this XmlParent.
     * Default implementation return 0
     * @return 0
     */
    virtual XmlElement* element()
	{ return 0; }

    /**
     * Append a new child to this XmlParent
     * @param child The child to append
     * @return NoError if the child was successfully added
     */
    virtual XmlSaxParser::Error addChild(XmlChild* child) = 0;

    /**
     * Append a new child of this XmlParent, release the object on failure
     * @param child The child to append
     * @return The child on success, 0 on failure
     */
    inline XmlChild* addChildSafe(XmlChild* child) {
	    XmlSaxParser::Error err = addChild(child);
	    if (err != XmlSaxParser::NoError)
		TelEngine::destruct(child);
	    return child;
	}

    /**
     * Remove a child
     * @param child The child to remove
     * @param delObj True to delete the object
     * @return XmlChild pointer if found and not deleted
     */
    virtual XmlChild* removeChild(XmlChild* child, bool delObj = true) = 0;

    /**
     * Reset this xml parent.
     * Default implementation does nothing
     */
    virtual void reset()
	{ }

    /**
     * Obtain this xml parent children.
     * Default implementation returns an empty list
     * @return The list of children
     */
    virtual const ObjList& getChildren() const
	{ return ObjList::empty(); }

    /**
     * Clear this xml parent children.
     * Default implementation does nothing
     */
    virtual void clearChildren()
	{  }

    /**
     * Check if at least one child element exists
     * @return True if this parent has at least one child
     */
    inline bool hasChildren() const
	{ return getChildren().skipNull() != 0; }
};

/**
 * A Document Object Model (DOM) parser for XML documents and fragments
 * @short Document Object Model XML Parser
 */
class YATE_API XmlDomParser : public XmlSaxParser
{
    friend class XmlChild;
public:
    /**
     * XmlDomParser constructor
     * @param name Debug name
     * @param fragment True if this parser needs to parse a piece of a xml document
     */
    XmlDomParser(const char* name = "XmlDomParser", bool fragment = false);

    /**
     * XmlDomParser constructor
     * @param fragment The fragment who should keep the parsed data
     * @param takeOwnership True to take ownership of the fragment
     */
    XmlDomParser(XmlParent* fragment, bool takeOwnership);

    /**
     * Destructor
     */
    virtual ~XmlDomParser();

    /**
     * Obtain an XmlDocument from the parsed data
     * @return The XmlDocument or 0
     */
    XmlDocument* document()
	{ return m_data->document(); }

    /**
     * Obtain an XmlFragment from the parsed data
     * @return The XmlFragment or 0
     */
    XmlFragment* fragment()
	{ return m_data->fragment(); }

    /**
     * Reset parser
     */
    virtual void reset();

    /**
     * Check if the current element is the given one
     * @param el The element to compare with
     * @return True if they are equal
     */
    inline bool isCurrent(const XmlElement* el) const
	{ return el == m_current; }

protected:

    /**
     * Append a xml comment in the xml tree
     * @param text The comment content
     */
    virtual void gotComment(const String& text);

    /**
     * Append a xml instruction in the xml tree
     * @param instr The instruction content
     */
    virtual void gotProcessing(const NamedString& instr);

    /**
     * Append a xml declaration in the xml tree
     * @param decl The declaration content
     */
    virtual void gotDeclaration(const NamedList& decl);

    /**
     * Append a xml text in the xml tree
     * @param text The text content
     */
    virtual void gotText(const String& text);

    /**
     * Append a xml CData in the xml tree
     * @param data The CData content
     */
    virtual void gotCdata(const String& data);

    /**
     * Append a xml element in the xml tree
     * @param element The element content
     * @param empty True if the element does not have attributes
     */
    virtual void gotElement(const NamedList& element, bool empty);

    /**
     * Complete current element
     * @param name The end tag name
     */
    virtual void endElement(const String& name);

    /**
     * Append a xml doctype in the xml tree
     * @param doc The doctype content
     */
    virtual void gotDoctype(const String& doc);

    /**
     * Callback method. Is called to check if we have an incomplete element
     * @return True if current element is not 0
     */
    virtual bool completed()
	{ return m_current == 0; }

private:
    XmlElement* m_current;                   // The current xml element
    XmlParent* m_data;                       // Main xml fragment
    bool m_ownData;                          // The DOM owns data
};

/**
 * Xml Child for Xml document
 * @short Xml Child
 */
class YATE_API XmlChild : public GenObject
{
    YCLASS(XmlChild,GenObject)
    friend class XmlDomParser;
public:
    /**
     * Constructor
     */
    XmlChild();

    /**
     * Set this child's parent
     * @param parent Parent of this child
     */
    virtual void setParent(XmlParent* parent)
	{ }

    /**
     * Get a Xml element
     * @return 0
     */
    virtual XmlElement* xmlElement()
	{ return 0; }

    /**
     * Get a Xml comment
     * @return 0
     */
    virtual XmlComment* xmlComment()
	{ return 0; }

    /**
     * Get a Xml CData
     * @return 0
     */
    virtual XmlCData* xmlCData()
	{ return 0; }

    /**
     * Get a Xml text
     * @return 0
     */
    virtual XmlText* xmlText()
	{ return 0; }

    /**
     * Get a Xml declaration
     * @return 0
     */
    virtual XmlDeclaration* xmlDeclaration()
	{ return 0; }

    /**
     * Get a Xml doctype
     * @return 0
     */
    virtual XmlDoctype* xmlDoctype()
	{ return 0; }
};


/**
 * Xml Declaration for Xml document
 * @short Xml Declaration
 */
class YATE_API XmlDeclaration : public XmlChild
{
    YCLASS(XmlDeclaration,XmlChild)
public:
    /**
     * Constructor
     * @param version XML version attribute
     * @param enc Encoding attribute
     */
    XmlDeclaration(const char* version = "1.0", const char* enc = "utf-8");

    /**
     * Constructor
     * @param decl Declaration attributes
     */
    XmlDeclaration(const NamedList& decl);

    /**
     * Copy constructor
     * @param orig Original XmlDeclaration
     */
    XmlDeclaration(const XmlDeclaration& orig);

    /**
     * Destructor
     */
    ~XmlDeclaration();

    /**
     * Obtain the tag name and attributes list
     * @return The declaration
     */
    inline const NamedList& getDec() const
	{ return m_declaration; }

    /**
     * Get the Xml declaration
     * @return This object
     * Reimplemented from XmlChild
     */
    virtual XmlDeclaration* xmlDeclaration()
	{ return this; }

    /**
     * Build a String from this XmlDeclaration
     * @param dump The string where to append representation
     * @param escape True if the attributes values need to be escaped
     */
    void toString(String& dump, bool escape = true) const;

private:
    NamedList m_declaration;                 // The declaration
};

/**
 * Xml Fragment a fragment from a Xml document
 * @short Xml Fragment
 */
class YATE_API XmlFragment : public XmlParent
{
public:

    /**
     * Constructor
     */
    XmlFragment();

    /**
     * Copy constructor
     * @param orig Original XmlFragment
     */
    XmlFragment(const XmlFragment& orig);

    /**
     * Destructor
     */
    virtual ~XmlFragment();

    /**
     * Get an Xml Fragment
     * @return This
     */
    virtual XmlFragment* fragment()
	{ return this; }

    /**
     * Get the list of children
     * @return The children list
     */
    virtual const ObjList& getChildren() const
	{ return m_list; }

    /**
     * Append a new xml child to this fragment
     * @param child the child to append
     * @return An error code if an error was detected
     */
    virtual XmlSaxParser::Error addChild(XmlChild* child);

    /**
     * Reset this Xml Fragment
     */
    virtual void reset();

    /**
     * Remove the first child from list and returns it
     * @return XmlChild pointer or 0
     */
    inline XmlChild* pop()
	{ return static_cast<XmlChild*>(m_list.remove(false)); }

    /**
     * Remove the first XmlElement from list and returns it if completed
     * @return XmlElement pointer or 0 if no XmlElement is found or the first one is not completed
     */
    XmlElement* popElement();

    /**
     * Remove a child. Reset the parent of not deleted xml element
     * @param child The child to remove
     * @param delObj True to delete the object
     * @return XmlChild pointer if found and not deleted
     */
    virtual XmlChild* removeChild(XmlChild* child, bool delObj = true);

    /**
     * Clear the list of children
     */
    virtual void clearChildren()
	{ m_list.clear(); }

    /**
     * Build a String from this XmlFragment
     * @param dump The string where to append representation
     * @param escape True if the attributes values need to be escaped
     * @param indent Spaces for output
     * @param origIndent Original indent
     * @param completeOnly True to build only if complete
     * @param auth Optional list of tag and attribute names to be replaced with '***'. This
     *  parameter can be used when the result will be printed to output to avoid printing
     *  authentication data to output. The array must end with an empty string
     * @param parent Optional parent element whose tag will be searched in the auth list
     */
    void toString(String& dump, bool escape = true, const String& indent = String::empty(),
	const String& origIndent = String::empty(), bool completeOnly = true,
	const String* auth = 0, const XmlElement* parent = 0) const;

    /**
     * Find a completed xml element in a list
     * @param list The list to search for the element
     * @param name Optional element tag to match
     * @param ns Optional element namespace to match
     * @param noPrefix True to compare the tag without namespace prefix, false to
     *  include namespace prefix when comparing the given tag.
     *  This parameter is ignored if name is 0 or ns is not 0
     * @return XmlElement pointer or 0 if not found
     */
    static XmlElement* findElement(ObjList* list, const String* name, const String* ns,
	bool noPrefix = true);

private:
    ObjList m_list;                    // The children list
};

/**
 * Xml Document
 * @short Xml Document
 */
class YATE_API XmlDocument : public XmlParent
{
public:

    /**
     * The Constructor
     */
    XmlDocument();

    /**
     * Destructor
     */
    virtual ~XmlDocument();

    /**
     * Get an Xml Document
     * @return This
     */
    virtual XmlDocument* document()
	{ return this; }

    /**
     * Append a new child to this document.
     * Set the root to an XML element if not already set. If we already have a completed root
     *  the element will be added to the root, otherwise an error will be returned.
     * If we don't have a root non xml elements (other then text) will be added the list
     *  of elements before root
     * @param child The child to append
     * @return An error code if an error was detected
     */
    virtual XmlSaxParser::Error addChild(XmlChild* child);

    /**
     * Retrieve the document declaration
     * @return XmlDeclaration pointer or 0 if not found
     */
    XmlDeclaration* declaration() const;

    /**
     * Retrieve the root element
     * @param completed True to retrieve the root element if is not completed
     * @return Root pointer or 0 if not found or is not completed
     */
    XmlElement* root(bool completed = false) const;

    /**
     * Take the root element from the document
     * @param completed True to retrieve the root element if is not completed
     * @return Root pointer or 0 if not found or is not completed
     */
    inline XmlElement* takeRoot(bool completed = false)
    {
	XmlElement* r = root(completed);
	if (r)
	    m_root = 0;
	return r;
    }

    /**
     * Reset this Xml Document
     */
    virtual void reset();

    /**
     * Remove a child
     * @param child The child to remove
     * @param delObj True to delete the object
     * @return XmlChild pointer if found and not deleted
     */
    virtual XmlChild* removeChild(XmlChild* child, bool delObj = true)
	{ return m_beforeRoot.removeChild(child,delObj); }

    /**
     * Load this document from data stream and parse it.
     * @param in The input stream
     * @param error Optional pointer to data to be filled with error if IOError is returned
     * @return Parser error (NoError on success)
     */
    virtual XmlSaxParser::Error read(Stream& in, int* error = 0);

    /**
     * Write this document to a data stream.
     * A indent + n * origIndent will be added before each xml child,
     *  where n is the imbrication level, starting with 0.
     * A indent + (n + 1) * origIndent will be added before each attribute
     * @param out The output stream
     * @param escape True if the attributes values need to be escaped
     * @param indent Line indent
     * @param origIndent Original indent
     * @param completeOnly True to build only if complete
     * @return Written bytes, negative on error
     */
    virtual int write(Stream& out, bool escape = true,
	const String& indent = String::empty(), const String& origIndent = String::empty(),
	bool completeOnly = true) const;

    /**
     * Load a file an parse it
     * Reset the document
     * @param file The file to load
     * @param error Pointer to data to be filled with file error if IOError is returned
     * @return Parser error (NoError on success)
     */
    XmlSaxParser::Error loadFile(const char* file, int* error = 0);

    /**
     * Save this xml document in the specified file. Create a new fle if not found.
     * Truncate an existing one
     * @param file The file to save or will be used the file used on load
     * @param escape True if the attributes values need to be escaped
     * @param indent Spaces for output
     * @param completeOnly True to build only if complete
     * @return 0 on success, error code on failure
     */
    int saveFile(const char* file = 0, bool escape = true,
	const String& indent = String::empty(), bool completeOnly = true) const;

    /**
     * Build a String from this XmlDocument
     * @param dump The string where to append representation
     * @param escape True if the attributes values need to be escaped
     * @param indent Spaces for output
     * @param origIndent Original indent
     */
    void toString(String& dump, bool escape = true, const String& indent = String::empty(),
	const String& origIndent = String::empty()) const;

private:
    XmlElement* m_root;                  // The root element
    XmlFragment m_beforeRoot;            // XML children before root (declaration ...)
    String m_file;                       // The file name used on load
    XmlFragment m_afterRoot;             // XML children after root (comments, empty text)
};


/**
 * Xml Element from a Xml document
 * @short Xml Element
 */

class YATE_API XmlElement : public XmlChild, public XmlParent
{
    YCLASS(XmlElement,XmlChild)
public:
    /**
     * Constructor
     * @param element The NamedList name represent the element name and the param the attributes
     * @param empty False if has children
     * @param parent The parent of this element
     */
    XmlElement(const NamedList& element, bool empty, XmlParent* parent = 0);

    /**
     * Constructor. Creates a new complete and empty element
     * @param name The name of the element
     * @param complete False to build an incomplete element
     */
    XmlElement(const char* name, bool complete = true);

    /**
     * Constructor. Create a new element with a text child
     * @param name The name of the element
     * @param value Element text child value
     * @param complete False to build an incomplete element
     */
    XmlElement(const char* name, const char* value, bool complete = true);

    /**
     * Copy constructor
     * @param orig Original XmlElement
     */
    XmlElement(const XmlElement& orig);

    /**
     * Destructor
     */
    virtual ~XmlElement();

    /**
     * Retrieve the element's tag
     * @return The element's tag
     */
    inline const char* tag() const
	{ return m_element; }

    /**
     * Check if this element must be processed in the default namespace (its tag is not prefixed)
     * @return True if this element must be processed in the default namespace
     */
    inline bool isDefaultNs() const
	{ return m_prefixed == 0; }

    /**
     * Retrieve the element's tag unprefixed (namespace prefix removed)
     * @return The element's tag unprefixed
     */
    inline const String& unprefixedTag() const
	{ return m_prefixed ? m_prefixed->name() : static_cast<const String&>(m_element); }

    /**
     * Set element's unprefixed tag, don't change namespace prefix
     * @param s New element's tag
     */
    void setUnprefixedTag(const String& s);

    /**
     * Retrieve the element's tag (without prefix)
     * @return Element tag
     */
    inline const String& getTag() const
	{ return m_prefixed ? m_prefixed->name() : static_cast<const String&>(m_element); }

    /**
     * Retrieve the element's tag (without prefix) and namespace
     * @param tag Pointer to element tag
     * @param ns Pointer to element's namespace (may be 0 for unprefixed tags)
     * @return True if a namespace was found for the element tag or the tag is not prefixed
     */
    bool getTag(const String*& tag, const String*& ns) const;

    /**
     * Get an XmlElement from this XmlChild
     * @return This object
     */
    virtual XmlElement* xmlElement()
	{ return this; }

    /**
     * Get an XmlElement from this XmlParent
     * @return This object
     */
    virtual XmlElement* element()
	{ return this; }

    /**
     * Append a new child of this element
     * @param child The child to append
     */
    virtual XmlSaxParser::Error addChild(XmlChild* child);

    /**
     * Remove a child
     * @param child The child to remove
     * @param delObj True to delete the object
     * @return XmlChild pointer if found and not deleted
     */
    virtual XmlChild* removeChild(XmlChild* child, bool delObj = true);

    /**
     * Notification for this element that is complete
     */
    virtual void setCompleted()
	{ m_complete = true; }

    /**
     * @return True if this element is completed
     */
    inline bool completed() const
	{ return m_complete; }

    /**
     * @return True if this element is empty
     */
    inline bool empty() const
	{ return m_empty; }

    /**
     * Retrieve an XmlElement parent of this one
     * @return XmlElement pointer or 0
     */
    inline XmlElement* parent() const
	{ return m_parent ? m_parent->element() : 0; }

    /**
     * @return The parent of this element
     */
    virtual XmlParent* getParent()
	{ return m_parent; }

    /**
     * Set this element's parent. Update inherited namespaces
     * @return The parent of this element
     */
    virtual void setParent(XmlParent* parent);

    /**
     * @return The name of this element
     */
    virtual const String& getName() const
	{ return m_element; }

    /**
     * @return The held element
     */
    virtual const NamedList& getElement() const
	{ return m_element; }

    /**
     * Helper method to obtain the children list
     * @return The children list
     */
    inline const ObjList& getChildren() const
	{ return m_children.getChildren(); }

    /**
     * Helper method to clear the children list
     */
    inline void clearChildren()
	{ return m_children.clearChildren(); }

    /**
     * Retrieve the list of inherited namespaces
     * @return The list of inherited namespaces (or 0)
     */
    inline const NamedList* inheritedNs() const
	{ return m_inheritedNs; }

    /**
     * Set inherited namespaces from a given element. Reset them anyway
     * @param xml The source element used to set inherited namespaces
     * @param inherit Copy element's inherited namespaces if it doesn't have a parent
     */
    void setInheritedNs(const XmlElement* xml = 0, bool inherit = true);

    /**
     * Add inherited namespaces from a list
     * @param list The list of namespaces
     */
    void addInheritedNs(const NamedList& list);

    /**
     * Extract the first child element
     * @return XmlElement pointer or 0
     */
    inline XmlElement* pop() {
	    XmlElement* x = findFirstChild();
	    if (!(x && x->completed()))
		return 0;
	    m_children.removeChild(x,false);
	    return x;
	}

    /**
     * Retrieve the element tag
     * @return The element tag
     */
    virtual const String& toString() const
	{ return m_element; }

    /**
     * Build (append to) a String from this XmlElement
     * @param dump The destination string
     * @param escape True if the attributes values need to be escaped
     * @param indent Spaces for output
     * @param origIndent Original indent
     * @param completeOnly True to build only if complete
     * @param auth Optional list of tag and attribute names to be replaced with '***'. This
     *  parameter can be used when the result will be printed to output to avoid printing
     *  authentication data to output. The array must end with an empty string
     */
    void toString(String& dump, bool escape = true, const String& indent = String::empty(),
	const String& origIndent = String::empty(), bool completeOnly = true,
	const String* auth = 0) const;

    /**
     * Find the first XmlElement child of this XmlElement
     * @param name Optional name of the child
     * @param ns Optional child namespace
     * @param noPrefix True to compare the tag without namespace prefix, false to
     *  include namespace prefix when comparing the given tag.
     *  This parameter is ignored if name is 0 or ns is not 0
     * @return The first child element meeting the requested conditions
     */
    inline XmlElement* findFirstChild(const String* name = 0, const String* ns = 0,
	bool noPrefix = true) const
	{ return XmlFragment::findElement(getChildren().skipNull(),name,ns,noPrefix); }

    /**
     * Find the first XmlElement child of this XmlElement
     * @param name Name of the child
     * @param ns Optional child namespace
     * @param noPrefix True to compare the tag without namespace prefix, false to
     *  include namespace prefix when comparing the given tag.
     *  This parameter is ignored if name is 0 or ns is not 0
     * @return The first child element meeting the requested conditions
     */
    inline XmlElement* findFirstChild(const String& name, const String* ns = 0,
	bool noPrefix = true) const
	{ return XmlFragment::findElement(getChildren().skipNull(),&name,ns,noPrefix); }

    /**
     * Finds next XmlElement child of this XmlElement
     * @param prev Previous child
     * @param name Optional name of the child
     * @param ns Optional child namespace
     * @param noPrefix True to compare the tag without namespace prefix, false to
     *  include namespace prefix when comparing the given tag.
     *  This parameter is ignored if name is 0 or ns is not 0
     * @return The next found child if prev exists else the first
     */
    inline XmlElement* findNextChild(const XmlElement* prev = 0, const String* name = 0,
	const String* ns = 0, bool noPrefix = true) const {
	    if (!prev)
		return findFirstChild(name,ns,noPrefix);
	    ObjList* start = getChildren().find(prev);
	    return start ? XmlFragment::findElement(start->skipNext(),name,ns,noPrefix) : 0;
	}

    /**
     * Finds next XmlElement child of this XmlElement
     * @param name Name of the child
     * @param prev Previous child
     * @param ns Optional child namespace
     * @param noPrefix True to compare the tag without namespace prefix, false to
     *  include namespace prefix when comparing the given tag.
     *  This parameter is ignored if name is 0 or ns is not 0
     * @return The next found child if prev exists else the first
     */
    inline XmlElement* findNextChild(const String& name, const XmlElement* prev = 0,
	const String* ns = 0, bool noPrefix = true) const
        { return findNextChild(prev,&name,ns,noPrefix); }

    /**
     * Retrieve a child's text
     * @param name Name (tag) of the child
     * @param ns Optional child namespace
     * @param noPrefix True to compare the tag without namespace prefix, false to
     *  include namespace prefix when comparing the given tag.
     *  This parameter is ignored ns is not 0
     * @return Pointer to child's text, 0 if the child was not found
     */
    inline const String* childText(const String& name, const String* ns = 0,
	bool noPrefix = true) const {
	    XmlElement* c = findFirstChild(&name,ns,noPrefix);
	    return c ? &(c->getText()) : 0;
	}

    /**
     * Get first XmlChild of this XmlElement
     * @return The first XmlChild found.
     */
     XmlChild* getFirstChild();

    /**
     * @return The first XmlText found in this XmlElement children
     */
    const String& getText() const;

    /**
     * Set text for first XmlText element found in this XmlElement's children
     * If child text element does not exist, create it and append it to the element's children.
     * @param text Text to set to the XmlElement. If null, the first found XmlText element
     *  will be deleted.
     * @return The set XmlText if text was set, null if an XmlText was deleted
     */
    XmlText* setText(const char* text);

    /**
     * Add a text child
     * @param text Non empty text to add
     */
    void addText(const char* text);

    /**
     * Retrieve the list of attributes
     * @return Element attributes
     */
    inline const NamedList& attributes() const
	{ return m_element; }

    /**
     * Copy element attributes to a list of parameters
     * @param list Destination list
     * @param prefix Prefix to be added to each attribute name
     * @return The number of attributes added to the destination list
     */
    unsigned int copyAttributes(NamedList& list, const String& prefix) const;

    /**
     * Set element attributes from a list of parameters
     * @param list List of attributes
     * @param prefix Add only the attributes that start with this prefix. =
     *  If NULL, it will set as attributes the whole parameter list
     * @param skipPrefix Skip over the prefix when building attribute name
     */
    void setAttributes(NamedList& list, const String& prefix, bool skipPrefix = true);

    /**
     * Add or replace an attribute
     * @param name Attribute name
     * @param value Attribute value
     */
    inline void setAttribute(const String& name, const char* value)
	{ m_element.setParam(name,value); }

    /**
     * Add or replace an attribute. Clears it if value is empty
     * @param name Attribute name
     * @param value Attribute value
     */
    inline void setAttributeValid(const String& name, const char* value) {
	    if (!TelEngine::null(value))
		m_element.setParam(name,value);
	    else
		removeAttribute(name);
	}

    /**
     * Obtain an attribute value for the given name
     * @param name The name of the attribute
     * @return Attribute value
     */
    inline const char* attribute(const String& name) const
	{ return m_element.getValue(name); }

    /**
     * Obtain an attribute value for the given name
     * @param name The name of the attribute
     * @return String pointer or 0 if not found
     */
    inline String* getAttribute(const String& name) const
	{ return m_element.getParam(name); }

    /**
     * Check if the element has an attribute with a requested value
     * @param name The name of the attribute
     * @param value The value to check
     * @return True if the element has an attribute with the requested value
     */
    inline bool hasAttribute(const String& name, const String& value) const {
	    String* a = getAttribute(name);
	    return a && *a == value;
	}

    /**
     * Remove an attribute
     * @param name Attribute name
     */
    inline void removeAttribute(const String& name)
	{ m_element.clearParam(name); }

    /**
     * Retrieve the element's namespace
     * @return Element's namespace or 0 if not found
     */
    inline String* xmlns() const {
	    if (!m_prefixed)
		return xmlnsAttribute(s_ns);
	    return xmlnsAttribute(s_nsPrefix + *m_prefixed);
	}

    /**
     * Retrieve a namespace attribute. Search in parent or inherited for it
     * @return String pointer or 0 if not found
     */
    String* xmlnsAttribute(const String& name) const;

    /**
     * Verify if this element belongs to the given namespace
     * @param ns The namespace to compare with
     * @return True if this element belongs to the given namespace
     */
    inline bool hasXmlns(const String& ns) const {
	    const String* x = xmlns();
	    return x && *x == ns;
	}

    /**
     * Set the element's namespace
     * @param name The namespace name (element prefix). Can be the default
     *  namespace attribute name (empty means the default one)
     * @param addAttr True to add a non empty, not repeating, namespace attribute to the list
     * @param value Namespace value (ignored if addAttr is false)
     * @return True on success, false if another namespace exists with the same value
     */
    bool setXmlns(const String& name = String::empty(), bool addAttr = false,
	const String& value = String::empty());

    /**
     * Check if a string represents a namespace attribute name
     * @param str The string to check
     * @return True if the given string is the default namespace attribute name or
     *  a namespace attribute name prefix
     */
    static inline bool isXmlns(const String& str)
	{ return str == s_ns || str.startsWith(s_nsPrefix); }

    /**
     * Build an XML element from a list parameter.
     * Parameter name will be set in a 'name' attribute.
     * Parameter value will be set in a 'value' attribute
     * Handle NamedPointer parameters carrying DataBlock, NamedList and
     *  XmlElement objects (a 'type' attribute is added to the created element).
     *  DataBlock: Encode using BASE64 and add it as element text
     *  NamedList: The name is added as element text. The function is called
     *   again for each list parameter
     *  XmlElement: Added as child to newly created element
     * @param param The parameter to convert
     * @param tag XmlElement tag
     * @param copyXml True to copy XmlElement objects instead of just remove
     *  them from the parameter
     * @return XmlElement pointer or 0 on failure
     */
    static XmlElement* param2xml(NamedString* param, const String& tag,
	bool copyXml = false);

    /**
     * Build a list parameter from xml element
     * See @ref param2xml for more info
     * @param xml The XML element to process
     * @param tag Child XmlElement tag to handle
     * @param copyXml True to copy XmlElement objects instead of just remove
     *  them from parent
     * @return NamedString pointer or 0 on failure
     */
    static NamedString* xml2param(XmlElement* xml, const String* tag,
	bool copyXml = false);

    /**
     * Build and add list parameters from XML element children.
     * Each parameter will be taken from 'name' and 'value' attributes.
     * See @ref param2xml for more info
     * @param list Destination list
     * @param parent The XML element to process
     * @param tag Child XmlElement tag to handle
     * @param copyXml True to copy XmlElement objects instead of just remove
     *  them from parent
     */
    static void xml2param(NamedList& list, XmlElement* parent, const String* tag,
	bool copyXml = false);

    /**
     * Default namespace attribute name
     */
    static const String s_ns;

    /**
     * Namespace attribute name perfix
     */
    static const String s_nsPrefix;

private:
    // Set prefixed data (tag and prefix)
    inline void setPrefixed() {
	    TelEngine::destruct(m_prefixed);
	    int pos = m_element.find(":");
	    if (pos != -1)
		m_prefixed = new NamedString(m_element.substr(pos + 1),m_element.substr(0,pos));
	}

    XmlFragment m_children;                      // Children of this element
    NamedList m_element;                         // The element
    NamedString* m_prefixed;                     // Splitted prefixed tag (the value is the namespace prefix)
    XmlParent* m_parent;                         // The XmlElement who holds this element
    NamedList* m_inheritedNs;                    // Inherited namespaces (if parent is 0)
    bool m_empty;                                // True if this element does not have any children
    bool m_complete;                             // True if the end element tag war reported
};

/**
 * A Xml Comment from Xml document
 * @short Xml Comment
 */
class YATE_API XmlComment : public XmlChild
{
    YCLASS(XmlComment,XmlChild)
public:
    /**
     * Constructor
     * @param comm The comment content
     */
    XmlComment(const String& comm);

    /**
     * Copy constructor
     * @param orig Original XmlComment
     */
    XmlComment(const XmlComment& orig);

    /**
     * Destructor
     */
    virtual ~XmlComment();

    /**
     * Get the text contained by this comment
     * @return The comment
     */
    inline const String& getComment() const
	{ return m_comment; }

    /**
     * Build a String from this XmlComment
     * @param dump The string where to append representation
     * @param indent Spaces for output
     */
    void toString(String& dump, const String& indent = String::empty()) const;

    /**
     * Get the Xml comment
     * @return This object
     */
    virtual XmlComment* xmlComment()
	{ return this; }

private:
    String m_comment;                       // The comment
};

/**
 * A Xml CData from Xml document
 * @short Xml Declaration
 */
class YATE_API XmlCData : public XmlChild
{
    YCLASS(XmlCData,XmlChild)
public:

    /**
     * Constructor
     * @param data The CData content
     */
    XmlCData(const String& data);

    /**
     * Copy constructor
     * @param orig Original XmlCData
     */
    XmlCData(const XmlCData& orig);

    /**
     * Destructor
     */
    virtual ~XmlCData();

    /**
     * Get the CData content
     * @return The content of this xml object
     */
    inline const String& getCData() const
	{ return m_data;}

    /**
     * Build a String from this XmlCData
     * @param dump The string where to append representation
     * @param indent Spaces for output
     */
    void toString(String& dump, const String& indent = String::empty()) const;

    /**
     * Get the Xml CData
     * @return This object
     */
    virtual XmlCData* xmlCData()
	{ return this; }

private:
    String m_data;                        // The data
};

/**
 * A Xml Declaration for Xml document
 * @short Xml Declaration
 */
class YATE_API XmlText : public XmlChild
{
    YCLASS(XmlText,XmlChild)
public:
    /**
     * Constructor
     * @param text The text
     */
    XmlText(const String& text);

    /**
     * Copy constructor
     * @param orig Original XmlText
     */
    XmlText(const XmlText& orig);

    /**
     * Destructor
     */
    virtual ~XmlText();

    /**
     * @return The text kept by this Xml Text
     */
    inline const String& getText() const
	{ return m_text; }

    /**
     * Set the text
     * @param text Text to set in this XmlText
     */
    inline void setText(const char* text)
	{ m_text = text; }

    /**
     * Build a String from this XmlText
     * @param dump The string where to append representation
     * @param escape True if the text need to be escaped
     * @param indent Spaces for output
     * @param auth Optional list of tag and attribute names to be replaced with '***'. This
     *  parameter can be used when the result will be printed to output to avoid printing
     *  authentication data to output. The array must end with an empty string
     * @param parent Optional parent element whose tag will be searched in the auth list
     */
    void toString(String& dump, bool escape = true, const String& indent = String::empty(),
	const String* auth = 0, const XmlElement* parent = 0) const;

    /**
     * Get the Xml text
     * @return This object
     */
    virtual XmlText* xmlText()
	{ return this; }

    /**
     * Helper method to check if the text held by this XmlText contains only spaces
     * @return False if the text contains non space characters.
     */
    bool onlySpaces();
private:
    String m_text;                        // The text
};

class YATE_API XmlDoctype : public XmlChild
{
    YCLASS(XmlDoctype,XmlChild)
public:
    /**
     * Constructor
     * @param doctype The doctype
     */
    XmlDoctype(const String& doctype);

    /**
     * Copy constructor
     * @param orig Original XmlDoctype
     */
    XmlDoctype(const XmlDoctype& orig);

    /**
     * Destructor
     */
    virtual ~XmlDoctype();

    /**
     * Get the doctype held by this Xml doctype
     * @return The content of this Xml doctype
     */
    inline const String& getDoctype() const
	{ return m_doctype; }

    /**
     * Get the Xml doctype
     * @return This object
     */
    virtual XmlDoctype* xmlDoctype()
	{ return this; }

    /**
     * Build a String from this XmlDoctype
     * @param dump The string where to append representation
     * @param indent Spaces for output
     */
    void toString(String& dump, const String& indent = String::empty()) const;

private:
    String m_doctype;                          // The document type
};

}; // namespace TelEngine

#endif /* __YATEXML_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
