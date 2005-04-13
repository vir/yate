/**
 * Socket.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004 Null Team
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "yateclass.h"

#include <string.h>

#ifndef _WINDOWS
#include <fcntl.h>
#endif

using namespace TelEngine;

Socket::Socket()
    : m_error(0), m_handle(invalidHandle())
{
    DDebug(DebugAll,"Socket::Socket() [%p]",this);
}

Socket::Socket(SOCKET handle)
    : m_error(0), m_handle(handle)
{
    DDebug(DebugAll,"Socket::Socket(%d) [%p]",handle,this);
}

Socket::Socket(int domain, int type, int protocol)
    : m_error(0), m_handle(invalidHandle())
{
    DDebug(DebugAll,"Socket::Socket(%d,%d,%d) [%p]",domain,type,protocol,this);
    m_handle = ::socket(domain,type,protocol);
    if (!valid())
	copyError();
}

Socket::~Socket()
{
    DDebug(DebugAll,"Socket::~Socket() handle=%d [%p]",m_handle,this);
    terminate();
}

bool Socket::create(int domain, int type, int protocol)
{
    DDebug(DebugAll,"Socket::create(%d,%d,%d) [%p]",domain,type,protocol,this);
    terminate();
    m_handle = ::socket(domain,type,protocol);
    if (valid()) {
	clearError();
	return true;
    }
    else {
	copyError();
	return false;
    }
}

bool Socket::terminate()
{
    bool ret = true;
    SOCKET tmp = m_handle;
    if (tmp != invalidHandle()) {
	DDebug(DebugAll,"Socket::terminate() handle=%d [%p]",m_handle,this);
	m_handle = invalidHandle();
#ifdef _WINDOWS
	ret = !::closesocket(tmp);
#else
	ret = !::close(tmp);
#endif
    }
    if (ret)
	clearError();
    else {
	copyError();
	// put back the handle, we may have another chance later
	m_handle = tmp;
    }
    return ret;
}

void Socket::attach(SOCKET handle)
{
    DDebug(DebugAll,"Socket::attach(%d) [%p]",handle,this);
    if (handle == m_handle)
	return;
    terminate();
    m_handle = handle;
    clearError();
}

SOCKET Socket::detach()
{
    DDebug(DebugAll,"Socket::detach() handle=%d [%p]",m_handle,this);
    SOCKET tmp = m_handle;
    m_handle = invalidHandle();
    clearError();
    return tmp;
}

SOCKET Socket::invalidHandle()
{
#ifdef _WINDOWS
    return INVALID_SOCKET;
#else
    return -1;
#endif
}

int Socket::socketError()
{
#ifdef _WINDOWS
    return SOCKET_ERROR;
#else
    return -1;
#endif
}

void Socket::copyError()
{
#ifdef _WINDOWS
    m_error = WSAGetLastError();
#else
    m_error = errno;
#endif
}

bool Socket::checkError(int retcode, bool strict)
{
    if (strict && (retcode != socketError()))
	retcode = 0;
    if (retcode) {
	copyError();
	return false;
    }
    else {
	clearError();
	return true;
    }
}

bool Socket::canRetry() const
{
    if (!m_error)
	return true;
#ifdef _WINDOWS
    return (m_error == WSAEWOULDBLOCK);
#else
    return (m_error == EAGAIN) || (m_error == EINTR) || (m_error == EWOULDBLOCK);
#endif
}

bool Socket::bind(struct sockaddr* addr, socklen_t addrlen)
{
    return checkError(::bind(m_handle,addr,addrlen));
}

bool Socket::listen(unsigned int backlog)
{
    if ((backlog == 0) || (backlog > SOMAXCONN))
	backlog = SOMAXCONN;
    return checkError(::listen(m_handle,backlog));
}

Socket* Socket::accept(struct sockaddr* addr, socklen_t* addrlen)
{
    SOCKET sock = acceptHandle(addr,addrlen);
    return (sock == invalidHandle()) ? 0 : new Socket(sock);
}

SOCKET Socket::acceptHandle(struct sockaddr* addr, socklen_t* addrlen)
{
    if (addrlen && !addr)
	*addrlen = 0;
    SOCKET res = ::accept(m_handle,addr,addrlen);
    if (res == invalidHandle())
	copyError();
    else
	clearError();
    return res;
}

int Socket::sendTo(const void* buffer, int length, const struct sockaddr* addr, socklen_t adrlen, int flags)
{
    if (!buffer)
	length = 0;
    int res = ::sendto(m_handle,(const char*)buffer,length,flags,addr,adrlen);
    checkError(res,true);
    return res;
}

int Socket::send(const void* buffer, int length, int flags)
{
    if (!buffer)
	length = 0;
    int res = ::send(m_handle,(const char*)buffer,length,flags);
    checkError(res,true);
    return res;
}

int Socket::writeData(const void* buffer, int length)
{
#ifdef _WINDOWS
    return send(buffer,length);
#else
    if (!buffer)
	length = 0;
    int res = ::write(m_handle,buffer,length);
    checkError(res,true);
    return res;
#endif
}

int Socket::writeData(const char* str)
{
    if (null(str))
	return 0;
    int len = ::strlen(str);
    return writeData(str,len);
}

int Socket::recvFrom(void* buffer, int length, struct sockaddr* addr, socklen_t* adrlen, int flags)
{
    if (!buffer)
	length = 0;
    if (adrlen && !addr)
	*adrlen = 0;
    int res = ::recvfrom(m_handle,(char*)buffer,length,flags,addr,adrlen);
    checkError(res,true);
    return res;
}

int Socket::recv(void* buffer, int length, int flags)
{
    if (!buffer)
	length = 0;
    int res = ::recv(m_handle,(char*)buffer,length,flags);
    checkError(res,true);
    return res;
}

int Socket::readData(void* buffer, int length)
{
#ifdef _WINDOWS
    return recv(buffer,length);
#else
    if (!buffer)
	length = 0;
    int res = ::read(m_handle,buffer,length);
    checkError(res,true);
    return res;
#endif
}

bool Socket::select(bool* readok, bool* writeok, bool* except, struct timeval* timeout)
{
    fd_set readfd,writefd,exceptfd;
    fd_set *rfds = 0;
    fd_set *wfds = 0;
    fd_set *efds = 0;
    if (readok) {
	rfds = &readfd;
	FD_ZERO(rfds);
	FD_SET(m_handle,rfds);
    }
    if (writeok) {
	wfds = &writefd;
	FD_ZERO(wfds);
	FD_SET(m_handle,wfds);
    }
    if (except) {
	efds = &exceptfd;
	FD_ZERO(efds);
	FD_SET(m_handle,efds);
    }
    if (checkError(::select(m_handle+1,rfds,wfds,efds,timeout),true)) {
	if (readok)
	    *readok = (FD_ISSET(m_handle,rfds) != 0);
	if (writeok)
	    *writeok = (FD_ISSET(m_handle,wfds) != 0);
	if (except)
	    *except = (FD_ISSET(m_handle,efds) != 0);
	return true;
    }
    return false;
}

bool Socket::setOption(int level, int name, const void* value, socklen_t length)
{
    if (!value)
	length = 0;
    return checkError(::setsockopt(m_handle,level,name,(const char*)value,length));
}

bool Socket::getOption(int level, int name, void* buffer, socklen_t* length)
{
    if (length && !buffer)
	*length = 0;
    return checkError(::getsockopt(m_handle,level,name,(char*)buffer,length));
}

bool Socket::setTOS(int tos)
{
#ifdef IP_TOS
    return setOption(IPPROTO_IP,IP_TOS,&tos,sizeof(tos));
#else
    m_error = ENOTIMPL;
    return false;
#endif
}

bool Socket::setBlocking(bool block)
{
    unsigned long flags = 1;
#ifdef _WINDOWS
    if (block)
	flags = 0;
    return checkError(::ioctlsocket(m_handle,FIONBIO,(unsigned long *) &flags));
#else
    flags = ::fcntl(m_handle,F_GETFL);
    if (flags < 0) {
	copyError();
	return false;
    }
    if (block)
	flags &= !O_NONBLOCK;
    else
	flags |= O_NONBLOCK;
    return checkError(::fcntl(m_handle,F_SETFL,flags));
#endif
}

/* vi: set ts=8 sw=4 sts=4 noet: */
