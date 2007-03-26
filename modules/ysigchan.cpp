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

using namespace TelEngine;
namespace { // anonymous

//TODO: Delete. It's used only for testing
static String s_out_CallControl;

class SigChannel;                        // Signalling channel
class SigDriver;                         // Signalling driver
class SigParams;                         // Named list containing creator data (pointers)
                                         // Used to pass parameters to objects that need to obtain some pointers
class SigCircuitGroup;                   // Used to create a signalling circuit group descendant to set the debug name
class SigLink;                           // Keep a signalling link
class SigIsdn;                           // ISDN (Q.931 over HDLC interface) call control
class SigIsdnMonitor;                    // ISDN (Q.931 over HDLC interface) call control monitor
class SigLinkThread;                     // Get events and check timeout for links that have a call controller

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

class SigLink : public RefObject
{
    friend class SigLinkThread;         // The thread must set m_thread to 0 on terminate
public:
    enum Type {
	IsdnPriNet,
	IsdnPriCpe,
	IsdnMonitor,
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
    // Build the signalling interface
    static SignallingInterface* buildInterface(const String& device,
	const String& debugName, String& error);
    SignallingCallControl* m_controller; // Call controller, if any
    bool m_init;                         // True if already initialized
    bool m_inband;                       // True to send in-band tones through this link
private:
    String m_name;                       // Link name
    int m_type;                          // Link type
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
    inline void buildCompName(String& dest, const char* name)
	{ dest = ""; dest << this->name() << '/' << name; }
private:
    ISDNQ921* m_q921;
    SignallingInterface* m_iface;
    SigCircuitGroup* m_group;
};

// Q.931 call control monitor over HDLC interface
class SigIsdnMonitor : public SigLink
{
public:
    inline SigIsdnMonitor(const char* name)
	: SigLink(name,IsdnMonitor),
	m_q921Net(0), m_q921Cpe(0), m_ifaceNet(0), m_ifaceCpe(0), m_groupNet(0), m_groupCpe(0)
	{}
    virtual ~SigIsdnMonitor()
	{ release(); }
protected:
    virtual bool create(NamedList& params);
    virtual bool reload(NamedList& params);
    virtual void release();
    inline ISDNQ931Monitor* q931()
	{ return static_cast<ISDNQ931Monitor*>(m_controller); }
    // Build component debug name
    inline void buildCompName(String& dest, const char* name, bool net)
	{ dest = ""; dest << this->name() << '/' << name << (net ? "/Net" : "/CPE"); }
private:
    ISDNQ921Pasive* m_q921Net;
    ISDNQ921Pasive* m_q921Cpe;
    SignallingInterface* m_ifaceNet;
    SignallingInterface* m_ifaceCpe;
    SigCircuitGroup* m_groupNet;
    SigCircuitGroup* m_groupCpe;
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
    sigMsg->params().addParam("format",msg.getValue("format"));
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

#define EVENT_NAME SignallingEvent::typeName(event->type())

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
		EVENT_NAME,this);
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
    Channel::callRejected(error,reason,msg);
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
    m_callMutex.lock();
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
    m_callMutex.unlock();
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
	    EVENT_NAME,tmp.c_str(),String::boolText(inband),this);
	Message* m = message("chan.dtmf");
	m->addParam("text",tmp);
	Engine::enqueue(m);
    }
}

void SigChannel::evProgress(SignallingEvent* event)
{
    DDebug(this,DebugCall,"Event: '%s' [%p]",EVENT_NAME,this);
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
	EVENT_NAME,m_reason.c_str(),this);
}

void SigChannel::evAccept(SignallingEvent* event)
{
    DDebug(this,DebugCall,"Event: '%s' [%p]",EVENT_NAME,this);
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
    DDebug(this,DebugCall,"Event: '%s' [%p]",EVENT_NAME,this);
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
    DDebug(this,DebugCall,"Event: '%s' [%p]",EVENT_NAME,this);
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

//TODO: ????????????
    if (event->call()->getObject("ISDNQ931CallMonitor")) {
	Debug(this,DebugStub,"ISDNQ931CallMonitor event");
	return;
    }

    if (event->type() == SignallingEvent::NewCall) {
	ch = new SigChannel(event);
	if (!ch->route(event)) {
	    //TODO: Send release with congestion
	    event->call()->userdata(0);
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
    ObjList* obj = m_links.skipNull();
    for (; obj; obj = obj->skipNext()) {
	SigLink* link = static_cast<SigLink*>(obj->get());
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
    ObjList* obj = m_links.skipNull();
    for (; obj; obj = obj->skipNext()) {
	SigLink* link = static_cast<SigLink*>(obj->get());
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
		break;
	    default:
		continue;
	}
	// Disable ?
	if (!sect->getBoolValue("enable",true)) {
	    clearLink(*sect,sect->getBoolValue("wait-call-end",false));
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
	{"isdn-monitor", IsdnMonitor},
	{0,0}
	};

SigLink::SigLink(const char* name, Type type)
    : m_controller(0),
    m_init(false),
    m_inband(false),
    m_name(name),
    m_type(type),
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

SignallingInterface* SigLink::buildInterface(const String& device, const String& debugName, String& error)
{
    NamedList ifaceDefs("sig");
    ifaceDefs.addParam("debugname",debugName);
    ifaceDefs.addParam("sig",device);
    SignallingInterface* iface = static_cast<SignallingInterface*>(SignallingFactory::build(ifaceDefs,&ifaceDefs));
    if (iface) {
	plugin.engine()->insert(iface);
	return iface;
    }
    error = "";
    error << "Failed to create signalling interface '" << device << "'";
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
	NamedList ifaceDefs("sig");
	buildCompName(compName,"D");
	m_iface = buildInterface(params.getValue("sig"),compName,error);
	if (!m_iface)
	    break;

	// Voice transfer: circuit group, spans, circuits
	buildCompName(compName,"B");
	m_group = new SigCircuitGroup(compName);
	String tmp = params.getValue("voice");
	// Use the same span as the signalling channel if missing
	if (tmp.null())
	    tmp = params.getValue("sig");
	ObjList* voice = tmp.split(',',false);
	if (!voice) {
	    error = "Missing or invalid 'voice' parameter";
	    break;
	}
	int start = 0;
	for (ObjList* o = voice->skipNull(); o; o = o->skipNext()) {
	    String* s = static_cast<String*>(o->get());
	    if (s->null())
		continue;
	    tmp = compName + "/" + *s;
	    SigParams spanParams("voice",m_group);
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
	if (!error.null())
	    break;
	plugin.engine()->insert(m_group);

	// Q921
	buildCompName(compName,"Q921");
	params.setParam("network",String::boolText(IsdnPriNet == type()));
	m_q921 = new ISDNQ921(params,compName);
	m_q921->setDebug(params.getBoolValue("print-layer2PDU",false),
	    params.getBoolValue("extended-debug",false));
	plugin.engine()->insert(m_q921);

	// Q931
	buildCompName(compName,"Q931");
	m_controller = new ISDNQ931(params,compName);
	q931()->setDebug(params.getBoolValue("print-layer3PDU",false),
	    params.getBoolValue("extended-debug",false));
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
    DDebug(&plugin,DebugAll,"SigIsdn('%s'). Reloading [%p]",
	name().c_str(),this);
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
    if (m_iface)
	m_iface->control(SignallingInterface::Disable);
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

	// Signalling interfaces
	buildCompName(compName,"D",true);
	m_ifaceNet = buildInterface(params.getValue("sig-net"),compName,error);
	if (!m_ifaceNet)
	    break;
	buildCompName(compName,"D",false);
	m_ifaceCpe = buildInterface(params.getValue("sig-cpe"),compName,error);
	if (!m_ifaceCpe)
	    break;
	
#if 0
    ISDNQ921Pasive* m_q921Net;
    ISDNQ921Pasive* m_q921Cpe;
    SigCircuitGroup* m_groupNet;
    SigCircuitGroup* m_groupCpe;




	// Voice transfer: circuit group, spans, circuits
	buildCompName(compName,"B");
	m_group = new SigCircuitGroup(compName);
	String tmp = params.getValue("voice");
	// Use the same span as the signalling channel if missing
	if (tmp.null())
	    tmp = params.getValue("sig");
	ObjList* voice = tmp.split(',',false);
	if (!voice) {
	    error = "Missing or invalid 'voice' parameter";
	    break;
	}
	int start = 0;
	for (ObjList* o = voice->skipNull(); o; o = o->skipNext()) {
	    String* s = static_cast<String*>(o->get());
	    if (s->null())
		continue;
	    tmp = compName + "/" + *s;
	    SigParams spanParams("voice",m_group);
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
	if (!error.null())
	    break;
	plugin.engine()->insert(m_group);

	// Q921
	buildCompName(compName,"Q921");
	if (buildQ921Pasive

	params.setParam("network",String::boolText(IsdnPriNet == type()));
	m_q921 = new ISDNQ921(params,compName);
	m_q921->setDebug(params.getBoolValue("print-layer2PDU",false),
	    params.getBoolValue("extended-debug",false));
	plugin.engine()->insert(m_q921);

	// Q931
	compName = "";
	compName << this->name() << '/' << "Q931";
	m_controller = new ISDNQ931(params,compName);
	q931()->setDebug(params.getBoolValue("print-layer3PDU",false),
	    params.getBoolValue("extended-debug",false));
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
#endif

	break;
    }
    if (error.null())
	return true;
    Debug(&plugin,DebugNote,"SigIsdnMonitor('%s'). Create failure. %s [%p]",
	name().c_str(),error.c_str(),this);
    return false;
}

bool SigIsdnMonitor::reload(NamedList& params)
{
    if (!m_init)
	return false;
    DDebug(&plugin,DebugAll,"SigIsdnMonitor('%s'). Reloading [%p]",
	name().c_str(),this);
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
    // *** Cleanup / Disable components
    if (q931())
	q931()->cleanup();
    if (m_q921Net)
	m_q921Net->cleanup();
    if (m_q921Cpe)
	m_q921Cpe->cleanup();
    if (m_ifaceNet)
	m_ifaceNet->control(SignallingInterface::Disable);
    if (m_ifaceCpe)
	m_ifaceCpe->control(SignallingInterface::Disable);
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
	    plugin.handleEvent(event);
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
