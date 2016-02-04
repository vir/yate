/**
 * tdmcard.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * TDM cards signalling and data driver
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
#include <wanpipe.h>


using namespace TelEngine;
namespace {


class TdmWorker;                         // The worker
class TdmThread;                         // The thread
class TdmDevice;                         // Connect a socket at the interface
class TdmInterface;                      // D-channel signalling interface
class TdmSpan;                           // Signalling span used to create voice circuits
class TdmCircuit;                        // A voice circuit
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
    ~TdmDevice();
    // Create a socket and bind him to the interface
    bool makeConnection();
    // process data
    int receiveData(u_int8_t *buf, unsigned int len);
    bool checkEvents();
    void close();
    int sendData(const DataBlock& buffer);
    int buildSpanChanFromIf(const char *interface_name, int *span, int *chan);
    bool setFormat(Format format);
    // send comandas
    bool setEvent(SignallingCircuitEvent::Type event, NamedList* params);
    inline SignallingComponent* owner() const
	{ return m_owner; }
    inline bool valid() const
	{ return m_sock != Socket::invalidHandle(); }
    inline int channel()
	{ return m_chan; }
    inline void setInterfaceName(const char* name)
	{ m_ifName = name; }
    inline int span()
	{ return m_span; }
    inline const String& tdmName() const
	{ return m_ifName; }
private:
    Mutex m_mutex;
    int m_sock;
    String m_ifName;
    wanpipe_api_t m_WPApi;
    Type m_type;
    int m_span;
    int m_chan;
    SignallingComponent* m_owner;        // Signalling component owning this device
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
	{ cleanup(true); SignallingInterface::destruct(); }
    // Get this object or an object from the base class
    virtual void* getObject(const String& name) const;
    // Send signalling packet
    virtual bool transmitPacket(const DataBlock& packet, bool repeat, PacketType type);
    // Interface control. Open device and start worker when enabled, cleanup when disabled
    virtual bool control(Operation oper, NamedList* params = 0);
    // Process incoming data
    virtual bool process();
    // Called by the factory to create TDM interfaces or spans
    static SignallingComponent* create(const String& type, NamedList& name);
private:
    inline void cleanup(bool release) {
	control(Disable,0);
	attach(0);
    }

    TdmDevice m_device;                  // The device
    Thread::Priority m_priority;         // Worker thread priority
    DataBlock m_buffer;                  // Read buffer
    bool m_readOnly;                     // Read only interface
    bool m_sendReadOnly;                 // Print send attempt on readonly interface error
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
    virtual void destroyed()
	{ cleanup(true); SignallingCircuit::destroyed(); }
    // Change circuit status. Clear events on status change
    // New status is Connect: Open device. Create source/consumer. Start worker
    // Cleanup on disconnect
    virtual bool status(Status newStat, bool sync = false);
    // Update data format for Tdm device and source/consumer
    virtual bool updateFormat(const char* format, int direction);
    // Get this circuit or source/consumer
    virtual void* getObject(const String& name) const;
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
    bool m_canSend;                      // Not a read only circuit
    Thread::Priority m_priority;         // Worker thread priority
    RefPointer<TdmSource> m_source;      // The data source
    TdmConsumer* m_consumer;             // The data consumer
    DataBlock m_sourceBuffer;            // Data source buffer
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

static const char* s_deviceName = "TDMDevice";

TdmDevice::TdmDevice(unsigned int chan, bool disableDbg)
    : m_mutex(true,s_deviceName),m_sock(Socket::invalidHandle()),
    m_type(chan ? TypeUnknown : Control),
    m_chan(chan),
    m_owner(0)
{
    DDebug(&plugin,DebugInfo,"TdmDevice(TdmQuery) type=%s chan=%u [%p]",
	lookup(m_type,s_types),chan,this);
    m_owner = new SignallingCircuitGroup(0,0,"TdmQuery");
    if (disableDbg)
	m_owner->debugEnabled(false);
}

TdmDevice::TdmDevice(Type t, SignallingComponent* dbg, unsigned int chan,
	unsigned int circuit)
    : m_mutex(true,s_deviceName), m_sock(Socket::invalidHandle()),
    m_type(t),
    m_span(-1),
    m_chan(chan),
    m_owner(dbg)
{
    DDebug(&plugin,DebugInfo,"TdmDevice type=%s chan=%u owner=%s cic=%u [%p]",
	lookup(t,s_types),chan,dbg ? dbg->debugName() : "",circuit,this);
    if (m_type == Control || m_type == TypeUnknown) {
	m_owner = 0;
	return;
    }
    m_down = false;
    plugin.append(this);
}

void TdmDevice::close()
{
    Lock myLock(m_mutex);
    m_span = -1;
    if (!valid())
	return;
    if (::close(m_sock) == 0)
	m_sock = Socket::invalidHandle();
    else
	Debug(&plugin,DebugWarn,"Failed to close TDM device %d: '%s'",errno,strerror(errno));

    if (m_type != Control && m_type != TypeUnknown)
	plugin.openClose(false);
}

TdmDevice::~TdmDevice()
{
    if (m_type == Control || m_type == TypeUnknown)
	TelEngine::destruct(m_owner);
    close();
    plugin.remove(this);
}

int TdmDevice::buildSpanChanFromIf(const char *interface_name, int *span, int *chan)
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

bool TdmDevice::makeConnection()
{
    Lock myLock(m_mutex);
    buildSpanChanFromIf(tdmName(),&m_span,&m_chan);
    if (m_span <= 0 || m_chan <= 0) {
	Debug(m_owner,DebugNote,"Unable to establish connection to span %d chan %d",m_span,m_chan);
	return false;
    }
    char fname[50];
    sprintf(fname,"/dev/wanpipe%d_if%d",m_span,m_chan);
    m_sock = ::open(fname, O_RDWR);
    if (!valid()) {
	DDebug(m_owner,DebugNote,"Cannot open span=%d chan=%d sock=%d",m_span,m_chan,m_sock);
	return false;
    }
    DDebug(m_owner,DebugNote,"Connection made on interface = %s",tdmName().c_str());
    return true;
}


int TdmDevice::receiveData(u_int8_t* buff, unsigned int buflen)
{
    if (!valid())
	return -1;

    Lock myLock(m_mutex);
    fd_set read;
    FD_ZERO(&read);
    FD_SET(m_sock,&read);
    struct timeval tVal;
    tVal.tv_sec = 0;
    tVal.tv_usec = 1000;
    if (::select(m_sock+1, &read, NULL, NULL, &tVal) <= 0 || !FD_ISSET(m_sock,&read))
	return 0;

    wan_msghdr_t msg;
    wan_iovec_t iov[2];
    memset(&msg,0,sizeof(msg));
    memset(&iov[0],0,sizeof(iov[0])*2);

    wp_api_hdr_t header;
    int hdrlen = sizeof(wp_api_hdr_t);
    memset(&header,0,hdrlen);

    iov[0].iov_len = hdrlen;
    iov[0].iov_base = &header;

    iov[1].iov_len = buflen;
    iov[1].iov_base = buff;

    msg.msg_iovlen = 2;
    msg.msg_iov = iov;

    int buflength = ::read(m_sock,&msg,sizeof(msg));

    buflength -= hdrlen;
    if (buflength <= 0)
	return 0;

#ifdef XDEBUG
    String tmp;
    tmp.hexify(buff,buflength,' ');
    Debug(m_owner,DebugInfo,"Read data on interface %s %d data=%s",tdmName().c_str(),buflength,tmp.c_str());
#endif
    return buflength;
}

bool TdmDevice::checkEvents()
{
    if (!valid())
	return -1;
    Lock myLock(m_mutex);
#ifdef WP_API_FEATURE_EVENTS

    wp_api_event_t* event;

    m_WPApi.wp_cmd.cmd = WP_API_CMD_READ_EVENT;
    if (ioctl(m_sock,WANPIPE_IOCTL_API_CMD,&m_WPApi.wp_cmd))
	return false;

    event = &m_WPApi.wp_cmd.event;

    switch (event->wp_api_event_type) {
	case WP_API_EVENT_ALARM:
	    if (!event->wp_api_event_alarm) {
		DDebug(m_owner,DebugWarn,"%s: Link is disconnected",tdmName().c_str());
		sendModuleUpdate("interfaceDown",tdmName(),m_down,SignallingInterface::LinkDown);
	    }
	    else {
		DDebug(m_owner,DebugInfo,"%s: Link is connected",tdmName().c_str());
		sendModuleUpdate("interfaceUp",tdmName(),m_down,SignallingInterface::LinkUp);
	    }
	    break;
	default :
	    DDebug(m_owner,DebugNote,"%s: Unknown OOB event",tdmName().c_str());
	    break;
    }
#endif
    return true;
}

int TdmDevice::sendData(const DataBlock& data)
{
    if (!valid())
	return -1;
    Lock myLock(m_mutex);
    fd_set write;
    struct timeval tVal;

    FD_ZERO(&write);
    FD_SET(m_sock, &write);
    tVal.tv_sec = 0;
    tVal.tv_usec = 1000;

    if (::select((m_sock+1),NULL, &write, NULL, &tVal) <= 0 || !FD_ISSET(m_sock,&write))
	return 0;

    int bsent = -1;
    int hdrlen = sizeof(wp_api_hdr_t);
    wp_api_hdr_t header;
    memset(&header,0,hdrlen);
    header.data_length = data.length();

    wan_msghdr_t msg;
    wan_iovec_t iov[2];

    memset(&msg,0,sizeof(msg));
    memset(&iov[0],0,sizeof(iov[0])*2);

    iov[0].iov_len=hdrlen;
    iov[0].iov_base=&header;

    iov[1].iov_len=data.length();
    iov[1].iov_base=data.data();

    msg.msg_iovlen=2;
    msg.msg_iov=iov;
    bsent = ::write(m_sock,&msg,data.length()+hdrlen);

    if (bsent > 0 && bsent > hdrlen) {
	bsent -= hdrlen;
    } else
	Debug(m_owner,DebugWarn,"Failed to transmit data, device '%s', error %s ",tdmName().c_str(),::strerror(errno));

    return bsent;
}

bool TdmDevice::setFormat(Format format)
{
    Lock myLock(m_mutex);
    m_WPApi.wp_cmd.chan = 0;
    m_WPApi.wp_cmd.result = SANG_STATUS_GENERAL_ERROR;
    m_WPApi.wp_cmd.cmd = WP_API_CMD_SET_CODEC;
    m_WPApi.wp_cmd.tdm_codec = format;
    if (ioctl(m_sock,WANPIPE_IOCTL_API_CMD,&m_WPApi.wp_cmd)) {
	Debug(m_owner,DebugNote,"Failed to set codec on device '%s', error '%s'.",
	      tdmName().c_str(),::strerror(errno));
	return false;
    }
    return true;
}

bool TdmDevice::setEvent(SignallingCircuitEvent::Type event, NamedList* params)
{
    Debug(m_owner,DebugInfo,"Events not supported!");
    return false;
}

bool TdmSpan::init(TdmDevice::Type type,
	const NamedList& config, const NamedList& defaults, const NamedList& params)
{
    TempObjectCounter cnt(plugin.objectsCounter());
    String voice = params.getValue("voicechans",config.getValue("voicechans"));
    unsigned int chans = 0;
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
	DDebug(m_group,DebugInfo,
	    "TdmSpan('%s'). Creating circuit code=%u channel=%u [%p]",
	    id().safe(),code,channel,this);
	TdmCircuit* cic = new TdmCircuit(type,code,channel,this,config,defaults,params);
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
    m_device(type,span->group(),channel,code),m_format(TdmDevice::WP_SLINEAR),
    m_canSend(true), m_priority(Thread::Normal), m_consumer(0)
{
    int sp = params.getIntValue("span",config.getIntValue("span",1));
    String name;
    name << "s" << sp << "c" << channel;
    m_device.setInterfaceName(name);
    m_device.makeConnection();
    int buflen = defaults.getIntValue("buflen",320);
    buflen = (unsigned int)params.getIntValue("buflen",config.getIntValue("buflen",buflen));
    m_sourceBuffer.assign(0,buflen);
    String priority = defaults.getValue("priority","100");
    m_priority = Thread::priority(params.getValue("priority",config.getValue("priority",priority)));
    m_format = TdmDevice::WP_SLINEAR;

    if (group() && group()->debugAt(DebugAll)) {
	String s;
	s << " driver=" << plugin.debugName();
	s << " type=" << lookup(type,s_types);
	s << " channel=" << channel;
	s << " cic=" << code;
	s << " buflen=" << buflen;
	s << " readonly=" << String::boolText(!m_canSend);
	s << " priority=" << Thread::priority(m_priority);
	DDebug(group(),DebugInfo,"TdmCircuit %s [%p]",s.c_str(),this);
    }
}

bool TdmCircuit::process()
{
    if (!(m_device.valid() && SignallingCircuit::status() == Connected && m_source))
	return false;

    int r = m_device.receiveData((u_int8_t*)m_sourceBuffer.data(),m_sourceBuffer.length());
    if (m_source && r > 0) {
	DataBlock data(m_sourceBuffer.data(),r);
	m_source->Forward(data);
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
    TempObjectCounter cnt(plugin.objectsCounter());
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
		DDebug(group(),DebugMild,"TdmCircuit(%u). Changed status to '%s' [%p]",
		    code(),lookupStatus(newStat),this);
	    if (newStat == Connected)
		break;
	    if (oldStat == Connected)
		cleanup(true,newStat,true);
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
    const char* format = "slin";
    m_source = new TdmSource(this,format);
    DDebug(DebugInfo,"Voice interface '%s'",m_device.tdmName().c_str());
    if (m_source)
	m_source->deref();
    if (m_canSend)
	m_consumer = new TdmConsumer(this,format);
    m_device.setFormat(TdmDevice::WP_SLINEAR);
}

void TdmCircuit::cleanup(bool release, Status stat, bool stop)
{
    if (stop || release)
	TdmWorker::stop();
    // Don't deref the source - the thread will do it
    m_source = 0;
    DDebug(group(),DebugNote,"Cleanup release=%s circuit %d on interface: %s",
	String::boolText(release),m_device.channel(),m_device.tdmName().c_str());
    TelEngine::destruct(m_consumer);
    if (release) {
	SignallingCircuit::destroyed();
	return;
    }
    status(stat);
    m_sourceBuffer.clear(false);
}

bool TdmCircuit::updateFormat(const char* format, int direction)
{
    if (!(format && *format))
	return false;
    TempObjectCounter cnt(plugin.objectsCounter());
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
    return m_device.setEvent(type,params);
}

void TdmCircuit::consume(const DataBlock& data)
{
    if (!(SignallingCircuit::status() == Connected && m_canSend && data.length()))
	return;
    int w = m_device.sendData(data);
    if (w <= 0)
	DDebug(group(),DebugInfo,"Failed to send circuit data!");
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

// Interface
SignallingComponent* TdmInterface::create(const String& type, NamedList& name)
{
    bool circuit = true;
    if (type == "SignallingInterface")
	circuit = false;
    else if (type == "SignallingCircuitSpan")
	;
    else
	return 0;

    TempObjectCounter cnt(plugin.objectsCounter());
    // Check in params if the module witch should create the component is specifyed
    // if the module is specifyed and is not tdmcard let the specifyed module to create the component
    const String* module = name.getParam("module");
    if (module && *module != "tdmcard")
	return 0;
    Configuration cfg(Engine::configFile("tdmcard"));
    const char* sectName = name.getValue((circuit ? "voice" : "sig"),name.getValue("basename",name));
    NamedList* config = cfg.getSection(sectName);

    if (!name.getBoolValue(YSTRING("local-config"),false))
	config = &name;
    else if (!config) {
	DDebug(&plugin,DebugConf,"No section '%s' in configuration",c_safe(sectName));
	return 0;
    } else
	name.copyParams(*config);

#ifdef DEBUG
    if (plugin.debugAt(DebugAll)) {
	String tmp;
	config->dump(tmp,"\r\n  ",'\'',true);
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
	return 0;
    }
    TdmInterface* iface = new TdmInterface(name);
    if (iface->init(devType,code,code,*config,*general,name))
	return iface;
    TelEngine::destruct(iface);
    return 0;
}


TdmInterface::TdmInterface(const NamedList& params)
    : SignallingComponent(params,&params,"tdm"),
      m_device(TdmDevice::DChan,this,0,0), m_priority(Thread::Normal),
      m_buffer(0,320), m_readOnly(false), m_sendReadOnly(false)
{

}

TdmInterface::~TdmInterface()
{
    cleanup(false);
}

void* TdmInterface::getObject(const String& name) const
{
    if (name == "TdmInterface")
	return (void*)this;
    return SignallingInterface::getObject(name);
}


bool TdmInterface::process()
{
    m_device.checkEvents();
    int r = m_device.receiveData((u_int8_t*)m_buffer.data(),m_buffer.length());
    if (r <= 0)
	return false;
    DataBlock packet(m_buffer.data(),r,false);
    if (packet.length()) {
	receivedPacket(packet);
    }
    packet.clear(false);
    return true;
}


bool TdmInterface::init(TdmDevice::Type type, unsigned int code, unsigned int channel,
	const NamedList& config, const NamedList& defaults, const NamedList& params)
{
    TempObjectCounter cnt(plugin.objectsCounter());
    m_readOnly = getBoolValue("readonly",params,config,defaults);
    String priority = defaults.getValue("priority");
    m_priority = Thread::priority(params.getValue("priority",config.getValue("priority",priority)));
    int sp = params.getIntValue("spam",config.getIntValue("span",1));
    (m_ifname = "") << "s" << sp << "c" << channel;
    if (debugAt(DebugInfo)) {
	String s;
	s << " driver=" << plugin.debugName();
	s << " section=" << config.c_str();
	s << " type=" << lookup(type,s_types);
	s << " channel=" << channel;
	s << " readonly=" << String::boolText(m_readOnly);
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
		return TelEngine::controlReturn(params,true);
	    m_readOnly = (oper == DisableTx);
	    m_sendReadOnly = false;
	    Debug(this,DebugInfo,"Tx is %sabled [%p]",m_readOnly?"dis":"en",this);
	    return TelEngine::controlReturn(params,true);
	case Query:
	    return TelEngine::controlReturn(params,valid());
	default:
	    return SignallingInterface::control(oper,params);
    }
    if (oper == Enable) {
	if (valid())
	    return TelEngine::controlReturn(params,true);
	m_device.setInterfaceName(m_ifname);
	bool ok = m_device.valid()|| m_device.makeConnection();
	if (ok) {
	    ok = TdmWorker::start(m_priority,this,debugName());
	}
	if (ok) {
	    Debug(this,DebugAll,"Enabled [%p]",this);
	}
	else {
	    Debug(this,DebugWarn,"Enable failed [%p]",this);
	    control(Disable,0);
	}
	return TelEngine::controlReturn(params,ok);
    }
    // oper is Disable
    bool ok = valid();
    TdmWorker::stop();
    m_device.close();
    if (ok)
	Debug(this,DebugAll,"Disabled [%p]",this);
    return TelEngine::controlReturn(params,true);
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
    int len = m_device.sendData(packet);
    if (len != (int)packet.length()) {
	Debug(this,DebugNote,"Transmit packet failed sent %d from %d",len,packet.length());
	return false;
    }
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
    DDebug(&plugin,DebugAll,"TdmSource::TdmSource() cic=%s  %s [%p]",m_address.c_str(),format,this);
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
    DDebug(&plugin,DebugAll,"TdmConsumer::TdmConsumer() cic=%s %s [%p]",m_address.c_str(),format,this);
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
