/**
 * ysigchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Signalling Channel
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

#include <string.h>
#include <stdio.h>

using namespace TelEngine;
namespace { // anonymous

class SigChannel;                        // Signalling channel
class SigDriver;                         // Signalling driver
class SigTopmost;                        // Keep a topmost non-trunk component
class OnDemand;                          // On demand component
class SigCircuitGroup;                   // Used to create a signalling circuit group descendant to set the debug name
class SigTrunk;                          // Keep a signalling trunk
class SigSS7Isup;                        // SS7 ISDN User Part call controller
class SigIsdn;                           // ISDN (Q.931 over HDLC interface) call control
class SigIsdnMonitor;                    // ISDN (Q.931 over HDLC interface) call control monitor
class SigConsumerMux;                    // Consumer used to push data to SigSourceMux
class SigSourceMux;                      // A data source multiplexer with 2 channels
class SigIsdnCallRecord;                 // Record an ISDN call monitor
class SigTrunkThread;                    // Get events and check timeout for trunks that have a call controller
class IsupDecodeHandler;                 // Handler for "isup.decode" message
class IsupEncodeHandler;                 // Handler for "isup.encode" message
class SigNotifier;                       // Class for handling received notifications
class SigSS7Tcap;                        // SS7 TCAP - Transaction Capabilities Application Part
class SigTCAPUser;                       // Default TCAP user

// The signalling channel
class SigChannel : public Channel
{
    YCLASS(SigChannel,Channel)
public:
    // Incoming
    SigChannel(SignallingEvent* event);
    // Outgoing
    SigChannel(const char* caller, const char* called);
    virtual ~SigChannel();
    virtual void destroyed();
    bool startRouter();
    bool startCall(Message& msg, String& trunks);
    inline SignallingCall* call() const
        { return m_call; }
    inline SigTrunk* trunk() const
        { return m_trunk; }
    inline bool hungup() const
	{ return m_hungup; }
    // Overloaded methods
    virtual bool msgProgress(Message& msg);
    virtual bool msgRinging(Message& msg);
    virtual bool msgAnswered(Message& msg);
    virtual bool msgTone(Message& msg, const char* tone);
    virtual bool msgText(Message& msg, const char* text);
    virtual bool msgUpdate(Message& msg);
    virtual bool msgDrop(Message& msg, const char* reason);
    virtual bool msgTransfer(Message& msg);
    virtual bool callPrerouted(Message& msg, bool handled);
    virtual bool callRouted(Message& msg);
    virtual void callAccept(Message& msg);
    virtual void callRejected(const char* error, const char* reason = 0,
	const Message* msg = 0);
    virtual void connected(const char *reason);
    virtual void disconnected(bool final, const char* reason);
    bool disconnect()
	{ return Channel::disconnect(m_reason,parameters()); }
    void handleEvent(SignallingEvent* event);
    void hangup(const char* reason = 0, SignallingEvent* event = 0, const NamedList* extra = 0);
    // Notifier message dispatched handler
    virtual void dispatched(const Message& msg, bool handled);
protected:
    void endDisconnect(const Message& params, bool handled);
private:
    bool startCall(Message& msg, SigTrunk* trunk);
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
    void evCircuit(SignallingEvent* event);
    void evGeneric(SignallingEvent* event, const char* operation);
    // Handle RTP forward from a message.
    // Return a circuit event to be sent or 0 if not handled
    SignallingCircuitEvent* handleRtp(Message& msg);
    // Set RTP data from circuit to message
    bool addRtp(Message& msg, bool possible = false);
    // Update media type, may have switched to fax
    bool updateMedia(Message& msg, const String& operation);
    // Initiate change with a list of possible modes
    bool initiateMedia(Message& msg, SignallingCircuit* cic, String& modes);
    // Initiate change for a single mode
    bool initiateMedia(Message& msg, SignallingCircuit* cic, const String& mode, bool mandatory);
    // Notification from peer channel
    bool notifyMedia(Message& msg);
    // Update circuit and format in source, optionally in consumer too
    void updateCircuitFormat(SignallingEvent* event, bool consumer);
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
    // Retrieve the ISUP call controller from trunk
    SS7ISUP* isupController() const;
    // Process parameter compatibility data from a message
    // Return true if the call was terminated
    bool processCompat(const NamedList& list);
    // Send a tone through signalling call
    bool sendSigTone(Message& msg, const char* tone);
    // Release postponed call accepted event. Send it if requested
    void releaseCallAccepted(bool send = false);
private:
    String m_caller;
    String m_called;
    SignallingCall* m_call;              // The signalling call this channel is using
    SigTrunk* m_trunk;                   // The trunk owning the signalling call
    bool m_hungup;                       // Hang up flag
    String m_reason;                     // Hangup reason
    bool m_inband;                       // True to try to send in-band tones
    bool m_ringback;                     // Always provide ringback media
    bool m_rtpForward;                   // Forward RTP
    bool m_sdpForward;                   // Forward SDP (only of rtp forward is enabled)
    String m_nextModes;                  // Next modes to attempt to set
    Message* m_route;                    // Prepared call.preroute message
    ObjList m_chanDtmf;                  // Postponed (received while routing) chan.dtmf messages
    SignallingEvent* m_callAccdEvent;    // Postponed call accepted event to be sent to remote
};

class SigDriver : public Driver
{
    friend class SigTopmost;
    friend class SigTrunk;               // Needded for appendTrunk() / removeTrunk()
    friend class OnDemand;               // Needed for Append OnDemand
    friend class SigTrunkThread;         // Needded for clearTrunk()
public:
    enum Operations {
	Create      = 0x01,
	Configure   = 0x02,
	Unknown     = 0x03,
    };

    enum PrivateRelays {
	TcapRequest = Private,
    };
    SigDriver();
    ~SigDriver();
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest);
    inline SignallingEngine* engine() const
	{ return m_engine; }
    void handleEvent(SignallingEvent* event);
    bool received(Message& msg, int id);
    // Find a trunk by name
    // If callCtrl is true, match only trunks with call controller
    SigTrunk* findTrunk(const char* name, bool callCtrl);
    // Find a trunk by call controller
    SigTrunk* findTrunk(const SignallingCallControl* ctrl);
    // Disconnect channels. If trunk is not 0, disconnect only channels belonging to that trunk
    void disconnectChannels(SigTrunk* trunk = 0);
    // Copy incoming message parameters to another list of parameters
    // The pointers of NamedPointer parameters are 'stolen' from 'sig' when copying
    // If 'params' is not 0, the contained parameters will not be prefixed with
    //  event call controller's prefix
    void copySigMsgParams(NamedList& dest, SignallingEvent* event,
	const String* params = 0);
    // Reinitialize
    bool reInitialize(NamedList& params);
    // Build an configuration file from param list
    void getCfg(NamedList& params, Configuration& cfg);
    // Check if the cfg sections exists in conf file
    bool checkSections(Configuration* cfg);
    // Append the new sections in the conf file
    // Save the configuration
    void saveConf(Configuration* cfg);
    // Init configuration sections
    bool initSection(NamedList* sect);
    // Copy outgoing message parameters
    void copySigMsgParams(SignallingEvent* event, const NamedList& params, const char* prePrefix = "o");
    // Load a trunk's section from data file. Trunk name must be set in list name
    // Return true if found
    bool loadTrunkData(NamedList& list);
    // Save a trunk's section to data file
    bool saveTrunkData(const NamedList& list);
    static const TokenDict s_operations[];
    // Append an onDemand component
    bool appendOnDemand(SignallingComponent* cmp, int type);
    // Install a relay
    inline bool requestRelay(int id, const char* name, unsigned priority = 100)
	{ return installRelay(id,name,priority); }
private:
    // Handle command complete requests
    virtual bool commandComplete(Message& msg, const String& partLine,
	const String& partWord);
    // Custom command handler
    virtual bool commandExecute(String& retVal, const String& line);
    // Help message handler
    bool commandHelp(String& retVal, const String& line);
    // Delete the given trunk if found
    // Clear trunk list if name is 0
    // Clear all stacks without waiting for call termination if name is 0
    void clearTrunk(const char* name = 0, bool waitCallEnd = false, unsigned int howLong = 0);
    // Append a trunk to the list. Duplicate names are not allowed
    bool appendTrunk(SigTrunk* trunk);
    // Remove a trunk from list without deleting it
    void removeTrunk(SigTrunk* trunk);
    // Create or initialize a trunk
    bool initTrunk(NamedList& sect, int type);
    // Get the status of a trunk
    void status(SigTrunk* trunk, String& retVal, const String& target, bool details);
    // Append a topmost non-trunk component
    bool appendTopmost(SigTopmost* topmost);
    // Remove a topmost component without deleting it
    void removeTopmost(SigTopmost* topmost);
    // Create or initialize a topmost component
    bool initTopmost(NamedList& sect, int type);
    // Get the status of a topmost component
    void status(SigTopmost* topmost, String& retVal);
    // Initialize an on demand component
    bool initOnDemand(NamedList& sect, int type);
    // Get the status on an on demand component
    void status(OnDemand* cmp, String& retVal);
    // Destroy OnDemand object that have no reference
    void checkOnDemand();

    SignallingEngine* m_engine;          // The signalling engine
    String m_dataFile;                   // Trunks data file (protected by m_trunksMutex)
    ObjList m_trunks;                    // Trunk list
    Mutex m_trunksMutex;                 // Lock trunk list operations
    ObjList m_topmost;                   // Topmost non-trunk list
    Mutex m_topmostMutex;                // Lock topmost non-trunk list operations
    ObjList m_onDemand;                  // List of objects created on demand
    Mutex m_onDemandMutex;               // Lock the list o objects created on demand
    String m_statusCmd;                  // Prefix for status commands
};

// Topmost element holder - call controller, link or application server
class TopMost : public RefObject
{
public:
    inline const String& name() const
	{ return m_name; }
    virtual const String& toString() const
	{ return m_name; }
    virtual bool initialize(NamedList& params) = 0;
protected:
    inline TopMost(const char* name)
	: m_name(name)
	{ }
private:
    String m_name;                       // Element name
};

class OnDemand : public TopMost
{
public:
    inline OnDemand(const char* name)
	: TopMost(name) { }
    virtual void status(String& retVal)
	{ }
    virtual bool initialize(NamedList& params)
	{ return true; }
    virtual bool reInitialize(const NamedList& params) = 0;
    virtual bool isAlive() = 0;
    virtual SignallingComponent* get() = 0;
};

class SigTopmost : public TopMost
{
public:
    SigTopmost(const char* name);
    // Return the status of this component
    virtual inline void status(String& retVal)
	{ }
    // Return the status of a signalling interface
    virtual inline void ifStatus(String& status)
	{ }
    // Return the status of a link
    virtual inline void linkStatus(String& status)
	{ }
protected:
    virtual void destroyed();
};

// Signalling link set (SS7 Layer 3)
class SigLinkSet : public SigTopmost
{
public:
    inline SigLinkSet(const char* name)
	: SigTopmost(name), m_linkset(0)
	{ }
    // Initialize (create or reload) the linkset
    // Return false on failure
    virtual bool initialize(NamedList& params);
    // Return the status of this component
    virtual void status(String& retVal);
    virtual void ifStatus(String& status);
    virtual void linkStatus(String& status);
protected:
    virtual void destroyed();
private:
    SS7Layer3* m_linkset;
};

class SigSCCPUser : public SigTopmost
{
    YCLASS(SigSCCPUser,SigTopmost)
public:
    inline SigSCCPUser(const char* name)
	: SigTopmost(name), m_user(0)
	{ }
    virtual ~SigSCCPUser();
    virtual bool initialize(NamedList& params);
private:
    SCCPUser* m_user;
};

class SigSccpGtt : public SigTopmost
{
    YCLASS(SigSccpGtt,SigTopmost)
public:
    inline SigSccpGtt(const char* name)
	: SigTopmost(name), m_gtt(0)
	{ }
    virtual ~SigSccpGtt();
    virtual bool initialize(NamedList& params);
private:
    GTT* m_gtt;
};

// MTP Traffic Testing
class SigTesting : public SigTopmost
{
public:
    inline SigTesting(const char* name)
	: SigTopmost(name), m_testing(0)
	{ }
    // Initialize (create or reload) the linkset
    // Return false on failure
    virtual bool initialize(NamedList& params);
protected:
    virtual void destroyed();
private:
    SS7Testing* m_testing;
};

class SigSS7Sccp : public OnDemand
{
public:
    inline SigSS7Sccp(SS7SCCP* sccp)
	: OnDemand(sccp? sccp->toString() : ""), m_sccp(sccp) { m_sccp->ref(); }
    virtual ~SigSS7Sccp();
    virtual bool initialize(NamedList& params)
	{ return true; }
    virtual void status(String& retVal);
    virtual bool isAlive();
    virtual SignallingComponent* get()
	{ return m_sccp; }
    virtual bool reInitialize(const NamedList& params)
	{ return m_sccp ? m_sccp->initialize(&params) : false; }
private:
    SS7SCCP* m_sccp;
};

// Signalling trunk (Call Controller)
class SigTrunk : public TopMost
{
    friend class SigTrunkThread;         // The thread must set m_thread to 0 on terminate
public:
    enum Mask {
	MaskSS7  = 0x01,
	MaskIsdn = 0x02,
	MaskMon  = 0x04,
	MaskNet  = 0x10,
	MaskCpe  = 0x20,
	MaskPri  = 0x40,
	MaskBri  = 0x80,
	MaskIsup = 0x10,
	MaskBicc = 0x20,
    };
    enum Type {
	Unknown    = 0,
	SS7Isup    = MaskSS7 | MaskIsup,
	SS7Bicc    = MaskSS7 | MaskBicc,
	IsdnPriNet = MaskIsdn | MaskNet | MaskPri,
	IsdnBriNet = MaskIsdn | MaskNet | MaskBri,
	IsdnPriCpe = MaskIsdn | MaskCpe | MaskPri,
	IsdnBriCpe = MaskIsdn | MaskCpe | MaskBri,
	IsdnPriMon = MaskIsdn | MaskMon,
    };
    inline Type type() const
	{ return m_type; }
    inline SignallingCallControl* controller() const
	{ return m_controller; }
    inline bool inband() const
	{ return m_inband; }
    inline bool ringback() const
	{ return m_ringback; }
    // Set exiting flag for call controller and timeout for the thread
    void setExiting(unsigned int msec);
    // Initialize (create or reload) the trunk. Process the debuglayer parameter.
    // Fix some type depending parameters
    // Return false on failure
    virtual bool initialize(NamedList& params);
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
    // Clear channels with calls belonging to this trunk. Cancel thread if any. Call release
    void cleanup();
    // Type names
    static const TokenDict s_type[];
    virtual inline void ifStatus(String& status)
	{ }
    virtual inline void linkStatus(String& status)
	{ }
protected:
    // Cancel thread. Cleanup. Remove from plugin list
    virtual void destroyed();
    // Set trunk name and type. Append to plugin list
    SigTrunk(const char* name, Type type);
    // Start worker thread. Set error on failure
    bool startThread(String& error, unsigned long sleepUsec, int floodLimit);
    // Create/Reload/Release data
    virtual bool create(NamedList& params, String& error) { return false; }
    virtual bool reload(NamedList& params) { return false; }
    virtual void release() { }
    // Build a signalling circuit group and insert it in the engine
    static SignallingCircuitGroup* buildCircuits(NamedList& params, const String& device,
	const String& debugName, String& error);
    // Build component debug name
    inline void buildName(String& dest, const char* name)
	{ dest = ""; dest << this->name() << '/' << name; }
    SignallingCallControl* m_controller; // Call controller, if any
    bool m_init;                         // True if already initialized
    bool m_inband;                       // True to send in-band tones through this trunk
    bool m_ringback;                     // Always provide ringback media
private:
    Type m_type;                         // Trunk type
    SigTrunkThread* m_thread;            // Event thread for call controller
};

// SS7 ISDN User Part call controller
class SigSS7Isup : public SigTrunk
{
public:
    SigSS7Isup(const char* name, bool bicc);
    virtual ~SigSS7Isup();
protected:
    virtual bool create(NamedList& params, String& error);
    virtual bool reload(NamedList& params);
    virtual void release();
    // Handle events received from call controller
    // Process Verify event. Calls the driver's handleEvent() method for other events
    virtual void handleEvent(SignallingEvent* event);
    // Save circuits state
    // Return true if changed
    bool verifyController(const NamedList* params, bool save = true);
    inline SS7ISUP* isup()
	{ return static_cast<SS7ISUP*>(m_controller); }
    bool m_bicc;
};

// Q.931 call control over HDLC interface
class SigIsdn : public SigTrunk
{
public:
    SigIsdn(const char* name, Type type);
    virtual ~SigIsdn();
    virtual void ifStatus(String& status);
    virtual void linkStatus(String& status);
protected:
    virtual bool create(NamedList& params, String& error);
    virtual bool reload(NamedList& params);
    virtual void release();
    inline ISDNQ931* q931()
	{ return static_cast<ISDNQ931*>(m_controller); }
};

// Q.931 call control monitor over HDLC interface
class SigIsdnMonitor : public SigTrunk, public Mutex
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
    // Remove a call and it's call monitor
    void removeCall(SigIsdnCallRecord* call);
protected:
    virtual bool create(NamedList& params, String& error);
    virtual bool reload(NamedList& params);
    virtual void release();
    inline ISDNQ931Monitor* q931()
	{ return static_cast<ISDNQ931Monitor*>(m_controller); }
private:
    ObjList m_monitors;                  // Monitor list
    unsigned int m_id;                   // ID generator
    unsigned int m_chanBuffer;           // The buffer length of one channel of a data source multiplexer
    unsigned char m_idleValue;           // Idle value for source multiplexer to fill when no data
};

class SigSS7Tcap : public SigTopmost
{
public:
    inline SigSS7Tcap(const char* name)
    : SigTopmost(name), m_tcap(0)
    { }
    // Initialize (create or reload) the TCAP component
    // Return false on failure
    virtual bool initialize(NamedList& params);
    // Return the status of this component
    virtual void status(String& retVal);
protected:
    virtual void destroyed();
private:
    SS7TCAP* m_tcap;
};

class MsgTCAPUser : public TCAPUser
{
public:
    inline MsgTCAPUser(const char* name)
	: TCAPUser(name)
        { }
    virtual ~MsgTCAPUser();
    // Initialize the TCAP user
    // Return false on failure
    virtual bool initialize(NamedList& params);
    virtual bool tcapRequest(NamedList& params);
    virtual bool tcapIndication(NamedList& params);
protected:
    SS7TCAP* tcapAttach();
    String m_tcapName;
};


class SigTCAPUser : public SigTopmost
{
public:
    inline SigTCAPUser(const char* name)
	: SigTopmost(name), m_user(0)
    { }
    virtual ~SigTCAPUser();
    // Initialize the TCAP user
    // Return false on failure
    virtual bool initialize(NamedList& params);
    virtual bool tcapRequest(NamedList& params);
    // Return the status of this component
    virtual void status(String& retVal);
protected:
    MsgTCAPUser* m_user;
    virtual void destroyed();
};

// Factory for locally configured components
class SigFactory : public SignallingFactory
{
public:
    enum SigMask {
	// Mask for private bits
	SigPrivate  = 0x00ff,
	// Created always from config file section
	SigTopMost  = 0x0100,
	// Created on demand from config file if section exists
	SigOnDemand = 0x0200,
	// Created from config file, use defaults if no section
	SigDefaults = 0x0400,
	// Component is a signalling trunk
	SigIsTrunk  = 0x0800,
    };
    enum SigType {
	Unknown           = 0,
	SigISDNLayer2     = 0x01 | SigDefaults,
	SigISDNLayer3     = 0x02 | SigDefaults,
	SigISDNIUA        = 0x03 | SigDefaults,
	SigSS7Layer2      = 0x11 | SigOnDemand,
	SigSS7Layer3      = 0x12 | SigTopMost,
	SigSS7Router      = 0x13 | SigDefaults,
	SigSS7Management  = 0x14 | SigDefaults,
	SigSS7Testing     = 0x16 | SigTopMost,
	SigSS7M2PA        = 0x21 | SigOnDemand,
	SigSS7M2UA        = 0x22 | SigOnDemand,
	SigSS7M3UA        = 0x23 | SigTopMost,
	SigSS7TCAP        = 0x24 | SigTopMost,
	SigSS7TCAPANSI    = 0x25 | SigTopMost,
	SigSS7TCAPITU     = 0x26 | SigTopMost,
	SigTCAPUser       = 0x27 | SigTopMost,
	SigISDNIUAClient  = 0x31 | SigDefaults,
	SigISDNIUAGateway = 0x32 | SigTopMost,
	SigSS7M2UAClient  = 0x33 | SigDefaults,
	SigSS7M2UAGateway = 0x34 | SigTopMost,
	SigSS7M3UAClient  = 0x35 | SigDefaults,
	SigSS7M3UAGateway = 0x36 | SigTopMost,
	SigSS7SCCP        = 0x37 | SigOnDemand,
	SigSCCP           = 0x38 | SigOnDemand,
	SigSCCPUserDummy  = 0x39 | SigTopMost,
	SigSccpGtt        = 0x3a | SigTopMost,
	SigSCCPManagement = 0x3b | SigDefaults,
	SigSS7ItuSccpManagement  = 0x3c | SigDefaults,
	SigSS7AnsiSccpManagement = 0x3d | SigDefaults,
	SigSS7Isup  = SigTrunk::SS7Isup    | SigIsTrunk | SigTopMost,
	SigSS7Bicc  = SigTrunk::SS7Bicc    | SigIsTrunk | SigTopMost,
	SigISDNPN   = SigTrunk::IsdnPriNet | SigIsTrunk | SigTopMost,
	SigISDNBN   = SigTrunk::IsdnBriNet | SigIsTrunk | SigTopMost,
	SigISDNPC   = SigTrunk::IsdnPriCpe | SigIsTrunk | SigTopMost,
	SigISDNBC   = SigTrunk::IsdnBriCpe | SigIsTrunk | SigTopMost,
	SigISDNMon  = SigTrunk::IsdnPriMon | SigIsTrunk | SigTopMost,
    };
    inline SigFactory()
	: SignallingFactory(true)
	{ }
    static const TokenDict s_compNames[];
    static const TokenDict s_compClass[];
protected:
    virtual SignallingComponent* create(const String& type, NamedList& name);
};

// Consumer used to push data to SigSourceMux
class SigConsumerMux : public DataConsumer
{
    friend class SigSourceMux;
public:
    virtual ~SigConsumerMux()
	{ }
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags);
protected:
    inline SigConsumerMux(SigSourceMux* owner, bool first, const char* format)
	: DataConsumer(format), m_owner(owner), m_first(first)
	{ }
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
    bool m_netInit;                      // The caller is from the network (true) or user (false) side of the trunk
    String m_reason;                     // Termination reason
    String m_status;                     // Call status
    SigIsdnMonitor* m_monitor;           // The owner of this recorder
    ISDNQ931CallMonitor* m_call;         // The call monitor
};

// Get events from call controller. Check timeouts
class SigTrunkThread : public Thread
{
    friend class SigTrunk;                // SigTrunk will set m_timeout when needded
public:
    inline SigTrunkThread(SigTrunk* trunk, unsigned long sleepUsec, int floodLimit)
	: Thread("YSIG Trunk"),
	  m_trunk(trunk), m_timeout(0), m_sleepUsec(sleepUsec),
	  m_floodEvents(floodLimit)
	{ }
    virtual ~SigTrunkThread();
    virtual void run();
private:
    SigTrunk* m_trunk;
    u_int64_t m_timeout;
    unsigned long m_sleepUsec;
    int m_floodEvents;
};


// isup.decode handler (decode an isup message)
class IsupDecodeHandler : public MessageHandler
{
public:
    // Init the ISUP component and the mesage name
    IsupDecodeHandler(bool decode = true);
    virtual void destruct();
    virtual bool received(Message& msg);
protected:
    // Get point code type (protocol version) from message
    // Return SS7PointCode::Other if unknown
    SS7PointCode::Type getPCType(Message& msg, const String& prefix);
    SS7ISUP* m_isup;
};

// isup.encode handler (encode an isup message)
class IsupEncodeHandler : public IsupDecodeHandler
{
public:
    // Init the ISUP component
    inline IsupEncodeHandler()
	: IsupDecodeHandler(false)
	{ }
    virtual bool received(Message& msg);
};

class SigNotifier : public SignallingNotifier
{
public:
    inline ~SigNotifier()
	{ }
    virtual void notify(NamedList& params);
    virtual void cleanup();
};

// Implementation for a SCCP Global Title Translator
class GTTranslator : public GTT
{
    YCLASS(GTTranslator,GTT)
public:
    GTTranslator(const NamedList& params);
    virtual ~GTTranslator();
    virtual NamedList* routeGT(const NamedList& gt, const String& prefix,
	    const String& nextPrefix);
    virtual bool initialize(const NamedList* config);
    virtual void updateTables(const NamedList& params);
};

class SCCPUserDummy : public SCCPUser
{
    YCLASS(SCCPUserDummy,SCCPUser)
public:
    SCCPUserDummy(const NamedList& params);

    virtual ~SCCPUserDummy();
    virtual HandledMSU receivedData(DataBlock& data, NamedList& params);
    virtual HandledMSU notifyData(DataBlock& data, NamedList& params);
    virtual bool managementNotify(SCCP::Type type, NamedList &params);
protected:
    int m_ssn;
};


static SigDriver plugin;
static SigFactory factory;
static SigNotifier s_notifier;
static Configuration s_cfg;
static const String s_noPrefixParams = "format,earlymedia";
static int s_floodEvents = 20;

static const char s_miniHelp[] = "sigdump component [filename]";
static const char s_fullHelp[] = "Command to dump signalling data to a file";

const TokenDict SigFactory::s_compNames[] = {
    { "isdn-q921",        SigISDNLayer2 },
    { "isdn-q931",        SigISDNLayer3 },
    { "isdn-iua",         SigISDNIUA },
    { "isdn-iua-client",  SigISDNIUAClient },
    { "isdn-iua-gateway", SigISDNIUAGateway },
    { "ss7-router",       SigSS7Router },
    { "ss7-mtp2",         SigSS7Layer2 },
    { "ss7-mtp3",         SigSS7Layer3 },
    { "ss7-snm",          SigSS7Management },
    { "ss7-test",         SigSS7Testing },
    { "ss7-m2pa",         SigSS7M2PA },
    { "ss7-m2ua",         SigSS7M2UA },
    { "ss7-m2ua-client",  SigSS7M2UAClient },
    { "ss7-m2ua-gateway", SigSS7M2UAGateway },
    { "ss7-m3ua",         SigSS7M3UA },
    { "ss7-m3ua-client",  SigSS7M3UAClient },
    { "ss7-m3ua-gateway", SigSS7M3UAGateway },
    { "ss7-isup",         SigSS7Isup },
    { "ss7-bicc",         SigSS7Bicc },
    { "ss7-sccp",         SigSS7SCCP },
    { "ss7-sccpu-dummy",  SigSCCPUserDummy },
    { "ss7-sccp-itu-mgm", SigSS7ItuSccpManagement },
    { "ss7-sccp-ansi-mgm",SigSS7AnsiSccpManagement },
    { "ss7-gtt",          SigSccpGtt },
    { "ss7-tcap-ansi",    SigSS7TCAPANSI },
    { "ss7-tcap-itu",     SigSS7TCAPITU },
    { "ss7-tcap-user",    SigTCAPUser },
    { "isdn-pri-net",     SigISDNPN },
    { "isdn-bri-net",     SigISDNBN },
    { "isdn-pri-cpe",     SigISDNPC },
    { "isdn-bri-cpe",     SigISDNBC },
    { "isdn-pri-mon",     SigISDNMon },
    { 0, 0 }
};

const TokenDict SigDriver::s_operations[] = {
    { "create",       Create },
    { "configure",    Configure },
    { "unknown",      Unknown },
    { 0, 0 }
};

const TokenDict SigFactory::s_compClass[] = {
#define MAKE_CLASS(x) { #x, Sig##x }
    MAKE_CLASS(ISDNLayer2),
    MAKE_CLASS(ISDNIUA),
    MAKE_CLASS(ISDNIUAClient),
    MAKE_CLASS(SS7Router),
    MAKE_CLASS(SS7Layer2),
    MAKE_CLASS(SS7Layer3),
    MAKE_CLASS(SS7Management),
    MAKE_CLASS(SS7Testing),
    MAKE_CLASS(SS7M2PA),
    MAKE_CLASS(SS7M2UA),
    MAKE_CLASS(SS7M2UAClient),
    MAKE_CLASS(SS7M3UA),
    MAKE_CLASS(SS7M3UAClient),
    MAKE_CLASS(SS7Isup),
    MAKE_CLASS(SS7Bicc),
    MAKE_CLASS(SS7SCCP),
    MAKE_CLASS(SCCP),
    MAKE_CLASS(SCCPManagement),
    MAKE_CLASS(SS7ItuSccpManagement),
    MAKE_CLASS(SS7AnsiSccpManagement),
    MAKE_CLASS(SCCPUserDummy),
    MAKE_CLASS(SS7TCAP),
    MAKE_CLASS(SS7TCAPANSI),
    MAKE_CLASS(SS7TCAPITU),
    MAKE_CLASS(TCAPUser),
    MAKE_CLASS(ISDNPN),
    MAKE_CLASS(ISDNBN),
    MAKE_CLASS(ISDNPC),
    MAKE_CLASS(ISDNBC),
    MAKE_CLASS(ISDNMon),
#undef MAKE_CLASS
    { 0, 0 }
};


class SCCPHandler : public MessageHandler
{
public:
    inline SCCPHandler()
	: MessageHandler("sccp.generate",100,plugin.name())
	{ }
    virtual bool received(Message& msg);
};


// Construct a locally configured component
SignallingComponent* SigFactory::create(const String& type, NamedList& name)
{
    const NamedList* config = s_cfg.getSection(name);
    DDebug(&plugin,DebugAll,"SigFactory::create('%s','%s') config=%p",
	type.c_str(),name.c_str(),config);
    int compType = type.toInteger(s_compClass,-1);
    if (compType < 0)
	return 0;
    TempObjectCounter cnt(plugin.objectsCounter());
    if (!config) {
	NamedList sec(name);
	sec.copySubParams(name,name + ".");
	if (sec.count())
	    config = &name;
	else {
	    if ((compType & SigDefaults) != 0)
		config = &name;
	    else
		return 0;
	}
    } else if (name.getBoolValue(YSTRING("local-config"),false))
	name.copyParams(*config);

    String* ty = config->getParam("type");
    switch (compType) {
	case SigISDNLayer2:
	    if (ty && *ty == "isdn-iua")
		return new ISDNIUA(*config);
	    if (name.getBoolValue("primary",true)) {
		NamedList cfg(*config);
		cfg.setParam("type",lookup(compType,s_compNames));
		return new ISDNQ921(cfg,name);
	    }
	    return new ISDNQ921Management(*config,name,name.getBoolValue("network",true));
	case SigISDNLayer3:
	    return new ISDNQ931(*config,name);
	case SigISDNIUA:
	    return new ISDNIUA(*config);
	case SigISDNIUAClient:
	    return new ISDNIUAClient(*config);
	case SigSS7Layer2:
	    if (ty) {
		if (*ty == "ss7-m2pa")
		    return new SS7M2PA(*config);
		if (*ty == "ss7-m2ua")
		    return new SS7M2UA(*config);
	    }
	    return new SS7MTP2(*config);
	case SigSS7M2PA:
	    return new SS7M2PA(*config);
	case SigSS7M2UA:
	    return new SS7M2UA(*config);
	case SigSS7M2UAClient:
	    return new SS7M2UAClient(*config);
	case SigSS7Layer3:
	    return new SS7MTP3(*config);
	case SigSS7Router:
	    return new SS7Router(*config);
	case SigSS7Management:
	    return new SS7Management(*config);
	case SigSS7Testing:
	    return new SS7Testing(*config);
	case SigSCCP:
	    if (ty && *ty != YSTRING("ss7-sccp"))
		return 0;
	case SigSS7SCCP:
	{
	    SS7SCCP* sccp = new SS7SCCP(*config);
	    plugin.appendOnDemand(sccp, SigSS7SCCP);
	    return sccp;
	}
	case SigSCCPManagement:
	    if (!ty)
		return 0;
	    if (*ty == "ss7-sccp-itu-mgm")
		return new SS7ItuSccpManagement(*config);
	    else if (*ty == "ss7-sccp-ansi-mgm")
		return new SS7AnsiSccpManagement(*config);
	    else
		return 0;
	case SigSS7ItuSccpManagement:
	    return new SS7ItuSccpManagement(*config);
	case SigSS7AnsiSccpManagement:
	    return new SS7AnsiSccpManagement(*config);
	case SigSS7TCAP:
	    if (ty) {
		if (*ty == "ss7-tcap-ansi")
		    return new SS7TCAPANSI(*config);
		if (*ty == "ss7-tcap-itu")
		    return new SS7TCAPITU(*config);
	    }
	    return 0;
    }
    return 0;
}

/**
 * SigChannel
 */
// Construct an incoming channel
SigChannel::SigChannel(SignallingEvent* event)
    : Channel(&plugin,0,false),
    m_call(event->call()),
    m_trunk(0),
    m_hungup(true),
    m_inband(false),
    m_ringback(false),
    m_rtpForward(false),
    m_sdpForward(false),
    m_route(0),
    m_callAccdEvent(0)
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
    m_trunk = plugin.findTrunk(m_call->controller());
    if (m_trunk) {
	m_inband = m_trunk->inband();
	m_ringback = m_trunk->ringback();
    }
    // Startup
    m_hungup = false;
    setState(0);
    SignallingCircuit* cic = getCircuit();
    if (cic) {
	if (m_trunk)
	    m_address << m_trunk->name() << "/" << cic->code();
	m_rtpForward = cic->getBoolParam("rtp_forward");
	m_sdpForward = cic->getBoolParam("sdp_forward");
    }
    Message* m = message("chan.startup");
    m->setParam("direction",status());
    m->addParam("caller",m_caller);
    m->addParam("called",m_called);
    if (msg)
	m->copyParam(msg->params(),"callername");
    // TODO: Add call control parameter ?
    Engine::enqueue(m);
    // call.preroute message
    m_route = message("call.preroute",false,true);
    // Parameters to be copied to call.preroute
    static String params = "caller,called,callername,format,formats,callernumtype,callernumplan,callerpres,callerscreening,callednumtype,callednumplan,inn,overlapped";
    plugin.copySigMsgParams(*m_route,event,&params);
    if (m_route->getBoolValue("overlapped") && !m_route->getValue("called"))
	m_route->setParam("called","off-hook");
    if (event->message()) {
	const String* pres = event->message()->params().getParam("callerpres");
	if (pres && (*pres == "restricted"))
	    m_route->setParam("privacy",String::boolText(true));
    }
}

// Construct an unstarted outgoing channel
SigChannel::SigChannel(const char* caller, const char* called)
    : Channel(&plugin,0,true),
    m_caller(caller),
    m_called(called),
    m_call(0),
    m_trunk(0),
    m_hungup(true),
    m_reason("noconn"),
    m_inband(false),
    m_ringback(false),
    m_rtpForward(false),
    m_sdpForward(false),
    m_route(0),
    m_callAccdEvent(0)
{
}

SigChannel::~SigChannel()
{
    TelEngine::destruct(m_route);
    releaseCallAccepted();
    hangup();
    setState("destroyed",true,true);
}

void SigChannel::destroyed()
{
    setState("destroyed",false,true);
    hangup();
}

bool SigChannel::startRouter()
{
    Message* m = m_route;
    m_route = 0;
    Lock lock(m_mutex);
    SignallingCircuit* cic = getCircuit();
    String addr;
    if (cic && cic->getParam("rtp_addr",addr)) {
	m_rtpForward = true;
	m_sdpForward = cic->getBoolParam("sdp_forward");
	addRtp(*m,true);
    }
    lock.drop();
    return Channel::startRouter(m);
}

// Start outgoing call by name of a trunk or list of trunks
bool SigChannel::startCall(Message& msg, String& trunks)
{
    ObjList* trunkList = trunks.split(',',false);
    unsigned int n = trunkList->length();
    for (unsigned int i = 0; i < n; i++) {
	const String* trunkName = static_cast<const String*>((*trunkList)[i]);
	if (!trunkName)
	    continue;
	SigTrunk* trunk = plugin.findTrunk(*trunkName,true);
	if (trunk && startCall(msg,trunk)) {
	    // success - update trunk parameter in message
	    trunks = *trunkName;
	    break;
	}
    }
    delete trunkList;

    setState(0);
    if (!m_call) {
	msg.setParam("error",m_reason);
	return false;
    }
    // Since the channel started the call remember to hang up as well
    m_hungup = false;
    SignallingCircuit* cic = getCircuit();
    if (cic) {
	m_address << m_trunk->name() << "/" << cic->code();
	// Set echo cancel
	const String* echo = msg.getParam("cancelecho");
	if (echo && *echo) {
	    int taps = echo->toInteger(-1);
	    if (taps > 0) {
		cic->setParam("echotaps",*echo);
		cic->setParam("echocancel",String::boolText(true));
	    }
	    else if (taps == 0)
		cic->setParam("echocancel",String::boolText(false));
	    else
		cic->setParam("echocancel",String::boolText(echo->toBoolean(true)));
	}
	const char* rfc2833 = msg.getValue("rfc2833");
	if (rfc2833)
	    cic->setParam("rtp_rfc2833",rfc2833);
	if (msg.getBoolValue("rtp_forward")) {
	    m_rtpForward = cic->getBoolParam("rtp_forward");
	    if (m_rtpForward) {
		m_sdpForward = (0 != msg.getParam("sdp_raw"));
		msg.setParam("rtp_forward","accepted");
	    }
	}
	else {
	    m_rtpForward = false;
	    cic->setParam("rtp_forward",String::boolText(false));
	}
    }
    setMaxcall(msg);
    setMaxPDD(msg);
    Message* m = message("chan.startup",msg);
    m->setParam("direction",status());
    m_targetid = msg.getValue("id");
    m->copyParams(msg,"caller,callername,called,billid,callto,username");
    // TODO: Add call control parameter ?
    Engine::enqueue(m);
    return true;
}

bool SigChannel::startCall(Message& msg, SigTrunk* trunk)
{
    if (!(trunk && trunk->controller()))
	return false;
    // Data
    m_inband = msg.getBoolValue("dtmfinband",trunk->inband());
    m_ringback = msg.getBoolValue("ringback",trunk->ringback());
    // Make the call
    SignallingMessage* sigMsg = new SignallingMessage;
    sigMsg->params().addParam("caller",m_caller);
    sigMsg->params().addParam("called",m_called);
    sigMsg->params().addParam("callername",msg.getValue("callername"));
    sigMsg->params().copyParam(msg,"circuits");
    sigMsg->params().copyParam(msg,"format");
    sigMsg->params().copyParam(msg,"callernumtype");
    sigMsg->params().copyParam(msg,"callernumplan");
    if (msg.getValue("privacy") && msg.getBoolValue("privacy",true))
	sigMsg->params().addParam("callerpres","restricted");
    else
	sigMsg->params().copyParam(msg,"callerpres");
    sigMsg->params().copyParam(msg,"callerscreening");
    sigMsg->params().copyParam(msg,"complete");
    sigMsg->params().copyParam(msg,"callednumtype");
    sigMsg->params().copyParam(msg,"callednumplan");
    sigMsg->params().copyParam(msg,"inn");
    sigMsg->params().copyParam(msg,"calledpointcode");
    // Copy RTP parameters
    if (msg.getBoolValue("rtp_forward")) {
	NamedList* tmp = new NamedList("rtp");
	tmp->copyParams(msg);
	sigMsg->params().addParam(new NamedPointer("circuit_parameters",tmp));
    }
    // Copy routing params
    unsigned int n = msg.length();
    String prefix = msg.getValue("osig-prefix");
    if (prefix.null())
	prefix << plugin.debugName() << ".";
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = msg.getParam(i);
	if (ns && ns->name().startsWith(prefix))
	    sigMsg->params().setParam(ns->name().substr(prefix.length()),*ns);
    }
    Debug(this,DebugAll,"Trying to call on trunk '%s' [%p]",
	trunk->name().safe(),this);
    m_call = trunk->controller()->call(sigMsg,m_reason);
    if (m_call) {
	m_call->userdata(this);
	m_trunk = trunk;
	m_reason.clear();
	return true;
    }
    Debug(this,DebugAll,"Failed to call on trunk '%s' reason: '%s' [%p]",
	trunk->name().safe(),m_reason.c_str(),this);
    return false;
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
	case SignallingEvent::Circuit:   evCircuit(event);  break;
	case SignallingEvent::Generic:   evGeneric(event,"transport"); break;
	case SignallingEvent::Suspend:   evGeneric(event,"suspend");   break;
	case SignallingEvent::Resume:    evGeneric(event,"resume");    break;
	case SignallingEvent::Charge:    evGeneric(event,"charge");    break;
	default:
	    DDebug(this,DebugStub,"No handler for event '%s' [%p]",
		event->name(),this);
	    if (event->message())
		processCompat(event->message()->params());
    }
}

bool SigChannel::msgProgress(Message& msg)
{
    Channel::msgProgress(msg);
    Lock lock(m_mutex);
    setState("progressing");
    if (!m_call)
	return true;
    bool media = getPeer() && getPeer()->getSource();
    media = media || (m_rtpForward && msg.getBoolValue("rtp_forward",true)
	&& msg.getBoolValue("media"));
    media = msg.getBoolValue("earlymedia",media);
    const char* format = msg.getValue("format");
    SignallingMessage* sm = new SignallingMessage;
    if (media && updateConsumer(format,false) && format)
	sm->params().addParam("format",format);
    if (media)
	m_ringback = false;
    sm->params().addParam("earlymedia",String::boolText(media));
    SignallingEvent* event = new SignallingEvent(SignallingEvent::Progress,sm,m_call);
    TelEngine::destruct(sm);
    SignallingCircuitEvent* cicEvent = handleRtp(msg);
    lock.drop();
    if (cicEvent)
	cicEvent->sendEvent();
    plugin.copySigMsgParams(event,msg);
    event->sendEvent();
    return true;
}

bool SigChannel::msgRinging(Message& msg)
{
    Channel::msgRinging(msg);
    Lock lock(m_mutex);
    setState("ringing");
    if (!m_call)
	return true;
    bool media = getPeer() && getPeer()->getSource();
    media = media || (m_rtpForward && msg.getBoolValue("rtp_forward",true)
	&& msg.getBoolValue("media"));
    media = msg.getBoolValue("earlymedia",media);
    const char* format = msg.getValue("format");
    SignallingMessage* sm = new SignallingMessage;
    if (media && updateConsumer(format,false) && format)
	sm->params().addParam("format",format);
    if (m_ringback && !media) {
	// Attempt to provide ringback using circuit features
	SignallingCircuit* cic = getCircuit();
	if (cic) {
	    NamedList params("ringback");
	    params.addParam("tone","ringback");
	    media = cic->sendEvent(SignallingCircuitEvent::GenericTone,&params);
	}
    }
    if (media)
	m_ringback = false;
    sm->params().addParam("earlymedia",String::boolText(media));
    SignallingEvent* event = new SignallingEvent(SignallingEvent::Ringing,sm,m_call);
    TelEngine::destruct(sm);
    SignallingCircuitEvent* cicEvent = handleRtp(msg);
    lock.drop();
    if (cicEvent)
	cicEvent->sendEvent();
    plugin.copySigMsgParams(event,msg);
    event->sendEvent();
    return true;
}

bool SigChannel::msgAnswered(Message& msg)
{
    Channel::msgAnswered(msg);
    Lock lock(m_mutex);
    setState("answered");
    if (!m_call)
	return true;
    m_ringback = false;
    updateSource(0,false);
    // Start echo training
    SignallingCircuit* cic = getCircuit();
    if (cic) {
	cic->sendEvent(SignallingCircuitEvent::RingEnd);
	String value;
	cic->setParam("echotrain",value);
    }
    const char* format = msg.getValue("format");
    SignallingMessage* sm = new SignallingMessage;
    if (updateConsumer(format,false) && format)
	sm->params().addParam("format",format);
    SignallingEvent* event = new SignallingEvent(SignallingEvent::Answer,sm,m_call);
    TelEngine::destruct(sm);
    SignallingCircuitEvent* cicEvent = handleRtp(msg);
    lock.drop();
    if (cicEvent)
	cicEvent->sendEvent();
    plugin.copySigMsgParams(event,msg);
    event->sendEvent();
    return true;
}

bool SigChannel::msgTone(Message& msg, const char* tone)
{
    if (!(tone && *tone))
	return true;
    bool trySig = true;
    Lock lock(m_mutex);
    DDebug(this,DebugCall,"Tone. '%s' %s[%p]",tone,(m_call ? "" : ". No call "),this);
    // Outgoing, overlap dialing call: try it first
    if (isOutgoing() && m_call && m_call->overlapDialing()) {
	lock.drop();
	if (sendSigTone(msg,tone))
	    return true;
	lock.acquire(m_mutex);
	trySig = false;
    }
    // Try to send: through the circuit, in band or through the signalling protocol
    SignallingCircuit* cic = getCircuit();
    if (cic) {
	NamedList params("");
	params.addParam("tone",tone);
	if (cic->sendEvent(SignallingCircuitEvent::Dtmf,&params))
	    return true;
    }
    if (m_inband && dtmfInband(tone))
	return true;
    if (!m_call)
	return true;
    lock.drop();
    return trySig && sendSigTone(msg,tone);
}

bool SigChannel::msgText(Message& msg, const char* text)
{
    Lock lock(m_mutex);
    DDebug(this,DebugCall,"Text. '%s' %s[%p]",text,(m_call ? "" : ". No call "),this);
    if (!m_call)
	return true;
    SignallingMessage* sm = new SignallingMessage;
    sm->params().addParam("text",text);
    SignallingEvent* event = new SignallingEvent(SignallingEvent::Message,sm,m_call);
    TelEngine::destruct(sm);
    lock.drop();
    plugin.copySigMsgParams(event,msg);
    event->sendEvent();
    return true;
}

bool SigChannel::msgUpdate(Message& msg)
{
    const String& oper = msg["operation"];
    SignallingEvent::Type evt = SignallingEvent::Unknown;
    if (oper == YSTRING("transport"))
	evt = SignallingEvent::Generic;
    else if (oper == YSTRING("suspend"))
	evt = SignallingEvent::Suspend;
    else if (oper == YSTRING("resume"))
	evt = SignallingEvent::Resume;
    else if (oper == YSTRING("progress"))
	evt = SignallingEvent::Progress;
    else if (oper == YSTRING("ringing"))
	evt = SignallingEvent::Ringing;
    else if (oper == YSTRING("charge"))
	evt = SignallingEvent::Charge;
    else if (m_callAccdEvent && (oper == YSTRING("accepted"))) {
	plugin.copySigMsgParams(m_callAccdEvent,msg,"i");
	releaseCallAccepted(true);
	return true;
    }
    else if (oper == YSTRING("request") || oper == YSTRING("reject"))
	return updateMedia(msg,oper);
    else if (oper == YSTRING("initiate")) {
	if (updateMedia(msg,oper))
	    return true;
	Debug(this,DebugMild,"No media update: %s",msg.getValue("reason"));
	return false;
    }
    else if (oper == YSTRING("notify"))
	return notifyMedia(msg);

    if (SignallingEvent::Unknown == evt)
	return Channel::msgUpdate(msg);
    Lock lock(m_mutex);
    if (!m_call)
	return false;
    SignallingMessage* sm = new SignallingMessage;
    sm->params().addParam("operation",oper);
    SignallingEvent* event = new SignallingEvent(evt,sm,m_call);
    lock.drop();
    TelEngine::destruct(sm);
    plugin.copySigMsgParams(event,msg);
    return event->sendEvent();
}

bool SigChannel::msgDrop(Message& msg, const char* reason)
{
    hangup(reason ? reason : "dropped",0,&msg);
    return Channel::msgDrop(msg,m_reason);
}

bool SigChannel::msgTransfer(Message& msg)
{
    Lock lock(m_mutex);
    DDebug(this,DebugCall,"msgTransfer %s[%p]",(m_call ? "" : ". No call "),this);
    if (!m_call)
	return true;
    SignallingEvent* event = new SignallingEvent(SignallingEvent::Transfer,0,m_call);
    lock.drop();
    plugin.copySigMsgParams(event,msg);
    return event->sendEvent();
}

bool SigChannel::callPrerouted(Message& msg, bool handled)
{
    Lock lock(m_mutex);
    setState("prerouted",false);
    return m_call != 0;
}

bool SigChannel::callRouted(Message& msg)
{
    Lock lock(m_mutex);
    if (m_rtpForward && !msg.getBoolValue("rtp_forward"))
	m_rtpForward = false;
    setState("routed",false);
    return m_call != 0;
}

void SigChannel::callAccept(Message& msg)
{
    bool terminated = processCompat(msg);
    Lock lock(m_mutex);
    SignallingEvent* event = 0;
    if (!terminated && m_call) {
	// Check if we should accept now the call
	bool acceptNow = msg.getBoolValue("accept_call",true);
	const char* format = msg.getValue("format");
	updateConsumer(format,false);
	SignallingMessage* sm = new SignallingMessage;
	if (format)
	    sm->params().addParam("format",format);
	if (acceptNow)
	    event = new SignallingEvent(SignallingEvent::Accept,sm,m_call);
	else {
	    DDebug(this,DebugAll,"Postponing call accepted [%p]",this);
	    m_callAccdEvent = new SignallingEvent(SignallingEvent::Accept,sm,m_call);
	    plugin.copySigMsgParams(m_callAccdEvent,msg,"i");
        }
	TelEngine::destruct(sm);
    }
    m_ringback = msg.getBoolValue("ringback",m_ringback);
    if (m_rtpForward) {
	const String* tmp = msg.getParam("rtp_forward");
	if (!(tmp && (*tmp == "accepted"))) {
	    m_rtpForward = false;
	    SignallingCircuit* cic = getCircuit();
	    if (cic)
		cic->setParam("rtp_forward",String::boolText(false));
	}
    }
    setState("accepted",false);
    lock.drop();
    if (event) {
	plugin.copySigMsgParams(event,msg,"i");
	event->sendEvent();
    }
    Channel::callAccept(msg);
    // Enqueue pending DTMFs
    GenObject* gen = 0;
    while (0 != (gen = m_chanDtmf.remove(false))) {
	Message* m = static_cast<Message*>(gen);
	complete(*m);
	dtmfEnqueue(m);
    }
}

void SigChannel::callRejected(const char* error, const char* reason, const Message* msg)
{
    if (m_reason.null())
	m_reason = error ? error : reason;
    setState("rejected",false,true);
    hangup(0,0,msg);
}

void SigChannel::connected(const char *reason)
{
    releaseCallAccepted(true);
    m_reason.clear();
    Channel::connected(reason);
}

void SigChannel::disconnected(bool final, const char* reason)
{
    if (m_reason.null())
	m_reason = reason;
    Channel::disconnected(final,m_reason);
}

void SigChannel::endDisconnect(const Message& params, bool handled)
{
    m_reason = params.getValue(YSTRING("reason"),m_reason);
    const char* prefix = params.getValue(YSTRING("message-oprefix"));
    if (TelEngine::null(prefix))
	return;
    paramMutex().lock();
    parameters().clearParams();
    parameters().setParam("message-oprefix",prefix);
    parameters().copySubParams(params,prefix,false);
    paramMutex().unlock();
}

void SigChannel::hangup(const char* reason, SignallingEvent* event, const NamedList* extra)
{
    static const String params = "reason";
    Lock lock(m_mutex);
    releaseCallAccepted();
    if (m_hungup)
	return;
    m_hungup = true;
    setSource();
    setConsumer();
    if (m_reason.null())
	m_reason = reason ? reason : (Engine::exiting() ? "net-out-of-order" : "normal-clearing");
    setState("hangup",true,true);
    SignallingEvent* ev = 0;
    Lock lock2(driver());
    if (m_call) {
	m_call->userdata(0);
	lock2.drop();
	SignallingMessage* msg = new SignallingMessage;
	msg->params().addParam("reason",m_reason);
	ev = new SignallingEvent(SignallingEvent::Release,msg,m_call);
	TelEngine::destruct(msg);
	TelEngine::destruct(m_call);
	if (extra)
	    plugin.copySigMsgParams(ev,*extra,"i");
	else {
	    paramMutex().lock();
	    plugin.copySigMsgParams(ev,parameters(),"i");
	    paramMutex().unlock();
	}
    }
    if (event) {
	paramMutex().lock();
	parameters().clearParams();
	plugin.copySigMsgParams(parameters(),event,&params);
	paramMutex().unlock();
    }
    lock2.drop();
    lock.drop();
    if (ev)
	ev->sendEvent();
    Message* m = message("chan.hangup",true);
    m->setParam("status",status());
    m->setParam("reason",m_reason);
    Engine::enqueue(m);
}

// Notifier message dispatched handler
void SigChannel::dispatched(const Message& msg, bool handled)
{
    static const String s_disc("chan.disconnected");
    DDebug(this,DebugAll,"dispatched(%s,%u) [%p]",msg.c_str(),handled,this);
    if (s_disc == msg)
	Channel::dispatched(msg,handled);
    else
	processCompat(msg);
}

void SigChannel::statusParams(String& str)
{
    Channel::statusParams(str);
}

void SigChannel::setState(const char* state, bool updateStatus, bool showReason)
{
    if (updateStatus && state)
	status(state);
    if (!debugAt(DebugCall))
	return;
    if (!state) {
	Debug(this,DebugCall,"%s call from=%s to=%s trunk=%s sigcall=%p [%p]",
	    isOutgoing()?"Outgoing":"Incoming",m_caller.safe(),m_called.safe(),
	    m_trunk ? m_trunk->name().c_str() : "no trunk",
	    m_call,this);
	return;
    }
#ifndef DEBUG
    if (!updateStatus)
	return;
#endif
    String show;
    show << "Call " << state;
    if (showReason)
	show << ". Reason: '" << m_reason << "'";
    if (!m_call)
        show << ". No signalling call ";
    Debug(this,DebugCall,"%s [%p]",show.c_str(),this);
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
	    event->name(),tmp.c_str(),
	    String::boolText(inband),this);
	Message* m = message("chan.dtmf");
	m->addParam("text",tmp);
	m->addParam("detected",inband ? "inband" : "signal");
	m->copyParams(event->message()->params(),"dialing");
	if (status() != "incoming")
	    dtmfEnqueue(m);
	else {
	    Lock lock(m_mutex);
	    m_chanDtmf.append(m);
	}
    }
}

void SigChannel::evProgress(SignallingEvent* event)
{
    if (!isAnswered())
	setState("progressing");
    updateCircuitFormat(event,false);
    Message* msg = message("call.progress",false,true);
    if (isupController())
	msg->setNotify(true);
    plugin.copySigMsgParams(*msg,event,&s_noPrefixParams);
    addRtp(*msg);
    if (isAnswered()) {
	*msg = "call.update";
	msg->setParam("operation","progress");
    }
    Engine::enqueue(msg);
}

void SigChannel::evRelease(SignallingEvent* event)
{
    const char* reason = 0;
    SignallingMessage* sig = (event ? event->message() : 0);
    if (sig)
	reason = sig->params().getValue("reason");
    hangup(reason,event);
}

void SigChannel::evAccept(SignallingEvent* event)
{
    setState("accepted",false,false);
    updateCircuitFormat(event,true);
    if (!(event->message() && event->message()->params().count()))
	return;
    Message* msg = message("call.update",false,true);
    if (isupController())
	msg->setNotify(true);
    plugin.copySigMsgParams(*msg,event,&s_noPrefixParams);
    addRtp(*msg);
    msg->setParam("operation","accepted");
    Engine::enqueue(msg);
}

void SigChannel::evAnswer(SignallingEvent* event)
{
    setState("answered");
    updateCircuitFormat(event,true);
    // Start echo training
    SignallingCircuit* cic = getCircuit();
    if (cic) {
	cic->sendEvent(SignallingCircuitEvent::RingEnd);
	String value;
	cic->setParam("echotrain",value);
    }
    Message* msg = message("call.answered",false,true);
    if (isupController())
	msg->setNotify(true);
    plugin.copySigMsgParams(*msg,event,&s_noPrefixParams);
    msg->clearParam("earlymedia");
    addRtp(*msg);
    Engine::enqueue(msg);
}

void SigChannel::evRinging(SignallingEvent* event)
{
    if (!isAnswered())
	setState("ringing");
    updateCircuitFormat(event,false);
    Message* msg = message("call.ringing",false,true);
    if (isupController())
	msg->setNotify(true);
    plugin.copySigMsgParams(*msg,event,&s_noPrefixParams);
    addRtp(*msg);
    if (isAnswered()) {
	*msg = "call.update";
	msg->setParam("operation","ringing");
    }
    Engine::enqueue(msg);
}

void SigChannel::evCircuit(SignallingEvent* event)
{
    SignallingCircuitEvent* ev = event->cicEvent();
    if (!ev)
	return;
    switch (ev->type()) {
	case SignallingCircuitEvent::Alarm:
	case SignallingCircuitEvent::Disconnected:
	    hangup("nomedia");
	    break;
	case SignallingCircuitEvent::GenericTone:
	    if (*ev == "fax") {
		bool inband = ev->getBoolValue("inband");
		Message* msg = message("call.fax",false,true);
		msg->setParam("detected",(inband ? "inband" : "signal"));
		msg->setParam("rtp_forward",String::boolText(m_rtpForward));
		plugin.copySigMsgParams(*msg,event,&s_noPrefixParams);
		Engine::enqueue(msg);
		break;
	    }
	    // fall through
	default:
	    Debug(this,DebugStub,"Unhandled circuit event '%s' type=%d [%p]",
		ev->c_str(),ev->type(),this);
    }
}

void SigChannel::evGeneric(SignallingEvent* event, const char* operation)
{
    SignallingMessage* sig = (event ? event->message() : 0);
    if (!sig || sig->params().count() < 1)
	return;
    Message* msg = message("call.update",false,true);
    plugin.copySigMsgParams(*msg,event,&s_noPrefixParams);
    msg->setParam("operation",operation);
    Engine::enqueue(msg);
}

// Handle RTP forward from a message.
// Return a circuit event to be sent or 0 if not handled
SignallingCircuitEvent* SigChannel::handleRtp(Message& msg)
{
    if (!(m_rtpForward && msg.getBoolValue("rtp_forward")))
	return 0;
    SignallingCircuit* cic = getCircuit();
    if (!cic)
	return 0;
    setSource();
    setConsumer();
    SignallingCircuitEvent* ev = new SignallingCircuitEvent(cic,
	SignallingCircuitEvent::Connect,"rtp");
    ev->copyParams(msg);
    return ev;
}

// Set RTP data from circuit to message
bool SigChannel::addRtp(Message& msg, bool possible)
{
    if (!m_rtpForward)
	return false;
    SignallingCircuit* cic = getCircuit();
    if (!cic)
	return false;
    bool ok = cic->getParams(msg,"rtp");
    if (m_sdpForward) {
	String sdp;
    	if (cic->getParam("sdp_raw",sdp) && sdp) {
	    ok = true;
	    msg.setParam("sdp_raw",sdp);
	}
    }
    if (ok)
	msg.setParam("rtp_forward",possible ? "possible" : String::boolText(true));
    return ok;
}

// Update media from call.update message
bool SigChannel::updateMedia(Message& msg, const String& operation)
{
    bool hadRtp = !m_rtpForward;
    bool rtpFwd = msg.getBoolValue("rtp_forward",m_rtpForward);
    if (!rtpFwd) {
	msg.setParam("error","failure");
	msg.setParam("reason","RTP forwarding is not enabled");
	return false;
    }
    RefPointer<SignallingCircuit> cic = getCircuit();
    if (!(cic && cic->connected())) {
	msg.setParam("error","failure");
	msg.setParam("reason","Circuit missing or not connected");
	return false;
    }
    if (operation == "request") {
	String tmp = msg;
	msg = "rtp";
	bool ok = cic->setParams(msg) && cic->status(SignallingCircuit::Connected,true);
	msg = tmp;
	if (!ok) {
	    msg.setParam("error","failure");
	    msg.setParam("reason","Circuit does not support media change");
	    return false;
	}
	m_rtpForward = cic->getBoolParam("rtp_forward");
	if (m_rtpForward && hadRtp)
	    clearEndpoint();
	Message* m = message("call.update",false,true);
	m->addParam("operation","notify");
	cic->getParams(*m,"rtp");
	return Engine::enqueue(m);
    }
    else if (operation == "initiate") {
	String modes = msg.getValue("mode");
	if (modes.null()) {
	    bool audio = msg.getBoolValue("media",true);
	    if (msg.getBoolValue("media_image",!audio))
		modes = audio ? "t38,fax" : "t38";
	    else if (audio)
		modes = "fax";
	}
	if (modes.null())
	    return false;
	if (!cic->setParam("rtp_forward",String::boolText(rtpFwd))) {
	    msg.setParam("error","failure");
	    msg.setParam("reason","Circuit does not support RTP forward");
	    return false;
	}
	m_rtpForward = cic->getBoolParam("rtp_forward");
	if (m_rtpForward && hadRtp)
	    clearEndpoint();
	m_nextModes.clear();
	if (initiateMedia(msg,cic,modes))
	    return true;
	msgDrop(msg,"nomedia");
    }
    else if (operation == "reject") {
	if (initiateMedia(msg,cic,m_nextModes))
	    return true;
	msgDrop(msg,"nomedia");
    }
    return false;
}

// Attempt to initiate media change from a list of modes
bool SigChannel::initiateMedia(Message& msg, SignallingCircuit* cic, String& modes)
{
    while (modes) {
	String mode;
	int pos = modes.find(',');
	if (pos > 0) {
	    mode = modes.substr(0,pos);
	    modes = modes.substr(pos+1);
	}
	else if (pos < 0) {
	    mode = modes;
	    modes.clear();
	}
	else {
	    modes = modes.substr(1);
	    continue;
	}
	if (initiateMedia(msg,cic,mode,modes.null())) {
	    m_nextModes = modes;
	    return true;
	}
    }
    return false;
}

// Attempt to initiate media change for a single mode
bool SigChannel::initiateMedia(Message& msg, SignallingCircuit* cic,
    const String& mode, bool mandatory)
{
    DDebug(this,DebugAll,"Attempting to initiate mode '%s'",mode.c_str());
    if (!(cic->setParam("special_mode",mode) &&
	cic->status(SignallingCircuit::Connected,true))) {
	msg.setParam("error","failure");
	msg.setParam("reason","Circuit does not support media change");
	return false;
    }
    Message m("call.update");
    complete(m);
    m.addParam("operation","request");
    m.addParam("mandatory",String::boolText(mandatory));
    cic->getParams(m,"rtp");
    if (!Engine::dispatch(m))
	return false;
    msg.clearParam("error");
    msg.clearParam("reason");
    return true;
}

bool SigChannel::notifyMedia(Message& msg)
{
    if (!(m_rtpForward && msg.getBoolValue("rtp_forward")))
	return false;
    RefPointer<SignallingCircuit> cic = getCircuit();
    if (!(cic && cic->connected()))
	return false;
    String tmp = msg;
    msg = "rtp";
    bool ok = cic->setParams(msg) && cic->status(SignallingCircuit::Connected,true);
    msg = tmp;
    if (ok)
	m_nextModes.clear();
    return ok;
}

void SigChannel::updateCircuitFormat(SignallingEvent* event, bool consumer)
{
    const char* format = 0;
    bool cicChange = false;
    if (event->message()) {
	format = event->message()->params().getValue("format");
	cicChange = event->message()->params().getBoolValue("circuit-change",false);
    }
    DDebug(this,DebugInfo,"Updating format to '%s'%s",format,cicChange ? ", circuit changed" : "");
    updateSource(format,cicChange);
    if (consumer)
	updateConsumer(0,cicChange);
}

bool SigChannel::updateConsumer(const char* format, bool force)
{
    if (m_rtpForward)
	return true;
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
    if (m_rtpForward)
	return true;
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

// Retrieve the ISUP call controller from trunk
SS7ISUP* SigChannel::isupController() const
{
    if (!(m_trunk && m_trunk->type() == SigTrunk::SS7Isup))
	return 0;
    return static_cast<SS7ISUP*>(m_trunk->controller());
}

// Process parameter compatibility data from a message
// Return true if the call was terminated
bool SigChannel::processCompat(const NamedList& list)
{
    SS7ISUP* isup = isupController();
    if (!isup)
	return false;
    Lock lock(m_mutex);
    unsigned int cic = 0;
    SignallingCircuit* c = getCircuit();
    if (c)
	cic = c->code();
    if (!cic)
	return false;
    lock.drop();
    bool terminated = false;
    isup->processParamCompat(list,cic,&terminated);
    return terminated;
}

// Send a tone through signalling call
bool SigChannel::sendSigTone(Message& msg, const char* tone)
{
    Lock lock(m_mutex);
    if (!m_call)
	return false;
    SignallingMessage* sm = new SignallingMessage;
    sm->params().addParam("tone",tone);
    SignallingEvent* event = new SignallingEvent(SignallingEvent::Info,sm,m_call);
    TelEngine::destruct(sm);
    lock.drop();
    plugin.copySigMsgParams(event,msg);
    return event->sendEvent();
}

// Release postponed call accepted event. Sent it if requested
void SigChannel::releaseCallAccepted(bool send)
{
    Lock lock(m_mutex);
    if (!m_callAccdEvent)
	return;
    if (send)
	m_callAccdEvent->sendEvent();
    else
	delete m_callAccdEvent;
    m_callAccdEvent = 0;
}

/**
 * SigDriver
 */
SigDriver::SigDriver()
    : Driver("sig","fixchans"),
    m_engine(0),
    m_trunksMutex(true,"SigDriver::trunks"),
    m_topmostMutex(true,"SigDriver::topmost"), m_onDemandMutex(true,"SigDriver::ondemand")
{
    Output("Loaded module Signalling Channel");
    m_statusCmd << "status " << name();
}

SigDriver::~SigDriver()
{
    Output("Unloading module Signalling Channel");
    clearTrunk();
    // Now that the call controllers are gone we can destroy the networks
    m_topmostMutex.lock();
    m_topmost.clear();
    m_topmostMutex.unlock();
    // Clear OnDemand components
    m_onDemandMutex.lock();
    m_onDemand.clear();
    m_onDemandMutex.unlock();
    if (m_engine)
	delete m_engine;
}

bool SigDriver::msgExecute(Message& msg, String& dest)
{
    CallEndpoint* peer = YOBJECT(CallEndpoint,msg.userData());
    if (!peer) {
	Debug(this,DebugNote,"Signalling call failed. No data channel");
	msg.setParam("error","failure");
	return false;
    }
    // Locate and check the trunk parameter
    String* trunk = msg.getParam("trunk");
    // The "link" parameter is OBSOLETE!
    if (!trunk)
	trunk = msg.getParam("link");
    if (!trunk || trunk->null()) {
	Debug(this,DebugNote,
	    "Signalling call failed. No trunk specified");
	msg.setParam("error","noconn");
	return false;
    }
    // Create channel
    lock();
    SigChannel* sigCh = new SigChannel(msg.getValue("caller"),dest);
    sigCh->initChan();
    unlock();
    bool ok = sigCh->startCall(msg,*trunk);
    if (ok) {
	if (sigCh->connect(peer,msg.getValue("reason"))) {
	    sigCh->callConnect(msg);
	    msg.setParam("peerid",sigCh->id());
	    msg.setParam("targetid",sigCh->id());
        }
    }
    else {
	if (!msg.getValue("error"))
	    msg.setParam("error","failure");
	Debug(this,DebugNote,"Signalling call failed with reason '%s'",msg.getValue("error"));
    }
    TelEngine::destruct(sigCh);
    return ok;
}

bool SigDriver::received(Message& msg, int id)
{
    String target;

    switch (id) {
	case Masquerade:
	    target = msg.getValue("id");
	    if (!target.startsWith(prefix())) {
		SigTrunk* trunk = findTrunk(target.substr(0,target.find('/')),false);
		if (trunk && trunk->masquerade(target,msg))
		    return false;
	    }
	    return Driver::received(msg,id);
	case Status:
	    target = msg.getValue("module");
	    // Target is the driver or channel
	    if (!target || target == name() || target.startsWith(prefix()))
		return Driver::received(msg,id);
	    break;
	case Drop:
	    target = msg.getValue("id");
	    if (!target.startsWith(prefix())) {
		SigTrunk* trunk = findTrunk(target.substr(0,target.find('/')),false);
		if (trunk && trunk->drop(target,msg))
		    return true;
	    }
	    return Driver::received(msg,id);
	case Control:
	    {
		const String* dest = msg.getParam("component");
		if (dest && (*dest == "sig"))
		    return reInitialize(msg);
	    }
	    if (m_engine && m_engine->control(msg))
		return true;
	    return Driver::received(msg,id);
	case TcapRequest:
	    {
		target = msg.getValue(YSTRING("tcap.user"));
		m_topmostMutex.lock();
		RefPointer<SigTCAPUser> user = static_cast<SigTCAPUser*>(m_topmost[target]);
		m_topmostMutex.unlock();
		return (user && user->tcapRequest(msg));
	    }
	case Halt:
	    clearTrunk();
	    if (m_engine)
		m_engine->stop();
	    return Driver::received(msg,id);
	case Help:
	    return commandHelp(msg.retValue(),msg.getValue("line"));
	default:
	    return Driver::received(msg,id);
    }

    if (!target.startSkip(name()))
	return false;
    if (target.startSkip("links")) {
	msg.retValue() << "module=" << name();
	msg.retValue() << ",format=Type|Status|Uptime;";

	String ret;
	m_trunksMutex.lock();
	for (ObjList* o = m_trunks.skipNull(); o; o = o->skipNext()) {
	    SigTrunk* trunk = static_cast<SigTrunk*>(o->get());
	    trunk->linkStatus(ret);
	}
	m_trunksMutex.unlock();
	m_topmostMutex.lock();
	for (ObjList* o = m_topmost.skipNull(); o; o = o->skipNext()) {
	    SigTopmost* top = static_cast<SigTopmost*>(o->get());
	    top->linkStatus(ret);
	}
	m_topmostMutex.unlock();

	ObjList* l = ret.split(',',false);
	msg.retValue() << "count=" << l->count();
	TelEngine::destruct(l);
	if (msg.getBoolValue("details",true))
	    msg.retValue().append(ret,";");

	msg.retValue() <<  "\r\n";
    }
    if (target.startSkip("ifaces")) {
	msg.retValue() << "module=" << name();
	msg.retValue() << ",format=Status;";

	String ret;
	m_trunksMutex.lock();
	for (ObjList* o = m_trunks.skipNull(); o; o = o->skipNext()) {
	    SigTrunk* trunk = static_cast<SigTrunk*>(o->get());
	    trunk->ifStatus(ret);
	}
	m_trunksMutex.unlock();
	m_topmostMutex.lock();
	for (ObjList* o = m_topmost.skipNull(); o; o = o->skipNext()) {
	    SigTopmost* top = static_cast<SigTopmost*>(o->get());
	    top->ifStatus(ret);
	}
	m_topmostMutex.unlock();

	ObjList* l = ret.split(',',false);
	msg.retValue() << "count=" << l->count();
	TelEngine::destruct(l);
	if (msg.getBoolValue("details",true))
	    msg.retValue().append(ret,";");

	msg.retValue() <<  "\r\n";
    }

    // Status target=trunk[/cic|/range]
    int pos = target.find("/");
    String trunkName = target.substr(0,pos);
    target = target.substr(trunkName.length() + 1);
    m_trunksMutex.lock();
    RefPointer<SigTrunk> trunk = findTrunk(trunkName,false);
    m_trunksMutex.unlock();
    if (trunk) {
	status(trunk,msg.retValue(),target,msg.getBoolValue(YSTRING("details"),true));
	return true;
    }
    m_topmostMutex.lock();
    RefPointer<SigTopmost> topmost = static_cast<SigTopmost*>(m_topmost[trunkName]);
    m_topmostMutex.unlock();
    if (topmost) {
	status(topmost,msg.retValue());
	return true;
    }
    m_onDemandMutex.lock();
    ObjList* o = m_onDemand.find(trunkName);
    RefPointer<OnDemand> cmp = 0;
    if (o)
	cmp = static_cast<OnDemand*>(o->get());
    m_onDemandMutex.unlock();
    if (cmp) {
	status(cmp,msg.retValue());
	return true;
    }
    return false;
}

// Utility used in status
static void countCic(SignallingCircuit* cic, unsigned int& avail, unsigned int& resetting,
    unsigned int& locked, unsigned int& idle)
{
    if (!cic->locked(SignallingCircuit::LockLockedBusy))
	avail++;
    else {
	if (cic->locked(SignallingCircuit::Resetting))
	    resetting++;
	if (cic->locked(SignallingCircuit::LockLocked))
	    locked++;
    }
    if (cic->available())
	idle++;
}

void SigDriver::status(SigTrunk* trunk, String& retVal, const String& target, bool details)
{
    bool all = target.null();
    String detail;
    String ctrlStatus = "Unknown";
    unsigned int circuits = 0;
    unsigned int calls = 0;
    unsigned int count = 0;
    int singleCic = false;
    unsigned int availableCics = 0;
    unsigned int resettingCics = 0;
    unsigned int lockedCics = 0;
    unsigned int idleCics = 0;
    while (true) {
	SignallingCallControl* ctrl = trunk ? trunk->controller() : 0;
	if (!ctrl)
	    break;
	Lock lckCtrl(ctrl);
	ctrlStatus = ctrl->statusName();
	if (!ctrl->circuits()) {
	    // Count now the number of calls. It should be 0 !!!
	    calls = ctrl->calls().count();
	    break;
	}
	SignallingCircuitRange range(target,0);
	SignallingCircuitRange* rptr = &range;
	if (target == "*" || target == "all") {
	    range.add(ctrl->circuits()->base(),ctrl->circuits()->last());
	    all = true;
	}
	else if (range.count() == 0)
	    rptr = ctrl->circuits()->findRange(target);
	else
	    singleCic = (range.count() == 1) && (-1 != target.toInteger(-1));
	// Count calls, circuits and circuit status if complete status was requested
	if (all) {
	    calls = ctrl->calls().count();
	    ObjList* o = ctrl->circuits()->circuits().skipNull();
	    for (; o; o = o->skipNext()) {
		circuits++;
		SignallingCircuit* cic = static_cast<SignallingCircuit*>(o->get());
		countCic(cic,availableCics,resettingCics,lockedCics,idleCics);
	    }
	}
	for (unsigned int i = 0; rptr && i < rptr->count(); i++) {
	    SignallingCircuit* cic = ctrl->circuits()->find((*rptr)[i]);
	    if (!cic)
		continue;
	    count++;
	    if (!singleCic) {
		if (!all)
		    countCic(cic,availableCics,resettingCics,lockedCics,idleCics);
		if (!details)
		    continue;
		detail.append(String(cic->code()) + "=",",");
		if (cic->span())
		    detail << cic->span()->id();
		detail << "|" << SignallingCircuit::lookupStatus(cic->status());
		detail << "|" << String::boolText(0 != cic->locked(SignallingCircuit::LockLocal));
		detail << "|" << String::boolText(0 != cic->locked(SignallingCircuit::LockRemote));
		detail << "|" << String::boolText(0 != cic->locked(SignallingCircuit::LockChanged|SignallingCircuit::Resetting));
	    }
	    else {
		detail.append("circuit=",",");
		detail << cic->code();
		detail << ",span=" << (cic->span() ? cic->span()->id() : String::empty());
		detail << ",status=" << SignallingCircuit::lookupStatus(cic->status());
		detail << ",lockedlocal=" << String::boolText(0 != cic->locked(SignallingCircuit::LockLocal));
		detail << ",lockedremote=" << String::boolText(0 != cic->locked(SignallingCircuit::LockRemote));
		detail << ",changing=" << String::boolText(0 != cic->locked(SignallingCircuit::LockChanged|SignallingCircuit::Resetting));
		char tmp[9];
		::sprintf(tmp,"%x",cic->locked(-1));
		detail << ",flags=0x" << tmp;
	    }
	}
	break;
    }

    retVal.clear();
    retVal << "module=" << name();
    retVal << ",trunk=" << trunk->name();
    retVal << ",type=" << lookup(trunk->type(),SigTrunk::s_type);
    if (!singleCic) {
	if (target)
	    retVal << ",format=Span|Status|LockedLocal|LockedRemote|Changing";
	if (all) {
	    retVal << ";circuits=" << circuits;
	    retVal << ",status=" << ctrlStatus;
	    retVal << ",calls=" << calls;
	}
	else
	    retVal << ";status=" << ctrlStatus;
	retVal << ",available=" << availableCics;
	retVal << ",resetting=" << resettingCics;
	retVal << ",locked=" << lockedCics;
	retVal << ",idle=" << idleCics;
	if (target)
	    retVal << ",count=" << count;
    }
    retVal.append(detail,";");
    retVal << "\r\n";
}

void SigDriver::status(SigTopmost* topmost, String& retVal)
{
    String detail;
    retVal.clear();
    retVal << "module=" << name();
    retVal << ",component=" << topmost->name();
    String details = "";
    topmost->status(details);
    if (!details.null())
	retVal << "," << details;
    retVal << "\r\n";
}

void SigDriver::status(OnDemand* cmp, String& retVal)
{
    retVal.clear();
    retVal << "module=" << name();
    retVal << ",component=" << cmp->name();
    String details = "";
    cmp->status(details);
    if (!details.null())
	retVal << "," << details;
    retVal << "\r\n";
}

void SigDriver::checkOnDemand()
{
    Lock lock(m_onDemandMutex);
    ObjList remove;
    ListIterator iter(m_onDemand);
    GenObject* obj = 0;
    while ((obj = iter.get())) {
	OnDemand* cmp = static_cast<OnDemand*>(obj);
	if (!cmp)
	    continue;
	Lock lock1(m_engine);
	if (cmp->isAlive())
	    continue;
	m_onDemand.remove(cmp);
    }
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
		DDebug(this,DebugMild,
		    "Received event (%p,'%s') without call. Controller: (%p)",
		    event,event->name(),event->controller());
		return;
	}
	// Remove trunk
	Lock lock(m_trunksMutex);
	SigTrunk* trunk = findTrunk(event->controller());
	if (!trunk)
	    return;
	clearTrunk(trunk->name(),false,0);
	return;
    }
    if (!event->message() && event->type() != SignallingEvent::Circuit) {
	Debug(this,DebugGoOn,"Received event (%p,'%s') without message",event,event->name());
	return;
    }
    // Ok. Send the message to the channel if we have one
    lock();
    RefPointer<SigChannel> ch = static_cast<SigChannel*>(event->call()->userdata());
    unlock();
    if (ch) {
	ch->handleEvent(event);
	if (event->type() == SignallingEvent::Release ||
	    (event->type() == SignallingEvent::Circuit && event->cicEvent() &&
	    event->cicEvent()->type() == SignallingCircuitEvent::Disconnected))
	    ch->disconnect();
	return;
    }
    // No channel
    if (event->type() == SignallingEvent::NewCall) {
	if (!canAccept(true)) {
	    Debug(this,DebugWarn,"Refusing new sig call, full or exiting");
	    SignallingMessage* msg = new SignallingMessage;
	    msg->params().addParam("reason","switch-congestion");
	    SignallingEvent* ev = new SignallingEvent(SignallingEvent::Release,msg,event->call());
	    TelEngine::destruct(msg);
	    ev->sendEvent();
	    return;
	}
	lock();
	ch = new SigChannel(event);
	ch->initChan();
	unlock();
	// Route the call
	if (!ch->startRouter()) {
	    ch->hangup("temporary-failure");
	    TelEngine::destruct(ch);
	}
    }
    else
	XDebug(this,DebugNote,"Received event (%p,'%s') from call without user data",
	    event,event->name());
}

// Find a trunk by name
// If callCtrl is true, match only trunks with call controller
SigTrunk* SigDriver::findTrunk(const char* name, bool callCtrl)
{
    if (!name)
	return 0;
    Lock lock(m_trunksMutex);
    for (ObjList* o = m_trunks.skipNull(); o; o = o->skipNext()) {
	SigTrunk* trunk = static_cast<SigTrunk*>(o->get());
	if (trunk->name() == name) {
	    if (callCtrl && !trunk->controller())
		return 0;
	    return trunk;
	}
    }
    return 0;
}

// Find a trunk by call controller
SigTrunk* SigDriver::findTrunk(const SignallingCallControl* ctrl)
{
    if (!ctrl)
	return 0;
    Lock lock(m_trunksMutex);
    for (ObjList* o = m_trunks.skipNull(); o; o = o->skipNext()) {
	SigTrunk* trunk = static_cast<SigTrunk*>(o->get());
	if (trunk->controller() == ctrl)
	    return trunk;
    }
    return 0;
}

// Disconnect channels. If trunk is not NULL, disconnect only its channels
void SigDriver::disconnectChannels(SigTrunk* trunk)
{
    RefPointer<SigChannel> o;
    lock();
    ListIterator iter(channels());
    unlock();
    for (;;) {
	lock();
	o = static_cast<SigChannel*>(iter.get());
	unlock();
	if (!o)
	    return;
	if (!trunk || (trunk == o->trunk()))
	    o->disconnect();
	o = 0;
    }
}

// Copy incoming message parameters to another list of parameters
// The pointers of NamedPointer parameters are 'stolen' from 'sig' when copying
// If 'params' is not 0, the contained parameters will not be prefixed with
//  event call controller's prefix
void SigDriver::copySigMsgParams(NamedList& dest, SignallingEvent* event,
    const String* params)
{
    SignallingMessage* sig = event ? event->message() : 0;
    if (!sig)
	return;

    ObjList exclude;
    // Copy 'params'
    if (params) {
	ObjList* p = params->split(',',false);
	for (ObjList* o = p->skipNull(); o; o = o->skipNext()) {
	    NamedString* ns = sig->params().getParam(static_cast<String*>(o->get())->c_str());
	    if (!ns)
		continue;
	    dest.addParam(ns->name(),*ns);
	    exclude.append(ns)->setDelete(false);
	}
	TelEngine::destruct(p);
    }
    // Copy all other parameters
    String prefix = (event->controller() ? event->controller()->msgPrefix() : String::empty());
    if (!prefix.null())
	dest.addParam("message-prefix",prefix);
    unsigned int n = sig->params().length();
    bool noParams = true;
    for (unsigned int i = 0; i < n; i++) {
	NamedString* param = sig->params().getParam(i);
	if (!param || exclude.find(param->name()))
	    continue;
	noParams = false;
	NamedPointer* np = static_cast<NamedPointer*>(param->getObject("NamedPointer"));
	if (!np)
	    dest.addParam(prefix+param->name(),*param);
	else
	    dest.addParam(new NamedPointer(prefix+param->name(),np->takeData(),*param));
    }
    if (!prefix.null() && noParams)
	dest.clearParam("message-prefix");
}

// Copy outgoing message parameters
void SigDriver::copySigMsgParams(SignallingEvent* event,
    const NamedList& params, const char* prePrefix)
{
    if (!(event && event->message() && event->controller()))
	return;
    String prefix = event->controller()->msgPrefix();
    if (prefix)
	prefix = prePrefix + prefix;
    prefix = params.getValue("message-oprefix",prefix);
    if (prefix.null())
	return;
    event->message()->params().copySubParams(params,prefix);
}

// Handle command complete requests
bool SigDriver::commandComplete(Message& msg, const String& partLine,
    const String& partWord)
{
    if (partLine.null() || partLine == "help") {
	if (itemComplete(msg.retValue(),"sigdump",partWord))
	    return false;
    }
    else if (partLine == "sigdump") {
	if (m_engine) {
	    NamedList params("");
	    params.addParam("operation","sigdump");
	    params.addParam("partword",partWord);
	    params.addParam("completion",msg.retValue());
	    if (m_engine->control(params))
		msg.retValue() = params.getValue("completion");
	}
    }
    else if (partLine == "control") {
	if (m_engine) {
	    NamedList params("");
	    params.addParam("partword",partWord);
	    params.addParam("completion",msg.retValue());
	    if (m_engine->control(params))
		msg.retValue() = params.getValue("completion");
	}
	return false;
    }
    else {
	String tmp = partLine;
	if (tmp.startSkip("control")) {
	    if (m_engine) {
		NamedList params("");
		params.addParam("component",tmp);
		params.addParam("partword",partWord);
		params.addParam("completion",msg.retValue());
		if (m_engine->control(params))
		    msg.retValue() = params.getValue("completion");
	    }
	    return false;
	}
    }
    bool status = partLine.startsWith("status");
    bool drop = !status && partLine.startsWith("drop");
    if (!(status || drop))
	return Driver::commandComplete(msg,partLine,partWord);
    String overviewCmd = "status overview " + name();
    if (partLine == overviewCmd) {
    	itemComplete(msg.retValue(),"links",partWord);
	itemComplete(msg.retValue(),"ifaces",partWord);
	return true;
    }

    Lock lock(this);
    // line='status sig': add trunks and topmost components
    if (partLine == m_statusCmd) {
	itemComplete(msg.retValue(),"links",partWord);
	itemComplete(msg.retValue(),"ifaces",partWord);
	ObjList* o;
	m_trunksMutex.lock();
	for (o = m_trunks.skipNull(); o; o = o->skipNext())
	    itemComplete(msg.retValue(),static_cast<SigTrunk*>(o->get())->name(),partWord);
	m_trunksMutex.unlock();
	m_topmostMutex.lock();
	for (o = m_topmost.skipNull(); o; o = o->skipNext())
	    itemComplete(msg.retValue(),static_cast<SigTopmost*>(o->get())->name(),partWord);
	m_topmostMutex.unlock();
	m_onDemandMutex.lock();
	for (o = m_onDemand.skipNull(); o; o = o->skipNext())
	    itemComplete(msg.retValue(),static_cast<OnDemand*>(o->get())->name(),partWord);
	m_onDemandMutex.unlock();
	return true;
    }

    if (partLine != "status" && partLine != "drop")
	return false;

    // Empty partial word or name start with it: add name and prefix
    if (!partWord || name().startsWith(partWord)) {
	msg.retValue().append(name(),"\t");
	if (channels().skipNull())
	    msg.retValue().append(prefix(),"\t");
	return false;
    }
    // Non empty partial word greater then module name: check if we have a prefix
    if (!partWord.startsWith(prefix()))
	return false;
    // Partial word is not empty and starts with module's prefix
    // Complete channels
    bool all = (partWord == prefix());
    for (ObjList* o = channels().skipNull(); o; o = o->skipNext()) {
	CallEndpoint* c = static_cast<CallEndpoint*>(o->get());
	if (all || c->id().startsWith(partWord))
	    msg.retValue().append(c->id(),"\t");
    }
    return true;
}

// Custom command handler
bool SigDriver::commandExecute(String& retVal, const String& line)
{
    String tmp = line;
    if (tmp.startSkip("sigdump")) {
	tmp.trimBlanks();
	if (tmp.null() || tmp == "help" || tmp == "?")
	    retVal << "Usage: " << s_miniHelp << "\r\n" << s_fullHelp;
	else {
	    if (m_engine) {
		NamedList params("");
		params.addParam("operation","sigdump");
		int sep = tmp.find(' ');
		if (sep > 0) {
		    params.addParam("component",tmp.substr(0,sep));
		    tmp >> " ";
		    params.addParam("file",tmp.trimBlanks());
		}
		else {
		    params.addParam("component",tmp);
		    params.addParam("file","");
		}
		return m_engine->control(params);
	    }
	    return false;
	}
	retVal << "\r\n";
	return true;
    }
    return Driver::commandExecute(retVal,line);
}

// Help message handler
bool SigDriver::commandHelp(String& retVal, const String& line)
{
    if (line.null() || line == "sigdump") {
	retVal << "  " << s_miniHelp << "\r\n";
	if (line) {
	    retVal << s_fullHelp << "\r\n";
	    return true;
	}
    }
    return false;
}

bool SigDriver::appendOnDemand(SignallingComponent* cmp, int type)
{
    if (!cmp)
	return false;
    if (type != SigFactory::SigSS7SCCP)
	return false;
    SS7SCCP* sccp = YOBJECT(SS7SCCP,cmp);
    if (!sccp)
	return false;
    Lock lock(m_onDemandMutex);
    if (m_onDemand.find(sccp->toString())) {
	Debug(this,DebugGoOn,"Request to append duplicat of on demand component (%p): '%s'.",
	    cmp,cmp->toString().c_str());
	return false;
    }
    SigSS7Sccp* dsccp = new SigSS7Sccp(sccp);
    m_onDemand.append(dsccp);
    DDebug(this,DebugAll,"On Demand (%p): '%s' added",cmp,cmp->toString().c_str());
    return true;
}

bool SigDriver::initOnDemand(NamedList& sect, int type)
{
    Lock lock(m_onDemandMutex);
    ObjList* ret = m_onDemand.find(sect);
    if (!ret) {
	DDebug(this,DebugAll,"Can not find on demand component %s. Must not be needed.",sect.c_str());
	return true;
    }
    OnDemand* cmp = static_cast<OnDemand*>(ret->get());
    if (!cmp)
	return false;
    return cmp->reInitialize(sect);
}


// Append a topmost component to the list. Duplicate names are not allowed
bool SigDriver::appendTopmost(SigTopmost* topmost)
{
    if (!topmost || topmost->name().null())
	return false;
    Lock lock(m_topmostMutex);
    if (m_topmost.find(topmost->name())) {
	Debug(this,DebugWarn,"Can't append topmost (%p): '%s'. Duplicate name",
	    topmost,topmost->name().c_str());
	return false;
    }
    m_topmost.append(topmost);
    DDebug(this,DebugAll,"Topmost (%p): '%s' added",topmost,topmost->name().c_str());
    return true;
}

// Remove a topmost component from list without deleting it
void SigDriver::removeTopmost(SigTopmost* topmost)
{
    if (!topmost)
	return;
    Lock lock(m_topmostMutex);
    m_topmost.remove(topmost,false);
    DDebug(this,DebugAll,"Topmost (%p): '%s' removed",topmost,topmost->name().c_str());
}

// Append a trunk to the list. Duplicate names are not allowed
bool SigDriver::appendTrunk(SigTrunk* trunk)
{
    if (!trunk || trunk->name().null())
	return false;
    if (findTrunk(trunk->name(),false)) {
	Debug(this,DebugWarn,"Can't append trunk (%p): '%s'. Duplicate name",
	    trunk,trunk->name().c_str());
	return false;
    }
    Lock lock(m_trunksMutex);
    m_trunks.append(trunk);
    DDebug(this,DebugAll,"Trunk (%p): '%s' added",trunk,trunk->name().c_str());
    return true;
}

// Remove a trunk from list without deleting it
void SigDriver::removeTrunk(SigTrunk* trunk)
{
    if (!trunk)
	return;
    Lock lock(m_trunksMutex);
    m_trunks.remove(trunk,false);
    DDebug(this,DebugAll,"Trunk (%p): '%s' removed",trunk,trunk->name().c_str());
}

// Delete the given trunk if found
// Clear trunk list if name is 0
// Clear all stacks without waiting for call termination if name is 0
void SigDriver::clearTrunk(const char* name, bool waitCallEnd, unsigned int howLong)
{
    Lock lock(m_trunksMutex);
    if (!name) {
	DDebug(this,DebugAll,"Clearing all trunks");
	disconnectChannels();
	ObjList* obj = m_trunks.skipNull();
	for (; obj; obj = obj->skipNext()) {
	    SigTrunk* trunk = static_cast<SigTrunk*>(obj->get());
	    trunk->cleanup();
	}
	m_trunks.clear();
	return;
    }
    SigTrunk* trunk = findTrunk(name,false);
    if (!trunk)
	return;
    DDebug(this,DebugAll,"Clearing trunk '%s'%s",trunk->name().c_str(),
	waitCallEnd ? ". Waiting for active calls to end" : "");
    // Delay clearing if trunk has a call controller
    if (waitCallEnd && trunk->controller()) {
	trunk->setExiting(howLong);
	return;
    }
    trunk->cleanup();
    m_trunks.remove(trunk,true);
}

bool SigDriver::initTrunk(NamedList& sect, int type)
{
    // Disable ?
    if (!sect.getBoolValue("enable",true)) {
	if (!findTrunk(sect,false) && !sect.null())
	    return false;
	clearTrunk(sect,false);
	return true;
    }
    const char* ttype = lookup(type,SigTrunk::s_type);
    // Create or initialize
    DDebug(this,DebugAll,"Initializing trunk '%s' of type '%s'",
	sect.c_str(),ttype);
    SigTrunk* trunk = findTrunk(sect.c_str(),false);
    bool create = (trunk == 0);
    if (create) {
	switch (type) {
	    case SigTrunk::SS7Isup:
		trunk = new SigSS7Isup(sect,false);
		break;
	    case SigTrunk::SS7Bicc:
		trunk = new SigSS7Isup(sect,true);
		break;
	    case SigTrunk::IsdnPriNet:
	    case SigTrunk::IsdnBriNet:
	    case SigTrunk::IsdnPriCpe:
	    case SigTrunk::IsdnBriCpe:
		trunk = new SigIsdn(sect,(SigTrunk::Type)type);
		break;
	    case SigTrunk::IsdnPriMon:
		trunk = new SigIsdnMonitor(sect);
		break;
	    default:
		return false;
	}
    }
    if (!trunk->initialize(sect)) {
	Debug(this,DebugWarn,"Failed to initialize trunk '%s' of type '%s'",
	    sect.c_str(),ttype);
	if (create)
	    clearTrunk(sect);
	return false;
    }
    return true;
}

bool SigDriver::initTopmost(NamedList& sect, int type)
{
    // Disable ?
    if (!sect.getBoolValue("enable",true)) {
	m_topmost.remove(sect);
	return true;
    }
    const char* ttype = lookup(type,SigFactory::s_compNames);
    // Create or initialize
    DDebug(this,DebugAll,"Initializing topmost '%s' of type '%s'",
	sect.c_str(),ttype);

    SigTopmost* topmost = static_cast<SigTopmost*>(m_topmost[sect]);
    bool create = (topmost == 0);
    if (create) {
	switch (type) {
	    case SigFactory::SigSS7Layer3:
	    case SigFactory::SigSS7M3UA:
		topmost = new SigLinkSet(sect);
		break;
	    case SigFactory::SigSS7Testing:
		topmost = new SigTesting(sect);
		break;
	    case SigFactory::SigSCCPUserDummy:
		topmost =  new SigSCCPUser(sect);
		break;
	    case SigFactory::SigSccpGtt:
		topmost = new SigSccpGtt(sect);
		break;
	    case SigFactory::SigSS7TCAPANSI:
	    case SigFactory::SigSS7TCAPITU:
		topmost = new SigSS7Tcap(sect);
		break;
	    case SigFactory::SigTCAPUser:
		topmost = new SigTCAPUser(sect);
		break;
	    default:
		return false;
	}
    }
    if (!topmost->initialize(sect)) {
	Debug(this,DebugWarn,"Failed to initialize '%s' of type '%s'",
	    sect.c_str(),ttype);
	if (create)
	    TelEngine::destruct(topmost);
	return false;
    }
    return true;
}

bool SigDriver::reInitialize(NamedList& params)
{
    Lock2 lock(m_trunksMutex,m_topmostMutex);
    Configuration* cfg = new Configuration();
    getCfg(params,*cfg);
    String oper = params.getValue("operation");
    int op = lookup(oper,s_operations,Unknown);
    switch (op) {
	case Create:
	    if (checkSections(cfg))
		break;
	    return false;
	case Configure:
	    saveConf(cfg);
	    break;
	default:
	    Debug(this,DebugNote,"Received Unknown control operation: %s", oper.c_str());
	    return false;
    }
    bool ret = false;
    unsigned int n = cfg->sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* sect = cfg->getSection(i);
	ret = initSection(sect);
    }
    TelEngine::destruct(cfg);
    SignallingComponent* router = 0;
    while ((router = engine()->find("","SS7Router",router)))
	YOBJECT(SS7Router,router)->printRoutes();
    checkOnDemand();
    return ret;
}

bool SigDriver::checkSections(Configuration* cfg)
{
    unsigned int n = cfg->sections();
    for (unsigned int i = 0;i < n;i ++) {
	NamedList* sect = cfg->getSection(i);
	if (!sect || sect->null())
	    continue;
	NamedList* sect1 = s_cfg.getSection(*sect);
	if (sect1 && !sect1->getBoolValue("enable")) {
	    Debug(this,DebugAll,"Section '%s' already exists in %s conf file",sect->c_str(),name().c_str());
	    return false;
	}
    }
    // If we are here no section was found in configuration
    // Just append the sections in the specifyed conf file
    saveConf(cfg);
    return true;
}

void SigDriver::saveConf(Configuration* cfg)
{
    unsigned int n = cfg->sections();
    for (unsigned int i = 0;i < n;i ++) {
	NamedList* sect = cfg->getSection(i);
	if (!sect || sect->null())
	    continue;
	NamedList* sect1 = s_cfg.getSection(*sect);
	if (!sect1) {
	    s_cfg.createSection(*sect);
	    sect1 = s_cfg.getSection(*sect);
	}
	else {
	    sect1->clearParams();
	}
	sect1->copyParams(*sect);
	if(!s_cfg.save())
	    Debug(this,DebugWarn,"Failed to save configuration data in file: %s",s_cfg.c_str());
    }
}

void SigDriver::getCfg(NamedList& params, Configuration& cfg)
{
    String trunk = params.getValue("section");
    unsigned int n = params.count();
    for (unsigned int i = 0;i <= n;i ++) {
	NamedString* ns = params.getParam(i);
	if (!ns || ns->name() == "operation" || ns->name() == "targetid"
	    || ns->name() == "component" || ns->name() == "section")
	    continue;
	cfg.setValue(trunk,ns->name(),*ns);
    }
}

void SigDriver::initialize()
{
    Output("Initializing module Signalling Channel");
    Lock2 lock(m_trunksMutex,m_topmostMutex);
    s_cfg = Engine::configFile("ysigchan");
    s_cfg.load();
    m_dataFile = s_cfg.getValue("general","datafile",Engine::configFile("ysigdata"));
    Engine::self()->runParams().replaceParams(m_dataFile);
    s_floodEvents = s_cfg.getIntValue("general","floodevents",20);
    int maxLock = s_cfg.getIntValue("general","maxlock",-2);
    if (maxLock > -2)
	SignallingEngine::maxLockWait(maxLock);
    // Startup
    setup();
    if (!m_engine) {
	installRelay(Masquerade);
	installRelay(Halt);
	installRelay(Help);
	installRelay(Progress);
	installRelay(Update);
	installRelay(Route);
	Engine::install(new IsupDecodeHandler);
	Engine::install(new IsupEncodeHandler);
	Engine::install(new SCCPHandler);
	m_engine = SignallingEngine::self(true);
	m_engine->debugChain(this);
	m_engine->start();
	m_engine->setNotifier(&s_notifier);
    }
    // Apply debug levels to driver and engine
    int level = s_cfg.getIntValue("general","debuglevel",-1);
    if (level >= 0)
	debugLevel(level);
    level = s_cfg.getIntValue("general","debuglevel_engine",-1);
    if (level >= 0)
	m_engine->debugLevel(level);
    // Build/initialize trunks and topmost components
    unsigned int n = s_cfg.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* sect = s_cfg.getSection(i);
	initSection(sect);
    }
    SignallingComponent* router = 0;
    while ((router = engine()->find("","SS7Router",router)))
	YOBJECT(SS7Router,router)->printRoutes();
    checkOnDemand();
}

bool SigDriver::initSection(NamedList* sect)
{
    if (!sect || sect->null() || *sect == "general")
	return false;
    bool ret = false;
    const char* stype = sect->getValue("type");
    int type = lookup(stype,SigFactory::s_compNames,SigFactory::Unknown);
    // Check for valid type
    if (type == SigFactory::Unknown) {
	Debug(this,DebugNote,"Section '%s'. Unknown/missing type '%s'",sect->c_str(),stype);
	return false;
    }
    if (type & SigFactory::SigIsTrunk)
	ret = initTrunk(*sect,type & SigFactory::SigPrivate);
    else if (type & SigFactory::SigTopMost)
	ret = initTopmost(*sect,type);
    else if (type & SigFactory::SigOnDemand)
	ret = initOnDemand(*sect,type);
    return ret;
}

// Load a trunk's section from data file. Return true if found
bool SigDriver::loadTrunkData(NamedList& list)
{
    if (!list)
	return false;
    Lock lock(m_trunksMutex);
    Configuration data(m_dataFile);
    data.load(false);
    NamedList* sect = data.getSection(list);
    if (sect)
	list.copyParams(*sect);
    return sect != 0;
}

// Save a trunk's section to data file
bool SigDriver::saveTrunkData(const NamedList& list)
{
    if (!list)
	return false;
    Lock lock(m_trunksMutex);
    Configuration data(m_dataFile);
    data.load(false);
    data.clearSection(list);
    unsigned int n = list.count();
    if (n) {
	NamedList* tmp = data.createSection(list);
	tmp->copyParams(list);
    }
    if (data.save()) {
	Debug(this,DebugAll,"Saved trunk '%s' data (%u items)",list.c_str(),n);
	return true;
    }
    return false;
}


/**
 * SigTopmost
 */
SigTopmost::SigTopmost(const char* name)
    : TopMost(name)
{
    XDebug(&plugin,DebugAll,"SigTopmost::SigTopmost('%s') [%p]",name,this);
    plugin.appendTopmost(this);
}

void SigTopmost::destroyed()
{
    XDebug(&plugin,DebugAll,"SigTopmost::destroyed() [%p]",this);
    plugin.removeTopmost(this);
    TopMost::destroyed();
}


/**
 * SigLinkSet
 */
void SigLinkSet::destroyed()
{
    TelEngine::destruct(m_linkset);
    SigTopmost::destroyed();
}

bool SigLinkSet::initialize(NamedList& params)
{
    if (!m_linkset) {
	m_linkset = YSIGCREATE(SS7Layer3,&params);
	plugin.engine()->insert(m_linkset);
    }
    return m_linkset && m_linkset->initialize(&params);
}

void SigLinkSet::status(String& retVal)
{
    retVal << "type=" << (m_linkset ? m_linkset->componentType() : "");
    retVal << ";status=" << (m_linkset && m_linkset->operational() ? "" : "non-") << "operational";
}

void SigLinkSet::ifStatus(String& status)
{
    SS7MTP3* mtp3 = static_cast<SS7MTP3*>(m_linkset);
    if (mtp3) {
	const ObjList* list = mtp3->links();
	for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	    GenPointer<SS7Layer2>* p = static_cast<GenPointer<SS7Layer2>*>(o->get());
	    if (!*p)
		continue;
	    SS7Layer2* link = *p;
	    SS7MTP2* mtp2 = static_cast<SS7MTP2*>(link->getObject("SS7MTP2"));
	    if (mtp2) {
		SignallingInterface* sigIface = mtp2->iface();
		if (sigIface) {
		    status.append(sigIface->toString(),",");
		    status << "=" << (sigIface->control(SignallingInterface::Query) ? "" : "non-");
		    status << "operational";
		}
	    }
	}
    }
}

void SigLinkSet::linkStatus(String& status)
{
    SS7MTP3* mtp3 = YOBJECT(SS7MTP3,m_linkset);
    if (mtp3) {
	const ObjList* list = mtp3->links();
	for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	    GenPointer<SS7Layer2>* p = static_cast<GenPointer<SS7Layer2>*>(o->get());
	    if (!*p)
		continue;
	    SS7Layer2* link = *p;
	    if (link) {
		status.append(link->toString(),",") << "=";
		status << link->componentType() ;
		status << "|" << link->statusName();
		status << "|" << link->upTime();
	    }
	}
    }
}

/**
 * SigSS7ccp
 */

SigSS7Sccp::~SigSS7Sccp()
{
    if (m_sccp) {
	if (m_sccp->refcount() > 1)
	    Debug(&plugin,DebugWarn,"Removing alive OnDemand component '%s' [%p]",
		  name().c_str(),this);
	TelEngine::destruct(m_sccp);
	m_sccp = 0;
    }
}

void SigSS7Sccp::status(String& retVal)
{
    if (!m_sccp)
	return;
    retVal << "type=" << m_sccp->componentType();
    retVal << ";sent=" << m_sccp->messagesSend();
    retVal << ",received=" <<  m_sccp->messagesReceived();
    retVal << ",translations=" <<  m_sccp->translations();
    retVal << ",errors=" <<  m_sccp->errors();
}

bool SigSS7Sccp::isAlive()
{
    return m_sccp && m_sccp->refcount() > 1;
}

/**
 * SigSCCPUser
 */

SigSCCPUser::~SigSCCPUser()
{
    if (m_user) {
	plugin.engine()->remove(m_user);
	TelEngine::destruct(m_user);
    }
}

bool SigSCCPUser::initialize(NamedList& params)
{
#ifdef DEBUG
    String tmp;
    params.dump(tmp,"\r\n  ",'\'',true);
    Debug(DebugAll,"SigSCCPUser::initialize %s",tmp.c_str());
#endif
    if (!m_user) {
	m_user = new SCCPUserDummy(params);
	plugin.engine()->insert(m_user);
    }
    return m_user && m_user->initialize(&params);

}

/**
 * class SigSccpGtt
 */

SigSccpGtt::~SigSccpGtt()
{
    DDebug(&plugin,DebugAll,"Destroying SigSccpGtt [%p]",this);
    if (m_gtt) {
	plugin.engine()->remove(m_gtt);
	TelEngine::destruct(m_gtt);
    }
}

bool SigSccpGtt::initialize(NamedList& params)
{
    if (!m_gtt) {
	m_gtt = new GTTranslator(params);
	plugin.engine()->insert(m_gtt);
    }
    return m_gtt && m_gtt->initialize(&params);
}

/**
 * SigTesting
 */
void SigTesting::destroyed()
{
    TelEngine::destruct(m_testing);
    SigTopmost::destroyed();
}

bool SigTesting::initialize(NamedList& params)
{
    if (!m_testing) {
	m_testing = YSIGCREATE(SS7Testing,&params);
	plugin.engine()->insert(m_testing);
    }
    return m_testing && m_testing->initialize(&params);
}


/**
 * SigTrunk
 */
const TokenDict SigTrunk::s_type[] = {
    { "ss7-isup",     SS7Isup },
    { "ss7-bicc",     SS7Bicc },
    { "isdn-pri-net", IsdnPriNet },
    { "isdn-bri-net", IsdnBriNet },
    { "isdn-pri-cpe", IsdnPriCpe },
    { "isdn-bri-cpe", IsdnBriCpe },
    { "isdn-pri-mon", IsdnPriMon },
    { 0, 0 }
};

SigTrunk::SigTrunk(const char* name, Type type)
    : TopMost(name),
      m_controller(0),
      m_init(false),
      m_inband(false),
      m_ringback(false),
      m_type(type),
      m_thread(0)
{
    XDebug(&plugin,DebugAll,"SigTrunk::SigTrunk('%s') [%p]",name,this);
    plugin.appendTrunk(this);
}

void SigTrunk::destroyed()
{
    XDebug(&plugin,DebugAll,"SigTrunk::destroyed() [%p]",this);
    cleanup();
    plugin.removeTrunk(this);
}

// Set exiting flag for call controller and timeout for the thread
void SigTrunk::setExiting(unsigned int msec)
{
    if (m_controller)
	m_controller->setExiting();
    if (m_thread)
	m_thread->m_timeout = Time::msecNow() + msec;
}

// Initialize (create or reload) the trunk. Set debug levels for contained objects
// Fix some type depending parameters:
//   Force 'readonly' to true for ISDN monitors
//   Check the value of 'rxunderruninterval' for SS7 and non monitor ISDN links
bool SigTrunk::initialize(NamedList& params)
{
    // Reload common parameters
    m_inband = params.getBoolValue("dtmfinband",s_cfg.getBoolValue("general","dtmfinband",false));
    m_ringback = params.getBoolValue("ringback",s_cfg.getBoolValue("general","ringback",false));

    // Check error:
    //  No need to initialize if no signalling engine or not in plugin's list
    //  For SS7 trunks check the router
    String error;
    bool init = true;
    while (true) {
	if (!(plugin.engine() && plugin.findTrunk(name(),false))) {
	    error = "No engine or not in module's list";
	    break;
	}
	// Create/reload
	init = m_init;
	m_init = true;
	if (init ? reload(params) : create(params,error))
	    return true;
	break;
    }
    Debug(&plugin,DebugNote,"Trunk('%s'). %s failure: %s [%p]",
	name().c_str(),init?"Reload":"Create",error.safe(),this);
    return false;
}

void SigTrunk::handleEvent(SignallingEvent* event)
{
    plugin.handleEvent(event);
}

// Clear channels with calls belonging to this trunk. Cancel thread if any. Call release
void SigTrunk::cleanup()
{
    plugin.disconnectChannels(this);
    if (m_thread) {
	m_thread->cancel();
	while(m_thread)
	    Thread::yield();
	Debug(&plugin,DebugAll,"Trunk('%s'). Worker thread terminated [%p]",name().c_str(),this);
    }
    release();
}

bool SigTrunk::startThread(String& error, unsigned long sleepUsec, int floodLimit)
{
    if (!m_thread) {
	if (m_controller)
	    m_thread = new SigTrunkThread(this,sleepUsec,floodLimit);
	else {
	    Debug(&plugin,DebugNote,
		"Trunk('%s'). No worker thread for trunk without call controller [%p]",
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

// Build a signalling circuit group for this trunk
SignallingCircuitGroup* SigTrunk::buildCircuits(NamedList& params, const String& device,
	const String& debugName, String& error)
{
    ObjList* voice = device.split(',',false);
    if (!voice) {
	error = "Missing or invalid voice parameter";
	return 0;
    }
    unsigned int start = params.getIntValue("offset",0);
    SignallingCircuitGroup* group =
	new SignallingCircuitGroup(0,SignallingCircuitGroup::Increment,debugName);
    for (ObjList* o = voice->skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	if (s->null())
	    continue;
	NamedList spanParams(*s);
	if (params.hasSubParams(*s + "."))
	    spanParams.copySubParams(params,*s + ".");
	else
	    spanParams.addParam("local-config","true");
	SignallingCircuitSpan* span = group->buildSpan(*s,start,&spanParams);
	if (!span) {
	    error << "Failed to build voice span '" << *s << "'";
	    break;
	}
	group->insertRange(span,*s);
	start += span->increment();
    }
    TelEngine::destruct(voice);
    if (error.null()) {
	unsigned int n = params.length();
	for (unsigned int i = 0; i < n; i++) {
	    const NamedString* s = params.getParam(i);
	    if (!s || (s->name() != "range"))
		continue;
	    ObjList* o = s->split(':',false);
	    switch (o->count()) {
		case 2:
		    // name:range
		    group->insertRange(o->at(1)->toString(),o->at(0)->toString());
		    break;
		case 3:
		    // name:strategy:range
		    group->insertRange(o->at(2)->toString(),o->at(0)->toString(),
			SignallingCircuitGroup::str2strategy(o->at(1)->toString(),-1));
		    break;
		default:
		    error << "Invalid range: '" << *s << "'";
		    n = 0;
	    }
	    TelEngine::destruct(o);
	}
    }
    if (error.null()) {
	plugin.engine()->insert(group);
	return group;
    }
    SignallingEngine::destruct(group);
    return 0;
}

/**
 * SigSS7Isup
 */
SigSS7Isup::SigSS7Isup(const char* name, bool bicc)
    : SigTrunk(name,SS7Isup),
      m_bicc(bicc)
{
}

SigSS7Isup::~SigSS7Isup()
{
    release();
}

bool SigSS7Isup::create(NamedList& params, String& error)
{
    release();

    String compName;                     // Component name

    // Voice transfer: circuit group, spans, circuits
    // Use the same span as the signalling channel if missing
    buildName(compName,"L1/Data");
    SignallingCircuitGroup* group = buildCircuits(params,
	params.getValue("voice",name()),compName,error);
    if (!group)
	return false;

    // Load circuits lock state from file
    NamedList sect(name());
    if (plugin.loadTrunkData(sect)) {
	DDebug(&plugin,DebugAll,
	    "SigSS7Isup('%s'). Loading circuits lock state from config [%p]",
	    name().c_str(),this);
	unsigned int n = sect.count();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* ns = sect.getParam(i);
	    if (!ns)
		continue;
	    unsigned int code = ns->name().toInteger(0);
	    if (!code)
		continue;
	    SignallingCircuit* cic = group->find(code);
	    if (!cic) {
		DDebug(&plugin,DebugMild,"SigSS7Isup('%s'). Can't find circuit %u [%p]",
		    name().c_str(),code,this);
		continue;
	    }
	    int flags = 0;
	    SignallingUtils::encodeFlags(group,flags,*ns,SignallingCircuit::s_lockNames);
	    // Allow only remote HW/maintenance and local maintenance
	    flags &= SignallingCircuit::LockRemote | SignallingCircuit::LockLocalMaint |
		SignallingCircuit::LockLocalMaintChg;
	    Debug(&plugin,DebugAll,"SigSS7Isup('%s'). Set lock %u=%s flags=0x%x [%p]",
		name().c_str(),code,ns->c_str(),flags,this);
	    cic->setLock(flags);
	}
    }

    plugin.engine()->insert(group);
    // ISUP
    buildName(compName,(m_bicc ? "BICC" : "ISUP"));
    params.setParam("debugname",compName);
    m_controller = m_bicc ? new SS7BICC(params) : new SS7ISUP(params);
    controller()->attach(group);
    if (isup()) {
	plugin.engine()->insert(isup());
	isup()->initialize(&params);
	if (!isup()->setPointCode(params)) {
	    error = "No point codes";
	    return false;
	}
    }

    // Start thread
    if (!startThread(error,plugin.engine()->tickDefault(),
	params.getIntValue("floodevents",s_floodEvents)))
	return false;

    return true;
}

bool SigSS7Isup::reload(NamedList& params)
{
    if (isup()) {
	isup()->initialize(&params);
	isup()->setPointCode(params);
    }
    return true;
}

void SigSS7Isup::release()
{
    // m_controller is a SS7ISUP call controller
    if (m_controller) {
	verifyController(0);
	m_controller->cleanup();
	if (plugin.engine()) {
	    plugin.engine()->remove(isup());
	    SignallingCircuitGroup* group = m_controller->attach((SignallingCircuitGroup*)0);
	    if (group) {
		plugin.engine()->remove(group);
		TelEngine::destruct(group);
	    }
	}
	isup()->destruct();
	m_controller = 0;
    }
    XDebug(&plugin,DebugAll,"SigSS7Isup('%s'). Released [%p]",name().c_str(),this);
}

// Handle events received from call controller
// Process Verify event. Calls the driver's handleEvent() method for other events
void SigSS7Isup::handleEvent(SignallingEvent* event)
{
    if (!event)
	return;
    if (event->type() != SignallingEvent::Verify) {
	SigTrunk::handleEvent(event);
	return;
    }
    if (event->message())
	verifyController(&event->message()->params());
    else
	verifyController(0);
}

// Save circuits state
bool SigSS7Isup::verifyController(const NamedList* params, bool save)
{
    if (!isup())
	return false;
    NamedList tmp("");
    // Do that anyway to reset the verify flag
    bool verify = isup()->verify();
    if (!params) {
	if (!verify)
	    return false;
	isup()->buildVerifyEvent(tmp);
	params = &tmp;
    }
    // Load config
    NamedList sect(name());
    plugin.loadTrunkData(sect);

    Lock lock(m_controller);
    SignallingCircuitGroup* group = m_controller->circuits();
    if (!group)
	return false;
    DDebug(&plugin,DebugInfo,"SigSS7Isup('%s'). Verifying circuits state [%p]",
	name().c_str(),this);
    Lock lockGroup(group);
    bool changed = false;
    NamedList list(sect.c_str());
    // Save local changed maintenance status
    // Save all remote lock flags (except for changed)
    for (ObjList* o = group->circuits().skipNull(); o; o = o->skipNext()) {
	SignallingCircuit* cic = static_cast<SignallingCircuit*>(o->get());
	String code(cic->code());
	bool saveCic = false;

	// Param exists and remote state didn't changed: check local
	//  maintenance flag against params's value (save if last state
        //  is not equal to the current one)
	NamedString* cicParam = sect.getParam(code);
	if (cicParam && !cic->locked(SignallingCircuit::LockRemoteChg)) {
	    int cicFlags = 0;
	    SignallingUtils::encodeFlags(0,cicFlags,*cicParam,SignallingCircuit::s_lockNames);
	    cicFlags &= cic->locked(SignallingCircuit::LockLocalMaintChg|SignallingCircuit::LockLocalMaint);
	    saveCic = (0 != cicFlags);
	}
	else
	    saveCic = (0 != cic->locked(SignallingCircuit::LockRemoteChg));

	if (!saveCic) {
	    if (cicParam) {
	        if (*cicParam)
		    list.addParam(code,*cicParam);
		changed = true;
	    }
	    continue;
	}

	int flags = 0;
	if (cic->locked(SignallingCircuit::LockLocalMaintChg))
	    flags |= SignallingCircuit::LockLocalMaintChg;
	if (cic->locked(SignallingCircuit::LockLocalMaint))
	    flags |= SignallingCircuit::LockLocalMaint;
	if (cic->locked(SignallingCircuit::LockRemoteHWFailChg) &&
	    cic->locked(SignallingCircuit::LockRemoteHWFail))
	    flags |= SignallingCircuit::LockRemoteHWFail;
	if (cic->locked(SignallingCircuit::LockRemoteMaintChg) &&
	    cic->locked(SignallingCircuit::LockRemoteMaint))
	    flags |= SignallingCircuit::LockRemoteMaint;

	// Save only if we have something
	if (flags || (cicParam && !cicParam->null())) {
	    String tmp;
	    for (const TokenDict* dict = SignallingCircuit::s_lockNames; dict->token; dict++)
		if (0 != (flags & dict->value))
		    tmp.append(dict->token,",");
	    DDebug(&plugin,DebugInfo,
		"SigSS7Isup('%s'). Saving cic %s flags 0x%x '%s' (all=0x%x) [%p]",
		name().c_str(),code.c_str(),flags,tmp.c_str(),cic->locked(-1),this);
	    if (tmp)
		list.addParam(code,tmp);
	    changed = true;
	}
	else if (cicParam)
	    changed = true;
	cic->resetLock(SignallingCircuit::LockRemoteChg);
    }
    lockGroup.drop();
    lock.drop();

    if (changed && save)
	plugin.saveTrunkData(list);
    return changed;
}


/**
 * SigIsdn
 */
SigIsdn::SigIsdn(const char* name, Type type)
    : SigTrunk(name,type)
{
}

SigIsdn::~SigIsdn()
{
    release();
}

bool SigIsdn::create(NamedList& params, String& error)
{
    release();
    String compName;                     // Component name

    // Voice transfer: circuit group, spans, circuits
    // Use the same span as the signalling channel if missing
    buildName(compName,"B");
    SignallingCircuitGroup* group = buildCircuits(params,
	params.getValue("voice",params.getValue("sig",name())),compName,error);
    if (!group)
	return false;

    // Q931
    params.setParam("network",String::boolText(0 != (MaskNet & type())));
    params.setParam("primary",String::boolText(0 != (MaskPri & type())));
    buildName(compName,"Q931");
    params.setParam("debugname",compName);
    m_controller = new ISDNQ931(params,compName);
    plugin.engine()->insert(q931());

    // Create links between components and enable them
    controller()->attach(group);
    q931()->initialize(&params);

    // Start thread
    if (!startThread(error,plugin.engine()->tickDefault(),
	params.getIntValue("floodevents",s_floodEvents)))
	return false;

    return true;
}

bool SigIsdn::reload(NamedList& params)
{
    if (q931())
	q931()->initialize(&params);
    return true;
}

void SigIsdn::release()
{
    // m_controller is an ISDNQ931 call controller
    if (m_controller) {
	m_controller->cleanup();
	if (plugin.engine()) {
	    plugin.engine()->remove(q931());
	    SignallingCircuitGroup* group = m_controller->attach((SignallingCircuitGroup*)0);
	    if (group) {
		plugin.engine()->remove(group);
		TelEngine::destruct(group);
	    }
	}
	q931()->destruct();
	m_controller = 0;
    }
    XDebug(&plugin,DebugAll,"SigIsdn('%s'). Released [%p]",name().c_str(),this);
}

void SigIsdn::ifStatus(String& status)
{
    if (m_controller) {
	const ISDNQ921* l2 = static_cast<const ISDNQ921*>(q931()->layer2());
	if (l2) {
	    SignallingInterface* sigIface = l2->iface();
	    if (sigIface) {
		status.append(sigIface->toString(),",");
		status << "=" << (sigIface->control(SignallingInterface::Query) ? "" : "non-") << "operational";
	    }
	}
    }
}

void SigIsdn::linkStatus(String& status)
{
    if (m_controller) {
	const ISDNLayer2* l2 = static_cast<const ISDNLayer2*>(q931()->layer2());
	if (l2) {
	    status.append(l2->toString(),",") << "=";
	    status << l2->componentType();
	    status << "|" << l2->stateName(l2->state());
	    status << "|" << l2->upTime();
	}
    }
}

/**
 * SigIsdnMonitor
 */
SigIsdnMonitor::SigIsdnMonitor(const char* name)
    : SigTrunk(name,IsdnPriMon),
      Mutex(true),
    m_id(0),
    m_chanBuffer(160),
    m_idleValue(255)
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

    Lock lock(this);
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
		rec->disconnect(event->message() ? event->message()->params().getValue("reason") : "normal-clearing");
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
	}
	else
	    rec->disconnect(0);
	lock.drop();
	TelEngine::destruct(rec);
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
	if (id != rec->id())
	    continue;
	msg = msg.getValue("message");
	msg.clearParam("message");
	msg.userData(rec);
	break;
    }
    return true;
}

bool SigIsdnMonitor::drop(String& id, Message& msg)
{
    const char* reason = msg.getValue("reason","dropped");
    if (id == name()) {
	lock();
	ListIterator iter(m_monitors);
	GenObject* o = 0;
	for (; (o = iter.get()); ) {
	    RefPointer<CallEndpoint> c = static_cast<CallEndpoint*>(o);
	    unlock();
	    c->disconnect(reason);
	    c = 0;
	    lock();
	}
	unlock();
	return true;
    }
    Lock lock(this);
    for (ObjList* o = m_monitors.skipNull(); o; o = o->skipNext()) {
	RefPointer<SigIsdnCallRecord> rec = static_cast<SigIsdnCallRecord*>(o->get());
	if (id == rec->id()) {
	    lock.drop();
	    rec->disconnect(reason);
	    return true;
	}
    }
    return false;
}

void SigIsdnMonitor::removeCall(SigIsdnCallRecord* call)
{
    Lock lock(this);
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

    // Set auto detection for Layer 2 (Q.921) type side of the trunk
    params.setParam("detect",String::boolText(true));

    // Voice transfer: circuit groups, spans, circuits
    // Use the same span as the signalling channel if missing
    buildName(compName,"Net/B");
    const char* device = params.getValue("voice-net",params.getValue("sig-net"));
    SignallingCircuitGroup* groupNet = buildCircuits(params,device,compName,error);
    if (!groupNet)
	return false;
    buildName(compName,"Cpe/B");
    device = params.getValue("voice-cpe",params.getValue("sig-cpe"));
    SignallingCircuitGroup* groupCpe = buildCircuits(params,device,compName,error);
    if (!groupCpe) {
	TelEngine::destruct(groupNet);
	return false;
    }
    String sNet, sCpe;
    groupNet->getCicList(sNet);
    groupCpe->getCicList(sCpe);
    if (sNet != sCpe)
	Debug(&plugin,DebugWarn,
	    "SigIsdnMonitor('%s'). Circuit groups are not equal [%p]",
	    name().c_str(),this);

    // Q931
    compName = "";
    compName << name() << '/' << "Q931";
    params.setParam("debugname",compName);
    params.setParam("print-messages",params.getValue("print-layer3PDU"));
    m_controller = new ISDNQ931Monitor(params,compName);
    plugin.engine()->insert(q931());

    // Create links between components and enable them
    q931()->attach(groupNet,true);
    q931()->attach(groupCpe,false);

    // Start thread
    if (!startThread(error,plugin.engine()->tickDefault(),
	params.getIntValue("floodevents",s_floodEvents)))
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
	q931()->initialize(&params);
    return true;
}

void SigIsdnMonitor::release()
{
    lock();
    ListIterator iter(m_monitors);
    GenObject* o = 0;
    for (; (o = iter.get()); ) {
	RefPointer<CallEndpoint> c = static_cast<CallEndpoint*>(o);
	unlock();
	c->disconnect();
	c = 0;
	lock();
    }
    unlock();

    // m_controller is a ISDNQ931Monitor call controller
    if (m_controller) {
	m_controller->cleanup();
	if (plugin.engine()) {
	    plugin.engine()->remove(q931());
	    SignallingCircuitGroup* group = q931()->attach((SignallingCircuitGroup*)0,false);
	    if (group) {
		plugin.engine()->remove(group);
		TelEngine::destruct(group);
	    }
	    group = q931()->attach((SignallingCircuitGroup*)0,true);
	    if (group) {
		plugin.engine()->remove(group);
		TelEngine::destruct(group);
	    }
	}
	q931()->destruct();
	m_controller = 0;
    }
    XDebug(&plugin,DebugAll,"SigIsdnMonitor('%s'). Released [%p]",name().c_str(),this);
}

/**
 * SigSS7Tcap
 */
void SigSS7Tcap::destroyed()
{
    DDebug(&plugin,DebugAll,"SigSS7TCAP::destroyed() [%p]",this);
    TelEngine::destruct(m_tcap);
    SigTopmost::destroyed();
}

bool SigSS7Tcap::initialize(NamedList& params)
{
    if (!m_tcap) {
	String type = params.getValue("type","");
	m_tcap = YSIGCREATE(SS7TCAP,&params);
	if (m_tcap)
	    plugin.engine()->insert(m_tcap);
    }
    return m_tcap && m_tcap->initialize(&params);
}

void SigSS7Tcap::status(String& retVal)
{
    retVal << "type=" << (m_tcap ? m_tcap->componentType() : "");
    NamedList p("");
    m_tcap->status(p);
    retVal << ";totalIncoming=" << p.getValue("totalIncoming","0");
    retVal << ",totalOutgoing=" << p.getValue("totalOutgoing","0");
    retVal << ",totalDiscarded=" << p.getValue("totalDiscarded","0");
    retVal << ",totalNormal=" << p.getValue("totalNormal","0");
    retVal << ",totalAbnormal=" << p.getValue("totalAbnormal","0");
}

/**
 * MsgTcapUser
 */
MsgTCAPUser::~MsgTCAPUser()
{
    DDebug(&plugin,DebugAll,"MsgTCAPUser::~MsgTCAPUser() [%p]",this);
}

bool MsgTCAPUser::initialize(NamedList& params)
{
    DDebug(&plugin,DebugAll,"MsgTCAPUser::initialize() [%p]",this);
    m_tcapName = params.getValue("tcap");
    plugin.requestRelay(SigDriver::TcapRequest,"tcap.request");
    if (!tcapAttach())
	Debug(DebugMild,"Please move configuration of [%s] after [%s], cannot attach now",
	    TCAPUser::toString().c_str(),m_tcapName.c_str());
    return true;
}

SS7TCAP* MsgTCAPUser::tcapAttach()
{
    if (!tcap()) {
	SignallingComponent* tc = plugin.engine()->find(m_tcapName,"SS7TCAP");
	if (tc)
	    attach(YOBJECT(SS7TCAP,tc));
    }
    return tcap();
}

bool MsgTCAPUser::tcapRequest(NamedList& params)
{
    const String* name = params.getParam(YSTRING("tcap"));
    if (m_tcapName && !TelEngine::null(name) && (*name != m_tcapName))
	return false;
    if (tcapAttach()) {
	SS7TCAPError err = tcap()->userRequest(params);
	return err.error() == SS7TCAPError::NoError;
    }
    return false;
}

bool MsgTCAPUser::tcapIndication(NamedList& params)
{
    Message msg("tcap.indication");
    msg.addParam("tcap",m_tcapName,false);
    msg.addParam("tcap.user",TCAPUser::toString(),false);
    msg.copyParams(params);
    return Engine::dispatch(&msg);
}

/**
 * SigTCAPUser
 */
SigTCAPUser::~SigTCAPUser()
{
    DDebug(&plugin,DebugAll,"SigTCAPUser::~SigTCAPUser() [%p]",this);
}

bool SigTCAPUser::initialize(NamedList& params)
{
    DDebug(&plugin,DebugAll,"SigTCAPUser::initialize() [%p]",this);
    if (!m_user)
	m_user = new MsgTCAPUser(toString().c_str());
    if (m_user)
	m_user->initialize(params);
    return true;
}

bool SigTCAPUser::tcapRequest(NamedList& params)
{
    return (m_user && m_user->tcapRequest(params));
}

void SigTCAPUser::status(String& retVal)
{
}

void SigTCAPUser::destroyed()
{
    DDebug(&plugin,DebugAll,"SigTCAPUser::destroyed() [%p]",this);
    TelEngine::destruct(m_user);
    SigTopmost::destroyed();
}

/**
 * SigConsumerMux
 */
unsigned long SigConsumerMux::Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
{
    if (m_owner) {
	m_owner->consume(m_first,data,tStamp);
	return invalidStamp();
    }
    return 0;
}

/**
 * SigSourceMux
 */
SigSourceMux::SigSourceMux(const char* format, unsigned char idleValue, unsigned int chanBuffer)
    : DataSource(format),
    m_lock(true,"SigSourceMux"),
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
    TelEngine::destruct(m_firstChan);
    TelEngine::destruct(m_secondChan);
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
	TelEngine::destruct(src);
    }
}

#undef MUX_CHAN

/**
 * SigIsdnCallRecord
 */
SigIsdnCallRecord::SigIsdnCallRecord(SigIsdnMonitor* monitor, const char* id,
	SignallingEvent* event)
    : CallEndpoint(id),
      m_lock(true,"SigIsdnCallRecord"),
      m_netInit(false),
      m_monitor(monitor),
      m_call(0)
{
    m_status = "startup";
    // These parameters should be checked by the monitor
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
    bool chg = (msg->params().getValue("circuit-change") != 0);
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
	    TelEngine::destruct(source);
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
	if (!callRouteAndExec(format)) {
	    if (m_reason)
		m_reason = "local-" + m_reason;
	    break;
	}
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
    Lock lock2(m_monitor);
    m_call->userdata(0);
    lock2.drop();
    if (m_monitor)
	m_monitor->q931()->terminateMonitor(m_call,m_reason);
    TelEngine::destruct(m_call);
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
    Message* m = message("call.preroute");
    bool ok = false;
    while (true) {
	if (Engine::dispatch(m) && (m->retValue() == "-" || m->retValue() == "error")) {
	    m_reason = m->getValue("reason",m->getValue("error","failure"));
	    break;
	}
	*m = "call.route";
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
 * SigTrunkThread
 */
SigTrunkThread::~SigTrunkThread()
{
    if (m_trunk)
	m_trunk->m_thread = 0;
    DDebug(&plugin,DebugAll,"Worker destroyed for trunk '%s' [%p]",
	m_trunk?m_trunk->name().c_str():"",this);
}

void SigTrunkThread::run()
{
    if (!(m_trunk && m_trunk->controller()))
	return;
    DDebug(&plugin,DebugAll,"%s is running for trunk '%s' [%p]",
	name(),m_trunk->name().c_str(),this);
    int evCount = 0;
    SignallingEvent* event = 0;
    while (true) {
	if (!event)
	    Thread::usleep(m_sleepUsec,true);
	else
	    Thread::check(true);
	Time time;
	event = m_trunk->controller()->getEvent(time);
	if (event) {
	    evCount++;
	    XDebug(&plugin,DebugAll,"Trunk('%s'). Got event #%d (%p,'%s',%p,%u)",
		m_trunk->name().c_str(),evCount,
		event,event->name(),event->call(),
		event->call()?event->call()->refcount():0);
	    m_trunk->handleEvent(event);
	    delete event;
	    if (evCount == m_floodEvents)
		Debug(&plugin,DebugMild,"Trunk('%s') flooded: %d handled events",
		    m_trunk->name().c_str(),evCount);
	    else if ((evCount % m_floodEvents) == 0)
		Debug(&plugin,DebugWarn,"Trunk('%s') severe flood: %d events",
		    m_trunk->name().c_str(),evCount);
	}
	else {
#ifdef XDEBUG
	    if (evCount)
		Debug(&plugin,DebugAll,"Trunk('%s'). Got no event",m_trunk->name().c_str());
#endif
	    evCount = 0;
	}
	// Check timeout if waiting to terminate
	if (m_timeout && time.msec() > m_timeout) {
	    DDebug(&plugin,DebugInfo,
		"SigTrunkThread::run(). Trunk '%s' timed out [%p]",
		m_trunk->name().c_str(),this);
	    String name = m_trunk->name();
	    m_trunk->m_thread = 0;
	    m_trunk = 0;
	    plugin.clearTrunk(name);
	    break;
	}
    }
}


/**
 * IsupDecodeHandler
 */
// Init the ISUP component
IsupDecodeHandler::IsupDecodeHandler(bool decode)
    : MessageHandler(decode ? "isup.decode" : "isup.encode",100,plugin.name())
{
    NamedList params("");
    String dname = plugin.prefix() + *this;
    params.addParam("debugname",dname);
    // Avoid some useless debug messages
    params.addParam("pointcodetype",SS7PointCode::lookup(SS7PointCode::ITU));
    params.addParam("remotepointcode","1-1-1");
    m_isup = new SS7ISUP(params);
    m_isup->debugChain(&plugin);
}

void IsupDecodeHandler::destruct()
{
    SignallingEngine::destruct(m_isup);
    MessageHandler::destruct();
}

bool IsupDecodeHandler::received(Message& msg)
{
    NamedString* ns = msg.getParam("rawdata");
    DataBlock* data = 0;
    if (ns) {
	NamedPointer* p = static_cast<NamedPointer*>(ns->getObject("NamedPointer"));
	if (p && p->userObject("DataBlock"))
	    data = static_cast<DataBlock*>(p->userData());
    }
    if (!data || data->length() < 2) {
	DDebug(&plugin,DebugNote,"%s. Invalid data len %u",c_str(),data->length());
	return false;
    }

    String prefix = msg.getValue("message-prefix");
    const unsigned char* paramPtr = (const unsigned char*)data->data();
    SS7MsgISUP::Type msgType = (SS7MsgISUP::Type)(*paramPtr++);
    DDebug(&plugin,DebugAll,"%s msg=%s type=%s basetype=%s [%p]",
	msg.c_str(),SS7MsgISUP::lookup(msgType),
	msg.getValue(prefix+"protocol-type"),
	msg.getValue(prefix+"protocol-basetype"),this);

    SS7PointCode::Type pcType = getPCType(msg,prefix);
    if (pcType == SS7PointCode::Other)
	return false;

    if (m_isup->decodeMessage(msg,msgType,pcType,paramPtr,data->length()-1))
	return true;
    msg.setParam("error","Parser failure");
    return false;
}

// Get point code type (protocol version) from message
SS7PointCode::Type IsupDecodeHandler::getPCType(Message& msg, const String& prefix)
{
    String proto = msg.getValue(prefix+"protocol-type");
    if (proto == "itu-t")
	return SS7PointCode::ITU;
    else if (proto == "ansi")
	return SS7PointCode::ANSI;
    // Check if protocol-basetype starts with known values
    // Use the protocol-type if base is missing
    const char* base = msg.getValue(prefix+"protocol-basetype");
    if (base)
	proto = base;
    if (proto.startsWith("itu-t"))
	return SS7PointCode::ITU;
    else if (proto.startsWith("ansi"))
	return SS7PointCode::ANSI;
    msg.setParam("error","Unknown protocol-type");
    return SS7PointCode::Other;
}


/**
 * IsupEncodeHandler
 */
bool IsupEncodeHandler::received(Message& msg)
{
    String prefix = msg.getValue("message-prefix");

    DDebug(&plugin,DebugAll,"%s msg=%s type=%s basetype=%s [%p]",
	msg.c_str(),msg.getValue(prefix+"message-type"),
	msg.getValue(prefix+"protocol-type"),
	msg.getValue(prefix+"protocol-basetype"),this);

    SS7MsgISUP::Type msgType = (SS7MsgISUP::Type)SS7MsgISUP::lookup(msg.getValue(prefix+"message-type"));
    if (msgType == SS7MsgISUP::Unknown) {
	msg.setParam("error","Unknown message-type");
	return false;
    }
    SS7PointCode::Type pcType = getPCType(msg,prefix);
    if (pcType == SS7PointCode::Other)
	return false;

    DataBlock* data = new DataBlock;
    if (m_isup->encodeMessage(*data,msgType,pcType,msg)) {
	msg.addParam(new NamedPointer("rawdata",data,"isup"));
	return true;
    }
    TelEngine::destruct(data);
    msg.setParam("error","Encoder failure");
    return false;
}

/**
 * SCCPHandler
 */

bool SCCPHandler::received(Message& msg)
{
    if (!plugin.engine())
	return false;
#ifdef XDEBUG
    String tmp;
    msg.dump(tmp,"\r\n  ",'\'',true);
    Debug(DebugAll,"SCCPHandler::received%s",tmp.c_str());
#endif
    const char* compName = msg.getValue("component-name");
    if (!compName) {
	DDebug(&plugin,DebugInfo,"Received message %s widthout component name",msg.c_str());
	return false;
    }
    SignallingComponent* cmp = plugin.engine()->find(compName);
    if (!cmp) {
	DDebug(&plugin,DebugInfo,"Failed to find signalling component %s",compName);
	return false;
    }
    SCCPUserDummy* sccpUser = YOBJECT(SCCPUserDummy,cmp);
    if (!sccpUser) {
	DDebug(&plugin,DebugInfo,"The component %s is not an instance of SCCPUserDummy!",compName);
	return false;
    }
    DataBlock data;
    NamedString* dataNamedPointer = msg.getParam("data");
    if (dataNamedPointer) {
	NamedPointer* dataPointer = YOBJECT(NamedPointer,dataNamedPointer);
	if (dataPointer) {
	    DataBlock* recvData = YOBJECT(DataBlock,dataPointer->userData());
	    if (recvData)
		data.assign(recvData->data(),recvData->length());
	} else {
	    String* hexData = YOBJECT(String,dataNamedPointer);
	    if (hexData) {
		data.unHexify(hexData->c_str(),hexData->length(),' ');
	    }
	}
    }
    if (data.length() == 0) {
	DDebug(&plugin,DebugNote,"Received message %s with no data",msg.c_str());
	return false;
    }
    if (msg.getParam("mgm")) {
	return sccpUser->sccpNotify((SCCP::Type)msg.getIntValue("type"),msg);
    }
    return sccpUser->sendData(data,msg);
}

/**
 * class SCCPUserDummy
 */

SCCPUserDummy::SCCPUserDummy(const NamedList& params)
    : SignallingComponent(params,&params,"ss7-tcap-user"),
      SCCPUser(params), m_ssn(0)
{
    Debug(&plugin,DebugAll,"Creating SCCPUserDummy [%p]",this);
    m_ssn = params.getIntValue("ssn",0);
}

SCCPUserDummy::~SCCPUserDummy()
{
    Debug(&plugin,DebugAll,"Destroying SCCPUserDummy [%p]",this);
}

HandledMSU SCCPUserDummy::receivedData(DataBlock& data, NamedList& params)
{
    int ssn = params.getIntValue("ssn");
    if (m_ssn && ssn != m_ssn)
	return HandledMSU::Rejected;
    Message* msg = new Message(params);
    *msg = "sccp.message";
    String hexData;
    hexData.hexify(data.data(),data.length(),' ');
    DataBlock* msgData = new DataBlock(data);
    msg->setParam(new NamedPointer("data",msgData));
    msg->setParam("hexData",hexData);
    msg->setParam("msgType","indication");
    Engine::enqueue(msg);
    return HandledMSU::Accepted;
}

HandledMSU SCCPUserDummy::notifyData(DataBlock& data, NamedList& params)
{
    Message* msg = new Message("sccp.message");
    msg->copyParams(params);
    String hexData;
    hexData.hexify(data.data(),data.length(),' ');
    DataBlock* msgData = new DataBlock(data);
    msg->setParam(new NamedPointer("data",msgData));
    msg->setParam("hexData",hexData);
    msg->setParam("msgType","notification");
    Engine::enqueue(msg);
    return HandledMSU::Accepted;
}

bool SCCPUserDummy::managementNotify(SCCP::Type type, NamedList &params)
{
    int ssn = params.getIntValue("ssn");
    if (ssn != m_ssn)
	return false;
    switch (type) {
	case SCCP::CoordinateConfirm:
	    DDebug(&plugin,DebugMild,"Subsystem %d can Go Out Of Service",m_ssn);
	    break;
	case SCCP::CoordinateIndication:
	    DDebug(&plugin,DebugMild,"SSN %d received SCCP::CoordinateIndication",m_ssn);
	    sccpNotify(SCCP::CoordinateResponse,params);
	    break;
	default:
	    return false;
    }
    return true;
}

/**
 * class GTTranslator
 */

GTTranslator::GTTranslator(const NamedList& params)
    : SignallingComponent(params.safe("GTT"),&params,"ss7-gtt"),
      GTT(params)
{
    DDebug(this,DebugAll,"Crated Global Title Translator [%p]",this);
}

GTTranslator::~GTTranslator()
{
    DDebug(this,DebugAll,"Destroying Global Title Translator [%p]",this);
}

NamedList* GTTranslator::routeGT(const NamedList& gt, const String& prefix, const String& nextPrefix)
{
    // TODO keep a cache!!
    // Verify if the requested gt exists in cache
    // if exists return the cached translation of th GT
    // if not exists send it for translation
    Message* msg = new Message("sccp.route");
    const char* name = sccp() ? sccp()->toString().c_str() : (const char*)0;
    msg->addParam("component",name,false);
    msg->addParam("translator",toString(),false);
    msg->copyParam(gt,YSTRING("HopCounter"));
    msg->copyParam(gt,YSTRING("MessageReturn"));
    msg->copyParam(gt,YSTRING("LocalPC"));
    msg->copyParam(gt,YSTRING("generated"));
    msg->copySubParams(gt,nextPrefix + ".",false);
    msg->copySubParams(gt,prefix + ".");
    if (Engine::dispatch(msg)) // Append the translated GT to cache
	return msg;
    TelEngine::destruct(msg);
    return 0;
}

void GTTranslator::updateTables(const NamedList& params)
{
    Message* msg = new Message("sccp.update");
    msg->copyParams(params);
    Engine::enqueue(msg);
}

bool GTTranslator::initialize(const NamedList* config)
{
    return GTT::initialize(config);
}

/**
  * SigNotifier
  */
void SigNotifier::notify(NamedList& notifs)
{
    DDebug(&plugin,DebugInfo,"SigNotifier [%p] received a notify ",this);
    Message* msg = new Message("module.update");
    msg->addParam("module",plugin.name());
    msg->copyParams(notifs);
    Engine::enqueue(msg);
}

void SigNotifier::cleanup()
{
    DDebug(&plugin,DebugInfo,"SigNotifier [%p] cleanup()",this);
    SignallingEngine* engine = plugin.engine();
    if (engine)
	engine->removeNotifier(this);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
