/**
 * Mutex.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 */

#include "telengine.h"

#include <unistd.h>
#include <pthread.h>

namespace TelEngine {

class MutexPrivate {
public:
    MutexPrivate();
    ~MutexPrivate();
    inline void ref()
	{ ++m_refcount; }
    inline void deref()
	{ if (!--m_refcount) delete this; }
    bool lock(long long int maxwait);
    void unlock();
    static int m_count;
    static int m_locks;
private:
    pthread_mutex_t m_mutex;
    int m_refcount;
};

};

using namespace TelEngine;

int MutexPrivate::m_count = 0;
int MutexPrivate::m_locks = 0;

// WARNING!!!
// No debug messages are allowed in mutexes since the debug output itself
// is serialized using a mutex!

MutexPrivate::MutexPrivate()
    : m_refcount(1)
{
    m_count++;
    ::pthread_mutex_init(&m_mutex,0);
}

MutexPrivate::~MutexPrivate()
{
    m_count--;
    ::pthread_mutex_destroy(&m_mutex);
}

bool MutexPrivate::lock(long long int maxwait)
{
    bool rval = false;
    ref();
    if (maxwait < 0)
	rval = !::pthread_mutex_lock(&m_mutex);
    else if (!maxwait)
	rval = !::pthread_mutex_trylock(&m_mutex);
    else {
	unsigned long long t = Time::now() + maxwait;
	do {
	    rval = !::pthread_mutex_trylock(&m_mutex);
	    if (rval)
		break;
	    ::usleep(1);
	} while (t > Time::now());
    }
    if (rval)
	m_locks++;
    else
	deref();
    return rval;
}

void MutexPrivate::unlock()
{
    ::pthread_mutex_unlock(&m_mutex);
    m_locks--;
    deref();
}

Mutex::Mutex()
    : m_private(0)
{
    m_private = new MutexPrivate;
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

Mutex& Mutex::operator=(const Mutex &original)
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

bool Mutex::lock(long long int maxwait)
{
    return m_private ? m_private->lock(maxwait) : false;
}

void Mutex::unlock()
{
    if (m_private)
	m_private->unlock();
}

int Mutex::count()
{
    return MutexPrivate::m_count;
}

int Mutex::locks()
{
    return MutexPrivate::m_locks;
}
