/**
 * Thread.cpp
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

#include "yatengine.h"

#include <unistd.h>
#include <pthread.h>
#include <errno.h>

#ifndef PTHREAD_STACK_MIN
#define PTHREAD_STACK_MIN 16384
#endif

namespace TelEngine {

class ThreadPrivate : public GenObject {
    friend class Thread;
public:
    ThreadPrivate(Thread *t,const char *name);
    ~ThreadPrivate();
    void run();
    bool cancel();
    void cleanup();
    void destroy();
    void pubdestroy();
    static ThreadPrivate *create(Thread *t,const char *name);
    static void killall();
    static Thread *current();
    Thread *m_thread;
    pthread_t thread;
    bool m_running;
    bool m_started;
    bool m_updest;
    const char *m_name;
private:
    static void *startFunc(void *arg);
    static void cleanupFunc(void *arg);
    static void destroyFunc(void *arg);
    static void keyAllocFunc();
};

};

using namespace TelEngine;

static pthread_key_t current_key;
static pthread_once_t current_key_once = PTHREAD_ONCE_INIT;
ObjList threads;
Mutex tmutex;

ThreadPrivate *ThreadPrivate::create(Thread *t,const char *name)
{
    ThreadPrivate *p = new ThreadPrivate(t,name);
    int e = 0;
    // Set a decent (256K) stack size that won't eat all virtual memory
    pthread_attr_t attr;
    ::pthread_attr_init(&attr);
    ::pthread_attr_setstacksize(&attr, 16*PTHREAD_STACK_MIN);

    for (int i=0; i<5; i++) {
	e = ::pthread_create(&p->thread,&attr,startFunc,p);
	if (e != EAGAIN)
	    break;
	::usleep(20);
    }
    ::pthread_attr_destroy(&attr);
    if (e) {
	Debug(DebugFail,"Error %d while creating pthread in '%s' [%p]",e,name,p);
	p->m_thread = 0;
	p->destroy();
	return 0;
    }
    p->m_running = true;
    return p;
}

ThreadPrivate::ThreadPrivate(Thread *t,const char *name)
    : m_thread(t), m_running(false), m_started(false), m_updest(true), m_name(name)
{
#ifdef DEBUG
    Debugger debug("ThreadPrivate::ThreadPrivate","(%p,\"%s\") [%p]",t,name,this);
#endif
    Lock lock(tmutex);
    threads.append(this);
}

ThreadPrivate::~ThreadPrivate()
{
#ifdef DEBUG
    Debugger debug("ThreadPrivate::~ThreadPrivate()"," %p '%s' [%p]",m_thread,m_name,this);
#endif
    m_running = false;
    tmutex.lock();
    threads.remove(this,false);
    if (m_thread && m_updest) {
	Thread *t = m_thread;
	m_thread = 0;
	delete t;
    }
    tmutex.unlock();
}

void ThreadPrivate::destroy()
{
    DDebug(DebugAll,"ThreadPrivate::destroy() '%s' [%p]",m_name,this);
    cleanup();
    delete this;
}

void ThreadPrivate::pubdestroy()
{
#ifdef DEBUG
    Debugger debug(DebugAll,"ThreadPrivate::pubdestroy()"," %p '%s' [%p]",m_thread,m_name,this);
#endif
    m_updest = false;
    cleanup();
    m_thread = 0;
    if (!cancel())
	Debug(DebugWarn,"ThreadPrivate::pubdestroy() %p '%s' failed cancel [%p]",m_thread,m_name,this);
}

void ThreadPrivate::run()
{
    DDebug(DebugAll,"ThreadPrivate::run() '%s' [%p]",m_name,this);
    ::pthread_once(&current_key_once,keyAllocFunc);
    ::pthread_setspecific(current_key,this);
    pthread_cleanup_push(cleanupFunc,this);
    ::pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,0);
    ::pthread_detach(::pthread_self());
    
    while (!m_started)
	::usleep(10);
    m_thread->run();
    pthread_cleanup_pop(1);
}

bool ThreadPrivate::cancel()
{
    DDebug(DebugAll,"ThreadPrivate::cancel() '%s' [%p]",m_name,this);
    bool ret = true;
    if (m_running) {
	ret = !::pthread_cancel(thread);
	if (ret) {
	    m_running = false;
	    ::usleep(10);
	}
    }
    return ret;
}

void ThreadPrivate::cleanup()
{
    DDebug(DebugAll,"ThreadPrivate::cleanup() %p '%s' [%p]",m_thread,m_name,this);
    if (m_thread && m_thread->m_private) {
	if (m_thread->m_private == this) {
	    m_thread->m_private = 0;
	    m_thread->cleanup();
	}
	else {
	    Debug(DebugWarn,"ThreadPrivate::cleanup() %p '%s' mismatching %p [%p]",m_thread,m_name,m_thread->m_private,this);
	    m_thread = 0;
	}
    }
}

Thread *ThreadPrivate::current()
{
    ThreadPrivate *t = reinterpret_cast<ThreadPrivate *>(::pthread_getspecific(current_key));
    return t ? t->m_thread : 0;
}

void ThreadPrivate::killall()
{
    Debugger debug("ThreadPrivate::killall()");
    ThreadPrivate *t;
    bool sledgehammer = false;
    int c = 1;
    tmutex.lock();
    ObjList *l = &threads;
    while (l && (t = static_cast<ThreadPrivate *>(l->get())) != 0)
    {
	Debug(DebugInfo,"Trying to kill ThreadPrivate '%s' [%p], attempt %d",t->m_name,t,c);
	tmutex.unlock();
	bool ok = t->cancel();
	if (ok) {
	    // delay a little so threads have a chance to clean up
	    for (int i=0; i<1000; i++) {
		tmutex.lock();
		bool done = (t != l->get());
		tmutex.unlock();
		if (done)
		    break;
		::usleep(10);
	    }
	}
	tmutex.lock();
	if (t != l->get())
	    c = 1;
	else {
	    if (ok) {
		Debug(DebugGoOn,"Could not kill %p but seems OK to delete it (pthread bug?)",t);
		tmutex.unlock();
		t->destroy();
		tmutex.lock();
		continue;
	    }
	    ::usleep(10);
	    if (++c >= 10) {
		Debug(DebugGoOn,"Could not kill %p, will use sledgehammer later.",t);
		sledgehammer = true;
		t->m_thread = 0;
		l = l->next();
		c = 1;
	    }
	}
    }
    tmutex.unlock();
    // last solution - a REALLY BIG tool!
    // usually too big since many libraries have threads of their own...
    if (sledgehammer) {
#ifdef THREAD_KILL
	Debug(DebugGoOn,"Brutally killing remaining threads!");
	::pthread_kill_other_threads_np();
#else
	Debug(DebugGoOn,"Aargh! I cannot kill remaining threads on this platform!");
#endif
    }
}

void ThreadPrivate::destroyFunc(void *arg)
{
#ifdef DEBUG
    Debugger debug("ThreadPrivate::destroyFunc","(%p)",arg);
#endif
    ThreadPrivate *t = reinterpret_cast<ThreadPrivate *>(arg);
    if (t)
	t->destroy();
}

void ThreadPrivate::cleanupFunc(void *arg)
{
    DDebug(DebugAll,"ThreadPrivate::cleanupFunc(%p)",arg);
    ThreadPrivate *t = reinterpret_cast<ThreadPrivate *>(arg);
    if (t)
	t->cleanup();
}

void ThreadPrivate::keyAllocFunc()
{
    DDebug(DebugAll,"ThreadPrivate::keyAllocFunc()");
    if (::pthread_key_create(&current_key,destroyFunc))
	Debug(DebugGoOn,"Failed to create current thread key!");
}

void *ThreadPrivate::startFunc(void *arg)
{
    DDebug(DebugAll,"ThreadPrivate::startFunc(%p)",arg);
    ThreadPrivate *t = reinterpret_cast<ThreadPrivate *>(arg);
    t->run();
    return 0;
}

Thread::Thread(const char *name)
    : m_private(0)
{
#ifdef DEBUG
    Debugger debug("Thread::Thread","(\"%s\") [%p]",name,this);
#endif
    m_private = ThreadPrivate::create(this,name);
}

Thread::~Thread()
{
    DDebug(DebugAll,"Thread::~Thread() [%p]",this);
    if (m_private)
	m_private->pubdestroy();
}

bool Thread::error() const
{
    return !m_private;
}

bool Thread::running() const
{
    return m_private ? m_private->m_started : false;
}

bool Thread::startup()
{
    if (!m_private)
	return false;
    m_private->m_started = true;
    return true;
}

Thread *Thread::current()
{
    return ThreadPrivate::current();
}

int Thread::count()
{
    Lock lock(tmutex);
    return threads.count();
}

void Thread::cleanup()
{
    DDebug(DebugAll,"Thread::cleanup() [%p]",this);
}

void Thread::killall()
{
    if (!ThreadPrivate::current())
	ThreadPrivate::killall();
}

void Thread::exit()
{
    DDebug(DebugAll,"Thread::exit()");
    ::pthread_exit(0);
}

void Thread::cancel()
{
    DDebug(DebugAll,"Thread::cancel() [%p]",this);
    if (m_private)
	m_private->cancel();
}

void Thread::yield()
{
    ::usleep(1);
}

void Thread::preExec()
{
#ifdef THREAD_KILL
    ::pthread_kill_other_threads_np();
#endif
}
/* vi: set ts=8 sw=4 sts=4 noet: */
