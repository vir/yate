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
class SigSS7Isup;                        // SS7 ISDN User Part call controller
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
    SigChannel(Message& msg, const char* caller, const char* called, SigLink* link);
    virtual ~SigChannel();
    inline SignallingCall* call() const
        { return m_call; }
    inline SigLink* link() const
        { return m_link; }
    inline bool hungup() const
	{ return m_hungup; }
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
    bool disconnect()
	{ return Channel::disconnect(m_reason); }
    void handleEvent(SignallingEvent* event);
    void hangup(const char* reason = 0);
private:
    virtual void statusParams(String& str);
    // Set call status. Print debug message
    void setState(const char* state, bool updateStatus = true, bool showReason = false);
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
    String m_caller;
    String m_called;
    SignallingCall* m_call;              // The signalling call this channel is using
    SigLink* m_link;                     // The link owning the signalling call
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
    SS7Router* router() const
	{ return m_router; }
    void handleEvent(SignallingEvent* event);
    bool received(Message& msg, int id);
    // Find a link by name
    // If callCtrl is true, match only links with call controller
    SigLink* findLink(const char* name, bool callCtrl);
    // Find a link by call controller
    SigLink* findLink(const SignallingCallControl* ctrl);
    // Disconnect channels. If link is not 0, disconnect only channels belonging to that link
    void disconnectChannels(SigLink* link = 0);
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
    SS7Router* m_router;                 // The SS7 router
    ObjList m_links;                     // Link list
    Mutex m_linksMutex;                  // Lock link list operations
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
	SS7Isup,
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
    // Initialize (create or reload) the link. Process the debuglayer parameter.
    // Fix some type depending parameters
    // Return false on failure
    bool initialize(NamedList& params);
    // Handle events received from call controller
    // Default action: calls the driver's handleEvent()
    virtual void handleEvent(SignallingEvent* event);
    // Handle chan.masquerade. Return true if handled
    // @param id Channel id
    // @param msg Received message
    virtual bool masquerade(String& id, Message& msg)
	{ return false; }
    // Handle chan.drop. Return true if handled
    // @param id Channel id
    // @param msg Received message
    virtual bool drop(String& id, Message& msg)
	{ return false; }
    // Clear channels with calls belonging to this link. Cancel thread if any. Call release
    void cleanup();
    // Type names
    static TokenDict s_type[];
protected:
    // Create/Reload/Release data
    virtual bool create(NamedList& params, String& error) { return false; }
    virtual bool reload(NamedList& params) { return false; }
    virtual void release() {}
    // Get debug enabler to set debug for it
    virtual DebugEnabler* getDbgEnabler(int id) { return 0; }
    // Start worker thread. Set error on failure
    bool startThread(String& error);
    // Build the signalling interface and insert it in the engine
    static SignallingInterface* buildInterface(NamedList& params, const String& device,
	const String& debugName, String& error);
    // Build a signalling circuit group and insert it in the engine
    static SigCircuitGroup* buildCircuits(NamedList& params, const String& device,
	const String& debugName, String& error);
    // Build component debug name
    inline void buildName(String& dest, const char* name)
	{ dest = ""; dest << this->name() << '/' << name; }
    SignallingCallControl* m_controller; // Call controller, if any
    bool m_init;                         // True if already initialized
    bool m_inband;                       // True to send in-band tones through this link
private:
    int m_type;                          // Link type
    String m_name;                       // Link name
    SigLinkThread* m_thread;             // Event thread for call controller
};

// SS7 ISDN User Part call controller
class SigSS7Isup : public SigLink
{
public:
    SigSS7Isup(const char* name);
    virtual ~SigSS7Isup();
protected:
    virtual bool create(NamedList& params, String& error);
    virtual bool reload(NamedList& params);
    virtual void release();
    virtual DebugEnabler* getDbgEnabler(int id);
    // Add point codes from a given configuration section
    // @return The number of point codes added
    unsigned int setPointCode(const NamedList& sect);
    inline SS7ISUP* isup()
	{ return static_cast<SS7ISUP*>(m_controller); }
private:
    SS7MTP3* m_network;
    SS7MTP2* m_link;
    SignallingInterface* m_iface;
    SigCircuitGroup* m_group;
};

// Q.931 call control over HDLC interface
class SigIsdn : public SigLink
{
public:
    SigIsdn(const char* name, bool net);
    virtual ~SigIsdn();
protected:
    virtual bool create(NamedList& params, String& error);
    virtual bool reload(NamedList& params);
    virtual void release();
    virtual DebugEnabler* getDbgEnabler(int id);
    inline ISDNQ931* q931()
	{ return static_cast<ISDNQ931*>(m_controller); }
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
    virtual bool masquerade(String& id, Message& msg);
    virtual bool drop(String& id, Message& msg);
    unsigned int chanBuffer() const
	{ return m_chanBuffer; }
    unsigned char idleValue() const
	{ return m_idleValue; }
    const String& peerId(bool network) const
	{ return network ? m_netId : m_cpeId; }
    // Remove a call and it's call monitor
    void removeCall(SigIsdnCallRecord* call);
protected:
    virtual bool create(NamedList& params, String& error);
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
    String m_address;                    // Address including circuit number
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
	: Thread("SigLinkThread"), m_link(link), m_timeout(0)
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
    m_call(event->call()),
    m_link(0),
    m_hungup(false),
    m_inband(false)
{
    if (!(m_call && m_call->ref())) {
	Debug(this,DebugCall,"No signalling call for this incoming call");
	m_call = 0;
	return;
    }
    SignallingMessage* msg = event->message();
    m_caller = msg ? msg->params().getValue("caller") : 0;
    m_called = msg ? msg->params().getValue("called") : 0;
    m_call->userdata(this);
    m_link = plugin.findLink(m_call->controller());
    if (m_link)
	m_inband = m_link->inband();
    // Startup
    setState(0);
    SignallingCircuit* cic = getCircuit();
    if (m_link && cic)
	m_address << m_link->name() << "/" << cic->code();
    Message* m = message("chan.startup");
    m->setParam("direction",status());
    m->addParam("caller",m_caller);
    m->addParam("called",m_called);
    m->copyParam(msg->params(),"callername");
    // TODO: Add call control parameter ?
    Engine::enqueue(m);
    // Route the call
    m = message("call.preroute",false,true);
    if (msg) {
	m->addParam("caller",m_caller);
	m->addParam("called",m_called);
	m->copyParam(msg->params(),"callername");
	m->copyParam(msg->params(),"format");
	m->copyParam(msg->params(),"formats");
	m->copyParam(msg->params(),"callernumtype");
	m->copyParam(msg->params(),"callernumplan");
	m->copyParam(msg->params(),"callerpres");
	m->copyParam(msg->params(),"callerscreening");
	m->copyParam(msg->params(),"callednumtype");
	m->copyParam(msg->params(),"callednumplan");
    }
    // TODO: Add call control parameter ?
    if (!startRouter(m))
	hangup("temporary-failure");
}

// Construct an outgoing channel
SigChannel::SigChannel(Message& msg, const char* caller, const char* called, SigLink* link)
    : Channel(&plugin,0,true),
    m_callMutex(true),
    m_caller(caller),
    m_called(called),
    m_call(0),
    m_link(link),
    m_hungup(false),
    m_inband(false)
{
    // Startup
    setState(0);
    if (!(m_link && m_link->controller())) {
	msg.setParam("error","noconn");
	m_hungup = true;
	return;
    }
    // Data
    m_inband = link->inband();
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
    sigMsg->params().copyParam(msg,"calledpointcode");
    m_call = link->controller()->call(sigMsg,m_reason);
    if (m_call) {
	m_call->userdata(this);
	SignallingCircuit* cic = getCircuit();
	if (cic)
	    m_address << m_link->name() << "/" << cic->code();
	setMaxcall(msg);
    }
    else
	msg.setParam("error",m_reason);
    Message* m = message("chan.startup",msg);
    m->setParam("direction",status());
    m_targetid = msg.getValue("id");
    m->setParam("caller",caller);
    m->setParam("called",called);
    m->setParam("billid",msg.getValue("billid"));
    // TODO: Add call control parameter ?
    Engine::enqueue(m);
}

SigChannel::~SigChannel()
{
    hangup();
    setState("destroyed",true,true);
}

void SigChannel::handleEvent(SignallingEvent* event)
{
    if (!event)
	return;
    XDebug(this,DebugAll,"Got event (%p,'%s') [%p]",event,event->name(),this);
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

bool SigChannel::msgProgress(Message& msg)
{
    Lock lock(m_callMutex);
    setState("progressing");
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
    Lock lock(m_callMutex);
    setState("ringing");
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
    Lock lock(m_callMutex);
    setState("answered");
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
    DDebug(this,DebugCall,"Tone. '%s' %s[%p]",tone,(m_call ? "" : ". No call "),this);
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
    DDebug(this,DebugCall,"Text. '%s' %s[%p]",text,(m_call ? "" : ". No call "),this);
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
    hangup(reason ? reason : "dropped");
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
    setState("prerouted",false);
    return m_call != 0;
}

bool SigChannel::callRouted(Message& msg)
{
    Lock lock(m_callMutex);
    setState("routed",false);
    return m_call != 0;
}

void SigChannel::callAccept(Message& msg)
{
    Lock lock(m_callMutex);
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
    setState("accepted",false);
    Channel::callAccept(msg);
}

void SigChannel::callRejected(const char* error, const char* reason, const Message* msg)
{
    if (m_reason.null())
	m_reason = error ? error : reason;
    setState("rejected",false,true);
    hangup();
}

void SigChannel::disconnected(bool final, const char* reason)
{
    if (m_reason.null())
	m_reason = reason;
    setState("disconnected",false,true);
    hangup();
    Channel::disconnected(final,m_reason);
}

void SigChannel::hangup(const char* reason)
{
    Lock lock(m_callMutex);
    if (m_hungup)
	return;
    setSource();
    setConsumer();
    m_hungup = true;
    if (m_reason.null())
	m_reason = reason ? reason : (Engine::exiting() ? "net-out-of-order" : "normal");
    setState("hangup",true,true);
    if (m_call) {
	m_call->userdata(0);
	SignallingMessage* msg = new SignallingMessage;
	msg->params().addParam("reason",m_reason);
	SignallingEvent* event = new SignallingEvent(SignallingEvent::Release,msg,m_call);
	msg->deref();
	m_call->sendEvent(event);
	m_call->deref();
	m_call = 0;
    }
    lock.drop();
    Message* m = message("chan.hangup",true);
    m->setParam("status",status());
    m->setParam("reason",m_reason);
    Engine::enqueue(m);
}

void SigChannel::statusParams(String& str)
{
    Channel::statusParams(str);
}

void SigChannel::setState(const char* state, bool updateStatus, bool showReason)
{
    if (updateStatus && state)
	status(state);
#ifdef DEBUG
    if (!debugAt(DebugCall))
	return;
    if (!state) {
	Debug(this,DebugCall,"%s call from '%s' to '%s' (Link: %s) [%p]",
	    isOutgoing()?"Outgoing":"Incoming",m_caller.safe(),m_called.safe(),
	    m_link ? m_link->name().c_str() : "no link",this);
	return;
    }
    String show;
    show << "Call " << state;
    if (showReason)
	show << ". Reason: '" << m_reason << "'";
    if (!m_call)
        show << ". No signalling call ";
    if (updateStatus)
	Debug(this,DebugCall,"%s [%p]",show.c_str(),this);
    else
	DDebug(this,DebugCall,"%s [%p]",show.c_str(),this);
#endif
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
    setState("progressing");
    Engine::enqueue(message("call.progress"));
}

void SigChannel::evRelease(SignallingEvent* event)
{
    const char* reason = 0;
    if (event->message())
	reason = event->message()->params().getValue("reason");
    hangup(reason);
}

void SigChannel::evAccept(SignallingEvent* event)
{
    setState("accepted",false,false);
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
    setState("answered");
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
    setState("ringing");
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
    m_router(0),
    m_linksMutex(true)
{
    Output("Loaded module Signalling Channel");
}

SigDriver::~SigDriver()
{
    Output("Unloading module Signalling Channel");
    clearLink();
    if (m_router) {
	if (m_engine)
	    m_engine->remove(m_router);
	TelEngine::destruct(m_router);
    }
    if (m_engine)
	delete m_engine;
}

bool SigDriver::msgExecute(Message& msg, String& dest)
{
    Channel* peer = static_cast<Channel*>(msg.userData());
    if (!peer) {
	Debug(this,DebugNote,"Signalling call failed. No data channel");
	msg.setParam("error","failure");
	return false;
    }
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
    SigChannel* sigCh = new SigChannel(msg,msg.getValue("caller"),dest,link);
    bool ok = sigCh->call();
    if (ok) {
	if (sigCh->connect(peer,msg.getValue("reason"))) {
	    msg.setParam("peerid",sigCh->id());
	    msg.setParam("targetid",sigCh->id());
        }
    }
    else {
	if (!msg.getValue("error"))
	    msg.setParam("error","failure");
	Debug(this,DebugNote,"Signalling call failed with reason '%s'",msg.getValue("error"));
    }
    sigCh->deref();
    return ok;
}

bool SigDriver::received(Message& msg, int id)
{
    switch (id) {
	case Masquerade: {
	    String s = msg.getValue("id");
	    if (s.startsWith(prefix()))
		break;
	    // Check for a link that would handle the message
	    int found = s.find('/');
	    if (found < 1)
		break;
	    SigLink* link = findLink(s.substr(0,found),false);
	    if (link && link->masquerade(s,msg))
		return false;
	    }
	    break;
	case Drop: {
	    String s = msg.getValue("id");
	    if (s.startsWith(prefix()))
		break;
	    // Check for a link that would handle the message
	    SigLink* link = findLink(s.substr(0,s.find('/')),false);
	    return link && link->drop(s,msg);
	    }
	    break;
	case Halt:
	    clearLink();
	    if (m_engine)
		m_engine->stop();
	    break;
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
		    "Received event (%p,'%s') without call. Controller: (%p)",
		    event,event->name(),event->controller());
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
	Debug(this,DebugGoOn,"Received event (%p,'%s') without message",event,event->name());
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
	if (ch->hungup())
	    ch->disconnect();
    }
    else
	XDebug(this,DebugNote,"Received event (%p,'%s') from call without user data",
	    event,event->name());
}

// Find a link by name
// If callCtrl is true, match only links with call controller
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

// Disconnect channels. If link is not 0, disconnect only channels belonging to that link
void SigDriver::disconnectChannels(SigLink* link)
{
    ListIterator iter(channels());
    GenObject* o = 0;
    if (link)
	for (; (o = iter.get()); ) {
	    SigChannel* c = static_cast<SigChannel*>(o);
	    if (link == c->link())
		c->disconnect();
	}
    else
	for (; (o = iter.get()); ) {
	    SigChannel* c = static_cast<SigChannel*>(o);
	    c->disconnect();
	}
}

// Append a link to the list. Duplicate names are not allowed
bool SigDriver::appendLink(SigLink* link)
{
    if (!link || link->name().null())
	return false;
    if (findLink(link->name(),false)) {
	Debug(this,DebugWarn,"Can't append link (%p): '%s'. Duplicate name",
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
	disconnectChannels();
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
    s_cfg = Engine::configFile("ysigchan");
    s_cfg.load();
    // Startup
    if (!m_engine) {
	setup();
	installRelay(Masquerade);
	installRelay(Halt);
	installRelay(Progress);
	installRelay(Update);
	installRelay(Route);
	m_engine = new SignallingEngine;
	m_engine->debugChain(this);
	m_engine->start();
	// SS7
	m_router = new SS7Router;
	m_router->attach(new SS7Management);
	m_router->attach(new SS7Maintenance);
	m_engine->insert(m_router);
    }
    // Build/initialize links
    Lock lock(m_linksMutex);
    unsigned int n = s_cfg.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* sect = s_cfg.getSection(i);
	if (!sect || sect->null() || *sect == "general")
	    continue;
	const char* stype = sect->getValue("type");
	int type = lookup(stype,SigLink::s_type,SigLink::Unknown);
	// Check for valid type
	if (type == SigLink::Unknown) {
	    Debug(this,DebugNote,"Link '%s'. Unknown/missing type '%s'",sect->c_str(),stype);
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
	if (create)
	    switch (type) {
		case SigLink::SS7Isup:
		    link = new SigSS7Isup(*sect);
		    break;
		case SigLink::IsdnPriNet:
		case SigLink::IsdnPriCpe:
		    link = new SigIsdn(*sect,type == SigLink::IsdnPriNet);
		    break;
		case SigLink::IsdnPriMon:
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
	{"ss7-isup",     SS7Isup},
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

// Initialize (create or reload) the link. Set debug levels for contained objects
// Fix some type depending parameters:
//   Force 'readonly' to true for ISDN monitors
//   Check the value of 'rxunderruninterval' for SS7 and non monitor ISDN links
bool SigLink::initialize(NamedList& params)
{
    // Check error:
    //  No need to initialize if no signalling engine or not in plugin's list
    //  For SS7 links check the router
    String error;
    while (true) {
#define SIGLINK_INIT_BREAK(s) {error=s;break;}
	if (!(plugin.engine() && plugin.findLink(name(),false)))
	    SIGLINK_INIT_BREAK("No engine or not in module's list")
	if (type() == SS7Isup && !plugin.router())
	    SIGLINK_INIT_BREAK("No SS7 router for this link")
#undef SIGLINK_INIT_BREAK
	// Fix type depending parameters
	int minRxUnder = 0;
	switch (m_type) {
	    case SigLink::SS7Isup:
		minRxUnder = 25;
		break;
	    case SigLink::IsdnPriNet:
	    case SigLink::IsdnPriCpe:
		minRxUnder = 2500;
		break;
	    case SigLink::IsdnPriMon:
		minRxUnder = 2500;
		params.setParam("readonly","true");
		break;
	    default: ;
	}
	if (minRxUnder) {
	    int rx = params.getIntValue("rxunderruninterval",0);
	    if (rx && minRxUnder > rx)
		params.setParam("rxunderruninterval",String(minRxUnder));
	}
	// Create/reload
	bool ok = m_init ? reload(params) : create(params,error);
	m_init = true;
	// Apply 'debuglayer'
	if (ok) {
	    String dbgLevel = params.getValue("debuglayer");
	    DDebug(&plugin,DebugAll,"SigLink('%s'). Set debug '%s' [%p]",
		name().c_str(),dbgLevel.safe(),this);
	    ObjList* levelList = dbgLevel.split(',',true);
	    int i = 0;
	    for (ObjList* o = levelList->skipNull(); o; o = o->skipNext(), i++) {
		int level = (static_cast<String*>(o->get()))->toInteger(-1);
		if (level == -1)
		    continue;
		DebugEnabler* dbg = getDbgEnabler(i);
		if (dbg) {
		    dbg->debugEnabled(level);
		    if (level)
			dbg->debugLevel(level);
		}
	    }
	    TelEngine::destruct(levelList);
	    return true;
	}
	break;
    }
    Debug(&plugin,DebugNote,"Link('%s'). %s failure: %s [%p]",
	name().c_str(),m_init?"Reload":"Create",error.safe(),this);
    return false;
}

void SigLink::handleEvent(SignallingEvent* event)
{
    plugin.handleEvent(event);
}

// Clear channels with calls belonging to this link. Cancel thread if any. Call release
void SigLink::cleanup()
{
    plugin.disconnectChannels(this);
    if (m_thread) {
	m_thread->cancel();
	while(m_thread)
	    Thread::yield();
    }
    release();
}

bool SigLink::startThread(String& error)
{
    if (!m_thread) {
	if (m_controller)
	    m_thread = new SigLinkThread(this);
	else {
	    Debug(&plugin,DebugNote,
		"Link('%s'). No worker thread for link without call controller [%p]",
		name().c_str(),this);
	    return true;
	}
    }
    if (!(m_thread->running() || m_thread->startup())) {
	error = "Failed to start worker thread";
	return false;
    }
    return true;
}

// Build a signalling interface for this link
SignallingInterface* SigLink::buildInterface(NamedList& params, const String& device,
	const String& debugName, String& error)
{
    params.setParam("debugname",debugName);
    bool needSig = !params.getParam("sig");
    if (needSig)
	params.addParam("sig",device);
    SignallingInterface* iface = static_cast<SignallingInterface*>
	(SignallingFactory::build("sig",&params));
    if (needSig)
	params.clearParam("sig");
    if (iface) {
	plugin.engine()->insert(iface);
	return iface;
    }
    error = "";
    error << "Failed to create signalling interface '" << device << "'";
    return 0;
}

// Build a signalling circuit for this link
SigCircuitGroup* SigLink::buildCircuits(NamedList& params, const String& device,
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
    TelEngine::destruct(voice);
    if (error.null()) {
	plugin.engine()->insert(group);
	return group;
    }
    TelEngine::destruct(group);
    return 0;
}

/**
 * SigSS7Isup
 */
SigSS7Isup::SigSS7Isup(const char* name)
    : SigLink(name,SS7Isup),
    m_network(0),
    m_link(0),
    m_iface(0),
    m_group(0)
{
}

SigSS7Isup::~SigSS7Isup()
{
    release();
}

bool SigSS7Isup::create(NamedList& params, String& error)
{
    release();

    if (!plugin.router()) {
	error = "No SS7 router";
	return false;
    }

    String compName;                     // Component name

    // Signalling interface
    buildName(compName,"L1");
    m_iface = buildInterface(params,params.getValue("sig"),compName,error);
    if (!m_iface)
	return false;

    // Voice transfer: circuit group, spans, circuits
    // Use the same span as the signalling channel if missing
    buildName(compName,"L1/Data");
    m_group = buildCircuits(params,params.getValue("voice",params.getValue("sig")),compName,error);
    if (!m_group)
	return false;

    // Layer 2
    buildName(compName,"mtp2");
    params.setParam("debugname",compName);
    m_link = new SS7MTP2(params);

    // Layer 3
    buildName(compName,"mtp3");
    params.setParam("debugname",compName);
    m_network = new SS7MTP3(params);

    // ISUP
    buildName(compName,"isup");
    params.setParam("debugname",compName);
    m_controller = new SS7ISUP(params);
    if (!setPointCode(params)) {
	error = "No point codes";
	return false;
    }

    // Create links between components and enable them
    m_link->SignallingReceiver::attach(m_iface);
    m_iface->control(SignallingInterface::Enable);
    m_network->attach(m_link);
    controller()->attach(m_group);
    plugin.router()->attach(m_network);
    plugin.router()->attach(isup());
    m_link->control(SS7Layer2::Align,&params);

    // Start thread
    if (!startThread(error))
	return false;

    return true;
}

bool SigSS7Isup::reload(NamedList& params)
{
    setPointCode(params);
    return true;
}

void SigSS7Isup::release()
{
    // *** Cleanup
    if (isup())
	isup()->cleanup();
    // *** Disable/detach components
    if (controller())
	controller()->attach(0);
    if (m_network)
	m_network->attach(0);
    if (m_link) {
	m_link->control(SS7Layer2::Pause);
	m_link->SignallingReceiver::attach(0);
    }
    if (m_iface) {
	m_iface->control(SignallingInterface::Disable);
	m_iface->attach(0);
    }
    // Remove from engine
    if (plugin.engine()) {
	plugin.engine()->remove(isup());
	plugin.engine()->remove(m_network);
	plugin.engine()->remove(m_link);
	plugin.engine()->remove(m_group);
	plugin.engine()->remove(m_iface);
    }
    // *** Release memory
    if (isup()) {
	isup()->destruct();
	m_controller = 0;
    }
    TelEngine::destruct(m_network);
    TelEngine::destruct(m_link);
    TelEngine::destruct(m_group);
    TelEngine::destruct(m_iface);
    XDebug(&plugin,DebugAll,"SigSS7Isup('%s'). Released [%p]",name().c_str(),this);
}

DebugEnabler* SigSS7Isup::getDbgEnabler(int id)
{
    switch (id) {
	case 0: return m_iface;
	case 1: return m_group;
	case 2: return m_link;
	case 3: return m_network;
	case 4: return isup();
    }
    return 0;
}

unsigned int SigSS7Isup::setPointCode(const NamedList& sect)
{
    if (!isup())
	return 0;
    unsigned int count = 0;
    unsigned int n = sect.length();
    for (unsigned int i= 0; i < n; i++) {
	NamedString* ns = sect.getParam(i);
	if (!ns)
	    continue;
	bool def = (ns->name() == "defaultpointcode");
	if (!def && ns->name() != "pointcode")
	    continue;
	SS7PointCode* pc = new SS7PointCode(0,0,0);
	if (pc->assign(*ns) && isup()->setPointCode(pc,def))
	    count++;
	else {
	    Debug(&plugin,DebugNote,"Invalid %s=%s in section '%s'",
		ns->name().c_str(),ns->safe(),sect.safe());
	    TelEngine::destruct(pc);
	}
    }
    return count;
}

/**
 * SigIsdn
 */
SigIsdn::SigIsdn(const char* name, bool net)
    : SigLink(name,net ? IsdnPriNet : IsdnPriCpe),
    m_q921(0),
    m_iface(0),
    m_group(0)
{
}

SigIsdn::~SigIsdn()
{
    release();
}

bool SigIsdn::create(NamedList& params, String& error)
{
    release();
    m_inband = params.getBoolValue("dtmfinband",s_cfg.getBoolValue("general","dtmfinband",false));
    String compName;                     // Component name

    // Signalling interface
    buildName(compName,"D");
    m_iface = buildInterface(params,params.getValue("sig"),compName,error);
    if (!m_iface)
	return false;

    // Voice transfer: circuit group, spans, circuits
    // Use the same span as the signalling channel if missing
    buildName(compName,"B");
    m_group = buildCircuits(params,params.getValue("voice",params.getValue("sig")),compName,error);
    if (!m_group)
	return false;

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
    controller()->attach(m_group);
    m_q921->ISDNLayer2::attach(q931());
    q931()->attach(m_q921);
    m_q921->multipleFrame(true,false);

    // Start thread
    if (!startThread(error))
	return false;

    return true;
}

bool SigIsdn::reload(NamedList& params)
{
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
    // *** Cleanup / Disable components / Remove links between them
    if (q931())
	q931()->cleanup();
    if (m_q921)
	m_q921->cleanup();
    if (m_iface) {
	m_iface->control(SignallingInterface::Disable);
	m_iface->attach(0);
    }
    // *** Remove from engine
    if (plugin.engine()) {
	plugin.engine()->remove(q931());
	plugin.engine()->remove(m_q921);
	plugin.engine()->remove(m_group);
	plugin.engine()->remove(m_iface);
    }
    // *** Release memory
    if (q931()) {
	q931()->destruct();
	m_controller = 0;
    }
    TelEngine::destruct(m_q921);
    TelEngine::destruct(m_group);
    TelEngine::destruct(m_iface);
    XDebug(&plugin,DebugAll,"SigIsdn('%s'). Released [%p]",name().c_str(),this);
}

DebugEnabler* SigIsdn::getDbgEnabler(int id)
{
    switch (id) {
	case 0: return m_iface;
	case 1: return m_group;
	case 2: return m_q921;
	case 3: return q931();
    }
    return 0;
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
	    "SigIsdnMonitor('%s'). Received event (%p,'%s') without call [%p]",
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
	    "SigIsdnMonitor('%s'). Received event (%p,'%s') with invalid user data (%p) [%p]",
	    name().c_str(),event,event->name(),mon->userdata(),this);
}

bool SigIsdnMonitor::masquerade(String& id, Message& msg)
{
    for (ObjList* o = m_monitors.skipNull(); o; o = o->skipNext()) {
	SigIsdnCallRecord* rec = static_cast<SigIsdnCallRecord*>(o->get());
	if (id == rec->id()) {
	    msg = msg.getValue("message");
	    msg.clearParam("message");
	    msg.userData(rec);
	}
    }
    return true;
}

bool SigIsdnMonitor::drop(String& id, Message& msg)
{
    const char* reason = msg.getValue("reason","dropped");
    if (id == name()) {
	m_monitorMutex.lock();
	ListIterator iter(m_monitors);
	GenObject* o = 0;
	for (; (o = iter.get()); ) {
	    CallEndpoint* c = static_cast<CallEndpoint*>(o);
	    c->disconnect(reason);
	}
	m_monitorMutex.unlock();
	return true;
    }
    for (ObjList* o = m_monitors.skipNull(); o; o = o->skipNext()) {
	SigIsdnCallRecord* rec = static_cast<SigIsdnCallRecord*>(o->get());
	if (id == rec->id()) {
	    rec->disconnect(reason);
	    return true;
	}
    }
    return false;
}

void SigIsdnMonitor::removeCall(SigIsdnCallRecord* call)
{
    Lock lock(m_monitorMutex);
    m_monitors.remove(call,false);
}

bool SigIsdnMonitor::create(NamedList& params, String& error)
{
    release();
    String compName;                     // Component name
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
    m_ifaceNet = buildInterface(params,params.getValue("sig-net"),compName,error);
    if (!m_ifaceNet)
	return false;
    buildName(compName,"D",false);
    m_ifaceCpe = buildInterface(params,params.getValue("sig-cpe"),compName,error);
    if (!m_ifaceCpe)
	return false;
	
    // Voice transfer: circuit groups, spans, circuits
    // Use the same span as the signalling channel if missing
    buildName(compName,"B",true);
    const char* device = params.getValue("voice-net",params.getValue("sig-net"));
    m_groupNet = buildCircuits(params,device,compName,error);
    if (!m_groupNet)
	return false;
    buildName(compName,"B",false);
    device = params.getValue("voice-cpe",params.getValue("sig-cpe"));
    m_groupCpe = buildCircuits(params,device,compName,error);
    if (!m_groupCpe)
	return false;
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
    if (!startThread(error))
	return false;

    if (debugAt(DebugInfo)) {
	String tmp;
	tmp << "\r\nChannel buffer: " << m_chanBuffer;
	tmp << "\r\nIdle value:     " << (int)m_idleValue;
	Debug(&plugin,DebugInfo,"SigIsdnMonitor('%s'). Initialized: [%p]%s",
	    name().c_str(),this,tmp.c_str());
    }
    return true;
}

bool SigIsdnMonitor::reload(NamedList& params)
{
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
    // *** Cleanup / Disable components / Remove links between them
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
    // *** Remove components from engine
    if (plugin.engine()) {
	plugin.engine()->remove(q931());
	plugin.engine()->remove(m_q921Net);
	plugin.engine()->remove(m_q921Cpe);
	plugin.engine()->remove(m_groupNet);
	plugin.engine()->remove(m_groupCpe);
	plugin.engine()->remove(m_ifaceNet);
	plugin.engine()->remove(m_ifaceCpe);
    }
    // *** Release memory
    if (q931()) {
	q931()->destruct();
	m_controller = 0;
    }
    TelEngine::destruct(m_q921Net);
    TelEngine::destruct(m_q921Cpe);
    TelEngine::destruct(m_groupNet);
    TelEngine::destruct(m_groupCpe);
    TelEngine::destruct(m_ifaceNet);
    TelEngine::destruct(m_ifaceCpe);
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
    DataSource*& src = first ? m_firstSrc : m_secondSrc;
    if (src) {
	src->clear();
	src->deref();
	src = 0;
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
    SignallingCircuit* cic = static_cast<SignallingCircuit*>(m_call->getObject("SignallingCircuitCaller"));
    if (!cic)
	cic = static_cast<SignallingCircuit*>(m_call->getObject("SignallingCircuitCalled"));
    if (cic)
	m_address << monitor->name() << "/" << cic->code();
    Debug(this->id(),DebugCall,"Initialized. Caller: '%s'. Called: '%s' [%p]",
	m_caller.c_str(),m_called.c_str(),this);
}

SigIsdnCallRecord::~SigIsdnCallRecord()
{
    close(0);
    if (m_monitor)
	m_monitor->removeCall(this);
    Message* m = message("chan.hangup",false);
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
	case SignallingEvent::NewCall:
	    Engine::enqueue(message("chan.startup"));
	    break;
	case SignallingEvent::Ringing:
	    if (m_status == "ringing")
		break;
	    m_status = "ringing";
	    Engine::enqueue(message("call.ringing"));
	    break;
	case SignallingEvent::Answer:
	    m_status = "answered";
	    Engine::enqueue(message("call.answered"));
	    break;
	case SignallingEvent::Accept:
	    break;
	default: ;
    }
    SignallingMessage* msg = event->message();
    bool chg = msg->params().getValue("circuit-change");
    String format = msg->params().getValue("format");
    if (format)
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
    m->addParam("status",m_status);
    if (m_address)
	m->addParam("address",m_address);
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
    TelEngine::destruct(m);
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
	bool fromCaller = event->message()->params().getBoolValue("fromcaller",false);
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
    DDebug(&plugin,DebugAll,"%s is running for link '%s' [%p]",
	name(),m_link->name().c_str(),this);
    SignallingEvent* event = 0;
    while (true) {
	if (!event)
	    Thread::yield(true);
	else if (Thread::check(true))
	    break;
	Time time;
	event = m_link->controller()->getEvent(time);
	if (event) {
	    XDebug(&plugin,DebugAll,"Link('%s'). Got event (%p,'%s',%p,%u)",
		m_link->name().c_str(),event,event->name(),event->call(),event->call()?event->call()->refcount():0);
	    m_link->handleEvent(event);
	    delete event;
	}
	// Check timeout if waiting to terminate
	if (m_timeout && time.msec() > m_timeout) {
	    DDebug(&plugin,DebugInfo,
		"SigLinkThread::run(). Link '%s' timed out [%p]",
		m_link->name().c_str(),this);
	    String name = m_link->name();
	    m_link->m_thread = 0;
	    m_link = 0;
	    plugin.clearLink(name);
	    break;
	}
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
