/**
 * analog.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Analog Channel
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

using namespace TelEngine;
namespace { // anonymous

class ModuleLine;                        // Module's interface to an analog line or recorder
                                         // Manages the call setup detector and sends call setup info
class ModuleGroup;                       // Module's interface to a group of lines
class AnalogChannel;                     // Channel associated with an analog line
class AnalogCallRec;                     // Recorder call endpoint associated with an analog line monitor
class AnalogDriver;                      // Analog driver
class AnalogWorkerThread;                // Worker thread to get events from a group
class ChanNotifyHandler;                 // chan.notify handler (notify lines on detector events)
class EngineStartHandler;                // engine.start handler (start detectors on lines expectind data before ring)

// Value for m_ringTimer interval. The timer is used to ignore some ring events
// Some ring patterns might raise multiple ring events for the same logical ring
// e.g. ring-ring....ring-ring...
#define RING_PATTERN_TIME 750

// Module's interface to an analog line or monitor
class ModuleLine : public AnalogLine
{
public:
    ModuleLine(ModuleGroup* grp, unsigned int cic, const NamedList& params, const NamedList& groupParams);
    // Get the module group representation of this line's owner
    ModuleGroup* moduleGroup();
    inline const String& caller() const
	{ return m_caller; }
    inline const String& callerName() const
	{ return m_callerName; }
    inline String& called()
	{ return m_called; }
    inline SignallingTimer& noRingTimer()
	{ return m_noRingTimer; }
    // Send call setup data
    void sendCallSetup(bool privacy);
    // Set call setup detector
    void setCallSetupDetector();
    // Remove call setup detector
    void removeCallSetupDetector();
    // Process notifications from detector
    void processNotify(Message& msg);
    // Set the caller, callername and called parameters
    inline void setCall(const char* caller = 0, const char* callername = 0,
	const char* called = 0)
	{ m_caller = caller; m_callerName = callername; m_called = called; }
    // Set the caller, callername and called parameters
    void copyCall(NamedList& dest, bool privacy = false);
    // Fill a string with line status parameters
    void statusParams(String& str);
    // Fill a string with line status detail parameters
    void statusDetail(String& str);
protected:
    virtual void checkTimeouts(const Time& when);
    // Remove detector. Call parent's destructor
    virtual void destroyed() {
	    removeCallSetupDetector();
	    AnalogLine::destroyed();
	}
    // Set the FXO line. Start detector if waiting call setup before first ring
    void setFXO(AnalogLine* fxo);

    String m_called;                     // Called's extension
    // Call setup (caller id)
    String m_caller;                     // Caller's extension
    String m_callerName;                 // Caller's name
    String m_detector;                   // Call setup detector resource
    DataConsumer* m_callSetupDetector;   // The call setup detector
    SignallingTimer m_noRingTimer;       // No more rings detected on unanswered line
    SignallingTimer m_callSetupTimer;    // Timeout of call setup data received before the first ring
                                         // Stop detector if started and timeout
};

// Module's interface to a group of lines
class ModuleGroup : public AnalogLineGroup
{
    friend class AnalogWorkerThread;     // Set worker thread variable
public:
    // Create an FXS/FXO group of analog lines
    inline ModuleGroup(AnalogLine::Type type, const char* name)
	: AnalogLineGroup(type,name),
	  m_init(false), m_ringback(false), m_thread(0), m_callEndedPlayTime(0)
	{ m_prefix << name << "/"; }
    // Create a group of analog lines used to record
    inline ModuleGroup(const char* name, ModuleGroup* fxo)
	: AnalogLineGroup(name,fxo), m_init(false), m_thread(0), m_callEndedPlayTime(0)
	{ m_prefix << name << "/"; }
    // Create an FXO group of analog lines to be attached to a group of recorders
    inline ModuleGroup(const char* name)
	: AnalogLineGroup(AnalogLine::FXO,name), m_init(false), m_thread(0), m_callEndedPlayTime(0)
	{ m_prefix << name << "/"; }
    virtual ~ModuleGroup()
	{}
    inline ModuleGroup* fxoRec()
	{ return static_cast<ModuleGroup*>(fxo()); }
    inline const String& prefix()
	{ return m_prefix; }
    inline bool ringback() const
	{ return m_ringback; }
    // Remove all channels associated with this group and stop worker thread
    virtual void destruct();
    // Process an event geberated by a line
    void handleEvent(ModuleLine& line, SignallingCircuitEvent& event);
    // Process an event generated by a monitor
    void handleRecEvent(ModuleLine& line, SignallingCircuitEvent& event);
    // Apply debug level. Call create and create worker thread on first init
    // Re(load) lines and calls specific group reload
    // Return false on failure
    bool initialize(const NamedList& params, const NamedList& defaults, String& error);
    // Copy some data to a module's channel
    void copyData(AnalogChannel* chan);
    // Append/remove endpoints from list
    void setEndpoint(CallEndpoint* ep, bool add);
    // Find a recorder by its line
    AnalogCallRec* findRecorder(ModuleLine* line);
    // Check timers for endpoints owned by this group
    void checkTimers(Time& when);
    // Fill a string with group status parameters
    void statusParams(String& str);
    // Fill a string with group status detail parameters
    void statusDetail(String& str);
protected:
    // Disconnect all group's endpoints
    void clearEndpoints(const char* reason = 0);
private:
    // Create FXS/FXO group data: called by initialize() on first init
    bool create(const NamedList& params, const NamedList& defaults,
	String& error);
    // Reload FXS/FXO data: called by initialize() (not called on first init if create failed)
    bool reload(const NamedList& params, const NamedList& defaults,
	String& error);
    // Create recorder group data: called by initialize() on first init
    bool createRecorder(const NamedList& params, const NamedList& defaults,
	String& error);
    // Reload recorder data: called by initialize() (not called on first init if create failed)
    bool reloadRecorder(const NamedList& params, const NamedList& defaults,
	String& error);
    // Reload existing line's parameters
    void reloadLine(ModuleLine* line, const NamedList& params);
    // Build the group of circuits (spans)
    void buildGroup(ModuleGroup* group, ObjList& spanList, String& error);
    // Complete missing line parameters from other list of parameters
    inline void completeLineParams(NamedList& dest, const NamedList& src, const NamedList& defaults) {
	    for (unsigned int i = 0; lineParams[i]; i++)
		if (!dest.getParam(lineParams[i]))
		    dest.addParam(lineParams[i],src.getValue(lineParams[i],
			defaults.getValue(lineParams[i])));
	}
    // Line parameters that can be overridden
    static const char* lineParams[];

    bool m_init;                         // Init flag
    bool m_ringback;                     // Lines need to provide ringback
    String m_prefix;                     // Line prefix used to complete commands
    AnalogWorkerThread* m_thread;        // The worker thread
    // FXS/FXO group data
    String m_callEndedTarget;            // callto when an FXS line was disconnected
    String m_oooTarget;                  // callto when out-of-order (hook is off for a long time after call ended)
    String m_lang;                       // Language for played tones
    u_int64_t m_callEndedPlayTime;       // Time to play call ended prompt
    // Recorder group data
    ObjList m_endpoints;                 // Record data endpoints
};

// Channel associated with an analog line
class AnalogChannel : public Channel
{
    friend class ModuleGroup;            // Copy data
public:
    enum RecordTrigger {
	None = 0,
	FXO,
	FXS
    };
    AnalogChannel(ModuleLine* line, Message* msg, RecordTrigger recorder = None);
    virtual ~AnalogChannel();
    inline ModuleLine* line() const
        { return m_line; }
    // Start outgoing media and echo train if earlymedia or got peer with data source
    virtual bool msgProgress(Message& msg);
    // Start outgoing media and echo train if earlymedia or got peer with data source
    virtual bool msgRinging(Message& msg);
    // Terminate ringing on line. Start echo train. Open audio streams
    virtual bool msgAnswered(Message& msg);
    // Send tones or flash
    virtual bool msgTone(Message& msg, const char* tone);
    // Hangup line
    virtual bool msgDrop(Message& msg, const char* reason);
    // Update echo canceller and/or start echo training
    virtual bool msgUpdate(Message& msg);
    // Set tone detector
    virtual bool callRouted(Message& msg);
    // Open media if not answered
    virtual void callAccept(Message& msg);
    // Hangup
    virtual void callRejected(const char* error, const char* reason = 0,
	const Message* msg = 0);
    // Disconnected notification
    virtual void disconnected(bool final, const char* reason);
    // Hangup
    bool disconnect(const char* reason = 0);
    // Hangup call
    // Keep call alive to play announcements on FXS line not set on hook by the remote FXO
    void hangup(bool local, const char* status = 0, const char* reason = 0);
    // Enqueue chan.dtmf message
    void evDigits(const char* text, bool tone);
    // Line got off hook. Terminate ringing
    // Outgoing: answer it (call outCallAnswered()). Incoming: start echo train
    void evOffHook();
    // Line ring on/off notification. Ring off is ignored
    // Outgoing: enqueue call.ringing
    // Incoming: FXO: Route the call if delayed. Remove line's detector and start ring timer
    void evRing(bool on);
    // Line started (initialized) notification
    // Answer outgoing FXO calls on lines not expecting polarity changes to answer
    // Send called number if any
    void evLineStarted();
    // Dial complete notification. Enqueue call.progress
    // Answer outgoing FXO calls on lines not expecting polarity changes to answer
    void evDialComplete();
    // Line polarity change notification
    // Terminate call if:
    //   - no line or line is not FXO,
    //   - Outgoing: don't answer on polarity or already answered and should hangup on polarity change
    //   - Incoming: don't answer on polarity or polarity already changed and should hangup on polarity change
    // Outgoing: don't answer on polarity or already answered: call outCallAnswered()
    void evPolarity();
    // Line ok: stop alarm timer
    // Terminate channel if not answered; otherwise: start timer if not already started
    void evAlarm(bool alarm, const char* alarms);
    // Check timers. Return false to terminate
    bool checkTimeouts(const Time& when);
protected:
    // Set reason if not already set
    inline void setReason(const char* reason)
	{ if (!m_reason) m_reason = reason; }
    // Route incoming. If first is false the router is started on second ring
    void startRouter(bool first);
    // Set data source and consumer
    bool setAudio(bool in);
    // Set call status. Return true
    bool setStatus(const char* newStat = 0);
    // Set tones to the remote end of the line
    bool setAnnouncement(const char* status, const char* callto);
    // Outgoing call answered: set call state, start echo train, open data source/consumer
    void outCallAnswered(bool stopDial = true);
    // Hangup. Release memory
    virtual void destroyed();
    // Detach the line from this channel and reset it
    void detachLine();
    // Send tones (DTMF or dial number)
    bool sendTones(const char* tone, bool dial = true);
    // Set line polarity
    inline void polarityControl(bool state) {
	    if (!(m_line && m_line->type() == AnalogLine::FXS &&
		m_line->polarityControl() && state != m_polarity))
		return;
	    m_polarity = state;
	    m_line->setCircuitParam("polarity",String::boolText(m_polarity));
	}
private:
    ModuleLine* m_line;                  // The analog line associated with this channel
    bool m_hungup;                       // Hang up flag
    bool m_ringback;                     // Circuit ringback provider flag
    bool m_routeOnSecondRing;            // Delay router if waiting callerid
    RecordTrigger m_recording;           // Recording trigger source
    String m_reason;                     // Hangup reason
    SignallingTimer m_callEndedTimer;    // Call ended notification to the FXO
    SignallingTimer m_ringTimer;         // Timer used to fix some ring patterns
    SignallingTimer m_alarmTimer;        // How much time a channel may stay with its line in alarm
    SignallingTimer m_dialTimer;         // FXO: delay dialing the number
                                         // FXS: send call setup after first ring
    String m_callEndedTarget;            // callto when an FXS line was disconnected
    String m_oooTarget;                  // callto when out-of-order (hook is off for a long time after call ended)
    String m_lang;                       // Language for played tones
    unsigned int m_polarityCount;        // The number of polarity changes received
    bool m_polarity;                     // The last value we've set for the line polarity
    bool m_privacy;                      // Send caller identity
    int m_callsetup;                     // Send callsetup before/after first ring
};

// Recorder call endpoint associated with an analog line monitor
class AnalogCallRec : public CallEndpoint, public DebugEnabler
{
public:
    // Append to driver's list
    AnalogCallRec(ModuleLine* m_line, bool fxsCaller, const char* id);
    inline ModuleLine* line()
	{ return m_line; }
    inline ModuleLine* fxo() const
	{ return m_line ? static_cast<ModuleLine*>(m_line->getPeer()) : 0; }
    inline bool startOnSecondRing()
	{ return m_startOnSecondRing; }
    void hangup(const char* reason = "normal");
    bool disconnect(const char* reason);
    virtual void* getObject(const String& name) const;
    inline const char* reason()
	{ return m_reason; }
    // Create data source. Route and execute
    // Return false to hangup
    bool startRecording();
    // Call answered: start recording
    bool answered();
    // Process rings: start recording if delayed
    // Return false to hangup
    bool ringing(bool fxsEvent);
    // Enqueue chan.dtmf
    void evDigits(bool fxsEvent, const char* text, bool tone);
    // Process line polarity changes. Return false to hangup
    bool evPolarity(bool fxsEvent);
    // Line alarms changed
    bool evAlarm(bool fxsEvent, bool alarm, const char* alarms);
    // Check timers. Return false to terminate
    bool checkTimeouts(const Time& when);
    // Fill a string with recorder status parameters
    void statusParams(String& str);
    // Fill a string with recorder status detail parameters
    void statusDetail(String& str);
protected:
    // Remove from driver's list
    virtual void destroyed();
    virtual void disconnected(bool final, const char *reason);
    // Create a message to be enqueued/dispatched to the engine
    // @param peers True to add caller and called parameters
    // @param userdata True to add this call endpoint as user data
    Message* message(const char* name, bool peers = true, bool userdata = false);
private:
    ModuleLine* m_line;                  // The monitored lines
    bool m_fxsCaller;                    // True if the call originated from the FXS
    bool m_answered;                     // True if answered
    bool m_hungup;                       // Already hungup flag
    unsigned int m_polarityCount;        // The number of polarity changes received
    bool m_startOnSecondRing;            // Start recording on second ring if waiting callerid
    SignallingTimer m_ringTimer;         // Timer used to fix some ring patterns
    String m_reason;                     // Hangup reason
    String m_status;                     // Call status
    String m_address;                    // Call enspoint's address
};

// The driver
class AnalogDriver : public Driver
{
public:
    // Additional driver commands
    enum Commands {
	CmdCount = 0
    };
    // Additional driver status commands
    enum StatusCommands {
	Groups         = 0,              // Show all groups
	Lines          = 1,              // Show all lines
	Recorders      = 2,              // Show all active recorders
	StatusCmdCount = 3
    };
    AnalogDriver();
    ~AnalogDriver();
    inline const String& recPrefix()
	{ return m_recPrefix; }
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest);
    virtual void dropAll(Message& msg);
    // Check timers for recorders owned by the given group
    void checkTimers(Time& when, ModuleGroup* recGrp);
    // Notification of line service state change or removal
    // Return true if a channel or recorder was found
    bool lineUnavailable(ModuleLine* line);
    // Disconnect or deref a channel
    void terminateChan(AnalogChannel* ch, const char* reason = "normal");
    // Destroy a monitor endpoint
    void terminateChan(AnalogCallRec* ch, const char* reason = "normal");
    // Attach detectors after engine started
    void engineStart(Message& msg);
    // Notify lines on detector events
    bool chanNotify(Message& msg);
    // Get an id for a recorder
    inline unsigned int nextRecId() {
	    Lock lock(this);
	    return ++m_recId;
	}
    // Append/remove recorders from list
    void setRecorder(AnalogCallRec* rec, bool add);
    // Find a group by its name
    inline ModuleGroup* findGroup(const String& name) {
	    Lock lock(this);
	    ObjList* obj = m_groups.find(name);
	    return obj ? static_cast<ModuleGroup*>(obj->get()) : 0;
	}
    // Find a recorder by its id
    inline AnalogCallRec* findRecorder(const String& name) {
	    Lock lock(this);
	    ObjList* obj = m_recorders.find(name);
	    return obj ? static_cast<AnalogCallRec*>(obj->get()) : 0;
	}
    // Additional driver status commands
    static String s_statusCmd[StatusCmdCount];
protected:
    virtual bool received(Message& msg, int id);
    // Handle command complete requests
    virtual bool commandComplete(Message& msg, const String& partLine,
	const String& partWord);
    // Execute commands
    virtual bool commandExecute(String& retVal, const String& line);
    // Complete channels/recorders IDs from partial command word
    inline void completeChanRec(String& dest, const String& partWord,
	bool chans, bool all) {
	    ObjList* o = (chans ? channels().skipNull() : m_recorders.skipNull());
	    for (; o; o = o->skipNext()) {
		CallEndpoint* c = static_cast<CallEndpoint*>(o->get());
		if (all || c->id().startsWith(partWord))
		    dest.append(c->id(),"\t");
	    }
	}
    // Complete group names from partial command word
    void completeGroups(String& dest, const String& partWord);
    // Complete line names from partial command word
    void completeLines(String& dest, const String& partWord);
    // Remove a group from list
    void removeGroup(ModuleGroup* group);
    // Find a group or recorder by its name
    // Set useFxo to true to find a recorder by its fxo's name
    ModuleGroup* findGroup(const char* name, bool useFxo);
private:
    bool m_init;                         // Init flag
    String m_recPrefix;                  // Prefix used for recorders
    unsigned int m_recId;                // Next recorder id
    ObjList m_groups;                    // Analog line groups
    ObjList m_recorders;                 // Recorders created by monitor groups
    String m_statusCmd;                  // Prefix for status commands
};

// Get events from a group. Check timers for lines
class AnalogWorkerThread : public Thread
{
public:
    AnalogWorkerThread(ModuleGroup* group);
    virtual ~AnalogWorkerThread();
    virtual void run();
private:
    ModuleGroup* m_client;               // The module's group client
    String m_groupName;                  // Group's name (saved to be printed in destructor)
};


/**
 * Module data and functions
 */
static AnalogDriver plugin;
static Configuration s_cfg;
static bool s_engineStarted = false;               // Received engine.start message
static const char* s_lineSectPrefix = "line ";     // Prefix for line sections in config
static const char* s_unk = "unknown";              // Used to set caller
// Status detail formats
static const char* s_lineStatusDetail = "format=State|UsedBy";
static const char* s_groupStatusDetail = "format=Type|Lines";
static const char* s_recStatusDetail = "format=Status|Address|Peer";


// engine.start handler (start detectors on lines expectind data before ring)
class EngineStartHandler : public MessageHandler
{
public:
    inline EngineStartHandler()
	: MessageHandler("engine.start",100,plugin.name())
	{}
    virtual bool received(Message& msg);
};

// chan.notify handler (notify lines on detector events)
class ChanNotifyHandler : public MessageHandler
{
public:
    inline ChanNotifyHandler()
	: MessageHandler("chan.notify",100,plugin.name())
	{}
    virtual bool received(Message& msg);
};


// Decode a line address into group name and circuit code
// Set first to decode group name until first '/'
// Return:
//   -1 if src is the name of the group
//   -2 if src contains invalid circuit code
//   Otherwise: The integer part of the circuit code
inline int decodeAddr(const String& src, String& group, bool first)
{
    int pos = first ? src.find("/") : src.rfind('/');
    if (pos == -1) {
	group = src;
	return -1;
    }
    group = src.substr(0,pos);
    return src.substr(pos+1).toInteger(-2);
}

// Get FXS/FXO type string
inline const char* callertype(bool fxs)
{
    return fxs ? "fxs" : "fxo";
}

// Get privacy from message
// Return true if caller's identity is private (screened)
static inline bool getPrivacy(const Message& msg)
{
    String tmp = msg.getValue("privacy");
    if (!tmp)
	return false;
    if (!tmp.isBoolean())
	return true;
    return tmp.toBoolean();
}


/**
 * ModuleLine
 */
ModuleLine::ModuleLine(ModuleGroup* grp, unsigned int cic, const NamedList& params, const NamedList& groupParams)
    : AnalogLine(grp,cic,params),
    m_callSetupDetector(0),
    m_noRingTimer(0),
    m_callSetupTimer(callSetupTimeout())
{
    m_detector = groupParams.getValue("analogdetect","analogdetect/callsetup");
    m_detector = params.getValue("analogdetect",m_detector);
    if (type() == AnalogLine::FXO && callSetup() == AnalogLine::Before && s_engineStarted)
	setCallSetupDetector();
}

inline ModuleGroup* ModuleLine::moduleGroup()
{
    return static_cast<ModuleGroup*>(group());
}

// Send call setup data through the FXS line
void ModuleLine::sendCallSetup(bool privacy)
{
    if (type() != AnalogLine::FXS)
	return;
    Lock lock(this);
    if (callSetup() == AnalogLine::NoCallSetup)
	return;

    Message msg("chan.attach");
    if (userdata())
	msg.userData(static_cast<RefObject*>(userdata()));
    msg.addParam("source",m_detector);
    msg.addParam("single",String::boolText(true));
    String tmp;
    tmp << plugin.prefix() << address();
    msg.addParam("notify",tmp);
    copyCall(msg,privacy);

    if (Engine::dispatch(msg))
	return;
    Debug(group(),DebugNote,"%s: failed to send call setup reason='%s' [%p]",
	address(),msg.getValue("reason"),this);
}

// Set the call setup detector
void ModuleLine::setCallSetupDetector()
{
    removeCallSetupDetector();
    m_callerName = "";

    Lock lock(this);
    if (callSetup() == AnalogLine::NoCallSetup)
	return;

    // Dispatch message
    DataSource* src = 0;
    if (circuit())
	src = static_cast<DataSource*>(circuit()->getObject(YATOM("DataSource")));
    Message msg("chan.attach");
    msg.userData(src);
    msg.addParam("consumer",m_detector);
    msg.addParam("single",String::boolText(true));
    String tmp;
    tmp << plugin.prefix() << address();
    msg.addParam("notify",tmp);

    const char* error = 0;
    while (true) {
	if (!Engine::dispatch(msg)) {
	    error = msg.getValue("reason");
	    if (!error)
		error = "chan.attach failed";
	    break;
	}
	DataConsumer* cons = 0;
	if (msg.userData())
	    cons = static_cast<DataConsumer*>(msg.userData()->getObject(YATOM("DataConsumer")));
	if (cons && cons->ref())
	    m_callSetupDetector = cons;
	else
	    error = "chan.attach returned without consumer";
	break;
    }

    if (!error)
	DDebug(group(),DebugAll,"%s: attached detector (%p) [%p]",
	    address(),m_callSetupDetector,this);
    else
	Debug(group(),DebugNote,"%s: failed to attach detector error='%s' [%p]",
	    address(),error,this);
}

// Remove the call setup detector from FXO line
void ModuleLine::removeCallSetupDetector()
{
    Lock lock(this);
    if (!m_callSetupDetector)
	return;

    m_callSetupTimer.stop();
    DataSource* src = m_callSetupDetector->getConnSource();
    if (src)
	src->detach(m_callSetupDetector);
    DDebug(group(),DebugAll,"%s: removed detector (%p) [%p]",
	address(),m_callSetupDetector,this);
    TelEngine::destruct(m_callSetupDetector);
}

// Process notifications from detector
void ModuleLine::processNotify(Message& msg)
{
    String operation = msg.getValue("operation");

    Lock lock(this);

    if (operation == "setup") {
	DDebug(group(),DebugAll,
	    "%s: received setup info detector=%p caller=%s callername=%s called=%s [%p]",
	    address(),m_callSetupDetector,
	    msg.getValue("caller"),msg.getValue("callername"),
	    msg.getValue("called"),this);
	if (!m_callSetupDetector)
	    return;
	m_called = msg.getValue("called",m_called);
	m_caller = msg.getValue("caller");
	m_callerName = msg.getValue("callername");
    }
    else if (operation == "terminate") {
	DDebug(group(),DebugAll,"%s: detector (%p) terminated reason=%s [%p]",
	    address(),m_callSetupDetector,msg.getValue("reason"),this);
	removeCallSetupDetector();
    }
    else if (operation == "start") {
	DDebug(group(),DebugAll,"%s: detector (%p) started [%p]",
	    address(),m_callSetupDetector,this);
	if (callSetup() == AnalogLine::Before && m_callSetupDetector)
	    m_callSetupTimer.start();
    }
    else
	DDebug(group(),DebugStub,
	    "%s: received notification with operation=%s [%p]",
	    address(),operation.c_str(),this);
}

// Set the caller, callername and called parameters
void ModuleLine::copyCall(NamedList& dest, bool privacy)
{
    if (privacy)
	dest.addParam("callerpres","restricted");
    else {
	if (m_caller)
	    dest.addParam("caller",m_caller);
	if (m_callerName)
	    dest.addParam("callername",m_callerName);
    }
    if (m_called)
	dest.addParam("called",m_called);
}

// Fill a string with line status parameters
void ModuleLine::statusParams(String& str)
{
    str.append("module=",";") << plugin.name();
    str << ",address=" << address();
    str << ",type=" << lookup(type(),typeNames());
    str << ",state=" << lookup(state(),stateNames());
    str  << ",usedby=";
    if (userdata())
	str << (static_cast<CallEndpoint*>(userdata()))->id();
    str << ",polaritycontrol=" << polarityControl();
    if (type() == AnalogLine::FXO) {
	str << ",answer-on-polarity=" << answerOnPolarity();
	str << ",hangup-on-polarity=" << hangupOnPolarity();
    }
    else
	str << ",answer-on-polarity=not-defined,hangup-on-polarity=not-defined";
    str << ",callsetup=" << lookup(callSetup(),AnalogLine::csNames());
    // Lines with peer are used in recorders (don't send DTMFs)
    if (!getPeer())
	str << ",dtmf=" << (outbandDtmf() ? "outband" : "inband");
    else
	str << ",dtmf=not-defined";

    // Fill peer status
    bool master = (type() == AnalogLine::FXS && getPeer());
    if (master)
	(static_cast<ModuleLine*>(getPeer()))->statusParams(str);
}

// Fill a string with line status detail parameters
void ModuleLine::statusDetail(String& str)
{
    // format=State|UsedBy
    Lock lock(this);
    str.append(address(),";") << "=";
    str << lookup(state(),AnalogLine::stateNames()) << "|";
    if (userdata())
	str << (static_cast<CallEndpoint*>(userdata()))->id();
}

// Check detector timeout. Calls line's timeout check method
void ModuleLine::checkTimeouts(const Time& when)
{
    if (m_callSetupTimer.timeout(when.msec())) {
	m_callSetupTimer.stop();
	DDebug(group(),DebugNote,"%s: call setup timed out [%p]",address(),this);
	// Reset detector
	setCallSetupDetector();
    }
    AnalogLine::checkTimeouts(when);
}


/**
 * ModuleGroup
 */
// Line parameters that can be overridden
const char* ModuleGroup::lineParams[] = {"echocancel","dtmfinband","answer-on-polarity",
    "hangup-on-polarity","ring-timeout","callsetup","alarm-timeout","delaydial",
    "polaritycontrol",0};

// Remove all channels associated with this group and stop worker thread
void ModuleGroup::destruct()
{
    clearEndpoints(Engine::exiting()?"shutdown":"out-of-service");
    if (m_thread) {
	XDebug(this,DebugInfo,"Terminating worker thread [%p]",this);
	m_thread->cancel(false);
	while (m_thread)
	    Thread::yield(true);
	Debug(this,DebugInfo,"Worker thread terminated [%p]",this);
    }
    AnalogLineGroup::destruct();
}

// Process an event generated by a line
void ModuleGroup::handleEvent(ModuleLine& line, SignallingCircuitEvent& event)
{
    Lock lock(&plugin);
    AnalogChannel* ch = static_cast<AnalogChannel*>(line.userdata());
    DDebug(this,DebugInfo,"Processing event %u '%s' line=%s channel=%s",
	event.type(),event.c_str(),line.address(),ch?ch->id().c_str():"");

    switch (event.type()) {
	case SignallingCircuitEvent::OffHook:
	case SignallingCircuitEvent::Wink:
	    // Line got offhook - clear the ring timer
	    line.noRingTimer().stop();
	default: ;
    }
    if (ch) {
	switch (event.type()) {
	    case SignallingCircuitEvent::Dtmf:
		ch->evDigits(event.getValue("tone"),true);
		break;
	    case SignallingCircuitEvent::PulseDigit:
		ch->evDigits(event.getValue("pulse"),false);
		break;
	    case SignallingCircuitEvent::OnHook:
		ch->hangup(false);
		plugin.terminateChan(ch);
		break;
	    case SignallingCircuitEvent::OffHook:
	    case SignallingCircuitEvent::Wink:
		ch->evOffHook();
		break;
	    case SignallingCircuitEvent::RingBegin:
	    case SignallingCircuitEvent::RingerOn:
		ch->evRing(true);
		break;
	    case SignallingCircuitEvent::RingEnd:
	    case SignallingCircuitEvent::RingerOff:
		ch->evRing(false);
		break;
	    case SignallingCircuitEvent::LineStarted:
		ch->evLineStarted();
		break;
	    case SignallingCircuitEvent::DialComplete:
		ch->evDialComplete();
		break;
	    case SignallingCircuitEvent::Polarity:
		ch->evPolarity();
		break;
	    case SignallingCircuitEvent::Flash:
		ch->evDigits("F",true);
		break;
	    case SignallingCircuitEvent::PulseStart:
		DDebug(ch,DebugAll,"Pulse dialing started [%p]",ch);
		break;
	    case SignallingCircuitEvent::Alarm:
	    case SignallingCircuitEvent::NoAlarm:
		ch->evAlarm(event.type() == SignallingCircuitEvent::Alarm,event.getValue("alarms"));
		break;
	    default:
		Debug(this,DebugStub,"handleEvent(%u,'%s') not implemented [%p]",
		    event.type(),event.c_str(),this);
	}
    }
    else
	if ((line.type() == AnalogLine::FXS &&
		event.type() == SignallingCircuitEvent::OffHook) ||
	    (line.type() == AnalogLine::FXO &&
		((event.type() == SignallingCircuitEvent::RingBegin) ||
		(type() == AnalogLine::Recorder && event.type() == SignallingCircuitEvent::Wink)))) {
	    if (!line.ref()) {
		Debug(this,DebugWarn,"Incoming call on line '%s' failed [%p]",
		    line.address(),this);
		return;
	    }
	    if (line.noRingTimer().started()) {
		if (line.noRingTimer().timeout())
		    line.noRingTimer().stop();
		else {
		    DDebug(this,DebugNote,
			"Ring timer still active on line (%p,%s) without channel [%p]",
			&line,line.address(),this);
		    // Restart the timer
		    line.noRingTimer().start();
		    return;
		}
	    }
	    AnalogChannel::RecordTrigger rec =
		(type() == AnalogLine::Recorder)
		    ? ((event.type() == SignallingCircuitEvent::RingBegin)
			? AnalogChannel::FXS : AnalogChannel::FXO)
		    : AnalogChannel::None;
	    ch = new AnalogChannel(&line,0,rec);
	    ch->initChan();
	    if (!ch->line())
		plugin.terminateChan(ch);
	}
	else
	    DDebug(this,DebugNote,
		"Event (%p,%u,%s) from line (%p,%s) without channel [%p]",
		&event,event.type(),event.c_str(),&line,line.address(),this);
}

// Process an event generated by a recorder
void ModuleGroup::handleRecEvent(ModuleLine& line, SignallingCircuitEvent& event)
{
    Lock lock(&plugin);
    AnalogCallRec* rec = static_cast<AnalogCallRec*>(line.userdata());
    DDebug(this,DebugInfo,"Processing event %u '%s' line=%s recorder=%s",
	event.type(),event.c_str(),line.address(),rec?rec->id().c_str():"");
    if (event.type() == SignallingCircuitEvent::OffHook)
	    line.noRingTimer().stop();
    if (rec) {
	// FXS event: our FXO receiver is watching the FXS end of the monitored line
	bool fxsEvent = (line.type() == AnalogLine::FXO);
	bool terminate = false;
	switch (event.type()) {
	    case SignallingCircuitEvent::Dtmf:
		rec->evDigits(fxsEvent,event.getValue("tone"),true);
		break;
	    case SignallingCircuitEvent::PulseDigit:
		rec->evDigits(fxsEvent,event.getValue("pulse"),false);
		break;
	    case SignallingCircuitEvent::OnHook:
		terminate = true;
		break;
	    case SignallingCircuitEvent::OffHook:
		terminate = !rec->answered();
		return;
	    case SignallingCircuitEvent::RingBegin:
	    case SignallingCircuitEvent::RingerOn:
		terminate = !rec->ringing(fxsEvent);
		break;
	    case SignallingCircuitEvent::Polarity:
		terminate = !rec->evPolarity(fxsEvent);
		break;
	    case SignallingCircuitEvent::Flash:
		rec->evDigits(fxsEvent,"F",true);
		break;
	    case SignallingCircuitEvent::Alarm:
	    case SignallingCircuitEvent::NoAlarm:
		terminate = !rec->evAlarm(fxsEvent,event.type() == SignallingCircuitEvent::Alarm,
		    event.getValue("alarms"));
		break;
	    case SignallingCircuitEvent::RingEnd:
	    case SignallingCircuitEvent::RingerOff:
	    case SignallingCircuitEvent::PulseStart:
	    case SignallingCircuitEvent::LineStarted:
	    case SignallingCircuitEvent::DialComplete:
	    case SignallingCircuitEvent::Wink:
		DDebug(rec,DebugAll,"Ignoring '%s' event [%p]",event.c_str(),rec);
		break;
	    default:
		Debug(this,DebugStub,"handleRecEvent(%u,'%s') not implemented [%p]",
		    event.type(),event.c_str(),this);
	}
	if (terminate) {
	    rec->hangup();
	    plugin.terminateChan(rec);
	}
	return;
    }

    // Check for new call
    bool fxsCaller = (line.type() == AnalogLine::FXO && event.type() == SignallingCircuitEvent::RingBegin);
    bool fxoCaller = (line.type() == AnalogLine::FXS && event.type() == SignallingCircuitEvent::OffHook);

    if (!(fxsCaller || fxoCaller)) {
	DDebug(this,DebugNote,
	    "Event (%p,%u,%s) from line (%p,%s) without recorder [%p]",
	    &event,event.type(),event.c_str(),&line,line.address(),this);
	return;
    }

    String id;
    id << plugin.recPrefix() << plugin.nextRecId();
    ModuleLine* fxs = (line.type() == AnalogLine::FXS ? &line : static_cast<ModuleLine*>(line.getPeer()));
    rec = new AnalogCallRec(fxs,fxsCaller,id);
    if (!(rec->line() && rec->fxo())) {
	plugin.terminateChan(rec,rec->reason());
	return;
    }
    if (rec->startOnSecondRing()) {
	DDebug(rec,DebugAll,"Delaying start until next ring [%p]",rec);
	return;
    }
    bool ok = true;
    if (fxsCaller || rec->fxo()->answerOnPolarity())
	ok = rec->startRecording();
    else
	ok = rec->answered();
    if (!ok) {
	rec->hangup();
	plugin.terminateChan(rec,rec->reason());
    }
}

// Apply debug level. Call create and create worker thread on first init
// Re(load) lines and calls specific group reload
bool ModuleGroup::initialize(const NamedList& params, const NamedList& defaults,
	String& error)
{
    if (!m_init)
	debugChain(&plugin);

    int level = params.getIntValue("debuglevel",m_init ? DebugEnabler::debugLevel() : plugin.debugLevel());
    if (level >= 0) {
	debugEnabled(0 != level);
	debugLevel(level);
    }

    m_ringback = params.getBoolValue("ringback");

    Lock2 lock(this,fxoRec());
    bool ok = true;
    if (!m_init) {
	m_init = true;
	if (!fxoRec())
	    ok = create(params,defaults,error);
	else
	    ok = createRecorder(params,defaults,error);
	if (!ok)
	    return false;
	m_thread = new AnalogWorkerThread(this);
	if (!m_thread->startup()) {
	    error = "Failed to start worker thread";
	    return false;
	}
    }

    // (Re)load analog lines
    bool all = params.getBoolValue("useallcircuits",true);

    unsigned int n = circuits().length();
    for (unsigned int i = 0; i < n; i++) {
	SignallingCircuit* cic = static_cast<SignallingCircuit*>(circuits()[i]);
	if (!cic)
	    continue;

	// Setup line parameter list
	NamedList dummy("");
	String sectName = s_lineSectPrefix + toString() + "/" + String(cic->code());
	NamedList* lineParams = s_cfg.getSection(sectName);
	if (!lineParams)
	    lineParams = &dummy;
	bool remove = !lineParams->getBoolValue("enable",true);

	ModuleLine* line = static_cast<ModuleLine*>(findLine(cic->code()));

	// Remove existing line if required
	if (remove) {
	    if (line) {
		XDebug(this,DebugAll,"Removing line=%s [%p]",line->address(),this);
		plugin.lineUnavailable(line);
		TelEngine::destruct(line);
	    }
	    continue;
	}

	// Reload line if already created. Notify plugin if service state changed
	completeLineParams(*lineParams,params,defaults);
	if (line) {
	    bool inService = (line->state() != AnalogLine::OutOfService);
	    reloadLine(line,*lineParams);
	    if (inService != (line->state() != AnalogLine::OutOfService))
		plugin.lineUnavailable(line);
	    continue;
	}

	// Don't create the line if useallcircuits is false and no section in config
	if (!all && lineParams == &dummy)
	    continue;

	DDebug(this,DebugAll,"Creating line for cic=%u [%p]",cic->code(),this);
	// Create a new line (create its peer if this is a monitor)
	line = new ModuleLine(this,cic->code(),*lineParams,params);
	while (fxoRec() && line->type() != AnalogLine::Unknown) {
	    SignallingCircuit* fxoCic = static_cast<SignallingCircuit*>(fxoRec()->circuits()[i]);
	    if (!fxoCic) {
		Debug(this,DebugNote,"FXO circuit is missing for %s/%u [%p]",
		    debugName(),cic->code(),this);
		TelEngine::destruct(line);
		break;
	    }

	    NamedList dummyFxo("");
	    String fxoName = s_lineSectPrefix + fxoRec()->toString() + "/" + String(fxoCic->code());
	    NamedList* fxoParams = s_cfg.getSection(fxoName);
	    if (!fxoParams)
		fxoParams = &dummyFxo;

	    completeLineParams(*fxoParams,params,defaults);

	    ModuleLine* fxoLine = new ModuleLine(fxoRec(),fxoCic->code(),*fxoParams,params);
	    if (fxoLine->type() == AnalogLine::Unknown) {
		TelEngine::destruct(fxoLine);
		TelEngine::destruct(line);
		break;
	    }
	    fxoRec()->appendLine(fxoLine);
	    line->setPeer(fxoLine);
	    break;
	}

	// Append line to group: constructor may fail
	if (line && line->type() != AnalogLine::Unknown) {
	    appendLine(line);
	    // Disconnect the line if not expecting call setup
	    if (line->callSetup() != AnalogLine::Before)
		line->disconnect(true);
	}
	else {
	    Debug(this,DebugNote,"Failed to create line %s/%u [%p]",
		debugName(),cic->code(),this);
	    TelEngine::destruct(line);
	}
    }

    if (!fxoRec())
	ok = reload(params,defaults,error);
    else
	ok = reloadRecorder(params,defaults,error);
    return ok;
}

// Copy some data to a channel
void ModuleGroup::copyData(AnalogChannel* chan)
{
    if (!chan || fxoRec())
	return;
    chan->m_callEndedTarget = m_callEndedTarget;
    chan->m_oooTarget = m_oooTarget;
    if (!chan->m_lang)
	chan->m_lang = m_lang;
    chan->m_callEndedTimer.interval(m_callEndedPlayTime);
}

// Append/remove endpoints from list
void ModuleGroup::setEndpoint(CallEndpoint* ep, bool add)
{
    if (!ep)
	return;
    Lock lock(this);
    if (add)
	m_endpoints.append(ep);
    else
	m_endpoints.remove(ep,false);
}

// Find a recorder by its line
AnalogCallRec* ModuleGroup::findRecorder(ModuleLine* line)
{
    if (!fxoRec())
	return 0;
    Lock lock(this);
    for (ObjList* o = m_endpoints.skipNull(); o; o = o->skipNull()) {
	AnalogCallRec* rec = static_cast<AnalogCallRec*>(o->get());
	if (rec->line() == line)
	    return rec;
    }
    return 0;
}

// Fill a string with group status parameters
void ModuleGroup::statusParams(String& str)
{
    str.append("module=",";") << plugin.name();
    str << ",name=" << toString();
    str << ",type=" << lookup(!fxo()?type():AnalogLine::Monitor,AnalogLine::typeNames());
    str << ",lines=" << lines().count();
    str << "," << s_lineStatusDetail;
    for (ObjList* o = lines().skipNull(); o; o = o->skipNext())
	(static_cast<ModuleLine*>(o->get()))->statusDetail(str);
}

// Fill a string with group status detail parameters
void ModuleGroup::statusDetail(String& str)
{
    // format=Type|Lines
    Lock lock(this);
    str.append(toString(),";") << "=";
    str << lookup(!fxo()?type():AnalogLine::Monitor,AnalogLine::typeNames());
    str << "|" << lines().count();
}

// Disconnect all group's endpoints
void ModuleGroup::clearEndpoints(const char* reason)
{
    if (!reason)
	reason = "shutdown";

    DDebug(this,DebugAll,"Clearing endpoints with reason=%s [%p]",reason,this);
    bool chans = !fxoRec();
    lock();
    ListIterator iter(m_endpoints);
    for (;;) {
	RefPointer<CallEndpoint> c = static_cast<CallEndpoint*>(iter.get());
	unlock();
	if (!c)
	    break;
	if (chans)
	    plugin.terminateChan(static_cast<AnalogChannel*>((CallEndpoint*)c),reason);
	else
	    plugin.terminateChan(static_cast<AnalogCallRec*>((CallEndpoint*)c),reason);
	c = 0;
	lock();
    }
}

// Check timers for recorders owned by this group
void ModuleGroup::checkTimers(Time& when)
{
    bool chans = !fxoRec();
    lock();
    ListIterator iter(m_endpoints);
    for (;;) {
	RefPointer<CallEndpoint> c = static_cast<CallEndpoint*>(iter.get());
	unlock();
	if (!c)
	    break;
	if (chans) {
	    AnalogChannel* ch = static_cast<AnalogChannel*>((CallEndpoint*)c);
	    if (!ch->checkTimeouts(when))
		plugin.terminateChan(ch,"timeout");
	}
	else {
	    AnalogCallRec* ch = static_cast<AnalogCallRec*>((CallEndpoint*)c);
	    if (!ch->checkTimeouts(when))
		plugin.terminateChan(ch,"timeout");
	}
	c = 0;
	lock();
    }
}

// Create FXS/FXO group data: called by initialize() on first init
bool ModuleGroup::create(const NamedList& params, const NamedList& defaults,
	String& error)
{
    String device = params.getValue("spans");
    ObjList* voice = device.split(',',false);
    if (voice && voice->count())
	buildGroup(this,*voice,error);
    else
	error << "Missing or invalid spans=" << device;
    TelEngine::destruct(voice);
    if (error)
	return false;
    return true;
}

// Reload FXS/FXO data: called by initialize() (not called on first init if create failed)
bool ModuleGroup::reload(const NamedList& params, const NamedList& defaults,
	String& error)
{
    // (Re)load tone targets
    if (type() == AnalogLine::FXS) {
	int tmp = params.getIntValue("call-ended-playtime",
	    defaults.getIntValue("call-ended-playtime",5));
	if (tmp < 0)
	    tmp = 5;
	m_callEndedPlayTime = 1000 * (unsigned int)tmp;
	m_callEndedTarget = params.getValue("call-ended-target",
	    defaults.getValue("call-ended-target"));
	if (!m_callEndedTarget)
	    m_callEndedTarget = "tone/busy";
	m_oooTarget = params.getValue("outoforder-target",
	    defaults.getValue("outoforder-target"));
	if (!m_oooTarget)
	    m_oooTarget = "tone/outoforder";
	m_lang = params.getValue("lang",defaults.getValue("lang"));
	XDebug(this,DebugAll,"Targets: call-ended='%s' outoforder='%s' [%p]",
	    m_callEndedTarget.c_str(),m_oooTarget.c_str(),this);
    }
    return true;
}

// Create recorder group data: called by initialize() on first init
bool ModuleGroup::createRecorder(const NamedList& params, const NamedList& defaults,
	String& error)
{
    for (unsigned int i = 0; i < 2; i++) {
	String device = params.getValue(callertype(0 != i));
	ObjList* voice = device.split(',',false);
	if (voice && voice->count())
	    if (i)
		buildGroup(this,*voice,error);
	    else
		buildGroup(fxoRec(),*voice,error);
	else
	    error << "Missing or invalid " << callertype(0 != i) << " spans=" << device;
	TelEngine::destruct(voice);
	if (error)
	    return false;
    }
    return true;
}

// Reload recorder data: called by initialize() (not called on first init if create failed)
bool ModuleGroup::reloadRecorder(const NamedList& params, const NamedList& defaults,
	String& error)
{
    return true;
}

// Reload existing line's parameters
void ModuleGroup::reloadLine(ModuleLine* line, const NamedList& params)
{
    if (!line)
	return;
    bool inService = !params.getBoolValue("out-of-service",false);
    if (inService == (line->state() != AnalogLine::OutOfService))
	return;
    Lock lock(line);
    Debug(this,DebugAll,"Reloading line %s in-service=%s [%p]",line->address(),String::boolText(inService),this);
    line->ref();
    line->enable(inService,true);
    line->deref();
}

// Build the circuit list for a given group
void ModuleGroup::buildGroup(ModuleGroup* group, ObjList& spanList, String& error)
{
    if (!group)
	return;
    unsigned int start = 0;
    for (ObjList* o = spanList.skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	if (s->null())
	    continue;
	SignallingCircuitSpan* span = buildSpan(*s,start);
	if (!span) {
	    error << "Failed to build span '" << *s << "'";
	    break;
	}
	start += span->increment();
    }
}


/**
 * AnalogChannel
 */
// Incoming: msg=0. Outgoing: msg is the call execute message
AnalogChannel::AnalogChannel(ModuleLine* line, Message* msg, RecordTrigger recorder)
    : Channel(&plugin,0,(msg != 0)),
    m_line(line),
    m_hungup(false),
    m_ringback(false),
    m_routeOnSecondRing(false),
    m_recording(recorder),
    m_callEndedTimer(0),
    m_ringTimer(RING_PATTERN_TIME),
    m_alarmTimer(line ? line->alarmTimeout() : 0),
    m_dialTimer(0),
    m_polarityCount(0),
    m_polarity(false),
    m_privacy(false),
    m_callsetup(AnalogLine::NoCallSetup)
{
    m_line->userdata(this);
    if (m_line->moduleGroup()) {
	m_line->moduleGroup()->setEndpoint(this,true);
	m_ringback = m_line->moduleGroup()->ringback();
    }

    // Set caller/called from line
    if (isOutgoing()) {
	m_lang = msg->getValue("lang");
	m_line->setCall(msg->getValue("caller"),msg->getValue("callername"),msg->getValue("called"));
    }
    else
	if ((m_line->type() == AnalogLine::FXS) || (recorder == FXO))
	    m_line->setCall("","","off-hook");
	else
	    m_line->setCall("","","ringing");

    const char* mode = 0;
    switch (recorder) {
	case FXO:
	    mode = "Record FXO";
	    break;
	case FXS:
	    mode = "Record FXS";
	    break;
	default:
	    mode = isOutgoing() ? "Outgoing" : "Incoming";
    }
    Debug(this,DebugCall,"%s call on line %s caller=%s called=%s [%p]",
	mode,
	m_line->address(),
	m_line->caller().c_str(),m_line->called().c_str(),this);

    m_line->connect(false);
    m_line->acceptPulseDigit(isIncoming());

    // Incoming on FXO:
    // Caller id after first ring: delay router until the second ring and
    //  set/remove call setup detector
    if (isIncoming() && m_line->type() == AnalogLine::FXO && recorder != FXO) {
	m_routeOnSecondRing = (m_line->callSetup() == AnalogLine::After);
	if (m_routeOnSecondRing)
	    m_line->setCallSetupDetector();
	else
	    m_line->removeCallSetupDetector();
    }

    m_address = m_line->address();
    if (m_line->type() == AnalogLine::FXS && m_line->moduleGroup())
	m_line->moduleGroup()->copyData(this);

    setMaxcall(msg);
    if (msg)
	setMaxPDD(*msg);
    // Startup
    Message* m = message("chan.startup");
    m->setParam("direction",status());
    if (msg)
	m->copyParams(*msg,"caller,callername,called,billid,callto,username");
    m_line->copyCall(*m);
    if (isOutgoing())
	m_targetid = msg->getValue("id");
    Engine::enqueue(m);

    // Init call
    setAudio(isIncoming());
    if (isOutgoing()) {
	// Check for parameters override
	m_dialTimer.interval(msg->getIntValue("delaydial",0));
	// FXO: send start line event
	// FXS: start ring and send call setup (caller id)
	// Return if failed to send events
	switch (line->type()) {
	    case AnalogLine::FXO:
		m_line->sendEvent(SignallingCircuitEvent::StartLine,AnalogLine::Dialing);
		break;
	    case AnalogLine::FXS:
		m_callsetup = m_line->callSetup();
		// Check call setup override
		{
		    NamedString* ns = msg->getParam("callsetup");
		    if (ns)
			m_callsetup = lookup(*ns,AnalogLine::csNames(),AnalogLine::NoCallSetup);
		}
		m_privacy = getPrivacy(*msg);
		if (m_callsetup == AnalogLine::Before)
		    m_line->sendCallSetup(m_privacy);
		{
		    NamedList* params = 0;
		    NamedList callerId("");
		    if (m_callsetup != AnalogLine::NoCallSetup) {
			params = &callerId;
			m_line->copyCall(callerId,m_privacy);
		    }
		    m_line->sendEvent(SignallingCircuitEvent::RingBegin,AnalogLine::Dialing,params);
		}
		if (m_callsetup == AnalogLine::After)
		    m_dialTimer.interval(500);
		break;
	    default: ;
	}
	if (line->state() == AnalogLine::Idle) {
	    setReason("failure");
	    msg->setParam("error",m_reason);
	    return;
	}
    }
    else {
	m_line->changeState(AnalogLine::Dialing);

	// FXO: start ring timer (check if the caller hangs up before answer)
	// FXS: do nothing
	switch (line->type()) {
	    case AnalogLine::FXO:
		if (recorder == FXO) {
		    m_line->noRingTimer().stop();
		    break;
		}
		m_line->noRingTimer().interval(m_line->noRingTimeout());
		DDebug(this,DebugAll,"Starting ring timer for " FMT64 "ms [%p]",
		    m_line->noRingTimer().interval(),this);
		m_line->noRingTimer().start();
		if (recorder == FXS) {
		    // The FXS recorder will route only on off-hook
		    m_routeOnSecondRing = false;
		    return;
		}
		break;
	    case AnalogLine::FXS:
		break;
	    default: ;
	}
	if (!m_routeOnSecondRing)
	    startRouter(true);
	else
	    DDebug(this,DebugInfo,"Delaying route until next ring [%p]",this);
    }
}

AnalogChannel::~AnalogChannel()
{
    XDebug(this,DebugCall,"AnalogChannel::~AnalogChannel() [%p]",this);
}

// Start outgoing media and echo train if earlymedia or got peer with data source
bool AnalogChannel::msgProgress(Message& msg)
{
    Lock lock(m_mutex);
    if (isAnswered())
	return true;

    Channel::msgProgress(msg);
    setStatus();
    if (m_line && m_line->type() != AnalogLine::FXS)
	m_line->acceptPulseDigit(false);
    if (msg.getBoolValue("earlymedia",getPeer() && getPeer()->getSource())) {
	setAudio(false);
	if (m_line)
	    m_line->setCircuitParam("echotrain",msg.getValue("echotrain"));
    }
    return true;
}

// Start outgoing media and echo train if earlymedia or got peer with data source
bool AnalogChannel::msgRinging(Message& msg)
{
    Lock lock(m_mutex);
    if (isAnswered())
	return true;

    Channel::msgRinging(msg);
    setStatus();
    if (m_line) {
	if (m_line->type() != AnalogLine::FXS)
	    m_line->acceptPulseDigit(false);
	m_line->changeState(AnalogLine::Ringing);
    }
    bool media = msg.getBoolValue("earlymedia",getPeer() && getPeer()->getSource());
    if (media) {
	setAudio(false);
	if (m_line)
	    m_line->setCircuitParam("echotrain",msg.getValue("echotrain"));
    }
    else if (m_ringback && m_line) {
	// Provide ringback from circuit features if supported
	NamedList params("ringback");
	params.addParam("tone","ringback");
	media = m_line->sendEvent(SignallingCircuitEvent::GenericTone,&params);
    }
    if (media)
	m_ringback = false;
    return true;
}

// Terminate ringing on line. Start echo train. Open audio streams
bool AnalogChannel::msgAnswered(Message& msg)
{
    Lock lock(m_mutex);
    if (m_line) {
	m_line->noRingTimer().stop();
	m_line->removeCallSetupDetector();
	m_line->sendEvent(SignallingCircuitEvent::RingEnd);
	if (m_line->type() == AnalogLine::FXS)
	    polarityControl(true);
	else {
	    m_line->acceptPulseDigit(false);
	    m_line->sendEvent(SignallingCircuitEvent::OffHook);
	}
	m_line->changeState(AnalogLine::Answered);
	m_line->setCircuitParam("echotrain",msg.getValue("echotrain"));
    }
    setAudio(true);
    setAudio(false);
    Channel::msgAnswered(msg);
    setStatus();
    return true;
}

// Send tones or flash
bool AnalogChannel::msgTone(Message& msg, const char* tone)
{
    Lock lock(m_mutex);
    if (!(tone && *tone && m_line))
	return false;
    if (*tone != 'F') {
	if (m_dialTimer.started()) {
	    Debug(this,DebugAll,"msgTone(%s). Adding to called number [%p]",tone,this);
	    m_line->called().append(tone);
	    return true;
	}
	return sendTones(tone,false);
    }
    // Flash event: don't send if not FXO
    if (m_line->type() != AnalogLine::FXO) {
	Debug(this,DebugInfo,"Can't send line flash on non-FXO line (tones='%s') [%p]",tone,this);
	return false;
    }
    Debug(this,DebugAll,"Sending line flash (tones='%s') [%p]",tone,this);
    return m_line->sendEvent(SignallingCircuitEvent::Flash);
}

// Hangup
bool AnalogChannel::msgDrop(Message& msg, const char* reason)
{
    Lock lock(m_mutex);
    setReason(reason ? reason : "dropped");
    if (Engine::exiting() || !m_line || m_line->type() != AnalogLine::FXS)
	Channel::msgDrop(msg,m_reason);
    hangup(true);
    return true;
}

// Update echo canceller and/or start echo training
bool AnalogChannel::msgUpdate(Message& msg)
{
    String tmp = msg.getValue("echocancel");
    Lock lock(m_mutex);
    if (!(tmp.isBoolean() && m_line))
	return false;
    bool ok = m_line->setCircuitParam("echocancel",tmp);
    if (tmp.toBoolean())
	m_line->setCircuitParam("echotrain",msg.getValue("echotrain"));
    return ok;
}

// Call routed: set tone detector
bool AnalogChannel::callRouted(Message& msg)
{
    Channel::callRouted(msg);
    setStatus();
    Lock lock(m_mutex);
    // Update tones language
    m_lang = msg.getValue("lang",m_lang);
    // Check if the circuit supports tone detection
    if (!m_line->circuit())
	return true;
    String value;
    if (m_line->circuit()->getParam("tonedetect",value) && value.toBoolean())
	return true;
    // Set tone detector
    setAudio(false);
    if (toneDetect())
	DDebug(this,DebugAll,"Loaded tone detector [%p]",this);
    else {
	setConsumer();
	DDebug(this,DebugNote,"Failed to set tone detector [%p]",this);
    }
    return true;
}

// Call accepted: set line and open audio
void AnalogChannel::callAccept(Message& msg)
{
    Lock lock(m_mutex);
    // Update tones language
    m_lang = msg.getValue("lang",m_lang);
    if (isAnswered())
	return;
    if (m_line) {
	if (m_line->type() != AnalogLine::FXS)
	    m_line->acceptPulseDigit(false);
	m_line->changeState(AnalogLine::DialComplete);
    }
    m_ringback = msg.getBoolValue("ringback",m_ringback);
    setAudio(false);
    setAudio(true);
    Channel::callAccept(msg);
}

// Call rejected: hangup
void AnalogChannel::callRejected(const char* error, const char* reason,
	const Message* msg)
{
    if (msg) {
	Lock lock(m_mutex);
	m_lang = msg->getValue("lang",m_lang);
    }
    setReason(error ? error : reason);
    Channel::callRejected(error,m_reason,msg);
    setStatus();
    hangup(true);
}

void AnalogChannel::disconnected(bool final, const char* reason)
{
    Lock lock(m_mutex);
    Channel::disconnected(final,m_reason);
    hangup(!final,"disconnected",reason);
}

// Disconnect the channel
bool AnalogChannel::disconnect(const char* reason)
{
    Lock lock(m_mutex);
    if (!m_hungup) {
	setReason(reason);
	setStatus("disconnecting");
    }
    return Channel::disconnect(m_reason,parameters());
}

// Hangup call
// Keep call alive to play announcements on FXS line not set on hook by the remote FXO
void AnalogChannel::hangup(bool local, const char* status, const char* reason)
{
    // Sanity: reset dial timer and call setup flag if FXS
    m_dialTimer.stop();
    m_callsetup = AnalogLine::NoCallSetup;

    Lock lock(m_mutex);

    if (m_hungup)
	return;
    m_hungup = true;
    setReason(reason ? reason : (Engine::exiting() ? "shutdown" : "normal"));
    if (status)
	setStatus(status);
    setSource();
    setConsumer();

    Message* m = message("chan.hangup",true);
    m->setParam("status",this->status());
    m->setParam("reason",m_reason);
    Engine::enqueue(m);

    setStatus("hangup");
    if (m_line && m_line->state() != AnalogLine::Idle)
	m_line->sendEvent(SignallingCircuitEvent::RingEnd);
    polarityControl(false);

    // Check some conditions to keep the channel
    if (!m_line || m_line->type() != AnalogLine::FXS ||
	!local || Engine::exiting() ||
	(isOutgoing() && m_line->state() < AnalogLine::Answered) ||
	(isIncoming() && m_line->state() == AnalogLine::Idle))
	return;

    Debug(this,DebugAll,"Call ended. Keep channel alive [%p]",this);
    if (m_callEndedTimer.interval()) {
	m_callEndedTimer.start();
	m_line->changeState(AnalogLine::CallEnded);
	if (!setAnnouncement("call-ended",m_callEndedTarget))
	    ref();
    }
    else {
	m_line->changeState(AnalogLine::OutOfOrder);
	if (!setAnnouncement("out-of-order",m_oooTarget))
	    ref();
    }
}

// Process incoming or outgoing digits
void AnalogChannel::evDigits(const char* text, bool tone)
{
    if (!(text && *text))
	return;
    Debug(this,DebugAll,"Got %s digits=%s [%p]",tone?"tone":"pulse",text,this);
    Message* m = message("chan.dtmf",false,true);
    m->addParam("text",text);
    if (!tone)
	m->addParam("pulse",String::boolText(true));
    m->addParam("detected","analog");
    dtmfEnqueue(m);
}

// Line got off hook. Terminate ringing
// Outgoing: answer it (call outCallAnswered())
// Incoming: start echo train
void AnalogChannel::evOffHook()
{
    Lock lock(m_mutex);
    if (isOutgoing()) {
	outCallAnswered();
	if (m_line)
	    m_line->sendEvent(SignallingCircuitEvent::RingEnd,AnalogLine::Answered);
    }
    else if (m_line) {
	m_line->sendEvent(SignallingCircuitEvent::RingEnd,m_line->state());
	m_line->setCircuitParam("echotrain");
	if (m_recording == FXS)
	    startRouter(true);
    }
}

// Line ring on/off notification. Ring off is ignored
// Outgoing: enqueue call.ringing
// Incoming: FXO: Route the call if delayed. Remove line's detector and start ring timer
void AnalogChannel::evRing(bool on)
{
    Lock lock(m_mutex);

    // Re(start) ring timer. Ignore ring events if timer was already started
    if (on) {
	bool ignore = m_ringTimer.started();
	m_ringTimer.start();
	if (ignore)
	    return;
    }

    // Check call setup
    if (m_callsetup == AnalogLine::After) {
	if (on)
	    m_dialTimer.stop();
	else
	    m_dialTimer.start();
    }

    // Done if ringer is off
    if (!on)
	return;

    // Outgoing: remote party is ringing
    if (isOutgoing()) {
	Engine::enqueue(message("call.ringing",false,true));
	if (m_line)
	    m_line->changeState(AnalogLine::Ringing);
	return;
    }
    // Incoming: start ringing (restart FXO timer to check remote hangup)
    // Start router if delayed
    if (!m_line)
	return;
    if (m_line->type() == AnalogLine::FXO) {
	if (m_routeOnSecondRing) {
	    m_routeOnSecondRing = false;
	    startRouter(false);
	}
	m_line->removeCallSetupDetector();
	if (m_line->noRingTimer().interval()) {
	    DDebug(this,DebugAll,"Restarting ring timer for " FMT64 "ms [%p]",
		m_line->noRingTimer().interval(),this);
	    m_line->noRingTimer().start();
	}
    }
}

// Line started (initialized) notification
// Answer outgoing FXO calls on lines not expecting polarity changes to answer
// Send called number if any
void AnalogChannel::evLineStarted()
{
    Lock lock(m_mutex);
    if (!m_line)
	return;
    // Send number: delay it if interval is not 0
    bool stopDial = true;
    if (m_line->called()) {
	if (m_line->delayDial() || m_dialTimer.interval()) {
	    if (!m_dialTimer.started()) {
		if (!m_dialTimer.interval())
		    m_dialTimer.interval(m_line->delayDial());
		DDebug(this,DebugAll,"Delaying dial for " FMT64 "ms [%p]",
		    m_dialTimer.interval(),this);
		m_dialTimer.start();
	    }
	    stopDial = false;
	}
	else
	    sendTones(m_line->called());
    }

    // Answer now outgoing FXO calls on lines not expecting polarity changes to answer
    if (isOutgoing() && m_line && m_line->type() == AnalogLine::FXO &&
	!m_line->answerOnPolarity())
	outCallAnswered(stopDial);
}

// Dial complete notification. Enqueue call.progress
// Answer outgoing FXO calls on lines not expecting polarity changes to answer
void AnalogChannel::evDialComplete()
{
    DDebug(this,DebugAll,"Dial completed [%p]",this);
    Lock lock(m_mutex);
    if (m_line)
	m_line->changeState(AnalogLine::DialComplete);
    Engine::enqueue(message("call.progress",true,true));
    // Answer now outgoing FXO calls on lines not expecting polarity changes to answer
    if (isOutgoing() && m_line && m_line->type() == AnalogLine::FXO &&
	!m_line->answerOnPolarity())
	outCallAnswered();
}

// Line polarity change notification
// Terminate call if:
//   - no line or line is not FXO,
//   - Outgoing: don't answer on polarity or already answered and should hangup on polarity change
//   - Incoming: don't answer on polarity or polarity already changed and should hangup on polarity change
void AnalogChannel::evPolarity()
{
    Lock lock(m_mutex);
    m_polarityCount++;
    DDebug(this,DebugAll,"Line polarity changed %u time(s) [%p]",m_polarityCount,this);
    bool terminate = (!m_line || m_line->type() != AnalogLine::FXO);
    if (!terminate) {
	if (isOutgoing())
	    if (!m_line->answerOnPolarity() || isAnswered())
		terminate = m_line->hangupOnPolarity();
	    else
		outCallAnswered();
	else if (!m_line->answerOnPolarity() || m_polarityCount > 1)
	    terminate = m_line->hangupOnPolarity();
    }

    if (terminate) {
	DDebug(this,DebugAll,"Terminating on polarity change [%p]",this);
	hangup(false);
	plugin.terminateChan(this);
    }
}

// Line ok: stop alarm timer
// Terminate channel if not answered; otherwise: start timer if not already started
void AnalogChannel::evAlarm(bool alarm, const char* alarms)
{
    Lock lock(m_mutex);
    if (!alarm) {
	Debug(this,DebugInfo,"No more alarms on line [%p]",this);
	if (m_line)
	    m_line->setCircuitParam("echotrain");
	m_alarmTimer.stop();
	return;
    }
    // Terminate now if not answered
    if (!isAnswered()) {
	Debug(this,DebugNote,"Line is out of order alarms=%s. Terminating now [%p]",
	    alarms,this);
	hangup(false,0,"net-out-of-order");
	plugin.terminateChan(this);
	return;
    }
    // Wait if answered
    if (!m_alarmTimer.started()) {
	Debug(this,DebugNote,
	    "Line is out of order alarms=%s. Starting timer for " FMT64U " ms [%p]",
	    alarms,m_alarmTimer.interval(),this);
	m_alarmTimer.start();
    }
}

// Check timers. Return false to terminate
bool AnalogChannel::checkTimeouts(const Time& when)
{
    Lock lock(m_mutex);
    // Stop ring timer: we didn't received a ring event in the last interval
    if (m_ringTimer.timeout(when.msecNow()))
	m_ringTimer.stop();
    if (m_alarmTimer.timeout(when.msecNow())) {
	m_alarmTimer.stop();
	DDebug(this,DebugInfo,"Line was in alarm for " FMT64 " ms [%p]",
	    m_alarmTimer.interval(),this);
	setReason("net-out-of-order");
	hangup(false);
	return false;
    }
    if (m_callEndedTimer.timeout(when.msecNow())) {
	m_callEndedTimer.stop();
	m_line->changeState(AnalogLine::OutOfOrder);
	disconnect();
	if (!setAnnouncement("out-of-order",m_oooTarget))
	    ref();
	return true;
    }
    if (m_line->noRingTimer().timeout(when.msecNow())) {
	DDebug(this,DebugInfo,"No ring for " FMT64 " ms. Terminating [%p]",
	    m_line->noRingTimer().interval(),this);
	m_line->noRingTimer().stop();
	setReason("cancelled");
	hangup(false);
	return false;
    }
    if (m_dialTimer.timeout(when.msecNow())) {
	m_dialTimer.stop();
	m_callsetup = AnalogLine::NoCallSetup;
	DDebug(this,DebugInfo,"Dial timer expired. %s [%p]",
	    m_line?"Sending number/callsetup":"Line is missing",this);
	if (!m_line)
	    return true;
	if (m_line->type() == AnalogLine::FXO)
	    sendTones(m_line->called());
	else if (m_line->type() == AnalogLine::FXS)
	    m_line->sendCallSetup(m_privacy);
	return true;
    }
    return true;
}

// Route incoming
void AnalogChannel::startRouter(bool first)
{
    m_routeOnSecondRing = false;
    Message* m = message("call.preroute",false,true);
    if (m_line) {
	m_line->copyCall(*m);
	const char* caller = m->getValue("caller");
	if (!(caller && *caller))
	    m->setParam("caller",s_unk);
	switch (m_line->type()) {
	    case AnalogLine::FXO:
		if (getSource())
		    m->addParam("format",getSource()->getFormat());
		break;
	    case AnalogLine::FXS:
		m->addParam("overlapped","true");
		m->addParam("lang",m_lang,false);
		break;
	    default: ;
	}
    }
    switch (m_recording) {
	case FXO:
	    m->addParam("callsource","fxo");
	    break;
	case FXS:
	    m->addParam("callsource","fxs");
	    break;
	default: ;
    }
    DDebug(this,DebugInfo,"Starting router %scaller=%s callername=%s [%p]",
	first?"":"(delayed) ",
	m->getValue("caller"),m->getValue("callername"),this);
    Channel::startRouter(m);
}

// Set data source and consumer
bool AnalogChannel::setAudio(bool in)
{
    if ((in && getSource()) || (!in && getConsumer()))
	return true;
    if ((m_recording != None) && !in)
	return true;

    SignallingCircuit* cic = m_line ? m_line->circuit() : 0;
    if (cic) {
	if (in)
	    setSource(static_cast<DataSource*>(cic->getObject(YATOM("DataSource"))));
	else
	    setConsumer(static_cast<DataConsumer*>(cic->getObject(YATOM("DataConsumer"))));
    }

    DataNode* res = in ? (DataNode*)getSource() : (DataNode*)getConsumer();
    if (res)
	DDebug(this,DebugAll,"Data %s set to (%p): '%s' [%p]",
	    in?"source":"consumer",res,res->getFormat().c_str(),this);
    else
	Debug(this,DebugNote,"Failed to set data %s%s [%p]",
	    in?"source":"consumer",cic?"":". Circuit is missing",this);
    return res != 0;
}

// Set call status
bool AnalogChannel::setStatus(const char* newStat)
{
    if (newStat)
	status(newStat);
    if (m_reason)
	Debug(this,DebugCall,"status=%s reason=%s [%p]",
	    status().c_str(),m_reason.c_str(),this);
    else
	Debug(this,DebugCall,"status=%s [%p]",status().c_str(),this);
    return true;
}

// Set tones to the remote end of the line
bool AnalogChannel::setAnnouncement(const char* status, const char* callto)
{
    setStatus(status);
    // Don't set announcements for FXO
    if (!m_line || m_line->type() == AnalogLine::FXO)
	return false;
    Message* m = message("call.execute",false,true);
    m->addParam("callto",callto);
    m->addParam("lang",m_lang,false);
    bool ok = Engine::dispatch(*m);
    TelEngine::destruct(m);
    if (ok) {
	setAudio(false);
	Debug(this,DebugAll,"Announcement set to %s",callto);
    }
    else
	Debug(this,DebugMild,"Set announcement=%s failed",callto);
    return ok;
}

// Outgoing call answered: set call state, start echo train, open data source/consumer
void AnalogChannel::outCallAnswered(bool stopDial)
{
    // Sanity: reset dial timer and call setup flag if FXS
    if (m_line && m_line->type() == AnalogLine::FXS) {
	m_dialTimer.stop();
	m_callsetup = AnalogLine::NoCallSetup;
    }

    if (isAnswered())
	return;

    if (stopDial)
	m_dialTimer.stop();
    m_answered = true;
    m_ringback = false;
    setStatus("answered");
    if (m_line) {
	m_line->changeState(AnalogLine::Answered);
	polarityControl(true);
	m_line->setCircuitParam("echotrain");
    }
    setAudio(true);
    setAudio(false);
    Engine::enqueue(message("call.answered",false,true));
}

// Hangup. Release memory
void AnalogChannel::destroyed()
{
    detachLine();
    if (!m_hungup)
	hangup(true);
    else {
	setConsumer();
	setSource();
    }
    setStatus("destroyed");
    Channel::destroyed();
}

// Detach the line from this channel
void AnalogChannel::detachLine()
{
    Lock lock(m_mutex);
    if (!m_line)
	return;

    if (m_line->moduleGroup())
	m_line->moduleGroup()->setEndpoint(this,false);
    m_line->userdata(0);
    m_line->acceptPulseDigit(true);
    if (m_line->state() != AnalogLine::Idle) {
	m_line->sendEvent(SignallingCircuitEvent::RingEnd);
	m_line->sendEvent(SignallingCircuitEvent::OnHook);
	m_line->changeState(AnalogLine::Idle);
    }
    m_line->removeCallSetupDetector();
    m_line->setCall();
    polarityControl(false);

    // Don't disconnect the line if waiting for call setup (need audio)
    if (m_line->type() == AnalogLine::FXO && m_line->callSetup() == AnalogLine::Before)
	m_line->setCallSetupDetector();
    else
	m_line->disconnect(false);
    TelEngine::destruct(m_line);
}

// Send tones (DTMF or dial number)
bool AnalogChannel::sendTones(const char* tone, bool dial)
{
    if (!(m_line && tone && *tone))
	return false;
    DDebug(this,DebugInfo,"Sending %sband tones='%s' dial=%u [%p]",
	m_line->outbandDtmf()?"out":"in",tone,dial,this);
    bool ok = false;
    if (m_line->outbandDtmf()) {
	NamedList p("");
	p.addParam("tone",tone);
	p.addParam("dial",String::boolText(dial));
	ok = m_line->sendEvent(SignallingCircuitEvent::Dtmf,&p);
    }
    if (!ok)
	ok = dtmfInband(tone);
    return ok;
}


/**
 * AnalogCallRec
 */
// Append to driver's list
AnalogCallRec::AnalogCallRec(ModuleLine* line, bool fxsCaller, const char* id)
    : CallEndpoint(id),
    m_line(line),
    m_fxsCaller(fxsCaller),
    m_answered(false),
    m_hungup(false),
    m_polarityCount(0),
    m_startOnSecondRing(false),
    m_ringTimer(RING_PATTERN_TIME),
    m_status("startup")
{
    debugName(CallEndpoint::id());
    debugChain(&plugin);

    ModuleLine* fxo = this->fxo();
    if (!(fxo && m_line->ref())) {
	m_line = 0;
	m_reason = "invalid-line";
	return;
    }

    plugin.setRecorder(this,true);
    if (m_line->moduleGroup())
	m_line->moduleGroup()->setEndpoint(this,true);
    m_line->userdata(this);

    m_line->connect(true);
    m_line->changeState(AnalogLine::Dialing,true);
    m_line->acceptPulseDigit(fxsCaller);
    fxo->acceptPulseDigit(!fxsCaller);

    // FXS caller:
    // Caller id after first ring: delay router until the second ring and
    //  set/remove call setup detector
    if (fxsCaller) {
	m_startOnSecondRing = (fxo->callSetup() == AnalogLine::After);
	if (m_startOnSecondRing)
	    fxo->setCallSetupDetector();
	else
	    fxo->removeCallSetupDetector();
    }

    if (fxsCaller && m_line->getPeer())
	m_address = m_line->getPeer()->address();
    else
	m_address = m_line->address();

    // Set caller/called
    if (fxsCaller) {
	if (m_startOnSecondRing && fxo->callSetup() == AnalogLine::Before)
	    fxo->setCall(fxo->caller(),"",m_line->called());
	else
	    fxo->setCall(s_unk,"",m_line->called());
    }
    else
	m_line->setCall(s_unk,"",fxo->called());

    Debug(this,DebugCall,"Created addr=%s initiator=%s [%p]",
	m_address.c_str(),callertype(fxsCaller),this);

    Engine::enqueue(message("chan.startup"));

    if (fxsCaller) {
	fxo->noRingTimer().interval(fxo->noRingTimeout());
	DDebug(this,DebugAll,"Starting ring timer for " FMT64 "ms [%p]",
	    fxo->noRingTimer().interval(),this);
	fxo->noRingTimer().start();
    }
}

// Close recorder. Disconnect the line
void AnalogCallRec::hangup(const char* reason)
{
    Lock lock(m_mutex);
    if (m_hungup)
	return;

    m_hungup = true;
    m_status = "hangup";
    if (!m_reason)
	m_reason = reason;
    if (!m_reason)
	m_reason = Engine::exiting() ? "shutdown" : "unknown";

    Debug(this,DebugCall,"Hangup reason='%s' [%p]",m_reason.c_str(),this);
    setSource();
    Engine::enqueue(message("chan.hangup",false));

    // Disconnect lines
    if (!m_line)
	return;

    ModuleLine* peer = fxo();
    bool sync = !(peer && peer->callSetup() == AnalogLine::Before);

    m_line->changeState(AnalogLine::Idle,true);
    m_line->disconnect(sync);
    m_line->acceptPulseDigit(true);
    m_line->setCall();

    if (peer) {
	if (!sync)
	    peer->setCallSetupDetector();
	peer->acceptPulseDigit(true);
	peer->setCall();
    }
}

bool AnalogCallRec::disconnect(const char* reason)
{
    Debug(this,DebugCall,"Disconnecting reason='%s' [%p]",reason,this);
    hangup(reason);
    return CallEndpoint::disconnect(m_reason);
}

// Get source(s) and other objects
// DataSource0: caller's source
// DataSource1: called's source
void* AnalogCallRec::getObject(const String& name) const
{
    int who = (name == YATOM("DataSource0")) ? 0 : (name == YATOM("DataSource1") ? 1 : -1);
    if (who == -1)
	return CallEndpoint::getObject(name);

    ModuleLine* target = 0;
    if (who)
	 target = m_fxsCaller ? m_line : fxo();
    else
	 target = m_fxsCaller ? fxo() : m_line;
    return (target && target->circuit()) ? target->circuit()->getObject(YATOM("DataSource")) : 0;
}

// Create data source. Route and execute
bool AnalogCallRec::startRecording()
{
    m_line->setCircuitParam("echotrain");
    if (getSource())
	return true;

    Debug(this,DebugCall,"Start recording [%p]",this);

    Lock lock (m_mutex);
    String format = "2*";
    DataSource* src = 0;
    String buflen;
    if (m_line && m_line->circuit()) {
	src = static_cast<DataSource*>(m_line->circuit()->getObject(YATOM("DataSource")));
	m_line->circuit()->getParam("buflen",buflen);
    }
    if (src)
	format << src->getFormat();

    // Create source
    Message* m = message("chan.attach",false,true);
    m->addParam("source","mux/");
    m->addParam("single",String::boolText(true));
    m->addParam("notify",id());
    if (buflen)
	m->addParam("chanbuffer",buflen);
    m->addParam("format",format);
    m->addParam("fail","true");
    m->addParam("failempty","true");
    if (!Engine::dispatch(m))
	Debug(this,DebugNote,"Error attaching data mux '%s' [%p]",m->getValue("error"),this);
    else if (m->userData())
	setSource(static_cast<DataSource*>(m->userData()->getObject(YATOM("DataSource"))));
    TelEngine::destruct(m);
    if (!getSource()) {
	m_reason = "nodata";
	return false;
    }

    // Route and execute
    m = message("call.preroute");
    m->addParam("callsource",callertype(m_fxsCaller));
    const char* caller = m->getValue("caller");
    if (!(caller && *caller))
	m->setParam("caller",s_unk);
    bool ok = false;
    while (true) {
	if (Engine::dispatch(m) && (m->retValue() == "-" || m->retValue() == "error")) {
	    m_reason = m->getValue("reason",m->getValue("error","failure"));
	    break;
	}
	*m = "call.route";
	m->addParam("type","record");
	m->addParam("format",format);
	m->setParam("callsource",callertype(m_fxsCaller));
	if (!(Engine::dispatch(m) && m->retValue())) {
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
    if (getPeer()) {
	XDebug(this,DebugInfo,"Got connected: deref() [%p]",this);
	deref();
    }
    else
	setSource();
    return ok;
}

// Call answered: start recording
bool AnalogCallRec::answered()
{
    Lock lock(m_mutex);
    if (m_line)
	m_line->noRingTimer().stop();
    if (fxo())
	fxo()->noRingTimer().stop();
    m_startOnSecondRing = false;
    if (!(m_line && startRecording()))
	return false;
    if (m_answered)
	return true;
    Debug(this,DebugCall,"Answered [%p]",this);
    m_answered = true;
    m_status = "answered";
    m_line->changeState(AnalogLine::Answered,true);
    Engine::enqueue(message("call.answered"));
    return true;
}

// Process rings: start recording if delayed
bool AnalogCallRec::ringing(bool fxsEvent)
{
    Lock lock(m_mutex);

    // Re(start) ring timer. Ignore ring events if timer was already started
    bool ignore = m_ringTimer.started();
    m_ringTimer.start();
    if (ignore)
	return true;

    if (m_line)
	m_line->changeState(AnalogLine::Ringing,true);

    // Ignore rings from caller party
    if (m_fxsCaller != fxsEvent) {
	DDebug(this,DebugAll,"Ignoring ring from caller [%p]",this);
	return true;
    }

    if (!m_answered) {
	m_status = "ringing";
	Engine::enqueue(message("call.ringing",false,true));
    }

    bool ok = true;
    if (m_fxsCaller) {
	if (m_startOnSecondRing) {
	    m_startOnSecondRing = false;
	    ok = startRecording();
	}
	if (m_line->getPeer())
	    fxo()->removeCallSetupDetector();
	if (ok && !m_answered) {
	    DDebug(this,DebugAll,"Restarting ring timer for " FMT64 "ms [%p]",
		fxo()->noRingTimer().interval(),this);
	    fxo()->noRingTimer().start();
	}
    }
    return ok;
}

// Enqueue chan.dtmf
void AnalogCallRec::evDigits(bool fxsEvent, const char* text, bool tone)
{
    if (!(text && *text))
	return;
    DDebug(this,DebugAll,"Got %s digits=%s from %s [%p]",
	tone?"tone":"pulse",text,callertype(fxsEvent),this);
    Message* m = message("chan.dtmf",false,true);
    m->addParam("text",text);
    if (!tone)
	m->addParam("pulse",String::boolText(true));
    m->addParam("sender",callertype(fxsEvent));
    m->addParam("detected","analog");
    Engine::enqueue(m);
}

// Process line polarity changes
bool AnalogCallRec::evPolarity(bool fxsEvent)
{
    if (fxsEvent)
	return true;

    Lock lock(m_mutex);
    m_polarityCount++;
    DDebug(this,DebugAll,"Line polarity changed %u time(s) [%p]",m_polarityCount,this);

    ModuleLine* fxo = this->fxo();
    if (!fxo)
	return false;

    if (m_fxsCaller) {
	if (!fxo->answerOnPolarity() || m_polarityCount > 1)
	    return !fxo->hangupOnPolarity();
	return true;
    }
    if (!fxo->answerOnPolarity() || m_answered)
	return !fxo->hangupOnPolarity();
    return answered();
}

// Line alarms changed
bool AnalogCallRec::evAlarm(bool fxsEvent, bool alarm, const char* alarms)
{
    Lock lock(m_mutex);
    if (alarm) {
	Debug(this,DebugNote,"%s line is out of order alarms=%s. Terminating now [%p]",
	    callertype(!fxsEvent),alarms,this);
	if (!m_reason) {
	    m_reason = callertype(!fxsEvent);
	    m_reason << "-out-of-order";
	}
	return false;
    }
    else {
	if (m_line)
	    m_line->setCircuitParam("echotrain");
	Debug(this,DebugInfo,"No more alarms on %s line [%p]",callertype(!fxsEvent),this);
    }
    return true;
}

// Check timers. Return false to terminate
bool AnalogCallRec::checkTimeouts(const Time& when)
{
    Lock lock(m_mutex);

    if (m_ringTimer.timeout(when.msecNow()))
	m_ringTimer.stop();

    if (!fxo()->noRingTimer().timeout(when.msecNow()))
	return true;
    DDebug(this,DebugInfo,"Ring timer expired [%p]",this);
    fxo()->noRingTimer().stop();
    hangup("cancelled");
    return false;
}

// Fill a string with recorder status parameters
void AnalogCallRec::statusParams(String& str)
{
    str.append("module=",",") << plugin.name();
    str << ",peerid=";
    if (getPeer())
	 str << getPeer()->id();
    str << ",status=" << m_status;
    str << ",initiator=" << callertype(m_fxsCaller);
    str << ",answered=" << m_answered;
    str << ",address=" << m_address;
}

// Fill a string with recorder status detail parameters
void AnalogCallRec::statusDetail(String& str)
{
    // format=Status|Address|Peer
    Lock lock(m_mutex);
    str.append(id(),";") << "=" << m_status;
    str << "|" << m_address << "|";
    if (getPeer())
	 str << getPeer()->id();
}

// Remove from driver's list
void AnalogCallRec::destroyed()
{
    plugin.setRecorder(this,false);
    hangup();
    // Reset line
    if (m_line) {
	m_line->userdata(0,true);
	if (m_line->moduleGroup())
	    m_line->moduleGroup()->setEndpoint(this,false);
	TelEngine::destruct(m_line);
    }
    Debug(this,DebugCall,"Destroyed reason='%s' [%p]",m_reason.c_str(),this);
    CallEndpoint::destroyed();
}

void AnalogCallRec::disconnected(bool final, const char *reason)
{
    DDebug(this,DebugCall,"Disconnected final=%s reason='%s' [%p]",
	String::boolText(final),reason,this);
    hangup(reason);
    CallEndpoint::disconnected(final,m_reason);
}

Message* AnalogCallRec::message(const char* name, bool peers, bool userdata)
{
    Message* m = new Message(name);
    m->addParam("id",id());
    m->addParam("status",m_status);
    if (m_address)
	m->addParam("address",m_address);
    ModuleLine* fxo = peers ? this->fxo() : 0;
    if (fxo) {
	if (m_fxsCaller) {
	    m->addParam("caller",fxo->caller());
	    m->addParam("called",fxo->called());
	}
	else {
	    m->addParam("caller",m_line->caller());
	    m->addParam("called",m_line->called());
	}
    }
    if (m_reason)
	m->addParam("reason",m_reason);
    if (userdata)
	m->userData(this);
    return m;
}


/**
 * AnalogDriver
 */
String AnalogDriver::s_statusCmd[StatusCmdCount] = {"groups","lines","recorders"};

AnalogDriver::AnalogDriver()
    : Driver("analog","varchans"),
    m_init(false),
    m_recId(0)
{
    Output("Loaded module Analog Channel");
    m_statusCmd << "status " << name();
    m_recPrefix << prefix() << "rec/";
}

AnalogDriver::~AnalogDriver()
{
    Output("Unloading module Analog Channel");
    m_groups.clear();
}

void AnalogDriver::initialize()
{
    Output("Initializing module Analog Channel");
    s_cfg = Engine::configFile("analog");
    s_cfg.load();

    NamedList dummy("");
    NamedList* general = s_cfg.getSection("general");
    if (!general)
	general = &dummy;

    // Startup
    setup();
    if (!m_init) {
	m_init = true;
	installRelay(Masquerade);
	installRelay(Halt);
	installRelay(Progress);
	installRelay(Update);
	installRelay(Route);
	Engine::install(new EngineStartHandler);
	Engine::install(new ChanNotifyHandler);
    }

    // Build/initialize groups
    String tmpRec = m_recPrefix.substr(0,m_recPrefix.length()-1);
    unsigned int n = s_cfg.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* sect = s_cfg.getSection(i);
	if (!sect || sect->null() || *sect == "general" ||
	    sect->startsWith(s_lineSectPrefix))
	    continue;

	// Check section name
	bool valid = true;
	if (*sect == name() || *sect == tmpRec)
	    valid = false;
	else
	    for (unsigned int i = 0; i < StatusCmdCount; i++)
		if (*sect == s_statusCmd[i]) {
		    valid = false;
		    break;
		}
	if (!valid) {
	    Debug(this,DebugWarn,"Invalid use of reserved word in section name '%s'",sect->c_str());
	    continue;
	}

	ModuleGroup* group = findGroup(*sect);
	if (!sect->getBoolValue("enable",true)) {
	    if (group)
		removeGroup(group);
	    continue;
	}

	// Create and/or initialize. Check for valid type if creating
	const char* stype = sect->getValue("type");
	int type = lookup(stype,AnalogLine::typeNames(),AnalogLine::Unknown);
	switch (type) {
	    case AnalogLine::FXO:
	    case AnalogLine::FXS:
	    case AnalogLine::Recorder:
	    case AnalogLine::Monitor:
		break;
	    default:
		Debug(this,DebugWarn,"Unknown type '%s' for group '%s'",stype,sect->c_str());
		continue;
	}

	bool create = (group == 0);
	Debug(this,DebugAll,"%sing group '%s' of type '%s'",create?"Creat":"Reload",sect->c_str(),stype);

	if (create) {
	    if (type != AnalogLine::Monitor)
		group = new ModuleGroup((AnalogLine::Type)type,*sect);
	    else {
		String tmp = *sect;
		tmp << "/fxo";
		ModuleGroup* fxo = new ModuleGroup(tmp);
		group = new ModuleGroup(*sect,fxo);
	    }
	    lock();
	    m_groups.append(group);
	    unlock();
	    XDebug(this,DebugAll,"Added group (%p,'%s')",group,group->debugName());
	}

	String error;
	if (!group->initialize(*sect,*general,error)) {
	    Debug(this,DebugWarn,"Failed to %s group '%s'. Error: '%s'",
		create?"create":"reload",sect->c_str(),error.safe());
	    if (create)
		removeGroup(group);
	}
    }
}

bool AnalogDriver::msgExecute(Message& msg, String& dest)
{
    CallEndpoint* peer = YOBJECT(CallEndpoint,msg.userData());
    ModuleLine* line = 0;
    String cause;
    const char* error = "failure";

    // Check message parameters: peer channel, group, circuit, line
    while (true) {
	if (!peer) {
	    cause = "No data channel";
	    break;
	}
	String tmp;
	int cic = decodeAddr(dest,tmp,true);
	ModuleGroup* group = findGroup(tmp);
	if (group && !group->fxoRec()) {
	    if (cic >= 0)
		line = static_cast<ModuleLine*>(group->findLine(cic));
	    else if (cic == -1) {
		Lock lock(group);
		// Destination is a group: find the first free idle line
		for (ObjList* o = group->lines().skipNull(); o; o = o->skipNext()) {
		    line = static_cast<ModuleLine*>(o->get());
		    Lock lockLine(line);
		    if (!line->userdata() && line->state() == AnalogLine::Idle)
			break;
		    line = 0;
		}
		lock.drop();
		if (!line) {
		    cause << "All lines in group '" << dest << "' are busy";
		    error = "busy";
		    break;
		}
	    }
	}

	if (!line) {
	    cause << "No line with address '" << dest << "'";
	    error = "noroute";
	    break;
	}
	if (line->type() == AnalogLine::Unknown) {
	    cause << "Line '" << line->address() << "' has unknown type";
	    break;
	}
	if (line->userdata()) {
	    cause << "Line '" << line->address() << "' is busy";
	    error = "busy";
	    break;
	}
	if (line->state() == AnalogLine::OutOfService) {
	    cause << "Line '" << line->address() << "' is out of service";
	    error = "noroute";
	    break;
	}
	if (!line->ref())
	    cause = "ref() failed";
	break;
    }

    if (!line || cause) {
	Debug(this,DebugNote,"Analog call failed. %s",cause.c_str());
	msg.setParam("error",error);
	return false;
    }

    Debug(this,DebugAll,"Executing call. caller=%s called=%s line=%s",
	msg.getValue("caller"),msg.getValue("called"),line->address());

    msg.clearParam("error");
    // Create channel
    AnalogChannel* analogCh = new AnalogChannel(line,&msg);
    analogCh->initChan();
    error = msg.getValue("error");
    if (!error) {
	if (analogCh->connect(peer,msg.getValue("reason"))) {
	    analogCh->callConnect(msg);
	    msg.setParam("peerid",analogCh->id());
	    msg.setParam("targetid",analogCh->id());
	    if (analogCh->line() && (analogCh->line()->type() == AnalogLine::FXS))
		Engine::enqueue(analogCh->message("call.ringing",false,true));
        }
    }
    else
	Debug(this,DebugNote,"Analog call failed with reason '%s'",error);
    analogCh->deref();
    return !error;
}

void AnalogDriver::dropAll(Message& msg)
{
    const char* reason = msg.getValue("reason");
    if (!(reason && *reason))
	reason = "dropped";
    DDebug(this,DebugInfo,"dropAll('%s')",reason);
    Driver::dropAll(msg);
    // Drop recorders
    lock();
    ListIterator iter(m_recorders);
    for (;;) {
	RefPointer<AnalogCallRec> c = static_cast<AnalogCallRec*>(iter.get());
	unlock();
	if (!c)
	    break;
	terminateChan(c,reason);
	c = 0;
	lock();
    }
}

bool AnalogDriver::received(Message& msg, int id)
{
    String target;

    switch (id) {
	case Masquerade:
	    // Masquerade a recorder message
	    target = msg.getValue("id");
	    if (target.startsWith(recPrefix())) {
		Lock lock(this);
		AnalogCallRec* rec = findRecorder(target);
		if (rec) {
		    msg = msg.getValue("message");
		    msg.clearParam("message");
		    msg.userData(rec);
		    return false;
		}
	    }
	    return Driver::received(msg,id);
	case Status:
	case Drop:
	    target = msg.getValue("module");
	    // Target is the driver or channel
	    if (!target || target == name() || target.startsWith(prefix()))
		return Driver::received(msg,id);
	    // Check if requested a recorder
	    if (target.startsWith(recPrefix())) {
		Lock lock(this);
		AnalogCallRec* rec = findRecorder(target);
		if (!rec)
		    return false;
		if (id == Status) {
		    msg.retValue().clear();
		    rec->statusParams(msg.retValue());
		    msg.retValue() << "\r\n";
		}
		else
		    terminateChan(rec,"dropped");
		return true;
	    }
	    // Done if the command is drop
	    if (id == Drop)
		return Driver::received(msg,id);
	    break;
	case Halt:
	    lock();
	    m_groups.clear();
	    unlock();
	    return Driver::received(msg,id);
	default:
	    return Driver::received(msg,id);
    }

    // Check for additional status commands or a specific group or line
    if (!target.startSkip(name(),false))
	return false;
    target.trimBlanks();
    int cmd = 0;
    for (; cmd < StatusCmdCount; cmd++)
	if (s_statusCmd[cmd] == target)
	    break;

    Lock lock(this);
    DDebug(this,DebugInfo,"Processing '%s' target=%s",msg.c_str(),target.c_str());
    // Specific group or line
    if (cmd == StatusCmdCount) {
	String group;
	int cic = decodeAddr(target,group,false);
	ModuleGroup* grp = findGroup(group);
	bool ok = true;
	while (grp) {
	    Lock lock(grp);
	    if (target == grp->toString()) {
		msg.retValue().clear();
		grp->statusParams(msg.retValue());
		break;
	    }
	    ModuleLine* line = static_cast<ModuleLine*>(grp->findLine(cic));
	    if (!line) {
		ok = false;
		break;
	    }
	    msg.retValue().clear();
	    Lock lockLine(line);
	    line->statusParams(msg.retValue());
	    break;
	}
	if (ok)
	    msg.retValue() << "\r\n";
	return ok;
    }

    // Additional command
    String detail;
    const char* format = 0;
    int count = 0;
    switch (cmd) {
	case Groups:
	    format = s_groupStatusDetail;
	    for (ObjList* o = m_groups.skipNull(); o; o = o->skipNext()) {
		count++;
		(static_cast<ModuleGroup*>(o->get()))->statusDetail(detail);
	    }
	    break;
	case Lines:
	    format = s_lineStatusDetail;
	    for (ObjList* o = m_groups.skipNull(); o; o = o->skipNext()) {
		ModuleGroup* grp = static_cast<ModuleGroup*>(o->get());
		Lock lockGrp(grp);
		for (ObjList* ol = grp->lines().skipNull(); ol; ol = ol->skipNext()) {
		    count++;
		    (static_cast<ModuleLine*>(ol->get()))->statusDetail(detail);
		}
	    }
	    break;
	case Recorders:
	    format = s_recStatusDetail;
	    for (ObjList* o = m_recorders.skipNull(); o; o = o->skipNext()) {
		count++;
		(static_cast<AnalogCallRec*>(o->get()))->statusDetail(detail);
	    }
	    break;
	default:
	    count = -1;
    }
    // Just in case we've missed something
    if (count == -1)
	return false;

    msg.retValue().clear();
    msg.retValue() << "module=" << name();
    msg.retValue() << "," << s_statusCmd[cmd] << "=" << count;
    msg.retValue() << "," << format;
    if (detail)
	msg.retValue() << ";" << detail;
    msg.retValue() << "\r\n";
    return true;
}

// Handle command complete requests
bool AnalogDriver::commandComplete(Message& msg, const String& partLine,
	const String& partWord)
{
    bool status = partLine.startsWith("status");
    bool drop = !status && partLine.startsWith("drop");
    if (!(status || drop))
	return Driver::commandComplete(msg,partLine,partWord);

    // 'status' command
    Lock lock(this);
    // line='status analog': add additional commands, groups and lines
    if (partLine == m_statusCmd) {
	DDebug(this,DebugInfo,"Processing '%s' partWord=%s",partLine.c_str(),partWord.c_str());
	for (unsigned int i = 0; i < StatusCmdCount; i++)
	    itemComplete(msg.retValue(),s_statusCmd[i],partWord);
	completeGroups(msg.retValue(),partWord);
	completeLines(msg.retValue(),partWord);
	return true;
    }

    if (partLine != "status" && partLine != "drop")
	return false;

    // Empty partial word or name start with it: add name, prefix and recorder prefix
    if (itemComplete(msg.retValue(),name(),partWord)) {
	if (channels().skipNull())
	    msg.retValue().append(prefix(),"\t");
	return false;
    }
    // Non empty partial word greater then module name: check if we have a prefix
    if (!partWord.startsWith(prefix()))
	return false;
    // Partial word is not empty and starts with module's prefix
    // Recorder prefix (greater then any channel ID): complete recorders
    // Between module and recorder prefix: complete recorder prefix and channels
    if (partWord.startsWith(recPrefix())) {
	bool all = (partWord == recPrefix());
	completeChanRec(msg.retValue(),partWord,false,all);
    }
    else {
	bool all = (partWord == prefix());
	completeChanRec(msg.retValue(),partWord,true,all);
	completeChanRec(msg.retValue(),partWord,false,all);
    }
    return true;
}

// Execute commands
bool AnalogDriver::commandExecute(String& retVal, const String& line)
{
    DDebug(this,DebugInfo,"commandExecute(%s)",line.c_str());
    return false;
}

// Complete group names from partial command word
void AnalogDriver::completeGroups(String& dest, const String& partWord)
{
    for (ObjList* o = m_groups.skipNull(); o; o = o->skipNext())
	itemComplete(dest,static_cast<ModuleGroup*>(o->get())->toString(),partWord);
}

// Complete line names from partial command word
void AnalogDriver::completeLines(String& dest, const String& partWord)
{
    for (ObjList* o = m_groups.skipNull(); o; o = o->skipNext()) {
	ModuleGroup* grp = static_cast<ModuleGroup*>(o->get());
	Lock lock(grp);
	for (ObjList* ol = grp->lines().skipNull(); ol; ol = ol->skipNext())
	    itemComplete(dest,static_cast<ModuleLine*>(ol->get())->toString(),partWord);
    }
}

// Notification of line service state change or removal
// Return true if a channel or recorder was found
bool AnalogDriver::lineUnavailable(ModuleLine* line)
{
    if (!line)
	return false;

    const char* reason = (line->state() == AnalogLine::OutOfService) ? "line-out-of-service" : "line-shutdown";
    Lock lock(this);
    for (ObjList* o = channels().skipNull(); o; o = o->skipNext()) {
	AnalogChannel* ch = static_cast<AnalogChannel*>(o->get());
	if (ch->line() != line)
	    continue;
	terminateChan(ch,reason);
	return true;
    }

    // Check for recorders
    if (!line->getPeer())
	return false;
    ModuleGroup* grp = line->moduleGroup();
    AnalogCallRec* rec = 0;
    if (grp && 0 != (rec = grp->findRecorder(line))) {
	terminateChan(rec,reason);
	return true;
    }
    return false;
}

// Destroy a channel
void AnalogDriver::terminateChan(AnalogChannel* ch, const char* reason)
{
    if (!ch)
	return;
    DDebug(this,DebugAll,"Terminating channel %s peer=%p reason=%s",
	ch->id().c_str(),ch->getPeer(),reason);
    if (ch->getPeer())
	ch->disconnect(reason);
    else
	ch->deref();
}

// Destroy a monitor endpoint
void AnalogDriver::terminateChan(AnalogCallRec* ch, const char* reason)
{
    if (!ch)
	return;
    DDebug(this,DebugAll,"Terminating recorder %s peer=%p reason=%s",
	ch->id().c_str(),ch->getPeer(),reason);
    if (ch->getPeer())
	ch->disconnect(reason);
    else
	ch->deref();
}

// Attach detectors after engine started
void AnalogDriver::engineStart(Message& msg)
{
    s_engineStarted = true;
    Lock lock(this);
    for (ObjList* o = m_groups.skipNull(); o; o = o->skipNext()) {
	ModuleGroup* grp = static_cast<ModuleGroup*>(o->get());
	if (grp->type() != AnalogLine::FXO) {
	    grp = grp->fxoRec();
	    if (!grp || grp->type() != AnalogLine::FXO)
		grp = 0;
	}
	if (!grp)
	    continue;
	Lock lock(grp);
	for (ObjList* ol = grp->lines().skipNull(); ol; ol = ol->skipNext()) {
	    ModuleLine* line = static_cast<ModuleLine*>(ol->get());
	    if (line->callSetup() == AnalogLine::Before)
	        line->setCallSetupDetector();
	}
    }
}

// Notify lines on detector events or channels
bool AnalogDriver::chanNotify(Message& msg)
{
    String target = msg.getValue("targetid");
    if (!target.startSkip(plugin.prefix(),false))
	return false;

    // Check if the notification is for a channel
    if (-1 != target.toInteger(-1)) {
	Debug(this,DebugStub,"Ignoring chan.notify with target=%s",msg.getValue("targetid"));
	return true;
    }

    // Notify lines
    String name;
    int cic = decodeAddr(target,name,false);
    ModuleLine* line = 0;
    Lock lockDrv(this);
    ModuleGroup* grp = findGroup(name);
    if (grp)
	line = static_cast<ModuleLine*>(grp->findLine(cic));
    else {
	// Find by recorder's fxo
	grp = findGroup(name,true);
	if (grp && grp->fxoRec())
	    line = static_cast<ModuleLine*>(grp->fxoRec()->findLine(cic));
    }

    Lock lockLine(line);
    if (!(line && line->ref())) {
	Debug(this,DebugNote,"Received chan.notify for unknown target=%s",target.c_str());
	return true;
    }
    lockDrv.drop();
    line->processNotify(msg);
    line->deref();
    return true;
}

// Append/remove recorders from list
void AnalogDriver::setRecorder(AnalogCallRec* rec, bool add)
{
    if (!rec)
	return;
    Lock lock(this);
    if (add)
	m_recorders.append(rec);
    else
	m_recorders.remove(rec,false);
}

// Remove a group from list
void AnalogDriver::removeGroup(ModuleGroup* group)
{
    if (!group)
	return;
    Lock lock(this);
    Debug(this,DebugAll,"Removing group (%p,'%s')",group,group->debugName());
    m_groups.remove(group);
}

// Find a group or recorder by its name
// Set useFxo to true to find a recorder by its fxo's name
ModuleGroup* AnalogDriver::findGroup(const char* name, bool useFxo)
{
    if (!useFxo)
	return findGroup(name);
    if (!(name && *name))
	return 0;
    Lock lock(this);
    String tmp = name;
    for (ObjList* o = m_groups.skipNull(); o; o = o->skipNext()) {
	ModuleGroup* grp = static_cast<ModuleGroup*>(o->get());
	if (grp->fxoRec() && grp->fxoRec()->toString() == tmp)
	    return grp;
    }
    return 0;
}


/**
 * AnalogWorkerThread
 */
AnalogWorkerThread::AnalogWorkerThread(ModuleGroup* group)
    : Thread("Analog Worker"),
    m_client(group),
    m_groupName(group ? group->debugName() : "")
{
}

AnalogWorkerThread::~AnalogWorkerThread()
{
    DDebug(&plugin,DebugAll,"AnalogWorkerThread(%p,'%s') terminated [%p]",
	m_client,m_groupName.c_str(),this);
    if (m_client)
	m_client->m_thread = 0;
}

void AnalogWorkerThread::run()
{
    Debug(&plugin,DebugAll,"AnalogWorkerThread(%p,'%s') start running [%p]",
	m_client,m_groupName.c_str(),this);
    if (!m_client)
	return;
    while (true) {
	Time t = Time();
	AnalogLineEvent* event = m_client->getEvent(t);
	if (!event) {
	    m_client->checkTimers(t);
	    Thread::idle(true);
	    continue;
	}
	ModuleLine* line = static_cast<ModuleLine*>(event->line());
	SignallingCircuitEvent* cicEv = event->event();
	if (line && cicEv)
	    if (!m_client->fxoRec())
		m_client->handleEvent(*line,*cicEv);
	    else
		m_client->handleRecEvent(*line,*cicEv);
	else
	    Debug(m_client,DebugInfo,"Invalid event (%p) line=%p cic event=%p",
		event,line,event->event());
	TelEngine::destruct(event);
	if (Thread::check(true))
	    break;
    }
}


/**
 * EngineStartHandler
 */
bool EngineStartHandler::received(Message& msg)
{
    plugin.engineStart(msg);
    return false;
}


/**
 * ChanNotifyHandler
 */
bool ChanNotifyHandler::received(Message& msg)
{
    return plugin.chanNotify(msg);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
