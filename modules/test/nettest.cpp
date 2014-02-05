/**
 * nettest.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Network and socket performance test module
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 * Author: Marian Podgoreanu
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

#include <yatephone.h>

#include <string.h>
#include <stdio.h>

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

using namespace TelEngine;
namespace { // anonymous

class Statistics;                        //
class NTTest;                            //
class NTWorkerContainer;                 // A set of workers
class NTWorker;                          //
class NTWriter;                          //
class NTReader;                          //
class NTSelectReader;                    //
class NTPlugin;                          // The module

/**
 * This class holds an fd_set
 * @short MultiSelect private data
 */
class PrivateFDSet
{
public:
    inline bool isset(int handle)
	{ return 0 != FD_ISSET(handle,&set); }
    inline void add(int handle)
	{ FD_SET(handle,&set); }
    inline void reset()
	{ FD_ZERO(&set); }
    fd_set set;
};

/**
 * This class encapsulates a select for a set of file descriptors.
 * File descriptors can be appended to wait for data to be read or write or
 *  wait for an exception to occur
 * @short A multiple file descriptor select
 */
class FDSetSelect
{
public:
    /**
     * Constructor
     */
    FDSetSelect();

    /**
     * Destructor. Release private data
     */
    ~FDSetSelect();

    /**
     * Check if data is available for read
     * This method should be called after @ref select() returns
     * @param handle File descriptor to check
     * @return True if there is any data available for the given file descriptor
     */
    bool canRead(int handle) const;

    /**
     * Check if a file descriptor can be used to write data
     * This method should be called after @ref select() returns
     * @param handle File descriptor to check
     * @return True if data can be written using the given file descriptor
     */
    bool canWrite(int handle) const;

    /**
     * Check if there is a pending event for a given file descriptor
     * This method should be called after @ref select() returns
     * @param handle File descriptor to check
     * @return True if there is a pending event for the given file descriptor
     */
    bool hasEvent(int handle) const;

    /**
     * Append a file descriptor to read, write and/or event set.
     * This method shouldn't be called while in select
     * @param handle File descriptor to append
     * @param read True to append to the read set (wait to receive data)
     * @param write True to append to the write set (check if the handle can be used to write data)
     * @param event True to append to the event set (check exceptions)
     * @return False if handle is invalid or target set is missing (all flags are false)
     */
    bool add(int handle, bool read, bool write, bool event);

    /**
     * Reset all file descriptor sets.
     * This method shouldn't be called while in select
     */
    void reset();

    /**
     * Start waiting for a file descriptor state change
     * @param uSec The select timeout in microseconds (can be 0 to wait until a file descriptor get set)
     * @return The number of file descriptors whose state changed or negative on error
     */
    int select(unsigned int uSec);

private:
    PrivateFDSet* m_read;                // Read set
    PrivateFDSet* m_write;               // Write set
    PrivateFDSet* m_event;               // Event set
    PrivateFDSet* m_crtR;                // Current event set for read/write/events
    PrivateFDSet* m_crtW;                //
    PrivateFDSet* m_crtE;                //
    int m_maxHandle;                     // Maximum handle value in current set(s)
    bool m_selectError;                  // Flag used to output errors
};

// Statistics class
class Statistics
{
public:
    inline Statistics()
	{ reset(); }
    inline void reset() {
	    msStart = Time::msecNow();
	    msStop = 0;
	    packets = totalBytes = errors = lostBytes = 0;
	    stopped = 0;
	}
    inline void success(unsigned int bytes) {
	    packets++;
	    totalBytes += bytes;
	}
    inline void failure(unsigned int bytes) {
	    packets++;
	    errors++;
	    lostBytes += bytes;
	}
    inline Statistics& operator+=(const Statistics& src) {
	    packets += src.packets;
	    totalBytes += src.totalBytes;
	    errors += src.errors;
	    lostBytes += src.lostBytes;
	    stopped += src.stopped;
	    return *this;
	}

    void output(String& dest);

    u_int64_t msStart;
    u_int64_t msStop;
    u_int64_t packets;
    u_int64_t totalBytes;
    u_int64_t errors;
    u_int64_t lostBytes;
    unsigned int stopped;
};

class NTTest : public Mutex, public GenObject, public DebugEnabler
{
public:
    NTTest(const char* name);
    virtual ~NTTest()
	{ stop(); }
    virtual void destruct()
	{ stop(); GenObject::destruct(); }
    inline bool send() const
	{ return m_send; }
    inline const String& localip() const
	{ return m_localip; }
    inline const String& remoteip() const
	{ return m_remoteip; }
    inline unsigned int packetLen() const
	{ return m_packetLen; }
    inline unsigned int interval() const
	{ return m_interval; }
    inline unsigned int lifetime() const
	{ return m_lifetime; }
    inline unsigned int packetCount() const
	{ return m_packetCount; }
    inline int selectTimeout() const
	{ return m_selectTimeout; }
    bool init(NamedList& params);
    void start();
    void stop();
    void addWorker();
    void removeWorker(NTWorker* worker);
private:
    Mutex m_mutex;
    String m_id;
    String m_localip;
    String m_remoteip;
    int m_port;
    unsigned int m_threads;
    bool m_send;
    unsigned int m_packetLen;
    unsigned int m_interval;
    unsigned int m_lifetime;
    unsigned int m_packetCount;
    ObjList m_containers;
    unsigned int m_workerCount;
    int m_selectTimeout;
    Statistics m_localStats;
};

class NTWorkerContainer : public Mutex, public GenObject, public DebugEnabler
{
    friend class SelectThread;
public:
    NTWorkerContainer(NTTest* test, unsigned int threads, const char* id);
    inline NTTest* test()
	{ return m_test; }
    void start(int& port);
    void stop();
    void addWorker(NTWorker* worker);
    void removeWorker(NTWorker* worker);
private:
    String m_id;
    NTTest* m_test;
    unsigned int m_workerCount;
    unsigned int m_threads;
    ObjList m_workers;
};

class NTWorker : public Thread, public GenObject
{
public:
    NTWorker(NTWorkerContainer* container, int port, const char* name = "NTWorker");
    ~NTWorker();
    const Statistics& counters() const
	{ return m_counters; }
protected:
    bool initSocket(Socket* sock = 0, SocketAddr* addr = 0);
protected:
    NTWorkerContainer* m_container;
    NTTest* m_test;
    u_int64_t m_timeToDie;
    Socket m_socket;
    SocketAddr m_addr;
    Statistics m_counters;
};

class NTWriter : public NTWorker
{
public:
    inline NTWriter(NTWorkerContainer* container, int port)
	: NTWorker(container,port),
	m_timeToSend(0)
	{}
    virtual void run();
private:
    u_int64_t m_timeToSend;
};

class NTReader : public NTWorker
{
public:
    inline NTReader(NTWorkerContainer* container, int port)
	: NTWorker(container,port)
	{}
    virtual void run();
};

class NTSelectReader : public NTWorker
{
public:
    NTSelectReader(NTWorkerContainer* container, int& port, unsigned int count);
    virtual ~NTSelectReader();
    virtual void run();
private:
    Socket* m_sockets;
    unsigned int m_count;
};

class NTPlugin : public Module
{
public:
    NTPlugin();
    virtual ~NTPlugin();
    virtual void initialize();
    virtual bool received(Message& msg, int id);
private:
    bool m_first;
};

// Static data
static DataBlock s_stopPattern;
static NTTest* s_test = 0;
// Config
static String s_localip;
static unsigned int s_packetLen = 320;
static unsigned int s_interval = 20;
static unsigned int s_lifetime = 60;
static unsigned long s_sleep = 2;
// Plugin
static NTPlugin plugin;


/**
 * FDSetSelect
 */
// Constructor
FDSetSelect::FDSetSelect()
    : m_read(new PrivateFDSet),
    m_write(new PrivateFDSet),
    m_event(new PrivateFDSet),
    m_crtR(0),
    m_crtW(0),
    m_crtE(0),
    m_maxHandle(Socket::invalidHandle()),
    m_selectError(false)
{
}

// Release private data
FDSetSelect::~FDSetSelect()
{
    delete m_read;
    delete m_write;
    delete m_event;
}

// Check if data is available for read
bool FDSetSelect::canRead(int handle) const
{
    return m_read->isset(handle);
}

// Check if a file descriptor can be used to write data
bool FDSetSelect::canWrite(int handle) const
{
    return m_write->isset(handle);
}


// Check if there is a pending event for a given file descriptor
bool FDSetSelect::hasEvent(int handle) const
{
    return m_event->isset(handle);
}

// Append a file descriptor to read, write and/or event set.
// Return false if handle is invalid or target set is missing (all flags are false)
bool FDSetSelect::add(int handle, bool read, bool write, bool event)
{
    if (!(read || write || event) || handle == Socket::invalidHandle() ||
	!Socket::canSelect(handle))
	return false;
    if (read) {
	m_read->add(handle);
	m_crtR = m_read;
    }
    if (write) {
	m_write->add(handle);
	m_crtW = m_write;
    }
    if (event) {
	m_event->add(handle);
	m_crtE = m_event;
    }
    if (m_maxHandle < handle)
	m_maxHandle = handle;
    return true;
}

// Reset all file descriptor sets
void FDSetSelect::reset()
{
    m_read->reset();
    m_write->reset();
    m_event->reset();
    m_crtR = m_crtW = m_crtE = 0;
    m_maxHandle = Socket::invalidHandle();
}

// Start waiting for a file descriptor state change
int FDSetSelect::select(unsigned int uSec)
{
    if (m_maxHandle == Socket::invalidHandle())
	return 0;
    struct timeval t;
    t.tv_sec = 0;
    t.tv_usec = (uSec > 0) ? uSec : 0;
    m_selectError = false;
    int result = ::select(m_maxHandle+1,&m_crtR->set,&m_crtW->set,&m_crtE->set,&t);
    if (result >= 0) {
	XDebug(DebugAll,"FDSetSelect got %d handlers [%p]",result,this);
	return result;
    }
    bool canRetry = (errno == EAGAIN || errno == EINTR || errno == EBADF);
    if (!(canRetry || m_selectError)) {
	Debug(DebugWarn,"FDSetSelect failed: %d: %s [%p]",errno,::strerror(errno),this);
	m_selectError = true;
    }
    return -1;
}


/**
 * Statistics
 */
void Statistics::output(String& dest)
{
    dest << "=================================================================";
#define stat_set64(text,val) \
    ::sprintf(buf,FMT64,val); \
    dest << "\r\n" << text << buf;
    char buf[128];
    stat_set64("Packets:           ",packets);
    stat_set64("Total (bytes):     ",totalBytes);
    stat_set64("Errors:            ",errors);
    stat_set64("Lost (bytes):      ",lostBytes);
    dest <<"\r\nStopped:           " << stopped;
    u_int64_t stop = msStop ? msStop : Time::msecNow();
    u_int64_t lenMsec = stop - msStart;
    u_int64_t lenSec = lenMsec / 1000;
    if (!lenSec)
	lenSec = 1;
    stat_set64("Test length (ms):  ",lenMsec);
    stat_set64("Ratio (Mb/s):      ",totalBytes / lenSec * 8 / 1000000);
    stat_set64("Ratio (packets/s): ",packets/lenSec);
#undef stat_set64
    dest << "\r\n=================================================================";
}


/**
 * NTTest
 */
NTTest::NTTest(const char* name)
    : Mutex(true),
    m_mutex(true),
    m_port(0),
    m_threads(0),
    m_send(true),
    m_packetLen(0),
    m_interval(0),
    m_lifetime(0),
    m_packetCount(0),
    m_workerCount(0),
    m_selectTimeout(-1)
{
    debugChain(&plugin);
    m_id << plugin.debugName() << "/" << name;
    debugName(m_id);
}

bool NTTest::init(NamedList& params)
{
    Lock2 lock(*this,m_mutex);
    stop();

    m_localip = params.getValue("localip",s_localip);
    if (m_localip.null()) {
	Debug(this,DebugNote,"Empty localip in section '%s'",debugName());
	return false;
    }
    m_remoteip = params.getValue("remoteip");
    if (m_remoteip.null()) {
	Debug(this,DebugNote,"Empty remoteip in section '%s'",debugName());
	return false;
    }

    String tmp = params.getValue("port");
    m_port = tmp.toInteger(0);
    if (!m_port) {
	Debug(this,DebugNote,"Invalid port=%s in section '%s'",
	    tmp.c_str(),debugName());
	return false;
    }

    m_threads = params.getIntValue("threads",1);
    if (m_threads < 1)
	m_threads = 1;

    m_send = params.getBoolValue("send",true);

    m_packetLen = params.getIntValue("packetlen",s_packetLen);
    if (m_packetLen < 16)
	m_packetLen = 16;
    else if (m_packetLen > 1400)
	m_packetLen = 1400;
    m_interval = params.getIntValue("interval",s_interval);
    if (m_interval < 1)
	m_interval = 1;
    else if (m_interval > 120)
	m_interval = 120;
    m_lifetime = params.getIntValue("lifetime",s_lifetime);
    bool sendAllPackets = params.getBoolValue("sendallpackets",true);
    if (sendAllPackets)
	m_packetCount = m_lifetime * 1000 / m_interval;
    else
	m_packetCount = 0;
    m_selectTimeout = params.getIntValue("select-timeout",-1);

    m_containers.clear();
    int workersets = params.getIntValue("workersets",1);
    if (workersets < 1 || (unsigned int)workersets > m_threads)
	workersets = 1;
    unsigned int nFull = 0;
    unsigned int nRest = 0;
    if (workersets == 1)
	nRest = m_threads;
    else {
	nFull = m_threads / (workersets - 1);
	nRest = m_threads - nFull * (workersets - 1);
    }
    for (int i = 1; i <= workersets; i++) {
	String id;
	id << m_id << "/" << i;
	if (i < workersets)
	    m_containers.append(new NTWorkerContainer(this,nFull,id));
	else
	    m_containers.append(new NTWorkerContainer(this,nRest,id));
    }

    unsigned int sock = m_threads;
    if (m_selectTimeout >= 0)
	m_threads = workersets;

    tmp = "";
    tmp << "\r\nAction:         " << (m_send ? "send" : "recv");
    if (m_selectTimeout >= 0)
	tmp << "\r\nSockets:        " << sock;
    else
	tmp << "\r\nThreads:        " << m_threads;
    tmp << "\r\nLocal address:  " << m_localip;
    tmp << "\r\nRemote address: " << m_remoteip;
    tmp << "\r\nPort:           " << m_port;
    tmp << "\r\nPacket length:  " << m_packetLen;
    tmp << "\r\nPackets:        " << m_packetCount;
    tmp << "\r\nInterval:       " << m_interval << "ms";
    tmp << "\r\nLifetime:       " << m_lifetime << "s";
    tmp << "\r\nWorker sets:    " << workersets;
    tmp << "\r\nSelect timeout: " << m_selectTimeout << (m_selectTimeout < 0 ? " (not used)" : "us");
    Debug(this,DebugInfo,"Initialized:%s",tmp.c_str());
    return true;
}

void NTTest::start()
{
    Lock lock(m_mutex);
    stop();
    DDebug(this,DebugAll,"Starting");
    m_localStats.reset();
    int port = m_port;
    for (ObjList* o = m_containers.skipNull(); o; o = o->skipNext())
	(static_cast<NTWorkerContainer*>(o->get()))->start(port);
}

void NTTest::stop()
{
    Lock lock(m_mutex);
    DDebug(this,DebugAll,"Stopping %u workers",m_workerCount);
    for (ObjList* o = m_containers.skipNull(); o; o = o->skipNext())
	(static_cast<NTWorkerContainer*>(o->get()))->stop();
}

void NTTest::addWorker()
{
    Lock lock(this);
    m_workerCount++;
    if (m_workerCount == m_threads)
	Debug(this,DebugAll,"Created %u workers",m_workerCount);
}

void NTTest::removeWorker(NTWorker* worker)
{
    Lock lock(this);
    if (!(worker && m_workerCount))
	return;
    m_localStats += worker->counters();
    m_workerCount--;
    if (m_workerCount)
	return;
    lock.drop();
    m_localStats.msStop = Time::msecNow();
    String tmp;
    m_localStats.output(tmp);
    Debug(this,DebugInfo,"No more workers. Local statistics:\r\n%s",tmp.c_str());
}


/**
 * NTWorkerContainer
 */
NTWorkerContainer::NTWorkerContainer(NTTest* test, unsigned int threads, const char* id)
    : Mutex(true),
    m_id(id),
    m_test(test),
    m_workerCount(0),
    m_threads(threads)
{
    debugName(m_id);
    debugChain(m_test);
}

void NTWorkerContainer::start(int& port)
{
    Lock lock(this);
    stop();
    if (!m_test)
	return;
    lock.drop();
    DDebug(this,DebugAll,"Starting");
    if (m_test->selectTimeout() >= 0)
	(new NTSelectReader(this,port,m_threads))->startup();
    else
	for (unsigned int i = 0; i < m_threads; i++, port++)
	    if (m_test->send())
		(new NTWriter(this,port))->startup();
	    else
		(new NTReader(this,port))->startup();
}

void NTWorkerContainer::stop()
{
    Lock l(this);
    DDebug(this,DebugAll,"Stopping %u workers",m_workerCount);
    if (!m_workerCount)
	return;
    if (m_workerCount) {
	ListIterator iterw(m_workers);
	for (GenObject* o = 0; 0 != (o = iterw.get());)
	    (static_cast<NTWorker*>(o))->cancel(false);
    }
    l.drop();
    while (m_workerCount)
	Thread::yield();
    DDebug(this,DebugAll,"Stopped");
}

void NTWorkerContainer::addWorker(NTWorker* worker)
{
    Lock lock(this);
    if (!worker)
	return;
    ObjList* obj = m_workers.append(worker);
    if (!obj)
	return;
    m_workerCount++;
    obj->setDelete(false);
    if (m_workerCount >= m_threads)
	DDebug(this,DebugAll,"Created %u workers",m_workerCount);
    lock.drop();
    if (m_test)
	m_test->addWorker();
}

void NTWorkerContainer::removeWorker(NTWorker* worker)
{
    Lock lock(this);
    if (!(worker && m_workerCount))
	return;
    m_workers.remove(worker,false);
    if (m_workerCount)
	m_workerCount--;
    if (!m_workerCount)
	DDebug(this,DebugAll,"No more workers");
    lock.drop();
    if (m_test)
	m_test->removeWorker(worker);
}


/**
 * NTWorker
 */
NTWorker::NTWorker(NTWorkerContainer* container, int port, const char* name)
    : Thread(name),
    m_container(container),
    m_test(container ? container->test() : 0),
    m_timeToDie(0),
    m_addr(AF_INET)
{
    if (!(m_container && m_test))
	return;
    container->addWorker(this);
    m_addr.host(m_test->send() ? m_test->remoteip() : m_test->localip());
    m_addr.port(port);
    if (!m_test->packetCount() && m_test->lifetime())
	m_timeToDie = Time::msecNow() + m_test->lifetime() * 1000;
}

NTWorker::~NTWorker()
{
    if (m_socket.valid()) {
	m_socket.setLinger(-1);
	m_socket.terminate();
    }
    if (m_container)
	m_container->removeWorker(this);
}

bool NTWorker::initSocket(Socket* sock, SocketAddr* addr)
{
    if (!(m_container && m_test))
	return false;
    if (!sock) {
	sock = &m_socket;
	addr = &m_addr;
    }
    if (!sock->create(addr->family(),SOCK_DGRAM)) {
	Debug(m_container,DebugNote,"Failed to create socket: %d '%s' [%p]",
	    sock->error(),::strerror(sock->error()),this);
	return false;
    }
    if (!m_test->send() && !sock->bind(*addr)) {
	Debug(m_container,DebugNote,"Failed to bind socket on port %d: %d '%s' [%p]",
	    addr->port(),sock->error(),::strerror(sock->error()),this);
	return false;
    }
    sock->setBlocking(false);
    return true;
}


/**
 * NTWriter
 */
void NTWriter::run()
{
    if (!initSocket())
	return;
    unsigned char buf[m_test->packetLen()];
    buf[0] = 1;
    while (true) {
	u_int64_t now = Time::msecNow();
	if (now < m_timeToSend) {
	    Thread::msleep(s_sleep,true);
	    continue;
	}

	bool die = false;
	if (m_test->packetCount())
	    die = m_counters.packets >= m_test->packetCount();
	else
	    die = m_timeToDie && now > m_timeToDie;
	if (die) {
	    Thread::msleep(5,true);
	    if (0 < m_socket.sendTo(s_stopPattern.data(),s_stopPattern.length(),m_addr))
		m_counters.stopped = 1;
	    break;
	}

	Thread::check(true);
	m_timeToSend = now + m_test->interval();
	int w = m_socket.sendTo(buf,m_test->packetLen(),m_addr);
	if (w != m_socket.socketError() || m_socket.canRetry()) {
	    if (w == m_socket.socketError())
		continue;
	    if (w)
		m_counters.success(w);
	    if ((unsigned int)w < m_test->packetLen())
		m_counters.failure(m_test->packetLen() - w);
	    continue;
	}
	Debug(m_container,DebugNote,"SEND error dest='%s:%d': %d '%s' [%p]",
	    m_addr.host().c_str(),m_addr.port(),
	    m_socket.error(),::strerror(m_socket.error()),this);
	m_counters.failure(m_test->packetLen());
    }
}


/**
 * NTReader
 */
void NTReader::run()
{
    if (!initSocket())
	return;
    unsigned char buf[m_test->packetLen()];
    SocketAddr addr;
    while (true) {
	if (m_timeToDie && (Time::msecNow() > m_timeToDie))
	    break;
	Thread::msleep(s_sleep,true);
	int r = m_socket.recvFrom(buf,m_test->packetLen(),addr);
	if (r > 0) {
	    if (buf[0] == 0) {
		m_counters.stopped = 1;
		break;
	    }
	    m_counters.success(r);
	    continue;
	}
	if (r == 0 || (r == m_socket.socketError() && m_socket.canRetry()))
	    continue;
	Debug(m_container,DebugNote,"RECV error src='%s:%d': %d '%s' [%p]",
	    addr.host().c_str(),addr.port(),
	    m_socket.error(),::strerror(m_socket.error()),this);
	m_counters.failure(0);
    }
}


/**
 * NTSelectReader
 */
NTSelectReader::NTSelectReader(NTWorkerContainer* container, int& port, unsigned int count)
    : NTWorker(container,0,"NTSelectReader"),
    m_sockets(0),
    m_count(count)
{
    DDebug(container,DebugAll,"NTSelectReader sockets=%u",count);
    m_sockets = new Socket[m_count];
    unsigned int ok = 0;
    for (unsigned int i = 0; i < count; i++, port++) {
	SocketAddr addr(AF_INET);
	addr.host(m_test->localip());
	addr.port(port);
	if (initSocket(&m_sockets[i],&addr))
	    ok++;
    }
    if (!ok) {
	Debug(container,DebugNote,"NTSelectReader: Bind or create failed for all sockets");
	delete[] m_sockets;
	m_sockets = 0;
	m_count = 0;
    }
}

NTSelectReader::~NTSelectReader()
{
    if (!m_sockets)
	return;
    for (unsigned int i = 0; i < m_count; i++)
	if (m_sockets[i].valid()) {
	    m_sockets[i].setLinger(-1);
	    m_sockets[i].terminate();
	}
    delete[] m_sockets;
}

void NTSelectReader::run()
{
    if (!m_count || !m_test || m_test->send())
	return;
    DDebug(m_container,DebugAll,"Select reader worker started");
    unsigned char buf[m_test->packetLen()];
    SocketAddr addr;
    int ok = 0;
    FDSetSelect set;
    while (true) {
	if (m_counters.stopped == m_count)
	    break;
	if (m_timeToDie && (Time::msecNow() > m_timeToDie))
	    break;
	set.reset();
	for (unsigned int i = 0; i < m_count; i++)
	    set.add(m_sockets[i].handle(),true,false,false);
	ok = set.select(m_container->test()->selectTimeout());
	if (ok <= 0) {
	    if (!m_test->selectTimeout())
		Thread::msleep(1,true);
	    continue;
	}
	for (unsigned int i = 0; i < m_count; i++) {
	    if (!(m_sockets[i].valid() && set.canRead(m_sockets[i].handle())))
		continue;
	    int r = m_sockets[i].recvFrom(buf,m_test->packetLen(),addr);
	    if (r > 0) {
		if (buf[0]) {
		    if ((unsigned int)r != m_test->packetLen())
			Debug(m_container,DebugMild,"RECV %u expected=%u [%p]",
			    r,m_test->packetLen(),this);
		    m_counters.success(r);
		}
		else {
		    m_sockets[i].setLinger(-1);
		    m_sockets[i].terminate();
		    m_counters.stopped++;
		}
		continue;
	    }
	    m_counters.failure(0);
	    if (r == 0 || (r == m_sockets[i].socketError() && m_sockets[i].canRetry()))
		continue;
	    Debug(m_container,DebugNote,"RECV error src='%s:%d': %d '%s' [%p]",
		addr.host().c_str(),addr.port(),
		m_sockets[i].error(),::strerror(m_sockets[i].error()),this);
	}
    }
}


/**
 * Plugin
 */
NTPlugin::NTPlugin()
    : Module("nettest","misc"), m_first(true)
{
    Output("Loaded module Network Test");
}

NTPlugin::~NTPlugin()
{
    Output("Unloading module Network Test");
}

void NTPlugin::initialize()
{
    Output("Initializing module Network Test");

    debugLevel(10);

    if (m_first) {
	m_first = false;
	setup();
	installRelay(Halt);
    }

    // Reset statistics
    lock();
    TelEngine::destruct(s_test);

    // Get new values from config
    Configuration cfg(Engine::configFile("nettest"));
    NamedList* general = cfg.getSection("general");
    NamedList dummy("");
    if (!general)
	general = &dummy;
    s_localip = general->getValue("localip");
    s_packetLen = general->getIntValue("packetlen",320);
    if (s_packetLen < 16)
	s_packetLen = 16;
    else if (s_packetLen > 1400)
	s_packetLen = 1400;
    s_interval = general->getIntValue("interval",20);
    if (s_interval < 1)
	s_interval = 1;
    else if (s_interval > 120)
	s_interval = 120;
    s_lifetime = general->getIntValue("lifetime",60);
    s_sleep = cfg.getIntValue("general","sleep",2);
    if (s_sleep < 1)
	s_sleep = 1;
    else if (s_sleep > 10)
	s_sleep = 10;
    s_stopPattern.assign(0,s_packetLen);

    Debug(this,DebugInfo,
	"Init: localip=%s packet=%u interval=%ums lifetime=%us",
	s_localip.c_str(),s_packetLen,s_interval,s_lifetime);

    unsigned int n = cfg.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* sect = cfg.getSection(i);
	if (!sect || sect->null() || *sect == "general")
	    continue;

	s_test = new NTTest(*sect);
	if (!s_test->init(*sect)) {
	    Debug(this,DebugNote,"Failed to init test from section '%s'",sect->c_str());
	    TelEngine::destruct(s_test);
	    continue;
	}

	s_test->start();
	break;
    }

    unlock();
}

bool NTPlugin::received(Message& msg, int id)
{
    if (id == Halt)
	TelEngine::destruct(s_test);
    return Module::received(msg,id);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
