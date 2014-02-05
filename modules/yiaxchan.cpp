/**
 * yiaxchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * IAX channel
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 * Author: Marian Podgoreanu
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
#include <yateversn.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>

#include <yateiax.h>

using namespace TelEngine;
namespace { // anonymous

static const TokenDict dict_payloads[] = {
    {"gsm",    IAXFormat::GSM},
    {"ilbc30", IAXFormat::ILBC},
    {"speex",  IAXFormat::SPEEX},
    {"lpc10",  IAXFormat::LPC10},
    {"mulaw",  IAXFormat::ULAW},
    {"alaw",   IAXFormat::ALAW},
    {"g723",   IAXFormat::G723_1},
    {"g729",   IAXFormat::G729},
    {"adpcm",  IAXFormat::ADPCM},
    {"g726",   IAXFormat::G726},
    {"slin",   IAXFormat::SLIN},
    {"g722",   IAXFormat::G722},
    {"amr",    IAXFormat::AMR},
    {"gsmhr",  IAXFormat::GSM_HR},
    {0, 0}
};

static const TokenDict dict_payloads_video[] = {
    {"h261",  IAXFormat::H261},
    {"h263",  IAXFormat::H263},
    {"h263p", IAXFormat::H263p},
    {"h264",  IAXFormat::H264},
    {0, 0}
};

static const TokenDict dict_tos[] = {
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
class YIAXLine : public RefObject, public Mutex
{
    friend class YIAXLineContainer;
public:
    YIAXLine(const String& name);
    virtual ~YIAXLine();
    virtual const String& toString() const
	{ return m_name; }
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
    inline unsigned int expire() const
	{ return m_expire; }
    inline const String& remoteAddr() const
	{ return m_remoteAddr; }
    inline int remotePort() const
	{ return m_remotePort; }
    inline const SocketAddr& remote() const
	{ return m_remote; }
    inline bool callToken() const
	{ return m_callToken; }
    inline bool trunking() const
	{ return m_trunking; }
    inline const NamedList& trunkInfo() const
	{ return m_trunkInfo; }
    inline YIAXEngine* engine() const
	{ return m_engine; }
    inline IAXTransaction* transaction() const
	{ return m_transaction; }

protected:
    virtual void destroyed() {
	    if (m_transaction) {
		m_transaction->setUserData(0);
		m_transaction = 0;
	    }
	    m_engine = 0;
	    RefObject::destroyed();
	}

private:
    void setRegistered(bool registered, const char* reason = 0, const char* error = 0);
    inline void setNextRegFromExpire(unsigned int val1, unsigned int val2)
	{ m_nextReg = Time::now() + 1000000 * (m_expire * (val1 ? val1 : 1) / (val2 ? val2 : 1)); }

    String m_name;
    int m_oper;                         // Requested operation
    String m_username;                  // Username
    String m_password;                  // Password
    String m_callingNo;                 // Calling number
    String m_callingName;               // Calling name
    bool m_callToken;                   // Advertise CALLTOKEN support
    unsigned int m_expire;              // Expire time
    String m_remoteAddr;
    int m_remotePort;
    SocketAddr m_remote;
    u_int64_t m_nextReg;                // Time to next registration
    u_int64_t m_nextKeepAlive;          // Time to next keep alive signal
    unsigned int m_keepAliveInterval;   // Keep alive interval
    bool m_registered;			// Registered flag. If true the line is registered
    RefPointer<IAXTransaction> m_transaction;
    RefPointer<YIAXEngine> m_engine;    // The engine to use
    NamedList m_trunkInfo;              // Trunk info parameters
    bool m_trunking;                    // Enable trunking
    NamedList m_engineToUse;            // Parameters used to identify the engine
};

/*
 * Line container: Add/Delete/Update/Register/Unregister lines
 */
class YIAXLineContainer : public Mutex
{
public:
    enum Operation {
	Unknown = 0,
	Create,
	Login,                           // Login
	Logout,                          // Logout and remain
	Delete,                          // Logout and delete after
    };
    inline YIAXLineContainer()
	: Mutex(true,"YIAXLineContainer"),
	m_lineTransMutex(false,"YIAXLineTransMutex")
	{}
    inline bool busy(const YIAXLine* line) {
	    Lock lck(m_lineTransMutex);
	    return line && 0 != line->transaction();
	}
    // Logout and remove all lines
    // Process only lines using a specified engine if given
    void clear(bool final = false, YIAXEngine* eng = 0);
    // Update a line from a message
    bool updateLine(Message &msg);
    // Re-check lines engine
    void updateEngine();
    // Event handler for a registration.
    void handleEvent(IAXEvent* event);
    // Terminate notification of a Register/Unregister operation
    void regTerminate(IAXEvent* event);
    // Process an authentication request for a Register/Unregister operation
    void regAuthReq(IAXEvent* event);
    // Timer notification
    void evTimer(Time& time);
    // Fill parameters with information taken from line
    void fillList(YIAXLine& line, NamedList& dest, SocketAddr& addr);
    // Find a line by name
    inline bool findLine(RefPointer<YIAXLine>& line, const String& name) {
	    Lock lck(this);
	    line = findLine(name);
	    return line != 0;
	}
    // Check if a line exists
    inline bool hasLine(const String& line)
	{ Lock lock(this); return findLine(line) != 0; }
    // Retrieve the lines list
    inline ObjList& lines()
	{ return m_lines;}

    Mutex m_lineTransMutex;

protected:
    // Set a line's transaction
    void setTransaction(YIAXLine* line, IAXTransaction* tr = 0,
	bool abortReg = true);
    // Change a line String parameter. Unregister it if changed
    inline void changeLine(bool& changed, YIAXLine* line, String& dest, const String& src) {
	    if (!line || dest == src)
		return;
	    unregisterLineNonBusy(line);
	    dest = src;
	    changed = true;
	}
    inline void changeLine(bool& changed, YIAXLine* line, unsigned int& dest, unsigned int src) {
	    if (!line || dest == src)
		return;
	    unregisterLineNonBusy(line);
	    dest = src;
	    changed = true;
	}
    inline void changeLine(bool& changed, YIAXLine* line, int& dest, int src) {
	    if (!line || dest == src)
		return;
	    unregisterLineNonBusy(line);
	    dest = src;
	    changed = true;
	}
    // Unregister a non busy registered line. Return true if unregistering
    inline bool unregisterLineNonBusy(YIAXLine* line) {
	    if (!line || !line->m_registered || busy(line))
		return false;
	    return registerLine(line,false);
	}
    // Find a line by name
    inline YIAXLine* findLine(const String& name) {
	    ObjList* o = m_lines.find(name);
	    return o ? static_cast<YIAXLine*>(o->get()) : 0;
	}
    // Find a line by address, return same if found
    inline YIAXLine* findLine(YIAXLine* line)
	{ return (line && m_lines.find(line)) ? line : 0; }
    // Find line from event. Clear event transaction if line is not found
    // Return referenced pointer
    YIAXLine* findLine(IAXEvent* ev);
    // (Un)register a line
    bool registerLine(YIAXLine* line, bool reg);
private:
    ObjList m_lines;
};


//
// Engine thread
//
class YIAX_API YIAXThread : public Thread, public GenObject
{
public:
    enum Operation {
	Listener,
	GetEvent,
	Trunking
    };
    inline YIAXThread(YIAXEngine* engine, int oper, Priority prio = Normal)
        : Thread(lookup(oper,s_operThName),prio), m_oper(oper), m_engine(engine)
        {}
    ~YIAXThread()
	{ notify(true); }
    inline const char* operThName() const
	{ return lookup(m_oper,s_operThName); }
    virtual void cleanup()
	{ notify(true); }
    virtual void run();
    static const TokenDict s_operThName[];

protected:
    void notify(bool final = false);

    int m_oper;
    YIAXEngine* m_engine;
};

/*
 * The IAX engine for this driver
 */
class YIAXEngine : public IAXEngine, public RefObject
{
    friend class YIAXDriver;
    friend class YIAXThread;
public:
    enum Status {
	Idle,
	Listening,
	Disabled,
	Removed,
	Exiting,
	BindFailed
    };
    YIAXEngine(const String& name, const char* iface, int port, const NamedList* params);
    virtual ~YIAXEngine()
	{}
    inline bool bound() const
	{ return m_status != BindFailed && m_status != Idle; }
    inline bool valid() const
	{ return bound() && !exiting(); }
    inline const char* statusName()
	{ return statusName(m_status); }
    inline void readSocket() {
	    SocketAddr addr;
	    IAXEngine::readSocket(addr);
	}
    inline bool timeout(const Time& now)
	{ return m_timeout && m_timeout < now; }
    void changeStatus(int stat);
    bool bind(const NamedList& params);
    // Schedule for termination
    void setTerminate(int stat);
    // Setup a given transaction from parameters
    void initTransaction(IAXTransaction* tr, const NamedList& params, YIAXLine* line = 0);
    // Process media from remote peer
    virtual void processMedia(IAXTransaction* transaction, DataBlock& data,
	u_int32_t tStamp, int type, bool mark);
    // Initiate (un)register transaction
    inline IAXTransaction* reg(bool regreq, const SocketAddr& addr, IAXIEList& ieList) {
	    if (regreq)
		return startLocalTransaction(IAXTransaction::RegReq,addr,ieList,true,false);
	    return startLocalTransaction(IAXTransaction::RegRel,addr,ieList,true,false);
	}
    // Initiate an aoutgoing call.
    IAXTransaction* call(SocketAddr& addr, NamedList& params);
    // Initiate a test of existence of a remote IAX peer.
    IAXTransaction* poke(SocketAddr& addr);
    // Cancel threads
    void cancelThreads(unsigned int waitTerminateMs = 100);
    // Add engine data to message
    inline void fillMessage(Message& msg, bool addRoute = true) {
	    msg.setParam("connection_id",toString());
	    msg.setParam("connection_reliable",String::boolText(false));
	    if (addRoute) {
		msg.setParam("route_params","oconnection_id");
		msg.setParam("oconnection_id",toString());
	    }
	}
    virtual const String& toString() const
	{ return name(); }
    static inline const char* statusName(int stat)
	{ return lookup(stat,s_statusName); }
    static const TokenDict s_statusName[];

protected:
    virtual void destroyed();
    // Event handler for transaction with a connection
    virtual void processEvent(IAXEvent* event);
    // Process a new format received with a full frame
    virtual bool mediaFormatChanged(IAXTransaction* trans, int type, u_int32_t format);
    // Event handler for incoming registration transactions
    void processRemoteReg(IAXEvent* event,bool first);
    // Send Register/Unregister messages to Engine
    // Return the message on success
    Message* userreg(IAXTransaction* tr, bool regrel = true);
    // Create threads
    void createThreads(const NamedList& params);
    void createThreads(unsigned int count, int oper, Thread::Priority prio);

private:
    void threadTerminated(YIAXThread* th);

    bool m_initialized;
    bool m_default;
    ObjList m_threads;
    u_int64_t m_timeout;                 // Timeout: destroy the engine
    int m_status;                        // Engine status
    String m_bindAddr;                   // Requested bind addr
    int m_bindPort;                      // Requested bind port
};

/*
 * YIAXDriver
 */
class YIAXDriver : public Driver
{
public:
    enum Relay {
	EngineStop = Private
    };
    YIAXDriver();
    virtual ~YIAXDriver();
    virtual void initialize();
    virtual bool hasLine(const String& line) const;
    virtual bool msgExecute(Message& msg, String& dest);
    virtual bool msgRoute(Message& msg);
    virtual bool received(Message& msg, int id);
    // Find a channel. Return a refferenced pointer
    inline Channel* findChan(Channel* chan) {
	    if (!chan)
		return 0;
	    Lock lck(this);
	    return channels().find(chan) && chan->ref() ? chan : 0;
	}
    // Find an engine from parameters. Return a refferenced pointer
    bool findEngine(RefPointer<YIAXEngine>& eng, const NamedList& params,
	const String& prefix = String::empty(), YIAXLine* line = 0,
	NamedList* copyParams = 0);
    // Find an engine by name. Return true if found
    inline bool findEngine(RefPointer<YIAXEngine>& engine, const String& name) {
	    if (!name)
		return false;
	    Lock lck(m_enginesMutex);
	    engine = findEngine(name);
	    return engine != 0;
	}
    // Update codecs a list of parameters
    // Return false if the result is 0 (no intersection)
    bool updateCodecsFromRoute(u_int32_t& codecs, const NamedList& params, int type);
    // Dispatch user.auth
    // tr The IAX transaction
    // response True if it is a response.
    // requestAuth True on exit: the caller should request authentication
    // invalidAuth True on exit: authentication response is incorrect
    // billid The billing ID parameter if available
    // Return false if not authenticated
    bool userAuth(IAXTransaction* tr, bool response, bool& requestAuth,
	bool& invalidAuth, const char* billid = 0);
    // Start/stop engine list changes
    void engineListChanging(bool start);
    // Setup an engine
    void setupEngine(const String& name, bool& valid, bool enable, bool def,
	const NamedList& params, const NamedList* defaults = 0);
    // Terminate exiting engines
    void checkEngineTerminate(const Time& now);

protected:
    virtual bool commandComplete(Message& msg, const String& partLine, const String& partWord);
    virtual void msgStatus(Message& msg);
    virtual void statusParams(String& str);
    void msgStatusAccounts(Message& msg);
    void msgStatusListeners(Message& msg);
    virtual void genUpdate(Message& msg);
    // Update default engine
    void updateDefaultEngine();
    // Terminate an engine. Terminate all if no engine is given
    void terminateEngine(YIAXEngine* eng, bool final, int reason);
    // Initialize formats from config
    void initFormats(const NamedList& params);
    // Find an engine by name
    inline YIAXEngine* findEngine(const String& name) {
	    if (!name)
		return 0;
	    for (ObjList* o = m_engines.skipNull(); o; o = o->skipNext()) {
		YIAXEngine* e = static_cast<YIAXEngine*>(o->get());
		if (e->name() == name)
		    return e;
	    }
	    return 0;
	}
    // Find an engine by local ip/port
    inline YIAXEngine* findEngine(const String& addr, int port) {
	    for (ObjList* o = m_engines.skipNull(); o; o = o->skipNext()) {
		YIAXEngine* e = static_cast<YIAXEngine*>(o->get());
		if (e->addr().host() == addr && e->addr().port() == port)
		    return e;
	    }
	    return 0;
	}

    ObjList m_engines;
    Mutex m_enginesMutex;
    YIAXEngine* m_defaultEngine;         // The default engine
    unsigned int m_haveEngTerminate;     // The number of terminating engines
    u_int32_t m_format;                  // The default media format
    u_int32_t m_formatVideo;             // Default video format
    u_int32_t m_capability;              // The media capability
    unsigned int m_failedAuths;
    bool m_init;
};

/*
 * YIAXRegDataHandler
 */
class YIAXRegDataHandler : public MessageHandler
{
public:
    YIAXRegDataHandler(const char* trackName)
	: MessageHandler("user.login",150,trackName)
	{ }
    virtual bool received(Message &msg);
};

class YIAXConnection;

/*
 * Base class for data consumer/source
 */
class YIAXData
{
public:
    inline YIAXData(YIAXConnection* conn, u_int32_t format, int type)
	: m_connection(conn), m_total(0), m_format(format), m_type(type)
	{}
    inline u_int32_t format()
	{ return m_format; }
protected:
    YIAXConnection* m_connection;
    unsigned m_total;
    u_int32_t m_format; // in IAX coding
    int m_type;
};

/*
 * Connection's data consumer
 */
class YIAXConsumer : public DataConsumer, public YIAXData
{
    YCLASS(YIAXConsumer,DataConsumer)
public:
    YIAXConsumer(YIAXConnection* conn, u_int32_t format, const char* formatText, int type);
    ~YIAXConsumer();
    virtual unsigned long Consume(const DataBlock &data, unsigned long tStamp, unsigned long flags);
};

/*
 * Connection's data source
 */
class YIAXSource : public DataSource, public YIAXData
{
    YCLASS(YIAXSource,DataSource)
public:
    YIAXSource(YIAXConnection* conn, u_int32_t format, const char* formatText, int type);
    ~YIAXSource();
    unsigned long Forward(const DataBlock &data, unsigned long tStamp, unsigned long flags);
};

/*
 * The connection
 */
class YIAXConnection : public Channel
{
    friend class YIAXDriver;
public:
    YIAXConnection(IAXTransaction* transaction, Message* msg = 0, NamedList* params = 0);
    virtual ~YIAXConnection();
    virtual void callAccept(Message& msg);
    virtual void callRejected(const char* error, const char* reason = 0, const Message* msg = 0);
    virtual bool callPrerouted(Message& msg, bool handled);
    virtual bool callRouted(Message& msg);
    virtual bool msgProgress(Message& msg);
    virtual bool msgRinging(Message& msg);
    virtual bool msgAnswered(Message& msg);
    virtual bool msgTone(Message& msg, const char* tone);
    virtual bool msgText(Message& msg, const char* text);
    virtual void disconnected(bool final, const char* reason);
    bool disconnect(const char* reason = 0);
    inline IAXTransaction* transaction() const
        { return m_transaction; }
    inline bool mutedIn() const
	{ return m_mutedIn; }
    inline bool mutedOut() const
	{ return m_mutedOut; }
    void handleEvent(IAXEvent* event);
    bool route(IAXEvent* event, bool authenticated = false);
    // Set reason
    inline void setReason(const char* reason) {
	    Lock lck(m_mutexChan);
	    m_reason = reason;
	}
    // Retrieve the data consumer from IAXFormat media type
    YIAXConsumer* getConsumer(int type);
    // Retrieve the data source from IAXFormat media type
    YIAXSource* getSource(int type);
    // Retrieve the data source from IAXFormat
    DataSource* getSourceMedia(int type);

protected:
    // location: 0: from peer, else: from protocol
    void hangup(int location, const char* error = 0, const char* reason = 0, bool reject = false);
    void resetTransaction(NamedList* params = 0, bool reject = false);
    void startMedia(bool in, int type = IAXFormat::Audio);
    void clearMedia(bool in, int type = IAXFormat::Audio);
    void evAuthRep(IAXEvent* event);
    void evAuthReq(IAXEvent* event);
    // Safe deref the connection if the reference counter was increased during registration
    void safeDeref();
    bool safeRefIncrease();
private:
    RefPointer<IAXTransaction> m_transaction; // IAX transaction
    String m_password;                  // Password for client authentication
    bool m_mutedIn;                     // No remote media accepted
    bool m_mutedOut;                    // No local media accepted
    bool m_audio;                       // Audio endpoint was used
    bool m_video;                       // Video endpoint was used
    String m_reason;                    // Call end reason text
    bool m_hangup;                      // Need to send chan.hangup message
    Mutex m_mutexChan;
    Mutex m_mutexTrans;                 // Safe m_transaction operations
    Mutex m_mutexRefIncreased;          // Safe ref/deref connection
    bool m_refIncreased;                // If true, the reference counter was increased
    unsigned int m_routeCount;          // Incoming: route counter
    RefPointer<DataSource> m_sources[IAXFormat::TypeCount]; // Keep all types of sources
};

//
// An IAX URI parser
//  [iax[2]:][username@]host[:port][/called_number[@called_context]]
//
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
static YIAXLineContainer s_lines;        // Lines
static YIAXDriver iplugin;               // Init the driver
static bool s_callTokenOut = true;       // Send an empty call token on outgoing calls
static unsigned int s_extendEngTerminate = 1000; // Extend engine terminate interval (milliseconds)
static unsigned int s_expires_min = 60;
static unsigned int s_expires_def = 60;
static unsigned int s_expires_max = 3600;
static bool s_authReq = true;
static unsigned int s_engineStop = 0;
static const String s_iaxEngineName = "iaxengine";
// Engine parameters to delete when using defaults
// They can't be inherited
static const String s_delEngParams[] = {"enable","default","addr","port","force_bind",""};

const TokenDict YIAXEngine::s_statusName[] = {
    {"Listening",    Listening},
    {"Disabled",     Disabled},
    {"Removed",      Removed},
    {"Exiting",      Exiting},
    {"BindFailed",   BindFailed},
    {"Idle",         Idle},
    {0,0}
};

const TokenDict YIAXThread::s_operThName[] = {
    {"YIAXListener", Listener},
    {"YIAXGetEvent", GetEvent},
    {"YIAXTrunking", Trunking},
    {0,0}
};

static inline bool isGeneralEngine(const String& name)
{
    return name == s_iaxEngineName;
}

// Retrieve data endpoint name from IAXFormat type
static inline const String& dataEpName(int type)
{
    return IAXFormat::typeNameStr(type);
}

// Retrieve format name from IAXFormat type
static const char* lookupFormat(u_int32_t format, int type)
{
    if (type == IAXFormat::Audio)
	return lookup(format,dict_payloads);
    if (type == IAXFormat::Video)
	return lookup(format,dict_payloads_video);
    return 0;
}

// Retrieve a parameter, find its value in a dictionary
// Return true if a non empty parameter was found
static inline bool getKeyword(const NamedList& list, const String& param,
    const TokenDict* tokens, int& val)
{
    const char* s = list.getValue(param);
    if (s)
	val = lookup(s,tokens);
    return (s != 0);
}

// Retrieve a configuration section
static inline const NamedList& safeSect(Configuration& cfg, const String& name)
{
    NamedList* sect = cfg.getSection(name);
    if (sect)
	return *sect;
    return NamedList::empty();
}

// Clear multiple list parameters
static void clearListParams(NamedList& list, const String* params)
{
    if (!params)
	return;
    for (; *params; params++)
	list.clearParam(*params);
}

// Retrieve bind addr/port parameters
static inline void getBindAddr(const NamedList& params, String& addr, int& port,
    bool* force = 0)
{
    addr = params.getValue(YSTRING("addr"));
    port = IAXEngine::getPort(params);
    if (force)
	*force = params.getBoolValue("force_bind",true);
}


//
// YIAXLine
//
// Create an idle line
YIAXLine::YIAXLine(const String& name)
    : Mutex(true,"IAX:Line"),
    m_name(name),
    m_oper(YIAXLineContainer::Create),
    m_callToken(false),
    m_expire(60), m_remotePort(4569),
    m_nextReg(0), m_nextKeepAlive(0),
    m_keepAliveInterval(0),
    m_registered(false),
    m_transaction(0),
    m_trunkInfo(""),
    m_trunking(false),
    m_engineToUse("")
{
    Debug(&iplugin,DebugAll,"Line(%s) created [%p]",m_name.c_str(),this);
}

YIAXLine::~YIAXLine()
{
    Debug(&iplugin,DebugAll,"Line(%s) destroyed [%p]",m_name.c_str(),this);
}

// Set the registered status, emits user.notify messages if necessary
void YIAXLine::setRegistered(bool registered, const char* reason, const char* error)
{
    if (m_registered && registered)
	return;
    m_registered = registered;
    if (!m_registered) {
	m_nextKeepAlive = 0;
	m_remote.clear();
    }
    Message* m = new Message("user.notify");
    m->addParam("account",toString());
    m->addParam("protocol","iax");
    m->addParam("username",m_username);
    m->addParam("registered",String::boolText(registered));
    m->addParam("reason",reason,false);
    m->addParam("error",error,false);
    Engine::enqueue(m);
}


//
// YIAXLineContainer
//
// Set a line's transaction
void YIAXLineContainer::setTransaction(YIAXLine* line, IAXTransaction* tr, bool abortReg)
{
    if (!line)
	return;
    Lock lck(m_lineTransMutex);
    if (line->transaction() == tr)
	return;
    if (line->transaction()) {
	Debug(&iplugin,DebugAll,"Line(%s) clearing transaction (%p) [%p]",
	    line->toString().c_str(),line->transaction(),line);
	RefPointer<IAXTransaction> trans = line->m_transaction;
	line->m_transaction = 0;
	if (trans)
	    trans->setUserData(0);
	lck.drop();
	if (abortReg && trans)
	    trans->abortReg();
	trans = 0;
    }
    if (!tr)
	return;
    line->m_transaction = tr;
    tr->setUserData(line);
    Debug(&iplugin,DebugAll,
	"Line(%s) set transaction (%p) listener='%s' callno=%u [%p]",
	line->toString().c_str(),tr,tr->getEngine()->name().c_str(),
	tr->localCallNo(),line);
}

// Find and update a line with parameters from message, create if needed
bool YIAXLineContainer::updateLine(Message& msg)
{
    const String& name = msg[YSTRING("account")];
    if (!name)
	return false;
    int oper = Unknown;
    const String& op = msg[YSTRING("operation")];
    if (op == YSTRING("login"))
	oper = Login;
    else if (op == YSTRING("create"))
	oper = Create;
    else if (op == YSTRING("logout"))
	oper = Logout;
    else if (op == YSTRING("delete"))
	oper = Delete;
    else
	return false;
    Lock lck(this);
    Debug(&iplugin,DebugAll,"Updating line '%s' oper=%s",name.c_str(),op.c_str());
    RefPointer<YIAXLine> line = findLine(name);
    bool oldLine = true;
    if (!line) {
	if (oper != Login && oper != Create)
	    return false;
	line = new YIAXLine(name);
	m_lines.append((YIAXLine*)line);
	oldLine = false;
    }
    lck.drop();
    line->lock();
    if (oper == Login || oper == Create) {
	bool changed = (line->m_oper != oper);
	String addr = msg.getValue("server",msg.getValue("domain"));
	int pos = addr.find(":");
	int port = -1;
	if (pos >= 0) {
	    port = addr.substr(pos + 1).toInteger();
	    addr = addr.substr(0,pos);
	}
	else
	    port = IAXEngine::getPort(msg);
	if (port > 0)
	    changeLine(changed,line,line->m_remotePort,port);
	line->m_keepAliveInterval = msg.getIntValue("keepalive",25,0);
	changeLine(changed,line,line->m_remoteAddr,addr);
	changeLine(changed,line,line->m_username,msg.getValue("username"));
	changeLine(changed,line,line->m_password,msg.getValue("password"));
	changeLine(changed,line,line->m_callingNo,msg.getValue("caller"));
	changeLine(changed,line,line->m_callingName,msg.getValue("callername"));
	unsigned int interval = msg.getIntValue("interval",60,60,0xffff);
	changeLine(changed,line,line->m_expire,interval);
	line->m_callToken = msg.getBoolValue("calltoken",s_callTokenOut);
	line->m_trunking = msg.getBoolValue("trunking");
	line->m_trunkInfo.clearParams();
	line->m_trunkInfo.copySubParams(msg,"trunk_");
	line->m_engineToUse.clearParams();
	RefPointer<YIAXEngine> engine;
	iplugin.findEngine(engine,msg,String::empty(),0,&line->m_engineToUse);
	if (engine != line->engine()) {
	    if (line->engine()) {
		// Registering: reset transaction and unregister
		// Unregistering: let the transaction finish
		if (line->registered()) {
		    setTransaction(line);
		    registerLine(line,false);
		}
	    }
	    line->m_engine = engine;
	}
	if (changed || oper == Login)
	    line->m_oper = Login;
	else if (line->m_oper != Login && line->m_oper != Create)
	    line->m_oper = oper;
	if (engine) {
	    if (!busy(line) && (changed || line->m_oper == Login))
		registerLine(line,true);
	}
	else if (line->m_oper == Login)
	    Debug(&iplugin,DebugNote,"No listener to register line '%s'",name.c_str());
    }
    else {
	if (line->m_registered)
	    unregisterLineNonBusy(line);
	line->m_oper = oper;
    }
    line->unlock();
    if (oper == Delete) {
	if (!busy(line))
	    m_lines.remove(line);
    }
    else if (oldLine) {
	// The line might be removed in regTerminate()
	Lock lck(this);
	if (!findLine((YIAXLine*)line) && line->ref())
	    m_lines.append(m_lines.append((YIAXLine*)line));
    }
    line = 0;
    return true;
}

// Re-check lines engine
void YIAXLineContainer::updateEngine()
{
    lock();
    ListIterator iter(m_lines);
    for (GenObject* gen = 0; 0 != (gen = iter.get());) {
	RefPointer<YIAXLine> line = static_cast<YIAXLine*>(gen);
	if (!line)
	    continue;
	unlock();
	line->lock();
	if (!line->engine())
	    iplugin.findEngine(line->m_engine,line->m_engineToUse);
	if (line->engine() && line->m_oper == Login && !line->m_registered &&
	    !line->m_nextReg && !busy(line))
	    registerLine(line,true);
	line->unlock();
	line = 0;
	lock();
    }
    unlock();
}

// Handle registration related transaction terminations
void YIAXLineContainer::regTerminate(IAXEvent* event)
{
    YIAXLine* line = findLine(event);
    DDebug(&iplugin,DebugAll,"YIAXLineContainer::regTerminate() ev=%d line=%p (%s)",
	event ? event->type() : -1,line,line ? line->toString().c_str() : "");
    if (!line)
	return;
    IAXTransaction* tr = event->getTransaction();
    line->lock();
    String reason;
    bool accepted = false;
    bool rejected = false;
    switch (event->type()) {
	case IAXEvent::Accept:
	    accepted = true;
	    break;
	case IAXEvent::Reject:
	    rejected = true;
	    event->getList().getString(IAXInfoElement::CAUSE,reason);
	    break;
	case IAXEvent::Timeout:
	    reason = "timeout";
	    break;
	case IAXEvent::Terminated:
	    reason = "failure";
	    break;
	default:
	    line->unlock();
	    TelEngine::destruct(line);
	    return;
    }
    line->m_remote.clear();
    // Line is going to perform another operation
    if (line->engine()) {
	// Engine changed ?
	if (line->engine() == tr->getEngine()) {
	    setTransaction(line,0,false);
	    bool reg = (tr->type() == IAXTransaction::RegReq);
	    bool notify = true;
	    // Operation changed ?
	    if (line->m_oper == Login) {
		if (reg) {
		    if (accepted) {
			// Honor server registration refresh. Assume default (60) if missing
			unsigned int interval = 60;
			if (event->getList().getNumeric(IAXInfoElement::REFRESH,interval) && !interval)
			    interval = 60;
			if (line->m_expire != interval) {
			    DDebug(&iplugin,DebugNote,
				"Line(%s) changed expire interval from %d to %u [%p]",
				line->toString().c_str(),line->m_expire,interval,line);
			    line->m_expire = interval;
			}
			if (tr->callingNo())
			    line->m_callingNo = tr->callingNo();
			if (tr->callingName())
			    line->m_callingName = tr->callingName();
			// Re-register at 75% of the expire time
			line->setNextRegFromExpire(3,4);
			Lock lck(tr);
			line->m_remote = tr->remoteAddr();
		    }
		    else if (rejected)
			// retry at 25% of the expire time
			line->setNextRegFromExpire(1,4);
		    else
			// retry at 50% of the expire time
			line->setNextRegFromExpire(1,2);
		}
		else {
		    notify  = false;
		    registerLine(line,true);
		}
	    }
	    else if (line->m_oper == Logout || line->m_oper == Delete) {
		if (reg && accepted) {
		    line->m_registered = true;
		    notify = false;
		    registerLine(line,false);
		}
		else
		    line->m_nextReg = 0;
	    }
	    if (notify) {
		const char* what = (line->m_oper == Login) ? "logon" : "logoff";
		const char* tf = (line->m_oper == Login) ? "to" : "from";
		if (accepted)
		    Debug(&iplugin,DebugCall,"IAX line '%s' %s success %s %s:%d",
			line->toString().c_str(),what,tf,line->m_remoteAddr.c_str(),
			line->m_remotePort);
		else
		    Debug(&iplugin,DebugWarn,"IAX line '%s' %s failure %s %s:%d reason=%s",
			line->toString().c_str(),what,tf,line->m_remoteAddr.c_str(),
			line->m_remotePort,reason.safe());
		line->setRegistered(reg && accepted,reason,rejected ? "noauth" : 0);
	    }
	}
	else {
	    bool process = true;
	    // Unregister from the old one if registered
	    if (accepted && tr->type() == IAXTransaction::RegReq) {
		RefPointer<YIAXEngine> tmp = line->engine();
		line->m_engine = static_cast<YIAXEngine*>(tr->getEngine());
		setTransaction(line,0,false);
		process = !(line->engine() && registerLine(line,false));
		line->m_engine = tmp;
		tmp = 0;
	    }
	    // Register to the new one
	    if (process) {
		setTransaction(line,0,false);
		if (line->m_oper == Login)
		    registerLine(line,true);
	    }
	}
    }
    else {
	// Line has no engine
	// Unregister from the old one if registered
	if (accepted && tr->type() == IAXTransaction::RegReq)
	    line->m_engine = static_cast<YIAXEngine*>(tr->getEngine());
	setTransaction(line,0,false);
	if (!(line->engine() && registerLine(line,false)))
	    line->setRegistered(false,reason,rejected ? "noauth" : 0);
	line->m_engine = 0;
	line->m_nextReg = 0;
    }
    bool remove = (line->m_oper == Delete && !busy(line));
    line->unlock();
    if (remove) {
	Lock lock(this);
	m_lines.remove(line);
    }
    TelEngine::destruct(line);
}

// Handle registration related authentication
void YIAXLineContainer::regAuthReq(IAXEvent* event)
{
    YIAXLine* line = findLine(event);
    DDebug(&iplugin,DebugAll,"YIAXLineContainer::regAuthReq() ev=%d line=%p",
	event ? event->type() : -1,line);
    IAXTransaction* tr = event->getTransaction();
    if (!line) {
	if (tr)
	    tr->setDestroy();
	return;
    }
    line->lock();
    String response;
    tr->getEngine()->getMD5FromChallenge(response,tr->challenge(),line->password());
    line->unlock();
    tr->sendAuthReply(response);
    TelEngine::destruct(line);
}

// Handle registration related events
void YIAXLineContainer::handleEvent(IAXEvent* event)
{
    switch (event->type()) {
	case IAXEvent::Accept:
	case IAXEvent::Reject:
	case IAXEvent::Timeout:
	case IAXEvent::Terminated:
	    regTerminate(event);
	    break;
	case IAXEvent::AuthReq:
	    regAuthReq(event);
	    break;
	default:
	    if (event->getTransaction())
		event->getTransaction()->getEngine()->defaultEventHandler(event);
    }
}

// Tick the timer for all lines, send keepalives and reregister
void YIAXLineContainer::evTimer(Time& time)
{
    lock();
    ListIterator iter(m_lines);
    GenObject* gen = 0;
    while (0 != (gen = iter.get())) {
	RefPointer<YIAXLine> line = static_cast<YIAXLine*>(gen);
	if (!line)
	    continue;
	unlock();
	line->lock();
	if (line->m_nextReg && line->m_nextReg <= time)
	    registerLine(line,line->m_oper == Login);
	else if (line->m_registered && line->m_keepAliveInterval &&
	    time > line->m_nextKeepAlive) {
	    if (line->m_nextKeepAlive && line->remote().host() && line->engine() &&
		line->engine()->valid()) {
		DDebug(&iplugin,DebugAll,"Line(%s) sending keep alive to %s:%d [%p]",
		    line->toString().c_str(),line->remote().host().c_str(),
		    line->remote().port(),(YIAXLine*)line);
		line->engine()->keepAlive(line->remote());
	    }
	    line->m_nextKeepAlive = time + line->m_keepAliveInterval * 1000000;
	}
	else if (line->m_oper == Delete && !busy(line))
	    m_lines.remove((YIAXLine*)line);
	line->unlock();
	line = 0;
	lock();
    }
    unlock();
}

// Fill parameters with information taken from line
void YIAXLineContainer::fillList(YIAXLine& line, NamedList& dest, SocketAddr& addr)
{
    line.lock();
    dest.setParam("username",line.username());
    dest.setParam("password",line.password());
    if (line.callingNo())
	dest.setParam("caller",line.callingNo());
    if (line.callingName())
	dest.setParam("callername",line.callingName());
    String a = line.remoteAddr();
    int p = line.remotePort();
    line.unlock();
    addr.host(a);
    addr.port(p);
}

// Find line from event. Clear event transaction if line is not found
// Return referenced pointer
YIAXLine* YIAXLineContainer::findLine(IAXEvent* ev)
{
    if (!ev)
	return 0;
    IAXTransaction* tr = ev->getTransaction();
    if (!tr)
	return 0;
    Lock2 lck(this,&m_lineTransMutex);
    YIAXLine* line = findLine(static_cast<YIAXLine*>(tr->getUserData()));
    bool ok = line && line->m_transaction == tr && line->ref();
    if (!ok)
	tr->setUserData(0);
    lck.drop();
    if (!ok) {
	tr->abortReg();
	line = 0;
    }
    return line;
}

// Initiate line (un)register
bool YIAXLineContainer::registerLine(YIAXLine* line, bool reg)
{
    if (!line)
	return false;
    line->m_nextReg = 0;
    line->m_nextKeepAlive = 0;
    setTransaction(line);
    if (!reg)
	line->m_registered = false;
    if (!(line->engine() && line->engine()->bound()))
	return false;
    // Deny register on exiting engines
    if (reg && line->engine()->exiting()) {
	line->m_engine = 0;
	return false;
    }
    SocketAddr addr(AF_INET);
    addr.host(line->remoteAddr());
    addr.port(line->remotePort());
    Debug(&iplugin,DebugAll,"%s line '%s' listener='%s' server=%s:%d [%p]",
	reg ? "Registering" : "Unregistering",line->toString().c_str(),
	line->engine()->name().c_str(),addr.host().c_str(),addr.port(),line);
    // Create IE list
    IAXIEList ieList;
    ieList.appendString(IAXInfoElement::USERNAME,line->username());
    ieList.appendString(IAXInfoElement::PASSWORD,line->password());
    ieList.appendNumeric(IAXInfoElement::REFRESH,line->expire(),2);
    if (line->callToken())
	ieList.appendBinary(IAXInfoElement::CALLTOKEN,0,0);
    IAXTransaction* tr = line->engine()->reg(reg,addr,ieList);
    if (!tr)
	return false;
    setTransaction(line,tr);
    tr->start();
    TelEngine::destruct(tr);
    return true;
}

// Unregister all lines
void YIAXLineContainer::clear(bool final, YIAXEngine* eng)
{
    if (final && !eng) {
	Lock lock(this);
	m_lines.clear();
	return;
    }
    lock();
    ListIterator iter(m_lines);
    for (GenObject* gen = 0; 0 != (gen = iter.get());) {
	RefPointer<YIAXLine> line = static_cast<YIAXLine*>(gen);
	if (!line)
	    continue;
	unlock();
	line->lock();
	if (!eng || eng == line->engine()) {
	    if (final) {
		setTransaction(line);
		line->m_engine = 0;
		if (line->registered())
		    line->setRegistered(false,"notransport");
	    }
	    else if (line->registered()) {
		setTransaction(line);
		registerLine(line,false);
	    }
	}
	line->unlock();
	line = 0;
	lock();
    }
    unlock();
}


//
// Engine thread
//
void YIAXThread::run()
{
    if (!m_engine)
	return;
    DDebug(m_engine,DebugAll,"%s started [%p]",currentName(),this);
    switch (m_oper) {
	case Listener:
	    m_engine->readSocket();
	    break;
	case GetEvent:
	    m_engine->runGetEvents();
	    break;
	case Trunking:
	    m_engine->runProcessTrunkFrames();
	    break;
	default:
	    DDebug(m_engine,DebugStub,"YIAXThread::run() not defined oper %d",m_oper);
    }
    notify(false);
}

void YIAXThread::notify(bool final)
{
    if (!m_engine)
	return;
    if (!final)
	DDebug(m_engine,DebugAll,"Thread '%s' terminated [%p]",operThName(),this);
    else
	Debug(m_engine,DebugWarn,"Thread '%s' abnormally terminated [%p]",
	   operThName(),this);
    m_engine->threadTerminated(this);
    m_engine = 0;
}


//
// YIAXEngine
//
YIAXEngine::YIAXEngine(const String& name, const char* iface, int port,
    const NamedList* params)
    : IAXEngine(iface,port,0,0,params,name),
    m_initialized(true),
    m_default(false),
    m_timeout(0),
    m_status(Idle),
    m_bindAddr(iface),
    m_bindPort(port)
{
    if (socket().valid())
	changeStatus(Listening);
    else
	changeStatus(BindFailed);
}

void YIAXEngine::changeStatus(int stat)
{
    if (stat == m_status)
	return;
    Debug(this,DebugInfo,"Status changed %s -> %s [%p]",
	statusName(),statusName(stat),this);
    m_status = stat;
}

bool YIAXEngine::bind(const NamedList& params)
{
    bool force = false;
    getBindAddr(params,m_bindAddr,m_bindPort,&force);
    if (IAXEngine::bind(m_bindAddr,m_bindPort,force)) {
	changeStatus(Listening);
	return true;
    }
    changeStatus(BindFailed);
    return false;
}

// Schedule for termination
void YIAXEngine::setTerminate(int stat)
{
    setExiting();
    Lock lck(this);
    changeStatus(stat);
    RefPointer<IAXTrunkInfo> ti;
    trunkInfo(ti);
    unsigned int tout = ti ? overallTout(ti->m_retransInterval,ti->m_retransCount) :
	overallTout();
    ti = 0;
    tout += s_extendEngTerminate;
    m_timeout = Time::now() + tout * 1000;
    Debug(this,DebugAll,"Set terminate timeout=%ums status=%s [%p]",
	tout,statusName(),this);
}

// Setup a given transaction from parameters
void YIAXEngine::initTransaction(IAXTransaction* tr, const NamedList& params, YIAXLine* line)
{
    if (!tr)
	return;
    String prefix = tr->outgoing() ? "trunkout" : "trunkin";
    String pref = prefix + "_";
    IAXTrunkInfo* trunk = 0;
    Lock lckLine(line);
    bool trunkOut = params.getBoolValue(prefix,line && line->trunking());
    if (line && line->trunkInfo().getParam(0)) {
	RefPointer<IAXTrunkInfo> def;
	tr->getEngine()->trunkInfo(def);
	trunk = new IAXTrunkInfo;
	trunk->initTrunking(line->trunkInfo(),String::empty(),def,trunkOut,true);
	trunk->updateTrunking(params,pref,trunkOut,true);
    }
    lckLine.drop();
    tr->getEngine()->initOutDataAdjust(params,tr);
    if (trunkOut) {
	if (trunk)
	    enableTrunking(tr,*trunk);
	else
	    enableTrunking(tr,&params,pref);
    }
    if (trunk)
	initTrunkIn(tr,*trunk);
    else
	initTrunkIn(tr,&params,pref);
    TelEngine::destruct(trunk);
}

// Handle received voice data, forward it to connection's source
void YIAXEngine::processMedia(IAXTransaction* transaction, DataBlock& data,
    u_int32_t tStamp, int type, bool mark)
{
    if (transaction) {
	YIAXConnection* conn = static_cast<YIAXConnection*>(transaction->getUserData());
	if (conn) {
	    DataSource* src = conn->getSourceMedia(type);
	    if (src) {
		unsigned long flags = 0;
		if (mark)
		    flags = DataNode::DataMark;
		src->Forward(data,tStamp,flags);
	    }
	    else
		DDebug(this,DebugAll,"processMedia. No media source [%p]",this);
	}
	else
	    DDebug(this,DebugAll,
		"processMedia. Transaction doesn't have a connection [%p]",this);
    }
    else
	DDebug(this,DebugAll,"processMedia. No transaction [%p]",this);
}

// Create a new call transaction from target address and message params
IAXTransaction* YIAXEngine::call(SocketAddr& addr, NamedList& params)
{
    Debug(this,DebugAll,
	"Outgoing Call: username=%s host=%s:%d called=%s called context=%s [%p]",
	params.getValue("username"),addr.host().c_str(),addr.port(),
	params.getValue("called"),params.getValue("calledname"),this);
    // Set format and capabilities
    u_int32_t audioCaps = capability();
    u_int32_t videoCaps = 0;
    iplugin.updateCodecsFromRoute(audioCaps,params,IAXFormat::Audio);
    if (params.getBoolValue("media_video")) {
	videoCaps = capability();
	iplugin.updateCodecsFromRoute(videoCaps,params,IAXFormat::Video);
    }
    u_int32_t fmtAudio = IAXFormat::pickFormat(audioCaps,format(true));
    u_int32_t fmtVideo = IAXFormat::pickFormat(videoCaps,format(false));
    u_int32_t codecs = audioCaps | videoCaps;
    u_int32_t fmt = fmtAudio | fmtVideo;
    if (!fmt) {
	DDebug(this,DebugNote,"Outgoing call failed: No preffered format [%p]",this);
	params.setParam("error","nomedia");
	return 0;
    }
    // Create IE list
    IAXIEList ieList;
    ieList.appendString(IAXInfoElement::USERNAME,params.getValue("username"));
    ieList.appendString(IAXInfoElement::PASSWORD,params.getValue("password"));
    ieList.appendString(IAXInfoElement::CALLING_NUMBER,params.getValue("caller"));
    int tmp = 0;
    if (getKeyword(params,YSTRING("callernumtype"),IAXInfoElement::s_typeOfNumber,tmp))
	ieList.appendNumeric(IAXInfoElement::CALLINGTON,tmp,1);
    u_int8_t cPres = callingPres();
    if (getKeyword(params,YSTRING("callerpres"),IAXInfoElement::s_presentation,tmp))
	cPres = (cPres & 0x0f) | tmp;
    if (getKeyword(params,YSTRING("callerscreening"),IAXInfoElement::s_screening,tmp))
	cPres = (cPres & 0xf0) | tmp;
    ieList.appendNumeric(IAXInfoElement::CALLINGPRES,cPres,1);
    ieList.appendString(IAXInfoElement::CALLING_NAME,params.getValue("callername"));
    ieList.appendString(IAXInfoElement::CALLED_NUMBER,params.getValue("called"));
    ieList.appendString(IAXInfoElement::CALLED_CONTEXT,params.getValue("iaxcontext"));
    ieList.appendNumeric(IAXInfoElement::FORMAT,fmt,4);
    ieList.appendNumeric(IAXInfoElement::CAPABILITY,codecs,4);
    if (params.getBoolValue("calltoken_out",s_callTokenOut))
	ieList.appendBinary(IAXInfoElement::CALLTOKEN,0,0);
    return startLocalTransaction(IAXTransaction::New,addr,ieList,true,false);
}

// Create a POKE transaction
IAXTransaction* YIAXEngine::poke(SocketAddr& addr)
{
    Debug(this,DebugAll,"Outgoing POKE: Host: %s Port: %d [%p]",
	addr.host().c_str(),addr.port(),this);
    IAXIEList ieList;
    return startLocalTransaction(IAXTransaction::Poke,addr,ieList);
}

// Cancel threads
void YIAXEngine::cancelThreads(unsigned int waitTerminateMs)
{
    Lock lck(this);
    ObjList* o = m_threads.skipNull();
    if (!o)
	return;
    for (; o; o = o->skipNext())
	(static_cast<YIAXThread*>(o->get()))->cancel(false);
    if (!waitTerminateMs)
	return;
    lck.drop();
    unsigned int intervals = waitTerminateMs / Thread::idleMsec();
    if (!intervals)
	intervals++;
    for (; intervals; intervals--) {
	Thread::idle(false);
	if (Thread::check(false))
	    break;
	Lock lck(this);
	if (!m_threads.skipNull())
	    break;
    }
}

void YIAXEngine::destroyed()
{
    cancelThreads();
    Debug(this,DebugAll,"Destroyed [%p]",this);
    RefObject::destroyed();
}

// Process all IAX events
void YIAXEngine::processEvent(IAXEvent* event)
{
    IAXTransaction* tr = event ? event->getTransaction() : 0;
    if (!tr) {
	if (event)
	    delete event;
	return;
    }
    if (tr->type() == IAXTransaction::New) {
	if (tr->getUserData()) {
	    Channel* chan = static_cast<Channel*>(tr->getUserData());
	    YIAXConnection* conn = static_cast<YIAXConnection*>(iplugin.findChan(chan));
	    if (conn) {
		// We already have a channel for this call
		conn->handleEvent(event);
		if (event->final()) {
		    // Final event: disconnect
		    DDebug(this,DebugAll,"processEvent. Disconnecting (%p): '%s' [%p]",
			conn,conn->id().c_str(),this);
		    conn->disconnect();
		}
		TelEngine::destruct(conn);
	    }
	    else {
		Debug(this,DebugNote,
		    "No connection (%p) for transaction (%p) callno=%u [%p]",
		    conn,tr,tr->localCallNo(),this);
		tr->setDestroy();
	    }
	}
	else if (event->type() == IAXEvent::New) {
	    // Incoming request for a new call
	    if (iplugin.canAccept(true) && !exiting()) {
		YIAXConnection* conn = new YIAXConnection(tr);
		conn->initChan();
		tr->setUserData(conn);
		if (!conn->route(event))
		    tr->setUserData(0);
	    }
	    else {
		Debug(&iplugin,DebugWarn,"Refusing new IAX call, full or exiting [%p]",
		    this);
		// Cause code 42: switch congestion
		tr->sendReject(0,42);
	    }
	}
    }
    else if (tr->type() == IAXTransaction::RegReq ||
	tr->type() == IAXTransaction::RegRel) {
	if (tr->outgoing())
	    s_lines.handleEvent(event);
	else if (event->type() == IAXEvent::New || event->type() == IAXEvent::AuthRep)
	    processRemoteReg(event,(event->type() == IAXEvent::New));
    }
    delete event;
}

// Process a new format received with a full frame
bool YIAXEngine::mediaFormatChanged(IAXTransaction* trans, int type, u_int32_t format)
{
    if (!trans)
	return false;
    Debug(this,DebugNote,"Refusing media change for transaction (%p) [%p]",trans,this);
    // TODO: implement
    return false;
}

// Process events for remote users registering to us
void YIAXEngine::processRemoteReg(IAXEvent* event, bool first)
{
    IAXTransaction* tr = event->getTransaction();
    DDebug(this,DebugAll,"processRemoteReg: %s username: '%s' [%p]",
	tr->type() == IAXTransaction::RegReq?"Register":"Unregister",
	tr->username().c_str(),this);
    // Check for automatomatically authentication request if it's the first request
    if (first && s_authReq) {
	DDebug(this,DebugAll,"processRemoteReg. Requesting authentication [%p]",this);
	tr->sendAuth();
	return;
    }
    // Authenticated: register/unregister
    bool requestAuth = false, invalidAuth = false;
    if (iplugin.userAuth(tr,!first,requestAuth,invalidAuth)) {
	// Authenticated. Try to (un)register
	const char* user = tr->username();
	bool regrel = (event->subclass() == IAXControl::RegRel);
	Message* msg = userreg(tr,regrel);
	if (msg) {
	    unsigned int exp = 0;
	    if (!regrel) {
		int expires = msg->getIntValue("expires");
		if (expires < 1)
		    expires = s_expires_def;
		exp = expires;
		Debug(&iplugin,DebugNote,"Registered user '%s' expires in %us [%p]",
		    user,exp,this);
	    }
	    else
		Debug(&iplugin,DebugNote,"Unregistered user '%s' [%p]",user,this);
	    tr->sendAccept(&exp);
	    TelEngine::destruct(msg);
	}
	else {
	    Debug(&iplugin,DebugNote,"%s failed for authenticated user '%s' [%p]",
		regrel ? "Unregister" : "Register",user,this);
	    tr->sendReject("not registered");
	}
	return;
    }
    // First request: check if we should request auth
    if (first && requestAuth) {
	DDebug(this,DebugAll,"processRemoteReg. Requesting authentication [%p]",this);
	tr->sendAuth();
	return;
    }
    const char* reason = 0;
    if (invalidAuth)
	reason = IAXTransaction::s_iax_modInvalidAuth;
    if (!reason)
	reason = "not authenticated";
    Debug(&iplugin,DebugNote,"User '%s' addr=%s:%d not authenticated reason=%s' [%p]",
	tr->username().c_str(),tr->remoteAddr().host().c_str(),
	tr->remoteAddr().port(),reason,this);
    tr->sendReject(reason);
}

// Build and dispatch the user.(un)register message
Message* YIAXEngine::userreg(IAXTransaction* tr, bool regrel)
{
    DDebug(this,DebugAll,"YIAXEngine - userreg. %s username: '%s' [%p]",
	regrel ? "Unregistering":"Registering",tr->username().c_str(),this);
    Message* msg = new Message(regrel ? "user.unregister" : "user.register");
    msg->addParam("username",tr->username());
    msg->addParam("driver","iax");
    if (!regrel) {
	IAXURI uri(tr->username(),tr->remoteAddr().host(),tr->calledNo(),
	    tr->calledContext(),tr->remoteAddr().port());
	String data = "iax/" + uri;
	msg->addParam("data",data);
	unsigned int exp = tr->expire();
	if (!exp)
	    exp = s_expires_def;
	else if (exp < s_expires_min)
	    exp = s_expires_min;
	else if (exp > s_expires_max)
	    exp = s_expires_max;
	msg->addParam("expires",String(exp));
    }
    msg->addParam("ip_host",tr->remoteAddr().host());
    msg->addParam("ip_port",String(tr->remoteAddr().port()));
    fillMessage(*msg);
    if (!Engine::dispatch(*msg))
	TelEngine::destruct(msg);
    return msg;
}

void YIAXEngine::createThreads(const NamedList& params)
{
    unsigned int read = params.getIntValue("read_threads",Engine::clientMode() ? 1 : 3,1);
    unsigned int event = params.getIntValue("event_threads",Engine::clientMode() ? 1 : 3,1);
    unsigned int trunking = params.getIntValue("trunk_threads",1,0);
    Thread::Priority prio = Thread::priority(params.getValue("thread"));
    createThreads(read,YIAXThread::Listener,prio);
    createThreads(event,YIAXThread::GetEvent,Thread::Normal);
    createThreads(trunking,YIAXThread::Trunking,prio);
}

void YIAXEngine::createThreads(unsigned int count, int oper, Thread::Priority prio)
{
    if (!count)
	return;
    Lock lck(this);
    unsigned int n = 0;
    for (unsigned int i = 0; i < count; i++) {
	YIAXThread* th = new YIAXThread(this,oper,prio);
	if (!th->startup())
	    continue;
	m_threads.append(th)->setDelete(false);
	n++;
    }
    if (n && oper == YIAXThread::Trunking)
	m_trunking = -1;
    if (n == count)
	Debug(this,DebugAll,"Created %u '%s' threads [%p]",
	    n,lookup(oper,YIAXThread::s_operThName),this);
    else
	Debug(this,DebugNote,"Created %u/%u '%s' threads [%p]",
	    n,count,lookup(oper,YIAXThread::s_operThName),this);
}

void YIAXEngine::threadTerminated(YIAXThread* th)
{
    if (!th)
	return;
    Lock lck(this);
    if (m_threads.remove(th,false))
	Debug(this,DebugAll,"Thread (%p) '%s' terminated [%p]",th,th->operThName(),this);
}


//
// YIAXRegDataHandler
//
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


//
// YIAXDriver
//
YIAXDriver::YIAXDriver()
    : Driver("iax","varchans"),
    m_enginesMutex(false,"IAXEngineList"),
    m_defaultEngine(0),
    m_haveEngTerminate(0),
    m_format(0),
    m_formatVideo(0),
    m_capability(0),
    m_failedAuths(0),
    m_init(true)
{
    Output("Loaded module YIAX");
}

YIAXDriver::~YIAXDriver()
{
    Output("Unloading module YIAX");
    lock();
    channels().clear();
    unlock();
    s_lines.clear(true);
    m_engines.clear();
}

void YIAXDriver::initialize()
{
    Output("Initializing module YIAX");
    // Load configuration
    Configuration cfg(Engine::configFile("yiaxchan"));
    const NamedList& gen = safeSect(cfg,"general");
    const NamedList& registrar = safeSect(cfg,"registrar");
    s_callTokenOut = gen.getBoolValue("calltoken_out",true);
    maxChans(gen.getIntValue("maxchans",maxChans()));
    s_expires_min = registrar.getIntValue("expires_min",60,1);
    s_expires_max = registrar.getIntValue("expires_max",3600,s_expires_min);
    s_expires_def = registrar.getIntValue("expires_def",60,s_expires_min,s_expires_max);
    s_authReq = registrar.getBoolValue("auth_required",true);
    initFormats(safeSect(cfg,"formats"));
    DDebug(this,DebugAll,
	"Initialized calltoken_out=%s expires_min=%u expires_max=%u expires_def=%u",
	String::boolText(s_callTokenOut),s_expires_min,s_expires_max,s_expires_def);
    if (m_init) {
	setup();
	installRelay(Halt);
	installRelay(Route);
	installRelay(Progress);
	installRelay(Status);
	installRelay(EngineStop,"engine.stop");
	Engine::install(new YIAXRegDataHandler(name()));
	m_init = false;
    }
    engineListChanging(true);
    bool upd = false;
    setupEngine(s_iaxEngineName,upd,true,gen.getBoolValue("default",true),gen);
    unsigned int n = cfg.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* p = cfg.getSection(i);
	if (!p)
	    continue;
	String name = *p;
	if (!name.startSkip("listener ",false))
	    continue;
	name.trimBlanks();
	if (!name)
	    continue;
	if (isGeneralEngine(name)) {
	    Debug(this,DebugConf,"Ignoring section '%s': '%s' is reserved",p->c_str(),name.c_str());
	    continue;
	}
	bool enable = p->getBoolValue("enable",true);
	bool def = p->getBoolValue("default");
	setupEngine(name,upd,enable,def,*p,&gen);
    }
    engineListChanging(false);
    if (upd)
	s_lines.updateEngine();
}

// Check if we have a line
bool YIAXDriver::hasLine(const String& line) const
{
    return line && s_lines.hasLine(line);
}

// Route calls that use a line owned by this driver
bool YIAXDriver::msgRoute(Message& msg)
{
    if (!isE164(msg.getValue("called")))
	return false;
    return Driver::msgRoute(msg);
}

bool YIAXDriver::msgExecute(Message& msg, String& dest)
{
    CallEndpoint* ch = YOBJECT(CallEndpoint,msg.userData());
    if (!ch) {
	Debug(this,DebugAll,"IAX call failed: no endpoint");
	return false;
    }
    NamedList params(msg);
    SocketAddr addr(AF_INET);
    RefPointer<YIAXLine> line;
    if (isE164(dest)) {
	const String& lName = params["line"];
	if (!lName) {
	    Debug(this,DebugNote,"IAX call to '%s' failed: no line",dest.c_str());
	    msg.setParam("error","failure");
	    return false;
	}
	if (!s_lines.findLine(line,lName)) {
	    Debug(this,DebugNote,"IAX call failed: no line '%s'",lName.c_str());
	    msg.setParam("error","offline");
	    return false;
	}
	if (!line->registered()) {
	    Debug(this,DebugNote,"IAX call failed: line '%s' is not registered",
		lName.c_str());
	    msg.setParam("error","offline");
	    return false;
	}
	s_lines.fillList(*line,params,addr);
	params.setParam("called",dest);
    }
    else {
	// dest should be an URI
	IAXURI uri(dest);
	uri.parse();
	uri.fillList(params);
	uri.setAddr(addr);
    }
    if (!addr.host().length()) {
	Debug(this,DebugNote,"IAX call failed: no remote address");
	msg.setParam("error","failure");
	return false;
    }
    RefPointer<YIAXEngine> engine;
    if (!(findEngine(engine,msg,"o",line) && engine->valid())) {
	Debug(this,DebugNote,"IAX call failed: no listener");
	msg.setParam("error","notransport");
	engine = 0;
	return false;
    }
    IAXTransaction* tr = engine->call(addr,params);
    if (!tr) {
	msg.copyParams(params,"error");
	engine = 0;
	return false;
    }
    YIAXConnection* conn = new YIAXConnection(tr,&msg,&params);
    conn->initChan();
    tr->setUserData(conn);
    engine->initTransaction(tr,msg,line);
    tr->start();
    if (conn->connect(ch,msg.getValue("reason"))) {
	conn->callConnect(msg);
	msg.setParam("peerid",conn->id());
	msg.setParam("targetid",conn->id());
    }
    else {
	tr->setUserData(0);
	tr->setDestroy();
    }
    TelEngine::destruct(tr);
    line = 0;
    engine = 0;
    conn->deref();
    return true;
}

bool YIAXDriver::received(Message& msg, int id)
{
    if (id == Timer) {
	s_lines.evTimer(msg.msgTime());
	if (m_haveEngTerminate)
	    checkEngineTerminate(msg.msgTime());
	return Driver::received(msg,id);
    }
    if (id == Status) {
	String target = msg.getValue("module");
	if (target && target.startsWith(name(),true) && !target.startsWith(prefix())) {
	    msgStatus(msg);
	    return false;
	}
	return Driver::received(msg,id);
    }
    if (id == EngineStop) {
	s_engineStop++;
	if (s_engineStop == 1)
	    terminateEngine(0,false,YIAXEngine::Exiting);
	if (m_haveEngTerminate)
	    checkEngineTerminate(msg.msgTime());
	if (m_haveEngTerminate)
	    Debug(this,DebugAll,"Returning true from '%s' handler",msg.c_str());
	return m_haveEngTerminate != 0;
    }
    if (id == Halt) {
	dropAll(msg);
	lock();
	channels().clear();
	unlock();
	s_lines.clear(true);
	terminateEngine(0,true,YIAXEngine::Exiting);
	return Driver::received(msg,id);
    }
    return Driver::received(msg,id);
}

// Find an engine from parameters. Return a refferenced pointer
bool YIAXDriver::findEngine(RefPointer<YIAXEngine>& eng, const NamedList& params,
    const String& prefix, YIAXLine* line, NamedList* copyParams)
{
    if (copyParams) {
	if (prefix)
	    copyParams->copySubParams(params,prefix);
	else
	    copyParams->copyParams(params,
		"connection_id,ip_transport_localip,ip_transport_localport");
    }
    Lock lck(m_enginesMutex);
    const String& engName = params[prefix + "connection_id"];
    if (engName) {
	eng = findEngine(engName);
	if (eng)
	    return true;
    }
    String addr = params[prefix + "ip_transport_localip"];
    addr.trimBlanks();
    if (addr) {
	int port = IAXEngine::getPort(params,prefix + "ip_transport_localport");
	eng = findEngine(addr,port);
	if (eng)
	    return true;
    }
    // Done if engine name and/or local ip were given
    if (engName || addr)
	return false;
    // Check for line engine
    if (line) {
	Lock lck(line);
	eng = line->engine();
	if (eng)
	    return true;
    }
    eng = m_defaultEngine;
    return eng != 0;
}

// Extract individual codecs from 'formats'
// Check if IAXFormat contains it
// Before exiting: update 'codecs'
bool YIAXDriver::updateCodecsFromRoute(u_int32_t& codecs, const NamedList& params, int type)
{
    String* formats = 0;
    if (type == IAXFormat::Audio)
	formats = params.getParam("formats");
    if (TelEngine::null(formats)) {
	String tmp("formats_");
	tmp << dataEpName(type);
	formats = params.getParam(tmp);
    }
    XDebug(this,DebugAll,"updateCodecsFromRoute(%u,%d) formats=%s",
	codecs,type,TelEngine::c_safe(formats));
    if (formats) {
	// Set intersection
	if (type == IAXFormat::Audio)
	    codecs &= IAXFormat::encode(*formats,dict_payloads);
	else if (type == IAXFormat::Video)
	    codecs &= IAXFormat::encode(*formats,dict_payloads_video);
	else
	    codecs = 0;
    }
    else {
	// Reset formats for non audio
	// They must be explicitely set
	if (type != IAXFormat::Audio)
	    codecs = 0;
    }
    return codecs != 0;
}

bool YIAXDriver::userAuth(IAXTransaction* tr, bool response, bool& requestAuth,
	bool& invalidAuth, const char* billid)
{
    bool newCall = (tr->type() == IAXTransaction::New);
    DDebug(this,DebugAll,"YIAXDriver::userAuth(%p,%u) newcall=%u",tr,response,newCall);
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
    msg.addParam("billid",billid,false);
    msg.addParam("newcall",String::boolText(newCall));
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
	    m_failedAuths++;
	    changed();
	    Message* fail = new Message(msg);
	    *fail = "user.authfail";
	    fail->retValue().clear();
	    Engine::enqueue(fail);
	    return false;
	}
    }
    return true;
}

// Start stop engine list changes
void YIAXDriver::engineListChanging(bool start)
{
    m_enginesMutex.lock();
    if (start)
	for (ObjList* o = m_engines.skipNull(); o; o = o->skipNext())
	    (static_cast<YIAXEngine*>(o->get()))->m_initialized = false;
    else {
	ListIterator iter(m_engines);
	for (GenObject* gen = 0; 0 != (gen = iter.get());) {
	    YIAXEngine* engine = static_cast<YIAXEngine*>(gen);
	    if (engine->m_initialized)
		continue;
	    RefPointer<YIAXEngine> eng = engine;
	    if (!eng)
		continue;
	    m_enginesMutex.unlock();
	    terminateEngine(eng,false,YIAXEngine::Removed);
	    eng = 0;
	    m_enginesMutex.lock();
	}
	updateDefaultEngine();
    }
    m_enginesMutex.unlock();
}

// Setup an engine
void YIAXDriver::setupEngine(const String& name, bool& valid, bool enable, bool def,
    const NamedList& params, const NamedList* defaults)
{
    if (!name)
	return;
    Lock lck(m_enginesMutex);
    NamedList p("");
    const NamedList* init = &params;
    if (enable && defaults) {
	p.copyParams(*defaults);
	if (!isGeneralEngine(name))
	    clearListParams(p,s_delEngParams);
	p.copyParams(params);
	init = &p;
    }
#ifdef XDEBUG
    String s;
    init->dump(s,"\r\n");
    Debug(this,DebugAll,"setupEngine(%s,%u,%u)%s",name.c_str(),enable,def,s.safe());
#endif
    YIAXEngine* eng = findEngine(name);
    bool bound = false;
    if (eng) {
	eng->m_initialized = true;
	eng->m_default = def;
	if (!enable) {
	    if (!eng->exiting() && eng->ref()) {
		lck.drop();
		terminateEngine(eng,false,YIAXEngine::Disabled);
		eng->deref();
	    }
	    return;
	}
	if (eng->exiting()) {
	    Debug(this,DebugConf,
		"Ignoring init for listener '%s': scheduled for termination",name.c_str());
	    return;
	}
	if (!eng->bound()) {
	    bound = eng->bind(params);
	    if (!bound)
		return;
	}
	eng->initialize(*init);
    }
    else if (enable) {
	String addr;
	int port = 0;
	getBindAddr(params,addr,port);
	// Create the engine
	eng = new YIAXEngine(name,addr,port,init);
	eng->debugChain(this);
	eng->m_default = def;
	bound = eng->bound();
	m_engines.append(eng);
	Debug(this,DebugInfo,"Added listener (%p) '%s' status='%s'",
	    eng,eng->name().c_str(),eng->statusName());
    }
    else
	return;
    if (bound) {
	int tos = init->getIntValue("tos",dict_tos,0);
	if (tos && !eng->socket().setTOS(tos))
	    Debug(eng,DebugWarn,"Could not set IP TOS to 0x%02x",tos);
	eng->createThreads(*init);
	valid = true;
    }
    // Init engine parameters from module
    eng->setFormats(m_capability,m_format,m_formatVideo);
}

// Terminate exiting engines
void YIAXDriver::checkEngineTerminate(const Time& now)
{
    m_enginesMutex.lock();
    if (!m_haveEngTerminate) {
	m_enginesMutex.unlock();
	return;
    }
    ListIterator iter(m_engines);
    for (GenObject* gen = 0; 0 != (gen = iter.get());) {
	YIAXEngine* engine = static_cast<YIAXEngine*>(gen);
	if (!engine->exiting())
	    continue;
	RefPointer<YIAXEngine> eng = engine;
	if (!eng)
	    continue;
	m_enginesMutex.unlock();
	if (eng->timeout(now) || !eng->haveTransactions())
	    terminateEngine(eng,true,eng->m_status);
	eng = 0;
	m_enginesMutex.lock();
    }
    m_enginesMutex.unlock();
}

bool YIAXDriver::commandComplete(Message& msg, const String& partLine, const String& partWord)
{
    if (partLine == ("status " + name()) ||
	partLine == ("status overview " + name())) {
	itemComplete(msg.retValue(),"accounts",partWord);
	itemComplete(msg.retValue(),"listeners",partWord);
    }
    return Driver::commandComplete(msg,partLine,partWord);
}

void YIAXDriver::msgStatus(Message& msg)
{
    String str = msg.getValue("module");
    while (str.startSkip(name())) {
	str.trimBlanks();
	if (str.null())
	    break;
	if (str.startSkip("accounts")) {
	    msgStatusAccounts(msg);
	    return;
	}
	if (str.startSkip("listeners")) {
	    msgStatusListeners(msg);
	    return;
	}
    }
    Driver::msgStatus(msg);
}

void YIAXDriver::statusParams(String& str)
{
    Driver::statusParams(str);
    m_enginesMutex.lock();
    unsigned int cnt = 0;
    ListIterator iter(m_engines);
    while (YIAXEngine* eng = static_cast<YIAXEngine*>(iter.get()))
	cnt += eng->transactionCount();
    m_enginesMutex.unlock();
    str.append("transactions=",",") << cnt;
}

void YIAXDriver::msgStatusAccounts(Message& msg)
{
    msg.retValue().clear();
    msg.retValue() << "module=" << name();
    msg.retValue() << ",protocol=IAX";
    msg.retValue() << ",format=Username|Status;";
    unsigned int n = 0;
    String det;
    bool details = msg.getBoolValue("details",true);
    s_lines.lock();
    for (ObjList* o = s_lines.lines().skipNull(); o; o = o->skipNext()) {
	n++;
	if (!details)
	    continue;
	YIAXLine* line = static_cast<YIAXLine*>(o->get());
	Lock lckLine(line);
	det.append(line->toString(),",") << "=";
	det.append(line->username()) << "|";
	det << (line->registered() ? "online" : "offline");
    }
    s_lines.unlock();
    msg.retValue() << "accounts=" << n;
    msg.retValue().append(det,";");
    msg.retValue() << "\r\n";
}

void YIAXDriver::msgStatusListeners(Message& msg)
{
    msg.retValue().clear();
    msg.retValue() << "module=" << name();
    msg.retValue() << ",protocol=IAX";
    msg.retValue() << ",format=Address|Status|Transactions;";
    unsigned int n = 0;
    String det;
    bool details = msg.getBoolValue("details",true);
    String def;
    m_enginesMutex.lock();
    ListIterator iter(m_engines);
    for (GenObject* gen = 0; 0 != (gen = iter.get());) {
	RefPointer<YIAXEngine> e = static_cast<YIAXEngine*>(gen);
	if (!e)
	    continue;
	n++;
	if (e == m_defaultEngine)
	    def = e->name();
	if (!details)
	    continue;
	det.append(e->toString(),",") << "=";
	if (e->bound())
	    det << e->addr().host() << ":" << e->addr().port();
	else
	    det << e->m_bindAddr << ":" << e->m_bindPort;
	det << "|" << e->statusName();
	m_enginesMutex.unlock();
	det << "|" << e->transactionCount();
	m_enginesMutex.lock();
    }
    m_enginesMutex.unlock();
    msg.retValue() << "listeners=" << n << ",default=" << def;
    msg.retValue().append(det,";");
    msg.retValue() << "\r\n";
}

// Add specific module update parameters
void YIAXDriver::genUpdate(Message& msg)
{
    unsigned int tmp = m_failedAuths;
    m_failedAuths = 0;
    msg.setParam("failed_auths",String(tmp));
}

// Update default engine
void YIAXDriver::updateDefaultEngine()
{
    YIAXEngine* def = 0;
    for (ObjList* o = m_engines.skipNull(); o; o = o->skipNext()) {
	YIAXEngine* eng = static_cast<YIAXEngine*>(o->get());
	if (!eng->valid())
	    continue;
	if (eng->m_default && !def) {
	    def = eng;
	    break;
	}
    }
    m_defaultEngine = def;
    if (s_engineStop)
	return;
    if (m_defaultEngine)
	Debug(this,DebugInfo,"Default listener is '%s'",def->name().c_str());
    else
	Debug(this,DebugNote,"No default listener set");
}

// Terminate an engine
void YIAXDriver::terminateEngine(YIAXEngine* eng, bool final, int reason)
{
    XDebug(this,DebugAll,"terminateEngine(%p,%u,%d)",eng,final,reason);
    if (!eng) {
	m_enginesMutex.lock();
	ListIterator iter(m_engines);
	for (GenObject* gen = 0; 0 != (gen = iter.get());) {
	    RefPointer<YIAXEngine> e = static_cast<YIAXEngine*>(gen);
	    if (!e)
		continue;
	    m_enginesMutex.unlock();
	    terminateEngine(e,final,reason);
	    e = 0;
	    m_enginesMutex.lock();
	}
	m_enginesMutex.unlock();
	return;
    }
    // Already set to terminate ?
    if (!final && eng->exiting())
	return;
    if (!eng->exiting()) {
	eng->setTerminate(reason);
	// Unregister all users registered using this engine
	Message* m = new Message("user.unregister");
	m->addParam("module",name());
	m->addParam("connection_id",eng->toString());
	Engine::enqueue(m);
    }
    // Drop all calls using the engine
    lock();
    ListIterator iter(channels());
    for (GenObject* gen = 0; 0 != (gen = iter.get());) {
	RefPointer<YIAXConnection> c = static_cast<YIAXConnection*>(gen);
	if (!c)
	    continue;
	unlock();
	c->m_mutexTrans.lock();
	if (c->m_transaction && c->m_transaction->getEngine() == eng) {
	    c->disconnect("notransport");
	    if (final)
		c->resetTransaction();
	}
	c->m_mutexTrans.unlock();
	c = 0;
	lock();
    }
    unlock();
    // Unregister all lines using the engine
    s_lines.clear(final,eng);
    Lock lck(m_enginesMutex);
    // Remove the engine from list
    if (final && m_engines.remove(eng,false)) {
	Debug(this,DebugInfo,"Removed listener (%p) '%s' status='%s'",
	    eng,eng->name().c_str(),eng->statusName());
	if (eng == m_defaultEngine)
	    updateDefaultEngine();
	TelEngine::destruct(eng);
    }
    // Update engine terminate count
    unsigned int n = 0;
    for (ObjList* o = m_engines.skipNull(); o; o = o->skipNext())
	if ((static_cast<YIAXEngine*>(o->get()))->exiting())
	    n++;
    m_haveEngTerminate = n;
}

// Initialize formats from config
void YIAXDriver::initFormats(const NamedList& params)
{
    Lock lck(m_enginesMutex);
    u_int32_t codecsAudio = 0;
    u_int32_t codecsVideo = 0;
    u_int32_t defAudio = 0;
    u_int32_t defVideo = 0;
    bool def = params.getBoolValue("default",true);
    const TokenDict* dicts[2] = {dict_payloads,dict_payloads_video};
    for (int i = 0; i < 2; i++) {
	u_int32_t fallback = 0;
	bool audio = (i == 0);
	const char* media = audio ? "audio" : "video";
	const String& preferred = params[audio ? "preferred" : "preferred_video"];
	u_int32_t& codecs = audio ? codecsAudio : codecsVideo;
	u_int32_t& defaultCodec = audio ? defAudio : defVideo;
	for (const TokenDict* d = dicts[i]; d->token; d++) {
	    bool defVal = def && DataTranslator::canConvert(d->token);
	    if (!params.getBoolValue(d->token,defVal))
		continue;
	    XDebug(this,DebugAll,"Adding supported %s codec %u: '%s'.",
		media,d->value,d->token);
	    codecs |= d->value;
	    fallback = d->value;
	    // Set default (desired) codec
	    if (!defaultCodec && preferred == d->token)
		defaultCodec = d->value;
	}
	// If desired codec is disabled fall back to last in list
	if (!defaultCodec)
	    defaultCodec = fallback;
	if (codecs) {
	    String tmp;
	    IAXFormat::formatList(tmp,codecs,dicts[i]);
	    Debug(this,DebugAll,"Enabled %s format(s) '%s' default=%s",
		media,tmp.c_str(),lookup(defaultCodec,dicts[i],""));
	}
	else
	    Debug(this,audio ? DebugWarn : DebugAll,"No %s format(s) available",media);
    }
    m_format = defAudio;
    m_formatVideo = defVideo;
    m_capability = codecsAudio | codecsVideo;
}


//
// IAXConsumer
//
YIAXConsumer::YIAXConsumer(YIAXConnection* conn, u_int32_t format,
    const char* formatText, int type)
    : DataConsumer(formatText),
    YIAXData(conn,format,type)
{
    Debug(m_connection,DebugAll,"YIAXConsumer '%s' format=%s [%p]",
	IAXFormat::typeName(m_type),formatText,this);
}

YIAXConsumer::~YIAXConsumer()
{
    Debug(m_connection,DebugAll,"YIAXConsumer '%s' destroyed total=%u [%p]",
	IAXFormat::typeName(m_type),m_total,this);
}

unsigned long YIAXConsumer::Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
{
    unsigned long sent = 0;
    if (m_connection && !m_connection->mutedOut()) {
	m_total += data.length();
	if (m_connection->transaction()) {
	    bool mark = (flags & DataMark) != 0;
	    sent = m_connection->transaction()->sendMedia(data,tStamp,format(),m_type,mark);
	}
    }
    return sent;
}

//
// YIAXSource
//
YIAXSource::YIAXSource(YIAXConnection* conn, u_int32_t format, const char* formatText, int type)
    : DataSource(formatText),
    YIAXData(conn,format,type)
{
    Debug(m_connection,DebugAll,"YIAXSource '%s' format=%s [%p]",
	IAXFormat::typeName(m_type),formatText,this);
}

YIAXSource::~YIAXSource()
{
    Debug(m_connection,DebugAll,"YIAXSource '%s' destroyed total=%u [%p]",
	IAXFormat::typeName(m_type),m_total,this);
}

unsigned long YIAXSource::Forward(const DataBlock& data, unsigned long tStamp, unsigned long flags)
{
    if (m_connection && m_connection->mutedIn())
	return invalidStamp();
    m_total += data.length();
    return DataSource::Forward(data,tStamp,flags);
}

//
// YIAXConnection
//
YIAXConnection::YIAXConnection(IAXTransaction* tr, Message* msg, NamedList* params)
    : Channel(&iplugin,0,tr->outgoing()),
      m_transaction(tr), m_mutedIn(false), m_mutedOut(false),
      m_audio(false), m_video(false),
      m_hangup(true),
      m_mutexChan(true,"YIAXConnection"),
      m_mutexTrans(true,"YIAXConnection::trans"),
      m_mutexRefIncreased(true,"YIAXConnection::refIncreased"),
      m_refIncreased(false),
      m_routeCount(0)
{
    Debug(this,DebugAll,"%s call. Transaction (%p) callno=%u [%p]",
	isOutgoing() ? "Outgoing" : "Incoming",tr,tr->localCallNo(),this);
    setMaxcall(msg);
    if (msg)
	setMaxPDD(*msg);
    if (tr)
	m_address << tr->remoteAddr().host() << ":" << tr->remoteAddr().port();
    if (msg)
	m_targetid = msg->getValue("id");
    Message* m = message("chan.startup",msg);
    m->addParam("username",tr->username(),false);
    if (params) {
	// outgoing call
	m_password = params->getValue("password");
	m->copyParams(*params,"caller,callername,called,billid,callto,username");
    }
    else {
	// incoming call
	m->addParam("called",tr->calledNo(),false);
	m->addParam("caller",tr->callingNo(),false);
	m->addParam("callername",tr->callingName(),false);
    }
    Engine::enqueue(m);
}

YIAXConnection::~YIAXConnection()
{
    status("destroyed");
    setConsumer();
    setSource();
    hangup(0);
    m_transaction = 0;
    Debug(this,DebugAll,"Destroyed with reason '%s' [%p]",m_reason.safe(),this);
}

// Incoming call accepted, possibly set trunking on this connection
void YIAXConnection::callAccept(Message& msg)
{
    DDebug(this,DebugCall,"callAccept [%p]",this);
    m_mutexTrans.lock();
    if (m_transaction) {
	YIAXEngine* eng = static_cast<YIAXEngine*>(m_transaction->getEngine());
	u_int32_t codecs = eng->capability();
	if (msg.getValue("formats")) {
	    u_int32_t ca = IAXFormat::mask(codecs,IAXFormat::Audio);
	    iplugin.updateCodecsFromRoute(ca,msg,IAXFormat::Audio);
	    eng->acceptFormatAndCapability(m_transaction,&ca);
	}
	u_int32_t cv = IAXFormat::mask(codecs,IAXFormat::Video);
	iplugin.updateCodecsFromRoute(cv,msg,IAXFormat::Video);
	eng->acceptFormatAndCapability(m_transaction,&cv,IAXFormat::Video);
	u_int32_t ci = IAXFormat::mask(codecs,IAXFormat::Image);
	iplugin.updateCodecsFromRoute(ci,msg,IAXFormat::Image);
	eng->acceptFormatAndCapability(m_transaction,&ci,IAXFormat::Image);
	eng->initTransaction(m_transaction,msg);
	m_transaction->sendAccept();
    }
    m_mutexTrans.unlock();
    Channel::callAccept(msg);
}

// Call rejected, check if we have to authenticate caller
void YIAXConnection::callRejected(const char* error, const char* reason, const Message* msg)
{
    if (!error)
	error = reason;
    Lock lock(m_mutexTrans);
    if (m_routeCount == 1 && m_transaction && error && String(error) == "noauth") {
	if (safeRefIncrease()) {
	    Debug(this,DebugAll,"Requesting authentication [%p]",this);
	    m_transaction->sendAuth();
	    return;
	}
	error = "temporary-failure";
    }
    lock.drop();
    Channel::callRejected(error,reason,msg);
    hangup(0,error,reason,true);
}

bool YIAXConnection::callRouted(Message& msg)
{
    // check if the caller did abort the call while routing
    if (!m_transaction) {
	Debug(this,DebugMild,"callRouted. No transaction: ABORT [%p]",this);
	return false;
    }
    DDebug(this,DebugAll,"callRouted [%p]",this);
    return true;
}

bool YIAXConnection::msgProgress(Message& msg)
{
    Lock lock(&m_mutexTrans);
    if (m_transaction) {
	m_transaction->sendProgress();
	// only start audio output for early media
	startMedia(false);
	return Channel::msgProgress(msg);
    }
    return false;
}

bool YIAXConnection::msgRinging(Message& msg)
{
    Lock lock(&m_mutexTrans);
    if (m_transaction) {
	m_transaction->sendRinging();
	// only start audio output for early media
	startMedia(false);
	return Channel::msgRinging(msg);
    }
    return false;
}

bool YIAXConnection::msgAnswered(Message& msg)
{
    Lock lock(&m_mutexTrans);
    if (m_transaction) {
	m_transaction->sendAnswer();
	// fully start media
	startMedia(true);
	startMedia(false);
	startMedia(true,IAXFormat::Video);
	startMedia(false,IAXFormat::Video);
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
    DDebug(this,DebugAll,"Disconnected. Final: %s . Reason: '%s' [%p]",
	String::boolText(final),reason,this);
    hangup(0,reason);
    Channel::disconnected(final,reason);
    safeDeref();
}

bool YIAXConnection::disconnect(const char* reason)
{
    Lock lck(m_mutexChan);
    if (!m_reason)
	m_reason = reason;
    String res = m_reason;
    lck.drop();
    return Channel::disconnect(res,parameters());
}

bool YIAXConnection::callPrerouted(Message& msg, bool handled)
{
    // check if the caller did abort the call while prerouting
    if (!m_transaction) {
	Debug(this,DebugMild,"callPrerouted. No transaction: ABORT [%p]",this);
	return false;
    }
    DDebug(this,DebugAll,"callPrerouted [%p]",this);
    return true;
}

void YIAXConnection::handleEvent(IAXEvent* event)
{
    switch(event->type()) {
	case IAXEvent::Text: {
	    String text;
	    event->getList().getString(IAXInfoElement::textframe,text);
	    DDebug(this,DebugInfo,"TEXT: '%s' [%p]",text.safe(),this);
	    Message* m = message("chan.text");
	    m->addParam("text",text);
            Engine::enqueue(m);
	    }
	    break;
	case IAXEvent::Dtmf: {
	    String dtmf((char)event->subclass());
	    dtmf.toUpper();
	    DDebug(this,DebugCall,"DTMF: %s [%p]",dtmf.safe(),this);
	    Message* m = message("chan.dtmf");
	    m->addParam("text",dtmf);
	    m->addParam("detected","iax-event");
	    dtmfEnqueue(m);
	    }
	    break;
	case IAXEvent::Noise:
	    DDebug(this,DebugInfo,"NOISE: %u [%p]",event->subclass(),this);
	    break;
	case IAXEvent::Progressing:
	    DDebug(this,DebugInfo,"CALL PROGRESSING [%p]",this);
	    Engine::enqueue(message("call.progress",false,true));
	    break;
	case IAXEvent::Accept:
	    DDebug(this,DebugCall,"ACCEPT [%p]",this);
	    startMedia(true);
	    break;
	case IAXEvent::Answer:
	    if (isAnswered())
		break;
	    DDebug(this,DebugCall,"ANSWER [%p]",this);
	    status("answered");
	    startMedia(true);
	    startMedia(false);
	    startMedia(true,IAXFormat::Video);
	    startMedia(false,IAXFormat::Video);
	    Engine::enqueue(message("call.answered",false,true));
	    break;
	case IAXEvent::Quelch:
	    DDebug(this,DebugCall,"QUELCH [%p]",this);
	    m_mutedOut = true;
	    break;
	case IAXEvent::Unquelch:
	    DDebug(this,DebugCall,"UNQUELCH [%p]",this);
	    m_mutedOut = false;
	    break;
	case IAXEvent::Ringing:
	    DDebug(this,DebugInfo,"RINGING [%p]",this);
	    startMedia(true);
	    Engine::enqueue(message("call.ringing",false,true));
	    break;
	case IAXEvent::Hangup:
	case IAXEvent::Reject:
	    {
		String error;
		String reason;
		event->getList().getString(IAXInfoElement::CAUSE,reason);
		u_int32_t code = 0;
		event->getList().getNumeric(IAXInfoElement::CAUSECODE,code);
		if (code) {
		    const char* s = IAXInfoElement::causeName(code);
		    if (s)
			error = s;
		    else
			error = (unsigned int)code;
		}
		DDebug(this,DebugCall,"REJECT/HANGUP: error='%s' reason='%s' [%p]",
		    error.c_str(),reason.c_str(),this);
		hangup(event->local() ? 1 : -1,error,reason);
	    }
	    break;
	case IAXEvent::Timeout:
	    DDebug(this,DebugNote,"TIMEOUT. Transaction: %u,%u, Frame: %u,%u [%p]",
		event->getTransaction()->localCallNo(),event->getTransaction()->remoteCallNo(),
		event->frameType(),event->subclass(),this);
	    if (event->final())
		setReason("offline");
	    break;
	case IAXEvent::Busy:
	    DDebug(this,DebugCall,"BUSY [%p]",this);
	    if (event->final())
		setReason("busy");
	    break;
	case IAXEvent::AuthRep:
	    evAuthRep(event);
	    break;
	case IAXEvent::AuthReq:
	    evAuthReq(event);
	    break;
	default:
	    if (m_transaction) {
		Lock lck(m_mutexTrans);
		if (m_transaction)
		    m_transaction->getEngine()->defaultEventHandler(event);
	    }
	    if (!m_transaction)
		event->setFinal();
    }
    if (event->final()) {
	safeDeref();
	m_transaction = 0;
    }
}

void YIAXConnection::hangup(int location, const char* error, const char* reason, bool reject)
{
    Lock lck(m_mutexChan);
    if (!m_hangup)
	return;
    m_hangup = false;
    if (!m_reason)
	m_reason = error ? error : reason;
    if (!m_reason && location != -1)
	m_reason = Engine::exiting() ? "shutdown" : "";
    const char* loc = location ? (location > 0 ? "internal" : "remote") : "peer";
    clearMedia(true);
    clearMedia(false);
    clearMedia(true,IAXFormat::Video);
    clearMedia(false,IAXFormat::Video);
    Message* m = message("chan.hangup");
    m->setParam("status","hangup");
    m->setParam("reason",m_reason);
    Debug(this,DebugCall,"Hangup location=%s reason=%s [%p]",loc,m_reason.safe(),this);
    lck.drop();
    resetTransaction(m,reject);
    Engine::enqueue(m);
}

void YIAXConnection::resetTransaction(NamedList* params, bool reject)
{
    Lock lckChan(m_mutexChan);
    String reason = m_reason;
    lckChan.drop();
    Lock lck(m_mutexTrans);
    if (!m_transaction)
	return;
    if (params) {
	IAXMediaData* d = m_audio ? m_transaction->getData(IAXFormat::Audio) : 0;
	if (d) {
	    String tmp;
	    d->print(tmp);
	    params->addParam("iax_stats",tmp);
	}
	d = m_video ? m_transaction->getData(IAXFormat::Video) : 0;
	if (d) {
	    String tmp;
	    d->print(tmp);
	    params->addParam("iax_stats_video",tmp);
	}
    }
    u_int8_t code = 0;
    if (reason) {
	int val = IAXInfoElement::causeCode(reason);
	if (val)
	    code = val;
	else
	    code = reason.toInteger(0,0,0,127);
    }
    if (reject)
	m_transaction->sendReject(reason,code);
    else
	m_transaction->sendHangup(reason,code);
    m_transaction->setUserData(0);
    m_transaction = 0;
}

bool YIAXConnection::route(IAXEvent* ev, bool authenticated)
{
    Lock lck(&m_mutexTrans);
    if (!m_transaction)
	return false;
    if (m_routeCount >= 2)
	return false;
    m_routeCount++;
    Message* m = message("call.preroute",false,true);
    if (authenticated) {
	DDebug(this,DebugAll,"Route pass 2: Password accepted [%p]",this);
	m_refIncreased = false;
	m->addParam("username",m_transaction->username());
    }
    else {
	DDebug(this,DebugAll,"Route pass 1: No username [%p]",this);
	if (!m_transaction->getEngine()->acceptFormatAndCapability(m_transaction)) {
	    hangup(0,"nomedia",0,true);
	    return false;
	}
	// Advertise the not yet authenticated username
	if (m_transaction->username())
	    m->addParam("authname",m_transaction->username());
    }
    m->addParam("called",m_transaction->calledNo());
    m->addParam("caller",m_transaction->callingNo());
    if (ev) {
	u_int32_t val = 0;
	if (ev->getList().getNumeric(IAXInfoElement::CALLINGTON,val))
	    IAXEngine::addKeyword(*m,"callernumtype",IAXInfoElement::s_typeOfNumber,val);
	if (ev->getList().getNumeric(IAXInfoElement::CALLINGPRES,val)) {
	    IAXEngine::addKeyword(*m,"callerpres",IAXInfoElement::s_presentation,val & 0xf0);
	    IAXEngine::addKeyword(*m,"callerscreening",IAXInfoElement::s_screening,val & 0x0f);
	}
    }
    m->addParam("callername",m_transaction->callingName());
    m->addParam("ip_host",m_transaction->remoteAddr().host());
    m->addParam("ip_port",String(m_transaction->remoteAddr().port()));
    String fmtsAudio;
    IAXFormat::formatList(fmtsAudio,m_transaction->capability(),dict_payloads);
    m->addParam("formats",fmtsAudio);
    String fmtsVideo;
    IAXFormat::formatList(fmtsVideo,m_transaction->capability(),dict_payloads_video);
    if (fmtsVideo) {
	if (fmtsAudio) {
	    m->addParam("media_audio",String::boolText(true));
	    m->addParam("formats_audio",fmtsAudio);
	}
	m->addParam("media_video",String::boolText(true));
	m->addParam("formats_video",fmtsVideo);
    }
    (static_cast<YIAXEngine*>(m_transaction->getEngine()))->fillMessage(*m,false);
    lck.drop();
    return startRouter(m);
}

// Retrieve the data consumer from IAXFormat media type
YIAXConsumer* YIAXConnection::getConsumer(int type)
{
    const String& name = dataEpName(type);
    return YOBJECT(YIAXConsumer,Channel::getConsumer(name));
}

// Retrieve the data source from IAXFormat media type
YIAXSource* YIAXConnection::getSource(int type)
{
    const String& name = dataEpName(type);
    return YOBJECT(YIAXSource,Channel::getSource(name));
}

DataSource* YIAXConnection::getSourceMedia(int type)
{
    const String& name = dataEpName(type);
    DataSource* src = Channel::getSource(name);
    if (m_sources[type] != src) {
	m_sources[type] = 0;
	DataEndpoint::commonMutex().lock();
	m_sources[type] = Channel::getSource(name);
	DataEndpoint::commonMutex().unlock();
    }
    return m_sources[type];
}

// Create audio source with the proper format
void YIAXConnection::startMedia(bool in, int type)
{
    u_int32_t format = 0;
    m_mutexTrans.lock();
    if (m_transaction) {
	format = in ? m_transaction->formatIn(type) : m_transaction->formatOut(type);
	if (format) {
	    if (type == IAXFormat::Audio)
		m_audio = true;
	    else if (type == IAXFormat::Video)
		m_video = true;
	}
    }
    m_mutexTrans.unlock();
    bool exists = false;
    if (in) {
	YIAXSource* src = getSource(type);
	if (src && src->format() == format)
	    return;
	exists = (src != 0);
    }
    else {
	YIAXConsumer* cons = getConsumer(type);
	if (cons && cons->format() == format)
	    return;
	exists = (cons != 0);
    }
    clearMedia(in,type);
    const String& epName = dataEpName(type);
    const char* formatText = format ? lookupFormat(format,type) : 0;
    const char* dir = in ? "incoming" : "outgoing";
    if (exists || format)
	Debug(this,DebugAll,"Starting %s media '%s' format '%s' (%u) [%p]",
	    dir,epName.c_str(),formatText,format,this);
    bool ok = true;
    if (in) {
	YIAXSource* src = 0;
	if (formatText)
	    src = new YIAXSource(this,format,formatText,type);
	setSource(src,epName);
	TelEngine::destruct(src);
	ok = !format || getSource(type);
    }
    else {
	YIAXConsumer* cons = 0;
	if (formatText)
	    cons = new YIAXConsumer(this,format,formatText,type);
	setConsumer(cons,epName);
	TelEngine::destruct(cons);
	ok = !format || getConsumer(type);
    }
    if (!ok)
	Debug(this,DebugNote,"Failed to start %s media '%s' format '%s' (%u) [%p]",
	    dir,epName.c_str(),formatText,format,this);
}

void YIAXConnection::clearMedia(bool in, int type)
{
    const String& epName = dataEpName(type);
    DDebug(this,DebugAll,"Clearing %s %s media [%p]",
	in ? "incoming" : "outgoing",epName.c_str(),this);
    if (in)
	setSource(0,epName);
    else
	setConsumer(0,epName);
}

void YIAXConnection::evAuthRep(IAXEvent* event)
{
    DDebug(this,DebugAll,"AUTHREP [%p]",this);
    bool requestAuth, invalidAuth;
    const char* reason = "noauth";
    if (iplugin.userAuth(event->getTransaction(),true,requestAuth,invalidAuth,billid())) {
	if (route(event,true))
	    return;
	Debug(this,DebugNote,"Failed to route. Rejecting [%p]",this);
	reason = "temporary-failure";
    }
    else
	Debug(this,DebugNote,"Not authenticated. Rejecting [%p]",this);
    event->setFinal();
    hangup(1,reason,0,true);
}

void YIAXConnection::evAuthReq(IAXEvent* event)
{
    DDebug(this,DebugAll,"AUTHREQ [%p]",this);
    String response;
    Lock lck(m_mutexTrans);
    if (m_transaction) {
	m_transaction->getEngine()->getMD5FromChallenge(response,
	    m_transaction->challenge(),m_password);
	m_transaction->sendAuthReply(response);
    }
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


//
// IAXURI
//
IAXURI::IAXURI(const char* user, const char* host, const char* calledNo, const char* calledContext, int port)
    : m_username(user),
      m_host(host),
      m_port(port),
      m_calledNo(calledNo),
      m_calledContext(calledContext),
      m_parsed(true)

{
    *this << "iax2:";
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
    static const Regexp r("^\\([Ii][Aa][Xx]2\\?:\\)\\?\\([^[:space:][:cntrl:]@]\\+@\\)\\?\\([[:alnum:]._-]\\+\\)\\(:[0-9]\\+\\)\\?\\(/[[:alnum:]]*\\)\\?\\([@?][^@?:/]*\\)\\?$");
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
