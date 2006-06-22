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

extern TelEngine::String s_iax_modInvalidAuth;

using namespace TelEngine;
namespace { // anonymous

static Configuration s_cfg;

class YIAXLineContainer;
class YIAXEngine;
class IAXURI;

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
    State m_state;
    String m_username;                  /* Username */
    String m_password;                  /* Password */
    String m_callingNo;                 /* Calling number */
    String m_callingName;               /* Calling name */
    u_int16_t m_expire;                 /* Expire time */
    String m_localAddr;
    String m_remoteAddr;
    int m_localPort;
    int m_remotePort;
    u_int32_t m_nextReg;                /* Time to next registration */
    u_int32_t m_nextKeepAlive;          /* Time to next keep alive signal */
    bool m_registered;
    bool m_register;                    /* Operation flag: True - register */
    IAXTransaction* m_transaction;
};

/**
 * YIAXLineContainer
 */
class YIAXLineContainer : public Mutex
{
public:
    inline YIAXLineContainer() : Mutex(true) {}
    inline ~YIAXLineContainer() {}

    /**
     * logout and remove all lines
     */
    void clear();

    /**
     * Update a line from a message
     * This method is thread safe 
     * @param msg Received message
     * @return True if the successfully updated
     */
    bool updateLine(Message &msg);

    /**
     * Event handler for a registration.
     * @param event The event.
     */
    void handleEvent(IAXEvent* event);

    /**
     * Terminate notification of a Register/Unregister operation
     * This method is thread safe
     * @param event The event (result)
     */
    void regTerminate(IAXEvent* event);

    /**
     * Timer notification
     * This method is thread safe
     * @param time Time
     */
    void evTimer(Time& time);

    /**
     * Fill a named list from a line
     * This method is thread safe
     */
    bool fillList(String& name, NamedList& dest, SocketAddr& addr, bool& registered);

    /**
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

/**
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

/**
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

/**
 * YIAXEngine
 */
class YIAXEngine : public IAXEngine
{
public:
    YIAXEngine(int transCount, int retransCount, int retransTime, int maxFullFrameDataLen, u_int32_t transTimeout);

    virtual ~YIAXEngine()
	{}

    /**
     * Process media from remote peer.
     * @param transaction IAXTransaction that owns the call leg
     * @param data Media data. 
     * @param tStamp Media timestamp. 
     */
    virtual void processMedia(IAXTransaction* transaction, DataBlock& data, u_int32_t tStamp);

    /**
     * Initiate an outgoing registration (release) request.
     * @param line YIAXLine pointer to use for registration.
     * @param regreq Registration request flag. If false a registration release will take place.
     * @return IAXTransaction pointer on success.
     */
    IAXTransaction* reg(YIAXLine* line, bool regreq = true);

    /**
     * Initiate an aoutgoing call.
     * @param addr Address to poke.
     * @param params Call parameters.
     * @return IAXTransaction pointer on success.
     */
    IAXTransaction* call(SocketAddr& addr, NamedList& params);

    /**
     * Initiate a test of existence of a remote IAX peer.
     * @param addr Address to poke.
     * @return IAXTransaction pointer on success.
     */
    IAXTransaction* poke(SocketAddr& addr);

    /**
     * Start thread members
     * @param listenThreadCount Reading thread count.
     */
    void start(u_int16_t listenThreadCount = 1);

protected:

    /**
     * Event handler for transaction with a connection.
     */
    virtual void processEvent(IAXEvent* event);

    void processRemoteReg(IAXEvent* event);

    /**
     * Send Register/Unregister messages to Engine
     */
    bool userreg(const String& username, u_int16_t refresh, bool regrel = true);

private:
    bool m_threadsCreated;      /* True if reading and get events threads were created */
};

/**
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

/**
 * YIAXDriver
 */
class YIAXDriver : public Driver
{
public:
    YIAXDriver();
    virtual ~YIAXDriver();
    virtual void initialize();
    /* Create an outgoing call */
    virtual bool msgExecute(Message& msg, String& dest);

    virtual bool msgRoute(Message& msg);

    virtual bool received(Message& msg, int id);

    inline u_int32_t defaultCodec() const
	{ return m_defaultCodec; }

    inline u_int32_t codecs() const
	{ return m_codecs; }

    inline int port() const
	{ return m_port; }

    inline YIAXEngine* getEngine() const
	{ return m_iaxEngine; }

protected:
    YIAXEngine* m_iaxEngine;
    u_int32_t m_defaultCodec;           /* Default codec */
    u_int32_t m_codecs;                 /* Capability */
    int m_port;				/* Default UDP port */
};

class YIAXConnection;

/**
 * YIAXConsumer
 */
class YIAXConsumer : public DataConsumer
{
public:
    YIAXConsumer(YIAXConnection* conn, const char* format);
    ~YIAXConsumer();
    virtual void Consume(const DataBlock &data, unsigned long tStamp);
private:
    YIAXConnection* m_connection;
    unsigned m_total;
};

/**
 * YIAXSource
 */
class YIAXSource : public DataSource
{
public:
    YIAXSource(YIAXConnection* conn, const char* format);
    ~YIAXSource();
    void Forward(const DataBlock &data, unsigned long tStamp = 0);
private:
    YIAXConnection* m_connection;
    unsigned m_total;
};

/**
 * YIAXConnection
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

    inline u_int32_t format() 
	{ return m_format; }

    void handleEvent(IAXEvent* event);

    /* Start router */
    bool route(bool authenticated = false);

protected:
    /* Hangup */
    void hangup(const char* reason = 0, bool reject = false);
    /* Hangup */
    inline void hangup(IAXEvent* event, const char* reason = 0, bool reject = false) {
	    event->setFinal();
	    hangup(reason,reject);
	}
    /* Start consumer */
    void startAudioIn();
    /* Start source */
    void startAudioOut();
    /* Events */
    void evAuthRep(IAXEvent* event);

    void safeDeref();
    bool safeGetRefIncreased();

private:
    YIAXEngine* m_iaxEngine;            /* IAX engine owning the transaction */
    IAXTransaction* m_transaction;      /* IAX transaction */
    bool m_mutedIn;                     /* No remote media accepted */
    bool m_mutedOut;                    /* No local media accepted */
    u_int32_t m_format;                 /* Current media format */
    u_int32_t m_capability;             /* Supported formats */
    String m_reason;                    /*  */
    bool m_hangup;			/* Need to send chan.hangup message */
    /*  */
    Mutex m_mutexRefIncreased;
    bool m_refIncreased;
};

/**
 * IAXURI
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


/**
 * Local data
 */
/* Init the driver */
static YIAXDriver iplugin;
/* Lines */
static YIAXLineContainer s_lines;

/**
 * Class definitions
 */

/**
 * YIAXLine
 */
YIAXLine::YIAXLine(const String& name)
    : String(name),
      m_state(Idle), m_expire(60), m_localPort(4569), m_remotePort(4569),
      m_nextReg(Time::secNow() + 40),
      m_registered(false),
      m_register(true)
{
//    yiax_line_allocated++;
}

YIAXLine::~YIAXLine()
{
//    yiax_line_released++;
}

/**
 * YIAXLineContainer
 */
bool YIAXLineContainer::updateLine(Message& msg)
{
    Lock lock(this);
    String name = msg.getValue("account");
    YIAXLine* line = findLine(name);
    if (line)
	return updateLine(line,msg);
    return addLine(msg);
}

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
	    Debug(&iplugin,DebugAll,"YIAXLineContainer::regTerminate[%s] - Ack for '%s'.",
		line->c_str(),line->state() == YIAXLine::Registering?"Register":"Unregister");
	    line->m_registered = true;
	    break;
	case IAXEvent::Reject:
	    // retry at 25% of the expire time
	    line->m_nextReg = Time::secNow() + (line->expire() / 4);
	    Debug(&iplugin,DebugAll,"YIAXLineContainer::regTerminate[%s] - Reject for '%s'.",
		line->c_str(),line->state() == YIAXLine::Registering?"Register":"Unregister");
	    line->m_registered = false;
	    break;
	case IAXEvent::Timeout:
	    // retry at 50% of the expire time
	    line->m_nextReg = Time::secNow() + (line->expire() / 2);
	    Debug(&iplugin,DebugAll,"YIAXLineContainer::regTerminate[%s] - Timeout for '%s'.",
		line->c_str(),line->state() == YIAXLine::Registering?"Register":"Unregister");
	    break;
	default:
	    return;
    }
    line->m_transaction = 0;
    // Unregister operation. Remove line
    if (line->state() == YIAXLine::Unregistering) {
	m_lines.remove(line,true);
	return;
    }
    line->m_state = YIAXLine::Idle;
}

void YIAXLineContainer::handleEvent(IAXEvent* event)
{
    switch (event->type()) {
	case IAXEvent::Accept:
	case IAXEvent::Reject:
	case IAXEvent::Timeout:
	    regTerminate(event);
	    break;
	default:
	    Debug(&iplugin,DebugAll,"YIAXLineContainer::handleEvent. Unexpected event: %u",event->type());
	    break;
    }
}

void YIAXLineContainer::evTimer(Time& time)
{
    u_int32_t sec = time.sec();
    Lock lock(this);
    for (ObjList* l = m_lines.skipNull(); l; l = l->next()) {
	YIAXLine* line = static_cast<YIAXLine*>(l->get());
	if (!line || line->state() != YIAXLine::Idle)
	    continue;
	if (sec > line->m_nextKeepAlive) {
	    line->m_nextKeepAlive = sec + 25;
	    SocketAddr addr(AF_INET);
	    addr.host(line->remoteAddr());
	    addr.port(line->remotePort());
	    iplugin.getEngine()->keepAlive(addr);
	}
	if (sec > line->m_nextReg) {
	    line->m_nextReg += line->expire();
	    if (line->m_register)
		startRegisterLine(line);
	    else
		startUnregisterLine(line);
	}
    }
}

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

YIAXLine* YIAXLineContainer::findLine(const String& name)
{
    for (ObjList* l = m_lines.skipNull(); l; l = l->next()) {
	YIAXLine* line = static_cast<YIAXLine*>(l->get());
	if (line && *line == name)
	    return line;
    }
    return 0;
}

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

void YIAXLineContainer::startRegisterLine(YIAXLine* line)
{
    Lock lock(this);
    line->m_register = true;
    if (line->state() != YIAXLine::Idle)
	return;
    if (iplugin.getEngine()->reg(line,true))
	line->m_state = YIAXLine::Registering;
}

void YIAXLineContainer::startUnregisterLine(YIAXLine* line)
{
    Lock lock(this);
    line->m_register = false;
    if (line->state() != YIAXLine::Idle)
	return;
    if (iplugin.getEngine()->reg(line,false))
	line->m_state = YIAXLine::Unregistering;
}

void YIAXLineContainer::clear()
{
    Lock lock(this);
    for (ObjList* l = m_lines.skipNull(); l; l = l->next()) {
	YIAXLine* line = static_cast<YIAXLine*>(l->get());
	if (line)
	    startUnregisterLine(line);
    }
}

/**
 * IAXListener
 */
void YIAXListener::run()
{
    Debug(m_engine,DebugAll,"%s started",currentName());
    SocketAddr addr;
    m_engine->readSocket(addr);
}

/**
 * IAXGetEvent
 */
void YIAXGetEvent::run()
{
    Debug(m_engine,DebugAll,"%s started",currentName());
    m_engine->runGetEvents();
}

/**
 * YIAXEngine
 */
YIAXEngine::YIAXEngine(int transCount, int retransCount, int retransTime, int maxFullFrameDataLen, u_int32_t transTimeout)
	: IAXEngine(transCount,retransCount,retransTime,maxFullFrameDataLen,transTimeout,iplugin.defaultCodec(),iplugin.codecs()),
	  m_threadsCreated(false)
{
}

void YIAXEngine::processMedia(IAXTransaction* transaction, DataBlock& data, u_int32_t tStamp)
{
    if (transaction)
	if (transaction->getUserData())
	    if ((static_cast<YIAXConnection*>(transaction->getUserData()))->getSource())
		(static_cast<YIAXSource*>((static_cast<YIAXConnection*>(transaction->getUserData()))->getSource()))->Forward(data,tStamp);
	    else
		; //Debug(this,DebugAll,"YIAXEngine - processMedia. No media source");
	else
	    Debug(this,DebugAll,"YIAXEngine - processMedia. Transaction doesn't have a connection");
    else
	Debug(this,DebugAll,"YIAXEngine - processMedia. No transaction");
}

IAXTransaction* YIAXEngine::reg(YIAXLine* line, bool regreq)
{
    if (!line)
	return 0;
    SocketAddr addr(AF_INET);
    addr.host(line->remoteAddr());
    addr.port(line->remotePort());
    Debug(this,DebugAll,"Outgoing Registration[%s]:\nUsername: %s\nHost: %s\nPort: %d\nTime(sec): %u",
		line->c_str(),line->username().c_str(),addr.host().c_str(),addr.port(),Time::secNow());
    /* Create IE list */
    IAXIEList ieList;
    ieList.appendString(IAXInfoElement::USERNAME,line->username());
    ieList.appendString(IAXInfoElement::PASSWORD,line->password());
    ieList.appendString(IAXInfoElement::CALLING_NUMBER,line->callingNo());
    ieList.appendString(IAXInfoElement::CALLING_NAME,line->callingName());
    ieList.appendNumeric(IAXInfoElement::REFRESH,line->expire(),2);
    /* Make it ! */
    IAXTransaction* tr = startLocalTransaction(regreq ? IAXTransaction::RegReq : IAXTransaction::RegRel,addr,ieList);
    if (tr)
	tr->setUserData(line);
    line->m_transaction = tr;
    return tr;
}

IAXTransaction* YIAXEngine::call(SocketAddr& addr, NamedList& params)
{
    Debug(this,DebugAll,"Outgoing Call:\nUsername: %s\nHost: %s\nPort: %d\nCalled number: %s\nCalled context: %s",
	params.getValue("username"),addr.host().c_str(),addr.port(),params.getValue("called"),params.getValue("calledname"));
    /* Create IE list */
    IAXIEList ieList;
    ieList.appendString(IAXInfoElement::USERNAME,params.getValue("username"));
    ieList.appendString(IAXInfoElement::PASSWORD,params.getValue("password"));
    ieList.appendString(IAXInfoElement::CALLING_NUMBER,params.getValue("caller"));
    ieList.appendString(IAXInfoElement::CALLING_NAME,params.getValue("callername"));
    ieList.appendString(IAXInfoElement::CALLED_NUMBER,params.getValue("called"));
    ieList.appendString(IAXInfoElement::CALLED_CONTEXT,params.getValue("calledname"));
    ieList.appendNumeric(IAXInfoElement::FORMAT,iplugin.defaultCodec(),4);
    ieList.appendNumeric(IAXInfoElement::CAPABILITY,iplugin.codecs(),4);
    /* Make the call ! */
    return startLocalTransaction(IAXTransaction::New,addr,ieList);
}

IAXTransaction* YIAXEngine::poke(SocketAddr& addr)
{
    Debug(this,DebugAll,"Outgoing POKE: Host: %s Port: %d",addr.host().c_str(),addr.port());
    IAXIEList ieList;
    /* Poke */
    return startLocalTransaction(IAXTransaction::Poke,addr,ieList);
}

void YIAXEngine::start(u_int16_t listenThreadCount)
{
    if (m_threadsCreated)
	return;
    if (!listenThreadCount)
	Debug(DebugWarn,"YIAXEngine - start. No reading threads(s)!.");
    for (; listenThreadCount; listenThreadCount--)
	(new YIAXListener(this,"YIAXListener thread"))->startup();
    (new YIAXGetEvent(this,"YIAXGetEvent thread"))->startup();
    m_threadsCreated = true;
}

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
		    /* Final event: disconnect */
		    Debug(this,DebugAll,"YIAXEngine::processEvent - Disconnect connection [%p]",connection);
		    connection->disconnect();
		}
	    }
	    else {
		if (event->type() == IAXEvent::New) {
		    /* Incoming request for a new call */
		    connection = new YIAXConnection(this,event->getTransaction());
		    event->getTransaction()->setUserData(connection);
		    if (!connection->route())
			event->getTransaction()->setUserData(0);
		}
	    }
	    break;
	case IAXTransaction::RegReq:
	case IAXTransaction::RegRel:
	    if (event->getTransaction()->getUserData()) {
		// Existing line
		s_lines.handleEvent(event);
		break;
	    }
	    if (event->type() == IAXEvent::New)
		processRemoteReg(event);
	    break;
	default: ;
    }
    delete event;
}

void YIAXEngine::processRemoteReg(IAXEvent* event)
{
    IAXTransaction* tr = event->getTransaction();

    Debug(this,DebugAll,"processRemoteReg: %s username: '%s'",
	tr->type() == IAXTransaction::RegReq?"Register":"Unregister",tr->username().c_str());
    Message msg("user.auth");
    msg.addParam("username",tr->username());
    if (!Engine::dispatch(msg)) {
	/* Not authenticated */
	Debug(this,DebugAll,"evNewRegistration. Not authenticated. Reject");
	tr->sendReject();
	return;
    }
    String password = msg.retValue();
    if (!password.length()) {
	/* Authenticated, no password. Try to (un)register */
	if (userreg(tr->username(),tr->expire(),event->subclass() == IAXControl::RegRel)) {
	    Debug(this,DebugAll,"evNewRegistration. Authenticated and (un)registered. Ack");
	    tr->sendAccept();
	}
	else {
	    Debug(this,DebugAll,"evNewRegistration. Authenticated but not (un)registered. Reject");
	    tr->sendReject();
	}
	return;
    }
    /* Authenticated, password required */
    Debug(this,DebugAll,"evNewRegistration. Request authentication");
    tr->sendAuth(password);
}

bool YIAXEngine::userreg(const String& username, u_int16_t refresh, bool regrel)
{
    Debug(this,DebugAll,"YIAXEngine - userreg. %s username: '%s'",regrel ? "Unregistering":"Registering",username.c_str());
    Message msg(regrel ? "user.unregister" : "user.register");
    msg.addParam("username",username);
    if (!regrel)
	msg.addParam("expires",(String(refresh)).c_str());
    return Engine::dispatch(msg);
}

/**
 * YIAXRegDataHandler
 */
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

/**
 * YIAXDriver
 */
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
    unlock();
    delete m_iaxEngine;
}

void YIAXDriver::initialize()
{
    Output("Initializing module YIAX");
    lock();
    s_cfg = Engine::configFile("yiaxchan");
    s_cfg.load();
    /* Load configuration */
    /* Codec capability */
    m_defaultCodec = 0;
    m_codecs = 0;
    u_int32_t fallback = 0;
    String preferred = s_cfg.getValue("formats","preferred");
    bool def = s_cfg.getBoolValue("formats","default",true);
    for (int i = 0; IAXFormat::audioData[i].token; i++) {
	if (s_cfg.getBoolValue("formats",IAXFormat::audioData[i].token,
	    def && DataTranslator::canConvert(IAXFormat::audioData[i].token))) {
	    m_codecs |= IAXFormat::audioData[i].value;
	    fallback = IAXFormat::audioData[i].value;
	    /* Set default (desired) codec */
	    if (preferred == IAXFormat::audioData[i].token)
		m_defaultCodec = fallback;
	}
    }
    if (!m_codecs)
	Debug(DebugWarn,"YIAXDriver - initialize. No audio format(s) available.");
    /* If desired codec is disabled fall back to last in list */
    if (!m_defaultCodec)
	m_defaultCodec = fallback;
    /* Port */
    if (s_cfg.getIntValue("general","port"))
	m_port = s_cfg.getIntValue("general","port");
    unlock();
    setup();
    /* We need channels to be dropped on shutdown */
    installRelay(Halt);
    installRelay(Route);
    /* Init IAX engine */
    int transCount = 16;
    int retransCount = 5;
    int retransTime = 500;
    int maxFullFrameDataLen = 1400;
    u_int32_t transTimeout = 10;
    int readThreadCount = 3;
    if (!m_iaxEngine) {
	Engine::install(new YIAXRegDataHandler);
	m_iaxEngine = new YIAXEngine(transCount,retransCount,retransTime,maxFullFrameDataLen,transTimeout);
    }
    m_iaxEngine->start(readThreadCount);
}

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
	msg.setParam("peerid",conn->id());
	msg.setParam("targetid",conn->id());
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

/**
 * IAXConsumer
 */
YIAXConsumer::YIAXConsumer(YIAXConnection* conn, const char* format)
    : DataConsumer(format), m_connection(conn), m_total(0)
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
	    m_connection->transaction()->sendMedia(data,m_connection->format());
    }
}

/**
 * YIAXSource
 */
YIAXSource::YIAXSource(YIAXConnection* conn, const char* format) 
    : DataSource(format), m_connection(conn), m_total(0)
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
      m_format(iplugin.defaultCodec()), m_capability(iplugin.codecs()), m_hangup(true),
      m_mutexRefIncreased(true), m_refIncreased(false)
{
    DDebug(this,DebugAll,"YIAXConnection::YIAXConnection [%p]",this);
#if 0
    if (username)
	m_username = *username;
    m_address << transaction->remoteAddr().host() << ":" << transaction->remoteAddr().port();
#endif
    setMaxcall(msg);
    Message* m = message("chan.startup");
    m->setParam("direction",status());
    if (msg) {
	m_targetid = msg->getValue("id");
	m->setParam("caller",msg->getValue("caller"));
	m->setParam("called",msg->getValue("called"));
	m->setParam("billid",msg->getValue("billid"));
    }
    Engine::enqueue(m);
}

YIAXConnection::~YIAXConnection()
{
    status("destroyed");
    setConsumer();
    setSource();
    hangup();
    DDebug(this,DebugAll,"YIAXConnection::~YIAXConnection [%p]",this);
}

void YIAXConnection::callAccept(Message& msg)
{
    DDebug(this,DebugAll,"callAccept [%p]",this);
    if (m_transaction)
	m_transaction->sendAccept();
    Channel::callAccept(msg);
}

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
    if (s == "noauth" && m_transaction && !safeGetRefIncreased()) {
	Debug(this,DebugAll,"callRejected [%p]. Request authentication",this);
	String pwd;
	m_transaction->sendAuth(pwd);
	if (ref()) {
	    m_refIncreased = true;
	    DDebug(this,DebugInfo,"callRejected [%p]. Authentication requested. Increased references counter",this);
	}
	return;
    }
    hangup(reason,true);
}

bool YIAXConnection::callRouted(Message& msg)
{
    if (!m_transaction) {
	Debug(this,DebugMild,"callRouted [%p]. No transaction: ABORT",this);
	return false;
    }
    DDebug(this,DebugAll,"callRouted [%p]",this);
    return true;
}

bool YIAXConnection::msgRinging(Message& msg)
{
    if (m_transaction) {
	m_transaction->sendRinging();
	startAudioOut();
	return Channel::msgRinging(msg);
    }
    return false;
}

bool YIAXConnection::msgAnswered(Message& msg)
{
    if (m_transaction) {
	m_transaction->sendAnswer();
	startAudioIn();
	startAudioOut();
	return Channel::msgAnswered(msg);
    }
    return false;
}

bool YIAXConnection::msgTone(Message& msg, const char* tone)
{
    if (m_transaction) {
	while (tone && *tone)
	    m_transaction->sendDtmf(*tone++);
	return true;
    }
    return false;
}

bool YIAXConnection::msgText(Message& msg, const char* text)
{
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
	    Debug(this,DebugAll,"YIAXConnection - ANSWERED (%s)",isOutgoing()?"outgoing":"incoming");
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
	    Debug(this,DebugAll,"YIAXConnection - TIMEOUT");
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
	   // Debug(DebugWarn,"YIAXConnection::handleEvent - Unhandled IAX event. Event type: %u. Frame - Type: %u Subclass: %u",
	//		event->type(),event->frameType(),event->subClass());
	    if (!m_transaction)
		event->setFinal();
    }
    if (event->final()) {
	//Debug(DebugWarn,"YIAXConnection::handleEvent - Final event: need deref ?");
	safeDeref();
	m_transaction = 0;
    }
}

void YIAXConnection::hangup(const char *reason, bool reject)
{
    if (!m_hangup)
	/* Already done */
	return;
    m_hangup = false;
    if (!reason)
	reason = m_reason;
    if (!reason)
	reason = Engine::exiting() ? "Server shutdown" : "Unexpected problem";
    if (m_transaction) {
	m_transaction->setUserData(0);
	if (reject)
	    m_transaction->sendReject(reason);
	else
	    m_transaction->sendHangup(reason);
        m_transaction = 0;
    }
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
    if (authenticated) {
	DDebug(this,DebugAll,"Route pass 2: Password accepted.");
	m_refIncreased = false;
	m->addParam("username",m_transaction->username());
    }
    else {
	DDebug(this,DebugAll,"Route pass 1: No username.");
	// advertise the not yet authenticated username
	if (m_transaction->username())
	    m->addParam("authname",m_transaction->username());
    }
    m->addParam("called",m_transaction->calledNo());
    m->addParam("callername",m_transaction->callingName());
    return startRouter(m);
}

void YIAXConnection::startAudioIn()
{
    if (getSource())
	return;
    const char* formatText = IAXFormat::audioText(m_format);
    setSource(new YIAXSource(this,formatText));
    getSource()->deref();
    DDebug(this,DebugAll,"startAudioIn - Format %u: '%s'",m_format,formatText);
}

void YIAXConnection::startAudioOut()
{
    if (getConsumer())
	return;
    const char* formatText = (char*)IAXFormat::audioText(m_format);
    setConsumer(new YIAXConsumer(this,formatText));
    getConsumer()->deref();
    DDebug(this,DebugAll,"startAudioOut - Format %u: '%s'",m_format,formatText);
}

void YIAXConnection::evAuthRep(IAXEvent* event)
{
    DDebug(this,DebugAll,"YIAXConnection - AUTHREP");
    IAXTransaction* tr = event->getTransaction();
    /* Try to obtain a password from Engine */
    Message msg("user.auth");
    msg.addParam("username",tr->username());
    if (!Engine::dispatch(msg)) {
	/* NOT Authenticated */
	hangup(event,"",true);
	return;
    }
    String pwd = msg.retValue();
    if (!pwd.length()) {
	/* Authenticated */
	tr->sendAccept();
	return;
    }
    if (!IAXEngine::isMD5ChallengeCorrect(tr->authdata(),tr->challenge(),pwd)) {
	/* Incorrect data received */
	DDebug(this,DebugAll,"AUTHREP - Incorrect MD5 answer. Reject.");
	hangup(event,s_iax_modInvalidAuth,true);
	return;
    }
    /* Password is correct. Route the user. */
    route(true);
}

void YIAXConnection::safeDeref()
{
    m_mutexRefIncreased.lock();
    bool bref = m_refIncreased;
    m_refIncreased = false;
    m_mutexRefIncreased.unlock();
    if (bref)
	deref();
}

bool YIAXConnection::safeGetRefIncreased()
{
    Lock lock(&m_mutexRefIncreased);
    return m_refIncreased;
}

/**
 * IAXURI
 */
IAXURI::IAXURI(const char* user, const char* host, const char* calledNo, const char* calledContext, int port)
      : m_username(user),
	m_host(host),
	m_port(port),
	m_calledNo(calledNo),
	m_calledContext(calledContext),
	m_parsed(true)

{
    *this << "iax:";
    if (m_username.length())
	*this << m_username << "@";
    *this << m_host;
    if (m_port)
	*this << ":" << m_port;
    if (m_calledNo.length()) {
	*this << "/" << m_calledNo;
	if (m_calledContext.length())
	    *this << "@" << m_calledContext;
    }
}

void IAXURI::parse()
{
#if 0
proto: user@ host :port /calledno @context
proto: user@ host :port /calledno ?context
#endif
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

bool IAXURI::fillList(NamedList& dest)
{
    if (!m_parsed)
	return false;
    if (m_username.length())
	dest.setParam("username",m_username);
    if (m_calledNo.length())
	dest.setParam("called",m_calledNo);
    if (m_calledContext.length())
	dest.setParam("calledname",m_calledContext);
    return true;
}

bool IAXURI::setAddr(SocketAddr& dest)
{
    parse();
    if (!m_host.length())
	return false;
    dest.host(m_host);
    dest.port(m_port ? m_port : iplugin.port());
    return true;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
