/**
 * transaction.cpp
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

using namespace TelEngine;

// Construct a transaction from its first message
MGCPTransaction::MGCPTransaction(MGCPEngine* engine, MGCPMessage* msg, bool outgoing,
	const SocketAddr& address, bool engineProcess)
    : Mutex(true,"MGCPTransaction"),
    m_state(Invalid),
    m_outgoing(outgoing),
    m_address(address),
    m_engine(engine),
    m_cmd(msg),
    m_provisional(0),
    m_response(0),
    m_ack(0),
    m_lastEvent(0),
    m_nextRetrans(0),
    m_crtRetransInterval(0),
    m_retransCount(0),
    m_timeout(false),
    m_ackRequest(true),
    m_private(0),
    m_engineProcess(engineProcess)
{
    if (m_engine) {
	ackRequest(m_engine->ackRequest());
	m_engine->appendTrans(this);
    }
    else {
	Debug(engine,DebugNote,"Can't create MGCP transaction without engine");
	return;
    }
    if (!(msg && msg->isCommand())) {
	Debug(engine,DebugNote,"Can't create MGCP transaction from response");
	return;
    }

    m_id = msg->transactionId();
    m_endpoint = m_cmd->endpointId();
    m_debug << "Transaction(" << (int)outgoing << "," << m_id << ")";

    DDebug(m_engine,DebugAll,"%s. cmd=%s ep=%s addr=%s:%d engineProcess=%u [%p]",
	m_debug.c_str(),m_cmd->name().c_str(),m_cmd->endpointId().c_str(),
	m_address.host().c_str(),m_address.port(),m_engineProcess,this);

    // Outgoing: send the message
    if (outgoing) {
	send(m_cmd);
	initTimeout(Time(),false);
    }
    else
	changeState(Initiated);
}

MGCPTransaction::~MGCPTransaction()
{
    DDebug(m_engine,DebugAll,"%s. Destroyed [%p]",m_debug.c_str(),this);
}

// Get an event from this transaction. Check timeouts
MGCPEvent* MGCPTransaction::getEvent(u_int64_t time)
{
    Lock lock(this);
    if (m_lastEvent)
	return 0;

    switch (state()) {
	case Initiated:
	    // Outgoing: Check if received any kind of response
	    //   Ignore a provisional response if we received a final one
	    //   Stop timer if received a final response
	    // Incoming: Process the received command
	    if (outgoing()) {
		m_lastEvent = checkResponse(time);
		if (!m_lastEvent && m_provisional) {
		    m_lastEvent = new MGCPEvent(this,m_provisional);
		    changeState(Trying);
		}
	    }
	    else {
		initTimeout(time,true);
		m_lastEvent = new MGCPEvent(this,m_cmd);
		if (m_engine && m_engine->provisional()) {
		    if (!m_provisional)
			m_provisional = new MGCPMessage(this,100);
		    send(m_provisional);
		}
		else
		    changeState(Trying);
	    }
	    break;
	case Trying:
	    // Outgoing: Check if received any response. If so, send a response ACK
	    // Incoming: Do nothing. Wait for the user to send a final response
	    if (outgoing())
		m_lastEvent = checkResponse(time);
	    break;
	case Responded:
	    // Outgoing: Change state to Ack. Should never be in this state
	    // Incoming: Check if we received a response ACK. Stop timer if received it
	    if (outgoing())
		changeState(Ack);
	    else {
		if (!m_ack)
		    break;
		m_lastEvent = new MGCPEvent(this,m_ack);
		m_nextRetrans = time + m_engine->extraTime();
		changeState(Ack);
	    }
	    break;
	case Ack:
	    // Just check timeouts
	    break;
	case Invalid:
	    m_lastEvent = terminate();
	    break;
	case Destroying:
	    break;
    }
    // Check timeouts
    if (!m_lastEvent)
	m_lastEvent = checkTimeout(time);

#ifdef DEBUG
    if (m_lastEvent) {
	const MGCPMessage* m = m_lastEvent->message();
	String s = m ? m->name() : String("");
	DDebug(m_engine,DebugAll,"%s. Generating event (%p) state=%u msg=%s [%p]",
	    m_debug.c_str(),m_lastEvent,state(),s.c_str(),this);
    }
#endif

    return m_lastEvent;
}

// Explicitely transmit a provisional code
bool MGCPTransaction::sendProvisional(int code, const char* comment)
{
    if (outgoing() || m_provisional || (state() >= Responded) || (code < 100) || (code > 199))
	return false;
    m_provisional = new MGCPMessage(this,code,comment);
    send(m_provisional);
    return true;
}

// Transmits a final response message if this is an incoming transaction
bool MGCPTransaction::setResponse(MGCPMessage* msg)
{
    Lock lock(this);

    // Check state, message, transaction direction. Also check if we already have a response
    bool msgValid = (msg && (msg->code() >= 200 || !msg->isCommand()));
    bool stateValid = (state() >= Initiated || state() <= Ack);
    if (m_response || outgoing() || !msgValid || !stateValid) {
	TelEngine::destruct(msg);
	return false;
    }

    DDebug(m_engine,DebugAll,"%s. Set response %s in state %u [%p]",
	m_debug.c_str(),msg->name().c_str(),state(),this);

    m_response = msg;
    if (m_ackRequest)
	// Force response ACK request
	m_response->params.setParam("K","");
    // Send and init timeout
    send(m_response);
    if (!m_ackRequest)
	changeState(Ack);
    initTimeout(Time(),false);
    return true;
}

// Transmits a final response message if this is an incoming transaction
bool MGCPTransaction::setResponse(int code, const NamedList* params, MimeSdpBody* sdp1,
	MimeSdpBody* sdp2)
{
    if (m_response || outgoing()) {
	TelEngine::destruct(sdp1);
	TelEngine::destruct(sdp2);
	return false;
    }
    const char* comment = 0;
    if (params)
	comment = params->c_str();
    MGCPMessage* msg = new MGCPMessage(this,code,comment);
    if (params) {
	unsigned int n = params->length();
	for (unsigned int i = 0; i < n; i++) {
	    const NamedString* p = params->getParam(i);
	    if (p)
		msg->params.addParam(p->name(),*p);
	}
    }
    if (sdp1) {
	msg->sdp.append(sdp1);
	if (sdp2)
	    msg->sdp.append(sdp2);
    }
    else
	TelEngine::destruct(sdp2);
    return setResponse(msg);
}

// Gracefully terminate this transaction. Release memory
void MGCPTransaction::destroyed()
{
    lock();
    if (state() != Destroying) {
	if (!outgoing() && !m_response)
	    setResponse(400);
	changeState(Destroying);
    }
    if (m_engine)
	m_engine->removeTrans(this,false);
    TelEngine::destruct(m_cmd);
    TelEngine::destruct(m_provisional);
    TelEngine::destruct(m_response);
    TelEngine::destruct(m_ack);
    unlock();
    RefObject::destroyed();
}

// Consume (process) a received message, other then the initiating one
void MGCPTransaction::processMessage(MGCPMessage* msg)
{
    if (!msg)
	return;
    Lock lock(this);
    if (state() < Initiated || state() > Ack) {
	bool cmd = msg->isCommand();
	Debug(m_engine,DebugInfo,"%s. Can't process %s %s in state %u [%p]",
	    m_debug.c_str(),msg->name().c_str(),cmd ? "command":"response",
	    state(),this);
	TelEngine::destruct(msg);
	return;
    }

    // Process commands
    if (msg->isCommand()) {
	// Commands can be received only by incoming transactions
        // Check for retransmission
	if (outgoing() || msg->name() != m_cmd->name()) {
	    Debug(m_engine,DebugNote,"%s. Can't accept %s [%p]",
		m_debug.c_str(),msg->name().c_str(),this);
	    TelEngine::destruct(msg);
	    return;
	}

	// Retransmit the last response
	DDebug(m_engine,DebugAll,
	    "%s. Received command retransmission in state %u [%p]",
	    m_debug.c_str(),state(),this);
	if (state() == Trying)
	    send(m_provisional);
	else if (state() == Responded)
	    send(m_response);
	// If state is Initiated, wait for getEvent to process the received command
	// Send nothing if we received the ACK to our final response
	TelEngine::destruct(msg);
	return;
    }

    // Process responses
    if (msg->isResponse()) {
	// Responses can be received only by outgoing transactions
	if (!outgoing()) {
	    Debug(m_engine,DebugNote,
		"%s. Can't accept response %d [%p]",
		m_debug.c_str(),msg->code(),this);
	    TelEngine::destruct(msg);
	    return;
	}

	// Check response
	// Send ACK for final response tretransmissions
	// Don't accept different final responses
	// Don't accept provisional responses after final responses
	// Don't accept different provisional responses
	bool ok = true;
	if (msg->code() >= 200) {
	    bool retrans = false;
	    ok = !m_response;
	    if (ok)
		m_response = msg;
	    else if (m_response->code() == msg->code()) {
		retrans = true;
		send(m_ack);
	    }
	    DDebug(m_engine,(ok || retrans) ? DebugAll : DebugNote,
		"%s. Received %sresponse %d [%p]",m_debug.c_str(),
		ok?"":(retrans?"retransmission for ":"different "),msg->code(),this);
	}
	else {
	    ok = (!m_response && !m_provisional);
	    if (ok)
		m_provisional = msg;
	    DDebug(m_engine,(ok || m_response)? DebugAll : DebugNote,
		"%s. Received %sprovisional response %d [%p]",m_debug.c_str(),
		ok?"":(m_response?"late ":"different "),msg->code(),this);
	}

	if (!ok)
	    TelEngine::destruct(msg);
	return;
    }

    // Process response ACK
    if (msg->isAck()) {
	// Responses can be received only by outgoing transactions
	if (outgoing()) {
	    Debug(m_engine,DebugNote,"%s. Can't accept response ACK [%p]",
		m_debug.c_str(),this);
	    TelEngine::destruct(msg);
	    return;
	}

	// Keep the ACK if not already received one
	if (state() == Responded && !m_ack) {
	    m_ack = msg;
	    return;
	}

	Debug(m_engine,DebugNote,
	    "%s. Ignoring response ACK in state %u [%p]",
	    m_debug.c_str(),state(),this);
	TelEngine::destruct(msg);
	return;
    }

    // !!! Unknown message type
    TelEngine::destruct(msg);
}

// Check timeouts. Manage retransmissions
MGCPEvent* MGCPTransaction::checkTimeout(u_int64_t time)
{
    if (!m_nextRetrans || time < m_nextRetrans)
	return 0;

    // Terminate transaction if we have nothing to retransmit:
    // Outgoing: Initiated: retransmit command. Trying: adjust timeout
    // Incoming: Responded: retransmit response
    while (m_retransCount) {
	if ((outgoing() && state() != Initiated && state() != Trying) ||
	    (!outgoing() && state() != Responded))
	    break;

	MGCPMessage* m = 0;
	if (state() == Initiated)
	    m = m_cmd;
	else if (state() == Trying)
	    ;
	else
	    m = m_response;

	m_crtRetransInterval *= 2;
	m_retransCount--;
	m_nextRetrans = time + m_crtRetransInterval;

	if (m) {
	    send(m);
	    Debug(m_engine,DebugInfo,"%s. Retransmitted %s remaining=%u [%p]",
		m_debug.c_str(),m->name().c_str(),m_retransCount,this);
	}
	else
	    Debug(m_engine,DebugAll,"%s. Adjusted timeout remaining=%u [%p]",
		m_debug.c_str(),m_retransCount,this);

	return 0;
    }

    m_timeout = (state() == Initiated || state() == Trying);
    if (m_timeout)
	engine()->timeout(this);
    return terminate();
}

// Event termination notification
void MGCPTransaction::eventTerminated(MGCPEvent* event)
{
    if (event != m_lastEvent)
	return;
    DDebug(m_engine,DebugAll,"%s. Event (%p) terminated [%p]",m_debug.c_str(),event,this);
    m_lastEvent = 0;
}

// Change transaction's state if the new state is a valid one
void MGCPTransaction::changeState(State newState)
{
    if (newState <= m_state)
	return;
    DDebug(m_engine,DebugInfo,"%s. Changing state from %u to %u [%p]",
	m_debug.c_str(),m_state,newState,this);
    m_state = newState;
}

// (Re)send one the initial, provisional or final response. Change transaction's state
void MGCPTransaction::send(MGCPMessage* msg)
{
    if (!(msg && m_engine))
	return;
    if (msg == m_cmd)
	changeState(Initiated);
    else if (msg == m_provisional)
	changeState(Trying);
    else if (msg == m_response)
	changeState(Responded);
    else if (msg == m_ack)
	changeState(Ack);
    else
	return;
    String tmp;
    msg->toString(tmp);
    m_engine->sendData(tmp,m_address);
}

// Check if received any final response. Create an event. Init timeout.
// Send a response ACK if requested by the response
MGCPEvent* MGCPTransaction::checkResponse(u_int64_t time)
{
    if (!m_response)
	return 0;
    if (m_response->params.getParam(YSTRING("k")) ||
	m_response->params.getParam(YSTRING("K"))) {
	m_ack = new MGCPMessage(this,0);
	send(m_ack);
    }
    initTimeout(time,true);
    changeState(Responded);
    return new MGCPEvent(this,m_response);
}

// Init timeout for retransmission or transaction termination
void MGCPTransaction::initTimeout(u_int64_t time, bool extra)
{
    if (!extra) {
	m_crtRetransInterval = m_engine->retransInterval();
	m_retransCount = m_engine->retransCount();
    }
    else {
	m_crtRetransInterval = (unsigned int)m_engine->extraTime();
	m_retransCount = 0;
    }
    m_nextRetrans = time + m_crtRetransInterval;
}

// Remove from engine. Create event. Deref the transaction
MGCPEvent* MGCPTransaction::terminate()
{
    if (m_engine)
	m_engine->removeTrans(this,false);
    if (m_timeout)
	Debug(m_engine,DebugNote,"%s. Timeout in state %u [%p]",m_debug.c_str(),state(),this);
#ifdef DEBUG
    else
	Debug(m_engine,DebugAll,"%s. Terminated in state %u [%p]",m_debug.c_str(),state(),this);
#endif
    MGCPEvent* event = new MGCPEvent(this);
    deref();
    return event;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
