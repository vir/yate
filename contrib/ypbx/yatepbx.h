/**
 * yatepbx.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Common C++ base classes for PBX related plugins
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

class YPBX_API MultiRouter : public MessageReceiver
{
public:
    enum {
	Route,
	Execute,
	Hangup,
	Disconnected
    };
    MultiRouter();
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
    Mutex m_mutex;
private:
    MessageRelay* m_relRoute;
    MessageRelay* m_relExecute;
    MessageRelay* m_relHangup;
    MessageRelay* m_relDisconnected;
};

}

/* vi: set ts=8 sw=4 sts=4 noet: */
