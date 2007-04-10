/**
 * ysigchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Signalling Channel
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

#include <string.h>

using namespace TelEngine;
namespace { // anonymous

class SigChannel;                        // Signalling channel
class SigDriver;                         // Signalling driver
class SigParams;                         // Named list containing creator data (pointers)
                                         // Used to pass parameters to objects that need to obtain some pointers
class SigCircuitGroup;                   // Used to create a signalling circuit group descendant to set the debug name
class SigLink;                           // Keep a signalling link
class SigIsdn;                           // ISDN (Q.931 over HDLC interface) call control
class SigIsdnMonitor;                    // ISDN (Q.931 over HDLC interface) call control monitor
class SigConsumerMux;                    // Consumer used to push data to SigSourceMux
class SigSourceMux;                      // A data source multiplexer with 2 channels
class SigIsdnCallRecord;                 // Record an ISDN call monitor
class SigLinkThread;                     // Get events and check timeout for links that have a call controller

// The signalling channel
class SigChannel : public Channel
{
public:
    // Incoming
    SigChannel(SignallingEvent* event);
    // Outgoing
    SigChannel(Message& msg, String& caller, String& called, SigLink* link);
    virtual ~SigChannel();
    inline SignallingCall* call() const
        { return m_call; }
    // Overloaded methods
    virtual bool msgProgress(Message& msg);
    virtual bool msgRinging(Message& msg);
    virtual bool msgAnswered(Message& msg);
    virtual bool msgTone(Message& msg, const char* tone);
    virtual bool msgText(Message& msg, const char* text);
    virtual bool msgDrop(Message& msg, const char* reason);
    virtual bool msgTransfer(Message& msg);
    virtual bool callPrerouted(Message& msg, bool handled);
    virtual bool callRouted(Message& msg);
    virtual void callAccept(Message& msg);
    virtual void callRejected(const char* error, const char* reason = 0,
	const Message* msg = 0);
    virtual void disconnected(bool final, const char* reason);
    void handleEvent(SignallingEvent* event);
    bool route(SignallingEvent* event);
    void hangup(const char* reason = 0, bool reject = true);
private:
    virtual void statusParams(String& str);
    // Event handlers
    void evInfo(SignallingEvent* event);
    void evProgress(SignallingEvent* event);
    void evRelease(SignallingEvent* event);
    void evAccept(SignallingEvent* event);
    void evAnswer(SignallingEvent* event);
    void evRinging(SignallingEvent* event);
    // Open or update format source/consumer
    // Set force to true to change source/consumer pointer/format
    bool updateConsumer(const char* format, bool force);
    bool updateSource(const char* format, bool force);
    // Get the circuit reserved for the call
    inline SignallingCircuit* getCircuit() {
	    SignallingCircuit* cic = 0;
	    if (m_call)
		cic = static_cast<SignallingCircuit*>(m_call->getObject("SignallingCircuit"));
	    return cic;
	}
private:
    Mutex m_callMutex;                   // Call operation lock
    SignallingCall* m_call;              // The signalling call this channel is using
    bool m_hungup;                       // Hang up flag
    String m_reason;                     // Hangup reason
    bool m_inband;                       // True to try to send in-band tones
};

class SigDriver : public Driver
{
    friend class SigLink;                // Needded for appendLink() / removeLink()
    friend class SigLinkThread;          // Needded for clearLink()
public:
    SigDriver();
    ~SigDriver();
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest);
    inline SignallingEngine* engine() const
	{ return m_engine; }
    void handleEvent(SignallingEvent* event);
    bool received(Message& msg, int id);
    // Find a link by name
    // If callCtrl is true, match only link with call controller
    SigLink* findLink(const char* name, bool callCtrl);
    // Find a link by call controller
    SigLink* findLink(const SignallingCallControl* ctrl);
private:
    // Delete the given link if found
    // Clear link list if name is 0
    // Clear all stacks without waiting for call termination if name is 0
    void clearLink(const char* name = 0, bool waitCallEnd = false, unsigned int howLong = 0);
    // Append a link to the list. Duplicate names are not allowed
    bool appendLink(SigLink* link);
    // Remove a link from list without deleting it
    void removeLink(SigLink* link);

    SignallingEngine* m_engine;          // The signalling engine
    ObjList m_links;                     // Link list
    Mutex m_linksMutex;                  // Link list operations
};

// Named list containing creator data (pointers)
// Used to pass parameters to objects that need to obtain some pointers
class SigParams : public NamedList
{
public:
    inline SigParams(const char* name, SignallingCircuitGroup* group)
	: NamedList(name), m_cicGroup(group)
	{}
    virtual void* getObject(const String& name) const;
private:
    SignallingCircuitGroup* m_cicGroup;
};

// Used to create a signalling circuit group descendant to set the debug name
class SigCircuitGroup : public SignallingCircuitGroup
{
public:
    inline SigCircuitGroup(const char* name, unsigned int base = 0, int strategy = Increment)
	: SignallingCircuitGroup(base,strategy,name)
	{}
    virtual ~SigCircuitGroup()
	{}
protected:
    virtual void timerTick(const Time& when)
	{}
};

// Signalling link
class SigLink : public RefObject
{
    friend class SigLinkThread;         // The thread must set m_thread to 0 on terminate
public:
    enum Type {
	IsdnPriNet,
	IsdnPriCpe,
	IsdnPriMon,
	Unknown
    };
    // Set link name and type. Append to plugin list
    SigLink(const char* name, Type type);
    // Cancel thread. Cleanup. Remove from plugin list
    virtual ~SigLink();
    inline int type() const
	{ return m_type; }
    inline SignallingCallControl* controller() const
	{ return m_controller; }
    inline const String& name() const
	{ return m_name; }
    inline bool inband() const
	{ return m_inband; }
    // Set exiting flag for call controller and timeout for the thread
    void setExiting(unsigned int msec);
    // Initialize the link. Return false on failure
    inline bool initialize(NamedList& params) {
	    if (m_init)
		return reload(params);
	    m_init = true;
	    return create(params);
	}
    // Handle events received from call controller
    // Default action: calls the driver's handleEvent()
    virtual void handleEvent(SignallingEvent* event);
    // Cancel thread if any. Call release
    void cleanup();
    // Type names
    static TokenDict s_type[];
protected:
    // Create/Reload/Release data
    virtual bool create(NamedList& params) { return false; }
    virtual bool reload(NamedList& params) { return false; }
    virtual void release() {}
    // Start worker thread
    bool startThread();
    // Build the signalling interface and insert it in the engine
    static SignallingInterface* buildInterface(const String& device,
	const String& debugName, String& error);
    // Build a signalling circuit group and insert it in the engine
    static SigCircuitGroup* buildCircuits(const String& device,
	const String& debugName, String& error);
    SignallingCallControl* m_controller; // Call controller, if any
    bool m_init;                         // True if already initialized
    bool m_inband;                       // True to send in-band tones through this link
private:
    int m_type;                          // Link type
    String m_name;                       // Link name
    SigLinkThread* m_thread;             // Event thread for call controller
};

// Q.931 call control over HDLC interface
class SigIsdn : public SigLink
{
public:
    inline SigIsdn(const char* name, bool net)
	: SigLink(name,net ? IsdnPriNet : IsdnPriCpe),
	m_q921(0), m_iface(0), m_group(0)
	{}
    virtual ~SigIsdn()
	{ release(); }
protected:
    virtual bool create(NamedList& params);
    virtual bool reload(NamedList& params);
    virtual void release();
    inline ISDNQ931* q931()
	{ return static_cast<ISDNQ931*>(m_controller); }
    // Build component debug name
    inline void buildName(String& dest, const char* name)
	{ dest = ""; dest << this->name() << '/' << name; }
private:
    ISDNQ921* m_q921;
    SignallingInterface* m_iface;
    SigCircuitGroup* m_group;
};

// Q.931 call control monitor over HDLC interface
class SigIsdnMonitor : public SigLink
{
    friend class SigIsdnCallRecord;
public:
    SigIsdnMonitor(const char* name);
    virtual ~SigIsdnMonitor();
    virtual void handleEvent(SignallingEvent* event);
    unsigned int chanBuffer() const
	{ return m_chanBuffer; }
    unsigned char idleValue() const
	{ return m_idleValue; }
    const String& peerId(bool network) const
	{ return network ? m_netId : m_cpeId; }
    // Remove a call and it's call monitor
    void removeCall(SigIsdnCallRecord* call);
protected:
    virtual bool create(NamedList& params);
    virtual bool reload(NamedList& params);
    virtual void release();
    inline ISDNQ931Monitor* q931()
	{ return static_cast<ISDNQ931Monitor*>(m_controller); }
    // Build component debug name
    inline void buildName(String& dest, const char* name, bool net)
	{ dest = ""; dest << (net ? m_netId : m_cpeId) << '/' << name; }
private:
    Mutex m_monitorMutex;                // Lock monitor list operations
    ObjList m_monitors;                  // Monitor list
    unsigned int m_id;                   // ID generator
    unsigned int m_chanBuffer;           // The buffer length of one channel of a data source multiplexer
    unsigned char m_idleValue;           // Idle value for source multiplexer to fill when no data
    String m_netId;                      // The id of the network side of the data link
    String m_cpeId;                      // The id of the user side of the data link
    // Components
    ISDNQ921Pasive* m_q921Net;
    ISDNQ921Pasive* m_q921Cpe;
    SignallingInterface* m_ifaceNet;
    SignallingInterface* m_ifaceCpe;
    SigCircuitGroup* m_groupNet;
    SigCircuitGroup* m_groupCpe;
};

// Consumer used to push data to SigSourceMux
class SigConsumerMux : public DataConsumer
{
    friend class SigSourceMux;
public:
    virtual ~SigConsumerMux()
	{}
    virtual void Consume(const DataBlock& data, unsigned long tStamp);
protected:
    inline SigConsumerMux(SigSourceMux* owner, bool first, const char* format)
	: DataConsumer(format), m_owner(owner), m_first(first)
	{}
private:
    SigSourceMux* m_owner;               // The owner of this consumer
    bool m_first;                        // The channel allocated by the owner
};

// A data source multiplexer with 2 channels
class SigSourceMux : public DataSource
{
public:
    // Create consumers
    // @param idleValue Value to fill missing data when forwarded
    // @param chanBuffer The length of a channel buffer (will be rounded up to a multiple of sample length)
    SigSourceMux(const char* format, unsigned char idleValue, unsigned int chanBuffer);
    virtual ~SigSourceMux();
    inline unsigned int sampleLen() const
	{ return m_sampleLen; }
    bool hasSource(bool first)
	{ return first ? (m_firstSrc != 0) : (m_secondSrc != 0); }
    // Replace the consumer of the given source. Remove current consumer's source before
    // @param first True to replace with the first channel, false for the second
    // Return false if source is 0 or has invalid format (other then ours)
    bool attach(bool first, DataSource* source);
    // Multiplex received data from consumers and forward it
    void consume(bool first, const DataBlock& data, unsigned long tStamp);
    // Remove the source for the appropriate consumer
    void removeSource(bool first);
protected:
    // Forward the buffer if at least one channel is filled. Reset data
    // If one channel is empty or incomplete, fill it with idle value
    void forwardBuffer();
    // Fill (interlaced samples) buffer with samples of received data
    // If no data, fill the free space with idle value
    void fillBuffer(bool first, unsigned char* data = 0, unsigned int samples = 0);
    inline bool firstFull() const
	{ return m_samplesFirst == m_maxSamples; }
    inline bool secondFull() const
	{ return m_samplesSecond == m_maxSamples; }
private:
    Mutex m_lock;                        // Lock consumers changes and data processing
    DataSource* m_firstSrc;              // First consumer's source
    DataSource* m_secondSrc;             // Second consumer's source
    SigConsumerMux* m_firstChan;         // First channel
    SigConsumerMux* m_secondChan;        // Second channel
    unsigned char m_idleValue;           // Filling value for missing data
    unsigned int m_sampleLen;            // The format sample length
    unsigned int m_maxSamples;           // Maximum samples in a channel buffer
    unsigned int m_samplesFirst;         // The number of samples in first channel's buffer
    unsigned int m_samplesSecond;        // The number of samples in second channel's buffer
    DataBlock m_buffer;                  // Multiplex buffer
    unsigned int m_error;                // Flag to show data length violation error
};

// Record an ISDN call monitor
class SigIsdnCallRecord : public CallEndpoint
{
public:
    SigIsdnCallRecord(SigIsdnMonitor* monitor, const char* id, SignallingEvent* event);
    virtual ~SigIsdnCallRecord();
    bool update(SignallingEvent* event);
    bool close(const char* reason);
    bool disconnect(const char* reason);
    // Process Info events. Send chan.dtmf
    void evInfo(SignallingEvent* event);
protected:
    virtual void disconnected(bool final, const char *reason);
    // Create a message to be enqueued/dispatched to the engine
    // @param peers True to caller and called parameters
    // @param userdata True to add this call endpoint as user data
    Message* message(const char* name, bool peers = true, bool userdata = false);
    // Send call.route and call.execute (if call.route succeedded)
    bool callRouteAndExec(const char* format);
private:
    Mutex m_lock;
    String m_caller;                     // The caller
    String m_called;                     // The called
    bool m_netInit;                      // The caller is from the network (true) or user (false) side of the link
    String m_reason;                     // Termination reason
    String m_status;                     // Call status
    SigIsdnMonitor* m_monitor;           // The owner of this recorder
    ISDNQ931CallMonitor* m_call;         // The call monitor
};

// Get events from call controller. Check timeouts
class SigLinkThread : public Thread
{
    friend class SigLink;                // SigLink will set m_timeout when needded
public:
    inline SigLinkThread(SigLink* link)
	: Thread("SigLink thread"), m_link(link), m_timeout(0)
	{}
    virtual ~SigLinkThread() {
	    if (m_link)
		m_link->m_thread = 0;
	}
    virtual void run();
private:
    SigLink* m_link;
    u_int64_t m_timeout;
};

static SigDriver plugin;
static Configuration s_cfg;

/**
 * SigChannel
 */
// Construct an incoming channel
SigChannel::SigChannel(SignallingEvent* event)
    : Channel(&plugin,0,false),
    m_callMutex(true),
    m_call(0),
    m_hungup(false),
    m_inband(false)
{
    Message* m = message("chan.startup");
    SignallingMessage* msg = event->message();
    m_call = event->call();
    if (!(msg && m_call && m_call->ref())) {
	Debug(this,DebugCall,
	    "Incoming. Invalid initiating event. No call or no message [%p]",this);
	Engine::enqueue(m);
	return;
    }
    Debug(this,DebugCall,
	"Incoming. Caller: '%s'. Called: '%s'. [%p]",
	msg->params().getValue("caller"),msg->params().getValue("called"),this);
    m_call->userdata(this);
    SigLink* link = plugin.findLink(m_call->controller());
    if (link)
	m_inband = link->inband();
    // Startup
    m->setParam("direction",status());
    m->setParam("caller",msg->params().getValue("caller"));
    m->setParam("called",msg->params().getValue("called"));
    m->setParam("callername",msg->params().getValue("callername"));
    // TODO: Add call control parameter ?
    Engine::enqueue(m);
}

// Construct an outgoing channel
SigChannel::SigChannel(Message& msg, String& caller, String& called, SigLink* link)
    : Channel(&plugin,0,true),
    m_callMutex(true),
    m_call(0),
    m_hungup(false),
    m_inband(false)
{
    if (!link)
	return;
    Debug(this,DebugCall,"Outgoing. Caller: '%s'. Called: '%s' [%p]",
	caller.c_str(),called.c_str(),this);
    // Data
    m_inband = link->inband();
    // Notify engine
    Message* m = message("chan.startup",msg);
    m->setParam("direction",status());
    m_targetid = msg.getValue("id");
    m->setParam("caller",msg.getValue("caller"));
    m->setParam("called",msg.getValue("called"));
    m->setParam("billid",msg.getValue("billid"));
    // TODO: Add call control parameter ?
    Engine::enqueue(m);
    if (!link->controller()) {
	msg.setParam("error","noroute");
	return;
    }
    // Make the call
    SignallingMessage* sigMsg = new SignallingMessage;
    sigMsg->params().addParam("caller",caller);
    sigMsg->params().addParam("called",called);
    sigMsg->params().addParam("callername",msg.getValue("callername"));
    sigMsg->params().copyParam(msg,"format");
    sigMsg->params().copyParam(msg,"callernumtype");
    sigMsg->params().copyParam(msg,"callernumplan");
    sigMsg->params().copyParam(msg,"callerpres");
    sigMsg->params().copyParam(msg,"callerscreening");
    sigMsg->params().copyParam(msg,"callednumtype");
    sigMsg->params().copyParam(msg,"callednumplan");
    m_call = link->controller()->call(sigMsg,m_reason);
    if (m_call)
	m_call->userdata(this);
    else
	msg.setParam("error",m_reason);
}

SigChannel::~SigChannel()
{
    hangup(0,false);
    status("destroyed");
    DDebug(this,DebugCall,"Destroyed with reason '%s' [%p]",m_reason.c_str(),this);
}

void SigChannel::handleEvent(SignallingEvent* event)
{
    if (!event)
	return;
    switch (event->type()) {
	case SignallingEvent::Info:      evInfo(event);     break;
	case SignallingEvent::Progress:  evProgress(event); break;
	case SignallingEvent::Accept:    evAccept(event);   break;
	case SignallingEvent::Answer:    evAnswer(event);   break;
	case SignallingEvent::Release:   evRelease(event);  break;
	case SignallingEvent::Ringing:   evRinging(event);  break;
	default:
	    DDebug(this,DebugStub,"No handler for event '%s' [%p]",
		event->name(),this);
    }
}

bool SigChannel::route(SignallingEvent* event)
{
    Message* m = message("call.preroute",false,true);
    SignallingMessage* msg = (event ? event->message() : 0);
    if (msg) {
	m->setParam("caller",msg->params().getValue("caller"));
	m->setParam("called",msg->params().getValue("called"));
	m->setParam("callername",msg->params().getValue("callername"));
	m->setParam("format",msg->params().getValue("format"));
	m->copyParam(msg->params(),"formats");
	m->copyParam(msg->params(),"callernumtype");
	m->copyParam(msg->params(),"callernumplan");
	m->copyParam(msg->params(),"callerpres");
	m->copyParam(msg->params(),"callerscreening");
	m->copyParam(msg->params(),"callednumtype");
	m->copyParam(msg->params(),"callednumplan");
    }
    // TODO: Add call control parameter ?
    return startRouter(m);
}

bool SigChannel::msgProgress(Message& msg)
{
    status("progressing");
    Lock lock(m_callMutex);
    DDebug(this,DebugCall,"msgProgress %s[%p]",(m_call ? "" : ". No call "),this);
    if (!m_call)
	return true;
    bool media = msg.getBoolValue("earlymedia",getPeer() && getPeer()->getSource());
    const char* format = msg.getValue("format");
    SignallingMessage* sm = 0;
    if (media && updateConsumer(format,false)) {
	sm = new SignallingMessage;
	sm->params().addParam("media",String::boolText(true));
	if (format)
	    sm->params().addParam("format",format);
    }
    SignallingEvent* event = new SignallingEvent(SignallingEvent::Progress,sm,m_call);
    if (sm)
	sm->deref();
    m_call->sendEvent(event);
    return true;
}

bool SigChannel::msgRinging(Message& msg)
{
    status("ringing");
    Lock lock(m_callMutex);
    DDebug(this,DebugCall,"msgRinging %s[%p]",(m_call ? "" : ". No call "),this);
    if (!m_call)
	return true;
    bool media = msg.getBoolValue("earlymedia",getPeer() && getPeer()->getSource());
    const char* format = msg.getValue("format");
    SignallingMessage* sm = 0;
    if (media && updateConsumer(format,false) && format) {
	sm = new SignallingMessage;
	sm->params().addParam("format",format);
    }
    SignallingEvent* event = new SignallingEvent(SignallingEvent::Ringing,sm,m_call);
    if (sm)
	sm->deref();
    m_call->sendEvent(event);
    return true;
}

bool SigChannel::msgAnswered(Message& msg)
{
    status("answered");
    Lock lock(m_callMutex);
    DDebug(this,DebugCall,"msgAnswered %s[%p]",(m_call ? "" : ". No call "),this);
    if (!m_call)
	return true;
    updateSource(0,false);
    const char* format = msg.getValue("format");
    SignallingMessage* sm = 0;
    if (updateConsumer(format,false) && format) {
	sm = new SignallingMessage;
	sm->params().addParam("format",format);
    }
    SignallingEvent* event = new SignallingEvent(SignallingEvent::Answer,sm,m_call);
    if (sm)
	sm->deref();
    m_call->sendEvent(event);
    return true;
}

bool SigChannel::msgTone(Message& msg, const char* tone)
{
    Lock lock(m_callMutex);
    DDebug(this,DebugCall,"msgTone. Tone: '%s' %s[%p]",
	tone,(m_call ? "" : ". No call "),this);
    if (m_inband && dtmfInband(tone))
	return true;
    // If we failed try to send as signalling anyway
    if (!m_call || !(tone && *tone))
	return true;
    SignallingMessage* sm = new SignallingMessage;
    sm->params().addParam("tone",tone);
    SignallingEvent* event = new SignallingEvent(SignallingEvent::Info,sm,m_call);
    sm->deref();
    m_call->sendEvent(event);
    return true;
}

bool SigChannel::msgText(Message& msg, const char* text)
{
    Lock lock(m_callMutex);
    DDebug(this,DebugCall,"msgText. Text: '%s' %s[%p]",
	text,(m_call ? "" : ". No call "),this);
    if (!m_call)
	return true;
    SignallingMessage* sm = new SignallingMessage;
    sm->params().addParam("text",text);
    SignallingEvent* event = new SignallingEvent(SignallingEvent::Message,sm,m_call);
    sm->deref();
    m_call->sendEvent(event);
    return true;
}

bool SigChannel::msgDrop(Message& msg, const char* reason)
{
    DDebug(this,DebugCall,"msgDrop. Reason: '%s' %s[%p]",
	reason,(m_call ? "" : ". No call "),this);
    hangup(reason,false);
    return true;
}

bool SigChannel::msgTransfer(Message& msg)
{
    Lock lock(m_callMutex);
    DDebug(this,DebugCall,"msgTransfer %s[%p]",(m_call ? "" : ". No call "),this);
    if (!m_call)
	return true;
    SignallingEvent* event = new SignallingEvent(SignallingEvent::Transfer,0,m_call);
    return m_call->sendEvent(event);
}

bool SigChannel::callPrerouted(Message& msg, bool handled)
{
    Lock lock(m_callMutex);
    if (!m_call) {
	Debug(this,DebugCall,"callPrerouted [%p]. No call. Abort",this);
	return false;
    }
    DDebug(this,DebugAll,"callPrerouted. [%p]",this);
    return true;
}

bool SigChannel::callRouted(Message& msg)
{
    Lock lock(m_callMutex);
    if (!m_call) {
	Debug(this,DebugCall,"callRouted [%p]. No call. Abort",this);
	return false;
    }
    DDebug(this,DebugAll,"callRouted. [%p]",this);
    return true;
}

void SigChannel::callAccept(Message& msg)
{
    Lock lock(m_callMutex);
    DDebug(this,DebugCall,"callAccept %s[%p]",(m_call ? "" : ". No call "),this);
    if (m_call) {
	const char* format = msg.getValue("format");
	updateConsumer(format,false);
	SignallingMessage* sm = 0;
	if (format) {
	    sm = new SignallingMessage;
	    sm->params().addParam("format",format);
	}
	SignallingEvent* event = new SignallingEvent(SignallingEvent::Accept,sm,m_call);
	if (sm)
	    sm->deref();
	m_call->sendEvent(event);
    }
    Channel::callAccept(msg);
}

void SigChannel::callRejected(const char* error, const char* reason, const Message* msg)
{
    DDebug(this,DebugCall,"callRejected. Error: '%s'. Reason: '%s' [%p]",
	error,reason,this);
    m_reason = error ? error : (reason ? reason : "unknown");
    hangup();
}

void SigChannel::disconnected(bool final, const char* reason)
{
    DDebug(this,DebugAll,"disconnected. Final: %s. Reason: '%s' [%p]",
	String::boolText(final),reason,this);
    Channel::disconnected(final,reason);
}

void SigChannel::hangup(const char* reason, bool reject)
{
    Lock lock(m_callMutex);
    if (m_hungup)
	return;
    setSource();
    setConsumer();
    m_hungup = true;
    if (reason)
	m_reason = reason;
    if (m_reason.null())
	m_reason = Engine::exiting() ? "net-out-of-order" : "normal";
    if (m_call) {
	m_call->userdata(0);
	SignallingMessage* msg = new SignallingMessage;
	msg->params().addParam("reason",m_reason);
	SignallingEvent* event = new SignallingEvent(SignallingEvent::Release,
	    msg,m_call);
	msg->deref();
	m_call->sendEvent(event);
	m_call->deref();
	m_call = 0;
    }
    lock.drop();
    Message* m = message("chan.hangup",true);
    m->setParam("status","hangup");
    m->setParam("reason",m_reason);
    Engine::enqueue(m);
    Debug(this,DebugCall,"Hung up. Reason: '%s' [%p]",m_reason.c_str(),this);
}

void SigChannel::statusParams(String& str)
{
    Channel::statusParams(str);
}

void SigChannel::evInfo(SignallingEvent* event)
{
    SignallingMessage* msg = event->message();
    if (!msg)
	return;
    // Check DTMF
    String tmp = msg->params().getValue("tone");
    if (!tmp.null()) {
	bool inband = msg->params().getBoolValue("inband");
	DDebug(this,DebugCall,"Event: '%s'. DTMF: '%s'. In band: %s [%p]",
	    event->name(),tmp.c_str(),String::boolText(inband),this);
	Message* m = message("chan.dtmf");
	m->addParam("text",tmp);
	Engine::enqueue(m);
    }
}

void SigChannel::evProgress(SignallingEvent* event)
{
    DDebug(this,DebugCall,"Event: '%s' [%p]",event->name(),this);
    status("progressing");
    Engine::enqueue(message("call.progress"));
}

void SigChannel::evRelease(SignallingEvent* event)
{
    if (event->message())
	m_reason = event->message()->params().getValue("reason");
    else
	m_reason = "";
    Debug(this,DebugCall,"Event: '%s'. Reason: '%s' [%p]",
	event->name(),m_reason.c_str(),this);
}

void SigChannel::evAccept(SignallingEvent* event)
{
    DDebug(this,DebugCall,"Event: '%s' [%p]",event->name(),this);
    const char* format = 0;
    bool cicChange = false;
    if (event->message()) {
	format = event->message()->params().getValue("format");
	cicChange = event->message()->params().getBoolValue("circuit-change",false);
    }
    updateSource(format,cicChange);
    updateConsumer(0,cicChange);
}

void SigChannel::evAnswer(SignallingEvent* event)
{
    DDebug(this,DebugCall,"Event: '%s' [%p]",event->name(),this);
    status("answered");
    const char* format = 0;
    bool cicChange = false;
    if (event->message()) {
	format = event->message()->params().getValue("format");
	cicChange = event->message()->params().getBoolValue("circuit-change",false);
    }
    updateSource(format,cicChange);
    updateConsumer(0,cicChange);
    Engine::enqueue(message("call.answered",false,true));
}

void SigChannel::evRinging(SignallingEvent* event)
{
    DDebug(this,DebugCall,"Event: '%s' [%p]",event->name(),this);
    status("ringing");
    const char* format = 0;
    bool cicChange = false;
    if (event->message()) {
	format = event->message()->params().getValue("format");
	cicChange = event->message()->params().getBoolValue("circuit-change",false);
    }
    updateSource(format,cicChange);
    Engine::enqueue(message("call.ringing",false,true));
}

bool SigChannel::updateConsumer(const char* format, bool force)
{
    DataConsumer* consumer = getConsumer();
    SignallingCircuit* cic = getCircuit();
    if (!cic)
	return false;
    if (consumer && !cic->updateFormat(format,-1) && !force)
	return true;
    // Set consumer
    setConsumer();
    setConsumer(static_cast<DataConsumer*>(cic->getObject("DataConsumer")));
    consumer = getConsumer();
    if (consumer) {
	DDebug(this,DebugAll,"Data consumer set to (%p): '%s' [%p]",
	    consumer,consumer->getFormat().c_str(),this);
	return true;
    }
    Debug(this,DebugNote,"Failed to set data consumer [%p]",this);
    return false;
}

bool SigChannel::updateSource(const char* format, bool force)
{
    DataSource* source = getSource();
    SignallingCircuit* cic = getCircuit();
    if (!cic)
	return false;
    if (source && !cic->updateFormat(format,1) && !force)
	return true;
    // Set source
    setSource();
    setSource(static_cast<DataSource*>(cic->getObject("DataSource")));
    source = getSource();
    if (source) {
	DDebug(this,DebugAll,"Data source set to (%p): '%s' [%p]",
	    source,source->getFormat().c_str(),this);
	return true;
    }
    Debug(this,DebugNote,"Failed to set data source [%p]",this);
    return false;
}

/**
 * SigDriver
 */
SigDriver::SigDriver()
    : Driver("sig","fixchans"),
    m_engine(0),
    m_linksMutex(true)
{
    Output("Loaded module Signalling Channel");
}

SigDriver::~SigDriver()
{
    Output("Unloading module Signalling Channel");
    clearLink();
    if (m_engine)
	delete m_engine;
}

bool SigDriver::msgExecute(Message& msg, String& dest)
{
    if (!msg.userData()) {
	Debug(this,DebugNote,"Signalling call failed. No data channel");
	msg.setParam("error","failure");
	return false;
    }
    // Get caller/called
    String caller = msg.getValue("caller");
    String called = dest;
    // Identify the call controller before create channel
    const char* tmp = msg.getValue("link");
    SigLink* link = findLink(tmp,true);
    if (!link) {
	Debug(this,DebugNote,
	    "Signalling call failed. No call controller named '%s'",tmp);
	msg.setParam("error","noroute");
	return false;
    }
    // Create channel
    DDebug(this,DebugAll,
	"msgExecute. Caller: '%s'. Called: '%s'. Call controller: '%s'",
	caller.c_str(),called.c_str(),link->name().c_str());
    bool ok = true;
    SigChannel* sigCh = new SigChannel(msg,caller,called,link);
    if (sigCh->call()) {
	Channel* ch = static_cast<Channel*>(msg.userData());
	if (ch && sigCh->connect(ch,msg.getValue("reason"))) {
	    msg.setParam("peerid",sigCh->id());
	    msg.setParam("targetid",sigCh->id());
        }
    }
    else {
	Debug(this,DebugNote,"Signalling call failed. No call");
	if (!msg.getValue("error"))
	    msg.setParam("error","failure");
	ok = false;
    }
    sigCh->deref();
    return ok;
}

bool SigDriver::received(Message& msg, int id)
{
    if (id == Halt) {
	ListIterator iter(channels());
	GenObject* o = 0;
	for (; (o = iter.get()); ) {
	    SigChannel* c = static_cast<SigChannel*>(o);
	    c->disconnect();
	}
	clearLink();
	if (m_engine)
	    m_engine->stop();
    }
    return Driver::received(msg,id);
}

void SigDriver::handleEvent(SignallingEvent* event)
{
    if (!event)
	return;
    // Check if we have a call and a message
    if (!event->call()) {
	switch (event->type()) {
	    case SignallingEvent::Disable:
		// Fall through if no call controller
		if (event->controller())
		    break;
	    default:
		DDebug(this,DebugGoOn,
		    "Received event (%p): %u without call. Controller: (%p)",
		    event,event->type(),event->controller());
		return;
	}
	// Remove link
	Lock lock(m_linksMutex);
	SigLink* link = findLink(event->controller());
	if (!link)
	    return;
	clearLink(link->name(),false,0);
	return;
    }
    if (!event->message()) {
	Debug(this,DebugGoOn,"Received event (%p) without message",event);
	return;
    }
    // Ok. Send the message to the channel if we have one
    SigChannel* ch = static_cast<SigChannel*>(event->call()->userdata());
    if (ch) {
	ch->handleEvent(event);
	if (event->type() == SignallingEvent::Release)
	    ch->disconnect();
	return;
    }
    // No channel
    if (event->type() == SignallingEvent::NewCall) {
	ch = new SigChannel(event);
	if (!ch->route(event)) {
	    ch->hangup("temporary-failure");
	    ch->disconnect();
	}
    }
    else
	XDebug(this,DebugNote,"Received event (%p) from call without user data",event);
}

// Find a link by name
// If callCtrl is true, match only link with call controller
SigLink* SigDriver::findLink(const char* name, bool callCtrl)
{
    if (!name)
	return 0;
    Lock lock(m_linksMutex);
    for (ObjList* o = m_links.skipNull(); o; o = o->skipNext()) {
	SigLink* link = static_cast<SigLink*>(o->get());
	if (link->name() == name) {
	    if (callCtrl && !link->controller())
		return 0;
	    return link;
	}
    }
    return 0;
}

// Find a link by call controller
SigLink* SigDriver::findLink(const SignallingCallControl* ctrl)
{
    if (!ctrl)
	return 0;
    Lock lock(m_linksMutex);
    for (ObjList* o = m_links.skipNull(); o; o = o->skipNext()) {
	SigLink* link = static_cast<SigLink*>(o->get());
	if (link->controller() == ctrl)
	    return link;
    }
    return 0;
}

// Append a link to the list. Duplicate names are not allowed
bool SigDriver::appendLink(SigLink* link)
{
    if (!link || link->name().null())
	return false;
    if (findLink(link->name(),false)) {
	Debug(this,DebugNote,"Can't append link (%p): '%s'. Duplicate name",
	    link,link->name().c_str());
	return false;
    }
    Lock lock(m_linksMutex);
    m_links.append(link);
    DDebug(this,DebugAll,"Link (%p): '%s' added",link,link->name().c_str());
    return true;
}

// Remove a link from list without deleting it
void SigDriver::removeLink(SigLink* link)
{
    if (!link)
	return;
    Lock lock(m_linksMutex);
    m_links.remove(link,false);
    DDebug(this,DebugAll,"Link (%p): '%s' removed",link,link->name().c_str());
}

// Delete the given link if found
// Clear link list if name is 0
// Clear all stacks without waiting for call termination if name is 0
void SigDriver::clearLink(const char* name, bool waitCallEnd, unsigned int howLong)
{
    Lock lock(m_linksMutex);
    if (!name) {
	DDebug(this,DebugAll,"Clearing all links");
	ObjList* obj = m_links.skipNull();
	for (; obj; obj = obj->skipNext()) {
	    SigLink* link = static_cast<SigLink*>(obj->get());
	    link->cleanup();
	}
	m_links.clear();
	return;
    }
    SigLink* link = findLink(name,false);
    if (!link)
	return;
    DDebug(this,DebugAll,"Clearing link '%s'%s",link->name().c_str(),
	waitCallEnd ? ". Waiting for active calls to end" : "");
    // Delay clearing if link has a call controller
    if (waitCallEnd && link->controller()) {
	link->setExiting(howLong);
	return;
    }
    link->cleanup();
    m_links.remove(link,true);
}

void SigDriver::initialize()
{
    Output("Initializing module Signalling Channel");
    if (!m_engine) {
	setup();
	installRelay(Halt);
	installRelay(Progress);
	installRelay(Update);
	installRelay(Route);
	m_engine = new SignallingEngine;
	m_engine->debugChain(this);
	m_engine->start();
    }
    // Get stacks
    s_cfg = Engine::configFile("ysigchan");
    s_cfg.load();
    // Build/initialize links
    Lock lock(m_linksMutex);
    unsigned int n = s_cfg.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* sect = s_cfg.getSection(i);
	if (!sect || sect->null())
	    continue;
	const char* stype = sect->getValue("type");
	int type = lookup(stype,SigLink::s_type,SigLink::Unknown);
	// Check for valid type
	switch (type) {
	    case SigLink::IsdnPriNet:
	    case SigLink::IsdnPriCpe:
	    case SigLink::IsdnPriMon:
		break;
	    default:
		if (stype)
		    Debug(this,DebugNote,"Link '%s'. Unknown type '%s'",sect->c_str(),stype);
		continue;
	}
	// Disable ?
	if (!sect->getBoolValue("enable",true)) {
	    clearLink(*sect,false);
	    continue;
	}
	// Create or initialize
	DDebug(this,DebugAll,"Initializing link '%s' of type '%s'",sect->c_str(),stype);
	SigLink* link = findLink(sect->c_str(),false);
	bool create = (link == 0);
	switch (type) {
	    case SigLink::IsdnPriNet:
	    case SigLink::IsdnPriCpe:
		if (!link)
		    link = new SigIsdn(*sect,type == SigLink::IsdnPriNet);
	    case SigLink::IsdnPriMon:
		if (!link)
		    link = new SigIsdnMonitor(*sect);
		break;
	    default:
		continue;
	}
	if (!link->initialize(*sect)) {
	    Debug(this,DebugWarn,"Failed to initialize link '%s' of type '%s'",
		sect->c_str(),stype);
	    if (create)
		clearLink(*sect);
	}
	else
	    DDebug(this,DebugAll,"Successfully initialized link '%s' of type '%s'",
		sect->c_str(),stype);
    }
}

/**
 * SigParams
 */
void* SigParams::getObject(const String& name) const
{
    if (name == "SignallingCircuitGroup")
	return m_cicGroup;
    return NamedList::getObject(name);
}

/**
 * SigLink
 */
TokenDict SigLink::s_type[] = {
	{"isdn-pri-net", IsdnPriNet},
	{"isdn-pri-cpe", IsdnPriCpe},
	{"isdn-pri-mon", IsdnPriMon},
	{0,0}
	};

SigLink::SigLink(const char* name, Type type)
    : m_controller(0),
    m_init(false),
    m_inband(false),
    m_type(type),
    m_name(name),
    m_thread(0)
{
    plugin.appendLink(this);
    XDebug(&plugin,DebugAll,"SigLink::SigLink('%s') [%p]",name,this);
}

SigLink::~SigLink()
{
    cleanup();
    plugin.removeLink(this);
    XDebug(&plugin,DebugAll,"SigLink::~SigLink [%p]",this);
}

// Set exiting flag for call controller and timeout for the thread
void SigLink::setExiting(unsigned int msec)
{
    if (m_controller)
	m_controller->setExiting();
    if (m_thread)
	m_thread->m_timeout = Time::msecNow() + msec;
}

void SigLink::handleEvent(SignallingEvent* event)
{
    plugin.handleEvent(event);
}

void SigLink::cleanup()
{
    if (m_thread) {
	m_thread->cancel();
	while(m_thread)
	    Thread::yield();
    }
    release();
}

bool SigLink::startThread()
{
    if (!m_thread && m_controller)
	m_thread = new SigLinkThread(this);
    if (!m_thread)
	return false;
    bool ok = m_thread->running();
    if (!ok)
	ok = m_thread->startup();
    return ok;
}

// Build a signalling interface for this link
SignallingInterface* SigLink::buildInterface(const String& device,
	const String& debugName, String& error)
{
    NamedList ifaceDefs("sig");
    ifaceDefs.addParam("debugname",debugName);
    ifaceDefs.addParam("sig",device);
    SignallingInterface* iface = static_cast<SignallingInterface*>
	(SignallingFactory::build(ifaceDefs,&ifaceDefs));
    if (iface) {
	plugin.engine()->insert(iface);
	return iface;
    }
    error = "";
    error << "Failed to create signalling interface '" << device << "'";
    return 0;
}

// Build a signalling circuit for this link
SigCircuitGroup* SigLink::buildCircuits(const String& device,
	const String& debugName, String& error)
{
    ObjList* voice = device.split(',',false);
    if (!voice) {
	error = "Missing or invalid voice parameter";
	return 0;
    }
    SigCircuitGroup* group = new SigCircuitGroup(debugName);
    int start = 0;
    for (ObjList* o = voice->skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	if (s->null())
	    continue;
	String tmp = debugName + "/" + *s;
	SigParams spanParams("voice",group);
	spanParams.addParam("debugname",tmp);
	spanParams.addParam("voice",*s);
	if (start)
	    spanParams.addParam("start",String(start));
	SignallingCircuitSpan* span = static_cast<SignallingCircuitSpan*>(
		SignallingFactory::build(spanParams,&spanParams));
	if (!span) {
	    error << "Failed to build voice span '" << *s << "'";
	    break;
	}
	int chans = spanParams.getIntValue("chans");
	start += chans;
    }
    delete voice;
    if (error.null()) {
	plugin.engine()->insert(group);
	return group;
    }
    delete group;
    return 0;
}

/**
 * SigIsdn
 */
bool SigIsdn::create(NamedList& params)
{
    release();
    String error;                        // Error string
    String compName;                     // Component name
    while (true) {
	// No need to create if no signalling engine or not in plugin's list
	if (!(plugin.engine() && plugin.findLink(name(),false))) {
	    error = "No signalling engine or not in module's list";
	    break;
	}

	m_inband = params.getBoolValue("dtmfinband",s_cfg.getBoolValue("general","dtmfinband",false));

	// Signalling interface
	buildName(compName,"D");
	m_iface = buildInterface(params.getValue("sig"),compName,error);
	if (!m_iface)
	    break;

	// Voice transfer: circuit group, spans, circuits
	// Use the same span as the signalling channel if missing
	buildName(compName,"B");
	const char* device = params.getValue("voice",params.getValue("sig"));
	m_group = buildCircuits(device,compName,error);
	if (!m_group)
	    break;

	// Q921
	buildName(compName,"Q921");
	params.setParam("network",String::boolText(IsdnPriNet == type()));
	params.setParam("print-frames",params.getValue("print-layer2PDU"));
	m_q921 = new ISDNQ921(params,compName);
	plugin.engine()->insert(m_q921);

	// Q931
	buildName(compName,"Q931");
	params.setParam("print-messages",params.getValue("print-layer3PDU"));
	m_controller = new ISDNQ931(params,compName);
	plugin.engine()->insert(q931());

	// Create links between components and enable them
	m_q921->SignallingReceiver::attach(m_iface);
	m_iface->control(SignallingInterface::Enable);
	q931()->attach(m_group);
	m_q921->ISDNLayer2::attach(q931());
	q931()->attach(m_q921);
	m_q921->multipleFrame(true,false);

	// Start thread
	if (!startThread())
	    error = "Failed to start worker thread";

	break;
    }
    if (error.null())
	return true;
    Debug(&plugin,DebugNote,"SigIsdn('%s'). Create failure. %s [%p]",
	name().c_str(),error.c_str(),this);
    return false;
}

bool SigIsdn::reload(NamedList& params)
{
    if (!m_init)
	return false;
    DDebug(&plugin,DebugAll,"SigIsdn('%s'). Reloading [%p]",name().c_str(),this);
    if (q931())
	q931()->setDebug(params.getBoolValue("print-layer3PDU",false),
	    params.getBoolValue("extended-debug",false));
    if (m_q921)
	m_q921->setDebug(params.getBoolValue("print-layer2PDU",false),
	    params.getBoolValue("extended-debug",false));
    return true;
}

void SigIsdn::release()
{
    // *** Cleanup / Disable components
    if (q931())
	q931()->cleanup();
    if (m_q921)
	m_q921->cleanup();
    if (m_iface) {
	m_iface->control(SignallingInterface::Disable);
	m_iface->attach(0);
    }
    // *** Remove links between components
    plugin.engine()->remove(q931());
    plugin.engine()->remove(m_q921);
    plugin.engine()->remove(m_group);
    plugin.engine()->remove(m_iface);
    // *** Release memory
    if (q931())
	delete q931();
    if (m_q921)
	delete m_q921;
    if (m_group)
	delete m_group;
    if (m_iface)
	delete m_iface;
    // *** Reset component pointers
    m_controller = 0;
    m_q921 = 0;
    m_iface = 0;
    m_group = 0;
    XDebug(&plugin,DebugAll,"SigIsdn('%s'). Released [%p]",name().c_str(),this);
}

/**
 * SigIsdnMonitor
 */
SigIsdnMonitor::SigIsdnMonitor(const char* name)
    : SigLink(name,IsdnPriMon),
    m_monitorMutex(true),
    m_id(0),
    m_chanBuffer(160),
    m_idleValue(255),
    m_q921Net(0), m_q921Cpe(0), m_ifaceNet(0), m_ifaceCpe(0), m_groupNet(0), m_groupCpe(0)
{
}

SigIsdnMonitor::~SigIsdnMonitor()
{
    release();
}

void SigIsdnMonitor::handleEvent(SignallingEvent* event)
{
    if (!event)
	return;
    if (!event->call()) {
	XDebug(&plugin,DebugNote,
	    "SigIsdnMonitor('%s'). Received event (%p): '%s' without call [%p]",
	    name().c_str(),event,event->name(),this);
	return;
    }

    Lock lock(m_monitorMutex);
    SigIsdnCallRecord* rec = 0;
    ISDNQ931CallMonitor* mon = static_cast<ISDNQ931CallMonitor*>(event->call());

    // Find monitor
    for (ObjList* o = m_monitors.skipNull(); o; o = o->skipNext()) {
	rec = static_cast<SigIsdnCallRecord*>(o->get());
	if (rec == mon->userdata())
	    break;
	rec = 0;
    }

    if (rec) {
	switch (event->type()) {
	    case SignallingEvent::Info:
		rec->evInfo(event);
		break;
	    case SignallingEvent::Accept:
	    case SignallingEvent::Ringing:
	    case SignallingEvent::Answer:
		if (rec->update(event))
		    break;
		// Fall through to release if update failed
	    case SignallingEvent::Release:
		rec->disconnect(event->message() ? event->message()->params().getValue("reason") : "normal");
		break;
	    default:
		DDebug(&plugin,DebugStub,
		    "SigIsdnMonitor('%s'). No handler for event '%s' [%p]",
		    name().c_str(),event->name(),this);
	}
	return;
    }

    if (event->type() == SignallingEvent::NewCall) {
	String id;
	id << name() << "/" << ++m_id;
	rec = new SigIsdnCallRecord(this,id,event);
	if (rec->update(event)) {
	    mon->userdata(rec);
	    m_monitors.append(rec);
	    rec->deref();
	}
	else
	    rec->disconnect(0);
    }
    else
	XDebug(&plugin,DebugNote,
	    "SigIsdnMonitor('%s'). Received event (%p) with invalid user data (%p) [%p]",
	    name().c_str(),event,mon->userdata(),this);
}

void SigIsdnMonitor::removeCall(SigIsdnCallRecord* call)
{
    Lock lock(m_monitorMutex);
    m_monitors.remove(call,false);
}

bool SigIsdnMonitor::create(NamedList& params)
{
    release();
    String error;                        // Error string
    String compName;                     // Component name
    while (true) {
	// No need to create if no signalling engine or not in plugin's list
	if (!(plugin.engine() && plugin.findLink(name(),false))) {
	    error = "No signalling engine or not in module's list";
	    break;
	}

	m_chanBuffer = params.getIntValue("muxchanbuffer",160);
	if (!m_chanBuffer)
	    m_chanBuffer = 160;
	unsigned int ui = params.getIntValue("idlevalue",255);
	m_idleValue = (ui <= 255 ? ui : 255);

	m_netId = name() + "/Net";
	m_cpeId = name() + "/Cpe";

	// Set auto detection for Layer 2 (Q.921) type side of the link
	params.setParam("detect",String::boolText(true));

	// Signalling interfaces
	buildName(compName,"D",true);
	m_ifaceNet = buildInterface(params.getValue("sig-net"),compName,error);
	if (!m_ifaceNet)
	    break;
	buildName(compName,"D",false);
	m_ifaceCpe = buildInterface(params.getValue("sig-cpe"),compName,error);
	if (!m_ifaceCpe)
	    break;
	
	// Voice transfer: circuit groups, spans, circuits
	// Use the same span as the signalling channel if missing
	buildName(compName,"B",true);
	const char* device = params.getValue("voice-net",params.getValue("sig-net"));
	m_groupNet = buildCircuits(device,compName,error);
	if (!m_groupNet)
	    break;
	buildName(compName,"B",false);
	device = params.getValue("voice-cpe",params.getValue("sig-cpe"));
	m_groupCpe = buildCircuits(device,compName,error);
	if (!m_groupCpe)
	    break;
	String sNet, sCpe;
	m_groupNet->getCicList(sNet);
	m_groupCpe->getCicList(sCpe);
	if (sNet != sCpe)
	    Debug(&plugin,DebugWarn,
		"SigIsdnMonitor('%s'). Circuit groups are not equal [%p]",
		name().c_str(),this);

	// Q921
	params.setParam("t203",params.getValue("idletimeout"));
	buildName(compName,"Q921",true);
	params.setParam("network",String::boolText(true));
	params.setParam("print-frames",params.getValue("print-layer2PDU"));
	m_q921Net = new ISDNQ921Pasive(params,compName);
	plugin.engine()->insert(m_q921Net);
	buildName(compName,"Q921",false);
	params.setParam("network",String::boolText(false));
	m_q921Cpe = new ISDNQ921Pasive(params,compName);
	plugin.engine()->insert(m_q921Cpe);

	// Q931
	compName = "";
	compName << name() << '/' << "Q931";
	params.setParam("print-messages",params.getValue("print-layer3PDU"));
	m_controller = new ISDNQ931Monitor(params,compName);
	plugin.engine()->insert(q931());

	// Create links between components and enable them
	q931()->attach(m_groupNet,true);
	q931()->attach(m_groupCpe,false);
	m_q921Net->SignallingReceiver::attach(m_ifaceNet);
	m_q921Cpe->SignallingReceiver::attach(m_ifaceCpe);
	m_ifaceNet->control(SignallingInterface::Enable);
	m_ifaceCpe->control(SignallingInterface::Enable);
	m_q921Net->ISDNLayer2::attach(q931());
	m_q921Cpe->ISDNLayer2::attach(q931());
	q931()->attach(m_q921Net,true);
	q931()->attach(m_q921Cpe,false);

	// Start thread
	if (!startThread())
	    error = "Failed to start worker thread";

	break;
    }
    if (error.null()) {
	if (debugAt(DebugInfo)) {
	    String tmp;
	    tmp << "\r\nChannel buffer: " << m_chanBuffer;
	    tmp << "\r\nIdle value:     " << (int)m_idleValue;
	    Debug(&plugin,DebugInfo,"SigIsdnMonitor('%s'). Initialized: [%p]%s",
		name().c_str(),this,tmp.c_str());
	}
	return true;
    }
    Debug(&plugin,DebugNote,"SigIsdnMonitor('%s'). Create failure. %s [%p]",
	name().c_str(),error.c_str(),this);
    return false;
}

bool SigIsdnMonitor::reload(NamedList& params)
{
    if (!m_init)
	return false;
    DDebug(&plugin,DebugAll,"SigIsdnMonitor('%s'). Reloading [%p]",name().c_str(),this);
    if (q931())
	q931()->setDebug(params.getBoolValue("print-layer3PDU",false),
	    params.getBoolValue("extended-debug",false));
    if (m_q921Net)
	m_q921Net->setDebug(params.getBoolValue("print-layer2PDU",false),
	    params.getBoolValue("extended-debug",false));
    if (m_q921Cpe)
	m_q921Cpe->setDebug(params.getBoolValue("print-layer2PDU",false),
	    params.getBoolValue("extended-debug",false));
    return true;
}

void SigIsdnMonitor::release()
{
    m_monitorMutex.lock();
    ListIterator iter(m_monitors);
    GenObject* o = 0;
    for (; (o = iter.get()); ) {
	CallEndpoint* c = static_cast<CallEndpoint*>(o);
	c->disconnect();
    }
    m_monitorMutex.unlock();
    // *** Cleanup / Disable components
    if (q931())
	q931()->cleanup();
    if (m_q921Net)
	m_q921Net->cleanup();
    if (m_q921Cpe)
	m_q921Cpe->cleanup();
    if (m_ifaceNet) {
	m_ifaceNet->control(SignallingInterface::Disable);
	m_ifaceNet->attach(0);
    }
    if (m_ifaceCpe) {
	m_ifaceCpe->control(SignallingInterface::Disable);
	m_ifaceCpe->attach(0);
    }
    // *** Remove links between components
    plugin.engine()->remove(q931());
    plugin.engine()->remove(m_q921Net);
    plugin.engine()->remove(m_q921Cpe);
    plugin.engine()->remove(m_groupNet);
    plugin.engine()->remove(m_groupCpe);
    plugin.engine()->remove(m_ifaceNet);
    plugin.engine()->remove(m_ifaceCpe);
    // *** Release memory
    if (q931())
	delete q931();
    if (m_q921Net)
	delete m_q921Net;
    if (m_q921Cpe)
	delete m_q921Cpe;
    if (m_groupNet)
	delete m_groupNet;
    if (m_groupCpe)
	delete m_groupCpe;
    if (m_ifaceNet)
	delete m_ifaceNet;
    if (m_ifaceCpe)
	delete m_ifaceCpe;
    // *** Reset component pointers
    m_controller = 0;
    m_q921Net = m_q921Cpe = 0;
    m_ifaceNet = m_ifaceCpe = 0;
    m_groupNet = m_groupCpe = 0;
    XDebug(&plugin,DebugAll,"SigIsdnMonitor('%s'). Released [%p]",name().c_str(),this);
}

/**
 * SigConsumerMux
 */
void SigConsumerMux::Consume(const DataBlock& data, unsigned long tStamp)
{
    if (m_owner)
	m_owner->consume(m_first,data,tStamp);
}

/**
 * SigSourceMux
 */
SigSourceMux::SigSourceMux(const char* format, unsigned char idleValue, unsigned int chanBuffer)
    : DataSource(format),
    m_lock(true),
    m_firstSrc(0),
    m_secondSrc(0),
    m_firstChan(0),
    m_secondChan(0),
    m_idleValue(idleValue),
    m_sampleLen(0),
    m_maxSamples(0),
    m_samplesFirst(0),
    m_samplesSecond(0),
    m_error(0)
{
    if (getFormat() == "2*slin")
	m_sampleLen = 2;
    else if (getFormat() == "2*mulaw")
	m_sampleLen = 1;
    else if (getFormat() == "2*alaw")
	m_sampleLen = 1;
    else {
	Debug(&plugin,DebugNote,
	    "SigSourceMux::SigSourceMux(). Unsupported format %s [%p]",
	    format,this);
	return;
    }
    // Adjust channel buffer to be multiple of sample length and not lesser then it
    if (chanBuffer < m_sampleLen)
	chanBuffer = m_sampleLen;
    m_maxSamples = chanBuffer / m_sampleLen;
    chanBuffer = m_maxSamples * m_sampleLen;
    m_buffer.assign(0,2 * chanBuffer);
    // +2 to skip ofer the "2*"
    m_firstChan = new SigConsumerMux(this,true,format+2);
    m_secondChan = new SigConsumerMux(this,false,format+2);
    XDebug(&plugin,DebugAll,
	"SigSourceMux::SigSourceMux(). Format: %s, sample=%u, buffer=%u [%p]",
	getFormat().c_str(),m_sampleLen,m_buffer.length(),this);
}

SigSourceMux::~SigSourceMux()
{
    Lock lock(m_lock);
    removeSource(true);
    removeSource(false);
    if (m_firstChan)
	m_firstChan->deref();
    if (m_secondChan)
	m_secondChan->deref();
    XDebug(&plugin,DebugAll,"SigSourceMux::~SigSourceMux() [%p]",this);
}

#define MUX_CHAN (first ? '1' : '2')

// Replace the consumer of the given source. Remove current consumer's source before
// @param first True to replace with the first channel, false for the second
// Return false if source is 0 or has invalid format (other then ours)
bool SigSourceMux::attach(bool first, DataSource* source)
{
    Lock lock(m_lock);
    removeSource(first);
    if (!(source && source->ref()))
	return false;
    if (first) {
	m_firstSrc = source;
	source->attach(m_firstChan);
    }
    else {
	m_secondSrc = source;
	source->attach(m_secondChan);
    }
    return true;
}

// Multiplex received data from consumers and forward it
// Forward multiplexed buffer if chan already filled
// If received data is not greater then free space:
//     Fill chan buffer with data
//     If both channels are filled, forward the multiplexed buffer
// Otherwise:
//     Fill free chan buffer
//     Forward buffer and consume the rest
void SigSourceMux::consume(bool first, const DataBlock& data, unsigned long tStamp)
{
    Lock lock(m_lock);
    unsigned int samples = data.length() / m_sampleLen;
    if (!m_error && (data.length() % m_sampleLen)) {
	Debug(&plugin,DebugWarn,
	    "SigSourceMux. Wrong sample (received %u bytes) on channel %c [%p]",
	    data.length(),MUX_CHAN,this);
	m_error++;
    }
    if (!samples)
	return;

    // Forward buffer if already filled for this channel
    if ((first && firstFull()) || (!first && secondFull())) {
	DDebug(&plugin,DebugMild,"SigSourceMux. Buffer overrun on channel %c [%p]",
	    MUX_CHAN,this);
	forwardBuffer();
    }

    unsigned int freeSamples = m_maxSamples - (first ? m_samplesFirst: m_samplesSecond);
    unsigned char* buf = (unsigned char*)data.data();

    if (samples <= freeSamples) {
	fillBuffer(first,buf,samples);
	if (firstFull() && secondFull())
	    forwardBuffer();
	return;
    }

    // Received more samples that free space in buffer
    fillBuffer(first,buf,freeSamples);
    forwardBuffer();
    unsigned int consumed = freeSamples * m_sampleLen;
    DataBlock rest(buf + consumed,data.length() - consumed);
    consume(first,rest,tStamp);
}

// Forward the buffer if at least one channel is filled. Reset data
// If one channel is empty or incomplete, fill it with idle value
void SigSourceMux::forwardBuffer()
{
    if (!(firstFull() || secondFull()))
	return;
    if (!(firstFull() && secondFull()))
	fillBuffer(!firstFull());
    m_samplesFirst = m_samplesSecond = 0;
    Forward(m_buffer);
}

// Fill interlaced samples buffer with samples of received data
// If no data, fill the free space with idle value
void SigSourceMux::fillBuffer(bool first, unsigned char* data, unsigned int samples)
{
    unsigned int* count = (first ? &m_samplesFirst : &m_samplesSecond);
    unsigned char* buf = (unsigned char*)m_buffer.data() + *count * m_sampleLen * 2;
    if (!first)
	buf += m_sampleLen;
    // Fill received data
    if (data) {
	if (samples > m_maxSamples - *count)
	    samples = m_maxSamples - *count;
	*count += samples;
	switch (m_sampleLen) {
	    case 1:
		for (; samples; samples--, buf += 2)
		    *buf = *data++;
		break;
	    case 2:
		for (; samples; samples--, buf += 4) {
		    buf[0] = *data++;
		    buf[1] = *data++;
		}
		break;
	    case 0:
		samples = 0;
	    default: {
		unsigned int delta = 2 * m_sampleLen;
		for (; samples; samples--, buf += delta, data += m_sampleLen)
		    ::memcpy(buf,data,m_sampleLen);
	    }
	}
	return;
    }
    // Fill with idle value
    samples = m_maxSamples - *count;
    *count = m_maxSamples;
    switch (m_sampleLen) {
	case 1:
	    for (; samples; samples--, buf += 2)
		*buf = m_idleValue;
	    break;
	case 2:
	    for (; samples; samples--, buf += 4)
		buf[0] = buf[1] = m_idleValue;
	    break;
	case 0:
	    samples = 0;
	default: {
	    unsigned int delta = 2 * m_sampleLen;
	    for (; samples; samples--, buf += delta, data += m_sampleLen)
		::memset(buf,m_idleValue,m_sampleLen);
	}
    }
}

// Remove the source for the appropriate consumer
void SigSourceMux::removeSource(bool first)
{
    DataSource** src = first ? &m_firstSrc : &m_secondSrc;
    if (*src) {
	(*src)->clear();
	(*src)->deref();
	*src = 0;
    }
}

#undef MUX_CHAN

/**
 * SigIsdnCallRecord
 */
SigIsdnCallRecord::SigIsdnCallRecord(SigIsdnMonitor* monitor, const char* id,
	SignallingEvent* event)
    : CallEndpoint(id),
    m_lock(true),
    m_netInit(false),
    m_monitor(monitor),
    m_call(0)
{
    m_status = "startup";
    // This parameters should be checked by the monitor
    if (!(monitor && event && event->message() && event->call() && event->call()->ref())) {
	m_reason = "Invalid initiating event";
	return;
    }
    m_call = static_cast<ISDNQ931CallMonitor*>(event->call());
    m_netInit = m_call->netInit();
    SignallingMessage* msg = event->message();
    m_caller = msg->params().getValue("caller");
    m_called = msg->params().getValue("called");
    Debug(this->id(),DebugCall,"Initialized. Caller: '%s'. Called: '%s' [%p]",
	m_caller.c_str(),m_called.c_str(),this);
}

SigIsdnCallRecord::~SigIsdnCallRecord()
{
    close(0);
    if (m_monitor)
	m_monitor->removeCall(this);
    Message* m = message("chan.hangup",false);
    m->addParam("status",m_status);
    m->addParam("reason",m_reason);
    Engine::enqueue(m);
    Debug(id(),DebugCall,"Destroyed. Reason: '%s' [%p]",m_reason.safe(),this);
}

// Update call endpoint status. Send chan.startup, call.route, call.execute
// Create the multiplexer if missing
// Update sources for the multiplexer. Change them if circuit changed
// Start recording if the multiplexer has at least one source
bool SigIsdnCallRecord::update(SignallingEvent* event)
{
    Lock lock(m_lock);
    if (!(m_call && m_monitor && event && event->message()))
	return false;
    switch (event->type()) {
	case SignallingEvent::NewCall: Engine::enqueue(message("chan.startup")); break;
	case SignallingEvent::Ringing: m_status = "ringing"; break;
	case SignallingEvent::Answer:  m_status = "answered"; break;
	case SignallingEvent::Accept:  break;
	default: ;
    }
    SignallingMessage* msg = event->message();
    bool chg = msg->params().getValue("circuit-change");
    String format = msg->params().getValue("format");
    format = "2*" + format;
    SigSourceMux* source = static_cast<SigSourceMux*>(getSource());
    m_reason = "";
    while (!source) {
	if (!format)
	    return true;
	source = new SigSourceMux(format,m_monitor->idleValue(),m_monitor->chanBuffer());
	if (!source->sampleLen()) {
	    source->deref();
	    m_reason = "Unsupported audio format";
	    break;
	}
	setSource(source);
	source->deref();
	if (!getSource()) {
	    m_reason = "Failed to set data source";
	    break;
	}
	// Start recording
	if (!callRouteAndExec(format))
	    break;
	DDebug(id(),DebugCall,"Start recording. Format: %s [%p]",format.c_str(),this);
    }
    if (m_reason.null() && format && source->getFormat() != format)
	m_reason = "Data format changed";
    if (!m_reason.null())
	return close(0);
    if (chg) {
	source->removeSource(true);
	source->removeSource(false);
    }
    // Set sources if missing
    bool first = true;
    while (true) {
	if (!source->hasSource(first)) {
	    SignallingCircuit* cic = static_cast<SignallingCircuit*>(m_call->getObject(
		first ? "SignallingCircuitCaller" : "SignallingCircuitCalled"));
	    DataSource* src = cic ? static_cast<DataSource*>(cic->getObject("DataSource")) : 0;
	    if (src) {
		source->attach(first,src);
		DDebug(id(),DebugAll,"Data source on channel %c set to (%p) [%p]",
		    first ? '1' : '2',src,this);
	    }
	}
	if (!first)
	    break;
	first = false;
    }
    return true;
}

// Close
bool SigIsdnCallRecord::close(const char* reason)
{
    Lock lock(m_lock);
    m_status = "hangup";
    if (!m_call)
	return false;
    if (m_reason.null())
	m_reason = reason;
    if (m_reason.null())
	m_reason = Engine::exiting() ? "net-out-of-order" : "unknown";
    m_call->userdata(0);
    if (m_monitor)
	m_monitor->q931()->terminateMonitor(m_call,m_reason);
    m_call->deref();
    m_call = 0;
    setSource();
    Debug(id(),DebugCall,"Closed. Reason: '%s' [%p]",m_reason.c_str(),this);
    return false;
}

bool SigIsdnCallRecord::disconnect(const char* reason)
{
    close(reason);
    XDebug(id(),DebugCall,"Disconnecting. Reason: '%s' [%p]",m_reason.safe(),this);
    return CallEndpoint::disconnect(m_reason);
}

void SigIsdnCallRecord::disconnected(bool final, const char* reason)
{
    DDebug(id(),DebugCall,"Disconnected. Final: %s. Reason: '%s' [%p]",
	String::boolText(final),reason,this);
    if (m_reason.null())
	m_reason = reason;
    CallEndpoint::disconnected(final,m_reason);
}

Message* SigIsdnCallRecord::message(const char* name, bool peers, bool userdata)
{
    Message* m = new Message(name);
    m->addParam("id",id());
    if (peers) {
	m->addParam("caller",m_caller);
	m->addParam("called",m_called);
    }
    if (userdata)
	m->userData(this);
    return m;
}

bool SigIsdnCallRecord::callRouteAndExec(const char* format)
{
    Message* m = message("call.route");
    bool ok = false;
    while (true) {
	m->addParam("type","record");
	m->addParam("format",format);
	m->addParam("callsource",m_netInit ? "net" : "cpe");
	if (!Engine::dispatch(m) || m->retValue().null()) {
	    m_reason = "noroute";
	    break;
	}
	*m = "call.execute";
	m->userData(this);
	m->setParam("callto",m->retValue());
	m->retValue().clear();
	if (!Engine::dispatch(m)) {
	    m_reason = "noconn";
	    break;
	}
	ok = true;
	break;
    }
    delete m;
    return ok;
}

void SigIsdnCallRecord::evInfo(SignallingEvent* event)
{
    if (!(event && event->message()))
	return;
    String tmp = event->message()->params().getValue("tone");
    if (!tmp.null()) {
	Message* m = message("chan.dtmf",false);
	m->addParam("text",tmp);
	bool fromCaller = event->message()->params().getValue("fromcaller",false);
	m->addParam("sender",fromCaller ? m_caller : m_called);
	Engine::enqueue(m);
    }
}

/**
 * SigLinkThread
 */
void SigLinkThread::run()
{
    if (!(m_link && m_link->controller()))
	return;
    DDebug(&plugin,DebugAll,"SigLinkThread::run(). Link: '%s' [%p]",
	m_link->name().c_str(),this);
    SignallingEvent* event = 0;
    while (true) {
	if (!event)
	    Thread::yield(true);
	else if (Thread::check(true))
	    break;
	Time time;
	event = m_link->controller()->getEvent(time);
	if (event) {
	    m_link->handleEvent(event);
	    delete event;
	}
	// Check timeout if waiting to terminate
	if (m_timeout && time.msec() > m_timeout) {
	    DDebug(&plugin,DebugInfo,
		"SigLinkThread::run(). Link '%s' timed out [%p]",
		m_link->name().c_str(),this);
	    String name = m_link->name();
	    // Break the link between link and worker thread
	    m_link->m_thread = 0;
	    m_link = 0;
	    plugin.clearLink(name);
	    break;
	}
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
