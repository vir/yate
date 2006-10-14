/**
 * yiaxchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * IAX channel
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
 * Author: Marian Podgoreanu
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

#include <yatephone.h>
#include <yateversn.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>

#include <yateiax.h>

using namespace TelEngine;
namespace { // anonymous

static TokenDict dict_tos[] = {
    { "lowdelay", Socket::LowDelay },
    { "throughput", Socket::MaxThroughput },
    { "reliability", Socket::MaxReliability },
    { "mincost", Socket::MinCost },
    { 0, 0 }
};

class YIAXLineContainer;
class YIAXEngine;
class IAXURI;

/*
 * Keep a single registration line
 */
class YIAXLine : public String
{
    friend class YIAXLineContainer;
    friend class YIAXEngine;
public:
    enum State {
	Idle,
	Registering,
	Unregistering,
    };
    YIAXLine(const String& name);
    virtual ~YIAXLine();
    inline State state() const
	{ return m_state; }
    inline bool registered() const
	{ return m_registered; }
    inline const String& username() const
	{ return m_username; }
    inline const String& password() const
	{ return m_password; }
    inline const String& callingNo() const
	{ return m_callingNo; }
    inline const String& callingName() const
	{ return m_callingName; }
    inline u_int16_t expire() const
	{ return m_expire; }
    inline const String& localAddr() const
	{ return m_localAddr; }
    inline const String& remoteAddr() const
	{ return m_remoteAddr; }
    inline int localPort() const
	{ return m_localPort; }
    inline int remotePort() const
	{ return m_remotePort; }
private:
    void setRegistered(bool registered, const char* reason = 0);
    State m_state;
    String m_username;                  // Username
    String m_password;                  // Password
    String m_callingNo;                 // Calling number
    String m_callingName;               // Calling name
    u_int16_t m_expire;                 // Expire time
    String m_localAddr;
    String m_remoteAddr;
    int m_localPort;
    int m_remotePort;
    u_int32_t m_nextReg;                // Time to next registration
    u_int32_t m_nextKeepAlive;          // Time to next keep alive signal
    bool m_registered;			// Registered flag. If true the line is registered
    bool m_register;                    // Operation flag: True - register
    IAXTransaction* m_transaction;
};

/*
 * Line container: Add/Delete/Update/Register/Unregister lines
 */
class YIAXLineContainer : public Mutex
{
public:
    inline YIAXLineContainer() : Mutex(true) {}
    inline ~YIAXLineContainer() {}

    /*
     * Logout and remove all lines
     * @param forced Forcedly remove lines synchronously
     */
    void clear(bool forced = false);

    /*
     * Update a line from a message
     * This method is thread safe 
     * @param msg Received message
     * @return True if the successfully updated
     */
    bool updateLine(Message &msg);

    /*
     * Event handler for a registration.
     * @param event The event.
     */
    void handleEvent(IAXEvent* event);

    /*
     * Terminate notification of a Register/Unregister operation
     * This method is thread safe
     * @param event The event (result)
     */
    void regTerminate(IAXEvent* event);

    /*
     * Timer notification
     * This method is thread safe
     * @param time Time
     */
    void evTimer(Time& time);

    /*
     * Fill a named list from a line
     * This method is thread safe
     */
    bool fillList(String& name, NamedList& dest, SocketAddr& addr, bool& registered);

    /*
     * Check if a line exists
     */
    inline bool hasLine(const String& line)
	{ Lock lock(this); return findLine(line) != 0; }

protected:
    bool updateLine(YIAXLine* line, Message &msg);
    bool addLine(Message &msg);
    YIAXLine* findLine(const String& name);
    YIAXLine* findLine(YIAXLine* line);
    void startRegisterLine(YIAXLine* line);
    void startUnregisterLine(YIAXLine* line);
private:
    ObjList m_lines;
};

/*
 * Thread class for reading data from socket for the specified IAX engine
 */
class YIAX_API YIAXListener : public Thread
{
public:
    inline YIAXListener(YIAXEngine* engine, const char* name = 0, Priority prio = Normal)
        : Thread(name,prio), m_engine(engine)
        {}
    virtual void run();
protected:
    YIAXEngine* m_engine;
};

/*
 * Thread class for reading events for the specified IAX engine
 */
class YIAX_API YIAXGetEvent : public Thread
{
public:
    inline YIAXGetEvent(YIAXEngine* engine, const char* name = 0, Priority prio = Normal)
	: Thread(name,prio), m_engine(engine)
	{}
    virtual void run();
protected:
    YIAXEngine* m_engine;
};

/*
 * Thread class for sending trunked mini frames for the specified IAX engine
 */
class YIAX_API YIAXTrunking : public Thread
{
public:
    inline YIAXTrunking(YIAXEngine* engine, const char* name = 0, Priority prio = Normal)
	: Thread(name,prio), m_engine(engine)
	{}
    virtual void run();
protected:
    YIAXEngine* m_engine;
};

/*
 * The IAX engine for this driver
 */
class YIAXEngine : public IAXEngine
{
public:
    /*
     * Constructor
     * @param iface Interface address to use
     * @param port UDP port to use
     * @param transListCount Number of entries in the transaction hash table
     * @param retransCount Retransmission counter for each transaction belonging to this engine
     * @param retransInterval Retransmission interval default value in miliseconds
     * @param authTimeout Timeout (in seconds) of acknoledged auth frames sent
     * @param transTimeout Timeout (in seconds) on remote request of transactions belonging to this engine
     * @param maxFullFrameDataLen Max full frame IE list (buffer) length
     * @param trunkSendInterval Send trunk meta frame interval
     * @param authRequired Automatically challenge all clients for authentication
     */
    YIAXEngine(const char* iface, int port, u_int16_t transListCount, u_int16_t retransCount, u_int16_t retransInterval,
	u_int16_t authTimeout, u_int16_t transTimeout,
	u_int16_t maxFullFrameDataLen, u_int32_t trunkSendInterval, bool authRequired);

    virtual ~YIAXEngine()
	{}

    /*
     * Process media from remote peer.
     * @param transaction IAXTransaction that owns the call leg
     * @param data Media data. 
     * @param tStamp Media timestamp. 
     */
    virtual void processMedia(IAXTransaction* transaction, DataBlock& data, u_int32_t tStamp);

    /*
     * Initiate an outgoing registration (release) request.
     * @param line YIAXLine pointer to use for registration.
     * @param regreq Registration request flag. If false a registration release will take place.
     * @return IAXTransaction pointer on success.
     */
    IAXTransaction* reg(YIAXLine* line, bool regreq = true);

    /*
     * Initiate an aoutgoing call.
     * @param addr Address to poke.
     * @param params Call parameters.
     * @return IAXTransaction pointer on success.
     */
    IAXTransaction* call(SocketAddr& addr, NamedList& params);

    /*
     * Initiate a test of existence of a remote IAX peer.
     * @param addr Address to poke.
     * @return IAXTransaction pointer on success.
     */
    IAXTransaction* poke(SocketAddr& addr);

    /*
     * Start thread members
     * @param listenThreadCount Reading socket thread count.
     * @param eventThreadCount Reading event thread count.
     * @param trunkingThreadCount Trunking thread count.
     */
    void start(u_int16_t listenThreadCount, u_int16_t eventThreadCount,u_int16_t trunkingThreadCount);

protected:

    /*
     * Event handler for transaction with a connection.
     */
    virtual void processEvent(IAXEvent* event);

    /*
     * Event handler for incoming registration transactions.
     * @param event The event.
     * @param first True if this is the first request.
     *  False if it is a response to an authentication request.
     */
    void processRemoteReg(IAXEvent* event,bool first);

    /*
     * Send Register/Unregister messages to Engine
     */
    bool userreg(IAXTransaction* tr, bool regrel = true);

private:
    bool m_threadsCreated;      // True if reading and get events threads were created
};

/*
 * YIAXRegDataHandler
 */
class YIAXRegDataHandler : public MessageHandler
{
public:
    YIAXRegDataHandler()
	: MessageHandler("user.login",150)
	{ }
    virtual bool received(Message &msg);
};

/*
 * YIAXDriver
 */
class YIAXDriver : public Driver
{
public:
    YIAXDriver();
    virtual ~YIAXDriver();
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest);
    virtual bool msgRoute(Message& msg);
    virtual bool received(Message& msg, int id);

    // Default codec map - should have only one bit set
    inline u_int32_t defaultCodec() const
	{ return m_defaultCodec; }

    // Supported codec map
    inline u_int32_t codecs() const
	{ return m_codecs; }

    // UDP port we are using, also used as default
    inline int port() const
	{ return m_port; }

    // Retrive the IAX engine
    inline YIAXEngine* getEngine() const
	{ return m_iaxEngine; }

    // Update codecs from 'formats' parameter of a message
    // @param codecs Codec list to update
    // @param formats The 'formats' parameter of a message
    // @return False if formtas is not 0 and the result is 0 (no intersection)
    bool updateCodecsFromRoute(u_int32_t& codecs, const char* formats);

    // Dispatch user.auth
    // @tr The IAX transaction
    // @param response True if it is a response.
    // @param requestAuth True on exit: the caller should request authentication
    // @param invalidAuth True on exit: authentication response is incorrect
    // @return False if not authenticated
    bool userAuth(IAXTransaction* tr, bool response, bool& requestAuth,
	bool& invalidAuth);

protected:
    YIAXEngine* m_iaxEngine;
    u_int32_t m_defaultCodec;
    u_int32_t m_codecs;
    int m_port;
};

class YIAXConnection;

/*
 * Connection's data consumer
 */
class YIAXConsumer : public DataConsumer
{
public:
    YIAXConsumer(YIAXConnection* conn, u_int32_t format, const char* formatText);
    ~YIAXConsumer();
    virtual void Consume(const DataBlock &data, unsigned long tStamp);
private:
    YIAXConnection* m_connection;
    unsigned m_total;
    u_int32_t m_format; // in IAX coding
};

/*
 * Connection's data source
 */
class YIAXSource : public DataSource
{
public:
    YIAXSource(YIAXConnection* conn, u_int32_t format, const char* formatText);
    ~YIAXSource();
    void Forward(const DataBlock &data, unsigned long tStamp = 0);
private:
    YIAXConnection* m_connection;
    unsigned m_total;
    u_int32_t m_format; // in IAX coding
};

/*
 * The connection
 */
class YIAXConnection : public Channel
{
public:
    YIAXConnection(YIAXEngine* iaxEngine, IAXTransaction* transaction, Message* msg = 0);
    virtual ~YIAXConnection();
    virtual void callAccept(Message& msg);
    virtual void callRejected(const char* error, const char* reason = 0, const Message* msg = 0);
    virtual bool callPrerouted(Message& msg, bool handled);
    virtual bool callRouted(Message& msg);
    virtual bool msgRinging(Message& msg);
    virtual bool msgAnswered(Message& msg);
    virtual bool msgTone(Message& msg, const char* tone);
    virtual bool msgText(Message& msg, const char* text);
    virtual void disconnected(bool final, const char* reason);

    inline IAXTransaction* transaction() const
        { return m_transaction; }

    inline bool mutedIn() const
	{ return m_mutedIn; }

    inline bool mutedOut() const
	{ return m_mutedOut; }

    void handleEvent(IAXEvent* event);

    bool route(bool authenticated = false);

protected:
    void hangup(const char* reason = 0, bool reject = false);
    inline void hangup(IAXEvent* event, const char* reason = 0, bool reject = false) {
	    event->setFinal();
	    hangup(reason,reject);
	}
    void startAudioIn();
    void startAudioOut();
    void evAuthRep(IAXEvent* event);
    // Safe deref the connection if the reference counter was increased during registration
    void safeDeref();
    bool safeRefIncrease();
private:
    YIAXEngine* m_iaxEngine;            // IAX engine owning the transaction
    IAXTransaction* m_transaction;      // IAX transaction
    String m_password;                  // Password for client authentication
    bool m_mutedIn;                     // No remote media accepted
    bool m_mutedOut;                    // No local media accepted
    String m_reason;                    // Call end reason text
    bool m_hangup;			// Need to send chan.hangup message
    Mutex m_mutexTrans;                 // Safe m_transaction operations
    Mutex m_mutexRefIncreased;          // Safe ref/deref connection
    bool m_refIncreased;                // If true, the reference counter was increased
};

/*
 * An IAX URI parser
 *  [iax[2]:][username@]host[:port][/called_number[@called_context]]
 */
class IAXURI : public String
{
public:
    inline IAXURI(String& s) : String(s), m_port(0), m_parsed(false) {}
    inline IAXURI(const char* s) : String(s), m_port(0), m_parsed(false) {}
    IAXURI(const char* user, const char* host, const char* calledNo, const char* calledContext, int port = 4569);
    inline ~IAXURI() {}
    void parse();
    bool fillList(NamedList& dest);
    bool setAddr(SocketAddr& dest);
    inline const String& username() const
	{ return m_username; }
    inline const String& host() const
	{ return m_host; }
    inline int port() const
	{ return m_port; }
    inline const String& calledNo() const
	{ return m_calledNo; }
    inline const String& calledContext() const
	{ return m_calledContext; }
protected:
    inline IAXURI() {}
private:
    String m_username;
    String m_host;
    int m_port;
    String m_calledNo;
    String m_calledContext;
    bool m_parsed;
};


/*
 * Local data
 */
static Configuration s_cfg; 		// Configuration file
static YIAXDriver iplugin;		// Init the driver
static YIAXLineContainer s_lines;	// Lines

/*
 * Class definitions
 */

// Create an idle line
YIAXLine::YIAXLine(const String& name)
    : String(name),
      m_state(Idle), m_expire(60), m_localPort(4569), m_remotePort(4569),
      m_nextReg(Time::secNow() + 40),
      m_registered(false),
      m_register(true)
{
}

YIAXLine::~YIAXLine()
{
}

// Set the registered status, emits user.notify messages if necessary
void YIAXLine::setRegistered(bool registered, const char* reason)
{
    if ((m_registered == registered) && !reason)
	return;
    m_registered = registered;
    if (m_username) {
	Message* m = new Message("user.notify");
	m->addParam("account",*this);
	m->addParam("protocol","iax");
	m->addParam("username",m_username);
	m->addParam("registered",String::boolText(registered));
	if (reason)
	    m->addParam("reason",reason);
	Engine::enqueue(m);
    }
}

// Find and update a line with parameters from message, create if needed
bool YIAXLineContainer::updateLine(Message& msg)
{
    Lock lock(this);
    String name = msg.getValue("account");
    YIAXLine* line = findLine(name);
    if (line)
	return updateLine(line,msg);
    return addLine(msg);
}

// Handle registration related transaction terminations
void YIAXLineContainer::regTerminate(IAXEvent* event)
{
    Lock lock(this);
    if (!event)
	return;
    IAXTransaction* trans = event->getTransaction();
    if (!trans)
	return;
    YIAXLine* line = findLine(static_cast<YIAXLine*>(trans->getUserData()));
    if (!line)
	return;
    switch (event->type()) {
	case IAXEvent::Accept:
	    // re-register at 75% of the expire time
	    line->m_nextReg = Time::secNow() + (line->expire() * 3 / 4);
	    line->m_callingNo = trans->callingNo();
	    line->m_callingName = trans->callingName();
	    Debug(&iplugin,DebugAll,"YIAXLineContainer::regTerminate[%s] - ACK for '%s'. Next: %u",
		line->c_str(),line->state() == YIAXLine::Registering?"Register":"Unregister",line->m_nextReg);
	    line->setRegistered(true);
	    break;
	case IAXEvent::Reject:
	    // retry at 25% of the expire time
	    line->m_nextReg = Time::secNow() + (line->expire() / 2);
	    Debug(&iplugin,DebugAll,"YIAXLineContainer::regTerminate[%s] - REJECT for '%s'. Next: %u",
		line->c_str(),line->state() == YIAXLine::Registering?"Register":"Unregister",line->m_nextReg);
	    line->setRegistered(false,"rejected");
	    break;
	case IAXEvent::Timeout:
	    // retry at 50% of the expire time
	    line->m_nextReg = Time::secNow() + (line->expire() / 2);
	    Debug(&iplugin,DebugAll,"YIAXLineContainer::regTerminate[%s] - Timeout for '%s'. Next: %u",
		line->c_str(),line->state() == YIAXLine::Registering?"Register":"Unregister",line->m_nextReg);
	    line->setRegistered(false,"timeout");
	    break;
	default:
	    return;
    }
    line->m_transaction = 0;
    // Unregister operation. Remove line
    if (line->state() == YIAXLine::Unregistering) {
	line->setRegistered(false);
	m_lines.remove(line,true);
	return;
    }
    line->m_state = YIAXLine::Idle;
}

// Handle registration related events
void YIAXLineContainer::handleEvent(IAXEvent* event)
{
    switch (event->type()) {
	case IAXEvent::Accept:
	case IAXEvent::Reject:
	case IAXEvent::Timeout:
	    regTerminate(event);
	    break;
	default:
	    iplugin.getEngine()->defaultEventHandler(event);
    }
}

// Tick the timer for all lines, send keepalives and reregister
void YIAXLineContainer::evTimer(Time& time)
{
    u_int32_t sec = time.sec();
    Lock lock(this);
    for (ObjList* l = m_lines.skipNull(); l; l = l->next()) {
	YIAXLine* line = static_cast<YIAXLine*>(l->get());
	// Line exists and is idle ?
	if (!line || line->state() != YIAXLine::Idle)
	    continue;
	// Time to keep alive
	if (sec > line->m_nextKeepAlive) {
	    line->m_nextKeepAlive = sec + 25;
	    SocketAddr addr(AF_INET);
	    addr.host(line->remoteAddr());
	    addr.port(line->remotePort());
	    iplugin.getEngine()->keepAlive(addr);
	}
	// Time to reg/unreg
	if (sec > line->m_nextReg) {
	    line->m_nextReg += line->expire();
	    if (line->m_register)
		startRegisterLine(line);
	    else
		startUnregisterLine(line);
	}
    }
}

// Fill parameters with information taken from line
bool YIAXLineContainer::fillList(String& name, NamedList& dest, SocketAddr& addr, bool& registered)
{
    Lock lock(this);
    registered = false;
    YIAXLine* line = findLine(name);
    if (!line)
	return false;
    dest.setParam("username",line->username());
    dest.setParam("password",line->password());
    dest.setParam("caller",line->callingNo());
    dest.setParam("callername",line->callingName());
    addr.host(line->remoteAddr());
    addr.port(line->remotePort());
    registered = line->registered();
    return true;
}

// Update a line with data from message, create or delete line if needed
bool YIAXLineContainer::updateLine(YIAXLine* line, Message& msg)
{
    Debug(&iplugin,DebugAll,"YIAXLineContainer - updateLine: %s",line->c_str());
    String op = msg.getValue("operation");
    if (op == "logout") {
	if (line->state() != YIAXLine::Unregistering)
	    return true;
	if (line->state() != YIAXLine::Idle && line->m_transaction)
	    line->m_transaction->abortReg();
	startUnregisterLine(line);
	return true;
    }
    bool change = false;
    if (line->m_remoteAddr != msg.getValue("server")) {
	line->m_remoteAddr = msg.getValue("server");
	change = true;
    }
    if (line->m_username != msg.getValue("username")) {
	line->m_username = msg.getValue("username");
	change = true;
    }
    if (line->m_password != msg.getValue("password")) {
	line->m_password = msg.getValue("password");
	change = true;
    }
    if (line->m_expire != String(msg.getValue("interval")).toInteger()) {
	line->m_expire = String(msg.getValue("interval")).toInteger();
	change = true;
    }
    line->m_nextReg = Time::secNow() + (line->m_expire * 3 / 4);
    line->m_nextKeepAlive = Time::secNow() + 25;
    if (change || op == "login")
	startRegisterLine(line);
    return change;
}

// Add a new line and start logging in to it if applicable
bool YIAXLineContainer::addLine(Message& msg)
{
    Debug(&iplugin,DebugAll,"YIAXLineContainer - addLine: %s",msg.getValue("account"));
    YIAXLine* line = new YIAXLine(msg.getValue("account"));
    m_lines.append(line);
    line->m_remoteAddr = msg.getValue("server");
    line->m_username = msg.getValue("username");
    line->m_password = msg.getValue("password");
    line->m_expire = msg.getIntValue("interval",60);
    line->m_nextReg = Time::secNow() + line->m_expire;
    String op = msg.getValue("operation");
    if (op.null() || (op == "login"))
	startRegisterLine(line);
    else
	if (op == "logout")
	    startUnregisterLine(line);
	else 
	    line->m_register = true;
    return true;
}

// Find a line by name
YIAXLine* YIAXLineContainer::findLine(const String& name)
{
    for (ObjList* l = m_lines.skipNull(); l; l = l->next()) {
	YIAXLine* line = static_cast<YIAXLine*>(l->get());
	if (line && *line == name)
	    return line;
    }
    return 0;
}

// Find a line by address, return same if found
YIAXLine* YIAXLineContainer::findLine(YIAXLine* line)
{
    if (!line)
	return 0;
    for (ObjList* l = m_lines.skipNull(); l; l = l->next()) {
	YIAXLine* ln = static_cast<YIAXLine*>(l->get());
	if (ln && ln == line)
	    return line;
    }
    return 0;
}

// Initiate registration of line, only if idle
void YIAXLineContainer::startRegisterLine(YIAXLine* line)
{
    Lock lock(this);
    line->m_register = true;
    if (line->state() != YIAXLine::Idle)
	return;
    if (iplugin.getEngine()->reg(line,true))
	line->m_state = YIAXLine::Registering;
}

// Initiate deregistration of line, only if idle
void YIAXLineContainer::startUnregisterLine(YIAXLine* line)
{
    Lock lock(this);
    line->m_register = false;
    if (line->state() != YIAXLine::Idle)
	return;
    if (iplugin.getEngine()->reg(line,false))
	line->m_state = YIAXLine::Unregistering;
}

// Unregister all lines
void YIAXLineContainer::clear(bool forced)
{
    Lock lock(this);
    if (forced) {
	m_lines.clear();
	return;
    }
    for (ObjList* l = m_lines.skipNull(); l; l = l->next()) {
	YIAXLine* line = static_cast<YIAXLine*>(l->get());
	if (line)
	    startUnregisterLine(line);
    }
}

// Run the socket listening thread
void YIAXListener::run()
{
    DDebug(m_engine,DebugAll,"%s started",currentName());
    SocketAddr addr;
    m_engine->readSocket(addr);
}

// Run the event retriving thread
void YIAXGetEvent::run()
{
    DDebug(m_engine,DebugAll,"%s started",currentName());
    m_engine->runGetEvents();
}

// Run the trunk sending thread
void YIAXTrunking::run()
{
    DDebug(m_engine,DebugAll,"%s started",currentName());
    m_engine->runProcessTrunkFrames();
}

/**
 * YIAXEngine
 */
YIAXEngine::YIAXEngine(const char* iface, int port, u_int16_t transListCount,
	u_int16_t retransCount, u_int16_t retransInterval, u_int16_t authTimeout,
	u_int16_t transTimeout, u_int16_t maxFullFrameDataLen,
	u_int32_t trunkSendInterval, bool authRequired)
    : IAXEngine(iface,port,transListCount,retransCount,retransInterval,authTimeout,
	transTimeout,maxFullFrameDataLen,iplugin.defaultCodec(),iplugin.codecs(),
	trunkSendInterval,authRequired),
      m_threadsCreated(false)
{
}

// Handle received voice data, forward it to connection's source
void YIAXEngine::processMedia(IAXTransaction* transaction, DataBlock& data, u_int32_t tStamp)
{
    if (transaction)
	if (transaction->getUserData())
	    if ((static_cast<YIAXConnection*>(transaction->getUserData()))->getSource())
		(static_cast<YIAXSource*>((static_cast<YIAXConnection*>(transaction->getUserData()))->getSource()))->Forward(data,tStamp);
	    else {
		XDebug(this,DebugAll,"YIAXEngine - processMedia. No media source");
	    }
	else
	    Debug(this,DebugAll,"YIAXEngine - processMedia. Transaction doesn't have a connection");
    else
	Debug(this,DebugAll,"YIAXEngine - processMedia. No transaction");
}

// Create a new registration transaction for a line
IAXTransaction* YIAXEngine::reg(YIAXLine* line, bool regreq)
{
    if (!line)
	return 0;
    SocketAddr addr(AF_INET);
    addr.host(line->remoteAddr());
    addr.port(line->remotePort());
    Debug(this,DebugAll,"Outgoing Registration[%s]:\nUsername: %s\nHost: %s\nPort: %d\nTime(sec): %u",
	line->c_str(),line->username().c_str(),addr.host().c_str(),addr.port(),Time::secNow());
    // Create IE list
    IAXIEList ieList;
    ieList.appendString(IAXInfoElement::USERNAME,line->username());
    ieList.appendString(IAXInfoElement::PASSWORD,line->password());
    ieList.appendString(IAXInfoElement::CALLING_NUMBER,line->callingNo());
    ieList.appendString(IAXInfoElement::CALLING_NAME,line->callingName());
    ieList.appendNumeric(IAXInfoElement::REFRESH,line->expire(),2);
    // Make it !
    IAXTransaction* tr = startLocalTransaction(regreq ? IAXTransaction::RegReq : IAXTransaction::RegRel,addr,ieList);
    if (tr)
	tr->setUserData(line);
    line->m_transaction = tr;
    return tr;
}

// Create a new call transaction from target address and message params
IAXTransaction* YIAXEngine::call(SocketAddr& addr, NamedList& params)
{
    Debug(this,DebugAll,"Outgoing Call:\nUsername: %s\nHost: %s\nPort: %d\nCalled number: %s\nCalled context: %s",
	params.getValue("username"),addr.host().c_str(),addr.port(),params.getValue("called"),params.getValue("calledname"));
    // Create IE list
    IAXIEList ieList;
    ieList.appendString(IAXInfoElement::USERNAME,params.getValue("username"));
    ieList.appendString(IAXInfoElement::PASSWORD,params.getValue("password"));
    ieList.appendString(IAXInfoElement::CALLING_NUMBER,params.getValue("caller"));
    ieList.appendString(IAXInfoElement::CALLING_NAME,params.getValue("callername"));
    ieList.appendString(IAXInfoElement::CALLED_NUMBER,params.getValue("called"));
    ieList.appendString(IAXInfoElement::CALLED_CONTEXT,params.getValue("iaxcontext"));
    ieList.appendNumeric(IAXInfoElement::FORMAT,iplugin.defaultCodec(),4);
    // Set capabilities
    u_int32_t codecs = iplugin.codecs();
    if (!iplugin.updateCodecsFromRoute(codecs,params.getValue("formats"))) {
	DDebug(this,DebugAll,"Outgoing call failed: No codecs.");
	params.setParam("error","nomedia");
	return 0;
    }
    ieList.appendNumeric(IAXInfoElement::CAPABILITY,codecs,4);
    return startLocalTransaction(IAXTransaction::New,addr,ieList);
}

// Create a POKE transaction
IAXTransaction* YIAXEngine::poke(SocketAddr& addr)
{
    Debug(this,DebugAll,"Outgoing POKE: Host: %s Port: %d",addr.host().c_str(),addr.port());
    IAXIEList ieList;
    return startLocalTransaction(IAXTransaction::Poke,addr,ieList);
}

void YIAXEngine::start(u_int16_t listenThreadCount, u_int16_t eventThreadCount, u_int16_t trunkThreadCount)
{
    if (m_threadsCreated)
	return;
    if (!listenThreadCount)
	Debug(this,DebugWarn,"YIAXEngine. No reading socket threads(s)!.");
    if (!eventThreadCount)
	Debug(this,DebugWarn,"YIAXEngine. No reading event threads(s)!.");
    if (!trunkThreadCount)
	Debug(this,DebugWarn,"YIAXEngine. No trunking threads(s)!.");
    for (; listenThreadCount; listenThreadCount--)
	(new YIAXListener(this,"YIAXListener thread"))->startup();
    for (; eventThreadCount; eventThreadCount--)
	(new YIAXGetEvent(this,"YIAXGetEvent thread"))->startup();
    for (; trunkThreadCount; trunkThreadCount--)
	(new YIAXTrunking(this,"YIAXTrunking thread"))->startup();
    m_threadsCreated = true;
}

// Process all IAX events
void YIAXEngine::processEvent(IAXEvent* event)
{
    YIAXConnection* connection = 0;
    switch (event->getTransaction()->type()) {
	case IAXTransaction::New:
	    connection = static_cast<YIAXConnection*>(event->getTransaction()->getUserData());
	    if (connection) {
		// We already have a channel for this call
		connection->handleEvent(event);
		if (event->final()) {
		    // Final event: disconnect
		    Debug(this,DebugAll,"YIAXEngine::processEvent - Disconnect connection [%p]",connection);
		    connection->disconnect();
		}
	    }
	    else {
		if (event->type() == IAXEvent::New) {
		    // Incoming request for a new call
		    connection = new YIAXConnection(this,event->getTransaction());
		    event->getTransaction()->setUserData(connection);
		    if (!connection->route())
			event->getTransaction()->setUserData(0);
		}
	    }
	    break;
	case IAXTransaction::RegReq:
	case IAXTransaction::RegRel:
	    if (event->type() == IAXEvent::New || event->type() == IAXEvent::AuthRep)
		processRemoteReg(event,(event->type() == IAXEvent::New));
	    else
		if (event->getTransaction()->getUserData())
		    s_lines.handleEvent(event);
	    break;
	default: ;
    }
    delete event;
}

// Process events for remote users registering to us
void YIAXEngine::processRemoteReg(IAXEvent* event, bool first)
{
    IAXTransaction* tr = event->getTransaction();
    Debug(this,DebugAll,"processRemoteReg: %s username: '%s'",
	tr->type() == IAXTransaction::RegReq?"Register":"Unregister",tr->username().c_str());
    // Check for automatomatically authentication request if it's the first request
    if (first && iplugin.getEngine()->authRequired()) {
	Debug(this,DebugAll,"processRemoteReg. Request authentication");
	tr->sendAuth();
	return;
    }
    // Authenticated: register/unregister
    bool requestAuth = false, invalidAuth = false;
    if (iplugin.userAuth(tr,!first,requestAuth,invalidAuth)) {
	// Authenticated. Try to (un)register
	if (userreg(tr,event->subclass() == IAXControl::RegRel)) {
	    Debug(this,DebugAll,"processRemoteReg. Authenticated and (un)registered. Ack");
	    tr->sendAccept();
	}
	else {
	    Debug(this,DebugAll,"processRemoteReg. Authenticated but not (un)registered. Reject");
	    tr->sendReject("not registered");
	}
	return;
    }
    // First request: check if we should request auth
    const char* reason = 0;
    if (first && requestAuth) {
	Debug(this,DebugAll,"processRemoteReg. Request authentication");
	tr->sendAuth();
	return;
    }
    else if (invalidAuth)
	 reason = IAXTransaction::s_iax_modInvalidAuth;
    if (!reason)
	reason = "not authenticated";
    Debug(this,DebugAll,"processRemoteReg. Not authenticated ('%s'). Reject",reason);
    tr->sendReject(reason);
}

// Build and dispatch the user.(un)register message
bool YIAXEngine::userreg(IAXTransaction* tr, bool regrel)
{
    Debug(this,DebugAll,"YIAXEngine - userreg. %s username: '%s'",
	regrel ? "Unregistering":"Registering",tr->username().c_str());
    Message msg(regrel ? "user.unregister" : "user.register");
    msg.addParam("username",tr->username());
    msg.addParam("driver","iax");
    if (!regrel) {
	String data = "iax/iax2:";
	data << tr->username() << "@";
	data << tr->remoteAddr().host() << ":" << tr->remoteAddr().port();
	// TODO: support number != username
	data << "/" << tr->username();
	msg.addParam("data",data);
	msg.addParam("expires",String((unsigned int)tr->expire()));
    }
    msg.addParam("ip_host",tr->remoteAddr().host());
    msg.addParam("ip_port",String(tr->remoteAddr().port()));
    return Engine::dispatch(msg);
}

// Handler for outgoing registration messages
bool YIAXRegDataHandler::received(Message& msg)
{
    String tmp(msg.getValue("protocol"));
    if (tmp != "iax")
	return false;
    tmp = msg.getValue("account");
    if (tmp.null())
	return false;
    s_lines.updateLine(msg);
    return true;
}

YIAXDriver::YIAXDriver()
    : Driver("iax","varchans"), m_iaxEngine(0), m_port(4569)
{
    Output("Loaded module YIAX");
}

YIAXDriver::~YIAXDriver()
{
    Output("Unloading module YIAX");
    lock();
    channels().clear();
    s_lines.clear(true);
    unlock();
    delete m_iaxEngine;
}

void YIAXDriver::initialize()
{
    Output("Initializing module YIAX");
    lock();
    // Load configuration
    s_cfg = Engine::configFile("yiaxchan");
    s_cfg.load();
    // Codec capabilities
    m_defaultCodec = 0;
    m_codecs = 0;
    u_int32_t fallback = 0;
    String preferred = s_cfg.getValue("formats","preferred");
    bool def = s_cfg.getBoolValue("formats","default",true);
    for (int i = 0; IAXFormat::audioData[i].token; i++) {
	if (s_cfg.getBoolValue("formats",IAXFormat::audioData[i].token,
	    def && DataTranslator::canConvert(IAXFormat::audioData[i].token))) {
	    XDebug(this,DebugAll,"Adding supported codec %u: '%s'.",
		IAXFormat::audioData[i].value,IAXFormat::audioData[i].token);
	    m_codecs |= IAXFormat::audioData[i].value;
	    fallback = IAXFormat::audioData[i].value;
	    // Set default (desired) codec
	    if (preferred == IAXFormat::audioData[i].token)
		m_defaultCodec = fallback;
	}
    }
    if (!m_codecs)
	Debug(DebugWarn,"YIAXDriver - initialize. No audio format(s) available.");
    // If desired codec is disabled fall back to last in list
    if (!m_defaultCodec)
	m_defaultCodec = fallback;
    // Port and interface
    m_port = s_cfg.getIntValue("general","port",4569);
    String iface = s_cfg.getValue("general","addr");
    bool authReq = s_cfg.getBoolValue("registrar","auth_required",true);
    unlock();
    setup();
    // We need channels to be dropped on shutdown
    installRelay(Halt);
    installRelay(Route);
    // Init IAX engine
    u_int16_t transListCount = 64;
    u_int16_t retransCount = 5;
    u_int16_t retransInterval = 500;
    u_int16_t authTimeout = 30;
    u_int16_t transTimeout = 10;
    u_int16_t maxFullFrameDataLen = 1400;
    u_int32_t trunkSendInterval = 10;
    if (!m_iaxEngine) {
	Engine::install(new YIAXRegDataHandler);
	m_iaxEngine = new YIAXEngine(iface,m_port,transListCount,retransCount,retransInterval,authTimeout,
		transTimeout,maxFullFrameDataLen,trunkSendInterval,authReq);
	m_iaxEngine->debugChain(this);
	int tos = s_cfg.getIntValue("general","tos",dict_tos,0);
	if (tos) {
	    if (!m_iaxEngine->socket().setTOS(tos))
		Debug(this,DebugWarn,"Could not set IP TOS to 0x%02x",tos);
	}
    }
    int readThreadCount = 3;
    int eventThreadCount = 3;
    int trunkingThreadCount = 1;
    m_iaxEngine->start(readThreadCount,eventThreadCount,trunkingThreadCount);
}

// Route calls that use a line owned by this driver
bool YIAXDriver::msgRoute(Message& msg)
{
    String called = msg.getValue("called");
    if (!isE164(called))
	return false;
    String line = msg.getValue("line");
    if (line.null())
	line = msg.getValue("account");
    if (line.null())
	return false;
    if (s_lines.hasLine(line)) {
	msg.setParam("line",line);
	msg.retValue() = prefix() + called;
	return true;
    }
    return false;
}

bool YIAXDriver::msgExecute(Message& msg, String& dest)
{
    if (!msg.userData()) {
	Debug(this,DebugAll,"No data channel for this IAX call!");
	return false;
    }
    SocketAddr addr(AF_INET);
    if (isE164(dest)) {
	// dest is called number. Find line
	String name(msg.getValue("line"));
	bool lineReg;
	if(!s_lines.fillList(name,msg,addr,lineReg)) {
	    Debug(this,DebugNote,"No line ['%s'] for this IAX call!",name.c_str());
	    return false;
	}
	if(!lineReg) {
	    Debug(this,DebugNote,"Line ['%s'] is not registered!",name.c_str());
	    msg.setParam("error","offline");
	    return false;
	}
	msg.setParam("called",dest);
    }
    else {
	// dest should be an URI
	IAXURI uri(dest);
	uri.parse();
	uri.fillList(msg);
	uri.setAddr(addr);
    }
    if (!addr.host().length()) {
	Debug(this,DebugAll,"Missing host name in this IAX call");
	return false;
    }
    IAXTransaction* tr = m_iaxEngine->call(addr,msg);
    if (!tr)
	return false;
    YIAXConnection* conn = new YIAXConnection(m_iaxEngine,tr,&msg);
    tr->setUserData(conn);
    Channel* ch = static_cast<Channel*>(msg.userData());
    if (ch && conn->connect(ch,msg.getValue("reason"))) {
	conn->callConnect(msg);
	msg.setParam("peerid",conn->id());
	msg.setParam("targetid",conn->id());
	// Enable trunking if trunkout parameter is enabled
	if (msg.getBoolValue("trunkout"))
	    m_iaxEngine->enableTrunking(tr);
    }
    else
	tr->setUserData(0);
    conn->deref();
    return true;
}

bool YIAXDriver::received(Message& msg, int id)
{
    if (id == Timer)
	s_lines.evTimer(msg.msgTime());
    else 
	if (id == Halt) {
	    dropAll(msg);
	    channels().clear();
	    s_lines.clear();
	}
    return Driver::received(msg,id);
}

bool YIAXDriver::updateCodecsFromRoute(u_int32_t& codecs, const char* formats)
{
// Extract individual codecs from 'formats'
// Check if IAXFormat contains it
// Before exiting: update 'codecs'
    if (!formats)
	return true;
    u_int32_t codec_formats = 0;
    for (u_int32_t i = 0; formats[i];) {
	// Skip separator(s)
	for (; formats[i] && formats[i] == ','; i++) ;
	// Find first separator
	u_int32_t start = i;
	for (; formats[i] && formats[i] != ','; i++) ;
	// Get format
	if (start != i) {
	    // Get string
	    String tmp(formats + start,i - start);
	    // Get format from IAXFormat::audioData
	    u_int32_t format = 0;
	    for (u_int32_t j = 0; IAXFormat::audioData[j].value; j++)
		if (tmp == IAXFormat::audioData[j].token) {
		    format = IAXFormat::audioData[j].value;
		    break;
		}
	    if (format)
		codec_formats |= format;
	}
    }
    // Set intersection
    codecs &= codec_formats;
    return codecs != 0;
}

bool YIAXDriver::userAuth(IAXTransaction* tr, bool response, bool& requestAuth,
	bool& invalidAuth)
{
    requestAuth = invalidAuth = false;
    // Create and dispatch user.auth
    Message msg("user.auth");
    msg.addParam("protocol","iax");
    msg.addParam("username",tr->username());
    msg.addParam("called",tr->calledNo());
    msg.addParam("caller",tr->callingNo());
    msg.addParam("callername",tr->callingName());
    msg.addParam("ip_host",tr->remoteAddr().host());
    msg.addParam("ip_port",String(tr->remoteAddr().port()));
    if (response) {
	msg.addParam("nonce",tr->challenge());
	msg.addParam("response",tr->authdata());
    }
    if (!Engine::dispatch(msg))
	return false;
    String pwd = msg.retValue();
    // We have a password
    if (pwd) {
	// Not a response: request authentication
	if (!response) {
	    requestAuth = true;
	    return false;
	}
	// Check response
	if (!IAXEngine::isMD5ChallengeCorrect(tr->authdata(),tr->challenge(),pwd)) {
	    invalidAuth = true;
	    return false;
	}
    }
    return true;
}

/**
 * IAXConsumer
 */
YIAXConsumer::YIAXConsumer(YIAXConnection* conn, u_int32_t format, const char* formatText)
    : DataConsumer(formatText), m_connection(conn), m_total(0), m_format(format)
{
}

YIAXConsumer::~YIAXConsumer()
{
}

void YIAXConsumer::Consume(const DataBlock& data, unsigned long tStamp)
{
    if (m_connection && !m_connection->mutedOut()) {
	m_total += data.length();
	if (m_connection->transaction())
	    m_connection->transaction()->sendMedia(data,m_format);
    }
}

/**
 * YIAXSource
 */
YIAXSource::YIAXSource(YIAXConnection* conn, u_int32_t format, const char* formatText) 
    : DataSource(formatText), m_connection(conn), m_total(0), m_format(format)
{ 
}

YIAXSource::~YIAXSource()
{
}

void YIAXSource::Forward(const DataBlock& data, unsigned long tStamp)
{
    if (m_connection && m_connection->mutedIn())
	return;
    m_total += data.length();
    DataSource::Forward(data,tStamp);
}

/**
 * YIAXConnection
 */
YIAXConnection::YIAXConnection(YIAXEngine* iaxEngine, IAXTransaction* transaction, Message* msg)
    : Channel(&iplugin,0,transaction->outgoing()),
      m_iaxEngine(iaxEngine), m_transaction(transaction), m_mutedIn(false), m_mutedOut(false),
      m_hangup(true), m_mutexTrans(true), m_mutexRefIncreased(true), m_refIncreased(false)
{
    DDebug(this,DebugAll,"YIAXConnection::YIAXConnection [%p]",this);
    setMaxcall(msg);
    Message* m = message("chan.startup");
    m->setParam("direction",status());
    if (transaction)
	m_address << transaction->remoteAddr().host() << ":" << transaction->remoteAddr().port();
    if (msg) {
	m_targetid = msg->getValue("id");
	m_password = msg->getValue("password");
	m->setParam("caller",msg->getValue("caller"));
	m->setParam("called",msg->getValue("called"));
	m->setParam("billid",msg->getValue("billid"));
    }
    Engine::enqueue(m);
}

YIAXConnection::~YIAXConnection()
{
    DDebug(this,DebugAll,"YIAXConnection::~YIAXConnection [%p]",this);
    status("destroyed");
    setConsumer();
    setSource();
    hangup();
}

// Incoming call accepted, possibly set trunking on this connection
void YIAXConnection::callAccept(Message& msg)
{
    DDebug(this,DebugAll,"callAccept [%p]",this);
    m_mutexTrans.lock();
    if (m_transaction) {
	m_transaction->sendAccept();
	// Enable trunking if trunkin parameter is enabled
	if (msg.getBoolValue("trunkin"))
	    m_iaxEngine->enableTrunking(m_transaction);
    }
    m_mutexTrans.unlock();
    Channel::callAccept(msg);
}

// Call rejected, check if we have to authenticate caller
void YIAXConnection::callRejected(const char* error, const char* reason, const Message* msg)
{
    Channel::callRejected(error,reason,msg);
    if (!reason)
	reason = m_reason;
    m_reason = error;
    if (!reason)
	reason = error;
    DDebug(this,DebugInfo,"callRejected [%p]. Error: '%s'",this,error);
    String s(error);
    Lock lock(m_mutexTrans);
    if (m_transaction && (s == "noauth") && safeRefIncrease()) {
	Debug(this,DebugAll,"callRejected [%p]. Request authentication",this);
	m_transaction->sendAuth();
	return;
    }
    lock.drop();
    hangup(reason,true);
}

bool YIAXConnection::callRouted(Message& msg)
{
    // check if the caller did abort the call while routing
    if (!m_transaction) {
	Debug(this,DebugMild,"callRouted [%p]. No transaction: ABORT",this);
	return false;
    }
    DDebug(this,DebugAll,"callRouted [%p]",this);
    return true;
}

bool YIAXConnection::msgRinging(Message& msg)
{
    Lock lock(&m_mutexTrans);
    if (m_transaction) {
	m_transaction->sendRinging();
	// only start audio output for early media
	startAudioOut();
	return Channel::msgRinging(msg);
    }
    return false;
}

bool YIAXConnection::msgAnswered(Message& msg)
{
    Lock lock(&m_mutexTrans);
    if (m_transaction) {
	m_transaction->sendAnswer();
	// fully start audio
	startAudioIn();
	startAudioOut();
	return Channel::msgAnswered(msg);
    }
    return false;
}

bool YIAXConnection::msgTone(Message& msg, const char* tone)
{
    Lock lock(&m_mutexTrans);
    if (m_transaction) {
	while (tone && *tone)
	    m_transaction->sendDtmf(*tone++);
	return true;
    }
    return false;
}

bool YIAXConnection::msgText(Message& msg, const char* text)
{
    Lock lock(&m_mutexTrans);
    if (m_transaction) {
	m_transaction->sendText(text);
	return true;
    }
    return false;
}

void YIAXConnection::disconnected(bool final, const char* reason)
{
    DDebug(this,DebugAll,"disconnected [%p]",this);
    Channel::disconnected(final,reason);
    safeDeref();
}

bool YIAXConnection::callPrerouted(Message& msg, bool handled)
{
    // check if the caller did abort the call while prerouting
    if (!m_transaction) {
	Debug(this,DebugMild,"callPrerouted [%p]. No transaction: ABORT",this);
	return false;
    }
    DDebug(this,DebugAll,"callPrerouted [%p]",this);
    return true;
}

void YIAXConnection::handleEvent(IAXEvent* event)
{
    switch(event->type()) {
	case IAXEvent::Text: {
	    Debug(this,DebugAll,"YIAXConnection - TEXT");
	    String text;
	    event->getList().getString(IAXInfoElement::textframe,text);
	    Message* m = message("chan.text");
	    m->addParam("text",text);
            Engine::enqueue(m);
	    }
	    break;
	case IAXEvent::Dtmf: {
	    Debug(this,DebugAll,"YIAXConnection - DTMF: %c",(char)event->subclass());
	    String dtmf((char)event->subclass());
	    dtmf.toUpper();
	    Message* m = message("chan.dtmf");
	    m->addParam("text",dtmf);
	    Engine::enqueue(m);
	    }
	    break;
	case IAXEvent::Noise:
	    Debug(this,DebugAll,"YIAXConnection - NOISE: %u",event->subclass());
	    break;
	case IAXEvent::Progressing:
	    Debug(this,DebugAll,"YIAXConnection - CALL PROGRESSING");
	    break;
	case IAXEvent::Accept:
	    Debug(this,DebugAll,"YIAXConnection - ACCEPT");
	    startAudioIn();
	    break;
	case IAXEvent::Answer:
	    if (isAnswered())
		break;
	    Debug(this,DebugAll,"YIAXConnection - ANSWER");
	    status("answered");
	    startAudioIn();
	    startAudioOut();
	    Engine::enqueue(message("call.answered",false,true));
	    break; 
	case IAXEvent::Quelch:
	    Debug(this,DebugAll,"YIAXConnection - QUELCH");
	    m_mutedOut = true;
	    break;
	case IAXEvent::Unquelch:
	    Debug(this,DebugAll,"YIAXConnection - UNQUELCH");
	    m_mutedOut = false;
	    break;
	case IAXEvent::Ringing:
	    Debug(this,DebugAll,"YIAXConnection - RINGING");
	    startAudioIn();
	    Engine::enqueue(message("call.ringing",false,true));
	    break; 
	case IAXEvent::Hangup:
	case IAXEvent::Reject:
	    event->getList().getString(IAXInfoElement::CAUSE,m_reason);
	    Debug(this,DebugAll,"YIAXConnection - REJECT/HANGUP: '%s'",m_reason.c_str());
	    break;
	case IAXEvent::Timeout:
	    DDebug(this,DebugNote,"YIAXConnection - TIMEOUT. Transaction: %u,%u, Frame: %u,%u ",
		event->getTransaction()->localCallNo(),event->getTransaction()->remoteCallNo(),
		event->frameType(),event->subclass());
	    m_reason = "Timeout";
	    break;
	case IAXEvent::Busy:
	    Debug(this,DebugAll,"YIAXConnection - BUSY");
	    m_reason = "Busy";
	    break;
	case IAXEvent::AuthRep:
	    evAuthRep(event);
	    break;
	default:
	    iplugin.getEngine()->defaultEventHandler(event);
	    if (!m_transaction)
		event->setFinal();
    }
    if (event->final()) {
	safeDeref();
	m_transaction = 0;
    }
}

void YIAXConnection::hangup(const char* reason, bool reject)
{
    if (!m_hangup)
	// Already done
	return;
    m_hangup = false;
    if (!reason)
	reason = m_reason;
    if (!reason)
	reason = Engine::exiting() ? "Server shutdown" : "Unexpected problem";
    m_mutexTrans.lock();
    if (m_transaction) {
	m_transaction->setUserData(0);
	if (reject)
	    m_transaction->sendReject(reason);
	else
	    m_transaction->sendHangup(reason);
        m_transaction = 0;
    }
    m_mutexTrans.unlock();
    Message* m = message("chan.hangup",true);
    m->setParam("status","hangup");
    m->setParam("reason",reason);
    Engine::enqueue(m);
    Debug(this,DebugCall,"YIAXConnection::hangup ('%s') [%p]",reason,this);
}

bool YIAXConnection::route(bool authenticated)
{
    if (!m_transaction)
	return false;
    Message* m = message("call.preroute",false,true);
    Lock lock(&m_mutexTrans);
    if (authenticated) {
	DDebug(this,DebugAll,"Route pass 2: Password accepted.");
	m_refIncreased = false;
	m->addParam("username",m_transaction->username());
    }
    else {
	DDebug(this,DebugAll,"Route pass 1: No username.");
	if (!iplugin.getEngine()->acceptFormatAndCapability(m_transaction)) {
	    hangup(IAXTransaction::s_iax_modNoMediaFormat,true);
	    return false;
	}
	// Advertise the not yet authenticated username
	if (m_transaction->username())
	    m->addParam("authname",m_transaction->username());
	// Set 'formats' parameter
	String formats;
	IAXFormat::formatList(formats,m_transaction->capability(),',');
	m->addParam("formats",formats);
    }
    m->addParam("called",m_transaction->calledNo());
    m->addParam("caller",m_transaction->callingNo());
    m->addParam("callername",m_transaction->callingName());
    m->addParam("ip_host",m_transaction->remoteAddr().host());
    m->addParam("ip_port",String(m_transaction->remoteAddr().port()));
    return startRouter(m);
}

// Create audio source with the proper format
void YIAXConnection::startAudioIn()
{
    if (getSource())
	return;
    u_int32_t format = 0;
    m_mutexTrans.lock();
    if (m_transaction)
	format = m_transaction->formatIn();
    m_mutexTrans.unlock();
    const char* formatText = IAXFormat::audioText(format);
    setSource(new YIAXSource(this,format,formatText));
    getSource()->deref();
    DDebug(this,DebugAll,"startAudioIn. Format %u: '%s'",format,formatText);
}

// Create audio consumer with the proper format
void YIAXConnection::startAudioOut()
{
    if (getConsumer())
	return;
    u_int32_t format = 0;
    m_mutexTrans.lock();
    if (m_transaction)
	format = m_transaction->formatOut();
    m_mutexTrans.unlock();
    const char* formatText = (char*)IAXFormat::audioText(format);
    setConsumer(new YIAXConsumer(this,format,formatText));
    getConsumer()->deref();
    DDebug(this,DebugAll,"startAudioOut. Format %u: '%s'",format,formatText);
}

void YIAXConnection::evAuthRep(IAXEvent* event)
{
    DDebug(this,DebugAll,"YIAXConnection - AUTHREP");
    bool requestAuth, invalidAuth;
    if (iplugin.userAuth(event->getTransaction(),true,requestAuth,invalidAuth)) {
	// Authenticated. Route the user.
	route(true);
	return;
    }
    const char* reason = invalidAuth ? IAXTransaction::s_iax_modInvalidAuth : "not authenticated";
    DDebug(this,DebugAll,"Not authenticated. Reason: '%s'. Reject.",reason);
    hangup(event,reason,true);
}

// Get rid of the extra reference
void YIAXConnection::safeDeref()
{
    m_mutexRefIncreased.lock();
    bool bref = m_refIncreased;
    m_refIncreased = false;
    m_mutexRefIncreased.unlock();
    if (bref)
	deref();
}

// Keep an extra reference to prevent destroying the connection
bool YIAXConnection::safeRefIncrease()
{
    bool ok = false;
    m_mutexRefIncreased.lock();
    if (!m_refIncreased && ref())
	m_refIncreased = ok = true;
    m_mutexRefIncreased.unlock();
    return ok;
}

// IAX URI constructor from components
IAXURI::IAXURI(const char* user, const char* host, const char* calledNo, const char* calledContext, int port)
    : m_username(user),
      m_host(host),
      m_port(port),
      m_calledNo(calledNo),
      m_calledContext(calledContext),
      m_parsed(true)

{
    *this << "iax:";
    if (m_username)
	*this << m_username << "@";
    *this << m_host;
    if (m_port)
	*this << ":" << m_port;
    if (m_calledNo) {
	*this << "/" << m_calledNo;
	if (m_calledContext)
	    *this << "@" << m_calledContext;
    }
}

void IAXURI::parse()
{
/*
    proto: user@ host :port /calledno @context
    proto: user@ host :port /calledno ?context
*/
    if (m_parsed)
	return;
    String tmp(*this), _port;
    Regexp r("^\\([Ii][Aa][Xx]2\\+:\\)\\?\\([^[:space:][:cntrl:]@]\\+@\\)\\?\\([[:alnum:]._-]\\+\\)\\(:[0-9]\\+\\)\\?\\(/[[:alnum:]]*\\)\\?\\([@?][^@?:/]*\\)\\?$");
    if (tmp.matches(r))
    {
	m_username = tmp.matchString(2);
        m_username = m_username.substr(0,m_username.length() -1);
	m_host = tmp.matchString(3).toLower();
	_port = tmp.matchString(4);
        m_port = _port.substr(1,_port.length()).toInteger();
	m_calledNo = tmp.matchString(5);
        m_calledNo = m_calledNo.substr(1,m_calledNo.length());
	m_calledContext = tmp.matchString(6);
        m_calledContext = m_calledContext.substr(1,m_calledContext.length());
    }
    else
    {
	m_username = "";
	m_host = "";
        m_port = 0;
	m_calledNo = "";
	m_calledContext = "";
    }
    m_parsed = true;
}

// Pick URI parameters from a message or setting
bool IAXURI::fillList(NamedList& dest)
{
    if (!m_parsed)
	return false;
    if (m_username.length())
	dest.setParam("username",m_username);
    if (m_calledNo.length())
	dest.setParam("called",m_calledNo);
    if (m_calledContext.length())
	dest.setParam("iaxcontext",m_calledContext);
    return true;
}

bool IAXURI::setAddr(SocketAddr& dest)
{
    parse();
    if (!m_host.length())
	return false;
    dest.host(m_host);
    dest.port(m_port ? m_port : 0);
    return true;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
