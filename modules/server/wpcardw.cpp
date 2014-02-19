/**
 * wpcardw.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Wanpipe PRI cards signalling and data driver for Windows
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

#include <yatephone.h>
#include <yatesig.h>

#ifndef _WINDOWS
#error This module is only for Windows
#else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <sang_api.h>
#include <sang_status_defines.h>

#define WP_HEADER ((int)sizeof(api_header_t))

#define MAX_READ_ERRORS 250              // WpSpan::run(): Display read error message
#define WPSOCKET_SELECT_TIMEOUT 125      // Value used in WpSocket::select() to timeout

using namespace TelEngine;
namespace { // anonymous

class Fifo;                              // Circular queue for data consumer
class WpSocket;                          // I/O for D and B channels
class WpInterface;                       // Wanpipe D-channel (SignallingInterface)
class WpSigThread;                       // D-channel read data
class WpSource;                          // Data source
class WpConsumer;                        // Data consumer
class WpCircuit;                         // Single Wanpipe B-channel (SignallingCircuit)
class WpSpan;                            // Wanpipe span B-channel group
class WpSpanThread;                      // B-channel group read/write data
class WpModule;                          // The driver

// Implements a circular queue for data consumer
class Fifo
{
public:
    inline Fifo(unsigned int buflen)
	: m_mutex(true), m_buffer(0,buflen), m_head(0), m_tail(1)
	{}
    inline void clear() {
	    m_head = 0;
	    m_tail = 1;
	}
    // Put a byte in fifo, overwrite last byte if full
    // Return false on buffer overrun
    bool put(unsigned char value);
    // Put data buffer in fifo, one byte at a time
    // Return the number of overwritten bytes
    unsigned int put(const unsigned char* buf, unsigned int length);
    // Get a byte from fifo, return last read if empty
    unsigned char get();
protected:
    unsigned char& operator[](unsigned int index)
	{ return ((unsigned char*)m_buffer.data())[index]; }
private:
    Mutex m_mutex;
    DataBlock m_buffer;
    unsigned int m_head;
    unsigned int m_tail;
};

// I/O socket for WpInterface and WpSpan
class WpSocket
{
public:
    WpSocket(DebugEnabler* dbg, const char* card = 0, const char* device = 0);
    inline ~WpSocket()
	{ close(); }
    inline bool valid() const
	{ return m_fd != INVALID_HANDLE_VALUE; }
    inline const String& card() const
	{ return m_card; }
    inline const String& device() const
	{ return m_device; }
    inline void card(const char* name)
	{ m_card = name; }
    inline void device(const char* name)
	{ m_device = name; }
    inline bool canRead() const
	{ return m_canRead; }
    // Open socket. Return false on failure
    bool open();
    // Close socket
    void close();
    // Read data. Return -1 on failure
    int recv(void* buffer, int len);
    // Send data. Return -1 on failure
    int send(void* buffer, int len);
    // Check socket. Set flags to the appropriate values on success
    // Return false on failure
    bool select(unsigned int multiplier);
    // Update the state of the link and return true if changed
    bool updateLinkStatus();
protected:
    inline void showError(const char* action, const char* info = 0, int level = DebugWarn) {
	    Debug(m_dbg,level,"WpSocket(%s_%s). %s failed%s. Code %d [%p]",
		m_card.c_str(),m_device.c_str(),action,c_safe(info),
		m_error,this);
	}
private:
    DebugEnabler* m_dbg;                 // Debug enabler owning this socket
    HANDLE m_fd;                         // The device handle
    int m_error;                         // Last error code
    String m_card;                       // Card name used to open socket
    String m_device;                     // Device name used to open socket
    bool m_canRead;                      // Set by select(). Can read from socket
    bool m_readError;                    // Flag used to print read errors
    bool m_writeError;                   // Flag used to print write errors
    bool m_selectError;                  // Flag used to print select errors
};

// Wanpipe D-channel
class WpInterface : public SignallingInterface
{
    friend class WpSigThread;
public:
    // Create an instance of WpInterface or WpSpan
    static void* create(const String& type, NamedList& name);
    WpInterface(const NamedList& params);
    virtual ~WpInterface();
    // Initialize interface. Return false on failure
    bool init(const NamedList& config, NamedList& params);
    // Remove links. Dispose memory
    virtual void destruct()
	{ cleanup(true); }
    // Send signalling packet
    virtual bool transmitPacket(const DataBlock& packet, bool repeat, PacketType type);
    // Interface control
    virtual bool control(Operation oper, NamedList* params);
protected:
    virtual void timerTick(const Time& when);
    // Read data from socket
    bool receiveAttempt();
private:
    inline void cleanup(bool release) {
	    control(Disable,0);
	    attach(0);
	    if (release)
		RefObject::destruct();
	}
    WpSocket m_socket;
    WpSigThread* m_thread;               // Thread used to read data from socket
    bool m_readOnly;                     // Readonly interface
    int m_notify;                        // Upper layer notification on received data (0: success. 1: not notified. 2: notified)
    bool m_sendReadOnly;                 // Print send attempt on readonly interface error
    SignallingTimer m_timerRxUnder;      // RX underrun notification
};

// Read signalling data for WpInterface
class WpSigThread : public Thread
{
    friend class WpInterface;
public:
    inline WpSigThread(WpInterface* iface, Priority prio = Normal)
	: Thread("WpInterfaceThread",prio), m_interface(iface)
	{}
    virtual ~WpSigThread();
    virtual void run();
private:
    WpInterface* m_interface;
};

// Wanpipe data source
class WpSource : public DataSource
{
    friend class WpCircuit;
public:
    WpSource(WpCircuit* owner, const char* format, unsigned int bufsize);
    virtual ~WpSource();
    inline void changeFormat(const char* format)
	{ m_format = format; }
    // Add a byte to the source buffer
    void put(unsigned char c);
protected:
    WpCircuit* m_owner;                  // B-channel owning this source
    DataBlock m_buffer;                  // Data buffer
    unsigned int m_bufpos;               // First free byte's index
    unsigned int m_total;
};

// Wanpipe data consumer
class WpConsumer : public DataConsumer, public Fifo
{
    friend class WpCircuit;
public:
    WpConsumer(WpCircuit* owner, const char* format, unsigned int bufsize);
    virtual ~WpConsumer();
    inline void changeFormat(const char* format)
	{ m_format = format; }
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags);
protected:
    WpCircuit* m_owner;                  // B-channel owning this consumer
    u_int32_t m_errorCount;              // The number of times the fifo was full
    u_int32_t m_errorBytes;              // The number of overwritten bytes in one session
    unsigned int m_total;
};

// Single Wanpipe B-channel
class WpCircuit : public SignallingCircuit
{
public:
    WpCircuit(unsigned int code, SignallingCircuitGroup* group, WpSpan* data,
	unsigned int buflen, unsigned int channel);
    // Get circuit channel number inside its span
    unsigned int channel() const
	{ return m_channel; }
    virtual ~WpCircuit();
    virtual bool status(Status newStat, bool sync = false);
    virtual bool updateFormat(const char* format, int direction);
    virtual void* getObject(const String& name) const;
    inline WpSource* source()
	{ return m_sourceValid; }
    inline WpConsumer* consumer()
	{ return m_consumerValid; }
    // Enqueue received events
    bool enqueueEvent(SignallingCircuitEvent* e);
private:
    Mutex m_mutex;
    unsigned int m_channel;              // Channel number inside span
    WpSource* m_sourceValid;             // Circuit's source if reserved, otherwise: 0
    WpConsumer* m_consumerValid;         // Circuit's consumer if reserved, otherwise: 0
    WpSource* m_source;
    WpConsumer* m_consumer;
};

// Wanpipe B-channel group
class WpSpan : public SignallingCircuitSpan
{
    friend class WpSpanThread;
public:
    WpSpan(const NamedList& params);
    virtual ~WpSpan();
    // Initialize data channel span. Return false on failure
    bool init(const NamedList& config, const NamedList& defaults, NamedList& params);
    // Swap data if necessary
    inline unsigned char swap(unsigned char c)
	{ return m_swap ? s_bitswap[c] : c; }
    // Data processor
    // Read events and data from socket. Send data when succesfully read
    // Received data is splitted for each circuit
    // Sent data from each circuit is merged into one data block
    void run();
    // Find a circuit by channel
    WpCircuit* find(unsigned int channel);
protected:
    // Create circuits (all or nothing)
    // delta: number to add to each circuit code
    // cicList: Circuits to create
    bool createCircuits(unsigned int delta, const String& cicList);
    // Read data from socket. Check for errors or in-band events
    // Return -1 on error
    int readData();
    // Decode received event
    bool decodeEvent();
    // Swapped bits table
    static unsigned char s_bitswap[256];
private:
    WpSocket m_socket;
    WpSpanThread* m_thread;
    bool m_canSend;                      // Can send data (not a readonly span)
    bool m_swap;                         // Swap bits flag
    unsigned int m_chans;                // Total number of circuits for this span
    unsigned int m_count;                // Circuit count
    unsigned int m_first;                // First circuit code
    unsigned int m_samples;              // Sample count
    unsigned char m_noData;              // Value to send when no data
    unsigned int m_buflen;               // Buffer length for sources/consumers
    // Used for data processing
    WpCircuit** m_circuits;              // The circuits belonging to this span
    unsigned int m_readErrors;           // Count data read errors
    TX_RX_DATA_STRUCT m_buffer;          // I/O data buffer
};

// B-channel group read/write data
class WpSpanThread : public Thread
{
    friend class WpSpan;
public:
    inline WpSpanThread(WpSpan* data, Priority prio = Normal)
	: Thread("WpSpanThread",prio), m_data(data)
	{}
    virtual ~WpSpanThread();
    virtual void run();
private:
    WpSpan* m_data;
};

// The module
class WpModule : public Module
{
public:
    WpModule();
    ~WpModule();
    virtual void initialize();
private:
    bool m_init;
};

YSIGFACTORY2(WpInterface);
static Mutex s_ifaceNotify(true);        // WpInterface: lock recv data notification counter
static WpModule driver;


/**
 * Fifo
 */
bool Fifo::put(unsigned char value)
{
    (*this)[m_tail] = value;
    bool full = (m_head == m_tail);
    m_tail++;
    if (m_tail >= m_buffer.length())
	m_tail = 0;
    if (full)
	m_head = m_tail;
    return full;
}

unsigned int Fifo::put(const unsigned char* buf, unsigned int length)
{
    Lock lock(m_mutex);
    unsigned int errors = 0;
    while (length--)
	if (put(*buf++))
	    errors++;
    return errors;
}

unsigned char Fifo::get()
{
    Lock lock(m_mutex);
    unsigned char tmp = (*this)[m_head];
    unsigned int nh = m_head + 1;
    if (nh >= m_buffer.length())
	nh = 0;
    if (nh != m_tail)
	m_head = nh;
    return tmp;
}

/**
 * WpSocket
 */
WpSocket::WpSocket(DebugEnabler* dbg, const char* card, const char* device)
    : m_dbg(dbg),
    m_fd(INVALID_HANDLE_VALUE),
    m_card(card),
    m_device(device),
    m_canRead(false),
    m_readError(false),
    m_writeError(false),
    m_selectError(false)
{
}


// Open socket
bool WpSocket::open()
{
    DDebug(m_dbg,DebugAll,
	"WpSocket::open(). Card: '%s'. Device: '%s' [%p]",
	m_card.c_str(),m_device.c_str(),this);
    String devname("\\\\.\\");
    devname << m_card << "_" << m_device;
    m_fd = ::CreateFile(devname,
	GENERIC_READ|GENERIC_WRITE,
	FILE_SHARE_READ|FILE_SHARE_WRITE,
	0,
	OPEN_EXISTING,
	FILE_FLAG_NO_BUFFERING|FILE_FLAG_WRITE_THROUGH,
	0);
    if (m_fd == INVALID_HANDLE_VALUE) {
	m_error = ::GetLastError();
	showError("Open");
	return false;
    }
    return true;
}

// Close socket
void WpSocket::close()
{
    if (m_fd == INVALID_HANDLE_VALUE)
	return;
    DDebug(m_dbg,DebugAll,"WpSocket::close(). Card: '%s'. Device: '%s' [%p]",
	m_card.c_str(),m_device.c_str(),this);
    ::CloseHandle(m_fd);
}

// Read data from socket
int WpSocket::recv(void* buffer, int len)
{
    int r = 0;
    if (DeviceIoControl(m_fd,IoctlReadCommand,0,0,buffer,len,(LPDWORD)&r,0)) {
	m_readError = false;
	return r;
    }
    m_error = ::GetLastError();
    showError("Read");
    m_readError = true;
    return -1;
}

// Write data to socket
int WpSocket::send(void* buffer, int len)
{
    int w = 0;
    if (DeviceIoControl(m_fd,IoctlWriteCommand,buffer,len,buffer,len,(LPDWORD)&w,0)) {
	if (w == len) {
	    m_writeError = false;
	    return w;
	}
	m_error = 0;
    }
    else {
	w = 0;
	m_error = ::GetLastError();
    }
    String info;
    info << " (Sent " << w << " instead of " << len << ')';
    showError("Send",info);
    m_writeError = true;
    return -1;
}

// Check events and socket availability
bool WpSocket::select(unsigned int multiplier)
{
    m_canRead = false;
    API_POLL_STRUCT apiPoll;
    apiPoll.operation_status = 0;
    apiPoll.poll_events_bitmap = 0;
    apiPoll.user_flags_bitmap = POLLIN;
    apiPoll.timeout = (multiplier * WPSOCKET_SELECT_TIMEOUT) / 1000;
    if (0 == apiPoll.timeout)
	apiPoll.timeout = 1;

    int sz = 0;
    if (DeviceIoControl(m_fd,IoctlApiPoll,0,0,&apiPoll,sizeof(apiPoll),(LPDWORD)&sz,0)) {
Output("HERE - 1");
	m_canRead = (apiPoll.poll_events_bitmap & POLL_EVENT_RX_DATA) != 0;
	m_selectError = false;
	return true;
    }

    if (m_selectError)
	return false;
    m_error = ::GetLastError();
    showError("Select");
    m_selectError = true;
    return false;
}


/**
 * WpInterface
 */
// Create WpInterface or WpSpan
void* WpInterface::create(const String& type, NamedList& name)
{
    bool iface = false;
    if (type == "sig")
	iface = true;
    else  if (type == "voice")
	;
    else
	return 0;

    TempObjectCounter cnt(driver.objectsCounter());
    Configuration cfg(Engine::configFile("wpcard"));
    cfg.load();
    const char* sectName = name.getValue(type);
    DDebug(&driver,DebugAll,"Factory trying to create %s='%s'",type.c_str(),sectName);
    NamedList* config = cfg.getSection(sectName);

    if (!name.getBoolValue(YSTRING("local-config"),false))
	config = &name;
    else if (!config) {
	Debug(&plugin,DebugConf,"No section '%s' in configuration",c_safe(sectName));
	return 0;
    } else
	name.copyParams(*config);

    if (iface) {
	WpInterface* iface = new WpInterface(name);
	if (iface->init(*config,(NamedList&)name))
	    return iface;
	TelEngine::destruct(iface);
	return 0;
    }
    NamedList* general = cfg.getSection("general");
    NamedList dummy("general");
    WpSpan* data = new WpSpan(name);
    if (data->init(*config,general?*general:dummy,(NamedList&)name))
	return data;
    TelEngine::destruct(data);
    return 0;
}

WpInterface::WpInterface(const NamedList& params)
    : m_socket(this),
    m_thread(0),
    m_readOnly(false),
    m_notify(0),
    m_sendReadOnly(false),
    m_timerRxUnder(0)
{
    setName(params.getValue("debugname","WpInterface"));
    XDebug(this,DebugAll,"WpInterface::WpInterface() [%p]",this);
}

WpInterface::~WpInterface()
{
    cleanup(false);
    XDebug(this,DebugAll,"WpInterface::~WpInterface() [%p]",this);
}

bool WpInterface::init(const NamedList& config, NamedList& params)
{
    TempObjectCounter cnt(driver.objectsCounter());
    // Set socket card / device
    m_socket.card(config);
    const char* sig = config.getValue("siggroup");
    if (!(sig && *sig)) {
	Debug(this,DebugWarn,
	    "Missing or invalid siggroup='%s' in configuration [%p]",
	    c_safe(sig),this);
	return false;
    }
    m_socket.device(sig);

    m_readOnly = params.getBoolValue("readonly",config.getBoolValue("readonly",false));

    int rx = params.getIntValue("rxunderruninterval");
    if (rx > 0)
	m_timerRxUnder.interval(rx);

    if (debugAt(DebugInfo)) {
	String s;
	s << "driver=" << driver.debugName();
	s << " section=" << config.c_str();
	s << " type=" << config.getValue("type","T1");
	s << " card=" << m_socket.card();
	s << " device=" << m_socket.device();
	s << " readonly=" << String::boolText(m_readOnly);
	s << " rxunderruninterval=" << (unsigned int)m_timerRxUnder.interval() << "ms";
	Debug(this,DebugInfo,"D-channel: %s [%p]",s.c_str(),this);
    }
    return true;
}

// Send signalling packet
bool WpInterface::transmitPacket(const DataBlock& packet, bool repeat, PacketType type)
{
    if (m_readOnly) {
	if (!m_sendReadOnly)
	    Debug(this,DebugWarn,"Attempt to send data on read only interface");
	m_sendReadOnly = true;
	return false;
    }

    if (!m_socket.valid())
	return false;

#ifdef XDEBUG
    if (debugAt(DebugAll)) {
	String str;
	str.hexify(packet.data(),packet.length(),' ');
	Debug(this,DebugAll,"Sending %u bytes: %s",packet.length(),str.c_str());
    }
#endif

    TX_DATA_STRUCT buffer;
    ::memcpy(buffer.data,packet.data(),packet.length());
    buffer.api_header.data_length = packet.length();
    buffer.api_header.operation_status = SANG_STATUS_TX_TIMEOUT;

    return -1 != m_socket.send(&buffer,sizeof(buffer));
}

// Receive signalling packet
bool WpInterface::receiveAttempt()
{
    if (!m_socket.valid())
	return false;
    if (!m_socket.select(5))
	return false;
    RX_DATA_STRUCT buffer;
    buffer.api_header.operation_status = SANG_STATUS_RX_DATA_TIMEOUT;
    int r = m_socket.recv(&buffer,sizeof(buffer));
    if (r == -1)
	return false;
    if (r > WP_HEADER) {
	XDebug(this,DebugAll,"Received %d bytes packet. Header length is %u [%p]",
	    r,WP_HEADER,this);
	r -= WP_HEADER;
	if (SANG_STATUS_SUCCESS != buffer.api_header.operation_status) {
	    DDebug(this,DebugWarn,"Packet got error: %u (%s) [%p]",
		buffer.api_header.operation_status,SDLA_DECODE_SANG_STATUS(buffer.api_header.operation_status),this);
	    return true;
	}

	s_ifaceNotify.lock();
	m_notify = 0;
	s_ifaceNotify.unlock();

#ifdef XDEBUG
	if (debugAt(DebugAll)) {
	    String str;
	    str.hexify(buf+WP_HEADER,r,' ');
	    Debug(this,DebugAll,"Received %d bytes: %s",r,str.c_str());
	}
#endif

	DataBlock data(buffer.data,r,false);
	receivedPacket(data);
	data.clear(false);
    }
    return true;
}

// Interface control
// Enable: Open thread and create thread if not already created
// Disable: Cancel thread. Close socket
bool WpInterface::control(Operation oper, NamedList* params)
{
    DDebug(this,DebugAll,"Control with oper=%u [%p]",oper,this);
    switch (oper) {
	case Enable:
	case Disable:
	    break;
	case EnableTx:
	case DisableTx:
	    if (m_readOnly == (oper == DisableTx))
		return TelEngine::controlReturn(params,true);
	    m_readOnly = (oper == DisableTx);
	    m_sendReadOnly = false;
	    Debug(this,DebugInfo,"Tx is %sabled [%p]",m_readOnly?"dis":"en",this);
	    return TelEngine::controlReturn(params,true);
	case Query:
	    return TelEngine::controlReturn(params,m_socket.valid() && m_thread && m_thread->running());
	default:
	    return SignallingInterface::control(oper,params);
    }
    if (oper == Enable) {
	bool ok = false;
	if (m_socket.valid() || m_socket.open()) {
	    if (!m_thread)
		m_thread = new WpSigThread(this);
	    if (m_thread->running())
		ok = true;
	    else
		ok = m_thread->startup();
	}
	if (ok) {
	    DDebug(this,DebugAll,"Enabled [%p]",this);
	    m_timerRxUnder.start();
	}
	else {
	    Debug(this,DebugWarn,"Enable failed [%p]",this);
	    control(Disable,0);
	}
	return TelEngine::controlReturn(params,ok);
    }
    // oper is Disable
    m_timerRxUnder.stop();
    if (m_thread) {
	m_thread->cancel();
	while (m_thread)
	    Thread::yield();
    }
    m_socket.close();
    DDebug(this,DebugAll,"Disabled [%p]",this);
    return TelEngine::controlReturn(params,true);
}

void WpInterface::timerTick(const Time& when)
{
    if (!m_timerRxUnder.timeout(when.msec()))
	return;
    s_ifaceNotify.lock();
    if (m_notify) {
	if (m_notify == 1) {
	    DDebug(this,DebugMild,"RX idle for " FMT64 "ms. Notifying receiver [%p]",
		m_timerRxUnder.interval(),this);
	    notify(RxUnderrun);
	    m_notify = 2;
	}
    }
    else
	m_notify = 1;
    s_ifaceNotify.unlock();
    m_timerRxUnder.start(when.msec());
}

/**
 * WpSigThread
 */
WpSigThread::~WpSigThread()
{
    if (m_interface) {
	Debug(m_interface,DebugAll,"Worker thread stopped [%p]",this);
	m_interface->m_thread = 0;
    }
    else
	Debug(DebugAll,"WpSigThread::~WpSigThread() [%p]",this);
}

void WpSigThread::run()
{
    if (!m_interface) {
	Debug(DebugWarn,"WpSigThread::run(). No client object [%p]",this);
	return;
    }
    Debug(m_interface,DebugAll,"Worker thread started [%p]",this);
    for (;;) {
	Thread::yield(true);
	while (m_interface && m_interface->receiveAttempt())
	    ;
    }
}

/**
 * WpSource
 */
WpSource::WpSource(WpCircuit* owner, const char* format, unsigned int bufsize)
    : DataSource(format),
    m_owner(owner),
    m_buffer(0,bufsize),
    m_bufpos(0),
    m_total(0)
{
    XDebug(DebugAll,"WpSource::WpSource(%p,%u,'%s') [%p]",
	owner,bufsize,format,this);
}

WpSource::~WpSource()
{
    XDebug(DebugAll,"WpSource::~WpSource() [%p]",this);
}

// Put a byte in buffer. Forward data when full
void WpSource::put(unsigned char c)
{
    ((char*)m_buffer.data())[m_bufpos] = c;
    if (++m_bufpos == m_buffer.length()) {
	m_bufpos = 0;
	Forward(m_buffer);
	m_total += m_buffer.length();
    }
}

/**
 * WpConsumer
 */
WpConsumer::WpConsumer(WpCircuit* owner, const char* format, unsigned int bufsize)
    : DataConsumer(format),
    Fifo(2 * bufsize),
    m_owner(owner),
    m_errorCount(0),
    m_errorBytes(0),
    m_total(0)

{
    XDebug(DebugAll,"WpConsumer::WpConsumer(%p,%u,'%s') [%p]",
	owner,bufsize,format,this);
}

WpConsumer::~WpConsumer()
{
    XDebug(DebugAll,"WpConsumer::~WpConsumer. [%p]",this);
}

// Put data in fifo buffer
unsigned long WpConsumer::Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
{
    unsigned int err = put((const unsigned char*)data.data(),data.length());
    if (err) {
	m_errorCount++;
	m_errorBytes += err;
    }
    m_total += data.length();
    return invalidStamp();
}

/**
 * WpCircuit
 */
WpCircuit::WpCircuit(unsigned int code, SignallingCircuitGroup* group, WpSpan* data,
	unsigned int buflen, unsigned int channel)
    : SignallingCircuit(TDM,code,Idle,group,data),
    m_mutex(true),
    m_channel(channel),
    m_sourceValid(0),
    m_consumerValid(0),
    m_source(0),
    m_consumer(0)
{
    if (buflen) {
	m_source = new WpSource(this,"alaw",buflen);
	m_consumer = new WpConsumer(this,"alaw",buflen);
	XDebug(group,DebugAll,"WpCircuit(%u). Source (%p). Consumer (%p) [%p]",
	    code,m_source,m_consumer,this);
    }
    else
	Debug(group,DebugNote,
	    "WpCircuit(%u). No source and consumer. Buffer length is 0 [%p]",
	    code,this);
}

WpCircuit::~WpCircuit()
{
    Lock lock(m_mutex);
    status(Missing);
    if (m_source)
	m_source->deref();
    if (m_consumer)
	m_consumer->deref();
    XDebug(group(),DebugAll,"WpCircuit::~WpCircuit(%u) [%p]",code(),this);
}

// Change circuit status. Clear events on succesfully changes status
// Connected: Set valid source and consumer
// Otherwise: Invalidate and reset source and consumer
bool WpCircuit::status(Status newStat, bool sync)
{
    Lock lock(m_mutex);
    if (SignallingCircuit::status() == newStat)
	return true;
    TempObjectCounter cnt(driver.objectsCounter());
    // Allow status change for the following values
    switch (newStat) {
	case Missing:
	case Disabled:
	case Idle:
	case Reserved:
	case Connected:
	    break;
	default: ;
	    Debug(group(),DebugNote,
		"WpCircuit(%u). Can't change status to unhandled value %u [%p]",
		code(),newStat,this);
	    return false;
    }
    if (SignallingCircuit::status() == Missing) {
	Debug(group(),DebugNote,
	    "WpCircuit(%u). Can't change status to '%s'. Circuit is missing [%p]",
	    code(),lookupStatus(newStat),this);
	return false;
    }
    Status oldStat = SignallingCircuit::status();
    // Change status
    if (!SignallingCircuit::status(newStat,sync))
	return false;
    // Enable/disable data transfer
    clearEvents();
    bool enableData = false;
    if (SignallingCircuit::status() == Connected)
	enableData = true;
    // Don't put this message for final states
    if (!Engine::exiting())
	DDebug(group(),DebugAll,"WpCircuit(%u). Changed status to '%s' [%p]",
	    code(),lookupStatus(newStat),this);
    if (enableData) {
	m_sourceValid = m_source;
	m_consumerValid = m_consumer;
	return true;
    }
    // Disable data if not already disabled
    if (m_consumerValid) {
	if (oldStat == Connected) {
	    XDebug(group(),DebugAll,"WpCircuit(%u). Consumer transferred %u byte(s) [%p]",
		code(),m_consumer->m_total,this);
	    if (m_consumer->m_errorCount)
		DDebug(group(),DebugMild,"WpCircuit(%u). Consumer errors: %u. Lost: %u/%u [%p]",
		    code(),m_consumer->m_errorCount,m_consumer->m_errorBytes,
		    m_consumer->m_total,this);
	}
	m_consumer->clear();
	m_consumerValid = 0;
	m_consumer->m_errorCount = m_consumer->m_errorBytes = 0;
	m_consumer->m_total = 0;
    }
    if (m_sourceValid) {
	if (oldStat == Connected)
	    XDebug(group(),DebugAll,"WpCircuit(%u). Source transferred %u byte(s) [%p]",
		code(),m_source->m_total,this);
	m_source->clear();
	m_sourceValid = 0;
	m_source->m_total = 0;
    }
    return true;
}

// Update source/consumer data format
bool WpCircuit::updateFormat(const char* format, int direction)
{
    if (!(format && *format))
	return false;
    TempObjectCounter cnt(driver.objectsCounter());
    bool consumerChanged = true;
    bool sourceChanged = true;
    Lock lock(m_mutex);
    if (direction == -1 || direction == 0) {
	if (m_consumer && m_consumer->getFormat() != format) {
	    m_consumer->changeFormat(format);
	    DDebug(group(),DebugAll,"WpCircuit(%u). Consumer format set to '%s' [%p]",
		code(),format,this);
	}
	else
	    consumerChanged = false;
    }
    if (direction == 1 || direction == 0) {
	if (m_source && m_source->getFormat() != format) {
	    m_source->changeFormat(format);
	    DDebug(group(),DebugAll,"WpCircuit(%u). Source format set to '%s' [%p]",
		code(),format,this);
	}
	else
	    sourceChanged = false;
    }
    return consumerChanged && sourceChanged;
}

// Get source or consumer
void* WpCircuit::getObject(const String& name) const
{
    if (!group())
	return 0;
    if (name == "DataSource")
	return m_sourceValid;
    if (name == "DataConsumer")
	return m_consumerValid;
    return 0;
}

// Enqueue received events
inline bool WpCircuit::enqueueEvent(SignallingCircuitEvent* e)
{
    if (e) {
	addEvent(e);
	XDebug(group(),e->type()!=SignallingCircuitEvent::Unknown?DebugAll:DebugStub,
	    "WpCircuit(%u). Enqueued event '%s' [%p]",code(),e->c_str(),this);
    }
    return true;
}


/**
 * WpSpan
 */
unsigned char WpSpan::s_bitswap[256] = {
	0x00,0x80,0x40,0xc0,0x20,0xa0,0x60,0xe0,0x10,0x90,0x50,0xd0,0x30,0xb0,0x70,0xf0,0x08,0x88,0x48,0xc8,
	0x28,0xa8,0x68,0xe8,0x18,0x98,0x58,0xd8,0x38,0xb8,0x78,0xf8,0x04,0x84,0x44,0xc4,0x24,0xa4,0x64,0xe4,
	0x14,0x94,0x54,0xd4,0x34,0xb4,0x74,0xf4,0x0c,0x8c,0x4c,0xcc,0x2c,0xac,0x6c,0xec,0x1c,0x9c,0x5c,0xdc,
	0x3c,0xbc,0x7c,0xfc,0x02,0x82,0x42,0xc2,0x22,0xa2,0x62,0xe2,0x12,0x92,0x52,0xd2,0x32,0xb2,0x72,0xf2,
	0x0a,0x8a,0x4a,0xca,0x2a,0xaa,0x6a,0xea,0x1a,0x9a,0x5a,0xda,0x3a,0xba,0x7a,0xfa,0x06,0x86,0x46,0xc6,
	0x26,0xa6,0x66,0xe6,0x16,0x96,0x56,0xd6,0x36,0xb6,0x76,0xf6,0x0e,0x8e,0x4e,0xce,0x2e,0xae,0x6e,0xee,
	0x1e,0x9e,0x5e,0xde,0x3e,0xbe,0x7e,0xfe,0x01,0x81,0x41,0xc1,0x21,0xa1,0x61,0xe1,0x11,0x91,0x51,0xd1,
	0x31,0xb1,0x71,0xf1,0x09,0x89,0x49,0xc9,0x29,0xa9,0x69,0xe9,0x19,0x99,0x59,0xd9,0x39,0xb9,0x79,0xf9,
	0x05,0x85,0x45,0xc5,0x25,0xa5,0x65,0xe5,0x15,0x95,0x55,0xd5,0x35,0xb5,0x75,0xf5,0x0d,0x8d,0x4d,0xcd,
	0x2d,0xad,0x6d,0xed,0x1d,0x9d,0x5d,0xdd,0x3d,0xbd,0x7d,0xfd,0x03,0x83,0x43,0xc3,0x23,0xa3,0x63,0xe3,
	0x13,0x93,0x53,0xd3,0x33,0xb3,0x73,0xf3,0x0b,0x8b,0x4b,0xcb,0x2b,0xab,0x6b,0xeb,0x1b,0x9b,0x5b,0xdb,
	0x3b,0xbb,0x7b,0xfb,0x07,0x87,0x47,0xc7,0x27,0xa7,0x67,0xe7,0x17,0x97,0x57,0xd7,0x37,0xb7,0x77,0xf7,
	0x0f,0x8f,0x4f,0xcf,0x2f,0xaf,0x6f,0xef,0x1f,0x9f,0x5f,0xdf,0x3f,0xbf,0x7f,0xff
	};

// Initialize B-channel group
// Create circuits. Start worker thread
WpSpan::WpSpan(const NamedList& params)
    : SignallingCircuitSpan(params.getValue("debugname"),
	static_cast<SignallingCircuitGroup*>(params.getObject("SignallingCircuitGroup"))),
    m_socket(m_group),
    m_thread(0),
    m_canSend(true),
    m_swap(false),
    m_chans(0),
    m_count(0),
    m_first(0),
    m_samples(0),
    m_noData(0),
    m_buflen(0),
    m_circuits(0),
    m_readErrors(0)
{
    XDebug(m_group,DebugAll,"WpSpan::WpSpan(). Name '%s' [%p]",id().safe(),this);
}

// Terminate worker thread
// Close socket. Clear circuit list
WpSpan::~WpSpan()
{
    if (m_thread) {
	m_thread->cancel();
	while (m_thread)
	    Thread::yield();
    }
    m_socket.close();
    if (m_circuits)
	delete[] m_circuits;
    XDebug(m_group,DebugAll,"WpSpan::~WpSpan() [%p]",this);
}

// Initialize
bool WpSpan::init(const NamedList& config, const NamedList& defaults, NamedList& params)
{
    if (!m_group) {
	Debug(DebugNote,"WpSpan('%s'). Circuit group is missing [%p]",
	    id().safe(),this);
	return false;
    }
    TempObjectCounter cnt(driver.objectsCounter());
    // Set socket card / device
    m_socket.card(config);
    const char* voice = params.getValue("voicegroup",config.getValue("voicegroup"));
    if (!voice) {
	Debug(m_group,DebugNote,"WpSpan('%s'). Missing or invalid voice group [%p]",
	    id().safe(),this);
	return false;
    }
    m_socket.device(voice);
    m_canSend = !params.getBoolValue("readonly",config.getBoolValue("readonly",false));
    // Type depending data: channel count, samples, circuit list
    String type = config.getValue("type");
    String cics = config.getValue("voicechans");
    unsigned int offs = config.getIntValue("offset",0);
    m_samples = params.getIntValue("samples",config.getIntValue("samples"));
    if (type.null())
	type = "E1";
    if (type == "E1") {
	m_chans = 31;
	if (cics.null())
	    cics = "1-15,17-31";
	if (!m_samples)
	    m_samples = 50;
    }
    else if (type == "T1") {
	m_chans = 24;
	if (cics.null())
	    cics = "1-23";
	if (!m_samples)
	    m_samples = 64;
    }
    else {
	Debug(m_group,DebugNote,"WpSpan('%s'). Invalid voice group type '%s' [%p]",
	    id().safe(),type.safe(),this);
	return false;
    }
    params.setParam("chans",String(m_chans));
    // Other data
    m_swap = defaults.getBoolValue("bitswap",true);
    m_noData = defaults.getIntValue("idlevalue",0xff);
    m_buflen = defaults.getIntValue("buflen",160);
    m_swap = params.getBoolValue("bitswap",config.getBoolValue("bitswap",m_swap));
    m_noData = params.getIntValue("idlevalue",config.getIntValue("idlevalue",m_noData));
    m_buflen = params.getIntValue("buflen",config.getIntValue("buflen",m_buflen));

    // Buffer length can't be 0
    if (!m_buflen)
	m_buflen = 160;
    // Channels
    if (!createCircuits(params.getIntValue("start") + offs,cics)) {
	Debug(m_group,DebugNote,
	    "WpSpan('%s'). Failed to create voice chans (voicechans=%s) [%p]",
	    id().safe(),cics.safe(),this);
	return false;
    }
    // Start processing data
    m_thread = new WpSpanThread(this);
    if (!m_thread->startup()) {
	Debug(m_group,DebugNote,"WpSpan('%s'). Failed to start worker thread [%p]",
	    id().safe(),this);
	return false;
    }
    if (debugAt(DebugInfo)) {
	String s;
	s << "driver=" << driver.debugName();
	s << " section=" << config.c_str();
	s << " type=" << type;
	s << " card=" << m_socket.card();
	s << " device=" << m_socket.device();
	s << " samples=" << m_samples;
	s << " bitswap=" << String::boolText(m_swap);
	s << " idlevalue=" << (unsigned int)m_noData;
	s << " buflen=" << (unsigned int)m_buflen;
	s << " readonly=" << String::boolText(!m_canSend);
	s << " channels=" << cics << " (" << m_count << ")";
	String cicList;
	if (m_circuits)
	    for (unsigned int i = 0; i < m_count; i++)
		cicList.append(String(m_circuits[i]->code()),",");
	s << " circuits=" << cicList;
	Debug(m_group,DebugInfo,"WpSpan('%s') %s [%p]",id().safe(),s.safe(),this);
    }
    return true;
}

// Create circuits (all or nothing)
// delta: number to add to each circuit code
// cicList: Circuits to create
bool WpSpan::createCircuits(unsigned int delta, const String& cicList)
{
    unsigned int* cicCodes = SignallingUtils::parseUIntArray(cicList,1,m_chans,m_count,true);
    if (!cicCodes)
	return false;
    if (m_circuits)
	delete[] m_circuits;
    m_circuits = new WpCircuit*[m_count];
    bool ok = true;
    for (unsigned int i = 0; i < m_count; i++) {
	m_circuits[i] = new WpCircuit(delta + cicCodes[i],m_group,this,m_buflen,cicCodes[i]);
	if (m_group->insert(m_circuits[i]))
	    continue;
	// Failure
	Debug(m_group,DebugNote,
	    "WpSpan('%s'). Failed to create/insert circuit %u. Rollback [%p]",
	    id().safe(),cicCodes[i],this);
	m_group->removeSpan(this,true,false);
	delete[] m_circuits;
	m_circuits = 0;
	ok = false;
	break;
    }
    delete[] cicCodes;
    return ok;
}

// Read events and data from socket. Send data when succesfully read
// Received data is splitted for each circuit
// Sent data from each circuit is merged into one data block
void WpSpan::run()
{
    if (!m_socket.open())
	return;
    DDebug(m_group,DebugInfo,
	"WpSpan('%s'). Worker is running: circuits=%u, samples=%u [%p]",
	id().safe(),m_count,m_samples,this);
    while (true) {
	if (Thread::check(true))
	    break;
	if (!m_socket.select(m_samples))
	    continue;
	if (!m_socket.canRead())
	    continue;
	int r = readData();
	if (r == -1)
	    continue;
	r -= WP_HEADER;
	// Calculate received samples. Check if we received valid data
	unsigned int samples = 0;
	if ((r > 0) && ((r % m_count) == 0))
	    samples = (unsigned int)r / m_count;
	if (!samples) {
	    Debug(m_group,DebugNote,
		"WpSpan('%s'). Received data %d is not a multiple of circuit number %u [%p]",
		id().safe(),r,m_count,this);
	    continue;
	}
	if (samples != m_samples)
	    Debug(m_group,DebugInfo,
		"WpSpan('%s'). Received %u samples. Expected %u [%p]",
		id().safe(),samples,m_samples,this);
	unsigned char* dat = m_buffer.data;
	if (m_canSend) {
	    // Read each byte from buffer. Prepare buffer for sending
	    for (int n = samples; n > 0; n--) {
		for (unsigned int i = 0; i < m_count; i++) {
		    if (m_circuits[i]->source())
			m_circuits[i]->source()->put(swap(*dat));
		    if (m_circuits[i]->consumer())
			*dat = swap(m_circuits[i]->consumer()->get());
		    else
			*dat = swap(m_noData);
		    dat++;
		}
	    }
	    m_buffer.api_header.data_length = m_samples * m_count;
	    m_buffer.api_header.operation_status = SANG_STATUS_TX_TIMEOUT;
	    m_socket.send(&m_buffer,sizeof(m_buffer));
	}
	else
	    for (int n = samples; n > 0; n--)
		for (unsigned int i = 0; i < m_count; i++)
		    if (m_circuits[i]->source())
			m_circuits[i]->source()->put(swap(*dat++));
    }
}

// Find a circuit by channel
WpCircuit* WpSpan::find(unsigned int channel)
{
    if (!m_circuits)
	return 0;
    for (unsigned int i = 0; i < m_count; i++)
	if (m_circuits[i] && m_circuits[i]->channel() == channel)
	    return m_circuits[i];
    return 0;
}

// Read data from socket. Check for errors or in-band events
// Return -1 on error
int WpSpan::readData()
{
    m_buffer.api_header.operation_status = SANG_STATUS_RX_DATA_TIMEOUT;
    int r = m_socket.recv(&m_buffer,sizeof(m_buffer));
    // Check errors
    if (r == -1)
	return -1;
    if (r < WP_HEADER) {
	Debug(m_group,DebugGoOn,"WpSpan('%s'). Short read %u byte(s) [%p]",
	    id().safe(),r,this);
	return -1;
    }
    if (SANG_STATUS_SUCCESS != m_buffer.api_header.operation_status) {
	m_readErrors++;
	if (m_readErrors == MAX_READ_ERRORS) {
	    Debug(m_group,DebugGoOn,"WpSpan('%s'). Read error %u (%s) [%p]",
		id().safe(),m_buffer.api_header.operation_status,
		SDLA_DECODE_SANG_STATUS(m_buffer.api_header.operation_status),this);
	    m_readErrors = 0;
	}
    }
    else
	m_readErrors = 0;
    // Check events
    decodeEvent();
    return r;
}

bool WpSpan::decodeEvent()
{
    return false;
}


/**
 * WpSpanThread
 */
WpSpanThread::~WpSpanThread()
{
    if (m_data) {
	Debug(m_data->group(),DebugAll,"WpSpan('%s'). Worker thread stopped [%p]",
	    m_data->id().safe(),this);
	m_data->m_thread = 0;
    }
    else
	Debug(DebugAll,"WpSpanThread::~WpSpanThread() [%p]",this);
}

void WpSpanThread::run()
{
    if (m_data) {
	Debug(m_data->group(),DebugAll,"WpSpan('%s'). Worker thread started [%p]",
	    m_data->id().safe(),this);
	m_data->run();
    }
    else
	Debug(DebugWarn,"WpSpanThread::run(). No client object [%p]",this);
}

/**
 * WpModule
 */
WpModule::WpModule()
    : Module("wanpipe","misc",true),
    m_init(false)
{
    Output("Loaded module Wanpipe");
}

WpModule::~WpModule()
{
    Output("Unloading module Wanpipe");
}

void WpModule::initialize()
{
    Output("Initializing module Wanpipe");
    if (!m_init) {
	m_init = true;
	setup();
	String events;
#ifndef HAVE_WANPIPE_HWEC
	events.append("set/reset echo canceller",", ");
#endif
#ifndef WAN_EC_TONE_PRESENT
	events.append("detect tones",", ");
#endif
	if (!events.null())
	    Debug(this,DebugWarn,"The module is unable to: %s [%p]",
		events.c_str(),this);
    }
}

}; // anonymous namespace

#endif /* _WINDOWS */

/* vi: set ts=8 sw=4 sts=4 noet: */
