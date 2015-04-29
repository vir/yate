/**
 * yateradio.h
 * Radio library
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2011-2014 Null Team
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

#ifndef __YATERADIO_H
#define __YATERADIO_H

#include <yateclass.h>
#include <yatexml.h>

#ifdef _WINDOWS

#ifdef LIBYRADIO_EXPORTS
#define YRADIO_API __declspec(dllexport)
#else
#ifndef LIBYRADIO_STATIC
#define YRADIO_API __declspec(dllimport)
#endif
#endif

#endif /* _WINDOWS */

#ifndef YRADIO_API
#define YRADIO_API
#endif


namespace TelEngine {

class YRADIO_API GSML3Codec
{
    YNOCOPY(GSML3Codec);
public:
    /**
     * Codec flags
     */
    enum Flags {
	XmlDumpMsg = 0x01,
	XmlDumpIEs = 0x02,
	MSCoder    = 0x04,
    };

    /**
     * Codec return status
     */
    enum Status {
	NoError = 0,
	MsgTooShort,
	UnknownProto,
	ParserErr,
	MissingParam,
	IncorrectOptionalIE,
	IncorrectMandatoryIE,
	MissingMandatoryIE,
	UnknownMsgType,
    };

    /**
     * Protocol discriminator according to ETSI TS 124 007 V11.0.0, section 11.2.3.1.1
     */
    enum Protocol {
	GCC       = 0x00, // Group Call Control
	BCC       = 0x01, // Broadcast Call Control
	EPS_SM    = 0x02, // EPS Session Management
	CC        = 0x03, // Call Control; Call Related SS messages
	GTTP      = 0x04, // GPRS Transparent Transport Protocol (GTTP)
	MM        = 0x05, // Mobility Management
	RRM       = 0x06, // Radio Resources Management
	EPS_MM    = 0x07, // EPS Mobility Management
	GPRS_MM   = 0x08, // GPRS Mobility Management
	SMS       = 0x09, // SMS
	GPRS_SM   = 0x0a, // GPRS Session Management
	SS        = 0x0b, // Non Call Related SS messages
	LCS       = 0x0c, // Location services
	Extension = 0x0e, // reserved for extension of the PD to one octet length
	Test      = 0x0f, // used by tests procedures described in 3GPP TS 44.014, 3GPP TS 34.109 and 3GPP TS 36.509
	Unknown   = 0xff,
    };

    /**
     * IE types
     */
    enum Type {
	NoType = 0,
	T,
	V,
	TV,
	LV,
	TLV,
	LVE,
	TLVE,
    };

    /**
     * Type of XML data to generate
     */
    enum XmlType {
	Skip,
	XmlElem,
	XmlRoot,
    };

    /**
     * EPS Security Headers
     */
    enum EPSSecurityHeader {
	PlainNAS                           = 0x00,
	IntegrityProtect                   = 0x01,
	IntegrityProtectCiphered           = 0x02,
	IntegrityProtectNewEPSCtxt         = 0x03,
	IntegrityProtectCipheredNewEPSCtxt = 0x04,
	ServiceRequestHeader               = 0xa0,
    };

    /**
     * Constructor
     */
    GSML3Codec(DebugEnabler* dbg = 0);

    /**
     * Decode layer 3 message payload
     * @param in Input buffer containing the data to be decoded
     * @param len Length of input buffer
     * @param out XmlElement into which the decoded data is returned
     * @param params Encoder parameters
     * @return Parsing result: 0 (NoError) if succeeded, error status otherwise
     */
    unsigned int decode(const uint8_t* in, unsigned int len, XmlElement*& out, const NamedList& params = NamedList::empty());

    /**
     * Encode a layer 3 message
     * @param in Layer 3 message in XML form
     * @param out Output buffer into which to put encoded data
     * @param params Encoder parameters
     * @return Parsing result: 0 (NoError) if succeeded, error status otherwise
     */
    unsigned int encode(const XmlElement* in, DataBlock& out, const NamedList& params = NamedList::empty());

    /**
     * Decode layer 3 message from an existing XML
     * @param xml XML which contains layer 3 messages to decode and into which the decoded XML will be put
     * @param params Decoder parameters
     * @return Parsing result: 0 (NoError) if succeeded, error status otherwise
     */
    unsigned int decode(XmlElement* xml, const NamedList& params = NamedList::empty());

    /**
     * Encode a layer 3 message from an existing XML
     * @param xml XML which contains a layer 3 message in XML form. The message will be replaced with its encoded buffer
     * @param params Encoder parameters
     * @return Parsing result: 0 (NoError) if succeeded, error status otherwise
     */
    unsigned int encode(XmlElement* xml, const NamedList& params = NamedList::empty());

    /**
     * Set data used in debug
     * @param enabler The DebugEnabler to use (0 to to use the engine)
     * @param ptr Pointer to print, 0 to use the codec pointer
     */
    void setCodecDebug(DebugEnabler* enabler = 0, void* ptr = 0);

    /**
     * Retrieve codec flags
     * @return Codec flags
     */
    inline uint8_t flags() const
	{ return m_flags; }

    /**
     * Set codec flags
     * @param flgs Flags to set
     * @param reset Reset flags before setting these ones
     */
    inline void setFlags(uint8_t flgs, bool reset = false)
    {
	if (reset)
	    resetFlags();
	m_flags |= flgs;
    }

    /**
     * Reset codec flags
     * @param flgs Flags to reset. If 0, all flags are reset
     */
    inline void resetFlags(uint8_t flgs = 0)
    {
	if (flgs)
	    m_flags &= ~flgs;
	else
	    m_flags = 0;
    }

    /**
     * Activate printing of debug messages
     * @param on True to activate, false to disable
     */
    inline void setPrintDbg(bool on = false)
	{ m_printDbg = on; }

    /**
     * Get printing of debug messages flag
     * @return True if debugging is activated, false otherwise
     */
    inline bool printDbg() const
	{ return m_printDbg; }

    /**
     * Get DebugEnabler used by this codec
     * @return DebugEnabler used by the codec
     */
    inline DebugEnabler* dbg() const
	{ return m_dbg; }

    /**
     * Retrieve the codec pointer used for debug messages
     * @return Codec pointer used for debug messages
     */
    inline void* ptr() const
	{ return m_ptr; }

    /**
     * Decode GSM 7bit buffer
     * @param buf Input buffer
     * @param len Input buffer length
     * @param text Destination text
     */
    static void decodeGSM7Bit(unsigned char* buf, unsigned int len, String& text);

    /**
     * Encode GSM 7bit buffer
     * @param text Input text
     * @param buf Destination buffer
     */
    static void encodeGSM7Bit(const String& text, DataBlock& buf);

    /**
     * IE types dictionary
     */
    static const TokenDict s_typeDict[];

    /**
     * L3 Protocols dictionary
     */
    static const TokenDict s_protoDict[];

    /**
     * EPS Security Headers dictionary
     */
    static const TokenDict s_securityHeaders[];

    /**
     * Errors dictionary
     */
    static const TokenDict s_errorsDict[];

    /**
     * Mobility Management reject causes dictionary
     */
    static const TokenDict s_mmRejectCause[];

    /**
     * GPRS Mobility Management reject causes dictionary
     */
    static const TokenDict s_gmmRejectCause[];

private:

    unsigned int decodeXml(XmlElement* xml, const NamedList& params, const String& pduTag);
    unsigned int encodeXml(XmlElement* xml, const NamedList& params, const String& pduTag);
    void printDbg(int dbgLevel, const uint8_t* in, unsigned int len, XmlElement* xml, bool encode = false);

    uint8_t m_flags;                 // Codec flags
    // data used for debugging messages
    DebugEnabler* m_dbg;
    void* m_ptr;
    // activate debug
    bool m_printDbg;
};


}; // namespace TelEngine

#endif /* __YATERADIO_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
