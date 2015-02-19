/**
 * Mutex.cpp
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

#ifdef _WINDOWS

typedef HANDLE HMUTEX;
typedef HANDLE HSEMAPHORE;

#else

#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#ifdef MUTEX_HACK
extern "C" {
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
extern int pthread_mutexattr_settype(pthread_mutexattr_t *__attr, int __kind);
#define PTHREAD_MUTEX_RECURSIVE_NP PTHREAD_MUTEX_RECURSIVE
#else
extern int pthread_mutexattr_settype(pthread_mutexattr_t *__attr, int __kind) __THROW;
#endif
}
#endif

typedef pthread_mutex_t HMUTEX;
typedef sem_t HSEMAPHORE;

#endif /* ! _WINDOWS */

#ifdef MUTEX_STATIC_UNSAFE
#undef MUTEX_STATIC_UNSAFE
#define MUTEX_STATIC_UNSAFE true
#else
#define MUTEX_STATIC_UNSAFE false
#endif

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
    inline const char* owner() const
	{ return m_owner; }
    bool locked() const
    	{ return (m_locked > 0); }
    bool lock(long maxwait);
    bool unlock();
    static volatile int s_count;
    static volatile int s_locks;
private:
    HMUTEX m_mutex;
    int m_refcount;
    volatile unsigned int m_locked;
    volatile unsigned int m_waiting;
    bool m_recursive;
    const char* m_name;
    const char* m_owner;
};

class SemaphorePrivate {
public:
    SemaphorePrivate(unsigned int maxcount, const char* name, unsigned int initialCount);
    ~SemaphorePrivate();
    inline void ref()
	{ ++m_refcount; }
    inline void deref()
	{ if (!--m_refcount) delete this; }
    inline const char* name() const
	{ return m_name; }
    bool locked() const
    	{ return (m_waiting > 0); }
    bool lock(long maxwait);
    bool unlock();
    static volatile int s_count;
    static volatile int s_locks;
private:
    HSEMAPHORE m_semaphore;
    int m_refcount;
    volatile unsigned int m_waiting;
    unsigned int m_maxcount;
    const char* m_name;
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

HMUTEX GlobalMutex::s_mutex;
static GlobalMutex s_global;
static unsigned long s_maxwait = 0;
static bool s_unsafe = MUTEX_STATIC_UNSAFE;
static bool s_safety = false;

volatile int MutexPrivate::s_count = 0;
volatile int MutexPrivate::s_locks = 0;
volatile int SemaphorePrivate::s_count = 0;
volatile int SemaphorePrivate::s_locks = 0;
bool GlobalMutex::s_init = true;

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
    if (s_unsafe)
	return;
#ifdef _WINDOWS
    ::WaitForSingleObject(s_mutex,INFINITE);
#else
    ::pthread_mutex_lock(&s_mutex);
#endif
}

void GlobalMutex::unlock()
{
    if (s_unsafe)
	return;
#ifdef _WINDOWS
    ::ReleaseMutex(s_mutex);
#else
    ::pthread_mutex_unlock(&s_mutex);
#endif
}


MutexPrivate::MutexPrivate(bool recursive, const char* name)
    : m_refcount(1), m_locked(0), m_waiting(0), m_recursive(recursive),
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
	if (s_safety)
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
    if (m_locked || m_waiting)
	Debug(DebugFail,"MutexPrivate '%s' owned by '%s' destroyed with %u locks, %u waiting [%p]",
	    m_name,m_owner,m_locked,m_waiting,this);
    else if (warn)
	Debug(DebugGoOn,"MutexPrivate '%s' owned by '%s' unlocked in destructor [%p]",
	    m_name,m_owner,this);
}

bool MutexPrivate::lock(long maxwait)
{
    bool rval = false;
    bool warn = false;
    if (s_maxwait && (maxwait < 0)) {
	maxwait = (long)s_maxwait;
	warn = true;
    }
    bool safety = s_safety;
    if (safety)
	GlobalMutex::lock();
    Thread* thr = Thread::current();
    if (thr)
	thr->m_locking = true;
    if (safety) {
	m_waiting++;
	GlobalMutex::unlock();
    }
#ifdef _WINDOWS
    DWORD ms = 0;
    if (maxwait < 0)
	ms = INFINITE;
    else if (maxwait > 0)
	ms = (DWORD)(maxwait / 1000);
    rval = s_unsafe || (::WaitForSingleObject(m_mutex,ms) == WAIT_OBJECT_0);
#else
    if (s_unsafe)
	rval = true;
    else if (maxwait < 0)
	rval = !::pthread_mutex_lock(&m_mutex);
    else if (!maxwait)
	rval = !::pthread_mutex_trylock(&m_mutex);
    else {
	u_int64_t t = Time::now() + maxwait;
#ifdef HAVE_TIMEDLOCK
	struct timeval tv;
	struct timespec ts;
	Time::toTimeval(&tv,t);
	ts.tv_sec = tv.tv_sec;
	ts.tv_nsec = 1000 * tv.tv_usec;
	rval = !::pthread_mutex_timedlock(&m_mutex,&ts);
#else
	bool dead = false;
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
#endif // HAVE_TIMEDLOCK
    }
#endif // _WINDOWS
    if (safety) {
	GlobalMutex::lock();
	m_waiting--;
    }
    if (thr)
	thr->m_locking = false;
    if (rval) {
	if (safety)
	    s_locks++;
	m_locked++;
	if (thr) {
	    thr->m_locks++;
	    m_owner = thr->name();
	}
	else
	    m_owner = 0;
    }
    if (safety)
	GlobalMutex::unlock();
    if (warn && !rval)
	Debug(DebugFail,"Thread '%s' could not lock mutex '%s' owned by '%s' waited by %u others for %lu usec!",
	    Thread::currentName(),m_name,m_owner,m_waiting,maxwait);
    return rval;
}

bool MutexPrivate::unlock()
{
    bool ok = false;
    // Hope we don't hit a bug related to the debug mutex!
    bool safety = s_safety;
    if (safety)
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
	if (safety) {
	    int locks = --s_locks;
	    if (locks < 0) {
		// this is very very bad - abort right now
		abortOnBug(true);
		s_locks = 0;
		Debug(DebugFail,"MutexPrivate::locks() is %d [%p]",locks,this);
	    }
	}
#ifdef _WINDOWS
	ok = s_unsafe || ::ReleaseMutex(m_mutex);
#else
	ok = s_unsafe || !::pthread_mutex_unlock(&m_mutex);
#endif
	if (!ok)
	    Debug(DebugFail,"Failed to unlock mutex '%s' [%p]",m_name,this);
    }
    else
	Debug(DebugFail,"MutexPrivate::unlock called on unlocked '%s' [%p]",m_name,this);
    if (safety)
	GlobalMutex::unlock();
    return ok;
}


SemaphorePrivate::SemaphorePrivate(unsigned int maxcount, const char* name,
    unsigned int initialCount)
    : m_refcount(1), m_waiting(0), m_maxcount(maxcount),
      m_name(name)
{
    if (initialCount > m_maxcount)
	initialCount = m_maxcount;
    GlobalMutex::lock();
    s_count++;
#ifdef _WINDOWS
    m_semaphore = ::CreateSemaphore(NULL,initialCount,maxcount,NULL);
#else
    ::sem_init(&m_semaphore,0,initialCount);
#endif
    GlobalMutex::unlock();
}

SemaphorePrivate::~SemaphorePrivate()
{
    GlobalMutex::lock();
    s_count--;
#ifdef _WINDOWS
    ::CloseHandle(m_semaphore);
    m_semaphore = 0;
#else
    ::sem_destroy(&m_semaphore);
#endif
    GlobalMutex::unlock();
    if (m_waiting)
	Debug(DebugFail,"SemaphorePrivate '%s' destroyed with %u locks [%p]",
	    m_name,m_waiting,this);
}

bool SemaphorePrivate::lock(long maxwait)
{
    bool rval = false;
    bool warn = false;
    if (s_maxwait && (maxwait < 0)) {
	maxwait = (long)s_maxwait;
	warn = true;
    }
    bool safety = s_safety;
    if (safety)
	GlobalMutex::lock();
    Thread* thr = Thread::current();
    if (thr)
	thr->m_locking = true;
    if (safety) {
	s_locks++;
	m_waiting++;
	GlobalMutex::unlock();
    }
#ifdef _WINDOWS
    DWORD ms = 0;
    if (maxwait < 0)
	ms = INFINITE;
    else if (maxwait > 0)
	ms = (DWORD)(maxwait / 1000);
    rval = s_unsafe || (::WaitForSingleObject(m_semaphore,ms) == WAIT_OBJECT_0);
#else
    if (s_unsafe)
	rval = true;
    else if (maxwait < 0)
	rval = !::sem_wait(&m_semaphore);
    else if (!maxwait)
	rval = !::sem_trywait(&m_semaphore);
    else {
	u_int64_t t = Time::now() + maxwait;
#ifdef HAVE_TIMEDWAIT
	struct timeval tv;
	struct timespec ts;
	Time::toTimeval(&tv,t);
	ts.tv_sec = tv.tv_sec;
	ts.tv_nsec = 1000 * tv.tv_usec;
	rval = !::sem_timedwait(&m_semaphore,&ts);
#else
	bool dead = false;
	do {
	    if (!dead) {
		dead = Thread::check(false);
		// give up only if caller asked for a limited wait
		if (dead && !warn)
		    break;
	    }
	    rval = !::sem_trywait(&m_semaphore);
	    if (rval)
		break;
	    Thread::yield();
	} while (t > Time::now());
#endif // HAVE_TIMEDWAIT
    }
#endif // _WINDOWS
    if (safety) {
	GlobalMutex::lock();
	int locks = --s_locks;
	if (locks < 0) {
	    // this is very very bad - abort right now
	    abortOnBug(true);
	    s_locks = 0;
	    Debug(DebugFail,"SemaphorePrivate::locks() is %d [%p]",locks,this);
	}
	m_waiting--;
    }
    if (thr)
	thr->m_locking = false;
    if (safety)
	GlobalMutex::unlock();
    if (warn && !rval)
	Debug(DebugFail,"Thread '%s' could not lock semaphore '%s' waited by %u others for %lu usec!",
	    Thread::currentName(),m_name,m_waiting,maxwait);
    return rval;
}

bool SemaphorePrivate::unlock()
{
    if (!s_unsafe) {
	bool safety = s_safety;
	if (safety)
	    GlobalMutex::lock();
#ifdef _WINDOWS
	::ReleaseSemaphore(m_semaphore,1,NULL);
#else
	int val = 0;
	if (!::sem_getvalue(&m_semaphore,&val) && (val < (int)m_maxcount))
	    ::sem_post(&m_semaphore);
#endif
	if (safety)
	    GlobalMutex::unlock();
    }
    return true;
}


Lockable::~Lockable()
{
}

bool Lockable::check(long maxwait)
{
    bool ret = lock(maxwait);
    if (ret)
	unlock();
    return ret;
}

bool Lockable::unlockAll()
{
    while (locked()) {
	if (!unlock())
	    return false;
	Thread::yield();
    }
    return true;
}

void Lockable::startUsingNow()
{
    s_unsafe = false;
}

void Lockable::enableSafety(bool safe)
{
    s_safety = safe;
}

bool Lockable::safety()
{
    return s_safety;
}

void Lockable::wait(unsigned long maxwait)
{
    s_maxwait = maxwait;
}

unsigned long Lockable::wait()
{
    return s_maxwait;
}


Mutex::Mutex(bool recursive, const char* name)
    : m_private(0)
{
    if (!name)
	name = "?";
    m_private = new MutexPrivate(recursive,name);
}

Mutex::Mutex(const Mutex &original)
    : Lockable(),
      m_private(original.privDataCopy())
{
}

Mutex::~Mutex()
{
    MutexPrivate* priv = m_private;
    m_private = 0;
    if (priv)
	priv->deref();
}

Mutex& Mutex::operator=(const Mutex& original)
{
    MutexPrivate* priv = m_private;
    m_private = original.privDataCopy();
    if (priv)
	priv->deref();
    return *this;
}

MutexPrivate* Mutex::privDataCopy() const
{
    if (m_private)
	m_private->ref();
    return m_private;
}

bool Mutex::lock(long maxwait)
{
    return m_private && m_private->lock(maxwait);
}

bool Mutex::unlock()
{
    return m_private && m_private->unlock();
}

bool Mutex::recursive() const
{
    return m_private && m_private->recursive();
}

bool Mutex::locked() const
{
    return m_private && m_private->locked();
}

const char* Mutex::owner() const
{
    return m_private ? m_private->owner() : static_cast<const char*>(0);
}

int Mutex::count()
{
    return MutexPrivate::s_count;
}

int Mutex::locks()
{
    return s_safety ? MutexPrivate::s_locks : -1;
}

bool Mutex::efficientTimedLock()
{
#if defined(_WINDOWS) || defined(HAVE_TIMEDLOCK)
    return true;
#else
    return false;
#endif
}


MutexPool::MutexPool(unsigned int len, bool recursive, const char* name)
    : m_name(0), m_data(0), m_length(len ? len : 1)
{
    if (TelEngine::null(name))
	name = "Pool";
    m_name = new String[m_length];
    for (unsigned int i = 0; i < m_length; i++)
	m_name[i] << name << "::" << (i + 1);
    m_data = new Mutex*[m_length];
    for (unsigned int i = 0; i < m_length; i++)
	m_data[i] = new Mutex(recursive,m_name[i]);
}

MutexPool::~MutexPool()
{
    if (m_data) {
	for (unsigned int i = 0; i < m_length; i++)
	    delete m_data[i];
	delete[] m_data;
    }
    if (m_name)
	delete[] m_name;
}


Semaphore::Semaphore(unsigned int maxcount, const char* name, unsigned int initialCount)
    : m_private(0)
{
    if (!name)
	name = "?";
    if (maxcount)
	m_private = new SemaphorePrivate(maxcount,name,initialCount);
}

Semaphore::Semaphore(const Semaphore &original)
    : Lockable(),
      m_private(original.privDataCopy())
{
}

Semaphore::~Semaphore()
{
    SemaphorePrivate* priv = m_private;
    m_private = 0;
    if (priv)
	priv->deref();
}

Semaphore& Semaphore::operator=(const Semaphore& original)
{
    SemaphorePrivate* priv = m_private;
    m_private = original.privDataCopy();
    if (priv)
	priv->deref();
    return *this;
}

SemaphorePrivate* Semaphore::privDataCopy() const
{
    if (m_private)
	m_private->ref();
    return m_private;
}

bool Semaphore::lock(long maxwait)
{
    return m_private && m_private->lock(maxwait);
}

bool Semaphore::unlock()
{
    return m_private && m_private->unlock();
}

bool Semaphore::locked() const
{
    return m_private && m_private->locked();
}

int Semaphore::count()
{
    return SemaphorePrivate::s_count;
}

int Semaphore::locks()
{
    return s_safety ? SemaphorePrivate::s_locks : -1;
}

bool Semaphore::efficientTimedLock()
{
#if defined(_WINDOWS) || defined(HAVE_TIMEDWAIT)
    return true;
#else
    return false;
#endif
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
