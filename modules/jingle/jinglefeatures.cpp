/**
 * jinglefeatures.cpp
 * This file is part of the YATE Project http://YATE.null.ro 
 *
 * Additional XMPP features
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <yatephone.h>
#include <yatejingle.h>

using namespace TelEngine;
namespace { // anonymous

/**
 * NOTE:
 * All responses sent to custom IQs set/get should carry the element
 * with the custom application (namespace). This is nedded to match
 * the application in the responses. Otherwise the module won't be
 * able to detect responses to custom requests/notifications
 */

class CustomXmppIqMsg;                 // A custom message built from an xmpp.iq
class YJingleFeatures;                 // The module

// A custom message built from an xmpp.iq
// Send a response to received IQ after dispatched
class CustomXmppIqMsg : public Message
{
public:
    // Params:
    // iq The received IQ
    // child The first child of the received IQ
    CustomXmppIqMsg(Message& msg, XMLElement& iq, XMLElement& child);
protected:
    virtual void dispatched(bool accepted);
private:
    String m_app;
    String m_oper;
};

// Features module
class YJingleFeatures : public Module
{
public:
    enum PrivateRelay {
	XmppIq = Private,
	Custom = Private << 1,
	UserInfo = Private << 2,
	UserRoster = Private << 3,
    };
    YJingleFeatures();
    virtual ~YJingleFeatures();
    // Check if a message is sent by this module
    inline bool isModule(Message& msg) {
	    NamedString* m = msg.getParam("module");
	    return m && *m == name();
	}
    // Build a message. Add the 'module' param
    // Copy 'line' and/or 'account' parameters
    inline Message* buildMsg(const char* msg, Message* line = 0) {
	    Message* m = new Message(msg);
	    m->addParam("module",name());
	    if (line)
		m->copyParams(*line,"account,line");
	    return m;
	}
    // Inherited methods
    virtual bool received(Message& msg, int id);
    virtual void initialize();
    // Message handlers
    bool handleXmppIq(Message& msg);
    bool handleCustom(Message& msg);
    bool handleUserRoster(Message& msg);
    bool handleUserInfo(Message& msg);
    // Build and dispatch an "xmpp.generate" message. Copy error param on failure
    // Consume the xml element
    bool xmppGenerate(Message& recv, XMLElement* xml, bool rsp = false);
    // Uninstall the relays
    bool unload();
    // Check if a custom application is handled by the module
    inline bool isApplication(const String* app) {
	    if (!m_apps || null(app))
		return false;
	    Lock lock(this);
	    return 0 != m_apps->find(*app);
	}
    // Build an XML element's children from a list
    bool addChildren(NamedList& params, XMLElement& xml);
    // Add an XML element's children to a list
    // The element's name will be used as message-prefix
    bool addChildren(XMLElement& xml, NamedList& params);

private:
    // Check module and target parameters of a received message
    // Optionally check the target parameter (if present, it must be one
    //  of the jingle module aliases)
    bool acceptMsg(Message& msg, bool checkTarget);
    // Handle dynamic roster data
    bool handleXmppIqDynamicRoster(Message& msg, XMLElement& query, XMPPUtils::IqType t,
	const JabberID& from, const JabberID& to, const String& id);
    // Handle client private iq data
    bool handleXmppIqPrivate(Message& msg, XMLElement& query, XMPPUtils::IqType t,
	const JabberID& from, const JabberID& to, const String& id);
    // Handle a valid vcard element
    bool handleXmppIqVCard(Message& msg, XMLElement& vcard, XMPPUtils::IqType t,
	const JabberID& from, const JabberID& to, const String& id);

    ObjList* m_apps;                     // Custom applications
};


INIT_PLUGIN(YJingleFeatures);
static const String s_jingleAlias[] = {"jingle", "xmpp", "jabber", ""};
static bool s_handleVCard = true;
static bool s_handlePrivate = true;
static bool s_handleAddrbook = true;
// Strings used to compare or build other strings
static const String s_customPrefix = "custom.";
static const String s_group = "group";

static TokenDict s_customIqType[] = {
    {"request", XMPPUtils::IqGet},
    {"update",  XMPPUtils::IqSet},
    {"notify",  XMPPUtils::IqResult},
    {"error",   XMPPUtils::IqError},
    {0,0},
};


UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow && !__plugin.unload())
	return false;
    return true;
}


// Check if a text is one of the jingle module's alias
static bool isJingleAlias(const String& name)
{
    for (int i = 0; s_jingleAlias[i].length(); i++)
	if (name == s_jingleAlias[i])
	    return true;
    return false;
}

// Check if the message source is the jingle module
static inline bool isJingleMsg(Message& msg)
{
    NamedString* module = msg.getParam("module");
    return module && isJingleAlias(*module);
}

// Find an xml element's child text
static const char* getChildText(XMLElement& xml, const char* name,
    XMLElement* start = 0)
{
    XMLElement* child = xml.findNextChild(start,name);
    const char* text = child ? child->getText() : 0;
    TelEngine::destruct(child);
    return text;
}

// Build an xml element error from error/reason parameters 
static XMLElement* createXmlError(const Message& msg, const char* defaultText = 0)
{
    XMPPError::ErrorType eType = XMPPError::TypeModify;
    XMPPError::Type err = XMPPError::UndefinedCondition;
    String* reason = msg.getParam("reason");
    const char* error = msg.getValue("error",defaultText);
    if (reason && *reason == "noauth") {
	eType = XMPPError::TypeAuth;
	err = XMPPError::NotAuthorized;
	if (null(error))
	    error = "Not authorized";
    }
    return XMPPUtils::createError(eType,err,error);
}


/*
 * CustomXmppIqMsg
 */
CustomXmppIqMsg::CustomXmppIqMsg(Message& msg, XMLElement& iq, XMLElement& child)
    : Message("custom")
{
    addParam("module",__plugin.name());
    m_app = child.getAttribute("xmlns");
    m_oper = child.name();
    addParam("application",m_app);
    addParam("operation",m_oper);

    // Check for stanza failure (the stream failed to send a required element)
    bool failure = msg.getBoolValue("failure");
    if (failure) {
	addParam("type","error");
	addParam("reason","noconn");
	addParam("error","Failed to send");
	copyParams(msg,"account,line,username,id");
	return;
    }

    // Process received element
    const char* type = msg.getValue("type");
    XMPPUtils::IqType iqType = XMPPUtils::iqType(type);
    addParam("type",::lookup(iqType,s_customIqType,type));
    copyParams(msg,"account,line,username,from,to,id");
    bool needRsp = iqType == XMPPUtils::IqSet || iqType == XMPPUtils::IqGet;
    addParam("need-response",String::boolText(needRsp));
    // Build params if not error
    if (iqType != XMPPUtils::IqError) {
	unsigned int n = 1;
	XMLElement* ch = child.findFirstChild();
	for (; ch; ch = child.findNextChild(ch), n++)
	    ch->toList(*this,s_customPrefix + String(n));
    }
    else {
	String err, errText;
	XMPPUtils::decodeError(&iq,err,errText);
	if (errText)
	    addParam("error",errText);
	else if (err)
	    addParam("error",err);
    }
}

void CustomXmppIqMsg::dispatched(bool accepted)
{
    if (!getBoolValue("need-response"))
	return;

    XMPPUtils::IqType t = accepted ? XMPPUtils::IqResult : XMPPUtils::IqError;
    XMLElement* iq = XMPPUtils::createIq(t,getValue("to"),getValue("from"),getValue("id"));
    XMLElement* oper = new XMLElement(m_oper);
    oper->setAttribute("xmlns",m_app);
    // Check for result params
    for (unsigned int n = 1; accepted; n++) {
	String prefix;
	prefix << "custom_out." << n;
	if (TelEngine::null(getParam(prefix)))
	    break;
	oper->addChild(new XMLElement(*this,prefix));
    }
    iq->addChild(oper);
    if (!accepted)
	iq->addChild(createXmlError(*this,"Unhandled message"));
    __plugin.xmppGenerate(*this,iq,true);
}


/*
 * YJingleFeatures
 */
YJingleFeatures::YJingleFeatures()
    : Module("jinglefeatures","misc"),
    m_apps(0)
{
    Output("Loaded module Jingle Features");
    m_apps = new ObjList;
}

YJingleFeatures::~YJingleFeatures()
{
    Output("Unloading module Jingle Features");
    TelEngine::destruct(m_apps);
}

bool YJingleFeatures::received(Message& msg, int id)
{
    switch (id) {
	case XmppIq:
	    return handleXmppIq(msg);
	case Custom:
	    return handleCustom(msg);
	case UserRoster:
	    return handleUserRoster(msg);
	case UserInfo:
	    return handleUserInfo(msg);
	case Halt:
	    unload();
	    break;
    }
    return Module::received(msg,id);
}

void YJingleFeatures::initialize()
{
    Output("Initializing module Jingle Features");

    Configuration cfg(Engine::configFile("jinglefeatures"));
    cfg.load();

    NamedList dummy("");
    NamedList* general = cfg.getSection("general");
    if (!general)
	general = &dummy;
    String apps = general->getValue("custom_applications");
    TelEngine::destruct(m_apps);
    m_apps = apps.split(',',false);

    NamedList* iq = cfg.getSection("iq");
    if (!iq)
	iq = &dummy;
    s_handleVCard = iq->getBoolValue("vcard",true);
    s_handlePrivate = iq->getBoolValue("private",true);
    s_handleAddrbook = iq->getBoolValue("addressbook",true);

    if (debugAt(DebugAll)) {
	String s;
	s << "vcard=" << String::boolText(s_handleVCard);
	s << " private=" << String::boolText(s_handlePrivate);
	s << " addressbook=" << String::boolText(s_handleAddrbook);
	s << " custom_applications=" << apps;
	Debug(this,DebugAll,"Initialized %s",s.c_str());
    }

    lock();

    static bool s_first = true;
    if (s_first) {
	s_first = false;
	setup();
	installRelay(XmppIq,"xmpp.iq",100);
	installRelay(Custom,"custom",100);
    }

    if (s_handleVCard || s_handlePrivate)
	installRelay(UserInfo,"user.info",100);
    else
	uninstallRelay(UserInfo);
    if (s_handleAddrbook)
	installRelay(UserRoster,"user.roster",100);
    else
	uninstallRelay(UserRoster);

    unlock();
}

// xmpp.iq handler
bool YJingleFeatures::handleXmppIq(Message& msg)
{
    if (!isJingleMsg(msg))
	return false;

    // No XML: nothing to be done
    XMLElement* xml = XMLElement::getXml(msg,false);
    if (!xml)
	return false;

    XMPPUtils::IqType t = XMPPUtils::iqType(msg.getValue("type"));
    JabberID from = msg.getValue("from");
    JabberID to = msg.getValue("to");
    String id = msg.getValue("id");
    Debug(this,DebugAll,"Processing '%s' from=%s to=%s id=%s",
	msg.c_str(),from.c_str(),to.c_str(),id.c_str());

    XMLElement* child = xml->findFirstChild();

    bool ok = false;
    // Use a while to break to the end
    while (child) {
	String xmlns = child->getAttribute("xmlns");

	// Query
	if (child->type() == XMLElement::Query) {
	    XMPPNamespace::Type ns = XMPPNamespace::type(xmlns);
	    switch (ns) {
		case XMPPNamespace::DynamicRoster:
		    ok = handleXmppIqDynamicRoster(msg,*child,t,from,to,id);
		    break;
		case XMPPNamespace::IqPrivate:
		    ok = handleXmppIqPrivate(msg,*child,t,from,to,id);
		    break;
		default: ;
	    }
	    break;
	}

	// vCard
	if (child->type() == XMLElement::VCard) {
	    if (XMPPUtils::hasXmlns(*child,XMPPNamespace::VCard))
		ok = handleXmppIqVCard(msg,*child,t,from,to,id);
	    break;
	}

	// Custom
	if (isApplication(&xmlns)) {
	    Engine::enqueue(new CustomXmppIqMsg(msg,*xml,*child));
	    ok = true;
	    break;
	}

	break;
    }

    TelEngine::destruct(child);
    return ok;
}

// custom handler
bool YJingleFeatures::handleCustom(Message& msg)
{
    // Don't handle jingle(features) messages or messages with
    //  other target
    if (!acceptMsg(msg,true) || isJingleMsg(msg))
	return false;

    // Check parameters
    const char* oper = msg.getValue("operation");
    if (null(oper))
	return false;
    String* xmlns = msg.getParam("application");
    if (!isApplication(xmlns))
	return false;
    const char* type = msg.getValue("type");
    int iqType = ::lookup(type,s_customIqType,XMPPUtils::IqCount);
    const char* error = 0;
    if (iqType == XMPPUtils::IqError || iqType == XMPPUtils::IqCount) {
	error = msg.getValue("error");
	if (iqType == XMPPUtils::IqCount && null(error)) {
	    Debug(this,DebugMild,"Custom message app=%s oper=%s with invalid type=%s",
		xmlns->c_str(),oper,type);
	    return false;
	}
    }

    Debug(this,DebugAll,"Generating IQ from custom app=%s oper=%s type=%s",
	xmlns->c_str(),oper,type);
    XMLElement* iq = XMPPUtils::createIq((XMPPUtils::IqType)iqType,msg.getValue("from"),
	msg.getValue("to"),0); 
    XMLElement* child = new XMLElement(oper);
    child->setAttribute("xmlns",*xmlns);
    // Build params
    for (unsigned int n = 1; true; n++) {
	String prefix;
	prefix << s_customPrefix << n;
	if (null(msg.getParam(prefix)))
	    break;
	child->addChild(new XMLElement(msg,prefix));
    }
    iq->addChild(child);
    if (iqType == XMPPUtils::IqError)
	iq->addChild(createXmlError(msg));

    bool rsp = iqType == XMPPUtils::IqResult || iqType == XMPPUtils::IqError;
    return xmppGenerate(msg,iq,rsp);
}

// user.roster handler
bool YJingleFeatures::handleUserRoster(Message& msg)
{
    // Don't handle jingle(features) messages
    if (!acceptMsg(msg,true) || isJingleMsg(msg))
	return false;

    NamedString* oper = msg.getParam("operation");
    if (!oper)
	return false;
    bool get = (*oper == "request");
    if (!get) {
	if (*oper == "update")
	    get = false;
	else
	    return false;
    }

    bool dynamic = msg.getBoolValue("addressbook");
    if (dynamic && !s_handleAddrbook)
	return false;

    Debug(this,DebugAll,"Processing '%s' operation=%s from=%s to=%s",
	msg.c_str(),oper->c_str(),msg.getValue("from"),msg.getValue("to"));

    XMLElement* xml = XMPPUtils::createIq((get ? XMPPUtils::IqGet : XMPPUtils::IqSet),
	msg.getValue("from"),msg.getValue("to"),msg.getValue("id"));

    XMPPNamespace::Type ns = XMPPNamespace::Roster;
    if (dynamic)
	ns = XMPPNamespace::DynamicRoster;
    XMLElement* query = XMPPUtils::createElement(XMLElement::Query,ns);
    // Fill items
    unsigned int nParams = msg.length();
    String prefix = "contact.";
    int n = msg.getIntValue("contact.count");
    for (int i = 1; i <= n; i++) {
	String pref = prefix + String(i);
	NamedString* jid = msg.getParam(pref);
	if (!(jid && *jid))
	    continue;
	XMLElement* item = new XMLElement(XMLElement::Item);
	item->setAttributeValid("jid",*jid);

	// Get data
	if (get) {
	    query->addChild(item);
	    continue;
	}

	// Set item attributes/children
	// Dynamic: all params are children
	// Roster: all params except for 'group' are attributes
	pref << ".";
	for (unsigned int j = 0; j < nParams; j++) {
	    NamedString* ns = msg.getParam(j);
	    if (!(ns && ns->name().startsWith(pref)))
		continue;
	    String tmp = ns->name().substr(pref.length());
	    if (!tmp)
		continue;
	    if (dynamic || tmp == s_group)
		item->addChild(new XMLElement(tmp,0,*ns));
	    else
		item->setAttributeValid(tmp,*ns);
	}
        query->addChild(item);
    }

    xml->addChild(query);
    return xmppGenerate(msg,xml);
}

// user.info handler
bool YJingleFeatures::handleUserInfo(Message& msg)
{
    // Don't handle jingle(features) messages
    if (!acceptMsg(msg,true) || isJingleMsg(msg))
	return false;

    NamedString* oper = msg.getParam("operation");
    if (!oper)
	return false;
    bool get = (*oper == "request");
    if (!get) {
	if (*oper == "update")
	    get = false;
	else
	    return false;
    }
    bool priv = msg.getBoolValue("private");

    if (!s_handleVCard || (priv && !s_handlePrivate))
	return false;
    Debug(this,DebugAll,"Processing '%s' operation=%s from=%s to=%s",
	msg.c_str(),oper->c_str(),msg.getValue("from"),msg.getValue("to"));

    XMLElement* xml = 0;
    if (!priv) {
	xml = XMPPUtils::createVCard(get,msg.getValue("from"),
	    msg.getValue("to"),msg.getValue("id"));
	XMLElement* vcard = !get ? xml->findFirstChild(XMLElement::VCard) : 0;
	if (vcard) {
	    // Name
	    const char* first = msg.getValue("name.first");
	    const char* middle = msg.getValue("name.middle");
	    const char* last = msg.getValue("name.last");
	    String firstN, lastN;
	    // Try to build elements if missing
	    if (!(first || last || middle)) {
		String* tmp = msg.getParam("name");
		if (tmp) {
		    int pos = tmp->rfind(' ');
		    if (pos > 0) {
			firstN = tmp->substr(0,pos);
			lastN = tmp->substr(pos + 1);
		    }
		    else
			lastN = *tmp;
		}
		first = firstN.c_str();
		last = lastN.c_str();
	    }
	    XMLElement* n = new XMLElement("N");
	    n->addChild(new XMLElement("GIVEN",0,first));
	    n->addChild(new XMLElement("MIDDLE",0,middle));
	    n->addChild(new XMLElement("FAMILY",0,last));
	    vcard->addChild(n);
	    TelEngine::destruct(vcard);
	}
    }
    else {
	xml = XMPPUtils::createIq(get ? XMPPUtils::IqGet : XMPPUtils::IqSet,
	    msg.getValue("from"),msg.getValue("to"),msg.getValue("id"));
	XMLElement* query = XMPPUtils::createElement(XMLElement::Query,XMPPNamespace::IqPrivate);
	addChildren(msg,*query);
	xml->addChild(query);
    }

    return xmppGenerate(msg,xml);
}

// Build and dispatch an "xmpp.generate" message. Copy error param on failure
// Consume the xml element
bool YJingleFeatures::xmppGenerate(Message& recv, XMLElement* xml, bool rsp)
{
    // Make sure we have an 'id' for debug purposes
    if (xml && !rsp) {
	const char* id = xml->getAttribute("id");
	if (!id)
	    xml->setAttribute("id",String((int)::random()));
    }
    Message* m = buildMsg("xmpp.generate",&recv);
    m->addParam("protocol","xmpp");
    m->addParam(new NamedPointer("xml",xml,""));
    bool ok = Engine::dispatch(m);
    if (!ok)
	recv.copyParams(*m,"error");
    TelEngine::destruct(m);
    return ok;
}

// Unload the module: uninstall the relays
bool YJingleFeatures::unload()
{
    DDebug(this,DebugAll,"Cleanup");
    if (!lock(500000))
	return false;
    uninstallRelays();
    unlock();
    return true;
}

// Build an XML element's children from a list
bool YJingleFeatures::addChildren(NamedList& params, XMLElement& xml)
{
    String prefix = params.getValue("message-prefix");
    if (!prefix)
	return false;
    prefix << ".";
    bool added = false;
    for (unsigned int i = 1; i < 0xffffffff; i++) {
	String childPrefix(prefix + String(i));
	if (!params.getValue(childPrefix))
	    break;
	xml.addChild(new XMLElement(params,childPrefix));
	added = true;
    }
    return added;
}

// Add an XML element's children to a list
// The element's name will be used as message-prefix
bool YJingleFeatures::addChildren(XMLElement& xml, NamedList& params)
{
    String pref = xml.name();
    params.addParam("message-prefix",pref);
    unsigned int n = 0;
    pref << ".";
    XMLElement* ch = xml.findFirstChild();
    bool added = ch != 0;
    for (; ch; ch = xml.findNextChild(ch)) {
	String tmpPref = pref;
	tmpPref << ++n;
	params.addParam(tmpPref,ch->name());
	tmpPref << ".";
	const char* text = ch->getText();
	if (!null(text))
	    params.addParam(tmpPref,text);
	NamedList tmp("");
	ch->getAttributes(tmp);
	unsigned int count = tmp.length();
	for (unsigned int i = 0; i < count; i++) {
	    NamedString* p = tmp.getParam(i);
	    if (p && p->name())
		params.addParam(tmpPref + p->name(),*p);
	}
    }
    return added;
}

// Check module and target parameters of a received message
bool YJingleFeatures::acceptMsg(Message& msg, bool checkTarget)
{
    NamedString* module = msg.getParam("module");
    if (module && *module == name())
	return false;
    if (checkTarget) {
	NamedString* target = msg.getParam("target");
	return !target || isJingleAlias(*target);
    }
    return true;
}

// Handle DynamicRoster received with xmpp.iq
bool YJingleFeatures::handleXmppIqDynamicRoster(Message& msg, XMLElement& query,
    XMPPUtils::IqType t, const JabberID& from, const JabberID& to, const String& id)
{
    if (!s_handleAddrbook || t != XMPPUtils::IqResult)
	return false;

    Debug(this,DebugAll,"Processing '%s' [DynamicRoster] from=%s to=%s id=%s",
	msg.c_str(),from.c_str(),to.c_str(),id.c_str());

    Message* m = buildMsg("user.roster",&msg);
    m->addParam("operation","notify");
    m->addParam("addressbook",String::boolText(true));
    m->copyParams(msg,"from,to,id");
    if (from.node())
	m->addParam("username",from.node());

    unsigned int n = 0;
    String prefix = "contact.";
    XMLElement* item = query.findFirstChild(XMLElement::Item);
    for (; item; item = query.findNextChild(item,XMLElement::Item)) {
	const char* jid = item->getAttribute("jid");
	if (!(jid && *jid))
	    continue;
	n++;
	String pref = prefix + String(n);
	m->addParam(pref,jid);
	pref << ".";
	for (XMLElement* x = item->findFirstChild(); x; x = item->findNextChild(x))
	    m->addParam(pref + x->name(),x->getText());
    }
    m->addParam("contact.count",String(n));
    Engine::enqueue(m);
    return true;
}

// Handle client private iq data responses
bool YJingleFeatures::handleXmppIqPrivate(Message& msg, XMLElement& query,
    XMPPUtils::IqType t, const JabberID& from, const JabberID& to, const String& id)
{
    if (!s_handlePrivate || t != XMPPUtils::IqResult)
	return false;

    Debug(this,DebugAll,"Processing '%s' [Private] from=%s to=%s id=%s",
	msg.c_str(),from.c_str(),to.c_str(),id.c_str());


    Message* m = buildMsg("user.info",&msg);
    m->addParam("operation","notify");
    m->addParam("private",String::boolText(true));
    m->copyParams(msg,"from,to,id");
    if (from.node())
	m->addParam("username",from.node());
    XMLElement* ch = query.findFirstChild();
    if (ch) {
	addChildren(*ch,*m);
	TelEngine::destruct(ch);
    }
    Engine::enqueue(m);
    return true;
}

// Handle a valid vcard received with xmpp.iq
bool YJingleFeatures::handleXmppIqVCard(Message& msg, XMLElement& vcard,
    XMPPUtils::IqType t, const JabberID& from, const JabberID& to, const String& id)
{
    if (!s_handleVCard || t != XMPPUtils::IqResult)
	return false;

    Debug(this,DebugAll,"Processing '%s' [VCard] from=%s to=%s id=%s",
	msg.c_str(),from.c_str(),to.c_str(),id.c_str());

    Message* m = buildMsg("user.info",&msg);
    m->addParam("operation","notify");
    m->addParam("vcard",String::boolText(true));
    m->copyParams(msg,"from,to,id");
    if (from.node())
	m->addParam("username",from.node());

    XMLElement* n = vcard.findFirstChild("N");
    if (n) {
	String name;
	const char* given = getChildText(*n,"GIVEN");
	if (given) {
	    m->addParam("name.first",given);
	    name << given;
	}
	const char* middle = getChildText(*n,"MIDDLE");
	if (middle) {
	    m->addParam("name.middle",middle);
	    name.append(middle," ");
	}
	const char* family = getChildText(*n,"FAMILY");
	if (family) {
	    m->addParam("name.last",family);
	    name.append(family," ");
	}
	if (name)
	    m->addParam("name",name);
	TelEngine::destruct(n);
    }

    Engine::enqueue(m);
    return true;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
