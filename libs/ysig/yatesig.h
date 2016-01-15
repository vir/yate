/**
 * yatesig.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Signalling Stack - implements the support for SS7, ISDN and PSTN
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

#ifndef __YATESIG_H
#define __YATESIG_H

#include <yateclass.h>

#ifdef _WINDOWS

#ifdef LIBYSIG_EXPORTS
#define YSIG_API __declspec(dllexport)
#else
#ifndef LIBYSIG_STATIC
#define YSIG_API __declspec(dllimport)
#endif
#endif

#endif /* _WINDOWS */

#ifndef YSIG_API
#define YSIG_API
#endif

/**
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

// Signalling classes
class SignallingDumper;                  // A generic data dumper
class SignallingDumpable;                // A component that can dump data
class SignallingNotifier;                // A signalling notifier
class SignallingTimer;                   // A signalling timer
class SignallingCounter;                 // A signalling counter
class SignallingFactory;                 // A signalling component factory
class SignallingComponent;               // Abstract signalling component that can be managed by the engine
class SignallingEngine;                  // Main signalling component holder
class SignallingThreadPrivate;           // Engine private thread
class SignallingMessage;                 // Abstract signalling message
class SignallingCallControl;             // Abstract phone call signalling
class SignallingCall;                    // Abstract single phone call
class SignallingEvent;                   // A single signalling related event
class SignallingCircuitEvent;            // A single signalling circuit related event
class SignallingCircuit;                 // Abstract data circuit used by signalling
class SignallingCircuitRange;            // A circuit range (set of circuits)
class SignallingCircuitGroup;            // Group of data circuits used by signalling
class SignallingCircuitSpan;             // A span in a circuit group
class SignallingInterface;               // Abstract digital signalling interface (hardware access)
class SignallingReceiver;                // Abstract Layer 2 packet data receiver
struct SignallingFlags;                  // Description of parameter flags
class SignallingUtils;                   // Library wide services and data provider
class SignallingMessageTimer;            // A pending signalling message
class SignallingMessageTimerList;        // A pending signalling message list
// Analog lines
class AnalogLine;                        // An analog line
class AnalogLineEvent;                   // A single analog line related event
class AnalogLineGroup;                   // A group of analog lines
// SS7
class SS7PointCode;                      // SS7 Code Point
class SS7Label;                          // SS7 Routing Label
class SS7MSU;                            // A block of data that holds a Message Signal Unit
class SIGTRAN;                           // Abstract SIGTRAN component
class SIGTransport;                      // Abstract transport for SIGTRAN
class SIGAdaptation;                     // Abstract Adaptation style SIGTRAN
class SIGAdaptClient;                    // Client side of adaptation (ASP)
class SIGAdaptServer;                    // Server side of adaptation (SG)
class SIGAdaptUser;                      // Abstract Adaptation User SIGTRAN
class ASPUser;                           // Abstract SS7 ASP user interface
class SCCP;                              // Abstract SS7 SCCP interface
class GTT;                               // Abstract SCCP Global Title Translation interface
class SCCPManagement;                    // Abstract SCCP Management interface
class SCCPUser;                          // Abstract SS7 SCCP user interface
class TCAPUser;                          // Abstract SS7 TCAP user interface
class SS7L2User;                         // Abstract user of SS7 layer 2 (data link) message transfer part
class SS7Layer2;                         // Abstract SS7 layer 2 (data link) message transfer part
class SS7L3User;                         // Abstract user of SS7 layer 3 (network) message transfer part
class SS7Layer3;                         // Abstract SS7 layer 3 (network) message transfer part
class SS7Layer4;                         // Abstract SS7 layer 4 (application) protocol
class SS7Route;                          // A SS7 MSU route
class SS7Router;                         // Main router for SS7 message transfer and applications
class SS7M2PA;                           // SIGTRAN MTP2 User Peer-to-Peer Adaptation Layer
class SS7M2UA;                           // SIGTRAN MTP2 User Adaptation Layer
class SS7M3UA;                           // SIGTRAN MTP3 User Adaptation Layer
class SS7MTP2;                           // SS7 Layer 2 implementation on top of a hardware interface
class SS7MTP3;                           // SS7 Layer 3 implementation on top of Layer 2
class SS7MsgSNM;                         // SNM signalling message
class SS7MsgMTN;                         // MTN signalling message
class SS7MsgISUP;                        // ISUP signalling message
class SS7MsgSCCP;                        // SCCP signalling message
class SS7Management;                     // SS7 SNM implementation
class SS7ISUPCall;                       // A SS7 ISUP call
class SS7ISUP;                           // SS7 ISUP implementation
class SS7BICC;                           // SS7 BICC implementation
class SS7TUP;                            // SS7 TUP implementation
class SS7SCCP;                           // SS7 SCCP implementation
class SccpSubsystem;                     // Helper class to keep a SCCP subsystem
class SccpLocalSubsystem;                // Helper class to keep s sccp local subsystem informations
class SccpRemote;                        // Helper class to keep a remote sccp
class SS7AnsiSccpManagement;             // SS7 SCCP management implementation for ANSI
class SS7ItuSccpManagement;              // SS7 SCCP management implementation for ITU
class SS7SUA;                            // SIGTRAN SCCP User Adaptation Layer
class SS7ASP;                            // SS7 ASP implementation
class SS7TCAPMessage;                    // SS7 TCAP message wrapper
class SS7TCAPError;                      // SS7 TCAP errors
class SS7TCAP;                           // SS7 TCAP implementation
class SS7TCAPTransaction;                // SS7 TCAP transaction base class
class SS7TCAPComponent;                  // SS7 TCAP component
class SS7TCAPANSI;                       // SS7 ANSI TCAP implementation
class SS7TCAPTransactionANSI;            // SS7 TCAP ANSI Transaction
class SS7TCAPITU;                        // SS7 ITU TCAP implementation
class SS7TCAPTransactionITU;             // SS7 TCAP ITU Transaction
// ISDN
class ISDNLayer2;                        // Abstract ISDN layer 2 (Q.921) message transport
class ISDNLayer3;                        // Abstract ISDN layer 3 (Q.931) message transport
class ISDNFrame;                         // An ISDN Q.921 frame
class ISDNQ921;                          // ISDN Q.921 implementation on top of a hardware interface
class ISDNQ921Passive;                   // Stateless ISDN Q.921 implementation on top of a hardware interface
class ISDNQ921Management;                // ISDN Layer 2 BRI TEI management or PRI with D-channel(s) backup
class ISDNIUA;                           // SIGTRAN ISDN Q.921 User Adaptation Layer
class ISDNQ931IE;                        // A Q.931 ISDN Layer 3 message Information Element
class ISDNQ931Message;                   // A Q.931 ISDN Layer 3 message
class ISDNQ931IEData;                    // A Q.931 message IE data processor
class ISDNQ931State;                     // Q.931 ISDN call and call controller state
class ISDNQ931Call;                      // A Q.931 ISDN call
class ISDNQ931CallMonitor;               // A Q.931 ISDN call monitor
class ISDNQ931ParserData;                // Q.931 message parser data
class ISDNQ931;                          // ISDN Q.931 implementation on top of Q.921
class ISDNQ931Monitor;                   // ISDN Q.931 implementation on top of Q.921 of call controller monitor

// Macro to create a factory that builds a component by class name
#define YSIGFACTORY(clas) \
class clas ## Factory : public SignallingFactory \
{ \
protected: \
virtual SignallingComponent* create(const String& type, NamedList& name) \
    { return (type == #clas) ? new clas : 0; } \
}; \
static clas ## Factory s_ ## clas ## Factory

// Macro to create a factory that calls a component's static create method
#define YSIGFACTORY2(clas) \
class clas ## Factory : public SignallingFactory \
{ \
protected: \
virtual SignallingComponent* create(const String& type, NamedList& name) \
    { return clas::create(type,name); } \
}; \
static clas ## Factory s_ ## clas ## Factory

// Macro to call the factory creation method and return the created component
#define YSIGCREATE(type,name) (static_cast<type*>(SignallingFactory::buildInternal(#type,name)))

/**
 * This class is a generic data dumper with libpcap compatibility
 * @short A generic data dumper
 */
class YSIG_API SignallingDumper
{
public:
    /**
     * Type of dumper output
     */
    enum Type {
	Raw,
	Hexa,
	Hdlc,
	Q921,
	Q931,
	Mtp2,
	Mtp3,
	Sccp,
    };

    /**
     * Constructor
     * @param type Type of the output desired
     * @param network True if we are the network side of the link
     */
    SignallingDumper(Type type = Hexa, bool network = false);

    /**
     * Destructor, closes the output
     */
    ~SignallingDumper();

    /**
     * Get the type of the dumper
     * @return Type of the dumper object
     */
    inline Type type() const
	{ return m_type; }

    /**
     * Get the network side flag
     * @return True if we are the network side
     */
    inline bool network() const
	{ return m_network; }

    /**
     * Check if the dumper is active
     * @return True if the object will actually send data to something
     */
    bool active() const;

    /**
     * Terminate the dump session, close the output
     */
    void terminate();

    /**
     * Set a new output stream
     * @param stream New stream for output, NULL to terminate
     * @param writeHeader True to write the header (if any) at start of stream
     */
    void setStream(Stream* stream = 0, bool writeHeader = true);

    /**
     * Dump the provided data
     * @param buf Pointer to buffer to dump
     * @param len Length of the data
     * @param sent True if data is being sent, false if is being received
     * @param link Link number (relevant to MTP2 only)
     * @return True if the data was dumped successfully
     */
    bool dump(void* buf, unsigned int len, bool sent = false, int link = 0);

    /**
     * Dump the provided data
     * @param data Buffer to dump
     * @param sent True if data is being sent, false if is being received
     * @param link Link number (relevant to MTP2 only)
     * @return True if the data was dumped successfully
     */
    inline bool dump(const DataBlock& data, bool sent = false, int link = 0)
	{ return dump(data.data(),data.length(),sent,link); }

    /**
     * Create a file to dump data in it. The file is opened/created in write only, binary mode
     * @param dbg DebugEnabler requesting the operation (used for debug message on failure)
     * @param filename The file name to use
     * @param type The dumper type
     * @param network True to create a network side dumper
     * @param create True to create the file if doesn't exist
     * @param append Append to an existing file. If false and the file already exists, it will be truncated
     * @return SignallingDumper pointer on success, 0 on failure
     */
    static SignallingDumper* create(DebugEnabler* dbg, const char* filename, Type type,
	bool network = false, bool create = true, bool append = false);

    /**
     * Create a dumper from an already existing stream
     * @param stream Stream to use for output, will be owned by dumper
     * @param type The dumper type
     * @param network True to create a network side dumper
     * @param writeHeader True to write the header (if any) at start of stream
     * @return SignallingDumper pointer on success, 0 on failure
     */
    static SignallingDumper* create(Stream* stream, Type type, bool network = false, bool writeHeader = true);

private:
    void head();
    Type m_type;
    bool m_network;
    Stream* m_output;
};

/**
 * A generic base class for components capable of creating data dumps
 * @short A data dumping capable component
 */
class YSIG_API SignallingDumpable
{
public:
    /**
     * Destructor - destroys the data dumper
     */
    inline ~SignallingDumpable()
	{ setDumper(); }

protected:
    /**
     * Constructor
     * @param type Default type of the data dumper
     * @param network True if we are the network side of the link
     */
    inline SignallingDumpable(SignallingDumper::Type type, bool network = false)
	: m_type(type), m_dumpNet(network), m_dumper(0)
	{ }

    /**
     * Dump the provided data if the dumper is valid
     * @param buf Pointer to buffer to dump
     * @param len Length of the data
     * @param sent True if data is being sent, false if is being received
     * @param link Link number (relevant to MTP2 only)
     * @return True if the data was dumped successfully
     */
    inline bool dump(void* buf, unsigned int len, bool sent = false, int link = 0)
	{ return (m_dumper && m_dumper->dump(buf,len,sent,link)); }

    /**
     * Dump data if the dumper is valid
     * @param data Buffer to dump
     * @param sent True if data is being sent, false if is being received
     * @param link Link number (relevant to MTP2 only)
     * @return True if the data was dumped successfully
     */
    inline bool dump(const DataBlock& data, bool sent = false, int link = 0)
	{ return dump(data.data(),data.length(),sent,link); }

    /**
     * Set the dump network side flag
     * @param network True to dump as network side, false othervise
     */
    inline void setDumpNetwork(bool network)
	{ m_dumpNet = network; }

    /**
     * Set or remove the data dumper
     * @param dumper Pointer to the data dumper object, 0 to remove
     */
    void setDumper(SignallingDumper* dumper = 0);

    /**
     * Set or remove a file data dumper
     * @param name Name of the file to dump to, empty to remove dumper
     * @param create True to create the file if doesn't exist
     * @param append Append to an existing file. If false and the file already exists, it will be truncated
     * @return True if the file dumper was created or removed
     */
    bool setDumper(const String& name, bool create = true, bool append = false);

    /**
     * Handle dumper related control on behalf of the owning component
     * @param params Control parameters to handle
     * @param owner Optional owning component
     * @return True if control operation was applied
     */
    bool control(NamedList& params, SignallingComponent* owner = 0);

private:
    SignallingDumper::Type m_type;
    bool m_dumpNet;
    SignallingDumper* m_dumper;
};

/**
 * Notifying class. Used to handle notifications.
 * @short Notifier class
 */

class YSIG_API SignallingNotifier
{
public:
    /**
     * Destructor.
     */
    virtual inline ~SignallingNotifier()
	{ cleanup(); }
    /**
     * Handle the received notifications
     * @param notifs Received notifications
     */
    virtual void notify(NamedList& notifs);
    /**
     * Handle necessary clean up
     */
    virtual void cleanup();
};

/**
 * Timer management class. Used to manage timeouts. The time is kept in miliseconds
 * @short A signalling timer
 */
class YSIG_API SignallingTimer
{
public:
    /**
     * Constructor
     * @param interval The timeout interval. Set to 0 to disable
     * @param time Optional timeout value. If non 0, the timer is started
     */
    inline SignallingTimer(u_int64_t interval, u_int64_t time = 0)
	: m_interval(interval), m_timeout(0)
	{ if (time) start(time); }

    /**
     * Set the timeout interval
     * @param value The new timeout value
     */
    inline void interval(u_int64_t value)
	{ m_interval = value; }

    /**
     * Set the timeout interval from a list of parameters. The interval value is
     *  checked to be at least minVal or 0 if allowDisable is true
     * @param params The list of parameters
     * @param param The name of the parameter containing the timer interval value
     * @param minVal Minimum value allowed for the timer interval
     * @param defVal Default value if it fails to get one from the given parameter
     * @param allowDisable True to allow 0 for the timer interval
     * @param sec True if the interval value if given in seconds
     */
    inline void interval(const NamedList& params, const char* param,
	unsigned int minVal, unsigned int defVal, bool allowDisable, bool sec = false) {
	    m_interval = getInterval(params,param,minVal,defVal,0,allowDisable);
	    if (sec)
		m_interval *= 1000;
	}

    /**
     * Get the timeout interval
     * @return The timeout interval
     */
    inline u_int64_t interval() const
	{ return m_interval; }

    /**
     * Get the time this timer will fire (timeout)
     * @return The timeout (fire) time
     */
    inline u_int64_t fireTime() const
	{ return m_timeout; }

    /**
     * Start the timer if enabled (interval is positive)
     * @param time Time to be added to the interval to set the timeout point
     */
    inline void start(u_int64_t time = Time::msecNow())
	{ if (m_interval) m_timeout = time + m_interval; }

    /**
     * Fire the timer at a specific absolute time
     * @param time Absolute time (in msec) when the timer will fire
     */
    inline void fire(u_int64_t time = Time::msecNow())
	{ m_timeout = time; }

    /**
     * Stop the timer
     */
    inline void stop()
	{ m_timeout = 0; }

    /**
     * Check if the timer is started
     * @return True if the timer is started
     */
    inline bool started() const
	{ return m_timeout > 0; }

    /**
     * Check if the timer is started and timed out
     * @param time The time to compare with
     * @return True if the timer timed out
     */
    inline bool timeout(u_int64_t time = Time::msecNow()) const
	{ return started() && (m_timeout < time); }

    /**
     * Retrieve a timer interval from a list of parameters.
     * @param params The list of parameters
     * @param param The name of the parameter containing the timer interval value
     * @param minVal Minimum value allowed for the timer interval
     * @param defVal Default value if it fails to get one from the given parameter
     * @param maxVal Optional interval maximum value
     * @param allowDisable True to allow 0 for the timer interval
     * @return The interval value
     */
    static unsigned int getInterval(const NamedList& params, const char* param,
	unsigned int minVal, unsigned int defVal, unsigned int maxVal = 0,
	bool allowDisable = false);

private:
    u_int64_t m_interval;                // Timer interval
    u_int64_t m_timeout;                 // Timeout value
};

/**
 * Counter management class. Keep a value between 0 and a given maximum one
 * @short A counter class
 */
class YSIG_API SignallingCounter
{
public:
    /**
     * Constructor
     * @param maxVal The maximum value for the counter
     */
    inline SignallingCounter(u_int32_t maxVal)
	: m_max(maxVal), m_count(0)
	{ }

    /**
     * Set the maximum value for the counter
     * @param value The new maximum value for the counter
     */
    inline void maxVal(u_int32_t value)
	{ m_max = value; }

    /**
     * Get the maximum value for the counter
     * @return The maximum value for the counter
     */
    inline u_int32_t maxVal() const
	{ return m_max; }

    /**
     * Get the current value of the counter
     * @return The current value of the counter
     */
    inline u_int32_t count() const
	{ return m_count; }

    /**
     * Reset the counter's value
     * @param down True to reset to 0, false to reset to maxVal()
     */
    inline void reset(bool down = true)
	{ m_count = down ? 0 : m_max; }

    /**
     * Increment the counter's value if it can
     * @return False if the counter is full (reached the maximum value)
     */
    inline bool inc()
    {
	if (full())
	    return false;
	m_count++;
	return true;
    }

    /**
     * Decrement the counter's value if it can
     * @return False if the counter is empty (reached 0)
     */
    inline bool dec()
    {
	if (empty())
	    return false;
	m_count--;
	return true;
    }

    /**
     * Check if the counter is empty (the value is 0)
     * @return True if the counter is empty
     */
    inline bool empty() const
	{ return m_count == 0; }

    /**
     * Check if the counter is full (the value reached the maximum)
     * @return True if the counter is full
     */
    inline bool full() const
	{ return m_count == maxVal(); }

private:
    u_int32_t m_max;                     // Maximum counter value
    u_int32_t m_count;                   // Current counter value
};

/**
 * A factory that constructs various elements by name
 * @short A signalling component factory
 */
class YSIG_API SignallingFactory : public GenObject
{
public:
    /**
     * Constructor, adds the factory to the global list
     * @param fallback True to add this factory at the end of the priority list
     */
    SignallingFactory(bool fallback = false);

    /**
     * Destructor, removes the factory from list
     */
    virtual ~SignallingFactory();

    /**
     * Builds a component given its name and arbitrary parameters
     * @param type The type of the component that should be returned
     * @param name Name of the requested component and additional parameters
     * @return Pointer to the created component, NULL on failure
     */
    static SignallingComponent* build(const String& type, NamedList* name = 0);

    /**
     * This method is for internal use only and must not be called directly
     * @param type The name of the interface that should be returned
     * @param name Name of the requested component and additional parameters
     * @return Raw pointer to the requested interface of the component, NULL on failure
     */
    static void* buildInternal(const String& type, NamedList* name);

protected:
    /**
     * Creates a component given its name and arbitrary parameters
     * @param type The name of the interface that should be returned
     * @param name Name of the requested component and additional parameters
     * @return Pointer to the created component
     */
    virtual SignallingComponent* create(const String& type, NamedList& name) = 0;
};

/**
 * Interface to an abstract signalling component that is managed by an engine.
 * The engine will periodically poll each component to keep them alive.
 * @short Abstract signalling component that can be managed by the engine
 */
class YSIG_API SignallingComponent : public RefObject, public DebugEnabler
{
    YCLASS(SignallingComponent,RefObject)
    friend class SignallingEngine;
public:
    /**
     * Destructor, detaches the engine and other components
     */
    virtual ~SignallingComponent();

    /**
     * Get the component's name so it can be used for list searches
     * @return A reference to the name by which the component is known to engine
     */
    virtual const String& toString() const;

    /**
     * Configure and initialize the component and any subcomponents it may have
     * @param config Optional configuration parameters override
     * @return True if the component was initialized properly
     */
    virtual bool initialize(const NamedList* config);

    /**
     * Choose parameters that should be used for object initialization
     * @param cmpName The name of the parameter holding the component name
     * @param params The list of parameters used to initialize the component
     * @param config The received list of parameters
     * @return True if the config was resolved
     */
    static bool resolveConfig(const String& cmpName, NamedList& params, const NamedList* config);

    /**
     * Query or modify component's settings or operational parameters
     * @param params The list of parameters to query or change
     * @return True if the control operation was executed
     */
    virtual bool control(NamedList& params);

    /**
     * Create a parameter list adequate to control this component
     * @param oper Optional name of the operation to execute
     * @return A new parameter list or descendant object, NULL if not supported
     */
    virtual NamedList* controlCreate(const char* oper = 0);

    /**
     * Execute or postpone a control command
     * @param params Parameter list describing the command, will be destroyed
     * @return True if the command was accepted (but not necessarily executed)
     */
    virtual bool controlExecute(NamedList* params);

    /**
     * Set the @ref TelEngine::SignallingEngine that manages this component
     *  and any subcomponent of it
     * @param eng Pointer to the engine that will manage this component
     */
    virtual void engine(SignallingEngine* eng);

    /**
     * Get the @ref TelEngine::SignallingEngine that manages this component
     * @return Pointer to engine or NULL if not managed by an engine
     */
    inline SignallingEngine* engine() const
	{ return m_engine; }

    /**
     * Conditionally set the debug level of the component
     * @param level Desired debug level, negative for no change
     * @return Current debug level
     */
    inline int debugLevel(int level)
	{ return (level >= 0) ? DebugEnabler::debugLevel(level) : DebugEnabler::debugLevel(); }

    /**
     * Return the type of this component
     * @return A string version of the component type
     */
    inline const String& componentType() const
	{ return m_compType; }

protected:
    /**
     * Constructor with a default empty component name
     * @param name Name of this component
     * @param params Optional pointer to creation parameters
     * @param type Default component type string
     */
    SignallingComponent(const char* name = 0, const NamedList* params = 0, const char* type = "unknown");

    /**
     * This method is called to clean up and destroy the object after the
     *  reference counter becomes zero
     */
    virtual void destroyed();

    /**
     * Insert another component in the same engine as this one.
     * This method should be called for every component we attach.
     * @param component Pointer to component to insert in engine
     */
    void insert(SignallingComponent* component);

    /**
     * Detach this component from all its links - components and engine.
     * Reimplement this method in all components that keep pointers to
     *  other components.
     * The default implementation detaches from the engine.
     */
    virtual void detach();

    /**
     * Method called periodically by the engine to keep everything alive
     * @param when Time to use as computing base for events and timeouts
     */
    virtual void timerTick(const Time& when);

    /**
     * Change the name of the component after it was constructed
     * @param name Name of this component
     */
    void setName(const char* name);

    /**
     * Change the type of the component after it was constructed
     * @param type Type of this component
     */
    inline void setCompType(const char* type)
	{ m_compType = type; }

    /**
     * Adjust (decrease only) the desired maximum time until next tick.
     * Can be called only from within timerTick()
     * @param usec Desired time until next engine's timerTick() call in usec
     * @return Timer sleep in usec after applying the current change
     */
    unsigned long tickSleep(unsigned long usec = 1000000) const;

private:
    SignallingEngine* m_engine;
    String m_name;
    String m_compType;
};

/**
 * The engine is the center of all SS7 or ISDN applications.
 * It is used as a base to build the protocol stack from components.
 * @short Main signalling component holder
 */
class YSIG_API SignallingEngine : public DebugEnabler, public Mutex
{
    friend class SignallingComponent;
    friend class SignallingThreadPrivate;
public:
    /**
     * Constructor of an empty engine
     * @param name The debug name of this engine
     */
    SignallingEngine(const char* name = "signalling");

    /**
     * Destructor, removes all components
     */
    virtual ~SignallingEngine();

    /**
     * Get a pointer to the first (and usually only) instance created
     * @param create Create the instance if it doesn't exist
     * @return Pointer to first engine, NULL if not created
     */
    static SignallingEngine* self(bool create = false);

    /**
     * Insert a component in the engine, lock the list while doing so
     * @param component Pointer to component to insert in engine
     */
    void insert(SignallingComponent* component);

    /**
     * Remove a component from the engine, lock the list while doing so
     * @param component Pointer to component to remove from engine
     */
    void remove(SignallingComponent* component);

    /**
     * Remove and destroy a component from the engine by name
     * @param name Name of component to remove from engine
     * @return True if a component was found and destroyed
     */
    bool remove(const String& name);

    /**
     * Retrieve a component by name, lock the list while searching for it
     * @param name Name of the component to find
     * @return Pointer to component found or NULL
     */
    SignallingComponent* find(const String& name);

    /**
     * Retrieve a component by name and class, lock the list while searching for it
     * @param name Name of the component to find, empty to find any of the type
     * @param type Class or base class of the component to find, empty to match any
     * @param start Component to start searching from, search all list if NULL
     * @return Pointer to component found or NULL
     */
    SignallingComponent* find(const String& name, const String& type, const SignallingComponent* start = 0);

    /**
     * Retrieve and reference an existing component, create by factory if not present
     * @param type Class or base class of the component to find or create
     * @param params Name of component to find or create and creation parameters
     * @param init Set to true to initialize a newly created component
     * @param ref True to add a reference when returning existing component
     * @return Pointer to component found or created, NULL on failure
     */
    SignallingComponent* build(const String& type, NamedList& params, bool init = false, bool ref = true);

    /**
     * Apply a control operation to all components in the engine
     * @param params The list of parameters to query or change
     * @return True if the control operation was executed by at least one component
     */
    bool control(NamedList& params);

    /**
     * Check if a component is in the engine's list
     * @param component Pointer to component to check
     * @return True if the component is in the engine's list
     */
    bool find(const SignallingComponent* component);

    /**
     * Handle notifications from a SignallingComponent
     * @param component The SignallingComponent from which the notifications were received
     * @param notifs The notifications sent by this SignallingComponent
     */
    void notify(SignallingComponent* component, NamedList notifs);

    /**
     * Starts the worker thread that keeps components alive
     * @param name Static name of the thread
     * @param prio Thread's priority
     * @param usec How long to sleep between iterations in usec, 0 to use library default
     * @return True if (already) started, false if an error occured
     */
    bool start(const char* name = "Sig Engine", Thread::Priority prio = Thread::Normal, unsigned long usec = 0);

    /**
     * Stops and destroys the worker thread if running
     */
    void stop();

    /**
     * Add to this engine a notifier object
     * @param notifier The SignallingNotifier object to be added to the engine
     */
    inline void setNotifier(SignallingNotifier* notifier)
	{ m_notifier = notifier; }

    /**
     * Remove from this engine a notifier object
     * @param notifier The SignallingNotifier object to be removed from the engine
     */
    inline void removeNotifier(SignallingNotifier* notifier)
    {
	if (m_notifier == notifier)
	    m_notifier = 0;
    }

    /**
     * Return a pointer to the worker thread
     * @return Pointer to running worker thread or NULL
     */
    Thread* thread() const;

    /**
     * Adjust (decrease only) the desired maximum time until next tick.
     * Can be called only from within timerTick()
     * @param usec Desired time until next timerTick() call in usec
     * @return Timer sleep in usec after applying the current change
     */
    unsigned long tickSleep(unsigned long usec = 1000000);

    /**
     * Get the default engine tick sleep time in microseconds
     * @return Default timer sleep in usec
     */
    inline unsigned long tickDefault() const
	{ return m_usecSleep; }

    /**
     * Get the maximum time we should spend acquiring a non-critical Mutex
     * @return Maximum non-critical lock wait in usec, -1 to wait forever
     */
    inline static long maxLockWait()
	{ return s_maxLockWait; }

    /**
     * Set the maximum time we should spend acquiring a non-critical Mutex
     * @param maxWait New maximum non-critical lock wait in usec, negative to wait forever
     */
    static void maxLockWait(long maxWait);

    /**
     * Helper template used to remove a component descendant from its engine,
     *  destroy it and set the received pointer to 0
     * @param obj Reference to pointer (lvalue) to the object to remove and destroy
     */
    template <class Obj> static inline void destruct(Obj*& obj)
    {
	if (!obj)
	    return;
	if (obj->engine())
	    obj->engine()->remove(obj);
	TelEngine::destruct(obj);
    }

protected:
    /**
     * Method called periodically by the worker thread to keep everything alive
     * @param when Time to use as computing base for events and timeouts
     * @return Desired sleep (in usec) until thread's next tick interval
     */
    virtual unsigned long timerTick(const Time& when);

    /**
     * The list of components managed by this engine
     */
    ObjList m_components;

private:
    SignallingThreadPrivate* m_thread;
    SignallingNotifier* m_notifier;
    unsigned long m_usecSleep;
    unsigned long m_tickSleep;
    static long s_maxLockWait;
};

/**
 * Interface of protocol independent signalling message
 * @short Abstract signalling message
 */
class YSIG_API SignallingMessage : public RefObject
{
    YCLASS(SignallingMessage,RefObject)
public:
    /**
     * Constructor
     * @param name Named list's name
     */
    inline SignallingMessage(const char* name = 0)
	: m_params(name)
	{ }

    /**
     * Get the name of the message
     * @return The name of the message
     */
    inline const char* name() const
	{ return m_params.c_str(); }

    /**
     * Get this message's parameter list
     * @return This message's parameter list
     */
    inline NamedList& params()
	{ return m_params; }

    /**
     * Get this message's parameter list - const version
     * @return This message's parameter list
     */
    inline const NamedList& params() const
	{ return m_params; }

protected:
    /**
     * Message parameter list
     */
    NamedList m_params;
};

/**
 * Interface of protocol independent signalling for phone calls
 * @short Abstract phone call signalling
 */
class YSIG_API SignallingCallControl : public Mutex
{
    friend class SignallingCall;
    friend class SS7ISUPCall;
    friend class ISDNQ931Call;
    friend class ISDNQ931CallMonitor;
public:

    /**
     * When is media absolutely required during the call
     */
    enum MediaRequired {
	MediaNever,
	MediaAnswered,
	MediaRinging,
	MediaAlways
    };

    /**
     * Constructor
     * @param params Call controller's parameters
     * @param msgPrefix Optional prefix to be added before a decoded message's
     *  parameters or retrieve message parameters from a list
     */
    SignallingCallControl(const NamedList& params, const char* msgPrefix = 0);

    /**
     * Destructor
     */
    virtual ~SignallingCallControl();

    /**
     * Retrieve Q.850 cause location
     * @return Controller location
     */
    inline const String& location() const
	{ return m_location; }

    /**
     * Set exiting flag
     */
    inline void setExiting()
	{ m_exiting = true; }

    /**
     * Get exiting flag
     * @return The exiting flag
     */
    inline bool exiting() const
	{ return m_exiting; }

    /**
     * Check the verify event flag. Reset it if true is returned
     * @return True if the verify event flag is set
     */
    inline bool verify()
    {
	Lock lock(this);
	if (!m_verifyEvent)
	    return false;
	m_verifyEvent = false;
	return true;
    }

    /**
     * Get the Media Required flag
     * @return Configured media requirement
     */
    inline MediaRequired mediaRequired() const
	{ return m_mediaRequired; }

    /**
     * Get the prefix used by this call controller when decoding message parameters or
     *  retrieve message parameters from a list
     * @return Message parameters prefix used by this call controller
     */
    inline const String& msgPrefix() const
	{ return m_msgPrefix; }

    /**
     * Get the circuit group attached to this call controller
     * @return The circuit group attached to this call controller
     */
    inline SignallingCircuitGroup* circuits() const
	{ return m_circuits; }

    /**
     * Get the list of calls currently known by this call controller
     * @return Reference to the list of calls
     */
    inline const ObjList& calls() const
	{ return m_calls; }

    /**
     * Get the controller's status as text
     * @return Controller status name
     */
    virtual const char* statusName() const = 0;

    /**
     * Attach/detach a circuit group to this call controller. Set group's allocation strategy.
     * Set locked flags for all circuits belonging to the attached circuit group.
     * Cleanup controller before detaching the group or attaching a new one
     * This method is thread safe
     * @param circuits Pointer to the SignallingCircuitGroup to attach. 0 to detach and force a cleanup
     * @return Pointer to the old group that was detached, NULL if none or no change
     */
    SignallingCircuitGroup* attach(SignallingCircuitGroup* circuits);

    /**
     * Reserve a circuit for later use. If the circuit list is 0, try to reserve a circuit from
     *  the group using its strategy. Release the given circuit before trying to reserve it.
     *  Set cic to 0 on failure.
     * This method is thread safe
     * @param cic Destination circuit
     * @param checkLock Lock flags to check. If the given lock flags are set, reservation will fail
     * @param range Optional range name to restrict circuit reservation within attached circuit group
     * @param list Comma separated list of circuits
     * @param mandatory The list is mandatory. If false and none of the circuits in
     *  the list are available, try to reserve a free one. Ignored if list is 0
     * @param reverseRestrict Used when failed to reserve circuit from list. If true and the circuit allocation
     *  strategy includes any restriction (odd or even) use the opposite restriction to reserve a circuit.
     *  Ignored if mandatory is true
     * @return False if the operation failed
     */
    bool reserveCircuit(SignallingCircuit*& cic, const char* range = 0, int checkLock = -1,
	const String* list = 0,	bool mandatory = true, bool reverseRestrict = false);

    /**
     * Initiate a release of a circuit. Set cic to 0.
     * This method is thread safe
     * @param cic The circuit to release
     * @param sync Synchronous release requested
     * @return True if the circuit release was initiated
     */
    bool releaseCircuit(SignallingCircuit*& cic, bool sync = false);

    /**
     * Initiate a release of a circuit from the attached group
     * This method is thread safe
     * @param code The circuit's code
     * @param sync Synchronous release requested
     * @return True if the circuit release was initiated
     */
    bool releaseCircuit(unsigned int code, bool sync = false);

    /**
     * Cleanup
     * @param reason Cleanup reason
     */
    virtual void cleanup(const char* reason = "net-out-of-order")
	{ }

    /**
     * Iterate through the call list to get an event
     * @param when The current time
     * @return SignallingEvent pointer or 0 if no events
     */
    virtual SignallingEvent* getEvent(const Time& when);

    /**
     * Create an outgoing call. Send a NewCall event with the given msg parameter
     * @param msg Call parameters
     * @param reason Failure reason if any
     * @return Referenced SignallingCall pointer on success or 0 on failure
     */
    virtual SignallingCall* call(SignallingMessage* msg, String& reason)
	{ reason = "not-implemented"; return 0; }

    /**
     * Build the parameters of a Verify event
     * @param params The list of parameters to fill
     */
    virtual void buildVerifyEvent(NamedList& params)
	{ }

protected:
    /**
     * Get the strategy used by the attached circuit group to allocate circuits
     * @return The strategy used by the attached circuit group to allocate circuits
     */
    inline int strategy() const
	{ return m_strategy; }

    /**
     * Process an event received from a call. This will give to derived classes an opportunity
     *  to intercept events generated by their calls
     * @param event The event
     * @return True if the event was processed by the controller.
     *  False to deliver the event to the requestor
     */
    virtual bool processEvent(SignallingEvent* event)
	{ return false; }

    /**
     * Process an event received from a non-reserved circuit
     * @param event The event, will be consumed and zeroed
     * @param call Optional signalling call whose circuit generated the event
     * @return Signalling event pointer or 0
     */
    virtual SignallingEvent* processCircuitEvent(SignallingCircuitEvent*& event,
	SignallingCall* call = 0)
	{ TelEngine::destruct(event); return 0; }

    /**
     * Clear call list
     */
    void clearCalls();

    /**
     * Remove a call from list
     * @param call The call to remove
     * @param del True to delete it. False to remove without destruct
     */
    void removeCall(SignallingCall* call, bool del = false);

    /**
     * Set the verify event flag. Restart/fire verify timer
     * @param restartTimer True to restart/fire the timer
     * @param fireNow True to fire the verify timer. Ignored if restartTimer is false
     * @param time Optional time to use for timer restart
     */
    void setVerify(bool restartTimer = false, bool fireNow = false, const Time* time = 0);

    /**
     * List of active calls
     */
    ObjList m_calls;

    /**
     * Prefix to be added to decoded message parameters or
     *  retrieve message parameters from a list
     */
    String m_msgPrefix;

    /**
     * Media required flag, call should drop if requirement not satisfied
     */
    MediaRequired m_mediaRequired;

    /**
     * Draw attention to call controller's user that something changed by
     *  raising a Verify event
     */
    bool m_verifyEvent;

    /**
     * Timer used to raise verify events
     */
    SignallingTimer m_verifyTimer;

    /**
     * Controller location used when encoding Q.850 cause
     */
    String m_location;

    /**
     * Media required keywords
     */
    static const TokenDict s_mediaRequired[];

private:
    SignallingCircuitGroup* m_circuits;  // Circuit group
    int m_strategy;                      // Strategy to allocate circuits for outgoing calls
    bool m_exiting;                      // Call control is terminating. Generate a Disable event when no more calls
};

/**
 * Interface of protocol independent phone call
 * @short Abstract single phone call
 */
class YSIG_API SignallingCall : public RefObject, public Mutex
{
public:
    /**
     * Constructor
     * @param controller The call controller owning this call
     * @param outgoing Call direction (true for outgoing)
     * @param signalOnly Just signalling (no voice) flag
     */
    SignallingCall(SignallingCallControl* controller, bool outgoing, bool signalOnly = false);

    /**
     * Destructor, notifies the controller
     */
    virtual ~SignallingCall();

    /**
     * Check if this is an outgoing call
     * @return True if it's an outgoing call
     */
    inline bool outgoing() const
	{ return m_outgoing; }

    /**
     * Retreive the controller of this call
     */
    inline SignallingCallControl* controller() const
	{ return m_controller; }

    /**
     * Set this call's private user data
     * @param data New user data
     */
    inline void userdata(void* data)
	{ m_private = data; }

    /**
     * Retreive the private user data of this call
     * @return User data
     */
    inline void* userdata() const
	{ return m_private; }

    /**
     * Check if this call is just a signalling (no voice) one
     * @return True if no audio data can be negotiated for this call
     */
    inline bool signalOnly() const
	{ return m_signalOnly; }

    /**
     * Check if this call is in overlapped send/recv state
     * @return True if this call is expecting more digits to be sent/received
     */
    inline bool overlapDialing() const
	{ return m_overlap; }

    /**
     * Send an event to this call
     * @param event The event to send
     * @return True if the operation succedded
     */
    virtual bool sendEvent(SignallingEvent* event)
	{ return false; }

    /**
     * Get an event from this call if not already got one
     * This method is thread safe
     * @param when The current time
     * @return SignallingEvent pointer or 0 if no events or this call has a not terminated event
     */
    virtual SignallingEvent* getEvent(const Time& when) = 0;

    /**
     * Event terminated notification. No event will be generated until
     *  the current event is terminated
     * This method is thread safe
     * @param event The terminated event
     */
    virtual void eventTerminated(SignallingEvent* event);

protected:
    /**
     * Enqueue a received message.
     * This method is thread safe
     * @param msg The received message
     */
    void enqueue(SignallingMessage* msg);

    /**
     * Dequeue a received message. Just return it if remove is false
     * This method is thread safe
     * @param remove True to remove the message from queue
     * @return SignallingMessage pointer or 0 if no more messages
     */
    SignallingMessage* dequeue(bool remove = true);

    /**
     * Clear incoming messages queue
     */
    void clearQueue()
    {
	Lock lock(m_inMsgMutex);
	m_inMsg.clear();
    }

    /**
     * Last event generated by this call. Used to serialize events
     */
    SignallingEvent* m_lastEvent;

    /**
     * Call is in overlapped send/recv state
     */
    bool m_overlap;

private:
    SignallingCallControl* m_controller; // Call controller this call belongs to
    bool m_outgoing;                     // Call direction
    bool m_signalOnly;                   // Just signalling flag
    ObjList m_inMsg;                     // Incoming messages queue
    Mutex m_inMsgMutex;                  // Lock incoming messages queue
    void* m_private;                     // Private user data
};

/**
 * An object holding a signalling event and related references
 * @short A single signalling related event
 */
class YSIG_API SignallingEvent
{
public:
    /**
     * Type of the event
     */
    enum Type {
	Unknown = 0,
	Generic,
	// Call related
	NewCall,
	Accept,
	Connect,
	Complete,
	Progress,
	Ringing,
	Answer,
	Transfer,
	Suspend,
	Resume,
	Release,
	Info,
	Charge,
	// Non-call related
	Message,
	Facility,
	Circuit,
	// Controller related
	Enable,
	Disable,
	Reset,
	Verify,
    };

    /**
     * Constructor for a call related event
     * @param type Type of the event
     * @param message Message carried by the event
     * @param call Call this event refers to
     */
    SignallingEvent(Type type, SignallingMessage* message, SignallingCall* call);

    /**
     * Constructor for a controller related event
     * @param type Type of the event
     * @param message Message carried by the event
     * @param controller Controller this event refers to
     */
    SignallingEvent(Type type, SignallingMessage* message, SignallingCallControl* controller = 0);

    /**
     * Constructor for a signalling circuit related event
     * @param event The event signaled by the circuit, will be consumed and zeroed
     * @param call Call this event refers to
     */
    SignallingEvent(SignallingCircuitEvent*& event, SignallingCall* call);

    /**
     * Destructor, dereferences any resources, notify the signalling call of termination
     */
    virtual ~SignallingEvent();

    /**
     * Get the string associated with this event's type
     * @return The string associated with this event's type, if any
     */
    inline const char* name() const
	{ return typeName(type()); }

    /**
     * Get the type of the event
     * @return Type of event, may be unknown
     */
    inline Type type() const
	{ return m_type; }

    /**
     * Get the call that generated this event, may be NULL
     */
    inline SignallingCall* call() const
	{ return m_call; }

    /**
     * Get the message that generated this event, may be NULL
     */
    inline SignallingMessage* message() const
	{ return m_message; }

    /**
     * Retrieve the controller of the call
     */
    inline SignallingCallControl* controller() const
	{ return m_controller; }

    /**
     * Retrieve the circuit event
     */
    inline SignallingCircuitEvent* cicEvent() const
	{ return m_cicEvent; }

    /**
     * Get the text associated with a given event type for debug purposes
     * @param t The requested type
     * @return The text associated with the given type
     */
    static inline const char* typeName(Type t)
	{ return lookup(t,s_types,0); }

    /**
     * Send this event through the call that generated it
     * @return True if there was a call and the operation succedded
     */
    bool sendEvent();

private:
    Type m_type;
    SignallingMessage* m_message;
    SignallingCall* m_call;
    SignallingCallControl* m_controller;
    SignallingCircuitEvent* m_cicEvent;
    static const TokenDict s_types[];
};

/**
 * An object holding a signalling circuit event and related references
 * @short A single signalling circuit related event
 */
class YSIG_API SignallingCircuitEvent : public NamedList
{
public:
    /**
     * Type of the event
     */
    enum Type {
	Unknown      = 0,
	Dtmf         = 1,                // Transfer tones: param: tone
	GenericTone  = 2,                // Play or detect tones: param: tone
	// Analog line events
	Timeout      = 10,               //
	Polarity     = 11,               // Line's polarity changed
	StartLine    = 15,               // Initialize FXO line
	LineStarted  = 16,               // FXO line initialized: send number
	DialComplete = 17,               // FXO line completed dialing the number
	OnHook       = 20,               // The hook is down
	OffHook      = 21,               // The hook is up
	RingBegin    = 22,               // Start ringing
	RingEnd      = 23,               // Stop ringing
	RingerOn     = 30,               // An FXS started the FXO's ringer
	RingerOff    = 31,               // An FXS stopped the FXO's ringer
	Wink         = 32,               // On hook momentarily
	Flash        = 33,               // Off hook momentarily
	PulseStart   = 40,               // Pulse dialing start
	PulseDigit   = 41,               // Transfer pulse digits: param: pulse
	// Remote circuit events
	Connect      = 50,               // Request connect
	Disconnect   = 51,               // Request disconnect
	Connected    = 52,               // Connected notification
	Disconnected = 53,               // Disconnected notification
	// Errors
	Alarm        = 100,              // Param: alarms (comma separated list of strings)
	NoAlarm      = 101,              // No more alarms
    };

    /**
     * Constructor for a circuit related event
     * @param cic The circuit that generated this event
     * @param type Event type as enumeration
     * @param name Optional name for the named list
     */
    SignallingCircuitEvent(SignallingCircuit* cic, Type type, const char* name = 0);

    /**
     * Destructor, dereferences any resources
     */
    virtual ~SignallingCircuitEvent();

    /**
     * Get the type of this event
     * @return The type of this event
     */
    inline Type type() const
	{ return m_type; }

    /**
     * Get the circuit that generated this event
     * @return The circuit that generated this event
     */
    inline SignallingCircuit* circuit()
	{ return m_circuit; }

    /**
     * Send this event through the circuit. Release (delete) the event
     * @return True if the operation succeded
     */
    bool sendEvent();

private:
    SignallingCircuit* m_circuit;
    Type m_type;
};

/**
 * Interface to an abstract voice/data circuit referenced by signalling
 * @short Abstract data circuit used by signalling
 */
class YSIG_API SignallingCircuit : public RefObject
{
    YCLASS(SignallingCircuit,RefObject)
    friend class SignallingCircuitGroup;
    friend class SignallingCircuitEvent;
public:
    /**
     * Type of the circuit hardware or transport
     */
    enum Type {
	Unknown = 0,
	Local, // not really a circuit
	TDM,
	RTP,
	IAX,
    };

    /**
     * Status of the circuit
     */
    enum Status {
	Missing = 0,
	Disabled,
	Idle,
	Reserved,
	Starting,
	Stopping,
	Special,
	Connected,
    };

    /**
     * Lock circuit flags
     */
    enum LockFlags {
	LockLocalHWFail       = 0x0001,  // Local side of the circuit is locked due to HW failure
	LockLocalMaint        = 0x0002,  // Local side of the circuit is locked for maintenance
	LockingHWFail         = 0x0004,  // Circuit (un)lock due to HW failure in progress
	LockingMaint          = 0x0008,  // Circuit (un)lock due to maintenance in progress
	LockLocalHWFailChg    = 0x0010,  // Local HW failure flag changed
	LockLocalMaintChg     = 0x0020,  // Local maintenance flag changed
	Resetting             = 0x0040,  // Circuit is beeing reset
	LockRemoteHWFail      = 0x0100,  // Remote side of the circuit is locked due to HW failure
	LockRemoteMaint       = 0x0200,  // Remote side of the circuit is locked for maintenance
	LockRemoteHWFailChg   = 0x1000,  // Remote HW failure flag changed
	LockRemoteMaintChg    = 0x2000,  // Remote maintenance flag changed
	// Masks used to test lock conditions
	LockLocal             = LockLocalHWFail | LockLocalMaint,
	LockRemote            = LockRemoteHWFail | LockRemoteMaint,
	LockLocked            = LockLocal | LockRemote,
	LockBusy              = LockingHWFail | LockingMaint | Resetting,
	LockLockedBusy        = LockLocked | LockBusy,
	LockLocalChg          = LockLocalHWFailChg | LockLocalMaintChg,
	LockRemoteChg         = LockRemoteHWFailChg | LockRemoteMaintChg,
	LockChanged           = LockLocalChg | LockRemoteChg,
    };

    /**
     * Destructor. Clear event list
     */
    virtual ~SignallingCircuit();

    /**
     * Initiate a status transition
     * @param newStat Desired new status
     * @param sync Synchronous status change requested
     * @return True if status change has been initiated
     */
    virtual bool status(Status newStat, bool sync = false)
	{ m_status = newStat; return true; }

    /**
     * Get the type of this circuit
     * @return Enumerated type of circuit
     */
    inline Type type() const
	{ return m_type; }

    /**
     * Get the status of this circuit
     * @return Enumerated status of circuit
     */
    inline Status status() const
	{ return m_status; }

    /**
     * Check if the given lock flags are set
     * @param flags The lock flags to check. -1 to check all flags
     * @return The lock flags of this circuit masked by the given flags
     */
    inline int locked(int flags = -1) const
	{ return (m_lock & flags); }

    /**
     * Set the given lock flags of this circuit
     * @param flags The lock flags to set
     */
    inline void setLock(int flags)
	{ m_lock |= flags; }

    /**
     * Reset the given lock flags of this circuit
     * @param flags The lock flags to reset
     */
    inline void resetLock(int flags)
	{ m_lock &= ~flags; }

    /**
     * Set the format of the data transported through this circuit
     * @param format The new data format
     * @param direction The direction to be updated. -1 means to the lower layer, 1 from the lower layer, 0 both directions
     * @return True if the operation succeedded (format changed)
     */
    virtual bool updateFormat(const char* format, int direction)
	{ return false; }

    /**
     * Set circuit data or trigger some action
     * @param param The data to update or the action to trigger
     * @param value The data value or action parameter
     * @return True on success
     */
    virtual bool setParam(const String& param, const String& value)
	{ return false; }

    /**
     * Set circuit data from a list of parameters
     * @param params Parameter list to set in the circuit
     * @return True if all parameters were successfully set
     */
    virtual bool setParams(const NamedList& params);

    /**
     * Get circuit parameter
     * @param param The parameter to get
     * @param value The value of the parameter
     * @return True on success. False if the parameter doesn't exist
     */
    virtual bool getParam(const String& param, String& value) const
	{ return false; }

    /**
     * Get boolean circuit parameter
     * @param param The parameter to get
     * @param defValue The default returned value
     * @return Value from circuit, devValue if the parameter doesn't exist
     */
    virtual bool getBoolParam(const String& param, bool defValue = false) const
	{ return defValue; }

    /**
     * Get integer circuit parameter
     * @param param The parameter to get
     * @param defValue The default returned value
     * @return Value from circuit, devValue if the parameter doesn't exist
     */
    virtual int getIntParam(const String& param, int defValue = 0) const
	{ return defValue; }

    /**
     * Get circuit parameters
     * @param params Parameter list to fill from the circuit
     * @param category Optional category used to select desired parameters
     * @return True if handled
     */
    virtual bool getParams(NamedList& params, const String& category = String::empty())
	{ return false; }

    /**
     * Get the group of circuits this one belongs to
     * @return Pointer to circuit group
     */
    inline SignallingCircuitGroup* group()
	{ return m_group; }

    /**
     * Get the circuit span this one belongs to
     * @return Pointer to circuit span
     */
    inline SignallingCircuitSpan* span()
	{ return m_span; }

    /**
     * Get the group of circuits this one belongs to - const version
     * @return Pointer to const circuit group
     */
    inline const SignallingCircuitGroup* group() const
	{ return m_group; }

    /**
     * Get the circuit span this one belongs to - const version
     * @return Pointer to const circuit span
     */
    inline const SignallingCircuitSpan* span() const
	{ return m_span; }

    /**
     * Get the group-local code of this circuit
     * @return Identification code within group
     */
    inline unsigned int code() const
	{ return m_code; }

    /**
     * Get the available status of the circuit
     * @return True if the circuit is available for use
     */
    inline bool available() const
	{ return m_status == Idle; }

    /**
     * Get the connected status of the circuit
     * @return True if the circuit is connected (in use)
     */
    inline bool connected() const
	{ return m_status == Connected; }

    /**
     * Reserve this circuit for later use
     * @return True if the circuit was changed from Idle to Reserved
     */
    inline bool reserve()
	{ return available() && status(Reserved,true); }

    /**
     * Connect this circuit
     * @param format Optional data format to update for both directions
     * @return True if the circuit state was changed to Connected
     */
    inline bool connect(const char* format = 0)
	{ updateFormat(format,0); return status(Connected,true); }

    /**
     * Disconnect (set state to Reserved) this circuit if connected
     * @return True if the circuit was changed from Connected to Reserved
     */
    inline bool disconnect()
	{ return status() == Connected && status(Reserved,true); }

    /**
     * Disable this circuit for maintenance
     * @return True if the circuit was changed from Idle to Reserved
     */
    inline bool disable()
	{ return status(Disabled,true); }

    /**
     * Set/reset HW failure lock flag
     * @param set True to set, false to reset the flag
     * @param remote True to use remote side of the circuit, false to use the local one
     * @param changed Set/reset changed flag. If false the changed flag won't be affected
     * @param setChanged The value of the changed flag. gnored if changed is false
     * @return True if the flag's state changed
     */
    bool hwLock(bool set, bool remote, bool changed = false, bool setChanged = false);

    /**
     * Set/reset maintenance lock flag
     * @param set True to set, false to reset the flag
     * @param remote True to use remote side of the circuit, false to use the local one
     * @param changed Set/reset changed flag. If false the changed flag won't be affected
     * @param setChanged The value of the changed flag. gnored if changed is false
     * @return True if the flag's state changed
     */
    bool maintLock(bool set, bool remote, bool changed = false, bool setChanged = false);

    /**
     * Add an event to the queue
     * This method is thread safe
     * @param event The event to enqueue
     */
    void addEvent(SignallingCircuitEvent* event);

    /**
     * Get an event from queue
     * This method is thread safe
     * @param when The current time
     * @return SignallingCircuitEvent pointer or 0 if no events
     */
    SignallingCircuitEvent* getEvent(const Time& when);

    /**
     * Send an event through this circuit
     * @param type The type of the event to send
     * @param params Optional event parameters
     * @return True on success
     */
    virtual bool sendEvent(SignallingCircuitEvent::Type type, NamedList* params = 0);

    /**
     * Get the text associated with a circuit type
     * @param type Circuit type used to retrieve the text
     * @return Pointer to the string associated with the given circuit type
     */
    static const char* lookupType(int type);

    /**
     * Get the text associated with a circuit status
     * @param status Circuit status used to retrieve the text
     * @return Pointer to the string associated with the given circuit status
     */
    static const char* lookupStatus(int status);

    /**
     * Keep the lock flags names
     */
    static const TokenDict s_lockNames[];

protected:
    /**
     * Constructor
     */
    SignallingCircuit(Type type, unsigned int code, SignallingCircuitGroup* group = 0,
	SignallingCircuitSpan* span = 0);

    /**
     * Constructor
     */
    SignallingCircuit(Type type, unsigned int code, Status status,
	SignallingCircuitGroup* group = 0, SignallingCircuitSpan* span = 0);

    /**
     * Clear event queue
     * This method is thread safe
     */
    virtual void clearEvents();

    /**
     * Event termination notification
     * @param event The terminated event
     */
    void eventTerminated(SignallingCircuitEvent* event);

    /**
     * Circuit operations mutex
     */
    Mutex m_mutex;

private:
    SignallingCircuitGroup* m_group;     // The group owning this circuit
    SignallingCircuitSpan* m_span;       // The span this circuit belongs to
    unsigned int m_code;                 // Circuit id
    Type m_type;                         // Circuit type (see enumeration)
    Status m_status;                     // Circuit local status
    int m_lock;                          // Circuit lock flags
    ObjList m_events;                    // In-band events
    SignallingCircuitEvent* m_lastEvent; // The last generated event
    bool m_noEvents;                     // No events pending, exit quickly
};

/**
 * Keeps a range (set) of circuits. The circuit codes contained within a range may
 *  not be contiguous.
 * See @ref SignallingUtils::parseUIntArray() for the format of the string ranges
 *  this object can be built from
 * @short A circuit range (set of circuits)
 */
class YSIG_API SignallingCircuitRange : public String
{
    YCLASS(SignallingCircuitRange,String)
    friend class SignallingCircuitGroup;
public:
    /**
     * Constructor
     * @param rangeStr String used to build this range
     * @param name Range name
     * @param strategy Strategy used to allocate circuits from this range
     */
    SignallingCircuitRange(const String& rangeStr, const char* name = 0,
	int strategy = -1);

    /**
     * Destructor
     */
    virtual ~SignallingCircuitRange()
	{ clear(); }

    /**
     * Get the number of circuits contained by this range
     * @return The number of circuits contained by this range
     */
    inline unsigned int count() const
	{ return m_count; }

    /**
     * Get the pointer to the circuit codes array
     * @return Pointer to the circuit codes array or 0
     */
    inline const unsigned int* range() const
	{ return (const unsigned int*)m_range.data(); }

    /**
     * Allocate and return an array containing range circuits
     * @param count Address of variable to be filled with circuit count
     * @return Pointer to allocated buffer, 0 if there is no circuit.
     *  The caller will own the returned buffer
     */
    unsigned int* copyRange(unsigned int& count) const;

    /**
     * Get the pointer to the circuit codes array
     * @return Pointer to the circuit codes array or 0
     */
    inline void clear()
	{ m_range.clear(); m_count = 0; }

    /**
     * Indexing operator
     * @param index The index in the array to retreive
     * @return The code at the given index
     */
    inline unsigned int operator[](unsigned int index)
	{ return range()[index]; }

    /**
     * Set this range from a string
     * @param rangeStr String used to (re)build this range
     * @return False if the string has invalid format
     */
    inline bool set(const String& rangeStr)
    {
	clear();
	return add(rangeStr);
    }

    /**
     * Add codes to this range from a string
     * @param rangeStr String containing the codes to be added to this range
     * @return False if the string has invalid format
     */
    bool add(const String& rangeStr);

    /**
     * Add an array of circuit codes to this range
     * @param codes The array to add
     * @param len The array's length
     */
    void add(unsigned int* codes, unsigned int len);

    /**
     * Add a circuit code to this range
     * @param code The circuit code to add
     */
    inline void add(unsigned int code)
	{ add(&code,1); }

    /**
     * Add a compact range of circuit codes to this range
     * @param first The first circuit code to add
     * @param last Number of last circuit code
     */
    void add(unsigned int first, unsigned int last);

    /**
     * Remove a circuit code from this range
     * @param code The circuit code to remove
     */
    void remove(unsigned int code);

    /**
     * Check if a circuit code is within this range
     * @param code The circuit code to find
     * @return True if found
     */
    bool find(unsigned int code);

    /**
     * Release memory
     */
    virtual void destruct()
    {
	clear();
	String::destruct();
    }

protected:
    void updateLast();                   // Update last circuit code

    DataBlock m_range;                   // Array containing the circuit codes
    unsigned int m_count;                // The number of elements in the array
    unsigned int m_last;                 // Last (the greater) not used circuit code within this range
    int m_strategy;                      // Keep the strategy used to allocate circuits from this range
    unsigned int m_used;                 // Last used circuit code
};

/**
 * Interface to a stateful group of voice/data circuits
 * @short Group of data circuits used by signalling
 */
class YSIG_API SignallingCircuitGroup : public SignallingComponent, public Mutex
{
    YCLASS(SignallingCircuitGroup,SignallingComponent)
    friend class SignallingCircuit;
    friend class SignallingCallControl;
    friend class SS7ISUP;
    friend class ISDNQ931;
public:
    /**
     * Circuit allocation strategy
     */
    enum Strategy {
	Other     = 0,
	// basic strategies
	Increment = 0x0001, // round-robin, up
	Decrement = 0x0002, // round-robin, down
	Lowest    = 0x0003, // pick first available
	Highest   = 0x0004, // pick last available
	Random    = 0x0005, // pick random circuit
	// even/odd strict select (glare avoidance)
	OnlyEven  = 0x1000,
	OnlyOdd   = 0x2000,
	// glare avoidance with fallback (to be able to use all circuits)
	Fallback  = 0x4000,
    };

    /**
     * Constructor, creates a group with a specific base code
     * @param base Base of identification codes for this group
     * @param strategy Default strategy used for circuit allocation
     * @param name Name of this component
     */
    SignallingCircuitGroup(unsigned int base = 0, int strategy = Increment,
	const char* name = "circgroup");

    /**
     * Destructor
     */
    virtual ~SignallingCircuitGroup();

    /**
     * Get the number of circuits in this group
     * @return The number of circuits owned by this group
     */
    inline unsigned int count() const
	{ return m_circuits.count(); }

    /**
     * Get the base of identification codes for this group
     * @return Base of identification codes for this group
     */
    inline unsigned int base() const
	{ return m_base; }

    /**
     * Get the maximum of identification codes for this group
     * @return The maximum of identification codes for this group
     */
    inline unsigned int last() const
	{ return m_range.m_last; }

    /**
     * Get the circuit allocation strategy
     * @return Strategy flags ORed together
     */
    inline int strategy() const
	{ return m_range.m_strategy; }

    /**
     * Set the circuit allocation strategy
     * @param strategy The new circuit allocation strategy
     */
    inline void setStrategy(int strategy)
	{ Lock lock(this); m_range.m_strategy = strategy; }

    /**
     * Get the circuit list
     */
    inline ObjList& circuits()
	{ return m_circuits; }

    /**
     * Create a comma separated list with this group's circuits
     * @param dest The destination string
     */
    void getCicList(String& dest);

    /**
     * Insert a circuit in the group
     * @param circuit Pointer to the circuit to insert
     * @return False if a circuit with the same code already exists
     */
    bool insert(SignallingCircuit* circuit);

    /**
     * Remove a circuit from the group
     * @param circuit Pointer to the circuit to remove
     */
    void remove(SignallingCircuit* circuit);

    /**
     * Create a circuit span using the factory
     * @param name Name of the span to create
     * @param start Desired start of circuit codes in span
     * @param params Optional parameters for creation of span and circuits
     * @return Pointer to new circuit span or NULL on failure
     */
    SignallingCircuitSpan* buildSpan(const String& name, unsigned int start = 0, NamedList* params = 0);

    /**
     * Insert a circuit span in the group
     * @param span Pointer to the circuit span to insert
     * @return False on failure
     */
    bool insertSpan(SignallingCircuitSpan* span);

    /**
     * Build and insert a range from circuits belonging to a given span
     * @param span Span to find
     * @param name Range name or 0 to use span's id
     * @param strategy Strategy used to allocate circuits from the new range,
     *  -1 to use group's strategy
     */
    void insertRange(SignallingCircuitSpan* span, const char* name,
	int strategy = -1);

    /**
     * Build and insert a range contained in a string.
     * See @ref SignallingUtils::parseUIntArray() for the format of the string range
     * @param range String used to build the range
     * @param name Range name
     * @param strategy Strategy used to allocate circuits from the new range,
     *  -1 to use group's strategy
     */
    void insertRange(const String& range, const char* name,
	int strategy = -1);

    /**
     * Remove a circuit span from the group
     * @param span Pointer to the circuit span to remove
     * @param delCics True to delete signalling circuits associated to the span
     * @param delSpan True to delete the span
     */
    void removeSpan(SignallingCircuitSpan* span, bool delCics = true, bool delSpan = false);

    /**
     * Remove signalling circuits associated to the given span
     * @param span Pointer to the circuit span whose circuits will be removed
     */
    void removeSpanCircuits(SignallingCircuitSpan* span);

    /**
     * Find a specific circuit by its identification code
     * @param cic Circuit Identification Code
     * @param local Interpret the cic parameter as group-local code
     * @return Pointer to circuit or NULL if not found
     */
    SignallingCircuit* find(unsigned int cic, bool local = false);

    /**
     * Find a range of circuits owned by this group
     * @param name The range name to find
     * @return Pointer to circuit range or 0 if not found
     */
    SignallingCircuitRange* findRange(const char* name);

    /**
     * Get the status of a circuit
     * @param cic Circuit Identification Code
     * @return Enumerated status of circuit
     */
    SignallingCircuit::Status status(unsigned int cic);

    /**
     * Initiate a circuit status transition
     * @param cic Circuit Identification Code
     * @param newStat Desired new status
     * @param sync Synchronous status change requested
     * @return True if status change has been initiated
     */
    bool status(unsigned int cic, SignallingCircuit::Status newStat, bool sync = false);

    /**
     * Reserve a circuit for later use
     * @param checkLock Lock flags to check. If the given lock flags are set, reservation will fail
     * @param strategy Strategy used for allocation, use group default if negative
     * @param range Range of circuits to allocate from. 0 to use group default
     * @return Referenced pointer to a reserved circuit or 0 on failure
     */
    SignallingCircuit* reserve(int checkLock = -1, int strategy = -1,
	SignallingCircuitRange* range = 0);

    /**
     * Reserve a circuit for later use
     * @param list Comma separated list of circuits
     * @param mandatory The list is mandatory. If false and none of the circuits in
     *  the list are available, try to reserve a free one
     * @param checkLock Lock flags to check. If the given lock flags are set, reservation will fail
     * @param strategy Strategy used for allocation if failed to allocate one from
     *  the list, use group default if negative
     * @param range Range of circuits to allocate from. 0 to use group default
     * @return Referenced pointer to a reserved circuit or 0 on failure
     */
    SignallingCircuit* reserve(const String& list, bool mandatory,
	int checkLock = -1, int strategy = -1, SignallingCircuitRange* range = 0);

    /**
     * Initiate a release of a circuit
     * @param cic Circuit to release
     * @param sync Synchronous release requested
     * @return True if the circuit release was initiated
     */
    inline bool release(SignallingCircuit* cic, bool sync = false)
	{ return cic && cic->status(SignallingCircuit::Idle,sync); }

    /**
     * Get the strategy value associated with a given name
     * @param name Strategy name whose value we want to obtain
     * @param def Value to return if not found
     * @return The requested strategy value or the default one
     */
    static int str2strategy(const char* name, int def = Increment)
	{ return lookup(name,s_strategy,def); }

    /**
     * Keep the strategy names
     */
    static const TokenDict s_strategy[];

protected:
    /**
     * Remove all spans and circuits. Release object
     */
    virtual void destroyed()
    {
	clearAll();
	SignallingComponent::destroyed();
    }

private:
    unsigned int advance(unsigned int n, int strategy, SignallingCircuitRange& range);
    void clearAll();

    ObjList m_circuits;                  // The circuits belonging to this group
    ObjList m_spans;                     // The spans belonging to this group
    ObjList m_ranges;                    // Additional circuit ranges
    SignallingCircuitRange m_range;      // Range containing all circuits belonging to this group
    unsigned int m_base;
};

/**
 * An interface to a span belonging to a circuit group
 * @short A span in a circuit group
 */
class YSIG_API SignallingCircuitSpan : public SignallingComponent
{
    YCLASS(SignallingCircuitSpan,SignallingComponent)
public:
    /**
     * Destructor. Remove from group's queue
     */
    virtual ~SignallingCircuitSpan();

    /**
     * Get the owner of this span
     * @return SignallingCircuitGroup pointer or 0
     */
    inline SignallingCircuitGroup* group() const
	{ return m_group; }

    /**
      * Get this span's id
      * @return The id of this span
      */
    inline const String& id() const
	{ return m_id; }

    /**
     * Get the increment in circuit numbers caused by this span
     * @return Circuit number increment for this span
     */
    inline unsigned int increment() const
	{ return m_increment; }

protected:
    /**
     * Constructor
     * @param id Optional span id
     * @param group Optional circuit group owning the span's circuits
     */
    SignallingCircuitSpan(const char* id = 0, SignallingCircuitGroup* group = 0);

    /**
     * The owner of this span
     */
    SignallingCircuitGroup* m_group;

    /**
     * The increment in channel code caused by this span
     */
    unsigned int m_increment;

private:
    String m_id;                         // Span's id
};

/**
 * An interface to an abstraction of a Layer 1 (hardware HDLC) interface
 * @short Abstract digital signalling interface (hardware access)
 */
class YSIG_API SignallingInterface : virtual public SignallingComponent
{
    YCLASS(SignallingInterface,SignallingComponent)
    friend class SignallingReceiver;
public:
    /**
     * Interface control operations
     */
    enum Operation {
	Specific  = 0,
	EnableTx  = 0x01,
	EnableRx  = 0x02,
	Enable    = 0x03,
	DisableTx = 0x04,
	DisableRx = 0x08,
	Disable   = 0x0c,
	FlushTx   = 0x10,
	FlushRx   = 0x20,
	Flush     = 0x30,
	QueryTx   = 0x40,
	QueryRx   = 0x80,
	Query     = 0xc0
    };

    /**
     * Interface generated notifications
     */
    enum Notification {
	LinkUp = 0,
	LinkDown,
	HardwareError,
	TxClockError,
	RxClockError,
	AlignError,
	CksumError,
	TxOversize,
	RxOversize,
	TxOverflow,
	RxOverflow,
	TxUnderrun,
	RxUnderrun,
    };

    /**
     * Packet types
     */
    enum PacketType {
	Unknown = 0,
	SS7Fisu,
	SS7Lssu,
	SS7Msu,
	Q921
    };

    /**
     * Constructor
     */
    inline SignallingInterface()
	: m_recvMutex(true,"SignallingInterface::recv"), m_receiver(0)
	{ }

    /**
     * Destructor, stops and detaches the interface
     */
    virtual ~SignallingInterface();

    /**
     * Attach a receiver to the interface. Detach from the old one if valid
     * @param receiver Pointer to receiver to attach
     */
    virtual void attach(SignallingReceiver* receiver);

    /**
     * Retrieve the signalling receiver attached to this interface
     * @return Pointer to attached receiver, NULL if none
     */
    inline SignallingReceiver* receiver() const
	{ return m_receiver; }

    /**
     * Execute a control operation. Operations can enable, disable or flush
     *  the transmitter, receiver or both. The status (enabled/disabled) can
     *  be queried and also interface-specific operations can be executed.
     * @param oper Operation to execute
     * @param params Optional parameters for the operation
     * @return True if the command completed successfully, for query operations
     *  also indicates the interface is enabled and operational
     */
    virtual bool control(Operation oper, NamedList* params = 0);

    /**
     * Keeps the names associated with the notifications
     */
    static const TokenDict s_notifName[];

protected:
    /**
     * Transmit a packet over the hardware interface
     * @param packet Packet data to send
     * @param repeat Continuously send a copy of the packet while no other
     *  data is available for transmission
     * @param type Type of the packet to send
     * @return True if the interface accepted the packet
     */
    virtual bool transmitPacket(const DataBlock& packet, bool repeat, PacketType type) = 0;

    /**
     * Push a valid received Signalling Packet up the protocol stack.
     * The starting and ending flags and any CRC are not part of the data.
     * @return True if packet was successfully delivered to the receiver
     */
    bool receivedPacket(const DataBlock& packet);

    /**
     * Generate a notification event to the attached receiver
     * @param event Notification event to be reported
     * @return True if notification was accepted by the receiver
     */
    bool notify(Notification event);

private:
    Mutex m_recvMutex;                   // Lock receiver pointer operations
    SignallingReceiver* m_receiver;
};

/**
 * An interface to an abstraction of a Layer 2 packet data receiver
 * @short Abstract Layer 2 packet data receiver
 */
class YSIG_API SignallingReceiver : virtual public SignallingComponent
{
    YCLASS(SignallingReceiver,SignallingComponent)
    friend class SignallingInterface;
public:
    /**
     * Constructor
     * @param name Name of the component to create
     */
    SignallingReceiver(const char* name = 0);

    /**
     * Destructor, stops the interface and detaches from it
     */
    virtual ~SignallingReceiver();

    /**
     * Attach a hardware interface to the data link. Detach from the old one if valid
     * @param iface Pointer to interface to attach
     * @return Pointer to old attached interface or NULL
     */
    virtual SignallingInterface* attach(SignallingInterface* iface);

    /**
     * Retrieve the interface used by this receiver
     * @return Pointer to the attached interface or NULL
     */
    inline SignallingInterface* iface() const
	{ return m_interface; }

    /**
     * Execute a control operation on the attached interface.
     * @param oper Operation to execute
     * @param params Optional parameters for the operation
     * @return True if the command completed successfully, for query operations
     *  also indicates the interface is enabled and operational
     */
    bool control(SignallingInterface::Operation oper, NamedList* params = 0);

protected:
    /**
     * Send a packet to the attached interface for transmission
     * @param packet Packet data to send
     * @param repeat Continuously send a copy of the packet while no other
     *  data is available for transmission
     * @param type Type of the packet to send
     * @return True if the interface accepted the packet
     */
    bool transmitPacket(const DataBlock& packet, bool repeat,
	SignallingInterface::PacketType type = SignallingInterface::Unknown);

    /**
     * Process a Signalling Packet received by the interface
     * @return True if message was successfully processed
     */
    virtual bool receivedPacket(const DataBlock& packet) = 0;

    /**
     * Process a notification generated by the attached interface
     * @param event Notification event reported by the interface
     * @return True if notification was processed
     */
    virtual bool notify(SignallingInterface::Notification event);

private:
    Mutex m_ifaceMutex;                  // Lock interface pointer operations
    SignallingInterface* m_interface;
};


/**
 * This class keeps a description of a parameter flag used to encode/decode flags
 * @short Description of parameter flags
 */
struct SignallingFlags
{
    /**
     * Mask to separate the relevant bits
     */
    unsigned int mask;

    /**
     * Actual value to match
     */
    unsigned int value;

    /**
     * Name of the flag
     */
    const char* name;
};

/**
 * Provides data and services for SS7 and ISDN
 * @short Library wide services and data provider
 */
class YSIG_API SignallingUtils
{
public:
    /**
     * Retreive the dictionary keeping the coding standard flags of ISUP and ISDN parameters as defined in Q.850
     * @return Pointer to the coding standards dictionary
     */
    static const TokenDict* codings();

    /**
     * Retreive the dictionary keeping the location flags of ISUP and ISDN parameters as defined in Q.850
     * @return Pointer to the locations dictionary
     */
    static const TokenDict* locations();

    /**
     * Retreive a dictionary given by index and coding standard for ISUP and ISDN parameters
     * @param index The desired disctionary:
     *  0: The release causes of ISUP and ISDN calls as defined in Q.850.
     *  1: The formats negotiated in ISDN and ISUP parameters as defined in Q.931/Q.763.
     *  2: The transfer capability negotiated in ISDN and ISUP parameters as defined in Q.931/Q.763.
     *  3: The transfer mode negotiated in ISDN and ISUP parameters as defined in Q.931/Q.763.
     *  4: The transfer rate negotiated in ISDN and ISUP parameters as defined in Q.931/Q.763.
     * @param coding Optional coding standard. Defaults to CCITT if 0
     * @return Pointer to the requested dictionary or 0
     */
    static inline const TokenDict* dict(unsigned int index, unsigned char coding = 0)
    {
	if (index > 4)
	    return 0;
	return (!coding ? s_dictCCITT[index] : 0);
    }

    /**
     * Check if a comma separated list of flags has a given flag
     * @param flags The list of flags
     * @param flag The flag to check
     * @return True if the given flag is found
     */
    static bool hasFlag(const String& flags, const char* flag);

    /**
     * Append a flag to a comma separated list of flags if it doesn't exist
     * @param flags The list of flags
     * @param flag The flag to add
     * @return True if the given flag was not found but added
     */
    static bool appendFlag(String& flags, const char* flag);

    /**
     * Remove a flag from a comma separated list of flags
     * @param flags The list of flags
     * @param flag The flag to remove
     * @return True if the given flag was found and removed
     */
    static bool removeFlag(String& flags, const char* flag);

    /**
     * Check if a list's parameter (comma separated list of flags) has a given flag
     * @param list The parameter list
     * @param param The parameter to check
     * @param flag The flag to check
     * @return True if the given flag is found
     */
    static bool hasFlag(const NamedList& list, const char* param, const char* flag);

    /**
     * Append a flag to a list parameter (comma separated list), craete parameter if missing
     * @param list The parameter list
     * @param param The parameter to append to
     * @param flag The flag to add
     * @return True if the given flag was not found but added
     */
    static bool appendFlag(NamedList& list, const char* param, const char* flag);

    /**
     * Add string (keyword) if found in a dictionary or integer parameter to a named list
     * @param list Destination list
     * @param param Parameter to add to the list
     * @param tokens The dictionary used to find the given value
     * @param val The value to find/add to the list
     */
    static void addKeyword(NamedList& list, const char* param,
	const TokenDict* tokens, unsigned int val);

    /**
     * Dump a buffer to a list of parameters
     * @param comp Signalling component requesting the service. Used to print debug messages
     * @param list The destination list
     * @param param Parameter to add to the list
     * @param buf The buffer containing the data to dump
     * @param len Buffer's length
     * @param sep The separator between elements
     */
    static void dumpData(const SignallingComponent* comp, NamedList& list, const char* param,
	const unsigned char* buf, unsigned int len, char sep = ' ');

    /**
     * Dump data from a buffer to a list of parameters. The buffer is parsed until (and including)
     *  the first byte with the extension bit (the most significant one) set
     * @param comp Signalling component requesting the service. Used to print debug messages
     * @param list The destination list
     * @param param Parameter to add to the list
     * @param buf The buffer containing the data to dump
     * @param len Buffer's length
     * @param sep The separator between elements
     * @return The number of bytes processed. 0 if the end of the buffer was reached without finding
     *  a byte with the extension bit set
     */
    static unsigned int dumpDataExt(const SignallingComponent* comp, NamedList& list, const char* param,
	const unsigned char* buf, unsigned int len, char sep = ' ');

    /**
     * Decode a received buffer to a comma separated list of flags and add it to a list of parameters
     * @param comp Signalling component requesting the service. Used to print debug messages
     * @param list The destination list
     * @param param The parameter to add to the list
     * @param flags The flags description to use
     * @param buf The buffer containing the data to parse
     * @param len Buffer's length
     * @return False if the flags description or the buffer is missing or the buffer's length exceeds
     *  the length of the 'unsigned int' data type
     */
    static bool decodeFlags(const SignallingComponent* comp, NamedList& list, const char* param,
	const SignallingFlags* flags, const unsigned char* buf, unsigned int len);

    /**
     * Decode cause parameters as defined in Q.850
     * @param comp Signalling component requesting the service. Used to print debug messages
     * @param list The destination list
     * @param buf The buffer containing the data to parse
     * @param len Buffer's length
     * @param prefix The prefix to add to the fields before adding to the destination list
     * @param isup True if the requestor is ISUP, false for ISDN requestor
     * @return True if successfully parsed
     */
    static bool decodeCause(const SignallingComponent* comp, NamedList& list, const unsigned char* buf,
	unsigned int len, const char* prefix, bool isup);

    /**
     * Decode bearer capabilities as defined in Q.931 (Bearer Capabilities) and Q.763 (User Service Information)
     * @param comp Signalling component requesting the service. Used to print debug messages
     * @param list The destination list
     * @param buf The buffer containing the data to parse
     * @param len Buffer's length
     * @param prefix The prefix to add to the fields before adding to the destination list
     * @param isup True if the requestor is ISUP, false for ISDN requestor
     * @return True if successfully parsed
     */
    static bool decodeCaps(const SignallingComponent* comp, NamedList& list, const unsigned char* buf,
	unsigned int len, const char* prefix, bool isup);

    /**
     * Encode a comma separated list of flags. Flags can be prefixed with the '-'
     *  character to be reset if previously set
     * @param comp Signalling component requesting the service. Used to print debug messages
     * @param dest Destination flak mask
     * @param flags The flag list
     * @param dict Dictionary used to retrieve the flag names and values
     * @return The OR'd value of found flags
     */
    static void encodeFlags(const SignallingComponent* comp, int& dest, const String& flags,
	const TokenDict* dict);

    /**
     * Encode a comma separated list of signalling flags
     * @param comp Signalling component requesting the service. Used to print debug messages
     * @param flags The flag list
     * @param dict Signalling flags used to retrieve the flag names and values
     * @param paramName Optional flags parameter name used for debug purposes
     * @return The OR'd value of found flags
     */
    static unsigned int encodeFlags(const SignallingComponent* comp, const String& flags,
	const SignallingFlags* dict, const char* paramName = 0);

    /**
     * Encode cause parameters as defined in Q.850. Create with normal clearing value if parameter is missing.
     * Don't encode diagnostic if total length exceeds 32 bytes for Q.931 requestor
     * @param comp Signalling component requesting the service. Used to print debug messages
     * @param buf The destination buffer
     * @param params The list with the parameters
     * @param prefix The prefix of the fields obtained from parameter list
     * @param isup True if the requestor is ISUP, false for ISDN requestor
     * @param fail Fail if the buffer is too long. Ignored if isup is true
     * @return False if the requestor is Q.931, fail is true and the length exceeds 32 bytes
     */
    static bool encodeCause(const SignallingComponent* comp, DataBlock& buf, const NamedList& params,
	const char* prefix, bool isup, bool fail = false);

    /**
     * Encode bearer capabilities as defined in Q.931 (Bearer Capabilities) and Q.763 (User Service Information)
     * @param comp Signalling component requesting the service. Used to print debug messages
     * @param buf The destination buffer
     * @param params The list with the parameters
     * @param prefix The prefix of the fields obtained from parameter list
     * @param isup True if the requestor is ISUP, false for ISDN requestor
     * @return True
     */
    static bool encodeCaps(const SignallingComponent* comp, DataBlock& buf, const NamedList& params,
	const char* prefix, bool isup);

    /**
     * Parse a list of unsigned integers or unsigned integer intervals. Source elements
     *  must be separated by a '.' or ',' character. Interval margins must be separated
     *  by a '-' character. Empty elements are ignored
     * @param source The string to parse
     * @param minVal The minimum value for each element in the array
     * @param maxVal The maximum value for each element in the array
     * @param count On exit will contain the length of the returned array (0 on failure)
     * @param discardDup True to discard duplicate values
     * @return Pointer to an array of unsigned integers on success (the caller must delete it after use).
     *  0 on failure (source is empty or has invalid format or an invalid value was found)
     */
    static unsigned int* parseUIntArray(const String& source, unsigned int minVal, unsigned int maxVal,
	unsigned int& count, bool discardDup);

private:
    static const TokenDict* s_dictCCITT[5];
};

/**
 * This class holds a signalling message along with timeout value(s)
 * @short A pending signalling message
 */
class YSIG_API SignallingMessageTimer : public GenObject, public SignallingTimer
{
public:
    /**
     * Constructor
     * @param interval Operation timeout interval
     * @param global Operation global timeout interval
     */
    inline SignallingMessageTimer(u_int64_t interval, u_int64_t global = 0)
	: SignallingTimer(interval),
	  m_globalTimer(global), m_msg(0)
	{ }

    /**
     * Destructor. Release data
     */
    virtual ~SignallingMessageTimer()
	{ TelEngine::destruct(m_msg); }

    /**
     * Retrieve stored signaling message
     * @return Pointer to the stored message
     */
    inline SignallingMessage* message() const
	{ return m_msg; }

    /**
     * Set a new message
     * @param msg Message to store in the timer
     */
    inline void message(SignallingMessage* msg)
	{ m_msg = msg; }


    /**
     * Get access to the global timer
     * @return A reference to the global timer
     */
    inline SignallingTimer& global()
	{ return m_globalTimer; }

    /**
     * Get const access to the global timer
     * @return A const reference to the global timer
     */
    inline const SignallingTimer& global() const
	{ return m_globalTimer; }

    /**
     * Get the time this message timer will timeout
     * @return The time this message timer will timeout
     */
    inline u_int64_t fireTime() const {
	if (!m_globalTimer.started() || m_globalTimer.fireTime() > SignallingTimer::fireTime())
	    return SignallingTimer::fireTime();
	return m_globalTimer.fireTime();
    }

protected:
    SignallingTimer m_globalTimer;
    SignallingMessage* m_msg;
};

/**
 * This class holds pending signalling messages.
 * The list will keep objects in timeout ascending order
 * @short A pending signalling message list
 */
class YSIG_API SignallingMessageTimerList : public ObjList
{
public:
    /**
     * Constructor
     */
    inline SignallingMessageTimerList()
	{ }

    /**
     * Add a pending operation to the list. Start its timer
     * @param interval Operation timeout interval
     * @param when Current time
     * @return Added operation or 0 on failure
     */
    inline SignallingMessageTimer* add(u_int64_t interval, const Time& when = Time())
	{ return interval ? add(new SignallingMessageTimer(interval),when) : 0; }

    /**
     * Add a pending operation to the list. Start its timer
     * @param m The Message Timer to add to the pending list
     * @param when Current time
     * @return Added message
     */
    SignallingMessageTimer* add(SignallingMessageTimer* m, const Time& when = Time());

    /**
     * Check if the first operation timed out. Remove it from list before returning it
     * @param when Current time
     * @return SignallingMessageTimer pointer or 0 if no timeout occured
     */
    SignallingMessageTimer* timeout(const Time& when = Time());
};

/**
 * This class is used to manage an analog line and keep data associated with it.
 * Also it can be used to monitor a pair of FXS/FXO analog lines
 * @short An analog line
 */
class YSIG_API AnalogLine : public RefObject, public Mutex
{
    YCLASS(AnalogLine,RefObject)
    friend class AnalogLineGroup;        // Reset group if destroyed before the line
public:
    /**
     * Line type enumerator
     */
    enum Type {
	FXO,                             // Telephone linked to an exchange
	FXS,                             // Telephone exchange linked to a telephone
	Recorder,                        // Passive FXO recording a 2 wire line
	Monitor,                         // Monitor (a pair of FXS/FXO lines)
	Unknown
    };

    /**
     * Line state enumeration
     */
    enum State {
	OutOfService   = -1,             // Line is out of service
	Idle           = 0,              // Line is idle (on hook)
	Dialing        = 1,              // FXS line is waiting for the FXO to dial the number
	DialComplete   = 2,              // FXS line: got enough digits from the FXO to reach a destination
	Ringing        = 3,              // Line is ringing
	Answered       = 4,              // Line is answered
	CallEnded      = 5,              // FXS line: notify the FXO on call termination
	OutOfOrder     = 6,              // FXS line: notify the FXO that the hook is off after call ended notification
    };

    /**
     * Call setup (such as Caller ID) management (send and detect)
     */
    enum CallSetupInfo {
	After,                           // Send/detect call setup after the first ring
	Before,                          // Send/detect call setup before the first ring
	NoCallSetup                      // No call setup detect or send
    };

    /**
     * Constructor. Reserve the line's circuit. Connect it if requested. Creation will fail if no group,
     *  circuit, caller or the circuit is already allocated for another line in the group
     * @param grp The group owning this analog line
     * @param cic The code of the signalling circuit used this line
     * @param params The line's parameters
     */
    AnalogLine(AnalogLineGroup* grp, unsigned int cic, const NamedList& params);

    /**
     * Destructor
     */
    virtual ~AnalogLine();

    /**
     * Get this line's type
     * @return The line type as enumeration
     */
    inline Type type() const
	{ return m_type; }

    /**
     * Get the line state
     * @return The line state as enumeration
     */
    inline State state() const
	{ return m_state; }

    /**
     * Get the group owning this line
     * @return The group owning this line
     */
    inline AnalogLineGroup* group()
	{ return m_group; }

    /**
     * Get this line's peer if belongs to a pair of monitored lines
     * @return This line's peer if belongs to a pair of monitored lines
     */
    inline AnalogLine* getPeer()
	{ return m_peer; }

    /**
     * Remove old peer's peer. Set this line's peer
     * @param line This line's peer
     * @param sync True to synchronize (set/reset) with the old peer
     */
    void setPeer(AnalogLine* line = 0, bool sync = true);

    /**
     * Get the line's circuit
     * @return SignallingCircuit pointer or 0 if no circuit was attached to this line
     */
    inline SignallingCircuit* circuit()
	{ return m_circuit; }

    /**
     * Get the line address: group_name/circuit_number
     * @return The line address
     */
    inline const char* address() const
	{ return m_address; }

    /**
     * Check if allowed to send outband DTMFs (DTMF events)
     * @return True if allowed to send outband DTMFs
     */
    inline bool outbandDtmf() const
	{ return !m_inband; }

    /**
     * Check if the line should be answered on polarity change
     * @return True if the line should be answered on polarity change
     */
    inline bool answerOnPolarity() const
	{ return m_answerOnPolarity; }

    /**
     * Check if the line should be hanged up on polarity change
     * @return True if the line should be hanged up on polarity change
     */
    inline bool hangupOnPolarity() const
	{ return m_hangupOnPolarity; }

    /**
     * Check if the line polarity change should be used
     * @return True if the line polarity change should be used
     */
    inline bool polarityControl() const
	{ return m_polarityControl; }

    /**
     * Check if the line is processing (send/receive) the setup info (such as caller id) and when it does it
     * @return Call setup info processing as enumeration
     */
    inline CallSetupInfo callSetup() const
	{ return m_callSetup; }

    /**
     * Get the time allowed to ellapse between the call setup data and the first ring
     * @return The time allowed to ellapse between the call setup data and the first ring
     */
    inline u_int64_t callSetupTimeout() const
	{ return m_callSetupTimeout; }

    /**
     * Get the time allowed to ellapse without receiving a ring on incoming calls
     * @return The time allowed to ellapse without receiving a ring on incoming calls
     */
    inline u_int64_t noRingTimeout() const
	{ return m_noRingTimeout; }

    /**
     * Get the time allowed to stay in alarm. This option can be used by the clients to terminate an active call
     * @return The time allowed to stay in alarm
     */
    inline u_int64_t alarmTimeout() const
	{ return m_alarmTimeout; }

    /**
     * Get the time delay of dialing the called number
     * @return The time delay of dialing the called number
     */
    inline u_int64_t delayDial() const
	{ return m_delayDial; }

    /**
     * Set/reset accept pulse digits flag
     * @param ok True to accept incoming pulse digits, false to ignore them
     */
    inline void acceptPulseDigit(bool ok)
	{ m_acceptPulseDigit = ok; }

    /**
     * Get the private user data of this line
     * @return The private user data of this line
     */
    inline void* userdata() const
	{ return m_private; }

    /**
     * Set the private user data of this line and its peer if any
     * @param data The new private user data value of this line
     * @param sync True to synchronize (set data) with the peer
     */
    inline void userdata(void* data, bool sync = true)
    {
	Lock lock(this);
	m_private = data;
	if (sync && m_peer)
	    m_peer->userdata(data,false);
    }

    /**
     * Get this line's address
     * @return This line's address
     */
    virtual const String& toString() const
	{ return m_address; }

    /**
     * Reset the line circuit's echo canceller to line default echo canceller state
     * @param train Start echo canceller training if enabled
     */
    void resetEcho(bool train);

    /**
     * Reset the line's circuit (change its state to Reserved)
     * @return True if the line's circuit state was changed to Reserved
     */
    inline bool resetCircuit()
	{ return state() != OutOfService && m_circuit && m_circuit->reserve(); }

    /**
     * Set a parameter of this line's circuit
     * @param param Parameter name
     * @param value Optional parameter value
     * @return True if the line's circuit parameter was set
     */
    inline bool setCircuitParam(const char* param, const char* value = 0)
	{ return m_circuit && m_circuit->setParam(param,value); }

    /**
     * Connect the line's circuit. Reset line echo canceller
     * @param sync True to synchronize (connect) the peer
     * @return True if the line's circuit state was changed to Connected
     */
    bool connect(bool sync);

    /**
     * Disconnect the line's circuit. Reset line echo canceller
     * @param sync True to synchronize (disconnect) the peer
     * @return True if the line's circuit was disconnected (changed state from Connected to Reserved)
     */
    bool disconnect(bool sync);

    /**
     * Send an event through this line if not out of service
     * @param type The type of the event to send
     * @param params Optional event parameters
     * @return True on success
     */
    bool sendEvent(SignallingCircuitEvent::Type type, NamedList* params = 0);

    /**
     * Send an event through this line if not out of service and change its state on success
     * @param type The type of the event to send
     * @param newState The new state of the line if the event was sent
     * @param params Optional event parameters
     * @return True on success
     */
    inline bool sendEvent(SignallingCircuitEvent::Type type, State newState,
	NamedList* params = 0)
    {
	if (!sendEvent(type,params))
	    return false;
	changeState(newState,false);
	return true;
    }

    /**
     * Get events from the line's circuit if not out of service. Check timeouts
     * @param when The current time
     * @return AnalogLineEvent pointer or 0 if no events
     */
    virtual AnalogLineEvent* getEvent(const Time& when);

    /**
     * Alternate get events from this line or peer
     * @param when The current time
     * @return AnalogLineEvent pointer or 0 if no events
     */
    virtual AnalogLineEvent* getMonitorEvent(const Time& when);

    /**
     * Check timeouts if the line is not out of service and no event was generated by the circuit
     * @param when Time to use as computing base for timeouts
     */
    virtual void checkTimeouts(const Time& when)
	{ }

    /**
     * Change the line state if neither current or new state are OutOfService
     * @param newState The new state of the line
     * @param sync True to synchronize (change state) the peer
     * @return True if line state changed
     */
    bool changeState(State newState, bool sync = false);

    /**
     * Enable/disable line. Change circuit's state to Disabled/Reserved when
     *  entering/exiting the OutOfService state
     * @param ok Enable (change state to Idle) or disable (change state to OutOfService) the line
     * @param sync True to synchronize (enable/disable) the peer
     * @param connectNow Connect the line if enabled. Ignored if the line will be disabled
     * @return True if line state changed
     */
    bool enable(bool ok, bool sync, bool connectNow = true);

    /**
     * Line type names dictionary
     */
    static const TokenDict* typeNames();

    /**
     * Line state names dictionary
     */
    static const TokenDict* stateNames();

    /**
     * Call setup info names
     */
    static const TokenDict* csNames();

protected:
    /**
     * Deref the circuit. Remove itself from group
     */
    virtual void destroyed();

private:
    Type m_type;                               // Line type
    State m_state;                             // Line state
    bool m_inband;                             // Refuse to send DTMFs if they should be sent in band
    int m_echocancel;                          // Default echo canceller state (0: managed by the circuit, -1: off, 1: on)
    bool m_acceptPulseDigit;                   // Accept incoming pulse digits
    bool m_answerOnPolarity;                   // Answer on line polarity change
    bool m_hangupOnPolarity;                   // Hangup on line polarity change
    bool m_polarityControl;                    // Set line polarity flag
    CallSetupInfo m_callSetup;                 // Call setup management
    u_int64_t m_callSetupTimeout;              // FXO: timeout period for received call setup data before first ring
    u_int64_t m_noRingTimeout;                 // FXO: timeout period with no ring received on incoming calls
    u_int64_t m_alarmTimeout;                  // Timeout period to stay in alarms
    u_int64_t m_delayDial;                     // FXO: Time to delay sending number
    AnalogLineGroup* m_group;                  // The group owning this line
    SignallingCircuit* m_circuit;              // The circuit managed by this line
    String m_address;                          // Line address: group and circuit
    void* m_private;                           // Private data used by this line's user
    // Monitor data
    AnalogLine* m_peer;                        // This line's peer if any
    bool m_getPeerEvent;                       // Flag used to get events from peer
};

/**
 * An object holding an event generated by an analog line and related references
 * @short A single analog line related event
 */
class YSIG_API AnalogLineEvent : public GenObject
{
public:
    /**
     * Constructor
     * @param line The analog line that generated this event
     * @param event The signalling circuit event
     */
    AnalogLineEvent(AnalogLine* line, SignallingCircuitEvent* event)
	: m_line(0), m_event(event)
	{ if (line && line->ref()) m_line = line; }

    /**
     * Destructor, dereferences any resources
     */
    virtual ~AnalogLineEvent()
    {
	TelEngine::destruct(m_line);
	TelEngine::destruct(m_event);
    }

    /**
     * Get the analog line that generated this event
     * @return The analog line that generated this event
     */
    inline AnalogLine* line()
	{ return m_line; }

    /**
     * Get the signalling circuit event carried by this analog line event
     * @return The signalling circuit event carried by this analog line event
     */
    inline SignallingCircuitEvent* event()
	{ return m_event; }

    /**
     * Disposes the memory
     */
    virtual void destruct()
    {
	TelEngine::destruct(m_line);
	TelEngine::destruct(m_event);
	GenObject::destruct();
    }

private:
    AnalogLine* m_line;
    SignallingCircuitEvent* m_event;
};

/**
 * This class is an analog line container.
 * It may contain another group when used to monitor analog lines
 * @short A group of analog lines
 */
class YSIG_API AnalogLineGroup : public SignallingCircuitGroup
{
    YCLASS(AnalogLineGroup,SignallingCircuitGroup)
public:
    /**
     * Constructor. Construct an analog line group owning single lines
     * @param type Line type as enumeration
     * @param name Name of this component
     * @param slave True if this is an FXO group owned by an FXS one. Ignored if type is not FXO
     */
    AnalogLineGroup(AnalogLine::Type type, const char* name, bool slave = false);

    /**
     * Constructor. Construct an FXS analog line group owning another group of FXO analog lines.
     * The fxo group is owned by this component and will be destructed if invalid (not FXO type)
     * @param name Name of this component
     * @param fxo The FXO group
     */
    AnalogLineGroup(const char* name, AnalogLineGroup* fxo);

    /**
     * Destructor
     */
    virtual ~AnalogLineGroup();

    /**
     * Get this group's type
     * @return The group's type
     */
    inline AnalogLine::Type type() const
	{ return m_type; }

    /**
     * Get the analog lines belonging to this group
     * @return The group's lines list
     */
    inline ObjList& lines()
	{ return m_lines; }

    /**
     * Get the group holding the FXO lines if present
     * @return The group holding the FXO lines or 0
     */
    inline AnalogLineGroup* fxo()
	{ return m_fxo; }

    /**
     * Check if this is an FXO group owned by an FXS one
     * @return True if this is an FXO group owned by an FXS one
     */
    inline bool slave()
	{ return m_slave; }

    /**
     * Append a line to this group. Line must have the same type as this group and must be owned by this group
     * @param line The line to append
     * @param destructOnFail Destroy line if failed to append. Defaults to true
     * @return True on success
     */
    bool appendLine(AnalogLine* line, bool destructOnFail = true);

    /**
     * Remove a line from the list and destruct it
     * @param cic The signalling circuit's code used by the line
     */
    void removeLine(unsigned int cic);

    /**
     * Remove a line from the list without destroying it
     * @param line The line to be removed
     */
    void removeLine(AnalogLine* line);

    /**
     * Find a line by its circuit
     * @param cic The signalling circuit's code used by the line
     * @return AnalogLine pointer or 0 if not found
     */
    AnalogLine* findLine(unsigned int cic);

    /**
     * Find a line by its address
     * @param address The address of the line
     * @return AnalogLine pointer or 0 if not found
     */
    AnalogLine* findLine(const String& address);

    /**
     * Iterate through the line list to get an event
     * @param when The current time
     * @return AnalogLineEvent pointer or 0 if no events
     */
    virtual AnalogLineEvent* getEvent(const Time& when);

protected:
    /**
     * Remove all lines. Release object
     */
    virtual void destroyed();

    /**
     * The analog lines belonging to this group
     */
    ObjList m_lines;

private:
    AnalogLine::Type m_type;             // Line type
    AnalogLineGroup* m_fxo;              // The group containing the FXO lines if this is a monitor
    bool m_slave;                        // True if this is an FXO group owned by an FXS one
};

/**
 * An universal SS7 Layer 3 routing Code Point
 * @short SS7 Code Point
 */
class YSIG_API SS7PointCode : public GenObject
{
    YCLASS(SS7PointCode,GenObject)
public:
    /**
     * Different incompatible types of points codes
     */
    enum Type {
	Other  = 0,
	ITU    = 1, // ITU-T Q.704
	ANSI   = 2, // ANSI T1.111.4
	ANSI8  = 3, // 8-bit SLS
	China  = 4, // GF 001-9001
	Japan  = 5, // JT-Q704, NTT-Q704
	Japan5 = 6, // 5-bit SLS
	// Do not change the next line, must be past last defined type
	DefinedTypes
    };

    /**
     * Constructor from components
     * @param network ANSI Network Identifier / ITU-T Zone Identification
     * @param cluster ANSI Network Cluster / ITU-T Area/Network Identification
     * @param member ANSI Cluster Member / ITU-T Signalling Point Identification
     */
    inline SS7PointCode(unsigned char network = 0, unsigned char cluster = 0, unsigned char member = 0)
	: m_network(network), m_cluster(cluster), m_member(member)
	{ }

    /**
     * Constructor from unpacked format
     * @param type Type of the unpacking desired
     * @param packed Packed format of the point code
     */
    inline SS7PointCode(Type type, unsigned int packed)
	: m_network(0), m_cluster(0), m_member(0)
	{ unpack(type,packed); }

    /**
     * Copy constructor
     * @param original Code point to be copied
     */
    inline SS7PointCode(const SS7PointCode& original)
	: m_network(original.network()), m_cluster(original.cluster()), m_member(original.member())
	{ }

    /**
     * Destructor
     */
    inline ~SS7PointCode()
	{ }

    /**
     * Retrieve the Network / Zone component of the Code Point
     * @return ANSI Network Identifier / ITU-T Zone Identification
     */
    inline unsigned char network() const
	{ return m_network; }

    /**
     * Retrieve the Cluster / Area component of the Code Point
     * @return ANSI Network Cluster / ITU-T Area/Network Identification
     */
    inline unsigned char cluster() const
	{ return m_cluster; }

    /**
     * Retrieve the Cluster / Point component of the Code Point
     * @return ANSI Cluster Member / ITU-T Signalling Point Identification
     */
    inline unsigned char member() const
	{ return m_member; }

    /**
     * Assignment from components
     * @param network ANSI Network Identifier / ITU-T Zone Identification
     * @param cluster ANSI Network Cluster / ITU-T Area/Network Identification
     * @param member ANSI Cluster Member / ITU-T Signalling Point Identification
     */
    inline void assign(unsigned char network, unsigned char cluster, unsigned char member)
	{ m_network = network; m_cluster = cluster; m_member = member; }

    /**
     * Assign data members from a given string of form 'network-cluster-member'
     * @param src Source string
     * @param type Type of the point code if numeric (packed) representation is used
     * @return False if the string has incorrect format or individual elements are not in the range 0..255
     */
    bool assign(const String& src, Type type = Other);

    /**
     * Assign data members from a packed memory block
     * @param type Type of the point code in memory
     * @param src Pointer to packed point code in memory
     * @param len Length of data, negative to not check validity
     * @param spare Pointer to variable to save spare bits, NULL to ignore them
     * @return True if success, false if invalid type or memory area
     */
    bool assign(Type type, const unsigned char* src, int len = -1, unsigned char* spare = 0);

    /**
     * Assignment operator
     * @param original Code point to be copied
     */
    inline SS7PointCode& operator=(const SS7PointCode& original)
	{ assign(original.network(),original.cluster(),original.member()); return *this; }

    /**
     * Equality operator
     * @param original Code point to be compared with
     */
    inline bool operator==(const SS7PointCode& original) const
	{ return m_network == original.network() && m_cluster == original.cluster() && m_member == original.member(); }

    /**
     * Inequality operator
     * @param original Code point to be compared with
     */
    inline bool operator!=(const SS7PointCode& original) const
	{ return m_network != original.network() || m_cluster != original.cluster() || m_member != original.member(); }

    /**
     * Check if the point code is compatible with a packing type
     * @return True if the Network and Member fit in the packing format
     */
    bool compatible(Type type) const;

    /**
     * Pack the code point into a single integer number.
     * @param type Type of the packing desired
     * @return Compact code point as integer or zero if the packing type is not supported
     */
    unsigned int pack(Type type) const;

    /**
     * Unpack an integer number into a point code
     * @param type Type of the unpacking desired
     * @param packed Packed format of the point code
     * @return True if the unpacking succeeded and the point code was updated
     */
    bool unpack(Type type, unsigned int packed);

    /**
     * Store the point code in a memory area
     * @param type Type of the packing desired
     * @param dest Location to store the label info, must be at least length() long
     * @param spare Spare bits to store after the point code if applicable (ITU)
     * @return True if the unpacking succeeded and the memory was updated
     */
    bool store(Type type, unsigned char* dest, unsigned char spare = 0) const;

    /**
     * Get the size (in bits) of a packed code point according to its type
     * @param type Type of the packing
     * @return Number of bits required to represent the code point, zero if unknown
     */
    static unsigned char size(Type type);

    /**
     * Get the length (in octets) of a packed code point according to its type
     * @param type Type of the packing
     * @return Number of octets required to represent the code point, zero if unknown
     */
    static unsigned char length(Type type);

    /**
     * Get a point type associated to a given text
     * @param text Text to find
     * @return Point code type as enumeration
     */
    static Type lookup(const char* text)
	{ return (Type)TelEngine::lookup(text,s_names,(int)Other); }

    /**
     * Get the text associated to a point type
     * @param type Type to find
     * @return The requested text or 0 if not found
     */
    static const char* lookup(Type type)
	{ return TelEngine::lookup((int)type,s_names); }

private:
    static const TokenDict s_names[];    // Keep the strigns associated with point code type
    unsigned char m_network;
    unsigned char m_cluster;
    unsigned char m_member;
};

// The number of valid point code types
#define YSS7_PCTYPE_COUNT (SS7PointCode::DefinedTypes-1)

/**
 * Operator to write a point code to a string
 * @param str String to append to
 * @param cp Point code to append to the string
 */
YSIG_API String& operator<<(String& str, const SS7PointCode& cp);

/**
 * A SS7 Layer 3 routing label, both ANSI and ITU capable
 * @short SS7 Routing Label
 */
class YSIG_API SS7Label
{
public:
    /**
     * Constructor of an empty, invalid label
     */
    SS7Label();

    /**
     * Copy constructor
     * @param original Label to copy
     */
    SS7Label(const SS7Label& original);

    /**
     * Swapping constructor, puts SPC into DPC and the other way around
     * @param original Label to swap
     * @param sls Signalling Link Selection
     * @param spare Spare bits
     */
    SS7Label(const SS7Label& original, unsigned char sls, unsigned char spare = 0);

    /**
     * Constructor from label components
     * @param type Type of point code used to pack the label
     * @param dpc Destination Point Code
     * @param opc Originating Point Code
     * @param sls Signalling Link Selection
     * @param spare Spare bits
     */
    SS7Label(SS7PointCode::Type type, const SS7PointCode& dpc,
	const SS7PointCode& opc, unsigned char sls, unsigned char spare = 0);

    /**
     * Constructor from packed label components
     * @param type Type of point code used to pack the label
     * @param dpc Destination Point Code
     * @param opc Originating Point Code
     * @param sls Signalling Link Selection
     * @param spare Spare bits
     */
    SS7Label(SS7PointCode::Type type, unsigned int dpc,
	unsigned int opc, unsigned char sls, unsigned char spare = 0);

    /**
     * Constructor from type and received MSU
     * @param type Type of point code to use to decode the MSU
     * @param msu A received MSU to be parsed
     */
    SS7Label(SS7PointCode::Type type, const SS7MSU& msu);

    /**
     * Assignment from label components
     * @param type Type of point code used to pack the label
     * @param dpc Destination Point Code
     * @param opc Originating Point Code
     * @param sls Signalling Link Selection
     * @param spare Spare bits
     */
    void assign(SS7PointCode::Type type, const SS7PointCode& dpc,
	const SS7PointCode& opc, unsigned char sls, unsigned char spare = 0);

    /**
     * Assignment from packed label components
     * @param type Type of point code used to pack the label
     * @param dpc Destination Point Code
     * @param opc Originating Point Code
     * @param sls Signalling Link Selection
     * @param spare Spare bits
     */
    void assign(SS7PointCode::Type type, unsigned int dpc,
	unsigned int opc, unsigned char sls, unsigned char spare = 0);

    /**
     * Assignment from type and received MSU
     * @param type Type of point code to use to decode the MSU
     * @param msu A received MSU to be parsed
     * @return True if the assignment succeeded
     */
    bool assign(SS7PointCode::Type type, const SS7MSU& msu);

    /**
     * Assignment from a packed memory block
     * @param type Type of the point codes in memory block
     * @param src Pointer to packed label in memory
     * @param len Length of data, negative to not check validity
     * @return True if success, false if invalid type or memory area
     */
    bool assign(SS7PointCode::Type type, const unsigned char* src, int len = -1);

    /**
     * Pack and store the label in a memory location
     * @param dest Location to store the label info, must be at least length() long
     * @return True on success, false if type is invalid
     */
    bool store(unsigned char* dest) const;

    /**
     * Check if the label is compatible with another packing type
     * @return True if the DLC, SLC and SLS fit in the new packing format
     */
    bool compatible(SS7PointCode::Type type) const;

    /**
     * Get the type (SS7 dialect) of the routing label
     * @return Dialect of the routing label as enumeration
     */
    inline SS7PointCode::Type type() const
	{ return m_type; }

    /**
     * Get the Destination Code Point inside the label
     * @return Reference of the destination code point
     */
    inline const SS7PointCode& dpc() const
	{ return m_dpc; }

    /**
     * Get a writable reference to the Destination Code Point inside the label
     * @return Reference of the destination code point
     */
    inline SS7PointCode& dpc()
	{ return m_dpc; }

    /**
     * Get the Originating Code Point inside the label
     * @return Reference of the source code point
     */
    inline const SS7PointCode& opc() const
	{ return m_opc; }

    /**
     * Get a writable reference to the Originating Code Point inside the label
     * @return Reference of the originating code point
     */
    inline SS7PointCode& opc()
	{ return m_opc; }

    /**
     * Get the Signalling Link Selection inside the label
     * @return Value of the SLS field
     */
    inline unsigned char sls() const
	{ return m_sls; }

    /**
     * Set the Signalling Link Selection inside the label
     * @param sls New value of the SLS/SLC field
     */
    inline void setSls(unsigned char sls)
	{ m_sls = sls; }

    /**
     * Get the spare bits inside the label
     * @return Value of the bits not included in DPC, OPC, or SLS
     */
    inline unsigned char spare() const
	{ return m_spare; }

    /**
     * Set the spare bits inside the label
     * @param spare New value of the spare bits
     */
    inline void setSpare(unsigned char spare)
	{ m_spare = spare; }

    /**
     * Get the length (in bytes) of this routing label
     * @return Number of bytes required to represent the label, zero if unknown
     */
    inline unsigned int length() const
	{ return length(m_type); }

    /**
     * Get the length (in bytes) of a packed routing label according to its type
     * @param type Type of the packing
     * @return Number of bytes required to represent the label, zero if unknown
     */
    static unsigned int length(SS7PointCode::Type type);

    /**
     * Get the size (in bits) of this routing label except the spare bits
     * @return Number of bits required to represent this label, zero if unknown
     */
    inline unsigned char size() const
	{ return size(m_type); }

    /**
     * Get the size (in bits) of a packed routing label according to its type
     * @param type Type of the packing
     * @return Number of bits required to represent the label, zero if unknown
     */
    static unsigned char size(SS7PointCode::Type type);

private:
    SS7PointCode::Type m_type;
    SS7PointCode m_dpc;
    SS7PointCode m_opc;
    unsigned char m_sls;
    unsigned char m_spare;
};

/**
 * Operator to write a routing label to a string
 * @param str String to append to
 * @param label Label to append to the string
 */
YSIG_API String& operator<<(String& str, const SS7Label& label);

/**
 * A raw data block with a little more understanding about MSU format
 * @short A block of data that holds a Message Signal Unit
 */
class YSIG_API SS7MSU : public DataBlock
{
    YCLASS(SS7MSU,DataBlock)
public:
    /**
     * Service indicator values
     */
    enum Services {
	// Signalling Network Management
	SNM   =  0,
	// Maintenance
	MTN   =  1,
	// Maintenance special
	MTNS  =  2,
	// Signalling Connection Control Part
	SCCP  =  3,
	// Telephone User Part
	TUP   =  4,
	// ISDN User Part
	ISUP  =  5,
	// Data User Part - call and circuit related
	DUP_C =  6,
	// Data User Part - facility messages
	DUP_F =  7,
	// MTP Testing User Part (reserved)
	MTP_T =  8,
	// Broadband ISDN User Part
	BISUP =  9,
	// Satellite ISDN User Part
	SISUP = 10,
	// AAL type2 Signaling
	AAL2  = 12,
	// Bearer Independent Call Control
	BICC  = 13,
	// Gateway Control Protocol
	GCP   = 14,
    };

    /**
     * Priority values
     */
    enum Priority {
	Regular  = 0x00,
	Special  = 0x10,
	Circuit  = 0x20,
	Facility = 0x30
    };

    /**
     * Subservice types
     */
    enum NetIndicator {
	International      = 0x00,
	SpareInternational = 0x40,
	National           = 0x80,
	ReservedNational   = 0xc0
    };

    /**
     * Empty MSU constructor
     */
    inline SS7MSU()
	{ }

    /**
     * Copy constructor
     * @param value Original MSU
     */
    inline SS7MSU(const SS7MSU& value)
	: DataBlock(value)
	{ }

    /**
     * Constructor from data block
     * @param value Raw data block to copy
     */
    inline SS7MSU(const DataBlock& value)
	: DataBlock(value)
	{ }

    /**
     * Constructor of an initialized MSU
     * @param value Data to assign, may be NULL to fill with zeros
     * @param len Length of data, may be zero (then value is ignored)
     * @param copyData True to make a copy of the data, false to use the pointer
     */
    inline SS7MSU(void* value, unsigned int len, bool copyData = true)
	: DataBlock(value,len,copyData)
	{ }

    /**
     * Constructor from routing label and raw data
     * @param sio Service Information Octet
     * @param label Routing label
     * @param len Length of data, may be zero (then value is ignored)
     * @param value Data to assign, may be NULL to fill with zeros
     */
    SS7MSU(unsigned char sio, const SS7Label label, void* value = 0, unsigned int len = 0);

    /**
     * Constructor from routing label and raw data
     * @param sif Service Information Field
     * @param ssf Subservice Field
     * @param label Routing label
     * @param len Length of data, may be zero (then value is ignored)
     * @param value Data to assign, may be NULL to fill with zeros
     */
    SS7MSU(unsigned char sif, unsigned char ssf, const SS7Label label, void* value = 0, unsigned int len = 0);

    /**
     * Destructor
     */
    virtual ~SS7MSU();

    /**
     * Assignment operator
     * @param value Original MSU
     * @return A reference to this MSU
     */
    inline SS7MSU& operator=(const SS7MSU& value)
	{ DataBlock::operator=(value); return *this; }

    /**
     * Assignment operator from data block
     * @param value Data block to assign
     * @return A reference to this MSU
     */
    inline SS7MSU& operator=(const DataBlock& value)
	{ DataBlock::operator=(value); return *this; }

    /**
     * Check if the MSU length appears valid
     * @return True if the MSU length is valid
     */
    bool valid() const;

    /**
     * Get a pointer to raw data
     * @param offs Offset in the MSU
     * @param len Minimum length of data requested
     * @return Pointer to data or NULL if invalid offset or length
     */
    inline unsigned char* getData(unsigned int offs, unsigned int len = 1)
	{ return (offs+len <= length()) ? offs + (unsigned char*)data() : 0; }

    /**
     * Get a const pointer to raw data
     * @param offs Offset in the MSU
     * @param len Minimum length of data requested
     * @return Pointer to data or NULL if invalid offset or length
     */
    inline const unsigned char* getData(unsigned int offs, unsigned int len = 1) const
	{ return (offs+len <= length()) ? offs + (const unsigned char*)data() : 0; }

    /**
     * Get a pointer to raw user part data after a routing label
     * @param label Routing label of the MSU
     * @param len Minimum length of data requested
     * @return Pointer to data or NULL if invalid offset or length
     */
    inline unsigned char* getData(const SS7Label& label, unsigned int len = 1)
	{ return getData(label.length()+1,len); }

    /**
     * Get a const pointer to raw user part data after a routing label
     * @param label Routing label of the MSU
     * @param len Minimum length of data requested
     * @return Pointer to data or NULL if invalid offset or length
     */
    inline const unsigned char* getData(const SS7Label& label, unsigned int len = 1) const
	{ return getData(label.length()+1,len); }

    /**
     * Retrieve the Service Information Octet
     * @return Value of the SIO or -1 if the MSU is empty
     */
    inline int getSIO() const
	{ return null() ? -1 : *(const unsigned char*)data(); }

    /**
     * Retrieve the Service Information Field
     * @return Value of the SIF or -1 if the MSU is empty
     */
    inline int getSIF() const
	{ return null() ? -1 : 0x0f & *(const unsigned char*)data(); }

    /**
     * Retrieve the Subservice Field (SSF)
     * @return Value of the subservice or -1 if the MSU is empty
     */
    inline int getSSF() const
	{ return null() ? -1 : 0xf0 & *(const unsigned char*)data(); }

    /**
     * Retrieve the Priority Field
     * @return Value of the priority or -1 if the MSU is empty
     */
    inline int getPrio() const
	{ return null() ? -1 : 0x30 & *(const unsigned char*)data(); }

    /**
     * Retrieve the Network Indicator (NI)
     * @return Value of the subservice or -1 if the MSU is empty
     */
    inline int getNI() const
	{ return null() ? -1 : 0xc0 & *(const unsigned char*)data(); }

    /**
     * Retrieve the name of the Service as decoded from the SIF
     * @return Name of the service, NULL if unknown or invalid MSU
     */
    const char* getServiceName() const;

    /**
     * Retrieve the name of the Priority as decoded from the SIF
     * @return Name of the priority, NULL if unknown or invalid MSU
     */
    const char* getPriorityName() const;

    /**
     * Retrieve the name of the Network Indicator as decoded from the SIF
     * @return Name of the network indicator, NULL if unknown or invalid MSU
     */
    const char* getIndicatorName() const;

    /**
     * Get the priority associated with a given name
     * @param name Priority name to find
     * @param defVal Default value to return if not found
     * @return The priority value or the given default one if not exists
     */
    static unsigned char getPriority(const char* name, unsigned char defVal = Regular);

    /**
     * Get the network indicator associated with a given name
     * @param name Network indicator name to find
     * @param defVal Default value to return if not found
     * @return The network indicator value or the given default one if not exists
     */
    static unsigned char getNetIndicator(const char* name, unsigned char defVal = National);
};

/**
 * Simple inline class used to know if a MSU was handled and if not why
 * @short MSU handling result codes (Q.704 15.17.5 and more)
 */
class HandledMSU
{
public:
    enum Result {
	Rejected     =  0,
	Unequipped   =  1,
	Inaccessible =  2,
	// private used (non Q.704)
	Accepted     = 16,
	Failure      = 17,
	NoAddress    = 18,
	NoCircuit    = 19,
    };

    /**
     * Regular constructor
     * @param result MSU handling result
     */
    inline HandledMSU(Result result = Rejected)
	{ m_result = result; }

    /**
     * Constructor from boolean success
     * @param success True signifies Accepted, false for Failure
     */
    inline HandledMSU(bool success)
	{ m_result = success ? Accepted : Failure; }

    /**
     * Copy constructor
     * @param original Result to copy
     */
    inline HandledMSU(const HandledMSU& original)
	{ m_result = original; }

    /**
     * Assignment from Result enumeration
     * @param result MSU handling result
     */
    inline HandledMSU& operator=(Result result)
	{ m_result = result; return *this; }

    /**
     * Assignment operator
     * @param original Result to assign from
     */
    inline HandledMSU& operator=(const HandledMSU& original)
	{ m_result = original; return *this; }

    /**
     * Equality operator
     * @param result Handling result value to compare to
     */
    inline bool operator==(Result result)
	{ return m_result == result; }

    /**
     * Equality operator
     * @param result Handling result value to compare to
     */
    inline bool operator==(const HandledMSU& result)
	{ return m_result == result; }

    /**
     * Inequality operator
     * @param result Handling result value to compare to
     */
    inline bool operator!=(Result result)
	{ return m_result != result; }

    /**
     * Inequality operator
     * @param result Handling result value to compare to
     */
    inline bool operator!=(const HandledMSU& result)
	{ return m_result != result; }

    /**
     * Result retrieval operator
     * @return Handling result enumeration
     */
    inline operator Result() const
	{ return m_result; }

    /**
     * Success checking
     * @return True if MSU was handled, false for any other result
     */
    inline bool ok() const
	{ return Accepted == m_result; }

    /**
     * Retrieve Q.704 15.17.5 UPU cause code
     * @return UPU cause code, 0 (Unknown) for all private causes
     */
    inline unsigned char upu() const
	{ return (Accepted > m_result) ? m_result : Rejected; }

private:
    Result m_result;
};

/**
 * A an abstraction offering connectivity to a SIGTRAN transport
 * @short An abstract SIGTRAN transport layer
 */
class YSIG_API SIGTransport : public SignallingComponent
{
    YCLASS(SIGTransport,SignallingComponent)
    friend class SIGTRAN;
public:
    /**
     * Type of transport used
     */
    enum Transport {
	None = 0,
	Sctp,
	// All the following transports are not standard
	Tcp,
	Udp,
	Unix,
    };

    /**
     * Get the SIGTRAN component attached to this transport
     * @return Pointer to adaptation layer or NULL
     */
     inline SIGTRAN* sigtran() const
	{ return m_sigtran; }

    /**
     * Get the default SCTP/TCP/UDP port used
     * @return Default protocol port, 0 if unknown, not set or no SIGTRAN attached
     */
    u_int32_t defPort() const;

    /**
     * Check if transport layer is reliable
     * @return true if transport is reliable
     */
    virtual bool reliable() const = 0;

    /**
     * Notify the SIGTRAN layer about transport status changes
     * @param status Status to notify
     */
    void notifyLayer(SignallingInterface::Notification status);

    /**
     * Configure and initialize the component and any subcomponents it may have
     * @param config Optional configuration parameters override
     * @return True if the component was initialized properly
     */
    virtual bool initialize(const NamedList* config)
	{ return false;}

    /**
     * Check if the network transport layer is connected
     * @param streamId Identifier of the stream to check if applicable
     * @return True if the transport (and stream if applicable) is connected
     */
    virtual bool connected(int streamId) const = 0;

    /**
     * Attach an user adaptation layer
     * @param sigtran SIGTRAN component to attach, can be NULL
     */
    void attach(SIGTRAN* sigtran);

    /**
     * Send a complete message to the adaptation layer for processing
     * @param msgVersion Version of the protocol
     * @param msgClass Class of the message
     * @param msgType Type of the message, depends on the class
     * @param msg Message data, may be empty
     * @param streamId Identifier of the stream the message was received on
     * @return True if the message was handled
     */
    bool processMSG(unsigned char msgVersion, unsigned char msgClass,
	unsigned char msgType, const DataBlock& msg, int streamId) const;

    /**
     * Force the underlaying transport to reconnect
     * @param force True to force transport socket reconnection
     */
    virtual void reconnect(bool force = false)
	{ }

    /**
     * Get sctp socket parameters.
     * @param params List of parameters to obtain
     * @param result List of parameters to fill
     * @return True if operation was successful, false if an error occurred
     */
    virtual bool getSocketParams(const String& params, NamedList& result)
	{ return false; }

    /**
     * Notification that a new incomming connection has been made
     * NOTE newTransport needs to be destroyed if will not be used
     * @param newTransport The new created transport
     * @param addr The newly created transport socket address
     * @return True if the newTransport will be used.
     */
    virtual bool transportNotify(SIGTransport* newTransport, const SocketAddr& addr);

    /**
     * Check if the transport thread is still running
     * @return True if the thread is still running.
     */
    virtual bool hasThread()
	{ return false; }

    /**
     * Stop the transport thread
     */
    virtual void stopThread()
	{ }
protected:
    /**
     * Constructor
     * @param name Default empty component name
     */
    inline explicit SIGTransport(const char* name = 0)
	: SignallingComponent(name), m_sigtran(0)
	{ }

    /**
     * Notification if the attached state changed
     * @param hasUAL True if an User Adaptation Layer is now attached
     */
    virtual void attached(bool hasUAL) = 0;

    /**
     * Transmit a message to the network
     * @param msgVersion Version of the protocol
     * @param msgClass Class of the message
     * @param msgType Type of the message, depends on the class
     * @param msg Message data, may be empty
     * @param streamId Identifier of the stream to send the data over
     * @return True if the message was transmitted to network
     */
    virtual bool transmitMSG(unsigned char msgVersion, unsigned char msgClass,
	unsigned char msgType, const DataBlock& msg, int streamId = 0);

    /**
     * Transmit a prepared message to the network
     * @param header Message header, typically 8 octets
     * @param msg Message data, may be empty
     * @param streamId Identifier of the stream to send the data over
     * @return True if the message was transmitted to network
     */
    virtual bool transmitMSG(const DataBlock& header, const DataBlock& msg, int streamId = 0) = 0;

private:
    SIGTRAN* m_sigtran;
};

/**
 * An interface to a Signalling Transport user adaptation component
 * @short Abstract SIGTRAN user adaptation component
 */
class YSIG_API SIGTRAN
{
    friend class SIGTransport;
public:
    /**
     * Message classes
     */
    enum MsgClass {
	// Management (IUA/M2UA/M3UA/SUA)
	MGMT  =  0,
	// Transfer (M3UA)
	TRAN  =  1,
	// SS7 Signalling Network Management (M3UA/SUA)
	SSNM  =  2,
	// ASP State Maintenance (IUA/M2UA/M3UA/SUA)
	ASPSM =  3,
	// ASP Traffic Maintenance (IUA/M2UA/M3UA/SUA)
	ASPTM =  4,
	// Q.921/Q.931 Boundary Primitives Transport (IUA)
	QPTM  =  5,
	// MTP2 User Adaptation (M2UA)
	MAUP  =  6,
	// Connectionless Messages (SUA)
	CLMSG =  7,
	// Connection-Oriented Messages (SUA)
	COMSG =  8,
	// Routing Key Management (M3UA/SUA)
	RKM   =  9,
	// Interface Identifier Management (M2UA)
	IIM   = 10,
	// M2PA Messages (M2PA)
	M2PA  = 11,
    };

    /**
     * Management messages
     */
    enum MsgMGMT {
	MgmtERR  =  0,
	MgmtNTFY =  1,
    };

    /**
     * Signalling Network Management messages
     */
    enum MsgSSNM {
	SsnmDUNA =  1, // Destination Unavailable
	SsnmDAVA =  2, // Destination Available
	SsnmDAUD =  3, // Destination State Audit
	SsnmSCON =  4, // Signalling Congestion
	SsnmDUPU =  5, // Destination User Part Unavailable
	SsnmDRST =  6, // Destination Restricted
    };

    /**
     * ASP State Maintenance messages
     */
    enum MsgASPSM {
	AspsmUP       =  1,
	AspsmDOWN     =  2,
	AspsmBEAT     =  3,
	AspsmUP_ACK   =  4,
	AspsmDOWN_ACK =  5,
	AspsmBEAT_ACK =  6,
    };

    /**
     * ASP Traffic Maintenance messages
     */
    enum MsgASPTM {
	AsptmACTIVE       =  1,
	AsptmINACTIVE     =  2,
	AsptmACTIVE_ACK   =  3,
	AsptmINACTIVE_ACK =  4,
    };

    /**
     * Routing Key Management messages
     */
    enum MsgRKM {
	RkmREG_REQ    =  1,
	RkmREG_RSP    =  2,
	RkmDEREG_REQ  =  3,
	RkmDEREG_RSP  =  4,
    };

    /**
     * Interface Identifier Management messages
     */
    enum MsgIIM {
	IimREG_REQ    =  1,
	IimREG_RSP    =  2,
	IimDEREG_REQ  =  3,
	IimDEREG_RSP  =  4,
    };

    /**
     * Constructs an uninitialized signalling transport
     * @param payload SCTP payload code, ignored for other transports
     * @param port SCTP/TCP/UDP default port used for transport
     */
    explicit SIGTRAN(u_int32_t payload = 0, u_int16_t port = 0);

    /**
     * Destructor, terminates transport layer
     */
    virtual ~SIGTRAN();

    /**
     * Attach a transport (connectivity provider)
     * @param trans Transport to attach to this component
     */
    virtual void attach(SIGTransport* trans);

    /**
     * Get the transport of this user adaptation component
     * @return Pointer to the transport layer or NULL
     */
    inline SIGTransport* transport() const
	{ return m_trans; }

    /**
     * Get the SCTP payload of this user adaptation component
     * @return SCTP payload code
     */
    inline u_int32_t payload() const
	{ return m_payload; }

    /**
     * Get the default SCTP/TCP/UDP port used for transport
     * @return Default protocol port, 0 if unknown or not set
     */
    inline u_int16_t defPort() const
	{ return m_defPort; }

    /**
     * Check if the network transport layer is connected
     * @param streamId Identifier of the stream to check if applicable
     * @return True if the transport (and stream if applicable) is connected
     */
    bool connected(int streamId = 0) const;

    virtual void notifyLayer(SignallingInterface::Notification status)
	{ }

    /**
     * Message class names dictionary
     * @return Pointer to dictionary of message classes
     */
    static const TokenDict* classNames();

    /**
     * Message types name lookup
     * @param msgClass Class of the message to look up
     * @param msgType Type of the message, depends on the class
     * @param defValue Value to return if lookup fails
     * @return Pointer to message type name
     */
    static const char* typeName(unsigned char msgClass, unsigned char msgType,
	const char* defValue = 0);

    /**
     * Transmit a message to the network transport layer
     * @param msgVersion Version of the protocol
     * @param msgClass Class of the message
     * @param msgType Type of the message, depends on the class
     * @param msg Message data, may be empty
     * @param streamId Identifier of the stream to send the data over
     * @return True if the message was transmitted to network
     */
    bool transmitMSG(unsigned char msgVersion, unsigned char msgClass,
	unsigned char msgType, const DataBlock& msg, int streamId = 0) const;

    /**
     * Transmit a message with default version to the network transport layer
     * @param msgClass Class of the message
     * @param msgType Type of the message, depends on the class
     * @param msg Message data, may be empty
     * @param streamId Identifier of the stream to send the data over
     * @return True if the message was transmitted to network
     */
    inline bool transmitMSG(unsigned char msgClass, unsigned char msgType,
	const DataBlock& msg, int streamId = 0) const
	{ return transmitMSG(1,msgClass,msgType,msg,streamId); }

    /**
     * Restart the underlaying transport
     * @param force True to hard restart, false to force restart if transport is down
     * @return True if the transport was notified that it needs to restart
     */
    bool restart(bool force);

    /**
     * Get sctp socket parameters.
     * @param params List of parameters to obtain
     * @param result List of parameters to fill
     * @return True if operation was successful, false if an error occurred
     */
    bool getSocketParams(const String& params, NamedList& result);

    /**
     * Notification that a new incomming connection has been made
     * @param newTransport The new created transport
     * @param addr The newly created transport socket address
     * @return True if the newTransport will be used.
     */
    virtual bool transportNotify(SIGTransport* newTransport, const SocketAddr& addr)
	{ TelEngine::destruct(newTransport); return false; }

    /**
     * Check if the transport thread is running
     * @return true if the transport thread is running
     */
    bool hasTransportThread();

    /**
     * Stop the transport thread
     */
    void stopTransportThread();

protected:
    /**
     * Process a complete message
     * @param msgVersion Version of the protocol
     * @param msgClass Class of the message
     * @param msgType Type of the message, depends on the class
     * @param msg Message data, may be empty
     * @param streamId Identifier of the stream the message was received on
     * @return True if the message was handled
     */
    virtual bool processMSG(unsigned char msgVersion, unsigned char msgClass,
	unsigned char msgType, const DataBlock& msg, int streamId) = 0;

private:
    SIGTransport* m_trans;
    u_int32_t m_payload;
    u_int16_t m_defPort;
    mutable Mutex m_transMutex;
};

/**
 * An interface to a Signalling Transport User Adaptation component
 * @short Abstract SIGTRAN User Adaptation component
 */
class YSIG_API SIGAdaptation : public SignallingComponent, public SIGTRAN, public Mutex
{
    YCLASS(SIGAdaptation,SignallingComponent)
public:
    /**
     * Traffic modes
     */
    enum TrafficMode {
	TrafficUnused    = 0,
	TrafficOverride  = 1,
	TrafficLoadShare = 2,
	TrafficBroadcast = 3,
    };

    enum HeartbeatState {
	HeartbeatDisabled     = 0,
	HeartbeatEnabled      = 1,
	HeartbeatWaitResponse = 2,
    };

    enum Errors {
	InvalidVersion            = 0x01,
	InvalidIID                = 0x02,
	UnsupportedMessageClass   = 0x03,
	UnsupportedMessageType    = 0x04,
	UnsupportedTrafficMode    = 0x05,
	UnexpectedMessage         = 0x06,
	ProtocolError             = 0x07,
	UnsupportedIIDType        = 0x08,
	InvalidStreamIdentifier   = 0x09,
	UnassignedTEI             = 0x0a,
	UnrecognizedSAPI          = 0x0b,
	InvalidTEISAPI            = 0x0c,
	ManagementBlocking        = 0x0d,
	ASPIDRequired             = 0x0e,
	InvalidASPID              = 0x0f,
	ASPActiveIID              = 0x10,
	InvalidParameterValue     = 0x11,
	ParameterFieldError       = 0x12,
	UnexpectedParameter       = 0x13,
	DestinationStatusUnknown  = 0x14,
	InvalidNetworkAppearance  = 0x15,
	MissingParameter          = 0x16,
	InvalidRoutingContext     = 0x19,
	NotConfiguredAS           = 0x1a,
	SubsystemStatusUnknown    = 0x1b,
	InvalidLoadsharingLabel   = 0x1c
    };

    /**
     * Destructor
     */
    virtual ~SIGAdaptation();

    /**
     * Transport initialization
     * @param config Configuration section for the adaptation
     */
    virtual bool initialize(const NamedList* config);

    /**
     * Advance to next tag in a Type-Length-Value set of parameters
     * @param data Block of data containing TLV parameters
     * @param offset Offset of current parameter in block, initialize to negative for first tag
     * @param tag Type tag of returned parameter
     * @param length Unpadded length of returned parameter in octets
     * @return True if the current parameter was valid
     */
    static bool nextTag(const DataBlock& data, int& offset, uint16_t& tag, uint16_t& length);

    /**
     * Find a specific tag in a Type-Length-Value set of parameters
     * @param data Block of data containing TLV parameters
     * @param offset Offset of current parameter in block, gets updated
     * @param tag Type tag of searched parameter
     * @param length Unpadded length of returned parameter in octets
     * @return True if the requested parameter was found
     */
    static bool findTag(const DataBlock& data, int& offset, uint16_t tag, uint16_t& length);

    /**
     * Get the value of a 32 bit integer parameter
     * @param data Block of data containing TLV parameters
     * @param tag Type tag of searched parameter
     * @param value Variable to store the decoded parameter if found
     * @return True if the requested parameter was found and decoded
     */
    static bool getTag(const DataBlock& data, uint16_t tag, uint32_t& value);

    /**
     * Get the value of a String parameter
     * @param data Block of data containing TLV parameters
     * @param tag Type tag of searched parameter
     * @param value Variable to store the decoded parameter if found
     * @return True if the requested parameter was found and decoded
     */
    static bool getTag(const DataBlock& data, uint16_t tag, String& value);

    /**
     * Get the value of a raw binary parameter
     * @param data Block of data containing TLV parameters
     * @param tag Type tag of searched parameter
     * @param value Variable to store the decoded parameter if found
     * @return True if the requested parameter was found and decoded
     */
    static bool getTag(const DataBlock& data, uint16_t tag, DataBlock& value);

    /**
     * Add a 32 bit integer parameter
     * @param data Block of data containing TLV parameters
     * @param tag Type tag of parameter to add
     * @param value Value of parameter to add
     */
    static void addTag(DataBlock& data, uint16_t tag, uint32_t value);

    /**
     * Add a String parameter
     * @param data Block of data containing TLV parameters
     * @param tag Type tag of parameter to add
     * @param value Value of parameter to add
     */
    static void addTag(DataBlock& data, uint16_t tag, const String& value);

    /**
     * Add a raw binary parameter
     * @param data Block of data containing TLV parameters
     * @param tag Type tag of parameter to add
     * @param value Value of parameter to add
     */
    static void addTag(DataBlock& data, uint16_t tag, const DataBlock& value);

    /**
     * Method called when the transport status has been changed
     * @param status Status of the transport causing the notification
     */
    void notifyLayer(SignallingInterface::Notification status);

protected:
    /**
     * Constructs an uninitialized User Adaptation component
     * @param name Name of this component
     * @param params Optional pointer to creation parameters
     * @param payload SCTP payload code, ignored for other transports
     * @param port SCTP/TCP/UDP default port used for transport
     */
    explicit SIGAdaptation(const char* name = 0, const NamedList* params = 0,
	u_int32_t payload = 0, u_int16_t port = 0);

    /**
     * Processing of common management messages
     * @param msgClass Class of the message
     * @param msgType Type of the message, depends on the class
     * @param msg Message data, may be empty
     * @param streamId Identifier of the stream the message was received on
     * @return True if the message was handled
     */
    virtual bool processCommonMSG(unsigned char msgClass,
	unsigned char msgType, const DataBlock& msg, int streamId);

    /**
     * Abstract processing of Management messages
     * @param msgType Type of the message, depends on the class
     * @param msg Message data, may be empty
     * @param streamId Identifier of the stream the message was received on
     * @return True if the message was handled
     */
    virtual bool processMgmtMSG(unsigned char msgType, const DataBlock& msg, int streamId) = 0;

    /**
     * Abstract processing of ASP State Maintenance messages
     * @param msgType Type of the message, depends on the class
     * @param msg Message data, may be empty
     * @param streamId Identifier of the stream the message was received on
     * @return True if the message was handled
     */
    virtual bool processAspsmMSG(unsigned char msgType, const DataBlock& msg, int streamId) = 0;

    /**
     * Abstract processing of ASP Traffic Maintenance messages
     * @param msgType Type of the message, depends on the class
     * @param msg Message data, may be empty
     * @param streamId Identifier of the stream the message was received on
     * @return True if the message was handled
     */
    virtual bool processAsptmMSG(unsigned char msgType, const DataBlock& msg, int streamId) = 0;

    /**
     * Method called periodically by the engine to keep everything alive
     * @param when Time to use as computing base for events and timeouts
     */
    virtual void timerTick(const Time& when);

    /**
     * Process the heartbeat messages
     * @param msgType The message type
     * @param msg Message data
     * @param streamId Identifier of the stream the message was received on
     * @return True if the message was handled
     */
    bool processHeartbeat(unsigned char msgType, const DataBlock& msg,
	int streamId);

    /**
     * Reset heartbeat for all streams
     */
    inline void resetHeartbeat()
    {
	Lock myLock(this);
	for (int i = 0;i < 32;i++)
	    m_streamsHB[i] = HeartbeatDisabled;
    }

    /**
     * Enable heartbeat for the specifyed steam id
     * @param streamId The stream id
     */
    inline void enableHeartbeat(unsigned char streamId) {
	if (streamId > 31)
	    return;
	m_streamsHB[streamId] = HeartbeatEnabled;
    }
private:
    unsigned int m_maxRetransmit;
    SignallingTimer m_sendHeartbeat;
    SignallingTimer m_waitHeartbeatAck;
    unsigned char m_streamsHB[32];
};

/**
 * Generic client side (ASP) Signalling Transport User Adaptation component
 * @short Client side SIGTRAN User Adaptation component
 */
class YSIG_API SIGAdaptClient : public SIGAdaptation
{
    friend class SIGAdaptUser;
    YCLASS(SIGAdaptClient,SIGAdaptation)
public:
    /**
     * ASP Client states
     */
    enum AspState {
	AspDown = 0,
	AspUpRq,
	AspUp,
	AspActRq,
	AspActive
    };

    /**
     * Method called when the transport status has been changed
     * @param status Status of the transport causing the notification
     */
    virtual void notifyLayer(SignallingInterface::Notification status);

protected:
    /**
     * Constructs an uninitialized User Adaptation client component
     * @param name Name of this component
     * @param params Optional pointer to creation parameters
     * @param payload SCTP payload code, ignored for other transports
     * @param port SCTP/TCP/UDP default port used for transport
     */
    explicit SIGAdaptClient(const char* name = 0, const NamedList* params = 0,
	u_int32_t payload = 0, u_int16_t port = 0);

    /**
     * Process Management messages as ASP
     * @param msgType Type of the message, depends on the class
     * @param msg Message data, may be empty
     * @param streamId Identifier of the stream the message was received on
     * @return True if the message was handled
     */
    virtual bool processMgmtMSG(unsigned char msgType, const DataBlock& msg, int streamId);

    /**
     * Process ASP State Maintenance messages as ASP
     * @param msgType Type of the message, depends on the class
     * @param msg Message data, may be empty
     * @param streamId Identifier of the stream the message was received on
     * @return True if the message was handled
     */
    virtual bool processAspsmMSG(unsigned char msgType, const DataBlock& msg, int streamId);

    /**
     * Process ASP Traffic Maintenance messages as ASP
     * @param msgType Type of the message, depends on the class
     * @param msg Message data, may be empty
     * @param streamId Identifier of the stream the message was received on
     * @return True if the message was handled
     */
    virtual bool processAsptmMSG(unsigned char msgType, const DataBlock& msg, int streamId);

    /**
     * Traffic activity state change notification
     * @param active True if the ASP is active and traffic is allowed
     */
    virtual void activeChange(bool active);

    /**
     * Check if the ASP is Up
     * @return True if the ASPSM is in UP state
     */
    inline bool aspUp() const
	{ return m_state >= AspUp; }

    /**
     * Check if the ASP is Active
     * @return True if the ASPTM is in ACTIVE state
     */
    inline bool aspActive() const
	{ return m_state >= AspActive; }

    /**
     * Request activation of the ASP
     * @return True if ASP activation started, false on failure
     */
    bool activate();

    /**
     * Set the state of the ASP, notify user components of changes
     * @param state New state of the ASP
     * @param notify True to notify user layers, false if the changes are internal
     */
    void setState(AspState state, bool notify = true);

    /**
     * Get access to the list of Adaptation Users of this component
     * @return Reference to the list of Adaptation Users
     */
    inline ObjList& users()
	{ return m_users; }

    /**
     * ASP Identifier for ASPSM UP messages
     */
    int32_t m_aspId;

    /**
     * Traffic mode for ASPTM ACTIVE messages
     */
    TrafficMode m_traffic;

private:
    void attach(SIGAdaptUser* user);
    void detach(SIGAdaptUser* user);
    ObjList m_users;
    AspState m_state;
};

/**
 * Generic server side (SG) Signalling Transport User Adaptation component
 * @short Server side SIGTRAN User Adaptation component
 */
class YSIG_API SIGAdaptServer : public SIGAdaptation
{
    YCLASS(SIGAdaptServer,SIGAdaptation)
protected:
    /**
     * Constructs an uninitialized User Adaptation server component
     * @param name Name of this component
     * @param params Optional pointer to creation parameters
     * @param payload SCTP payload code, ignored for other transports
     * @param port SCTP/TCP/UDP default port used for transport
     */
    inline explicit SIGAdaptServer(const char* name = 0, const NamedList* params = 0,
	u_int32_t payload = 0, u_int16_t port = 0)
	: SIGAdaptation(name,params,payload,port)
	{ }

    /**
     * Process Management messages as SG
     * @param msgType Type of the message, depends on the class
     * @param msg Message data, may be empty
     * @param streamId Identifier of the stream the message was received on
     * @return True if the message was handled
     */
    virtual bool processMgmtMSG(unsigned char msgType, const DataBlock& msg, int streamId);

    /**
     * Process ASP State Maintenance messages as SG
     * @param msgType Type of the message, depends on the class
     * @param msg Message data, may be empty
     * @param streamId Identifier of the stream the message was received on
     * @return True if the message was handled
     */
    virtual bool processAspsmMSG(unsigned char msgType, const DataBlock& msg, int streamId);

    /**
     * Process ASP Traffic Maintenance messages as SG
     * @param msgType Type of the message, depends on the class
     * @param msg Message data, may be empty
     * @param streamId Identifier of the stream the message was received on
     * @return True if the message was handled
     */
    virtual bool processAsptmMSG(unsigned char msgType, const DataBlock& msg, int streamId);
};

/**
 * An interface to a Signalling Transport Adaptation user
 * @short Abstract SIGTRAN Adaptation user
 */
class YSIG_API SIGAdaptUser
{
    friend class SIGAdaptClient;
public:
    /**
     * Destructor
     */
    virtual ~SIGAdaptUser();

protected:
    /**
     * Default constructor
     */
    inline SIGAdaptUser()
	: m_autostart(false), m_streamId(1), m_adaptation(0)
	{ }

    /**
     * Get the User Adaptation to which this component belongs
     * @return Pointer to the User Adaptation layer
     */
    inline SIGAdaptClient* adaptation() const
	{ return m_adaptation; }

    /**
     * Get the transport of the user adaptation component
     * @return Pointer to the transport layer or NULL
     */
    inline SIGTransport* transport() const
	{ return m_adaptation ? m_adaptation->transport() : 0; }

    /**
     * Set the User Adaptation to which this component belongs
     * @param adapt Pointer to the new User Adaptation layer
     */
    void adaptation(SIGAdaptClient* adapt);

    /**
     * Traffic activity state change notification
     * @param active True if the ASP is active and traffic is allowed
     */
    virtual void activeChange(bool active) = 0;

    /**
     * Request activation of the ASP
     * @return True if ASP activation started, false on failure
     */
    inline bool activate()
	{ return m_adaptation && m_adaptation->activate(); }

    /**
     * Check if the ASP is Up
     * @return True if the ASPSM is in UP state
     */
    inline bool aspUp() const
	{ return m_adaptation && m_adaptation->aspUp(); }

    /**
     * Check if the ASP is Active
     * @return True if the ASPTM is in ACTIVE state
     */
    inline bool aspActive() const
	{ return m_adaptation && m_adaptation->aspActive(); }

    /**
     * Obtain the stream id to use for data messages
     * @return The stream id
     */
    inline unsigned char getStreamId()
	{ return m_streamId; }

    /**
     * Automatically start on init flag
     */
    bool m_autostart;

    /**
     * The SCTP streamId
     */
    unsigned char m_streamId;

private:
    SIGAdaptClient* m_adaptation;
};

/**
 * An interface to a SS7 Application Signalling Part user
 * @short Abstract SS7 ASP user interface
 */
class YSIG_API ASPUser
{
};

/**
 * An interface to a SS7 SCCP Global Title Translation
 * @short Abstract SS7 SCCP GTT interface
 */
class YSIG_API GTT : virtual public SignallingComponent
{
public:
    /**
     * Constructor
     */
    GTT(const NamedList& config);

    /**
     * Destructor
     */
    virtual ~GTT();

    /**
     * Route a SCCP message based on Global Title
     * @param gt The original global title used for message routing
     * @param prefix Optional prefix for gt params
     * @param nextPrefix Optional prefix for gt params
     * @return A new SCCP called party address or null if no route was found.
     */
    virtual NamedList* routeGT(const NamedList& gt, const String& prefix, const String& nextPrefix);

    /**
     * Initialize this GTT
     */
    virtual bool initialize(const NamedList* config);

    /**
     * Attach a SCCP to us
     * @param sccp Pointer to the SCCP to use
     */
    virtual void attach(SCCP* sccp);

    /**
     * Request to update Translations tables.
     * Called when a remote pointcode or ssn has become reachable / unreachable
     * @param params List of parameters that fired this request
     */
    virtual void updateTables(const NamedList& params)
	{ }

    /**
     * Retrieve the SCCP attached to this translator
     * @return Pointer to the attached SCCP
     */
    inline SCCP* sccp() const
	{ return m_sccp; }

protected:
    virtual void destroyed();

private:
    SCCP* m_sccp;

};

/**
 * An interface to a SS7 Signalling Connection Control Part
 * @short Abstract SS7 SCCP interface
 */
class YSIG_API SCCP : virtual public SignallingComponent
{
    YCLASS(SCCP,SignallingComponent)
    friend class SCCPManagement;
public:

    enum Type {                     // used in management status method                                 Flow
         CoordinateRequest         = 0, // Request that a subsystem to go oos (out of service)              (User->SCCP)
         CoordinateConfirm         = 1, // Confirmation that a subsystem can go oos                         (SCCP->User)
         CoordinateIndication      = 2, // Indication that a subsystem requires to go oos                   (SCCP->User)
         CoordinateResponse        = 3, // Response to a subsystem requires to go oos                       (User->SCCP)
         StatusIndication          = 4, // Indication form SCCP that a subsystem status has been changed    (SCCP->User)
         StatusRequest             = 5, // Indication from a user that a subsystem status has been changed  (User->SCCP)
         PointCodeStatusIndication = 6, // Indication to User that a point code is available / unavailable  (SCCP->User)
         TraficIndication          = 7, // ANSI only! Indication that a traffic mix has been detected       (SCCP->User).
         SubsystemStatus           = 8, // Request from sccp t users to find the status of a local subsystem
     };

    /**
     * Constructor
     */
    SCCP();

    /**
     * Destructor
     */
    virtual ~SCCP();

    /**
     * Send a message
     * @param data Data to be transported trough SCCP protocol
     * @param params SCCP parameters
     * SCCP parameters :
     * MessageReturn : boolean / integer True or 0x08 to return message on error. NOTE int values should me below 0x0f
     * sequenceControl : boolean. True to send messages in sequence
     * LocalPC : integer. Local pointcode
     * RemotePC : integer. Remote pointcode
     * Address Parameter:
     * 	Address parameter starts with : CallingPartyAddress or CalledPartyAddress followed by:
     *	    .ssn             : integer (0-255) Subsequence number
     *	    .pointcode       : integer Packed pointcode
     *	    .gt              : string The digits of the global title
     *	    .gt.plan         : integer GT numbering plan
     *	    .gt.encoding     : integer GT encoding scheme
     *	    .gt.translation  : integer GT Translation type
     *	    .gt.nature       : integer Gt nature of address indicator (ITU only)
     * Importance : integer (0-7) Importance of the message! (ITU only)
     */
    virtual int sendMessage(DataBlock& data, const NamedList& params);

    /**
      * Receive management information from attached users.
      * @param type The type of management message
      * @param params List of parameters (Affected subsystem [M])
      * @return True if the notification was processed
      */
     virtual bool managementStatus(Type type, NamedList& params);

    /**
     * Attach an user to this SS7 SCCP
     * @param user Pointer to the SCCP user
     */
    virtual void attach(SCCPUser* user);

    /**
     * Detach an user from this SS7 SCCP
     * @param user Pointer to the SCCP user
     */
    virtual void detach(SCCPUser* user);

    /**
     * Attach an Global Title Translator to this SS7 SCCP
     * @param gtt Pointer to the Global Title Translator
     */
    virtual void attachGTT(GTT* gtt);

    /**
     * Obtain the dictionary for notifications types
     * @return Pointer to the notification types dictionary
     */
    static const TokenDict* notifTypes();

    virtual void updateTables(const NamedList& params)
	{
	    Lock lock(m_translatorLocker);
	    if (m_translator)
		m_translator->updateTables(params);
	}
protected:
    /**
     * Translate a Global Title
     * @param params The Global Title content
     * @param prefix The prefix of the global title content parameters
     * @param nextPrefix Other prefix of the global title content parameters
     * @return a new SCCP route or 0 is no route was found
     */
    NamedList* translateGT(const NamedList& params, const String& prefix,
	    const String& nextPrefix);

    /**
     * Send a SCCP message to users list for processing
     * @param data The message data
     * @param params The list of parameters
     * @param ssn The ssn of the SCCP user
     * @return HandledMSU enum value
     */
    HandledMSU pushMessage(DataBlock& data, NamedList& params, int ssn);

    /**
     * Notify the users that a message failed to be delivered to destination
     * @param data The message data
     * @param params The list of parameters
     * @param ssn The ssn of the SCCP user
     * @return HandledMSU enum value
     */
    HandledMSU notifyMessage(DataBlock& data, NamedList& params, int ssn);

    /**
     * Broadcast a management message to all attached users
     * @param type The type of notification
     * @param params The list of parameters
     * @return True if at least one user processed the message
     */
    bool managementMessage(Type type, NamedList& params);

    /**
     * Check if this sccp is an endpoint
     * @return False
     */
    virtual bool isEndpoint()
	{ return false; }

    /**
     * Copy the parameters returned by Global Title Translator in the SCCP Message
     * @param msg The SCCP message
     * @param gtParams The parameters returned by GTT
     */
    void resolveGTParams(SS7MsgSCCP* msg, const NamedList* gtParams);
private:
    ObjList m_users;
    Mutex m_translatorLocker;
    Mutex m_usersLocker;
    GTT* m_translator;
};

class YSIG_API SubsystemStatusTest : public RefObject
{
    YCLASS(SubsystemStatusTest,RefObject)
public:
    /**
     * Constructor
     * @param interval The time interval in milliseconds representing the test duration
     */
    inline SubsystemStatusTest(u_int32_t interval)
	: m_interval(interval), m_statusInfo(interval), m_remoteSccp(0), m_remoteSubsystem(0),
	m_markAllowed(false)
	{ }

    /**
     * Destructor
     */
    virtual ~SubsystemStatusTest();

    /**
     * Start this subsystem status test
     * @param remoteSccp The remote sccp where the subsystem is located
     * @param rSubsystem The remote subsystem for witch the test is performed
     * @return True if the test has successfully started
     */
    bool startTest(SccpRemote* remoteSccp, SccpSubsystem* rSubsystem);

    /**
     * Obtain the remote SCCP of this status test
     * @return Remote Sccp
     */
    inline SccpRemote* getRemote()
	{ return m_remoteSccp; };

    /**
     * Helper method to check for timeouts
     * @return True if timed out
     */
    inline bool timeout()
	{ return m_statusInfo.started() && m_statusInfo.timeout(); }
    /**
     * Get the subsystem who caused this test
     * @return The subsystem for who this test was initiated
     */
    inline SccpSubsystem* getSubsystem()
	{ return m_remoteSubsystem; }

    /**
     * Restart subsystem status test with exponential backoff
     */
    void restartTimer();

    /**
     * Helper method used to find if we should mark the remote subsystem as allowed at the end of the test
     */
    inline bool markAllowed()
	{ return m_markAllowed; }

    /**
     * Mark the tested subsystem as allowed at the end of the test
     * @param allowed True to set the subsystem allowed at the end of the test
     */
    inline void setAllowed(bool allowed)
	{ m_markAllowed = allowed; }
private:
    u_int32_t m_interval;
    SignallingTimer m_statusInfo;
    SccpRemote* m_remoteSccp;
    SccpSubsystem* m_remoteSubsystem;
    bool m_markAllowed;
};

/**
 * An interface to a SS7 Signalling Connection Control Part user
 * @short Abstract SS7 SCCP user interface
 */
class YSIG_API SCCPUser : virtual public SignallingComponent
{
    YCLASS(SCCPUser,SignallingComponent)
public:
    /**
     * Constructor
     */
    SCCPUser(const NamedList& params);

    /**
     * Destructor, detaches from the SCCP implementation
     */
    virtual ~SCCPUser();

    /**
     * Configure and initialize the component and any subcomponents it may have
     * @param config Optional configuration parameters override
     * @return True if the component was initialized properly
     */
    virtual bool initialize(const NamedList* config);

    /**
     * Send a message to SCCP for transport
     * @param data User data
     * @param params SCCP parameters
     * Note! If the request is made with return option set there is no warranty that a notification
     * will be received in case that the message failed to be delivered
     */
    virtual bool sendData(DataBlock& data, NamedList& params);

    /**
     * Send a request / notification from users to sccp regarding a subsystem status
     * <pre>
      Type : CoordinateRequest -> Request from a user to sccp to go OOS
           params: "ssn" The ssn to go OOS
     	           "smi" Subsystem multiplicity indicator
     		   "backups" The number of backup subsystems
     		   "backup.N.ssn" The ssn of the backup subsystem number N
     		   "backup.N.pointcode" The ssn of the backup subsystem number N
      Type : CoordinateResponse -> Indication from a user to sccp in response to CoordinateIndication<
           params: "ssn" The subsystem number that approved the indication
                   "smi" 0
      Type : StatusRequest -> Request from user to sccp to update a subsystem status
       	   params: "ssn" The affected subsystem number
       		   "smi" 0;
      		   "subsystem-status": The requested status: UserOutOfService, UserInService
      </pre>
     * @param type The type of request / notification
     * @param params List of parameters
     * @return True if sccp management has processed the request / notification.
     */
    virtual bool sccpNotify(SCCP::Type type, NamedList& params);

    /**
     * Notification from SCCP that a message has arrived
     * @param data Received user data
     * @param params SCCP parameters
     * @return True if this user has processed the message, false otherwise
     */
     virtual HandledMSU receivedData(DataBlock& data, NamedList& params);

    /**
     * Notification from SCCP that a message failed to arrive to it's destination
     * @param data User data send.
     * @param params SCCP parameters
     * Note! The data may not contain the full message block previously sent (in case of SCCP segmentation),
     * but it must always must contain the first segment
     */
     virtual HandledMSU notifyData(DataBlock& data, NamedList& params);

     /**
      * Notification from SCCP management to a sccp user about pointcodes status, OOS responses/indications, subsystems status
      * <pre>
      Type: CoordinateIndication -> Indication from a remote SCCP User that requires to go OOS
          params: "ssn" -> The subsystem number of the remote SCCP User
      	          "smi" -> Subsystem multiplicity indicator
      	          "pointcode" -> The pointcode of the remote SCCP User
      Type: SubsystemStatus -> Request from SCCP Management to SCCP Users to query the specified subsystem status.
                               This happens when a SubsystemStatusTest(SST) message is received
      	  params: "ssn" -> The requested subsystem
          returned params:
      	          "subsystem-status" -> The status of the subsystem: UserOutOfService, UserInService
      	        		        Missing = UserOutOfService
      </pre>
      * @param type The type of notification
      * @param params List of parameters
      * @return False on error
      */
     virtual bool managementNotify(SCCP::Type type, NamedList& params);

    /**
     * Attach as user to a SCCP
     * @param sccp Pointer to the SCCP to use
     * NOTE: This method will deref the pointer if is the same with the one that we already have!!
     * When this method is called the sccp pointer reference counter must be incremented for this
     * SCCPUser.
     */
    virtual void attach(SCCP* sccp);

    /**
     * Retrieve the SCCP to which this component is attached
     * @return Pointer to the attached SCCP or NULL
     */
    inline SCCP* sccp() const
	{ return m_sccp; }

protected:
    virtual void destroyed();

private:
    SCCP* m_sccp;
    Mutex m_sccpMutex;
    int m_sls;
};

/**
 * An interface to a SS7 Transactional Capabilities Application Part user
 * @short Abstract SS7 TCAP user interface
 */
class YSIG_API TCAPUser : public SignallingComponent
{
    YCLASS(TCAPUser,SignallingComponent)
    friend class SS7TCAP;
public:
    TCAPUser(const char* name, const NamedList* params = 0)
      : SignallingComponent(name,params),
	m_tcap(0)
	{}
    /**
     * Destructor, detaches from the TCAP implementation
     */
    virtual ~TCAPUser();

    /**
     * Attach as user to a SS7 TCAP
     * @param tcap Pointer to the TCAP to use
     */
    virtual void attach(SS7TCAP* tcap);

    /**
     * Receive a TCAP message from TCAP layer
     * @param params The message in NamedList form
     * @return True or false if the message was processed by this user
     */
    virtual bool tcapIndication(NamedList& params);

    /**
     * Retrieve the TCAP to which this user is attached
     * @return Pointer to a SS7 TCAP interface or NULL
     */
    inline SS7TCAP* tcap() const
	{ return m_tcap; }

    /**
     * Received a management notification from SCCP layer
     * @param type SCCP management notification type
     * @param params Management notification params
     * @return True or false if the notification was handled bu this user
     */
    virtual bool managementNotify(SCCP::Type type, NamedList& params);

    /**
     * Get TCAP user management state
     * @return The state of the user
     */
    virtual int managementState();

    /**
     * This method is called to clean up and destroy the object after the
     *  reference counter becomes zero
     */
    virtual void  destroyed();

protected:
    inline void setTCAP(SS7TCAP* tcap)
    {
	Lock l(m_tcapMtx);
	m_tcap = tcap;
    }

private:
    SS7TCAP* m_tcap;
    Mutex m_tcapMtx;
};


/**
 * An user of a Layer 2 (data link) SS7 message transfer part
 * @short Abstract user of SS7 layer 2 (data link) message transfer part
 */
class YSIG_API SS7L2User : virtual public SignallingComponent
{
    YCLASS(SS7L2User,SignallingComponent)
    friend class SS7Layer2;
public:
    /**
     * Attach a SS7 Layer 2 (data link) to the user component
     * @param link Pointer to data link to attach
     */
    virtual void attach(SS7Layer2* link) = 0;

    /**
     * Detach a SS7 Layer 2 (data link) from the user component
     * @param link Pointer to data link to detach
     */
    virtual void detach(SS7Layer2* link) = 0;

protected:
    /**
     * Process a MSU received from the Layer 2 component
     * @param msu Message data, starting with Service Indicator Octet
     * @param link Data link that delivered the MSU
     * @param sls Signalling Link the MSU was received from
     * @return True if the MSU was processed
     */
    virtual bool receivedMSU(const SS7MSU& msu, SS7Layer2* link, int sls) = 0;

    /**
     * Process a MSU recovered from the Layer 2 component after failure
     * @param msu Message data, starting with Service Indicator Octet
     * @param link Data link from where the MSU was recovered
     * @param sls Signalling Link the MSU was recovered from
     * @return True if the MSU was processed
     */
    virtual bool recoveredMSU(const SS7MSU& msu, SS7Layer2* link, int sls) = 0;

    /**
     * Process a notification generated by the attached data link
     * @param link Data link that generated the notification
     */
    virtual void notify(SS7Layer2* link) = 0;
};

/**
 * An interface to a Layer 2 (data link) SS7 message transfer part
 * @short Abstract SS7 layer 2 (data link) message transfer part
 */
class YSIG_API SS7Layer2 : virtual public SignallingComponent
{
    YCLASS(SS7Layer2,SignallingComponent)
    friend class SS7MTP3;
public:
    /**
     * LSSU Status Indications
     */
    enum LinkStatus {
	OutOfAlignment = 0,
	NormalAlignment = 1,
	EmergencyAlignment = 2,
	OutOfService = 3,
	ProcessorOutage = 4,
	Busy = 5,
	// short versions as defined by RFC
	O = OutOfAlignment,
	N = NormalAlignment,
	E = EmergencyAlignment,
	OS = OutOfService,
	PO = ProcessorOutage,
	B = Busy,
    };

    /**
     * Control primitives
     */
    enum Operation {
	// take link out of service
	Pause  = 0x100,
	// start link operation, align if it needs to
	Resume = 0x200,
	// start link, force realignment
	Align  = 0x300,
	// get operational status
	Status = 0x400,
    };

    /**
     * Link inhibition reason bits
     */
    enum Inhibitions {
	// basic maintenance checks not performed
	Unchecked = 0x01,
	// management inactivation
	Inactive  = 0x02,
	// locally inhibited
	Local     = 0x04,
	// remotely inhibited
	Remote    = 0x08,
    };

    /**
     * Push a Message Signal Unit down the protocol stack
     * @param msu Message data, starting with Service Indicator Octet
     * @return True if message was successfully queued
     */
    virtual bool transmitMSU(const SS7MSU& msu) = 0;

    /**
     * Remove the MSUs waiting in the transmit queue and return them
     * @param sequence First sequence number to recover, flush earlier packets
     */
    virtual void recoverMSU(int sequence)
	{ }

    /**
     * Retrieve the current link status indications
     * @return Link status indication bits
     */
    virtual unsigned int status() const;

    /**
     * Get the name of a Layer 2 status
     * @param status Status indication value
     * @param brief Request to return the short status name
     * @return String describing the status
     */
    virtual const char* statusName(unsigned int status, bool brief) const;

    /**
     * Get the name of the current local Layer 2 status
     * @param brief Request to return the short status name
     * @return String describing the status
     */
    inline const char* statusName(bool brief = false) const
	{ return statusName(status(),brief); }

    /**
     * Check if the link is fully operational
     * @return True if the link is aligned and operational
     */
    virtual bool operational() const = 0;

    /**
     * Get the uptime of the link
     * @return Time since link got up in seconds
     */
    unsigned int upTime() const
	{ return m_lastUp ? (Time::secNow() - m_lastUp) : 0; }

    /**
     * Attach a Layer 2 user component to the data link. Detach from the old one if valid
     * @param l2user Pointer to Layer 2 user component to attach
     */
    void attach(SS7L2User* l2user);

    /**
     * Get the Layer 2 user component that works with this data link
     * @return Pointer to the user component to which the messages are sent
     */
    inline SS7L2User* user() const
	{ return m_l2user; }

    /**
     * Get the Signalling Link Selection number allocated to this link
     * @return SLS value assigned by the upper layer
     */
    inline int sls() const
	{ return m_sls; }

    /**
     * Assign a new Signalling Link Selection number
     * @param linkSel New SLS to assign to this link
     */
    inline void sls(int linkSel)
	{ if ((m_sls < 0) || !m_l2user) m_sls = linkSel; }

    /**
     * Retrieve the inhibition flags set by MTP3 Management
     * @return Inhibition flags ORed together, zero if not inhibited
     */
    inline int inhibited() const
	{ return m_inhibited; }

    /**
     * Check some of the inhibition flags set by MTP3 Management
     * @param flags Flags to check for, ORed together
     * @return True if any of the specified inhibition flags is active
     */
    inline bool inhibited(int flags) const
	{ return (m_inhibited & flags) != 0; }

    /**
     * Get the current congestion level of the link
     * @return Congestion level, 0 if not congested, 3 if maximum congestion
     */
    virtual unsigned int congestion()
	{ return m_congestion; }

    /**
     * Get the sequence number of the last MSU received
     * @return Last FSN received, negative if not available
     */
    virtual int getSequence()
	{ return m_lastSeqRx; }

    /**
     * Execute a control operation. Operations can change the link status or
     *  can query the aligned status.
     * @param oper Operation to execute
     * @param params Optional parameters for the operation
     * @return True if the command completed successfully, for query operations
     *  also indicates the data link is aligned and operational
     */
    virtual bool control(Operation oper, NamedList* params = 0);

    /**
     * Query or modify layer's settings or operational parameters
     * @param params The list of parameters to query or change
     * @return True if the control operation was executed
     */
    virtual bool control(NamedList& params);

protected:
    /**
     * Constructor
     */
    inline SS7Layer2()
	: m_autoEmergency(true), m_lastSeqRx(-1), m_congestion(0),
	  m_l2userMutex(true,"SS7Layer2::l2user"), m_l2user(0), m_sls(-1),
	  m_checkTime(0), m_checkFail(0), m_inhibited(Unchecked), m_lastUp(0),
	  m_notify(false)
	{ }

    /**
     * Method called periodically by the engine to keep everything alive
     * @param when Time to use as computing base for events and timeouts
     */
    virtual void timerTick(const Time& when);

    /**
     * Push a received Message Signal Unit up the protocol stack
     * @param msu Message data, starting with Service Indicator Octet
     * @return True if message was successfully delivered to the user component
     */
    inline bool receivedMSU(const SS7MSU& msu)
    {
	m_l2userMutex.lock();
	RefPointer<SS7L2User> tmp = m_l2user;
	m_l2userMutex.unlock();
	return tmp && tmp->receivedMSU(msu,this,m_sls);
    }

    /**
     * Push a recovered Message Signal Unit back up the protocol stack
     * @param msu Message data, starting with Service Indicator Octet
     * @return True if message was successfully delivered to the user component
     */
    inline bool recoveredMSU(const SS7MSU& msu)
    {
	m_l2userMutex.lock();
	RefPointer<SS7L2User> tmp = m_l2user;
	m_l2userMutex.unlock();
	return tmp && tmp->recoveredMSU(msu,this,m_sls);
    }

    /**
     * Set the notify flag.
     * The user part will be notified on timer tick about status change
     */
    void notify();

    /**
     * Set and clear inhibition flags, method used by MTP3
     * @param setFlags Flag bits to set ORed together
     * @param clrFlags Flag bits to clear  ORed together (optional)
     * @return True if inhibition flags were set
     */
    bool inhibit(int setFlags, int clrFlags = 0);

    /**
     * Get a best guess of the emergency alignment requirement
     * @param params Optional parameters whose "emergency" is used
     * @param emg Default emergency state
     * @return True if emergency alignment should be used
     */
    bool getEmergency(NamedList* params = 0, bool emg = false) const;

    /**
     * Flag to automatically perform emergency alignment when linkset is down
     */
    bool m_autoEmergency;

    /**
     * Last received MSU sequence number, -1 if unknown, bit 24 set if long FSN
     */
    int m_lastSeqRx;

    /**
     * Current congestion level
     */
    unsigned int m_congestion;

private:
    Mutex m_l2userMutex;
    SS7L2User* m_l2user;
    int m_sls;
    u_int64_t m_checkTime;
    int m_checkFail;
    int m_inhibited;
    u_int32_t m_lastUp;
    bool m_notify;                       // Notify on timer tick
};

/**
 * Keeps a packed destination point code, a network priority or a list of networks used
 *  to route to the enclosed destination point code
 * @short A SS7 MSU route
 */
class YSIG_API SS7Route : public RefObject, public Mutex
{
    friend class SS7Layer3;
    friend class SS7Router;
public:
    /**
     * Route state
     */
    enum State {
	Unknown       = 0x80,
	// Standard route states
	Prohibited    = 0x01,
	Restricted    = 0x02,
	Congestion    = 0x04,
	Allowed       = 0x08,
	// Masks for typical combinations
	NotAllowed    = 0x77,
	NotCongested  = 0x78,
	NotRestricted = 0x7c,
	NotProhibited = 0x7e,
	KnownState    = 0x7f,
	AnyState      = 0xff
    };

    /**
     * Constructor
     * @param packed The packed value of the destination point code
     * @param type The destination point code type
     * @param priority Optional value of the network priority
     * @param shift SLS right shift to apply for balancing between linksets
     * @param maxDataLength The maximum data that can be transported on this
     *        route
     */
    inline SS7Route(unsigned int packed, SS7PointCode::Type type,
	    unsigned int priority = 0, unsigned int shift = 0,
	    unsigned int maxDataLength = 272)
	: Mutex(true,"SS7Route"), m_packed(packed), m_type(type),
	m_priority(priority), m_shift(shift),m_maxDataLength(maxDataLength),
	m_state(Unknown),m_buffering(0), m_congCount(0),m_congBytes(0)
	{ m_networks.setDelete(false); }

    /**
     * Copy constructor
     * @param original The original route
     */
    inline SS7Route(const SS7Route& original)
	: Mutex(true,"SS7Route"), m_packed(original.packed()),
	  m_type(original.m_type), m_priority(original.priority()),
	  m_shift(original.shift()), m_maxDataLength(original.getMaxDataLength()),
	  m_state(original.state()), m_buffering(0), m_congCount(0), m_congBytes(0)
	{ m_networks.setDelete(false); }

    /**
     * Destructor
     */
    virtual ~SS7Route()
	{ }

    /**
     * Retrieve the current state of the route
     * @return Current route state
     */
    State state() const
	{ return m_state; }

    /**
     * Retrieve the state names token table
     * @return Pointer to the token table mapping states to names
     */
    static const TokenDict* stateNames();

    /**
     * Retrieve the name of the current state
     * @return Name of the state, NULL if invalid
     */
    const char* stateName() const
	{ return lookup(m_state,stateNames()); }

    /**
     * Retrieve the name of an arbitrary state
     * @param state Route state whose name to return
     * @return Name of the state, NULL if invalid
     */
    static const char* stateName(State state)
	{ return lookup(state,stateNames()); }

    /**
     * Get the priority of this route
     * @return Route priority, zero = highest (adjacent)
     */
    unsigned int priority() const
	{ return m_priority; }

    /**
     * Get the maximum data length that can be transported on this route
     * @return The maximum data length
     */
    unsigned int getMaxDataLength() const
	{ return m_maxDataLength; }

    /**
     * Get the packed Point Code of this route
     * @return Packed Point Code of the route's destination
     */
    unsigned int packed() const
	{ return m_packed; }

    /**
     * Get the SLS right shift for this route
     * @return How many bits of SLS to shift off right when selecting linkset
     */
    unsigned int shift() const
	{ return m_shift; }

    /**
     * Attach a network to use for this destination or change its priority.
     * This method is thread safe
     * @param network The network to attach or change priority
     * @param type The point code type used to get the priority from the given network or the networks already in the list
     */
    void attach(SS7Layer3* network, SS7PointCode::Type type);

    /**
     * Remove a network from the list without deleting it.
     * This method is thread safe
     * @param network The network to remove
     * @return False if the list of networks is empty
     */
    bool detach(SS7Layer3* network);

    /**
     * Check if this route goes to a specific network
     * @param network Pointer to the network to search
     * @return True if the network was found in the route's list
     */
    bool hasNetwork(const SS7Layer3* network);

    /**
     * Check if this route goes to a specific network
     * @param network Pointer to the network to search
     * @return True if the network was found in the route's list
     */
    bool hasNetwork(const SS7Layer3* network) const;

    /**
     * Check if the at least one network/linkset is fully operational
     * @param sls Signalling Link to check, negative to check if any is operational
     * @return True if the route has at least one linkset operational
     */
    bool operational(int sls = -1);

    /**
     * Try to transmit a MSU through one of the attached networks.
     * This method is thread safe
     * @param router The router requesting the operation (used for debug)
     * @param msu Message data, starting with Service Indicator Octet
     * @param label Routing label of the MSU
     * @param sls Signalling Link Selection, negative to choose best
     * @param source Avoided network where the packet was received from
     * @param states The states a network can have to be a transmission candidate
     * @return Link the message was successfully queued to, negative for error
     */
    int transmitMSU(const SS7Router* router, const SS7MSU& msu,
	const SS7Label& label, int sls, State states, const SS7Layer3* source = 0);

    /**
     * Check the current congestion status according to Q.704 11.2.3.1
     * @return True if a TFC should be sent
     */
    bool congested();

    /**
     * Initiate controlled rerouting procedure, buffer user part messages for T6
     */
    void reroute();

private:
    int transmitInternal(const SS7Router* router, const SS7MSU& msu,
	const SS7Label& label, int sls, State states, const SS7Layer3* source);
    void rerouteCheck(u_int64_t when);
    void rerouteFlush();
    unsigned int m_packed;               // Packed destination point code
    SS7PointCode::Type m_type;           // The point code type
    unsigned int m_priority;             // Network priority for the given destination (used by SS7Layer3)
    unsigned int m_shift;                // SLS right shift when selecting linkset
    unsigned int m_maxDataLength;        // The maximum data length that can be transported on this route
    ObjList m_networks;                  // List of networks used to route to the given destination (used by SS7Router)
    State m_state;                       // State of the route
    u_int64_t m_buffering;               // Time when controlled rerouting ends
    ObjList m_reroute;                   // Controlled rerouting buffer
    unsigned int m_congCount;            // Congestion event count
    unsigned int m_congBytes;            // Congestion MSU bytes count
};

/**
 * An user of a Layer 3 (data link) SS7 message transfer part
 * @short Abstract user of SS7 layer 3 (network) message transfer part
 */
class YSIG_API SS7L3User : virtual public SignallingComponent
{
    friend class SS7Layer3;
    friend class SS7Router;
public:
    /**
     * Attach a SS7 Layer 3 (network) to the user component
     * @param network Pointer to network component to attach
     */
    virtual void attach(SS7Layer3* network) = 0;

protected:
    /**
     * Process a MSU received from the Layer 3 component
     * @param msu Message data, starting with Service Indicator Octet
     * @param label Routing label of the received MSU
     * @param network Network layer that delivered the MSU
     * @param sls Signalling Link the MSU was received from
     * @return Result of MSU processing
     */
    virtual HandledMSU receivedMSU(const SS7MSU& msu, const SS7Label& label, SS7Layer3* network, int sls) = 0;

    /**
     * Reroute a recovered Message Signal Unit
     * @param msu Message data, starting with Service Indicator Octet
     * @param label Routing label of the recovered MSU
     * @param network Network layer that recovered the MSU
     * @param sls Signalling Link the MSU was recovered from
     * @return True if the MSU was successfully rerouted
     */
    virtual bool recoveredMSU(const SS7MSU& msu, const SS7Label& label, SS7Layer3* network, int sls)
	{ return false; }

    /**
     * Notification for receiving User Part Unavailable
     * @param type Type of Point Code
     * @param node Node on which the User Part is unavailable
     * @param part User Part (service) reported unavailable
     * @param cause Unavailability cause - Q.704 15.17.5
     * @param label Routing label of the UPU message
     * @param sls Signaling link the UPU was received on
     */
    virtual void receivedUPU(SS7PointCode::Type type, const SS7PointCode node,
	SS7MSU::Services part, unsigned char cause, const SS7Label& label, int sls)
	{ }

    /**
     * Process a notification generated by the attached network layer
     * @param link Network or linkset that generated the notification
     * @param sls Signalling Link that generated the notification, negative if none
     */
    virtual void notify(SS7Layer3* link, int sls);

    /**
     * Process route status changed notifications
     * @param type Type of Point Code
     * @param node Destination node witch state has changed
     * @param state The new route state
     */
    virtual void routeStatusChanged(SS7PointCode::Type type, const SS7PointCode& node, SS7Route::State state)
	{ }

    /**
     * Retrieve the route table of a network for a specific Point Code type
     * @param network Network layer to retrieve routes from
     * @param type Point Code type of the desired table
     * @return Pointer to the list of SS7Route or NULL if no such route
     */
    static ObjList* getNetRoutes(SS7Layer3* network, SS7PointCode::Type type);

    /**
     * Retrieve the route table of a network for a specific Point Code type
     * @param network Network layer to retrieve routes from
     * @param type Point Code type of the desired table
     * @return Pointer to the list of SS7Route or NULL if no such route
     */
    static const ObjList* getNetRoutes(const SS7Layer3* network, SS7PointCode::Type type);
};

/**
 * An interface to a Layer 3 (network) SS7 message transfer part
 * @short Abstract SS7 layer 3 (network) message transfer part
 */
class YSIG_API SS7Layer3 : virtual public SignallingComponent
{
    YCLASS(SS7Layer3,SignallingComponent)
    friend class SS7L3User;
    friend class SS7Router;              // Access the data members to build the routing table
    friend class SS7Route;
public:
    /**
     * Destructor
     */
    virtual ~SS7Layer3()
	 { attach(0); }

    /**
     * Initialize the network layer, connect it to the SS7 router
     * @param config Optional configuration parameters override
     * @return True if the network was initialized properly
     */
    virtual bool initialize(const NamedList* config);

    /**
     * Push a Message Signal Unit down the protocol stack
     * @param msu Message data, starting with Service Indicator Octet
     * @param label Routing label of the MSU to use in routing
     * @param sls Signalling Link Selection, negative to choose best
     * @return Link the message was successfully queued to, negative for error
     */
    virtual int transmitMSU(const SS7MSU& msu, const SS7Label& label, int sls = -1) = 0;

    /**
     * Check if the network/linkset is fully operational
     * @param sls Signalling Link to check, negative to check if any is operational
     * @return True if the linkset is enabled and operational
     */
    virtual bool operational(int sls = -1) const = 0;

    /**
     * Retrieve inhibition flags of a specific link
     * @param sls Signalling Link to check
     * @return Inhibitions of the specified link, zero if not inhibited
     */
    virtual int inhibited(int sls) const
	{ return 0; }

    /**
     * Check some of the inhibition flags of a specific link
     * @param sls Signalling Link to check
     * @param flags Flags to check for, ORed together
     * @return True if any of the specified inhibition flags is active
     */
    inline bool inhibited(int sls, int flags) const
	{ return (inhibited(sls) & flags) != 0; }

    /**
     * Set and clear inhibition flags on the links
     * @param sls Signalling Link to modify
     * @param setFlags Flag bits to set ORed together
     * @param clrFlags Flag bits to clear ORed together (optional)
     * @return True if inhibition flags were set
     */
    virtual bool inhibit(int sls, int setFlags, int clrFlags = 0)
	{ return false; }

    /**
     * Check if a link is operational and not inhibited
     * @param sls Signalling Link to check
     * @param ignore Inhibition flags to ignore, ORed together
     * @return True if the link is operational and not inhibited
     */
    inline bool inService(int sls, int ignore = 0)
	{ return operational(sls) && !inhibited(sls,~ignore); }

    /**
     * Get the current congestion level of a link
     * @param sls Signalling Link to check for congestion, -1 for maximum
     * @return Congestion level, 0 if not congested, 3 if maximum congestion
     */
    virtual unsigned int congestion(int sls)
	{ return 0; }

    /**
     * Get the sequence number of the last MSU received on a link
     * @param sls Signalling Link to retrieve MSU number from
     * @return Last FSN received, negative if not available
     */
    virtual int getSequence(int sls) const
	{ return -1; }

    /**
     * Remove the MSUs waiting in the transmit queue and return them
     * @param sls Signalling Link to recover MSUs from
     * @param sequence First sequence number to recover, flush earlier packets
     */
    virtual void recoverMSU(int sls, int sequence)
	{ }

    /**
     * Initiate a MTP restart procedure if supported by the network layer
     * @return True if a restart was initiated
     */
    virtual bool restart()
	{ return false; }

    /**
     * Attach a Layer 3 user component to this network. Detach the old user if valid.
     * Attach itself to the given user
     * @param l3user Pointer to Layer 3 user component to attach
     */
    void attach(SS7L3User* l3user);

    /**
     * Retrieve the Layer 3 user component to which this network is attached
     * @return Pointer to the Layer 3 user this network is attached to
     */
    inline SS7L3User* user() const
	{ return m_l3user; }

    /**
     * Retrieve the point code type of this Layer 3 component for a MSU type
     * @param netType Type of the network like coded in the MSU NI field
     * @return The type of codepoint this component will use
     */
    SS7PointCode::Type type(unsigned char netType) const;

    /**
     * Set the point code of this Layer 3 component for a network type
     * @param type Point code type to set for the network type
     * @param netType Type of the network like coded in the MSU NI field
     */
    void setType(SS7PointCode::Type type, unsigned char netType);

    /**
     * Set the point code of this Layer 3 component for all network types
     * @param type Point code type to set
     */
    void setType(SS7PointCode::Type type);

    /**
     * Check if the network can possibly handle a Point Code type
     * @param pcType Point code type to check
     * @return True if there is one network type that can handle the Point Code
     */
    bool hasType(SS7PointCode::Type pcType) const;

    /**
     * Get the Network Indicator bits that would match a Point Code type
     * @param pcType Point Code type to search for
     * @param defNI Default Network Indicator bits to use
     * @return Network Indicator bits matching the Point Code type
     */
    virtual unsigned char getNI(SS7PointCode::Type pcType, unsigned char defNI) const;

    /**
     * Get the Network Indicator bits that would match a Point Code type
     * @param pcType Point Code type to search for
     * @return Network Indicator bits matching the Point Code type
     */
    inline unsigned char getNI(SS7PointCode::Type pcType) const
	{ return getNI(pcType,m_defNI); }

    /**
     * Get the default Network Indicator bits
     * @return Default Network Indicator bits for this layer
     */
    inline unsigned char getNI() const
	{ return m_defNI; }

    /**
     * Set the default Network Indicator bits
     * @param defNI Network Indicator bits to set as defaults
     */
    void setNI(unsigned char defNI);

    /**
     * Build the list of outgoing routes serviced by this network. Clear the list before re-building it.
     * This method is thread safe
     * @param params The parameter list
     * @return False if no route available
     */
    bool buildRoutes(const NamedList& params);

    /**
     * Get the maximum data length of a route by packed Point Code.
     * This method is thread safe
     * @param type Destination point code type
     * @param packedPC The packed point code
     * @return The maximum data length that can be transported on the route. Maximum msu size (272) if no route to the given point code
     */
    unsigned int getRouteMaxLength(SS7PointCode::Type type, unsigned int packedPC);

    /**
     * Get the priority of a route by packed Point Code.
     * This method is thread safe
     * @param type Destination point code type
     * @param packedPC The packed point code
     * @return The priority of the route. -1 if no route to the given point code
     */
    unsigned int getRoutePriority(SS7PointCode::Type type, unsigned int packedPC);

    /**
     * Get the priority of a route by unpacked Point Code.
     * This method is thread safe
     * @param type Destination point code type
     * @param dest The destination point code
     * @return The priority of the route. -1 if no route to the given point code
     */
    inline unsigned int getRoutePriority(SS7PointCode::Type type, const SS7PointCode& dest)
	{ return getRoutePriority(type,dest.pack(type)); }

    /**
     * Get the current state of a route by packed Point Code.
     * This method is thread safe
     * @param type Destination point code type
     * @param packedPC The packed point code
     * @param checkAdjacent True to take into account the adjacent STP
     * @return The state of the route, SS7Route::Unknown if no route to the given point code
     */
    SS7Route::State getRouteState(SS7PointCode::Type type, unsigned int packedPC, bool checkAdjacent = false);

    /**
     * Get the current state of a route by unpacked Point Code.
     * This method is thread safe
     * @param type Destination point code type
     * @param dest The destination point code
     * @param checkAdjacent True to take into account the adjacent STP
     * @return The state of the route, SS7Route::Unknown if no route to the given point code
     */
    inline SS7Route::State getRouteState(SS7PointCode::Type type, const SS7PointCode& dest, bool checkAdjacent = false)
	{ return getRouteState(type,dest.pack(type),checkAdjacent); }

    /**
     * Check if access to a specific Point Code is allowed from this network
     * @param type Destination point code type
     * @param packedPC The destination point code
     * @return True if access to the specified Point Code is allowed
     */
    virtual bool allowedTo(SS7PointCode::Type type, unsigned int packedPC) const
	{ return true; }

    /**
     * Print the destinations or routing table to output
     */
    void printRoutes();

    /**
     * Retrieve the local Point Code for a specific Point Code type
     * @param type Desired Point Code type
     * @return Packed local Point Code, zero if not set
     */
    inline unsigned int getLocal(SS7PointCode::Type type) const
	{ return (type < SS7PointCode::DefinedTypes) ? m_local[type-1] : 0; }

    /**
     * Retrieve the default local Point Code for a specific Point Code type
     * @param type Desired Point Code type
     * @return Packed local Point Code, zero if not set
     */
    virtual unsigned int getDefaultLocal(SS7PointCode::Type type) const
	{ return getLocal(type); }

protected:
    /**
     * Constructor
     * @param type Default point code type
     */
    SS7Layer3(SS7PointCode::Type type = SS7PointCode::Other);

    /**
     * Push a received Message Signal Unit up the protocol stack
     * @param msu Message data, starting with Service Indicator Octet
     * @param label Routing label of the received MSU
     * @param sls Signalling Link the MSU was received from
     * @return Result of MSU processing by user part
     */
    inline HandledMSU receivedMSU(const SS7MSU& msu, const SS7Label& label, int sls)
    {
	m_l3userMutex.lock();
	RefPointer<SS7L3User> tmp = m_l3user;
	m_l3userMutex.unlock();
	return tmp ? tmp->receivedMSU(msu,label,this,sls) : HandledMSU(HandledMSU::Unequipped);
    }

    /**
     * Push a recovered Message Signal Unit back up the protocol stack
     * @param msu Message data, starting with Service Indicator Octet
     * @param label Routing label of the recovered MSU
     * @param sls Signalling Link the MSU was recovered from
     * @return True if the MSU was successfully rerouted
     */
    inline bool recoveredMSU(const SS7MSU& msu, const SS7Label& label, int sls)
    {
	m_l3userMutex.lock();
	RefPointer<SS7L3User> tmp = m_l3user;
	m_l3userMutex.unlock();
	return tmp && tmp->recoveredMSU(msu,label,this,sls);
    }

    /**
     * Notify out user part about a status change
     * @param sls Link that generated the notification, -1 if none
     */
    inline void notify(int sls = -1)
    {
	m_l3userMutex.lock();
	RefPointer<SS7L3User> tmp = m_l3user;
	m_l3userMutex.unlock();
	if (tmp)
	    tmp->notify(this,sls);
    }

    /**
     * Callback called from maintenance when valid SLTA or SLTM are received
     * @param sls Link that was checked by maintenance
     * @param remote True if remote checked the link, false if local success
     */
    virtual void linkChecked(int sls, bool remote)
	{ }

    /**
     * Default processing of a MTN (Maintenance MSU)
     * @param msu Message data, starting with Service Indicator Octet
     * @param label Routing label of the received MSU
     * @param sls Signalling Link the MSU was received from
     * @return True if the MSU was processed
     */
    virtual bool maintenance(const SS7MSU& msu, const SS7Label& label, int sls);

    /**
     * Default processing of a SNM (Management MSU)
     * @param msu Message data, starting with Service Indicator Octet
     * @param label Routing label of the received MSU
     * @param sls Signalling Link the MSU was received from
     * @return True if the MSU was processed
     */
    virtual bool management(const SS7MSU& msu, const SS7Label& label, int sls);

    /**
     * Default processing of an unknown MSU - emit an User Part Unavailable
     * @param msu Message data, starting with Service Indicator Octet
     * @param label Routing label of the received MSU
     * @param sls Signalling Link the MSU was received from
     * @param cause Unavailability cause code (Q.704 15.17.5)
     * @return True if the MSU was processed
     */
    virtual bool unavailable(const SS7MSU& msu, const SS7Label& label, int sls, unsigned char cause = 0);

    /**
     * Send a Transfer Prohibited if an unexpected MSU is received in STP mode
     * @param ssf Subservice Field of received MSU
     * @param label Routing label of the received MSU
     * @param sls Signalling Link the MSU was received from
     * @return True if the TFP was sent
     */
    virtual bool prohibited(unsigned char ssf, const SS7Label& label, int sls);

    /**
     * Check if we should answer with SLTA to received SLTM in maintenance()
     * @return True to send a SLTA for each good received SLTM
     */
    virtual bool responder() const
	{ return true; }

    /**
     * Find a route having the specified point code type and packed point code.
     * This method is thread safe
     * @param type The point code type used to choose the list of packed point codes
     * @param packed The packed point code to find in the list
     * @return SS7Route pointer or 0 if type is invalid or the given packed point code was not found
     */
    SS7Route* findRoute(SS7PointCode::Type type, unsigned int packed);

    /**
     * Retrieve the route table for a specific Point Code type
     * @param type Point Code type of the desired table
     * @return Pointer to the list of SS7Route or NULL if no such route
     */
    inline ObjList* getRoutes(SS7PointCode::Type type)
	{ return (type < SS7PointCode::DefinedTypes) ? &m_route[type-1] : 0; }

    /**
     * Retrieve the route table for a specific Point Code type
     * @param type Point Code type of the desired table
     * @return Pointer to the list of SS7Route or NULL if no such route
     */
    inline const ObjList* getRoutes(SS7PointCode::Type type) const
	{ return (type < SS7PointCode::DefinedTypes) ? &m_route[type-1] : 0; }

    /** Mutex to lock routing list operations */
    Mutex m_routeMutex;

    /** Outgoing point codes serviced by a network (for each point code type) */
    ObjList m_route[YSS7_PCTYPE_COUNT];

private:
    Mutex m_l3userMutex;                 // Mutex to lock L3 user pointer
    SS7L3User* m_l3user;
    SS7PointCode::Type m_cpType[4];      // Map incoming MSUs net indicators to point code type
                                         // or the routing table of a message router
    unsigned int m_local[YSS7_PCTYPE_COUNT];
    unsigned char m_defNI;               // Default Network Indicator
};

/**
 * An interface to a Layer 4 (application) SS7 protocol
 * @short Abstract SS7 layer 4 (application) protocol
 */
class YSIG_API SS7Layer4 : public SS7L3User
{
public:
    /**
     * This method is called to clean up and destroy the object after the
     *  reference counter becomes zero
     */
    virtual void destroyed();

    /**
     * Initialize the application layer, connect it to the SS7 router
     * @param config Optional configuration parameters override
     * @return True if the application was initialized properly
     */
    virtual bool initialize(const NamedList* config);

    /**
     * Attach a SS7 network or router to this service. Detach itself from the old one if valid
     * @param network Pointer to network or router to attach
     */
    virtual void attach(SS7Layer3* network);

    /**
     * Retrieve the SS7 network or router to which this service is attached
     * @return Pointer to the network or router this service is attached to
     */
    inline SS7Layer3* network() const
	{ return m_layer3; }

    /**
     * Get the default sending Service Information Octet for this protocol
     * @return SIO value
     */
    inline unsigned char sio() const
	{ return m_sio; }

    /**
     * Get the Service Information Field (SS7 protocol number)
     * @return SIF value used in matching and sending MSUs
     */
    inline unsigned char sif() const
	{ return m_sio & 0x0f; }

    /**
     * Get the default sending Service Switching Function bits for this protocol
     * @return Combined Priority and Network Indicator bits
     */
    inline unsigned char ssf() const
	{ return m_sio & 0xf0; }

    /**
     * Get the default sending Priority bits for this protocol
     * @return Priority bits
     */
    inline unsigned char prio() const
	{ return m_sio & 0x30; }

    /**
     * Get the default sending Network Indicator bits for this protocol
     * @return Network Indicator bits
     */
    inline unsigned char ni() const
	{ return m_sio & 0xc0; }

    /**
     * Get a SIO value from a parameters list
     * @param params Parameter list to retrieve "service", "priority" and "netindicator"
     * @param sif Default Service Information Field to apply parameters to
     * @param prio Default Priority Field to apply parameters to
     * @param ni Default Network Indicator Field to apply parameters to
     * @return Adjusted SIO value
     */
    static unsigned char getSIO(const NamedList& params, unsigned char sif, unsigned char prio, unsigned char ni);

    /**
     * Get a SIO value from a parameters list
     * @param params Parameter list to retrieve "service", "priority" and "netindicator"
     * @param sif Default Service Information Field to apply parameters to
     * @param ssf Default Subservice Field to apply parameters to
     * @return Adjusted SIO value
     */
    static inline unsigned char getSIO(const NamedList& params, unsigned char sif, unsigned char ssf)
	{ return getSIO(params,sif,ssf & 0x30,ssf & 0xc0); }

    /**
     * Get a SIO value from a parameters list
     * @param params Parameter list to retrieve "service", "priority" and "netindicator"
     * @param sio Default SIO to apply parameters to
     * @return Adjusted SIO value
     */
    static inline unsigned char getSIO(const NamedList& params, unsigned char sio)
	{ return getSIO(params,sio & 0x0f,sio & 0x30,sio & 0xc0); }

    /**
     * Get a SIO value from a parameters list
     * @param params Parameter list to retrieve "service", "priority" and "netindicator"
     * @return Adjusted SIO value
     */
    inline unsigned char getSIO(const NamedList& params) const
	{ return getSIO(params,m_sio); }

protected:
    /**
     * Constructor
     * @param sio Default value of Service Information Octet
     * @param params Optional parameters to alter the value of SIO
     */
    SS7Layer4(unsigned char sio = SS7MSU::National, const NamedList* params = 0);

    /**
     * Ask the Layer 3 to push a Message Signal Unit down the protocol stack
     * @param msu Message data, starting with Service Indicator Octet
     * @param label Routing label of the MSU to use in routing
     * @param sls Signalling Link Selection, negative to choose best
     * @return Link the message was successfully queued to, negative for error
     */
    inline int transmitMSU(const SS7MSU& msu, const SS7Label& label, int sls = -1)
    {
	m_l3Mutex.lock();
	RefPointer<SS7Layer3> tmp = m_layer3;
	m_l3Mutex.unlock();
	return tmp ? tmp->transmitMSU(msu,label,sls) : -1;
    }

    /**
     * Service Information Octet (SIO) for this protocol
     */
    unsigned char m_sio;

private:
    Mutex m_l3Mutex;                     // Lock pointer use operations
    SS7Layer3* m_layer3;
};

/**
 * A message router between Transfer and Application layers.
 *  Messages are distributed according to the service type.
 * @short Main router for SS7 message transfer and applications
 */
class YSIG_API SS7Router : public SS7L3User, public SS7Layer3, public Mutex
{
    YCLASS2(SS7Router,SS7L3User,SS7Layer3)
public:
    /**
     * Control primitives
     */
    enum Operation {
	// stop MTP operation, disable the router
	Pause     = 0x100,
	// start router, restart MTP if needed
	Resume    = 0x200,
	// forcibly execute MTP restart
	Restart   = 0x300,
	// get operational status
	Status    = 0x400,
	// forcibly advertise availability to adjacent routes
	Traffic   = 0x500,
	// forcibly advertise available routes
	Advertise = 0x600,
    };

    /**
     * Default constructor
     * @param params The list with the parameters
     */
    SS7Router(const NamedList& params);

    /**
     * Destructor
     */
    virtual ~SS7Router();

    /**
     * Configure and initialize the router, maintenance and management
     * @param config Optional configuration parameters override
     * @return True if the router was initialized properly
     */
    virtual bool initialize(const NamedList* config);

    /**
     * Push a Message Signal Unit down the protocol stack
     * @param msu Message data, starting with Service Indicator Octet
     * @param label Routing label of the MSU to use in routing
     * @param sls Signalling Link Selection, negative to choose best
     * @return Link the message was successfully queued to, negative for error
     */
    virtual int transmitMSU(const SS7MSU& msu, const SS7Label& label, int sls = -1);

    /**
     * Check if the router is fully operational
     * @param sls Signalling Link to check, negative to check if any is operational
     * @return True if the router is enabled and operational
     */
    virtual bool operational(int sls = -1) const;

    /**
     * Initiate a MTP restart procedure
     * @return True if a restart was initiated
     */
    virtual bool restart();

    /**
     * Attach a SS7 Layer 3 (network) to the router. Attach the router to the given network
     * @param network Pointer to network to attach
     */
    virtual void attach(SS7Layer3* network);

    /**
     * Detach a SS7 Layer 3 (network) from the router. Detach the router from the given network
     * @param network Pointer to network to detach
     */
    virtual void detach(SS7Layer3* network);

    /**
     * Attach a SS7 Layer 4 (service) to the router. Attach itself to the service
     * @param service Pointer to service to attach
     */
    void attach(SS7Layer4* service);

    /**
     * Detach a SS7 Layer 4 (service) from the router. Detach itself from the service
     * @param service Pointer to service to detach
     */
    void detach(SS7Layer4* service);

    /**
     * Management request uninhibiting a signaling link
     * @param network SS7 Layer 3 owning the link to uninhibit
     * @param sls Signalink Link Selection
     * @param remote True to uninhibit the remote side of the link
     * @return True if an uninhibition request was sent
     */
    bool uninhibit(SS7Layer3* network, int sls, bool remote);

    /**
     * Set and clear inhibition flags on a link of an attached network
     * @param link Signalling Link to modify identified by a routing label
     * @param setFlags Flag bits to set ORed together
     * @param clrFlags Flag bits to clear  ORed together (optional)
     * @param notLast Do not apply inhibition to the last usable link
     * @return True if inhibition flags were set
     */
    bool inhibit(const SS7Label& link, int setFlags, int clrFlags = 0, bool notLast = false);

    /**
     * Check inhibition flags on a link of a router attached network
     * @param link Signalling Link to check identified by a routing label
     * @param flags Flag bits to check ORed together
     * @return True if any of the specified inhibition flags are set
     */
    bool inhibited(const SS7Label& link, int flags);

    /**
     * Get the sequence number of the last MSU received on a link
     * @param link Routing label identifying the link to retrieve the sequence from
     * @return Last FSN received, negative if not available
     */
    int getSequence(const SS7Label& link);

    /**
     * Remove the MSUs waiting in the transmit queue and return them
     * @param link Routing label identifying the link to recover MSUs
     * @param sequence First sequence number to recover, flush earlier packets
     */
    void recoverMSU(const SS7Label& link, int sequence);

    /**
     * Notification for receiving User Part Unavailable
     * @param type Type of Point Code
     * @param node Node on which the User Part is unavailable
     * @param part User Part (service) reported unavailable
     * @param cause Unavailability cause - Q.704 15.17.5
     * @param label Routing label of the UPU message
     * @param sls Signaling link the UPU was received on
     */
    virtual void receivedUPU(SS7PointCode::Type type, const SS7PointCode node,
	SS7MSU::Services part, unsigned char cause, const SS7Label& label, int sls);

    /**
     * Check if the transfer function and STP management is enabled
     * @return True if acting as a full STP
     */
    inline bool transfer() const
	{ return m_transfer; }

    /**
     * Check if the messages are transferred even if STP management may be disabled
     * @return True if messages are transferred between networks
     */
    inline bool transferring() const
	{ return m_transfer || m_transferSilent; }

    /**
     * Check if the MTP is restarting
     * @return True if MTP restart procedure is in progress
     */
    inline bool starting() const
	{ return !m_started; }

    /**
     * Get access to the Management component if available
     * @return A pointer to the SS7Management or NULL if not created
     */
    inline SS7Management* getManagement() const
	{ return m_mngmt; }

    /**
     * Get the Network Indicator bits that would match a Point Code type
     * @param pcType Point Code type to search for
     * @param defNI Default Network Indicator bits to use
     * @return Network Indicator bits matching the Point Code type
     */
    virtual unsigned char getNI(SS7PointCode::Type pcType, unsigned char defNI) const;

    /**
     * Retrieve the default local Point Code for a specific Point Code type
     * @param type Desired Point Code type
     * @return Packed local Point Code, zero if not set
     */
    virtual unsigned int getDefaultLocal(SS7PointCode::Type type) const;

protected:
    /**
     * Reset state of all routes of a network to Unknown
     */
    void clearView(const SS7Layer3* network);

    /**
     * Get the state of a route as seen from another network or adjacent point code
     * @param type Point Code type to search for
     * @param packedPC The packed point code whose state is viewed
     * @param remotePC The point code of an adjacent viewer, its network is skipped
     * @param network The network that will be not included in the view
     * @return State of the route as viewed from the specified point code or network
     */
    SS7Route::State getRouteView(SS7PointCode::Type type, unsigned int packedPC,
        unsigned int remotePC = 0, const SS7Layer3* network = 0);

    /**
     * Set the current state of a route by packed Point Code.
     * This method is thread safe
     * @param type Destination point code type
     * @param packedPC The packed point code
     * @param state The new state of the route
     * @param remotePC The point code that caused the route change
     * @param network The network that caused the route change
     * @return True if the route was found and its state changed
     */
    bool setRouteState(SS7PointCode::Type type, unsigned int packedPC, SS7Route::State state,
        unsigned int remotePC = 0, const SS7Layer3* network = 0);

    /**
     * Set the current state of a route by unpacked Point Code.
     * This method is thread safe
     * @param type Destination point code type
     * @param dest The destination point code
     * @param state The new state of the route
     * @param remotePC The point code that caused the route change
     * @param network The network that caused the route change
     * @return True if the route was found and its state changed
     */
    inline bool setRouteState(SS7PointCode::Type type, const SS7PointCode& dest, SS7Route::State state,
        unsigned int remotePC = 0, const SS7Layer3* network = 0)
	{ return setRouteState(type,dest.pack(type),state,remotePC,network); }

    /**
     * Load the default local Point Codes from a list of parameters
     * @param params List of parameters to load "local=" entries from
     */
    void loadLocalPC(const NamedList& params);

    /**
     * Periodical timer tick used to perform state transition and housekeeping
     * @param when Time to use as computing base for events and timeouts
     */
    virtual void timerTick(const Time& when);

    /**
     * Process a MSU received from the Layer 3 component
     * @param msu Message data, starting with Service Indicator Octet
     * @param label Routing label of the received MSU
     * @param network Network layer that delivered the MSU
     * @param sls Signalling Link the MSU was received from
     * @return Result of MSU processing
     */
    virtual HandledMSU receivedMSU(const SS7MSU& msu, const SS7Label& label, SS7Layer3* network, int sls);

    /**
     * Add a network to the routing table. Clear all its routes before appending it to the table
     * This method is thread safe
     * @param network The network to add to the routing table
     */
    void updateRoutes(SS7Layer3* network);

    /**
     * Remove the given network from all destinations in the routing table.
     * Remove the entry in the routing table if empty (no more routes to the point code).
     * This method is thread safe
     * @param network The network to remove
     */
    void removeRoutes(SS7Layer3* network);

    /**
     * Trigger the route changed notification for each route that is not Unknown
     * @param states Mask of required states of the route
     * @param onlyPC The only point code that should receive notifications
     */
    void notifyRoutes(SS7Route::State states = SS7Route::AnyState, unsigned int onlyPC = 0);

    /**
     * Trigger the route changed notification for each route that is not Unknown
     * @param states Mask of required states of the route
     * @param network Notify to adjacent nodes of this network
     */
    void notifyRoutes(SS7Route::State states, const SS7Layer3* network);

    /**
     * Notification callback when a route state changed to other than Unknown.
     * This method is called with the route mutex locked
     * @param route Pointer to the route whose state has changed
     * @param type Type of the pointcode of the route
     * @param remotePC The point code that caused the route change
     * @param network The network that caused the route change
     * @param onlyPC If set only advertise to this point code
     * @param forced Notify even if the route view didn't change
     */
    virtual void routeChanged(const SS7Route* route, SS7PointCode::Type type,
        unsigned int remotePC = 0, const SS7Layer3* network = 0,
        unsigned int onlyPC = 0, bool forced = false);

    /**
     * Process a notification generated by the attached network layer
     * @param network Network or linkset that generated the notification
     * @param sls Signallink Link that generated the notification, negative if none
     * @return True if notification was processed
     */
    virtual void notify(SS7Layer3* network, int sls);

    /**
     * Query or modify the management settings or operational parameters
     * @param params The list of parameters to query or change
     * @return True if the control operation was executed
     */
    virtual bool control(NamedList& params);

    /**
     * Detach management
     */
    virtual void destroyed();

    /** List of L3 (networks) attached to this router */
    ObjList m_layer3;
    /** List of L4 (services) attached to this router */
    ObjList m_layer4;
    /** Counter used to spot changes in the lists of L3 or L4 */
    int m_changes;
    /** Locally unhandled MSUs are to be routed to other networks */
    bool m_transfer;
    /** STP phase 2 of restart procedure in effect */
    bool m_phase2;
    /** MTP restart procedure has completed */
    bool m_started;
    /** MTP overall restart timer T20 */
    SignallingTimer m_restart;
    /** MTP isolation timer T1 */
    SignallingTimer m_isolate;

private:
    void restart2();
    void disable();
    void sendRestart(const SS7Layer3* network = 0);
    void sendRestart(SS7PointCode::Type type, unsigned int packedPC);
    void silentAllow(const SS7Layer3* network = 0);
    void silentAllow(SS7PointCode::Type type, unsigned int packedPC);
    void checkRoutes(const SS7Layer3* noResume = 0);
    void clearRoutes(SS7Layer3* network, bool ok);
    void reroute(const SS7Layer3* network);
    void rerouteCheck(const Time& when);
    void rerouteFlush();
    bool setRouteSpecificState(SS7PointCode::Type type, unsigned int packedPC,
	unsigned int srcPC, SS7Route::State state, const SS7Layer3* changer = 0);
    inline bool setRouteSpecificState(SS7PointCode::Type type, const SS7PointCode& dest,
	const SS7PointCode&src, SS7Route::State state, const SS7Layer3* changer = 0)
	{ return setRouteSpecificState(type,dest.pack(type),src.pack(type),state,changer); }
    void sendRouteTest();
    int routeMSU(const SS7MSU& msu, const SS7Label& label, SS7Layer3* network, int sls, SS7Route::State states);
    void buildView(SS7PointCode::Type type, ObjList& view, SS7Layer3* network);
    void buildViews();
    void printStats();
    Mutex m_statsMutex;
    SignallingTimer m_trafficOk;
    SignallingTimer m_trafficSent;
    SignallingTimer m_routeTest;
    bool m_testRestricted;
    bool m_transferSilent;
    bool m_checkRoutes;
    bool m_autoAllowed;
    bool m_sendUnavail;
    bool m_sendProhibited;
    unsigned long m_rxMsu;
    unsigned long m_txMsu;
    unsigned long m_fwdMsu;
    unsigned long m_failMsu;
    unsigned long m_congestions;
    SS7Management* m_mngmt;
};

/**
 * RFC4165 SS7 Layer 2 implementation over SCTP/IP.
 * M2PA is intended to be used as a symmetrical Peer-to-Peer replacement of
 *  a hardware based SS7 data link.
 * @short SIGTRAN MTP2 User Peer-to-Peer Adaptation Layer
 */
class YSIG_API SS7M2PA : public SS7Layer2, public SIGTRAN
{
    YCLASS(SS7M2PA,SS7Layer2)
public:
    enum m2paState {
	Alignment = 1,
	ProvingNormal = 2,
	ProvingEmergency = 3,
	Ready = 4,
	ProcessorOutage = 5,
	ProcessorRecovered = 6,
	Busy = 7,
	BusyEnded = 8,
	OutOfService = 9,
    };

    enum msgType {
	UserData = 1,
	LinkStatus = 2
    };

    enum sctpState {
	Idle,
	Associating,
	Established
    };

    enum M2PAOperations {
	Pause        = SS7Layer2::Pause,
	// start link operation, align if it needs to
	Resume       = SS7Layer2::Resume,
	// start link, force realignment
	Align        = SS7Layer2::Align,
	// get operational status
	Status       = SS7Layer2::Status,
	// restart transport layer
	TransRestart = 0x500
    };

    /**
     * Constructor
     */
    SS7M2PA(const NamedList& params);

    /**
     * Destructor
     */
    ~SS7M2PA();

    /**
     * Configure and initialize M2PA and its transport
     * @param config Optional configuration parameters override
     * @return True if M2PA and the transport were initialized properly
     */
    virtual bool initialize(const NamedList* config);

     /**
     * Query or modify layer's settings or operational parameters
     * @param params The list of parameters to query or change
     * @return True if the control operation was executed
     */
    virtual bool control(NamedList& params);

    /**
     * Execute a control operation. Operations can change the link status or
     *  can query the aligned status.
     * @param oper Operation to execute
     * @param params Optional parameters for the operation
     * @return True if the command completed successfully, for query operations
     *  also indicates the data link is aligned and operational
     */
    virtual bool control(M2PAOperations oper, NamedList* params = 0);

    /**
     * Execute a control operation. Operations can change the link status or
     *  can query the aligned status.
     * @param oper Operation to execute
     * @param params Optional parameters for the operation
     * @return True if the command completed successfully, for query operations
     *  also indicates the data link is aligned and operational
     */
    virtual bool control(SS7Layer2::Operation oper, NamedList* params = 0)
	{ return control((M2PAOperations)oper,params); }

    /**
     * Retrieve the current link status indications
     * @return Link status indication bits
     */
    virtual unsigned int status() const;

    /**
     * Push a Message Signal Unit down the protocol stack
     * @param msu MSU data to transmit
     * @return True if message was successfully queued
     */
    virtual bool transmitMSU(const SS7MSU& msu);

    /**
     * Method called when the transport status has been changed
     * @param status Up or down
     */
    virtual void notifyLayer(SignallingInterface::Notification status);

    /**
     * Remove the MSUs waiting in the transmit queue and return them
     * @param sequence First sequence number to recover, flush earlier packets
     */
    virtual void recoverMSU(int sequence);

    /**
     * Decode sequence numbers from message and process them
     * @param data The message
     * @param msgType The message type
     * @return True if sequence numbers ar as we expected to be
     */
    bool decodeSeq(const DataBlock& data, u_int8_t msgType);

    /**
     * Helper method called when an error was detected
     * Change state to OutOfService and notifys upper layer
     * @param info Debuging purpose, Information about detected error
     */
    void abortAlignment(const char* info = 0);

    /**
     * Send link status message to inform the peer about ouer curent state
     * @param streamId The id of the stream who should send the message
     */
    void transmitLS(int streamId = 0);

    /**
     * Create M2PA header (sequence numbers)
     * @param data The data where the header will be stored
     */
    void setHeader(DataBlock& data);

    /**
     * Decode and process link status message
     * @param data The message
     * @param streamId The stream id witch received the message
     * @return True if the message was procesed
     */
    bool processLinkStatus(DataBlock& data, int streamId);

    /**
     * Decode and process link status message in more strict manner
     * @param data The message
     * @param streamId The stream id witch received the message
     * @return True if the message was procesed
     */
    bool processSLinkStatus(DataBlock& data, int streamId);

    /**
     * Helper method used to acknowledge the last received message
     * when no data are to transmit
     */
    void sendAck();

    /**
     * Remove a frame from acknowledgement list
     * @param bsn The sequence number of the frame to be removed
     * @return True if the frame was found and removed.
     */
    bool removeFrame(u_int32_t bsn);

    /**
     * Check if a sequence number may be a valid next BSN
     * @param bsn Backward Sequence Number to check
     * @return True if the provided BSN is in the valid range
     */
    bool nextBsn(u_int32_t bsn) const;

    /**
     * Increment the given sequence number
     * @param nr Reference of the number to increment
     * @return The incremented number
     */
    static inline u_int32_t increment(u_int32_t& nr)
	{ return (nr == 0xffffff) ? (nr = 0) : nr++; }

    /**
     * Obtain next sequence number
     * @param nr The current sequence number
     * @return The next number in sequence
     */
    static inline u_int32_t getNext(u_int32_t nr)
	{ return (nr == 0xffffff) ? 0 : nr + 1; }

protected:

    /**
     * Periodical timer tick used to perform alignment and housekeeping
     * @param when Time to use as computing base for events and timeouts
     */
    virtual void timerTick(const Time& when);

    /**
     * Check if the link is aligned.
     * The link may not be operational, the other side may be still proving.
     * @return True if the link is aligned
     */
    virtual bool aligned() const;

    /**
     * Check if the link is aligned and operational
     * @return True if the link is operational
     */
    virtual bool operational() const;

    /**
     * Process a complete message
     * @param msgVersion Version of the protocol
     * @param msgClass Class of the message
     * @param msgType Type of the message, depends on the class
     * @param msg Message data, may be empty
     * @param streamId Identifier of the stream the message was received on
     * @return True if the message was handled
     */
    virtual bool processMSG(unsigned char msgVersion, unsigned char msgClass,
	unsigned char msgType, const DataBlock& msg, int streamId);

    /**
     * Initiates alignment and proving procedure
     * @param emergency True if emergency alignment is desired
     */
    void startAlignment(bool emergency = false);

    /**
     * Retransmit unacknowledged data
     */
     void retransData();

     virtual void destroyed();
private:
    void dumpMsg(u_int8_t version, u_int8_t mClass, u_int8_t type,
	const DataBlock& data, int stream, bool send);
    void setLocalStatus(unsigned int status);
    void setRemoteStatus(unsigned int status);
    u_int32_t m_seqNr;
    u_int32_t m_needToAck;
    u_int32_t m_lastAck;
    u_int32_t m_confCounter;
    u_int32_t m_maxUnack;
    u_int32_t m_maxQueueSize;
    unsigned int m_localStatus;
    unsigned int m_state;
    unsigned int m_remoteStatus;
    unsigned int m_transportState;
    unsigned int m_connFailCounter;
    unsigned int m_connFailThreshold;
    Mutex m_mutex;
    ObjList m_ackList;
    SignallingTimer m_t1;
    SignallingTimer m_t2;
    SignallingTimer m_t3;
    SignallingTimer m_t4;
    SignallingTimer m_ackTimer;
    SignallingTimer m_confTimer;
    SignallingTimer m_oosTimer;
    SignallingTimer m_waitOosTimer;
    SignallingTimer m_connFailTimer;
    bool m_autostart;
    bool m_sequenced;
    bool m_dumpMsg;
};

/**
 * The common client side of SIGTRAN SS7 MTP2 User Adaptation (RFC3331)
 * @short Client side of SIGTRAN SS7 MTP2 UA
 */
class YSIG_API SS7M2UAClient : public SIGAdaptClient
{
    YCLASS(SS7M2UAClient,SIGAdaptClient)
public:
    /**
     * Contructor of an empty IUA client
     */
    inline SS7M2UAClient(const NamedList& params)
	: SIGAdaptClient(params.safe("SS7M2UAClient"),&params,2,2904)
	{ }
    virtual bool processMSG(unsigned char msgVersion, unsigned char msgClass,
	unsigned char msgType, const DataBlock& msg, int streamId);
};

/**
 * RFC3331 SS7 Layer 2 implementation over SCTP/IP.
 * M2UA is intended to be used as a Provider-User where real MTP2 runs on a
 *  Signalling Gateway and MTP3 runs on an Application Server.
 * @short SIGTRAN MTP2 User Adaptation Layer
 */
class YSIG_API SS7M2UA : public SS7Layer2, public SIGAdaptUser
{
    friend class SS7M2UAClient;
    YCLASS(SS7M2UA,SS7Layer2)
public:
    /**
     * Constructor
     * @param params List of construction parameters
     */
    SS7M2UA(const NamedList& params);

    /**
     * Configure and initialize M2UA and its transport
     * @param config Optional configuration parameters override
     * @return True if M2UA and the transport were initialized properly
     */
    virtual bool initialize(const NamedList* config);

    /**
     * Execute a control operation. Operations can change the link status or
     *  can query the aligned status.
     * @param oper Operation to execute
     * @param params Optional parameters for the operation
     * @return True if the command completed successfully, for query operations
     *  also indicates the data link is aligned and operational
     */
    virtual bool control(Operation oper, NamedList* params = 0);

    /**
     * Retrieve the current link status indications
     * @return Link status indication bits
     */
    virtual unsigned int status() const;

    /**
     * Push a Message Signal Unit down the protocol stack
     * @param msu Message data, starting with Service Indicator Octet
     * @return True if message was successfully queued
     */
    virtual bool transmitMSU(const SS7MSU& msu);

    /**
     * Remove the MSUs waiting in the transmit queue and return them
     * @param sequence First sequence number to recover, flush earlier packets
     */
    virtual void recoverMSU(int sequence);

    /**
     * Check if the link is fully operational
     * @return True if the link is aligned and operational
     */
    virtual bool operational() const;

    /**
     * Get the sequence number of the last MSU received, request if not available
     * @return Last FSN received, negative if not available
     */
    virtual int getSequence();

    /**
     * Traffic activity state change notification
     * @param active True if the ASP is active and traffic is allowed
     */
    virtual void activeChange(bool active);

    /**
     * Retrieve the numeric Interface Identifier (if any)
     * @return IID value, -1 if not set
     */
    inline int32_t iid() const
	{ return m_iid; }

protected:
    enum LinkState {
	LinkDown,
	LinkReq,
	LinkReqEmg,
	LinkUp,
	LinkUpEmg,
    };

    /**
     * Periodical timer tick used to perform alignment and housekeeping
     * @param when Time to use as computing base for events and timeouts
     */
    virtual void timerTick(const Time& when);

    SS7M2UAClient* client() const
	{ return static_cast<SS7M2UAClient*>(adaptation()); }
    virtual bool processMGMT(unsigned char msgType, const DataBlock& msg, int streamId);
    virtual bool processMAUP(unsigned char msgType, const DataBlock& msg, int streamId);
    void postRetrieve();
    SignallingTimer m_retrieve;
    int32_t m_iid;
    int m_linkState;
    bool m_rpo;
    bool m_longSeq;
};

/**
 * RFC3332 SS7 Layer 3 implementation over SCTP/IP.
 * M3UA is intended to be used as a Provider-User where real MTP3 runs on a
 *  Signalling Gateway and MTP users are located on an Application Server.
 * @short SIGTRAN MTP3 User Adaptation Layer
 */
class YSIG_API SS7M3UA : public SS7Layer3, public SIGAdaptUser
{
    YCLASS(SS7M3UA,SS7Layer3)
};

/**
 * Q.703 SS7 Layer 2 (Data Link) implementation on top of a hardware interface
 * @short SS7 Layer 2 implementation on top of a hardware interface
 */
class YSIG_API SS7MTP2 : public SS7Layer2, public SignallingReceiver, public SignallingDumpable, public Mutex
{
    YCLASS2(SS7MTP2,SS7Layer2,SignallingReceiver)
public:
    /**
     * Types of error correction
     */
    enum ErrorCorrection {
	Basic,       // retransmits only based on sequence numbers
	Preventive,  // continuously retransmit unacknowledged packets
	Adaptive,    // switch to using preventive retransmission dynamically
    };

    /**
     * Constructor
     * @param params Layer's parameters
     * @param status Initial status
     */
    SS7MTP2(const NamedList& params, unsigned int status = OutOfService);

    /**
     * Destructor
     */
    virtual ~SS7MTP2();

    /**
     * Configure and initialize MTP2 and its interface
     * @param config Optional configuration parameters override
     * @return True if MTP2 and the interface were initialized properly
     */
    virtual bool initialize(const NamedList* config);

    /**
     * Push a Message Signal Unit down the protocol stack
     * @param msu MSU data to transmit
     * @return True if message was successfully queued
     */
    virtual bool transmitMSU(const SS7MSU& msu);

    /**
     * Remove the MSUs waiting in the transmit queue and return them
     * @param sequence First sequence number to recover, flush earlier packets
     */
    virtual void recoverMSU(int sequence);

    /**
     * Retrieve the current link status indications
     * @return Link status indication bits
     */
    virtual unsigned int status() const;

    /**
     * Check if the link is aligned.
     * The link may not be operational, the other side may be still proving.
     * @return True if the link is aligned
     */
    virtual bool aligned() const;

    /**
     * Check if the link is aligned and operational
     * @return True if the link is operational
     */
    virtual bool operational() const;

    /**
     * Execute a control operation. Operations can change the link status or
     *  can query the aligned status.
     * @param oper Operation to execute
     * @param params Optional parameters for the operation
     * @return True if the command completed successfully, for query operations
     *  also indicates the data link is aligned and operational
     */
    virtual bool control(Operation oper, NamedList* params = 0);

    /**
     * Process a notification generated by the attached interface
     * @param event Notification event reported by the interface
     * @return True if notification was processed
     */
    virtual bool notify(SignallingInterface::Notification event);

protected:
    /**
     * Remove all attachements. Disposes the memory
     */
    virtual void destroyed()
    {
	SS7Layer2::attach(0);
	TelEngine::destruct(SignallingReceiver::attach(0));
	SignallingComponent::destroyed();
    }

    /**
     * Periodical timer tick used to perform alignment and housekeeping
     * @param when Time to use as computing base for events and timeouts
     */
    virtual void timerTick(const Time& when);

    /**
     * Process a Signalling Packet received by the hardware interface
     * @return True if message was successfully processed
     */
    virtual bool receivedPacket(const DataBlock& packet);

    /**
     * Process a received Fill-In Signal Unit
     */
    virtual void processFISU();

    /**
     * Process a received Link Status Signal Unit
     * @param status Link status indications
     */
    virtual void processLSSU(unsigned int status);

    /**
     * Push a Link Status Signal Unit down the protocol stack
     * @param status Link status indications
     * @return True if message was successfully queued
     */
    bool transmitLSSU(unsigned int status);

    /**
     * Push a Link Status Signal Unit with the current status down the protocol stack
     * @return True if message was successfully queued
     */
    inline bool transmitLSSU()
	{ return transmitLSSU(m_lStatus); }

    /**
     * Push a Fill-In Signal Unit down the protocol stack
     * @return True if message was successfully queued
     */
    bool transmitFISU();

    /**
     * Initiates alignment and proving procedure
     * @param emergency True if emergency alignment is desired
     */
    void startAlignment(bool emergency = false);

    /**
     * Abort an alignment procedure if link errors occur
     * @param retry Keep trying to align
     */
    void abortAlignment(bool retry = true);

    /**
     * Start the link proving period
     * @return True if proving period was started
     */
    bool startProving();

private:
    virtual bool control(NamedList& params)
	{ return SignallingDumpable::control(params,this) || SS7Layer2::control(params); }
    void unqueueAck(unsigned char bsn);
    bool txPacket(const DataBlock& packet, bool repeat, SignallingInterface::PacketType type = SignallingInterface::Unknown);
    void setLocalStatus(unsigned int status);
    void setRemoteStatus(unsigned int status);
    // sent but yet unacknowledged packets
    ObjList m_queue;
    // data link status (alignment) - desired, local and remote
    unsigned int m_status, m_lStatus, m_rStatus;
    // various interval period end
    u_int64_t m_interval;
    // time when resending packets
    u_int64_t m_resend;
    // time when aborting resending packets
    u_int64_t m_abort;
    // time when need to transmit next FISU/LSSU
    u_int64_t m_fillTime;
    // remote congestion indicator
    bool m_congestion;
    // backward and forward sequence numbers
    unsigned char m_bsn, m_fsn;
    // backward and forward indicator bits
    bool m_bib, m_fib;
    // last forward sequence number we sent a retransmission request
    unsigned char m_lastFsn;
    // last received backward sequence number
    unsigned char m_lastBsn;
    // last received backward indicator bit
    bool m_lastBib;
    // count of errors
    unsigned int m_errors;
    // maximum accepted errors
    unsigned int m_maxErrors;
    // packet resend interval
    unsigned int m_resendMs;
    // packet resend abort interval
    unsigned int m_abortMs;
    // FISU/LSSU soft resend interval
    unsigned int m_fillIntervalMs;
    // fill link with end-to-end FISU/LSSU
    bool m_fillLink;
    // automatically align on resume
    bool m_autostart;
    // flush MSUs after aligning
    bool m_flushMsus;
};

/**
 * Q.704 SS7 Layer 3 (Network) implementation on top of SS7 Layer 2
 * @short SS7 Layer 3 implementation on top of Layer 2
 */
class YSIG_API SS7MTP3 : public SS7Layer3, public SS7L2User, public SignallingDumpable, public Mutex
{
    YCLASS(SS7MTP3,SS7Layer3)
public:
    /**
     * Control primitives
     */
    enum Operation {
	// take linkset out of service
	Pause   = 0x100,
	// start linkset operation
	Resume  = 0x200,
	// force a MTP restart procedure
	Restart = 0x300,
	// get operational status
	Status  = 0x400,
    };

    /**
     * Constructor
     * @param params Layer's parameters
     */
    SS7MTP3(const NamedList& params);

    /**
     * Destructor
     */
    virtual ~SS7MTP3();

    /**
     * Configure and initialize the MTP3 and all its links
     * @param config Optional configuration parameters override
     * @return True if MTP3 and at least one link were initialized properly
     */
    virtual bool initialize(const NamedList* config);

    /**
     * Push a Message Signal Unit down the protocol stack
     * @param msu Message data, starting with Service Indicator Octet
     * @param label Routing label of the MSU used in routing
     * @param sls Signalling Link Selection, negative to choose best
     * @return Link the message was successfully queued to, negative for error
     */
    virtual int transmitMSU(const SS7MSU& msu, const SS7Label& label, int sls = -1);

    /**
     * Check if the network/linkset is fully operational
     * @param sls Signalling Link to check, negative to check if any is operational
     * @return True if the linkset is enabled and operational
     */
    virtual bool operational(int sls = -1) const;

    /**
     * Retrieve inhibition flags of a specific link
     * @param sls Signalling Link to check
     * @return Inhibitions of the specified link, zero if not inhibited
     */
    virtual int inhibited(int sls) const;

    /**
     * Set and clear inhibition flags on the links
     * @param sls Signalling Link to modify
     * @param setFlags Flag bits to set ORed together
     * @param clrFlags Flag bits to clear  ORed together (optional)
     * @return True if inhibition flags were set
     */
    virtual bool inhibit(int sls, int setFlags, int clrFlags = 0);

    /**
     * Get the current congestion level of a link
     * @param sls Signalling Link to check for congestion, -1 for maximum
     * @return Congestion level, 0 if not congested, 3 if maximum congestion
     */
    virtual unsigned int congestion(int sls);

    /**
     * Get the sequence number of the last MSU received on a link
     * @param sls Signalling Link to retrieve MSU number from
     * @return Last FSN received, negative if not available
     */
    virtual int getSequence(int sls) const;

    /**
     * Remove the MSUs waiting in the transmit queue and return them
     * @param sls Signalling Link to recover MSUs from
     * @param sequence First sequence number to recover, flush earlier packets
     */
    virtual void recoverMSU(int sls, int sequence);

    /**
     * Execute a control operation on the linkset
     * @param oper Operation to execute
     * @param params Optional parameters for the operation
     * @return True if the command completed successfully, for query operations
     *  also indicates the linkset is enabled and operational
     */
    virtual bool control(Operation oper, NamedList* params = 0);

    /**
     * Attach a SS7 Layer 2 (data link) to the network transport. Attach itself to the link
     * @param link Pointer to data link to attach
     */
    virtual void attach(SS7Layer2* link);

    /**
     * Detach a SS7 Layer 2 (data link) from the network transport. Remove the link's L2 user
     * @param link Pointer to data link to detach
     */
    virtual void detach(SS7Layer2* link);

    /**
     * Query or modify layer's settings or operational parameters
     * @param params The list of parameters to query or change
     * @return True if the control operation was executed
     */
    virtual bool control(NamedList& params);

    /**
     * Check if access to a specific Point Code is allowed from this network
     * @param type Destination point code type
     * @param packedPC The destination point code
     * @return True if access to the specified Point Code is allowed
     */
    virtual bool allowedTo(SS7PointCode::Type type, unsigned int packedPC) const;

    /**
     * Get the total number of links attached
     * @return Number of attached data links
     */
    inline unsigned int linksTotal() const
	{ return m_total; }

    /**
     * Get the number of links that are checked by maintenance
     * @return Number of MTN checked data links
     */
    inline unsigned int linksChecked() const
	{ return m_checked; }

    /**
     * Get the number of links that are currently operational
     * @return Number of operational data links
     */
    inline unsigned int linksActive() const
	{ return m_active; }

    /**
     * Get a list of the links held by this linkset
     * @return A list containing the links
     */
    inline const ObjList* links() const
	{ return &m_links; }

protected:
    /**
     * Detach all links and user. Destroys the object, disposes the memory
     */
    virtual void destroyed();

    /**
     * Periodical timer tick used to perform housekeeping and link checking
     * @param when Time to use as computing base for events and timeouts
     */
    virtual void timerTick(const Time& when);

    /**
     * Callback called from maintenance when valid SLTA or SLTM are received
     * @param sls Link that was checked by maintenance
     * @param remote True if remote checked the link, false if local success
     */
    virtual void linkChecked(int sls, bool remote);

    /**
     * Check if we should answer with SLTA to received SLTM in maintenance()
     * @return True to send a SLTA for each good received SLTM
     */
    virtual bool responder() const
	{ return !m_inhibit; }

    /**
     * Process a MSU received from the Layer 2 component
     * @param msu Message data, starting with Service Indicator Octet
     * @param link Data link that delivered the MSU
     * @param sls Signalling Link the MSU was received from
     * @return True if the MSU was processed
     */
    virtual bool receivedMSU(const SS7MSU& msu, SS7Layer2* link, int sls);

    /**
     * Process a MSU recovered from the Layer 2 component after failure
     * @param msu Message data, starting with Service Indicator Octet
     * @param link Data link from where the MSU was recovered
     * @param sls Signalling Link the MSU was recovered from
     * @return True if the MSU was processed
     */
    virtual bool recoveredMSU(const SS7MSU& msu, SS7Layer2* link, int sls);

    /**
     * Process a notification generated by the attached data link
     * @param link Data link that generated the notification
     * @return True if notification was processed
     */
    virtual void notify(SS7Layer2* link);

    /**
     * Count the total and active number of links
     * @return Number of active links
     */
    unsigned int countLinks();

private:
    ObjList m_links;
    // total links in linkset
    unsigned int m_total;
    // checked links in linkset
    unsigned int m_checked;
    // currently active links
    unsigned int m_active;
    // SLS to SLC shift
    bool m_slcShift;
    // inhibited flag
    bool m_inhibit;
    // warn if all links are down
    bool m_warnDown;
    // check the links before placing them in service
    bool m_checklinks;
    // realign links that don't respond to test messages anymore
    bool m_forcealign;
    // maintenance check intervals (Q.707)
    u_int64_t m_checkT1;
    u_int64_t m_checkT2;
    // list of allowed point codes seen from this network
    unsigned int* m_allowed[YSS7_PCTYPE_COUNT];
};

/**
 * Decoded Signalling Network Management (SNM) User Part message
 * @short SNM signalling message
 */
class YSIG_API SS7MsgSNM : public SignallingMessage
{
public:
    /**
     * SNM Message type as defined by Q.704 Table 1
     */
    enum Type {
	Unknown = 0,
	COO  = 0x11, // Changeover Order signal
	ECO  = 0x12, // Emergency Changeover Order signal
	RCT  = 0x13, // Route Set Congestion Test signal
	TFP  = 0x14, // Transfer Prohibited signal
	RST  = 0x15, // Route Set Test for prohibited destination
	RSP  = RST,  // Route Set Test for prohibited destination (ANSI)
	LIN  = 0x16, // Link Inhibit signal
	TRA  = 0x17, // Traffic Restart Allowed signal
	DLC  = 0x18, // Data Link Connection Order signal
	UPU  = 0x1a, // User Part Unavailable signal
	COA  = 0x21, // Changeover Acknowledgment signal
	ECA  = 0x22, // Emergency Changeover Acknowledgment signal
	TFC  = 0x23, // Transfer Controlled signal
	TCP  = 0x24, // Transfer Cluster Prohibited
	TFPA = TCP,  // Transfer Prohibited Acknowledgment (Yellow Book only)
	RSR  = 0x25, // Route Set Test for prohibited destination (national use)
	LUN  = 0x26, // Link Uninhibit signal
	TRW  = 0x27, // Traffic Restart Waiting (ANSI only)
	CSS  = 0x28, // Connection Successful signal
	XCO  = 0x31, // Extended Changeover Order signal
	TFR  = 0x34, // Transfer Restricted signal (national use)
	RCP  = 0x35, // Route Set Test for cluster-prohibited
	LIA  = 0x36, // Link Inhibit Acknowledgment signal
	CNS  = 0x38, // Connection Not Successful signal
	XCA  = 0x41, // Extended Changeover Acknowledgment signal
	TCR  = 0x44, // Transfer Cluster Restricted signal (ANSI only)
	RCR  = 0x45, // Route Set Test for cluster-restricted (ANSI only)
	LUA  = 0x46, // Link Uninhibit Acknowledgment signal
	CNP  = 0x48, // Connection Not Possible signal
	CBD  = 0x51, // Changeback Declaration signal
	TFA  = 0x54, // Transfer Allowed signal
	LID  = 0x56, // Link Inhibit Denied signal
	CBA  = 0x61, // Changeback Acknowledgment signal
	TCA  = 0x64, // Transfer Cluster Allowed
	TFAA = TCA,  // Transfer Allowed Acknowledgment (Yellow Book only)
	LFU  = 0x66, // Link Forced Uninhibit signal
	LLT  = 0x76, // Link Local Inhibit Test signal
	LLI  = LLT,  // Link Local Inhibit Test signal (ANSI)
	LRT  = 0x86, // Link Remote Inhibit Test signal
	LRI  = LRT,  // Link Remote Inhibit Test signal (ANSI)
    };

    /**
     * SNM Message group (H0) as defined by Q.704 15.3
     */
    enum Group {
	CHM = 0x01,  // Changeover and changeback
	ECM = 0x02,  // Emergency changeover
	FCM = 0x03,  // Transfer controlled and signalling route set congestion
	TFM = 0x04,  // Transfer prohibited/allowed/restricted
	RSM = 0x05,  // Signalling route/set/test
	MIM = 0x06,  // Management inhibit
	TRM = 0x07,  // Traffic restart allowed
	DLM = 0x08,  // Signalling data/link/connection
	UFC = 0x0a,  // User part flow control
    };

    /**
     * Constructor
     * @param type Message type
     */
    SS7MsgSNM(unsigned char type);

    /**
     * Get the type of this message
     * @return The type of this message
     */
    inline unsigned char type() const
	{ return m_type; }

    /**
     * Get the group this message belongs to
     * @return This message's group
     */
    inline unsigned char group() const
	{ return m_type & 0x0f; }

    /**
     * Fill a string with this message's parameters for debug purposes
     * @param dest The destination string
     * @param label The routing label
     * @param params True to add parameters
     */
    void toString(String& dest, const SS7Label& label, bool params) const;

    /**
     * Parse a received buffer and build a message from it
     * @param receiver The SS7 management entity that received the MSU
     * @param type Message type
     * @param pcType The point code type contained in received MSU's label
     * @param buf Buffer after message head
     * @param len Buffer length
     * @return Valid message pointer of 0 on failure
     */
    static SS7MsgSNM* parse(SS7Management* receiver, unsigned char type,
	SS7PointCode::Type pcType,
	const unsigned char* buf, unsigned int len);

    /**
     * Get the dictionary containing the names of the message type
     */
    static const TokenDict* names();

    /**
     * Convert a SNM message type to a C string
     * @param type Type of SNM message to look up
     * @param defvalue Default string to return
     * @return Name of the SNM message type
     */
    static inline const char* lookup(Type type, const char* defvalue = 0)
	{ return TelEngine::lookup(type,names(),defvalue); }

    /**
     * Look up a SNM message name
     * @param name String name of the SNM message
     * @param defvalue Default type to return
     * @return Encoded type of the SNM message
     */
    static inline Type lookup(const char* name, Type defvalue = Unknown)
	{ return static_cast<Type>(TelEngine::lookup(name,names(),defvalue)); }

private:
    unsigned char m_type;
};

/**
 * Decoded Maintenance (MTN) User Part message
 * @short MTN signalling message
 */
class YSIG_API SS7MsgMTN
{
public:
    /**
     * MTN Message type as defined by Q.707 5.4
     */
    enum Type {
	Unknown = 0,
	SLTM = 0x11, // Signalling Link Test Message
	SLTA = 0x21, // Signalling Link Test Acknowledgment
    };

    static const TokenDict* names();

    /**
     * Convert a MTN message type to a C string
     * @param type Type of MTN message to look up
     * @param defvalue Default string to return
     * @return Name of the MTN message type
     */
    static inline const char* lookup(Type type, const char* defvalue = 0)
	{ return TelEngine::lookup(type,names(),defvalue); }

    /**
     * Look up a MTN message name
     * @param name String name of the MTN message
     * @param defvalue Default type to return
     * @return Encoded type of the MTN message
     */
    static inline Type lookup(const char* name, Type defvalue = Unknown)
	{ return static_cast<Type>(TelEngine::lookup(name,names(),defvalue)); }
};

/**
 * Decoded ISDN User Part message
 * @short ISUP signalling message
 */
class YSIG_API SS7MsgISUP : public SignallingMessage
{
    YCLASS(SS7MsgISUP,SignallingMessage)
    friend class SS7ISUPCall;
public:
    /**
     * ISUP Message type as defined by Q.762 Table 2 and Q.763 Table 4
     */
    enum Type {
	Unknown = 0,
	IAM  = 0x01, // Initial Address Message
	SAM  = 0x02, // Subsequent Address Message
	INR  = 0x03, // Information Request (national use)
	INF  = 0x04, // Information (national use)
	COT  = 0x05, // Continuity
	ACM  = 0x06, // Address Complete Message
	CON  = 0x07, // Connect
	FOT  = 0x08, // Forward Transfer
	ANM  = 0x09, // Answer Message
	REL  = 0x0c, // Release Request
	SUS  = 0x0d, // Suspend
	RES  = 0x0e, // Resume
	RLC  = 0x10, // Release Complete
	CCR  = 0x11, // Continuity Check Request
	RSC  = 0x12, // Reset Circuit
	BLK  = 0x13, // Blocking
	UBL  = 0x14, // Unblocking
	BLA  = 0x15, // Blocking Acknowledgement
	UBA  = 0x16, // Unblocking Acknowledgement
	GRS  = 0x17, // Circuit Group Reset
	CGB  = 0x18, // Circuit Group Blocking
	CGU  = 0x19, // Circuit Group Unblocking
	CGA  = 0x1a, // Circuit Group Blocking Acknowledgement
	CGBA = CGA,
	CUA  = 0x1b, // Circuit Group Unblocking Acknowledgement
	CMR  = 0x1c, // Call Modification Request (ANSI only)
	CMC  = 0x1d, // Call Modification Completed (ANSI only)
	CMRJ = 0x1e, // Call Modification Rejected (ANSI only)
	FACR = 0x1f, // Facility Request
	FAA  = 0x20, // Facility Accepted
	FRJ  = 0x21, // Facility Reject
	FAD  = 0x22, // Facility Deactivated (ANSI only)
	FAI  = 0x23, // Facility Information (ANSI only)
	LPA  = 0x24, // Loopback Acknowledgement (national use)
	CSVR = 0x25, // CUG Selection and Validation Request (ANSI only)
	CSVS = 0x26, // CUG Selection and Validation Response (ANSI only)
	DRS  = 0x27, // Delayed Release (ANSI only)
	PAM  = 0x28, // Pass Along Message (national use)
	GRA  = 0x29, // Circuit Group Reset Acknowledgement
	CQM  = 0x2a, // Circuit Group Query (national use)
	CQR  = 0x2b, // Circuit Group Query Response (national use)
	CPR  = 0x2c, // Call Progress
	CPG  = CPR,
	USR  = 0x2d, // User-to-User Information
	UEC  = 0x2e, // Unequipped CIC (national use)
	UCIC = UEC,
	CNF  = 0x2f, // Confusion
	OLM  = 0x30, // Overload Message (national use)
	CRG  = 0x31, // Charge Information (national use and format, ITU only)
	NRM  = 0x32, // Network Resource Management
	FAC  = 0x33, // Facility (national use)
	UPT  = 0x34, // User Part Test
	UPA  = 0x35, // User Part Available
	IDR  = 0x36, // Identification Request (ITU only)
	IRS  = 0x37, // Identification Response (ITU only)
	SGM  = 0x38, // Segmentation
	LOP  = 0x40, // Loop Prevention
	APM  = 0x41, // Application Transport
	PRI  = 0x42, // Pre-Release Information
	SDN  = 0x43, // Subsequent Directory Number (national use)
	CRA  = 0xe9, // Circuit Reservation Acknowledgement (ANSI only)
	CRM  = 0xea, // Circuit Reservation (ANSI only)
	CVR  = 0xeb, // Circuit Validation Response (ANSI only)
	CVT  = 0xec, // Circuit Validation Test (ANSI only)
	EXM  = 0xed, // Exit Message (ANSI only)
	// Dummy, used for various purposes
	CtrlSave = 256, // control, save circuits
	CtrlCicEvent,   // control, generate circuit events, debug only
    };

    /**
     * ISUP Message type as defined by Q.763 Table 5
     */
    enum Parameters {
	EndOfParameters                = 0,
	CallReference                  = 0x01,
	TransmissionMediumRequirement  = 0x02,
	AccessTransport                = 0x03,
	CalledPartyNumber              = 0x04,
	SubsequentNumber               = 0x05,
	NatureOfConnectionIndicators   = 0x06,
	ForwardCallIndicators          = 0x07,
	OptionalForwardCallIndicators  = 0x08,
	CallingPartyCategory           = 0x09,
	CallingPartyNumber             = 0x0a,
	RedirectingNumber              = 0x0b,
	RedirectionNumber              = 0x0c,
	ConnectionRequest              = 0x0d,
	InformationRequestIndicators   = 0x0e,
	InformationIndicators          = 0x0f,
	ContinuityIndicators           = 0x10,
	BackwardCallIndicators         = 0x11,
	CauseIndicators                = 0x12,
	RedirectionInformation         = 0x13,
	GroupSupervisionTypeIndicator  = 0x15,
	RangeAndStatus                 = 0x16,
	CallModificationIndicators     = 0x17, // ANSI only
	FacilityIndicator              = 0x18,
	FacilityInformationIndicators  = 0x19, // ANSI only
	CUG_InterlockCode              = 0x1a,
	Index                          = 0x1b, // ANSI only
	CUG_CheckResponseIndicators    = 0x1c, // ANSI only
	UserServiceInformation         = 0x1d,
	SignallingPointCode            = 0x1e,
	UserToUserInformation          = 0x20,
	ConnectedNumber                = 0x21,
	SuspendResumeIndicators        = 0x22,
	TransitNetworkSelection        = 0x23,
	EventInformation               = 0x24,
	CircuitAssignmentMap           = 0x25, // ANSI only
	CircuitStateIndicator          = 0x26,
	AutomaticCongestionLevel       = 0x27,
	OriginalCalledNumber           = 0x28,
	OptionalBackwardCallIndicators = 0x29,
	UserToUserIndicators           = 0x2a,
	OriginationISCPointCode        = 0x2b, // ITU only
	GenericNotification            = 0x2c, // ITU only
	CallHistoryInformation         = 0x2d, // ITU only
	AccessDeliveryInformation      = 0x2e, // ITU only
	NetworkSpecificFacilities      = 0x2f, // ITU only
	UserServiceInformationPrime    = 0x30,
	PropagationDelayCounter        = 0x31, // ITU only
	RemoteOperations               = 0x32,
	ServiceActivation              = 0x33,
	UserTeleserviceInformation     = 0x34, // ITU only
	TransmissionMediumUsed         = 0x35,
	CallDiversionInformation       = 0x36, // ITU only
	EchoControlInformation         = 0x37, // ITU only
	MessageCompatInformation       = 0x38, // ITU only
	ParameterCompatInformation     = 0x39, // ITU only
	MLPP_Precedence                = 0x3a, // ITU name
	Precedence                     = MLPP_Precedence, // ANSI name
	MCID_RequestIndicator          = 0x3b, // ITU only
	MCID_ResponseIndicator         = 0x3c, // ITU only
	HopCounter                     = 0x3d,
	TransMediumRequirementPrime    = 0x3e, // ITU only
	LocationNumber                 = 0x3f, // ITU only
	RedirectionNumberRestriction   = 0x40, // ITU only
	FreephoneIndicators            = 0x41, // ITU only
	GenericReference               = 0x42, // ITU only
	CCSScallIndication             = 0x4b,
	ForwardGVNS                    = 0x4c,
	BackwardGVNS                   = 0x4d,
	RedirectCapability             = 0x4e, // National use
	CalledINNumber                 = 0x6f,
	UID_ActionIndicators           = 0x74,
	UID_CapabilityIndicators       = 0x75,
	RedirectCounter                = 0x77, // National use
	ApplicationTransport           = 0x78,
	CCNRpossibleIndicator          = 0x7a,
	PivotRoutingIndicators         = 0x7c,
	CalledDirectoryNumber          = 0x7d, // National use
	OriginalCalledINNumber         = 0x7f,
	CallingGeodeticLocation        = 0x81,
	HTR_Information                = 0x82,
	NetworkRoutingNumber           = 0x84, // National use
	QueryOnReleaseCapability       = 0x85, // Network option
	PivotStatus                    = 0x86, // National use
	PivotCounter                   = 0x87,
	PivotRoutingForwardInformation = 0x88,
	PivotRoutingBackInformation    = 0x89,
	RedirectStatus                 = 0x8a, // National use
	RedirectForwardInformation     = 0x8b, // National use
	RedirectBackwardInformation    = 0x8c, // National use
	NumberPortabilityInformation   = 0x8d, // Network option
	GenericNumber                  = 0xc0, // ITU name
	GenericAddress                 = GenericNumber, // ANSI name
	GenericDigits                  = 0xc1,
	OperatorServicesInformation    = 0xc2, // ANSI only
	Egress                         = 0xc3, // ANSI only
	Jurisdiction                   = 0xc4, // ANSI only
	CarrierIdentification          = 0xc5, // ANSI only
	BusinessGroup                  = 0xc6, // ANSI only
	GenericName                    = 0xc7, // ANSI only
	NotificationIndicator          = 0xe1, // ANSI only
	TransactionRequest             = 0xe3, // ANSI only
	CircuitGroupCharactIndicator   = 0xe5, // ANSI only
	CircuitValidationRespIndicator = 0xe6, // ANSI only
	OutgoingTrunkGroupNumber       = 0xe7, // ANSI only
	CircuitIdentificationName      = 0xe8, // ANSI only
	CommonLanguage                 = 0xe9, // ANSI only
	OriginatingLineInformation     = 0xea, // ANSI only
	ChargeNumber                   = 0xeb, // ANSI only
	ServiceCodeIndicator           = 0xec, // ANSI only
	SpecialProcessingRequest       = 0xed, // ANSI only
	CarrierSelectionInformation    = 0xee, // ANSI only
	NetworkTransport               = 0xef, // ANSI only
	NationalForwardCallIndicatorsLinkByLink = 0xf4, // UK-ISUP
	NationalInformationIndicators           = 0xf5, // UK-ISUP
	NationalInformationRequestIndicators    = 0xf6, // UK-ISUP
	CalledSubscribersTerminatingFacilMarks  = 0xf7, // UK-ISUP
	CallingSubscribersOriginatingFacilMarks = 0xf8, // UK-ISUP
	CallingSubscribersBasicServiceMarks     = 0xf9, // UK-ISUP
	CalledSubscribersBasicServiceMarks      = 0xfa, // UK-ISUP
	PartialCLI                              = 0xfb, // UK-ISUP
	LastDivertingLineIdentity               = 0xfc, // UK-ISUP
	PresentationNumber                      = 0xfd, // UK-ISUP
	NationalForwardCallIndicators           = 0xfe, // UK-ISUP
    };

    /**
     * Constructor
     * @param type Type of ISUP message as enumeration
     * @param cic Source/destination Circuit Identification Code
     */
    inline SS7MsgISUP(Type type, unsigned int cic)
	: SignallingMessage(lookup(type,"Unknown")), m_type(type), m_cic(cic)
	{ }

    /**
     * Destructor
     */
    virtual ~SS7MsgISUP()
	{ }

    /**
     * Get the type of this message
     * @return The type of this message as enumeration
     */
    inline Type type() const
	{ return m_type; }

    /**
     * Get the source/destination Circuit Identification Code of this message
     * @return The source/destination Circuit Identification Code of this message
     */
    inline unsigned int cic() const
	{ return m_cic; }

    /**
     * Fill a string with this message's parameters for debug purposes
     * @param dest The destination string
     * @param label The routing label
     * @param params True to add parameters
     * @param raw Optional raw message data to be added to destination
     * @param rawLen Raw data length
     */
    void toString(String& dest, const SS7Label& label, bool params,
	const void* raw = 0, unsigned int rawLen = 0) const;

    /**
     * Get the dictionary with the message names
     * @return Pointer to the dictionary with the message names
     */
    static const TokenDict* names();

    /**
     * Convert an ISUP message type to a C string
     * @param type Type of ISUP message to look up
     * @param defvalue Default string to return
     * @return Name of the ISUP message type
     */
    static inline const char* lookup(Type type, const char* defvalue = 0)
	{ return TelEngine::lookup(type,names(),defvalue); }

    /**
     * Look up an ISUP message name
     * @param name String name of the ISUP message
     * @param defvalue Default type to return
     * @return Encoded type of the ISUP message
     */
    static inline Type lookup(const char* name, Type defvalue = Unknown)
	{ return static_cast<Type>(TelEngine::lookup(name,names(),defvalue)); }

private:
    Type m_type;                         // Message type
    unsigned int m_cic;                  // Source/destination Circuit Identification Code
};

/**
 * Implementation of SS7 SNM User Part (Management) - Q.704
 * @short SS7 SNM implementation
 */
class YSIG_API SS7Management : public SS7Layer4, public Mutex
{
    YCLASS(SS7Management,SS7Layer4)
public:
    /**
     * Constructor
     */
    SS7Management(const NamedList& params, unsigned char sio = SS7MSU::SNM|SS7MSU::National);

protected:
    /**
     * Process a MSU received from a Layer 3 component
     * @param msu Message data, starting with Service Indicator Octet
     * @param label Routing label of the received MSU
     * @param network Network layer that delivered the MSU
     * @param sls Signalling Link the MSU was received from
     * @return Result of MSU processing
     */
    virtual HandledMSU receivedMSU(const SS7MSU& msu, const SS7Label& label, SS7Layer3* network, int sls);

    /**
     * Set and clear inhibition flags on a link of a router attached network
     * @param link Signalling Link to modify identified by a routing label
     * @param setFlags Flag bits to set ORed together
     * @param clrFlags Flag bits to clear  ORed together (optional)
     * @return True if inhibition flags were set
     */
    bool inhibit(const SS7Label& link, int setFlags, int clrFlags = 0);

    /**
     * Check inhibition flags on a link of a router attached network
     * @param link Signalling Link to check identified by a routing label
     * @param flags Flag bits to check ORed together
     * @return True if any of the specified inhibition flags are set
     */
    bool inhibited(const SS7Label& link, int flags);

    /**
     * Recover MSUs from a link
     * @param link Signalling Link to recover identified by a routing label
     * @param sequence Starting sequence number to recover
     */
    void recover(const SS7Label& link, int sequence);

    /**
     * Process a notification generated by the attached network layer
     * @param link Network or linkset that generated the notification
     * @param sls Signallink Link that generated the notification, negative if none
     * @return True if notification was processed
     */
    virtual void notify(SS7Layer3* link, int sls);

    /**
     * Query or modify the management settings or operational parameters
     * @param params The list of parameters to query or change
     * @return True if the control operation was executed
     */
    virtual bool control(NamedList& params);

    /**
     * Method called periodically by the engine to retransmit messages
     * @param when Time to use as computing base for timers
     */
    virtual void timerTick(const Time& when);

private:
    bool postpone(SS7MSU* msu, const SS7Label& label, int txSls,
	u_int64_t interval, u_int64_t global = 0, bool force = false, const Time& when = Time());
    bool timeout(const SS7MSU& msu, const SS7Label& label, int txSls, bool final);
    bool timeout(SignallingMessageTimer& timer, bool final);
    SignallingMessageTimerList m_pending;
    bool m_changeMsgs;
    bool m_changeSets;
    bool m_neighbours;
};

/**
 * Implementation of SS7 MTP Test User Part - Q.782 2.3
 * @short SS7 MTP Test Traffic implementation
 */
class YSIG_API SS7Testing : public SS7Layer4, public Mutex
{
    YCLASS(SS7Testing,SS7Layer4)
public:
    /**
     * Constructor
     */
    inline SS7Testing(const NamedList& params, unsigned char sio = SS7MSU::MTP_T|SS7MSU::National)
	: SignallingComponent(params.safe("SS7Testing"),&params,"ss7-test"),
	  SS7Layer4(sio,&params),
	  Mutex(true,"SS7Testing"),
	  m_timer(0), m_exp(0), m_seq(0), m_len(16), m_sharing(false)
	{ }

    /**
     * Configure and initialize the user part
     * @param config Configuration parameters
     * @return True if the component was initialized properly
     */
    virtual bool initialize(const NamedList* config);

    /**
     * Query or modify the test part settings or operational parameters
     * @param params The list of parameters to query or change
     * @return True if the control operation was executed
     */
    virtual bool control(NamedList& params);

protected:
    /**
     * Process a MSU received from a Layer 3 component
     * @param msu Message data, starting with Service Indicator Octet
     * @param label Routing label of the received MSU
     * @param network Network layer that delivered the MSU
     * @param sls Signalling Link the MSU was received from
     * @return Result of MSU processing
     */
    virtual HandledMSU receivedMSU(const SS7MSU& msu, const SS7Label& label, SS7Layer3* network, int sls);

    /**
     * Process a notification generated by the attached network layer
     * @param link Network or linkset that generated the notification
     * @param sls Signallink Link that generated the notification, negative if none
     * @return True if notification was processed
     */
    virtual void notify(SS7Layer3* link, int sls);

    /**
     * Method called periodically by the engine to emit new messages
     * @param when Time to use as computing base for timers
     */
    virtual void timerTick(const Time& when);

private:
    bool sendTraffic();
    void setParams(const NamedList& params, bool setSeq = false);
    SignallingTimer m_timer;
    SS7Label m_lbl;
    u_int32_t m_exp;
    u_int32_t m_seq;
    u_int16_t m_len;
    bool m_sharing;
};

/**
 * A signalling call using SS7 ISUP protocol
 * @short An SS7 ISUP call
 */
class YSIG_API SS7ISUPCall : public SignallingCall
{
    friend class SS7ISUP;
public:
    /**
     * Call state enumerators
     */
    enum State {
	// NOTE: Keep the order of state values: the code relies on it
	Null      = 0,                   // No message exchanged
	Testing   = 1,                   // IAM but waiting for continuity check
	Setup     = 2,                   // IAM (initial address)
	Accepted  = 3,                   // ACM (address complete)
	Ringing   = 4,                   // CPM (call progress)
	Answered  = 5,                   // ANM (answer)
	Releasing = 6,                   // REL (release)
	Released  = 7                    // Call released, no message or events allowed
    };

    /**
     * Destructor.
     * Complete call release. Releas circuit. Remove itself from controller's list
     */
    virtual ~SS7ISUPCall();

    /**
     * Get the call state
     * @return The call state as enumeration
     */
    inline State state() const
	{ return m_state; }

    /**
     * Check if the call is a not test one in early state
     * @return True if this a non test call in early state
     */
    inline bool earlyState() const
	{ return m_state <= Setup && !m_testCall; }

    /**
     * Get the call's circuit range
     * @return The call's circuit range
     */
    inline const String& cicRange() const
	{ return m_cicRange; }

    /**
     * Get the call id (the code of the circuit reserved for this call)
     * @return The call id
     */
    inline unsigned int id() const
	{ return m_circuit ? m_circuit->code() : 0; }

    /**
     * Get an event from this call
     * This method is thread safe
     * @param when The current time
     * @return SignallingEvent pointer or 0 if no events
     */
    virtual SignallingEvent* getEvent(const Time& when);

    /**
     * Send an event to this call
     * @param event The event to send
     * @return True if the operation succedded
     */
    virtual bool sendEvent(SignallingEvent* event);

    /**
     * Set termination flag. Set termination reason if not already set
     * @param gracefully True to send RLC on termination, false to destroy the call without notification
     * @param reason Termination reason
     * @param diagnostic Optional diagnostic data to be sent with termination reason
     * @param location Optional release location
     */
    inline void setTerminate(bool gracefully, const char* reason = 0,
	const char* diagnostic = 0, const char* location = 0)
    {
	Lock lock(this);
	m_terminate = true;
	m_gracefully = gracefully;
	setReason(reason,0,diagnostic,location);
    }

    /**
     * Get a pointer to this object or other data
     * @param name Object name
     * @return The requested pointer or 0 if not exists
     */
    virtual void* getObject(const String& name) const;

protected:
    /**
     * Constructor
     * @param controller The call controller
     * @param cic The reserved circuit
     * @param local The local point code used to create the routing label for sent messages
     * @param remote The remote point code used to create the routing label for sent messages
     * @param outgoing Call direction
     * @param sls Optional link for the routing label
     * @param range Optional range used to re-allocate a circuit for this call if necessary
     * @param testCall True if this is a test call
     */
    SS7ISUPCall(SS7ISUP* controller, SignallingCircuit* cic,
	const SS7PointCode& local, const SS7PointCode& remote, bool outgoing,
	int sls = -1, const char* range = 0, bool testCall = false);

    /**
     * Release call. Stop timers. Send a RLC (Release Complete) message if it should terminate gracefully
     * Decrease the object's referrence count and generate a Release event if not final
     * This method is thread safe
     * @param final True if called from destructor
     * @param msg Received message with parameters if any
     * @param reason Optional release reason
     * @param timeout True if this is method is called due to T5 timer expiry
     * @return SignallingEvent pointer or 0
     */
    SignallingEvent* releaseComplete(bool final, SS7MsgISUP* msg = 0, const char* reason = 0,
	bool timeout = false);

    /**
     * Check if the call's circuit can be replaced at this time
     * @return True if the circuit can be replaced
     */
    bool canReplaceCircuit();

    /**
     * Replace the circuit reserved for this call. Release the already reserved circuit.
     * Retransmit the initial IAM request on success.
     * On failure set the termination flag and release the new circuit if valid.
     * If false is returned, the call is prepared to return a Release event.
     * This method is thread safe
     * @param circuit The new circuit reserved for this call
     * @param msg Optional message to send before IAM (it will be consumed)
     * @return False if the state is greater then Setup, the call is not outgoing or the new circuit is 0
     */
    bool replaceCircuit(SignallingCircuit* circuit, SS7MsgISUP* msg = 0);

    /**
     * Stop waiting for a SGM (Segmentation) message when another message is received by the controller.
     * This method is thread safe
     * @param discard True to discard (destruct) the segment waiting message if any
     */
    void stopWaitSegment(bool discard);

private:
    // Initialize/set IAM message parameters
    // @param msg Valid ISUP message
    // @param outgoing Message direction: true for outgoing
    // @param sigMsg Valid signalling message with parameters if outgoing
    bool copyParamIAM(SS7MsgISUP* msg, bool outgoing = false, SignallingMessage* sigMsg = 0);
    // If already releasing, set termination flag. Otherwise, send REL (Release) message
    // Set m_lastEvent if event is 0 (the method is called internally)
    // @param event Event with the parameters. 0 if release is started on some timeout
    // @param msg Received message to put in release event
    // @return Generated release event if any
    SignallingEvent* release(SignallingEvent* event = 0, SS7MsgISUP* msg = 0);
    // Set termination reason from message or parameter
    void setReason(const char* reason, SignallingMessage* msg, const char* diagnostic = 0,
	const char* location = 0);
    // Accept send/receive messages in current state based on call direction
    bool validMsgState(bool send, SS7MsgISUP::Type type, bool hasBkwCallInd = false);
    // Connect the reserved circuit. Return false if it fails. Return true if this call is a signalling only one
    bool connectCircuit(const char* special = 0);
    // Transmit the IAM message. Start IAM timer if not started
    bool transmitIAM();
    // Transmit SAM digits
    bool transmitSAM(const char* extra = 0);
    // (Re)transmit REL. Create and populate message if needed. Remember sls
    bool transmitREL(const NamedList* params = 0);
    // Check if the circuit needs continuity testing
    bool needsTesting(const SS7MsgISUP* msg);
    // Stop waiting for a SGM (Segmentation) message. Copy parameters to the pending segmented message if sgm is valid.
    // Change call state and set m_lastEvent
    // @param sgm Optional received SGM message with parameters to be added to the pending segmented message
    // @param timeout True if waiting timer timed out. Ignored if sgm is non null
    // @return m_lastEvent
    SignallingEvent* processSegmented(SS7MsgISUP* sgm = 0, bool timeout = false);
    // Transmit message. Set routing label's link if not already set
    inline bool transmitMessage(SS7MsgISUP* msg);
    // Get the ISUP call controller
    inline SS7ISUP* isup() const;
    // Set overlapped flag. Output a debug message
    void setOverlapped(bool on, bool numberComplete = true);

    State m_state;                       // Call state
    bool m_testCall;                     // Test only call
    SignallingCircuit* m_circuit;        // Circuit reserved for this call
    String m_cicRange;                   // The range used to re(alloc) a circuit
    SS7Label m_label;                    // The routing label for this call
    bool m_terminate;                    // Termination flag
    bool m_gracefully;                   // Terminate gracefully: send RLC
    bool m_circuitChanged;               // Circuit change flag
    bool m_circuitTesting;               // The circuit is tested for continuity
    bool m_inbandAvailable;              // Inband data is available
    int m_replaceCounter;                // Circuit replace counter
    String m_format;                     // Data format used by the circuit
    String m_reason;                     // Termination reason
    String m_diagnostic;                 // Termination diagnostic
    String m_location;                   // Termination location
    SS7MsgISUP* m_iamMsg;                // Message with the call parameters for outgoing calls
    SS7MsgISUP* m_sgmMsg;                // Pending received message with segmentation flag set
    SS7MsgISUP* m_relMsg;                // Release message preserved for retransmissions
    // Overlapped
    String m_samDigits;                  // SAM digits
    unsigned int m_sentSamDigits;        // The number of sent SAM digits
    // Timers
    SignallingTimer m_relTimer;          // Send release
    SignallingTimer m_iamTimer;          // Send initial address
    SignallingTimer m_sgmRecvTimer;      // Receive segmented message
    SignallingTimer m_contTimer;         // Continuity timer
    SignallingTimer m_anmTimer;          // T9 ACM -> ANM timer
};

/**
 * Implementation of SS7 ISDN User Part
 * @short SS7 ISUP implementation
 */
class YSIG_API SS7ISUP : public SignallingCallControl, public SS7Layer4
{
    YCLASS(SS7ISUP,SS7Layer4)
    friend class SS7ISUPCall;
public:
    /**
     * Special SLS values
     */
    enum {
	SlsAuto    = -1,
	SlsLatest  = -2,
	SlsCircuit = -3,
	SlsDefault = -4
    };

    enum ChargeProcess {
	Confusion,
	Ignore,
	Raw,
	Parsed
    };

    /**
     * Constructor
     * @param params Call controller's parameters
     * @param sio The default Service Information Octet
     */
    SS7ISUP(const NamedList& params, unsigned char sio = SS7MSU::ISUP|SS7MSU::National);

    /**
     * Destructor
     */
    virtual ~SS7ISUP();

    /**
     * Configure and initialize the call controller and user part
     * @param config Optional configuration parameters override
     * @return True if ISUP was initialized properly
     */
    virtual bool initialize(const NamedList* config);

    /**
     * Get the controller's status as text
     * @return Controller status name
     */
    virtual const char* statusName() const;

    /**
     * Attach a SS7 network or router to this service. Detach itself from the old one
     * @param network Pointer to network or router to attach
     */
    virtual void attach(SS7Layer3* network);

    /**
     * Get the length of the Circuit Identification Code for this user part
     * @return Length of the CIC field in octets
     */
    unsigned int cicLen() const
	{ return m_cicLen; }

    /**
     * Get the default data format
     * @return The default data format
     */
    const String& format() const
	{ return m_format; }

    /**
     * Check if the message parser of this controller should ignore unknown digits encoding
     * @return True if unknown digits are ignored
     */
    inline bool ignoreUnknownAddrSignals() const
	{ return m_ignoreUnkDigits; }

    /**
     * Append a point code to the list of point codes serviced by this controller
     *  if not already there. Set default point code if requested.
     * If the list is empty, the default point code is set to the first point code added
     * @param pc The point code to append
     * @param def True if this point code is the default for outgoing calls
     * @return False if the point code is invalid for this call controller type. If true is returned, don't reuse the pointer
     */
    bool setPointCode(SS7PointCode* pc, bool def);

    /**
     * Append all point codes from a parameter list, use "pointcode" and
     *  "defaultpointcode" parameters
     * @param params List of parameters to take point codes from
     * @return Count of point codes added
     */
    unsigned int setPointCode(const NamedList& params);

    /**
     * Check if the given point code is serviced by this controller
     * @param pc The point code to check
     * @return SS7PointCode pointer or 0 if not found
     */
    SS7PointCode* hasPointCode(const SS7PointCode& pc);

    /**
     * Check if this controller should handle a remote point code
     * @param pc The remote point code to check
     * @return True if pc matches the remote or there is no remote set
     */
    inline bool handlesRemotePC(const SS7PointCode& pc) const
	{ return !m_remotePoint || (pc == *m_remotePoint); }

    /**
     * Set a routing label to be used for outgoing messages
     * @param label Routing label to set
     * @param opc Originating point code
     * @param dpc Destination point code
     * @param sls Signalling Link Selection
     */
    inline void setLabel(SS7Label& label, const SS7PointCode& opc, const SS7PointCode& dpc,
	unsigned char sls = 255)
	{ label.assign(m_type,dpc,opc,sls); }

    /**
     * Set debug data of this call controller
     * @param printMsg Enable/disable message printing on output
     * @param extendedDebug Enable/disable hex data dump if print messages is enabled
     */
    inline void setDebug(bool printMsg, bool extendedDebug)
	{ m_extendedDebug = ((m_printMsg = printMsg) && extendedDebug); }

    /**
     * Create a new MSU populated with type, routing label and space for fixed part
     * @param type Type of ISUP message
     * @param ssf Subservice Field
     * @param label Routing label for the new MSU
     * @param cic Circuit Identification Code
     * @param params Optional parameter list
     * @return Pointer to the new MSU or NULL if an error occured
     */
    virtual SS7MSU* createMSU(SS7MsgISUP::Type type, unsigned char ssf,
	const SS7Label& label, unsigned int cic, const NamedList* params = 0) const;

    /**
     * Create an outgoing call. Send a NewCall event with the given msg parameter
     * This method is thread safe
     * @param msg Call parameters
     * @param reason Failure reason if any
     * @return Referenced SignallingCall pointer on success or 0 on failure
     */
    virtual SignallingCall* call(SignallingMessage* msg, String& reason);

    /**
     * Converts an ISUP message to a Message Signal Unit and push it down the protocol stack.
     * The given message is consumed
     * @param msg The message to send
     * @param label The routing label for the message
     * @param recvLbl True if the given label is from a received message. If true, a new routing
     *  label will be created from the received one
     * @param sls Signalling Link to use for the new routing label. Ignored if recvLbl is false
     * @return Link the message was successfully queued to, negative for error
     */
    int transmitMessage(SS7MsgISUP* msg, const SS7Label& label, bool recvLbl, int sls = SlsDefault);

    /**
     * Cleanup calls
     * This method is thread safe
     * @param reason Cleanup reason
     */
    virtual void cleanup(const char* reason = "net-out-of-order");

    /**
     * Query or modify ISUP's settings or operational parameters
     * @param params The list of parameters to query or change
     * @return True if the control operation was executed
     */
    virtual bool control(NamedList& params);

    /**
     * Decode an ISUP message buffer to a list of parameters
     * @param msg Destination list of parameters
     * @param msgType The message type
     * @param pcType The point code type (message version)
     * @param paramPtr Pointer to the Parameter area (just after the message type)
     * @param paramLen Length of the Parameter area
     * @return True if the mesage was successfully parsed
     */
    bool decodeMessage(NamedList& msg, SS7MsgISUP::Type msgType, SS7PointCode::Type pcType,
	const unsigned char* paramPtr, unsigned int paramLen);

    /**
     * Encode an ISUP list of parameters to a buffer.
     * The input list may contain a 'message-prefix' parameter to override this controller's prefix
     * @param buf Destination buffer
     * @param msgType The message type
     * @param pcType The point code type (message version)
     * @param params Message list of parameters
     * @param cic Optional cic to be added before mesage
     * @return True if the mesage was successfully encoded
     */
    bool encodeMessage(DataBlock& buf, SS7MsgISUP::Type msgType, SS7PointCode::Type pcType,
	const NamedList& params, unsigned int* cic = 0);

    /**
     * Process parameter compatibility lists.
     * Terminate an existing call if a non emtpy release call parameter(s) list is found.
     * Send CNF if non emtpy cnf parameter(s) list is found
     * @param list Message parameter list
     * @param cic The circuit code
     * @param callReleased Optional pointer to boolean value to be set if a call was released
     * @return True if any parameter compatibility was handled
     */
    bool processParamCompat(const NamedList& list, unsigned int cic, bool* callReleased = 0);

    /**
     * Obtain the way that charge message should be processed
     * @return The way that charge message should be processed
     */
    inline ChargeProcess getChargeProcessType() const
	{ return m_chargeProcessType; }

protected:
    /**
     * Remove all links with other layers. Disposes the memory
     */
    virtual void destroyed();

    /**
     * Send CGU if not already done. Check timeouts
     * @param when Time to use as computing base for timeouts
     */
    virtual void timerTick(const Time& when);

    /**
     * Process a notification generated by the attached network layer
     * @param link Network or linkset that generated the notification
     * @param sls Signalling Link that generated the notification, negative if none
     */
    virtual void notify(SS7Layer3* link, int sls);

    /**
     * Create a new MSU populated with type, routing label and space for fixed part
     * @param type Type of ISUP message
     * @param sio Service Information Octet
     * @param label Routing label for the new MSU
     * @param cic Circuit Identification Code
     * @param params Parameter list
     * @return Pointer to the new MSU or NULL if an error occured
     */
    SS7MSU* buildMSU(SS7MsgISUP::Type type, unsigned char sio,
	const SS7Label& label, unsigned int cic, const NamedList* params) const;

    /**
     * Process a MSU received from a Layer 3 component
     * @param msu Message data, starting with Service Indicator Octet
     * @param label Routing label of the received MSU
     * @param network Network layer that delivered the MSU
     * @param sls Signalling Link the MSU was received from
     * @return Result of MSU processing
     */
    virtual HandledMSU receivedMSU(const SS7MSU& msu, const SS7Label& label, SS7Layer3* network, int sls);

    /**
     * Process a MSU received from a Layer 3 component
     * @param type Type of ISUP message
     * @param cic Circuit Identification Code
     * @param paramPtr Pointer to the Parameter area
     * @param paramLen Length of the Parameter area
     * @param label Routing label of the received MSU
     * @param network Network layer that delivered the MSU
     * @param sls Signalling Link the MSU was received from
     * @return True if the MSU was processed
     */
    virtual bool processMSU(SS7MsgISUP::Type type, unsigned int cic,
        const unsigned char* paramPtr, unsigned int paramLen,
	const SS7Label& label, SS7Layer3* network, int sls);

    /**
     * Notification for receiving User Part Unavailable
     * @param type Type of Point Code
     * @param node Node on which the User Part is unavailable
     * @param part User Part (service) reported unavailable
     * @param cause Unavailability cause - Q.704 15.17.5
     * @param label Routing label of the UPU message
     * @param sls Signaling link the UPU was received on
     */
    virtual void receivedUPU(SS7PointCode::Type type, const SS7PointCode node,
	SS7MSU::Services part, unsigned char cause, const SS7Label& label, int sls);

    /**
     * Process an event received from a non-reserved circuit
     * @param event The event, will be consumed and zeroed
     * @param call Optional signalling call whose circuit generated the event
     * @return Signalling event pointer or 0
     */
    virtual SignallingEvent* processCircuitEvent(SignallingCircuitEvent*& event,
	SignallingCall* call = 0);

    /**
     * Initiate circuit reset. The circuit must be already reserved
     * This method is thread safe
     * @param cic The circuit to reset. Its referrence counter will be decreased and
     *  the pointer will be zeroed
     * @param timer Ellapsed timer
     * @return True if the circuit reset was initiated
     */
    bool startCircuitReset(SignallingCircuit*& cic, const String& timer);

    /**
     * Length of the Circuit Identification Code in octets
     */
    unsigned int m_cicLen;

private:
    // Process a received message that should be processed by a call
    // @param msg The received message
    // @param label The routing label of the received message
    // @param sls Signalling Link the message was received from
    void processCallMsg(SS7MsgISUP* msg, const SS7Label& label, int sls);
    // Process a received message that should be processed by this call controller
    // @param msg The received message
    // @param label The routing label of the received message
    // @param sls Signalling Link the message was received from
    void processControllerMsg(SS7MsgISUP* msg, const SS7Label& label, int sls);
    // Replace a call's circuit if checkCall is true
    // Clear lock flags of the circuit. Release currently reseting circuit if the code match
    // Return false if the given circuit doesn't exist
    bool resetCircuit(unsigned int cic, bool remote, bool checkCall);
    // Block/unblock a circuit side (local or remote)
    // Return false if the given circuit doesn't exist
    bool blockCircuit(unsigned int cic, bool block, bool remote, bool hwFail,
	bool changed, bool changedState, bool resetLocking = false);
    // Find a call by its circuit identification code
    // This method is not thread safe
    SS7ISUPCall* findCall(unsigned int cic);
    // Find a call by its circuit identification code
    // This method is thread safe
    inline void findCall(unsigned int cic, RefPointer<SS7ISUPCall>& call) {
	    Lock mylock(this);
	    call = findCall(cic);
	}
    // Encode a raw message
    SS7MSU* encodeRawMessage(SS7MsgISUP::Type type, unsigned char sio,
	const SS7Label& label, unsigned int cic, const String& param) const;
    // Send blocking/unblocking messages.
    // Restart the re-check timer if there is any (un)lockable, not sent cic
    // Return false if no request was sent
    bool sendLocalLock(const Time& when = Time());
    // Fill label from local/remote point codes
    // This method is thread safe
    // Return a true if local and remote point codes are valid
    bool setLabel(SS7Label& label, unsigned int cic);
    // Retrieve a pending message
    SignallingMessageTimer* findPendingMessage(SS7MsgISUP::Type type, unsigned int cic,
	bool remove = false);
    // Retrieve a pending message with given parameter
    SignallingMessageTimer* findPendingMessage(SS7MsgISUP::Type type, unsigned int cic,
	const String& param, const String& value, bool remove = false);
    // Transmit a list of messages. Return true if at least 1 message was sent
    bool transmitMessages(ObjList& list);
    // Handle circuit(s) (un)block command
    bool handleCicBlockCommand(const NamedList& p, bool block);
    // Handle remote circuit(s) (un)block command
    bool handleCicBlockRemoteCommand(const NamedList& p, unsigned int* cics,
	unsigned int count, bool block);
    // Handle circuit(s) event generation command
    bool handleCicEventCommand(const NamedList& p);
    // Try to start single circuit (un)blocking. Set a pending operation on success
    // @param force True to ignore resetting/(un)blocking flags of the circuit
    // Return built message to be sent on success
    SS7MsgISUP* buildCicBlock(SignallingCircuit* cic, bool block, bool force = false);
    // Replace circuit for outgoing calls in Setup state
    // Send REL/RSC before repeat attempt
    void replaceCircuit(unsigned int cic, const String& map, bool rel = true);
    // Handle circuit hw-fail block
    // Replace cics for outgoing calls. Terminate incoming
    void cicHwBlocked(unsigned int cic, const String& map);

    SS7PointCode::Type m_type;           // Point code type of this call controller
    ObjList m_pointCodes;                // Point codes serviced by this call controller
    SS7PointCode* m_defPoint;            // Default point code for outgoing calls
    SS7PointCode* m_remotePoint;         // Default remote point code for outgoing calls and maintenance
    unsigned char m_sls;                 // Last known valid SLS
    bool m_earlyAcm;                     // Accept progress/ringing in early ACM
    bool m_inn;                          // Routing to internal network number flag
    int m_defaultSls;                    // Default SLS to use in outbound calls
    unsigned int m_maxCalledDigits;      // Maximum digits allowed in Called Number in IAM
    String m_numPlan;                    // Numbering plan
    String m_numType;                    // Number type
    String m_numPresentation;            // Number presentation
    String m_numScreening;               // Number screening
    String m_callerCat;                  // Caller party category
    String m_format;                     // Default format
    String m_continuity;                 // Continuity test type
    bool m_confirmCCR;                   // Send LPA in response to CCR
    bool m_dropOnUnknown;                // Drop call in early state on unknown message
    bool m_ignoreGRSSingle;              // Ignore (drop) GRS with range 0 (1 circuit affected)
    bool m_ignoreCGBSingle;              // Ignore (drop) CGB with range 0 (1 circuit in map)
    bool m_ignoreCGUSingle;              // Ignore (drop) CGU with range 0 (1 circuit in map)
    bool m_duplicateCGB;                 // Send duplicate CGB messages (ANSI)
    bool m_ignoreUnkDigits;              // Check if the message parser should ignore unknown digits encoding
    bool m_l3LinkUp;                     // Flag indicating the availability of a Layer3 data link
    ChargeProcess m_chargeProcessType;   // Indicates the way that charge message should be processed
    u_int64_t m_t1Interval;              // Q.764 T1 timer interval
    u_int64_t m_t5Interval;              // Q.764 T5 timer interval
    u_int64_t m_t7Interval;              // Q.764 T7 timer interval
    u_int64_t m_t9Interval;              // Q.764 T9 AMM/CON recv timer interval
    u_int64_t m_t12Interval;             // Q.764 T12 BLK timer interval
    u_int64_t m_t13Interval;             // Q.764 T13 BLK global timer interval
    u_int64_t m_t14Interval;             // Q.764 T14 UBL timer interval
    u_int64_t m_t15Interval;             // Q.764 T15 UBL global timer interval
    u_int64_t m_t16Interval;             // Q.764 T16 RSC timer interval
    u_int64_t m_t17Interval;             // Q.764 T17 timer interval
    u_int64_t m_t18Interval;             // Q.764 T18 CGB timer interval
    u_int64_t m_t19Interval;             // Q.764 T19 CGB global timer interval
    u_int64_t m_t20Interval;             // Q.764 T20 CGU timer interval
    u_int64_t m_t21Interval;             // Q.764 T21 CGU global timer interval
    u_int64_t m_t27Interval;             // Q.764 T27 Reset after Cont. Check failure
    u_int64_t m_t34Interval;             // Q.764 T34 Segmentation receive timout
    SignallingMessageTimerList m_pending;// Pending messages (RSC ...)
    // Remote User Part test
    SignallingTimer m_uptTimer;          // Timer for UPT
    bool m_userPartAvail;                // Flag indicating the remote User Part availability
    SS7MsgISUP::Type m_uptMessage;       // Message used, may not be always UPT
    unsigned int m_uptCicCode;           // The circuit code sent with UPT
    int m_cicWarnLevel;                  // Wrong CIC warn level
    int m_replaceCounter;                // Circuit replace counter
    // Circuit reset
    SignallingTimer m_rscTimer;          // RSC message or idle timeout
    SignallingCircuit* m_rscCic;         // Circuit currently beeing reset
    u_int32_t m_rscInterval;             // Saved reset interval
    u_int32_t m_rscSpeedup;              // Circuits left for speedup
    // Blocking/unblocking circuits
    SignallingTimer m_lockTimer;         // Timer used to re-check local lock
    bool m_lockGroup;                    // Allow sending requests for a group
    // Debug flags
    bool m_printMsg;                     // Print messages to output
    bool m_extendedDebug;                // Extended debug flag
};

/**
 * Implementation of SS7 Bearer Independent Call Control User Part
 * @short SS7 BICC implementation
 */
class YSIG_API SS7BICC : public SS7ISUP
{
    YCLASS(SS7BICC,SS7ISUP)
public:
    /**
     * Constructor
     * @param params Call controller's parameters
     * @param sio The default Service Information Octet
     */
    SS7BICC(const NamedList& params, unsigned char sio = SS7MSU::BICC|SS7MSU::National);

    /**
     * Destructor
     * Terminate all calls
     */
    virtual ~SS7BICC();

protected:
    /**
     * Process a MSU received from a Layer 3 component
     * @param msu Message data, starting with Service Indicator Octet
     * @param label Routing label of the received MSU
     * @param network Network layer that delivered the MSU
     * @param sls Signalling Link the MSU was received from
     * @return Result of MSU processing
     */
    virtual HandledMSU receivedMSU(const SS7MSU& msu, const SS7Label& label, SS7Layer3* network, int sls);
};

/**
 * Implementation of SS7 Telephone User Part
 * @short SS7 TUP implementation
 */
class YSIG_API SS7TUP : public SignallingCallControl, public SS7Layer4
{
public:
    /**
     * Constructor
     * @param params Call controller's parameters
     * @param sif The default Service Information Field
     */
    SS7TUP(const NamedList& params, unsigned char sif = SS7MSU::TUP);

    /**
     * Destructor
     * Terminate all calls
     */
    virtual ~SS7TUP();
};

/**
 * An interface to a SS7 SCCP Management
 * @short Abstract SS7 SCCP Management
 */

class YSIG_API SCCPManagement : public SignallingComponent , public Mutex
{
    friend class SS7SCCP;
    friend class SccpLocalSubsystem; // Local Broadcast
    YCLASS(SCCPManagement,SignallingComponent)
public:
    enum MsgType {
	SSA = 0x01,  // Subsystem-allowed
	SSP = 0x02,  // Subsystem-prohibited
	SST = 0x03,  // Subsystem-status-test
	SOR = 0x04,  // Subsystem-out-of-service-request
	SOG = 0x05,  // Subsystem-out-of-service-grant
	SSC = 0x06,  // SCCP/Subsystem-congested      (ITU  only)
	SBR = 0xfd,  // Subsystem-backup-routing      (ANSI only)
	SNR = 0xfe,  // Subsystem-normal-routing      (ANSI only)
	SRT = 0xff   // Subsystem-routing-status-test (ANSI only)
    };

    enum LocalBroadcast {
	UserOutOfService,
	UserInService,
	PCInaccessible,         // Signalling Point Inaccessible
	PCAccessible,           // Signalling Point Accessible
	SccpRemoteInaccessible,
	SccpRemoteAccessible,
	PCCongested,           // Signalling Point Congested
	SubsystemStatus        // Request send by sccp management to find if a ssn is available
    };

    enum SccpStates {
	Allowed = SS7Route::Allowed,
	Prohibited = SS7Route::Prohibited,
	Unknown = SS7Route::Unknown,
	WaitForGrant,
	IgnoreTests
    };

    /**
     * Constructor
     */
    SCCPManagement(const NamedList& params, SS7PointCode::Type type);

    /**
     * Destructor
     */
    virtual ~SCCPManagement();

    /**
     * Initialize this sccp management
     */
    virtual bool initialize(const NamedList* config);

    /**
     * Process a management message received from sccp
     * @param message The message to process
     * @return True if the message was processed successfully
     */
    virtual bool processMessage(SS7MsgSCCP* message);

    /**
     * Attach a ss7 sccp to this management
     * @param sccp The ss7 sccp to attach
     */
    void attach(SS7SCCP* sccp);

    /**
     * Process a notification from MTP about a pointcode status
     * @param link The affected link
     * @param operational True if the layer3 is operational
     */
    virtual void pointcodeStatus(SS7Layer3* link, bool operational);

    /**
     * Process a notification from router about a route state change
     * @param type The Point Code type
     * @param node The remote pointcode
     * @param state The route state
     */
    virtual void routeStatus(SS7PointCode::Type type, const SS7PointCode& node, SS7Route::State state);

    /**
     * Notification from sccp about local subsystems status
     * @param type The type of notification
     * @param params The notification parameters
     */
    virtual void notify(SCCP::Type type, NamedList& params);

    /**
     * Method called by SCCP to inform management that no route was found for the message
     * @param msg The SCCP message that failed to be routed
     */
    virtual void routeFailure(SS7MsgSCCP* msg);

    /**
     * Method called by sccp when a sccp message hasn't been processed by any user
     * @param msg The message
     * @param label The mtp routing label
     */
    virtual void subsystemFailure(SS7MsgSCCP* msg, const SS7Label& label);

    /**
     * Notification from layer3 about a remote sccp unavailability
     * @param pointcode The poincode of the unavailable sccp
     * @param cause Unavailability cause
     */
    virtual void sccpUnavailable(const SS7PointCode& pointcode, unsigned char cause);

    /**
     * Helper method used to obtain a string statistic about the messages received for unknown subsystems
     * @param dest The string where the statistics will be stored
     * @param extended True to print an extended statistic ( the number of messages received for unknown subsystem)
     */
    void subsystemsStatus(String& dest,bool extended = true);

    /**
     * Helper method used to obtain information about the messages that failed to be routed
     * @param dest The destination string
     * @param extended True to print the GTT failures
     */
    void routeStatus(String& dest,bool extended = false);

    /**
     * Helper method used to notify the concerned signalling points about a subsystem status
     * @param msg The message type to broadcast
     * @param ssn Local affected ssn
     * @param smi Local subsystem multiplicity indicator
     */
    virtual void notifyConcerned(MsgType msg, unsigned char ssn, int smi);

    /**
     * Obtain broadcast type dict table
     * @return Pointer to broadcast type dict table
     */
    static const TokenDict* broadcastType();

    /**
     * Helper method used to inform Global Title Translator to update translation tables
     * @param rsccp The remote SCCP witch status has been changed
     * @param ssn The remote SCCP subsystem witch status has been changed
     */
    virtual void updateTables(SccpRemote* rsccp, SccpSubsystem* ssn = 0);

    /**
     * Print a sccp management message
     * @param dest The destination string
     * @param type The sccp management message type
     * @param params List of sccp management message parameters
     */
    virtual void printMessage(String& dest, MsgType type, const NamedList& params);

    /**
     * Obtain a sccp management state name
     * @param state The sccp management enum state
     * @return The state name if found or 0
     */
    static const char* stateName(SCCPManagement::SccpStates state)
	{ return lookup(state,s_states); }
protected:

    /**
     * Method called periodically by engine to check for timeouts
     * @param when Time to use as computing base for events and timeouts
     * Reimplemented from SignallingComponent
     */
    virtual void timerTick(const Time& when);

    inline SS7SCCP* sccp()
	{ return m_sccp; }

    ObjList m_remoteSccp;
    ObjList m_statusTest;
    ObjList m_localSubsystems;
    ObjList m_concerned;
    SS7PointCode::Type m_pcType;

    /**
     * Obtain the subsystem status test time interval
     * @return Subsystem status test duration
     */
    inline u_int32_t getTestTimeout()
	{ return m_testTimeout; }

    /**
     * Broadcast a management message to local attached sccp users
     * @param type The broadcast type
     * @param params List of parameters
     * @return True if at least one user has processed the message
     */
    bool managementMessage(SCCP::Type type, NamedList& params);

    /**
     * Obtain a local subsystem
     * @param ssn The local subsystem ssn
     * @return Pointer to local subsystem if found, 0 otherwise
     */
    SccpLocalSubsystem* getLocalSubsystem(unsigned char ssn);

    /**
     * Obtain a remote SCCP
     * @param pointcode The remote sccp pointcode
     * @return The remote SCCP with the matching pointcode or 0 if not found
     */
    SccpRemote* getRemoteSccp(int pointcode);

    /**
     * Encode a sccp management message and send it to remote address
     * @param msgType The SCCP management message type
     * @param params List of message parameters
     * @return True if the message was successfully send
     */
    virtual bool sendMessage(SCCPManagement::MsgType msgType, const NamedList& params) = 0;

    /**
     * Stop subsystem status tests for a remote location
     * @param remoteSccp The remote sccp
     * @param rSubsystem The remote subsystem. Can be 0 to stop all tests for the remote sccp
     * @param less Stop all sst except this
     */
    virtual void stopSst(SccpRemote* remoteSccp, SccpSubsystem* rSubsystem = 0, SccpSubsystem* less = 0);

    /**
     * Stop all subsystem status tests
     */
    inline void stopSSTs()
	{ Lock lock(this); m_statusTest.clear(); }

    /**
     * Start a new subsystem status test
     * @param remoteSccp The remote sccp
     * @param rSubsystem The remote subsystem
     */
    virtual void startSst(SccpRemote* remoteSccp, SccpSubsystem* rSubsystem);

    /**
     * Notification from sccp that mtp has finished restarting
     */
    void mtpEndRestart();

    /**
     * Send a local sccp broadcast
     * @param type The broadcast message type
     * @param pointcode The affected pointcode. -1 if it should not be included
     * @param sps The signalling point status.  -1 if it should not be included
     * @param rss The remote sccp status. -1 if it should not be included
     * @param rl The restriction level. -1 if it should not be included
     * @param ssn The affected ssn. -1 if it should not be included
     * @param ss The subsystem status. -1 if it should not be included
     */
    void localBroadcast(SCCP::Type type, int pointcode, int sps, int rss = -1,
	    int rl = -1, int ssn = -1, int ss = -1);

    /**
     * Helper method. Send subsystem status test
     * Note: Management mutex must not be locked. Thread safe
     * @param remote The remote sccp
     * @param sub The remote subsystem
     */
    bool sendSST(SccpRemote* remote, SccpSubsystem* sub);

    /**
     * Process a sccp management message
     * @param msgType The sccp management message type
     * @param ssn The affected subsystem
     * @param smi The subsystem multiplicity indicator
     * @param params The message params
     * @return True if the message was handled
     */
    bool handleMessage(int msgType, unsigned char ssn, unsigned char smi, NamedList& params);

    /**
     * Process remote sccp state
     * @param rsccp The remote sccp witch state has changed
     * @param newState The new state of the remote sccp
     */
    virtual void manageSccpRemoteStatus(SccpRemote* rsccp, SS7Route::State newState)
	{ }

    /**
     * Helper method used to check if we should print sccp management messages
     * @return True if we should print messages
     */
    inline bool printMessagess()
	{ return m_printMessages; }

    /**
     * Helper method that handles coordinate request
     * @param ssn Local subsystem that wish to go out of service
     * @param smi Subsystem multiplicity indicator
     * @param params List of parameters
     */
    void handleCoordinateChanged(unsigned char ssn, int smi, const NamedList& params);

    /**
     * Handle a subsystem out of service grant message
     * @param ssn Remote subsystem ssn
     * @param pointcode Remote subsystem pointcode
     * Note! Lock management mutex before calling this method
     */
    void handleSog(unsigned char ssn, int pointcode);

    /**
     * Process the status of subsystems
     * @param subsystem The subsystem who's status has changed
     * @param allowed True if the subsystem status is Allowed false for Prohibited
     * @param remote The remote sccp pointcode where the subsystem is located
     * @param smi Subsystem Multiplicity Indicator
     */
    virtual void handleSubsystemStatus(SccpSubsystem* subsystem, bool allowed, SccpRemote* remote, int smi)
	{ }

    /**
     * Ontain the coordinate changed time interval
     * @return The coordinate time interval in ms
     */
    inline u_int32_t getCoordTimeout()
	{ return m_coordTimeout; }

    /**
     * Obtain ignore status tests time interval
     * @return ignore status tests time interval in ms
     */
    inline u_int32_t getIgnoreTestsInterval()
	{ return m_ignoreStatusTestsInterval; }
private:
    // Helper method to fill broadcast param list
    void putValue(NamedList& params,int val,const char* name, bool dict = false);

    SS7SCCP* m_sccp;
    NamedList m_unknownSubsystems;
    unsigned int m_subsystemFailure;    // Counter used in status to inform about the total number of packets received for a unknown ssn
    unsigned int m_routeFailure;
    u_int32_t m_testTimeout;
    u_int32_t m_coordTimeout;
    u_int32_t m_ignoreStatusTestsInterval;
    bool m_autoAppend;
    bool m_printMessages;
    static const TokenDict s_broadcastType[];
    static const TokenDict s_states[];
};

class YSIG_API SS7MsgSCCP : public SignallingMessage
{
    YCLASS(SS7MsgSCCP,SignallingMessage)
public:
    /**
     * SCCP Message type
     */
    enum Type {
	Unknown = 0,
	CR     = 0x01, // Connection request
	CC     = 0x02, // Connection confirm
	CREF   = 0x03, // Connection refused
	RLSD   = 0x04, // Released
	RLC    = 0x05, // Release complete
	DT1    = 0x06, // Data form 1
	DT2    = 0x07, // Data form 2
	AK     = 0x08, // Data acknowledgement
	UDT    = 0x09, // Unitdata
	UDTS   = 0x0a, // Unitdata service
	ED     = 0x0b, // Expedited data
	EA     = 0x0c, // Expedited data acknowledgement
	RSR    = 0x0d, // Reset request
	RSC    = 0x0e, // Reset confirmation
	ERR    = 0x0f, // Protocol data unit error
	IT     = 0x10, // Inactivity test
	XUDT   = 0x11, // Extended unitdata
	XUDTS  = 0x12, // Extended unitdata service
	LUDT   = 0x13, // Long unitdata
	LUDTS  = 0x14, // Long unitdata service
    };

    enum Parameters {
	EndOfParameters                = 0,
	DestinationLocalReference      = 0x01,
	SourceLocalReference           = 0x02,
	CalledPartyAddress             = 0x03,
	CallingPartyAddress            = 0x04,
	ProtocolClass                  = 0x05,
	Segmenting                     = 0x06,
	ReceiveSequenceNumber          = 0x07,
	Sequencing                     = 0x08,
	Credit                         = 0x09,
	ReleaseCause                   = 0x0a,
	ReturnCause                    = 0x0b,
	ResetCause                     = 0x0c,
	ErrorCause                     = 0x0d,
	RefusalCause                   = 0x0e,
	Data                           = 0x0f,
	Segmentation                   = 0x10,
	HopCounter                     = 0x11,
	Importance                     = 0x12, // ITU only
	LongData                       = 0x13,
	MessageTypeInterworking        = 0xf8, // ANSI only
	INS                            = 0xf9, // ANSI only
	ISNI                           = 0xfa, // ANSI only
    };

    /**
     * Constructor
     * @param type Type of SCCP message as enumeration
     */
    inline SS7MsgSCCP(Type type)
	: SignallingMessage(lookup(type,"Unknown")), m_type(type), m_data(0)
	{ }

    /**
     * Destructor
     * NOTE: The SCCP message does not own the data pointer
     * In destructor the data pointer should be valid if data was set from decode message
     * In any other cases the pointer should be 0
     * NOTE: The data is not destroyed!! Only removed from data object and after the data object is destroyed
     */
    virtual ~SS7MsgSCCP();

    /**
     * Get the type of this message
     * @return The type of this message as enumeration
     */
    inline Type type() const
	{ return m_type; }

    /**
     * Helper method to change the message type
     * @param type The new message type
     */
    inline void updateType(Type type)
	{ m_type = type; params().assign(lookup(type,"Unknown")); }

    /**
     * Utility method to verify if this message is a long unit data
     * @return True if this message is a long unit data
     */
    inline bool isLongDataMessage() const
	{ return m_type == LUDT || m_type == LUDTS; }

    /**
     * Utility method to verify if this message can be a UDT message
     * A SCCP message can be an UDT message if it not contains HopCounter parameter
     * or other optional parameters
     * @return True if this message can be a UDT message
     */
    inline bool canBeUDT() const
	{ return !(params().getParam(YSTRING("Importance")) ||
		    params().getParam(YSTRING("HopCounter"))); }

    /**
     * Fill a string with this message's parameters for debug purposes
     * @param dest The destination string
     * @param label The routing label
     * @param params True to add parameters
     * @param raw Optional raw message data to be added to destination
     * @param rawLen Raw data length
     */
    void toString(String& dest, const SS7Label& label, bool params,
	const void* raw = 0, unsigned int rawLen = 0) const;

    /**
     * Get the dictionary with the message names
     * @return Pointer to the dictionary with the message names
     */
    static const TokenDict* names();

    /**
     * Convert an SCCP message type to a C string
     * @param type Type of SCCP message to look up
     * @param defvalue Default string to return
     * @return Name of the SCCP message type
     */
    static inline const char* lookup(Type type, const char* defvalue = 0)
	{ return TelEngine::lookup(type,names(),defvalue); }

    /**
     * Look up an SCCP message name
     * @param name String name of the SCCP message
     * @param defvalue Default type to return
     * @return Encoded type of the SCCP message
     */
    static inline Type lookup(const char* name, Type defvalue = Unknown)
	{ return static_cast<Type>(TelEngine::lookup(name,names(),defvalue)); }

    /**
     * Set data for this message
     * @param data the data
     */
    inline void setData(DataBlock* data)
	{ m_data = data; }

    /**
     * Remove the data from this message
     */
    inline void removeData()
	{ m_data = 0; }

    /**
     * Obtain the data associated with this message
     * @return The data
     */
    inline DataBlock* getData()
	{ return m_data; }

    /**
     * Extract the data associated with this message
     * @return The data
     */
    inline DataBlock* extractData()
	{
	    DataBlock* data = m_data;
	    m_data = 0;
	    return data;
	}

private:
    Type m_type;                         // Message type
    DataBlock* m_data;                   // Message data NOTE: The message never owns the data
};

class YSIG_API SS7MsgSccpReassemble : public SS7MsgSCCP
{
    YCLASS(SS7MsgSccpReassemble,SignallingMessage)
public:
    enum Return {
	Rejected,
	Accepted,
	Error,
	Finished,
    };
    /**
     * Constructor
     * @param msg The first message segment
     * @param label The MTP routing label
     * @param timeToLive The time internal in milliseconds that we wait to reassemble the message
     */
    SS7MsgSccpReassemble(SS7MsgSCCP* msg, const SS7Label& label, unsigned int timeToLive);

    /**
     * Destructor
     */
    virtual ~SS7MsgSccpReassemble();

    /**
     * Helper method used to check if the given message is part of this reassembling process
     * @param msg The message to verify
     * @param label The SS7 routing label
     * @return True if msg is a part of this reassembling process
     */
    bool canProcess(const SS7MsgSCCP* msg, const SS7Label& label);

    /**
     * Append a sccp message segment to the main message
     * @param msg The message segment
     * @param label The SS7 routing label
     * @return One of the Return enum options
     */
    Return appendSegment(SS7MsgSCCP* msg, const SS7Label& label);

    /**
     * Check if this reassemble process has expired
     * @return True if this reassemble process has expired
     */
    inline bool timeout()
	{ return m_timeout > 0 ? Time::msecNow() > m_timeout : false; }

    /**
     * Helper method to verify if all segments have arrived
     * @return True if all segments arrived
     */
    inline bool haveAllSegments()
	{ return m_remainingSegments == 0; }
private:
    SS7Label m_label;
    NamedList m_callingPartyAddress;
    u_int32_t m_segmentationLocalReference;
    u_int64_t m_timeout;
    unsigned char m_remainingSegments;
    unsigned int m_firstSgmDataLen;
};

class YSIG_API SccpSubsystem : public RefObject
{
    YCLASS(SccpSubsystem,RefObject);
public:
    /**
     * Constructor
     * @param ssn The subsystem number alocated to this subsystem
     * @param state The subsystem initial state
     * @param smi The subsystem multiplicity indicator
     */
    inline SccpSubsystem(int ssn, SCCPManagement::SccpStates state = SCCPManagement::Allowed, unsigned char smi = 0)
	: m_ssn(ssn), m_smi(smi), m_state(state)
	{ }

    virtual ~SccpSubsystem()
	{ }

    /**
     * Obtain subsystem number
     * @return The subsystem number
     */
    inline unsigned char getSSN()
	{ return m_ssn; }

    /**
     * Obtain the state of this subsystem
     * @return This subsystem state
     */
    inline SCCPManagement::SccpStates getState()
	{ return m_state; }

    /**
     * Set the state of this subsystem
     * @param state The new state
     */
    inline void setState(SCCPManagement::SccpStates state)
	{ m_state = state; }

    /**
     * Obtain the subsystem multiplicity indicator of this sccp subsystem
     * @return The subsystem multiplicity indicator
     */
    inline unsigned char getSmi()
	{ return m_smi; }

    /**
     * Dump this sccp subsystem status
     * @param dest Destination string
     */
    void dump(String& dest)
	{
	    dest << "Subsystem: " << m_ssn << " , smi: " << m_smi;
	    dest << ", state: " << SCCPManagement::stateName(m_state) << " ";
	}
private:
    unsigned char m_ssn;
    unsigned char m_smi;
    SCCPManagement::SccpStates m_state;
};

class YSIG_API RemoteBackupSubsystem : public GenObject
{
    YCLASS(RemoteBackupSubsystem,GenObject);
public:

    /**
     * Constructor
     * @param ssn Remote subsystem number
     * @param pointcode Remote pointcode
     * @param wfg True if we are expecting SOG from this remote subsystem
     */
    RemoteBackupSubsystem(unsigned char ssn, int pointcode, bool wfg = false)
	: m_ssn(ssn), m_pointcode(pointcode), m_waitForGrant(wfg)
	{ }

    virtual ~RemoteBackupSubsystem()
	{ }

    /**
     * Helper method used to verify if a remore subsystem match this one
     * @param ssn Remote subsystem number
     * @param pointcode Remote pointcode
     * @return True if pointcode and ssn match
     */
    inline bool equals(unsigned char ssn, int pointcode)
	{ return m_pointcode == pointcode && m_ssn == ssn; }

    /**
     * Helper method to reset wait for grant flag
     */
    inline void permisionGranted()
	{ m_waitForGrant = false; }

    /**
     * Check if we are steel waiting to receive SOG
     * @return True if SOG message has arrived from the remote subsystem
     */
    inline bool waitingForGrant()
	{ return m_waitForGrant; }

private:
    unsigned char m_ssn;
    int m_pointcode;
    bool m_waitForGrant;
};

class YSIG_API SccpLocalSubsystem : public RefObject, public Mutex
{
    YCLASS(SccpLocalSubsystem,RefObject);
public:
    /**
     * Constructor
     * @param ssn The subsystem number
     * @param coordInterval The time interval for coordinate changed timer
     * @param istInterval The time interval for ignore status test timer
     * @param smi Subsystem multiplicity indicator
     */
    SccpLocalSubsystem(unsigned char ssn, u_int64_t coordInterval, u_int64_t istInterval, unsigned char smi = 0);

    /**
     * Destructor
     */
    virtual ~SccpLocalSubsystem();

    /**
     * Obtain the subsystem number number of this sccp subsystem
     * @return The ssn associated with this subsystem
     */
    inline unsigned char getSSN()
	{ return m_ssn; }

    /**
     * Set a new state of this SCCP subsystem
     * @param newState Thew new state to set
     */
    inline void setState(SCCPManagement::SccpStates newState)
	{ m_state = newState; }

    /**
     * Obtain the state associated with this sccp subsystem
     * @return The state of this SCCP subsystem
     */
    inline SCCPManagement::SccpStates getState()
	{ return m_state; }

    /**
     * Start coordinate change timer
     */
    inline void startCoord()
	{ m_coordTimer.start(); }

    /**
     * Check if this subsystem should ignore SST (Subsystem status test)
     */
    inline bool ignoreTests()
	{ return m_ignoreTestsTimer.started(); }

    /**
     * Inform this subsystem if should ignore subsystem status tests
     * @param ignore True to ignore subsystem status tests
     */
    void setIgnoreTests(bool ignore);

    /**
     * Check if coordinate change timer has timed out
     * @return True if coordinate change timer has timed out
     */
    bool timeout();

    /**
     * Handle coord timer timeout
     * @param mgm Pointer to sccp management who owns this sccp local subsystem
     */
    void manageTimeout(SCCPManagement* mgm);

    /**
     * Stop coordinate change timer
     */
    inline void stopCoordTimer()
	{ m_coordTimer.stop(); }

    /**
     * Obtain the subsystem multiplicity indicator of this subsystem
     * @return The Subsystem multiplicity indicator
     */
    inline unsigned char getSmi()
	{ return m_smi; }

    /**
     * Dump this sccp subsystem status
     * @param dest Destination string
     */
    void dump(String& dest);

    /**
     * Process a subsystem out of service grant message
     * @param ssn The remote ssn
     * @param pointcode The remote pointcode
     * @return True if the message was procesed
     */
    bool receivedSOG(unsigned char ssn, int pointcode);

    /**
     * Helper method used to reset timers
     */
    inline void resetTimers()
	{ m_coordTimer.stop(); m_ignoreTestsTimer.stop(); }
    /**
     * Clear remote backup subsystems
     */
    inline void clearBackups()
	{
	    Lock lock(this);
	    m_backups.clear();
	}

    /**
     * Append new backup subsystem
     * @param backup The backup subsystem to append
     */
    inline void appendBackup(RemoteBackupSubsystem* backup)
	{
	    Lock lock(this);
	    m_backups.append(backup);
	}
private:
    unsigned char m_ssn;
    unsigned char m_smi;
    SCCPManagement::SccpStates m_state;
    SignallingTimer m_coordTimer;
    SignallingTimer m_ignoreTestsTimer;
    ObjList m_backups;
    bool m_receivedAll;
};

/**
 * Helper class to keep a remote sccp
 */
class YSIG_API SccpRemote : public RefObject, public Mutex
{
    YCLASS(SccpRemote,RefObject);
public:
    /**
     * Constructor
     * @param pcType The pointcode type
     */
    SccpRemote(SS7PointCode::Type pcType);

    /**
     * Constructor.
     * Construncot an Remote sccp from given pointcode and pointcode type
     * @param pointcode Integer value assigned to remote pointcode.
     * @param pcType Remote pointcode type
     */
    SccpRemote(unsigned int pointcode, SS7PointCode::Type pcType);

    /**
     * Destructor
     */
    virtual ~SccpRemote();

    /**
     * Initialize the pointcode and subsystems list from a string
     * @param params String containing the pointcode and the subsystems list
     * @return False if the pointcode from the string is not valid
     * Usage
     */
    bool initialize(const String& params);

    /**
     * Obtain the state of this remote SCCP
     * @return Remote SCCP state
     */
    inline SCCPManagement::SccpStates getState()
	{ return m_state; }

    /**
     * Find a subsystem stored in remote subsystems list
     * @param ssn The subsystem number of the remote subsystem
     * @return Pointer to the Subsystem or 0 if it was not found
     */
    SccpSubsystem* getSubsystem(int ssn);

    /**
     * Set remote SCCP state
     * @param state The new state of the remote SCCP
     */
    void setState(SCCPManagement::SccpStates state);

    /**
     * Obtain the PointCode of the remote SCCP
     * @return The pointcode of the remote SCCP
     */
    inline const SS7PointCode& getPointCode()
	{ return m_pointcode; }

    /**
     * Obtain the pointcode as an integer
     * @return The packed pointcode representation
     */
    inline int getPackedPointcode()
	{ return m_pointcode.pack(m_pointcodeType); }

    /**
     * Obtain a string representation of the remote pointcode type
     * @return String representation of remote pointcode type
     */
    inline const char* getPointCodeType()
	{ return SS7PointCode::lookup(m_pointcodeType); }

    /**
     * Dump this sccp status an all it's subsystems
     * @param dest Destination string
     * @param extended True to append the subsystems status
     */
    void dump(String& dest, bool extended = false);

    /**
     * Helper method to change a subsystem state
     * @param ssn The subsystem ssn
     * @param newState The subsystem new state
     * @return False if the subsystem state is the same
     */
    bool changeSubsystemState(int ssn,SCCPManagement::SccpStates newState);

    /**
     * Helper method to obtain remote sccp's subsystems list
     * @return The subsystems list
     */
    inline ObjList& getSubsystems()
	{ return m_subsystems; }

    /**
     * Helper method to set congestion level
     * @param cl The new congestion level
     */
    inline void setCongestion(unsigned int cl)
	{ m_congestionLevel = cl; }

    /**
     * Helper method to reset congestion level
     */
    inline void resetCongestion()
	{ m_congestionLevel = 0; }

    /**
     * Helper method to obtain the congestion level
     * @return The congestion level
     */
    inline unsigned int getCongestion()
	{ return m_congestionLevel; }
private:
    SS7PointCode m_pointcode;
    SS7PointCode::Type m_pointcodeType;
    ObjList m_subsystems;
    SCCPManagement::SccpStates m_state;
    unsigned int m_congestionLevel;
};

class YSIG_API SS7AnsiSccpManagement : public SCCPManagement
{
    YCLASS(SS7AnsiSccpManagement,SCCPManagement)
public:

    /**
     * Constructor
     */
    inline SS7AnsiSccpManagement(const NamedList& params)
	: SCCPManagement(params,SS7PointCode::ANSI)
	{ }

    virtual ~SS7AnsiSccpManagement();

    /**
     * Process a management message received from sccp
     * @param message The message to process
     * @return True if the message was processed successfully
     */
    virtual bool processMessage(SS7MsgSCCP* message);

    /**
     * Encode a sccp management message and send it to remote address
     * @param msgType The SCCP management message type
     * @param params List of message parameters
     * @return True if the message was successfully send
     */
    virtual bool sendMessage(SCCPManagement::MsgType msgType, const NamedList& params);

    /**
     * Handle notifications received from remote concerned sccp's
     * @param rsccp Remote SCCP pointcode
     * @param newState The new state of the remote SCCP
     */
    virtual void manageSccpRemoteStatus(SccpRemote* rsccp, SS7Route::State newState);

    /**
     * Process the status of subsystems
     * @param subsystem The subsystem who's status has changed
     * @param allowed True if the subsystem status is Allowed false for Prohibited
     * @param remote The remote sccp pointcode where the subsystem is located
     * @param smi Subsystem Multiplicity Indicator
     */
    virtual void handleSubsystemStatus(SccpSubsystem* subsystem, bool allowed, SccpRemote* remote, int smi);

    /**
     * Handle a SCCP Management message
     * @param msgType The message type
     * @param params List of message parameters
     * @return True if the message was handled
     */
    bool handleMessage(int msgType, NamedList& params);
};

class YSIG_API SS7ItuSccpManagement : public SCCPManagement
{
    YCLASS(SS7ItuSccpManagement,SCCPManagement)
public:

    /**
     * Constructor
     */
    SS7ItuSccpManagement(const NamedList& params);

    /**
     * Destructor
     */
    virtual ~SS7ItuSccpManagement()
	{ }

    /**
     * Process a management message received from sccp
     * @param message The message to process
     * @return True if the message was processed successfully
     */
    virtual bool processMessage(SS7MsgSCCP* message);

    /**
     * Encode and send a SCCP ITU management message
     * @param msgType The type of sccp management message
     * @param params List of parameters
     * @return True if the message was successfully sent
     */
    virtual bool sendMessage(MsgType msgType, const NamedList& params);

    /**
     * Handle notifications received from remote concerned sccp's
     * @param rsccp Remote SCCP pointcode
     * @param newState The new state of the remote SCCP
     */
    virtual void manageSccpRemoteStatus(SccpRemote* rsccp, SS7Route::State newState);

    /**
     * Handle a SCCP Management message
     * @param msgType The message type
     * @param params List of message parameters
     * @return True if the message was handled
     */
    bool handleMessage(int msgType, NamedList& params);

    /**
     * Process the status of subsystems
     * @param subsystem The subsystem who's status has changed
     * @param allowed True if the subsystem status is Allowed false for Prohibited
     * @param remote The remote sccp pointcode where the subsystem is located
     * @param smi Subsystem Multiplicity Indicator
     */
    virtual void handleSubsystemStatus(SccpSubsystem* subsystem, bool allowed, SccpRemote* remote, int smi);

};

/**
 * Helper class to memorize SCCP data segments
 */
class SS7SCCPDataSegment : public GenObject
{
    YCLASS(SS7SCCPDataSegment,GenObject)
public:
    /**
     * Constructor
     * @param index The index in the original DataBlock where this segment starts
     * @param length The length of this segment
     */
    inline SS7SCCPDataSegment(unsigned int index, unsigned int length)
	: m_length(length), m_index(index)
	{}

    /**
     * Destructor
     */
    virtual ~SS7SCCPDataSegment()
	{}

    /**
     * Assignees to a DataBlock this segment's data
     * @param temp The destination DataBlock segment
     * @param orig The original DataBlock where this segment is located
     */
    inline void fillSegment(DataBlock& temp, const DataBlock& orig)
	{ temp.assign(orig.data(m_index,m_length),m_length,false); }

private:
    unsigned int m_length;
    unsigned int m_index;
};

/**
 * Implementation of SS7 Signalling Connection Control Part
 * @short SS7 SCCP implementation
 */
class YSIG_API SS7SCCP : public SS7Layer4, public SCCP, public Mutex
{
    YCLASS(SS7SCCP,SCCP)
    friend class SCCPManagement;
public:
    enum ReturnCauses {
	NoTranslationAddressNature         = 0x00,
	NoTranslationSpecificAddress       = 0x01,
	SubsystemCongestion                = 0x02,
	SubsystemFailure                   = 0x03,
	UnequippedUser                     = 0x04,
	MtpFailure                         = 0x05,
	NetworkCongestion                  = 0x06,
	Unqualified                        = 0x07,
	ErrorInMessageTransport            = 0x08,
	ErrorInLocalProcessing             = 0x09,
	DestinationCanNotPerformReassembly = 0x0a,
	SccpFailure                        = 0x0b,
	HopCounterViolation                = 0x0c,
	SegmentationNotSupported           = 0x0d,
	SegmentationFailure                = 0x0e,
	// ANSI only
	MessageChangeFailure               = 0xf7,
	InvalidINSRoutingRequest           = 0xf8,
	InvalidISNIRoutingRequest          = 0xf9,
	UnauthorizedMessage                = 0xfa,
	MessageIncompatibility             = 0xfb,
	NotSupportedISNIRouting            = 0xfc,
	RedundantISNIConstrainedRouting    = 0xfd,
	ISNIIdentificationFailed           = 0xfe,
    };

    enum Control {
	Status                    = 0x01,
	FullStatus                = 0x02,
	EnableExtendedMonitoring  = 0x03,
	DisableExtendedMonitoring = 0x04,
	EnablePrintMsg            = 0x05,
	DisablePrintMsg           = 0x06,
    };
    /**
     * Constructor
     */
     SS7SCCP(const NamedList& config);

    /**
      * Destructor
      */
     ~SS7SCCP();

    /**
     * Configure and initialize the Signalling connection control part
     * @param config Optional configuration parameters override
     * @return True if SCCP was initialized properly
     */
    virtual bool initialize(const NamedList* config);

    /**
     * Attach a SS7 network or router to this service. Detach itself from the old one
     * @param network Pointer to network or router to attach
     */
    virtual void attach(SS7Layer3* network);

    /**
     * Converts an SCCP message to a Message Signal Unit and push it down the protocol stack.
     * The given message is consumed
     * @param msg The message to send
     * @param local True if the message is local initiated
     * @return Link the message was successfully queued to, negative for error
     */
    int transmitMessage(SS7MsgSCCP* msg, bool local = false);

    /**
      * Receive management information from attached users.
      * @param type The type of management message
      * @param params List of parameters (Affected subsystem [M])
      * @return True if the notification was processed
      */
     virtual bool managementStatus(Type type, NamedList& params);

    /**
     * Send a message
     * Reimplemented from SCCP
     * @param data Data to be transported trough SCCP protocol
     * @param params SCCP parameters
     */
    virtual int sendMessage(DataBlock& data, const NamedList& params);

    /**
     * Process a MSU received from the Layer 3 component
     * @param msu Message data, starting with Service Indicator Octet
     * @param label Routing label of the received MSU
     * @param network Network layer that delivered the MSU
     * @param sls Signalling Link the MSU was received from
     * @return Result of MSU processing
     */
    virtual HandledMSU receivedMSU(const SS7MSU& msu, const SS7Label& label, SS7Layer3* network, int sls);

    /**
     * Notification for receiving User Part Unavailable
     * @param type Type of Point Code
     * @param node Node on which the User Part is unavailable
     * @param part User Part (service) reported unavailable
     * @param cause Unavailability cause - Q.704 15.17.5
     * @param label Routing label of the UPU message
     * @param sls Signaling link the UPU was received on
     */
    virtual void receivedUPU(SS7PointCode::Type type, const SS7PointCode node,
	SS7MSU::Services part, unsigned char cause, const SS7Label& label, int sls);

    virtual bool control(NamedList& params);

    /**
     * Message changeover procedure for segmentation purpose
     * @param origMsg The original message
     * @param label MTP3 routing label
     * @param local True if the origMsg is local initiated
     * @return Negative value if the message failed to be sent
     */
    int segmentMessage(SS7MsgSCCP* origMsg, const SS7Label& label, bool local);

    /**
     * Helper method to know if we use ITU or ANSI
     */
     inline const bool ITU() const
	{ return m_type == SS7PointCode::ITU; }

    /**
     * Check if GT digit parser of should ignore unknown digits encoding
     * @return True if unknown digits are ignored
     */
    inline bool ignoreUnknownAddrSignals() const
	{ return m_ignoreUnkDigits; }

    /**
     * Process a notification generated by the attached network layer
     * @param link Network or linkset that generated the notification
     * @param sls Signalling Link that generated the notification, negative if none
     */
    virtual void notify(SS7Layer3* link, int sls);

    /**
     * Process route status changed notifications
     * @param type Type of Point Code
     * @param node Destination node witch communication status has changed
     * @param state The new route state
     */
    virtual void routeStatusChanged(SS7PointCode::Type type, const SS7PointCode& node, SS7Route::State state);
    /**
     * Obtain the number of messages send by this SCCP instance
     * @return The number of messages send
     */
    inline unsigned int messagesSend()
	{ return m_totalSent; }

    /**
     * Obtain the number of messages received by this SCCP instance
     * @return The number of messsages received
     */
    inline unsigned int messagesReceived()
	{ return m_totalReceived; }

    /**
     * Obtain the number of errors found by this SCCP instance
     * @return The number of errors found
     */
    inline unsigned int errors()
	{ return m_errors; }

    /**
     * Obtain the number of GT translations made by this SCCP instance
     * @return The number of translations made
     */
    inline unsigned int translations()
	{ return m_totalGTTranslations; }

    /**
     * Obtain the local SCCP point code
     * @return Pointer to local point code or 0 if no pointcode was configured
     */
    inline const SS7PointCode* getLocalPointCode() const
	{ return m_localPointCode; }

    /**
     * Obtain local pointcode type
     * @return Local pointcode type
     */
     inline SS7PointCode::Type getLocalPointCodeType()
	{ return m_type; }

    /**
     * Helper method to obtain the packed pointcode
     * @return Packed pointcode or 0 if local pointcode is not set
     */
    inline int getPackedPointCode()
	{ return m_localPointCode ? m_localPointCode->pack(m_type) : 0; }

    /**
     * Helper method to check if attached layer 3 is up
     * @return True if attached layer3 is up
     */
    inline bool isLayer3Up()
	{ return m_layer3Up; }
protected:

    /**
     * This method is called to clean up and destroy the object after the
     *  reference counter becomes zero
     */
    virtual void  destroyed();

    /**
     * Helper method to check if this sccp needs extended monitoring
     * @return True if extended monitoring is needed
     */
    inline bool extendedMonitoring()
	{ return m_extendedMonitoring; }

    /**
     * Method called periodically to check for timeouts
     * Reimplemented from SignallingComponent
     */
    virtual void timerTick(const Time& when);

    /**
     * Reassemble a message segment
     * @param segment The message segment
     * @param label The MTP routing label
     * @param msg Pointer to fill with the SS7MsgSccpReassemble who processed the message
     * @return SS7MsgSccpReassemble::Return enum value
     */
    SS7MsgSccpReassemble::Return reassembleSegment(SS7MsgSCCP* segment, const SS7Label& label, SS7MsgSCCP*& msg);

    /**
     * Check if this sccp is an endpoint
     * @return True if this sccp is an endpoint
     */
    virtual bool isEndpoint()
	{ return m_endpoint; }

    /**
     * Route a SCCP Message to other SCCP component.
     * @param msg The sccp message.
     * @return -1 if the message failed to be routed
     */
    int routeLocal(SS7MsgSCCP* msg);
private:
    // Helper method to calculate sccp address length
    unsigned int getAddressLength(const NamedList& params, const String& prefix);
    // Helper method used to ajust HopCounter and Importance parameters
    void ajustMessageParams(NamedList& params, SS7MsgSCCP::Type type);
    // Obtain maximum data length for a UDT, XUDT and LUDT message
    void getMaxDataLen(const SS7MsgSCCP* msg, const SS7Label& label,
	    unsigned int& udtLength, unsigned int& xudtLength, unsigned int& ludtLength);
    // Helper method to obtain data segments
    // NOTE The list must be destroyed
    ObjList* getDataSegments(unsigned int dataLength, unsigned int maxSegmentSize);
    // Helper method to print a SCCP message
    void printMessage(const SS7MSU* msu, const SS7MsgSCCP* msg, const SS7Label& label);
    // Helper method used to extract the pointcode from Calling/Called party address.
    // Also will call GT translate if there is no pointcode in called party address
    int getPointCode(SS7MsgSCCP* msg, const String& prefix, const char* pCode, bool translate);
    // Transmit a SCCP message
    int sendSCCPMessage(SS7MsgSCCP* sccpMsg,int dpc,int opc, bool local);
    // Helper method used to verify if the message is a connectionless data message
    inline bool isSCLCMessage(int msgType)
	{ return msgType == SS7MsgSCCP::UDT || msgType == SS7MsgSCCP::XUDT || msgType == SS7MsgSCCP::LUDT; }
    // Helper method used to verify if the message is a connectionless service message
    inline bool isSCLCSMessage(int msgType)
	{ return msgType == SS7MsgSCCP::UDTS || msgType == SS7MsgSCCP::XUDTS || msgType == SS7MsgSCCP::LUDTS; }
    bool isSCOCMsg(int msgType);

    bool fillLabelAndReason(String& dest, const SS7Label& label,const SS7MsgSCCP* msg);
    inline bool unknownPointCodeType()
	{ return m_type != SS7PointCode::ITU && m_type != SS7PointCode::ANSI && m_type != SS7PointCode::ANSI8; }
    // Helper method used to verify if th importance level is in standard range
    int checkImportanceLevel(int msgType,int initialImportance);
    // Helper method used to verify if the optional parameters present in message are declared in standards
    void checkSCLCOptParams(SS7MsgSCCP* msg);
    // Helper method used to monitor service messages
    void archiveMessage(SS7MsgSCCP* msg);
    // Helper method used to dump service messages and error codes status
    void dumpArchive(String& msg, bool extended);

    bool processMSU(SS7MsgSCCP::Type type, const unsigned char* paramPtr,
	    unsigned int paramLen, const SS7Label& label, SS7Layer3* network, int sls);

    bool decodeMessage(SS7MsgSCCP* msg, SS7PointCode::Type pcType,
	    const unsigned char* paramPtr, unsigned int paramLen);

    void returnMessage(SS7MsgSCCP* message, int error);

    static void switchAddresses(const NamedList& source, NamedList& dest);
    // Helper method to dump sccp status
    void printStatus(bool extended);
    void setNetworkUp(bool operational);

    SS7MSU* buildMSU(SS7MsgSCCP* msg, const SS7Label& label, bool checkLength = true) const;
    bool routeSCLCMessage(SS7MsgSCCP*& msg, const SS7Label& label);
    // Member data
    SS7PointCode::Type m_type;           // Point code type of this SCCP
    SS7PointCode* m_localPointCode;      // Local point code
    SCCPManagement* m_management;        // SCCP management
    ObjList m_reassembleList;            // List of sccp messages that are waiting to be reassembled
    u_int8_t m_hopCounter;               // Hop counter value
    NamedList m_msgReturnStatus;         // List with message return statistics
    u_int32_t m_segTimeout;              // Time in milliseconds for segmentation timeout
    bool m_ignoreUnkDigits;              // Check if GT digit parser of should ignore unknown digits encoding
    bool m_layer3Up;                     // Flag used to verify if the network is operational
    unsigned int m_maxUdtLength;         // The maximum length of data packet transported in a UDT message
    u_int32_t m_totalSent;               // Counter of the total number of SCCP messages sent
    u_int32_t m_totalReceived;           // The number of incoming sccp messages
    u_int32_t m_errors;                  // Counter of the number of messages that failed to be delivered
    u_int32_t m_totalGTTranslations;     // The number of GT translations made
    u_int32_t m_gttFailed;               // Number of global title that failed to be translated
    bool m_extendedMonitoring;           // Flag used to check if the monitoring is normal or extended
    const char* m_mgmName;               // The name of the sccp management section from ysigchan.conf
    // Debug flags
    bool m_printMsg;                     // Print messages to output
    bool m_extendedDebug;                // Extended debug flag
    bool m_endpoint;                     // Flag used to force message processing if the message have a ssn
};

/**
 * RFC3868 SS7 SCCP implementation over SCTP/IP
 * SUA is intended to be used as a Provider-User where real SCCP runs on a
 *  Signalling Gateway and SCCP users are located on an Application Server.
 * @short SIGTRAN SCCP User Adaptation Layer
 */
class YSIG_API SS7SUA : public SIGAdaptUser, public SCCP
{
};

/**
 * A TCAP message wrapper, encapsulates the data received from SCCP
 * @short TCAP message wrapper
 */
class YSIG_API SS7TCAPMessage : public GenObject
{
public:
    /**
     * Constructor
     * @param params NamedList reference containing information from the SCCP level
     * @param data DataBlock representing the TCAP payload
     * @param notice Flag if this is a notification, true if it is, false if it's a message
     */
    inline SS7TCAPMessage(NamedList& params, DataBlock& data, bool notice = false)
	: m_msgParams(params), m_msgData(data), m_notice(notice)
	{}

    /**
     * Get the SCCP parameters
     * @return NamedList reference containing information from the SCCP level
     */
    inline NamedList& msgParams()
	{ return m_msgParams; }

    /**
     * Get the TCAP message data
     * @return DataBlock representing the encoded TCAP message
     */
    inline DataBlock& msgData()
	{ return m_msgData; }

    /**
     * Is this message a notice or a normal message?
     * @return True if message is a notice, false otherwise
     */
    inline bool& isNotice()
	{ return m_notice; }

private:
    NamedList m_msgParams;
    DataBlock m_msgData;
    bool m_notice;
};

/**
 * Implementation of SS7 Transactional Capabilities Application Part
 * @short SS7 TCAP implementation
 */
class YSIG_API SS7TCAP : public SCCPUser
{
    YCLASS(SS7TCAP,SCCPUser)
public:
    /**
     * TCAP implementation variant
     */
    enum TCAPType {
	UnknownTCAP,
	ITUTCAP,
	ANSITCAP,
    };

    /**
     * Component handling primitives between TCAP and TCAP user (TC-user)
     */
    enum TCAPUserCompActions {
	TC_Invoke         = 1,        // ITU-T Invoke primitive, ANSI InvokeLast - Request/Indication
	TC_ResultLast     = 2,        // ITU-T & ANSI ResultLast primitive - Request/Indication
	TC_U_Error        = 3,        // ITU-T & ANSI ReturnError primitive - Request/Indication
	TC_U_Reject       = 4,        // ITU-T & ANSI Reject primitive - Request/Indication, TC-user rejected the component
	TC_R_Reject       = 5,        // ITU-T & ANSI Reject primitive - Indication, Remote TC-user rejected the component
	TC_L_Reject       = 6,        // ITU-T & ANSI Reject primitive - Indication, local Component Sublayer rejected the component
	TC_InvokeNotLast  = 7,        // ANSI InvokeNotLast primitive - Request/Indication
	TC_ResultNotLast  = 8,        // ITU-T & ANSI ResultNotLast primitive - Request/Indication
	TC_L_Cancel       = 9,        // Local Cancel primitive - Indication, inform TC-user that an operation has timed out
	TC_U_Cancel       = 10,       // User Cancel primitive - Request, TC-user request cancellation of an operation
	TC_TimerReset     = 11,       // Timer Reset - Indication, allow TC-user to refresh an operation timer
    };

    /**
     * TCAP message primitives
     */
    enum TCAPUserTransActions {
	TC_Unknown         = 0,
	TC_Unidirectional  = 1,          // ITU-T & ANSI - Request/Indication, request Unidirectional message
	TC_Begin,                   // ITU-T - Request/Indication, begin a dialogue
	TC_QueryWithPerm,           // ANSI - Request/Indication, begin a dialogue with permission to end it
	TC_QueryWithoutPerm,        // ANSI - Request/Indication, begin a dialogue without permission to end it
	TC_Continue,                // ITU-T - Request/Indication, continue a dialogue
	TC_ConversationWithPerm,    // ANSI - Request/Indication, continue a dialogue with permission
	TC_ConversationWithoutPerm, // ANSI - Request/Indication, continue a dialogue without permission
	TC_End,                     // ITU-T -Request/Indication, end a dialogue
	TC_Response,                // ANSI - Request/Indication, end a dialogue
	TC_U_Abort,                 // ITU-T & ANSI - Request/Indication, abort a dialogue per user's request
	TC_P_Abort,                 // ITU-T & ANSI - Indication, notify the abort of a dialogue because of a protocol error
	TC_Notice,                  // ITU-T & ANSI - Indication, notify the TC-user that the network was unable to provide the requested service
    };

    /**
     * Component Operation Classes
     */
    enum TCAPComponentOperationClass {
	SuccessOrFailureReport    = 1,
	FailureOnlyReport         = 2,
	SuccessOnlyReport         = 3,
	NoReport                  = 4,
    };

    /**
     * Type of message counters
     */
    enum TCAPCounter {
	IncomingMsgs,
	OutgoingMsgs,
	DiscardedMsgs,
	NormalMsgs,
	AbnormalMsgs,
    };

    /**
     * Constructor
     * @param params Parameters for building this TCAP
     */
    SS7TCAP(const NamedList& params);

    /**
     * Destructor
     */
    virtual ~SS7TCAP();

    /**
     * Configure and initialize the component and any subcomponents it may have
     * @param config Optional configuration parameters override
     * @return True if the component was initialized properly
     */
    virtual bool initialize(const NamedList* config);

    /**
     * Send a message to SCCP for transport, inherited from SCCPUser
     * @param data User data
     * @param params SCCP parameters
     */
    virtual bool sendData(DataBlock& data, NamedList& params);

    /**
     * Notification from SCCP that a message has arrived, inherited from SCCPUser
     * @param data Received user data
     * @param params SCCP parameters
     * @return True if this user has processed the message, false otherwise
     */
    virtual HandledMSU receivedData(DataBlock& data, NamedList& params);

    /**
     * Notification from SCCP that a message failed to arrive to its destination, inherited from SCCPUser
     * @param data User data sent.
     * @param params SCCP parameters
     * Note! The data may not contain the full message block previously sent (in case of SCCP segmentation),
     * but it must always must contain the first segment
     */
    virtual HandledMSU notifyData(DataBlock& data, NamedList& params);

    /**
     * Notification from SCCP layer about management status
     * @param type Type of management notification
     * @param params Notification params
     */
    bool managementNotify(SCCP::Type type, NamedList& params);

    /**
     * Attach a SS7 TCAP user
     * @param user Pointer to the TCAP user to attach
     */
    void attach(TCAPUser* user);

    /**
     * Detach a SS7 TCAP user
     * @param user TCAP user to detach
     */
    void detach(TCAPUser* user);

    /**
     * A TCAP user made a request
     * @param requestParams NamedList containing all the necessary data for the TCAP request
     * @return A SS7TCAPError reporting the status of the request
     */
    virtual SS7TCAPError userRequest(NamedList& requestParams);

    /**
     * Process received SCCP data
     * @param sccpData A TCAP message received from SCCP to process
     * @return A code specifying if this message was handled
     */
    virtual HandledMSU processSCCPData(SS7TCAPMessage* sccpData);

    /**
     * Report which TCAP implementation is in use
     */
    inline TCAPType tcapType()
	{ return m_tcapType; }

    /**
     * Set TCAP version
     * @param type TCAP version
     */
    inline void setTCAPType(TCAPType type)
	{ m_tcapType = type; }

    /**
     * Enqueue data received from SCCP as a TCAP message, kept in a processing queue
     * @param msg A SS7TCAPMessage pointer containing all data received from SSCP
     */
    virtual void enqueue(SS7TCAPMessage* msg);

    /**
     * Dequeue a TCAP message when ready to process it
     * @return A SS7TCAPMessage pointer dequeued from the queue
     */
    virtual SS7TCAPMessage* dequeue();

    /**
     * Get a new transaction ID
     * @return A transaction ID
     */
    virtual const String allocTransactionID();

    /**
     * Get a new transaction ID
     * @param str String into which to put the id
     */
    void allocTransactionID(String& str);

    /**
     * Dictionary for TCAP versions
     */
    static const TokenDict s_tcapVersion[];

    /**
     * Dictionary for component primitives
     */
    static const TokenDict s_compPrimitives[];

    /**
     * Dictionary for transaction primitives
     */
    static const TokenDict s_transPrimitives[];

    /**
     * Dictionary for component opearation classes
     */
    static const TokenDict s_compOperClasses[];

    /**
     * Build a transaction
     * @param type Type with which to build the transactions
     * @param transactID ID for the transaction
     * @param params Parameters for building the transaction
     * @param initLocal True if built by user, false if by remote end
     * @return A transaction
     */
    virtual SS7TCAPTransaction* buildTransaction(SS7TCAP::TCAPUserTransActions type, const String& transactID, NamedList& params,
	bool initLocal = true) = 0;

    /**
     * Find the transaction with the given id
     * @param tid Searched local id
     * @return A pointer to the transaction or null if not found
     */
    SS7TCAPTransaction* getTransaction(const String& tid);

    /**
     * Remove transaction
     * @param tr The transaction to remove
     */
    void removeTransaction(SS7TCAPTransaction* tr);

    /**
     * Method called periodically to do processing and timeout checks
     * @param when Time to use as computing base for events and timeouts
     */
    virtual void timerTick(const Time& when);

    /**
     * Send to TCAP users a decode message
     * @param params Message in NamedList form
     * @return True if the message was handled by a user, false otherwise
     */
    virtual bool sendToUser(NamedList& params);

    /**
     * Build SCCP data
     * @param params NamedList containing the parameters to be given to SCCP
     * @param tr Transaction for which to build SCCP data
     */
    virtual void buildSCCPData(NamedList& params, SS7TCAPTransaction* tr);

    /**
     * Status of TCAP
     * @param status NamedList to fill with status information
     */
    virtual void status(NamedList& status);

    /**
     * Status of TCAP users
     * @param status NamedList to fill with user status information
     */
    virtual void userStatus(NamedList& status);

    /**
     * Handle an decoding error
     * @param error The encoutered error
     * @param params TCAP message parameters which where successfully decoded until the error was encoutered
     * @param data Data block containing the rest of the message
     * @param tr Transaction to which this message belongs to
     * @return Status if the error was handled or not
     */
    virtual HandledMSU handleError(SS7TCAPError& error, NamedList& params, DataBlock& data, SS7TCAPTransaction* tr = 0);

    /**
     * Update the SCCP Management state for this SSN when requested by a user
     * @param user The TCAP user which changed its state
     * @param status The state of the TCAP user
     * @param params Additional parameters to be transmitted to the SCPP
     */
    virtual void updateUserStatus(TCAPUser* user, SCCPManagement::LocalBroadcast status, NamedList& params);

    /**
     * Increment one of the status counters
     * @param counterType The type of the counter to increment
     */
    inline void incCounter(TCAPCounter counterType)
    {
	switch (counterType) {
	    case IncomingMsgs:
		m_recvMsgs++;
		break;
	    case OutgoingMsgs:
		m_sentMsgs++;
		break;
	    case DiscardedMsgs:
		m_discardMsgs++;
		break;
	    case NormalMsgs:
		m_normalMsgs++;
		break;
	    case AbnormalMsgs:
		m_abnormalMsgs++;
		break;
	    default:
		break;
	}
    }

    /**
     * Retrieve one of the status counters
     * @param counterType The type of the counter to increment
     * @return The value of the counter
     */
    inline unsigned int count(TCAPCounter counterType)
    {
	switch (counterType) {
	    case IncomingMsgs:
		return m_recvMsgs;
	    case OutgoingMsgs:
		return m_sentMsgs;
	    case DiscardedMsgs:
		return m_discardMsgs;
	    case NormalMsgs:
		return m_normalMsgs;
	    case AbnormalMsgs:
		return m_abnormalMsgs;
	    default:
		break;
	}
	return 0;
    }

    /**
     * Get the type of transaction in string form
     * @param tr Type of transaction
     * @return A string containing the string form of that type of transaction
     */
    static inline const char* lookupTransaction(int tr)
	{ return lookup(tr,s_transPrimitives,"Unknown"); }

    /**
     * Get the type of transaction from string form
     * @param tr Type of transaction in string form
     * @return The type of transaction
     */
    static inline int lookupTransaction(const char* tr)
	{ return lookup(tr,s_transPrimitives,TC_Unknown); }

    /**
     * Get the type of component in string form
     * @param comp Type of component
     * @return A string containing the string form of that type of component
     */
    static inline const char* lookupComponent(int comp)
	{ return lookup(comp,s_compPrimitives,"Unknown"); }

    /**
     * Get the type of component from string form
     * @param comp Type of component
     * @return The type of component
     */
    static inline int lookupComponent(const char* comp)
	{ return lookup(comp,s_compPrimitives,TC_Unknown); }

protected:
    virtual SS7TCAPError decodeTransactionPart(NamedList& params, DataBlock& data) = 0;
    virtual void encodeTransactionPart(NamedList& params, DataBlock& data) = 0;
    bool sendSCCPNotify(NamedList& params);
    // list of TCAP users attached to this TCAP instance
    ObjList m_users;
    Mutex m_usersMtx;

    // list of messages received from sublayer, waiting to be processed
    ObjList m_inQueue;
    Mutex m_inQueueMtx;

    unsigned int m_SSN;
    unsigned int m_defaultRemoteSSN;
    unsigned int m_defaultHopCounter;
    SS7PointCode m_defaultRemotePC;
    SS7PointCode::Type m_remoteTypePC;
    u_int64_t m_trTimeout;

    // list of current TCAP transactions
    Mutex m_transactionsMtx;
    ObjList m_transactions;
    // type of TCAP
    TCAPType m_tcapType;

    // counter for allocating transaction IDs
    u_int32_t m_idsPool;

    // counters
    unsigned int m_recvMsgs;
    unsigned int m_sentMsgs;
    unsigned int m_discardMsgs;
    unsigned int m_normalMsgs;
    unsigned int m_abnormalMsgs;

    // Subsystem Status
    SCCPManagement::LocalBroadcast m_ssnStatus;
};

class YSIG_API SS7TCAPError
{
public:
    enum ErrorType {
	// P-AbortCause TransactionProblems
	Transact_UnrecognizedPackageType,     // named after the ANSI specification, equivalent to ITU-T UnrecongnizedMessageType P-AbortCause
	Transact_IncorrectTransactionPortion, // <==> ITU-T incorrectTrasactionPortion P-AbortCause
	Transact_BadlyStructuredTransaction,  // <==> ITU-T badlyFormattedTransactionPortion P-AbortCause
	Transact_UnassignedTransactionID,
	Transact_PermissionToReleaseProblem,  // HANDLING NOT DEFINED
	Transact_ResourceUnavailable,         // <==> ITU-T resourceLimitation P-AbortCause

	// P-AbortCause DialogProblem
	Dialog_UnrecognizedDialoguePortionID, // ANSI only
	Dialog_BadlyStructuredDialoguePortion,// ANSI only
	Dialog_MissingDialoguePortion,        // ANSI only
	Dialog_InconsistentDialoguePortion,   // ANSI only
	Dialog_Abnormal,                      // ITU only, indication only

	// GeneralProblem
	General_UnrecognizedComponentType,   // named after the ANSI specification, equivalent to ITU-T UnrecognizedComponent General Problem
	General_IncorrectComponentPortion,   // ANSI specification, equivalent to ITU-T MistypedComponent General Problem
	General_BadlyStructuredCompPortion,  // ANSI specification, equivalent to ITU-T BadlyStructuredComponent General Problem
	General_IncorrectComponentCoding,    // ANSI specification, no ITU-T equivalent

	// InvokeProblem
	Invoke_DuplicateInvokeID,           // ANSI & ITU-T specification
	Invoke_UnrecognizedOperationCode,   // ANSI specification, equivalent to ITU-T UnrecognizedOperation Invoke Problem
	Invoke_IncorrectParameter,          // ANSI specification, equivalent to ITU-T MistypedParameter Invoke Problem
	Invoke_UnrecognizedCorrelationID,   // ANSI specification, equivalent to ITU-T UnrecognizedLinkedID Invoke Problem
	Invoke_ResourceLimitation,          // ITU-T only
	Invoke_InitiatingRelease,           // ITU-T only
	Invoke_LinkedResponseUnexpected,    // ITU-T only
	Invoke_UnexpectedLinkedOperation,   // ITU-T only

	// ReturnResult
	Result_UnrecognizedInvokeID,        // ITU-T only
	Result_UnrecognisedCorrelationID,   // ANSI only
	Result_UnexpectedReturnResult,      // ANSI specification, equivalent to ITU-T ReturnResultUnexpected Result Problem
	Result_IncorrectParameter,          // ANSI specification, equivalent to ITU-T MistypedParameter Result Problem

	// ReturnError
	Error_UnrecognizedInvokeID,        // ITU-T only
	Error_UnrecognisedCorrelationID,   // ANSI only
	Error_UnexpectedReturnError,       // ANSI only
	Error_UnrecognisedError,           // ANSI & ITU-T
	Error_UnexpectedError,             // ANSI & ITU-T
	Error_IncorrectParameter,          // ANSI specification, equivalent to ITU-T MistypedParameter Return Error Problem

	Discard,
	NoError,
    };

    /**
     * Constructor
     * @param tcapType TCAP specification user for this error
     */
    SS7TCAPError(SS7TCAP::TCAPType tcapType);

    /**
     * Constructor
     * @param tcapType TCAP specification used for this error
     * @param error The error
     */
    SS7TCAPError(SS7TCAP::TCAPType tcapType, ErrorType error);

    /**
     * Destructor
     */
    ~SS7TCAPError();

    /**
     * Get the error
     * @return The TCAP error
     */
    inline ErrorType error()
	{ return m_error; }

    /**
     * Set the error
     * @param error Error to set
     */
    inline void setError(ErrorType error)
	{ m_error = error; }

    /**
     * Error name
     * @return The error name
     */
    const String errorName();

    /**
     * The full value of the error
     * @return 2 byte integer containing the full code of the error
     */
    u_int16_t errorCode();

    /**
     * Obtain abstract TCAP error from TCAP protocol defined error value
     * @param tcapType Type of TCAP for which the error is searched
     * @param code TCAP protocol error value
     * @return The type of the error
     */
    static int errorFromCode(SS7TCAP::TCAPType tcapType, u_int16_t code);

    /**
     * Obtain TCAP specific error value from abstract TCAP error
     * @param tcapType Type of TCAP for which the error is searched
     * @param err Abstrat TCAP error
     * @return The error value as defined by the TCAP protocol
     */
    static u_int16_t codeFromError(SS7TCAP::TCAPType tcapType, int err);

    /**
     * Dictionary for error types
     */
    static const TokenDict s_errorTypes[];

private:
    SS7TCAP::TCAPType m_tcapType;
    ErrorType m_error;
};

/**
 * Implementation of SS7 Transactional Capabilities Application Part Transaction
 * @short SS7 TCAP transaction implementation
 */
class YSIG_API SS7TCAPTransaction : public RefObject, public Mutex
{
public:
    enum TransactionState {
	Idle                      = 0,
	PackageSent               = 1,
	PackageReceived           = 2,
	Active                    = 3,
    };

    enum TransactionTransmit {
	NoTransmit       = 0,
	PendingTransmit  = 256,
	Transmitted      = 521,
    };

    /**
     * Constructor
     * @param tcap TCAP holding this transaction
     * @param type Initiating type for transaction
     * @param transactID Transaction ID
     * @param params Decoded TCAP parameters for building the transaction
     * @param timeout Transaction timeout
     * @param initLocal True if the transaction was initiated locally, false if not
     */
    SS7TCAPTransaction(SS7TCAP* tcap, SS7TCAP::TCAPUserTransActions type, const String& transactID, NamedList& params,
	u_int64_t timeout, bool initLocal = true);

    /**
     * Destructor
     */
    ~SS7TCAPTransaction();

    /**
     * Process transaction data and fill the NamedList with the decoded data
     * @param params NamedList to fill with decoded data
     * @param data Data to decode
     * @return A TCAP error encountered whilst decoding
     */
    virtual SS7TCAPError handleData(NamedList& params, DataBlock& data) = 0;

    /**
     * An update request for this transaction
     * @param type The type of transaction to which this transaction should be updated
     * @param params Update parameter
     * @param updateByUser True if the update is made by the local user, false if it's made by the remote end
     * @return A TCAP Error
     */
    virtual SS7TCAPError update(SS7TCAP::TCAPUserTransActions type, NamedList& params, bool updateByUser = true) = 0;

    /**
     * Handle TCAP relevant dialog data
     * @param params NamedList containing (if present) dialog information
     * @param byUser True if the dialog information is provided by the local user, false otherwise
     * @return A report error
     */
    virtual SS7TCAPError handleDialogPortion(NamedList& params,bool byUser = true) = 0;

    /**
     * Build a Reject component in answer to an encoutered error during decoding of the component portion
     * @param error The encountered error
     * @param params Decoded TCAP message parameters
     * @param data DataBlock containing the rest of the coded TCAP message
     * @return A report error
     */
    virtual SS7TCAPError buildComponentError(SS7TCAPError& error, NamedList& params, DataBlock& data);

    /**
     * Update components
     * @param params NamedList reference containing the update information
     * @param updateByUser Flag if the update was issued by local user or by remote
     * @return A report error
     */
    virtual SS7TCAPError handleComponents(NamedList& params, bool updateByUser = true);

    /**
     * Request encoding for the components of this transaction
     * @param params Components parameters to encode
     * @param data DataBlock reference in which to insert the encoded components
     */
    virtual void requestComponents(NamedList& params, DataBlock& data);

    /**
     * Fill the NamedList with transaction portion parameters
     * @param params NamedList reference to fill with transaction portion parameters
     */
    virtual void transactionData(NamedList& params);

    /**
     * Request content for this transaction
     * @param params List of parameters of this tranaction
     * @param data Data block to fill with encoded content
     */
    virtual void requestContent(NamedList& params, DataBlock& data) = 0;

    /**
     * Check components for timeouts
     */
    virtual void checkComponents();

    /**
     * Set the current type of transaction primitive
     * @param type The transaction primitive to be set
     */
    inline void setTransactionType(SS7TCAP::TCAPUserTransActions type)
	{ Lock l(this); m_type = type; }

    /**
     * Retrieve the current type of primitive that is set for this transaction
     * @return The transaction primitive type
     */
    inline SS7TCAP::TCAPUserTransActions transactionType()
	{ return m_type; }

    /**
     * Set the state of this transaction, trigger a transmission pending state
     * @param state The state to set for the transaction
     */
    inline void setState(TransactionState state)
    {
	Lock l(this);
	m_state = state;
	// changing state automatically triggers a change in transmission state (except for Idle)
	if (state != Idle)
	    m_transmit = PendingTransmit;
    }

    /**
     * Retrieve the state of this transaction
     * @return The state of this transaction
     */
    inline TransactionState transactionState()
	{ return m_state; }

    /**
     * Set the transmission state for this transaction
     * @param state The transmission state to be set
     */
    void setTransmitState(TransactionTransmit state);

    /**
     * The transmission state for this transaction
     * @return The current transmission state
     */
    inline TransactionTransmit transmitState()
	{ return m_transmit; }

    /**
     * The TCAP to which this transaction belongs
     * @return A pointer to the TCAP component
     */
    inline SS7TCAP* tcap()
	{ return m_tcap; }

    /**
     * Get the ID of the transaction so it can be used for list searches
     * @return A reference to the ID
     */
    const String& toString() const
	{ return m_localID; }

    /**
     * Set the TCAP username to which this transaction belongs
     * @param name The name of the user to set
     */
    inline void setUserName(const String& name)
	{ m_userName = name; }

    /**
     * Return the name of the TCAP user to which this transaction belongs
     * @return The name of the user
     */
    const String& userName()
	{ return m_userName; }

    /**
     * Check if a basic end was set for this transaction
     * @return True if basic end was specified by the user, false if prearranged end was specified
     */
    inline bool basicEnd()
	{ return m_basicEnd; }

    /**
     * Add SCCP Addressing information
     * @param fillParams NamedList to fill with addressing information
     * @param local True if the information is for the user, otherwise
     */
    void addSCCPAddressing(NamedList& fillParams, bool local);

    /**
     * Check if the flag to end this transaction immediately was set
     * @return True if the end flag was set, false otherwise
     */
    inline bool endNow()
	{ return m_endNow; }

    /**
     * Set the flag to end this transaction immediately
     * @param endNow Boolean value to set to the end flag
     */
    inline void endNow(bool endNow)
	{ m_endNow = endNow; }

    /**
     * Check if the transaction has timed out
     * @return True if the transaction timed out, false otherwise
     */
    inline bool timedOut()
	{ return m_timeout.timeout(); }

    /**
     * Find a component with given id
     * @param id Id of component to find
     * @return The component with given id or null
     */
    SS7TCAPComponent* findComponent(const String& id);

    /**
     * Update the state of this transaction to end the transaction
     */
    virtual void updateToEnd();

    /**
     * Update transaction state
     * @param byUser True if update is requested by user, false if by remote
     */
    virtual void updateState(bool byUser = true) = 0;

    /**
     * Set information in case of abnormal dialog detection
     * @param params List of parameters where to set the abnormal dialog information
     */
    virtual void abnormalDialogInfo(NamedList& params);

    /**
     * @param params NamedList reference to fill with the decoded dialog information
     * @param data DataBlock reference from which to decode the dialog information
     * @return A TCAP error encountered whilst decoding
     */
    virtual SS7TCAPError decodeDialogPortion(NamedList& params, DataBlock& data) = 0;

    /**
     * @param params NamedList reference from which to take the dialog information to encode
     * @param data DataBlock reference into which to put the encoded dialog information
     */
    virtual void encodeDialogPortion(NamedList& params, DataBlock& data) = 0;

    /**
     * @param params NamedList reference to fill with the decoded component information
     * @param data DataBlock reference from which to decode the component information
     * @return A TCAP error encountered whilst decoding
     */
    virtual SS7TCAPError decodeComponents(NamedList& params, DataBlock& data) = 0;

    /**
     * @param params NamedList reference from which to take the component information to encode
     * @param data DataBlock reference into which to put the encoded component information
     */
    virtual void encodeComponents(NamedList& params, DataBlock& data) = 0;

protected:
    SS7TCAP* m_tcap;
    SS7TCAP::TCAPType m_tcapType;
    String m_userName;
    String m_localID;
    String m_remoteID;
    SS7TCAP::TCAPUserTransActions m_type;
    TransactionState m_state;
    TransactionTransmit m_transmit;

    ObjList m_components;

    NamedList m_localSCCPAddr;
    NamedList m_remoteSCCPAddr;

    bool m_basicEnd; // basic or prearranged end (specified by user when sending a Response)
    bool m_endNow; // delete immediately after sending
    SignallingTimer m_timeout;
};

/**
 * Implementation of SS7 Transactional Capabilities Application Part Component
 * @short SS7 TCAP component implementation
 */
class YSIG_API SS7TCAPComponent : public GenObject
{
public:
    /**
     * Component state
     */
    enum TCAPComponentState {
	Idle,
	OperationPending,
	OperationSent,
	WaitForReject,
    };

    /**
     * Constructor
     * @param type TCAP type for which to build this component
     * @param trans TCAP transaction to which this component belongs to
     * @param params Parameters for building component
     * @param index Index in the list of parameters
     */
    SS7TCAPComponent(SS7TCAP::TCAPType type, SS7TCAPTransaction* trans, NamedList& params, unsigned int index);

    /**
     * Destructor
     */
    virtual ~SS7TCAPComponent();

    /**
     * Update this component's data
     * @param params Update parameters
     * @param index Index of parameters in the list for the update of this component
     */
    virtual void update(NamedList& params, unsigned int index);

    /**
     * Put the information of the component in a NamedList
     * @param index Index for build parameter names
     * @param fillIn NamedList to fill with this component's information
     */
    virtual void fill(unsigned int index, NamedList& fillIn);

    /**
     * Build a TCAP Component from a NamedList
     * @param type TCAP type of component
     * @param tr The transaction to which this component should belong
     * @param params Parameters for building the component
     * @param index Index in the list of parameters
     * @return A pointer to the built SS7TCAPComponent or nil if not all required parameters are present
     */
    static SS7TCAPComponent* componentFromNamedList(SS7TCAP::TCAPType type, SS7TCAPTransaction* tr, NamedList& params, unsigned int index);

    /**
     * Set the transaction to which this component belongs to
     * @param transact TCAP transaction
     */
    void setTransaction(SS7TCAPTransaction* transact);

    /**
     * Returns the transaction to which this component belongs to.
     */
    SS7TCAPTransaction* transaction();

    /**
     * Set the type for this component
     * @param type The type of the component
     */
    inline void setType(SS7TCAP::TCAPUserCompActions type)
	{ m_type = type; }

    /**
     * Get the type of the component
     */
    inline SS7TCAP::TCAPUserCompActions type()
	{ return m_type; }

    /**
     * Set the Invoke ID for this component
     * @param invokeID The invoke ID to assign
     */
    virtual void setInvokeID(String invokeID)
	{ m_id = invokeID; }

    /**
     * String representation of this component's Invoke ID
     * @return String representation of Invoke ID
     */
    virtual const String&  toString () const
	{ return m_id; }

    /**
     * String representation of this component's Correlation ID
     * @return String representation of Correlation ID
     */
    virtual const String&  correlationID() const
	{ return m_corrID; }

    /**
     * Check if the component has timed out
     * @return True if the component timed out, false otherwise
     */
    inline bool timedOut()
	{ return m_opTimer.timeout(); }

    /**
     * Set component state
     * @param state The state to be set
     */
    void setState(TCAPComponentState state);

    /**
     * Obtain the component state
     * @return The component state
     */
    inline TCAPComponentState state()
	{ return m_state; }

    /**
     * Reset invocation timer on user request
     * @param params List of parameters
     * @param index Index of this component's parameters in the list
     */
    void resetTimer(NamedList& params, unsigned int index);

    /**
     * Retrieve operation class for this component
     * @return The class of the operation
     */
    SS7TCAP::TCAPComponentOperationClass operationClass()
	{ return m_opClass; }

    /**
     * Dictionary for component states
     */
    static const TokenDict s_compStates[];

private:
    SS7TCAPTransaction* m_transact;
    SS7TCAP::TCAPUserCompActions m_type;
    TCAPComponentState m_state;
    String m_id;
    String m_corrID;
    String m_opCode;
    String m_opType;
    SS7TCAP::TCAPComponentOperationClass m_opClass;
    SignallingTimer m_opTimer;
    SS7TCAPError m_error;
};

/**
 * Implementation of SS7 Transactional Capabilities Application Part - specification ANSI
 * @short ANSI SS7 TCAP implementation
 */
class YSIG_API SS7TCAPANSI : virtual public SS7TCAP
{
    YCLASS(SS7TCAPANSI,SS7TCAP)
public:
    enum TCAPTags {
	TransactionIDTag    = 0xc7,
	PCauseTag           = 0xd7,
	UserAbortPTag       = 0xd8 , // Primitive
	UserAbortCTag       = 0xf8,  // Constructor
    };

    enum TCAPDialogTags {
	DialogPortionTag         = 0xf9,
	ProtocolVersionTag       = 0xda,
	IntApplicationContextTag = 0xdb,
	OIDApplicationContextTag = 0xdc,
	UserInformationTag       = 0xfd,
	IntSecurityContextTag    = 0x80,
	OIDSecurityContextTag    = 0x81,
	ConfidentialityTag       = 0xa2,
    };

    enum UserInfoTags {
	DirectReferenceTag       = 0x06,
	DataDescriptorTag        = 0x07,
	ExternalTag              = 0x28,
	SingleASNTypePEncTag     = 0x80, // Primitive Single-ASN1-Type-Encoding
	SingleASNTypeCEncTag     = 0xa0, // Constructor Single-ASN1-Type-Encoding
	OctetAlignEncTag         = 0x81,
	ArbitraryEncTag          = 0x82,
    };

    enum ConfidentialityTags {
	IntConfidentialContextTag    = 0x80,
	OIDConfidentialContextTag    = 0x81,
    };

    enum TCAPComponentTags {
	ComponentPortionTag  = 0xe8,
	ComponentsIDsTag     = 0xcf,
	OperationNationalTag = 0xd0,
	OperationPrivateTag  = 0xd1,
	ErrorNationalTag     = 0xd3,
	ErrorPrivateTag      = 0xd4,
	ProblemCodeTag       = 0xd5,
	ParameterSetTag      = 0xf2,
	ParameterSeqTag      = 0x30,
    };

    /**
     * Constructor
     * @param params NamedList containing parameters for building TCAP
     */
    SS7TCAPANSI(const NamedList& params);

    /**
     * Destructor
     */
    ~SS7TCAPANSI();

    /**
     * Build a transaction
     * @param type Type with which to build the transactions
     * @param transactID ID for the transaction
     * @param params Parameters for building the transaction
     * @param initLocal True if built by user, false if by remote end
     * @return A transaction
     */
    virtual SS7TCAPTransaction* buildTransaction(SS7TCAP::TCAPUserTransActions type, const String& transactID, NamedList& params,
	bool initLocal = true);

private:
    SS7TCAPError decodeTransactionPart(NamedList& params, DataBlock& data);
    void encodeTransactionPart(NamedList& params, DataBlock& data);
};

/**
 * Implementation of SS7 Transactional Capabilities Application Part Transaction - specification ANSI
 * @short ANSI SS7 TCAP transaction implementation
 */
class YSIG_API SS7TCAPTransactionANSI : public SS7TCAPTransaction
{
public:
    enum TCAPANSIComponentType {
	CompUnknown         = 0x0,
	Local               = 0x1,
	InvokeLast          = 0xe9,
	ReturnResultLast    = 0xea,
	ReturnError         = 0xeb,
	Reject              = 0xec,
	InvokeNotLast       = 0xed,
	ReturnResultNotLast = 0xee,
    };

    enum ANSITransactionType {
	Unknown                         = 0x0,
	Unidirectional                  = 0xe1,
	QueryWithPermission             = 0xe2,
	QueryWithoutPermission          = 0xe3,
	Response                        = 0xe4,
	ConversationWithPermission      = 0xe5,
	ConversationWithoutPermission   = 0xe6,
	Abort                           = 0xf6,
    };

    /**
     * Constructor
     * @param tcap TCAP holding this transaction
     * @param type Initiating type for transaction
     * @param transactID Transaction ID
     * @param params Decoded TCAP parameters for building the transaction
     * @param timeout Transaction timeout
     * @param initLocal True if the transaction was initiated locally, false if not
     */
    SS7TCAPTransactionANSI(SS7TCAP* tcap, SS7TCAP::TCAPUserTransActions type, const String& transactID, NamedList& params,
	u_int64_t timeout, bool initLocal = true);

    /**
     * Destructor
     */
    ~SS7TCAPTransactionANSI();

    /**
     * Process transaction data and fill the NamedList with the decoded data
     * @param params NamedList to fill with decoded data
     * @param data Data to decode
     * @return A TCAP error encountered whilst decoding
     */
    virtual SS7TCAPError handleData(NamedList& params, DataBlock& data);

    /**
     * An update request for this transaction
     * @param type The type of transaction to which this transaction should be updated
     * @param params Update parameter
     * @param updateByUser True if the update is made by the local user, false if it's made by the remote end
     * @return A TCAP Error
     */
    virtual SS7TCAPError update(SS7TCAP::TCAPUserTransActions type, NamedList& params, bool updateByUser = true);

    /**
     * Handle TCAP relevant dialog data
     * @param params NamedList containing (if present) dialog information
     * @param byUser True if the dialog information is provided by the local user, false otherwise
     * @return A report error
     */
    virtual SS7TCAPError handleDialogPortion(NamedList& params, bool byUser = true);

    /**
     * Encode P-Abort information
     * @param tr The transaction on which the abort was signalled
     * @param params NamedList reference from which to get the P-Abort information
     * @param data DataBlock reference in which to insert the encoded P-Abort information
     */
    static void encodePAbort(SS7TCAPTransaction* tr, NamedList& params, DataBlock& data);

    /**
     * Decode P-Abort TCAP message portion
     * @param tr The transaction on which the abort was signalled
     * @param params NamedList reference to fill with the decoded P-Abort information
     * @param data DataBlock reference from which to decode P-Abort information
     */
    static SS7TCAPError decodePAbort(SS7TCAPTransaction* tr, NamedList& params, DataBlock& data);

    /**
     * Update the state of this transaction to end the transaction
     */
    virtual void updateToEnd();

    /**
     * Update transaction state
     * @param byUser True if update is requested by user, false if by remote
     */
    virtual void updateState(bool byUser);

    /**
     * Request content for this transaction
     * @param params List of parameters of this tranaction
     * @param data Data block to fill with encoded content
     */
    virtual void requestContent(NamedList& params, DataBlock& data);

    /**
     * Dictionary keeping string versions of transaction types
     */
    static const TokenDict s_ansiTransactTypes[];

private:
    SS7TCAPError decodeDialogPortion(NamedList& params, DataBlock& data);
    void encodeDialogPortion(NamedList& params, DataBlock& data);
    SS7TCAPError decodeComponents(NamedList& params, DataBlock& data);
    void encodeComponents(NamedList& params, DataBlock& data);

    SS7TCAP::TCAPUserTransActions m_prevType;
};

/**
 * Implementation of SS7 Transactional Capabilities Application Part - specification ITU-T
 * @short ITU-T SS7 TCAP implementation
 */
class YSIG_API SS7TCAPITU : virtual public SS7TCAP
{
    YCLASS(SS7TCAPITU,SS7TCAP)
public:
    enum TCAPTags {
	OriginatingIDTag    = 0x48,
	DestinationIDTag    = 0x49,
	PCauseTag           = 0x4a,
    };

    enum TCAPDialogTags {
	DialogPortionTag         = 0x6b,
	ProtocolVersionTag       = 0x80,
	ApplicationContextTag    = 0xa1,
	UserInformationTag       = 0xbe,
    };

    enum UserInfoTags {
	DirectReferenceTag       = 0x06,
	DataDescriptorTag        = 0x07,
	ExternalTag              = 0x28,
	SingleASNTypePEncTag     = 0x80, // Primitive Single-ASN1-Type-Encoding
	SingleASNTypeCEncTag     = 0xa0, // Constructor Single-ASN1-Type-Encoding
	OctetAlignEncTag         = 0x81,
	ArbitraryEncTag          = 0x82,
    };

    enum TCAPComponentTags {
	ComponentPortionTag  = 0x6c,
	LocalTag             = 0x02,
	LinkedIDTag          = 0x80,
	GlobalTag            = 0x06,
	ParameterSeqTag      = 0x30,
	ParameterSetTag      = 0x31,
    };

    /**
     * Constructor
     * @param params Parameters to build ITU TCAP
     */
    SS7TCAPITU(const NamedList& params);

    /**
     * Destructor
     */
    ~SS7TCAPITU();

    /**
     * Build a transaction
     * @param type Type with which to build the transactions
     * @param transactID ID for the transaction
     * @param params Parameters for building the transaction
     * @param initLocal True if built by user, false if by remote end
     * @return A transaction
     */
    virtual SS7TCAPTransaction* buildTransaction(SS7TCAP::TCAPUserTransActions type, const String& transactID, NamedList& params,
	bool initLocal = true);

private:
    SS7TCAPError decodeTransactionPart(NamedList& params, DataBlock& data);
    void encodeTransactionPart(NamedList& params, DataBlock& data);
};

/**
 * Implementation of SS7 Transactional Capabilities Application Part Transaction - specification ITU-T
 * @short ITU-T SS7 TCAP transaction implementation
 */
class YSIG_API SS7TCAPTransactionITU : public SS7TCAPTransaction
{
public:
    enum ITUComponentType {
	CompUnknown         = 0x0,
	Local               = 0x1,
	Invoke              = 0xa1,
	ReturnResultLast    = 0xa2,
	ReturnError         = 0xa3,
	Reject              = 0xa4,
	ReturnResultNotLast = 0xa7,
    };

    enum ITUTransactionType {
	Unknown                         = 0x0,
	Unidirectional                  = 0x61,
	Begin                           = 0x62,
	End                             = 0x64,
	Continue                        = 0x65,
	Abort                           = 0x67,
    };

    enum ITUDialogTags {
	AARQDialogTag               = 0x60,
	AAREDialogTag               = 0x61,
	ABRTDialogTag               = 0x64,
	ResultDiagnosticUserTag     = 0xa1,
	ResultDiagnosticProviderTag = 0xa2,
	ResultTag                   = 0xa2,
	ResultDiagnosticTag         = 0xa3,
    };

    enum ITUDialogValues {
	ResultAccepted                     = 0,
	ResultRejected                     = 1,
	DiagnosticUserNull                 = 0x10,
	DiagnosticUserNoReason             = 0x11,
	DiagnosticUserAppCtxtNotSupported  = 0x12,
	DiagnosticProviderNull             = 0x20,
	DiagnosticProviderNoReason         = 0x21,
	DiagnosticProviderNoCommonDialog   = 0x22,
	AbortSourceUser                    = 0x30,
	AbortSourceProvider                = 0x31,
    };

    /**
     * Constructor
     * @param tcap TCAP holding this transaction
     * @param type Initiating type for transaction
     * @param transactID Transaction ID
     * @param params Parameters to build this transaction
     * @param timeout Transaction time out interval
     * @param initLocal True if the transaction was initiated locally, false if not
     */
    SS7TCAPTransactionITU(SS7TCAP* tcap, SS7TCAP::TCAPUserTransActions type, const String& transactID, NamedList& params,
	u_int64_t timeout, bool initLocal = true);

    /**
     * Destructor
     */
    ~SS7TCAPTransactionITU();

    /**
     * Process transaction data and fill the NamedList with the decoded data
     * @param params NamedList to fill with decoded data
     * @param data Data to decode
     * @return A TCAP error encountered whilst decoding
     */
    virtual SS7TCAPError handleData(NamedList& params, DataBlock& data);

    /**
     * An update request for this transaction
     * @param type The type of transaction to which this transaction should be updated
     * @param params Update parameter
     * @param updateByUser True if the update is made by the local user, false if it's made by the remote end
     * @return A TCAP Error
     */
    virtual SS7TCAPError update(SS7TCAP::TCAPUserTransActions type, NamedList& params, bool updateByUser = true);

    /**
     * Handle TCAP relevant dialog data
     * @param params NamedList containing (if present) dialog information
     * @param byUser True if the dialog information is provided by the local user, false otherwise
     * @return A report error
     */
    virtual SS7TCAPError handleDialogPortion(NamedList& params, bool byUser = true);

    /**
     * Encode P-Abort information
     * @param tr The transaction on which the abort was signalled
     * @param params NamedList reference from which to get the P-Abort information
     * @param data DataBlock reference in which to insert the encoded P-Abort information
     */
    static void encodePAbort(SS7TCAPTransaction* tr, NamedList& params, DataBlock& data);

    /**
     * Decode P-Abort TCAP message portion
     * @param tr The transaction on which the abort was signalled
     * @param params NamedList reference to fill with the decoded P-Abort information
     * @param data DataBlock reference from which to decode P-Abort information
     * @return A report error
     */
    static SS7TCAPError decodePAbort(SS7TCAPTransaction* tr, NamedList& params, DataBlock& data);

    /**
     * Update the state of this transaction to end the transaction
     */
    virtual void updateToEnd();

    /**
     * Check if the transaction present dialog information
     * @return True if dialog information is present, false otherwise
     */
    inline bool dialogPresent()
	{ return !(m_appCtxt.null()); }

    /**
     * Test for dialog when decoding
     * @param data Data from which the transaction is decoded
     * @return True if dialog portion is present, false otherwise
     */
    bool testForDialog(DataBlock& data);

    /**
     * Encode dialog portion of transaction
     * @param params List of parameters to encode
     * @param data Data block into which to insert the encoded data
     */
    void encodeDialogPortion(NamedList& params, DataBlock& data);

    /**
     * Decode dialog portion
     * @param params List into which to put the decoded dialog parameters
     * @param data Data to decodeCaps
     * @return A report error
     */
    SS7TCAPError decodeDialogPortion(NamedList& params, DataBlock& data);

    /**
     * Update transaction state
     * @param byUser True if update is requested by user, false if by remote
     */
    void updateState(bool byUser = false);

    /**
     * Request content for this transaction
     * @param params List of parameters of this tranaction
     * @param data Data block to fill with encoded content
     */
    virtual void requestContent(NamedList& params, DataBlock& data);

    /**
     * Set information in case of abnormal dialog detection
     * @param params List of parameters where to set the abnormal dialog information
     */
    virtual void abnormalDialogInfo(NamedList& params);

    /**
     * Dictionary for Dialogue PDUs
     */
    static const TokenDict s_dialogPDUs[];

    /**
     * Dictionary for dialogue result values
     */
    static const TokenDict s_resultPDUValues[];

private:
    SS7TCAPError decodeComponents(NamedList& params, DataBlock& data);
    void encodeComponents(NamedList& params, DataBlock& data);

    String m_appCtxt;
};

// The following classes are ISDN, not SS7, but they use the same signalling
//  interfaces so they will remain here

/**
 * An interface to a Layer 2 (Q.921) ISDN message transport
 * @short Abstract ISDN layer 2 (Q.921) message transport
 */
class YSIG_API ISDNLayer2 : virtual public SignallingComponent
{
    YCLASS(ISDNLayer2,SignallingComponent)
    friend class ISDNQ921Management;
public:
    /**
     * Layer states if it has a TEI assigned
     */
    enum State {
	Released,                        // Multiple frame acknowledged not allowed
	WaitEstablish,                   // Wating to establish 'multiple frame acknowledged' mode
	Established,                     // Multiple frame acknowledged allowed
	WaitRelease,                     // Wating to release 'multiple frame acknowledged' mode
    };

    /**
     * Destructor
     */
    virtual ~ISDNLayer2();

    /**
     * Get the ISDN Layer 3 attached to this layer
     */
    inline ISDNLayer3* layer3() const
	{ return m_layer3; }

    /**
     * Get the layer's state
     * @return The layer's state as enumeration
     */
    inline State state() const
	{ return m_state; }

    /**
     * Check if this interface is the network or CPE (user) side of the link
     * @return True if this interface is the network side of the link
     */
    inline bool network() const
	{ return m_network; }

    /**
     * Check if this interface should change its type
     * @return True if type change is allowed
     */
    inline bool detectType() const
	{ return m_detectType; }

    /**
     * Get the SAPI (Service Access Point Identifier) of this interface
     * @return The SAPI (Service Access Point Identifier) of this interface
     */
    inline u_int8_t localSapi() const
	{ return m_sapi; }

    /**
     * Get the TEI (Terminal Endpoint Identifier) of this interface
     * @return The TEI (Terminal Endpoint Identifier) of this interface
     */
    inline u_int8_t localTei() const
	{ return m_tei; }

    /**
     * Get the maximum length of user data transported through this layer
     * @return The maximum length of user data transported through this layer
     */
    inline u_int32_t maxUserData() const
	{ return m_maxUserData; }

    /**
     * Check if this interface has a TEI assigned
     * @return True if this interface has a TEI assigned
     */
    inline bool teiAssigned() const
	{ return m_teiAssigned; }

    /**
     * Check if this interface will automatically re-establish when released
     * @return The auto restart flag
     */
    inline bool autoRestart() const
	{ return m_autoRestart; }

    /**
     * Get the uptime of the interface
     * @return Time since interface got up in seconds
     */
    unsigned int upTime() const
	{ return m_lastUp ? (Time::secNow() - m_lastUp) : 0; }

    /**
     * Implements Q.921 DL-ESTABLISH and DL-RELEASE request primitives
     * Descendants must implement this method to fullfill the request
     * @param tei This layer TEI (Terminal Endpoint Identifier)
     * @param establish True to establish. False to release
     * @param force True to establish even if we already are in this mode. This
     *  parameter is ignored if establish is false
     * @return True if the request was accepted
     */
    virtual bool multipleFrame(u_int8_t tei, bool establish, bool force)
	{ return false; }

    /**
     * Implements Q.921 DL-DATA and DL-UNIT DATA request primitives
     * Descendants must implement this method to fullfill the request
     * @param data Data to send
     * @param tei This layer TEI
     * @param ack True to send an acknowledged frame, false to send an unacknowledged one
     * @return True if the request was accepted
     */
    virtual bool sendData(const DataBlock& data, u_int8_t tei, bool ack)
	{ return false; }

    /**
     * Emergency release.
     * Descendants must implement this method to cleanup/reset data
     */
    virtual void cleanup() = 0;

    /**
     * Attach an ISDN Q.931 Layer 3 if the given parameter is different from the one we have
     * Cleanup the object before ataching the new Layer 3
     * This method is thread safe
     * @param layer3 Pointer to the Q.931 Layer 3 to attach
     */
    virtual void attach(ISDNLayer3* layer3);

    /**
     * Get the text associated with a given state
     * @param s The state to get the text for
     * @return The text associated with the given state
     */
    static inline const char* stateName(State s)
	{ return lookup((int)s,m_states); }

protected:
    /**
     * Constructor
     * Initialize this interface and the component
     * @param params Layer's parameters
     * @param name Optional name of the component
     * @param tei Value of TEI for this layer
     */
    ISDNLayer2(const NamedList& params, const char* name = 0, u_int8_t tei = 0);

    /**
     * Retrieve the layer's mutex
     * @return Reference to the Layer 2 mutex
     */
    inline Mutex& l2Mutex()
	{ return m_layerMutex; }

    /**
     * Implements Q.921 DL-ESTABLISH indication/confirmation primitive
     *  of 'multiple frame acknowledged' mode established
     * @param tei The TEI requested
     * @param confirm True if this is a confirmation of a previous request.
     *  False if it is an indication of state change on remote request
     * @param timeout True if the reason is a timeout.
     */
    void multipleFrameEstablished(u_int8_t tei, bool confirm, bool timeout);

    /**
     * Implements Q.921 DL-RELEASE indication/confirmation primitive
     *  of 'multiple frame acknowledged' mode released
     * @param tei The TEI released
     * @param confirm True if this is a confirmation of a previous request.
     *  False if it is an indication of state change on remote request
     * @param timeout True if the reason is a timeout.
     */
    void multipleFrameReleased(u_int8_t tei, bool confirm, bool timeout);

    /**
     * Notify layer 3 of data link set/release command or response
     * Used for stateless layer 2
     * @param tei The TEI of this layer
     * @param cmd True if received a command, false if received a response
     * @param value The value of the notification
     *	If 'cmd' is true (command), the value is true if a request to establish data link was received
     *   or false if received a request to release data link
     *	If 'cmd' is false (response), the value is the response
     */
    void dataLinkState(u_int8_t tei, bool cmd, bool value);

    /**
     * Notify layer 3 of data link idle timeout
     * Used for stateless layer 2
     */
    void idleTimeout();

    /**
     * Implements Q.921 DL-DATA and DL-UNIT DATA indication primitives
     * Receive data from remote peer
     * @param data Received data
     * @param tei The TEI for which the data was received
     */
    void receiveData(const DataBlock& data, u_int8_t tei);

    /**
     * Set TEI assigned status. Print a debug message. If status is false calls cleanup()
     * Descendants are responsable for TEI assigned status management
     * @param status The new TEI assigned status
     */
    void teiAssigned(bool status);

    /**
     * Set the state
     * Descendants are responsable for multiple frame status management
     * @param newState The new state
     * @param reason Reason of state change, NULL if unspecified
     */
    void changeState(State newState, const char* reason = 0);

    /**
     * Change the interface type
     * @return True if interface type changed
     */
    bool changeType();

    /**
     * Set the automatically re-establish when released flag
     * @param restart The new value of the auto restart flag
     */
    inline void autoRestart(bool restart)
	{ m_autoRestart = restart; }

    /**
     * Set the Reference Identifier used in management procedures
     * @param ri The new reference number
     */
    inline void setRi(u_int16_t ri)
	{ m_ri = ri; }

    /**
     * Parse a received packet
     * @param packet The packet received
     * @return Pointer to a newly created frame, NULL if an error occured
     */
    ISDNFrame* parsePacket(const DataBlock& packet);

private:
    ISDNLayer3* m_layer3;                // The attached Layer 3 interface
    Mutex m_layerMutex;                  // Layer 2 operations mutex
    Mutex m_layer3Mutex;                 // Control m_layer3 operations
    State m_state;                       // Layer's state
    bool m_network;                      // Network/CPE type of the interface
    bool m_detectType;                   // Detect interface type
    u_int8_t m_sapi;                     // SAPI value
    u_int8_t m_tei;                      // TEI value
    u_int16_t m_ri;                      // Reference number
    u_int32_t m_lastUp;                  // Time when the interface got up
    bool m_checked;                      // Flag to indicate if the layer was checked
    bool m_teiAssigned;                  // The TEI status
    bool m_autoRestart;                  // True to restart when released
    u_int32_t m_maxUserData;             // Maximum length of user data transported trough this layer
    unsigned int m_teiRefNumber;         // The Reference Number (Ri) carried by a TEI management frame
    static const TokenDict m_states[];   // Keep the string associated with each state
};

/**
 * An interface to a Layer 3 (Q.931) ISDN message transport
 * @short Abstract ISDN layer 3 (Q.931) message transport
 */
class YSIG_API ISDNLayer3 : virtual public SignallingComponent
{
    YCLASS(ISDNLayer3,SignallingComponent)
public:
    /**
     * Implements Q.921 DL-ESTABLISH indication/confirmation primitive:
     *  'multiple frame acknowledged' mode established
     * @param tei The TEI of the frame
     * @param confirm True if this is a confirmation of a previous request.
     *  False if it is an indication of state change on remote request
     * @param timeout True if the reason is a timeout
     * @param layer2 Pointer to the notifier
     */
    virtual void multipleFrameEstablished(u_int8_t tei, bool confirm, bool timeout, ISDNLayer2* layer2)
	{ }

    /**
     * Implements Q.921 DL-RELEASE indication/confirmation primitive:
     *  'multiple frame acknowledged' mode released
     * @param tei The TEI of the frame
     * @param confirm True if this is a confirmation of a previous request.
     *  False if it is an indication of state change on remote request
     * @param timeout True if the reason is a timeout.
     * @param layer2 Pointer to the notifier
     */
    virtual void multipleFrameReleased(u_int8_t tei, bool confirm, bool timeout, ISDNLayer2* layer2)
	{ }

    /**
     * Notification from layer 2 of data link set/release command or response
     * Used for stateless layer 2
     * @param tei The TEI of the command or response
     * @param cmd True if received a command, false if received a response
     * @param value The value of the notification
     *	If 'cmd' is true (command), the value is true if a request to establish data link was received
     *   or false if received a request to release data link
     *	If 'cmd' is false (response), the value is the response
     * @param layer2 Pointer to the notifier
     */
    virtual void dataLinkState(u_int8_t tei, bool cmd, bool value, ISDNLayer2* layer2)
	{ }

    /**
     * Notification from layer 2 of data link idle timeout
     * Used for stateless layer 2
     * @param layer2 Pointer to the notifier
     */
    virtual void idleTimeout(ISDNLayer2* layer2)
	{ }

    /**
     * Implements Q.921 DL-DATA and DL-UNIT DATA indication primitives
     * Receive data from remote peer
     * @param data Received data
     * @param tei The TEI of the received frame
     * @param layer2 Pointer to the sender
     */
    virtual void receiveData(const DataBlock& data, u_int8_t tei, ISDNLayer2* layer2) = 0;

    /**
     * Attach an ISDN Q.921 Layer 2
     * @param layer2 Pointer to the Q.921 Layer 2 to attach
     * @return Pointer to the detached Layer 2 or NULL
     */
    virtual ISDNLayer2* attach(ISDNLayer2* layer2)
	{ return 0; }

protected:
    /**
     * Constructor
     * Initialize the component
     * @param name Name of this component
     */
    inline ISDNLayer3(const char* name = 0)
	: SignallingComponent(name),
	  m_layerMutex(true,"ISDNLayer3::layer")
	{ }

    /**
     * Retrieve the layer's mutex
     * @return Reference to the Layer 3 mutex
     */
    inline Mutex& l3Mutex()
	{ return m_layerMutex; }

private:
    Mutex m_layerMutex;                  // Layer 3 operations mutex
};

/**
 * Encapsulates an ISDN (Q.921) frame exchanged over a hardware HDLC interface
 * @short An ISDN frame
 */
class YSIG_API ISDNFrame : public RefObject
{
    friend class ISDNQ921;
    friend class ISDNQ921Management;
public:
    /**
     * Frame type according to Q.921 3.6
     */
    enum Type {
	DISC = 1,                        // disconnect (command)
	DM = 2,                          // disconnected (response)
	FRMR = 3,                        // frame reject (response)
	I = 4,                           // information transfer (response)
	REJ = 5,                         // reject (command/response)
	RNR = 6,                         // receive not ready (command/response)
	RR = 7,                          // receive ready (command/response)
	SABME = 8,                       // set asynchronous balanced mode extended (command)
	UA = 9,                          // unnumbered acknoledgement (response)
	UI = 10,                         // unnumbered information (command)
	XID = 11,                        // exchange identification (command/response)
	// Note: Keep all errors greater then Invalid: The code relies on it
	Invalid = 100,
	ErrUnknownCR = 101,              // Error: Unknown command/response. Set by parser
	ErrHdrLength = 102,              // Error: Invalid header length. Set by parser
	ErrDataLength = 103,             // Error: Information field too long
	ErrRxSeqNo = 104,                // Error: Invalid receive sequence number
	ErrTxSeqNo = 105,                // Error: Invalid send sequence number
	ErrInvalidEA = 106,              // Error: Invalid 'extended address' bit(s). Set by parser
	ErrInvalidAddress = 107,         // Error: Invalid SAPI/TEI
	ErrUnsupported = 108,            // Error: Unsupported command. E.g. XID
	ErrInvalidCR = 109,              // Error: Invalid command/response flag
    };

    /**
     * Codes used for TEI management procedures (Q.921 Table 8)
     */
    enum TeiManagement {
	TeiReq       = 1,                // TEI request (user to network)
	TeiAssigned  = 2,                // TEI assigned (network to user)
	TeiDenied    = 3,                // TEI denied (network to user)
	TeiCheckReq  = 4,                // TEI check request (network to user)
	TeiCheckRsp  = 5,                // TEI check response (user to network)
	TeiRemove    = 6,                // TEI remove (network to user)
	TeiVerify    = 7                 // TEI verify (user to network)
    };

    /**
     * Frame category
     */
    enum Category {
	Data,                            // I, UI
	Supervisory,                     // RR, RNR, REJ
	Unnumbered,                      // SABME, DISC, UA DM, FRMR XID
	Error
    };

    /**
     * Destructor
     */
    virtual ~ISDNFrame();

    /**
     * Get the type of this frame
     * @return The type of this frame as enumeration
     */
    inline Type type() const
	{ return m_type; }

    /**
     * Get the error type
     * @return The error type of this frame as enumeration
     */
    inline Type error() const
	{ return m_error; }

    /**
     * Get the category of this frame
     * @return The category of this frame as enumeration
     */
    inline Category category() const
	{ return m_category; }

    /**
     * Check if this frame is a command
     * @return True if this frame is a command. False if it is a response
     */
    inline bool command() const
	{ return m_command; }

    /**
     * Get the SAPI of this frame
     * @return The SAPI of this frame
     */
    inline u_int8_t sapi() const
	{ return m_sapi; }

    /**
     * Get the TEI of this frame
     * @return The TEI of this frame
     */
    inline u_int8_t tei() const
	{ return m_tei; }

    /**
     * Check if this frame is a poll (expect response) or a final one
     * @return True if this a poll frame. False if it is a final one
     */
    inline bool poll() const
	{ return m_poll; }

    /**
     * Get the transmitter send sequence number
     * @return The transmitter send sequence number
     */
    inline u_int8_t ns() const
	{ return m_ns; }

    /**
     * Get the transmitter receive sequence number
     * @return The transmitter receive sequence number
     */
    inline u_int8_t nr() const
	{ return m_nr; }

    /**
     * Get the length of the frame's header
     * @return The length of the frame's header
     */
    inline u_int8_t headerLength() const
	{ return m_headerLength; }

    /**
     * Get the length of the data carried by this frame
     * @return The length of the data carried by this frame
     */
    inline u_int32_t dataLength() const
	{ return m_dataLength; }

    /**
     * Get the frame's buffer
     * @return The frame's buffer
     */
    inline const DataBlock& buffer() const
	{ return m_buffer; }

    /**
     * Check if the frame was sent
     * @return True if the frame was sent
     */
    inline bool sent() const
	{ return m_sent; }

    /**
     * Set transmitted flag
     */
    inline void sent(bool value)
	{ m_sent = value; }

    /**
     * Get the text associated with the frame's type
     * @return The text associated with the frame's type
     */
    inline const char* name() const
	{ return typeName(type()); }

    /**
     * Update sequence numbers for I frames
     * @param ns Optional update send sequence number
     * @param nr Optional update receive sequence number
     */
    void update(u_int8_t* ns = 0, u_int8_t* nr = 0);

    /**
     * Get the data transferred with this frame
     * @param dest The destination buffer
     */
    inline void getData(DataBlock& dest) const
	{ dest.assign((u_int8_t*)m_buffer.data() + m_headerLength,m_dataLength); }

    /**
     * Write this frame to a string for debug purposes
     * @param dest The destination string
     * @param extendedDebug True to dump message header and data
     */
    void toString(String& dest, bool extendedDebug) const;

    /**
     * Check if the received frame is a TEI management frame
     * @return True if the frame is a TEI management one, false if it's not
     */
    bool checkTeiManagement() const;

    /**
     * Get reference number from frame
     * @param data The data block which contains it
     * @return Reference number
     */
    static u_int16_t getRi(const DataBlock& data);

    /**
     * Get frame message type
     * @param data The data block which contains it
     * @return Message type
     */
    inline static u_int8_t getType(const DataBlock& data)
	{ return static_cast<u_int8_t>(data.at(3,0)); }

    /**
     * Get action indicator
     * @param data Data block which contains it
     * @return Action indicator value
     */
    inline static u_int8_t getAi(const DataBlock& data)
	{ return static_cast<u_int8_t>(data.at(4,0) >> 1); }


    /**
     * Parse a received data block
     * @param data Data to parse
     * @param receiver The receiver of the data
     * @return ISDNFrame pointer or 0 (no control field)
     */
    static ISDNFrame* parse(const DataBlock& data, ISDNLayer2* receiver);

    /**
     * Build a TEI management message buffer
     * @param data Destination buffer to fill
     * @param type The message type
     * @param ri The reference number
     * @param ai The action indicator
     * @return True on succes
     */
    static bool buildTeiManagement(DataBlock& data, u_int8_t type, u_int16_t ri,
	u_int8_t ai);

    /**
     * Get the command bit value for a given side of a data link
     * @param network True for the network side,
     *  false for the user side of a data link
     * @return The appropriate command bit value
     */
    static inline bool commandBit(bool network)
	{ return network; }

    /**
     * Get the response bit value for a given side of a data link
     * @param network True for the network side,
     *  false for the user side of a data link
     * @return The appropriate response bit value
     */
    static inline bool responseBit(bool network)
	{ return !network; }

    /**
     * Get the command/response type from C/R bit value and sender type
     * @param cr The value of the C/R bit
     * @param sentByNetwork True if the sender is the network side of the data link
     * @return True if it is a command
     */
    static inline bool isCommand(u_int8_t cr, bool sentByNetwork)
	{ return cr ? sentByNetwork : !sentByNetwork; }

    /**
     * Get the text associated with the given frame type
     * @param type Frame type to get the text for
     * @return The text associated with the given frame type
     */
    static inline const char* typeName(Type type)
	{ return lookup(type,s_types,"Invalid frame"); }

    /**
     * Keep the association between frame types and texts
     */
    static const TokenDict s_types[];

protected:
    /**
     * Constructor
     * Used by the parser
     * @param type Frame type
     */
    ISDNFrame(Type type = Invalid);

    /**
     * Constructor
     * Create U/S frames: SABME/DM/DISC/UA/FRMR/XID/RR/RNR/REJ
     * Set data members. Encode frame in buffer according to Q.921
     * Used by ISDNLayer2 to create outgoing frames
     * @param type Frame type
     * @param command Frame command/response's flag
     * @param senderNetwork True if the sender is the network side of the data link
     * @param sapi SAPI value
     * @param tei TEI value
     * @param pf Poll/final flag
     * @param nr Optional transmitter receive sequence number
     */
    ISDNFrame(Type type, bool command, bool senderNetwork,
	u_int8_t sapi, u_int8_t tei, bool pf, u_int8_t nr = 0);

    /**
     * Constructor
     * Create I/UI frames
     * Set data members. Encode frame in buffer according to Q.921
     * Used by ISDNLayer2 to create outgoing frames
     * @param ack True to create an I frame. False to create an UI frame
     * @param senderNetwork True if the sender is the network side of the data link
     * @param sapi SAPI value
     * @param tei TEI value
     * @param pf Poll/final flag
     * @param data Transmitted data
     */
    ISDNFrame(bool ack, bool senderNetwork, u_int8_t sapi, u_int8_t tei,
	bool pf, const DataBlock& data);

private:
    Type m_type;                         // Frame type
    Type m_error;                        // Frame error type
    Category m_category;                 // Frame category
    // Address
    bool m_command;                      // Command/Response frame
    bool m_senderNetwork;                // True if the sender of this frame is the network side of the data link
    u_int8_t m_sapi;                     // SAPI value
    u_int8_t m_tei;                      // TEI value
    // Control
    bool m_poll;                         // Poll/Final flag
    u_int8_t m_ns;                       // N(S) value (when applicable): transmitter send sequence number
    u_int8_t m_nr;                       // N(R) value (when applicable): transmitter receive sequence number
    // Data
    u_int8_t m_headerLength;             // Header length
    u_int32_t m_dataLength;              // Data length
    DataBlock m_buffer;                  // Whole frame: header + data + FCS (frame check sequence = 2 bytes)
    // Outgoing frames only
    bool m_sent;                         // True if already sent
};

/**
 * Q.921 ISDN Layer 2 implementation on top of a hardware HDLC interface
 * @short ISDN Q.921 implementation on top of a hardware interface
 */
class YSIG_API ISDNQ921 : public ISDNLayer2, public SignallingReceiver, public SignallingDumpable
{
    YCLASS2(ISDNQ921,ISDNLayer2,SignallingReceiver)
    friend class ISDNQ921Management;
public:
    /**
     * Constructor
     * Initialize this object and the component
     * @param params Layer's and @ref TelEngine::ISDNLayer2 parameters
     * @param name Name of this component
     * @param mgmt TEI management component
     * @param tei Value of TEI for this component
     */
    ISDNQ921(const NamedList& params, const char* name = 0, ISDNQ921Management* mgmt = 0, u_int8_t tei = 0);

    /**
     * Destructor
     */
    virtual ~ISDNQ921();

    /**
     * Configure and initialize Q.921 and its interface
     * @param config Optional configuration parameters override
     * @return True if Q.921 and the interface were initialized properly
     */
    virtual bool initialize(const NamedList* config);

    /**
     * Get the timeout of a data frame. After that, a higher layer may retransmit data
     * @return The timeout of a data frame
     */
    inline u_int64_t dataTimeout() const
	{ return m_retransTimer.interval() * m_n200.maxVal(); }

    /**
     * Implements Q.921 DL-ESTABLISH and DL-RELEASE request primitives
     * If accepted, the primitive is enqueued for further processing
     * This method is thread safe
     * @param tei This layer's TEI
     * @param establish True to establish. False to release
     * @param force True to establish even if we already are in this mode. This
     *  parameter is ignored if establish is false
     * @return True if the request was accepted
     */
    virtual bool multipleFrame(u_int8_t tei, bool establish, bool force);

    /**
     * Implements Q.921 DL-DATA and DL-UNIT DATA request primitives
     * Send data through the HDLC interface
     * This method is thread safe
     * @param data Data to send
     * @param tei The TEI to send with the data frane
     * @param ack True to send an acknowledged frame, false to send an unacknowledged one
     * @return False if the request was not accepted or send operation failed
     */
    virtual bool sendData(const DataBlock& data, u_int8_t tei, bool ack);

    /**
     * Send a SABME frame to reset the layer
     * @return True if a SABME frame was sent
     */
    inline bool sendSabme()
	{ return sendUFrame(ISDNFrame::SABME,true,true); }

    /**
     * Emergency release.
     * Send 'disconnect' command. Reset all data. Set state to 'Released'
     * This method is thread safe
     */
    virtual void cleanup();

    /**
     * Set debug data of this layer
     * @param printFrames Enable/disable frame printing on output
     * @param extendedDebug Enable/disable hex data dump if print frames is enabled
     */
    inline void setDebug(bool printFrames, bool extendedDebug)
	{ m_extendedDebug = ((m_printFrames = printFrames) && extendedDebug); }

protected:
    /**
     * Detach links. Disposes memory
     */
    virtual void destroyed()
    {
	ISDNLayer2::attach((ISDNLayer3*)0);
	TelEngine::destruct(SignallingReceiver::attach(0));
	SignallingComponent::destroyed();
    }

    /**
     * Method called periodically to check timeouts
     * This method is thread safe
     * @param when Time to use as computing base for events and timeouts
     */
    virtual void timerTick(const Time& when);

    /**
     * Process a packet received by the receiver's interface
     * This method is thread safe
     * @param packet The received packet
     * @return True if message was successfully processed
     */
    virtual bool receivedPacket(const DataBlock& packet);

    /**
     * Process the frame received
     * @param frame Pointer to frame to process
     * @return True if the frame was processed
     */
    bool receivedFrame(ISDNFrame* frame);

    /**
     * Process a notification generated by the attached interface
     * This method is thread safe
     * @param event Notification event reported by the interface
     * @return True if notification was processed
     */
    virtual bool notify(SignallingInterface::Notification event);

    /**
     * Reset object if not in Released state. Drop all frames
     * This method is thread safe
     */
    void reset();

private:
    virtual bool control(NamedList& params)
	{ return SignallingDumpable::control(params,this); }
    // Acknoledge outgoing frames
    // @param frame The acknoledging frame
    bool ackOutgoingFrames(const ISDNFrame* frame);
    // Process a received I/UI frame
    // @param ack True for I frame, false for UI frame
    // @return True to send data to Layer 3
    bool processDataFrame(const ISDNFrame* frame, bool ack);
    // Process a received S frame
    // @return True to exit from timer recovery state
    bool processSFrame(const ISDNFrame* frame);
    // Process a received U frame
    // @param newState The new state if true is returned
    // @param confirmation True if the new state is Established or Released and
    //  this is a confirmation
    // @return True to change state
    bool processUFrame(const ISDNFrame* frame, State& newState,
	bool& confirmation);
    // Accept frame according to Q.921 5.8.5
    // Update counters.
    // If not accepted the frame is rejected or dropped
    // reject is set to true if the frame is rejected
    bool acceptFrame(ISDNFrame* frame, bool& reject);
    // Update rejected frames counter. Print message. Send FRMR (frame reject)
    void rejectFrame(const ISDNFrame* frame, const char* reason = 0);
    // Update dropped frames counter. Print message
    void dropFrame(const ISDNFrame* frame, const char* reason = 0);
    // Send S frames other then UI frames
    bool sendUFrame(ISDNFrame::Type type, bool command, bool pf,
	bool retrans = false);
    // Send U frames
    bool sendSFrame(ISDNFrame::Type type, bool command, bool pf);
    // Send a frame to remote peer
    // @param frame Frame to send
    // @return False if the operation failed
    bool sendFrame(const ISDNFrame* frame);
    // Send pending outgoing I frames
    // @param retrans: True   Send all transmission window
    //                 False  Send only the unsent frames in transmission window
    // @return True if a transmission took place
    bool sendOutgoingData(bool retrans = false);
    // Start/Stop T200. Stop/Start T203
    // If start is false reset N200 (retransmission counter)
    // @param start True to start. False to stop
    // @param t203 Start/don't start T203. Ignored if start is false
    // @param time Current time if known
    void timer(bool start, bool t203, u_int64_t time = 0);

    ISDNQ921Management* m_management;    // TEI management component
    // State variables
    bool m_remoteBusy;                   // Remote peer is busy: don't send any I frames
    bool m_timerRecovery;                // T200 expired
    bool m_rejectSent;                   // True if we've sent a REJ frame
    bool m_pendingDMSabme;               // True if we have a pending SABME on DM received
    bool m_lastPFBit;                    // Last P/F bit sent with an I or S frame
    u_int8_t m_vs;                       // Sequence number of the next transmitted I frame
    u_int8_t m_va;                       // Last ack'd I frame by remote peer
    u_int8_t m_vr;                       // Expected I frame sequence number
    // Timers and counters
    SignallingTimer m_retransTimer;      // T200: Retransmission interval
    SignallingTimer m_idleTimer;         // T203: Channel idle interval
    SignallingCounter m_window;          // Maximum/current number of pending outgoing I frames
    SignallingCounter m_n200;            // Maximum/current retransmission counter
    // Data
    ObjList m_outFrames;                 // Outgoing I frames queue
    // Statistics
    u_int32_t m_txFrames;                // The number of frames accepted by layer 1 to be transmitted
    u_int32_t m_txFailFrames;            // The number of frames not accepted by layer 1 to be transmitted
    u_int32_t m_rxFrames;                // The number of successfully parsed frames
    u_int32_t m_rxRejectedFrames;        // The number of rejected frames. Doesn't include dropped frames
    u_int32_t m_rxDroppedFrames;         // The number of dropped frames. Doesn't include rejected frames
    u_int32_t m_hwErrors;                // The number of hardware notifications
    // Debug flags
    bool m_printFrames;                  // Print frames to output
    bool m_extendedDebug;                // Extended debug flag
    // Flags used to avoid repetitive errors
    bool m_errorSend;                    // Send error
    bool m_errorReceive;                 // Receive error
};

/**
 * This class is intended to be used as a proxy between an ISDN Layer 3 and multiple
 *  Layer 2 objects sharing the same signalling interface.
 * It is used for BRI TEI management or PRI with D-channel backup.
 * It also keeps a list of ISDN Layer 2 object(s) used for the designated purpose
 * @short ISDN Layer 2 BRI TEI management or PRI with D-channel(s) backup
 */
class YSIG_API ISDNQ921Management : public ISDNLayer2, public ISDNLayer3, public SignallingReceiver, public SignallingDumpable
{
    YCLASS3(ISDNQ921Management,ISDNLayer2,ISDNLayer3,SignallingReceiver)
public:
    /**
     * Constructor - initialize this Layer 2 and the component
     * @param params Layer's parameters
     * @param name Optional name of the component
     * @param net True if managing the network side of Q.921
     */
    ISDNQ921Management(const NamedList& params, const char* name = 0, bool net = true);

    /**
     * Destructor
     */
    virtual ~ISDNQ921Management();

    /**
     * Configure and initialize Q.921 Management and its children
     * @param config Optional configuration parameters override
     * @return True if Q.921 management was initialized properly
     */
    virtual bool initialize(const NamedList* config);

    /**
     * Set the engine for this management and all Layer 2 children
     * @param eng Pointer to the engine that will manage this mangement
     */
    virtual void engine(SignallingEngine* eng);

    /**
     * Implements Q.921 DL-ESTABLISH and DL-RELEASE request primitives
     * @param tei This layer TEI (-1 to apply it to all targets this object may have attached)
     * @param establish True to establish. False to release
     * @param force True to establish even if we already are established.
     * @return True if the request was accepted
     */
    virtual bool multipleFrame(u_int8_t tei, bool establish, bool force);

    /**
     * Implements Q.921 DL-DATA and DL-UNIT DATA request primitives
     * @param data Data to send
     * @param tei This layer TEI (-1 for broadcast)
     * @param ack True to send an acknowledged frame, false to send an unacknowledged one
     * @return True if the request was accepted
     */
    virtual bool sendData(const DataBlock& data, u_int8_t tei, bool ack);

    /**
     * Implements Q.921 send frame to the interface
     * @param frame The frame to be sent
     * @param q921 Pointer to the Q.921 that sends the frame, if any
     * @return True if the frame was sent
     */
    bool sendFrame(const ISDNFrame* frame, const ISDNQ921* q921 = 0);


    /**
     * Emergency release.
     * Cleanup all Layer 2 objects attached to this Management
     */
    virtual void cleanup();

    /**
     * Implements Q.921 DL-ESTABLISH indication/confirmation primitive:
     *  'multiple frame acknowledged' mode established
     * @param tei This layer TEI
     * @param confirm True if this is a confirmation of a previous request,
     *  false if it is an indication of state change on remote request
     * @param timeout True if the reason is a timeout
     * @param layer2 Pointer to the notifier
     */
    virtual void multipleFrameEstablished(u_int8_t tei, bool confirm, bool timeout, ISDNLayer2* layer2);

    /**
     * Implements Q.921 DL-RELEASE indication/confirmation primitive:
     *  'multiple frame acknowledged' mode released
     * @param tei This layer TEI
     * @param confirm True if this is a confirmation of a previous request,
     *  false if it is an indication of state change on remote request
     * @param timeout True if the reason is a timeout.
     * @param layer2 Pointer to the notifier
     */
    virtual void multipleFrameReleased(u_int8_t tei, bool confirm, bool timeout, ISDNLayer2* layer2);

    /**
     * Notification from layer 2 of data link set/release command or response
     * Used for stateless layer 2
     * @param tei This layer TEI
     * @param cmd True if received a command, false if received a response
     * @param value The value of the notification
     * If 'cmd' is true (command), the value is true if a request to establish data link was received
     *   or false if received a request to release data link
     * If 'cmd' is false (response), the value is the response
     * @param layer2 Pointer to the notifier
     */
    virtual void dataLinkState(u_int8_t tei, bool cmd, bool value, ISDNLayer2* layer2);

    /**
     * Implements Q.921 DL-DATA and DL-UNIT DATA indication primitives
     * Receive data from an encapsulated Layer 2 and send it to the attached Layer 3
     * @param data Received data
     * @param tei The TEI as received in the packet
     * @param layer2 Pointer to the sender
     */
    virtual void receiveData(const DataBlock& data, u_int8_t tei, ISDNLayer2* layer2);

protected:
    /**
     * Method called periodically to check timeouts
     * This method is thread safe
     * @param when Time to use as computing base for events and timeouts
     */
    virtual void timerTick(const Time& when);

    /**
     * Process a Signalling Packet received by the interface.
     * Parse the data and send all non-UI frames to the appropriate Layer 2.
     * Process UI frames
     * @return True if message was successfully processed
     */
    virtual bool receivedPacket(const DataBlock& packet);

    /**
     * Process a notification generated by the attached interface
     * @param event Notification event reported by the interface
     * @return True if notification was processed
     */
    virtual bool notify(SignallingInterface::Notification event);

    /**
     * Process UI frames carrying TEI management messages
     * @param frame The parsed frame
     * @return False if the frame is not a TEI management one, true otherwise
     */
    bool processTeiManagement(ISDNFrame* frame);

    /**
     * Send a TEI management frame
     * @param type Type of the frame to send
     * @param ri Reference number to send in frame
     * @param ai Action indicator to send in frame
     * @param tei The TEI to send the frame to, default use broadcast
     * @param pf The Poll/Final bit to set in the frame
     * @return True if frame was sent successfully
     */
    bool sendTeiManagement(ISDNFrame::TeiManagement type, u_int16_t ri, u_int8_t ai, u_int8_t tei = 127, bool pf = false);

    /**
     * Process TEI request message and send back to TE:
     *  TEI Assigned message if the request succeeded;
     *  TEI Denied message with the received reference number if the
     *   reference number is already in use;
     *  TEI Denied message with the reference number set to 127 if
     *   there is no TEI value available.
     * @param ri The reference number
     * @param ai Action indicator
     * @param pf The Poll/Final bit in the incoming frame
     */
    void processTeiRequest(u_int16_t ri, u_int8_t ai, bool pf);

    /**
     * Process Tei remove message removing the tei(s) contained by ai
     * @param ai Contains the TEI value to remove or 127 to remove all TEI values
     */
    void processTeiRemove(u_int8_t ai);

    /**
     * Process TEI Check Request message and send to the NET a message with
     *  the TEI and the asociated reference number
     * @param ai Contains the TEI value to check or 127 to check all TEI values
     * @param pf The Poll/Final bit in the incoming frame
     */
    void processTeiCheckRequest(u_int8_t ai, bool pf);

    /**
     * Process TEI Check Response message and set the check flag to true to know that we have a response for that TEI
     * @param ri The associated reference number to the ai
     * @param ai The TEI value as received in the answer
     */
    void processTeiCheckResponse(u_int16_t ri, u_int8_t ai);

    /**
     * Process TEI Assigned message
     * @param ri The reference number assigned to the ai
     * @param ai The TEI value assigned
     */
    void processTeiAssigned(u_int16_t ri, u_int8_t ai);

    /**
     * Process TEI Denied message
     * @param ri The reference number of the denied request
     */
    void processTeiDenied(u_int16_t ri);

    /**
     * Process TEI verify
     * @param ai The TEI value of the message initiator
     * @param pf The Poll/Final bit in the incoming frame
     */
    void processTeiVerify(u_int8_t ai, bool pf);

    /**
     * Send TEI request message
     * @param tei TEI value to assign
     */
    void sendTeiReq(u_int8_t tei);

    /**
     * Send a TEI remove frame
     */
    void sendTeiRemove();

private:
    ISDNQ921* m_layer2[127];             // The list of Layer 2 objects attached to this Layer 3
    SignallingTimer m_teiManTimer;       // T202
    SignallingTimer m_teiTimer;          // T201
};

/**
 * Q.921 ISDN Layer 2 pasive (stateless) implementation on top of a hardware HDLC interface
 * @short Stateless pasive ISDN Q.921 implementation on top of a hardware interface
 */
class YSIG_API ISDNQ921Passive : public ISDNLayer2, public SignallingReceiver, public SignallingDumpable
{
    YCLASS2(ISDNQ921Passive,ISDNLayer2,SignallingReceiver)
public:
    /**
     * Constructor
     * Initialize this object and the component
     * @param params Layer's and @ref TelEngine::ISDNLayer2 parameters
     * @param name Name of this component
     */
    ISDNQ921Passive(const NamedList& params, const char* name = 0);

    /**
     * Destructor
     */
    virtual ~ISDNQ921Passive();

    /**
     * Emergency release
     * Reset all data. Set state to 'Released'
     * This method is thread safe
     */
    virtual void cleanup();

    /**
     * Configure and initialize the passive Q.921 and its interface
     * @param config Optional configuration parameters override
     * @return True if Q.921 and the interface were initialized properly
     */
    virtual bool initialize(const NamedList* config);

    /**
     * Set debug data of this layer
     * @param printFrames Enable/disable frame printing on output
     * @param extendedDebug Enable/disable hex data dump if print frames is enabled
     */
    inline void setDebug(bool printFrames, bool extendedDebug)
	{ m_extendedDebug = ((m_printFrames = printFrames) && extendedDebug); }

protected:
    /**
     * Detach links. Disposes memory
     */
    virtual void destroyed()
    {
	ISDNLayer2::attach(0);
	TelEngine::destruct(SignallingReceiver::attach(0));
	SignallingComponent::destroyed();
    }

    /**
     * Method called periodically to check timeouts
     * This method is thread safe
     * @param when Time to use as computing base for events and timeouts
     */
    virtual void timerTick(const Time& when);

    /**
     * Process a packet received by the receiver's interface
     * This method is thread safe
     * @param packet The received packet
     * @return True if message was successfully processed
     */
    virtual bool receivedPacket(const DataBlock& packet);

    /**
     * Process a notification generated by the attached interface
     * This method is thread safe
     * @param event Notification event reported by the interface
     * @return True if notification was processed
     */
    virtual bool notify(SignallingInterface::Notification event);

private:
    virtual bool control(NamedList& params)
	{ return SignallingDumpable::control(params,this); }
    // Filter received frames. Accept only frames that would generate a notification to the upper layer:
    // UI/I, and Valid SABME/DISC/UA/DM
    // On success, if frame is not a data one, prepare cmd and value to notify layer 3
    bool acceptFrame(ISDNFrame* frame, bool& cmd, bool& value);
    // Show debug message. Count dropped frames
    bool dropFrame(const ISDNFrame* frame, const char* reason = 0);

    bool m_checkLinkSide;                // Check if this is the correct side of the data link
    SignallingTimer m_idleTimer;         // Channel idle interval
    u_int8_t m_lastFrame;                // Transmitter send number of the last received frame
    u_int32_t m_rxFrames;                // The number of successfully parsed frames
    u_int32_t m_rxDroppedFrames;         // The number of dropped frames. Doesn't include rejected frames
    u_int32_t m_hwErrors;                // The number of hardware notifications
    bool m_printFrames;                  // Print frames to output
    bool m_extendedDebug;                // Extended debug flag
    bool m_errorReceive;                 // Receive error
};

/**
 * The common client side of SIGTRAN ISDN Q.921 User Adaptation (RFC4233)
 * @short Client side of SIGTRAN ISDN Q.921 UA
 */
class YSIG_API ISDNIUAClient : public SIGAdaptClient
{
    YCLASS(ISDNIUAClient,SIGAdaptClient)
public:
    /**
     * Contructor of an empty IUA client
     */
    inline ISDNIUAClient(const NamedList& params)
	: SIGAdaptClient(params.safe("ISDNIUAClient"),&params,1,9900)
	{ }

    virtual bool processMSG(unsigned char msgVersion, unsigned char msgClass,
	unsigned char msgType, const DataBlock& msg, int streamId);
};

/**
 * RFC4233 ISDN Layer 2 implementation over SCTP/IP
 * IUA is intended to be used as a Provider-User where Q.921 runs on a
 *  Signalling Gateway and the user (Q.931) runs on an Application Server.
 * @short SIGTRAN ISDN Q.921 User Adaptation Layer
 */
class YSIG_API ISDNIUA : public ISDNLayer2, public SIGAdaptUser
{
    friend class ISDNIUAClient;
    YCLASS(ISDNIUA,ISDNLayer2)
public:
    /**
     * Constructor
     * Initialize this object and the layer 2
     * @param params Object and Layer 2 parameters
     * @param name Optional name for Layer 2
     * @param tei Value of TEI for this component
     */
    ISDNIUA(const NamedList& params, const char* name = 0, u_int8_t tei = 0);

    /**
     * Destructor
     */
    virtual ~ISDNIUA();

    /**
     * Configure and initialize IUA and its transport
     * @param config Optional configuration parameters override
     * @return True if IUA and the transport were initialized properly
     */
    virtual bool initialize(const NamedList* config);

    /**
     * Implements Q.921 DL-ESTABLISH and DL-RELEASE request primitives
     * @param tei This layer's TEI
     * @param establish True to establish. False to release
     * @param force True to establish even if we already are in this mode. This
     *  parameter is ignored if establish is false
     * @return True if the request was accepted
     */
    virtual bool multipleFrame(u_int8_t tei, bool establish, bool force);

    /**
     * Implements Q.921 DL-DATA and DL-UNIT DATA request primitives
     * @param data Data to send
     * @param tei The TEI to send with the data frane
     * @param ack True to send an acknowledged frame, false to send an unacknowledged one
     * @return False if the request was not accepted or send operation failed
     */
    virtual bool sendData(const DataBlock& data, u_int8_t tei, bool ack);

    /**
     * Emergency release.
     */
    virtual void cleanup();

    /**
     * Traffic activity state change notification
     * @param active True if the ASP is active and traffic is allowed
     */
    virtual void activeChange(bool active);

    /**
     * Retrieve the numeric Interface Identifier (if any)
     * @return IID value, -1 if not set
     */
    inline int32_t iid() const
	{ return m_iid; }

protected:
    ISDNIUAClient* client() const
	{ return static_cast<ISDNIUAClient*>(adaptation()); }
    virtual bool processMGMT(unsigned char msgType, const DataBlock& msg, int streamId);
    virtual bool processQPTM(unsigned char msgType, const DataBlock& msg, int streamId);
    int32_t m_iid;
};

/**
 * Q.931 ISDN Layer 3 message Information Element
 * @short A Q.931 ISDN Layer 3 message Information Element
 */
class YSIG_API ISDNQ931IE : public NamedList
{
    friend class ISDNQ931Message;
public:
    /**
     * Keep IE type enumerations. See Q.931 4.5
     */
    enum Type {
	// Fixed (1 byte) length information element
	Shift = 0x90,                    // Shift
	MoreData = 0xa0,                 // More data
	SendComplete = 0xa1,             // Sending complete
	Congestion = 0xb0,               // Congestion level
	Repeat = 0xd0,                   // Repeat indicator
	// Variable length information element
	Segmented = 0x00,                // Segmented message
	BearerCaps = 0x04,               // Bearer capability
	Cause = 0x08,                    // Cause
	CallIdentity = 0x10,             // Call identity
	CallState = 0x14,                // Call state
	ChannelID = 0x18,                // Channel identification
	Progress = 0x1e,                 // Progress indicator
	NetFacility = 0x20,              // Network-specific facilities
	Notification = 0x27,             // Notification indicator
	Display = 0x28,                  // Display
	DateTime = 0x29,                 // Date/time
	Keypad = 0x2c,                   // Keypad facility
	Signal = 0x34,                   // Signal
	ConnectedNo = 0x4c,              // Connected number (Q.951)
	CallingNo = 0x6c,                // Calling party number
	CallingSubAddr = 0x6d,           // Calling party subaddress
	CalledNo = 0x70,                 // Called party number
	CalledSubAddr = 0x71,            // Called party subaddress
	NetTransit = 0x78,               // Transit network selection
	Restart = 0x79,                  // Restart indicator
	LoLayerCompat = 0x7c,            // Low layer compatibility
	HiLayerCompat = 0x7d,            // High layer compatibility
	// Not used
	UserUser = 0x7e,                 // User-user
	Escape = 0x7f,                   // Escape for extension
    };

    /**
     * Constructor
     * Constructs an unknown IE with raw data
     * @param type The type of this IE
     */
    ISDNQ931IE(u_int16_t type);

    /**
     * Destructor
     */
    virtual ~ISDNQ931IE();

    /**
     * Get the type of this IE
     * @return The type of this IE
     */
    inline u_int8_t type() const
	{ return (u_int8_t)m_type; }

    /**
     * Add a parameter using the IE name as prefix
     * @param name Parameter name
     * @param value Parameter value
     */
    inline void addParamPrefix(const char* name, const char* value)
	{ addParam(*this+"."+name,value); }

    /**
     * Put this message into a string for debug purposes
     * @param dest The destination string
     * @param extendedDebug True to add the content of this IE and dump data.
     *  If false, only the IE name is added to the destination string
     * @param before Optional string to be added before
     */
    void toString(String& dest, bool extendedDebug, const char* before = 0);

    /**
     * Get the string associated with a given IE type
     * @param type The IE type whose string we want to get
     * @param defVal The value to return if not found
     * @return Pointer to the requested string or defValue
     */
    static inline const char* typeName(int type, const char* defVal = 0)
	{ return lookup(type,s_type,defVal); }

    /**
     * Keep the string associated with IE types
     */
    static const TokenDict s_type[];

    /**
     * Internally used buffer
     */
    DataBlock m_buffer;

private:
    u_int16_t m_type;                    // IE type
};

/**
 * Q.931 ISDN Layer 3 message
 * @short A Q.931 ISDN Layer 3 message
 */
class YSIG_API ISDNQ931Message : public SignallingMessage
{
public:
    /**
     * Message type enumeration
     */
    enum Type {
	Alerting = 0x01,                 // ALERTING
	Proceeding = 0x02,               // CALL PROCEEDING
	Connect = 0x07,                  // CONNECT
	ConnectAck = 0x0f,               // CONNECT ACK
	Progress = 0x03,                 // PROGRESS
	Setup = 0x05,                    // SETUP
	SetupAck = 0x0d,                 // SETUP ACK
	Resume = 0x26,                   // RESUME
	ResumeAck = 0x2e,                // RESUME ACK
	ResumeRej = 0x22,                // RESUME REJECT
	Suspend = 0x25,                  // SUSPEND
	SuspendAck = 0x2d,               // SUSPEND ACK
	SuspendRej = 0x21,               // SUSPEND REJECT
	UserInfo = 0x20,                 // USER INFO
	Disconnect = 0x45,               // DISCONNECT
	Release = 0x4d,                  // RELEASE
	ReleaseComplete = 0x5a,          // RELEASE COMPLETE
	Restart = 0x46,                  // RESTART
	RestartAck = 0x4e,               // RESTART ACK
	Segment = 0x60,                  // SEGMENT
	CongestionCtrl = 0x79,           // CONGESTION CONTROL
	Info = 0x7b,                     // INFORMATION
	Notify = 0x6e,                   // NOTIFY
	Status = 0x7d,                   // STATUS
	StatusEnquiry = 0x75,            // STATUS ENQUIRY
    };

    /**
     * Constructor
     * Constructs a message from given data. Used for incoming messages
     * @param type Message type
     * @param initiator The call initiator flag: True: this is the initiator
     * @param callRef The call reference
     * @param callRefLen The call reference length
     */
    ISDNQ931Message(Type type, bool initiator, u_int32_t callRef, u_int8_t callRefLen);

    /**
     * Constructor
     * Constructs a message with dummy call reference
     * @param type Message type
     */
    ISDNQ931Message(Type type);

    /**
     * Constructor
     * Constructs a message for a given call. Used for outgoing messages
     * @param type Message type
     * @param call The call this message belongs to
     */
    ISDNQ931Message(Type type, ISDNQ931Call* call);

    /**
     * Destructor
     */
    virtual ~ISDNQ931Message();

    /**
     * Get the type of this message
     * @return The type of this message as enumeration
     */
    inline Type type() const
	{ return m_type; }

    /**
     * Check if the sender of this message is the call initiator
     * @return True if the sender of this message is the call initiator
     */
    inline bool initiator() const
	{ return m_initiator; }

    /**
     * Get the id of the call this message belongs to
     * @return The call reference
     */
    inline u_int32_t callRef() const
	{ return m_callRef; }

    /**
     * Get the length of the call reference
     * @return The length of the call reference
     */
    inline u_int8_t callRefLen() const
	{ return m_callRefLen; }

    /**
     * Check if this message has a dummy call reference
     * @return True if this message has a dummy call reference
     */
    inline bool dummyCallRef() const
	{ return m_dummy; }

    /**
     * Check if this message contains unknown mandatory IE(s)
     * @return True if this message contains unknown mandatory IE(s)
     */
    inline bool unknownMandatory() const
	{ return m_unkMandatory; }

    /**
     * Set the unknown mandatory IE(s) flag
     */
    inline void setUnknownMandatory()
	{ m_unkMandatory = true; }

    /**
     * Get the IE list of this message
     * @return A valid pointer to the list of this message's IEs
     */
    inline ObjList* ieList()
	{ return &m_ie; }

    /**
     * Get a pointer to the first IE with the given type
     * @param type Requested IE's type
     * @param base Optional search starting element. If 0, search is started from the first IE following base
     * @return Pointer to the IE or 0 if not found
     */
    ISDNQ931IE* getIE(ISDNQ931IE::Type type, ISDNQ931IE* base = 0);

    /**
     * Remove an IE from list without destroying it
     * @param type Requested IE's type
     * @param base Optional search starting element. If 0, search is started from the first IE following base
     * @return Pointer to the IE or 0 if not found
     */
    ISDNQ931IE* removeIE(ISDNQ931IE::Type type, ISDNQ931IE* base = 0);

    /**
     * Get the value of a given parameter of a given IE
     * @param type Requested IE's type
     * @param param Requested IE's parameter. Set to 0 to use IE's name
     * @param defVal Default value to return if IE is missing or the parameter is missing
     * @return Pointer to the requested value or 0
     */
    inline const char* getIEValue(ISDNQ931IE::Type type, const char* param,
	const char* defVal = 0)
    {
	ISDNQ931IE* ie = getIE(type);
	return (ie ? ie->getValue(param?param:ie->c_str(),defVal) : defVal);
    }

    /**
     * Append an IE with a given parameter
     * @param type IE's type
     * @param param IE's parameter. Set to 0 to use IE's name
     * @param value IE parameter's value
     * @return Pointer to the requested value or 0
     */
    inline ISDNQ931IE* appendIEValue(ISDNQ931IE::Type type, const char* param,
	const char* value)
    {
	ISDNQ931IE* ie = new ISDNQ931IE(type);
	ie->addParam(param?param:ie->c_str(),value);
	appendSafe(ie);
	return ie;
    }

    /**
     * Append an information element to this message
     * @param ie Information element to add
     * @return True if the IE was added or replaced, false if it was invalid
     */
    inline bool append(ISDNQ931IE* ie)
	{ return 0 != m_ie.append(ie); }

    /**
     * Append/insert an information element to this message. Check the IE list consistency
     * The given IE is 'consumed': deleted or appended to the list
     * @param ie Information element to add
     * @return True if the IE was added or replaced, false if it was invalid
     */
    bool appendSafe(ISDNQ931IE* ie);

    /**
     * Put this message into a string for debug purposes
     * @param dest The destination string
     * @param extendedDebug True to add the content of IEs and dump data.
     *  If false, only the IE name is added to the destination string
     * @param indent The line indent
     */
    void toString(String& dest, bool extendedDebug, const char* indent = 0) const;

    /**
     * Get a pointer to a data member or this message
     * @param name Object name
     * @return The requested pointer or 0 if not exists
     */
    virtual void* getObject(const String& name) const;

    /**
     * Encode this message
     * If message segmentation is allowed and the message is longer then maximum allowed,
     *  split it into Segment messages
     * @param parserData The parser settings
     * @param dest The destination list.
     *  If 1 is returned the list contains a DataBuffer with this message.
     *  If more then 1 is returned, the list is filled with data buffers with Segment messages
     * @return The number of segments on success or 0 on failure.
     */
    u_int8_t encode(ISDNQ931ParserData& parserData, ObjList& dest);

    /**
     * Parse received data
     * If the message type is Segment, decode only the header and the first IE
     *  If valid, fills the given buffer with the rest of the message. If segData is 0, drop the message.
     * @param parserData The parser settings
     * @param buffer The received data
     * @param segData Segment message data. If 0, received segmented messages will be dropped
     * @return Valid ISDNQ931Message pointer on success or 0
     */
    static ISDNQ931Message* parse(ISDNQ931ParserData& parserData,
	const DataBlock& buffer, DataBlock* segData);

    /**
     * Get the string associated with a given message type
     * @param t The message type whose string we want to get
     * @return Pointer to the string associated with the given message type or 0
     */
    static inline const char* typeName(int t)
	{ return lookup(t,s_type,"Unknown"); }

    /**
     * Keep the string associated with message types
     */
    static const TokenDict s_type[];

    /**
     * Internally used buffer for debug purposes
     */
    DataBlock m_buffer;

private:
    Type m_type;                         // Message type
    bool m_initiator;                    // The call initiator flag: True: this is the initiator
    u_int32_t m_callRef;                 // The call reference
    u_int8_t m_callRefLen;               // The call reference length
    bool m_unkMandatory;                 // True if this message contains unknown mandatory IE(s)
    bool m_dummy;                        // True if this message has a dummy call reference
    ObjList m_ie;                        // IE list
};

/**
 * Extract data from IEs. Append IEs to Q.931 messages
 * @short A Q.931 message IE data processor
 */
class YSIG_API ISDNQ931IEData
{
    friend class ISDNQ931Call;
    friend class ISDNQ931CallMonitor;
    friend class ISDNQ931;
    friend class ISDNQ931Monitor;
private:
    // Constructor
    ISDNQ931IEData(bool bri = false);
    // Process received IEs
    // If add is true, append an IE to the message
    // If add is false, extract data from message. Set data to default values if IE is missing
    // @return False if the IE is missing when decoding or the IE wasn't added
    bool processBearerCaps(ISDNQ931Message* msg, bool add, ISDNQ931ParserData* data = 0);
    bool processCause(ISDNQ931Message* msg, bool add, ISDNQ931ParserData* data = 0);
    bool processDisplay(ISDNQ931Message* msg, bool add, ISDNQ931ParserData* data = 0);
    bool processKeypad(ISDNQ931Message* msg, bool add, ISDNQ931ParserData* data = 0);
    bool processChannelID(ISDNQ931Message* msg, bool add, ISDNQ931ParserData* data = 0);
    bool processProgress(ISDNQ931Message* msg, bool add, ISDNQ931ParserData* data = 0);
    bool processRestart(ISDNQ931Message* msg, bool add, ISDNQ931ParserData* data = 0);
    bool processNotification(ISDNQ931Message* msg, bool add, ISDNQ931ParserData* data = 0);
    bool processCalledNo(ISDNQ931Message* msg, bool add, ISDNQ931ParserData* data = 0);
    bool processCallingNo(ISDNQ931Message* msg, bool add, ISDNQ931ParserData* data = 0);

    // IE parameters
    String m_display;                    // Display: The data
    String m_callerNo;                   // CallingNo: Number
    String m_callerType;                 // CallingNo: Number type
    String m_callerPlan;                 // CallingNo: Number plan
    String m_callerPres;                 // CallingNo: Number presentation
    String m_callerScreening;            // CallingNo: Number screening
    String m_calledNo;                   // CalledNo: Number
    String m_calledType;                 // CalledNo: Number type
    String m_calledPlan;                 // CalledNo: Number plan
    String m_transferCapability;         // BearerCaps: Transfer capability
    String m_transferMode;               // BearerCaps: Transfer mode
    String m_transferRate;               // BearerCaps: Transfer rate
    String m_format;                     // BearerCaps: Layer 1 protocol
    String m_reason;                     // Cause
    String m_keypad;                     // Keypad: 'keypad' parameter
    String m_progress;                   // Progress: Progress description
    String m_notification;               // Notify: Notification indicator
    bool m_bri;                          // ChannelID: BRI interface flag
    bool m_channelMandatory;             // ChannelID: Indicated channel is mandatory/preferred
    bool m_channelByNumber;              // ChannelID: m_channels contains a channel list or a slot map
    String m_channelType;                // ChannelID: Channel type
    String m_channelSelect;              // ChannelID: Channel select
    String m_channels;                   // ChannelID: Channel list or slot map
    String m_restart;                    // Restart: The class of restarting circuits
};

/**
 * Q.931 ISDN call and call controller state
 * @short Q.931 ISDN call and call controller state
 */
class YSIG_API ISDNQ931State
{
public:
    /**
     * Call and call controller state enumeration values
     */
    enum State {
	// Common state
	Null                    = 0x00,  // Null
	// Call states
	CallInitiated           = 0x01,  // Call initiated: sent SETUP
	OverlapSend             = 0x02,  // Overlap sending
	OutgoingProceeding      = 0x03,  // Outgoing call proceeding: received valid CALL PROCEEDING
	CallDelivered           = 0x04,  // Call delivered: received valid ALERTING
	CallPresent             = 0x06,  // Call present: received valid SETUP or recover from STATUS
	CallReceived            = 0x07,  // Call received: sent ALERTING or recover from STATUS
	ConnectReq              = 0x08,  // Connect request: sent/received valid CONNECT or recover from STATUS
	IncomingProceeding      = 0x09,  // Incoming call proceeding: sent CALL PROCEEDING or recover from STATUS
	Active                  = 0x0a,  // Active: sent/received valid CONNECT ACK
	DisconnectReq           = 0x0b,  // Disconnect request: sent DISCONNECT
	DisconnectIndication    = 0x0c,  // Disconnect indication: received valid DISCONNECT
	SuspendReq              = 0x0f,  // Suspend request
	ResumeReq               = 0x11,  // Resume reques
	ReleaseReq              = 0x13,  // Release request: sent/received valid RELEASE
	CallAbort               = 0x16,  // Call abort: received STATUS in Null state with remote not in Null state
	OverlapRecv             = 0x19,  // Overlap receiving
	// Call controller states
	RestartReq              = 0x3d,  // Restart request
	Restart                 = 0x3e,  // Restart
    };

    /**
     * Constructor
     */
   inline ISDNQ931State() : m_state(Null)
	{ }

    /**
     * Get the state
     * @return The state as enumeration
     */
    inline State state() const
	{ return m_state; }

    /**
     * Get the text associated with a given state value
     * @param s The requested state value
     * @return The text associated with the given state value or 0
     */
    static const char* stateName(u_int8_t s)
	{ return lookup(s,s_states,0); }

    /**
     * Keep the association between state values and their texts
     */
    static const TokenDict s_states[];

protected:
    /**
     * Check if a received message type is valid in the current call state
     * @param type The type of the received message
     * @param retrans Optional flag to set on failure if the message is a retransmission
     * @return False if the message is not valid in the current call state
     */
    bool checkStateRecv(int type, bool* retrans);

    /**
     * Check if a message is allowed to be sent in the current call state
     * @param type The type of the received message
     * @return False if the message is not valid in the current call state
     */
    bool checkStateSend(int type);

    /**
     * The call and call controller state
     */
    State m_state;

};

/**
 * Q.931 ISDN call
 * @short A Q.931 ISDN call
 */
class YSIG_API ISDNQ931Call : public ISDNQ931State, public SignallingCall
{
    friend class ISDNQ931;
public:
    /**
     * Destructor
     */
    virtual ~ISDNQ931Call();

    /**
     * Get the id of this call
     * @return The call reference
     */
    inline u_int32_t callRef() const
	{ return m_callRef; }

    /**
     * Get the length of the call reference
     * @return The length of the call reference
     */
    inline u_int32_t callRefLen() const
	{ return m_callRefLen; }

    /**
     * Get the Terminal Equipment Indicator for this call
     * @return Value of TEI used in this call
     */
    inline u_int8_t callTei() const
	{ return m_tei; }

    /**
     * Get the circuit this call had reserved
     * @return The circuit reserved by this call
     */
    inline SignallingCircuit* circuit()
	{ return m_circuit; }

    /**
     * Set termination (and destroy) flags
     * This method is thread safe
     * @param destroy The destroy flag. If true, the call will be destroyed
     * @param reason Terminate reason
     */
    void setTerminate(bool destroy, const char* reason);

    /**
     * Send an event to this call
     * This method is thread safe
     * @param event The sent event
     * @return True if the operation succedded
     */
    virtual bool sendEvent(SignallingEvent* event);

    /**
     * Get an event from this call
     * This method is thread safe
     * @param when The current time
     * @return SignallingEvent pointer or 0 if no events
     */
    virtual SignallingEvent* getEvent(const Time& when);

    /**
     * Data link (interface) state notification
     * This method is thread safe
     * @param up The data link state
     */
    void dataLinkState(bool up);

    /**
     * Get a pointer to a data member or this call
     * @param name Object name
     * @return The requested pointer or 0 if not exists
     */
    virtual void* getObject(const String& name) const;

protected:
    /**
     * Constructor
     * @param controller The call controller
     * @param outgoing The call direction
     * @param callRef The call reference
     * @param callRefLen The call reference length in bytes
     * @param tei The Terminal Equipment Identifier used in this call
     */
    ISDNQ931Call(ISDNQ931* controller, bool outgoing, u_int32_t callRef,
	u_int8_t callRefLen, u_int8_t tei = 0);

    /**
     * Send RELEASE COMPLETE if not in Null state.
     * Clear all call data.
     * Remove from controller's queue. Decrease the object's refence count
     * @param reason Optional release reason. If missing, the last reason is used
     * @param diag Optional hexified string for the cause diagnostic
     * @return Pointer to an SignallingEvent of type Release, with no message
     */
    SignallingEvent* releaseComplete(const char* reason = 0, const char* diag = 0);

    /**
     * Get an event from the circuit reserved for this call
     * @param when The current time
     * @return SignallingEvent pointer or 0 if no events
     */
    SignallingEvent* getCircuitEvent(const Time& when);

private:
    // Reserve and connect a circuit. Change the reserved one if it must to
    bool reserveCircuit();
    // Process call when terminate flag is set. Check timeout
    // @param msg Optional message extracted from queue
    SignallingEvent* processTerminate(ISDNQ931Message* msg = 0);
    // Check timer(s)
    SignallingEvent* checkTimeout(u_int64_t time);
    // Check received messages for valid state
    // True to send status if not accepted
    bool checkMsgRecv(ISDNQ931Message* msg, bool status);
    // Process received messages
    // @param msg Valid ISDNQ931Message pointer
    SignallingEvent* processMsgAlerting(ISDNQ931Message* msg);
    SignallingEvent* processMsgCallProceeding(ISDNQ931Message* msg);
    SignallingEvent* processMsgConnect(ISDNQ931Message* msg);
    SignallingEvent* processMsgConnectAck(ISDNQ931Message* msg);
    SignallingEvent* processMsgDisconnect(ISDNQ931Message* msg);
    SignallingEvent* processMsgInfo(ISDNQ931Message* msg);
    SignallingEvent* processMsgNotify(ISDNQ931Message* msg);
    SignallingEvent* processMsgProgress(ISDNQ931Message* msg);
    SignallingEvent* processMsgRelease(ISDNQ931Message* msg);
    SignallingEvent* processMsgSetup(ISDNQ931Message* msg);
    SignallingEvent* processMsgSetupAck(ISDNQ931Message* msg);
    SignallingEvent* processMsgStatus(ISDNQ931Message* msg);
    SignallingEvent* processMsgStatusEnquiry(ISDNQ931Message* msg);
    // Send message
    // @param msg Pointer to SignallingMessage with parameters
    bool sendAlerting(SignallingMessage* sigMsg);
    bool sendCallProceeding(SignallingMessage* sigMsg);
    bool sendConnect(SignallingMessage* sigMsg);
    bool sendConnectAck(SignallingMessage* sigMsg);
    bool sendDisconnect(SignallingMessage* sigMsg);
    bool sendInfo(SignallingMessage* sigMsg);
    bool sendProgress(SignallingMessage* sigMsg);
    bool sendRelease(const char* reason = 0, SignallingMessage* sigMsg = 0);
    bool sendReleaseComplete(const char* reason = 0, const char* diag = 0, u_int8_t tei = 0);
    bool sendSetup(SignallingMessage* sigMsg);
    bool sendSuspendRej(const char* reason = 0, SignallingMessage* sigMsg = 0);
    bool sendSetupAck();
    // Errors on processing received messages
    // Missing mandatory IE
    // @param release True to send release complete and generate a release event
    SignallingEvent* errorNoIE(ISDNQ931Message* msg, ISDNQ931IE::Type type, bool release);
    SignallingEvent* errorWrongIE(ISDNQ931Message* msg, ISDNQ931IE::Type type, bool release);
    // Change call state
    void changeState(State newState);
    // Remove the call from controller's list
    void removeFromController();
    // Get the Q931 call controller
    inline ISDNQ931* q931();

    // Call data
    u_int32_t m_callRef;                 // Call reference
    u_int32_t m_callRefLen;              // Call reference length
    u_int8_t m_tei;                      // TEI used for the call
    SignallingCircuit* m_circuit;        // Circuit reserved for this call
    bool m_circuitChange;                // True if circuit changed
    bool m_channelIDSent;                // Incoming calls: ChannelID IE already sent
    bool m_rspBearerCaps;                // Incoming calls: Send BearerCaps IE in the first response
    bool m_inbandAvailable;              // Inband data is available
    bool m_net;                          // Flag indicating call is sent by a NT
    ISDNQ931IEData m_data;               // Data to process IEs
    ObjList m_inMsg;                     // Incoming message queue
    bool m_broadcast[127];               // TEIs that answered to PTMP SETUP
    // Timers
    SignallingTimer m_discTimer;         // T305: sending DISCONNECT
    SignallingTimer m_relTimer;          // T308: sending RELEASE
    SignallingTimer m_conTimer;          // T313: sending CONNECT
    SignallingTimer m_overlapSendTimer;  // T302 for overlapped sending
    SignallingTimer m_overlapRecvTimer;  // T304
    SignallingTimer m_retransSetupTimer; // T302 for setup retransmission (PTMP)
    // Termination
    bool m_terminate;                    // Terminate flag: send RELEASE
    bool m_destroy;                      // Destroy flag: call releaseComplete()
    bool m_destroyed;                    // Call destroyed flag
};

/**
 * Q.931 ISDN call monitor
 * @short A Q.931 ISDN call monitor
 */
class YSIG_API ISDNQ931CallMonitor : public ISDNQ931State, public SignallingCall
{
    friend class ISDNQ931Monitor;
public:
    /**
     * Destructor
     */
    virtual ~ISDNQ931CallMonitor();

    /**
     * Check if the initiator is from the network side of the data link
     * @return True if the initiator is from the network side of the data link, false if it is from the user side
     */
    inline bool netInit() const
	{ return m_netInit; }

    /**
     * Get an event from this call
     * This method is thread safe
     * @param when The current time
     * @return SignallingEvent pointer or 0 if no events
     */
    virtual SignallingEvent* getEvent(const Time& when);

    /**
     * Set termination flag
     * This method is thread safe
     * @param reason Terminate reason
     */
    void setTerminate(const char* reason);

    /**
     * Get a pointer to a data member or this call
     * @param name Object name
     * @return The requested pointer or 0 if not exists
     */
    virtual void* getObject(const String& name) const;

protected:
    /**
     * Constructor
     * @param controller The call controller
     * @param callRef The call reference
     * @param netInit True if the initiator is from the network side of the link
     */
    ISDNQ931CallMonitor(ISDNQ931Monitor* controller, u_int32_t callRef, bool netInit);

    /**
     * Clear all call data
     * Remove from controller's queue. Decrease the object's refence count
     * @param reason Optional release reason. If missing, the last reason is used
     * @return Pointer to an SignallingEvent of type Release
     */
    SignallingEvent* releaseComplete(const char* reason = 0);

private:
    // Get an event from one of the reserved circuits
    SignallingEvent* getCircuitEvent(const Time& when);
    // Process received setup message
    SignallingEvent* processMsgSetup(ISDNQ931Message* msg);
    // Process received responses to setup message (Proceeding, Alerting, Connect)
    SignallingEvent* processMsgResponse(ISDNQ931Message* msg);
    // Process termination messages (Disconnect, Release, Release Complete)
    SignallingEvent* processMsgTerminate(ISDNQ931Message* msg);
    // Process INFORMATION messages to get tones
    SignallingEvent* processMsgInfo(ISDNQ931Message* msg);
    // Reserve/release the circuits
    bool reserveCircuit();
    void releaseCircuit();
    // Connect the caller's or called's circuit
    bool connectCircuit(bool caller);
    // Change call state
    void changeState(State newState);
    // Remove the call from controller's list
    void removeFromController();
    // Get the Q931Monitor call controller
    inline ISDNQ931Monitor* q931();

    u_int32_t m_callRef;                 // Call reference
    SignallingCircuit* m_callerCircuit;  // Circuit reserved for caller
    SignallingCircuit* m_calledCircuit;  // Circuit reserved for called
    SignallingCircuit* m_eventCircuit;   // Last circuit that generated an event
    bool m_netInit;                      // The call initiator is from the network side of the link
    bool m_circuitChange;                // True if circuit changed
    ISDNQ931IEData m_data;               // Data to process IEs
    bool m_terminate;                    // Terminate flag
    String m_terminator;                 // The name of the entity that terminated the call
    ObjList m_inMsg;                     // Incoming messages queue
};

/**
 * This class holds Q.931 parser settings used to encode/decode Q.931 messages
 * @short Q.931 message parser data
 */
class YSIG_API ISDNQ931ParserData
{
public:
    /**
     * Constructor
     * @param params Parser settings
     * @param dbg The debug enabler used for output
     */
    ISDNQ931ParserData(const NamedList& params, DebugEnabler* dbg = 0);

    /**
     * Check the state of a flag
     * @param mask The flag to check
     * @return True if the given flag is set
     */
    inline bool flag(int mask) const
	{ return (0 != (m_flags & mask)); }

    DebugEnabler* m_dbg;                 // The debug enabler used for output
    u_int32_t m_maxMsgLen;               // Maximum length of outgoing messages (or message segments)
    int m_flags;                         // The current behaviour flags
    int m_flagsOrig;                     // The original behaviour flags
    u_int8_t m_maxDisplay;               // Max Display IE size
    bool m_allowSegment;                 // True if message segmentation is allowed
    u_int8_t m_maxSegments;              // Maximum allowed segments for outgoing messages
    bool m_extendedDebug;                // True to fill message/IE buffer
};

/**
 * Q.931 ISDN Layer 3 implementation on top of a Layer 2
 * @short ISDN Q.931 implementation on top of Q.921
 */
class YSIG_API ISDNQ931 : public SignallingCallControl, public SignallingDumpable, public ISDNLayer3
{
    YCLASS(ISDNQ931,ISDNLayer3)
    friend class ISDNQ931Call;
public:
    /**
     * Enumeration flags defining the behaviour of the ISDN call controller and
     *  any active calls managed by it
     */
    enum BehaviourFlags {
	// Append the progress indicator 'non-isdn-source' if present when
	// sending SETUP. If this flag is not set, the indicator will be
	// removed from the message
	SendNonIsdnSource = 0x00000001,
	// Ignore (don't send) the progress indicator 'non-isdn-destination'
	// if present when sending SETUP ACKNOWLEDGE or CONNECT
	IgnoreNonIsdnDest = 0x00000002,
	// Always set presentation='allowed' and screening='network-provided'
	// for Calling Party Number IE
	ForcePresNetProv = 0x00000004,
	// Translate '3.1khz-audio' transfer capability code 0x10 to/from 0x08
	Translate31kAudio = 0x00000008,
	// Send only transfer mode and rate when sending the Bearer Capability IE
	// with transfer capability 'udi' or 'rdi' (unrestricted/restricted
	// digital information)
	URDITransferCapsOnly = 0x00000010,
	// Don't send Layer 1 capabilities (data format) with the
	// Bearer Capability IE when in circuit switch mode
	NoLayer1Caps = 0x00000020,
	// Don't parse incoming IEs found after a temporary (non-locking) shift
	IgnoreNonLockedIE = 0x00000040,
	// Don't send the Display IE
	// This flag is internally set for EuroIsdnE1 type when the call
	// controller is the CPE side of the link
	NoDisplayIE = 0x00000080,
	// Don't append a charset byte 0xb1 before Display data
	NoDisplayCharset = 0x00000100,
	// Send a Sending Complete IE even if no overlap dialing
	ForceSendComplete = 0x00000200,
	// Don't change call state to Active instead of ConnectRequest after
	// sending CONNECT. This flag is internally set when the call
	// controller is the CPE side of the data link
	NoActiveOnConnect = 0x00000400,
	// Check the validity of the notification indicator when sending a NOTIFY message
	CheckNotifyInd = 0x00000800,
	// Request an outbound PRI channel to be exclusive
	ChannelExclusive = 0x00001000,
    };

    /**
     * Call controller switch type. Each value is a mask of behaviour flags
     */
    enum SwitchType {
	Unknown      = 0,
	// Standard Euro ISDN (CTR4, ETSI 300-102)
	EuroIsdnE1   = ForceSendComplete|CheckNotifyInd|NoDisplayCharset|URDITransferCapsOnly,
	// T1 Euro ISDN variant (ETSI 300-102)
	EuroIsdnT1   = ForceSendComplete|CheckNotifyInd,
	// National ISDN
	NationalIsdn = SendNonIsdnSource,
	// DMS 100
	Dms100       = ForcePresNetProv|IgnoreNonIsdnDest,
	// Lucent 5E
	Lucent5e     = IgnoreNonLockedIE,
	// AT&T 4ESS
	Att4ess      = ForcePresNetProv|IgnoreNonLockedIE|Translate31kAudio|NoLayer1Caps,
	// QSIG Switch
	QSIG         = NoActiveOnConnect|NoDisplayIE|NoDisplayCharset
    };

    /**
     * Constructor
     * Initialize this object and the component
     * @param params Layer's parameters and parser settings
     * @param name Name of this component
     */
    ISDNQ931(const NamedList& params, const char* name = 0);

    /**
     * Destructor
     * Destroy all calls
     */
    virtual ~ISDNQ931();

    /**
     * Configure and initialize Q.931 and its layer 2
     * @param config Optional configuration parameters override
     * @return True if Q.931 and the layer 2 were initialized properly
     */
    virtual bool initialize(const NamedList* config);

    /**
     * Get the controller's status as text
     * @return Controller status name
     */
    virtual const char* statusName() const;

    /**
     * Get the layer 2 attached to this object
     * @return Pointer to the layer 2 attached to this object or 0 if none
     */
    inline const ISDNLayer2* layer2() const
	{ return m_q921; }

    /**
     * Check if this call controller supports primary or basic rate transfer
     * @return True for primary rate. False for basic rate
     */
    inline bool primaryRate() const
	{ return m_primaryRate; }

    /**
     * Chech if this call controller is at the NET or CPE side of the link
     * @return True if we are NET, false if we are CPE
     */
    inline bool network() const
	{ return m_q921 ? m_q921->network() : m_networkHint; }

    /**
     * Check if this call controller supports circuit switch or packet mode transfer
     * @return True for circuit switch. False for packet mode
     */
    inline bool transferModeCircuit() const
	{ return m_transferModeCircuit; }

    /**
     * Get the parser settings of this call control
     * @return The parser settings
     */
    inline ISDNQ931ParserData& parserData()
	{ return m_parserData; }

    /**
     * Get the default numbering plan for outgoing calls
     * @return The default numbering plan for outgoing calls
     */
    inline const String& numPlan() const
	{ return m_numPlan; }

    /**
     * Get the default number type for outgoing calls
     * @return The default number type for outgoing calls
     */
    inline const String& numType() const
	{ return m_numType; }

    /**
     * Get the default number presentation for outgoing calls
     * @return The default number presentation for outgoing calls
     */
    inline const String& numPresentation() const
	{ return m_numPresentation; }

    /**
     * Get the default number screening for outgoing calls
     * @return The default number screening for outgoing calls
     */
    inline const String& numScreening() const
	{ return m_numScreening; }

    /**
     * Get the default data format for outgoing calls
     * @return The default data format for outgoing calls
     */
    inline const String& format() const
	{ return m_format; }

    /**
     * Send a message
     * @param msg The message to be sent
     * @param tei TEI value to use at Layer 2
     * @param reason Optional string to write the failure reason
     * @return False if the message is invalid, Layer 2 is missing or refused the data
     */
    bool sendMessage(ISDNQ931Message* msg, u_int8_t tei, String* reason = 0);

    /**
     * Notification of Layer 2 up state
     * @param tei TEI received by the Layer 2
     * @param confirm True if this is a confirmation of a previous request.
     *  False if it is an indication of state change on remote request
     * @param timeout True if the reason is a timeout.
     * @param layer2 Pointer to the notifier
     */
    virtual void multipleFrameEstablished(u_int8_t tei, bool confirm, bool timeout, ISDNLayer2* layer2);

    /**
     * Notification of Layer 2 down state
     * @param tei TEI received by the Layer 2
     * @param confirm True if this is a confirmation of a previous request.
     *  False if it is an indication of state change on remote request
     * @param timeout True if the reason is a timeout.
     * @param layer2 Pointer to the notifier
     */
    virtual void multipleFrameReleased(u_int8_t tei, bool confirm, bool timeout, ISDNLayer2* layer2);

    /**
     * Receive data from Layer 2
     * @param data Received data
     * @param tei TEI received by the Layer 2
     * @param layer2 Pointer to the sender Layer 2
     */
    virtual void receiveData(const DataBlock& data, u_int8_t tei, ISDNLayer2* layer2);

    /**
     * Attach an ISDN Q.921 transport
     * This method is thread safe
     * @param q921 Pointer to the Q.921 transport to attach
     * @return Pointer to the detached Layer 2 or NULL
     */
    virtual ISDNLayer2* attach(ISDNLayer2* q921);

    /**
     * Create an outgoing call. Send a NewCall event with the given msg parameter
     * @param msg Call parameters
     * @param reason Failure reason if any
     * @return Referenced SignallingCall pointer on success or 0 on failure
     */
    SignallingCall* call(SignallingMessage* msg, String& reason);

    /**
     * Restart one or more the circuits
     * @param circuits Comma separated list of circuits to be restarted
     * @return True if the procedure was successfully started or enqueued
     */
    bool restart(const char* circuits);

    /**
     * Send a STATUS message for a given call
     * @param call The call requesting the operation
     * @param tei The TEI to send with the STATUS message
     * @param cause Value for Cause IE
     * @param display Optional value for Display IE
     * @param diagnostic Optional value for cause diagnostic value
     * @return The result of the operation (true if successfully sent)
     */
    inline bool sendStatus(ISDNQ931Call* call, const char* cause, u_int8_t tei = 0,
	const char* display = 0, const char* diagnostic = 0)
    {
	return call && sendStatus(cause,call->callRefLen(),call->callRef(),tei,
	    call->outgoing(),call->state(),display,diagnostic);
    }

    /**
     * Send a RELEASE or RELEASE COMPLETE message for a given call
     * @param call The call requesting the operation
     * @param release True to send RELEASE, false to send RELEASE COMPLETE
     * @param cause Value for Cause IE
     * @param tei TEI to which the release is sent to
     * @param diag Optional hexified string for cause dignostic
     * @param display Optional value for Display IE
     * @param signal Optional value for Signal IE
     * @return The result of the operation (true if successfully sent)
     */
    inline bool sendRelease(ISDNQ931Call* call, bool release, const char* cause, u_int8_t tei = 0,
	const char* diag = 0, const char* display = 0, const char* signal = 0)
    {
	return call && sendRelease(release,call->callRefLen(),call->callRef(),tei,
	    call->outgoing(),cause,diag,display,signal);
    }

    /**
     * Set terminate to all calls
     * This method is thread safe
     * @param reason Cleanup reason
     */
    virtual void cleanup(const char* reason = "offline");

    /**
     * Set the timeout interval for a given timer if implemented
     * If the timer is not implemented the interval is set to 0
     * @param timer The destination timer
     * @param id The timer number as defined in Q.931
     */
    void setInterval(SignallingTimer& timer, int id);

    /**
     * Manage timeout for the call setup message
     */
    void manageTimeout();

    /**
     * Set debug data of this call controller
     * @param printMsg Enable/disable message printing on output
     * @param extendedDebug Enable/disable hex data dump if print messages is enabled
     */
    inline void setDebug(bool printMsg, bool extendedDebug)
	{ m_parserData.m_extendedDebug = m_extendedDebug =
	    ((m_printMsg = printMsg) && extendedDebug); }

    /**
     * The list of behaviour flag names
     */
    static const TokenDict s_flags[];

     /**
     * The list of switch type names
     */
    static const TokenDict s_swType[];

protected:
    /**
     * Detach links. Disposes memory
     */
    virtual void destroyed()
    {
	TelEngine::destruct(attach(0));
	TelEngine::destruct(SignallingCallControl::attach(0));
	ISDNLayer3::destroyed();
    }

    /**
     * Method called periodically to check timeouts
     * This method is thread safe
     * @param when Time to use as computing base for events and timeouts
     */
    virtual void timerTick(const Time& when);

    /**
     * Find a call given its call reference and direction
     * @param callRef The call reference to find
     * @param outgoing True to find an outgoing call, false to find an incoming one
     * @param tei TEI of the layer associated to the call to find
     * @return A referenced pointer to a call or 0
     */
    ISDNQ931Call* findCall(u_int32_t callRef, bool outgoing, u_int8_t tei = 0);

    /**
     * Find a call given a circuit number
     * @param circuit The circuit number to find
     * @return A referenced pointer to a call or 0
     */
    ISDNQ931Call* findCall(unsigned int circuit);

    /**
     * Terminate calls. If list is 0 terminate all calls
     * @param list Optional list of circuits (strings) to be released
     * @param reason The reason to be passed to each terminated call
     */
    void terminateCalls(ObjList* list, const char* reason);

    /**
     * Check if this call control can accept new calls
     * @param outgoing Call direction (true for outgoing)
     * @param reason String to be filled with the reason if not accepted
     * @return True if the call request is accepted
     */
    bool acceptNewCall(bool outgoing, String& reason);

    /**
     * Process received data. Process received message segments if any
     * @param data The received data
     * @return ISDNQ931Message pointer or 0
     */
    ISDNQ931Message* getMsg(const DataBlock& data);

    /**
     * End waiting for message segments
     * If reason is 0 parse already received data for the segmented message
     * This method is thread safe
     * @param reason Debug info reason. If non 0 drop the received segment(s)
     * @return ISDNQ931Message pointer or 0
     */
    ISDNQ931Message* endReceiveSegment(const char* reason = 0);

    /**
     * Process messages with global call reference or should have one
     * @param msg The received message
     * @param tei The TEI received with the message
     */
    void processGlobalMsg(ISDNQ931Message* msg, u_int8_t tei = 0);

    /**
     * Process a restart request
     * @param msg The received message
     * @param tei The TEI received with the message
     */
    void processMsgRestart(ISDNQ931Message* msg, u_int8_t tei = 0);

    /**
     * Process messages with invalid call reference
     * @param msg The received message
     * @param tei The TEI received with the message
     */
    void processInvalidMsg(ISDNQ931Message* msg, u_int8_t tei = 0);

    /**
     * Try to reserve a circuit for restarting if none. Send a restart request on it's behalf
     * Start counting the restart interval if no circuit reserved
     * This method is thread safe
     * @param time The time of the transmission
     * @param retrans Retransmission flag (true if a previous request timed out)
     */
    void sendRestart(u_int64_t time = Time::msecNow(), bool retrans = false);

    /**
     * End restart procedure on timeout or restart acknoledge
     * This method is thread safe
     * @param restart True to try to send restart for the next circuit
     * @param time The time of the transmission
     * @param timeout True if a restart request timed out
     */
    void endRestart(bool restart, u_int64_t time, bool timeout = false);

    /**
     * Send a STATUS message
     * @param cause Value for Cause IE
     * @param callRefLen The call reference length parameter.
     * @param callRef The call reference
     * @param tei The TEI to send with the STATUS message
     * @param initiator True if this is from the call initiator
     * @param state The state for CallState IE
     * @param display Optional value for Display IE
     * @param diagnostic Optional value for cause diagnostic value
     * @return The result of the operation (true if successfully sent)
     */
    bool sendStatus(const char* cause, u_int8_t callRefLen, u_int32_t callRef = 0,
	u_int8_t tei = 0, bool initiator = false, ISDNQ931Call::State state = ISDNQ931Call::Null,
	const char* display = 0, const char* diagnostic = 0);

    /**
     * Send a RELEASE or RELEASE COMPLETE message
     * @param release True to send RELEASE, false to send RELEASE COMPLETE
     * @param callRefLen The call reference length parameter
     * @param callRef The call reference
     * @param tei The TEI of the Layer 2 associated with the call
     * @param initiator The call initiator flag
     * @param cause Value for Cause IE
     * @param diag Optional hexified string for cause dignostic
     * @param display Optional value for Display IE
     * @param signal Optional value for Signal IE
     * @return The result of the operation (true if successfully sent)
     */
    bool sendRelease(bool release, u_int8_t callRefLen, u_int32_t callRef, u_int8_t tei,
	bool initiator, const char* cause = 0, const char* diag = 0,
	const char* display = 0, const char* signal = 0);

private:
    virtual bool control(NamedList& params)
	{ return SignallingDumpable::control(params,this); }
    bool q921Up() const;                 // Check if layer 2 may be up
    ISDNLayer2* m_q921;                  // The attached layer 2
    bool m_q921Up;                       // Layer 2 state
    // Protocol data
    bool m_networkHint;                  // NET / CPE hint, layer 2 is authoritative
    bool m_primaryRate;                  // Primary/base rate support
    bool m_transferModeCircuit;          // Circuit switch/packet mode transfer
    u_int32_t m_callRef;                 // Current available call reference for outgoing calls
    u_int8_t m_callRefLen;               // Call reference length
    u_int32_t m_callRefMask;             // Call reference mask
    ISDNQ931ParserData m_parserData;     // Parser settings
    ISDNQ931IEData m_data;               // Process IEs
    // Timers & counters
    SignallingTimer m_l2DownTimer;       // T309: Layer 2 is down timeout
    SignallingTimer m_recvSgmTimer;      // T314: Receive segment timeout
    SignallingTimer m_syncCicTimer;      // T316: Restart individual circuit timeout
    SignallingCounter m_syncCicCounter;  // RESTART retransmission counter
    SignallingTimer m_callDiscTimer;     // Q931 call value (see ISDQ931Call)
    SignallingTimer m_callRelTimer;      // Q931 call value (see ISDQ931Call)
    SignallingTimer m_callConTimer;      // Q931 call value (see ISDQ931Call)
    // Default values
    String m_numPlan;                    // Numbering plan
    String m_numType;                    // Number type
    String m_numPresentation;            // Number presentation
    String m_numScreening;               // Number screening
    String m_format;                     // Data format
    String m_cpeNumber;                  // The number of the BRI CPE
    // Restart data
    SignallingCircuit* m_restartCic;     // Currently restarting circuit
    unsigned int m_lastRestart;          // Last restarted circuit's code
    SignallingTimer m_syncGroupTimer;    // Restarting circuit group interval
    // Message segmentation data
    DataBlock m_segmentData;             // Message segments buffer
    ISDNQ931Message* m_segmented;        // Segmented message
    u_int8_t m_remaining;                // Remaining segments
    // Debug
    bool m_printMsg;                     // True to print messages to output
    bool m_extendedDebug;                // Extended debug flag
    // Flags used to print error messages
    bool m_flagQ921Down;                 // Layer 2 is down period timed out
    bool m_flagQ921Invalid;              // Refusing to send message when Layer 2 is missing or down
};

/**
 * Q.931 ISDN Layer 3 implementation on top of a Layer 2. Manage Q.931 monitors
 * @short ISDN Q.931 implementation on top of Q.921 of call controller monitor
 */
class YSIG_API ISDNQ931Monitor : public SignallingCallControl, public ISDNLayer3
{
    YCLASS(ISDNQ931Monitor,ISDNLayer3)
    friend class ISDNQ931CallMonitor;
public:
    /**
     * Constructor
     * Initialize this object and the component
     * @param params Layer's parameters and parser settings
     * @param name Name of this component
     */
    ISDNQ931Monitor(const NamedList& params, const char* name = 0);

    /**
     * Destructor
     * Destroy all calls
     */
    virtual ~ISDNQ931Monitor();

    /**
     * Configure and initialize the Q.931 monitor and its interfaces
     * @param config Optional configuration parameters override
     * @return True if Q.931 monitor and both interfaces were initialized properly
     */
    virtual bool initialize(const NamedList* config);

    /**
     * Get the controller's status as text
     * @return Controller status name
     */
    virtual const char* statusName() const;

    /**
     * Notification from layer 2 of data link set/release command or response
     * @param tei The TEI of the notification
     * @param cmd True if received a command, false if received a response
     * @param value The value of the notification
     *	If 'cmd' is true (command), the value is true if a request to establish data link was received
     *   or false if received a request to release data link
     *	If 'cmd' is false (response), the value is the response
     * @param layer2 Pointer to the notifier
     */
    virtual void dataLinkState(u_int8_t tei, bool cmd, bool value, ISDNLayer2* layer2);

    /**
     * Notification from layer 2 of data link idle timeout
     * @param layer2 Pointer to the notifier
     */
    virtual void idleTimeout(ISDNLayer2* layer2);

    /**
     * Implements Q.921 DL-DATA and DL-UNIT DATA indication primitives
     * @param data Received data
     * @param tei The TEI of the Layer 2
     * @param layer2 Pointer to the sender
     */
    virtual void receiveData(const DataBlock& data, u_int8_t tei, ISDNLayer2* layer2);

    /**
     * Attach ISDN Q.921 pasive transport that monitors one side of the link
     * This method is thread safe
     * @param q921 Pointer to the monitor to attach
     * @param net True if this is the network side of the data link, false for user (CPE) side
     * @return Pointer to detached monitor or NULL
     */
    virtual ISDNQ921Passive* attach(ISDNQ921Passive* q921, bool net);

    /**
     * Attach a circuit group to this call controller
     * This method is thread safe
     * @param circuits Pointer to the SignallingCircuitGroup to attach
     * @param net True if this group belongs to the network side of the data link, false for user (CPE) side
     * @return Pointer to the old group that was detached, NULL if none or no change
     */
    virtual SignallingCircuitGroup* attach(SignallingCircuitGroup* circuits, bool net);

    /**
     * Get a pointer to the NET or CPE circuit group
     * @param net True to get the network side circuits, false for user (CPE) side
     * @return Pointer to the circuit group requested, NULL if none attached
     */
    inline ISDNQ921Passive* circuits(bool net) const
	{ return net ? m_q921Net : m_q921Cpe; }

    /**
     * Set debug data of this call controller
     * @param printMsg Enable/disable message printing on output
     * @param extendedDebug Enable/disable hex data dump if print messages is enabled
     */
    inline void setDebug(bool printMsg, bool extendedDebug)
	{ m_parserData.m_extendedDebug = m_extendedDebug =
	    ((m_printMsg = printMsg) && extendedDebug); }

    /**
     * Terminate all monitors
     * This method is thread safe
     * @param reason Cleanup reason
     */
    virtual void cleanup(const char* reason = "offline")
	{ terminateMonitor(0,reason); }

    /**
     * Terminate all monitors or only one
     * This method is thread safe
     * @param mon The monitor to terminate, 0 to terminate all
     * @param reason The termination reason
     */
    void terminateMonitor(ISDNQ931CallMonitor* mon, const char* reason);

protected:
    /**
     * Detach links. Disposes memory
     */
    virtual void destroyed()
    {
	TelEngine::destruct(SignallingCallControl::attach(0));
	TelEngine::destruct(attach((ISDNQ921Passive*)0,true));
	TelEngine::destruct(attach((ISDNQ921Passive*)0,false));
	attach((SignallingCircuitGroup*)0,true);
	attach((SignallingCircuitGroup*)0,false);
	ISDNLayer3::destroyed();
    }

    /**
     * Method called periodically to check timeouts
     * This method is thread safe
     * @param when Time to use as computing base for events and timeouts
     */
    virtual void timerTick(const Time& when);

    /**
     * Reserve the same circuit code from both circuit groups
     * This is an atomic operation: if one circuit fails to be reserved, both of them will fail
     * Release both circuits on failure
     * This method is thread safe
     * @param code The circuit code to reserve
     * @param netInit True if the caller is from the network side of the link, false if it's from CPE side
     * @param caller The destination caller circuit
     * @param called The destination called circuit
     * @return True on success
     */
    bool reserveCircuit(unsigned int code, bool netInit,
	SignallingCircuit** caller, SignallingCircuit** called);

    /**
     * Release a circuit
     * This method is thread safe
     * @param circuit The circuit to release
     * @return True on success
     */
    bool releaseCircuit(SignallingCircuit* circuit);

    /**
     * Process a restart or restart acknoledge message
     * Terminate the monitor having the circuit given in restart message
     * @param msg The received message
     */
    void processMsgRestart(ISDNQ931Message* msg);

private:
    // Find a call monitor by call reference or reserved circuit
    // @return Referenced call monitor pointer or 0 if not found
    ISDNQ931CallMonitor* findMonitor(unsigned int value, bool byCallRef);
    // Drop some messages. Return true if the message should be dropped
    bool dropMessage(const ISDNQ931Message* msg);

    ISDNQ921Passive* m_q921Net;          // Net side of the link
    ISDNQ921Passive* m_q921Cpe;          // CPE side of the link
    SignallingCircuitGroup* m_cicNet;    // Circuit group for the net side of the link
    SignallingCircuitGroup* m_cicCpe;    // Circuit group for the cpe side of the link
    ISDNQ931ParserData m_parserData;     // Parser settings
    ISDNQ931IEData m_data;               // Process IEs
    // Debug
    bool m_printMsg;                     // True to print messages to output
    bool m_extendedDebug;                // Extended debug flag
};

}

#endif /* __YATESIG_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
