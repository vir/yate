/**
 * TelEngine.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004, 2005 Null Team
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


#ifdef _WINDOWS
extern "C" {

static u_int32_t s_randSeed = (u_int32_t)(TelEngine::Time::now() & 0xffffffff);

long int random()
{ return (s_randSeed = (s_randSeed + 1) * 0x8088405) % RAND_MAX; }

void srandom(unsigned int seed)
{ s_randSeed = seed % RAND_MAX; }

}
#endif


namespace TelEngine {

#define DebugMin DebugFail
#define DebugMax DebugAll

#define OUT_BUFFER_SIZE 8192

static int s_debug = DebugWarn;
static int s_indent = 0;
static bool s_debugging = true;
static bool s_abort = false;
static u_int64_t s_timestamp = 0;

static const char* const s_colors[11] = {
    "\033[5;41;1;33m\033[K",// DebugFail - blinking yellow on red
    "\033[41;1;33m\033[K",  // Unnamed   - yellow on red
    "\033[41;1;37m\033[K",  // DebugGoOn - white on red
    "\033[41;37m\033[K",    // Unnamed   - gray on red
    "\033[41;37m\033[K",    // Unnamed   - gray on red
    "\033[40;1;31m\033[K",  // DebugWarn - light red on black
    "\033[40;1;33m\033[K",  // DebugMild - yellow on black
    "\033[40;1;37m\033[K",  // DebugCall - white on black
    "\033[40;1;32m\033[K",  // DebugNote - light green on black
    "\033[40;1;36m\033[K",  // DebugInfo - light cyan on black
    "\033[40;36m\033[K"     // DebugAll  - cyan on black
};

static const char* const s_levels[11] = {
    "FAIL",
    "FAIL",
    "GOON",
    "GOON",
    "GOON",
    "WARN",
    "MILD",
    "CALL",
    "NOTE",
    "INFO",
    "ALL",
};

static const char* dbg_level(int level)
{
    if (level < DebugMin)
	level = DebugMin;
    if (level > DebugMax)
	level = DebugMax;
    return s_levels[level];
}

static void dbg_stderr_func(const char* buf, int level)
{
    ::write(2,buf,::strlen(buf));
}

static void dbg_colorize_func(const char* buf, int level)
{
    const char* col = debugColor(level);
    ::write(2,col,::strlen(col));
    ::write(2,buf,::strlen(buf));
    col = debugColor(-2);
    ::write(2,col,::strlen(col));
}

static void (*s_output)(const char*,int) = dbg_stderr_func;
static void (*s_intout)(const char*,int) = 0;

static Mutex out_mux;
static Mutex ind_mux;

static void common_output(int level,char* buf)
{
    if (level < -1)
	level = -1;
    if (level > DebugMax)
	level = DebugMax;
    int n = ::strlen(buf);
    if (n && (buf[n-1] == '\n'))
	    n--;
    buf[n] = '\n';
    buf[n+1] = '\0';
    // serialize the output strings
    out_mux.lock();
    if (s_output)
	s_output(buf,level);
    if (s_intout)
	s_intout(buf,level);
    out_mux.unlock();
}

static void dbg_output(int level,const char* prefix, const char* format, va_list ap)
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
    common_output(level,buf);
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
    common_output(-1,buf);
}

void Debug(int level, const char* format, ...)
{
    if (!s_debugging)
	return;
    if (level > s_debug)
	return;
    if (!format)
	format = "";
    char buf[32];
    ::sprintf(buf,"<%s> ",dbg_level(level));
    va_list va;
    va_start(va,format);
    ind_mux.lock();
    dbg_output(level,buf,format,va);
    ind_mux.unlock();
    va_end(va);
    if (s_abort && (level == DebugFail))
	abort();
}

void Debug(const char* facility, int level, const char* format, ...)
{
    if (!s_debugging)
	return;
    if (level > s_debug)
	return;
    if (!format)
	format = "";
    char buf[64];
    ::snprintf(buf,sizeof(buf),"<%s:%s> ",facility,dbg_level(level));
    va_list va;
    va_start(va,format);
    ind_mux.lock();
    dbg_output(level,buf,format,va);
    ind_mux.unlock();
    va_end(va);
    if (s_abort && (level == DebugFail))
	abort();
}

void Debug(const DebugEnabler* local, int level, const char* format, ...)
{
    if (!s_debugging)
	return;
    const char* facility = 0;
    if (!local) {
	if (level > s_debug)
	    return;
    }
    else {
	if (!local->debugAt(level))
	    return;
	facility = local->debugName();
    }
    if (!format)
	format = "";
    char buf[64];
    if (facility)
	::snprintf(buf,sizeof(buf),"<%s:%s> ",facility,dbg_level(level));
    else
	::sprintf(buf,"<%s> ",dbg_level(level));
    va_list va;
    va_start(va,format);
    ind_mux.lock();
    dbg_output(level,buf,format,va);
    ind_mux.unlock();
    va_end(va);
    if (s_abort && (level == DebugFail))
	abort();
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

const char* debugColor(int level)
{
    if (level == -2)
	return "\033[0m\033[K"; // reset to defaults
    if ((level < DebugMin) || (level > DebugMax))
	return "\033[0;40;37m\033[K"; // light gray on black
    return s_colors[level];
}

void setDebugTimestamp()
{
    s_timestamp = (Time::now() / 1000000) * 1000000;
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

void DebugEnabler::debugCopy(const DebugEnabler* original)
{
    if (original) {
	m_level = original->debugLevel();
	m_enabled = original->debugEnabled();
    }
    else {
	m_level = TelEngine::debugLevel();
	m_enabled = debugEnabled();
    }
    m_chain = 0;
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
	dbg_output(DebugAll,buf,format,va);
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
	dbg_output(DebugAll,buf,format,va);
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
    dbg_output(DebugAll,buf,fmt,va);
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

void Debugger::setOutput(void (*outFunc)(const char*,int))
{
    out_mux.lock();
    s_output = outFunc ? outFunc : dbg_stderr_func;
    out_mux.unlock();
}

void Debugger::setIntOut(void (*outFunc)(const char*,int))
{
    out_mux.lock();
    s_intout = outFunc;
    out_mux.unlock();
}

void Debugger::enableOutput(bool enable, bool colorize)
{
    s_debugging = enable;
    if (colorize)
	setOutput(dbg_colorize_func);
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

u_int64_t Time::msecNow()
{
    return (u_int64_t)(now() / 1000);
}

u_int32_t Time::secNow()
{
#ifdef _WINDOWS
    return (u_int32_t)(now() / 1000000);
#else
    struct timeval tv;
    return ::gettimeofday(&tv,0) ? 0 : tv.tv_sec;
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


bool GenObject::alive() const
{
    return true;
}

void GenObject::destruct()
{
    delete this;
}

static Mutex s_refmutex;

RefObject::~RefObject()
{
    if (m_refcount > 0)
	Debug(DebugFail,"RefObject [%p] destroyed with count=%d",this,m_refcount);
}

bool RefObject::alive() const
{
    return m_refcount > 0;
}

void RefObject::destruct()
{
    deref();
}

bool RefObject::ref()
{
    s_refmutex.lock();
    bool ret = (m_refcount > 0);
    if (ret)
	++m_refcount;
    s_refmutex.unlock();
    return ret;
}

bool RefObject::deref()
{
    s_refmutex.lock();
    int i = m_refcount;
    if (i > 0)
	--m_refcount;
    s_refmutex.unlock();
    if (i == 1)
	zeroRefs();
    else if (i <= 0)
	Debug(DebugFail,"RefObject::deref() called with count=%d [%p]",i,this);
    return (i <= 1);
}

void RefObject::zeroRefs()
{
    delete this;
}

bool RefObject::resurrect()
{
    s_refmutex.lock();
    bool ret = (0 == m_refcount);
    if (ret)
	m_refcount = 1;
    s_refmutex.unlock();
    return ret;
}


void RefPointerBase::assign(RefObject* oldptr, RefObject* newptr, void* pointer)
{
    if (oldptr == newptr)
	return;
    // Always reference the new object before dereferencing the old one
    //  and also don't keep pointers to objects that fail referencing
    m_pointer = (newptr && newptr->ref()) ? pointer : 0;
    if (oldptr)
	oldptr->deref();
}

};

/* vi: set ts=8 sw=4 sts=4 noet: */
