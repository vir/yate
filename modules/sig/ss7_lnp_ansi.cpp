/**
 * ss7_lnp_ansi.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Query LNP databases using Telcordia GR-533-Core specification
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2011-2014 Null Team
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
#include <yatesig.h>
#include <yateasn.h>
#include <stdlib.h>

using namespace TelEngine;

namespace {

class LNPClient;
class LNPQuery;
class BlockedCode;

class SS7LNPDriver : public Module
{
public:
    enum LNPCounter {
	Announcement = 1,
	DBOverload,
	OSSControls,
	PortedQueries,
	TimedOutQueries,
	ErrorredQueries,
	SendFailure,
	TotalQueries,
    };
    enum Cmds {
	CmdList = 1,
	CmdTest,
    };
    SS7LNPDriver();
    ~SS7LNPDriver();
    virtual void initialize();
    virtual bool msgRoute(Message& msg);
    virtual void msgTimer(Message& msg);
    void incCounter(LNPCounter counter);
    void resetCounters(bool globalToo = false);
    static const TokenDict s_cmds[];
protected:
    virtual bool received(Message& msg, int id);
    virtual bool commandComplete(Message& msg, const String& partLine, const String& partWord);
    virtual bool commandExecute(String& retVal, const String& line);
    virtual void statusModule(String& str);
    virtual void statusParams(String& str);
    virtual void statusDetail(String& str);
    bool parseParams(const String& line, NamedList& parsed, String& error);
    unsigned int m_overallCounts[TotalQueries];
    unsigned int m_currentCounts[TotalQueries];
    u_int64_t m_countReset;
    LNPClient* m_lnp;
};

class LNPQuery : public GenObject
{
public:
    enum QueryStatus {
	Pending,
	TimedOut,
	ReportedError,
	ResponseRejected,
	PortingDone,
	Announcement,
	UnderControl,
    };
    LNPQuery(LNPClient* lnp, u_int8_t id, const String& called, Message* params);
    ~LNPQuery();
    void endQuery(SS7TCAP::TCAPUserCompActions primitive, int opCode, const NamedList& params);
    const String& toString() const
	{ return m_id; }
    inline bool timedOut()
	{ return m_timeout < Time::msecNow(); }
    inline QueryStatus status()
	{ return m_status; }
    inline NamedList* parameters()
	{ return m_msg; }
    inline SS7TCAP::TCAPUserCompActions primitive()
	{ return m_primitive; }
    inline void setPrimitive(SS7TCAP::TCAPUserCompActions prim)
	{ m_primitive = prim; }
    inline String& problemData()
	{ return m_problemData; }
    inline void setProblemData(String& hexData)
	{ m_problemData = hexData; }
    inline SS7TCAPError problem()
	{ return m_error; }
    inline unsigned int dbSSN()
	{ return m_dbSSN; }
    inline int dbPointCode()
	{ return m_dbPC; }
    inline void setDialogID(const char* id)
	{ m_dialogID = id; }
    inline String& dialogID()
	{ return m_dialogID; }
    inline String& calledNumber()
	{ return m_called; }
    inline void extractAddress(NamedList& params);
private:
    String m_id;
    u_int64_t m_timeout;
    Message* m_msg;
    QueryStatus m_status;
    SS7TCAP::TCAPUserCompActions m_primitive;
    String m_problemData;
    SS7TCAPError m_error;
    unsigned int m_dbSSN;
    int m_dbPC;
    String m_dialogID;
    String m_called;
    LNPClient* m_lnp;
};

class LNPClient : public TCAPUser {
public:
    enum Operation {
	None                        = 0x0,
	ProvideInstructionsStart    = 0x0301,
	ConnectionControlConnect    = 0x0401,
	CallerInteractionPlay       = 0x0501,
	SendNotificationTermination = 0x0601,
	NetworkManagementACG        = 0x0701,
	ProceduralError             = 0x0803,
    };
    enum PrivateLNP {
	BillingIndicators        = 0x41,
	ConnectTime              = 0x42,
	EchoData                 = 0x43,
	OrigStationType          = 0x45,
	TerminationIndicators    = 0x46,
	ACGIndicator             = 0x47,
	CICExpansion             = 0x48,
	DigitsPrivate            = 0x49,
    };
    enum LNPParams {
	ServiceKey              = 0xaa,
	StandardAnnouncement    = 0x82,
	Digits                  = 0x84,
	ProblemData             = 0x86,
	PrivateErrorCode        = 0xd4,
	PrivateParam            = 0xdf,
    };
    // Digit encoding as defined by ANSI ATIS-1000114.2004  T1.114.5 Figure 8
    enum DigitsNature {
	NatureNational = 0x00,
	NatureInternational = 0x01,
	PresentationRestriction = 0x02,
    };
    enum DigitsType {
	DigitsNotUsed             = 0x00,
	CalledPartyNumber         = 0x01,
	CallingPartyNumber        = 0x02,
	CallerInteraction         = 0x03,
	RoutingNumber             = 0x04,
	BillingNumber             = 0x05,
	DestinationNumber         = 0x06,
	LATA                      = 0x07,
	Carrier                   = 0x08,
	LastCallingParty          = 0x09,
	LastCalledParty           = 0x0a,
	CallingDirectoryNumber    = 0x0b,
	VMSRIdentifier            = 0x0c,
	OriginalCalledNumber      = 0x0d,
	RedirectiongNumber        = 0x0e,
	ConnectedNumber           = 0x0f,
    };
    enum DigitsEncoding {
	EncodingNotUsed = 0x00,
	EncodingBCD     = 0x01,
	EncodingIA5     = 0x02,
    };
    enum DigitsNumberingPlan {
	NPNotUsed           = 0x00,
	NPISDN              = 0x10,
	NPTelephony         = 0x20,
	NPData              = 0x30,
	NPTelex             = 0x40,
	NPMaritimeMobile    = 0x50,
	NPLandMobile        = 0x60,
	NPPrivate           = 0x70,
    };
    enum ACGCause {
	ACGVacantCode          = 1,
	ACGOutOfBand           = 2,
	ACGDatabaseOverload    = 3,
	ACGMassCalling         = 4,
	ACGOSSInitiated        = 5,
    };
    enum Announcements {
	NotUsed = 0,
	OutOfBand = 1,
	VacandCode = 2,
	DisconnectedNumber = 3,
	ReorderBusy = 4,
	Busy = 5,
	NoCircuit = 6,
	Reorder = 7,
	Ringing = 8,
    };
    LNPClient();
    ~LNPClient();
    virtual bool tcapIndication(NamedList& params);
    virtual bool managementNotify(SCCP::Type type, NamedList& params);
    virtual bool makeQuery(const String& called, Message& msg);
    virtual bool tcapRequest(SS7TCAP::TCAPUserTransActions primitive, LNPQuery* query);
    virtual int  waitForQuery(LNPQuery* query);
    bool mandatoryParams(Operation opCode, NamedList& params);
    bool findTCAP();
    void checkBlocked();
    BlockedCode* findACG(const char* code);
    void statusBlocked(String& status);
    inline int managementState()
	{ return SCCPManagement::UserInService; }

protected:
    virtual void destroyed();
    virtual SS7TCAPError decodeParameters(NamedList&params, String& hexData);
    virtual void encodeParameters(Operation op, NamedList& params, String& hexData);
    virtual SS7TCAPError decodeDigits(NamedList& params, DataBlock& data, const char* prefix = 0);
    virtual void encodeDigits(DigitsType type, NamedList& params, DataBlock& data);
    SS7TCAPError decodePrivateParam(TelEngine::NamedList&, TelEngine::DataBlock&);
    void encodePrivateParam(PrivateLNP param, NamedList& params, DataBlock& data);
    void encodeBCD(String& digits, DataBlock& data);
    unsigned int decodeBCD(unsigned int length, String& digits, unsigned char* buff);
    ObjList m_queries;
    Mutex m_queriesMtx;
    u_int8_t m_compID;
    Mutex m_blockedMtx;
    ObjList m_blocked;
};

class BlockedCode : public GenObject
{
public:
    BlockedCode(const char* code, u_int64_t duration, u_int64_t gap, LNPClient::ACGCause cause);
    ~BlockedCode();
    void resetGapInterval();
    void update(u_int64_t duration, u_int64_t gap, LNPClient::ACGCause cause);
    inline bool durationExpired()
	{ return (m_duration <= 2048 && m_durationExpiry < Time::secNow()); }
    inline void setACGCause(LNPClient::ACGCause cause)
	{ m_cause = cause; }
    inline LNPClient::ACGCause acgCause()
	{ return m_cause; }
    const String& toString() const
	{ return m_code; }
    inline bool codeAllowed()
	{ return (m_gap == 0 ? false : m_gapExpiry < Time::secNow()); }
    inline unsigned int duration()
	{ return m_duration; }
    inline unsigned int gap()
	{ return m_gap; }
private:
    String m_code;
    unsigned int m_duration;
    u_int64_t m_durationExpiry;
    unsigned int m_gap;
    u_int64_t m_gapExpiry;
    LNPClient::ACGCause m_cause;
};

static Configuration s_cfg;

static SS7PointCode s_remotePC;
static SS7PointCode::Type s_remotePCType;

static bool s_copyBack = true;
static String s_lnpPrefix = "lnp";
static bool s_playAnnounce = true;


INIT_PLUGIN(SS7LNPDriver);

static const TokenDict s_counters[] = {
    {"Announcement",    SS7LNPDriver::Announcement},
    {"DBOverload",      SS7LNPDriver::DBOverload},
    {"UnderControl",    SS7LNPDriver::OSSControls},
    {"Ported",          SS7LNPDriver::PortedQueries},
    {"TimedOut",        SS7LNPDriver::TimedOutQueries},
    {"Errorred",        SS7LNPDriver::ErrorredQueries},
    {"SendFailure",	SS7LNPDriver::SendFailure},
    {"Total",           SS7LNPDriver::TotalQueries},
    {0,0},
};

const TokenDict SS7LNPDriver::s_cmds[] = {
    {"listblocked",  CmdList},
    {"test",         CmdTest},
    {0,0}
};

static const char* s_cmdsLine = "lnp {test [{called|caller|lata|origstation|cicexpansion}=value]| listblocked }";

static const TokenDict s_digitTypes[] = {
    {"DigitsNotUsed",             LNPClient::DigitsNotUsed},
    {"CalledPartyNumber",         LNPClient::CalledPartyNumber},
    {"CallingPartyNumber",        LNPClient::CallingPartyNumber},
    {"CallerInteraction",         LNPClient::CallerInteraction},
    {"RoutingNumber",             LNPClient::RoutingNumber},
    {"BillingNumber",             LNPClient::BillingNumber},
    {"DestinationNumber",         LNPClient::DestinationNumber},
    {"LATA",                      LNPClient::LATA},
    {"Carrier",                   LNPClient::Carrier},
    {"LastCallingParty",          LNPClient::LastCallingParty},
    {"LastCalledParty",           LNPClient::LastCalledParty},
    {"CallingDirectoryNumber",    LNPClient::CallingDirectoryNumber},
    {"VMSRIdentifier",            LNPClient::VMSRIdentifier},
    {"OriginalCalledNumber",      LNPClient::OriginalCalledNumber},
    {"RedirectionNumber",         LNPClient::RedirectiongNumber},
    {"ConnectedNumber",           LNPClient::ConnectedNumber},
};

static const TokenDict s_operations[] = {
    {"ProvideInstructions:Start",           LNPClient::ProvideInstructionsStart},
    {"ConnectionControl:Connect",           LNPClient::ConnectionControlConnect},
    {"CallerInteraction:PlayAnnouncement",  LNPClient::CallerInteractionPlay},
    {"SendNotification:Termination",        LNPClient::SendNotificationTermination},
    {"NetworkManagement:ACG",               LNPClient::NetworkManagementACG},
    {"Procedural:ReportError",              LNPClient::ProceduralError},
    {"None",                                LNPClient::None},
};

static const TokenDict s_nature[] = {
    {"national",         LNPClient::NatureNational},
    {"international",    LNPClient::NatureInternational},
};

static const TokenDict s_plans[] = {
    {"notused",        LNPClient::NPNotUsed},
    {"isdn",           LNPClient::NPISDN},
    {"telephony",      LNPClient::NPTelephony},
    {"data",           LNPClient::NPData},
    {"telex",          LNPClient::NPTelex},
    {"maritimemobile", LNPClient::NPMaritimeMobile},
    {"landmobile",     LNPClient::NPLandMobile},
    {"private",        LNPClient::NPPrivate},
};

static const TokenDict s_encodings[] = {
    {"notused",        LNPClient::EncodingNotUsed},
    {"bcd",           LNPClient::EncodingBCD},
    {"ia5",           LNPClient::EncodingIA5},
};

// ANSI Originating Line Info
static const TokenDict s_dict_oli[] = {
    { "normal",            0 },
    { "multiparty",        1 },
    { "ani-failure",       2 },
    { "hotel-room-id",     6 },
    { "coinless",          7 },
    { "restricted",        8 },
    { "test-call-1",      10 },
    { "aiod-listed-dn",   20 },
    { "identified-line",  23 },
    { "800-call",         24 },
    { "coin-line",        27 },
    { "restricted-hotel", 68 },
    { "test-call-2",      95 },
    { 0, 0 }
};

static const TokenDict s_announce[] = {
    {"outofband",       LNPClient::OutOfBand},
    {"vacantcode",      LNPClient::VacandCode},
    {"disconnected",    LNPClient::DisconnectedNumber},
    {"reorderbusy",     LNPClient::ReorderBusy},
    {"busy",            LNPClient::Busy},
    {"nocircuit",       LNPClient::NoCircuit},
    {"reorder",         LNPClient::Reorder},
    {"ring",            LNPClient::Ringing},
    {"",                LNPClient::NotUsed},
};

static const TokenDict s_acgCauses[] = {
    {"ACGVacantCode",          LNPClient::ACGVacantCode},
    {"ACGOutOfBand",           LNPClient::ACGOutOfBand},
    {"ACGDatabaseOverload",    LNPClient::ACGDatabaseOverload},
    {"ACGMassCalling",         LNPClient::ACGMassCalling},
    {"ACGOSSInitiated",        LNPClient::ACGOSSInitiated},
    {0,0},
};

static const TokenDict s_gaps[] = {
    {"3", 1},
    {"4", 2},
    {"6", 3},
    {"8", 4},
    {"11", 5},
    {"16", 6},
    {"22", 7},
    {"30", 8},
    {"42", 9},
    {"58", 10},
    {"81", 11},
    {"112", 12},
    {"156", 13},
    {"217", 14},
    {"300", 15},
    {"0", 0},
};

static const String s_remPC = "RemotePC";
static const String s_cpdSSN = "CalledPartyAddress.ssn";
static const String s_cpdGT = "CalledPartyAddress.gt";
static const String s_cpdTT = "CalledPartyAddress.gt.tt";
static const String s_cpdRoute = "CalledPartyAddress.route";
static const String s_checkAddr = "tcap.checkAddress";

static const String s_lnpCfg = "lnp";
static const String s_sccpCfg = "sccp_addr";
static const String s_sccpPrefix = "sccp.";
static const String s_tcapPrefix = "tcap";
static const String s_opCode = ".operationCode";
static const String s_localID = ".localCID";
static const String s_remoteID = ".remoteCID";
static const String s_compType = ".componentType";
static const String s_calledPN = ".CalledPartyNumber";
static const String s_callingPN = ".CallingPartyNumber";
static const String s_lata = ".LATA";
static const String s_cicExp = ".CICExpansion";
static const String s_stationType = ".OriginatingStationType";
static const String s_problemData = ".ProblemData";
static const String s_privateError = ".PrivateError";
static const String s_acg = ".ACG";
static const String s_acgDuration = ".ACG.Duration";
static const String s_acgGap = ".ACG.Gap";
static const String s_acgCause = ".ACG.ControlCause";
static const String s_billing = ".BilingIndicators";
static const String s_routingNumber = ".RoutingNumber";
static const String s_announcement = ".StandardAnnouncement";
static const String s_carrier = ".Carrier";
static const String s_billAMA = ".BillingIndicators.AMACallType";
static const String s_billFeature = ".BillingIndicators.ServiceFeature";
static const String s_echoData = ".EchoData";
static const String s_tcapTID = "tcap.transaction.localTID";
static const String s_compCount = "tcap.component.count";
static const String s_endNow = "tcap.transaction.endNow";
static const String s_tcapComp = "tcap.component.1";
static const String s_tcapCompType = "tcap.component.1.componentType";
static const String s_opCodeType = "tcap.component.1.operationCodeType";
static const String s_tcapOpCode = "tcap.component.1.operationCode";
static const String s_tcapProblem = "tcap.component.1.problemCode";
static const String s_tcapLocalCID = "tcap.component.1.localCID";
static const String s_compTimeout = "tcap.component.1.timeout";
static const String s_tcapReq = "tcap.request.type";
static const String s_tcapUser = "tcap.user";
static const String s_tcapBasicTerm = "tcap.transaction.terminationBasic";


static void copyLNPParams(NamedList* dest, NamedList* src, String paramsToCopy)
{
    if (!(dest && src))
	return;

    DDebug(&__plugin,DebugAll,"copyLNPParams(dest=%p,src=%p)",dest,src);
    String called = s_cfg.getValue(s_lnpCfg,"called","${called}");
    src->replaceParams(called);
    String caller = s_cfg.getValue(s_lnpCfg,"caller","${caller}");

    String lata = s_cfg.getValue(s_lnpCfg,"LATA","${lata}");
    String origStation =  s_cfg.getValue(s_lnpCfg,"station_type","${oli$normal}");
    String cicExpansion = s_cfg.getValue(s_lnpCfg,"cic_expansion","${cicexpansion$true}");

    src->replaceParams(caller);
    src->replaceParams(lata);
    src->replaceParams(origStation);
    src->replaceParams(cicExpansion);

    dest->setParam(s_lnpPrefix + s_calledPN,called);
    if (!TelEngine::isE164(caller))
	caller = "";
    dest->setParam(s_lnpPrefix + s_callingPN,caller);
    dest->setParam(s_lnpPrefix + s_lata,lata);
    dest->setParam(s_lnpPrefix + s_cicExp,(cicExpansion ? "1" : "0"));
    dest->setParam(s_lnpPrefix + s_stationType,origStation);

    if (src->getBoolValue("copyparams",false)) {
	dest->copySubParams(*src,s_sccpPrefix);
	dest->copyParam(*src,s_tcapPrefix,'.');
	dest->copyParam(*src,s_lnpPrefix,'.');
    }
}

// Get a space separated word from a buffer. msgUnescape() it if requested
// Return false if empty
static bool getWord(String& buf, String& word, bool unescape = false)
{
    XDebug(&__plugin,DebugAll,"getWord(%s)",buf.c_str());
    int pos = buf.find(" ");
    if (pos >= 0) {
	word = buf.substr(0,pos);
	buf = buf.substr(pos + 1);
    }
    else {
	word = buf;
	buf = "";
    }
    if (!word)
	return false;
    if (unescape)
	word.msgUnescape();
    return true;
}

#ifdef DEBUG
static void dumpData(int debugLevel, const char* message, void* obj, NamedList& params,
		    DataBlock data = DataBlock::empty())
{
    if (obj) {
	String tmp;
	params.dump(tmp,"\r\n  ",'\'',true);
	String str;
	str.hexify(data.data(),data.length(),' ');
	Debug(&__plugin,debugLevel,"%s [%p]\r\nparams='%s',\r\ndata='%s'",message,obj,tmp.c_str(),str.c_str());
    }
}
#endif

/**
 * LNPClient
 */
LNPClient::LNPClient()
    : TCAPUser("LNP"),
      m_queriesMtx(true,"LNPQueries"),
      m_compID(0),
      m_blockedMtx(true,"LNPBlocked")
{
    Debug(this,DebugAll,"LNPClient created [%p]",this);
}

LNPClient::~LNPClient()
{
    Debug(this,DebugAll,"LNPClient destroyed [%p]",this);
}

void LNPClient::destroyed()
{
    DDebug(&__plugin,DebugAll,"LNPClient::destroyed() [%p]",this);
    if (tcap())
	attach(0);
    m_blocked.clear();
    m_queries.clear();
}

bool LNPClient::findTCAP()
{
    SignallingComponent* tcap = 0;
    SignallingEngine* engine = SignallingEngine::self(true);
    if (engine) {
	__plugin.lock();
	NamedString* name = s_cfg.getKey(s_lnpCfg,"tcap");
	__plugin.unlock();
	if (!TelEngine::null(name))
	    tcap = engine->find(*name,"SS7TCAPANSI",tcap);
    }
    if (tcap) {
	Debug(this,DebugInfo,"LNP client attaching to TCAP");
	attach(YOBJECT(SS7TCAPANSI,tcap));
    }
    return (tcap != 0);
}

bool LNPClient::tcapIndication(NamedList& params)
{
    DDebug(this,DebugAll,"LNPClient::tcapIndication() [%p]",this);

    String localTID = params.getValue(s_tcapTID);
    unsigned int compCount = params.getIntValue(s_compCount);
    String paramRoot = "tcap.component.";
    String acg = "";
    bool removeACG = true;

    int dialog = SS7TCAP::lookupTransaction(params.getValue("tcap.request.type"));

    Lock l(m_queriesMtx);
    for (unsigned int i = 1; i <= compCount; i++) {
	String prefix = paramRoot + String(i);
	String payload = params.getValue(prefix);

	Operation opCode = (Operation)params.getIntValue(prefix + s_opCode);
	NamedString* id = params.getParam(prefix + (dialog != SS7TCAP::TC_Notice ? s_localID : s_remoteID));
	LNPQuery* query = 0;
	if (!TelEngine::null(id)) {
	    ObjList* o = m_queries.find(*id);
	    query = (o ? static_cast<LNPQuery*>(o->get()) : 0);
	    if (query) {
		query->extractAddress(params);
		acg = query->calledNumber();
	    }
	}

	int primitive = SS7TCAP::lookupComponent(params.getValue(prefix + s_compType));
	if (dialog == SS7TCAP::TC_Response) {

	    SS7TCAPError error = decodeParameters(params,payload);
	    if (error.error() != SS7TCAPError::NoError && query) {
		// build error
		Debug(this,DebugAll,"Detected error=%s while decoding parameters [%p]",error.errorName().c_str(),this);
		query->setPrimitive(SS7TCAP::TC_Invoke);
		query->setProblemData(payload);
		tcapRequest(SS7TCAP::TC_Unidirectional,query);
	    }
	    switch (primitive) {
		case SS7TCAP::TC_Invoke:
		case SS7TCAP::TC_InvokeNotLast:
		    if (opCode == NetworkManagementACG) {
			// build blocked code
			DDebug(this,DebugAll,"Executing NetworkManagement:ACG operation [%p]",this);
			removeACG = false;
			String code = params.getValue(s_lnpPrefix + s_calledPN);
			u_int64_t duration = params.getIntValue(s_lnpPrefix + s_acgDuration);
			u_int64_t gap = params.getIntValue(s_lnpPrefix + s_acgGap);
			ACGCause cause = (ACGCause) lookup(params.getValue(s_lnpPrefix + s_acgCause),s_acgCauses,0);
			m_blockedMtx.lock();
			BlockedCode* acg = findACG(code);
			if (acg)
			    acg->update(duration,gap,cause);
			else if (!TelEngine::null(code))
			    m_blocked.append(new BlockedCode(code,duration,gap,cause));
			m_blockedMtx.unlock();
		    }
		    else if (opCode == SendNotificationTermination) {
			Debug(this,DebugStub,"LNPClient::handleOperation() [%p] - Operation SendNotification:Termination "
			    "was received, not implemented",this);
		    }
		    else if (opCode == CallerInteractionPlay) {
			DDebug(this,DebugAll,"Executing CallerInteraction:PlayAnnouncement operation [%p]",this);
			if (query)
			    query->endQuery((SS7TCAP::TCAPUserCompActions)primitive,opCode,params);
		    }
		    else if (opCode == ConnectionControlConnect) {
			if (query) {
			    DDebug(this,DebugAll,"Executing ConnectionControl:Connect operation [%p]",this);
    			    query->endQuery((SS7TCAP::TCAPUserCompActions)primitive,opCode,params);
			}
			else
			    return false;
		    }
		    else
			return false;
		    break;
		case SS7TCAP::TC_U_Error:
		case SS7TCAP::TC_R_Reject:
		case SS7TCAP::TC_L_Reject:
		case SS7TCAP::TC_U_Reject:
		case SS7TCAP::TC_L_Cancel:
		    // remove component and return false to call.route
		    DDebug(this,DebugAll,"Executing Cancel operation [%p]",this);
		    if (query)
			query->endQuery((SS7TCAP::TCAPUserCompActions)primitive,None,params);
		    break;

		case SS7TCAP::TC_U_Cancel:
		case SS7TCAP::TC_ResultLast:
		case SS7TCAP::TC_ResultNotLast:
		case SS7TCAP::TC_TimerReset:
		default:

		    break;
	    }
	}
	else if (dialog == SS7TCAP::TC_Notice) {
	    Debug(this,DebugInfo,"Received notice='%s' from sublayer, query=%p [%p]",params.getValue("ReturnCause"),query,this);
	    if (query)
		query->endQuery(SS7TCAP::TC_L_Cancel,None,params);
	    else
		return false;
	}
	else
	    return false;
    }

    if (removeACG && !TelEngine::null(acg)) {
	m_blockedMtx.lock();
	while (BlockedCode* c = findACG(acg))
	    m_blocked.remove(c);
	m_blockedMtx.unlock();
    }
    params.setParam(s_endNow,String::boolText(true));
    return true;
}

bool LNPClient::mandatoryParams(Operation opCode, NamedList& params)
{
    bool ok = true;
    switch (opCode) {
	case ProvideInstructionsStart:
	    // we dont check requests
	    break;
	case ConnectionControlConnect:
	    for (unsigned int i = 0; i < 3; i++) {
		String param;
		if (i == 0)
		    param = s_lnpPrefix + s_routingNumber;
		if (i == 1)
		    param = s_lnpPrefix + s_billing + ".";
		if (i == 2)
		    param = s_lnpPrefix + s_carrier;
		NamedList subParams("");
		if (TelEngine::null(params.getParam(param)) || !subParams.copySubParams(params,param,false).count()) {
		    ok = false;
		    break;
		}
	    }
	    break;
	case CallerInteractionPlay:
	    if (TelEngine::null(params.getParam(s_lnpPrefix + s_announcement)))
		ok = false;
	    break;
	case SendNotificationTermination:
	    // we dont verify send notification
	    break;
	case NetworkManagementACG:
	    for (unsigned int i = 0; i < 2; i++) {
		String param;
		if (i == 0)
		    param = s_lnpPrefix + s_calledPN;
		if (i == 1)
		    param = s_lnpPrefix + s_acg + ".";
		NamedList subParams("");
		if (TelEngine::null(params.getParam(param)) || !subParams.copySubParams(params,param,false).count()) {
		    ok = false;
		    break;
		}
	    }
	case ProceduralError:
	case None:
	default:
	    break;
    }
    if (!ok)
	Debug(&__plugin,DebugAll,"LNPClient::mandatoryParams() - check for mandatory parameters failed for operation=%s [%p]",
	    lookup(opCode,s_operations,""),this);
    return ok;
}

bool LNPClient::managementNotify(SCCP::Type type, NamedList& params)
{
    return true;
}

bool LNPClient::makeQuery(const String& called, Message& msg)
{
    DDebug(this,DebugAll,"LNP Query for number=%s [%p]",called.c_str(),this);
    __plugin.incCounter(SS7LNPDriver::TotalQueries);
    BlockedCode* acg = findACG(called);
    if (acg) {
	if (!acg->codeAllowed()) {
	    Debug(this,DebugInfo,"Blocking LNP query for number=%s, ACG controlled",called.c_str());
	    String announcement;
	    if (acg->acgCause() == ACGDatabaseOverload) {
		announcement = lookup(NoCircuit,s_announce);
		__plugin.incCounter(SS7LNPDriver::DBOverload);
	    }
	    else {
		announcement = lookup(Busy,s_announce);
		__plugin.incCounter(SS7LNPDriver::OSSControls);
	    }
	    if (s_copyBack) {
		msg.setParam(s_lnpPrefix + s_acgCause,lookup(acg->acgCause(),s_acgCauses,String(acg->acgCause())));
		msg.setParam(s_lnpPrefix + s_acgDuration,String(acg->duration()));
		msg.setParam(s_lnpPrefix + s_acgGap,String(acg->gap()));
	    }
	    if (s_playAnnounce) {
		__plugin.lock();
		msg.retValue() << s_cfg.getValue("announcements",announcement,"tone/busy");
		__plugin.unlock();
		msg.setParam("autoprogress",String::boolText(true));
		return true;
	    }
	    else
		return false;
	}
	else {
	    DDebug(this,DebugInfo,"Allowing LNP query for number=%s which is ACG controlled",called.c_str());
	    acg->resetGapInterval();
	}
    }

    LNPQuery* code = new LNPQuery(this,m_compID++,called,&msg);

    if (!tcapRequest(SS7TCAP::TC_QueryWithPerm,code))
	return false;

    m_queriesMtx.lock();
    m_queries.append(code);
    m_queriesMtx.unlock();
    bool status = false;
    int t = waitForQuery(code);
    if (s_playAnnounce && code->status() > LNPQuery::PortingDone)
	status = true;
    m_queriesMtx.lock();
    m_queries.remove(code);
    m_queriesMtx.unlock();
    Debug(this,(t > 500) ? DebugNote : DebugAll,"LNP lookup took %d msec",t);
    return status;
}

bool LNPClient::tcapRequest(SS7TCAP::TCAPUserTransActions primitive, LNPQuery* code)
{
    // request parameters from code object
    if (!code)
	return false;

    DDebug(this,DebugAll,"LNPClient::tcapRequest(type=%s,query=%s) [%p]",SS7TCAP::lookupTransaction(primitive),code->toString().c_str(),this);
    NamedList params("lnp");

    // encode parameters
    String hexPayload;
    NamedList* sect = 0;
    switch (primitive) {
	case SS7TCAP::TC_Unidirectional:
	    if (code->primitive() == SS7TCAP::TC_Invoke) {
		params.setParam(s_lnpPrefix + s_problemData,code->problemData());
		encodeParameters(ProceduralError,params,hexPayload);
		params.setParam(s_opCodeType,"national");
		params.setParam(s_tcapOpCode,String(ProceduralError));
	    }
	    else if (code->primitive() == SS7TCAP::TC_U_Reject) {
		copyLNPParams(&params,code->parameters(),"CalledPartyNumber");
		encodeParameters(None,params,hexPayload);
		params.setParam(s_tcapProblem,String(code->problem().errorCode()));
	    }
	    else
		return false;
	    // complete SCCP data with dpc and SSN only
	    params.setParam(s_remPC,String(code->dbPointCode()));
	    params.setParam(s_cpdSSN,String(code->dbSSN()));
	    params.setParam(s_checkAddr,String::boolText(false));
	    params.setParam(s_cpdRoute,"ssn");
	    break;
	case SS7TCAP::TC_Begin:
	case SS7TCAP::TC_QueryWithPerm:
	    if (code->primitive() != SS7TCAP::TC_Invoke)
		return false;
	    __plugin.lock();
	    params.setParam(s_tcapLocalCID,code->toString());
	    params.setParam(s_opCodeType,"national");
	    params.setParam(s_tcapOpCode,String(ProvideInstructionsStart));
	    params.setParam(s_compTimeout,String(s_cfg.getIntValue(s_lnpCfg,"timeout",3000) / 1000 + 1));
	    // complete sccp data, read from configure
	    sect = s_cfg.getSection(s_sccpCfg);
	    if (!sect) {
		Debug(this,DebugInfo,"Section [sccp_addr] is missing, query abort");
		return false;
	    }
	    params.copySubParams(*sect,s_sccpPrefix);
	    if (String("gt") == params.getValue(s_cpdRoute,"gt")) {
		params.setParam(s_cpdGT,code->calledNumber());
		// Translation Type defaults to 11, which,
		// according to ATIS 1000112.2005 is Number Portability Translation Type
		if (TelEngine::null(params.getParam(s_cpdTT)))
		    params.setParam(s_cpdTT,String(11));
	    }
	    params.setParam(s_remPC,String(s_remotePC.pack(s_remotePCType)));
	    params.setParam(s_checkAddr,String::boolText(sect->getBoolValue("check_addr",false)));
	    copyLNPParams(&params,code->parameters(),"");
	    encodeParameters(ProvideInstructionsStart,params,hexPayload);
	    __plugin.unlock();
	    break;
	case SS7TCAP::TC_Response:
	case SS7TCAP::TC_Unknown:
	    params.setParam(s_tcapLocalCID,code->toString());
	    params.setParam(s_checkAddr,String::boolText(false));
	    params.setParam(s_tcapTID,code->dialogID());
	    params.setParam(s_tcapBasicTerm,String::boolText(false));
	    break;
	case SS7TCAP::TC_QueryWithoutPerm:
	case SS7TCAP::TC_Continue:
	case SS7TCAP::TC_ConversationWithPerm:
	case SS7TCAP::TC_ConversationWithoutPerm:
	case SS7TCAP::TC_End:
	case SS7TCAP::TC_U_Abort:
	case SS7TCAP::TC_P_Abort:
	case SS7TCAP::TC_Notice:
	default:
	    return false;
    }

    // set component parameters
    params.setParam(s_compCount,"1");
    params.setParam(s_tcapComp,hexPayload);
    params.setParam(s_tcapCompType,SS7TCAP::lookupComponent(code->primitive()));
    // set transaction parameters
    params.setParam(s_tcapReq,SS7TCAP::lookupTransaction(primitive));
    params.setParam(s_tcapUser,toString());
    // send to tcap
    if (tcap()) {
	SS7TCAPError err = tcap()->userRequest(params);
	if (err.error() != SS7TCAPError::NoError)
	    return false;
	if (primitive == SS7TCAP::TC_QueryWithPerm || primitive == SS7TCAP::TC_Begin)
	    code->setDialogID(params.getValue(s_tcapTID));
    }
    else
	return false;
    return true;
}

int LNPClient::waitForQuery(LNPQuery* query)
{
    u_int64_t t = Time::msecNow();
    while (true) {
	Lock mylock(m_queriesMtx);
	if (!query || (query && query->status() != LNPQuery::Pending))
	    return (int)(Time::msecNow() - t);
	if (query->timedOut() && query->status() != LNPQuery::TimedOut) {
	    Debug(this,DebugAll,"Query for called=%s timed out [%p]",query->calledNumber().c_str(),this);
	    query->endQuery(SS7TCAP::TC_U_Cancel,(int)None,NamedList::empty());
	}
	mylock.drop();
	Thread::idle();
    }
}

SS7TCAPError LNPClient::decodeParameters(NamedList& params, String& hexData)
{
    SS7TCAPError error(SS7TCAP::ANSITCAP);

    DataBlock data;
    data.unHexify(hexData,hexData.length(),' ');
    if (!data.length())
	return error;

    // decode parameter set
    u_int8_t tag = data[0];
    if (tag != 0xf2)
	return error;
    data.cut(-1);
    int len = ASNLib::decodeLength(data);
    if (len < 0 || len > (int)data.length()) {
	error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
	hexData.hexify(&tag,sizeof(tag),' ');
	return error;
    }
    String value = "";
    u_int8_t aux = 0;
    while (data.length() && error.error() == SS7TCAPError::NoError) {
	tag = data[0];
	switch (tag) {
	    case ServiceKey:
		data.cut(-1);
		len = ASNLib::decodeLength(data);
		error = decodeDigits(params,data,s_lnpPrefix);
		break;
	    case StandardAnnouncement:
		data.cut(-1);
		len = ASNLib::decodeLength(data);
		if (len != 1)
		    error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
		else {
		    params.setParam(s_lnpPrefix + s_announcement,lookup(data[0],s_announce,"busy"));
		    data.cut(-1);
		}
		break;
	    case Digits:
		error = decodeDigits(params,data,s_lnpPrefix);
		break;
	    case ProblemData:
		data.cut(-1);
		len = ASNLib::decodeLength(data);
		value.hexify(data.data(),len,' ');
		params.setParam(s_lnpPrefix + s_problemData,value);
		data.cut(-len);
		break;
	    case PrivateErrorCode:
		data.cut(-1);
		len = ASNLib::decodeLength(data);
		if (len != 1)
		    error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
		else {
		    params.setParam(s_lnpPrefix + s_privateError,String(data[0]));
		    data.cut(-1);
		}
		break;
	    case PrivateParam:
		data.cut(-1);
		aux = data[0];
		error = decodePrivateParam(params,data);
		break;
	}
	if (error.error() != SS7TCAPError::NoError) {
	    hexData.clear();
	    if (aux) {
		DataBlock db(&aux,sizeof(aux));
		db.insert(DataBlock(&tag,sizeof(tag)));
		hexData.hexify(db.data(),db.length(),' ');
	    }
	    else
		hexData.hexify(&tag,sizeof(tag),' ');
	    return error;
	}
    }
    return error;
}

void LNPClient::encodeParameters(Operation op, NamedList&params, String& hexPayload)
{
    // mask to know which parameters to look for when encoding
    // assignment is bit A = ServiceKey, bit B = CalledPartyNumber, bit C = CallingPartyNumber, bit D = LATA,
    // bit E = OriginatingStationLine, bit F = CICExpansion, big G = ProblemData
    u_int8_t encodeMask = 0x00;
    if (op == None) // should be a reject component
	encodeMask = 0x02;
    else if (op == ProceduralError)
	encodeMask = 0x40;
    else if (op == ProvideInstructionsStart)
	encodeMask = 0x3d;

    DataBlock data;
    u_int8_t tag = 0;
    DataBlock db;
    if ((encodeMask & 0x01)) {// ServiceKey
	encodeDigits(CalledPartyNumber,params,db);
	db.insert(ASNLib::buildLength(db));
	data.insert(db);
	tag = ServiceKey;
	data.insert(DataBlock(&tag,1));
	db.clear();
    }
    if ((encodeMask & 0x02)) // CalledPartyNumber
	encodeDigits(CalledPartyNumber,params,data);
    if ((encodeMask & 0x04)) // CallingPartyNumber
	encodeDigits(CallingPartyNumber,params,data);
    if ((encodeMask & 0x08)) // LATA
	encodeDigits(LATA,params,data);
    if ((encodeMask & 0x30)) {
	if ((encodeMask & 0x10))
	    encodePrivateParam(OrigStationType,params,data);
	if ((encodeMask & 0x20))
	    encodePrivateParam(CICExpansion,params,data);
    }
    if ((encodeMask & 0x40)) { // ProblemData
	String hex = params.getValue(s_lnpPrefix + s_problemData);
	db.unHexify(hex,hex.length(),' ');
	db.insert(ASNLib::buildLength(db));
	tag = ProblemData;
	data.insert(db);
	data.insert(DataBlock(&tag,1));
    }

    data.insert(ASNLib::buildLength(data));
    tag = 0xf2;
    data.insert(DataBlock(&tag,1));
    hexPayload.hexify(data.data(),data.length(),' ');
}

SS7TCAPError LNPClient::decodePrivateParam(NamedList& params, DataBlock& data)
{
    SS7TCAPError error(SS7TCAP::ANSITCAP);
    // decode parameter set
    if (data.length() < 2)
	return error;
    unsigned int tag = data[0];
    data.cut(-1);
    unsigned int len = ASNLib::decodeLength(data);
    switch (tag) {
	case BillingIndicators:
	    if (len != 4)
		error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
	    else  {
		unsigned char* buff = data.data(0,2);
		String digits = "";
		decodeBCD(3,digits,buff);
		params.setParam(s_lnpPrefix + s_billAMA,digits);
		buff = data.data(2,2);
		digits.clear();
		decodeBCD(3,digits,buff);
		params.setParam(s_lnpPrefix + s_billFeature,digits);
		data.cut(-4);
	    }
	    break;
	case ConnectTime:
	case EchoData:
	case TerminationIndicators:
	    if (len != 4)
		error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
	    else  {
		String value = "";
		value.hexify(data.data(),len,' ');
		params.setParam(s_lnpPrefix + s_echoData,value);
		data.cut(-4);
	    }
	    break;
	case OrigStationType:
	    if (len != 1)
		error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
	    else  {
 		params.setParam(s_lnpPrefix + s_stationType,lookup(data[0],s_dict_oli,String(data[0])));
		data.cut(-1);
	    }
	    break;
	case ACGIndicator:
	    if (len != 3)
		error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
	    else  {
		params.setParam(s_lnpPrefix + s_acgCause,lookup(data[0],s_acgCauses,String(data[0])));
		unsigned int duration = 1 << (data[1] > 0 ? data[1] - 1 : 0);
		params.setParam(s_lnpPrefix + s_acgDuration,String(duration));
		params.setParam(s_lnpPrefix + s_acgGap,lookup(data[2],s_gaps,"0"));
		data.cut(-3);
	    }
	    break;
	case CICExpansion:
	    if (len != 1)
		error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
	    else  {
		params.setParam(s_lnpPrefix + s_cicExp,String(data[0]));
		data.cut(-1);
	    }
	    break;
	case DigitsPrivate:
	default:
	    break;
    }
    return error;
}

void LNPClient::encodePrivateParam(PrivateLNP param, NamedList& params, DataBlock& data)
{
    u_int8_t tag = 0;
    u_int8_t len = 0;
    DataBlock db;
    switch (param) {
	case OrigStationType:
	    tag = lookup(params.getValue(s_lnpPrefix +  s_stationType),s_dict_oli,
			 params.getIntValue(s_lnpPrefix + s_stationType));
	    db.append(&param,1);
	    len = 1;
	    db.append(&len,1);
	    db.append(&tag,1);
	    break;
	case CICExpansion:
	    tag = params.getIntValue(s_lnpPrefix + s_cicExp,1);
	    db.append(&param,1);
	    len = 1;
	    db.append(&len,1);
	    db.append(&tag,1);
	    break;
	case BillingIndicators:
	case ConnectTime:
	case EchoData:
	case TerminationIndicators:
	case ACGIndicator:
	case DigitsPrivate:
	default:
	    break;
    }
    tag = PrivateParam;
    data.insert(db);
    data.insert(DataBlock(&tag,1));
}

SS7TCAPError LNPClient::decodeDigits(NamedList& params, DataBlock& data, const char* prefix)
{
    SS7TCAPError error(SS7TCAP::ANSITCAP);
    if (data[0] != Digits)
	return	error;
    data.cut(-1);
    int len = ASNLib::decodeLength(data);
    if (len < 0 || len > (int)data.length()) {
	error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
	return error;
    }
    unsigned char* buff = data.data(0,len);
    int index = 0;
    String digits;
    String param = (prefix ? String(prefix) + "." : "");
    while (index < len) {
	u_int8_t byte = buff[index];
	switch (index) {
	    case 0:
		param += lookup(byte,s_digitTypes);
		params.setParam(param + ".type",lookup(byte,s_digitTypes,String(byte)));
		break;
	    case 1:
		params.setParam(param + ".nature",lookup((byte & NatureInternational),s_nature,String(byte & NatureInternational)));
		params.setParam(param + ".restrict",((byte & PresentationRestriction) ? "true" : "false"));
		break;
	    case 2:
		params.setParam(param + ".plan",lookup((byte & 0xf0),s_plans,String((byte & 0xf0) >> 4)));
		params.setParam(param + ".encoding",lookup((byte & 0x0f),s_encodings,String(byte & 0x0f)));
		break;
	    case 3:
		index += decodeBCD(byte,digits,(len > 4 ? data.data(4,(byte / 2 + (byte % 2 ? 1 : 0))) : 0));
		if (index >= len) {
		    error.setError(SS7TCAPError::General_BadlyStructuredCompPortion);
		    return error;
		}
		params.setParam(param,digits);
		break;
	    default:
		break;
	}
	index++;
    }
    data.cut(-len);
    return error;
}

void LNPClient::encodeDigits(DigitsType type, NamedList& params, DataBlock& data)
{
    String prefix = lookup(type,s_digitTypes);
    NamedString* digits = params.getParam(s_lnpPrefix + "." + prefix);
    if (TelEngine::null(digits))
	return;
    DDebug(this,DebugAll,"LNPClient::encodeDigits(type=%s,digits=%s [%p]",lookup(type,s_digitTypes,""),digits->c_str(),this);
    DataBlock db;
    encodeBCD(*digits,db);

    unsigned int index = 4;
    while (index != 0) {
	u_int8_t byte = 0;
	switch (index) {
	    case 1:
		byte = (type ? type : params.getIntValue(prefix + ".type",type));
		db.insert(DataBlock(&byte,sizeof(byte)));
		break;
	    case 2:
		byte |= (lookup(s_cfg.getValue(s_lnpCfg,"number_nature"),s_nature,NatureNational) &  NatureInternational);
		if (s_cfg.getBoolValue(s_lnpCfg,"presentation_restrict",false))
		    byte |= PresentationRestriction;
		db.insert(DataBlock(&byte,sizeof(byte)));
		break;
	    case 3:
		if (type == LATA || type == Carrier)
		    byte |= (NotUsed & 0xf0);
		else {
		    String numPlan = s_cfg.getValue(s_lnpCfg,"numplan","isdn");
		    byte |= (lookup(numPlan,s_plans,NPISDN) & 0xf0);
		}
		byte |= (lookup(s_cfg.getValue(s_lnpCfg,"number_encoding","bcd"),s_encodings,EncodingBCD) & 0x0f);
		db.insert(DataBlock(&byte,sizeof(byte)));
		break;
	    case 4:
		byte = digits->length();
		db.insert(DataBlock(&byte,sizeof(byte)));
		break;
	    default:
		break;
	}
	index--;
    }
    db.insert(ASNLib::buildLength(db));
    u_int8_t tag = Digits;
    data.insert(db);
#ifdef DEBUG
    dumpData(DebugAll,"Encoded digits",this,params,db);
#endif
    data.insert(DataBlock(&tag,sizeof(tag)));
}

void LNPClient::encodeBCD(String& digits, DataBlock& data)
{
    unsigned int len = digits.length() / 2 + (digits.length() % 2 ?  1 : 0);
    unsigned char buf[30];
    unsigned int i = 0;
    unsigned int j = 0;
    bool odd = false;
    while((i < digits.length()) && (j < len)) {
	char c = digits[i++];
	unsigned char d = 0;
	if (('0' <= c) && (c <= '9'))
	    d = c - '0';
	else if ('A' == c)
	    d = 10;
	else if ('B' == c)
	    d = 11;
	else if ('C' == c)
	    d = 12;
	else if ('*' == c)
	    d = 13;
	else if ('#' == c)
	    d = 14;
	else if ('.' == c)
	    d = 15;
	else
	    continue;
	odd = !odd;
	if (odd)
	    buf[j] = d;
	else
	    buf[j++] |= (d << 4);
    }
    if (odd)
	j++;
    data.append(&buf,j);
}

unsigned int LNPClient::decodeBCD(unsigned int length, String& digits, unsigned char* buff)
{
    if (!(buff && length))
	return 0;
    static const char s_digits[] = "0123456789ABC*#.";
    unsigned int bytesNo = length / 2 + (length % 2 ?  1 : 0);
    unsigned int index = 0;
    while (index < bytesNo) {
	digits += s_digits[(buff[index] & 0x0f)];
	if (index * 2 + 1 < length)
	    digits += s_digits[(buff[index] >> 4)];
	index++;
    }
    XDebug(this,DebugAll,"Decoded BCD digits=%s",digits.c_str());
    return index;
}

void LNPClient::checkBlocked()
{
    Lock l(m_blockedMtx);
    ListIterator iter(m_blocked);
    for (;;) {
	BlockedCode* code = static_cast<BlockedCode*>(iter.get());
	if (!code)
	    break;
	if (code->durationExpired())
	    m_blocked.remove(code);
    }
}

BlockedCode* LNPClient::findACG(const char* code)
{
    ObjList* o = m_blocked.find(code);
    if (!o) {
	ListIterator iter(m_blocked);
	for(;;) {
	    BlockedCode* acg = static_cast<BlockedCode*>(iter.get());
	    if (!acg)
		return 0;;
	    if (acg->toString().startsWith(code))
		return acg;
	}
    }
    else
	return static_cast<BlockedCode*>(o->get());
}

void LNPClient::statusBlocked(String& status)
{
    Lock l(m_blockedMtx);
    status.append("format=Cause|Duration|Gap|Allowed",",");
    status.append("count=",";") << m_blocked.count();
    ListIterator iter(m_blocked);
    String str;
    for(;;) {
	BlockedCode* acg = static_cast<BlockedCode*>(iter.get());
	if (!acg)
	    break;
	str.append(acg->toString(),",") << "=" << lookup(acg->acgCause(),s_acgCauses) << "|" << acg->duration() << "|"
	    << acg->gap() << "|" << String::boolText(acg->codeAllowed());
    }
    status.append(str,";");
}

/**
 * LNPQuery
 */
LNPQuery::LNPQuery(LNPClient* lnp, u_int8_t id, const String& called, Message* msg)
    : m_id(id), m_msg(msg), m_status(Pending), m_primitive(SS7TCAP::TC_Invoke), m_error(SS7TCAP::ANSITCAP),
      m_dbSSN(0), m_dbPC(0), m_dialogID(""), m_called(called), m_lnp(lnp)
{
    Debug(&__plugin,DebugAll,"LNPQuery::LNPQuery() created with id=%u, for called=%s [%p]",id,called.c_str(),this);
    Lock l(__plugin);
    m_timeout = Time::msecNow() + s_cfg.getIntValue(s_lnpCfg,"timeout",3000,1000,30000); // milliseconds
}

LNPQuery::~LNPQuery()
{
    Debug(&__plugin,DebugAll,"LNPQuery::LNPQuery() destroyed [%p]",this);
    m_msg = 0;
    m_lnp = 0;
}

void LNPQuery::endQuery(SS7TCAP::TCAPUserCompActions primitive, int opCode, const NamedList& params)
{
    DDebug(&__plugin,DebugAll,"LNPQuery::endQuery() for id=%s, callednum=%s with request=%s [%p]",
	m_id.c_str(),m_called.c_str(),SS7TCAP::lookupComponent(primitive),this);

    String retValue = "";
    bool copy = true;
    switch (primitive) {
	case SS7TCAP::TC_Invoke:
	case SS7TCAP::TC_InvokeNotLast:
	    if (opCode == LNPClient::CallerInteractionPlay) {
		__plugin.lock();
		retValue = s_cfg.getValue("announcements",params.getValue(s_lnpPrefix + s_announcement,"busy"),"tone/busy");
		__plugin.unlock();
		m_status = Announcement;
		__plugin.incCounter(SS7LNPDriver::Announcement);
	    }
	    else if (opCode == LNPClient::ConnectionControlConnect && m_msg) {
		m_msg->setParam("querylnp",String::boolText(false));
		m_msg->setParam("npdi",String::boolText(true));
		String routing = params.getValue(s_lnpPrefix + s_routingNumber);
		if (routing != calledNumber())
		    m_msg->setParam("routing",routing);
		m_status = PortingDone;
		__plugin.incCounter(SS7LNPDriver::PortedQueries);
	    }
	    break;
	case SS7TCAP::TC_U_Error:
	case SS7TCAP::TC_R_Reject:
	case SS7TCAP::TC_L_Reject:
	case SS7TCAP::TC_U_Reject:
	    m_status = (primitive == SS7TCAP::TC_U_Error ? ReportedError : ResponseRejected);
	    __plugin.incCounter(SS7LNPDriver::ErrorredQueries);
	    break;
	case SS7TCAP::TC_U_Cancel:
	    setPrimitive(SS7TCAP::TC_U_Cancel);
	    if (m_lnp)
		m_lnp->tcapRequest(SS7TCAP::TC_Response,this);
	    m_status = TimedOut;
	    __plugin.incCounter(SS7LNPDriver::TimedOutQueries);
	    break;
	case SS7TCAP::TC_L_Cancel:
	    m_status = ReportedError;
	    copy = false;
	    __plugin.incCounter(SS7LNPDriver::SendFailure);
	    break;
	case SS7TCAP::TC_ResultLast:
	case SS7TCAP::TC_ResultNotLast:
	case SS7TCAP::TC_TimerReset:
	default:
	    break;
    }
    if (m_msg) {
	if (s_playAnnounce && !TelEngine::null(retValue)) {
	    m_msg->retValue() << retValue;
	    m_msg->setParam("autoprogress",String::boolText("true"));
	}
	if (copy)
	    m_msg->copyParam(params,s_lnpPrefix,'.');
    }
}

void LNPQuery::extractAddress(NamedList& params)
{
    m_dbSSN = params.getIntValue("CallingPartyAddress.ssn");
    m_dbPC = params.getIntValue(s_remPC);
    DDebug(&__plugin,DebugAll,"LNPQuery::extractAddress() - extract remoteSSN =%d, remotePC=%d [%p]",m_dbSSN,m_dbPC,this);
}

/**
 * BlockedCode
 */
BlockedCode::BlockedCode(const char* code, u_int64_t duration, u_int64_t gap, LNPClient::ACGCause cause)
    : m_code(code)
{
    Debug(&__plugin,DebugAll,"BlockedCode created [%p] - code '%s' blocked for " FMT64U
	" seconds with gap=" FMT64U " seconds, cause=%s",
	this,m_code.c_str(),duration,gap,lookup(cause,s_acgCauses,""));
    update(duration,gap,cause);
}

BlockedCode::~BlockedCode()
{
    Debug(&__plugin,DebugAll,"BlockedCode[%p] destroyed for code '%s'",this,m_code.c_str());
}

void BlockedCode::resetGapInterval()
{
    u_int64_t interval = (u_int64_t) (90.0 + (110 - 90) * ((double)Random::random() / (double)RAND_MAX)) / 100.0 * m_gap;
    DDebug(&__plugin,DebugAll,"BlockedCode created [%p] - code '%s' has gap interval=" FMT64
	" seconds",this,m_code.c_str(),interval);
    m_gapExpiry = Time::secNow() + interval;
}

void BlockedCode::update(u_int64_t duration, u_int64_t gap, LNPClient::ACGCause cause)
{
    DDebug(&__plugin,DebugAll,"BlockedCode created [%p] - code '%s' update duration=" FMT64
	" seconds, gap=" FMT64 " seconds",this,m_code.c_str(),duration,gap);
    m_cause = cause;
    m_duration = (unsigned int)duration;
    m_durationExpiry = Time::secNow() + duration;
    m_gap = (unsigned int)gap;
    resetGapInterval();
}

/**
 * SS7LNPDriver
 */
SS7LNPDriver::SS7LNPDriver()
    : Module("ss7_lnp_ansi","misc"),
      m_lnp(0)
{
    Output("Loaded module SS7LnpAnsi");
}

SS7LNPDriver::~SS7LNPDriver()
{
    Output("Unloaded module SS7LnpAnsi");
    TelEngine::destruct(m_lnp);
}

void SS7LNPDriver::initialize()
{
    Output("Initializing module SS7LnpAnsi");
    Module::initialize();
    lock();
    s_cfg = Engine::configFile(name());
    s_cfg.load();
    unlock();
    if (!m_lnp)
	m_lnp = new LNPClient();
    installRelay(Route,s_cfg.getIntValue("general","call.route",50));
    installRelay(Timer);
    installRelay(Help);

    s_copyBack = s_cfg.getBoolValue(s_lnpCfg,"copy_back_all",true);
    s_lnpPrefix = s_cfg.getValue("general","prefix","lnp");
    s_playAnnounce = s_cfg.getBoolValue("general","play_announcements",false);

    const char* code = s_cfg.getValue(s_sccpCfg,YSTRING("remote_pointcode"));
    s_remotePCType = SS7PointCode::lookup(s_cfg.getValue(s_sccpCfg,YSTRING("pointcodetype"),""));
    if (!(s_remotePC.assign(code,s_remotePCType) && s_remotePC.pack(s_remotePCType))) {
	int codeInt = s_cfg.getIntValue(s_sccpCfg,YSTRING("remote_pointcode"));
	if (!s_remotePC.unpack(s_remotePCType,codeInt))
	    Debug(this,DebugMild,"SS7LNPDriver::initialize() [%p] - Invalid remote_pointcode=%s value configured",
		this,code);
    }
    resetCounters(true);
}

bool SS7LNPDriver::msgRoute(Message& msg)
{
    XDebug(this,DebugAll,"SS7LNPDriver::msgRoute()");
    if (!msg.getBoolValue("querylnp_tcap",true))
	return false;
    Lock mylock(this);
    String called = s_cfg.getValue(s_lnpCfg,"called","${called}");
    msg.replaceParams(called);
    if (!msg.getBoolValue("querylnp",TelEngine::isE164(called) && !msg.getBoolValue("npdi")))
	return false;
    mylock.drop();
    bool ok = false;
    if (m_lnp)
	ok = m_lnp->makeQuery(called,msg);
    if (!s_copyBack)
	msg.clearParam(s_lnpPrefix,'.');
    return ok;
}

void SS7LNPDriver::msgTimer(Message& msg)
{
    if (m_countReset < Time::secNow())
	resetCounters();
    if (m_lnp) {
	if (!m_lnp->tcap())
	    m_lnp->findTCAP();
	m_lnp->checkBlocked();
    }
}
bool SS7LNPDriver::received(Message& msg, int id)
{
    if (id == Help) {
	String line = msg.getValue("line");
	if (line.null()) {
	    msg.retValue() << "  " << s_cmdsLine << "\r\n";
	    return false;
	}
	if (line != "lnp")
	    return false;
	msg.retValue() << "Commands for the SS7 LNP module\r\n";
	msg.retValue() << s_cmdsLine << "\r\n";
	return true;
    }
    return Module::received(msg,id);
}

bool SS7LNPDriver::commandComplete(Message& msg, const String& partLine, const String& partWord)
{
    if (partLine.null() && partWord.null())
	return false;
    XDebug(this,DebugAll,"commandComplete() partLine='%s' partWord=%s",
	partLine.c_str(),partWord.c_str());
    if (partLine.null() || partLine == "help")
	return Module::itemComplete(msg.retValue(),"lnp",partWord);
    // Line is module name: complete module commands
    if (partLine == "lnp") {
	for (const TokenDict* list = s_cmds; list->token; list++)
	    Module::itemComplete(msg.retValue(),list->token,partWord);
	return true;
    }
    return Module::commandComplete(msg,partLine,partWord);
}

bool SS7LNPDriver::parseParams(const String& line, NamedList& parsed, String& error)
{
    Debug(this,DebugAll,"SS7LNPDriver::parseParams(%s)",line.c_str());
    bool ok = true;
    ObjList* list = line.split(' ',false);
    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	int pos = s->find("=");
	// Empty parameter name is not allowed
	if (pos < 1) {
	    error << "Invalid parameter " << *s;
	    ok = false;
	    break;
	}
	String name = s->substr(0,pos);
	String value = s->substr(pos + 1);
	name.msgUnescape();
	value.msgUnescape();
	parsed.addParam(name,value);
	XDebug(&__plugin,DebugAll,"parseParams() found '%s'='%s'",name.c_str(),value.c_str());
    }
    TelEngine::destruct(list);
    return ok;
}

bool SS7LNPDriver::commandExecute(String& retVal, const String& line)
{
    String tmp(line);
    if (!tmp.startSkip("lnp",false))
	return false;
    tmp.trimSpaces();
    XDebug(this,DebugAll,"commandExecute(%s)",tmp.c_str());
    // Retrieve the command
    String cmdStr;
    int cmd = 0;
    if (getWord(tmp,cmdStr))
	cmd = lookup(cmdStr,s_cmds);
    if (!cmd) {
	retVal << "Unknown command\r\n";
	return true;
    }

    // Execute the command
    bool ok = false;
    String error;
    if (cmd == CmdList) {
	String str;
	Module::statusModule(str);
	if (m_lnp)
	    m_lnp->statusBlocked(str);
	ok = true;
	retVal << str << "\r\n";
    }
    else if (cmd == CmdTest){
	Message msg("");
	if (parseParams(tmp,msg,error)) {
	    if (TelEngine::null(msg.getParam("called")) || TelEngine::null(msg.getParam("caller")))
		error = "Parameter 'called' or 'caller' is missing, both are mandatory";
	    else {
		if (!m_lnp)
		    error = "LNP Client not instantiated";
		else {
		    m_lnp->makeQuery(msg.getParam("called"),msg);
		    msg.dump(retVal," ");
		    retVal << "\r\n";
		    ok = true;
		}
	    }
	}
    }
    else {
	Debug(this,DebugStub,"Command '%s' not implemented",cmdStr.c_str());
	error = "Unknown command";
    }
    retVal << "lnp " << cmdStr << (ok ? " succeeded" : " failed");
    if (!ok && error)
	retVal << ". " << error;
    retVal << "\r\n";
    return true;
}

void SS7LNPDriver::statusModule(String& str)
{
    Module::statusModule(str);
    str.append("format=Total|Current",",");
}

void SS7LNPDriver::statusParams(String& str)
{
    str.append("count=",",") << TotalQueries;
}

void SS7LNPDriver::statusDetail(String& str)
{
    for (unsigned int i = 0; i < TotalQueries; i++) {
	unsigned int current = m_currentCounts[i];
	unsigned int total = m_overallCounts[i];
	str.append(lookup(i+1,s_counters,""),",") << "=" << total << "|" << current;
    }
}

void SS7LNPDriver::incCounter(LNPCounter counter)
{
    if (counter < Announcement || counter > TotalQueries)
	return;
    m_currentCounts[counter - 1]++;
    m_overallCounts[counter - 1]++;
}

void SS7LNPDriver::resetCounters(bool globalToo)
{
    lock();
    for (unsigned int i = 0; i < TotalQueries; i++) {
	XDebug(this,DebugAll,"Resetting statistic counters");
	m_currentCounts[i] = 0;
	if (globalToo)
	    m_overallCounts[i] = 0;
    }
    m_countReset = Time::secNow() + s_cfg.getIntValue("general","count_time",300); // standard says reset every 5 minutes
    unlock();
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
