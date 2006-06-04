/**
 * yatess7.h
 * Yet Another SS7 Stack
 * This file is part of the YATE Project http://YATE.null.ro
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

#ifndef __YATESS7_H
#define __YATESS7_H

#include <yateclass.h>

#ifdef _WINDOWS

#ifdef LIBYSS7_EXPORTS
#define YSS7_API __declspec(dllexport)
#else
#ifndef LIBYSS7_STATIC
#define YSS7_API __declspec(dllimport)
#endif
#endif

#endif /* _WINDOWS */

#ifndef YSS7_API
#define YSS7_API
#endif

/** 
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

class SignallingEngine;
class SignallingThreadPrivate;
class SignallingReceiver;
class SCCPUser;
class SS7Layer2;
class SS7Layer3;
class SS7Router;
class SS7TCAP;
class ISDNLayer3;

// Macro to create a factory that builds a component by class name
#define YSIGFACTORY(clas,iface) \
class clas ## Factory : public SignallingFactory \
{ \
protected: \
virtual void* create(const String& type, const NamedList& name) \
    { return (type == #clas) ? static_cast<iface*>(new clas) : 0; } \
}; \
static clas ## Factory s_ ## clas ## Factory

// Macro to create a factory that calls a component's static create method
#define YSIGFACTORY2(clas,iface) \
class clas ## Factory : public SignallingFactory \
{ \
protected: \
virtual void* create(const String& type, const NamedList& name) \
    { return clas::create(type,name); } \
}; \
static clas ## Factory s_ ## clas ## Factory

/**
 * A factory that constructs various elements by name
 * @short A signalling component factory
 */
class YSS7_API SignallingFactory : public GenObject
{
public:
    /**
     * Constructor, adds the factory to the global list
     */
    SignallingFactory();

    /**
     * Destructor, removes the factory from list
     */
    virtual ~SignallingFactory();

    /**
     * Builds a component given its name and arbitrary parameters
     * @param type The name of the interface that should be returned
     * @param name Name of the requested component and additional parameters
     * @return Pointer to the requested interface of the created component
     */
    static void* build(const String& type, const NamedList* name = 0);

protected:
    /**
     * Creates a component given its name and arbitrary parameters
     * @param type The name of the interface that should be returned
     * @param name Name of the requested component and additional parameters
     * @return Pointer to the requested interface of the created component
     */
    virtual void* create(const String& type, const NamedList& name) = 0;
};

/**
 * Interface to an abstract signalling component that is managed by an engine.
 * The engine will periodically poll each component to keep them alive.
 * @short Abstract signalling component that can be managed by the engine
 */
class YSS7_API SignallingComponent : public GenObject
{
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
    virtual const String& toString();

    /**
     * Get the @ref TelEngine::SignallingEngine that manages this component
     * @return Pointer to engine or NULL if not managed by an engine
     */
    inline SignallingEngine* engine() const
	{ return m_engine; }

protected:
    /**
     * Constructor with a default empty component name
     * @param name Name of this component
     */
    inline SignallingComponent(const char* name = 0)
	: m_engine(0), m_name(name)
	{ }

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
    inline void setName(const char* name)
	{ m_name = name; }

private:
    SignallingEngine* m_engine;
    String m_name;
};

/**
 * The engine is the center of all SS7 or ISDN applications.
 * It is used as a base to build the protocol stack from components.
 * @short Main signalling component holder
 */
class YSS7_API SignallingEngine : public DebugEnabler, public Mutex
{
    friend class SignallingComponent;
    friend class SignallingThreadPrivate;
public:
    /**
     * Constructor of an empty engine
     */
    SignallingEngine();

    /**
     * Destructor, removes all components
     */
    virtual ~SignallingEngine();

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
     * Retrive a component by name, lock the list while searching for it
     * @param name Name of the component to find
     * @return Pointer to component found or NULL
     */
    SignallingComponent* find(const String& name);

    /**
     * Starts the worker thread that keeps components alive
     * @param name Static name of the thread
     * @param prio Thread's priority
     * @param usec How long to sleep between iterations, in microseconds
     * @return True if (already) started, false if an error occured
     */
    bool start(const char* name = "Signalling", Thread::Priority prio = Thread::Normal, unsigned long usec = 1000);

    /**
     * Stops and destroys the worker thread if running
     */
    void stop();

    /**
     * Return a pointer to the worker thread
     * @return Pointer to running worker thread or NULL
     */
    Thread* thread() const;

protected:
    /**
     * Method called periodically by the @ref Thread to keep everything alive
     * @param when Time to use as computing base for events and timeouts
     */
    virtual void timerTick(const Time& when);

    /**
     * The list of components managed by this engine
     */
    ObjList m_components;

private:
    SignallingThreadPrivate* m_thread;
    bool m_listChanged;
};

/**
 * Interface of protocol independent signalling element
 * @short Abstract signalling information element
 */
class YSS7_API SignallingElement : public NamedString
{
};

/**
 * Interface of protocol independent signalling message
 * @short Abstract signalling message
 */
class YSS7_API SignallingMessage : public RefObject
{
public:
    /**
     * Append an information element to this message
     * @param element Information element to add
     * @return True if the IE was added or replaced, false if it was invalid
     */
    virtual bool append(const SignallingElement& element) = 0;

    /**
     * Appending operator for signalling elements
     */
    inline SignallingMessage& operator+=(const SignallingElement& element)
	{ append(element); return *this; }

    /**
     * Stream style appending operator for signalling elements
     */
    inline SignallingMessage& operator<<(const SignallingElement& element)
	{ append(element); return *this; }
};

/**
 * Interface of protocol independent signalling for phone calls
 * @short Abstract phone call signalling
 */
class YSS7_API SignallingCallControl
{
};

/**
 * Interface of protocol independent phone call
 * @short Abstract single phone call
 */
class YSS7_API SignallingCall : public RefObject
{
};

/**
 * An object holding a signalling event and related references
 * @short A single signalling related event
 */
class YSS7_API SignallingEvent
{
    
protected:
    SignallingMessage* m_message;
    SignallingCall* m_call;
};

/**
 * An interface to an abstraction of a Layer 1 (hardware HDLC) interface
 * @short Abstract digital signalling interface (hardware access)
 */
class YSS7_API SignallingInterface : virtual public SignallingComponent
{
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
     * Destructor, stops and detaches the interface
     */
    virtual ~SignallingInterface();

    /**
     * Attach a receiver to the interface
     * @param iface Pointer to receiver to attach
     */
    virtual void attach(SignallingReceiver* receiver);

    /**
     * Retrive the signalling receiver attached to this interface
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
    SignallingReceiver* m_receiver;
};

/**
 * An interface to an abstraction of a Layer 2 packet data receiver
 * @short Abstract Layer 2 packet data receiver
 */
class YSS7_API SignallingReceiver : virtual public SignallingComponent
{
    friend class SignallingInterface;
public:

    /**
     * Destructor, stops the interface and detaches from it
     */
    virtual ~SignallingReceiver();

    /**
     * Attach a hardware interface to the data link
     * @param iface Pointer to interface to attach
     */
    virtual void attach(SignallingInterface* iface);

    /**
     * Retrive the interface used by this receiver
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
    inline bool control(SignallingInterface::Operation oper, NamedList* params = 0)
	{ return m_interface && m_interface->control(oper,params); }

protected:
    /**
     * Send a packet to the attached interface for transmission
     * @param packet Packet data to send
     * @param repeat Continuously send a copy of the packet while no other
     *  data is available for transmission
     * @param type Type of the packet to send
     * @return True if the interface accepted the packet
     */
    inline bool transmitPacket(const DataBlock& packet, bool repeat, SignallingInterface::PacketType type = SignallingInterface::Unknown)
	{ return m_interface && m_interface->transmitPacket(packet,repeat,type); }

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
    SignallingInterface* m_interface;
};

/**
 * A raw data block with a little more understanding about MSU format
 * @short A block of data that holds a Message Signal Unit
 */
class YSS7_API SS7MSU : public DataBlock
{
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
     * Retrive the Service Information Octet
     * @return Value of the SIO or -1 if the MSU is empty
     */
    inline int getSIO() const
	{ return null() ? -1 : *(const unsigned char*)data(); }

    /**
     * Retrive the Service Information Field
     * @return Value of the SIF or -1 if the MSU is empty
     */
    inline int getSIF() const
	{ return null() ? -1 : 0x0f & *(const unsigned char*)data(); }

    /**
     * Retrive the Subservice Field (SSF)
     * @return Value of the subservice or -1 if the MSU is empty
     */
    inline int getSSF() const
	{ return null() ? -1 : 0xf0 & *(const unsigned char*)data(); }

    /**
     * Retrive the Priority Field
     * @return Value of the priority or -1 if the MSU is empty
     */
    inline int getPrio() const
	{ return null() ? -1 : 0x30 & *(const unsigned char*)data(); }

    /**
     * Retrive the Network Indicator (NI)
     * @return Value of the subservice or -1 if the MSU is empty
     */
    inline int getNI() const
	{ return null() ? -1 : 0xc0 & *(const unsigned char*)data(); }

    /**
     * Retrive the name of the Service as decoded from the SIF
     * @return Name of the service, NULL if unknown or invalid MSU
     */
    const char* getServiceName() const;

    /**
     * Retrive the name of the Priority as decoded from the SIF
     * @return Name of the priority, NULL if unknown or invalid MSU
     */
    const char* getPriorityName() const;

    /**
     * Retrive the name of the Network Indicator as decoded from the SIF
     * @return Name of the network indicator, NULL if unknown or invalid MSU
     */
    const char* getIndicatorName() const;
};

/**
 * An universal SS7 Layer 3 routing Code Point
 * @short SS7 Code Point
 */
class YSS7_API SS7CodePoint
{
public:
    /**
     * Different incompatible types of codepoints
     */
    enum Type {
	Other = 0,
	ITU   = 1,
	ANSI  = 2,
	China = 3,
	Japan = 4,
    };

    /**
     * Constructor from components
     * @param network ANSI Network Identifier / ITU-T Zone Identification
     * @param cluster ANSI Network Cluster / ITU-T Area/Network Identification
     * @param member ANSI Cluster Member / ITU-T Signalling Point Identification
     */
    inline SS7CodePoint(unsigned char network = 0, unsigned char cluster = 0, unsigned char member = 0)
	: m_network(network), m_cluster(cluster), m_member(member)
	{ }

    /**
     * Constructor from unpacked format
     * @param type Type of the unpacking desired
     * @param packed Packed format of the codepoint
     */
    inline SS7CodePoint(Type type, unsigned int packed)
	: m_network(0), m_cluster(0), m_member(0)
	{ unpack(type,packed); }

    /**
     * Copy constructor
     * @param original Code point to be copied
     */
    inline SS7CodePoint(const SS7CodePoint& original)
	: m_network(original.network()), m_cluster(original.cluster()), m_member(original.member())
	{ }

    /**
     * Destructor
     */
    inline ~SS7CodePoint()
	{ }

    /**
     * Retrive the Network / Zone component of the Code Point
     * @return ANSI Network Identifier / ITU-T Zone Identification
     */
    inline unsigned char network() const
	{ return m_network; }

    /**
     * Retrive the Cluster / Area component of the Code Point
     * @return ANSI Network Cluster / ITU-T Area/Network Identification
     */
    inline unsigned char cluster() const
	{ return m_cluster; }

    /**
     * Retrive the Cluster / Point component of the Code Point
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
     * Assignment operator
     * @param original Code point to be copied
     */
    inline SS7CodePoint& operator=(const SS7CodePoint& original)
	{ assign(original.network(),original.cluster(),original.member()); return *this; }

    /**
     * Check if the codepoint is compatible with a packing type
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
     * Unpack an integer number into a codepoint
     * @param type Type of the unpacking desired
     * @param packed Packed format of the codepoint
     * @return True if the unpacking succeeded and the codepoint was updated
     */
    bool unpack(Type type, unsigned int packed);

    /**
     * Get the size (in bits) of a packed code point according to its type
     * @param type Type of the packing
     * @return Number of bits required to represent the code point, zero if unknown
     */
    static unsigned char size(Type type);

private:
    unsigned char m_network;
    unsigned char m_cluster;
    unsigned char m_member;
};

/**
 * Operator to write a codepoint to a string
 * @param str String to append to
 * @param cp Codepoint to append to the string
 */
String& operator<<(String& str, const SS7CodePoint& cp);

/**
 * A SS7 Layer 3 routing label, both ANSI and ITU capable
 * @short SS7 Routing Label
 */
class YSS7_API SS7Label
{
public:
    /**
     * Constructor of an empty, invalid label
     */
    SS7Label();

    /**
     * Constructor from type and received MSU
     * @param type Type of codepoint to use to decode the MSU
     * @param msu A received MSU to be parsed
     */
    SS7Label(SS7CodePoint::Type type, const SS7MSU& msu);

    /**
     * Assignment from type and received MSU
     * @param type Type of codepoint to use to decode the MSU
     * @param msu A received MSU to be parsed
     * @return True if the assignment succeeded
     */
    bool assign(SS7CodePoint::Type type, const SS7MSU& msu);

    /**
     * Check if the label is compatible with another packing type
     * @return True if the DLC, SLC and SLS fit in the new packing format
     */
    bool compatible(SS7CodePoint::Type type) const;

    /**
     * Get the type (SS7 dialect) of the routing label
     * @return Dialect of the routing label as enumeration
     */
    inline SS7CodePoint::Type type() const
	{ return m_type; }

    /**
     * Get the Destination Code Point inside the label
     * @return Reference of the destination code point
     */
    inline const SS7CodePoint& dpc() const
	{ return m_dpc; }

    /**
     * Get the Source Code Point inside the label
     * @return Reference of the source code point
     */
    inline const SS7CodePoint& spc() const
	{ return m_spc; }

    /**
     * Get the Signalling Link Selection inside the label
     * @return Value of the SLS field
     */
    inline unsigned char sls() const
	{ return m_sls; }

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
    static unsigned int length(SS7CodePoint::Type type);

    /**
     * Get the size (in bits) of a packed routing label according to its type
     * @param type Type of the packing
     * @return Number of bits required to represent the label, zero if unknown
     */
    static unsigned char size(SS7CodePoint::Type type);

private:
    SS7CodePoint::Type m_type;
    SS7CodePoint m_dpc;
    SS7CodePoint m_spc;
    unsigned char m_sls;
};

/**
 * Operator to write a routing label to a string
 * @param str String to append to
 * @param cp Label to append to the string
 */
String& operator<<(String& str, const SS7Label& label);

/**
 * An interface to a Signalling Transport component
 * @short Abstract SIGTRAN component
 */
class YSS7_API SIGTRAN
{
public:
    /**
     * Constructs an uninitialized signalling transport
     */
    SIGTRAN();

    /**
     * Destructor, closes connection and any socket
     */
    ~SIGTRAN();
};

/**
 * An interface to a SS7 Application Signalling Part user
 * @short Abstract SS7 ASP user interface
 */
class YSS7_API ASPUser
{
};

/**
 * An interface to a SS7 Signalling Connection Control Part
 * @short Abstract SS7 SCCP interface
 */
class YSS7_API SCCP
{
public:
    /**
     * Destructor
     */
    virtual ~SCCP();

    /**
     * Attach an user to this SS7 SCCP
     * @param sccp Pointer to the SCCP user
     */
    virtual void attach(SCCPUser* user);

protected:
    ObjList m_users;
};

/**
 * An interface to a SS7 Signalling Connection Control Part user
 * @short Abstract SS7 SCCP user interface
 */
class YSS7_API SCCPUser
{
public:
    /**
     * Destructor, detaches from the SCCP implementation
     */
    virtual ~SCCPUser();

    /**
     * Attach as user to a SCCP
     * @param sccp Pointer to the SCCP to use
     */
    virtual void attach(SCCP* sccp);

    /**
     * Retrive the SCCP to which this component is attached
     * @return Pointer to the attached SCCP or NULL
     */
    inline SCCP* sccp() const
	{ return m_sccp; }

private:
    SCCP* m_sccp;
};

/**
 * An interface to a SS7 Transactional Capabilities Application Part user
 * @short Abstract SS7 TCAP user interface
 */
class YSS7_API TCAPUser
{
public:
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
     * Retrive the TCAP to which this user is attached
     * @return Pointer to a SS7 TCAP interface or NULL
     */
    inline SS7TCAP* tcap() const
	{ return m_tcap; }

private:
    SS7TCAP* m_tcap;
};

/**
 * An user of a Layer 2 (data link) SS7 message transfer part
 * @short Abstract user of SS7 layer 2 (data link) message transfer part
 */
class YSS7_API SS7L2User : virtual public SignallingComponent
{
    friend class SS7Layer2;
public:
    /**
     * Attach a SS7 Layer 2 (data link) to the user component
     * @param link Pointer to data link to attach
     */
    virtual void attach(SS7Layer2* link) = 0;

protected:
    /**
     * Process a MSU received from the Layer 2 component
     * @param msu Message data, starting with Service Indicator Octet
     * @param link Data link that delivered the MSU
     * @return True if the MSU was processed
     */
    virtual bool receivedMSU(const SS7MSU& msu, SS7Layer2* link) = 0;
};

/**
 * An interface to a Layer 2 (data link) SS7 message transfer part
 * @short Abstract SS7 layer 2 (data link) message transfer part
 */
class YSS7_API SS7Layer2 : virtual public SignallingComponent
{
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
     * Push a Message Signal Unit down the protocol stack
     * @param msu Message data, starting with Service Indicator Octet
     * @return True if message was successfully queued
     */
    virtual bool transmitMSU(const SS7MSU& msu) = 0;

    /**
     * Retrive the current link status indications
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
     * Attach a Layer 2 user component to the data link
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
     * Execute a control operation. Operations can change the link status or
     *  can query the aligned status.
     * @param oper Operation to execute
     * @param params Optional parameters for the operation
     * @return True if the command completed successfully, for query operations
     *  also indicates the data link is aligned and operational
     */
    virtual bool control(Operation oper, NamedList* params = 0);

protected:
    /**
     * Constructor
     */
    inline SS7Layer2()
	: m_l2user(0)
	{ }

    /**
     * Push a received Message Signal Unit up the protocol stack
     * @param msu Message data, starting with Service Indicator Octet
     * @return True if message was successfully delivered to the user component
     */
    inline bool receivedMSU(const SS7MSU& msu)
	{ return m_l2user && m_l2user->receivedMSU(msu,this); }

private:
    SS7L2User* m_l2user;
};

/**
 * An user of a Layer 3 (data link) SS7 message transfer part
 * @short Abstract user of SS7 layer 3 (network) message transfer part
 */
class YSS7_API SS7L3User : virtual public SignallingComponent
{
    friend class SS7Layer3;
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
     * @param link Network layer that delivered the MSU
     * @return True if the MSU was processed
     */
    virtual bool receivedMSU(const SS7MSU& msu, SS7Layer3* network) = 0;
};

/**
 * An interface to a Layer 3 (network) SS7 message transfer part
 * @short Abstract SS7 layer 3 (network) message transfer part
 */
class YSS7_API SS7Layer3 : virtual public SignallingComponent
{
public:
    /**
     * Push a Message Signal Unit down the protocol stack
     * @param msu Message data, starting with Service Indicator Octet
     * @param sls Signalling Link Selection, negative to choose best
     * @return True if message was successfully queued to a link
     */
    virtual bool transmitMSU(const SS7MSU& msu, int sls = -1) = 0;

    /**
     * Attach a Layer 3 user component to this network
     * @param l3user Pointer to Layer 3 user component to attach
     */
    void attach(SS7L3User* l3user);

    /**
     * Retrive the Layer 3 user component to which this network is attached
     * @return Pointer to the Layer 3 user this network is attached to
     */
    inline SS7L3User* user() const
	{ return m_l3user; }

    /**
     * Retrive the codepoint type of this Layer 3 component
     * @return The type of codepoint this component is able to use
     */
    inline SS7CodePoint::Type type() const
	{ return m_cpType; }

protected:
    /**
     * Constructor
     * @param type Codepoint type
     */
    inline SS7Layer3(SS7CodePoint::Type type = SS7CodePoint::Other)
	: m_l3user(0), m_cpType(type)
	{ }

    /**
     * Push a received Message Signal Unit up the protocol stack
     * @param msu Message data, starting with Service Indicator Octet
     * @return True if message was successfully delivered to the user component
     */
    inline bool receivedMSU(const SS7MSU& msu)
	{ return m_l3user && m_l3user->receivedMSU(msu,this); }

private:
    SS7L3User* m_l3user;
    SS7CodePoint::Type m_cpType;
};

/**
 * An interface to a Layer 4 (application) SS7 protocol
 * @short Abstract SS7 layer 4 (application) protocol
 */
class YSS7_API SS7Layer4 : public SS7L3User
{
public:
    /**
     * Attach a SS7 network or router to this service
     * @param router Pointer to network or router to attach
     */
    virtual void attach(SS7Layer3* network);

    /**
     * Retrive the SS7 network or router to which this service is attached
     * @return Pointer to the network or router this service is attached to
     */
    inline SS7Layer3* network() const
	{ return m_layer3; }

private:
    SS7Layer3* m_layer3;
};

/**
 * A message router between Transfer and Application layers.
 *  Messages are distributed according to the service type.
 * @short Main router for SS7 message transfer and applications
 */
class YSS7_API SS7Router : public SS7L3User, public SS7Layer3
{
public:
    /**
     * Push a Message Signal Unit down the protocol stack
     * @param msu Message data, starting with Service Indicator Octet
     * @param sls Signalling Link Selection, negative to choose best
     * @return True if message was successfully queued to a link
     */
    virtual bool transmitMSU(const SS7MSU& msu, int sls = -1);

    /**
     * Attach a SS7 Layer 3 (network) to the router
     * @param network Pointer to network to attach
     */
    virtual void attach(SS7Layer3* network);

    /**
     * Attach a SS7 Layer 4 (service) to the router
     * @param service Pointer to service to attach
     */
    void attach(SS7Layer4* service);

protected:
    /**
     * Process a MSU received from the Layer 3 component
     * @param msu Message data, starting with Service Indicator Octet
     * @param link Network layer that delivered the MSU
     * @return True if the MSU was processed
     */
    virtual bool receivedMSU(const SS7MSU& msu, SS7Layer3* network);

    ObjList m_layer3;
    ObjList m_layer4;
};

/**
 * RFC4165 SS7 Layer 2 implementation over SCTP/IP.
 * M2PA is intended to be used as a symmetrical Peer-to-Peer replacement of
 *  a hardware based SS7 data link.
 * @short SIGTRAN MTP2 User Peer-to-Peer Adaptation Layer
 */
class YSS7_API SS7M2PA : public SS7Layer2, public SIGTRAN
{
};

/**
 * RFC3331 SS7 Layer 2 implementation over SCTP/IP.
 * M2UA is intended to be used as a Provider-User where real MTP2 runs on a
 *  Signalling Gateway and MTP3 runs on an Application Server.
 * @short SIGTRAN MTP2 User Adaptation Layer
 */
class YSS7_API SS7M2UA : public SS7Layer2, public SIGTRAN
{
};

/**
 * RFC3332 SS7 Layer 3 implementation over SCTP/IP.
 * M3UA is intended to be used as a Provider-User where real MTP3 runs on a
 *  Signalling Gateway and MTP users are located on an Application Server.
 * @short SIGTRAN MTP3 User Adaptation Layer
 */
class YSS7_API SS7M3UA : public SS7Layer3, public SIGTRAN
{
};

/**
 * Q.703 SS7 Layer 2 (Data Link) implementation on top of a hardware interface
 * @short SS7 Layer 2 implementation on top of a hardware interface
 */
class YSS7_API SS7MTP2 : public SS7Layer2, public SignallingReceiver, public Mutex
{
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
     */
    SS7MTP2(unsigned int status = OutOfService);

    /**
     * Push a Message Signal Unit down the protocol stack
     * @param msu MSU data to transmit
     * @return True if message was successfully queued
     */
    virtual bool transmitMSU(const SS7MSU& msu);

    /**
     * Retrive the current link status indications
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

protected:
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
	{ return transmitLSSU(m_status); }

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
     */
    void abortAlignment();

    /**
     * Start the link proving period
     * @return True if proving period was started
     */
    bool startProving();

private:
    void setLocalStatus(unsigned int status);
    void setRemoteStatus(unsigned int status);
    // sent but yet unacknowledged packets
    ObjList m_queue;
    // data link status (alignment) - desired, local and remote
    unsigned int m_status, m_lStatus, m_rStatus;
    // various interval period end
    u_int64_t m_interval;
    // remote congestion indicator
    bool m_congestion;
    // backward and forward sqeuence numbers
    unsigned char m_bsn, m_fsn;
    // backward and forward indicator bits
    bool m_bib, m_fib;
};

/**
 * Q.704 SS7 Layer 3 (Network) implementation on top of SS7 Layer 2
 * @short SS7 Layer 3 implementation on top of Layer 2
 */
class YSS7_API SS7MTP3 : public SS7Layer3, public SS7L2User
{
public:
    /**
     * Constructor
     */
    SS7MTP3(SS7CodePoint::Type type = SS7CodePoint::Other);

    /**
     * Push a Message Signal Unit down the protocol stack
     * @param msu Message data, starting with Service Indicator Octet
     * @param sls Signalling Link Selection, negative to choose best
     * @return True if message was successfully queued to a link
     */
    virtual bool transmitMSU(const SS7MSU& msu, int sls = -1);

    /**
     * Attach a SS7 Layer 2 (data link) to the network transport
     * @param link Pointer to data link to attach
     */
    virtual void attach(SS7Layer2* link);

protected:
    /**
     * Process a MSU received from the Layer 2 component
     * @param msu Message data, starting with Service Indicator Octet
     * @param link Data link that delivered the MSU
     * @return True if the MSU was processed
     */
    virtual bool receivedMSU(const SS7MSU& msu, SS7Layer2* link);

    ObjList m_links;
};

/**
 * Decoded ISDN User Part message
 * @short ISUP signalling message
 */
class YSS7_API ISUPMessage : public SignallingMessage
{
public:
    enum Type {
	IAM   = 0x01, // Initial Address Message
	SAM   = 0x02, // Subsequent Address Message
	ACM   = 0x06, // Address Complete Message
	CON   = 0x07, // Connect Message
	ANM   = 0x09, // Answer Message
	REL   = 0x0c, // Release Request
	RLC   = 0x10, // Release Complete
    };
};

/**
 * Implementation of SS7 ISDN User Part
 * @short SS7 ISUP implementation
 */
class YSS7_API SS7ISUP : public SignallingCallControl, public SS7Layer4
{
};

/**
 * Implementation of SS7 Telephone User Part
 * @short SS7 TUP implementation
 */
class YSS7_API SS7TUP : public SignallingCallControl, public SS7Layer4
{
};

/**
 * Implementation of SS7 Signalling Connection Control Part
 * @short SS7 SCCP implementation
 */
class YSS7_API SS7SCCP : public SS7Layer4, public SCCP
{
};

/**
 * RFC3868 SS7 SCCP implementation over SCTP/IP
 * SUA is intended to be used as a Provider-User where real SCCP runs on a
 *  Signalling Gateway and SCCP users are located on an Application Server.
 * @short SIGTRAN SCCP User Adaptation Layer
 */
class YSS7_API SS7SUA : public SIGTRAN, public SCCP
{
};

/**
 * Implementation of SS7 Application Service Part
 * @short SS7 ASP implementation
 */
class YSS7_API SS7ASP : public SCCPUser, virtual public SignallingComponent
{
protected:
    ObjList m_sccps;
};

/**
 * Implementation of SS7 Transactional Capabilities Application Part
 * @short SS7 TCAP implementation
 */
class YSS7_API SS7TCAP : public ASPUser, virtual public SignallingComponent
{
    /**
     * Attach a SS7 TCAP user
     * @param user Pointer to the TCAP user to attach
     */
    void attach(TCAPUser* user);

protected:
    ObjList* m_users;
};

// The following classes are ISDN, not SS7, but they use the same signalling
//  interfaces so they will remain here

/**
 * An interface to a Layer 2 (Q.921) ISDN message transport
 * @short Abstract ISDN layer 2 (Q.921) message transport
 */
class YSS7_API ISDNLayer2 : virtual public SignallingComponent
{
    /**
     * Attach an ISDN Q.931 call control
     * @param user Pointer to the Q.931 call control to attach
     */
    void attach(ISDNLayer3* q931);

private:
    ISDNLayer3* m_q931;
};

/**
 * An interface to a Layer 3 (Q.931) ISDN message transport
 * @short Abstract ISDN layer 3 (Q.931) message transport
 */
class YSS7_API ISDNLayer3 : virtual public SignallingComponent
{
};

/**
 * Q.921 ISDN Layer 2 implementation on top of a hardware HDLC interface
 * @short ISDN Q.921 implementation on top of a hardware interface
 */
class YSS7_API ISDNQ921 : public ISDNLayer2, public SignallingReceiver
{
};

/**
 * RFC4233 ISDN Layer 2 implementation over SCTP/IP
 * IUA is intended to be used as a Provider-User where Q.921 runs on a
 *  Signalling Gateway and the user (Q.931) runs on an Application Server.
 * @short SIGTRAN ISDN Q.921 User Adaptation Layer
 */
class YSS7_API ISDNIUA : public ISDNLayer2, public SIGTRAN
{
};

/**
 * Q.931 ISDN Layer 3 implementation on top of a Layer 2
 * @short ISDN Q.931 implementation on top of Q.921
 */
class YSS7_API ISDNQ931 : public SignallingCallControl, public ISDNLayer3
{
    /**
     * Attach an ISDN Q.921 transport
     * @param user Pointer to the Q.921 transport to attach
     */
    void attach(ISDNLayer2* q921);

private:
    ISDNLayer2* m_q921;
};

}

#endif /* __YATESS7_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
