/**
 * wpcard.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Wanpipe PRI cards signalling and data driver
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

#include <yatephone.h>
#include <yatess7.h>

#ifdef _WINDOWS
#error This module is not for Windows
#else

extern "C" {

#define INVALID_HANDLE_VALUE (-1)
#define __LINUX__
#include <linux/if_wanpipe.h>
#include <linux/if.h>
#include <linux/wanpipe.h>
#include <linux/wanpipe_cfg.h>
#include <linux/sdla_bitstrm.h>

};

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <sys/ioctl.h>
#include <fcntl.h>

#define WP_HEADER 16

#define WP_RD_ERROR    0
#define WP_RD_STAMP_LO 1
#define WP_RD_STAMP_HI 2

#define WP_WR_TYPE     0
#define WP_WR_FORCE    1

#define WP_ERR_FIFO  0x01
#define WP_ERR_CRC   0x02
#define WP_ERR_ABORT 0x04

#define MAX_PACKET 1200

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

static const char* s_driverName = "Wanpipe";

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
    inline WpSocket(DebugEnabler* dbg, const char* card = 0, const char* device = 0)
	: m_dbg(dbg), m_card(card), m_device(device),
	m_canRead(false), m_event(false),
	m_readError(false), m_writeError(false), m_selectError(false)
	{}
    inline ~WpSocket()
	{ close(); }
    inline bool valid() const
	{ return m_socket.valid(); }
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
    inline bool event() const
	{ return m_event; }
    // Open socket. Return false on failure
    bool open(bool blocking);
    // Close socket
    void close();
    // Read data. Return -1 on failure
    int recv(void* buffer, int len, int flags = 0);
    // Send data. Return -1 on failure
    int send(const void* buffer, int len, int flags = 0);
    // Check socket. Set flags to the appropriate values on success
    // Return false on failure
    bool select(unsigned int multiplier);
protected:
    inline void showError(const char* action, const char* info = 0) {
	    Debug(m_dbg,DebugWarn,"WpSocket(%s/%s). %s failed%s. %d: %s [%p]",
		m_card.c_str(),m_device.c_str(),action,c_safe(info),
		m_socket.error(),::strerror(m_socket.error()),this);
	}
private:
    DebugEnabler* m_dbg;                 // Debug enabler owning this socket
    Socket m_socket;                     // 
    String m_card;                       // Card name used to open socket
    String m_device;                     // Device name used to open socket
    bool m_canRead;                      // Set by select(). Can read from socket
    bool m_event;                        // Set by select(). An event occurred
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
    static void* create(const String& type, const NamedList& name);
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
		GenObject::destruct();
	}
    WpSocket m_socket;
    WpSigThread* m_thread;               // Thread used to read data from socket
    bool m_readOnly;                     // Readonly interface
    int m_notify;                        // Upper layer notification on received data (0: success. 1: not notified. 2: notified)
    int m_overRead;                      // Header extension
    unsigned char m_errorMask;           // Error mask to filter received errors
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
    virtual void Consume(const DataBlock& data, unsigned long tStamp);
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
	unsigned int buflen);
    virtual ~WpCircuit();
    virtual bool status(Status newStat, bool sync = false);
    virtual bool updateFormat(const char* format, int direction);
    virtual void* getObject(const String& name) const;
    inline WpSource* source()
	{ return m_sourceValid; }
    inline WpConsumer* consumer()
	{ return m_consumerValid; }
private:
    Mutex m_mutex;
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
protected:
    // Create circuits (all or nothing)
    // delta: number to add to each circuit code
    // cicList: Circuits to create
    bool createCircuits(unsigned int delta, const String& cicList);
    // Check for received event (including in-band events)
    bool readEvent();
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
    unsigned char* m_buffer;             // I/O data buffer
    unsigned int m_bufferLen;            // I/O data buffer length
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


YSIGFACTORY2(WpInterface,SignallingInterface);

static Mutex s_ifaceNotify(true);        // WpInterface: lock recv data notification counter

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
// Open socket
bool WpSocket::open(bool blocking)
{
    DDebug(m_dbg,DebugAll,
	"WpSocket::open(). Card: '%s'. Device: '%s'. Blocking: %s [%p]",
	m_card.c_str(),m_device.c_str(),String::boolText(blocking),this);
    if (!m_socket.create(AF_WANPIPE,SOCK_RAW)) {
	showError("Create");
	return false;
    }
    // Bind to the card/interface
    struct wan_sockaddr_ll sa;
    memset(&sa,0,sizeof(struct wan_sockaddr_ll));
    ::strncpy((char*)sa.sll_card,m_card.safe(),sizeof(sa.sll_card));
    ::strncpy((char*)sa.sll_device,m_device.safe(),sizeof(sa.sll_device));
    sa.sll_protocol = htons(PVC_PROT);
    sa.sll_family = AF_WANPIPE;
    if (!m_socket.bind((struct sockaddr *)&sa, sizeof(sa))) {
	showError("Bind");
	close();
	return false;
    }
    if (!m_socket.setBlocking(blocking)) {
	showError("Set blocking");
	close();
	return false;
    }
    return true;
}

// Close socket
void WpSocket::close()
{
    if (!m_socket.valid())
	return;
    DDebug(m_dbg,DebugAll,"WpSocket::close(). Card: '%s'. Device: '%s' [%p]",
	m_card.c_str(),m_device.c_str(),this);
    m_socket.setLinger(-1);
    m_socket.terminate();
}

// Read data from socket
int WpSocket::recv(void* buffer, int len, int flags)
{
    int r = m_socket.recv(buffer,len,flags);
    if (r != Socket::socketError()) {
	m_readError = false;
	return r;
    }
    if (!(m_socket.canRetry() || m_readError)) {
	const char* info = 0;
#ifdef SIOC_WANPIPE_SOCK_STATE
	r == ::ioctl(m_socket.handle(),SIOC_WANPIPE_SOCK_STATE,0);
	if (r == -1)
	    info = " (IOCTL failed: data link may be disconnected)";
#endif
	showError("Read",info);
	m_readError = true;
    }
    return -1;
}

// Write data to socket
int WpSocket::send(const void* buffer, int len, int flags)
{
    int w = m_socket.send(buffer,len,flags);
    if (w != Socket::socketError() && w == len) {
	m_writeError = false;
	return w;
    }
    if (m_writeError)
	return -1;
    if (w == Socket::socketError())
	w = 0;
    String info;
    info << " (Sent " << w << " instead of " << len << ')';
    showError("Send",info);
    m_writeError = true;
    return -1;
}

// Check events and socket availability
bool WpSocket::select(unsigned int multiplier)
{
    m_canRead = m_event = false;
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = multiplier * WPSOCKET_SELECT_TIMEOUT;
    if (m_socket.select(&m_canRead,0,&m_event,&tv)) {
	m_selectError = false;
	return true;
    }
    if (m_selectError)
	return false;
    showError("Select");
    m_selectError = true;
    return false;
}

/**
 * WpInterface
 */
// Create WpInterface or WpSpan
void* WpInterface::create(const String& type, const NamedList& name)
{
    bool interface = false;
    if (type == "sig")
	interface = true;
    else  if (type == "voice")
	;
    else
	return 0;

    Configuration cfg(Engine::configFile("wpcard"));
    cfg.load();
    const char* sectName = name.getValue(type);
    DDebug(s_driverName,DebugAll,"Factory trying to create %s='%s'",type.c_str(),sectName);
    NamedList* config = cfg.getSection(sectName);
    if (!config) {
	DDebug(s_driverName,DebugAll,"No section '%s' in configuration",c_safe(sectName));
	return 0;
    }

    if (interface) {
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
    m_overRead(0),
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
    // Set socket card / device
    m_socket.card(config);
    const char* sig = params.getValue("siggroup",config.getValue("siggroup"));
    if (!sig) {
	Debug(this,DebugWarn,
	    "Missing or invalid siggroup='%s' in configuration [%p]",
	    c_safe(sig),this);
	return false;
    }
    m_socket.device(sig);

    m_readOnly = config.getBoolValue("readonly",false);

    int i = params.getIntValue("errormask",config.getIntValue("errormask",255));
    m_errorMask = ((i >= 0 && i < 256) ? i : 255);

    int rx = params.getIntValue("rxunderruninterval");
    if (rx > 0)
	m_timerRxUnder.interval(rx);

    if (debugAt(DebugInfo)) {
	String s;
	s << "\r\nCard:                  " << m_socket.card();
	s << "\r\nDevice:                " << m_socket.device();
	s << "\r\nError mask:            " << (unsigned int)m_errorMask;
	s << "\r\nRead only:             " << String::boolText(m_readOnly);
	s << "\r\nRX underrun interval:  " << (unsigned int)m_timerRxUnder.interval() << " ms";
	Debug(this,DebugInfo,"Initialized: [%p]%s",this,s.c_str());
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

    DataBlock data(0,WP_HEADER);
    data += packet;
    unsigned char* d = static_cast<unsigned char*>(data.data());
    if (repeat)
	d[WP_WR_FORCE] = 1;
    switch (type) {
	case SS7Fisu:
	    d[WP_WR_TYPE] = WANOPT_SS7_FISU;
	    break;
	case SS7Lssu:
	    d[WP_WR_TYPE] = WANOPT_SS7_LSSU;
	    break;
	default:
	    break;
    }
    return -1 != m_socket.send(data.data(),data.length(),0);
}

static inline const char* error(unsigned char err)
{
    static String s;
    s.clear();
    if (err & WP_ERR_CRC)
	s.append("CRC");
    if (err & WP_ERR_FIFO)
	s.append("RxOver"," ");
    if (err & WP_ERR_ABORT)
	s.append("Align"," ");
    if (s.null())
	s << (int)err;
    return s.safe();
}

// Receive signalling packet
bool WpInterface::receiveAttempt()
{
    if (!m_socket.valid())
	return false;
    if (!m_socket.select(5))
	return false;
    if (!m_socket.canRead())
	return false;
    unsigned char buf[WP_HEADER + MAX_PACKET];
    int r = m_socket.recv(buf,sizeof(buf),MSG_NOSIGNAL);
    if (r == -1)
	return false;
    if (r > (WP_HEADER + m_overRead)) {
	XDebug(this,DebugAll,"Received %d bytes packet. Header length is %u [%p]",
	    r,WP_HEADER + m_overRead,this);
	r -= (WP_HEADER + m_overRead);
	unsigned char err = buf[WP_RD_ERROR] & m_errorMask;
	if (err) {
	    DDebug(this,DebugWarn,"Packet got error: %u (%s) [%p]",
		buf[WP_RD_ERROR],error(buf[WP_RD_ERROR]),this);
	    if (err & WP_ERR_FIFO)
		notify(RxOverflow);
	    if (err & WP_ERR_CRC)
		notify(CksumError);
	    if (err & WP_ERR_ABORT)
		notify(AlignError);
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

	DataBlock data(buf+WP_HEADER,r);
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
		return true;
	    m_readOnly = (oper == DisableTx);
	    m_sendReadOnly = false;
	    Debug(this,DebugInfo,"Tx is %sabled [%p]",m_readOnly?"dis":"en",this);
	    return true;
	case Query:
	    return m_socket.valid() && m_thread && m_thread->running();
	default:
	    return SignallingInterface::control(oper,params);
    }
    if (oper == Enable) {
	bool ok = false;
	if (m_socket.valid() || m_socket.open(true)) {
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
	return ok;
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
    return true;
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
    DDebug(m_interface,DebugAll,"WpSigThread::~WpSigThread() [%p]",this);
    if (m_interface)
	m_interface->m_thread = 0;
}

void WpSigThread::run()
{
    DDebug(m_interface,DebugAll,"%s start running [%p]",name(),this);
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
void WpConsumer::Consume(const DataBlock& data, unsigned long tStamp)
{
    unsigned int err = put((const unsigned char*)data.data(),data.length());
    if (err) {
	m_errorCount++;
	m_errorBytes += err;
    }
    m_total += data.length();
}

/**
 * WpCircuit
 */
WpCircuit::WpCircuit(unsigned int code, SignallingCircuitGroup* group, WpSpan* data,
	unsigned int buflen)
    : SignallingCircuit(TDM,code,Idle,group,data),
    m_mutex(true),
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
	    "WpCircuit(%u). Can't change status to '%u'. Circuit is missing [%p]",
	    code(),newStat,this);
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
	DDebug(group(),DebugAll,"WpCircuit(%u). Changed status to %u [%p]",
	    code(),newStat,this);
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
    m_readErrors(0),
    m_buffer(0),
    m_bufferLen(0)
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
    if (m_buffer)
	delete[] m_buffer;
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
    // Set socket card / device
    m_socket.card(config);
    const char* voice = params.getValue("voicegroup",config.getValue("voicegroup"));
    if (!voice) {
	Debug(m_group,DebugNote,"WpSpan('%s'). Missing or invalid voice group [%p]",
	    id().safe(),this);
	return false;
    }
    m_socket.device(voice);
    m_canSend = !config.getBoolValue("readonly",false);
    // Type depending data: channel count, samples, circuit list
    String type = config.getValue("type");
    String cics = config.getValue("voicechans");
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
    if (!createCircuits(params.getIntValue("start"),cics)) {
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
	s << "\r\nType:           " << type;
	s << "\r\nGroup:          " << m_group->debugName();
	s << "\r\nCard:           " << m_socket.card();
	s << "\r\nDevice:         " << m_socket.device();
	s << "\r\nSamples:        " << m_samples;
	s << "\r\nBit swap:       " << String::boolText(m_swap);
	s << "\r\nIdle value:     " << (unsigned int)m_noData;
	s << "\r\nBuffer length:  " << (unsigned int)m_buflen;
	s << "\r\nUsed channels:  " << m_count;
	s << "\r\nRead only:      " << String::boolText(!m_canSend);
	Debug(m_group,DebugInfo,"WpSpan('%s'). Initialized: [%p]%s",
	    id().safe(),this,s.c_str());
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
	m_circuits[i] = new WpCircuit(delta + cicCodes[i],m_group,this,m_buflen);
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
    delete cicCodes;
    return ok;
}

// Read events and data from socket. Send data when succesfully read
// Received data is splitted for each circuit
// Sent data from each circuit is merged into one data block
void WpSpan::run()
{
    if (!m_socket.open(true))
	return;
    if (!m_buffer) {
	m_bufferLen = WP_HEADER + m_samples * m_count;
	m_buffer = new unsigned char[m_bufferLen];
    }
    XDebug(m_group,DebugInfo,
	"WpSpan('%s'). Running: circuits=%u, buffer=%u, samples=%u [%p]",
	id().safe(),m_count,m_bufferLen,m_samples,this);
    while (true) {
	if (Thread::check(true))
	    break;
	if (!m_socket.select(m_samples))
	    continue;
	if (m_socket.event())
	    readEvent();
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
	unsigned char* dat = m_buffer + WP_HEADER;
	if (m_canSend) {
	    // Read each byte from buffer. Prepare buffer for sending
	    for (int n = samples; n > 0; n--)
		for (unsigned int i = 0; i < m_count; i++) {
		    if (m_circuits[i]->source())
			m_circuits[i]->source()->put(swap(*dat));
		    if (m_circuits[i]->consumer())
			*dat = swap(m_circuits[i]->consumer()->get());
		    else
			*dat = swap(m_noData);
		    dat++;
		}
	    ::memset(m_buffer,0,WP_HEADER);
	    m_socket.send(m_buffer,WP_HEADER + samples * m_count,MSG_DONTWAIT);
	}
	else
	    for (int n = samples; n > 0; n--)
		for (unsigned int i = 0; i < m_count; i++)
		    if (m_circuits[i]->source())
			m_circuits[i]->source()->put(swap(*dat++));
    }
}

// Check for received event (including in-band events)
bool WpSpan::readEvent()
{
    XDebug(m_group,DebugInfo,"WpSpan('%s'). Got event. Checking OOB [%p]",
	id().safe(),this);
    int r = m_socket.recv(m_buffer,m_bufferLen,MSG_OOB);
    if (r >= WP_HEADER)
	decodeEvent();
    return true;
}

// Read data from socket. Check for errors or in-band events
// Return -1 on error
int WpSpan::readData()
{
    m_buffer[WP_RD_ERROR] = 0;
    int r = m_socket.recv(m_buffer,m_bufferLen);
    // Check errors
    if (r == -1)
	return -1;
    if (r < WP_HEADER) {
	Debug(m_group,DebugGoOn,"WpSpan('%s'). Short read %u byte(s) [%p]",
	    id().safe(),r,this);
	return -1;
    }
    if (m_buffer[WP_RD_ERROR]) {
	m_readErrors++;
	if (m_readErrors == MAX_READ_ERRORS) {
	    Debug(m_group,DebugGoOn,"WpSpan('%s'). Read error %u [%p]",
		id().safe(),m_buffer[WP_RD_ERROR],this);
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
#if 0
    if (!m_circuits)
	return false;
    SignallingCircuitEvent* event = 0;
    int code = 0xffffffff;
    // TODO: Decode event here. Set circuit code
    if (!event)
	return false;
    for (int i = 0; i < m_count; i++)
	if (m_circuits[i]->code() == code) {
	    cic->addEvent(event);
	    return true;
	}
    delete event;
    return true;
#endif
}

/**
 * WpSpanThread
 */
WpSpanThread::~WpSpanThread()
{
    if (m_data) {
	DDebug(m_data->group(),DebugAll,"WpSpanThread::~WpSpanThread() [%p]",this);
	m_data->m_thread = 0;
    }
    else
	DDebug(DebugAll,"WpSpanThread::~WpSpanThread() [%p]",this);
}

void WpSpanThread::run()
{
    if (m_data) {
	DDebug(m_data->group(),DebugAll,"%s start running for (%p): '%s' [%p]",
	    name(),m_data,m_data->id().safe(),this);
	m_data->run();
    }
    else
	DDebug(DebugAll,"WpSpanThread::run(). No client object [%p]",this);
}

}; // anonymous namespace

#endif /* _WINDOWS */

/* vi: set ts=8 sw=4 sts=4 noet: */
