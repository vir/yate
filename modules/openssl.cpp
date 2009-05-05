/**
 * openssl.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * OpenSSL based SSL/TLS socket support
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <yatengine.h>

#include <string.h>

#include <openssl/opensslconf.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>

#ifndef OPENSSL_NO_AES
#include <openssl/aes.h>
#endif

using namespace TelEngine;
namespace { // anonymous

#define MODNAME "openssl"

#define MAKE_ERR(x) { #x, X509_V_ERR_##x }
static TokenDict s_verifyCodes[] = {
    MAKE_ERR(UNABLE_TO_GET_ISSUER_CERT),
    MAKE_ERR(UNABLE_TO_DECRYPT_CERT_SIGNATURE),
    MAKE_ERR(UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY),
    MAKE_ERR(CERT_SIGNATURE_FAILURE),
    MAKE_ERR(CERT_NOT_YET_VALID),
    MAKE_ERR(CERT_HAS_EXPIRED),
    MAKE_ERR(ERROR_IN_CERT_NOT_BEFORE_FIELD),
    MAKE_ERR(DEPTH_ZERO_SELF_SIGNED_CERT),
    MAKE_ERR(SELF_SIGNED_CERT_IN_CHAIN),
    MAKE_ERR(UNABLE_TO_GET_ISSUER_CERT_LOCALLY),
    MAKE_ERR(UNABLE_TO_VERIFY_LEAF_SIGNATURE),
    MAKE_ERR(INVALID_CA),
    MAKE_ERR(PATH_LENGTH_EXCEEDED),
    MAKE_ERR(INVALID_PURPOSE),
    MAKE_ERR(CERT_UNTRUSTED),
    MAKE_ERR(CERT_REJECTED),
    { 0, 0 }
};
#undef MAKE_ERR

static TokenDict s_verifyMode[] = {
    // don't ask for a certificate, don't stop if verification fails
    { "none", SSL_VERIFY_NONE },
    // certificate is verified only if provided (a server always provides one)
    { "peer", SSL_VERIFY_PEER },
    // server only - verify client certificate only if provided and only once
    { "only", SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE },
    // server only - client must provide a certificate at every (re)negotiation
    { "must", SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT },
    // server only - client must provide a certificate only at first negotiation
    { "once", SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT | SSL_VERIFY_CLIENT_ONCE },
    { 0, 0 }
};


class SslSocket : public Socket, public Mutex
{
public:
    SslSocket(SOCKET handle, bool server, int verify);
    virtual ~SslSocket();
    virtual bool terminate();
    virtual bool valid();
    virtual int writeData(const void* buffer, int length);
    virtual int readData(void* buffer, int length);
    void onInfo(int where, int retVal);
    inline SSL* ssl() const
	{ return m_ssl; }
private:
    int sslError(int retcode);
    SSL* m_ssl;
};

class SslHandler : public MessageHandler
{
public:
    inline SslHandler()
	: MessageHandler("socket.ssl")
	{ }
    virtual bool received(Message& msg);
};

#ifndef OPENSSL_NO_AES
// AES Counter Mode
class AesCtrCipher : public Cipher
{
public:
    AesCtrCipher();
    virtual ~AesCtrCipher();
    virtual unsigned int blockSize() const
	{ return AES_BLOCK_SIZE; }
    virtual unsigned int initVectorSize() const
	{ return AES_BLOCK_SIZE; }
    virtual bool setKey(const void* key, unsigned int len, Direction dir);
    virtual bool initVector(const void* vect, unsigned int len, Direction dir);
    virtual bool encrypt(void* outData, unsigned int len, const void* inpData);
    virtual bool decrypt(void* outData, unsigned int len, const void* inpData);
private:
    AES_KEY* m_key;
    unsigned char m_initVector[AES_BLOCK_SIZE];
};

class CipherHandler : public MessageHandler
{
public:
    inline CipherHandler()
	: MessageHandler("engine.cipher")
	{ }
    virtual bool received(Message& msg);
};
#endif

class OpenSSL : public Plugin
{
public:
    OpenSSL();
    ~OpenSSL();
    virtual void initialize();
protected:
    SslHandler* m_handler;
};

INIT_PLUGIN(OpenSSL);

static int s_index = -1;
static SSL_CTX* s_context = 0;


// Attempt to add randomness from system time when called
static void addRand(u_int64_t usec)
{
    // a rough estimation of 2 bytes of entropy
    ::RAND_add(&usec,sizeof(usec),2);
}

// Callback function called from OpenSSL for state changes and alerts
void infoCallback(const SSL* ssl, int where, int retVal)
{
    SslSocket* sock = 0;
    if (s_index >= 0)
	sock = static_cast<SslSocket*>(::SSL_get_ex_data(const_cast<SSL*>(ssl),s_index));
    if (sock) {
	if (sock->ssl() == ssl)
	    sock->onInfo(where,retVal);
	else
	    Debug(MODNAME,DebugFail,"Mismatched session %p [%p]",ssl,sock);
    }
}


// Create a SSL socket from a regular socket handle
SslSocket::SslSocket(SOCKET handle, bool server, int verify)
    : Socket(handle), Mutex(false,"SslSocket"),
      m_ssl(0)
{
    DDebug(DebugAll,"SslSocket::SslSocket(%d,%s,%s) [%p]",
	handle,String::boolText(server),lookup(verify,s_verifyMode,"unknown"),this);
    if (Socket::valid()) {
	m_ssl = ::SSL_new(s_context);
	if (s_index >= 0)
	    ::SSL_set_ex_data(m_ssl,s_index,this);
	::SSL_set_verify(m_ssl,verify,0);
	::SSL_set_fd(m_ssl,handle);
	if (server)
	    ::SSL_set_accept_state(m_ssl);
	else
	    ::SSL_set_connect_state(m_ssl);
    }
}

// Destructor, clean up early
SslSocket::~SslSocket()
{
    DDebug(DebugAll,"SslSocket::~SslSocket() handle=%d [%p]",handle(),this);
    clearFilters();
    terminate();
}

// Terminate the socket and the SSL session around it
bool SslSocket::terminate()
{
    lock();
    if (m_ssl) {
	if (s_index >= 0)
	    ::SSL_set_ex_data(m_ssl,s_index,0);
	::SSL_shutdown(m_ssl);
	::SSL_free(m_ssl);
	m_ssl = 0;
	// SSL_free also destroys the BIO and the socket so get rid of them
	detach();
    }
    unlock();
    return Socket::terminate();
}

// Check if the socket is valid
bool SslSocket::valid()
{
    return m_ssl && Socket::valid();
}

// Write unencrypted data through the SSL stream
int SslSocket::writeData(const void* buffer, int length)
{
    if (!buffer)
	length = 0;
    Lock lock(this);
    return sslError(::SSL_write(m_ssl,buffer,length));
}

// Read decrypted data from the SSL stream
int SslSocket::readData(void* buffer, int length)
{
    if (!buffer)
	length = 0;
    Lock lock(this);
    return sslError(::SSL_read(m_ssl,buffer,length));
}

// Deal with the various SSL error codes
int SslSocket::sslError(int retcode)
{
    if (retcode <= 0) {
	switch (::SSL_get_error(m_ssl,retcode)) {
	    case SSL_ERROR_ZERO_RETURN:
		clearError();
		retcode = 0;
		break;
	    case SSL_ERROR_WANT_READ:
	    case SSL_ERROR_WANT_WRITE:
	    case SSL_ERROR_WANT_CONNECT:
	    case SSL_ERROR_WANT_ACCEPT:
		m_error = EAGAIN;
		retcode = 0;
		break;
	    case SSL_ERROR_SYSCALL:
		copyError();
		break;
	    default:
		m_error = EINVAL;
		retcode = -1;
		break;
	}
    }
    else
	clearError();
    return retcode;
}

// Callback function called from OpenSSL for state changes and alerts
void SslSocket::onInfo(int where, int retVal)
{
#ifdef DEBUG
    if (where & SSL_CB_LOOP)
	Debug(MODNAME,DebugAll,"State %s [%p]",SSL_state_string_long(m_ssl),this);
#endif
    if ((where & SSL_CB_EXIT) && (retVal == 0))
	Debug(MODNAME,DebugMild,"Failed %s [%p]",SSL_state_string_long(m_ssl),this);
    if (where & SSL_CB_ALERT)
	Debug(MODNAME,DebugMild,"Alert %s: %s [%p]",
	    SSL_alert_type_string_long(retVal),
	    SSL_alert_desc_string_long(retVal),this);
    if (where & SSL_CB_HANDSHAKE_DONE) {
	long verify = ::SSL_get_verify_result(m_ssl);
	if (verify != X509_V_OK) {
	    // handshake succeeded but the certificate has problems
	    const char* error = lookup(verify,s_verifyCodes);
	    Debug(MODNAME,DebugWarn,"Certificate verify error %ld%s%s [%p]",
		verify,error ? ": " : "",c_safe(error),this);
	}
    }
}


// Handler for the socket.ssl message - turns regular sockets into SSL
bool SslHandler::received(Message& msg)
{
    addRand(msg.msgTime());
    Socket** ppSock = static_cast<Socket**>(msg.userObject("Socket*"));
    if (!ppSock) {
	Debug(MODNAME,DebugGoOn,"SslHandler: No pointer to Socket");
	return false;
    }
    Socket* pSock = *ppSock;
    if (!pSock) {
	Debug(MODNAME,DebugGoOn,"SslHandler: NULL Socket pointer");
	return false;
    }
    if (!pSock->valid()) {
	Debug(MODNAME,DebugWarn,"SslHandler: Invalid Socket");
	return false;
    }
    SslSocket* sSock = new SslSocket(pSock->handle(),
	msg.getBoolValue("server",false),
	msg.getIntValue("verify",s_verifyMode,SSL_VERIFY_NONE));
    if (!sSock->valid()) {
	Debug(MODNAME,DebugWarn,"SslHandler: Invalid SSL Socket");
	// detach and destroy new socket, preserve old one
	sSock->detach();
	delete sSock;
	return false;
    }
    // replace socket, detach and destroy old one
    *ppSock = sSock;
    pSock->detach();
    delete pSock;
    return true;
}


#ifndef OPENSSL_NO_AES
AesCtrCipher::AesCtrCipher()
    : m_key(0)
{
    m_key = new AES_KEY;
    DDebug(DebugAll,"AesCtrCipher::AesCtrCipher() key=%p [%p]",m_key,this);
}

AesCtrCipher::~AesCtrCipher()
{
    DDebug(DebugAll,"AesCtrCipher::~AesCtrCipher() key=%p [%p]",m_key,this);
    delete m_key;
}

bool AesCtrCipher::setKey(const void* key, unsigned int len, Direction dir)
{
    if (!(key && len && m_key))
	return false;
    // AES_ctr128_encrypt is its own inverse
    return 0 == AES_set_encrypt_key((const unsigned char*)key,len*8,m_key);
}

bool AesCtrCipher::initVector(const void* vect, unsigned int len, Direction dir)
{
    if (len && !vect)
	return false;
    if (len > AES_BLOCK_SIZE)
	len = AES_BLOCK_SIZE;
    if (len < AES_BLOCK_SIZE)
	::memset(m_initVector,0,AES_BLOCK_SIZE);
    if (len)
	::memcpy(m_initVector,vect,len);
    return true;
}

bool AesCtrCipher::encrypt(void* outData, unsigned int len, const void* inpData)
{
    if (!(outData && len))
	return false;
    if (!inpData)
	inpData = outData;
    unsigned int num = 0;
    unsigned char eCountBuf[AES_BLOCK_SIZE];
    AES_ctr128_encrypt(
	(const unsigned char*)inpData,
	(unsigned char*)outData,
	len,
	m_key,
	m_initVector,
	eCountBuf,
	&num);
    return true;
}

bool AesCtrCipher::decrypt(void* outData, unsigned int len, const void* inpData)
{
    // AES_ctr128_encrypt is its own inverse
    return encrypt(outData,len,inpData);
}


// Handler for the engine.cipher message - Cipher Factory
bool CipherHandler::received(Message& msg)
{
    addRand(msg.msgTime());
    const String* name = msg.getParam("cipher");
    if (!name)
	return false;
    Cipher** ppCipher = static_cast<Cipher**>(msg.userObject("Cipher*"));
    if (*name == "aes_ctr") {
	if (ppCipher)
	    *ppCipher = new AesCtrCipher();
	return true;
    }
    return false;
}
#endif


OpenSSL::OpenSSL()
    : Plugin("openssl",true),
      m_handler(0)
{
    Output("Loaded module OpenSSL - based on " OPENSSL_VERSION_TEXT);
}

OpenSSL::~OpenSSL()
{
    Output("Unloading module OpenSSL");
    ::SSL_CTX_free(s_context);
}

void OpenSSL::initialize()
{
    if (m_handler)
	return;
    Output("Initializing module OpenSSL");
    ::SSL_load_error_strings();
    ::SSL_library_init();
    addRand(Time::now());
    s_index = ::SSL_get_ex_new_index(0,const_cast<char*>("TelEngine::SslSocket"),0,0,0);
    s_context = ::SSL_CTX_new(::SSLv23_method());
    SSL_CTX_set_info_callback(s_context,infoCallback); // macro - no ::
    m_handler = new SslHandler;
    Engine::install(m_handler);
#ifndef OPENSSL_NO_AES
    Engine::install(new CipherHandler);
#endif
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
