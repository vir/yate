/*
 * yatemime.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * MIME types, body codecs and related functions
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
 * Abstract base class for holding Multipurpose Internet Mail Extensions data
 * @short Abstract MIME data holder
 */
class YATE_API MimeBody : public GenObject
{
public:
    /**
     * Destructor
     */
    virtual ~MimeBody();

    /**
     * RTTI method, get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Retrive the MIME type of this body
     * @return Name of the MIME type/subtype
     */
    inline const String& getType() const
	{ return m_type; }

    /**
     * Retrive the binary encoding of this MIME body
     * @return Block of binary data
     */
    const DataBlock& getBody() const;

    /**
     * Check if this body is an Session Description Protocol
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
     * Method to build a MIME body from a type and data buffer
     * @param buf Pointer to buffer of data
     * @param len Length of data in buffer
     * @param type Name of the MIME type/subtype, must be lower case
     * @return Newly allocated MIME body or NULL if type is unknown
     */
    static MimeBody* build(const char* buf, int len, const String& type);

    /**
     * Utility method, returns an unfolded line and advances the pointer
     * @param buf Reference to pointer to start of buffer data
     * @param len Reference to variable holding buffer length
     * @return Newly allocated String holding the line of text
     */
    static String* getUnfoldedLine(const char*& buf, int& len);

protected:
    /**
     * Constructor to be used only by derived classes
     * @param type Name of the MIME type/subtype, must be lower case
     */
    MimeBody(const String& type);

    /**
     * Method that is called internally to build the binary encoded body
     */
    virtual void buildBody() const = 0;

    /**
     * Block of binary data that @ref buildBody() must fill
     */
    mutable DataBlock m_body;

private:
    String m_type;
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
     */
    MimeSdpBody();

    /**
     * Constructor from block of data
     * @param buf Pointer to buffer of data
     * @param len Length of data in buffer
     * @param type Name of the MIME type/subtype, should be "application/sdp"
     */
    MimeSdpBody(const String& type, const char* buf, int len);

    /**
     * Destructor
     */
    virtual ~MimeSdpBody();

    /**
     * RTTI method, get a pointer to a derived class given that class name
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
     * Retrive the lines hold in data
     * @return List of NamedStrings
     */
    inline const ObjList& lines() const
	{ return m_lines; }

    /**
     * Append a new name=value line of SDP data
     * @param name Name of the line, should be one character
     * @param value Text of the line
     */
    inline void addLine(const char* name, const char* value = 0)
	{ m_lines.append(new NamedString(name,value)); }

    /**
     * Retrive the first line matching a name
     * @param name Name of the line to search
     * @return First instance of the searched name or NULL if none present
     */
    const NamedString* getLine(const char* name) const;

    /**
     * Retrive the next line of the same type as the current
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
    ObjList m_lines;
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
     * @param buf Pointer to buffer of data
     * @param len Length of data in buffer
     * @param type Name of the specific MIME type/subtype
     */
    MimeBinaryBody(const String& type, const char* buf, int len);

    /**
     * Destructor
     */
    virtual ~MimeBinaryBody();

    /**
     * RTTI method, get a pointer to a derived class given that class name
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
     * @param buf Pointer to buffer of data
     * @param len Length of data in buffer
     * @param type Name of the specific MIME type/subtype
     */
    MimeStringBody(const String& type, const char* buf, int len = -1);

    /**
     * Destructor
     */
    virtual ~MimeStringBody();

    /**
     * RTTI method, get a pointer to a derived class given that class name
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
     * Retrive the stored data
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
     * @param buf Pointer to buffer of data
     * @param len Length of data in buffer
     * @param type Name of the specific MIME type/subtype
     */
    MimeLinesBody(const String& type, const char* buf, int len);

    /**
     * Destructor
     */
    virtual ~MimeLinesBody();

    /**
     * RTTI method, get a pointer to a derived class given that class name
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
     * Retrive the stored lines of text
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
