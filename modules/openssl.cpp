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

#include <openssl/ssl.h>
#include <openssl/rand.h>

using namespace TelEngine;
namespace { // anonymous

class SslSocket : public Socket, public Mutex
{
public:
    SslSocket(SOCKET handle, bool server);
    virtual ~SslSocket();
    virtual bool terminate();
    virtual bool valid();
    virtual int writeData(const void* buffer, int length);
    virtual int readData(void* buffer, int length);
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

static SSL_CTX* s_context = 0;


// Attempt to add randomness from system time when called
static void addRand(u_int64_t usec)
{
    // a rough estimation of 2 bytes of entropy
    ::RAND_add(&usec,sizeof(usec),2);
}


// Create a SSL socket from a regular socket handle
SslSocket::SslSocket(SOCKET handle, bool server)
    : Socket(handle),
      m_ssl(0)
{
    DDebug(DebugAll,"SslSocket::SslSocket(%d,%s) [%p]",
	handle,String::boolText(server),this);
    if (Socket::valid()) {
	m_ssl = ::SSL_new(s_context);
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


// Handler for the socket.ssl message - turns regular sockets into SSL
bool SslHandler::received(Message& msg)
{
    addRand(msg.msgTime());
    Socket** ppSock = static_cast<Socket**>(msg.userObject("Socket*"));
    if (!ppSock) {
	Debug("openssl",DebugGoOn,"SslHandler: No pointer to Socket");
	return false;
    }
    Socket* pSock = *ppSock;
    if (!pSock) {
	Debug("openssl",DebugGoOn,"SslHandler: NULL Socket pointer");
	return false;
    }
    if (!pSock->valid()) {
	Debug("openssl",DebugWarn,"SslHandler: Invalid Socket");
	return false;
    }
    SslSocket* sSock = new SslSocket(pSock->handle(),msg.getBoolValue("server",false));
    if (!sSock->valid()) {
	Debug("openssl",DebugWarn,"SslHandler: Invalid SSL Socket");
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
    s_context = ::SSL_CTX_new(::SSLv23_method());
    m_handler = new SslHandler;
    Engine::install(m_handler);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
