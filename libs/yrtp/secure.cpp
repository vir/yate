/**
 * secure.cpp
 * Yet Another RTP Stack
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

#include <yatertp.h>

#include <string.h>
#include <stdlib.h>

using namespace TelEngine;

static const DataBlock s_16bit(0,2);


RTPSecure::RTPSecure()
    : m_owner(0), m_rtpCipher(0),
      m_rtpAuthLen(0), m_rtpEncrypted(false)
{
    DDebug(DebugAll,"RTPSecure::RTPSecure() [%p]",this);
}

RTPSecure::RTPSecure(const String& suite)
    : m_owner(0), m_rtpCipher(0),
      m_rtpAuthLen(4), m_rtpEncrypted(true)
{
    DDebug(DebugAll,"RTPSecure::RTPSecure('%s') [%p]",suite.c_str(),this);
    if (suite == YSTRING("NULL")) {
	m_rtpAuthLen = 0;
	m_rtpEncrypted = false;
    }
    else if (suite == YSTRING("AES_CM_128_HMAC_SHA1_32"))
	m_rtpAuthLen = 4;
    else if (suite == YSTRING("AES_CM_128_HMAC_SHA1_80"))
	m_rtpAuthLen = 10;
}

RTPSecure::RTPSecure(const RTPSecure& other)
    : GenObject(),
      m_owner(0), m_rtpCipher(0),
      m_rtpAuthLen(other.m_rtpAuthLen), m_rtpEncrypted(other.m_rtpEncrypted)
{
    DDebug(DebugAll,"RTPSecure::~RTPSecure(%p) [%p]",&other,this);
}

RTPSecure::~RTPSecure()
{
    DDebug(DebugAll,"RTPSecure::~RTPSecure() [%p]",this);
    TelEngine::destruct(m_rtpCipher);
}

void RTPSecure::owner(RTPBaseIO* newOwner)
{
    m_owner = newOwner;
    init();
}

bool RTPSecure::supported(RTPSession* session) const
{
    if (m_owner && !session)
	session = m_owner->session();
    return session && session->checkCipher("aes_ctr");
}

void RTPSecure::init()
{
    if (!m_owner)
	return;
    Debug(DebugInfo,"RTPSecure::init() encrypt=%s authlen=%d [%p]",
	String::boolText(m_rtpEncrypted),m_rtpAuthLen,this);
    m_owner->secLength(m_rtpAuthLen);
    if ((m_rtpEncrypted || m_rtpAuthLen) && !m_rtpCipher && m_owner->session()) {
	Cipher* cipher = m_owner->session()->createCipher("aes_ctr",Cipher::Bidir);
	if (!cipher)
	    return;
	cipher->setKey(m_masterKey);
	deriveKey(*cipher,m_cipherKey,16,0);
	deriveKey(*cipher,m_cipherSalt,14,2);
	// add now the extra 16 bits since we need them for each packet
	m_cipherSalt.append(s_16bit);
	// prepare components of auth HMAC-SHA1
	DataBlock authKey;
	deriveKey(*cipher,authKey,20,1);
	m_authOpad.clear();
	m_authIpad.clear();
	unsigned char* key = (unsigned char*)authKey.data();
	unsigned char ipad[64];
	unsigned char opad[64];
	for (unsigned i = 0; i < 64; i++) {
	    unsigned char c = (i < authKey.length()) ? key[i] : 0;
	    ipad[i] = c ^ 0x36;
	    opad[i] = c ^ 0x5c;
	}
	// preinitialize the two partial digests
	m_authIpad.update(ipad,sizeof(ipad));
	m_authOpad.update(opad,sizeof(opad));
	// finally, prepare the cipher for RTP processing
	cipher->setKey(m_cipherKey);
	m_rtpCipher = cipher;
	DDebug(DebugInfo,"RTPSecure::init() got cipher=%p [%p]",cipher,this);
    }
}

bool RTPSecure::setup(const String& cryptoSuite, const String& keyParams, const ObjList* paramList)
{
    Debug(DebugInfo,"RTPSecure::setup('%s','%s',%p) [%p]",
	cryptoSuite.c_str(),keyParams.c_str(),paramList,this);
    m_rtpEncrypted = !paramList || (0 == paramList->find("UNENCRYPTED_SRTP"));
    if (cryptoSuite.null() || cryptoSuite == YSTRING("NULL")) {
	m_rtpAuthLen = 0;
	m_rtpEncrypted = false;
    }
    else if (cryptoSuite == YSTRING("AES_CM_128_HMAC_SHA1_32"))
	m_rtpAuthLen = 4;
    else if (cryptoSuite == YSTRING("AES_CM_128_HMAC_SHA1_80"))
	m_rtpAuthLen = 10;
    else {
	Debug(DebugMild,"Unknown SRTP crypto suite '%s'",cryptoSuite.c_str());
	return false;
    }
    if (paramList && (0 != paramList->find("UNAUTHENTICATED_SRTP")))
	m_rtpAuthLen = 0;
    if (m_rtpEncrypted || m_rtpAuthLen) {
	if (keyParams.null())
	    return false;
	bool err = true;
	ObjList* l = keyParams.split('|');
	// HACK use a for so we can break out easily in case of error
	for (;err;err = false) {
	    String* key = static_cast<String*>(l->get());
	    if (!key->startSkip("inline:",false))
		break;
	    Base64 b64;
	    DataBlock saltedKey;
	    b64 << *key;
	    if (!b64.decode(saltedKey,false))
		break;
	    if (saltedKey.length() != 30)
		break;
	    char* sk = (char*)saltedKey.data();
	    m_masterKey.assign(sk,16);
	    m_masterSalt.assign(sk+16,14);
	}
	TelEngine::destruct(l);
	if (err)
	    return false;
    }
    init();
    return true;
}

bool RTPSecure::create(String& suite, String& keyParams, bool buildMaster)
{
    if ((m_masterKey.null() || m_masterSalt.null()) && m_rtpAuthLen && !buildMaster)
	return false;
    m_rtpEncrypted = true;
    switch (m_rtpAuthLen) {
	case 0:
	    suite = "NULL";
	    m_rtpEncrypted = false;
	    break;
	case 4:
	    suite = "AES_CM_128_HMAC_SHA1_32";
	    break;
	case 10:
	    suite = "AES_CM_128_HMAC_SHA1_80";
	    break;
	default:
	    return false;
    }
    bool needInit = m_masterKey.null() || m_masterSalt.null();
    if (needInit) {
#if 0
	// Key Derivation Test Vectors from RFC 3711 B.3.
	unsigned char sk[30] = {
	    0xE1, 0xF9, 0x7A, 0x0D, 0x3E, 0x01, 0x8B, 0xE0, 0xD6, 0x4F, 0xA3, 0x2C, 0x06, 0xDE, 0x41, 0x39,
	    0x0E, 0xC6, 0x75, 0xAD, 0x49, 0x8A, 0xFE, 0xEB, 0xB6, 0x96, 0x0B, 0x3A, 0xAB, 0xE6
	    };
#else
	unsigned char sk[30];
	for (unsigned int i = 0; i < sizeof(sk);) {
	    u_int16_t r = (u_int16_t)Random::random();
	    sk[i++] = r & 0xff;
	    sk[i++] = (r >> 8) & 0xff;
	}
#endif
	m_masterKey.assign(sk,16);
	m_masterSalt.assign(sk+16,14);
    }
    Base64 b64;
    b64 << m_masterKey << m_masterSalt;
    String key;
    b64.encode(key,0,false);
    keyParams = "inline:" + key;
    if (needInit)
	init();
    return true;
}

bool RTPSecure::deriveKey(Cipher& cipher, DataBlock& key, unsigned int len, unsigned char label, u_int64_t index)
{
    if (!(len && m_masterSalt.length()))
	return false;
    unsigned int vLen = cipher.initVectorSize();
    if (!vLen)
	return false;
    key = m_masterSalt;
    if (vLen > key.length()) {
	DataBlock tmp(0,vLen - key.length());
	key += tmp;
    }
    else
	vLen = key.length();
    // initially point 16 bits before end of block
    unsigned char* p = (vLen - 2) + (unsigned char*)key.data();
    for (int i = 0; i < 6; i++) {
	*--p ^= (index & 0xff);
	index = index >> 8;
    }
    *--p ^= label;
    cipher.initVector(key);
    key.assign(0,len);
    cipher.encrypt(key);
    return true;
}

bool RTPSecure::rtpDecipher(unsigned char* data, int len, const void* secData, u_int32_t ssrc, u_int64_t seq)
{
    if (!(m_rtpEncrypted && data))
	return true;
    if (!(len && m_rtpCipher))
	return false;
    DataBlock iv(m_cipherSalt);
    int i;
    // SSRC << 64
    unsigned char* p = (iv.length() - 8) + (unsigned char*)iv.data();
    for (i = 0; i < 4; i++) {
	*--p ^= (ssrc & 0xff);
	ssrc >>= 8;
    }
    // index << 16
    p = (iv.length() - 2) + (unsigned char*)iv.data();
    for (i = 0; i < 6; i++) {
	*--p ^= (seq & 0xff);
	seq >>= 8;
    }
    m_rtpCipher->initVector(iv);
    m_rtpCipher->decrypt(data,len);
    return true;
}

bool RTPSecure::rtpCheckIntegrity(const unsigned char* data, int len, const void* authData, u_int32_t ssrc, u_int64_t seq)
{
    if (0 == m_rtpAuthLen)
	return true;
    if (!(len && data && authData))
	return false;

    // RFC 3711 4.2
    u_int32_t roc = htonl((u_int32_t)(seq >> 16));
    SHA1 h1(m_authIpad);
    h1.update(data,len);
    h1.update(&roc,sizeof(roc));
    h1.finalize();
    SHA1 hmac(m_authOpad);
    hmac.update(h1.rawDigest(),h1.rawLength());
    hmac.finalize();
#ifdef DEBUG
    if (::memcmp(authData,hmac.rawDigest(),m_rtpAuthLen)) {
	String s1,s2;
	s1.hexify((void*)authData,m_rtpAuthLen);
	s2.hexify((void*)hmac.rawDigest(),m_rtpAuthLen);
	Debug(DebugMild,"SRTP HMAC recv: %s calc: %s seq: " FMT64U " [%p]",
	    s1.c_str(),s2.c_str(),seq,this);
	return false;
    }
    return true;
#else
    return 0 == ::memcmp(authData,hmac.rawDigest(),m_rtpAuthLen);
#endif
}

void RTPSecure::rtpEncipher(unsigned char* data, int len)
{
    if (!(len && data && m_rtpEncrypted && m_rtpCipher && m_owner))
	return;
    // SRTP is symmetrical as it just XORs the data with a keystream
    rtpDecipher(data,len,0,m_owner->ssrc(),m_owner->fullSeq());
}

void RTPSecure::rtpAddIntegrity(const unsigned char* data, int len, unsigned char* authData)
{
    if (!(m_rtpAuthLen && len && data && authData && m_owner))
	return;

    // RFC 3711 4.2
    u_int32_t roc = htonl(m_owner->rollover());
    SHA1 h1(m_authIpad);
    h1.update(data,len);
    h1.update(&roc,sizeof(roc));
    h1.finalize();
    SHA1 hmac(m_authOpad);
    hmac.update(h1.rawDigest(),h1.rawLength());
    hmac.finalize();
    ::memcpy(authData,hmac.rawDigest(),m_rtpAuthLen);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
