/**
 * yatess7.h
 * Yet Another SS7 Stack
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2006 Null Team
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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
class SignallingReceiver;
class SS7Layer3;

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
     * Destructor
     */
    virtual ~SignallingComponent();

    /**
     * Get the @ref SignallingEngine that manages this component
     * @return Pointer to engine or NULL if not managed by an engine
     */
    inline SignallingEngine* engine() const
	{ return m_engine; }

protected:
    /**
     * Default constructor
     */
    inline SignallingComponent()
	: m_engine(0)
	{ }

    /**
     * Detach this component from all its links - components and engine
     */
    virtual void detach();

    /**
     * Method called periodically by the engine to keep everything alive
     * @param when Time to use as computing base for events and timeouts
     */
    virtual void timerTick(const Time& when);

private:
    SignallingEngine* m_engine;
};

/**
 * The engine is the center of all SS7 or ISDN applications.
 * It is used as a base to build the protocol stack from components.
 * @short Main message transfer and application hub
 */
class YSS7_API SignallingEngine : public DebugEnabler, public Mutex
{
    friend class SignallingComponent;
public:
    /**
     * Constructor of an empty engine
     */
    SignallingEngine();

    /**
     * Destructor, removes all components
     */
    virtual ~SignallingEngine();

protected:
    /**
     * Insert a component in the engine
     * @param component Pointer to component to insert in engine
     */
    void insert(SignallingComponent* component);

    /**
     * Remove a component from the engine
     * @param component Pointer to component to remove from engine
     */
    void remove(SignallingComponent* component);

    /**
     * Method called periodically by a @ref Thread to keep everything alive
     * @param when Time to use as computing base for events and timeouts
     */
    virtual void timerTick(const Time& when);

    /**
     * The list of components managed by this engine
     */
    ObjList m_components;
};

/**
 * Interface of protocol independent call signalling
 * @short Abstract phone call signalling
 */
class YSS7_API CallSignalling
{
};

/**
 * An interface to an abstraction of a Layer 1 (hardware HDLC) interface
 * @short Abstract digital signalling interface (hardware access)
 */
class YSS7_API SignallingInterface : virtual public SignallingComponent
{
private:
    SignallingReceiver* m_receiver;
};

/**
 * An interface to an abstraction of a Layer 2 data receiver
 * @short Abstract Layer 2 data receiver
 */
class YSS7_API SignallingReceiver
{
public:
    /**
     * Attach a hardware interface to the data link
     * @param iface Pointer to interface to attach
     */
    void attach(SignallingInterface* iface);

protected:
    SignallingInterface* m_interface;
};

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
};

/**
 * An interface to a SS7 Signalling Connection Control Part user
 * @short Abstract SS7 SCCP user interface
 */
class YSS7_API SCCPUser
{
};

/**
 * An interface to a SS7 Transactional Capabilities Application Part user
 * @short Abstract SS7 TCAP user interface
 */
class YSS7_API TCAPUser
{
};

/**
 * An interface to a Layer 2 (data link) SS7 message transfer part
 * @short Abstract SS7 layer 2 (data link) message transfer part
 */
class YSS7_API SS7Layer2 : virtual public SignallingComponent
{
public:
    /**
     * Attach a SS7 Layer 3 (network) to the data link
     * @param link Pointer to network to attach
     */
    void attach(SS7Layer3* network);

protected:
    SS7Layer3* m_network;
};

/**
 * An interface to a Layer 3 (network) SS7 message transfer part
 * @short Abstract SS7 layer 3 (network) message transfer part
 */
class YSS7_API SS7Layer3 : virtual public SignallingComponent
{
};

/**
 * An interface to a Layer 4 (application) SS7 protocol
 * @short Abstract SS7 layer 4 (application) protocol
 */
class YSS7_API SS7Layer4 : virtual public SignallingComponent
{
};

/**
 * An interface to a Layer 2 (Q.921) ISDN message transport
 * @short Abstract ISDN layer 2 (Q.921) message transport
 */
class YSS7_API ISDNLayer2 : virtual public SignallingComponent
{
};

/**
 * An interface to a Layer 3 (Q.931) ISDN message transport
 * @short Abstract ISDN layer 3 (Q.931) message transport
 */
class YSS7_API ISDNLayer3 : virtual public SignallingComponent
{
};

/**
 * RFC4165 SS7 Layer 2 implementation over SCTP/IP
 * @short SIGTRAN MTP2 User Peer-to-Peer Adaptation Layer
 */
class YSS7_API SS7M2PA : public SS7Layer2, public SIGTRAN
{
};

/**
 * RFC3331 SS7 Layer 2 implementation over SCTP/IP
 * @short SIGTRAN MTP2 User Adaptation Layer
 */
class YSS7_API SS7M2UA : public SS7Layer2, public SIGTRAN
{
};

/**
 * RFC3332 SS7 Layer 3 implementation over SCTP/IP
 * @short SIGTRAN MTP3 User Adaptation Layer
 */
class YSS7_API SS7M3UA : public SS7Layer3, public SIGTRAN
{
};

/**
 *
 * @short SS7 Layer 2 implementation on top of a hardware interface
 */
class YSS7_API SS7MTP2 : public SS7Layer2, public SignallingReceiver
{
};

/**
 *
 * @short SS7 Layer 3 implementation on top of Layer 2
 */
class YSS7_API SS7MTP3 : public SS7Layer3
{
public:
    /**
     * Attach a SS7 Layer 2 (data link) to the network transport
     * @param link Pointer to data link to attach
     */
    void attach(SS7Layer2* link);

private:
    ObjList m_links;
};

/**
 * Implementation of SS7 ISDN User Part
 * @short SS7 ISUP implementation
 */
class YSS7_API SS7ISUP : public CallSignalling, public SS7Layer4
{
};

/**
 * Implementation of SS7 Broadband ISDN User Part
 * @short SS7 BISUP implementation
 */
class YSS7_API SS7BISUP : public CallSignalling, public SS7Layer4
{
};

/**
 * Implementation of SS7 Telephone User Part
 * @short SS7 TUP implementation
 */
class YSS7_API SS7TUP : public CallSignalling, public SS7Layer4
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
    /**
     * Attach a SS7 SCCP to the ASP
     * @param sccp Pointer to the SCCP to attach
     */
    void attach(SCCP* sccp);

private:
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

};

/**
 *
 * @short ISDN Q.921 implementation on top of a hardware interface
 */
class YSS7_API ISDNQ921 : public ISDNLayer2, public SignallingReceiver
{
};

/**
 * RFC4233 ISDN Layer 2 implementation over SCTP/IP
 * @short SIGTRAN ISDN Q.921 User Adaptation Layer
 */
class YSS7_API ISDNIUA : public ISDNLayer2, public SIGTRAN
{
};

/**
 *
 * @short ISDN Q.931 implementation on top of Q.921
 */
class YSS7_API ISDNQ931 : public CallSignalling, public ISDNLayer3
{
    /**
     * Attach an ISDN Q.921 transport
     * @param user Pointer to the Q.921 transport to attach
     */
    void attach(ISDNLayer2* q921);
};

}

#endif /* __YATESS7_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
