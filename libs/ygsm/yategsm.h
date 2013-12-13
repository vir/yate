/*
 * yategsm.h
 * GSM Radio Layer 3 library
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2011-2013 Null Team
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

#ifndef __YATEGSM_H
#define __YATEGSM_H

#include <yateclass.h>
#include <yatexml.h>

#ifdef _WINDOWS

#ifdef LIBYGSM_EXPORTS
#define YGSM_API __declspec(dllexport)
#else
#ifndef LIBYGSM_STATIC
#define YGSM_API __declspec(dllimport)
#endif
#endif

#endif /* _WINDOWS */

#ifndef YGSM_API
#define YGSM_API
#endif


namespace TelEngine {

class YGSM_API RL3Codec
{
    YNOCOPY(RL3Codec);
public:
    enum Flags {
	XmlDumpMsg = 0x01,
	XmlDumpIEs = 0x02,
    };
    enum Status {
	NoError = 0,
	MsgTooShort,
	UnknownProto,
	ParserErr,
    };
    // Protocol discriminator according to ETSI TS 124 007 V11.0.0, section 11.2.3.1.1
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
    enum XmlType {
	Skip,
	XmlElem,
    };
    RL3Codec(DebugEnabler* dbg);
    unsigned int decode(const uint8_t* in, unsigned int len, XmlElement*& out);
    unsigned int encode(XmlElement* in, DataBlock& out);

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

    inline DebugEnabler* dbg() const
	{ return m_dbg; }

    inline void* ptr() const
	{ return m_ptr; }

    static const TokenDict s_typeDict[];
    static const TokenDict s_protoDict[];
private:
    uint8_t m_flags;                 // Codec flags
    // data used for debugging messages
    DebugEnabler* m_dbg;
    void* m_ptr;
};


}; // namespace TelEngine

#endif /* __YATEGSM_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
