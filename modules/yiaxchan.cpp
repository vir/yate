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

#if 0
u_int64_t yiax_connection_allocated = 0;
u_int64_t yiax_connection_released = 0;
u_int64_t yiax_source_allocated = 0;
u_int64_t yiax_source_released = 0;
u_int64_t yiax_consumer_allocated = 0;
u_int64_t yiax_consumer_released = 0;
u_int64_t yiax_line_allocated = 0;
u_int64_t yiax_line_released = 0;
#endif

static Configuration s_cfg;
static String s_modNoMediaFormat("Unsupported media format or capability");
static String s_modNoAuthMethod("Unsupported authentication method");
static String s_modInvalidAuth("Invalid authentication request, response or challenge");

class YIAXLineContainer;
class YIAXEngine;
class IAXURI;

class YIAXLine : public String
{
    friend class YIAXLineContainer;
public:
    enum State {
	Idle,
	Registering,
	Unregistering,
	Unregistered,
    };

    YIAXLine(const String& name);
    virtual ~YIAXLine();

    inline State state() const
	{ return m_state; }
    inline const String& username() const
	{ return m_username; }
    inline const String& password() const
	{ return m_password; }
    inline const String& callingNo() const
	{ return m_callingNo; }
    inline const String& callingName() const
	{ return m_callingName; }
    inline const u_int16_t expire() const
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
    void update(IAXRegData& regdata);
    void fill(IAXRegData& regdata);

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
     * Update a line from a message
     * This method is thread safe 
     * @param msg Received message
     * @return True if the successfully updated
     */
    bool updateLine(Message &msg);

    /**
     * Get IAXRegData info from line given by regdata.m_name
     * This method is thread safe
     * @param regdata IAXRegData to fill
     * @return True if line exists and @ref regdata was successfully filled
     */
    bool fillRegData(IAXRegData& regdata);

    /**
     * Notification of a successfull Register/Unregister operation
     * This method is thread safe
     * @param regdata IAXRegData received from server
     */
    void regAck(IAXRegData& regdata);

    /**
     * Notification of an unsuccessfull Register/Unregister operation
     * This method is thread safe
     * @param regdata IAXRegData received from server
     */
    void regRej(IAXRegData& regdata);

    /**
     * Notification of operation timeout
     * This method is thread safe
     * @param regdata IAXRegData received from server
     */
    void regTimeout(IAXRegData& regdata);

    /**
     * Timer notification
     * This method is thread safe
     * @param time Time
     */
    void evTimer(Time& time);

protected:
    bool updateLine(YIAXLine* line, Message &msg);
    bool addLine(Message &msg);
    YIAXLine* findLine(String& name);
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
    inline YIAXEngine(int transCount, int retransCount, int retransTime, int maxFullFrameDataLen, u_int32_t transTimeout)
	: IAXEngine(transCount,retransCount,retransTime,maxFullFrameDataLen,transTimeout), 
	  m_threadsCreated(false)
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
     * @param destination Destination: address and port.
     * @param params IE list values.
     * @return IAXTransaction pointer on success.
     */
    IAXTransaction* call(const char* destination, NamedList& params, String* username = 0); 

    /**
     * Initiate a test of existence of a remote IAX peer.
     * @param destination Destination: address and port.
     * @return IAXTransaction pointer on success.
     */
    IAXTransaction* poke(const char* destination);

    /**
     * Start thread members
     * @param listenThreadCount Reading thread count.
     */
    void start(u_int16_t listenThreadCount = 1);

    static void YIAXEngine::getMD5FromChallenge(String& md5data, String& challenge, String& password);

    static bool YIAXEngine::isMD5ChallengeCorrect(String& md5data, String& challenge, String& password);

protected:

    /**
     * Event handler for a running registration (with an established transaction).
     * @param event The event.
     */
    void handleRegDataEvent(IAXEvent* event);

    /**
     * Event handler for a new registration request.
     * @param event The event.
     * @param transaction The event's transaction.
     */
    void evNewRegistration(IAXEvent* event, IAXConnectionlessTransaction* transaction);

    /**
     * Event handler for a new registration request reply.
     * @param event The event.
     * @param transaction The event's transaction.
     */
    void evRegRecv(IAXEvent* event, IAXConnectionlessTransaction* transaction);

    /**
     * Event handler for a authentification request.
     * @param event The event.
     * @param transaction The event's transaction.
     */
    void evRegAuth(IAXEvent* event, IAXConnectionlessTransaction* transaction);

    /**
     * Event handler for registration confirmation.
     * @param event The event.
     * @param transaction The event's transaction.
     */
    void evRegAck(IAXEvent* event, IAXConnectionlessTransaction* transaction);

    /**
     * Event handler for registration reject.
     * @param event The event.
     * @param transaction The event's transaction.
     */
    void evRegRej(IAXEvent* event, IAXConnectionlessTransaction* transaction);

    /**
     * Event handler for timeout.
     * @param event The event.
     * @param transaction The event's transaction.
     */
    void evRegTimeout(IAXEvent* event, IAXConnectionlessTransaction* transaction);

    /**
     * Event handler for transaction with a connection.
     */
    virtual void processEvent(IAXEvent* event);

    /**
     * Event handler for connectionless transaction
     */
    virtual void processConnectionlessEvent(IAXEvent* event);

    /**
     * Send Register/Unregister messages to Engine
     */
    bool userreg(String& username, u_int16_t refresh, bool regrel = true);

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

    virtual bool received(Message& msg, int id);

    inline u_int32_t defaultCodec() const
	{ return m_defaultCodec; }

    inline u_int32_t codecs() const
	{ return m_codecs; }

    inline int port() const
	{ return m_port; }

    inline YIAXEngine* getEngine() const
	{ return m_iaxEngine; }

    static bool setAddrFromURI(IAXURI& uri, SocketAddr& addr);

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
    YIAXConnection(YIAXEngine* iaxEngine, IAXTransaction* transaction, Message* msg = 0, String* username = 0);
    virtual ~YIAXConnection();
    virtual void callAccept(Message& msg);
    virtual void callRejected(const char* error, const char* reason = 0, const Message* msg = 0);
    virtual bool callPrerouted(Message& msg, bool handled);
    virtual bool callRouted(Message& msg);
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

    bool init(IAXEvent* event);

protected:
   void setFormatAndCapability(u_int32_t format, u_int32_t capability);

    /* Hangup */
    void hangup(const char* reason = 0, bool reject = false);
    /* Hangup */
    inline void hangup(IAXEvent* event, const char* reason = 0, bool reject = false) {
	    event->setFinal();
	    hangup(reason,reject);
	}
    /* Start router */
    bool route(bool authenticated = false);
    /* Start consumer */
    void startAudioIn();
    /* Start source */
    void startAudioOut();

    /**
     * Transport a text inside a call
     * @param text Text to transport
     * @param incoming The direction. If true, it's an incoming (from the remote peer) text
     */
    void YIAXConnection::transportText(String& text, bool incoming);

    /**
     * Transport a DTMF text inside a call
     * @param text DTMF text to transport
     * @param incoming The direction. If true, it's an incoming (from the remote peer) DTMF text
     */
    void YIAXConnection::transportDtmf(String& text, bool incoming);

    /* Events */
    void evAccept(IAXEvent* ev);
    void evVoice(IAXEvent* ev);
    void evReject(IAXEvent* ev);
    void evAnswer(IAXEvent* ev);
    void evRinging(IAXEvent* ev);
    void evBusy(IAXEvent* ev);
    void evTimeout(IAXEvent* ev);
    void evQuelch(IAXEvent* ev);
    void evUnquelch(IAXEvent* ev);
    void evText(IAXEvent* ev);
    void evDtmf(IAXEvent* ev);
    void evNoise(IAXEvent* ev);
    void evProgressing(IAXEvent* ev);
    void evAuthReq(IAXEvent* event);
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
    String m_username;                  /*  */
    String m_password;                  /*  */
    String m_calledNumber;              /*  */
    String m_callingName;               /*  */
    String m_challenge;                 /*  */
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
static YIAXLineContainer m_lines;

/**
 * Class definitions
 */

/**
 * YIAXLine
 */
YIAXLine::YIAXLine(const String& name)
	: String(name), m_state(Idle), m_expire(60), m_localPort(4569), m_remotePort(4569), m_nextReg(Time::secNow() + 40)
{
//    yiax_line_allocated++;
}

YIAXLine::~YIAXLine()
{
//    yiax_line_released++;
}

void YIAXLine::update(IAXRegData& regdata)
{
    m_username = regdata.m_username;
    m_callingNo = regdata.m_callingNo;
    m_callingName = regdata.m_callingName;
    m_expire = regdata.m_expire;
}

void YIAXLine::fill(IAXRegData& regdata)
{
    regdata.m_username = m_username;
    regdata.m_callingNo = m_callingNo;
    regdata.m_callingName = m_callingName;
    regdata.m_expire = m_expire;
    regdata.m_name = (String)*this;
    regdata.m_userdata = this;
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

bool YIAXLineContainer::fillRegData(IAXRegData& regdata)
{
    Lock lock(this);
    YIAXLine* line = findLine(regdata.m_name);
    if (!line)
	return false;
    line->fill(regdata);
    return true;
}

void YIAXLineContainer::regAck(IAXRegData& regdata)
{
    Lock lock(this);
    YIAXLine* line = findLine(regdata.m_name);
    if (!line)
	return;
    line->m_nextReg = Time::secNow() + line->m_expire * 5 / 6;
    line->m_callingNo = regdata.m_callingNo;
    line->m_callingName = regdata.m_callingName;
    Debug(&iplugin,DebugAll,"YIAXLineContainer - regAck[%s]. %s.",
	regdata.m_name.c_str(),line->state() == YIAXLine::Registering?"Register":"Unregister");
    if (line->state() == YIAXLine::Unregistering) {
	m_lines.remove(line,true);
	return;
    }
    line->m_state = YIAXLine::Idle;
}

void YIAXLineContainer::regRej(IAXRegData& regdata)
{
    Lock lock(this);
    YIAXLine* line = findLine(regdata.m_name);
    if (!line)
	return;
    line->m_nextReg = Time::secNow() + line->m_expire * 5 / 6;
    Debug(&iplugin,DebugAll,"YIAXLineContainer - regRej[%s]. %s.",
	regdata.m_name.c_str(),line->state() == YIAXLine::Registering?"Register":"Unregister");
    if (line->state() == YIAXLine::Unregistering) {
	m_lines.remove(line,true);
	return;
    }
    line->m_state = YIAXLine::Idle;
}

void YIAXLineContainer::regTimeout(IAXRegData& regdata)
{
    Lock lock(this);
    YIAXLine* line = findLine(regdata.m_name);
    if (!line)
	return;
    line->m_nextReg = Time::secNow() + line->m_expire * 5 / 6;
    Debug(&iplugin,DebugAll,"YIAXLineContainer - regTimeout[%s]. %s.",
	regdata.m_name.c_str(),line->state() == YIAXLine::Registering?"Register":"Unregister");
    if (line->state() == YIAXLine::Unregistering) {
	m_lines.remove(line,true);
	return;
    }
    line->m_state = YIAXLine::Idle;
}

void YIAXLineContainer::evTimer(Time& time)
{
    Lock lock(this);
    for (ObjList* l = m_lines.skipNull(); l; l = l->next()) {
	YIAXLine* line = static_cast<YIAXLine*>(l->get());
	if (!line)
	    continue;
	if (time.sec() > line->m_nextKeepAlive) {
	    SocketAddr addr(AF_INET);
	    addr.host(line->remoteAddr());
	    addr.port(line->remotePort());
	    iplugin.getEngine()->keepAlive(addr);
	    line->m_nextKeepAlive = time.sec() + 25;
	}
	if (time.sec() > line->m_nextReg) {
	    startRegisterLine(line);
	    line->m_nextReg += line->m_expire;
	}
    }
}

bool YIAXLineContainer::updateLine(YIAXLine* line, Message& msg)
{
    Debug(&iplugin,DebugAll,"YIAXLineContainer - updateLine: %s",line->c_str());
    String op = msg.getValue("operation");
    if (op == "logout") {
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
    line->m_nextReg = Time::secNow() + line->m_expire * 5 / 6;
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
    line->m_expire = String(msg.getValue("interval")).toInteger();
    line->m_nextReg = Time::secNow() + line->m_expire * 5 / 6;
    String op = msg.getValue("operation");
    if (op == "login")
	startRegisterLine(line);
    else
	if (op == "logout")
	    startUnregisterLine(line);
	else ;
    return true;
}

YIAXLine* YIAXLineContainer::findLine(String& name)
{
    for (ObjList* l = m_lines.skipNull(); l; l = l->next()) {
	YIAXLine* line = static_cast<YIAXLine*>(l->get());
	if (line && *line == name)
	    return line;
    }
    return 0;
}

void YIAXLineContainer::startRegisterLine(YIAXLine* line)
{
    if (iplugin.getEngine()->reg(line,true))
	line->m_state = YIAXLine::Registering;
}

void YIAXLineContainer::startUnregisterLine(YIAXLine* line)
{
    if (iplugin.getEngine()->reg(line,false))
	line->m_state = YIAXLine::Unregistering;
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
    IAXRegData regdata(line->username(),line->password(),line->callingNo(),line->callingName(),line->expire(),line->c_str(),line);
    Debug(this,DebugAll,"Outgoing Registration[%s]:\nUsername: %s\nHost: %s\nPort: %d",
		line->c_str(),line->username().c_str(),addr.host().c_str(),addr.port());
    /* Create IE list */
    ObjList ieList;
    ieList.append(new IAXInfoElementString(IAXInfoElement::USERNAME,(unsigned char*)line->username().c_str(),line->username().length()));
    ieList.append(new IAXInfoElementNumeric(IAXInfoElement::REFRESH,line->expire(),2));
    /* Make it ! */
    IAXTransaction* tr;
    if (regreq)
	tr = startLocalTransaction(IAXTransaction::RegReq,addr,&ieList,&regdata);
    else
	tr = startLocalTransaction(IAXTransaction::RegRel,addr,&ieList,&regdata);
    if (tr)
	Debug(this,DebugAll,"YIAXEngine - Outgoing Registration[%u]: (%s,%u). SUCCESS",(unsigned)regreq,addr.host().c_str(),addr.port());
    else
	Debug(this,DebugAll,"YIAXEngine - Outgoing Registration[%u]: (%s,%u). FAIL",(unsigned)regreq,addr.host().c_str(),addr.port());
    return tr;
}

IAXTransaction* YIAXEngine::call(const char* destination, NamedList& params, String* username)
{
    IAXURI uri(destination);
    SocketAddr addr(AF_INET);
    const char* s;

    uri.parse();
    Debug(this,DebugAll,"Outgoing Call:\nUsername:        %s\nHost:            %s\nPort:            %d\nCalled number:   %s\nCalled context:  %s",
	uri.username().c_str(),uri.host().c_str(),uri.port(),uri.calledNo().c_str(),uri.calledContext().c_str());
    /* Username */
    if (username)
	*username = uri.username();
    /* Init addr */
    if (!YIAXDriver::setAddrFromURI(uri,addr)) {
	Debug(this,DebugAll,"YIAXEngine - Outgoing Call. Missing host name");
	return 0;
    }
    /* Create IE list */
    ObjList ieList;
    ieList.append(new IAXInfoElementNumeric(IAXInfoElement::FORMAT,iplugin.defaultCodec(),4));
    ieList.append(new IAXInfoElementNumeric(IAXInfoElement::CAPABILITY,iplugin.codecs(),4));
    if (0 != (s = params.getValue("caller")))
	ieList.append(new IAXInfoElementString(IAXInfoElement::CALLING_NUMBER,(unsigned char*)s,strlen(s)));
    if (0 != (s = params.getValue("callername")))
	ieList.append(new IAXInfoElementString(IAXInfoElement::CALLING_NAME,(unsigned char*)s,strlen(s)));
    if (uri.calledNo().length())
	ieList.append(new IAXInfoElementString(IAXInfoElement::CALLED_NUMBER,(unsigned char*)uri.calledNo().c_str(),uri.calledNo().length()));
    else
	Debug(this,DebugAll,"YIAXEngine - Outgoing Call. Missing called number");
    if (uri.calledContext().length())
	ieList.append(new IAXInfoElementString(IAXInfoElement::CALLED_CONTEXT,
		(unsigned char*)uri.calledContext().c_str(),uri.calledContext().length()));
    if (username && username->length())
	ieList.append(new IAXInfoElementString(IAXInfoElement::USERNAME,(unsigned char*)username->c_str(),username->length()));
    /* Make the call ! */
    IAXTransaction* tr = startLocalTransaction(IAXTransaction::New,addr,&ieList);
    if (tr)
	Debug(this,DebugAll,"YIAXEngine - Outgoing Call: (%s,%u). SUCCESS",addr.host().c_str(),addr.port());
    else
	Debug(this,DebugAll,"YIAXEngine - Outgoing Call: (%s,%u). FAIL",addr.host().c_str(),addr.port());
    return tr;
}

IAXTransaction* YIAXEngine::poke(const char* destination)
{
    IAXURI uri(destination);
    SocketAddr addr(AF_INET);

    uri.parse();
    Debug(this,DebugAll,"Outgoing POKE:\nUsername:        %s\nHost:            %s\nPort:            %d\nCalled number:   %s\nCalled context:  %s",
	uri.username().c_str(),uri.host().c_str(),uri.port(),uri.calledNo().c_str(),uri.calledContext().c_str());
    /* Init addr */
    if (!YIAXDriver::setAddrFromURI(uri,addr)) {
	Debug(this,DebugAll,"YIAXEngine - Poke: (%s,%u). Missing host name",addr.host().c_str(),addr.port());
	return 0;
    }
    /* Poke */
    IAXTransaction* tr = startLocalTransaction(IAXTransaction::Poke,addr,0);
    if (tr)
	Debug(this,DebugAll,"YIAXEngine - Poke: (%s,%u). SUCCESS",addr.host().c_str(),addr.port());
    else
	Debug(this,DebugAll,"YIAXEngine - Poke: (%s,%u). FAIL",addr.host().c_str(),addr.port());
    return tr;
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
}

void YIAXEngine::getMD5FromChallenge(String& md5data, String& challenge, String& password)
{
    MD5 md5;
    md5 << challenge << password;
    md5data = md5.hexDigest();
}

bool YIAXEngine::isMD5ChallengeCorrect(String& md5data, String& challenge, String& password)
{
    MD5 md5;
    md5 << challenge << password;
    return md5data == md5.hexDigest();
}

void YIAXEngine::handleRegDataEvent(IAXEvent* event)
{
    switch (event->type()) {
	case IAXEvent::NewRegistration:
	    evNewRegistration(event,static_cast<IAXConnectionlessTransaction*>(event->getTransaction()));
	    break;
	case IAXEvent::RegRecv:
	    evRegRecv(event,static_cast<IAXConnectionlessTransaction*>(event->getTransaction()));
	    break;
	case IAXEvent::RegAuth:
	    evRegAuth(event,static_cast<IAXConnectionlessTransaction*>(event->getTransaction()));
	    break;
	case IAXEvent::RegAck:
	    evRegAck(event,static_cast<IAXConnectionlessTransaction*>(event->getTransaction()));
	    break;
	case IAXEvent::Reject:
	    evRegRej(event,static_cast<IAXConnectionlessTransaction*>(event->getTransaction()));
	    break;
	case IAXEvent::Timeout:
	    evRegTimeout(event,static_cast<IAXConnectionlessTransaction*>(event->getTransaction()));
	    break;
	default:
	    Debug(this,DebugAll,"YIAXEngine - handleRegDataEvent. Unexpected event: %u",event->type());
	    break;
    }
}

void YIAXEngine::evNewRegistration(IAXEvent* event, IAXConnectionlessTransaction* transaction)
{
    if (!(event->type() == IAXEvent::NewRegistration && (event->subclass() == IAXControl::RegReq || event->subclass() == IAXControl::RegRel)))
	return;
    Debug(this,DebugAll,"YIAXEngine - evNewRegistration: %s for username: '%s'",
	transaction->type() == IAXTransaction::RegReq?"RegReg":"RegRel",transaction->username().c_str());
    Message msg("user.auth");
    msg.addParam("username",transaction->username().c_str());
    if (!Engine::dispatch(msg)) {
	/* Not authenticated */
	Debug(this,DebugAll,"YIAXEngine - evNewRegistration. Not authenticated. Reject");
	transaction->sendReject();
	return;
    }
    String password = msg.retValue();
    if (!password.length()) {
	/* Authenticated, no password. Try to (un)register */
	if (userreg(transaction->username(),transaction->expire(),event->subclass() == IAXControl::RegRel)) {
	    Debug(this,DebugAll,"YIAXEngine - evNewRegistration. Authenticated and (un)registered. Ack");
	    transaction->sendRegAck();
	}
	else {
	    Debug(this,DebugAll,"YIAXEngine - evNewRegistration. Authenticated but not (un)registered. Reject");
	    transaction->sendReject();
	}
	return;
    }
    /* Authenticated, password required */
    Debug(this,DebugAll,"YIAXEngine - evNewRegistration. Request authentication");
    srand(Time::secNow());
    String challenge = rand();
    transaction->sendRegAuth(password,IAXAuthMethod::MD5,&challenge);
}

void YIAXEngine::evRegRecv(IAXEvent* event, IAXConnectionlessTransaction* transaction)
{
    if (event->type() != IAXEvent::RegRecv)
	return;
    Debug(this,DebugAll,"YIAXEngine - evRegRecv: %s",transaction->type() == IAXTransaction::RegReq?"RegReg":"RegRel");
    IAXInfoElement* ie = event->getIE(IAXInfoElement::MD5_RESULT);
    String res; 
    if (ie)
	res = (static_cast<IAXInfoElementString*>(ie))->data();
    if (!YIAXEngine::isMD5ChallengeCorrect(res,transaction->challenge(),transaction->password())) {
	/* Incorrect data received */
	Debug(this,DebugAll,"YIAXEngine - evRegRecv. Incorrect MD5 challenge. Reject.");
	transaction->sendReject();
	return;
    }
    /* Response is correct. */
    Debug(this,DebugAll,"YIAXEngine - evRegRecv. Authenticated and (un)registered. Ack");
    transaction->sendRegAck();
}

void YIAXEngine::evRegAuth(IAXEvent* event, IAXConnectionlessTransaction* transaction)
{
    if (!(event->type() == IAXEvent::RegAuth && (transaction->type() == IAXTransaction::RegReq || transaction->type() == IAXTransaction::RegRel)))
	return;
    Debug(this,DebugAll,"YIAXEngine - evRegAuth: %s",transaction->type() == IAXTransaction::RegReq?"RegReg":"RegRel");
    String data;
    u_int8_t auth = 255;
    IAXInfoElement* ie = event->getIE(IAXInfoElement::AUTHMETHODS);
    if (!ie) {
	transaction->sendReject("No authentication method");
	return;
    }
    auth = (static_cast<IAXInfoElementNumeric*>(ie))->data();
    switch (auth) {
	case IAXAuthMethod::MD5:
	    ie = event->getIE(IAXInfoElement::CHALLENGE);
	    if (!ie) {
		transaction->sendReject("No challenge");
		return;
	    }
	    YIAXEngine::getMD5FromChallenge(data,(static_cast<IAXInfoElementString*>(ie))->data(),transaction->password());
	    break;
	case IAXAuthMethod::RSA:
	    transaction->sendReject("Unsupported enchryption format");
	    return;
	case IAXAuthMethod::Text:
	    data = transaction->password();
	    break;
	default:
	    transaction->sendReject("Unknown enchryption format");
	    return;
    }
    transaction->sendReg(data,(IAXAuthMethod::Type)auth);
}

void YIAXEngine::evRegAck(IAXEvent* event, IAXConnectionlessTransaction* transaction)
{
    if (event->type() != IAXEvent::RegAck)
	return;
    IAXRegData regdata;
    transaction->fillRegData(regdata);
    m_lines.regAck(regdata);
}

void YIAXEngine::evRegRej(IAXEvent* event, IAXConnectionlessTransaction* transaction)
{
    if (event->type() != IAXEvent::Reject)
	return;
    IAXRegData regdata;
    transaction->fillRegData(regdata);
    m_lines.regRej(regdata);
}

void YIAXEngine::evRegTimeout(IAXEvent* event, IAXConnectionlessTransaction* transaction)
{
    if (event->type() != IAXEvent::Timeout)
	return;
    IAXRegData regdata;
    transaction->fillRegData(regdata);
    m_lines.regTimeout(regdata);
}

void YIAXEngine::processEvent(IAXEvent* event)
{
    YIAXConnection* connection = static_cast<YIAXConnection*>(event->getTransaction()->getUserData());
    if (connection) {
	connection->handleEvent(event);
	if (event->final()) {
	    /* Final event: disconnect */
	    Debug(this,DebugAll,"YIAXEngine::processEvent - Disconnect connection [%p]",connection);
	    connection->disconnect();
	}
    }
    else {
	if (event->type() == IAXEvent::NewCall) {
	    /* Incoming request for a new call */
	    connection = new YIAXConnection(this,event->getTransaction());
	    if (connection->init(event))
		event->getTransaction()->setUserData(connection);
	}
    }
    delete event;
}

void YIAXEngine::processConnectionlessEvent(IAXEvent* event)
{
    IAXTransaction::Type type = (static_cast<IAXTransaction*>(event->getTransaction()))->type();
    if (type == IAXTransaction::RegReq || type == IAXTransaction::RegRel) {
	handleRegDataEvent(event);
    }
    delete event;
}


bool YIAXEngine::userreg(String& username, u_int16_t refresh, bool regrel)
{
    if (regrel) {
	Debug(this,DebugAll,"YIAXEngine - userreg. Unregistering username: '%s'",username.c_str());
	Message msgunreg("user.unregister");
	msgunreg.addParam("username",username.c_str());
	return Engine::dispatch(msgunreg);
    }
    Debug(this,DebugAll,"YIAXEngine - userreg. Registering username: '%s'",username.c_str());
    Message msgreg("user.register");
    msgreg.addParam("username",username.c_str());
    msgreg.addParam("expires",(String(refresh)).c_str());
    return Engine::dispatch(msgreg);
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
    m_lines.updateLine(msg);
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


extern u_int64_t iax_transaction_allocated;
extern u_int64_t iax_transaction_released;
extern u_int64_t iax_frame_allocated;
extern u_int64_t iax_frame_released;
extern u_int64_t iax_ies_allocated;
extern u_int64_t iax_ies_released;
extern u_int64_t iax_events_allocated;
extern u_int64_t iax_events_released;


YIAXDriver::~YIAXDriver()
{
    Output("Unloading module YIAX");
    lock();
    channels().clear();
    unlock();
    delete m_iaxEngine;

#if 0
    Output("*******************************************************************");
    Output("Transactions:     %10lu %10lu",iax_transaction_allocated,iax_transaction_released);
    Output("Frames:           %10lu %10lu",iax_frame_allocated,iax_frame_released);
    Output("IE:               %10lu %10lu",iax_ies_allocated,iax_ies_allocated);
    Output("Events:           %10lu %10lu",iax_events_allocated,iax_events_released);
    Output("Connections:      %10lu %10lu",yiax_connection_allocated,yiax_connection_released);
    Output("Sources:          %10lu %10lu",yiax_source_allocated,yiax_source_released);
    Output("Consumers:        %10lu %10lu",yiax_consumer_allocated,yiax_consumer_released);
    Output("Lines:            %10lu %10lu",yiax_line_allocated,yiax_line_released);
    Output("*******************************************************************");
#endif
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

bool YIAXDriver::msgExecute(Message& msg, String& dest)
{
    if (!msg.userData()) {
	Debug(this,DebugAll,"YIAXDriver - msgExecute. No data channel for this IAX call!");
	return false;
    }
    IAXRegData regdata(String(msg.getValue("line")));
    if (!m_lines.fillRegData(regdata)) {
	Debug(this,DebugAll,"YIAXDriver - msgExecute. No line ['%s'] for this IAX call!",regdata.m_name.c_str());
	msg.setParam("error","offline");
//	return false;
    }
    IAXTransaction* tr = m_iaxEngine->call(dest.c_str(),msg,&regdata.m_username);
    if (!tr)
	return false;
    YIAXConnection* conn = new YIAXConnection(m_iaxEngine,tr,&msg,&regdata.m_username);
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
	m_lines.evTimer(msg.msgTime());
    else 
	if (id == Halt) {
	    dropAll(msg);
	    channels().clear();
	}
    return Driver::received(msg,id);
}

bool YIAXDriver::setAddrFromURI(IAXURI& uri, SocketAddr& addr)
{
    uri.parse();
    if (!uri.host().length())
	return false;
    addr.host(uri.host());
    uri.port() ? addr.port(uri.port()) : addr.port(iplugin.port());
    return true;
}

/**
 * IAXConsumer
 */
YIAXConsumer::YIAXConsumer(YIAXConnection* conn, const char* format)
    : DataConsumer(format), m_connection(conn), m_total(0)
{
//    yiax_consumer_allocated++;
}

YIAXConsumer::~YIAXConsumer()
{
//    yiax_consumer_released++;
}

void YIAXConsumer::Consume(const DataBlock& data, unsigned long tStamp)
{
    if (m_connection && !m_connection->mutedOut()) {
	m_total += data.length();
	if(m_connection->transaction())
	    m_connection->transaction()->sendMedia(data,m_connection->format());
    }
}

/**
 * YIAXSource
 */
YIAXSource::YIAXSource(YIAXConnection* conn, const char* format) 
    : DataSource(format), m_connection(conn), m_total(0)
{ 
//    yiax_source_allocated++;
}

YIAXSource::~YIAXSource()
{
//    yiax_source_released++;
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
YIAXConnection::YIAXConnection(YIAXEngine* iaxEngine, IAXTransaction* transaction, Message* msg, String* username)
    : Channel(&iplugin,0,transaction->outgoing()),
      m_iaxEngine(iaxEngine), m_transaction(transaction), m_mutedIn(false), m_mutedOut(false),
      m_format(iplugin.defaultCodec()), m_capability(iplugin.codecs()), m_hangup(true),
      m_mutexRefIncreased(true), m_refIncreased(false)
{
//    yiax_connection_allocated++;

    Debug(this,DebugAll,"YIAXConnection::YIAXConnection [%p]",this);
    if (username)
	m_username = *username;
    m_address << transaction->remoteAddr().host() << ":" << transaction->remoteAddr().port();
//    m_password = "supersecret";

    setMaxcall(msg);
    Message* m = message("chan.startup");
    m->setParam("direction",status());
    if (msg) {
	m->setParam("caller",msg->getValue("caller"));
	m->setParam("called",msg->getValue("called"));
	m->setParam("billid",msg->getValue("billid"));
    }
    Engine::enqueue(m);
}

YIAXConnection::~YIAXConnection()
{
//    yiax_connection_released++;

    status("destroyed");
    setConsumer();
    setSource();
    hangup();
    Debug(this,DebugAll,"YIAXConnection::~YIAXConnection  [%p]",this);
}

void YIAXConnection::callAccept(Message& msg)
{
    Debug(this,DebugAll,"YIAXConnection - callAccept [%p]",this);
    if (m_transaction)
	m_transaction->sendAccept(m_format);
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
    Debug(this,DebugAll,"YIAXConnection - callRejected [%p]. Error: '%s'",this,error);
    String s(error);
    if (s == "noauth" && m_transaction && !safeGetRefIncreased()) {
	Debug(this,DebugAll,"YIAXConnection - callRejected [%p]. Request authentication",this);
	srand(Time::secNow());
	m_challenge = rand();
	m_transaction->sendAuthReq(m_username,IAXAuthMethod::MD5,&m_challenge);
	if (ref()) {
	    m_refIncreased = true;
	    Debug(this,DebugAll,"YIAXConnection - callRejected [%p]. Authentication requested. Increased references counter",this);
	}
	return;
    }
    hangup(reason,true);
}

bool YIAXConnection::callRouted(Message& msg)
{
    if (!m_transaction) {
	Debug(this,DebugAll,"YIAXConnection - callRouted [%p]. No transaction: ABORT",this);
	return false;
    }
    Debug(this,DebugAll,"YIAXConnection - callRouted [%p]",this);
    return true;
}

bool YIAXConnection::msgTone(Message& msg, const char* tone)
{
    if (Channel::id() == msg.getValue("targetid")) {
	String t(tone);
	transportText(t,false);
	return true;
    }
    return false;
}

bool YIAXConnection::msgText(Message& msg, const char* text)
{
    if (Channel::id() == msg.getValue("targetid")) {
	String t(text);
	transportText(t,false);
	return true;
    }
    return false;
}

void YIAXConnection::disconnected(bool final, const char* reason)
{
    Debug(this,DebugAll,"YIAXConnection - disconnected [%p]",this);
    Channel::disconnected(final,reason);
    safeDeref();
}

bool YIAXConnection::callPrerouted(Message& msg, bool handled)
{
    if (!m_transaction) {
	Debug(this,DebugAll,"YIAXConnection - callPrerouted [%p]. No transaction: ABORT",this);
	return false;
    }
    Debug(this,DebugAll,"YIAXConnection - callPrerouted [%p]",this);
    return true;
}

void YIAXConnection::handleEvent(IAXEvent* event)
{
    switch(event->type()) {
	case IAXEvent::Progressing:
	    evProgressing(event);
	    break;
	case IAXEvent::Accept:
	    evAccept(event);
	    break;
	case IAXEvent::Quelch:
	    evQuelch(event);
	    break;
	case IAXEvent::Unquelch:
	    evUnquelch(event);
	    break;
	case IAXEvent::Ringing:
	    evRinging(event);
	    break; 
	case IAXEvent::Answer:
	    evAnswer(event);
	    break; 
	case IAXEvent::Hangup:
	case IAXEvent::Reject:
	    evReject(event);
	    break;
	case IAXEvent::Timeout:
	    evTimeout(event);
	    break;
	case IAXEvent::Busy:
	    evBusy(event);
	    break;
	case IAXEvent::Text:
	    evText(event);
	    break;
	case IAXEvent::Dtmf:
	    evDtmf(event);
	    break;
	case IAXEvent::Noise:
	    evNoise(event);
	    break;
	case IAXEvent::AuthReq:
	    evAuthReq(event);
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

bool YIAXConnection::init(IAXEvent* event)
{
    if (event->type() != IAXEvent::NewCall)
	return false;
    Debug(this,DebugAll,"YIAXConnection - NEW INCOMING CALL.");
    IAXInfoElementNumeric* ief = static_cast<IAXInfoElementNumeric*>(event->getIE(IAXInfoElement::FORMAT));
    IAXInfoElementNumeric* iec = static_cast<IAXInfoElementNumeric*>(event->getIE(IAXInfoElement::CAPABILITY));
    setFormatAndCapability(ief ? ief->data() : 0,iec ? iec->data() : 0);
    if (!m_format) {
	Debug(this,DebugAll,"YIAXConnection - NEW INCOMING CALL. No valid format. Reject.");
	hangup(event,s_modNoMediaFormat,true);
	return false;
    }
    IAXInfoElement* ie = event->getIE(IAXInfoElement::USERNAME);
    if (ie)
	m_username = (static_cast<IAXInfoElementString*>(ie))->data();
    ie = event->getIE(IAXInfoElement::CALLED_NUMBER);
    if (ie)
	m_calledNumber = (static_cast<IAXInfoElementString*>(ie))->data();
    ie = event->getIE(IAXInfoElement::CALLING_NAME);
    if (ie)
	m_callingName = (static_cast<IAXInfoElementString*>(ie))->data();
    return route();
}

void YIAXConnection::setFormatAndCapability(u_int32_t format, u_int32_t capability)
{
    m_capability = iplugin.codecs() & capability;
    m_format = format & m_capability;
    if (IAXFormat::audioText(m_format))
	return;
    /* No valid format: choose one*/
    m_format = 0;
    if (!m_capability)
	return;
    for (u_int32_t i = 0; IAXFormat::audioData[i].value; i++)
	if (0 != (m_capability & IAXFormat::audioData[i].value)) {
	    m_format = IAXFormat::audioData[i].value;
	    break;
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
    Debug(this,DebugAll,"YIAXConnection - hangup ('%s') [%p]",reason,this);
}

bool YIAXConnection::route(bool authenticated)
{
    Message* m = message("call.preroute",false,true);
    if (authenticated) {
	Debug(this,DebugAll,"YIAXConnection - route. Pass 2: Password accepted.");
	m_refIncreased = false;
	m->addParam("username",m_username);
    }
    else {
	Debug(this,DebugAll,"YIAXConnection - route. Pass 1: No username.");
	// advertise the not yet authenticated username
	if (m_username)
	    m->addParam("authname",m_username);
    }
    m->addParam("called",m_calledNumber);
    m->addParam("callername",m_callingName);
    return startRouter(m);
}

void YIAXConnection::startAudioIn()
{
    if (getSource())
	return;
    const char* formatText = IAXFormat::audioText(m_format);
    setSource(new YIAXSource(this,formatText));
    getSource()->deref();
    Debug(this,DebugAll,"YIAXConnection - startAudioIn - Format %u: '%s'",m_format,formatText);
}

void YIAXConnection::startAudioOut()
{
    if (getConsumer())
	return;
    const char* formatText = (char*)IAXFormat::audioText(m_format);
    setConsumer(new YIAXConsumer(this,formatText));
    getConsumer()->deref();
    Debug(this,DebugAll,"YIAXConnection - startAudioOut - Format %u: '%s'",m_format,formatText);
}

void YIAXConnection::transportText(String& text, bool incoming)
{
    if (!text.length())
	return;
    if (incoming) {
	Message* m = message("chan.text");
	m->addParam("text",text);
        Engine::enqueue(m);
    }
    else
	if (m_transaction)
	    m_transaction->sendText(text);
}

void YIAXConnection::transportDtmf(String& text, bool incoming)
{
    if (!text.length())
	return;
#if 0
	if ((dtmf >= '0' && dtmf <= '9') ||
	    (dtmf == '*') ||
	    (dtmf == '#') ||
	    (dtmf >= 'A' && dtmf <= 'D') ||
	    (dtmf >= 'a' && dtmf <= 'd'))
#endif
    if (incoming) {
	Message* m = message("chan.dtmf");
	m->addParam("text",text);
	Engine::enqueue(m);
    }
    else
	if (m_transaction)
	    for(u_int16_t i = 0; i < text.length(); i++)
		m_transaction->sendDtmf(text[i]);
}

void YIAXConnection::evAccept(IAXEvent* event)
{
    Debug(this,DebugAll,"YIAXConnection - ACCEPT (%s)",isOutgoing()?"outgoing":"incoming");
    if (isOutgoing()) {
	IAXInfoElementNumeric* ief = static_cast<IAXInfoElementNumeric*>(event->getIE(IAXInfoElement::FORMAT));
	if (ief)
	    setFormatAndCapability(ief->data(),m_capability);
	/* m_format is a valid codec received ? */
	if (!m_format) {
	    Debug(this,DebugAll,"YIAXConnection - ACCEPT: Unsupported codec: %u. Reject.",m_format);
	    hangup(event,s_modNoMediaFormat,true);
	    return;
	}
    }
    else
	m_transaction->sendAnswer();
    startAudioIn();
    startAudioOut();
}

void YIAXConnection::evReject(IAXEvent* event)
{
    IAXInfoElement* ie = event->getIE(IAXInfoElement::CAUSE);
    if (ie)
	m_reason = (static_cast<IAXInfoElementString*>(ie))->data();
    Debug(this,DebugAll,"YIAXConnection - REJECT/HANGUP:  '%s'",m_reason.c_str());
}

void YIAXConnection::evAnswer(IAXEvent* event)
{
    Debug(this,DebugAll,"YIAXConnection - ANSWERED (%s)",isOutgoing()?"outgoing":"incoming");
    if (isAnswered())
	return;
    Engine::enqueue(message("call.answered"));
    startAudioIn();
    startAudioOut();
}

void YIAXConnection::evRinging(IAXEvent* event)
{
    Debug(this,DebugAll,"YIAXConnection - RINGING");
    Engine::enqueue(message("call.ringing"));
}

void YIAXConnection::evBusy(IAXEvent* event)
{
    Debug(this,DebugAll,"YIAXConnection - BUSY");
    m_reason = "Busy";
}

void YIAXConnection::evTimeout(IAXEvent* event)
{
    Debug(this,DebugAll,"YIAXConnection - TIMEOUT");
    m_reason = "Timeout";
}

void YIAXConnection::evQuelch(IAXEvent* event)
{
    Debug(this,DebugAll,"YIAXConnection - QUELCH");
    m_mutedOut = true;
}

void YIAXConnection::evUnquelch(IAXEvent* event)
{
    Debug(this,DebugAll,"YIAXConnection - UNQUELCH");
    m_mutedOut = false;
}

void YIAXConnection::evText(IAXEvent* event)
{
    Debug(this,DebugAll,"YIAXConnection - TEXT");
    IAXInfoElement* ie = event->getIE(IAXInfoElement::textframe);
    if (ie)
	transportText((static_cast<IAXInfoElementString*>(ie))->data(),true);
}

void YIAXConnection::evDtmf(IAXEvent* event)
{
    Debug(this,DebugAll,"YIAXConnection - DTMF: %c",(char)event->subclass());
    String dtmf((char)event->subclass());
    transportDtmf(dtmf,true);
}

void YIAXConnection::evNoise(IAXEvent* event)
{
    Debug(this,DebugAll,"YIAXConnection - NOISE: %u",event->subclass());
}

void YIAXConnection::evProgressing(IAXEvent* event)
{
    Debug(this,DebugAll,"YIAXConnection - CALL PROGRESSING");
//    Engine::enqueue(message("call.progress"));
}

void YIAXConnection::evAuthReq(IAXEvent* event)
{
    Debug(this,DebugAll,"YIAXConnection - AUTHREQ");
    IAXInfoElement* ie = event->getIE(IAXInfoElement::USERNAME);
    if (ie)
	m_username = (static_cast<IAXInfoElementString*>(ie))->data();
    ie = event->getIE(IAXInfoElement::AUTHMETHODS);
    String iedata;
    u_int8_t auth = (static_cast<IAXInfoElementNumeric*>(ie))->data();
    if (ie)
	switch (auth) {
	    case IAXAuthMethod::MD5:
		ie = event->getIE(IAXInfoElement::CHALLENGE);
		if (!ie) {
		    Debug(this,DebugAll,"YIAXConnection - AUTHREQ. No challenge. Hangup.");
		    hangup(event,s_modInvalidAuth,true);
		    return;
		}
		m_challenge = ((static_cast<IAXInfoElementString*>(ie))->data());
		YIAXEngine::getMD5FromChallenge(iedata,m_challenge,m_password);
		break;
	    case IAXAuthMethod::RSA:
		Debug(this,DebugAll,"YIAXConnection - AUTHREQ. RSA not supported. Hangup.");
		hangup(event,s_modNoAuthMethod,true);
		return;
	    case IAXAuthMethod::Text:
		iedata = m_password;
		break;
	    default:
		Debug(this,DebugAll,"YIAXConnection - AUTHREQ. Unsupported enchryption format. Hangup.");
		hangup(event,s_modInvalidAuth,true);
		return;
	}
    if (m_transaction)
	m_transaction->sendAuthRep(iedata,(IAXAuthMethod::Type)auth);
}

void YIAXConnection::evAuthRep(IAXEvent* event)
{
    Debug(this,DebugAll,"YIAXConnection - AUTHREP");
    IAXInfoElement* ie = event->getIE(IAXInfoElement::MD5_RESULT);
    if (!ie) {
	hangup(event,s_modInvalidAuth,true);
	return;
    }
    /* Try to obtain a password from Engine */
    Message msg("user.auth");
    msg.addParam("username",m_username.c_str());
    if (Engine::dispatch(msg)) {
	String pwd = msg.retValue();
	if (pwd.length())
	    /* Received a password */
	    m_password = pwd;
	else {
	    /* Authenticated */
	    m_transaction->sendAccept(m_format);
	    return;
	}
    }
    else {
	/* NOT Authenticated */
	hangup(event,"",true);
	return;
    }
    if (!YIAXEngine::isMD5ChallengeCorrect((static_cast<IAXInfoElementString*>(ie))->data(),m_challenge,m_password)) {
	/* Incorrect data received */
	Debug(this,DebugAll,"YIAXConnection - AUTHREP. Incorrect MD5 challenge. Reject.");
	hangup(event,s_modInvalidAuth,true);
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

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
