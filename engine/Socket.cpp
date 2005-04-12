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

#ifndef _WINDOWS
#include <fcntl.h>
#endif

using namespace TelEngine;

Socket::Socket()
    : m_error(), m_handle(invalidHandle())
{
}

Socket::Socket(SOCKET handle)
    : m_error(), m_handle(handle)
{
}

Socket::Socket(int domain, int type, int protocol)
    : m_error(), m_handle(invalidHandle())
{
    m_handle = ::socket(domain,type,protocol);
    if (!valid())
	copyError();
}

Socket::~Socket()
{
    close();
}

bool Socket::create(int domain, int type, int protocol)
{
    close();
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

bool Socket::close()
{
    bool ret = true;
    SOCKET tmp = m_handle;
    if (tmp != invalidHandle()) {
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

SOCKET Socket::detach()
{
    SOCKET tmp = m_handle;
    m_handle = invalidHandle();
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

void Socket::copyError()
{
#ifdef _WINDOWS
    m_error = WSAGetLastError();
#else
    m_error = errno;
#endif
}

bool Socket::checkError(int retcode)
{
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
#ifdef _WINDOWS
    return (m_error == WSAEWOULDBLOCK);
#else
    return (m_error == EAGAIN) || (m_error == EINTR);
#endif
}

bool Socket::listen(unsigned int backlog)
{
    if ((backlog == 0) || (backlog > SOMAXCONN))
	backlog = SOMAXCONN;
    return checkError(::listen(m_handle,backlog));
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
