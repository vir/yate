/**
 * Thread.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 */

#include "telengine.h"

#include <unistd.h>
#include <pthread.h>

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
    int e = ::pthread_create(&p->thread,0,startFunc,p);
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
    : m_thread(t), m_running(false), m_updest(true), m_name(name)
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
    Debugger debug("ThreadPrivate::~ThreadPrivate()"," '%s' [%p]",m_name,this);
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
#ifdef DEBUG
    Debug(DebugAll,"ThreadPrivate::destroy() '%s' [%p]",m_name,this);
#endif
    cleanup();
    delete this;
}

void ThreadPrivate::pubdestroy()
{
#ifdef DEBUG
    Debug(DebugAll,"ThreadPrivate::pubdestroy() '%s' [%p]",m_name,this);
#endif
    m_updest = false;
    if (!cancel()) {
	cleanup();
	m_thread = 0;
    }
}

void ThreadPrivate::run()
{
#ifdef DEBUG
    Debug(DebugAll,"ThreadPrivate::run() '%s' [%p]",m_name,this);
#endif
    ::pthread_once(&current_key_once,keyAllocFunc);
    ::pthread_setspecific(current_key,this);
    pthread_cleanup_push(cleanupFunc,this);
    ::pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,0);
    
    while (!m_running)
	::usleep(10);
    m_thread->run();
    pthread_cleanup_pop(1);
}

bool ThreadPrivate::cancel()
{
#ifdef DEBUG
    Debug(DebugAll,"ThreadPrivate::cancel() '%s' [%p]",m_name,this);
#endif
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
#ifdef DEBUG
    Debug(DebugAll,"ThreadPrivate::cleanup() '%s' [%p]",m_name,this);
#endif
    if (m_thread && m_thread->m_private) {
	m_thread->m_private = 0;
	m_thread->cleanup();
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
	    for (int i=0; i<5; i++) {
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
		Debug(DebugFail,"Could not kill %p, will use sledgehammer later.",t);
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
#ifdef __linux__
	Debug(DebugFail,"Brutally killing remaining threads!");
	::pthread_kill_other_threads_np();
#else
	Debug(DebugFail,"Aargh! I cannot kill remaining threads on this platform!");
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
#ifdef DEBUG
    Debug(DebugAll,"ThreadPrivate::cleanupFunc(%p)",arg);
#endif
    ThreadPrivate *t = reinterpret_cast<ThreadPrivate *>(arg);
    if (t)
	t->cleanup();
}

void ThreadPrivate::keyAllocFunc()
{
#ifdef DEBUG
    Debug(DebugAll,"ThreadPrivate::keyAllocFunc()");
#endif
    if (::pthread_key_create(&current_key,destroyFunc))
	Debug(DebugGoOn,"Failed to create current thread key!");
}

void *ThreadPrivate::startFunc(void *arg)
{
#ifdef DEBUG
    Debug(DebugAll,"ThreadPrivate::startFunc(%p)",arg);
#endif
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
#ifdef DEBUG
    Debug(DebugAll,"Thread::~Thread() [%p]",this);
#endif
    if (m_private)
	m_private->pubdestroy();
}

bool Thread::error() const
{
    return !m_private;
}

bool Thread::running() const
{
    return m_private ? m_private->m_running : false;
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
#ifdef DEBUG
    Debug(DebugAll,"Thread::cleanup() [%p]",this);
#endif
}

void Thread::killall()
{
    if (!ThreadPrivate::current())
	ThreadPrivate::killall();
}

void Thread::exit()
{
#ifdef DEBUG
    Debug(DebugAll,"Thread::exit()");
#endif
    ::pthread_exit(0);
}

void Thread::cancel()
{
#ifdef DEBUG
    Debug(DebugAll,"Thread::cancel() [%p]",this);
#endif
    if (m_private)
	m_private->cancel();
}

void Thread::yield()
{
    ::usleep(1);
}

void Thread::preExec()
{
#ifdef __linux__
    ::pthread_kill_other_threads_np();
#endif
}
