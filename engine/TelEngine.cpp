/**
 * TelEngine.cpp
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

#include "yateclass.h"

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <time.h>


#ifdef _WINDOWS

#ifndef HAVE_GMTIME_S
#include <errno.h>

static int _gmtime_s(struct tm* _tm, const time_t* time)
{
    static TelEngine::Mutex m(false,"_gmtime_s");
    struct tm* tmp;
    if (!_tm)
	return EINVAL;
    _tm->tm_isdst = _tm->tm_yday = _tm->tm_wday = _tm->tm_year = _tm->tm_mon = _tm->tm_mday =
	_tm->tm_hour = _tm->tm_min = _tm->tm_sec = -1;
    if (!time)
	return EINVAL;
    m.lock();
    tmp = gmtime(time);
    if (!tmp) {
	m.unlock();
	return EINVAL;
    }
    *_tm = *tmp;
    m.unlock();
    return 0;
}

static int _localtime_s(struct tm* _tm, const time_t* time)
{
    static TelEngine::Mutex m(false,"_localtime_s");
    struct tm* tmp;
    if (!_tm)
	return EINVAL;
    _tm->tm_isdst = _tm->tm_yday = _tm->tm_wday = _tm->tm_year = _tm->tm_mon = _tm->tm_mday =
	_tm->tm_hour = _tm->tm_min = _tm->tm_sec = -1;
    if (!time)
	return EINVAL;
    m.lock();
    tmp = localtime(time);
    if (!tmp) {
	m.unlock();
	return EINVAL;
    }
    *_tm = *tmp;
    m.unlock();
    return 0;
}

#endif

#else // !_WINDOWS
#include <sys/resource.h>
#endif

namespace { // anonymous

class ObjCounterList : public TelEngine::ObjList
{
public:
    inline ObjCounterList()
	: m_invalid(false)
	{ }
    ~ObjCounterList()
	{ m_invalid = true; }
    inline bool invalid() const
	{ return m_invalid; }
private:
    bool m_invalid;
};

}; // anonymous namespace

namespace TelEngine {

#define DebugMin DebugFail
#define DebugVis DebugConf
#define DebugMax DebugAll

#define OUT_BUFFER_SIZE 8192

// RefObject mutex pool array size
#ifndef REFOBJECT_MUTEX_COUNT
#define REFOBJECT_MUTEX_COUNT 47
#endif

static int s_debug = DebugWarn;
static int s_indent = 0;
static bool s_debugging = true;
static bool s_abort = false;
static u_int64_t s_startTime = 0;
static u_int64_t s_timestamp = 0;
static Debugger::Formatting s_fmtstamp = Debugger::None;

static const char* const s_colors[11] = {
    "\033[5;41;1;33m\033[K",// DebugFail - blinking yellow on red
    "\033[44;1;37m\033[K",  // DebugTest - white on blue
    "\033[41;1;37m\033[K",  // DebugGoOn - white on red
    "\033[41;37m\033[K",    // DebugConf - gray on red
    "\033[40;31m\033[K",    // DebugStub - red on black
    "\033[40;1;31m\033[K",  // DebugWarn - light red on black
    "\033[40;1;33m\033[K",  // DebugMild - yellow on black
    "\033[40;1;37m\033[K",  // DebugCall - white on black
    "\033[40;1;32m\033[K",  // DebugNote - light green on black
    "\033[40;1;36m\033[K",  // DebugInfo - light cyan on black
    "\033[40;36m\033[K"     // DebugAll  - cyan on black
};

static const char* const s_levels[11] = {
    "FAIL",
    "TEST",
    "GOON",
    "CONF",
    "STUB",
    "WARN",
    "MILD",
    "CALL",
    "NOTE",
    "INFO",
    "ALL",
};

static const char* dbg_level(int& level)
{
    if (level < DebugMin)
	level = DebugMin;
    if (level > DebugMax)
	level = DebugMax;
    return s_levels[level];
}

static void dbg_stderr_func(const char* buf, int level)
{
    YIGNORE(::write(2,buf,::strlen(buf)));
}

static void dbg_colorize_func(const char* buf, int level)
{
    const char* col = debugColor(level);
    YIGNORE(::write(2,col,::strlen(col)));
    YIGNORE(::write(2,buf,::strlen(buf)));
    col = debugColor(-2);
    YIGNORE(::write(2,col,::strlen(col)));
}

static void (*s_output)(const char*,int) = dbg_stderr_func;
static void (*s_intout)(const char*,int) = 0;
static void (*s_alarms)(const char*,int,const char*,const char*) = 0;
static void (*s_relay)(int,const char*,const char*,const char*) = 0;

static Mutex out_mux(false,"DebugOutput");
static Mutex ind_mux(false,"DebugIndent");
static Thread* s_thr = 0;

bool CapturedEvent::s_capturing = false;
ObjList CapturedEvent::s_events;

static bool reentered()
{
    if (!s_thr)
	return false;
    return (Thread::current() == s_thr);
}

static void common_output(int level,char* buf)
{
    if (level < -1)
	level = -1;
    if (level > DebugMax)
	level = DebugMax;
    int n = ::strlen(buf);
    if (n && (buf[n-1] == '\n'))
	n--;
    // serialize the output strings
    out_mux.lock();
    // TODO: detect reentrant calls from foreign threads and main thread
    s_thr = Thread::current();
    if (CapturedEvent::capturing()) {
	buf[n] = '\0';
	bool save = s_debugging;
	s_debugging = false;
	CapturedEvent::append(level,buf);
	s_debugging = save;
    }
    buf[n] = '\n';
    buf[n+1] = '\0';
    if (s_output)
	s_output(buf,level);
    if (s_intout)
	s_intout(buf,level);
    buf[n] = '\0';
    s_thr = 0;
    out_mux.unlock();
}

static void dbg_output(int level,const char* prefix, const char* format, va_list ap,
    const char* alarmComp = 0, const char* alarmInfo = 0)
{
    void (*relay)(int,const char*,const char*,const char*) = s_relay;
    void (*alarms)(const char*,int,const char*,const char*) = s_alarms;
    bool out = (s_output || s_intout || relay) && (prefix || format);
    bool alarm = alarmComp && format && (alarms || relay);
    if (!(out || alarm))
	return;
    char buf[OUT_BUFFER_SIZE];
    unsigned int n = Debugger::formatTime(buf,s_fmtstamp);
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
    char* msg = buf+n;
    if (format) {
	::vsnprintf(msg,l,format,ap);
	buf[OUT_BUFFER_SIZE - 2] = 0;
    }
    if (relay) {
	relay(level,buf,alarmComp,alarmInfo);
	return;
    }
    if (out)
	common_output(level,buf);
    if (alarm)
	alarms(msg,level,alarmComp,alarmInfo);
}

void Output(const char* format, ...)
{
    char buf[OUT_BUFFER_SIZE];
    void (*relay)(int,const char*,const char*,const char*) = s_relay;
    if (!((s_output || s_intout || relay) && format && *format))
	return;
    if (reentered())
	return;
    va_list va;
    va_start(va,format);
    ::vsnprintf(buf,sizeof(buf)-2,format,va);
    va_end(va);
    if (relay)
	relay(-1,buf,0,0);
    else
	common_output(-1,buf);
}

void Debug(int level, const char* format, ...)
{
    if (!s_debugging)
	return;
    if (level > s_debug || level < DebugMin)
	return;
    if (reentered())
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
    if (level > s_debug || level < DebugMin)
	return;
    if (reentered())
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
	if (level > s_debug || level < DebugMin)
	    return;
    }
    else {
	if (!local->debugAt(level))
	    return;
	facility = local->debugName();
    }
    if (reentered())
	return;
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

void Alarm(const char* component, int level, const char* format, ...)
{
    if (!format || level < DebugMin || reentered())
	return;
    if (TelEngine::null(component))
	component = "unknown";
    char buf[64];
    ::snprintf(buf,sizeof(buf),"<%s:%s> ",component,dbg_level(level));
    va_list va;
    va_start(va,format);
    ind_mux.lock();
    dbg_output(level,buf,format,va,component);
    ind_mux.unlock();
    va_end(va);
    if (s_abort && (level == DebugFail))
	abort();
}

void Alarm(const DebugEnabler* component, int level, const char* format, ...)
{
    if (!format || level < DebugMin || reentered())
	return;
    const char* name = (component && !TelEngine::null(component->debugName()))
	? component->debugName() : "unknown";
    char buf[64];
    ::snprintf(buf,sizeof(buf),"<%s:%s> ",name,dbg_level(level));
    va_list va;
    va_start(va,format);
    ind_mux.lock();
    dbg_output(level,buf,format,va,name);
    ind_mux.unlock();
    va_end(va);
    if (s_abort && (level == DebugFail))
	abort();
}

void Alarm(const char* component, const char* info, int level, const char* format, ...)
{
    if (!format || level < DebugMin || reentered())
	return;
    if (TelEngine::null(component))
	component = "unknown";
    char buf[64];
    ::snprintf(buf,sizeof(buf),"<%s:%s> ",component,dbg_level(level));
    va_list va;
    va_start(va,format);
    ind_mux.lock();
    dbg_output(level,buf,format,va,component,info);
    ind_mux.unlock();
    va_end(va);
    if (s_abort && (level == DebugFail))
	abort();
}

void Alarm(const DebugEnabler* component, const char* info, int level, const char* format, ...)
{
    if (!format || level < DebugMin || reentered())
	return;
    const char* name = (component && !TelEngine::null(component->debugName()))
	? component->debugName() : "unknown";
    char buf[64];
    ::snprintf(buf,sizeof(buf),"<%s:%s> ",name,dbg_level(level));
    va_list va;
    va_start(va,format);
    ind_mux.lock();
    dbg_output(level,buf,format,va,name,info);
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
    if (level < DebugVis)
	level = DebugVis;
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

const char* debugLevelName(int level)
{
    return (level < DebugMin) ? "" : dbg_level(level);
}


int DebugEnabler::debugLevel(int level)
{
    if (level < DebugVis)
	level = DebugVis;
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
    : m_name(name), m_level(DebugAll)
{
    if (s_debugging && m_name && (s_debug >= DebugAll) && !reentered()) {
	char buf[64];
	::snprintf(buf,sizeof(buf),">>> %s",m_name);
	va_list va;
	va_start(va,format);
	ind_mux.lock();
	dbg_output(m_level,buf,format,va);
	va_end(va);
	s_indent++;
	ind_mux.unlock();
    }
    else
	m_name = 0;
}

Debugger::Debugger(int level, const char* name, const char* format, ...)
    : m_name(name), m_level(level)
{
    if (s_debugging && m_name && (s_debug >= level) && !reentered()) {
	char buf[64];
	::snprintf(buf,sizeof(buf),">>> %s",m_name);
	va_list va;
	va_start(va,format);
	ind_mux.lock();
	dbg_output(m_level,buf,format,va);
	va_end(va);
	s_indent++;
	ind_mux.unlock();
    }
    else
	m_name = 0;
}

static void dbg_dist_helper(int level, const char* buf, const char* fmt, ...)
{
    va_list va;
    va_start(va,fmt);
    dbg_output(level,buf,fmt,va);
    va_end(va);
}

bool controlReturn(NamedList* params, bool ret, const char* retVal)
{
    if (retVal && params)
	params->setParam("retVal",retVal);
    if (!params || !params->getObject(YATOM("Message")))
	return ret;
    const String* module = params->getParam("module");
    if (TelEngine::null(module) || YSTRING("rmanager") != *module)
	return ret;
    const String s_opStat("operation-status");
    if (!params->getParam(s_opStat))
	params->addParam(s_opStat,String::boolText(ret));
    return ret;
}

Debugger::~Debugger()
{
    if (m_name) {
	ind_mux.lock();
	s_indent--;
	if (s_debugging)
	    dbg_dist_helper(m_level,"<<< ","%s",m_name);
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

void Debugger::setAlarmHook(void (*alarmFunc)(const char*,int,const char*,const char*))
{
    s_alarms = alarmFunc;
}

void Debugger::setRelayHook(void (*relayFunc)(int,const char*,const char*,const char*))
{
    s_relay = relayFunc;
}

void Debugger::enableOutput(bool enable, bool colorize)
{
    s_debugging = enable;
    if (colorize)
	setOutput(dbg_colorize_func);
}

uint32_t Debugger::getStartTimeSec()
{
    return (uint32_t)(s_timestamp / 1000000);
}

Debugger::Formatting Debugger::getFormatting()
{
    return s_fmtstamp;
}

void Debugger::setFormatting(Formatting format, uint32_t startTimeSec)
{
    // start stamp will be rounded to full second
    if (!s_timestamp) {
	uint64_t sec = Time::now() / 1000000;
	if (startTimeSec && startTimeSec < sec)
	    sec = startTimeSec;
	s_timestamp = sec * 1000000;
    }
    s_fmtstamp = format;
}

unsigned int Debugger::formatTime(char* buf, Formatting format)
{
    if (!buf)
	return 0;
    if (None != format) {
	u_int64_t t = Time::now();
	if (Relative == format)
	    t -= s_timestamp;
	unsigned int s = (unsigned int)(t / 1000000);
	unsigned int u = (unsigned int)(t % 1000000);
	switch (format) {
	    case Textual:
	    case TextLocal:
	    case TextSep:
	    case TextLSep:
		{
		    time_t sec = (time_t)s;
		    struct tm tmp;
		    if (TextLocal == format || TextLSep == format)
#ifdef _WINDOWS
			_localtime_s(&tmp,&sec);
#else
			localtime_r(&sec,&tmp);
#endif
		    else
#ifdef _WINDOWS
			_gmtime_s(&tmp,&sec);
#else
			gmtime_r(&sec,&tmp);
#endif
		    if (Textual == format || TextLocal == format)
			::sprintf(buf,"%04d%02d%02d%02d%02d%02d.%06u ",
			    tmp.tm_year+1900,tmp.tm_mon+1,tmp.tm_mday,
			    tmp.tm_hour,tmp.tm_min,tmp.tm_sec,u);
		    else
			::sprintf(buf,"%04d-%02d-%02d_%02d:%02d:%02d.%06u ",
			    tmp.tm_year+1900,tmp.tm_mon+1,tmp.tm_mday,
			    tmp.tm_hour,tmp.tm_min,tmp.tm_sec,u);
		}
		break;
	    default:
		::sprintf(buf,"%07u.%06u ",s,u);
	}
	return ::strlen(buf);
    }
    buf[0] = '\0';
    return 0;
}

void Debugger::relayOutput(int level, char* buffer, const char* component, const char* info)
{
    if (TelEngine::null(buffer))
	return;
    void (*alarms)(const char*,int,const char*,const char*) = s_alarms;
    bool out = s_output || s_intout;
    bool alarm = (level >= 0) && alarms && !TelEngine::null(component);
    if (!(out || alarm))
	return;
    if (reentered())
	return;
    if (out)
	common_output(level,buffer);
    if (alarm) {
	const char* msg = ::strstr(buffer,"> ");
	if (msg && msg != buffer)
	    msg += 2;
	else
	    msg = buffer;
	if (*msg)
	    alarms(msg,level,component,info);
    }
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

u_int64_t Time::fromTimeval(const struct timeval* tv)
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

// Build EPOCH time from date/time components
unsigned int Time::toEpoch(int year, unsigned int month, unsigned int day,
	unsigned int hour, unsigned int minute, unsigned int sec, int offset)
{
    DDebug(DebugAll,"Time::toEpoch(%d,%u,%u,%u,%u,%u,%d)",
	year,month,day,hour,minute,sec,offset);
    if (year < 1970)
	return (unsigned int)-1;
    if (month < 1 || month > 12 || !day)
	return (unsigned int)-1;
    if (hour == 24 && (minute || sec))
	return (unsigned int)-1;
    else if (hour > 23 || minute > 59 || sec > 59)
	return (unsigned int)-1;
    // Check if month and day are correct in the given year
    month--;
    unsigned int m[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (isLeap(year))
	m[1] = 29;
    if (day > m[month])
	return (unsigned int)-1;
    // Count the number of days since EPOCH
    int64_t days = (year - 1970) * 365;
    // Add a day for each leap year from 1970 to 'year' (not including)
    for (int y = 1972; y < year; y += 4) {
	if (isLeap(y))
	    days++;
    }
    // Add days ellapsed in given year
    for (unsigned int i = 0; i < month; i++)
	days += m[i];
    days += day - 1;
    int64_t ret = (days * 24 + hour) * 3600 + minute * 60 + sec + offset;

    // Check for incorrect time or overflow
    if (ret < 0 || ret > (unsigned int)-1)
	return (unsigned int)-1;
    return (unsigned int)ret;
}

// Split a given EPOCH time into its date/time components
bool Time::toDateTime(unsigned int epochTimeSec, int& year, unsigned int& month,
    unsigned int& day, unsigned int& hour, unsigned int& minute, unsigned int& sec,
    unsigned int* wDay)
{
#ifdef _WINDOWS
    FILETIME ft;
    SYSTEMTIME st;
    // 11644473600: the number of seconds from 1601, January 1st (FILETIME)
    //  to EPOCH (1970, January 1st)
    // Remember: FILETIME keeps the number of 100 nsec units
    u_int64_t time = (11644473600 + epochTimeSec) * 10000000;
    ft.dwLowDateTime = (DWORD)time;
    ft.dwHighDateTime = (DWORD)(time >> 32);
    if (!FileTimeToSystemTime(&ft,&st))
	return false;
    year = st.wYear;
    month = st.wMonth;
    day = st.wDay;
    hour = st.wHour;
    minute = st.wMinute;
    sec = st.wSecond;
    if (wDay)
	*wDay = st.wDayOfWeek;
#else
    struct tm t;
    time_t time = (time_t)epochTimeSec;
    if (!gmtime_r(&time,&t))
	return false;
    year = 1900 + t.tm_year;
    month = t.tm_mon + 1;
    day = t.tm_mday;
    hour = t.tm_hour;
    minute = t.tm_min;
    sec = t.tm_sec;
    if (wDay)
	*wDay = t.tm_wday;
#endif
    DDebug(DebugAll,"Time::toDateTime(%u,%d,%u,%u,%u,%u,%u)",
	epochTimeSec,year,month,day,hour,minute,sec);
    return true;
}

int Time::timeZone()
{
#ifdef _WINDOWS
    struct tm t;
    time_t time = (time_t)secNow();
    _localtime_s(&t,&time);
    if (t.tm_isdst)
	return -(_timezone + _dstbias);
    return -_timezone;
#else
#ifdef HAVE_GMTOFF
    struct tm t;
    time_t time = (time_t)secNow();
    if (localtime_r(&time,&t))
	return t.tm_gmtoff;
#endif
#ifdef HAVE_INT_TZ
    return -timezone;
#else
    return 0;
#endif
#endif
}


static Random s_random;
static Mutex s_randomMutex(false,"Random");

u_int32_t Random::next()
{
    return (m_random = (m_random + 1) * 0x8088405);
}

long int Random::random()
{
    s_randomMutex.lock();
    long int ret = s_random.next() % RAND_MAX;
    s_randomMutex.unlock();
    return ret;
}

void Random::srandom(unsigned int seed)
{
    s_randomMutex.lock();
    s_random.set(seed % RAND_MAX);
    s_randomMutex.unlock();
}


bool GenObject::s_counting = false;
static ObjCounterList s_counters;
static Mutex s_countersMutex(false,"Counters");

GenObject::GenObject()
    : m_counter(0)
{
    NamedCounter* counter = Thread::getCurrentObjCounter();
    if (counter && counter->enabled())
	setObjCounter(counter);
}

bool GenObject::alive() const
{
    return true;
}

void GenObject::destruct()
{
    delete this;
}

NamedCounter* GenObject::setObjCounter(NamedCounter* counter)
{
    if (counter == m_counter)
	return counter;
    if (s_counters.invalid())
	return 0;
    Lock mylock(0);
    if (Mutex::count() >= 0)
	mylock.acquire(s_countersMutex);
    NamedCounter* oldCounter = m_counter;
    if (counter != oldCounter) {
	m_counter = counter;
	mylock.drop();
	if (counter)
	    counter->inc();
	if (oldCounter)
	    oldCounter->dec();
    }
    return oldCounter;
}

NamedCounter* GenObject::getObjCounter(const String& name, bool create)
{
    if (name.null() || s_counters.invalid())
	return 0;
    Lock mylock(s_countersMutex);
    NamedCounter* cnt = static_cast<NamedCounter*>(s_counters[name]);
    if (create && !cnt) {
	NamedCounter* saved = Thread::setCurrentObjCounter(0);
	s_counters.append(cnt = new NamedCounter(name));
	Thread::setCurrentObjCounter(saved);
    }
    return cnt;
}

ObjList& GenObject::getObjCounters()
{
    return s_counters;
}


#ifndef ATOMIC_OPS
static MutexPool s_refMutex(REFOBJECT_MUTEX_COUNT,false,"RefObject");
#endif

RefObject::RefObject()
    : m_refcount(1), m_mutex(0)
{
#ifndef ATOMIC_OPS
    m_mutex = s_refMutex.mutex(this);
#endif
}

RefObject::~RefObject()
{
    if (m_refcount > 0)
	Debug(DebugFail,"RefObject [%p] destroyed with count=%d",this,m_refcount);
}

void* RefObject::getObject(const String& name) const
{
    if (name == YATOM("RefObject"))
	return (void*)this;
    return GenObject::getObject(name);
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
#ifdef ATOMIC_OPS
#ifdef _WINDOWS
    if (InterlockedIncrement((LONG*)&m_refcount) > 1)
	return true;
    InterlockedDecrement((LONG*)&m_refcount);
#else
    if (__sync_add_and_fetch(&m_refcount,1) > 1)
	return true;
    __sync_sub_and_fetch(&m_refcount,1);
#endif
#else
    Lock lock(m_mutex);
    if (m_refcount > 0) {
	++m_refcount;
	return true;
    }
#endif
    return false;
}

bool RefObject::deref()
{
#ifdef ATOMIC_OPS
#ifdef _WINDOWS
    int i = InterlockedDecrement((LONG*)&m_refcount) + 1;
    if (i <= 0)
	InterlockedIncrement((LONG*)&m_refcount);
#else
    int i = __sync_fetch_and_sub(&m_refcount,1);
    if (i <= 0)
	__sync_fetch_and_add(&m_refcount,1);
#endif
#else
    m_mutex->lock();
    int i = m_refcount;
    if (i > 0)
	--m_refcount;
    m_mutex->unlock();
#endif
    if (i == 1)
	zeroRefs();
    else if (i <= 0)
	Debug(DebugFail,"RefObject::deref() called with count=%d [%p]",i,this);
    return (i <= 1);
}

void RefObject::zeroRefs()
{
    destroyed();
    delete this;
}

bool RefObject::resurrect()
{
#ifdef ATOMIC_OPS
#ifdef _WINDOWS
    if (InterlockedIncrement((LONG*)&m_refcount) == 1)
	return true;
    InterlockedDecrement((LONG*)&m_refcount);
    return false;
#else
    if (__sync_add_and_fetch(&m_refcount,1) == 1)
	return true;
    __sync_sub_and_fetch(&m_refcount,1);
    return false;
#endif
#else
    m_mutex->lock();
    bool ret = (0 == m_refcount);
    if (ret)
	m_refcount = 1;
    m_mutex->unlock();
    return ret;
#endif
}

void RefObject::destroyed()
{
}

bool RefObject::efficientIncDec()
{
#ifdef ATOMIC_OPS
    return true;
#else
    return false;
#endif
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


NamedCounter::NamedCounter(const String& name)
    : String(name), m_count(0), m_enabled(getObjCounting()), m_mutex(0)
{
#ifndef ATOMIC_OPS
    m_mutex = s_refMutex.mutex(this);
#endif
}

int NamedCounter::inc()
{
#ifdef ATOMIC_OPS
#ifdef _WINDOWS
    return InterlockedIncrement((LONG*)&m_count);
#else
    return __sync_add_and_fetch(&m_count,1);
#endif
#else
    Lock lock(m_mutex);
    return ++m_count;
#endif
}

int NamedCounter::dec()
{
#ifdef ATOMIC_OPS
#ifdef _WINDOWS
    return InterlockedDecrement((LONG*)&m_count);
#else
    return __sync_fetch_and_sub(&m_count,1);
#endif
#else
    Lock lock(m_mutex);
    return --m_count;
#endif
}


void SysUsage::init()
{
    if (!s_startTime)
	s_startTime = Time::now();
}

u_int64_t SysUsage::startTime()
{
    init();
    return s_startTime;
}

u_int64_t SysUsage::usecRunTime(Type type)
{
    switch (type) {
	case WallTime:
	    return Time::now() - startTime();
	case UserTime:
	    {
#ifdef _WINDOWS
		FILETIME dummy,ft;
		if (GetProcessTimes(GetCurrentProcess(),&dummy,&dummy,&dummy,&ft)) {
		    u_int64_t t = ft.dwLowDateTime | (((u_int64_t)ft.dwHighDateTime) << 32);
		    return t / 10;
		}
#else
		struct rusage usage;
		// FIXME: this is broken, may not sum all threads
		if (!::getrusage(RUSAGE_SELF,&usage))
		    return Time::fromTimeval(usage.ru_utime);
#endif
	    }
	    break;
	case KernelTime:
	    {
#ifdef _WINDOWS
		FILETIME dummy,ft;
		if (GetProcessTimes(GetCurrentProcess(),&dummy,&dummy,&ft,&dummy)) {
		    u_int64_t t = ft.dwLowDateTime | (((u_int64_t)ft.dwHighDateTime) << 32);
		    return t / 10;
		}
#else
		struct rusage usage;
		// FIXME: this is broken, may not sum all threads
		if (!::getrusage(RUSAGE_SELF,&usage))
		    return Time::fromTimeval(usage.ru_stime);
#endif
	    }
	    break;
    }
    return 0;
}

u_int64_t SysUsage::msecRunTime(Type type)
{
    return usecRunTime(type) / 1000;
}

u_int32_t SysUsage::secRunTime(Type type)
{
    return (u_int32_t)(usecRunTime(type) / 1000000);
}

double SysUsage::runTime(Type type)
{
#ifdef _WINDOWS
    // VC++ 6 does not implement conversion from UINT64 to double!
    return 0.000001 * (int64_t)usecRunTime(type);
#else
    return 0.000001 * usecRunTime(type);
#endif
}

};

/* vi: set ts=8 sw=4 sts=4 noet: */
