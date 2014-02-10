/**
 * yatepbx.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Common C++ base classes for PBX related plugins
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

#ifdef _WINDOWS

#ifdef LIBYPBX_EXPORTS
#define YPBX_API __declspec(dllexport)
#else
#ifndef LIBYPBX_STATIC
#define YPBX_API __declspec(dllimport)
#endif
#endif

#endif /* _WINDOWS */

#ifndef YPBX_API
#define YPBX_API
#endif

namespace TelEngine {

/**
 * Hold extra informations about an active CallEndpoint
 */
class YPBX_API CallInfo : public NamedList
{
public:
    inline CallInfo(const char* name, CallEndpoint* call = 0)
	: NamedList(name), m_call(call)
	{ }

    virtual ~CallInfo()
	{ m_call = 0; }

    inline CallEndpoint* call() const
	{ return m_call; }

    inline void setCall(CallEndpoint* call)
	{ m_call = call; }

    inline void clearCall()
	{ m_call = 0; }

    /**
     * Copy one parameter from a NamedList - typically a Message
     */
    bool copyParam(const NamedList& original, const String& name, bool clear = false);

    /**
     * Copy many parameters from a NamedList, end the list with a NULL or 0
     */
    void copyParams(const NamedList& original, bool clear, ...);
    void fillParam(NamedList& target, const String& name, bool clear = false);
    void fillParams(NamedList& target);

protected:
    CallEndpoint* m_call;
    int m_route;
};

/**
 * Hold a list of call informations
 */
class YPBX_API CallList
{
public:
    inline void append(CallInfo* call)
	{ m_calls.append(call); }
    inline void remove(CallInfo* call)
	{ m_calls.remove(call,false); }
    CallInfo* find(const String& id);
    CallInfo* find(const CallEndpoint* call);
protected:
    ObjList m_calls;
};

class YPBX_API MultiRouter : public MessageReceiver, public Mutex
{
public:
    enum {
	Route,
	Execute,
	Hangup,
	Disconnected
    };
    MultiRouter(const char* trackName = 0);
    virtual ~MultiRouter();
    void setup(int priority = 0);
    virtual bool received(Message& msg, int id);
    virtual bool msgRoute(Message& msg, CallInfo& info, bool first);
    virtual bool msgExecute(Message& msg, CallInfo& info, bool first);
    virtual bool msgDisconnected(Message& msg, CallInfo& info);
    virtual void msgHangup(Message& msg, CallInfo& info);
    virtual Message* buildExecute(CallInfo& info, bool reroute) = 0;
    Message* defaultExecute(CallInfo& info, const char* route = 0);
protected:
    CallList m_list;
private:
    String m_trackName;
    MessageRelay* m_relRoute;
    MessageRelay* m_relExecute;
    MessageRelay* m_relHangup;
    MessageRelay* m_relDisconnected;
};

class ChanAssistList;

/**
 * Object that assists a channel
 */
class YPBX_API ChanAssist :  public RefObject
{
public:
    /**
     * Destructor
     */
    virtual ~ChanAssist();

    /**
     * Get the String value of this object
     * @return ID of the assisted channel
     */
    virtual const String& toString() const
	{ return m_chanId; }

    /**
     * Process the chan.startup message
     * @param msg First channel message, may be received after call.execute
     */
    virtual void msgStartup(Message& msg);

    /**
     * Process the chan.hangup message
     * @param msg Last channel message
     */
    virtual void msgHangup(Message& msg);

    /**
     * Process the call.execute message, copy any parameters needed later
     * @param msg Call execute message, may be received before chan.startup
     */
    virtual void msgExecute(Message& msg);

    /**
     * Process the channel disconnect message, may connect to something else
     * @param msg The chan.disconnected message
     * @param reason The disconnection reason
     */
    virtual bool msgDisconnect(Message& msg, const String& reason);

    /**
     * Retrieve the list that owns this object
     * @return Pointer to the owner list
     */
    inline ChanAssistList* list() const
	{ return m_list; }

    /**
     * Get the name of the assisted channel
     * @return Identifier of the channel
     */
    inline const String& id() const
	{ return m_chanId; }

    /**
     * Retrieve a smart pointer to an arbitrary channel
     * @param id Identifier of the channel to locate
     * @return Smart pointer to the channel or NULL if not found or dead
     */
    static RefPointer<CallEndpoint> locate(const String& id);

    /**
     * Retrieve a smart pointer to the assisted channel
     * @return Smart pointer to the channel or NULL if not found or dead
     */
    inline RefPointer<CallEndpoint> locate() const
	{ return locate(m_chanId); }

protected:
    /**
     * Constructor of base class
     * @param list ChanAssistList that owns this object
     * @param id Identifier of the assisted channel
     */
    inline ChanAssist(ChanAssistList* list, const String& id)
	: m_list(list), m_chanId(id)
	{ }
private:
    ChanAssist(); // no default constructor please
    ChanAssistList* m_list;
    String m_chanId;
};

/**
 * Class keeping a list of ChanAssist objects. It also serves as base to
 *  implement channel assisting plugins.
 */
class YPBX_API ChanAssistList : public Module
{
    friend class ChanAssist;
public:
    /**
     * Message realy IDs
     */
    enum {
	Startup = Private,
	Hangup,
	Disconnected,
	AssistPrivate
    };

    /**
     * Destructor
     */
    virtual ~ChanAssistList()
	{ }

    /**
     * Message handler called internally
     * @param msg Received nessage
     * @param id Numeric identifier of the message type
     * @return True if the message was handled and further processing should stop
     */
    virtual bool received(Message& msg, int id);

    /**
     * Message handler for an assistant object
     * @param msg Received nessage
     * @param id Numeric identifier of the message type
     * @param assist Pointer to the matching assistant object
     * @return True if the message was handled and further processing should stop
     */
    virtual bool received(Message& msg, int id, ChanAssist* assist);

    /**
     * Method to (re)initialize the plugin
     */
    virtual void initialize();

    /**
     * Create a new channel assistant
     * @param msg Message that triggered the creation
     * @param id Channel's identifier
     * @return Pointer to new assistant object, NULL if unacceptable
     */
    virtual ChanAssist* create(Message& msg, const String& id) = 0;

    /**
     * Initialize the plugin for the first time
     * @param priority Priority used to install message handlers
     */
    virtual void init(int priority = 15);

    /**
     * Find a channel assistant by channel ID
     * @param id Identifier of the assisted channel
     * @return Pointer to the assistant object
     */
    inline ChanAssist* find(const String& id) const
	{ return static_cast<ChanAssist*>(m_calls[id]); }

protected:
    /**
     * Constructor
     * @param name Name of the module
     * @param earlyInit True to attempt to initialize module before others
     */
    inline ChanAssistList(const char* name, bool earlyInit = false)
	: Module(name, "misc", earlyInit), m_first(true)
	{ }

    /**
     * Removes an assistant object from list
     * @param assist Object to remove from list
     */
    void removeAssist(ChanAssist* assist);

    /**
     * Access to the assisted calls list
     * @return The HashList holding the calls
     */
    inline HashList& calls()
	{ return m_calls; }

    /**
     * Access to the assisted calls list
     * @return The HashList holding the calls
     */
    inline const HashList& calls() const
	{ return m_calls; }

private:
    ChanAssistList(); // no default constructor please
    HashList m_calls;
    bool m_first;
};

}

/* vi: set ts=8 sw=4 sts=4 noet: */
