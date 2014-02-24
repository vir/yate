/**
 * sigtransport.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * SIGTRAN transports provider, supports SCTP, TCP, UDP, UNIX sockets
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2009-2014 Null Team
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <yatephone.h>
#include <yatesig.h>

#define MAX_BUF_SIZE  48500

#define CONN_RETRY_MIN   250000
#define CONN_RETRY_MAX 60000000
#define DECREASE_INTERVAL 1000000
#define DECREASE_AMOUNT    250000

using namespace TelEngine;
namespace { // anonymous

class Transport;
class TransportWorker;
class TransportThread;
class SockRef;
class MessageReader;
class TReader;
class StreamReader;
class ListenThread;
class TransportModule;

class TransportWorker
{
    friend class TransportThread;
public:
    inline TransportWorker()
	: m_thread(0), m_threadMutex(true,"TransportThread")
	{ }
    virtual ~TransportWorker() { stop(); }
    virtual bool readData() = 0;
    virtual bool connectSocket() = 0;
    virtual bool needConnect() = 0;
    virtual void reset() = 0;
    bool running();
    bool start(Thread::Priority prio = Thread::Normal);
    inline void resetThread()
	{
	    Lock myLock(m_threadMutex);
	    m_thread = 0;
	}
    virtual const char* getTransportName() = 0;
    inline bool hasThread()
	{
	    Lock myLock(m_threadMutex);
	    return m_thread != 0;
	}
    void exitThread();
protected:
    void stop();
private:
    TransportThread* m_thread;
    Mutex m_threadMutex;
};

class SockRef : public RefObject
{
public:
    inline SockRef(Socket** sock)
	: m_sock(sock)
	{ }
    void* getObject(const String& name) const
    {
	if (name == "Socket*")
	    return m_sock;
	return RefObject::getObject(name);
    }
private:
    Socket** m_sock;
};

class TransportThread : public Thread
{
    friend class TransportWorker;
public:
    inline TransportThread(TransportWorker* worker, String* tName, Priority prio = Normal)
	: Thread(*tName,prio), m_worker(worker), m_exit(false), m_threadName(tName), m_cleanWorker(true)
	{ }
    virtual ~TransportThread();
    virtual void run();
    void exitThread()
	{
	    m_cleanWorker = false;
	    m_exit = true;
	}
    void resetWorker()
	{ m_worker = 0; }
private:
    TransportWorker* m_worker;
    bool m_exit;
    String* m_threadName;
    bool m_cleanWorker;
};

class ListenerThread : public Thread
{
public:
    bool init(const NamedList& param);
    ListenerThread(Transport* trans);
    ~ListenerThread();
    inline void terminate()
	{ cancel(); }
    virtual void run();
    bool addAddress(const NamedList &param);
private:
    Socket* m_socket;
    Transport* m_transport;
    bool m_stream;
};

class TReader : public TransportWorker, public Mutex, public RefObject
{
public:
    inline TReader()
	: Mutex(true,"TReader"),
	  m_sending(true,"TReader::sending"), m_canSend(true), m_reconnect(false),
	  m_tryAgain(0), m_interval(CONN_RETRY_MIN), m_downTime(0), m_decrease(0)
	{ }
    virtual ~TReader();
    virtual void listen(int maxConn) = 0;
    virtual bool sendMSG(const DataBlock& header, const DataBlock& msg, int streamId = 0) = 0;
    virtual void setSocket(Socket* s) = 0;
    virtual bool getSocketParams(const String& params, NamedList& result) = 0;
    virtual void reset() = 0;
    void reconnect()
	{ m_reconnect = true; }
    Mutex m_sending;
    bool m_canSend;
protected:
    bool m_reconnect;
    u_int64_t m_tryAgain;
    u_int32_t m_interval;
    u_int64_t m_downTime;
    u_int64_t m_decrease;
};

class Transport : public SIGTransport
{
public:
    enum TransportType {
	None = 0,
	Sctp,
	// All the following transports are not standard
	Tcp,
	Udp,
	Unix,
    };
    enum State {
	Up,
	Initiating,
	Down
    };
    virtual bool initialize(const NamedList* config);
    Transport(const NamedList& params, String* mutexName);
    Transport(TransportType type, String* mutexName);
    ~Transport();
    inline unsigned char getVersion(unsigned char* buf) const
	{ return buf[0]; }
    inline unsigned char getType(unsigned char* buf) const
	{ return buf[3]; }
    inline unsigned char getClass(unsigned char* buf) const
	{ return buf[2]; }
    inline int transType() const
	{ return m_type; }
    inline bool listen() const
	{ return m_listener != 0; }
    inline bool streamDefault() const
	{ return m_type == Tcp || m_type == Unix; }
    inline bool supportEvents()
	{ return m_type == Sctp && m_supportEvents; }
    void setStatus(int status);
    inline int status() const
	{ return m_state; }
    inline void resetListener()
	{ m_listener = 0; }
    inline void startReading()
	{ if (m_reader) m_reader->start(); }
    bool addSocket(Socket* socket,SocketAddr& adress);
    virtual bool reliable() const
	{ return m_type == Sctp || m_type == Tcp; }
    virtual bool control(NamedList &param);
    virtual bool connected(int id) const
	{ return m_state == Up;}
    virtual void attached(bool ual)
	{ }
    virtual void reconnect(bool force);
    bool connectSocket();
    void resetReader(TReader* reader);
    u_int32_t getMsgLen(unsigned char* buf);
    bool addAddress(const NamedList &param, Socket* socket);
    bool bindSocket();
    static SignallingComponent* create(const String& type, NamedList& params);
    virtual bool transmitMSG(const DataBlock& header, const DataBlock& msg, int streamId = 0);
    virtual bool getSocketParams(const String& params, NamedList& result);
    virtual void destroyed();

    virtual bool hasThread();
    virtual void stopThread();
private:
    TReader* m_reader;
    Mutex m_readerMutex;
    bool m_streamer;
    int m_type;
    int m_state;
    ListenerThread* m_listener;
    NamedList m_config;
    bool m_endpoint;
    bool m_supportEvents;
    bool m_listenNotify;
    String* m_mutexName;
};

class StreamReader : public TReader
{
public:
    StreamReader(Transport* transport, Socket* sock);
    ~StreamReader();
    virtual bool readData();
    virtual bool connectSocket();
    virtual bool needConnect()
	{ return m_transport && m_transport->status() == Transport::Down && !m_transport->listen(); }
    virtual bool sendMSG(const DataBlock& header, const DataBlock& msg, int streamId = 0);
    virtual void setSocket(Socket* s);
    virtual void listen(int maxConn)
	{ }
    virtual bool sendBuffer(int streamId = 0);
    virtual bool getSocketParams(const String& params, NamedList& result);
    virtual void reset()
	{ if (m_transport) m_transport->resetReader(this); }
    void connectionDown(bool stop = true);
    void stopThread();
    virtual const char* getTransportName()
	{ return m_transport ? m_transport->debugName() : ""; }
private:
    Transport* m_transport;
    Socket* m_socket;
    DataBlock m_sendBuffer;
    DataBlock m_recvBuffer;
    DataBlock m_headerBuffer;
    int m_headerLen;
    DataBlock m_readBuffer;
    u_int32_t m_totalPacketLen;
};

class MessageReader : public TReader
{
public:
    MessageReader(Transport* transport, Socket* sock, SocketAddr& remote);
    ~MessageReader();
    virtual bool readData();
    virtual bool connectSocket()
	{ return bindSocket(); }
    virtual bool needConnect()
	{ return m_transport && m_transport->status() == Transport::Down && !m_transport->listen(); }
    virtual bool sendMSG(const DataBlock& header, const DataBlock& msg, int streamId = 0);
    virtual bool getSocketParams(const String& params, NamedList& result);
    virtual void setSocket(Socket* s);
    virtual void listen(int maxConn)
	{ m_socket->listen(maxConn); }
    virtual void reset()
	{ m_transport->resetReader(this); }
    bool bindSocket();
    void reconnectSocket();
    void updateTransportStatus(int status);
    virtual const char* getTransportName()
	{ return m_transport ? m_transport->debugName() : ""; }
private:
    Transport* m_transport;
    Socket* m_socket;
    SocketAddr m_remote;
    u_int32_t m_reconnectInterval;
    u_int64_t m_reconnectTryAgain;
};

class TransportModule : public Module
{
public:
    TransportModule();
    ~TransportModule();
    virtual void initialize();
private:
    bool m_init;
};

static TransportModule plugin;
YSIGFACTORY2(Transport);
static long s_maxDownAllowed = 10000000;
static ObjList s_names;
Mutex s_namesMutex(false,"TransportNames");

static void addName(String* name)
{
    Lock myLock(s_namesMutex);
    s_names.append(name);
}

static void removeName(String* name)
{
    Lock myLock(s_namesMutex);
    s_names.remove(name);
}

static const TokenDict s_transType[] = {
    { "none",  Transport::None },
    { "sctp",  Transport::Sctp },
    { "tcp",   Transport::Tcp },
    { "udp",   Transport::Udp },
    { "unix",  Transport::Unix },
    { 0, 0 }
};

static const TokenDict s_transStatus[] = {
    { "up", Transport::Up },
    { "initiating", Transport::Initiating },
    { "down", Transport::Down },
    { 0, 0 }
};

static void resolveAddress(const String& addr, String& ip, int& port)
{
    ObjList* o = addr.split(':');
    if (!(o && o->count())) {
	ip = "0.0.0.0";
	TelEngine::destruct(o);
	return;
    }
    const String* s = static_cast<const String*>(o->get());
    if (TelEngine::null(s))
	ip = "0.0.0.0";
    else
	ip = *s;
    ObjList* p = o->skipNext();
    if (p)
	port = p->get()->toString().toInteger(port);
    TelEngine::destruct(o);
}

/** ListenerThread class */

ListenerThread::ListenerThread(Transport* trans)
    : Thread("Listener Thread"),
      m_socket(0), m_transport(trans), m_stream(true)
{
    DDebug("Transport Listener:",DebugAll,"Creating ListenerThread (%p)",this);
}

ListenerThread::~ListenerThread()
{
    DDebug("Transport Listener",DebugAll,"Destroying ListenerThread (%p)",this);
    if (m_transport) {
	Debug("Transport Listener",DebugWarn,"Unusual exit");
	m_transport->resetListener();
    }
    m_transport = 0;
    m_socket->terminate();
    delete m_socket;
}

bool ListenerThread::init(const NamedList& param)
{
    m_socket = new Socket();
    bool multi = param.getParam("local1") != 0;
    m_stream = param.getBoolValue("stream",m_transport->streamDefault());
    if (multi && m_transport->transType() != Transport::Sctp) {
	Debug("ListenerThread",DebugWarn,"Socket %s does not support multihomed",
	    lookup(m_transport->transType(),s_transType));
	return false;
    }
    switch (m_transport->transType()) {
	case Transport::Sctp:
	{
	    Socket* soc = 0;
	    Message m("socket.sctp");
	    SockRef* s = new SockRef(&soc);
	    m.userData(s);
	    TelEngine::destruct(s);
	    if (!(Engine::dispatch(m) && soc)) {
		Debug("ListenerThread",DebugConf,"Could not obtain SctpSocket");
		return false;
	    }
	    delete m_socket;
	    m_socket = soc;
	    m_socket->create(AF_INET,m_stream ? SOCK_STREAM : SOCK_SEQPACKET,IPPROTO_SCTP);
	    break;
	}
	case Transport::Tcp:
	    m_socket->create(AF_INET,SOCK_STREAM);
	    break;
	case Transport::Udp:
	    m_socket->create(AF_INET,SOCK_DGRAM);
	    break;
	case Transport::Unix:
	    m_socket->create(AF_UNIX,SOCK_STREAM);
	    break;
	default:
	    Debug("ListenerThread",DebugWarn,"Unknown type of socket %d",m_transport->transType());
    }
    if (!m_socket->valid()) {
	Debug("ListenerThread",DebugWarn,"Unable to create listener socket: %s",
	    strerror(m_socket->error()));
	return false;
    }
    if (!m_socket->setBlocking(false)) {
	DDebug("ListenerThread",DebugWarn,"Unable to set listener to nonblocking mode");
	return false;
    }
    SocketAddr addr(AF_INET);
    String address, adr = param.getValue("local");
    int port = m_transport->defPort();
    resolveAddress(adr,address,port);
    addr.host(address);
    addr.port(port);
    if (!m_socket->bind(addr)) {
	Debug(DebugWarn,"Unable to bind to %s:%u: %d %s",
	    addr.host().c_str(),addr.port(),errno,strerror(errno));
	return false;
    } else
	DDebug("ListenerThread",DebugAll,"Socket bound to %s:%u",
	    addr.host().c_str(),addr.port());
    if (multi && !addAddress(param))
	return false;
    if (!m_socket->listen(3)) {
	Debug("ListenerThread",DebugWarn,"Unable to listen on socket: %d %s",
	    m_socket->error(),strerror(m_socket->error()));
	return false;
    }
    return true;
}

void ListenerThread::run()
{
    if (!m_stream)
	return;
    for (;;)
    {
	Thread::msleep(50,false);
	if (check(false) || Engine::exiting())
	    break;
	SocketAddr address;
	Socket* newSoc = m_socket->accept(address);
	if (!newSoc) {
	    if (!m_socket->canRetry())
		DDebug("ListenerThread",DebugNote, "Accept error: %s", strerror(m_socket->error()));
	    continue;
	} else {
	    if (!m_transport->addSocket(newSoc,address)) {
		DDebug("ListenerThread",DebugNote,"Connection rejected for %s",
		    address.host().c_str());
		newSoc->terminate();
		delete newSoc;
	    }
	}
    }
    if (m_transport) {
	m_transport->resetListener();
	m_transport = 0;
    }
}

bool ListenerThread::addAddress(const NamedList &param)
{
    ObjList o;
    for (int i = 1; ; i++) {
	String temp = "local";
	temp += i;
	const String* adr = param.getParam(temp);
	if (!adr)
	    break;
	String address;
	int port = m_transport ? m_transport->defPort() : 0;
	resolveAddress(*adr,address,port);
	SocketAddr* addr = new SocketAddr(AF_INET);
	addr->host(address);
	addr->port(port);
	o.append(addr);
    }
    SctpSocket* s = static_cast<SctpSocket*>(m_socket);
    if (!s) {
	Debug("ListenerThread",DebugGoOn,"Failed to cast socket");
	return false;
    }
    if (!s->bindx(o)) {
	Debug("ListenerThread",DebugWarn,"Failed to bindx sctp socket: %d: %s",errno,strerror(errno));
	return false;
    } else
	Debug(DebugNote,"Socket bound to %d auxiliary addresses",o.count());
    return true;
}


/** TransportThread class */

TransportThread::~TransportThread()
{
    DDebug("TransportThread",DebugAll,"Destroying TransportThread [%p]",this);
    if (m_worker)
	m_worker->resetThread();
    removeName(m_threadName);
}

void TransportThread::run()
{
    while (!m_exit) {
	bool ret = false;
	if (m_worker->needConnect())
	    ret = m_worker->connectSocket();
	else
	    ret = m_worker->readData();
	if (ret)
	    Thread::check(true);
	else
	    Thread::msleep(5,true);
    }
    if (!m_worker)
	return;
    m_worker->resetThread();
    if (m_cleanWorker)
	m_worker->reset();
    m_worker = 0;
}

TReader::~TReader()
{
   DDebug(DebugAll,"Destroying TReader [%p]",this);
}

/** TransportWorker class */

bool TransportWorker::running()
{
    Lock myLock(m_threadMutex);
    return m_thread && m_thread->running();
}

bool TransportWorker::start(Thread::Priority prio)
{
    Lock myLock(m_threadMutex);
    if (!m_thread) {
	String* name = new String(getTransportName());
	addName(name);
	m_thread = new TransportThread(this,name,prio);
    }
    if (m_thread->running() || m_thread->startup())
	return true;
    m_thread->cancel(true);
    m_thread = 0;
    return false;
}

void TransportWorker::stop()
{
    Lock myLock(m_threadMutex);
    if (!(m_thread && m_thread->running()))
	return;
    m_thread->exitThread();

    if (m_thread == Thread::current()) {
	m_thread->resetWorker();
	m_thread = 0;
	DDebug(DebugWarn,"Stopping TransportWorker from itself!! %p ", this);
	return;
    }
    myLock.drop();
    while (true) {
	Thread::msleep(1);
	if (!m_thread)
	    return;
    }
}

void TransportWorker::exitThread()
{
    Lock myLock(m_threadMutex);
    if (!m_thread)
	return;
    m_thread->exitThread();
}

/**
 * Transport class
 */

SignallingComponent* Transport::create(const String& type, NamedList& name)
{
    if (type != "SIGTransport")
	return 0;
    TempObjectCounter cnt(plugin.objectsCounter());
    Configuration cfg(Engine::configFile("sigtransport"));
    cfg.load();

    NamedString* listenNotify = name.getParam(YSTRING("listen-notify"));
    const char* sectName = name.getValue("basename");
    NamedList* config = cfg.getSection(sectName);
    if (!name.getBoolValue(YSTRING("local-config"),false))
	config = &name;
    else if (!config) {
	Debug("SIGTransport",DebugWarn,"No section %s in configuration!",sectName);
	return 0;
    } else
	name.copyParams(*config);

    if (listenNotify)
	config->setParam(listenNotify->name(),*listenNotify);
    String* mName = new String("TransportReader:");
    mName->append(*config);
    addName(mName);
    return new Transport(*config,mName);
}

Transport::Transport(const NamedList &param, String* mutexName)
    : m_reader(0), m_readerMutex(true,*mutexName), m_streamer(false), m_state(Down),
    m_listener(0), m_config(param), m_endpoint(true), m_supportEvents(true),
    m_listenNotify(true), m_mutexName(mutexName)
{
    setName("Transport:" + param);
    DDebug(this,DebugAll,"Transport created (%p)",this);
    m_listenNotify = param.getBoolValue("listen-notify",true);
}

Transport::Transport(TransportType type, String* mutexName)
    : m_reader(0), m_readerMutex(true,*mutexName), m_streamer(true), m_type(type), m_state(Down),
    m_listener(0), m_config(""), m_endpoint(true), m_supportEvents(true),
    m_listenNotify(false), m_mutexName(mutexName)
{
    DDebug(this,DebugInfo,"Creating new Transport [%p]",this);
}

Transport::~Transport()
{
    if (m_listener)
	m_listener->terminate();
    while (m_listener)
	Thread::yield();
    Debug(this,DebugAll,"Destroying Transport [%p]",this);
    TelEngine::destruct(m_reader);
    removeName(m_mutexName);
}

void Transport::destroyed()
{
    SignallingComponent::destroyed();
    m_readerMutex.lock();
    TReader* tmp = m_reader;
    m_reader = 0;
    m_readerMutex.unlock();
    TelEngine::destruct(tmp);
}

void Transport::resetReader(TReader* caller)
{
    if (!caller)
	return;
    if (m_reader) {
	m_readerMutex.lock();
	if (caller == m_reader)
	    m_reader = 0;
	m_readerMutex.unlock();
    }
    if (m_listener)
	TelEngine::destruct(caller);
}

bool Transport::control(NamedList& param)
{
    String cmp = param.getValue(YSTRING("component"));
    if (!cmp)
	return false;
    if (cmp.startsWith(YSTRING("Transport:")) && cmp != toString())
	return false;
    if (toString() != String("Transport:" + cmp))
	return false;
    String oper = param.getValue(YSTRING("operation"),YSTRING("init"));
    if (oper == YSTRING("init"))
	return TelEngine::controlReturn(&param,initialize(&param));
    else if (oper == YSTRING("add_addr")) {
	if (!m_listener) {
	    Debug(this,DebugWarn,"Unable to listen on another address, listener is missing");
	    return TelEngine::controlReturn(&param,false);
	}
	return TelEngine::controlReturn(&param,m_listener->addAddress(param));
    } else if (oper == YSTRING("reconnect")) {
	reconnect(true);
	return TelEngine::controlReturn(&param,true);
    }
    return TelEngine::controlReturn(&param,false);
}

void Transport::reconnect(bool force)
{
    Lock lock(m_readerMutex);
    if (!m_reader) {
	Debug(this,DebugWarn,"Request to reconnect but the transport is not initialized!!");
	return;
    }
    if (m_state == Up && !force) {
	Debug(this,DebugInfo,
	      "Skipped transport restart. Transport is UP and force restart was not requested.");
	return;
    }
    Debug(this,DebugInfo,"Transport reconnect requested");
    m_reader->reconnect();
}

bool Transport::getSocketParams(const String& params, NamedList& result)
{
    if (m_reader)
	return m_reader->getSocketParams(params,result);
    return false;
}

bool Transport::initialize(const NamedList* params)
{

    m_type = lookup(m_config.getValue("type","sctp"),s_transType);
    m_streamer = m_config.getBoolValue("stream",streamDefault());
    m_endpoint = m_config.getBoolValue("endpoint",false);
    if (!m_endpoint && m_streamer) {
	m_listener = new ListenerThread(this);
	if (!m_listener->init(m_config)) {
	    DDebug(this,DebugNote,"Unable to start listener");
	    return false;
	}
	m_listener->startup();
	return true;
    }
    Lock lock(m_readerMutex);
    if (m_streamer)
	m_reader = new StreamReader(this,0);
    else {
	SocketAddr addr(AF_INET);
	String address, adr = m_config.getValue("remote");
	int port = defPort();
	resolveAddress(adr,address,port);
	addr.host(address);
	addr.port(port);
	m_reader = new MessageReader(this,0,addr);
	bindSocket();
    }
    m_reader->start();
    lock.drop();
    return true;
}

bool Transport::bindSocket()
{
    bool multi = m_config.getParam("local1") != 0;
    if (multi && transType() != Transport::Sctp) {
	Debug(this,DebugWarn,"Sockets type %s do not suport multihomed",
	    lookup(transType(),s_transType));
	return false;
    }
    Socket* socket = 0;
    switch (m_type) {
	case Transport::Sctp:
	{
	    Socket* soc = 0;
	    Message m("socket.sctp");
	    SockRef* s = new SockRef(&soc);
	    m.userData(s);
	    TelEngine::destruct(s);
	    if (!(Engine::dispatch(m) && soc)) {
		Debug(this,DebugConf,"Could not obtain SctpSocket");
		return false;
	    }
	    socket = soc;
	    socket->create(AF_INET,SOCK_SEQPACKET,IPPROTO_SCTP);
	    SctpSocket* sctp = static_cast<SctpSocket*>(socket);
	    if (!sctp->setStreams(2,2))
		Debug(this,DebugInfo,"Failed to set sctp streams number");
	    if (!sctp->subscribeEvents())
		Debug(this,DebugWarn,"Unable to subscribe to Sctp events");
	    if (!sctp->setParams(m_config))
		Debug(this,DebugWarn,"Failed to set SCTP params!");
	    int ppid = sigtran() ? sigtran()->payload() : 0;
	    ppid = m_config.getIntValue("payload",ppid);
	    if (ppid > 0)
		sctp->setPayload(ppid);
	    break;
	}
	case Transport::Udp:
	    socket = new Socket();
	    socket->create(AF_INET,SOCK_DGRAM);
	    break;
	default:
	    DDebug(this,DebugWarn,"Unknown/unwanted type of socket %s",
		lookup(transType(),s_transType,"Unknown"));
    }
    if (!socket)
	return false;
    if (!socket->valid()) {
	Debug(this,DebugWarn,"Unable to create message socket: %s",
	    strerror(socket->error()));
	socket->terminate();
	delete socket;
	return false;
    }
    if (!socket->setBlocking(false)) {
	Debug(this,DebugWarn,"Unable to set message socket to nonblocking mode");
	socket->terminate();
	delete socket;
	return false;
    }
    SocketAddr addr(AF_INET);
    String address, adr = m_config.getValue("local");
    int port = defPort();
    resolveAddress(adr,address,port);
    addr.host(address);
    addr.port(port);
    if (!socket->bind(addr)) {
	Debug(this,DebugMild,"Unable to bind to %s:%u: %d: %s",
	    addr.host().c_str(),addr.port(),errno,strerror(errno));
	socket->terminate();
	delete socket;
	return false;
    } else
	DDebug(this,DebugAll,"Socket bound to %s:%u",addr.host().c_str(),addr.port());
    if (multi && !addAddress(m_config,socket)) {
	socket->terminate();
	delete socket;
	return false;
    }
    Lock lock(m_readerMutex);
    if (!m_reader)
	return false;
    m_reader->setSocket(socket);
    int linger = m_config.getIntValue("linger",0);
    socket->setLinger(linger);

    if (m_type == Sctp) {
	// Send a dummy MGMT NTFY message to create the connection
	static const unsigned char dummy[8] =
		{ 1, 0, 0, 1, 0, 0, 0, 8 };
	DataBlock hdr((void*)dummy,8,false);
	setStatus(Initiating);
	if (m_reader->sendMSG(hdr,DataBlock::empty(),1))
	    m_reader->listen(1);
	hdr.clear(false);
    } else
	setStatus(Up);
    return true;
}

bool Transport::addAddress(const NamedList &param, Socket* socket)
{
    ObjList o;
    for (int i = 1; ; i++) {
	String temp = "local";
	temp << i;
	const String* adr = param.getParam(temp);
	if (!adr)
	    break;
	String address;
	int port = defPort();
	resolveAddress(*adr,address,port);
	SocketAddr* addr = new SocketAddr(AF_INET);
	addr->host(address);
	addr->port(port);
	o.append(addr);
    }
    SctpSocket* s = static_cast<SctpSocket*>(socket);
    if (!s) {
	Debug(this,DebugGoOn,"Failed to cast socket");
	return false;
    }
    if (!s->bindx(o)) {
	Debug(this,DebugWarn,"Failed to bindx sctp socket: %d: %s",errno,strerror(errno));
	return false;
    } else
	Debug(DebugNote,"Socket bound to %d auxiliary addresses",o.count());
    return true;
}


bool Transport::connectSocket()
{
    if (!m_streamer && !m_endpoint)
	return false;
    Socket* sock = 0;
    switch (m_type){
	case Sctp :
	{
	    Message m("socket.sctp");
	    SockRef* s = new SockRef(&sock);
	    m.userData(s);
	    TelEngine::destruct(s);
	    if (!(Engine::dispatch(m) && sock)) {
		Debug(this,DebugConf,"Could not obtain SctpSocket");
		return false;
	    }
	    sock->create(AF_INET,m_streamer ? SOCK_STREAM : SOCK_SEQPACKET,IPPROTO_SCTP);
	    SctpSocket* socket = static_cast<SctpSocket*>(sock);
	    if (!socket->setStreams(2,2))
		Debug(this,DebugInfo,"Failed to set sctp streams number");
	    if (!socket->subscribeEvents()) {
		Debug(this,DebugWarn,"Unable to subscribe to Sctp events");
		m_supportEvents = false;
	    }
	    if (!socket->setParams(m_config))
		Debug(this,DebugWarn,"Failed to set SCTP params!");
	    int ppid = sigtran() ? sigtran()->payload() : 0;
	    ppid = m_config.getIntValue("payload",ppid);
	    if (ppid > 0)
		socket->setPayload(ppid);
	    break;
	}
	case Tcp :
	    sock = new Socket();
	    sock->create(AF_INET,SOCK_STREAM);
	    break;
	case Udp :
	    sock = new Socket();
	    m_streamer = false;
	    sock->create(AF_INET,SOCK_DGRAM);
	    break;
	case Unix :
	    sock = new Socket();
	    sock->create(AF_UNIX,SOCK_STREAM);
	    break;
	default:
	    DDebug(this,DebugWarn,"Unknown type of socket %s",lookup(m_type,s_transType,"Unknown"));
	    return false;
    }
    String adr = m_config.getValue("local");
    if (adr || !m_streamer) {
	SocketAddr addr(AF_INET);
	String address;
	int port = m_streamer ? 0 : defPort();
	resolveAddress(adr,address,port);
	addr.host(address);
	addr.port(port);
	if (!sock->bind(addr))
	    Debug(this,DebugWarn,"Failed to bind socket to %s:%d: %d: %s",
		address.c_str(),port,sock->error(),strerror(sock->error()));
    }
    if (!m_config.getParam("remote1")) {
	adr = m_config.getValue("remote");
	SocketAddr addr(AF_INET);
	String address;
	int port = defPort();
	resolveAddress(adr,address,port);
	addr.host(address);
	addr.port(port);
	if (m_endpoint && !sock->connect(addr)) {
	    Debug(this,DebugWarn,"Unable to connect to %s:%u: %d: %s",
		addr.host().c_str(),addr.port(),errno,strerror(errno));
	    sock->terminate();
	    delete sock;
	    return false;
	}
    }
    else {
	ObjList o;
	for (unsigned int i = 0; ; i++) {
	    String aux = "remote";
	    if (i)
		aux << i;
	    String* adr = m_config.getParam(aux);
	    if (!adr)
		break;
	    String address;
	    int port = defPort();
	    resolveAddress(*adr,address,port);
	    SocketAddr* addr = new SocketAddr(AF_INET);
	    addr->host(address);
	    addr->port(port);
	    o.append(addr);
	}
	SctpSocket* s = static_cast<SctpSocket*>(sock);
	if (!s) {
	    Debug(this,DebugGoOn,"Failed to cast socket");
	    return false;
	}
	if (!s->connectx(o)) {
	    Debug(this,DebugNote,"Failed to connectx sctp socket: %d: %s",
		errno,strerror(errno));
	    s->terminate();
	    delete s;
	    return false;
	} else
	     Debug(this,DebugNote,"Socket conected to %d addresses",o.count());
    }
    sock->setBlocking(false);
    Lock mylock(m_readerMutex);
    if (!m_reader) {
	Debug(this,DebugFail,"Connect socket null reader");
	sock->terminate();
	delete sock;
	return false;
    }
    m_reader->setSocket(sock);
    mylock.drop();
    setStatus(Up);
    return true;
}

void Transport::setStatus(int status)
{
    if (m_state == status)
	return;
    DDebug(this,DebugInfo,"State change: %s -> %s [%p]",
	lookup(m_state,s_transStatus,"?"),lookup(status,s_transStatus,"?"),this);
    m_state = status;
    Lock mylock(m_readerMutex);
    if (!m_reader)
	return;
    m_reader->m_canSend = true;
    mylock.drop();
    notifyLayer((status == Up) ? SignallingInterface::LinkUp : SignallingInterface::LinkDown);
}

u_int32_t Transport::getMsgLen(unsigned char* buf)
{
    return ((unsigned int)buf[4] << 24) | ((unsigned int)buf[5] << 16) |
	((unsigned int)buf[6] << 8) | (unsigned int)buf[7];
}


bool Transport::transmitMSG(const DataBlock& header, const DataBlock& msg, int streamId)
{
    Lock lock(m_readerMutex);
    RefPointer<TReader> reader = m_reader;
    if (!reader)
	return false;
    lock.drop();
    return reader->sendMSG(header,msg,streamId);;
}

bool Transport::addSocket(Socket* socket,SocketAddr& socketAddress)
{
    if (m_listenNotify) {
	String* name = new String("Transport:");
	name->append(socketAddress.host() + ":");
	name->append((int)socketAddress.port());
	addName(name);
	Transport* newTrans = new Transport((TransportType)m_type,name);
	if (!transportNotify(newTrans,socketAddress)) {
	    DDebug(this,DebugInfo,"New transport wasn't accepted!");
	    return false;
	}
	if (!newTrans->addSocket(socket,socketAddress)) {
	    newTrans->setStatus(Down);
	    return false;
	}
	return true;
    }
    Lock lock(m_readerMutex);
    if (transType() == Up)
	return false;
    if (m_reader) {
	StreamReader* sr = static_cast<StreamReader*>(m_reader);
	m_reader = 0;
	TelEngine::destruct(sr);
    }
    if (!m_config.c_str()) {
	m_config.assign(String(socketAddress.host().c_str()) << ":" << socketAddress.port());
	setName(m_config);
    }
    socket->setBlocking(false);
    switch (m_type) {
	case Sctp :
	{
	    Socket* sock = 0;
	    Message m("socket.sctp");
	    m.addParam("handle",String(socket->detach()));
	    delete socket;
	    SockRef* s = new SockRef(&sock);
	    m.userData(s);
	    TelEngine::destruct(s);
	    if (!(Engine::dispatch(m) && sock)) {
		DDebug(this,DebugNote,"Could not obtain SctpSocket");
		return false;
	    }
	    SctpSocket* soc = static_cast<SctpSocket*>(sock);
	    if (!soc->setStreams(2,2))
		DDebug(this,DebugInfo,"Sctp set Streams failed");
	    if (!soc->subscribeEvents()) {
		DDebug(this,DebugInfo,"Sctp subscribe events failed");
		m_supportEvents = false;
	    }
	    if (!soc->setParams(m_config))
		Debug(this,DebugWarn,"Failed to set SCTP params!");
	    int ppid = sigtran() ? sigtran()->payload() : 0;
	    ppid = m_config.getIntValue("payload",ppid);
	    if (ppid > 0)
		soc->setPayload(ppid);
	    soc->setBlocking(false);
	    if (m_streamer)
		m_reader = new StreamReader(this,soc);
	    else
		Debug(this,DebugStub,"Add socket requested to create sctp message reader!");
	    break;
	}
	case Unix :
	case Tcp :
	    m_reader = new StreamReader(this,socket);
	    break;
	case Udp :
	    Debug(this,DebugStub,"Add socket requested to create message reader for UDP socket type!");
	    break;
	default:
	    Debug(this,DebugWarn,"Unknown socket type %d",m_type);
	    return false;
    }
    setStatus(Up);
    m_reader->start();
    return true;
}

bool Transport::hasThread()
{
    Lock lock(m_readerMutex);
    if (!m_reader)
	return false;
    return m_reader->hasThread();
}

void Transport::stopThread()
{
    Lock lock(m_readerMutex);
    if (m_reader)
	m_reader->exitThread();
}


/**
 * class StreamReader
 */

StreamReader::StreamReader(Transport* transport,Socket* sock)
    : m_transport(transport), m_socket(sock), m_headerLen(8), m_totalPacketLen(0)
{
     DDebug(transport,DebugAll,"Creating StreamReader (%p,%p) [%p]",transport,sock,this);
}

StreamReader::~StreamReader()
{
    stop();
    if (m_socket) {
	m_socket->terminate();
	delete m_socket;
    }
    DDebug(m_transport,DebugAll,"Destroying StreamReader [%p]",this);
}

void StreamReader::setSocket(Socket* s)
{
    if (!s || s == m_socket)
	return;
    m_reconnect = false;
    Socket* temp = m_socket;
    m_socket = s;
    if (temp) {
	temp->terminate();
	delete temp;
    }
}

bool StreamReader::sendMSG(const DataBlock& header, const DataBlock& msg, int streamId)
{
    Lock mylock(m_sending);
    if (!m_canSend) {
	DDebug(m_transport,DebugNote,"Cannot send message at this time");
	return false;
    }
    bool ret = false;
    if (((m_sendBuffer.length() + msg.length()) + header.length()) < MAX_BUF_SIZE) {
	m_sendBuffer += header;
	m_sendBuffer += msg;
	ret = true;
    }
    else
	Debug(m_transport,DebugWarn,"Buffer Overrun");
    mylock.drop();
    return sendBuffer(streamId) && ret;
}

bool StreamReader::sendBuffer(int streamId)
{
    Lock mylock(m_sending);
    if (!m_canSend) {
	DDebug(m_transport,DebugNote,"Cannot send message at this time");
	return false;
    }
    if (!m_socket)
	return needConnect();
    if (m_sendBuffer.null())
	return true;
    bool sendOk = false, error = false;
    if (!m_socket->select(0,&sendOk,&error,Thread::idleUsec())) {
	DDebug(m_transport,DebugAll,"Select error detected. %s",strerror(errno));
	return false;
    }
    if (error) {
	if (m_socket->updateError() && !m_socket->canRetry()) {
	    m_reconnect = true;
	    m_canSend = false;
	}
	return false;
    }
    if (!sendOk)
	return true;
    int len = 0;
    if (m_transport->transType() == Transport::Sctp) {
	SctpSocket* s = static_cast<SctpSocket*>(m_socket);
	if (!s) {
	    Debug(m_transport,DebugGoOn,"Sctp conversion failed");
	    return false;
	}
	if (m_transport->status() == Transport::Up)
	    if (!s->valid()) {
		m_reconnect = true;
		m_canSend = false;
		return false;
	    }
	int flags = 0;
	len = s->sendMsg(m_sendBuffer.data(),m_sendBuffer.length(),streamId,flags);
    }
    else
	len = m_socket->send(m_sendBuffer.data(),m_sendBuffer.length());
    if (len <= 0) {
	if (!m_socket->canRetry()) {
	    Debug(m_transport,DebugMild,"Send error detected. %s",strerror(errno));
	    m_reconnect = true;
	    m_canSend = false;
	}
	return false;
    }
    m_sendBuffer.cut(-len);
    return true;
}

bool StreamReader::connectSocket()
{
    Time t;
    if (t.usec() < m_tryAgain && !m_reconnect) {
	Thread::yield(true);
	return false;
    }
    if (m_reconnect) {
	m_reconnect = false;
	m_interval = CONN_RETRY_MIN;
    }
    m_tryAgain = t + m_interval;
    // exponential backoff
    m_interval *= 2;
    if (m_interval > CONN_RETRY_MAX)
	m_interval = CONN_RETRY_MAX;
    if (m_transport->connectSocket()) {
	m_decrease = t.usec() + DECREASE_INTERVAL;
	return true;
    }
    return false;
}

bool StreamReader::readData()
{
    Lock myLock(m_sending,SignallingEngine::maxLockWait());
    if (!(myLock.locked() && m_socket))
	return false;
    if (m_reconnect) {
	connectionDown(false);
	myLock.drop();
	stopThread();
	return false;
    }
    if (m_interval > CONN_RETRY_MIN) {
	Time t;
	if (t.usec() > m_decrease) {
	    if (((int64_t)m_interval - DECREASE_AMOUNT) > CONN_RETRY_MIN)
		m_interval -= DECREASE_AMOUNT;
	    else
		m_interval = CONN_RETRY_MIN;
	    m_decrease = t.usec() + DECREASE_INTERVAL;
	}
    }
    sendBuffer();
    if (!m_socket)
	return false;
    myLock.drop();
    int stream = 0, len = 0;
    SocketAddr addr;
    unsigned char buf[MAX_BUF_SIZE];
    if (m_headerBuffer.length() < 8) {
	int flags = 0;
	if (m_transport->transType() == Transport::Sctp) {
	    SctpSocket* s = static_cast<SctpSocket*>(m_socket);
	    if (!s) {
		Debug(m_transport,DebugGoOn,"Sctp conversion failed");
		return false;
	    }
	    len = s->recvMsg((void*)buf,m_headerLen,addr,stream,flags);
	    if (flags) {
		if (flags == 2) {
		    Debug(m_transport,DebugInfo,"Sctp commUp");
		    m_transport->setStatus(Transport::Up);
		    return true;
		}
		connectionDown();
		return false;
	    }
	}
	else {
	    SocketAddr addr;
	    len = m_socket->recv((void*)buf,m_headerLen);
	}
	if (len == 0) {
	    connectionDown();
	    return false;
	}
	if (len < 0) {
	    if (!m_socket->canRetry())
		connectionDown();
	    return false;
	}
	m_headerLen -= len;
	m_headerBuffer.append(buf,len);
	if (m_headerLen > 0)
	    return true;
	unsigned char* auxBuf = (unsigned char*)m_headerBuffer.data();
	m_totalPacketLen = m_transport->getMsgLen(auxBuf);
	if (m_totalPacketLen >= 8 && m_totalPacketLen < MAX_BUF_SIZE) {
	    m_totalPacketLen -= 8;
	    XDebug(m_transport,DebugAll,"Expecting %d bytes of packet data %d",m_totalPacketLen,stream);
	    if (!m_totalPacketLen) {
		m_transport->setStatus(Transport::Up);
		m_transport->processMSG(m_transport->getVersion((unsigned char*)m_headerBuffer.data()),
		    m_transport->getClass((unsigned char*)m_headerBuffer.data()),
		    m_transport->getType((unsigned char*)m_headerBuffer.data()),
		    DataBlock::empty(),stream);
		m_headerLen = 8;
		m_headerBuffer.clear(true);
	    }
	}
	else {
	    DDebug(m_transport,DebugWarn,"Protocol error - unsupported length of packet %d!",
		m_totalPacketLen);
	    return false;
	}
    }
    unsigned char buf1[MAX_BUF_SIZE];
    if (m_totalPacketLen > 0) {
	len = 0;
	int flags = 0;
	if (m_transport->transType() == Transport::Sctp) {
	    SctpSocket* s = static_cast<SctpSocket*>(m_socket);
	    if (!s) {
		Debug(m_transport,DebugGoOn,"Sctp conversion failed");
		return false;
	    }
	    len = s->recvMsg((void*)buf1,m_totalPacketLen,addr,stream,flags);
	    if (flags && flags != 2) {
		connectionDown();
		return false;
	    }
	}
	else {
	    SocketAddr addr;
	    len = m_socket->recv((void*)buf1,m_totalPacketLen);
	}
	if (len == 0) {
	    if (!m_transport->supportEvents())
		connectionDown();
	    return false;
	}
	if (len < 0) {
	    if (!m_socket->canRetry())
		connectionDown();
	    return false;
	}
	m_transport->setStatus(Transport::Up);
	m_totalPacketLen -= len;
	m_readBuffer.append(buf1,len);
	if (m_totalPacketLen > 0)
	    return true;
	m_transport->processMSG(m_transport->getVersion((unsigned char*)m_headerBuffer.data()),
	    m_transport->getClass((unsigned char*)m_headerBuffer.data()),m_transport->getType((unsigned char*)
	    m_headerBuffer.data()), m_readBuffer,stream);
	m_totalPacketLen = 0;
	m_readBuffer.clear(true);
	m_headerLen = 8;
	m_headerBuffer.clear(true);
	return true;
    }
    return false;
}

void StreamReader::connectionDown(bool stopTh) {
    Debug(m_transport,DebugMild,"Connection down [%p]",m_socket);
    while (!m_sending.lock(Thread::idleUsec()))
	Thread::yield();
    m_canSend = false;
    m_sendBuffer.clear();
    if (m_socket) {
	m_socket->terminate();
	delete m_socket;
	m_socket = 0;
    }
    m_sending.unlock();

    if (stopTh) {
	m_transport->setStatus(Transport::Down);
	stopThread();
    }
}

bool StreamReader::getSocketParams(const String& params, NamedList& result)
{
    Lock reconLock(m_sending,SignallingEngine::maxLockWait());
    if (!(reconLock.locked() && m_socket))
	return false;
    m_socket->getParams(params,result);
    return true;
}

void StreamReader::stopThread()
{
    m_transport->setStatus(Transport::Down);
    if (!m_transport->listen())
	return;
    stop();
}

/**
 * class MessageReader
 */

MessageReader::MessageReader(Transport* transport, Socket* sock, SocketAddr& addr)
    : m_transport(transport), m_socket(sock), m_remote(addr),
      m_reconnectInterval(s_maxDownAllowed),m_reconnectTryAgain(0)
{
    DDebug(DebugAll,"Creating MessageReader [%p]",this);
}

MessageReader::~MessageReader()
{
    stop();
    if (m_socket) {
	m_socket->terminate();
	delete m_socket;
    }
    DDebug(DebugAll,"Destroying MessageReader [%p]",this);
}

void MessageReader::setSocket(Socket* s)
{
    if (!s || s == m_socket)
	return;
    m_reconnect = false;
    if (m_socket) {
	m_socket->terminate();
	delete m_socket;
    }
    m_socket = s;
}

bool MessageReader::bindSocket()
{
    Time t;
    if (t < m_tryAgain) {
	Thread::yield(true);
	return false;
    }
    Lock mylock(m_sending);
    if (m_transport->bindSocket()) {
	m_interval = CONN_RETRY_MIN;
	return true;
    }
    m_tryAgain = Time::now() + m_interval;
    // exponential backoff
    m_interval *= 2;
    if (m_interval > CONN_RETRY_MAX)
	m_interval = CONN_RETRY_MAX;
    return false;
}


void MessageReader::reconnectSocket()
{
    u_int64_t t = Time::now();
    if (t < m_reconnectTryAgain) {
	Thread::yield(true);
	return;
    }
    m_reconnect = true;
    m_reconnectTryAgain = t + m_reconnectInterval;
    // exponential backoff
    m_reconnectInterval *= 2;
    if (m_reconnectInterval > CONN_RETRY_MAX)
	m_reconnectInterval = CONN_RETRY_MAX;
}

bool MessageReader::sendMSG(const DataBlock& header, const DataBlock& msg, int streamId)
{
    if (!m_canSend) {
	DDebug(m_transport,DebugNote,"Cannot send message at this time");
	return false;
    }
    bool sendOk = false, error = false;
    bool ret = false;
    Lock mylock(m_sending);
    while (m_socket && m_socket->select(0,&sendOk,&error,Thread::idleUsec())) {
	if (error) {
	    DDebug(m_transport,DebugAll,"Send error detected. %s",strerror(errno));
	    mylock.drop();
	    updateTransportStatus(Transport::Down);
	    break;
	}
	if (!sendOk)
	    break;
	int totalLen = header.length() + msg.length();
	if (!totalLen) {
	    ret = true;
	    break;
	}
	DataBlock buf(header);
	buf += msg;
#ifdef XDEBUG
	String aux;
	aux.hexify(buf.data(),buf.length(),' ');
	Debug(m_transport,DebugInfo,"Sending: %s",aux.c_str());
#endif
	int len = 0;
	if (m_transport->transType() == Transport::Sctp) {
	    SctpSocket* s = static_cast<SctpSocket*>(m_socket);
	    if (!s) {
		Debug(m_transport,DebugGoOn,"Sctp conversion failed");
		break;
	    }
	    int flags = 0;
	    len = s->sendTo(buf.data(),totalLen,streamId,m_remote,flags);
	}
	else
	    len = m_socket->sendTo(buf.data(),totalLen,m_remote);
	if (len == totalLen) {
	    ret = true;
	    break;
	}
	DDebug(m_transport,DebugMild,"Error sending message %d %d %s %s %d",len,totalLen,strerror(errno),m_remote.host().c_str(),m_remote.port());
	break;
    }
    return ret;
}

bool MessageReader::readData()
{
    Lock reconLock(m_sending,SignallingEngine::maxLockWait());
    // If m_socket is null We are already reconnecting
    if (m_socket && m_reconnect) {
	if (!reconLock.locked())
	    return false;
	if (m_downTime != 0) {
	    int sec = (int)((Time::now() - m_downTime) / 1000000); // usec to sec
	    Debug(m_transport,DebugNote,"Reconnecting sctp socket! is down for %d seconds.",sec);
	}
	m_socket->terminate();
	delete m_socket;
	m_socket = 0;
	reconLock.drop();
	updateTransportStatus(Transport::Initiating);
	return false;
    }
    reconLock.drop();
    if (!m_socket && !bindSocket())
	return false;
    bool readOk = false,error = false;
    if (!(running() && m_socket))
	return false;
    if (!m_socket->select(&readOk,0,&error,Thread::idleUsec())) {
	if (m_transport->status() == Transport::Initiating)
	    reconnectSocket();
	return false;
    }
    if ((!readOk || error)) {
	if (!readOk && m_transport->status() == Transport::Initiating)
	    reconnectSocket();
	return false;
    }

    unsigned char buffer[MAX_BUF_SIZE];
    int stream = 0;
    int r = 0;
    SocketAddr addr;
    if (m_transport->transType() == Transport::Sctp) {
	if (m_transport->status() == Transport::Initiating) {
	    Time t;
	    if (t < m_tryAgain) {
		Thread::yield(true);
		return false;
	    }
	    m_tryAgain = t + m_interval;
	}
	int flags = 0;
	SctpSocket* s = static_cast<SctpSocket*>(m_socket);
	if (!s) {
	    Debug(m_transport,DebugGoOn,"Sctp conversion failed");
	    return false;
	}
	SocketAddr addr;
	r = s->recvMsg((void*)buffer,MAX_BUF_SIZE,addr,stream,flags);
	if (flags) {
	     if (flags == 2) {
		DDebug(m_transport,DebugAll,"Sctp connection is Up");
		updateTransportStatus(Transport::Up);
		return true;
	    }
	    DDebug(m_transport,DebugNote,"Message error [%p] %d",m_socket,flags);
	    if (m_transport->status() != Transport::Up)
		return false;
	    updateTransportStatus(Transport::Initiating);
	    Lock lock(m_sending);
	    Debug(m_transport,DebugInfo,"Terminating socket [%p] Reason: connection down!",m_socket);
	    m_socket->terminate();
	    delete m_socket;
	    m_socket = 0;
	    return false;
	}
    }
    else
	r = m_socket->recvFrom((void*)buffer,MAX_BUF_SIZE,addr);
    if (r <= 0)
	return false;
    m_interval = CONN_RETRY_MIN;
    m_reconnectInterval = s_maxDownAllowed;

    u_int32_t len = m_transport->getMsgLen(buffer);
    if ((unsigned int)r != len) {
	Debug(m_transport,DebugNote,"Protocol read error read: %d, expected %d",r,len);
	return false;
    }
    updateTransportStatus(Transport::Up);
    DataBlock packet(buffer,r);
    packet.cut(-8);
    m_transport->processMSG(m_transport->getVersion((unsigned char*)buffer),
	m_transport->getClass((unsigned char*)buffer), m_transport->getType((unsigned char*)buffer),packet,stream);
    return true;
}

bool MessageReader::getSocketParams(const String& params, NamedList& result)
{
    Lock reconLock(m_sending,SignallingEngine::maxLockWait());
    if (!(reconLock.locked() && m_socket))
	return false;
    m_socket->getParams(params,result);
    return true;
}

void MessageReader::updateTransportStatus(int status)
{
    if (status == Transport::Up)
	m_downTime = 0;
    else if (m_downTime == 0) {
	m_downTime = Time::now();
	m_reconnectTryAgain = m_downTime + m_reconnectInterval;
    }
    m_transport->setStatus(status);
}

/**
 * class TransportModule
 */

TransportModule::TransportModule()
    : Module("sigtransport","misc",true),
      m_init(false)
{
    Output("Loaded module SigTransport");
}

TransportModule::~TransportModule()
{
    Output("Unloading module SigTransport");
}

void TransportModule::initialize()
{
    Output("Initializing module SigTransport");
    Configuration cfg(Engine::configFile("sigtransport"));
    cfg.load();
    s_maxDownAllowed = cfg.getIntValue(YSTRING("general"),YSTRING("max_down"),10);
    s_maxDownAllowed *=1000000;
    if (!m_init) {
	m_init = true;
	setup();
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
