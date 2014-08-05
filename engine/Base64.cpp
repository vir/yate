/**
 * Base64.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Base64 data encoding and decoding
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

#include "yateclass.h"

using namespace TelEngine;

// Padding char
#define PADDING_CHAR '='

static String s_eoln = "\r\n";
static String s_ignore = "=\r\n\t ";

// Base64 alphabet
// See RFC 4648 Table 1
static char s_alphabet[65] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

#define IC 255
// ASCII to Base64 translation table
// Each element except for IC represents an index in s_alphabet
static unsigned char s_ato64[256] = {
    IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,
    IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,
    IC,IC,IC,62,IC,IC,IC,63,52,53,54,55,56,57,58,59,60,61,IC,IC,
    IC,IC,IC,IC,IC, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,IC,IC,IC,IC,IC,IC,26,27,28,
    29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,
    49,50,51,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,
    IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,
    IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,
    IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,
    IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,
    IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,
    IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC,IC
};
#undef IC

// Check in the translation table if 'ch' is a valid Base64 char
static inline bool valid(char ch)
{
    return s_ato64[(unsigned char)ch] < 64;
}

// Check if 'ch' should be ignored
// Check in the translation table if 'ch' is a valid Base64 char
// Return -1 to ignore it, 1 to accept it, 0 on error
static inline int validLiberal(char ch)
{
    for (unsigned int i = 0; i < s_ignore.length(); i++)
	if (s_ignore.at(i) == ch)
	    return -1;
    return valid(ch) ? 1 : 0;
}

// Add end of line to dest and increase index if lines is non 0 and end
// if line was reached
static inline void addEoln(String& dest, unsigned int& idx,
	unsigned int& lines, unsigned int& crtLine, unsigned int lineLen)
{
    if (!(lines && crtLine == lineLen))
	return;
    char* d = (char*)dest.c_str();
    d[idx++] = s_eoln[0];
    d[idx++] = s_eoln[1];
    crtLine = 0;
    lines--;
}

// Add an encoded Base64 char to a destination string after clearing
// the bits 6 and 7. Increase the string's index
// Add end of line to dest and increase index if lines is non 0 and end
// if line was reached
static inline void addEnc(String& dest, unsigned int& idx, unsigned char ch,
	unsigned int& lines, unsigned int& crtLine, unsigned int lineLen)
{
    ((char*)dest.c_str())[idx++] = s_alphabet[ch & 0x3f];
    crtLine++;
    addEoln(dest,idx,lines,crtLine,lineLen);
}

// Add a decoded char buffer to a destination buffer and increase the index
// Len must be is 2,3,4
static inline bool addDec(DataBlock& dest, unsigned int& idx,
	unsigned char* dec, unsigned int len)
{
    switch (len) {
	case 0:
	    return true;
	case 2:
	case 3:
	case 4:
	    break;
	default:
	    return false;
    }
    unsigned char* d = dest.data(idx,--len);
    if (!d)
	return false;
    idx += len;
    switch (len) {
	case 3:
	    *d++ = dec[0] << 2 | dec[1] >> 4;
	    *d++ = dec[1] << 4 | dec[2] >> 2;
	    *d = dec[2] << 6 | dec[3];
	    return true;
	case 2:
	    *d++ = dec[0] << 2 | dec[1] >> 4;
	    *d = dec[1] << 4 | dec[2] >> 2;
	    return !(dec[2] & 0x03);
	case 1:
	    *d = dec[0] << 2 | dec[1] >> 4;
	    return !(dec[1] & 0x0f);
    }
    return false;
}

// Encode this buffer to a destination string
void Base64::encode(String& dest, unsigned int lineLen, bool lineAtEnd)
{
    dest = "";
    if (!length())
	return;

    unsigned char* s = (unsigned char*)data();  // Source buffer
    unsigned int rest = length() % 3;           // The number of bytes that will need padding
    unsigned int full = length() - rest;        // The number of bytes in source that will
                                                //  be processed in 3-byte chunks
    unsigned int i = 0;                         // Source index
    unsigned int lines = 0;                     // Number of lines
    unsigned int crtLine = 0;                   // Index in current line
    unsigned int iDest = 0;                     // Destination index
    unsigned int len = full / 3 * 4 + (rest ? 4 : 0); // Destination length, without EOLNs

    // Calculate how many lines we need (except for the last one)
    if (lineLen) {
	lines = len / lineLen;
	if (0 == (len % lineLen) && lines)
	    lines--;
    }
    dest.assign(PADDING_CHAR,len + lines * s_eoln.length());

    DDebug("Base64",DebugAll,
	"Encoding %u bytes (full=%u rest=%u) to %u bytes lines=%u",
	length(),full,rest,dest.length(),lines);

    // Encode each 3 bytes chunk from source to 4 bytes Base64 destination
    // 1: s_alphabet[bits 2-7 from s[i]]
    // 2: s_alphabet[bits 0,1 from s[i] + bits 4-7 from s[i+1]]
    // 3: s_alphabet[bits 0-3 from s[i+1] + bits 6,7 from s[i+2]]
    // 4: s_alphabet[bits 0-5 from s[i+2]]
    for (; i < full; i += 3) {
	addEnc(dest,iDest,s[i] >> 2,lines,crtLine,lineLen);
	addEnc(dest,iDest,s[i] << 4 | s[i+1] >> 4,lines,crtLine,lineLen);
	addEnc(dest,iDest,s[i+1] << 2 | s[i+2] >> 6,lines,crtLine,lineLen);
	addEnc(dest,iDest,s[i+2],lines,crtLine,lineLen);
    }
    // Encode rest (can be 1 or 2) to 4 bytes destination
    // 1: 2 chars + 2 padding, 2: 3 chars + 1 padding
    // Don't add the final padding char: destination was filled with it
    if (rest) {
	addEnc(dest,iDest,s[i] >> 2,lines,crtLine,lineLen);
	if (rest == 1)
	    addEnc(dest,iDest,s[i] << 4,lines,crtLine,lineLen);
	else {
	    addEnc(dest,iDest,s[i] << 4 | s[i+1] >> 4,lines,crtLine,lineLen);
	    addEnc(dest,iDest,s[i+1] << 2,lines,crtLine,lineLen);
	}
    }
    // Add final end of line ?
    if (lineAtEnd)
	dest << s_eoln;
}

// Decode this buffer to a destination one
bool Base64::decode(DataBlock& dest, bool liberal)
{
    dest.clear();

    // Calculate the number of alphabet characters
    unsigned int full = 0;
    unsigned int rest = 0;
    unsigned char* src = (unsigned char*)data();
    if (liberal)
	for (unsigned int i = 0; i < length(); i++) {
	    int res = validLiberal(src[i]);
	    if (!res) {
		Debug("Base64",DebugInfo,"Got invalid char 0x%x at pos %u [%p]",src[i],i,this);
		return false;
	    }
	    if (res > 0)
		full++;
	}
    else {
	full = length();
	// Skip padding chars from end
	for (; full; full--)
	   if (src[full-1] != PADDING_CHAR)
		break;
    }
    // rest MUST be 0, 2 or 3
    // rest is 1: can't build an 8-bit ascii char from a 6-bit Base64 char
    rest = full % 4;
    full -= rest;
    if (!(full || rest) || rest == 1) {
	Debug("Base64",DebugInfo,"Got invalid length %u [%p]",length(),this);
	return false;
    }
    dest.assign(0,full / 4 * 3 + (rest ? rest - 1 : 0));

    DDebug("Base64",DebugAll,"Decoding %u bytes (full=%u rest=%u) to %u bytes",
	length(),full,rest,dest.length());

    unsigned int iDest = 0;
    unsigned char dec[4];
    if (!liberal) {
	#define GET_DEC(a) \
	    if (valid(src[i+a])) \
		dec[a] = s_ato64[src[i+a]]; \
	    else { \
		Debug("Base64",DebugInfo,"Got invalid char 0x%x at pos %u [%p]", \
		    src[i+a],i+a,this); \
		return false; \
	    }
	unsigned int i = 0;
	// Decode each 4 bytes chunk from source
	// Translate each byte and build 3 destination bytes from 4 6-bit Base64 chars
	// 1: bits 0-5 from dec[0] + bits 4,5 from dec[1]
	// 2: bits 0-3 from dec[1] + bits 2-5 from dec[2]
	// 3: bits 0,1 from dec[2] + bits 0-5 from dec[3]
	for (; i < full; i += 4) {
	    GET_DEC(0)
	    GET_DEC(1)
	    GET_DEC(2)
	    GET_DEC(3)
	    addDec(dest,iDest,dec,4);
	}
	// Process the rest
	// 2: 1 destination byte. 3: 2 bytes in destination
	if (rest) {
	    GET_DEC(0)
	    GET_DEC(1)
	    if (rest == 3) {
		GET_DEC(2)
	    }
	}
	#undef GET_DEC
    }
    else {
	unsigned int iDec = 0;
	for (unsigned int i = 0; i < length(); i++, src++) {
	    int res = validLiberal(*src);
	    if (!res) {
		Debug("Base64",DebugInfo,"Got invalid char 0x%x at pos %u [%p]",*src,i,this);
		return false;
	    }
	    if (res < 0)
		continue;
	    dec[iDec++] = s_ato64[*src];
	    if (iDec == 4) {
		addDec(dest,iDest,dec,4);
		iDec = 0;
	    }
	}
    }
    if (addDec(dest,iDest,dec,rest))
	return true;
    Debug("Base64",DebugInfo,"Got garbage bits at end, probably truncated");
    return false;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
