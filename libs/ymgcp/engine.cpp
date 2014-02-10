/**
 * engine.cpp
 * Yet Another MGCP Stack
 * This file is part of the YATE Project http://YATE.null.ro
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

#include <yatemgcp.h>

#include <string.h>

namespace TelEngine {

// Engine process, receive or check timeouts
class MGCPPrivateThread : public Thread, public GenObject
{
public:
    enum Action {
	Process = 1,
	Receive = 2,
    };
    // Create a thread to process or receive data for the engine
    MGCPPrivateThread(MGCPEngine* engine, bool process, Thread::Priority priority);
    virtual ~MGCPPrivateThread();
    virtual void run();
private:
    MGCPEngine* m_engine;
    SocketAddr m_addr;
    Action m_action;
};

};

using namespace TelEngine;

#define MAX_TRANS_ID 999999999           // Maximum length for transaction identifier

// Some default values. Time values are given in miliseconds
#define RECV_BUF_LEN 1500                // Receive buffer length
#define TR_RETRANS_INTERVAL 250
#define TR_RETRANS_INTERVAL_MIN 100
#define TR_RETRANS_COUNT 3
#define TR_RETRANS_COUNT_MIN 1
#define TR_EXTRA_TIME 30000
#define TR_EXTRA_TIME_MIN 10000


/**
 * MGCPPrivateThread
 */
MGCPPrivateThread::MGCPPrivateThread(MGCPEngine* engine, bool process,
	Thread::Priority priority)
    : Thread(process?"MGCP Process":"MGCP Receive",priority),
    m_engine(engine),
    m_addr(AF_INET),
    m_action(process?Process:Receive)
{
    DDebug(m_engine,DebugInfo,"MGCPPrivateThread::MGCPPrivateThread() [%p]",this);
    if (m_engine)
	m_engine->appendThread(this);
}

MGCPPrivateThread::~MGCPPrivateThread()
{
    DDebug(m_engine,DebugInfo,"MGCPPrivateThread::~MGCPPrivateThread() [%p]",this);
    if (m_engine)
	m_engine->removeThread(this);
}

void MGCPPrivateThread::run()
{
    DDebug(m_engine,DebugInfo,"%s started [%p]",currentName(),this);
    if (!m_engine)
	return;
    switch (m_action) {
	case Process:
	    m_engine->runProcess();
	    break;
	case Receive:
	    m_engine->runReceive(m_addr);
	    break;
    }
}


/**
 * MGCPEngine
 */
MGCPEngine::MGCPEngine(bool gateway, const char* name, const NamedList* params)
    : Mutex(true,"MGCPEngine"),
    m_iterator(m_transactions),
    m_gateway(gateway),
    m_initialized(false),
    m_nextId(1),
    m_address(AF_INET),
    m_maxRecvPacket(RECV_BUF_LEN),
    m_recvBuf(0),
    m_allowUnkCmd(false),
    m_retransInterval(TR_RETRANS_INTERVAL * 1000),
    m_retransCount(TR_RETRANS_COUNT),
    m_extraTime(TR_EXTRA_TIME * 1000),
    m_parseParamToLower(true),
    m_provisional(true),
    m_ackRequest(true)
{
    debugName((name && *name) ? name : (gateway ? "mgcp_gw" : "mgcp_ca"));

    DDebug(this,DebugAll,"MGCPEngine::MGCPEngine(). Gateway: %s [%p]",
	String::boolText(gateway),this);
    // Add known commands
    for (int i = 0; mgcp_commands[i].token; i++)
	m_knownCommands.append(new String(mgcp_commands[i].token));
    // Init
    if (params)
	initialize(*params);
}

MGCPEngine::~MGCPEngine()
{
    cleanup(false);
    if (m_recvBuf)
	delete[] m_recvBuf;
    DDebug(this,DebugAll,"MGCPEngine::~MGCPEngine()");
}

// Initialize this engine
void MGCPEngine::initialize(const NamedList& params)
{
    int level = params.getIntValue(YSTRING("debuglevel"));
    if (level)
	debugLevel(level);

    m_allowUnkCmd = params.getBoolValue(YSTRING("allow_unknown_cmd"),false);
    int val = params.getIntValue(YSTRING("retrans_interval"),TR_RETRANS_INTERVAL);
    m_retransInterval = 1000 * (val < TR_RETRANS_INTERVAL_MIN ? TR_RETRANS_INTERVAL_MIN : val);
    val = params.getIntValue(YSTRING("retrans_count"),TR_RETRANS_COUNT);
    m_retransCount = (val < TR_RETRANS_COUNT_MIN ? TR_RETRANS_COUNT_MIN : val);
    val = params.getIntValue(YSTRING("extra_time_to_live"),TR_EXTRA_TIME);
    m_extraTime = 1000 * (val < TR_EXTRA_TIME_MIN ? TR_EXTRA_TIME_MIN : val);

    if (!m_initialized) {
	val = params.getIntValue(YSTRING("max_recv_packet"),RECV_BUF_LEN);
	m_maxRecvPacket = val < RECV_BUF_LEN ? RECV_BUF_LEN : val;
    }

    m_parseParamToLower = params.getBoolValue(YSTRING("lower_case_params"),true);
    m_provisional = params.getBoolValue(YSTRING("send_provisional"),true);
    m_ackRequest = params.getBoolValue(YSTRING("request_ack"),true);

    // Bind socket if not valid
    if (!m_socket.valid()) {
	m_address.host(params.getValue("localip"));
	int port = params.getIntValue("port",-1);
	m_address.port(port < 0 ? defaultPort(gateway()) : port);
	m_socket.create(AF_INET,SOCK_DGRAM);

	int reqlen = params.getIntValue("buffer");
	if (reqlen > 0) {
#ifdef SO_RCVBUF
	    int buflen = reqlen;
	    if ((unsigned int)buflen < maxRecvPacket())
		buflen = maxRecvPacket();
	    if (buflen < 4096)
		buflen = 4096;
	    if (m_socket.setOption(SOL_SOCKET,SO_RCVBUF,&buflen,sizeof(buflen))) {
		buflen = 0;
		socklen_t sz = sizeof(buflen);
		if (m_socket.getOption(SOL_SOCKET,SO_RCVBUF,&buflen,&sz))
		    Debug(this,DebugAll,"UDP buffer size is %d (requested %d)",buflen,reqlen);
		else
		    Debug(this,DebugWarn,"Could not get UDP buffer size (requested %d)",reqlen);
	    }
	    else
		Debug(this,DebugWarn,"Could not set UDP buffer size %d (%d: %s)",
		    buflen,m_socket.error(),::strerror(m_socket.error()));
#else
	    Debug(this,DebugMild,"Can't set socket receive buffer: unsupported feature");
#endif
	}

	if (!m_socket.bind(m_address)) {
	    Alarm(this,"socket",DebugWarn,"Failed to bind socket to %s:%d. Error: %d: %s",
		m_address.host().safe(),m_address.port(),
		m_socket.error(),::strerror(m_socket.error()));
	    m_socket.terminate();
	}
	else
	    m_socket.getSockName(m_address);
	m_socket.setBlocking(false);
    }

    // Create private threads
    if (!m_initialized) {
	Thread::Priority prio = Thread::priority(params.getValue("thread_priority"));
	int c = params.getIntValue("private_receive_threads",1);
	for (int i = 0; i < c; i++)
	    (new MGCPPrivateThread(this,false,prio))->startup();
	c = params.getIntValue("private_process_threads",1);
	for (int i = 0; i < c; i++)
	    (new MGCPPrivateThread(this,true,prio))->startup();
    }

    if (debugAt(DebugAll)) {
	String tmp;
	tmp << "\r\ntype:              " << (gateway() ? "Gateway" : "Call Agent");
	tmp << "\r\nbind address:      " << m_address.host() << ":" << m_address.port();
	tmp << "\r\nallow_unknown_cmd: " << String::boolText(m_allowUnkCmd);
	tmp << "\r\nretrans_interval:  " << m_retransInterval;
	tmp << "\r\nretrans_count:     " << m_retransCount;
	tmp << "\r\nlower_case_params: " << m_parseParamToLower;
	tmp << "\r\nmax_recv_packet:   " << maxRecvPacket();
	tmp << "\r\nsend_provisional:  " << provisional();
	Debug(this,DebugInfo,"%s:%s",m_initialized?"Reloaded":"Initialized",tmp.c_str());
    }

    m_initialized = true;
}

// Add a command to the list of known commands
void MGCPEngine::addCommand(const char* cmd)
{
    String* tmp = new String(cmd);
    Lock lock(this);
    tmp->toUpper();
    if (tmp->length() == 4 && !knownCommand(*tmp)) {
	Debug(this,DebugInfo,"Adding extra command %s",tmp->c_str());
	m_knownCommands.append(tmp);
    }
    else
	TelEngine::destruct(tmp);
}

// Append an endpoint to this engine if not already done
void MGCPEngine::attach(MGCPEndpoint* ep)
{
    if (!ep)
	return;
    Lock lock(this);
    if (!m_endpoints.find(ep)) {
	m_endpoints.append(ep);
	Debug(this,DebugInfo,"Attached endpoint '%s'",ep->id().c_str());
    }
}

// Remove an endpoint from this engine and, optionally, remove all its transactions
void MGCPEngine::detach(MGCPEndpoint* ep, bool del, bool delTrans)
{
    if (!ep)
	return;
    if (del)
	delTrans = true;
    Debug(this,DebugInfo,"Detaching endpoint '%s'",ep->id().c_str());

    Lock lock(this);
    // Remove transactions
    if (delTrans) {
	ListIterator iter(m_transactions);
	for (GenObject* o; 0 != (o = iter.get());) {
	    MGCPTransaction* tr = static_cast<MGCPTransaction*>(o);
	    if (ep->id() == tr->ep())
		m_transactions.remove(tr,true);
	}
    }
    m_endpoints.remove(ep,del);
}

// Find an endpoint by its pointer
MGCPEndpoint* MGCPEngine::findEp(MGCPEndpoint* ep)
{
    Lock lock(this);
    return m_endpoints.find(ep) ? ep : 0;
}

// Find an endpoint by its id
MGCPEndpoint* MGCPEngine::findEp(const String& epId)
{
    Lock lock(this);
    return static_cast<MGCPEndpoint*>(m_endpoints[epId]);
}

// find a transaction
MGCPTransaction* MGCPEngine::findTrans(unsigned int id, bool outgoing)
{
    Lock lock(this);
    for (ObjList* o = m_transactions.skipNull(); o; o = o->skipNext()) {
	MGCPTransaction* tr = static_cast<MGCPTransaction*>(o->get());
	if (outgoing == tr->outgoing() && id == tr->id())
	    return tr;
    }
    return 0;
}

// Generate a new id for an outgoing transaction
unsigned int MGCPEngine::getNextId()
{
    Lock lock(this);
    if (m_nextId < MAX_TRANS_ID)
	return m_nextId++;
    m_nextId = 1;
    return MAX_TRANS_ID;
}

// Send a command message. Create a transaction for it.
// Fail if the message is not a valid one or isn't a valid command
MGCPTransaction* MGCPEngine::sendCommand(MGCPMessage* cmd, const SocketAddr& addr,
    bool engineProcess)
{
    if (!cmd)
	return 0;
    if (!(cmd->valid() && cmd->isCommand())) {
	Debug(this,DebugNote,"Can't initiate outgoing transaction for (%p) cmd=%s",
	    cmd,cmd->name().c_str());
	TelEngine::destruct(cmd);
	return 0;
    }

    Lock lock(this);
    return new MGCPTransaction(this,cmd,true,addr,engineProcess);
}

// Read data from the socket. Parse and process the received message
bool MGCPEngine::receive(unsigned char* buffer, SocketAddr& addr)
{
    if (!m_socket.valid())
	return false;
    if (Socket::efficientSelect() && m_socket.canSelect()) {
	bool canRead = false;
	if (m_socket.select(&canRead,0,0,Thread::idleUsec()) && !canRead)
	    return false;
    }
    int len = maxRecvPacket();
    int rd = m_socket.recvFrom(buffer,len,addr);
    if (rd == Socket::socketError()) {
	if (!m_socket.canRetry())
	    Debug(this,DebugWarn,"Socket read error: %d: %s",
		m_socket.error(),::strerror(m_socket.error()));
	return false;
    }
    if (rd > 0)
	len = rd;
    else
	return false;

    ObjList msgs;
    if (!MGCPMessage::parse(this,msgs,buffer,len)) {
	ObjList* o = msgs.skipNull();
	MGCPMessage* msg = static_cast<MGCPMessage*>(o?o->get():0);
	if (msg && msg->valid() && !msg->isCommand()) {
	    String tmp;
	    msg->toString(tmp);
	    sendData(tmp,addr);
	}
	return false;
    }
    if (!msgs.skipNull())
	return false;

    Lock lock(this);
    if (debugAt(DebugInfo)) {
	String tmp((const char*)buffer,len);
	Debug(this,DebugInfo,
	    "Received %u message(s) from %s:%d\r\n-----\r\n%s\r\n-----",
	    msgs.count(),addr.host().c_str(),addr.port(),tmp.c_str());
    }

    // Process received message(s)
    while (true) {
	MGCPMessage* msg = static_cast<MGCPMessage*>(msgs.remove(false));
	if (!msg)
	    break;

	// Command messages may contain ACK'd incoming transaction's responses
	// See RFC 3435: 3.2.2.19 and 3.5.1
	if (msg->isCommand()) {
	    String s = msg->params.getValue(YSTRING("k"));
	    if (!(s || m_parseParamToLower))
		s = msg->params.getValue(YSTRING("K"));
	    if (s) {
		unsigned int len = 0;
		unsigned int* trList = decodeAck(s,len);
		// Build an ACK message for each of ACK'd transaction response
		if (trList) {
		    for (unsigned int i = 0; i < len; i++) {
			MGCPTransaction* tr = findTrans(trList[i],false);
			if (tr)
			    tr->processMessage(new MGCPMessage(tr,0));
			else
			    DDebug(this,DebugNote,
				"Message %s carry ACK for unknown transaction %u",
				msg->name().c_str(),trList[i]);
		    }
		    delete trList;
		}
		else {
		    DDebug(this,DebugNote,"Message %s has invalid k: '%s' parameter",
			msg->name().c_str(),s.c_str());
		    MGCPTransaction* tr = findTrans(msg->transactionId(),false);
		    if (!tr)
			tr = new MGCPTransaction(this,msg,false,addr);
		    tr->setResponse(400,"Bad Transaction Ack");
		    continue;
		}
	    }
	}

	// Outgoing transaction id namespace is different then the incoming one
	// Check message:
	//   Command or response ACK: Destination is an incoming transaction
	//   Response: Destination is an outgoing transaction
	bool outgoing = !(msg->isCommand() || msg->isAck());
	MGCPTransaction* tr = findTrans(msg->transactionId(),outgoing);
	if (tr) {
	    tr->processMessage(msg);
	    continue;
	}
	// No transaction
	if (msg->isCommand()) {
	    new MGCPTransaction(this,msg,false,addr);
	    continue;
	}
	Debug(this,DebugNote,"Received response %d for unknown transaction %u",
	    msg->code(),msg->transactionId());
	TelEngine::destruct(msg);
    }

    return true;
}

// Try to get an event from a transaction.
// If the event contains an unknown command and this engine is not allowed
//  to process such commands, calls the @ref returnEvent() method, otherwise,
//  calls the @ref processEvent() method
bool MGCPEngine::process(u_int64_t time)
{
    MGCPEvent* event = getEvent(time);
    if (!event)
	return false;
    if (!processEvent(event))
	returnEvent(event);
    return true;
}

// Try to get an event from a given transaction.
// If the event contains an unknown command and this engine is not allowed
//  to process such commands, calls the returnEvent() method, otherwise,
//  calls the processEvent() method
bool MGCPEngine::processTransaction(MGCPTransaction* tr, u_int64_t time)
{
    MGCPEvent* event = tr ? tr->getEvent(time) : 0;
    if (!event)
	return false;
    if (!processEvent(event))
	returnEvent(event);
    return true;
}

// Repeatedly calls receive() until the calling thread terminates
void MGCPEngine::runReceive(SocketAddr& addr)
{
    if (m_recvBuf)
	delete[] m_recvBuf;
    m_recvBuf = new unsigned char[maxRecvPacket()];

    while (true)
	if (!receive(m_recvBuf,addr))
	    Thread::idle(true);
	else
	    Thread::check(true);
}

// Repeatedly calls receive() until the calling thread terminates
void MGCPEngine::runReceive()
{
    SocketAddr addr(AF_INET);
    runReceive(addr);
}

// Repeatedly calls process() until the calling thread terminates
void MGCPEngine::runProcess()
{
    while (true)
	if (!process())
	    Thread::idle(true);
	else
	    Thread::check(true);
}

// Try to get an event from a transaction
MGCPEvent* MGCPEngine::getEvent(u_int64_t time)
{
    lock();
    while (true) {
	if (Thread::check(false))
	    break;
	MGCPTransaction* tr = static_cast<MGCPTransaction*>(m_iterator.get());
        // End of iteration? NO: get a reference to the transaction
	if (!tr) {
	    m_iterator.assign(m_transactions);
	    break;
	}
	if (!tr->m_engineProcess)
	    continue;
	RefPointer<MGCPTransaction> sref = tr;
	if (!sref)
	    continue;
	// Get an event from the transaction
	unlock();
	MGCPEvent* event = sref->getEvent(time);
	// Remove the transaction if destroying
	if (event)
	    return event;
	lock();
    }
    unlock();
    return 0;
}

// Process an event generated by a transaction. Descendants must override this
//  method if they want to process events without breaking them apart
bool MGCPEngine::processEvent(MGCPEvent* event)
{
    DDebug(this,DebugAll,"MGCPEngine::processEvent(%p)",event);
    if (!event)
	return false;
    MGCPTransaction* trans = event->transaction();
    if (processEvent(trans,event->message())) {
	// Get rid of the event, it was handled
	delete event;
	return true;
    }
    return false;
}

// Process an event generated by a transaction. Descendants must override this
//  method if they want to process events
bool MGCPEngine::processEvent(MGCPTransaction* trans, MGCPMessage* msg)
{
    Debug(this,DebugStub,"MGCPEngine::processEvent(%p,%p)",trans,msg);
    return false;
}

// Returns an unprocessed event to this engine to be deleted.
// Incoming transactions will be responded. Unknown commands will receive a
//  504 Unknown Command response, the others will receive a 507 Unsupported Functionality one
void MGCPEngine::returnEvent(MGCPEvent* event)
{
    if (!event)
	return;
    DDebug(this,DebugInfo,"Event (%p) returned to the engine",event);
    MGCPTransaction* tr = event->transaction();
    const MGCPMessage* msg = event->message();
    if (tr && !tr->outgoing() && msg && msg->isCommand())
	tr->setResponse(knownCommand(msg->name()) ? 507 : 504);
    delete event;
}

// Terminate all transactions. Cancel all private threads if any and
// wait for them to terminate
void MGCPEngine::cleanup(bool gracefully, const char* text)
{
    DDebug(this,DebugAll,"Cleanup (gracefully=%s text=%s)",
	String::boolText(gracefully),text);

    // Terminate transactions
    Lock mylock(this);
    if (gracefully)
	for (ObjList* o = m_transactions.skipNull(); o; o = o->skipNext()) {
	    MGCPTransaction* tr = static_cast<MGCPTransaction*>(o->get());
	    if (!tr->outgoing())
		tr->setResponse(400,text);
	}
    m_transactions.clear();

    // Check if we have any private threads to wait
    if (!m_threads.skipNull())
	return;

    // Terminate private threads
    Debug(this,DebugAll,"Terminating %u private threads",m_threads.count());
    ListIterator iter(m_threads);
    for (GenObject* o = 0; 0 != (o = iter.get());)
	static_cast<MGCPPrivateThread*>(o)->cancel(!gracefully);
    DDebug(this,DebugAll,"Waiting for private threads to terminate");
    u_int64_t maxWait = Time::now() + 2000000;
    while (m_threads.skipNull()) {
	mylock.drop();
	if (Time::now() > maxWait) {
	    Debug(this,DebugGoOn,"Private threads did not terminate!");
	    return;
	}
	Thread::idle();
	mylock.acquire(this);
    }
    DDebug(this,DebugAll,"Private threads terminated");
}

// Write data to socket
bool MGCPEngine::sendData(const String& msg, const SocketAddr& address)
{
    if (debugAt(DebugInfo)) {
	SocketAddr local;
	m_socket.getSockName(local);
	Debug(this,DebugInfo,
	    "Sending message from %s:%d to %s:%d\r\n-----\r\n%s\r\n-----",
	    local.host().c_str(),local.port(),address.host().c_str(),address.port(),
	    msg.c_str());
    }

    int len = m_socket.sendTo(msg.c_str(),msg.length(),address);
    if (len != Socket::socketError())
	return true;
    if (!m_socket.canRetry())
	Alarm(this,"socket",DebugWarn,"Socket write error: %d: %s",
	    m_socket.error(),::strerror(m_socket.error()));
    else
	DDebug(this,DebugMild,"Socket temporary unavailable: %d: %s",
	    m_socket.error(),::strerror(m_socket.error()));
    return false;
}

// Append a transaction to the list
void MGCPEngine::appendTrans(MGCPTransaction* trans)
{
    if (!trans)
	return;
    Lock lock(this);
    DDebug(this,DebugAll,"Added transaction (%p)",trans);
    m_transactions.append(trans);
}

// Remove a transaction from the list
void MGCPEngine::removeTrans(MGCPTransaction* trans, bool del)
{
    if (!trans)
	return;
    Lock lock(this);
    DDebug(this,DebugAll,"Removed transaction (%p) del=%u",trans,del);
    m_transactions.remove(trans,del);
}

// Append a private thread to the list
void MGCPEngine::appendThread(MGCPPrivateThread* thread)
{
    if (!thread)
	return;
    Lock lock(this);
    m_threads.append(thread)->setDelete(false);
    XDebug(this,DebugAll,"Added private thread (%p)",thread);
}

// Remove private thread from the list without deleting it
void MGCPEngine::removeThread(MGCPPrivateThread* thread)
{
    if (!thread)
	return;
    Lock lock(this);
    m_threads.remove(thread,false);
    XDebug(this,DebugAll,"Removed private thread (%p)",thread);
}

// Process ACK received with a message or response
// Build an ACK message for each of responded incoming transaction
unsigned int* MGCPEngine::decodeAck(const String& param, unsigned int& count)
{
    ObjList* list = param.split(',',false);
    if (!list->count()) {
	TelEngine::destruct(list);
	return 0;
    }

    unsigned int maxArray = 0;
    unsigned int* array = 0;
    bool ok = true;
    int first, last;

    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	s->trimBlanks();
	// Get the interval (may be a single value)
	int sep = s->find('-');
	if (sep == -1)
	    first = last = s->toInteger(-1);
	else {
	    first = s->substr(0,sep).toInteger(-1);
	    last = s->substr(sep + 1).toInteger(-2);
	}
	if (first < 0 || last < 0 || last < first) {
	    ok = false;
	    break;
	}
	// Resize and copy array if not enough room
	unsigned int len = (unsigned int)(last - first + 1);
	if (count + len > maxArray) {
	    maxArray = count + len;
	    unsigned int* tmp = new unsigned int[maxArray];
	    if (array) {
		::memcpy(tmp,array,sizeof(unsigned int) * count);
		delete[] array;
	    }
	    array = tmp;
	}
	// Add to array code list
	for (; first <= last; first++)
	    array[count++] = first;
    }
    TelEngine::destruct(list);

    if (ok && count)
	return array;
    count = 0;
    if (array)
	delete[] array;
    return 0;
}


/**
 * MGCPEvent
 */
// Constructs an event from a transaction
MGCPEvent::MGCPEvent(MGCPTransaction* trans, MGCPMessage* msg)
    : m_transaction(0),
    m_message(0)
{
    if (trans && trans->ref())
	m_transaction = trans;
    if (msg && msg->ref())
	m_message = msg;
}

// Delete the message. Notify and deref the transaction
MGCPEvent::~MGCPEvent()
{
    if (m_transaction) {
	m_transaction->eventTerminated(this);
	m_transaction->deref();
    }
    TelEngine::destruct(m_message);
}


/**
 * The list of known commands defined in RFC 3435
 */
TokenDict MGCPEngine::mgcp_commands[] = {
    {"EPCF", 1},                         // CA --> GW  EndpointConfiguration
    {"CRCX", 2},                         // CA --> GW  CreateConnection
    {"MDCX", 3},                         // CA --> GW  ModifyConnection
    {"DLCX", 4},                         // CA <--> GW DeleteConnection
    {"RQNT", 5},                         // CA --> GW  NotificationRequest
    {"AUEP", 6},                         // CA --> GW  AuditEndpoint
    {"AUCX", 7},                         // CA --> GW  AuditConnection
    {"RSIP", 8},                         // GW --> CA  RestartInProgress
    {"NTFY", 9},                         // GW --> CA  Notify
    {"MESG", 10},                        // GW --> CA  Message
    {0,0}
};

/**
 * The list of known responses defined in RFC 3435 2.4
 */
TokenDict MGCPEngine::mgcp_responses[] = {
    {"ACK", 0},                          // Response Acknowledgement
    {"Trying", 100},                     // The transaction is currently being executed
    {"Queued", 101},                     // The transaction has been queued for execution
    {"OK", 200},                         // The requested transaction was executed normally
    {"OK", 250},                         // Used only to respond to DeleteConnection
    {"Unspecified", 400},                // The transaction could not be executed, due to some unspecified transient error
    {"Already Off Hook", 401},           // The phone is already off hook
    {"Already On Hook", 402},            // The phone is already on hook
    {"No Resources Now", 403},           // The transaction could not be executed, because the endpoint does
                                         //  not have sufficient resources at this time
    {"Insufficient Bandwidth", 404},
    {"Endpoint Is Restarting", 405},     // The transaction could not be executed, because the endpoint is restarting
    {"Timeout", 406},                    // The transaction did not complete in a reasonable period of time
    {"Aborted", 407},                    // The transaction was aborted by some external action,
                                         //  e.g., a ModifyConnection command aborted by a DeleteConnection command
    {"Overload", 409},                   // The transaction could not be executed because of internal overload
    {"No Endpoint Available", 410},      // A valid "any of" wildcard was used, but there was no endpoint
                                         //  available to satisfy the request
    {"Unknown Endpoint", 500},
    {"Endpoint Not Ready", 501},         // The endpoint is not ready. Includes out-of-service
    {"No Resources", 502},               // The endpoint doesn't have sufficient resources (permanent condition)
    {"Wildcard Too Complicated", 503},   // "All of" wildcard too complicated
    {"Unknown Command", 504},            // Unknown or unsupported command.
    {"Unsupported RemoteConnectionDescriptor", 505}, // This SHOULD be used when one or more mandatory parameters
                                         //  or values in the RemoteConnectionDescriptor is not supported
    {"Unable To Satisfy LocalConnectionOptions And RemoteConnectionDescriptor", 506}, // LocalConnectionOptions and
                                         //  RemoteConnectionDescriptor contain one or more mandatory parameters
                                         //  or values that conflict with each other
    {"Unsupported Functionality", 507},
    {"Unknown Or Unsupported Quarantine Handling", 508},
    {"Bad RemoteConnectionDescriptor", 509}, // Syntax or semantic error in the RemoteConnectionDescriptor
    {"Protocol Error", 510},             // Unspecified protocol error was detected
    {"Unrecognized Extension", 511},     // Used for unsupported critical parameter extensions ("X+")
    {"Can't Detect Event", 512},         // The gateway is not equipped to detect one of the requested events
    {"Can't Generate Signal", 513},      // The gateway is not equipped to generate one of the requested signals
    {"Can't Send Announcement", 514},    // The gateway cannot send the specified announcement.
    {"No Connection", 515},              // The transaction refers to an incorrect connection-id
    {"Bad Call-id", 516},                // Unknown or incorrect call-id (connection-id not associated with this call-id)
    {"Unsupported Mode", 517},           // Unsupported or invalid mode
    {"Unsupported Package", 518},
    {"No Digit Map", 519},               // Endpoint does not have a digit map
    {"Endpoint Is Restarting", 520},
    {"Endpoint Redirected To Another Call Agent", 521},  // The associated redirection behavior is only well-defined
                                         //  when this response is issued for a RestartInProgress command
    {"Unknown Event Or Signal", 522},    // The request referred to an event or signal that is not defined in
                                         //  the relevant package (which could be the default package)
    {"Illegal Action", 523},             // Unknown action or illegal combination of actions
    {"Inconsistency In LocalConnectionOptions", 524},
    {"Unknown Extension In LocalConnectionOptions", 525},  // Used for unsupported mandatory vendor extensions ("x+")
    {"Insufficient Bandwidth", 526},
    {"Missing RemoteConnectionDescriptor", 527},
    {"Incompatible Protocol Version", 528},
    {"Internal Hardware Failure", 529},
    {"CAS Signaling Protocol Error", 530},
    {"Grouping Of Trunks Failure", 531}, // e.g., facility failure
    {"Unsupported LocalConnectionOptions", 532}, // Unsupported value(s) in LocalConnectionOptions.
    {"Response Too Large", 533},
    {"Codec Negotiation Failure", 534},
    {"Packetization Period Not Supported", 535},
    {"Unsupported RestartMethod", 536},
    {"Unsupported Digit Map Extension", 537},
    {"Event/Signal Parameter Error", 538}, // e.g., missing, erroneous, unsupported, unknown, etc.
    {"Unsupported Command Parameter", 539}, // Used when the parameter is neither a package or vendor extension parameter
    {"Per Endpoint Connection Limit Exceeded", 540},
    {"Unsupported LocalConnectionOptions", 541}, // Used when the LocalConnectionOptions is neither a package
                                         //  nor a vendor extension LocalConnectionOptions.
    {0,0},
};

/**
 * The list of known reason codes defined in RFC 3435 2.5
 * Reason codes are used by the gateway when deleting a connection to
   inform the Call Agent about the reason for deleting the connection.
   They may also be used in a RestartInProgress command to inform the
   Call Agent of the reason for the RestartInProgress
 */
TokenDict MGCPEngine::mgcp_reasons[] = {
    {"Normal", 0},                       // Endpoint state is normal (only in response to audit requests)
    {"Endpoint Malfunctioning", 900},
    {"Endpoint Taken Out-Of-Service", 901},
    {"Loss Of Lower Layer Connectivity", 902},
    {"QoS Resource Reservation Was Lost", 903},
    {"Manual Intervention", 904},
    {"Facility failure", 905},
    {0,0},
};

/* vi: set ts=8 sw=4 sts=4 noet: */
