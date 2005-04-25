/**
 * TelEngine.cpp
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

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

namespace TelEngine {

#define DebugMin DebugFail
#define DebugMax DebugAll

#define OUT_BUFFER_SIZE 2048

static int s_debug = DebugWarn;
static int s_indent = 0;
static bool s_debugging = true;
static bool s_abort = false;
static u_int64_t s_timestamp = 0;

static void dbg_stderr_func(const char* buf)
{
    ::fwrite(buf,1,::strlen(buf),stderr);
    ::fflush(stderr);
}

static void (*s_output)(const char*) = dbg_stderr_func;
static void (*s_intout)(const char*) = 0;

static Mutex out_mux;
static Mutex ind_mux;

static void common_output(char* buf)
{
    int n = ::strlen(buf);
    if (n && (buf[n-1] == '\n'))
	    n--;
    buf[n] = '\n';
    buf[n+1] = '\0';
    // serialize the output strings
    out_mux.lock();
    if (s_output)
	s_output(buf);
    if (s_intout)
	s_intout(buf);
    out_mux.unlock();
}

static void dbg_output(const char* prefix, const char* format, va_list ap)
{
    if (!(s_output || s_intout))
	return;
    char buf[OUT_BUFFER_SIZE];
    unsigned int n = 0;
    if (s_timestamp) {
	u_int64_t t = Time::now() - s_timestamp;
	unsigned int s = (unsigned int)(t / 1000000);
	unsigned int u = (unsigned int)(t % 1000000);
	::sprintf(buf,"%07u.%06u ",s,u);
	n = ::strlen(buf);
    }
    unsigned int l = s_indent*2;
    if (l >= sizeof(buf)-n)
	l = sizeof(buf)-n-1;
    ::memset(buf+n,' ',l);
    n += l;
    buf[n] = 0;
    l = sizeof(buf)-n-2;
    if (prefix)
	::strncpy(buf+n,prefix,l);
    n = ::strlen(buf);
    l = sizeof(buf)-n-2;
    if (format) {
	::vsnprintf(buf+n,l,format,ap);
    }
    common_output(buf);
}

void Output(const char* format, ...)
{
    char buf[OUT_BUFFER_SIZE];
    if (!((s_output || s_intout) && format && *format))
	return;
    va_list va;
    va_start(va,format);
    ::vsnprintf(buf,sizeof(buf)-2,format,va);
    va_end(va);
    common_output(buf);
}

void Debug(int level, const char* format, ...)
{
    if (level <= s_debug) {
	if (!s_debugging)
	    return;
	if (!format)
	    format = "";
	char buf[32];
	::sprintf(buf,"<%d> ",level);
	va_list va;
	va_start(va,format);
	ind_mux.lock();
	dbg_output(buf,format,va);
	ind_mux.unlock();
	va_end(va);
	if (s_abort && (level == DebugFail))
	    abort();
	return;
    }
}

void Debug(const char* facility, int level, const char* format, ...)
{
    if (level <= s_debug) {
	if (!s_debugging)
	    return;
	if (!format)
	    format = "";
	char buf[64];
	::snprintf(buf,sizeof(buf),"<%s:%d> ",facility,level);
	va_list va;
	va_start(va,format);
	ind_mux.lock();
	dbg_output(buf,format,va);
	ind_mux.unlock();
	va_end(va);
	if (s_abort && (level == DebugFail))
	    abort();
	return;
    }
}

void Debug(const DebugEnabler* local, int level, const char* format, ...)
{
    if (local && local->debugAt(level)) {
	if (!s_debugging)
	    return;
	if (!format)
	    format = "";
	char buf[32];
	::sprintf(buf,"<%d> ",level);
	va_list va;
	va_start(va,format);
	ind_mux.lock();
	dbg_output(buf,format,va);
	ind_mux.unlock();
	va_end(va);
	if (s_abort && (level == DebugFail))
	    abort();
	return;
    }
}

void abortOnBug()
{
    if (s_abort)
	abort();
}

bool abortOnBug(bool doAbort)
{
    bool tmp = s_abort;
    s_abort = doAbort;
    return tmp;
}  

int debugLevel()
{
    return s_debug;
}

int debugLevel(int level)
{
    if (level < DebugMin)
	level = DebugMin;
    if (level > DebugMax)
	level = DebugMax;
    return (s_debug = level);
}

bool debugAt(int level)
{
    return (s_debugging && (level <= s_debug));
}

void setDebugTimestamp()
{
    s_timestamp = Time::now();
}

int DebugEnabler::debugLevel(int level)
{
    if (level < DebugMin)
	level = DebugMin;
    if (level > DebugMax)
	level = DebugMax;
    m_chain = 0;
    return (m_level = level);
}

bool DebugEnabler::debugAt(int level) const
{
    if (m_chain)
	return m_chain->debugAt(level);
    return (m_enabled && (level <= m_level));
}

Debugger::Debugger(const char* name, const char* format, ...)
    : m_name(name)
{
    if (s_debugging && m_name && (s_debug >= DebugAll)) {
	char buf[64];
	::snprintf(buf,sizeof(buf),">>> %s",m_name);
	va_list va;
	va_start(va,format);
	ind_mux.lock();
	dbg_output(buf,format,va);
	va_end(va);
	s_indent++;
	ind_mux.unlock();
    }
    else
	m_name = 0;
}

Debugger::Debugger(int level, const char* name, const char* format, ...)
    : m_name(name)
{
    if (s_debugging && m_name && (s_debug >= level)) {
	char buf[64];
	::snprintf(buf,sizeof(buf),">>> %s",m_name);
	va_list va;
	va_start(va,format);
	ind_mux.lock();
	dbg_output(buf,format,va);
	va_end(va);
	s_indent++;
	ind_mux.unlock();
    }
    else
	m_name = 0;
}

static void dbg_dist_helper(const char* buf, const char* fmt, ...)
{
    va_list va;
    va_start(va,fmt);
    dbg_output(buf,fmt,va);
    va_end(va);
}

Debugger::~Debugger()
{
    if (m_name) {
	ind_mux.lock();
	s_indent--;
	if (s_debugging) {
	    char buf[64];
	    ::snprintf(buf,sizeof(buf),"<<< %s",m_name);
	    dbg_dist_helper(buf,0);
	}
	ind_mux.unlock();
    }
}

void Debugger::setOutput(void (*outFunc)(const char*))
{
    out_mux.lock();
    s_output = outFunc ? outFunc : dbg_stderr_func;
    out_mux.unlock();
}

void Debugger::setIntOut(void (*outFunc)(const char*))
{
    out_mux.lock();
    s_intout = outFunc;
    out_mux.unlock();
}

void Debugger::enableOutput(bool enable)
{
    s_debugging = enable;
}

u_int64_t Time::now()
{
#ifdef _WINDOWS

    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    // Convert from FILETIME (100 nsec units since January 1, 1601)
    //  to extended time_t (1 usec units since January 1, 1970)
    u_int64_t rval = ((ULARGE_INTEGER*)&ft)->QuadPart / 10;
    rval -= 11644473600000000;
    return rval;

#else

    struct timeval tv;
    return ::gettimeofday(&tv,0) ? 0 : fromTimeval(&tv);

#endif
}

u_int64_t Time::fromTimeval(struct timeval* tv)
{
    u_int64_t rval = 0;
    if (tv) {
	// Please keep it this way or the compiler may b0rk
	rval = tv->tv_sec;
	rval *= 1000000;
	rval += tv->tv_usec;
    }
    return rval;
}

void Time::toTimeval(struct timeval* tv, u_int64_t usec)
{
    if (tv) {
	tv->tv_usec = (long)(usec % 1000000);
	tv->tv_sec = (long)(usec / 1000000);
    }
}

static Mutex s_refmutex;

RefObject::~RefObject()
{
    if (m_refcount > 0)
	Debug(DebugFail,"RefObject [%p] destroyed with count=%d",this,m_refcount);
}

int RefObject::ref()
{
    s_refmutex.lock();
    int i = ++m_refcount;
    s_refmutex.unlock();
    return i;
}

bool RefObject::deref()
{
    s_refmutex.lock();
    int i = --m_refcount;
    if (i == 0)
	m_refcount = -1;
    s_refmutex.unlock();
    if (i == 0)
	delete this;
    return (i <= 0);
}

void RefPointerBase::assign(RefObject* oldptr, RefObject* newptr, void* pointer)
{
    if (oldptr == newptr)
	return;
    // Always reference the new object before dereferencing the old one
    if (newptr)
	newptr->ref();
    m_pointer = pointer;
    if (oldptr)
	oldptr->deref();
}

};

/* vi: set ts=8 sw=4 sts=4 noet: */
