/**
 * monitoring.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Module for monitoring and gathering information about YATE.
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

#include <yatephone.h>

#define SIP_PORT	5060

using namespace TelEngine;

namespace {

class Monitor;
class MsgUpdateHandler;
class CdrHandler;
class HangupHandler;
class CallMonitor;

// structure to hold a counter, a threshold for the counter
// and an alarm for when the threshold had been surpassed
typedef struct {
    unsigned int counter;
    unsigned int threshold;
    bool alarm;
} BaseInfo;

// container for MGCP transaction information
typedef struct {
    BaseInfo transactions;  // MGCP transactions information
    BaseInfo deletes;       // MGCP delete connection transactions that have timed out
    u_int64_t reset;	    // interval after which the data of this structure should be reset
    u_int64_t resetTime;    // time at which the data should be reset
    bool gw_monitor;
} MGCPInfo;

// container for SIP transaction information
typedef struct {
    BaseInfo auths;	    // SIP authentication requests information
    BaseInfo transactions;  // SIP timed out transactions information
    BaseInfo byes;	    // SIP timed out BYE transactions information
    u_int64_t reset;
    u_int64_t resetTime;
} SIPInfo;

/**
 * Class Cache
 * BaseClass for retaining and expiring different type of data
 */
class Cache : public Mutex, public GenObject
{
public:
    enum Info {
	COUNT   = 1,
	INDEX   = 2,
    };
    inline Cache(const char* name)
    	: Mutex(false,name),
	  m_reload(true), m_expireTime(0), m_retainInfoTime(0)
	{ }
    virtual ~Cache();

    // set the time which is used for increasing the expire time for the cache at each access
    inline void setRetainInfoTime(u_int64_t time)
    {
	m_retainInfoTime = time;
	m_expireTime = 0; //expire data
    }

    // get information from the cached data
    virtual String getInfo(const String& query, unsigned int& index, TokenDict* dict);
    // check if the information has expired
    inline bool isExpired()
	{ return Time::secNow() > m_expireTime; }
    // update the time at which the data will expire
    inline void updateExpire()
    {
	m_expireTime = Time::secNow() + m_retainInfoTime;
	m_reload = false;
    }

protected:
    // load data into this object from a engine.status message
    virtual bool load();
    // discard the cached data
    virtual void discard();
    // table containing information about modules obtained from an engine.status message
    ObjList m_table;
    // flag for reloading
    bool m_reload;
private:
    // time at which the cached data will expire (in seconds)
    u_int64_t m_expireTime;
    // value with which increase the expire time at each access
    u_int64_t m_retainInfoTime;
};

/**
 * Class ActiveCallInfo
 * Hold data about current calls
 */
class ActiveCallsInfo : public Cache
{
public:
    enum InfoType {
	COUNT       = 1,    // count of current call
	INDEX       = 2,    // index of a call
	ID	    = 3,    // id of a call
	STATUS      = 4,    // status of a call
	CALLER      = 5,    // caller party
	CALLED      = 6,    // called party
	PEER	    = 7,    // peer(s) channel of a call
	DURATION    = 8,     // call duration
    };
    // Constructor
    inline ActiveCallsInfo()
	: Cache("Monitor::activeCallsInfo")
	{ }
    // Destructor
    inline ~ActiveCallsInfo()
	{ }
    // add information about peers by checking the billing ID
    String checkPeers(const String& billID, const String& callID);

protected:
    // load data into this object from a engine.status message
    bool load();
};

/**
 * Class SigInfo
 * Base class for handling information from the signalling channel module about signalling components
 */
class SigInfo : public Cache
{
public:
    // enum for types of information
    enum InfoType {
	COUNT       = 1,    // number of components
	INDEX       = 2,    // index of a component
	ID	    = 3,    // id of a component
	STATUS      = 4,    // status of a component
	TYPE	    = 5,    // the type of the component
	ALARMS_COUNT = 6,   // alarm counter for the component
	SKIP	    = 7,     // helper value to skip unnecessary information when parsing the status string
    };
    // Constructor
    inline SigInfo(const char* name, const TokenDict* dict)
	: Cache(name), m_dictionary(dict)
	{ }
    // Destructor
    inline ~SigInfo()
	{ m_table.clear(); }
    // update the alarm counter for the component with the given name
    void updateAlarmCounter(const String& name);
protected:
    // load data into this object from a engine.status message
    virtual bool load();
    virtual void discard();
    // dictionary for
    const TokenDict* m_dictionary;
};

/**
 * Class InterfaceInfo
 */
class InterfaceInfo : public SigInfo
{
public:
    // Constructor
    inline InterfaceInfo()
	: SigInfo("Monitor::ifaceInfo",s_ifacesInfo)
	{ }
    // Destructor
    inline ~InterfaceInfo()
	{ }
    // Dictionary for mapping Monitor queries about signalling interfaces
    static TokenDict s_ifacesInfo[];
protected:
    // load data into this object from a engine.status message
    bool load();
};

/**
 * Class LinkInfo
 */
class LinkInfo : public SigInfo
{
public:
    enum LinkExtraInfo {
    	UPTIME		= 8,
    };
    // Constructor
    inline LinkInfo()
	: SigInfo("Monitor::linkInfo",s_linkInfo)
	{ }
    // Destructor
    inline ~LinkInfo()
	{ }
    // dictionary for mapping Monitor queries
    static TokenDict s_linkInfo[];
protected:
    // load data into this object from a engine.status message
    bool load();
};

/**
 * Class LinksetInfo
 * Hold status data about signalling linksets
 */
class LinksetInfo : public SigInfo
{
public:
    inline LinksetInfo()
	: SigInfo("Monitor::linksetInfo",s_linksetInfo)
	{ }
    inline ~LinksetInfo()
	{ }
    // dictionary for mapping Monitor queries
    static TokenDict s_linksetInfo[];
protected:
    // load data into this object from a engine.status message
    bool load();
    // parse individual link entries
    NamedList* parseLinksetInfo(String& info, const String& link, NamedList* infoFill = 0);
    // dictonary for mapping status parameters
    static TokenDict s_linksetStatus[];
};

/**
 * Class TrunkInfo
 * Hold status data about signalling trunks
 */
 class TrunkInfo : public SigInfo
 {
 public:
    enum TrunkExtraInfo {
    	CIRCUITS	=  7,
	CALLS		=  8,
	LOCKED		=  9,
	IDLE		= 10,
    };
    // Constructor
    inline TrunkInfo()
	: SigInfo("Monitor::trunkInfo",s_trunkInfo)
	{ }
    // Destructor
    inline ~TrunkInfo()
	{ }
    // dictionary for mapping Monitor queries
    static TokenDict s_trunkInfo[];
protected:
    // load data into this object from a engine.status message
    bool load();
    virtual void discard();
    // parse individual trunk entries
    NamedList* parseTrunkInfo(String& info,const String& trunkName, NamedList* infoFill = 0);
    // dictonary for mapping status parameters
    static TokenDict s_trunkStatus[];
 };

/**
 * Accounts Info
 * Cache for account status information
 */
class AccountsInfo : public Cache
{
public:
    // account information type
    enum AccountInfoType {
	COUNT       = 1,
	INDEX       = 2,
	ID	    = 3,
	STATUS      = 4,
	PROTO	    = 5,
	USERNAME    = 6,
    };
    // Constructor
    inline AccountsInfo()
	: Cache("Monitor::accountsInfo")
	{ }
    // Destructor
    inline ~AccountsInfo()
	{ }
private:
    // load data into this object from a engine.status message
    bool load();
};

/**
 * EngineInfo - engine status information cache
 */
class EngineInfo : public Cache
{
public:
    enum EngineInfoType {
	ENGINE_TYPE	    = 1,
	ENGINE_PLUGINS      = 2,
	ENGINE_HANDLERS     = 3,
	ENGINE_MESSAGES     = 4,
	ENGINE_THREADS      = 5,
	ENGINE_WORKERS      = 6,
	ENGINE_MUTEXES      = 7,
	ENGINE_LOCKS	    = 8,
	ENGINE_SEMAPHORES   = 9,
	ENGINE_WAITING      = 10,
	ENGINE_RUNATTEMPT   = 11,
	ENGINE_NODENAME     = 12,
	ENGINE_STATE	    = 13,
	ENGINE_CALL_ACCEPT  = 14,
	ENGINE_UNEX_RESTART = 15,
    };
    // Constructor
    inline EngineInfo()
	: Cache("Monitor::engineInfo")
	{ }
    // Destructor
    inline ~EngineInfo()
	{ }
    // get information from the cached data. Reimplemented from Cache
    String getInfo(const String query, unsigned int index, TokenDict* dict);
private:
    // load data into this object from a engine.status message
    bool load();
    // dictionary for mapping engine status parameters
    static TokenDict s_engineInfo[];
};

/**
  * ModuleInfo - module information cache
  */
class ModuleInfo : public Cache
{
public:
    enum ModuleInfoType {
    	COUNT		    = 1,
	INDEX		    = 2,
	MODULE_NAME	    = 3,
	MODULE_TYPE	    = 4,
	MODULE_INFO	    = 5,
	MODULE_FORMAT       = 6,
    };
    // Constructor
    inline ModuleInfo()
	: Cache("Monitor::moduleInfo")
	{ }
    // Destructor
    inline ~ModuleInfo()
	{ }
private:
    // load data into this object from a engine.status message
    bool load();
    // dictionary for mapping engine status parameters
    static TokenDict s_moduleInfo[];
};

/**
 * DatabaseAccount
 * A container which holds status information about a single database account
 */
class DatabaseAccount : public GenObject
{
public:
    enum DbIndex {
	TOTAL_IDX	= 0,	// index for total number of queries
	FAILED_IDX	= 1,	// index for number of failed queries
	ERROR_IDX       = 2,     // index for number of queries returning with an error status
	TIME_IDX	= 3,	// index for time spent executing queries
	CONN_IDX	= 4,	// index for number of active connections
    };
    enum DbAlarms {
	TOTAL_ALARM 	= 0x1,
	FAILED_ALARM	= 0x2,
	ERROR_ALARM     = 0x4,
	EXEC_ALARM	= 0x8,
	CONNS_ALARM	= 0x10,
    };
    // enum for information type
    enum DbData {
	QueriesCount		= 1,
	FailedCount		= 2,
	ErrorsCount		= 3,
	ExecTime		= 4,
	TooManyAlrm		= 5,
	TooManyFailedAlrm       = 6,
	TooManyErrorAlrm	= 7,
	ExecTooLongAlrm		= 8,
	NoConnAlrm		= 9,
	TooManyAlrmCount	= 10,
	TooManyFailedAlrmCount  = 11,
	TooManyErrorAlrmCount   = 12,
	ExecTooLongAlrmCount    = 13,
	NoConnAlrmCount		= 14,
	MaxQueries		= 15,
	MaxFailedQueries	= 16,
	MaxErrorQueries		= 17,
	MaxExecTime		= 18,
	AccountName		= 19,
	AccountIndex		= 20,
    };
    // Constructor
    DatabaseAccount(const NamedList* cfg);
    // Destructor
    inline ~DatabaseAccount()
	{ }
    // reimplemented toString() method to make object searcheble in lists
    inline const String& toString() const
	{ return m_name; }
    // set the database entry index in the database account table
    inline void setIndex(unsigned int index)
	{ m_index = index; }
    // get this account's index
    inline unsigned int index()
	{ return m_index; }
    // update the internal data from the list received
    void update(const NamedList& info);
    // obtain data
    const String getInfo(unsigned int query);
    // reset internal data
    void reset();
    inline bool isCurrent()
	{ return m_isCurrent; }
    inline void setIsCurrent(bool current = true)
	{ m_isCurrent = current; }
    // update configuration for this direction
    void updateConfig(const NamedList* cfg);
private:
    // account name
    String m_name;
    // index
    unsigned int m_index;
    // counters for number of queries
    unsigned int m_dbCounters[ExecTime];
    // counters for previous interval counter values
    unsigned int m_prevDbCounters[ExecTime];
    // alarms set
    u_int16_t m_alarms;
    // alarm counters
    unsigned int m_alarmCounters[CONN_IDX + 1];
    // thresholds for triggering alarms
    unsigned int m_thresholds[CONN_IDX];
    // time at which internal data should be reset
    unsigned int m_resetTime;
    // time to hold on on current data
    unsigned int m_resetInterval;
    // flag if monitored account is current (i.e. false means this direction was removed from monitoring)
    bool m_isCurrent;
};

/**
 * DatabaseInfo - database information
 * A list of DatabaseAccounts
 */
class DatabaseInfo : public Cache
{
public:
    enum DbInfoType {
	Connections     = 1,
	FailedConns     = 2,
	Accounts	= 3,
    };
    // Constructor
    inline DatabaseInfo(bool monitored = false)
	: Cache("Monitor::dbInfo"), m_monitor(monitored)
	{ }
    // Destructor
    inline ~DatabaseInfo()
	{ m_table.clear(); }
    // create and add a new DatabaseAccount created from the received configuration
    void addDatabase(NamedList* cfg);
    // update the internal data from the received message
    void update(const Message& msg);
    // get the requested information
    String getInfo(const String& query, unsigned int& index, TokenDict* dict);
    // try to reset internal data of the table entries
    void reset();

    inline void setMonitorEnabled(bool enable = false)
	{ m_monitor = enable; }
    inline bool monitorEnable()
	{ return m_monitor; }
    // reconfigure
    void setConfigure(const NamedList* sect);
    // update database account after reinitialization
    void updateDatabaseAccounts();
private:
    static TokenDict s_databaseInfo[];
    bool load();
    // number of successful and failed connections to databases
    unsigned int m_connData[FailedConns];
    bool m_monitor;

    static String s_dbParam;
    static String s_totalParam;
    static String s_failedParam;
    static String s_errorParam;
    static String s_hasConnParam;
    static String s_timeParam;
};

/**
 *  RTPEntry. Container holding data about a single monitored RTP direction
 */
class RTPEntry : public GenObject
{
public:
    enum RTPInfoType {
	Count       = 1,
	Index       = 2,
	Direction   = 3,
	NoAudio     = 4,
	LostAudio   = 5,
	PktsLost    = 6,
	SyncLost    = 7,
	SeqLost     = 8,
	WrongSRC    = 9,
	WrongSSRC   = 10,
    };
    RTPEntry(String rtpDirection);
    ~RTPEntry();
    inline const String& toString() const
	{ return m_rtpDir; }
    // update the entry from the received information
    void update(const NamedList& nl);
    // reset internal data
    void reset();
    // set the index for this entry
    inline void setIndex(unsigned int index)
	{ m_index = index; }
    // get info from thins entry
    String getInfo(unsigned int query);
    // mapping dictionary for Monitor queries
    static TokenDict s_rtpInfo[];
    inline bool isCurrent()
	{ return m_isCurrent; }
    inline void setIsCurrent(bool current = true)
	{ m_isCurrent = current; }
private:
    // the RTP direction
    String m_rtpDir;
    // counters
    unsigned int m_counters[WrongSSRC - Direction];
    unsigned int m_index;
    // flag if monitored direction is current (i.e. false means this direction was removed from monitoring)
    bool m_isCurrent;
};

/**
 *  RTPTable. A list of RTPEntry
 */
class RTPTable : public GenObject
{
public:
    // Constructor
    inline RTPTable(const NamedList* cfg);
    // Destructor
    inline ~RTPTable()
	{ m_rtpEntries.clear(); }
    // update internal data
    void update(Message& msg);
    // get the answer to a query
    String getInfo(const String& query, const unsigned int& index);
    // reset internal data
    void reset();
    // check if the internal data should be reset
    inline bool shouldReset()
	{ return Time::secNow() >= m_resetTime; }
    void reconfigure(const NamedList* cfg);
private:
    // list of RTPEntry
    ObjList m_rtpEntries;
    Mutex m_rtpMtx;
    // interval for how long the data should be kept
    u_int64_t m_resetInterval;
    // time at which the data should be reset
    u_int64_t m_resetTime;
    // RTP monitored?
    bool m_monitor;
};

// A route entry which is monitored for quality of service values
class CallRouteQoS : public GenObject
{
public:
    enum CallStatus {
	ANSWERED    = 1,
	DELIVERED   = 2,
    };
    enum Indexes {
	CURRENT_IDX     = 0,
	PREVIOUS_IDX    = 1,
	TOTAL_IDX       = 2,
    };
    enum ALARMS {
	LOW_ASR     = 1,
	HIGH_ASR    = 2,
	LOW_NER     = 4,
    };
    enum QoSNotifs {
	ASR_LOW		= 1,
	ASR_HIGH	= 2,
	ASR_LOW_ALL	= 3,
	ASR_HIGH_ALL	= 4,
	NER_LOW		= 5,
	NER_LOW_ALL	= 6,
	ASR		= 7,
	NER		= 8,
	ASR_ALL		= 9,
	NER_ALL		= 10,
	MIN_ASR		= 11,
	MAX_ASR		= 12,
	MIN_NER		= 13,
	LOW_ASR_COUNT   = 14,
	HIGH_ASR_COUNT  = 15,
	LOW_ASR_ALL_COUNT   = 16,
	HIGH_ASR_ALL_COUNT  = 17,
	LOW_NER_COUNT       = 18,
	LOW_NER_ALL_COUNT   = 19,
	HANGUP		= 40,
	REJECT		= 41,
	BUSY		= 42,
	CANCELLED	= 43,
	NO_ANSWER	= 44,
	NO_ROUTE	= 45,
	NO_CONN		= 46,
	NO_AUTH		= 47,
	CONGESTION      = 48,
	NO_MEDIA	= 49,
	NO_CAUSE	= 50,
	HANGUP_ALL	= 60,
	REJECT_ALL	= 61,
	BUSY_ALL	= 62,
	CANCELLED_ALL	= 63,
	NO_ANSWER_ALL	= 64,
	NO_ROUTE_ALL	= 65,
	NO_CONN_ALL	= 66,
	NO_AUTH_ALL	= 67,
	CONGESTION_ALL  = 68,
	NO_MEDIA_ALL	= 69,
	NAME		= 80,
	INDEX		= 81,
    };
    // Constructor
    CallRouteQoS(const String direction, const NamedList* cfg = 0);
    // Destructor
    ~CallRouteQoS();
    // update the call counters and call end reason counters
    void update(int type = -1, int endReason = -1);
    // update the ASR and NER values
    void updateQoS();
    // reset the internal data
    void reset();
    // check if the given value has surpassed the given threshold and set the appropriate alarm
    void checkForAlarm(int& value, float hysteresis, u_int8_t& alarm, const int min, const int max, u_int8_t minAlarm, u_int8_t maxAlarm = 0xff);
    // is this route in a state of alarm
    bool alarm();
    // get the alarm
    const String alarmText();
    // send periodic notifications
    void sendNotifs(unsigned int index, bool reset = false);
    // get the response to the given query
    bool get(int query, String& result);
    // reimplemented toString() method to make the object searcheable in lists
    inline const String& toString() const
	{ return m_routeName; }
    // set the index of this object
    inline void setIndex(unsigned int index)
	{ m_index = index; }
    inline unsigned int index()
	{ return m_index; }
    inline bool isCurrent()
	{ return m_isCurrent; }
    inline void setIsCurrent(bool current = true)
	{ m_isCurrent = current; }
    // update configuration for this direction
    void updateConfig(const NamedList* cfg);
private:
    String m_routeName;
    // call hangup reasons counters
    unsigned int m_callCounters[NO_CAUSE - HANGUP];
    unsigned int m_callCountersAll[NO_CAUSE - HANGUP];

    unsigned int m_totalCalls[TOTAL_IDX + 1];		// total calls
    unsigned int m_answeredCalls[TOTAL_IDX + 1];     // total answered calls
    unsigned int m_delivCalls[TOTAL_IDX + 1];	// total delivered calls

    // alarm flags for avoiding sending multiple times the same alarm
    u_int8_t m_alarms;
    u_int8_t m_overallAlarms;

    // flags for keeping track if an alarm has been sent or not
    u_int8_t m_alarmsSent;
    u_int8_t m_overallAlarmsSent;

    // alarm thresholds
    int m_minASR;
    int m_maxASR;
    int m_minNER;

    // alarm counters
    unsigned int m_alarmCounters[NER_LOW_ALL + 1];
    // minimum number of calls before starting
    unsigned int m_minCalls;
    // index in the table
    unsigned int m_index;
    // flag if monitored direction is current (i.e. false means this direction was removed from monitoring)
    bool m_isCurrent;
};

/**
 * Class CallMonitor
 * Monitors number of calls, termination causes, computes statistic data
 */
class CallMonitor : public MessageHandler, public Thread
{
public:
     enum Indexes {
	IN_ASR_Idx	= 0,
	OUT_ASR_Idx	= 1,
	IN_NER_Idx	= 2,
	OUT_NER_Idx	= 3,
    };

    enum Queries {
	INCOMING_CALLS    = 9,
	OUTGOING_CALLS    = 10,
	ROUTES_COUNT	  = 11,
    };

    // constructor
    CallMonitor(const NamedList* sect, unsigned int priority = 100);
    virtual ~CallMonitor()
	{ }
    // inherited methods
    virtual bool received(Message& msg);
    virtual bool init();
    virtual void run();

    // obtain the value from the monitored data
    void get(const String& query, const int& index, String& result);

    // get the value of a call counter
    bool getCounter(int type, unsigned int& value);

    // send an alarm from a route
    void sendAlarmFrom(CallRouteQoS* route);

    // add a route to be monitored
    void addRoute(NamedList* sect);

    // reconfigure
    void setConfigure(const NamedList* sect);
    // update route after reinitialization
    void updateRoutes();

private:
    // interval at which notifications are sent
    unsigned int m_checkTime;
    // time at which notifications are sent
    unsigned int m_notifTime;
    // call counters
    unsigned int m_inCalls;
    unsigned int m_outCalls;
    // list of routes
    ObjList m_routes;
    Mutex m_routesMtx;
    bool m_first;
    // parameter on which to select routes from call.cdr message
    String m_routeParam;
    // Directions monitored?
    bool m_monitor;
    // configure mutex
    Mutex m_cfgMtx;
};

class SnmpMsgHandler;
class EngineStartHandler;
class AuthHandler;
class RegisterHandler;

/**
 * Class Monitor
 * Monitoring module
 */
class Monitor : public Module
{
public:
    // enum for notification categories
    enum Categories {
	CALL_MONITOR	= 1,
	DATABASE	= 2,
	ALARM_COUNTERS	= 3,
	ACTIVE_CALLS    = 4,
	PSTN		= 5,
	ENGINE		= 6,
	MODULE		= 7,
	AUTH_REQUESTS   = 8,
	REGISTER_REQUESTS = 9,
	INTERFACE	  = 10,
	SIP		  = 11,
	RTP		  = 12,
	TRUNKS		  = 13,
	LINKSETS	  = 14,
	LINKS		  = 15,
	IFACES		  = 16,
	ACCOUNTS	  = 17,
	MGCP		  = 18,
    };

     enum SigTypes {
	SS7_MTP3	= 1,
	TRUNK	        = 2,
	ISDN		= 3,
    };

    enum Cards {
	InterfaceDown = 1,
	InterfaceUp,
    };

    enum SigNotifs {
	TrunkDown   = 1,
	TrunkUp,
	LinksetDown,
	LinksetUp,
	LinkDown,
	LinkUp,
	IsdnQ921Down,
	IsdnQ921Up,
    };

    enum SipNotifs {
        TransactTimedOut = 1,
	FailedAuths,
	ByesTimedOut,
	GWTimeout,
	GWUp,
	DeletesTimedOut,
    };
    Monitor();
    virtual ~Monitor();
    //inherited methods
    virtual void initialize();
    virtual bool received(Message& msg, int id);
    bool unload();

    // handle module.update messages
    void update(Message& msg);
    // read configuration file
    void readConfig(const Configuration& cfg);
    // build and send SS7 notifications
    void sendSigNotifs(Message& msg);
    // build and send physical interface notifications
    void sendCardNotifs(Message& msg);
    // handle MGCP & SIP status notifications
    void checkNotifs(Message& msg, unsigned int type);
    // build a notification message
    void sendTrap(const String& trap, const String& value, unsigned int index = 0,
	const char* text = 0);
    // send multiple notifications at once
    void sendTraps(const NamedList& traps);
    // handle a monitor.query message
    bool solveQuery(Message& msg);
    // update monitored SIP gateway information
    void handleChanHangup(const String& address, int& cause);
    bool verifyGateway(const String& address);
    // obtain SIP/MGCP transactions info
    String getTransactionsInfo(const String& query, const int who);
private:
    // message handlers
    MsgUpdateHandler* m_msgUpdateHandler;
    SnmpMsgHandler* m_snmpMsgHandler;
    HangupHandler* m_hangupHandler;
    EngineStartHandler* m_startHandler;
    CallMonitor* m_callMonitor;
    AuthHandler* m_authHandler;
    RegisterHandler* m_registerHandler;
    bool m_init;
    bool m_newTraps;
    // list of monitored SIP gateways and timed out gateways
    ObjList* m_sipMonitoredGws;
    ObjList m_timedOutGws;

    // flags if certain monitored information should be passed along in form of notifications
    bool m_trunkMon;
    bool m_linksetMon;
    bool m_linkMon;
    bool m_interfaceMon;
    bool m_isdnMon;
    // caches
    ActiveCallsInfo* m_activeCallsCache;
    TrunkInfo* m_trunkInfo;
    EngineInfo* m_engineInfo;
    ModuleInfo* m_moduleInfo;
    DatabaseInfo* m_dbInfo;
    RTPTable* m_rtpInfo;

    LinksetInfo* m_linksetInfo;
    LinkInfo* m_linkInfo;
    InterfaceInfo* m_ifaceInfo;
    AccountsInfo* m_accountsInfo;
};

static int s_yateRun = 0;
static int s_yateRunAlarm = 0;
static int s_alarmThreshold = DebugNote;
static String s_nodeState = "";
static double s_qosHysteresisFactor = 2.0;

MGCPInfo s_mgcpInfo = { {0, 0, false}, {0, 0, false}, 0, 0, false};
static SIPInfo s_sipInfo = { {0, 0, false}, {0, 0, false},  {0, 0, false}, 0, 0};

static TokenDict s_modules[] = {
    {"mysqldb",	    Monitor::DATABASE},
    {"pgsqldb",     Monitor::DATABASE},
    {"sig",	    Monitor::PSTN},
    {"wanpipe",     Monitor::INTERFACE},
    {"zaptel",      Monitor::INTERFACE},
    {"Tdm",	    Monitor::INTERFACE},
    {"sip",	    Monitor::SIP},
    {"yrtp",	    Monitor::RTP},
    {"mgcpca",      Monitor::MGCP},
    {0,0}
};

static TokenDict s_categories[] = {
    // database info
    {"databaseCount",			Monitor::DATABASE},
    {"databaseIndex",			Monitor::DATABASE},
    {"databaseAccount",			Monitor::DATABASE},
    {"queriesCount",			Monitor::DATABASE},
    {"failedQueries",			Monitor::DATABASE},
    {"errorQueries",			Monitor::DATABASE},
    {"queryExecTime",			Monitor::DATABASE},
    {"successfulConnections",		Monitor::DATABASE},
    {"failedConnections",		Monitor::DATABASE},
    // database alarm counters
    {"tooManyQueriesAlarms",		Monitor::DATABASE},
    {"tooManyFailedQueriesAlarms",	Monitor::DATABASE},
    {"tooManyErrorQueriesAlarms",	Monitor::DATABASE},
    {"queryExecTooLongAlarms",		Monitor::DATABASE},
    {"noConnectionAlarms",		Monitor::DATABASE},
    // database thresholds
    {"queriesCountThreshold",		Monitor::DATABASE},
    {"failedQueriesThreshold",		Monitor::DATABASE},
    {"errorQueriesThreshold",		Monitor::DATABASE},
    {"queryExecTimeThreshold",		Monitor::DATABASE},
     // QOS
    {"qosDirectionsCount",		Monitor::CALL_MONITOR},
    {"qosEntryIndex",			Monitor::CALL_MONITOR},
    {"qosEntryDirection",		Monitor::CALL_MONITOR},
    {"lowASRThreshold",			Monitor::CALL_MONITOR},
    {"highASRThreshold",		Monitor::CALL_MONITOR},
    {"currentASR",			Monitor::CALL_MONITOR},
    {"overallASR",			Monitor::CALL_MONITOR},
    {"lowNERThreshold",			Monitor::CALL_MONITOR},
    {"currentNER",			Monitor::CALL_MONITOR},
    {"overallNER",			Monitor::CALL_MONITOR},
    // QOS alarm counters
    {"currentLowASRAlarmCount",		Monitor::CALL_MONITOR},
    {"overallLowASRAlarmCount",		Monitor::CALL_MONITOR},
    {"currentHighASRAlarmCount",	Monitor::CALL_MONITOR},
    {"overallHighASRAlarmCount",	Monitor::CALL_MONITOR},
    {"currentLowNERAlarmCount",		Monitor::CALL_MONITOR},
    {"overallLowNERAlarmCount",		Monitor::CALL_MONITOR},
    // call counters
    {"incomingCalls",			Monitor::CALL_MONITOR},
    {"outgoingCalls",			Monitor::CALL_MONITOR},

    {"currentHangupEndCause",		Monitor::CALL_MONITOR},
    {"currentBusyEndCause",		Monitor::CALL_MONITOR},
    {"currentRejectedEndCause",		Monitor::CALL_MONITOR},
    {"currentCancelledEndCause",	Monitor::CALL_MONITOR},
    {"currentNoAnswerEndCause",		Monitor::CALL_MONITOR},
    {"currentNoRouteEndCause",		Monitor::CALL_MONITOR},
    {"currentNoConnectionEndCause",	Monitor::CALL_MONITOR},
    {"currentNoAuthEndCause",		Monitor::CALL_MONITOR},
    {"currentCongestionEndCause",       Monitor::CALL_MONITOR},
    {"currentNoMediaEndCause",		Monitor::CALL_MONITOR},

    {"overallHangupEndCause",		Monitor::CALL_MONITOR},
    {"overallBusyEndCause",		Monitor::CALL_MONITOR},
    {"overallRejectedEndCause",		Monitor::CALL_MONITOR},
    {"overallCancelledEndCause",	Monitor::CALL_MONITOR},
    {"overallNoAnswerEndCause",		Monitor::CALL_MONITOR},
    {"overallNoRouteEndCause",		Monitor::CALL_MONITOR},
    {"overallNoConnectionEndCause",	Monitor::CALL_MONITOR},
    {"overallNoAuthEndCause",		Monitor::CALL_MONITOR},
    {"overallCongestionEndCause",       Monitor::CALL_MONITOR},
    {"overallNoMediaEndCause",		Monitor::CALL_MONITOR},

    // connections info
    // linksets
    {"linksetCount",		Monitor::LINKSETS},
    {"linksetIndex",		Monitor::LINKSETS},
    {"linksetID",		Monitor::LINKSETS},
    {"linksetType",		Monitor::LINKSETS},
    {"linksetStatus",		Monitor::LINKSETS},
    {"linksetDownAlarms",       Monitor::LINKSETS},
    // links
    {"linkCount",		Monitor::LINKS},
    {"linkIndex",		Monitor::LINKS},
    {"linkID",			Monitor::LINKS},
    {"linkType",		Monitor::LINKS},
    {"linkStatus",		Monitor::LINKS},
    {"linkDownAlarms",		Monitor::LINKS},
    {"linkUptime",		Monitor::LINKS},
    // interfaces
    {"interfacesCount",		Monitor::IFACES},
    {"interfaceIndex",		Monitor::IFACES},
    {"interfaceID",		Monitor::IFACES},
    {"interfaceStatus",		Monitor::IFACES},
    {"interfaceDownAlarms",     Monitor::IFACES},
    // accounts
    {"accountsCount",		Monitor::ACCOUNTS},
    {"accountIndex",		Monitor::ACCOUNTS},
    {"accountID",		Monitor::ACCOUNTS},
    {"accountStatus",		Monitor::ACCOUNTS},
    {"accountProtocol",		Monitor::ACCOUNTS},
    {"accountUsername",		Monitor::ACCOUNTS},
    // active calls info
    {"activeCallsCount",	Monitor::ACTIVE_CALLS},
    {"callEntryIndex",		Monitor::ACTIVE_CALLS},
    {"callEntryID",		Monitor::ACTIVE_CALLS},
    {"callEntryStatus",		Monitor::ACTIVE_CALLS},
    {"callEntryCaller",		Monitor::ACTIVE_CALLS},
    {"callEntryCalled",		Monitor::ACTIVE_CALLS},
    {"callEntryPeerChan",	Monitor::ACTIVE_CALLS},
    {"callEntryDuration",       Monitor::ACTIVE_CALLS},
    // trunk info
    {"trunksCount",		Monitor::TRUNKS},
    {"trunkIndex",		Monitor::TRUNKS},
    {"trunkID",			Monitor::TRUNKS},
    {"trunkType",		Monitor::TRUNKS},
    {"trunkCircuitCount",	Monitor::TRUNKS},
    {"trunkCurrentCallsCount",	Monitor::TRUNKS},
    {"trunkDownAlarms",		Monitor::TRUNKS},
    {"trunkCircuitsLocked",	Monitor::TRUNKS},
    {"trunkCircuitsIdle",	Monitor::TRUNKS},
    // engine info
    {"plugins",			Monitor::ENGINE},
    {"handlers",		Monitor::ENGINE},
    {"messages",		Monitor::ENGINE},
    {"threads",			Monitor::ENGINE},
    {"workers",			Monitor::ENGINE},
    {"mutexes",			Monitor::ENGINE},
    {"locks",			Monitor::ENGINE},
    {"semaphores",		Monitor::ENGINE},
    {"waitingSemaphores",       Monitor::ENGINE},
    {"acceptStatus",		Monitor::ENGINE},
    {"unexpectedRestart",       Monitor::ENGINE},
    // node info
    {"runAttempt",		Monitor::ENGINE},
    {"name",			Monitor::ENGINE},
    {"state",			Monitor::ENGINE},
    // module info
    {"moduleCount",		Monitor::MODULE},
    {"moduleIndex",		Monitor::MODULE},
    {"moduleName",		Monitor::MODULE},
    {"moduleType",		Monitor::MODULE},
    {"moduleExtra",		Monitor::MODULE},
    // request stats
    {"authenticationRequests",  Monitor::AUTH_REQUESTS},
    {"registerRequests",	Monitor::REGISTER_REQUESTS},
    // rtp stats
    {"rtpDirectionsCount",      Monitor::RTP},
    {"rtpEntryIndex",		Monitor::RTP},
    {"rtpDirection",		Monitor::RTP},
    {"noAudioCounter",		Monitor::RTP},
    {"lostAudioCounter",	Monitor::RTP},
    {"packetsLost",		Monitor::RTP},
    {"syncLost",		Monitor::RTP},
    {"sequenceNumberLost",      Monitor::RTP},
    {"wrongSRC",		Monitor::RTP},
    {"wrongSSRC",		Monitor::RTP},
    // sip stats
    {"transactionsTimedOut",    Monitor::SIP},
    {"failedAuths",		Monitor::SIP},
    {"byesTimedOut",		Monitor::SIP},
    // mgcp stats
    {"mgcpTransactionsTimedOut",    Monitor::MGCP},
    {"deleteTransactionsTimedOut",  Monitor::MGCP},
    {0,0}
};

static TokenDict s_callQualityQueries[] = {
    // alarms
    {"currentLowASR",		    CallRouteQoS::ASR_LOW},
    {"overallLowASR",		    CallRouteQoS::ASR_LOW_ALL},
    {"currentHighASR",		    CallRouteQoS::ASR_HIGH},
    {"overallHighASR",		    CallRouteQoS::ASR_HIGH_ALL},
    {"currentLowNER",		    CallRouteQoS::NER_LOW},
    {"overallLowNER",		    CallRouteQoS::NER_LOW_ALL},
    {"qosEntryDirection",	    CallRouteQoS::NAME},
    {"qosEntryIndex",		    CallRouteQoS::INDEX},
    // notifications
    {"currentASR",		    CallRouteQoS::ASR},
    {"overallASR",		    CallRouteQoS::ASR_ALL},
    {"currentNER",		    CallRouteQoS::NER},
    {"overallNER",		    CallRouteQoS::NER_ALL},
    // end cause counters
    {"currentHangupEndCause",	    CallRouteQoS::HANGUP},
    {"currentBusyEndCause",	    CallRouteQoS::BUSY},
    {"currentRejectedEndCause",     CallRouteQoS::REJECT},
    {"currentCancelledEndCause",    CallRouteQoS::CANCELLED},
    {"currentNoAnswerEndCause",	    CallRouteQoS::NO_ANSWER},
    {"currentNoRouteEndCause",	    CallRouteQoS::NO_ROUTE},
    {"currentNoConnectionEndCause", CallRouteQoS::NO_CONN},
    {"currentNoAuthEndCause",       CallRouteQoS::NO_AUTH},
    {"currentCongestionEndCause",   CallRouteQoS::CONGESTION},
    {"currentNoMediaEndCause",      CallRouteQoS::NO_MEDIA},

    {"overallHangupEndCause",       CallRouteQoS::HANGUP_ALL},
    {"overallBusyEndCause",	    CallRouteQoS::BUSY_ALL},
    {"overallRejectedEndCause",     CallRouteQoS::REJECT_ALL},
    {"overallCancelledEndCause",    CallRouteQoS::CANCELLED_ALL},
    {"overallNoAnswerEndCause",     CallRouteQoS::NO_ANSWER_ALL},
    {"overallNoRouteEndCause",	    CallRouteQoS::NO_ROUTE_ALL},
    {"overallNoConnectionEndCause", CallRouteQoS::NO_CONN_ALL},
    {"overallNoAuthEndCause",	    CallRouteQoS::NO_AUTH_ALL},
    {"overallCongestionEndCause",   CallRouteQoS::CONGESTION_ALL},
    {"overallNoMediaEndCause",      CallRouteQoS::NO_MEDIA_ALL},
    // thresholds
    {"lowASRThreshold",		    CallRouteQoS::MIN_ASR},
    {"highASRThreshold",	    CallRouteQoS::MAX_ASR},
    {"lowNERThreshold",		    CallRouteQoS::MIN_NER},
    // alarm counters
    {"currentLowASRAlarmCount",	    CallRouteQoS::LOW_ASR_COUNT},
    {"currentHighASRAlarmCount",    CallRouteQoS::HIGH_ASR_COUNT},
    {"overallLowASRAlarmCount",	    CallRouteQoS::LOW_ASR_ALL_COUNT},
    {"overallHighASRAlarmCount",    CallRouteQoS::HIGH_ASR_ALL_COUNT},
    {"currentLowNERAlarmCount",     CallRouteQoS::LOW_NER_COUNT},
    {"overallLowNERAlarmCount",	    CallRouteQoS::LOW_NER_ALL_COUNT},
    {0,0}
};
// call end reasons
static TokenDict s_endReasons[] = {
    {"User hangup",               CallRouteQoS::HANGUP},
    {"Rejected",                  CallRouteQoS::REJECT},
    {"rejected",                  CallRouteQoS::REJECT},
    {"User busy",                 CallRouteQoS::BUSY},
    {"busy",                      CallRouteQoS::BUSY},
    {"Request Terminated",        CallRouteQoS::NO_ANSWER},
    {"noanswer",                  CallRouteQoS::NO_ANSWER},
    {"No route to call target",   CallRouteQoS::NO_ROUTE},
    {"noroute",                   CallRouteQoS::NO_ROUTE},
    {"Service Unavailable",       CallRouteQoS::NO_CONN},
    {"noconn",                    CallRouteQoS::NO_CONN},
    {"service-unavailable",       CallRouteQoS::NO_CONN},
    {"Unauthorized",              CallRouteQoS::NO_AUTH},
    {"noauth",                    CallRouteQoS::NO_AUTH},
    {"Cancelled",                 CallRouteQoS::CANCELLED},
    {"Congestion",                CallRouteQoS::CONGESTION},
    {"congestion",                CallRouteQoS::CONGESTION},
    {"Unsupported Media Type",    CallRouteQoS::NO_MEDIA},
    {"nomedia",                   CallRouteQoS::NO_MEDIA},
    {0,0}
};

static TokenDict s_callCounterQueries[] = {
    {"incomingCalls",		CallMonitor::INCOMING_CALLS},
    {"outgoingCalls",		CallMonitor::OUTGOING_CALLS},
    {"qosDirectionsCount",      CallMonitor::ROUTES_COUNT},
    {0,0}
};

static TokenDict s_activeCallInfo[] = {
    {"activeCallsCount",    ActiveCallsInfo::COUNT},
    {"callEntryID",	    ActiveCallsInfo::ID},
    {"callEntryIndex",	    ActiveCallsInfo::INDEX},
    {"callEntryID",	    ActiveCallsInfo::ID},
    {"callEntryStatus",	    ActiveCallsInfo::STATUS},
    {"callEntryCaller",     ActiveCallsInfo::CALLER},
    {"callEntryCalled",     ActiveCallsInfo::CALLED},
    {"callEntryPeerChan",   ActiveCallsInfo::PEER},
    {"callEntryDuration",   ActiveCallsInfo::DURATION},
    {0,0}
};

TokenDict TrunkInfo::s_trunkInfo[] = {
    {"trunksCount",		TrunkInfo::COUNT},
    {"trunkIndex",		TrunkInfo::INDEX},
    {"trunkID",			TrunkInfo::ID},
    {"trunkType",		TrunkInfo::TYPE},
    {"trunkCircuitCount",	TrunkInfo::CIRCUITS},
    {"trunkCurrentCallsCount",	TrunkInfo::CALLS},
    {"trunkDownAlarms",		TrunkInfo::ALARMS_COUNT},
    {"trunkCircuitsLocked",	TrunkInfo::LOCKED},
    {"trunkCircuitsIdle",	TrunkInfo::IDLE},
    {0,0}
};

TokenDict TrunkInfo::s_trunkStatus[] = {
    {"module",      TrunkInfo::SKIP},
    {"trunk",       TrunkInfo::ID},
    {"type",	    TrunkInfo::TYPE},
    {"circuits",    TrunkInfo::CIRCUITS},
    {"calls",       TrunkInfo::CALLS},
    {"status",      TrunkInfo::STATUS},
    {"locked",      TrunkInfo::LOCKED},
    {"idle",        TrunkInfo::IDLE},
    {0,0}
};

static TokenDict s_accountInfo[] = {
    {"accountsCount",   AccountsInfo::COUNT},
    {"accountIndex",    AccountsInfo::INDEX},
    {"accountID",       AccountsInfo::ID},
    {"accountStatus",   AccountsInfo::STATUS},
    {"accountProtocol", AccountsInfo::PROTO},
    {"accountUsername", AccountsInfo::USERNAME},
    {0,0}
};

static TokenDict s_engineQuery[] = {

    {"plugins",		    EngineInfo::ENGINE_PLUGINS},
    {"handlers",	    EngineInfo::ENGINE_HANDLERS},
    {"messages",	    EngineInfo::ENGINE_MESSAGES},
    {"threads",		    EngineInfo::ENGINE_THREADS},
    {"workers",		    EngineInfo::ENGINE_WORKERS},
    {"mutexes",		    EngineInfo::ENGINE_MUTEXES},
    {"locks",		    EngineInfo::ENGINE_LOCKS},
    {"semaphores",	    EngineInfo::ENGINE_SEMAPHORES},
    {"waitingSemaphores",   EngineInfo::ENGINE_WAITING},
    {"acceptStatus",	    EngineInfo::ENGINE_CALL_ACCEPT},
    // node info
    {"runAttempt",	    EngineInfo::ENGINE_RUNATTEMPT},
    {"name",		    EngineInfo::ENGINE_NODENAME},
    {"state",		    EngineInfo::ENGINE_STATE},
    {"unexpectedRestart",   EngineInfo::ENGINE_UNEX_RESTART},
    {0,0}
};

TokenDict EngineInfo::s_engineInfo[] = {
    {"type",                EngineInfo::ENGINE_TYPE},
    {"plugins",             EngineInfo::ENGINE_PLUGINS},
    {"handlers",            EngineInfo::ENGINE_HANDLERS},
    {"messages",            EngineInfo::ENGINE_MESSAGES},
    {"threads",             EngineInfo::ENGINE_THREADS},
    {"workers",             EngineInfo::ENGINE_WORKERS},
    {"mutexes",             EngineInfo::ENGINE_MUTEXES},
    {"locks",               EngineInfo::ENGINE_LOCKS},
    {"semaphores",          EngineInfo::ENGINE_SEMAPHORES},
    {"waiting",             EngineInfo::ENGINE_WAITING},
    {"runattempt",          EngineInfo::ENGINE_RUNATTEMPT},
    {"nodename",            EngineInfo::ENGINE_NODENAME},
    {"acceptcalls",         EngineInfo::ENGINE_CALL_ACCEPT},
    {"lastsignal",          EngineInfo::ENGINE_UNEX_RESTART},
    {0,0}
};

TokenDict ModuleInfo::s_moduleInfo[] = {
    {"name",	    ModuleInfo::MODULE_NAME},
    {"type",	    ModuleInfo::MODULE_TYPE},
    {"format",      ModuleInfo::MODULE_FORMAT},
    {0,0}
};

static TokenDict s_moduleQuery[] = {
    {"moduleCount",     ModuleInfo::COUNT},
    {"moduleIndex",     ModuleInfo::INDEX},
    {"moduleName",      ModuleInfo::MODULE_NAME},
    {"moduleType",      ModuleInfo::MODULE_TYPE},
    {"moduleExtra",     ModuleInfo::MODULE_INFO},
    {0,0}
};

TokenDict DatabaseInfo::s_databaseInfo[] = {
    {"conns",	   DatabaseInfo::Connections},
    {"failed",     DatabaseInfo::FailedConns},
    {0,0}
};

static TokenDict s_databaseQuery[] = {
    {"successfulConnections",   DatabaseInfo::Connections},
    {"failedConnections",       DatabaseInfo::FailedConns},
    {"databaseCount",		DatabaseInfo::Accounts},
    {0,0}
};

static TokenDict s_dbAccountQueries[] = {
    {"databaseIndex",			DatabaseAccount::AccountIndex},
    {"databaseAccount",			DatabaseAccount::AccountName},
    {"queriesCount",			DatabaseAccount::QueriesCount},
    {"failedQueries",			DatabaseAccount::FailedCount},
    {"errorQueries",			DatabaseAccount::ErrorsCount},
    {"queryExecTime",			DatabaseAccount::ExecTime},
    // alarms counters
    {"tooManyQueriesAlarms",		DatabaseAccount::TooManyAlrmCount},
    {"tooManyFailedQueriesAlarms",	DatabaseAccount::TooManyFailedAlrmCount},
    {"tooManyErrorQueriesAlarms",	DatabaseAccount::TooManyErrorAlrmCount},
    {"queryExecTooLongAlarms",		DatabaseAccount::ExecTooLongAlrmCount},
    {"noConnectionAlarms",		DatabaseAccount::NoConnAlrmCount},
    // database thresholds
    {"queriesCountThreshold",		DatabaseAccount::MaxQueries},
    {"failedQueriesThreshold",		DatabaseAccount::MaxFailedQueries},
    {"errorQueriesThreshold",		DatabaseAccount::MaxErrorQueries},
    {"queryExecTimeThreshold",		DatabaseAccount::MaxExecTime},
    // alarm
    {"tooManyQueries",			DatabaseAccount::TooManyAlrm},
    {"tooManyFailedQueries",		DatabaseAccount::TooManyFailedAlrm},
    {"tooManyErrorQueries",		DatabaseAccount::TooManyErrorAlrm},
    {"queryExecTimeTooLong",		DatabaseAccount::ExecTooLongAlrm},
    {"noConnection",			DatabaseAccount::NoConnAlrm},
    {0,0}
};

static TokenDict s_dbAccountInfo[] = {
    {"maxqueries",	    DatabaseAccount::MaxQueries},
    {"maxfailed",	    DatabaseAccount::MaxFailedQueries},
    {"maxerrors",	    DatabaseAccount::MaxErrorQueries},
    {"maxtimeperquery",	    DatabaseAccount::MaxExecTime},
    {"total",		    DatabaseAccount::TOTAL_IDX},
    {"failed",		    DatabaseAccount::FAILED_IDX},
    {"errorred",	    DatabaseAccount::ERROR_IDX},
    {"querytime",	    DatabaseAccount::TIME_IDX},
    {"hasconn",		    DatabaseAccount::CONN_IDX},
    {0,-1}
};

TokenDict RTPEntry::s_rtpInfo[] = {
    {"remoteip",    RTPEntry::Direction},
    {"noaudio",     RTPEntry::NoAudio},
    {"lostaudio",   RTPEntry::LostAudio},
    {"lostpkts",    RTPEntry::PktsLost},
    {"synclost",    RTPEntry::SyncLost},
    {"seqslost",    RTPEntry::SeqLost},
    {"wrongsrc",    RTPEntry::WrongSRC},
    {"wrongssrc",   RTPEntry::WrongSSRC},
    {0,0}
};

static TokenDict s_rtpQuery[] = {
    {"rtpDirectionsCount",  RTPEntry::Count},
    {"rtpEntryIndex",       RTPEntry::Index},
    {"rtpDirection",	    RTPEntry::Direction},
    {"noAudioCounter",      RTPEntry::NoAudio},
    {"lostAudioCounter",    RTPEntry::LostAudio},
    {"packetsLost",	    RTPEntry::PktsLost},
    {"syncLost",	    RTPEntry::SyncLost},
    {"sequenceNumberLost",  RTPEntry::SeqLost},
    {"wrongSRC",	    RTPEntry::WrongSRC},
    {"wrongSSRC",	    RTPEntry::WrongSSRC},
    {0,0}
};

static TokenDict s_sigTypes[] = {
    {"ss7-mtp3", 	Monitor::SS7_MTP3},
    {"trunk",		Monitor::TRUNK},
    {"isdn-q921", 	Monitor::ISDN},
    {0,0}
};

static TokenDict s_sipNotifs[] = {
    {"transactionsTimedOut",    Monitor::TransactTimedOut},
    {"failedAuths",		Monitor::FailedAuths},
    {"byesTimedOut",		Monitor::ByesTimedOut},
    {"gatewayTimeout",		Monitor::GWTimeout},
    {"gatewayUp",		Monitor::GWUp},
    {0,0}
};

static TokenDict s_mgcpNotifs[] = {
    {"mgcpTransactionsTimedOut",    Monitor::TransactTimedOut},
    {"deleteTransactionsTimedOut",  Monitor::DeletesTimedOut},
    {"mgcpGatewayTimedOut",	    Monitor::GWTimeout},
    {"mgcpGatewayUp",		    Monitor::GWUp},
    {0,0}
};

static TokenDict s_sigNotifs[] = {
    {"trunkDown",       Monitor::TrunkDown},
    {"trunkUp",		Monitor::TrunkUp},
    {"linksetDown",     Monitor::LinksetDown},
    {"linksetUp",       Monitor::LinksetUp},
    {"linkUp",		Monitor::LinkUp},
    {"linkDown",	Monitor::LinkDown},
    {"linkUp",		Monitor::LinkUp},
    {"isdnQ921Down",    Monitor::IsdnQ921Down},
    {"isdnQ921Up",      Monitor::IsdnQ921Up},
    {0,0}
};

static TokenDict s_cardInfo[] = {
    {"interfaceDown",       Monitor::InterfaceDown},
    {"interfaceUp",	    Monitor::InterfaceUp},
    {0,0}
};

static TokenDict s_cardNotifs[] = {
    {"interfaceDown",       Monitor::InterfaceDown},
    {"interfaceUp",	    Monitor::InterfaceUp},
    {0,0}
};

TokenDict LinksetInfo::s_linksetInfo[] = {
    {"linksetCount",	    LinksetInfo::COUNT},
    {"linksetIndex",	    LinksetInfo::INDEX},
    {"linksetID",	    LinksetInfo::ID},
    {"linksetType",	    LinksetInfo::TYPE},
    {"linksetStatus",       LinksetInfo::STATUS},
    {"linksetDownAlarms",   LinksetInfo::ALARMS_COUNT},
    {0,0}
};

TokenDict LinksetInfo::s_linksetStatus[] = {
    {"module",      LinksetInfo::SKIP},
    {"component",   LinksetInfo::ID},
    {"type",	    LinksetInfo::TYPE},
    {"status",      LinksetInfo::STATUS},
    {0,0}
};

TokenDict LinkInfo::s_linkInfo[] = {
    {"linkCount",       LinkInfo::COUNT},
    {"linkIndex",       LinkInfo::INDEX},
    {"linkID",		LinkInfo::ID},
    {"linkType",	LinkInfo::TYPE},
    {"linkStatus",      LinkInfo::STATUS},
    {"linkDownAlarms",  LinkInfo::ALARMS_COUNT},
    {"linkUptime",      LinkInfo::UPTIME},
    {0,0}
};

TokenDict InterfaceInfo::s_ifacesInfo[] = {
    {"interfacesCount",     InterfaceInfo::COUNT},
    {"interfaceIndex",      InterfaceInfo::INDEX},
    {"interfaceID",	    InterfaceInfo::ID},
    {"interfaceStatus",     InterfaceInfo::STATUS},
    {"interfaceDownAlarms", InterfaceInfo::ALARMS_COUNT},
    {0,0}
};

String DatabaseInfo::s_dbParam = "database.";
String DatabaseInfo::s_totalParam = "total.";
String DatabaseInfo::s_failedParam = "failed.";
String DatabaseInfo::s_errorParam = "errorred.";
String DatabaseInfo::s_hasConnParam = "hasconn.";
String DatabaseInfo::s_timeParam = "querytime.";

INIT_PLUGIN(Monitor);

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow && !__plugin.unload())
	return false;
    return true;
}


/**
 * Class MsgUpdateHandler
 * Class for handling a "module.update" message
 */
class MsgUpdateHandler : public MessageHandler
{
public:
    inline MsgUpdateHandler(unsigned int priority = 100)
	: MessageHandler("module.update",priority,__plugin.name())
	{ }
    virtual ~MsgUpdateHandler()
	{ }
    virtual bool received(Message& msg);
};

/**
 * Class SnmpMsgHandler
 * Class for handling a "monitor.query" message, message used for obtaining information from the monitor
 */
class SnmpMsgHandler : public MessageHandler
{
public:
    inline SnmpMsgHandler(unsigned int priority = 100)
	: MessageHandler("monitor.query",priority,__plugin.name())
	{ }
    virtual ~SnmpMsgHandler()
	{ }
    virtual bool received(Message& msg);
};

/**
 * Class HangupHandler
 * Handler for "chan.hangup" message"
 */
class HangupHandler : public MessageHandler
{
public:
    inline HangupHandler(unsigned int priority = 100)
	: MessageHandler("chan.hangup",priority,__plugin.name())
	{ }
    virtual ~HangupHandler()
	{ }
    virtual bool received(Message& msg);
};

/**
 * Class EngineStartHandler
 * Handler for "engine.start" message
 */
class EngineStartHandler : public MessageHandler
{
public:
    inline EngineStartHandler(unsigned int priority = 100)
	: MessageHandler("engine.start",priority,__plugin.name())
	{ }
    virtual ~EngineStartHandler()
	{ }
    virtual bool received(Message& msg);
};

/**
 * Class AuthHandler
 * Handler for a "user.auth" message. It counts the number of authentication requests
 */
class AuthHandler : public MessageHandler
{
public:
    inline AuthHandler()
	: MessageHandler("user.auth",1,__plugin.name()),
	  m_count(0)
	{ }
    virtual ~AuthHandler()
	{ }
    virtual bool received(Message& msg);
    // return the number of authentication requests
    inline unsigned int getCount()
	{ return m_count; }
private:
    unsigned int m_count;
};

/**
 * Class RegisterHandler
 * Handler for a "user.register" message. It counts the number of register requests
 */
class RegisterHandler : public MessageHandler
{
public:
    inline RegisterHandler()
	: MessageHandler("user.register",1,__plugin.name()),
	  m_count(0)
	{ }
    virtual ~RegisterHandler()
	{ }
    virtual bool received(Message& msg);
    // return the count
    inline unsigned int getCount()
	{ return m_count; }
private:
    unsigned int m_count;
};


// helper function to get rid of new line characters
static void cutNewLine(String& line) {
    if (line.endsWith("\n"))
	line = line.substr(0,line.length() - 1);
    if (line.endsWith("\r"))
	line = line.substr(0,line.length() - 1);
}

// callback for engine alarm hook
static void alarmCallback(const char* message, int level, const char* component, const char* info)
{
    if (TelEngine::null(component) || TelEngine::null(message))
	return;
    const char* lvl = debugLevelName(level);
    if (TelEngine::null(lvl))
	return;
    TempObjectCounter cnt(__plugin.objectsCounter());
    Message* msg = new Message("module.update");
    msg->addParam("module",__plugin.name());
    msg->addParam("level",String(level));
    msg->addParam("from",component,false);
    msg->addParam("text",message,false);
    msg->addParam("info",info,false);
    Engine::enqueue(msg);
    if ((s_alarmThreshold >= DebugFail) && (level <= s_alarmThreshold)) {
	msg = new Message("monitor.notify",0,true);
	msg->addParam("notify","genericAlarm");
	msg->addParam("notify.0","alarmSource");
	msg->addParam("value.0",component);
	msg->addParam("notify.1","alarmLevel");
	msg->addParam("value.1",lvl);
	msg->addParam("notify.2","alarmText");
	msg->addParam("value.2",message);
	if (!TelEngine::null(info)) {
	    msg->addParam("notify.3","alarmInfo");
	    msg->addParam("value.3",info);
	}
	Engine::enqueue(msg);
    }
}


/**
  * MsgUpdateHandler
  */
bool MsgUpdateHandler::received(Message& msg)
{
    DDebug(__plugin.name(),DebugAll,"MsgUpdateHandler::received()");
    __plugin.update(msg);
    return true;
}

/**
  * SnmpMsgHandler
  */
bool SnmpMsgHandler::received(Message& msg)
{
    DDebug(__plugin.name(),DebugAll,"SnmpMsgHandler::received()");
    return __plugin.solveQuery(msg);
}

/**
  * HangupHandler
  */
bool HangupHandler::received(Message& msg)
{
    DDebug(__plugin.name(),DebugAll,"HangupHandler::received()");
    String status = msg.getValue("status","");
    String address = msg.getValue("address","");
    int cause = msg.getIntValue("cause_sip",0);
    if (status == "outgoing" && cause && !address.null())
	__plugin.handleChanHangup(address,cause);
    if (status == "ringing" && !address.null())
	__plugin.verifyGateway(address);
    return false;
}

/**
  * EngineStartHandler
  */
bool EngineStartHandler::received(Message& msg)
{
    DDebug(__plugin.name(),DebugAll,"EngineStartHandler::received()");
    s_yateRun = Engine::runParams().getIntValue("runattempt",0);
    if (s_yateRun >= s_yateRunAlarm && s_yateRunAlarm >= 1) {
	String notif = lookup(EngineInfo::ENGINE_RUNATTEMPT,s_engineQuery,"");
	if (!notif.null())
	    __plugin.sendTrap(notif,String(s_yateRun));
    }
    int lastsignal = Engine::runParams().getIntValue(YSTRING("lastsignal"),0);
    if (lastsignal > 0) {
	String notif = lookup(EngineInfo::ENGINE_UNEX_RESTART,s_engineQuery,"");
	if (!notif.null())
	    __plugin.sendTrap(notif,String(lastsignal));
    }
    return false;
};

/**
 * AuthHandler
 */
bool AuthHandler::received(Message& msg)
{
    String user = msg.getValue("username","");
    if (!user.null())
        m_count++;
    return false;
}

/**
 * RegisterHandler
 */
bool RegisterHandler::received(Message& msg)
{
    m_count++;
    return false;
}

/**
 * Cache
 */
Cache::~Cache()
{
    discard();
}

bool Cache::load()
{
    return false;
}

// discard cached data
void Cache::discard()
{
    DDebug(&__plugin,DebugInfo,"Cache::discard() [%p] - dropping cached data",this);
    Lock l(this);
    m_reload = true;
    m_table.clear();
}

String Cache::getInfo(const String& query, unsigned int& index, TokenDict* dict)
{
    DDebug(&__plugin,DebugAll,"Cache::getInfo(query='%s',index='%d') [%p]",query.c_str(),index,this);
    // if we have data, check if it is still valid
    if (isExpired())
	discard();
    else
	// if the data has not yet expired, update the expire time
	updateExpire();

    String retStr;
    // if the is no data available, obtain it from an engine.status message
    if (m_reload && !load())
	return retStr;

    Lock l(this);
    // lookup the type of the query, and if it's of type COUNT, return the number of entries
    int type = lookup(query,dict,0);
    if (type == COUNT) {
	retStr += m_table.count();
	return retStr;
    }
    // if it's not of type COUNT, check if the requested index is within the range of the table
    if (index < 1 || index > m_table.count())
	return retStr;
    // get the entry of the given index
    NamedList* nl = static_cast<NamedList*>(m_table[index - 1]);
    if (!nl)
	return retStr;
    // get the result
    if (type == INDEX) {
	retStr += index;
	return retStr;
    }
    retStr = nl->getValue(query,"");
    if (retStr.null())
	retStr = "no info";
    return retStr;
}

/**
 * ActiveCallInfo
 */
// match bill ids to find a call's peer
String ActiveCallsInfo::checkPeers(const String& billID, const String& id)
{
    DDebug(&__plugin,DebugAll,"ActiveCallsInfo::checkPeers('%s','%s')",billID.c_str(),id.c_str());
    if (billID.null())
	return id;
    String retPeers;
    for(ObjList* o = m_table.skipNull(); o; o = o->skipNext()) {
	NamedList* nl = static_cast<NamedList*>(o->get());
	if (!nl)
	    continue;
	String otherBillID = nl->getValue("billId","");
	String peers = nl->getValue(lookup(PEER,s_activeCallInfo,0),"");
	if (billID == otherBillID) {
	    String otherID = nl->getValue(lookup(ID,s_activeCallInfo,0),"");
	    peers.append(id,";");
	    retPeers.append(otherID,";");
	}
	nl->setParam(lookup(PEER,s_activeCallInfo,0),peers);
    }
    return retPeers;
}

// load data into the cache
bool ActiveCallsInfo::load()
{
    DDebug(&__plugin,DebugInfo,"ActiveCallsInfo::load() [%p] - loading data",this);
    // emit an engine.status message
    Message m("engine.status");
    m.addParam("module","cdrbuild");
    Engine::dispatch(m);
    String& status = m.retValue();
    if (TelEngine::null(status))
	return false;

    Lock l(this);
    m_table.clear();

    int pos = status.rfind(';');
    if (pos < 0)
	return false;

    status = status.substr(pos + 1);
    ObjList* calls = status.split(',');
    for (ObjList* o = calls->skipNull(); o; o = o->skipNext()) {
	String* callInfo = static_cast<String*>(o->get());
	if ( *callInfo == "\r\n")
	    continue;
	if (pos > -1) {
	    int pos = callInfo->find("=");
	    NamedList* nl = new NamedList("");

	    String id  = callInfo->substr(0,pos);
	    callInfo->startSkip(String(id + "="));
	    nl->setParam(lookup(ID,s_activeCallInfo,0),id);
	    int i = 0;
	    String peers;
	    while (i < 5) {
		pos = callInfo->find("|");
		if (pos < -1)
		    break;
		String val = callInfo->substr(0,pos);
		callInfo->startSkip(String(val + "|"),false);
		int p = -1;
		switch (i) {
		    case 0:
			p = val.find("=");
			if (p > -1)
			    val = val.substr(p+1);
			nl->setParam(lookup(STATUS,s_activeCallInfo,0),val);
			break;
		    case 1:
			val = ( val.null() ? "no info" : val);
			nl->setParam(lookup(CALLER,s_activeCallInfo,0),val);
			break;
		    case 2:
			val = ( val.null() ? "no info" : val);
			nl->setParam(lookup(CALLED,s_activeCallInfo,0),val);
			break;
		    case 3:
			peers = checkPeers(val,nl->getValue(lookup(ID,s_activeCallInfo,0),""));
			nl->setParam("billId",val);
			nl->setParam(lookup(PEER,s_activeCallInfo,0),peers);
			break;
		    case 4:
			cutNewLine(val);
			val = ( val.null() ? "no info" : val);
			nl->setParam(lookup(DURATION,s_activeCallInfo,0),val);
			break;
		    default:
			break;
		}
		i++;
	    }
	    m_table.append(nl);
	}
    }
    TelEngine::destruct(calls);
    updateExpire();
    return true;
}

/**
 * InterfaceInfo
 */
// reimplementation of the Cache::discard() function, only resets the status information
void SigInfo::discard()
{
    if (!m_dictionary)
	return;
    DDebug(&__plugin,DebugAll,"SigInfo::discard() [%p] - dropping cached data",this);
    for (ObjList* o = m_table.skipNull(); o; o = o->skipNext()) {
	NamedList* nl = static_cast<NamedList*>(o->get());
        nl->setParam(lookup(STATUS,m_dictionary,""),"unknown");
    }
    m_reload = true;
}

// STUB
bool SigInfo::load()
{
    DDebug(&__plugin,DebugWarn,"SigInfo::load() [%p] - STUB",this);
    return true;
}

// increase the counter for number of alarms sent
void SigInfo::updateAlarmCounter(const String& name)
{
    if (!m_dictionary)
	return;
    DDebug(&__plugin,DebugAll,"SigInfo::updateAlarmCounter('%s') [%p]",name.c_str(),this);
    NamedList* nl = static_cast<NamedList*>(m_table[name]);
    if (!nl) {
	load();
	nl = static_cast<NamedList*>(m_table[name]);
    }
    if (nl) {
	String param = lookup(ALARMS_COUNT,m_dictionary,"");
	if (param.null())
	    return;
	int val = nl->getIntValue(param,0);
	val++;
	nl->setParam(param,String(val));
    }
}

/**
 * InterfaceInfo
 */
// parse interface information and store it in the cache
bool InterfaceInfo::load()
{
    DDebug(&__plugin,DebugAll,"InterfaceInfo::load() [%p] - updating internal data",this);
    Message m("engine.status");
    m.addParam("module","sig ifaces");
    Engine::dispatch(m);
    String& status = m.retValue();
    if (!TelEngine::null(status)) {
	cutNewLine(status);
	ObjList* parts = status.split(';');
	if (!(parts && parts->count() > 2)) {
	    TelEngine::destruct(parts);
	    return true;
	}
	String ifaces = static_cast<String*>(parts->at(2));
	if (ifaces.null()) {
	    TelEngine::destruct(parts);
	    return true;
	}
	Lock l(this);
	ObjList* list = ifaces.split(',');
	for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	    String iface = static_cast<String*>(o->get());
	    String name, status;
	    iface.extractTo("=",name).extractTo("|",status);
	    if (name.null())
		continue;
	    NamedList* nl = static_cast<NamedList*>(m_table[name]);
	    if (!nl) {
		nl = new NamedList(name);
		nl->setParam(lookup(ID,s_ifacesInfo,""),name);
		nl->setParam(lookup(STATUS,s_ifacesInfo,""),status);
		m_table.append(nl);
	    }
	    else
		nl->setParam(lookup(STATUS,s_ifacesInfo,""),status);
	    if (!nl->getParam(lookup(ALARMS_COUNT,s_ifacesInfo,"")))
		nl->setParam(lookup(ALARMS_COUNT,s_ifacesInfo,""),"0");
	}
	TelEngine::destruct(list);
	TelEngine::destruct(parts);
    }
    updateExpire();
    return true;
}

/**
 * LinkInfo
 */
// parse link information
bool LinkInfo::load()
{
    DDebug(&__plugin,DebugAll,"LinkInfo::load() [%p] - loading data",this);
    Message m("engine.status");
    m.addParam("module","sig links");
    Engine::dispatch(m);
    String& status = m.retValue();
    if (!TelEngine::null(status)) {
	cutNewLine(status);
	ObjList* parts = status.split(';');
	if (!(parts && parts->count() > 2)) {
	    TelEngine::destruct(parts);
	    return true;
	}
	String links = static_cast<String*>(parts->at(2));
	if (links.null()) {
	    TelEngine::destruct(parts);
	    return true;
	}
	Lock l(this);
	ObjList* list = links.split(',');
	for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	    String link = static_cast<String*>(o->get());
	    String name,type,status;
	    int uptime = 0;
	    link.extractTo("=",name).extractTo("|",type).
		extractTo("|",status).extractTo("|",uptime);
	    if (name.null() || type.null())
		continue;
	    NamedList* nl = static_cast<NamedList*>(m_table[name]);
	    if (!nl) {
		nl = new NamedList(name);
		nl->setParam(lookup(ID,s_linkInfo,""),name);
		nl->setParam(lookup(TYPE,s_linkInfo,""),type);
		nl->setParam(lookup(STATUS,s_linkInfo,""),status);
		nl->setParam(lookup(UPTIME,s_linkInfo,""),String(uptime));
		m_table.append(nl);
	    }
	    else {
		nl->setParam(lookup(STATUS,s_linkInfo,""),status);
		nl->setParam(lookup(UPTIME,s_linkInfo,""),String(uptime));
	    }
	    if (!nl->getParam(lookup(ALARMS_COUNT,s_linkInfo,"")))
		nl->setParam(lookup(ALARMS_COUNT,s_linkInfo,""),"0");
	    if (!nl->getParam(lookup(UPTIME,s_linkInfo,"")))
		nl->setParam(lookup(UPTIME,s_linkInfo,""),"0");
	}
	TelEngine::destruct(list);
	TelEngine::destruct(parts);
    }
    updateExpire();
    return true;
}

/**
 * LinksetInfo
 */
// parse linkset information
bool LinksetInfo::load()
{
    DDebug(&__plugin,DebugAll,"LinksetInfo::load() [%p] - loading data",this);
    Message m("engine.command");
    m.addParam("partial","status sig ");
    m.addParam("partline","status sig");
    if (!Engine::dispatch(m))
	return false;
    String& status = m.retValue();
    if (TelEngine::null(status))
	return false;

    Lock l(this);
    ObjList* links = status.split('\t');
    for (ObjList* o = links->skipNull(); o; o = o->skipNext()) {
	String* link = static_cast<String*>(o->get());
	if (*link == "links" || *link == "ifaces")
	    continue;
	Message msg("engine.status");
	msg.addParam("module",String("sig " + *link));
	Engine::dispatch(msg);
	String& linkInfo = msg.retValue();
	if (linkInfo.null())
	    continue;
	NamedList* nl = static_cast<NamedList*>(m_table[*link]);
	if (nl)
	    parseLinksetInfo(linkInfo,*link,nl);
	else {
	    nl = parseLinksetInfo(linkInfo,*link);
	    if (nl)
		m_table.append(nl);
	}
    }
    TelEngine::destruct(links);
    updateExpire();
    return true;
}

// parse the information about a single linkset
NamedList* LinksetInfo::parseLinksetInfo(String& info,const String& link, NamedList* infoFill)
{
    cutNewLine(info);
    DDebug(&__plugin,DebugAll,"LinksetInfo::parseLinkInfo(info='%s',link='%s', infoFill='%p') [%p]",info.c_str(),link.c_str(),infoFill,this);
    NamedList* nl = (infoFill ? infoFill : new NamedList(link));

    ObjList* parts = info.split(';',false);
    for (ObjList* obj = parts->skipNull(); obj; obj = obj->skipNext()) {
	String* infoPart = static_cast<String*>(obj->get());
	if (TelEngine::null(infoPart))
	    continue;
	ObjList* params = infoPart->split(',',false);
	for (ObjList* o = params->skipNull(); o; o = o->skipNext()) {
	    String* param = static_cast<String*>(o->get());
	    int pos = param->find("=");
	    if (pos < 0)
		continue;
	    String nameParam = param->substr(0,pos);
	    String valParam = param->substr(pos + 1);
	    int type = lookup(nameParam,s_linksetStatus,0);
	    if (type > 0) {
		if (type == TYPE && (valParam.null() || valParam != "ss7-mtp3")) {
		    TelEngine::destruct(params);
		    TelEngine::destruct(nl);
		    TelEngine::destruct(parts);
		    return 0;
		}
		nl->setParam(lookup(type,s_linksetInfo,""),valParam);
	    }
	}
	TelEngine::destruct(params);
    }
    NamedString* linksetId = nl->getParam(lookup(ID,s_linksetInfo));
    NamedString* typeStr = nl->getParam(lookup(TYPE,s_linksetInfo));
    if (TelEngine::null(linksetId) || TelEngine::null(typeStr)) {
	TelEngine::destruct(parts);
	TelEngine::destruct(nl);
	return 0;
    }
    TelEngine::destruct(parts);
    if (!nl->getParam(lookup(ALARMS_COUNT,s_linksetInfo,"")))
	nl->setParam(lookup(ALARMS_COUNT,s_linksetInfo,""),"0");
    return nl;
}

/**
 * TrunkInfo
 */
// reset trunk information
void TrunkInfo::discard()
{
    DDebug(&__plugin,DebugAll,"TrunkInfo::discard() [%p] - dropping cached data",this);
    for (ObjList* o = m_table.skipNull(); o; o = o->skipNext()) {
	NamedList* nl = static_cast<NamedList*>(o->get());
	nl->setParam(lookup(TYPE,s_trunkInfo,""),"");
	nl->setParam(lookup(CIRCUITS,s_trunkInfo,""),"0");
	nl->setParam(lookup(CALLS,s_trunkInfo,""),"0");
	nl->setParam(lookup(LOCKED,s_trunkInfo,""),"0");
	nl->setParam(lookup(IDLE,s_trunkInfo,""),"0");
    }
    m_reload = true;
}

// parse and load trunk information
bool TrunkInfo::load()
{
    DDebug(&__plugin,DebugAll,"TrunkInfo::load() [%p] - loading data",this);
    Message m("engine.command");
    m.addParam("partial","status sig ");
    m.addParam("partline","status sig");
    if (!Engine::dispatch(m))
	return false;
    String& status = m.retValue();
    if (TelEngine::null(status))
	return false;

    Lock l(this);
    ObjList* trunks = status.split('\t');
    for (ObjList* o = trunks->skipNull(); o; o = o->skipNext()) {
	String* trunk = static_cast<String*>(o->get());
	if ((*trunk) == "links" || (*trunk) == "ifaces")
	    continue;
	Message msg("engine.status");
	msg.addParam("module",String("sig " + *trunk));
	Engine::dispatch(msg);
	String& trunkInfo = msg.retValue();
	if (trunkInfo.null())
	    continue;
	NamedList* nl = static_cast<NamedList*>(m_table[*trunk]);
	if (nl)
	    parseTrunkInfo(trunkInfo,*trunk,nl);
	else {
	    nl = parseTrunkInfo(trunkInfo,*trunk);
	    if (nl)
		m_table.append(nl);
	}
    }
    TelEngine::destruct(trunks);
    // update the expire time
    updateExpire();
    return true;
}

// parse the information for a single trunk
NamedList* TrunkInfo::parseTrunkInfo(String& info, const String& trunk, NamedList* infoFill)
{
    cutNewLine(info);
    DDebug(&__plugin,DebugAll,"TrunkInfo::parseTrunkInfo(info='%s',trunk='%s', infoFill='%p') [%p]",info.c_str(),trunk.c_str(),infoFill,this);
    NamedList* nl = (infoFill ? infoFill : new NamedList(trunk));

    ObjList* parts = info.split(';',false);
    for (ObjList* obj = parts->skipNull(); obj; obj = obj->skipNext()) {
	String* infoPart = static_cast<String*>(obj->get());
	if (TelEngine::null(infoPart))
	    continue;
	ObjList* params = infoPart->split(',',false);
	for (ObjList* o = params->skipNull(); o; o = o->skipNext()) {
	    String* param = static_cast<String*>(o->get());
	    int pos = param->find("=");
	    if (pos < 0) {
		TelEngine::destruct(params);
		continue;
	    }
	    String nameParam = param->substr(0,pos);
	    String valParam = param->substr(pos + 1);

	    int type = lookup(nameParam,s_trunkStatus,0);
	    if (type > 0)
		nl->setParam(lookup(type,s_trunkInfo,""),valParam);
	}
	TelEngine::destruct(params);
    }
    // check that it's indeed a trunk
    NamedString* trunkId = nl->getParam(lookup(ID,s_trunkInfo));
    if (TelEngine::null(trunkId)) {
	TelEngine::destruct(parts);
	TelEngine::destruct(nl);
	return 0;
    }
    TelEngine::destruct(parts);
    if (!nl->getParam(lookup(ALARMS_COUNT,s_trunkInfo,"")))
	nl->setParam(lookup(ALARMS_COUNT,s_trunkInfo,""),"0");
    return nl;
}

/**
 * AccountsInfo
 */
// parse and store accounts information
bool AccountsInfo::load()
{
    DDebug(&__plugin,DebugAll,"AccountsInfo::load() [%p] - loading data",this);
    String modules[] = {"sip","h323","iax","jabberclient"};

    int i = 0;
    while (i < 4) {
	Message m("engine.status");
	m.setParam("module",modules[i] + " accounts");
	Engine::dispatch(m);
	String& status = m.retValue();
	i++;
	if (TelEngine::null(status))
	    continue;

	cutNewLine(status);
	//find protocol
	String protoParam = "protocol=";
	int pos = status.find(protoParam);
	if (pos < 0)
	    continue;
	int auxPos = status.find(",",pos);
	if (auxPos < pos + (int)protoParam.length())
	    continue;
	String proto = status.substr(pos + protoParam.length(),auxPos - (pos + protoParam.length()));

	pos = status.rfind(';');
	if (pos < 0)
	    continue;
	status = status.substr(pos + 1);
	Lock l(this);
	ObjList* accounts = status.split(',',false);
	for (ObjList* o = accounts->skipNull(); o; o = o->skipNext()) {
	    String* account = static_cast<String*>(o->get());
	    int pos1 = account->find("=");
	    int pos2 = account->find("|");
	    if (pos1 < 0 || pos2 < 0)
		continue;
	    String name = account->substr(0,pos1);
	    String username = account->substr(pos1 + 1,pos2 - pos1 -1);
	    String status = account->substr(pos2 + 1);

	    if (name.null())
		continue;
	    NamedList* nl = new NamedList("");
	    nl->setParam(lookup(ID,s_accountInfo,""),name);
	    nl->setParam(lookup(USERNAME,s_accountInfo,""),username);
	    nl->setParam(lookup(STATUS,s_accountInfo,""),status);
	    nl->setParam(lookup(PROTO,s_accountInfo,""),proto);
	    m_table.append(nl);
	}

	TelEngine::destruct(accounts);
	l.drop();
    }
    updateExpire();
    return true;
}

/**
  * EngineInfo - engine status information cache
  */
// reimplemented from Cache; obtain the result for a engine query
String EngineInfo::getInfo(const String query, unsigned int index, TokenDict* dict)
{
    DDebug(&__plugin,DebugAll,"EngineInfo::getInfo(%s %d) [%p]",query.c_str(),index,this);

    // if we have data, check if it is still valid
    if (isExpired())
	discard();
    else
	// if the data has not yet expired, update the expire time
	updateExpire();

    String retStr;
    // if the is no data available, obtain it from an engine.status message
    if (m_reload && !load())
	return retStr;

    Lock l(this);
    int type = lookup(query,s_engineQuery,0);
    if (!type)
	return retStr;

    if (index > 1)
	return retStr;
    NamedList* nl = static_cast<NamedList*>(m_table[index]);
    if (!nl)
	return retStr;
    if (type == ENGINE_STATE)
	return s_nodeState;
    retStr = nl->getValue(query,"");
    if (retStr.null())
	retStr = "no info";
    return retStr;
}

// load data into the cache
bool EngineInfo::load()
{
    DDebug(&__plugin,DebugInfo,"EngineInfo::load() [%p] - loading data",this);
    // emit an engine.status message
    Message m("engine.status");
    m.setParam("module","engine");
    Engine::dispatch(m);
    String& status = m.retValue();
    if (TelEngine::null(status))
	return false;
    cutNewLine(status);

    Lock l(this);
    m_table.clear();
    NamedList* nl = new NamedList("");
    ObjList* params = status.split(';');
    for(ObjList* o = params->skipNull(); o; o = o->skipNext()) {
	String* strVal = static_cast<String*>(o->get());
	ObjList* l = strVal->split(',');
	for (ObjList* j = l->skipNull(); j; j = j->skipNext()) {
	    String* str = static_cast<String*>(j->get());
	    int pos = str->find("=");
	    if (pos < 0)
		continue;
	    String param = str->substr(0,pos);
	    String value = str->substr(pos + 1);
	    int type = lookup(param,s_engineInfo,0);
	    if (!type)
		continue;
	    nl->setParam(lookup(type,s_engineQuery,""),value);
	}
	TelEngine::destruct(l);
    }
    m_table.append(nl);
    TelEngine::destruct(params);
    updateExpire();
    return true;
}

/**
  * ModuleInfo
  */
// load data into the cache
bool ModuleInfo::load()
{
    DDebug(&__plugin,DebugInfo,"ModuleInfo::load() [%p] - loading data",this);
    // emit an engine.status message
    Message m("engine.status");
    m.setParam("details",String::boolText(false));
    Engine::dispatch(m);
    String& status = m.retValue();
    if (TelEngine::null(status))
	return false;

    Lock l(this);
    m_table.clear();

    // split the info into lines
    ObjList* lines = status.split('\n',false);
    for (ObjList* o = lines->skipNull(); o; o = o->skipNext()) {
	String* line = static_cast<String*>(o->get());
	if (!line)
	    continue;
	cutNewLine(*line);
	ObjList* parts = line->split(';');
	NamedList* nl = new NamedList("");
	for (ObjList* p = parts->skipNull(); p; p = p->skipNext()) {
	    String* str = static_cast<String*>(p->get());
	    ObjList* paramVal = str->split(',');
	    String info = "";
	    for (ObjList* l = paramVal->skipNull(); l; l = l->skipNext()) {
	    	String* pair = static_cast<String*>(l->get());
	    	int pos = pair->find("=");
		if (pos < 0)
		    continue;
		String param = pair->substr(0,pos);
		String value = pair->substr(pos + 1);
		int type = lookup(param,s_moduleInfo,0);
		if (!type) {
		    info += (info.null() ? pair : String("," + *pair));
		    continue;
		}
		nl->setParam(lookup(type,s_moduleQuery,""),value);
	    }
	    nl->setParam(lookup(MODULE_INFO,s_moduleQuery,""),info);
	    TelEngine::destruct(paramVal);
	}
	TelEngine::destruct(parts);
	if ( String("engine") == nl->getValue(lookup(MODULE_NAME,s_moduleQuery,""),"")) {
	    TelEngine::destruct(nl);
	    continue;
	}
        m_table.append(nl);

    }
    TelEngine::destruct(lines);
    updateExpire();
    return true;
}

/**
 *  DatabaseAccount - an object holding information about a single monitored database account
 */
// configure a database account for monitoring and initialize internal data
DatabaseAccount::DatabaseAccount(const NamedList* cfg)
    : m_resetTime(0), m_resetInterval(3600),
    m_isCurrent(true)
{
    if (cfg) {
	Debug(&__plugin,DebugAll,"DatabaseAccount('%s') created for monitoring [%p]",cfg->c_str(),this);
	m_name = cfg->c_str();
	m_alarms = 0;
	for (int i = 0; i < ExecTime; i++) {
	    m_dbCounters[i] = 0;
	    m_prevDbCounters[i] = 0;
	}
	for (int i = 0; i <= NoConnAlrmCount - TooManyAlrmCount; i++)
	    m_alarmCounters[i] = 0;
	updateConfig(cfg);
	m_resetTime = Time::secNow() + m_resetInterval;
    }
    m_isCurrent = true;
}

// update internal data from a message received from the sql modules
void DatabaseAccount::update(const NamedList& info)
{
    XDebug(&__plugin,DebugAll,"DatabaseAccount::update() [%p]",this);
    for (unsigned int i = 0; i < info.count(); i++) {
	NamedString* ns = info.getParam(i);
	if (!(ns && *ns))
	    continue;
	int type = lookup(ns->name(),s_dbAccountInfo,-1);
	if (type < 0)
	    continue;

	u_int16_t alarm = TOTAL_ALARM << (type);
	if (type <= TIME_IDX) {
	    m_dbCounters[type] = ns->toInteger();
	    if ((type != TIME_IDX) && (m_dbCounters[type] - m_prevDbCounters[type] >= m_thresholds[type]) && ((m_alarms & alarm) == 0)) {
		m_alarms |= alarm;
		m_alarmCounters[type]++;
		__plugin.sendTrap(lookup(TooManyAlrm + type,s_dbAccountQueries,""),toString(),index());
	    }
	}
	if (type == CONN_IDX) {
	    if (!ns->toBoolean()) {
		if ((m_alarms & alarm) == 0) {
		    m_alarmCounters[CONN_IDX]++;
		    m_alarms |= alarm;
		    __plugin.sendTrap(lookup(NoConnAlrm,s_dbAccountQueries,""),toString(),index());
		}
	    }
	    else
		m_alarms &= ~alarm;
	}
    }
    // wait to gather all necessary data to compute average time
    double execTime = m_dbCounters[TIME_IDX] - m_prevDbCounters[TIME_IDX];
    double queriesNo = (m_dbCounters[TOTAL_IDX] - m_prevDbCounters[TOTAL_IDX]) - (m_dbCounters[FAILED_IDX] - m_prevDbCounters[FAILED_IDX]);
    if (queriesNo > 0 && (execTime / queriesNo / 1000)  >= m_thresholds[TIME_IDX]) {
	if ((m_alarms & EXEC_ALARM) == 0)  {
	    m_alarms |= EXEC_ALARM;
	    m_alarmCounters[TIME_IDX]++;
	    __plugin.sendTrap(lookup(ExecTooLongAlrm,s_dbAccountQueries,""),toString(),index());
	}
    }
    else
	m_alarms &= ~EXEC_ALARM;
}

void DatabaseAccount::updateConfig(const NamedList* cfg)
{
    if (!cfg)
	return;
    for (int i = 0; i <= MaxExecTime - MaxQueries; i++)
	m_thresholds[i] = cfg->getIntValue(lookup(MaxQueries + i,s_dbAccountInfo,""),0);
    m_resetInterval = cfg->getIntValue("notiftime",3600);
    if (m_resetTime > Time::secNow() + m_resetInterval)
	m_resetTime = Time::secNow() + m_resetInterval;
    m_isCurrent = true;
}

// obtain information from this entry
const String DatabaseAccount::getInfo(unsigned int query)
{
    DDebug(&__plugin,DebugAll,"DatabaseAccount::getInfo('%s') [%p]",lookup(query,s_dbAccountQueries,""),this);
    String ret = "";
    double execTime = 0;
    double queriesNo = 0;
    switch (query) {
	case QueriesCount:
	case FailedCount:
	case ErrorsCount:
	    ret << m_dbCounters[query - 1] - m_prevDbCounters[query - 1];
	    break;
	case ExecTime:
	    execTime = m_dbCounters[TIME_IDX] - m_prevDbCounters[TIME_IDX];
	    queriesNo = (m_dbCounters[TOTAL_IDX] - m_prevDbCounters[TOTAL_IDX]) - (m_dbCounters[FAILED_IDX] - m_prevDbCounters[FAILED_IDX]);
	    if (queriesNo > 0)
		ret << (unsigned int) (execTime / queriesNo / 1000);
	    else
		ret << 0;
	    break;
	case TooManyAlrmCount:
	case TooManyFailedAlrmCount:
	case TooManyErrorAlrmCount:
	case ExecTooLongAlrmCount:
	case NoConnAlrmCount:
	    ret << m_alarmCounters[query - TooManyAlrmCount];
	    break;
	case MaxQueries:
	case MaxFailedQueries:
	case MaxErrorQueries:
	case MaxExecTime:
	    ret << m_thresholds[query - MaxQueries];
	    break;
	case AccountName:
	    ret = m_name;
	    break;
	case AccountIndex:
	    ret = index();
	    break;
	default:
	    break;
    }
    return ret;
}

void DatabaseAccount::reset()
{
    if (Time::secNow() < m_resetTime)
	return;

    __plugin.sendTrap(lookup(QueriesCount,s_dbAccountQueries,""),String(m_dbCounters[TOTAL_IDX] - m_prevDbCounters[TOTAL_IDX]),index());
    __plugin.sendTrap(lookup(FailedCount,s_dbAccountQueries,""),String(m_dbCounters[FAILED_IDX] - m_prevDbCounters[FAILED_IDX]),index());
    __plugin.sendTrap(lookup(ErrorsCount,s_dbAccountQueries,""),String(m_dbCounters[ERROR_IDX] - m_prevDbCounters[ERROR_IDX]),index());

    double execTime = m_dbCounters[TIME_IDX] - m_prevDbCounters[TIME_IDX];
    double queriesNo = (m_dbCounters[TOTAL_IDX] - m_prevDbCounters[TOTAL_IDX]) - (m_dbCounters[FAILED_IDX] - m_prevDbCounters[FAILED_IDX]);
    unsigned int time = 0;
    if (queriesNo > 0)
	time = (unsigned int) (execTime / queriesNo / 1000);
    __plugin.sendTrap(lookup(ExecTime,s_dbAccountQueries,""),String(time),index());

    m_alarms = 0;
    for (unsigned int i = 0; i < CONN_IDX; i++)
	m_prevDbCounters[i] = m_dbCounters[i];

    m_resetTime = Time::secNow() + m_resetInterval;
}

/**
 * DatabaseInfo
 */
// parse database status information and store it for all sql modules
bool DatabaseInfo::load()
{
    DDebug(&__plugin,DebugInfo,"DatabaseInfo::load() [%p] - loading data",this);
    String modules[] = {"pgsqldb", "mysqldb"};
    unsigned int i = 0;

    for (int i = 0; i < FailedConns; i++)
	m_connData[i] = 0;

    while (i < 2) {
	Message msg("engine.status");
	msg.addParam("module",modules[i]);
	msg.addParam("details","false");
	Engine::dispatch(msg);
	String& status = msg.retValue();
	if (!TelEngine::null(status))  {
	    cutNewLine(status);
	    /* status example: name=mysqldb,type=database,format=Total|Failed|Errors|AvgExecTime;conns=1,failed=1 */
	    int pos = status.rfind(';');
	    if (pos < 0)
		continue;
	    String connInfo = status.substr(pos + 1);
	    if (connInfo.null())
		continue;
	    ObjList* l = connInfo.split(',');
	    for (ObjList* j = l->skipNull(); j; j = j->skipNext()) {
		String* str = static_cast<String*>(j->get());
		pos = str->find("=");
		if (pos < 0)
		    continue;
		String param = str->substr(0,pos);
		String value = str->substr(pos + 1);
		int type = lookup(param,s_databaseInfo,0);
		if (!type)
		    continue;
		m_connData[type - 1] += value.toInteger();
	    }
	    TelEngine::destruct(l);
	}
	i++;
    }
    updateExpire();
    return true;
}

// get the answer to a query for the entry with the given index
String DatabaseInfo::getInfo(const String& query, unsigned int& index, TokenDict* dict)
{
    DDebug(&__plugin,DebugAll,"DatabaseInfo::getInfo(query='%s',index='%d',dict='%p') [%p]",query.c_str(),index,dict,this);
    // handle a query about an entry in the table
    Lock l(this);
    int type = lookup(query,s_dbAccountQueries,0);
    if (type) {
	if (index == 0 || index > m_table.count())
	    return "";
	DatabaseAccount* acc = static_cast<DatabaseAccount*>(m_table[index - 1]);
	if (!acc)
	    return "";
	return acc->getInfo(type);
    }
    if (!isExpired())
	// if the data has not yet expired, update the expire time
	updateExpire();
    // if the is no data available, obtain it from an engine.status message
    if (m_reload && !load())
	return "";
    // handle queries about connections
    type = lookup(query,s_databaseQuery,0);
    switch (type) {
	case Accounts:
	    return String(m_table.count());
	case Connections:
	case FailedConns:
	    return String(m_connData[type - 1]);
	default:
	    break;
    }
    return "";
}

void DatabaseInfo::reset()
{
    Lock l(this);
    for (ObjList* o = m_table.skipNull(); o; o = o->skipNext()) {
	DatabaseAccount* acc = static_cast<DatabaseAccount*>(o->get());
	acc->reset();
    }
}

// create a new DatabaseAccount object for monitoring a database
void DatabaseInfo::addDatabase(NamedList* cfg)
{
    if (!cfg || !m_monitor)
	return;
    DDebug(&__plugin,DebugInfo,"DatabaseInfo::addDatabase('%s') [%p]",cfg->c_str(),this);
    lock();
    DatabaseAccount* acc = static_cast<DatabaseAccount*>(m_table[*cfg]);
    if (!acc) {
	acc = new DatabaseAccount(cfg);;
        m_table.append(acc);
	acc->setIndex(m_table.count());
    }
    else
	acc->updateConfig(cfg);
    unlock();
}

void DatabaseInfo::updateDatabaseAccounts()
{
    lock();
    bool deletedRoute = true;
    while (deletedRoute) {
	deletedRoute = false;
	for (ObjList* o = m_table.skipNull(); o; o = o->skipNext()) {
	    DatabaseAccount* acc = static_cast<DatabaseAccount*>(o->get());
	    if (!acc->isCurrent()) {
		DDebug(__plugin.name(),DebugAll,"DatabaseInfo::updateDatabaseAccounts() - removed database account '%s' from monitoring",
			    acc->toString().c_str());
		m_table.remove(acc);
		deletedRoute = true;
	    }
	}
    }
    unsigned int index = 1;
    for (ObjList* o = m_table.skipNull(); o; o = o->skipNext()) {
	DatabaseAccount* acc = static_cast<DatabaseAccount*>(o->get());
	acc->setIsCurrent(false);
	acc->setIndex(index);
	index++;
    }
    unlock();
}

//update the information for a given account
void DatabaseInfo::update(const Message& msg)
{
    XDebug(&__plugin,DebugInfo,"DatabaseInfo::update()");
    int count = msg.getIntValue("count",0);
    for (int i = 0; i < count; i++) {
	String str = s_dbParam;
	str << i;
	String acc = msg.getValue(str);
	DatabaseAccount* dbAccount = static_cast<DatabaseAccount*>(m_table[acc]);
	if (!dbAccount)
	    continue;
	NamedList nl(acc);
	str = s_totalParam;
	str << i;
	nl.setParam("total",msg.getValue(str));
	str = s_failedParam;
	str << i;
	nl.setParam("failed",msg.getValue(str));
	str = s_errorParam;
	str << i;
	nl.setParam("errorred",msg.getValue(str));
	str = s_hasConnParam;
	str << i;
	nl.setParam("hasconn",msg.getValue(str));
	str = s_timeParam;
	str << i;
	nl.setParam("querytime",msg.getValue(str));
	dbAccount->update(nl);
    }
}

/**
 * RTPEntry
 */
RTPEntry::RTPEntry(String rtpDirection)
    : m_rtpDir(rtpDirection), m_index(0),
      m_isCurrent(true)
{
    Debug(&__plugin,DebugAll,"RTPEntry '%s' created [%p]",rtpDirection.c_str(),this);
    reset();
}

RTPEntry::~RTPEntry()
{
    Debug(&__plugin,DebugAll,"RTPEntry '%s' destroyed [%p]",m_rtpDir.c_str(),this);
}

// update the RTP info from the given list
void RTPEntry::update(const NamedList& nl)
{
    DDebug(&__plugin,DebugAll,"RTPEntry::update() name='%s' [%p]",m_rtpDir.c_str(),this);
    for (unsigned int i = 0; i < nl.count(); i++) {
	NamedString* n = nl.getParam(i);
	if (!n)
	    continue;
	int type = lookup(n->name(),s_rtpInfo,0);
	if (!type || type < NoAudio)
	    continue;
	m_counters[type - NoAudio] += (*n).toInteger();
    }
}

// reset counters
void RTPEntry::reset()
{
    DDebug(&__plugin,DebugAll,"RTPEntry::reset() '%s' [%p]",m_rtpDir.c_str(),this);
    for (int i = 0; i < WrongSSRC - Direction; i++)
	m_counters[i] = 0;
}

// the the answer to a query about this RTP direction
String RTPEntry::getInfo(unsigned int query)
{
    DDebug(&__plugin,DebugAll,"RTPEntry::getInfo('%s') '%s' [%p]",lookup(query,s_rtpQuery,""),m_rtpDir.c_str(),this);
    String retStr = "";
    switch (query) {
	case Direction:
	    retStr << m_rtpDir;
	    break;
	case Index:
	    retStr << m_index;
	    break;
	case NoAudio:
	case LostAudio:
	case PktsLost:
	case SyncLost:
	case SeqLost:
	case WrongSRC:
	case WrongSSRC:
	    retStr << m_counters[query - NoAudio];
	    break;
	default:
	    break;
    }
    return retStr;
}

/**
 *  RTPTable
 */
RTPTable::RTPTable(const NamedList* cfg)
    : m_rtpMtx(false,"Monitor::rtpInfo"),
      m_resetInterval(3600), m_monitor(false)
{
    Debug(&__plugin,DebugAll,"RTPTable created [%p]",this);
    // build RTPEntry objects for monitoring RTP directions if monitoring is enabled
    reconfigure(cfg);
}

void RTPTable::reconfigure(const NamedList* cfg)
{
    if (!cfg)
	return;
    m_monitor = cfg->getBoolValue("monitor",false);
    m_resetInterval = cfg->getIntValue("reset_interval",3600);

    m_rtpMtx.lock();
    if (!m_monitor)
	m_rtpEntries.clear();
    String directions = cfg->getValue("rtp_directions","");
    Debug(&__plugin,DebugAll,"RTPTable [%p] configured with directions='%s',resetTime=" FMT64U,this,directions.c_str(),m_resetInterval);
    if (m_monitor) {
	ObjList* l = directions.split(',');
	for (ObjList* o = l->skipNull(); o; o = o->skipNext()) {
	    String* str = static_cast<String*>(o->get());
	    RTPEntry* entry = static_cast<RTPEntry*>(m_rtpEntries[*str]);
	    if (!entry) {
		entry = new RTPEntry(*str);
		m_rtpEntries.append(entry);
		entry->setIndex(m_rtpEntries.count());
	    }
	    else
		entry->setIsCurrent(true);
	}
	TelEngine::destruct(l);
    }

    bool deletedDir = true;
    while (deletedDir) {
	deletedDir = false;
	for (ObjList* o = m_rtpEntries.skipNull(); o; o = o->skipNext()) {
	    RTPEntry* entry = static_cast<RTPEntry*>(o->get());
	    if (!entry->isCurrent()) {
		DDebug(__plugin.name(),DebugAll,"RTPTable::reconfigure() - removed direction '%s' from monitoring",entry->toString().c_str());
		m_rtpEntries.remove(entry);
		deletedDir = true;
	    }
	}
    }
    unsigned int index = 1;
    for (ObjList* o = m_rtpEntries.skipNull(); o; o = o->skipNext()) {
	RTPEntry* entry = static_cast<RTPEntry*>(o->get());
	entry->setIsCurrent(false);
	entry->setIndex(index);
	index++;
    }

    m_rtpMtx.unlock();
    m_resetTime = Time::secNow() + m_resetInterval;
}

// update an entry
void RTPTable::update(Message& msg)
{
    XDebug(&__plugin,DebugAll,"RTPTable::update() [%p]",this);
    String dir = lookup(RTPEntry::Direction,RTPEntry::s_rtpInfo,"");
    if (dir.null())
	dir = "remoteip";
    String rtpDir = msg.getValue(dir,"");
    if (rtpDir.null())
	return;
    m_rtpMtx.lock();
    RTPEntry* entry = static_cast<RTPEntry*>(m_rtpEntries[rtpDir]);
    if (entry)
	entry->update(msg);
    m_rtpMtx.unlock();
}

String RTPTable::getInfo(const String& query, const unsigned int& index)
{
    DDebug(&__plugin,DebugAll,"RTPTable::getInfo(query='%s',index='%u') [%p]",query.c_str(),index,this);
    String retStr = "";
    int type = lookup(query,s_rtpQuery,0);
    if (!type)
	return retStr;
    if (type == RTPEntry::Count)
	retStr << m_rtpEntries.count();
    else if (index > 0 && index <= m_rtpEntries.count()) {
	m_rtpMtx.lock();
	RTPEntry* entry = static_cast<RTPEntry*>(m_rtpEntries[index - 1]);
	if (entry)
	    retStr << entry->getInfo(type);
	m_rtpMtx.unlock();
    }
    return retStr;
}

// reset information
void RTPTable::reset()
{
    XDebug(&__plugin,DebugAll,"RTPTable::reset() [%p]",this);
    m_rtpMtx.lock();
    for (ObjList* o = m_rtpEntries.skipNull(); o; o = o->skipNext()) {
	RTPEntry* e = static_cast<RTPEntry*>(o->get());
	e->reset();
    }
    m_rtpMtx.unlock();
    m_resetTime = Time::secNow() + m_resetInterval;
}

/**
  * CallRouteQoS
  */
CallRouteQoS::CallRouteQoS(const String direction, const NamedList* cfg)
    : m_routeName(direction)
{
    Debug(&__plugin,DebugAll,"CallRouteQoS [%p] created for route '%s',cfg='%p'",this,direction.c_str(),cfg);
    for (int i = 0; i < NO_CAUSE - HANGUP; i++) {
	m_callCounters[i] = 0;
	m_callCountersAll[i] = 0;
    }

    for (int i = 0; i <= TOTAL_IDX; i++) {
	m_totalCalls[i] = 0;
	m_answeredCalls[i] = 0;
	m_delivCalls[i] = 0;
    }

    for (int i = 0; i <= NER_LOW_ALL; i++)
	m_alarmCounters[i] = 0;

    m_alarms = 0;
    m_overallAlarms = 0;

    m_alarmsSent = 0;
    m_overallAlarmsSent = 0;

    m_minCalls = 1;
    m_minASR = m_maxASR = m_minNER = -1;
    if (cfg)
	updateConfig(cfg);
    m_index = 0;
}

CallRouteQoS::~CallRouteQoS()
{
    Debug(&__plugin,DebugAll,"CallRouteQoS [%p] destroyed",this);
}

void CallRouteQoS::updateConfig(const NamedList* cfg)
{
    if (!cfg)
	return;
    m_minCalls = cfg->getIntValue("mincalls",m_minCalls);
    m_minASR = cfg->getIntValue("minASR",m_minASR);
    if (m_minASR > 100 || m_minASR < -1) {
	Debug(&__plugin,DebugNote,"CallRouteQoS::updateConfig() - route '%s': configured minASR is not in the -1..100 interval, "
		"defaulting to -1",m_routeName.c_str());
	m_minASR = -1;
    }
    m_maxASR = cfg->getIntValue("maxASR",m_maxASR);
    if (m_maxASR > 100 || m_maxASR < -1) {
	Debug(&__plugin,DebugNote,"CallRouteQoS::updateConfig() - route '%s': configured maxASR is not in the -1..100 interval, "
		"defaulting to -1",m_routeName.c_str());
	m_maxASR = -1;
    }
    m_minNER = cfg->getIntValue("minNER",m_minNER);
    if (m_minNER > 100 || m_minNER < -1) {
	Debug(&__plugin,DebugNote,"CallRouteQoS::updateConfig() - route '%s': configured minNER is not in the -1..100 interval, "
		"defaulting to -1",m_routeName.c_str());
	m_minNER = -1;
    }
    m_isCurrent = true;
}

// update the counters taking into account the type of the call and reason with which the call was ended
void CallRouteQoS::update(int type, int endReason)
{
    DDebug(&__plugin,DebugAll,"CallRouteQoS::update(callType='%d',endReason='%d') [%p] ",type,endReason,this);
    m_totalCalls[CURRENT_IDX]++;
    m_totalCalls[TOTAL_IDX]++;
    switch (type) {
	case ANSWERED:
	    m_answeredCalls[CURRENT_IDX]++;
	    m_answeredCalls[TOTAL_IDX]++;
	    break;
	case DELIVERED:
	    m_delivCalls[CURRENT_IDX]++;
	    m_delivCalls[TOTAL_IDX]++;
	    break;
	default:
	    break;
    }
    if (endReason != -1 && endReason >= HANGUP && endReason < NO_CAUSE) {
	m_callCounters[endReason - HANGUP]++;
	m_callCountersAll[endReason - HANGUP]++;
    }
}

// update the internal ASR/NER values and check for alarms
void CallRouteQoS::updateQoS()
{
    //XDebug(&__plugin,DebugAll,"CallRouteQoS::updateQoS() [%p]",this);
    int realASR, totalASR;
    if ((m_totalCalls[CURRENT_IDX] != m_totalCalls[PREVIOUS_IDX]) && (m_totalCalls[CURRENT_IDX] >= m_minCalls)) {

	float currentHyst = 50.0 / m_totalCalls[CURRENT_IDX] * s_qosHysteresisFactor;
	float totalHyst = 50.0 / m_totalCalls[TOTAL_IDX] * s_qosHysteresisFactor;

	realASR = (int) (m_answeredCalls[CURRENT_IDX] * 100.0 / m_totalCalls[CURRENT_IDX]);
	checkForAlarm(realASR,currentHyst,m_alarms,m_minASR,m_maxASR,LOW_ASR,HIGH_ASR);
	m_totalCalls[PREVIOUS_IDX] = m_totalCalls[CURRENT_IDX];

	totalASR = (int) (m_answeredCalls[TOTAL_IDX] * 100.0 / m_totalCalls[TOTAL_IDX]);
	checkForAlarm(totalASR,totalHyst,m_overallAlarms,m_minASR,m_maxASR,LOW_ASR,HIGH_ASR);

	int ner = (int) ((m_answeredCalls[CURRENT_IDX] + m_delivCalls[CURRENT_IDX]) * 100.0 / m_totalCalls[CURRENT_IDX]);
	checkForAlarm(ner,currentHyst,m_alarms,m_minNER,-1,LOW_NER);

	ner = (int) ((m_answeredCalls[TOTAL_IDX] + m_delivCalls[TOTAL_IDX]) * 100.0 / m_totalCalls[TOTAL_IDX]);
	checkForAlarm(ner,totalHyst,m_overallAlarms,m_minNER,-1,LOW_NER);
    }
}

// reset counters
void CallRouteQoS::reset()
{
    DDebug(&__plugin,DebugInfo,"CallRoute::reset() [%p]",this);
    m_totalCalls[CURRENT_IDX] = m_totalCalls[PREVIOUS_IDX] = 0;
    m_answeredCalls[CURRENT_IDX] = m_answeredCalls[PREVIOUS_IDX] = 0;
    m_delivCalls[CURRENT_IDX] = m_delivCalls[PREVIOUS_IDX] = 0;
    m_alarms = 0;
    m_alarmsSent = 0;
    m_alarmCounters[ASR_LOW] = m_alarmCounters[ASR_HIGH] = m_alarmCounters[NER_LOW] = 0;
    for (int i = 0; i < NO_CAUSE - HANGUP; i++)
	m_callCounters[i] = 0;
}

// check a value against a threshold and if necessary set an alarm
void CallRouteQoS::checkForAlarm(int& value, float hysteresis, u_int8_t& alarm, const int min,
			const int max, u_int8_t minAlarm, u_int8_t maxAlarm)
 {
    if (min >= 0) {
	float hystValue = (alarm & minAlarm ? value - hysteresis : value + hysteresis);
	if (hystValue <= min)
	    alarm |= minAlarm;
	else
	    alarm &= ~minAlarm;
    }
    if (max >= 0) {
	float hystValue  = (alarm & maxAlarm ? value + hysteresis : value - hysteresis);
	if (hystValue >= max)
	    alarm |= maxAlarm;
	else
	    alarm &= ~maxAlarm;
    }
}

// is this entry in an alarm state?
bool CallRouteQoS::alarm()
{
    if (m_alarms || m_overallAlarms)
	return true;
    m_alarmsSent = m_overallAlarmsSent = 0;
    return false;
}

// get the string version of the alarm and remember that we sent this alarm (avoid sending the same alarm multiple times)
const String CallRouteQoS::alarmText()
{
    String text = "";
    if (m_alarms & LOW_ASR) {
	if (!(m_alarmsSent & LOW_ASR)) {
	    m_alarmsSent |= LOW_ASR;
	    m_alarmCounters[ASR_LOW]++;
	    return text = lookup(ASR_LOW,s_callQualityQueries,"");
	}
    }
    else
	m_alarmsSent &= ~LOW_ASR;

    if (m_alarms & HIGH_ASR) {
	if (!(m_alarmsSent & HIGH_ASR)) {
	    m_alarmsSent |= HIGH_ASR;
	    m_alarmCounters[ASR_HIGH]++;
	    return text = lookup(ASR_HIGH,s_callQualityQueries,"");
	}
    }
    else
	m_alarmsSent &= ~HIGH_ASR;

    if (m_alarms & LOW_NER) {
	if (!(m_alarmsSent & LOW_NER)) {
	    m_alarmsSent |= LOW_NER;
	    m_alarmCounters[NER_LOW]++;
	    return text = lookup(NER_LOW,s_callQualityQueries,"");
	}
    }
    else
	m_alarmsSent &= ~LOW_NER;

    if (m_overallAlarms & LOW_ASR) {
	if (!(m_overallAlarmsSent & LOW_ASR)) {
	    m_overallAlarmsSent |= LOW_ASR;
	    m_alarmCounters[ASR_LOW_ALL]++;
	    return text = lookup(ASR_LOW_ALL,s_callQualityQueries,"");
	}
    }
    else
	m_overallAlarmsSent &= ~LOW_ASR;

    if (m_overallAlarms & HIGH_ASR) {
	if (!(m_overallAlarmsSent & HIGH_ASR)) {
	    m_overallAlarmsSent |= HIGH_ASR;
    	    m_alarmCounters[ASR_HIGH_ALL]++;
	    return text = lookup(ASR_HIGH_ALL,s_callQualityQueries,"");
	}
    }
    else
	m_overallAlarmsSent &= ~HIGH_ASR;

    if (m_overallAlarms & LOW_NER) {
	if (!(m_overallAlarmsSent & LOW_NER)) {
	    m_overallAlarmsSent |= LOW_NER;
	    m_alarmCounters[NER_LOW_ALL]++;
	    return text = lookup(NER_LOW_ALL,s_callQualityQueries,"");
	}
    }
    else
	m_overallAlarmsSent &= ~LOW_NER;
    return text;
}

// send periodical notification containing current ASR and NER values
void CallRouteQoS::sendNotifs(unsigned int index, bool rst)
{
    DDebug(&__plugin,DebugInfo,"CallRouteQoS::sendNotifs() - route='%s' reset=%s [%p]",toString().c_str(),String::boolText(rst),this);
    // we dont want notifcations if we didn't have the minimum number of calls
    if (m_totalCalls[CURRENT_IDX] >= m_minCalls) {
	NamedList nl("");
	String value;
	nl.addParam("index",String(index));
	nl.addParam("count","4");

	for (int i = 0; i < 4; i++) {
	    String param = "notify.";
	    param << i;
	    String paramValue = "value.";
	    paramValue << i;
	    nl.addParam(param,lookup(ASR + i,s_callQualityQueries,""));
	    get(ASR + i,value);
	    nl.addParam(paramValue,value);
	}
	__plugin.sendTraps(nl);
    }
    if (rst)
	reset();
}

// get the value for a query
bool CallRouteQoS::get(int query, String& result)
{
    DDebug(&__plugin,DebugInfo,"CallRouteQoS::get(query='%s') [%p]",lookup(query,s_callQualityQueries,""),this);
    int val = 0;
    if (query) {
	switch (query) {
	    case ASR:
		if (m_totalCalls[CURRENT_IDX]) {
		    val = (int) (m_answeredCalls[CURRENT_IDX] * 100.0 / m_totalCalls[CURRENT_IDX]);
		    result = String( val);
		}
		else
		    result = "-1";
		return true;
	    case NER:
	    	if (m_totalCalls[CURRENT_IDX]) {
	    	    val = (int) ((m_answeredCalls[CURRENT_IDX] + m_delivCalls[CURRENT_IDX]) * 100.0 / m_totalCalls[CURRENT_IDX]);
		    result = String(val);
		}
		else
		    result = "-1";
		return true;
	    case ASR_ALL:
		if (m_totalCalls[TOTAL_IDX]) {
		    val = (int) (m_answeredCalls[TOTAL_IDX] * 100.0 / m_totalCalls[TOTAL_IDX]);
		    result = String(val);
		}
		else
		    result = "-1";
		return true;
	    case NER_ALL:
	    	if (m_totalCalls[TOTAL_IDX]) {
	    	    val = (int) ((m_answeredCalls[TOTAL_IDX] + m_delivCalls[TOTAL_IDX]) * 100.0 / m_totalCalls[TOTAL_IDX]);
	    	    result = String(val);
	    	}
		else
		    result = "-1";
		return true;
	    case MIN_ASR:
	        result << m_minASR;
	        return true;
	    case MAX_ASR:
		result << m_maxASR;
		return true;
	    case MIN_NER:
	    	result << m_minNER;
	    	return true;
	    case LOW_ASR_COUNT:
	    case HIGH_ASR_COUNT:
	    case LOW_ASR_ALL_COUNT:
	    case HIGH_ASR_ALL_COUNT:
	    case LOW_NER_COUNT:
	    case LOW_NER_ALL_COUNT:
		result << m_alarmCounters[query - LOW_ASR_COUNT + 1];
		return true;
	    case HANGUP:
	    case REJECT:
	    case BUSY:
	    case CANCELLED:
	    case NO_ANSWER:
	    case NO_ROUTE:
	    case NO_CONN:
	    case NO_AUTH:
	    case CONGESTION:
	    case NO_MEDIA:
		result << m_callCounters[query - HANGUP];
		return true;
	    case HANGUP_ALL:
	    case REJECT_ALL:
	    case BUSY_ALL:
	    case CANCELLED_ALL:
	    case NO_ANSWER_ALL:
	    case NO_ROUTE_ALL:
	    case NO_CONN_ALL:
	    case NO_AUTH_ALL:
	    case CONGESTION_ALL:
	    case NO_MEDIA_ALL:
		result << m_callCountersAll[query - HANGUP_ALL];
		return true;
	    case NAME:
		result << toString();
		return true;
	    case INDEX:
		result << m_index;
		return true;
	    default:
		return false;
	}
    }
    return false;
}

/**
  * CallMonitor
  */
CallMonitor::CallMonitor(const NamedList* sect, unsigned int priority)
      : MessageHandler("call.cdr",priority),
	Thread("Call Monitor"),
	m_checkTime(3600),
	m_notifTime(0), m_inCalls(0), m_outCalls(0), m_first(true),
	m_routeParam("address"),
	m_monitor(false)
{
    setFilter("operation","finalize");
    // configure
    setConfigure(sect);

    m_notifTime = Time::secNow() + m_checkTime;
    init();
}

bool CallMonitor::init()
{
    return startup();
}

// main loop. Update the monitor data, if neccessary, send alarms and/or notifications
void CallMonitor::run()
{
    for (;;) {
	check();
	idle();
	bool sendNotif = false;

	m_cfgMtx.lock();
	if (!m_first && Time::secNow() >= m_notifTime) {
	    m_notifTime = Time::secNow() + m_checkTime;
	    sendNotif = true;
	}
	m_cfgMtx.unlock();

	m_routesMtx.lock();
	unsigned int index = 0;
	for (ObjList* o = m_routes.skipNull(); o; o = o->skipNext()) {
	    index++;
	    CallRouteQoS* route = static_cast<CallRouteQoS*>(o->get());
	    route->updateQoS();
	    if (route->alarm())
		sendAlarmFrom(route);
	    if (sendNotif)
		route->sendNotifs(index,true);
	}
	if (sendNotif)
	    sendNotif = false;

	if (m_first)
	    m_first = false;
	m_routesMtx.unlock();
    }
}

// set configuration
void CallMonitor::setConfigure(const NamedList* sect)
{
    if (!sect)
	return;
    m_cfgMtx.lock();
    m_checkTime = sect ? sect->getIntValue("time_interval",3600) : 3600;
    m_routeParam = sect ? sect->getValue("route","address") : "address";
    m_monitor = sect ? sect->getBoolValue("monitor",false) : false;
    if (!m_monitor)
	m_routes.clear();

    // if the previous time for notification is later than the one with the new interval, reset it
    if (m_notifTime > Time::secNow() + m_checkTime)
	m_notifTime = Time::secNow() + m_checkTime;

    s_qosHysteresisFactor = sect ? sect->getDoubleValue("hysteresis_factor",2.0) : 2.0;
    if (s_qosHysteresisFactor > 10 || s_qosHysteresisFactor < 1.0) {
	Debug(&__plugin,DebugNote,"CallMonitor::setConfigure() - configured hysteresis_factor is not in the 1.0 - 10.0 interval,"
		" defaulting to 2.0");
	s_qosHysteresisFactor = 2.0;
    }
    m_cfgMtx.unlock();
}

// add a route to be monitored
void CallMonitor::addRoute(NamedList* cfg)
{
    if (!m_monitor || !cfg)
	return;
    m_routesMtx.lock();
    CallRouteQoS* route = static_cast<CallRouteQoS*>(m_routes[*cfg]);
    if (!route) {
	route = new CallRouteQoS(*cfg,cfg);
        m_routes.append(route);
	route->setIndex(m_routes.count());
    }
    else
	route->updateConfig(cfg);
    m_routesMtx.unlock();
}

void CallMonitor::updateRoutes()
{
    m_routesMtx.lock();
    bool deletedRoute = true;
    while (deletedRoute) {
	deletedRoute = false;
	for (ObjList* o = m_routes.skipNull(); o; o = o->skipNext()) {
	    CallRouteQoS* route = static_cast<CallRouteQoS*>(o->get());
	    if (!route->isCurrent()) {
		DDebug(__plugin.name(),DebugAll,"CallMonitor::updateRoutes() - removed route '%s' from monitoring",route->toString().c_str());
		m_routes.remove(route);
		deletedRoute = true;
	    }
	}
    }
    unsigned int index = 1;
    for (ObjList* o = m_routes.skipNull(); o; o = o->skipNext()) {
	CallRouteQoS* route = static_cast<CallRouteQoS*>(o->get());
	route->setIsCurrent(false);
	route->setIndex(index);
	index++;
    }
    m_routesMtx.unlock();
}

// send an alarm received from a route
void CallMonitor::sendAlarmFrom(CallRouteQoS* route)
{
    if (!route)
	return;
    String alarm = route->alarmText();
    if (!alarm.null())
	__plugin.sendTrap(alarm,route->toString());
}

// extract from a call.cdr message call information and update the monitor data
bool CallMonitor::received(Message& msg)
{
    DDebug(__plugin.name(),DebugAll,"CdrHandler::received()");

    if (m_routeParam.null())
	return false;
    String routeStr = msg.getValue(m_routeParam);
    if (routeStr.null())
	return false;
    if (m_routeParam == YSTRING("address")) {
	int pos = routeStr.rfind(':');
	if (pos < 0)
	    pos = routeStr.rfind('/');
	if (pos > 0)
	    routeStr = routeStr.substr(0,pos);
    }

    const String& status = msg[YSTRING("status")];
    int code = -1;
    if (status == YSTRING("answered"))
	code = CallRouteQoS::ANSWERED;
    else if (status == YSTRING("ringing") || status == YSTRING("accepted"))
	code = CallRouteQoS::DELIVERED;

    const String& direction = msg[YSTRING("direction")];
    bool outgoing = false;
    if (msg.getBoolValue("cdrwrite",true)) {
	if (direction == YSTRING("incoming"))
	    m_inCalls++;
	else if (direction == YSTRING("outgoing")) {
	    outgoing = true;
	    m_outCalls++;
	}
    }

    const String& reason = msg[YSTRING("reason")];
    int type = lookup(reason,s_endReasons,CallRouteQoS::HANGUP);
    if (type == CallRouteQoS::HANGUP && code == CallRouteQoS::DELIVERED && outgoing)
	type = CallRouteQoS::CANCELLED;
    else if (type <= CallRouteQoS::NO_ANSWER && !outgoing)
	type = CallRouteQoS::HANGUP;

    m_routesMtx.lock();
    CallRouteQoS* route = static_cast<CallRouteQoS*>(m_routes[routeStr]);
    if (route)
	route->update(code,type);
    m_routesMtx.unlock();
    return false;
}

// get a value from the call counters
bool CallMonitor::getCounter(int type, unsigned int& value)
{
    DDebug(__plugin.name(),DebugAll,"CallMonitor::getCounter(%s)",lookup(type,s_callCounterQueries,""));
    if (type == 0 || type > ROUTES_COUNT)
	return false;
    switch (type) {
	case INCOMING_CALLS:
	    value = m_inCalls;
	    break;
	case OUTGOING_CALLS:
	    value = m_outCalls;
	    break;
	case ROUTES_COUNT:
	    value = m_routes.count();
	    break;
	default:
	    return false;
    }
    return true;
}

// obtain call monitor specific data
void CallMonitor::get(const String& query, const int& index, String& result)
{
    DDebug(__plugin.name(),DebugAll,"CallMonitor::get(%s,%d)",query.c_str(),index);
    if (index > 0) {
	CallRouteQoS* route = static_cast<CallRouteQoS*>(m_routes[index - 1]);
	if (!route)
	    return;

	int type = lookup(query,s_callQualityQueries,0);
	if (type && route->get(type,result))
	    return;
    }
    int type = lookup(query,s_callCounterQueries,0);
    unsigned int value = 0;
    if (getCounter(type,value)) {
	result += value;
	return;
    }
}

/**
  * Monitor
  */
Monitor::Monitor()
      : Module("monitoring","misc"),
	m_msgUpdateHandler(0),
	m_snmpMsgHandler(0),
	m_hangupHandler(0),
	m_startHandler(0),
	m_callMonitor(0),
	m_authHandler(0),
	m_registerHandler(0),
	m_init(false),
	m_newTraps(false),
	m_sipMonitoredGws(0),
	m_trunkMon(false),
	m_linksetMon(false),
	m_linkMon(false),
	m_interfaceMon(false),
	m_isdnMon(false),
	m_activeCallsCache(0),
	m_trunkInfo(0),
	m_engineInfo(0),
	m_moduleInfo(0),
	m_dbInfo(0),
	m_rtpInfo(0),
	m_linksetInfo(0),
	m_linkInfo(0),
	m_ifaceInfo(0),
	m_accountsInfo(0)
{
    Output("Loaded module Monitoring");
}

Monitor::~Monitor()
{
    Output("Unloaded module Monitoring");

    Debugger::setAlarmHook();

    TelEngine::destruct(m_moduleInfo);
    TelEngine::destruct(m_engineInfo);
    TelEngine::destruct(m_activeCallsCache);
    TelEngine::destruct(m_linkInfo);
    TelEngine::destruct(m_linksetInfo);
    TelEngine::destruct(m_trunkInfo);
    TelEngine::destruct(m_dbInfo);
    TelEngine::destruct(m_rtpInfo);
    TelEngine::destruct(m_ifaceInfo);
    TelEngine::destruct(m_accountsInfo);
    TelEngine::destruct(m_sipMonitoredGws);

    TelEngine::destruct(m_msgUpdateHandler);
    TelEngine::destruct(m_snmpMsgHandler);
    TelEngine::destruct(m_startHandler);
    TelEngine::destruct(m_authHandler);
    TelEngine::destruct(m_registerHandler);
    TelEngine::destruct(m_hangupHandler);
}

bool Monitor::unload()
{
    DDebug(this,DebugAll,"::unload()");
    if (!lock(500000))
	return false;

    Engine::uninstall(m_msgUpdateHandler);
    Engine::uninstall(m_snmpMsgHandler);
    Engine::uninstall(m_startHandler);
    Engine::uninstall(m_authHandler);
    Engine::uninstall(m_registerHandler);
    Engine::uninstall(m_hangupHandler);

    if (m_callMonitor) {
	Engine::uninstall(m_callMonitor);
	m_callMonitor->cancel();
	m_callMonitor = 0;
    }

    uninstallRelays();
    unlock();
    return true;
}

void Monitor::initialize()
{
    Output("Initializing module Monitoring");

    // read configuration
    Configuration cfg(Engine::configFile("monitoring"));

    if (!m_init) {
	m_init = true;
	setup();
	installRelay(Halt);
	installRelay(Timer);
	Debugger::setAlarmHook(alarmCallback);

	s_nodeState = "active";
    }

    if (!m_msgUpdateHandler) {
	m_msgUpdateHandler = new MsgUpdateHandler();
        Engine::install(m_msgUpdateHandler);
    }
    if (!m_snmpMsgHandler) {
	m_snmpMsgHandler = new SnmpMsgHandler();
	Engine::install(m_snmpMsgHandler);
    }
    if (!m_hangupHandler) {
	m_hangupHandler = new HangupHandler();
	Engine::install(m_hangupHandler);
    }
    if (!m_startHandler) {
	m_startHandler = new EngineStartHandler();
	Engine::install(m_startHandler);
    }
    if (!m_authHandler) {
	m_authHandler = new AuthHandler();
	Engine::install(m_authHandler);
    }
    if (!m_registerHandler) {
	m_registerHandler = new RegisterHandler();
	Engine::install(m_registerHandler);
    }

    // build a call monitor
    NamedList* asrCfg = cfg.getSection("call_qos");
    if (!m_callMonitor) {
	m_callMonitor = new CallMonitor(asrCfg);
	Engine::install(m_callMonitor);
    }
    else
	m_callMonitor->setConfigure(asrCfg);

    int cacheFor = cfg.getIntValue("general","cache",1);
    if (!m_activeCallsCache)
	m_activeCallsCache = new ActiveCallsInfo();
    m_activeCallsCache->setRetainInfoTime(cacheFor);//seconds

    if (!m_trunkInfo)
        m_trunkInfo = new TrunkInfo();
    m_trunkInfo->setRetainInfoTime(cacheFor);//seconds

    if (!m_linksetInfo)
	m_linksetInfo = new LinksetInfo();
    m_linksetInfo->setRetainInfoTime(cacheFor);//seconds

    if (!m_linkInfo)
	m_linkInfo = new LinkInfo();
    m_linkInfo->setRetainInfoTime(cacheFor);//seconds

    if (!m_ifaceInfo)
	m_ifaceInfo = new InterfaceInfo();
    m_ifaceInfo->setRetainInfoTime(cacheFor);//seconds

    if (!m_accountsInfo)
	m_accountsInfo = new AccountsInfo();
    m_accountsInfo->setRetainInfoTime(cacheFor);//seconds

    if (!m_engineInfo)
	m_engineInfo = new EngineInfo();
    m_engineInfo->setRetainInfoTime(cacheFor);//seconds

    if (!m_moduleInfo)
	m_moduleInfo = new ModuleInfo();
    m_moduleInfo->setRetainInfoTime(cacheFor);//seconds

    bool enable = cfg.getBoolValue("database","monitor",false);
    if (!m_dbInfo)
	m_dbInfo = new DatabaseInfo(enable);
    else
	m_dbInfo->setMonitorEnabled(enable);
    m_dbInfo->setRetainInfoTime(cacheFor);

    readConfig(cfg);
}

void Monitor::readConfig(const Configuration& cfg)
{
    // get the threshold for yate restart alarm
    s_yateRunAlarm = cfg.getIntValue("general","restart_alarm",1);
    int level = cfg.getIntValue("general","alarm_threshold",DebugNote);
    if (level < DebugFail)
	level = -1;
    else if (level < DebugConf)
	level = DebugConf;
    else if (level > DebugAll)
	level = DebugAll;
    s_alarmThreshold = level;
    m_newTraps = !cfg.getBoolValue("general","old_trap_style");

    // read configs for database monitoring (they type=database, the name of the section is the database account)
    for (unsigned int i = 0; i < cfg.sections(); i++) {
	NamedList* sec = cfg.getSection(i);
	if (!sec || (*sec == "general"))
	    continue;
	String type = sec->getValue("type","");
	if (type.null())
	    continue;
	if (type == "database" && m_dbInfo)
	    m_dbInfo->addDatabase(sec);
	if (type == "call_qos" && m_callMonitor)
	    m_callMonitor->addRoute(sec);
    }
    m_callMonitor->updateRoutes();
    m_dbInfo->updateDatabaseAccounts();

    // read config for SIP monitoring
    String gw = cfg.getValue("sip","gateways","");
    if (!gw.null()) {
	if (m_sipMonitoredGws)
	    TelEngine::destruct(m_sipMonitoredGws);
        m_sipMonitoredGws = gw.split(';',false);
	for (ObjList* o = m_sipMonitoredGws->skipNull(); o; o = o->skipNext()) {
	    String* addr = static_cast<String*>(o->get());
	    int pos = addr->find(":");
	    if (pos == -1)
	        addr->append(":" + String(SIP_PORT));
	    else {
	        String tmp = addr->substr(pos+1);
	        if (tmp.null())
		    addr->append(":" + String(SIP_PORT));
	    }
        }
    }
    s_sipInfo.auths.threshold = cfg.getIntValue("sip","max_failed_auths",0);
    s_sipInfo.transactions.threshold = cfg.getIntValue("sip","max_transaction_timeouts",0);
    s_sipInfo.byes.threshold = cfg.getIntValue("sip","max_byes_timeouts",0);
    s_sipInfo.reset = cfg.getIntValue("sip","reset_time",0);
    if (s_sipInfo.reset)
        s_sipInfo.resetTime = Time::secNow() + s_sipInfo.reset;

    // read SS7 monitoring
    bool sigEnable = cfg.getBoolValue("sig","monitor",false);
    m_trunkMon = cfg.getBoolValue("sig","trunk",sigEnable);
    m_interfaceMon = cfg.getBoolValue("sig","interface",sigEnable);
    m_linksetMon = cfg.getBoolValue("sig","linkset",sigEnable);
    m_linkMon = cfg.getBoolValue("sig","link",sigEnable);
    m_isdnMon = cfg.getBoolValue("sig","isdn",sigEnable);

    // read RTP monitoring
    NamedList* sect = cfg.getSection("rtp");
    if (sect) {
        if (!m_rtpInfo)
	    m_rtpInfo = new RTPTable(sect);
	else
	    m_rtpInfo->reconfigure(sect);
    }
    else
	TelEngine::destruct(m_rtpInfo);

    // read config for MGCP monitoring
    s_mgcpInfo.transactions.threshold = cfg.getIntValue("mgcp","max_transaction_timeouts",0);
    s_mgcpInfo.deletes.threshold = cfg.getIntValue("mgcp","max_deletes_timeouts",0);
    s_mgcpInfo.reset = cfg.getIntValue("mgcp","reset_time",0);
    s_mgcpInfo.gw_monitor = cfg.getBoolValue("mgcp","gw_monitor",false);
    if (s_mgcpInfo.reset)
        s_mgcpInfo.resetTime = Time::secNow() + s_mgcpInfo.reset;

}

// handle messages received by the module
bool Monitor::received(Message& msg, int id)
{
    if (id == Halt) {
	DDebug(this,DebugInfo,"::received() - Halt Message");
	s_nodeState = "exiting";
	unload();
    }
    if (id == Timer) {
	if (m_rtpInfo && m_rtpInfo->shouldReset())
	    m_rtpInfo->reset();
	if (s_sipInfo.resetTime && Time::secNow() > s_sipInfo.resetTime) {
	    s_sipInfo.auths.counter = s_sipInfo.transactions.counter = s_sipInfo.byes.counter = 0;
	    s_sipInfo.auths.alarm = s_sipInfo.transactions.alarm = s_sipInfo.byes.alarm = false;
	    s_sipInfo.resetTime = Time::secNow() + s_sipInfo.reset;
	}
	if (s_mgcpInfo.resetTime && Time::secNow() > s_mgcpInfo.resetTime) {
	    s_mgcpInfo.transactions.counter = s_mgcpInfo.deletes.counter = 0;
	    s_mgcpInfo.transactions.alarm = s_mgcpInfo.deletes.alarm = false;
	    s_mgcpInfo.resetTime = Time::secNow() + s_mgcpInfo.reset;
	}
	if (m_dbInfo)
	    m_dbInfo->reset();
    }
    return Module::received(msg,id);
}

// handle module.update messages
void Monitor::update(Message& msg)
{
    String module = msg.getValue("module","");
    XDebug(this,DebugAll,"Monitor::update() from module=%s",module.c_str());
    int type = lookup(module,s_modules,0);
    switch (type) {
	case DATABASE:
	    if (m_dbInfo)
		m_dbInfo->update(msg);
	    break;
	case PSTN:
	    sendSigNotifs(msg);
	    break;
	case INTERFACE:
	    sendCardNotifs(msg);
	    break;
	case RTP:
	    if (m_rtpInfo)
		m_rtpInfo->update(msg);
	    break;
	case SIP:
	case MGCP:
	    checkNotifs(msg,type);
	    break;
	default:
	    break;
    }
}

// build SS7 notifications
void Monitor::sendSigNotifs(Message& msg)
{
    const String& type = msg[YSTRING("type")];
    const String& name = msg[YSTRING("from")];
    if (type.null() || name.null())
	return;
    // get the type of the notification
    int t = lookup(type,s_sigTypes,0);
    DDebug(this,DebugInfo,"Monitor::sendSigNotifs() - send notification from '%s'",name.c_str());
    bool up = msg.getBoolValue(YSTRING("operational"));
    const char* text = msg.getValue(YSTRING("text"));
    String notif;
    // build trap information
    switch (t) {
	case ISDN:
	    if (m_isdnMon)
		sendTrap(lookup((up ? IsdnQ921Up : IsdnQ921Down),s_sigNotifs),name,0,text);
	    if (!up && m_linkInfo)
		m_linkInfo->updateAlarmCounter(name);
	    break;
	case SS7_MTP3:
	    if (m_linksetMon) {
		sendTrap(lookup((up ? LinksetUp : LinksetDown),s_sigNotifs),name,0,text);
		if (!up && m_linksetInfo)
		    m_linksetInfo->updateAlarmCounter(name);
	    }
	    notif = msg.getValue("link","");
	    if (m_linkMon && !notif.null()) {
		up = msg.getBoolValue(YSTRING("linkup"),false);
		sendTrap(lookup(( up ? LinkUp : LinkDown),s_sigNotifs),notif);
		if (!up && m_linkInfo)
		    m_linkInfo->updateAlarmCounter(name);
	    }
	    break;
	case TRUNK:
	    if (m_trunkMon)
		sendTrap(lookup(( up ? TrunkUp : TrunkDown),s_sigNotifs),name,0,text);
	    if (!up && m_trunkInfo)
		m_trunkInfo->updateAlarmCounter(name);
	    break;
	default:
	    break;
    }
}

// build and send trap from interface notifications
void Monitor::sendCardNotifs(Message& msg)
{
    String device = msg.getValue("interface","");
    DDebug(this,DebugInfo,"::sendCardNotifs() - a notification from interface '%s' has been received",device.c_str());
    if (device.null())
	return;
    String notif = msg.getValue("notify","");
    int type = lookup(notif,s_cardInfo,0);
    if (type && m_interfaceMon) {
	String trap = lookup(type,s_cardNotifs,"");
	if (!trap.null())
	    sendTrap(notif,device);
	    if (m_ifaceInfo)
		m_ifaceInfo->updateAlarmCounter(device);
    }
}

// helper function to check for a value against a threshold from a BaseInfo struct and send a notification if necessary
static void checkInfo(unsigned int count, BaseInfo& info, unsigned int alrm, TokenDict* dict)
{
    DDebug(&__plugin,DebugAll,"checkInfo(count=%d, info={threshold=%d,alarm=%s,counter=%d})",
		count,info.threshold,String::boolText(info.alarm),info.counter);
    info.counter += count;
    if (info.threshold && !info.alarm && info.counter >= info.threshold) {
	info.alarm = true;
	String notif = lookup(alrm,dict,"");
	if (!notif.null())
	    __plugin.sendTrap(notif,String(info.counter));
    }
}

// handle module.update messages from SIP and MGCP
void Monitor::checkNotifs(Message& msg, unsigned int type)
{
    DDebug(&__plugin,DebugAll,"::checkNotifs() from module='%s'",lookup(type,s_modules,""));
    if (type == SIP) {
	unsigned int count = msg.getIntValue("failed_auths",0);
	checkInfo(count,s_sipInfo.auths,FailedAuths,s_sipNotifs);

	count = msg.getIntValue("transaction_timeouts",0);
	checkInfo(count,s_sipInfo.transactions,TransactTimedOut,s_sipNotifs);

	count = msg.getIntValue("bye_timeouts",0);
	checkInfo(count,s_sipInfo.byes,ByesTimedOut,s_sipNotifs);
    }
    if (type == MGCP) {
	unsigned int transTO = msg.getIntValue("tr_timedout",0);
	checkInfo(transTO,s_mgcpInfo.transactions,TransactTimedOut,s_mgcpNotifs);

	transTO = msg.getIntValue("del_timedout",0);
	checkInfo(transTO,s_mgcpInfo.deletes,DeletesTimedOut,s_mgcpNotifs);

	if (s_mgcpInfo.gw_monitor) {
	    if (msg.getValue("mgcp_gw_down"))
		sendTrap(lookup(GWTimeout,s_mgcpNotifs,"mgcpGatewayTimedOut"),msg.getValue("mgcp_gw_down"));
	    if (msg.getValue("mgcp_gw_up"))
		sendTrap(lookup(GWUp,s_mgcpNotifs,"mgcpGatewayUp"),msg.getValue("mgcp_gw_up"));
	}
    }
}

// get SIP/MGCP transaction info
String Monitor::getTransactionsInfo(const String& query, const int who)
{
    String result = "";
    if (who == SIP) {
	int type = lookup(query,s_sipNotifs,0);
	switch (type) {
	    case TransactTimedOut:
		result << s_sipInfo.transactions.counter;
		return result;
	    case FailedAuths:
	    	result << s_sipInfo.auths.counter;
		return result;
	    case ByesTimedOut:
	    	result << s_sipInfo.byes.counter;
		return result;
	    default:
		break;
	}
    }
    else if (who == MGCP) {
        int type = lookup(query,s_mgcpNotifs,0);
        switch (type) {
	    case TransactTimedOut:
		result << s_mgcpInfo.transactions.counter;
		return result;
	    case DeletesTimedOut:
		result << s_mgcpInfo.deletes.counter;
		return result;
	    default:
		break;
	}
    }
    return "";
}

// build a notification message. Increase the alarm counters if the notification was an alarm
void Monitor::sendTrap(const String& trap, const String& value, unsigned int index, const char* text)
{
    DDebug(&__plugin,DebugAll,"Monitor::sendtrap(trap='%s',value='%s',index='%d') [%p]",trap.c_str(),value.c_str(),index,this);
    Message* msg = new Message("monitor.notify",0,true);
    if (m_newTraps)
	msg->addParam("notify","specificAlarm");
    msg->addParam("notify.0",trap);
    msg->addParam("value.0",value);
    if (text && m_newTraps) {
	msg->addParam("notify.1","alarmText");
	msg->addParam("value.1",text);
    }
    if (index)
	msg->addParam("index",String(index));
    Engine::enqueue(msg);
}

// build a notification message. Increase the alarm counters if the notification was an alarm
void Monitor::sendTraps(const NamedList& traps)
{
    Message* msg = new Message("monitor.notify",0,true);
    if (m_newTraps)
	msg->addParam("notify","specificAlarm");
    msg->copyParams(traps);
    Engine::enqueue(msg);
}

// handle a query for a specific monitored value
bool Monitor::solveQuery(Message& msg)
{
    XDebug(__plugin.name(),DebugAll,"::solveQuery()");
    String query = msg.getValue("name","");
    if (query.null())
	return false;
    int queryWho = lookup(query,s_categories,-1);
    String result = "";
    unsigned int index = msg.getIntValue("index",0);
    DDebug(__plugin.name(),DebugAll,"::solveQuery(query=%s, index=%u)",query.c_str(),index);
    switch (queryWho) {
	case DATABASE:
	    if (m_dbInfo)
		result = m_dbInfo->getInfo(query,index,s_databaseQuery);
	    break;
	case CALL_MONITOR:
	    if (m_callMonitor)
		m_callMonitor->get(query,index,result);
	    break;
	case ACTIVE_CALLS:
	    if (m_activeCallsCache)
		result = m_activeCallsCache->getInfo(query,index,s_activeCallInfo);
	    break;
	case TRUNKS:
	    if (m_trunkInfo)
		result = m_trunkInfo->getInfo(query,index,m_trunkInfo->s_trunkInfo);
	    break;
	case LINKSETS:
	    if (m_linksetInfo)
		result = m_linksetInfo->getInfo(query,index,m_linksetInfo->s_linksetInfo);
	    break;
	case LINKS:
	    if (m_linkInfo)
		result = m_linkInfo->getInfo(query,index,m_linkInfo->s_linkInfo);
	    break;
	case IFACES:
	    if (m_ifaceInfo)
		result = m_ifaceInfo->getInfo(query,index,m_ifaceInfo->s_ifacesInfo);
	    break;
	case ACCOUNTS:
	    if (m_accountsInfo)
		result = m_accountsInfo->getInfo(query,index,s_accountInfo);
	    break;
	case ENGINE:
	    if (m_engineInfo)
	        result = m_engineInfo->getInfo(query,index,s_engineQuery);
	    break;
	case MODULE:
	    if (m_moduleInfo)
		result = m_moduleInfo->getInfo(query,index,s_moduleQuery);
	    break;
	case AUTH_REQUESTS:
	    if (m_authHandler)
		result = m_authHandler->getCount();
	    break;
	case REGISTER_REQUESTS:
	    if (m_registerHandler)
	        result = m_registerHandler->getCount();
	    break;
	case RTP:
	    if (m_rtpInfo)
		result = m_rtpInfo->getInfo(query,index);
	    break;
	case SIP:
	case MGCP:
	    result = getTransactionsInfo(query,queryWho);
	    break;
	default:
	    return false;
    }
    msg.setParam("value",result);
    return true;
}

// verify if a call hasn't hangup because of a gateway timeout. In that case, if the gateway was
// monitored send a notification
void Monitor::handleChanHangup(const String& address, int& code)
{
    DDebug(this,DebugInfo,"::handleChanHangup('%s', '%d')",address.c_str(),code);
    if (address.null())
	return;
    if (m_sipMonitoredGws && m_sipMonitoredGws->find(address)) {
	if (code == 408 && !m_timedOutGws.find(address)) {
	    sendTrap(lookup(GWTimeout,s_sipNotifs,"gatewayTimeout"),address);
	    m_timedOutGws.append(new String(address));
	}
    }
}

// if a call has passed through, get the gateway and verify if it was previously down
// if it was send a notification that the gateway is up again
bool Monitor::verifyGateway(const String& address)
{
    if (address.null())
	return false;
    if (m_timedOutGws.find(address)) {
	m_timedOutGws.remove(address);
	sendTrap(lookup(GWUp,s_sipNotifs,"gatewayUp"),address);
    }
    return true;
}

};

/* vi: set ts=8 sw=4 sts=4 noet: */
