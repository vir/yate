/**
 * wpcard.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Wanpipe PRI cards signalling and data driver
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

#ifdef _WINDOWS
#error This module is not for Windows
#else

extern "C" {

#define INVALID_HANDLE_VALUE (-1)

#define __LINUX__
#ifdef HAVE_WANPIPE_API
#include <if_wanpipe.h>
#include <wanpipe_defines.h>
#include <wanpipe_cfg.h>
#include <wanpipe.h>
#ifdef NEW_WANPIPE_API
#include <aft_core.h>
#else
#include <sdla_aft_te1.h>
#endif
#else
#include <linux/if_wanpipe.h>
#include <linux/if.h>
#include <linux/wanpipe_defines.h>
#include <linux/wanpipe_cfg.h>
#include <linux/wanpipe.h>
#include <linux/sdla_aft_te1.h>
#endif

#ifdef HAVE_WANPIPE_HWEC

#include <wanec_iface.h>
#ifdef HAVE_WANPIPE_HWEC_API
#include <wanec_iface_api.h>
#ifdef NEW_WANPIPE_API
#define WAN_EC_CMD_DTMF_ENABLE WAN_EC_API_CMD_TONE_ENABLE
#define WAN_EC_CMD_DTMF_DISABLE WAN_EC_API_CMD_TONE_DISABLE
#else
#define WAN_EC_CMD_DTMF_ENABLE WAN_EC_API_CMD_DTMF_ENABLE
#define WAN_EC_CMD_DTMF_DISABLE WAN_EC_API_CMD_DTMF_DISABLE
#endif
#ifdef u_buffer_config
#define HAVE_WANPIPE_HWEC_3310
#endif
#endif

#ifndef WANEC_DEV_DIR
#warning Incompatible echo canceller API, upgrade or configure --without-wphwec
#undef HAVE_WANPIPE_HWEC
#endif // WANEC_DEV_DIR

#endif // HAVE_WANPIPE_HWEC

};

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <sys/ioctl.h>
#include <fcntl.h>

#ifdef NEW_WANPIPE_API
#define WP_HEADER WAN_MAX_HDR_SZ
#else
#define WP_HEADER 16
#define WP_RD_ERROR  0
#define WP_RPT_REPEAT 0                  // Repeat flag in header
#define WP_RPT_LEN 1                     // Repeated data length
#define WP_RPT_DATA 2                    // Repeated data offset in header
#endif

#define WP_RPT_MAXDATA 8                 // Max repeated data length

#define WP_ERR_FIFO  (1 << WP_FIFO_ERROR_BIT)
#define WP_ERR_CRC   (1 << WP_CRC_ERROR_BIT)
#define WP_ERR_ABORT (1 << WP_ABORT_ERROR_BIT)
#ifdef NEW_WANPIPE_API
#define WP_ERR_FRAME (1 << WP_FRAME_ERROR_BIT)
#define WP_ERR_DMA   (1 << WP_DMA_ERROR_BIT)
#endif

// by default ignore ABORT and OVERFLOW conditions unrelated to current packet
#define WP_ERR_MASK  (0xff & ~(WP_ERR_FIFO|WP_ERR_ABORT))

#define MAX_PACKET 1200

#define MAX_READ_ERRORS 250              // WpSpan::run(): Display read error message
#define WPSOCKET_SELECT_TIMEOUT 125      // usec/sample used in WpSocket::select() to timeout
#define WPSOCKET_SELECT_SAMPLES 32       // Maximum signaling samples to wait for

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
class Fifo : public Mutex
{
public:
    inline Fifo(unsigned int buflen)
	: Mutex(true,"WPCard::Fifo"),
	  m_buffer(0,buflen), m_head(0), m_tail(1)
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
    DataBlock m_buffer;
    unsigned int m_head;
    unsigned int m_tail;
};

// I/O socket for WpInterface and WpSpan
class WpSocket
{
public:
    // Link state enumeration
    enum LinkStatus {
	Connected,                       // Link up
	Disconnected,                    // Link down
	Connecting,                      // Link is connecting
    };
    WpSocket(DebugEnabler* dbg, const char* card = 0, const char* device = 0);
    inline ~WpSocket()
	{ close(); }
    inline LinkStatus status()
	{ return m_status; }
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
    // Get/Set echo canceller availability
    inline bool echoCanAvail() const
	{ return m_echoCanAvail; }
    void echoCanAvail(bool val);
    // Set echo canceller and tone detection if available
    bool echoCancel(bool enable, unsigned long chanmap);
    // Set tone detection if available
    bool dtmfDetect(bool enable);
    inline bool canRead() const
	{ return m_canRead; }
    inline bool canWrite() const
	{ return m_canWrite; }
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
    bool select(unsigned int multiplier, bool checkWrite = false);
    // Update the state of the link and return true if changed
    bool updateLinkStatus();
protected:
    inline void showError(const char* action, const char* info = 0, int level = DebugWarn) {
	    Debug(m_dbg,level,"WpSocket(%s/%s). %s failed%s. %d: %s [%p]",
		m_card.c_str(),m_device.c_str(),action,c_safe(info),
		m_socket.error(),::strerror(m_socket.error()),this);
	}
private:
    DebugEnabler* m_dbg;                 // Debug enabler owning this socket
    LinkStatus m_status;                  // The state of the link
    Socket m_socket;                     // The socket
    String m_card;                       // Card name used to open socket
    String m_device;                     // Device name used to open socket
    bool m_echoCanAvail;                 // Echo canceller is available or not
    bool m_canRead;                      // Set by select(). Can read from socket
    bool m_canWrite;                     // Set by select(). Can write to socket
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
    static SignallingComponent* create(const String& type, NamedList& name);
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
    // Update link status. Notify the receiver if state changed
    bool updateStatus();
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
    int m_overRead;                      // Header extension
    unsigned char m_errorMask;           // Error mask to filter received errors
    unsigned char m_lastError;           // Last error seen
    bool m_sendReadOnly;                 // Print send attempt on readonly interface error
    SignallingTimer m_timerRxUnder;      // RX underrun notification
    // Repeat packet
    bool m_repeatCapable;                // HW repeat available
    Mutex m_repeatMutex;                 // Lock repeat buffer
    DataBlock m_repeatPacket;            // Packet to repeat
    bool m_down;
};

// Read signalling data for WpInterface
class WpSigThread : public Thread
{
    friend class WpInterface;
public:
    inline WpSigThread(WpInterface* iface, Priority prio = Normal)
	: Thread("Wp Interface",prio), m_interface(iface)
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
class WpCircuit : public SignallingCircuit, public Mutex
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
    virtual bool setParam(const String& param, const String& value);
    virtual void* getObject(const String& name) const;
    inline bool validSource()
	{ return 0 != m_sourceValid; }
    inline bool validConsumer()
	{ return 0 != m_consumerValid; }
    inline WpSource* source()
	{ return m_source; }
    inline WpConsumer* consumer()
	{ return m_consumer; }
    // Enqueue received events
    bool enqueueEvent(SignallingCircuitEvent* e);
private:
    unsigned int m_channel;              // Channel number inside span
    WpSource* m_sourceValid;             // Circuit's source if reserved, otherwise: 0
    WpConsumer* m_consumerValid;         // Circuit's consumer if reserved, otherwise: 0
    WpSource* m_source;
    WpConsumer* m_consumer;
    String m_specialMode;
};

// Wanpipe B-channel group
class WpSpan : public SignallingCircuitSpan
{
    friend class WpSpanThread;
public:
    WpSpan(const NamedList& params, const char* debugname);
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
    // Clear circuit vector safely
    void clearCircuits();
    // Check for received event (including in-band events)
    bool readEvent();
    // Read data from socket. Check for errors or in-band events
    // Return -1 on error
    int readData();
    // Decode received event
    bool decodeEvent();
    // Update link status. Enqueue an event in each circuit's queue on status change
    bool updateStatus();
    // Swapped bits table
    static unsigned char s_bitswap[256];
private:
    WpSocket m_socket;
    WpSpanThread* m_thread;
    bool m_canSend;                      // Can send data (not a readonly span)
    bool m_swap;                         // Swap bits flag
    unsigned long m_chanMap;             // Channel map used to set tone detector
    bool m_echoCancel;                   // Enable/disable echo canceller
    bool m_dtmfDetect;                   // Enable/disable tone detector
    unsigned int m_chans;                // Total number of circuits for this span
    unsigned int m_count;                // Circuit count
    unsigned int m_first;                // First circuit code
    unsigned int m_samples;              // Sample count
    unsigned int m_noData;               // Value to send on idle channels
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
	: Thread("Wp Span",prio), m_data(data)
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

#define MAKE_NAME(x) { #x, WpSocket::x }
static TokenDict s_linkStatus[] = {
    MAKE_NAME(Connected),
    MAKE_NAME(Disconnected),
    MAKE_NAME(Connecting),
    {0,0}
};
#undef MAKE_NAME

YSIGFACTORY2(WpInterface);
static Mutex s_ifaceNotify(true,"WPCard::notify"); // WpInterface: lock recv data notification counter
static bool s_repeatCapable = true;      // Global repeat packet capability
static WpModule driver;


static void sendModuleUpdate(bool& notifStat, int status, const String& device)
{
    Message* msg = new Message("module.update");
    msg->addParam("module",driver.name());
    msg->addParam("interface",device);
    if(notifStat && status == SignallingInterface::LinkUp) {
	msg->addParam("notify","interfaceUp");
	notifStat = false;
	Engine::enqueue(msg);
	return;
    }
    if (!notifStat && status == SignallingInterface::LinkDown) {
	msg->addParam("notify","interfaceDown");
	notifStat = true;
	Engine::enqueue(msg);
	return;
    }
    TelEngine::destruct(msg);
}

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
    lock();
    unsigned int errors = 0;
    while (length--)
	if (put(*buf++))
	    errors++;
    unlock();
    return errors;
}

unsigned char Fifo::get()
{
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
    m_status(Disconnected),
    m_card(card),
    m_device(device),
#ifdef HAVE_WANPIPE_HWEC
    m_echoCanAvail(true),
#else
    m_echoCanAvail(false),
#endif
    m_canRead(false),
    m_canWrite(false),
    m_event(false),
    m_readError(false),
    m_writeError(false),
    m_selectError(false)
{
}

// Set echo canceller availability
void WpSocket::echoCanAvail(bool val)
{
#ifdef HAVE_WANPIPE_HWEC
    m_echoCanAvail = val;
#endif
}

// Set echo canceller and tone detection if available
bool WpSocket::echoCancel(bool enable, unsigned long chanmap)
{
    if (!m_echoCanAvail) {
	Debug(m_dbg,DebugNote,
	    "WpSocket(%s/%s). Echo canceller is unavailable. Can't %s it [%p]",
	    m_card.c_str(),m_device.c_str(),enable?"enable":"disable",this);
	return false;
    }

    bool ok = false;

#ifdef HAVE_WANPIPE_HWEC
    int fd = -1;
    String dev;
    dev << WANEC_DEV_DIR << WANEC_DEV_NAME;
    for (int i = 0; i < 5; i++) {
	fd = ::open(dev,O_RDONLY);
	if (fd >= 0)
	    break;
	Thread::msleep(200);
    }
    const char* operation = 0;
    if (fd >= 0) {
	wan_ec_api_t ecapi;
	::memset(&ecapi,0,sizeof(ecapi));
#ifdef HAVE_WANPIPE_HWEC_3310
	ecapi.fe_chan_map = chanmap;
#else
	ecapi.channel_map = chanmap;
#endif
	if (enable) {
	    ecapi.cmd = WAN_EC_CMD_DTMF_ENABLE;
	    ecapi.verbose = WAN_EC_VERBOSE_EXTRA1;
	    // event on start of tone, before echo canceller
#ifdef NEW_WANPIPE_API
	    ecapi.u_tone_config.type = WAN_EC_TONE_PRESENT;
	    ecapi.u_tone_config.port_map = WAN_EC_CHANNEL_PORT_SOUT;
#else
	    ecapi.u_dtmf_config.type = WAN_EC_TONE_PRESENT;
#ifdef HAVE_WANPIPE_HWEC_3310
	    ecapi.u_dtmf_config.port_map = WAN_EC_CHANNEL_PORT_SOUT;
#else
	    ecapi.u_dtmf_config.port = WAN_EC_CHANNEL_PORT_SOUT;
#endif
#endif
	}
	else
	    ecapi.cmd = WAN_EC_CMD_DTMF_DISABLE;
	ecapi.err = WAN_EC_API_RC_OK;
	if (::ioctl(fd,ecapi.cmd,ecapi))
	    operation = "IOCTL";
    }
    else
	operation = "Open";
    ok = (0 == operation);
    if (!ok && m_dbg && m_dbg->debugAt(DebugNote))
	Debug(m_dbg,DebugNote,
	    "WpSocket(%s/%s). %s failed dev=%s. Can't %s echo canceller. %d: %s [%p]",
	    m_card.c_str(),m_device.c_str(),operation,dev.c_str(),(enable?"enable":"disable"),
	    errno,::strerror(errno),this);
    ::close(fd);
#endif

#ifdef DEBUG
    if (ok && m_dbg && m_dbg->debugAt(DebugInfo)) {
	String map('0',32);
	for (unsigned int i = 0; i < 32; i++)
	    if (chanmap & (1 << i))
		((char*)(map.c_str()))[i] = '1';
	DDebug(m_dbg,DebugInfo,
	    "WpSocket(%s/%s). %sabled echo canceller chanmap=%s [%p]",
	    m_card.c_str(),m_device.c_str(),enable?"En":"Dis",map.c_str(),this);
    }
#endif

    return ok;
}

// Set tone detection if available
bool WpSocket::dtmfDetect(bool enable)
{
    bool ok = false;

    // TODO: new API has different event handling style, may need total rewrite
#if defined(HAVE_WANPIPE_HWEC) && !defined(NEW_WANPIPE_API)
    api_tx_hdr_t a;
    ::memset(&a,0,sizeof(api_tx_hdr_t));
    a.wp_api_tx_hdr_event_type = WP_API_EVENT_DTMF;
    a.wp_api_tx_hdr_event_mode = enable ? WP_API_EVENT_ENABLE : WP_API_EVENT_DISABLE;
    ok = (::ioctl(m_socket.handle(),SIOC_WANPIPE_API,&a) >= 0);
#else
    // pretend enabling fails, disabling succeeds
    if (!enable)
	ok = true;
    else
	errno = ENOSYS;
#endif

    if (ok)
	DDebug(m_dbg,DebugInfo,
	    "WpSocket(%s/%s). %sabled tone detector [%p]",
	     m_card.c_str(),m_device.c_str(),enable?"En":"Dis",this);
    else
	showError("dtmfDetect");
    return ok;
}

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
	showError("Read");
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
bool WpSocket::select(unsigned int multiplier, bool checkWrite)
{
    m_canRead = m_canWrite = m_event = false;
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = multiplier * WPSOCKET_SELECT_TIMEOUT;
    if (m_socket.select(&m_canRead,(checkWrite ? &m_canWrite : 0),&m_event,&tv)) {
	m_selectError = false;
	return true;
    }
    if (m_selectError)
	return false;
    showError("Select");
    m_selectError = true;
    return false;
}

// Update the state of the link and return true if changed
bool WpSocket::updateLinkStatus()
{
    LinkStatus old = m_status;
    if (valid())
	switch (::ioctl(m_socket.handle(),SIOC_WANPIPE_SOCK_STATE,0)) {
	    case 0:
		m_status = Connected;
		break;
	    case 1:
		m_status = Disconnected;
		break;
	    default:
		m_status = Connecting;
	}
    else
	m_status = Disconnected;
    return m_status != old;
}


/**
 * WpInterface
 */
// Create WpInterface or WpSpan
SignallingComponent* WpInterface::create(const String& type, NamedList& name)
{
    const String* module = name.getParam("module");
    if (module && *module != "wpcard") {
	DDebug(&driver,DebugWarn,"We aren't the target for creating %s",type.c_str());
	return 0;
    }
    bool interface = false;
    if (type == "SignallingInterface")
	interface = true;
    else if (type == "SignallingCircuitSpan")
	;
    else
	return 0;

    TempObjectCounter cnt(driver.objectsCounter());
    Configuration cfg(Engine::configFile("wpcard"));
    const char* sectName = name.getValue((interface ? "sig" : "voice"),name.getValue("basename",name));
    NamedList* config = cfg.getSection(sectName);

    if (!name.getBoolValue(YSTRING("local-config"),false))
	config = &name;
    else if (!config) {
	DDebug(&driver,DebugConf,"No section '%s' in configuration",c_safe(sectName));
	return 0;
    } else
	name.copyParams(*config);

#ifdef DEBUG
    if (driver.debugAt(DebugAll)) {
	String tmp;
	config->dump(tmp,"\r\n  ",'\'',true);
	Debug(&driver,DebugAll,"WpInterface::create %s%s",
	    (interface ? "interface" : "span"),tmp.c_str());
    }
#endif
    if (interface) {
	WpInterface* iface = new WpInterface(name);
	if (iface->init(*config,(NamedList&)name))
	    return iface;
	TelEngine::destruct(iface);
	return 0;
    }
    NamedList* general = cfg.getSection("general");
    NamedList dummy("general");
    WpSpan* data = new WpSpan(name,sectName);
    if (data->init(*config,general?*general:dummy,(NamedList&)name))
	return data;
    TelEngine::destruct(data);
    return 0;
}

WpInterface::WpInterface(const NamedList& params)
    : SignallingComponent(params,&params,"tdm"),
      m_socket(this),
      m_thread(0),
      m_readOnly(false),
      m_notify(0),
      m_overRead(0),
      m_lastError(0),
      m_sendReadOnly(false),
      m_timerRxUnder(0),
      m_repeatCapable(s_repeatCapable),
      m_repeatMutex(true,"WpInterface::repeat")
{
    DDebug(this,DebugAll,"WpInterface::WpInterface() [%p]",this);
}

WpInterface::~WpInterface()
{
    cleanup(false);
    DDebug(this,DebugAll,"WpInterface::~WpInterface() [%p]",this);
}

bool WpInterface::init(const NamedList& config, NamedList& params)
{
    // Set socket card / device
    m_socket.card(!params.null() ? params : config);
    const char* sig = params.getValue("siggroup",config.getValue("siggroup"));
    if (!(sig && *sig)) {
	Debug(this,DebugWarn,
	    "Missing or invalid siggroup='%s' in configuration [%p]",
	    c_safe(sig),this);
	return false;
    }
    m_socket.device(sig);

    m_readOnly = params.getBoolValue("readonly",config.getBoolValue("readonly",false));

    int i = params.getIntValue("errormask",config.getIntValue("errormask",WP_ERR_MASK));
    m_errorMask = ((i >= 0 && i < 256) ? i : WP_ERR_MASK);

    int rx = params.getIntValue("rxunderrun");
    if (rx > 0)
	m_timerRxUnder.interval(rx);

    m_repeatCapable = params.getBoolValue("hwrepeatcapable",
	config.getBoolValue("hwrepeatcapable",m_repeatCapable));

    if (debugAt(DebugInfo)) {
	String s;
	s << "driver=" << driver.debugName();
	s << " section=" << config.c_str();
	s << " type=" << config.getValue("type","T1");
	s << " card=" << m_socket.card();
	s << " device=" << m_socket.device();
	s << " errormask=" << (unsigned int)m_errorMask;
	s << " readonly=" << String::boolText(m_readOnly);
	s << " rxunderruninterval=" << (unsigned int)m_timerRxUnder.interval() << "ms";
	s << " hwrepeatcapable=" << String::boolText(m_repeatCapable);
	Debug(this,DebugInfo,"D-channel: %s [%p]",s.c_str(),this);
    }
    m_down = false;
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

    m_repeatMutex.lock();
    m_repeatPacket.clear();
    m_repeatMutex.unlock();

    DataBlock data(0,WP_HEADER);
    data += packet;

    // using a while is a hack so we can break out of it
    while (repeat) {
#ifdef wp_api_tx_hdr_hdlc_rpt_data
	if (m_repeatCapable) {
	    if (packet.length() <= WP_RPT_MAXDATA) {
		unsigned char* hdr = (unsigned char*)data.data();
#ifdef NEW_WANPIPE_API
		((wp_api_hdr_t*)hdr)->wp_api_tx_hdr_hdlc_rpt_repeat = 1;
		((wp_api_hdr_t*)hdr)->wp_api_tx_hdr_hdlc_rpt_len = packet.length();
		::memcpy(((wp_api_hdr_t*)hdr)->wp_api_tx_hdr_hdlc_rpt_data,
		    packet.data(),packet.length());
#else
		hdr[WP_RPT_REPEAT] = 1;
		hdr[WP_RPT_LEN] = packet.length();
		::memcpy(hdr+WP_RPT_DATA,packet.data(),packet.length());
#endif
	    }
	    else
		Debug(this,DebugWarn,"Can't repeat packet (type=%u) with length=%u",
		    type,packet.length());
	    break;
	}
#endif
	m_repeatMutex.lock();
	m_repeatPacket = data;
	m_repeatMutex.unlock();
	break;
    }

    return -1 != m_socket.send(data.data(),data.length(),0);
}

// Receive signalling packet
// Send repeated packet if needed
bool WpInterface::receiveAttempt()
{
    if (!m_socket.valid())
	return false;

    if (!m_socket.select(WPSOCKET_SELECT_SAMPLES,(0 != m_repeatPacket.length())))
	return false;
    m_repeatMutex.lock();
    if (m_socket.canWrite() && m_repeatPacket.length())
	m_socket.send(m_repeatPacket.data(),m_repeatPacket.length(),0);
    m_repeatMutex.unlock();
    updateStatus();
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
#ifdef NEW_WANPIPE_API
	unsigned char err = ((wp_api_hdr_t*)buf)->wp_api_rx_hdr_error_map;
#else
	unsigned char err = buf[WP_RD_ERROR];
#endif
	if (err != m_lastError) {
	    m_lastError = err;
	    if (err) {
		String errText;
		if (err & WP_ERR_CRC)
		    errText.append("CRC");
		if (err & WP_ERR_FIFO)
		    errText.append("RxOver"," ");
		if (err & WP_ERR_ABORT)
		    errText.append("Align"," ");
		if (errText)
		    errText = " (" + errText + ")";
		Debug(this,DebugWarn,"Packet got error: %u%s [%p]",
		    err,errText.safe(),this);
	    }
	}
	err &= m_errorMask;
	if (err) {
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

	DataBlock data(buf+WP_HEADER,r,false);
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

// Update link status. Notify the receiver if state changed
bool WpInterface::updateStatus()
{
    if (!m_socket.updateLinkStatus())
	return false;
    Debug(this,DebugNote,"Link status changed to %s [%p]",
	lookup(m_socket.status(),s_linkStatus),this);
    if (m_socket.status() == WpSocket::Connected) {
	notify(LinkUp);
	sendModuleUpdate(m_down,LinkUp,m_socket.card());
    }
    else {
	notify(LinkDown);
	sendModuleUpdate(m_down,LinkDown,m_socket.card());
    }
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
    m_interface->updateStatus();
    for (;;) {
	Thread::yield(true);
	while (m_interface && m_interface->receiveAttempt())
	    Thread::check(true);
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
    : SignallingCircuit(TDM,code,Idle,group,data), Mutex(true,"WpCircuit"),
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
    XDebug(group(),DebugAll,"WpCircuit::~WpCircuit(%u) [%p]",code(),this);
    Lock lock(this);
    status(Missing);
    TelEngine::destruct(m_source);
    TelEngine::destruct(m_consumer);
}

// Change circuit status. Clear events on succesfully changes status
// Connected: Set valid source and consumer
// Otherwise: Invalidate and reset source and consumer
bool WpCircuit::status(Status newStat, bool sync)
{
    Lock lock(this);
    if (SignallingCircuit::status() == newStat)
	return true;
    TempObjectCounter cnt(driver.objectsCounter());
    // Allow status change for the following values
    switch (newStat) {
	case Missing:
	case Disabled:
	case Idle:
	case Reserved:
	    m_specialMode.clear();
	    // fall through
	case Special:
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
    if (SignallingCircuit::status() >= Special)
	enableData = true;
    // Don't put this message for final states
    if (!Engine::exiting())
	DDebug(group(),DebugAll,"WpCircuit(%u). Changed status to '%s' [%p]",
	    code(),lookupStatus(newStat),this);
    if (enableData) {
	m_sourceValid = m_source;
	m_consumerValid = m_consumer;
	if (newStat == Special) {
	    Message m("circuit.special");
	    m.userData(this);
	    if (group())
		m.addParam("group",group()->toString());
	    if (span())
		m.addParam("span",span()->toString());
	    if (m_specialMode)
		m.addParam("mode",m_specialMode);
	    return Engine::dispatch(m);
	}
	return true;
    }
    // Disable data if not already disabled
    if (m_consumerValid) {
	m_consumerValid = 0;
	if (oldStat == Connected) {
	    XDebug(group(),DebugAll,"WpCircuit(%u). Consumer transferred %u byte(s) [%p]",
		code(),m_consumer->m_total,this);
	    if (m_consumer->m_errorCount)
		DDebug(group(),DebugMild,"WpCircuit(%u). Consumer errors: %u. Lost: %u/%u [%p]",
		    code(),m_consumer->m_errorCount,m_consumer->m_errorBytes,
		    m_consumer->m_total,this);
	}
	m_consumer->clear();
	m_consumer->m_errorCount = m_consumer->m_errorBytes = 0;
	m_consumer->m_total = 0;
    }
    if (m_sourceValid) {
	m_sourceValid = 0;
	if (oldStat == Connected)
	    XDebug(group(),DebugAll,"WpCircuit(%u). Source transferred %u byte(s) [%p]",
		code(),m_source->m_total,this);
	m_source->clear();
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
    Lock lock(this);
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

bool WpCircuit::setParam(const String& param, const String& value)
{
    TempObjectCounter cnt(driver.objectsCounter());
    if (param == "special_mode")
	m_specialMode = value;
    else
	return false;
    return true;
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
    return SignallingCircuit::getObject(name);
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
WpSpan::WpSpan(const NamedList& params, const char* debugname)
    : SignallingCircuitSpan(params.getValue("debugname",debugname),
	static_cast<SignallingCircuitGroup*>(params.getObject("SignallingCircuitGroup"))),
    m_socket(m_group),
    m_thread(0),
    m_canSend(true),
    m_swap(false),
    m_chanMap(0),
    m_echoCancel(false),
    m_dtmfDetect(false),
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
    DDebug(m_group,DebugAll,"WpSpan::WpSpan(). Name '%s' [%p]",id().safe(),this);
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
    clearCircuits();
    if (m_buffer)
	delete[] m_buffer;
    DDebug(m_group,DebugAll,"WpSpan::~WpSpan() [%p]",this);
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
    m_socket.card(!params.null() ? params : config);
    const char* voice = params.getValue("voicegroup",config.getValue("voicegroup"));
    if (!voice) {
	Debug(m_group,DebugNote,"WpSpan('%s'). Missing or invalid voice group [%p]",
	    id().safe(),this);
	return false;
    }
    m_socket.device(voice);
    m_canSend = !params.getBoolValue("readonly",config.getBoolValue("readonly",false));
    // Type depending data: channel count, samples, circuit list
    String type = params.getValue("type",config.getValue("type"));
    String cics = params.getValue("voicechans",config.getValue("voicechans"));
    unsigned int start = params.getIntValue("offset",config.getIntValue("offset",0));
    start += params.getIntValue("start");
    start = config.getIntValue("start",start);
    m_samples = params.getIntValue("samples",config.getIntValue("samples"));
    int idleValue = 0xd5; // A-Law idle code
    if (type.null())
	type = "E1";
    if (type == "E1") {
	m_chans = 31;
	m_increment = 32;
	if (cics.null())
	    cics = "1-15,17-31";
	if (!m_samples)
	    m_samples = 50;
    }
    else if (type == "T1") {
	idleValue = 0xff; // mu-Law idle code
	m_chans = 24;
	m_increment = 24;
	if (cics.null())
	    cics = "1-23";
	if (!m_samples)
	    m_samples = 64;
    }
    else if (type == "BRI") {
	m_chans = 3;
	m_increment = 3;
	if (cics.null())
	    cics = "1-2";
	if (!m_samples)
	    m_samples = 80;
    }
    else {
	Debug(m_group,DebugNote,"WpSpan('%s'). Invalid voice group type '%s' [%p]",
	    id().safe(),type.safe(),this);
	return false;
    }
    m_increment = config.getIntValue("increment",m_increment);

    // Other data
    m_swap = defaults.getBoolValue("bitswap",true);
    m_noData = defaults.getIntValue("idlevalue",idleValue);
    m_buflen = defaults.getIntValue("buflen",160);
    m_swap = params.getBoolValue("bitswap",config.getBoolValue("bitswap",m_swap));
    m_noData = params.getIntValue("idlevalue",config.getIntValue("idlevalue",m_noData));
    m_buflen = params.getIntValue("buflen",config.getIntValue("buflen",m_buflen));
    bool tmpDefault = defaults.getBoolValue("echocancel",config.getBoolValue("echocancel",false));
    m_echoCancel = params.getBoolValue("echocancel",tmpDefault);
    tmpDefault = defaults.getBoolValue("dtmfdetect",config.getBoolValue("dtmfdetect",false));
    m_dtmfDetect = params.getBoolValue("dtmfdetect",tmpDefault);

    // Buffer length can't be 0
    if (!m_buflen)
	m_buflen = 160;
    // Channels
    if (!createCircuits(start,cics)) {
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
	if (m_noData < 256)
	    s << " idlevalue=" << m_noData;
	else
	    s << " idlevalue=(circuit)";
	s << " buflen=" << (unsigned int)m_buflen;
	s << " echocancel=" << String::boolText(m_echoCancel);
	s << " dtmfdetect=" << String::boolText(m_dtmfDetect);
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
    clearCircuits();
    m_circuits = new WpCircuit*[m_count];
    unsigned int i;
    for (i = 0; i < m_count; i++)
	m_circuits[i] = 0;
    bool ok = true;
    m_chanMap = 0;
    for (i = 0; i < m_count; i++) {
	m_circuits[i] = new WpCircuit(delta + cicCodes[i],m_group,this,m_buflen,cicCodes[i]);
	if (m_group->insert(m_circuits[i])) {
	    m_circuits[i]->ref();
	    if (m_circuits[i]->channel())
		m_chanMap |= ((unsigned long)1 << (m_circuits[i]->channel() - 1));
	    continue;
	}
	// Failure
	Debug(m_group,DebugNote,
	    "WpSpan('%s'). Failed to create/insert circuit %u. Rollback [%p]",
	    id().safe(),cicCodes[i],this);
	m_group->removeSpan(this,true,false);
	clearCircuits();
	ok = false;
	break;
    }
    delete[] cicCodes;
    return ok;
}

void WpSpan::clearCircuits()
{
    WpCircuit** circuits = m_circuits;
    m_circuits = 0;
    if (!circuits)
	return;
    for (unsigned int i = 0; i < m_count; i++)
	TelEngine::destruct(circuits[i]);
    delete[] circuits;
}

// Read events and data from socket. Send data when succesfully read
// Received data is splitted for each circuit
// Sent data from each circuit is merged into one data block
void WpSpan::run()
{
    if (!m_socket.open(true))
	return;
    // Set echo canceller / tone detector
    if (m_socket.echoCancel(m_echoCancel,m_chanMap))
	m_socket.dtmfDetect(m_dtmfDetect);
    if (!m_buffer) {
	m_bufferLen = WP_HEADER + m_samples * m_count;
	m_buffer = new unsigned char[m_bufferLen];
    }
    DDebug(m_group,DebugInfo,
	"WpSpan('%s'). Worker is running: circuits=%u, buffer=%u, samples=%u [%p]",
	id().safe(),m_count,m_bufferLen,m_samples,this);
    updateStatus();
    while (true) {
	Thread::check(true);
	if (!m_socket.select(m_samples))
	    continue;
	updateStatus();
	if (m_socket.event())
	    readEvent();
	if (!m_socket.canRead())
	    continue;
	int r = readData();
	if (r == -1)
	    continue;
	r -= WP_HEADER;
#ifdef NEW_WANPIPE_API
	if (r == WAN_MAX_EVENT_SZ) {
	    // Got an event
	    wp_api_event_t* ev = (wp_api_event_t*)(m_buffer + WP_HEADER);
	    SignallingCircuitEvent* e = 0;
	    WpCircuit* circuit = 0;
	    switch (ev->wp_api_event_type) {
		case WP_API_EVENT_DTMF:
		    if (ev->wp_api_event_dtmf_type == WAN_EC_TONE_PRESENT) {
			String tone((char)ev->wp_api_event_dtmf_digit);
			tone.toUpper();
			int chan = ev->wp_api_event_channel;
			circuit = find(chan);
			if (circuit) {
			    e = new SignallingCircuitEvent(circuit,SignallingCircuitEvent::Dtmf,"DTMF");
			    e->addParam("tone",tone);
			}
			else
			    Debug(m_group,DebugMild,
				"WpSpan('%s'). Detected DTMF '%s' for invalid channel %d [%p]",
				id().safe(),tone.c_str(),chan,this);
		    }
		    break;
#ifdef DEBUG
		default:
		    {
			String tmp;
			tmp.hexify(m_buffer + WP_HEADER,r,' ');
			Debug(m_group,DebugAll,"Event %u: %s",ev->wp_api_event_type,tmp.c_str());
		    }
		    break;
#endif
	    }
	    if (e)
		(static_cast<WpCircuit*>(e->circuit()))->enqueueEvent(e);
	    continue;
	}
#endif
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
	if (m_canSend) {
	    for (unsigned int i = 0; i < m_count; i++) {
		WpCircuit* circuit = m_circuits[i];
		// Forward read data if we have a source
		WpSource* s = (circuit && circuit->validSource()) ? circuit->source() : 0;
		if (s) {
		    unsigned char* dat = m_buffer + WP_HEADER + i;
		    for (int n = samples; n > 0; n--, dat += m_count)
			s->put(swap(*dat));
		}
		// Fill send buffer for current circuit
		unsigned char* dat = m_buffer + WP_HEADER + i;
		WpConsumer* c = (circuit && circuit->validConsumer()) ? circuit->consumer() : 0;
		if (c) {
		    Lock lock(c);
		    for (int n = samples; n > 0; n--, dat += m_count)
			*dat = swap(c->get());
		}
		else {
		    unsigned char noData = swap((m_noData < 256 || !circuit) ? m_noData : circuit->code() & 0xff);
		    for (int n = samples; n > 0; n--, dat += m_count)
			*dat = noData;
		}
	    }
	    ::memset(m_buffer,0,WP_HEADER);
	    m_socket.send(m_buffer,WP_HEADER + samples * m_count,MSG_DONTWAIT);
	}
	else {
	    for (unsigned int i = 0; i < m_count; i++) {
		WpCircuit* circuit = m_circuits[i];
		if (!(circuit && circuit->validSource()))
		    continue;
		WpSource* s = circuit->source();
		unsigned char* dat = m_buffer + WP_HEADER + i;
		for (int n = samples; n > 0; n--, dat += m_count)
		    s->put(swap(*dat));
	    }
	}
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
#ifdef NEW_WANPIPE_API
    ((wp_api_hdr_t*)m_buffer)->wp_api_rx_hdr_error_map = 0;
#else
    m_buffer[WP_RD_ERROR] = 0;
#endif
    int r = m_socket.recv(m_buffer,m_bufferLen);
    // Check errors
    if (r == -1)
	return -1;
    if (r < WP_HEADER) {
	Debug(m_group,DebugGoOn,"WpSpan('%s'). Short read %u byte(s) [%p]",
	    id().safe(),r,this);
	return -1;
    }
#ifdef NEW_WANPIPE_API
    unsigned char err = ((wp_api_hdr_t*)m_buffer)->wp_api_rx_hdr_error_map;
#else
    unsigned char err = m_buffer[WP_RD_ERROR];
#endif
    if (err) {
	m_readErrors++;
	if (m_readErrors == MAX_READ_ERRORS) {
	    Debug(m_group,DebugGoOn,"WpSpan('%s'). Read error 0x%02X [%p]",
		id().safe(),err,this);
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
    // TODO: new API has different event handling style, may need total rewrite
#if defined(WAN_EC_TONE_PRESENT) && !defined(NEW_WANPIPE_API)
    api_rx_hdr_t* ev = (api_rx_hdr_t*)m_buffer;
    SignallingCircuitEvent* e = 0;
    WpCircuit* circuit = 0;
    switch (ev->event_type) {
	case WP_API_EVENT_NONE:
	    return false;
	case WP_API_EVENT_DTMF:
	    if (ev->hdr_u.wp_api_event.u_event.dtmf.type == WAN_EC_TONE_PRESENT) {
		String tone((char)ev->hdr_u.wp_api_event.u_event.dtmf.digit);
		tone.toUpper();
		int chan = ev->hdr_u.wp_api_event.channel;
		circuit = find(chan);
		if (circuit) {
		    e = new SignallingCircuitEvent(circuit,SignallingCircuitEvent::Dtmf,"DTMF");
		    e->addParam("tone",tone);
		}
		else
		    Debug(m_group,DebugMild,
			"WpSpan('%s'). Detected DTMF '%s' for invalid channel %d [%p]",
			id().safe(),tone.c_str(),chan,this);
	    }
	    break;
	default:
	    Debug(m_group,DebugStub,"WpSpan('%s'). Unhandled event %u [%p]",
		id().safe(),ev->event_type,this);
    }
    if (e)
	(static_cast<WpCircuit*>(e->circuit()))->enqueueEvent(e);
    return true;
#else
    return false;
#endif
}

// Update link status. Notify the receiver if state changed
bool WpSpan::updateStatus()
{
    if (!m_socket.updateLinkStatus())
	return false;
    const char* evName = lookup(m_socket.status(),s_linkStatus);
    Debug(m_group,DebugNote,"WpSpan('%s'). Link status changed to %s [%p]",
	id().safe(),evName,this);
    SignallingCircuitEvent::Type evType = SignallingCircuitEvent::NoAlarm;
    if (m_socket.status() != WpSocket::Connected)
	evType = SignallingCircuitEvent::Alarm;
    for (unsigned int i = 0; i < m_count; i++)
	if (m_circuits[i]) {
	    SignallingCircuitEvent* e = new SignallingCircuitEvent(m_circuits[i],evType,evName);
	    if (evType == SignallingCircuitEvent::Alarm)
		e->addParam("alarms","red");
	    m_circuits[i]->enqueueEvent(e);
    }
    return true;
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

    Configuration cfg(Engine::configFile("wpcard"));
    s_repeatCapable = cfg.getBoolValue("general","hwrepeatcapable",s_repeatCapable);

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
