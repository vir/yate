/**
 * jbfeatures.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Jabber features module
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
#include <yatejabber.h>


// TODO:
// implement roster group and name max length when setting it from protocol

using namespace TelEngine;

namespace { // anonymous

class JBFeaturesModule;                  // The module


/*
 * The module
 */
class JBFeaturesModule : public Module
{
public:
    enum PrivateRelay {
	JabberFeature = Private,
	UserUpdate = Private << 1,
    };
    JBFeaturesModule();
    virtual ~JBFeaturesModule();
    virtual void initialize();
    // Check if a message was sent by us
    inline bool isModule(const Message& msg) const {
	    String* module = msg.getParam("module");
	    return module && *module == name();
	}
    // Handle 'jabber.feature' roster management
    // RFC 3921
    bool handleFeatureRoster(JabberID& from, Message& msg);
    // Handle 'jabber.feature' private data get/set
    // XEP-0049 Private XML storage
    bool handleFeaturePrivateData(JabberID& from, Message& msg);
    // Handle 'jabber.feature' vcard get/set
    // XEP-0054 vcard-temp
    bool handleFeatureVCard(JabberID& from, Message& msg);
    // Handle 'jabber.feature' offline message get/add
    bool handleFeatureMsgOffline(JabberID& from, Message& msg);
    // Handle 'jabber.feature' in-band register get/set
    // XEP-0077 In-Band Registration
    bool handleFeatureRegister(JabberID& from, Message& msg);
protected:
    virtual bool received(Message& msg, int id);
    // Build and dispatch or enqueue a 'database' message
    // Return 0 when the message is enqueued or dispatch failure
    Message* queryDb(const NamedList& params, const String& account,
	const String& query, bool sync = true);
private:
    bool m_init;                         // Module already initialized
    String m_defAccount;                 // Default database account
    String m_vcardAccount;               // Database vcard account
    String m_vcardQueryGet;              // vcard 'get' query
    String m_vcardQuerySet;              // vcard 'set' item query
    String m_vcardQueryDel;              // vcard 'delete' item query
    String m_dataAccount;                // Database private data account
    String m_dataQueryGet;               // Private data 'get' query
    String m_dataQuerySet;               // Private data 'set' query
    String m_dataQueryDel;               // Private data 'delete' query
    // Offline messages
    unsigned int m_maxChatCount;         // Maximum number of chat messages to store
    unsigned int m_nextCheck;            // The next time (in seconds) to run the expire query
    unsigned int m_expire;               // Chat expiring interval (in seconds)
    String m_chatAccount;                // Database offline messages account
    String m_chatQueryExpire;            // Offline messages expire query
    String m_chatQueryGet;               // Offline messages 'get' query
    String m_chatQueryAdd;               // Offline messages 'add' query
    String m_chatQueryDel;               // Offline messages 'delete' query
    // In-band user register (XEP-0077)
    bool m_regEnable;                    // Enable user (un)register
    bool m_regChange;                    // Enable user changes (such as password)
    bool m_regAllowUnsecure;             // Allow user registration support on unsecured streams
    String m_regUrl;                     // URL to send to the user when creation is disabled
    String m_regInfo;                    // Instructions to send along with the url
};


/*
 * Local data
 */
INIT_PLUGIN(JBFeaturesModule);           // The module
static String s_groupSeparator = ",";    // Roster item list grup separator
static bool s_ignoreGrp = true;          // Ignore invalid groups or refuse roster update
static bool s_rosterQueryHierarchical = true; // Request hierarchical result in user.roster query

// Return a safe pointer to config section
static inline const NamedList* getSection(Configuration& cfg, const char* name)
{
    NamedList* sect = cfg.getSection(name);
    if (sect)
	return sect;
    return &NamedList::empty();
}

// Add a 'subscription' and, optionally, an 'ask' attribute to a roster item
static inline void addSubscription(XmlElement& dest, const String& sub)
{
    XMPPDirVal d(sub);
    if (d.test(XMPPDirVal::PendingOut))
	dest.setAttribute("ask","subscribe");
    String tmp;
    d.toSubscription(tmp);
    dest.setAttribute("subscription",tmp);
}

// Build a roster item XML element from message parameters
static XmlElement* buildRosterItem(NamedList& list, unsigned int index)
{
    String prefix("contact.");
    prefix << index;
    NamedString* contact = list.getParam(prefix);
    XDebug(&__plugin,DebugAll,"buildRosterItem(%s,%u) contact=%s",
	list.c_str(),index,TelEngine::c_safe(contact));
    if (TelEngine::null(contact))
	return 0;
    XmlElement* item = new XmlElement("item");
    item->setAttribute("jid",*contact);
    NamedList* params = YOBJECT(NamedList,contact);
    if (!params)
	prefix << ".";
    ObjList* groups = 0;
    NamedIterator iter(params ? *params : list);
    String dummy;
    for (const NamedString* param = 0; 0 != (param = iter.get());) {
	const String& name = params ? param->name() : dummy;
	if (!params) {
	    dummy = param->name();
	    if (!dummy.startSkip(prefix,false))
		continue;
	}
	if (name == "name")
	    item->setAttributeValid("name",*param);
	else if (name == "subscription")
	    addSubscription(*item,*param);
	else if (name == "groups") {
	    if (!groups)
		groups = param->split(s_groupSeparator[0],false);
	}
	else
	    item->addChild(XMPPUtils::createElement(name,*param));
    }
    if (!item->getAttribute("subscription"))
	addSubscription(*item,String::empty());
    for (ObjList* o = groups ? groups->skipNull() : 0; o; o = o->skipNext()) {
	String* grp = static_cast<String*>(o->get());
	item->addChild(XMPPUtils::createElement("group",*grp));
    }
    TelEngine::destruct(groups);
    return item;
}

// Build a result and set it to a message parameter
// Release the given xml pointer and zero it
// Return true
static bool buildResult(Message& msg, XmlElement*& xml, XmlElement* child = 0)
{
    const char* id = xml ? xml->attribute("id") : 0;
    XmlElement* rsp = XMPPUtils::createIqResult(0,0,id);
    TelEngine::destruct(xml);
    if (child)
	rsp->addChild(child);
    msg.setParam(new NamedPointer("response",rsp));
    return true;
}

// Build an error and set it to a message parameter
// Release the given xml pointer and zero it
// Return false
static bool buildError(Message& msg, XmlElement*& xml,
    XMPPError::Type error = XMPPError::ServiceUnavailable,
    XMPPError::ErrorType type = XMPPError::TypeModify, const char* text = 0)
{
    const char* id = xml ? xml->attribute("id") : 0;
    XmlElement* rsp = XMPPUtils::createIq(XMPPUtils::IqError,0,0,id);
    if (TelEngine::null(id) && xml) {
	rsp->addChild(xml);
	xml = 0;
    }
    else
	TelEngine::destruct(xml);
    rsp->addChild(XMPPUtils::createError(type,error,text));
    msg.setParam(new NamedPointer("response",rsp));
    return false;
}

// Add xml data to a list
static inline void addXmlData(NamedList& list, XmlElement* xml, const char* param = "xml")
{
    String buf;
    if (xml)
	xml->toString(buf);
    list.addParam(param,buf);
}


/*
 * JBFeaturesModule
 */
// Early load, late unload
JBFeaturesModule::JBFeaturesModule()
    : Module("jbfeatures","misc",true),
    m_init(false),
    m_maxChatCount(0), m_nextCheck(0), m_expire(0),
    m_regEnable(true), m_regChange(true), m_regAllowUnsecure(false)
{
    Output("Loaded module Jabber Server Features");
}

JBFeaturesModule::~JBFeaturesModule()
{
    Output("Unloading module Jabber Server Features");
}

void JBFeaturesModule::initialize()
{
    Output("Initializing module Jabber Server Features");

    Configuration cfg(Engine::configFile("jbfeatures"));
    NamedList dummy("");

    const NamedList* reg = getSection(cfg,"register");
    m_regEnable = reg->getBoolValue("allow_management",true);
    m_regChange = reg->getBoolValue("allow_change",true);
    m_regAllowUnsecure = reg->getBoolValue("allow_unsecure",false);
    m_regUrl = reg->getValue("url");
    m_regInfo = reg->getValue("intructions");
    // TODO: Notify feature XMPPNamespace::Register to the jabber server
    const NamedList* offlinechat = getSection(cfg,"offline_chat");
    int tmp = offlinechat->getIntValue("maxcount");
    m_maxChatCount = tmp > 0 ? tmp : 0;
    tmp = offlinechat->getIntValue("expires");
    if (tmp < 0)
	tmp = 0;
    else if (tmp && tmp < 30)
	tmp = 30;
    m_expire = tmp * 60;
    if (m_expire) {
	if (!m_nextCheck)
	    m_nextCheck = Time::secNow();
    }
    else
	m_nextCheck = 0;
    const NamedList* general = getSection(cfg,"general");
    s_ignoreGrp = general->getBoolValue("ignore_invalid_groups",true);
    s_rosterQueryHierarchical = general->getBoolValue("roster_query_hierarchical",true);

    if (m_init)
	return;

    m_init = true;
    m_defAccount = general->getValue("account");
    s_groupSeparator = general->getValue("groups_separator");
    if (s_groupSeparator.length() == 2) {
	DataBlock d;
	d.unHexify(s_groupSeparator);
	s_groupSeparator.clear();
	if (d.length() && d[0])
	    s_groupSeparator = (char)d[0];
    }
    if (!s_groupSeparator)
	s_groupSeparator = ",";
    else
	s_groupSeparator = s_groupSeparator.substr(0,1);
    const NamedList* vcard = getSection(cfg,"vcard");
    m_vcardAccount = vcard->getValue("account");
    m_vcardQueryGet = vcard->getValue("get");
    m_vcardQuerySet = vcard->getValue("set");
    m_vcardQueryDel = vcard->getValue("clear_user");
    const NamedList* pdata = getSection(cfg,"private_data");
    m_dataAccount = pdata->getValue("account");
    m_dataQueryGet = pdata->getValue("get");
    m_dataQuerySet = pdata->getValue("set");
    m_dataQueryDel = pdata->getValue("clear_user");
    m_chatAccount = offlinechat->getValue("account");
    m_chatQueryExpire = offlinechat->getValue("expire_query");
    m_chatQueryGet = offlinechat->getValue("get");
    m_chatQueryAdd = offlinechat->getValue("add");
    m_chatQueryDel = offlinechat->getValue("clear_user");
    setup();
    installRelay(Halt);
    installRelay(JabberFeature,"jabber.feature");
    installRelay(UserUpdate,"user.update");
}

// Handle 'jabber.feature' roster management
// RFC 3921
bool JBFeaturesModule::handleFeatureRoster(JabberID& from, Message& msg)
{
    XmlElement* xml = XMPPUtils::getXml(msg);
    DDebug(this,DebugAll,"handleFeatureRoster() from=%s xml=%p",from.c_str(),xml);
    if (!xml)
	return false;
    // Ignore responses
    XMPPUtils::IqType t = XMPPUtils::iqType(xml->attribute("type"));
    if (t != XMPPUtils::IqGet && t != XMPPUtils::IqSet) {
	TelEngine::destruct(xml);
	return false;
    }
    // The client must add it's resource in request
    if (!from.resource())
	return buildError(msg,xml);
    // The request must be carried by a 'query' tag
    XmlElement* child = xml->findFirstChild();
    if (!(child && XMPPUtils::isUnprefTag(*child,XmlTag::Query)))
	return buildError(msg,xml);
    JabberID contact;
    bool get = (t == XMPPUtils::IqGet);
    bool set = !get;
    if (!get) {
	// Set/remove contact: check jid
	// Don't allow user to operate on itself
	XmlElement* item = XMPPUtils::findFirstChild(*child,XmlTag::Item,XMPPNamespace::Roster);
	if (item) {
	    contact = item->getAttribute("jid");
	    String* sub = item->getAttribute("subscription");
	    set = !sub || *sub != "remove";
	}
	if (!contact)
	    return buildError(msg,xml,XMPPError::BadRequest);
	else if (contact.bare() == from.bare())
	    return buildError(msg,xml,XMPPError::NotAllowed);
    }
    Message m("user.roster");
    m.addParam("module",name());
    m.addParam("operation",set ? "update" : (get ? "query": "delete"));
    m.addParam("username",from.bare());
    if (get)
	m.addParam("hierarchical",String::boolText(s_rosterQueryHierarchical));
    else {
	m.addParam("contact",contact.bare());
	if (set) {
	    // We already found the item
	    XmlElement* item = XMPPUtils::findFirstChild(*child,XmlTag::Item,XMPPNamespace::Roster);
	    NamedString* params = new NamedString("contact.parameters","name,groups");
	    m.addParam(params);
	    m.addParam("name",item->attribute("name"));
	    NamedString* groups = new NamedString("groups");
	    m.addParam(groups);
	    // Groups and other children
	    const String* ns = &XMPPUtils::s_ns[XMPPNamespace::Roster];
	    for (XmlElement* c = item->findFirstChild(0,ns); c; c = item->findNextChild(c,0,ns)) {
		if (XMPPUtils::isUnprefTag(*c,XmlTag::Group)) {
		    const String& grp = c->getText();
		    if (!grp)
			continue;
		    // Check for forbidden separator
		    if (0 > grp.find(s_groupSeparator[0]))
			groups->append(grp,s_groupSeparator);
		    else if (!s_ignoreGrp) {
			String text;
			text << "Group '" << grp << "' contains unacceptable character";
			return buildError(msg,xml,XMPPError::Policy,XMPPError::TypeModify,text);
		    }
		}
		else {
		    params->append(c->tag(),",");
		    m.addParam(c->tag(),c->getText());
		}
	    }
	}
    }
    if (Engine::dispatch(m)) {
#ifdef DEBUG
	u_int64_t start = Time::now();
#endif
	XmlElement* child = 0;
	if (get) {
	    unsigned int n = m.getIntValue("contact.count");
	    child = XMPPUtils::createElement(XmlTag::Query,XMPPNamespace::Roster);
	    for (unsigned int i = 1; i <= n; i++)
		child->addChild(buildRosterItem(m,i));
	}
	msg.setParam("groups_separator",s_groupSeparator);
#ifdef DEBUG
	Debug(this,DebugAll,"Roster '%s' user='%s' filled in %u ms",
	    m.getValue("operation"),m.getValue("username"),
	    (unsigned int)((Time::now() - start + 500) / 1000));
#endif
	return buildResult(msg,xml,child);
    }
    if (m.getParam("error"))
	return buildError(msg,xml,XMPPError::ItemNotFound);
    return buildError(msg,xml);
}

// Handle 'jabber.feature' private data get/set
// XEP-0049 Private XML storage
bool JBFeaturesModule::handleFeaturePrivateData(JabberID& from, Message& msg)
{
    XmlElement* xml = XMPPUtils::getXml(msg);
    DDebug(this,DebugAll,"handleFeaturePrivateData() from=%s xml=%p",from.c_str(),xml);
    if (!xml)
	return false;
    // Ignore responses
    XMPPUtils::IqType t = XMPPUtils::iqType(xml->attribute("type"));
    if (t != XMPPUtils::IqGet && t != XMPPUtils::IqSet) {
	TelEngine::destruct(xml);
	return false;
    }
    // The request must be carried by a 'query' tag
    XmlElement* child = xml->findFirstChild();
    if (!(child && XMPPUtils::isUnprefTag(*child,XmlTag::Query)))
	return buildError(msg,xml);

    // XEP-0049 2.3:
    //   At least one child with a valid namespace must exist
    //   Iq 'set' may contain more then 1 child qualified by the same namespace
    XmlElement* ch = child->findFirstChild();
    String* ns = ch ? ch->xmlns() : 0;
    if (TelEngine::null(ns))
	return buildError(msg,xml,XMPPError::BadFormat);

    // TODO handle special jabber:iq:private requests:
    //   storage:imprefs (seen from Exodus)
    //   storage:bookmarks (XEP-0048 Bookmark storage)
    //   storage:metacontacts (seen from Gajim)
    //   storage:rosternotes (seen from Gajim)

    // Handle 'get'
    if (t == XMPPUtils::IqGet) {
	String tag(ch->tag());
	// We should have only 1 child
	ch = child->findNextChild(ch);
	if (ch)
	    return buildError(msg,xml,XMPPError::NotAcceptable);
	NamedList p("");
	p.addParam("username",from.bare());
	p.addParam("tag",tag);
	p.addParam("xmlns",*ns);
	Message* m = queryDb(p,m_dataAccount,m_dataQueryGet);
	XmlElement* query = XMPPUtils::createElement(XmlTag::Query,
	    XMPPNamespace::IqPrivate);
	XmlElement* pdata = 0;
	if (m) {
	    Array* a = static_cast<Array*>(m->userObject(YATOM("Array")));
	    String* data = a ? YOBJECT(String,a->get(0,1)) : 0;
	    pdata = data ? XMPPUtils::getXml(*data) : 0;
	    if (pdata) {
		// Avoid sending inconsistent tag or namespace
		if (tag != pdata->toString() || *ns != pdata->xmlns()) {
		    Debug(this,DebugNote,
			"User %s got invalid private data tag/ns='%s'/'%s' instead of '%s'/'%s'",
			from.bare().c_str(),pdata->tag(),
			TelEngine::c_safe(pdata->xmlns()),tag.c_str(),ns->c_str());
		    TelEngine::destruct(pdata);
		}
	    }
	    else if (data)
		Debug(this,DebugNote,"User %s got invalid xml private data",from.bare().c_str());
	    TelEngine::destruct(m);
	}
	if (!pdata)
	    pdata = XMPPUtils::createElement(tag,"",*ns);
	query->addChild(pdata);
	return buildResult(msg,xml,query);
    }

    // Handle 'set'
    // All children must share the same namespace
    for (; ch; ch = child->findNextChild(ch))
	if (*ns != ch->xmlns())
	    return buildError(msg,xml,XMPPError::NotAcceptable);
    // Update all data. Return error if at least one item fails or there is no data
    for (ch = child->findFirstChild(); ch; ch = child->findNextChild(ch)) {
	XDebug(this,DebugAll,"Setting private data for '%s' tag=%s xmlns=%s",
	    from.bare().c_str(),ch->tag(),ns->c_str());
	NamedList p("");
	p.addParam("username",from.bare());
	p.addParam("tag",ch->tag());
	p.addParam("xmlns",*ns);
	addXmlData(p,ch);
	Message* m = queryDb(p,m_dataAccount,m_dataQuerySet);
	if (!m)
	    break;
	TelEngine::destruct(m);
    }
    if (!ch)
	return buildResult(msg,xml);
    return buildError(msg,xml);
}

// Handle 'jabber.feature' vcard get/set
// XEP-0054 vcard-temp
bool JBFeaturesModule::handleFeatureVCard(JabberID& from, Message& msg)
{
    JabberID to(msg.getValue("to"));
    XmlElement* xml = XMPPUtils::getXml(msg);
    DDebug(this,DebugAll,"handleFeatureVCard() from=%s to=%s xml=%p",
	from.c_str(),to.c_str(),xml);
    if (!xml)
	return false;
    // Ignore responses
    XMPPUtils::IqType t = XMPPUtils::iqType(xml->attribute("type"));
    if (t != XMPPUtils::IqGet && t != XMPPUtils::IqSet) {
	TelEngine::destruct(xml);
	return false;
    }
    NamedList p("");
    bool otherUser = (to && to.bare() != from.bare());
    if (otherUser) {
	// Check auth
	Message auth("resource.subscribe");
	auth.addParam("operation","query");
	auth.addParam("subscriber",from.bare());
	auth.addParam("notifier",to.bare());
	if (!Engine::dispatch(auth))
	    return buildError(msg,xml);
	p.addParam("username",to.bare());
    }
    else
	p.addParam("username",from.bare());
    Message* m = 0;
    if (t == XMPPUtils::IqGet)
	m = queryDb(p,m_vcardAccount,m_vcardQueryGet);
    else {
	XmlElement* vcard = xml->findFirstChild();
	addXmlData(p,vcard,"vcard");
	m = queryDb(p,m_vcardAccount,m_vcardQuerySet);
    }
    // Don't return error on failure if the user requested its vcard
    if (!m && (otherUser || t == XMPPUtils::IqSet))
	return buildError(msg,xml);
    XmlElement* vcard = 0;
    if (t == XMPPUtils::IqGet && m) {
	Array* a = static_cast<Array*>(m->userObject(YATOM("Array")));
	if (a) {
	    String* vc = YOBJECT(String,a->get(0,1));
	    XDebug(this,DebugInfo,"Got vcard for '%s': '%s'",p.getValue("username"),
		TelEngine::c_safe(vc));
	    if (!TelEngine::null(vc)) {
		vcard = XMPPUtils::getXml(*vc);
		if (vcard) {
		    // Avoid sending inconsistent tag
		    if (!XMPPUtils::isTag(*vcard,XmlTag::VCard,XMPPNamespace::VCard)) {
			Debug(this,DebugNote,"Wrong vcard tag='%s' or ns='%s' for '%s'",
			    vcard->tag(),TelEngine::c_safe(vcard->xmlns()),p.getValue("username"));
			TelEngine::destruct(vcard);
		    }
		}
		else
		    Debug(this,DebugNote,"Failed to parse vcard for '%s'",p.getValue("username"));
	    }
	}
    }
    if (!vcard && t == XMPPUtils::IqGet)
	vcard = XMPPUtils::createElement(XmlTag::VCard,XMPPNamespace::VCard);
    TelEngine::destruct(m);
    return buildResult(msg,xml,vcard);
}

// Handle 'jabber.feature' offline message get/add
bool JBFeaturesModule::handleFeatureMsgOffline(JabberID& from, Message& msg)
{
    String* oper = msg.getParam("operation");
    DDebug(this,DebugAll,"handleFeatureMsgOffline() oper=%s",TelEngine::c_safe(oper));
    if (!oper || *oper == "add") {
	// Store offline message
	JabberID user(msg.getValue("to"));
	if (!(user && user.valid()))
	    return false;
	XmlElement* xml = XMPPUtils::getXml(msg);
	if (!xml)
	    return false;
	XMPPUtils::MsgType t = XMPPUtils::msgType(xml->attribute("type"));
	const String& body = XMPPUtils::body(*xml);
	bool ok = body && (t == XMPPUtils::Normal || t == XMPPUtils::Chat);
	if (ok) {
	    xml->removeAttribute("to");
	    if (TelEngine::null(xml->getAttribute("from")))
		xml->setAttribute("from",from);
	    NamedList p("");
	    p.addParam("username",user.bare());
	    addXmlData(p,xml);
	    const char* time = msg.getValue("time");
	    if (!TelEngine::null(time))
		p.addParam("time",time);
	    else
		p.addParam("time",String(msg.msgTime().sec()));
	    p.addParam("maxcount",String(m_maxChatCount));
	    Message* m = queryDb(p,m_chatAccount,m_chatQueryAdd);
	    if (m) {
		Array* a = static_cast<Array*>(m->userObject(YATOM("Array")));
		String* res = a ? YOBJECT(String,a->get(0,1)) : 0;
		if (res) {
		    DDebug(this,DebugAll,"Got result %s to add chat",res->c_str());
		    ok = (res->toInteger() != 0);
		}
		else
		    ok = false;
	    }
	    else
		ok = false;
	    TelEngine::destruct(m);
	}
	if (ok)
	    TelEngine::destruct(xml);
	else
	    msg.setParam(new NamedPointer("xml",xml));
	return ok;
    }
    if (*oper == "query") {
	// Retrieve offline messages
	NamedList p("");
	p.addParam("username",from.bare());
	Message* m = queryDb(p,m_chatAccount,m_chatQueryGet);
	if (!m)
	    return false;
	Array* a = static_cast<Array*>(m->userObject(YATOM("Array")));
	int rows = a ? a->getRows() : 0;
	int cols = a ? a->getColumns() : 0;
	DDebug(this,DebugAll,"Got %d offline messages for user =%s",
	    rows ? rows - 1 : 0,from.bare().c_str());
	for (int row = 1; row < rows; row++) {
	    String* data = 0;
	    String* time = 0;
	    for (int col = 0; col < cols; col++) {
		String* s = YOBJECT(String,a->get(col,0));
		if (!s)
		    continue;
		if (*s == "xml")
		    data = YOBJECT(String,a->get(col,row));
		else if (*s == "time")
		    time = YOBJECT(String,a->get(col,row));
	    }
	    if (TelEngine::null(data))
		continue;
	    XmlElement* xml = XMPPUtils::getXml(*data);
	    if (xml) {
		if (!TelEngine::null(time))
		    xml->addChild(XMPPUtils::createDelay(time->toInteger()));
		msg.addParam(new NamedPointer("xml",xml));
		continue;
	    }
	    Debug(this,DebugNote,"Invalid database offline chat xml for user=%s",
		from.bare().c_str());
	}
	TelEngine::destruct(m);
	return true;
    }
    if (*oper == "delete") {
	// Remove user's offline messages
	NamedList p("");
	p.addParam("username",from.bare());
	queryDb(p,m_chatAccount,m_chatQueryDel,false);
	return true;
    }
    return false;
}

// Handle 'jabber.feature' in-band register get/set
// XEP-0077 In-Band Registration
bool JBFeaturesModule::handleFeatureRegister(JabberID& from, Message& msg)
{
    XmlElement* xml = XMPPUtils::getXml(msg);
    DDebug(this,DebugAll,"handleFeatureRegister() from=%s xml=%p",from.c_str(),xml);
    if (!xml)
	return false;
    // Ignore responses
    XMPPUtils::IqType t = XMPPUtils::iqType(xml->attribute("type"));
    if (t != XMPPUtils::IqGet && t != XMPPUtils::IqSet) {
	TelEngine::destruct(xml);
	return false;
    }
    // Handle 'query' elements only
    XmlElement* child = xml->findFirstChild();
    if (!(child && XMPPUtils::isUnprefTag(*child,XmlTag::Query)))
	return buildError(msg,xml);
    // Registration available only on secured streams
    int flags = msg.getIntValue("stream_flags");
    if (!(m_regAllowUnsecure || 0 != (flags & JBStream::StreamTls)))
	return buildError(msg,xml,XMPPError::EncryptionRequired);
    // Set auth or remove the user
    if (t == XMPPUtils::IqSet) {
	const char* oper = 0;
	bool remove = XMPPUtils::remove(*child);
	JabberID user;
	if (0 == (flags & JBStream::StreamAuthenticated)) {
	    if (!m_regEnable || remove)
		return buildError(msg,xml);
	    XmlElement* tmp = XMPPUtils::findFirstChild(*child,XmlTag::Username,
		XMPPNamespace::IqRegister);
	    const String& username = tmp ? tmp->getText() : String::empty();
	    if (!username)
		return buildError(msg,xml,XMPPError::BadRequest);
	    const char* domain = msg.getValue("stream_domain");
	    if (TelEngine::null(domain))
		return buildError(msg,xml,XMPPError::BadRequest);
	    oper = "add";
	    user.set(username,domain);
	}
	else {
	    if (!remove) {
		if (m_regChange)
		    oper = "update";
		else
		    return buildError(msg,xml);
	    }
	    else if (m_regEnable)
		oper = "delete";
	    else
		return buildError(msg,xml);
	    user.set(from.node(),from.domain());
	}
	// Update the user
	Message m("user.update");
	m.addParam("module",name());
	m.addParam("operation",oper);
	m.addParam("user",user);
	if (!remove) {
	    XmlElement* p = XMPPUtils::findFirstChild(*child,XmlTag::Password,
		XMPPNamespace::IqRegister);
	    const String& pwd = p ? p->getText() : String::empty();
	    if (!pwd)
		return buildError(msg,xml,XMPPError::BadRequest);
	    m.addParam("password",pwd);
	}
	if (Engine::dispatch(m))
	    return buildResult(msg,xml);
	return buildError(msg,xml,XMPPError::NotAllowed);
    }

    // Get auth
    XmlElement* query = 0;
    if (m_regEnable) {
	query = XMPPUtils::createElement(XmlTag::Query,
	    XMPPNamespace::IqRegister);
	if (0 == (flags & JBStream::StreamAuthenticated)) {
	    query->addChild(XMPPUtils::createElement(XmlTag::Username));
	    query->addChild(XMPPUtils::createElement(XmlTag::Password));
	}
	else
	    query->addChild(XMPPUtils::createElement(XmlTag::Registered));
    }
    else if (m_regUrl) {
	// XEP-0077 Section 5 Redirection
	query = XMPPUtils::createElement(XmlTag::Query,XMPPNamespace::IqRegister);
	if (m_regInfo)
	    query->addChild(XMPPUtils::createElement("instructions",m_regInfo));
	query->addChild(XMPPUtils::createXOobUrl(m_regUrl));
    }
    if (query)
	return buildResult(msg,xml,query);
    return buildError(msg,xml);
}

// Message handler
bool JBFeaturesModule::received(Message& msg, int id)
{
    if (id == JabberFeature) {
	JabberID from(msg.getValue("from"));
	switch (XMPPUtils::s_ns[msg.getValue("feature")]) {
	    case XMPPNamespace::VCard:
		return from && handleFeatureVCard(from,msg);
	    case XMPPNamespace::Roster:
		return from && handleFeatureRoster(from,msg);
	    case XMPPNamespace::IqPrivate:
		return from && handleFeaturePrivateData(from,msg);
	    case XMPPNamespace::MsgOffline:
		return from && handleFeatureMsgOffline(from,msg);
	    case XMPPNamespace::IqRegister:
		return handleFeatureRegister(from,msg);
	    default: ;
	}
	return false;
    }
    if (id == UserUpdate) {
	// Handle user deletion: remove vcard, private data, offline messages ...
	String* notif = msg.getParam("notify");
	if (TelEngine::null(notif) || *notif != "delete")
	    return false;
	String* user = msg.getParam("user");
	if (TelEngine::null(user))
	    return false;
	DDebug(this,DebugAll,
	    "User '%s' deleted: removing vcard, private data, offline messages",
	    user->c_str());
	NamedList p("");
	p.addParam("username",*user);
	queryDb(p,m_vcardAccount,m_vcardQueryDel,false);
	queryDb(p,m_dataAccount,m_dataQueryDel,false);
	queryDb(p,m_chatAccount,m_chatQueryDel,false);
	return false;
    }
    if (id == Timer) {
	unsigned int sec = msg.msgTime().sec();
	if (m_nextCheck && m_nextCheck < sec) {
	    if (m_expire && m_chatQueryExpire) {
		XDebug(this,DebugAll,"Running chat expire query");
		NamedList p("");
		p.addParam("time",String(sec));
		queryDb(p,m_chatAccount,m_chatQueryExpire,false);
		m_nextCheck = sec + m_expire / 2;
	    }
	    else
		m_nextCheck = 0;
	}
    }
    else if (id == Halt) {
	uninstallRelays();
	DDebug(this,DebugAll,"Halted");
    }
    return Module::received(msg,id);
}

// Build and dispatch or enqueue a 'database' message
Message* JBFeaturesModule::queryDb(const NamedList& params, const String& account,
    const String& query, bool sync)
{
    if (!((account || m_defAccount) && query))
	return 0;
    Message* m = new Message("database");
    m->addParam("account",account ? account : m_defAccount);
    String tmp = query;
    params.replaceParams(tmp,true);
    m->addParam("query",tmp);
    if (sync) {
	if (Engine::dispatch(m)) {
	    String* error = m->getParam("error");
	    if (error) {
		DDebug(this,DebugNote,"'database' failed error='%s'",error->c_str());
		TelEngine::destruct(m);
	    }
	}
	else
	    TelEngine::destruct(m);
    }
    else {
	m->addParam("results",String::boolText(false));
	Engine::enqueue(m);
	m = 0;
    }
    return m;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
