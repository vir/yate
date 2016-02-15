/**
 * yradius.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * RADIUS Client functionality for YATE
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 *
 * Largely based on the code sent by Faizan Naqvi (Tili)
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

using namespace TelEngine;
namespace { // anonymous

#define RADIUS_MAXLEN 4096

enum
{
    NoError = 0,
    AcctSuccess = NoError,
    AuthSuccess = NoError,
    AuthFailed,
    ServerErr,
    ConfErr,
    UnknownErr,
};

static Configuration s_cfg;
static Mutex s_cfgMutex("YRadius::cfg");
static ObjList acctBuilders;
static SocketAddr s_localAddr(AF_INET);
static Socket s_localSock;
static bool s_localTime = false;
static bool s_shortnum = false;
static bool s_unisocket = false;
static bool s_printAttr = false;
static bool s_pb_enabled = false;
static bool s_pb_parallel = false;
static bool s_pb_simplify = false;
static bool s_cisco = true;
static bool s_quintum = false;
static String s_pb_stoperror;
static String s_pb_maxcall;

// Attribute types, not exactly as in RFC 2865
enum {
    a_void = 0,	// invalid, used in separators
    a_binary,	// rfc2865 string
    a_string,	// rfc2865 text
    a_ipaddr,	// rfc2865 address
    a_int,	// rfc2865 integer
    a_date,	// rfc2865 time
    a_avpair	// special text formatted as name=value
};

// Structure for building attribute tables
typedef struct {
    int code;
    const char* name;
    const char* confName;
    int type;
} rad_dict;

// Structure for building pointers to standard or vendor tables
typedef struct {
    int vendor;
    const char* name;
    const rad_dict* dict;
} rad_vendor;


// Standard or vendor-specific RADIUS values and tables

// RADIUS packet codes
enum {
    Access_Request = 1,
    Access_Accept = 2,
    Access_Reject = 3,
    Accounting_Request = 4,
    Accounting_Response = 5,
    Access_Challenge = 11,
    // experimental codes
    Status_Server = 12,
    Status_Client = 13,
};

// Accounting status types
enum {
    Acct_Start = 1,
    Acct_Stop  = 2,
    Acct_Alive = 3,
    Acct_On    = 7,
    Acct_Off   = 8,
};

// Subattribute type for Digest-Attributes - see draft-sterman-aaa-sip-00.txt
enum {
    Digest_Realm = 1,
    Digest_Nonce = 2,
    Digest_Method = 3,
    Digest_URI = 4,
    Digest_QOP = 5,
    Digest_Algo = 6,
    Digest_Body = 7,
    Digest_CNonce = 8,
    Digest_NCount = 9,
    Digest_UserName = 10
};

// Standard RADIUS attributes
static rad_dict radius_dict[] = {
    {   1, "User-Name",             "User-Name",             a_string },
    {   2, "User-Password",         "User-Password",         a_binary },
    {   3, "CHAP-Password",         "CHAP-Password",         a_binary },
    {   4, "NAS-IP-Address",        "NAS-IP-Address",        a_ipaddr },
    {   5, "NAS-Port",              "NAS-Port",              a_int },
    {   6, "Service-Type",          "Service-Type",          a_int },
    {  18, "Reply-Message",         "Reply-Message",         a_string },
    {  26, "Vendor-Specific",       "Vendor-Specific",       a_binary },
    {  27, "Session-Timeout",       "Session-Timeout",       a_int },
    {  30, "Called-Station-Id",     "Called-Station-Id",     a_string },
    {  31, "Calling-Station-Id",    "Calling-Station-Id",    a_string },
    {  32, "NAS-Identifier",        "NAS-Identifier",        a_string },
    {  40, "Acct-Status-Type",      "Acct-Status-Type",      a_int },
    {  41, "Acct-Delay-Time",       "Acct-Delay-Time",       a_int },
    {  42, "Acct-Input-Octets",     "Acct-Input-Octets",     a_int },
    {  43, "Acct-Output-Octets",    "Acct-Output-Octets",    a_int },
    {  44, "Acct-Session-Id",       "Acct-Session-Id",       a_string },
    {  45, "Acct-Authentic",        "Acct-Authentic",        a_int },
    {  46, "Acct-Session-Time",     "Acct-Session-Time",     a_int },
    {  47, "Acct-Input-Packets",    "Acct-Input-Packets",    a_int },
    {  48, "Acct-Output-Packets",   "Acct-Output-Packets",   a_int },
    {  49, "Acct-Terminate-Cause",  "Acct-Terminate-Cause",  a_int },
    {  50, "Acct-Multi-Session-Id", "Acct-Multi-Session-Id", a_string },
    {  51, "Acct-Link-Count",       "Acct-Link-Count",       a_int },
    {  60, "CHAP-Challenge",        "CHAP-Challenge",        a_binary },
    {  61, "NAS-Port-Type",         "NAS-Port-Type",         a_int },
    {  62, "Port-Limit",            "Port-Limit",            a_int },
    {  63, "Login-LAT-Port",        "Login-LAT-Port",        a_string },
    {  68, "Configuration-Token",   "Configuration-Token",   a_binary },
    { 206, "Digest-Response",       "Digest-Response",       a_string },
    { 207, "Digest-Attributes",     "Digest-Attributes",     a_binary },
    {   0, 0, 0, a_void }
};

// Cisco vendor attributes
static rad_dict cisco_dict[]= {
    {   1, "Cisco-AVPair",             "Cisco-AVPair",             a_string },
    {   2, "Cisco-NAS-Port",           "Cisco-NAS-Port",           a_string },
    {   2, "NAS-Port-Name",            "NAS-Port-Name",            a_string }, // alternate name
    {  23, "h323-remote-address",      "h323-remote-address",      a_avpair },
    {  24, "h323-conf-id",             "h323-conf-id",             a_avpair },
    {  25, "h323-setup-time",          "h323-setup-time",          a_avpair },
    {  26, "h323-call-origin",         "h323-call-origin",         a_avpair },
    {  27, "h323-call-type",           "h323-call-type",           a_avpair },
    {  28, "h323-connect-time",        "h323-connect-time",        a_avpair },
    {  29, "h323-disconnect-time",     "h323-disconnect-time",     a_avpair },
    {  30, "h323-disconnect-cause",    "h323-disconnect-cause",    a_avpair },
    {  31, "h323-voice-quality",       "h323-voice-quality",       a_avpair },
    {  33, "h323-gw-id",               "h323-gw-id",               a_avpair },
    {  34, "h323-call-treatment",      "h323-call-treatment",      a_string },
    { 101, "h323-credit-amount",       "h323-credit-amount",       a_avpair },
    { 102, "h323-credit-time",         "h323-credit-time",         a_avpair },
    { 103, "h323-return-code",         "h323-return-code",         a_avpair },
    { 104, "h323-prompt-id",           "h323-prompt-id",           a_avpair },
    { 105, "h323-time-and-day",        "h323-time-and-day",        a_avpair },
    { 106, "h323-redirect-number",     "h323-redirect-number",     a_avpair },
    { 107, "h323-preferred-lang",      "h323-preferred-lang",      a_avpair },
    { 108, "h323-redirect-ip-address", "h323-redirect-ip-address", a_avpair },
    { 109, "h323-billing-model",       "h323-billing-model",       a_avpair },
    { 110, "h323-currency",            "h323-currency",            a_avpair },
    { 187, "Cisco-Multilink-ID",       "Cisco-Multilink-ID",       a_int },
    { 188, "Cisco-Num-In-Multilink",   "Cisco-Num-In-Multilink",   a_int },
    { 190, "Cisco-Pre-Input-Octets",   "Cisco-Pre-Input-Octets",   a_int },
    { 191, "Cisco-Pre-Output-Octets",  "Cisco-Pre-Output-Octets",  a_int },
    { 192, "Cisco-Pre-Input-Packets",  "Cisco-Pre-Input-Packets",  a_int },
    { 193, "Cisco-Pre-Output-Packets", "Cisco-Pre-Output-Packets", a_int },
    { 194, "Cisco-Maximum-Time",       "Cisco-Maximum-Time",       a_int },
    { 195, "Cisco-Disconnect-Cause",   "Cisco-Disconnect-Cause",   a_int },
    { 197, "Cisco-Data-Rate",          "Cisco-Data-Rate",          a_int },
    { 198, "Cisco-PreSession-Time",    "Cisco-PreSession-Time",    a_int },
    { 208, "Cisco-PW-Lifetime",        "Cisco-PW-Lifetime",        a_int },
    { 209, "Cisco-IP-Direct",          "Cisco-IP-Direct",          a_int },
    { 210, "Cisco-PPP-VJ-Slot-Comp",   "Cisco-PPP-VJ-Slot-Comp",   a_int },
    { 212, "Cisco-PPP-Async-Map",      "Cisco-PPP-Async-Map",      a_int },
    { 217, "Cisco-IP-Pool-Definition", "Cisco-IP-Pool-Definition", a_int },
    { 218, "Cisco-Assign-IP-Pool",     "Cisco-Assign-IP-Pool",     a_int },
    { 228, "Cisco-Route-IP",           "Cisco-Route-IP",           a_int },
    { 233, "Cisco-Link-Compression",   "Cisco-Link-Compression",   a_int },
    { 234, "Cisco-Target-Util",        "Cisco-Target-Util",        a_int },
    { 235, "Cisco-Maximum-Channels",   "Cisco-Maximum-Channels",   a_int },
    { 242, "Cisco-Data-Filter",        "Cisco-Data-Filter",        a_int },
    { 243, "Cisco-Call-Filter",        "Cisco-Call-Filter",        a_int },
    { 244, "Cisco-Idle-Limit",         "Cisco-Idle-Limit",         a_int },
    { 255, "Cisco-Xmit-Rate",          "Cisco-Xmit-Rate",          a_int },
    {   0, 0, 0, a_void }
};

static rad_dict quintum_dict[]= {
    {   1, "Quintum-AVPair",        "Quintum-AVPair",                a_string },
    {   2, "Tenor-NAS-Port",        "Tenor-NAS-Port",                a_string },
    {  23, "h323-remote-address",   "Quintum-h323-remote-address",   a_avpair },
    {  24, "h323-conf-id",          "Quintum-h323-conf-id",          a_avpair },
    {  25, "h323-setup-time",       "Quintum-h323-setup-time",       a_avpair },
    {  26, "h323-call-origin",      "Quintum-h323-call-origin",      a_avpair },
    {  27, "h323-call-type",        "Quintum-h323-call-type",        a_avpair },
    {  28, "h323-connect-time",     "Quintum-h323-connect-time",     a_avpair },
    {  29, "h323-disconnect-time",  "Quintum-h323-disconnect-time",  a_avpair },
    {  30, "h323-disconnect-cause", "Quintum-h323-disconnect-cause", a_avpair },
    {  31, "h323-voice-quality",    "Quintum-h323-voice-quality",    a_avpair },
    {  33, "h323-gw-id",            "Quintum-h323-gw-id",            a_avpair },
    { 101, "h323-credit-amount",    "Quintum-h323-credit-amount",    a_avpair },
    { 102, "h323-credit-time",      "Quintum-h323-credit-time",      a_avpair },
    { 103, "h323-return-code",      "Quintum-h323-return-code",      a_avpair },
    { 104, "h323-prompt-id",        "Quintum-h323-prompt-id",        a_avpair },
    { 106, "h323-redirect-number",  "Quintum-h323-redirect-number",  a_avpair },
    { 107, "h323-preferred-lang",   "Quintum-h323-preferred-lang",   a_avpair },
    { 109, "h323-billing-model",    "Quintum-h323-billing-model",    a_avpair },
    { 110, "h323-currency",         "Quintum-h323-currency",         a_avpair },
    { 230, "Trunkid-In",            "Quintum-Trunkid-In",            a_string },
    { 231, "Trunkid-Out",           "Quintum-Trunkid-Out",           a_string },
    {   0, 0, 0, a_void }
};

// Microsoft vendor attributes
static rad_dict ms_dict[]= {
    {   1, "MS-CHAP-Response",              "MS-CHAP-Response",              a_binary },
    {   2, "MS-CHAP-Error",                 "MS-CHAP-Error",                 a_binary },
    {   3, "MS-CHAP-CPW-1",                 "MS-CHAP-CPW-1",                 a_binary },
    {   4, "MS-CHAP-CPW-2",                 "MS-CHAP-CPW-2",                 a_binary },
    {   5, "MS-CHAP-LM-Enc-PW",             "MS-CHAP-LM-Enc-PW",             a_binary },
    {   6, "MS-CHAP-NT-Enc-PW",             "MS-CHAP-NT-Enc-PW",             a_binary },
    {   7, "MS-MPPE-Encryption-Policy",     "MS-MPPE-Encryption-Policy",     a_binary },
    {   8, "MS-MPPE-Encryption-Types",      "MS-MPPE-Encryption-Types",      a_binary },
    {   9, "MS-RAS-Vendor",                 "MS-RAS-Vendor",                 a_int },
    {  10, "MS-CHAP-Domain",                "MS-CHAP-Domain",                a_binary },
    {  11, "MS-CHAP-Challenge",             "MS-CHAP-Challenge",             a_binary },
    {  12, "MS-CHAP-MPPE-Keys",             "MS-CHAP-MPPE-Keys",             a_binary },
    {  13, "MS-BAP-Usage",                  "MS-BAP-Usage",                  a_int },
    {  14, "MS-Link-Utilization-Threshold", "MS-Link-Utilization-Threshold", a_int },
    {  15, "MS-Link-Drop-Time-Limit",       "MS-Link-Drop-Time-Limit",       a_int },
    {  16, "MS-MPPE-Send-Key",              "MS-MPPE-Send-Key",              a_binary },
    {  17, "MS-MPPE-Recv-Key",              "MS-MPPE-Recv-Key",              a_binary },
    {  18, "MS-RAS-Version",                "MS-RAS-Version",                a_binary },
    {  22, "MS-Filter",                     "MS-Filter",                     a_binary },
    {  23, "MS-Acct-Auth-Type",             "MS-Acct-Auth-Type",             a_int },
    {  24, "MS-Acct-EAP-Type",              "MS-Acct-EAP-Type",              a_int },
    {  25, "MS-CHAP2-Response",             "MS-CHAP2-Response",             a_binary },
    {  26, "MS-CHAP2-Success",              "MS-CHAP2-Success",              a_binary },
    {  27, "MS-CHAP2-PW",                   "MS-CHAP2-PW",                   a_binary },
    {  30, "MS-Primary-NBNS-Server",        "MS-Primary-NBNS-Server",        a_ipaddr },
    {  31, "MS-Secondary-NBNS-Server",      "MS-Secondary-NBNS-Server",      a_ipaddr },
    {   0, 0, 0, a_void }
};

static rad_vendor vendors_dict[] = {
    {    0, 0,           radius_dict },
    {    9, "cisco",     cisco_dict },
    {  311, "microsoft", ms_dict },
    { 6618, "quintum",   quintum_dict },
    {   0, 0, 0 }
};

// End of RADIUS values and tables


// map termination cause keywords to Acct-Terminate-Cause attribute values
static TokenDict dict_errors[] = {
    { "noanswer", 4 }, // Idle-Timeout
    { "timeout", 5 }, // Session-Timeout
    { "drop", 7 }, // Admin-Reset
    { "reboot", 7 }, // Admin-Reboot
    { "halt", 7 }, // Admin-Reboot
    { "offline", 8 }, // Port-Error
    { "congestion", 8 }, // Port-Error
    { "failure", 9 }, // NAS-Error
    { "noconn", 9 }, // NAS-Error
    { "busy", 13 }, // Port-Preempted
    { "nocall", 15 }, // Service-Unavailable
    { "noroute", 15 }, // Service-Unavailable
    { "forbidden", 17 }, // User-Error
    { "rejected", 18 }, // Host-Request
    { 0, 0 },
};


// Class to hold one RADIUS attribute
class RadAttrib : public GenObject
{
public:
    RadAttrib(const rad_dict* type, int vendor, void* value, unsigned int length);
    RadAttrib(const rad_dict* type, int vendor, const char* value);
    RadAttrib(const char* name, const char* value);
    RadAttrib(const char* name, int value);
    RadAttrib(const char* name, unsigned char subType, const char* value);
    virtual ~RadAttrib();
    bool packTo(DataBlock& data) const;
    bool getString(String& retval) const;
    inline bool isValid() const
	{ return (m_type != 0); }
    inline bool isVendor() const
	{ return (m_type && (m_type->code == 26)) && (m_vendor == 0); }
    inline int vendor() const
	{ return m_vendor; }
    inline const rad_dict* type() const
	{ return m_type; }
    inline const char* name() const
	{ return m_type ? m_type->name : 0; }
    inline int code() const
	{ return m_type ? m_type->code : -1; }
    inline const DataBlock& data() const
	{ return m_value; }
    static const rad_dict* find(const char* name, int* vendor = 0, const char** vendName = 0);
    static const rad_dict* find(int code, int vendor = 0);
    static bool decode(void* buf, unsigned int len, ObjList& list);
    inline static bool decode(const DataBlock& data, ObjList& list)
	{ return decode(data.data(),data.length(),list); }
    static RadAttrib* decode(void*& buffer, unsigned int& length, int vendor = 0);
private:
    bool assign(const char* value);
    bool assign(int value);
    bool assign(unsigned char subType, const char* value);
    const rad_dict* m_type;
    int m_vendor;
    DataBlock m_value;
};

// Class to encapsulate an entire client request operation
class RadiusClient : public GenObject
{
public:
    RadiusClient()
	: m_socket(0), m_authPort(0), m_acctPort(0),
	  m_timeout(2000), m_retries(2), m_cisco(s_cisco), m_quintum(s_quintum)
	{ }
    virtual ~RadiusClient();
    inline const String& server() const
	{ return m_server; }
    inline bool addCisco() const
	{ return m_cisco; }
    inline bool addQuintum() const
	{ return m_quintum; }
    bool setRadServer(const char* host, int authport, int acctport, const char* secret, int timeoutms = 4000, int retries = 2);
    bool setRadServer(const NamedList& sect);
    bool addSocket();
    int doAuthenticate(ObjList* result = 0);
    int doAccounting(ObjList* result = 0);
    bool addAttribute(const char* attrib, const char* val, bool emptyOk = false);
    bool addAttribute(const char* attrib, int val);
    bool addAttribute(const char* attrib, unsigned char subType, const char* val, bool emptyOk = false);
    void addAttributes(NamedList& params, NamedList* list);
    bool prepareAttributes(NamedList& params, bool forAcct = true, String* user = 0);
    bool returnAttributes(NamedList& params, const ObjList* attributes, bool ok = true);
    static bool fillRandom(DataBlock& data, int len);

private:
    Socket* socket() const;
    static unsigned char newSessionId();
    int makeRequest(int port, unsigned char request, unsigned char* response = 0, ObjList* result = 0);
    bool checkAuthenticator(const unsigned char* buffer, int length);
    inline bool checkAuthenticator(const DataBlock& data)
	{ return checkAuthenticator((const unsigned char*)data.data(),data.length()); }

    static unsigned char s_sessionId;
    Socket* m_socket;
    ObjList m_attribs;
    String m_server,m_secret,m_section;
    unsigned int m_authPort,m_acctPort;
    int m_timeout, m_retries;
    bool m_cisco, m_quintum;
    DataBlock m_authdata;
};

class RadiusModule : public Module
{
public:
    RadiusModule();
    virtual ~RadiusModule();
    virtual void initialize();
protected:
    bool m_init;
};

INIT_PLUGIN(RadiusModule);

static const String s_fmtCisco("cisco_format");
static const String s_fmtQuintum("quintum_format");

class AuthHandler : public MessageHandler
{
public:
    inline AuthHandler(int prio)
	: MessageHandler("user.auth",prio,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class AcctHandler : public MessageHandler
{
public:
    inline AcctHandler(int prio)
	: MessageHandler("call.cdr",prio,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class RadiusHandler : public MessageHandler
{
public:
    inline RadiusHandler(int prio)
	: MessageHandler("radius.generate",prio,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

// PortaOne specific routing
static void portaBillingRoute(NamedList& params, const ObjList* attributes)
{
    String route;
    // should we call multiple targets parallel or sequencial?
    const char* rsep = s_pb_parallel ? " " : " | ";
    for (; attributes; attributes = attributes->next()) {
	const RadAttrib* attr = static_cast<const RadAttrib*>(attributes->get());
	// PortaBilling uses only Cisco-AVPair
	if (!(attr && (attr->vendor() == 9) && (attr->code() == 1)))
	    continue;
	String tmp;
	attr->getString(tmp);
	if (tmp.startSkip("h323-ivr-in=DURATION:",false)) {
	    int sec = tmp.toInteger();
	    if (sec > 0) {
		Debug(&__plugin,DebugCall,"PortaBilling setting timeout %d seconds",sec);
		tmp = sec * 1000;
		params.setParam("timeout",tmp);
	    }
	    continue;
	}
	if (!tmp.startSkip("h323-ivr-in=PortaBilling_",false))
	    continue;
	XDebug(&__plugin,DebugInfo,"PortaBilling '%s'",tmp.c_str());
	if (tmp.startSkip("Routing:",false)) {
	    if (s_pb_simplify) {
		int sep = tmp.find(';');
		if (sep >= 0)
		    tmp.assign(tmp,sep);
	    }
	    if (tmp.null())
		continue;
	    tmp = "sip/sip:" + tmp;
	    if (route.null())
		route = tmp;
	    else {
		if (!route.startsWith("fork",true))
		    route = "fork " + route;
		route << rsep << tmp;
	    }
	}
	else if (tmp.startSkip("CLI:",false)) {
	    if (tmp) {
		Debug(&__plugin,DebugCall,"PortaBilling setting caller '%s'",tmp.c_str());
		params.setParam("caller",tmp);
	    }
	}
	else if (tmp.startSkip("CompleteNumber:",false)) {
	    if (tmp) {
		Debug(&__plugin,DebugCall,"PortaBilling setting called '%s'",tmp.c_str());
		params.setParam("called",tmp);
	    }
	}
    }
    if (route) {
	Debug(&__plugin,DebugCall,"PortaBilling returned route '%s'",route.c_str());
	params.setParam("callto",route);
	if (s_pb_maxcall)
	    params.setParam("maxcall",s_pb_maxcall);
	if (s_pb_stoperror && route.startsWith("fork",true))
	    params.setParam("stoperror",s_pb_stoperror);
    }
}

// find one attribute by name, optionally store vendor ID and name
const rad_dict* RadAttrib::find(const char* name, int* vendor, const char** vendName)
{
    for (rad_vendor* v = vendors_dict; v->dict; v++) {
	for (const rad_dict* dict = v->dict; dict->name; dict++) {
	    if (!strcasecmp(name,dict->confName)) {
		if (vendor)
		    *vendor = v->vendor;
		if (vendName)
		    *vendName = v->name;
		return dict;
	    }
	}
    }
    return 0;
}

// find one attribute by code and vendor ID
const rad_dict* RadAttrib::find(int code, int vendor)
{
    for (rad_vendor* v = vendors_dict; v->dict; v++) {
	if (v->vendor == vendor) {
	    for (const rad_dict* dict = v->dict; dict->name; dict++) {
		if (dict->code == code)
		    return dict;
	    }
	    break;
	}
    }
    return 0;
}

// decode one attribute, adjust buffer and length; on error set buffer to null
RadAttrib* RadAttrib::decode(void*& buffer, unsigned int& length, int vendor)
{
    XDebug(&__plugin,DebugAll,"RadAttrib::decode(%p,%u,%d)",buffer,length,vendor);
    if (!(buffer && length))
	return 0;
    if (length < 3) {
	buffer = 0;
	return 0;
    }
    unsigned char* ptr = (unsigned char*)buffer;
    int code = ptr[0];
    unsigned int len = ptr[1];
    if ((len < 3) || (len > length)) {
	buffer = 0;
	return 0;
    }
    buffer = ptr + len;
    length -= len;
    const rad_dict* type = find(code,vendor);
    if (!type)
	return 0;
    switch (type->type) {
	case a_ipaddr:
	case a_int:
	case a_date:
	    if (len != 6) {
		buffer = 0;
		// roll back length to make it easier to debug
		length += len;
		return 0;
	    }
    }
    return new RadAttrib(type,vendor,ptr+2,len-2);
}

// decode an entire received set of attributes
bool RadAttrib::decode(void* buf, unsigned int len, ObjList& list)
{
    unsigned int len1 = len;
    while (len) {
	RadAttrib* attr = decode(buf,len);
	if (!attr) {
	    if (!buf) {
		Debug(&__plugin,DebugMild,"Invalid attribute at offset %u",len1 - len + 20);
		return false;
	    }
	    continue;
	}
	if (attr->isVendor()) {
	    unsigned int len2 = attr->data().length();
	    char* ptr = (char*)attr->data().data();
	    if ((len2 < 4) || !ptr) {
		DDebug(&__plugin,DebugMild,"Invalid vendor attribute %u len=%u",attr->code(),len2);
		attr->destruct();
		return false;
	    }
	    void* buf2 = ptr + 4;
	    len2 -= 4;
	    int vendor = ((int)ptr[0] << 24) | ((int)ptr[1] << 16) | ((int)ptr[2] << 8) | ptr[3];
	    while (len2) {
		RadAttrib* attr2 = decode(buf2,len2,vendor);
		if (!attr2) {
		    if (!buf2) {
			DDebug(&__plugin,DebugMild,"Invalid vendor %u attribute",vendor);
			attr->destruct();
			return false;
		    }
		    continue;
		}
		list.append(attr2);
	    }
	    attr->destruct();
	}
	else
	    list.append(attr);
    }
    return true;
}

RadAttrib::RadAttrib(const rad_dict* type, int vendor, void* value, unsigned int length)
    : m_type(type), m_vendor(vendor), m_value(value,length)
{
    XDebug(&__plugin,DebugAll,"RadAttrib::RadAttrib(%p,%d,%p,%u) [%p]",type,vendor,value,length,this);
}

RadAttrib::RadAttrib(const rad_dict* type, int vendor, const char* value)
    : m_type(type), m_vendor(vendor)
{
    XDebug(&__plugin,DebugAll,"RadAttrib::RadAttrib(%p,%d,'%s') [%p]",type,vendor,value,this);
    if (m_type && value)
	assign(value);
}

RadAttrib::RadAttrib(const char* name, const char* value)
    : m_type(0), m_vendor(0)
{
    XDebug(&__plugin,DebugAll,"RadAttrib::RadAttrib('%s','%s') [%p]",name,value,this);
    if (null(name) || null(value))
	return;
    m_type = find(name,&m_vendor);
    if (!m_type) {
	Debug(&__plugin,DebugGoOn,"Failed to find item %s in dictionary",name);
	return;
    }
    assign(value);
}

RadAttrib::RadAttrib(const char* name, int value)
    : m_type(0), m_vendor(0)
{
    XDebug(&__plugin,DebugAll,"RadAttrib::RadAttrib('%s',%d) [%p]",name,value,this);
    if (null(name))
	return;
    m_type = find(name,&m_vendor);
    if (!m_type) {
	Debug(&__plugin,DebugGoOn,"Failed to find item %s in dictionary",name);
	return;
    }
    assign(value);
}

RadAttrib::RadAttrib(const char* name, unsigned char subType, const char* value)
{
    XDebug(&__plugin,DebugAll,"RadAttrib::RadAttrib('%s',%u,'%s') [%p]",name,subType,value,this);
    if (null(name) || null(value))
	return;
    m_type = find(name,&m_vendor);
    if (!m_type) {
	Debug(&__plugin,DebugGoOn,"Failed to find item %s in dictionary",name);
	return;
    }
    assign(subType,value);
}

RadAttrib::~RadAttrib()
{
    XDebug(&__plugin,DebugAll,"RadAttrib::~RadAttrib type=%p vendor=%d [%p]",m_type,m_vendor,this);
}

bool RadAttrib::assign(const char* value)
{
    if (null(value))
	return false;
    switch (m_type->type) {
	case a_string:
	    m_value.assign((void*)value,strlen(value));
	    break;
	case a_avpair:
	    {
		String val(m_type->name);
		val << "=" << value;
		m_value.assign((void*)val.c_str(),val.length());
	    }
	    break;
	case a_int:
	    {
		unsigned int val = htonl((unsigned int)atoi(value));
		m_value.assign((void*)&val,sizeof(int32_t));
	    }
	    break;
	case a_ipaddr:
	    {
		uint32_t addr = inet_addr(value);
		m_value.assign((void*)&addr,sizeof(int32_t));
	    }
	    break;
	default:
	    Debug(&__plugin,DebugGoOn,"Ignoring unknown attribute of type %d",m_type->type);
	    return false;
    }
    return true;
}

bool RadAttrib::assign(int value)
{
    switch (m_type->type) {
	case a_string:
	    {
		String val(value);
		m_value.assign((void*)val.c_str(),val.length());
	    }
	    break;
	case a_avpair:
	    {
		String val(m_type->name);
		val << "=" << value;
		m_value.assign((void*)val.c_str(),val.length());
	    }
	    break;
	case a_int:
	case a_ipaddr:
	    {
		unsigned int val = htonl(value);
		m_value.assign((void*)&val,sizeof(int32_t));
	    }
	    break;
	default:
	    Debug(&__plugin,DebugGoOn,"Ignoring unknown attribute of type %d",m_type->type);
	    return false;
    }
    return true;
}

bool RadAttrib::assign(unsigned char subType, const char* value)
{
    if (null(value) || (m_type->type != a_binary))
	return false;
    // copy at most 253 characters so with header will be under 255
    String val(value,253);
    unsigned char header[2];
    header[0] = subType;
    header[1] = (val.length() + 2) & 0xff;
    m_value.assign(header,2);
    m_value.append(val);
    return true;
}

bool RadAttrib::packTo(DataBlock& data) const
{
    if (m_value.null() || !m_type)
	return false; // invalid attribute.

    // RADIUS cannot support attributes longer than 255
    unsigned char buf[256];
    unsigned char* ptr = buf;
    unsigned int len = m_value.length();
    if (len > 253)
	len = 253;
    unsigned int total = len + 2;

    if (m_vendor) {
	if (len > 247)
	    len = 247;
	total = len + 8;
	// put generic vendor type, full length, vendor code
	*ptr++ = 26;
	*ptr++ = 0xff & total;
	*ptr++ = 0xff & (m_vendor >> 24);
	*ptr++ = 0xff & (m_vendor >> 16);
	*ptr++ = 0xff & (m_vendor >> 8);
	*ptr++ = 0xff & m_vendor;
    }
    // put type, length, data
    *ptr++ = m_type->code;
    *ptr++ = 0xff & (len + 2);
    memcpy(ptr,m_value.data(),len);
    if (len != m_value.length())
	Debug(&__plugin,DebugMild,"Attribute '%s' (%u) truncated from %u to %u bytes",
	    m_type->name,m_type->code,m_value.length(),len);
    DataBlock tmp(buf,total,false);
    data += tmp;
    tmp.clear(false);
    return true;
}

bool RadAttrib::getString(String& retval) const
{
    if (!m_type)
	return false;
    retval.clear();
    if (m_value.null())
	return false;
    switch (m_type->type) {
	case a_string:
	    retval.assign((const char*)m_value.data(),m_value.length());
	    break;
	case a_avpair:
	    retval.assign((const char*)m_value.data(),m_value.length());
	    {
		// strip away any "name=" prefix
		String tmp = m_type->name;
		tmp << "=";
		retval.startSkip(tmp,false);
	    }
	    break;
	case a_ipaddr:
	    {
		struct in_addr addr;
		addr.s_addr = *(int32_t*)m_value.data();
		// FIXME: this should be globally thread safe
		s_cfgMutex.lock();
		retval = inet_ntoa(addr);
		s_cfgMutex.unlock();
	    }
	    break;
	case a_int:
	    retval = (int)ntohl(*(int32_t*)m_value.data());
	    break;
	default:
	    return false;
    }
    return true;
}


unsigned char RadiusClient::s_sessionId = (unsigned char)(Time::now() & 0xff);

RadiusClient::~RadiusClient()
{
    delete m_socket;
}

Socket* RadiusClient::socket() const
{
    return m_socket ? m_socket : &s_localSock;
}

// Create and add a local UDP socket to the client request
bool RadiusClient::addSocket()
{
    if (m_socket)
	return true;
    SocketAddr localAddr(AF_INET);
    localAddr.host(s_localAddr.host());
    if (!(localAddr.valid() && localAddr.host())) {
	Debug(&__plugin,DebugInfo,"Invalid address '%s' - falling back to global socket",
	    localAddr.host().c_str());
	return false;
    }

    // we only have UDP support
    Socket* s = new Socket(PF_INET,SOCK_DGRAM,IPPROTO_IP);
    if (!(s && s->valid())) {
	Debug(&__plugin,DebugWarn,"Error creating UDP socket - falling back to global socket");
	delete s;
	return false;
    }
    if (!s->bind(localAddr)) {
	Debug(&__plugin,DebugWarn,"Error %d binding to %s - falling back to global socket",
	    s->error(),localAddr.host().c_str());
	delete s;
	return false;
    }
    DDebug(&__plugin,DebugInfo,"Created new socket for request");
    m_socket = s;
    return true;
}

// Build incremental session ID byte
unsigned char RadiusClient::newSessionId()
{
    Lock lock(s_cfgMutex);
    return s_sessionId++;
}

// Set the server parameters
bool RadiusClient::setRadServer(const char* host, int authport, int acctport, const char* secret, int timeoutms, int retries)
{
    // adjust an absolute minimum of 1 try with a 500ms timeout
    if (retries < 1)
	retries = 1;
    if (timeoutms < 500)
	timeoutms = 500;
    m_server = host;
    m_authPort = authport;
    m_acctPort = acctport;
    m_secret = secret;
    m_timeout = timeoutms;
    m_retries = retries;
    return (m_server && (m_authPort || m_acctPort));
}

// Set the server parameters from a config file section
bool RadiusClient::setRadServer(const NamedList& sect)
{
    return setRadServer(sect.getValue("server"),
	sect.getIntValue("auth_port",1812),
	sect.getIntValue("acct_port",1813),
	sect.getValue("secret"),
	sect.getIntValue("timeout",2000),
	sect.getIntValue("retries",2));
}

// Fill a data block with (pseudo) random data
bool RadiusClient::fillRandom(DataBlock& data, int len)
{
    data.assign(0,len);
    unsigned char *dd = (unsigned char*)data.data();
    if (!dd)
	return false;
    unsigned int r = 0;
    while (len--) {
	while (!r)
	    r = Random::random();
	*dd++ = r & 0xff;
	r = r >> 8;
    }
    return true;
}

// Cryptographically check if the response is properly authenticated
bool RadiusClient::checkAuthenticator(const unsigned char* buffer, int length)
{
    if (!buffer)
	return false;

    const unsigned char* recauth = buffer+4;
    const unsigned char* recattr = buffer+20;
    int attrlen = length - 20;

    MD5 md5(buffer,4);
    md5.update(m_authdata);
    if (attrlen > 0)
	md5.update(recattr,attrlen);
    md5.update(m_secret);
    if (memcmp(md5.rawDigest(),recauth,16)) {
	Debug(&__plugin,DebugMild,"Authenticators do not match");
	return false;
    }
    Debug(&__plugin,DebugAll,"Authenticator matched for response");
    return true;
}

// Make one request, wait for answer and optionally decode it
int RadiusClient::makeRequest(int port, unsigned char request, unsigned char* response, ObjList* result)
{
    if (!(port && socket() && socket()->valid()))
	return ServerErr;

    // create the address to send and receive packets
    SocketAddr sockAddr(AF_INET);
    sockAddr.host(m_server);
    sockAddr.port(port);

    // build the attribute block
    DataBlock attrdata;
    for (ObjList* l = &m_attribs; l; l = l->next()) {
	const RadAttrib* attr = static_cast<const RadAttrib*>(l->get());
	if (attr)
	    attr->packTo(attrdata);
    }
    int datalen = 20 + attrdata.length();
    if (datalen > RADIUS_MAXLEN) {
	Debug(&__plugin,DebugGoOn,"Packet of %u bytes exceeds RADIUS maximum",datalen);
	return UnknownErr;
    }

    // now we create the header for the RADIUS packet
    unsigned char sessionId = newSessionId();
    unsigned char tmp[4];
    tmp[0] = request;
    tmp[1] = sessionId;
    tmp[2] = (datalen >> 8) & 0xff;
    tmp[3] = datalen & 0xff;

    // build the authenticator which is 16 octets long
    switch (request) {
	case Access_Request:
	    // random 16 octets used to authenticate the answer
	    if (!fillRandom(m_authdata,16))
		return UnknownErr;
	    break;
	case Accounting_Request:
	    // authenticate our packet to the server (see rfc2866)
	    {
		DataBlock zeros(0,16);
		MD5 md5(tmp,4);
		md5 << zeros << attrdata << m_secret;
		m_authdata.assign((void*)md5.rawDigest(),16);
	    }
	    break;
	default:
	    Debug(&__plugin,DebugFail,"Unknown request %u was asked. We only support Access and Accounting",request);
	    return UnknownErr;
    }

    // now we build the packet out of header, authenticator and attributes
    DataBlock radpckt(tmp,sizeof(tmp));
    radpckt.append(m_authdata);
    radpckt.append(attrdata);

    if (!s_unisocket)
	addSocket();

    // we have the data ready, send it and wait for an answer
    for (int r = m_retries; r > 0; r--) {
	if (socket()->sendTo(radpckt.data(),radpckt.length(),sockAddr) == Socket::socketError()) {
	    Alarm(&__plugin,"socket",DebugGoOn,"Packet sending error %d to %s:%d",
		socket()->error(),sockAddr.host().c_str(),sockAddr.port());
		return UnknownErr;
	}

	// ok, we now must wait for receiving the matching answer
	u_int64_t tryEnd = Time::now() + 1000 * m_timeout;
	int64_t tout;
	while ((tout = (tryEnd - Time::now())) > 0) {
	    bool canRead = false;
	    if (!socket()->select(&canRead,NULL,NULL,tout)) {
		Debug(&__plugin,DebugWarn,"Error %d in select",socket()->error());
		return UnknownErr;
	    }
	    if (!canRead) {
		if (r > 1)
		    Debug(&__plugin,DebugMild,"Timeout waiting for server %s:%d, there are %d retries left",
			sockAddr.host().c_str(),sockAddr.port(),r-1);
		break;
	    }

	    SocketAddr recvAddr;
	    unsigned char recdata[RADIUS_MAXLEN];
	    int readlen = socket()->recvFrom(recdata,sizeof(recdata),recvAddr);
	    if (readlen == Socket::socketError()) {
		Debug(&__plugin,DebugWarn,"Packet reading error %d from %s:%d",
		    socket()->error(),sockAddr.host().c_str(),sockAddr.port());
		break;
	    }
	    if (readlen < 20) {
		Debug(&__plugin,DebugInfo,"Ignoring short (%d bytes) response from %s:%d",
		    readlen,recvAddr.host().c_str(),recvAddr.port());
		continue;
	    }
	    datalen = ((unsigned int)recdata[2] << 8) | recdata[3];
	    if ((datalen < 20) || (datalen > readlen)) {
		Debug(&__plugin,DebugInfo,"Ignoring packet with length %d (%d received) response from %s:%d",
		    datalen,readlen,recvAddr.host().c_str(),recvAddr.port());
		continue;
	    }
	    if (recdata[1] != sessionId) {
		DDebug(&__plugin,DebugAll,"Ignoring mismatched (%u vs %u) response from %s:%d",
		    recdata[1],sessionId,recvAddr.host().c_str(),recvAddr.port());
		continue;
	    }
	    if (!checkAuthenticator(recdata,datalen)) {
		Debug(&__plugin,DebugMild,"Ignoring unauthenticated session %u response from %s:%d",
		    sessionId,recvAddr.host().c_str(),recvAddr.port());
		continue;
	    }
	    if (result) {
		// try to decode answer
		if (!RadAttrib::decode(recdata+20,datalen-20,*result))
		    // authenticated but malformed - no reason to try again
		    return ServerErr;
	    }
	    if (response)
		*response = recdata[0];
	    DDebug(&__plugin,DebugInfo,"Received valid response %u on session %u from %s:%d",
		recdata[0],sessionId,recvAddr.host().c_str(),recvAddr.port());
	    return NoError;
	}
    }
    Debug(&__plugin,DebugWarn,"Timeout receiving session %u from server %s:%d",
	sessionId,sockAddr.host().c_str(),sockAddr.port());
    return ServerErr;
}

// Make an authentication request, wait for answer
int RadiusClient::doAuthenticate(ObjList* result)
{
    unsigned char response = 0;
    int err = makeRequest(m_authPort,Access_Request,&response,result);
    if (err != NoError) {
	Debug(&__plugin,DebugWarn,"Aborting authentication with radius %s:%d",
	    m_server.c_str(),m_authPort);
	return err;
    }

    // we have the response of radius or some other app to which we accidentally sent data because user put wrong port or ip.
    if (response != Access_Accept) {
	Debug(&__plugin,DebugMild,"Server returned %u, assuming Access-Reject",response);
	return AuthFailed;
    }

    Debug(&__plugin,DebugInfo,"Server returned Access-Accept");
    return AuthSuccess;
}

// Make an accounting request, wait for answer
int RadiusClient::doAccounting(ObjList* result)
{
    unsigned char response = 0;
    int err = makeRequest(m_acctPort,Accounting_Request,&response,result);
    if (err != NoError) {
	Debug(&__plugin,DebugWarn,"Aborting accounting with radius %s:%d",
	    m_server.c_str(),m_acctPort);
	return err;
    }

    // we have the response of radius or some other app to which we accidentally sent data because user put wrong port or ip.
    if (response != Accounting_Response) {
	Debug(&__plugin,DebugWarn,"Server %s:%d returned %d but we were expecting Accounting_Response",
	    m_server.c_str(),m_acctPort,response);
	return ServerErr;
    }
    Debug(&__plugin,DebugInfo,"Server returned Accounting-Response");
    return AcctSuccess;
}

// Add one text attribute
bool RadiusClient::addAttribute(const char* attrib, const char* val, bool emptyOk)
{
    if (null(attrib))
	return false;
    if (null(val))
	return emptyOk;
    RadAttrib* attr = new RadAttrib(attrib,val);
    if (attr->isValid()) {
	m_attribs.append(attr);
	return true;
    }
    attr->destruct();
    return false;
}

// Add one numeric attribute
bool RadiusClient::addAttribute(const char* attrib, int val)
{
    if (null(attrib))
	return false;
    RadAttrib* attr = new RadAttrib(attrib,val);
    if (attr->isValid()) {
	m_attribs.append(attr);
	return true;
    }
    attr->destruct();
    return false;
}

// Add one text attribute with subtype
bool RadiusClient::addAttribute(const char* attrib, unsigned char subType, const char* val, bool emptyOk)
{
    if (null(attrib))
	return false;
    if (null(val))
	return emptyOk;
    RadAttrib* attr = new RadAttrib(attrib,subType,val);
    if (attr->isValid()) {
	m_attribs.append(attr);
	return true;
    }
    attr->destruct();
    return false;
}

// Copy from parameter list (usually message) to RADIUS attributes
void RadiusClient::addAttributes(NamedList& params, NamedList* list)
{
    if (!list)
	return;
    DDebug(&__plugin,DebugInfo,"Adding attributes from section '%s'",list->c_str());
    unsigned int n = list->length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* s = list->getParam(i);
	if (!s)
	    continue;
	if ((s->name() == YSTRING("rad_include")) || s->name().startsWith("inc:",false)) {
	    if (*s == *list)
		Debug(&__plugin,DebugWarn,"Section '%s' includes itself!",s->c_str());
	    else
		addAttributes(params,s_cfg.getSection(*s));
	    continue;
	}
	if (s->name().startsWith("set:",false)) {
	    // set parameters in the params itself
	    String key = s->name().substr(4).trimBlanks();
	    if (key.null())
		continue;
	    String val = *s;
	    params.replaceParams(val);
	    params.setParam(key,val);
	    continue;
	}
	if (!s->name().startsWith("add:",false))
	    continue;
	// set RADIUS attributes
	String key = s->name().substr(4).trimBlanks();
	if (key.null())
	    continue;
	String val = *s;
	params.replaceParams(val);
	static const Regexp r("^\\([0-9]\\+\\):\\(.*\\)");
	if (key.matches(r)) {
	    int subType = key.matchString(1).toInteger(-1);
	    if ((subType >= 0) && (subType <= 255)) {
		key = key.matchString(2);
		addAttribute(key,subType,val);
	    }
	    else
		Debug(&__plugin,DebugWarn,"Invalid subtype in attribute '%s'",key.c_str());
	}
	else
	    addAttribute(key,val);
    }
}

// Find matching NAS section and populate attributes accordingly
bool RadiusClient::prepareAttributes(NamedList& params, bool forAcct, String* user)
{
    const char* caller = params.getValue("caller");
    const char* called = 0;
    if (s_shortnum) {
	// prefer short called over calledfull
	called = params.getValue("called");
	if (!called)
	    called = params.getValue("calledfull");
    }
    else {
	// prefer long calledfull over called
	called = params.getValue("calledfull");
	if (!called)
	    called = params.getValue("called");
    }
    const char* username = params.getValue("username");
    if (!username)
	username = params.getValue("authname");
    if (!username) {
	if (forAcct)
	    username = caller;
	// we were unable to build an username
	// don't even send such a request to PortaOne
	if (s_pb_enabled && !username)
	    return false;
    }
    Lock lock(s_cfgMutex);
    NamedList* nasSect = 0;
    String nasName;
    unsigned int n = s_cfg.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* sect = s_cfg.getSection(i);
	if (!sect)
	    continue;
	nasName = *sect;
	// section must be [nas] or [nas SOMETHING]
	if (!nasName.startSkip("nas"))
	    continue;
	// found section - see if it's enabled for auth or accounting
	if (!sect->getBoolValue(forAcct ? "rad_acct" : "rad_auth",true))
	    continue;
	unsigned int n2 = sect->length();
	for (unsigned int i2 = 0; i2 < n2; i2++) {
	    NamedString* pair = sect->getParam(i2);
	    if (!pair || pair->null())
		continue;
	    // ignore format control keys
	    if ((pair->name() == s_fmtCisco) || (pair->name() == s_fmtQuintum))
		continue;
	    // ignore keys like rad_SOMETHING or SOMETHING:SOMETHING
	    if (pair->name().startsWith("rad_",false) ||
		(pair->name().find(':') >= 0))
		continue;
	    Regexp r(pair->c_str());
	    const char* val = c_safe(params.getValue(pair->name()));
	    if (!r.matches(val)) {
		// mismatch - skip this section
		sect = 0;
		break;
	    }
	}
	if (sect) {
	    nasSect = sect;
	    break;
	}
    }
    if (!nasSect)
	return false;

    // find the name of the corresponding server section
    NamedString* serv = nasSect->getParam("rad_server");
    String servName("radius");
    if (serv) {
	// explicit empty name means get out
	if (serv->null())
	    return false;
	servName << " " << *serv;
    }
    else if (nasName)
	servName << " " << nasName;

    // find the section, exit if empty
    NamedList* servSect = s_cfg.getSection(servName);
    if (!servSect) {
	Debug(&__plugin,DebugWarn,"Section [%s] does not exist",servName.c_str());
	return false;
    }

    if (!setRadServer(*servSect)) {
	Debug(&__plugin,DebugWarn,"Section [%s] does not hold a valid server",servName.c_str());
	return false;
    }

    // remember the name of the NAS section
    m_section = *nasSect;

    Debug(&__plugin,DebugInfo,"Using sections [%s] and [%s] for %s",
	m_section.c_str(),servName.c_str(),forAcct ? "accounting" : "authentication");
    m_cisco = nasSect->getBoolValue(s_fmtCisco,servSect->getBoolValue(s_fmtCisco,s_cisco));
    m_quintum = nasSect->getBoolValue(s_fmtQuintum,servSect->getBoolValue(s_fmtQuintum,s_quintum));
    addAttribute("User-Name",username);
    addAttribute("Calling-Station-Id",caller);
    addAttribute("Called-Station-Id",called);
    addAttributes(params,nasSect);
    addAttributes(params,servSect);
    if (user)
	*user = username;
    return true;
}

// Copy some attributes back from RADIUS answer to parameter list (message)
bool RadiusClient::returnAttributes(NamedList& params, const ObjList* attributes, bool ok)
{
    Lock lock(s_cfgMutex);
    NamedList* sect = s_cfg.getSection(m_section);
    if (!sect)
	return false;

    String attrDump;
    for (; attributes; attributes = attributes->next()) {
	const RadAttrib* attr = static_cast<const RadAttrib*>(attributes->get());
	if (!attr)
	    continue;
	if (s_printAttr && __plugin.debugAt(DebugAll)) {
	    String val;
	    attr->getString(val);
	    attrDump << "\r\n  " << attr->name() << "='" << val << "'";
	}
	String tmp(ok ? "ret:" : "ret-fail:");
	tmp += attr->name();
	String* par = sect->getParam(tmp);
	if (par && *par) {
	    attr->getString(tmp);
	    if (!params.getParam(*par)) {
		params.addParam(*par,tmp);
		continue;
	    }
	    // Handle duplicate params
	    int count = 0;
	    while (++count) {
		if (params.getParam(*par + "." + String(count)))
		    continue;
		params.addParam(*par + "." + String(count),tmp);
		break;
	    }
	}
    }
    if (attrDump)
	Debug(&__plugin,DebugAll,"Returned attributes:%s",attrDump.c_str());
    return true;
}


bool AuthHandler::received(Message& msg)
{
    if (!msg.getBoolValue("auth_radius",true))
	return false;
    String proto = msg.getValue("protocol",msg.getValue("module"));
    if (proto.null())
	return false;
    RadiusClient radclient;
    // preserve the actually authenticated username in case we succeed
    String user;
    if (!radclient.prepareAttributes(msg,false,&user))
	return false;
    bool cisco = msg.getBoolValue(s_fmtCisco,radclient.addCisco());
    bool quintum = msg.getBoolValue(s_fmtQuintum,radclient.addQuintum());
    // TODO: process plaintext password
    if ((proto == "digest") || (proto == "sip")) {
	const char* resp = msg.getValue("response");
	const char* nonce = msg.getValue("nonce");
	const char* method = msg.getValue("method");
	const char* uri = msg.getValue("uri");
	const char* user = msg.getValue("username");
	if (resp && nonce && method && uri && user) {
	    // mandatory auth parameters
	    if (!(
		radclient.addAttribute("Digest-Response",resp) &&
		radclient.addAttribute("Digest-Attributes",Digest_Nonce,nonce) &&
		radclient.addAttribute("Digest-Attributes",Digest_Method,method) &&
		radclient.addAttribute("Digest-Attributes",Digest_URI,uri) &&
		radclient.addAttribute("Digest-Attributes",Digest_UserName,user)
	    ))
		return false;
	    // optional auth parameters
	    radclient.addAttribute("Digest-Attributes",Digest_Realm,msg.getValue("realm"));
	    radclient.addAttribute("Digest-Attributes",Digest_Algo,msg.getValue("algorithm","MD5"));
	    radclient.addAttribute("Digest-Attributes",Digest_QOP,msg.getValue("qop"));
	}
    }

    String address = msg.getValue("address");
    // suppress any port number - IMHO this is stupid
    int sep = address.find(':');
    if (sep >= 0)
	address = address.substr(0,sep);
    if (cisco)
	radclient.addAttribute("h323-remote-address",address);
    if (quintum)
	radclient.addAttribute("Quintum-h323-remote-address",address);
    if (cisco || quintum) {
	const String& billid = msg[YSTRING("billid")];
	if (billid) {
	    // create a Cisco-compatible conference ID
	    MD5 cid(billid);
	    String confid;
	    confid << cid.hexDigest().substr(0,8) << " ";
	    confid << cid.hexDigest().substr(8,8) << " ";
	    confid << cid.hexDigest().substr(16,8) << " ";
	    confid << cid.hexDigest().substr(24,8);
	    confid.toUpper();
	    String tmp("call-id=");
	    if (address.null())
		address = s_localAddr.host();
	    tmp << billid << "@" << address;
	    if (cisco) {
		radclient.addAttribute("h323-conf-id",confid);
		radclient.addAttribute("Cisco-AVPair",tmp);
	    }
	    if (quintum) {
		radclient.addAttribute("Quintum-h323-conf-id",confid);
		radclient.addAttribute("Quintum-AVPair",tmp);
	    }
	}
    }

    ObjList result;
    if (radclient.doAuthenticate(&result) != AuthSuccess) {
	radclient.returnAttributes(msg,&result,false);
	return false;
    }
    // copy back the username we actually authenticated
    if (user)
	msg.setParam("username",user);
    // and pick whatever other parameters we want to return
    radclient.returnAttributes(msg,&result,true);
    if (s_pb_enabled)
	portaBillingRoute(msg,&result);
    // signal we don't return a password
    msg.retValue().clear();
    return true;
}


// Build a Cisco style (like NTP) date/time string
static bool ciscoTime(double t, String& ret)
{
    time_t sec = (time_t)floor(t);
    unsigned int msec = (unsigned int)(1000.0 * (t - sec));
    // we need to protect localtime/gmtime which may not be thread safe
    static Mutex mutex(false,"YRadius::ciscoTime");
    Lock lock(mutex);
    struct tm* brokenTime = s_localTime ? localtime(&sec) : gmtime(&sec);
    if (!brokenTime)
	return false;
    char buf[64];
    ret.clear();
    if (!strftime(buf,sizeof(buf),"%H:%M:%S",brokenTime))
	return false;
    ret = buf;
    sprintf(buf,".%03u ",msec);
    ret << buf;
    if (!strftime(buf,sizeof(buf),"%Z %a %b %d %Y",brokenTime)) {
	ret.clear();
	return false;
    }
    ret << buf;
    return false;
}


bool AcctHandler::received(Message& msg)
{
    if (!msg.getBoolValue("cdrwrite_radius",true))
	return false;
    String op = msg.getValue("operation");
    int acctStat = 0;
    if (op == "initialize")
	acctStat = Acct_Start;
    else if (op == "finalize")
	acctStat = Acct_Stop;
    else if (op == "status")
	acctStat = Acct_Alive;
    else
	return false;

    String billid = msg.getValue("billid");
    if (billid.null())
	return false;

    String address = msg.getValue("address");
    int sep = address.find(':');
    if (sep >= 0)
	address = address.substr(0,sep);

    String dir = msg.getValue("direction");
    if (dir == "incoming")
	dir = "answer";
    else if (dir == "outgoing")
	dir = "originate";
    else
	return false;

    RadiusClient radclient;
    if (!radclient.prepareAttributes(msg))
	return false;
    bool cisco = msg.getBoolValue(s_fmtCisco,radclient.addCisco());
    bool quintum = msg.getBoolValue(s_fmtQuintum,radclient.addQuintum());

    // create a Cisco-compatible conference ID
    MD5 cid(billid);
    String confid;
    confid << cid.hexDigest().substr(0,8) << " ";
    confid << cid.hexDigest().substr(8,8) << " ";
    confid << cid.hexDigest().substr(16,8) << " ";
    confid << cid.hexDigest().substr(24,8);
    confid.toUpper();

    // cryptographically generate an unique call leg ID
    MD5 sid(billid);
    sid << msg.getValue("chan");

    radclient.addAttribute("Acct-Session-Id",sid.hexDigest());
    radclient.addAttribute("Acct-Status-Type",acctStat);
    if (cisco) {
	radclient.addAttribute("h323-call-origin",dir);
	radclient.addAttribute("h323-conf-id",confid);
	radclient.addAttribute("h323-remote-address",address);
    }
    if (quintum) {
	radclient.addAttribute("Quintum-h323-call-origin",dir);
	radclient.addAttribute("Quintum-h323-conf-id",confid);
	radclient.addAttribute("Quintum-h323-remote-address",address);
    }

    String tmp("call-id=");
    if (address.null())
	address = s_localAddr.host();
    tmp << billid << "@" << address;
    if (cisco)
	radclient.addAttribute("Cisco-AVPair",tmp);
    if (quintum)
	radclient.addAttribute("Quintum-AVPair",tmp);

    double t = msg.getDoubleValue("time");
    if (cisco || quintum) {
	ciscoTime(t,tmp);
	if (cisco)
	    radclient.addAttribute("h323-setup-time",tmp);
	if (quintum)
	    radclient.addAttribute("Quintum-h323-setup-time",tmp);
    }
    double duration = msg.getDoubleValue("duration",-1);
    double billtime = msg.getDoubleValue("billtime");
    if ((cisco || quintum) && (Acct_Start != acctStat) && (duration >= 0.0)) {
	if (billtime > 0.0) {
	    ciscoTime(t+duration-billtime,tmp);
	    if (cisco)
		radclient.addAttribute("h323-connect-time",tmp);
	    if (quintum)
		radclient.addAttribute("Quintum-h323-connect-time",tmp);
	}
    }

    if (Acct_Stop == acctStat) {
	if ((cisco || quintum) && (duration >= 0.0)) {
	    ciscoTime(t+duration,tmp);
	    if (cisco)
		radclient.addAttribute("h323-disconnect-time",tmp);
	    if (quintum)
		radclient.addAttribute("Quintum-h323-disconnect-time",tmp);
	}

	radclient.addAttribute("Acct-Session-Time",(int)billtime);
	int cause = lookup(msg.getValue("status"),dict_errors,-1,10);
	if (cause >= 0)
	    radclient.addAttribute("Acct-Terminate-Cause",cause);
	String tmp = msg.getValue("reason");
	if (tmp) {
	    tmp = "disconnect-text=" + tmp;
	    if (cisco)
		radclient.addAttribute("Cisco-AVPair",tmp);
	    if (quintum)
		radclient.addAttribute("Quintum-AVPair",tmp);
	}
    }
    radclient.doAccounting();
    return false;
}

bool RadiusHandler::received(Message& msg)
{
    bool auth = msg.getBoolValue(YSTRING("auth"),true);
    int acctStat = 0;
    if (!auth) {
	String op = msg.getValue("operation");
	if (op == "initialize")
	    acctStat = Acct_Start;
	else if (op == "finalize")
	    acctStat = Acct_Stop;
	else if (op == "status") {
	    acctStat = Acct_Alive;
	} else
	    return false;
    }
    RadiusClient radclient;
    if (!radclient.prepareAttributes(msg))
	return false;

    if (!auth) {
	radclient.addAttribute("Acct-Status-Type",acctStat);
    }

    ObjList result;
    if (auth && radclient.doAuthenticate(&result) != AuthSuccess) {
	radclient.returnAttributes(msg,&result,false);
	return false;
    } else if (!auth && radclient.doAccounting(&result) != AuthSuccess) {
	radclient.returnAttributes(msg,&result,false);
	return false;
    }

    radclient.returnAttributes(msg,&result,true);
    return true;
}

RadiusModule::RadiusModule()
    : Module("yradius","misc"), m_init(false)
{
    Output("Loaded module Radius client");
}

RadiusModule::~RadiusModule()
{
    Output("Unloaded module Radius client");
}

void RadiusModule::initialize()
{
    Output("Initializing module Radius client");
    s_cfgMutex.lock();
    s_cfg = Engine::configFile("yradius");
    s_cfg.load();
    s_localTime = s_cfg.getBoolValue("general","local_time",false);
    s_shortnum = s_cfg.getBoolValue("general","short_number",false);
    s_unisocket = s_cfg.getBoolValue("general","single_socket",false);
    s_printAttr = s_cfg.getBoolValue("general","print_attributes",false);
    s_pb_enabled = s_cfg.getBoolValue("portabill","enabled",false);
    s_pb_parallel = s_cfg.getBoolValue("portabill","parallel",false);
    s_pb_simplify = s_cfg.getBoolValue("portabill","simplify",false);
    s_cisco = s_cfg.getBoolValue("general",s_fmtCisco,true);
    s_quintum = s_cfg.getBoolValue("general",s_fmtQuintum,true);
    s_pb_stoperror = s_cfg.getValue("portabill","stoperror","busy");
    s_pb_maxcall = s_cfg.getValue("portabill","maxcall");
    s_cfgMutex.unlock();

    if (m_init || !s_cfg.getBoolValue("general","enabled",true))
	return;

    s_localAddr.host(s_cfg.getValue("general","addr"));
    s_localAddr.port(s_cfg.getIntValue("general","port",1810));

    if (s_localAddr.host().null()) {
	Debug(this,DebugNote,"Local address not set or invalid. Radius functions disabled");
	return;
    }

    if (!(s_localAddr.valid() && s_localAddr.host() && s_localAddr.port())) {
	Debug(this,DebugWarn,"Invalid address %s:%d. Radius functions unavailable",
	    s_localAddr.host().c_str(),s_localAddr.port());
	return;
    }

    // we only have UDP support
    if (!s_localSock.create(PF_INET,SOCK_DGRAM,IPPROTO_IP)) {
	Alarm(this,"socket",DebugGoOn,"Error creating socket. Radius functions unavailable");
	return;
    }
    if (!s_localSock.bind(s_localAddr)) {
	Alarm(this,"socket",DebugWarn,"Error %d binding to %s:%d. Radius functions unavailable",
	    s_localSock.error(),s_localAddr.host().c_str(),s_localAddr.port());
	return;
    }

    m_init = true;
    setup();

    Engine::install(new AuthHandler(s_cfg.getIntValue("general","auth_priority",70)));
    Engine::install(new AcctHandler(s_cfg.getIntValue("general","acct_priority",70)));
    Engine::install(new RadiusHandler(100));
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
