/**
 * Hasher.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2013-2014 Null Team
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

#include <stdlib.h>
#include <string.h>

using namespace TelEngine;

Hasher::~Hasher()
{
}

Hasher& Hasher::operator<<(const char* value)
{
    if (!null(value))
	update(value,::strlen(value));
    return *this;
}

// For details see RFC 2104: HMAC: Keyed-Hashing for Message Authentication

unsigned int Hasher::hmacBlockSize() const
{
    return 64;
}

bool Hasher::hmacStart(DataBlock& opad, const void* key, unsigned int keyLen)
{
    XDebug(DebugAll,"Hasher::hmacStart(%p,%p,%u) [%p]",&opad,key,keyLen,this);
    clear();
    opad.clear();
    if (keyLen && !key)
	return false;
    unsigned int size = hmacBlockSize();
    if (keyLen > size) {
	// Key needs to be hashed if longer than block size
	if (!update(key,keyLen)) {
	    clear();
	    return false;
	}
	opad.assign((void*)rawDigest(),hashLength());
	clear();
    }
    else
	opad.assign((void*)key,keyLen);
    if (opad.length() < size) {
	// Pad keys with zeros to required length
	DataBlock tmp(0,size - opad.length());
	opad.append(tmp);
    }
    uint8_t* data = (uint8_t*)opad.data();
    unsigned int i;
    // Build the inner pad
    for (i = 0; i < size; i++)
	data[i] ^= 0x36;
    if (!update(opad)) {
	clear();
	opad.clear();
	return false;
    }
    // Convert to outer pad
    for (i = 0; i < size; i++)
	data[i] ^= 0x6a;
    return true;
}

bool Hasher::hmacFinal(const DataBlock& opad)
{
    XDebug(DebugAll,"Hasher::hmacFinal(%p) [%p]",&opad,this);
    if (opad.length() != hmacBlockSize())
	return false;
    DataBlock tmp((void*)rawDigest(),hashLength());
    clear();
    if (!(update(opad) && update(tmp))) {
	clear();
	return false;
    }
    finalize();
    return true;
}

bool Hasher::hmac(const void* key, unsigned int keyLen, const void* msg, unsigned int msgLen)
{
    XDebug(DebugAll,"hmac(%p,%u,%p,%u) [%p]",key,keyLen,msg,msgLen,this);
    if ((keyLen && !key) || (msgLen && !msg))
	return false;
    DataBlock pad;
    return hmacStart(pad,key,keyLen) && update(msg,msgLen) && hmacFinal(pad);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
