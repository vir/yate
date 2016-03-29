/**
 * Math.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2015 Null Team
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

#include "yatemath.h"
#include <stdio.h>

using namespace TelEngine;

#ifdef DEBUG
#define YBITVECTOR_VALID(meth,offs,len) { \
    BitVector tmp(*this,offs,len); \
    if (!tmp.valid()) \
	Debug(DebugFail,"BitVector::%s contains non 0/1 value [%p]",meth,this); \
}
#else
#ifdef _WINDOWS
#define YBITVECTOR_VALID do { break; } while
#else
#define YBITVECTOR_VALID(meth,offs,len)
#endif
#endif


static inline bool isBitSet(uint8_t val)
{
    return val != 0;
}

// Unpack 8 bits to buffer, MSB first
// Advance the buffer
static inline void unpackMsb8(uint8_t*& d, uint8_t val)
{
    *d++ = (val >> 7) & 0x01;
    *d++ = (val >> 6) & 0x01;
    *d++ = (val >> 5) & 0x01;
    *d++ = (val >> 4) & 0x01;
    *d++ = (val >> 3) & 0x01;
    *d++ = (val >> 2) & 0x01;
    *d++ = (val >> 1) & 0x01;
    *d++ = val & 0x01;
}

// Copy string, advance dest and src, return src
static inline const char* copyInc(char*& dest, const char* src, unsigned int n)
{
    if (n) {
	::strncpy(dest,src,n);
	dest += n;
    }
    return src + n;
}


//
// RefStorage
//
String& RefStorage::dumpSplit(String& buf, const String& str, unsigned int lineLen,
    unsigned int offset, const char* linePrefix, const char* suffix)
{
    suffix = TelEngine::c_safe(suffix);
    if (TelEngine::null(linePrefix))
	linePrefix = suffix;
    unsigned int len = str.length();
    unsigned int linePrefLen = ::strlen(linePrefix);
    // No lines ?
    if (!(lineLen && len && linePrefLen && lineLen < len))
	return buf << str << suffix;
    unsigned int firstLineLen = 0;
    if (offset && offset < lineLen) {
	firstLineLen = lineLen - offset;
	if (firstLineLen > len)
	    firstLineLen = len;
	len -= firstLineLen;
	// Nothing to be added after first line ?
	if (!len)
    	    return buf << str << suffix;
    }
    unsigned int nFullLines = len / lineLen;
    unsigned int lastLineLen = len % lineLen;
    unsigned int suffixLen = ::strlen(suffix);
    unsigned int nSep = nFullLines + (lastLineLen ? 1 : 0);
    char* tmpBuf = new char[str.length() + nSep * linePrefLen + suffixLen + 1];
    char* dest = tmpBuf;
    const char* src = str.c_str();
    src = copyInc(dest,src,firstLineLen);
    for (; nFullLines; nFullLines--) {
	copyInc(dest,linePrefix,linePrefLen);
	src = copyInc(dest,src,lineLen);
    }
    if (lastLineLen) {
	copyInc(dest,linePrefix,linePrefLen);
	src = copyInc(dest,src,lastLineLen);
    }
    copyInc(dest,suffix,suffixLen);
    *dest = 0;
    buf << tmpBuf;
    delete[] tmpBuf;
    return buf;
}


//
// BitVector
//
BitVector::BitVector(const char* str, unsigned int maxLen)
    : ByteVector(::strlen(TelEngine::c_safe(str)),0,maxLen)
{
    uint8_t* d = data();
    for (uint8_t* last = end(d,length()); d != last; ++d, ++str)
	if (*str == '1')
	    *d = 1;
}

// Check if this vector contains valid values (0 or 1)
bool BitVector::valid() const
{
    const uint8_t* d = data();
    for (const uint8_t* last = end(d,length()); d != last; ++d)
	if (*d > 1)
	    return false;
    return true;
}

bool BitVector::get(FloatVector& dest) const
{
    YBITVECTOR_VALID("get()",0,length());
    if (!dest.resize(length()))
	return false;
    float* d = dest.data();
    const uint8_t* src = data();
    for (const uint8_t* last = end(src,length()); src != last; ++src, ++d)
	*d = isBitSet(*src) ? 1.0F : 0.0F;
    return true;
}

bool BitVector::set(const FloatVector& input)
{
    if (!resize(input.length()))
	return false;
    const float* src = input.data();
    uint8_t* d = data();
    for (uint8_t* last = end(d,length()); d != last; ++d, ++src)
	*d = *src ? 1 : 0;
    return true;
}

// Apply XOR on vector bits from value, MSB first
void BitVector::xorMsb(uint32_t value, unsigned int offs, uint8_t len)
{
    len = (uint8_t)availableClamp(32,offs,len);
    uint8_t* d = data(offs,len);
    if (!d)
	return;
    YBITVECTOR_VALID("xorMsb()",offs,len);
    uint8_t shift = 24;
    for (uint8_t full = len / 8; full; --full, shift -= 8) {
	uint8_t v = (uint8_t)(value >> shift);
	*d++ ^= (v >> 7) & 0x01;
	*d++ ^= (v >> 6) & 0x01;
	*d++ ^= (v >> 5) & 0x01;
	*d++ ^= (v >> 4) & 0x01;
	*d++ ^= (v >> 3) & 0x01;
	*d++ ^= (v >> 2) & 0x01;
	*d++ ^= (v >> 1) & 0x01;
	*d++ ^= v & 0x01;
    }
    uint8_t rest = len % 8;
    if (!rest)
	return;
    uint8_t v = (uint8_t)(value >> (shift + 8 - rest));
    uint8_t* stop = --d;
    for (d = stop + rest; d != stop; --d, v >>= 1)
	*d ^= v & 0x01;
}

// Pack up to 64 bits, LSB first
uint64_t BitVector::pack(unsigned int offs, int len) const
{
    len = (int)availableClamp(64,offs,len);
    const uint8_t* d = data(offs,len);
    if (!d)
	return 0;
    YBITVECTOR_VALID("pack()",offs,len);
    uint64_t res = 0;
    for (int i = 0; i < len; ++i, ++d)
	if (isBitSet(*d))
	    res |= (uint64_t)1 << i;
    return res;
}

// Unpack up to 64 bits into this vector, LSB first
void BitVector::unpack(uint64_t value, unsigned int offs, uint8_t len)
{
    len = (uint8_t)availableClamp(64,offs,len);
    YBITVECTOR_VALID("unpack()",offs,len);
    uint8_t* d = data(offs,len);
    for (uint8_t* last = end(d,len); d != last; ++d, value >>= 1)
	*d = (uint8_t)(value & 0x01);
}

// Unpack up to 32 bits into this vector (MSB to LSB).
// MSB from value is the first unpacked bit
void BitVector::unpackMsb(uint32_t value, unsigned int offs, uint8_t len)
{
    len = (uint8_t)availableClamp(32,offs,len);
    uint8_t* d = data(offs,len);
    if (!d)
	return;
    YBITVECTOR_VALID("unpackMsb()",offs,len);
    uint8_t shift = 24;
    for (uint8_t full = len / 8; full; --full, shift -= 8)
	unpackMsb8(d,value >> shift);
    uint8_t rest = len % 8;
    if (!rest)
	return;
    uint8_t v = (uint8_t)(value >> (shift + 8 - rest));
    uint8_t* stop = --d;
    for (d = stop + rest; d != stop; --d, v >>= 1)
	*d = v & 0x01;
}

// Pack bits into a ByteVector
bool BitVector::pack(ByteVector& dest) const
{
    if (!length())
	return 0;
    unsigned int full = length() / 8;
    unsigned int rest = length() % 8;
    unsigned int n = full + (rest ? 1 : 0);
    uint8_t* d = dest.data(0,n);
    if (!d) {
	YMATH_FAIL(false,
	    "BitVector::pack() not enough data in destination vector [%p]",this);
	return false;
    }
    YBITVECTOR_VALID("pack()",0,length());
    dest.bzero(0,n);
    const uint8_t* src = data();
    // Full bytes
    for (const uint8_t* last = end(src,full * 8); src != last; ++d) {
#define SET_BIT(bit) if (isBitSet(*src++)) *d |= (1 << bit);
	SET_BIT(7);
	SET_BIT(6);
	SET_BIT(5);
	SET_BIT(4);
	SET_BIT(3);
	SET_BIT(2);
	SET_BIT(1);
	SET_BIT(0);
#undef SET_BIT
    }
    // Partial byte
    if (rest)
	for (uint8_t val = 0x80; rest; --rest, val >>= 1)
	    if (isBitSet(*src++))
		*d |= val;
    return true;
}

// Unpack a ByteVector into this BitVector
// MSB bit of the first element in source goes to first bit in this vector
bool BitVector::unpack(const ByteVector& src)
{
    const uint8_t* s = src.data(0,src.length());
    if (!s)
	return true;
    unsigned int len = src.length() * 8;
    uint8_t* d = data(0,len);
    if (d) {
	YBITVECTOR_VALID("unpack()",0,length());
	for(uint8_t* last = end(d,len); d != last; ++s)
	    unpackMsb8(d,*s);
	return true;
    }
    YMATH_FAIL(false,"BitVector::unpack() not enough space in vector [%p]",this);
    return false;
}

// Append bits to a string
String& BitVector::appendTo(String& buf, unsigned int offs, int len) const
{
    len = available(offs,len);
    const uint8_t* d = data(offs,len);
    if (!d)
	return buf;
    YBITVECTOR_VALID("appendTo()",offs,len);
    String tmp('0',len);
    char* s = (char*)tmp.c_str();
    for (const uint8_t* last = end(d,len); d != last; ++d, ++s)
	if (isBitSet(*d))
	    *s = '1';
    return buf.append(tmp);
}


//
// Math
//
// Append a Complex number to a String (using "%g%+gi" format)
String& Math::dumpComplex(String& dest, const Complex& val, const char* sep,
    const char* fmt)
{
    if (TelEngine::null(fmt))
	fmt = "%g%+gi";
    else if (::strlen(fmt) > 30) {
	String tmp;
	return dest.append(tmp.printf(512,fmt,val.re(),val.im()),sep);
    }
    char s[60];
    ::sprintf(s,fmt,val.re(),val.im());
    return dest.append(s,sep);
}

// Append float value to a String (using %g format)
String& Math::dumpFloat(String& dest, const float& val, const char* sep,
    const char* fmt)
{
    if (TelEngine::null(fmt))
	fmt = "%g";
    else if (::strlen(fmt) > 30) {
	String tmp;
	return dest.append(tmp.printf(512,fmt,val),sep);
    }
    char s[60];
    ::sprintf(s,fmt,val);
    return dest.append(s,sep);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
