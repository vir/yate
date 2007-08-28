/**
 * zapcard.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Zaptel PRI cards signalling and data driver
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
#ifdef NEW_ZAPTEL_LOCATION
#define __LINUX__
#include <zaptel/zaptel.h>
#else
#include <linux/zaptel.h>
#endif
};

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#define ZAP_ERR_OVERRUN 0x01             // Flags used to filter interface errors
#define ZAP_ERR_ABORT   0x02

using namespace TelEngine;
namespace { // anonymous

class ZapWorkerClient;                   // Worker thread client (implements process())
class ZapWorkerThread;                   // Worker thread (calls client's process() in a loop)
class ZapDevice;                         // Zaptel I/O device. Implements the interface with the Zaptel driver
class ZapInterface;                      // D-channel signalling interface
class ZapSpan;                           // Signalling span used to create voice circuits
class ZapCircuit;                        // A voice circuit
class ZapAnalogCircuit;                  // A analog circuit
class ZapSource;                         // Data source
class ZapConsumer;                       // Data consumer
class ZapModule;                         // The driver

static const char* s_zapDevName = "//dev/zap/channel";
static const char* s_threadName = "ZapWorkerThread";

static Mutex s_ifaceNotify(true);        // ZapInterface: lock recv data notification counter

#define ZAP_CRC_LEN 2                    // The length of the CRC field in signalling packets

// Worker thread client (implements process())
class ZapWorkerClient
{
    friend class ZapWorkerThread;
public:
    virtual ~ZapWorkerClient() { stop(); }
    bool running() const;
    // Return true to tell the worker to call again
    // Return false to yield
    virtual bool process() = 0;
protected:
    inline ZapWorkerClient() : m_thread(0) {}
    // Start thread if not started
    bool start(Thread::Priority prio, DebugEnabler* dbg, const String& addr);
    // Stop thread if started
    void stop();
private:
    ZapWorkerThread* m_thread;
};

// Worker thread (calls client's process() in a loop)
class ZapWorkerThread : public Thread
{
public:
    inline ZapWorkerThread(ZapWorkerClient* client, const String& addr, Priority prio = Normal)
	: Thread(s_threadName,prio), m_client(client), m_address(addr)
	{}
    virtual ~ZapWorkerThread();
    // Call client's process() in a loop
    virtual void run();
private:
    ZapWorkerClient* m_client;
    String m_address;
};

// I/O device
class ZapDevice
{
public:
    // Flags to check alarms
    enum Alarm {
	Recover  = ZT_ALARM_RECOVER,     // Recovering from alarm
	Loopback = ZT_ALARM_LOOPBACK,    // In loopback
	Yellow   = ZT_ALARM_YELLOW,
	Red      = ZT_ALARM_RED,
	Blue     = ZT_ALARM_BLUE,
	NotOpen  = ZT_ALARM_NOTOPEN
    };

    // List of events
    enum Event {
	None = ZT_EVENT_NONE,
	OnHook = ZT_EVENT_ONHOOK,
	OffHookRing = ZT_EVENT_RINGOFFHOOK,
	WinkFlash = ZT_EVENT_WINKFLASH,
	Alarm = ZT_EVENT_ALARM,
	NoAlarm = ZT_EVENT_NOALARM,
	HdlcAbort = ZT_EVENT_ABORT,
	HdlcOverrun = ZT_EVENT_OVERRUN,
	BadFCS = ZT_EVENT_BADFCS,
	DialComplete = ZT_EVENT_DIALCOMPLETE,
	RingerOn = ZT_EVENT_RINGERON,
	RingerOff = ZT_EVENT_RINGEROFF,
	HookComplete = ZT_EVENT_HOOKCOMPLETE,
	BitsChanged = ZT_EVENT_BITSCHANGED,  // Bits changing on a CAS/User channel
	PulseStart = ZT_EVENT_PULSE_START,   // Beginning of a pulse coming on its way
	Timeout = ZT_EVENT_TIMER_EXPIRED,
	TimerPing = ZT_EVENT_TIMER_PING,
	RingBegin = ZT_EVENT_RINGBEGIN,
	Polarity = ZT_EVENT_POLARITY,        // Polarity reversal event
	// These are event masks
	PulseDigit = ZT_EVENT_PULSEDIGIT,    // This is OR'd with the digit received
	DtmfDown = ZT_EVENT_DTMFDOWN,        // Ditto for DTMF key down event
	DtmfUp = ZT_EVENT_DTMFUP,            // Ditto for DTMF key up event
	DtmfEvent = ZT_EVENT_PULSEDIGIT | ZT_EVENT_DTMFDOWN | ZT_EVENT_DTMFUP
    };

    // List of hook to send events
    enum HookEvent {
	HookOn      = ZT_ONHOOK,
	HookOff     = ZT_OFFHOOK,
	HookWink    = ZT_WINK,
	HookFlash   = ZT_FLASH,
	HookStart   = ZT_START,
	HookRing    = ZT_RING,
	HookRingOff = ZT_RINGOFF
    };

    // List of valid IOCTL requests
    enum IoctlRequest {
	SetChannel     = 0,              // Specify a channel number for an opened device
	SetBlkSize     = 1,              // Set data I/O block size
	SetBuffers     = 2,              // Set buffers
	SetFormat      = 3,              // Set format
	SetAudioMode   = 4,              // Set audio mode
	SetEchoCancel  = 5,              // Set echo cancel
	SetDial        = 6,              // Append, replace, or cancel a dial string
	SetHook        = 7,              // Set Hookswitch Status
#ifdef ZT_TONEDETECT
	SetToneDetect  = 8,              // Set tone detection
#else
	SetToneDetect  = -1,
#endif
	SetLinear      = 9,              // Temporarily set the channel to operate in linear mode
	GetParams      = 10,             // Get device parameters
	GetEvent       = 11,             // Get events from device
	GetInfo        = 12,             // Get device status
	StartEchoTrain = 13,             // Start echo training
	FlushBuffers   = 14,             // Flush read/write buffers
    };

    // Zaptel formats
    enum Format {
	Slin    = -1,
	Default = ZT_LAW_DEFAULT,
	Mulaw   = ZT_LAW_MULAW,
	Alaw    = ZT_LAW_ALAW
    };

    // Circuit type used to create circuits and interface
    enum Type {
	E1,
	T1,
	FXO,
	FXS
    };

    ZapDevice(SignallingComponent* dbg, unsigned int chan, unsigned int circuit, bool interface);
    inline ~ZapDevice()
	{ close(); }
    inline bool valid() const
	{ return m_handle >= 0; }
    inline unsigned int channel() const
	{ return m_channel; }
    void channel(unsigned int chan, unsigned int circuit);
    inline int alarms() const
	{ return m_alarms; }
    inline void resetAlarms()
	{ m_alarms = 0; }
    inline bool canRead() const
	{ return m_canRead; }
    inline bool event() const
	{ return m_event; }
    // Open the device. Specify channel to use.
    // Circuit: Set block size (ignore numbufs)
    // Interface: Check channel mode. Set buffers
    bool open(unsigned int numbufs, unsigned int bufsize);
    // Close device. Reset handle
    void close();
    // Set data format. Fails if called for an interface
    bool setFormat(Format format);
    // Set/unset tone detection
    bool setDtmfDetect(bool detect);
    // Update echo canceller (disable if taps is 0)
    bool setEchoCancel(bool enable, unsigned int taps);
    // Start echo canceller training for a given period of time (in miliseconds)
    bool startEchoTrain(unsigned int period);
    // Send hook events
    bool sendHook(HookEvent event);
    // Send DTMFs events
    bool sendDtmf(const char* tone);
    // Get an event. Return 0 if no events. Set dtmf if the event is a DTMF
    int getEvent(char& dtmf);
    // Get alarms from this device. Return true if alarms changed
    bool getAlarms(String* alarms);
    // 
    inline bool setLinear(int val, int level = DebugWarn)
	{ return ioctl(SetLinear,&val,level); }
    // Flush read and write buffers
    bool flushBuffers();
    // Check if received data. Wait usec microseconds before returning
    bool select(unsigned int usec);
    // Receive data. Return -1 on error or the number of bytes read
    // If -1 is returned, the caller should check if m_event is set
    int recv(void* buffer, int len);
    // Send data. Return -1 on error or the number of bytes written
    int send(const void* buffer, int len);
protected:
    inline bool canRetry()
	{ return errno == EAGAIN || errno == EINTR; }
    // Make IOCTL requests on this device
    bool ioctl(IoctlRequest request, void* param, int level = DebugWarn);
private:
    SignallingComponent* m_owner;        // Signalling component owning this device
    bool m_interface;                    // True if this is a D-channel
    String m_name;                       // Additional debug name for circuits
    int m_handle;                        // The handler
    unsigned int m_channel;              // The channel this file is used for
    int m_alarms;                        // Device alarms flag
    bool m_canRead;                      // True if there is data to read
    bool m_event;                        // True if an event occurred when recv/select
    bool m_readError;                    // Flag used to print read errors
    bool m_writeError;                   // Flag used to print write errors
    bool m_selectError;                  // Flag used to print select errors
    fd_set m_rdfds;
    fd_set m_errfds;
    struct timeval m_tv;
};

// D-channel signalling interface
class ZapInterface : public SignallingInterface, public ZapWorkerClient
{
public:
    ZapInterface(const NamedList& params);
    virtual ~ZapInterface();
    inline bool valid() const
	{ return m_device.valid() && running(); }
    // Initialize interface. Return false on failure
    bool init(ZapDevice::Type type, unsigned int code, unsigned int channel,
	const NamedList& config, const NamedList& defaults, const NamedList& params);
    // Remove links. Dispose memory
    virtual void destruct()
	{ cleanup(true); }
    // Get this object or an object from the base class
    virtual void* getObject(const String& name) const;
    // Send signalling packet
    virtual bool transmitPacket(const DataBlock& packet, bool repeat, PacketType type);
    // Interface control. Open device and start worker when enabled, cleanup when disabled
    virtual bool control(Operation oper, NamedList* params = 0);
    // Process incoming data
    virtual bool process();
    // Called by the factory to create Zaptel interfaces or spans
    static void* create(const String& type, const NamedList& name);
protected:
    // Check if received any data in the last interval. Notify receiver
    virtual void timerTick(const Time& when);
    // Check for device events. Notify receiver
    void checkEvents();
private:
    inline void cleanup(bool release) {
	    control(Disable,0);
	    attach(0);
	    if (release)
		GenObject::destruct();
	}

    ZapDevice m_device;                  // The device
    Thread::Priority m_priority;         // Worker thread priority
    unsigned char m_errorMask;           // Error mask to filter received error events
    unsigned int m_numbufs;              // The number of buffers used by the channel
    unsigned int m_bufsize;              // The buffer size
    unsigned char* m_buffer;             // Read buffer
    bool m_readOnly;                     // Read only interface
    bool m_sendReadOnly;                 // Print send attempt on readonly interface error
    int m_notify;                        // Notify receiver on channel non idle (0: success. 1: not notified. 2: notified)
    SignallingTimer m_timerRxUnder;      // RX underrun notification
};

// Signalling span used to create voice circuits
class ZapSpan : public SignallingCircuitSpan
{
public:
    inline ZapSpan(const NamedList& params)
	: SignallingCircuitSpan(params.getValue("debugname"),
	    static_cast<SignallingCircuitGroup*>(params.getObject("SignallingCircuitGroup")))
	{}
    virtual ~ZapSpan()
	{}
    // Create circuits. Insert them into the group
    bool init(ZapDevice::Type type, unsigned int offset,
	const NamedList& config, const NamedList& defaults, const NamedList& params);
};

// A voice circuit
class ZapCircuit : public SignallingCircuit, public ZapWorkerClient
{
public:
    ZapCircuit(ZapDevice::Type type, unsigned int code, unsigned int channel,
	ZapSpan* span, const NamedList& config, const NamedList& defaults,
	const NamedList& params);
    virtual ~ZapCircuit()
	{ cleanup(false); }
    inline unsigned int channel() const
	{ return m_device.channel(); }
    virtual void destroyed()
	{ cleanup(true); }
    // Change circuit status. Clear events on status change
    // New status is Connect: Open device. Create source/consumer. Start worker
    // Cleanup on disconnect
    virtual bool status(Status newStat, bool sync = false);
    // Update data format for zaptel device and source/consumer 
    virtual bool updateFormat(const char* format, int direction);
    // Setup echo canceller or start echo canceller training
    virtual bool setParam(const String& param, const String& value);
    // Get circuit data
    virtual bool getParam(const String& param, String& value) const;
    // Get this circuit or source/consumer
    virtual void* getObject(const String& name) const;
    // Process incoming data
    virtual bool process();
    // Send an event
    virtual bool sendEvent(SignallingCircuitEvent::Type type, NamedList* params = 0);
    // Consume data sent by the consumer
    void consume(const DataBlock& data);
protected:
    // Close device. Stop worker. Remove source consumer. Change status. Release memory if requested
    // Reset echo canceller and tone detector if the device is not closed
    void cleanup(bool release, Status stat = Missing, bool stop = true);
    // Update format, echo canceller, dtmf detection
    bool setFormat(ZapDevice::Format format);
    // Get and process some events
    void checkEvents();
    // Process additional events. Return false if not processed
    virtual bool processEvent(int event, char c = 0)
	{ return false; }
    // Create source buffer and data source and consumer
    void createData();
    // Enqueue received events
    bool enqueueEvent(SignallingCircuitEvent* event);
    bool enqueueEvent(int event, SignallingCircuitEvent::Type type);
    // Enqueue received digits
    bool enqueueDigit(bool tone, char digit);

    ZapDevice m_device;                  // The device
    ZapDevice::Type m_type;              // Circuit type
    ZapDevice::Format m_format;          // The data format
    bool m_echoCancel;                   // Echo canceller state
    bool m_crtEchoCancel;                // Current echo canceller state
    unsigned int m_echoTaps;             // Echo cancel taps
    unsigned int m_echoTrain;            // Echo canceller's train period in miliseconds
    bool m_dtmfDetect;                   // Dtmf detection flag
    bool m_crtDtmfDetect;                // Current dtmf detection state
    bool m_canSend;                      // Not a read only circuit
    unsigned char m_idleValue;           // Value used to fill incomplete source buffer
    Thread::Priority m_priority;         // Worker thread priority
    ZapSource* m_source;                 // The data source
    ZapConsumer* m_consumer;             // The data consumer
    DataBlock m_sourceBuffer;            // Data source buffer
    DataBlock m_consBuffer;              // Data consumer buffer
    unsigned int m_buflen;               // Data block length
    unsigned int m_consBufMax;           // Max consumer buffer length
    unsigned int m_consErrors;           // Consumer. Total number of send failures
    unsigned int m_consErrorBytes;       // Consumer. Total number of lost bytes
    unsigned int m_consTotal;            // Consumer. Total number of bytes transferred
};

// An analog circuit
class ZapAnalogCircuit : public ZapCircuit
{
public:
    inline ZapAnalogCircuit(ZapDevice::Type type, unsigned int code, unsigned int channel,
	ZapSpan* span, const NamedList& config, const NamedList& defaults,
	const NamedList& params)
	: ZapCircuit(type,code,channel,span,config,defaults,params),
	m_hook(true)
	{}
    virtual ~ZapAnalogCircuit()
	{}
    // Change circuit status. Clear events on status change
    // Reserved: Open device and start worker if old status is not Connected
    // Connect: Create source/consumer
    // Cleanup on disconnect
    virtual bool status(Status newStat, bool sync);
    // Get circuit data
    virtual bool getParam(const String& param, String& value) const;
    // Send an event
    virtual bool sendEvent(SignallingCircuitEvent::Type type, NamedList* params = 0);
    // Process incoming data
    virtual bool process();
protected:
    // Process additional events. Return false if not processed
    virtual bool processEvent(int event, char c = 0);
    // Change hook state if different
    void changeHook(bool hook);

    bool m_hook;                         // The remote end's hook status
};

// Data source
class ZapSource : public DataSource
{
public:
    ZapSource(ZapCircuit* circuit, const char* format);
    virtual ~ZapSource();
    inline void changeFormat(const char* format)
	{ m_format = format; }
private:
    String m_address;
};

// Data consumer
class ZapConsumer : public DataConsumer
{
    friend class ZapCircuit;
public:
    ZapConsumer(ZapCircuit* circuit, const char* format);
    virtual ~ZapConsumer();
    inline void changeFormat(const char* format)
	{ m_format = format; }
    virtual void Consume(const DataBlock& data, unsigned long tStamp)
	{ if (m_circuit) m_circuit->consume(data); }
private:
    ZapCircuit* m_circuit;
    String m_address;
};

// The driver
class ZapModule : public DebugEnabler
{
public:
    ZapModule();
    ~ZapModule();
};


static ZapModule driver;
YSIGFACTORY2(ZapInterface,SignallingInterface);  // Factory used to create zaptel interfaces and spans


/**
 * ZapWorkerClient
 */
bool ZapWorkerClient::running() const
{
    return m_thread && m_thread->running();
}

bool ZapWorkerClient::start(Thread::Priority prio, DebugEnabler* dbg, const String& addr)
{
    if (!m_thread)
	m_thread = new ZapWorkerThread(this,addr,prio);
    if (m_thread->running())
	return true;
    if (m_thread->startup())
	return true;
    m_thread->cancel(true);
    m_thread = 0;
    Debug(dbg,DebugWarn,"Failed to start %s for %s [%p]",s_threadName,addr.c_str(),dbg);
    return false;
}

void ZapWorkerClient::stop()
{
    if (!m_thread)
	return;
    m_thread->cancel();
    while (m_thread)
	Thread::yield();
}

/**
 * ZapWorkerThread
 */
ZapWorkerThread::~ZapWorkerThread()
{
    DDebug(&driver,DebugAll,"%s is terminated for client (%p): %s",
	s_threadName,m_client,m_address.c_str());
    if (m_client)
	m_client->m_thread = 0;
}

void ZapWorkerThread::run()
{
    if (!m_client)
	return;
    DDebug(&driver,DebugAll,"%s is running for client (%p): %s",
	s_threadName,m_client,m_address.c_str());
    while (true) {
	if (m_client->process())
	    Thread::check(true);
	else
	    Thread::yield(true);
    }
}


/**
 * ZapDevice
 */
static TokenDict s_alarms[] = {
    {"recover",  ZapDevice::Recover},
    {"loopback", ZapDevice::Loopback},
    {"yellow",   ZapDevice::Yellow},
    {"red",      ZapDevice::Red},
    {"blue",     ZapDevice::Blue},
    {"not-open", ZapDevice::NotOpen},
    {0,0}
};

#define MAKE_NAME(x) { #x, ZapDevice::x }
static TokenDict s_events[] = {
    MAKE_NAME(None),
    MAKE_NAME(OnHook),
    MAKE_NAME(OffHookRing),
    MAKE_NAME(WinkFlash),
    MAKE_NAME(Alarm),
    MAKE_NAME(NoAlarm),
    MAKE_NAME(HdlcAbort),
    MAKE_NAME(HdlcOverrun),
    MAKE_NAME(BadFCS),
    MAKE_NAME(DialComplete),
    MAKE_NAME(RingerOn),
    MAKE_NAME(RingerOff),
    MAKE_NAME(HookComplete),
    MAKE_NAME(BitsChanged),
    MAKE_NAME(PulseStart),
    MAKE_NAME(Timeout),
    MAKE_NAME(TimerPing),
    MAKE_NAME(RingBegin),
    MAKE_NAME(Polarity),
    MAKE_NAME(PulseDigit),
    MAKE_NAME(DtmfDown),
    MAKE_NAME(DtmfUp),
    MAKE_NAME(DtmfEvent),
    {0,0}
};

static TokenDict s_hookEvents[] = {
    MAKE_NAME(HookOn),
    MAKE_NAME(HookOff),
    MAKE_NAME(HookWink),
    MAKE_NAME(HookFlash),
    MAKE_NAME(HookStart),
    MAKE_NAME(HookRing),
    MAKE_NAME(HookRingOff),
    {0,0}
};

static TokenDict s_ioctl_request[] = {
    MAKE_NAME(SetChannel),
    MAKE_NAME(SetBlkSize),
    MAKE_NAME(SetBuffers),
    MAKE_NAME(SetFormat),
    MAKE_NAME(SetAudioMode),
    MAKE_NAME(SetDial),
    MAKE_NAME(SetHook),
    MAKE_NAME(SetToneDetect),
    MAKE_NAME(SetLinear),
    MAKE_NAME(GetParams),
    MAKE_NAME(GetEvent),
    MAKE_NAME(GetInfo),
    MAKE_NAME(StartEchoTrain),
    MAKE_NAME(FlushBuffers),
    {0,0}
};

static TokenDict s_types[] = {
    MAKE_NAME(E1),
    MAKE_NAME(T1),
    MAKE_NAME(FXO),
    MAKE_NAME(FXS),
    {0,0}
};
#undef MAKE_NAME

static TokenDict s_formats[] = {
    {"slin",    ZapDevice::Slin},
    {"default", ZapDevice::Default},
    {"mulaw",   ZapDevice::Mulaw},
    {"alaw",    ZapDevice::Alaw},
    {0,0}
    };

ZapDevice::ZapDevice(SignallingComponent* dbg, unsigned int chan, unsigned int circuit, bool interface)
    : m_owner(dbg),
    m_interface(interface),
    m_handle(-1),
    m_channel(chan),
    m_alarms(0),
    m_canRead(false),
    m_event(false),
    m_readError(false),
    m_writeError(false),
    m_selectError(false)
{
    this->channel(chan,circuit);
}

void ZapDevice::channel(unsigned int chan, unsigned int circuit)
{
    m_channel = chan;
    if (!m_interface)
	m_name << "ZapCircuit(" << circuit << "). ";
}

// Open the device. Specify channel to use.
// Circuit: Set block size
// Interface: Check channel mode. Set buffers
bool ZapDevice::open(unsigned int numbufs, unsigned int bufsize)
{
    close();

    if (m_interface)
	m_handle = ::open(s_zapDevName,O_RDWR,0600);
    else
	m_handle = ::open(s_zapDevName,O_RDWR|O_NONBLOCK);
    if (m_handle < 0) {
	Debug(m_owner,DebugWarn,"%sFailed to open '%s'. %d: %s [%p]",
	    m_name.safe(),s_zapDevName,errno,::strerror(errno),m_owner);
	return false;
    }

    while (true) {
	// Specify the channel to use
	if (!ioctl(SetChannel,&m_channel))
	    break;

	if (!m_interface) {
	    if (bufsize && !ioctl(SetBlkSize,&bufsize))
		break;
	    DDebug(m_owner,DebugAll,"%sBlock size set to %u on channel %u [%p]",
		m_name.safe(),bufsize,m_channel,m_owner);
	    return true;
	}

	// Open for an interface
	// Check channel mode
	ZT_PARAMS par;
	if (!ioctl(GetParams,&par))
	    break;
	if (par.sigtype != ZT_SIG_HDLCFCS) {
	    Debug(m_owner,DebugWarn,"Channel %u is not in HDLC/FCS mode [%p]",m_channel,m_owner);
	    break;
	}
	// Set buffers
	ZT_BUFFERINFO bi;
	bi.txbufpolicy = ZT_POLICY_IMMEDIATE;
	bi.rxbufpolicy = ZT_POLICY_IMMEDIATE;
	bi.numbufs = numbufs;
	bi.bufsize = bufsize;
	if (ioctl(SetBuffers,&bi))
	    DDebug(m_owner,DebugAll,"%snumbufs=%u bufsize=%u on channel %u [%p]",
		m_name.safe(),numbufs,bufsize,m_channel,m_owner);
	return true;
    }
    close();
    return false;
}

// Close device. Reset handle
void ZapDevice::close()
{
    if (!valid())
	return;
    ::close(m_handle);
    m_handle = -1;
}

// Set data format. Fails if called for an interface
bool ZapDevice::setFormat(Format format)
{
    if (m_interface)
	return false;
    if (!ioctl(SetFormat,&format,0)) {
	Debug(m_owner,DebugNote,"%sFailed to set format '%s' on channel %u [%p]",
	    m_name.safe(),lookup(format,s_formats,String((int)format)),
	    m_channel,m_owner);
	return false;
    }
    DDebug(m_owner,DebugAll,"%sFormat set to '%s' on channel %u [%p]",
	m_name.safe(),lookup(format,s_formats),m_channel,m_owner);
    return true;
}

// Set/unset tone detection
bool ZapDevice::setDtmfDetect(bool detect)
{
    int tmp = 0;
#ifdef ZT_TONEDETECT
    setLinear(0);
    if (detect)
	tmp = ZT_TONEDETECT_ON | ZT_TONEDETECT_MUTE;
#endif
    if (!ioctl(SetToneDetect,&tmp,DebugNote))
	return false;
    DDebug(m_owner,DebugAll,"%sTone detector %s on channel %u [%p]",
	m_name.safe(),detect?"started":"stopped",m_channel,m_owner);
    return true;
}

// Update echo canceller (0: disable)
bool ZapDevice::setEchoCancel(bool enable, unsigned int taps)
{
    enable = enable && taps;
    int tmp = 1;
    if (enable && !ioctl(SetAudioMode,&tmp,DebugMild))
	return false;
    if (!enable)
	taps = 0;
    if (!ioctl(SetEchoCancel,&taps,DebugMild))
	return false;
    if (taps)
	Debug(m_owner,DebugAll,
	    "%sEcho canceller enabled on channel %u (taps=%u) [%p]",
	    m_name.safe(),m_channel,taps,m_owner);
    else
	Debug(m_owner,DebugAll,"%sEcho canceller disabled on channel %u [%p]",
	    m_name.safe(),m_channel,m_owner);
    return true;
}

// Start echo training
bool ZapDevice::startEchoTrain(unsigned int period)
{
    if (!period)
	return true;
    if (!ioctl(StartEchoTrain,&period,DebugNote))
	return false;
    DDebug(m_owner,DebugAll,"%sEcho train started for %u ms on channel %u [%p]",
	m_name.safe(),period,m_channel,m_owner);
    return true;
}

// Send hook events
bool ZapDevice::sendHook(HookEvent event)
{
    const char* name = lookup(event,s_hookEvents);
    if (!name) {
	Debug(m_owner,DebugStub,"%sRequest to send unhandled hook event %u [%p]",
	    m_name.safe(),event,this);
	return false;
    }

    DDebug(m_owner,DebugAll,"%sSending hook event '%s' on channel %u [%p]",
	m_name.safe(),name,m_channel,m_owner);
    return ioctl(SetHook,&event);
}

// Send DTMFs events
bool ZapDevice::sendDtmf(const char* tone)
{
    if (!(tone && *tone))
	return false;
    int len = strlen(tone);
    if (len > ZT_MAX_DTMF_BUF - 2) {
	Debug(m_owner,DebugNote,"%sCan't send dtmf '%s' (len %d > %u) [%p]",
	    m_name.safe(),tone,len,ZT_MAX_DTMF_BUF-2,this);
	return false;
    }
    ZT_DIAL_OPERATION dop;
    dop.op = ZT_DIAL_OP_APPEND;
    dop.dialstr[0] = 'T';
	
//	dop.dialstr[2] = 0;
//	for (; *tone; tone++) {
//	    dop.dialstr[1] = *tone;
//	    DDebug(m_owner,DebugAll,"%sSending DTMF '%c' on channel %u [%p]",
//		m_name.safe(),*tone,m_channel,this);
//	    if (!ioctl(ZapDevice::SetDial,&dop,DebugMild))
//		return false;
//	}
//	return true;

    strncpy(dop.dialstr+1,tone,len);
    DDebug(m_owner,DebugAll,"%sSending DTMF '%s' on channel %u [%p]",
	m_name.safe(),tone,m_channel,this);
    return ioctl(ZapDevice::SetDial,&dop,DebugMild);
}

// Get an event. Return 0 if no events. Set dtmf if the event is a DTMF
int ZapDevice::getEvent(char& dtmf)
{
    int event = 0;
    if (!ioctl(GetEvent,&event,DebugMild))
	return 0;
    if (event & DtmfEvent) {
	dtmf = (char)event;
	event &= DtmfEvent;
    }
#ifdef XDEBUG
    if (event)
	Debug(m_owner,DebugAll,"%sGot event %d on channel %u [%p]",
	    m_name.safe(),event,m_channel,m_owner);
#endif
    return event;
}

// Get alarms from this device. Return true if alarms changed
bool ZapDevice::getAlarms(String* alarms)
{
    ZT_SPANINFO info;
    memset(&info,0,sizeof(info));
    info.spanno = m_channel;
    if (!(ioctl(GetInfo,&info,DebugAll)))
	return false;
    if (m_alarms == info.alarms)
	return false;
    m_alarms = info.alarms;
    if (alarms)
	for(int i = 0; s_alarms[i].token; i++)
	    if (m_alarms & s_alarms[i].value)
		alarms->append(s_alarms[i].token,",");
    return true;
}

// Flush read/write buffers
bool ZapDevice::flushBuffers()
{
    int x = ZT_FLUSH_READ | ZT_FLUSH_WRITE;
    bool ok = ioctl(FlushBuffers,&x,DebugNote);
    if (ok)
	DDebug(m_owner,DebugAll,"%sFlushed I/O buffers on channel %u [%p]",
	    m_name.safe(),m_channel,m_owner);
    return ok;
}

// Check if received data. Wait usec microseconds before returning
bool ZapDevice::select(unsigned int usec)
{
    FD_ZERO(&m_rdfds);
    FD_SET(m_handle, &m_rdfds);
    FD_ZERO(&m_errfds);
    FD_SET(m_handle, &m_errfds);
    m_tv.tv_sec = 0;
    m_tv.tv_usec = usec;
    int sel = ::select(m_handle+1,&m_rdfds,NULL,&m_errfds,&m_tv);
    if (sel >= 0) {
	m_event = FD_ISSET(m_handle,&m_errfds);
	m_canRead = FD_ISSET(m_handle,&m_rdfds);
	m_selectError = false;
	return true;
    }
    if (!(canRetry() || m_selectError)) {
	Debug(m_owner,DebugWarn,"%sSelect failed on channel %u. %d: %s [%p]",
	    m_name.safe(),m_channel,errno,::strerror(errno),m_owner);
	m_selectError = true;
    }
    return false;
}

int ZapDevice::recv(void* buffer, int len)
{
    int r = ::read(m_handle,buffer,len);
    if (r >= 0) {
	m_event = false;
	m_readError = false;
	return r;
    }
    // The caller should check for events if the error is ELAST
    m_event = (errno == ELAST);
    if (!(canRetry() || m_readError)) {
	Debug(m_owner,DebugWarn,"%sRead failed on channel %u. %d: %s [%p]",
	    m_name.safe(),m_channel,errno,::strerror(errno),m_owner);
	m_readError = true;
    }
    return -1;
}

int ZapDevice::send(const void* buffer, int len)
{
    int w = ::write(m_handle,buffer,len);
    if (w == len) {
	m_writeError = false;
	return w;
    }
    if (!m_writeError) {
	Debug(m_owner,DebugWarn,
	    "%sWrite failed on channel %u (sent %d instead of %d). %d: %s [%p]",
	    m_name.safe(),m_channel,w>=0?w:0,len,errno,::strerror(errno),m_owner);
	m_writeError = true;
    }
    return (w < 0 ? -1 : w);
}

// Make IOCTL requests on this device
bool ZapDevice::ioctl(IoctlRequest request, void* param, int level)
{
    int ret = -1;
    switch (request) {
	case SetChannel:
	    ret = ::ioctl(m_handle,ZT_SPECIFY,param);
	    break;
	case SetBlkSize:
	    ret = ::ioctl(m_handle,ZT_SET_BLOCKSIZE,param);
	    break;
	case SetBuffers:
	    ret = ::ioctl(m_handle,ZT_SET_BUFINFO,param);
	    break;
	case SetFormat:
	    ret = ::ioctl(m_handle,ZT_SETLAW,param);
	    break;
	case SetAudioMode:
	    ret = ::ioctl(m_handle,ZT_AUDIOMODE,param);
	    break;
	case SetEchoCancel:
	    ret = ::ioctl(m_handle,ZT_ECHOCANCEL,param);
	    break;
	case SetDial:
	    ret = ::ioctl(m_handle,ZT_DIAL,param);
	    break;
	case SetHook:
	    ret = ::ioctl(m_handle,ZT_HOOK,param);
	    break;
	case SetToneDetect:
#ifdef ZT_TONEDETECT
	    ret = ::ioctl(m_handle,ZT_TONEDETECT,param);
	    break;
#else
	    // Show message only if requested to set tone detection
	    if (param && *param)
		Debug(m_owner,level,"%sIOCTL(%s) failed: unsupported request [%p]",
		    m_name.safe(),lookup(SetToneDetect,s_ioctl_request),m_owner);
	    return false;
#endif
	case SetLinear:
	    ret = ::ioctl(m_handle,ZT_SETLINEAR,param);
	    break;
	case GetParams:
	    ret = ::ioctl(m_handle,ZT_GET_PARAMS,param);
	    break;
	case GetEvent:
	    ret = ::ioctl(m_handle,ZT_GETEVENT,param);
	    break;
	case GetInfo:
	    ret = ::ioctl(m_handle,ZT_SPANSTAT,param);
	    break;
	case StartEchoTrain:
	    ret = ::ioctl(m_handle,ZT_ECHOTRAIN,param);
	    break;
	case FlushBuffers:
	    ret = ::ioctl(m_handle,ZT_FLUSH,param);
	    break;
    }
    if (ret >= 0 || errno == EINPROGRESS) {
	if (errno == EINPROGRESS)
	    DDebug(m_owner,DebugAll,"%sIOCTL(%s) in progress on channel %u (param=%d) [%p]",
		m_name.safe(),lookup(request,s_ioctl_request),
		m_channel,*(unsigned int*)param,m_owner);
	return true;
    }
    Debug(m_owner,level,"%sIOCTL(%s) failed on channel %u (param=%d). %d: %s [%p]",
	m_name.safe(),lookup(request,s_ioctl_request),
	m_channel,*(unsigned int*)param,errno,::strerror(errno),m_owner);
    return false;
}


/**
 * ZapInterface
 */
ZapInterface::ZapInterface(const NamedList& params)
    : m_device(this,0,0,true),
    m_priority(Thread::Normal),
    m_errorMask(255),
    m_numbufs(16),
    m_bufsize(1024),
    m_buffer(0),
    m_readOnly(false),
    m_sendReadOnly(false),
    m_notify(0),
    m_timerRxUnder(0)
{
    setName(params.getValue("debugname","ZapInterface"));
    m_buffer = new unsigned char[m_bufsize + ZAP_CRC_LEN];
    XDebug(this,DebugAll,"ZapInterface::ZapInterface() [%p]",this);
}

ZapInterface::~ZapInterface()
{
    cleanup(false);
    delete[] m_buffer;
    XDebug(this,DebugAll,"ZapInterface::~ZapInterface() [%p]",this);
}

// Called by the factory to create Zaptel interfaces or spans
void* ZapInterface::create(const String& type, const NamedList& name)
{
    bool circuit = true;
    if (type == "sig")
	circuit = false;
    else  if (type == "voice")
	;
    else
	return 0;

    Configuration cfg(Engine::configFile("zapcard"));
    cfg.load();

    const char* sectName = name.getValue(type);
    DDebug(&driver,DebugAll,"Factory trying to create %s='%s'",type.c_str(),sectName);
    NamedList* config = cfg.getSection(sectName);
    if (!config) {
	DDebug(&driver,DebugAll,"No section '%s' in configuration",c_safe(sectName));
	return 0;
    }

    String sDevType = config->getValue("type");
    ZapDevice::Type devType = (ZapDevice::Type)lookup(sDevType,s_types,ZapDevice::E1);

    NamedList dummy("general");
    NamedList* general = cfg.getSection("general");
    if (!general)
	general = &dummy;

    String sOffset = config->getValue("offset");
    unsigned int offset = (unsigned int)sOffset.toInteger(-1);
    if (offset == (unsigned int)-1) {
	Debug(&driver,DebugWarn,"Section '%s'. Invalid offset='%s'",
	    config->c_str(),sOffset.safe());
	return 0;
    }

    if (circuit) {
	ZapSpan* span = new ZapSpan(name);
	bool ok = false;
	if (span->group())
	    ok = span->init(devType,offset,*config,*general,name);
	else
	    Debug(&driver,DebugWarn,"Can't create span '%s'. Group is missing",
		span->id().safe());
	if (ok)
	    return span;
	TelEngine::destruct(span);
	return 0;
    }

    // Check span type
    if (devType != ZapDevice::E1 && devType != ZapDevice::T1) {
	Debug(&driver,DebugWarn,"Section '%s'. Can't create D-channel for type='%s'",
	    config->c_str(),sDevType.c_str());
	return 0;
    }
    // Check channel
    String sig = config->getValue("sigchan");
    unsigned int count = (devType == ZapDevice::E1 ? 31 : 24);
    if (!sig)
	sig = (devType == ZapDevice::E1 ? 16 : 24);
    unsigned int code = (unsigned int)sig.toInteger(0);
    if (!(sig && code && code <= count)) {
	Debug(&driver,DebugWarn,"Section '%s'. Invalid sigchan='%s' for type='%s'",
	    config->c_str(),sig.safe(),sDevType.c_str());
	return false;
    }
    ZapInterface* iface = new ZapInterface(name);
    if (iface->init(devType,code,offset+code,*config,*general,name))
	return iface;
    TelEngine::destruct(iface);
    return 0;
}

bool ZapInterface::init(ZapDevice::Type type, unsigned int code, unsigned int channel,
	const NamedList& config, const NamedList& defaults, const NamedList& params)
{
    m_device.channel(channel,code);
    m_readOnly = config.getBoolValue("readonly",false);
    m_priority = Thread::priority(config.getValue("priority",defaults.getValue("priority")));
    int rx = params.getIntValue("rxunderruninterval");
    if (rx > 0)
	m_timerRxUnder.interval(rx);
    int i = params.getIntValue("errormask",config.getIntValue("errormask",255));
    m_errorMask = ((i >= 0 && i < 256) ? i : 255);
    if (debugAt(DebugInfo)) {
	String s;
	s << "\r\nType:                 " << lookup(type,s_types);
	s << "\r\nD-channel:            " << (unsigned int)m_device.channel();
	s << "\r\nError mask:           " << (unsigned int)m_errorMask;
	s << "\r\nRead only:            " << String::boolText(m_readOnly);
	s << "\r\nRX underrun interval: " << (unsigned int)m_timerRxUnder.interval() << " ms";
	s << "\r\nBuffers (count/size): " << (unsigned int)m_numbufs << "/" << (unsigned int)m_bufsize;
	s << "\r\nWorker priority:      " << Thread::priority(m_priority);
	Debug(this,DebugInfo,"Initialized: [%p]%s",this,s.c_str());
    }
    return true;
}

// Process incoming data
bool ZapInterface::process()
{
    if (!m_device.select(100))
	return false;
    if (!m_device.canRead()) {
	if (m_device.event())
	    checkEvents();
	return false;
    }

    int r = m_device.recv(m_buffer,m_bufsize + ZAP_CRC_LEN);
    if (r == -1) {
	if (m_device.event())
	    checkEvents();
	return false;
    }
    if (r < ZAP_CRC_LEN + 1) {
	Debug(this,DebugMild,"Short read %u bytes (with CRC) [%p]",r,this);
	return false;
    }

    s_ifaceNotify.lock();
    m_notify = 0;
    s_ifaceNotify.unlock();
    DataBlock packet(m_buffer,r - ZAP_CRC_LEN);
#ifdef XDEBUG
    String hex;
    hex.hexify(packet.data(),packet.length(),' ');
    Debug(this,DebugAll,"Received data: %s [%p]",hex.safe(),this);
#endif
    receivedPacket(packet);
    packet.clear(false);
    return true;
}

void* ZapInterface::getObject(const String& name) const
{
    if (name == "ZapInterface")
	return (void*)this;
    return SignallingInterface::getObject(name);
}

// Send signalling packet
bool ZapInterface::transmitPacket(const DataBlock& packet, bool repeat, PacketType type)
{
    static DataBlock crc(0,ZAP_CRC_LEN);

    if (m_readOnly) {
	if (!m_sendReadOnly)
	    Debug(this,DebugWarn,"Attempt to send data on read only interface");
	m_sendReadOnly = true;
	return false;
    }
    if (!m_device.valid())
	return false;

#ifdef XDEBUG
    String hex;
    hex.hexify(packet.data(),packet.length(),' ');
    Debug(this,DebugAll,"Sending data: %s [%p]",hex.safe(),this);
#endif
    *((DataBlock*)&packet) += crc;
    return m_device.send(packet.data(),packet.length());
}

// Interface control. Open device and start worker when enabled, cleanup when disabled
bool ZapInterface::control(Operation oper, NamedList* params)
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
	    return valid();
	default:
	    return SignallingInterface::control(oper,params);
    }
    if (oper == Enable) {
	if (valid())
	    return true;
	bool ok = m_device.valid() || m_device.open(m_numbufs,m_bufsize);
	if (ok)
	    ok = ZapWorkerClient::start(m_priority,this,debugName());
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
    bool ok = valid();
    m_timerRxUnder.stop();
    ZapWorkerClient::stop();
    m_device.close();
    if (ok)
	Debug(this,DebugAll,"Disabled [%p]",this);
    return true;
}

// Check if received any data in the last interval. Notify receiver
void ZapInterface::timerTick(const Time& when)
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

void ZapInterface::checkEvents()
{
    char c = 0;
    int event = m_device.getEvent(c);
    if (!event)
	return;
    int level = DebugWarn;
    switch (event) {
	case ZapDevice::Alarm:
	case ZapDevice::NoAlarm:
	    if (event == ZapDevice::Alarm) {
		String s;
		if (m_device.getAlarms(&s))
		    Debug(this,DebugNote,"Alarms changed. %d: '%s' [%p]",m_device.alarms(),s.safe(),this);
	    }
	    else {
		m_device.resetAlarms();
		Debug(this,DebugNote,"No more alarms [%p]",this);
	    }
	    return;
	case ZapDevice::HdlcAbort:
	    if (m_errorMask & ZAP_ERR_ABORT)
		notify(AlignError);
	    break;
	case ZapDevice::HdlcOverrun:
	    if (m_errorMask & ZAP_ERR_OVERRUN)
		notify(RxOverflow);
	    break;
	case ZapDevice::PulseDigit:
	case ZapDevice::DtmfDown:
	case ZapDevice::DtmfUp:
	    Debug(this,DebugNote,"Got DTMF event '%s' on D-channel [%p]",
		lookup(event,s_events,""),this);
	    return;
	default:
	    level = DebugStub;
    }
    DDebug(this,level,"Got event %d ('%s') [%p]",event,lookup(event,s_events,""),this);
}


/**
 * ZapSpan
 */
// Create circuits
bool ZapSpan::init(ZapDevice::Type type, unsigned int offset,
	const NamedList& config, const NamedList& defaults, const NamedList& params)
{
    String voice = config.getValue("voicechans");
    unsigned int chans = 0;
    bool digital = true;
    switch (type) {
	case ZapDevice::E1:
	    if (!voice)
		voice = "1-15.17-31";
	    chans = 31;
	    break;
	case ZapDevice::T1:
	    if (!voice)
		voice = "1-23";
	    chans = 24;
	    break;
	case ZapDevice::FXO:
	case ZapDevice::FXS:
	    digital = false;
	    if (!voice)
		voice = "1";
	    chans = (unsigned int)-1;
	    break;
	default:
	    Debug(m_group,DebugStub,
		"ZapSpan('%s'). Can't create circuits for type=%s [%p]",
		id().safe(),lookup(type,s_types),this);
	    return false;
    }
    unsigned int count = 0;
    unsigned int* cics = SignallingUtils::parseUIntArray(voice,1,chans,count,true);
    if (!cics) {
	Debug(m_group,DebugWarn,
	    "ZapSpan('%s'). Invalid voicechans='%s' (type=%s,chans=%u) [%p]",
	    id().safe(),voice.safe(),lookup(type,s_types),chans,this);
	return false;
    }

    if (!digital)
	chans = count;
    ((NamedList*)&params)->setParam("chans",String(chans));
    unsigned int start = params.getIntValue("start",0);

    // Create and insert circuits
    unsigned int added = 0;
    for (unsigned int i = 0; i < count; i++) {
	unsigned int code = start + cics[i];
	unsigned int channel = offset + cics[i];
	ZapCircuit* cic = 0;
	if (digital)
	    cic = new ZapCircuit(type,code,channel,this,config,defaults,params);
	else
	    cic = new ZapAnalogCircuit(type,code,channel,this,config,defaults,params);
	if (m_group->insert(cic)) {
	    added++;
	    continue;
	}
	TelEngine::destruct(cic);
	Debug(m_group,DebugGoOn,
	    "ZapSpan('%s'). Duplicate circuit code=%u (channel=%u) [%p]",
	    id().safe(),code,channel,this);
    }
    if (!added) {
	Debug(m_group,DebugWarn,"ZapSpan('%s'). No circuits inserted for this span [%p]",
	    id().safe(),this);
	delete[] cics;
	return false;
    }

    if (m_group && m_group->debugAt(DebugInfo)) {
	String s;
	s << "\r\nType:     " << lookup(type,s_types);
	s << "\r\nGroup:    " << m_group->debugName();
	String c,ch;
	for (unsigned int i = 0; i < count; i++) {
	    c.append(String(start+cics[i])," ");
	    ch.append(String(offset+cics[i])," ");
	}
	s << "\r\nCircuits: " << c;
	s << "\r\nChannels: " << ch;
	Debug(m_group,DebugInfo,"ZapSpan('%s'). Initialized: [%p]%s",
	    id().safe(),this,s.c_str());
    }
    delete[] cics;
    return true;
}


/**
 * ZapCircuit
 */
ZapCircuit::ZapCircuit(ZapDevice::Type type, unsigned int code, unsigned int channel,
	ZapSpan* span, const NamedList& config, const NamedList& defaults,
	const NamedList& params)
    : SignallingCircuit(TDM,code,Idle,span->group(),span),
    m_device(span->group(),channel,code,false),
    m_type(type),
    m_format(ZapDevice::Alaw),
    m_echoCancel(false),
    m_crtEchoCancel(false),
    m_echoTaps(0),
    m_echoTrain(400),
    m_dtmfDetect(false),
    m_crtDtmfDetect(false),
    m_canSend(true),
    m_idleValue(255),
    m_priority(Thread::Normal),
    m_source(0),
    m_consumer(0),
    m_buflen(0),
    m_consBufMax(0),
    m_consErrors(0),
    m_consErrorBytes(0),
    m_consTotal(0)
{
    m_dtmfDetect = config.getBoolValue("dtmfdetect",defaults.getBoolValue("dtmfdetect",false));
    if (m_dtmfDetect && ZapDevice::SetToneDetect < 0) {
	Debug(group(),DebugWarn,
	    "ZapCircuit(%u). DTMF detection is not supported by hardware [%p]",
	    code,this);
	m_dtmfDetect = false;
    }
    m_crtDtmfDetect = m_dtmfDetect;
    m_echoTaps = (unsigned int)config.getIntValue("echotaps",defaults.getIntValue("echotaps",0));
    m_crtEchoCancel = m_echoCancel = m_echoTaps;
    m_echoTrain = (unsigned int)config.getIntValue("echotrain",defaults.getIntValue("echotrain",400));
    m_canSend = config.getBoolValue("readonly",true);
    m_buflen = (unsigned int)config.getIntValue("buflen",defaults.getIntValue("buflen",160));
    if (!m_buflen)
	m_buflen = 160;
    m_consBufMax = m_buflen * 4;
    m_sourceBuffer.assign(0,m_buflen);
    m_idleValue = defaults.getIntValue("idlevalue",0xff);
    m_idleValue = params.getIntValue("idlevalue",config.getIntValue("idlevalue",m_idleValue));
    m_priority = Thread::priority(config.getValue("priority",defaults.getValue("priority")));

    if (type == ZapDevice::E1)
	m_format = ZapDevice::Alaw;
    else if (type == ZapDevice::T1)
	m_format = ZapDevice::Mulaw;
    else if (type == ZapDevice::FXO || type == ZapDevice::FXS) {
	const char* f = config.getValue("format",defaults.getValue("format"));
	m_format = (ZapDevice::Format)lookup(f,s_formats,ZapDevice::Mulaw);
	if (m_format != ZapDevice::Alaw && m_format != ZapDevice::Mulaw)
	    m_format = ZapDevice::Mulaw;
    }
    else
	Debug(group(),DebugStub,"ZapCircuit(%u). Unhandled circuit type=%d [%p]",
	    code,type,this);
}

// Change circuit status. Clear events on status change
// New status is Connect: Open device. Create source/consumer. Start worker
// Cleanup on disconnect
bool ZapCircuit::status(Status newStat, bool sync)
{
    if (SignallingCircuit::status() == newStat)
	return true;
    if (SignallingCircuit::status() == Missing) {
	Debug(group(),DebugNote,
	    "ZapCircuit(%u). Can't change status to '%u'. Circuit is missing [%p]",
	    code(),newStat,this);
	return false;
    }
    Status oldStat = SignallingCircuit::status();
    // Allow status change for the following values
    switch (newStat) {
	case Missing:
	case Disabled:
	case Idle:
	case Reserved:
	case Connected:
	    if (!SignallingCircuit::status(newStat,sync))
		return false;
	    clearEvents();
	    if (!Engine::exiting())
		DDebug(group(),DebugAll,"ZapCircuit(%u). Changed status to %u [%p]",
		    code(),newStat,this);
	    if (newStat == Connected)
		break;
	    if (oldStat == Connected)
		cleanup(false,newStat);
	    return true;
	default: ;
	    Debug(group(),DebugStub,
		"ZapCircuit(%u). Can't change status to unhandled value %u [%p]",
		code(),newStat,this);
	    return false;
    }
    // Connected: open device, create source/consumer, start worker
    while (true) {
	if (!m_device.open(0,m_buflen))
	    break;
	m_device.flushBuffers();
	setFormat(m_format);
	createData();
	String addr;
	if (group())
	    addr << group()->debugName() << "/";
	addr << code();
	if (!ZapWorkerClient::start(m_priority,group(),addr))
	    break;
	return true;
    }
    // Rollback on error
    cleanup(false,oldStat);
    return false;
}

// Update data format for zaptel device and source/consumer 
bool ZapCircuit::updateFormat(const char* format, int direction)
{
    if (!(m_source && m_consumer && format && *format))
	return false;
    // Do nothing if format is the same
    if (m_source->getFormat() == format && m_consumer->getFormat() == format)
	return false;
    // Check format
    // T1,E1: allow alaw or mulaw
    int f = lookup(format,s_formats,-2);
    switch (m_type) {
	case ZapDevice::E1:
	case ZapDevice::T1:
	case ZapDevice::FXS:
	case ZapDevice::FXO:
	    if (f == ZapDevice::Alaw || f == ZapDevice::Mulaw)
		break;
	    // Fallthrough to deny format change
	default:
	    Debug(group(),DebugNote,
		"ZapCircuit(%u). Can't set format to '%s' for type=%s [%p]",
		code(),format,lookup(m_type,s_types),this);
	    return false;
    }
    // Update the format for Zaptel device
    if (setFormat((ZapDevice::Format)f)) {
	m_source->changeFormat(format);
	m_consumer->changeFormat(format);
	return true;
    }
    Debug(group(),DebugNote,
	"ZapCircuit(%u). Failed to update data format to '%s' [%p]",
	code(),format,this);
    return false;
}

// Setup echo canceller or start echo canceller training
bool ZapCircuit::setParam(const String& param, const String& value)
{
    if (param == "echotrain") {
	int tmp = value.toInteger();
	if (tmp > 0)
	    m_echoTrain = (unsigned int)tmp;
	return m_device.valid() && m_crtEchoCancel && m_device.startEchoTrain(m_echoTrain);
    }
    if (param == "echocancel") {
	bool tmp = value.toBoolean();
	if (tmp == m_crtEchoCancel)
	    return true;
	if (m_echoTaps)
	    m_crtEchoCancel = tmp;
	else if (tmp)
	    return false;
	else
	    m_crtEchoCancel = false;
	if (!m_device.valid())
	    return false;
	bool ok = m_device.setEchoCancel(m_crtEchoCancel,m_echoTaps);
	if (m_crtEchoCancel)
	    m_crtEchoCancel = ok;
	return ok; 
    }
    if (param == "echotaps") {
	m_echoTaps = (unsigned int)value.toInteger();
	return true;
    }
    if (param == "tonedetect") {
	bool tmp = value.toBoolean();
	if (tmp == m_crtDtmfDetect)
	    return true;
	m_crtDtmfDetect = tmp;
	if (!m_device.valid())
	    return true;
	bool ok = m_device.setDtmfDetect(m_crtDtmfDetect);
	if (m_crtDtmfDetect)
	    m_crtDtmfDetect = ok;
	return ok;
    }
    return false;
}

// Get circuit data
bool ZapCircuit::getParam(const String& param, String& value) const
{
    if (param == "tonedetect")
	value = String::boolText(m_crtDtmfDetect);
    else if (param == "channel")
	value = m_device.channel();
    else if (param == "echocancel")
	value = String::boolText(m_crtEchoCancel);
    else if (param == "echotaps")
	value = m_echoTaps;
    else
	return false;
    return true;
}

// Get source or consumer
void* ZapCircuit::getObject(const String& name) const
{
    if (name == "ZapCircuit")
	return (void*)this;
    if (SignallingCircuit::status() == Connected) {
	if (name == "DataSource")
	    return m_source;
	if (name == "DataConsumer")
	    return m_consumer;
    }
    return SignallingCircuit::getObject(name);
}

// Process incoming data
bool ZapCircuit::process()
{
    if (!(m_device.valid() && SignallingCircuit::status() == Connected && m_source))
	return false;

    if (!m_device.select(10))
	return false;
    if (!m_device.canRead()) {
	if (m_device.event())
	    checkEvents();
	return false;
    }

    int r = m_device.recv(m_sourceBuffer.data(),m_sourceBuffer.length());
    if (m_device.event())
	checkEvents();
    if (r > 0) {
	if ((unsigned int)r != m_sourceBuffer.length())
	    ::memset((unsigned char*)m_sourceBuffer.data() + r,m_idleValue,m_sourceBuffer.length() - r);
	m_source->Forward(m_sourceBuffer);
	return true;
    }
    return false;
}

// Send an event through the circuit
bool ZapCircuit::sendEvent(SignallingCircuitEvent::Type type, NamedList* params)
{
    if (type == SignallingCircuitEvent::Dtmf)
	return m_device.sendDtmf(params ? params->getValue("tone") : 0);

    Debug(group(),DebugNote,"ZapCircuit(%u). Unable to send unknown event %u [%p]",
	code(),type,this);
    return false;
}

// Consume data sent by the consumer
void ZapCircuit::consume(const DataBlock& data)
{
    if (!(SignallingCircuit::status() == Connected && m_canSend && data.length()))
	return;
    m_consTotal += data.length();
    XDebug(group(),DebugAll,"ZapCircuit(%u). Consuming %u bytes. Buffer=%u [%p]",
	code(),data.length(),m_consBuffer.length(),this);
    if (m_consBuffer.length() + data.length() <= m_consBufMax)
	m_consBuffer += data;
    else {
	m_consErrors++;
	m_consErrorBytes += data.length();
	XDebug(group(),DebugMild,"ZapCircuit(%u). Buffer overrun %u bytes [%p]",
	    code(),data.length(),this);
    }
    while (m_consBuffer.length() >= m_buflen) {
	int w = m_device.send(m_consBuffer.data(),m_buflen);
	if (w > 0) {
	    m_consBuffer.cut(-w);
	    XDebug(group(),DebugAll,"ZapCircuit(%u). Sent %d bytes. Remaining: %u [%p]",
		code(),w,m_consBuffer.length(),this);
	}
	else
	    break;
    }
}

// Close device. Stop worker. Remove source consumer. Change status. Release memory if requested
// Reset echo canceller and tone detector if the device is not closed
void ZapCircuit::cleanup(bool release, Status stat, bool stop)
{
    if (stop || release) {
	ZapWorkerClient::stop();
	m_device.close();
    }
    if (m_consumer) {
	if (m_consErrors)
	    DDebug(group(),DebugMild,"ZapCircuit(%u). Consumer errors: %u. Lost: %u/%u [%p]",
		code(),m_consErrors,m_consErrorBytes,m_consTotal,this);
	m_consumer->deref();
	m_consumer = 0;
    }
    if (m_source) {
	m_source->clear();
	m_source->deref();
	m_source = 0;
    }
    if (release) {
	SignallingCircuit::destroyed();
	return;
    }
    status(stat);
    m_sourceBuffer.clear();
    m_consBuffer.clear();
    m_consErrors = m_consErrorBytes = m_consTotal = 0;
    // Reset echo canceller and tone detector
    if (m_device.valid() && (m_crtEchoCancel != m_echoCancel))
	m_device.setEchoCancel(m_echoCancel,m_echoTaps);
    m_crtEchoCancel = m_echoCancel;
    if (m_device.valid() && (m_crtDtmfDetect != m_dtmfDetect))
	m_device.setDtmfDetect(m_dtmfDetect);
    m_crtDtmfDetect = m_dtmfDetect;
}

// Update format, echo canceller, dtmf detection
bool ZapCircuit::setFormat(ZapDevice::Format format)
{
    m_device.flushBuffers();
    if (!m_device.setFormat(format))
	return false;
    if (m_crtEchoCancel)
	m_crtEchoCancel = m_device.setEchoCancel(m_crtEchoCancel,m_echoTaps);
    if (m_crtDtmfDetect)
	m_crtDtmfDetect = m_device.setDtmfDetect(true);
    else
	m_device.setDtmfDetect(false);
    return true;
}

// Get events
void ZapCircuit::checkEvents()
{
    char c = 0;
    int event = m_device.getEvent(c);
    if (!event)
	return;
    switch (event) {
	case ZapDevice::DtmfDown:
	case ZapDevice::DtmfUp:
	    if (!m_crtDtmfDetect) {
		DDebug(group(),DebugAll,"ZapCircuit(%u). Ignoring DTMF '%s'=%c [%p]",
		    code(),lookup(event,s_events,""),c,this);
		return;
	    }
	    if (event == ZapDevice::DtmfUp)
		enqueueDigit(true,c);
	    else
		DDebug(group(),DebugAll,"ZapCircuit(%u). Ignoring '%s'=%c [%p]",
		    code(),lookup(event,s_events,""),c,this);
	    return;
	case ZapDevice::Alarm:
	case ZapDevice::NoAlarm:
	    if (event == ZapDevice::Alarm) {
		String s;
		m_device.getAlarms(&s);
		Debug(group(),DebugNote,
		    "ZapCircuit(%u). Alarms changed. %d: '%s' [%p]",
		    code(),m_device.alarms(),s.safe(),this);
		SignallingCircuitEvent* e = new SignallingCircuitEvent(this,
		    SignallingCircuitEvent::Alarm,lookup(event,s_events));
		if (s)
		    e->addParam("alarms",s);
		enqueueEvent(e);
	    }
	    else {
		m_device.resetAlarms();
		Debug(group(),DebugNote,"ZapCircuit(%u). No more alarms [%p]",code(),this);
		enqueueEvent(event,SignallingCircuitEvent::NoAlarm);
	    }
	    return;
	default: ;
    }
    if (processEvent(event,c))
	return;
    enqueueEvent(event,SignallingCircuitEvent::Unknown);
}

// Create source buffer and data source and consumer
void ZapCircuit::createData()
{
    m_sourceBuffer.assign(0,m_buflen);
    const char* format = lookup(m_format,s_formats,"alaw");
    m_source = new ZapSource(this,format);
    m_consumer = new ZapConsumer(this,format);
}

// Enqueue received events
inline bool ZapCircuit::enqueueEvent(SignallingCircuitEvent* e)
{
    if (e) {
	addEvent(e);
	DDebug(group(),e->type()!=SignallingCircuitEvent::Unknown?DebugAll:DebugStub,
	    "ZapCircuit(%u). Enqueued event '%s' [%p]",code(),e->c_str(),this);
    }
    return true;
}

// Enqueue received events
inline bool ZapCircuit::enqueueEvent(int event, SignallingCircuitEvent::Type type)
{
    return enqueueEvent(new SignallingCircuitEvent(this,type,lookup(event,s_events)));
}

// Enqueue received digits
bool ZapCircuit::enqueueDigit(bool tone, char digit)
{
    char digits[2] = {digit,0};
    SignallingCircuitEvent* e = 0;

    if (tone) {
	e = new SignallingCircuitEvent(this,SignallingCircuitEvent::Dtmf,
	    lookup(ZapDevice::DtmfUp,s_events));
	e->addParam("tone",digits);
    }
    else {
	e = new SignallingCircuitEvent(this,SignallingCircuitEvent::PulseDigit,
	    lookup(ZapDevice::PulseDigit,s_events));
	e->addParam("pulse",digits);
    }
    return enqueueEvent(e);
}


/**
 * ZapAnalogCircuit
 */
// Change circuit status. Clear events on status change
// New status is Reserved: Open device and start worker if old status is not Connected
// New status is Connect: Create source/consumer
// Cleanup on disconnect
bool ZapAnalogCircuit::status(Status newStat, bool sync)
{
    if (SignallingCircuit::status() == newStat)
	return true;
    if (SignallingCircuit::status() == Missing) {
	Debug(group(),DebugNote,
	    "ZapCircuit(%u). Can't change status to '%u'. Circuit is missing [%p]",
	    code(),newStat,this);
	return false;
    }
    // Allow status change for the following values
    switch (newStat) {
	case Missing:
	case Disabled:
	case Idle:
	case Reserved:
	case Connected:
	    break;
	default: ;
	    Debug(group(),DebugStub,
		"ZapCircuit(%u). Can't change status to unhandled value %u [%p]",
		code(),newStat,this);
	    return false;
    }

    Status oldStat = SignallingCircuit::status();
    if (!SignallingCircuit::status(newStat,sync))
	return false;
    clearEvents();
    if (!Engine::exiting())
	DDebug(group(),DebugAll,"ZapCircuit(%u). Changed status to %u [%p]",
	    code(),newStat,this);

    if (newStat == Reserved) {
	// Just cleanup if old status was Connected and the device is already valid
	// Otherwise: open device and start worker
	if (oldStat == Connected && m_device.valid())
	    cleanup(false,Reserved,false);
	else {
	    String addr;
	    if (group())
		addr << group()->debugName() << "/";
	    addr << code();
	    if (m_device.open(0,m_buflen) && ZapWorkerClient::start(m_priority,group(),addr))
		setFormat(m_format);
	    else
		cleanup(false,Idle,true);
	}
	return SignallingCircuit::status() == Reserved;
    }
    else if (newStat == Connected) {
	if (m_device.valid())
	    createData();
	else
	    cleanup(false,Idle,true);
	return SignallingCircuit::status() == Connected;
    }
    return true;
}

// Get circuit data
bool ZapAnalogCircuit::getParam(const String& param, String& value) const
{
    if (param == "hook") {
	value = String::boolText(m_hook);
	return true;
    }
    return ZapCircuit::getParam(param,value);
}

// Send an event
bool ZapAnalogCircuit::sendEvent(SignallingCircuitEvent::Type type, NamedList* params)
{
    if (type == SignallingCircuitEvent::Dtmf)
	return ZapCircuit::sendEvent(type,params);

    switch (type) {
	case SignallingCircuitEvent::OnHook:
	    if (!m_device.sendHook(ZapDevice::HookOn))
		return false;
	    changeHook(true);
	    return true;
	case SignallingCircuitEvent::OffHook:
	    if (!m_device.sendHook(ZapDevice::HookOff))
		return false;
	    changeHook(false);
	    return true;
	case SignallingCircuitEvent::Wink:
	    return m_device.sendHook(ZapDevice::HookWink);
	case SignallingCircuitEvent::Flash:
	    return m_device.sendHook(ZapDevice::HookFlash);
	case SignallingCircuitEvent::RingBegin:
	    return m_device.sendHook(ZapDevice::HookRing);
	case SignallingCircuitEvent::RingEnd:
	    return m_device.sendHook(ZapDevice::HookRingOff);
	case SignallingCircuitEvent::StartLine:
	    return m_device.sendHook(ZapDevice::HookStart);
	default: ;
    }
    return ZapCircuit::sendEvent(type,params);
}

// Process additional events. Return false if not processed
bool ZapAnalogCircuit::processEvent(int event, char c)
{
    switch (event) {
#if 0
Unhandled:
	BitsChanged
	PulseStart
	Timeout
	TimerPing
#endif
	case ZapDevice::RingerOn:
	    return enqueueEvent(event,SignallingCircuitEvent::RingerOn);
	case ZapDevice::RingerOff:
	    return enqueueEvent(event,SignallingCircuitEvent::RingerOff);
	case ZapDevice::OnHook:
	    changeHook(true);
	    return enqueueEvent(event,SignallingCircuitEvent::OnHook);
	case ZapDevice::RingBegin:
	    return enqueueEvent(event,SignallingCircuitEvent::RingBegin);
	case ZapDevice::OffHookRing:
	    if (m_type == ZapDevice::FXS) {
		changeHook(false);
		return enqueueEvent(event,SignallingCircuitEvent::OffHook);
	    }
	    return enqueueEvent(event,SignallingCircuitEvent::RingerOn);
	case ZapDevice::Polarity:
	    return enqueueEvent(event,SignallingCircuitEvent::Polarity);
	case ZapDevice::WinkFlash:
	    if (m_hook)
		return enqueueEvent(event,SignallingCircuitEvent::Wink);
	    return enqueueEvent(event,SignallingCircuitEvent::Flash);
	case ZapDevice::HookComplete:
	    return enqueueEvent(event,SignallingCircuitEvent::LineStarted);
	case ZapDevice::DialComplete:
	    return enqueueEvent(event,SignallingCircuitEvent::DialComplete);
	case ZapDevice::PulseDigit:
	    return enqueueDigit(false,c);
	default: ;
    }
    return false;
}

// Process incoming data
bool ZapAnalogCircuit::process()
{
    if (!m_device.valid()) {
	Debug(group(),DebugNote,
	    "ZapCircuit(%u). Can't process: device is invalid [%p]",code(),this);
	return false;
    }

    checkEvents();

    if (!(m_source && m_device.select(10) && m_device.canRead()))
	return false;

    int r = m_device.recv(m_sourceBuffer.data(),m_sourceBuffer.length());
    if (m_device.event())
	checkEvents();
    if (r > 0) {
	if ((unsigned int)r != m_sourceBuffer.length())
	    ::memset((unsigned char*)m_sourceBuffer.data() + r,m_idleValue,m_sourceBuffer.length() - r);
	XDebug(group(),DebugAll,"ZapCircuit(%u). Forwarding %u bytes [%p]",
	    code(),m_sourceBuffer.length(),this);
	m_source->Forward(m_sourceBuffer);
	return true;
    }

    return false;
}

// Change hook state if different
void ZapAnalogCircuit::changeHook(bool hook)
{
    if (m_hook == hook)
	return;
    DDebug(group(),DebugInfo,"ZapCircuit(%u). Hook state changed to %s [%p]",
	code(),hook?"ON":"OFF",this);
    m_hook = hook;
}


/**
 * ZapSource
 */
inline void setAddr(String& addr, ZapCircuit* cic)
{
    if (cic) {
	if (cic->group())
	    addr << cic->group()->debugName() << "/";
	addr << cic->code();
    }
    else
	addr = -1;
}

ZapSource::ZapSource(ZapCircuit* circuit, const char* format)
    : DataSource(format)
{
#ifdef XDEBUG
    setAddr(m_address,circuit);
    Debug(&driver,DebugAll,"ZapSource::ZapSource() cic=%s [%p]",m_address.c_str(),this);
#endif
}

ZapSource::~ZapSource()
{
    XDebug(&driver,DebugAll,"ZapSource::~ZapSource() cic=%s [%p]",m_address.c_str(),this);
}


/**
 * ZapConsumer
 */
ZapConsumer::ZapConsumer(ZapCircuit* circuit, const char* format)
    : DataConsumer(format),
    m_circuit(circuit)
{
#ifdef XDEBUG
    setAddr(m_address,circuit);
    Debug(&driver,DebugAll,"ZapConsumer::ZapConsumer() cic=%s [%p]",m_address.c_str(),this);
#endif
}

ZapConsumer::~ZapConsumer()
{
    XDebug(&driver,DebugAll,"ZapConsumer::~ZapConsumer() cic=%s [%p]",m_address.c_str(),this);
}


/**
 * ZapModule
 */
ZapModule::ZapModule()
{
    debugName("Zaptel");
    Output("Loaded module %s",debugName());
    Configuration cfg(Engine::configFile("zapcard"));
    cfg.load();

    int level = cfg.getIntValue("general","debuglevel");

    if (level > 0)
	debugLevel(level);
}

ZapModule::~ZapModule()
{
    Output("Unloading module %s",debugName());
}

}; // anonymous namespace

#endif /* _WINDOWS */

/* vi: set ts=8 sw=4 sts=4 noet: */
