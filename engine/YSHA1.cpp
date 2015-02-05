/**
 * YSHA1.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Originally based on the public domain implementation written by Steve Reid.
 * Copyright (c) Alan Smithee.
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) Jean-Francois Dive <jef@linuxbe.org>
 * Adapted for YATE by Paul Chitescu
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

#include <stdlib.h>
#include <string.h>

#if (defined(WORDS_BIGENDIAN) || defined(BIGENDIAN))
#define be32_to_cpu(x) (x) /* Nothing */
#define cpu_to_be32(x) (x)
#else

static inline u_int32_t be32_to_cpu(u_int32_t x)
{
    return ((x & 0xff000000) >> 24) | ((x & 0xff0000) >> 8) | ((x & 0xff00) << 8) | ((x & 0xff) << 24);
}

#define cpu_to_be32(x) be32_to_cpu(x)
#endif

#define SHA1_DIGEST_SIZE	20
#define SHA1_HMAC_BLOCK_SIZE	64

static inline u_int32_t rol(u_int32_t value, u_int32_t bits)
{
    return (((value) << (bits)) | ((value) >> (32 - (bits))));
}

/* blk0() and blk() perform the initial expand. */
/* I got the idea of expanding during the round function from SSLeay */
# define blk0(i) block32[i]

#define blk(i) (block32[i&15] = rol(block32[(i+13)&15]^block32[(i+8)&15] \
    ^block32[(i+2)&15]^block32[i&15],1))

/* (R0+R1), R2, R3, R4 are the different operations used in SHA1 */
#define R0(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk0(i)+0x5A827999+rol(v,5); \
                        w=rol(w,30);
#define R1(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk(i)+0x5A827999+rol(v,5); \
                        w=rol(w,30);
#define R2(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0x6ED9EBA1+rol(v,5);w=rol(w,30);
#define R3(v,w,x,y,z,i) z+=(((w|x)&y)|(w&x))+blk(i)+0x8F1BBCDC+rol(v,5); \
                        w=rol(w,30);
#define R4(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0xCA62C1D6+rol(v,5);w=rol(w,30);

typedef struct {
    u_int64_t count;
    u_int32_t state[5];
    u_int8_t buffer[64];
} sha1_ctx;

/* Hash a single 512-bit block. This is the core of the algorithm. */
static void sha1_transform(u_int32_t *state, const u_int8_t *in)
{
    u_int32_t a, b, c, d, e;
    u_int32_t block32[16];

    /* convert/copy data to workspace */
    for (a = 0; a < sizeof(block32)/sizeof(u_int32_t); a++)
	block32[a] = be32_to_cpu (((const u_int32_t *)in)[a]);

    /* Copy context->state[] to working vars */
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];

    /* 4 rounds of 20 operations each. Loop unrolled. */
    R0(a,b,c,d,e, 0); R0(e,a,b,c,d, 1); R0(d,e,a,b,c, 2); R0(c,d,e,a,b, 3);
    R0(b,c,d,e,a, 4); R0(a,b,c,d,e, 5); R0(e,a,b,c,d, 6); R0(d,e,a,b,c, 7);
    R0(c,d,e,a,b, 8); R0(b,c,d,e,a, 9); R0(a,b,c,d,e,10); R0(e,a,b,c,d,11);
    R0(d,e,a,b,c,12); R0(c,d,e,a,b,13); R0(b,c,d,e,a,14); R0(a,b,c,d,e,15);
    R1(e,a,b,c,d,16); R1(d,e,a,b,c,17); R1(c,d,e,a,b,18); R1(b,c,d,e,a,19);
    R2(a,b,c,d,e,20); R2(e,a,b,c,d,21); R2(d,e,a,b,c,22); R2(c,d,e,a,b,23);
    R2(b,c,d,e,a,24); R2(a,b,c,d,e,25); R2(e,a,b,c,d,26); R2(d,e,a,b,c,27);
    R2(c,d,e,a,b,28); R2(b,c,d,e,a,29); R2(a,b,c,d,e,30); R2(e,a,b,c,d,31);
    R2(d,e,a,b,c,32); R2(c,d,e,a,b,33); R2(b,c,d,e,a,34); R2(a,b,c,d,e,35);
    R2(e,a,b,c,d,36); R2(d,e,a,b,c,37); R2(c,d,e,a,b,38); R2(b,c,d,e,a,39);
    R3(a,b,c,d,e,40); R3(e,a,b,c,d,41); R3(d,e,a,b,c,42); R3(c,d,e,a,b,43);
    R3(b,c,d,e,a,44); R3(a,b,c,d,e,45); R3(e,a,b,c,d,46); R3(d,e,a,b,c,47);
    R3(c,d,e,a,b,48); R3(b,c,d,e,a,49); R3(a,b,c,d,e,50); R3(e,a,b,c,d,51);
    R3(d,e,a,b,c,52); R3(c,d,e,a,b,53); R3(b,c,d,e,a,54); R3(a,b,c,d,e,55);
    R3(e,a,b,c,d,56); R3(d,e,a,b,c,57); R3(c,d,e,a,b,58); R3(b,c,d,e,a,59);
    R4(a,b,c,d,e,60); R4(e,a,b,c,d,61); R4(d,e,a,b,c,62); R4(c,d,e,a,b,63);
    R4(b,c,d,e,a,64); R4(a,b,c,d,e,65); R4(e,a,b,c,d,66); R4(d,e,a,b,c,67);
    R4(c,d,e,a,b,68); R4(b,c,d,e,a,69); R4(a,b,c,d,e,70); R4(e,a,b,c,d,71);
    R4(d,e,a,b,c,72); R4(c,d,e,a,b,73); R4(b,c,d,e,a,74); R4(a,b,c,d,e,75);
    R4(e,a,b,c,d,76); R4(d,e,a,b,c,77); R4(c,d,e,a,b,78); R4(b,c,d,e,a,79);
    /* Add the working vars back into context.state[] */
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    /* Wipe variables */
    a = b = c = d = e = 0;
    memset (block32, 0x00, sizeof block32);
}

static void sha1_init(sha1_ctx *sctx)
{
    static const sha1_ctx initstate = {
	0,
	{ 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 },
	{ 0, }
    };

    *sctx = initstate;
}

static void sha1_update(sha1_ctx *sctx, const u_int8_t *data, unsigned int len)
{
    unsigned int i, j;

    j = (sctx->count >> 3) & 0x3f;
    sctx->count += len << 3;

    if ((j + len) > 63) {
	memcpy(&sctx->buffer[j], data, (i = 64-j));
	sha1_transform(sctx->state, sctx->buffer);
	for ( ; i + 63 < len; i += 64) {
	    sha1_transform(sctx->state, &data[i]);
	}
	j = 0;
    }
    else i = 0;
    memcpy(&sctx->buffer[j], &data[i], len - i);
}


/* Add padding and return the message digest. */
static void sha1_final(sha1_ctx *sctx, u_int8_t *out)
{
    u_int32_t i, j, index, padlen;
    u_int64_t t;
    u_int8_t bits[8] = { 0, };
    static const u_int8_t padding[64] = { 0x80, };

    t = sctx->count;
    bits[7] = 0xff & t; t>>=8;
    bits[6] = 0xff & t; t>>=8;
    bits[5] = 0xff & t; t>>=8;
    bits[4] = 0xff & t; t>>=8;
    bits[3] = 0xff & t; t>>=8;
    bits[2] = 0xff & t; t>>=8;
    bits[1] = 0xff & t; t>>=8;
    bits[0] = 0xff & t;

    /* Pad out to 56 mod 64 */
    index = (sctx->count >> 3) & 0x3f;
    padlen = (index < 56) ? (56 - index) : ((64+56) - index);
    sha1_update(sctx, padding, padlen);

    /* Append length */
    sha1_update(sctx, bits, sizeof bits);

    /* Store state in digest */
    for (i = j = 0; i < 5; i++, j += 4) {
	u_int32_t t2 = sctx->state[i];
	out[j+3] = t2 & 0xff; t2>>=8;
	out[j+2] = t2 & 0xff; t2>>=8;
	out[j+1] = t2 & 0xff; t2>>=8;
	out[j  ] = t2 & 0xff;
    }

    /* Wipe context */
    memset(sctx, 0, sizeof *sctx);
}

// Yate's C++ wrapper routines start here

using namespace TelEngine;

SHA1::SHA1()
{
}

SHA1::SHA1(const void* buf, unsigned int len)
{
    update(buf,len);
}

SHA1::SHA1(const DataBlock& data)
{
    update(data);
}

SHA1::SHA1(const String& str)
{
    update(str);
}

SHA1::SHA1(const SHA1& original)
{
    m_hex = original.m_hex;
    ::memcpy(m_bin,original.m_bin,sizeof(m_bin));
    if (original.m_private) {
	m_private = ::malloc(sizeof(sha1_ctx));
	::memcpy(m_private,original.m_private,sizeof(sha1_ctx));
    }
}

SHA1::~SHA1()
{
    clear();
}

SHA1& SHA1::operator=(const SHA1& original)
{
    clear();
    m_hex = original.m_hex;
    ::memcpy(m_bin,original.m_bin,sizeof(m_bin));
    if (original.m_private) {
	m_private = ::malloc(sizeof(sha1_ctx));
	::memcpy(m_private,original.m_private,sizeof(sha1_ctx));
    }
    return *this;
}

void SHA1::clear()
{
    if (m_private) {
	::free(m_private);
	m_private = 0;
    }
    m_hex.clear();
    ::memset(m_bin,0,sizeof(m_bin));
}

void SHA1::init()
{
    if (m_private)
	return;
    clear();
    m_private = ::malloc(sizeof(sha1_ctx));
    sha1_init((sha1_ctx*)m_private);
}

void SHA1::finalize()
{
    if (m_hex)
	return;
    init();
    sha1_final((sha1_ctx*)m_private, (u_int8_t*)m_bin);
    m_hex.hexify(m_bin,sizeof(m_bin));
}

bool SHA1::updateInternal(const void* buf, unsigned int len)
{
    // Don't update an already finalized digest
    if (m_hex)
	return false;
    if (!len)
	return true;
    if (!buf)
	return false;
    init();
    sha1_update((sha1_ctx*)m_private, (const u_int8_t*)buf, len);
    return true;
}

const unsigned char* SHA1::rawDigest()
{
    finalize();
    return m_bin;
}

// NIST FIPS 186-2 change notice 1 PRF with 160 bit SHA1 function G(t,c)
bool SHA1::fips186prf(DataBlock& out, const DataBlock& seed, unsigned int len)
{
    unsigned int l = seed.length();
    out.clear();
    if ((len == 0) || (len > 512) || (l == 0) || (l > 64))
	return false;
    sha1_ctx ctx;
    sha1_init(&ctx);
    memcpy(ctx.buffer,seed.data(),l);
    if (l < 64)
	memset(ctx.buffer + l, 0, 64 - l);
    out.assign(0,len);
    uint8_t* ptr = (uint8_t*)out.data();
    while (len) {
	u_int32_t w[5];
	memcpy(w,ctx.state,20);
	sha1_transform(w,ctx.buffer);
	w[0] = cpu_to_be32(w[0]);
	w[1] = cpu_to_be32(w[1]);
	w[2] = cpu_to_be32(w[2]);
	w[3] = cpu_to_be32(w[3]);
	w[4] = cpu_to_be32(w[4]);
	if (len <= 20) {
	    memcpy(ptr,w,len);
	    break;
	}
	memcpy(ptr,&w,20);
	unsigned int cy = 1;
	for (int i = 19; i >= 0; i--) {
	    cy += ctx.buffer[i] + (unsigned int)ptr[i];
	    ctx.buffer[i] = (uint8_t)cy;
	    cy >>= 8;
	}
	ptr += 20;
	len -= 20;
    }
    return true;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
