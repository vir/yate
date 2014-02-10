/**
 * Compressor.cpp
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

#include "yatengine.h"

using namespace TelEngine;

// Compress the input buffer, flush all data,
//  append compressed data to the received data block
int Compressor::compress(const void* buf, unsigned int len, DataBlock& dest)
{
    XDebug(DebugAll,"Compressor(%s)::compress(%p,%u) dest len %u [%p]",
	toString().c_str(),buf,len,dest.length(),this);
    if (!(buf && len)) {
	buf = 0;
	len = 0;
    }
    const unsigned char* wrBuf = (const unsigned char*)buf;
    int ret = 0;
    while (true) {
	int wr = 0;
	if (len) {
	    wr = writeComp(wrBuf + ret,len,false);
	    XDebug(DebugAll,"Compressor(%s)::compress() wrote %d [%p]",
		toString().c_str(),wr,this);
	    if (wr > 0) {
		ret += wr;
		len -= wr;
	    }
	}
	int rd = readComp(dest,true);
	XDebug(DebugAll,"Compressor(%s)::compress() read %d [%p]",
	    toString().c_str(),rd,this);
	// Don't stop if write succeeded and was partial
	if (rd >= 0 && wr >= 0 && len)
	    continue;
	// Set return value to error or 0 if nothing wrote so far
	if (!ret)
	    ret = wr;
	break;
    }
    XDebug(DebugAll,"Compressor(%s)::compress(%p,%u) returning %d dest len %u [%p]",
	toString().c_str(),buf,len,ret,dest.length(),this);
    return ret;
}

// Decompress the input buffer, flush all data,
//  append decompressed data to the received data block
int Compressor::decompress(const void* buf, unsigned int len, DataBlock& dest)
{
    XDebug(DebugAll,"Compressor(%s)::decompress(%p,%u) dest len %u [%p]",
	toString().c_str(),buf,len,dest.length(),this);
    if (!(buf && len)) {
	buf = 0;
	len = 0;
    }
    const unsigned char* wrBuf = (const unsigned char*)buf;
    int ret = 0;
    while (true) {
	int wr = 0;
	if (len) {
	    wr = writeDecomp(wrBuf + ret,len,false);
	    XDebug(DebugAll,"Compressor(%s)::decompress() wrote %d [%p]",
		toString().c_str(),wr,this);
	    if (wr > 0) {
		ret += wr;
		len -= wr;
	    }
	}
	int rd = readDecomp(dest,true);
	XDebug(DebugAll,"Compressor(%s)::decompress() read %d [%p]",
	    toString().c_str(),rd,this);
	// Don't stop if write succeeded and was partial
	if (rd >= 0 && wr >= 0 && len)
	    continue;
	// Set return value to error or 0 if nothing wrote so far
	if (!ret)
	    ret = wr;
	break;
    }
    XDebug(DebugAll,"Compressor(%s)::decompress(%p,%u) returning %d dest len %u [%p]",
	toString().c_str(),buf,len,ret,dest.length(),this);
    return ret;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
