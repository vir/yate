/**
 * tdmcard.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * TDM cards signalling and data driver
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
#include <yatesig.h>

#ifdef _WINDOWS
#error This module is not for Windows
#else

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define __LINUX__
#include <if_wanpipe.h>
#include <wanpipe_defines.h>
#include <wanpipe_cfg.h>
#include <wanpipe.h>
#include <sdla_aft_te1.h>
#include <wanpipe_tdm_api_iface.h>


using namespace TelEngine;
namespace {


class TdmWorker;                         // The worker
class TdmThread;                         // The thread
class TdmDevice;                         // Connect a socket at the interface
class TdmInterface;                      // D-channel signalling interface
class TdmSpan;                           // Signalling span used to create voice circuits
class TdmCircuit;                        // A voice circuit
class TdmAnalogCircuit;                  // A voice analog circuit
class TdmSource;                         // Data source
class TdmConsumer;                       // Data consumer


class TdmWorker
{
    friend class TdmThread;
public:
    virtual ~TdmWorker() { stop(); }
    bool running() const;
    // Return true to tell the worker to call again
    // Return false to yield
    virtual bool process() = 0;
protected:
    inline TdmWorker() : m_thread(0) {}
    // Start thread if not started
    bool start(Thread::Priority prio, DebugEnabler* dbg, const String& addr);
    // Stop thread if started
    void stop();
private:
    TdmThread* m_thread;
};


class TdmThread : public Thread
{
    public:
    inline TdmThread(TdmWorker* worker, const String& addr, Priority prio = Normal)
	: Thread(s_threadName,prio),
	  m_worker(worker), m_address(addr)
	{}
    virtual ~TdmThread();
    virtual void run();
    static const char* s_threadName;
private:
    TdmWorker* m_worker;
    String m_address;
};
#define MAX_NUM_OF_TIMESLOTS  31*16
#define MAX_IF_NAME 20
#define TDM_CRC_LEN 2



class TdmDevice : public GenObject
{
public:

    enum Type {
	DChan,
	E1,
	T1,
	NET,
	CPE,
	FXO,
	FXS,
	Control,
	TypeUnknown
    };
    enum Format {
	WP_NONE,
	WP_SLINEAR
    };



    TdmDevice(Type t, SignallingComponent* dbg, unsigned int chan,
	unsigned int circuit);
    TdmDevice(unsigned int chan, bool disableDbg = true);
    inline TdmDevice()
	{}
    ~TdmDevice();
    // Create a socket and bind him to the interface
    bool makeConnection();
    // process data
    int receiveData(u_int8_t *&buf, unsigned int &len);
    // process out of bounds
    bool checkEvents();
    // process rx
    void close();
    bool select(unsigned int usec);
    //send Rx Data
    int request_transmit_data(DataBlock buffer,int timeout, unsigned maxlen = 0);
    int sangoma_tdm_read_event();
    int sangoma_open_tdmapi_span_chan(int span, int chan);
    int sangoma_span_chan_fromif(const char *interface_name, int *span, int *chan);
    int sangoma_tdm_cmd_exec();
    int sangoma_readmsg_tdm(void *hdrbuf,
		int hdrlen, void *databuf, int datalen, int flag);
    int sangoma_writemsg_tdm(void *hdrbuf,
		int hdrlen, void *databuf, unsigned short datalen, int flag);
    int sangoma_tdm_set_codec(int codec);
    int sangoma_tdm_flush_bufs();
    bool setFormat(Format format);
    // flush buffers
    bool flushBuffers();
    // send comandas
    bool sendHook(int hookstatus);
    void decode_alarms(unsigned int alarm_types);
    int transmit_data(unsigned char * buf, int lenbuf, int timeout);
    inline SignallingComponent* owner() const
	{ return m_owner; }
    inline bool valid() const
	{ return m_sock != Socket::invalidHandle(); }
    inline int channel()
	{ return m_chan; }
    inline u_int64_t received() const
	{ return m_received; }
    inline u_int64_t sent() const
	{ return m_sent;}
    inline void resetdatatrans()
	{ m_sent = m_received = 0; }
    inline void receivefrom(int x)
	{ m_received += x; }
    inline void setInterfaceName(const char* name)
	{ m_ifName = name; }
    inline bool canRead() const
	{ return m_canRead; }
    inline int span()
	{ return m_span; }
    inline const String& tdmName() const
	{ return m_ifName; }
    inline bool canRetry()
	{ return errno == EAGAIN || errno == EINTR; }
    inline bool event() const
	{ return m_event ; }

private:
    int m_sock;
    String m_ifName;
    wanpipe_tdm_api_t* m_tdm_api;
    Type m_type;
    int m_span;
    int m_chan;
    SignallingComponent* m_owner;        // Signalling component owning this device
    bool m_event;
    bool m_canRead; 
    bool m_selectError;
    u_int64_t m_sent;
    u_int64_t m_received;
    struct timeval m_tv;
    bool m_down;
};

class TdmInterface : public SignallingInterface, public TdmWorker
{
public:
    TdmInterface(const NamedList& params);
    virtual ~TdmInterface();
    inline bool valid() const
	{ return m_device.valid() && running(); }
    // Initialize interface. Return false on failure
    bool init(TdmDevice::Type type, unsigned int code, unsigned int channel,
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
    // Called by the factory to create TDM interfaces or spans
    static SignallingComponent* create(const String& type, const NamedList& name);
protected:
    // Check if received any data in the last interval. Notify receiver
    virtual void timerTick(const Time& when);
private:
    inline void cleanup(bool release) {
	control(Disable,0);
	attach(0);
    }

    TdmDevice m_device;                  // The device
    Thread::Priority m_priority;         // Worker thread priority
    unsigned char m_errorMask;           // Error mask to filter received error events
    unsigned int m_numbufs;              // The number of buffers used by the channel
    unsigned int m_bufsize;              // The buffer size
    unsigned char* m_buffer;             // Read buffer
    bool m_readOnly;                     // Read only interface
    bool m_sendReadOnly;                 // Print send attempt on readonly interface error
    int m_notify;                       // Notify receiver on channel non idle (0: success. 1: not notified. 2: notified)
    SignallingTimer m_timerRxUnder;      // RX underrun notification
    String m_ifname;
};

class TdmSpan : public SignallingCircuitSpan
{
public:
    inline TdmSpan(const NamedList& params)
	: SignallingCircuitSpan(params.getValue("debugname"),
	    static_cast<SignallingCircuitGroup*>(params.getObject("SignallingCircuitGroup")))
	{}
    virtual ~TdmSpan()
	{}
    // Create circuits. Insert them into the group
    bool init(TdmDevice::Type type,
	const NamedList& config, const NamedList& defaults, const NamedList& params);
};

class TdmCircuit : public SignallingCircuit, public TdmWorker
{
public:
    TdmCircuit(TdmDevice::Type type, unsigned int code, unsigned int channel,
	TdmSpan* span, const NamedList& config, const NamedList& defaults,
	const NamedList& params);
    virtual ~TdmCircuit()
	{ cleanup(false); }
    inline const TdmDevice device() const
	{ return m_device; }
    virtual void destroyed()
	{ cleanup(true); }
    // Change circuit status. Clear events on status change
    // New status is Connect: Open device. Create source/consumer. Start worker
    // Cleanup on disconnect
    virtual bool status(Status newStat, bool sync = false); //
    // Update data format for Tdm device and source/consumer 
    virtual bool updateFormat(const char* format, int direction); //
    // Setup echo canceller or start echo canceller training
    //virtual bool setParam(const String& param, const String& value);
    // Get circuit data
    //virtual bool getParam(const String& param, String& value) const;
    // Get this circuit or source/consumer
    virtual void* getObject(const String& name) const;//
    // Process incoming data
    bool process();
    // Send an event
    virtual bool sendEvent(SignallingCircuitEvent::Type type, NamedList* params = 0);
    // Consume data sent by the consumer
    void consume(const DataBlock& data);//
    //bool enqueueEvent(SignallingCircuitEvent* e);
    bool enqueueEvent(int event, SignallingCircuitEvent::Type type);
protected:
    // Close device. Stop worker. Remove source consumer. Change status. Release memory if requested
    // Reset echo canceller and tone detector if the device is not closed
    void cleanup(bool release, Status stat = Missing, bool stop = true);
    // Get and process some events
    virtual bool processEvent(int event, char c = 0)
	{ return false; }
    // Create source buffer and data source and consumer
    void createData();//
    // Enqueue received events
    bool enqueueEvent(SignallingCircuitEvent* event);

    TdmDevice m_device;                  // The device
    TdmDevice::Type m_type;              // Circuit type
    TdmDevice::Format m_format;
    bool m_echoCancel;                   // Echo canceller state
    bool m_crtEchoCancel;                // Current echo canceller state
    unsigned int m_echoTaps;             // Echo cancel taps
    unsigned int m_echoTrain;            // Echo canceller's train period in miliseconds
    bool m_dtmfDetect;                   // Dtmf detection flag
    bool m_crtDtmfDetect;                // Current dtmf detection state
    bool m_canSend;                      // Not a read only circuit
    bool m_buffering;                    // Buffering initial data
    unsigned char m_idleValue;           // Value used to fill incomplete source buffer
    Thread::Priority m_priority;         // Worker thread priority
    RefPointer<TdmSource> m_source;      // The data source
    TdmConsumer* m_consumer;             // The data consumer
    DataBlock m_sourceBuffer;            // Data source buffer
    DataBlock m_consBuffer;              // Data consumer buffer
    unsigned int m_buflen;               // Data block length
    unsigned int m_consBufMax;           // Max consumer buffer length
    unsigned int m_consErrors;           // Consumer. Total number of send failures
    unsigned int m_consErrorBytes;       // Consumer. Total number of lost bytes
    unsigned int m_consTotal;            // Consumer. Total number of bytes transferred
    unsigned int m_bufpos;               // Auxiliar buffer length
    int m_errno;                         // Last write error
};

class TdmAnalogCircuit : public TdmCircuit
{
public:
    inline TdmAnalogCircuit(TdmDevice::Type type, unsigned int code, unsigned int channel,
	TdmSpan* span, const NamedList& config, const NamedList& defaults,
	const NamedList& params)
	: TdmCircuit(type, code, channel, span, config, defaults, params),
	m_hook(true)
	{}
    virtual ~TdmAnalogCircuit()
	{}
    // Change circuit status. Clear events on status change
    // Reserved: Open device and start worker if old status is not Connected
    // Connect: Create source/consumer
    // Cleanup on disconnect
    virtual bool status(Status newStat, bool sync);
    // Get circuit data
    //virtual bool getParam(const String& param, String& value) const;
    // Set line polarity
    //virtual bool setParam(const String& param, const String& value);
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
class TdmSource : public DataSource
{
public:
    TdmSource(TdmCircuit* circuit, const char* format);
    virtual ~TdmSource();
    inline void changeFormat(const char* format)
	{ m_format = format; }
private:
    String m_address;
};

// Data consumer
class TdmConsumer : public DataConsumer
{
    friend class TdmCircuit;
public:
    TdmConsumer(TdmCircuit* circuit, const char* format);
    virtual ~TdmConsumer();
    inline void changeFormat(const char* format)
	{ m_format = format; }
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
	{ if (m_circuit) m_circuit->consume(data); return invalidStamp(); }
private:
    TdmCircuit* m_circuit;
    String m_address;
};

class TdmModule : public Module
{
public:
    // Additional module commands
    enum StatusCommands {
	TdmSpans       = 0,              // Show all Tdm spans
	TdmChannels    = 1,              // Show all configured Tdm channels
	TdmChannelsAll = 2,              // Show all Tdm channels
	StatusCmdCount = 3
    };
    TdmModule();
    ~TdmModule();
    inline const String& prefix()
	{ return m_prefix; }
    void append(TdmDevice* dev);
    void remove(TdmDevice* dev);
    inline void openClose(bool open) {
	    Lock lock(this);
	    if (open)
		m_active++;
	    else
		m_active--;
	}
    virtual void initialize();
    // Find a device by its Tdm channel
    TdmDevice* findTdmChan(int chan);
    // Additional module status commands
    static String s_statusCmd[StatusCmdCount];
protected:
    virtual bool received(Message& msg, int id);
    virtual void statusModule(String& str);
    virtual void statusParams(String& str);
    //virtual void statusDetail(String& str);
    virtual bool commandComplete(Message& msg, const String& partLine,
	const String& partWord);
private:
    bool m_init;                         // Already initialized flag
    String m_prefix;                     // Module prefix
    String m_statusCmd;                  // Status command for this module (status Zaptel)
    ObjList m_devices;                   // Device list
    // Statistics
    unsigned int m_count;                // The number of devices in the list
    unsigned int m_active;               // The number of active(opened) devices
};


/**
 * Module data and functions
 */
static TdmModule plugin;
YSIGFACTORY2(TdmInterface);              // Factory used to create TDM interfaces and spans
static const char* s_chanParamsHdr = "format=Type|TdmType|Span|SpanPos|Alarms|UsedBy";
static const char* s_spanParamsHdr = "format=Channels|Total|Alarms|Name|Description";

// Get a boolean value from received parameters or other sections in config
// Priority: parameters, config, defaults
static inline bool getBoolValue(const char* param, const NamedList& config,
	const NamedList& defaults, const NamedList& params, bool defVal = false)
{
    defVal = config.getBoolValue(param,defaults.getBoolValue(param,defVal));
    return params.getBoolValue(param,defVal);
}


static void sendModuleUpdate(const String& notif,  const String& device, bool& notifStat, int status = 0)
{
    Message* msg = new Message("module.update");
    msg->addParam("module",plugin.name());
    msg->addParam("interface",device);
    msg->addParam("notify",notif);
    if(notifStat && status == SignallingInterface::LinkUp) {
	notifStat = false;
	Engine::enqueue(msg);
	return;
    }
    if (!notifStat && status == SignallingInterface::LinkDown) {
	notifStat = true;
	Engine::enqueue(msg);
	return;
    }
    TelEngine::destruct(msg);
}

bool TdmWorker::running() const
{
    return m_thread && m_thread->running();
}

bool TdmWorker::start(Thread::Priority prio, DebugEnabler* dbg, const String& addr)
{
    if (!m_thread)
	m_thread = new TdmThread(this,addr,prio);
    if (m_thread->running())
	return true;
    if (m_thread->startup())
	return true;
    m_thread->cancel(true);
    m_thread = 0;
    Debug(dbg,DebugWarn,"Failed to start %s for %s [%p]",
	TdmThread::s_threadName,addr.c_str(),dbg);
    return false;
}

void TdmWorker::stop()
{
    if (!m_thread)
	return;
    Debug(DebugAll,"TdmWorker::stop() [%p]",&m_thread);
    m_thread->cancel();
    while (m_thread)
	Thread::yield();
}

const char* TdmThread::s_threadName = "Tdm Worker";

TdmThread::~TdmThread()
{
    DDebug(&plugin,DebugAll,"%s is terminated for client (%p): %s",
	s_threadName,m_worker,m_address.c_str());
    if (m_worker)
	m_worker->m_thread = 0;
}

void TdmThread::run()
{
    if (!m_worker)
	return;
    Debug(&plugin,DebugAll,"%s is running for client (%p): %s [%p]",
	s_threadName,m_worker,m_address.c_str(),this);
    while (m_worker) {
	if (m_worker->process())
	    Thread::check(true);
	else
	    Thread::yield(true);
    }
}


static TokenDict s_types[] = {
    {"DChan",TdmDevice::DChan},
    {"E1",TdmDevice::E1},
    {"T1",TdmDevice::T1},
    {"NET",TdmDevice::NET},
    {"CPE",TdmDevice::CPE},
    {"FXO",TdmDevice::FXO},
    {"FXS",TdmDevice::FXS},
    {"Control",TdmDevice::Control},
    {"not-used", TdmDevice::TypeUnknown},
    {0,0}
};


TdmDevice::TdmDevice(unsigned int chan, bool disableDbg)
    : m_sock(Socket::invalidHandle()),
    m_type(chan ? TypeUnknown : Control),
    m_chan(chan),
    m_owner(0)
{
    DDebug(&plugin,DebugInfo,"TdmDevice(TdmQuery) type=%s chan=%u [%p]",
	lookup(m_type,s_types),chan,this);
    close();
    m_owner = new SignallingCircuitGroup(0,0,"TdmQuery");
    if (disableDbg)
	m_owner->debugEnabled(false);
    m_sent = m_received = 0;
}

TdmDevice::TdmDevice(Type t, SignallingComponent* dbg, unsigned int chan,
	unsigned int circuit)
    : m_sock(Socket::invalidHandle()),
    m_type(t),
    m_span(-1),
    m_chan(chan),
    m_owner(dbg)
{
    DDebug(&plugin,DebugInfo,"TdmDevice type=%s chan=%u owner=%s cic=%u [%p]",
	lookup(t,s_types),chan,dbg?dbg->debugName():"",circuit,this);
    close();
    if (m_type == Control || m_type == TypeUnknown) {
	m_owner = 0;
	return;
    }
    m_sent = m_received = 0;
    m_down = false;
    plugin.append(this);
}

void TdmDevice::close()
{
    m_span = -1;
    if (!valid())
	return;
    if (::close(m_sock) == 0)
	m_sock = Socket::invalidHandle();
    else
	Debug(&plugin,DebugWarn,"Failed to close tdm device %d: '%s'",errno,strerror(errno));

    if (m_type != Control && m_type != TypeUnknown)
	plugin.openClose(false);
}

TdmDevice::~TdmDevice()
{
    Debug(&plugin,DebugAll,"TdmDevice destruct type=%s owner=%s [%p]",
	lookup(m_type,s_types),m_owner?m_owner->debugName():"",this);
    if (m_type == Control || m_type == TypeUnknown)
	TelEngine::destruct(m_owner);
    close();
    plugin.remove(this);
}

int TdmDevice::sangoma_tdm_read_event()
{
    if (!valid())
	return -1;

#ifdef WP_TDM_FEATURE_EVENTS

    wp_tdm_api_event_t *rx_event;

#if defined(WIN32)	
    rx_event = &last_tdm_api_event_buffer;
#else
    int err;
    m_tdm_api->wp_tdm_cmd.cmd = SIOC_WP_TDM_READ_EVENT;
    err = sangoma_tdm_cmd_exec();
    if (err){
	return err;
    }
    rx_event = &m_tdm_api->wp_tdm_cmd.event;
#endif

    switch (rx_event->wp_tdm_api_event_type){
	case WP_TDMAPI_EVENT_RBS:
	printf("%d: GOT RBS EVENT %p\n",(int)m_sock,m_tdm_api->wp_tdm_event.wp_rbs_event);
	if (m_tdm_api->wp_tdm_event.wp_rbs_event) {
	    m_tdm_api->wp_tdm_event.wp_rbs_event(m_sock,rx_event->wp_tdm_api_event_rbs_bits);
	}
	break;
	
#ifdef WP_TDM_FEATURE_DTMF_EVENTS	
	case WP_TDMAPI_EVENT_DTMF:
	    printf("%d: GOT DTMF EVENT\n",(int)m_sock);
	    if (m_tdm_api->wp_tdm_event.wp_dtmf_event) {
		m_tdm_api->wp_tdm_event.wp_dtmf_event(m_sock,
			rx_event->wp_tdm_api_event_dtmf_digit,
			rx_event->wp_tdm_api_event_dtmf_type,
			rx_event->wp_tdm_api_event_dtmf_port);
	    }
	    break;
#endif
		
	case WP_TDMAPI_EVENT_RXHOOK:
	     printf("%d: GOT RXHOOK EVENT\n",(int)m_sock);
	     if (m_tdm_api->wp_tdm_event.wp_rxhook_event) {
		m_tdm_api->wp_tdm_event.wp_rxhook_event(m_sock,
			rx_event->wp_tdm_api_event_hook_state);
	    }
	    break;

	case WP_TDMAPI_EVENT_RING_DETECT:
	    printf("%d: GOT RXRING EVENT\n",(int)m_sock);
	    if (m_tdm_api->wp_tdm_event.wp_ring_detect_event) {
		m_tdm_api->wp_tdm_event.wp_ring_detect_event(m_sock,
			rx_event->wp_tdm_api_event_ring_state);
	    }
	    break;

	case WP_TDMAPI_EVENT_RING_TRIP_DETECT:
	    printf("%d: GOT RING TRIP EVENT\n",(int)m_sock);
	    if (m_tdm_api->wp_tdm_event.wp_ring_trip_detect_event) {
		m_tdm_api->wp_tdm_event.wp_ring_trip_detect_event(m_sock,
			rx_event->wp_tdm_api_event_ring_state);
	    }
	    break;

#ifdef WP_TDM_FEATURE_FE_ALARM
	case WP_TDMAPI_EVENT_ALARM:
	    printf("%d: GOT FE ALARMS EVENT %i\n",(int)m_sock,
			rx_event->wp_tdm_api_event_alarm);
	    if (m_tdm_api->wp_tdm_event.wp_fe_alarm_event) {
		m_tdm_api->wp_tdm_event.wp_fe_alarm_event(m_sock,
			rx_event->wp_tdm_api_event_alarm);
	}
	break;
#endif
	default:
	    printf("%d: Unknown TDM event!", (int)m_sock);
	    break;
	}
	
	return 0;
#else
	printf("Error: Read Event not supported!\n");
	return -1;
#endif
}

int TdmDevice::sangoma_open_tdmapi_span_chan(int span, int chan) 
{
    char fname[50];
    int fd = 0;

#if defined(WIN32)

	//NOTE: under Windows Interfaces are zero based but 'chan' is 1 based.
	//		Subtract 1 from 'chan'.
    DDebug(fname,DebugNote,"\\\\.\\WANPIPE%d_IF%d",span, chan - 1);

    //prn(verbose, "Opening device: %s...\n", fname);

    fd = CreateFile(	fname, 
			GENERIC_READ | GENERIC_WRITE, 
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			(LPSECURITY_ATTRIBUTES)NULL, 
			OPEN_EXISTING,
			FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
			(HANDLE)NULL
			);
#else
    sprintf(fname,"/dev/wptdm_s%dc%d",span,chan);
    DDebug(m_owner,DebugNote,"fname = %s",fname);
    fd = ::open(fname, O_RDWR);
#endif
    if (fd == Socket::invalidHandle()) {
	String err;
	Thread::errorString(err);
	Debug(m_owner,DebugNote,"Failed to open '%s' %d: '%s'",
	    fname,Thread::lastError(),err.c_str());
    }
    return fd;
}

int TdmDevice::sangoma_span_chan_fromif(const char *interface_name, int *span, int *chan)
{
    char *p = NULL, *sp = NULL, *ch = NULL;
    int ret = 0;
    char data[50];
    strncpy(data, interface_name, 50);
    if ((data[0])) {
	for (p = data; *p; p++) {
	    if (sp && *p == 'c') {
		*p = '\0';
		ch = (p + 1);
		break;
	    }
	    else 
		if (*p == 's') {
		    sp = (p + 1);
		}
	}
	if(ch && sp) {
	    *span = atoi(sp);
	    *chan = atoi(ch);
	    ret = 1;
	}
	else {
	    *span = -1;
	    *chan = -1;
	}
    }
    return ret;
}

int TdmDevice::sangoma_tdm_cmd_exec()
{
    if (!valid())
	return -1;
    int err;
    wanpipe_tdm_api_t *tdm_api1 = m_tdm_api;

#if defined(WIN32)
    err = tdmv_api_ioctl(m_sock, &m_tdm_api->wp_tdm_cmd);
#else
    err = ioctl(m_sock,SIOC_WANPIPE_TDM_API,&tdm_api1->wp_tdm_cmd);
    if (err < 0){
	char tmp[50];
	sprintf(tmp,"TDM API: CMD: %i\n",tdm_api1->wp_tdm_cmd.cmd);
	DDebug(m_owner,DebugWarn,"Command execute failed on interface %s",
	    tdmName().c_str());
	perror(tmp);
	return -1;
    }
#endif
    return err;
}

int TdmDevice::sangoma_readmsg_tdm(void *hdrbuf, int hdrlen, void *databuf, int datalen, int flag)
{
    int rx_len=0;
    if (!valid())
	return -1;

#if defined(WIN32)
    static RX_DATA_STRUCT	rx_data;
    api_header_t *pri;
    wp_tdm_api_rx_hdr_t *tdm_api_rx_hdr;
    wp_tdm_api_rx_hdr_t *user_buf = (wp_tdm_api_rx_hdr_t*)hdrbuf;
    if(hdrlen != sizeof(wp_tdm_api_rx_hdr_t)){
	//error
	prn(1, "Error: sangoma_readmsg_tdm(): invalid size of user's 'header buffer'.\
			Should be 'sizeof(wp_tdm_api_rx_hdr_t)'.\n");
	return -1;
    }
   if(DoReadCommand(fd, &rx_data) ){
		//error
	prn(1, "Error: DoReadCommand() failed! Check messages log.\n");
	return -1;
    }

    //use our special buffer at rxdata to hold received data
    pri = &rx_data.api_header;
    tdm_api_rx_hdr = (wp_tdm_api_rx_hdr_t*)rx_data.data;
    user_buf->wp_tdm_api_event_type = pri->operation_status;
    switch(pri->operation_status) {
	case SANG_STATUS_RX_DATA_AVAILABLE:
	    //prn(verbose, "SANG_STATUS_RX_DATA_AVAILABLE\n");
	    if(pri->data_length > datalen){
		rx_len=0;
		break;
	    }
	    memcpy(databuf, rx_data.data, pri->data_length);
	    rx_len = pri->data_length;
	    break;
	case SANG_STATUS_TDM_EVENT_AVAILABLE:
	    //prn(verbose, "SANG_STATUS_TDM_EVENT_AVAILABLE\n");
	    //make event is accessable for the caller directly:
	    memcpy(databuf, rx_data.data, pri->data_length);
	    rx_len = pri->data_length;
	    //make copy for use with sangoma_tdm_read_event() - indirect access.
	    memcpy(&last_tdm_api_event_buffer,tdm_api_rx_hdr,sizeof(wp_tdm_api_rx_hdr_t));
	    break;
	default:
	    switch(pri->operation_status) {
		case SANG_STATUS_RX_DATA_TIMEOUT:
		    //no data in READ_CMD_TIMEOUT, try again.
		    prn(1, "Error: Timeout on read.\n");
		    break;
		case SANG_STATUS_BUFFER_TOO_SMALL:
		    //Recieved data longer than the pre-configured maximum.
		    //Maximum length is set in 'Interface Properties',
		    //in the 'Device Manager'.
		    prn(1, "Error: Received data longer than buffer passed to API.\n");
		    break;
		case SANG_STATUS_LINE_DISCONNECTED:
		    //Front end monitoring is enabled and Line is
		    //in disconnected state.
		    //Check the T1/E1 line is in "Connected" state,
		    //alse check the Alarms and the message log.
		    prn(1, "Error: Line disconnected.\n");
		    break;
		default:
		   prn(1, "Rx:Unknown Operation Status: %d\n", pri->operation_status);
		   break;
	    }
	    return 0;
	}

#else
    struct msghdr msg;
    struct iovec iov[2];
    memset(&msg,0,sizeof(struct msghdr));
    iov[0].iov_len = hdrlen;
    iov[0].iov_base = hdrbuf;
    iov[1].iov_len = datalen;
    iov[1].iov_base = databuf;
    msg.msg_iovlen = 2;
    msg.msg_iov = iov;
    rx_len = read(m_sock,&msg,datalen+hdrlen);
    rx_len-=sizeof(wp_tdm_api_rx_hdr_t);
    if (rx_len < 0) {
	return -EINVAL;
    }
#endif
#ifdef XDEBUG
    String tmp;
    tmp.hexify(databuf,rx_len,' ');
    Debug(DebugAll,"Reading data on interface %s data=%s",tdmName().c_str(),tmp.c_str());
#endif
    return rx_len;
}

int TdmDevice::sangoma_writemsg_tdm(void *hdrbuf, int hdrlen, void *databuf, unsigned short datalen, int flag)
{
    int bsent;
    if (!valid())
	return -1;

#if defined(WIN32)
    static TX_DATA_STRUCT local_tx_data;
    api_header_t *pri;
    pri = &local_tx_data.api_header;
    pri->data_length = datalen;
    memcpy(local_tx_data.data, databuf, pri->data_length);
    //queue data for transmission
    if(DoWriteCommand(m_sock, &local_tx_data)){
	//error
	prn(1, "Error: DoWriteCommand() failed!! Check messages log.\n");
	return -1;
    }
    bsent=0;
    //check that frame was transmitted
    switch(local_tx_data.api_header.operation_status) {
	case SANG_STATUS_SUCCESS:
	    bsent = datalen;
	    break;
	case SANG_STATUS_TX_TIMEOUT:
	    //error
	    prn(1, "****** Error: SANG_STATUS_TX_TIMEOUT ******\n");
	    //Check messages log or look at statistics.
	    break;
	case SANG_STATUS_TX_DATA_TOO_LONG:
	    //Attempt to transmit data longer than the pre-configured maximum.
	    //Maximum length is set in 'Interface Properties',
	    //in the 'Device Manager'.
	    prn(1, "****** SANG_STATUS_TX_DATA_TOO_LONG ******\n");
	    break;
	case SANG_STATUS_TX_DATA_TOO_SHORT:
	    //Minimum is 1 byte  for Primary   port,
	    // 2 bytes for Secondary port
	    prn(1, "****** SANG_STATUS_TX_DATA_TOO_SHORT ******\n");
	    break;
	case SANG_STATUS_LINE_DISCONNECTED:
	    //Front end monitoring is enabled and Line is
	    //in disconnected state.
	    //Check the T1/E1 line is in "Connected" state,
	    //alse check the Alarms and the message log.
	    prn(1, "****** SANG_STATUS_LINE_DISCONNECTED ******\n");
	    break;
	default:
	    prn(1, "Unknown return code (0x%X) on transmission!\n",
			local_tx_data.api_header.operation_status);
	    break;
    }
#else
    struct msghdr msg;
    struct iovec iov[2];
    memset(&msg,0,sizeof(struct msghdr));
    iov[0].iov_len = hdrlen;
    iov[0].iov_base = hdrbuf;
    iov[1].iov_len = datalen;
    iov[1].iov_base = databuf;
    msg.msg_iovlen = 2;
    msg.msg_iov = iov;
    bsent = write(m_sock,&msg,datalen+hdrlen);
    if (bsent > 0)
	bsent-=sizeof(wp_tdm_api_tx_hdr_t);
    else
	Debug(DebugWarn,"Writing failed");

#endif
    XDebug(DebugAll,"Writing data on interface %s ",tdmName().c_str());
    return bsent;
}

int TdmDevice::sangoma_tdm_set_codec(int codec)
{
    int err;
    m_tdm_api->wp_tdm_cmd.cmd = SIOC_WP_TDM_SET_CODEC;
    m_tdm_api->wp_tdm_cmd.tdm_codec = codec;
    err = sangoma_tdm_cmd_exec();
    return err;
}

int TdmDevice::sangoma_tdm_flush_bufs()
{
#if 0
    tdm_api->wp_tdm_cmd.cmd = SIOC_WP_TDM_FLUSH_BUFFERS;
    err=sangoma_tdm_cmd_exec(fd,tdm_api);
    if (err){
	return err;
    }
#endif
    return 0;
}

bool TdmDevice::makeConnection()
{
    sangoma_span_chan_fromif(tdmName(),&m_span,&m_chan);
    DDebug(m_owner,DebugNote,"Conn: name='%s' span=%d chan=%d [%p]",tdmName().c_str(),m_span,m_chan,this);
    if (m_span > 0 && m_chan > 0) {
	m_sock = sangoma_open_tdmapi_span_chan(m_span,m_chan);
	if (!valid()) {
	    DDebug(m_owner,DebugNote,"Cannot open span=%d chan=%d sock=%d",m_span,m_chan,m_sock);
	    return false;
	}
	m_tdm_api = (wanpipe_tdm_api_t*)malloc(sizeof(wanpipe_tdm_api_t));
	memset(m_tdm_api,0,sizeof(wanpipe_tdm_api_t));
	DDebug(m_owner,DebugNote,"Connection made on interface = %s",tdmName().c_str());
	return true;
    }
    return false;
}


int TdmDevice::receiveData(u_int8_t*& buff, unsigned int &buflen)
{
    wp_tdm_api_rx_hdr_t abuff;
    memset(&abuff,0,sizeof(abuff));
    int buflength = sangoma_readmsg_tdm(&abuff,sizeof(abuff),
		buff,buflen,0);
    if (buflength < 0)
	return -1;
    XDebug(m_owner,DebugNote,"Received %d data",buflength);
    m_sent += buflength;
    return buflength;
}

void TdmDevice::decode_alarms(unsigned int alarm_types)
{
    DDebug(m_owner,DebugNote,"[Framer]: ALOS:%s  LOS:%s RED:%s AIS:%s RAI:%s OOF:%s\n", 
			WAN_TE_ALOS_ALARM(alarm_types),
			WAN_TE_LOS_ALARM(alarm_types),
			WAN_TE_RED_ALARM(alarm_types),
			WAN_TE_AIS_ALARM(alarm_types),
			WAN_TE_RAI_ALARM(alarm_types),
			WAN_TE_OOF_ALARM(alarm_types));
    if (alarm_types & WAN_TE_BIT_LIU_ALARM) {
	DDebug(m_owner,DebugWarn,"[LIU]: Short Ckt:%s Open Ckt:%s Loss of Signal:%s\n",
			WAN_TE_LIU_ALARM_SC(alarm_types),
			WAN_TE_LIU_ALARM_OC(alarm_types),
			WAN_TE_LIU_ALARM_LOS(alarm_types));	
    }
}


bool TdmDevice::checkEvents()
{
    wp_tdm_api_event_t *rx_event;
    int err = sangoma_tdm_read_event();
    if (err != 0)
	return false;
    rx_event = &m_tdm_api->wp_tdm_cmd.event;
    switch (rx_event->wp_tdm_api_event_type) {
	case WP_TDMAPI_EVENT_ALARM:
	    if (!rx_event->wp_tdm_api_event_alarm) {
		DDebug(m_owner,DebugWarn,"%s: Link is disconnected",tdmName().c_str());
		sendModuleUpdate("interfaceDown",tdmName(),m_down,SignallingInterface::LinkDown);
	    }
	    else {
		DDebug(m_owner,DebugInfo,"%s: Link is connected",tdmName().c_str());
		sendModuleUpdate("interfaceUp",tdmName(),m_down,SignallingInterface::LinkUp);
	    }
	    decode_alarms(rx_event->wp_tdm_api_event_alarm);
	    break;
	default :
	    DDebug(m_owner,DebugNote,"%s: Unknown OOB event",tdmName().c_str());
	    break;
    }
    return true;
}

bool TdmDevice::select(unsigned int usec)
{
    if (!valid())
	return false;
    fd_set ready;
    fd_set oob;
    FD_ZERO(&ready);
    FD_ZERO(&oob);
    FD_SET(m_sock,&ready);
    FD_SET(m_sock,&oob);
    m_tv.tv_sec = 0;
    m_tv.tv_usec = usec;
    int sel = ::select(m_sock+1, &ready, NULL, &oob, &m_tv);
    if (sel > 0) {
	m_event = FD_ISSET(m_sock,&oob);
	m_canRead = FD_ISSET(m_sock,&ready);
	m_selectError = false;
	return true;
    }
    if (sel == 0)
	return false;
    if (!(canRetry() || m_selectError)) {
	Debug(m_owner,DebugWarn,"%s Select failed on channel %u. %d: %s [%p]",
	    tdmName().c_str(),m_chan,errno,::strerror(errno),m_owner);
	m_selectError = true;
    }
    return false;
}

int TdmDevice::transmit_data(unsigned char *buf, int length, int timeout)
{
    int  err = 0;
    fd_set write;
    char hdr[16];
    if (!valid()) {
	DDebug(m_owner,DebugNote,"TdmDevice::transmit_data socket invalid %d",m_sock);
	return -1;
    }
    FD_ZERO(&write);
    FD_SET(m_sock, &write);
    for (int i = 0;i <16; i++)
	hdr[i]= i;
    m_tv.tv_sec = 0;
    m_tv.tv_usec = timeout;
    int ret = ::select((m_sock+1),NULL, &write, NULL, &m_tv);
    if(ret > 0) {
	if (FD_ISSET(m_sock, &write)) {
	    err = sangoma_writemsg_tdm(hdr, 16, buf, length, 0);
	    if (err < 0) {
		DDebug(m_owner,DebugNote,"%s: Error: Failed to transmit data, %d %s "
		,tdmName().c_str(),errno,::strerror(errno));
		return 0;
	    }
	}
    }
    return err;
}

int TdmDevice::request_transmit_data(DataBlock data, int timeout, unsigned int maxlen)
{
    if (maxlen == 0 || maxlen > data.length())
	maxlen = data.length();
    int len = transmit_data((unsigned char*)data.data(),maxlen,timeout);
    return len;
}

bool TdmDevice::setFormat(Format format)
{
    int aux = sangoma_tdm_set_codec(format);
    if (aux != 0) {
	Debug(m_owner,DebugNote,"Failed to set format '%d' on channel %d",
	    format,m_sock);
	return false;
    }
    DDebug(m_owner,DebugAll,"Format set to '%d' on channel %d",
		format,m_sock);
    return true;
}

bool TdmDevice::flushBuffers()
{
    int x = sangoma_tdm_flush_bufs();
    if (x != 0) {
	DDebug(m_owner,DebugWarn,"Error flushing TDM buffers");
	return false;
    }
    return true;
}

bool TdmDevice::sendHook(int tx)
{
    m_tdm_api->wp_tdm_cmd.cmd = (unsigned int)tx;
    int err = sangoma_tdm_cmd_exec();
    if (!err) {
	DDebug(m_owner,DebugNote,"Unable to send hook events");
	return false;
    }
    return true;
}

// Tdm span

bool TdmSpan::init(TdmDevice::Type type,
	const NamedList& config, const NamedList& defaults, const NamedList& params)
{
    String voice = params.getValue("voicechans",config.getValue("voicechans"));
    unsigned int chans = 0;
    bool digital = true;
    switch (type) {
	case TdmDevice::E1:
	    if (!voice)
		voice = "1-15.17-31";
	    chans = 31;
	    m_increment = 32;
	    break;
	case TdmDevice::T1:
	    if (!voice)
		voice = "1-23";
	    chans = 24;
	    m_increment = 24;
	    break;
	case TdmDevice::NET:
	case TdmDevice::CPE:
	    if (!voice)
		voice = "1.2";
	    chans = 3;
	    m_increment = 3;
	    break;
	case TdmDevice::FXO:
	case TdmDevice::FXS:
	    digital = false;
	    if (!voice)
		voice = "1";
	    chans = (unsigned int)-1;
	    break;
	default:
	    Debug(m_group,DebugWarn,
		"TdmSpan('%s'). Can't create circuits for type=%s [%p]",
		id().safe(),lookup(type,s_types),this);
	    return false;
    }

    unsigned int count = 0;
    unsigned int* cics = SignallingUtils::parseUIntArray(voice,1,chans,count,true);
    if (!cics) {
	Debug(m_group,DebugWarn,
	    "TdmSpan('%s'). Invalid voicechans='%s' (type=%s,chans=%u) [%p]",
	    id().safe(),voice.safe(),lookup(type,s_types),chans,this);
	return false;
    }

    if (!digital)
	m_increment = chans = count;
    m_increment = config.getIntValue("increment",m_increment);
    unsigned int start = config.getIntValue("start",params.getIntValue("start",0));
    // Create and insert circuits
    unsigned int added = 0;
    DDebug(m_group,DebugNote,
	"TdmSpan('%s'). Creating circuits starting with %u [%p]",
	id().safe(),start,this);
    for (unsigned int i = 0; i < count; i++) {
	unsigned int code = start + cics[i];
	unsigned int channel = cics[i];
	TdmCircuit* cic = 0;
	DDebug(m_group,DebugInfo,
	    "TdmSpan('%s'). Creating circuit code=%u channel=%u [%p]",
	    id().safe(),code,channel,this);
	if (digital)
	    cic = new TdmCircuit(type,code,channel,this,config,defaults,params);
	else
	    cic = new TdmAnalogCircuit(type,code,channel,this,config,defaults,params);
	if (m_group->insert(cic)) {
	    added++;
	    continue;
	}
	TelEngine::destruct(cic);
	Debug(m_group,DebugWarn,
	    "TdmSpan('%s'). Duplicate circuit code=%u (channel=%u) [%p]",
	    id().safe(),code,channel,this);
    }
    if (!added) {
	Debug(m_group,DebugWarn,"TdmSpan('%s'). No circuits inserted for this span [%p]",
	    id().safe(),this);
	delete[] cics;
	return false;
    }
    if (m_group && m_group->debugAt(DebugInfo)) {
	String s;
	s << "driver=" << plugin.debugName();
	s << " section=" << !params.null() ? params.c_str() : config.c_str();
	s << " type=" << lookup(type,s_types);
	String c,ch;
	for (unsigned int i = 0; i < count; i++) {
	    c.append(String(start+cics[i]),",");
	    ch.append(String(cics[i]),",");
	}
	s << " channels=" << ch;
	s << " circuits=" << c;
	Debug(m_group,DebugInfo,"TdmSpan('%s') %s [%p]",id().safe(),s.c_str(),this);
    }
    delete[] cics;
    return true;
}

// tdm circut

TdmCircuit::TdmCircuit(TdmDevice::Type type, unsigned int code, unsigned int channel,
	TdmSpan* span, const NamedList& config, const NamedList& defaults,
	const NamedList& params)
    : SignallingCircuit(TDM,code,Idle,span->group(),span),
    m_device(type,span->group(),channel,code),
    m_format(TdmDevice::WP_SLINEAR),
    m_echoCancel(false),
    m_crtEchoCancel(false),
    m_echoTaps(0),
    m_echoTrain(400),
    m_dtmfDetect(false),
    m_crtDtmfDetect(false),
    m_canSend(true),
    m_buffering(true),
    m_idleValue(255),
    m_priority(Thread::Normal),
    m_consumer(0),
    m_buflen(0),
    m_consBufMax(0),
    m_consErrors(0),
    m_consErrorBytes(0),
    m_consTotal(0),
    m_bufpos(0),
    m_errno(0)
{
    int sp = params.getIntValue("span",config.getIntValue("span",1));
    String name;
    name << "s" << sp << "c" << channel;
    m_device.setInterfaceName(name);
    m_device.makeConnection();
    m_dtmfDetect = params.getBoolValue("dtmfdetect",config.getBoolValue("dtmfdetect",true));
    if (m_dtmfDetect) {
	Debug(group(),DebugAll,
	    "TdmCircuit(%u). DTMF detection is not supported by hardware [%p]",
	    code,this);
	m_dtmfDetect = false;
    }
    m_crtDtmfDetect = m_dtmfDetect;
    int tmp = defaults.getIntValue("echotaps");
    tmp = params.getIntValue("echotaps",config.getIntValue("echotaps",tmp));
    m_echoTaps = tmp >= 0 ? tmp : 0;
    m_crtEchoCancel = m_echoCancel = m_echoTaps;
    tmp = defaults.getIntValue("echotrain",400);
    tmp = (unsigned int)params.getIntValue("echotrain",config.getIntValue("echotrain",tmp));
    m_echoTrain = tmp >= 0 ? tmp : 0;
    m_canSend = !getBoolValue("readonly",params,config,defaults);
    m_buflen = defaults.getIntValue("buflen",80);
    m_buflen = (unsigned int)params.getIntValue("buflen",config.getIntValue("buflen",m_buflen));
    m_consBufMax = m_buflen * 4;
    m_sourceBuffer.assign(0,m_buflen);
    m_idleValue = defaults.getIntValue("idlevalue",0xff);
    m_idleValue = params.getIntValue("idlevalue",config.getIntValue("idlevalue",m_idleValue));
    String priority = defaults.getValue("priority","100");
    m_priority = Thread::priority(params.getValue("priority",config.getValue("priority",priority)));
    m_format = TdmDevice::WP_SLINEAR;

    if (group() && group()->debugAt(DebugAll)) {
	String s;
	s << " driver=" << plugin.debugName();
	s << " type=" << lookup(type,s_types);
	s << " channel=" << channel;
	s << " cic=" << code;
	s << " dtmfdetect=" << String::boolText(m_dtmfDetect);
	s << " echotaps=" << m_echoTaps;
	s << " echotrain=" << m_echoTrain;
	s << " buflen=" << m_buflen;
	s << " readonly=" << String::boolText(!m_canSend);
	s << " idlevalue=" << (unsigned int)m_idleValue;
	s << " priority=" << Thread::priority(m_priority);
	DDebug(group(),DebugNote,"TdmCircuit %s [%p]",s.c_str(),this);
    }
}

bool TdmCircuit::process()
{
    if (!(m_device.valid() && SignallingCircuit::status() == Connected && m_source))
	return false;
    if (!m_device.select(500))
	return false;
    if (!m_device.canRead())
	return false;

    unsigned int len = m_buflen - m_bufpos;
    u_int8_t * buf = m_sourceBuffer.data(m_bufpos,len);
    if (!buf) {
	m_bufpos = 0;
	return false;
    }
    int r = m_device.receiveData(buf,len);
    if (m_source && r > 0) {
	m_bufpos += r;
	if (m_bufpos < m_buflen)
	    return true;
	m_source->Forward(m_sourceBuffer);
	m_bufpos = 0;
	return true;
    }
    return false;
}


bool TdmCircuit::status(Status newStat, bool sync)
{
    if (SignallingCircuit::status() == newStat)
	return true;
    if (SignallingCircuit::status() == Missing) {
	Debug(group(),DebugNote,
	    "TdmCircuit(%u). Can't change status to '%s'. Circuit is missing [%p]",
	    code(),lookupStatus(newStat),this);
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
		DDebug(group(),DebugAll,"TdmCircuit(%u). Changed status to '%s' [%p]",
		    code(),lookupStatus(newStat),this);
	    if (newStat == Connected)
		break;
	    if (oldStat == Connected)
		cleanup(false,newStat,true);
	    return true;
	default: ;
	    Debug(group(),DebugWarn,
		"TdmCircuit(%u). Can't change status to unhandled value %u [%p]",
		code(),newStat,this);
	    return false;
    }
    // Connected: create source/consumer, start worker
    while (true) {
 	createData();
	String addr;
	if (group())
	    addr << group()->debugName() << "/";
	addr << code();
	if (!TdmWorker::start(m_priority,group(),addr))
	    break;
	return true;
    }
    // Rollback on error
    cleanup(false,oldStat);
    return false;
}

void TdmCircuit::createData()
{
    m_buffering = true;
    m_sourceBuffer.assign(0,m_buflen);
    const char* format = "alaw";
    m_source = new TdmSource(this,format);
    DDebug(DebugInfo,"Voice interface '%s'",m_device.tdmName().c_str());
    if (m_source)
	m_source->deref();
    if (m_canSend)
	m_consumer = new TdmConsumer(this,format);
}

void TdmCircuit::cleanup(bool release, Status stat, bool stop)
{
    if (stop || release)
	TdmWorker::stop();
    // Don't deref the source - the thread will do it
    m_source = 0;
    Debug(DebugNote,"Cleanup release=%s received="FMT64" sent="FMT64" on interface: %s",
	String::boolText(release),m_device.received(),m_device.sent(),m_device.tdmName().c_str());
    TelEngine::destruct(m_consumer);
    if (release) {
	SignallingCircuit::destroyed();
	return;
    }
    status(stat);
    m_device.resetdatatrans();
    m_sourceBuffer.clear(false);
    m_consBuffer.clear(false);
}

bool TdmCircuit::updateFormat(const char* format, int direction)
{
    if (!(format && *format))
	return false;
    bool consumerChanged = true;
    bool sourceChanged = true;
    if (direction == -1 || direction == 0) {
	if (m_consumer && m_consumer->getFormat() != format) {
	    m_consumer->changeFormat(format);
	    DDebug(group(),DebugAll,"TdmCircuit(%u). Consumer format set to '%s' [%p]",
		code(),format,this);
	}
	else
	    consumerChanged = false;
    }
    if (direction == 1 || direction == 0) {
	if (m_source && m_source->getFormat() != format) {
	    m_source->changeFormat(format);
	    DDebug(group(),DebugAll,"TdmCircuit(%u). Source format set to '%s' [%p]",
		code(),format,this);
	}
	else
	    sourceChanged = false;
    }
	bool aux = consumerChanged && sourceChanged;
    return aux;
}

void* TdmCircuit::getObject(const String& name) const
{

    if (name == "DataSource")
	return m_source;
    if (name == "DataConsumer")
	return m_consumer;
    return 0;
}

bool TdmCircuit::sendEvent(SignallingCircuitEvent::Type type, NamedList* params)
{
    if (!m_canSend)
	return false;
    if (type == SignallingCircuitEvent::Dtmf) {
	const char* tones = 0;
	bool dtmf = true;
	bool dial = true;
	if (params) {
	    tones = params->getValue("tone");
	    dtmf = !params->getBoolValue("pulse",false);
	    dial = params->getBoolValue("dial",true);
	}
	//TODO send the events
	return true;
    }

    Debug(group(),DebugNote,"TdmCircuit(%u). Unable to send unknown event %u [%p]",
	code(),type,this);
    return false;
}

void TdmCircuit::consume(const DataBlock& data)
{
    if (!(SignallingCircuit::status() == Connected && m_canSend && data.length()))
	return;
    m_device.receivefrom(data.length());
    if (m_consBuffer.length() + data.length() <= m_consBufMax)
	m_consBuffer += data;
    else {
	XDebug(group(),DebugInfo,
	    "TdmCircuit(%u). Buffer overrun old=%u channel=%u (%d: %s) [%p]",
	    code(),m_consBuffer.length(),m_device.channel(),m_errno,
	    ::strerror(m_errno),this);
	m_consErrors++;
	m_consErrorBytes += m_consBuffer.length();
    }
    // Send buffer. Stop on error
    if (m_buffering && (m_consBuffer.length() < 3 * m_buflen)) {
	return;
    }
    m_buffering = false;
    while (m_consBuffer.length() >= m_buflen) {
	int w = m_device.request_transmit_data(m_consBuffer,500,m_buflen);
	if (w <= 0) {
	    m_errno = errno;
	    break;
	} else {
	    m_consBuffer.cut(-w);
	    m_errno = 0;
	}
     }
}

inline bool TdmCircuit::enqueueEvent(SignallingCircuitEvent* e)
{
    if (e) {
	addEvent(e);
	DDebug(group(),e->type() != SignallingCircuitEvent::Unknown?DebugAll:DebugWarn,
	    "TdmCircuit(%u). Enqueued event '%s' [%p]",code(),e->c_str(),this);
    }
    return true;
}


inline bool TdmCircuit::enqueueEvent(int event, SignallingCircuitEvent::Type type)
{
    return enqueueEvent(new SignallingCircuitEvent(this,type,(const char*)event));
}

// analog circuit
bool TdmAnalogCircuit::status(Status newStat, bool sync)
{
    if (SignallingCircuit::status() == newStat)
	return true;
    if (SignallingCircuit::status() == Missing) {
	Debug(group(),DebugNote,
	    "TdmCircuit(%u). Can't change status to '%u'. Circuit is missing [%p]",
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
	    Debug(group(),DebugWarn,
		"TdmCircuit(%u). Can't change status to unhandled value %u [%p]",
		code(),newStat,this);
	    return false;
    }

    Status oldStat = SignallingCircuit::status();
    if (!SignallingCircuit::status(newStat,sync))
	return false;
    clearEvents();
    if (!Engine::exiting())
	DDebug(group(),DebugAll,"TdmCircuit(%u). Changed status to %u [%p]",
	    code(),newStat,this);

    if (newStat != Connected && m_device.valid())
	m_device.flushBuffers();

    if (newStat == Reserved) {
	// Just cleanup if old status was Connected or the device is already valid
	// Otherwise: open device and start worker
	if (oldStat == Connected || m_device.valid())
	    cleanup(false,Reserved,false);
	else {return true;
	    String addr;
	    if (group())
		addr << group()->debugName() << "/";
	    addr << code();
	    if (m_device.makeConnection() && TdmWorker::start(m_priority,group(),addr))
		m_device.setFormat(TdmDevice::WP_SLINEAR);
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

bool TdmAnalogCircuit::sendEvent(SignallingCircuitEvent::Type type, NamedList* params)
{
    if (!m_canSend)
	return false;

    if (type == SignallingCircuitEvent::Dtmf)
	return TdmCircuit::sendEvent(type,params);

    switch (type) {
	case SignallingCircuitEvent::OnHook:
	    if (!m_device.sendHook(WP_TDMAPI_EVENT_RXHOOK_ON))
		return false;
	    changeHook(true);
	    return true;
	case SignallingCircuitEvent::OffHook:
	    if (!m_device.sendHook(WP_TDMAPI_EVENT_RXHOOK_OFF))
		return false;
	    changeHook(false);
	    return true;
	case SignallingCircuitEvent::Polarity:
	    if (!params)
		return false;
	    return setParam("polarity",params->getValue("polarity"));
	case SignallingCircuitEvent::RingBegin:
	    return m_device.sendHook(WP_TDMAPI_EVENT_RING_PRESENT);
	case SignallingCircuitEvent::RingEnd:
	    return m_device.sendHook(WP_TDMAPI_EVENT_RING_STOP);
	default: ;
    }
    return TdmCircuit::sendEvent(type,params);
}

void TdmAnalogCircuit::changeHook(bool hook)
{
    if (m_hook == hook)
	return;
    DDebug(group(),DebugInfo,"TdmCircuit(%u). Hook state changed to %s [%p]",
	code(),hook?"ON":"OFF",this);
    m_hook = hook;
}

bool TdmAnalogCircuit::processEvent(int event, char c)
{
    switch (event) {
	case WP_TDMAPI_EVENT_RING:
	    return enqueueEvent(event,SignallingCircuitEvent::RingerOn);
	case WP_TDMAPI_EVENT_TXSIG_ONHOOK:
	    changeHook(true);
	    return enqueueEvent(event,SignallingCircuitEvent::OnHook);
	case WP_TDMAPI_EVENT_RING_DETECT:
	    m_device.setFormat(TdmDevice::WP_SLINEAR);
	    return enqueueEvent(event,SignallingCircuitEvent::RingBegin);
	case WP_TDMAPI_EVENT_TXSIG_OFFHOOK:
	    return enqueueEvent(event,SignallingCircuitEvent::OffHook);
	case WP_TDMAPI_EVENT_SETPOLARITY:
	    return enqueueEvent(event,SignallingCircuitEvent::Polarity);
	default:
	    Debug(group(),DebugWarn,"TdmCircuit(%u). Unknown event %u [%p]",
		code(),event,this);
    }
    return false;
}

bool TdmAnalogCircuit::process()
{
   if (!(m_device.valid() && SignallingCircuit::status() != SignallingCircuit::Disabled))
	return false;

    //m_device.pollHook();
    m_device.checkEvents();

    if (!(m_source && m_device.select(10) && m_device.canRead())) 
	return false;
    u_int8_t *buf = (u_int8_t*)m_sourceBuffer.data();
    int r = m_device.receiveData(buf,m_buflen);
    if (m_device.event())
	m_device.checkEvents();
    if (r > 0) {
	if ((unsigned int)r != m_sourceBuffer.length())
	    ::memset((unsigned char*)m_sourceBuffer.data() + r,m_idleValue,m_sourceBuffer.length() - r);
	XDebug(group(),DebugAll,"TdmCircuit(%u). Forwarding %u bytes [%p]",
	    code(),m_sourceBuffer.length(),this);
	m_source->Forward(m_sourceBuffer);
	return true;
    }

    return false;
}

// Interface
SignallingComponent* TdmInterface::create(const String& type, const NamedList& name)
{
    bool circuit = true;
    if (type == "SignallingInterface")
	circuit = false;
    else if (type == "SignallingCircuitSpan")
	;
    else
	return 0;

    // Check in params if the module witch should create the component is specifyed
    // if the module is specifyed and is not tdmcard let the specifyed module to create the component
    const String* module = name.getParam("module");
    if (module && *module != "tdmcard")
	return 0;
    Configuration cfg(Engine::configFile("tdmcard"));
    const char* sectName = name.getValue((circuit ? "voice" : "sig"),name.getValue("basename",name));
    NamedList* config = cfg.getSection(sectName);
    if (module) {
	DDebug(&plugin,DebugAll,"Replace config params in section %s",c_safe(sectName));
	if (!config) {
	    cfg.createSection(sectName);
	    config = cfg.getSection(sectName);
	}
	config->copyParams(name);
	if(!cfg.save())
	    DDebug(&plugin,DebugAll,"Failed to save configuration in file %s ",module->c_str());
    }
    else if (!config){
	DDebug(&plugin,DebugAll,"No section '%s' in configuration",c_safe(sectName));
	return 0;
    }
#ifdef DEBUG
    if (plugin.debugAt(DebugAll)) {
	String tmp;
	name.dump(tmp,"\r\n  ",'\'',true);
	Debug(&plugin,DebugAll,"TdmInterface::create %s%s",
	    (circuit ? "span" : "interface"),tmp.c_str());
    }
#endif
    NamedList dummy("general");
    NamedList* general = cfg.getSection("general");
    if (!general)
	general = &dummy;
    String sDevType = config->getValue("type");
    TdmDevice::Type devType = (TdmDevice::Type)lookup(sDevType,s_types,TdmDevice::E1);
    if (circuit) {
	TdmSpan* span = new TdmSpan(name);
	bool ok = false;
	if (span->group())
	    ok = span->init(devType,*config,*general,name);
	else
	    Debug(&plugin,DebugWarn,"Can't create span '%s'. Group is missing",
		span->id().safe());
	if (ok)
	    return span;
	TelEngine::destruct(span);
	return 0;
    }

    // Check span type
    if (devType != TdmDevice::E1 && devType != TdmDevice::T1 && 
		devType != TdmDevice::NET && devType != TdmDevice::CPE) {
	Debug(&plugin,DebugWarn,"Section '%s'. Can't create D-channel for type='%s'",
	    config->c_str(),sDevType.c_str());
	return 0;
    }
    // Check channel
    String sig = config->getValue("sigchan");
    unsigned int count;
    if (devType == TdmDevice::E1)
	count = 31;
    else
	count = (devType == TdmDevice::T1) ? 24 : 3;
    if (!sig) {
	if (devType == TdmDevice::E1)
	    sig = 16;
	else
	    sig = (devType == TdmDevice::T1) ? 24 : 3;
    }
    unsigned int code = (unsigned int)sig.toInteger(0);
    if (!(sig && code && code <= count)) {
	Debug(&plugin,DebugWarn,"Section '%s'. Invalid sigchan='%s' for type='%s'",
	    config->c_str(),sig.safe(),sDevType.c_str());
	return false;
    }
    TdmInterface* iface = new TdmInterface(name);
    if (iface->init(devType,code,code,*config,*general,name))
	return iface;
    TelEngine::destruct(iface);
    return 0;
}


TdmInterface::TdmInterface(const NamedList& params)
    : SignallingComponent(params),
      m_device(TdmDevice::DChan,this,0,0),
      m_priority(Thread::Normal),
      m_errorMask(255),
      m_numbufs(16), m_bufsize(1024), m_buffer(0),
      m_readOnly(false), m_sendReadOnly(false),
      m_notify(0),
      m_timerRxUnder(0)
{
    m_buffer = new unsigned char[m_bufsize];
}

TdmInterface::~TdmInterface()
{
    cleanup(false);
    delete[] m_buffer;
}

void* TdmInterface::getObject(const String& name) const
{
    if (name == "TdmInterface")
	return (void*)this;
    return SignallingInterface::getObject(name);
}


bool TdmInterface::process()
{
    if (!m_device.select(100))
	return false;
    if (!m_device.canRead())
	return false;
    m_device.checkEvents();
    int r = m_device.receiveData(m_buffer,m_bufsize);
    if (r <= 0)
	return false;
    DataBlock packet(m_buffer,r,false);
    if (packet.length()) {
	receivedPacket(packet);
    }
    packet.clear(false);
    return true;

}


bool TdmInterface::init(TdmDevice::Type type, unsigned int code, unsigned int channel,
	const NamedList& config, const NamedList& defaults, const NamedList& params)
{
    m_readOnly = getBoolValue("readonly",params,config,defaults);
    String priority = defaults.getValue("priority");
    m_priority = Thread::priority(params.getValue("priority",config.getValue("priority",priority)));
    int rx = params.getIntValue("rxunderrun");
    if (rx > 0)
	m_timerRxUnder.interval(rx);
    int i = params.getIntValue("errormask",config.getIntValue("errormask",255));
    m_errorMask = ((i >= 0 && i < 256) ? i : 255);
    int sp = params.getIntValue("spam",config.getIntValue("span",1));
    (m_ifname = "") << "s" << sp << "c" << channel;
    if (debugAt(DebugInfo)) {
	String s;
	s << " driver=" << plugin.debugName();
	s << " section=" << config.c_str();
	s << " type=" << lookup(type,s_types);
	s << " channel=" << channel;
	s << " errormask=" << (unsigned int)m_errorMask;
	s << " readonly=" << String::boolText(m_readOnly);
	s << " rxunderruninterval=" << (unsigned int)m_timerRxUnder.interval() << " ms";
	s << " numbufs=" << (unsigned int)m_numbufs;
	s << " bufsize=" << (unsigned int)m_bufsize;
	s << " priority=" << Thread::priority(m_priority);
	Debug(this,DebugInfo,"D-channel: %s [%p]",s.c_str(),this);
    }
    return true;
}

bool TdmInterface::control(Operation oper, NamedList* params)
{
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
	m_device.setInterfaceName(m_ifname);
	bool ok = m_device.valid()|| m_device.makeConnection();
	if (ok) {
	    ok = TdmWorker::start(m_priority,this,debugName());
	}
	if (ok) {
	    Debug(this,DebugAll,"Enabled [%p]",this);
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
    TdmWorker::stop();
    m_device.close();
    if (ok)
	Debug(this,DebugAll,"Disabled [%p]",this);
    return true;
}


void TdmInterface::timerTick(const Time& when)
{
}

bool TdmInterface::transmitPacket(const DataBlock& packet, bool repeat, PacketType type)
{

    if (m_readOnly) {
	if (!m_sendReadOnly)
	    Debug(this,DebugWarn,"Attempt to send data on read only interface");
	m_sendReadOnly = true;
	return false;
    }
    if (!m_device.valid())
	return false;
    XDebug(DebugAll,"Request transmit %u data on interface %s",packet.length(),m_ifname.c_str());
    int len = m_device.request_transmit_data(packet,5000,packet.length());
    if (len != (int)packet.length())
	return false;
    return true;
}

// source
inline void setAddr(String& addr, TdmCircuit* cic)
{
#ifdef XDEBUG
    if (cic) {
	if (cic->group())
	    addr << cic->group()->debugName() << "/";
	addr << cic->code();
    }
    else
	addr = -1;
#endif
}

TdmSource::TdmSource(TdmCircuit* circuit, const char* format)
    : DataSource(format)
{
    setAddr(m_address,circuit);
    DDebug(&plugin,DebugAll,"TdmSource::TdmSource() cic=%s [%p]",m_address.c_str(),this);
}

TdmSource::~TdmSource()
{
    DDebug(&plugin,DebugAll,"TdmSource::~TdmSource() cic=%s [%p]",m_address.c_str(),this);
}


TdmConsumer::TdmConsumer(TdmCircuit* circuit, const char* format)
    : DataConsumer(format),
    m_circuit(circuit)
{
    setAddr(m_address,circuit);
    DDebug(&plugin,DebugAll,"TdmConsumer::TdmConsumer() cic=%s [%p]",m_address.c_str(),this);
}

TdmConsumer::~TdmConsumer()
{
    DDebug(&plugin,DebugAll,"TdmConsumer::~TdmConsumer() cic=%s [%p]",m_address.c_str(),this);
}

String TdmModule::s_statusCmd[StatusCmdCount] = {"spans","channels","all"};

TdmModule::TdmModule()
    : Module("tdmcard","misc",true),
    m_init(false),
    m_count(0),
    m_active(0)
{
    Output("Loaded module Sangoma TDM");
    m_prefix << name() << "/";
    m_statusCmd << "status " << name();
}

TdmModule::~TdmModule()
{
    Output("Unloading module Sangoma TDM");
}

void TdmModule::append(TdmDevice* dev)
{
    if (!dev)
	return;
    Lock lock(this);
    m_devices.append(dev)->setDelete(false);
    m_count = m_devices.count();
}

void TdmModule::remove(TdmDevice* dev)
{
    if (!dev)
	return;
    Lock lock(this);
    m_devices.remove(dev,false);
    m_count = m_devices.count();
}

void TdmModule::initialize()
{
    Output("Initializing module Sangoma TDM");

    Configuration cfg(Engine::configFile("tdmcard"));
    cfg.load();

    NamedList dummy("");
    NamedList* general = cfg.getSection("general");
    if (!general)
	general = &dummy;
    TdmDevice dev(0,false);
    if (!dev.valid())
	Debug(this,DebugNote,"Failed to open Tdm device: driver might not be loaded %d: (%s)",errno,::strerror(errno));
    if (!m_init) {
	m_init = true;
	setup();
	installRelay(Command);
    }
}


// Find a device by its Tdm channel
TdmDevice* TdmModule::findTdmChan(int chan)
{
    Lock lock(this);
    for (ObjList* o = m_devices.skipNull(); o; o = o->skipNext()) {
	TdmDevice* dev = static_cast<TdmDevice*>(o->get());
	if ((int)dev->channel() == chan) {
	    return dev;
	}
    }
    return 0;
}

bool TdmModule::received(Message& msg, int id)
{
    if (id == Status) {
	String dest = msg.getValue("module");

	// Module status
	if (!dest || dest == name()) {
	    Module::msgStatus(msg);
	    return false;
	}
	Lock lock(this);
	// Device status
	if (dest.startSkip(prefix(),false)) {
	    TdmDevice* dev = findTdmChan((unsigned int)dest.toInteger());
	    if (!dev)
		return false;
	    msg.retValue().clear();
	    msg.retValue() << "name=" << dev->tdmName();
	    msg.retValue() << ",module=" << name();
	    if (dev->span() != -1)
		msg.retValue() << ",span=" << dev->span();
	    msg.retValue() << "\r\n";
	    return true;
	}

	// Additional commands
	if (dest.startSkip(name(),false)) {
	    dest.trimBlanks();
	    int cmd = 0;
	    for (; cmd < StatusCmdCount; cmd++)
		if (s_statusCmd[cmd] == dest)
		    break;
	    if (cmd == TdmSpans) {
		TdmDevice* ctl = new TdmDevice(0);
		NamedList ver("");
		msg.retValue().clear();
		msg.retValue() << "module=" << name() << "," << s_spanParamsHdr;
		msg.retValue() << ";version=" << ver.getValue("version");
		msg.retValue() << ",echocanceller=" << ver.getValue("echocanceller");
		TelEngine::destruct(ctl);
	    }
	    else if (cmd == TdmChannels || cmd == TdmChannelsAll) {
		TdmDevice* ctl = new TdmDevice(0);
		String s;
		unsigned int chan = 0;
		for (int span = 1; ctl->valid(); span++) {
		    // Check span
		    NamedList p("");
		    // Get info
		    int chans = p.getIntValue("total-chans");
		    for (int i = 0; i < chans; i++) {
			chan++;
			// Get device
			// Create or reset debuger to avoid unwanted debug output to console
			bool created = false;
			bool opened = false;
			TdmDevice* dev = findTdmChan(chan);
			if (!dev) {
			    dev = new TdmDevice(chan);
			    created = true;
			}
			else if (dev->owner())
			    dev->owner()->debugEnabled(false);
			if (!dev->valid()) {
			    dev->makeConnection();
			    opened = true;
			}

			// Cleanup if we opened/created the device
			if (created) {
			    TelEngine::destruct(dev);
			    continue;
			}
			if (opened)
			    close(chan);
			if (dev->owner())
			    dev->owner()->debugEnabled(true);
		    }
		}
		TelEngine::destruct(ctl);

		msg.retValue().clear();
		msg.retValue() << "module=" << name() << "," << s_chanParamsHdr;
		msg.retValue() << ";used=" << m_count << ",total=" << chan;
		msg.retValue() << s;
	    }
	    else
		return false;
	    msg.retValue() << "\r\n";
	    return true;
	}

	return false;
    }
    return Module::received(msg,id);
}

void TdmModule::statusModule(String& str)
{
    Module::statusModule(str);
    str.append(s_chanParamsHdr,",");
}

void TdmModule::statusParams(String& str)
{
    Module::statusParams(str);
    str.append("active=",",") << m_active;
    str << ",count=" << m_count;
}

bool TdmModule::commandComplete(Message& msg, const String& partLine,
    const String& partWord)
{
    bool ok = Module::commandComplete(msg,partLine,partWord);
    if (!partLine.startsWith("status"))
	return ok;
    Lock lock(this);
    if (name().startsWith(partWord)) {
	if (m_devices.skipNull())
	    msg.retValue().append(prefix(),"\t");
	return ok;
    }
    if (partLine == m_statusCmd) {
	for (unsigned int i = 0; i < StatusCmdCount; i++)
	    if (!partWord || s_statusCmd[i].startsWith(partWord))
		msg.retValue().append(s_statusCmd[i],"\t");
	return true;
    }
    if (partWord.startsWith(prefix())) {
	for (ObjList* o = m_devices.skipNull(); o; o = o->skipNext()) {
	    TdmDevice* dev = static_cast<TdmDevice*>(o->get());
	    if (!partWord || dev->tdmName().startsWith(partWord))
		msg.retValue().append(dev->tdmName(),"\t");
	}
	return true;
    }
    return ok;
}

}; // anonymous namespace

#endif /* _WINDOWS */

/* vi: set ts=8 sw=4 sts=4 noet: */
