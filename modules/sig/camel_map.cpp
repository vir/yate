/**
 * camel_map.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * MAP/CAMEL TCAP <-> XML translators
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
#include <string.h>
#include <yatexml.h>
#include <yateasn.h>

using namespace TelEngine;

namespace {

class TcapXUser;
class TcapXModule;
class XMLConnListener;
class XMLConnection;
class TcapXApplication;
class TcapToXml;
class XmlToTcap;
class StringList;
struct MapCamelType;
struct Parameter;
struct Operation;

struct XMLMap {
    Regexp name;
    const char* map;
    const char* tag;
    int type;
};

struct TCAPMap {
    const char* path;
    bool isPrefix;     // for this path, XML children are expected to exist
    const String name;
};

struct OpTable {
    const Operation* mainTable;
    const OpTable* fallbackTable;
};

struct AppCtxt {
    const char* name;
    const char* oid;
    const ObjList& ops;
    const OpTable* opTable;
};

struct Capability {
    const char* name;
    const ObjList& ops;
};

class StringList
{
public:
    StringList(const char* list, char sep = ',');
    virtual ~StringList();
    inline operator const ObjList&() const
	{ return *m_list; }
private:
    ObjList* m_list;
};

class MyDomParser : public XmlDomParser
{
public:
    MyDomParser(TcapXApplication* app, const char* name = "MyDomParser", bool fragment = false);
    virtual ~MyDomParser();

protected:
    virtual void gotElement(const NamedList& element, bool empty);
    virtual void endElement(const String& name);
    void verifyRoot();

private:
    TcapXApplication* m_app;
};

class XMLConnection : public Thread
{
public:
    XMLConnection(Socket* skt, TcapXApplication* app);
    ~XMLConnection();
    // inherited from thread
    void start();
    void run();
    void cleanup();

    bool writeData(XmlFragment* frag);
private:
    Socket* m_socket;
    String m_address;
    TcapXApplication* m_app;
    MyDomParser m_parser;
};

class XMLConnListener : public Thread
{
public:
    XMLConnListener(TcapXUser* user, const NamedList& sect);
    ~XMLConnListener();
    virtual bool init();
    virtual void run();
    void cleanup();
    bool createConn(Socket* skt, String& addr);
protected:
    // the socket on which we accept connections
    TcapXUser* m_user;
    Socket m_socket;
    String m_host;
    int m_port;
};

class Transaction : public NamedString
{
public:
    inline Transaction(const char* tcapID, const char* appID, const AppCtxt* ctxt = 0)
	: NamedString(tcapID,appID), m_ctxt(ctxt)
	{}
    inline ~Transaction()
	{}
    inline const AppCtxt* context()
	{ return m_ctxt; }
    inline void setContext(AppCtxt* ctxt)
	{ m_ctxt = ctxt; }

private:
    const AppCtxt* m_ctxt;
};

class IDMap : public ObjList
{
public:
    inline IDMap()
	{}
    inline ~IDMap()
	{}
    void appendID(const char* tcapID, const char* appID, const AppCtxt* ctxt = 0);
    const String& findTcapID(const char* appID);
    const String& findAppID(const char* tcapID);
    Transaction*  findByAppID(const char* appID);
    Transaction*  findByTcapID(const char* tcapID);
};

class XmlToTcap : public GenObject, public Mutex
{
public:
    enum MsgType {
	Unknown,
	Capability,
	Tcap,
    };
    XmlToTcap(TcapXApplication* app);
    ~XmlToTcap();
    inline bool hasDeclaration()
	{ return m_decl != 0; }
    inline XmlElement* message()
	{ return m_elem; }
    inline MsgType type()
	{ return m_type; }
    bool validDeclaration();
    bool valid(XmlDocument* doc);
    bool checkXmlns();
    bool parse(NamedList& tcapParams);
    bool parse(NamedList& tcapParams, XmlElement* elem, String prefix, const AppCtxt* ctxt);
    bool handleComponent(NamedList& tcapParams, XmlElement* elem, const AppCtxt* ctxt);
    bool handleMAPDialog(NamedList& tcapParams, XmlElement* elem, String prefix);
    void reset();
    static const TCAPMap s_tcapMap[];
    const TCAPMap* findMap(String& path);
    bool encodeComponent(DataBlock& payload, XmlElement* elem, bool searchArgs, int& err, Operation* op);
    void encodeOperation(Operation* op, XmlElement* elem, DataBlock& payload, int& err, bool searchArgs);
private:
    TcapXApplication* m_app;
    XmlDeclaration* m_decl;
    XmlElement* m_elem;
    MsgType m_type;
};

class TcapToXml : public GenObject, public Mutex
{
public:
    enum MsgType {
	Unknown,
	State,
	Tcap,
    };
    enum XmlType {
	None,
	Element,
	NewElement,
	Attribute,
	Value,
	End,
    };
    TcapToXml(TcapXApplication* app);
    ~TcapToXml();
    inline MsgType type()
	{ return m_type; }
    bool buildXMLMessage(NamedList& msg, XmlFragment* frag, MsgType type, const AppCtxt* ctxt = 0);
    static const XMLMap s_xmlMap[];
    void reset();
    void handleMAPDialog(XmlElement* root, NamedList& params);
    bool decodeDialogPDU(XmlElement* el, const AppCtxt* ctxt, DataBlock& data);
    XmlElement* addToXml(XmlElement* root, const XMLMap* map, NamedString* val);
    void addComponentsToXml(XmlElement* root, NamedList& params, const AppCtxt* ctxt);
    const XMLMap* findMap(String& elem);
    void addParametersToXml(XmlElement* elem, String& payloadHex, Operation* op, bool searchArgs = true);
    void decodeTcapToXml(TelEngine::XmlElement*, TelEngine::DataBlock&, Operation* op, unsigned int index = 0, bool seachArgs = true);
    bool decodeOperation(Operation* op, XmlElement* elem, DataBlock& data, bool searchArgs = true);
private:
    TcapXApplication* m_app;
    MsgType m_type;
};

class TcapXUser : public TCAPUser, public Mutex
{
public:
    enum UserType {
	MAP,
	CAMEL,
    };
    TcapXUser(const char* name);
    ~TcapXUser();
    virtual bool tcapIndication(NamedList& params);
    virtual bool managementNotify(SCCP::Type type, NamedList& params);
    bool findTCAP(const char* name);
    int managementState();
    void notifyManagementState(bool forced = false);
    bool initialize(NamedList& sect);
    bool createApplication(Socket* skt, String& addr);
    void setListener(XMLConnListener* list);
    void removeApp(TcapXApplication* app);
    SS7TCAPError applicationRequest(TcapXApplication* app, NamedList& params, int reqType);
    bool sendToApp(NamedList& params, TcapXApplication* app = 0, bool saveID = true);
    TcapXApplication* findApplication(NamedList& params);
    void reorderApps(TcapXApplication* app);
    inline UserType type()
	{ return m_type; }
    inline bool printMessages()
	{ return m_printMsg; }
    inline bool addEncoding()
	{ return m_addEnc; }
    inline unsigned int applicationCount()
    {
	Lock l(m_appsMtx);
	return m_apps.count();
    }
    void statusString(String& str);
protected:
    ObjList m_apps;
    Mutex m_appsMtx;
    XMLConnListener* m_listener;
    ObjList m_trIDs;
    UserType m_type;
    bool m_printMsg;
    bool m_addEnc;
    SCCPManagement::LocalBroadcast m_mngtStatus;
};

class TcapXApplication : public RefObject, public Mutex
{
public:
    enum State {
	Waiting,
	Active,
	ShutDown,
	Inactive,
    };
    enum ParamType {
	Unknown,
	Null,
	Bool,
	Integer,
	OID,
	HexString,
	BitString,
	TBCD,
	AddressString,
	AppString,
	Enumerated,
	Choice,
	Sequence,
	SequenceOf,
	SetOf,
	GSMString,
	Flags,
	CellIdFixedLength,
	LAIFixedLength,
	CalledPartyNumber,
	CallingPartyNumber,
	LocationNumber,
	OriginalCalledNumber,
	RedirectingNumber,
	GenericNumber,
	ChargeNumber,
	HiLayerCompat,
	UserServiceInfo,
	RedirectionInformation,
	None,
    };
    enum EncType {
	BoolEnc,
	IntEnc,
	OIDEnc,
	StringEnc,
	NullEnc,
	HexEnc,
	TelephonyEnc,
	NoEnc,
    };
    enum Error {
	NoError,
	DataMissing,
	UnexpectedDataValue,
    };
    TcapXApplication(const char* name, Socket* skt, TcapXUser* user);
    ~TcapXApplication();
    bool hasCapability(const char* oper);
    bool handleXML();
    void receivedXML(XmlDocument* frag);
    const String& toString() const
	{ return m_name; }
    void setIO(XMLConnection* io);
    void reportState(State state, const char* error = 0);
    void closeConnection();
    bool supportCapability(const String& capab);
    void sendStateResponse(const char* error = 0);
    bool handleCapability(NamedList& params);
    bool handleTcap(NamedList& params);
    bool handleIndication(NamedList& params);
    void reportError(const char* err);
    bool sendTcapMsg(NamedList& params, const AppCtxt* ctxt = 0);
    bool canHandle(NamedList& params);
    void status(NamedList& status);
    const AppCtxt* findCtxt(const String& appID, const String& remoteID);
    inline State state()
	{ return m_state; }
    inline unsigned int trCount()
	{ Lock l(this); return m_ids.count() + m_pending.count(); }
    inline TcapXUser::UserType type()
	{ return m_type; }
    inline bool addEncoding()
	{ return (m_user ? m_user->addEncoding() : false); }

private:
    IDMap m_ids;
    IDMap m_pending;
    ObjList m_capab;
    XMLConnection* m_io;
    String m_name;
    TcapXUser* m_user;
    unsigned int m_sentXml;
    unsigned int m_receivedXml;
    unsigned int m_sentTcap;
    unsigned int m_receivedTcap;
    State m_state;
    TcapToXml m_tcap2Xml;
    XmlToTcap m_xml2Tcap;
    TcapXUser::UserType m_type;
};

class TcapXModule : public Module
{
public:
    TcapXModule();
    virtual ~TcapXModule();
    //inherited methods
    virtual void initialize();
    // uninstall relays and message handlers
    bool unload();
    inline bool showMissing()
	{ return m_showMissing; }
protected:
    // inherited methods
    virtual bool received(Message& msg, int id);
    virtual void statusModule(String& str);
    void initUsers(Configuration& cfg);
    virtual void statusParams(String& str);
    virtual void statusDetail(String& str);
    unsigned int applicationCount();
private:
    Mutex m_usersMtx;
    ObjList m_users;
    bool m_showMissing;
};

INIT_PLUGIN(TcapXModule);

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow && !__plugin.unload())
	return false;
    return true;
}

void printMissing(const char* missing, const char* parent, bool atEncoding = true)
{
    if (__plugin.showMissing())
	Debug(&__plugin,DebugMild,
	    (atEncoding ?
		"Missing mandatory child '%s' in XML parent '%s'" :
		"Missing mandatory parameter '%s' in payload for '%s'"),
	    missing,parent);
}

const String s_namespace = "http://yate.null.ro/xml/tcap/v1";

static const String s_msgTag = "m";
static const String s_capabTag = "c";
static const String s_component = "component";
static const String s_typeStr = "type";
static const String s_tagAttr = "tag";
static const String s_encAttr = "enc";
static const String s_qualifierAttr = "qualifier";
static const String s_planAttr = "plan";
static const String s_natureAttr = "nature";
static const String s_innAttr = "inn";
static const String s_completeAttr = "complete";
static const String s_restrictAttr = "restrict";
static const String s_screenedtAttr = "screened";
static const String s_userInformation = "userInformation";
static const String s_encodingContentsTag = "encoding-contents";
static const String s_directReferenceTag = "direct-reference";
static const String s_appContext = "application";
static const String s_localTID = "localTID";
static const String s_remoteTID = "remoteTID";

static const String s_tcapUser = "tcap.user";
static const String s_tcapRequestError = "tcap.request.error";
static const String s_tcapLocalTID = "tcap.transaction.localTID";
static const String s_tcapRemoteTID = "tcap.transaction.remoteTID";
static const String s_tcapEndNow = "tcap.transaction.endNow";
static const String s_tcapAppCtxt = "tcap.dialogPDU.application-context-name";
static const String s_tcapDirectReference = "tcap.dialogPDU.userInformation.direct-reference";
static const String s_tcapEncodingContent = "tcap.dialogPDU.userInformation.encoding-contents";
static const String s_tcapEncodingType = "tcap.dialogPDU.userInformation.encoding-type";
static const String s_tcapReqType = "tcap.request.type";
static const String s_tcapCompCount = "tcap.component.count";
static const String s_tcapCompPrefix = "tcap.component";
static const String s_tcapCompPrefixSep = "tcap.component.";
static const String s_tcapAbortCause = "tcap.transaction.abort.cause";
static const String s_tcapAbortInfo = "tcap.transaction.abort.information";
static const String s_tcapCompType = "componentType";
static const String s_tcapOpCodeType = "operationCodeType";
static const String s_tcapOpCode = "operationCode";
static const String s_tcapOpClass = "operationClass";
static const String s_tcapErrCodeType = "errorCodeType";
static const String s_tcapErrCode = "errorCode";
static const String s_tcapProblemCode = "problemCode";

static NamedString s_encodingPath(s_tcapEncodingContent,"");

static const TokenDict s_tagTypes[] = {
    {"universal",    AsnTag::Universal},
    {"application",  AsnTag::Application},
    {"context",      AsnTag::Context},
    {"private",      AsnTag::Private},
    {"",    -1},
};

struct Parameter {
    const String name;
    const AsnTag& tag;
    bool isOptional;
    TcapXApplication::ParamType type;
    const void* content;
};

struct Operation {
    const String name;
    bool local;
    int code;
    int opClass;
    const AsnTag& argTag;
    const Parameter* args;
    const AsnTag& retTag;
    const Parameter* res;
};

static const TokenDict s_dict_numNature[] = {
    { "unknown",             0x00 },
    { "international",       0x10 },
    { "national",            0x20 },
    { "network-specific",    0x30 },
    { "subscriber",          0x40 },
    { "reserved",            0x50 },
    { "abbreviated",         0x60 },
    { "extension-reserved",  0x70 },
    { 0, 0 },
};

// Numbering Plan Indicator
static const TokenDict s_dict_numPlan[] = {
    { "unknown",      0 },
    { "isdn",         1 },
    { "data",         3 },
    { "telex",        4 },
    { "land-mobile",  6 },
    { "isdn-mobile",  7 },
    { "national",     8 },
    { "private",      9 },
    { "extension-reserved",      15 },
    { 0, 0 },
};

struct MapCamelType {
    TcapXApplication::ParamType type;
    TcapXApplication::EncType encoding;
    bool (*decode)(const Parameter*, MapCamelType*, AsnTag& tag, DataBlock&, XmlElement*, bool, int& err);
    bool (*encode)(const Parameter*, MapCamelType*, DataBlock&, XmlElement*, int& err);
};

static const MapCamelType* findType(TcapXApplication::ParamType type);

static AsnTag s_sequenceTag(AsnTag::Universal,AsnTag::Constructor,16);
static AsnTag s_intTag(AsnTag::Universal, AsnTag::Primitive, 2);
static AsnTag s_bitsTag(AsnTag::Universal, AsnTag::Primitive, 3);
static AsnTag s_nullTag(AsnTag::Universal, AsnTag::Primitive, 5);
static AsnTag s_oidTag(AsnTag::Universal, AsnTag::Primitive, 6);
static AsnTag s_hexTag(AsnTag::Universal, AsnTag::Primitive, 4);
static AsnTag s_numStrTag(AsnTag::Universal, AsnTag::Primitive, 18);
static AsnTag s_noTag(AsnTag::Universal, AsnTag::Primitive, 0);
static AsnTag s_enumTag(AsnTag::Universal, AsnTag::Primitive, 10);
static AsnTag s_boolTag(AsnTag::Universal, AsnTag::Primitive, 1);

static const Parameter* findParam(const Parameter* param, const String& tag)
{
    if (!param)
	return 0;
    XDebug(DebugAll,"findParam(param=%s,tag=%s)",param->name.c_str(),tag.c_str());
    if (param->type == TcapXApplication::Choice && param->content) {
	const Parameter* p = static_cast<const Parameter*>(param->content);
	if (p)
	    return findParam(p,tag);
    }
    while (param && !TelEngine::null(param->name)) {
	if (tag == param->name)
	    return param;
	param++;
    }
    return 0;
}

static bool encodeParam(const Parameter* param, DataBlock& data, XmlElement* elem, int& err);

static bool encodeRaw(const Parameter* param, DataBlock& payload, XmlElement* elem, int& err)
{
    if (!elem)
	return true;

    XDebug(&__plugin,DebugAll,"encodeRaw(param=[%p],elem=%s[%p])",param,elem->getTag().c_str(),elem);
    bool hasChildren = false;
    bool status = true;
    while (XmlElement* child = elem->pop()) {
	hasChildren = true;
	DataBlock db;
	Parameter* p = (Parameter*)findParam(param,elem->getTag());
	if (p)
	    status = encodeParam(p,db,child,err);
	else
	    status = encodeRaw(p,db,child,err);
	payload.append(db);
	TelEngine::destruct(child);
	if (!status)
	    break;
    }
    AsnTag tag;
    const String* clas = elem->getAttribute(s_typeStr);
    bool checkParam = (param && !TelEngine::null(param->name));
    if (TelEngine::null(clas)) {
	if (checkParam)
	    tag.classType(param->tag.classType());
	else {
	    Debug(DebugMild,"In <%s> missing %s=\"...\" attribute!",elem->getTag().c_str(),s_typeStr.c_str());
	    return false;
	}
    }
    else
	tag.classType((AsnTag::Class)lookup(*clas,s_tagTypes,AsnTag::Universal));
    clas = elem->getAttribute(s_tagAttr);
    if (TelEngine::null(clas)) {
	if (checkParam)
	    tag.code(param->tag.code());
	else {
	    Debug(DebugMild,"In <%s> missing %s=\"...\" attribute!",elem->getTag().c_str(),s_tagAttr.c_str());
	    return false;
	}
    }
    else
	tag.code(clas->toInteger());

    const String& text = elem->getText();
    if (!hasChildren) {
	clas = elem->getAttribute(s_encAttr);
	if (TelEngine::null(clas)) {
	    if (text) {
		Debug(DebugMild,"In <%s> missing %s=\"...\" attribute!",elem->getTag().c_str(),s_encAttr.c_str());
		return false;
	    }
	    payload.clear();
	    tag.type(param ? param->tag.type() : AsnTag::Primitive);
	    clas = &String::empty();
	}
	else
	    tag.type(AsnTag::Primitive);
	if (*clas == "hex")
	    payload.unHexify(text.c_str(),text.length(),' ');
	else if (*clas == "int")
	    payload.insert(ASNLib::encodeInteger(text.toInteger(),false));
	else if (*clas == "str")
	    payload.insert(ASNLib::encodeUtf8(text,false));
	else if (*clas == "null")
	    payload.clear();
	else if (*clas == "oid") {
	    ASNObjId obj = text;
	    payload.insert(ASNLib::encodeOID(obj,false));
	}
	else if (*clas == "bool")
	    payload.insert(ASNLib::encodeBoolean(text.toBoolean(),false));
	payload.insert(ASNLib::buildLength(payload));
	AsnTag::encode(tag.classType(),tag.type(),tag.code(),payload);
    }
    else {
	tag.type(AsnTag::Constructor);
	payload.insert(ASNLib::buildLength(payload));
	AsnTag::encode(tag.classType(),tag.type(),tag.code(),payload);
    }
    return true;
}

static bool encodeParam(const Parameter* param, DataBlock& data, XmlElement* elem, int& err)
{
    if (!(param && elem))
	return false;
    XDebug(&__plugin,DebugAll,"encodeParam(param=%s[%p],elem=%s[%p])",param->name.c_str(),param,elem->getTag().c_str(),elem);
    MapCamelType* type = const_cast<MapCamelType*>(findType(param->type));
    bool ok = true;
    if (!type)
	ok = encodeRaw(param,data,elem,err);
    else {
	XmlElement* child = (elem->getTag() == s_component || elem->getTag() == s_encodingContentsTag ? elem->findFirstChild(&param->name) : elem);
	if (!child) {
	    if (!param->isOptional)
		return false;
	    return true;
	}
	if (child->getAttribute(s_tagAttr) || child->getAttribute(s_encAttr))
	    ok = encodeRaw(param,data,child,err);
	else {
	    if (param->tag.type() == AsnTag::Constructor &&
		!(param->type == TcapXApplication::HexString || param->type == TcapXApplication::SequenceOf
		|| param->type == TcapXApplication::Sequence || param->type == TcapXApplication::Choice
		|| param->type == TcapXApplication::SetOf))
		Debug(&__plugin,DebugGoOn,"Encoding definition conflict for param='%s', tag is defined as contructor"
		    " while its type is primitive",param->name.c_str());
	    ok = type->encode(param,type,data,child,err);
	}
	elem->removeChild(child);
    }
#ifdef XDEBUG
    String str;
    str.hexify(data.data(),data.length(),' ');
    Debug(&__plugin,DebugAll,"encodeParam(param=%s[%p],elem=%s[%p] has %ssucceeded, encodedData=%s)",param->name.c_str(),param,
		elem->getTag().c_str(),elem,(ok ? "" : "not "),str.c_str());
#endif
    return ok;
}

static bool decodeRaw(XmlElement* elem, DataBlock& data, bool singleParam = false)
{
    if (!(elem && data.length()))
	return false;
    DDebug(&__plugin,DebugAll,"decodeRaw(elem=%s[%p])",elem->getTag().c_str(),elem);
    while (data.length()) {
	AsnTag tag;
	AsnTag::decode(tag,data);

	data.cut(-(int)tag.coding().length());

	XmlElement* child = new XmlElement("u");
	elem->addChild(child);
	child->setAttributeValid(s_typeStr,lookup(tag.classType(),s_tagTypes,""));
	child->setAttributeValid(s_tagAttr,String((unsigned int)tag.code()));

	if (tag.type() == AsnTag::Primitive) {

	    String enc;
	    String value;
	    int len = 0;
	    u_int8_t fullTag = tag.classType() | tag.type() | tag.code();
	    switch (fullTag) {
		case ASNLib::BOOLEAN: {
			bool val;
			if (ASNLib::decodeBoolean(data,&val,false) < 0)
			    return false;
			value = String::boolText(val);
			enc = "bool";
		    }
		    break;
		case ASNLib::INTEGER: {
			u_int64_t val;
			if (ASNLib::decodeInteger(data,val,sizeof(val),false) < 0)
			    return false;
			    // to do fix conversion from u_int64_t
			value = String((int)val);
			enc = "int";
		    }
		    break;
		case ASNLib::OBJECT_ID: {
			ASNObjId val;
			if (ASNLib::decodeOID(data,&val,false) < 0)
			    return false;
			value = val.toString();
			enc = "oid";
		    }
		    break;
		case ASNLib::UTF8_STR: {
			if (ASNLib::decodeUtf8(data,&enc,false) < 0)
			    return false;
			value = enc;
			enc = "str";
		    }
		    break;
		case ASNLib::NULL_ID:
		    if (ASNLib::decodeNull(data,false) < 0)
			return false;
		    enc = "null";
		    break;
		case ASNLib::NUMERIC_STR:
		case ASNLib::PRINTABLE_STR:
		case ASNLib::IA5_STR:
		case ASNLib::VISIBLE_STR: {
			int type;
			if (ASNLib::decodeString(data,&enc,&type,false) < 0)
			    return false;
			value = enc;
			enc = "str";
		    }
		    break;
		default:
		    len = ASNLib::decodeLength(data);
		    if (len < 0) {
			DDebug(&__plugin,DebugWarn,"decodeRaw() - invalid length=%d while decoding, stopping",len);
			return false;
		    }
		    value.hexify(data.data(),(len > (int)data.length() ? data.length() : len),' ');
		    data.cut(-len);
		    enc = "hex";
		    break;
	    }

	    child->setAttributeValid(s_encAttr,enc);
	    child->addText(value);
	}
	else {
	    int len = ASNLib::decodeLength(data);
	    DataBlock payload(data.data(),len);
	    data.cut(-len);
	    decodeRaw(child,payload);
	}
	if (singleParam)
	    break;
    }
    return true;
}

static bool decodeParam(const Parameter* param, AsnTag& tag, DataBlock& data, XmlElement* elem, bool addEnc, int& err)
{
    if (!(param && elem && data.length()))
	return false;
    String str;
    str.hexify(data.data(),data.length(),' ');

    XDebug(&__plugin,DebugAll,"decodeParam(param=%s[%p],elem=%s[%p]) - data = %s",param->name.c_str(),param,elem->getTag().c_str(),
	elem,str.c_str());
    MapCamelType* type = (MapCamelType*)findType(param->type);
    bool ok = true;
    if (!type)
	ok =  decodeRaw(elem,data,true);
    else
	ok = type->decode(param,type,tag,data,elem,addEnc,err);

    XDebug(&__plugin,DebugAll,"decodeParam(param=%s[%p],elem=%s[%p]) %s",param->name.c_str(),param,elem->getTag().c_str(),
	    elem, (ok ? "OK" : "FAILED"));
    return ok;
}

static unsigned int decodeBCD(unsigned int length, String& digits, unsigned char* buff)
{
    if (!(buff && length))
	return 0;
    static const char s_digits[] = "0123456789*#ABC";
    unsigned int index = 0;
    while (index < length) {
	digits += s_digits[(buff[index] & 0x0f)];
	u_int8_t odd = (buff[index] >> 4);
	if ((odd & 0x0f) != 0x0f)
	    digits += s_digits[(buff[index] >> 4)];
	index++;
    }
    XDebug(&__plugin,DebugAll,"Decoded BCD digits=%s",digits.c_str());
    return index;
}

void encodeBCD(const String& digits, DataBlock& data)
{
    XDebug(&__plugin,DebugAll,"encodeBCD(digit=%s)",digits.c_str());
    unsigned int len = digits.length() / 2 + (digits.length() % 2 ?  1 : 0);
    unsigned char buf[32];
    unsigned int i = 0;
    unsigned int j = 0;
    bool odd = false;
    while((i < digits.length()) && (j < len)) {
	char c = digits[i++];
	unsigned char d = 0;
	if (('0' <= c) && (c <= '9'))
	    d = c - '0';
	else if ('*' == c)
	    d = 10;
	else if ('#' == c)
	    d = 11;
	else if ('a' == c || 'A' == c)
	    d = 12;
	else if ('b' == c || 'B' == c)
	    d = 13;
	else if ('c' == c || 'C' == c)
	    d = 14;
// 	else if ('.' == c)
// 	    d = 15;
	else
	    continue;
	odd = !odd;
	if (odd)
	    buf[j] = d;
	else
	    buf[j++] |= (d << 4);
    }
    if (odd)
	buf[j++] |= 0xf0;

    data.append(&buf,j);
}

static bool decodeTBCD(const Parameter* param, MapCamelType* type, AsnTag& tag, DataBlock& data, XmlElement* parent, bool addEnc, int& err)
{
    if (!(param && type && data.length() && parent))
	return false;
    XDebug(&__plugin,DebugAll,"decodeTBCD(param=%s[%p],elem=%s[%p])",param->name.c_str(),param,parent->getTag().c_str(),parent);
    if (param->tag != tag)
	return false;

    data.cut(-(int)tag.coding().length());

    XmlElement* child = new XmlElement(param->name);
    parent->addChild(child);
    if (addEnc)
	child->setAttribute(s_encAttr,"str");
    int len = ASNLib::decodeLength(data);
    String digits;
    len = decodeBCD(len,digits,data.data(0,len));
    data.cut(-len);
    child->addText(digits);
    return true;
}

static bool encodeTBCD(const Parameter* param, MapCamelType* type, DataBlock& data, XmlElement* elem, int& err)
{
    if (!(param && elem))
	return false;
    XDebug(&__plugin,DebugAll,"encodeTBCD(param=%s[%p],elem=%s[%p])",param->name.c_str(),param,elem->getTag().c_str(),elem);
    const String& text = elem->getText();
    encodeBCD(text,data);
    data.insert(ASNLib::buildLength(data));
    data.insert(param->tag.coding());
    return true;
}

static bool decodeTel(const Parameter* param, MapCamelType* type, AsnTag& tag, DataBlock& data, XmlElement* parent, bool addEnc, int& err)
{
    if (!(param && type && data.length() && parent))
	return false;
    XDebug(&__plugin,DebugAll,"decodeTel(param=%s[%p],elem=%s[%p],datalen=%d)",param->name.c_str(),param,parent->getTag().c_str(),
	parent,data.length());
    if (param->tag != tag)
	return false;
    data.cut(-(int)tag.coding().length());

    XmlElement* child = new XmlElement(param->name);
    parent->addChild(child);
    int len = ASNLib::decodeLength(data);
    if (len < 1)
	return false;
    u_int8_t attr = data[0];
    child->setAttribute(s_natureAttr,lookup((attr & 0x70),s_dict_numNature,"unknown"));
    child->setAttribute(s_planAttr,lookup((attr & 0x0f),s_dict_numPlan,"unknown"));
    if ((attr & 0x0f) == 1)
	child->setAttribute(s_encAttr,"e164");
    else if ((attr & 0x0f) == 6)
	child->setAttribute(s_encAttr,"e212");
    String digits;
    decodeBCD(len - 1,digits,data.data(1,len - 1 ));

    data.cut(-len);
    child->addText(digits);
    return true;
}

static bool encodeTel(const Parameter* param, MapCamelType* type, DataBlock& data, XmlElement* elem, int& err)
{
    if (!(param && elem))
	return false;
    XDebug(&__plugin,DebugAll,"encodeTel(param=%s[%p],elem=%s[%p],datalen=%d)",param->name.c_str(),param,elem->getTag().c_str(),
	elem,data.length());

    u_int8_t first = 0x80; // noExtension bit set
    first |= lookup(elem->attribute(s_natureAttr),s_dict_numNature,0);
    first |= lookup(elem->attribute(s_planAttr),s_dict_numPlan,0);
    data.append(&first,sizeof(first));

    const String& digits = elem->getText();
    encodeBCD(digits,data);

    data.insert(ASNLib::buildLength(data));
    data.insert(param->tag.coding());
    return true;
}

static bool decodeHex(const Parameter* param, MapCamelType* type, AsnTag& tag, DataBlock& data, XmlElement* parent, bool addEnc, int& err)
{
    if (!(param && type && data.length() && parent))
	return false;
    XDebug(&__plugin,DebugAll,"decodeHex(param=%s[%p],elem=%s[%p],datalen=%d)",param->name.c_str(),param,parent->getTag().c_str(),
	parent,data.length());
    if (param->tag != tag)
	return false;
    data.cut(-(int)tag.coding().length());

    XmlElement* child = new XmlElement(param->name);
    parent->addChild(child);
    if (addEnc)
	child->setAttribute(s_encAttr,"hex");

    int len = ASNLib::decodeLength(data);
    bool checkEoC = (len == ASNLib::IndefiniteForm && tag.type() == AsnTag::Constructor);
    if (!checkEoC && len < 0)
	return false;
    String octets;
    if (checkEoC) {
	DataBlock d(data.data(),data.length());
	int l = ASNLib::parseUntilEoC(d);
	octets.hexify(data.data(),l,' ');
	data.cut(-l);
	ASNLib::matchEOC(data);
    }
    else {
	octets.hexify(data.data(),(len > (int)data.length() ? data.length() : len),' ');
	data.cut(-len);
    }
    child->addText(octets);
    return true;
}

static bool encodeHex(const Parameter* param, MapCamelType* type, DataBlock& data, XmlElement* elem, int& err)
{
    if (!(param && elem))
	return false;
    XDebug(&__plugin,DebugAll,"encodeHexparam=%s[%p],elem=%s[%p])",param->name.c_str(),param,elem->getTag().c_str(),elem);
    const String& text = elem->getText();
    if (!data.unHexify(text.c_str(),text.length(),' ')) {
	Debug(&__plugin,DebugWarn,"Failed to parse hexified string '%s'",text.c_str());
	return false;
    }
    data.insert(ASNLib::buildLength(data));
    data.insert(param->tag.coding());;
    return true;
}

static bool decodeOID(const Parameter* param, MapCamelType* type, AsnTag& tag, DataBlock& data, XmlElement* parent, bool addEnc, int& err)
{
    if (!(param && type && data.length() && parent))
	return false;
    XDebug(&__plugin,DebugAll,"decodeOID(param=%s[%p],elem=%s[%p],datalen=%d)",param->name.c_str(),param,parent->getTag().c_str(),
	parent,data.length());
    if (param->tag != tag)
	return false;
    data.cut(-(int)tag.coding().length());

    XmlElement* child = new XmlElement(param->name);
    parent->addChild(child);
    if (addEnc)
	child->setAttribute(s_encAttr,"oid");

    ASNObjId obj;
    ASNLib::decodeOID(data,&obj,false);
    child->addText(obj.toString());

    return true;
}

static bool encodeOID(const Parameter* param, MapCamelType* type, DataBlock& data, XmlElement* elem, int& err)
{
    if (!(param && elem))
	return false;
    XDebug(&__plugin,DebugAll,"encodeOID(param=%s[%p],elem=%s[%p])",param->name.c_str(),param,elem->getTag().c_str(),elem);
    ASNObjId oid = elem->getText();
    data.append(ASNLib::encodeOID(oid,false));
    data.insert(ASNLib::buildLength(data));
    data.insert(param->tag.coding());
    return true;
}

static bool decodeNull(const Parameter* param, MapCamelType* type, AsnTag& tag, DataBlock& data, XmlElement* parent, bool addEnc, int& err)
{
    if (!(param && type && data.length() && parent))
	return false;
    XDebug(&__plugin,DebugAll,"decodeNull(param=%s[%p],elem=%s[%p],datalen=%d)",param->name.c_str(),param,parent->getTag().c_str(),
	parent,data.length());
    if (param->tag != tag)
	return false;
    data.cut(-(int)tag.coding().length());

    XmlElement* child = new XmlElement(param->name);
    parent->addChild(child);
    if (addEnc)
	child->setAttribute(s_encAttr,"null");

    int len = ASNLib::decodeNull(data,false);
    data.cut(-len);
    return true;
}

static bool encodeNull(const Parameter* param, MapCamelType* type, DataBlock& data, XmlElement* elem, int& err)
{
    if (!(param && elem))
	return false;
    XDebug(&__plugin,DebugAll,"encodeNull(param=%s[%p],elem=%s[%p])",param->name.c_str(),param,elem->getTag().c_str(),elem);
    ASNObjId oid = elem->getText();
    data.append(ASNLib::encodeNull(false));
    data.insert(ASNLib::buildLength(data));
    data.insert(param->tag.coding());
    return true;
}

static bool decodeInt(const Parameter* param, MapCamelType* type, AsnTag& tag, DataBlock& data, XmlElement* parent, bool addEnc, int& err)
{
    if (!(param && type && data.length() && parent))
	return false;
    XDebug(&__plugin,DebugAll,"decodeInt(param=%s[%p],elem=%s[%p],datalen=%d)",param->name.c_str(),param,parent->getTag().c_str(),
	parent,data.length());
    if (param->tag != tag)
	return false;
    data.cut(-(int)tag.coding().length());

    XmlElement* child = new XmlElement(param->name);
    parent->addChild(child);
    if (addEnc)
	child->setAttribute(s_encAttr,"int");

    u_int64_t val;
    ASNLib::decodeInteger(data,val,sizeof(val),false);
    child->addText(String((int)val));
    return true;
}

static bool encodeInt(const Parameter* param, MapCamelType* type, DataBlock& data, XmlElement* elem, int& err)
{
    if (!(param && elem))
	return false;
    XDebug(&__plugin,DebugAll,"encodeInt(param=%s[%p],elem=%s[%p])",param->name.c_str(),param,elem->getTag().c_str(),elem);
    u_int64_t val = elem->getText().toInteger();
    data.append(ASNLib::encodeInteger(val,false));
    data.insert(ASNLib::buildLength(data));
    data.insert(param->tag.coding());
    return true;
}

static bool decodeSeq(const Parameter* param, MapCamelType* type, AsnTag& tag, DataBlock& data, XmlElement* parent, bool addEnc, int& err)
{
    if (!(param && type && data.length() && parent))
	return false;
    XDebug(&__plugin,DebugAll,"decodeSeq(param=%s[%p],elem=%s[%p],datalen=%d,first=0x%x)",param->name.c_str(),param,
		parent->getTag().c_str(),parent,data.length(),data[0]);
    if (param->tag != tag)
	return false;
    data.cut(-(int)tag.coding().length());

    int len = ASNLib::decodeLength(data);
    bool checkEoC = (len == ASNLib::IndefiniteForm);
    len = (checkEoC ? data.length() : len);
    if (!checkEoC && len < 0)
	return false;
    int initLen = data.length();

    XmlElement* child = new XmlElement(param->name);
    parent->addChild(child);

    if (param->content) {
	const Parameter* params= static_cast<const Parameter*>(param->content);
	while (params && params->name) {
	    if ((initLen - (int)data.length() >= len) || (checkEoC && ASNLib::matchEOC(data) > 0))
		break;
	    AsnTag childTag;
	    AsnTag::decode(childTag,data);
	    if (!decodeParam(params,childTag,data,child,addEnc,err)) {
		if (!params->isOptional) {
		    if (err != TcapXApplication::DataMissing)
			printMissing(params->name.c_str(),param->name.c_str(),false);
		    err = TcapXApplication::DataMissing;
		    return false;
		}
		else {
		    params++;
		    continue;
		}
	    }
	    params++;
	}
    }
    return true;
}

static bool encodeSeq(const Parameter* param, MapCamelType* type, DataBlock& data, XmlElement* elem, int& err)
{
    if (!(param && elem))
	return false;
    XDebug(&__plugin,DebugAll,"encodeSeq(param=%s[%p],elem=%s[%p])",param->name.c_str(),param,elem->getTag().c_str(),elem);

    if (param->content) {
	const Parameter* params= static_cast<const Parameter*>(param->content);
	while (params && params->name) {
	    const String& name = params->name;
	    XmlElement* child = elem->findFirstChild(&name);
	    if (!child) {
		if (!params->isOptional) {
		    printMissing(params->name.c_str(),param->name.c_str());
		    err = TcapXApplication::DataMissing;
		    return false;
		}
		else {
		    params++;
		    continue;
		}
	    }
	    DataBlock db;
	    if (!encodeParam(params,db,child,err))
		return false;
	    data.append(db);
	    elem->removeChild(child);
	    params++;
	}
    }

    if (param->tag != s_noTag) {
	data.insert(ASNLib::buildLength(data));
	data.insert(param->tag.coding());
    }
    return true;
}

static bool decodeSeqOf(const Parameter* param, MapCamelType* type, AsnTag& tag, DataBlock& data, XmlElement* parent, bool addEnc,
	    int& err)
{
    if (!(param && type && data.length() && parent))
	return false;
    XDebug(&__plugin,DebugAll,"decodeSeqOf(param=%s[%p],elem=%s[%p],datalen=%d)",param->name.c_str(),param,parent->getTag().c_str(),
	    parent,data.length());
    if (param->tag != tag)
	return false;
    data.cut(-(int)tag.coding().length());

    int len = ASNLib::decodeLength(data);
    bool checkEoC = (len == ASNLib::IndefiniteForm);
    if (!checkEoC && len < 0)
	return false;
    XmlElement* child = new XmlElement(param->name);
    parent->addChild(child);

    int initLength = data.length();
    int payloadLen = (checkEoC ? data.length() : len);
    if (param->content) {
	const Parameter* params= static_cast<const Parameter*>(param->content);
	while (params && !TelEngine::null(params->name) && payloadLen) {
	    if (checkEoC && ASNLib::matchEOC(data) > 0)
		break;
	    AsnTag childTag;
	    AsnTag::decode(childTag,data);
	    if (!decodeParam(params,childTag,data,child,addEnc,err)) {
		if (!param->isOptional) {
		    if (err != TcapXApplication::DataMissing)
			printMissing(params->name.c_str(),param->name.c_str(),false);
		    err = TcapXApplication::DataMissing;
		    return false;
		}
		else
		    break;
	    }
	    payloadLen = data.length() - (initLength - len);
	}
    }
    return true;
}

static bool encodeSeqOf(const Parameter* param, MapCamelType* type, DataBlock& data, XmlElement* elem, int& err)
{
    if (!(param && elem))
	return false;
    XDebug(&__plugin,DebugAll,"encodeSeqOf(param=%s[%p],elem=%s[%p])",param->name.c_str(),param,elem->getTag().c_str(),elem);

    if (param->content) {
	const Parameter* params = static_cast<const Parameter*>(param->content);
	XmlElement* child = elem->pop();
	bool atLeastOne = false;
	while (params && params->name && child) {
	    if (!(child->getTag() == params->name)) {
		Debug(&__plugin,DebugAll,"Skipping over unknown parameter '%s' for parent '%s', expecting '%s'",
		    child->tag(),elem->tag(),params->name.c_str());
		TelEngine::destruct(child);
		child = elem->pop();
		continue;
	    }
	    DataBlock db;
	    if (!encodeParam(params,db,child,err)) {
		TelEngine::destruct(child);
		if (err != TcapXApplication::DataMissing) {
		    printMissing(params->name.c_str(),param->name.c_str());
		    err = TcapXApplication::DataMissing;
		}
		if (!param->isOptional && !(elem->findFirstChild()) && !atLeastOne)
		    return false;
		else {
		    child = elem->pop();
		    continue;
		}
	    }
	    else
		atLeastOne = true;
	    data.append(db);
	    TelEngine::destruct(child);
	    child = elem->pop();
	}
    }
    if (param->tag != s_noTag) {
	data.insert(ASNLib::buildLength(data));
	data.insert(param->tag.coding());
    }
    return true;
}


static bool decodeChoice(const Parameter* param, MapCamelType* type, AsnTag& tag, DataBlock& data, XmlElement* parent, bool addEnc, int& err)
{
    if (!(param && type && data.length() && parent))
	return false;
    XDebug(&__plugin,DebugAll,"decodeChoice(param=%s[%p],elem=%s[%p],datalen=%d)",param->name.c_str(),param,parent->getTag().c_str(),
	parent,data.length());
    bool checkEoC = false;
    if (param->tag != s_noTag) {
	if (param->tag != tag)
	    return false;
	data.cut(-(int)tag.coding().length());
	int len = ASNLib::decodeLength(data);
	checkEoC = (len == ASNLib::IndefiniteForm);
	if (!checkEoC && len < 0)
	    return false;
    }
    XmlElement* child = new XmlElement(param->name);

    bool showDebug = !(param->tag == s_noTag && param->isOptional);
    if (param->content) {
	const Parameter* params= static_cast<const Parameter*>(param->content);
	while (params && !TelEngine::null(params->name)) {
	    AsnTag childTag;
	    AsnTag::decode(childTag,data);
	    if (!decodeParam(params,childTag,data,child,addEnc,err)) {
		params++;
		continue;
	    }
	    if (checkEoC)
		ASNLib::matchEOC(data);
	    parent->addChild(child);
	    return true;
	}
	if (err != TcapXApplication::DataMissing && showDebug) {
	    if (__plugin.showMissing())
		Debug(&__plugin,DebugNote,"No valid choice in payload for '%s'",child->tag());
	    err = TcapXApplication::DataMissing;
	}
    }
    TelEngine::destruct(child);
    return false;
}

static bool encodeChoice(const Parameter* param, MapCamelType* type, DataBlock& data, XmlElement* elem, int& err)
{
    if (!(param && elem))
	return false;
    XDebug(&__plugin,DebugAll,"encodeChoice(param=%s[%p],elem=%s[%p])",param->name.c_str(),param,elem->getTag().c_str(),elem);

    if (param->content) {
	const Parameter* params = static_cast<const Parameter*>(param->content);
	XmlElement* child = elem->pop();
	while (child && params && !TelEngine::null(params->name)) {
	    if (child->getTag() == params->name) {
		DataBlock db;
		if (!encodeParam(params,db,child,err)) {
		    TelEngine::destruct(child);
		    return false;
		}
		data.append(db);
		if (param->tag != s_noTag) {
		    data.insert(ASNLib::buildLength(data));
		    data.insert(param->tag.coding());
		}
		TelEngine::destruct(child);
		return true;
	    }
	    params++;
	}
	TelEngine::destruct(child);
    }
    if (err != TcapXApplication::DataMissing) {
	if (__plugin.showMissing())
	    Debug(&__plugin,DebugNote,"No valid choice was given for parent '%s'",elem->tag());
	err = TcapXApplication::DataMissing;
    }
    return false;
}

static bool decodeEnumerated(const Parameter* param, MapCamelType* type, AsnTag& tag, DataBlock& data, XmlElement* parent, bool addEnc, int& err)
{
    if (!(param && type && data.length() && parent))
	return false;
    XDebug(&__plugin,DebugAll,"decodeEnumerated(param=%s[%p],elem=%s[%p],datalen=%d)",param->name.c_str(),param,parent->getTag().c_str(),
	parent,data.length());
    if (param->tag != tag)
	return false;
    data.cut(-(int)tag.coding().length());

    int len = ASNLib::decodeLength(data);
    if (len < 0)
	return false;

    XmlElement* child = new XmlElement(param->name);
    parent->addChild(child);

    u_int8_t val = data[0];
    data.cut(-1);
    if (param->content) {
	const TokenDict* dict = static_cast<const TokenDict*>(param->content);
	if (!dict)
	    return false;
	child->addText(lookup(val,dict,""));
	if (addEnc)
	    child->setAttribute(s_encAttr,"str");
    }
    else {
	child->addText(String(val));
	if (addEnc)
	    child->setAttribute(s_encAttr,"int");
    }
    return true;
}

static bool encodeEnumerated(const Parameter* param, MapCamelType* type, DataBlock& data, XmlElement* elem, int& err)
{
    if (!(param && elem))
	return false;
    XDebug(&__plugin,DebugAll,"encodeEnumerated(param=%s[%p],elem=%s[%p])",param->name.c_str(),param,elem->getTag().c_str(),elem);

    if (param->content) {
	const TokenDict* dict = static_cast<const TokenDict*>(param->content);
	if (!dict)
	    return false;
	const String& text = elem->getText();
	int val = lookup(text,dict,-1);
	if (val < 0 || val > 255) {
	    err = TcapXApplication::UnexpectedDataValue;
	    return false;
	}
	u_int8_t enumVal = val & 0xff;
	data.append(&enumVal,sizeof(enumVal));
    }
    if (param->tag != s_noTag) {
	data.insert(ASNLib::buildLength(data));
	data.insert(param->tag.coding());
    }
    return true;
}

static bool decodeBitString(const Parameter* param, MapCamelType* type, AsnTag& tag, DataBlock& data, XmlElement* parent, bool addEnc, int& err)
{
    if (!(param && type && data.length() && parent))
	return false;
    XDebug(&__plugin,DebugAll,"decodeBitString(param=%s[%p],elem=%s[%p],datalen=%d)",param->name.c_str(),param,parent->getTag().c_str(),
	parent,data.length());
    if (param->tag != tag)
	return false;
    data.cut(-(int)tag.coding().length());

    XmlElement* child = new XmlElement(param->name);
    parent->addChild(child);
    if (addEnc)
	child->setAttribute(s_encAttr,"str");

    String val, str;
    int value = 0;
    ASNLib::decodeBitString(data,&val,false);
    for (unsigned int i = 0; i < val.length(); i++) {
	char c = val[i];
	if (c == '1') {
	    int mask = 1 << i;
	    value |= mask;
	}
    }
    if (param->content) {
	const TokenDict* dict = static_cast<const TokenDict*>(param->content);
	if (!dict)
	    return true;
	while (dict->token){
	    if ((dict->value & value) == dict->value)
		str.append(dict->token,",");
	    dict++;
	}
    }
    child->addText(str);
    return true;
}

static bool encodeBitString(const Parameter* param, MapCamelType* type, DataBlock& data, XmlElement* elem, int& err)
{
    if (!(param && elem))
	return false;
    XDebug(&__plugin,DebugAll,"encodeBitString(param=%s[%p],elem=%s[%p])",param->name.c_str(),param,elem->getTag().c_str(),elem);

    if (param->content) {
	const TokenDict* dict = static_cast<const TokenDict*>(param->content);
	String val;
	int value = 0;
	if (dict) {
	    const String& text = elem->getText();
	    ObjList* list = text.split(',',false);
	    while(dict->token){
		if (list->find(dict->token))
		    value |= dict->value;
		dict++;
	    }
	    TelEngine::destruct(list);
	}

	int size = sizeof(value) * 8;
	bool start = false;
	while (size > 0) {
	    size--;
	    u_int8_t b = (value >> size) & 0x01;
	    if (b == 1 && !start)
		start = true;
	    if (start)
		val = (b == 1? "1" : "0") + val;
	}

	data.append(ASNLib::encodeBitString(val,false));
    }
    if (param->tag != s_noTag) {
	data.insert(ASNLib::buildLength(data));
	data.insert(param->tag.coding());
    }
    return true;
}

// These tables contain embedded UTF-8 characters
static const char* const s_gsm7base[128] = {
    "@", "£", "$", "¥", "è", "é", "ù", "ì", "ò", "Ç", "\n", "Ø", "ø", "\r", "Å", "å",
    "Δ", "_", "Φ", "Γ", "Λ", "Ω", "Π", "Ψ", "Σ", "Θ", "Ξ", "", "Æ", "æ", "ß", "É",
    " ", "!", "\"", "#", "¤", "%", "&", "'", "(", ")", "*", "+", ",", "-", ".", "/",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", ":", ";", "<", "=", ">", "?",
    "¡", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
    "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "Ä", "Ö", "Ñ", "Ü", "§",
    "¿", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o",
    "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z", "ä", "ö", "ñ", "ü", "à"
};

static const char* const s_gsm7esc[128] = {
    "", "", "", "", "", "", "", "", "", "", "\f", "", "", "", "", "",
    "", "", "", "", "^", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "{", "}", "", "", "", "", "", "\\",
    "", "", "", "", "", "", "", "", "", "", "", "", "[", "~", "]", "",
    "|", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "€", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};

static void decodeGSM7Bit(DataBlock& data, int& len, String& decoded)
{
    u_int8_t bits = 0;
    u_int16_t buf = 0;
    bool esc = false;
    for (int i = 0; i < len; i++) {
	buf |= ((u_int16_t)data[i]) << bits;
	bits += 8;
	while (bits >= 7) {
	    if (esc) {
		decoded << s_gsm7esc[buf & 0x7f];
		esc = false;
	    }
	    else if ((buf & 0x7f) == 0x1b)
		esc = true;
	    else
		decoded << s_gsm7base[buf & 0x7f];
	    buf >>= 7;
	    bits -= 7;
	}
    }
    data.cut(-len);
    if ((bits == 0) && decoded.endsWith("\r"))
	decoded.assign(decoded,decoded.length()-1);
}

static bool decodeGSMString(const Parameter* param, MapCamelType* type, AsnTag& tag, DataBlock& data, XmlElement* parent, bool addEnc, int& err)
{
    if (!(param && type && data.length() && parent))
	return false;
    XDebug(&__plugin,DebugAll,"decodeGSMString(param=%s[%p],elem=%s[%p],datalen=%d)",param->name.c_str(),param,parent->getTag().c_str(),
	parent,data.length());

    if (param->tag != tag)
	return false;
    data.cut(-(int)tag.coding().length());

    int len = ASNLib::decodeLength(data);
    if (len < 0)
	return false;

    // TODO - should check Encoding Scheme at some point as also translation tables
    XmlElement* enc = new XmlElement(param->name);
    parent->addChild(enc);
    if (addEnc)
	enc->setAttribute(s_encAttr,"str");
    String str;
    decodeGSM7Bit(data,len,str);
    enc->addText(str);
    return true;
}

static void encodeGSM7Bit(const String& str, DataBlock& db)
{
    if (str.null())
	return;
    u_int8_t bits = 0;
    u_int32_t buf = 0;
    String tmp = str;
    while (tmp) {
	bool notFound = true;
	for (int i = 0; i < 128; i++) {
	    if (tmp.startSkip(s_gsm7base[i],false)) {
		buf |= (i << bits);
		bits += 7;
		notFound = false;
		break;
	    }
	}
	if (notFound) {
	    for (int i = 0; i < 128; i++) {
		if (tmp.startSkip(s_gsm7esc[i],false)) {
		    buf |= ((i << 7) | 0x1b) << bits;
		    bits += 14;
		    notFound = false;
		    break;
		}
	    }
	    if (notFound) {
		// TODO: skip one UTF-8 instead of one C char
		tmp = tmp.c_str() + 1;
		continue;
	    }
	}
	while (bits >= 8) {
	    u_int8_t byte = buf & 0xff;
	    db.append(&byte,sizeof(byte));
	    buf >>= 8;
	    bits -= 8;
	}
    }
    if (bits) {
	u_int8_t byte = buf & 0xff;
	// if just 1 bit use a shifted \r as filler
	if (bits == 1)
	    byte |= 0x1a;
	db.append(&byte,sizeof(byte));
    }
}

static bool encodeGSMString(const Parameter* param, MapCamelType* type, DataBlock& data, XmlElement* elem, int& err)
{
    if (!(param && elem))
	return false;

    XDebug(&__plugin,DebugAll,"encodeGSMString(param=%s[%p],elem=%s[%p])",param->name.c_str(),param,elem->getTag().c_str(),elem);

    if (elem->getTag() != param->name)
	return false;

    const String& str = elem->getText();
    encodeGSM7Bit(str,data);

    if (param->tag != s_noTag) {
	data.insert(ASNLib::buildLength(data));
	data.insert(param->tag.coding());
    }
    return true;
}

static bool decodeFlags(const Parameter* param, MapCamelType* type, AsnTag& tag, DataBlock& data, XmlElement* parent, bool addEnc, int& err)
{
    if (!(param && type && data.length() && parent))
	return false;
    XDebug(&__plugin,DebugAll,"decodeFlags(param=%s[%p],elem=%s[%p],datalen=%d, data[0]=%d)",param->name.c_str(),param,
	   parent->getTag().c_str(),parent,data.length(),data[0]);
    if (param->tag != tag)
	return false;
    data.cut(-(int)tag.coding().length());

    int len = ASNLib::decodeLength(data);
    if (len <= 0)
	return false;

    XmlElement* child = new XmlElement(param->name);
    parent->addChild(child);

    if (addEnc)
	child->setAttribute(s_encAttr,"str");

    u_int8_t flags = data[0];
    String str;
    if (param->content) {
	const SignallingFlags* list = static_cast<const SignallingFlags*>(param->content);
	if (!list)
	    return false;
	for (; list->mask; list++) {
	    if ((flags & list->mask) == list->value)
		str.append(list->name,",");
	}
    }
    data.cut(-len);
    child->addText(str);
    return true;
}

static bool encodeFlags(const Parameter* param, MapCamelType* type, DataBlock& data, XmlElement* elem, int& err)
{
    if (!(param && elem))
	return false;

    XDebug(&__plugin,DebugAll,"encodeFlags(param=%s[%p],elem=%s[%p])",param->name.c_str(),param,elem->getTag().c_str(),elem);

    u_int8_t byte = 0;
    if (param->content) {
 	const SignallingFlags* flags = static_cast<const SignallingFlags*>(param->content);
	if (!flags)
	    return false;
	const String& text = elem->getText();
	ObjList* list = text.split(',',false);

	for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	    String* s = static_cast<String*>(o->get());
	    const SignallingFlags* flag = flags;
	    for (; flag->mask && *s != flag->name; flag++);
	    if (!flag->name) {
		DDebug(&__plugin,DebugAll,"encodeFlags '%s' not found",s->c_str());
		continue;
	    }
	    byte |= flag->value;
	}
	TelEngine::destruct(list);
    }
    data.append(&byte,sizeof(byte));
    if (param->tag != s_noTag) {
	data.insert(ASNLib::buildLength(data));
	data.insert(param->tag.coding());
    }
    return true;
}

static bool decodeString(const Parameter* param, MapCamelType* type, AsnTag& tag, DataBlock& data, XmlElement* parent, bool addEnc, int& err)
{
    if (!(param && type && data.length() && parent))
	return false;
    XDebug(&__plugin,DebugAll,"decodeString(param=%s[%p],elem=%s[%p],datalen=%d, data[0]=%d)",param->name.c_str(),param,parent->getTag().c_str(),
	parent,data.length(),data[0]);
    if (param->tag != tag)
	return false;
    data.cut(-(int)tag.coding().length());

    String value;
    int t = 0;
    int len = ASNLib::decodeString(data,&value,&t,false);
    if (len <= 0)
	return false;

    XmlElement* child = new XmlElement(param->name);
    parent->addChild(child);
    child->addText(value);
    if (addEnc)
	child->setAttribute(s_encAttr,"str");
    return true;
}

static bool encodeString(const Parameter* param, MapCamelType* type, DataBlock& data, XmlElement* elem, int& err)
{
    if (!(param && elem))
	return false;

    XDebug(&__plugin,DebugAll,"encodeString(param=%s[%p],elem=%s[%p])",param->name.c_str(),param,elem->getTag().c_str(),elem);

    const String& text = elem->getText();
    data.append(ASNLib::encodeString(text,ASNLib::PRINTABLE_STR,false));
    if (param->tag != s_noTag) {
	data.insert(ASNLib::buildLength(data));
	data.insert(param->tag.coding());
    }
    return true;
}

static bool decodeBool(const Parameter* param, MapCamelType* type, AsnTag& tag, DataBlock& data, XmlElement* parent, bool addEnc, int& err)
{
    if (!(param && type && data.length() && parent))
	return false;
    XDebug(&__plugin,DebugAll,"decodeBool(param=%s[%p],elem=%s[%p],datalen=%d, data[0]=%d)",param->name.c_str(),param,parent->getTag().c_str(),
	parent,data.length(),data[0]);
    if (param->tag != tag)
	return false;
    data.cut(-(int)tag.coding().length());

    bool value = false;
    int len = ASNLib::decodeBoolean(data,&value,false);
    if (len <= 0)
	return false;

    XmlElement* child = new XmlElement(param->name);
    parent->addChild(child);
    child->addText(String::boolText(value));
    if (addEnc)
	child->setAttribute(s_encAttr,"bool");
    return true;
}

static bool encodeBool(const Parameter* param, MapCamelType* type, DataBlock& data, XmlElement* elem, int& err)
{
    if (!(param && elem))
	return false;
    XDebug(&__plugin,DebugAll,"encodeBool(param=%s[%p],elem=%s[%p])",param->name.c_str(),param,elem->getTag().c_str(),elem);

    bool val = elem->getText().toBoolean();
    data.append(ASNLib::encodeBoolean(val,false));
    if (param->tag != s_noTag) {
	data.insert(ASNLib::buildLength(data));
	data.insert(param->tag.coding());
    }
    return true;
}

// Nature of Address Indicator
static const TokenDict s_dict_nai[] = {
    { "subscriber",        1 },
    { "unknown",           2 },
    { "national",          3 },
    { "international",     4 },
    { "network-specific",  5 },
    { "national-routing",  6 },
    { "specific-routing",  7 },
    { "routing-with-cdn",  8 },
    { 0, 0 }
};

// Numbering Plan Indicator
static const TokenDict s_dict_numPlanIsup[] = {
    { "unknown",  0 },
    { "isdn",     1 },
    { "data",     3 },
    { "telex",    4 },
    { "private",  5 },
    { "national", 6 },
    { 0, 0 }
};

// Address Presentation
static const TokenDict s_dict_presentation[] = {
    { "allowed",     0 },
    { "restricted",  1 },
    { "unavailable", 2 },
    // aliases for restrict=...
    { "no",    0 },
    { "false", 0 },
    { "yes",   1 },
    { "true",  1 },
    { 0, 0 }
};

// Screening Indicator
static const TokenDict s_dict_screening[] = {
    { "user-provided",        0 },
    { "user-provided-passed", 1 },
    { "user-provided-failed", 2 },
    { "network-provided",     3 },
    // aliases for screened=...
    { "no",    0 },
    { "false", 0 },
    { "yes",   1 },
    { "true",  1 },
    { 0, 0 }
};

// Generic number qualifier
static const TokenDict s_dict_qual[] = {
    { "dialed-digits",        0 },
    { "called-additional",    1 },
    { "caller-failed",        2 },
    { "caller-not-screened",  3 },
    { "terminating",          4 },
    { "connected-additional", 5 },
    { "caller-additional",    6 },
    { "called-original",      7 },
    { "redirecting",          8 },
    { "redirection",          9 },
    { 0, 0 }
};

// Utility function - extract just ISUP digits from a parameter
static void getDigits(String& num, bool odd, const unsigned char* buf, unsigned int len)
{
    static const char digits[] = "0123456789ABCDE.";
    for (unsigned int i = 0; i < len; i++) {
	num += digits[buf[i] & 0x0f];
	if (odd && ((i+1) == len))
	    break;
	num += digits[buf[i] >> 4];
    }
}

// Utility function - write digit sequences
static void setDigits(DataBlock& data, const char* val, unsigned char nai, int b2 = -1, int b0 = -1)
{
    unsigned char buf[32];
    unsigned int len = 0;
    if (b0 >= 0)
	buf[len++] = b0 & 0xff;
    unsigned int naiPos = len++;
    buf[naiPos] = nai & 0x7f;
    if (b2 >= 0)
	buf[len++] = b2 & 0xff;

    bool odd = false;
    while (val && (len < sizeof(buf))) {
	char c = *val++;
	if (!c)
	    break;
	unsigned char n = 0;
	if (('0' <= c) && (c <= '9'))
	    n = c - '0';
	else if ('.' == c)
	    n = 15;
	else if ('A' == c)
	    n = 10;
	else if ('B' == c)
	    n = 11;
	else if ('C' == c)
	    n = 12;
	else if ('D' == c)
	    n = 13;
	else
	    continue;
	odd = !odd;
	if (odd)
	    buf[len] = n;
	else
	    buf[len++] |= (n << 4);
    }
    if (odd) {
	buf[naiPos] |= 0x80;
	len++;
    }
    XDebug(&__plugin,DebugAll,"setDigits encoding %u octets (%s)",len,odd ? "odd" : "even");
    data.append(buf,len);
}

static bool decodeCallNumber(const Parameter* param, MapCamelType* type, AsnTag& tag, DataBlock& data,
	XmlElement* parent, bool addEnc, int& err)
{
    if (!(param && type && data.length() && parent))
	return false;
    XDebug(&__plugin,DebugAll,"decodeCallNumber(param=%s[%p],elem=%s[%p],datalen=%d)",param->name.c_str(),param,
	    parent->getTag().c_str(),parent,data.length());
    if (param->tag != tag)
	return false;
    data.cut(-(int)tag.coding().length());

    XmlElement* child = new XmlElement(param->name);
    parent->addChild(child);
    int len = ASNLib::decodeLength(data);
    if (len < 2)
	return false;
    unsigned int index = 0;
    unsigned char qualifier = 0;
    if (type->type == TcapXApplication::GenericNumber) {
	if (len < 3)
	    return false;
	qualifier = data[index];
	index++;
    }
    bool odd = (data[index] & 0x80) != 0;
    unsigned char nai = data[index] & 0x7f;
    index++;
    unsigned char plan = (data[index] >> 4) & 7;
    unsigned char pres = (data[index] >> 2) & 3;
    unsigned char scrn = data[index] & 3;

    if (type->type == TcapXApplication::GenericNumber)
	child->setAttribute(s_qualifierAttr,lookup(qualifier,s_dict_qual,"unknown"));
    child->setAttribute(s_natureAttr,lookup(nai,s_dict_nai,"unknown"));
    child->setAttribute(s_planAttr,lookup(plan,s_dict_numPlanIsup,"unknown"));
    if (plan == 1)
	child->setAttribute(s_encAttr,"e164");

    String tmp;
    switch (type->type) {
	case TcapXApplication::CalledPartyNumber:
	case TcapXApplication::LocationNumber:
	    tmp = ((data[index] & 0x80) == 0);
	    child->setAttribute(s_innAttr,tmp);
	    break;
	case TcapXApplication::CallingPartyNumber:
	case TcapXApplication::GenericNumber:
	    tmp = ((data[index] & 0x80) == 0);
	    child->setAttribute(s_completeAttr,tmp);
	    break;
	default:
	    break;
    }
    switch (type->type) {
	case TcapXApplication::CallingPartyNumber:
	case TcapXApplication::RedirectingNumber:
	case TcapXApplication::OriginalCalledNumber:
	case TcapXApplication::LocationNumber:
	case TcapXApplication::GenericNumber:
	    child->setAttribute(s_restrictAttr,lookup(pres,s_dict_presentation));
	default:
	    break;
    }
    switch (type->type) {
	case TcapXApplication::CallingPartyNumber:
	case TcapXApplication::LocationNumber:
	case TcapXApplication::GenericNumber:
	    child->setAttribute(s_screenedtAttr,lookup(scrn,s_dict_screening));
	default:
	    break;
    }

    index++;
    String digits;
    getDigits(digits,odd,data.data(index,len - index),len - index);

    data.cut(-len);
    child->addText(digits);
    return true;
}

static bool encodeCallNumber(const Parameter* param, MapCamelType* type, DataBlock& data, XmlElement* elem, int& err)
{
    if (!(param && elem))
	return false;
    XDebug(&__plugin,DebugAll,"encodeCallNumber(param=%s[%p],elem=%s[%p],datalen=%d)",param->name.c_str(),param,
	elem->getTag().c_str(),elem,data.length());

    unsigned char nai = lookup(elem->attribute(s_natureAttr),s_dict_nai,2) & 0x7f;
    unsigned char plan = lookup(elem->attribute(s_planAttr),s_dict_numPlanIsup,1);

    int b0 = -1;
    if (type->type == TcapXApplication::GenericNumber) {
	b0 = 0;
	b0 = 0xff & (lookup(elem->attribute(s_qualifierAttr),s_dict_presentation));
    }

    // Numbering plan
    unsigned char b2 = (plan & 7) << 4;

    switch (type->type) {
	case TcapXApplication::CalledPartyNumber:
	case TcapXApplication::LocationNumber:
	    if (!TelEngine::null(elem->getAttribute(s_innAttr)) && !elem->getAttribute(s_innAttr)->toBoolean(true))
		b2 |= 0x80;
	    break;
	case TcapXApplication::CallingPartyNumber:
	case TcapXApplication::GenericNumber:
	    if (!TelEngine::null(elem->getAttribute(s_completeAttr)) && !elem->getAttribute(s_completeAttr)->toBoolean(true))
		b2 |= 0x80;
	    break;
	default:
	    break;
    }
    switch (type->type) {
	case TcapXApplication::CallingPartyNumber:
	case TcapXApplication::RedirectingNumber:
	case TcapXApplication::OriginalCalledNumber:
	case TcapXApplication::LocationNumber:
	case TcapXApplication::GenericNumber:
	    b2 |= (lookup(elem->attribute(s_restrictAttr),s_dict_presentation) & 3) << 2;
	default:
	    break;
    }
    switch (param->type) {
	case TcapXApplication::CallingPartyNumber:
	case TcapXApplication::LocationNumber:
	case TcapXApplication::GenericNumber:
	    b2 |= (lookup(elem->attribute(s_screenedtAttr),s_dict_screening) & 3);
	default:
	    break;
    }
    const String& digits = elem->getText();
    setDigits(data,digits,nai,b2,b0);

    data.insert(ASNLib::buildLength(data));
    data.insert(param->tag.coding());
    return true;
}


// Redirection Information (Q,763 3.45) bits CBA
static const TokenDict s_dict_redir_main[] = {
    { "none",                     0 },
    { "rerouted",                 1 },
    { "rerouted-restrict-all",    2 },
    { "diverted",                 3 },
    { "diverted-restrict-all",    4 },
    { "rerouted-restrict-number", 5 },
    { "diverted-restrict-number", 6 },
    { 0, 0 }
};

// Redirection Information (Q,763 3.45) bits HGFE or PONM
static const TokenDict s_dict_redir_reason[] = {
    { "busy",      1 },
    { "noanswer",  2 },
    { "always",    3 },
    { "deflected", 4 },
    { "diverted",  5 },
    { "offline",   6 },
    { 0, 0 }
};

static const String s_reasonOrigAttr = "reason_original";
static const String s_counterAttr = "counter";
static const String s_reasonAttr = "reason";

static bool decodeRedir(const Parameter* param, MapCamelType* type, AsnTag& tag, DataBlock& data,
	XmlElement* parent, bool addEnc, int& err)
{
    if (!(param && type && data.length() && parent))
	return false;
    XDebug(&__plugin,DebugAll,"decodeRedir(param=%s[%p],elem=%s[%p],datalen=%d)",param->name.c_str(),param,
	    parent->getTag().c_str(),parent,data.length());
    if (param->tag != tag)
	return false;
    data.cut(-(int)tag.coding().length());

    XmlElement* child = new XmlElement(param->name);
    parent->addChild(child);
    int len = ASNLib::decodeLength(data);
    if (len < 1)
	return false;

    // Redirecting indicator
    unsigned char reason = data[0] & 0x07;
    child->addText(lookup(reason,s_dict_redir_main,""));

    // Original reason
    reason = data[0] >> 4;
    child->setAttribute(s_reasonOrigAttr,lookup(reason,s_dict_redir_reason));

    if (len > 1) {
	// Counter
	int count = data[1] & 0x07;
	if (count)
	    child->setAttribute(s_counterAttr,String(count));
	// Reason
	reason = data[1] >> 4;
	if (reason)
	    child->setAttribute(s_reasonAttr,lookup(reason,s_dict_redir_reason));
    }
    if (addEnc)
	child->setAttribute(s_encAttr,"str");
    data.cut(-len);
    return true;
}

static bool encodeRedir(const Parameter* param, MapCamelType* type, DataBlock& data, XmlElement* elem, int& err)
{
    if (!(param && elem))
	return false;
    XDebug(&__plugin,DebugAll,"encodeRedir(param=%s[%p],elem=%s[%p],datalen=%d)",param->name.c_str(),param,
	elem->getTag().c_str(),elem,data.length());

    unsigned char b0 = lookup(elem->getText(),s_dict_redir_main,0) & 0x07;
    b0 |= (lookup(elem->attribute(s_reasonOrigAttr),s_dict_redir_reason,0) & 0x0f) << 4;
    data.append(&b0,sizeof(b0));

    unsigned char b1 = String(elem->attribute(s_counterAttr)).toInteger() & 0x07;
    b1 |= (lookup(elem->attribute(s_reasonAttr),s_dict_redir_reason,0) & 0x0f) << 4;
    data.append(&b1,sizeof(b1));

    data.insert(ASNLib::buildLength(data));
    data.insert(param->tag.coding());
    return true;
}

// Coding standard as defined in Q.931/Q.850
static const TokenDict s_dict_codingStandard[] = {
    {"CCITT",            0x00},
    {"ISO/IEC",          0x20},
    {"national",         0x40},
    {"network specific", 0x60},
    {0,0}
};

// Q.931 4.5.5. Information transfer capability: Bits 0-4
// Defined for CCITT coding standard
static const TokenDict s_dict_transferCapCCITT[] = {
    {"speech",       0x00},          // Speech
    {"udi",          0x08},          // Unrestricted digital information
    {"rdi",          0x09},          // Restricted digital information
    {"3.1khz-audio", 0x10},          // 3.1 khz audio
    {"udi-ta",       0x11},          // Unrestricted digital information with tone/announcements
    {"video",        0x18},          // Video
    {0,0}
};

// Q.931 4.5.5. Transfer mode: Bits 5,6
// Defined for CCITT coding standard
static const TokenDict s_dict_transferModeCCITT[] = {
    {"circuit",      0x00},          // Circuit switch mode
    {"packet",       0x40},          // Packet mode
    {0,0}
};

// Q.931 4.5.5. Transfer rate: Bits 0-4
// Defined for CCITT coding standard
static const TokenDict s_dict_transferRateCCITT[] = {
    {"packet",        0x00},         // Packet mode only
    {"64kbit",        0x10},         // 64 kbit/s
    {"2x64kbit",      0x11},         // 2x64 kbit/s
    {"384kbit",       0x13},         // 384 kbit/s
    {"1536kbit",      0x15},         // 1536 kbit/s
    {"1920kbit",      0x17},         // 1920 kbit/s
    {"multirate",     0x18},         // Multirate (64 kbit/s base rate)
    {0,0}
};

// Q.931 4.5.5. User information Layer 1 protocol: Bits 0-4
// Defined for CCITT coding standard
static const TokenDict s_dict_formatCCITT[] = {
    {"v110",          0x01},         // Recomendation V.110 and X.30
    {"mulaw",         0x02},         // Recomendation G.711 mu-law
    {"alaw",          0x03},         // Recomendation G.711 A-law
    {"g721",          0x04},         // Recomendation G.721 32kbit/s ADPCM and I.460
    {"h221",          0x05},         // Recomendation H.221 and H.242
    {"non-CCITT",     0x07},         // Non CCITT standardized rate adaption
    {"v120",          0x08},         // Recomendation V.120
    {"x31",           0x09},         // Recomendation X.31 HDLC flag stuffing
    {0,0}
};

static const String s_codingAttr = "coding";
static const String s_transferCapAttr = "transfercap";
static const String s_transferModeAttr = "transfermode";
static const String s_transferRateAttr = "transferrate";
static const String s_multiplierAttr = "multiplier";

static bool decodeUSI(const Parameter* param, MapCamelType* type, AsnTag& tag, DataBlock& data,
	XmlElement* parent, bool addEnc, int& err)
{
    if (!(param && type && data.length() && parent))
	return false;
    XDebug(&__plugin,DebugAll,"decodeUSI(param=%s[%p],elem=%s[%p],datalen=%d)",param->name.c_str(),param,
	    parent->getTag().c_str(),parent,data.length());
    if (param->tag != tag)
	return false;
    data.cut(-(int)tag.coding().length());

    XmlElement* child = new XmlElement(param->name);
    parent->addChild(child);
    int len = ASNLib::decodeLength(data);
    if (len < 2)
	return false;

    // Byte 0: Coding standard (bit 5,6), Information transfer capability (bit 0-4)
    // Byte 1: Transfer mode (bit 5,6), Transfer rate (bit 0-4)
    unsigned char coding = data[0] & 0x60;
    child->setAttribute(s_codingAttr,lookup(coding,s_dict_codingStandard));
    coding = data[0] & 0x1f;
    child->setAttribute(s_transferCapAttr,lookup(coding,s_dict_transferCapCCITT));
    coding = data[1] & 0x60;
    child->setAttribute(s_transferModeAttr,lookup(coding,s_dict_transferModeCCITT));
    u_int8_t rate = data[1] & 0x1f;
    child->setAttribute(s_transferRateAttr,lookup(rate,s_dict_transferRateCCITT));
    // Figure 4.11 Note 1: Next byte is the rate multiplier if the transfer rate is 'multirate' (0x18)
    u_int8_t crt = 2;
    if (rate == 0x18) {
	if (len < 3) {
	    Debug(&__plugin,DebugMild,"decodeUSI(). Invalid length %u. No rate multiplier",len);
	    return false;
	}
	child->setAttribute(s_multiplierAttr,String(data[2] & 0x7f));
	crt = 3;
    }
    if (len <= crt) {
	data.cut(-len);
	return true;
    }

    u_int8_t ident = (data[crt] & 0x60) >> 5;
    if (ident != 1) {
	Debug(&__plugin,DebugNote,"decodeUSI(). Invalid layer 1 ident %u",ident);
	return true;
    }
    child->addText(lookup(data[crt] & 0x1f,s_dict_formatCCITT));

    data.cut(-len);
    return true;
}

static bool encodeUSI(const Parameter* param,  MapCamelType* type, DataBlock& data, XmlElement* elem, int& err)
{
    if (!(param && elem))
	return false;
    XDebug(&__plugin,DebugAll,"encodUSI(param=%s[%p],elem=%s[%p],datalen=%d)",param->name.c_str(),param,
	elem->getTag().c_str(),elem,data.length());

    u_int8_t buff[5] = {2,0x00,0x80,0x80,0x80};
    unsigned char coding = lookup(elem->attribute(s_codingAttr),s_dict_codingStandard,0);
    unsigned char cap = lookup(elem->attribute(s_transferCapAttr),s_dict_transferCapCCITT,0);
    unsigned char mode = lookup(elem->attribute(s_transferModeAttr),s_dict_transferModeCCITT,0);
    unsigned char rate = lookup(elem->attribute(s_transferRateAttr),s_dict_transferRateCCITT,0x10);

    buff[1] = (coding & 0x60) | (cap & 0x1f);
    buff[2] |= (mode & 0x60) | (rate & 0x1f);
    if (rate == 0x18) {
	buff[0] = 3;
	rate = String(elem->attribute(s_multiplierAttr)).toInteger();
	buff[3] |= rate & 0x7f;
    }
    // User information layer data
    // Bit 7 = 1, Bits 5,6 = layer (1), Bits 0-4: the value
    int format = lookup(elem->getText(),s_dict_formatCCITT,-1);
    if (format != -1) {
	buff[buff[0] + 1] |= 0x20 | (((unsigned char)format) & 0x1f);
	buff[0]++;
    }
    data.assign(buff,buff[0] + 1);
    data.insert(param->tag.coding());
    return true;
}

static const StringList s_locationManagementCapabOps("updateLocation,cancelLocation,purgeMS,updateGprsLocation,anyTimeInterrogation");
static const StringList s_authenticationCapabOps("sendAuthenticationInfo,authenticationFailureReport");
static const StringList s_subscriberDataCapabOps("insertSubscriberData,deleteSubscriberData,restoreData");
static const StringList s_routingCapabOps("sendRoutingInfoForGprs,sendRoutingInfoForLCS,statusReport");
static const StringList s_vlrRoutingCapabOps("provideRoamingNumber,provideSubscriberInfo");
static const StringList s_traceSubscriberCapabOps("activateTraceMode,deactivateTraceMode");
static const StringList s_servicesCapabOps("registerSS,eraseSS,activateSS,deactivateSS,interrogateSS,registerPassword,getPassword,"
				    "processUnstructuredSS-Request,unstructuredSS-Request,unstructuredSS-Notify");
static const StringList s_miscellaneousCapabOps("sendIMSI,readyForSM,setReportingState");
static const StringList s_errorRecoveryCapabOps("reset,forwardCheckSS-Indication,failureReport");
static const StringList s_smscCapabOps("informServiceCentre,alertServiceCentre,sendRoutingInfoForSM,mo-forwardSM,mt-forwardSM,forwardSM");

static const StringList s_noOps("");

static const Capability s_mapCapab[] = {
    {"LocationManagement",       s_locationManagementCapabOps},
    {"Authentication",           s_authenticationCapabOps},
    {"SubscriberData",           s_subscriberDataCapabOps},
    {"Routing",                  s_routingCapabOps},
    {"VLR-Routing",              s_vlrRoutingCapabOps},
    {"TraceSubscriber",          s_traceSubscriberCapabOps},
    {"Services",                 s_servicesCapabOps},
    {"Miscellaneous",            s_miscellaneousCapabOps},
    {"ErrorRecovery",            s_errorRecoveryCapabOps},
    {"Charging",                 s_noOps},
    {"SMSC",                     s_smscCapabOps},
    {"None",                     s_noOps},
    {0, s_noOps},
};

static const StringList s_smscCapabOIDs("shortMsgMO-RelayContext-v3,shortMsgMO-RelayContext-v2,shortMsgMO-RelayContext-v1,"
					"shortMsgMT-RelayContext-v3,shortMsgMT-RelayContext-v2");
static const Capability s_mapCapabOID[] = {
    {"SMSC",                     s_smscCapabOIDs},
    {"None",                     s_noOps},
    {0, s_noOps},
};

static const Capability s_camelCapabOID[] = {
    {"None",                     s_noOps},
    {0, s_noOps},
};


static const MapCamelType s_types[] = {
    {TcapXApplication::Null,                   TcapXApplication::NullEnc,       decodeNull,        encodeNull},
    {TcapXApplication::Integer,                TcapXApplication::IntEnc,        decodeInt,         encodeInt},
    {TcapXApplication::OID,                    TcapXApplication::OIDEnc,        decodeOID,         encodeOID},
    {TcapXApplication::TBCD,                   TcapXApplication::StringEnc,     decodeTBCD,        encodeTBCD},
    {TcapXApplication::AddressString,          TcapXApplication::TelephonyEnc,  decodeTel,         encodeTel},
    {TcapXApplication::HexString,              TcapXApplication::HexEnc,        decodeHex,         encodeHex},
    {TcapXApplication::Sequence,               TcapXApplication::NoEnc,         decodeSeq,         encodeSeq},
    {TcapXApplication::SequenceOf,             TcapXApplication::NoEnc,         decodeSeqOf,       encodeSeqOf},
    // for now, use the same encoder/decoder as SequenceOf, TODO - if needed,replace it
    {TcapXApplication::SetOf,                  TcapXApplication::NoEnc,         decodeSeqOf,       encodeSeqOf},
    {TcapXApplication::Choice,                 TcapXApplication::NoEnc,         decodeChoice,      encodeChoice},
    {TcapXApplication::Enumerated,             TcapXApplication::NoEnc,         decodeEnumerated,  encodeEnumerated},
    {TcapXApplication::GSMString,              TcapXApplication::StringEnc,     decodeGSMString,   encodeGSMString},
    {TcapXApplication::BitString,              TcapXApplication::HexEnc,        decodeBitString,   encodeBitString},
    {TcapXApplication::Flags,                  TcapXApplication::StringEnc,     decodeFlags,       encodeFlags},
    {TcapXApplication::AppString,              TcapXApplication::StringEnc,     decodeString,      encodeString},
    {TcapXApplication::Bool,                   TcapXApplication::BoolEnc,       decodeBool,        encodeBool},
    {TcapXApplication::CellIdFixedLength,      TcapXApplication::StringEnc,     decodeTBCD,        encodeTBCD},
    {TcapXApplication::LAIFixedLength,         TcapXApplication::StringEnc,     decodeTBCD,        encodeTBCD},
    {TcapXApplication::CalledPartyNumber,      TcapXApplication::TelephonyEnc,  decodeCallNumber,  encodeCallNumber},
    {TcapXApplication::CallingPartyNumber,     TcapXApplication::TelephonyEnc,  decodeCallNumber,  encodeCallNumber},
    {TcapXApplication::LocationNumber,         TcapXApplication::TelephonyEnc,  decodeCallNumber,  encodeCallNumber},
    {TcapXApplication::OriginalCalledNumber,   TcapXApplication::TelephonyEnc,  decodeCallNumber,  encodeCallNumber},
    {TcapXApplication::RedirectingNumber,      TcapXApplication::TelephonyEnc,  decodeCallNumber,  encodeCallNumber},
    {TcapXApplication::GenericNumber,          TcapXApplication::TelephonyEnc,  decodeCallNumber,  encodeCallNumber},
    {TcapXApplication::ChargeNumber,           TcapXApplication::TelephonyEnc,  decodeCallNumber,  encodeCallNumber},
    {TcapXApplication::RedirectionInformation, TcapXApplication::StringEnc,     decodeRedir,       encodeRedir},
    {TcapXApplication::UserServiceInfo,        TcapXApplication::NoEnc,         decodeUSI,         encodeUSI},
    // TODO High Layer Compatibility encode/decode implementation  - see User teleservice information (Q.763 3.59)
    {TcapXApplication::HiLayerCompat,          TcapXApplication::NoEnc,         decodeHex,         encodeHex},
    {TcapXApplication::None,                   TcapXApplication::NoEnc,         0,                 0},
};


static const MapCamelType* findType(TcapXApplication::ParamType type)
{
    const MapCamelType* t = s_types;
    while (t->type != TcapXApplication::None) {
	if (t->type == type)
	    return t;
	t++;
    }
    return 0;
}

static const AsnTag s_ctxtPrim_0_Tag(AsnTag::Context, AsnTag::Primitive, 0);
static const AsnTag s_ctxtPrim_1_Tag(AsnTag::Context, AsnTag::Primitive, 1);
static const AsnTag s_ctxtPrim_2_Tag(AsnTag::Context, AsnTag::Primitive, 2);
static const AsnTag s_ctxtPrim_3_Tag(AsnTag::Context, AsnTag::Primitive, 3);
static const AsnTag s_ctxtPrim_4_Tag(AsnTag::Context, AsnTag::Primitive, 4);
static const AsnTag s_ctxtPrim_5_Tag(AsnTag::Context, AsnTag::Primitive, 5);
static const AsnTag s_ctxtPrim_6_Tag(AsnTag::Context, AsnTag::Primitive, 6);
static const AsnTag s_ctxtPrim_7_Tag(AsnTag::Context, AsnTag::Primitive, 7);
static const AsnTag s_ctxtPrim_8_Tag(AsnTag::Context, AsnTag::Primitive, 8);
static const AsnTag s_ctxtPrim_9_Tag(AsnTag::Context, AsnTag::Primitive, 9);
static const AsnTag s_ctxtPrim_10_Tag(AsnTag::Context, AsnTag::Primitive, 10);
static const AsnTag s_ctxtPrim_11_Tag(AsnTag::Context, AsnTag::Primitive, 11);
static const AsnTag s_ctxtPrim_12_Tag(AsnTag::Context, AsnTag::Primitive, 12);
static const AsnTag s_ctxtPrim_13_Tag(AsnTag::Context, AsnTag::Primitive, 13);
static const AsnTag s_ctxtPrim_14_Tag(AsnTag::Context, AsnTag::Primitive, 14);
static const AsnTag s_ctxtPrim_15_Tag(AsnTag::Context, AsnTag::Primitive, 15);
static const AsnTag s_ctxtPrim_16_Tag(AsnTag::Context, AsnTag::Primitive, 16);
static const AsnTag s_ctxtPrim_17_Tag(AsnTag::Context, AsnTag::Primitive, 17);
static const AsnTag s_ctxtPrim_18_Tag(AsnTag::Context, AsnTag::Primitive, 18);
static const AsnTag s_ctxtPrim_19_Tag(AsnTag::Context, AsnTag::Primitive, 19);
static const AsnTag s_ctxtPrim_20_Tag(AsnTag::Context, AsnTag::Primitive, 20);
static const AsnTag s_ctxtPrim_21_Tag(AsnTag::Context, AsnTag::Primitive, 21);
static const AsnTag s_ctxtPrim_22_Tag(AsnTag::Context, AsnTag::Primitive, 22);
static const AsnTag s_ctxtPrim_23_Tag(AsnTag::Context, AsnTag::Primitive, 23);
static const AsnTag s_ctxtPrim_24_Tag(AsnTag::Context, AsnTag::Primitive, 24);
static const AsnTag s_ctxtPrim_25_Tag(AsnTag::Context, AsnTag::Primitive, 25);
static const AsnTag s_ctxtPrim_26_Tag(AsnTag::Context, AsnTag::Primitive, 26);
static const AsnTag s_ctxtPrim_27_Tag(AsnTag::Context, AsnTag::Primitive, 27);
static const AsnTag s_ctxtPrim_28_Tag(AsnTag::Context, AsnTag::Primitive, 28);
static const AsnTag s_ctxtPrim_29_Tag(AsnTag::Context, AsnTag::Primitive, 29);
static const AsnTag s_ctxtPrim_30_Tag(AsnTag::Context, AsnTag::Primitive, 30);
static const AsnTag s_ctxtPrim_33_Tag(AsnTag::Context, AsnTag::Primitive, 33);
static const AsnTag s_ctxtPrim_34_Tag(AsnTag::Context, AsnTag::Primitive, 34);
static const AsnTag s_ctxtPrim_35_Tag(AsnTag::Context, AsnTag::Primitive, 35);
static const AsnTag s_ctxtPrim_50_Tag(AsnTag::Context, AsnTag::Primitive, 50);
static const AsnTag s_ctxtPrim_53_Tag(AsnTag::Context, AsnTag::Primitive, 53);
static const AsnTag s_ctxtPrim_54_Tag(AsnTag::Context, AsnTag::Primitive, 54);
static const AsnTag s_ctxtPrim_55_Tag(AsnTag::Context, AsnTag::Primitive, 55);
static const AsnTag s_ctxtPrim_56_Tag(AsnTag::Context, AsnTag::Primitive, 56);
static const AsnTag s_ctxtPrim_57_Tag(AsnTag::Context, AsnTag::Primitive, 57);
static const AsnTag s_ctxtPrim_58_Tag(AsnTag::Context, AsnTag::Primitive, 58);

static const AsnTag s_ctxtCstr_0_Tag(AsnTag::Context, AsnTag::Constructor, 0);
static const AsnTag s_ctxtCstr_1_Tag(AsnTag::Context, AsnTag::Constructor, 1);
static const AsnTag s_ctxtCstr_2_Tag(AsnTag::Context, AsnTag::Constructor, 2);
static const AsnTag s_ctxtCstr_3_Tag(AsnTag::Context, AsnTag::Constructor, 3);
static const AsnTag s_ctxtCstr_4_Tag(AsnTag::Context, AsnTag::Constructor, 4);
static const AsnTag s_ctxtCstr_5_Tag(AsnTag::Context, AsnTag::Constructor, 5);
static const AsnTag s_ctxtCstr_6_Tag(AsnTag::Context, AsnTag::Constructor, 6);
static const AsnTag s_ctxtCstr_7_Tag(AsnTag::Context, AsnTag::Constructor, 7);
static const AsnTag s_ctxtCstr_8_Tag(AsnTag::Context, AsnTag::Constructor, 8);
static const AsnTag s_ctxtCstr_9_Tag(AsnTag::Context, AsnTag::Constructor, 9);
static const AsnTag s_ctxtCstr_10_Tag(AsnTag::Context, AsnTag::Constructor, 10);
static const AsnTag s_ctxtCstr_11_Tag(AsnTag::Context, AsnTag::Constructor, 11);
static const AsnTag s_ctxtCstr_12_Tag(AsnTag::Context, AsnTag::Constructor, 12);
static const AsnTag s_ctxtCstr_13_Tag(AsnTag::Context, AsnTag::Constructor, 13);
static const AsnTag s_ctxtCstr_14_Tag(AsnTag::Context, AsnTag::Constructor, 14);
static const AsnTag s_ctxtCstr_15_Tag(AsnTag::Context, AsnTag::Constructor, 15);
static const AsnTag s_ctxtCstr_16_Tag(AsnTag::Context, AsnTag::Constructor, 16);
static const AsnTag s_ctxtCstr_17_Tag(AsnTag::Context, AsnTag::Constructor, 17);
static const AsnTag s_ctxtCstr_18_Tag(AsnTag::Context, AsnTag::Constructor, 18);
static const AsnTag s_ctxtCstr_21_Tag(AsnTag::Context, AsnTag::Constructor, 21);
static const AsnTag s_ctxtCstr_22_Tag(AsnTag::Context, AsnTag::Constructor, 22);
static const AsnTag s_ctxtCstr_23_Tag(AsnTag::Context, AsnTag::Constructor, 23);
static const AsnTag s_ctxtCstr_24_Tag(AsnTag::Context, AsnTag::Constructor, 24);
static const AsnTag s_ctxtCstr_25_Tag(AsnTag::Context, AsnTag::Constructor, 25);
static const AsnTag s_ctxtCstr_27_Tag(AsnTag::Context, AsnTag::Constructor, 27);
static const AsnTag s_ctxtCstr_28_Tag(AsnTag::Context, AsnTag::Constructor, 28);
static const AsnTag s_ctxtCstr_29_Tag(AsnTag::Context, AsnTag::Constructor, 29);
static const AsnTag s_ctxtCstr_30_Tag(AsnTag::Context, AsnTag::Constructor, 30);
static const AsnTag s_ctxtCstr_31_Tag(AsnTag::Context, AsnTag::Constructor, 31);
static const AsnTag s_ctxtCstr_32_Tag(AsnTag::Context, AsnTag::Constructor, 32);
static const AsnTag s_ctxtCstr_50_Tag(AsnTag::Context, AsnTag::Constructor, 50);
static const AsnTag s_ctxtCstr_51_Tag(AsnTag::Context, AsnTag::Constructor, 51);
static const AsnTag s_ctxtCstr_52_Tag(AsnTag::Context, AsnTag::Constructor, 52);
static const AsnTag s_ctxtCstr_53_Tag(AsnTag::Context, AsnTag::Constructor, 53);
static const AsnTag s_ctxtCstr_57_Tag(AsnTag::Context, AsnTag::Constructor, 57);
static const AsnTag s_ctxtCstr_59_Tag(AsnTag::Context, AsnTag::Constructor, 59);

static const TokenDict s_camelPhases[] = {
    {"phase1",      1},
    {"phase2",      2},
    {"phase3",      4},
    {"phase4",      8},
    {0,0},
};

static const TokenDict s_ISTSupportIndicator[] = {
    {"basicISTSupported",   0},
    {"istCommandSupported", 1},
    {0,                     0},
};

static const Parameter s_superChargerInfo[] = {
    {"sendSubscriberData",      s_ctxtPrim_0_Tag, false, TcapXApplication::Null,          0},
    {"subscriberDataStored",    s_ctxtPrim_1_Tag, false, TcapXApplication::HexString,     0},
    {"",                        s_noTag,          false, TcapXApplication::None,          0},
};

static const TokenDict s_supportedLCSCapabilitySets[] = {
    {"lcsCapabilitySet1",  0x01},
    {"lcsCapabilitySet2",  0x02},
    {"lcsCapabilitySet3",  0x04},
    {"lcsCapabilitySet4",  0x08},
    {"lcsCapabilitySet5",  0x10},
    {0,                    0},
};

static const TokenDict s_offeredCamel4CSIs[] = {
    {"o-csi",            0x01},
    {"d-csi",            0x02},
    {"vt-csi",           0x04},
    {"t-csi",            0x08},
    {"mt-sms-csi",       0x10},
    {"mg-csi",           0x20},
    {"psi-enhancements", 0x40},
    {0,                  0},
};

static const TokenDict s_supportedRATTypes[] = {
    {"utran",            0x01},
    {"geran",            0x02},
    {"gan",              0x04},
    {"i-hspa-evolution", 0x08},
    {"e-utran",          0x10},
    {0,                  0},
};

static const Parameter s_vlrCapability[] = {
    {"supportedCamelPhases",                        s_ctxtPrim_0_Tag, true, TcapXApplication::BitString,  s_camelPhases},
    {"extensionContainer",                          s_sequenceTag,    true, TcapXApplication::HexString,  0},
    {"solsaSupportIndicator",                       s_ctxtPrim_2_Tag, true, TcapXApplication::Null,       0},
    {"istSupportIndicator",                         s_ctxtPrim_1_Tag, true, TcapXApplication::Enumerated, s_ISTSupportIndicator},
    {"superChargerSupportedInServingNetworkEntity", s_ctxtCstr_3_Tag, true, TcapXApplication::Choice,     s_superChargerInfo},
    {"longFTN-Supported",                           s_ctxtPrim_4_Tag, true, TcapXApplication::Null,       0},
    {"supportedLCS-CapabilitySets",                 s_ctxtPrim_5_Tag, true, TcapXApplication::BitString,  s_supportedLCSCapabilitySets},
    {"offeredCamel4CSIs",                           s_ctxtPrim_6_Tag, true, TcapXApplication::BitString,  s_offeredCamel4CSIs},
    {"supportedRAT-TypesIndicator",                 s_ctxtPrim_7_Tag, true, TcapXApplication::BitString,  s_supportedRATTypes},
    {"longGroupID-Supported",                       s_ctxtPrim_8_Tag, true, TcapXApplication::Null,       0},
    {"",                                            s_noTag,          false,TcapXApplication::None,       0},
};

static const Parameter s_addInfoSeq[] = {
    {"imeisv",                       s_ctxtPrim_0_Tag, false, TcapXApplication::TBCD, 0},
    {"skipSubscriberDataUpdate",     s_ctxtPrim_1_Tag, true,  TcapXApplication::Null, 0},
    {"",                             s_noTag,          false, TcapXApplication::None, 0},
};

static const Parameter s_locationArea[] = {
    {"laiFixedLength",    s_ctxtPrim_0_Tag, false, TcapXApplication::LAIFixedLength,    0},
    {"lac",               s_ctxtPrim_1_Tag, false, TcapXApplication::HexString,         0},
    {"",                  s_noTag,          false, TcapXApplication::None,              0},
};

static const Parameter s_locationAreaChoice[] = {
    {"locationArea",      s_noTag, false, TcapXApplication::Choice,    s_locationArea},
    {"",                  s_noTag, false, TcapXApplication::None,      0},
};

static const Parameter s_imsiWithLmsi[] = {
    {"imsi",              s_hexTag,    false,   TcapXApplication::TBCD,      0},
    {"lmsi",              s_hexTag,    false,   TcapXApplication::HexString, 0},
    {"",                  s_noTag,     false,   TcapXApplication::None,      0},
};

static const Parameter s_mapIdentity[] = {
    {"imsi",              s_hexTag,         false,    TcapXApplication::TBCD,      0},
    {"imsi-WithLMSI",     s_sequenceTag,    false,    TcapXApplication::Sequence,  s_imsiWithLmsi},
    {"",                  s_noTag,          false,    TcapXApplication::None,      0},
};

static const TokenDict s_cancellationType[] = {
    {"updateProcedure",         0x00},
    {"subscriptionWithdraw",    0x01},
    {"initialAttachProcedure",  0x02},
    {0,0},
};

static const TokenDict s_typeOfUpdate[] = {
    {"sgsn-change",   0x00},
    {"mme-change",    0x01},
    {0,0},
};

static const TokenDict s_protocolId[] = {
    {"gsm-0408",    0x01},
    {"gsm-0806",    0x02},
    {"gsm-BSSMAP",  0x03},
    {"ets-300102-1",0x04},
    {0,0},
};

static const Parameter s_externalSignalInfo[] = {
    {"protocolId",        s_enumTag,       false,    TcapXApplication::Enumerated,    s_protocolId},
    {"signalInfo",        s_hexTag,        false,    TcapXApplication::HexString,     0},
    {"extensionContainer",s_sequenceTag,   true,     TcapXApplication::HexString,     0},
    {"",                  s_noTag,         false,    TcapXApplication::None,          0},
};

static const TokenDict s_alertPattern[] = {
    {"alertingLevel-0",    0x00},
    {"alertingLevel-1",    0x01},//   AlertingPattern ::= '00000001'B
    {"alertingLevel-2",    0x02},//   AlertingPattern ::= '00000010'B
    {"alertingCategory-1", 0x04},//   AlertingPattern ::= '00000100'B
    {"alertingCategory-2", 0x05},//   AlertingPattern ::= '00000101'B
    {"alertingCategory-3", 0x06},//   AlertingPattern ::= '00000110'B
    {"alertingCategory-4", 0x07},//   AlertingPattern ::= '00000111'B
    {"alertingCategory-5", 0x08},//   AlertingPattern ::= '00001000'B
    {0,0},
};

static const TokenDict s_extProtocolId[] = {
    {"ets-300356",    0x01},
    {0,0},
};

static const Parameter s_extExtenalSignalInfo[] = {
    {"ext-protocolId",    s_enumTag,      false,    TcapXApplication::Enumerated,    s_extProtocolId},
    {"signalInfo",        s_hexTag,       false,    TcapXApplication::HexString,     0},
    {"extensionContainer",s_sequenceTag,  true,     TcapXApplication::HexString,     0},
    {"",                  s_noTag,        false,    TcapXApplication::None,          0},
};

static const TokenDict s_category[] = {
    { "unknown",     0x00 },                // calling party's category is unknown
    { "operator-FR", 0x01 },                // operator, language French
    { "operator-EN", 0x02 },                // operator, language English
    { "operator-DE", 0x03 },                // operator, language German
    { "operator-RU", 0x04 },                // operator, language Russian
    { "operator-ES", 0x05 },                // operator, language Spanish
    { "ordinary",    0x0a },                // ordinary calling subscriber
    { "priority",    0x0b },                // calling subscriber with priority
    { "data",        0x0c },                // data call (voice band data)
    { "test",        0x0d },                // test call
    { "payphone",    0x0f },                // payphone
    { 0, 0 },
};

static const TokenDict s_subscriberStatus[] = {
    {"serviceGranted",                 0x00},
    {"operatorDeterminedBarring",      0x01},
    {0,0},
};

static const TokenDict s_SSCode[] = {
// TS 100 974 v7.15.0 page 321
    {"allSS",                      0x00}, //    SS-Code ::= '00000000'B  -- all SS
    {"allLineIdentificationSS",    0x10}, //    SS-Code ::= '00010000'B  -- all line identification SS
    {"clip",                       0x11}, //    SS-Code ::= '00010001'B  -- calling line identification presentation
    {"clir",                       0x12}, //    SS-Code ::= '00010010'B  -- calling line identification restriction
    {"colp",                       0x13}, //    SS-Code ::= '00010011'B  -- connected line identification presentation
    {"colr",                       0x14}, //    SS-Code ::= '00010100'B  -- connected line identification restriction
    {"mci",                        0x15}, //    SS-Code ::= '00010101'B  -- malicious call identification
    {"allNameIdentificationSS",    0x18}, //    SS-Code ::= '00011000'B  -- all name identification SS
    {"cnap",                       0x19}, //    SS-Code ::= '00011001'B  -- calling name presentation
    {"allForwardingSS",            0x20}, //    SS-Code ::= '00100000'B  -- all forwarding SS
    {"cfu",                        0x21}, //    SS-Code ::= '00100001'B  -- call forwarding unconditional
    {"allCondForwardingSS",        0x28}, //    SS-Code ::= '00101000'B  -- all conditional forwarding SS
    {"cfb",                        0x29}, //    SS-Code ::= '00101001'B  -- call forwarding on mobile subscriber busy
    {"cfnry",                      0x2a}, //    SS-Code ::= '00101010'B  -- call forwarding on no reply
    {"cfnrc",                      0x2b}, //    SS-Code ::= '00101011'B  -- call forwarding on mobile subscriber not reachable
    {"cd",                         0x24}, //    SS-Code ::= '00100100'B  -- call deflection
    {"allCallOfferingSS",          0x30}, //    SS-Code ::= '00110000'B  -- all call offering SS includes also all forwarding SS
    {"ect",                        0x31}, //    SS-Code ::= '00110001'B  -- explicit call transfer
    {"mah",                        0x32}, //    SS-Code ::= '00110010'B  -- mobile access hunting
    {"allCallCompletionSS",        0x40}, //    SS-Code ::= '01000000'B  -- all Call completion SS
    {"cw",                         0x41}, //    SS-Code ::= '01000001'B  -- call waiting
    {"hold",                       0x42}, //    SS-Code ::= '01000010'B  -- call hold
    {"ccbs-A",                     0x43}, //    SS-Code ::= '01000011'B  -- completion of call to busy subscribers, originating side
    {"ccbs-B",                     0x44}, //    SS-Code ::= '01000100'B  -- completion of call to busy subscribers, destination side
    {"allMultiPartySS",            0x50}, //    SS-Code ::= '01010000'B  -- all multiparty SS
    {"multiPTY",                   0x51}, //    SS-Code ::= '01010001'B  -- multiparty
    {"allCommunityOfInterest-SS",  0x60}, //    SS-Code ::= '01100000'B  -- all community of interest SS
    {"cug",                        0x61}, //    SS-Code ::= '01100001'B  -- closed user group
    {"allChargingSS",              0x70}, //    SS-Code ::= '01110000'B  -- all charging SS
    {"aoci",                       0x71}, //    SS-Code ::= '01110001'B  -- advice of charge information
    {"aocc",                       0x72}, //    SS-Code ::= '01110010'B  -- advice of charge charging
    {"allAdditionalInfoTransferSS",0x80}, //    SS-Code ::= '10000000'B  -- all additional information transfer SS
    {"uus1",                       0x81}, //    SS-Code ::= '10000001'B  -- UUS1 user-to-user signalling
    {"uus2",                       0x82}, //    SS-Code ::= '10000010'B  -- UUS2 user-to-user signalling
    {"uus3",                       0x83}, //    SS-Code ::= '10000011'B  -- UUS3 user-to-user signalling
    {"allBarringSS",               0x90}, //    SS-Code ::= '10010000'B  -- all barring SS
    {"barringOfOutgoingCalls",     0x91}, //    SS-Code ::= '10010001'B  -- barring of outgoing calls
    {"baoc",                       0x92}, //    SS-Code ::= '10010010'B  -- barring of all outgoing calls
    {"boic",                       0x93}, //    SS-Code ::= '10010011'B  -- barring of outgoing international calls
    {"boicExHC",                   0x94}, //    SS-Code ::= '10010100'B  -- barring of outgoing international calls except those directed to the home PLMN
    {"barringOfIncomingCalls",     0x99}, //    SS-Code ::= '10011001'B  -- barring of incoming calls
    {"baic",                       0x9a}, //    SS-Code ::= '10011010'B  -- barring of all incoming calls
    {"bicRoam",                    0x9b}, //    SS-Code ::= '10011011'B  -- barring of incoming calls when roaming outside home PLMN Country
    {"allPLMN-specificSS",         0xf0}, //    SS-Code ::= '11110000'B
    {"plmn-specificSS-1",          0xf1}, //    SS-Code ::= '11110001'B
    {"plmn-specificSS-2",          0xf2}, //    SS-Code ::= '11110010'B
    {"plmn-specificSS-3",          0xf3}, //    SS-Code ::= '11110011'B
    {"plmn-specificSS-4",          0xf4}, //    SS-Code ::= '11110100'B
    {"plmn-specificSS-5",          0xf5}, //    SS-Code ::= '11110101'B
    {"plmn-specificSS-6",          0xf6}, //    SS-Code ::= '11110110'B
    {"plmn-specificSS-7",          0xf7}, //    SS-Code ::= '11110111'B
    {"plmn-specificSS-8",          0xf8}, //    SS-Code ::= '11111000'B
    {"plmn-specificSS-9",          0xf9}, //    SS-Code ::= '11111001'B
    {"plmn-specificSS-A",          0xfa}, //    SS-Code ::= '11111010'B
    {"plmn-specificSS-B",          0xfb}, //    SS-Code ::= '11111011'B
    {"plmn-specificSS-C",          0xfc}, //    SS-Code ::= '11111100'B
    {"plmn-specificSS-D",          0xfd}, //    SS-Code ::= '11111101'B
    {"plmn-specificSS-E",          0xfe}, //    SS-Code ::= '11111110'B
    {"plmn-specificSS-F",          0xff}, //    SS-Code ::= '11111111'B
    {"allCallPrioritySS",          0xa0}, //    SS-Code ::= '10100000'B  -- all call priority SS
    {"emlpp",                      0xa1}, //    SS-Code ::= '10100001'B  -- enhanced Multilevel Precedence Pre-emption (EMLPP) service
    {"allLCSPrivacyException",     0xb0}, //    SS-Code ::= '10110000'B  -- all LCS Privacy Exception Classes
    {"universal",                  0xb1}, //    SS-Code ::= '10110001'B  -- allow location by any LCS client
    {"callrelated",                0xb2}, //    SS-Code ::= '10110010'B  -- allow location by any value added LCS client to which a call
//                                                                       -- is established from the target MS
    {"callunrelated",              0xb3}, //    SS-Code ::= '10110011'B  -- allow location by designated external value added LCS clients
    {"plmnoperator",               0xb4}, //    SS-Code ::= '10110100'B  -- allow location by designated PLMN operator LCS clients
    {"allMOLR-SS",                 0xc0}, //    SS-Code ::= '11000000'B  -- all Mobile Originating Location Request Classes
    {"basicSelfLocation",          0xc1}, //    SS-Code ::= '11000001'B  -- allow an MS to request its own location
    {"autonomousSelfLocation",     0xc2}, //    SS-Code ::= '11000010'B  -- allow an MS to perform self location without interaction
//                                                                       -- with the PLMN for a predetermined period of time
    {"transferToThirdParty",       0xc3}, //    SS-Code ::= '11000011'B  -- allow an MS to request transfer of its location to another LCS client
    {0,0},
};

static const TokenDict s_bearerServiceCode[] = {
    {"allBearerServices",      0x00}, // BearerServiceCode ::= '00000000'B
    {"allDataCDA-Services",    0x10}, // BearerServiceCode ::= '00010000'B
    {"dataCDA-300bps",         0x11}, // BearerServiceCode ::= '00010001'B
    {"dataCDA-1200bps",        0x12}, // BearerServiceCode ::= '00010010'B
    {"dataCDA-1200-75bps",     0x13}, // BearerServiceCode ::= '00010011'B
    {"dataCDA-2400bps",        0x14}, // BearerServiceCode ::= '00010100'B
    {"dataCDA-4800bps",        0x15}, // BearerServiceCode ::= '00010101'B
    {"dataCDA-9600bps",        0x16}, // BearerServiceCode ::= '00010110'B
    {"general-dataCDA",        0x17}, // BearerServiceCode ::= '00010111'B
    {"allDataCDS-Services",    0x18}, // BearerServiceCode ::= '00011000'B
    {"dataCDS-1200bps",        0x1a}, // BearerServiceCode ::= '00011010'B
    {"dataCDS-2400bps",        0x1c}, // BearerServiceCode ::= '00011100'B
    {"dataCDS-4800bps",        0x1d}, // BearerServiceCode ::= '00011101'B
    {"dataCDS-9600bps",        0x1e}, // BearerServiceCode ::= '00011110'B
    {"general-dataCDS",        0x1f}, // BearerServiceCode ::= '00011111'B
    {"allPadAccessCA-Services",0x20}, // BearerServiceCode ::= '00100000'B
    {"padAccessCA-300bps",     0x21}, // BearerServiceCode ::= '00100001'B
    {"padAccessCA-1200bps",    0x22}, // BearerServiceCode ::= '00100010'B
    {"padAccessCA-1200-75bps", 0x23}, // BearerServiceCode ::= '00100011'B
    {"padAccessCA-2400bps",    0x24}, // BearerServiceCode ::= '00100100'B
    {"padAccessCA-4800bps",    0x25}, // BearerServiceCode ::= '00100101'B
    {"padAccessCA-9600bps",    0x26}, // BearerServiceCode ::= '00100110'B
    {"general-padAccessCA",    0x27}, // BearerServiceCode ::= '00100111'B
    {"allDataPDS-Services",    0x28}, // BearerServiceCode ::= '00101000'B
    {"dataPDS-2400bps",        0x2c}, // BearerServiceCode ::= '00101100'B
    {"dataPDS-4800bps",        0x2d}, // BearerServiceCode ::= '00101101'B
    {"dataPDS-9600bps",        0x2e}, // BearerServiceCode ::= '00101110'B
    {"general-dataPDS",        0x2f}, // BearerServiceCode ::= '00101111'B

    {"allAlternateSpeech-DataCDA",    0x30}, // BearerServiceCode ::= '00110000'B
    {"allAlternateSpeech-DataCDS",    0x38}, // BearerServiceCode ::= '00111000'B
    {"allSpeechFollowedByDataCDA",    0x40}, // BearerServiceCode ::= '01000000'B
    {"allSpeechFollowedByDataCDS",    0x48}, // BearerServiceCode ::= '01001000'B
    {"allDataCircuitAsynchronous",    0x50}, // BearerServiceCode ::= '01010000'B
    {"allAsynchronousServices",       0x60}, // BearerServiceCode ::= '01100000'B
    {"allDataCircuitSynchronous",     0x58}, // BearerServiceCode ::= '01011000'B
    {"allSynchronousServices",        0x68}, // BearerServiceCode ::= '01101000'B

    {"allPLMN-specificBS",   0xd0}, //        BearerServiceCode ::= '11010000'B
    {"plmn-specificBS-1",    0xd1}, //        BearerServiceCode ::= '11010001'B
    {"plmn-specificBS-2",    0xd2}, //        BearerServiceCode ::= '11010010'B
    {"plmn-specificBS-3",    0xd3}, //        BearerServiceCode ::= '11010011'B
    {"plmn-specificBS-4",    0xd4}, //        BearerServiceCode ::= '11010100'B
    {"plmn-specificBS-5",    0xd5}, //        BearerServiceCode ::= '11010101'B
    {"plmn-specificBS-6",    0xd6}, //        BearerServiceCode ::= '11010110'B
    {"plmn-specificBS-7",    0xd7}, //        BearerServiceCode ::= '11010111'B
    {"plmn-specificBS-8",    0xd8}, //        BearerServiceCode ::= '11011000'B
    {"plmn-specificBS-9",    0xd9}, //        BearerServiceCode ::= '11011001'B
    {"plmn-specificBS-A",    0xda}, //        BearerServiceCode ::= '11011010'B
    {"plmn-specificBS-B",    0xdb}, //        BearerServiceCode ::= '11011011'B
    {"plmn-specificBS-C",    0xdc}, //        BearerServiceCode ::= '11011100'B
    {"plmn-specificBS-D",    0xdd}, //        BearerServiceCode ::= '11011101'B
    {"plmn-specificBS-E",    0xde}, //        BearerServiceCode ::= '11011110'B
    {"plmn-specificBS-F",    0xdf}, //        BearerServiceCode ::= '11011111'B
    {0,0},
};

static const TokenDict s_teleserviceCode[] = {
    {"allTeleservices",                  0x00},  // TeleserviceCode ::= '00000000'B
    {"allSpeechTransmissionServices",    0x10},  // TeleserviceCode ::= '00010000'B
    {"telephony",                        0x11},  // TeleserviceCode ::= '00010001'B
    {"emergencyCalls",                   0x12},  // TeleserviceCode ::= '00010010'B
    {"allShortMessageServices",          0x20},  // TeleserviceCode ::= '00100000'B
    {"shortMessageMT-PP",                0x21},  // TeleserviceCode ::= '00100001'B
    {"shortMessageMO-PP",                0x22},  // TeleserviceCode ::= '00100010'B
    {"allFacsimileTransmissionServices", 0x60},  // TeleserviceCode ::= '01100000'B
    {"facsimileGroup3AndAlterSpeech",    0x61},  // TeleserviceCode ::= '01100001'B
    {"automaticFacsimileGroup3",         0x62},  // TeleserviceCode ::= '01100010'B
    {"facsimileGroup4",                  0x63},  // TeleserviceCode ::= '01100011'B
    {"allDataTeleservices",              0x70},  // TeleserviceCode ::= '01110000'B
    {"allTeleservices-ExeptSMS",         0x80},  // TeleserviceCode ::= '10000000'B
    {"allVoiceGroupCallServices",        0x90},  // TeleserviceCode ::= '10010000'B
    {"voiceGroupCall",                   0x91},  // TeleserviceCode ::= '10010001'B
    {"voiceBroadcastCall",               0x92},  // TeleserviceCode ::= '10010010'B
    {"allPLMN-specificTS",               0xd0},  // TeleserviceCode ::= '11010000'B
    {"plmn-specificTS-1",                0xd1},  // TeleserviceCode ::= '11010001'B
    {"plmn-specificTS-2",                0xd2},  // TeleserviceCode ::= '11010010'B
    {"plmn-specificTS-3",                0xd3},  // TeleserviceCode ::= '11010011'B
    {"plmn-specificTS-4",                0xd4},  // TeleserviceCode ::= '11010100'B
    {"plmn-specificTS-5",                0xd5},  // TeleserviceCode ::= '11010101'B
    {"plmn-specificTS-6",                0xd6},  // TeleserviceCode ::= '11010110'B
    {"plmn-specificTS-7",                0xd7},  // TeleserviceCode ::= '11010111'B
    {"plmn-specificTS-8",                0xd8},  // TeleserviceCode ::= '11011000'B
    {"plmn-specificTS-9",                0xd9},  // TeleserviceCode ::= '11011001'B
    {"plmn-specificTS-A",                0xda},  // TeleserviceCode ::= '11011010'B
    {"plmn-specificTS-B",                0xdb},  // TeleserviceCode ::= '11011011'B
    {"plmn-specificTS-C",                0xdc},  // TeleserviceCode ::= '11011100'B
    {"plmn-specificTS-D",                0xdd},  // TeleserviceCode ::= '11011101'B
    {"plmn-specificTS-E",                0xde},  // TeleserviceCode ::= '11011110'B
    {"plmn-specificTS-F",                0xdf},  // TeleserviceCode ::= '11011111'B
    {0,0},
};

static const Parameter s_extBearerServiceCode[] = {
    {"ext-BearerServiceCode",   s_hexTag,    false,    TcapXApplication::HexString,         0},
    {"",                        s_noTag,     false,    TcapXApplication::None,              0},
};

static const Parameter s_extTeleserviceCode[] = {
    {"ext-TeleserviceCode",     s_hexTag,   false,    TcapXApplication::HexString,       0},
    {"",                        s_noTag,    false,    TcapXApplication::None,            0},
};

static const Parameter s_bearerService[] = {
    {"bearerService",           s_hexTag,   false,    TcapXApplication::Enumerated,        s_bearerServiceCode},
    {"",                        s_noTag,    false,    TcapXApplication::None,              0},
};

static const Parameter s_teleservice[] = {
    {"teleservice",             s_hexTag,   false,    TcapXApplication::Enumerated,      s_teleserviceCode},
    {"",                        s_noTag,    false,    TcapXApplication::None,            0},
};

static const Parameter s_basicServiceCode[] = {
    {"bearerService",  s_ctxtPrim_2_Tag,   false,   TcapXApplication::Enumerated, s_bearerServiceCode},
    {"teleservice",    s_ctxtPrim_3_Tag,   false,   TcapXApplication::Enumerated, s_teleserviceCode},
    {"",               s_noTag,            false,   TcapXApplication::None,       0},
};

static const Parameter s_extBasicServiceCode[] = {
    {"ext-BearerService",       s_ctxtPrim_2_Tag,   false,   TcapXApplication::HexString, 0},
    {"ext-Teleservice",         s_ctxtPrim_3_Tag,   false,   TcapXApplication::HexString, 0},
    {"",                        s_noTag,            false,   TcapXApplication::None,      0},
};


static const SignallingFlags s_forwardOptions[] = {
    { 0x80, 0x80, "notify-called" },
    { 0x40, 0x40, "presentation" },
    { 0x20, 0x20, "notify-caller" },
    { 0x0c, 0x00, "offline" },
    { 0x0c, 0x04, "busy" },
    { 0x0c, 0x08, "noanswer" },
    { 0x0c, 0x0c, "always" },
    { 0, 0, 0 },
};

static const SignallingFlags s_ssStatus[] = {
    { 0x01, 0x01, "active" },
    { 0x02, 0x02, "registered" },
    { 0x04, 0x04, "provisioned" },
    { 0x08, 0x08, "quiescent" },
    { 0, 0, 0 },
};

static const Parameter s_forwFeatureSeq[] = {
    {"basicService",            s_noTag,           true,   TcapXApplication::Choice,        s_basicServiceCode},
    {"ss-Status",               s_ctxtPrim_4_Tag,  false,  TcapXApplication::Flags,         s_ssStatus},
    {"forwardedToNumber",       s_ctxtPrim_5_Tag,  true,   TcapXApplication::AddressString, 0},
    {"forwardedToSubaddress",   s_ctxtPrim_8_Tag,  true,   TcapXApplication::HexString,     0},
    {"forwardingOptions",       s_ctxtPrim_6_Tag,  true,   TcapXApplication::Flags,         s_forwardOptions},
    {"noReplyConditionTime",    s_ctxtPrim_7_Tag,  true,   TcapXApplication::Integer,       0},
    {"extensionContainer",      s_ctxtCstr_9_Tag,  true,   TcapXApplication::HexString,     0},
    {"longForwardedToNumber",   s_ctxtPrim_10_Tag, true,   TcapXApplication::AddressString, 0},
    {"",                        s_noTag,           false,  TcapXApplication::None,          0},
};

static const Parameter s_forwFeature[] = {
    {"forwardingFeature",       s_sequenceTag,  true,     TcapXApplication::Sequence,             s_forwFeatureSeq},
    {"",                        s_noTag,        false,    TcapXApplication::None,                 0},
};

static const Parameter s_extForwInfo[] = {
    {"ss-Code",               s_hexTag,          false,  TcapXApplication::Enumerated,    s_SSCode},
    {"forwardingFeatureList", s_sequenceTag,     false,  TcapXApplication::SequenceOf,    s_forwFeature},
    {"extensionContainer",    s_ctxtCstr_0_Tag,  true,   TcapXApplication::HexString,     0},
    {"",                      s_noTag,           false,  TcapXApplication::None,          0},
};

static const Parameter s_extCallBarFeatureSeq[] = {
    {"basicService",          s_noTag,           true,  TcapXApplication::Choice,    s_basicServiceCode},
    {"ss-Status",             s_ctxtPrim_4_Tag,  false, TcapXApplication::Flags,     s_ssStatus},
    {"extensionContainer",    s_sequenceTag,     true,  TcapXApplication::HexString, 0},
    {"",                      s_noTag,           false, TcapXApplication::None,      0},
};

static const Parameter s_extCallBarFeature[] = {
    {"ext-CallBarFeature",    s_sequenceTag,   true,     TcapXApplication::Sequence,      s_extCallBarFeatureSeq},
    {"",                      s_noTag,         false,    TcapXApplication::None,          0},
};

static const Parameter s_extCallBarInfo[] = {
    {"ss-Code",               s_hexTag,         false,  TcapXApplication::Enumerated,    s_SSCode},
    {"callBarringFeatureList",s_sequenceTag,    false,  TcapXApplication::SequenceOf,    s_extCallBarFeature},
    {"extensionContainer",    s_sequenceTag,    true,   TcapXApplication::HexString,     0},
    {"",                      s_noTag,          false,  TcapXApplication::None,          0},
};

static const TokenDict s_intraCUGOptions[] = {
    {"noCUG-Restrictions",    0},
    {"cugIC-CallBarred",      1},
    {"cugOG-CallBarred",      2},
    {0,0},
};

static const Parameter s_basicServiceCodeType[] = {
    {"basicService",          s_noTag,    true,     TcapXApplication::Choice,        s_basicServiceCode},
    {"",                      s_noTag,    false,    TcapXApplication::None,          0},
};

static const Parameter s_CUGSubscriptionSeq[] = {
    {"cug-Index",             s_intTag,          false,  TcapXApplication::Integer,       0},
    {"cug-Interlock",         s_hexTag,          false,  TcapXApplication::HexString,     0},
    {"intraCUG-Options",      s_enumTag,         false,  TcapXApplication::Enumerated,    s_intraCUGOptions},
    {"basicServiceGroupList", s_sequenceTag,     true,   TcapXApplication::SequenceOf,    s_basicServiceCodeType},
    {"extensionContainer",    s_ctxtCstr_0_Tag,  true,   TcapXApplication::HexString,     0},
    {"",                      s_noTag,           false,  TcapXApplication::None,          0},
};

static const Parameter s_CUGSubscription[] = {
    {"cug-Subscription",      s_sequenceTag,   false,    TcapXApplication::Sequence,      s_CUGSubscriptionSeq},
    {"",                      s_noTag,         false,    TcapXApplication::None,          0},
};

static const TokenDict s_interCUGRestrinctions[] = {
    {"CUG-only",                       0x00},
    {"CUG-outgoing-access",            0x01},
    {"CUG-incoming-access",            0x02},
    {"CUG-both",                       0x03},
    {0,0},
};

static const Parameter s_CUGFeatureSeq[] = {
    {"basicService",              s_noTag,        true,   TcapXApplication::Choice,       s_basicServiceCode},
    {"preferentialCUG-Indicator", s_intTag,       true,   TcapXApplication::Integer,      0},
    {"interCUG-Restrictions",     s_hexTag,       false,  TcapXApplication::Enumerated,   s_interCUGRestrinctions},
    {"extensionContainer",        s_sequenceTag,  true,   TcapXApplication::HexString,    0},
    {"",                          s_noTag,        false,  TcapXApplication::None,         0},
};

static const Parameter s_CUGFeature[] = {
    {"cug-Feature",   s_sequenceTag,  false,  TcapXApplication::Sequence,     s_CUGFeatureSeq},
    {"",              s_noTag,        false,  TcapXApplication::None,         0},
};

static const Parameter s_cugInfo[] = {
    {"cug-SubscriptionList",  s_sequenceTag,    false,  TcapXApplication::SequenceOf,    s_CUGSubscription},
    {"cug-FeatureList",       s_sequenceTag,    true,   TcapXApplication::SequenceOf,    s_CUGFeature},
    {"extensionContainer",    s_ctxtCstr_0_Tag, true,   TcapXApplication::HexString,     0},
    {"",                      s_noTag,          false,  TcapXApplication::None,          0},
};

static const TokenDict s_cliRestrictionOption[] = {
    {"permanent",                       0},
    {"temporaryDefaultRestricted",      1},
    {"temporaryDefaultAllowed",         2},
    {0,0},
};

static const TokenDict s_overrideCategory[] = {
    {"overrideEnabled",       0},
    {"overrideDisabled",      1},
    {0,0},
};

static const Parameter s_ssSubscriptionOption[] = {
    {"cliRestrictionOption",  s_ctxtPrim_2_Tag,   false,  TcapXApplication::Enumerated, s_cliRestrictionOption},
    {"overrideCategory",      s_ctxtPrim_1_Tag,   false,  TcapXApplication::Enumerated, s_overrideCategory},
    {"",                      s_noTag,                                         false,  TcapXApplication::None,       0},
};

static const Parameter s_extSSData[] = {
    {"ss-Code",               s_hexTag,          false,    TcapXApplication::Enumerated,    s_SSCode},
    {"ss-Status",             s_ctxtPrim_4_Tag,  false,    TcapXApplication::Flags,         s_ssStatus},
    {"ss-SubscriptionOption", s_noTag,           true,     TcapXApplication::Choice,        s_ssSubscriptionOption},
    {"basicServiceGroupList", s_sequenceTag,     true,     TcapXApplication::SequenceOf,    s_basicServiceCodeType},
    {"extensionContainer",    s_ctxtCstr_5_Tag,  true,     TcapXApplication::HexString,     0},
    {"",                      s_noTag,           false,    TcapXApplication::None,          0},
};

static const TokenDict s_EMLPPPriority[] = {
    {"priorityLevel0", 0},
    {"priorityLevel1", 1},
    {"priorityLevel2", 2},
    {"priorityLevel3", 3},
    {"priorityLevel4", 4},
    {"priorityLevelB", 5},
    {"priorityLevelA", 6},
    {0,0},
};

static const Parameter s_EMLPPInfo[] = {
    {"maximumentitledPriority", s_intTag,        false,    TcapXApplication::Enumerated,    s_EMLPPPriority},
    {"defaultPriority",         s_intTag,        false,    TcapXApplication::Enumerated,    s_EMLPPPriority},
    {"extensionContainer",      s_sequenceTag,   true,     TcapXApplication::HexString,     0},
    {"",                        s_noTag,         false,    TcapXApplication::None,          0},
};

static const Parameter s_extSSInfoChoice[] = {
    {"forwardingInfo",  s_ctxtCstr_0_Tag, false,  TcapXApplication::Sequence, s_extForwInfo},
    {"callBarringInfo", s_ctxtCstr_1_Tag, false,  TcapXApplication::Sequence, s_extCallBarInfo},
    {"cug-Info",        s_ctxtCstr_2_Tag, false,  TcapXApplication::Sequence, s_cugInfo},
    {"ss-Data",         s_ctxtCstr_3_Tag, false,  TcapXApplication::Sequence, s_extSSData},
    {"emlpp-Info",      s_ctxtCstr_4_Tag, false,  TcapXApplication::Sequence, s_EMLPPInfo},
    {"",                s_noTag,          false,  TcapXApplication::None,     0},
};

static const Parameter s_extSSInfo[] = {
    {"SS-Info",         s_noTag,     false,    TcapXApplication::Choice, s_extSSInfoChoice},
    {"",                s_noTag,     false,    TcapXApplication::None,   0},
};

static const TokenDict s_odbGeneralData[] = {
    {"allOG-CallsBarred",                                                   0x00000001},
    {"internationalOGCallsBarred",                                          0x00000002},
    {"internationalOGCallsNotToHPLMN-CountryBarred",                        0x00000004},
    {"interzonalOGCallsBarred",                                             0x00000040},
    {"interzonalOGCallsNotToHPLMN-CountryBarred",                           0x00000080},
    {"interzonalOGCallsAndInternationalOGCallsNotToHPLMN-CountryBarred",    0x00000100},
    {"premiumRateInformationOGCallsBarred",                                 0x00000008},
    {"premiumRateEntertainementOGCallsBarred",                              0x00000010},
    {"ss-AccessBarred",                                                     0x00000020},
    {"allECT-Barred",                                                       0x00000200},
    {"chargeableECT-Barred",                                                0x00000400},
    {"internationalECT-Barred",                                             0x00000800},
    {"interzonalECT-Barred",                                                0x00001000},
    {"doublyChargeableECT-Barred",                                          0x00002000},
    {"multipleECT-Barred",                                                  0x00004000},
    {"allPacketOrientedServicesBarred",                                     0x00008000},
    {"roamerAccessToHPLMN-AP-Barred",                                       0x00010000},
    {"roamerAccessToVPLMN-AP-Barred",                                       0x00020000},
    {"roamingOutsidePLMNOG-CallsBarred",                                    0x00040000},
    {"allIC-CallsBarred",                                                   0x00080000},
    {"roamingOutsidePLMNIC-CallsBarred",                                    0x00100000},
    {"roamingOutsidePLMNICountryIC-CallsBarred",                            0x00200000},
    {"roamingOutsidePLMN-Barred",                                           0x00400000},
    {"roamingOutsidePLMN-CountryBarred",                                    0x00800000},
    {"registrationAllCF-Barred",                                            0x01000000},
    {"registrationCFNotToHPLMN-Barred",                                     0x02000000},
    {"registrationInterzonalCF-Barred",                                     0x04000000},
    {"registrationInterzonalCFNotToHPLMN-Barred",                           0x08000000},
    {"registrationInternationalCF-Barred",                                  0x10000000},
    {0,0},
};

static const TokenDict s_odbHPLMNData[] = {
    {"plmn-SpecificBarringType1", 0x01},
    {"plmn-SpecificBarringType2", 0x02},
    {"plmn-SpecificBarringType3", 0x04},
    {"plmn-SpecificBarringType4", 0x08},
    {0,0},
};

static const Parameter s_odbData[] = {
    {"odb-GeneralData",        s_bitsTag,       false,    TcapXApplication::BitString,  s_odbGeneralData},
    {"odb-HPLMN-Data",         s_bitsTag,       true,     TcapXApplication::BitString,  s_odbHPLMNData},
    {"extensionContainer",     s_sequenceTag,   true,     TcapXApplication::HexString,  0},
    {"",                       s_noTag,         false,    TcapXApplication::None,       0},
};

static const Parameter s_zoneCode[] = {
    {"zoneCode",        s_hexTag,   false,    TcapXApplication::HexString,    0},
    {"",                s_noTag,    false,    TcapXApplication::None,         0},
};

static const Parameter s_voiceBroadcastDataSeq[] = {
    {"groupid",                  s_hexTag,         false,   TcapXApplication::TBCD,       0},
    {"broadcastInitEntitlement", s_nullTag,        true,    TcapXApplication::Null,       0},
    {"extensionContainer",       s_sequenceTag,    true,    TcapXApplication::HexString,  0},
    {"longGroupId",              s_ctxtPrim_0_Tag, true,    TcapXApplication::TBCD,       0},
    {"",                         s_noTag,          false,   TcapXApplication::None,       0},
};

static const Parameter s_voiceBroadcastData[] = {
    {"voiceBroadcastData",       s_sequenceTag,    false,   TcapXApplication::Sequence,   s_voiceBroadcastDataSeq},
    {"",                         s_noTag,          false,   TcapXApplication::None,       0},
};

static const TokenDict s_additionalSubscriptions[] = {
    {"privilegedUplinkRequest",  0x01},
    {"emergencyUplinkRequest",   0x02},
    {"emergencyReset",           0x04},
    {0,0},
};

static const Parameter s_voiceGroupCallDataSeq[] = {
    {"groupid",                  s_hexTag,         false,   TcapXApplication::TBCD,          0},
    {"extensionContainer",       s_sequenceTag,    true,    TcapXApplication::HexString,     0},
    {"additionalSubscriptions",  s_bitsTag,        true,    TcapXApplication::BitString,     s_additionalSubscriptions},
    // additionalInfo is BitString, but no definition was found in 3GPP TS 43.068, leaved as hex string
    {"additionalInfo",           s_ctxtPrim_0_Tag, true,    TcapXApplication::HexString,     0},
    {"longGroupId",              s_ctxtPrim_1_Tag, true,    TcapXApplication::TBCD,          0},
    {"",                         s_noTag,          false,   TcapXApplication::None,          0},
};

static const Parameter s_voiceGroupCallData[] = {
    {"voiceGroupCallData",       s_sequenceTag,    false,   TcapXApplication::Sequence,      s_voiceGroupCallDataSeq},
    {"",                         s_noTag,          false,   TcapXApplication::None,          0},
};

static const TokenDict s_OBcsmTriggerDetectionPoint[] = {
    {"collectedInfo",      2},
    {"routeSelectFailure", 4},
    {0,0},
};

static const TokenDict s_defaultCallHandling[] = {
    {"continueCall",    0},
    {"releaseCall",     1},
    {0,0},
};

static const Parameter s_OBcsmCamelTDPDataSeq[] = {
    {"o-BcsmTriggerDetectionPoint",  s_enumTag,        false,  TcapXApplication::Enumerated,    s_OBcsmTriggerDetectionPoint},
    {"serviceKey",                   s_intTag,         false,  TcapXApplication::Integer,       0},
    {"gsmSCF-Address",               s_ctxtPrim_0_Tag, false,  TcapXApplication::AddressString, 0},
    {"defaultCallHandling",          s_ctxtPrim_1_Tag, false,  TcapXApplication::Enumerated,    s_defaultCallHandling},
    {"extensionContainer",           s_ctxtCstr_2_Tag, true,   TcapXApplication::HexString,     0},
    {"",                             s_noTag,          false,  TcapXApplication::None,          0},
};

static const Parameter s_OBcsmCamelTDPData[] = {
    {"o-BcsmCamelTDPData",  s_sequenceTag,    false,  TcapXApplication::Sequence,      s_OBcsmCamelTDPDataSeq},
    {"",                    s_noTag,          false,  TcapXApplication::None,          0},
};

static const TokenDict s_camelCapabilityHandling[] = {
    {"phase1",      1},
    {"phase2",      2},
    {"phase3",      3},
    {"phase4",      4},
    {0,0},
};

static const Parameter s_OCSI[] = {
    {"o-BcsmCamelTDPDataList",   s_sequenceTag,    false,   TcapXApplication::SequenceOf, s_OBcsmCamelTDPData},
    {"extensionContainer",       s_sequenceTag,    true,    TcapXApplication::HexString,  0},
    {"camelCapabilityHandling",  s_ctxtPrim_0_Tag, true,    TcapXApplication::Enumerated, s_camelCapabilityHandling},
    {"notificationToCSE",        s_ctxtPrim_1_Tag, true,    TcapXApplication::Null,       0},
    {"csiActive",                s_ctxtPrim_2_Tag, true,    TcapXApplication::Null,       0},
    {"",                         s_noTag,          false,   TcapXApplication::None,       0},
};

static const Parameter s_ssCode[] = {
    {"ss-Code",                  s_hexTag,   false,    TcapXApplication::Enumerated,    s_SSCode},
    {"",                         s_noTag,    false,    TcapXApplication::None,          0},
};

static const Parameter s_SSCamelData[] = {
    {"ss-EventList",            s_sequenceTag,     false, TcapXApplication::SequenceOf,    s_ssCode},
    {"gsmSCF-Address",          s_hexTag,          false, TcapXApplication::AddressString, 0},
    {"extensionContainer",      s_ctxtCstr_0_Tag,  true,  TcapXApplication::HexString,     0},
    {"",                        s_noTag,           false, TcapXApplication::None,          0},
};

static const Parameter s_SSCSI[] = {
    {"ss-CamelData",             s_sequenceTag,    false,   TcapXApplication::Sequence,   s_SSCamelData},
    {"extensionContainer",       s_sequenceTag,    true,    TcapXApplication::HexString,  0},
    {"notificationToCSE",        s_ctxtPrim_0_Tag, true,    TcapXApplication::Null,       0},
    {"csiActive",                s_ctxtPrim_1_Tag, true,    TcapXApplication::Null,       0},
    {"",                         s_noTag,          false,   TcapXApplication::None,       0},
};

static const TokenDict s_matchType[] = {
    {"inhibiting", 0x00},
    {"enabling",   0x01},
    {0,0xff},
};

static const Parameter s_destinationNumber[] = {
    {"destinationNumber",  s_hexTag,         false, TcapXApplication::AddressString, 0},
    {"",                   s_noTag,          false, TcapXApplication::None,          0},
};

static const Parameter s_destinationNumberLength[] = {
    {"destinationNumberLength",  s_intTag,         false, TcapXApplication::Integer,       0},
    {"",                         s_noTag,          false, TcapXApplication::None,          0},
};

static const Parameter s_destinationNumberCriteria[] = {
// TS 100 974 v7.15.0 page 306
    {"matchType",                   s_ctxtPrim_0_Tag,  false,   TcapXApplication::Enumerated, s_matchType},
    {"destinationNumberList",       s_ctxtCstr_1_Tag,  true,    TcapXApplication::SequenceOf, s_destinationNumber},
    {"destinationNumberLengthList", s_ctxtCstr_2_Tag,  true,    TcapXApplication::SequenceOf, s_destinationNumberLength},
    {"",                            s_noTag,           false,   TcapXApplication::None,       0},
};

static const TokenDict s_callTypeCriteria[] = {
// TS 100 974 v7.15.0 page 307
    {"forwarded",        0x00},
    {"notForwarded",     0x01},
    {0,0xff},
};

static const Parameter s_causeValue[]={
    {"causeValue",  s_hexTag,    false,  TcapXApplication::HexString,   0},
    {"",            s_noTag,     false,  TcapXApplication::None,        0},
};

static const Parameter s_OBcsmCamelTDPCriteriaSeq[] = {
    {"o-BcsmTriggerDetectionPoint",  s_enumTag,        false,  TcapXApplication::Enumerated,    s_OBcsmTriggerDetectionPoint},
    {"destinationNumberCriteria",    s_ctxtCstr_0_Tag, true,   TcapXApplication::Sequence,      s_destinationNumberCriteria},
    {"basicServiceCriteria",         s_ctxtCstr_1_Tag, true,   TcapXApplication::SequenceOf,    s_basicServiceCodeType},
    {"callTypeCriteria",             s_ctxtPrim_2_Tag, true,   TcapXApplication::Enumerated,    s_callTypeCriteria},
    {"o-CauseValueCriteria",         s_ctxtCstr_3_Tag, true,   TcapXApplication::SequenceOf,    s_causeValue},
    {"extensionContainer",           s_ctxtCstr_4_Tag, true,   TcapXApplication::HexString,     0},
    {"",                             s_noTag,          false,  TcapXApplication::None,          0},
};

static const Parameter s_OBcsmCamelTDPCriteria[] = {
    {"o-BcsmCamelTDP-Criteria",      s_sequenceTag,    false,  TcapXApplication::Sequence,      s_OBcsmCamelTDPCriteriaSeq},
    {"",                             s_noTag,          false,  TcapXApplication::None,          0},
};

static const TokenDict s_MMCodeValues[] = {
    {"Location-update-in-same-VLR",                                0x00}, //    MM-Code ::= '00000000'B
    {"Location-update-to-other-VLR",                               0x01}, //    MM-Code ::= '00000001'B
    {"IMSI-Attach",                                                0x02}, //    MM-Code ::= '00000010'B
    {"MS-initiated-IMSI-Detach",                                   0x03}, //    MM-Code ::= '00000011'B
    {"Network-initiated-IMSI-Detach",                              0x04}, //    MM-Code ::= '00000100'B
    {"Routeing-Area-update-in-same-SGSN",                          0x80}, //    MM-Code ::= '10000000'B
    {"Routeing-Area-update-to-other-SGSN-update-from-new-SGSN",    0x81}, //    MM-Code ::= '10000001'B
    {"Routeing-Area-update-to-other-SGSN-disconnect-by-detach",    0x82}, //    MM-Code ::= '10000010'B
    {"GPRS-Attach",                                                0x83}, //    MM-Code ::= '10000011'B
    {"MS-initiated-GPRS-Detach",                                   0x84}, //    MM-Code ::= '10000100'B
    {"Network-initiated-GPRS-Detach",                              0x85}, //    MM-Code ::= '10000101'B
    {"Network-initiated-transfer-to-MS-not-reachable-for-paging",  0x86}, //    MM-Code ::= '10000110'B
    {0,0},
};

static const Parameter s_MMCode[] = {
    {"MM-Code",      s_hexTag,    false,  TcapXApplication::Enumerated, s_MMCodeValues},
    {"",             s_noTag,     false,  TcapXApplication::None,       0},
};

static const Parameter s_M_CSI[] = {
    {"mobilityTriggers",   s_sequenceTag,    false, TcapXApplication::SequenceOf,     s_MMCode},
    {"serviceKey",         s_intTag,         false, TcapXApplication::Integer,       0},
    {"gsmSCF-Address",     s_ctxtPrim_0_Tag, false, TcapXApplication::AddressString, 0},
    {"extensionContainer", s_ctxtCstr_1_Tag, true,  TcapXApplication::HexString,     0},
    {"notificationToCSE",  s_ctxtPrim_2_Tag, true,  TcapXApplication::Null,          0},
    {"csiActive",          s_ctxtPrim_3_Tag, true,  TcapXApplication::Null,          0},
    {"",                   s_noTag,          false, TcapXApplication::None,          0},
};

static const TokenDict s_SMSTriggerDetectionPoint[] = {
    {"sms-CollectedInfo",    1},
    {"sms-DeliveryRequest",  2},
    {0, 0},
};

static const TokenDict s_defaultSMSHandling[] = {
    {"continueTransaction",    0},
    {"releaseTransaction",     1},
    {0, 0},
};

static const Parameter s_SMS_CAMEL_TDPDataSeq[] = {
    {"sms-TriggerDetectionPoint",  s_ctxtPrim_0_Tag, false, TcapXApplication::Enumerated,    s_SMSTriggerDetectionPoint},
    {"serviceKey",                 s_ctxtPrim_1_Tag, false, TcapXApplication::Integer,       0},
    {"gsmSCF-Address",             s_ctxtPrim_2_Tag, false, TcapXApplication::AddressString, 0},
    {"defaultSMS-Handling",        s_ctxtPrim_3_Tag, false, TcapXApplication::Enumerated,    s_defaultSMSHandling},
    {"extensionContainer",         s_ctxtCstr_4_Tag, true,  TcapXApplication::HexString,     0},
    {"",                           s_noTag,          false, TcapXApplication::None,          0},
};

static const Parameter s_SMS_CAMEL_TDPData[] = {
    {"sms-CAMEL-TDP-Data",      s_sequenceTag,    false,  TcapXApplication::Sequence,      s_SMS_CAMEL_TDPDataSeq},
    {"",                              s_noTag,    false,  TcapXApplication::None,          0},
};

static const Parameter s_SMS_CSI[] = {
    {"sms-CAMEL-TDP-DataList",  s_ctxtCstr_0_Tag,   true,   TcapXApplication::SequenceOf,   s_SMS_CAMEL_TDPData},
    {"camelCapabilityHandling", s_ctxtPrim_1_Tag,   true,   TcapXApplication::Enumerated,   s_camelCapabilityHandling},
    {"extensionContainer",      s_ctxtCstr_2_Tag,   true,   TcapXApplication::HexString,    0},
    {"notificationToCSE",       s_ctxtPrim_3_Tag,   true,   TcapXApplication::Null,         0},
    {"csiActive",               s_ctxtPrim_4_Tag,   true,   TcapXApplication::Null,         0},
    {"",                        s_noTag,            false,  TcapXApplication::None,         0},
};

static const TokenDict s_TBcsmTriggerDetectionPoint[] = {
    {"termAttemptAuthorized", 12},
    {"tBusy",                 13},
    {"tNoAnswer",             14},
    {0, 0},
};

static const Parameter s_TBcsmCamelTDPDataSeq[] = {
    {"t-BcsmTriggerDetectionPoint",  s_enumTag,        false,  TcapXApplication::Enumerated,    s_TBcsmTriggerDetectionPoint},
    {"serviceKey",                   s_intTag,         false,  TcapXApplication::Integer,       0},
    {"gsmSCF-Address",               s_ctxtPrim_0_Tag, false,  TcapXApplication::AddressString, 0},
    {"defaultCallHandling",          s_ctxtPrim_1_Tag, false,  TcapXApplication::Enumerated,    s_defaultCallHandling},
    {"extensionContainer",           s_ctxtCstr_2_Tag, true,   TcapXApplication::HexString,     0},
    {"",                             s_noTag,          false,  TcapXApplication::None,          0},
};

static const Parameter s_TBcsmCamelTDPData[] = {
    {"t-BcsmCamelTDPData",  s_sequenceTag,   true,   TcapXApplication::Sequence,   s_TBcsmCamelTDPDataSeq},
    {"",                    s_noTag,         false,  TcapXApplication::None,         0},
};

static const Parameter s_T_CSI[] = {
    {"t-BcsmCamelTDPDataList",  s_sequenceTag,      false,  TcapXApplication::SequenceOf,   s_TBcsmCamelTDPData},
    {"extensionContainer",      s_sequenceTag,      true,   TcapXApplication::HexString,    0},
    {"camelCapabilityHandling", s_ctxtPrim_0_Tag,   true,   TcapXApplication::Enumerated,   s_camelCapabilityHandling},
    {"notificationToCSE",       s_ctxtPrim_1_Tag,   true,   TcapXApplication::Null,         0},
    {"csiActive",               s_ctxtPrim_2_Tag,   true,   TcapXApplication::Null,         0},
    {"",                        s_noTag,            false,  TcapXApplication::None,         0},
};

static const Parameter s_T_BCSM_CAMEL_TDPCriteriaSeq[] = {
    {"t-BCSM-TriggerDetectionPoint",  s_enumTag,        false,  TcapXApplication::Enumerated,    s_TBcsmTriggerDetectionPoint},
    {"basicServiceCriteria",          s_ctxtCstr_0_Tag, true,   TcapXApplication::SequenceOf,    s_basicServiceCodeType},
    {"t-CauseValueCriteria",          s_ctxtCstr_1_Tag, true,   TcapXApplication::SequenceOf,    s_causeValue},
    {"",                              s_noTag,          false,  TcapXApplication::None,          0},
};

static const Parameter s_T_BCSM_CAMEL_TDPCriteria[] = {
    {"t-BCSM-CAMEL-TDP-Criteria",  s_sequenceTag,      false,  TcapXApplication::SequenceOf,   s_T_BCSM_CAMEL_TDPCriteriaSeq},
    {"",                           s_noTag,            false,  TcapXApplication::None,         0},
};

static const Parameter s_DP_AnalysedInfoCriteriumSeq[] = {
    {"dialledNumber",              s_hexTag,      false, TcapXApplication::HexString,     0},
    {"serviceKey",                 s_intTag,      false, TcapXApplication::Integer,       0},
    {"gsmSCF-Address",             s_hexTag,      false, TcapXApplication::AddressString, 0},
    {"defaultCallHandling",        s_enumTag,     false, TcapXApplication::Enumerated,    s_defaultCallHandling},
    {"extensionContainer",         s_sequenceTag, true,  TcapXApplication::HexString,     0},
    {"",                           s_noTag,       false, TcapXApplication::None,          0},
};

static const Parameter s_DP_AnalysedInfoCriterium[] = {
    {"DP-AnalysedInfoCriterium",  s_sequenceTag,      false,  TcapXApplication::SequenceOf,   s_DP_AnalysedInfoCriteriumSeq},
    {"",                          s_noTag,            false,  TcapXApplication::None,         0},
};

static const Parameter s_D_CSI[] = {
    {"dp-AnalysedInfoCriteriaList", s_ctxtCstr_0_Tag,   true,   TcapXApplication::SequenceOf,   s_DP_AnalysedInfoCriterium},
    {"camelCapabilityHandling",     s_ctxtPrim_1_Tag,   true,   TcapXApplication::Enumerated,   s_camelCapabilityHandling},
    {"extensionContainer",          s_ctxtCstr_2_Tag,   true,   TcapXApplication::HexString,    0},
    {"notificationToCSE",           s_ctxtPrim_3_Tag,   true,   TcapXApplication::Null,         0},
    {"csiActive",                   s_ctxtPrim_4_Tag,   true,   TcapXApplication::Null,         0},
    {"",                            s_noTag,            false,  TcapXApplication::None,         0},
};

static const TokenDict s_MT_SMS_TPDUTypeEnum[] = {
    {"sms-DELIVER",         0},
    {"sms-SUBMIT-REPORT",   1},
    {"sms-STATUS-REPORT",   2},
    {0, 0},
};

static const Parameter s_MT_SMS_TPDUType[] = {
    {"MT-SMS-TPDU-Type",  s_enumTag,         false, TcapXApplication::Enumerated,  s_MT_SMS_TPDUTypeEnum},
    {"",                  s_noTag,           false, TcapXApplication::None,        0},
};

static const Parameter s_MT_smsCAMELTDP_CriteriaSeq[] = {
    {"sms-TriggerDetectionPoint",  s_enumTag,         false, TcapXApplication::Enumerated,  s_SMSTriggerDetectionPoint},
    {"tpdu-TypeCriterion",         s_ctxtCstr_0_Tag,  true,  TcapXApplication::SequenceOf,  s_MT_SMS_TPDUType},
    {"",                           s_noTag,           false, TcapXApplication::None,        0},
};

static const Parameter s_MT_smsCAMELTDP_Criteria[] = {
    {"MT-smsCAMELTDP-Criteria", s_sequenceTag,   false,  TcapXApplication::Sequence,   s_MT_smsCAMELTDP_CriteriaSeq},
    {"",                        s_noTag,         false,  TcapXApplication::None,       0},
};

static const Parameter s_VlrCamelSubscriptionInfo[] = {
    {"o-CSI",                        s_ctxtCstr_0_Tag,    true,    TcapXApplication::Sequence,   s_OCSI},
    {"extensionContainer",           s_ctxtCstr_1_Tag,    true,    TcapXApplication::HexString,  0},
    {"ss-CSI",                       s_ctxtCstr_2_Tag,    true,    TcapXApplication::Sequence,   s_SSCSI},
    {"o-BcsmCamelTDP-CriteriaList",  s_ctxtCstr_4_Tag,    true,    TcapXApplication::SequenceOf, s_OBcsmCamelTDPCriteria},
    {"tif-CSI",                      s_ctxtPrim_3_Tag,    true,    TcapXApplication::Null,       0},
    {"m-CSI",                        s_ctxtCstr_5_Tag,    true,    TcapXApplication::Sequence,   s_M_CSI},
    {"mo-sms-CSI",                   s_ctxtCstr_6_Tag,    true,    TcapXApplication::Sequence,   s_SMS_CSI},
    {"vt-CSI",                       s_ctxtCstr_7_Tag,    true,    TcapXApplication::Sequence,   s_T_CSI},
    {"t-BCSM-CAMEL-TDP-CriteriaList",s_ctxtCstr_8_Tag,    true,    TcapXApplication::SequenceOf, s_T_BCSM_CAMEL_TDPCriteria},
    {"d-CSI",                        s_ctxtCstr_9_Tag,    true,    TcapXApplication::Sequence,   s_D_CSI},
    {"mt-sms-CSI",                   s_ctxtCstr_10_Tag,   true,    TcapXApplication::Sequence,   s_SMS_CSI},
    {"mt-smsCAMELTDP-CriteriaList",  s_ctxtCstr_11_Tag,   true,    TcapXApplication::SequenceOf, s_MT_smsCAMELTDP_Criteria},
    {"",                             s_noTag,             false,   TcapXApplication::None,       0},
};

static const Parameter s_naeaPreferredCI[] = {
// NAEAPreferredICI
    {"naea-PreferredCIC",  s_ctxtPrim_0_Tag,   false,   TcapXApplication::TBCD,         0},
    {"extensionContainer", s_ctxtCstr_1_Tag,   true,    TcapXApplication::HexString,    0},
    {"",                   s_noTag,            false,   TcapXApplication::None,         0},
};

static const Parameter s_PDPContextSeq[] = {
    {"pdp-ContextId",                s_intTag,            false, TcapXApplication::Integer,      0},
    {"pdp-Type",                     s_ctxtPrim_16_Tag,   false, TcapXApplication::HexString,    0},
    {"pdp-Address",                  s_ctxtPrim_17_Tag,   true,  TcapXApplication::HexString,    0},
    {"qos-Subscribed",               s_ctxtPrim_18_Tag,   false, TcapXApplication::HexString,    0},
    {"vplmnAddressAllowed",          s_ctxtPrim_19_Tag,   true,  TcapXApplication::Null,         0},
    {"apn",                          s_ctxtPrim_20_Tag,   false, TcapXApplication::HexString,    0},
    {"extensionContainer",           s_ctxtCstr_21_Tag,   true,  TcapXApplication::HexString,    0},
    {"ext-QoS-Subscribed",           s_ctxtPrim_0_Tag,    true,  TcapXApplication::HexString,    0},
    {"pdp-ChargingCharacteristics",  s_ctxtPrim_1_Tag,    true,  TcapXApplication::HexString,    0},
    {"ext2-QoS-Subscribed",          s_ctxtPrim_2_Tag,    true,  TcapXApplication::HexString,    0},
    {"ext3-QoS-Subscribed",          s_ctxtPrim_3_Tag,    true,  TcapXApplication::HexString,    0},
    {"ext4-QoS-Subscribed",          s_ctxtPrim_4_Tag,    true,  TcapXApplication::HexString,    0},
    {"apn-oi-Replacement",           s_ctxtPrim_5_Tag,    true,  TcapXApplication::HexString,    0},
    {"ext-pdp-Type",                 s_ctxtPrim_6_Tag,    true,  TcapXApplication::HexString,    0},
    {"ext-pdp-Address",              s_ctxtPrim_7_Tag,    true,  TcapXApplication::HexString,    0},
    {"",                             s_noTag,             false, TcapXApplication::None,         0},
};

static const Parameter s_PDPContext[] = {
    {"pdp-Context",  s_sequenceTag,   false,   TcapXApplication::Sequence,     s_PDPContextSeq},
    {"",                   s_noTag,   false,   TcapXApplication::None,         0},
};

static const Parameter s_GPRSSubscriptionData[] = {
    {"completeDataListIncluded", s_nullTag,        true,  TcapXApplication::Null,       0},
    {"gprsDataList",             s_ctxtCstr_1_Tag, false, TcapXApplication::SequenceOf, s_PDPContext},
    {"extensionContainer",       s_ctxtCstr_2_Tag, true,  TcapXApplication::HexString,  0},
    {"apn-oi-Replacement",       s_ctxtPrim_3_Tag, true,  TcapXApplication::HexString,  0},
    {"",                         s_noTag,          false, TcapXApplication::None,       0},
};

static const TokenDict s_networkAccessMode[] = {
    {"bothMSCAndSGSN",    0x00},
    {"onlyMSC",           0x01},
    {"onlySGSN",          0x02},
    {0,0xff},
};

static const TokenDict s_lsaOnlyAccessIndicator[] = {
    {"accessOutsideLSAsAllowed",     0x00},
    {"accessOutsideLSAsRestricted",  0x01},
    {0,0xff},
};

static const Parameter s_LSADataSeq[] = {
    {"lsaIdentity",            s_ctxtPrim_0_Tag, false, TcapXApplication::HexString,   0},
    {"lsaAttributes",          s_ctxtPrim_1_Tag, false, TcapXApplication::HexString,   0},
    {"lsaActiveModeIndicator", s_ctxtPrim_2_Tag, true,  TcapXApplication::Null,        0},
    {"extensionContainer",     s_ctxtCstr_3_Tag, true,  TcapXApplication::HexString,   0},
    {"",                       s_noTag,          false, TcapXApplication::None,        0},
};

static const Parameter s_LSAData[] = {
    {"lsaData", s_sequenceTag,  false,  TcapXApplication::Sequence,   s_LSADataSeq},
    {"",        s_noTag,        false,  TcapXApplication::None,       0},
};

static const Parameter s_LSAInformation[] = {
    {"completeDataListIncluded", s_nullTag,         true, TcapXApplication::Null,        0},
    {"lsaOnlyAccessIndicator",   s_ctxtPrim_1_Tag,  true, TcapXApplication::Enumerated,  s_lsaOnlyAccessIndicator},
    {"lsaDataList",              s_ctxtCstr_2_Tag,  true, TcapXApplication::SequenceOf,  s_LSAData},
    {"extensionContainer",       s_ctxtCstr_3_Tag,  true, TcapXApplication::HexString,   0},
    {"",                         s_noTag,           false,TcapXApplication::None,        0},
};

static const Parameter s_GMLC[] = {
    {"gmlc",  s_hexTag, false,    TcapXApplication::AddressString, 0},
    {"",      s_noTag,  false,    TcapXApplication::None,          0},
};

static const TokenDict s_notificationToMSUser[] = {
    {"notifyLocationAllowed",                           0x00},
    {"notifyAndVerify-LocationAllowedIfNoResponse",     0x01},
    {"notifyAndVerify-LocationNotAllowedIfNoResponse",  0x02},
    {"locationNotAllowed",                              0x03},
    {0,0},
};

static const TokenDict s_GMLCRestriction[] = {
    {"gmlc-List",       0x00},
    {"home-Country",    0x01},
    {0,0xff},
};

static const Parameter s_LCSClientExternalIDSeq[] = {
    {"externalAddress",      s_ctxtPrim_0_Tag, true,  TcapXApplication::AddressString, 0},
    {"extensionContainer",   s_ctxtCstr_1_Tag, true,  TcapXApplication::HexString,     0},
    {"",                     s_noTag,          false, TcapXApplication::None,          0},
};

static const Parameter s_externalClientSeq[] = {
    {"clientIdentity",       s_sequenceTag,    false,  TcapXApplication::Sequence,   s_LCSClientExternalIDSeq},
    {"gmlc-Restriction",     s_ctxtPrim_0_Tag, true,   TcapXApplication::Enumerated, s_GMLCRestriction},
    {"notificationToMSUser", s_ctxtPrim_1_Tag, true,   TcapXApplication::Enumerated, s_notificationToMSUser},
    {"extensionContainer",   s_ctxtCstr_2_Tag, true,   TcapXApplication::HexString,  0},
    {"",                     s_noTag,          false,  TcapXApplication::None,       0},
};

static const Parameter s_externalClient[] = {
    {"externalClient",  s_sequenceTag, false,    TcapXApplication::Sequence, s_externalClientSeq},
    {"",                s_noTag,       false,    TcapXApplication::None,          0},
};

static const TokenDict s_LCSClientInternalIDEnum[] = {
    {"broadcastService",             0x00},
    {"o-andM-HPLMN",                 0x01},
    {"o-andM-VPLMN",                 0x02},
    {"anonymousLocation",            0x03},
    {"targetMSsubscribedService",    0x04},
    {0,0xff},
};


static const Parameter s_LCSClientInternalID[] = {
    {"lcsClientInternalID",  s_enumTag, false,    TcapXApplication::Enumerated, s_LCSClientInternalIDEnum},
    {"",                     s_noTag,   false,    TcapXApplication::None,       0},
};

static const Parameter s_serviceTypeSeq[] = {
    {"serviceTypeIdentity",    s_intTag,         false,TcapXApplication::Integer,    0},
    {"gmlc-Restriction",       s_ctxtPrim_0_Tag, true, TcapXApplication::Enumerated, s_GMLCRestriction},
    {"notificationToMSUser",   s_ctxtPrim_1_Tag, true, TcapXApplication::Enumerated, s_notificationToMSUser},
    {"extensionContainer",     s_ctxtCstr_2_Tag, true, TcapXApplication::HexString,  0},
    {"",                       s_noTag,          false,TcapXApplication::None,       0},
};

static const Parameter s_serviceType[] = {
    {"serviceType",  s_sequenceTag, false,    TcapXApplication::Sequence, s_serviceTypeSeq},
    {"",             s_noTag,       false,    TcapXApplication::None,     0},
};

static const Parameter s_LCSPrivacyClassSeq[] = {
    {"ss-Code",                s_hexTag,         false,TcapXApplication::Enumerated, s_SSCode},
    {"ss-Status",              s_hexTag,         false,TcapXApplication::Flags,      s_ssStatus},
    {"notificationToMSUser",   s_ctxtPrim_0_Tag, true, TcapXApplication::Enumerated, s_notificationToMSUser},
    {"externalClientList",     s_ctxtCstr_1_Tag, true, TcapXApplication::SequenceOf, s_externalClient},
    {"plmnClientList",         s_ctxtCstr_2_Tag, true, TcapXApplication::SequenceOf, s_LCSClientInternalID},
    {"extensionContainer",     s_ctxtCstr_3_Tag, true, TcapXApplication::HexString,  0},
    {"ext-externalClientList", s_ctxtCstr_4_Tag, true, TcapXApplication::SequenceOf, s_externalClient},
    {"serviceTypeList",        s_ctxtCstr_5_Tag, true, TcapXApplication::SequenceOf, s_serviceType},
    {"",                       s_noTag,          false,TcapXApplication::None,       0},
};

static const Parameter s_LCSPrivacyException[] = {
    {"lcsPrivacyClass",  s_sequenceTag, false,    TcapXApplication::Sequence, s_LCSPrivacyClassSeq},
    {"",                 s_noTag,       false,    TcapXApplication::None,     0},
};

static const Parameter s_MOLRClassSeq[] = {
    {"ss-Code",              s_hexTag,         false,TcapXApplication::Enumerated, s_SSCode},
    {"ss-Status",            s_hexTag,         false,TcapXApplication::Flags,      s_ssStatus},
    {"extensionContainer",   s_ctxtCstr_0_Tag, true, TcapXApplication::HexString,  0},
    {"",                     s_noTag,          false,TcapXApplication::None,       0},
};

static const Parameter s_MOLRClass[] = {
    {"mOLRClass",  s_sequenceTag, false,    TcapXApplication::Sequence, s_MOLRClassSeq},
    {"",           s_noTag,       false,    TcapXApplication::None,     0},
};

static const Parameter s_LCSInformation[] = {
    {"gmlc-List",                    s_ctxtCstr_0_Tag, true,  TcapXApplication::SequenceOf,  s_GMLC},
    {"lcs-PrivacyExceptionList",     s_ctxtCstr_1_Tag, true,  TcapXApplication::SequenceOf,  s_LCSPrivacyException},
    {"molr-List",                    s_ctxtCstr_2_Tag, true,  TcapXApplication::SequenceOf,  s_MOLRClass},
    {"add-lcs-PrivacyExceptionList", s_ctxtCstr_3_Tag, true,  TcapXApplication::SequenceOf,  s_LCSPrivacyException},
    {"",                             s_noTag,          false, TcapXApplication::None,        0},
};

static const Parameter s_MC_SSInfo[] = {
    {"ss-Code",                 s_ctxtPrim_0_Tag,    false,  TcapXApplication::Enumerated,  s_SSCode},
    {"ss-Status",               s_ctxtPrim_1_Tag,    false,  TcapXApplication::Flags,       s_ssStatus},
    {"nbrSB",                   s_ctxtPrim_2_Tag,    false,  TcapXApplication::Integer,     0},
    {"nbrUser",                 s_ctxtPrim_3_Tag,    false,  TcapXApplication::Integer,     0},
    {"extensionContainer",      s_ctxtCstr_4_Tag,    true,   TcapXApplication::HexString,   0},
    {"",                        s_noTag,          false, TcapXApplication::None,        0},
};

static const TokenDict s_GPRSTriggerDetectionPoint[] = {
    {"attach",                                    1},
    {"attachChangeOfPosition",                    2},
    {"pdp-ContextEstablishment",                 11},
    {"pdp-ContextEstablishmentAcknowledgement",  12},
    {"pdp-ContextChangeOfPosition",              14},
    {0,0},
};

static const TokenDict s_defaultGPRSHandling[] = {
    {"continueTransaction",    0},
    {"releaseTransaction",     1},
    {0,0},
};

static const Parameter s_GPRSCamelTDPDataSeq[] = {
    {"gprs-TriggerDetectionPoint",  s_ctxtPrim_0_Tag, false, TcapXApplication::Enumerated,    s_GPRSTriggerDetectionPoint},
    {"serviceKey",                  s_ctxtPrim_1_Tag, false, TcapXApplication::Integer,       0},
    {"gsmSCF-Address",              s_ctxtPrim_2_Tag, false, TcapXApplication::AddressString, 0},
    {"defaultSessionHandling",      s_ctxtPrim_3_Tag, false, TcapXApplication::Enumerated,    s_defaultGPRSHandling},
    {"extensionContainer",          s_ctxtCstr_4_Tag, true,  TcapXApplication::HexString,     0},
    {"",                            s_noTag,          false, TcapXApplication::None,          0},
};

static const Parameter s_GPRSCamelTDPData[] = {
    {"gprs-CamelTDPData",       s_sequenceTag,    false, TcapXApplication::Sequence,    s_GPRSCamelTDPDataSeq},
    {"",                        s_noTag,          false, TcapXApplication::None,        0},
};

static const Parameter s_GPRS_CSISeq[] = {
    {"gprs-CamelTDPDataList",   s_ctxtCstr_0_Tag, true,  TcapXApplication::SequenceOf,  s_GPRSCamelTDPData},
    {"camelCapabilityHandling", s_ctxtPrim_1_Tag, true,  TcapXApplication::Enumerated,  s_camelCapabilityHandling},
    {"extensionContainer",      s_ctxtCstr_2_Tag, true,  TcapXApplication::HexString,   0},
    {"notificationToCSE",       s_ctxtPrim_3_Tag, true,  TcapXApplication::Null,        0},
    {"csiActive",               s_ctxtPrim_4_Tag, true,  TcapXApplication::Null,        0},
    {"",                        s_noTag,          false, TcapXApplication::None,        0},
};

static const Parameter s_SGSN_CAMELSubscriptionInfoSeq[] = {
    {"gprs-CSI",                    s_ctxtCstr_0_Tag,    true,   TcapXApplication::Sequence,    s_GPRS_CSISeq},
    {"mo-sms-CSI",                  s_ctxtCstr_1_Tag,    true,   TcapXApplication::Sequence,    s_SMS_CSI},
    {"extensionContainer",          s_ctxtCstr_2_Tag,    true,   TcapXApplication::HexString,   0},
    {"mt-sms-CSI",                  s_ctxtCstr_3_Tag,    true,   TcapXApplication::Sequence,    s_SMS_CSI},
    {"mt-smsCAMELTDP-CriteriaList", s_ctxtCstr_4_Tag,    true,   TcapXApplication::SequenceOf,  s_MT_smsCAMELTDP_Criteria},
    {"mg-csi",                      s_ctxtCstr_5_Tag,    true,   TcapXApplication::Sequence,    s_M_CSI},
    {"",                            s_noTag,             false,  TcapXApplication::None,        0},
};

static const TokenDict s_accessRestrictionData[] = {
    {"utranNotAllowed",               0x01},
    {"geranNotAllowed",               0x02},
    {"ganNotAllowed",                 0x03},
    {"i-hspa-evolutionNotAllowed",    0x04},
    {"e-utranNotAllowed",             0x05},
    {"ho-toNon3GPP-AccessNotAllowed", 0x06},
    {0,0},
};

static const Parameter s_AMBRSeq[] = {
    {"max-RequestedBandwidth-UL",   s_ctxtPrim_0_Tag,    false,  TcapXApplication::Integer,     0},
    {"max-RequestedBandwidth-DL",   s_ctxtPrim_1_Tag,    false,  TcapXApplication::Integer,     0},
    {"extensionContainer",          s_ctxtCstr_2_Tag,    true,   TcapXApplication::HexString,   0},
    {"",                            s_noTag,             false,  TcapXApplication::None,        0},
};

static const Parameter s_AllocationRetentionPrioritySeq[] = {
    {"priority-level",                 s_ctxtPrim_0_Tag,    false,  TcapXApplication::Integer,     0},
    {"pre-emption-capability",         s_ctxtPrim_1_Tag,    true,   TcapXApplication::Bool,        0},
    {"pre-emption-vulnerability",      s_ctxtPrim_1_Tag,    true,   TcapXApplication::Bool,        0},
    {"extensionContainer",             s_ctxtCstr_3_Tag,    true,   TcapXApplication::HexString,   0},
    {"",                               s_noTag,             false,  TcapXApplication::None,        0},
};

static const Parameter s_EPS_QoS_SubscribedSeq[] = {
    {"qos-Class-Identifier",           s_ctxtPrim_0_Tag,    false,  TcapXApplication::Integer,     0},
    {"allocation-Retention-Priority",  s_ctxtCstr_1_Tag,    false,  TcapXApplication::Sequence,    s_AllocationRetentionPrioritySeq},
    {"extensionContainer",             s_ctxtCstr_2_Tag,    true,   TcapXApplication::HexString,   0},
    {"",                               s_noTag,             false,  TcapXApplication::None,        0},
};

static const Parameter s_PDN_GW_Identity[] = {
    {"pdn-gw-ipv4-Address",         s_ctxtPrim_0_Tag, true,  TcapXApplication::HexString, 0},
    {"pdn-gw-ipv6-Address",         s_ctxtPrim_1_Tag, true,  TcapXApplication::HexString, 0},
    {"pdn-gw-name",                 s_ctxtPrim_2_Tag, true,  TcapXApplication::HexString, 0},
    {"extensionContainer",          s_ctxtCstr_3_Tag, true,  TcapXApplication::HexString, 0},
    {"",                            s_noTag,          false, TcapXApplication::None,      0},
};

static const TokenDict s_PDN_GW_AllocationType[] = {
    {"static",   0},
    {"dynamic",  1},
    {0,0},
};

static const Parameter s_SpecificAPNInfoSeq[] = {
    {"apn",                   s_ctxtPrim_0_Tag,    false,  TcapXApplication::HexString, 0},
    {"pdn-gw-Identity",       s_ctxtCstr_1_Tag,    false,  TcapXApplication::Sequence,  s_PDN_GW_Identity},
    {"extensionContainer",    s_ctxtCstr_2_Tag,    true,   TcapXApplication::HexString, 0},
    {"",                      s_noTag,             false,  TcapXApplication::None,      0},
};

static const Parameter s_SpecificAPNInfo[] = {
    {"specificAPNInfo",       s_sequenceTag, false,  TcapXApplication::Sequence,  s_SpecificAPNInfoSeq},
    {"",                      s_noTag,       false,  TcapXApplication::None,      0},
};

static const Parameter s_APNConfigurationSeq[] = {
    {"contextId",                    s_ctxtPrim_0_Tag,    false, TcapXApplication::Integer,     0},
    {"pdn-Type",                     s_ctxtPrim_1_Tag,    false, TcapXApplication::HexString,   0},
    {"servedPartyIP-IPv4-Address",   s_ctxtPrim_2_Tag,    true,  TcapXApplication::HexString,   0},
    {"apn",                          s_ctxtPrim_3_Tag,    false, TcapXApplication::HexString,   0},
    {"eps-qos-Subscribed",           s_ctxtCstr_4_Tag,    false, TcapXApplication::Sequence,    s_EPS_QoS_SubscribedSeq},
    {"pdn-gw-Identity",              s_ctxtCstr_5_Tag,    true,  TcapXApplication::Sequence,    s_PDN_GW_Identity},
    {"pdn-gw-AllocationType",        s_ctxtPrim_6_Tag,    true,  TcapXApplication::Enumerated,  s_PDN_GW_AllocationType},
    {"vplmnAddressAllowed",          s_ctxtPrim_7_Tag,    true,  TcapXApplication::Null,        0},
    {"chargingCharacteristics",      s_ctxtPrim_8_Tag,    true,  TcapXApplication::HexString,   0},
    {"ambr",                         s_ctxtCstr_9_Tag,    true,  TcapXApplication::Sequence,    s_AMBRSeq},
    {"specificAPNInfoList",          s_ctxtCstr_10_Tag,   true,  TcapXApplication::SequenceOf,  s_SpecificAPNInfo},
    {"extensionContainer",           s_ctxtCstr_11_Tag,   true,  TcapXApplication::HexString,   0},
    {"servedPartyIP-IPv6-Address",   s_ctxtPrim_12_Tag,   true,  TcapXApplication::HexString,   0},
    {"apn-oi-Replacement",           s_ctxtPrim_13_Tag,   true,  TcapXApplication::HexString,   0},
    {"",                             s_noTag,             false, TcapXApplication::None,        0},
};

static const Parameter s_APNConfiguration[] = {
    {"APN-Configuration",           s_sequenceTag,    false,  TcapXApplication::Sequence,    s_APNConfigurationSeq},
    {"",                            s_noTag,          false,  TcapXApplication::None,        0},
};

static const Parameter s_APN_ConfigurationProfileSeq[] = {
    {"defaultContext",           s_intTag,            false, TcapXApplication::Integer,     0},
    {"completeDataListIncluded", s_nullTag,           true,  TcapXApplication::Null,        0},
    {"epsDataList",              s_ctxtCstr_1_Tag,    false, TcapXApplication::SequenceOf,  s_APNConfiguration},
    {"extensionContainer",       s_ctxtCstr_2_Tag,    true,  TcapXApplication::HexString,   0},
    {"",                         s_noTag,             false, TcapXApplication::None,        0},
};

static const Parameter s_EPS_SubscriptionData[] = {
    {"apn-oi-Replacement",      s_ctxtPrim_0_Tag,    true,   TcapXApplication::HexString,      0},
    {"rfsp-id",                 s_ctxtPrim_2_Tag,    true,   TcapXApplication::Integer,        0},
    {"ambr",                    s_ctxtCstr_3_Tag,    true,   TcapXApplication::Sequence,       s_AMBRSeq},
    {"apn-ConfigurationProfile",s_ctxtCstr_4_Tag,    true,   TcapXApplication::Sequence,       s_APN_ConfigurationProfileSeq},
    {"stn-sr",                  s_ctxtPrim_6_Tag,    true,   TcapXApplication::AddressString,  0},
    {"extensionContainer",      s_ctxtCstr_5_Tag,    true,   TcapXApplication::HexString,      0},
    {"",                        s_noTag,             false,  TcapXApplication::None,           0},
};

static const Parameter s_CSG_SubscriptionDataSeq[] = {
    // should find encoding for csg-ID
    {"csg-Id",                s_bitsTag,        false, TcapXApplication::HexString,      0},
    {"expirationDate",        s_hexTag,         true,  TcapXApplication::HexString,      0},
    {"extensionContainer",    s_sequenceTag,    true,  TcapXApplication::HexString,      0},
    {"",                      s_noTag,          false, TcapXApplication::None,           0},
};

static const Parameter s_CSG_SubscriptionData[] = {
    {"CSG-SubscriptionData",    s_sequenceTag,    false,  TcapXApplication::Sequence,    s_CSG_SubscriptionDataSeq},
    {"",                        s_noTag,          false,  TcapXApplication::None,        0},
};

static const Parameter s_contextId[] = {
// TS 100 974 v7.15.0 page 305
    {"contextId",      s_intTag, false, TcapXApplication::Integer,    0},
    {"",               s_noTag,  false, TcapXApplication::None,       0},
};

static const Parameter s_GPRSSubscriptionDataWithdraw[] = {
// TS 100 974 v7.15.0 page 305
    {"allGPRSData",      s_nullTag,     false, TcapXApplication::Null,       0},
    {"contextIdList",    s_sequenceTag, false, TcapXApplication::SequenceOf, s_contextId},
    {"",                 s_noTag,       false, TcapXApplication::None,       0},
};

static const Parameter s_LSAIdentity[] = {
// TS 100 974 v7.15.0 page 300
    {"lsaIdentity",    s_hexTag, false, TcapXApplication::HexString,  0},
    {"",               s_noTag,  false, TcapXApplication::None,       0},
};

static const Parameter s_LSAInformationWithdraw[] = {
// TS 100 974 v7.15.0 page 305
    {"allLSAData",      s_nullTag,     false, TcapXApplication::Null,       0},
    {"lsaIdentityList", s_sequenceTag, false, TcapXApplication::SequenceOf, s_LSAIdentity},
    {"",                s_noTag,       false, TcapXApplication::None,       0},
};

static const TokenDict s_specificCSI_Withdraw[] = {
    {"o-csi",      0x0001},
    {"ss-csi",     0x0002},
    {"tif-csi",    0x0004},
    {"d-csi",      0x0008},
    {"vt-csi",     0x0010},
    {"mo-sms-csi", 0x0020},
    {"m-csi",      0x0040},
    {"gprs-csi",   0x0080},
    {"t-csi",      0x0100},
    {"mt-sms-csi", 0x0200},
    {"mg-csi",     0x0400},
    {"o-IM-CSI",   0x0800},
    {"d-IM-CSI",   0x1000},
    {"vt-IM-CSI",  0x2000},
    {0,0},
};

static const Parameter s_EPS_SubscriptionDataWithdraw[] = {
    {"allEPS-Data",      s_nullTag,     false, TcapXApplication::Null,       0},
    {"contextIdList",    s_sequenceTag, false, TcapXApplication::SequenceOf, s_contextId},
    {"",                 s_noTag,       false, TcapXApplication::None,       0},
};


static const TokenDict s_regionalSubscriptionResponse[] = {
// TS 100 974 v7.15.0 page 305
    {"networkNode-AreaRestricted", 0},
    {"tooManyZoneCodes",           1},
    {"zoneCodesConflict",          2},
    {"regionalSubscNotSupported",  3},
    {0,0},
};

static const TokenDict s_supportedFeatures[] = {
    {"odb-all-apn",                                                    0x00000001},
    {"odb-HPLMN-APN",                                                  0x00000002},
    {"odb-VPLMN-APN",                                                  0x00000004},
    {"odb-all-og",                                                     0x00000008},
    {"odb-all-international-og",                                       0x00000010},
    {"odb-all-int-og-not-to-HPLMN-country",                            0x00000020},
    {"odb-all-interzonal-og",                                          0x00000040},
    {"odb-all-interzonal-og-not-to-HPLMN-country",                     0x00000080},
    {"odb-all-interzonal-og-and-internat-og-not-to-HPLMN-country",     0x00000100},
    {"regSub",                                                         0x00000200},
    {"trace",                                                          0x00000400},
    {"lcs-all-PrivExcep",                                              0x00000800},
    {"lcs-universal",                                                  0x00001000},
    {"lcs-CallSessionRelated",                                         0x00002000},
    {"lcs-CallSessionUnrelated",                                       0x00004000},
    {"lcs-PLMN-operator",                                              0x00008000},
    {"lcs-ServiceType",                                                0x00010000},
    {"lcs-all-MOLR-SS",                                                0x00020000},
    {"lcs-basicSelfLocation",                                          0x00040000},
    {"lcs-autonomousSelfLocation",                                     0x00080000},
    {"lcs-transferToThirdParty",                                       0x00100000},
    {"sm-mo-pp",                                                       0x00200000},
    {"barring-OutgoingCalls",                                          0x00400000},
    {"baoc",                                                           0x00800000},
    {"boic",                                                           0x01000000},
    {"boicExHC",                                                       0x02000000},
    {0, 0},
};

static const Parameter s_SSForBSCode[] = {
// TS 100 974 v7.15.0 page 319
    {"ss-Code",                   s_hexTag,           false,  TcapXApplication::Enumerated,  s_SSCode},
    {"basicService",              s_noTag,            true,   TcapXApplication::Choice,      s_basicServiceCode},
    {"longFTN-Supported",         s_ctxtPrim_4_Tag,   true,   TcapXApplication::Null,        0},
    {"",                          s_noTag,            false,  TcapXApplication::None,        0},
};

static const Parameter s_ccbsFeature[] = {
// TS 100 974 v7.15.0 page 319
    {"ccbs-Index",             s_ctxtPrim_0_Tag,   true,    TcapXApplication::Integer,             0},
    {"b-subscriberNumber",     s_ctxtPrim_1_Tag,   true,    TcapXApplication::AddressString,       0},
    {"b-subscriberSubaddress", s_ctxtPrim_2_Tag,   true,    TcapXApplication::HexString ,          0},
    {"basicServiceGroup",      s_ctxtCstr_3_Tag,   true,    TcapXApplication::Choice,              s_extBasicServiceCode},
    {"",                       s_noTag,            false,   TcapXApplication::None,                0},
};

static const Parameter s_CCBSFeature[] = {
// TS 100 974 v7.15.0 page 319
    {"CCBS-Feature",              s_sequenceTag,    false,   TcapXApplication::Sequence,  s_ccbsFeature},
    {"",                          s_noTag,          false,   TcapXApplication::None,      0},
};

static const Parameter s_genericServiceInfo[] = {
// TS 100 974 v7.15.0 page 319
    {"ss-Status",               s_hexTag,         false,    TcapXApplication::Flags,         s_ssStatus},
    {"cliRestrictionOption",    s_enumTag,        true,     TcapXApplication::Enumerated,    s_cliRestrictionOption},
    {"maximumentitledPriority", s_ctxtPrim_0_Tag, true,     TcapXApplication::Enumerated,    s_EMLPPPriority},
    {"defaultPriority",         s_ctxtPrim_1_Tag, true,     TcapXApplication::Enumerated,    s_EMLPPPriority},
    {"ccbs-FeatureList",        s_ctxtCstr_2_Tag, true,     TcapXApplication::SequenceOf,    s_CCBSFeature},
    {"nbrSB",                   s_ctxtPrim_3_Tag, true,     TcapXApplication::Integer,       0},
    {"nbrUser",                 s_ctxtPrim_4_Tag, true,     TcapXApplication::Integer,       0},
    {"nbrSN",                   s_ctxtPrim_5_Tag, true,     TcapXApplication::Integer,       0},
    {"",                        s_noTag,          false,    TcapXApplication::None,          0},
};

static const Parameter s_InterrogateSSRes[] = {
// TS 100 974 v7.15.0 page 319
    {"ss-Status",              s_ctxtPrim_0_Tag,   false,    TcapXApplication::Flags,         s_ssStatus},
    {"basicServiceGroupList",  s_ctxtCstr_2_Tag,   false,    TcapXApplication::SequenceOf,    s_basicServiceCodeType},
    {"forwardingFeatureList",  s_ctxtCstr_3_Tag,   false,    TcapXApplication::SequenceOf,    s_forwFeature},
    {"genericServiceInfo",     s_ctxtCstr_4_Tag,   false,    TcapXApplication::Sequence,      s_genericServiceInfo},
    {"",                       s_noTag,            false,    TcapXApplication::None,          0},
};

static const TokenDict s_failureCauseEnum[] = {
// TS 129 002 v9.3.0 page 353
    {"wrongUserResponse",     0x00},
    {"wrongNetworkSignature", 0x01},
    {0,                       0x00},
};

static const TokenDict s_accessTypeEnum[] = {
// TS 129 002 v9.3.0 page 353
    {"call",                  0x00},
    {"emergencyCall",         0x01},
    {"locationUpdating",      0x02},
    {"supplementaryService",  0x03},
    {"shortMessage",          0x04},
    {"gprsAttach",            0x05},
    {"routingAreaUpdating",   0x06},
    {"serviceRequest",        0x07},
    {"pdpContextActivation",  0x08},
    {"pdpContextDeactivation",0x09},
    {"gprsDetach",            0x0a},
    {0,                       0x00},
};

static const TokenDict s_guidanceInfo[] = {
    {"enterPW",           0 },
    {"enterNewPW",        1 },
    {"enterNewPW-Again",  2 },
    {0, 0},
};

static const Parameter s_SGSN_CapabilitySeq[] = {
    {"solsaSupportIndicator",                      s_nullTag,         true, TcapXApplication::Null,       0},
    {"extensionContainer",                         s_ctxtCstr_1_Tag,  true, TcapXApplication::HexString,  0},
    {"superChargerSupportedInServingNetworkEntity",s_ctxtCstr_2_Tag,  true, TcapXApplication::Choice,     s_superChargerInfo},
    {"gprsEnhancementsSupportIndicator",           s_ctxtPrim_3_Tag,  true, TcapXApplication::Null,       0},
    {"supportedCamelPhases",                       s_ctxtPrim_4_Tag,  true, TcapXApplication::BitString,  s_camelPhases},
    {"supportedLCS-CapabilitySets",                s_ctxtPrim_5_Tag,  true, TcapXApplication::BitString,  s_supportedLCSCapabilitySets},
    {"offeredCamel4CSIs",                          s_ctxtPrim_6_Tag,  true, TcapXApplication::BitString,  s_offeredCamel4CSIs},
    {"smsCallBarringSupportIndicator",             s_ctxtPrim_7_Tag,  true, TcapXApplication::Null,       0},
    {"supportedRAT-TypesIndicator",                s_ctxtPrim_8_Tag,  true, TcapXApplication::BitString,  s_supportedRATTypes},
    {"supportedFeatures",                          s_ctxtPrim_9_Tag,  true, TcapXApplication::BitString,  s_supportedFeatures},
    {"t-adsDataRetrieval",                         s_ctxtPrim_10_Tag, true, TcapXApplication::Null,       0},
    {"homogeneousSupportOfIMSVoiceOverPSSessions", s_ctxtPrim_11_Tag, true, TcapXApplication::Bool,       0},
    {"",                                           s_noTag,           false,TcapXApplication::None,       0},
};

static const Parameter s_PDN_GW_UpdateSeq[] = {
    {"apn",               s_ctxtPrim_0_Tag, true, TcapXApplication::HexString, 0},
    {"pdn-gw-Identity",   s_ctxtCstr_1_Tag, true, TcapXApplication::Sequence,  s_PDN_GW_Identity},
    {"contextId",         s_ctxtPrim_2_Tag, true, TcapXApplication::Integer,   0},
    {"extensionContainer",s_ctxtCstr_3_Tag, true, TcapXApplication::HexString, 0},
    {"",                  s_noTag,          false,TcapXApplication::None,      0},
};

static const TokenDict s_ISR_Information[] = {
    {"updateMME",               0x01},
    {"cancelSGSN",              0x02},
    {"initialAttachIndicator",  0x04},
    {0, 0},
};

static const Parameter s_EPS_InfoChoice[] = {
    {"pdn-gw-update",      s_ctxtCstr_0_Tag, false, TcapXApplication::Sequence,  s_PDN_GW_UpdateSeq},
    {"isr-Information",    s_ctxtPrim_1_Tag, false, TcapXApplication::BitString, s_ISR_Information},
    {"",                   s_noTag,          false, TcapXApplication::None,      0},
};

static const TokenDict s_used_RAT_Type[] = {
    {"utran",            0},
    {"geran",            1},
    {"gan",              2},
    {"i-hspa-evolution", 3},
    {"e-utran",          4},
    {0, 0},
};

static const Parameter s_hlrId[] = {
    {"HLR-Id",            s_hexTag,   false,   TcapXApplication::TBCD,       0},
    {"",                  s_noTag,    false,   TcapXApplication::None,       0},
};

static const Parameter s_additionalNumber[] = {
    {"msc-Number",  s_ctxtPrim_0_Tag, false,  TcapXApplication::AddressString, 0},
    {"sgsn-Number", s_ctxtPrim_1_Tag, false,  TcapXApplication::AddressString, 0},
    {"",            s_noTag,          false,  TcapXApplication::None,          0},
};

static const TokenDict s_SM_DeliveryNotIntended[] = {
    {"onlyIMSI-requested",    0},
    {"onlyMCC-MNC-requested", 1},
    {0, 0},
};

static const Parameter s_locationInfoWithLMSI[] = {
// TS 100 974 v7.15.0 page 324
    {"networkNode-Number",  s_ctxtPrim_1_Tag,  false,  TcapXApplication::AddressString, 0},
    {"lmsi",                s_hexTag,          true,   TcapXApplication::HexString,     0},
    {"extensionContainer",  s_sequenceTag,     true,   TcapXApplication::HexString,     0},
    {"gprsNodeIndicator",   s_ctxtPrim_5_Tag,  true,   TcapXApplication::Null,          0},
    {"additional-Number",   s_ctxtCstr_6_Tag,  true,   TcapXApplication::Choice,        s_additionalNumber},
    {"",                    s_noTag,           false,  TcapXApplication::None,          0},
};

static const TokenDict s_traceDepth[] = {
    {"minimum", 0},
    {"medium",  1},
    {"maximum", 2},
    {0, 0},
};

static const Parameter s_traceDepthListSeq[] = {
    {"msc-s-TraceDepth",  s_ctxtPrim_0_Tag,  true,  TcapXApplication::Enumerated,  s_traceDepth},
    {"mgw-TraceDepth",    s_ctxtPrim_1_Tag,  true,  TcapXApplication::Enumerated,  s_traceDepth},
    {"sgsn-TraceDepth",   s_ctxtPrim_2_Tag,  true,  TcapXApplication::Enumerated,  s_traceDepth},
    {"ggsn-TraceDepth",   s_ctxtPrim_3_Tag,  true,  TcapXApplication::Enumerated,  s_traceDepth},
    {"rnc-TraceDepth",    s_ctxtPrim_4_Tag,  true,  TcapXApplication::Enumerated,  s_traceDepth},
    {"bmsc-TraceDepth",   s_ctxtPrim_5_Tag,  true,  TcapXApplication::Enumerated,  s_traceDepth},
    {"mme-TraceDepth",    s_ctxtPrim_6_Tag,  true,  TcapXApplication::Enumerated,  s_traceDepth},
    {"sgw-TraceDepth",    s_ctxtPrim_7_Tag,  true,  TcapXApplication::Enumerated,  s_traceDepth},
    {"pgw-TraceDepth",    s_ctxtPrim_8_Tag,  true,  TcapXApplication::Enumerated,  s_traceDepth},
    {"eNB-TraceDepth",    s_ctxtPrim_9_Tag,  true,  TcapXApplication::Enumerated,  s_traceDepth},
    {"",                  s_noTag,           false, TcapXApplication::None,        0},
};

static const TokenDict s_traceNETypeList[] = {
    {"msc-s",0x0001},
    {"mgw",  0x0002},
    {"sgsn", 0x0004},
    {"ggsn", 0x0008},
    {"rnc",  0x0010},
    {"bm-sc",0x0020},
    {"mme",  0x0040},
    {"sgw",  0x0080},
    {"pgw",  0x0100},
    {"eNB",  0x0200},
    {0, 0},
};

static const TokenDict s_MSC_S_InterfaceList[] = {
    {"a",      0x0001},
    {"iu",     0x0002},
    {"mc",     0x0004},
    {"map-g",  0x0008},
    {"map-b",  0x0010},
    {"map-e",  0x0020},
    {"map-f",  0x0040},
    {"cap",    0x0080},
    {"map-d",  0x0100},
    {"map-c",  0x0200},
    {0, 0},
};

static const TokenDict s_MGW_InterfaceList[] = {
    {"mc",        0x01},
    {"nb-up",     0x02},
    {"iu-up",     0x04},
    {0, 0},
};

static const TokenDict s_SGSN_InterfaceList[] = {
    {"gb",      0x0001},
    {"iu",      0x0002},
    {"gn",      0x0004},
    {"map-gr",  0x0008},
    {"map-gd",  0x0010},
    {"map-gf",  0x0020},
    {"gs",      0x0040},
    {"ge",      0x0080},
    {"s3",      0x0100},
    {"s4",      0x0200},
    {"s6d",     0x0400},
    {0, 0},
};

static const TokenDict s_GGSN_InterfaceList[] = {
    {"gn",        0x01},
    {"gi",        0x02},
    {"gmb",       0x04},
    {0, 0},
};

static const TokenDict s_RNC_InterfaceList[] = {
    {"iu",       0x01},
    {"iur",      0x02},
    {"iub",      0x04},
    {"uu",       0x08},
    {0, 0},
};

static const TokenDict s_BMSC_InterfaceList[] = {
    {"gmb",       0x01},
    {0, 0},
};

static const TokenDict s_MME_InterfaceList[] = {
    {"s1-mme",    0x01},
    {"s3",        0x02},
    {"s6a",       0x04},
    {"s10",       0x08},
    {"s11",       0x10},
    {0, 0},
};

static const TokenDict s_SGW_InterfaceList[] = {
    {"s4",        0x01},
    {"s5",        0x02},
    {"s8b",       0x04},
    {"s11",       0x08},
    {"gxc",       0x10},
    {0, 0},
};

static const TokenDict s_PGW_InterfaceList[] = {
    {"s2a", 0x01},
    {"s2b", 0x02},
    {"s2c", 0x04},
    {"s5",  0x08},
    {"s6b", 0x10},
    {"gx",  0x20},
    {"s8b", 0x40},
    {"sgi", 0x80},
    {0, 0},
};

static const TokenDict s_ENB_InterfaceList[] = {
    {"s1-mme",   0x01},
    {"x2",       0x02},
    {"uu",       0x04},
    {0, 0},
};

static const Parameter s_traceInterfaceListSeq[] = {
    {"msc-s-List",  s_ctxtPrim_0_Tag,  true,  TcapXApplication::BitString,  s_MSC_S_InterfaceList},
    {"mgw-List",    s_ctxtPrim_1_Tag,  true,  TcapXApplication::BitString,  s_MGW_InterfaceList},
    {"sgsn-List",   s_ctxtPrim_2_Tag,  true,  TcapXApplication::BitString,  s_SGSN_InterfaceList},
    {"ggsn-List",   s_ctxtPrim_3_Tag,  true,  TcapXApplication::BitString,  s_GGSN_InterfaceList},
    {"rnc-List",    s_ctxtPrim_4_Tag,  true,  TcapXApplication::BitString,  s_RNC_InterfaceList},
    {"bmsc-List",   s_ctxtPrim_5_Tag,  true,  TcapXApplication::BitString,  s_BMSC_InterfaceList},
    {"mme-List",    s_ctxtPrim_6_Tag,  true,  TcapXApplication::BitString,  s_MME_InterfaceList},
    {"sgw-List",    s_ctxtPrim_7_Tag,  true,  TcapXApplication::BitString,  s_SGW_InterfaceList},
    {"pgw-List",    s_ctxtPrim_8_Tag,  true,  TcapXApplication::BitString,  s_PGW_InterfaceList},
    {"eNB-List",    s_ctxtPrim_9_Tag,  true,  TcapXApplication::BitString,  s_ENB_InterfaceList},
    {"",            s_noTag,           false, TcapXApplication::None,       0},
};

static const TokenDict s_MSC_S_EventList[] = {
    {"mo-mtCall",                 0x01},
    {"mo-mt-sms",                 0x02},
    {"lu-imsiAttach-imsiDetach",  0x04},
    {"handovers",                 0x08},
    {"ss",                        0x10},
    {0, 0},
};

static const TokenDict s_MGW_EventList[] = {
    {"context",                 0x01},
    {0, 0},
};

static const TokenDict s_SGSN_EventList[] = {
    {"pdpContext",                0x01},
    {"mo-mt-sms",                 0x02},
    {"rau-gprsAttach-gprsDetach", 0x04},
    {"mbmsContext",               0x08},
    {0, 0},
};

static const TokenDict s_GGSN_EventList[] = {
    {"pdpContext",                0x01},
    {"mbmsContext",               0x02},
    {0, 0},
};

static const TokenDict s_BMSC_EventList[] = {
    {"mbmsMulticastServiceActivation",      0x01},
    {0, 0},
};

static const TokenDict s_MME_EventList[] = {
    {"ue-initiatedPDNconectivityRequest",      0x01},
    {"serviceRequestts",                       0x02},
    {"initialAttachTrackingAreaUpdateDetach",  0x04},
    {"ue-initiatedPDNdisconnection",           0x08},
    {"bearerActivationModificationDeletion",   0x10},
    {"handover",                               0x20},
    {0, 0},
};

static const TokenDict s_SPGW_EventList[] = {
    {"pdn-connectionCreation",               0x01},
    {"pdn-connectionTermination",            0x02},
    {"bearerActivationModificationDeletion", 0x04},
    {0, 0},
};

static const Parameter s_traceEventListSeq[] = {
    {"msc-s-List",  s_ctxtPrim_0_Tag,  true,  TcapXApplication::BitString,  s_MSC_S_EventList},
    {"mgw-List",    s_ctxtPrim_1_Tag,  true,  TcapXApplication::BitString,  s_MGW_EventList},
    {"sgsn-List",   s_ctxtPrim_2_Tag,  true,  TcapXApplication::BitString,  s_SGSN_EventList},
    {"ggsn-List",   s_ctxtPrim_3_Tag,  true,  TcapXApplication::BitString,  s_GGSN_EventList},
    {"bmsc-List",   s_ctxtPrim_4_Tag,  true,  TcapXApplication::BitString,  s_BMSC_EventList},
    {"mme-List",    s_ctxtPrim_5_Tag,  true,  TcapXApplication::BitString,  s_MME_EventList},
    {"sgw-List",    s_ctxtPrim_6_Tag,  true,  TcapXApplication::BitString,  s_SPGW_EventList},
    {"pgw-List",    s_ctxtPrim_7_Tag,  true,  TcapXApplication::BitString,  s_SPGW_EventList},
    {"",            s_noTag,           false, TcapXApplication::None,       0},
};


static const Parameter s_authenticationSetSeq[] = {
// TS 100 974 v7.15.0 page 298
    {"rand",   s_hexTag,  false,  TcapXApplication::HexString, 0},
    {"sres",   s_hexTag,  false,  TcapXApplication::HexString, 0},
    {"kc",     s_hexTag,  false,  TcapXApplication::HexString, 0},
    {"",       s_noTag,   false,  TcapXApplication::None,      0},
};

static const Parameter s_authenticationSet[] = {
    {"set",    s_sequenceTag,  false,  TcapXApplication::Sequence,  s_authenticationSetSeq},
    {"",       s_noTag,        false,  TcapXApplication::None,      0},
};

static const Parameter s_authenticationTriplet[] = {
    {"triplet",    s_sequenceTag,  false,  TcapXApplication::Sequence,  s_authenticationSetSeq},
    {"",           s_noTag,        false,  TcapXApplication::None,      0},
};

static const Parameter s_authenticationQuintupletSeq[] = {
    {"rand",   s_hexTag,  false,  TcapXApplication::HexString, 0},
    {"xres",   s_hexTag,  false,  TcapXApplication::HexString, 0},
    {"ck",     s_hexTag,  false,  TcapXApplication::HexString, 0},
    {"ik",     s_hexTag,  false,  TcapXApplication::HexString, 0},
    {"autn",   s_hexTag,  false,  TcapXApplication::HexString, 0},
    {"",       s_noTag,   false,  TcapXApplication::None,      0},
};

static const Parameter s_authenticationQuintuplet[] = {
    {"quintuplet",    s_sequenceTag,  false,  TcapXApplication::Sequence,  s_authenticationQuintupletSeq},
    {"",              s_noTag,        false,  TcapXApplication::None,      0},
};

static const Parameter s_authChoice[] = {
    {"tripletList",    s_ctxtCstr_0_Tag, false, TcapXApplication::SequenceOf,  s_authenticationTriplet},
    {"quintupletList", s_ctxtCstr_1_Tag, false, TcapXApplication::SequenceOf,  s_authenticationQuintuplet},
    {"",               s_noTag,          false, TcapXApplication::None,        0},
};

static const Parameter s_epcAvSeq[] = {
    {"rand",                s_hexTag,      false,  TcapXApplication::HexString, 0},
    {"xres",                s_hexTag,      false,  TcapXApplication::HexString, 0},
    {"autn",                s_hexTag,      false,  TcapXApplication::HexString, 0},
    {"kasme",               s_hexTag,      false,  TcapXApplication::HexString, 0},
    {"extensionContainer",  s_sequenceTag, true,   TcapXApplication::HexString, 0},
    {"",                    s_noTag,       false,  TcapXApplication::None,      0},
};

static const Parameter s_EPCAV[] = {
// TS 129 002 V9.3.0 page 359
    {"EPC-AV",    s_sequenceTag,  false,  TcapXApplication::Sequence,  s_epcAvSeq},
    {"",                s_noTag,  false,  TcapXApplication::None,      0},
};

static const Parameter s_authenticationRes[] = {
// TS 129 002 V9.3.0 page 352
    {"authenticationSetList",     s_noTag,          true,   TcapXApplication::Choice,     s_authChoice},
    {"extensionContainer",        s_sequenceTag,    true,   TcapXApplication::HexString,  0},
    {"eps-AuthenticationSetList", s_ctxtCstr_2_Tag, true,   TcapXApplication::SequenceOf, s_EPCAV},
    {"",                          s_noTag,          false,  TcapXApplication::None,       0},
};

static const TokenDict s_alertReason[] = {
// TS 100 974 v7.15.0 page 326
    {"ms-Present",       0},
    {"memoryAvailable",  1},
    {0,0},
};

static const Parameter s_subscriberIdentity[] = {
// TS 100 974 v7.15.0 page 335
    {"imsi",     s_ctxtPrim_0_Tag,    false,   TcapXApplication::TBCD,             0},
    {"msisdn",   s_ctxtPrim_1_Tag,    false,   TcapXApplication::AddressString,    0},
    {"",         s_noTag,             false,   TcapXApplication::None,             0},
};

static const TokenDict s_domainType[] = {
    {"cs-Domain",       0},
    {"ps-Domain",       1},
    {0,0},
};

static const TokenDict s_requestedNodes[] = {
    {"mme",  0x01},
    {"sgsn", 0x02},
    {0,0},
};

static const Parameter s_requestedInfo[] = {
// TS 100 974 v7.15.0 page 309
    {"locationInformation", s_ctxtPrim_0_Tag, true,   TcapXApplication::Null,         0},
    {"subscriberState",     s_ctxtPrim_1_Tag, true,   TcapXApplication::Null,         0},
    {"extensionContainer",  s_ctxtCstr_2_Tag, true,   TcapXApplication::HexString,    0},
    {"currentLocation",     s_ctxtPrim_3_Tag, true,   TcapXApplication::Null,         0},
    {"requestedDomain",     s_ctxtPrim_4_Tag, true,   TcapXApplication::Enumerated,   s_domainType},
    {"imei",                s_ctxtPrim_6_Tag, true,   TcapXApplication::Null,         0},
    {"ms-classmark",        s_ctxtPrim_5_Tag, true,   TcapXApplication::Null,         0},
    {"mnpRequestedInfo",    s_ctxtPrim_7_Tag, true,   TcapXApplication::Null,         0},
    {"t-adsData",           s_ctxtPrim_8_Tag, true,   TcapXApplication::Null,         0},
    {"requestedNodes",      s_ctxtPrim_9_Tag, true,   TcapXApplication::BitString,    s_requestedNodes},
    {"",                    s_noTag,          false,  TcapXApplication::None,         0},
};

static const Parameter s_cellIdOrLAI[] = {
// TS 100 974 v7.15.0 page 335
    {"cellIdFixedLength", s_ctxtPrim_0_Tag, false, TcapXApplication::CellIdFixedLength, 0},
    {"laiFixedLength",    s_ctxtPrim_1_Tag, false, TcapXApplication::LAIFixedLength,    0},
    {"",                  s_noTag,          false, TcapXApplication::None,              0},
};

static const Parameter s_locationInformationEPSSeq[] = {
    {"e-utranCellGlobalIdentity",   s_ctxtPrim_0_Tag, true, TcapXApplication::HexString,  0},
    {"trackingAreaIdentity",        s_ctxtPrim_1_Tag, true, TcapXApplication::HexString,  0},
    {"extensionContainer",          s_ctxtCstr_2_Tag, true, TcapXApplication::HexString,  0},
    {"geographicalInformation",     s_ctxtPrim_3_Tag, true, TcapXApplication::HexString,  0},
    {"geodeticInformation",         s_ctxtPrim_4_Tag, true, TcapXApplication::HexString,  0},
    {"currentLocationRetrieved",    s_ctxtPrim_5_Tag, true, TcapXApplication::Null,       0},
    {"ageOfLocationInformation",    s_ctxtPrim_6_Tag, true, TcapXApplication::Integer,    0},
    {"",                            s_noTag,          false,TcapXApplication::None,       0},
};

static const Parameter s_userCSGInformationSeq[] = {
    {"csg-Id",                s_ctxtPrim_0_Tag,  false, TcapXApplication::HexString,      0},
    {"extensionContainer",    s_ctxtCstr_1_Tag,  true,  TcapXApplication::HexString,      0},
    {"accessMode",            s_ctxtPrim_2_Tag,  true,  TcapXApplication::HexString,      0},
    {"cmi",                   s_ctxtPrim_3_Tag,  true,  TcapXApplication::HexString,      0},
    {"",                      s_noTag,           false, TcapXApplication::None,           0},
};

static const Parameter s_locationInformation[] = {
// TS 100 974 v7.15.0 page 309
    {"ageOfLocationInformation", s_intTag,         true,   TcapXApplication::Integer,       0},
    {"geographicalInformation",  s_ctxtPrim_0_Tag, true,   TcapXApplication::HexString,     0},
    {"vlr-Number",               s_ctxtPrim_1_Tag, true,   TcapXApplication::AddressString, 0},
    {"locationNumber",           s_ctxtPrim_2_Tag, true,   TcapXApplication::HexString,     0},
    {"cellIdOrLAI",              s_ctxtCstr_3_Tag, true,   TcapXApplication::Choice,        s_cellIdOrLAI},
    {"extensionContainer",       s_ctxtCstr_4_Tag, true,   TcapXApplication::HexString,     0},
    {"selectedLSA-Id",           s_ctxtPrim_5_Tag, true,   TcapXApplication::HexString,     0},
    {"msc-Number",               s_ctxtPrim_6_Tag, true,   TcapXApplication::AddressString, 0},
    {"geodeticInformation",      s_ctxtPrim_7_Tag, true,   TcapXApplication::HexString,     0},
    {"currentLocationRetrieved", s_ctxtPrim_8_Tag, true,   TcapXApplication::Null,          0},
    {"sai-Present",              s_ctxtPrim_9_Tag, true,   TcapXApplication::Null,          0},
    {"locationInformationEPS",   s_ctxtCstr_10_Tag,true,   TcapXApplication::Sequence,      s_locationInformationEPSSeq},
    {"userCSGInformation",       s_ctxtCstr_11_Tag,true,   TcapXApplication::Sequence,      s_userCSGInformationSeq},
    {"",                         s_noTag,          false,  TcapXApplication::None,          0},
};

static const TokenDict s_notReachableReason[] = {
    {"msPurged",        0},
    {"imsiDetached",    1},
    {"restrictedArea",  2},
    {"notRegistered",   3},
    {0,0},
};

static const Parameter s_subscriberState[] = {
// TS 100 974 v7.15.0 page 309
    {"assumedIdle",         s_ctxtPrim_0_Tag,   false, TcapXApplication::Null,       0},
    {"camelBusy",           s_ctxtPrim_1_Tag,   false, TcapXApplication::Null,       0},
    {"netDetNotReachable",  s_enumTag,          false, TcapXApplication::Enumerated, s_notReachableReason},
    {"notProvidedFromVLR",  s_ctxtPrim_2_Tag,   false, TcapXApplication::Null,       0},
    {"",                    s_noTag,            false, TcapXApplication::None,       0},
};

static const Parameter s_cellGlobalIdOrServiceAreaIdOrLAI[] = {
    {"cellGlobalIdOrServiceAreaIdFixedLength", s_ctxtPrim_0_Tag, false,  TcapXApplication::HexString,         0},
    {"laiFixedLength",                         s_ctxtPrim_1_Tag, false,  TcapXApplication::LAIFixedLength,    0},
    {"",                                       s_noTag,          false,  TcapXApplication::None,              0},
};

static const Parameter s_locationInformationGPRSSeq[] = {
    {"cellGlobalIdOrServiceAreaIdOrLAI", s_ctxtCstr_0_Tag,  true,  TcapXApplication::Choice,         s_cellGlobalIdOrServiceAreaIdOrLAI},
    {"routeingAreaIdentity",             s_ctxtPrim_1_Tag,  true,  TcapXApplication::HexString,      0},
    {"geographicalInformation",          s_ctxtPrim_2_Tag,  true,  TcapXApplication::HexString,      0},
    {"sgsn-Number",                      s_ctxtPrim_3_Tag,  true,  TcapXApplication::AddressString,  0},
    {"selectedLSAIdentity",              s_ctxtPrim_4_Tag,  true,  TcapXApplication::HexString,      0},
    {"extensionContainer",               s_ctxtCstr_5_Tag,  true,  TcapXApplication::HexString,      0},
    {"sai-Present",                      s_ctxtPrim_6_Tag,  true,  TcapXApplication::Null,           0},
    {"geodeticInformation",              s_ctxtPrim_7_Tag,  true,  TcapXApplication::HexString,      0},
    {"currentLocationRetrieved",         s_ctxtPrim_8_Tag,  true,  TcapXApplication::Null,           0},
    {"ageOfLocationInformation",         s_ctxtPrim_9_Tag,  true,  TcapXApplication::Integer,        0},
    {"userCSGInformation",               s_ctxtCstr_10_Tag, true,  TcapXApplication::Sequence,       s_userCSGInformationSeq},
    {"",                                 s_noTag,           false, TcapXApplication::None,           0},
};

static const Parameter s_PDP_ContextInfoSeq[] = {
    {"pdp-ContextIdentifier",    s_ctxtPrim_0_Tag,   false,  TcapXApplication::Integer,   0},
    {"pdp-ContextActive",        s_ctxtPrim_1_Tag,   true,   TcapXApplication::Null,      0},
    {"pdp-Type",                 s_ctxtPrim_2_Tag,   false,  TcapXApplication::HexString, 0},
    {"pdp-Address",              s_ctxtPrim_3_Tag,   true,   TcapXApplication::HexString, 0},
    {"apn-Subscribed",           s_ctxtPrim_4_Tag,   true,   TcapXApplication::HexString, 0},
    {"apn-InUse",                s_ctxtPrim_5_Tag,   true,   TcapXApplication::HexString, 0},
    {"nsapi",                    s_ctxtPrim_6_Tag,   true,   TcapXApplication::Integer,   0},
    {"transactionId",            s_ctxtPrim_7_Tag,   true,   TcapXApplication::HexString, 0},
    {"teid-ForGnAndGp",          s_ctxtPrim_8_Tag,   true,   TcapXApplication::HexString, 0},
    {"teid-ForIu",               s_ctxtPrim_9_Tag,   true,   TcapXApplication::HexString, 0},
    {"ggsn-Address",             s_ctxtPrim_10_Tag,  true,   TcapXApplication::HexString, 0},
    {"qos-Subscribed",           s_ctxtPrim_11_Tag,  true,   TcapXApplication::HexString, 0},
    {"qos-Requested",            s_ctxtPrim_12_Tag,  true,   TcapXApplication::HexString, 0},
    {"qos-Negotiated",           s_ctxtPrim_13_Tag,  true,   TcapXApplication::HexString, 0},
    {"chargingId",               s_ctxtPrim_14_Tag,  true,   TcapXApplication::HexString, 0},
    {"chargingCharacteristics",  s_ctxtPrim_15_Tag,  true,   TcapXApplication::HexString, 0},
    {"rnc-Address",              s_ctxtPrim_16_Tag,  true,   TcapXApplication::HexString, 0},
    {"extensionContainer",       s_ctxtCstr_17_Tag,  true,   TcapXApplication::HexString, 0},
    {"qos2-Subscribed",          s_ctxtPrim_18_Tag,  true,   TcapXApplication::HexString, 0},
    {"qos2-Requested",           s_ctxtPrim_19_Tag,  true,   TcapXApplication::HexString, 0},
    {"qos2-Negotiated",          s_ctxtPrim_20_Tag,  true,   TcapXApplication::HexString, 0},
    {"qos3-Subscribed",          s_ctxtPrim_21_Tag,  true,   TcapXApplication::HexString, 0},
    {"qos3-Requested",           s_ctxtPrim_22_Tag,  true,   TcapXApplication::HexString, 0},
    {"qos3-Negotiated",          s_ctxtPrim_23_Tag,  true,   TcapXApplication::HexString, 0},
    {"qos4-Subscribed",          s_ctxtPrim_25_Tag,  true,   TcapXApplication::HexString, 0},
    {"qos4-Requested",           s_ctxtPrim_26_Tag,  true,   TcapXApplication::HexString, 0},
    {"ext-pdp-Type",             s_ctxtPrim_28_Tag,  true,   TcapXApplication::HexString, 0},
    {"ext-pdp-Address",          s_ctxtPrim_29_Tag,  true,   TcapXApplication::HexString, 0},
    {"",                         s_noTag,            false,  TcapXApplication::None,      0},
};


static const Parameter s_PDP_ContextInfo[] = {
    {"PDP-ContextInfo",                  s_sequenceTag,     false, TcapXApplication::Sequence,       s_PDP_ContextInfoSeq},
    {"",                                 s_noTag,           false, TcapXApplication::None,           0},
};

static const Parameter s_PS_SubscriberStateChoice[] = {
    {"notProvidedFromSGSNorMME",          s_ctxtPrim_0_Tag,  false, TcapXApplication::Null,           0},
    {"ps-Detached",                       s_ctxtPrim_1_Tag,  false, TcapXApplication::Null,           0},
    {"ps-AttachedNotReachableForPaging",  s_ctxtPrim_2_Tag,  false, TcapXApplication::Null,           0},
    {"ps-AttachedReachableForPaging",     s_ctxtPrim_3_Tag,  false, TcapXApplication::Null,           0},
    {"ps-PDP-ActiveNotReachableForPaging",s_ctxtCstr_4_Tag,  false, TcapXApplication::SequenceOf,     s_PDP_ContextInfo},
    {"ps-PDP-ActiveReachableForPaging",   s_ctxtCstr_5_Tag,  false, TcapXApplication::SequenceOf,     s_PDP_ContextInfo},
    {"netDetNotReachable",                s_enumTag,         false, TcapXApplication::Enumerated,     s_notReachableReason},
    {"",                                  s_noTag,           false, TcapXApplication::None,           0},
};

static const Parameter s_GPRSMSClassSeq[] = {
    {"mSNetworkCapability",      s_ctxtPrim_0_Tag,  false,  TcapXApplication::HexString,  0},
    {"mSRadioAccessCapability",  s_ctxtPrim_1_Tag,  true,   TcapXApplication::HexString,  0},
    {"",                         s_noTag,           false,  TcapXApplication::None,       0},
};

static const TokenDict s_numberPortabilityStatus[] = {
    {"notKnownToBePorted",                    0},
    {"ownNumberPortedOut",                    1},
    {"foreignNumberPortedToForeignNetwork",   2},
    {"ownNumberNotPortedOut",                 4},
    {"foreignNumberPortedIn",                 5},
    {0,0}
};

static const Parameter s_MNPInfoResSeq[] = {
    {"routeingNumber",           s_ctxtPrim_0_Tag,  true,    TcapXApplication::TBCD,          0},
    {"imsi",                     s_ctxtPrim_1_Tag,  true,    TcapXApplication::TBCD,          0},
    {"msisdn",                   s_ctxtPrim_2_Tag,  true,    TcapXApplication::AddressString, 0},
    {"numberPortabilityStatus",  s_ctxtPrim_3_Tag,  true,    TcapXApplication::Enumerated,    s_numberPortabilityStatus},
    {"extensionContainer",       s_ctxtCstr_4_Tag,  true,    TcapXApplication::HexString,     0},
    {"",                         s_noTag,           false,   TcapXApplication::None,          0},
};

static const TokenDict s_IMS_VoiceOverPS_SessionsInd[] = {
    {"imsVoiceOverPS-SessionsNotSupported",   0},
    {"imsVoiceOverPS-SessionsSupported",      1},
    {0,0}
};

static const Parameter s_subscriberInfo[] = {
// TS 100 974 v7.15.0 page 309
    {"locationInformation",               s_ctxtCstr_0_Tag, true, TcapXApplication::Sequence,  s_locationInformation},
    {"subscriberState",                   s_ctxtCstr_1_Tag, true, TcapXApplication::Choice,    s_subscriberState},
    {"extensionContainer",                s_ctxtCstr_2_Tag, true, TcapXApplication::HexString, 0},
    {"locationInformationGPRS",           s_ctxtCstr_3_Tag, true, TcapXApplication::Sequence,  s_locationInformationGPRSSeq},
    {"ps-SubscriberState",                s_ctxtCstr_4_Tag, true, TcapXApplication::Choice,    s_PS_SubscriberStateChoice},
    {"imei",                              s_ctxtPrim_5_Tag, true, TcapXApplication::TBCD,      0},
    {"ms-Classmark2",                     s_ctxtPrim_6_Tag, true, TcapXApplication::HexString, 0},
    {"gprs-MS-Class",                     s_ctxtCstr_7_Tag, true, TcapXApplication::Sequence,  s_GPRSMSClassSeq},
    {"mnpInfoRes",                        s_ctxtCstr_8_Tag, true, TcapXApplication::Sequence,  s_MNPInfoResSeq},
    {"imsVoiceOverPS-SessionsIndication", s_ctxtPrim_9_Tag, true, TcapXApplication::Enumerated,s_IMS_VoiceOverPS_SessionsInd},
    {"lastUE-ActivityTime",               s_ctxtPrim_10_Tag,true, TcapXApplication::HexString, 0},
    {"lastRAT-Type",                      s_ctxtPrim_11_Tag,true, TcapXApplication::Enumerated,s_used_RAT_Type},
    {"eps-SubscriberState",               s_ctxtCstr_12_Tag,true, TcapXApplication::Choice,    s_PS_SubscriberStateChoice},
    {"locationInformationEPS",            s_ctxtCstr_13_Tag,true, TcapXApplication::Sequence,  s_locationInformationEPSSeq},
    {"",                                  s_noTag,          false,TcapXApplication::None,      0},
};

static const TokenDict s_reportingState[] = {
    {"stopMonitoring",   0},
    {"startMonitoring",  1},
    {0,0}
};

static const TokenDict s_CCBSSubscriberStatus[] = {
    {"ccbsNotIdle",         0},
    {"ccbsIdle",            1},
    {"ccbsNotReachable",    2},
    {0,0}
};

static const Parameter s_eventReportData[] = {
// TS 100 974 v7.15.0 page 315
    {"ccbs-SubscriberStatus",   s_ctxtPrim_0_Tag, true,  TcapXApplication::Enumerated,   s_CCBSSubscriberStatus},
    {"extensionContainer",      s_ctxtCstr_1_Tag, true,  TcapXApplication::HexString,    0},
    {"",                        s_noTag,          false, TcapXApplication::None,         0},
};

static const TokenDict s_monitoringMode[] = {
// TS 100 974 v7.15.0 page 315
    {"a-side", 0},
    {"b-side", 1},
    {0,0}
};
static const TokenDict s_callOutcome[] = {
// TS 100 974 v7.15.0 page 316
    {"success", 0},
    {"failure", 1},
    {"busy",    2},
    {0,0}
};

static const Parameter s_callReportData[] = {
// TS 100 974 v7.15.0 page 315
    {"monitoringMode",          s_ctxtPrim_0_Tag,  true,    TcapXApplication::Enumerated,   s_monitoringMode},
    {"callOutcome",             s_ctxtPrim_1_Tag,  true,    TcapXApplication::Enumerated,   s_callOutcome},
    {"extensionContainer",      s_ctxtCstr_2_Tag,  true,    TcapXApplication::HexString,     0},
    {"",                        s_noTag,           false,   TcapXApplication::None,          0},
};

static const Parameter s_updateLocationArgs[] = {
    // update location args
    {"imsi",                        s_hexTag,          false,   TcapXApplication::TBCD,          0},
    {"msc-Number",                  s_ctxtPrim_1_Tag,  false,   TcapXApplication::AddressString, 0},
    {"vlr-Number",                  s_hexTag,          false,   TcapXApplication::AddressString, 0},
    {"lmsi",                        s_ctxtPrim_10_Tag, true,    TcapXApplication::HexString,     0},
    {"extensionContainer",          s_sequenceTag,     true,    TcapXApplication::HexString,     0},
    {"vlr-Capability",              s_ctxtCstr_6_Tag,  true,    TcapXApplication::Sequence,      s_vlrCapability},
    {"informPreviousNetworkEntity", s_ctxtPrim_11_Tag, true,    TcapXApplication::Null,          0},
    {"cs-LCS-NotSupportedByUE",     s_ctxtPrim_12_Tag, true,    TcapXApplication::Null,          0},
    {"v-gmlc-Address",              s_ctxtPrim_2_Tag,  true,    TcapXApplication::HexString,     0},
    {"add-info",                    s_ctxtCstr_13_Tag, true,    TcapXApplication::Sequence,      s_addInfoSeq},
    {"pagingArea",                  s_ctxtCstr_14_Tag, true,    TcapXApplication::SequenceOf,    s_locationAreaChoice},
    {"skipSubscriberDataUpdate",    s_ctxtPrim_15_Tag, true,    TcapXApplication::Null,          0},
    {"restorationIndicator",        s_ctxtPrim_16_Tag, true,    TcapXApplication::Null,          0},
    {"",                            s_noTag,           false,   TcapXApplication::None,          0},
};

static const Parameter s_updateLocationRes[] = {
    {"hlr-Number",        s_hexTag,         false,   TcapXApplication::AddressString,0},
    {"extensionContainer",s_sequenceTag,    true,    TcapXApplication::HexString,    0},
    {"add-Capability",    s_nullTag,        true,    TcapXApplication::Null,         0},
    {"pagingArea-Capability",s_ctxtPrim_0_Tag, true, TcapXApplication::Null,         0},
    {"",                  s_noTag,          false,   TcapXApplication::None,         0},
};

static const Parameter s_cancelLocationArgs[] = {
    {"identity",          s_noTag,          false,    TcapXApplication::Choice,       s_mapIdentity},
    {"cancellationType",  s_enumTag,        true,     TcapXApplication::Enumerated,   s_cancellationType},
    {"extensionContainer",s_sequenceTag,    true,     TcapXApplication::HexString,    0},
    {"typeOfUpdate",      s_ctxtPrim_0_Tag, true,     TcapXApplication::Enumerated,   s_typeOfUpdate},
    {"",                  s_noTag,          false,    TcapXApplication::None,         0},
};

static const Parameter s_extensionContainerRes[] = {
    {"extensionContainer",s_sequenceTag,    true,     TcapXApplication::HexString,     0},
    {"",                  s_noTag,          false,    TcapXApplication::None,          0},
};

static const Parameter s_provideRoamingNumberArgs[] = {
    {"imsi",                      s_ctxtPrim_0_Tag,    false,   TcapXApplication::TBCD,             0},
    {"msc-Number",                s_ctxtPrim_1_Tag,    false,   TcapXApplication::AddressString,    0},
    {"msisdn",                    s_ctxtPrim_2_Tag,    true,    TcapXApplication::AddressString,    0},
    {"lmsi",                      s_ctxtPrim_4_Tag,    true,    TcapXApplication::HexString,        0},
    {"gsm-BearerCapability",      s_ctxtCstr_5_Tag,    true,    TcapXApplication::Sequence,         s_externalSignalInfo},
    {"networkSignalInfo",         s_ctxtCstr_6_Tag,    true,    TcapXApplication::Sequence,         s_externalSignalInfo},
    {"suppressionOfAnnouncement", s_ctxtPrim_7_Tag,    true,    TcapXApplication::Null,             0},
    {"gmsc-Address",              s_ctxtPrim_8_Tag,    true,    TcapXApplication::AddressString,    0},
    {"callReferenceNumber",       s_ctxtPrim_9_Tag,    true,    TcapXApplication::HexString,        0},
    {"or-Interrogation",          s_ctxtPrim_10_Tag,   true,    TcapXApplication::Null,             0},
    {"extensionContainer",        s_ctxtCstr_11_Tag,   true,    TcapXApplication::HexString,        0},
    {"alertingPattern",           s_ctxtPrim_12_Tag,   true,    TcapXApplication::Enumerated,       s_alertPattern},
    {"ccbs-Call",                 s_ctxtPrim_13_Tag,   true,    TcapXApplication::Null,             0},
    {"supportedCamelPhasesInGMSC",s_ctxtPrim_15_Tag,   true,    TcapXApplication::BitString,        s_camelPhases},
    {"additionalSignalInfo",      s_ctxtCstr_14_Tag,   true,    TcapXApplication::Sequence,         s_extExtenalSignalInfo},
    {"orNotSupportedInGMSC",      s_ctxtPrim_16_Tag,   true,    TcapXApplication::Null,             0},
    {"pre-pagingSupported",       s_ctxtPrim_17_Tag,   true,    TcapXApplication::Null,             0},
    {"longFTN-Supported",         s_ctxtPrim_18_Tag,   true,    TcapXApplication::Null,             0},
    {"suppress-VT-CSI",           s_ctxtPrim_19_Tag,   true,    TcapXApplication::Null,             0},
    {"offeredCamel4CSIsInInterrogatingNode", s_ctxtPrim_20_Tag, true, TcapXApplication::BitString,  s_offeredCamel4CSIs},
    {"mtRoamingRetrySupported",   s_ctxtPrim_21_Tag,   true,    TcapXApplication::Null,             0},
    {"pagingArea",                s_ctxtCstr_22_Tag,   true,    TcapXApplication::SequenceOf,       s_locationAreaChoice},
    {"callPriority",              s_ctxtPrim_23_Tag,   true,    TcapXApplication::Enumerated,       s_EMLPPPriority},
    {"",                          s_noTag,             false,   TcapXApplication::None,             0},
};

static const Parameter s_provideRoamingNumberRes[] = {
    {"roamingNumber",             s_hexTag,         false,TcapXApplication::AddressString,  0},
    {"extensionContainer",        s_sequenceTag,    true, TcapXApplication::HexString,      0},
    {"releaseResourcesSupported", s_nullTag,        true, TcapXApplication::Null,           0},
    {"",                          s_noTag,          false,TcapXApplication::None,           0},
};

static const Parameter s_insertSubscriberDataArgs[] = {
    {"imsi",                                           s_ctxtPrim_0_Tag,  true,   TcapXApplication::TBCD,         0},
    {"msisdn",                                         s_ctxtPrim_1_Tag,  true,   TcapXApplication::AddressString,0},
    {"category",                                       s_ctxtPrim_2_Tag,  true,   TcapXApplication::Enumerated,   s_category},
    {"subscriberStatus",                               s_ctxtPrim_3_Tag,  true,   TcapXApplication::Enumerated,   s_subscriberStatus},
    {"bearerServiceList",                              s_ctxtCstr_4_Tag,  true,   TcapXApplication::SequenceOf,   s_bearerService},
    {"teleserviceList",                                s_ctxtCstr_6_Tag,  true,   TcapXApplication::SequenceOf,   s_teleservice},
    {"provisionedSS",                                  s_ctxtCstr_7_Tag,  true,   TcapXApplication::SequenceOf,   s_extSSInfo},
    {"odb-Data",                                       s_ctxtCstr_8_Tag,  true,   TcapXApplication::Sequence,     s_odbData},
    {"roamingRestrictionDueToUnsupportedFeature",      s_ctxtPrim_9_Tag,  true,   TcapXApplication::Null,         0},
    {"regionalSubscriptionData",                       s_ctxtCstr_10_Tag, true,   TcapXApplication::SequenceOf,   s_zoneCode},
    {"vbsSubscriptionData",                            s_ctxtCstr_11_Tag, true,   TcapXApplication::SequenceOf,   s_voiceBroadcastData},
    {"vgcsSubscriptionData",                           s_ctxtCstr_12_Tag, true,   TcapXApplication::SequenceOf,   s_voiceGroupCallData},
    {"vlrCamelSubscriptionInfo",                       s_ctxtCstr_13_Tag, true,   TcapXApplication::Sequence,     s_VlrCamelSubscriptionInfo},
    {"extensionContainer",                             s_ctxtCstr_14_Tag, true,   TcapXApplication::HexString,     0},
    {"naea-PreferredCI",                               s_ctxtCstr_15_Tag, true,   TcapXApplication::Sequence,     s_naeaPreferredCI},
    {"gprsSubscriptionData",                           s_ctxtCstr_16_Tag, true,   TcapXApplication::Sequence,     s_GPRSSubscriptionData},
    {"roamingRestrictedInSgsnDueToUnsupportedFeature", s_ctxtPrim_23_Tag, true,   TcapXApplication::Null,         0},
    {"networkAccessMode",                              s_ctxtPrim_24_Tag, true,   TcapXApplication::Enumerated,   s_networkAccessMode},
    {"lsaInformation",                                 s_ctxtCstr_25_Tag, true,   TcapXApplication::Sequence,     s_LSAInformation},
    {"lmu-Indicator",                                  s_ctxtPrim_21_Tag, true,   TcapXApplication::Null,         0},
    {"lcsInformation",                                 s_ctxtCstr_22_Tag, true,   TcapXApplication::Sequence,     s_LCSInformation},
    {"istAlertTimer",                                  s_ctxtPrim_26_Tag, true,   TcapXApplication::Integer,      0},
    {"superChargerSupportedInHLR",                     s_ctxtPrim_27_Tag, true,   TcapXApplication::HexString,    0},
    {"mc-SS-Info",                                     s_ctxtCstr_28_Tag, true,   TcapXApplication::Sequence,     s_MC_SSInfo},
    {"cs-AllocationRetentionPriority",                 s_ctxtPrim_29_Tag, true,   TcapXApplication::HexString,    0},
    {"sgsn-CAMEL-SubscriptionInfo",                    s_ctxtCstr_17_Tag, true,   TcapXApplication::Sequence,     s_SGSN_CAMELSubscriptionInfoSeq},
    {"chargingCharacteristics",                        s_ctxtPrim_18_Tag, true,   TcapXApplication::HexString,    0},
    {"accessRestrictionData",                          s_ctxtPrim_19_Tag, true,   TcapXApplication::BitString,    s_accessRestrictionData},
    {"ics-Indicator",                                  s_ctxtPrim_20_Tag, true,   TcapXApplication::Bool,         0},
    {"eps-SubscriptionData",                           s_ctxtCstr_31_Tag, true,   TcapXApplication::Sequence,     s_EPS_SubscriptionData},
    {"csg-SubscriptionDataList",                       s_ctxtCstr_32_Tag, true,   TcapXApplication::SequenceOf,   s_CSG_SubscriptionData},
    {"ue-ReachabilityRequestIndicator",                s_ctxtPrim_33_Tag, true,   TcapXApplication::Null,         0},
    {"sgsn-Number",                                    s_ctxtPrim_34_Tag, true,   TcapXApplication::AddressString,0},
    {"mme-Name",                                       s_ctxtPrim_35_Tag, true,   TcapXApplication::HexString,    0},
    {"",                                               s_noTag,           false,  TcapXApplication::None,         0},
};

static const Parameter s_insertSubscriberDataRes[] = {
    {"teleserviceList",             s_ctxtCstr_1_Tag,  true,     TcapXApplication::SequenceOf,   s_teleservice},
    {"bearerServiceList",           s_ctxtCstr_2_Tag,  true,     TcapXApplication::SequenceOf,   s_bearerService},
    {"ss-List",                     s_ctxtCstr_3_Tag,  true,     TcapXApplication::SequenceOf,   s_ssCode},
    {"odb-GeneralData",             s_ctxtPrim_4_Tag,  true,     TcapXApplication::BitString,    s_odbGeneralData},
    {"regionalSubscriptionResponse",s_ctxtPrim_5_Tag,  true,     TcapXApplication::Enumerated,   s_regionalSubscriptionResponse},
    {"supportedCamelPhases",        s_ctxtPrim_6_Tag,  true,     TcapXApplication::BitString,    s_camelPhases},
    {"extensionContainer",          s_ctxtCstr_7_Tag,  true,     TcapXApplication::HexString,    0},
    {"offeredCamel4CSIs",           s_ctxtPrim_8_Tag,  true,     TcapXApplication::BitString,    s_offeredCamel4CSIs},
    {"supportedFeatures",           s_ctxtPrim_9_Tag,  true,     TcapXApplication::BitString,    s_supportedFeatures},
    {"",                            s_noTag,           false,    TcapXApplication::None,         0},
};

static const Parameter s_deleteSubscriberDataArgs[] = {
    {"imsi",                                            s_ctxtPrim_0_Tag, false, TcapXApplication::TBCD,       0},
    {"basicServiceList",                                s_ctxtCstr_1_Tag, true,  TcapXApplication::SequenceOf, s_basicServiceCodeType},
    {"ss-List",                                         s_ctxtCstr_2_Tag, true,  TcapXApplication::SequenceOf, s_ssCode},
    {"roamingRestrictionDueToUnsupportedFeature",       s_ctxtPrim_4_Tag, true,  TcapXApplication::Null,       0},
    {"regionalSubscriptionIdentifier",                  s_ctxtPrim_5_Tag, true,  TcapXApplication::HexString,  0},
    {"vbsGroupIndication",                              s_ctxtPrim_7_Tag, true,  TcapXApplication::Null,       0},
    {"vgcsGroupIndication",                             s_ctxtPrim_8_Tag, true,  TcapXApplication::Null,       0},
    {"camelSubscriptionInfoWithdraw",                   s_ctxtPrim_9_Tag, true,  TcapXApplication::Null,       0},
    {"extensionContainer",                              s_ctxtCstr_6_Tag, true,  TcapXApplication::HexString,  0},
    {"gprsSubscriptionDataWithdraw",                    s_ctxtCstr_10_Tag,true,  TcapXApplication::Choice,     s_GPRSSubscriptionDataWithdraw},
    {"roamingRestrictedInSgsnDueToUnsuppportedFeature", s_ctxtPrim_11_Tag,true,  TcapXApplication::Null,       0},
    {"lsaInformationWithdraw",                          s_ctxtCstr_12_Tag,true,  TcapXApplication::Choice,     s_LSAInformationWithdraw},
    {"gmlc-ListWithdraw",                               s_ctxtPrim_13_Tag,true,  TcapXApplication::Null,       0},
    {"istInformationWithdraw",                          s_ctxtPrim_14_Tag,true,  TcapXApplication::Null,       0},
    {"specificCSI-Withdraw",                            s_ctxtPrim_15_Tag,true,  TcapXApplication::BitString,  s_specificCSI_Withdraw},
    {"chargingCharacteristicsWithdraw",                 s_ctxtPrim_16_Tag,true,  TcapXApplication::Null,       0},
    {"stn-srWithdraw",                                  s_ctxtPrim_17_Tag,true,  TcapXApplication::Null,       0},
    {"epsSubscriptionDataWithdraw",                     s_ctxtCstr_18_Tag,true,  TcapXApplication::Choice,     s_EPS_SubscriptionDataWithdraw},
    {"apn-oi-replacementWithdraw",                      s_ctxtPrim_19_Tag,true,  TcapXApplication::Null,       0},
    {"csg-SubscriptionDeleted",                         s_ctxtPrim_20_Tag,true,  TcapXApplication::Null,       0},
    {"",                                                s_noTag,          false, TcapXApplication::None,       0},
};

static const Parameter s_deleteSubscriberDataRes[] = {
    {"regionalSubscriptionResponse", s_ctxtPrim_0_Tag,  true, TcapXApplication::Enumerated,   s_regionalSubscriptionResponse},
    {"extensionContainer",           s_sequenceTag,     true, TcapXApplication::HexString,    0},
    {"",                             s_noTag,           false,TcapXApplication::None,         0},
};

// GSM 09.02 v5.3.0 page 721
static const Parameter s_subscriberId[] = {
    {"imsi",     s_ctxtPrim_0_Tag,    false,   TcapXApplication::TBCD,       0},
    {"tmsi",     s_ctxtPrim_1_Tag,    false,   TcapXApplication::HexString,  0},
    {"",         s_noTag,             false,   TcapXApplication::None,       0},
};

// GSM 09.02 v5.3.0 page 721
static const TokenDict s_requestParamEnum[] = {
    {"requestIMSI",               0},
    {"requestAuthenticationSet",  1},
    {"requestSubscriberData",     2},
    {"requestKi",                 4},
    {0,0}
};

// GSM 09.02 v5.3.0 page 721
static const Parameter s_requestParameter[] = {
    {"requestParameter",  s_enumTag,  true, TcapXApplication::Enumerated, s_requestParamEnum},
    {"",                  s_noTag,   false, TcapXApplication::None,       0},
};

// GSM 09.02 v5.3.0 pp 721-723
static const Parameter s_sentParameterChoice[] = {
    {"imsi",               s_ctxtPrim_0_Tag,  true,  TcapXApplication::TBCD,       0},
    {"authenticationSet",  s_ctxtCstr_1_Tag,  true,  TcapXApplication::Sequence,   s_authenticationSetSeq},
    {"subscriberData",     s_ctxtCstr_2_Tag,  true,  TcapXApplication::Sequence,   s_insertSubscriberDataArgs},
    {"ki",                 s_ctxtPrim_3_Tag,  true,  TcapXApplication::HexString,  0},
    {"",                   s_noTag,          false,  TcapXApplication::None,       0},
};

// GSM 09.02 v5.3.0 pp 721-723
static const Parameter s_sentParameterList[] = {
    {"sentParameter",  s_noTag,  false,  TcapXApplication::Choice,  s_sentParameterChoice},
    {"",               s_noTag,  false,  TcapXApplication::None,    0},
};

// GSM 09.02 v5.3.0 page 721
static const Parameter s_sendParametersDataArgs[] = {
    {"subscriberId",         s_noTag,        false,  TcapXApplication::Choice,      s_subscriberId},
    {"requestParameterList", s_sequenceTag,  false,  TcapXApplication::SequenceOf,  s_requestParameter},
    {"",                     s_noTag,        false,  TcapXApplication::None,        0},
};

// GSM 09.02 v5.3.0 pp. 721
static const Parameter s_sendParametersDataRes[] = {
    {"sentParameterList",  s_sequenceTag,  false,  TcapXApplication::SequenceOf,  s_sentParameterList},
    {"",                   s_noTag,        false,  TcapXApplication::None,        0},
};

static const Parameter s_registerSSArgs[] = {
    {"ss-Code",                  s_hexTag,           false,  TcapXApplication::Enumerated,           s_SSCode},
    {"basicService",             s_noTag,            true,   TcapXApplication::Choice,               s_basicServiceCode},
    {"forwardedToNumber",        s_ctxtPrim_4_Tag,   true,   TcapXApplication::AddressString,        0},
    {"forwardedToSubaddress",    s_ctxtPrim_6_Tag,   true,   TcapXApplication::HexString,            0},
    {"noReplyConditionTime",     s_ctxtPrim_5_Tag,   true,   TcapXApplication::Integer,              0},
    {"defaultPriority",          s_ctxtPrim_7_Tag,   true,   TcapXApplication::Enumerated,           s_EMLPPPriority},
    {"nbrUser",                  s_ctxtPrim_8_Tag,   true,   TcapXApplication::Integer,              0},
    {"longFTN-Supported",        s_ctxtPrim_9_Tag,   true,   TcapXApplication::Null,                 0},
    {"",                         s_noTag,            false,  TcapXApplication::None,                 0},
};

static const Parameter s_ssInfoRes[] = {
    {"ss-Info",                  s_noTag,    false,   TcapXApplication::Choice,    s_extSSInfoChoice},
    {"",                         s_noTag,    false,   TcapXApplication::None,      0},
};

static const Parameter s_ssCodeArgs[] = {
    {"ss-Code",                  s_hexTag,         false,  TcapXApplication::Enumerated,  s_SSCode},
    {"basicService",             s_noTag,          true,   TcapXApplication::Choice,      s_basicServiceCode},
    {"longFTN-Supported",        s_ctxtPrim_4_Tag, true,   TcapXApplication::Null,        0},
    {"",                         s_noTag,          false,  TcapXApplication::None,        0},
};

static const Parameter s_authFailureArgs[] = {
    {"imsi",               s_hexTag,          false,   TcapXApplication::TBCD,            0},
    {"failureCause",       s_enumTag,         false,   TcapXApplication::Enumerated,      s_failureCauseEnum},
    {"extensionContainer", s_sequenceTag,     true,    TcapXApplication::HexString,       0},
    {"re-attempt",         s_boolTag,         true,    TcapXApplication::Bool,            0},
    {"accessType",         s_enumTag,         true,    TcapXApplication::Enumerated,      s_accessTypeEnum},
    {"rand",               s_hexTag,          true,    TcapXApplication::HexString,       0},
    {"vlr-Number",         s_ctxtPrim_0_Tag,  true,    TcapXApplication::AddressString,   0},
    {"sgsn-Number",        s_ctxtPrim_1_Tag,  true,    TcapXApplication::AddressString,   0},
    {"",                   s_noTag,           false,   TcapXApplication::None,            0},
};

static const Parameter s_registerPasswordArgs[] = {
    {"ss-Code",                  s_hexTag, false, TcapXApplication::Enumerated,  s_SSCode},
    {"",                         s_noTag,  false, TcapXApplication::None,        0},
};

static const Parameter s_registerPasswordRes[] = {
    {"newPassword",              s_numStrTag, false, TcapXApplication::AppString, 0},
    {"",                         s_noTag,     false, TcapXApplication::None,      0},
};

static const Parameter s_getPasswordArgs[] = {
    {"guidanceInfo",             s_enumTag, false, TcapXApplication::Enumerated,  s_guidanceInfo},
    {"",                         s_noTag,   false, TcapXApplication::None,        0},
};

static const Parameter s_getPasswordRes[] = {
    {"currentPassword",          s_numStrTag, false, TcapXApplication::AppString, 0},
    {"",                         s_noTag,     false, TcapXApplication::None,      0},
};

static const Parameter s_updateGprsLocationArgs[] = {
    {"imsi",                            s_hexTag,          false,   TcapXApplication::TBCD,           0},
    {"sgsn-Number",                     s_hexTag,          false,   TcapXApplication::AddressString,  0},
    {"sgsn-Address",                    s_hexTag,          false,   TcapXApplication::HexString,      0},
    {"extensionContainer",              s_sequenceTag,     true,    TcapXApplication::HexString,      0},
    {"sgsn-Capability",                 s_ctxtCstr_0_Tag,  true,    TcapXApplication::Sequence,       s_SGSN_CapabilitySeq},
    {"informPreviousNetworkEntity",     s_ctxtPrim_1_Tag,  true,    TcapXApplication::Null,           0},
    {"ps-LCS-NotSupportedByUE",         s_ctxtPrim_2_Tag,  true,    TcapXApplication::Null,           0},
    {"v-gmlc-Address",                  s_ctxtPrim_3_Tag,  true,    TcapXApplication::HexString,      0},
    {"add-info",                        s_ctxtCstr_4_Tag,  true,    TcapXApplication::Sequence,       s_addInfoSeq},
    {"eps-info",                        s_ctxtCstr_5_Tag,  true,    TcapXApplication::Choice,         s_EPS_InfoChoice},
    {"servingNodeTypeIndicator",        s_ctxtPrim_6_Tag,  true,    TcapXApplication::Null,           0},
    {"skipSubscriberDataUpdate",        s_ctxtPrim_7_Tag,  true,    TcapXApplication::Null,           0},
    {"usedRAT-Type",                    s_ctxtPrim_8_Tag,  true,    TcapXApplication::Enumerated,     s_used_RAT_Type},
    {"gprsSubscriptionDataNotNeeded",   s_ctxtPrim_9_Tag,  true,    TcapXApplication::Null,           0},
    {"nodeTypeIndicator",               s_ctxtPrim_10_Tag, true,    TcapXApplication::Null,           0},
    {"areaRestricted",                  s_ctxtPrim_11_Tag, true,    TcapXApplication::Null,           0},
    {"ue-reachableIndicator",           s_ctxtPrim_12_Tag, true,    TcapXApplication::Null,           0},
    {"epsSubscriptionDataNotNeeded",    s_ctxtPrim_13_Tag, true,    TcapXApplication::Null,           0},
    {"",                                s_noTag,           false,   TcapXApplication::None,           0},
};

static const Parameter s_updateGprsLocationRes[] = {
    {"hlr-Number",                     s_hexTag,         false,   TcapXApplication::AddressString,  0},
    {"extensionContainer",             s_sequenceTag,    true,    TcapXApplication::HexString,      0},
    {"add-Capability",                 s_nullTag,        true,    TcapXApplication::Null,           0},
    {"sgsn-mmeSeparationSupported",    s_ctxtPrim_0_Tag, true,    TcapXApplication::Null,           0},
    {"",                               s_noTag,          false ,  TcapXApplication::None,           0},
};

static const Parameter s_sendRoutingInfoForGprsArgs[] = {
    {"imsi",              s_ctxtPrim_0_Tag,    false,   TcapXApplication::TBCD,           0},
    {"ggsn-Address",      s_ctxtPrim_1_Tag,    true,    TcapXApplication::HexString,      0},
    {"ggsn-Number",       s_ctxtPrim_2_Tag,    false,   TcapXApplication::AddressString,  0},
    {"extensionContainer",s_ctxtCstr_3_Tag,    true,    TcapXApplication::HexString,      0},
    {"",                  s_noTag,             false,   TcapXApplication::None,           0},
};

static const Parameter s_sendRoutingInfoForGprsRes[] = {
    {"sgsn-Address",                    s_ctxtPrim_0_Tag,    false,   TcapXApplication::HexString,     0},
    {"ggsn-Address",                    s_ctxtPrim_1_Tag,    true,    TcapXApplication::HexString,     0},
    {"mobileNotReachableReason",        s_ctxtPrim_2_Tag,    true,    TcapXApplication::Integer,       0},
    {"extensionContainer",              s_ctxtCstr_3_Tag,    true,    TcapXApplication::HexString,     0},
    {"",                                s_noTag,             false,   TcapXApplication::None,          0},
};

static const Parameter s_failureReportArgs[] = {
    {"imsi",              s_ctxtPrim_0_Tag,    false,   TcapXApplication::TBCD,          0},
    {"ggsn-Number",       s_ctxtPrim_1_Tag,    false,   TcapXApplication::AddressString, 0},
    {"ggsn-Address",      s_ctxtPrim_2_Tag,    true,    TcapXApplication::HexString,     0},
    {"extensionContainer",s_ctxtCstr_3_Tag,    true,    TcapXApplication::HexString,     0},
    {"",                  s_noTag,             false,   TcapXApplication::None,          0},
};

static const Parameter s_failureReportRes[] = {
    {"ggsn-Address",      s_ctxtPrim_0_Tag,    true,    TcapXApplication::HexString,      0},
    {"extensionContainer",s_ctxtCstr_1_Tag,    true,    TcapXApplication::HexString,      0},
    {"",                  s_noTag,             false,   TcapXApplication::None,           0},
};

static const Parameter s_resetArgs[] = {
    {"hlr-Number",        s_hexTag,         false,   TcapXApplication::AddressString,     0},
    {"hlr-List",          s_sequenceTag,    true,    TcapXApplication::SequenceOf,        s_hlrId},
    {"",                  s_noTag,          false,   TcapXApplication::None,              0},
};

static const Parameter s_sendRoutingInfoForSMArgs[] = {
    {"msisdn",                 s_ctxtPrim_0_Tag,  false,   TcapXApplication::AddressString,    0},
    {"sm-RP-PRI",              s_ctxtPrim_1_Tag,  false,   TcapXApplication::Bool,             0},
    {"serviceCentreAddress",   s_ctxtPrim_2_Tag,  false,   TcapXApplication::AddressString,    0},
    {"extensionContainer",     s_ctxtCstr_6_Tag,  true,    TcapXApplication::HexString,        0},
    {"gprsSupportIndicator",   s_ctxtPrim_7_Tag,  true,    TcapXApplication::Null,             0},
    {"sm-RP-MTI",              s_ctxtPrim_8_Tag,  true,    TcapXApplication::Integer,          0},
    {"sm-RP-SMEA",             s_ctxtPrim_9_Tag,  true,    TcapXApplication::HexString,        0},
    {"sm-deliveryNotIntended", s_ctxtPrim_10_Tag, true,    TcapXApplication::Enumerated,       s_SM_DeliveryNotIntended},
    {"",                       s_noTag,           false,   TcapXApplication::None,             0},
};

static const Parameter s_sendRoutingInfoForSMRes[] = {
    {"imsi",                   s_hexTag,          false,   TcapXApplication::TBCD,           0},
    {"locationInfoWithLMSI",   s_ctxtCstr_0_Tag,  false,   TcapXApplication::Sequence,       s_locationInfoWithLMSI},
    {"extensionContainer",     s_ctxtCstr_4_Tag,  true,    TcapXApplication::HexString,      0},
    {"",                       s_noTag,           false,   TcapXApplication::None,           0},
};

static const Parameter s_smRpDa[] = {
    {"imsi",                   s_ctxtPrim_0_Tag,  false,   TcapXApplication::TBCD,           0},
    {"lmsi",                   s_ctxtPrim_1_Tag,  false,   TcapXApplication::HexString,      0},
    {"serviceCentreAddressDA", s_ctxtPrim_4_Tag,  false,   TcapXApplication::AddressString,  0},
    {"noSM-RP-DA",             s_ctxtPrim_5_Tag,  false,   TcapXApplication::Null,           0},
    {"",                       s_noTag,           false,   TcapXApplication::None,           0},
};

static const Parameter s_smRpOa[] = {
    {"msisdn",                 s_ctxtPrim_2_Tag,  false,   TcapXApplication::AddressString,  0},
    {"serviceCentreAddressOA", s_ctxtPrim_4_Tag,  false,   TcapXApplication::AddressString,  0},
    {"noSM-RP-OA",             s_ctxtPrim_5_Tag,  false,   TcapXApplication::Null,           0},
    {"",                       s_noTag,           false,   TcapXApplication::None,           0},
};

static const Parameter s_mtForwardSMArgs[] = {
    {"sm-RP-DA",               s_noTag,           false,   TcapXApplication::Choice,         s_smRpDa},
    {"sm-RP-OA",               s_noTag,           false,   TcapXApplication::Choice,         s_smRpOa},
    {"sm-RP-UI",               s_hexTag,          false,   TcapXApplication::HexString,      0},
    {"extensionContainer",     s_sequenceTag,     true,    TcapXApplication::HexString,      0},
    {"",                       s_noTag,           false,   TcapXApplication::None,           0},
};

static const Parameter s_moForwardSMArgs[] = {
    {"sm-RP-DA",               s_noTag,           false,   TcapXApplication::Choice,         s_smRpDa},
    {"sm-RP-OA",               s_noTag,           false,   TcapXApplication::Choice,         s_smRpOa},
    {"sm-RP-UI",               s_hexTag,          false,   TcapXApplication::HexString,      0},
    {"moreMessagesToSend",     s_nullTag,         true,    TcapXApplication::Null,           0},
    {"extensionContainer",     s_sequenceTag,     true,    TcapXApplication::HexString,      0},
    {"imsi",                   s_hexTag,          true,    TcapXApplication::TBCD,           0},
    {"",                       s_noTag,           false,   TcapXApplication::None,           0},
};

static const Parameter s_forwardSMRes[] = {
    {"sm-RP-UI",               s_hexTag,          true,    TcapXApplication::HexString,      0},
    {"extensionContainer",     s_sequenceTag,     true,    TcapXApplication::HexString,      0},
    {"",                       s_noTag,           false,   TcapXApplication::None,           0},
};

static const Parameter s_forwardSMArgs[] = {
    {"sm-RP-DA",               s_noTag,           false,   TcapXApplication::Choice,         s_smRpDa},
    {"sm-RP-OA",               s_noTag,           false,   TcapXApplication::Choice,         s_smRpOa},
    {"sm-RP-UI",               s_hexTag,          false,   TcapXApplication::HexString,      0},
    {"moreMessagesToSend",     s_nullTag,         true,    TcapXApplication::Null,           0},
    {"",                       s_noTag,           false,   TcapXApplication::None,           0},
};

static const TokenDict s_SMDeliveryOutcomeEnum[] = {
    {"memoryCapacityExceeded", 0},
    {"absentSubscriber",       1},
    {"successfulTransfer",     2},
    {0,0}
};

static const Parameter s_reportSMDeliveryArgs[] = {
    {"msisdn",                                  s_hexTag,         false, TcapXApplication::AddressString, 0},
    {"serviceCentreAddress",                    s_hexTag,         false, TcapXApplication::AddressString, 0},
    {"sm-DeliveryOutcome",                      s_enumTag,        false, TcapXApplication::Enumerated,    s_SMDeliveryOutcomeEnum},
    {"absentSubscriberDiagnosticSM",            s_ctxtPrim_0_Tag, true,  TcapXApplication::Integer,       0},
    {"extensionContainer",                      s_ctxtCstr_1_Tag, true,  TcapXApplication::HexString,     0},
    {"gprsSupportIndicator",                    s_ctxtPrim_2_Tag, true,  TcapXApplication::Null,          0},
    {"deliveryOutcomeIndicator",                s_ctxtPrim_3_Tag, true,  TcapXApplication::Null,          0},
    {"additionalSM-DeliveryOutcome",            s_ctxtPrim_4_Tag, true,  TcapXApplication::Enumerated,    s_SMDeliveryOutcomeEnum},
    {"additionalAbsentSubscriberDiagnosticSM",  s_ctxtPrim_5_Tag, true,  TcapXApplication::Integer,       0},
    {"ip-sm-gw-Indicator",                      s_ctxtPrim_6_Tag, true,  TcapXApplication::Null,          0},
    {"ip-sm-gw-sm-deliveryOutcome",             s_ctxtPrim_7_Tag, true,  TcapXApplication::Enumerated,    s_SMDeliveryOutcomeEnum},
    {"ip-sm-gw-absentSubscriberDiagnosticSM",   s_ctxtPrim_8_Tag, true,  TcapXApplication::Integer,       0},
    {"",                                        s_noTag,          false, TcapXApplication::None,          0},
};

static const Parameter s_reportSMDeliveryRes[] = {
    { "storedMSISDN",       s_hexTag,      true,  TcapXApplication::AddressString, 0},
    { "extensionContainer", s_sequenceTag, true,  TcapXApplication::HexString,     0},
    {"",                    s_noTag,       false, TcapXApplication::None,          0},
};

static const Parameter s_activateTraceModeArgs[] = {
    {"imsi",                  s_ctxtPrim_0_Tag,    true,    TcapXApplication::TBCD,          0},
    {"traceReference",        s_ctxtPrim_1_Tag,    false,   TcapXApplication::HexString,     0},
    {"traceType",             s_ctxtPrim_2_Tag,    false,   TcapXApplication::Integer,       0},
    {"omc-Id",                s_ctxtPrim_3_Tag,    true,    TcapXApplication::AddressString, 0},
    {"extensionContainer",    s_ctxtCstr_4_Tag,    true,    TcapXApplication::HexString,     0},
    {"traceReference2",       s_ctxtPrim_5_Tag,    true,    TcapXApplication::HexString,     0},
    {"traceDepthList",        s_ctxtCstr_6_Tag,    true,    TcapXApplication::Sequence,      s_traceDepthListSeq},
    {"traceNE-TypeList",      s_ctxtPrim_7_Tag,    true,    TcapXApplication::BitString,     s_traceNETypeList},
    {"traceInterfaceList",    s_ctxtCstr_8_Tag,    true,    TcapXApplication::Sequence,      s_traceInterfaceListSeq},
    {"traceEventList",        s_ctxtCstr_9_Tag,    true,    TcapXApplication::Sequence,      s_traceEventListSeq},
    {"traceCollectionEntity", s_ctxtPrim_10_Tag,   true,    TcapXApplication::HexString,     0},
    {"",                      s_noTag,             false,   TcapXApplication::None,          0},
};

static const Parameter s_traceModeRes[] = {
    {"extensionContainer",    s_ctxtCstr_0_Tag,    true,    TcapXApplication::HexString,    0},
    {"traceSupportIndicator", s_ctxtPrim_1_Tag,    true,    TcapXApplication::Null,         0},
    {"",                      s_noTag,             false,   TcapXApplication::None,         0},
};

static const Parameter s_deactivateTraceModeArgs[] = {
    {"imsi",              s_ctxtPrim_0_Tag,    true,    TcapXApplication::TBCD,         0},
    {"traceReference",    s_ctxtPrim_1_Tag,    false,   TcapXApplication::HexString,    0},
    {"extensionContainer",s_ctxtCstr_2_Tag,    true,    TcapXApplication::HexString,    0},
    {"traceReference2",   s_ctxtPrim_3_Tag,    true,    TcapXApplication::HexString,    0},
    {"",                  s_noTag,             false,   TcapXApplication::None,         0},
};

static const Parameter s_lcsLocationInfo[] = {
    {"msc-Number",                      s_hexTag,          false,  TcapXApplication::AddressString, 0},
    {"lmsi",                            s_ctxtPrim_0_Tag,  true,   TcapXApplication::HexString,     0},
    {"extensionContainer",              s_ctxtCstr_1_Tag,  true,   TcapXApplication::HexString,     0},
    {"gprsNodeIndicator",               s_ctxtPrim_2_Tag,  true,   TcapXApplication::Null,          0},
    {"additional-Number",               s_ctxtCstr_3_Tag,  true,   TcapXApplication::Choice,        s_additionalNumber},
    {"supportedLCS-CapabilitySets",     s_ctxtPrim_4_Tag,  true,   TcapXApplication::BitString,     s_supportedLCSCapabilitySets},
    {"additional-LCS-CapabilitySets",   s_ctxtPrim_5_Tag,  true,   TcapXApplication::BitString,     s_supportedLCSCapabilitySets},
    {"mme-Name",                        s_ctxtPrim_6_Tag,  true,   TcapXApplication::HexString,     0},
    {"aaa-Server-Name",                 s_ctxtPrim_8_Tag,  true,   TcapXApplication::HexString,     0},
    {"",                                s_noTag,           false,  TcapXApplication::None,          0},
};

static const Parameter s_sendRoutingInfoForLCSArgs[] = {
    {"mlcNumber",         s_ctxtPrim_0_Tag,    false,   TcapXApplication::AddressString, 0},
    {"targetMS",          s_ctxtCstr_1_Tag,    false,   TcapXApplication::Choice,        s_subscriberIdentity},
    {"extensionContainer",s_ctxtCstr_2_Tag,    true,    TcapXApplication::HexString,     0},
    {"",                  s_noTag,             false,   TcapXApplication::None,          0},
};

static const Parameter s_sendRoutingInfoForLCSRes[] = {
    {"targetMS",                        s_ctxtCstr_0_Tag,    false,   TcapXApplication::Choice,        s_subscriberIdentity},
    {"lcsLocationInfo",                 s_ctxtCstr_1_Tag,    false,   TcapXApplication::Sequence,      s_lcsLocationInfo},
    {"extensionContainer",              s_ctxtCstr_2_Tag,    true,    TcapXApplication::HexString,     0},
    {"v-gmlc-Address",                  s_ctxtPrim_3_Tag,    true,    TcapXApplication::HexString,     0},
    {"h-gmlc-Address",                  s_ctxtPrim_4_Tag,    true,    TcapXApplication::HexString,     0},
    {"ppr-Address",                     s_ctxtPrim_5_Tag,    true,    TcapXApplication::HexString,     0},
    {"additional-v-gmlc-Address",       s_ctxtPrim_6_Tag,    true,    TcapXApplication::HexString,     0},
    {"",                                s_noTag,             false,   TcapXApplication::None,          0},
};

static const Parameter s_resynchronisationInfo[] = {
    {"rand",   s_hexTag,  false,  TcapXApplication::HexString, 0},
    {"auts",   s_hexTag,  false,  TcapXApplication::HexString, 0},
    {"",       s_noTag,   false,  TcapXApplication::None,      0},
};

static const TokenDict s_requestingNodeType[] = {
    {"vlr",              0},
    {"sgsn",             1},
    {"s-cscf",           2},
    {"bsf",              3},
    {"gan-aaa-server",   4},
    {"wlan-aaa-server",  5},
    {"mme",             16},
    {"mme-sgsn",        17},
    {0, 0},
};

static const Parameter s_sendAuthInfoSeq[] = {
    {"imsi",                                 s_ctxtPrim_0_Tag,  false, TcapXApplication::TBCD,       0},
    {"numberOfRequestedVectors",             s_intTag,          false, TcapXApplication::Integer,    0},
    {"segmentationProhibited",               s_nullTag,         true,  TcapXApplication::Null,       0},
    {"immediateResponsePreferred",           s_ctxtPrim_1_Tag,  true,  TcapXApplication::Null,       0},
    {"re-synchronisationInfo",               s_sequenceTag,     true,  TcapXApplication::Sequence,   s_resynchronisationInfo},
    {"extensionContainer",                   s_ctxtCstr_2_Tag,  true,  TcapXApplication::HexString,  0},
    {"requestingNodeType",                   s_ctxtPrim_3_Tag,  true,  TcapXApplication::Enumerated, s_requestingNodeType},
    {"requestingPLMN-Id",                    s_ctxtPrim_4_Tag,  true,  TcapXApplication::TBCD,       0},
    {"numberOfRequestedAdditional-Vectors",  s_ctxtPrim_5_Tag,  true,  TcapXApplication::Integer,    0},
    {"additionalVectorsAreForEPS",           s_ctxtPrim_6_Tag,  true,  TcapXApplication::Null,       0},
    {"",                                     s_noTag,           false, TcapXApplication::None,       0},
};

static const Parameter s_sendAuthenticationInfoArgs[] = {
    {"imsi",                                 s_hexTag,          false, TcapXApplication::TBCD,       0},
    {"sendAuthenticationInfoArgs",           s_sequenceTag,     false, TcapXApplication::Sequence,   s_sendAuthInfoSeq},
    {"",                                     s_noTag,           false, TcapXApplication::None,       0},
};

static const Parameter s_sendAuthenticationInfoRes[] = {
    {"sendAuthenticationInfoRes-v2", s_sequenceTag,     false, TcapXApplication::SequenceOf, s_authenticationSet},
    {"sendAuthenticationInfoRes-v3", s_ctxtCstr_3_Tag,  false, TcapXApplication::Sequence,   s_authenticationRes},
    {"",                             s_noTag,           false, TcapXApplication::None,       0},
};

static const Parameter s_restoreDataArgs[] = {
    {"imsi",                 s_hexTag,          false,   TcapXApplication::TBCD,        0},
    {"lmsi",                 s_hexTag,          true,    TcapXApplication::HexString,   0},
    {"extensionContainer",   s_sequenceTag,     true,    TcapXApplication::HexString,   0},
    {"vlr-Capability",       s_ctxtCstr_6_Tag,  true,    TcapXApplication::Sequence,    s_vlrCapability},
    {"restorationIndicator", s_ctxtPrim_7_Tag,  true,    TcapXApplication::Null,        0},
    {"",                     s_noTag,           false,   TcapXApplication::None,        0},
};

static const Parameter s_restoreDataRes[] = {
    {"hlr-Number",         s_hexTag,         false,   TcapXApplication::AddressString, 0},
    {"msNotReachable",     s_nullTag,        true,    TcapXApplication::Null,          0},
    {"extensionContainer", s_sequenceTag,    true,    TcapXApplication::HexString,     0},
    {"",                   s_noTag,          false,   TcapXApplication::None,          0},
};

static const Parameter s_sendIMSIArgs[] = {
    {"msisdn",        s_hexTag,    false,   TcapXApplication::AddressString,    0},
    {"",              s_noTag,     false,   TcapXApplication::None,             0},
};

static const Parameter s_sendIMSIRes[] = {
    {"imsi",          s_hexTag,    false,   TcapXApplication::TBCD,        0},
    {"",              s_noTag,     false,   TcapXApplication::None,        0},
};

static const Parameter s_unstructuredSSArgs[] = {
    {"ussd-DataCodingScheme", s_hexTag,            false,    TcapXApplication::HexString,        0},
    {"ussd-String",           s_hexTag,            false,    TcapXApplication::GSMString,        0},
    {"alertingPattern",       s_hexTag,            true,     TcapXApplication::Enumerated,       s_alertPattern},
    {"msisdn",                s_ctxtPrim_0_Tag,    true,     TcapXApplication::AddressString,    0},
    {"",                      s_noTag,             false,    TcapXApplication::None,             0},
};

static const Parameter s_unstructuredSSRes[] = {
    {"ussd-DataCodingScheme", s_hexTag,    false,    TcapXApplication::HexString,        0},
    {"ussd-String",           s_hexTag,    false,    TcapXApplication::GSMString,        0},
    {"",                      s_noTag,     false,    TcapXApplication::None,             0},
};

static const TokenDict s_mwStatus[] = {
    { "sc-AddressNotIncluded",  0x01 },
    { "mnrf-Set",               0x02 },
    { "mcef-Set",               0x04 },
    { "mnrg-Set",               0x08 },
    { 0, 0 }
};

static const Parameter s_informServiceCentreArgs[] = {
    { "storedMSISDN",                           s_hexTag,            true,  TcapXApplication::AddressString,    0 },
    { "mw-Status",                              s_bitsTag,           true,  TcapXApplication::BitString,        s_mwStatus },
    { "extensionContainer",                     s_sequenceTag,       true,  TcapXApplication::HexString,        0 },
    { "absentSubscriberDiagnosticSM",           s_intTag,            true,  TcapXApplication::Integer,          0 },
    { "additionalAbsentSubscriberDiagnosticSM", s_ctxtPrim_0_Tag,    true,  TcapXApplication::Integer,          0 },
    { "",                                       s_noTag,             false, TcapXApplication::None,             0 },
};

static const Parameter s_alertServiceCentreArgs[] = {
    {"msisdn",                s_hexTag,    false,     TcapXApplication::AddressString,    0},
    {"serviceCentreAddress",  s_hexTag,    false,     TcapXApplication::AddressString,    0},
    {"",                      s_noTag,     false,     TcapXApplication::None,             0},
};

static const Parameter s_readyForSMArgs[] = {
    {"imsi",                           s_ctxtPrim_0_Tag,    false,   TcapXApplication::TBCD,        0},
    {"alertReason",                    s_enumTag,           false,   TcapXApplication::Enumerated,  s_alertReason},
    {"alertReasonIndicator",           s_nullTag,           true,    TcapXApplication::Null,        0},
    {"extensionContainer",             s_sequenceTag,       true,    TcapXApplication::HexString,   0},
    {"additionalAlertReasonIndicator", s_ctxtPrim_1_Tag,    true,    TcapXApplication::Null,        0},
    {"",                               s_noTag,             false,   TcapXApplication::None,        0},
};

static const Parameter s_purgeMSArgs[] = {
    {"imsi",              s_hexTag,            false,   TcapXApplication::TBCD,            0},
    {"vlr-Number",        s_ctxtPrim_0_Tag,    true,    TcapXApplication::AddressString,   0},
    {"sgsn-Number",       s_ctxtPrim_1_Tag,    true,    TcapXApplication::AddressString,   0},
    {"extensionContainer",s_sequenceTag,       true,    TcapXApplication::HexString,       0},
    {"",                  s_noTag,             false,   TcapXApplication::None,            0},
};

static const Parameter s_purgeMSRes[] = {
    {"freezeTMSI",        s_ctxtPrim_0_Tag,    true,   TcapXApplication::Null,         0},
    {"freezeP-TMSI",      s_ctxtPrim_1_Tag,    true,   TcapXApplication::Null,         0},
    {"extensionContainer",s_sequenceTag,       true,   TcapXApplication::HexString,    0},
    {"freezeM-TMSI",      s_ctxtPrim_2_Tag,    true,   TcapXApplication::Null,         0},
    {"",                  s_noTag,             false,  TcapXApplication::None,         0},
};

static const Parameter s_provideSubscriberInfoArgs[] = {
    {"imsi",              s_ctxtPrim_0_Tag,    false,  TcapXApplication::TBCD,         0},
    {"lmsi",              s_ctxtPrim_1_Tag,    true,   TcapXApplication::HexString,    0},
    {"requestedInfo",     s_ctxtCstr_2_Tag,    false,  TcapXApplication::Sequence,     s_requestedInfo},
    {"extensionContainer",s_ctxtCstr_3_Tag,    true,   TcapXApplication::HexString,    0},
    {"",                  s_noTag,             false,  TcapXApplication::None,         0},
};

static const Parameter s_provideSubscriberInfoRes[] = {
    {"subscriberInfo",        s_sequenceTag,   false,   TcapXApplication::Sequence,      s_subscriberInfo},
    {"extensionContainer",    s_sequenceTag,   true,    TcapXApplication::HexString,     0},
    {"",                      s_noTag,         false,   TcapXApplication::None,          0},
};

static const Parameter s_anyTimeInterrogationArgs[] = {
    {"subscriberIdentity",    s_ctxtCstr_0_Tag,    false,   TcapXApplication::Choice,        s_subscriberIdentity},
    {"requestedInfo",         s_ctxtCstr_1_Tag,    false,   TcapXApplication::Sequence,      s_requestedInfo},
    {"gsmSCF-Address",        s_ctxtPrim_3_Tag,    false,   TcapXApplication::AddressString, 0},
    {"extensionContainer",    s_ctxtCstr_2_Tag,    true,    TcapXApplication::HexString,     0},
    {"",                      s_noTag,             false,   TcapXApplication::None,          0},
};

static const Parameter s_anyTimeInterrogationRes[] = {
    {"subscriberInfo",        s_sequenceTag,   false,   TcapXApplication::Sequence,      s_subscriberInfo},
    {"extensionContainer",    s_sequenceTag,   true,    TcapXApplication::HexString,     0},
    {"",                      s_noTag,         false,   TcapXApplication::None,          0},
};

static const Parameter s_setReportingStateArgs[] = {
    {"imsi",                  s_ctxtPrim_0_Tag,    true,    TcapXApplication::TBCD,         0},
    {"lmsi",                  s_ctxtPrim_1_Tag,    true,    TcapXApplication::HexString,    0},
    {"ccbs-Monitoring",       s_ctxtPrim_2_Tag,    true,    TcapXApplication::Enumerated,   s_reportingState},
    {"extensionContainer",    s_ctxtCstr_3_Tag,    true,    TcapXApplication::HexString,    0},
    {"",                      s_noTag,             false,   TcapXApplication::None,         0},
};

static const Parameter s_setReportingStateRes[] = {
    {"ccbs-SubscriberStatus", s_ctxtPrim_0_Tag, true,   TcapXApplication::Enumerated,    s_CCBSSubscriberStatus},
    {"extensionContainer",    s_ctxtCstr_1_Tag, true,   TcapXApplication::HexString,     0},
    {"",                      s_noTag,         false,   TcapXApplication::None,          0},
};

static const Parameter s_statusReportArgs[] = {
    {"imsi",                  s_ctxtPrim_0_Tag,    false,    TcapXApplication::TBCD,         0},
    {"eventReportData",       s_ctxtCstr_1_Tag,    true,     TcapXApplication::Sequence,     s_eventReportData},
    {"callReportdata",        s_ctxtCstr_2_Tag,    true,     TcapXApplication::Sequence,     s_callReportData},
    {"extensionContainer",    s_ctxtCstr_3_Tag,    true,     TcapXApplication::HexString,    0},
    {"",                      s_noTag,             false,    TcapXApplication::None,         0},
};

static const Parameter s_statusReportRes[] = {
    {"extensionContainer",    s_ctxtCstr_0_Tag,   true,    TcapXApplication::HexString,     0},
    {"",                      s_noTag,            false,   TcapXApplication::None,          0},
};

static const Operation s_mapOps[] = {
    {"updateLocation",                true,   2,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_updateLocationArgs,
	s_sequenceTag, s_updateLocationRes
    },
    {"cancelLocation",                true,   3,  SS7TCAP::SuccessOrFailureReport,
	s_ctxtCstr_3_Tag, s_cancelLocationArgs,
	s_sequenceTag,    s_extensionContainerRes
    },
    {"provideRoamingNumber",          true,   4,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_provideRoamingNumberArgs,
	s_sequenceTag, s_provideRoamingNumberRes
    },
    {"insertSubscriberData",          true,   7,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_insertSubscriberDataArgs,
	s_sequenceTag, s_insertSubscriberDataRes
    },
    {"deleteSubscriberData",          true,   8,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_deleteSubscriberDataArgs,
	s_sequenceTag, s_deleteSubscriberDataRes
    },
    {"sendParameters",                true,   9,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_sendParametersDataArgs,
	s_noTag, s_sendParametersDataRes
    },
    {"registerSS",                    true,  10,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_registerSSArgs,
	s_noTag,       s_extSSInfoChoice
    },
    {"eraseSS",                       true,  11,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_ssCodeArgs,
	s_noTag,       s_extSSInfoChoice
    },
    {"activateSS",                    true,  12,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_ssCodeArgs,
	s_noTag,       s_extSSInfoChoice
    },
    {"deactivateSS",                  true,  13,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_ssCodeArgs,
	s_noTag,       s_extSSInfoChoice
    },
    {"interrogateSS",                 true,  14,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_ssCodeArgs,
	s_noTag,       s_InterrogateSSRes
    },
    {"authenticationFailureReport",   true,  15,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_authFailureArgs,
	s_sequenceTag, s_extensionContainerRes,
    },
    {"registerPassword",              true,  17,  SS7TCAP::SuccessOrFailureReport,
	s_noTag, s_registerPasswordArgs,
	s_noTag, s_registerPasswordRes
    },
    {"getPassword",                   true,  18,  SS7TCAP::SuccessOnlyReport,
	s_noTag, s_getPasswordArgs,
	s_noTag, s_getPasswordRes
    },
    {"updateGprsLocation",            true,  23,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_updateGprsLocationArgs,
	s_sequenceTag, s_updateGprsLocationRes
    },
    {"sendRoutingInfoForGprs",        true,  24,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_sendRoutingInfoForGprsArgs,
	s_sequenceTag, s_sendRoutingInfoForGprsRes
    },
    {"failureReport",                 true,  25,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_failureReportArgs,
	s_sequenceTag, s_failureReportRes
    },
    {"reset",                         true,  37,  SS7TCAP::NoReport,
	s_sequenceTag, s_resetArgs,
	s_noTag, 0
    },
    {"forwardCheckSS-Indication",     true,  38,  SS7TCAP::NoReport,
	s_noTag, 0,
	s_noTag, 0
    },
    {"mt-forwardSM",                  true,  44,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_mtForwardSMArgs,
	s_sequenceTag, s_forwardSMRes
    },
    {"sendRoutingInfoForSM",          true,  45,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_sendRoutingInfoForSMArgs,
	s_sequenceTag, s_sendRoutingInfoForSMRes
    },
    {"mo-forwardSM",                  true,  46,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_moForwardSMArgs,
	s_sequenceTag, s_forwardSMRes
    },
    {"forwardSM",                     true,  46,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_forwardSMArgs,
	s_noTag, 0
    },
    {"reportSM-DeliveryStatus",       true,  47,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_reportSMDeliveryArgs,
	s_sequenceTag, s_reportSMDeliveryRes,

    },
    {"activateTraceMode",             true,  50,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_activateTraceModeArgs,
	s_sequenceTag, s_traceModeRes
    },
    {"deactivateTraceMode",           true,  51,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_deactivateTraceModeArgs,
	s_sequenceTag, s_traceModeRes
    },
    {"sendAuthenticationInfo",        true,  56,  SS7TCAP::SuccessOrFailureReport,
	s_noTag, s_sendAuthenticationInfoArgs,
	s_noTag, s_sendAuthenticationInfoRes
    },
    {"restoreData",                   true,  57,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_restoreDataArgs,
	s_sequenceTag, s_restoreDataRes
    },
    {"sendIMSI",                      true,  58,  SS7TCAP::SuccessOrFailureReport,
	s_noTag, s_sendIMSIArgs,
	s_noTag, s_sendIMSIRes
    },
    {"processUnstructuredSS-Request", true,  59,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_unstructuredSSArgs,
	s_sequenceTag, s_unstructuredSSRes
    },
    {"unstructuredSS-Request",        true,  60,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_unstructuredSSArgs,
	s_sequenceTag, s_unstructuredSSRes
    },
    {"unstructuredSS-Notify",         true,  61,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_unstructuredSSArgs,
	s_noTag, 0
    },
    {"informServiceCentre",           true,  63,  SS7TCAP::NoReport,
	s_sequenceTag, s_informServiceCentreArgs,
	s_noTag, 0
    },
    {"alertServiceCentre",            true,  64,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_alertServiceCentreArgs,
	s_noTag, 0
    },
    {"readyForSM",                    true,  66,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_readyForSMArgs,
	s_sequenceTag, s_extensionContainerRes
    },
    {"purgeMS",                       true,  67,  SS7TCAP::SuccessOrFailureReport,
	s_ctxtCstr_3_Tag, s_purgeMSArgs,
	s_sequenceTag,    s_purgeMSRes
    },
    {"provideSubscriberInfo",         true,  70,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_provideSubscriberInfoArgs,
	s_sequenceTag, s_provideSubscriberInfoRes
    },
    {"anyTimeInterrogation",          true,  71,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_anyTimeInterrogationArgs,
	s_sequenceTag, s_anyTimeInterrogationRes
    },
    {"setReportingState",             true,  73,  SS7TCAP::SuccessOrFailureReport,
 	s_sequenceTag, s_setReportingStateArgs,
	s_sequenceTag, s_setReportingStateRes
    },
    {"statusReport",                  true,  74,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_statusReportArgs,
	s_sequenceTag, s_statusReportRes
    },
    {"sendRoutingInfoForLCS",         true,  85,  SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_sendRoutingInfoForLCSArgs,
	s_sequenceTag, s_sendRoutingInfoForLCSRes
    },
    {"",                              false,  0,  -1,
	s_noTag,  0,
	s_noTag,  0
    },
};

static const StringList s_camelCapabOps("initialDP,assistRequestInstructions,establishTemporaryConnection,disconnectForwardConnection,"
		"connectToResource,connect,releaseCall,requestReportBCSMEvent,eventReportBCSM,continue,resetTimer,"
		"furnishChargingInformation,applyCharging,applyChargingReport,callInformationReport,callInformationRequest,"
		"sendChargingInformation,playAnnouncement,promptAndCollectUserInformation,specializedResourceReport,"
		"cancel,activityTest,initiateCallAttempt,disconnectLeg,moveLeg,splitLeg,entityReleased,"
		"continueWithArgument,disconnectForwardConnectionWithArgument,playTone,callGap");

static const Capability s_camelCapab[] = {
    {"Camel",  s_camelCapabOps},
    {0, s_noOps},
};

static TokenDict s_eventTypeBCSM[] = {
    {"collectedInfo",             2},
    {"routeSelectFailure",        4},
    {"oCalledPartyBusy",          5},
    {"oNoAnswer",                 6},
    {"oAnswer",                   7},
    {"oDisconnect",               9},
    {"oAbandon",                  10},
    {"termAttemptAuthorized",     12},
    {"tBusy",                     13},
    {"tNoAnswer",                 14},
    {"tAnswer",                   15},
    {"tDisconnect",               17},
    {"tAbandon",                  18},
    {"", 0}
};

static TokenDict s_naCICSelectionType[] = {
    {"not-indicated",                     0x00},
    {"subscribed-not-dialed",             0x01},
    {"subscribed-and-dialed",             0x02},
    {"subscribed-dialing-undeterminded",  0x03},
    {"dialed-CIC-not-subscribed",         0x04},
    {"", 0}
};

static const Parameter s_naCarrierInformationSeq[] = {
    {"naCarrierId",            s_ctxtPrim_0_Tag, true,   TcapXApplication::TBCD,       0}, //Carrier digits
    {"naCICSelectionType",     s_ctxtPrim_1_Tag, true,   TcapXApplication::Enumerated, s_naCICSelectionType},
    {"",                       s_noTag,                                        false,  TcapXApplication::None,       0},
};

static const Parameter s_initialDPArgExtension[] = {
    {"naCarrierInformation",   s_ctxtCstr_0_Tag, true,   TcapXApplication::Sequence,        s_naCarrierInformationSeq},
    {"gmscAddress",            s_ctxtPrim_1_Tag, true,   TcapXApplication::AddressString,   0},
    {"",                       s_noTag,          false,  TcapXApplication::None,            0},
};

static Parameter s_bearerCap[] = {
    {"bearerCap",  s_ctxtPrim_0_Tag, false,  TcapXApplication::UserServiceInfo, 0},
    {"",           s_noTag,          false,  TcapXApplication::None,            0},
};

static const Parameter s_initialDPArgs[] = {
    {"serviceKey",                     s_ctxtPrim_0_Tag,  false,  TcapXApplication::Integer,                0},
    {"calledPartyNumber",              s_ctxtPrim_2_Tag,  true,   TcapXApplication::CalledPartyNumber,      0},
    {"callingPartyNumber",             s_ctxtPrim_3_Tag,  true,   TcapXApplication::CallingPartyNumber,     0},
    {"callingPartysCategory",          s_ctxtPrim_5_Tag,  true,   TcapXApplication::Enumerated,             s_category},
    // TODO decode iPSSPCapabilities according to ETSI TS 101 046 V7.1.0 (2000-07) page 42
    {"iPSSPCapabilities",              s_ctxtPrim_8_Tag,  true,   TcapXApplication::HexString,              0},  // might need further decoding
    {"locationNumber",                 s_ctxtPrim_10_Tag, true,   TcapXApplication::LocationNumber,         0},
    {"originalCalledPartyID",          s_ctxtPrim_12_Tag, true,   TcapXApplication::OriginalCalledNumber,   0},
    {"extensions",                     s_ctxtCstr_15_Tag, true,   TcapXApplication::HexString,              0},
    {"highLayerCompatibility",         s_ctxtPrim_23_Tag, true,   TcapXApplication::HiLayerCompat,          0}, // might need further decoding
    {"additionalCallingPartyNumber",   s_ctxtPrim_25_Tag, true,   TcapXApplication::TBCD,                   0}, // not sure about this either
    {"bearerCapability",               s_ctxtCstr_27_Tag, true,   TcapXApplication::Choice,                 s_bearerCap},
    {"eventTypeBCSM",                  s_ctxtPrim_28_Tag, true,   TcapXApplication::Enumerated,             s_eventTypeBCSM},
    {"redirectingPartyID",             s_ctxtPrim_29_Tag, true,   TcapXApplication::RedirectingNumber,      0},
    {"redirectionInformation",         s_ctxtPrim_30_Tag, true,   TcapXApplication::RedirectionInformation, 0},
    {"imsi",                           s_ctxtPrim_50_Tag, true,   TcapXApplication::TBCD,                   0},
    {"subscriberState",                s_ctxtCstr_51_Tag, true,   TcapXApplication::Choice,                 s_subscriberState},
    {"locationInformation",            s_ctxtCstr_52_Tag, true,   TcapXApplication::Sequence,               s_locationInformation},
    {"ext-basicServiceCode",           s_ctxtCstr_53_Tag, true,   TcapXApplication::Choice,                 s_basicServiceCode},
    {"callReferenceNumber",            s_ctxtPrim_54_Tag, true,   TcapXApplication::HexString,              0},
    {"mscAddress",                     s_ctxtPrim_55_Tag, true,   TcapXApplication::AddressString,          0},
    {"calledPartyBCDNumber",           s_ctxtPrim_56_Tag, true,   TcapXApplication::AddressString,          0}, //should be checked
    {"timeAndTimezone",                s_ctxtPrim_57_Tag, true,   TcapXApplication::TBCD,                   0},//might need special decoding
    {"gsm-ForwardingPending",          s_ctxtPrim_58_Tag, true,   TcapXApplication::Null,                   0},
    {"initialDPArgExtension",          s_ctxtCstr_59_Tag, true,   TcapXApplication::Sequence,               s_initialDPArgExtension},
    {"",                               s_noTag,           false,  TcapXApplication::None,                   0},
};

static const TokenDict s_monitorMode[] = {
    {"interrupted",       0x00},
    {"notifyAndContinue", 0x01},
    {"transparent",       0x02},
    {"", 0},
};

static const TokenDict s_legType[] = {
    {"leg1",  0x01},
    {"leg2",  0x02},
    {"", 0},
};

static const Parameter s_legID[] = {
    {"sendingSideID",   s_ctxtPrim_0_Tag, false, TcapXApplication::Enumerated, s_legType},
    {"receivingSideID", s_ctxtPrim_1_Tag, false, TcapXApplication::Enumerated, s_legType},
    {"",                s_noTag,          false, TcapXApplication::None,       0},
};

static const Parameter s_DPSpecificCriteria[] = {
    {"applicationTimer",s_ctxtPrim_1_Tag, false, TcapXApplication::Integer,    0},
    {"",                s_noTag,          false, TcapXApplication::None,       0},
};

static const Parameter s_bcsmEventSeq[] = {
    {"eventTypeBCSM",      s_ctxtPrim_0_Tag, false,   TcapXApplication::Enumerated,   s_eventTypeBCSM},
    {"monitorMode",        s_ctxtPrim_1_Tag, false,   TcapXApplication::Enumerated,   s_monitorMode},
    {"legID",              s_ctxtCstr_2_Tag, true,    TcapXApplication::Choice,       s_legID},
    {"dPSpecificCriteria", s_ctxtCstr_30_Tag,true,    TcapXApplication::Choice,       s_DPSpecificCriteria},
    {"",                   s_noTag,          false,   TcapXApplication::None,         0},
};

static const Parameter s_bcsmEvent[] = {
    {"bcsmEvent",     s_sequenceTag, false,  TcapXApplication::Sequence,  s_bcsmEventSeq},
    {"",              s_noTag,       false,  TcapXApplication::None,        0},
};

static const Parameter s_requestReportBCSMEventArgs[] = {
    {"bcsmEvents",     s_ctxtCstr_0_Tag, false,  TcapXApplication::SequenceOf,  s_bcsmEvent},
    {"extensions",     s_ctxtCstr_2_Tag, true,   TcapXApplication::HexString,   0},
    {"",               s_noTag,          false,  TcapXApplication::None,        0},
};

static const Parameter s_receivingSideID[] = {
    {"receivingSideID", s_ctxtPrim_1_Tag, false, TcapXApplication::Enumerated, s_legType},
    {"",                s_noTag,          false, TcapXApplication::None,       0},
};

static const Parameter s_failureCause[] = {
// failureCause := Cause (should find encoding, for now treated as hex string)
    {"failureCause",    s_ctxtPrim_0_Tag, true,  TcapXApplication::HexString, 0},
    {"",                s_noTag,          false, TcapXApplication::None,      0},
};

static const Parameter s_busyCause[] = {
// busyCause := Cause (should find encoding, for now treated as hex string)
    {"busyCause",    s_ctxtPrim_0_Tag, true,  TcapXApplication::HexString, 0},
    {"",             s_noTag,          false, TcapXApplication::None,      0},
};

static const Parameter s_releaseCause[] = {
// releaseCause := Cause (should find encoding, for now treated as hex string)
    {"releaseCause",    s_ctxtPrim_0_Tag, true,  TcapXApplication::HexString, 0},
    {"",                s_noTag,          false, TcapXApplication::None,      0},
};

static const Parameter s_tNoAsnwerInfo[] = {
    {"callForwarded",    s_ctxtPrim_50_Tag, true,  TcapXApplication::Null, 0},
    {"",                 s_noTag,           false, TcapXApplication::None, 0},
};

static const Parameter s_tBusyInfo[] = {
// busyCause := Cause (should find encoding, for now treated as hex string)
    {"busyCause",        s_ctxtPrim_0_Tag,  true,  TcapXApplication::HexString, 0},
    {"callForwarded",    s_ctxtPrim_50_Tag, true,  TcapXApplication::Null, 0},
    {"",                 s_noTag,           false, TcapXApplication::None, 0},
};

static const Parameter s_eventSpecificInformationBCSM[] = {
    {"routeSelectFailureSpecificInfo", s_ctxtCstr_2_Tag,  false, TcapXApplication::Sequence, s_failureCause},
    {"oCalledPartyBusySpecificInfo",   s_ctxtCstr_3_Tag,  false, TcapXApplication::Sequence, s_busyCause},
    {"oNoAnswerSpecificInfo",          s_ctxtCstr_4_Tag,  false, TcapXApplication::Sequence, 0},
    {"oAnswerSpecificInfo",            s_ctxtCstr_5_Tag,  false, TcapXApplication::Sequence, 0},
    {"oDisconnectSpecificInfo",        s_ctxtCstr_7_Tag,  false, TcapXApplication::Sequence, s_releaseCause},
    {"tBusySpecificInfo",              s_ctxtCstr_8_Tag,  false, TcapXApplication::Sequence, s_tBusyInfo},
    {"tNoAnswerSpecificInfo",          s_ctxtCstr_9_Tag,  false, TcapXApplication::Sequence, s_tNoAsnwerInfo},
    {"tAnswerSpecificInfo",            s_ctxtCstr_10_Tag, false, TcapXApplication::Sequence, 0},
    {"tDisconnectSpecificInfo",        s_ctxtCstr_12_Tag, false, TcapXApplication::Sequence, s_releaseCause},
    {"",                               s_noTag,           false, TcapXApplication::None,     0},
};

static const TokenDict s_messageType[] = {
    {"request",       0x00},
    {"notification",  0x01},
    {"", 0},
};

static const Parameter s_miscCallInfoSeq[] = {
    {"messageType",      s_ctxtPrim_0_Tag,  false, TcapXApplication::Enumerated, s_messageType},
    {"",                 s_noTag,           false, TcapXApplication::None,       0},
};

static const Parameter s_eventReportBCSMArgs[] = {
    {"eventTypeBCSM",                s_ctxtPrim_0_Tag,  false,   TcapXApplication::Enumerated, s_eventTypeBCSM},
    {"eventSpecificInformationBCSM", s_ctxtCstr_2_Tag,  true,    TcapXApplication::Choice,     s_eventSpecificInformationBCSM},
    {"legID",                        s_ctxtCstr_3_Tag,  true,    TcapXApplication::Choice,     s_receivingSideID},
    {"miscCallInfo",                 s_ctxtCstr_4_Tag,  false,   TcapXApplication::Sequence,   s_miscCallInfoSeq},
    {"extensions",                   s_ctxtCstr_5_Tag,  true,    TcapXApplication::HexString,  0},
    {"",                             s_noTag,           false,   TcapXApplication::None,       0},
};

static const Parameter s_calledPartyNumber[] = {
    {"calledPartyNumber",  s_hexTag, false,   TcapXApplication::CalledPartyNumber, 0},
    {"",                   s_noTag,  false,   TcapXApplication::None,              0},
};

static const Parameter s_genericNumber[] = {
    {"genericNumber",  s_hexTag, false,   TcapXApplication::GenericNumber,   0},
    {"",               s_noTag,  false,   TcapXApplication::None,            0},
};

static const Parameter s_naInfoSeq[] = {
    {"naCarrierInformation",   s_ctxtCstr_0_Tag, true,   TcapXApplication::Sequence,        s_naCarrierInformationSeq},
//    NA Oli information takes the same value as defined in ANSI ISUP T1.113
    {"naOliInfo",              s_ctxtPrim_1_Tag, true,   TcapXApplication::HexString,       0},
    {"naChargeNumber",         s_ctxtPrim_2_Tag, true,   TcapXApplication::ChargeNumber,    0},
    {"",                       s_noTag,          false,  TcapXApplication::None,            0},
};

static const Parameter s_connectArgs[] = {
    {"destinationRoutingAddress",   s_ctxtCstr_0_Tag, false,  TcapXApplication::SequenceOf,      s_calledPartyNumber},
    //alertingPattern  decoded as hex string, encoding in GSM 09.02 (ref. 14)
    {"alertingPattern",             s_ctxtPrim_1_Tag,  true,   TcapXApplication::HexString,               0},
    {"originalCalledPartyID",       s_ctxtPrim_6_Tag,  true,   TcapXApplication::OriginalCalledNumber,    0},
    {"extensions",                  s_ctxtCstr_10_Tag, true,   TcapXApplication::HexString,               0},
    {"callingPartysCategory",       s_ctxtPrim_28_Tag, true,   TcapXApplication::Enumerated,              s_category},
    {"redirectingPartyID",          s_ctxtPrim_29_Tag, true,   TcapXApplication::RedirectingNumber,       0},
    {"redirectionInformation",      s_ctxtPrim_30_Tag, true,   TcapXApplication::RedirectionInformation,  0},
    {"genericNumbers",              s_ctxtCstr_14_Tag, true,   TcapXApplication::SetOf,                   s_genericNumber},
    {"suppressionOfAnnouncement",   s_ctxtPrim_55_Tag, true,   TcapXApplication::Null,                    0},
    {"oCSIApplicable",              s_ctxtPrim_56_Tag, true,   TcapXApplication::Null,                    0},
    {"na-Info",                     s_ctxtCstr_57_Tag, true,   TcapXApplication::Sequence,                s_naInfoSeq},
    {"",                            s_noTag,           false,  TcapXApplication::None,                    0},
};

static const Parameter s_releaseCallArgs[] = {
    // TODO - decode Cause (ETS 300 356-1 [3] Cause and Location values refer to Q.850).
    {"cause",    s_hexTag, false,  TcapXApplication::HexString, 0},
    {"",         s_noTag,  false,  TcapXApplication::None,      0},
};

static const Parameter s_assistRequestInstructionsArgs[] = {
    // Defined as Digits    - see for encoding   ETS 300 356-1 [3] Generic Number
    {"correlationID",     s_ctxtPrim_0_Tag, false,    TcapXApplication::TBCD,            0},
    // ETSI TS 101 046 - GSM CAP specification (CAMEL) page 42 for further decoding
    {"iPSSPCapabilities", s_ctxtPrim_2_Tag, false,    TcapXApplication::HexString,       0},
    {"extensions",        s_ctxtCstr_3_Tag, true,     TcapXApplication::HexString,       0},
    {"",                  s_noTag,          false,    TcapXApplication::None,            0},
};

static const TokenDict s_bothwayThroughConnectionInd[] = {
    {"bothwayPathRequired",       0x00},
    {"bothwayPathNotRequired",    0x01},
    {"", 0},
};

static const Parameter s_serviceInteractionIndicatorsTwo[] = {
// FROM CS2-datatypes { ccitt(0) identified-organization(4) etsi(0) inDomain(1)
// in-network(1) CS2(20) modules(0) in-cs2-datatypes (0) version1(0)}
    {"bothwayThroughConnectionInd", s_ctxtPrim_2_Tag, true,  TcapXApplication::Enumerated, s_bothwayThroughConnectionInd},
    {"",                            s_noTag,          false, TcapXApplication::None,       0},
};

static const Parameter s_establishTemporaryConnectionArgs[] = {
    //Digits    - see for encoding   ETS 300 356-1 [3] , definde ETSI TS 101 046 - GSM CAP specification (CAMEL) page 42
    {"assistingSSPIPRoutingAddress",   s_ctxtPrim_0_Tag, false, TcapXApplication::TBCD,       0},
    // Defined as Digits    - see for encoding   ETS 300 356-1 [3] Generic Number
    {"correlationID",                  s_ctxtPrim_1_Tag, true,  TcapXApplication::TBCD,       0},
    {"scfID",                          s_ctxtPrim_3_Tag, true,  TcapXApplication::HexString,  0},
    {"extensions",                     s_ctxtCstr_4_Tag, true,  TcapXApplication::HexString,  0},
    {"serviceInteractionIndicatorsTwo",s_ctxtCstr_7_Tag, true,  TcapXApplication::Sequence,   s_serviceInteractionIndicatorsTwo},
    {"na-Info",                        s_ctxtCstr_50_Tag,true,  TcapXApplication::Sequence,   s_naInfoSeq},
    {"",                               s_noTag,          false, TcapXApplication::None,       0},
};

static const Parameter s_resourceAddress[] = {
    {"ipRoutingAddress", s_ctxtPrim_0_Tag, false, TcapXApplication::CalledPartyNumber, 0},
    {"none",             s_ctxtPrim_3_Tag, false, TcapXApplication::Null,              0},
    {"",                 s_noTag,          false, TcapXApplication::None,              0},
};

static const Parameter s_connectToResourceArgs[] = {
    {"resourceAddress",                s_noTag,          false, TcapXApplication::Choice,     s_resourceAddress},
    {"extensions",                     s_ctxtCstr_4_Tag, true,  TcapXApplication::HexString,  0},
    {"serviceInteractionIndicatorsTwo",s_ctxtCstr_7_Tag, true,  TcapXApplication::Sequence,   s_serviceInteractionIndicatorsTwo},
    {"",                               s_noTag,          false, TcapXApplication::None,       0},
};

static const TokenDict s_timerID[] = {
    {"tssf",       0x00},
    {"",           0x01},
};

static const Parameter s_resetTimerArgs[] = {
    {"timerID",       s_ctxtPrim_0_Tag, false, TcapXApplication::Enumerated, s_timerID},
    {"timervalue",    s_ctxtPrim_1_Tag, false, TcapXApplication::Integer,    0},
    {"extensions",    s_ctxtCstr_2_Tag, true,  TcapXApplication::HexString,  0},
    {"",              s_noTag,          false, TcapXApplication::None,       0},
};

static const Parameter s_sendingSideID[] = {
    {"sendingSideID", s_ctxtPrim_0_Tag, false, TcapXApplication::Enumerated, s_legType},
    {"",              s_noTag,          false, TcapXApplication::None,       0},
};

static const Parameter s_fCIBCCCAMELsequenceSeq[] = {
    {"freeFormatData",  s_ctxtPrim_0_Tag, false, TcapXApplication::HexString,  0},
    {"partyToCharge",   s_ctxtCstr_1_Tag, false, TcapXApplication::Choice,     s_sendingSideID},
    {"",                s_noTag,          false, TcapXApplication::None,       0},
};

static const Parameter s_fCIBillingChargingCharacteristicsChoice[] = {
    {"fCIBCCCAMELsequence1", s_ctxtCstr_0_Tag, false,  TcapXApplication::Sequence, s_fCIBCCCAMELsequenceSeq},
    {"",                     s_noTag,          false,  TcapXApplication::None,     0},
};

static const Parameter s_FCIBillingChargingCharacteristics[] = {
    {"fCIBillingChargingCharacteristics", s_hexTag, false,  TcapXApplication::Choice,  s_fCIBillingChargingCharacteristicsChoice},
    {"",                                  s_noTag,  false,  TcapXApplication::None,    0},
};

static const Parameter s_releaseIfdurationExceeded[] = {
    {"tone",          s_boolTag,         false, TcapXApplication::Bool,       0},
    {"extensions",    s_ctxtCstr_10_Tag, true,  TcapXApplication::HexString,  0},
    {"",              s_noTag,           false, TcapXApplication::None,       0},
};

static const Parameter s_timeDuratinChargingSeq[] = {
    {"maxCallPeriodDuration",      s_ctxtPrim_0_Tag, false, TcapXApplication::Integer,  0},
    {"releaseIfdurationExceeded",  s_ctxtCstr_1_Tag, true,  TcapXApplication::Sequence, s_releaseIfdurationExceeded},
    {"tariffSwitchInterval",       s_ctxtPrim_2_Tag, true,  TcapXApplication::Integer,  0},
    {"",                           s_noTag,          false, TcapXApplication::None,     0},
};


static const Parameter s_aChBillingChargingCharacteristics[] = {
    {"timeDurationCharging", s_ctxtCstr_0_Tag, false, TcapXApplication::Sequence, s_timeDuratinChargingSeq},
    {"",                     s_noTag,          false, TcapXApplication::None,       0},
};

static const Parameter s_applyChargingArgs[] = {
    {"aChBillingChargingCharacteristics", s_ctxtPrim_0_Tag, false, TcapXApplication::Choice,   s_aChBillingChargingCharacteristics},
    {"partyToCharge",                     s_ctxtCstr_2_Tag, false, TcapXApplication::Choice,   s_sendingSideID},
    {"extensions",                        s_ctxtCstr_3_Tag, true,  TcapXApplication::HexString,0},
    {"",                                  s_noTag,          false, TcapXApplication::None,     0},
};

static const Parameter s_timeIfTariffSwitchSeq[] = {
    {"timeSinceTariffSwitch",  s_ctxtPrim_0_Tag, false, TcapXApplication::Integer,  0},
    {"tariffSwitchInterval",   s_ctxtPrim_1_Tag, true,  TcapXApplication::Integer,  0},
    {"",                       s_noTag,          false, TcapXApplication::None,     0},
};

static const Parameter s_timeInformation[] = {
    {"timeIfNoTariffSwitch",  s_ctxtPrim_0_Tag, false, TcapXApplication::Integer,  0},
    {"timeIfTariffSwitch",    s_ctxtCstr_1_Tag, false, TcapXApplication::Sequence, s_timeIfTariffSwitchSeq},
    {"",                      s_noTag,          false, TcapXApplication::None,     0},
};

static const Parameter s_timeDurationChargingResSeq[] = {
    {"partyToCharge",   s_ctxtCstr_0_Tag, false, TcapXApplication::Choice,   s_receivingSideID},
    {"timeInformation", s_ctxtCstr_1_Tag, false, TcapXApplication::Choice,   s_timeInformation},
    {"callActive",      s_ctxtPrim_2_Tag, false, TcapXApplication::Bool,     0},
    {"",                s_noTag,          false, TcapXApplication::None,     0},
};

static const Parameter s_callResultChoice[] = {
    {"timeDurationChargingResult", s_ctxtCstr_0_Tag, false, TcapXApplication::Sequence, s_timeDurationChargingResSeq},
    {"",                           s_noTag,          false, TcapXApplication::None,     0},
};

static const Parameter s_callResult[] = {
    {"callResult", s_hexTag, false, TcapXApplication::Choice,   s_callResultChoice},
    {"",           s_noTag,  false, TcapXApplication::None,     0},
};

static const TokenDict s_requestedInformationType[] = {
    {"callAttemptElapsedTime",       0x00},
    {"callStopTime",                 0x01},
    {"callConnectedElapsedTime",     0x02},
    {"releaseCause",                 0x1e},
    {"",                             0xff},
};

static const Parameter s_requestedInformationValue[] = {
    {"callAttemptElapsedTimeValue",  s_ctxtPrim_0_Tag, false, TcapXApplication::Integer,  0},
    {"callStopTimeValue",            s_ctxtPrim_1_Tag, false, TcapXApplication::TBCD,     0},
    {"callConnectedElapsedTimeValue",s_ctxtPrim_2_Tag, false, TcapXApplication::Integer,  0},
    {"releaseCauseValue",            s_ctxtPrim_30_Tag,false, TcapXApplication::HexString,0},
    {"",                             s_noTag,          false, TcapXApplication::None,     0},
};

static const Parameter s_requestedInformationSeq[] = {
    {"requestedInformationType",  s_ctxtPrim_0_Tag, false, TcapXApplication::Enumerated, s_requestedInformationType},
    {"requestedInformationValue", s_ctxtCstr_1_Tag, false, TcapXApplication::Choice,     s_requestedInformationValue},
    {"",                          s_noTag,          false, TcapXApplication::None,       0},
};

static const Parameter s_requestedInformation[] = {
    {"requestedInformation", s_sequenceTag,  false,  TcapXApplication::Sequence,   s_requestedInformationSeq},
    {"",                     s_noTag,        false,  TcapXApplication::None,       0},
};

static const Parameter s_callInformationArgs[] = {
    {"requestedInformationList", s_ctxtCstr_0_Tag, false,TcapXApplication::SequenceOf, s_requestedInformation},
    {"extensions",               s_ctxtCstr_2_Tag, true, TcapXApplication::HexString,  0},
    {"legID",                    s_ctxtCstr_3_Tag, true, TcapXApplication::Choice,     s_receivingSideID},
    {"",                         s_noTag,          false,TcapXApplication::None,       0},
};

static const Parameter s_requestedInfoType[] = {
    {"requestedInformationType", s_enumTag, false, TcapXApplication::Enumerated, s_requestedInformationType},
    {"",                         s_noTag,   false, TcapXApplication::None,       0},
};

static const Parameter s_callInformationRequestArgs[] = {
    {"requestedInformationTypeList", s_ctxtCstr_0_Tag, false,TcapXApplication::SequenceOf, s_requestedInfoType},
    {"extensions",                   s_ctxtCstr_2_Tag, true, TcapXApplication::HexString,  0},
    {"legID",                        s_ctxtCstr_3_Tag, true, TcapXApplication::Choice,     s_sendingSideID},
    {"",                             s_noTag,          false,TcapXApplication::None,       0},
};

static const Parameter s_CAIGSM0224Seq[] = {
    {"e1", s_ctxtPrim_0_Tag, true, TcapXApplication::Integer, 0},
    {"e2", s_ctxtPrim_1_Tag, true, TcapXApplication::Integer, 0},
    {"e3", s_ctxtPrim_2_Tag, true, TcapXApplication::Integer, 0},
    {"e4", s_ctxtPrim_3_Tag, true, TcapXApplication::Integer, 0},
    {"e5", s_ctxtPrim_4_Tag, true, TcapXApplication::Integer, 0},
    {"e6", s_ctxtPrim_5_Tag, true, TcapXApplication::Integer, 0},
    {"e7", s_ctxtPrim_6_Tag, true, TcapXApplication::Integer, 0},
    {"",   s_noTag,          false,TcapXApplication::None,    0},
};

static const Parameter s_AOCSubsequentSeq[] = {
    {"cAI-GSM0224",          s_ctxtCstr_0_Tag, false,TcapXApplication::Sequence, s_CAIGSM0224Seq},
    {"tariffSwitchInterval", s_ctxtPrim_1_Tag, true, TcapXApplication::Integer,  0},
    {"",                     s_noTag,          false,TcapXApplication::None,     0},
};

static const Parameter s_AOCBeforeAnswerSeq[] = {
    {"aOCInitial",    s_ctxtCstr_0_Tag, false,TcapXApplication::Sequence, s_CAIGSM0224Seq},
    {"aOCSubsequent", s_ctxtCstr_1_Tag, true, TcapXApplication::Sequence, s_AOCSubsequentSeq},
    {"",              s_noTag,          false,TcapXApplication::None,       0},
};

static const Parameter s_sCIBillingChargingCharacteristics[] = {
    {"aOCBeforeAnswer", s_ctxtCstr_0_Tag, false, TcapXApplication::Sequence, s_AOCBeforeAnswerSeq},
    {"aOCAfterAnswer",  s_ctxtCstr_1_Tag, false, TcapXApplication::Sequence, s_AOCSubsequentSeq},
    {"",                s_noTag,          false, TcapXApplication::None,     0},
};

static const Parameter s_sendChargingInformationArgs[] = {
    {"sCIBillingChargingCharacteristics", s_ctxtPrim_0_Tag, false, TcapXApplication::Choice,    s_sCIBillingChargingCharacteristics},
    {"partyToCharge",                     s_ctxtCstr_1_Tag, false, TcapXApplication::Choice,    s_sendingSideID},
    {"extensions",                        s_ctxtCstr_2_Tag, true,  TcapXApplication::HexString, 0},
    {"",                                  s_noTag,          false, TcapXApplication::None,      0},
};

static const Parameter s_textSeq[] = {
    {"messageContent", s_ctxtPrim_0_Tag, false, TcapXApplication::AppString,  0},
    {"attributes",     s_ctxtPrim_1_Tag, true,  TcapXApplication::HexString,  0},
    {"",               s_noTag,          false, TcapXApplication::None,       0},
};

static const Parameter s_elementaryMessageID[] = {
    {"elementaryMessageID", s_intTag, false, TcapXApplication::Integer,  0},
    {"",                    s_noTag,  false, TcapXApplication::None,     0},
};

static const Parameter s_variablePartChoice[] = {
    {"integer", s_ctxtPrim_0_Tag, false, TcapXApplication::Integer,    0},
    {"number",  s_ctxtPrim_1_Tag, false, TcapXApplication::TBCD,       0},
    {"time",    s_ctxtPrim_2_Tag, false, TcapXApplication::TBCD,       0},
    {"date",    s_ctxtPrim_3_Tag, false, TcapXApplication::TBCD,       0},
    {"price",   s_ctxtPrim_4_Tag, false, TcapXApplication::TBCD,       0},
    {"",        s_noTag,          false, TcapXApplication::None,       0},
};

static const Parameter s_variablePart[] = {
    {"variablePart", s_noTag, false, TcapXApplication::Choice,  s_variablePartChoice},
    {"",             s_noTag, false, TcapXApplication::None,    0},
};

static const Parameter s_variableMessageSeq[] = {
    {"elementaryMessageID", s_ctxtPrim_0_Tag, false, TcapXApplication::Integer,    0},
    {"variableParts",       s_ctxtCstr_1_Tag, false, TcapXApplication::SequenceOf, s_variablePart},
    {"",                    s_noTag,          false, TcapXApplication::None,       0},
};

static const Parameter s_messageID[] = {
    {"elementaryMessageID", s_ctxtPrim_0_Tag, false, TcapXApplication::Integer,    0},
    {"text",                s_ctxtCstr_1_Tag, false, TcapXApplication::Sequence,   s_textSeq},
    {"elementaryMessageIDs",s_ctxtCstr_29_Tag,false, TcapXApplication::SequenceOf, s_elementaryMessageID},
    {"variableMessage",     s_ctxtCstr_30_Tag,false, TcapXApplication::Sequence,   s_variableMessageSeq},
    {"",                    s_noTag,          false, TcapXApplication::None,       0},
};

static const Parameter s_inbandInfoSeq[] = {
    {"messageID",           s_ctxtCstr_0_Tag, false,TcapXApplication::Choice,  s_messageID},
    {"numberOfRepetitions", s_ctxtPrim_1_Tag, true, TcapXApplication::Integer, 0},
    {"duration",            s_ctxtPrim_2_Tag, true, TcapXApplication::Integer, 0},
    {"interval",            s_ctxtPrim_3_Tag, true, TcapXApplication::Integer, 0},
    {"",                    s_noTag,          false,TcapXApplication::None,    0},
};

static const Parameter s_toneSeq[] = {
    {"toneID",   s_ctxtPrim_0_Tag, false, TcapXApplication::Integer,   0},
    {"duration", s_ctxtPrim_1_Tag, true,  TcapXApplication::Integer,   0},
    {"",         s_noTag,          false, TcapXApplication::None,      0},
};

static const Parameter s_informationToSend[] = {
    {"inbandInfo", s_ctxtCstr_0_Tag, false, TcapXApplication::Sequence, s_inbandInfoSeq},
    {"tone",       s_ctxtCstr_1_Tag, false, TcapXApplication::Sequence, s_toneSeq},
    {"",           s_noTag,          false, TcapXApplication::None,     0},
};

static const Parameter s_playAnnouncementArgs[] = {
    {"informationToSend",           s_ctxtCstr_0_Tag, false, TcapXApplication::Choice,   s_informationToSend},
    {"disconnectFromIPForbidden",   s_ctxtPrim_1_Tag, false, TcapXApplication::Bool,     0},
    {"requestAnnouncementComplete", s_ctxtPrim_2_Tag, false, TcapXApplication::Bool,     0},
    {"extensions",                  s_ctxtCstr_3_Tag, true,  TcapXApplication::HexString,0},
    {"",                            s_noTag,          false, TcapXApplication::None,     0},
};

static const TokenDict s_errorTreatment[] = {
    {"stdErrorAndInfo",       0x00},
    {"help",                  0x01},
    {"repeatPrompt",          0x02},
    {"",                      0xff},
};

static const Parameter s_collectedInfoSeq[] = {
    {"minimumNbOfDigits",   s_ctxtPrim_0_Tag,  false, TcapXApplication::Integer,   0},
    {"maximumNbOfDigits",   s_ctxtPrim_1_Tag,  false, TcapXApplication::Integer,   0},
    {"endOfReplyDigit",     s_ctxtPrim_2_Tag,  true,  TcapXApplication::HexString, 0},
    {"cancelDigit",         s_ctxtPrim_3_Tag,  true,  TcapXApplication::HexString, 0},
    {"startDigit",          s_ctxtPrim_4_Tag,  true,  TcapXApplication::HexString, 0},
    {"firstDigitTimeOut",   s_ctxtPrim_5_Tag,  false, TcapXApplication::Integer,   0},
    {"interDigitTimeOut",   s_ctxtPrim_6_Tag,  false, TcapXApplication::Integer,   0},
    {"errorTreatment",      s_ctxtPrim_7_Tag,  false, TcapXApplication::Enumerated,s_errorTreatment},
    {"interruptableAnnInd", s_ctxtPrim_8_Tag,  false, TcapXApplication::Bool,      0},
    {"voiceInformation",    s_ctxtPrim_9_Tag,  false, TcapXApplication::Bool,      0},
    {"voiceBack",           s_ctxtPrim_10_Tag, false, TcapXApplication::Bool,      0},
    {"",                    s_noTag,           false, TcapXApplication::None,      0},
};

static const Parameter s_collectedInfo[] = {
    {"collectedDigits",             s_ctxtCstr_0_Tag, false, TcapXApplication::Sequence, s_collectedInfoSeq},
    {"",                            s_noTag,          false, TcapXApplication::None,     0},
};

static const Parameter s_promptAndCollectUserInformationArgs[] = {
    {"collectedInfo",               s_ctxtCstr_0_Tag, false,  TcapXApplication::Choice,   s_collectedInfo},
    {"disconnectFromIPForbidden",   s_ctxtPrim_1_Tag, false,  TcapXApplication::Bool,     0},
    {"informationToSend",           s_ctxtCstr_2_Tag, true,   TcapXApplication::Choice,   s_informationToSend},
    {"extensions",                  s_ctxtCstr_3_Tag, true,   TcapXApplication::HexString,0},
    {"",                            s_noTag,          false,  TcapXApplication::None,     0},
};

static const Parameter s_specializedResourceReportArgs[] = {
    {"specializedResourceReportArgs",      s_nullTag, false, TcapXApplication::Null, 0},
    {"",                                   s_noTag,   false, TcapXApplication::None, 0},
};

static const Parameter s_cancelChoice[] = {
    {"invokeID",       s_ctxtPrim_0_Tag, false, TcapXApplication::Integer, 0},
    {"allRequests",    s_ctxtPrim_1_Tag, false, TcapXApplication::Null,    0},
    {"",               s_noTag,          false, TcapXApplication::None,    0},
};

static const Parameter s_cancelArgs[] = {
    {"cancelArg",      s_noTag,   false, TcapXApplication::Choice, s_cancelChoice},
    {"",               s_noTag,   false, TcapXApplication::None,   0},
};

static const Operation s_camelOps[] = {
    {"initialDP",                true,   0,   SS7TCAP::FailureOnlyReport,
	s_sequenceTag, s_initialDPArgs,
	s_noTag, 0
    },
    {"assistRequestInstructions",   true,   16,   SS7TCAP::FailureOnlyReport,
	s_sequenceTag, s_assistRequestInstructionsArgs,
	s_noTag, 0
    },
    {"establishTemporaryConnection",   true,   17,   SS7TCAP::FailureOnlyReport,
	s_sequenceTag, s_establishTemporaryConnectionArgs,
	s_noTag, 0
    },
    {"disconnectForwardConnection",   true,   18,   SS7TCAP::FailureOnlyReport,
	s_noTag, 0,
	s_noTag, 0
    },
    {"connectToResource",   true,   19,   SS7TCAP::FailureOnlyReport,
	s_sequenceTag, s_connectToResourceArgs,
	s_noTag, 0
    },
    {"connect",   true,   20,   SS7TCAP::FailureOnlyReport,
	s_sequenceTag, s_connectArgs,
	s_noTag, 0
    },
    {"releaseCall",   true,   22,   SS7TCAP::NoReport,
	s_noTag, s_releaseCallArgs,
	s_noTag, 0
    },
    {"requestReportBCSMEvent",   true,   23,   SS7TCAP::FailureOnlyReport,
	s_sequenceTag, s_requestReportBCSMEventArgs,
	s_noTag, 0
    },
    {"eventReportBCSM",   true,   24,   SS7TCAP::NoReport,
	s_sequenceTag, s_eventReportBCSMArgs,
	s_noTag, 0
    },
    {"continue",   true,   31,   SS7TCAP::NoReport,
	s_noTag, 0,
	s_noTag, 0
    },
    {"resetTimer",   true,   33,   SS7TCAP::FailureOnlyReport,
	s_sequenceTag, s_resetTimerArgs,
	s_noTag, 0
    },
    {"furnishChargingInformation",   true,   34,   SS7TCAP::FailureOnlyReport,
	s_noTag, s_FCIBillingChargingCharacteristics,
	s_noTag, 0
    },
    {"applyCharging",   true,   35,   SS7TCAP::FailureOnlyReport,
	s_sequenceTag, s_applyChargingArgs,
	s_noTag, 0
    },
    {"applyChargingReport",   true,   36,   SS7TCAP::FailureOnlyReport,
	s_noTag, s_callResult,
	s_noTag, 0
    },
    {"callInformationReport",   true,   44,   SS7TCAP::NoReport,
	s_sequenceTag, s_callInformationArgs,
	s_noTag, 0
    },
    {"callInformationRequest",   true,   45,   SS7TCAP::FailureOnlyReport,
	s_sequenceTag, s_callInformationRequestArgs,
	s_noTag, 0
    },
    {"sendChargingInformation",   true,   46,   SS7TCAP::FailureOnlyReport,
	s_sequenceTag, s_sendChargingInformationArgs,
	s_noTag, 0
    },
    {"playAnnouncement",   true,   47,   SS7TCAP::FailureOnlyReport,
	s_sequenceTag, s_playAnnouncementArgs,
	s_noTag, 0
    },
    {"promptAndCollectUserInformation",   true,   48,   SS7TCAP::SuccessOrFailureReport,
	s_sequenceTag, s_promptAndCollectUserInformationArgs,
	s_noTag, 0
    },
    {"specializedResourceReport",   true,   49,   SS7TCAP::NoReport,
	s_noTag, 0,
	s_noTag, 0
    },
    {"cancel",   true,   53,   SS7TCAP::FailureOnlyReport,
	s_noTag, s_cancelChoice,
	s_noTag, 0
    },
    {"activityTest",   true,   55,   SS7TCAP::SuccessOnlyReport,
	s_noTag, 0,
	s_noTag, 0
    },
    {"",                              false,  0, -1,
	s_noTag, 0,
	s_noTag, 0
    },

};


static const TokenDict s_unknownSubscriberDiagnostic[] = {
    {"imsiUnknown",                0},
    {"gprsSubscriptionUnknown",    1},
    {"npdbMismatch",               2},
    {0, 0},
};

static const TokenDict s_roamingNotAllowedCause[] = {
    {"plmnRoamingNotAllowed",        0},
    {"operatorDeterminedBarring",    3},
    {0, 0},
};

static const TokenDict s_additionalRoamingNotAllowedCause[] = {
    {"supportedRAT-TypesNotAllowed",  0},
    {0, 0},
};

static const TokenDict s_absentSubscriberReason[] = {
    {"imsiDetach",      0 },
    {"restrictedArea",  1 },
    {"noPageResponse",  2 },
    {"purgedMS",        3 },
    {"mtRoamingRetry",  4 },
    {0, 0},
};

static const TokenDict s_smDeliveryFailureCause[] = {
    {"memoryCapacityExceeded",      0 },
    {"equipmentProtocolError",      1 },
    {"equipmentNotSM-Equipped",     2 },
    {"unknownServiceCentre",        3 },
    {"sc-Congestion",               4 },
    {"invalidSME-Address",          5 },
    {"subscriberNotSC-Subscriber",  6 },
    {0, 0},
};

static const TokenDict s_networkResource[] = {
    {"plmn",            0 },
    {"hlr",             1 },
    {"vlr",             2 },
    {"pvlr",            3 },
    {"controllingMSC",  4 },
    {"vmsc",            5 },
    {"eir",             6 },
    {"rss",             7 },
    {0, 0},
};

static const TokenDict s_additionalNetworkResource[] = {
    {"sgsn",   0},
    {"ggsn",   1},
    {"gmlc",   2},
    {"gsmSCF", 3},
    {"nplr",   4},
    {"auc",    5},
    {"ue",     6},
    {"mme",    7},
    {0, 0},
};

static const TokenDict s_failureCauseParam[] = {
    {"limitReachedOnNumberOfConcurrentLocationRequests", 0},
    {0, 0},
};

static const TokenDict s_unauthorizedLcscDiag[] = {
    {"noAdditionalInformation",                        0},
    {"clientNotInMSPrivacyExceptionList",              1},
    {"callToClientNotSetup",                           2},
    {"privacyOverrideNotApplicable",                   3},
    {"disallowedByLocalRegulatoryRequirements",        4},
    {"unauthorizedPrivacyClass",                       5},
    {"unauthorizedCallSessionUnrelatedExternalClient", 6},
    {"unauthorizedCallSessionRelatedExternalClient",   7},
    {0, 0},
};

static const TokenDict s_positionMethodFailureDiag[] = {
    {"congestion",                               0},
    {"insufficientResources",                    1},
    {"insufficientMeasurementData",              2},
    {"inconsistentMeasurementData",              3},
    {"locationProcedureNotCompleted",            4},
    {"locationProcedureNotSupportedByTargetMS",  5},
    {"qoSNotAttainable",                         6},
    {"positionMethodNotAvailableInNetwork",      7},
    {"positionMethodNotAvailableInLocationArea", 8},
    {0, 0},
};

static const Parameter s_extensibleSystemFailure[] = {
    {"networkResource",                s_enumTag,       true,    TcapXApplication::Enumerated,    s_networkResource},
    {"extensionContainer",             s_sequenceTag,   true,    TcapXApplication::HexString,     0},
    {"additionalNetworkResource",      s_ctxtPrim_0_Tag,true,    TcapXApplication::Enumerated,    s_additionalNetworkResource},
    {"failureCauseParam",              s_ctxtPrim_1_Tag,true,    TcapXApplication::Enumerated,    s_failureCauseParam},
    {"",                               s_noTag,         false,   TcapXApplication::None,          0},
};

static const Parameter s_systemFailure[] = {
    {"networkResource",                s_enumTag,       false,    TcapXApplication::Enumerated,    s_networkResource},
    {"extensibleSystemFailure",        s_sequenceTag,   false,    TcapXApplication::Sequence,      s_extensibleSystemFailure},
    {"",                               s_noTag,         false,    TcapXApplication::None,          0},
};

static const TokenDict s_pwRegistrationFailureCause[] = {
    {"undetermined",          0 },
    {"invalidFormat",         1 },
    {"newPasswordsMismatch",  2 },
    {0, 0},
};

static const TokenDict s_callBarringCause[] = {
    {"barringServiceActive",      0 },
    {"operatorBarring",           1 },
    {0, 0},
};

static const TokenDict s_cugRejectCause[] = {
    {"incomingCallsBarredWithinCUG",                 0 },
    {"subscriberNotMemberOfCUG",                     1 },
    {"requestedBasicServiceViolatesCUG-Constraints", 5 },
    {"calledPartySS-InteractionViolation",           7 },
    {0, 0},
};

static const Parameter s_extensibleCallBarredParam[] = {
    {"callBarringCause",               s_enumTag,       true,    TcapXApplication::Enumerated,    s_callBarringCause},
    {"extensionContainer",             s_sequenceTag,   true,    TcapXApplication::HexString,     0},
    {"unauthorisedMessageOriginator",  s_ctxtPrim_1_Tag,true,    TcapXApplication::Null,          0},
    {"",                               s_noTag,         false,   TcapXApplication::None,          0},
};

static const Parameter s_cugRejectErr[] = {
    {"cug-RejectCause",                s_enumTag,       true,    TcapXApplication::Enumerated,    s_cugRejectCause},
    {"extensionContainer",             s_sequenceTag,   true,    TcapXApplication::HexString,     0},
    {"",                               s_noTag,         false,   TcapXApplication::None,          0},
};

static const Parameter s_unknownSubscriberErr[] = {
    {"extensionContainer",             s_sequenceTag,   true,    TcapXApplication::HexString,     0},
    {"unknownSubscriberDiagnostic",    s_enumTag,       true,    TcapXApplication::Enumerated,    s_unknownSubscriberDiagnostic},
    {"",                               s_noTag,         false,   TcapXApplication::None,          0},
};

static const Parameter s_absentsubscriberSMErr[] = {
    {"absentSubscriberDiagnosticSM",             s_intTag,        true,    TcapXApplication::Integer,       0},
    {"extensionContainer",                       s_sequenceTag,   true,    TcapXApplication::HexString,     0},
    {"additionalAbsentSubscriberDiagnosticSM",   s_intTag,        true,    TcapXApplication::Integer,       0},
    {"",                                         s_noTag,         false,   TcapXApplication::None,          0},
};

static const Parameter s_roamingNotAllowedErr[] = {
    {"roamingNotAllowedCause",           s_enumTag,        true,    TcapXApplication::Enumerated,    s_roamingNotAllowedCause},
    {"extensionContainer",               s_sequenceTag,    true,    TcapXApplication::HexString,     0},
    {"additionalRoamingNotAllowedCause", s_ctxtPrim_0_Tag, true,    TcapXApplication::Enumerated,    s_additionalRoamingNotAllowedCause},
    {"",                                 s_noTag,          false,   TcapXApplication::None,          0},
};

static const Parameter s_callBarredErr[] = {
    {"callBarringCause",               s_enumTag,       false,   TcapXApplication::Enumerated,    s_callBarringCause},
    {"extensibleCallBarredParam",      s_sequenceTag,   false,   TcapXApplication::Sequence,      s_extensibleCallBarredParam},
    {"",                               s_noTag,         false,   TcapXApplication::None,          0},
};

static const Parameter s_ssErrorStatusErr[] = {
    {"ss-Status",     s_hexTag,         false,    TcapXApplication::Flags,         s_ssStatus},
    {"",              s_noTag,          false,    TcapXApplication::None,          0},
};

static const Parameter s_ssIncompatibilityErr[] = {
    {"ss-Code",               s_ctxtPrim_1_Tag,   true,    TcapXApplication::Enumerated,    s_SSCode},
    {"basicService",          s_noTag,            true,    TcapXApplication::Choice,        s_basicServiceCode},
    {"ss-Status",             s_ctxtPrim_4_Tag,   true,    TcapXApplication::Flags,         s_ssStatus},
    {"",                      s_noTag,            false,   TcapXApplication::None,          0},
};

static const Parameter s_facilityNotSupportedErr[] = {
    {"extensionContainer",                             s_sequenceTag,      true,  TcapXApplication::HexString,   0},
    {"shapeOfLocationEstimateNotSupported",            s_ctxtPrim_0_Tag,   true,  TcapXApplication::Null,        0},
    {"neededLcsCapabilityNotSupportedInServingNode",   s_ctxtPrim_1_Tag,   true,  TcapXApplication::Null,        0},
    {"",                                               s_noTag,            false, TcapXApplication::None,        0},
};

static const Parameter s_absentSubscriberErr[] = {
    {"extensionContainer",             s_sequenceTag,     true,  TcapXApplication::HexString,   0},
    {"absentSubscriberReason",         s_ctxtPrim_0_Tag,  true,  TcapXApplication::Enumerated,  s_absentSubscriberReason},
    {"",                               s_noTag,           false, TcapXApplication::None,        0},
};

static const Parameter s_subscriberBusyMtSmsErr[] = {
    {"extensionContainer",                s_sequenceTag,    true,    TcapXApplication::HexString,     0},
    {"gprsConnectionSuspended",           s_nullTag,        true,    TcapXApplication::Null,          0},
    {"",                                  s_noTag,          false,   TcapXApplication::None,          0},
};

static const Parameter s_smDeliveryFailureErr[] = {
    {"sm-EnumeratedDeliveryFailureCause", s_enumTag,        true,    TcapXApplication::Enumerated,    s_smDeliveryFailureCause},
    {"diagnosticInfo",                    s_hexTag,         true,    TcapXApplication::HexString,     0},
    {"extensionContainer",                s_sequenceTag,    true,    TcapXApplication::HexString,     0},
    {"",                                  s_noTag,          false,   TcapXApplication::None,          0},
};

static const Parameter s_pwRegistrationFailureErr[] = {
    {"pw-RegistrationFailureCause",    s_enumTag,   true,    TcapXApplication::Enumerated,    s_pwRegistrationFailureCause},
    {"",                               s_noTag,     false,   TcapXApplication::None,          0},
};

static const Parameter s_busySubscriberErr[] = {
    {"extensionContainer",             s_sequenceTag,      true,    TcapXApplication::HexString,     0},
    {"ccbs-Possible",                  s_ctxtPrim_0_Tag,   true,    TcapXApplication::Null,          0},
    {"ccbs-Busy",                      s_ctxtPrim_1_Tag,   true,    TcapXApplication::Null,          0},
    {"",                               s_noTag,            false,   TcapXApplication::None,          0},
};

static const Parameter s_unauthorizedLcscErr[] = {
    {"unauthorizedLCSClient-Diagnostic",  s_ctxtPrim_0_Tag, true,    TcapXApplication::Enumerated,    s_unauthorizedLcscDiag},
    {"extensionContainer",                s_ctxtPrim_1_Tag, true,    TcapXApplication::HexString,     0},
    {"",                                  s_noTag,          false,   TcapXApplication::None,          0},
};

static const Parameter s_positionMethodFailureErr[] = {
    {"positionMethodFailure-Diagnostic",  s_ctxtPrim_0_Tag, true,    TcapXApplication::Enumerated,    s_positionMethodFailureDiag},
    {"extensionContainer",                s_ctxtPrim_1_Tag, true,    TcapXApplication::HexString,     0},
    {"",                                  s_noTag,          false,   TcapXApplication::None,          0},
};

static const Operation s_mapErrors[] = {
    {"unknownSubscriber", true, 1, -1,
	s_sequenceTag, s_unknownSubscriberErr,
	s_noTag, 0
    },
    {"unknownMSC", true, 3, -1,
	s_noTag, 0,
	s_noTag, 0
    },
    {"unidentifiedSubscriber", true, 5, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"absentsubscriberSM", true, 6, -1,
	s_sequenceTag, s_absentsubscriberSMErr,
	s_noTag, 0
    },
    {"unknownEquipment", true, 7, -1,
	s_noTag, 0,
	s_noTag, 0
    },
    {"roamingNotAllowed", true, 8, -1,
	s_sequenceTag, s_roamingNotAllowedErr,
	s_noTag, 0
    },
    {"illegalSubscriber", true, 9, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"bearerServiceNotProvisioned", true, 10, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"teleserviceNotProvisioned", true, 11, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"illegalEquipment", true, 12, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"callBarred", true, 13, -1,
	s_noTag, s_callBarredErr,
	s_noTag, 0
    },
    {"forwardingViolation", true, 14, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"cug-Reject", true, 15, -1,
	s_sequenceTag, s_cugRejectErr,
	s_noTag, 0
    },
    {"illegalSS-Operation", true, 16, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"ss-ErrorStatus", true, 17, -1,
	s_noTag, s_ssErrorStatusErr,
	s_noTag, 0
    },
    {"ss-NotAvailable", true, 18, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"ss-SubscriptionViolation", true, 19, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"ss-Incompatibility", true, 20, -1,
	s_sequenceTag, s_ssIncompatibilityErr,
	s_noTag, 0
    },
    {"facilityNotSupported", true, 21, -1,
	s_sequenceTag, s_facilityNotSupportedErr,
	s_noTag, 0
    },
    {"ongoingGroupCall", true, 22, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"noHandoverNumberAvailable", true, 25, -1,
	s_noTag, 0,
	s_noTag, 0
    },
    {"subsequentHandoverFailure", true, 26, -1,
	s_noTag, 0,
	s_noTag, 0
    },
    {"absentSubscriber", true, 27, -1,
	s_sequenceTag, s_absentSubscriberErr,
	s_noTag, 0
    },
    {"incompatibleTerminal", true, 28, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"shortTermDenial", true, 29, -1,
	s_sequenceTag, 0,
	s_noTag, 0
    },
    {"longTermDenial", true, 30, -1,
	s_sequenceTag, 0,
	s_noTag, 0
    },
    {"subscriberBusyForMT-SMS", true, 31, -1,
	s_sequenceTag, s_subscriberBusyMtSmsErr,
	s_noTag, 0
    },
    {"sm-DeliveryFailure", true, 32, -1,
	s_sequenceTag, s_smDeliveryFailureErr,
	s_noTag, 0
    },
    {"messageWaitingListFull", true, 33, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"systemFailure", true, 34, -1,
	s_noTag, s_systemFailure,
	s_noTag, 0
    },
    {"dataMissing", true, 35, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"unexpectedDataValue", true, 36, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"pw-RegistrationFailure", true, 37, -1,
	s_noTag, s_pwRegistrationFailureErr,
	s_noTag, 0
    },
    {"negativePW-Check", true, 38, -1,
	s_noTag, 0,
	s_noTag, 0
    },
    {"noRoamingNumberAvailable", true, 39, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"tracingBufferFull", true, 40, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"targetCellOutsideGroupCallArea", true, 42, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"numberOfPW-AttemptsViolation", true, 43, -1,
	s_noTag, 0,
	s_noTag, 0
    },
    {"numberChanged", true, 44, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"busySubscriber", true, 45, -1,
	s_sequenceTag, s_busySubscriberErr,
	s_noTag, 0
    },
    {"noSubscriberReply", true, 46, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"forwardingFailed", true, 47, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"or-NotAllowed", true, 48, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"ati-NotAllowed", true, 49, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"noGroupCallNumberAvailable", true, 50, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"resourceLimitation", true, 51, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"unauthorizedRequestingNetwork", true, 52, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"unauthorizedLCSClient", true, 53, -1,
	s_sequenceTag, s_unauthorizedLcscErr,
	s_noTag, 0
    },
    {"positionMethodFailure", true, 54, -1,
	s_sequenceTag, s_positionMethodFailureErr,
	s_noTag, 0
    },
    {"unknownOrUnreachableLCSClient", true, 58, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"mm-EventNotSupported", true, 59, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"atsi-NotAllowed", true, 60, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"atm-NotAllowed", true, 61, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"informationNotAvailable", true, 62, -1,
	s_sequenceTag, s_extensionContainerRes,
	s_noTag, 0
    },
    {"unknownAlphabet", true, 71, -1,
	s_noTag, 0,
	s_noTag, 0
    },
    {"ussd-Busy", true, 72, -1,
	s_noTag, 0,
	s_noTag, 0
    },
    {"",          false,  0, -1,
	s_noTag,  0,
	s_noTag,  0
    },
};

static const TokenDict s_problemEnum[] = {
    {"unknownOperation",         0x00},
    {"tooLate",                  0x01},
    {"operationNotCancellable",  0x02},
    {"",  0xff},
};

static const Parameter s_cancelFailedErr[] = {
    {"problem",      s_ctxtPrim_0_Tag, false, TcapXApplication::Enumerated, s_problemEnum},
    {"operation",    s_ctxtPrim_1_Tag, false, TcapXApplication::Integer,    0},
    {"",             s_noTag,          false, TcapXApplication::None,       0},
};

static const TokenDict s_requestedInfoEnum[] = {
    {"unknownRequestedInfo",       0x01},
    {"requestedInfoNotAvailable",  0x02},
    {"",  0xff},
};

static const Parameter s_requestedInfoErr[] = {
    {"requestedInfoError",   s_enumTag, false, TcapXApplication::Enumerated, s_requestedInfoEnum},
    {"",                     s_noTag,   false, TcapXApplication::None,       0},
};

static const TokenDict s_systemFailureEnum[] = {
    {"unavailableResources",          0x00},
    {"componentFailure",              0x01},
    {"basicCallProcessingException",  0x02},
    {"resourceStatusFailure",         0x03},
    {"endUserFailure",                0x04},
    {"",  0xff},
};

static const Parameter s_systemFailureCamelErr[] = {
    {"systemFailureError",   s_enumTag, false, TcapXApplication::Enumerated, s_systemFailureEnum},
    {"",                     s_noTag,   false, TcapXApplication::None,       0},
};


static const TokenDict s_taskRefusedEnum[] = {
    {"generic",         0x00},
    {"unobtainable",    0x01},
    {"congestion",      0x02},
    {"",  0xff},
};

static const Parameter s_taskRefusedErr[] = {
    {"taskRefusedError",   s_enumTag, false, TcapXApplication::Enumerated, s_taskRefusedEnum},
    {"",                   s_noTag,   false, TcapXApplication::None,       0},
};


static const Operation s_camelErrors[] = {
    {"cancelled", true, 0, -1,
	s_noTag, 0,
	s_noTag, 0
    },
    {"cancelFailed", true, 1, -1,
	s_sequenceTag, s_cancelFailedErr,
	s_noTag, 0
    },
    {"eTCFailed", true, 3, -1,
	s_noTag, 0,
	s_noTag, 0
    },
    {"improperCallerResponse", true, 4, -1,
	s_noTag, 0,
	s_noTag, 0
    },
    {"missingCustomerRecord", true, 6, -1,
	s_noTag, 0,
	s_noTag, 0
    },
    {"missingParameter", true, 7, -1,
	s_noTag, 0,
	s_noTag, 0
    },
    {"parameterOutOfRange", true, 8, -1,
	s_noTag, 0,
	s_noTag, 0
    },
    {"requestedInfoError", true, 10, -1,
	s_noTag, s_requestedInfoErr,
	s_noTag, 0
    },
    {"systemFailure", true, 11, -1,
	s_noTag, s_systemFailureCamelErr,
	s_noTag, 0
    },
    {"taskRefused", true, 12, -1,
	s_noTag, s_taskRefusedErr,
	s_noTag, 0
    },
    {"unavailableResource", true, 13, -1,
	s_noTag, 0,
	s_noTag, 0
    },
    {"unexpectedComponentSequence", true, 14, -1,
	s_noTag, 0,
	s_noTag, 0
    },
    {"unexpectedDataValue", true, 15, -1,
	s_noTag, 0,
	s_noTag, 0
    },
    {"unexpectedParameter", true, 16, -1,
	s_noTag, 0,
	s_noTag, 0
    },
    {"unknownLegID", true, 17, -1,
	s_noTag, 0,
	s_noTag, 0
    },
    {"",            false,  0,  -1,
	s_noTag,  0,
	s_noTag,  0
    },
};

static const StringList s_netLocUpCtxtOps("updateLocation,forwardCheckSS-Indication,restoreData,insertSubscriberData,activateTraceMode");
static const StringList s_locationCancelCtxtOps("cancelLocation");
static const StringList s_roamingNumberEnqCtxtOps("provideRoamingNumber");
static const StringList s_locationInfoRetrieveCtxtOps("sendRoutingInfo");
static const StringList s_reportingCtxtOps("setReportingState,statusReport,remoteUserFree");
static const StringList s_resetCtxtOps("reset");
static const StringList s_infoRetrieveCtxt2Ops("sendAuthenticationInfo");
static const StringList s_infoRetrieveCtxt1Ops("sendParameters");
static const StringList s_subscriberDataCtxtOps("insertSubscriberData,deleteSubscriberData");
static const StringList s_tracingCtxtOps("activateTraceMode,deactivateTraceMode");
static const StringList s_networkFunctionalSsCtxtOps("registerSS,eraseSS,activateSS,deactivateSS,"
							"interrogateSS,registerPassword,getPassword");
static const StringList s_networkUnstructuredSsCtxt2Ops("processUnstructuredSS-Request,unstructuredSS-Request,unstructuredSS-Notify");
static const StringList s_networkUnstructuredSsCtxt1Ops("processUnstructuredSS-Data");
static const StringList s_shortMsgGatewayCtxtOps("sendRoutingInfoForSM,informServiceCentre");
static const StringList s_shortMsgMOCtxtOps("mo-forwardSM");
static const StringList s_forwardMsgCtxtOps("forwardSM");
static const StringList s_shortMsgAlertCtxtOps("alertServiceCentre");
static const StringList s_mwdMngtCtxtOps("readyForSM");
static const StringList s_shortMsgMTCtxtOps("mt-forwardSM");
static const StringList s_imsiRetrievalCtxtOps("sendIMSI");
static const StringList s_msPurgingCtxtOps("purgeMS");
static const StringList s_subscriberInfoEnquiryCtxOps("provideSubscriberInfo");
static const StringList s_anyTimeInfoEnquiryCtxOps("anyTimeInterrogation");
static const StringList s_gprsLocationUpdateCtxtOps("updateGprsLocation,insertSubscriberData,activateTraceMode");
static const StringList s_gprsLocationInfoRetrieveCtxtOps("sendRoutingInfoForGprs");
static const StringList s_failureReportCtxtOps("failureReport");
static const StringList s_locationSvcGatewayCtxtOps("sendRoutingInfoForLCS");
static const StringList s_authFailureReportCtxtOps("authenticationFailureReport");

static const OpTable s_defMapOpTable = { s_mapOps, 0};

static const AppCtxt s_mapAppCtxt[]= {
    // Network Loc Up context
    {"networkLocUpContext-v3", "0.4.0.0.1.0.1.3", s_netLocUpCtxtOps, &s_defMapOpTable},
    {"networkLocUpContext-v2", "0.4.0.0.1.0.1.2", s_netLocUpCtxtOps, &s_defMapOpTable},
    {"networkLocUpContext-v1", "0.4.0.0.1.0.1.1", s_netLocUpCtxtOps, &s_defMapOpTable},

    // Location Cancellation context
    {"locationCancelationContext-v3", "0.4.0.0.1.0.2.3", s_locationCancelCtxtOps, &s_defMapOpTable},
    {"locationCancelationContext-v2", "0.4.0.0.1.0.2.2", s_locationCancelCtxtOps, &s_defMapOpTable},
    {"locationCancelationContext-v1", "0.4.0.0.1.0.2.1", s_locationCancelCtxtOps, &s_defMapOpTable},

    // Roaming Number Enquiry Context
    {"roamingNumberEnquiryContext-v3", "0.4.0.0.1.0.3.3", s_roamingNumberEnqCtxtOps, &s_defMapOpTable},
    {"roamingNumberEnquiryContext-v2", "0.4.0.0.1.0.3.2", s_roamingNumberEnqCtxtOps, &s_defMapOpTable},
    {"roamingNumberEnquiryContext-v1", "0.4.0.0.1.0.3.1", s_roamingNumberEnqCtxtOps, &s_defMapOpTable},

    // Location Info Retrieval Context
    {"locationInfoRetrievalContext-v3", "0.4.0.0.1.0.5.3", s_locationInfoRetrieveCtxtOps, &s_defMapOpTable},
    {"locationInfoRetrievalContext-v2", "0.4.0.0.1.0.5.2", s_locationInfoRetrieveCtxtOps, &s_defMapOpTable},
    {"locationInfoRetrievalContext-v1", "0.4.0.0.1.0.5.1", s_locationInfoRetrieveCtxtOps, &s_defMapOpTable},

    // Reporting Context
    {"reportingContext-v3", "0.4.0.0.1.0.7.3", s_reportingCtxtOps, &s_defMapOpTable},

    // Reset context
    {"resetContext-v2", "0.4.0.0.1.0.10.2", s_resetCtxtOps, &s_defMapOpTable},
    {"resetContext-v1", "0.4.0.0.1.0.10.1", s_resetCtxtOps, &s_defMapOpTable},

    // Info retrieval context
    {"infoRetrievalContext-v3", "0.4.0.0.1.0.14.3", s_infoRetrieveCtxt2Ops, &s_defMapOpTable},
    {"infoRetrievalContext-v2", "0.4.0.0.1.0.14.2", s_infoRetrieveCtxt2Ops, &s_defMapOpTable},
    {"infoRetrievalContext-v1", "0.4.0.0.1.0.14.1", s_infoRetrieveCtxt1Ops, &s_defMapOpTable},

    // Subscriber Data Management Context
    {"subscriberDataMngtContext-v3", "0.4.0.0.1.0.16.3", s_subscriberDataCtxtOps, &s_defMapOpTable},
    {"subscriberDataMngtContext-v2", "0.4.0.0.1.0.16.2", s_subscriberDataCtxtOps, &s_defMapOpTable},
    {"subscriberDataMngtContext-v1", "0.4.0.0.1.0.16.1", s_subscriberDataCtxtOps, &s_defMapOpTable},

    // Tracing context
    {"tracingContext-v3", "0.4.0.0.1.0.17.3", s_tracingCtxtOps, &s_defMapOpTable},
    {"tracingContext-v2", "0.4.0.0.1.0.17.2", s_tracingCtxtOps, &s_defMapOpTable},
    {"tracingContext-v1", "0.4.0.0.1.0.17.1", s_tracingCtxtOps, &s_defMapOpTable},

    // Network functional SS context
    {"networkFunctionalSsContext-v2", "0.4.0.0.1.0.18.2", s_networkFunctionalSsCtxtOps, &s_defMapOpTable},
    {"networkFunctionalSsContext-v1", "0.4.0.0.1.0.18.1", s_networkFunctionalSsCtxtOps, &s_defMapOpTable},

    // Network unstructured SS context
    {"networkUnstructuredSsContext-v2", "0.4.0.0.1.0.19.2", s_networkUnstructuredSsCtxt2Ops, &s_defMapOpTable},
    {"networkUnstructuredSsContext-v1", "0.4.0.0.1.0.19.1", s_networkUnstructuredSsCtxt1Ops, &s_defMapOpTable},

    // Short message routing
    {"shortMsgGatewayContext-v3", "0.4.0.0.1.0.20.3", s_shortMsgGatewayCtxtOps, &s_defMapOpTable},
    {"shortMsgGatewayContext-v2", "0.4.0.0.1.0.20.2", s_shortMsgGatewayCtxtOps, &s_defMapOpTable},
    {"shortMsgGatewayContext-v1", "0.4.0.0.1.0.20.1", s_shortMsgGatewayCtxtOps, &s_defMapOpTable},

    // Mobile Originated short messages
    {"shortMsgMO-RelayContext-v3", "0.4.0.0.1.0.21.3", s_shortMsgMOCtxtOps, &s_defMapOpTable},
    {"shortMsgMO-RelayContext-v2", "0.4.0.0.1.0.21.2", s_forwardMsgCtxtOps, &s_defMapOpTable},
    {"shortMsgMO-RelayContext-v1", "0.4.0.0.1.0.21.1", s_forwardMsgCtxtOps, &s_defMapOpTable},

    // Short message alerts
    {"shortMsgAlertContext-v2", "0.4.0.0.1.0.23.2", s_shortMsgAlertCtxtOps, &s_defMapOpTable},
    {"shortMsgAlertContext-v1", "0.4.0.0.1.0.23.1", s_shortMsgAlertCtxtOps, &s_defMapOpTable},

    // readyForSM context
    {"mwdMngtContext-v3", "0.4.0.0.1.0.24.3", s_mwdMngtCtxtOps, &s_defMapOpTable},
    {"mwdMngtContext-v2", "0.4.0.0.1.0.24.2", s_mwdMngtCtxtOps, &s_defMapOpTable},
    {"mwdMngtContext-v1", "0.4.0.0.1.0.24.1", s_mwdMngtCtxtOps, &s_defMapOpTable},

    // Mobile Terminated short messages
    {"shortMsgMT-RelayContext-v3", "0.4.0.0.1.0.25.3", s_shortMsgMTCtxtOps, &s_defMapOpTable},
    {"shortMsgMT-RelayContext-v2", "0.4.0.0.1.0.25.2", s_forwardMsgCtxtOps, &s_defMapOpTable},

    // sendIMSI Context
    {"imsiRetrievalContext-v2", "0.4.0.0.1.0.26.2", s_imsiRetrievalCtxtOps, &s_defMapOpTable},

    // MS Purging Context
    {"msPurgingContext-v3", "0.4.0.0.1.0.27.3", s_msPurgingCtxtOps, &s_defMapOpTable},
    {"msPurgingContext-v2", "0.4.0.0.1.0.27.2", s_msPurgingCtxtOps, &s_defMapOpTable},

    // Subscriber Information Enquiry Context
    {"subscriberInfoEnquiryContext-v3", "0.4.0.0.1.0.28.3", s_subscriberInfoEnquiryCtxOps, &s_defMapOpTable},

    // Any Time Info Enquiry Context
    {"anyTimeInfoEnquiryContext-v3", "0.4.0.0.1.0.29.3", s_anyTimeInfoEnquiryCtxOps, &s_defMapOpTable},

    // GPRS Location Update Context
    {"gprsLocationUpdateContext-v3", "0.4.0.0.1.0.32.3", s_gprsLocationUpdateCtxtOps, &s_defMapOpTable},

    // GPRS Location Info Retrieval Context
    {"gprsLocationInfoRetrievalContext-v3" , "0.4.0.0.1.0.33.3", s_gprsLocationInfoRetrieveCtxtOps, &s_defMapOpTable},

    // Failure Report Context
    {"failureReportContext-v3" , "0.4.0.0.1.0.34.3", s_failureReportCtxtOps, &s_defMapOpTable},

    // Location Services Gateway Context
    {"locationSvcGatewayContext-v3", "0.4.0.0.1.0.37.3", s_locationSvcGatewayCtxtOps, &s_defMapOpTable},

    // Authentication Failure Report Context
    {"authenticationFailureReportContext-v3" , "0.4.0.0.1.0.39.3", s_authFailureReportCtxtOps, &s_defMapOpTable},

    {0, 0, s_noOps, 0},
};

static const StringList s_cap2gsmSSFgsmSCFCtxtOps("initialDP,establishTemporaryConnection,connectToResource,"
							    "disconnectForwardConnection,connect,releaseCall,eventReportBCSM,"
							    "requestReportBCSMEvent,applyChargingReport,applyCharging,continue,"
							    "resetTimer,furnishChargingInformation,callInformationReport,"
							    "callInformationRequest,sendChargingInformation,specializedResourceReport,"
							    "playAnnouncement,promptAndCollectUserInformation,cancel,activityTest");
static const StringList s_cap2AssistgsmSSFgsmSCFCtxtOps("assistRequestInstructions,disconnectForwardConnection,connectToResource,"
								"resetTimer,specializedResourceReport,playAnnouncement,"
								"promptAndCollectUserInformation,cancel,activityTest");
static const StringList s_cap2gsmSRFgsmSCFCtxtOps("assistRequestInstructions,specializedResourceReport,playAnnouncement,"
							"promptAndCollectUserInformation,cancel,activityTest");

static OpTable s_defCamelOpTable = { s_camelOps, 0 };

static const AppCtxt s_camelAppCtxt[] = {
    {"CAP-v2-gsmSSF-to-gsmSCF-AC", "0.4.0.0.1.0.50.1", s_cap2gsmSSFgsmSCFCtxtOps, &s_defCamelOpTable},

    {"CAP-v2-assist-gsmSSF-to-gsmSCF-AC", "0.4.0.0.1.0.51.1", s_cap2AssistgsmSSFgsmSCFCtxtOps, &s_defCamelOpTable},

    {"CAP-v2-gsmSRF-to-gsmSCF-AC", "0.4.0.0.1.0.52.1", s_cap2gsmSRFgsmSCFCtxtOps, &s_defCamelOpTable},

    {0, 0, s_noOps, 0}
};

static const StringList s_mapDialogCtxtOps("map-open,map-accept,map-close,map-refuse,map-userAbort,map-providerAbort");

static const AppCtxt s_mapDialogCtxt[] = {
    {"map-DialogueAS", "0.4.0.0.1.1.1.1", s_mapDialogCtxtOps, 0},
    {0, 0, s_noOps, 0}
};

static const Parameter s_mapOpenSeq[] = {
    {"destinationReference",   s_ctxtPrim_0_Tag, true, TcapXApplication::AddressString, 0},
    {"originationReference",   s_ctxtPrim_1_Tag, true, TcapXApplication::AddressString, 0},
    {"extensionContainer",     s_sequenceTag,    true, TcapXApplication::HexString,     0},
    {"",                       s_noTag,          false,TcapXApplication::None,          0},
};

static const TokenDict s_mapRefuseReasonEnum[] = {
    {"noReasonGiven",               0},
    {"invalidDestinationReference", 1},
    {"invalidOriginatingReference", 2},
    {0,0},
};

static const Parameter s_mapRefuseSeq[] = {
    {"reason",                        s_enumTag,        false,   TcapXApplication::Enumerated,    s_mapRefuseReasonEnum},
    {"extensionContainer",            s_sequenceTag,    true,    TcapXApplication::HexString,     0},
    {"alternativeApplicationContext", s_oidTag,         true,    TcapXApplication::OID,           0},
    {"",                              s_noTag,          false,   TcapXApplication::None,          0},
};

static const TokenDict s_rscUnavailableReason[] = {
    {"shortTermResourceLimitation",   0},
    {"longTermResourceLimitation",    1},
    {0,0},
};

static const TokenDict s_procCancelReason[] = {
    {"handoverCancellation",       0},
    {"radioChannelRelease",        1},
    {"networkPathRelease",         2},
    {"callRelease",                3},
    {"associatedProcedureFailure", 4},
    {"tandemDialogueRelease",      5},
    {"remoteOperationsFailure",    6},
    {0,0},
};

static const Parameter s_mapUserAbortChoice[] = {
    {"userSpecificReason",                s_ctxtPrim_0_Tag,     false,  TcapXApplication::Null,         0},
    {"userResourceLimitation",            s_ctxtPrim_1_Tag,     false,  TcapXApplication::Null,         0},
    {"resourceUnavailable",               s_ctxtPrim_2_Tag,     false,  TcapXApplication::Enumerated,   s_rscUnavailableReason},
    {"applicationProcedureCancellation",  s_ctxtPrim_3_Tag,     false,  TcapXApplication::Enumerated,   s_procCancelReason},
    {"",                                  s_noTag,              false,   TcapXApplication::None,        0},
};

static const Parameter s_mapUserAbortSeq[] = {
    {"map-UserAbortChoice",           s_noTag,         false,    TcapXApplication::Choice,       s_mapUserAbortChoice},
    {"extensionContainer",            s_sequenceTag,    true,    TcapXApplication::HexString,     0},
    {"",                              s_noTag,          false,   TcapXApplication::None,          0},
};

static const TokenDict s_providerAbortReason[] = {
    {"abnormalDialogue",   0},
    {"invalidPDU",         1},
    {0,0},
};

static const Parameter s_mapProviderAbortSeq[] = {
    {"map-ProviderAbortReason",       s_enumTag,        false,   TcapXApplication::Enumerated,    s_providerAbortReason},
    {"extensionContainer",            s_sequenceTag,    true,    TcapXApplication::HexString,     0},
    {"",                              s_noTag,          false,   TcapXApplication::None,          0},
};

static const Parameter s_mapDialogChoice[] = {
    {"map-open",           s_ctxtCstr_0_Tag, false,  TcapXApplication::Sequence, s_mapOpenSeq},
    {"map-accept",         s_ctxtCstr_1_Tag, false,  TcapXApplication::Sequence, s_extensionContainerRes},
    {"map-close",          s_ctxtCstr_2_Tag, false,  TcapXApplication::Sequence, s_extensionContainerRes},
    {"map-refuse",         s_ctxtCstr_3_Tag, false,  TcapXApplication::Sequence, s_mapRefuseSeq},
    {"map-userAbort",      s_ctxtCstr_4_Tag, false,  TcapXApplication::Sequence, s_mapUserAbortSeq},
    {"map-providerAbort",  s_ctxtCstr_5_Tag, false,  TcapXApplication::Sequence, s_mapProviderAbortSeq},
    {"",                   s_noTag,          false,  TcapXApplication::None,     0},
};

static const TokenDict s_appStates[] = {
    {"waiting",     TcapXApplication::Waiting},
    {"active",      TcapXApplication::Active},
    {"shutdown",    TcapXApplication::ShutDown},
    {"inactive",    TcapXApplication::Inactive},
    {"", 0},
};

static const TokenDict s_userTypes[] = {
    {"MAP",     TcapXUser::MAP},
    {"CAMEL",   TcapXUser::CAMEL},
    {"", 0},
};

static void replace(String& str, char what, char with)
{
    char* c = (char*)str.c_str();
    while (c && *c) {
	if(*c == what)
	    *c = with;
	c++;
    }
}

static const Operation* findError(TcapXUser::UserType type, int opCode, bool opLocal = true)
{
    DDebug(&__plugin,DebugAll,"findError(opCode=%d, local=%s)",opCode,String::boolText(opLocal));
    const Operation* ops = (type == TcapXUser::MAP ? s_mapErrors : s_camelErrors);
    while (!TelEngine::null(ops->name)) {
	if (ops->code == opCode && ops->local == opLocal)
	    return ops;
	ops++;
    }
    return 0;
}

static const Operation* findError(TcapXUser::UserType type, const String& op)
{
    DDebug(&__plugin,DebugAll,"findError(opCode=%s)",op.c_str());
    const Operation* ops = (type == TcapXUser::MAP ? s_mapErrors : s_camelErrors);
    while (!TelEngine::null(ops->name)) {
	if (op == ops->name)
	    return ops;
	ops++;
    }
    return 0;
}

static bool isAppCtxtOperation(const AppCtxt* ctxt, const Operation* op)
{
    DDebug(&__plugin,DebugAll,"isAppCtxtOperation(ctxt=%s[%p],op=%s[%p]]",(ctxt ? ctxt->name : ""),ctxt,(op ? op->name.c_str() : ""),op);
    if (!ctxt)
	return true;
    return (0 != ctxt->ops.find(op->name));
}

static const Operation* findOperation(TcapXUser::UserType type, int opCode, bool opLocal = true, const AppCtxt* ctxt = 0)
{
    DDebug(&__plugin,DebugAll,"findOperation(type=%s,opCode=%d,local=%s,ctxt=%p)",lookup(type,s_userTypes),opCode,String::boolText(opLocal),ctxt);
    const Operation* ops = 0;
    const OpTable* opTable = (ctxt ? ctxt->opTable : 0);
    do {
	ops = ( opTable ? opTable->mainTable : (type == TcapXUser::MAP ? s_mapOps : s_camelOps));
	while (!TelEngine::null(ops->name)) {
	    if (ops->code == opCode && ops->local == opLocal) {
		if (isAppCtxtOperation(ctxt,ops))
		    return ops;
	    }
	    ops++;
	}
	if (opTable)
	    opTable = opTable->fallbackTable;
    } while (opTable);
    return 0;
}

static const Operation* findOperation(TcapXUser::UserType type, const String& op, const AppCtxt* ctxt = 0)
{
    DDebug(&__plugin,DebugAll,"findOperation(opCode=%s,ctxt=%p)",op.c_str(),ctxt);
    const Operation* ops = 0;
    const OpTable* opTable = (ctxt ? ctxt->opTable : 0);
    do {
	ops = ( opTable ? opTable->mainTable : (type == TcapXUser::MAP ? s_mapOps : s_camelOps));
	while (!TelEngine::null(ops->name)) {
	    if (op == ops->name && isAppCtxtOperation(ctxt,ops))
		return ops;
	    ops++;
	}
	if (opTable)
	    opTable = opTable->fallbackTable;
    } while (opTable);
    return 0;
}

static const Capability* findCapability(TcapXUser::UserType type, const String& opName)
{
    DDebug(&__plugin,DebugAll,"findCapability(opName=%s)",opName.c_str());
    const Capability* cap = (type == TcapXUser::MAP ? s_mapCapab : s_camelCapab);
    while (cap->name) {
	if (cap->ops.find(opName))
	    return cap;
	cap++;
    }
    return 0;
}

static const Capability* findCapabilityOID(TcapXUser::UserType type, const char* oid)
{
    if (!oid)
	return 0;
    DDebug(&__plugin,DebugAll,"findCapabilityOID(oid=%s)",oid);
    const Capability* cap = (type == TcapXUser::MAP ? s_mapCapabOID : s_camelCapabOID);
    while (cap->name) {
	if (cap->ops.find(oid))
	    return cap;
	cap++;
    }
    return 0;
}

static bool findDefCapability(TcapXUser::UserType type, const String& cap)
{
    DDebug(&__plugin,DebugAll,"findDefCapability(opName=%s)",cap.c_str());
    const Capability* caps = (type == TcapXUser::MAP ? s_mapCapab : s_camelCapab);
    while (caps->name) {
	if (cap == caps->name)
	    return true;
	caps++;
    }
    return false;
}

static const AppCtxt* findCtxtFromOid(const String& oid, const AppCtxt* ctxt)
{
    DDebug(&__plugin,DebugAll,"findCtxtFromOid(oid=%s)",oid.c_str());
    while (ctxt && ctxt->name) {
	if (oid == ctxt->oid)
	    return ctxt;
	ctxt++;
    }
    return 0;
}

static const AppCtxt* findCtxtFromStr(const String& oid, const AppCtxt* ctxt)
{
    DDebug(&__plugin,DebugAll,"findCtxtFromStr(ctxt=%s)",oid.c_str());
    while (ctxt && ctxt->name) {
	if (oid == ctxt->name)
	    return ctxt;
	ctxt++;
    }
    return 0;
}


/**
 * IDMap
 */
void IDMap::appendID(const char* tcapID, const char* appID, const AppCtxt* ctxt)
{
    DDebug(&__plugin,DebugAll,"IDMap::appendID(tcapID=%s,appID=%s)",tcapID,appID);
    if (tcapID && appID)
	append(new Transaction(appID,tcapID,ctxt));
}

const String& IDMap::findTcapID(const char* appID)
{
    DDebug(&__plugin,DebugAll,"IDMap::findTcapID(appID=%s)",appID);
    ObjList* obj = find(appID);
    if (obj) {
	NamedString* ns = static_cast<NamedString*>(obj->get());
	if (!TelEngine::null(ns))
	    return (*ns);
    }
    return String::empty();
}

const String& IDMap::findAppID(const char* tcapID)
{
    DDebug(&__plugin,DebugAll,"IDMap::findAppID(tcapID=%s)",tcapID);
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	NamedString* ns = static_cast<NamedString*>(o->get());
	if (!TelEngine::null(ns) && (*ns) == tcapID)
	    return ns->name();
    }
    return String::empty();
}

Transaction* IDMap::findByAppID(const char* appID)
{
    DDebug(&__plugin,DebugAll,"IDMap::findByAppID(appID=%s)",appID);
    ObjList* obj = find(appID);
    if (obj) {
	Transaction* ns = static_cast<Transaction*>(obj->get());
	return ns;
    }
    return 0;
}

Transaction* IDMap::findByTcapID(const char* tcapID)
{
    DDebug(&__plugin,DebugAll,"IDMap::findByTcapID(tcapID=%s)",tcapID);
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	Transaction* ns = static_cast<Transaction*>(o->get());
	if (!TelEngine::null(ns) && (*ns) == tcapID)
	    return ns;
    }
    return 0;
}

/**
 * StringList
 */
StringList::StringList(const char* list, char sep)
{
    DDebug(&__plugin,DebugAll,"StringList(list=%s) [%p]",list,this);
    String str(list);
    m_list = str.split(sep,false);
}

StringList::~StringList()
{
    XDebug(&__plugin,DebugAll,"~StringList() [%p]",this);
    TelEngine::destruct(m_list);
}


/**
 * MyDomParser
 */
MyDomParser::MyDomParser(TcapXApplication* app, const char* name, bool fragment)
    : XmlDomParser(name,fragment),
      m_app(app)
{
    Debug(DebugAll,"MyDomParser created [%p]",this);
}
MyDomParser::~MyDomParser()
{
    Debug(DebugAll,"MyDomParser destroyed [%p]",this);
}

void MyDomParser::gotElement(const NamedList& element, bool empty)
{
    XmlDomParser::gotElement(element,empty);
    if (empty)
	verifyRoot();
}

void MyDomParser::endElement(const String& name)
{
    XmlDomParser::endElement(name);
    verifyRoot();
}

void MyDomParser::verifyRoot()
{
    if (m_app && document() && document()->root() && document()->root()->completed()) {
	m_app->receivedXML(document());
	document()->reset();
    }
}

/**
 * XMLConnection
 */
XMLConnection::XMLConnection(Socket* skt, TcapXApplication* app)
    : Thread("XMLConnection"),
      m_socket(skt), m_app(app),
      m_parser(app,"MyDomParser",false)
{
    Debug(&__plugin,DebugAll,"XMLConnection created with socket=[%p] for application=%s[%p] [%p]",skt,app->toString().c_str(),app,this);
    m_app->ref();
    start();
}

XMLConnection::~XMLConnection()
{
    Debug(&__plugin,DebugAll,"XMLConnection destroyed [%p]",this);
    if (m_socket){
	delete m_socket;
	m_socket = 0;
    }
    TelEngine::destruct(m_app);
}

void XMLConnection::start()
{
    Debug(&__plugin,DebugAll,"XMLConnection start [%p]",this);
    m_socket->setBlocking(false);
}

void XMLConnection::run()
{
    if (!m_socket)
	return;
    char buffer[2048];
    for (;;) {
	if (!(m_socket && m_socket->valid()))
	    break;

	Thread::check();
	bool readOk = false, error = false;
	if (!m_socket->select(&readOk,0,&error,idleUsec()))
	    continue;

	if (!readOk || error) {
	    if (error) {
		if (m_socket->error())
		    Debug(&__plugin,DebugInfo,"XMLConnection[%p] : Reading data error: %s (%d)",this,strerror(m_socket->error()),
			m_socket->error());
		return;
	    }
	    continue;
	}
	int readSize = m_socket->readData(buffer,sizeof(buffer));
	if (!readSize) {
	    if (m_socket->canRetry()) {
		idle(true);
		continue;
	    }
	    return;
	}
	else if (readSize < 0) {
	    if (m_socket->canRetry()) {
		idle(true);
		continue;
	    }
	    cancel();
	    Debug(&__plugin,DebugWarn,"Read error %s(%d) on socket %p in XMLConnection[%p]",strerror(m_socket->error()),
		    m_socket->error(),m_socket,this);
	    break;
	}

	buffer[readSize] = 0;
	XDebug(&__plugin,DebugAll,"READ %d : %s",readSize,buffer);

	if (!m_parser.parse(buffer)) {
	    if (m_parser.error() != XmlSaxParser::Incomplete) {
		Debug(&__plugin,DebugWarn,"Parser error %s in read data [%p] unparsed type %d, buffer = %s, pushed = %s",
		    m_parser.getError(),this,m_parser.unparsed(),m_parser.getBuffer().c_str(),buffer);
		break;
	    }
	}
    }
}

void XMLConnection::cleanup()
{
    DDebug(&__plugin,DebugAll,"XMLConnection::cleanup() [%p]",this);
    m_app->setIO(0);
}

bool XMLConnection::writeData(XmlFragment* elem)
{
    if (!elem)
	return false;

    String xml;
    elem->toString(xml,true);
    XDebug(&__plugin,DebugAll,"WRITE : %s",xml.c_str());
    int len = xml.length();
    const char* buffer = xml.c_str();
    while (m_socket && (len > 0)) {
	bool writeOk = false,error = false;
	if (!m_socket->select(0,&writeOk,&error,idleUsec()) || error) {
	    if (!m_socket->canRetry())
		return false;
	    continue;
	}
	if (!writeOk )
	    continue;
	int w = m_socket->writeData(buffer,len);
	if (w < 0) {
	    if (!m_socket->canRetry()) {
		Debug(&__plugin,DebugWarn,"XMLConnection::writeData(xml=[%p]) [%p] on socket [%p] could not write error : %s",
		    elem,this,m_socket,strerror(m_socket->error()));
		cancel();
		return false;
	    }
	}
	else {
	    buffer += w;
	    len -= w;
	}
    }
    return true;
}



/**
 * XMLConnListener
 */
XMLConnListener::XMLConnListener(TcapXUser* user, const NamedList& sect)
    : Thread("XMLConnListener"),
      m_user(user)
{
    Debug(&__plugin,DebugAll,"XMLConnListener [%p] created",this);
    m_host = sect.getValue(YSTRING("host"),"127.0.0.1");
    m_port = sect.getIntValue(YSTRING("port"),5555);
}

XMLConnListener::~XMLConnListener()
{
    Debug(&__plugin,DebugAll,"XMLConnListener [%p] destroyed",this);
    m_user = 0;
}

bool XMLConnListener::init()
{
    SocketAddr addr;

    if (!addr.assign(AF_INET) || !addr.host(m_host) || !addr.port(m_port)) {
	Debug(m_user,DebugWarn,"Could not assign address=%s:%d for user listener=%s [%p]",m_host.c_str(),m_port,
	    m_user->toString().c_str(),this);
	return false;
    }

    if (!m_socket.create(addr.family(),SOCK_STREAM)) {
	Debug(m_user,DebugWarn, "Could not create socket for user listener=%s [%p] error %d: %s",
	    m_user->toString().c_str(),this,m_socket.error(),strerror(m_socket.error()));
	return false;
    }

    m_socket.setReuse();

    if (!m_socket.bind(addr)) {
	Debug(m_user,DebugWarn,"Could not bind user listener=%s [%p] error %d: %s",
	    m_user->toString().c_str(),this,m_socket.error(),strerror(m_socket.error()));
	return false;
    }
    if (!m_socket.setBlocking(false) || !m_socket.listen())
	return false;

    return startup();
}

void XMLConnListener::run()
{
    for (;;) {
	Thread::check();
	idle();
	SocketAddr address;
	Socket* newSocket = m_socket.accept(address);
	if (!newSocket) {
	    if (m_socket.canRetry())
		continue;
	    Debug(m_user,DebugWarn,"Accept error: %s",strerror(m_socket.error()));
	    break;
	}
	else {
	    String addr(address.host());
	    addr << ":" << address.port();
	    if (!createConn(newSocket,addr))
		Debug(m_user,DebugInfo,"Connection from %s rejected",addr.c_str());
	}
    }
}

bool XMLConnListener::createConn(Socket* skt, String& addr)
{
    if (!skt->valid()) {
	delete skt;
	return false;
    }
    if (!skt->setBlocking(false)) {
	Debug(m_user,DebugGoOn, "Failed to set TCP socket to nonblocking mode: %s",
	    strerror(skt->error()));
	delete skt;
	return false;
    }
    if (m_user && !m_user->createApplication(skt,addr)) {
	delete skt;
	return false;
    }
    return true;
}

void XMLConnListener::cleanup()
{
    DDebug(m_user,DebugAll,"XMLConnListener::cleanup() [%p]",this);
    m_user->setListener(0);
}

/**
 * TcapToXml
 */
const XMLMap TcapToXml::s_xmlMap[] = {
    {Regexp("^state$"),                                        "state",                                 "",                          TcapToXml::Value},
    {Regexp("^error$"),                                        "error",                                 "",                          TcapToXml::Value},
    {Regexp("^LocalPC$"),                                      "transport.mtp",                         "",                          TcapToXml::Element},
    {Regexp("^RemotePC$"),                                     "transport.mtp",                         "",                          TcapToXml::Element},
    {Regexp("^sls$"),                                          "transport.mtp",                         "",                          TcapToXml::Element},
    {Regexp("^ReturnCause$"),                                  "transport.sccp",                        "ReturnCause",               TcapToXml::Element},
    {Regexp("^HopCounter$"),                                   "transport.sccp",                        "HopCounter",                TcapToXml::Element},
    {Regexp("^CallingPartyAddress\\.gt\\.encoding$"),          "transport.sccp.CallingPartyAddress.gt", "encoding",                  TcapToXml::Attribute},
    {Regexp("^CallingPartyAddress\\.gt\\.plan$"),              "transport.sccp.CallingPartyAddress.gt", "plan",                      TcapToXml::Attribute},
    {Regexp("^CallingPartyAddress\\.gt\\.nature$"),            "transport.sccp.CallingPartyAddress.gt", "nature",                    TcapToXml::Attribute},
    {Regexp("^CallingPartyAddress\\.gt\\.translation$"),       "transport.sccp.CallingPartyAddress.gt", "translation",               TcapToXml::Attribute},
    {Regexp("^CallingPartyAddress\\.gt$"),                     "transport.sccp.CallingPartyAddress",    "gt",                        TcapToXml::Element},
    {Regexp("^CallingPartyAddress\\.ssn$"),                    "transport.sccp.CallingPartyAddress",    "ssn",                       TcapToXml::Element},
    {Regexp("^CallingPartyAddress\\.route$"),                  "transport.sccp.CallingPartyAddress",    "route",                     TcapToXml::Element},
    {Regexp("^CallingPartyAddress\\.pointcode$"),              "transport.sccp.CallingPartyAddress",    "pointcode",                 TcapToXml::Element},
    {Regexp("^CallingPartyAddress\\..\\+$"),                   "transport.sccp.CallingPartyAddress",    "",                          TcapToXml::Element},
    {Regexp("^CalledPartyAddress\\.gt\\.encoding$"),           "transport.sccp.CalledPartyAddress.gt",  "encoding",                  TcapToXml::Attribute},
    {Regexp("^CalledPartyAddress\\.gt\\.plan$"),               "transport.sccp.CalledPartyAddress.gt",  "plan",                      TcapToXml::Attribute},
    {Regexp("^CalledPartyAddress\\.gt\\.nature$"),             "transport.sccp.CalledPartyAddress.gt",  "nature",                    TcapToXml::Attribute},
    {Regexp("^CalledPartyAddress\\.gt\\.translation$"),        "transport.sccp.CalledPartyAddress.gt",  "translation",               TcapToXml::Attribute},
    {Regexp("^CalledPartyAddress\\.gt$"),                      "transport.sccp.CalledPartyAddress",     "gt",                        TcapToXml::Element},
    {Regexp("^CalledPartyAddress\\.ssn$"),                     "transport.sccp.CalledPartyAddress",     "ssn",                       TcapToXml::Element},
    {Regexp("^CalledPartyAddress\\.route$"),                   "transport.sccp.CalledPartyAddress",     "route",                     TcapToXml::Element},
    {Regexp("^CalledPartyAddress\\.pointcode$"),               "transport.sccp.CalledPartyAddress",     "pointcode",                 TcapToXml::Element},
    {Regexp("^CalledPartyAddress\\..\\+$"),                    "transport.sccp.CalledPartyAddress",     "",                          TcapToXml::Element},
    {Regexp("^tcap\\.request\\.type$"),                        "transport.tcap",                        "request-type",              TcapToXml::Element},
    {Regexp("^tcap\\.transaction\\.localTID$"),                "transport.tcap",                        "localTID",                  TcapToXml::Element},
    {Regexp("^tcap\\.transaction\\.remoteTID$"),               "transport.tcap",                        "remoteTID",                 TcapToXml::Element},
    {Regexp("^tcap\\.transaction\\.abort\\.cause$"),           "transport.tcap",                        "abort-cause",               TcapToXml::Element},
    {Regexp("^tcap\\.transaction\\.abort\\.information$"),     "transport.tcap",                        "abort-information",         TcapToXml::Element},
    {Regexp("^tcap\\.transaction\\..\\+$"),                    "transport.tcap",                        "",                          TcapToXml::Element},
    {Regexp("^tcap\\.dialogPDU\\.application-context-name$"),  "application",                           "",                          TcapToXml::Value},
    {Regexp("^tcap\\.dialogPDU\\.dialog-pdu-type$"),           "transport.tcap.dialog",                 "type",                      TcapToXml::Attribute},
    {Regexp("^tcap\\.dialogPDU\\.protocol-version$"),          "transport.tcap.dialog",                 "version",                   TcapToXml::Attribute},
    {Regexp("^tcap\\.dialogPDU\\.abort-source$"),              "transport.tcap.dialog",                 "abort-source",              TcapToXml::Element},
    {Regexp("^tcap\\.dialogPDU\\.result$"),                    "transport.tcap.dialog",                 "result",                    TcapToXml::Element},
    {Regexp("^tcap\\.dialogPDU\\.result-source-diagnostic$"),  "transport.tcap.dialog",                 "result-source-diagnostic",  TcapToXml::Element},
    {Regexp("^tcap\\.dialogPDU\\.userInformation\\.direct-reference$"), "transport.tcap.dialog.userInformation", "direct-reference", TcapToXml::Element},
    {Regexp("^tcap\\.dialogPDU\\.userInformation\\.encoding-contents$"),"transport.tcap.dialog.userInformation", "encoding-contents",TcapToXml::Element},
    {Regexp("^tcap\\.dialogPDU\\.userInformation\\.encoding-type$"),    "transport.tcap.dialog.userInformation", "encoding-type",    TcapToXml::Element},
    {Regexp("^tcap\\.dialogPDU\\.userInformation\\..\\+$"),    "transport.tcap.dialog.userInformation", "",                          TcapToXml::Element},
    {Regexp("^tcap\\.component\\.count$"),                     "",                                      "",                          TcapToXml::None},
    {Regexp("^tcap\\.component\\..\\+\\.localCID$"),           "component",                             "",                          TcapToXml::Attribute},
    {Regexp("^tcap\\.component\\..\\+\\.remoteCID$"),          "component",                             "",                          TcapToXml::Attribute},
    {Regexp("^tcap\\.component\\..\\+\\.componentType$"),      "component",                             "type",                      TcapToXml::Attribute},
    {Regexp("^tcap\\.component\\..\\+\\.operationCode$"),      "component",                             "",                          TcapToXml::Attribute},
    {Regexp("^tcap\\.component\\..\\+\\.operationCodeType$"),  "",                                      "",                          TcapToXml::None},
    {Regexp("^tcap\\.component\\..\\+\\.errorCode$"),          "component",                             "",                          TcapToXml::Attribute},
    {Regexp("^tcap\\.component\\..\\+\\.errorCodeType$"),      "",                                      "",                          TcapToXml::None},
    {Regexp("^tcap\\.component\\..\\+\\.problemCode$"),        "component",                             "",                          TcapToXml::Attribute},
    {Regexp("^tcap\\.component\\..\\+\\.operationClass$"),     "component",                             "",                          TcapToXml::Attribute},
    {Regexp("^tcap\\.component\\..\\+\\..\\+"),                "component",                             "",                          TcapToXml::NewElement},
    {Regexp(""),                                               "",                                      "",                          TcapToXml::End},
};


TcapToXml::TcapToXml(TcapXApplication* app)
    : Mutex(false,"TcapToXml"),
      m_app(app), m_type(Unknown)
{
    DDebug(&__plugin,DebugAll,"TcapToXml created for application=%s[%p] [%p]",m_app->toString().c_str(),m_app,this);
}

TcapToXml::~TcapToXml()
{
    DDebug(&__plugin,DebugAll,"TcapToXml destroyed [%p]",this);
    reset();
}

void TcapToXml::reset()
{
    m_type = Unknown;
}

const XMLMap* TcapToXml::findMap(String& what)
{
    XDebug(&__plugin,DebugAll,"TcapToXml::findMap(%s) [%p]",what.c_str(),this);
    const XMLMap* map = s_xmlMap;
    while (map->type != End) {
	if (what.matches(map->name))
	    return map;
	map++;
    }
    return 0;
}

bool TcapToXml::buildXMLMessage(NamedList& params, XmlFragment* msg, MsgType type, const AppCtxt* ctxt)
{
    DDebug(&__plugin,DebugAll,"TcapToXML::buildXMLMessage() [%p]",this);
    if (!msg)
	return false;
    XmlDeclaration* decl = new XmlDeclaration();
    msg->addChild(decl);
    XmlElement* el = new XmlElement(s_msgTag);
    el->setXmlns("",true,s_namespace);
    msg->addChild(el);

    AppCtxt* appCtxt = 0;
    NamedString* ctxtStr = params.getParam(s_tcapAppCtxt);
    if (!TelEngine::null(ctxtStr)) {
	const AppCtxt* contexts = (m_app->type() == TcapXUser::MAP ? s_mapAppCtxt : s_camelAppCtxt);
	appCtxt = (AppCtxt*)findCtxtFromOid(*ctxtStr,contexts);
    }
    if (appCtxt)
	params.setParam(s_tcapAppCtxt,appCtxt->name);

    // translate P-Abort causes
    NamedString* param = params.getParam(s_tcapAbortCause);
    if (!TelEngine::null(param) && (*param) == "pAbort") {
	int code = params.getIntValue(s_tcapAbortInfo);
	params.setParam(s_tcapAbortInfo,lookup(code,SS7TCAPError::s_errorTypes,*param));
    }

    if (m_app->type() == TcapXUser::MAP && !TelEngine::null(params.getParam(s_tcapDirectReference)))
	handleMAPDialog(el,params);

    for (unsigned int i = 0; i < params.count(); i++) {
	NamedString* ns = params.getParam(i);
	if (TelEngine::null(ns))
	    continue;
	if (ns->name().startsWith(s_tcapCompPrefixSep))
	    continue;
	const XMLMap* map = findMap((String&)ns->name());
	if (!map)
	    continue;
	addToXml(el,map,ns);
    }

    addComponentsToXml(el,params,ctxt);
    return true;
}

void TcapToXml::handleMAPDialog(XmlElement* root, NamedList& params)
{
    if (!root)
	return;
    XDebug(&__plugin,DebugAll,"TcapToXml::handleMAPDialog(root=%p) [%p]",root,this);
    NamedString* param = params.getParam(s_tcapDirectReference);
    if (TelEngine::null(param))
	return;
    const AppCtxt* mapCtxt = findCtxtFromOid(*param,s_mapDialogCtxt);
    // we don't know the context, leave it alone
    if (!mapCtxt)
	return;
    if (mapCtxt)
	params.setParam(s_tcapDirectReference,mapCtxt->name);
    param = params.getParam(s_tcapEncodingContent);
    if (TelEngine::null(param))
	return;
    const XMLMap* map = findMap((String&)s_tcapEncodingContent);
    if (!map)
	return;
    XmlElement* parent = addToXml(root,map,&s_encodingPath);
    if (!parent)
	return;
    DataBlock db;
    db.unHexify(param->c_str(),param->length(),' ');
    if (decodeDialogPDU(parent,mapCtxt,db)) {
	params.clearParam(s_tcapEncodingContent);
    }
}

bool TcapToXml::decodeDialogPDU(XmlElement* el, const AppCtxt* ctxt, DataBlock& data)
{
    if (!(el && ctxt))
	return false;
    XDebug(&__plugin,DebugAll,"TcapToXml::decodeDialogPDU(el=%p) [%p]",el,this);
    const Parameter* param = s_mapDialogChoice;
    int err = TcapXApplication::NoError;
    while (param && param->name) {
	AsnTag tag;
	AsnTag::decode(tag,data);
	if (decodeParam(param,tag,data,el,m_app->addEncoding(),err)) {
	    bool ok = (0 != ctxt->ops.find(param->name));
	    if (!ok)
		el->clearChildren();
	    return ok;
	}
	param++;
    }
    if (data.length())
	decodeRaw(el,data);
    return true;
}

XmlElement* TcapToXml::addToXml(XmlElement* root, const XMLMap* map, NamedString* val)
{
    if (!(root && map && val))
	return 0;
    if (map->type == None)
	return 0;
    XDebug(&__plugin,DebugAll,"TcapToXml::addToXml(frag=%p, map=%s[%p], val=%s[%p]) [%p]",root,map->map,map,val->name().c_str(),val,this);
    String mapStr = map->map;
    ObjList* path = mapStr.split('.',false);
    XmlElement* parent = root;

    for (ObjList* o = path->skipNull(); o; o = o->skipNext()) {
	String* elem = static_cast<String*>(o->get());
	if (TelEngine::null(elem))
	    continue;
	XmlElement* child = parent->findFirstChild(elem);
	if (!child) {
	    child = new XmlElement(*elem);
	    parent->addChild(child);
	}
	parent = child;
    }

    if (parent) {
	// add to parent child element if map is elem, or attr if map si attribute
	XmlElement* child = 0;

	String tag;
	if (TelEngine::null(map->tag)) {
	    tag = val->name();
	    if (tag.find('.') > -1 && tag.startsWith(map->name))
		tag.startSkip(map->name,false);
	    replace(tag,'.','-');
	}
	else
	    tag = map->tag;
	switch (map->type) {
	    case Element:
		child = parent->findFirstChild(&tag);
		if (child) {
		    child->addText(*val);
		    parent = child;
		    break;
		}
		// fall through and create new child if not found
	    case NewElement:
		child = new XmlElement(tag);
		child->addText(*val);
		parent->addChild(child);
		parent = child;
		break;
	    case Value:
		parent->addText(*val);
		break;
	    case Attribute:
		parent->setAttributeValid(tag,*val);
		break;
	    case None:
	    default:
		break;
	}
    }
    TelEngine::destruct(path);
    return parent;
}

void TcapToXml::addComponentsToXml(XmlElement* el, NamedList& params, const AppCtxt* ctxt)
{
    if (!el)
	return;
    DDebug(&__plugin,DebugAll,"TcapToXml::addComponentsToXml(el=%p, params=%p) [%p]",el,&params,this);
    int count = params.getIntValue(s_tcapCompCount);
    for (int i = 1; i <= count; i++) {
	XmlElement* comp = new XmlElement(s_component);
	el->addChild(comp);

	String root;
	root << s_tcapCompPrefix << "." << String(i);
	NamedList compParams("");
	compParams.copyParam(params,root,'.');

	NamedString* opCode = params.getParam(root + "." + s_tcapOpCode);
	NamedString* opType = params.getParam(root + "." + s_tcapOpCodeType);
	Operation* op = 0;
	if (!TelEngine::null(opCode) && !TelEngine::null(opType))
	    op = (Operation*)findOperation(m_app->type(),opCode->toInteger(),(*opType == "local"),ctxt);
	if (op) {
	    compParams.setParam(root + "." + s_tcapOpCode,op->name);
	    if (op->opClass > -1)
		compParams.setParam(root + "." + s_tcapOpClass,lookup(op->opClass,SS7TCAP::s_compOperClasses,"reportAll"));
	}

	int compType = SS7TCAP::lookupComponent(params.getValue(root + "." + s_tcapCompType));

	bool searchArgs = true;
	switch (compType) {
	    case SS7TCAP::TC_Invoke:
	    case SS7TCAP::TC_ResultLast:
	    case SS7TCAP::TC_ResultNotLast:
		searchArgs = (compType == SS7TCAP::TC_Invoke);
		break;
	    case SS7TCAP::TC_U_Error:
		opCode = params.getParam(root + "." + s_tcapErrCode);
		opType = params.getParam(root + "." + s_tcapErrCodeType);
		if (!TelEngine::null(opCode) && !TelEngine::null(opType))
		    op = (Operation*)findError(m_app->type(),opCode->toInteger(),(*opType == "local"));
		if (op)
		    compParams.setParam(root + "." + s_tcapErrCode,op->name);
		break;
	    case SS7TCAP::TC_R_Reject:
	    case SS7TCAP::TC_U_Reject:
	    case SS7TCAP::TC_L_Reject:
		    compParams.setParam(root + "." + s_tcapProblemCode,
			    lookup(params.getIntValue(root + "." + s_tcapProblemCode),SS7TCAPError::s_errorTypes));
		break;
	    default:
		break;
	}
	for (unsigned int j = 0; j < compParams.count(); j++) {
	    NamedString* ns = compParams.getParam(j);
	    if (TelEngine::null(ns))
		continue;

	    const XMLMap* map = findMap((String&)ns->name());
	    if (!map)
		continue;
	    XmlElement* child;
	    int pos = ns->name().rfind('.');
	    String tag = ns->name().substr(pos + 1);
	    tag = (TelEngine::null(map->tag) ? tag : (String)map->tag);
	    switch (map->type) {
		case Element:
		    child = new XmlElement(tag);
		    child->addText(*ns);
		    comp->addChild(child);
		    break;
		case Value:
		    comp->addText(*ns);
		    break;
		case Attribute:
		    comp->setAttributeValid(tag,*ns);
		    break;
		case None:
		default:
		    break;
	    }
	}
	NamedString* payloadHex = params.getParam(root);
	if (TelEngine::null(payloadHex))
	    continue;
	addParametersToXml(comp,*payloadHex,op,searchArgs);
    }
}

void TcapToXml::addParametersToXml(XmlElement* elem, String& payloadHex, Operation* op, bool searchArgs)
{
    if (!elem)
	return;
    DDebug(&__plugin,DebugAll,"TcapToXml::addParametersToXml(elem=%s[%p], payload=%s, op=%s[%p], searchArgs=%s) [%p]",
	    elem->getTag().c_str(),elem,payloadHex.c_str(),(op ? op->name.c_str() : ""),op,String::boolText(searchArgs),this);

    DataBlock data;
    if (!data.unHexify(payloadHex.c_str(),payloadHex.length(),' ')) {
	DDebug(&__plugin,DebugAll,"TcapToXml::addParamtersToXml() invalid hexified payload=%s [%p]",payloadHex.c_str(),this);
	return;
    }
    if (elem->getTag() == s_component) {
	AsnTag tag = (op ? (searchArgs ? op->argTag : op->retTag) : s_noTag);
	AsnTag decTag;
	AsnTag::decode(decTag,data);
	if (op && tag != decTag) {
	    if (tag != s_noTag)
		op = 0;
	}
	if (decTag.type() == AsnTag::Constructor && tag == decTag) { // initial constructor
	    data.cut(-(int)decTag.coding().length());
	    int len = ASNLib::decodeLength(data);
	    if (len != (int)data.length())
		return;
	}

    }
    decodeTcapToXml(elem,data,op,0,searchArgs);
}

void TcapToXml::decodeTcapToXml(XmlElement* elem, DataBlock& data, Operation* op, unsigned int index, bool searchArgs)
{
    DDebug(&__plugin,DebugAll,"TcapToXml::decodeTcapToXml(elem=%s[%p],op=%s[%p], searchArgs=%s) [%p]",
	    elem->getTag().c_str(),elem,(op ? op->name.c_str() : ""),op,String::boolText(searchArgs),this);
    if (!data.length() || !elem)
	return;

    if (op)
	decodeOperation(op,elem,data,searchArgs);
    else
	decodeRaw(elem,data);
}

bool TcapToXml::decodeOperation(Operation* op, XmlElement* elem, DataBlock& data, bool searchArgs)
{
    if (!(op && elem && m_app))
	return false;

    const Parameter* param = (searchArgs ? op->args : op->res);
    AsnTag opTag = (searchArgs ? op->argTag : op->retTag);
    int err = TcapXApplication::NoError;
    while (param && !TelEngine::null(param->name)) {
	AsnTag tag;
	AsnTag::decode(tag,data);
	if (!decodeParam(param,tag,data,elem,m_app->addEncoding(),err)) {
	    if (!param->isOptional && (err != TcapXApplication::DataMissing)) {
		if (opTag == s_noTag) {
		    const Parameter* tmp = param;
		    if ((++tmp) && TelEngine::null(tmp->name))
			printMissing(param->name.c_str(),elem->tag(),false);
		}
		else
		    printMissing(param->name.c_str(),elem->tag(),false);
	    }
	}
	else if (opTag == s_noTag) // should be only one child
		break;
	param++;
    }
    if (data.length())
	decodeRaw(elem,data);
    return true;
}

/**
 * XmlToTcap
 */

const TCAPMap XmlToTcap::s_tcapMap[] = {
    {"c",                                                 false,   ""},
    {"transport.mtp.",                                    true,    ""},
    {"transport.sccp.CallingPartyAddress.gt.",            true,   "CallingPartyAddress.gt"},
    {"transport.sccp.CallingPartyAddress.",               true,   "CallingPartyAddress"},
    {"transport.sccp.CalledPartyAddress.gt.",             true,   "CalledPartyAddress.gt"},
    {"transport.sccp.CalledPartyAddress.",                true,   "CalledPartyAddress"},
    {"transport.sccp.",                                   true,   ""},
    {"transport.tcap.request-type",                       false,  "tcap.request.type"},
    {"transport.tcap.abort-cause",                        false,  "tcap.transaction.abort.cause"},
    {"transport.tcap.abort-information",                  false,  "tcap.transaction.abort.information"},
    {"transport.tcap.dialog.type",                        false,  "tcap.dialogPDU.dialog-pdu-type"},
    {"transport.tcap.dialog.version",                     false,  "tcap.dialogPDU.protocol-version"},
    {"transport.tcap.dialog.userInformation",             true,   "tcap.dialogPDU.userInformation"},
    {"transport.tcap.dialog.",                            true,   "tcap.dialogPDU"},
    {"transport.tcap.",                                   true,   "tcap.transaction"},
    {"application",                                       false,  "tcap.dialogPDU.application-context-name"},
    {0, ""},
};

XmlToTcap::XmlToTcap(TcapXApplication* app)
    : Mutex(false,"XmlToTcap"),
      m_app(app), m_decl(0), m_elem(0)
{
    DDebug(&__plugin,DebugAll,"XmlToTcap created for application=%s[%p] [%p]",m_app->toString().c_str(),m_app,this);
}
XmlToTcap::~XmlToTcap()
{
    DDebug(&__plugin,DebugAll,"XmlToTcap destroyed [%p]",this);
    reset();
}

void XmlToTcap::reset()
{
    m_elem = 0;
    m_decl = 0;
    m_type = Unknown;
}

const TCAPMap* XmlToTcap::findMap(String& path)
{
    XDebug(&__plugin,DebugAll,"XmlToTcap::findMap(%s) [%p]",path.c_str(),this);
    const TCAPMap* map = s_tcapMap;
    while (map && map->path) {
	if (path == map->path || path.startsWith(map->path))
	    return map;
	map++;
    }
    return 0;
}

bool XmlToTcap::validDeclaration()
{
    Lock l(this);
    if (!m_decl)
	return false;
    DDebug(&__plugin,DebugAll,"XmlToTcap::validDeclaration() [%p]",this);
    const NamedList& decl = m_decl->getDec();
    NamedString* vers = decl.getParam(YSTRING("version"));
    NamedString* enc = decl.getParam(YSTRING("encoding"));
    if ((!TelEngine::null(vers) && (*vers != "1.0")) || (!TelEngine::null(enc) && (*enc |= "UTF-8")))
	return false;
    return true;
}

bool XmlToTcap::checkXmlns()
{
    Lock l(this);
    if (!m_elem)
	return false;
    String* xmlns = m_elem->xmlns();
    if (TelEngine::null(xmlns)) {
	if (m_app->state() == TcapXApplication::Waiting)
	    return false;
    }
    else {
	if (s_namespace != *xmlns)
	    return false;
    }
    return true;
}


bool XmlToTcap::valid(XmlDocument* doc)
{
    Lock l(this);
    if (!doc)
	return false;
    DDebug(&__plugin,DebugAll,"XmlToTcap::valid() [%p]",this);
    reset();
    m_decl = doc->declaration();
    m_elem = doc->root();
    if (!(m_elem && m_elem->getTag() == s_msgTag))
	return false;
    return true;
}

void XmlToTcap::encodeOperation(Operation* op, XmlElement* elem, DataBlock& payload, int& err, bool searchArgs)
{
    if (!(op && elem))
	return;
    const Parameter* param = (searchArgs ? op->args : op->res);
    AsnTag opTag = (searchArgs ? op->argTag : op->retTag);
    while (param && !TelEngine::null(param->name)) {
	DataBlock db;
	err = TcapXApplication::NoError;
	if (!encodeParam(param,db,elem,err)) {
	    if (!param->isOptional && (err != TcapXApplication::DataMissing)) {
		if (opTag == s_noTag) {
		    const Parameter* tmp = param;
		    if ((++tmp) && TelEngine::null(tmp->name))
			printMissing(param->name.c_str(),elem->tag());
		}
		else
		    printMissing(param->name.c_str(),elem->tag());
	    }
	}
	else {
	    payload.append(db);
	    if (opTag == s_noTag)
		break;
	}
	param++;
    }
    XmlElement* child = elem->pop();
    while (child) {
	DataBlock db;
	encodeRaw(param,db,child,err);
	payload.append(db);
	TelEngine::destruct(child);
	child = elem->pop();
    }
    return;
}

bool XmlToTcap::encodeComponent(DataBlock& payload, XmlElement* elem, bool searchArgs, int& err, Operation* op)
{
    DDebug(&__plugin,DebugAll,"XmlToTcap::encodeComponent(elem=%p op=%p) [%p]",elem,op,this);
    if (!elem)
	return false;

    if (op)
	encodeOperation(op,elem,payload,err,searchArgs);
    else if (elem->hasChildren())
	encodeRaw(0,payload,elem,err);

    if (elem->getTag() == s_component) {
	AsnTag tag = ( op ? (searchArgs ? op->argTag : op->retTag) : s_noTag);
	if (tag != s_noTag) {
	    payload.insert(ASNLib::buildLength(payload));
	    payload.insert(tag.coding());
	}
    }
    return true;
}

bool XmlToTcap::handleComponent(NamedList& tcapParams, XmlElement* elem, const AppCtxt* appCtxt)
{
    DDebug(&__plugin,DebugAll,"XMLToTcap::handleComponent(params=%p, elem=%p) [%p]",&tcapParams,elem,this);
    if (!elem && elem->getTag() == s_component)
	return false;
    unsigned int index = tcapParams.getIntValue(s_tcapCompCount) + 1;
    String prefix = s_tcapCompPrefix;
    prefix << "." << index;

    int type = 0;
    const NamedList& comp = elem->attributes();
    NamedString* opName = 0;
    NamedString* errName = 0;
    for (unsigned int i = 0; i < comp.length(); i++) {
	NamedString* ns = comp.getParam(i);
	if (TelEngine::null(ns))
	    continue;

	if (ns->name() == s_typeStr) {
	    tcapParams.setParam(prefix + "." + s_tcapCompType,*ns);
	    type = SS7TCAP::lookupComponent(*ns);
	}
	else if (ns->name() == s_tcapOpCode)
	    opName = ns;
	else if (ns->name() == s_tcapErrCode)
	    errName = ns;
	else if (ns->name() == s_tcapProblemCode)
	    tcapParams.setParam(prefix + "." + s_tcapProblemCode,String(lookup(*ns,SS7TCAPError::s_errorTypes)));
	else
	    tcapParams.setParam(prefix + "." + ns->name(),*ns);
    }

    tcapParams.setParam(s_tcapCompCount,String(index));

    Operation* op = 0;
    if (!type) {
	Debug(&__plugin,DebugWarn,"Trying to encode component with index='%u' without component type",index);
	return true;
    }
    if (type == SS7TCAP::TC_Invoke || type == SS7TCAP::TC_ResultLast || type == SS7TCAP::TC_ResultNotLast) {
	if (opName) {
	    op = (Operation*)findOperation(m_app->type(),*opName,appCtxt);
	    if (!op)
		Debug(&__plugin,DebugMild,"Cannot find operation='%s' in ctxt='%s' [%p]",opName->c_str(),(appCtxt ? appCtxt->name : ""),this);
	    else {
		tcapParams.setParam(prefix + "." + s_tcapOpCode,String(op->code));
		tcapParams.setParam(prefix + "." + s_tcapOpCodeType,(op->local ? "local" : "global"));
	    }
	}
	else {
	    if (type == SS7TCAP::TC_Invoke) {
		Debug(&__plugin,DebugWarn,"Trying to encode Invoke component with index='%u' without operationCode",index);
		return true;
	    }
	}
    }
    else if (type == SS7TCAP::TC_U_Error) {
	if (!errName) {
	    Debug(&__plugin,DebugWarn,"Trying to encode U_Error component with index='%u' without errorCode",index);
	    return true;
	}
	op = (Operation*)findError(m_app->type(),*errName);
	if (!op)
	    Debug(&__plugin,DebugMild,"Cannot find error='%s' [%p]",errName->c_str(),this);
	else {
	    tcapParams.setParam(prefix + "." + s_tcapErrCode,String(op->code));
	    tcapParams.setParam(prefix + "." + s_tcapErrCodeType,(op->local ? "local" : "global"));
	}
    }

    DataBlock payload;
    bool searchArgs = (type == SS7TCAP::TC_Invoke || type == SS7TCAP::TC_U_Error ? true : false);

    int err = TcapXApplication::NoError;
    if (!encodeComponent(payload,elem,searchArgs,err,op))
	return false;

    String str;
    str.hexify(payload.data(),payload.length(),' ');
    tcapParams.setParam(prefix,str);

    return true;
}

bool XmlToTcap::handleMAPDialog(NamedList& tcapParams, XmlElement* elem, String prefix)
{
    DDebug(&__plugin,DebugAll,"XMLToTcap::handleMAPDialog(params=%p, elem=%p, prefix=%s) [%p]",&tcapParams,elem,prefix.c_str(),this);
    if (!elem && elem->getTag() != s_userInformation)
	return false;

    XmlElement* content = elem->findFirstChild(&s_encodingContentsTag);
    if (!(content && content->findFirstChild()))
	return parse(tcapParams,elem,prefix,0);

    DataBlock payload;
    const Parameter* param = s_mapDialogChoice;
    int err = TcapXApplication::NoError;
    while (param && param->name) {
	DataBlock db;
	if (encodeParam(param,db,content,err)) {
	    payload.append(db);
	    break;
	}
	param++;
    }
    XmlElement* child = content->pop();
    while (child) {
	DataBlock db;
	encodeRaw(param,db,child,err);
	payload.append(db);
	TelEngine::destruct(child);
	child = content->pop();
    }
    // set encoding contents and encoding contents type
    String hexString;
    hexString.hexify(payload.data(),payload.length(),' ');
    tcapParams.setParam(s_tcapEncodingContent,hexString);
    tcapParams.setParam(s_tcapEncodingType,YSTRING("single-ASN1-type-contructor"));

    // set direct reference
    XmlElement* reference = elem->findFirstChild(&s_directReferenceTag);
    if (reference) {
	const AppCtxt* appCtxt = findCtxtFromStr(reference->getText(),s_mapDialogCtxt);
	if (!appCtxt)
	    tcapParams.setParam(s_tcapDirectReference,reference->getText());
	else
	    tcapParams.setParam(s_tcapDirectReference,appCtxt->oid);
    }
    else {
	// find reference for decoded param
	const AppCtxt* ctxt = s_mapDialogCtxt;
	while (ctxt && ctxt->name) {
	    bool ok = (param && ctxt->ops.find(param->name));
	    if (ok) {
		tcapParams.setParam(s_tcapDirectReference,ctxt->oid);
		break;
	    }
	}
    }
    return true;
}

bool XmlToTcap::parse(NamedList& tcapParams)
{
    Lock l(this);
    XDebug(&__plugin,DebugAll,"XmlToTcap::parse()");
    tcapParams.setParam(s_tcapCompCount,String(0));

    XmlElement* ctxtElem = (m_elem ? m_elem->findFirstChild(&s_appContext) : 0);
    XmlElement* ltidElem = (m_elem ? m_elem->findFirstChild(&s_localTID) : 0);
    XmlElement* rtidElem = (m_elem ? m_elem->findFirstChild(&s_remoteTID) : 0);

    const AppCtxt* appCtxt = 0;
    // try to find the associated context for the given ids
    if (ltidElem || rtidElem) {
	appCtxt = m_app->findCtxt((ltidElem ? ltidElem->getText() : String::empty()),(rtidElem ? rtidElem->getText() : String::empty()));
    }
    //if we didn't find a saved context for the current transaction, search context in the XML info
    if (!appCtxt && ctxtElem)
	appCtxt = (AppCtxt*)findCtxtFromStr(ctxtElem->getText(),(m_app->type() == TcapXUser::MAP ? s_mapAppCtxt : s_camelAppCtxt));

    bool ok = parse(tcapParams,m_elem,"",appCtxt);
    NamedString* c = tcapParams.getParam(s_capabTag);

    if (!c) {
	c = tcapParams.getParam(s_tcapReqType);
	if (c) {
	    m_type = Tcap;
	    // retrieve application context
	    NamedString* ctxt = tcapParams.getParam(s_tcapAppCtxt);
	    if (!TelEngine::null(ctxt)) {
		const AppCtxt* search = (m_app->type() == TcapXUser::MAP ? s_mapAppCtxt : s_camelAppCtxt);
		AppCtxt* appCtxt = (AppCtxt*)findCtxtFromStr(*ctxt,search);
		if (appCtxt)
		    tcapParams.setParam(s_tcapAppCtxt,appCtxt->oid);
	    }
	    // retrieve MAP dialog context
	    ctxt = tcapParams.getParam(s_tcapDirectReference);
	    if (!TelEngine::null(ctxt)) {
		const AppCtxt* search = (m_app->type() == TcapXUser::MAP ? s_mapDialogCtxt : 0);
		AppCtxt* appCtxt = (AppCtxt*)findCtxtFromStr(*ctxt,search);
		if (appCtxt)
		    tcapParams.setParam(s_tcapDirectReference,appCtxt->oid);
	    }
	    // we shoudn't receive P_Abort from application, but make sure anyway
	    ctxt = tcapParams.getParam(s_tcapAbortCause);
	    if (!TelEngine::null(ctxt) && (*ctxt) == "pAbort") {
		ctxt = tcapParams.getParam(s_tcapAbortInfo);
		int code = lookup(*ctxt,SS7TCAPError::s_errorTypes);
		*ctxt = String(code);
	    }
	}
    }
    else
	m_type = Capability;
    return ok;
}

bool XmlToTcap::parse(NamedList& tcapParams, XmlElement* elem, String prefix, const AppCtxt* appCtxt)
{
    XDebug(&__plugin,DebugAll,"XmlToTcap::parse(elem=%p, prefix=%s) [%p]",elem,prefix.c_str(),this);
    if (!elem)
	return true;

    bool status = true;
    bool hasChildren = false;
    while (XmlElement* child = elem->pop()) {
	hasChildren = true;
	if (child->getTag() == s_component)
	    status = handleComponent(tcapParams,child,appCtxt);
	else if (child->getTag() == s_userInformation && m_app->type() == TcapXUser::MAP)
	    status = handleMAPDialog(tcapParams,child,(!TelEngine::null(prefix) ? prefix + "." + child->getTag() : child->getTag()));
	else
	    status = parse(tcapParams,child, (!TelEngine::null(prefix) ? prefix + "." + child->getTag() : child->getTag()),appCtxt);
	TelEngine::destruct(child);
	if (!status)
	    break;
    }
    const NamedList& attrs = elem->attributes();
    for (unsigned int i = 0; i < attrs.count(); i++) {
	NamedString* ns = attrs.getParam(i);
	if (TelEngine::null(ns))
	    continue;

	String find = (!TelEngine::null(prefix) ? prefix + "." + ns->name() : ns->name());
	const TCAPMap* map = findMap(find);
	if (map) {
	    if (TelEngine::null(map->name))
		tcapParams.addParam(find,*ns);
	    else {
		if (map->isPrefix)
		    tcapParams.addParam(map->name + "." + ns->name(),*ns);
		else
		    tcapParams.addParam(map->name,*ns);
	    }
	}
    }
    if (!hasChildren) {
	const TCAPMap* map = findMap(prefix);
	if (map) {
	    if (map->isPrefix) {
		if (!TelEngine::null(map->name))
		    tcapParams.addParam(map->name + "." + elem->getTag(),elem->getText());
		else
		    tcapParams.addParam(elem->getTag(),elem->getText());
	    }
	    else {
		if (!TelEngine::null(map->name))
		    tcapParams.addParam(map->name,elem->getText());
		else
		    tcapParams.addParam(elem->getTag(),elem->getText());
	    }
	}
    }
    return status;
}

/**
 * TcapXApplication
 */
TcapXApplication::TcapXApplication(const char* name, Socket* skt, TcapXUser* user)
    : Mutex(true,name),
      m_io(0), m_name(name), m_user(user),
      m_sentXml(0), m_receivedXml(0),
      m_sentTcap(0), m_receivedTcap(0),
      m_state(Waiting),
      m_tcap2Xml(this),
      m_xml2Tcap(this)
{
    if (skt) {
	m_io = new XMLConnection(skt,this);
	if (!m_io->startup()) {
	    delete m_io;
	    m_io = 0;
	}
    }
    m_type = user->type();
    Debug(&__plugin,DebugAll,"TcapXApplication created with name=%s and connection=%p [%p]",m_name.c_str(),m_io,this);
}

TcapXApplication::~TcapXApplication()
{
    Debug(&__plugin,DebugAll,"TcapXApplication with name=%s destroyed [%p]",m_name.c_str(),this);
    closeConnection();
    while (m_io)
	Thread::idle();
    m_user = 0;
}

bool TcapXApplication::hasCapability(const char* cap)
{
    if (!cap)
	return false;
    DDebug(&__plugin,DebugAll,"TcapXApplication::hasCapability(cap=%s) [%p]",cap,this);

    Lock l(this);
    ObjList* o = m_capab.find(cap);
    if (!o)
	return false;
    String* str = static_cast<String*>(o->get());
    if (!(str && (*str == cap)))
	return false;
    return true;
}

bool TcapXApplication::canHandle(NamedList& params)
{
    DDebug(&__plugin,DebugAll,"TcapXApplication::canHandle(params=%p) [%p]",&params,this);
    if (m_state != Active)
	return false;
    int compCount = params.getIntValue(s_tcapCompCount,0);

    // try matching on application context
    const AppCtxt* appCtxt = 0;
    while (true) {
	NamedString* appOID = params.getParam(s_tcapAppCtxt);
	if (TelEngine::null(appOID))
	    break;
	appCtxt = findCtxtFromOid(*appOID,(m_type== TcapXUser::MAP ? s_mapAppCtxt : s_camelAppCtxt));
	if (!appCtxt)
	    break;
	const Capability* cap = findCapabilityOID(m_type,appCtxt->name);
	if (!cap) {
	    if (!compCount)
		return false;
	    break;
	}
	if (!hasCapability(cap->name) && !compCount) {
	    Debug(&__plugin,DebugAll,"TcapXApplication '%s' cannot handle oid='%s' [%p]",m_name.c_str(),appCtxt->name,this);
	    return false;
	}
	break;
    }
    if (!compCount && !appCtxt)
	return false;

    for (int i = 1; i <= compCount; i++) {

	NamedString* opCode = params.getParam(s_tcapCompPrefixSep + String(i) + "." + s_tcapOpCode);
	NamedString* opType = params.getParam(s_tcapCompPrefixSep + String(i) + "." + s_tcapOpCodeType);
	if (TelEngine::null(opCode) || TelEngine::null(opType))
	    continue;
	const Operation* op = findOperation(m_type,opCode->toInteger(),(*opType == "local"),appCtxt);
	if (!op)
	    return false;
	const Capability* cap = findCapability(m_type,op->name);
	if (!cap)
	    return false;
	if (!hasCapability(cap->name)) {
	    Debug(&__plugin,DebugAll,"TcapXApplication '%s' cannot handle operation='%s' [%p]",m_name.c_str(),op->name.c_str(),this);
	    return false;
	}
    }
    return true;
}

void TcapXApplication::closeConnection()
{
    DDebug(&__plugin,DebugAll,"TcapXApplication::closeConnection() - app=%s closing [%p]",m_name.c_str(),this);
    Lock l(this);
    if (m_io)
	m_io->cancel();
    l.drop();
}

void TcapXApplication::setIO(XMLConnection* io)
{
    DDebug(&__plugin,DebugAll,"TcapXApplication::setIO(io=%p) - app=%s[%p]",io,m_name.c_str(),this);
    Lock l(this);
    if (!m_io)
	return;
    m_io = io;
    l.drop();
    if (!io && m_user)
	m_user->removeApp(this);
}

bool TcapXApplication::supportCapability(const String& capab)
{
    if (!findDefCapability(m_type,capab))
	return false;
    return true;
}

bool TcapXApplication::handleXML()
{
    Debug(&__plugin,DebugAll,"TcapXApplication::handleXML() - %s[%p]",m_name.c_str(),this);

    if (!m_xml2Tcap.checkXmlns()) {
	Debug(&__plugin,DebugInfo,"TcapXApplication=%s - XMLNS mismatch, closing the connection [%p]",m_name.c_str(),this);
	closeConnection();
	return false;
    }

    NamedList params("xml");
    if (!m_xml2Tcap.parse(params)) {
	Debug(&__plugin,DebugInfo,"TcapXApplication=%s - parse error, closing the connection [%p]",m_name.c_str(),this);
	closeConnection();
	return false;
    };
    if (m_user->printMessages()) {
	String tmp;
	params.dump(tmp,"\r\n  ",'\'',true);
	Debug(&__plugin,DebugAll,"App=%s[%p] parsed params %s from xml",m_name.c_str(),this,tmp.c_str());
    }
    switch (m_xml2Tcap.type()) {
	case XmlToTcap::Capability:
	    return handleCapability(params);
	case XmlToTcap::Tcap:
	    return handleTcap(params);
	case XmlToTcap::Unknown:
	default:
	    Debug(&__plugin,DebugInfo,"TcapXApplication=%s - unknown XML message [%p]",m_name.c_str(),this);
	    closeConnection();
	    return false;
    }
    return true;
}

bool TcapXApplication::sendTcapMsg(NamedList& params, const AppCtxt* ctxt)
{
    DDebug(&__plugin,DebugAll,"TcapXApplication::sentTcapMsg(params=%p) [%p]",&params,this);
    XmlFragment* msg = new XmlFragment();
    bool ok = m_tcap2Xml.buildXMLMessage(params,msg,TcapToXml::Tcap,ctxt);

    if (m_user->printMessages()) {
	String tmp;
	msg->toString(tmp,false,"\r\n","  ",false);
	Debug(&__plugin,DebugInfo,"App=%s[%p] is sending XML\r\n%s",m_name.c_str(),this,tmp.c_str());
    }
    if (ok && m_io) {
	m_io->writeData(msg);
	m_sentXml++;
    }
    delete msg;
    return ok;
}

bool TcapXApplication::handleCapability(NamedList& params)
{
    DDebug(&__plugin,DebugAll,"TcapXApplication::handleCapability() - app=%s [%p]",m_name.c_str(),this);
    Lock l(this);
    bool firstCap = true;
    for (unsigned int i = 0; i < params.count(); i++) {
	NamedString* ns = params.getParam(i);
	if (!ns)
	    continue;
	if (ns->name() == s_capabTag) {
	    if (TelEngine::null(ns))  {
		m_capab.clear();
		reportState(ShutDown);
		return true;
	    }
	    else {
		if (!supportCapability(*ns)) {
		    reportState(Inactive,String("Unsupported: ") + *ns);
		    return false;
		}
		if (firstCap && m_state == Active) {
		    m_capab.clear();
		    firstCap = false;
		}
		m_capab.append(new String(*ns));
	    }
	}
    }
    if (m_state == Waiting)
	reportState(Active);
    return true;
}

bool TcapXApplication::handleIndication(NamedList& tcap)
{
    DDebug(&__plugin,DebugAll,"TcapXApplication::handleIndication() - app=%s state=%s [%p]",m_name.c_str(),lookup(m_state,s_appStates),this);

    if (m_user->printMessages()) {
	String tmp;
	tcap.dump(tmp,"\r\n  ",'\'',true);
	Debug(&__plugin,DebugInfo,"App=%s[%p] received TCAP indication %s",m_name.c_str(),this,tmp.c_str());
    }

    int dialog = SS7TCAP::lookupTransaction(tcap.getValue(s_tcapReqType));
    String ltid = tcap.getValue(s_tcapLocalTID);
    NamedString* rtid = tcap.getParam(s_tcapRemoteTID);

    bool saveID = false, removeID = false;
    String appID;
    switch (dialog) {
	case SS7TCAP::TC_Unidirectional:
	    if (state() != Active)
		return false;
	    tcap.setParam(s_tcapLocalTID,"");
	    break;
	case SS7TCAP::TC_Begin:
	case SS7TCAP::TC_QueryWithPerm:
	case SS7TCAP::TC_QueryWithoutPerm:
	    if (state() != Active || TelEngine::null(rtid))
		return false;
	    tcap.setParam(s_tcapLocalTID,"");
	    saveID = true;
	    break;
	case SS7TCAP::TC_Continue:
	case SS7TCAP::TC_ConversationWithPerm:
	case SS7TCAP::TC_ConversationWithoutPerm:
	case SS7TCAP::TC_Notice:
	case SS7TCAP::TC_Unknown:
	    if (TelEngine::null(ltid))
		return false;
	    lock();
	    appID = m_ids.findAppID(ltid);
	    if (TelEngine::null(appID)) {
		    if (TelEngine::null(rtid)){
			unlock();
			reportError("Unknown request ID");
			return false;
		    }
		    appID = m_pending.findTcapID(*rtid);
		    if (TelEngine::null(appID)) {
			unlock();
			reportError("Unknown request ID");
			return false;
		    }
		    m_pending.remove(*rtid);
	    }
	    unlock();
	    break;
	case SS7TCAP::TC_End:
	case SS7TCAP::TC_Response:
	case SS7TCAP::TC_U_Abort:
	case SS7TCAP::TC_P_Abort:
	    if (TelEngine::null(ltid))
		return false;
	    lock();
	    appID = m_ids.findAppID(ltid);
	    if (TelEngine::null(appID)) {
		    appID = m_pending.findAppID(ltid);
		    if (TelEngine::null(appID)) {
			unlock();
			reportError("Unknown request ID");
			return false;
		    }
		    m_pending.remove(appID);
	    }
	    unlock();
	    removeID = true;
	    break;
	default:
	    return false;
    }

    tcap.setParam(s_tcapLocalTID,appID);

    const AppCtxt* ctxt = 0;
    if (saveID) {
        NamedString* oid = tcap.getParam(s_tcapAppCtxt);
	if (!TelEngine::null(oid))
	    ctxt = findCtxtFromOid(*oid, m_type == TcapXUser::MAP ? s_mapAppCtxt : s_camelAppCtxt);
    }
    else
	ctxt = findCtxt(appID,(TelEngine::null(rtid) ? String::empty() : *rtid));

    bool ok = sendTcapMsg(tcap,ctxt);

    Lock l(this);
    if (saveID)
	m_pending.appendID(ltid,*rtid,ctxt);
    if (removeID) {
	m_ids.remove(appID);
	if (m_state == ShutDown && !trCount())
	    reportState(Inactive);
    }
    m_receivedTcap++;
    return ok;
}

bool TcapXApplication::handleTcap(NamedList& tcap)
{
    DDebug(&__plugin,DebugAll,"TcapXApplication::handleTcap() - app=%s state=%s [%p]",m_name.c_str(),lookup(m_state,s_appStates),this);

    int dialog = SS7TCAP::lookupTransaction(tcap.getValue(s_tcapReqType));
    String ltid = tcap.getValue(s_tcapLocalTID);
    String rtid = tcap.getValue(s_tcapRemoteTID);
    bool endNow = tcap.getBoolValue(s_tcapEndNow,false);

    bool saveID = false;
    bool removeID = false;
    lock();
    String tcapID = m_ids.findTcapID(ltid);
    unlock();

    switch (dialog) {
	case SS7TCAP::TC_Unknown:
	case SS7TCAP::TC_Unidirectional:
	    break;
	case SS7TCAP::TC_Begin:
	case SS7TCAP::TC_QueryWithPerm:
	case SS7TCAP::TC_QueryWithoutPerm:
	    if (TelEngine::null(ltid)) {
		reportError("Missing request ID");
		return false;
	    }
	    if (!TelEngine::null(tcapID)) {
		reportError("Duplicate request ID");
		return false;
	    }
	    saveID = true;
	    break;
	case SS7TCAP::TC_Continue:
	case SS7TCAP::TC_ConversationWithPerm:
	case SS7TCAP::TC_ConversationWithoutPerm:
	    if (TelEngine::null(ltid)) {
		reportError("Missing request ID");
		return false;
	    }
	    if (TelEngine::null(tcapID)) {
		if (!TelEngine::null(rtid)) {
		    lock();
		    tcapID = m_pending.findTcapID(rtid);
		    unlock();
		    if (TelEngine::null(tcapID)) {
			reportError("Unknown request ID");
			return false;
		    }
		    lock();
		    m_pending.remove(rtid);
		    unlock();
		    saveID = true;
		}
		else {
		    reportError("Unknown request ID");
		    return false;
		}
	    }
	    break;
	case SS7TCAP::TC_End:
	case SS7TCAP::TC_Response:
	case SS7TCAP::TC_U_Abort:
	    if (TelEngine::null(ltid)) {
		reportError("Missing request ID");
		return false;
	    }
	    if (TelEngine::null(tcapID)) {
		if (!TelEngine::null(rtid)) {
		    lock();
		    tcapID = m_pending.findTcapID(rtid);
		    unlock();
		    if (TelEngine::null(tcapID)) {
			reportError("Unknown request ID");
			return false;
		    }
		    lock();
		    m_pending.remove(rtid);
		    unlock();
		}
		else {
		    reportError("Unknown request ID");
		    return false;
		}
	    }
	    removeID = true;
	    break;
	case SS7TCAP::TC_P_Abort:
	case SS7TCAP::TC_Notice:
	default:
	    reportError("Invalid request");
	    return false;
    }
    if (!m_user)
	return false;
    tcap.setParam(s_tcapLocalTID,tcapID);

    const AppCtxt* ctxt = 0;
    if (saveID && !endNow) {
	NamedString* ctxtStr = tcap.getParam(s_tcapAppCtxt);
	if (!TelEngine::null(ctxtStr))
	    ctxt = findCtxtFromOid(*ctxtStr,(m_type == TcapXUser::MAP ? s_mapAppCtxt : s_camelAppCtxt));
    }

    if (m_user->printMessages()) {
	String tmp;
	tcap.dump(tmp,"\r\n  ",'\'',true);
	Debug(&__plugin,DebugInfo,"App=%s[%p] is sending TCAP request %s",m_name.c_str(),this,tmp.c_str());
    }

    SS7TCAPError error = m_user->applicationRequest(this,tcap,dialog);
    if (error.error() != SS7TCAPError::NoError) {
	NamedString* err = tcap.getParam(s_tcapRequestError);
	reportError(TelEngine::null(err) ? error.errorName() : *err);
	return false;
    }

    Lock l(this);
    if (removeID || endNow) {
	m_ids.remove(ltid);
    	if (m_state == ShutDown && !trCount())
	    reportState(Inactive);
    }
    if (saveID && !endNow)
	m_ids.appendID(tcap.getValue(s_tcapLocalTID),ltid,ctxt);
    m_sentTcap++;
    return true;
}

void TcapXApplication::receivedXML(XmlDocument* doc)
{

    Debug(&__plugin,DebugAll,"TcapXApplication::receivedXML(frag=%p) - %s[%p]",doc,m_name.c_str(),this);
    if (!doc)
	return;

    if (m_user->printMessages()) {
	String tmp;
	doc->toString(tmp,false,"\r\n","  ");
	Debug(&__plugin,DebugInfo,"App=%s[%p] received XML\r\n%s",m_name.c_str(),this,tmp.c_str());
    }

    if (!m_xml2Tcap.valid(doc)) {
	Debug(&__plugin,DebugInfo,"TcapXApplication=%s - invalid message, closing the connection [%p]",m_name.c_str(),this);
	closeConnection();
	return;
    }
    if (m_state == Waiting && !m_xml2Tcap.hasDeclaration()) {
	Debug(&__plugin,DebugInfo,"TcapXApplication=%s - initial XML declaration missing, closing the connection [%p]",m_name.c_str(),this);
	closeConnection();
	return;
    }
    if (m_xml2Tcap.hasDeclaration() && !m_xml2Tcap.validDeclaration()) {
	Debug(&__plugin,DebugInfo,"TcapXApplication=%s - XML declaration mismatch, closing the connection [%p]",m_name.c_str(),this);
	closeConnection();
	return;
    }
    if (handleXML())
	m_receivedXml++;
}

void TcapXApplication::reportState(State state, const char* error)
{
    DDebug(&__plugin,DebugAll,"TcapXApplication::reportState(state=%s, error=%s) [%p]",lookup(state,s_appStates),error,this);
    m_state = state;
    switch (m_state) {
	case Waiting:
	    break;
	case Active:
	    sendStateResponse();
	    m_user->notifyManagementState();
	    break;
	case ShutDown:
	    Debug(&__plugin,DebugInfo,"Requested shutdown, %d transactions pending [%p]",trCount(),this);
	    if (trCount())
		break;
	    m_state = Inactive;
	case Inactive:
	    sendStateResponse(error);
	    closeConnection();
	default:
	    break;
    }
}

void TcapXApplication::sendStateResponse(const char* error)
{
    DDebug(&__plugin,DebugAll,"TcapXApplication::sendStateResponse(error=%s) [%p]",error,this);

    NamedList params("xml");
    params.setParam("state",lookup(m_state,s_appStates));
    if (error)
	params.setParam("error",error);
    XmlFragment* msg = new XmlFragment();
    bool ok = m_tcap2Xml.buildXMLMessage(params,msg,TcapToXml::State);

    if (m_user->printMessages()) {
	String tmp;
	msg->toString(tmp,false,"\r\n","  ",false);
	Debug(&__plugin,DebugInfo,"App=%s[%p] is sending XML\r\n%s",m_name.c_str(),this,tmp.c_str());
    }
    if (ok && m_io) {
	m_io->writeData(msg);
	m_sentXml++;
    }
    delete msg;
}

void TcapXApplication::reportError(const char* err)
{
    if (!err)
	return;
    Debug(&__plugin,DebugInfo,"TcapXApplication::reportError(error=%s) - app=%s [%p]",err,m_name.c_str(),this);
}

void TcapXApplication::status(NamedList& status)
{
    DDebug(&__plugin,DebugInfo,"TcapXApplication::status() [%p]",this);

    Lock l(this);
    status.setParam("receivedXML",String(m_receivedXml));
    status.setParam("sentXML",String(m_sentXml));
    status.setParam("receivedTcap",String(m_receivedTcap));
    status.setParam("sentTcap",String(m_sentTcap));
}

const AppCtxt* TcapXApplication::findCtxt(const String& appID, const String& remoteID)
{
    DDebug(DebugAll,"TcapXApplication::findCtxt('%s','%s') [%p]",appID.c_str(),remoteID.c_str(),this);
    Lock l(this);
    if (!appID.null()) {
	Transaction* t = m_ids.findByAppID(appID);
	if (t)
	    return t->context();
    }
    if (!remoteID.null()) {
	Transaction* t = m_pending.findByAppID(remoteID);
	if (t)
	    return t->context();
    }
    return 0;
}

/**
 * TcapXUser
 */
TcapXUser::TcapXUser(const char* name)
    : TCAPUser(name),
      Mutex(true,name),
      m_appsMtx(true,"TCAPXApps"),
      m_listener(0), m_type(MAP),
      m_printMsg(false), m_addEnc(false),
      m_mngtStatus(SCCPManagement::UserOutOfService)
{
    Debug(&__plugin,DebugAll,"TcapXUser '%s' created [%p]",toString().c_str(),this);
}

TcapXUser::~TcapXUser()
{
    Debug(&__plugin,DebugAll,"TcapXUser '%s' destroyed [%p]",toString().c_str(),this);
    lock();
    if (m_listener)
	m_listener->cancel();
    if (tcap())
	attach(0);
    unlock();

    m_appsMtx.lock();
    for (ObjList* o = m_apps.skipNull(); o; o = o->skipNext()) {
	TcapXApplication* app = static_cast<TcapXApplication*>(o->get());
	if (app)
	    app->closeConnection();
    }
    m_appsMtx.unlock();

    while (m_listener)
	Thread::idle();

    while (true) {
	Thread::idle();
	m_appsMtx.lock();
	ObjList* o = m_apps.skipNull();
	m_appsMtx.unlock();
	if (!o)
	    break;
    }
}

bool TcapXUser::initialize(NamedList& sect)
{
    Debug(this,DebugAll,"TcapXUser::initialize() [%p]",this);

    Lock l(this);
    if (!m_listener) {
	m_listener = new XMLConnListener(this,sect);
	if (!m_listener->init()) {
	    delete m_listener;
	    m_listener = 0;
	    return false;
	}
    }

    m_type = (UserType)lookup(sect.getValue(s_typeStr,"MAP"),s_userTypes,m_type);
    m_printMsg = sect.getBoolValue(YSTRING("print-messages"),false);
    m_addEnc = sect.getBoolValue(YSTRING("add-encoding"),false);
    if (!tcap() && !findTCAP(sect.getValue("tcap",0)))
	return false;
    notifyManagementState(true);
    return true;
}

void TcapXUser::removeApp(TcapXApplication* app)
{
    Debug(this,DebugAll,"Removing application=%s[%p] [%p]",(app ? app->toString().c_str() : ""),app,this);
    Lock l(m_appsMtx);
    m_apps.remove(app);
    l.drop();
    notifyManagementState();
}

bool TcapXUser::tcapIndication(NamedList& params)
{
    DDebug(this,DebugAll,"TcapXUser::tcapIndication() [%p]",this);

    NamedString* ltid = params.getParam(s_tcapLocalTID);
    if (TelEngine::null(ltid)) {
	DDebug(this,DebugAll,"Received transaction without local transaction id, rejecting it");
	return false;
    }
    int dialog = SS7TCAP::lookupTransaction(params.getValue(s_tcapReqType));

    ObjList* o = m_trIDs.find(*ltid);
    NamedString* tcapID = 0;
    if (o)
	tcapID = static_cast<NamedString*>(o->get());
    bool searchApp = false, removeID = false;
    switch (dialog) {
	case SS7TCAP::TC_Unidirectional:
	    return sendToApp(params);
	case SS7TCAP::TC_Begin:
	case SS7TCAP::TC_QueryWithPerm:
	case SS7TCAP::TC_QueryWithoutPerm:
	    if (TelEngine::null(ltid) || !TelEngine::null(tcapID)) {
		DDebug(this,DebugAll,"Received a new transaction with an id that we already have, rejecting it");
		return false;
	    }
	    return sendToApp(params);
	case SS7TCAP::TC_Continue:
	case SS7TCAP::TC_ConversationWithPerm:
	case SS7TCAP::TC_ConversationWithoutPerm:
	case SS7TCAP::TC_Notice:
	case SS7TCAP::TC_Unknown:
	    if (TelEngine::null(ltid) || TelEngine::null(tcapID)) {
		DDebug(this,DebugAll,"Received a dialog continue TCAP message for a dialog that doesn't exist, rejecting it");
		return false;
	    }
	    searchApp = true;
	    break;
	case SS7TCAP::TC_End:
	case SS7TCAP::TC_Response:
	case SS7TCAP::TC_U_Abort:
	case SS7TCAP::TC_P_Abort:
	    if (TelEngine::null(ltid) || TelEngine::null(tcapID)) {
		DDebug(this,DebugAll,"Received a end dialogue TCAP message for a dialog that doesn't exist, rejecting it");
		return false;
	    }
	    searchApp = true;
	    removeID = true;
	    break;
	default:
	    DDebug(this,DebugAll,"Received a TCAP message without type of dialog message, rejecting it");
	    return false;
    }

    Lock la(m_appsMtx);
    o = m_apps.find(*tcapID);
    if (!o && searchApp) {
	Debug(this,DebugMild,"Cannot find application that was handling transaction with id='%s'",ltid->c_str());
	return false;
    }

    TcapXApplication* app = static_cast<TcapXApplication*>(o->get());
    if (searchApp && !app)
	return false;

    if (removeID) {
	reorderApps(app);
	m_trIDs.remove(tcapID);
    }
    la.drop();
    return sendToApp(params,app,false);
}

bool TcapXUser::sendToApp(NamedList& params, TcapXApplication* app, bool saveID)
{
    DDebug(this,DebugAll,"TcapXUser::sendToApp(params=%p,app=%s[%p]) [%p]",&params,(app ? app->toString().c_str() : ""),app,this);

    Lock l(m_appsMtx);
    if (!app)
	app = findApplication(params);
    if (!app) {
	NamedString* opCode = params.getParam(YSTRING("tcap.component.1.operationCode"));
	NamedString* opType = params.getParam(YSTRING("tcap.component.1.operationCodeType"));
	NamedString* appCtxt = params.getParam(s_tcapAppCtxt);
	if (!(TelEngine::null(opCode) || TelEngine::null(opType))) {
	    const Operation* op = findOperation(m_type,opCode->toInteger(),(*opType == "local"),0);
	    Debug(this,DebugInfo,"TcapXUser::sendToApp() - cannot find application to handle operation='%s' [%p]",(op ? op->name.c_str() : "no operation"),this);
	}
	else {
	    if (!TelEngine::null(appCtxt))
		Debug(this,DebugInfo,"TcapXUser::sendToApp() - cannot find application to handle application OID='%s' [%p]",appCtxt->c_str(),this);
	    else
		Debug(this,DebugInfo,"TcapXUser::sendToApp() - cannot find application to handle transaction with no given operation or app OID [%p]",this);
	}
	return false;
    }
    if (saveID)
	m_trIDs.append(new NamedString(params.getValue(s_tcapLocalTID),app->toString()));
    return (app && app->handleIndication(params));
}

TcapXApplication* TcapXUser::findApplication(NamedList& params)
{
    DDebug(this,DebugAll,"TcapXUser::findApplication() [%p]",this);
    for (ObjList* o = m_apps.skipNull(); o; o = o->skipNext()) {
	TcapXApplication* app = static_cast<TcapXApplication*>(o->get());
	if (app->canHandle(params)) {
	    // reorder list
	    reorderApps(app);
	    return app;
	}
    }
    return 0;
}

void TcapXUser::reorderApps(TcapXApplication* app)
{
    if (!app)
	return;
    ObjList* appObj = m_apps.find(app);
    if (!appObj)
	return;
    ObjList* next = appObj->next();
    // it's already at the bottom of the list
    if (!next)
	return;
    unsigned int count = app->trCount() + 1;
    while (next) {
	TcapXApplication* nextApp = static_cast<TcapXApplication*>(next->get());
	if (nextApp) {
	    if (count < nextApp->trCount())
		break;
	    next = next->next();
	}
	else
	    break;
    }
    if (next) {
	if (next != appObj->next()) {
	    m_apps.remove(app,false);
	    next->insert(app);
	    return;
	}
    }
    else {
	m_apps.remove(app,false);
	m_apps.append(app);
    }
}

void TcapXUser::statusString(String& str)
{
    DDebug(this,DebugAll,"TcapXUser::statusString() [%p]",this);
    Lock l(m_appsMtx);
    NamedList params("");
    for (ObjList* o = m_apps.skipNull(); o; o = o->skipNext()) {
	TcapXApplication* app = static_cast<TcapXApplication*>(o->get());
	if (!app)
	    continue;
	str.append(app->toString(),",") << "=" << toString() << "|" << lookup(m_type,s_userTypes);
	app->status(params);
	str << "|" << params.getIntValue(YSTRING("receivedXML"));
	str << "|" << params.getIntValue(YSTRING("sentXML"));
	str << "|" << params.getIntValue(YSTRING("receivedTcap"));
	str << "|" << params.getIntValue(YSTRING("sentTcap"));
    }
}

bool TcapXUser::managementNotify(SCCP::Type type, NamedList& params)
{
// TODO
// check docs if they say anything about management
    return true;
}

bool TcapXUser::findTCAP(const char* name)
{
    if (!name)
	return false;
    SignallingComponent* tcap = 0;
    SignallingEngine* engine = SignallingEngine::self(true);
    if (engine)
	tcap = engine->find(name,"SS7TCAPITU",tcap);
    if (tcap) {
	Debug(this,DebugAll,"TcapXUser '%s' attaching to TCAP=%s [%p]",toString().c_str(),name,this);
	attach(YOBJECT(SS7TCAPITU,tcap));
    }
    return (tcap != 0);
}

bool TcapXUser::createApplication(Socket* skt, String& addr)
{
    if (!skt)
	return false;

    String appName = toString() + ":" + addr;
    Lock l(m_appsMtx);
    // it's new, put it at the top of the list
    m_apps.insert(new TcapXApplication(appName,skt,this));
    return true;
}

int TcapXUser::managementState()
{
    DDebug(this,DebugAll,"TcapXUser::managementState() - user=%s[%p]",toString().c_str(),this);
    Lock l(m_appsMtx);
    for (ObjList* o = m_apps.skipNull(); o; o = o->skipNext()) {
	TcapXApplication* app = static_cast<TcapXApplication*>(o->get());
	if (!app)
	    continue;
	if (app->state() == TcapXApplication::Active)
	    return SCCPManagement::UserInService;
    }
    return SCCPManagement::UserOutOfService;
}

void TcapXUser::notifyManagementState(bool forced)
{
    DDebug(this,DebugAll,"TcapXUser::notifyManagementState(forced=%s) [%p]",String::boolText(forced),this);
    SCCPManagement::LocalBroadcast state = (SCCPManagement::LocalBroadcast)managementState();
    Lock l(this);
    if (forced || state != m_mngtStatus) {
	Debug(this,DebugInfo,"Changing management state from '%s' to '%s' [%p]",
	      lookup(m_mngtStatus,SCCPManagement::broadcastType(),""),lookup(state,SCCPManagement::broadcastType(),""),this);
	m_mngtStatus = state;
	l.drop();
	if (tcap()) {
	    NamedList p("");
	    tcap()->updateUserStatus(this,state,p);
	}
    }
}

void TcapXUser::setListener(XMLConnListener* list)
{
    Lock l(this);
    m_listener = list;
}

SS7TCAPError TcapXUser::applicationRequest(TcapXApplication* app, NamedList& params, int reqType)
{
    DDebug(this,DebugAll,"TcapXUser::applicationRequest() - user=%s, request from app=%s[%p] [%p]",
	toString().c_str(),(app ? app->toString().c_str() : ""),app,this);

    SS7TCAPError error((tcap() ? tcap()->tcapType() : SS7TCAP::ITUTCAP));
    if (!app)
	return error;
    if (tcap()) {
	params.setParam(s_tcapUser,toString());
	error = tcap()->userRequest(params);
    }
    else {
	params.setParam(s_tcapRequestError,"No TCAP attached");
	error.setError(SS7TCAPError::Transact_UnassignedTransactionID);
	return error;
    }
    bool saveID = false;
    bool removeID = false;
    switch (reqType) {
	case SS7TCAP::TC_Begin:
	case SS7TCAP::TC_QueryWithPerm:
	case SS7TCAP::TC_QueryWithoutPerm:
	    saveID = true;
	    break;
	case SS7TCAP::TC_End:
	case SS7TCAP::TC_Response:
	case SS7TCAP::TC_U_Abort:
	    removeID = true;
	    break;
	case SS7TCAP::TC_P_Abort:
	case SS7TCAP::TC_Notice:
	case SS7TCAP::TC_Unknown:
	case SS7TCAP::TC_Unidirectional:
	case SS7TCAP::TC_Continue:
	case SS7TCAP::TC_ConversationWithPerm:
	case SS7TCAP::TC_ConversationWithoutPerm:
	default:
	    break;
    }

    Lock la(m_appsMtx);
    NamedString* ltid = params.getParam(s_tcapLocalTID);
    if (saveID) {
	reorderApps(app);
	if (TelEngine::null(ltid)) {
	    params.setParam(s_tcapRequestError,"TCAP error");
	    error.setError(SS7TCAPError::Transact_UnassignedTransactionID);
	}
	else
	    m_trIDs.append(new NamedString(*ltid,app->toString()));
    }
    if (removeID) {
	reorderApps(app);
	if (TelEngine::null(ltid) ||!m_trIDs.find(*ltid))
	    error.setError(SS7TCAPError::Transact_UnassignedTransactionID);
	else
	    m_trIDs.remove(*ltid);
    }
    return error;
}

/**
 * TcapXModule
 */
TcapXModule::TcapXModule()
    : Module("camel_map","misc"),
      m_usersMtx(true,"TCAPXUsers")
{
    Output("Loaded TCAPXML module");
}

TcapXModule::~TcapXModule()
{
    Output("Unloaded module TCAPXML");
}

void TcapXModule::initialize()
{
    Output("Initializing module TCAPXML");
    Module::initialize();

    Configuration cfg(Engine::configFile(name()));
    installRelay(Halt);
    cfg.load();
    m_showMissing = cfg.getBoolValue(YSTRING("general"),YSTRING("show-missing"),true);
    initUsers(cfg);
}

bool TcapXModule::unload()
{
    if (!lock(500000))
	return false;
    uninstallRelays();
    m_users.clear();
    unlock();
    return true;
}

void TcapXModule::initUsers(Configuration& cfg)
{
    DDebug(&__plugin,DebugAll,"TcapXModule::initUsers() [%p]",this);
    int n = cfg.sections();
    Lock l(m_usersMtx);
    for (int i = 0; i < n; i++) {
	NamedList* sect = cfg.getSection(i);
	if (!sect)
	    continue;
	String name(*sect);
	if (name.startSkip("tcap") && name) {
	    if (!sect->getBoolValue("enable",true)) {
		m_users.remove(name);
		continue;
	    }
	    ObjList* o = m_users.find(name);
	    TcapXUser* usr = (o ? static_cast<TcapXUser*>(o->get()) : 0);
	    if (!usr) {
		usr = new TcapXUser(name);
		m_users.append(usr);
	    }
	    if (!usr->initialize(*sect)) {
		Debug(&__plugin,DebugInfo,"TcapXModule::initUsers() - user '%s' failed to initialize [%p]",name.c_str(),this);
		m_users.remove(name);
	    }
	}
    }
}

bool TcapXModule::received(Message& msg, int id)
{
    if (id == Halt)
	unload();
    return Module::received(msg,id);
}

unsigned int TcapXModule::applicationCount()
{
    Lock l(m_usersMtx);
    unsigned int count = 0;
    for (ObjList* o = m_users.skipNull(); o; o = o->skipNext()) {
	TcapXUser* user = static_cast<TcapXUser*>(o->get());
	count += user->applicationCount();
    }
    return count;
}

void TcapXModule::statusModule(String& str)
{
    Module::statusModule(str);
    str.append("format=User|Type|ReceivedXML|SentXML|ReceivedTCAP|SentTcap",",");
}

void TcapXModule::statusParams(String& str)
{
    str.append("count=",",") << applicationCount();
}

void TcapXModule::statusDetail(String& str)
{
    Lock l(m_usersMtx);
    for (ObjList* o = m_users.skipNull(); o; o = o->skipNext()) {
	TcapXUser* user = static_cast<TcapXUser*>(o->get());
	if (!user)
	    continue;
	user->statusString(str);
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet enc=utf-8: */
