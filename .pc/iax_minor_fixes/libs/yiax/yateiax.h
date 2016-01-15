/**
 * yateiax.h
 * Yet Another IAX2 Stack
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

#ifndef __YATEIAX_H
#define __YATEIAX_H

#include <yateclass.h>

#ifdef _WINDOWS

#ifdef LIBYIAX_EXPORTS
#define YIAX_API __declspec(dllexport)
#else
#ifndef LIBYIAX_STATIC
#define YIAX_API __declspec(dllimport)
#endif
#endif

#endif /* _WINDOWS */

#ifndef YIAX_API
#define YIAX_API
#endif

/**
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

class IAXInfoElement;                    // A single IAX2 Information Element
class IAXInfoElementString;              // A single IAX2 text Information Element
class IAXInfoElementNumeric;             // A single IAX2 numeric Information Element
class IAXInfoElementBinary;              // A single IAX2 numeric Information Element
class IAXIEList;                         // Information Element container
class IAXAuthMethod;                     // Wrapper class for authentication methods values
class IAXFormatDesc;                     // IAX format description
class IAXFormat;                         // Wrapper class for formats
class IAXControl;                        // Wrapper class for subclasses of frames of type IAX
class IAXFrame;                          // This class holds an IAX frame
class IAXFullFrame;                      // This class holds an IAX full frame
class IAXFrameOut;                       // This class holds an outgoing IAX full frame
class IAXTrunkInfo;                      // Trunk info
class IAXMetaTrunkFrame;                 // Meta trunk frame
class IAXMediaData;                      // IAX2 transaction media data
class IAXTransaction;                    // An IAX2 transaction
class IAXEvent;                          // Event class
class IAXEngine;                         // IAX engine

#define IAX_PROTOCOL_VERSION         0x0002           // Protocol version
#define IAX2_MAX_CALLNO              32767            // Max call number value
#define IAX2_MAX_TRANSINFRAMELIST    127              // Max transaction incoming frame list

// Trunk frame header length
#define IAX2_TRUNKFRAME_HEADERLENGTH 8
// Trunk frame length
#define IAX2_TRUNKFRAME_LEN_MIN 20                    // 16 bytes: meta header + miniframe with timestamps header
#define IAX2_TRUNKFRAME_LEN_DEF 1400
// Trunk frame send interval in milliseconds
#define IAX2_TRUNKFRAME_SEND_MIN 5
#define IAX2_TRUNKFRAME_SEND_DEF 20

// Frame retransmission
#define IAX2_RETRANS_COUNT_MIN 1
#define IAX2_RETRANS_COUNT_MAX 10
#define IAX2_RETRANS_COUNT_DEF 4
#define IAX2_RETRANS_INTERVAL_MIN 200
#define IAX2_RETRANS_INTERVAL_MAX 5000
#define IAX2_RETRANS_INTERVAL_DEF 500

// Ping
#define IAX2_PING_INTERVAL_MIN 10000
#define IAX2_PING_INTERVAL_DEF 20000

// Sent challenge timeout
#define IAX2_CHALLENGETOUT_MIN 5000
#define IAX2_CHALLENGETOUT_DEF 30000

/**
 * This class holds a single Information Element with no data
 * @short A single IAX2 Information Element
 */
class YIAX_API IAXInfoElement : public RefObject
{
public:
    /**
     * Information Element enumeration types
     */
    enum Type {
	textframe = 0x00,	 // Text	Used internally only to generate an event of type Text
        CALLED_NUMBER = 0x01,    // Text
        CALLING_NUMBER = 0x02,   // Text
        CALLING_ANI = 0x03,      // Text
        CALLING_NAME = 0x04,     // Text
        CALLED_CONTEXT = 0x05,   // Text
        USERNAME = 0x06,         // Text
        PASSWORD = 0x07,         // Text
        CAPABILITY = 0x08,       // DW
        FORMAT = 0x09,           // DW
        LANGUAGE = 0x0a,         // Text
        VERSION = 0x0b,          // W		Value: IAX_PROTOCOL_VERSION
        ADSICPE = 0x0c,          // W
        DNID = 0x0d,             // Text
        AUTHMETHODS = 0x0e,      // W
        CHALLENGE = 0x0f,        // Text
        MD5_RESULT = 0x10,       // Text
        RSA_RESULT = 0x11,       // Text
        APPARENT_ADDR = 0x12,    // BIN
        REFRESH = 0x13,          // W
        DPSTATUS = 0x14,         // W
        CALLNO = 0x15,           // W		Max value: IAX2_MAX_CALLNO
        CAUSE = 0x16,            // Text
        IAX_UNKNOWN = 0x17,      // B
        MSGCOUNT = 0x18,         // W
        AUTOANSWER = 0x19,       // Null
        MUSICONHOLD = 0x1a,      // Text
        TRANSFERID = 0x1b,       // DW
        RDNIS = 0x1c,            // Text
        PROVISIONING = 0x1d,     // BIN
        AESPROVISIONING = 0x1e,  // BIN
        DATETIME = 0x1f,         // DW
        DEVICETYPE = 0x20,       // Text
        SERVICEIDENT = 0x21,     // BIN
        FIRMWAREVER = 0x22,      // W
        FWBLOCKDESC = 0x23,      // DW
        FWBLOCKDATA = 0x24,      // BIN
        PROVVER = 0x25,          // DW
        CALLINGPRES = 0x26,      // B
        CALLINGTON = 0x27,       // B
        CALLINGTNS = 0x28,       // W
        SAMPLINGRATE = 0x29,     // DW
        CAUSECODE = 0x2a,        // B
        ENCRYPTION = 0x2b,       // B
        ENKEY = 0x2c,            // BIN
        CODEC_PREFS = 0x2d,      // Text
        RR_JITTER = 0x2e,        // DW
        RR_LOSS = 0x2f,          // DW
        RR_PKTS = 0x30,          // DW
        RR_DELAY = 0x31,         // W
        RR_DROPPED = 0x32,       // DW
        RR_OOO = 0x33,           // DW
        CALLTOKEN = 0x36,        // BIN
        CAPABILITY2 = 0x37,      // BIN		1 byte version + array
        FORMAT2 = 0x38,          // BIN		1 byte version + array
    };

    /**
     * Constructor
     * @param type Type of this IE
     */
    inline IAXInfoElement(Type type) : m_type(type) {}

    /**
     * Destructor
     */
    virtual ~IAXInfoElement() {}

    /**
     * Get the type of this IE
     * @return Type of this IE
     */
    inline Type type() const
        { return m_type; }

    /**
     * Constructs a buffer containing this Information Element
     * @param buf Destination buffer
     */
    virtual void toBuffer(DataBlock& buf);

    /**
     * Add this element to a string
     * @param buf Destination string
     */
    virtual void toString(String& buf);

    /**
     * Get the text associated with an IE type value
     * @param ieCode Numeric code of the IE
     * @return Pointer to the IE text or 0 if it doesn't exist
     */
    static inline const char* ieText(u_int8_t ieCode)
	{ return lookup(ieCode,s_ieData); }

    /**
     * Retrieve the cause name associated with a given code
     * @param code Cause code
     * @return Cause name, 0 if not found
     */
    static inline const char* causeName(int code)
	{ return lookup(code,s_causeName); }

    /**
     * Retrieve the cause code associated with a given name
     * @param name Cause name
     * @param defVal Default value to return if not found
     * @return Cause code
     */
    static inline int causeCode(const char* name, int defVal = 0)
	{ return lookup(name,s_causeName,defVal); }

    /**
     * Cause code dictionary
     */
    static const TokenDict s_causeName[];

    /**
     * Number type dictionary
     */
    static const TokenDict s_typeOfNumber[];

    /**
     * Number presentation dictionary
     */
    static const TokenDict s_presentation[];

    /**
     * Number screening dictionary
     */
    static const TokenDict s_screening[];

private:
    static const TokenDict s_ieData[];// Association between IE type and text
    Type m_type;		// Type of this IE
};

/**
 * This class holds a single Information Element with text data
 * @short A single IAX2 text Information Element
 */
class YIAX_API IAXInfoElementString : public IAXInfoElement
{
public:
    /**
     * Constructor
     * @param type Type of this IE
     * @param buf Source buffer to construct this IE
     * @param len Buffer length
     */
    inline IAXInfoElementString(Type type, const char* buf, unsigned len) : IAXInfoElement(type), m_strData(buf,(int)len)
        {}

    /**
     * Destructor
     */
    virtual ~IAXInfoElementString() {}

    /**
     * Get the data length
     * @return The data length
     */
    inline int length() const
        { return m_strData.length(); }

    /**
     * Get the data
     * @return The data
     */
    inline String& data()
        { return m_strData; }

    /**
     * Constructs a buffer containing this Information Element
     * @param buf Destination buffer
     */
    virtual void toBuffer(DataBlock& buf);

    /**
     * Add this element to a string
     * @param buf Destination string
     */
    virtual void toString(String& buf)
	{ buf << m_strData; }

private:
    String m_strData;		// IE text data
};

/**
 * This class holds a single Information Element with 1, 2 or 4 byte(s) length data
 * @short A single IAX2 numeric Information Element
 */
class YIAX_API IAXInfoElementNumeric : public IAXInfoElement
{
public:
    /**
     * Constructor
     * @param type Type of this IE
     * @param val Source value to construct this IE
     * @param len Value length
     */
    IAXInfoElementNumeric(Type type, u_int32_t val, u_int8_t len);

    /**
     * Destructor
     */
    virtual ~IAXInfoElementNumeric() {}

    /**
     * Get the data length
     * @return The data length
     */
    inline int length() const
        { return m_length; }

    /**
     * Get the data
     * @return The data
     */
    inline u_int32_t data() const
	{ return m_numericData; }

    /**
     * Constructs a buffer containing this Information Element
     * @param buf Destination buffer
     */
    virtual void toBuffer(DataBlock& buf);

    /**
     * Add this element to a string
     * @param buf Destination string
     */
    virtual void toString(String& buf);

private:
    u_int8_t m_length;		// IE data length
    u_int32_t m_numericData;	// IE numeric data
};

/**
 * This class holds a single Information Element with binary data
 * @short A single IAX2 numeric Information Element
 */
class YIAX_API IAXInfoElementBinary : public IAXInfoElement
{
public:
    /**
     * Constructor
     * @param type Type of this IE
     * @param buf Source buffer to construct this IE
     * @param len Buffer length
     */
    IAXInfoElementBinary(Type type, unsigned char* buf, unsigned len) : IAXInfoElement(type), m_data(buf,len)
        {}

    /**
     * Destructor
     */
    virtual ~IAXInfoElementBinary() {}

    /**
     * Get the data length
     * @return The data length
     */
    inline int length() const
        { return m_data.length(); }

    /**
     * Get the data
     * @return The data
     */
    inline DataBlock& data()
        { return m_data; }

    /**
     * Set the data
     * @param buf Source buffer to construct this IE
     * @param len Buffer length
     */
    inline void setData(void* buf, unsigned len)
        { m_data.assign(buf,len); }

    /**
     * Constructs a buffer containing this Information Element
     * @param buf Destination buffer
     */
    virtual void toBuffer(DataBlock& buf);

    /**
     * Constructs an APPARENT_ADDR information element from a SocketAddr object
     * @param addr Source object
     * @return A valid IAXInfoElementBinary pointer
     */
    static IAXInfoElementBinary* packIP(const SocketAddr& addr);

    /**
     * Decode an APPARENT_ADDR information element and copy it to a SocketAddr object
     * @param addr Destination object
     * @param ie Source IE
     * @return False if ie is 0
     */
    static bool unpackIP(SocketAddr& addr, IAXInfoElementBinary* ie);

    /**
     * Add this element to a string
     * @param buf Destination string
     */
    virtual void toString(String& buf);

private:
    DataBlock m_data;		// IE binary data
};

/**
 * Management class for a list of Information Elements
 * @short Information Element container
 */
class YIAX_API IAXIEList
{
public:
    /**
     * Constructor
     */
    IAXIEList();

    /**
     * Constructor. Construct the list from an IAXFullFrame object
     * @param frame Source object
     * @param incoming True if it is an incoming frame
     */
    IAXIEList(const IAXFullFrame* frame, bool incoming = true);

    /**
     * Destructor
     */
    ~IAXIEList();

    /**
     * Get the invalid IE list flag
     * @return False if the last frame parse was unsuccessful
     */
    inline bool invalidIEList() const
	{ return m_invalidIEList; }

    /**
     * Clear the list
     */
    inline void clear()
	{ m_list.clear(); }

    /**
     * Check if the list is empty
     * @return True if the list is empty
     */
    inline bool empty()
	{ return 0 == m_list.skipNull(); }

    /**
     * Insert a VERSION Information Element in the list if not already done
     */
    void insertVersion();

    /**
     * Get the validity of the VERSION Information Element of the list if any
     * @return False if version is not IAX_PROTOCOL_VERSION or the list doesn't contain a VERSION Information Element
     */
    inline bool validVersion() {
	    u_int32_t ver = 0xFFFF;
	    getNumeric(IAXInfoElement::VERSION,ver);
	    return ver == IAX_PROTOCOL_VERSION;
	}

    /**
     * Append an Information Element to the list
     * @param ie IAXInfoElement pointer to append
     */
    inline void appendIE(IAXInfoElement* ie)
	{ m_list.append(ie); }

    /**
     * Append an Information Element taken from another list
     * @param src Source IE list
     * @param type IE to move
     * @return True if found and added
     */
    inline bool appendIE(IAXIEList& src, IAXInfoElement::Type type) {
	    IAXInfoElement* ie = src.getIE(type,true);
	    if (ie)
		appendIE(ie);
	    return ie != 0;
	}

    /**
     * Append an Information Element to the list
     * @param type The type of the IAXInfoElement to append
     */
    inline void appendNull(IAXInfoElement::Type type)
	{ m_list.append(new IAXInfoElement(type)); }

    /**
     * Append a text Information Element to the list from a String
     * @param type The type of the IAXInfoElementString to append
     * @param src The source
     */
    inline void appendString(IAXInfoElement::Type type, const String& src)
	{ m_list.append(new IAXInfoElementString(type,src.c_str(),src.length())); }

    /**
     * Append a text Information Element to the list from a buffer
     * @param type The type of the IAXInfoElementString to append
     * @param src The source
     * @param len Source length
     */
    inline void appendString(IAXInfoElement::Type type, unsigned char* src, unsigned len)
	{ m_list.append(new IAXInfoElementString(type,(char*)src,len)); }

    /**
     * Append a numeric Information Element to the list
     * @param type The type of the IAXInfoElementNumeric to append
     * @param value The source
     * @param len Source length
     */
    inline void appendNumeric(IAXInfoElement::Type type, u_int32_t value, u_int8_t len)
	{ m_list.append(new IAXInfoElementNumeric(type,value,len)); }

    /**
     * Append a binary Information Element to the list
     * @param type The type of the IAXInfoElementBinary to append
     * @param data The source data to append
     * @param len Source length
     */
    inline void appendBinary(IAXInfoElement::Type type, unsigned char* data, unsigned len)
	{ m_list.append(new IAXInfoElementBinary(type,data,len)); }

    /**
     * Construct the list from an IAXFullFrame object.
     *  On exit m_invalidIEList will contain the opposite of the returned value
     * @param frame Source object
     * @param incoming True if it is an incoming frame
     * @return False if the frame contains invalid IEs
     */
    bool createFromFrame(const IAXFullFrame* frame, bool incoming = true);

    /**
     * Construct a buffer from this list
     * @param buf Destination buffer
     */
    void toBuffer(DataBlock& buf);

    /**
     * Add this list to a string
     * @param dest Destination string
     * @param indent Optional indent for each element
     */
    void toString(String& dest, const char* indent = 0);

    /**
     * Retrieve an IAXInfoElement from the list
     * @param type The desired type
     * @param remove True to remove from list. The caller will own the object
     * @return An IAXInfoElement pointer or 0 if the list doesn't contain an IE of this type
     */
    IAXInfoElement* getIE(IAXInfoElement::Type type, bool remove = false);

    /**
     * Get the data of a list item into a String. Before any operation dest is cleared
     * @param type The desired type
     * @param dest The destination String
     * @return False if the list doesn't contain an IE of this type
     */
    bool getString(IAXInfoElement::Type type, String& dest);

    /**
     * Get the data of a list item into a numeric destination
     * @param type The desired type
     * @param dest The destination
     * @return False if the list doesn't contain an IE of this type
     */
    bool getNumeric(IAXInfoElement::Type type, u_int32_t& dest);

    /**
     * Get the data of a list item into a DataBlock. Before any operation dest is cleared
     * @param type The desired type
     * @param dest The destination buffer
     * @return False if the list doesn't contain an IE of this type
     */
    bool getBinary(IAXInfoElement::Type type, DataBlock& dest);

private:
    bool m_invalidIEList;	// Invalid IE flag
    ObjList m_list;		// The IE list
};

/**
 * This class holds the enumeration values for authentication methods
 * @short Wrapper class for authentication methods values
 */
class YIAX_API IAXAuthMethod
{
public:
    /**
     * Authentication method enumeration types
     */
    enum Type {
        Text = 1,
        MD5  = 2,
        RSA  = 4,
    };

    /**
     * Create a string list from authentication methods
     * @param dest The destination
     * @param auth The authentication methods as ORed bits
     * @param sep The separator to use
    */
    static void authList(String& dest, u_int16_t auth, char sep);

    static TokenDict s_texts[];
};


/**
 * This class holds IAX format description
 * @short IAX format description
 */
class YIAX_API IAXFormatDesc
{
public:
    /**
     * Constructor
     */
    inline IAXFormatDesc()
	: m_format(0), m_multiplier(1)
	{}

    /**
     * Get the format
     * @return The format
     */
    inline u_int32_t format() const
	{ return m_format; }

    /**
     * Get the format multiplier used to translate timestamps
     * @return The format multiplier (always greater then 0)
     */
    inline unsigned int multiplier() const
	{ return m_multiplier; }

    /**
     * Set the format
     * @param fmt The format
     * @param type Format type as IAXFormat::Media enumeration
     */
    void setFormat(u_int32_t fmt, int type);

protected:
    u_int32_t m_format;                  // The format
    unsigned int m_multiplier;           // Format multiplier derived from sampling rate
};

/**
 * This class holds the enumeration values for audio and video formats
 * @short Wrapper class for audio and video formats
 */
class YIAX_API IAXFormat
{
public:
    /**
     * Format enumeration types
     */
    enum Formats {
        G723_1 = (1 <<  0),
        GSM    = (1 <<  1),
        ULAW   = (1 <<  2),
        ALAW   = (1 <<  3),
        G726   = (1 <<  4),
        ADPCM  = (1 <<  5),
        SLIN   = (1 <<  6),
        LPC10  = (1 <<  7),
        G729   = (1 <<  8),
        SPEEX  = (1 <<  9),
        ILBC   = (1 << 10),
        G726AAL2 = (1 << 11),
        G722   = (1 << 12),
        AMR    = (1 << 13),
        // NOTE: GSM Half Rate is not defined in RFC5456
        GSM_HR    = (1 << 31),
        AudioMask = G723_1 | GSM | ULAW | ALAW | G726 | ADPCM | SLIN | LPC10 | G729 | SPEEX |
            ILBC | G726AAL2 | G722 | AMR | GSM_HR,
        JPEG   = (1 << 16),
        PNG    = (1 << 17),
        ImageMask = JPEG | PNG,
        H261   = (1 << 18),
        H263   = (1 << 19),
        H263p  = (1 << 20),
        H264   = (1 << 21),
        VideoMask = H261 | H263 | H263p | H264,
    };

    /**
     * Media type enumeration
     */
    enum Media {
        Audio = 0,
        Video,
        Image,
        TypeCount
    };

    /**
     * Constructor. Build an audio format
     * @param type Media type
    */
    inline IAXFormat(int type = Audio)
	: m_type(type)
	{}

    /**
     * Get the media type
     * @return Media type
    */
    inline int type() const
	{ return m_type; }

    /**
     * Get the format
     * @return The format
    */
    inline u_int32_t format() const
	{ return m_format.format(); }

    /**
     * Get the incoming format
     * @return The incoming format
    */
    inline u_int32_t in() const
	{ return m_formatIn.format(); }

    /**
     * Get the outgoing format
     * @return The outgoing format
    */
    inline u_int32_t out() const
	{ return m_formatOut.format(); }

    /**
     * Get the incoming or outgoing format description
     * @param in True to retrieve the incoming format, false to retrieve the outgoing one
     * @return Requested format desc
    */
    inline const IAXFormatDesc& formatDesc(bool in) const
	{ return in ? m_formatIn : m_formatOut; }

     /**
     * Get the text associated with the format
     * @return Format name
    */
    inline const char* formatName() const
	{ return formatName(format()); }

    /**
     * Get the text associated with the media type
     * @return Media name
    */
    inline const char* typeName() const
	{ return typeName(m_type); }

    /**
     * Set format
     * @param fmt Optional pointer to format to set
     * @param fmtIn Optional pointer to incoming format to set
     * @param fmtOut Optional pointer to outgoing format to set
    */
    void set(u_int32_t* fmt, u_int32_t* fmtIn, u_int32_t* fmtOut);

    /**
     * Create a string list from formats
     * @param dest The destination
     * @param formats The formats
     * @param dict Optional dictionary to use, 0 to use s_formats
     * @param sep The separator to use
    */
    static void formatList(String& dest, u_int32_t formats, const TokenDict* dict = 0,
	const char* sep = ",");

    /**
     * Pick a format from a list of capabilities
     * @param formats Capabilities list
     * @param format Optional format to pick
     * @return IAX format, 0 if not found
    */
    static u_int32_t pickFormat(u_int32_t formats, u_int32_t format = 0);

    /**
     * Encode a formats list
     * @param formats Formats list
     * @param dict Dictionary to use
     * @param sep Formats list separator
     * @return Encoded formats
    */
    static u_int32_t encode(const String& formats, const TokenDict* dict, char sep = ',');

    /**
     * Mask formats by type
     * @param value Input format(s)
     * @param type Media type to retrieve
     * @return Media format(s) from input
    */
    static inline u_int32_t mask(u_int32_t value, int type) {
	    if (type == Audio)
		return value & AudioMask;
	    if (type == Video)
		return value & VideoMask;
	    if (type == Image)
		return value & ImageMask;
	    return 0;
	}

    /**
     * Clear formats by type
     * @param value Input format(s)
     * @param type Media type to clear
     * @return Cleared format(s) from input
    */
    static inline u_int32_t clear(u_int32_t value, int type) {
	    if (type == Audio)
		return value & ~AudioMask;
	    if (type == Video)
		return value & ~VideoMask;
	    if (type == Image)
		return value & ~ImageMask;
	    return value;
	}

    /**
     * Get the text associated with a format
     * @param fmt The desired format
     * @return A pointer to the text associated with the format or 0 if the format doesn't exist
    */
    static inline const char* formatName(u_int32_t fmt)
	{ return lookup(fmt,s_formats); }

    /**
     * Get the text associated with a media type
     * @param type The media type
     * @return A pointer to the text associated with the media type
    */
    static inline const char* typeName(int type)
	{ return lookup(type,s_types); }

    /**
     * Get the text associated with a media type
     * @param type The media type
     * @return A string associated with the media type
    */
    static inline const String& typeNameStr(int type)
	{ return s_typesList[type]; }

    /**
     * Keep the texts associated with the formats
    */
    static const TokenDict s_formats[];

    /**
     * Keep the texts associated with type
     */
    static const TokenDict s_types[];

    /**
     * Keep the texts associated with a type also as String
     */
    static const String s_typesList[TypeCount];

protected:
    int m_type;
    IAXFormatDesc m_format;
    IAXFormatDesc m_formatIn;
    IAXFormatDesc m_formatOut;
};

/**
 * This class holds the enumeration values for IAX control (subclass)
 * @short Wrapper class for subclasses of frames of type IAX
 */
class YIAX_API IAXControl
{
public:
    /**
     * IAX control (subclass) enumeration types
     */
    enum Type {
        New       = 0x01,
        Ping      = 0x02,
        Pong      = 0x03,
        Ack       = 0x04,
        Hangup    = 0x05,
        Reject    = 0x06,
        Accept    = 0x07,
        AuthReq   = 0x08,
        AuthRep   = 0x09,
        Inval     = 0x0a,
        LagRq     = 0x0b,
        LagRp     = 0x0c,
        RegReq    = 0x0d,
        RegAuth   = 0x0e,
        RegAck    = 0x0f,
        RegRej    = 0x10,
        RegRel    = 0x11,
        VNAK      = 0x12,
        DpReq     = 0x13,
        DpRep     = 0x14,
        Dial      = 0x15,
        TxReq     = 0x16,
        TxCnt     = 0x17,
        TxAcc     = 0x18,
        TxReady   = 0x19,
        TxRel     = 0x1a,
        TxRej     = 0x1b,
        Quelch    = 0x1c,
        Unquelch  = 0x1d,
        Poke      = 0x1e,
	//Reserved  = 0x1f,
        MWI       = 0x20,
        Unsupport = 0x21,
        Transfer  = 0x22,
        Provision = 0x23,
        FwDownl   = 0x24,
        FwData    = 0x25,
        CallToken = 0x28,
    };

    /**
     * Get the string associated with the given IAX control type
     * @param type The requested type
     * @return The text if type is valid or 0
     */
    static inline const char* typeText(int type)
	{ return lookup(type,s_types,0); }

private:
    static TokenDict s_types[]; // Keep the association between IAX control codes and their name
};

/**
 * This class holds all data needded to manage an IAX frame
 * @short This class holds an IAX frame
 */
class YIAX_API IAXFrame : public RefObject
{
public:
    /**
     * IAX frame type enumeration
     */
    enum Type {
        DTMF    = 0x01,
        Voice   = 0x02,
        Video   = 0x03,
        Control = 0x04,
        Null    = 0x05,
        IAX     = 0x06,
        Text    = 0x07,
        Image   = 0x08,
        HTML    = 0x09,
        Noise   = 0x0a,
    };

    /**
     * Constructor. Constructs an incoming frame
     * @param type Frame type
     * @param sCallNo Source call number
     * @param tStamp Frame timestamp
     * @param retrans Retransmission flag
     * @param buf IE buffer
     * @param len IE buffer length
     * @param mark Mark flag
     */
    IAXFrame(Type type, u_int16_t sCallNo, u_int32_t tStamp, bool retrans,
	     const unsigned char* buf, unsigned int len, bool mark = false);

    /**
     * Destructor
     */
    virtual ~IAXFrame();

    /**
     * Get the type of this frame as enumeration
     * @return The type of this frame as enumeration
     */
    inline Type type() const
	{ return m_type; }

    /**
     * Get the data buffer of the frame
     * @return The data buffer of the frame
     */
    inline DataBlock& data()
        { return m_data; }

    /**
     * Get the retransmission flag of this frame
     * @return The retransmission flag of this frame
     */
    inline bool retrans() const
	{ return m_retrans; }

    /**
     * Get the source call number of this frame
     * @return The source call number of this frame
     */
    inline u_int16_t sourceCallNo() const
	{ return m_sCallNo; }

    /**
     * Get the timestamp of this frame
     * @return The timestamp of this frame
     */
    inline u_int32_t timeStamp() const
	{ return m_tStamp; }

    /**
     * Get the mark flag
     * @return The mark flag
     */
    inline bool mark() const
	{ return m_mark; }

    /**
     * Get a pointer to this frame if it is a full frame
     * @return A pointer to this frame if it is a full frame or 0
     */
    virtual IAXFullFrame* fullFrame();

    /**
     * Parse a received buffer and returns a IAXFrame pointer if valid
     * @param buf Received buffer
     * @param len Buffer length
     * @param engine The IAXEngine who requested the operation
     * @param addr The source address
     * @return A frame pointer on success or 0
     */
    static IAXFrame* parse(const unsigned char* buf, unsigned int len, IAXEngine* engine = 0, const SocketAddr* addr = 0);

    /**
     * Build a miniframe buffer
     * @param dest Destination buffer
     * @param sCallNo Source call number
     * @param ts Frame timestamp
     * @param data Data
     * @param len Data length
     */
    static inline void buildMiniFrame(DataBlock& dest, u_int16_t sCallNo, u_int32_t ts,
	void* data, unsigned int len) {
	    unsigned char header[4] = {(unsigned char)(sCallNo >> 8),
		(unsigned char)sCallNo,(unsigned char)(ts >> 8),(unsigned char)ts};
	    dest.assign(header,4);
	    dest.append(data,len);
	}

    /**
     * Build a video meta frame buffer
     * @param dest Destination buffer
     * @param sCallNo Source call number
     * @param tStamp Frame timestamp
     * @param mark Frame mark
     * @param data Data
     * @param len Data length
     */
    static void buildVideoMetaFrame(DataBlock& dest, u_int16_t sCallNo, u_int32_t tStamp,
	bool mark, void* data, unsigned int len);

    /**
     * Pack a subclass value according to IAX protocol
     * @param value Value to pack
     * @return The packed subclass value or 0 if invalid (>255 and not a power of 2)
     */
    static u_int8_t packSubclass(u_int32_t value);

    /**
     * Unpack a subclass value according to IAX protocol
     * @param value Value to unpack
     * @return The unpacked subclass value
     */
    static u_int32_t unpackSubclass(u_int8_t value);

    /**
     * Get the string associated with the given IAX frame type
     * @param type The requested type
     * @return The text if type is valid or 0
     */
    static inline const char* typeText(int type)
	{ return lookup(type,s_types,0); }

protected:
    /**
     * Contains the frame's IE list for an incoming frame or the whole frame for an outgoing one
     */
    DataBlock m_data;

    /**
     * Retransmission flag
     */
    bool m_retrans;

private:
    static TokenDict s_types[]; // Keep the association between IAX frame types and their names
    Type m_type;		// Frame type
    u_int16_t m_sCallNo;	// Source call number
    u_int32_t m_tStamp;		// Frame timestamp
    bool m_mark;		// Mark flag
};

/**
 * This class holds all data needded to manage an IAX full frame
 * @short This class holds an IAX full frame
 */
class YIAX_API IAXFullFrame : public IAXFrame
{
public:
    /**
     * IAX frame subclass enumeration types for frames of type Control
     */
    enum ControlType {
        Hangup = 0x01,
        //Ring = 0x02,
        Ringing = 0x03,
        Answer = 0x04,
        Busy = 0x05,
        Congestion = 0x08,
        FlashHook = 0x09,
        Option = 0x0b,
        KeyRadio = 0x0c,
        UnkeyRadio = 0x0d,
        Progressing = 0x0e,
        Proceeding = 0x0f,
        Hold = 0x10,
        Unhold = 0x11,
        VidUpdate = 0x12,
        SrcUpdate = 0x14,
        StopSounds = 0xff,
    };

    /**
     * Constructor. Constructs an incoming full frame
     * @param type Frame type
     * @param subclass Frame subclass
     * @param sCallNo Source (remote) call number
     * @param dCallNo Destination (local) call number
     * @param oSeqNo Outgoing sequence number
     * @param iSeqNo Incoming (expected) sequence number
     * @param tStamp Frame timestamp
     * @param retrans Retransmission flag
     * @param buf IE buffer
     * @param len IE buffer length
     * @param mark Mark flag
     */
    IAXFullFrame(Type type, u_int32_t subclass, u_int16_t sCallNo, u_int16_t dCallNo,
		 unsigned char oSeqNo, unsigned char iSeqNo,
		 u_int32_t tStamp, bool retrans,
		 const unsigned char* buf, unsigned int len, bool mark = false);

    /**
     * Constructor. Constructs an outgoing full frame
     * @param type Frame type
     * @param subclass Frame subclass
     * @param sCallNo Source (remote) call number
     * @param dCallNo Destination (local) call number
     * @param oSeqNo Outgoing sequence number
     * @param iSeqNo Incoming (expected) sequence number
     * @param tStamp Frame timestamp
     * @param buf IE buffer
     * @param len IE buffer length
     * @param mark Mark flag
     */
    IAXFullFrame(Type type, u_int32_t subclass, u_int16_t sCallNo, u_int16_t dCallNo,
		 unsigned char oSeqNo, unsigned char iSeqNo,
		 u_int32_t tStamp,
		 const unsigned char* buf = 0, unsigned int len = 0, bool mark = false);

    /**
     * Constructor. Constructs an outgoing full frame
     * @param type Frame type
     * @param subclass Frame subclass
     * @param sCallNo Source (remote) call number
     * @param dCallNo Destination (local) call number
     * @param oSeqNo Outgoing sequence number
     * @param iSeqNo Incoming (expected) sequence number
     * @param tStamp Frame timestamp
     * @param ieList List of frame IEs
     * @param maxlen Max frame data length
     * @param mark Mark flag
     */
    IAXFullFrame(Type type, u_int32_t subclass, u_int16_t sCallNo, u_int16_t dCallNo,
		 unsigned char oSeqNo, unsigned char iSeqNo,
		 u_int32_t tStamp, IAXIEList* ieList, u_int16_t maxlen, bool mark = false);

    /**
     * Destructor
     */
    virtual ~IAXFullFrame();

    /**
     * Get the destination call number
     * @return The destination call number
     */
    inline u_int16_t destCallNo() const
        { return m_dCallNo; }

    /**
     * Get the outgoing sequence number
     * @return The outgoing sequence number
     */
    inline unsigned char oSeqNo() const
        { return m_oSeqNo; }

    /**
     * Get the incoming sequence number
     * @return The incoming sequence number
     */
    inline unsigned char iSeqNo() const
        { return m_iSeqNo; }

    /**
     * Get the subclass of this frame
     * @return The subclass of this frame
     */
    inline u_int32_t subclass() const
	{ return m_subclass; }

    /**
     * Check if this frame is used to request authentication
     * @return True if this frame is used to request authentication (like RegReq or RegAuth)
     */
    inline bool isAuthReq() const {
	    return type() == IAXFrame::IAX &&
		(subclass() == IAXControl::AuthReq || subclass() == IAXControl::RegAuth);
	}

    /**
     * Check if this frame is an INVAL one
     * @return True if this frame is INVAL
     */
    inline bool isInval() const
	{ return type() == IAXFrame::IAX && subclass() == IAXControl::Inval; }

    /**
     * Get a pointer to this frame if it is a full frame
     * @return A pointer to this frame
     */
    virtual IAXFullFrame* fullFrame();

    /**
     * Rebuild frame buffer from the list of IEs
     * @param maxlen Max frame data length
     */
    void updateBuffer(u_int16_t maxlen);

    /**
     * Retrieve the IE list
     * @return IAXIEList pointer or NULL
     */
    inline IAXIEList* ieList()
	{ return m_ieList; }

    /**
     * Update IE list from buffer if not already done
     * @param incoming True if this is an incoming frame
     * @return True if the list is valid
     */
    bool updateIEList(bool incoming);

    /**
     * Remove the IE list
     * @param delObj True to delete it
     * @return IAXIEList pointer or NULL if requested to delete it or already NULL
     */
    IAXIEList* removeIEList(bool delObj = true);

    /**
     * Fill a string with this frame
     * @param dest The string to fill
     * @param local The local address
     * @param remote The remote address
     * @param incoming True if it is an incoming frame
     */
    void toString(String& dest, const SocketAddr& local, const SocketAddr& remote,
	bool incoming);

    /**
     * Get the string associated with the given IAX control type
     * @param type The requested control type
     * @return The text if type is valid or 0
     */
    static inline const char* controlTypeText(int type)
	{ return lookup(type,s_controlTypes,0); }

protected:
    /**
     * Destroyed notification. Clear data
     */
    virtual void destroyed();

private:
    // Build frame buffer header
    void setDataHeader();
    static TokenDict s_controlTypes[]; // Keep the association between control types and their names
    u_int16_t m_dCallNo;	// Destination call number
    unsigned char m_oSeqNo;	// Out sequence number
    unsigned char m_iSeqNo;	// In sequence number
    u_int32_t m_subclass;	// Subclass
    IAXIEList* m_ieList;        // List of IEs
};

/**
 * This class holds all data needded to manage an outgoing IAX full frame
 * @short This class holds an outgoing IAX full frame
 */
class YIAX_API IAXFrameOut : public IAXFullFrame
{
public:
    /**
     * Constructor. Constructs an outgoing full frame
     * @param type Frame type
     * @param subclass Frame subclass
     * @param sCallNo Source (remote) call number
     * @param dCallNo Destination (local) call number
     * @param oSeqNo Outgoing sequence number
     * @param iSeqNo Incoming (expected) sequence number
     * @param tStamp Frame timestamp
     * @param buf IE buffer
     * @param len IE buffer length
     * @param retransCount Retransmission counter
     * @param retransIntervalMs Time interval to the next retransmission
     * @param ackOnly Acknoledge only flag. If true, the frame only expects an ACK
     * @param mark Mark flag
     */
    inline IAXFrameOut(Type type, u_int32_t subclass, u_int16_t sCallNo, u_int16_t dCallNo,
                       unsigned char oSeqNo, unsigned char iSeqNo, u_int32_t tStamp,
		       const unsigned char* buf, unsigned int len,
                       u_int16_t retransCount, u_int32_t retransIntervalMs,
		       bool ackOnly, bool mark = false)
        : IAXFullFrame(type,subclass,sCallNo,dCallNo,oSeqNo,iSeqNo,tStamp,buf,len,mark),
          m_ack(false), m_ackOnly(ackOnly), m_retransCount(retransCount),
          m_retransTimeInterval(retransIntervalMs * 1000),
	  m_nextTransTime(Time::now() + m_retransTimeInterval)
	{}

    /**
     * Constructor. Constructs an outgoing full frame
     * @param type Frame type
     * @param subclass Frame subclass
     * @param sCallNo Source (remote) call number
     * @param dCallNo Destination (local) call number
     * @param oSeqNo Outgoing sequence number
     * @param iSeqNo Incoming (expected) sequence number
     * @param tStamp Frame timestamp
     * @param ieList List of frame IEs
     * @param maxlen Max frame data length
     * @param retransCount Retransmission counter
     * @param retransIntervalMs Time interval to the next retransmission
     * @param ackOnly Acknoledge only flag. If true, the frame only expects an ACK
     * @param mark Mark flag
     */
    inline IAXFrameOut(Type type, u_int32_t subclass, u_int16_t sCallNo, u_int16_t dCallNo,
                       unsigned char oSeqNo, unsigned char iSeqNo, u_int32_t tStamp,
		       IAXIEList* ieList, u_int16_t maxlen,
                       u_int16_t retransCount, u_int32_t retransIntervalMs, bool ackOnly,
		       bool mark = false)
        : IAXFullFrame(type,subclass,sCallNo,dCallNo,oSeqNo,iSeqNo,tStamp,ieList,maxlen,mark),
          m_ack(false), m_ackOnly(ackOnly), m_retransCount(retransCount),
          m_retransTimeInterval(retransIntervalMs * 1000),
	  m_nextTransTime(Time::now() + m_retransTimeInterval)
	{}

    /**
     * Destructor
     */
    virtual ~IAXFrameOut()
	{}

    /**
     * Get the retransmission counter of this frame
     * @return The retransmission counter is 0
     */
    inline unsigned int retransCount() const
        { return m_retransCount; }

    /**
     * Ask the frame if it's time for retransmit
     * @param time Current time
     * @return True if it's time to retransmit
     */
    inline bool timeForRetrans(u_int64_t time) const
        { return time >= m_nextTransTime; }

    /**
     * Set the retransmission flag of this frame
     */
    inline void setRetrans() {
	    if (m_retrans)
		return;
	    m_retrans = true;
	    ((unsigned char*)m_data.data())[2] |= 0x80;
	}

    /**
     * Update the retransmission counter and the time to next retransmission
     */
    inline void transmitted() {
	    if (!m_retransCount)
		return;
	    m_retransCount--;
	    m_retransTimeInterval *= 2;
	    m_nextTransTime += m_retransTimeInterval;
	}

    /**
     * Get the acknoledged flag of this frame
     * @return The acknoledged flag of this frame
     */
    inline bool ack() const
	{ return m_ack; }

    /**
     * Set the acknoledged flag of this frame
     */
    inline void setAck()
	{ m_ack = true; }

    /**
     * Get the acknoledge only flag of this frame
     * @return The acknoledge only flag of this frame
     */
    inline bool ackOnly() const
	{ return m_ackOnly; }

    /**
     * Check if absolute timeout can be set
     * @return True if absolute timeout can be set
     */
    inline bool canSetTimeout()
	{ return m_retransTimeInterval != 0; }

    /**
     * Set absolute timeout. Reset retransmission counter
     * @param tout Timeout time
     */
    inline void setTimeout(u_int64_t tout) {
	    if (!m_retransTimeInterval)
		return;
	    m_retransTimeInterval = 0;
	    m_retransCount = 0;
	    m_nextTransTime = tout;
	}

private:
    bool m_ack;				// Acknoledge flag
    bool m_ackOnly;			// Frame need only ACK as a response
    u_int16_t m_retransCount;		// Retransmission counter
    u_int32_t m_retransTimeInterval;	// Retransmission interval
    u_int64_t m_nextTransTime;		// Next transmission time
};

/**
 * This class holds trunk description
 * @short Trunk info
 */
class YIAX_API IAXTrunkInfo : public RefObject
{
public:
    /**
     * Constructor
     */
    inline IAXTrunkInfo()
	: m_timestamps(true), m_sendInterval(IAX2_TRUNKFRAME_SEND_DEF),
	m_maxLen(IAX2_TRUNKFRAME_LEN_DEF),
	m_efficientUse(false), m_trunkInSyncUsingTs(true),
	m_trunkInTsDiffRestart(5000),
	m_retransCount(IAX2_RETRANS_COUNT_DEF),
	m_retransInterval(IAX2_RETRANS_INTERVAL_DEF),
	m_pingInterval(IAX2_PING_INTERVAL_DEF)
	{}

    /**
     * Init non trunking related data
     * @param params Parameter list
     * @param prefix Parameter prefix
     * @param def Optional defaults
     */
    void init(const NamedList& params, const String& prefix = String::empty(),
	const IAXTrunkInfo* def = 0);

    /**
     * Init trunking from parameters
     * @param params Parameter list
     * @param prefix Parameter prefix
     * @param def Optional defaults
     * @param out True to init outgoing trunk data
     * @param in True to init incoming trunk data
     */
    void initTrunking(const NamedList& params, const String& prefix = String::empty(),
	const IAXTrunkInfo* def = 0, bool out = true, bool in = true);

    /**
     * Update trunking from parameters. Don't change values not present in list
     * @param params Parameter list
     * @param prefix Parameter prefix
     * @param out True to update outgoing trunk data
     * @param in True to update incoming trunk data
     */
    void updateTrunking(const NamedList& params, const String& prefix = String::empty(),
	bool out = true, bool in = true);

    /**
     * Dump info
     * @param buf Destination buffer
     * @param sep Parameters separator
     * @param out True to dump outgoing trunking info
     * @param in True to dump incoming trunking info
     * @param other True to dump non trunking info
     */
    void dump(String& buf, const char* sep = " ", bool out = true, bool in = true,
	bool other = true);

    bool m_timestamps;                   // Trunk type: with(out) timestamps
    unsigned int m_sendInterval;         // Send interval
    unsigned int m_maxLen;               // Max frame length
    bool m_efficientUse;                 // Outgoing trunking: use or not the trunk based on calls using it
    bool m_trunkInSyncUsingTs;           // Incoming trunk without timestamps: use trunk
                                         //  time or trunk timestamp to re-build frame ts
    u_int32_t m_trunkInTsDiffRestart;    // Incoming trunk without timestamp: diff between
                                         //  timestamps at which we restart
    unsigned int m_retransCount;         // Frame retransmission counter
    unsigned int m_retransInterval;      // Frame retransmission interval in milliseconds
    unsigned int m_pingInterval;         // Ping interval in milliseconds
};

/**
 * Handle meta trunk frame with timestamps
 * @short Meta trunk frame
 */
class YIAX_API IAXMetaTrunkFrame : public RefObject, public Mutex
{
public:
    /**
     * Constructor. Constructs an outgoing meta trunk frame
     * @param engine The engine that owns this frame
     * @param addr Remote peer address
     * @param timestamps True if miniframes have timestamps, false if not
     * @param maxLen Maximum frame length
     * @param sendInterval Trunk send interval in milliseconds
     */
    IAXMetaTrunkFrame(IAXEngine* engine, const SocketAddr& addr, bool timestamps,
	unsigned int maxLen, unsigned int sendInterval);

    /**
     * Destructor
     */
    virtual ~IAXMetaTrunkFrame();

    /**
     * Get the remote peer address
     * @return The remote peer address
     */
    inline const SocketAddr& addr() const
	{ return m_addr; }

    /**
     * Retrieve the number of calls using this trunk
     * @return The number of calls using this trunk
     */
    inline unsigned int calls() const
	{ return m_calls; }

    /**
     * Change the number of calls using this trunk
     * @param add True to add a call, false to remove it
     */
    inline void changeCalls(bool add) {
	    Lock lck(this);
	    if (add)
		m_calls++;
	    else if (m_calls)
		m_calls--;
	}

    /**
     * Check if the frame is adding mini frames timestamps
     * @return True if the frame is adding mini frames timestamps
     */
    inline bool trunkTimestamps() const
	{ return m_trunkTimestamps; }

    /**
     * Retrieve the send interval
     * @return Send interval in milliseconds
     */
    inline unsigned int sendInterval() const
	{ return m_sendInterval; }

    /**
     * Retrieve the frame maximum length
     * @return Frame maximum length
     */
    inline unsigned int maxLen() const
	{ return m_maxLen; }

    /**
     * Add a mini frame. If no room, send before adding
     * @param sCallNo Sorce call number
     * @param data Mini frame data
     * @param tStamp Mini frame timestamp
     * @return The number of data bytes added to trunk, 0 on failure
     */
    unsigned int add(u_int16_t sCallNo, const DataBlock& data, u_int32_t tStamp);

    /**
     * Send this frame to remote peer if the time arrived
     * @param now Current time
     * @return The result of the write operation
     */
    inline bool timerTick(const Time& now = Time()) {
	    if (m_dataAddIdx == IAX2_TRUNKFRAME_HEADERLENGTH || !m_send)
		return false;
	    Lock lck(this);
	    return (now > m_send) && doSend(now,true);
	}

    /**
     * Send this frame to remote peer if there is any data in buffer
     * @return The result of the write operation
     */
    inline bool send() {
	    if (m_dataAddIdx == IAX2_TRUNKFRAME_HEADERLENGTH)
		return false;
	    Lock lck(this);
	    return m_dataAddIdx != IAX2_TRUNKFRAME_HEADERLENGTH && doSend();
	}

private:
    IAXMetaTrunkFrame() {}      // No default constructor
    // Send this frame to remote peer
    bool doSend(const Time& now = Time(), bool onTime = false);
    // Set timestamp and next time to send
    inline void setTimestamp(u_int64_t now) {
	    m_timeStamp = now;
	    m_send = now + (u_int64_t)m_sendInterval * 1000;
	}
    // Set next time to send
    inline void setSendTime(u_int64_t now)
	{ m_send = now + (u_int64_t)m_sendInterval * 1000; }

    // Set the timestamp of this frame
    inline void setTimestamp(u_int32_t tStamp) {
            m_data[4] = (u_int8_t)(tStamp >> 24);
	    m_data[5] = (u_int8_t)(tStamp >> 16);
	    m_data[6] = (u_int8_t)(tStamp >> 8);
	    m_data[7] = (u_int8_t)tStamp;
	}

    unsigned int m_calls;       // The number of calls using it
    u_int8_t* m_data;		// Data buffer
    u_int16_t m_dataAddIdx;	// Current add index
    u_int64_t m_timeStamp;      // First time data was added
    u_int64_t m_send;           // Time to send
    u_int32_t m_lastSentTs;     // Last sent timestamp
    unsigned int m_sendInterval;// Send interval in milliseconds
    IAXEngine* m_engine;	// The engine that owns this frame
    SocketAddr m_addr;		// Remote peer address
    bool m_trunkTimestamps;     // Trunk type: with(out) timestamps
    unsigned int m_maxLen;      // Max frame length
    unsigned int m_maxDataLen;  // Max frame data length
    unsigned char m_miniHdrLen; // Miniframe header length
};

/**
 * This class holds data used by transaction to sync media.
 * The mutexes are not reentrant
 * @short IAX2 transaction media data
 */
class YIAX_API IAXMediaData
{
    friend class IAXTransaction;
public:
    /**
     * Constructor
     */
    inline IAXMediaData()
	: m_inMutex(false,"IAXTransaction::InMedia"),
	m_outMutex(false,"IAXTransaction::OutMedia"),
	m_startedIn(false), m_startedOut(false),
	m_outStartTransTs(0), m_outFirstSrcTs(0),
	m_lastOut(0), m_lastIn(0), m_sent(0), m_sentBytes(0),
	m_recv(0), m_recvBytes(0), m_ooPackets(0), m_ooBytes(0),
	m_showInNoFmt(true), m_showOutOldTs(true),
	m_dropOut(0), m_dropOutBytes(0)
	{}

    /**
     * Increase drop out data
     * @param len The number of dropped bytes
     */
    inline void dropOut(unsigned int len) {
	    if (len) {
		m_dropOut++;
		m_dropOutBytes += len;
	    }
	}

    /**
     * Print statistics
     * @param buf Destination buffer
     */
    void print(String& buf);

protected:
    Mutex m_inMutex;
    Mutex m_outMutex;
    bool m_startedIn;                    // Incoming media started
    bool m_startedOut;                   // Outgoing media started
    int m_outStartTransTs;               // Transaction timestamp where media send started
    unsigned int m_outFirstSrcTs;        // First outgoing source packet timestamp as received from source
    u_int32_t m_lastOut;                 // Last transmitted mini timestamp
    u_int32_t m_lastIn;                  // Last received timestamp
    unsigned int m_sent;                 // Packets sent
    unsigned int m_sentBytes;            // Bytes sent
    unsigned int m_recv;                 // Packets received
    unsigned int m_recvBytes;            // Bytes received
    unsigned int m_ooPackets;            // Dropped received out of order packets
    unsigned int m_ooBytes;              // Dropped received out of order bytes
    bool m_showInNoFmt;                  // Show incoming media arrival without format debug
    bool m_showOutOldTs;                 // Show dropped media out debug message
    unsigned int m_dropOut;              // The number of dropped outgoing packets
    unsigned int m_dropOutBytes;         // The number of dropped outgoing bytes
};

/**
 * This class holds all the data needded for the management of an IAX2 transaction
 *  which might be a call leg, a register/unregister or a poke one
 * @short An IAX2 transaction
 */
class YIAX_API IAXTransaction : public RefObject, public Mutex
{
    friend class IAXEvent;
    friend class IAXEngine;
public:
    /**
     * The transaction type as enumeration
     */
    enum Type {
	Incorrect,			// Unsupported/unknown type
	New,				// Media exchange call
	RegReq,				// Registration
	RegRel,				// Registration release
	Poke,				// Ping
	//FwDownl,
    };

    /**
     * The transaction state as enumeration
     */
    enum State {
        Connected,		     	// Call leg established (Accepted) for transactions of type New
	NewLocalInvite,		     	// New outgoing transaction: Poke/New/RegReq/RegRel
	NewLocalInvite_AuthRecv,     	// Auth request received for an outgoing transaction
	NewLocalInvite_RepSent,	     	// Auth reply sent for an outgoing transaction
	NewRemoteInvite,             	// New incoming transaction: Poke/New/RegReq/RegRel
	NewRemoteInvite_AuthSent,    	// Auth sent for an incoming transaction
	NewRemoteInvite_RepRecv,     	// Auth reply received for an incoming transaction
	Unknown,                     	// Initial state
	Terminated,                  	// Terminated. No more frames accepted
        Terminating,                 	// Terminating. Wait for ACK or timeout to terminate
    };

    /**
     * Constructs an incoming transaction from a received full frame with an IAX
     *  control message that needs a new transaction
     * @param engine The engine that owns this transaction
     * @param frame A valid full frame
     * @param lcallno Local call number
     * @param addr Address from where the frame was received
     * @param data Pointer to arbitrary user data
     */
    static IAXTransaction* factoryIn(IAXEngine* engine, IAXFullFrame* frame, u_int16_t lcallno, const SocketAddr& addr,
		void* data = 0);

    /**
     * Constructs an outgoing transaction with an IAX control message that needs a new transaction
     * @param engine The engine that owns this transaction
     * @param type Transaction type
     * @param lcallno Local call number
     * @param addr Address to use
     * @param ieList Starting IE list
     * @param data Pointer to arbitrary user data
     */
    static IAXTransaction* factoryOut(IAXEngine* engine, Type type, u_int16_t lcallno, const SocketAddr& addr,
		IAXIEList& ieList, void* data = 0);

    /**
     * Destructor
     */
    virtual ~IAXTransaction();

    /**
     * The IAX engine this transaction belongs to
     * @return Pointer to the IAXEngine of this transaction
     */
    inline IAXEngine* getEngine() const
        { return m_engine; }

    /**
     * Get the type of this transaction
     * @return The type of the transaction as enumeration
     */
    inline Type type() const
        { return m_type; }

    /**
     * Retrieve transaction type name
     * @return Transaction type name
     */
    inline const char* typeName()
	{ return typeName(type()); }

    /**
     * Get the state of this transaction
     * @return The state of the transaction as enumeration
     */
    inline State state() const
        { return m_state; }

    /**
     * Retrieve the transaction state name
     * @return Transaction state name
     */
    inline const char* stateName()
	{ return stateName(state()); }

    /**
     * Get the timestamp of this transaction
     * @return The timestamp of this transaction
     */
    inline u_int64_t timeStamp() const
        { return Time::msecNow() - m_timeStamp; }

    /**
     * Get the direction of this transaction
     * @return True if it is an outgoing transaction
     */
    inline bool outgoing() const
        { return m_localInitTrans; }

    /**
     * Store a pointer to arbitrary user data
     * @param data User provided pointer
     */
    inline void setUserData(void* data)
        { m_userdata = data; }

    /**
     * Return the opaque user data stored in the transaction
     * @return Pointer set by user
     */
    inline void* getUserData() const
        { return m_userdata; }

    /**
     * Retrieve the local call number
     * @return 15-bit local call number
     */
    inline u_int16_t localCallNo() const
        { return m_lCallNo; }

    /**
     * Retrieve the remote call number
     * @return 15-bit remote call number
     */
    inline u_int16_t remoteCallNo() const
        { return m_rCallNo; }

    /**
     * Retrieve the remote host+port address
     * @return A reference to the remote address
     */
    inline const SocketAddr& remoteAddr() const
        { return m_addr; }

    /**
     * Retrieve the username
     * @return A reference to the username
     */
    inline const String& username()
	{ return m_username; }

    /**
     * Retrieve the calling number
     * @return A reference to the calling number
     */
    inline const String& callingNo()
	{ return m_callingNo; }

    /**
     * Retrieve the calling name
     * @return A reference to the calling name
     */
    inline const String& callingName()
	{ return m_callingName; }

    /**
     * Retrieve the called number
     * @return A reference to the called number
     */
    inline const String& calledNo()
	{ return m_calledNo; }

    /**
     * Retrieve the called context
     * @return A reference to the called context
     */
    inline const String& calledContext()
	{ return m_calledContext; }

    /**
     * Retrieve the challenge sent/received during authentication
     * @return A reference to the challenge
     */
    inline const String& challenge()
	{ return m_challenge; }

    /**
     * Retrieve the media of a given type
     * @param type Media type to retrieve
     * @return IAXFormat pointer or 0 for invalid type
     */
    inline IAXFormat* getFormat(int type) {
	    if (type == IAXFormat::Audio)
		return &m_format;
	    if (type == IAXFormat::Video)
		return &m_formatVideo;
	    return 0;
	}

    /**
     * Retrieve the media data for a given type
     * @param type Media type to retrieve
     * @return IAXMediaData pointer or 0 for invalid type
     */
    inline IAXMediaData* getData(int type) {
	    if (type == IAXFormat::Audio)
		return &m_dataAudio;
	    if (type == IAXFormat::Video)
		return &m_dataVideo;
	    return 0;
	}

    /**
     * Retrieve the media format used during initialization
     * @param type Media type to retrieve
     * @return The initial media format for the given type
     */
    inline u_int32_t format(int type) {
	    IAXFormat* fmt = getFormat(type);
	    return fmt ? fmt->format() : 0;
	}

    /**
     * Retrieve the incoming media format
     * @param type Media type to retrieve
     * @return The incoming media format for the given type
     */
    inline u_int32_t formatIn(int type) {
	    IAXFormat* fmt = getFormat(type);
	    return fmt ? fmt->in() : 0;
	}

    /**
     * Retrieve the outgoing media format
     * @param type Media type to retrieve
     * @return The outgoing media format for the given type
     */
    inline u_int32_t formatOut(int type) {
	    IAXFormat* fmt = getFormat(type);
	    return fmt ? fmt->out() : 0;
	}

    /**
     * Retrieve the media capability of this transaction
     * @return The media capability of this transaction
     */
    inline u_int32_t capability() const
	{ return m_capability; }

    /**
     * Retrieve the expiring time for a register/unregister transaction
     * @return The expiring time for a register/unregister transaction
     */
    inline u_int32_t expire() const
	{ return m_expire; }

    /**
     * Retrieve the authentication data sent/received during authentication
     * @return A reference to the authentication data
     */
    inline const String& authdata()
	{ return m_authdata; }

    /**
     * Set the destroy flag
     */
    inline void setDestroy()
	{ m_destroy = true; }

    /**
     * Start an outgoing transaction.
     * This method is thread safe
     */
    void start();

    /**
     * Process a frame from remote peer.
     * This method is thread safe
     * @param frame IAX frame belonging to this transaction to process
     * @return 'this' if successful or NULL if the frame is invalid
     */
    IAXTransaction* processFrame(IAXFrame* frame);

    /**
     * Process received media data
     * @param data Received data
     * @param tStamp Mini frame timestamp multiplied by format multiplier
     * @param type Media type
     * @param full True if received in a full frame
     * @param mark Mark flag
     * @return 0
     */
    IAXTransaction* processMedia(DataBlock& data, u_int32_t tStamp,
	int type = IAXFormat::Audio, bool full = false, bool mark = false);

    /**
     * Send media data to remote peer. Update the outgoing media format if changed
     * @param data Data to send
     * @param tStamp Data timestamp
     * @param format Data format
     * @param type Media type
     * @param mark Mark flag
     * @return The number of bytes sent
     */
    unsigned int sendMedia(const DataBlock& data, unsigned int tStamp, u_int32_t format,
	int type = IAXFormat::Audio, bool mark = false);

    /**
     * Get an IAX event from the queue
     * This method is thread safe.
     * @param now Current time
     * @return Pointer to an IAXEvent or 0 if none available
     */
    IAXEvent* getEvent(const Time& now = Time());

    /**
     * Get the maximum allowed number of full frames in the incoming frame list
     * @return The maximum allowed number of full frames in the incoming frame list
     */
    static unsigned char getMaxFrameList();

    /**
     * Set the maximum allowed number of full frames in the incoming frame list
     * @param value The new value of m_maxInFrames
     * @return False if value is greater then IAX2_MAX_TRANSINFRAMELIST
     */
    static bool setMaxFrameList(unsigned char value);

    /**
     * Send an ANSWER frame to remote peer
     * This method is thread safe
     * @return False if the current transaction state is not Connected
     */
    inline bool sendAnswer()
	{ return sendConnected(IAXFullFrame::Answer); }

    /**
     * Send a RINGING frame to remote peer
     * This method is thread safe
     * @return False if the current transaction state is not Connected
     */
    inline bool sendRinging()
	{ return sendConnected(IAXFullFrame::Ringing); }

    /**
     * Send a PROCEEDING frame to remote peer
     * This method is thread safe
     * @return False if the current transaction state is not Connected
     */
    inline bool sendProgress()
	{ return sendConnected(IAXFullFrame::Proceeding); }

    /**
     * Send an ACCEPT/REGACK frame to remote peer
     * This method is thread safe
     * @param expires Optional pointer to expiring time for register transactions
     * @return False if the transaction type is not New and state is NewRemoteInvite or NewRemoteInvite_AuthRep or
     *  if the transaction type is not RegReq and state is NewRemoteInvite or
     *  type is not RegReq/RegRel and state is NewRemoteInvite_AuthRep
     */
    bool sendAccept(unsigned int* expires = 0);

    /**
     * Send a HANGUP frame to remote peer
     * This method is thread safe
     * @param cause Optional reason for hangup
     * @param code Optional code of reason
     * @return False if the transaction type is not New or state is Terminated/Terminating
     */
    bool sendHangup(const char* cause = 0, u_int8_t code = 0);

    /**
     * Send a REJECT/REGREJ frame to remote peer
     * This method is thread safe
     * @param cause Optional reason for reject
     * @param code Optional code of reason
     * @return False if the transaction type is not New/RegReq/RegRel or state is Terminated/Terminating
     */
    bool sendReject(const char* cause = 0, u_int8_t code = 0);

    /**
     * Send an AUTHREQ/REGAUTH frame to remote peer
     * This method is thread safe
     * @return False if the current transaction state is not NewRemoteInvite
     */
    bool sendAuth();

    /**
     * Send an AUTHREP/REGREQ/REGREL frame to remote peer as a response to AUTHREQ/REGREQ/REGREL
     * This method is thread safe
     * @param response Response to send
     * @return False if the current transaction state is not NewLocalInvite_AuthRecv
     */
    bool sendAuthReply(const String& response);

    /**
     * Send a DTMF frame to remote peer
     * This method is thread safe
     * @param dtmf DTMF char to send
     * @return False if the current transaction state is not Connected or dtmf is grater then 127
     */
    inline bool sendDtmf(u_int8_t dtmf)
	{ return dtmf <= 127 ? sendConnected((IAXFullFrame::ControlType)dtmf,IAXFrame::DTMF) : false; }

    /**
     * Send a TEXT frame to remote peer
     * This method is thread safe
     * @param text Text to send
     * @return False if the current transaction state is not Connected
     */
    bool sendText(const char* text);

    /**
     * Send a NOISE frame to remote peer
     * This method is thread safe
     * @param noise Noise value to send
     * @return False if the current transaction state is not Connected or noise is grater then 127
     */
    inline bool sendNoise(u_int8_t noise)
	{ return noise <= 127 ? sendConnected((IAXFullFrame::ControlType)noise,IAXFrame::Noise) : false; }

    /**
     * Abort a registration transaction
     * This method is thread safe
     * @return False transaction is not a registration one or is already terminating
     */
    bool abortReg();

    /**
     * Enable trunking for this transaction
     * @param trunkFrame Pointer to IAXMetaTrunkFrame used to send trunked media
     * @param efficientUse Use or not the trunk based on calls using it
     * @return False trunking is already enabled for this transactio or trunkFrame is 0
     */
    bool enableTrunking(IAXMetaTrunkFrame* trunkFrame, bool efficientUse);

    /**
     * Process a received call token
     * This method is thread safe
     * @param callToken Received call token
     */
    void processCallToken(const DataBlock& callToken);

    /**
     * Process incoming audio miniframes from trunk without timestamps
     * @param ts Trunk frame timestamp
     * @param blocks Received blocks
     * @param now Current time
     */
    void processMiniNoTs(u_int32_t ts, ObjList& blocks, const Time& now = Time());

    /**
     * Print transaction data on stdin
     * @param printStats True to print media statistics
     * @param printFrames True to print in/out pending frames
     * @param location Additional location info to be shown in debug
     */
    void print(bool printStats = false, bool printFrames = false, const char* location = "status");

    /**
     * Retrieve transaction type name from transaction type
     * @param type Transaction type
     * @return Requested type name
     */
    static inline const char* typeName(int type)
	{ return lookup(type,s_typeName); }

    /**
     * Retrieve transaction state name
     * @param state Transaction state
     * @return Requested state name
     */
    static inline const char* stateName(int state)
	{ return lookup(state,s_stateName); }

    /**
     * Transaction type name
     */
    static const TokenDict s_typeName[];

    /**
     * Transaction state name
     */
    static const TokenDict s_stateName[];

    /**
     * Standard message sent if unsupported/unknown/none authentication methosd was received
     */
    static String s_iax_modNoAuthMethod;

    /**
     * Standard message sent if unsupported/unknown/none media format was received
     */
    static String s_iax_modNoMediaFormat;

    /**
     * Standard message sent if the received authentication data is incorrect
     */
    static String s_iax_modInvalidAuth;

    /**
     * Standard message sent if a received frame doesn't have an username information element
     */
    static String s_iax_modNoUsername;

protected:
    /**
     * Constructor: constructs an incoming transaction from a received full frame with an IAX
     *  control message that needs a new transaction
     * @param engine The engine that owns this transaction
     * @param frame A valid full frame
     * @param lcallno Local call number
     * @param addr Address from where the frame was received
     * @param data Pointer to arbitrary user data
     */
    IAXTransaction(IAXEngine* engine, IAXFullFrame* frame, u_int16_t lcallno, const SocketAddr& addr,
	void* data = 0);

    /**
     * Constructor: constructs an outgoing transaction with an IAX control message that needs a new transaction
     * @param engine The engine that owns this transaction
     * @param type Transaction type: see Type enumeration
     * @param lcallno Local call number
     * @param addr Address to use
     * @param ieList Starting IE list
     * @param data Pointer to arbitrary user data
     */
    IAXTransaction(IAXEngine* engine, Type type, u_int16_t lcallno, const SocketAddr& addr, IAXIEList& ieList,
	void* data = 0);

    /**
     * Cleanup
     */
    virtual void destroyed();

    /**
     * Init data members from an IE list
     * @param ieList IE list to init from
     */
    void init(IAXIEList& ieList);

    /**
     * Increment sequence numbers (inbound or outbound) for the frames that need it
     * @param frame Received frame if inbound is true, otherwise a transmitted one
     * @param inbound True for inbound frames
     * @return True if incremented.
     */
    bool incrementSeqNo(const IAXFullFrame* frame, bool inbound);

    /**
     * Test if frame is acceptable (not an out of order or a late one)
     * @param frame Frame to test
     * @return True if frame can be added to incoming frame list
     */
    bool isFrameAcceptable(const IAXFullFrame* frame);

    /**
     * Change the transaction state
     * @param newState the new transaction state
     * @return False if trying to change a termination state into a non termination one
     */
    bool changeState(State newState);

    /**
     * Terminate the transaction.
     * @param evType IAXEvent type to generate
     * @param local If true it is a locally generated event
     * @param frame Frame to build event from
     * @param createIEList If true create IE list in the generated event
     * @return Pointer to a valid IAXEvent
     */
    IAXEvent* terminate(u_int8_t evType, bool local, IAXFullFrame* frame = 0, bool createIEList = true);

    /**
     * Wait for ACK to terminate the transaction. No more events will be generated
     * @param evType IAXEvent type to generate
     * @param local If true it is a locally generated event
     * @param frame Frame to build event from
     * @return Pointer to a valid IAXEvent if evType if non 0, 0 otherwise
     */
    IAXEvent* waitForTerminate(u_int8_t evType = 0, bool local = true, IAXFullFrame* frame = 0);

    /**
     * Constructs an IAXFrameOut frame, send it to remote peer and put it in the transmission list
     * This method is thread safe
     * @param type Frame type
     * @param subclass Frame subclass
     * @param data Frame IE list
     * @param len Frame IE list length
     * @param tStamp Frame timestamp. If 0 the transaction timestamp will be used
     * @param ackOnly Frame's acknoledge only flag
     * @param mark Frame mark flag
     */
    void postFrame(IAXFrame::Type type, u_int32_t subclass, void* data = 0, u_int16_t len = 0, u_int32_t tStamp = 0,
	bool ackOnly = false, bool mark = false);

    /**
     * Constructs an IAXFrameOut frame, send it to remote peer and put it in the transmission list
     * This method is thread safe
     * @param type Frame type
     * @param subclass Frame subclass
     * @param ies Frame IE list
     * @param tStamp Frame timestamp. If 0 the transaction timestamp will be used
     * @param ackOnly Frame's acknoledge only flag
     */
    void postFrameIes(IAXFrame::Type type, u_int32_t subclass, IAXIEList* ies, u_int32_t tStamp = 0,
		bool ackOnly = false);

    /**
     * Send a full frame to remote peer
     * @param frame Frame to send
     * @param vnak If true the transmission is a response to a VNAK frame
     * @return True on success
     */
    bool sendFrame(IAXFrameOut* frame, bool vnak = false);

    /**
     * Create an event
     * @param evType Event type
     * @param local If true it is a locally generated event.
     * @param frame Frame to create from
     * @param newState The transaction new state
     * @return Pointer to an IAXEvent or 0 (invalid IE list)
     */
    IAXEvent* createEvent(u_int8_t evType, bool local, IAXFullFrame* frame, State newState);

    /**
     * Create an event from a received frame that is a response to a sent frame and
     *  change the transaction state to newState. Remove the response from incoming list.
     * @param frame Frame to create response for
     * @param findType Frame type to find
     * @param findSubclass Frame subclass to find
     * @param evType Event type to generate
     * @param local Local flag for the generated event.
     * @param newState New transaction state if an event was generated
     * @return Pointer to an IAXEvent or 0 (invalid IE list)
     */
    IAXEvent* createResponse(IAXFrameOut* frame, u_int8_t findType, u_int8_t findSubclass, u_int8_t evType, bool local, State newState);

    /**
     * Find a response for a previously sent frame
     * @param frame Frame to find response for
     * @param delFrame Delete frame flag. If true on exit, a response was found
     * @return Pointer to an IAXEvent or 0
     */
    IAXEvent* getEventResponse(IAXFrameOut* frame, bool& delFrame);

    /**
     * Find a response for a previously sent frame if the transaction type is New
     * @param frame Frame to find response for
     * @param delFrame Delete frame flag. If true on exit, a response was found
     * @return Pointer to an IAXEvent or 0
     */
    IAXEvent* getEventResponse_New(IAXFrameOut* frame, bool& delFrame);

    /**
     * Process an authentication request. If valid, send an authentication reply
     * @param event Already generated event
     * @return Pointer to a valid IAXEvent
     */
    IAXEvent* processAuthReq(IAXEvent* event);

    /**
     * Process an accept. If not valid (call m_engine->acceptFormatAndCapability) send a reject.
     *  Otherwise return the event
     * @param event Already generated event
     * @return Pointer to a valid IAXEvent
     */
    IAXEvent* processAccept(IAXEvent* event);

    /**
     * Process an authentication reply
     * @param event Already generated event
     * @return Pointer to a valid IAXEvent
     */
    IAXEvent* processAuthRep(IAXEvent* event);

    /**
     * Find a response for a previously sent frame if the transaction type is RegReq/RegRel
     * @param frame Frame to find response for
     * @param delFrame Delete frame flag. If true on exit, a response was found
     * @return Pointer to an IAXEvent or 0
     */
    IAXEvent* getEventResponse_Reg(IAXFrameOut* frame, bool& delFrame);

    /**
     * Update transaction data from the event
     * @param event Already generated event
     * @return The received event
     */
    IAXEvent* processRegAck(IAXEvent* event);

    /**
     * Find out if an incoming frame would start a transaction
     * @param frame Frame to process
     * @param delFrame Delete frame flag. If true on exit, frame is valid
     * @return Pointer to an IAXEvent or 0
     */
    IAXEvent* getEventStartTrans(IAXFullFrame* frame, bool& delFrame);

    /**
     * Find out if a frame is a remote request
     * @param frame Frame to process
     * @param delFrame Delete rame flag. If true on exit, a request was found
     * @return Pointer to an IAXEvent or 0
     */
    IAXEvent* getEventRequest(IAXFullFrame* frame, bool& delFrame);

    /**
     * Find out if a frame is a remote request if transaction type is New
     * @param frame Frame to process
     * @param delFrame Delete rame flag. If true on exit, a request was found
     * @return Pointer to an IAXEvent or 0
     */
    IAXEvent* getEventRequest_New(IAXFullFrame* frame, bool& delFrame);

    /**
     * Search for a frame in m_inFrames having the given type and subclass
     * @param type Frame type to find.
     * @param subclass Frame subclass to find.
     * @return Pointer to frame if found or 0.
     */
    IAXFullFrame* findInFrame(IAXFrame::Type type, u_int32_t subclass);

    /**
     * Search in m_inFrames for a frame with the same timestamp as frameOut and deletes it.
     * @param frameOut Frame to find response for
     * @param type Frame type to find
     * @param subclass Frame subclass to find
     * @return True if found.
     */
    bool findInFrameTimestamp(const IAXFullFrame* frameOut, IAXFrame::Type type, u_int32_t subclass);

    /**
     * Search in m_inFrames for an ACK frame which confirm the received frame and deletes it
     * @param frameOut Frame to find response for
     * @return True if found.
     */
    bool findInFrameAck(const IAXFullFrame* frameOut);

    /**
     * Acknoledge the last received full frame
     */
    void ackInFrames();

    /**
     * Send a frame to remote peer in state Connected
     * This method is thread safe
     * @param subclass Frame subclass to send
     * @param frametype Frame type to send
     * @return False if the current transaction state is not Connected
     */
    bool sendConnected(IAXFullFrame::ControlType subclass, IAXFrame::Type frametype = IAXFrame::Control);

    /**
     * Send an ACK frame
     * @param frame Aknoledged frame
     */
    void sendAck(const IAXFullFrame* frame);

    /**
     * Send an VNAK frame
     */
    void sendVNAK();

    /**
     * Send an Unsupport frame
     * @param subclass Unsupported frame's subclass
     */
    void sendUnsupport(u_int32_t subclass);

    /**
     * Internal protocol outgoing frames processing (PING/LAGRQ)
     * @param frame Frame to process
     * @param delFrame Delete frame flag. If true on exit, a response was found
     * @return 0.
     */
    IAXEvent* processInternalOutgoingRequest(IAXFrameOut* frame, bool& delFrame);

    /**
     * Internal protocol incoming frames processing (PING/LAGRQ)
     * @param frame Frame to process
     * @param delFrame Delete frame flag. If true on exit, a request was found
     * @return 0.
     */
    IAXEvent* processInternalIncomingRequest(const IAXFullFrame* frame, bool& delFrame);

    /**
     * Process mid call control frames
     * @param frame Frame to process
     * @param delFrame Delete frame flag. If true on exit, a request was found
     * @return A valid IAXEvent or 0
     */
    IAXEvent* processMidCallControl(IAXFullFrame* frame, bool& delFrame);

    /**
     * Process mid call IAX control frames
     * @param frame Frame to process
     * @param delFrame Delete frame flag. If true on exit, a request was found
     * @return A valid IAXEvent or 0
     */
    IAXEvent* processMidCallIAXControl(IAXFullFrame* frame, bool& delFrame);

    /**
     * Test if frame is a Reject/RegRej frame
     * @param frame Frame to process.
     * @param delFrame Delete frame flag. If true on exit, a request was found
     * @return A valid IAXEvent or 0.
     */
    IAXEvent* remoteRejectCall(IAXFullFrame* frame, bool& delFrame);

    /**
     * Process received media full frames
     * @param frame Received frame
     * @param type Media type
     * @return 0
     */
    IAXTransaction* processMediaFrame(const IAXFullFrame* frame, int type);

    /**
     * Send all frames from outgoing queue with outbound sequence number starting with seqNo.
     * @param seqNo Requested sequence number
     * @return 0
     */
    IAXTransaction* retransmitOnVNAK(u_int16_t seqNo);

    /**
     * Generate a Reject event after internally rejecting a transaction
     * @param reason The reason of rejecting
     * @param code Error code
     * @return A valid IAXEvent
     */
    IAXEvent* internalReject(const char* reason, u_int8_t code);

    /**
     * Event terminated feedback
     * This method is thread safe
     * @param event The event notifying termination
     */
    void eventTerminated(IAXEvent* event);

    /**
     * Set the current event
     * @param event The event notifying termination
     * @return event
     */
    inline IAXEvent* keepEvent(IAXEvent* event) {
	m_currentEvent = event;
	return event;
    }

private:
    void adjustTStamp(u_int32_t& tStamp);
    void postFrame(IAXFrameOut* frame);
    void receivedVoiceMiniBeforeFull();
    void resetTrunk();
    void init();
    void setPendingEvent(IAXEvent* ev = 0);
    inline void restartTrunkIn(u_int64_t now, u_int32_t ts) {
	    m_trunkInStartTime = now;
	    u_int64_t dt = (now - m_lastVoiceFrameIn) / 1000;
	    m_trunkInTsDelta = m_lastVoiceFrameInTs + (u_int32_t)dt;
	    m_trunkInFirstTs = ts;
	}
    // Process accept format and caps
    bool processAcceptFmt(IAXIEList* list);
    // Process queued ACCEPT. Reject with given reason/code if not found
    // Reject with 'nomedia' if found and format is not acceptable
    IAXEvent* checkAcceptRecv(const char* reason, u_int8_t code);

    // Params
    bool m_localInitTrans;			// True: local initiated transaction
    bool m_localReqEnd;				// Local client requested terminate
    Type m_type;				// Transaction type
    State m_state;				// Transaction state
    bool m_destroy;                             // Destroy flag
    bool m_accepted;                            // ACCEPT received and processed
    u_int64_t m_timeStamp;			// Transaction creation timestamp
    u_int64_t m_timeout;			// Transaction timeout in Terminating state
    SocketAddr m_addr;				// Socket
    u_int16_t m_lCallNo;			// Local peer call id
    u_int16_t m_rCallNo;			// Remote peer call id
    unsigned char m_oSeqNo;			// Outgoing frame sequence number
    unsigned char m_iSeqNo;			// Incoming frame sequence number
    IAXEngine* m_engine;			// Engine that owns this transaction
    void* m_userdata;				// Arbitrary user data
    u_int32_t m_lastFullFrameOut;		// Last transmitted full frame timestamp
    IAXMediaData m_dataAudio;
    IAXMediaData m_dataVideo;
    u_int16_t m_lastAck;			// Last ack'd received frame's oseqno
    IAXEvent* m_pendingEvent;			// Pointer to a pending event or 0
    IAXEvent* m_currentEvent;			// Pointer to last generated event or 0
    // Outgoing frames management
    ObjList m_outFrames;			// Transaction & protocol control outgoing frames
    unsigned int m_retransCount;		// Retransmission counter. 0 --> Timeout
    unsigned int m_retransInterval;		// Frame retransmission interval
    // Incoming frames management
    ObjList m_inFrames;				// Transaction & protocol control incoming frames
    static unsigned char m_maxInFrames;		// Max frames number allowed in m_inFrames
    // Call leg management
    u_int32_t m_pingInterval;			// Ping remote peer interval
    u_int64_t m_timeToNextPing;			// Time of the next Ping
    // Statistics
    u_int32_t m_inTotalFramesCount;		// Total received frames
    u_int32_t m_inOutOfOrderFrames;		// Total out of order frames
    u_int32_t m_inDroppedFrames;		// Total dropped frames
    // Data
    IAXAuthMethod::Type m_authmethod;		// Authentication method to use
    String m_username;				// Username
    String m_callingNo;				// Calling number
    String m_callingName;			// Calling name
    String m_calledNo;				// Called number
    String m_calledContext;			// Called context
    String m_challenge;				// Challenge
    String m_authdata;				// Auth data received with auth reply
    u_int32_t m_expire;				// Registration expiring time
    IAXFormat m_format;				// Audio format
    IAXFormat m_formatVideo;			// Video format
    u_int32_t m_capability;			// Media capability of this transaction
    bool m_callToken;                           // Call token supported/expected
    unsigned int m_adjustTsOutThreshold;        // Adjust outgoing data timestamp threshold
    unsigned int m_adjustTsOutOverrun;          // Value used to adjust outgoing data timestamp on data
                                                //  overrun (incoming data with rate greater then expected)
    unsigned int m_adjustTsOutUnderrun;         // Value used to adjust outgoing data timestamp on data
                                                //  underrun (incoming data with rate less then expected)
    u_int64_t m_lastVoiceFrameIn;               // Time we received the last voice frame
    u_int32_t m_lastVoiceFrameInTs;             // Timestamp in the last received voice frame
    int m_reqVoiceVNAK;                         // Send VNAK if not received full voice frame
    // Meta trunking
    IAXMetaTrunkFrame* m_trunkFrame;		// Reference to a trunk frame if trunking is enabled for this transaction
    bool m_trunkFrameCallsSet;                  // Trunk frame calls increased
    bool m_trunkOutEfficientUse;                // Use or not the trunk frame based on calls using it
    bool m_trunkOutSend;                        // Currently using the trunk frame
    bool m_trunkInSyncUsingTs;                  // Incoming trunk without timestamps: generate timestamp
                                                //  using time or using trunk timestamp
    u_int64_t m_trunkInStartTime;               // First time we received trunk in data
    u_int32_t m_trunkInTsDelta;                 // Value used to re-build ts: last voice timestamp
    u_int32_t m_trunkInTsDiffRestart;           // Incoming trunk without timestamp: diff between timestamps at which we restart
    u_int32_t m_trunkInFirstTs;                 // Incoming trunk without timestamp: first trunk timestamp
    // Postponed start
    IAXIEList* m_startIEs;                      // Postponed start
};

/**
 * This class holds an event generated by a transaction
 * @short Event class
 */
class YIAX_API IAXEvent
{
    friend class IAXTransaction;
    friend class IAXConnectionlessTransaction;
public:
    /**
     * Event type as enumeration
     */
    enum Type {
	DontSet = 0,            // Used internal
        Invalid,		// Invalid frame received
	Terminated,		// Transaction terminated
        Timeout,		// Transaction timeout
	NotImplemented,		// Feature not implemented
	New,			// New remote transaction
	AuthReq,		// Auth request
	AuthRep,		// Auth reply
	Accept,			// Request accepted
	Hangup,			// Remote hangup
	Reject,			// Remote reject
	Busy,			// Call busy
	Text,			// Text frame received
	Dtmf,			// DTMF frame received
	Noise,			// Noise frame received
	Answer,			// Call answered
	Quelch,			// Quelch the call
	Unquelch,		// Unquelch the call
	Progressing,		// Call progressing
	Ringing,		// Ringing
    };

    /**
     * Destructor
     * Dereferences the transaction possibly causing its destruction
     */
    ~IAXEvent();

    /**
     * Get the type of this event
     * @return The type of the event as enumeratio
     */
    inline Type type() const
	{ return m_type; }

    /**
     * Check if this is a locally generated event
     * @return True if it is a locally generated event
     */
    inline bool local() const
        { return m_local; }

    /**
     * Check if this is a transaction finalization event
     * @return True if the transaction has finalized and will be destroyed
     */
    inline bool final() const
        { return m_final; }

    /**
     * Set the final flag.
     */
    inline void setFinal()
        { m_final = true; }

    /**
     * Get the type of the frame that generated the event
     * If 0 (internal event), the event consumer must delete the event
     * @return Frame type
     */
    inline u_int8_t frameType()
	{ return m_frameType; }

    /**
     * Get the subclass of the frame that generated the event
     * @return Frame subclass
     */
    inline u_int32_t subclass()
	{ return m_subClass; }

    /**
     * Get the IAX engine this event belongs to, if any
     * @return The IAX engine this event belongs to, if any
     */
    inline IAXEngine* getEngine() const
	{ return m_transaction ? m_transaction->getEngine() : 0; }

    /**
     * Get the IAX transaction that generated the event, if any
     * @return The IAX transaction that generated the event, if any
     */
    inline IAXTransaction* getTransaction() const
	{ return m_transaction; }

    /**
     * Get the opaque user data stored in the transaction
     * @return The opaque user data stored in the transaction
     */
    inline void* getUserData() const
	{ return m_transaction ? m_transaction->getUserData() : 0; }

    /**
     * Get the IE list
     * @return IE list reference
     */
    inline IAXIEList& getList()
	{ return *m_ieList; }

protected:
    /**
     * Constructor
     * @param type Event type
     * @param local Local flag
     * @param final Final flag
     * @param transaction IAX transaction that generated the event
     * @param frameType The type of the frame that generated the event
     * @param subclass The subclass of the frame that generated the event
     */
    IAXEvent(Type type, bool local, bool final, IAXTransaction* transaction, u_int8_t frameType = 0, u_int32_t subclass = 0);

    /**
     * Constructor
     * @param type Event type
     * @param local Local flag
     * @param final Final flag
     * @param transaction IAX transaction that generated the event
     * @param frame The frame that generated the event
     */
    IAXEvent(Type type, bool local, bool final, IAXTransaction* transaction, IAXFullFrame* frame = 0);

private:
    inline IAXEvent() {}		// Default constructor

    Type m_type;			// Event type
    u_int8_t m_frameType;		// Frame type
    u_int32_t m_subClass;		// Frame subclass
    bool m_local;			// If true the event is generated locally, the receiver MUST not respond
    bool m_final;			// Final event flag
    IAXTransaction* m_transaction;	// Transaction that generated this event
    IAXIEList* m_ieList;		// IAXInfoElement list
};

/**
 * This class holds all information needded to manipulate all IAX transactions and events
 * @short IAX engine class
 */
class YIAX_API IAXEngine : public DebugEnabler, public Mutex
{
public:
    /**
     * Constructor
     * @param iface Address of the interface to use, default all (0.0.0.0)
     * @param port UDP port to run the protocol on
     * @param format Default media format
     * @param capab Media capabilities of this engine
     * @param params Optional extra parameter list
     * @param name Engine name
     */
    IAXEngine(const char* iface, int port, u_int32_t format, u_int32_t capab,
	const NamedList* params = 0, const char* name = "iaxengine");

    /**
     * Destructor
     * Closes all transactions belonging to this engine and flush all queues
     */
    virtual ~IAXEngine();

    /**
     * Retrieve the engine name
     * @return Engine name
     */
    inline const String& name() const
	{ return m_name; }

    /**
     * Retrieve the default caller number type
     * @return Default caller number type
     */
    inline u_int8_t callerNumType() const
	{ return m_callerNumType; }

    /**
     * Retrieve the default caller number presentation and screening concatenated value
     * @return Default caller number presentation and screening
     */
    inline u_int8_t callingPres() const
	{ return m_callingPres; }

    /**
     * Add a parsed frame to the transaction list
     * @param addr Address from which the frame was received
     * @param frame A parsed IAX frame
     * @return Pointer to the transaction or 0 to deref the frame
     */
    IAXTransaction* addFrame(const SocketAddr& addr, IAXFrame* frame);

    /**
     * Add a raw frame to the transaction list
     * @param addr Address from which the message was received
     * @param buf Pointer to the start of the buffer holding the IAX frame
     * @param len Length of the message buffer
     * @return Pointer to the transaction or 0
     */
    IAXTransaction* addFrame(const SocketAddr& addr, const unsigned char* buf, unsigned int len);

    /**
     * Find a complete transaction.
     * This method is thread safe
     * @param addr Remote address
     * @param rCallNo Remote transaction call number
     * @return Referrenced pointer to the transaction or 0
     */
    IAXTransaction* findTransaction(const SocketAddr& addr, u_int16_t rCallNo);

    /**
     * Process media from remote peer. Descendents must override this method
     * @param transaction IAXTransaction that owns the call leg
     * @param data Media data
     * @param tStamp Media timestamp
     * @param type Media type
     * @param mark Mark flag
     */
    virtual void processMedia(IAXTransaction* transaction, DataBlock& data, u_int32_t tStamp,
	int type, bool mark)
	{}

    /**
     * Event processor method. Keeps calling getEvent() and passing
     *  any events to processEvent() until there are no more events
     * @return True if at least one event was processed
     */
    bool process();

    /**
     * Get the timeout interval sent challenge
     * @return Sent challenge timeout interval
     */
    inline unsigned int challengeTout() const
        { return m_challengeTout; }

    /**
     * Get the maximum allowed frame length
     * @return The maximum allowed frame length
     */
    inline u_int16_t maxFullFrameDataLen() const
        { return m_maxFullFrameDataLen; }

    /**
     * Get the default media format
     * @param audio True to retrieve default audio format, false for video format
     * @return The default media format
     */
    inline u_int32_t format(bool audio = true) const
        { return audio ? m_format : m_formatVideo; }

    /**
     * Get the media capability of this engine
     * @return The media capability of this engine
     */
    inline u_int32_t capability() const
        { return m_capability; }

    /**
     * Retrieve outgoing data timestamp adjust values
     * @param thres Adjust outgoing data timestamp threshold
     * @param over Value used to adjust outgoing data timestamp on data overrun
     * @param under Value used to adjust outgoing data timestamp on data underrun
     */
    inline void getOutDataAdjust(unsigned int& thres, unsigned int& over,
	unsigned int& under) const {
	    thres = m_adjustTsOutThreshold;
	    over = m_adjustTsOutOverrun;
	    under = m_adjustTsOutUnderrun;
	}

    /**
     * Initialize outgoing data timestamp adjust values.
     * This method is thread safe
     * @param params Parameters list
     * @param tr Optional transaction to init, initialize the engine's data if 0
     */
    void initOutDataAdjust(const NamedList& params, IAXTransaction* tr = 0);

    /**
     * (Re)Initialize the engine
     * @param params Parameter list
     */
    void initialize(const NamedList& params);

    /**
     * Read data from socket
     * @param addr Socket to read from
     */
    void readSocket(SocketAddr& addr);

    /**
     * Write data to socket.
     * @param buf Data to write
     * @param len Data length
     * @param addr Socket to write to
     * @param frame Optional frame to be printed
     * @param sent Pointer to variable to be filled with the number of bytes sent
     * @return True on success
     */
    bool writeSocket(const void* buf, int len, const SocketAddr& addr, IAXFullFrame* frame = 0,
	unsigned int* sent = 0);

    /**
     * Write a full frame to socket
     * @param addr Socket to write to
     * @param frame Frame to write
     * @return True on success
     */
    inline bool writeSocket(const SocketAddr& addr, IAXFullFrame* frame)
	{ return !frame || writeSocket(frame->data().data(),frame->data().length(),addr,frame); }

    /**
     * Read events
     */
    void runGetEvents();

    /**
     * Removes a transaction from queue. Free the allocated local call number
     *  Does not delete it
     * @param transaction Transaction to remove
     */
    void removeTransaction(IAXTransaction* transaction);

    /**
     * Check if there are any transactions in the engine
     * This method is thread safe
     * @return True if the engine holds at least 1 transaction
     */
    bool haveTransactions();

    /**
     * Return the transactions count
     * This method is thread safe
     * @return Transactions count
     */
    u_int32_t transactionCount();

    /**
     * Send an INVAL with call numbers set to 0 to a remote peer to keep it alive
     * @param addr Address to send to
     */
    void keepAlive(const SocketAddr& addr);

    /**
     * Process a new format received with a full frame
     * @param trans Transaction that received the new format
     * @param type Media type
     * @param format The received format
     * @return True if accepted
     */
    virtual bool mediaFormatChanged(IAXTransaction* trans, int type, u_int32_t format)
	{ return false; }

    /**
     * Check call token on incoming call requests.
     * This method is called by the engine when processing an incoming call request
     * @param addr The address from where the call request was received
     * @param frame Received frame
     * @return True if accepted, false to ignore the call
     */
    virtual bool checkCallToken(const SocketAddr& addr, IAXFullFrame& frame);

    /**
     * Process the initial received format and capability.
     * If accepted on exit will set the transaction format and capability
     * @param trans Transaction that received the new format
     * @param caps Optional codecs to set in transaction before processing
     * @param type Media type
     * @return True if accepted
     */
    bool acceptFormatAndCapability(IAXTransaction* trans, unsigned int* caps = 0,
	int type = IAXFormat::Audio);

    /**
     * Default event handler. event MUST NOT be deleted
     * @param event The event to handle
     */
    virtual void defaultEventHandler(IAXEvent* event);

    /**
     * Check if the engine is exiting
     * @return True if the engine is exiting
     */
    inline bool exiting() const
	{ return m_exiting; }

    /**
     * Set the exiting flag
     */
    virtual void setExiting();

    /**
     * Enable trunking for the given transaction. Allocate a trunk meta frame if needed.
     * Trunk data is ignored if a trunk object for transaction remote address already exists
     * @param trans Transaction to enable trunking for
     * @param params Trunk parameters list, may be 0
     * @param prefix Trunk parameters name prefix
     */
    void enableTrunking(IAXTransaction* trans, const NamedList* params,
	const String& prefix = String::empty());

    /**
     * Enable trunking for the given transaction. Allocate a trunk meta frame if needed.
     * Trunk data is ignored if a trunk object for transaction remote address already exists
     * @param trans Transaction to enable trunking for
     * @param data Trunk info to use
     */
    void enableTrunking(IAXTransaction* trans, IAXTrunkInfo& data);

    /**
     * Init incoming trunking data for a given transaction
     * @param trans Transaction to init
     * @param params Trunk parameters list, may be 0
     * @param prefix Trunk parameters name prefix
     */
    void initTrunkIn(IAXTransaction* trans, const NamedList* params,
	const String& prefix = String::empty());

    /**
     * Init incoming trunking data for a given transaction
     * @param trans Transaction to init
     * @param data Trunk info to use
     */
    void initTrunkIn(IAXTransaction* trans, IAXTrunkInfo& data);

    /**
     * Retrieve the default trunk info data
     * @param info Destination to be set with trunk info pointer
     * @return True if destination pointr is valid
     */
    inline bool trunkInfo(RefPointer<IAXTrunkInfo>& info) {
	    Lock lck(m_trunkInfoMutex);
	    info = m_trunkInfoDef;
	    return info != 0;
	}

    /**
     * Send an INVAL frame
     * @param frame Frame for which to send an INVAL frame
     * @param addr The address from where the call request was received
     */
    void sendInval(IAXFullFrame* frame, const SocketAddr& addr);

    /**
     * Keep calling processTrunkFrames to send trunked media data
     */
    void runProcessTrunkFrames();

    /**
     * Get the socket used for engine operation
     * @return Reference to the UDP socket
     */
    inline Socket& socket()
	{ return m_socket; }

    /**
     * Retrieve the socket address on wgich we are bound
     * @return Local address we are bound on
     */
    inline const SocketAddr& addr() const
	{ return m_addr; }

    /**
     * Send engine formats
     * @param caps Capabilities
     * @param fmtAudio Default audio format
     * @param fmtVideo Default video format
     */
    inline void setFormats(u_int32_t caps, u_int32_t fmtAudio, u_int32_t fmtVideo) {
	    m_format = fmtAudio;
	    m_formatVideo = fmtVideo;
	    m_capability = caps;
	}

    /**
     * Retrieve a port parameter
     * @param params Parameters list
     * @param param Parameter to retrieve
     * @return The port (default, 4569, if the parameter is missing or invalid)
     */
    static inline int getPort(const NamedList& params, const String& param = "port")
	{ return params.getIntValue(param,4569); }

    /**
     * Get the MD5 data from a challenge and a password
     * @param md5data Destination String
     * @param challenge Challenge source
     * @param password Password source
     */
    static void getMD5FromChallenge(String& md5data, const String& challenge, const String& password);

    /**
     * Test if a received response to an authentication request is correct
     * @param md5data Data to compare with
     * @param challenge Received challenge
     * @param password Password source
     */
    static bool isMD5ChallengeCorrect(const String& md5data, const String& challenge, const String& password);

    /**
     * Build a time signed secret used to authenticate an IP address
     * @param buf Destination buffer
     * @param secret Extra secret to add to MD5 sum
     * @param addr Socket address
     */
    static void buildAddrSecret(String& buf, const String& secret,
	const SocketAddr& addr);

    /**
     * Decode a secret built using buildAddrSecret()
     * @param buf Input buffer
     * @param secret Extra secret to check
     * @param addr Socket address
     * @return Secret age, negative if invalid
     */
    static int addrSecretAge(const String& buf, const String& secret,
	const SocketAddr& addr);

    /**
     * Add string (keyword) if found in a dictionary or integer parameter to a named list
     * @param list Destination list
     * @param param Parameter to add to the list
     * @param tokens The dictionary used to find the given value
     * @param val The value to find/add to the list
     */
    static inline void addKeyword(NamedList& list, const char* param,
	const TokenDict* tokens, unsigned int val) {
	    const char* value = lookup(val,tokens);
	    if (value)
		list.addParam(param,value);
	    else
		list.addParam(param,String(val));
	}

    /**
     * Decode a DATETIME value
     * @param dt Value to decode
     * @param year The year component of the date
     * @param month The month component of the date
     * @param day The day component of the date
     * @param hour The hour component of the time
     * @param minute The minute component of the time
     * @param sec The seconds component of the time
     */
    static void decodeDateTime(u_int32_t dt, unsigned int& year, unsigned int& month,
	unsigned int& day, unsigned int& hour, unsigned int& minute, unsigned int& sec);

    /**
     * Calculate overall timeout from interval and retransmission counter
     * @param interval The first retransmisssion interval
     * @param nRetrans The number of retransmissions
     * @return The overall timeout
     */
    static unsigned int overallTout(unsigned int interval = IAX2_RETRANS_INTERVAL_DEF,
	unsigned int nRetrans = IAX2_RETRANS_COUNT_DEF);

protected:
    /**
     * Process all trunk meta frames in the queue
     * @param time Time of the call
     * @return True if at least one frame was sent
     */
    bool processTrunkFrames(const Time& time = Time());

    /**
     * Default event for connection transactions handler. This method may be overriden to perform custom
     *  processing
     * This method is thread safe
     * @param event Event to process
     */
    virtual void processEvent(IAXEvent* event);

    /**
     * Get an IAX event from the queue.
     * This method is thread safe.
     * @param now Current time
     * @return Pointer to an IAXEvent or 0 if none is available
     */
    IAXEvent* getEvent(const Time& now = Time());

    /**
     * Generate call number. Update used call numbers list
     * @return Call number or 0 if none available
     */
    u_int16_t generateCallNo();

    /**
     * Release a call number
     * @param lcallno Call number to release
     */
    void releaseCallNo(u_int16_t lcallno);

    /**
     * Start a transaction based on a local request
     * @param type Transaction type
     * @param addr Remote address to send the request
     * @param ieList First frame IE list
     * @param refTrans Return a refferenced transaction pointer
     * @param startTrans Start transaction
     * @return IAXTransaction pointer on success
     */
    IAXTransaction* startLocalTransaction(IAXTransaction::Type type,
	const SocketAddr& addr, IAXIEList& ieList,
	bool refTrans = false, bool startTrans = true);

    /**
     * Bind the socket. Terminate it before trying
     * @param iface Address of the interface to use, default all (0.0.0.0)
     * @param port UDP port to run the protocol on
     * @param force Force binding if failed on required port
     * @return True on success
     */
    bool bind(const char* iface, int port, bool force);

    int m_trunking;                             // Trunking capability: negative: ok, otherwise: not enabled

private:
    String m_name;                              // Engine name
    Socket m_socket;				// Socket
    SocketAddr m_addr;                          // Address we are bound on
    ObjList** m_transList;			// Full transactions
    ObjList m_incompleteTransList;		// Incomplete transactions (no remote call number)
    bool m_lUsedCallNo[IAX2_MAX_CALLNO + 1];	// Used local call numnmbers flags
    int m_lastGetEvIndex;			// getEvent: keep last array entry
    bool m_exiting;                             // Exiting flag
    // Parameters
    int m_maxFullFrameDataLen;			// Max full frame data (IE list) length
    u_int16_t m_startLocalCallNo;		// Start index of local call number allocation
    u_int16_t m_transListCount;			// m_transList count
    unsigned int m_challengeTout;		// Sent challenge timeout interval
    bool m_callToken;                           // Call token required on incoming calls
    String m_callTokenSecret;                   // Secret used to generate call tokens
    int m_callTokenAge;                         // Max allowed call token age
    bool m_showCallTokenFailures;               // Print incoming call token failures to output
    bool m_rejectMissingCallToken;              // Reject/ignore incoming calls without call token if mandatory
    bool m_printMsg;                            // Print frame to output
    u_int8_t m_callerNumType;                   // Caller number type
    u_int8_t m_callingPres;                     // Caller presentation + screening
    // Media
    u_int32_t m_format;				// The default media format
    u_int32_t m_formatVideo;                    // Default video format
    u_int32_t m_capability;			// The media capability
    unsigned int m_adjustTsOutThreshold;        // Adjust outgoing data timestamp threshold
    unsigned int m_adjustTsOutOverrun;          // Value used to adjust outgoing data timestamp on data
                                                //  overrun (incoming data with rate greater then expected)
    unsigned int m_adjustTsOutUnderrun;         // Value used to adjust outgoing data timestamp on data
                                                //  underrun (incoming data with rate less then expected)
    // Trunking
    Mutex m_mutexTrunk;				// Mutex for trunk operations
    ObjList m_trunkList;			// Trunk frames list
    Mutex m_trunkInfoMutex;                     // Trunk info mutex
    RefPointer<IAXTrunkInfo> m_trunkInfoDef;    // Defaults for trunk data
};

}

#endif /* __YATEIAX_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
