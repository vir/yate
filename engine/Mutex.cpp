/**
 * Mutex.cpp
 * This file is part of the YATE Project http://YATE.null.ro
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

#include "yateclass.h"

#ifdef _WINDOWS

typedef HANDLE HMUTEX;

#else

#include <pthread.h>

#ifdef MUTEX_HACK
extern "C" {
#if defined(__FreeBSD__)
extern int pthread_mutexattr_settype(pthread_mutexattr_t *__attr, int __kind);
#define PTHREAD_MUTEX_RECURSIVE_NP PTHREAD_MUTEX_RECURSIVE
#else
extern int pthread_mutexattr_settype(pthread_mutexattr_t *__attr, int __kind) __THROW;
#endif
}
#endif

typedef pthread_mutex_t HMUTEX;

#endif /* ! _WINDOWS */

namespace TelEngine {

class MutexPrivate {
public:
    MutexPrivate(bool recursive, const char* name);
    ~MutexPrivate();
    inline void ref()
	{ ++m_refcount; }
    inline void deref()
	{ if (!--m_refcount) delete this; }
    inline bool recursive() const
	{ return m_recursive; }
    inline const char* name() const
	{ return m_name; }
    bool locked() const
    	{ return (m_locked > 0); }
    bool lock(long maxwait);
    void unlock();
    static volatile int s_count;
    static volatile int s_locks;
private:
    HMUTEX m_mutex;
    int m_refcount;
    volatile unsigned int m_locked;
    bool m_recursive;
    const char* m_name;
    const char* m_owner;
};

class GlobalMutex {
public:
    GlobalMutex();
    static void init();
    static void lock();
    static void unlock();
private:
    static bool s_init;
    static HMUTEX s_mutex;
};

};

using namespace TelEngine;

static GlobalMutex s_global;
static unsigned long s_maxwait = 0;

volatile int MutexPrivate::s_count = 0;
volatile int MutexPrivate::s_locks = 0;
bool GlobalMutex::s_init = true;
HMUTEX GlobalMutex::s_mutex;

// WARNING!!!
// No debug messages are allowed in mutexes since the debug output itself
// is serialized using a mutex!

void GlobalMutex::init()
{
    if (s_init) {
	s_init = false;
#ifdef _WINDOWS
	s_mutex = ::CreateMutex(NULL,FALSE,NULL);
#else
	pthread_mutexattr_t attr;
	::pthread_mutexattr_init(&attr);
	::pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE_NP);
	::pthread_mutex_init(&s_mutex,&attr);
	::pthread_mutexattr_destroy(&attr);
#endif
    }
}

GlobalMutex::GlobalMutex()
{
    init();
}

void GlobalMutex::lock()
{
    init();
#ifdef _WINDOWS
    ::WaitForSingleObject(s_mutex,INFINITE);
#else
    ::pthread_mutex_lock(&s_mutex);
#endif
}

void GlobalMutex::unlock()
{
    init();
#ifdef _WINDOWS
    ::ReleaseMutex(s_mutex);
#else
    ::pthread_mutex_unlock(&s_mutex);
#endif
}


MutexPrivate::MutexPrivate(bool recursive, const char* name)
    : m_refcount(1), m_locked(0), m_recursive(recursive),
      m_name(name), m_owner(0)
{
    GlobalMutex::lock();
    s_count++;
#ifdef _WINDOWS
    // All mutexes are recursive in Windows
    m_mutex = ::CreateMutex(NULL,FALSE,NULL);
#else
    if (recursive) {
	pthread_mutexattr_t attr;
	::pthread_mutexattr_init(&attr);
	::pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE_NP);
	::pthread_mutex_init(&m_mutex,&attr);
	::pthread_mutexattr_destroy(&attr);
    }
    else
	::pthread_mutex_init(&m_mutex,0);
#endif
    GlobalMutex::unlock();
}

MutexPrivate::~MutexPrivate()
{
    bool warn = false;
    GlobalMutex::lock();
    if (m_locked) {
	warn = true;
	m_locked--;
	s_locks--;
#ifdef _WINDOWS
	::ReleaseMutex(m_mutex);
#else
	::pthread_mutex_unlock(&m_mutex);
#endif
    }
    s_count--;
#ifdef _WINDOWS
    ::CloseHandle(m_mutex);
    m_mutex = 0;
#else
    ::pthread_mutex_destroy(&m_mutex);
#endif
    GlobalMutex::unlock();
    if (m_locked)
	Debug(DebugFail,"MutexPrivate '%s' owned by '%s' destroyed with %u locks [%p]",
	    m_name,m_owner,m_locked,this);
    else if (warn)
	Debug(DebugGoOn,"MutexPrivate '%s' owned by '%s' unlocked in destructor [%p]",
	    m_name,m_owner,this);
}

bool MutexPrivate::lock(long maxwait)
{
    bool rval = false;
    bool warn = false;
    bool dead = false;
    if (s_maxwait && (maxwait < 0)) {
	maxwait = (long)s_maxwait;
	warn = true;
    }
    GlobalMutex::lock();
    ref();
    Thread* thr = Thread::current();
    if (thr)
	thr->m_locking = true;
    GlobalMutex::unlock();
#ifdef _WINDOWS
    DWORD ms = 0;
    if (maxwait < 0)
	ms = INFINITE;
    else if (maxwait > 0) {
	ms = (DWORD)(maxwait / 1000);
    }
    rval = (::WaitForSingleObject(m_mutex,ms) == WAIT_OBJECT_0);
#else
    if (maxwait < 0)
	rval = !::pthread_mutex_lock(&m_mutex);
    else if (!maxwait)
	rval = !::pthread_mutex_trylock(&m_mutex);
    else {
	u_int64_t t = Time::now() + maxwait;
	do {
	    if (!dead) {
		dead = Thread::check(false);
		// give up only if caller asked for a limited wait
		if (dead && !warn)
		    break;
	    }
	    rval = !::pthread_mutex_trylock(&m_mutex);
	    if (rval)
		break;
	    Thread::yield();
	} while (t > Time::now());
    }
#endif
    GlobalMutex::lock();
    if (thr)
	thr->m_locking = false;
    if (rval) {
	s_locks++;
	m_locked++;
	if (thr) {
	    thr->m_locks++;
	    m_owner = thr->name();
	}
	else
	    m_owner = 0;
    }
    else
	deref();
    GlobalMutex::unlock();
    if (warn && !rval)
	Debug(DebugFail,"Thread '%s' could not lock mutex '%s' owned by '%s' for %lu usec!",
	    Thread::currentName(),m_name,m_owner,maxwait);
    return rval;
}

void MutexPrivate::unlock()
{
    // Hope we don't hit a bug related to the debug mutex!
    GlobalMutex::lock();
    if (m_locked) {
	Thread* thr = Thread::current();
	if (thr)
	    thr->m_locks--;
	if (!--m_locked) {
	    const char* tname = thr ? thr->name() : 0;
	    if (tname != m_owner) 
		Debug(DebugFail,"MutexPrivate '%s' unlocked by '%s' but owned by '%s' [%p]",
		    m_name,tname,m_owner,this);
	    m_owner = 0;
	}
	int locks = --s_locks;
	if (locks < 0) {
	    // this is very very bad - abort right now
	    abortOnBug(true);
	    s_locks = 0;
	    Debug(DebugFail,"MutexPrivate::locks() is %d [%p]",locks,this);
	}
#ifdef _WINDOWS
	::ReleaseMutex(m_mutex);
#else
	::pthread_mutex_unlock(&m_mutex);
#endif
	deref();
    }
    else
	Debug(DebugFail,"MutexPrivate::unlock called on unlocked '%s' [%p]",m_name,this);
    GlobalMutex::unlock();
}


Mutex::Mutex(bool recursive, const char* name)
    : m_private(0)
{
    if (!name)
	name = "?";
    m_private = new MutexPrivate(recursive,name);
}

Mutex::Mutex(const Mutex &original)
    : m_private(original.privDataCopy())
{
}

Mutex::~Mutex()
{
    MutexPrivate *priv = m_private;
    m_private = 0;
    if (priv)
	priv->deref();
}

Mutex& Mutex::operator=(const Mutex& original)
{
    MutexPrivate *priv = m_private;
    m_private = original.privDataCopy();
    if (priv)
	priv->deref();
    return *this;
}

MutexPrivate *Mutex::privDataCopy() const
{
    if (m_private)
	m_private->ref();
    return m_private;
}

bool Mutex::lock(long maxwait)
{
    return m_private ? m_private->lock(maxwait) : false;
}

void Mutex::unlock()
{
    if (m_private)
	m_private->unlock();
}

bool Mutex::check(long maxwait)
{
    bool ret = lock(maxwait);
    if (ret)
	unlock();
    return ret;
}

bool Mutex::recursive() const
{
    return m_private && m_private->recursive();
}

bool Mutex::locked() const
{
    return m_private && m_private->locked();
}

int Mutex::count()
{
    return MutexPrivate::s_count;
}

int Mutex::locks()
{
    return MutexPrivate::s_locks;
}

void Mutex::wait(unsigned long maxwait)
{
    s_maxwait = maxwait;
}


bool Lock2::lock(Mutex* mx1, Mutex* mx2, long maxwait)
{
    // if we got only one mutex it must be mx1
    if (!mx1) {
	mx1 = mx2;
	mx2 = 0;
    }
    // enforce a fixed locking order - lowest address first
    else if (mx1 && mx2 && (mx1 > mx2)) {
	Mutex* tmp = mx1;
	mx1 = mx2;
	mx2 = tmp;
    }
    drop();
    if (!mx1)
	return false;
    if (!mx1->lock(maxwait))
	return false;
    if (mx2) {
	if (!mx2->lock(maxwait)) {
	    mx1->unlock();
	    return false;
	}
    }
    m_mx1 = mx1;
    m_mx2 = mx2;
    return true;
}

void Lock2::drop()
{
    Mutex* mx1 = m_mx1;
    Mutex* mx2 = m_mx2;
    m_mx1 = m_mx2 = 0;
    // unlock in reverse order for performance reason
    if (mx2)
	mx2->unlock();
    if (mx1)
	mx1->unlock();
}

/* vi: set ts=8 sw=4 sts=4 noet: */
