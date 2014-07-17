/**
 * yatemime.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * MIME types, body codecs and related functions
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

#ifndef __YATEMIME_H
#define __YATEMIME_H

#ifndef __cplusplus
#error C++ is required
#endif

#include <yateclass.h>

/**
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

/**
 * A MIME header line.
 * The NamedString's value contain the first parameter after the header name
 * @short MIME header line
 */
class YATE_API MimeHeaderLine : public NamedString
{
public:
    /**
     * Constructor.
     * Builds a MIME header line from a string buffer.
     * Splits the value into header parameters
     * @param name The header name
     * @param value The header value
     * @param sep Optional parameter separator. If 0, the default ';' will be used
     */
    MimeHeaderLine(const char* name, const String& value, char sep = 0);

    /**
     * Constructor.
     * Builds this MIME header line from another one
     * @param original Original header line to build from.
     * @param newName Optional new header name. If 0, the original name will be used
     */
    MimeHeaderLine(const MimeHeaderLine& original, const char* newName = 0);

    /**
     * Destructor.
     */
    virtual ~MimeHeaderLine();

    /**
     * RTTI method, get a pointer to a derived class given the class name.
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Duplicate this MIME header line.
     * @param newName Optional new header name. If 0, this header's name will be used
     * @return Copy of this MIME header line
     */
    virtual MimeHeaderLine* clone(const char* newName = 0) const;

    /**
     * Build a string line from this MIME header without adding a line separator
     * @param line Destination string
     * @param header True to add the header in front of line text
     */
    virtual void buildLine(String& line, bool header = true) const;

    /**
     * Assignement operator. Set the header's value
     * @param value The new headr value
     */
    inline MimeHeaderLine& operator=(const char* value)
        { NamedString::operator=(value); return *this; }

    /**
     * Get the header's parameters
     * @return This header's list of parameters
     */
    inline const ObjList& params() const
	{ return m_params; }

    /**
     * Get the character used as separator in header line
     * @return This header's separator
     */
    inline char separator() const
	{ return m_separator; }

    /**
     * Replace the value of an existing parameter or add a new one
     * @param name Parameter's name
     * @param value Parameter's value
     */
    void setParam(const char* name, const char* value = 0);

    /**
     * Remove a parameter from list
     * @param name Parameter's name
     */
    void delParam(const char* name);

    /**
     * Get a header parameter
     * @param name Parameter's name
     * @return Pointer to the desired parameter or 0 if not found
     */
    const NamedString* getParam(const char* name) const;

    /**
     * Utility function, puts quotes around a string.
     * @param str String to put quotes around.
     * @param force True to force quoting even if was already quoted
     */
    static void addQuotes(String& str, bool force = false);

    /**
     * Utility function, removes quotes around a string.
     * @param str String to remove quotes.
     * @param force True to force unquoting even if wasn't properly quoted
     */
    static void delQuotes(String& str, bool force = false);

    /**
     * Utility function, puts quotes around a string.
     * @param str String to put quotes around.
     * @param force True to force quoting even if was already quoted
     * @return The input string enclosed in quotes.
     */
    static String quote(const String& str, bool force = false);

    /**
     * Utility function, removes quotes around a string.
     * @param str String to remove quotes around.
     * @param force True to force unquoting even if wasn't properly quoted
     * @return The input string with enclosing quotes removed.
     */
    static String unquote(const String& str, bool force = false);

    /**
     * Utility function to find a separator not in "quotes" or inside \<uri\>.
     * @param str Input string used to find the separator.
     * @param sep The separator to find.
     * @param offs Starting offset in input string.
     * @return The position of the separator in input string or -1 if not found.
     */
    static int findSep(const char* str, char sep, int offs = 0);

    /**
     * Build a string from a list of MIME header lines.
     * Add a CR/LF terminator after each line
     * @param buf Destination string
     * @param headers The list with the header lines
     */
    static void buildHeaders(String& buf, const ObjList& headers);

protected:
    ObjList m_params;                    // Header list of parameters
    char m_separator;                    // Parameter separator
private:
    void operator=(const MimeHeaderLine&); // no assignment
};

/**
 * A MIME header line containing authentication data.
 * @short MIME authentication line
 */
class YATE_API MimeAuthLine : public MimeHeaderLine
{
public:
    /**
     * Constructor.
     * Builds a MIME authentication header line from a string buffer.
     * Splits the value into header parameters
     * @param name The header name
     * @param value The header value
     */
    MimeAuthLine(const char* name, const String& value);

    /**
     * Constructor.
     * Builds this MIME authentication header line from another one
     * @param original Original header line to build from.
     * @param newName Optional new header name. If 0, the original name will be used
     */
    MimeAuthLine(const MimeAuthLine& original, const char* newName = 0);

    /**
     * RTTI method, get a pointer to a derived class given the class name.
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Duplicate this MIME header line.
     * @param newName Optional new header name. If 0, this header's name will be used
     * @return Copy of this MIME header line
     */
    virtual MimeHeaderLine* clone(const char* newName = 0) const;

    /**
     * Build a string line from this MIME header without adding a line separator
     * @param line Destination string
     * @param header True to add the header in front of line text
     */
    virtual void buildLine(String& line, bool header = true) const;

private:
    void operator=(const MimeAuthLine&); // no assignment
};

/**
 * Abstract base class for holding Multipurpose Internet Mail Extensions data.
 * Keeps a Content-Type header line with body type and parameters and
 *  any additional header lines the body may have.
 * The body type contains lower case characters.
 * @short Abstract MIME data holder
 */
class YATE_API MimeBody : public GenObject
{
    YNOCOPY(MimeBody); // no automatic copies please
public:
    /**
     * Destructor
     */
    virtual ~MimeBody();

    /**
     * RTTI method, get a pointer to a derived class given the class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Retrieve the MIME type of this body
     * @return Name of the MIME type/subtype
     */
    inline const MimeHeaderLine& getType() const
	{ return m_type; }

    /**
     * Get the first body that matches a requested type, descends into multiparts
     * @param type Name of the MIME type to search for
     * @return Pointer to requested body or NULL if not found
     */
    MimeBody* getFirst(const String& type) const;

    /**
     * Retrieve the additional headers of this MIME body (other then Content-Type)
     * @return The list of header lines of this MIME body
     */
    inline const ObjList& headers() const
	{ return m_headers; }

    /**
     * Append an additional header line to this body
     * @param hdr The header line to append
     */
    inline void appendHdr(MimeHeaderLine* hdr)
	{ if (hdr) m_headers.append(hdr); }

    /**
     * Remove an additional header line from this body
     * @param hdr The header line to remove
     * @param delobj True to delete the header, false to remove from list without deleting it
     */
    inline void removeHdr(MimeHeaderLine* hdr, bool delobj = true)
	{ if (hdr) m_headers.remove(hdr,delobj); }

    /**
     * Find an additional header line by its name. The names are compared case insensitive
     * @param name The name of the header to find
     * @param start The starting point in the list. 0 to start from the beginning
     * @return Pointer to MimeHeaderLine or 0 if not found
     */
    MimeHeaderLine* findHdr(const String& name, const MimeHeaderLine* start = 0) const;

    /**
     * Build a string with this body's header lines
     * @param buf Destination string
     */
    inline void buildHeaders(String& buf) {
	    m_type.buildLine(buf);
	    buf << "\r\n";
	    MimeHeaderLine::buildHeaders(buf,m_headers);
	}

    /**
     * Replace the value of an existing parameter or add a new one
     * @param name Parameter's name
     * @param value Parameter's value
     * @param header Header whose parameter will be changed.
     *  Set to 0 to use the body's content type header
     * @return False if the header doesn't exist
     */
    bool setParam(const char* name, const char* value = 0, const char* header = 0);

    /**
     * Remove a header parameter
     * @param name Parameter's name
     * @param header Header whose parameter will be removed.
     *  Set to 0 to use the body's content type header
     * @return False if the header doesn't exist
     */
    bool delParam(const char* name, const char* header = 0);

    /**
     * Get a header parameter
     * @param name Parameter's name
     * @param header Header whose parameter will be retrieved.
     *  Set to 0 to use the body's content type header
     * @return Pointer to the desired parameter or 0 if not found
     */
    const NamedString* getParam(const char* name, const char* header = 0) const;

    /**
     * Retrieve the binary encoding of this MIME body. Build the body if empty.
     * The body doesn't contain the Content-Type header or the additional headers
     * @return Block of binary data
     */
    const DataBlock& getBody() const;

    /**
     * Get the binary data of this MIME body without building it.
     * @return Block of binary data
     */
    inline const DataBlock& body() const
	{ return m_body; }

    /**
     * Check if this body is a Session Description Protocol
     * @return True if this body holds a SDP
     */
    virtual bool isSDP() const
	{ return false; }

    /**
     * Check if this body is multipart (can hold other MIME bodies)
     * @return True if this body is multipart
     */
    virtual bool isMultipart() const
	{ return false; }

    /**
     * Duplicate this MIME body
     * @return Copy of this MIME body
     */
    virtual MimeBody* clone() const = 0;

    /**
     * Method to build a MIME body from a type and data buffer.
     * Unknown body types are built into a binary body. Exactly 1 leading CRLF
     * is removed from the beginning of the buffer if found before building it
     * @param buf Pointer to buffer of data just after the body headers
     * @param len Length of data in buffer
     * @param type The header line declaring the body's content.
     *  Usually this is a Content-Type header line
     * @return Newly allocated MIME body or NULL if the buffer is empty
     */
    static MimeBody* build(const char* buf, int len, const MimeHeaderLine& type);

    /**
     * Utility method, returns an unfolded line and advances the pointer
     * @param buf Reference to pointer to start of buffer data
     * @param len Reference to variable holding buffer length
     * @return Newly allocated String holding the line of text
     */
    static String* getUnfoldedLine(const char*& buf, int& len);

protected:
    /**
     * Constructor to be used only by derived classes.
     * Converts the MIME type string to lower case
     * @param type The value of the Content-Type header line
     */
    MimeBody(const String& type);

    /**
     * Constructor to be used only by derived classes.
     * Builds this body from a header line.
     * Converts the MIME type string to lower case
     * @param type The content type header line
     */
    MimeBody(const MimeHeaderLine& type);

    /**
     * Method that is called internally to build the binary encoded body
     */
    virtual void buildBody() const = 0;

    /**
     * Block of binary data that @ref buildBody() must fill
     */
    mutable DataBlock m_body;

    /**
     * Additional body headers (other then Content-Type)
     */
    ObjList m_headers;

private:
    MimeHeaderLine m_type;               // Content type header line
};

/**
 * An object holding the bodies of a multipart MIME
 * @short MIME multipart container
 */
class YATE_API MimeMultipartBody : public MimeBody
{
public:
    /**
     * Constructor to build an empty multipart body
     * @param subtype The multipart subtype
     * @param boundary The string used as separator for enclosed bodies.
     *  A random one will be created if missing. The length will be truncated
     *  to 70 if this value is exceeded
     */
    explicit MimeMultipartBody(const char* subtype = "mixed", const char* boundary = 0);

    /**
     * Constructor from block of data
     * @param type The value of the Content-Type header line
     * @param buf Pointer to buffer of data
     * @param len Length of data in buffer
     */
    MimeMultipartBody(const String& type, const char* buf, int len);

    /**
     * Constructor from block of data
     * @param type The content type header line
     * @param buf Pointer to buffer of data
     * @param len Length of data in buffer
     */
    MimeMultipartBody(const MimeHeaderLine& type, const char* buf, int len);

    /**
     * Destructor
     */
    virtual ~MimeMultipartBody();

    /**
     * Get the list of bodies enclosed contained in this multipart
     * @return The list of bodies enclosed contained in this multipart
     */
    inline const ObjList& bodies() const
	{ return m_bodies; }

    /**
     * Append a body to this multipart
     * @param body The body to append
     */
    inline void appendBody(MimeBody* body)
	{ if (body) m_bodies.append(body); }

    /**
     * Remove a body from this multipart
     * @param body The body to remove
     * @param delobj True to delete the body, false to remove from list without deleting it
     */
    inline void removeBody(MimeBody* body, bool delobj = true)
	{ if (body) m_bodies.remove(body,delobj); }

    /**
     * Find a body. Enclosed multiparts are also searched for the requested body
     * @param content The value of the body to find. Must be lower case
     * @param start The starting point in the list. 0 to start from the beginning.
     *  Be aware that this parameter is used internally to search within enclosed
     *  multipart bodies and set to 0 when the starting point is found
     * @return Pointer to MimeBody or 0 if not found
     */
    MimeBody* findBody(const String& content, MimeBody** start = 0) const;

    /**
     * RTTI method, get a pointer to a derived class given the class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Check if this body is multipart (can hold other MIME bodies)
     * @return True if this body is multipart
     */
    virtual bool isMultipart() const
	{ return true; }

    /**
     * Duplicate this MIME body
     * @return Copy of this MIME body
     */
    virtual MimeBody* clone() const;

protected:
    /**
     * Copy constructor
     */
    MimeMultipartBody(const MimeMultipartBody& original);

    /**
     * Method that is called internally to build the binary encoded body
     */
    virtual void buildBody() const;

    /**
     * Parse a data buffer and append any valid body to this multipart
     * Ignore prolog, epilog and invalid bodies
     * @param buf Pointer to buffer of data
     * @param len Length of data in buffer
     */
    void parse(const char* buf, int len);

private:
    // Parse input buffer for first body boundary or data end
    // Advance buffer pass the boundary line and decrease the buffer length
    // Set endBody to true if the last boundary was found
    // Return the length of data before the found boundary
    int findBoundary(const char*& buf, int& len,
	const char* boundary, unsigned int bLen, bool& endBody);
    // Build a boundary string to be used when parsing or building body
    // Remove quotes if present. Trim blanks
    // Insert CRLF and boundary marks ('--') before parameter
    // @param boundary Destination string
    // @return False if the parameter is missing or the boundary is empty
    bool getBoundary(String& boundary) const;


    ObjList m_bodies;                    // The list of bodies contained in this multipart
};

/**
 * An object holding the lines of an application/sdp MIME type
 * @short MIME for application/sdp
 */
class YATE_API MimeSdpBody : public MimeBody
{
public:
    /**
     * Default constructor, builds an empty application/sdp
     * @param hashing Enable hashing the content lines
     */
    MimeSdpBody(bool hashing = false);

    /**
     * Constructor from block of data
     * @param type The value of the Content-Type header line
     * @param buf Pointer to buffer of data
     * @param len Length of data in buffer
     */
    MimeSdpBody(const String& type, const char* buf, int len);

    /**
     * Constructor from block of data
     * @param type The content type header line
     * @param buf Pointer to buffer of data
     * @param len Length of data in buffer
     */
    MimeSdpBody(const MimeHeaderLine& type, const char* buf, int len);

    /**
     * Destructor
     */
    virtual ~MimeSdpBody();

    /**
     * RTTI method, get a pointer to a derived class given the class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Override that checks if this body is an Session Description Protocol
     * @return True, since this body holds a SDP
     */
    virtual bool isSDP() const
	{ return true; }

    /**
     * Duplicate this MIME body
     * @return Copy of this MIME body - a new MimeSdpBody
     */
    virtual MimeBody* clone() const;

    /**
     * Retrieve the lines hold in data
     * @return List of NamedStrings
     */
    inline const ObjList& lines() const
	{ return m_lines; }

    /**
     * Retrieve the hash of body lines
     * @return Hash of body, zero if hashing not enabled
     */
    inline unsigned int hash() const
	{ return m_hash; }

    /**
     * Append a new name=value line of SDP data
     * @param name Name of the line, should be one character
     * @param value Text of the line
     * @return Pointer to new added line
     */
    NamedString* addLine(const char* name, const char* value = 0);

    /**
     * Retrieve the first line matching a name
     * @param name Name of the line to search
     * @return First instance of the searched name or NULL if none present
     */
    const NamedString* getLine(const char* name) const;

    /**
     * Retrieve the next line of the same type as the current
     * @param line Current line
     * @return Next instance of same name or NULL if no more
     */
    const NamedString* getNextLine(const NamedString* line) const;

protected:
    /**
     * Copy constructor
     */
    MimeSdpBody(const MimeSdpBody& original);

    /**
     * Override that is called internally to build the binary encoded body
     */
    virtual void buildBody() const;

private:
    // Build the lines from a data buffer
    void buildLines(const char* buf, int len);

    ObjList m_lines;
    ObjList* m_lineAppend;
    unsigned int m_hash;
    bool m_hashing;
};

/**
 * An object holding a binary block of MIME data
 * @short MIME for obscure binary data
 */
class YATE_API MimeBinaryBody : public MimeBody
{
public:
    /**
     * Constructor from block of data
     * @param type The value of the Content-Type header line
     * @param buf Pointer to buffer of data
     * @param len Length of data in buffer
     */
    MimeBinaryBody(const String& type, const char* buf, int len);

    /**
     * Constructor from block of data
     * @param type The content type header line
     * @param buf Pointer to buffer of data
     * @param len Length of data in buffer
     */
    MimeBinaryBody(const MimeHeaderLine& type, const char* buf, int len);

    /**
     * Destructor
     */
    virtual ~MimeBinaryBody();

    /**
     * RTTI method, get a pointer to a derived class given the class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Duplicate this MIME body
     * @return Copy of this MIME body - a new MimeBinaryBody
     */
    virtual MimeBody* clone() const;

protected:
    /**
     * Copy constructor
     */
    MimeBinaryBody(const MimeBinaryBody& original);

    /**
     * Override that is called internally to build the binary encoded body
     */
    virtual void buildBody() const;
};

/**
 * An object holding MIME data as just one text string
 * @short MIME for one text string
 */
class YATE_API MimeStringBody : public MimeBody
{
public:
    /**
     * Constructor from block of data
     * @param type The value of the Content-Type header line
     * @param buf Pointer to buffer of data
     * @param len Length of data in buffer
     */
    MimeStringBody(const String& type, const char* buf, int len = -1);

    /**
     * Constructor from block of data
     * @param type The content type header line
     * @param buf Pointer to buffer of data
     * @param len Length of data in buffer
     */
    MimeStringBody(const MimeHeaderLine& type, const char* buf, int len = -1);

    /**
     * Destructor
     */
    virtual ~MimeStringBody();

    /**
     * RTTI method, get a pointer to a derived class given the class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Duplicate this MIME body
     * @return Copy of this MIME body - a new MimeStringBody
     */
    virtual MimeBody* clone() const;

    /**
     * Retrieve the stored data
     * @return String holding the data text
     */
    inline const String& text() const
	{ return m_text; }

protected:
    /**
     * Copy constructor
     */
    MimeStringBody(const MimeStringBody& original);

    /**
     * Override that is called internally to build the binary encoded body
     */
    virtual void buildBody() const;

private:
    String m_text;
};

/**
 * An object holding MIME data as separate text lines
 * @short MIME for multiple text lines
 */
class YATE_API MimeLinesBody : public MimeBody
{
public:
    /**
     * Constructor from block of data
     * @param type The value of the Content-Type header line
     * @param buf Pointer to buffer of data
     * @param len Length of data in buffer
     */
    MimeLinesBody(const String& type, const char* buf, int len);

    /**
     * Constructor from block of data
     * @param type The content type header line
     * @param buf Pointer to buffer of data
     * @param len Length of data in buffer
     */
    MimeLinesBody(const MimeHeaderLine& type, const char* buf, int len);

    /**
     * Destructor
     */
    virtual ~MimeLinesBody();

    /**
     * RTTI method, get a pointer to a derived class given the class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Duplicate this MIME body
     * @return Copy of this MIME body - a new MimeLinesBody
     */
    virtual MimeBody* clone() const;

    /**
     * Retrieve the stored lines of text
     * @return List of Strings
     */
    inline const ObjList& lines() const
	{ return m_lines; }

    /**
     * Append a line of text to the data
     * @param line Text to append
     */
    inline void addLine(const char* line)
	{ m_lines.append(new String(line)); }

protected:
    /**
     * Copy constructor
     */
    MimeLinesBody(const MimeLinesBody& original);

    /**
     * Override that is called internally to build the binary encoded body
     */
    virtual void buildBody() const;

private:
    ObjList m_lines;
};

}; // namespace TelEngine

#endif /* __YATEMIME_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
