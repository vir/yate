/**
 * ysnmpagent.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Module for SNMP protocol agent
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
#include <yatesnmp.h>

#include <string.h>

// values for the different versions of the protocol
#define SNMP_VERSION_1     	0
#define SNMP_VERSION_2C		1
#define SNMP_VERSION_2S		2	// not implemented
#define SNMP_VERSION_3		3

// user security model
#define USM_SEC_MODEL		3

// privacy flags
#define REPORT_FLAG		0x04
#define PRIVACY_FLAG		0x02
#define	AUTH_FLAG		0x01

// maximum value for the agent encryption salt
#define SALT_MAX_VAL		0xffffffffffffffffULL

// maximum time frame window (in seconds) in which a message should be handled
#define TIMEFRAME_VAL		150
// maximum value engine boots, after which it should be reset
#define ENGINE_BOOTS_MAX 	2147483647
// maximum value for engine time, after which it should be reset
#define ENGINE_TIME_MAX		2147483647

#define MSG_MAX_SIZE		65507

using namespace TelEngine;

namespace {

class TransportType;
class SnmpSocketListener;
class SnmpMsgQueue;
class SnmpMessage;
class SnmpAgent;
class SnmpUser;

class SnmpUdpListener;
class TrapHandler;
class AsnMibTree;

/**
  *	TransportType
  */
class TransportType
{
public:
    enum Type {
	UDP	= 0,
	TCP	= 1, // not implemented
//	OSI	= 2, // not implemented
    };
    inline TransportType(int type = UDP)
	: m_type(type)
	{}

    static inline const char* lookupType(int stat, const char* defVal = 0)
	{ return lookup(stat, s_typeText, defVal); }

    static const TokenDict s_typeText[];

    int m_type;
};

/**
  * Abstract class SnmpSocketListener
  */
class SnmpSocketListener : public Thread
{
public:
    inline SnmpSocketListener(const char* addr, int port, SnmpMsgQueue* queue)
	: Thread("SNMP Socket"),
	  m_addr(TelEngine::null(addr) ? "0.0.0.0" : addr),
	  m_port(port), m_msgQueue(queue)
	{}
    inline ~SnmpSocketListener()
	{}
    virtual bool init() = 0;
    virtual void run() = 0;
    virtual bool sendMessage(DataBlock& data, const SocketAddr& to) = 0;
    virtual void cleanup() = 0;

protected:
    Socket m_socket;
    String m_addr;
    int m_port;
    SnmpMsgQueue* m_msgQueue;
};

/**
  * SnmpMsgQueue - class for holding a queue of messages received
  */
class SnmpMsgQueue : public Thread
{
public:
    SnmpMsgQueue(SnmpAgent* agent, Thread::Priority prio = Thread::Normal,
	const char* addr = "0.0.0.0", unsigned int port = 161, int type = TransportType::UDP);
    ~SnmpMsgQueue();
    virtual bool init();
    virtual void run();
    virtual void cleanup();
    virtual void addMsg(unsigned char* msg, int len, SocketAddr& fromAddr);
    virtual bool sendMsg(SnmpMessage* msg);
    void setSocket(SnmpSocketListener* socket);

protected:
    SnmpSocketListener* m_socket;

private:
    TransportType m_type;
    ObjList m_msgQueue;
    Mutex m_queueMutex;
    SnmpAgent* m_snmpAgent;
};

/**
  * SnmpMessage - a wrapper for SNMP messages
  */
class SnmpMessage : public GenObject
{
public:
    inline SnmpMessage()
	{}
    inline SnmpMessage(unsigned char* rawData, unsigned int length, SocketAddr fromAddr)
	:  m_from(fromAddr)
	{ m_data.assign(rawData,length);}
    inline ~SnmpMessage()
	{ }
    inline void setData(const DataBlock& data)
	{ m_data = data;}
    inline DataBlock& data()
	{ return m_data;}
    inline const SocketAddr& peer()
	{ return m_from;}
    inline void setPeer(SocketAddr peer)
	{ m_from = peer;}
private:
    DataBlock  m_data;
    SocketAddr m_from;
};

class SnmpUser : public GenObject
{
public:
    // enum for authentication and privacy encryption
    enum SecurityProtocols {
	MD5_AUTH	=	1,
	SHA1_AUTH	= 	2,
	AES_ENCRYPT	=	3,
	DES_ENCRYPT	=	4
    };
    // Constructor
    SnmpUser(NamedList* cfg);

    // Destructor
    inline ~SnmpUser()
    {}

    inline const String& toString() const
	{ return m_name; }

    inline bool needsAuth()
	{ return !m_authPassword.null(); }
    inline bool needsPriv()
    	{ return !m_privPassword.null(); }
    inline int authProto()
	{ return m_authProto; }
    inline int privProto()
	{ return m_privProto; }
    inline int accessLevel()
	{ return m_accessLevel; }
    inline const DataBlock& digestK1()
	{ return m_k1; }
    inline const DataBlock& digestK2()
	{ return m_k2; }
    inline const DataBlock& authKey()
	{ return m_authKey; }
    inline const DataBlock& privKey()
	{ return m_privKey; }

private:
    String m_name;
    String m_authPassword;
    String m_privPassword;
    int m_authProto;
    int m_privProto;
    int m_accessLevel;

    DataBlock m_authKey;
    DataBlock m_k1;
    DataBlock m_k2;
    DataBlock m_privKey;

    static TokenDict s_access[];
    // generate encryption key
    DataBlock generateAuthKey(String pass = "");
    // generate an authentication information
    void generateAuthInfo();
};

/**
  * SnmpV3MsgContainer - a container for a decoded SNMPv3 message data
  */
class SnmpV3MsgContainer : public GenObject
{
public:
    // Constructor
    inline SnmpV3MsgContainer()
	: m_scopedPdu(0), m_user(0), m_msgEngineBoots(0),
	  m_msgEngineTime(0), m_msgId(0), m_securityModel(0),
	  m_msgMaxSize(MSG_MAX_SIZE), m_privFlag(false), m_authFlag(false), m_reportFlag(false)
    {}
    // Destructor
    inline ~SnmpV3MsgContainer()
	{ TelEngine::destruct(m_scopedPdu);}

    // Return the security parameters from the message
    inline Snmp::UsmSecurityParameters& getSecurity()
	{ return m_security;}
    // reportable flag
    inline bool reportable()
	{ return m_reportFlag;}

    // validate a SNMPv3 message
    int validate(Snmp::SNMPv3Message& msg, int& authRes);
    // handle a request
    int processRequest(Snmp::SNMPv3Message& msg);
    // prepare a message for sending. Generate authentication and privacy information as needed.
    int prepareForSend(Snmp::SNMPv3Message& msg);
    // in case the message exceeds the size constraint for a SNMPv3 message, generate a tooBigMessage response
    int generateTooBigMsg(Snmp::SNMPv3Message& msg);

    // extract information from the message header and verify it
    int processHeader(Snmp::SNMPv3Message& msg);
    // extract and verify authentication and privacy information
    int processSecurityModel(Snmp::SNMPv3Message& msg);
    // proces the pdu from the message
    int processScopedPdu();

    // check the authentication credentials for the given user
    int checkUser();
    // check to see if the message was received in the acceptable timeframe window
    int checkTimeWindow();
    // check digest of the message
    int checkAuth(Snmp::SNMPv3Message& msg);

    // build a message digest
    void msgDigest(Snmp::SNMPv3Message& msg, OctetString& digest);

    // decrypt a pdu
    int decrypt(Snmp::SNMPv3Message& msg);
    // encrypt a given pdu, return encryption result in ecryptedPdu
    int encrypt(Snmp::ScopedPDU* pdu, DataBlock& encryptedPdu);

    // get the maximum size of a message
    inline int msgMaxSize()
	{ return m_msgMaxSize;}

    // set the SNMPv3 user for this message
    inline void setUser(SnmpUser* user)
    {
	m_user = user;
	if (m_user) {
	    m_authFlag = m_user->needsAuth();
	    m_privFlag = m_user->needsPriv();
	}
    }

    inline void setScopedPdu(Snmp::ScopedPDU* scopedPdu)
	{ m_scopedPdu = scopedPdu;}
    inline void setAuthFlag(bool val = true)
	{ m_authFlag = val; }
    inline void setPrivFlag(bool val = true)
	{ m_privFlag = val; }
    inline void setReportFlag(bool val = true)
	{ m_reportFlag = val; }
private:
    // security parameters
    Snmp::UsmSecurityParameters m_security;
    // message pdu
    Snmp::ScopedPDU* m_scopedPdu;
    // user
    SnmpUser* m_user;
     // message "salt"
    DataBlock m_msgSalt;
    // message engine boots
    u_int32_t m_msgEngineBoots;
    // message engine time
    u_int32_t m_msgEngineTime;
    // message id
    int m_msgId;
    // message USM
    int m_securityModel;
    // message maximum size
    int m_msgMaxSize;
    // message flags
    bool m_privFlag;
    bool m_authFlag;
    bool m_reportFlag;
};

/**
  * SnmpAgent
  */
class SnmpAgent : public Module
{
public:
    // type of values
    enum ValType {
	INTEGER,
	STRING,
	OBJECT_ID,
	IPADDRESS,
	COUNTER,
	TIMETICKS,
	ARBITRARY,
	BIG_COUNTER,
	UNSIGNED_INTEGER
    };
    // SNMPv3 process statuses
    enum USMEnum {
	WRONG_COMMUNITY		= -2,
	MESSAGE_DROP		= -1,
	SUCCESS			= 0,
	WRONG_SEC_LEVEL		= 1,
	WRONG_WINDOW_TIME	= 2,
	WRONG_USER 		= 3,
	WRONG_ENGINE_ID		= 4,
	WRONG_DIGEST		= 5,
	WRONG_ENCRYPT		= 6
    };
    // formats for generating a engine ID
    enum EngineFormats {
	IPv4		= 1,
        IPv6		= 2,
	MAC		= 3,
	TEXT		= 4,
	OCTETS		= 5,
	ENTERPRISE 	= 128
    };

    SnmpAgent();
    virtual ~SnmpAgent();
    //inherited methods
    virtual void initialize();
    virtual bool received(Message& msg, int id);
    bool unload();

    inline const OctetString& getEngineID()
	{ return m_engineId;}

    inline u_int32_t getEngineBoots()
	{ return m_engineBoots;}

    inline u_int32_t getEngineTime()
    {
	u_int32_t time = Time::secNow() - m_startTime;
	if (time >= ENGINE_TIME_MAX) {
	    m_engineBoots++;
	    m_startTime += time;
	    time = 0;
	}
	return time;
    }

    // get the salt used for security. Change it's value with each call.
    inline u_int32_t getEngineSalt()
    {
	u_int32_t tmp = (u_int32_t)(m_salt++);
	m_salt = (m_salt == SALT_MAX_VAL ? 0 : m_salt);
	return tmp;
    }

    // process a SNMP message
    int processMsg(SnmpMessage* msg);
    // process a SNMPv2 message
    int processSnmpV2Msg(Snmp::Message& msg, const String& host);
    // decode a pdu and process it accordingly
    void decodePDU(int& reqType, Snmp::PDU* obj, const int& access);
    // process get/getnext/set requests
    int processGetReq(Snmp::VarBind* varBind, AsnValue* value, int& error, const int& access);
    int processGetNextReq(Snmp::VarBind* varBind, AsnValue* value, int& error, const int& access);
    int processSetReq(Snmp::VarBind* varBind, int& error, const int& access);
    // handle a getbulkrequest
    Snmp::PDU* decodeBulkPDU(int& reqType, Snmp::BulkPDU* pdu, const int& access);

    // handle a SNMPv3 message
    int processSnmpV3Msg(Snmp::SNMPv3Message& msg, const String& host, int& authRes);
    // generate reports and responses for SNMPv3 messages
    int generateReport(Snmp::SNMPv3Message& msg, const int& usmRes, SnmpV3MsgContainer& msgContainer);
    int generateResponse(Snmp::SNMPv3Message& msg, SnmpV3MsgContainer& msgContainer);


    Snmp::PDU* getPDU(Snmp::PDUs& p);
    // get a encryption object
    Cipher* getCipher(int criptoType = SnmpUser::DES_ENCRYPT);

    // get&set the value from/for a variable binding
    DataBlock getVal(Snmp::VarBind* varBind);
    void assignValue(Snmp::VarBind* varBind, AsnValue* val);

    // obtain the value for a query
    AsnValue makeQuery(const String& query, unsigned int& index, AsnMib* mib = 0);

    // send in form of a SNMP trap a notification
    bool sendNotification(const String& notif, const String* value = 0,
	unsigned int index = 0, const NamedList* extra = 0);
    // build trap network destination
    SocketAddr buildDestination(const String& ip, const String& port);
    // build a variable bindings list of mandatory OIDs for a trap
    Snmp::VarBindList* addTrapOIDs(const String& notifOID);
    // build SNMPv2 and SNMPv3 traps
    Snmp::SNMPv2_Trap_PDU buildTrapPDU(const String& name, const String* value = 0,
	unsigned int index = 0);
    int buildTrapMsgV3(Snmp::SNMPv3Message& msg, DataBlock pduData);

    // set a value from a varbind
    void setValue(const String& varName, Snmp::VarBind* val, int& error);
    // generate a engineID
    OctetString genEngineId(const int format, String& info);
    // verify if a trap is disabled
    bool trapDisabled(const String& name);

    // verify if a query is in the Yate tree
    bool queryIsSupported(const String& query, AsnMib* mib = 0);
    // obtain a SNMPv3 user
    inline SnmpUser* getUser(const String& user)
	{ return static_cast<SnmpUser*>(m_users[user]); }
    void setMsgQueue(SnmpMsgQueue* queue);
    void authFail(const SocketAddr& addr, int snmpVersion, int reason, int protocol = TransportType::UDP);

private:
    bool m_init;
    SnmpMsgQueue* m_msgQueue;
    String m_lastRecvHost;
    String m_roCommunity;
    String m_rwCommunity;
    String m_rcCommunity;
    AsnMibTree* m_mibTree;
    // msg v3 vars
    OctetString m_engineId;
    u_int32_t m_engineBoots;
    u_int32_t m_startTime;

    //  user security model statistics
    u_int32_t m_stats[7];

    u_int32_t m_silentDrops;
    u_int64_t m_salt;

    TrapHandler* m_trapHandler;
    ObjList* m_traps;

    // SNMP v3 users
    SnmpUser* m_trapUser;
    ObjList m_users;

    // AES and DES ciphers
    Cipher* m_cipherAES;
    Cipher* m_cipherDES;
};

/**
  * SnmpUdpListener - UDP socket for receiving and sending messages
  */
class SnmpUdpListener : public SnmpSocketListener
{
public:
    SnmpUdpListener(const char* addr, int port, SnmpMsgQueue* queue);
    ~SnmpUdpListener();
    virtual bool init();
    virtual void run();
    virtual bool sendMessage(DataBlock& data, const SocketAddr& to);
    virtual void cleanup();
};

/**
  * CipherHolder - class for obtaining an appropriate encryption/decryption object from OpenSSL module
  */
class CipherHolder : public RefObject
{
public:
    inline CipherHolder()
	: m_cipher(0)
	{ }
    virtual ~CipherHolder()
	{ TelEngine::destruct(m_cipher); }
    virtual void* getObject(const String& name) const
	{ return (name == YATOM("Cipher*")) ? (void*)&m_cipher : RefObject::getObject(name); }
    inline Cipher* cipher()
	{ Cipher* tmp = m_cipher; m_cipher = 0; return tmp; }
private:
    Cipher* m_cipher;
};

/**
 * Tree of OIDs.
 */
class AsnMibTree : public GenObject {
    YCLASS(AsnMibTree, GenObject)
public:
    inline AsnMibTree()
	{}
    // Constructor with file name from which the tree is to be built
    AsnMibTree(const String& fileName);
    virtual ~AsnMibTree();
    // Find a MIB object given the object id
    AsnMib* find(const ASNObjId& id);
    // Find a MIB given the MIB name
    AsnMib* find(const String& name);
    // Find the next MIB object in the tree
    AsnMib* findNext(const ASNObjId& id);
    // Get access level for the given object id
    int getAccess(const ASNObjId& oid);
    // Build the tree of MIB objects
    void buildTree();
    //Find the module revision of which this OID is part of
    String findRevision(const String& name);

private:
    String m_treeConf;
    ObjList m_mibs;
};

const TokenDict TransportType::s_typeText[] = {
    {"UDP",	UDP},
    {"TCP",	TCP},
    {0,0}
};

static const TokenDict s_proto[] = {
    {"SNMPv1",	SNMP_VERSION_1},
    {"SNMPv2c",	SNMP_VERSION_2C},
    {"SNMPv3",	SNMP_VERSION_3},
    {0,0}
};

static const TokenDict s_errors[] = {
    {"MESSAGE_DROP",		SnmpAgent::MESSAGE_DROP},
    {"SUCCESS",			SnmpAgent::SUCCESS},
    {"WRONG_SEC_LEVEL",		SnmpAgent::WRONG_SEC_LEVEL},
    {"WRONG_WINDOW_TIME",	SnmpAgent::WRONG_WINDOW_TIME},
    {"WRONG_USER",		SnmpAgent::WRONG_USER},
    {"WRONG_ENGINE_ID",		SnmpAgent::WRONG_ENGINE_ID},
    {"WRONG_DIGEST",		SnmpAgent::WRONG_DIGEST},
    {"WRONG_ENCRYPT",		SnmpAgent::WRONG_ENCRYPT},
    {0,0}
};

static const TokenDict s_readableErrors[] = {
    {"wrong community string",  SnmpAgent::WRONG_COMMUNITY},
    {"message dropped",         SnmpAgent::MESSAGE_DROP},
    {"success",                 SnmpAgent::SUCCESS},
    {"wrong security level",    SnmpAgent::WRONG_SEC_LEVEL},
    {"wrong time window",       SnmpAgent::WRONG_WINDOW_TIME},
    {"unknown user",            SnmpAgent::WRONG_USER},
    {"wrong engine ID",         SnmpAgent::WRONG_ENGINE_ID},
    {"wrong digest",            SnmpAgent::WRONG_DIGEST},
    {"encryption failure",      SnmpAgent::WRONG_ENCRYPT},
    {0,0}
};

static const TokenDict s_stats[] = {
    {"usmStatsUnknownEngineIDs",	SnmpAgent::WRONG_ENGINE_ID},
    {"usmStatsUnknownUserNames",	SnmpAgent::WRONG_USER},
    {"usmStatsWrongDigests",		SnmpAgent::WRONG_DIGEST},
    {"usmStatsUnsupportedSecLevels",	SnmpAgent::WRONG_SEC_LEVEL},
    {"usmStatsDecryptionErrors",	SnmpAgent::WRONG_ENCRYPT},
    {"usmStatsNotInTimeWindows",	SnmpAgent::WRONG_WINDOW_TIME},
    {0,0}
};

static const TokenDict s_crypto[] = {
    {"DES_CBC",		SnmpUser::DES_ENCRYPT},
    {"AES128_CFB",	SnmpUser::AES_ENCRYPT},
    {0,0}
};

static const TokenDict s_pdus[] = {
    {"GetRequest",	Snmp::PDUs::GET_REQUEST},
    {"GetNextRequest",	Snmp::PDUs::GET_NEXT_REQUEST},
    {"GetBulkRequest",	Snmp::PDUs::GET_BULK_REQUEST},
    {"Response",	Snmp::PDUs::RESPONSE},
    {"SetRequest",	Snmp::PDUs::SET_REQUEST},
    {"InformRequest",	Snmp::PDUs::INFORM_REQUEST},
    {"SnmpV2Trap",	Snmp::PDUs::SNMPV2_TRAP},
    {"Report",		Snmp::PDUs::REPORT},
    {0,0}
};

static const TokenDict s_types[] = {
    // ASN.1 built-in types
    {"INTEGER",		AsnValue::INTEGER},
    {"OCTET_STRING",	AsnValue::STRING},
    {"OBJECT_ID",	AsnValue::OBJECT_ID},
    // SNMP v2 SMI
    {"Integer32",	AsnValue::INTEGER},
    {"DisplayString", 	AsnValue::STRING},
    // SNMP v2 SMI tagged types
    {"IpAddress",	AsnValue::IPADDRESS},
    {"Counter32",	AsnValue::COUNTER},
    {"Gauge32",		AsnValue::UNSIGNED_INTEGER},
    {"Unsigned32",	AsnValue::UNSIGNED_INTEGER},
    {"TimeTicks",	AsnValue::TIMETICKS},
    {"Opaque",		AsnValue::ARBITRARY},
    {"Counter64",	AsnValue::BIG_COUNTER},
    {0,0}
};

TokenDict SnmpUser::s_access[] = {
    {"readonly",    AsnMib::readOnly},
    {"readwrite",   AsnMib::readWrite},
    {"readCreate",  AsnMib::readCreate},
    {0,0}
};

static Configuration s_cfg;
static Configuration s_saveCfg;
static bool s_enabledTraps = false;
static u_int8_t s_zero = 0;

static u_int32_t s_pen = 34501;

static SocketAddr s_remote;
static String s_yateRoot;
static String s_yateVersion;

// zero digest
static const DataBlock s_zeroKey(0,12);

INIT_PLUGIN(SnmpAgent);

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow && !__plugin.unload())
	return false;
    return true;
}

/**
  * AsnMibTree
  */
AsnMibTree::AsnMibTree(const String& fileName)
{
    DDebug(&__plugin,DebugAll,"AsnMibTree object created from %s", fileName.c_str());
    m_treeConf = fileName;
    buildTree();
}

AsnMibTree::~AsnMibTree()
{
    m_mibs.clear();
}

void AsnMibTree::buildTree()
{
    Configuration cfgTree;
    cfgTree = m_treeConf;
    if(!cfgTree.load())
	Debug(&__plugin,DebugWarn,"Failed to load MIB tree");
    else {
    	for (unsigned int i = 0; i < cfgTree.sections(); i++) {
    	    NamedList* sect = cfgTree.getSection(i);
    	    if (sect) {
	    	AsnMib* mib = new AsnMib(*sect);
	    	m_mibs.append(mib);
	    }
    	}
    }
}

String AsnMibTree::findRevision(const String& name)
{
    AsnMib* mib = find(name);
    if (!mib)
    	return "";
    String revision = "";
    while (revision.null()) {
    	ASNObjId parentID = mib->getParent();
    	AsnMib* parent = find(parentID);
    	if (!parent)
    	    return revision;
    	revision = parent->getRevision();
    	mib = parent;
    }
    return revision;
}

AsnMib* AsnMibTree::find(const String& name)
{
    DDebug(&__plugin,DebugAll,"AsnMibTree::find('%s')",name.c_str());
    const ObjList *n = m_mibs.skipNull();
    AsnMib* mib = 0;
    while (n) {
	mib = static_cast<AsnMib*>(n->get());
	if (name == mib->getName())
	    break;
	n = n->skipNext();
	mib = 0;
    }
    return mib;
}

AsnMib* AsnMibTree::find(const ASNObjId& id)
{
    DDebug(&__plugin,DebugAll,"AsnMibTree::find('%s')",id.toString().c_str());

    String value = id.toString();
    int pos = 0;
    int index = 0;
    AsnMib* searched = 0;
    unsigned int cycles = 0;
    while (cycles < 2) {
 	ObjList* n = m_mibs.find(value);
	searched = n ? static_cast<AsnMib*>(n->get()) : 0;
	if (searched) {
	    searched->setIndex(index);
	    return searched;
	}
	pos = value.rfind('.');
	if (pos < 0)
	    return 0;
	index = value.substr(pos + 1).toInteger();
	value = value.substr(0,pos);
	cycles++;
    }
    return searched;
}

AsnMib* AsnMibTree::findNext(const ASNObjId& id)
{
    DDebug(&__plugin,DebugAll,"AsnMibTree::findNext('%s')",id.toString().c_str());
    String searchID = id.toString();
    // check it the oid is in our known tree
    AsnMib* root = static_cast<AsnMib*>(m_mibs.get());
    if (root && !(id.toString().startsWith(root->toString()))) {
    	NamedList p(id.toString());
    	AsnMib oid(p);
    	int comp = oid.compareTo(root);
    	if (comp < 0)
    	    searchID = root->toString();
    	else if (comp > 0)
    	    return 0;
    }
    AsnMib* searched = static_cast<AsnMib*>(m_mibs[searchID.toString()]);
    if (searched) {
    	if (searched->getAccessValue() > AsnMib::accessibleForNotify) {
	    DDebug(&__plugin,DebugInfo,"AsnMibTree::findNext('%s') - found an exact match to be '%s'",
			id.toString().c_str(), searched->toString().c_str());
	    return searched;
	}
    }
    String value = searchID.toString();
    int pos = 0;
    int index = 0;
    while (true) {
 	ObjList* n = m_mibs.find(value);
	searched = n ? static_cast<AsnMib*>(n->get()) : 0;
	if (searched) {
	    if (id.toString() == searched->getOID() || id.toString() == searched->toString()) {
	    	ObjList* aux = n->skipNext();
	    	if (!aux)
	    	    return 0;
	    	while (aux) {
		    AsnMib* mib = static_cast<AsnMib*>(aux->get());
		    if (mib && mib->getAccessValue() > AsnMib::accessibleForNotify)
			return mib;
		    aux = aux->skipNext();
	    	}
		return 0;
	    }
	    else {
	    	searched->setIndex(index + 1);
	    	return searched;
	    }
	}
	pos = value.rfind('.');
	if (pos < 0)
	    return 0;
	index = value.substr(pos + 1).toInteger();
	value = value.substr(0,pos);
    }
    return 0;
}

int AsnMibTree::getAccess(const ASNObjId& id)
{
    DDebug(&__plugin,DebugAll,"AsnMibTree::getAccess('%s')",id.toString().c_str());
    AsnMib* mib = find(id);
    if (!mib)
	return 0;
    return mib->getAccessValue();
}

/**
  * TrapHandler - message handler for incoming notifications
  */
class TrapHandler : public MessageHandler
{
public:
    inline TrapHandler(unsigned int priority = 100)
	: MessageHandler("monitor.notify",priority,__plugin.name())
	{ }
    virtual ~TrapHandler()
	{ }
    virtual bool received(Message& msg);
};


static DataBlock toNetworkOrder(u_int64_t val, unsigned int size)
{
    XDebug(&__plugin,DebugAll,"toNetworkOrder(" FMT64 ")",val);
    DataBlock res;
    for (unsigned int i = 0; i < size; i++) {
	DataBlock aux;
	u_int8_t auxInt = (u_int8_t)(val >> (8 * i));
	aux.append(&auxInt,1);
	res.insert(aux);
    }
    return res;
}

/**
  * SnmpUdpListener
  */
SnmpUdpListener::SnmpUdpListener(const char* addr, int port, SnmpMsgQueue* queue)
    : SnmpSocketListener(addr,port,queue)
{
    DDebug(&__plugin,DebugAll,"SnmpUdpListener created for %s:%d",m_addr.safe(),m_port);
}

SnmpUdpListener::~SnmpUdpListener()
{
    DDebug(&__plugin,DebugAll,"SnmpUdpListener for %s:%d destroyed",m_addr.safe(),m_port);
}

bool SnmpUdpListener::init()
{
    SocketAddr addr;

    if (!addr.assign(AF_INET) || !addr.host(m_addr) || !addr.port(m_port)) {
	Alarm(&__plugin,"socket",DebugWarn,"Could not assign values to socket address for SNMP UDP Listener");
	return false;
    }

    if (!m_socket.create(addr.family(),SOCK_DGRAM)) {
	Alarm(&__plugin,"socket",DebugWarn,"Could not create socket for SNMP UDP Listener error %d",
	    m_socket.error());
	return false;
    }

    m_socket.setReuse();

    if (!m_socket.bind(addr)) {
	Alarm(&__plugin,"socket",DebugWarn,"Could not bind SNMP UDP Listener, error %d %s",
	    m_socket.error(),strerror(m_socket.error()));
	return false;
    }
    if (!m_socket.setBlocking(false)) {
	Alarm(&__plugin,"socket",DebugWarn,"Could not set nonblocking SNMP UDP Listener, error %d %s",
	    m_socket.error(),strerror(m_socket.error()));
	return false;
    }
    Debug(&__plugin,DebugInfo,"SNMP UDP Listener initialized on port %d", m_port);
    return startup();
}

void SnmpUdpListener::run()
{
    DDebug(&__plugin,DebugInfo,"SNMP UDP Listener started to run");
    unsigned char buffer[2048];

    for (;;) {
	bool readOk = false;
	bool error = false;

	check();

	if (!m_socket.select(&readOk,0,&error,idleUsec()))
	    continue;

	if (!readOk || error) {
	     if (error)
		Debug(&__plugin,DebugInfo,"SNMP UDP Reading data error: (%d)",m_socket.error());
	     continue;
	}

	SocketAddr from;
	int readSize = m_socket.recvFrom(buffer,sizeof(buffer),from,0);
	if (!readSize) {
	    if (m_socket.canRetry()) {
		idle(true);
		continue;
	    }
	}
	else if (readSize < 0) {
	    if (m_socket.canRetry()) {
		idle(true);
		continue;
	    }
	    cancel();
	    Debug(&__plugin,DebugWarn,"SNMP UDP Read error in SnmpUdpListener [%p]",this);
	    break;
	}

	buffer[readSize] = 0;

	if (m_msgQueue)
	    m_msgQueue->addMsg(buffer,readSize,from);
    }
}

bool SnmpUdpListener::sendMessage(DataBlock& d, const SocketAddr& to)
{
    DDebug(&__plugin,DebugAll,"SnmpUdpListener::sendMessage() of length '%d' to '%s:%d'",d.length(),to.host().c_str(),to.port());

    int len = d.length();
    while (m_socket.valid() && (len > 0)) {

	bool writeOk = false,error = false;

	if (!m_socket.select(0,&writeOk,&error,idleUsec()))
	    continue;
	if (!writeOk || error)
	    continue;
	int w = m_socket.sendTo(d.data(),len,to);
	if (w < 0) {
	    if (!m_socket.canRetry()) {
		// we are shooting ourselves in the foot!
		s_enabledTraps = false;
		Alarm(&__plugin,"socket",DebugWarn,"Could not send message, SNMP disabled!");
		cancel();
		return false;
	    }
	}
	else
	    len -= w;
    }
    return true;
}

void SnmpUdpListener::cleanup()
{
    DDebug(&__plugin,DebugAll,"SnmpUdpListener::cleanup() [%p]",this);
    m_msgQueue->setSocket(0);
}


/**
  * SnmpMsgQueue
  */
SnmpMsgQueue::SnmpMsgQueue(SnmpAgent* agent, Thread::Priority prio, const char* addr, unsigned int port, int type)
    : Thread("SNMP Queue",prio),
      m_socket(0),
      m_queueMutex(false,"SnmpAgent::queue"),
      m_snmpAgent(agent)
{
    Debug(&__plugin,DebugAll,"SnmpMsgQueue created for %s:%d with priority '%s'",addr,port,priority(prio));
    if (type == TransportType::UDP) {
	m_socket = new SnmpUdpListener(addr,port,this);
	if (!m_socket->init()) {
	    delete m_socket;
	    m_socket = 0;
	}
    }
}

SnmpMsgQueue::~SnmpMsgQueue()
{
    DDebug(&__plugin,DebugAll,"~SnmpMsgQueue() [%p]",this);
    m_snmpAgent->setMsgQueue(0);
}

bool SnmpMsgQueue::init()
{
    DDebug(&__plugin,DebugAll,"SnmpMsgQueue::init()");
    if (!m_socket)
	return false;
    return startup();
}

void SnmpMsgQueue::cleanup()
{
    DDebug(&__plugin,DebugAll,"SnmpMsgQueue::cleanup()");
    m_msgQueue.clear();
    if (m_socket)
	m_socket->cancel();
    while (m_socket)
	Thread::idle();
}

void SnmpMsgQueue::setSocket(SnmpSocketListener* socket)
{
    m_queueMutex.lock();
    m_socket = socket;
    m_queueMutex.unlock();
}

void SnmpMsgQueue::run()
{
    while (m_socket && m_snmpAgent) {
	Thread::check();
	SnmpMessage* msg = 0;
	if (m_msgQueue.get()) {
	    m_queueMutex.lock();
	    msg = static_cast<SnmpMessage*>(m_msgQueue.remove(false));
	    m_queueMutex.unlock();
	}
	if (!msg) {
	    idle();
	    continue;
	}

	XDebug(&__plugin,DebugAll,"Processing message [%p]",msg);

	if (m_snmpAgent) {
	    int res = m_snmpAgent->processMsg(msg);
	    if (res < 0)
		Debug(&__plugin,DebugAll,"Error processing message [%p]",msg);
	}
	XDebug(&__plugin,DebugAll,"Processing of message [%p] finished",msg);
	TelEngine::destruct(msg);
    }
}

void SnmpMsgQueue::addMsg(unsigned char* msg, int len, SocketAddr& fromAddr)
{
    XDebug(&__plugin,DebugAll,"SnmpMsgQueue::addMsg() - message received with length %d from address %s:%d",
	len,fromAddr.host().c_str(),fromAddr.port() );

    if(!msg)
	return;
    SnmpMessage* snmpMsg = new SnmpMessage(msg,len,fromAddr);

    m_queueMutex.lock();
    m_msgQueue.append(snmpMsg);
    m_queueMutex.unlock();
}

bool SnmpMsgQueue::sendMsg(SnmpMessage* msg)
{
    DDebug(&__plugin,DebugAll,"SnmpMsgQueue::sendMsg([%p])",msg);
    if (!msg)
	return false;
    DataBlock content = msg->data();
    return m_socket && (content.length() > 0) && m_socket->sendMessage(content,msg->peer());
}

/**
  * TrapHandler
  */
bool TrapHandler::received(Message& msg)
{
    unsigned int index = msg.getIntValue("index",0);
    const String& single = msg[YSTRING("notify")];
    if (single)
        return __plugin.sendNotification(single,msg.getParam(YSTRING("value")),index,&msg);
    bool ok = false;
    int count = msg.getIntValue("count",-1);
    for (int i = 0; ; i++) {
	if (count >= 0 && i >= count)
	    break;
	String param = "notify.";
	param << i;
        const String& notif = msg[param];
        if (!notif.null()) {
	    String paramValue = "value.";
	    paramValue << i;
            ok = __plugin.sendNotification(notif,msg.getParam(paramValue),index) || ok;
        }
        else if (count < 0)
	    break;
    }
    return ok;
}

/**
 * SnmpUser
 */
SnmpUser::SnmpUser(NamedList* cfg)
{
    DDebug(&__plugin,DebugAll,"SnmpUser::SnmpUser(cfg=%p) [%p]",cfg,this);
    if (cfg) {
	m_name = *cfg;
	m_authPassword = cfg->getValue("auth_password","");

    	String proto = cfg->getValue("auth_protocol","MD5");
	m_authProto = (proto == "MD5" ? MD5_AUTH : SHA1_AUTH);

	m_privPassword = cfg->getValue("priv_password","");

	proto = cfg->getValue("priv_protocol","DES");
	m_privProto = (proto == "DES" ? DES_ENCRYPT : AES_ENCRYPT);

	// get the user's privilege level
	String access = cfg->getValue("access","readonly");
	m_accessLevel = lookup(access,s_access);

	if (needsAuth())
	    generateAuthInfo();
    }
}

DataBlock SnmpUser::generateAuthKey(String password)
{
    DataBlock b;
    if (TelEngine::null(password))
	return b;
    DDebug(&__plugin,DebugAll,"SnmpUser::generateAuthKey(user=%s) [%p]",m_name.c_str(),this);
    DataBlock authKey(0,64);

    MD5 digestMD5;
    SHA1 digestSHA;
    int count = 0;
    int passIndex = 0;
    int len = password.length();
    // initialization
    unsigned char ku[64];
    while (count < 1048576) {	// 1MB
	for (int i = 0; i < 64; i++)
	   ku[i] = password[passIndex++ % len];
	count += 64;
	if (m_authProto == MD5_AUTH) {
	    digestMD5.update(ku,64);
	    continue;
	}
	if (m_authProto == SHA1_AUTH)
	    digestSHA.update(ku,64);
    }

    DataBlock aux;
    if (m_authProto == MD5_AUTH) {
	digestMD5.finalize();
	aux.append((void*)digestMD5.rawDigest(), digestMD5.rawLength());
	digestMD5.clear();
    }
    else if (m_authProto == SHA1_AUTH) {
	digestSHA.finalize();
	aux.append((void*)digestSHA.rawDigest(), digestSHA.rawLength());
	digestSHA.clear();
    }
    authKey.clear();

    // key localization
    authKey.append(aux);
    authKey.append(__plugin.getEngineID());
    authKey.append(aux);

    // obtain final key according to encryption method
    if (m_authProto == SnmpUser::MD5_AUTH) {
	digestMD5.update(authKey);
	digestMD5.finalize();
	authKey.clear();
	authKey.append((void*)digestMD5.rawDigest(),digestMD5.rawLength());
	authKey.truncate(16);
	DDebug(&__plugin,DebugAll,"SnmpV3MsgContainer::generateAuthKey() [%p] - MD5 authKey generated %s",
	      this,digestMD5.hexDigest().substr(0,32).c_str());
	return authKey;
    }
    else if (m_authProto == SnmpUser::SHA1_AUTH) {
	digestSHA.update(authKey);
	digestSHA.finalize();
	authKey.clear();
	authKey.append((void*)digestSHA.rawDigest(),digestSHA.rawLength());
	authKey.truncate(20);
	DDebug(&__plugin,DebugAll,"SnmpV3MsgContainer::generateAuthKey() [%p] - SHA authKey generated %s",
		this,digestSHA.hexDigest().substr(0,40).c_str());
	return authKey;
    }
    Debug(&__plugin,DebugInfo,"::generateAuthKey() [%p] - invalid auth protocol",this);
    m_authKey.clear();
    return authKey;
}

// generate an authentication information
void SnmpUser::generateAuthInfo()
{
    if (TelEngine::null(m_authPassword))
	return;

    m_authKey = generateAuthKey(m_authPassword);
    m_k1.clear();
    m_k2.clear();
    for (unsigned int i = 0; i < 64; i++) {
	u_int8_t val = 0;
	if (i < m_authKey.length())
	    val = m_authKey[i];
	u_int8_t x1 = val ^ 0x36;
	u_int8_t x2 = val ^ 0x5c;
	m_k1.append(&x1,1);
	m_k2.append(&x2,1);
    }
    m_privKey = generateAuthKey(m_privPassword);
}

/**
  * SnmpV3MsgContainer
  */

// validate a message
int SnmpV3MsgContainer::validate(Snmp::SNMPv3Message& msg, int& authRes)
{
    DDebug(&__plugin,DebugAll,"SnmpV3MsgContainer::validate() [%p]",this);
    int res = processHeader(msg);
    if (res != SnmpAgent::SUCCESS)
	return res;
    res = processSecurityModel(msg);
    if (res != SnmpAgent::SUCCESS)
	return res;

    // if the auth flag is set, check the digest for the message
    if (m_authFlag) {
	res = checkAuth(msg);
	if (res != SnmpAgent::SUCCESS) {
	    authRes = res;
	    return res;
	}
    }
    // check the user data
    res = checkUser();
    if (res != SnmpAgent::SUCCESS) {
	authRes = res;
	return res;
    }
    // if the privacy flag is set, decrypt the message
    if (m_privFlag) {
	res = decrypt(msg);
	if (res != SnmpAgent::SUCCESS)
	    return res;
    }
    Debug(&__plugin,DebugAll,"SnmpV3MsgContainer::validate() [%p] - message %p validated",this,&msg);
    return res;
}
// handle the request fom the SNMPv3 message
int SnmpV3MsgContainer::processRequest(Snmp::SNMPv3Message& msg)
{
    DDebug(&__plugin,DebugAll,"SnmpV3MsgContainer::processRequest() [%p]",this);
    if (msg.m_msgData && msg.m_msgData->m_choiceType == Snmp::ScopedPduData::PLAINTEXT)
	m_scopedPdu = msg.m_msgData->m_plaintext;
    processScopedPdu();
    return SnmpAgent::SUCCESS;
}

// prepare for sending a SNMPv3 message
int SnmpV3MsgContainer::prepareForSend(Snmp::SNMPv3Message& msg)
{
    DDebug(&__plugin,DebugAll,"SnmpV3MsgContainer::prepareForSend() [%p]",this);

    // set the message flags
    u_int8_t msgFlags = 0;
    if (m_reportFlag)
	msgFlags &= ~REPORT_FLAG;
    if (m_authFlag)
	msgFlags |= AUTH_FLAG;
    if (m_privFlag)
	msgFlags |= PRIVACY_FLAG;
    if (!msg.m_msgGlobalData)
	return SnmpAgent::MESSAGE_DROP;
    msg.m_msgGlobalData->m_msgFlags.assign(&msgFlags,sizeof(msgFlags));

    // make sure auth and encrypt parameters are empty
    m_security.m_msgPrivacyParameters.clear();
    m_security.m_msgAuthenticationParameters.clear();
    m_security.m_msgAuthoritativeEngineID = __plugin.getEngineID();
    m_security.m_msgAuthoritativeEngineTime = __plugin.getEngineTime();
    m_security.m_msgAuthoritativeEngineBoots = __plugin.getEngineBoots();

    if (!m_user && m_authFlag)
	return SnmpAgent::MESSAGE_DROP;
    // if the privacy flag is set, encrypt the pdu
    if (m_user && m_privFlag && m_user->needsPriv()) {
	if (!msg.m_msgData)
	    msg.m_msgData = new Snmp::ScopedPduData();
	msg.m_msgData->m_encryptedPDU.clear();
	encrypt(m_scopedPdu,msg.m_msgData->m_encryptedPDU);
	msg.m_msgData->m_choiceType = Snmp::ScopedPduData::ENCRYPTEDPDU;
	m_security.m_msgPrivacyParameters.append(m_msgSalt);
    }
    // if the auth flag is set, compute the message digest and set it in the message
    if (m_user && m_authFlag && m_user->needsAuth()) {
	m_security.m_msgAuthenticationParameters = s_zeroKey;
	OctetString digest;
	msgDigest(msg,digest);
	m_security.m_msgAuthenticationParameters = digest;
    }
    // encode and set the security parameters
    DataBlock data;
    msg.m_msgSecurityParameters.clear();
    m_security.encode(msg.m_msgSecurityParameters);
    return SnmpAgent::SUCCESS;
}

// build a tooBigMessage
int SnmpV3MsgContainer::generateTooBigMsg(Snmp::SNMPv3Message& msg)
{
    Debug(&__plugin,DebugInfo,"SnmpV3MsgContainer::generateTooBigMsg() [%p]",this);
    if (!m_scopedPdu)
	return SnmpAgent::MESSAGE_DROP;
    DataBlock data = m_scopedPdu->m_data;
    Snmp::PDUs pdus;
    pdus.decode(data);
    Snmp::PDU* pdu = __plugin.getPDU(pdus);
    if (!pdu)
	return SnmpAgent::MESSAGE_DROP;
    pdu->m_error_status = Snmp::PDU::s_tooBig_error_status;
    pdu->m_error_index = 0;
    if (!pdu->m_variable_bindings)
	pdu->m_variable_bindings = new Snmp::VarBindList();
    pdu->m_variable_bindings->m_list.clear();
    data.clear();
    pdus.encode(data);
    m_scopedPdu->m_data.clear();
    m_scopedPdu->m_data.append(data);
    prepareForSend(msg);
    return SnmpAgent::SUCCESS;
}

// parse the header data from the message
int SnmpV3MsgContainer::processHeader(Snmp::SNMPv3Message& msg)
{
    Snmp::HeaderData* header = msg.m_msgGlobalData;
    if (!header) {
	Debug(&__plugin,DebugInfo,"SnmpV3MsgContainer::processHeader() - no header [%p]",this);
	return SnmpAgent::MESSAGE_DROP;
    }
    DDebug(&__plugin,DebugAll,"SnmpV3MsgContainer::processHeader('%p')",header);
    // * msgId
    m_msgId = header->m_msgID;
    // * msgMaxSize
    m_msgMaxSize = header->m_msgMaxSize;
    // * msgFlags
    u_int8_t msgFlags = (u_int8_t)(header->m_msgFlags.length() == 1 ? header->m_msgFlags[0] : 0);

    // get the message flags
    m_reportFlag = ((msgFlags &  REPORT_FLAG) == 0x0 ? false : true);
    m_privFlag = ((msgFlags & PRIVACY_FLAG) == 0x0 ? false : true);
    m_authFlag = ((msgFlags & AUTH_FLAG) == 0x0 ? false : true);
    // * msgSecurityModel
    m_securityModel = header->m_msgSecurityModel;
    if (m_securityModel != USM_SEC_MODEL) {
	Debug(&__plugin,DebugInfo,"SnmpV3MsgContainer::processHeader() [%p] - invalid security model=%d",this,m_securityModel);
	return SnmpAgent::MESSAGE_DROP;
    }
    DDebug(&__plugin,DebugInfo,"SnmpV3MsgContainer::processHeader() found msgID = %d, m_msgMaxSize = %d, "
	  "reportFlag = %s, privFlag = %s, authFlag = %s",m_msgId,m_msgMaxSize,String::boolText(m_reportFlag),
	  String::boolText(m_privFlag),String::boolText(m_authFlag));

    return SnmpAgent::SUCCESS;
}

// parse and handle the security data
int SnmpV3MsgContainer::processSecurityModel(Snmp::SNMPv3Message& msg)
{
    DDebug(&__plugin,DebugAll,"SnmpV3MsgContainer::processSecurityModel() [%p]",this);

    int r = m_security.decode(msg.m_msgSecurityParameters);
    if (r < 0)
	return SnmpAgent::MESSAGE_DROP;

    // extract from the message the engineID, engineBoots/Time and the username
    OctetString authEngineId = m_security.m_msgAuthoritativeEngineID;
    m_msgEngineBoots = m_security.m_msgAuthoritativeEngineBoots;
    m_msgEngineTime = m_security.m_msgAuthoritativeEngineTime;

    m_user = __plugin.getUser(m_security.m_msgUserName.getString());

    DDebug(&__plugin,DebugInfo,"SnmpV3MsgContainer::processSecurityModel found authEngineId = '%s', engineBoots = '%d', "
	"engineTime = '%d', username = '%s'", authEngineId.toHexString().c_str(),
	    m_msgEngineBoots,m_msgEngineTime,(m_user ? m_user->toString().c_str() : ""));

    // check the engine data and if it doesn't match set the correct data and return a wrong engine id status
    if (authEngineId.toHexString() != __plugin.getEngineID().toHexString())
	return  SnmpAgent::WRONG_ENGINE_ID;

    int res = checkTimeWindow();
    if (res != SnmpAgent::SUCCESS)
	return res;
    return SnmpAgent::SUCCESS;
}

// verify the message time against the engine time and the time window
int SnmpV3MsgContainer::checkTimeWindow()
{
    DDebug(&__plugin,DebugAll,"SnmpV3MsgContainer::checkTimeWindow() [%p]",this);
    u_int32_t engineBoots = __plugin.getEngineBoots();
    if (engineBoots == ENGINE_BOOTS_MAX || engineBoots != m_msgEngineBoots)
	return SnmpAgent::WRONG_WINDOW_TIME;

    int32_t engineTime = (int32_t)__plugin.getEngineTime();
    if (((engineTime - TIMEFRAME_VAL) > (int32_t)m_msgEngineTime) || ((engineTime + TIMEFRAME_VAL) < (int32_t)m_msgEngineTime))
	return SnmpAgent::WRONG_WINDOW_TIME;
    return SnmpAgent::SUCCESS;
}

// check the user data provided in the message against locally stored data
int SnmpV3MsgContainer::checkUser()
{
    if (!m_user) {
	Debug(&__plugin,DebugInfo,"SnmpV3MsgContainer::checkUser() - Unknown user name [%p]",this);
	return SnmpAgent::WRONG_USER;
    }
    DDebug(&__plugin,DebugAll,"SnmpV3MsgContainer::checkUser('%s') [%p]",m_user->toString().c_str(),this);

    if (m_authFlag != m_user->needsAuth()) {
	Debug(&__plugin,DebugInfo,"SnmpV3MsgContainer::checkUser() [%p] - Unsupported security level 'auth' for user %s",
		this,m_user->toString().c_str());
	return  SnmpAgent::WRONG_SEC_LEVEL;
    }

    if (m_privFlag != m_user->needsPriv()) {
	Debug(&__plugin,DebugInfo,"SnmpV3MsgContainer::checkUser() [%p] - Unsupported security level 'priv' for user %s",
		this,m_user->toString().c_str());
	return  SnmpAgent::WRONG_SEC_LEVEL;
    }
    return SnmpAgent::SUCCESS;
}

// handle the pdu from the SNMPv3 message
int SnmpV3MsgContainer::processScopedPdu()
{
    DDebug(&__plugin,DebugAll,"SnmpV3MsgContainer::processScopedPdu(scopedPdu=%p) [%p]",m_scopedPdu,this);
    if (!(m_scopedPdu && m_user))
	return SnmpAgent::MESSAGE_DROP;

    Snmp::PDUs pdus;
    pdus.decode(m_scopedPdu->m_data);
    int type = pdus.m_choiceType;

    Snmp::PDU* decodedPdu = 0;
    if (type != Snmp::PDUs::GET_BULK_REQUEST) {
	decodedPdu = __plugin.getPDU(pdus);
	__plugin.decodePDU(type,decodedPdu,m_user->accessLevel());
	pdus.m_choiceType = type;
    }
    else {
	Snmp::BulkPDU* bulkReq = pdus.m_get_bulk_request->m_GetBulkRequest_PDU;
	if (bulkReq) {
	    // handle bulk request
	    decodedPdu = __plugin.decodeBulkPDU(type,bulkReq,m_user->accessLevel());
	    type = pdus.m_choiceType = Snmp::PDUs::RESPONSE;
	}
    }
    if (type == Snmp::PDUs::RESPONSE) {
	TelEngine::destruct(pdus.m_response->m_Response_PDU);
	pdus.m_response->m_Response_PDU = (decodedPdu ? decodedPdu : new Snmp::PDU());
    }

    m_scopedPdu->m_data.clear();
    pdus.encode(m_scopedPdu->m_data);
    return SnmpAgent::SUCCESS;
}

// check the digest received in the message
int SnmpV3MsgContainer::checkAuth(Snmp::SNMPv3Message& msg)
{
    if (!m_user)
	return SnmpAgent::WRONG_USER;
    DDebug(&__plugin,DebugAll,"SnmpV3MsgContainer::checkAuth('%s') [%p]",m_user->toString().c_str(),this);
    //put digest on zero
    OctetString authDigest = m_security.m_msgAuthenticationParameters;
    m_security.m_msgAuthenticationParameters = s_zeroKey;
    OctetString digest;
    msgDigest(msg,digest);

    if (digest.toHexString() != authDigest.toHexString()) {
	Debug(&__plugin,DebugInfo,"SnmpV3MsgContainer::checkAuth('%s') [%p] - wrong digest received on wire",m_user->toString().c_str(),this);
	return SnmpAgent::WRONG_DIGEST;
    }
    DDebug(&__plugin,DebugInfo,"SnmpV3MsgContainer::checkAuth('%s') [%p] - AUTH SUCCESS",m_user->toString().c_str(),this);
    return SnmpAgent::SUCCESS;
}

// compute a message digest
void SnmpV3MsgContainer::msgDigest(Snmp::SNMPv3Message& msg, OctetString& digest)
{
    digest.clear();
    if (!m_user)
	return;
    if (!m_user->needsAuth())
	return;
    DDebug(&__plugin,DebugAll,"SnmpV3MsgContainer::msgDigest(%s) [%p]",m_user->toString().c_str(),this);

    const DataBlock& k1 = m_user->digestK1();
    const DataBlock& k2 = m_user->digestK2();

    m_security.encode(digest);
    msg.m_msgSecurityParameters.clear();
    msg.m_msgSecurityParameters.append(digest);
    digest.clear();
    msg.encode(digest);

    //  md51 = MD5 digest on  k1 + msg, then md52 = MD5 digest on k2 + md51
    // msgDigest = md52[0..11]
    //  sha1 = SHA1 digest on  k1 + msg, then sha2 = SHA1 digest on k2 + sha1
    // msgDigest = sha2[0..11]
    if (m_user->authProto() == SnmpUser::MD5_AUTH) {
	MD5 md5;
	md5.update(k1);
	md5.update(digest);
	md5.finalize();

	digest.clear();
	digest.append((void*)md5.rawDigest(),md5.rawLength());

	md5.clear();
	md5.update(k2);
	md5.update(digest);
	md5.finalize();

	digest.clear();
	digest.append((void*)md5.rawDigest(),md5.rawLength());
	digest.truncate(12);
	DDebug(&__plugin,DebugAll,"SnmpV3MsgContainer::msgDigest()[%p]:MD5 digest is %s",this,md5.hexDigest().substr(0,24).c_str());
    }
    else if (m_user->authProto() == SnmpUser::SHA1_AUTH) {
	SHA1 sha1;
	sha1.update(k1);
	sha1.update(digest);
	sha1.finalize();

	digest.clear();
	digest.append((void*)sha1.rawDigest(),sha1.rawLength());

	sha1.clear();
	sha1.update(k2);
	sha1.update(digest);
	sha1.finalize();

	digest.clear();
	digest.append((void*)sha1.rawDigest(),sha1.rawLength());
	digest.truncate(12);
	DDebug(&__plugin,DebugAll,"SnmpV3MsgContainer::msgDigest() [%p] : SHA1 digest is %s",this,sha1.hexDigest().substr(0,24).c_str());
    }
}

// decrypt an encrypted pdu
int SnmpV3MsgContainer::decrypt(Snmp::SNMPv3Message& msg)
{
    if(msg.m_msgData && msg.m_msgData->m_choiceType != Snmp::ScopedPduData::ENCRYPTEDPDU)
	return SnmpAgent::WRONG_ENCRYPT;
    if (!msg.m_msgData || !m_user)
	return SnmpAgent::MESSAGE_DROP;
    DDebug(&__plugin,DebugAll,"SnmpV3MsgContainer::decrypt() [%p]",this);

    DataBlock encryptedBlock = msg.m_msgData->m_encryptedPDU;
    // get privacy key
    DataBlock privKey = m_user->privKey();
    DataBlock encryptKey = privKey;
    DataBlock initVector;

    // build the initialization vector from the key according to encryption method
    if (m_user->privProto() == SnmpUser::DES_ENCRYPT) {
	if (encryptedBlock.length() % 8 != 0 )
	    return SnmpAgent::WRONG_ENCRYPT;
	encryptKey.truncate(8);
	DataBlock preIV = privKey;
	preIV.truncate(16);
	preIV.cut(-8);
	//m_security.m_msgPrivacyParameters is the salt
	for (unsigned int i  = 0; i < m_security.m_msgPrivacyParameters.length(); i++) {
	    u_int8_t aux = preIV[i] ^ m_security.m_msgPrivacyParameters[i];
	    initVector.append(&aux,1);
	}
    }
    if (m_user->privProto() == SnmpUser::AES_ENCRYPT) {
	encryptKey.truncate(16);
 	DataBlock aux = toNetworkOrder(m_msgEngineBoots,4);
 	initVector.append(aux);
 	aux = toNetworkOrder(m_msgEngineTime,4);
 	initVector.append(aux);
  	initVector.append(m_security.m_msgPrivacyParameters);
    }

    //rfc3826
    Cipher* cipher = __plugin.getCipher(m_user->privProto());
    if (!cipher){
	Debug(&__plugin,DebugInfo,"Could not obtain %s cipher",lookup(m_user->privProto(),s_crypto,""));
	return SnmpAgent::WRONG_ENCRYPT;
    }
    // set the decrypt key
    if (!cipher->setKey(encryptKey))
	return SnmpAgent::WRONG_ENCRYPT;
    // set the initialization vector
    if (!cipher->initVector(initVector))
	return SnmpAgent::WRONG_ENCRYPT;
    // decrypt the data
    cipher->decrypt(encryptedBlock);
    // decode the pdu from the data
    m_scopedPdu = new Snmp::ScopedPDU();
    if(m_scopedPdu->decode(encryptedBlock) < 0)
	return SnmpAgent::WRONG_ENCRYPT;

    return SnmpAgent::SUCCESS;
}

// encrypt the given pdu
int SnmpV3MsgContainer::encrypt(Snmp::ScopedPDU* pdu, DataBlock& encryptedPdu)
{
    DDebug(&__plugin,DebugAll,"SnmpV3MsgContainer::encrypt() pdu=[%p] [%p]",pdu,this);
    if (!pdu ||!m_user)
	return SnmpAgent::MESSAGE_DROP;

    pdu->encode(encryptedPdu);

    // obtain the engine salt
    m_msgSalt.clear();
    u_int64_t engineSalt = __plugin.getEngineSalt();
    m_msgSalt = toNetworkOrder(engineSalt,8);

    DataBlock privKey = m_user->privKey();
    DataBlock encryptKey = privKey;
    // build the initialization vector
    DataBlock initVector;
    if (m_user->privProto() == SnmpUser::DES_ENCRYPT) {
	encryptKey.truncate(8);
	DataBlock preIV = privKey;
	preIV.truncate(16);
	preIV.cut(-8);
	for (unsigned int i  = 0; i < m_msgSalt.length(); i++) {
	    u_int8_t aux = preIV[i] ^ m_msgSalt[i];
	    initVector.append(&aux,1);
	}
	int r = encryptedPdu.length() % 8;
	if(r != 0)
	    for (int i = 0; i < 8 - r; i++)
		encryptedPdu.append(&s_zero,1);
    }
    else if (m_user->privProto() == SnmpUser::AES_ENCRYPT) {
	encryptKey.truncate(16);
	DataBlock aux = toNetworkOrder(m_msgEngineBoots,4);
	initVector.append(aux);
	aux = toNetworkOrder(m_msgEngineTime,4);
	initVector.append(aux);
	initVector.append(m_msgSalt);
    }

    Cipher* cipher = __plugin.getCipher(m_user->privProto());
    if (!cipher) {
	Debug(&__plugin,DebugInfo,"Could not obtain '%s' cipher",lookup(m_user->privProto(),s_crypto,""));
	return SnmpAgent::MESSAGE_DROP;
    }

    // set the encryption key
    if (!cipher->setKey(encryptKey))
	return SnmpAgent::MESSAGE_DROP;

    // set the initialization vector
    if (!cipher->initVector(initVector))
	return SnmpAgent::MESSAGE_DROP;

    // encrypt the data
    cipher->encrypt(encryptedPdu);
    return SnmpAgent::SUCCESS;
}

/**
  * SnmpAgent
  */
SnmpAgent::SnmpAgent()
      : Module("snmpagent","misc"),
	m_init(false), m_msgQueue(0), m_mibTree(0),
	m_engineBoots(0),m_startTime(0), m_silentDrops(0),
	m_salt(0),
	m_trapHandler(0),
	m_traps(0),
	m_trapUser(0),
	m_cipherAES(0),
	m_cipherDES(0)
{
    Output("Loaded module SNMP Agent");
}

SnmpAgent::~SnmpAgent()
{
    Output("Unloaded module SNMP Agent");
    TelEngine::destruct(m_trapHandler);
    TelEngine::destruct(m_traps);
    TelEngine::destruct(m_cipherAES);
    TelEngine::destruct(m_cipherDES);
    TelEngine::destruct(m_mibTree);
    TelEngine::destruct(m_trapUser);
}

bool SnmpAgent::unload()
{
    Debug(this,DebugAll,"::unload()");
    if (!lock(500000))
	return false;

    uninstallRelays();
    Engine::uninstall(m_trapHandler);

    if (m_traps) {
        String traps = "";
        for (ObjList* o = m_traps->skipNull(); o; o = o->skipNext()) {
            String* str = static_cast<String*>(o->get());
            traps.append(*str,",");
        }
        s_saveCfg.setValue("traps_conf","traps_disable",traps);
        s_saveCfg.save();
    }

    if (m_msgQueue)
	m_msgQueue->cancel();
    m_users.clear();
    unlock();
    while (m_msgQueue)
	Thread::idle();
    return true;
}

void SnmpAgent::setMsgQueue(SnmpMsgQueue* queue)
{
    lock();
    m_msgQueue = queue;
    unlock();
}

void SnmpAgent::initialize()
{
    Output("Initializing module SNMP Agent");

    s_cfg = Engine::configFile("ysnmpagent");
    s_cfg.load();

    // load community strings for SNMPv2 or prior
    m_roCommunity = s_cfg.getValue("snmp_v2","ro_community","");
    m_rwCommunity = s_cfg.getValue("snmp_v2","rw_community","");
    m_rcCommunity = s_cfg.getValue("snmp_v2","rc_community","");

    // load the file containing the Yate OID and initialize the tree
    String treeConf = s_cfg.getValue("general","mibs");
    if (treeConf.null())
	treeConf << Engine::sharedPath() << Engine::pathSeparator() <<
	    "data" << Engine::pathSeparator() << "snmp_mib.conf";
    // in case of reinitialization, first destroy the previously allocated object
    TelEngine::destruct(m_mibTree);
    m_mibTree = new AsnMibTree(treeConf);

    // get information needed for the computation of the agents' engine id (SNMPv3)
    int engineFormat = s_cfg.getIntValue("snmp_v3","engine_format",TEXT);
    const char* defaultInfo = (TEXT == engineFormat) ? Engine::nodeName().c_str() : 0;
    String engineInfo = s_cfg.getValue("snmp_v3","engine_info",defaultInfo);
    m_engineId = genEngineId(engineFormat,engineInfo);

    // read configuration for traps
    s_enabledTraps = s_cfg.getBoolValue("traps","enable_traps",true);
    String remoteIP = s_cfg.getValue("traps","remote_ip","localhost");
    String remotePort = s_cfg.getValue("traps","remote_port","162");
    s_remote = buildDestination(remoteIP,remotePort);
    if (!s_remote.valid())
	s_enabledTraps = false;
    // initiate the user for sending SNMPv3 traps
    String trapUser = s_cfg.getValue("traps","trap_user","");
    TelEngine::destruct(m_trapUser);
    if (!TelEngine::null(trapUser) && s_cfg.getSection(trapUser))
	m_trapUser = new SnmpUser(s_cfg.getSection(trapUser));

    for (unsigned int i = 0; i < s_cfg.sections(); i++) {
	NamedList* sec = s_cfg.getSection(i);
	if (!sec || (*sec == "general") || (*sec == "snmp_v2") || (*sec == "snmp_v3")
		    || (*sec == "traps") || (*sec == s_cfg.getValue("traps","trap_user","")))
	    continue;
	m_users.append(new SnmpUser(sec));
    }

    // reported version
    String ver = s_cfg.getValue("general","version","${version}");
    Engine::runParams().replaceParams(ver);
    s_yateVersion = ver;

    // load saved data
    s_saveCfg = Engine::configFile("snmp_data");
    s_saveCfg.load();

    // read last used snmpEngineID
    String snmpEngineId = s_saveCfg.getValue("snmp_v3","engine_id","");
    if (snmpEngineId == m_engineId.toHexString()) {
	// the snmpEngineID hasn't been modified so snmpEngineBoots must be increased
	// if there is no engineBoots value saved, it must be set to ENGINE_BOOTS_MAX
	m_engineBoots = s_saveCfg.getIntValue("snmp_v3","engine_boots",ENGINE_BOOTS_MAX);
	if (m_engineBoots == ENGINE_BOOTS_MAX)
	    Alarm(this,"config",DebugWarn,"snmpEngineBoots reached maximum value, snmpEngineID must be reconfigured");
	else
	    m_engineBoots++;
	s_saveCfg.setValue("snmp_v3","engine_boots",(int)m_engineBoots);
    }
    else {
	// reset snmpEngineBoots if snmpEngineID has changed. Save the new value of snmpEngineID
	Debug(this,DebugInfo,"snmpEngineID has been reconfigured, resetting snmpEngineBoots");
	s_saveCfg.setValue("snmp_v3","engine_id",m_engineId.toHexString());
	m_engineBoots = 1;
	s_saveCfg.setValue("snmp_v3","engine_boots",(int)m_engineBoots);
    }
    s_saveCfg.save();

    // load disabled traps
    String traps = s_saveCfg.getValue("traps_conf","traps_disabled","");
    TelEngine::destruct(m_traps);
    m_traps = traps.split(',',false);

    //	USM inits
    // initialize all counters for USM stats
    for (int i = 0; i < 7; i++)
	m_stats[i] = 0;

    // init engine start time
    m_startTime = Time::secNow();
    // init encryption salt
    m_salt = 0;
    m_salt += m_engineBoots;
    m_salt <<= 32;
    m_salt += m_startTime;

    m_silentDrops = 0;
    m_lastRecvHost.clear();

    AsnMib* yateMib = (m_mibTree ? m_mibTree->find("yate") : 0);
    if (yateMib)
	s_yateRoot = yateMib->toString();

    // port on which to listen for SNMP requests
    int snmpPort = s_cfg.getIntValue("general","port",161);
    const char* snmpAddr = s_cfg.getValue("general","addr");
    if (!snmpAddr)
	snmpAddr = "0.0.0.0";
    // thread priority
    Thread::Priority threadPrio = Thread::priority(s_cfg.getValue("general","thread"));

    // do module init, install message handlers
    if (!m_init) {
	m_init = true;
	setup();
	installRelay(Halt);
	m_msgQueue = new SnmpMsgQueue(this,threadPrio,snmpAddr,snmpPort);
	if (!m_msgQueue->init()) {
	    delete m_msgQueue;
	    m_msgQueue = 0;
	}
	if (m_trapHandler)
	    return;
	m_trapHandler = new TrapHandler();
	Engine::install(m_trapHandler);
    }
}

bool SnmpAgent::received(Message& msg, int id)
{
    if (id == Halt) {
	// save and cleanup
	DDebug(&__plugin,DebugInfo,"::received() - Halt Message");
	unload();
    }
    return Module::received(msg,id);
}


int SnmpAgent::processMsg(SnmpMessage* msg)
{
    DDebug(&__plugin,DebugAll,"::processMsg([%p])",msg);
    if(!msg)
	return MESSAGE_DROP;
    DataBlock data = msg->data();
    const String& host = msg->peer().host();

    // determine the version of the SNMP message
    Snmp::Message msgSnmp;
    int l = msgSnmp.decode(data);
    if (l > 0) {
	// SNMPv2 message
	DDebug(&__plugin,DebugAll,"::processMsg() - received %s message msg=%p",lookup(msgSnmp.m_version,s_proto,""),&msgSnmp);
	// try to handle it
	int res = processSnmpV2Msg(msgSnmp,host);
	if (res < 0) {
	    if (res == WRONG_COMMUNITY)
		authFail(msg->peer(),msgSnmp.m_version,res);
	    m_silentDrops++;
	    return MESSAGE_DROP;
	}
	data.clear();
	// encode response in case of successful handling into data
	msgSnmp.encode(data);
    }
    else {
	data = msg->data();
	Snmp::SNMPv3Message m;
	l = m.decode(data);
	if (l >= 0) {
	    // SNMPv3 message
	    DDebug(&__plugin,DebugAll,"::processMsg() - received SNMPv3 message msg=%p",&m);
	    int authRes = SUCCESS;
	    int res = processSnmpV3Msg(m,host,authRes);
	    if (authRes != SUCCESS)
		authFail(msg->peer(),m.m_msgVersion,authRes);
	    if(res < 0) {
		m_silentDrops++;
		return MESSAGE_DROP;
	    }
	    data.clear();
	    // encode response in case of successful handling into data
	    m.encode(data);
	}
	else {
	    Debug(&__plugin,DebugNote,"Unknown SNMP protocol version from %s",host.c_str());
	    return MESSAGE_DROP;
	}
    }
    if (host != m_lastRecvHost) {
	m_lastRecvHost = host;
	Debug(&__plugin,DebugNote,"SNMP client connected from %s",host.c_str());
    }
#ifdef DEBUG
    else
	Debug(&__plugin,DebugAll,"::processMsg([%p]) - successful",msg);
#endif
    // set the data for the message wrapper
    msg->setData(data);
    // send it and return with success
    if (m_msgQueue)
	m_msgQueue->sendMsg(msg);

    return SUCCESS;
}

// process a SNMPv2 message
int SnmpAgent::processSnmpV2Msg(Snmp::Message& msg, const String& host)
{
    DDebug(&__plugin,DebugAll,"::processSnmpV2Msg() [%p]",&msg);
    // verify community string
    String community = msg.m_community.getString();
    int access = AsnMib::notAccessible;
    if (!m_rcCommunity.null() && m_rcCommunity == community)
	access = AsnMib::readCreate;
    else if (!m_rwCommunity.null() && m_rwCommunity == community)
	access = AsnMib::readWrite;
    else if (!m_roCommunity.null() && m_roCommunity == community)
	access = AsnMib::readOnly;
    if (access == AsnMib::notAccessible) {
    	Debug(&__plugin,DebugInfo,"Dropping message from %s with wrong community '%s'",
	    host.c_str(),community.safe());
	return WRONG_COMMUNITY;
    }
    // obtain pdus and do decoding
    DataBlock pdu = msg.m_data;
    if (pdu.length() > 0) {
	Snmp::PDUs chosen;
	int l = chosen.decode(pdu);
	if (l < 0)
	    return MESSAGE_DROP;
	Snmp::BulkPDU* bulkReq = 0;
	Snmp::PDU* req = getPDU(chosen);
	Snmp::PDU* tmp = chosen.m_response->m_Response_PDU;
	if (req) {
	    // handle received pdu according to type
	    decodePDU(chosen.m_choiceType,req,access);
	    if (chosen.m_choiceType == Snmp::PDUs::RESPONSE) {
		if (req != tmp)
		    TelEngine::destruct(tmp);
		chosen.m_response->m_Response_PDU = (req ? req : new Snmp::PDU());
	    }
	}
	else if (chosen.m_choiceType == Snmp::PDUs::GET_BULK_REQUEST) {
	    bulkReq = chosen.m_get_bulk_request->m_GetBulkRequest_PDU;
	    if (bulkReq) {
		// handle bulk request
		Snmp::PDU* responsePDU = decodeBulkPDU(chosen.m_choiceType,bulkReq,access);
		if (tmp != responsePDU)
		    TelEngine::destruct(tmp);
		chosen.m_response->m_Response_PDU = (responsePDU ? responsePDU : new Snmp::PDU());
		chosen.m_choiceType = Snmp::PDUs::RESPONSE;
	    }
	}
	// encode the result and set it in the message wrapper
	msg.m_data.clear();
	chosen.encode(msg.m_data);
    }
    DDebug(&__plugin,DebugAll,"::processSnmpV2Msg() [%p] - successful",&msg);
    return SUCCESS;
}

// handle a request pdu, generate response
void SnmpAgent::decodePDU(int& reqType, Snmp::PDU* obj, const int& access)
{
    DDebug(&__plugin,DebugAll,"::decodePDU([%p]) - pdu type is %s",obj,lookup(reqType,s_pdus,""));
    if (!obj) {
	Debug(&__plugin,DebugMild,"No SNMP PDU to decode");
	return;
    }

    // obtain list of requested OIDs
    Snmp::VarBindList* list = obj->m_variable_bindings;
    if (!list)
	return;

    for (unsigned int i = 0; i < list->m_list.count(); i++) {
	Snmp::VarBind* obji = static_cast<Snmp::VarBind*>(list->m_list[i]);
	if (obji) {
	    int res = 0;
	    AsnValue val;
	    // for each OID requested, handle the request accordingly
	    switch (reqType) {
		case Snmp::PDUs::GET_REQUEST:
		    res = processGetReq(obji,&val,obj->m_error_status,access);
		    break;
		case Snmp::PDUs::GET_NEXT_REQUEST:
		    res = processGetNextReq(obji,&val,obj->m_error_status,access);
		    break;
		case Snmp::PDUs::SET_REQUEST:
		    res = processSetReq(obji,obj->m_error_status,access);
		    break;
		case Snmp::PDUs::SNMPV2_TRAP:
		case Snmp::PDUs::INFORM_REQUEST:
		case Snmp::PDUs::REPORT:
		default:
		  break;
	    }
	    // if the request was handled, but an error was returned, set the error in the response
	    if (res == 1 && obj->m_error_status) {
		obj->m_error_index = i + 1;
		obj->m_variable_bindings = list;
		break;
	    }
	    // if the request was not handled, set a generic error
	    if (!res) {
		obj->m_error_status =  Snmp::PDU::s_genErr_error_status;
		obj->m_error_index = i + 1;
		break;
	    }
	    assignValue(obji,&val);
	}
    }
    reqType = Snmp::PDUs::RESPONSE;
}

// handle a GetRequest for a single variable binding
int SnmpAgent::processGetReq(Snmp::VarBind* varBind, AsnValue* value,  int& error, const int& access)
{
    DDebug(&__plugin,DebugInfo,"::processGetRequest() - varBind [%p], value [%p]",varBind,value);
    if (!(varBind && value && m_mibTree))
	return 0;
    // obtain the OID
    Snmp::ObjectName* objName = varBind->m_name;
    if (!objName)
	return 0;

    ASNObjId oid = objName->m_ObjectName;

    // try to find the OID in the tree, if not found set the appropiate error and return
    AsnMib* mib = m_mibTree->find(oid);
    if (!mib) {
	varBind->m_choiceType = Snmp::VarBind::NOSUCHOBJECT;
	return 1;
    }

    // get the access level of the requested oid, if it doesn't match the access level of the request, set error and return
    if (mib->getAccessValue() < access) {
	varBind->m_choiceType = Snmp::VarBind::NOSUCHOBJECT;
	return 1;
    }
    DDebug(&__plugin,DebugInfo,"::processGetRequest() - varBind [%p], value [%p], oid %s",varBind,value,oid.toString().c_str());
    unsigned int index = mib->index();
    objName->m_ObjectName = mib->getOID();
    mib->setIndex(0);
    // obtain the string equivalent of the OID (the name of the oid)
    String askFor = mib->getName();
    if (TelEngine::null(askFor)) {
	varBind->m_choiceType = Snmp::VarBind::NOSUCHOBJECT;
	return 1;
    }
    // try to get its value
    *value = makeQuery(askFor,index,mib);
    String typeStr = mib->getType();
    // get the type of the OID's value (integer,string,OID?) and set it
    int type = lookup(typeStr,s_types,0);
    if (type != 0)
	value->setType(type);
    // if the is no value for the requested OID, return with error set
    if (!value || value->getValue().null()) {
 	varBind->m_choiceType = Snmp::VarBind::NOSUCHINSTANCE;
 	return 1;
    }
    return 1;
}

// handle a GetNextRequest
int SnmpAgent::processGetNextReq(Snmp::VarBind* varBind, AsnValue* value,  int& error, const int& access)
{
    DDebug(&__plugin,DebugInfo,"::processGetNextRequest() - varBind [%p], value [%p]",varBind,value);
    if (!(varBind && value && m_mibTree))
	return 0;
    // obtain the OID in the request
    Snmp::ObjectName* objName = varBind->m_name;
    if (!objName)
	return 0;

    ASNObjId oid = objName->m_ObjectName;
    AsnMib *next = 0, *aux = 0;

    // obtain the value for the next oid
    next = m_mibTree->find(oid);
    if (next && !next->getName().null()) {
	String name = next->getName();
	unsigned int idx = next->index();
	if (!idx) {
	    *value = makeQuery(name,idx,next);
	    int type = lookup(next->getType(),s_types,0);
	    if (type != 0)
		value->setType(type);
	    if (!value || value->getValue().null())
		next->setIndex(idx + 1);
	    else
		next = m_mibTree->findNext(oid);
	}
	else
	    next->setIndex(idx + 1);
    }
    else
	next = m_mibTree->findNext(oid);

    if (!next) {
        varBind->m_choiceType = Snmp::VarBind::ENDOFMIBVIEW;
	return 1;
    }
    unsigned int index = next->index();

    // obtain the value for the next oid
    while (next) {
        aux = next;
        String askFor = next->getName();
        if (TelEngine::null(askFor)) {
	    varBind->m_choiceType = Snmp::VarBind::NOSUCHINSTANCE;
	    next->setIndex(0);
	    return 1;
        }
        *value = makeQuery(askFor,index,next);
        int type = lookup(next->getType(),s_types,0);
        if (type != 0)
	    value->setType(type);
        if (!value || value->getValue().null()) {
	    if (!index) {
	        index++;
	        continue;
	    }
	    else {
	        oid = next->getOID();
	        next = m_mibTree->findNext(oid);
	        index = 0;
	        aux->setIndex(0);
	        aux = next;
	        continue;
	    }
        }
        else {
	    next->setIndex(index);
	    objName->m_ObjectName = next->getOID();
	    next->setIndex(0);
	    return 1;
        }
    }
    // no OID with a value was found, set end of mib view
    varBind->m_choiceType = Snmp::VarBind::ENDOFMIBVIEW;
    return 1;
}

// process a SetRequest
int SnmpAgent::processSetReq(Snmp::VarBind* varBind, int& error, const int& access)
{
    // !NOTE : Yate will not allow setting values. Besides the enableTrap and disableTrap, it will always return with error
    DDebug(&__plugin,DebugInfo,"::setRequest() - varBind [%p] userAccess %d",varBind,access);
    if (!(varBind && m_mibTree))
	return 0;
    Snmp::ObjectName* objName = varBind->m_name;
    if (!objName)
	return 0;
    if (access < AsnMib::readWrite) {
	error = Snmp::PDU::s_noAccess_error_status;
	return 1;
    }

    ASNObjId oid = objName->m_ObjectName;
    AsnMib* mib = m_mibTree->find(oid);
    if (!mib) {
	error = Snmp::PDU::s_noAccess_error_status;
	return 1;
    }

    // check access level
    int oidAccess = mib->getAccessValue();
    switch (oidAccess) {
	case AsnMib::notAccessible:
	case AsnMib::accessibleForNotify:
	    error = Snmp::PDU::s_noAccess_error_status;
	    return 1;
	    break;
	case AsnMib::readOnly:
	    error = Snmp::PDU::s_notWritable_error_status;
	    return 1;
	    break;
	case AsnMib::readWrite:
	case AsnMib::readCreate:
	default:
	    break;
    }

    // set value
    String name = mib->getName();
    setValue(name,varBind,error);
    return 1;
}

// set a value for YATE
void SnmpAgent::setValue(const String& varName, Snmp::VarBind* val, int& error)
{
    DDebug(this,DebugAll,"::setValue('%s', [%p])",varName.c_str(),val);
    if (!val) {
	error = Snmp::PDU::s_wrongType_error_status;
	return;
    }
    if (!m_mibTree) {
	error = Snmp::PDU::s_noCreation_error_status;
	return;
    }
    // only if the variable asked to be set are these
    if (varName == "enableTrap" || varName == "disableTrap") {
	DataBlock data = getVal(val);
	if (!data.length())
	    return;
	String valStr((const char*)data.data(),data.length());
	ASNObjId oid = valStr;
	AsnMib* mib = m_mibTree->find(oid);
	if (!mib || (mib && mib->getAccessValue() < AsnMib::accessibleForNotify)) {
	    Debug(this,DebugInfo,"::setValue('%s', [%p]), given oid value not found",varName.c_str(),val);
	    error = Snmp::PDU::s_noCreation_error_status;
	    return;
	}
	if (m_traps) {
	    if (varName == "enableTrap")
		m_traps->remove(mib->getName());
	    else if (varName == "disableTrap") {
		if (!m_traps->find(mib->getName()))
		    m_traps->append(new String(mib->getName()));
	    }
	}
    }
    else
	error = Snmp::PDU::s_notWritable_error_status;

}

// handle a GetBulkRequest
Snmp::PDU* SnmpAgent::decodeBulkPDU(int& reqType, Snmp::BulkPDU* pdu, const int& access)
{
    DDebug(&__plugin,DebugInfo,"::decodeBulkPDU() pdu [%p]", pdu);
    if (!(pdu && m_mibTree)) {
	Debug(&__plugin,DebugMild,"::decodeBulkPDU() : no pdu to decode");
	return 0;
    }

    int nonRepeaters = pdu->m_non_repeaters;
    int maxRepetitions = pdu->m_max_repetitions;
    Snmp::VarBindList* list = pdu->m_variable_bindings;
    if (!list)
	return 0;
    DDebug(&__plugin,DebugInfo,"decodeBulkPDU : PDU [%p] list has size %d, non-Repeaters %d, max-Repetitions %d",
	pdu,list->m_list.count(),nonRepeaters,maxRepetitions);

    Snmp::PDU* retPdu = new Snmp::PDU();
    retPdu->m_request_id = pdu->m_request_id;
    retPdu->m_error_status = Snmp::PDU::s_noError_error_status;
    retPdu->m_error_index = 0;

    int i = 0;
    int error = 0;
    AsnValue val;

    // handle non-repeaters
    ObjList* o = list->m_list.skipNull();
    for (; o; o = o->skipNext()) {
	if (i >= nonRepeaters)
	    break;
	Snmp::VarBind* var = static_cast<Snmp::VarBind*>(o->get());
	if (var) {
	    Snmp::VarBind* newVar = new Snmp::VarBind();
	    newVar->m_choiceType = Snmp::VarBind::VALUE;
	    newVar->m_name->m_ObjectName = var->m_name->m_ObjectName;
	    int res = processGetNextReq(newVar,&val,error,access);
	    if (res == 1 && error) {
		retPdu->m_error_index = i + 1;
		retPdu->m_error_status = error;
		break;
	    }
	    if (!res) {
		retPdu->m_error_status = Snmp::PDU::s_genErr_error_status;
		retPdu->m_error_index = i + 1;
		break;
	    }
	    if (newVar->m_choiceType == Snmp::VarBind::VALUE)
		assignValue(newVar,&val);
	    retPdu->m_variable_bindings->m_list.append(newVar);
	    i++;
	}
	if (retPdu->m_error_status)
	    break;
    }
    // handle repeaters
    int j = 0;
    bool endOfView = false;
    while (j < maxRepetitions) {
	int k = i;
	for (ObjList* l = o->skipNull(); l; l = l->skipNext()) {
	    Snmp::VarBind* var = static_cast<Snmp::VarBind*>(l->get());
	    k++;
	    if (var) {
		Snmp::VarBind* newVar = new Snmp::VarBind();
		newVar->m_choiceType = Snmp::VarBind::VALUE;
		newVar->m_name->m_ObjectName = var->m_name->m_ObjectName;
		int res = processGetNextReq(newVar,&val,error,access);
		if (res == 1 && error) {
		    retPdu->m_error_index = k;
		    retPdu->m_error_status = error;
		    break;
		}
		if (!res) {
		    retPdu->m_error_status =  Snmp::PDU::s_genErr_error_status;
		    retPdu->m_error_index = k;
		    break;
		}
		if (newVar->m_choiceType == Snmp::VarBind::VALUE)
		    assignValue(newVar,&val);
		retPdu->m_variable_bindings->m_list.append(newVar);
		var->m_name->m_ObjectName = newVar->m_name->m_ObjectName;
		l->set(var,false);
		if (newVar->m_choiceType == Snmp::VarBind::ENDOFMIBVIEW)
		    endOfView = true;
	    }
	}
	if (retPdu->m_error_status)
	    break;
	if (endOfView)
	    break;
	j++;
    }
    return retPdu;
}

// set the value for a variable binding
void SnmpAgent::assignValue(Snmp::VarBind* varBind, AsnValue* val)
{
    if (!varBind || !(val && val->getValue()))
	return;
    XDebug(&__plugin,DebugAll,"::assignValue([%p], [%p]) - data:%s, type:%d = %s",
	    varBind,val,val->getValue().c_str(),val->type(),lookup(val->type(),s_types,""));

    // set the type of the varbind and assign a value object for it
    varBind->m_choiceType = Snmp::VarBind::VALUE;

    if (!varBind->m_value)
	varBind->m_value = new Snmp::ObjectSyntax();
    Snmp::ObjectSyntax* objSyn = varBind->m_value;

    if (!objSyn->m_simple)
	objSyn->m_simple = new Snmp::SimpleSyntax();
    Snmp::SimpleSyntax* simple = objSyn->m_simple;

    if (!objSyn->m_application_wide)
	objSyn->m_application_wide = new Snmp::ApplicationSyntax();
    Snmp::ApplicationSyntax* app = objSyn->m_application_wide;
    // assign value according to type
    switch (val->type()) {
	case AsnValue::INTEGER:
	    objSyn->m_choiceType = Snmp::ObjectSyntax::SIMPLE;
	    objSyn->m_simple = simple;
	    simple->m_choiceType = Snmp::SimpleSyntax::INTEGER_VALUE;
	    simple->m_integer_value =(int32_t)val->getValue().toInteger();
	    break;
	case AsnValue::STRING:
	    objSyn->m_choiceType = Snmp::ObjectSyntax::SIMPLE;
	    objSyn->m_simple = simple;
	    simple->m_choiceType = Snmp::SimpleSyntax::STRING_VALUE;
	    simple->m_string_value = val->getValue();
	    break;
	case AsnValue::OBJECT_ID:
	    objSyn->m_choiceType = Snmp::ObjectSyntax::SIMPLE;
	    objSyn->m_simple = simple;
	    simple->m_choiceType = Snmp::SimpleSyntax::OBJECTID_VALUE;
	    simple->m_objectID_value = (const char*) val->getValue().c_str();
	    break;
	case AsnValue::IPADDRESS:
	    objSyn->m_choiceType = Snmp::ObjectSyntax::APPLICATION_WIDE;
	    objSyn->m_application_wide = app;
	    app->m_choiceType = Snmp::ApplicationSyntax::IPADDRESS_VALUE;
	    app->m_ipAddress_value->m_IpAddress = String(val->getValue());
	    break;
	case AsnValue::COUNTER:
	    objSyn->m_choiceType = Snmp::ObjectSyntax::APPLICATION_WIDE;
	    objSyn->m_application_wide = app;
	    app->m_choiceType = Snmp::ApplicationSyntax::COUNTER_VALUE;
	    app->m_counter_value->m_Counter32 = (u_int32_t)val->getValue().toInteger();
	    break;
	case AsnValue::TIMETICKS:
	    objSyn->m_choiceType = Snmp::ObjectSyntax::APPLICATION_WIDE;
	    objSyn->m_application_wide = app;
	    app->m_choiceType = Snmp::ApplicationSyntax::TIMETICKS_VALUE;
	    app->m_timeticks_value-> m_TimeTicks = (u_int32_t)val->getValue().toInteger();
	    break;
	case AsnValue::ARBITRARY:
	    objSyn->m_choiceType = Snmp::ObjectSyntax::APPLICATION_WIDE;
	    objSyn->m_application_wide = app;
	    app->m_choiceType = Snmp::ApplicationSyntax::ARBITRARY_VALUE;
	    app->m_arbitrary_value->m_Opaque =  val->getValue();
	    break;
	case AsnValue::BIG_COUNTER:
	    objSyn->m_choiceType = Snmp::ObjectSyntax::APPLICATION_WIDE;
	    objSyn->m_application_wide = app;
	    app->m_choiceType = Snmp::ApplicationSyntax::BIG_COUNTER_VALUE;
	    app->m_big_counter_value->m_Counter64 = (uint64_t)val->getValue().toInt64();
	    break;
	case AsnValue::UNSIGNED_INTEGER:
	    objSyn->m_choiceType = Snmp::ObjectSyntax::APPLICATION_WIDE;
	    objSyn->m_application_wide = app;
	    app->m_choiceType = Snmp::ApplicationSyntax::UNSIGNED_INTEGER_VALUE;
	    app->m_unsigned_integer_value->m_Unsigned32 = (u_int32_t)val->getValue().toInteger();
	    break;
	default:
	    Debug(&__plugin, DebugNote, "unknown value");
    }
}

/**
  * Function for handling a v3 SNMP message
  * Message structure
  *	msgVersion = 3
  *	msgGlobalData
  *		- msgId
  *		- msgMaxSize
  *		- msgFlags ( report, auth, priv)
  *		- msgSecurityModel
  * 	msgSecurityParameters - ! NOTE it's a string of octets which should be decoded according to the security model
  *	msgData
  * 	    - plain text
  *		- contextEngineID
  *		- contextName
  *		- data - PDU
  *	    or
  *	    - encrypted Data - string to be decrypted according the encryption methos (DES-CBC / AES-CFB)
  */
int SnmpAgent::processSnmpV3Msg(Snmp::SNMPv3Message& msg, const String& host, int& authRes)
{
    DDebug(&__plugin,DebugAll,"::processSnmpV3Msg() [%p]",&msg);
    // initialize a SNMPv3 container
    SnmpV3MsgContainer msgContainer;
    // message is valid?
    int secRes = msgContainer.validate(msg,authRes);
    if (secRes == MESSAGE_DROP) {
 	Debug(&__plugin,DebugNote, "SNMPv3 message from %s not validated, silent drop",host.c_str());
	return MESSAGE_DROP;
    }

    // if a error was found validating the message and the reportableFlag is set, generate a ReportPDU
    if (secRes) {
	if (msgContainer.reportable()) {
	    if (generateReport(msg,secRes,msgContainer) < 0)
		return MESSAGE_DROP;
	    return secRes;
	}
	else {
 	    Debug(&__plugin,DebugNote,"Error during SNMPv3 message from %s processing, further processing aborted",host.c_str());
	    return MESSAGE_DROP;
	}
    }
    // generate a ResponsePDU otherwise
    if (generateResponse(msg,msgContainer) == MESSAGE_DROP)
	return MESSAGE_DROP;
    return SUCCESS;
}

// build a ReportPDU
int SnmpAgent::generateReport(Snmp::SNMPv3Message& msg, const int& secRes, SnmpV3MsgContainer& cont)
{
    DDebug(&__plugin,DebugInfo,"::generateReport() - %s",lookup(secRes,s_errors,"unknown cause"));
    if (!m_mibTree)
	return MESSAGE_DROP;
    if (!msg.m_msgGlobalData)
  	return MESSAGE_DROP;
    // reset the message flags
    cont.setReportFlag(false);
    cont.setPrivFlag(false);
    if (secRes == WRONG_DIGEST || secRes == WRONG_USER)
	cont.setAuthFlag(false);

    // extract from the ScopedPDU the PDU
    if (!msg.m_msgData)
	msg.m_msgData = new Snmp::ScopedPduData();
    Snmp::ScopedPduData* data = msg.m_msgData;
    int choice = data->m_choiceType;
    data->m_choiceType = Snmp::ScopedPduData::PLAINTEXT;
    Snmp::ScopedPDU* pdu = 0;
    Snmp::PDUs p;
    TelEngine::destruct(p.m_report->m_Report_PDU);
    if (choice == Snmp::ScopedPduData::PLAINTEXT && data->m_plaintext) {
	pdu = data->m_plaintext;
	p.decode(pdu->m_data);
	p.m_report->m_Report_PDU = getPDU(p);
    }
    if (!pdu) {
	pdu = new Snmp::ScopedPDU();
	p.m_report->m_Report_PDU = new Snmp::PDU();
    }

    pdu->m_contextEngineID = m_engineId;
    pdu->m_contextName = pdu->m_contextName;
    // set the PDUs type
    p.m_choiceType = Snmp::PDUs::REPORT;
    // the error information
    p.m_report->m_Report_PDU->m_error_status = Snmp::PDU::s_noError_error_status;
    p.m_report->m_Report_PDU->m_error_index = 0;
    // clear the VarBindList
    p.m_report->m_Report_PDU->m_variable_bindings->m_list.clear();
    Snmp::VarBind* var = new Snmp::VarBind();

    AsnValue val;
    AsnMib* mib = 0;

    // look up the cause for non validating the message
    String stat = lookup(secRes,s_stats,"");
    if (!stat.null()) {
	// find its OID
	mib = m_mibTree->find(stat);
	// increase the counter for USM stats
	m_stats[secRes]++;
	// set the value
	if (mib) {
	    var->m_name->m_ObjectName = mib->getOID();
	    val.setValue(String(m_stats[secRes]));
	    val.setType(AsnValue::COUNTER);
	}
    }

    if (val.getValue().length() == 0)
      return MESSAGE_DROP;

    // set the value in the ReportPDU
    assignValue(var,&val);
    p.m_report->m_Report_PDU->m_variable_bindings->m_list.append(var);

    // build the message
    pdu->m_data.clear();
    p.encode(pdu->m_data);
    if (!msg.m_msgData)
          return MESSAGE_DROP;
    msg.m_msgData->m_plaintext = pdu;
    cont.prepareForSend(msg);
    return SUCCESS;
}

// build a ResponsePDU
int SnmpAgent::generateResponse(Snmp::SNMPv3Message& msg, SnmpV3MsgContainer& msgContainer)
{
    DDebug(&__plugin,DebugAll,"::generateResponse() for msg=%p",&msg);
    // process the request (Get/GetNext/SetRequest)
    msgContainer.processRequest(msg);

    // set the security parameters for the ResponsePDU
    msgContainer.prepareForSend(msg);

    // encode the message
    DataBlock ret;
    msg.encode(ret);
    // if it passes the maximum length for a SNMP message return a tooBig message
    if ((int)ret.length() > msgContainer.msgMaxSize())
	return msgContainer.generateTooBigMsg(msg);
    return SUCCESS;
}

// get a PDU
Snmp::PDU* SnmpAgent::getPDU(Snmp::PDUs& p)
{
    Snmp::PDU* pdu = 0;
    switch (p.m_choiceType) {
	case Snmp::PDUs::GET_REQUEST:
	    pdu = p.m_get_request->m_GetRequest_PDU;
	    p.m_get_request->m_GetRequest_PDU = 0;
	    return pdu;
	case Snmp::PDUs::GET_NEXT_REQUEST:
	    pdu = p.m_get_next_request->m_GetNextRequest_PDU;
	    p.m_get_next_request->m_GetNextRequest_PDU = 0;
	    return pdu;
	case Snmp::PDUs::SET_REQUEST:
	    pdu = p.m_set_request->m_SetRequest_PDU;
	    p.m_set_request->m_SetRequest_PDU = 0;
	    return pdu;
	case Snmp::PDUs::RESPONSE:
	    pdu = p.m_response->m_Response_PDU;
	    p.m_response->m_Response_PDU = 0;
	    return pdu;
	case Snmp::PDUs::INFORM_REQUEST:
	    pdu = p.m_inform_request->m_InformRequest_PDU;
	    p.m_inform_request->m_InformRequest_PDU = 0;
	    return pdu;
	case Snmp::PDUs::SNMPV2_TRAP:
	    pdu =  p.m_snmpV2_trap->m_SNMPv2_Trap_PDU;
	    p.m_snmpV2_trap->m_SNMPv2_Trap_PDU = 0;
	    return pdu;
	case Snmp::PDUs::REPORT:
	    pdu = p.m_report->m_Report_PDU;
	    p.m_report->m_Report_PDU = 0;
	    return pdu;
	default:
	    break;
    }
    return 0;
}

// build a remote destination
SocketAddr SnmpAgent::buildDestination(const String& ip, const String& port)
{
    SocketAddr dest(AF_INET);
    dest.host(ip);
    dest.port(port.toInteger());
    return dest;
}

// build a Variable Binding list containing the mandatory OIDs for a trap
Snmp::VarBindList* SnmpAgent::addTrapOIDs(const String& notifOID)
{
    if (!m_mibTree)
	return 0;

    // add sysUpTime
    AsnMib* mib = m_mibTree->find("sysUpTime");
    if (!mib)
	return 0;
    Snmp::VarBind* sysUpTime = new Snmp::VarBind();
    sysUpTime->m_name->m_ObjectName = mib->getOID();
    u_int32_t sysTime = (u_int32_t)((Time::msecNow() / 10) - (100 * (uint64_t)m_startTime)); // measured hundreths of a second
    AsnValue val(String(sysTime),AsnValue::TIMETICKS);
    assignValue(sysUpTime,&val);

    // add trapOID
    mib = m_mibTree->find("snmpTrapOID");
    if (!mib)
	return 0;
    Snmp::VarBind* trapOID = new Snmp::VarBind();
    trapOID->m_name->m_ObjectName = mib->getOID();
    AsnValue trOID(notifOID,AsnValue::OBJECT_ID);
    assignValue(trapOID,&trOID);

    Snmp::VarBindList* list = new Snmp::VarBindList();
    list->m_list.append(sysUpTime);
    list->m_list.append(trapOID);

    return list;
}

// build a trap PDU for SNMPv2
Snmp::SNMPv2_Trap_PDU SnmpAgent::buildTrapPDU(const String& name, const String* value, unsigned int index)
{
    DDebug(&__plugin,DebugAll,"::buildTrapPDU(notif='%s', value='%s', index='%u')",
	name.c_str(),TelEngine::c_str(value),index);
    Snmp::SNMPv2_Trap_PDU trapPDU;;
    Snmp::PDU* pdu = trapPDU.m_SNMPv2_Trap_PDU;
    if (!(m_mibTree && pdu))
	return trapPDU;
    // set a requestID and error information
    pdu->m_request_id = Time::secNow();// - m_startTime;
    pdu->m_error_status = Snmp::PDU::s_noError_error_status;
    pdu->m_error_index = 0;

    // try to find the OID of the notification received
    AsnMib* notifMib = m_mibTree->find(name);
    if (!notifMib) {
	DDebug(&__plugin,DebugInfo,"::buildTrapPDU(notif='%s', value='%s') - no such notification exists",
		    name.c_str(),TelEngine::c_str(value));
	return trapPDU;
    }

    // add the mandatory OIDs
    notifMib->setIndex(index);
    String oid = notifMib->getOID();
    TelEngine::destruct(pdu->m_variable_bindings);
    pdu->m_variable_bindings = addTrapOIDs(index ? oid : notifMib->toString());
    if (!pdu->m_variable_bindings) {
	Debug(&__plugin,DebugInfo,"::buildTrapPDU() - could not set sysUpTime and/or trapOID");
	return trapPDU;
    }

    // add the trap OID with index and its value if requested
    if (value) {
	Snmp::VarBind* trapVal = new Snmp::VarBind();
	trapVal->m_name->m_ObjectName = oid;
	String typeStr = notifMib->getType();
	int type = lookup(typeStr,s_types,0);
	AsnValue* v = new AsnValue(value,type);
	assignValue(trapVal,v);
	pdu->m_variable_bindings->m_list.append(trapVal);
	TelEngine::destruct(v);
    }
    // return the trapPDU
    return trapPDU;
}

// build a SNMPv3 TrapPDU
int SnmpAgent::buildTrapMsgV3(Snmp::SNMPv3Message& msg, DataBlock d)
{
    if (!m_trapUser)
	return -1;
    DDebug(&__plugin,DebugAll,"::buildTrapMsgV3() from msg=%p",&msg);
    // build header data
    msg.m_msgVersion = SNMP_VERSION_3;
    Snmp::HeaderData* header = msg.m_msgGlobalData;
    if (!header)
	return -1;
    header->m_msgID = Time::secNow() - m_startTime;
    header->m_msgMaxSize = MSG_MAX_SIZE;
    header->m_msgSecurityModel = USM_SEC_MODEL;

    // get data to fill
    Snmp::ScopedPduData* scopedData = msg.m_msgData;
    if (!scopedData) {
	scopedData = new Snmp::ScopedPduData();
	msg.m_msgData = scopedData;
    }
    scopedData->m_choiceType = Snmp::ScopedPduData::PLAINTEXT;
    Snmp::ScopedPDU* scopedPdu = scopedData->m_plaintext;
    if (!scopedPdu) {
	scopedPdu = new Snmp::ScopedPDU();
	scopedData->m_plaintext = scopedPdu;
    }
    scopedPdu->m_contextEngineID = m_engineId;
    scopedPdu->m_contextName = "";
    scopedPdu->m_data = d;

    SnmpV3MsgContainer msgWrapper;
    msgWrapper.setScopedPdu(scopedPdu);
    msgWrapper.setReportFlag(false);
    // get received security parameters
    msgWrapper.setUser(m_trapUser);
    // build the security parameters
    Snmp::UsmSecurityParameters& sec = msgWrapper.getSecurity();
    sec.m_msgAuthoritativeEngineID = m_engineId;
    sec.m_msgAuthoritativeEngineBoots = m_engineBoots;
    sec.m_msgAuthoritativeEngineTime = Time::secNow() - m_startTime;
    sec.m_msgUserName = m_trapUser->toString();
    sec.m_msgAuthenticationParameters = s_zeroKey;

    msgWrapper.prepareForSend(msg);
    msgWrapper.setScopedPdu(0);
    return 0;
}

// check to see if a trap is disabled
bool SnmpAgent::trapDisabled(const String& name)
{
    if (!m_mibTree)
	return true;
    AsnMib* mib = m_mibTree->find(name);
    if (!mib) {
	DDebug(&__plugin,DebugInfo,"Notification '%s' does not exist",name.c_str());
	return true;
    }
    if (m_traps && m_traps->find(name))
	return true;
    String trapOid;
    String oid = mib->toString();
    String disabledTraps = s_cfg.getValue("traps","disable_traps","");
    ObjList* list = disabledTraps.split(',');
    if (!list)
	return false;
    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	String* trap = static_cast<String*>(o->get());
	if (trap->endsWith(".*"))
	    *trap = trap->substr(0, trap->length() - 2);
	AsnMib* trapMib = m_mibTree->find(*trap);
	if (trapMib)
	    trapOid = trapMib->toString();
	if (trapOid == oid || oid.startsWith(trapOid)) {
	    TelEngine::destruct(list);
	    return true;
	}
    }
    TelEngine::destruct(list);
    return false;
}

// send a trap from a received notification
bool SnmpAgent::sendNotification(const String& name, const String* value, unsigned int index, const NamedList* extra)
{
    if (!(s_enabledTraps && m_msgQueue))
	return false;
    // check to see if the trap is enabled
    if (trapDisabled(name))
	return false;
    // check to see if trap handling has beed configured
    NamedList* params = s_cfg.getSection("traps");
    if (!params) {
	Debug(&__plugin,DebugMild,"::sendNotification('%s', '%s') -"
	      " traps have not been configured",name.c_str(),TelEngine::c_str(value));
	return false;
    }
    DDebug(&__plugin,DebugAll,"::sendNotification('%s', '%s')",name.c_str(),TelEngine::c_str(value));

    // check to see that the right version for SNMP traps are configured
    String protoStr = params->getValue("proto_version","SNMPv2c");
    int proto = lookup(protoStr,s_proto,0);
    if (proto < SNMP_VERSION_2C) {
	Debug(&__plugin,DebugStub,"::sendNotification() STUB : TRAPS FOR SNMPv1 NOT IMPLEMENTED");
	return false;
    }
    else {
	if (proto == SNMP_VERSION_2S) {
	    Debug(&__plugin,DebugStub,"::sendNotification() - SNMPv2S not supported");
	    return false;
	}
    }

    // build a trap pdu
    Snmp::SNMPv2_Trap_PDU trapPDU = buildTrapPDU(name,value,index);
    if (trapPDU.m_SNMPv2_Trap_PDU->m_variable_bindings->m_list.count() < 2) {
	Debug(&__plugin,DebugWarn,"::sendNotification() - trap PDU incorrectly built - aborting the send of the notification");
	return false;
    }

    // populate extra variables
    if (extra) {
	Snmp::PDU* pdu = trapPDU.m_SNMPv2_Trap_PDU;
	int count = extra->getIntValue("count",-1);
	bool anyDisabled = false;
	bool allDisabled = true;
	for (int i = 0; ; i++) {
	    // if count is set iterate up to it
	    if (count >= 0 && i >= count)
		break;
	    String extraName = "notify.";
	    extraName << i;
	    const String& xName = (*extra)[extraName];
	    if (xName.null()) {
		// if count not set stop at first missing name
		if (count < 0)
		    break;
		else
		    continue;
	    }
	    if (trapDisabled(xName)) {
		anyDisabled = true;
		continue;
	    }
	    allDisabled = false;
	    String extraVal = "value.";
	    extraVal << i;
	    const String& xVal = (*extra)[extraVal];
	    AsnMib* notifMib = m_mibTree->find(xName);
	    if (!notifMib) {
		DDebug(&__plugin,DebugInfo,"::sendNotification(notif.%d='%s', value.%d='%s') - no such notification exists",
		    i,xName.c_str(),i,xVal.c_str());
		continue;
	    }
	    Snmp::VarBind* trapVar = new Snmp::VarBind();
	    trapVar->m_name->m_ObjectName = notifMib->getOID();
	    int type = lookup(notifMib->getType(),s_types,0);
	    AsnValue* v = new AsnValue(&xVal,type);
	    assignValue(trapVar,v);
	    pdu->m_variable_bindings->m_list.append(trapVar);
	    TelEngine::destruct(v);
	}
	if (anyDisabled && allDisabled)
	    return false;
    }

    // build pdus
    Snmp::PDUs pdus;
    pdus.m_choiceType = Snmp::PDUs::SNMPV2_TRAP;
    TelEngine::destruct(pdus.m_snmpV2_trap);
    pdus.m_snmpV2_trap = &trapPDU;
    DataBlock d;
    pdus.encode(d);
    pdus.m_snmpV2_trap = 0;

    DataBlock data;
    // build the required version of a SNMP message
    if (proto == SNMP_VERSION_2C) {
	Snmp::Message msg;
	msg.m_version = Snmp::Message::s_version_2_version;
	msg.m_community = params->getValue("community","");
	msg.m_data = d;
	msg.encode(data);
    }
    else if (proto == SNMP_VERSION_3) {
	Snmp::SNMPv3Message msg;
	if (buildTrapMsgV3(msg,d) == -1)
	    return false;
	msg.encode(data);
    }

    // build a new SNMP message wrapper
    SnmpMessage* msgContainer = new SnmpMessage();
    msgContainer->setPeer(s_remote);

    // send the data of the message
    msgContainer->setData(data);
    bool ok = m_msgQueue && m_msgQueue->sendMsg(msgContainer);
    TelEngine::destruct(msgContainer);
    return ok;
}

// obtain from the openssl module a cipher for encryption
Cipher* SnmpAgent::getCipher(int cryptoType)
{
    DDebug(this,DebugAll,"::getCipher(%s)",lookup(cryptoType,s_crypto,""));
    if (cryptoType != SnmpUser::AES_ENCRYPT && cryptoType != SnmpUser::DES_ENCRYPT)
	return 0;

    if (cryptoType == SnmpUser::AES_ENCRYPT && m_cipherAES)
	return m_cipherAES;
    if (cryptoType == SnmpUser::DES_ENCRYPT && m_cipherDES)
	return m_cipherDES;

    Cipher* ret = 0;
    Message msg("engine.cipher");
    if (cryptoType == SnmpUser::AES_ENCRYPT)
	msg.addParam("cipher","aes_cfb");
    if (cryptoType == SnmpUser::DES_ENCRYPT)
	msg.addParam("cipher","des_cbc");
    CipherHolder* cHold = new CipherHolder;
    msg.userData(cHold);
    cHold->deref();

    if (Engine::dispatch(msg)) {
	ret = cHold->cipher();
	if (cryptoType == SnmpUser::AES_ENCRYPT)
	    m_cipherAES = ret;
	if (cryptoType == SnmpUser::DES_ENCRYPT)
	    m_cipherDES = ret;
    }
    return ret;
}

// get the value from a variable binding
DataBlock SnmpAgent::getVal(Snmp::VarBind* varBind)
{
    DDebug(this,DebugAll,"::getVal([%p])",varBind);
    if (!varBind)
	return DataBlock();
    Snmp::ObjectSyntax* objSyn = varBind->m_value;
    if (!objSyn)
	return DataBlock();
    int type = -1;
    switch (objSyn->m_choiceType) {
	case Snmp::ObjectSyntax::SIMPLE:
	    type = objSyn->m_simple->m_choiceType;
	    switch (type) {
		case Snmp::SimpleSyntax::INTEGER_VALUE:
		    return DataBlock((void*)&objSyn->m_simple->m_integer_value,sizeof(objSyn->m_simple->m_integer_value));
		case Snmp::SimpleSyntax::STRING_VALUE:
		    return DataBlock((void*)objSyn->m_simple->m_string_value.toString().c_str(),sizeof(objSyn->m_simple->m_string_value));
		case Snmp::SimpleSyntax::OBJECTID_VALUE:
		    return DataBlock((void*)objSyn->m_simple->m_objectID_value.toString().c_str(),
					    objSyn->m_simple->m_objectID_value.toString().length());
		default:
		  break;
	    }
	    break;
	case Snmp::ObjectSyntax::APPLICATION_WIDE:
	    type = objSyn->m_application_wide->m_choiceType;
	    switch (type) {
		case Snmp::ApplicationSyntax::IPADDRESS_VALUE:
		    return DataBlock((void*)objSyn->m_application_wide->m_ipAddress_value,
			    sizeof(objSyn->m_application_wide->m_ipAddress_value));
		case Snmp::ApplicationSyntax::COUNTER_VALUE:
		    return DataBlock((void*)objSyn->m_application_wide->m_counter_value,
			    sizeof(objSyn->m_application_wide->m_counter_value));
		case Snmp::ApplicationSyntax::TIMETICKS_VALUE:
		    return DataBlock((void*)objSyn->m_application_wide->m_timeticks_value,
			    sizeof(objSyn->m_application_wide->m_timeticks_value));
		case Snmp::ApplicationSyntax::ARBITRARY_VALUE:
		    return DataBlock((void*)objSyn->m_application_wide->m_timeticks_value,
			    sizeof(objSyn->m_application_wide->m_timeticks_value));
		case Snmp::ApplicationSyntax::BIG_COUNTER_VALUE:
		    return DataBlock((void*)objSyn->m_application_wide->m_big_counter_value,
			    sizeof(objSyn->m_application_wide->m_big_counter_value));
		case Snmp::ApplicationSyntax::UNSIGNED_INTEGER_VALUE:
		    return DataBlock((void*)objSyn->m_application_wide->m_unsigned_integer_value,
			    sizeof(objSyn->m_application_wide->m_unsigned_integer_value));
		default:
		    break;
	    }
	    break;
	default:
	  break;
    }
    Debug(&__plugin,DebugInfo,"SnmpAgent::getVal([%p]) - no value",varBind);
    return DataBlock();
}

// obtain the value for a query made through SNMP
AsnValue SnmpAgent::makeQuery(const String& query, unsigned int& index, AsnMib* mib)
{
    DDebug(&__plugin,DebugAll, "::makeQuery(query='%s', index='%d')",query.c_str(),index);
    AsnValue val;
    if (query == YSTRING("version")) {
	val.setValue(s_yateVersion);
	val.setType(AsnValue::STRING);
	return val;
    }
    if (query == YSTRING("runId")) {
	String v = Engine::runParams().getValue(YSTRING("runid"),"");
	val.setValue(v);
	val.setType(AsnValue::STRING);
	return val;
    }
    if (query == YSTRING("upTime")) {
	val.setValue(String(SysUsage::secRunTime()));
	val.setType(AsnValue::COUNTER);
	return val;
    }
    if (query == YSTRING("snmpEngineID")) {
	val.setValue(m_engineId.toHexString());
	val.setType(AsnValue::STRING);
	return val;
    }
    if (query == YSTRING("snmpEngineBoots")) {
	val.setValue(String(m_engineBoots));
	val.setType(AsnValue::INTEGER);
	return val;
    }

    if (query == YSTRING("yateMIBRevision") && m_mibTree) {
	String rev = m_mibTree->findRevision(query);
	val.setValue(rev);
	val.setType(AsnValue::STRING);
	return val;
    }
    if (!queryIsSupported(query,mib))
	return val;

    // ask the monitor module
    Message msg("monitor.query");
    msg.addParam("name",query);
    msg.addParam("index",String(index));
    if (Engine::dispatch(msg)) {
	const String* value = msg.getParam(YSTRING("value"));
	if (!value)
	    value = &msg.retValue();
	if (*value) {
	    val.setValue(*value);
	    val.setType(STRING);
	}
    }

    return val;
}

bool SnmpAgent::queryIsSupported(const String& query, AsnMib* mib)
{
    if (!m_mibTree || s_yateRoot.null())
	return false;
    if (!mib)
	mib = m_mibTree->find(query);
    if (!mib)
	return false;
    return (mib->toString().startsWith(s_yateRoot,false));
}

// generate snmpEngineID from configuration parameters
OctetString SnmpAgent::genEngineId(const int format, String& info)
{
    DDebug(&__plugin,DebugInfo,"::genEngineId(%d,%s)",format,info.c_str());
    OctetString aux;

    // set the first 4 bytes to the PEN number and first bit set to 1 (see RFC 3411)
    u_int32_t firstPart = s_pen | 0x80000000;
    for (unsigned int i = 0; i < sizeof(u_int32_t); i++) {
	u_int8_t byte = (firstPart >> (8 * i));
	DataBlock d;
	d.append(&byte,1);
	aux.insert(d);
    }
    int size = 1, base = 10;
    // add the format for of the 6 bytes remaining from the engine id
    aux.append((void*)&format,1);
    ObjList* list = 0;
    DataBlock db;
    // according to the given format, build the rest of 6 bytes
    switch (format) {
	case IPv4:
	    list = info.split('.');
	    base = 10;
	    break;
	case IPv6:
	    list = info.split(':');
	    size = 2;
	    break;
	case MAC:
	    list = info.split(':');
	    base = 16;
	    break;
	case TEXT:
	    aux.append(info);
	    break;
	case OCTETS:
	    db.unHexify(info);
	    aux.append(db);
	    break;
	case ENTERPRISE:
	    aux.append(info);
	    break;
	default:
	    break;
    }
    if (list) {
	for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	    u_int8_t val;
	    String* str = static_cast<String*>(o->get());
	    if (size == 2) {
		int intVal = str->toInteger(0,16);
		DataBlock d,daux;
		val = intVal;
		intVal = intVal >> 8;
		d.append(&val,1);
		daux.insert(d);
		d.clear();
		val = intVal;
		d.append(&val,1);
		daux.insert(d);
		aux.append(daux);
	    }
	    else {
		val = str->toInteger(0,base);
		aux.append(&val,size);
	    }
	}
	TelEngine::destruct(list);
    }
    return aux;
}

void SnmpAgent::authFail(const SocketAddr& addr, int snmpVersion, int reason, int protocol)
{
    String rAddr = addr.host();
    String rPort(addr.port());

    Message* m = new Message("user.authfail");
    m->setParam(YSTRING("module"),name());
    m->setParam(YSTRING("address"),rAddr + ":" + rPort);
    m->setParam(YSTRING("ip_host"),rAddr);
    m->setParam(YSTRING("ip_port"),rPort);
    m->setParam(YSTRING("ip_transport"),lookup(protocol,TransportType::s_typeText,""));
    m->setParam(YSTRING("protocol"),lookup(snmpVersion,s_proto,""));
    m->setParam(YSTRING("reason"),lookup(reason,s_readableErrors,""));

    Engine::enqueue(m);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
