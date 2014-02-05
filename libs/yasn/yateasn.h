/**
 * yateasn.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * ASN.1 Library
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

#ifndef __YATEASN_H
#define __YATEASN_H

#include <yateclass.h>

#ifdef _WINDOWS

#ifdef LIBYASN_EXPORTS
#define YASN_API __declspec(dllexport)
#else
#ifndef LIBYASN_STATIC
#define YASN_API __declspec(dllimport)
#endif
#endif

#endif /* _WINDOWS */

#ifndef YASN_API
#define YASN_API
#endif

namespace TelEngine {

#define ASN_LONG_LENGTH 	0x80
#define ASN_BIT8		0x80
#define ASN_EXTENSION_ID	31
#define IS_EXTENSION_ID(byte) (((byte) & ASN_EXTENSION_ID) == ASN_EXTENSION_ID)

class AsnObject;
class AsnValue;
class ASNObjId;
class ASNLib;
class ASNError;

/**
 * Helper class for operations with octet strings. Helps with conversions from String to/from DataBlock
 * @short Helper class for operations with octet strings
 */
class YASN_API OctetString : public DataBlock
{
public:
    /**
     * Get the String contained in this buffer
     * @return String containing the internal data
     */
    inline String getString()
    {
	String str((const char*)data(),length());
	return str;
    }
    inline DataBlock& operator=(const String& value)
    {
	clear();
	append(value);
	return *this;
    }
    inline DataBlock& operator=(const DataBlock& value)
    {
	clear();
	append(value);
	return *this;
    }
    /**
     * Get the content of the buffer in form of a hexified string
     * @return Hexified string
     */
    inline const String toHexString() const
    {
	String str;
	str = str.hexify(data(),length());
	return str;
    }
    /**
     * Builed this DataBlock from a hexified string
     * @return The DataBlock built from the given hexified string
     */
    inline DataBlock& fromHexString(const String& value)
    {
	unHexify(value);
	return *this;
    }
};

/**
 * Abstract class implemented by all ASN.1 type objects
 * @short Base Class for ASN.1 objects
 */
class YASN_API AsnObject : public GenObject {
    YCLASS(AsnObject, GenObject)
public:
    /**
     * Constructor
     */
    inline AsnObject()
	{}

    /**
     * Copy constructor
     * @param original Value object to copy
     */
    inline AsnObject(const AsnObject& original)
	{}

    /**
     * Destructor
     */
    virtual inline ~AsnObject()
	{}

    /**
     * Function to decode the parameters of this object from given data
     * @param data DataBlock from which the object is decoded
     */
    virtual int decode(DataBlock& data) = 0;

    /**
     * Function to encode this object into a datablock
     * @param data The DataBlock in which the object should be encoded
     */
    virtual int encode(DataBlock& data) = 0;

    /**
     * Function for obtaining this object's data
     * @param params NamedList in which this object's data should be put
     */
    virtual void getParams(NamedList* params) = 0;

    /**
     * Function for setting this object's data
     * @param params NamedList containing the values to which this object's data should be set
     */
    virtual void setParams(NamedList* params) = 0;
};

/**
 * Class wrapper for different types of ASN.1 values
 * @short An ASN.1 value
 */
class YASN_API AsnValue : public GenObject {
    YCLASS(AsnValue, GenObject)
public:
    /**
     * Type of value
     */
    enum ValType {
	INTEGER			= 1,
	STRING			= 2,
	OBJECT_ID		= 3,
	IPADDRESS		= 4,
	COUNTER			= 5,
	TIMETICKS		= 6,
	ARBITRARY		= 7,
	BIG_COUNTER		= 8,
	UNSIGNED_INTEGER	= 9
    };

    /**
     * Constructor
     */
    inline AsnValue()
	: m_type(0), m_data("")
	{}

    /**
     * Copy constructor
     * @param original Value object to copy
     */
    inline AsnValue(const AsnValue& original)
	: m_type(original.m_type), m_data(original.m_data)
	{ }

    /**
     * Constructor
     * @param value Object value
     * @param type AsnValue type, default is String
     */
    inline AsnValue(const String& value, int type = STRING)
	: m_type(type), m_data(value)
	{ }

    /**
     * Destructor
     */
    virtual inline ~AsnValue()
	{}

    /**
     * Get the value in the form of a string
     * @return String containing the internal data
     */
    inline const String& getValue()
	{ return m_data; }

    /**
     * Get the type of the data so that we know how to interpret it
     * @return The type of the data
     */
    inline int type()
	{ return m_type;}

    /**
     * Assign operator
     */
    inline AsnValue& operator=( AsnValue* val)
    {
	if (!val)
	    return *this;
	m_data = val->getValue();
	m_type = val->type();
	return *this;
    }

    /**
     * Assign operator
     */
    inline AsnValue& operator=( AsnValue val)
    {
	m_data = val.getValue();
	m_type = val.type();
	return *this;
    }

    /**
     * Set data
     * @param data The data to which the internal data will be set
     */
    inline void setValue(const String& data)
	{ m_data.clear();m_data = data; }

    /**
     * Set data type
     * @param type The type assigned
     */
    inline void setType(int type)
	{ m_type = type; }

private:
    int m_type;
    String m_data;
};

/**
 * Class describing an ASN.1 OID
 */
class YASN_API AsnMib : public GenObject {
    YCLASS(AsnMib, GenObject)
public:
    /**
     * Access levels
     */
    enum Access {
	notAccessible = 0,
	accessibleForNotify = 1,
	readOnly = 2,
	readWrite = 3,
	readCreate = 4
    };
    /**
     * Constructor
     */
    inline AsnMib()
	: m_access(""), m_accessVal(0), m_index(0)
	{}

    /**
     * Constructor
     * @param params NamedList containing data for building this object, it should contain name, access level, value type
     */
    AsnMib(NamedList& params);

    /**
     * Destructor
     */
    inline ~AsnMib()
	{}

    /**
     * Get OID access level in string form
     * @return String containing the access level for this OID. It's one of the following values : not-accessible, read-only, read-write,
	read-create, accessible-for-notify.
     */
    inline String& getAccess()
	{ return m_access;}

    /**
     * Get OID access level
     * @return String containing the access level for this OID. It's one of the following values : not-accessible, read-only, read-write,
	read-create, accessible-for-notify.
     */
    inline int getAccessValue()
	{ return m_accessVal;}

    /**
     * Get the name of this OID
     * @return Name of the OID
     */
    inline String& getName()
	{ return m_name;}

    /**
     * Get the oid
     * @return The OID
     */
    inline String getOID()
	{ String str = ".";
	  str += m_index;
	  return m_oid + str;}

    /**
     * Get the type of the value of this OID
     * @return String containing the type of value
     */
    inline String& getType()
	{ return m_type;}

    /**
     * Get the revision of this OID
     * @return String containing the revision string
     */
    inline String& getRevision()
	{ return m_revision; }

    /**
     * Get the string representation of this OID
     * @return String representation of this OID
     */
    inline const String& toString() const
	{ return m_oid;}

    /**
     * Set the index of an OID in case this OID is part of a table.
     * @param ind Given index
     */
    inline void setIndex(unsigned int ind)
	{ m_index = ind;}

    /**
     * Obtain the index of this OID
     * @return This OID's index in the OID table
     */
    inline unsigned int index()
	{ return m_index;}

    /**
     * Compare this object ID with another
     * @param mib The object ID with which this object should be compared
     * @return 0 if they're equal, -1 if this object is less lexicographically then the given parameter, 1 if it's greater
     */
    int compareTo(AsnMib* mib);

    /**
     * Get the parent object ID of this object
     * @return String version of the parent ID
     */
    inline String getParent()
    {
	int pos = m_oid.rfind('.');
	return m_oid.substr(0,pos);
    }

private:
    String m_name;
    String m_oid;
    String m_access;
    int m_accessVal;
    String m_type;
    String m_revision;
    int size;
    int maxVal;
    int minVal;
    unsigned int m_index;

    static TokenDict s_access[];
};

/**
 * Class for holding only an OID
 */
class YASN_API ASNObjId : public GenObject {
    YCLASS(ASNObjId, GenObject)
public:
    /**
     * Constructor
     */
    ASNObjId();

    /**
     * Copy constructor
     * @param original OID object to copy
     */
    inline ASNObjId(const ASNObjId& original)
	: m_value(original.m_value), m_name(original.m_name)
	{ }

    /**
     * Constructor
     * @param val OID value in string format
     */
    ASNObjId(const String& val);

    /**
     * Constructor
     * @param name Name of the OID
     * @param val OID value in string format
     */
    ASNObjId(const String& name, const String& val);

    /**
     * Constructor
     * @param mib Mib used for creating this OID
     */
    ASNObjId(AsnMib* mib);

    /**
     * Destructor
     */
    ~ASNObjId();

    /**
     * Assignment operator from OID
     */
    inline ASNObjId& operator=(const ASNObjId& original)
	{ m_value = original.toString(); return *this; }

    /**
     * Assign operator from a string value
     */
    inline ASNObjId& operator=(const String& val)
	{ m_value = val; return *this; }

    /**
     * Assign operator from a const char* value
     */
    inline ASNObjId& operator=(const char* val)
	{ m_value = val; return *this; }

    /**
     * Transform the value of this OID from a string value to a sequence of numbers
     */
    void toDataBlock();

    /**
     * Get the sequence form of the OID
     * @return Datablock sequence of ids
     */
    DataBlock getIds();

    /**
     * String representation of the OID
     */
    inline const String& toString() const
	{ return m_value; }

    /**
     * Get the name of the OID
     * @return String representation of the name
     */
    inline const String& getName() const
	{ return m_name; }

    /**
     * Set the OID value
     * @param value OID value
     */
    inline void setValue(const String& value)
	{ m_value = value; toDataBlock();}

private:
    String m_value;
    String m_name;
    DataBlock m_ids;
};

/**
 * Class AsnTag
 * @short Class for ASN.1 tags
 */
class AsnTag {
public:
    /**
     * ASN.1 Tag class types enum
     */
    enum Class {
	Universal   = 0x00,
	Application = 0x40,
	Context     = 0x80,
	Private     = 0xc0,
    };

    /**
     * ASN.1  Type of tag enum
     */
    enum Type {
	Primitive   = 0x00,
	Constructor = 0x20,
    };

    /**
     * Constructor
     */
    inline AsnTag()
        : m_class(Universal), m_type(Primitive), m_code(0)
    { }

    /**
     * Constructor
     * @param clas Class of the ASN.1 Tag
     * @param type Type of the ASN.1 Tag
     * @param code Code ot the ASN.1 Tag
     */
    inline AsnTag(Class clas, Type type, unsigned int code)
        : m_class(clas), m_type(type), m_code(code)
    {
	encode();
    }

    /**
     * Destructor
     */
    inline ~AsnTag()
    {}

    /**
     * Decode an ASN.1 tag from the given data
     * @param tag Tag to fill
     * @param data Data from which the tag should be filled
     */
    static void decode(AsnTag& tag, DataBlock& data);

    /**
     * Encode an ASN.1 tag and put the encoded form into the given data
     * @param clas Class of the tag
     * @param type Type of the tag
     * @param code Tag code
     * @param data DataBlock into which to insert the encoded tag
     */
    static void encode(Class clas, Type type, unsigned int code, DataBlock& data);

    /**
     * Encode self
     */
    inline void encode()
	{ AsnTag::encode(m_class,m_type,m_code,m_coding); }

    /**
     * Equality operator
     */
    inline bool operator==(const AsnTag& tag) const
    {
	return (m_class == tag.classType() && m_type == tag.type() && m_code == tag.code());
    }

    /**
     * Inequality operator
     */
    inline bool operator!=(const AsnTag& tag) const
    {
	return !(m_class == tag.classType() && m_type == tag.type() && m_code == tag.code());
    }

    /**
     * Assignment operator
     */
    inline AsnTag& operator=(const AsnTag& value)
    {
	m_class = value.classType();
	m_type = value.type();
	m_code = value.code();
	encode();
	return *this;
    }

    /**
     * Get the tag class
     * @return The class of the tag
     */
    inline const Class classType() const
	{ return m_class; }

    /**
     * Set the tag class
     * @param clas The clas to set for the  tag
     */
    inline void classType(Class clas)
	{ m_class = clas; }

    /**
     * Get the tag type
     * @return The type of the tag
     */
    inline const Type type() const
	{ return m_type; }

    /**
     * Set the tag type
     * @param type The type to set for the  tag
     */
    inline void type(Type type)
	{ m_type = type; }

    /**
     * Get the tag code
     * @return The code of the tag
     */
    inline const unsigned int code() const
	{ return m_code; }

    /**
     * Set the tag code
     * @param code The code to set for the  tag
     */
    inline void code(unsigned int code)
	{ m_code = code; }

    /**
     * Get the tag encoding
     * @return The DataBlock containing the encoding for the tag
     */
    inline const DataBlock& coding() const
	{ return m_coding; }

private:
    Class m_class;
    Type m_type;
    unsigned int m_code;
    DataBlock m_coding;
};

/**
 * Class ASNLib
 * @short Class containing functions for decoding/encoding ASN.1 basic data types
 */
class YASN_API ASNLib {
public:
    /**
     * ASN.1 Type tags
     */
    enum TypeTag {
	UNIVERSAL	= 0x00,
	BOOLEAN		= 0x01,
	INTEGER 	= 0x02,
	BIT_STRING	= 0x03,
	OCTET_STRING 	= 0x04,
	NULL_ID		= 0x05,
	OBJECT_ID	= 0x06,
	REAL		= 0x09, //not implemented
	UTF8_STR	= 0x0c,
	SEQUENCE	= 0x30,
	SET		= 0x31,
	NUMERIC_STR	= 0x12,
	PRINTABLE_STR	= 0x13,
	IA5_STR		= 0x16,
	UTC_TIME	= 0x17,
	GENERALIZED_TIME = 0x18,
	VISIBLE_STR	= 0x1a,
	GENERAL_STR	= 0x1b, // not implemented
	UNIVERSAL_STR	= 0x1c, // not implemented
	CHARACTER_STR	= 0x1d, // not implemented
	BMP_STR		= 0x1e, // not implemented
	CHOICE		= 0x1f, // does not have a value
	DEFINED		= 0x2d
    };
	// values not implemented
	// 10 	ENUMERATED
	// 11 	EMBEDDED PDV
	// 13 	RELATIVE-OID
	// 20 	TeletexString, T61String
	// 21 	VideotexString
	// 25 	GraphicString
	// 27 	GeneralString
	// 28 	UniversalString
	// 29 	CHARACTER STRING
	// 30 	BMPString
    /**
     * Error types
     */
    enum Error {
	InvalidLengthOrTag = -1,
	ConstraintBreakError = -2,
	ParseError = -3,
	InvalidContentsError = -4,
	IndefiniteForm = -5,
    };

    /**
     * Constructor
     */
    ASNLib();

    /**
     * Destructor
     */
    ~ASNLib();

    /**
     * Decode the length of the block data containing the ASN.1 type data
     * @param data Input block from which to extract the length
     * @return The length of the data block containing data, -1 if it couldn't be decoded
     */
    static int decodeLength(DataBlock& data);

    /**
     * Decode a boolean value from the encoded data
     * @param data Input block from which the boolean value should be extracted
     * @param val Pointer to a boolean to be filled with the decoded value
     * @param tagCheck Flag for indicating if in the process of decoding the value the presence of the ASN.1 tag for boolean (0x01) should be verified
     * @return Length of data consumed from the input data it the decoding was successful, -1 if the boolean value could not be decoded
     */
    static int decodeBoolean(DataBlock& data, bool* val, bool tagCheck);

    /**
     * Decode an integer value from the encoded data
     * @param data Input block from which the integer value should be extracted
     * @param intVal Integer to be filled with the decoded value
     * @param bytes Width of the decoded integer field
     * @param tagCheck Flag for indicating if in the process of decoding the value the presence of the ASN.1 tag for integer (0x02) should be verified
     * @return Length of data consumed from the input data it the decoding was successful, -1 if the integer value could not be decoded
     */
    static int decodeInteger(DataBlock& data, u_int64_t& intVal, unsigned int bytes, bool tagCheck);

    /**
     * Decode an unsigned integer value from the encoded data - helper function for casting from u_int64_t to u_int8_t in case of size constraints
     * @param data Input block from which the integer value should be extracted
     * @param intVal Integer to be filled with the decoded value
     * @param tagCheck Flag for indicating if in the process of decoding the value the presence of the ASN.1 tag for integer (0x02) should be verified
     * @return Length of data consumed from the input data it the decoding was successful, -1 if the integer value could not be decoded
     */
    static int decodeUINT8(DataBlock& data, u_int8_t* intVal, bool tagCheck);

    /**
     * Decode an unsigned integer value from the encoded data - helper function for casting from u_int64_t to u_int16_t in case of size constraints
     * @param data Input block from which the integer value should be extracted
     * @param intVal Integer to be filled with the decoded value
     * @param tagCheck Flag for indicating if in the process of decoding the value the presence of the ASN.1 tag for integer (0x02) should be verified
     * @return Length of data consumed from the input data it the decoding was successful, -1 if the integer value could not be decoded
     */
    static int decodeUINT16(DataBlock& data, u_int16_t* intVal, bool tagCheck);

    /**
     * Decode an unsigned integer value from the encoded data - helper function for casting from u_int64_t to u_int32_t in case of size constraints
     * @param data Input block from which the integer value should be extracted
     * @param intVal Integer to be filled with the decoded value
     * @param tagCheck Flag for indicating if in the process of decoding the value the presence of the ASN.1 tag for integer (0x02) should be verified
     * @return Length of data consumed from the input data it the decoding was successful, -1 if the integer value could not be decoded
     */
    static int decodeUINT32(DataBlock& data, u_int32_t* intVal, bool tagCheck);

    /**
     * Decode an unsigned integer value from the encoded data - helper function for casting in case of size constraints
     * @param data Input block from which the integer value should be extracted
     * @param intVal Integer to be filled with the decoded value
     * @param tagCheck Flag for indicating if in the process of decoding the value the presence of the ASN.1 tag for integer (0x02) should be verified
     * @return Length of data consumed from the input data it the decoding was successful, -1 if the integer value could not be decoded
     */
    static int decodeUINT64(DataBlock& data, u_int64_t* intVal, bool tagCheck);

    /**
     * Decode an integer value from the encoded data - helper function for casting from u_int64_t to int8_t in case of size constraints
     * @param data Input block from which the integer value should be extracted
     * @param intVal Integer to be filled with the decoded value
     * @param tagCheck Flag for indicating if in the process of decoding the value the presence of the ASN.1 tag for integer (0x02) should be verified
     * @return Length of data consumed from the input data it the decoding was successful, -1 if the integer value could not be decoded
     */
    static int decodeINT8(DataBlock& data, int8_t* intVal, bool tagCheck);

    /**
     * Decode an integer value from the encoded data - helper function for casting from u_int64_t to int16_t in case of size constraints
     * @param data Input block from which the integer value should be extracted
     * @param intVal Integer to be filled with the decoded value
     * @param tagCheck Flag for indicating if in the process of decoding the value the presence of the ASN.1 tag for integer (0x02) should be verified
     * @return Length of data consumed from the input data it the decoding was successful, -1 if the integer value could not be decoded
     */
    static int decodeINT16(DataBlock& data, int16_t* intVal, bool tagCheck);

    /**
     * Decode an integer value from the encoded data - helper function for casting from u_int64_t to int32_t in case of size constraints
     * @param data Input block from which the integer value should be extracted
     * @param intVal Integer to be filled with the decoded value
     * @param tagCheck Flag for indicating if in the process of decoding the value the presence of the ASN.1 tag for integer (0x02) should be verified
     * @return Length of data consumed from the input data it the decoding was successful, -1 if the integer value could not be decoded
     */
    static int decodeINT32(DataBlock& data, int32_t* intVal, bool tagCheck);

    /**
     * Decode an integer value from the encoded data - helper function for casting in case of size constraints
     * @param data Input block from which the integer value should be extracted
     * @param intVal Integer to be filled with the decoded value
     * @param tagCheck Flag for indicating if in the process of decoding the value the presence of the ASN.1 tag for integer (0x02) should be verified
     * @return Length of data consumed from the input data it the decoding was successful, -1 if the integer value could not be decoded
     */
    static int decodeINT64(DataBlock& data, int64_t* intVal, bool tagCheck);

    /**
     * Decode a bitstring value from the encoded data
     * @param data Input block from which the bitstring value should be extracted
     * @param val String to be filled with the decoded value
     * @param tagCheck Flag for indicating if in the process of decoding the value the presence of the ASN.1 tag for integer (0x03) should be verified
     * @return Length of data consumed from the input data it the decoding was successful, -1 if the integer value could not be decoded
     */
    static int decodeBitString(DataBlock& data, String* val, bool tagCheck);

    /**
     * Decode a string value from the encoded data
     * @param data Input block from which the octet string value should be extracted
     * @param strVal String to be filled with the decoded value
     * @param tagCheck Flag for indicating if in the process of decoding the value the presence of the ASN.1 tag for integer (0x04) should be verified
     * @return Length of data consumed from the input data it the decoding was successful, -1 if the integer value could not be decoded
     */
    static int decodeOctetString(DataBlock& data, OctetString* strVal, bool tagCheck);

    /**
     * Decode a null value from the encoded data
     * @param data Input block from which the null value should be extracted
     * @param tagCheck Flag for indicating if in the process of decoding the value the presence of the ASN.1 tag for integer (0x05) should be verified
     * @return Length of data consumed from the input data it the decoding was successful, -1 if the integer value could not be decoded
     */
    static int decodeNull(DataBlock& data, bool tagCheck);

    /**
     * Decode an object id value from the encoded data
     * @param data Input block from which the OID value should be extracted
     * @param obj ASNObjId to be filled with the decoded value
     * @param tagCheck Flag for indicating if in the process of decoding the value the presence of the ASN.1 tag for integer (0x06) should be verified
     * @return Length of data consumed from the input data it the decoding was successful, -1 if the integer value could not be decoded
     */
    static int decodeOID(DataBlock& data, ASNObjId* obj, bool tagCheck);

    /**
     * Decode a real value from the encoded data - not implemented
     * @param data Input block from which the real value should be extracted
     * @param realVal Float to be filled with the decoded value
     * @param tagCheck Flag for indicating if in the process of decoding the value the presence of the ASN.1 tag for integer (0x09) should be verified
     * @return Length of data consumed from the input data it the decoding was successful, -1 if the integer value could not be decoded
     */
    static int decodeReal(DataBlock& data, float* realVal, bool tagCheck);

    /**
     * Decode other types of ASN.1 strings from the encoded data (NumericString, PrintableString, VisibleString, IA5String)
     * @param data Input block from which the string value should be extracted
     * @param str String to be filled with the decoded value
     * @param type Integer to be filled with the value indicating which type of string has been decoded
     * @param tagCheck Flag for indicating if in the process of decoding the value the presence of the ASN.1 tag should be verified
     * @return Length of data consumed from the input data it the decoding was successful, -1 if the integer value could not be decoded
     */
    static int decodeString(DataBlock& data, String* str, int* type, bool tagCheck);

    /**
     * Decode an UTF8 string from the encoded data
     * @param data Input block from which the string value should be extracted
     * @param str String to be filled with the decoded value
     * @param tagCheck Flag for indicating if in the process of decoding the value the presence of the ASN.1 tag (0x0c) should be verified
     * @return Length of data consumed from the input data it the decoding was successful, -1 if the integer value could not be decoded
     */
    static int decodeUtf8(DataBlock& data, String* str, bool tagCheck);

    /**
     * Decode a GeneralizedTime value from the encoded data
     * @param data Input block from which the value should be extracted
     * @param time Integer to be filled with time in seconds since epoch
     * @param fractions Integer to be filled with fractions of a second
     * @param utc Flag indicating if the decode time value represent local time or UTC time
     * @param tagCheck Flag for indicating if in the process of decoding the value the presence of the ASN.1 tag (0x18) should be verified
     * @return Length of data consumed from the input data it the decoding was successful, -1 if the integer value could not be decoded
     */
    static int decodeGenTime(DataBlock& data, unsigned int* time, unsigned int* fractions, bool* utc, bool tagCheck);

    /**
     * Decode a UTC time value from the encoded data
     * @param data Input block from which the value should be extracted
     * @param time Integer to be filled with time in seconds since epoch
     * @param tagCheck Flag for indicating if in the process of decoding the value the presence of the ASN.1 tag (0x17) should be verified
     * @return Length of data consumed from the input data it the decoding was successful, -1 if the integer value could not be decoded
     */
    static int decodeUTCTime(DataBlock& data, unsigned int* time, bool tagCheck);

    /**
     * Decode a block of arbitrary data
     * @param data Input block from which the value should be extracted
     * @param val DataBlock in which the data shoulb be copied
     * @param tagCheck Flag for indicating if in the process of decoding the value the presence of the ASN.1 should be verified
     * @return Length of data consumed from the input data it the decoding was successful, -1 if the integer value could not be decoded
     */
    static int decodeAny(DataBlock data, DataBlock* val, bool tagCheck);

    /**
     * Decode the header of an ASN.1 sequence ( decodes the tag and the length of the sequence)
     * @param data Input block from which the header should be extracted
     * @param tagCheck Flag for indicating if in the process of decoding the value the presence of the ASN.1 (0x30) should be verified
     * @return Length of data consumed from the input data it the decoding was succesful, -1 if the integer value could not be decoded
     */
    static int decodeSequence(DataBlock& data, bool tagCheck);

    /**
     * Decode the header of an ASN.1 set ( decodes the tag and the length of the sequence)
     * @param data Input block from which the header should be extracted
     * @param tagCheck Flag for indicating if in the process of decoding the value the presence of the ASN.1 (0x31) should be verified
     * @return Length of data consumed from the input data it the decoding was succesful, -1 if the integer value could not be decoded
     */
    static int decodeSet(DataBlock& data, bool tagCheck);

    /**
     * Encode the length of the given data
     * @param data The data for which the length should be encoded
     * @return The data block which now contains the length encoding
     */
    static DataBlock buildLength(DataBlock& data);

    /**
     * Encode the given boolean value
     * @param val The boolean value to encode
     * @param tagCheck Flag to specify if the boolean type tag should be inserted in the encoding
     * @return The data block encoding of the value
     */
    static DataBlock encodeBoolean(bool val, bool tagCheck);

    /**
     * Encode the given integer value
     * @param intVal The integer value to encode
     * @param tagCheck Flag to specify if the integer type tag should be inserted in the encoding
     * @return The data block encoding of the value
     */
    static DataBlock encodeInteger(u_int64_t intVal, bool tagCheck);

    /**
     * Encode the given octet string value
     * @param strVal The octet string value to encode
     * @param tagCheck Flag to specify if the octet string type tag should be inserted in the encoding
     * @return The data block encoding of the value
     */
    static DataBlock encodeOctetString(OctetString strVal, bool tagCheck);

    /**
     * Encode a null value
     * @param tagCheck Flag to specify if the null tag should be inserted in the encoding
     * @return The data block encoding of the value
     */
    static DataBlock encodeNull(bool tagCheck);

    /**
     * Encode the given bitstring value
     * @param val The bitstring value to encode
     * @param tagCheck Flag to specify if the type tag should be inserted in the encoding
     * @return The data block encoding of the value
     */
    static DataBlock encodeBitString(String val, bool tagCheck);

    /**
     * Encode the given OID value
     * @param obj The OID value to encode
     * @param tagCheck Flag to specify if the type tag should be inserted in the encoding
     * @return The data block encoding of the value
     */
    static DataBlock encodeOID(ASNObjId obj, bool tagCheck);

    /**
     * Encode the given real value - not implemented
     * @param val The real value to encode
     * @param tagCheck Flag to specify if the type tag should be inserted in the encoding
     * @return The data block encoding of the value
     */
    static DataBlock encodeReal(float val, bool tagCheck);

    /**
     * Encode the given string value to NumericString, PrintableString, IA5String, VisibleString
     * @param str The string value to encode
     * @param type The type of the encoding
     * @param tagCheck Flag to specify if the type tag should be inserted in the encoding
     * @return The data block encoding of the value
     */
    static DataBlock encodeString(String str, int type, bool tagCheck);

    /**
     * Encode the UTF8 string value
     * @param str The string value to encode
     * @param tagCheck Flag to specify if the type tag should be inserted in the encoding
     * @return The data block encoding of the value
     */
    static DataBlock encodeUtf8(String str, bool tagCheck);

    /**
     * Encode the given time value into a GeneralizedTime format
     * @param time Time in seconds since epoch to encode
     * @param fractions Fractions of a seconds to encode
     * @param tagCheck Flag to specify if the type tag should be inserted in the encoding
     * @return The data block encoding of the value
     */
    static DataBlock encodeGenTime(unsigned int time, unsigned int fractions, bool tagCheck);

    /**
     * Encode the given time value into an UTCTime format
     * @param time Time in seconds since epoch to encode
     * @param tagCheck Flag to specify if the type tag should be inserted in the encoding
     * @return The data block encoding of the value
     */
    static DataBlock encodeUTCTime(unsigned int time, bool tagCheck);

    /**
     * Encode an arbitrary block a data
     * @param data data
     * @param tagCheck Flag to specify if the type tag should be inserted in the encoding
     * @return The data block encoding of the value
     */
    static DataBlock encodeAny(DataBlock data, bool tagCheck);

    /**
     * Encode the header for a sequence
     * @param data Sequence data for which the header is encoded
     * @param tagCheck Flag to specify if the ype tag should be inserted in the encoding
     * @return The length of the data block length encoding
     */
    static int encodeSequence(DataBlock& data, bool tagCheck);

    /**
     * Encode the header for a set
     * @param data Sequence data for which the header is encoded
     * @param tagCheck Flag to specify if the type tag should be inserted in the encoding
     * @return The length of the data block length encoding
     */
    static int encodeSet(DataBlock& data, bool tagCheck);

    /**
     * Verify the data for End Of Contents presence
     * @param  data Input block to verify
     * @return Length of data consumed from the input data it the decoding was succesful, it should be 2 in case of success, -1 if the data doesn't match EoC
     */
    static int matchEOC(DataBlock& data);

    /**
     * Extract length until a End Of Contents is found.
     * @param data Input block for which to determine the length to End Of Contents
     * @param length Length to which to add determined length
     * @return Length until End Of Contents
     */
    static int parseUntilEoC(DataBlock& data, int length = 0);
};

}

#endif /* __YATEASN_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
