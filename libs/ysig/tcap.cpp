/**
 * tcap.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Signalling Stack - implements the support for SS7, ISDN and PSTN
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

#include "yatesig.h"
#include "yateasn.h"


using namespace TelEngine;

#ifdef DEBUG
static void dumpData(int debugLevel, SS7TCAP* tcap, String message, void* obj, NamedList& params,
		    DataBlock data = DataBlock::empty())
{
    if (tcap) {
	String tmp;
	params.dump(tmp,"\r\n  ",'\'',true);
	String str;
	str.hexify(data.data(),data.length(),' ');
	Debug(tcap,debugLevel,"%s [%p] - \r\nparams='%s',\r\ndata='%s'",
	    message.safe(),obj,tmp.c_str(),str.c_str());
    }
}
#endif

TCAPUser::~TCAPUser()
{
    Debug(this,DebugAll,"TCAPUser::~TCAPUser() [%p] - tcap user destroyed",this);
}

void TCAPUser::destroyed()
{
    Debug(this,DebugAll,"TCAPUser::destroyed() [%p]",this);
    Lock lock(m_tcapMtx);
    if (m_tcap) {
	// notify SCCP OutOfService
	NamedList p("");
	m_tcap->updateUserStatus(this,SCCPManagement::UserOutOfService,p);

	m_tcap->detach(this);
	Debug(this,DebugAll,"TCAPUser::~TCAPUser() [%p] - Detached from TCAP (%p,%s)",this,m_tcap,m_tcap->toString().safe());
	m_tcap->deref();
	m_tcap = 0;
    }
    lock.drop();
    SignallingComponent::destroyed();
}

void TCAPUser::attach(SS7TCAP* tcap)
{
    Lock lock(m_tcapMtx);

    if (m_tcap == tcap)
	return;
    SS7TCAP* tmp = m_tcap;
    m_tcap = tcap;
    lock.drop();
    DDebug(this,DebugAll,"TCAPUser::attach(tcap=%s [%p], replacing tcap=%s [%p] [%p]",(m_tcap ? m_tcap->toString().safe() : ""),m_tcap,
	    (tmp ? tmp->toString().c_str() : ""),tmp,this);
    if (tmp) {
	tmp->detach(this);
	Debug(this,DebugAll,"TCAPUser::attach() - Detached from TCAP (%p,%s) [%p]",tmp,tmp->toString().safe(),this);
	tmp->deref();
	tmp = 0;
    }
    if (!tcap)
	return;

    tcap->attach(this);
    tcap->ref();
    Debug(this,DebugAll,"Attached to TCAP (%p,%s) [%p] tcapRefCount=%d",tcap,tcap->toString().safe(),this,tcap->refcount());
}

bool TCAPUser::tcapIndication(NamedList& params)
{
    Debug(this,DebugStub,"Please implement TCAPUser::tcapIndication()");
    return false;
}

bool TCAPUser::managementNotify(SCCP::Type type, NamedList& params)
{
    Debug(this,DebugStub,"Please implement TCAPUser::managementNotify()");
    return false;
}

int TCAPUser::managementState()
{
    return SCCPManagement::UserOutOfService;
}

struct PrimitiveMapping {
    int primitive;
    int mappedTo;
};

static bool s_extendedDbg = false;
static bool s_printMsgs = false;
static const String s_checkAddr = "tcap.checkAddress";
static const String s_localPC = "LocalPC";
static const String s_remotePC = "RemotePC";
static const String s_callingPA = "CallingPartyAddress";
static const String s_callingSSN = "CallingPartyAddress.ssn";
static const String s_callingRoute = "CallingPartyAddress.route";
static const String s_calledPA = "CalledPartyAddress";
static const String s_calledSSN = "CalledPartyAddress.ssn";
static const String s_HopCounter = "HopCounter";

// TCAP message parameters
static const String s_tcapUser = "tcap.user";
static const String s_tcapBasicTerm = "tcap.transaction.terminationBasic";
static const String s_tcapEndNow = "tcap.transaction.endNow";
static const String s_tcapRequest = "tcap.request.type";
static const String s_tcapRequestError = "tcap.request.error";
static const String s_tcapTransPrefix = "tcap.transaction";
static const String s_tcapMsgType = "tcap.transaction.messageType";
static const String s_tcapLocalTID = "tcap.transaction.localTID";
static const String s_tcapRemoteTID = "tcap.transaction.remoteTID";
static const String s_tcapAbortCause = "tcap.transaction.abort.cause";
static const String s_tcapAbortInfo = "tcap.transaction.abort.information";

static const String s_tcapDialogPrefix = "tcap.dialogPDU";
static const String s_tcapProtoVers = "tcap.dialogPDU.protocol-version";
static const String s_tcapIntAppID = "tcap.dialogPDU.integerApplicationId";
static const String s_tcapObjAppID = "tcap.dialogPDU.objectApplicationId";
static const String s_tcapIntSecID = "tcap.dialogPDU.integerSecurityId";
static const String s_tcapObjSecID = "tcap.dialogPDU.objectSecurityId";
static const String s_tcapIntConfidID = "tcap.dialogPDU.integerConfidentialityId";
static const String s_tcapObjConfidID = "tcap.dialogPDU.objectConfidentialityId";
static const String s_tcapReference = "tcap.dialogPDU.userInformation.direct-reference";
static const String s_tcapDataDesc = "tcap.dialogPDU.userInformation.data-descriptor";
static const String s_tcapEncodingContent = "tcap.dialogPDU.userInformation.encoding-contents";
static const String s_tcapEncodingType = "tcap.dialogPDU.userInformation.encoding-type";

static const String s_tcapCompCount = "tcap.component.count";
static const String s_tcapCompPrefix = "tcap.component";
static const String s_tcapLocalCID = "localCID";
static const String s_tcapRemoteCID = "remoteCID";
static const String s_tcapCompType = "componentType";
static const String s_tcapOpCodeType = "operationCodeType";
static const String s_tcapOpCode = "operationCode";
static const String s_tcapErrCodeType = "errorCodeType";
static const String s_tcapErrCode = "errorCode";
static const String s_tcapProblemCode = "problemCode";
static const String s_tcapPayload = "payload";


static void populateSCCPAddress(NamedList& localAddr, NamedList& remoteAddr, NamedList& initParams, bool initLocal,
	bool keepPrefix = false)
{
    String localParam(initLocal ? s_callingPA : s_calledPA);
    String remoteParam(initLocal ? s_calledPA : s_callingPA);

    NamedList aux("");
    aux.copySubParams(initParams,localParam + ".");
    if (keepPrefix) {
	for (unsigned int i = 0; i < aux.count(); i++) {
	    NamedString* p = aux.getParam(i);
	    if (!TelEngine::null(p)) {
		localAddr.setParam(remoteParam + "." +  p->name(),*p);
	    }
	}
    }
    else
	localAddr.copyParams(aux);
    if (!TelEngine::null(initParams.getParam(s_localPC)))
	localAddr.copyParam(initParams,s_localPC);

    aux.clearParams();
    aux.copySubParams(initParams,remoteParam + ".");
    if (keepPrefix) {
	for (unsigned int i = 0; i < aux.count(); i++) {
	    NamedString* p = aux.getParam(i);
	    if (!TelEngine::null(p)) {
		remoteAddr.setParam(localParam + "." +  p->name(),*p);
	    }
	}
    }
    else
	remoteAddr.copyParams(aux);
    if (!TelEngine::null(initParams.getParam(s_remotePC)))
	remoteAddr.copyParam(initParams,s_remotePC);
}

static void compPrefix(String& prefix, unsigned int index, bool endSep = false)
{
    prefix = s_tcapCompPrefix;
    prefix << "." << index << (endSep ? "." : "");
}

/**
 * SS7TCAP implementation
 */
const TokenDict SS7TCAP::s_tcapVersion[] = {
    {"UnknownTCAP", SS7TCAP::UnknownTCAP},
    {"ITU-T TCAP",  SS7TCAP::ITUTCAP},
    {"ANSI TCAP",   SS7TCAP::ANSITCAP},
    {0,-1},
};

const TokenDict SS7TCAP::s_compPrimitives[] = {
    {"Invoke",           SS7TCAP::TC_Invoke},
    {"ResultLast",       SS7TCAP::TC_ResultLast},
    {"U_Error",          SS7TCAP::TC_U_Error},
    {"U_Reject",         SS7TCAP::TC_U_Reject},
    {"R_Reject",         SS7TCAP::TC_R_Reject},
    {"L_Reject",         SS7TCAP::TC_L_Reject},
    {"InvokeNotLast",    SS7TCAP::TC_InvokeNotLast},
    {"ResultNotLast",    SS7TCAP::TC_ResultNotLast},
    {"L_Cancel",         SS7TCAP::TC_L_Cancel},
    {"U_Cancel",         SS7TCAP::TC_U_Cancel},
    {"TimerReset",       SS7TCAP::TC_TimerReset},
    {0,0},
};

const TokenDict SS7TCAP::s_transPrimitives[] = {
    {"Unidirectional",             SS7TCAP::TC_Unidirectional},
    {"Begin",                      SS7TCAP::TC_Begin},
    {"QueryWithPerm",              SS7TCAP::TC_QueryWithPerm},
    {"QueryWithoutPerm",           SS7TCAP::TC_QueryWithoutPerm},
    {"Continue",                   SS7TCAP::TC_Continue},
    {"ConversationWithPerm",       SS7TCAP::TC_ConversationWithPerm},
    {"ConversationWithoutPerm",    SS7TCAP::TC_ConversationWithoutPerm},
    {"End",                        SS7TCAP::TC_End},
    {"Response",                   SS7TCAP::TC_Response},
    {"U_Abort",                    SS7TCAP::TC_U_Abort},
    {"P_Abort",                    SS7TCAP::TC_P_Abort},
    {"Notice",                     SS7TCAP::TC_Notice},
    {"Unknown",                    SS7TCAP::TC_Unknown},
    {0,0},
};

const TokenDict SS7TCAP::s_compOperClasses[] = {
    {"reportAll",        SS7TCAP::SuccessOrFailureReport},
    {"reportFail",       SS7TCAP::FailureOnlyReport},
    {"reportSuccess",    SS7TCAP::SuccessOnlyReport},
    {"reportNone",       SS7TCAP::NoReport},
};


SS7TCAP::SS7TCAP(const NamedList& params)
    : SCCPUser(params),
      m_usersMtx(true,"TCAPUsers"),
      m_inQueueMtx(true,"TCAPPendingMsg"),
      m_SSN(0),
      m_defaultRemoteSSN(0),
      m_defaultHopCounter(0),
      m_defaultRemotePC(0),
      m_remoteTypePC(SS7PointCode::Other),
      m_trTimeout(300),
      m_transactionsMtx(true,"TCAPTransactions"),
      m_tcapType(UnknownTCAP),
      m_idsPool(0)
{
    Debug(this,DebugAll,"SS7TCAP::SS7TCAP() [%p] created",this);
    m_recvMsgs = m_sentMsgs = m_discardMsgs = m_normalMsgs = m_abnormalMsgs = 0;
    m_ssnStatus = SCCPManagement::UserOutOfService;
}

SS7TCAP::~SS7TCAP()
{
    Debug(this,DebugAll,"SS7TCAP::~SS7TCAP() [%p] destroyed, refCount=%d, usersCount=%d",this,refcount(),m_users.count());
    if (m_users.count()) {
	Debug(this,DebugGoOn,"SS7TCAP destroyed while having %d user(s) still attached [%p]",m_users.count(),this);
	ListIterator iter(m_users);
	for (;;) {
	    TCAPUser* user = static_cast<TCAPUser*>(iter.get());
	    // End of iteration?
	    if (!user)
		break;
	    if(user->tcap())
		user->setTCAP(0);
	}
	m_users.setDelete(false);
    }
    m_transactions.clear();
    m_inQueue.clear();

}

bool SS7TCAP::initialize(const NamedList* config)
{
#ifdef DEBUG
    if(config && debugAt(DebugAll)) {
	String tmp;
	config->dump(tmp,"\r\n  ",'\'',true);
	Debug(this,DebugAll,"SS7TCAP::initialize([%p]) [%p] for configuration '%s'",config,this,tmp.c_str());
    }
#endif
    if (config) {
	// read local point code and default remote point code
	m_SSN = config->getIntValue(YSTRING("local_SSN"),-1);
	m_defaultRemoteSSN = config->getIntValue(YSTRING("default_remote_SSN"),-1);
	m_defaultHopCounter = config->getIntValue(YSTRING("default_hopcounter"),0);
	if (m_defaultHopCounter > 15 || config->getBoolValue(YSTRING("default_hopcounter")))
	    m_defaultHopCounter = 15;

	const char* code = config->getValue(YSTRING("default_remote_pointcode"));
	m_remoteTypePC = SS7PointCode::lookup(config->getValue(YSTRING("pointcodetype"),""));
	if (!(m_defaultRemotePC.assign(code,m_remoteTypePC) && m_defaultRemotePC.pack(m_remoteTypePC))) {
	    int codeInt = config->getIntValue(YSTRING("default_remote_pointcode"));
	    if (!m_defaultRemotePC.unpack(m_remoteTypePC,codeInt))
		Debug(this,DebugMild,"SS7TCAP::initialize([%p]) [%p] - Invalid default_remote_pointcode=%s value configured",
		    config,this,code);
	}

	m_trTimeout = config->getIntValue(YSTRING("transact_timeout"),m_trTimeout / 1000) * 1000; // seconds to miliseconds
	s_printMsgs = config->getBoolValue(YSTRING("print-messages"),false);
	s_extendedDbg = config->getBoolValue(YSTRING("extended-debug"),false);
    }
    bool ok = SCCPUser::initialize(config);
    if (ok) {
	NamedList p("");
	sendSCCPNotify(p);
	Debug(this,DebugInfo,"SSN=%d has status='%s'[%p]",m_SSN,lookup(m_ssnStatus,SCCPManagement::broadcastType(),""),this);
    }
    return ok;
}

bool SS7TCAP::sendData(DataBlock& data, NamedList& params)
{
    if (params.getBoolValue(s_callingSSN))
	params.setParam(s_callingSSN,String(m_SSN));
    if (params.getBoolValue(s_checkAddr,true)) {
	String dpc = params.getValue(s_remotePC,"");
	unsigned int pc = m_defaultRemotePC.pack(m_remoteTypePC);
	if (dpc.null() && pc)
	    params.addParam(s_remotePC,String(pc));
	int ssn = params.getIntValue(s_calledSSN,-1);
	if (ssn < 0 && m_defaultRemoteSSN <= 255)
	    params.setParam(s_calledSSN,String(m_defaultRemoteSSN));
	ssn = params.getIntValue(s_callingSSN,-1);
	if (ssn < 0 && m_SSN <= 255) {
	    params.setParam(s_callingSSN,String(m_SSN));
	    if (!params.getParam(s_callingRoute))
		params.addParam(s_callingRoute,"ssn");
	}
	if (m_defaultHopCounter && !params.getParam(s_HopCounter))
	    params.addParam(s_HopCounter,String(m_defaultHopCounter));
    }
#ifdef DEBUG
    if (s_printMsgs && debugAt(DebugInfo))
	dumpData(DebugInfo,this,"Sending to SCCP : ",this,params,data);
#endif
    return SCCPUser::sendData(data,params);
}

HandledMSU SS7TCAP::receivedData(DataBlock& data, NamedList& params)
{
    HandledMSU result;
    if (!data.length())
	return result;
#ifdef DEBUG
    if (s_printMsgs && debugAt(DebugInfo))
	dumpData(DebugInfo,this,"Received from SCCP: ",this,params,data);
#endif
    unsigned int cpaSSN = params.getIntValue(s_calledSSN,0);
    unsigned int ssn = params.getIntValue("ssn",0);
    if (m_SSN != cpaSSN && m_SSN != ssn)
	return result;
    enqueue(new SS7TCAPMessage(params,data));
    result = HandledMSU::Accepted;
    return result;
}

HandledMSU SS7TCAP::notifyData(DataBlock& data, NamedList& params)
{
    HandledMSU result;
#ifdef DEBUG
    if (s_printMsgs && debugAt(DebugInfo))
	dumpData(DebugInfo,this,"Received notify from SCCP: ",this,params,data);
#endif
    enqueue(new SS7TCAPMessage(params,data,true));
    return result;
}

bool SS7TCAP::managementNotify(SCCP::Type type, NamedList& params)
{
    Lock lock(m_usersMtx);
    ListIterator iter(m_users);
    bool ok = false;

    if (type == SCCP::SubsystemStatus && m_SSN != (unsigned int)params.getIntValue("ssn")) {
	params.setParam("subsystem-status","UserOutOfService");
	return true;
    }
    bool inService = false;
    for (;;) {
	TCAPUser* user = static_cast<TCAPUser*>(iter.get());
	// End of iteration?
	if (!user)
	    break;
	if (user->managementNotify(type,params))
	    ok = true;
	if (user->managementState() == (int) SCCPManagement::UserInService)
	    inService = true;
    }
    if (type == SCCP::SubsystemStatus)
	params.setParam("subsystem-status",(inService ? "UserInService" : "UserOutOfService"));
    return ok;
}

void SS7TCAP::updateUserStatus(TCAPUser* user, SCCPManagement::LocalBroadcast status, NamedList& params)
{
    if (!user)
	return;
    DDebug(this,DebugAll,"SS7TCAP::updateUserStatus(user=%s[%p],status=%d) [%p]",user->toString().c_str(),user,status,this);
    bool notify = false;
    Lock l(m_usersMtx);
    SCCPManagement::LocalBroadcast tmp = m_ssnStatus;
    switch (m_ssnStatus) {
	case SCCPManagement::UserOutOfService:
	    if (status == SCCPManagement::UserInService) {
		m_ssnStatus = SCCPManagement::UserInService;
		notify = true;
	    }
	    break;
	case SCCPManagement::UserInService:
	    if (status == SCCPManagement::UserOutOfService) {
		ListIterator it(m_users);
		for (;;) {
		    TCAPUser* usr = static_cast<TCAPUser*>(it.get());
		    // End of iteration?
		    if (!usr) {
			m_ssnStatus = SCCPManagement::UserOutOfService;
			notify = true;
			break;
		    }
		    if (usr->managementState() == (int) SCCPManagement::UserInService)
			break;
		}
	    }
	default:
	    break;
    }

    if (notify) {
	sendSCCPNotify(params); // it always returns false, so no point in checking result
	Debug(this,DebugInfo,"SSN=%d changed status from '%s' to '%s' [%p]",m_SSN,
		  lookup(tmp,SCCPManagement::broadcastType(),""),lookup(m_ssnStatus,SCCPManagement::broadcastType(),""),this);
    }
}

bool SS7TCAP::sendSCCPNotify(NamedList& params)
{
    params.setParam(YSTRING("subsystem-status"),lookup(m_ssnStatus,SCCPManagement::broadcastType(),""));
    params.setParam(YSTRING("ssn"),String(m_SSN));
    if (!params.getParam(YSTRING("smi")))
	    params.setParam("smi","0");
    return sccpNotify(SCCP::StatusRequest,params);
}

void SS7TCAP::attach(TCAPUser* user)
{
    if (!user)
	return;
    DDebug(this,DebugAll,"SS7TCAP::attach(user=%s [%p]) [%p]",user->toString().safe(),user,this);
    Lock l(m_usersMtx);
    if (m_users.find(user))
	return;
    m_users.append(user);
    Debug(this,DebugAll,"SS7TCAP '%s'[%p] attached user=%s [%p]",toString().safe(),this,user->toString().safe(),user);
}

void SS7TCAP::detach(TCAPUser* user)
{
    if (!user)
	return;
    DDebug(this,DebugAll,"SS7TCAP::detach(user=%s [%p]) [%p], refCount=%d",user->toString().safe(),user,this,refcount());
    Lock l(m_usersMtx);
    if (m_users.find(user)) {
	m_users.remove(user,false);
	Debug(this,DebugAll,"SS7TCAP '%s'[%p] detached user=%s [%p], refCount=%d",toString().safe(),this,user->toString().c_str(),user,refcount());
    }
}

void SS7TCAP::enqueue(SS7TCAPMessage* msg)
{
    if (!msg)
	return;
    Lock lock(m_inQueueMtx);
    m_inQueue.append(msg);
    XDebug(this,DebugAll,"SS7TCAP::enqueue(). Enqueued transaction wrapper (%p) [%p]",msg,this);
}

SS7TCAPMessage* SS7TCAP::dequeue()
{
    Lock lock(m_inQueueMtx,SignallingEngine::maxLockWait());
    if (!lock.locked())
	return 0;
    ObjList* obj = m_inQueue.skipNull();
    if (!obj)
	return 0;
    SS7TCAPMessage* msg = static_cast<SS7TCAPMessage*>(obj->get());
    m_inQueue.remove(msg,false);
    XDebug(this,DebugAll,"SS7TCAP::dequeue(). Dequeued transaction wrapper (%p) [%p]",msg,this);
    return msg;
}

void SS7TCAP::allocTransactionID(String& str)
{
    u_int32_t tmp = m_idsPool;
    m_idsPool++;
    unsigned char buff[sizeof(tmp)];
    int len = sizeof(tmp);
    for (int index = len - 1; index >= 0; index--) {
	buff[index] = tmp & 0xff;
	tmp >>= 8;
    }
    str.hexify(buff,len,' ');
    XDebug(this,DebugAll,"SS7TCAP::allocTransactionID() - allocated new transaction ID=%s [%p]",str.c_str(),this);
}

const String SS7TCAP::allocTransactionID()
{
    String str;
    allocTransactionID(str);
    return str;
}

bool SS7TCAP::sendToUser(NamedList& params)
{
    String userName = params.getValue(s_tcapUser,""); // if it has a specified user, send it to that user
    Lock lock(m_usersMtx);
    if (!userName.null()) {
	ObjList* obj = m_users.find(userName);
	if (!obj) {
	    Debug(this,DebugInfo,"SS7TCAP::sendToUser() [%p] - failed to send message with id=%s to user=%s,"
		" no such application",this,params.getValue(s_tcapLocalTID),userName.c_str());
	    return false;
	}
	TCAPUser* user = static_cast<TCAPUser*>(obj->get());
	if (!user) {
	    Debug(this,DebugInfo,"SS7TCAP::sendToUser() [%p] - failed to send message with id=%s to user,%s"
		" no such application",this,params.getValue(s_tcapLocalTID),userName.c_str());
	    return false;
	}
#ifdef DEBUG
	if (s_printMsgs && debugAt(DebugInfo))
	    dumpData(DebugInfo,this,"Sent to TCAP user: ",this,params);
#endif
	return user->tcapIndication(params);
    }
    else {
	ListIterator iter(m_users);
	for (;;) {
	    TCAPUser* user = static_cast<TCAPUser*>(iter.get());
	    // End of iteration?
	    if (!user) {
		Debug(this,DebugInfo,"SS7TCAP::sendToUser() [%p] - failed to send message with id=%s to any user",
			this,params.getValue(s_tcapLocalTID));
		return false;
	    }
	    if (user->tcapIndication(params)) {
		params.setParam(s_tcapUser,user->toString()); // set the user for this transaction
#ifdef DEBUG
		if (s_printMsgs && debugAt(DebugInfo))
		    dumpData(DebugInfo,this,"Sent to TCAP user: ",this,params);
#endif
		break;
	    }
	}
    }
    return true;
}

void SS7TCAP::status(NamedList& status)
{
    status.setParam("totalIncoming",String(m_recvMsgs));
    status.setParam("totalOutgoing",String(m_sentMsgs));
    status.setParam("totalDiscarded",String(m_discardMsgs));
    status.setParam("totalNormal",String(m_normalMsgs));
    status.setParam("totalAbnormal",String(m_abnormalMsgs));
}

void SS7TCAP::userStatus(NamedList& status)
{
    Debug(this,DebugStub,"Please implement SS7TCAP::userStatus()");
}

SS7TCAPTransaction* SS7TCAP::getTransaction(const String& tid)
{
    SS7TCAPTransaction* tr = 0;
    Lock lock(m_transactionsMtx);
    ObjList* o = m_transactions.find(tid);
    if (o)
	tr = static_cast<SS7TCAPTransaction*>(o->get());
    if (tr && tr->ref())
	return tr;
    return 0;
}

void SS7TCAP::removeTransaction(SS7TCAPTransaction* tr)
{
    Lock lock(m_transactionsMtx);
    m_transactions.remove(tr);
}

void SS7TCAP::timerTick(const Time& when)
{
    // first check pending received messages
    SS7TCAPMessage* msg = dequeue();

    while (msg) {
	processSCCPData(msg);
	TelEngine::destruct(msg);
	//break;
	msg = dequeue();
    }

    // update/handle rest of transactions
    Lock lock(m_transactionsMtx);
    ListIterator iter(m_transactions);
    for (;;) {
	SS7TCAPTransaction* tr = static_cast<SS7TCAPTransaction*>(iter.get());
	// End of iteration?
	if (!tr)
	    break;
	if (!tr->ref())
	    continue;
	lock.drop();
	NamedList params("");
	DataBlock data;
	if (tr->transactionState() != SS7TCAPTransaction::Idle)
	    tr->checkComponents();
	if (tr->endNow())
	    tr->setState(SS7TCAPTransaction::Idle);
	if (tr->timedOut()) {
	    DDebug(this,DebugInfo,"SS7TCAP::timerTick() - transaction with id=%s(%p) timed out [%p]",tr->toString().c_str(),tr,this);
	    tr->updateToEnd();
	    buildSCCPData(params,tr);
	    if (!tr->basicEnd())
		tr->transactionData(params);
	    sendToUser(params);
	    tr->setState(SS7TCAPTransaction::Idle);
	}

	if (tr->transactionState() == SS7TCAPTransaction::Idle)
	    removeTransaction(tr);
	TelEngine::destruct(tr);
	if (!lock.acquire(m_transactionsMtx))
	    break;
    }
}

HandledMSU SS7TCAP::processSCCPData(SS7TCAPMessage* msg)
{
    HandledMSU result;
    if (!msg)
	return result;
    XDebug(this,DebugAll,"SS7TCAP::processSCCPData(msg=[%p]) [%p]",msg,this);

    NamedList& msgParams = msg->msgParams();
    DataBlock& msgData = msg->msgData();

    SS7TCAPError transactError = decodeTransactionPart(msgParams,msgData);
    if (transactError.error() != SS7TCAPError::NoError)
	return handleError(transactError,msgParams,msgData);

    NamedString* trID = msgParams.getParam(s_tcapLocalTID);
    String trType = msgParams.getValue(s_tcapRequest);
    SS7TCAP::TCAPUserTransActions type = (SS7TCAP::TCAPUserTransActions)trType.toInteger(SS7TCAP::s_transPrimitives);

     // check if it's a notice from SCCP, switch the ids if so
    if (msg->isNotice()) {
	trID = msgParams.getParam(s_tcapRemoteTID);
	msgParams.setParam(s_tcapRemoteTID,msgParams.getValue(s_tcapLocalTID));
	msgParams.setParam(s_tcapLocalTID,(TelEngine::null(trID) ? "" : *trID));
	type = TC_Notice;
	msgParams.setParam(s_tcapRequest,lookup(type,SS7TCAP::s_transPrimitives,"Notice"));
     }
    else
	incCounter(SS7TCAP::IncomingMsgs);

    SS7TCAPTransaction* tr = 0;
    switch (type) {
	case SS7TCAP::TC_Unidirectional:
	case SS7TCAP::TC_Begin:
	case SS7TCAP::TC_QueryWithPerm:
	case SS7TCAP::TC_QueryWithoutPerm:
	    // if there isn't a destination ID, allocate a new one and build a transaction
	    if (TelEngine::null(trID)) {
		String newID;
		allocTransactionID(newID);
		tr = buildTransaction(type,newID,msgParams,false);
		tr->ref();
		m_transactionsMtx.lock();
		m_transactions.append(tr);
		m_transactionsMtx.unlock();
		msgParams.setParam(s_tcapLocalTID,newID);
	    }
	    break;
	case SS7TCAP::TC_Continue:
	case SS7TCAP::TC_ConversationWithPerm:
	case SS7TCAP::TC_ConversationWithoutPerm:
	case SS7TCAP::TC_End:
	case SS7TCAP::TC_Response:
	case SS7TCAP::TC_P_Abort:
	case SS7TCAP::TC_U_Abort:
	case SS7TCAP::TC_Notice:
	    if (TelEngine::null(trID)) {
		transactError.setError(SS7TCAPError::Transact_UnassignedTransactionID);
		return handleError(transactError,msgParams,msgData);
	    }
	    tr = getTransaction(*trID);
	    if (!tr) {
		transactError.setError(SS7TCAPError::Transact_UnassignedTransactionID);
		return handleError(transactError,msgParams,msgData);
	    }
	    transactError = tr->update((SS7TCAP::TCAPUserTransActions)type,msgParams,false);
	    if (transactError.error() != SS7TCAPError::NoError) {
		result = handleError(transactError,msgParams,msgData,tr);
		TelEngine::destruct(tr);
		return result;
	    }
	    break;
	default:
	    incCounter(SS7TCAP::DiscardedMsgs);
	    return result;
    }
    if (tr) {
	transactError = tr->handleData(msgParams,msgData);
	if (transactError.error() != SS7TCAPError::NoError) {
	    result = handleError(transactError,msgParams,msgData,tr);
	    TelEngine::destruct(tr);
	    return result;
	}

	tr->addSCCPAddressing(msgParams,true);
	tr->updateState(false);
	if (sendToUser(msgParams)) {
	    tr->setUserName(msgParams.getValue(s_tcapUser));
	    tr->endNow(msgParams.getBoolValue(s_tcapEndNow,false));

	    if (tr->transactionType() == SS7TCAP::TC_Unidirectional
		|| tr->transactionType() == SS7TCAP::TC_U_Abort
		|| tr->transactionType() == SS7TCAP::TC_P_Abort
		|| tr->transactionType() == SS7TCAP::TC_End
		|| tr->transactionType() == SS7TCAP::TC_Response)
		tr->setState(SS7TCAPTransaction::Idle);
	    else
		tr->setTransmitState(SS7TCAPTransaction::Transmitted);
	}
	else if (type != SS7TCAP::TC_Notice) {
	    tr->update(SS7TCAP::TC_U_Abort,msgParams,false);
	    buildSCCPData(msgParams,tr);
	    tr->setTransmitState(SS7TCAPTransaction::Transmitted);
	    tr->updateState(false);
	}
	else
	    tr->setState(SS7TCAPTransaction::Idle);
	TelEngine::destruct(tr);
    }
    result = HandledMSU::Accepted;
    incCounter(SS7TCAP::NormalMsgs);
    return result;
}

SS7TCAPError SS7TCAP::userRequest(NamedList& params)
{
#ifdef DEBUG
    if (s_printMsgs && debugAt(DebugInfo))
	dumpData(DebugInfo,this,"SS7TCAP::userRequest() - received request ",this,params);
#endif

    NamedString* req = params.getParam(s_tcapRequest);

    NamedString* otid = params.getParam(s_tcapLocalTID);
    NamedString* user = params.getParam(s_tcapUser);
    SS7TCAPError error(m_tcapType);
    if (TelEngine::null(req)) {
	Debug(this,DebugInfo,"SS7TCAP::userRequest()[%p] - received a transaction request from user=%s with originating ID=%s"
	    " without request type, rejecting it",this,(user ? user->c_str() : ""),(otid ? otid->c_str() : ""));
	params.setParam(s_tcapRequestError,"missing_primitive");
	error.setError(SS7TCAPError::Transact_IncorrectTransactionPortion);
	return error;
    }

    SS7TCAPTransaction* tr = 0;
    if (!TelEngine::null(req)) {
	int type = req->toInteger(SS7TCAP::s_transPrimitives);
	switch (type) {
	    case SS7TCAP::TC_Unidirectional:
	    case SS7TCAP::TC_Begin:
	    case SS7TCAP::TC_QueryWithPerm:
	    case SS7TCAP::TC_QueryWithoutPerm:
		// if otid not set, alloc one and set it
		if (TelEngine::null(otid)) {
		    params.setParam(s_tcapLocalTID,allocTransactionID());
		    otid = params.getParam(s_tcapLocalTID);
		}
		else {
		    // if set, check if we already have it
		    tr = getTransaction(*otid);
		    if (tr) {
			Debug(this,DebugInfo,"SS7TCAP::userRequest()[%p] - received a new transaction request from user=%s with originating ID=%s which is the ID "
				"of an already existing transaction, rejecting the request",this,(user ? user->c_str() : ""),otid->c_str());
			params.setParam(s_tcapRequestError,"allocated_id");
			error.setError(SS7TCAPError::Transact_IncorrectTransactionPortion);
			TelEngine::destruct(tr);
			return error;
		    }
		}
		// create transaction
		tr = buildTransaction((SS7TCAP::TCAPUserTransActions)type,otid,params,true);
		if (!TelEngine::null(user))
		    tr->setUserName(user);
		tr->ref();
		m_transactionsMtx.lock();
		m_transactions.append(tr);
		m_transactionsMtx.unlock();
		break;
	    case SS7TCAP::TC_Continue:
	    case SS7TCAP::TC_ConversationWithPerm:
	    case SS7TCAP::TC_ConversationWithoutPerm:
	    case SS7TCAP::TC_End:
	    case SS7TCAP::TC_Response:
	    case SS7TCAP::TC_U_Abort:
		// find transaction and update
		if (!TelEngine::null(otid)) {
		    tr = getTransaction(*otid);
		    if (!tr) {
			params.setParam(s_tcapRequestError,"unknown_transaction");
			error.setError(SS7TCAPError::Transact_UnassignedTransactionID);
			return error;
		    }
		    error = tr->update((SS7TCAP::TCAPUserTransActions)type,params);
		    if (error.error() != SS7TCAPError::NoError) {
			TelEngine::destruct(tr);
			return error;
		    }
		}
		else {
		    params.setParam(s_tcapRequestError,"need_transaction_id");
		    error.setError(SS7TCAPError::Transact_UnassignedTransactionID);
		    return error;
		}
		break;
	    case SS7TCAP::TC_Unknown:
		if (!TelEngine::null(otid))
		    tr = getTransaction(*otid);
		break;
	    case SS7TCAP::TC_P_Abort:
	    case SS7TCAP::TC_Notice:
	    default:
		Debug(this,DebugAll,"SS7TCAP::userRequest() - received user request with unsuited primitive='%s' [%p]",req->c_str(),this);
		params.setParam(s_tcapRequestError,"wrong_primitive");
		error.setError(SS7TCAPError::Transact_UnrecognizedPackageType);
		return error;
	}
    }
    if (tr) {
	error = tr->handleDialogPortion(params,true);
	if (error.error() != SS7TCAPError::NoError) {
	    TelEngine::destruct(tr);
	    return error;
	}
	error = tr->handleComponents(params,true);
	if (error.error() != SS7TCAPError::NoError) {
	    TelEngine::destruct(tr);
	    return error;
	}
	if (tr->transmitState() == SS7TCAPTransaction::PendingTransmit) {
	    tr->updateState(true);
	    buildSCCPData(params,tr);
	    tr->setTransmitState(SS7TCAPTransaction::Transmitted);
	}
	else if (tr->transmitState() == SS7TCAPTransaction::NoTransmit)
	    removeTransaction(tr);
	TelEngine::destruct(tr);
    }
    return error;
}

void SS7TCAP::buildSCCPData(NamedList& params, SS7TCAPTransaction* tr)
{
    if (!tr)
	return;
    DDebug(this,DebugAll,"SS7TCAP::buildSCCPData(tr=%p) for local transaction ID=%s [%p]",tr,tr->toString().c_str(),this);

    Lock l(tr);
    bool sendOk = true;
    int type = tr->transactionType();
    if (type == SS7TCAP::TC_End
	  || type == TC_Response) {
	if (!tr->basicEnd()) {
	    sendOk = false; // prearranged end, don't send to remote Transaction End message
	    Debug(this,DebugAll,"SS7TCAP::buildSCCPData(tr=%p) [%p] - transaction with id=%s has set prearranged end, won't be"
		" sending anything to SCCP",tr,this,tr->toString().c_str());
	}
    }

    if (sendOk) {
	DataBlock data;
	tr->requestContent(params,data);
	tr->addSCCPAddressing(params,false);
	encodeTransactionPart(params,data);

	if (!sendData(data,params)) {
	    params.setParam("ReturnCause","Network failure");
	    enqueue(new SS7TCAPMessage(params,data,true));
	    Debug(this,DebugInfo,"SS7TCAP::buildSCCPData(tr=%p) [%p] - message for transaction with id=%s failed to be sent",
		tr,this,tr->toString().c_str());
	    return;
	};
	incCounter(SS7TCAP::OutgoingMsgs);
    }
}

HandledMSU SS7TCAP::handleError(SS7TCAPError& error, NamedList& params, DataBlock& data, SS7TCAPTransaction* tr)
{
    Debug(this,DebugInfo,"SS7TCAP::handleError(error=%s) for transaction with id=%s(%p) [%p]",error.errorName().c_str(),
	(tr ? tr->toString().c_str() : "unknown"),tr,this);
    HandledMSU result = HandledMSU::Accepted;

    int type = lookup(params.getValue(s_tcapRequest,""),SS7TCAP::s_transPrimitives);
    NamedString* rtid = params.getParam(s_tcapRemoteTID);
    NamedString* ltid = params.getParam(s_tcapLocalTID);
    bool buildRemAbort = false;
    bool buildLocAbort = false;
    switch (type) {
	case SS7TCAP::TC_Unidirectional:
	    incCounter(SS7TCAP::DiscardedMsgs);
	    return result; // return with rejected, meaning Discarded
	case SS7TCAP::TC_Begin:
	case SS7TCAP::TC_QueryWithPerm:
	case SS7TCAP::TC_QueryWithoutPerm:
	    if (!TelEngine::null(rtid))
		buildRemAbort = true;
	    else {
		// no originating ID, we don't know to whom to send the Abort, meaning we'll discard the message
		incCounter(SS7TCAP::DiscardedMsgs);
		return result;
	    }
	    break;
	case SS7TCAP::TC_Continue:
	case SS7TCAP::TC_ConversationWithPerm:
	case SS7TCAP::TC_ConversationWithoutPerm:
	    if (TelEngine::null(rtid) && TelEngine::null(ltid)) {
		incCounter(SS7TCAP::DiscardedMsgs);
		return result;
	    }
	    if (!TelEngine::null(rtid)) {
		buildRemAbort = true;
		if (!TelEngine::null(ltid))
		    buildLocAbort = true;
	    }
	    break;
	case SS7TCAP::TC_End:
	case SS7TCAP::TC_Response:
	case SS7TCAP::TC_P_Abort:
	case SS7TCAP::TC_U_Abort:
	    if (TelEngine::null(ltid)) {
		incCounter(SS7TCAP::DiscardedMsgs);
		return result;
	    }
	    else
		buildLocAbort = true;
	    break;
	default:
	    if (!TelEngine::null(rtid)) {
		buildRemAbort = true;
		if (!TelEngine::null(ltid))
		    buildLocAbort = true;
	    }
	    else {
		incCounter(SS7TCAP::DiscardedMsgs);
		return result;
	    }
	    break;
    }

    if (buildLocAbort && !TelEngine::null(ltid)) { // notify user of the abort
	params.setParam(s_tcapRequest,lookup(SS7TCAP::TC_P_Abort,SS7TCAP:: s_transPrimitives));
	params.setParam(s_tcapAbortCause,"pAbort");
	params.setParam(s_tcapAbortInfo,String(error.error()));
	if (tr) {
	    tr->update(SS7TCAP::TC_P_Abort,params,false);
	    tr->updateState();
	}
	sendToUser(params);
    }
    if (buildRemAbort) {
	// clean dataBlock
	data.clear();

	if (!TelEngine::null(rtid)) { // we have the remote ID, notify of abort
	    NamedList addr("");
	    populateSCCPAddress(addr,addr,params,false,true);
	    params.copyParams(addr);

	    if (error.error() != SS7TCAPError::Dialog_Abnormal) {
		params.setParam(s_tcapRequest,lookup(SS7TCAP::TC_P_Abort,SS7TCAP::s_transPrimitives));
		params.setParam(s_tcapAbortCause,"pAbort");
		params.setParam(s_tcapAbortInfo,String(error.error()));
	    }
	    else if (tr)
		tr->abnormalDialogInfo(params);

	    if (tcapType() == ANSITCAP)
		SS7TCAPTransactionANSI::encodePAbort(tr,params,data);
	    else if (tcapType() == ITUTCAP)
		SS7TCAPTransactionITU::encodePAbort(tr,params,data);

	    encodeTransactionPart(params,data);
	    sendData(data,params);
	}
    }
    if (buildRemAbort || buildLocAbort) {
	incCounter(SS7TCAP::AbnormalMsgs);
	result = HandledMSU::Accepted;
    }
    return result;
}

/**
 * SS7TCAPError implementation
 */
struct TCAPError {
    SS7TCAPError::ErrorType errorType;
    u_int16_t errorCode;
};

static const TCAPError s_ansiErrorDefs[] = {
    // error                                                    fullcode
    { SS7TCAPError::Transact_UnrecognizedPackageType,             0x01},
    { SS7TCAPError::Transact_IncorrectTransactionPortion,         0x02},
    { SS7TCAPError::Transact_BadlyStructuredTransaction,          0x03},
    { SS7TCAPError::Transact_UnassignedTransactionID,             0x04},
    { SS7TCAPError::Transact_PermissionToReleaseProblem,          0x05},
    { SS7TCAPError::Transact_ResourceUnavailable,                 0x06},

    { SS7TCAPError::Dialog_UnrecognizedDialoguePortionID,         0x07},
    { SS7TCAPError::Dialog_BadlyStructuredDialoguePortion,        0x08},
    { SS7TCAPError::Dialog_MissingDialoguePortion,                0x09},
    { SS7TCAPError::Dialog_InconsistentDialoguePortion,           0x0a},

    // GeneralProblem
    { SS7TCAPError::General_UnrecognizedComponentType,            0x101},
    { SS7TCAPError::General_IncorrectComponentPortion,            0x102},
    { SS7TCAPError::General_BadlyStructuredCompPortion,           0x103},
    { SS7TCAPError::General_IncorrectComponentCoding,             0x104},

    // InvokeProblem
    { SS7TCAPError::Invoke_DuplicateInvokeID,                     0x201},
    { SS7TCAPError::Invoke_UnrecognizedOperationCode,             0x202},
    { SS7TCAPError::Invoke_IncorrectParameter,                    0x203},
    { SS7TCAPError::Invoke_UnrecognizedCorrelationID,             0x204},

    // ReturnResult
    { SS7TCAPError::Result_UnrecognisedCorrelationID,             0x301},
    { SS7TCAPError::Result_UnexpectedReturnResult,                0x302},
    { SS7TCAPError::Result_IncorrectParameter,                    0x303},

    // ReturnError
    { SS7TCAPError::Error_UnrecognisedCorrelationID,              0x401},
    { SS7TCAPError::Error_UnexpectedReturnError,                  0x402},
    { SS7TCAPError::Error_UnrecognisedError,                      0x403},
    { SS7TCAPError::Error_UnexpectedError,                        0x404},
    { SS7TCAPError::Error_IncorrectParameter,                     0x405},

    { SS7TCAPError::NoError,                                      0xfff},
};

static const TCAPError s_ituErrorDefs[] = {
    // error                                                    fullcode
    { SS7TCAPError::Transact_UnrecognizedPackageType,             0x00},
    { SS7TCAPError::Transact_UnassignedTransactionID,             0x01},
    { SS7TCAPError::Transact_BadlyStructuredTransaction,          0x02},
    { SS7TCAPError::Transact_IncorrectTransactionPortion,         0x03},
    { SS7TCAPError::Transact_ResourceUnavailable,                 0x04},

    { SS7TCAPError::Dialog_Abnormal,                              0x7000},

    // GeneralProblem
    { SS7TCAPError::General_UnrecognizedComponentType,            0x8000},
    { SS7TCAPError::General_IncorrectComponentPortion,            0x8001},
    { SS7TCAPError::General_BadlyStructuredCompPortion,           0x8002},

    // InvokeProblem
    { SS7TCAPError::Invoke_DuplicateInvokeID,                     0x8100},
    { SS7TCAPError::Invoke_UnrecognizedOperationCode,             0x8101},
    { SS7TCAPError::Invoke_IncorrectParameter,                    0x8102},
    { SS7TCAPError::Invoke_UnrecognizedCorrelationID,             0x8105},
    { SS7TCAPError::Invoke_ResourceLimitation,                    0x8103},
    { SS7TCAPError::Invoke_InitiatingRelease,                     0x8104},
    { SS7TCAPError::Invoke_LinkedResponseUnexpected,              0x8106},
    { SS7TCAPError::Invoke_UnexpectedLinkedOperation,             0x8107},

    // ReturnResult
    { SS7TCAPError::Result_UnrecognizedInvokeID,                  0x8200},
    { SS7TCAPError::Result_UnexpectedReturnResult,                0x8201},
    { SS7TCAPError::Result_IncorrectParameter,                    0x8202},

    // ReturnError
    { SS7TCAPError::Error_UnrecognizedInvokeID,                   0x8300},
    { SS7TCAPError::Error_UnexpectedReturnError,                  0x8301},
    { SS7TCAPError::Error_UnrecognisedError,                      0x8302},
    { SS7TCAPError::Error_UnexpectedError,                        0x8303},
    { SS7TCAPError::Error_IncorrectParameter,                     0x8304},

    { SS7TCAPError::NoError,                                      0xffff},
};

const TokenDict SS7TCAPError::s_errorTypes[] = {
    {"Transact-UnrecognizedPackageType",        SS7TCAPError::Transact_UnrecognizedPackageType},
    {"Transact-IncorrectTransactionPortion",    SS7TCAPError::Transact_IncorrectTransactionPortion},
    {"Transact-BadlyStructuredTransaction",     SS7TCAPError::Transact_BadlyStructuredTransaction},
    {"Transact-UnassignedTransactionID",        SS7TCAPError::Transact_UnassignedTransactionID },
    {"Transact-PermissionToReleaseProblem",     SS7TCAPError::Transact_PermissionToReleaseProblem},
    {"Transact-ResourceUnavailable",            SS7TCAPError::Transact_ResourceUnavailable},

    {"Dialog-UnrecognizedDialoguePortionID",    SS7TCAPError::Dialog_UnrecognizedDialoguePortionID},
    {"Dialog-BadlyStructuredDialoguePortion",   SS7TCAPError::Dialog_BadlyStructuredDialoguePortion},
    {"Dialog-MissingDialoguePortion",           SS7TCAPError::Dialog_MissingDialoguePortion},
    {"Dialog-InconsistentDialoguePortion",      SS7TCAPError::Dialog_InconsistentDialoguePortion},
    {"Dialog-Abnormal",                         SS7TCAPError::Dialog_Abnormal},

    {"General-UnrecognizedComponentType",       SS7TCAPError::General_UnrecognizedComponentType},
    {"General-IncorrectComponentPortion",       SS7TCAPError::General_IncorrectComponentPortion},
    {"General-BadlyStructuredCompPortion",      SS7TCAPError::General_BadlyStructuredCompPortion},
    {"General-IncorrectComponentCoding",        SS7TCAPError::General_IncorrectComponentCoding},

    {"Invoke-DuplicateInvokeID",                SS7TCAPError::Invoke_DuplicateInvokeID},
    {"Invoke-UnrecognizedOperationCode",        SS7TCAPError::Invoke_UnrecognizedOperationCode},
    {"Invoke-IncorrectParameter",               SS7TCAPError::Invoke_IncorrectParameter},
    {"Invoke-UnrecognizedCorrelationID",        SS7TCAPError::Invoke_UnrecognizedCorrelationID},
    {"Invoke-ResourceLimitation",               SS7TCAPError::Invoke_ResourceLimitation},
    {"Invoke-InitiatingRelease",                SS7TCAPError::Invoke_InitiatingRelease},
    {"Invoke-LinkedResponseUnexpected",         SS7TCAPError::Invoke_LinkedResponseUnexpected},
    {"Invoke-UnexpectedLinkedOperation",        SS7TCAPError::Invoke_UnexpectedLinkedOperation},

    {"Result-UnrecognizedInvokeID",             SS7TCAPError::Result_UnrecognizedInvokeID},
    {"Result-UnrecognisedCorrelationID",        SS7TCAPError::Result_UnrecognisedCorrelationID},
    {"Result-UnexpectedReturnResult",           SS7TCAPError::Result_UnexpectedReturnResult},
    {"Result-IncorrectParameter",               SS7TCAPError::Result_IncorrectParameter},

    {"Error-UnrecognizedInvokeID",              SS7TCAPError::Error_UnrecognizedInvokeID},
    {"Error-UnrecognisedCorrelationID",         SS7TCAPError::Error_UnrecognisedCorrelationID},
    {"Error-UnexpectedReturnError",             SS7TCAPError::Error_UnexpectedReturnError},
    {"Error-UnrecognisedError",                 SS7TCAPError::Error_UnrecognisedError},
    {"Error-UnexpectedError",                   SS7TCAPError::Error_UnexpectedError},
    {"Error-IncorrectParameter",                SS7TCAPError::Error_IncorrectParameter},

    {"NoError",                                 SS7TCAPError::NoError},
    {0,0},
};

SS7TCAPError::SS7TCAPError(SS7TCAP::TCAPType tcapType)
    : m_tcapType(tcapType), m_error(SS7TCAPError::NoError)
{
}

SS7TCAPError::SS7TCAPError(SS7TCAP::TCAPType tcapType, ErrorType error)
    : m_tcapType(tcapType), m_error(error)
{
    XDebug(DebugAll,"SS7TCAPError created TCAP=%s with error=%s [%p]",lookup(tcapType,SS7TCAP::s_tcapVersion),
	lookup(error,s_errorTypes),this);
}

SS7TCAPError::~SS7TCAPError()
{
}

const String SS7TCAPError::errorName()
{
    return lookup(m_error,s_errorTypes,"NoError");
}

u_int16_t SS7TCAPError::errorCode()
{
    const TCAPError* errDef = (m_tcapType == SS7TCAP::ANSITCAP ? s_ansiErrorDefs : s_ituErrorDefs);
    for (;errDef && errDef->errorType != SS7TCAPError::NoError; errDef++) {
	if (errDef->errorType == m_error)
	    break;
    }
    return errDef->errorCode;
}

int SS7TCAPError::errorFromCode(SS7TCAP::TCAPType tcapType, u_int16_t code)
{
    const TCAPError* errDef = (tcapType == SS7TCAP::ANSITCAP ? s_ansiErrorDefs : s_ituErrorDefs);
    for (;errDef && errDef->errorType != SS7TCAPError::NoError; errDef++) {
	if (errDef->errorCode == code)
	    break;
    }
    return errDef->errorType;
}

u_int16_t SS7TCAPError::codeFromError(SS7TCAP::TCAPType tcapType, int err)
{
    const TCAPError* errDef = (tcapType == SS7TCAP::ANSITCAP ? s_ansiErrorDefs : s_ituErrorDefs);
    for (;errDef && errDef->errorType != SS7TCAPError::NoError; errDef++) {
	if (errDef->errorType == err)
	    break;
    }
    return errDef->errorCode;
}

/**
 * SS7TCAPTransaction
 */
SS7TCAPTransaction::SS7TCAPTransaction(SS7TCAP* tcap, SS7TCAP::TCAPUserTransActions type,
	const String& transactID, NamedList& params, u_int64_t timeout, bool initLocal)
    : Mutex(true,"TcapTransaction"),
      m_tcap(tcap), m_tcapType(SS7TCAP::UnknownTCAP), m_userName(""), m_localID(transactID), m_type(type),
      m_localSCCPAddr(""), m_remoteSCCPAddr(""), m_basicEnd(true), m_endNow(false), m_timeout(timeout)
{

    DDebug(m_tcap,DebugAll,"SS7TCAPTransaction(tcap = '%s' [%p], transactID = %s) created [%p]",
	    m_tcap->toString().c_str(),tcap,transactID.c_str(),this);

    m_remoteID = params.getValue(s_tcapRemoteTID);
    populateSCCPAddress(m_localSCCPAddr,m_remoteSCCPAddr,params,initLocal,false);
    m_endNow = params.getBoolValue(s_tcapEndNow,false);
    if (initLocal)
	setState(PackageSent);
    else
	setState(PackageReceived);
}

SS7TCAPTransaction::~SS7TCAPTransaction()
{
    DDebug(tcap(),DebugAll,"Transaction with ID=%s of user=%s destroyed [%p]",
	   m_localID.c_str(),m_userName.c_str(),this);
    m_components.clear();
    m_tcap = 0;
}

SS7TCAPComponent* SS7TCAPTransaction::findComponent(const String& id)
{
    SS7TCAPComponent* comp = 0;
    ObjList* o = m_components.find(id);
    if (o)
	comp = static_cast<SS7TCAPComponent*>(o->get());
    return comp;
}

SS7TCAPError SS7TCAPTransaction::update(SS7TCAP::TCAPUserTransActions type, NamedList& params, bool updateByUser)
{
    DDebug(tcap(),DebugStub,"SS7TCAPTransaction::update() [%p], localID=%s - stub",this,m_localID.c_str());
    SS7TCAPError error(m_tcapType);
    return error;
}

SS7TCAPError SS7TCAPTransaction::buildComponentError(SS7TCAPError& error, NamedList& params, DataBlock& data)
{
    if (error.error() == SS7TCAPError::NoError)
	return error;
    Debug(tcap(),DebugInfo,"SS7TCAPTransaction::buildComponentError(error=%s) for transaction with id=%s [%p]",error.errorName().c_str(),
	toString().c_str(),this);
    int compCount = params.getIntValue(s_tcapCompCount,1);

    if (!compCount)
	return error;

    String param;
    compPrefix(param,compCount,true);
    bool buildRej = false;
    NamedString* typeStr = params.getParam(param + s_tcapCompType);
    if (TelEngine::null(typeStr))
	buildRej = true;
    else {
	int type = typeStr->toInteger(SS7TCAP::s_compPrimitives);
	NamedString* invokeID  = params.getParam(param + s_tcapRemoteCID);

	switch (type) {
	    case SS7TCAP::TC_ResultLast:
	    case SS7TCAP::TC_ResultNotLast:
	    case SS7TCAP::TC_U_Error:
		if (!TelEngine::null(invokeID)) {
		    SS7TCAPComponent* comp = findComponent(*invokeID);
		    if (comp)
			m_components.remove(comp);
		}
		break;
	    case SS7TCAP::TC_Invoke:
	    case SS7TCAP::TC_R_Reject:
	    default:
		break;
	}
	buildRej = true;
    }

    params.setParam(param + s_tcapCompType,lookup(SS7TCAP::TC_L_Reject,SS7TCAP::s_compPrimitives,"L_Reject"));
    params.setParam(param + s_tcapProblemCode,String(error.error()));
    if (buildRej) {
	SS7TCAPComponent* comp = SS7TCAPComponent::componentFromNamedList(m_tcapType,this,params,compCount);
	if (comp)
	    m_components.append(comp);
    }
    return error;
}

SS7TCAPError SS7TCAPTransaction::handleComponents(NamedList& params, bool updateByUser)
{
    XDebug(tcap(),DebugAll,"SS7TCAPTransaction::handleComponents(updateByUser=%s) [%p]",String::boolText(updateByUser),this);
    int count = params.getIntValue(s_tcapCompCount,0);
    SS7TCAPError error(m_tcapType);
    if (!count)
	return error;
    int index = 0;
    Lock l(this);
    while (index < count) {
	index++;
	String paramRoot;
	compPrefix(paramRoot,index,true);

	NamedString* localCID = params.getParam(paramRoot + s_tcapLocalCID);
	NamedString* typeStr = params.getParam(paramRoot + s_tcapCompType);
	if (TelEngine::null(typeStr))
	    continue;
	int type = typeStr->toInteger(SS7TCAP::s_compPrimitives);
	switch (type) {
	    case SS7TCAP::TC_Invoke:
	    case SS7TCAP::TC_InvokeNotLast:
		if (!updateByUser) {
		    if (!TelEngine::null(localCID)) {
			// we have a linked/correlation ID, check the state of that component
			SS7TCAPComponent* linkedTo = findComponent(*localCID);
			if (!linkedTo) {
			    type = SS7TCAP::TC_L_Reject;
			    params.setParam(paramRoot + s_tcapProblemCode,String(SS7TCAPError::Invoke_UnrecognizedCorrelationID));
			}
			else {
			    if (linkedTo->state() != SS7TCAPComponent::OperationSent) {
				type = SS7TCAP::TC_L_Reject;
				params.setParam(paramRoot + s_tcapProblemCode,String(SS7TCAPError::Invoke_UnexpectedLinkedOperation));
			    }
			}
		    }
		    if (type == SS7TCAP::TC_L_Reject) {
			params.setParam(paramRoot + s_tcapCompType,lookup(type,SS7TCAP::s_compPrimitives,"L_Reject"));
			SS7TCAPComponent* comp = SS7TCAPComponent::componentFromNamedList(m_tcapType,this,params,index);
			if (comp)
			    m_components.append(comp);
		    }
		}
		else {
		    if (!TelEngine::null(localCID)) {
			if (findComponent(*localCID)) {
			    error.setError(SS7TCAPError::Invoke_DuplicateInvokeID);
			    return error;
			}
			else {
			    SS7TCAPComponent* comp = SS7TCAPComponent::componentFromNamedList(m_tcapType,this,params,index);
			    if (comp) {
				m_components.append(comp);
				comp->setState(SS7TCAPComponent::OperationSent);
			    }
			}
		    }
		}
		break;
	    case SS7TCAP::TC_ResultLast:
	    case SS7TCAP::TC_ResultNotLast:
	    case SS7TCAP::TC_U_Error:
		if (!updateByUser) {
		    if (!TelEngine::null(localCID)) {
			SS7TCAPComponent* comp = findComponent(*localCID);
			if (comp)
			    comp->update(params,index);
			else {
			    params.setParam(paramRoot + s_tcapCompType,lookup(SS7TCAP::TC_L_Reject,SS7TCAP::s_compPrimitives,"L_Reject"));
			    params.setParam(paramRoot + s_tcapProblemCode,String(SS7TCAPError::Invoke_UnexpectedLinkedOperation));
			    SS7TCAPComponent* comp = SS7TCAPComponent::componentFromNamedList(m_tcapType,this,params,index);
			    if (comp)
				m_components.append(comp);
			}
		    }
		}
		break;
	    case SS7TCAP::TC_R_Reject:
	    case SS7TCAP::TC_U_Reject:
		if (!updateByUser) {
		    params.setParam(paramRoot + s_tcapCompType,lookup(SS7TCAP::TC_R_Reject,SS7TCAP::s_compPrimitives,"R_Reject"));
		    if (!TelEngine::null(localCID)) {
			SS7TCAPComponent* comp = findComponent(*localCID);
			if (comp)
			   m_components.remove(comp);
		    }
		}
		else if (!TelEngine::null(localCID)) {
		    m_components.remove(*localCID);
		}
		break;
	    case SS7TCAP::TC_L_Reject:
	    case SS7TCAP::TC_U_Cancel:
		if (updateByUser) {
		    if (!TelEngine::null(localCID))
			m_components.remove(*localCID);
		}
		break;
	    case SS7TCAP::TC_TimerReset:
		if (updateByUser && !TelEngine::null(localCID) && m_tcapType == SS7TCAP::ITUTCAP) {
		    SS7TCAPComponent* comp = findComponent(*localCID);
		    if (comp)
			comp->resetTimer(params,index);
		}
		break;
	    case SS7TCAP::TC_L_Cancel:
	    default:
		break;
	}
    }
    DDebug(tcap(),DebugAll,"SS7TCAPTransaction::handleComponents() - transaction with localID=%s handled %d components [%p]",
	m_localID.c_str(),index,this);
    return error;
}

void SS7TCAPTransaction::requestComponents(NamedList& params, DataBlock& data)
{
    Lock(this);
    unsigned int index = params.getIntValue(s_tcapCompCount);
    for (ObjList* o = m_components.skipNull(); o; o = o->skipNext()) {
	SS7TCAPComponent* comp = static_cast<SS7TCAPComponent*>(o->get());
	if (comp && comp->state() == SS7TCAPComponent::OperationPending) {
	    index++;
	    comp->fill(index,params);
	}
    }
#ifdef DEBUG
    if (tcap() && s_printMsgs && s_extendedDbg && debugAt(DebugAll))
	dumpData(DebugAll,tcap(),"SS7TCAPTransaction::requestComponents() preparing to encode components:",this,params,data);
#endif
    params.setParam(s_tcapCompCount,String(index));
    encodeComponents(params,data);
#ifdef DEBUG
    if (tcap() && s_printMsgs && s_extendedDbg && debugAt(DebugAll))
	dumpData(DebugAll,tcap(),"SS7TCAPTransaction::requestComponents()  encoded components'",this,params,data);
#endif
}

void SS7TCAPTransaction::transactionData(NamedList& params)
{
    Lock l(this);
    params.setParam(s_tcapRequest,lookup(m_type,SS7TCAP::s_transPrimitives));
    params.setParam(s_tcapLocalTID,m_localID);
    params.setParam(s_tcapRemoteTID,m_remoteID);
#ifdef DEBUG
    if (s_printMsgs && s_extendedDbg && debugAt(DebugAll))
	dumpData(DebugAll,tcap(),"SS7TCAPTransaction::transactionData() - added transaction data",this,params);
#endif
}

void SS7TCAPTransaction::checkComponents()
{
    NamedList params("");
    int index = 0;
    Lock l(this);
    ListIterator iter(m_components);
    for (;;) {
	SS7TCAPComponent* comp = static_cast<SS7TCAPComponent*>(iter.get());
	if (!comp)
	    break;
	if (comp->timedOut()) {
	    XDebug(tcap(),DebugInfo,"SS7TCAPTransaction::checkComponents() - component with local ID = %s timed out in"
		    " transaction with local ID = %s [%p]",comp->toString().c_str(),toString().c_str(),this);
	    SS7TCAP::TCAPUserCompActions type = comp->type();
	    String paramRoot = "";
	    switch (type) {
		case SS7TCAP::TC_Invoke:
		case SS7TCAP::TC_InvokeNotLast:
			if (comp->operationClass() != SS7TCAP::NoReport) {
			    index++;
			    comp->setType(SS7TCAP::TC_L_Cancel);
			    comp->fill(index,params);
			}
			comp->setState(SS7TCAPComponent::Idle);
		    break;
		case SS7TCAP::TC_ResultLast:
		case SS7TCAP::TC_U_Error:
		    comp->setState(SS7TCAPComponent::Idle);
		    break;
		case SS7TCAP::TC_ResultNotLast:
		case SS7TCAP::TC_U_Reject:
		case SS7TCAP::TC_L_Reject:
		case SS7TCAP::TC_R_Reject:
		case SS7TCAP::TC_L_Cancel:
		case SS7TCAP::TC_U_Cancel:
		case SS7TCAP::TC_TimerReset:
		default:
		    break;
	    }
	}
	if (comp->state() == SS7TCAPComponent::Idle)
	    m_components.remove(comp);
    }
    if (params.count()) {
	params.setParam(s_tcapCompCount,String(index));
	transactionData(params);
	params.clearParam(s_tcapRequest);
	tcap()->sendToUser(params);
    }
    if (m_components.count() == 0) {// we don't have any more components
	if (!m_timeout.started()) {
	    m_timeout.start();
	    XDebug(tcap(),DebugInfo,"SS7TCAPTransaction::checkComponents() - timer for transaction with localID=%s has been started [%p]",
		toString().c_str(),this);
	}
    }
}

void SS7TCAPTransaction::setTransmitState(TransactionTransmit state)
{
    Lock l(this);
    m_transmit = state;
    if (m_transmit == Transmitted) {
	switch (m_type) {
	    case SS7TCAP::TC_Unidirectional:
	    case SS7TCAP::TC_P_Abort:
	    case SS7TCAP::TC_U_Abort:
	    case SS7TCAP::TC_Response:
	    case SS7TCAP::TC_End:
		m_state = Idle;
		break;
	    case SS7TCAP::TC_Notice:
	    case SS7TCAP::TC_Begin:
	    case SS7TCAP::TC_QueryWithPerm:
	    case SS7TCAP::TC_QueryWithoutPerm:
	    case SS7TCAP::TC_Continue:
	    case SS7TCAP::TC_ConversationWithPerm:
	    case SS7TCAP::TC_ConversationWithoutPerm:
	    default:
		break;
	}
    }
}

void SS7TCAPTransaction::addSCCPAddressing(NamedList& fillParams, bool local)
{
    String localParam(local ? s_calledPA : s_callingPA);
    String remoteParam(local ? s_callingPA : s_calledPA);
    fillParams.clearParam(s_calledPA,'.');
    fillParams.clearParam(s_callingPA,'.');
    Lock l(this);
    fillParams.copyParam(m_localSCCPAddr,s_localPC);
    for (unsigned int i = 0; i < m_localSCCPAddr.count(); i++) {
	NamedString* ns = m_localSCCPAddr.getParam(i);
	if (ns && *ns && !(*ns).null()) {
	    const String& name = ns->name();
	    if (name != s_localPC)
		fillParams.setParam(localParam + "." + name,*ns);
	}
    }
    fillParams.copyParam(m_remoteSCCPAddr,s_remotePC);
    for (unsigned int i = 0; i < m_remoteSCCPAddr.count(); i++) {
	NamedString* ns = m_remoteSCCPAddr.getParam(i);
	if (ns && *ns && !(*ns).null()) {
	    const String& name = ns->name();
	    if (name != s_remotePC)
		fillParams.setParam(remoteParam + "." +  name,*ns);
	}
    }
}

SS7TCAPError SS7TCAPTransaction::handleData(NamedList& params, DataBlock& data)
{
    DDebug(tcap(),DebugAll,"SS7TCAPTransaction::handleData() transactionID=%s data length=%u [%p]",m_localID.c_str(),
	   data.length(),this);
    Lock lock(this);
    // in case of Abort message, check Cause Information
    SS7TCAPError error(m_tcapType);
    return error;
}

void SS7TCAPTransaction::updateToEnd()
{
}

void SS7TCAPTransaction::abnormalDialogInfo(NamedList& params)
{
    Debug(tcap(),DebugAll,"SS7TCAPTransaction::abnormalDialogInfo() [%p]",this);
}

/**
 * SS7TCAPComponent
 */
const TokenDict SS7TCAPComponent::s_compStates[] = {
    {"Idle",                SS7TCAPComponent::Idle},
    {"OperationPending",    SS7TCAPComponent::OperationPending},
    {"OperationSent",       SS7TCAPComponent::OperationSent},
    {"WaitForReject",       SS7TCAPComponent::WaitForReject},
};

SS7TCAPComponent::SS7TCAPComponent(SS7TCAP::TCAPType type, SS7TCAPTransaction* trans, NamedList& params, unsigned int index)
    : m_transact(trans), m_state(Idle),
      m_id(""), m_corrID(""), m_opClass(SS7TCAP::SuccessOrFailureReport), m_opTimer(0), m_error(type)
{
    String paramRoot;
    compPrefix(paramRoot,index,true);

    m_type = (SS7TCAP::TCAPUserCompActions) lookup(params.getValue(paramRoot + s_tcapCompType),SS7TCAP::s_compPrimitives);
    m_id = params.getValue(paramRoot + s_tcapLocalCID);
    m_corrID = params.getValue(paramRoot + s_tcapRemoteCID);

    setState(OperationPending);

    m_opType = params.getValue(paramRoot + s_tcapOpCodeType,"");
    m_opCode = params.getValue(paramRoot + s_tcapOpCode,"");
    NamedString* opClass = params.getParam(paramRoot + "operationClass");
    if (!TelEngine::null(opClass))
	m_opClass = (SS7TCAP::TCAPComponentOperationClass) opClass->toInteger(SS7TCAP::s_compOperClasses,SS7TCAP::SuccessOrFailureReport);
    m_opTimer.interval(params.getIntValue(paramRoot + "timeout",5) * 1000);

    m_error.setError((SS7TCAPError::ErrorType)params.getIntValue(paramRoot + s_tcapProblemCode));

    DDebug(m_transact->tcap(),DebugAll,"SS7TCAPComponent() [%p] created for transaction='%s' [%p] with localID=%s, remoteID=%s,"
	    " type=%s, class=%s",this, (m_transact ? m_transact->toString().c_str() :""),m_transact,m_id.c_str(),
	    m_corrID.c_str(),lookup(m_type,SS7TCAP::s_compPrimitives),lookup(m_opClass,SS7TCAP::s_compOperClasses));
}

SS7TCAPComponent::~SS7TCAPComponent()
{
    DDebug(m_transact->tcap(),DebugAll,"SS7TCAPComponent::~SS7TCAPComponent() - component [%p] destroyed",this);
    m_transact = 0;
}

void SS7TCAPComponent::update(NamedList& params, unsigned int index)
{
    String paramRoot;
    compPrefix(paramRoot,index,false);
    DDebug(m_transact->tcap(),DebugAll,"SS7TCAPComponent::update() - update component with localID=%s [%p]",m_id.c_str(),this);

    m_type = (SS7TCAP::TCAPUserCompActions) lookup(params.getValue(paramRoot + "." + s_tcapCompType),SS7TCAP::s_compPrimitives);
    switch (m_type) {
	case SS7TCAP::TC_ResultLast:
	    if (m_opClass == SS7TCAP::SuccessOrFailureReport || m_opClass == SS7TCAP::SuccessOnlyReport)
		setState(WaitForReject);
	    else if (m_opClass == SS7TCAP::FailureOnlyReport || m_opClass == SS7TCAP::NoReport) {
		// build reject component
		m_type = SS7TCAP::TC_L_Reject;
		params.setParam(paramRoot + "." + s_tcapCompType,lookup(SS7TCAP::TC_L_Reject,SS7TCAP::s_compPrimitives));
		params.setParam(paramRoot + "." + s_tcapProblemCode,String(SS7TCAPError::Result_UnexpectedReturnResult));
		m_error.setError(SS7TCAPError::Result_UnexpectedReturnResult);
		setState(OperationPending);
		return;
	    }
	    break;
	case SS7TCAP::TC_ResultNotLast:
	    if (m_opClass == SS7TCAP::FailureOnlyReport || m_opClass == SS7TCAP::NoReport) {
		// build reject component
		m_type = SS7TCAP::TC_L_Reject;
		params.setParam(paramRoot + "." + s_tcapCompType,lookup(SS7TCAP::TC_L_Reject,SS7TCAP::s_compPrimitives));
		params.setParam(paramRoot + "." + s_tcapProblemCode,String(SS7TCAPError::Result_UnexpectedReturnResult));
		m_error.setError(SS7TCAPError::Result_UnexpectedReturnResult);
		setState(OperationPending);
		return;
	    }
	    else if (m_opClass == SS7TCAP::SuccessOnlyReport)
		setState(WaitForReject);
	    break;
	case SS7TCAP::TC_U_Error:
	    if (m_opClass == SS7TCAP::FailureOnlyReport)
		setState(WaitForReject);
	    else if (m_opClass == SS7TCAP::SuccessOnlyReport || m_opClass == SS7TCAP::NoReport) {
		m_type = SS7TCAP::TC_L_Reject;
		params.setParam(paramRoot + "." + s_tcapCompType,lookup(SS7TCAP::TC_L_Reject,SS7TCAP::s_compPrimitives));
		params.setParam(paramRoot + "." + s_tcapProblemCode,String(SS7TCAPError::Error_UnexpectedReturnError));
		m_error.setError(SS7TCAPError::Error_UnexpectedReturnError);
		setState(OperationPending);
		return;
	    }
	    break;
	case SS7TCAP::TC_TimerReset:
	default:
	    break;
    }
    if (TelEngine::null(params.getParam(paramRoot + "." + s_tcapOpCode))) {
	params.setParam(paramRoot + "." + s_tcapOpCode,m_opCode);
	params.setParam(paramRoot + "." + s_tcapOpCodeType,m_opType);
    }
}

SS7TCAPComponent* SS7TCAPComponent::componentFromNamedList(SS7TCAP::TCAPType tcapType, SS7TCAPTransaction* tr, NamedList& params, unsigned int index)
{
    if (!tr)
	return 0;

    String paramRoot;
    compPrefix(paramRoot,index,true);
    NamedString* str = params.getParam(paramRoot + s_tcapLocalCID);
    if (TelEngine::null(str))
	str = params.getParam(paramRoot + s_tcapRemoteCID);
    if (TelEngine::null(str))
	return 0;
    int type = lookup(params.getValue(paramRoot + s_tcapCompType),SS7TCAP::s_compPrimitives);
    // we allow building Reject components that have been built by Component layer or Invokes requested by local user
    if (type != SS7TCAP::TC_Invoke && type != SS7TCAP::TC_InvokeNotLast && type != SS7TCAP::TC_L_Reject
	&& type != SS7TCAP::TC_U_Reject && type != SS7TCAP::TC_R_Reject)
	return 0;

    SS7TCAPComponent* comp = new SS7TCAPComponent(tcapType,tr,params,index);
    return comp;
}

void SS7TCAPComponent::setState(TCAPComponentState state)
{
#ifdef DEBUG
    if (m_transact && m_transact->tcap() && s_extendedDbg)
	DDebug(m_transact->tcap(),DebugAll,"SS7TCAPComponent::setState(%s), locaID=%s remoteID=%s [%p]",lookup(state,s_compStates),
		m_id.c_str(),m_corrID.c_str(),this);
#endif
    m_state = state;
    m_opTimer.stop();
    if (!(state == Idle || state == OperationPending))
	m_opTimer.start();
}

void SS7TCAPComponent::fill(unsigned int index, NamedList& fillIn)
{
#ifdef DEBUG
    if (m_transact && m_transact->tcap() && s_printMsgs && s_extendedDbg && debugAt(DebugAll))
	DDebug(m_transact->tcap(),DebugAll,"SS7TCAPComponent::fill() - component with localID=%s,remoteID=%s of transaction=%s "
	    "fill for index=%u [%p]",m_id.c_str(),m_corrID.c_str(),m_transact->toString().c_str(),index,this);
#endif
    String paramRoot;
    compPrefix(paramRoot,index,true);

    fillIn.setParam(paramRoot + s_tcapLocalCID,m_id);
    fillIn.setParam(paramRoot + s_tcapRemoteCID,m_corrID);
    fillIn.setParam(paramRoot + s_tcapCompType,lookup(m_type,SS7TCAP::s_compPrimitives,"Unknown"));

    if (m_error.error() != SS7TCAPError::NoError) {
	if (m_type == SS7TCAP::TC_U_Error)
	    fillIn.setParam(paramRoot + s_tcapErrCode,String(m_error.error()));
	else if (m_type == SS7TCAP::TC_L_Reject || m_type == SS7TCAP::TC_U_Reject || m_type == SS7TCAP::TC_R_Reject)
	    fillIn.setParam(paramRoot + s_tcapProblemCode,String(m_error.error()));
    }
    if (m_type == SS7TCAP::TC_L_Cancel) {
	fillIn.setParam(paramRoot + s_tcapOpCode,m_opCode);
	fillIn.setParam(paramRoot + s_tcapOpCodeType,m_opType);
    }
    if (m_type == SS7TCAP::TC_U_Reject || m_type == SS7TCAP::TC_R_Reject || m_type == SS7TCAP::TC_L_Reject)
	setState(Idle);
}

void SS7TCAPComponent::resetTimer(NamedList& params, unsigned int index)
{
    DDebug(m_transact->tcap(),DebugInfo,"SS7TCAPComponent::resetTimer() [%p]",this);
    String paramRoot;
    compPrefix(paramRoot,index,false);
    if (state() == OperationSent)
	m_opTimer.start();
    params.clearParam(paramRoot,'.');
}

// class SS7TCAPANSI

static u_int8_t s_tcapProtoVersion = 0x04;

static const PrimitiveMapping s_componentsANSIMap[] = {
    {SS7TCAP::TC_Invoke,              SS7TCAPTransactionANSI::InvokeLast},
    {SS7TCAP::TC_ResultLast,          SS7TCAPTransactionANSI::ReturnResultLast},
    {SS7TCAP::TC_U_Error,             SS7TCAPTransactionANSI::ReturnError},
    {SS7TCAP::TC_U_Reject,            SS7TCAPTransactionANSI::Reject},
    {SS7TCAP::TC_R_Reject,            SS7TCAPTransactionANSI::Reject},
    {SS7TCAP::TC_L_Reject,            SS7TCAPTransactionANSI::Reject},
    {SS7TCAP::TC_InvokeNotLast,       SS7TCAPTransactionANSI::InvokeNotLast},
    {SS7TCAP::TC_ResultNotLast,       SS7TCAPTransactionANSI::ReturnResultNotLast},
    {SS7TCAP::TC_L_Cancel,            SS7TCAPTransactionANSI::Local},
    {SS7TCAP::TC_U_Cancel,            SS7TCAPTransactionANSI::Local},
    {SS7TCAP::TC_TimerReset,          SS7TCAPTransactionANSI::Local},
};

static const PrimitiveMapping s_transANSIMap[] = {
    {SS7TCAP::TC_Unidirectional,              SS7TCAPTransactionANSI::Unidirectional},
    {SS7TCAP::TC_QueryWithPerm,               SS7TCAPTransactionANSI::QueryWithPermission},
    {SS7TCAP::TC_QueryWithoutPerm,            SS7TCAPTransactionANSI::QueryWithoutPermission},
    {SS7TCAP::TC_Begin,                       SS7TCAPTransactionANSI::QueryWithPermission}, // on receiving a ITU_T Begin, we'll map it to ANSI QueryWithPermission
    {SS7TCAP::TC_ConversationWithPerm,        SS7TCAPTransactionANSI::ConversationWithPermission},
    {SS7TCAP::TC_ConversationWithoutPerm,     SS7TCAPTransactionANSI::ConversationWithoutPermission},
    {SS7TCAP::TC_Continue,                    SS7TCAPTransactionANSI::ConversationWithPermission},
    {SS7TCAP::TC_Response,                    SS7TCAPTransactionANSI::Response},
    {SS7TCAP::TC_End,                         SS7TCAPTransactionANSI::Response},
    {SS7TCAP::TC_U_Abort,                     SS7TCAPTransactionANSI::Abort},
    {SS7TCAP::TC_P_Abort,                     SS7TCAPTransactionANSI::Abort},
    {SS7TCAP::TC_Notice,                      SS7TCAPTransactionANSI::Unknown},
    {SS7TCAP::TC_Unknown,                     SS7TCAPTransactionANSI::Unknown},
};

static const PrimitiveMapping* mapCompPrimitivesANSI(int primitive, int comp = -1)
{
    const PrimitiveMapping* map = s_componentsANSIMap;
    for (; map->primitive != SS7TCAP::TC_Unknown; map++) {
	if (primitive != -1) {
	    if (map->primitive == primitive )
		break;
	}
	else if (comp != -1)
	    if (map->mappedTo == comp)
		break;
    }
    return map;
}

static const PrimitiveMapping* mapTransPrimitivesANSI(int primitive, int trans = -1)
{
    const PrimitiveMapping* map = s_transANSIMap;
    for (; map->primitive != SS7TCAP::TC_Unknown; map++) {
	if (primitive != -1) {
	    if (map->primitive == primitive )
		break;
	}
	else if (trans != -1)
	    if (map->mappedTo == trans)
		break;
    }
    return map;
}

static const SS7TCAPTransactionANSI::ANSITransactionType primitiveToTransactANSI(String primitive,
	SS7TCAP::TCAPUserTransActions primitiveType = SS7TCAP::TC_Unknown)
{
    SS7TCAPTransactionANSI::ANSITransactionType type = SS7TCAPTransactionANSI::Unknown;

    if (!primitive.null())
	primitiveType = (SS7TCAP::TCAPUserTransActions)primitive.toInteger(SS7TCAP::s_transPrimitives);

    const PrimitiveMapping* map = mapTransPrimitivesANSI(primitiveType);
    if (map)
	type = (SS7TCAPTransactionANSI::ANSITransactionType)map->mappedTo;
    return type;
}

SS7TCAPANSI::SS7TCAPANSI(const NamedList& params)
    : SignallingComponent(params.safe("SS7TCAPANSI"),&params,"ss7-tcap-ansi"),
      SS7TCAP(params)
{
    String tmp;
    params.dump(tmp,"\r\n  ",'\'',true);
    DDebug(this,DebugAll,"SS7TCAPANSI::SS7TCAPANSI(%s)",tmp.c_str());
    setTCAPType(SS7TCAP::ANSITCAP);
}

SS7TCAPANSI::~SS7TCAPANSI()
{
    DDebug(this,DebugAll,"SS7TCAPANSI::~SS7TCAPANSI() [%p] destroyed with %d transactions, refCount=%d",
		this,m_transactions.count(),refcount());
}

SS7TCAPTransaction* SS7TCAPANSI::buildTransaction(SS7TCAP::TCAPUserTransActions type, const String& transactID, NamedList& params,
	bool initLocal)
{
    return new SS7TCAPTransactionANSI(this,type,transactID,params,m_trTimeout,initLocal);
}

SS7TCAPError SS7TCAPANSI::decodeTransactionPart(NamedList& params, DataBlock& data)
{
    SS7TCAPError error(SS7TCAP::ANSITCAP);
    if (data.length() < 2)  // should find out which is the minimal TCAP message length
	return error;

    // decode message type
    u_int8_t msgType = data[0];
    data.cut(-1);

    const PrimitiveMapping* map = mapTransPrimitivesANSI(-1,msgType);
    if (map) {
	String type = lookup(map->primitive,SS7TCAP::s_transPrimitives,"Unknown");
	params.setParam(s_tcapRequest,type);
    }

    // decode message length
    unsigned int len = ASNLib::decodeLength(data);
    if (len != data.length())
	return error;
    // decode transaction IDs, start with Transaction Identifier
    u_int8_t tag = data[0];
    if (tag != TransactionIDTag) {// 0xc7
	error.setError(SS7TCAPError::Transact_IncorrectTransactionPortion);
	return error; // check it
    }
    data.cut(-1);

    // if we'll detect an error, it should be a BadlyStructuredTransaction error
    error.setError(SS7TCAPError::Transact_BadlyStructuredTransaction);

    len = ASNLib::decodeLength(data);
    if (len > data.length() || data.length() < len || (len != 0 && len != 4 && len != 8))
	return error;

    // transaction IDs shall be decoded according to message type
    String tid1, tid2;
    if (len  > 0 ) {
	tid1.hexify(data.data(),4,' ');
	data.cut(-4);
	if (len == 8) {
	    tid2.hexify(data.data(),4,' ');
	    data.cut(-4);
	}
    }
    switch (msgType) {
	case SS7TCAPTransactionANSI::Unidirectional:
	    if (len != 0)
		return error;
	    break;
	case SS7TCAPTransactionANSI::QueryWithPermission:
	case SS7TCAPTransactionANSI::QueryWithoutPermission:
	    if (len != 4)
		return error;
	    params.setParam(s_tcapRemoteTID,tid1);
	    break;
	case SS7TCAPTransactionANSI::Response:
	case SS7TCAPTransactionANSI::Abort:
	    if (len != 4)
		return error;
	    params.setParam(s_tcapLocalTID,tid1);
	    break;
	case SS7TCAPTransactionANSI::ConversationWithPermission:
	case SS7TCAPTransactionANSI::ConversationWithoutPermission:
	    if (len != 8)
		return error;
	    params.setParam(s_tcapRemoteTID,tid1);
	    params.setParam(s_tcapLocalTID,tid2);
	    break;
	default:
	    error.setError(SS7TCAPError::Transact_UnrecognizedPackageType);
	    return error;
    };

#ifdef DEBUG
    if (s_printMsgs && s_extendedDbg && debugAt(DebugAll))
	dumpData(DebugAll,this,"SS7TCAPANSI::decodeTransactionPart() finished",this,params,data);
#endif

    error.setError(SS7TCAPError::NoError);
    return error;
}

void SS7TCAPANSI::encodeTransactionPart(NamedList& params, DataBlock& data)
{
#ifdef DEBUG
    if (s_printMsgs && s_extendedDbg && debugAt(DebugAll))
	dumpData(DebugAll,this,"SS7TCAPANSI::encodeTransactionPart() - to be encoded",this,params,data);
#endif

    int msgType = primitiveToTransactANSI(params.getValue(s_tcapRequest));

    const String& otid = params[s_tcapLocalTID];
    const String& dtid = params[s_tcapRemoteTID];

    String ids;
    switch (msgType) {
	case SS7TCAPTransactionANSI::Unidirectional:
	    break;
	case SS7TCAPTransactionANSI::QueryWithPermission:
	case SS7TCAPTransactionANSI::QueryWithoutPermission:
	    ids = otid;
	    break;
	case SS7TCAPTransactionANSI::Response:
	case SS7TCAPTransactionANSI::Abort:
	    ids = dtid;
	    break;
	case SS7TCAPTransactionANSI::ConversationWithPermission:
	case SS7TCAPTransactionANSI::ConversationWithoutPermission:
	    ids << otid << " " << dtid;
	    break;
	default:
	    break;
    };

    DataBlock db;
    db.unHexify(ids.c_str(),ids.length(),' ');
    db.insert(ASNLib::buildLength(db));
    int tag = TransactionIDTag;
    db.insert(DataBlock(&tag,1));

    data.insert(db);
    data.insert(ASNLib::buildLength(data));
    data.insert(DataBlock(&msgType,1));
}

/**
 * SS7TCAPTransactionANSI implementation
 */
const TokenDict SS7TCAPTransactionANSI::s_ansiTransactTypes[] = {
    {"Unidirectional",                SS7TCAPTransactionANSI::Unidirectional},
    {"QueryWithPermission",           SS7TCAPTransactionANSI::QueryWithPermission},
    {"QueryWithoutPermission",        SS7TCAPTransactionANSI::QueryWithoutPermission},
    {"Response",                      SS7TCAPTransactionANSI::Response},
    {"ConversationWithPermission",    SS7TCAPTransactionANSI::ConversationWithPermission},
    {"ConversationWithoutPermission", SS7TCAPTransactionANSI::ConversationWithoutPermission},
    {"Abort",                         SS7TCAPTransactionANSI::Abort},
    {0,0},
};

SS7TCAPTransactionANSI::SS7TCAPTransactionANSI(SS7TCAP* tcap, SS7TCAP::TCAPUserTransActions type,
	const String& transactID, NamedList& params, u_int64_t timeout, bool initLocal)
    : SS7TCAPTransaction(tcap,type,transactID,params,timeout,initLocal),
      m_prevType(type)
{
    DDebug(tcap,DebugAll,"SS7TCAPTransactionANSI[%p] created with type='%s' and localID='%s'",this,
	    lookup(type,SS7TCAP::s_transPrimitives),m_localID.c_str());
}

SS7TCAPTransactionANSI::~SS7TCAPTransactionANSI()
{
    DDebug(tcap(),DebugAll,"Transaction with ID=%s of user=%s destroyed, TCAP refcount=%d [%p]",
	   m_localID.c_str(),m_userName.c_str(),tcap()->refcount(),this);
}

SS7TCAPError SS7TCAPTransactionANSI::handleData(NamedList& params, DataBlock& data)
{
    XDebug(tcap(),DebugAll,"SS7TCAPTransactionANSI::handleData() transactionID=%s data length=%u [%p]",m_localID.c_str(),
	   data.length(),this);
    Lock lock(this);
    // decode DialogPortion
    SS7TCAPError error = decodeDialogPortion(params,data);
    if (error.error() != SS7TCAPError::NoError)
	return error;
    error = handleDialogPortion(params,false);
    if (error.error() != SS7TCAPError::NoError)
	return error;

    // in case of Abort message, check Cause Information
    String msg = params.getValue(s_tcapMsgType);
    if (msg.toInteger(s_ansiTransactTypes) == Abort) {
	error = decodePAbort(this,params,data);
	if (error.error() != SS7TCAPError::NoError)
	    return error;
    }
    // decodeComponents
    error = decodeComponents(params,data);
    if (error.error() != SS7TCAPError::NoError)
	buildComponentError(error,params,data);

    error = handleComponents(params,false);
    return error;
}

SS7TCAPError SS7TCAPTransactionANSI::update(SS7TCAP::TCAPUserTransActions type, NamedList& params, bool updateByUser)
{
    DDebug(tcap(),DebugAll,"SS7TCAPTransactionANSI::update() [%p], localID=%s - update to type=%s initiated by %s",this,m_localID.c_str(),
	lookup(type,SS7TCAP::s_transPrimitives,"Unknown"), (updateByUser ? "user" : "remote"));
#ifdef DEBUG
    if (s_printMsgs && s_extendedDbg && debugAt(DebugAll))
	dumpData(DebugAll,tcap(),"SS7TCAPTransactionANSI::update() with",this,params);
#endif
    Lock l(this);
    SS7TCAPError error(SS7TCAP::ANSITCAP);
    switch (type) {
	case SS7TCAP::TC_Begin:
	case SS7TCAP::TC_QueryWithPerm:
	case SS7TCAP::TC_QueryWithoutPerm:
	case SS7TCAP::TC_Unidirectional:
	    Debug(tcap(),DebugInfo,"SS7TCAPTransactionANSI::update() [%p], localID=%s - invalid update: trying to update from type=%s to type=%s",
		    this,m_localID.c_str(),lookup(m_type,SS7TCAP::s_transPrimitives,"Unknown"),
		    lookup(type,SS7TCAP::s_transPrimitives,"Unknown"));
	    params.setParam(s_tcapRequestError,"invalid_update");
	    params.setParam("tcap.request.error.currentState",lookup(m_type,SS7TCAP::s_transPrimitives,"Unknown"));
	    error.setError(SS7TCAPError::Transact_IncorrectTransactionPortion);
	    return error;

	case SS7TCAP::TC_End:
	case SS7TCAP::TC_Response:
	    if (m_type == SS7TCAP::TC_QueryWithoutPerm || m_type == SS7TCAP::TC_ConversationWithoutPerm) {
		params.setParam(s_tcapRequestError,"invalid_update");
		params.setParam("tcap.request.error.currentState",lookup(m_type,SS7TCAP::s_transPrimitives,"Unknown"));
		error.setError(SS7TCAPError::Transact_IncorrectTransactionPortion);
		return error;
	    }
	    else {
		if (!m_basicEnd)
		    // prearranged end, no need to transmit to remote end
		    m_transmit = NoTransmit;
		else
		    m_transmit = PendingTransmit;
		m_type = type;
	    }
	    break;
	case SS7TCAP::TC_Continue:
	case SS7TCAP::TC_ConversationWithPerm:
	case SS7TCAP::TC_ConversationWithoutPerm:
	    if (m_type == SS7TCAP::TC_End || m_type == SS7TCAP::TC_Response) {
		params.setParam(s_tcapRequestError,"invalid_update");
		params.setParam("tcap.request.error.currentState",lookup(m_type,SS7TCAP::s_transPrimitives,"Unknown"));
		error.setError(SS7TCAPError::Transact_IncorrectTransactionPortion);
		return error;
	    }
	    else {
		m_remoteID = params.getValue(s_tcapRemoteTID);
		m_type = type;
		m_transmit = PendingTransmit;
	    }
	    break;
	case SS7TCAP::TC_Notice:
	case SS7TCAP::TC_P_Abort:
	    if (updateByUser) {
		Debug(tcap(),DebugInfo,"SS7TCAPTransactionANSI::update() [%p], localID=%s - invalid update: trying to update from type=%s to type=%s",
		    this,m_localID.c_str(),lookup(m_type,SS7TCAP::s_transPrimitives,"Unknown"),
		    lookup(type,SS7TCAP::s_transPrimitives,"Unknown"));
		params.setParam(s_tcapRequestError,"invalid_update");
		params.setParam("tcap.request.error.currentState",lookup(m_type,SS7TCAP::s_transPrimitives,"Unknown"));
		error.setError(SS7TCAPError::Transact_IncorrectTransactionPortion);
		return error;
	    }
	case SS7TCAP::TC_U_Abort:
	    if (!updateByUser && String("pAbort") == params.getValue(s_tcapAbortCause))
		m_type = SS7TCAP::TC_P_Abort;
	    else
		m_type = type;
	    m_transmit = PendingTransmit;
	    break;
	default:
	    break;
    }

    populateSCCPAddress(m_localSCCPAddr,m_remoteSCCPAddr,params,updateByUser);
    if (updateByUser) {
	setState(PackageSent);
	m_basicEnd = params.getBoolValue(s_tcapBasicTerm,true);
	m_endNow = params.getBoolValue(s_tcapEndNow,false);
    }
    else
	setState(PackageReceived);
    if (m_timeout.started()) {
	m_timeout.stop();
	XDebug(tcap(),DebugInfo,"SS7TCAPTransactionANSI::update() [%p], localID=%s - timeout timer has been stopped",this,m_localID.c_str());
    }
    return error;
}

SS7TCAPError SS7TCAPTransactionANSI::decodeDialogPortion(NamedList& params, DataBlock& data)
{
    XDebug(tcap(),DebugAll,"SS7TCAPTransactionANSI::decodeDialogPortion() for transaction with localID=%s [%p]",
	m_localID.c_str(),this);

    SS7TCAPError error(SS7TCAP::ANSITCAP);

    u_int8_t tag = data[0];
    // dialog is not present
    if (tag != SS7TCAPANSI::DialogPortionTag) // 0xf9
	return error;
    data.cut(-1);

    // dialog portion is present, decode dialog length
    int len = ASNLib::decodeLength(data);
    if (len < 0 || len > (int)data.length()) {
	error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
	return error;
    }

    tag = data[0];
    // check for protocol version
    if (data[0] == SS7TCAPANSI::ProtocolVersionTag) { //0xda
	data.cut(-1);
	// decode protocol version
	u_int8_t proto;
	len = ASNLib::decodeUINT8(data,&proto,false);
	if (len != 1) {
	    error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
	    return error;
	}
	params.setParam(s_tcapProtoVers,String(proto));
    }

    tag = data[0];
    // check for Application Context
    if (tag == SS7TCAPANSI::IntApplicationContextTag || tag == SS7TCAPANSI::OIDApplicationContextTag) { // 0xdb , 0xdc
	data.cut(-1);
	 if (tag == SS7TCAPANSI::IntApplicationContextTag) { //0xdb
	    u_int64_t val = 0;
	    len = ASNLib::decodeInteger(data,val,sizeof(int),false);
	    if (len < 0) {
		error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
		return error;
	    }
	    params.setParam(s_tcapIntAppID,String((int)val));
	}
	if (tag == SS7TCAPANSI::OIDApplicationContextTag) { // oxdc
	    ASNObjId oid;
	    len = ASNLib::decodeOID(data,&oid,false);
	    if (len < 0) {
		error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
		return error;
	    }
	    params.setParam(s_tcapObjAppID,oid.toString());
	}
    }

    // check for user information
    tag = data[0];
    if (tag == SS7TCAPANSI::UserInformationTag) {// 0xfd
	data.cut(-1);
	len = ASNLib::decodeLength(data);
	if (len < 0) {
	    error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
	    return error;
	}

	tag = data[0];
	if (tag != SS7TCAPANSI::ExternalTag) {
	    error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
	    return error;
	}
	data.cut(-1);

	len = ASNLib::decodeLength(data);
	if (len < 0 || len > (int)data.length()) {
	    error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
	    return error;
	}
	// direct Reference
	tag = data[0];
	if (tag == SS7TCAPANSI::DirectReferenceTag) { // 0x06
	    data.cut(-1);
	    ASNObjId oid;
	    len = ASNLib::decodeOID(data,&oid,false);
	    if (len < 0) {
		error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
		return error;
	    }
	    params.setParam(s_tcapReference,oid.toString());
	}
	// data Descriptor
	tag = data[0];
	if (tag == SS7TCAPANSI::DataDescriptorTag) { // 0x07
	    data.cut(-1);
	    String str;
	    int type;
	    len = ASNLib::decodeString(data,&str,&type,false);
	    if (len < 0) {
		error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
		return error;
	    }
	    params.setParam(s_tcapDataDesc,str);
	}
	// encoding
	tag = data[0];
	if (tag == SS7TCAPANSI::SingleASNTypePEncTag || tag == SS7TCAPANSI::SingleASNTypeCEncTag ||
	    tag == SS7TCAPANSI::OctetAlignEncTag || tag == SS7TCAPANSI::ArbitraryEncTag) {
	    data.cut(-1);
	    len = ASNLib::decodeLength(data);
	    if (len < 0) {
		error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
		return error;
	    }
	    DataBlock d((void*)data.data(0,len),len);
	    data.cut(-len);

	    // put encoding context in hexified form
	    String dataHexified;
	    dataHexified.hexify(d.data(),d.length(),' ');
	    params.setParam(s_tcapEncodingContent,dataHexified);
	    // put encoding identifier
	    switch (tag) {
		case SS7TCAPANSI::SingleASNTypePEncTag: // 0x80
		    params.setParam(s_tcapEncodingType,"single-ASN1-type-primitive");
		    break;
		case SS7TCAPANSI::SingleASNTypeCEncTag: // 0xa0
		    params.setParam(s_tcapEncodingType,"single-ASN1-type-contructor");
		    break;
		case SS7TCAPANSI::OctetAlignEncTag:     // 0x81
		    params.setParam(s_tcapEncodingType,"octet-aligned");
		    break;
		case SS7TCAPANSI::ArbitraryEncTag:      // 0x82
		    params.setParam(s_tcapEncodingType,"arbitrary");
		    break;
		default:
		    break;
	    }
	}
    }

    // check for security context
    tag = data[0];
    if (tag == SS7TCAPANSI::IntSecurityContextTag || tag == SS7TCAPANSI::OIDSecurityContextTag) {
	data.cut(-1);
	if (tag == SS7TCAPANSI::IntSecurityContextTag) { //0x80
	    int val = 0;
	    len = ASNLib::decodeINT32(data,&val,false);
	    if (len < 0) {
		error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
		return error;
	    }
	    params.setParam(s_tcapIntSecID,String(val));
	}
	if (tag == SS7TCAPANSI::OIDSecurityContextTag) { // 0x81
	    ASNObjId oid;
	    len = ASNLib::decodeOID(data,&oid,false);
	    if (len < 0) {
		error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
		return error;
	    }
	    params.setParam(s_tcapObjSecID,oid.toString());
	}
    }

    // check for Confidentiality information
    tag = data[0];
    if (tag == SS7TCAPANSI::ConfidentialityTag) { // 0xa2
	data.cut(-1);
	len = ASNLib::decodeLength(data);
	if (len < 0) {
	    error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
	    return error;
	}
	tag = data[0];
	if (tag == SS7TCAPANSI::IntSecurityContextTag || tag == SS7TCAPANSI::OIDSecurityContextTag) {
	    data.cut(-1);
	    if (tag == SS7TCAPANSI::IntSecurityContextTag) { //0x80
		int val = 0;
		len = ASNLib::decodeINT32(data,&val,false);
		if (len < 0) {
		    error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
		    return error;
		}
		params.setParam(s_tcapIntConfidID,String(val));
	    }
	    if (tag == SS7TCAPANSI::OIDSecurityContextTag) { // 0x81
		ASNObjId oid;
		len = ASNLib::decodeOID(data,&oid,false);
		if (len < 0) {
		    error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
		    return error;
		}
		params.setParam(s_tcapObjConfidID,oid.toString());
	    }
	}
    }
#ifdef DEBUG
     if (s_printMsgs && s_extendedDbg && debugAt(DebugAll))
	 dumpData(DebugAll,tcap(),"SS7TCAPTransactionANSI::decodeDialogPortion() - decoded dialog portion",this,params,data);
#endif
    return error;
}

void SS7TCAPTransactionANSI::encodeDialogPortion(NamedList& params, DataBlock& data)
{
    XDebug(tcap(),DebugAll,"SS7TCAPTransactionANSI::encodeDialogPortion() for transaction with localID=%s [%p]",m_localID.c_str(),this);

    DataBlock dialogData;
    int tag;

    // encode confidentiality information
    NamedString* val = params.getParam(s_tcapIntConfidID);
    NamedString* oidStr = params.getParam(s_tcapObjConfidID);
    ASNObjId oid;

    if (!TelEngine::null(val) && !TelEngine::null(oidStr)) {
	// parameter error, encoding of this portion skipped
	Debug(tcap(),DebugInfo,"SS7TCAPTransactionANSI::encodeDialogPortion() - skipping encoding of Confidentiality Information,"
	      " both IntegerConfidentialityAlgorithmID=%s and ObjectIDConfidentialityID=%s specified, can't pick one",
	      val->c_str(),oidStr->c_str());
    }
    else {
	if (!TelEngine::null(val)) {
	    DataBlock db = ASNLib::encodeInteger(val->toInteger(),false);
	    db.insert(ASNLib::buildLength(db));
	    tag = SS7TCAPANSI::IntSecurityContextTag;
	    db.insert(DataBlock(&tag,1));

	    dialogData.insert(db);
	}
	else if (!TelEngine::null(oidStr)) {
	    oid = *oidStr;
	    DataBlock db = ASNLib::encodeOID(oid,false);
	    db.insert(ASNLib::buildLength(db));
	    tag = SS7TCAPANSI::OIDSecurityContextTag;
	    db.insert(DataBlock(&tag,1));

	    dialogData.insert(db);
	}
	if (dialogData.length()) {
	    dialogData.insert(ASNLib::buildLength(dialogData));
	    tag = SS7TCAPANSI::ConfidentialityTag;
	    dialogData.insert(DataBlock(&tag,1));
	}
    }
    // encode security information
    val = params.getParam(s_tcapIntSecID);
    oidStr = params.getParam(s_tcapObjSecID);

    if (!TelEngine::null(val) && !TelEngine::null(oidStr)) {
	// parameter error, encoding of this portion skipped
	Debug(tcap(),DebugInfo,"SS7TCAPTransactionANSI::encodeDialogPortion() - skipping encoding of Security Context Information,"
	      " both IntegerSecurityContext=%s and ObjectIDSecurityContext=%s specified, can't pick one",
	      val->c_str(),oid.toString().c_str());
    }
    else if (!TelEngine::null(val)) {
	DataBlock db = ASNLib::encodeInteger(val->toInteger(),false);
	db.insert(ASNLib::buildLength(db));
	tag = SS7TCAPANSI::IntSecurityContextTag;
	db.insert(DataBlock(&tag,1));

	dialogData.insert(db);
    }
    else if (!TelEngine::null(oidStr)) {
	oid = *oidStr;
	DataBlock db = ASNLib::encodeOID(oid,false);
	db.insert(ASNLib::buildLength(db));
	tag = SS7TCAPANSI::OIDSecurityContextTag;
	db.insert(DataBlock(&tag,1));

	dialogData.insert(db);
    }

    // encode user information
    DataBlock userInfo;
    val = params.getParam(s_tcapEncodingType);
    if (!TelEngine::null(val)) {
	if (*val == "single-ASN1-type-primitive")
	    tag = SS7TCAPANSI::SingleASNTypePEncTag;
	else if (*val == "single-ASN1-type-contructor")
	    tag = SS7TCAPANSI::SingleASNTypeCEncTag;
	else if (*val == "octet-aligned")
	    tag = SS7TCAPANSI::OctetAlignEncTag;
	else if (*val == "arbitrary")
	    tag = SS7TCAPANSI::ArbitraryEncTag;

	val = params.getParam(s_tcapEncodingContent);
	if (val) {
	    DataBlock db;
	    db.unHexify(val->c_str(),val->length(),' ');
	    db.insert(ASNLib::buildLength(db));
	    db.insert(DataBlock(&tag,1));

	    userInfo.insert(db);
	}
    }
    val = params.getParam(s_tcapDataDesc);
    if (!TelEngine::null(val)) {
	DataBlock db = ASNLib::encodeString(*val,ASNLib::PRINTABLE_STR,false);
	db.insert(ASNLib::buildLength(db));
	tag = SS7TCAPANSI::DataDescriptorTag;
	db.insert(DataBlock(&tag,1));

	userInfo.insert(db);
    }
    val = params.getParam(s_tcapReference);
    if (!TelEngine::null(val)) {
	oid = *val;
	DataBlock db = ASNLib::encodeOID(oid,false);
	db.insert(ASNLib::buildLength(db));
	tag = SS7TCAPANSI::DirectReferenceTag;
	db.insert(DataBlock(&tag,1));

	userInfo.insert(db);
    }

    if (userInfo.length()) {
	userInfo.insert(ASNLib::buildLength(userInfo));
	tag = SS7TCAPANSI::ExternalTag;
	userInfo.insert(DataBlock(&tag,1));
	userInfo.insert(ASNLib::buildLength(userInfo));
	tag = SS7TCAPANSI::UserInformationTag;
	userInfo.insert(DataBlock(&tag,1));

	dialogData.insert(userInfo);
    }

    // Aplication context
    val = params.getParam(s_tcapIntAppID);
    oidStr = params.getParam(s_tcapObjAppID);
    if (!TelEngine::null(val) && !TelEngine::null(oidStr)) {
	// parameter error, encoding of this portion skipped
	Debug(tcap(),DebugInfo,"SS7TCAPTransactionANSI::encodeDialogPortion() - skipping encoding of Application Context Information,"
	    " both IntegerApplicationID=%s and ObjectApplicationID=%s specified, can't pick one",val->c_str(),oid.toString().c_str());
    }
    else if (!TelEngine::null(val)) {
	DataBlock db = ASNLib::encodeInteger(val->toInteger(),false);
	db.insert(ASNLib::buildLength(db));
	tag = SS7TCAPANSI::IntApplicationContextTag;
	db.insert(DataBlock(&tag,1));

	dialogData.insert(db);
    }
    else if (!TelEngine::null(oidStr)) {
	oid = *oidStr;
	DataBlock db = ASNLib::encodeOID(oid,false);
	db.insert(ASNLib::buildLength(db));
	tag = SS7TCAPANSI::OIDApplicationContextTag;
	db.insert(DataBlock(&tag,1));

	dialogData.insert(db);
    }

    val = params.getParam(s_tcapProtoVers);
    if (!TelEngine::null(val)) {
	u_int8_t proto = val->toInteger();
	DataBlock db  = ASNLib::encodeInteger(proto,false);
	db.insert(ASNLib::buildLength(db));
	tag = SS7TCAPANSI::ProtocolVersionTag;
	db.insert(DataBlock(&tag,1));
	dialogData.insert(db);
    }

    if (dialogData.length()) {
	dialogData.insert(ASNLib::buildLength(dialogData));
	tag = SS7TCAPANSI::DialogPortionTag;
	dialogData.insert(DataBlock(&tag,1));
    }

    data.insert(dialogData);
    params.clearParam(s_tcapDialogPrefix,'.');
#ifdef DEBUG
     if (s_printMsgs && s_extendedDbg && debugAt(DebugAll))
	 dumpData(DebugAll,tcap(),"SS7TCAPTransactionANSI::encodeDialogPortion() - encoded dialog portion",this,params,data);
#endif
}

SS7TCAPError SS7TCAPTransactionANSI::decodePAbort(SS7TCAPTransaction* tr, NamedList& params, DataBlock& data)
{
    u_int8_t tag = data[0];
    SS7TCAPError error(SS7TCAP::ANSITCAP);
    if (tag == SS7TCAPANSI::PCauseTag || tag == SS7TCAPANSI::UserAbortPTag ||  tag == SS7TCAPANSI::UserAbortCTag) {
	SS7TCAPError error(SS7TCAP::ANSITCAP);
	data.cut(-1);
	if (tag == SS7TCAPANSI::PCauseTag) {
	    u_int8_t pCode = 0;
	    int len = ASNLib::decodeUINT8(data,&pCode,false);
	    if (len != 1) {
		error.setError(SS7TCAPError::Transact_BadlyStructuredTransaction);
		return error;
	    }
	    params.setParam(s_tcapAbortCause,"pAbort");
	    params.setParam(s_tcapAbortInfo,String(SS7TCAPError::errorFromCode(SS7TCAP::ANSITCAP,pCode)));
	}
	else {
	    int len = ASNLib::decodeLength(data);
	    if (len < 0) {
		error.setError(SS7TCAPError::Transact_BadlyStructuredTransaction);
		return error;
	    }
	    String str;
	    str.hexify(data.data(0,len),len,' ');
	    data.cut(-len);
	    params.setParam(s_tcapAbortCause,(tag == SS7TCAPANSI::UserAbortPTag ? "userAbortP" : "userAbortC"));
	    params.setParam(s_tcapAbortInfo,str);
	    if (tr)
		tr->setTransactionType(SS7TCAP::TC_U_Abort);
	}
#ifdef DEBUG
     if (tr && tr->tcap() && s_printMsgs && s_extendedDbg && debugAt(DebugAll))
	 dumpData(DebugAll,tr->tcap(),"SS7TCAPTransactionANSI::decodePAbort() - decoded Abort info",tr,params,data);
#endif
    }
    return error;
}

void SS7TCAPTransactionANSI::encodePAbort(SS7TCAPTransaction* tr, NamedList& params, DataBlock& data)
{
    NamedString* pAbortCause = params.getParam(s_tcapAbortCause);
    DataBlock db;
    if (!TelEngine::null(pAbortCause)) {
	int tag = 0;
	if (*pAbortCause == "pAbort") {
	    tag = SS7TCAPANSI::PCauseTag;
	    u_int16_t pCode = SS7TCAPError::codeFromError(SS7TCAP::ANSITCAP,params.getIntValue(s_tcapAbortInfo));
	    if (pCode) {
		db.append(ASNLib::encodeInteger(pCode,false));
		db.insert(ASNLib::buildLength(db));
	    }
	}
	else if (*pAbortCause == "userAbortP" || *pAbortCause == "userAbortC") {
	    NamedString* info = params.getParam(s_tcapAbortInfo);
	    if (!TelEngine::null(info))
		db.unHexify(info->c_str(),info->length(),' ');
	    db.insert(ASNLib::buildLength(db));
	    if (*pAbortCause == "userAbortP")
		tag = SS7TCAPANSI::UserAbortPTag;
	    else
		tag = SS7TCAPANSI::UserAbortCTag;
	}
	if (db.length())
	    db.insert(DataBlock(&tag,1));
    }
    if (db.length()) {
	data.insert(db);
	params.clearParam(s_tcapAbortCause);
	params.clearParam(s_tcapAbortInfo);
    }
#ifdef DEBUG
     if (tr && tr->tcap() && s_printMsgs && s_extendedDbg && debugAt(DebugAll))
	 dumpData(DebugAll,tr->tcap(),"SS7TCAPTransactionANSI::encodePAbort() - encoded Abort info",tr,params,data);
#endif
}

void SS7TCAPTransactionANSI::requestContent(NamedList& params, DataBlock& data)
{
#ifdef DEBUG
    if (s_extendedDbg)
	DDebug(tcap(),DebugAll,"SS7TCAPTransactionANSI::requestContent() for transaction with id=%s [%p]",m_localID.c_str(),this);
#endif
    if (m_type == SS7TCAP::TC_P_Abort || m_type == SS7TCAP::TC_U_Abort)
	encodePAbort(this,params,data);
    else
	requestComponents(params,data);
    encodeDialogPortion(params,data);
    transactionData(params);
}

void SS7TCAPTransactionANSI::updateToEnd()
{
    if (transactionType() == SS7TCAP::TC_QueryWithoutPerm || transactionType() == SS7TCAP::TC_ConversationWithoutPerm)
	setTransactionType(SS7TCAP::TC_U_Abort);
    else
	setTransactionType(SS7TCAP::TC_Response);
}

SS7TCAPError SS7TCAPTransactionANSI::decodeComponents(NamedList& params, DataBlock& data)
{
    XDebug(tcap(),DebugAll,"SS7TCAPTransactionANSI::decodeComponents() [%p] - data length=%u",this,data.length());

    SS7TCAPError error(SS7TCAP::ANSITCAP);
    if (!data.length()) {
	params.setParam(s_tcapCompCount,"0");
	return error;
    }

    u_int8_t tag = data[0];
    if (tag != SS7TCAPANSI::ComponentPortionTag) { // 0xe8
	error.setError(SS7TCAPError::General_IncorrectComponentPortion);
	return error;
    }
    data.cut(-1);

    // decode length of component portion
    int len = ASNLib::decodeLength(data);
    bool checkEoC = (len == ASNLib::IndefiniteForm);
    if (!checkEoC && (len < 0 || len != (int)data.length())) { // the length of the remaining data should be the same as the decoded length()
	error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
	return error;
    }

    unsigned int compCount = 0;
    while (data.length()) {
	if (checkEoC && ASNLib::matchEOC(data) > 0)
	    break;
	compCount++;
	// decode component type
	u_int8_t compType = data[0];
	data.cut(-1);

	// verify component length
	len = ASNLib::decodeLength(data);
	if (len < 0 || len > (int)data.length()) {
	    error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
	    break;
	}

	// decode component IDs, start with ComponentsIDs identifier
	tag = data[0];
	if (tag != SS7TCAPANSI::ComponentsIDsTag) {// 0xcf
	    error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
	    break;
	}
	data.cut(-1);

	// obtain component ID(s)
	u_int16_t compIDs;
	len = ASNLib::decodeUINT16(data,&compIDs,false);
	if (len < 0) {
	    error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
	    break;
	}

	String compParam;
	compPrefix(compParam,compCount,false);
	// comp IDs shall be decoded according to component type
	switch (compType) {
	    case InvokeLast:
	    case InvokeNotLast:
		if (len == 1)
		    params.setParam(compParam + "." + s_tcapRemoteCID,String(compIDs));
		else if (len == 2) {
		    params.setParam(compParam + "." + s_tcapRemoteCID,String(compIDs >> 8));
		    params.setParam(compParam + "." + s_tcapLocalCID,String((u_int8_t)compIDs));
		}
		else {
		    params.setParam(compParam + "." + s_tcapRemoteCID,"");
		    params.setParam(compParam + "." + s_tcapLocalCID,"");
		}
		break;
	    case ReturnResultLast:
	    case ReturnError:
	    case Reject:
	    case ReturnResultNotLast:
		if (len != 1)
		    error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
		else
		    params.setParam(compParam + "." + s_tcapLocalCID,String(compIDs));
		break;
	    default:
		error.setError(SS7TCAPError::General_UnrecognizedComponentType);
		break;
	}
	const PrimitiveMapping* map = mapCompPrimitivesANSI(-1,compType);
	if (!map) {
	    error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
	    break;
	}
	params.setParam(compParam + "." + s_tcapCompType,lookup(map->primitive,SS7TCAP::s_compPrimitives,"Unknown"));

	if (error.error() != SS7TCAPError::NoError)
	    break;

	// decode Operation Code
	tag = data[0];
	if (tag == SS7TCAPANSI::OperationNationalTag || tag == SS7TCAPANSI::OperationPrivateTag) {
	    data.cut(-1);

	    int opCode = 0;
	    len = ASNLib::decodeINT32(data,&opCode,false);
	    if (tag == SS7TCAPANSI::OperationNationalTag) {
		if (len != 2) {
		    error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
		    break;
		}
		params.setParam(compParam + "." + s_tcapOpCodeType,"national");
	    }
	    if (tag == SS7TCAPANSI::OperationPrivateTag)
		params.setParam(compParam + "." + s_tcapOpCodeType,"private");
	    params.setParam(compParam + "." + s_tcapOpCode,String(opCode));
	}

	// decode  Error Code
	tag = data[0];
	if (tag == SS7TCAPANSI::ErrorNationalTag || tag == SS7TCAPANSI::ErrorPrivateTag) { // 0xd3, 0xd4
	    data.cut(-1);

	    int errCode = 0;
	    len = ASNLib::decodeINT32(data,&errCode,false);
	    if (len < 0 || (tag == SS7TCAPANSI::ErrorNationalTag && len != 1)) {
		error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
		break;
	    }
	    if (tag == SS7TCAPANSI::ErrorNationalTag)
		params.setParam(compParam + "." + s_tcapErrCodeType,"national");
	    else
		params.setParam(compParam + "." + s_tcapErrCodeType,"private");
	    params.setParam(compParam + "." + s_tcapErrCode,String(errCode));
	}

	// decode Problem
	tag = data[0];
	if (tag == SS7TCAPANSI::ProblemCodeTag) { // 0xd5
	    data.cut(-1);
	    u_int16_t problemCode = 0;
	    len = ASNLib::decodeUINT16(data,&problemCode,false);
	    if (len != 2) {
		error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
		break;
	    }
	    params.setParam(compParam + "." + s_tcapProblemCode,String(SS7TCAPError::errorFromCode(tcap()->tcapType(),problemCode)));
	}
	// decode Parameters (Set or Sequence) as payload
	tag = data[0];
	String dataHexified = "";
	if (tag == SS7TCAPANSI::ParameterSetTag || tag == SS7TCAPANSI::ParameterSeqTag) { // 0xf2 0x30
		data.cut(-1);
		len = ASNLib::decodeLength(data);
		if (len < 0 || len > (int)data.length()) {
		    error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
		    break;
		}
		DataBlock d((void*)data.data(0,len),len);
		data.cut(-len);
		d.insert(ASNLib::buildLength(d));
		d.insert(DataBlock(&tag,1));
		dataHexified.hexify(d.data(),d.length(),' ');
	    }
	params.setParam(compParam,dataHexified);
    }

    params.setParam(s_tcapCompCount,String(compCount));
#ifdef DEBUG
    if (tcap() && s_printMsgs && s_extendedDbg && debugAt(DebugAll))
	dumpData(DebugAll,tcap(),"Finished decoding message",this,params,data);
#endif
    return error;
}

void SS7TCAPTransactionANSI::encodeComponents(NamedList& params, DataBlock& data)
{
    XDebug(tcap(),DebugAll,"SS7TCAPTransactionANSI::encodeComponents() for transaction with localID=%s [%p]",m_localID.c_str(),this);

    int componentCount = params.getIntValue(s_tcapCompCount,0);
    DataBlock compData;
    if (componentCount) {
	int index = componentCount + 1;

	while (--index) {
	    DataBlock codedComp;
	    // encode parameters
	    String compParam;
	    compPrefix(compParam,index,false);
	    // Component Type
	    NamedString* value = params.getParam(compParam + "." + s_tcapCompType);
	    if (TelEngine::null(value))
		continue;
	    int compPrimitive = lookup(*value,SS7TCAP::s_compPrimitives);
	    const PrimitiveMapping* map = mapCompPrimitivesANSI(compPrimitive,-1);
	    if (!map)
		continue;
	    int compType = map->mappedTo;
	    String payloadHex = params.getValue(compParam,"");
	    if (!payloadHex.null()) {
		DataBlock payload;
		payload.unHexify(payloadHex.c_str(),payloadHex.length(),' ');
		//payload.insert(ASNLib::buildLength(payload));
		codedComp.insert(payload);
	    }

	    // encode Problem only if Reject
	    if (compType == Reject) {
		value = params.getParam(compParam + "." + s_tcapProblemCode);
		if (!TelEngine::null(value)) {
		    u_int16_t code = SS7TCAPError::codeFromError(tcap()->tcapType(),value->toInteger());
		    DataBlock db = ASNLib::encodeInteger(code,false);
		    // should check that encoded length is 2
		    if (db.length() < 2) {
			code = 0;
			db.insert(DataBlock(&code,1));
		    }
		    db.insert(ASNLib::buildLength(db));
		    int tag = SS7TCAPANSI::ProblemCodeTag;
		    db.insert(DataBlock(&tag,1));
		    codedComp.insert(db);
		}
	    }

	    // encode  Error Code only if ReturnError
	    if (compType == ReturnError) {
		value = params.getParam(compParam + "." + s_tcapErrCodeType);
		if (!TelEngine::null(value)) {
		    int errCode = params.getIntValue(compParam + "." + s_tcapErrCode,0);
		    DataBlock db = ASNLib::encodeInteger(errCode,false);
		    db.insert(ASNLib::buildLength(db));

		    int tag = 0;
		    if (*value == "national")
			tag = SS7TCAPANSI::ErrorNationalTag;
		    else if (*value == "private")
			tag = SS7TCAPANSI::ErrorPrivateTag;
		    db.insert(DataBlock(&tag,1));
		    codedComp.insert(db);
		}
	    }

	    // encode Operation Code only if Invoke
	    if (compType == InvokeLast ||
		compType == InvokeNotLast) {
		value = params.getParam(compParam + "." + s_tcapOpCodeType);
		if (!TelEngine::null(value)) {
		    int opCode = params.getIntValue(compParam + "." + s_tcapOpCode,0);
		    DataBlock db = ASNLib::encodeInteger(opCode,false);
		    int tag = 0;
		    if (*value == "national") {
			tag = SS7TCAPANSI::OperationNationalTag;
			if (db.length() < 2) {
			    opCode = 0;
			    db.insert(DataBlock(&opCode,1));
			}
		    }
		    else if (*value == "private")
			tag = SS7TCAPANSI::OperationPrivateTag;
		    db.insert(ASNLib::buildLength(db));
		    db.insert(DataBlock(&tag,1));
		    codedComp.insert(db);
		}
	    }
	    NamedString* invID = params.getParam(compParam + "." + s_tcapLocalCID);
	    NamedString* corrID = params.getParam(compParam + "." + s_tcapRemoteCID);
	    DataBlock db;
	    u_int8_t val = 0;
	    switch (compType) {
		case InvokeLast:
		case InvokeNotLast:
		    if (!TelEngine::null(invID)) {
			val = invID->toInteger();
			db.append(&val,sizeof(u_int8_t));
			if (!TelEngine::null(corrID)) {
			    val = corrID->toInteger();
			    db.append(&val,sizeof(u_int8_t));
			}
		    }
		    else {
			if (!TelEngine::null(corrID)) {
			    val = corrID->toInteger();
			    db.append(&val,sizeof(u_int8_t));
			}
		    }
		    break;
		case ReturnResultLast:
		case ReturnError:
		case Reject:
		case ReturnResultNotLast:
		    val = corrID->toInteger();
		    db.append(&val,sizeof(u_int8_t));
		    break;
		default:
		    break;
	    }

	    db.insert(ASNLib::buildLength(db));
	    int tag = SS7TCAPANSI::ComponentsIDsTag;
	    db.insert(DataBlock(&tag,1));
	    codedComp.insert(db);
	    codedComp.insert(ASNLib::buildLength(codedComp));
	    codedComp.insert(DataBlock(&compType,1));

	    params.clearParam(compParam,'.'); // clear all params for this component
	    compData.insert(codedComp);
	}
    }

    compData.insert(ASNLib::buildLength(compData));
    int tag = SS7TCAPANSI::ComponentPortionTag;
    compData.insert(DataBlock(&tag,1));

    data.insert(compData);
    params.clearParam(s_tcapCompPrefix,'.');
}

SS7TCAPError SS7TCAPTransactionANSI::handleDialogPortion(NamedList& params, bool byUser)
{
    XDebug(tcap(),DebugAll,"SS7TCAPTransactionANSI::handleDialogPortion() [%p]",this);

    SS7TCAPError err(SS7TCAP::ANSITCAP);

    NamedList dialog("");
    Lock l(this);
    switch (m_type) {
	case SS7TCAP::TC_Begin:
	case SS7TCAP::TC_QueryWithPerm:
	case SS7TCAP::TC_QueryWithoutPerm:
	case SS7TCAP::TC_Unidirectional:
	    if (!byUser) {
		int protoVersion = params.getIntValue(s_tcapProtoVers);
		if (protoVersion) { // there is a Dialog portion
		    if ((protoVersion & s_tcapProtoVersion ) != s_tcapProtoVersion)
			params.setParam(s_tcapProtoVers,String(s_tcapProtoVersion));
		}
	    }
	    else {
		dialog.copyParams(params,s_tcapDialogPrefix,'.');
		if (dialog.count())
		    params.setParam(s_tcapProtoVers,String(s_tcapProtoVersion));
	    }
	    return err;
	case SS7TCAP::TC_End:
	case SS7TCAP::TC_Response:
	    dialog.copyParams(params,s_tcapDialogPrefix,'.');
	    if (dialog.count() && m_prevType != SS7TCAP::TC_Begin && m_prevType != SS7TCAP::TC_QueryWithPerm) {
		err.setError(SS7TCAPError::Dialog_InconsistentDialoguePortion);
		return err;
	    }
	    break;
	case SS7TCAP::TC_Continue:
	case SS7TCAP::TC_ConversationWithPerm:
	case SS7TCAP::TC_ConversationWithoutPerm:
	    dialog.copyParams(params,s_tcapDialogPrefix,'.');
	    if (dialog.count() && m_prevType != SS7TCAP::TC_Begin && m_prevType != SS7TCAP::TC_QueryWithPerm
		    && m_prevType != SS7TCAP::TC_QueryWithoutPerm) {
		err.setError(SS7TCAPError::Dialog_InconsistentDialoguePortion);
		return err;
	    }
	    break;
	case SS7TCAP::TC_Notice:
	case SS7TCAP::TC_P_Abort:
	    break;
	case SS7TCAP::TC_U_Abort:
	    break;
	default:
	    break;
    }

    return err;
}

void SS7TCAPTransactionANSI::updateState(bool byUser)
{
    switch (m_type) {
	case SS7TCAP::TC_Begin:
	case SS7TCAP::TC_QueryWithPerm:
	case SS7TCAP::TC_QueryWithoutPerm:
	case SS7TCAP::TC_Continue:
	case SS7TCAP::TC_ConversationWithPerm:
	case SS7TCAP::TC_ConversationWithoutPerm:
	    (byUser ? setState(SS7TCAPTransaction::PackageSent) : setState(SS7TCAPTransaction::PackageReceived));
	    break;
	case SS7TCAP::TC_End:
	case SS7TCAP::TC_U_Abort:
	case SS7TCAP::TC_P_Abort:
	case SS7TCAP::TC_Response:
	case SS7TCAP::TC_Unidirectional:
	    setState(Idle);
	    break;
	case SS7TCAP::TC_Notice:
	case SS7TCAP::TC_Unknown:
	default:
	    break;
    }
}

/**
 * ITU-T SS7 TCAP implementation
 */

static const int s_ituTCAPProto = 1;
static const String s_tcapDialogueID = "tcap.dialogPDU.dialog-as-id";
static const String s_tcapDialogueAppCtxt = "tcap.dialogPDU.application-context-name";
static const String s_tcapDialoguePduType = "tcap.dialogPDU.dialog-pdu-type";
static const String s_tcapDialogueAbrtSrc = "tcap.dialogPDU.abort-source";
static const String s_tcapDialogueResult = "tcap.dialogPDU.result";
static const String s_tcapDialogueDiag = "tcap.dialogPDU.result-source-diagnostic";
static const String s_unstructDialogueOID = "0.0.17.773.1.2.1";
static const String s_structDialogueOID = "0.0.17.773.1.1.1";

static const PrimitiveMapping s_componentsITUMap[] = {
    {SS7TCAP::TC_Invoke,              SS7TCAPTransactionITU::Invoke},
    {SS7TCAP::TC_ResultLast,          SS7TCAPTransactionITU::ReturnResultLast},
    {SS7TCAP::TC_U_Error,             SS7TCAPTransactionITU::ReturnError},
    {SS7TCAP::TC_U_Reject,            SS7TCAPTransactionITU::Reject},
    {SS7TCAP::TC_R_Reject,            SS7TCAPTransactionITU::Reject},
    {SS7TCAP::TC_L_Reject,            SS7TCAPTransactionITU::Reject},
    {SS7TCAP::TC_InvokeNotLast,       SS7TCAPTransactionITU::Invoke},
    {SS7TCAP::TC_ResultNotLast,       SS7TCAPTransactionITU::ReturnResultNotLast},
    {SS7TCAP::TC_L_Cancel,            SS7TCAPTransactionITU::Local},
    {SS7TCAP::TC_U_Cancel,            SS7TCAPTransactionITU::Local},
    {SS7TCAP::TC_TimerReset,          SS7TCAPTransactionITU::Local},
    {SS7TCAP::TC_Unknown,             SS7TCAPTransactionITU::Unknown},
};

static const PrimitiveMapping s_transITUMap[] = {
    {SS7TCAP::TC_Unidirectional,              SS7TCAPTransactionITU::Unidirectional},
    {SS7TCAP::TC_Begin,                       SS7TCAPTransactionITU::Begin},
    {SS7TCAP::TC_QueryWithPerm,               SS7TCAPTransactionITU::Begin},
    {SS7TCAP::TC_QueryWithoutPerm,            SS7TCAPTransactionITU::Begin},
    {SS7TCAP::TC_Continue,                    SS7TCAPTransactionITU::Continue},
    {SS7TCAP::TC_ConversationWithPerm,        SS7TCAPTransactionITU::Continue},
    {SS7TCAP::TC_ConversationWithoutPerm,     SS7TCAPTransactionITU::Continue},
    {SS7TCAP::TC_End,                         SS7TCAPTransactionITU::End},
    {SS7TCAP::TC_Response,                    SS7TCAPTransactionITU::End},
    {SS7TCAP::TC_U_Abort,                     SS7TCAPTransactionITU::Abort},
    {SS7TCAP::TC_P_Abort,                     SS7TCAPTransactionITU::Abort},
    {SS7TCAP::TC_Notice,                      SS7TCAPTransactionITU::Unknown},
    {SS7TCAP::TC_Unknown,                     SS7TCAPTransactionITU::Unknown},
};

static const PrimitiveMapping* mapCompPrimitivesITU(int primitive, int comp = -1)
{
    const PrimitiveMapping* map = s_componentsITUMap;
    for (; map->primitive != SS7TCAP::TC_Unknown; map++) {
	if (primitive != -1) {
	    if (map->primitive == primitive )
		break;
	}
	else if (comp != -1)
	    if (map->mappedTo == comp)
		break;
    }
    return map;
}

static const PrimitiveMapping* mapTransPrimitivesITU(int primitive, int trans = -1)
{
    const PrimitiveMapping* map = s_transITUMap;
    for (; map->primitive != SS7TCAP::TC_Unknown; map++) {
	if (primitive != -1) {
	    if (map->primitive == primitive )
		break;
	}
	else if (trans != -1)
	    if (map->mappedTo == trans)
		break;
    }
    return map;
}

SS7TCAPITU::SS7TCAPITU(const NamedList& params)
    : SignallingComponent(params.safe("SS7TCAPITU"),&params,"ss7-tcap-itu"),
      SS7TCAP(params)
{
    String tmp;
    params.dump(tmp,"\r\n  ",'\'',true);
    DDebug(this,DebugAll,"SS7TCAPITU::SS7TCAPITU(%s)",tmp.c_str());
    setTCAPType(SS7TCAP::ITUTCAP);
}

SS7TCAPITU::~SS7TCAPITU()
{
    DDebug(this,DebugAll,"SS7TCAPITU::~SS7TCAPITU() [%p] destroyed with %d transactions, refCount=%d",
	this,m_transactions.count(),refcount());
}

SS7TCAPTransaction* SS7TCAPITU::buildTransaction(SS7TCAP::TCAPUserTransActions type, const String& transactID, NamedList& params,
	bool initLocal)
{
    return new SS7TCAPTransactionITU(this,type,transactID,params,m_trTimeout,initLocal);
}

SS7TCAPError SS7TCAPITU::decodeTransactionPart(NamedList& params, DataBlock& data)
{
    SS7TCAPError error(SS7TCAP::ITUTCAP);
    if (data.length() < 2)
	return error;

    // decode message type
    u_int8_t msgType = data[0];
    data.cut(-1);

    const PrimitiveMapping* map = mapTransPrimitivesITU(-1,msgType);
    if (map) {
	String type = lookup(map->primitive,SS7TCAP::s_transPrimitives,"Unknown");
	params.setParam(s_tcapRequest,type);
    }

    // decode message length
    int len = ASNLib::decodeLength(data);
    if (len != (int)data.length()) {
	error.setError(SS7TCAPError::Transact_BadlyStructuredTransaction);
	return error;
    }

    // decode transaction ids
    bool decodeOTID = false;
    bool decodeDTID = false;
    switch (map->mappedTo) {
	case SS7TCAPTransactionITU::Unidirectional:
	    return error;
	case SS7TCAPTransactionITU::Begin:
	    decodeOTID = true;
	    break;
	case SS7TCAPTransactionITU::End:
	case SS7TCAPTransactionITU::Abort:
	    decodeDTID = true;
	    break;
	case SS7TCAPTransactionITU::Continue:
	    decodeOTID = true;
	    decodeDTID = true;
	    break;
	default:
	    error.setError(SS7TCAPError::Transact_UnrecognizedPackageType);
	    return error;
    }

    u_int8_t tag = data[0];
    String str;
    if (decodeOTID) {
	// check for originating ID
	if (tag != OriginatingIDTag) {// 0x48
	    error.setError(SS7TCAPError::Transact_IncorrectTransactionPortion);
	    return error;
	}
	data.cut(-1);

	len = ASNLib::decodeLength(data);
	if (len < 1 || len > 4 || len > (int)data.length()) {
	    error.setError(SS7TCAPError::Transact_BadlyStructuredTransaction);
	    return error;
	}
	str.hexify(data.data(),len,' ');
	data.cut(-len);
	params.setParam(s_tcapRemoteTID,str);
    }

    tag = data[0];
    if (decodeDTID) {
	// check for originating ID
	if (tag != DestinationIDTag) {// 0x49
	    error.setError(SS7TCAPError::Transact_IncorrectTransactionPortion);
	    return error;
	}
	data.cut(-1);

	len = ASNLib::decodeLength(data);
	if (len < 1 || len > 4 || len > (int)data.length()) {
	    error.setError(SS7TCAPError::Transact_BadlyStructuredTransaction);
	    return error;
	}
	str.hexify(data.data(),len,' ');
	data.cut(-len);
	params.setParam(s_tcapLocalTID,str);
    }

#ifdef DEBUG
    if (s_printMsgs && s_extendedDbg && debugAt(DebugAll))
	dumpData(DebugAll,this,"SS7TCAPITU::decodeTransactionPart() finished",this,params,data);
#endif

    error.setError(SS7TCAPError::NoError);
    return error;
}

void SS7TCAPITU::encodeTransactionPart(NamedList& params, DataBlock& data)
{
    String msg = params.getValue(s_tcapRequest);
    const PrimitiveMapping* map = mapTransPrimitivesITU(msg.toInteger(SS7TCAP::s_transPrimitives),-1);
    if (!map)
	return;

    u_int8_t msgType = map->mappedTo;
    NamedString* val = 0;
    u_int8_t tag;
    bool encDTID = false;
    bool encOTID = false;

    switch (msgType) {
	case SS7TCAPTransactionITU::Unidirectional:
	    break;
	case SS7TCAPTransactionITU::Begin:
	    encOTID = true;
	    break;
	case SS7TCAPTransactionITU::End:
	case SS7TCAPTransactionITU::Abort:
	    encDTID = true;
	    break;
	case SS7TCAPTransactionITU::Continue:
	    encOTID = true;
	    encDTID = true;
	    break;
	default:
	    break;
    }

    if (encDTID) {
	val = params.getParam(s_tcapRemoteTID);
	if (!TelEngine::null(val)) {
	    // destination TID
	    DataBlock db;
	    db.unHexify(val->c_str(),val->length(),' ');
	    db.insert(ASNLib::buildLength(db));
	    tag = DestinationIDTag;
	    db.insert(DataBlock(&tag,1));
	    data.insert(db);
	}
    }
    if (encOTID) {
	val = params.getParam(s_tcapLocalTID);
	if (!TelEngine::null(val)) {
	    // origination id
	    DataBlock db;
	    db.unHexify(val->c_str(),val->length(),' ');
	    db.insert(ASNLib::buildLength(db));
	    tag = OriginatingIDTag;
	    db.insert(DataBlock(&tag,1));
	    data.insert(db);
	}
    }

    data.insert(ASNLib::buildLength(data));
    data.insert(DataBlock(&msgType,1));
}

/**
 * ITU-T SS7 TCAP transaction implementation
 */
const TokenDict SS7TCAPTransactionITU::s_dialogPDUs[] = {
    {"AARQ",    SS7TCAPTransactionITU::AARQDialogTag},
    {"AARE",    SS7TCAPTransactionITU::AAREDialogTag},
    {"ABRT",    SS7TCAPTransactionITU::ABRTDialogTag},
    {0,0},
};

const TokenDict SS7TCAPTransactionITU::s_resultPDUValues[] = {
    {"accepted",                                       SS7TCAPTransactionITU::ResultAccepted},
    {"reject-permanent",                               SS7TCAPTransactionITU::ResultRejected},
    {"user-null",                                      SS7TCAPTransactionITU::DiagnosticUserNull},
    {"user-no-reason-given",                           SS7TCAPTransactionITU::DiagnosticUserNoReason},
    {"user-application-context-name-not-supported",    SS7TCAPTransactionITU::DiagnosticUserAppCtxtNotSupported},
    {"provider-null",                                  SS7TCAPTransactionITU::DiagnosticProviderNull},
    {"provider-no-reason-given",                       SS7TCAPTransactionITU::DiagnosticProviderNoReason},
    {"provider-no-common-dialogue-portion",            SS7TCAPTransactionITU::DiagnosticProviderNoCommonDialog},
    {"dialogue-service-user",                          SS7TCAPTransactionITU::AbortSourceUser},
    {"dialogue-service-provider",                      SS7TCAPTransactionITU::AbortSourceProvider},
    {0,-1},
};

SS7TCAPTransactionITU::SS7TCAPTransactionITU(SS7TCAP* tcap, SS7TCAP::TCAPUserTransActions type,
	const String& transactID, NamedList& params, u_int64_t timeout, bool initLocal)
    : SS7TCAPTransaction(tcap,type,transactID,params,timeout,initLocal)
{
    DDebug(m_tcap,DebugAll,"SS7TCAPTransactionITU(tcap = '%s' [%p], transactID = %s, timeout=" FMT64 " ) created [%p]",
	    m_tcap->toString().c_str(),tcap,transactID.c_str(),timeout,this);
}


SS7TCAPTransactionITU::~SS7TCAPTransactionITU()
{
    DDebug(tcap(),DebugAll,"Transaction with ID=%s of user=%s destroyed [%p]",
	   m_localID.c_str(),m_userName.c_str(),this);
}

SS7TCAPError SS7TCAPTransactionITU::handleData(NamedList& params, DataBlock& data)
{
    DDebug(tcap(),DebugAll,"SS7TCAPTransactionITU::handleData() transactionID=%s data length=%u [%p]",m_localID.c_str(),
	   data.length(),this);
    Lock lock(this);
    // in case of Abort message, check Cause Information
    SS7TCAPError error(SS7TCAP::ITUTCAP);

    if (m_type == SS7TCAP::TC_P_Abort) {
	error = decodePAbort(this,params,data);
	if (error.error() != SS7TCAPError::NoError)
	    return error;
    }
    else if (testForDialog(data)) {
	// decode DialogPortion
	error = decodeDialogPortion(params,data);
	if (error.error() != SS7TCAPError::NoError)
	    return error;
    }
    error = handleDialogPortion(params,false);
    if (error.error() != SS7TCAPError::NoError)
	return error;

    // decodeComponents
    error = decodeComponents(params,data);
     if (error.error() != SS7TCAPError::NoError)
	buildComponentError(error,params,data);

    error = handleComponents(params,false);
    return error;
}

bool SS7TCAPTransactionITU::testForDialog(DataBlock& data)
{
    return (data.length() && data[0] == SS7TCAPITU::DialogPortionTag);
}

void SS7TCAPTransactionITU::updateState(bool byUser)
{
    switch (m_type) {
	case SS7TCAP::TC_Begin:
	case SS7TCAP::TC_QueryWithPerm:
	case SS7TCAP::TC_QueryWithoutPerm:
	    break;
	case SS7TCAP::TC_Continue:
	case SS7TCAP::TC_ConversationWithPerm:
	case SS7TCAP::TC_ConversationWithoutPerm:
	    setState(Active);
	    break;
	case SS7TCAP::TC_End:
	case SS7TCAP::TC_U_Abort:
	case SS7TCAP::TC_P_Abort:
	case SS7TCAP::TC_Response:
	case SS7TCAP::TC_Unidirectional:
	    setState(Idle);
	    break;
	case SS7TCAP::TC_Notice:
	case SS7TCAP::TC_Unknown:
	default:
	    break;
    }
}

SS7TCAPError SS7TCAPTransactionITU::update(SS7TCAP::TCAPUserTransActions type, NamedList& params, bool updateByUser)
{
   DDebug(tcap(),DebugAll,"SS7TCAPTransactionITU::update() [%p], localID=%s - update to type=%s by %s",this,m_localID.c_str(),
	lookup(type,SS7TCAP::s_transPrimitives,"Unknown"), (updateByUser ? "user" : "remote"));
#ifdef DEBUG
    if (s_printMsgs && s_extendedDbg && debugAt(DebugAll))
	dumpData(DebugAll,tcap(),"SS7TCAPTransactionITU::update() with",this,params);
#endif

    Lock l(this);
    SS7TCAPError error(SS7TCAP::ITUTCAP);
    switch (type) {
	case SS7TCAP::TC_Begin:
	case SS7TCAP::TC_QueryWithPerm:
	case SS7TCAP::TC_QueryWithoutPerm:
	case SS7TCAP::TC_Unidirectional:
	    Debug(tcap(),DebugInfo,"SS7TCAPTransactionITU::update() [%p], localID=%s - invalid update: trying to update from type=%s to type=%s",
		    this,m_localID.c_str(),lookup(m_type,SS7TCAP::s_transPrimitives,"Unknown"),
		    lookup(type,SS7TCAP::s_transPrimitives,"Unknown"));
	    params.setParam(s_tcapRequestError,"invalid_update");
	    params.setParam("tcap.request.error.currentState",lookup(m_type,SS7TCAP::s_transPrimitives,"Unknown"));
	    error.setError(SS7TCAPError::Transact_IncorrectTransactionPortion);
	    return error;

	case SS7TCAP::TC_End:
	case SS7TCAP::TC_Response:
	    m_type = type;
	    if (m_state == PackageReceived) {
		m_basicEnd = params.getBoolValue(s_tcapBasicTerm,m_basicEnd);
		if (!m_basicEnd)
		    // prearranged end, no need to transmit to remote end
		    m_transmit = NoTransmit;
		else
		    m_transmit = PendingTransmit;
	    }
	    else if (m_state == PackageSent) {
		if(!updateByUser)
		    m_transmit = PendingTransmit;
		else
		    m_transmit = NoTransmit;
	    }
	    else if (m_state == Active) {
		if(!updateByUser)
		    m_transmit = PendingTransmit;
		else {
		    m_basicEnd = params.getBoolValue(s_tcapBasicTerm,m_basicEnd);
		    if (!m_basicEnd)
			// prearranged end, no need to transmit to remote end
			m_transmit = NoTransmit;
		    else
			m_transmit = PendingTransmit;
		}
	    }
	    break;
	case SS7TCAP::TC_Continue:
	case SS7TCAP::TC_ConversationWithPerm:
	case SS7TCAP::TC_ConversationWithoutPerm:
	    if (m_state == PackageSent)
		m_remoteID = params.getValue(s_tcapRemoteTID);
	    m_type = type;
	    m_transmit = PendingTransmit;
	    break;
	case SS7TCAP::TC_Notice:
	    m_type = type;
	    if (updateByUser) {
		Debug(tcap(),DebugInfo,"SS7TCAPTransactionITU::update() [%p], localID=%s - invalid update: trying to update from type=%s to type=%s",
		    this,m_localID.c_str(),lookup(m_type,SS7TCAP::s_transPrimitives,"Unknown"),
		    lookup(type,SS7TCAP::s_transPrimitives,"Unknown"));
		params.setParam(s_tcapRequestError,"invalid_update");
		params.setParam("tcap.request.error.currentState",lookup(m_type,SS7TCAP::s_transPrimitives,"Unknown"));
		error.setError(SS7TCAPError::Transact_IncorrectTransactionPortion);
		return error;
	    }
	    break;
	case SS7TCAP::TC_P_Abort:
	case SS7TCAP::TC_U_Abort:
	    m_type = type;
	    if (m_state == PackageReceived)
		m_transmit = PendingTransmit;
	    else if (m_state == PackageSent) {
		if (!updateByUser) {
		    if (String("pAbort") == params.getValue(s_tcapAbortCause))
			m_type = SS7TCAP::TC_P_Abort;
		    else
			m_type = SS7TCAP::TC_P_Abort;
		    m_transmit = PendingTransmit;
		}
		else
		    m_transmit = NoTransmit;
	    }
	    else if (m_state == Active) {
		if (!updateByUser) {
		    if (String("pAbort") == params.getValue(s_tcapAbortCause))
			m_type = SS7TCAP::TC_P_Abort;
		    else
			m_type = SS7TCAP::TC_P_Abort;
		}
		m_transmit = PendingTransmit;
	    }
	    break;
	default:
	    break;
    }

    populateSCCPAddress(m_localSCCPAddr,m_remoteSCCPAddr,params,updateByUser);
    m_basicEnd = params.getBoolValue(s_tcapBasicTerm,true);
    m_endNow = params.getBoolValue(s_tcapEndNow,m_endNow);

    if (m_timeout.started()) {
	m_timeout.stop();
	XDebug(tcap(),DebugInfo,"SS7TCAPTransactionITU::update() [%p], localID=%s - timeout timer has been stopped",this,m_localID.c_str());
    }
    return error;
}

SS7TCAPError SS7TCAPTransactionITU::handleDialogPortion(NamedList& params, bool byUser)
{
    DDebug(tcap(),DebugAll,"SS7TCAPTransactionITU::handleDialogPortion() [%p]",this);

    SS7TCAPError error(SS7TCAP::ITUTCAP);

    NamedString* diagPDU = params.getParam(s_tcapDialoguePduType);
    NamedString* appCtxt = params.getParam(s_tcapDialogueAppCtxt);
    int protoVers = params.getIntValue(s_tcapProtoVers,s_ituTCAPProto,0);

    Lock l(this);
    switch (m_type) {
	case SS7TCAP::TC_Unidirectional:
	    if (byUser) {
		// check for context name, if not present no AUDT
		if (TelEngine::null(appCtxt))
		    return error;
		m_appCtxt = *appCtxt;
		// build AUDT.
		params.setParam(s_tcapDialogueID,s_unstructDialogueOID.toString());
		if (protoVers)
		    params.setParam(s_tcapProtoVers,String(protoVers));
		params.setParam(s_tcapDialoguePduType,lookup(AARQDialogTag,s_dialogPDUs));
	    }
	    else {
		// check to be AUDT
		if (TelEngine::null(diagPDU) || !protoVers)
		    return error;
		if (diagPDU->toInteger(s_dialogPDUs) != AARQDialogTag || s_ituTCAPProto != protoVers)
		    error.setError(SS7TCAPError::Dialog_Abnormal);
	    }
	    break;
	case SS7TCAP::TC_Begin:
	case SS7TCAP::TC_QueryWithPerm:
	case SS7TCAP::TC_QueryWithoutPerm:
	    if (byUser) {
		if (TelEngine::null(appCtxt))
		    break;
		m_appCtxt = *appCtxt;
		// build AARQ
		params.setParam(s_tcapDialogueID,s_structDialogueOID);
		if (protoVers)
		    params.setParam(s_tcapProtoVers,String(protoVers));
		params.setParam(s_tcapDialoguePduType,lookup(AARQDialogTag,s_dialogPDUs));
	    }
	    else {
		if (TelEngine::null(diagPDU) || !protoVers)
		    break;
		// check to be AARQ and that it has context
		if (diagPDU->toInteger(s_dialogPDUs) != AARQDialogTag || TelEngine::null(appCtxt)) {
		    error.setError(SS7TCAPError::Dialog_Abnormal);
		    break;
		}
		// check proto version, if not 1, build AARE - no common dialogue version, return err to build abort
		if (s_ituTCAPProto != protoVers) {
		    params.clearParam(s_tcapDialogPrefix,'.');
		    params.setParam(s_tcapDialogueID,s_structDialogueOID);
		    params.setParam(s_tcapDialoguePduType,lookup(AAREDialogTag,s_dialogPDUs));
		    params.setParam(s_tcapDialogueResult,lookup(ResultRejected,s_resultPDUValues));
		    params.setParam(s_tcapDialogueDiag,lookup(DiagnosticProviderNoCommonDialog,s_resultPDUValues));
		    error.setError(SS7TCAPError::Dialog_Abnormal);
		    break;
		}
		m_appCtxt = *appCtxt;
	    }
	    break;
	case SS7TCAP::TC_End:
	case SS7TCAP::TC_Response:
	    if (byUser) {
		if (!basicEnd() || transactionState() != PackageReceived || m_appCtxt.null()) {
		    params.clearParam(s_tcapDialogPrefix,'.');
		    break;
		}
		if (TelEngine::null(appCtxt))
		    params.setParam(s_tcapDialogueAppCtxt,m_appCtxt);
		// build AARE with result=accepted, result-source-diagnostic=null / dialog-service-user(null)
		params.setParam(s_tcapDialogueID,s_structDialogueOID);
		if (protoVers)
		    params.setParam(s_tcapProtoVers,String(protoVers));
		params.setParam(s_tcapDialoguePduType,lookup(AAREDialogTag,s_dialogPDUs));
		params.setParam(s_tcapDialogueResult,lookup(ResultAccepted,s_resultPDUValues));
		if (!params.getParam(s_tcapDialogueDiag))
		    params.addParam(s_tcapDialogueDiag,lookup(DiagnosticUserNoReason,s_resultPDUValues));
	    }
	    else {
		// page 51 q.774
		// dialog info ?
		// => yes => AC MODE ? = no, discard components. TC-p-abort to TCU , terminate transaction
		//			= yes, check correct AARE, incorrect => TC-P-Abort to user, send TC_END to user otherwise
		// => no =? AC MODE ? = no, send TC_END to user (continue processing)
		// 			= yes, TC-p-abort to TCU , terminate transaction
		if (transactionState() != PackageSent && !TelEngine::null(diagPDU)) {
		    error.setError(SS7TCAPError::Dialog_Abnormal);
		    break;
		}
		if (!TelEngine::null(appCtxt)) {
		    if (m_appCtxt.null())
			error.setError(SS7TCAPError::Dialog_Abnormal);
		    else {
			if (TelEngine::null(diagPDU) || diagPDU->toInteger(s_dialogPDUs) != AAREDialogTag)
			    error.setError(SS7TCAPError::Dialog_Abnormal);
		    }
		}
		else {
		    if (!m_appCtxt.null() && transactionState() != Active)
			error.setError(SS7TCAPError::Dialog_Abnormal);
		}
	    }
	    break;
	case SS7TCAP::TC_Continue:
	case SS7TCAP::TC_ConversationWithPerm:
	case SS7TCAP::TC_ConversationWithoutPerm:
	    if (byUser) {
		if (transactionState() != PackageReceived || TelEngine::null(appCtxt)) {
		    params.clearParam(s_tcapDialogPrefix,'.');
		    break;
		}
		// build AARE
		m_appCtxt = *appCtxt;
		params.setParam(s_tcapDialogueID,s_structDialogueOID);
		if (protoVers)
		    params.setParam(s_tcapProtoVers,String(protoVers));
		params.setParam(s_tcapDialoguePduType,lookup(AAREDialogTag,s_dialogPDUs));
		params.setParam(s_tcapDialogueResult,lookup(ResultAccepted,s_resultPDUValues));
		if (!params.getParam(s_tcapDialogueDiag))
		    params.addParam(s_tcapDialogueDiag,lookup(DiagnosticUserNoReason,s_resultPDUValues));
	    }
	    else {
		// dialog info?
		// yes => AC MODE? => yes, Check AARE
		//		   = > no, discard, build P Abort with ABRT apdu
		// no => AC MODE? => no, send to user / continue processing
		//		  => yes, build U_Abort with ABRT apdu
		if (transactionState() == PackageReceived)
		    break;
		if (!TelEngine::null(appCtxt)) {
		    if (m_appCtxt.null())
			error.setError(SS7TCAPError::Dialog_Abnormal);
		    else {
			if (TelEngine::null(diagPDU) || diagPDU->toInteger(s_dialogPDUs) != AAREDialogTag)
			    error.setError(SS7TCAPError::Dialog_Abnormal);
		    }
		}
		else {
		    if (!m_appCtxt.null() && transactionState() == PackageSent)
			error.setError(SS7TCAPError::Dialog_Abnormal);
		}
	    }
	    break;
	case SS7TCAP::TC_Notice:
	case SS7TCAP::TC_P_Abort:
	    break;
	case SS7TCAP::TC_U_Abort:
	    if (byUser) {
		if (m_appCtxt.null())
		    break;
		params.setParam(s_tcapDialogueID,s_structDialogueOID);
		if (protoVers)
		    params.setParam(s_tcapProtoVers,String(protoVers));
		if (transactionState() == PackageReceived ) {
		    NamedString* abrtReason = params.getParam(s_tcapDialogueDiag);
		    if (!TelEngine::null(abrtReason) && (abrtReason->toInteger(s_resultPDUValues) == DiagnosticUserAppCtxtNotSupported ||
			abrtReason->toInteger(s_resultPDUValues) == DiagnosticProviderNoCommonDialog)) {
			// build AARE
			if (TelEngine::null(appCtxt))
			    params.setParam(s_tcapDialogueAppCtxt,m_appCtxt);
			params.setParam(s_tcapDialoguePduType,lookup(AAREDialogTag,s_dialogPDUs));
			params.setParam(s_tcapDialogueResult,lookup(ResultRejected,s_resultPDUValues));
		    }
		    else {
			// build ABRT
			params.setParam(s_tcapDialoguePduType,lookup(ABRTDialogTag,s_dialogPDUs));
			params.setParam(s_tcapDialogueAbrtSrc,lookup(AbortSourceUser,s_resultPDUValues));
		    }
		}
		else if (transactionState() == Active) {
		    if (TelEngine::null(params.getParam(s_tcapDialogueAbrtSrc)))
			params.setParam(s_tcapDialogueAbrtSrc,lookup(AbortSourceUser,s_resultPDUValues));
		    params.setParam(s_tcapDialoguePduType,lookup(ABRTDialogTag,s_dialogPDUs));
		}
	    }
	    else {
		// state initsent/active
		if (!TelEngine::null(m_appCtxt)) {
		    NamedString* diagID = params.getParam(s_tcapDialogueID);
		    NamedString* pdu = params.getParam(s_tcapDialoguePduType);
		    if (!TelEngine::null(diagID) && !TelEngine::null(pdu)) {
			if (s_structDialogueOID == *diagID && (pdu->toInteger(s_dialogPDUs) == AAREDialogTag
			    || pdu->toInteger(s_dialogPDUs) == ABRTDialogTag)) {

			    if (pdu->toInteger() == AAREDialogTag) {
				NamedString* diag = params.getParam(s_tcapDialogueDiag);
				if (transactionState() == PackageSent && !TelEngine::null(diag) &&
				    diag->toInteger(s_resultPDUValues) != DiagnosticProviderNoCommonDialog) {
				    params.setParam(s_tcapRequest,lookup(SS7TCAP::TC_P_Abort,SS7TCAP::s_transPrimitives));
				    params.setParam(s_tcapAbortCause,"pAbort");
				    m_transmit = PendingTransmit;
				}
				else
				    error.setError(SS7TCAPError::Dialog_Abnormal);
			    }
			    else {
				NamedString* src = params.getParam(s_tcapDialogueAbrtSrc);
				if (!TelEngine::null(src) && src->toInteger(s_resultPDUValues) != AbortSourceUser)
				    error.setError(SS7TCAPError::Dialog_Abnormal);
			    }
			}
			else
			    error.setError(SS7TCAPError::Dialog_Abnormal);
		    }
		    else
			error.setError(SS7TCAPError::Dialog_Abnormal);
		}
		else {
		    if (!TelEngine::null(appCtxt))
			error.setError(SS7TCAPError::Dialog_Abnormal);
		}
	    }
	    break;
	default:
	    break;
    }

#ifdef DEBUG
    if (tcap() && s_printMsgs && s_extendedDbg && debugAt(DebugAll))
	dumpData(DebugAll,tcap(),"SS7TCAPTransactionITU::handleDialogPortion()",this,params,DataBlock::empty());
#endif
    return error;
}

void SS7TCAPTransactionITU::abnormalDialogInfo(NamedList& params)
{
    params.setParam(s_tcapRequest,lookup(SS7TCAP::TC_U_Abort,SS7TCAP::s_transPrimitives));
    params.setParam(s_tcapAbortCause,"uAbort");
    params.setParam(s_tcapDialogueID,s_structDialogueOID);
    params.setParam(s_tcapDialoguePduType,
	lookup(SS7TCAPTransactionITU::ABRTDialogTag,SS7TCAPTransactionITU::s_dialogPDUs));
    params.setParam(s_tcapDialogueAbrtSrc,
	lookup(SS7TCAPTransactionITU::AbortSourceProvider,SS7TCAPTransactionITU::s_resultPDUValues));
}

void SS7TCAPTransactionITU::encodePAbort(SS7TCAPTransaction* tr, NamedList& params, DataBlock& data)
{
    NamedString* pAbortCause = params.getParam(s_tcapAbortCause);
    DataBlock db;
    if (!TelEngine::null(pAbortCause)) {
	int tag = 0;
	if (*pAbortCause == "pAbort") {
	    tag = SS7TCAPITU::PCauseTag;
	    u_int8_t pCode = SS7TCAPError::codeFromError(SS7TCAP::ITUTCAP,params.getIntValue(s_tcapAbortInfo));
	    if (pCode) {
		db.append(ASNLib::encodeInteger(pCode,false));
		db.insert(ASNLib::buildLength(db));
		db.insert(DataBlock(&tag,1));
	    }
	}
	else if (*pAbortCause == "uAbort") {
	    if (tr)
		tr->encodeDialogPortion(params,data);
	}
    }
    if (db.length())
	data.insert(db);

#ifdef DEBUG
     if (tr && tr->tcap() && s_printMsgs && s_extendedDbg && debugAt(DebugAll))
	 dumpData(DebugAll,tr->tcap(),"SS7TCAPTransactionITU::encodePAbort() - encoded Abort info",tr,params,data);
#endif
}

SS7TCAPError SS7TCAPTransactionITU::decodePAbort(SS7TCAPTransaction* tr, NamedList& params, DataBlock& data)
{
    u_int8_t tag = data[0];
    SS7TCAPError error(SS7TCAP::ITUTCAP);
    if (!tr)
	return error;
    SS7TCAPTransactionITU* tri = static_cast<SS7TCAPTransactionITU*>(tr);
    if (!tri)
	return error;
    if (tag == SS7TCAPITU::PCauseTag) {
	data.cut(-1);
	u_int8_t pCode = 0;
	int len = ASNLib::decodeUINT8(data,&pCode,false);
	if (len != 1) {
	    error.setError(SS7TCAPError::Transact_BadlyStructuredTransaction);
	    return error;
	}
	params.setParam(s_tcapAbortCause,"pAbort");
	params.setParam(s_tcapAbortInfo,String(SS7TCAPError::errorFromCode(SS7TCAP::ITUTCAP,pCode)));
    }
    else if (tri->testForDialog(data))  {
	error = tri->decodeDialogPortion(params,data);
	if (error.error() != SS7TCAPError::NoError)
	    return error;
	params.setParam(s_tcapAbortCause,"uAbort");
    }
#ifdef DEBUG
     if (tr && tr->tcap() && s_printMsgs && s_extendedDbg && debugAt(DebugAll))
	 dumpData(DebugAll,tr->tcap(),"SS7TCAPTransactionITU::decodePAbort() - decoded Abort info",tr,params,data);
#endif
    return error;
}

void SS7TCAPTransactionITU::updateToEnd()
{
    setTransactionType(SS7TCAP::TC_End);
    if (transactionState() == PackageSent)
	m_basicEnd = false;
}

SS7TCAPError SS7TCAPTransactionITU::decodeDialogPortion(NamedList& params, DataBlock& data)
{
    DDebug(tcap(),DebugAll,"SS7TCAPTransactionITU::decodeDialogPortion() for transaction with localID=%s [%p]",
    m_localID.c_str(),this);

    SS7TCAPError error(SS7TCAP::ITUTCAP);

    u_int8_t tag = data[0];
    // dialog is not present
    if (tag != SS7TCAPITU::DialogPortionTag) // 0x6b
	return error;
    data.cut(-1);

    // dialog portion is present, decode dialog length
    int len = ASNLib::decodeLength(data);
    if (len < 0 || len > (int)data.length()) {
	error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
	return error;
    }

    tag = data[0];
    if (tag != SS7TCAPITU::ExternalTag) {// 0x28
	error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
	return error;
    }
    data.cut(-1);

    len = ASNLib::decodeLength(data);
    if (len < 0 || len > (int)data.length()) {
	error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
	return error;
    }

    // decode dialog-as-id
    ASNObjId oid;
    len = ASNLib::decodeOID(data,&oid,true);
    if (len < 0) {
	error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
	return error;
    }
    params.setParam(s_tcapDialogueID,oid.toString());

    // remove Encoding Tag
    tag = data[0];
    if (tag != SS7TCAPITU::SingleASNTypeCEncTag) {// 0xa0
	error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
	return error;
    }
    data.cut(-1);

    len = ASNLib::decodeLength(data);
    if (len < 0 || len > (int)data.length()) {
	error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
	return error;
    }

    int dialogPDU = data[0]; // should be DialoguePDU type tag
    if (dialogPDU != AARQDialogTag && dialogPDU != AAREDialogTag && dialogPDU != ABRTDialogTag)  {// 0x60 0x61 0x64
	error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
	return error;
    }
    data.cut(-1);
    params.setParam(s_tcapDialoguePduType,lookup(dialogPDU,s_dialogPDUs));

    len = ASNLib::decodeLength(data);
    if (len < 0 || len > (int)data.length()) {
	error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
	return error;
    }

    // check for protocol version or abort-source
    if (data[0] == SS7TCAPITU::ProtocolVersionTag) { //0x80 bitstring
	data.cut(-1);
	if (dialogPDU != ABRTDialogTag) {
	    // decode protocol version
	    String proto;
	    len = ASNLib::decodeBitString(data,&proto,false);
	    if (len != 1) {
		error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
		return error;
	    }
	    params.setParam(s_tcapProtoVers,proto);
	}
	else {
	    u_int8_t abrtSrc = 0xff;
	    len = ASNLib::decodeUINT8(data,&abrtSrc,false);
	    int code = 0x30 | abrtSrc;
	    params.setParam(s_tcapDialogueAbrtSrc,lookup(code,s_resultPDUValues));
	}
    }

    // check for Application Context Tag  length OID tag length
    if (data[0] == SS7TCAPITU::ApplicationContextTag) { // 0xa1
	data.cut(-1);
	len = ASNLib::decodeLength(data);
	if (len < 0 || len > (int)data.length()) {
	    error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
	    return error;
	}
	ASNObjId oid;
	len = ASNLib::decodeOID(data,&oid,true);
	if (len < 0) {
	    error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
	    return error;
	}
	params.setParam(s_tcapDialogueAppCtxt,oid.toString());
    }

    if (data[0] == ResultTag) {
	data.cut(-1);
	len = ASNLib::decodeLength(data);
	if (len < 0 || len > (int)data.length()) {
	    error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
	    return error;
	}
	u_int8_t res = 0xff;
	len = ASNLib::decodeUINT8(data,&res,true);
	params.setParam(s_tcapDialogueResult,lookup(res,s_resultPDUValues));
    }

    if (data[0] == ResultDiagnosticTag) {
	data.cut(-1);
	len = ASNLib::decodeLength(data);
	if (data[0] == ResultDiagnosticUserTag || data[0]== ResultDiagnosticProviderTag) {
	    tag = data[0];
	    data.cut(-1);
	    len = ASNLib::decodeLength(data);
	    if (len < 0 || len > (int)data.length()) {
		error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
		return error;
	    }
	    u_int8_t res = 0xff;
	    len = ASNLib::decodeUINT8(data,&res,true);
	    if (tag == ResultDiagnosticUserTag) {
		int code = 0x10 | res;
		params.setParam(s_tcapDialogueDiag,lookup(code,s_resultPDUValues));
	    }
	    else {
		int code = 0x20 | res;
		params.setParam(s_tcapDialogueDiag,lookup(code,s_resultPDUValues));
	    }
	}
    }
    // check for user information
    if (data[0] == SS7TCAPITU::UserInformationTag) {// 0xfd
	data.cut(-1);
	len = ASNLib::decodeLength(data);
	if (len < 0) {
	    error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
	    return error;
	}

	tag = data[0];
	if (tag != SS7TCAPITU::ExternalTag) {
	    error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
	    return error;
	}
	data.cut(-1);

	len = ASNLib::decodeLength(data);
	if (len < 0 || len > (int)data.length()) {
	    error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
	    return error;
	}

	// direct Reference
	tag = data[0];
	if (tag == SS7TCAPITU::DirectReferenceTag) { // 0x06
	    data.cut(-1);
	    ASNObjId oid;
	    len = ASNLib::decodeOID(data,&oid,false);
	    if (len < 0) {
		error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
		return error;
	    }
	    params.setParam(s_tcapReference,oid.toString());
	}

	// data Descriptor
	tag = data[0];
	if (tag == SS7TCAPITU::DataDescriptorTag) { // 0x07
	    data.cut(-1);
	    String str;
	    int type;
	    len = ASNLib::decodeString(data,&str,&type,false);
	    if (len < 0) {
		error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
		return error;
	    }
	    params.setParam(s_tcapDataDesc,str);
	}

	// encoding
	tag = data[0];
	if (tag == SS7TCAPITU::SingleASNTypePEncTag || tag == SS7TCAPITU::SingleASNTypeCEncTag ||
	    tag == SS7TCAPITU::OctetAlignEncTag || tag == SS7TCAPITU::ArbitraryEncTag) {
	    data.cut(-1);
	    len = ASNLib::decodeLength(data);
	    if (len < 0) {
		error.setError(SS7TCAPError::Dialog_BadlyStructuredDialoguePortion);
		return error;
	    }
	    DataBlock d((void*)data.data(0,len),len);
	    data.cut(-len);

	    // put encoding context in hexified form
	    String dataHexified;
	    dataHexified.hexify(d.data(),d.length(),' ');
	    params.setParam(s_tcapEncodingContent,dataHexified);
	    // put encoding identifier
	    switch (tag) {
		case SS7TCAPITU::SingleASNTypePEncTag: // 0x80
		    params.setParam(s_tcapEncodingType,"single-ASN1-type-primitive");
		    break;
		case SS7TCAPITU::SingleASNTypeCEncTag: // 0xa0
		    params.setParam(s_tcapEncodingType,"single-ASN1-type-contructor");
		    break;
		case SS7TCAPITU::OctetAlignEncTag:     // 0x81
		    params.setParam(s_tcapEncodingType,"octet-aligned");
		    break;
		case SS7TCAPITU::ArbitraryEncTag:      // 0x82
		    params.setParam(s_tcapEncodingType,"arbitrary");
		    break;
		default:
		    break;
	    }
	}
    }
#ifdef DEBUG
     if (s_printMsgs && s_extendedDbg && debugAt(DebugAll))
	 dumpData(DebugAll,tcap(),"SS7TCAPTransactionITU::decodeDialogPortion() - decoded dialog portion",this,params,data);
#endif
    return error;
}

void SS7TCAPTransactionITU::encodeDialogPortion(NamedList& params, DataBlock& data)
{
    DDebug(tcap(),DebugAll,"SS7TCAPTransactionITU::encodeDialogPortion() for transaction with localID=%s [%p]",
		m_localID.c_str(),this);

    DataBlock dialogData;
    int tag;

    NamedString* typeStr = params.getParam(s_tcapDialoguePduType);
    if (TelEngine::null(typeStr))
	return;
    u_int8_t pduType = typeStr->toInteger(s_dialogPDUs);

    // encode user information
    DataBlock userInfo;
    NamedString* val = params.getParam(s_tcapEncodingType);
    if (!TelEngine::null(val)) {
	if (*val == "single-ASN1-type-primitive")
	    tag = SS7TCAPITU::SingleASNTypePEncTag;
	else if (*val == "single-ASN1-type-contructor")
	    tag = SS7TCAPITU::SingleASNTypeCEncTag;
	else if (*val == "octet-aligned")
	    tag = SS7TCAPITU::OctetAlignEncTag;
	else if (*val == "arbitrary")
	    tag = SS7TCAPITU::ArbitraryEncTag;

	val = params.getParam(s_tcapEncodingContent);
	if (val) {
	    DataBlock db;
	    db.unHexify(val->c_str(),val->length(),' ');
	    db.insert(ASNLib::buildLength(db));
	    db.insert(DataBlock(&tag,1));
	    userInfo.insert(db);
	}
    }
    val = params.getParam(s_tcapDataDesc);
    if (!TelEngine::null(val)) {
	DataBlock db = ASNLib::encodeString(*val,ASNLib::PRINTABLE_STR,false);
	db.insert(ASNLib::buildLength(db));
	tag = SS7TCAPITU::DataDescriptorTag;
	db.insert(DataBlock(&tag,1));
	userInfo.insert(db);
    }
    val = params.getParam(s_tcapReference);
    if (!TelEngine::null(val)) {
	ASNObjId oid = *val;
	DataBlock db = ASNLib::encodeOID(oid,false);
	db.insert(ASNLib::buildLength(db));
	tag = SS7TCAPITU::DirectReferenceTag;
	db.insert(DataBlock(&tag,1));
	userInfo.insert(db);
    }

    if (userInfo.length()) {
	userInfo.insert(ASNLib::buildLength(userInfo));
	tag = SS7TCAPITU::ExternalTag;
	userInfo.insert(DataBlock(&tag,1));
	userInfo.insert(ASNLib::buildLength(userInfo));
	tag = SS7TCAPITU::UserInformationTag;
	userInfo.insert(DataBlock(&tag,1));
	dialogData.insert(userInfo);
    }

    switch (pduType) {
	case AAREDialogTag:
	    val = params.getParam(s_tcapDialogueDiag);
	    if (!TelEngine::null(val)) {
		u_int16_t code = val->toInteger(s_resultPDUValues);
		DataBlock db = ASNLib::encodeInteger(code % 0x10,true);
		db.insert(ASNLib::buildLength(db));
		if ((code & 0x10) == 0x10)
		    tag = ResultDiagnosticUserTag;
		else
		    tag = ResultDiagnosticProviderTag;
		db.insert(DataBlock(&tag,1));
		db.insert(ASNLib::buildLength(db));
		tag = ResultDiagnosticTag;
		db.insert(DataBlock(&tag,1));
		dialogData.insert(db);
	    }

	    val = params.getParam(s_tcapDialogueResult);
	    if (!TelEngine::null(val)) {
		u_int8_t res = val->toInteger(s_resultPDUValues);
		DataBlock db = ASNLib::encodeInteger(res,true);
		db.insert(ASNLib::buildLength(db));
		tag = ResultTag;
		db.insert(DataBlock(&tag,1));
		dialogData.insert(db);
	    }
	case AARQDialogTag:
	    // Application context
	    val = params.getParam(s_tcapDialogueAppCtxt);
	    if (!TelEngine::null(val)) {
		ASNObjId oid = *val;
		DataBlock db = ASNLib::encodeOID(oid,true);
		db.insert(ASNLib::buildLength(db));
		tag = SS7TCAPITU::ApplicationContextTag;
		db.insert(DataBlock(&tag,1));
		dialogData.insert(db);
	    }
	    val = params.getParam(s_tcapProtoVers);
	    if (!TelEngine::null(val) && (val->toInteger() > 0)) {
		DataBlock db = ASNLib::encodeBitString(*val,false);
		db.insert(ASNLib::buildLength(db));
		tag = SS7TCAPITU::ProtocolVersionTag;
		db.insert(DataBlock(&tag,1));
		dialogData.insert(db);
	    }
	    break;
	case ABRTDialogTag:
	    val = params.getParam(s_tcapDialogueAbrtSrc);
	    if (!TelEngine::null(val)) {
		u_int8_t code = val->toInteger(s_resultPDUValues) % 0x30;
		DataBlock db = ASNLib::encodeInteger(code,false);
		db.insert(ASNLib::buildLength(db));
		tag = SS7TCAPITU::ProtocolVersionTag;
		db.insert(DataBlock(&tag,1));
		dialogData.insert(db);
	    }
	    break;
	default:
	    return;
    }

    dialogData.insert(ASNLib::buildLength(dialogData));
    dialogData.insert(DataBlock(&pduType,1));
    dialogData.insert(ASNLib::buildLength(dialogData));
    tag = SS7TCAPITU::SingleASNTypeCEncTag;
    dialogData.insert(DataBlock(&tag,1));

    val = params.getParam(s_tcapDialogueID);
    if (TelEngine::null(val))
	return;

    ASNObjId oid = *val;
    dialogData.insert(ASNLib::encodeOID(oid,true));
    dialogData.insert(ASNLib::buildLength(dialogData));
    tag = SS7TCAPITU::ExternalTag;
    dialogData.insert(DataBlock(&tag,1));
    dialogData.insert(ASNLib::buildLength(dialogData));
    tag = SS7TCAPITU::DialogPortionTag;
    dialogData.insert(DataBlock(&tag,1));

    data.insert(dialogData);
    params.clearParam(s_tcapDialogPrefix,'.');
#ifdef DEBUG
     if (s_printMsgs && s_extendedDbg && debugAt(DebugAll))
	 dumpData(DebugAll,tcap(),"SS7TCAPTransactionITU::encodeDialogPortion() - encoded dialog portion",this,params,data);
#endif
}

SS7TCAPError SS7TCAPTransactionITU::decodeComponents(NamedList& params, DataBlock& data)
{
    XDebug(tcap(),DebugAll,"SS7TCAPTransactionITU::decodeComponents() [%p] - data length=%u",this,data.length());

    SS7TCAPError error(SS7TCAP::ITUTCAP);
    if (!data.length()) {
	params.setParam(s_tcapCompCount,"0");
	return error;
    }

    u_int8_t tag = data[0];
    if (tag != SS7TCAPITU::ComponentPortionTag) { // 0x6c
	error.setError(SS7TCAPError::General_IncorrectComponentPortion);
	return error;
    }
    data.cut(-1);

    // decode length of component portion
    int len = ASNLib::decodeLength(data);
    bool checkEoC = (len == ASNLib::IndefiniteForm);
    if (!checkEoC && (len < 0 || len != (int)data.length())) { // the length of the remaining data should be the same as the decoded length
	error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
	return error;
    }

    unsigned int compCount = 0;
    while (data.length()) {
	if (checkEoC && ASNLib::matchEOC(data) > 0)
	    break;
	compCount++;
	// decode component type
	u_int8_t compType = data[0];
	data.cut(-1);

	// verify component length
	len = ASNLib::decodeLength(data);
	if (len < 0 || len > (int)data.length()) {
	    error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
	    break;
	}
	unsigned int initLength = data.length();
	unsigned int compLength = len;

	// decode invoke id
	u_int16_t compID;
	tag = data[0];
	bool notDeriv = false;
	if (tag != SS7TCAPITU::LocalTag) {// 0x02
	    if (compType == Reject) {
		ASNLib::decodeNull(data,true);
		notDeriv = true;
	    }
	    else {
		error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
		break;
	    }
	} else {
	    data.cut(-1);

	    // obtain component ID(s)
	    len = ASNLib::decodeUINT16(data,&compID,false);
	    if (len < 0) {
		error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
		break;
	    }
	}
	String compParam;
	compPrefix(compParam,compCount,false);
	// comp IDs shall be decoded according to component type
	switch (compType) {
	    case Invoke:
		params.setParam(compParam + "." + s_tcapRemoteCID,String(compID));
		if (data[0] == SS7TCAPITU::LinkedIDTag) {
		    data.cut(-1);
		    u_int16_t linkID;
		    len = ASNLib::decodeUINT16(data,&linkID,false);
		    if (len < 0) {
			error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
			break;
		    }
		    params.setParam(compParam + "." + s_tcapLocalCID,String(compID));
		}
		break;
	    case ReturnResultLast:
	    case ReturnError:
	    case Reject:
	    case ReturnResultNotLast:
		if (notDeriv)
		    params.setParam(compParam + "." + s_tcapLocalCID,"");
		else
		    params.setParam(compParam + "." + s_tcapLocalCID,String(compID));
		break;
	    default:
		error.setError(SS7TCAPError::General_UnrecognizedComponentType);
		break;
	}

	const PrimitiveMapping* map = mapCompPrimitivesITU(-1,compType);
	if (!map) {
	    error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
	    break;
	}
	params.setParam(compParam + "." +  s_tcapCompType,lookup(map->primitive,SS7TCAP::s_compPrimitives,"Unknown"));

	if (error.error() != SS7TCAPError::NoError) {
	    break;
	}

	// decode Operation Code
	if (compType == Invoke ||
	    compType == ReturnResultLast ||
	    compType == ReturnResultNotLast) {
	    tag = data[0];
	    if (tag == SS7TCAPITU::ParameterSeqTag) {
		data.cut(-1);
		len = ASNLib::decodeLength(data);
	    }
	    tag = data[0];
	    if (tag == SS7TCAPITU::LocalTag) {
		data.cut(-1);
		int opCode = 0;
		len = ASNLib::decodeINT32(data,&opCode,false);
		params.setParam(compParam +"." + s_tcapOpCodeType,"local");
		params.setParam(compParam + "." + s_tcapOpCode,String(opCode));
	    }
	    else if (tag == SS7TCAPITU::GlobalTag) {
		data.cut(-1);
		ASNObjId obj;
		len = ASNLib::decodeOID(data,&obj,false);
		params.setParam(compParam + "." + s_tcapOpCodeType,"global");
		params.setParam(compParam + "." + s_tcapOpCode,obj.toString());
	    }
	    else if (compType == Invoke) {
		error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
		break;
	    }
	}

	// decode  Error Code
	if (compType == ReturnError) {
	    tag = data[0];
	    if (tag == SS7TCAPITU::LocalTag) {
		data.cut(-1);
		int opCode = 0;
		len = ASNLib::decodeINT32(data,&opCode,false);
		params.setParam(compParam + "." + s_tcapErrCodeType,"local");
		params.setParam(compParam + "." + s_tcapErrCode,String(opCode));
	    }
	    else if (tag == SS7TCAPITU::GlobalTag) {
		data.cut(-1);
		ASNObjId obj;
		len = ASNLib::decodeOID(data,&obj,false);
		params.setParam(compParam + "." + s_tcapErrCodeType,"global");
		params.setParam(compParam + "." + s_tcapErrCode,obj.toString());
	    }
	    else {
		error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
		break;
	    }
	}

	// decode Problem
	if (compType == Reject) {
	    tag = data[0];
	    data.cut(-1);
	    u_int16_t problemCode = 0x0 | (tag << 8);
	    u_int8_t code = 0;
	    len = ASNLib::decodeUINT8(data,&code,false);
	    problemCode |= code;
	    params.setParam(compParam + "." + s_tcapProblemCode,String(SS7TCAPError::errorFromCode(tcap()->tcapType(),problemCode)));
	}
	else {
	// decode Parameters (Set or Sequence) as payload
	    int payloadLen = data.length() - (initLength - compLength);
	    DataBlock d((void*)data.data(0,payloadLen),payloadLen);
	    data.cut(-payloadLen);
	    String dataHexified = "";
	    dataHexified.hexify(d.data(),d.length(),' ');
	    params.setParam(compParam,dataHexified);
	}
	if (initLength - data.length() != compLength) { // check we consumed the announced component length
	    error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
	    break;
	}
    }

    params.setParam(s_tcapCompCount,String(compCount));
#ifdef DEBUG
    if (tcap() && s_printMsgs && s_extendedDbg && debugAt(DebugAll))
	dumpData(DebugAll,tcap(),"Finished decoding message",this,params,data);
#endif
    return error;
}

void SS7TCAPTransactionITU::encodeComponents(NamedList& params, DataBlock& data)
{
    XDebug(tcap(),DebugAll,"SS7TCAPTransactionITU::encodeComponents() for transaction with localID=%s [%p]",m_localID.c_str(),this);

    int componentCount = params.getIntValue(s_tcapCompCount,0);
    DataBlock compData;
    if (componentCount) {
	int index = componentCount + 1;

	while (--index) {
	    DataBlock codedComp;
	    // encode parameters
	    String compParam;
	    compPrefix(compParam,index,false);
	    // Component Type
	    int compPrimitive = lookup(params.getValue(compParam + "." + s_tcapCompType,"Unknown"),SS7TCAP::s_compPrimitives);
	    const PrimitiveMapping* map = mapCompPrimitivesITU(compPrimitive,-1);
	    if (!map)
		continue;
	    int compType = map->mappedTo;

	    NamedString* value = 0;
	    bool hasPayload = false;
	    if (compType == Reject) {
		value = params.getParam(compParam + "." + s_tcapProblemCode);
		if (!TelEngine::null(value)) {
		    u_int16_t codeErr = SS7TCAPError::codeFromError(tcap()->tcapType(),(SS7TCAPError::ErrorType)value->toInteger());
		    u_int8_t problemTag = (codeErr & 0xff00) >> 8;
		    u_int8_t code = codeErr & 0x000f;
		    DataBlock db(DataBlock(&code,1));
		    db.insert(ASNLib::buildLength(db));
		    db.insert(DataBlock(&problemTag,1));
		    codedComp.insert(db);
		}
		else {
		    Debug(tcap(),DebugWarn,"Missing mandatory 'problemCode' information for component with index='%d' from transaction "
			    "with localID=%s [%p]",index,m_localID.c_str(),this);
		    continue;
		}
	    }
	    else {
		NamedString* payloadHex = params.getParam(compParam);
		if (!TelEngine::null(payloadHex)) {
		    DataBlock payload;
		    payload.unHexify(payloadHex->c_str(),payloadHex->length(),' ');
		    codedComp.insert(payload);
		    hasPayload = true;
		}
	    }
	    // encode  Error Code only if ReturnError
	    if (compType == ReturnError) {
		value = params.getParam(compParam + "." + s_tcapErrCodeType);
		if (!TelEngine::null(value)) {
		    int tag = 0;
		    DataBlock db;
		    if (*value == "local") {
			tag = SS7TCAPITU::LocalTag;
			int errCode = params.getIntValue(compParam + "." + s_tcapErrCode,0);
			db = ASNLib::encodeInteger(errCode,false);
			db.insert(ASNLib::buildLength(db));
		    }
		    else if (*value == "global") {
			tag = SS7TCAPITU::GlobalTag;
			ASNObjId oid = String(params.getValue(compParam + "." + s_tcapErrCode));
			db = ASNLib::encodeOID(oid,false);
			db.insert(ASNLib::buildLength(db));
		    }
		    db.insert(DataBlock(&tag,1));
		    codedComp.insert(db);
		}
		else {
		    Debug(tcap(),DebugWarn,"Missing mandatory 'errorCodeType' information for component with index='%d' from transaction "
			    "with localID=%s [%p]",index,m_localID.c_str(),this);
		    continue;
		}
	    }

	    // encode Operation Code only if Invoke
	    if (compType == Invoke ||
		compType == ReturnResultNotLast ||
		compType == ReturnResultLast) {
		value = params.getParam(compParam + "." + s_tcapOpCodeType);
		if (!TelEngine::null(value)) {
		    int tag = 0;
		    DataBlock db;
		    if (*value == "local") {
			int opCode = params.getIntValue(compParam + "." + s_tcapOpCode,0);
			db = ASNLib::encodeInteger(opCode,true);
		    }
		    else if (*value == "global") {
			ASNObjId oid(params.getValue(compParam + "." + s_tcapOpCode));
			db = ASNLib::encodeOID(oid,true);
			//db.insert(ASNLib::buildLength(db));
		    }
		    codedComp.insert(db);
		    if (compType != Invoke) {
			tag = SS7TCAPITU::ParameterSeqTag;
			codedComp.insert(ASNLib::buildLength(codedComp));
			codedComp.insert(DataBlock(&tag,1));
		    }
		}
		else {
		    if (compType == Invoke || hasPayload) {
			Debug(tcap(),DebugWarn,"Missing mandatory 'operationCodeType' information for component with index='%d' from transaction "
			    "with localID=%s [%p]",index,m_localID.c_str(),this);
			continue;
		    }
		}
	    }

	    NamedString* invID = params.getParam(compParam + "." + s_tcapLocalCID);
	    NamedString* linkID = params.getParam(compParam + "." + s_tcapRemoteCID);
	    DataBlock db;
	    u_int8_t val = 0;
	    switch (compType) {
		case Invoke:
		    if (!TelEngine::null(linkID)) {
			val = linkID->toInteger();
			DataBlock db1;
			db1.append(&val,sizeof(u_int8_t));
			db1.insert(ASNLib::buildLength(db1));
			val = SS7TCAPITU::LinkedIDTag;
			db1.insert(DataBlock(&val,1));
			codedComp.insert(db1);
		    }
		    if (!TelEngine::null(invID)) {
			val = invID->toInteger();
			db.append(&val,sizeof(u_int8_t));
			db.insert(ASNLib::buildLength(db));
			val = SS7TCAPITU::LocalTag;
			db.insert(DataBlock(&val,1));
		    }
		    else {
			Debug(tcap(),DebugWarn,"Missing mandatory 'localCID' information for component with index='%d' from transaction "
			    "with localID=%s [%p]",index,m_localID.c_str(),this);
			continue;
		    }
		    break;
		case ReturnResultLast:
		case ReturnError:
		case ReturnResultNotLast:
		    if (!TelEngine::null(linkID)) {
			val = linkID->toInteger();
			db.append(&val,sizeof(u_int8_t));
			db.insert(ASNLib::buildLength(db));
			val = SS7TCAPITU::LocalTag;
			db.insert(DataBlock(&val,1));
		    }
		    else {
			Debug(tcap(),DebugWarn,"Missing mandatory 'remoteCID' information for component with index='%d' from transaction "
			    "with localID=%s [%p]",index,m_localID.c_str(),this);
			continue;
		    }
		    break;
		case Reject:
		    if (TelEngine::null(linkID))
			linkID = invID;
		    if (!TelEngine::null(linkID)) {
			val = linkID->toInteger();
			db.append(&val,sizeof(u_int8_t));
			db.insert(ASNLib::buildLength(db));
			val = SS7TCAPITU::LocalTag;
			db.insert(DataBlock(&val,1));
		    }
		    else
			db.insert(ASNLib::encodeNull(true));
		    break;
		default:
		    break;
	    }
	    codedComp.insert(db);

	    if(codedComp.length()) {
		codedComp.insert(ASNLib::buildLength(codedComp));
		codedComp.insert(DataBlock(&compType,1));
	    }

	    params.clearParam(compParam,'.'); // clear all params for this component
	    compData.insert(codedComp);
	}

	if (compData.length()) {
	    compData.insert(ASNLib::buildLength(compData));
	    int tag = SS7TCAPITU::ComponentPortionTag;
	    compData.insert(DataBlock(&tag,1));

	    data.insert(compData);
	}
    }

    params.clearParam(s_tcapCompPrefix,'.');
}

void SS7TCAPTransactionITU::requestContent(NamedList& params, DataBlock &data)
{
#ifdef DEBUG
    DDebug(tcap(),DebugAll,"SS7TCAPTransactionITU::requestContent() - for id=%s [%p]",m_localID.c_str(),this);
#endif
    if (m_type == SS7TCAP::TC_P_Abort || m_type == SS7TCAP::TC_U_Abort)
	encodePAbort(this,params,data);
    else {
	requestComponents(params,data);
	if (dialogPresent()) {
	    if (TelEngine::null(params.getParam(s_tcapDialoguePduType)))
		handleDialogPortion(params,true);
	    encodeDialogPortion(params,data);
	}
    }
    transactionData(params);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
