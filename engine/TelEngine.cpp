/**
 * TelEngine.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 */

#include "telengine.h"

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>

namespace TelEngine {

#define DebugMin DebugFail
#define DebugMax DebugAll

static int s_debug = DebugWarn;
static int s_indent = 0;
static bool s_debugging = true;
static bool s_abort = false;

static void dbg_stderr_func(const char *buf)
{
    ::fwrite(buf,1,::strlen(buf),stderr);
    ::fflush(stderr);
}

static void (*s_output)(const char *) = dbg_stderr_func;
static void (*s_intout)(const char *) = 0;

static Mutex out_mux;

static void common_output(char *buf)
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

static void dbg_output(const char *prefix, const char *format, va_list ap)
{
    if (!(s_output || s_intout))
	return;
    char buf[1024];
    unsigned int n = s_indent*2;
    if (n >= sizeof(buf))
	n = sizeof(buf)-1;
    ::memset(buf,' ',n);
    buf[n] = 0;
    unsigned int l = sizeof(buf)-n-2;
    if (prefix)
	::strncpy(buf+n,prefix,l);
    n = ::strlen(buf);
    l = sizeof(buf)-n-2;
    if (format) {
	::vsnprintf(buf+n,l,format,ap);
    }
    common_output(buf);
}

void Output(const char *format, ...)
{
    char buf[1024];
    if (!((s_output || s_intout) && format && *format))
	return;
    va_list va;
    va_start(va,format);
    ::vsnprintf(buf,sizeof(buf)-2,format,va);
    va_end(va);
    common_output(buf);
}

bool Debug(int level, const char *format, ...)
{
    if (level <= s_debug) {
	if (!s_debugging)
	    return true;
	if (!format)
	    format = "";
	char buf[32];
	::sprintf(buf,"<%d> ",level);
	va_list va;
	va_start(va,format);
	dbg_output(buf,format,va);
	va_end(va);
	if (s_abort && (level == DebugFail))
	    abort();
	return true;
    }
    return false;
}

bool Debug(const char *facility, int level, const char *format, ...)
{
    if (level <= s_debug) {
	if (!s_debugging)
	    return true;
	if (!format)
	    format = "";
	char buf[64];
	::snprintf(buf,sizeof(buf),"<%s:%d> ",facility,level);
	va_list va;
	va_start(va,format);
	dbg_output(buf,format,va);
	va_end(va);
	if (s_abort && (level == DebugFail))
	    abort();
	return true;
    }
    return false;
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

Debugger::Debugger(const char *name, const char *format, ...)
    : m_name(name)
{
    if (s_debugging && m_name && (s_debug >= DebugAll)) {
	char buf[64];
	::snprintf(buf,sizeof(buf),">>> %s",m_name);
	va_list va;
	va_start(va,format);
	dbg_output(buf,format,va);
	va_end(va);
	s_indent++;
    }
    else
	m_name = 0;
}

Debugger::Debugger(int level, const char *name, const char *format, ...)
    : m_name(name)
{
    if (s_debugging && m_name && (s_debug >= level)) {
	char buf[64];
	::snprintf(buf,sizeof(buf),">>> %s",m_name);
	va_list va;
	va_start(va,format);
	dbg_output(buf,format,va);
	va_end(va);
	s_indent++;
    }
    else
	m_name = 0;
}

Debugger::~Debugger()
{
    if (m_name) {
	s_indent--;
	if (s_debugging) {
	    char buf[64];
	    ::snprintf(buf,sizeof(buf),"<<< %s",m_name);
	    char *format = 0;
	    va_list va = 0;
	    dbg_output(buf,format,va);
	}
    }
}

void Debugger::setOutput(void (*outFunc)(const char *))
{
    s_output = outFunc ? outFunc : dbg_stderr_func;
}

void Debugger::setIntOut(void (*outFunc)(const char *))
{
    s_intout = outFunc;
}

void Debugger::enableOutput(bool enable)
{
    s_debugging = enable;
}

unsigned long long Time::now()
{
    struct timeval tv;
    return ::gettimeofday(&tv,0) ? 0 : fromTimeval(&tv);
}

unsigned long long Time::fromTimeval(struct timeval *tv)
{
    unsigned long long rval = 0;
    if (tv) {
	// Please keep it this way or the compiler may b0rk
	rval = tv->tv_sec;
	rval *= 1000000;
	rval += tv->tv_usec;
    }
    return rval;
}

void Time::toTimeval(struct timeval *tv, unsigned long long usec)
{
    if (tv) {
	tv->tv_usec = usec % 1000000;
	tv->tv_sec = usec / 1000000;
    }
}

RefObject::~RefObject()
{
#ifndef NDEBUG
    if (m_refcount)
	Debug(DebugMild,"RefObject [%p] destroyed with count=%d",this,m_refcount);
#endif
}

};
