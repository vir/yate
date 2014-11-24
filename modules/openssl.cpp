/**
 * openssl.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * OpenSSL based SSL/TLS socket support
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

#include <yatephone.h>

#include <string.h>

#include <openssl/opensslconf.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/err.h>

#ifndef OPENSSL_NO_AES
#include <openssl/aes.h>
#endif

#ifndef OPENSSL_NO_DES
#include <openssl/des.h>
#endif

using namespace TelEngine;
namespace { // anonymous

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

class SslContext : public String
{
public:
    // Constructor. Build the context
    SslContext(const char* name);
    // Initialize certificate, key and domains. Check the key
    // Return false on failure
    bool init(const NamedList& params);
    // Load a certificate and key. Check the key
    bool loadCertificate(const String& cert, const String& key);
    // Check if this context can be used for server sockets in a given domain
    bool hasDomain(const String& domain);
    // Add a comma separated list of domains to a buffer
    void addDomains(String& buf);
    // Release memory, free the context
    virtual void destruct();
    inline operator SSL_CTX*()
	{ return m_context; }
protected:
    SSL_CTX* m_context;
    ObjList m_domains;
};

class SslSocket : public Socket, public Mutex
{
public:
    SslSocket(SOCKET handle, bool server, int verify, SslContext* context = 0);
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
protected:
    AES_KEY* m_key;
    unsigned char m_initVector[AES_BLOCK_SIZE];
};

//AES - Cipher Feedback Mode
class AesCfbCipher : public AesCtrCipher
{
public:
    AesCfbCipher();
    virtual ~AesCfbCipher();
    virtual bool encrypt(void* outData, unsigned int len, const void* inpData);
    virtual bool decrypt(void* outData, unsigned int len, const void* inpData);
};

#endif

#ifndef OPENSSL_NO_DES
// CBC-DES Cipher - Cipher-Block Chaining Mode
class DesCtrCipher : public Cipher
{
public:
    enum {
	Des,
	Des3_2,
	Des3_3
    };
    DesCtrCipher(const char* type);
    virtual ~DesCtrCipher();
    virtual unsigned int blockSize() const
	{ return DES_KEY_SZ; }
    virtual unsigned int initVectorSize() const
	{ return DES_KEY_SZ; }
    virtual bool setKey(const void* key, unsigned int len, Direction dir);
    virtual bool initVector(const void* vect, unsigned int len, Direction dir);
    virtual bool encrypt(void* outData, unsigned int len, const void* inpData);
    virtual bool decrypt(void* outData, unsigned int len, const void* inpData);
    static bool initKey(const void* key,DES_key_schedule& k);
private:
    DES_key_schedule m_key1;
    DES_key_schedule m_key2;
    DES_key_schedule m_key3;
    unsigned char m_initVector[DES_KEY_SZ];
    int m_type;
    bool m_keysSet;
    static const TokenDict s_des[];
};
#endif

class CipherHandler : public MessageHandler
{
public:
    inline CipherHandler()
	: MessageHandler("engine.cipher")
	{ }
    virtual bool received(Message& msg);
};

class SslHandler;

class OpenSSL : public Module
{
public:
    OpenSSL();
    ~OpenSSL();
    virtual void initialize();
    // Find a context by name or domain
    // This method is not thread safe. The caller must lock the plugin
    // until the returned context is not used anymore
    SslContext* findContext(const String& token, bool byDomain = false) const;
    // Find a context from 'context' or 'domain' parameters
    // This method is not thread safe
    SslContext* findContext(Message& msg) const;
protected:
    virtual void statusParams(String& str);
    virtual void statusDetail(String& str);

    SslHandler* m_handler;
    ObjList m_contexts;                  // Server contexts list
    String m_statusCmd;                  // Module status command
};


static int s_index = -1;
static SSL_CTX* s_context = 0;
static OpenSSL __plugin;

#ifndef OPENSSL_NO_DES
const TokenDict DesCtrCipher::s_des[] = {
    { "des",    Des },
    { "des3_2", Des3_2 },
    { "des3_3", Des3_3 },
    { 0, 0 }
};
#endif

class SslHandler : public MessageHandler
{
public:
    inline SslHandler()
	: MessageHandler("socket.ssl",100,__plugin.name())
	{ }
    virtual bool received(Message& msg);
};


// Attempt to add randomness from system time when called
static void addRand(u_int64_t usec)
{
    // a rough estimation of 2 bytes of entropy
    ::RAND_add(&usec,sizeof(usec),2);
}

// Retrieve SslSocket from SSL structure
static inline SslSocket* sslSocket(const SSL* ssl)
{
    if (ssl && s_index >= 0)
	return static_cast<SslSocket*>(::SSL_get_ex_data(const_cast<SSL*>(ssl),s_index));
    return 0;
}

// Callback function called from OpenSSL for state changes and alerts
void infoCallback(const SSL* ssl, int where, int retVal)
{
    SslSocket* sock = sslSocket(ssl);
    if (sock) {
	if (sock->ssl() == ssl)
	    sock->onInfo(where,retVal);
	else
	    Debug(&__plugin,DebugFail,"Mismatched session %p [%p]",ssl,sock);
    }
}

#ifdef DEBUG
// Callback function called from OpenSSL for protocol messages
void msgCallback(int write, int version, int content_type, const void* buf,
    size_t len, SSL* ssl, void* arg)
{
    SslSocket* sock = sslSocket(ssl);
    if (!sock)
	return;
    if (sock->ssl() == ssl)
	Debug(&__plugin,DebugAll,
	    "%s SSL message: version=%d content_type=%d buf=%p len=%u [%p]",
	    write ? "Sent" : "Received",version,content_type,buf,(unsigned int)len,
	    sock);
    else
	Debug(&__plugin,DebugFail,"msgCallback: Mismatched session %p [%p]",ssl,sock);
}
#endif


SslContext::SslContext(const char* name)
    : String(name),
    m_context(0)
{
    m_context = ::SSL_CTX_new(::SSLv23_method());
    SSL_CTX_set_info_callback(m_context,infoCallback);
#ifdef DEBUG
    SSL_CTX_set_msg_callback(m_context,msgCallback);
#endif
}

// Initialize certificate, key and domains. Check the key
// Return false on failure
bool SslContext::init(const NamedList& params)
{
    // Load certificate and key. Check them
    if (!loadCertificate(params["certificate"],params["key"]))
	return false;
    // Load domains
    m_domains.clear();
    String* d = params.getParam("domains");
    if (d) {
	ObjList* list = d->split(',',false);
	for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	    String* s = static_cast<String*>(o->get());
	    s->trimBlanks();
	    if (s->null())
		continue;
	    if (s->startsWith("*") && (s->length() < 3 || (*s)[1] != '.')) {
		Debug(&__plugin,DebugNote,"Context '%s' ignoring invalid domain='%s'",
		    c_str(),s->c_str());
		continue;
	    }
	    m_domains.append(new String(s->toLower()));
	}
	TelEngine::destruct(list);
	DDebug(&__plugin,DebugAll,"Context '%s' loaded domains=%s",
	    c_str(),d->safe());
    }
    return true;
}

// Load a certificate and key. Check the key
bool SslContext::loadCertificate(const String& c, const String& k)
{
    String cert;
    if (c) {
	cert << Engine::configPath();
	if (cert && !cert.endsWith(Engine::pathSeparator()))
	    cert << Engine::pathSeparator();
	cert << c;
    }
    String key;
    if (k) {
	key << Engine::configPath();
	if (key && !key.endsWith(Engine::pathSeparator()))
	    key << Engine::pathSeparator();
	key << k;
    }
    else
	key = cert;
    if (!::SSL_CTX_use_certificate_chain_file(m_context,cert)) {
	unsigned long err = ::ERR_get_error();
	Debug(&__plugin,DebugWarn,
	    "Context '%s' failed to load certificate from '%s' '%s'",
	    c_str(),cert.c_str(),::ERR_error_string(err,0));
	return false;
    }
    if (!::SSL_CTX_use_PrivateKey_file(m_context,key,SSL_FILETYPE_PEM)) {
	unsigned long err = ::ERR_get_error();
	Debug(&__plugin,DebugWarn,
	    "Context '%s' failed to load key from '%s' '%s'",
	    c_str(),key.c_str(),::ERR_error_string(err,0));
	return false;
    }
    if (!::SSL_CTX_check_private_key(m_context)) {
	unsigned long err = ::ERR_get_error();
	Debug(&__plugin,DebugWarn,
	    "Context '%s' certificate='%s' or key='%s' are invalid '%s'",
	    c_str(),cert.c_str(),key.c_str(),::ERR_error_string(err,0));
	return false;
    }
    DDebug(&__plugin,DebugAll,"Context '%s' loaded certificate='%s' key='%s'",
	c_str(),cert.c_str(),key.c_str());
    return true;
}

// Check if this context can be used for server sockets in a given domain
bool SslContext::hasDomain(const String& domain)
{
    for (ObjList* o = m_domains.skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	if (*s == domain ||
	    ((*s)[0] == '*' && domain.endsWith(s->c_str() + 1)))
	    return true;
    }
    return false;
}

// Add a comma separated list of domains to a buffer
void SslContext::addDomains(String& buf)
{
    bool notFirst = false;
    for (ObjList* o = m_domains.skipNull(); o; o = o->skipNext()) {
	if (notFirst)
	    buf << ",";
	else
	    notFirst = true;
	buf << (static_cast<String*>(o->get()))->c_str();
    }
}

// Release memory, free the context
void SslContext::destruct()
{
    ::SSL_CTX_free(m_context);
    String::destruct();
}


// Create a SSL socket from a regular socket handle
SslSocket::SslSocket(SOCKET handle, bool server, int verify, SslContext* context)
    : Socket(handle), Mutex(false,"SslSocket"),
      m_ssl(0)
{
    DDebug(&__plugin,DebugAll,"SslSocket::SslSocket(%d,%s,%s,%s) [%p]",
	handle,String::boolText(server),lookup(verify,s_verifyMode,"unknown"),
	context ? context->c_str() : "",this);
    if (Socket::valid()) {
	m_ssl = ::SSL_new(context ? *context : s_context);
	if (s_index >= 0)
	    ::SSL_set_ex_data(m_ssl,s_index,this);
	::SSL_set_verify(m_ssl,verify,0);
	::SSL_set_fd(m_ssl,handle);
	BIO* bio = ::SSL_get_rbio(m_ssl);
	if (!(bio && BIO_set_close(bio,BIO_NOCLOSE)))
	    Debug(&__plugin,DebugGoOn,"SslSocket::SslSocket(%d) no BIO or cannot set NOCLOSE [%p]",
		handle,this);
	if (server)
	    ::SSL_set_accept_state(m_ssl);
	else
	    ::SSL_set_connect_state(m_ssl);
    }
}

// Destructor, clean up early
SslSocket::~SslSocket()
{
    DDebug(&__plugin,DebugAll,"SslSocket::~SslSocket() handle=%d [%p]",handle(),this);
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
	// SSL_free also destroys the BIO
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
    if (!m_ssl) {
	m_error = EINVAL;
	return socketError();
    }
    return sslError(::SSL_write(m_ssl,buffer,length));
}

// Read decrypted data from the SSL stream
int SslSocket::readData(void* buffer, int length)
{
    if (!buffer)
	length = 0;
    Lock lock(this);
    if (!m_ssl) {
	m_error = EINVAL;
	return socketError();
    }
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
		retcode = socketError();
		break;
	    case SSL_ERROR_SYSCALL:
		copyError();
		break;
	    default:
		m_error = EINVAL;
		retcode = socketError();
		break;
	}
#ifdef DEBUG
	if (!canRetry())
	    Debug(&__plugin,DebugNote,"SslSocket error='%s' state='%s' [%p]",
		ERR_error_string(ERR_get_error(),0),SSL_state_string_long(m_ssl),this);
#endif
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
	Debug(&__plugin,DebugAll,"State %s [%p]",SSL_state_string_long(m_ssl),this);
#endif
    if ((where & SSL_CB_EXIT) && (retVal == 0))
	Debug(&__plugin,DebugMild,"Failed %s [%p]",SSL_state_string_long(m_ssl),this);
    if (where & SSL_CB_ALERT)
	Debug(&__plugin,DebugMild,"Alert %s: %s [%p]",
	    SSL_alert_type_string_long(retVal),
	    SSL_alert_desc_string_long(retVal),this);
    if (where & SSL_CB_HANDSHAKE_DONE) {
	long verify = ::SSL_get_verify_result(m_ssl);
	if (verify != X509_V_OK) {
	    // handshake succeeded but the certificate has problems
	    const char* error = lookup(verify,s_verifyCodes);
	    Debug(&__plugin,DebugWarn,"Certificate verify error %ld%s%s [%p]",
		verify,error ? ": " : "",c_safe(error),this);
	}
    }
}


// Handler for the socket.ssl message - turns regular sockets into SSL
bool SslHandler::received(Message& msg)
{
    if (msg.getBoolValue("test")) {
	if (!msg.getBoolValue("server"))
	    return true;
	Lock lock(__plugin);
	return 0 != __plugin.findContext(msg);
    }
    addRand(msg.msgTime());
    Socket** ppSock = static_cast<Socket**>(msg.userObject(YATOM("Socket*")));
    if (!ppSock) {
	Debug(&__plugin,DebugGoOn,"SslHandler: No pointer to Socket");
	return false;
    }
    Socket* pSock = *ppSock;
    if (!pSock) {
	Debug(&__plugin,DebugGoOn,"SslHandler: NULL Socket pointer");
	return false;
    }
    if (!pSock->valid()) {
	Debug(&__plugin,DebugWarn,"SslHandler: Invalid Socket");
	return false;
    }
    SslSocket* sSock = 0;
    if (msg.getBoolValue("server",false)) {
	Lock lock(&__plugin);
	SslContext* c = __plugin.findContext(msg);
	if (!c)
	    return false;
	sSock = new SslSocket(pSock->handle(),true,
	    msg.getIntValue("verify",s_verifyMode,SSL_VERIFY_NONE),c);
    }
    else {
	const String& cert = msg["certificate"];
	SslContext* c = cert ? new SslContext(msg) : 0;
	if (!c || c->loadCertificate(cert,msg["key"]))
	    sSock = new SslSocket(pSock->handle(),false,
		msg.getIntValue("verify",s_verifyMode,SSL_VERIFY_NONE),c);
	TelEngine::destruct(c);
    }
    if (!(sSock && sSock->valid())) {
	if (sSock) {
	    Debug(&__plugin,DebugWarn,"SslHandler: Invalid SSL Socket");
	    // detach and destroy new socket, preserve old one
	    sSock->detach();
	    delete sSock;
	}
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
    DDebug(&__plugin,DebugAll,"AesCtrCipher::AesCtrCipher() key=%p [%p]",m_key,this);
}

AesCtrCipher::~AesCtrCipher()
{
    DDebug(&__plugin,DebugAll,"AesCtrCipher::~AesCtrCipher() key=%p [%p]",m_key,this);
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

AesCfbCipher::AesCfbCipher()
{
    DDebug(&__plugin,DebugAll,"AesCfbCipher::AesCfbCipher() key=%p [%p]",m_key,this);
}

AesCfbCipher::~AesCfbCipher()
{
    DDebug(&__plugin,DebugAll,"AesCfbCipher::~AesCfbCipher() key=%p [%p]",m_key,this);
}

bool AesCfbCipher::encrypt(void* outData, unsigned int len, const void* inpData)
{
    if (!(outData && len))
	return false;
    if (!inpData)
	inpData = outData;
    int num = 0;
    AES_cfb128_encrypt(
	(const unsigned char*)inpData,
	(unsigned char*)outData,
	len,
	m_key,
	m_initVector,
	&num,
	AES_ENCRYPT);
    return true;
}

bool AesCfbCipher::decrypt(void* outData, unsigned int len, const void* inpData)
{
    if (!(outData && len))
	return false;
    if (!inpData)
	inpData = outData;
    int num = 0;
    AES_cfb128_encrypt(
	(const unsigned char*)inpData,
	(unsigned char*)outData,
	len,
	m_key,
	m_initVector,
	&num,
	AES_DECRYPT);
    return true;
}

#endif

#ifndef OPENSSL_NO_DES
DesCtrCipher::DesCtrCipher(const char* type)
    : m_keysSet(false)
{
    m_type = lookup(type,s_des,Des);
    DDebug(&__plugin,DebugAll,"DesCtrCipher::DesCtrCipher() key=%p [%p]",&m_key1,this);
}

DesCtrCipher::~DesCtrCipher()
{
    DDebug(&__plugin,DebugAll,"DesCtrCipher::~DesCtrCipher() key=%p [%p]",&m_key1,this);
}

bool DesCtrCipher::initKey(const void* key,DES_key_schedule& k)
{
    DES_cblock nativeKey;
    ::memcpy(nativeKey,key,8);

    DES_set_odd_parity(&nativeKey);
    return 0 == DES_set_key_checked(&nativeKey,&k);
}

bool DesCtrCipher::setKey(const void* key, unsigned int len, Direction dir)
{
    m_keysSet = false;
    if (!(key && len))
	return false;
    bool lenOk = false;
    uint8_t* buf = (uint8_t*)key;

    switch (m_type) {
	case Des:
	    if ((lenOk = (len == 8)))
		m_keysSet = initKey(buf,m_key1);
	    break;
	case Des3_2:
	    if ((lenOk = (len == 16)))
		m_keysSet = initKey(buf,m_key1) && initKey(buf + 8,m_key2);
	    break;
	case Des3_3:
	    if (len == 16) {
		Debug(&__plugin,DebugAll,"Key length=%u too short for 3-key DES cipher, switching to 2-key DES cipher [%p]",
			len,this);
		m_keysSet = initKey(buf,m_key3);
	    } else if (len == 24) {
		m_keysSet = initKey(buf + 16,m_key3);
	    } else // lenOk = false
		break;
	    lenOk = true;
	    m_keysSet = m_keysSet && initKey(buf,m_key1) && initKey(buf + 8,m_key2);
	    break;
	default:
	    Debug(&__plugin,DebugStub,"DesCtrCipher::setKey() Unknown cipher type '%s'",lookup(m_type,s_des));
	    return m_keysSet;
    }
    if (!lenOk) {
	Debug(&__plugin,DebugMild,"Invalid key length %u for cipher type %s",
		len,lookup(m_type,s_des));
	return false;
    }
    return m_keysSet;
}

bool DesCtrCipher::initVector(const void* vect, unsigned int len, Direction dir)
{
    if (len && !vect)
	return false;
    if (len > DES_KEY_SZ)
	len = DES_KEY_SZ;
    if (len < DES_KEY_SZ)
	::memset(m_initVector,0,DES_KEY_SZ);
    if (len)
	::memcpy(m_initVector,vect,len);
    return true;
}

bool DesCtrCipher::encrypt(void* outData, unsigned int len, const void* inpData)
{
    if (!m_keysSet) {
	Debug(&__plugin,DebugNote,"DesCtrCipher::encrypt() Please set the keys first! [%p]",this);
	return false;
    }
    DDebug(&__plugin,DebugAll,"DesCtrCipher::encrypt(%p, %d. %p) [%p]",outData,len,inpData,this);
    if (!(outData && len))
	return false;
    if (len % 8 != 0) {
	Debug(&__plugin,DebugWarn,"DesCtrCipher::encrypt() - length of data block to be encrypted is not a multiple of 8, memory corruption possible - encryption aborted");
	return false;
    }
    if (!inpData)
	inpData = outData;

    switch (m_type) {
	case Des:
	    DES_ncbc_encrypt((const unsigned char*)inpData,(unsigned char*)outData,len,&m_key1,(DES_cblock *)m_initVector,DES_ENCRYPT);
	    break;
	case Des3_2:
	    DES_ede2_cbc_encrypt((const unsigned char*)inpData,(unsigned char*)outData,len,&m_key1,&m_key2,(DES_cblock *)m_initVector,DES_ENCRYPT);
	    break;
	case Des3_3:
	    DES_ede3_cbc_encrypt((const unsigned char*)inpData,(unsigned char*)outData,len,&m_key1,&m_key2,&m_key3,(DES_cblock *)m_initVector,DES_ENCRYPT);
	    break;
	default:
	    return false;
    }
    return true;
}

bool DesCtrCipher::decrypt(void* outData, unsigned int len, const void* inpData)
{
    if (!m_keysSet) {
	Debug(&__plugin,DebugNote,"DesCtrCipher::dencrypt() Please set the keys first! [%p]",this);
	return false;
    }

    DDebug(&__plugin,DebugAll,"DesCtrCipher::decrypt(%p, %d. %p) [%p]",outData,len,inpData,this);
    if (!(outData && len))
	return false;
    if (len % 8 != 0) {
	Debug(&__plugin,DebugWarn,"DesCtrCipher::decrypt() - length of data block to be decrypted is not a multiple of 8, memory corruption possible - decryption aborted");
	return false;
    }
    if (!inpData)
	inpData = outData;
    switch (m_type) {
	case Des:
	    DES_ncbc_encrypt((const unsigned char*)inpData,(unsigned char*)outData,len,&m_key1,(DES_cblock *)m_initVector,DES_DECRYPT);
	    break;
	case Des3_2:
	    DES_ede2_cbc_encrypt((const unsigned char*)inpData,(unsigned char*)outData,len,&m_key1,&m_key2,(DES_cblock *)m_initVector,DES_DECRYPT);
	    break;
	case Des3_3:
	    DES_ede3_cbc_encrypt((const unsigned char*)inpData,(unsigned char*)outData,len,&m_key1,&m_key2,&m_key3,(DES_cblock *)m_initVector,DES_DECRYPT);
	    break;
	default:
	    return false;
    }
    return true;
}
#endif

// Handler for the engine.cipher message - Cipher Factory
bool CipherHandler::received(Message& msg)
{
    addRand(msg.msgTime());
    const String* name = msg.getParam("cipher");
    if (!name)
	return false;
    Cipher** ppCipher = static_cast<Cipher**>(msg.userObject(YATOM("Cipher*")));
#ifndef OPENSSL_NO_AES
    if (*name == "aes_ctr") {
	if (ppCipher)
	    *ppCipher = new AesCtrCipher();
	return true;
    }
    if (*name == "aes_cfb") {
	if (ppCipher)
	    *ppCipher = new AesCfbCipher();
	return true;
    }
#endif
#ifndef OPENSSL_NO_DES
    if (*name == "des_cbc") {
	if (ppCipher)
	    *ppCipher = new DesCtrCipher(msg.getValue(YSTRING("type"),"des"));
	return true;
    }
#endif
    return false;
}


OpenSSL::OpenSSL()
    : Module("openssl","misc",true),
      m_handler(0)
{
    Output("Loaded module OpenSSL - based on " OPENSSL_VERSION_TEXT);
    m_statusCmd << "status " << name();
}

OpenSSL::~OpenSSL()
{
    Output("Unloading module OpenSSL");
    ::SSL_CTX_free(s_context);
}

void OpenSSL::initialize()
{
    Output("Initializing module OpenSSL");
    Configuration cfg(Engine::configFile("openssl"));
    if (!m_handler) {
	setup();
	::SSL_load_error_strings();
	::SSL_library_init();
	addRand(Time::now());
	s_index = ::SSL_get_ex_new_index(0,const_cast<char*>("TelEngine::SslSocket"),0,0,0);
	s_context = ::SSL_CTX_new(::SSLv23_method());
	SSL_CTX_set_info_callback(s_context,infoCallback); // macro - no ::
	m_handler = new SslHandler;
	Engine::install(m_handler);
#if !defined(OPENSSL_NO_AES) || !defined(OPENSSL_NO_DES)
	Engine::install(new CipherHandler);
#endif
    }

    lock();
    // Load server contexts
    unsigned int n = cfg.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* p = cfg.getSection(i);
	if (!p || *p == "general" || !p->c_str())
	    continue;
	SslContext* context = findContext(*p);
	if (!p->getBoolValue("enable",true)) {
	    if (context) {
		DDebug(this,DebugAll,"Removing disabled context '%s'",context->c_str());
		m_contexts.remove(context);
	    }
	    continue;
	}
	if (!context)
	    context = new SslContext(*p);
	if (context->init(*p)) {
	    if (!findContext(*p)) {
		m_contexts.append(context);
		DDebug(this,DebugAll,"Added context '%s'",context->c_str());
	    }
	}
	else {
	    if (findContext(*p)) {
		DDebug(this,DebugAll,"Removing invalid context '%s'",context->c_str());
		m_contexts.remove(context);
	    }
	    else {
		DDebug(this,DebugAll,"Ignoring invalid context '%s'",context->c_str());
		TelEngine::destruct(context);
	    }
	}
    }
    unlock();
}

// Find a context by name or domain
SslContext* OpenSSL::findContext(const String& token, bool byDomain) const
{
    if (!byDomain) {
	ObjList* o = m_contexts.find(token);
	return o ? static_cast<SslContext*>(o->get()) : 0;
    }
    for (ObjList* o = m_contexts.skipNull(); o; o = o->skipNext()) {
	SslContext* c = static_cast<SslContext*>(o->get());
	if (c->hasDomain(token))
	    return c;
    }
    return 0;
}

// Find a context from 'context' or 'domain' parameters
SslContext* OpenSSL::findContext(Message& msg) const
{
    SslContext* c = 0;
    const String& context = msg["context"];
    String domain;
    if (context)
	c = __plugin.findContext(context);
    if (!c) {
	domain = msg["domain"];
	if (domain)
	    c = __plugin.findContext(domain.toLower(),true);
    }
    if (c)
	return c;
    Debug(this,DebugWarn,
	"SslHandler: Unable to find a server context for context=%s or domain=%s",
	context.safe(),domain.safe());
    return 0;
}

void OpenSSL::statusParams(String& str)
{
    Lock lock(this);
    str << "contexts=" << m_contexts.count();
}

void OpenSSL::statusDetail(String& str)
{
    Lock lock(this);
    for (ObjList* o = m_contexts.skipNull(); o; o = o->skipNext()) {
	SslContext* c = static_cast<SslContext*>(o->get());
	str.append(c->c_str(),";");
	str << "=";
	c->addDomains(str);
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
