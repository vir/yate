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
    {"g726aal2", IAXFormat::G726AAL2},
    {"g722",   IAXFormat::G722},
    {"amr",    IAXFormat::AMR},
    // video payloads
    {"jpeg",   IAXFormat::JPEG},
    {"png",    IAXFormat::PNG},
    {"h261",   IAXFormat::H261},
    {"h263",   IAXFormat::H263},
    {"h263p",  IAXFormat::H263p},
    {"h264",   IAXFormat::H264},
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
    friend class YIAXEngine;
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
    inline int expire() const
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
    void setRegistered(bool registered, const char* reason = 0, const char* error = 0);
    String m_name;
    String m_username;                  // Username
    String m_password;                  // Password
    String m_callingNo;                 // Calling number
    String m_callingName;               // Calling name
    int m_expire;                       // Expire time
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
    inline YIAXLineContainer() : Mutex(true,"YIAXLineContainer") {}
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
     * Process an authentication request for a Register/Unregister operation
     * This method is thread safe
     * @param event The event (AuthReq)
     */
    void regAuthReq(IAXEvent* event);

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

    /**
     * Get a copy of the lines list
     */
    inline ObjList* lines()
	{ Lock lock(this); return m_lines.skipNull();}

protected:
    // Clear a line's transaction
    void clearTransaction(YIAXLine* line);
    // Change a line String parameter. Unregister it if changed
    bool changeLine(YIAXLine* line, String& dest, const String& src);
    bool changeLine(YIAXLine* line, int& dest, int src);
    // Update a line
    bool updateLine(YIAXLine* line, Message &msg);
    bool addLine(Message &msg);
    YIAXLine* findLine(const String& name);
    YIAXLine* findLine(YIAXLine* line);
    // (Un)register a line
    void registerLine(YIAXLine* line, bool reg);
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
	u_int16_t maxFullFrameDataLen, u_int32_t trunkSendInterval, bool authRequired,
	NamedList* params);

    virtual ~YIAXEngine()
	{}

    /*
     * Process media from remote peer.
     * @param transaction IAXTransaction that owns the call leg
     * @param data Media data. 
     * @param tStamp Media timestamp. 
     * @param type Media type
     */
    virtual void processMedia(IAXTransaction* transaction, DataBlock& data,
	u_int32_t tStamp, int type, bool mark);

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

    // Initialize formats from config
    void initFormats(NamedList* params);

protected:

    /*
     * Event handler for transaction with a connection.
     */
    virtual void processEvent(IAXEvent* event);

    /*
     * Process a new format received with a full frame
     * @param trans Transaction that received the new format
     * @param type Media type
     * @param format The received format
     * @return True if accepted
     */
    virtual bool mediaFormatChanged(IAXTransaction* trans, int type, u_int32_t format);

    /*
     * Event handler for incoming registration transactions.
     * @param event The event.
     * @param first True if this is the first request.
     *  False if it is a response to an authentication request.
     */
    void processRemoteReg(IAXEvent* event,bool first);

    /*
     * Send Register/Unregister messages to Engine
     * Return the message on success
     */
    Message* userreg(IAXTransaction* tr, bool regrel = true);

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
    virtual bool hasLine(const String& line) const;
    virtual bool msgExecute(Message& msg, String& dest);
    virtual bool msgRoute(Message& msg);
    virtual bool received(Message& msg, int id);

    // Retrieve the IAX engine
    inline YIAXEngine* getEngine() const
	{ return m_iaxEngine; }

    // Update codecs a list of parameters
    // @return False if the result is 0 (no intersection)
    bool updateCodecsFromRoute(u_int32_t& codecs, const NamedList& params, int type);

    // Dispatch user.auth
    // @tr The IAX transaction
    // @param response True if it is a response.
    // @param requestAuth True on exit: the caller should request authentication
    // @param invalidAuth True on exit: authentication response is incorrect
    // @param billid The billing ID parameter if available
    // @return False if not authenticated
    bool userAuth(IAXTransaction* tr, bool response, bool& requestAuth,
	bool& invalidAuth, const char* billid = 0);

    bool commandComplete(Message& msg, const String& partLine, const String& partWord);
    void msgStatus(Message& msg);

protected:
    virtual void genUpdate(Message& msg);
    YIAXEngine* m_iaxEngine;
    unsigned int m_failedAuths;
    int m_port;
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
    void Forward(const DataBlock &data, unsigned long tStamp = 0, unsigned long flags = 0);
};

/*
 * The connection
 */
class YIAXConnection : public Channel
{
public:
    YIAXConnection(YIAXEngine* iaxEngine, IAXTransaction* transaction, Message* msg = 0,
	NamedList* params = 0);
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

    inline bool disconnect()
	{ return Channel::disconnect(m_reason,parameters()); }

    inline IAXTransaction* transaction() const
        { return m_transaction; }

    inline bool mutedIn() const
	{ return m_mutedIn; }

    inline bool mutedOut() const
	{ return m_mutedOut; }

    void handleEvent(IAXEvent* event);

    bool route(bool authenticated = false);

    // Retrieve the data consumer from IAXFormat media type
    YIAXConsumer* getConsumer(int type);
    // Retrieve the data source from IAXFormat media type
    YIAXSource* getSource(int type);

protected:
    // location: 0: from peer, else: from protocol
    void hangup(int location, const char* reason = 0, bool reject = false);
    void startMedia(bool in, int type = IAXFormat::Audio);
    void clearMedia(bool in, int type = IAXFormat::Audio);
    void evAuthRep(IAXEvent* event);
    void evAuthReq(IAXEvent* event);
    // Safe deref the connection if the reference counter was increased during registration
    void safeDeref();
    bool safeRefIncrease();
private:
    YIAXEngine* m_iaxEngine;            // IAX engine owning the transaction
    IAXTransaction* m_transaction;      // IAX transaction
    String m_password;                  // Password for client authentication
    bool m_mutedIn;                     // No remote media accepted
    bool m_mutedOut;                    // No local media accepted
    bool m_audio;                       // Audio endpoint was used
    bool m_video;                       // Video endpoint was used
    String m_reason;                    // Call end reason text
    bool m_hangup;                      // Need to send chan.hangup message
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
static YIAXLineContainer s_lines;	// Lines
static Thread::Priority s_priority = Thread::Normal;  // Threads priority
static bool s_callTokenOut = true;      // Send an empty call token on outgoing calls
static YIAXDriver iplugin;		// Init the driver

static unsigned int s_expires_min = 60;
static unsigned int s_expires_def = 60;
static unsigned int s_expires_max = 3600;

static String s_statusCmd = "status";

// Retrieve data endpoint name from IAXFormat type
static inline const char* dataEpName(int type)
{
    return IAXFormat::typeName(type);
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


/*
 * Class definitions
 */

// Create an idle line
YIAXLine::YIAXLine(const String& name)
    : Mutex(true,"IAX:Line"), m_name(name),
      m_expire(60), m_localPort(4569), m_remotePort(4569),
      m_nextReg(Time::secNow() + 40), m_nextKeepAlive(0),
      m_registered(false),
      m_register(true),
      m_transaction(0)
{
    DDebug(&iplugin,DebugAll,"YIAXLine(%s) [%p]",m_name.c_str(),this);
}

YIAXLine::~YIAXLine()
{
    DDebug(&iplugin,DebugAll,"~YIAXLine(%s) [%p]",m_name.c_str(),this);
}

// Set the registered status, emits user.notify messages if necessary
void YIAXLine::setRegistered(bool registered, const char* reason, const char* error)
{
    if ((m_registered == registered) && !reason)
	return;
    m_registered = registered;
    if (m_username) {
	Message* m = new Message("user.notify");
	m->addParam("account",toString());
	m->addParam("protocol","iax");
	m->addParam("username",m_username);
	m->addParam("registered",String::boolText(registered));
	m->addParam("reason",reason,false);
	m->addParam("error",error,false);
	Engine::enqueue(m);
    }
}

// Clear a line's transaction
void YIAXLineContainer::clearTransaction(YIAXLine* line)
{
    if (!(line && line->m_transaction))
	return;
    IAXTransaction* trans = line->m_transaction;
    line->m_transaction = 0;
    trans->abortReg();
    trans->setUserData(0);
}

// Change a line String parameter. Unregister it if changed
bool YIAXLineContainer::changeLine(YIAXLine* line, String& dest, const String& src)
{
    if (!line || dest == src)
	return false;
    if (line->m_registered) {
	line->m_registered = false;
	registerLine(line,false);
    }
    dest = src;
    return true;
}

// Change a line int parameter. Unregister it if changed
bool YIAXLineContainer::changeLine(YIAXLine* line, int& dest, int src)
{
    if (!line || dest == src)
	return false;
    if (line->m_registered) {
	line->m_registered = false;
	registerLine(line,false);
    }
    dest = src;
    return true;
}

// Find and update a line with parameters from message, create if needed
bool YIAXLineContainer::updateLine(Message& msg)
{
    const String& op = msg["operation"];
    bool login = (op != "logout" && op != "delete");
    const String& name = msg["account"];
    DDebug(&iplugin,DebugAll,"Updating line '%s'",name.c_str());
    Lock lck(this);
    YIAXLine* line = findLine(name);
    if (!line) {
	if (!login)
	    return false;
	line = new YIAXLine(name);
	line->ref();
	m_lines.append(line);
    }
    lck.drop();
    line->lock();
    if (login) {
	bool changed = !line->m_register;
	String addr = msg.getValue("server",msg.getValue("domain"));
	int pos = addr.find(":");
	int port = -1;
	if (pos >= 0) {
	    port = addr.substr(pos + 1).toInteger();
	    addr = addr.substr(0,pos);
	}
	else
	    port = msg.getIntValue("port");
	if (port > 0)
	    changed = changeLine(line,line->m_remotePort,port);
	changed = changeLine(line,line->m_remoteAddr,addr) || changed;
	changed = changeLine(line,line->m_username,msg.getValue("username")) || changed;
	changed = changeLine(line,line->m_password,msg.getValue("password")) || changed;
	int interval = msg.getIntValue("interval");
	if (interval < 60)
	    interval = 60;
	changed = changeLine(line,line->m_expire,interval) || changed;
	if (changed || op == "login") {
	    line->m_nextReg = Time::secNow() + (line->m_expire * 3 / 4);
	    line->m_nextKeepAlive = 0;
	    registerLine(line,true);
	}
    }
    else if (line->m_register)
	registerLine(line,false);
    line->unlock();
    // The line might be removed in regTerminate()
    Lock lock(this);
    YIAXLine* l = findLine(line);
    if (l) {
	TelEngine::destruct(l);
	TelEngine::destruct(line);
    }
    else
	m_lines.append(line);
    return true;
}

// Handle registration related transaction terminations
void YIAXLineContainer::regTerminate(IAXEvent* event)
{
    if (!event)
	return;
    IAXTransaction* trans = event->getTransaction();
    if (!trans)
	return;
    YIAXLine* line = findLine(static_cast<YIAXLine*>(trans->getUserData()));
    if (!line) {
	trans->setUserData(0);
	return;
    }
    line->lock();
    if (line->m_transaction != trans) {
	line->unlock();
	trans->setUserData(0);
	return;
    }
    String reason;
    bool ok = false;
    switch (event->type()) {
	case IAXEvent::Accept:
	    if (line->m_register) {
		// Honor server registration refresh. Assume default (60) if missing
		unsigned int interval = 60;
		if (event->getList().getNumeric(IAXInfoElement::REFRESH,interval) && !interval)
		    interval = 60;
		if (line->m_expire != (int)interval) {
		    DDebug(&iplugin,DebugNote,
			"Line(%s) changed expire interval from %d to %u [%p]",
			line->toString().c_str(),line->m_expire,interval,line);
		    line->m_expire = interval;
		}
		// re-register at 75% of the expire time
		line->m_nextReg = Time::secNow() + (line->expire() * 3 / 4);
		line->m_callingNo = trans->callingNo();
		line->m_callingName = trans->callingName();
	    }
	    ok = line->m_register;
	    break;
	case IAXEvent::Reject:
	    // retry at 25% of the expire time
	    line->m_nextReg = Time::secNow() + (line->expire() / 4);
	    event->getList().getString(IAXInfoElement::CAUSE,reason);
	    break;
	case IAXEvent::Timeout:
	    // retry at 50% of the expire time
	    line->m_nextReg = Time::secNow() + (line->expire() / 2);
	    reason = "timeout";
	    break;
	default:
	    ;
    }
    bool remove = false;
    if (event->type() == IAXEvent::Accept ||
	event->type() == IAXEvent::Reject ||
	event->type() == IAXEvent::Timeout) {
	const char* what = line->m_register ? "logon" : "logoff";
	const char* tf = line->m_register ? "to" : "from";
	if (event->type() == IAXEvent::Accept)
	    Debug(&iplugin,DebugCall,"IAX line '%s' %s success %s %s:%d",
		line->toString().c_str(),what,tf,line->m_remoteAddr.c_str(),
		line->m_remotePort);
	else
	    Debug(&iplugin,DebugWarn,"IAX line '%s' %s failure %s %s:%d reason=%s",
		line->toString().c_str(),what,tf,line->m_remoteAddr.c_str(),
		line->m_remotePort,reason.safe());
	clearTransaction(line);
	line->setRegistered(ok,reason,event->type() == IAXEvent::Reject ? "noauth" : 0);
	remove = !line->m_register;
    }
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
    if (!event || event->type() != IAXEvent::AuthReq)
	return;
    IAXTransaction* trans = event->getTransaction();
    if (!trans)
	return;
    YIAXLine* line = findLine(static_cast<YIAXLine*>(trans->getUserData()));
    if (!line) {
	trans->setUserData(0);
	return;
    }
    line->lock();
    if (line->m_transaction == trans) {
	String response;
	iplugin.getEngine()->getMD5FromChallenge(response,trans->challenge(),line->password());
	line->unlock();
	trans->sendAuthReply(response);
    }
    else {
	line->unlock();
	trans->setUserData(0);
    }
    TelEngine::destruct(line);
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
	case IAXEvent::AuthReq:
	    regAuthReq(event);
	    break;
	default:
	    iplugin.getEngine()->defaultEventHandler(event);
    }
}

// Tick the timer for all lines, send keepalives and reregister
void YIAXLineContainer::evTimer(Time& time)
{
    u_int32_t sec = time.sec();
    lock();
    ListIterator iter(m_lines);
    GenObject* gen = 0; 
    while (0 != (gen = iter.get())) {
	RefPointer<YIAXLine> line = static_cast<YIAXLine*>(gen);
	unlock();
	if (line) {
	    line->lock();
	    if (sec > line->m_nextReg) {
		line->m_nextReg += line->expire();
		registerLine(line,line->m_register);
	    }
	    else if (line->m_registered && sec > line->m_nextKeepAlive) {
		line->m_nextKeepAlive = sec + 25;
		SocketAddr addr(AF_INET);
		addr.host(line->remoteAddr());
		addr.port(line->remotePort());
		iplugin.getEngine()->keepAlive(addr);
	    }
	    line->unlock();
	    line = 0;
	}
	lock();
    }
    unlock();
}

// Fill parameters with information taken from line
bool YIAXLineContainer::fillList(String& name, NamedList& dest, SocketAddr& addr, bool& registered)
{
    registered = false;
    YIAXLine* line = findLine(name);
    if (!line)
	return false;
    line->lock();
    dest.setParam("username",line->username());
    dest.setParam("password",line->password());
    dest.setParam("caller",line->callingNo());
    dest.setParam("callername",line->callingName());
    String a = line->remoteAddr();
    int p = line->remotePort();
    registered = line->registered();
    line->unlock();
    TelEngine::destruct(line);
    addr.host(a);
    addr.port(p);
    return true;
}

// Find a line by name
YIAXLine* YIAXLineContainer::findLine(const String& name)
{
    Lock lock(this);
    ObjList* o = m_lines.find(name);
    if (!o)
	return 0;
    YIAXLine* line = static_cast<YIAXLine*>(o->get());
    line->ref();
    return line;
}

// Find a line by address, return same if found
YIAXLine* YIAXLineContainer::findLine(YIAXLine* line)
{
    if (!line)
	return 0;
    Lock lock(this);
    if (!m_lines.find(line))
	return 0;
    line->ref();
    return line;
}

// Initiate line (un)register
void YIAXLineContainer::registerLine(YIAXLine* line, bool reg)
{
    if (!line)
	return;
    line->m_register = reg;
    clearTransaction(line);
    iplugin.getEngine()->reg(line,reg);
}

// Unregister all lines
void YIAXLineContainer::clear(bool forced)
{
    if (forced) {
	Lock lock(this);
	m_lines.clear();
	return;
    }
    lock();
    ListIterator iter(m_lines);
    GenObject* gen = 0; 
    while (0 != (gen = iter.get())) {
	RefPointer<YIAXLine> line = static_cast<YIAXLine*>(gen);
	unlock();
	if (line) {
	    line->lock();
	    if (line->m_register)
		registerLine(line,false);
	    line->unlock();
	    line = 0;
	}
	lock();
    }
    unlock();
}

// Run the socket listening thread
void YIAXListener::run()
{
    DDebug(m_engine,DebugAll,"%s started",currentName());
    SocketAddr addr;
    m_engine->readSocket(addr);
}

// Run the event retreiving thread
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
	u_int32_t trunkSendInterval, bool authRequired, NamedList* params)
    : IAXEngine(iface,port,transListCount,retransCount,retransInterval,authTimeout,
	transTimeout,maxFullFrameDataLen,0,0,
	trunkSendInterval,authRequired,params),
      m_threadsCreated(false)
{
}

// Handle received voice data, forward it to connection's source
void YIAXEngine::processMedia(IAXTransaction* transaction, DataBlock& data,
    u_int32_t tStamp, int type, bool mark)
{
    if (transaction) {
	YIAXConnection* conn = static_cast<YIAXConnection*>(transaction->getUserData());
	if (conn) {
	    YIAXSource* src = conn->getSource(type);
	    if (src) {
		unsigned long flags = 0;
		if (mark)
		    flags = DataNode::DataMark;
		src->Forward(data,tStamp,flags);
	    }
	    else
		DDebug(this,DebugAll,"processMedia. No media source");
	}
	else
	    DDebug(this,DebugAll,"processMedia. Transaction doesn't have a connection");
    }
    else
	DDebug(this,DebugAll,"processMedia. No transaction");
}

// Create a new registration transaction for a line
IAXTransaction* YIAXEngine::reg(YIAXLine* line, bool regreq)
{
    if (!line)
	return 0;
    SocketAddr addr(AF_INET);
    addr.host(line->remoteAddr());
    addr.port(line->remotePort());
    Debug(this,DebugAll,"%s line=%s username=%s server=%s:%d",
	regreq ? "Registering" : "Unregistering",line->toString().c_str(),
	line->username().c_str(),addr.host().c_str(),addr.port());
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
    Debug(this,DebugAll,
	"Outgoing Call: username=%s host=%s:%d called=%s called context=%s",
	params.getValue("username"),addr.host().c_str(),addr.port(),
	params.getValue("called"),params.getValue("calledname"));
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
	DDebug(this,DebugNote,"Outgoing call failed: No preffered format");
	params.setParam("error","nomedia");
	return 0;
    }
    // Create IE list
    IAXIEList ieList;
    ieList.appendString(IAXInfoElement::USERNAME,params.getValue("username"));
    ieList.appendString(IAXInfoElement::PASSWORD,params.getValue("password"));
    ieList.appendString(IAXInfoElement::CALLING_NUMBER,params.getValue("caller"));
    ieList.appendString(IAXInfoElement::CALLING_NAME,params.getValue("callername"));
    ieList.appendString(IAXInfoElement::CALLED_NUMBER,params.getValue("called"));
    ieList.appendString(IAXInfoElement::CALLED_CONTEXT,params.getValue("iaxcontext"));
    ieList.appendNumeric(IAXInfoElement::FORMAT,fmt,4);
    ieList.appendNumeric(IAXInfoElement::CAPABILITY,codecs,4);
    if (params.getBoolValue("calltoken_out",s_callTokenOut))
	ieList.appendBinary(IAXInfoElement::CALLTOKEN,0,0);
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
	Debug(this,DebugWarn,"YIAXEngine. No reading socket threads(s)!");
    if (!eventThreadCount)
	Debug(this,DebugWarn,"YIAXEngine. No reading event threads(s)!");
    if (!trunkThreadCount)
	Debug(this,DebugWarn,"YIAXEngine. No trunking threads(s)!");
    for (; listenThreadCount; listenThreadCount--)
	(new YIAXListener(this,"YIAX Listener",s_priority))->startup();
    for (; eventThreadCount; eventThreadCount--)
	(new YIAXGetEvent(this,"YIAX GetEvent"))->startup();
    for (; trunkThreadCount; trunkThreadCount--)
	(new YIAXTrunking(this,"YIAX Trunking",s_priority))->startup();
    m_threadsCreated = true;
}

// Initialize formats from config
void YIAXEngine::initFormats(NamedList* params)
{
    NamedList dummy("");
    if (!params)
	params = &dummy;
    u_int32_t codecsAudio = 0;
    u_int32_t codecsVideo = 0;
    u_int32_t defAudio = 0;
    u_int32_t defVideo = 0;
    bool def = params->getBoolValue("default",true);
    const TokenDict* dicts[2] = {dict_payloads,dict_payloads_video};
    for (int i = 0; i < 2; i++) {
	u_int32_t fallback = 0;
	bool audio = (i == 0);
	const char* media = audio ? "audio" : "video";
	const String& preferred = (*params)[audio ? "preferred" : "preferred_video"];
	u_int32_t& codecs = audio ? codecsAudio : codecsVideo;
	u_int32_t& defaultCodec = audio ? defAudio : defVideo;
	for (const TokenDict* d = dicts[i]; d->token; d++) {
	    bool defVal = def && DataTranslator::canConvert(d->token);
	    if (!params->getBoolValue(d->token,defVal))
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
	    Debug(&iplugin,DebugAll,"Enabled %s format(s) '%s' default=%s",
		media,tmp.c_str(),lookup(defaultCodec,dicts[i],""));
	}
	else
	    Debug(&iplugin,audio ? DebugWarn : DebugAll,"No %s format(s) available",media);
    }
    setFormats(codecsAudio | codecsVideo,defAudio,defVideo);
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
		    DDebug(this,DebugAll,"processEvent. Disconnecting (%p): '%s'",
			connection,connection->id().c_str());
		    connection->disconnect();
		}
	    }
	    else {
		if (event->type() == IAXEvent::New) {
		    // Incoming request for a new call
		    connection = new YIAXConnection(this,event->getTransaction());
		    connection->initChan();
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

// Process a new format received with a full frame
bool YIAXEngine::mediaFormatChanged(IAXTransaction* trans, int type, u_int32_t format)
{
    if (!trans)
	return false;
    Debug(this,DebugNote,"Refusing media change for transaction (%p)",trans);
    // TODO: implement
    return false;
}

// Process events for remote users registering to us
void YIAXEngine::processRemoteReg(IAXEvent* event, bool first)
{
    IAXTransaction* tr = event->getTransaction();
    DDebug(this,DebugAll,"processRemoteReg: %s username: '%s'",
	tr->type() == IAXTransaction::RegReq?"Register":"Unregister",tr->username().c_str());
    // Check for automatomatically authentication request if it's the first request
    if (first && iplugin.getEngine()->authRequired()) {
	DDebug(this,DebugAll,"processRemoteReg. Requesting authentication");
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
		Debug(&iplugin,DebugNote,"Registered user '%s' expires in %u s",
		    user,exp);
	    }
	    else
		Debug(&iplugin,DebugNote,"Unregistered user '%s'",user);
	    tr->sendAccept(&exp);
	    TelEngine::destruct(msg);
	}
	else {
	    Debug(&iplugin,DebugNote,"%s failed for authenticated user '%s'",
		regrel ? "Unregister" : "Register",user);
	    tr->sendReject("not registered");
	}
	return;
    }
    // First request: check if we should request auth
    if (first && requestAuth) {
	DDebug(this,DebugAll,"processRemoteReg. Requesting authentication");
	tr->sendAuth();
	return;
    }
    const char* reason = 0;
    if (invalidAuth)
	reason = IAXTransaction::s_iax_modInvalidAuth;
    if (!reason)
	reason = "not authenticated";
    Debug(&iplugin,DebugNote,"User '%s' addr=%s:%d not authenticated reason=%s'",
	tr->username().c_str(),tr->remoteAddr().host().c_str(),
	tr->remoteAddr().port(),reason);
    tr->sendReject(reason);
}

// Build and dispatch the user.(un)register message
Message* YIAXEngine::userreg(IAXTransaction* tr, bool regrel)
{
    DDebug(this,DebugAll,"YIAXEngine - userreg. %s username: '%s'",
	regrel ? "Unregistering":"Registering",tr->username().c_str());
    Message* msg = new Message(regrel ? "user.unregister" : "user.register");
    msg->addParam("username",tr->username());
    msg->addParam("driver","iax");
    if (!regrel) {
	String data = "iax/iax2:";
	data << tr->username() << "@";
	data << tr->remoteAddr().host() << ":" << tr->remoteAddr().port();
	const char* called = tr->calledNo();
	const char* context = tr->calledContext();
	if (called || context) {
	    data << "/" << called;
	    if (context)
		data << "@" << context;
	}
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
    if (!Engine::dispatch(*msg))
	TelEngine::destruct(msg);
    return msg;
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
    : Driver("iax","varchans"), m_failedAuths(0), m_port(4569)
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
    delete m_iaxEngine;
}

void YIAXDriver::initialize()
{
    Output("Initializing module YIAX");
    // Load configuration
    Configuration cfg(Engine::configFile("yiaxchan"));
    NamedList* gen = cfg.getSection("general");
    NamedList dummy("general");
    if (!gen)
	gen = &dummy;
    s_callTokenOut = gen->getBoolValue("calltoken_out",true);
    NamedList* registrar = cfg.getSection("registrar");
    if (!registrar)
	registrar = &dummy;
    s_expires_min = registrar->getIntValue("expires_min",60,1);
    s_expires_max = registrar->getIntValue("expires_max",3600,s_expires_min);
    s_expires_def = registrar->getIntValue("expires_def",60,s_expires_min,s_expires_max);
    DDebug(this,DebugAll,
	"Initialized calltoken_out=%s expires_min=%u expires_max=%u expires_def=%u",
	String::boolText(s_callTokenOut),s_expires_min,s_expires_max,s_expires_def);
    // Setup driver if this is the first call
    if (m_iaxEngine) {
	m_iaxEngine->initialize(*gen);
	m_iaxEngine->initFormats(cfg.getSection("formats"));
	return;
    }
    setup();
    installRelay(Halt);
    installRelay(Route);
    installRelay(Progress);
    installRelay(Status);
    Engine::install(new YIAXRegDataHandler);
    // Init IAX engine
    u_int16_t transListCount = 64;
    u_int16_t retransCount = 5;
    u_int16_t retransInterval = 500;
    u_int16_t authTimeout = 30;
    u_int16_t transTimeout = 10;
    u_int16_t maxFullFrameDataLen = 1400;
    u_int32_t trunkSendInterval = 10;
    m_port = gen->getIntValue("port",4569);
    String iface = gen->getValue("general","addr");
    bool authReq = cfg.getBoolValue("registrar","auth_required",true);
    m_iaxEngine = new YIAXEngine(iface,m_port,transListCount,retransCount,retransInterval,authTimeout,
	transTimeout,maxFullFrameDataLen,trunkSendInterval,authReq,gen);
    m_iaxEngine->debugChain(this);
    m_iaxEngine->initFormats(cfg.getSection("formats"));
    int tos = gen->getIntValue("tos",dict_tos,0);
    if (tos && !m_iaxEngine->socket().setTOS(tos))
	Debug(this,DebugWarn,"Could not set IP TOS to 0x%02x",tos);
    s_priority = Thread::priority(gen->getValue("thread"),s_priority);
    DDebug(this,DebugInfo,"Default thread priority set to '%s'",Thread::priority(s_priority));
    int readThreadCount = gen->getIntValue("read_threads",Engine::clientMode() ? 1 : 3);
    if (readThreadCount < 1)
	readThreadCount = 1;
    int eventThreadCount = gen->getIntValue("event_threads",Engine::clientMode() ? 1 : 3);
    if (eventThreadCount < 1)
	eventThreadCount = 1;
    int trunkingThreadCount = gen->getIntValue("trunk_threads",1);
    if (trunkingThreadCount < 1)
	trunkingThreadCount = 1;
    m_iaxEngine->start(readThreadCount,eventThreadCount,trunkingThreadCount);
}

// Add specific module update parameters
void YIAXDriver::genUpdate(Message& msg)
{
    unsigned int tmp = m_failedAuths;
    m_failedAuths = 0;
    msg.setParam("failed_auths",String(tmp));
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
    if (!msg.userData()) {
	Debug(this,DebugAll,"No data channel for this IAX call!");
	return false;
    }
    NamedList params(msg);
    SocketAddr addr(AF_INET);
    if (isE164(dest)) {
	String* line = params.getParam("line");
	bool lineReg = (line == 0);
	if (line && !s_lines.fillList(*line,params,addr,lineReg)) {
	    Debug(this,DebugNote,"IAX call failed: no line '%s'",line->c_str());
	    msg.setParam("error","offline");
	    return false;
	}
	if (!lineReg) {
	    Debug(this,DebugNote,"IAX call failed: line '%s' is not registered!",line->c_str());
	    msg.setParam("error","offline");
	    return false;
	}
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
	Debug(this,DebugAll,"Missing host name in this IAX call");
	return false;
    }
    IAXTransaction* tr = m_iaxEngine->call(addr,params);
    if (!tr)
	return false;
    YIAXConnection* conn = new YIAXConnection(m_iaxEngine,tr,&msg,&params);
    conn->initChan();
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
    else
	if (id == Status) {
	    String target = msg.getValue("module");
	    if (target && target.startsWith(name(),true) && !target.startsWith(prefix())) {
		msgStatus(msg);
		return false;
	    }
	}
    return Driver::received(msg,id);
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

bool YIAXDriver::commandComplete(Message& msg, const String& partLine, const String& partWord)
{
    String cmd = s_statusCmd + " " + name();
    String overviewCmd = s_statusCmd + " overview " + name();
    if (partLine == cmd || partLine == overviewCmd)
	itemComplete(msg.retValue(),"accounts",partWord);
    else 
    	return Driver::commandComplete(msg,partLine,partWord);
    return false;
}

void YIAXDriver::msgStatus(Message& msg)
{
    String str = msg.getValue("module");
    while (str.startSkip(name())) {
	str.trimBlanks();
	if (str.null())
	    break;
	if (str.startSkip("accounts")) {
	    msg.retValue().clear();
	    msg.retValue() << "module=" << name();
	    msg.retValue() << ",protocol=IAX";
	    msg.retValue() << ",format=Username|Status;";
	    msg.retValue() << "accounts=" << s_lines.lines()->count();
	    if (!msg.getBoolValue("details",true)) {
		msg.retValue() << "\r\n";
		return;
	    }
	    for (ObjList* o = s_lines.lines(); o; o = o->skipNext()) {
		YIAXLine* line = static_cast<YIAXLine*>(o->get());
		str.append(line->toString(),",") << "=";
		str.append(line->username()) << "|";
		str << (line-> registered() ? "online" : "offline");
	    }
	    msg.retValue().append(str,";"); 
	    msg.retValue() << "\r\n";
	    return;
	}
    }
    Driver::msgStatus(msg);
}


/**
 * IAXConsumer
 */
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
	    sent = m_connection->transaction()->sendMedia(data,format(),m_type,mark);
	}
    }
    return sent;
}

/**
 * YIAXSource
 */
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

void YIAXSource::Forward(const DataBlock& data, unsigned long tStamp, unsigned long flags)
{
    if (m_connection && m_connection->mutedIn())
	return;
    m_total += data.length();
    DataSource::Forward(data,tStamp,flags);
}

/**
 * YIAXConnection
 */
YIAXConnection::YIAXConnection(YIAXEngine* iaxEngine, IAXTransaction* transaction, Message* msg,
    NamedList* params)
    : Channel(&iplugin,0,transaction->outgoing()),
      m_iaxEngine(iaxEngine), m_transaction(transaction), m_mutedIn(false), m_mutedOut(false),
      m_audio(false), m_video(false),
      m_hangup(true),
      m_mutexTrans(true,"YIAXConnection::trans"),
      m_mutexRefIncreased(true,"YIAXConnection::refIncreased"),
      m_refIncreased(false)
{
    Debug(this,DebugAll,"Created %s [%p]",isOutgoing()?"outgoing":"incoming",this);
    setMaxcall(msg);
    Message* m = message("chan.startup",msg);
    m->setParam("direction",status());
    if (transaction)
	m_address << transaction->remoteAddr().host() << ":" << transaction->remoteAddr().port();
    if (msg)
	m_targetid = msg->getValue("id");
    if (params) {
	m_password = params->getValue("password");
	m->copyParams(*params,"caller,callername,called,billid,callto,username");
    }
    Engine::enqueue(m);
}

YIAXConnection::~YIAXConnection()
{
    status("destroyed");
    setConsumer();
    setSource();
    hangup(0);
    Debug(this,DebugAll,"Destroyed with reason '%s' [%p]",m_reason.safe(),this);
}

// Incoming call accepted, possibly set trunking on this connection
void YIAXConnection::callAccept(Message& msg)
{
    DDebug(this,DebugCall,"callAccept [%p]",this);
    m_mutexTrans.lock();
    if (m_transaction) {
	u_int32_t codecs = iplugin.getEngine()->capability();
	if (msg.getValue("formats")) {
	    u_int32_t ca = IAXFormat::mask(codecs,IAXFormat::Audio);
	    iplugin.updateCodecsFromRoute(ca,msg,IAXFormat::Audio);
	    iplugin.getEngine()->acceptFormatAndCapability(m_transaction,&ca);
	}
	u_int32_t cv = IAXFormat::mask(codecs,IAXFormat::Video);
	iplugin.updateCodecsFromRoute(cv,msg,IAXFormat::Video);
	iplugin.getEngine()->acceptFormatAndCapability(m_transaction,&cv,IAXFormat::Video);
	u_int32_t ci = IAXFormat::mask(codecs,IAXFormat::Image);
	iplugin.updateCodecsFromRoute(ci,msg,IAXFormat::Image);
	iplugin.getEngine()->acceptFormatAndCapability(m_transaction,&ci,IAXFormat::Image);
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
    if (!error)
	error = reason;
    String s(error);
    Lock lock(m_mutexTrans);
    if (m_transaction && s == "noauth") {
	if (safeRefIncrease()) {
	    Debug(this,DebugInfo,"callRejected. Requesting authentication [%p]",this);
	    m_transaction->sendAuth();
	    return;
	}
	else
	    error = "temporary-failure";
    }
    lock.drop();
    Debug(this,DebugCall,"callRejected. Reason: '%s' [%p]",error,this);
    hangup(0,reason,true);
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
		String reason;
		event->getList().getString(IAXInfoElement::CAUSE,reason);
		DDebug(this,DebugCall,"REJECT/HANGUP: '%s' [%p]",reason.c_str(),this);
		hangup(event->local() ? 1 : -1,reason);
	    }
	    break;
	case IAXEvent::Timeout:
	    DDebug(this,DebugNote,"TIMEOUT. Transaction: %u,%u, Frame: %u,%u [%p]",
		event->getTransaction()->localCallNo(),event->getTransaction()->remoteCallNo(),
		event->frameType(),event->subclass(),this);
	    if (event->final())
		m_reason = "offline";
	    break;
	case IAXEvent::Busy:
	    DDebug(this,DebugCall,"BUSY [%p]",this);
	    if (event->final())
		m_reason = "busy";
	    break;
	case IAXEvent::AuthRep:
	    evAuthRep(event);
	    break;
	case IAXEvent::AuthReq:
	    evAuthReq(event);
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

void YIAXConnection::hangup(int location, const char* reason, bool reject)
{
    if (!m_hangup)
	return;
    m_hangup = false;
    if (m_reason.null())
	m_reason = reason;
    if (m_reason.null())
	m_reason = Engine::exiting() ? "shutdown" : "unknown";
    const char* loc = location ? (location > 0 ? "internal" : "remote") : "peer";
    clearMedia(true);
    clearMedia(false);
    clearMedia(true,IAXFormat::Video);
    clearMedia(false,IAXFormat::Video);
    Message* m = message("chan.hangup",true);
    m->setParam("status","hangup");
    m->setParam("reason",m_reason);
    m_mutexTrans.lock();
    if (m_transaction) {
	IAXMediaData* d = m_audio ? m_transaction->getData(IAXFormat::Audio) : 0;
	if (d) {
	    String tmp;
	    d->print(tmp);
	    m->addParam("iax_stats",tmp);
	}
	d = m_video ? m_transaction->getData(IAXFormat::Video) : 0;
	if (d) {
	    String tmp;
	    d->print(tmp);
	    m->addParam("iax_stats_video",tmp);
	}
	if (reject)
	    m_transaction->sendReject(m_reason);
	else
	    m_transaction->sendHangup(m_reason);
	m_transaction->setUserData(0);
        m_transaction = 0;
    }
    m_mutexTrans.unlock();
    Engine::enqueue(m);
    Debug(this,DebugCall,"Hangup location=%s reason=%s [%p]",loc,m_reason.safe(),this);
}

bool YIAXConnection::route(bool authenticated)
{
    if (!m_transaction)
	return false;
    Message* m = message("call.preroute",false,true);
    Lock lock(&m_mutexTrans);
    if (authenticated) {
	DDebug(this,DebugAll,"Route pass 2: Password accepted [%p]",this);
	m_refIncreased = false;
	m->addParam("username",m_transaction->username());
    }
    else {
	DDebug(this,DebugAll,"Route pass 1: No username [%p]",this);
	if (!iplugin.getEngine()->acceptFormatAndCapability(m_transaction)) {
	    hangup(0,IAXTransaction::s_iax_modNoMediaFormat,true);
	    return false;
	}
	// Advertise the not yet authenticated username
	if (m_transaction->username())
	    m->addParam("authname",m_transaction->username());
    }
    m->addParam("called",m_transaction->calledNo());
    m->addParam("caller",m_transaction->callingNo());
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
    return startRouter(m);
}

// Retrieve the data consumer from IAXFormat media type
YIAXConsumer* YIAXConnection::getConsumer(int type)
{
    const char* name = dataEpName(type);
    return name ? YOBJECT(YIAXConsumer,Channel::getConsumer(name)) : 0;
}

// Retrieve the data source from IAXFormat media type
YIAXSource* YIAXConnection::getSource(int type)
{
    const char* name = dataEpName(type);
    return name ? YOBJECT(YIAXSource,Channel::getSource(name)) : 0;
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
    const char* epName = dataEpName(type);
    const char* formatText = format ? lookupFormat(format,type) : 0;
    const char* dir = in ? "incoming" : "outgoing";
    if (exists || format)
	Debug(this,DebugAll,"Starting %s media '%s' format '%s' (%u) [%p]",
	    dir,epName,formatText,format,this);
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
	    dir,epName,formatText,format,this);
}

void YIAXConnection::clearMedia(bool in, int type)
{
    const char* epName = dataEpName(type);
    DDebug(this,DebugAll,"Clearing %s %s media [%p]",
	in ? "incoming" : "outgoing",epName,this);
    if (in)
	setSource(0,epName);
    else
	setConsumer(0,epName);
}

void YIAXConnection::evAuthRep(IAXEvent* event)
{
    DDebug(this,DebugAll,"AUTHREP [%p]",this);
    bool requestAuth, invalidAuth;
    if (iplugin.userAuth(event->getTransaction(),true,requestAuth,invalidAuth,billid())) {
	// Authenticated. Route the user.
	route(true);
	return;
    }
    DDebug(this,DebugCall,"Not authenticated. Rejecting [%p]",this);
    event->setFinal();
    hangup(1,"noauth",true);
}

void YIAXConnection::evAuthReq(IAXEvent* event)
{
    DDebug(this,DebugAll,"AUTHREQ [%p]",this);
    String response;
    if (m_iaxEngine && m_transaction) {
	m_iaxEngine->getMD5FromChallenge(response,m_transaction->challenge(),m_password);
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
    static const Regexp r("^\\([Ii][Aa][Xx]2\\+:\\)\\?\\([^[:space:][:cntrl:]@]\\+@\\)\\?\\([[:alnum:]._-]\\+\\)\\(:[0-9]\\+\\)\\?\\(/[[:alnum:]]*\\)\\?\\([@?][^@?:/]*\\)\\?$");
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
