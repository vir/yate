/**
 * sipfeatures.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Additional SIP features
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
#include <yatexml.h>

#include <stdlib.h>

using namespace TelEngine;
namespace { // anonymous

// Features module
class YSipFeatures : public Module
{
public:
    enum Event {
	Dialog  = 0,                     // dialog
	MWI     = 1,                     // message-sumary
    };

    enum Content {
	AppDlgInfoXml,                   // application/dialog-info+xml
	AppSimpleMsgSummary,             // application/simple-message-summary
    };

    YSipFeatures();
    virtual ~YSipFeatures();
    virtual void initialize();
    inline bool forceDlgID() const
	{ return m_forceDlgID; }
    // Check expiring time from a received message.
    // Look first at 'sip_expires' parameter and, if missing or emtpy, look at optional 'param'
    // @param noExpires Default value to use if failed to get an expiring value. -1 to use the default one
    // Return a negative value on failure or the expiring time on success
    // Failure: set the response to 423 (interval too brief) and to 'osip_Min-Expires' parameter of msg
    int checkExpire(Message& msg, int noExpires = -1, const char* param = 0);
private:
    int m_expiresMin;                    // Minimum accepted value for expires
    int m_expiresMax;                    // Maximum accepted value for expires
    int m_expiresDef;                    // Default value for expires
    bool m_forceDlgID;                   // Append dialog data if missing on dialog state notifications
};

// Expiring time values: min/default/max
#define EXPIRES_MIN 60
#define EXPIRES_DEF 600
#define EXPIRES_MAX 3600

#define KNOWN_EVENTS 2

static bool s_verboseXml = true;        // Build verbose XML body (add line breaks and spaces)
static YSipFeatures s_module;
static TokenDict s_allowedEvents[KNOWN_EVENTS+1];

// List of known events
static TokenDict s_events[KNOWN_EVENTS+1] = {
	{"dialog",          YSipFeatures::Dialog},
	{"message-summary", YSipFeatures::MWI},
	{0,0}
	};

// List of known content types
static TokenDict s_contents[] = {
	{"application/dialog-info+xml",        YSipFeatures::AppDlgInfoXml},
	{"application/simple-message-summary", YSipFeatures::AppSimpleMsgSummary},
	{0,0}
	};


// sip.subscribe handler
class YSipSubscribeHandler : public MessageHandler
{
public:
    YSipSubscribeHandler(int prio = 100)
	: MessageHandler("sip.subscribe",prio,s_module.name())
	{ }
    virtual bool received(Message &msg);
    // Get the event from a received message. Set the content type
    // Set 'code' parameter of the message if false is returned
    bool getEventData(Message& msg, int& event, String& evName, String& content);
};

// resource.notify handler
class YSipNotifyHandler : public MessageHandler
{
public:
    YSipNotifyHandler(int prio = 100)
	: MessageHandler("resource.notify",prio,s_module.name())
	{ }
    virtual bool received(Message &msg);
    // Create the body for 'dialog' event notification
    void createDialogBody(String& dest, const Message& src, const String& entity);
    // Create the body for 'message-summary' event notification
    void createMWIBody(String& dest, const Message& src);
};


// Escape a string to be packed into another
inline void appendEsc(String& dest, const String& src1, const String& src2, const char sep = ' ')
{
    String tmp;
    tmp << src1 << sep << src2;
    dest << tmp.msgEscape() << "\n";
}

// Force a parameter of a message
inline void forceParam(Message& msg, const char* param, const char* value)
{
    const char* p = msg.getValue(param);
    if (!(p && *p))
	msg.setParam(param,value);
}

/**
 * YSipSubscribeHandler
 */
// resource.subscribe parameters:
// event
//   Keyword indicating the event: dialog (subscription to call state events),
//   message-summary (message waiting subscription)
// operation
//   Keyword indicating the request: subscribe (request a subscription),
//   unsubscribe (request to unsubscribe from event)
// expires
//   Integer indicating the subscription duration (if operation is subscribe).
//   If 0, the subscription won't expire
// subscriber
//   The requestor
// notifier
//   The resource (user) to subscribe to
// notifyto
//   The URI used as destination when notifying
// data
//   Data used by protocol
bool YSipSubscribeHandler::received(Message &msg)
{
    // Check received data
    String evName, content;
    int event;
    if (!getEventData(msg,event,evName,content))
	return false;
    NamedString* contact = msg.getParam("sip_contact");
    if (TelEngine::null(contact)) {
	Debug(&s_module,DebugNote,"SUBSCRIBE with missing or empty contact");
	msg.setParam("code","400");
	return false;
    }
    // Expiration time
    int expires = -1;
    // Set default expiring time
    // See draft-ietf-sipping-dialog-package-06.txt (dialog), RFC3842 (message-summary)
    switch (event) {
	case YSipFeatures::Dialog:
	case YSipFeatures::MWI:
	    expires = 3600;
	    break;
    }
    expires = s_module.checkExpire(msg,expires);
    if (expires == -1)
	return false;
    String sExpires(expires);

    Message m("resource.subscribe");
    m.addParam("event",evName);
    if (expires) {
	m.addParam("operation","subscribe");
	m.addParam("expires",sExpires);
    }
    else
	m.addParam("operation","unsubscribe");
    m.addParam("subscriber",msg.getValue("username"));

    String notifyTo = *contact;
    static Regexp r("<\\([^>]\\+\\)>");
    if (notifyTo.matches(r))
	notifyTo = notifyTo.matchString(1);
    m.addParam("notifyto",notifyTo);

    URI uriRequest(msg.getValue("sip_uri"));
    m.addParam("notifier",uriRequest.getUser());
    m.addParam("notifier_domain",uriRequest.getHost(),false);
    // Pack data parameters
    String data;
    appendEsc(data,"host",msg.getValue("ip_host"));
    appendEsc(data,"port",msg.getValue("ip_port"));
    appendEsc(data,"uri",notifyTo);
    const String& conn = msg["connection_id"];
    if (conn)
	appendEsc(data,"connection_id",conn);
    String from = msg.getValue("sip_to");
    if (-1 == from.find("tag="))
	from << ";tag=" << msg.getValue("xsip_dlgtag");
    appendEsc(data,"sip_From",from);
    appendEsc(data,"sip_To",msg.getValue("sip_from"));
    appendEsc(data,"sip_Call-ID",msg.getValue("sip_callid"));
    String uri = msg.getValue("sip_uri");
    appendEsc(data,"sip_Contact","<" + uri + ">");
    appendEsc(data,"sip_Event",evName);
    if (content)
	appendEsc(data,"xsip_type",content);
    m.addParam("data",data);

    XDebug(&s_module,DebugAll,
	"SUBSCRIBE. notifier=%s subscriber=%s event=%s notifyto=%s",
	m.getValue("notifier"),m.getValue("subscriber"),
	evName.c_str(),notifyTo.c_str());

    if (!Engine::dispatch(m))
	return false;

    msg.setParam("osip_Expires",sExpires);
    msg.setParam("osip_Contact",*contact);
    msg.setParam("code","200");
    return true;
}

// Get the event from a received message. Set the content type
bool YSipSubscribeHandler::getEventData(Message& msg, int& event, String& evName,
	String& content)
{
    NamedString* tmp = msg.getParam("sip_event");
    evName = tmp ? tmp->c_str() : "";
    event = lookup(evName,s_allowedEvents,-1);
    // RFC3265 3.1.2: An Event header MUST be present
    // draft-ietf-sipping-dialog-package-06.txt The Event header for 'dialog' event
    //   may contain dialog identifier(s). Reject them
    if (!(tmp && *tmp && event != -1)) {
	if (!tmp)
	    Debug(&s_module,DebugNote,
		"SUBSCRIBE. Can't handle event='%s'. Event header is missing",
		evName.c_str());
	msg.setParam("code",tmp?"489":"400");     // Bad event or request
	return false;
    }

    // Set content type
    int defType = -1;
    switch (event) {
	case YSipFeatures::Dialog:
	    defType = YSipFeatures::AppDlgInfoXml;        // draft-ietf-sipping-dialog-package-06.txt
	    break;
	case YSipFeatures::MWI:
	    defType = YSipFeatures::AppSimpleMsgSummary;  // RFC3842
	    break;
    }
    content = lookup(defType,s_contents);
    // Check if an Accept header is present: if so, it MUST contain the content
    // type we know how to handle
    String accept = msg.getValue("sip_Accept");
    if (content && accept) {
	ObjList* obj = accept.split(',');
	if (!obj->find(content))
	    content = "";
	TelEngine::destruct(obj);
    }
    if (content)
	return true;

    // Can't choose a content
    Debug(&s_module,DebugNote,
	"SUBSCRIBE. Can't handle content type. accept='%s' event='%s'",
	accept.c_str(),evName.c_str());
    msg.setParam("code","406");          // Not acceptable
    return false;
}


/**
 * YSipNotifyHandler
 */
// resource.notify parameters
// event
//   Keyword indicating the event: dialog (subscription to call state events),
//   message-summary (message waiting subscription)
// expires
//   Optional integer indicating the remaining time of the subscription
// subscriber
//   The entity that requested the subscription
// notifier
//   The resource (user) making the notification
// notifyto
//   URI used as destination for the notification
// data
//   Data used by protocol
// notifyseq
//   Integer indicating the sequence number of the notification within the subscription
//   given by notifier and event
// subscriptionstate
//   Keyword indicating the subscription state: pending/active/terminated
// terminatereason
//   Optional subscription termination reason if subscriptionstate is terminated
//
// Event specific parameters are prefixed by the event name:
// dialog.id
//   The id of the dialog if any
// dialog.callid
//   The dialog identifier
// dialog.localtag
//   The local tag component of the dialog identifier
// dialog.remotetag
//   The remote tag component of the dialog identifier
// dialog.direction
//   Keyword indicating the call direction from Yate's point of view: incoming/outgoing
// dialog.remoteuri
//   The notifier dialog peer's URI
// dialog.state
//   Keyword indicating the call state: trying/confirmed/early/rejected/terminated
// message-summary.voicenew
//   Optional integer specifying the number of unread (new) voice messages
// message-summary.voiceold
//   Optional integer specifying the number of read (old) voice messages
bool YSipNotifyHandler::received(Message &msg)
{
    String notifyto = msg.getValue("notifyto");
    if (!notifyto.startsWith("sip:"))
	return false;
    NamedString* event = msg.getParam("event");
    int evType = event ? lookup(*event,s_allowedEvents,-1) : -1;

    Message m("xsip.generate");
    m.addParam("method","NOTIFY");
    // Copy notify parameters
    String data = msg.getValue("data");
    ObjList* obj = data.split('\n');
    for (ObjList* o = obj->skipNull(); o; o = o->skipNext()) {
	String s = (static_cast<String*>(o->get()))->msgUnescape();
	int pos = s.find(" ");
	if (pos == -1)
	    continue;
	m.addParam(s.substr(0,pos),s.substr(pos+1));
    }
    TelEngine::destruct(obj);
    String ss = msg.getValue("subscriptionstate");
    if (ss) {
	const char* res = msg.getValue("terminatereason");
	if (res)
	    ss << ";" << res;
	m.addParam("sip_Subscription-State",ss);
    }
    else
	m.addParam("sip_Subscription-State","active");
    const char* exp = msg.getValue("expires");
    if (exp && *exp)
	m.addParam("sip_Expires",exp);
    // Check event & Create body
    String body;
    switch (evType) {
	case YSipFeatures::Dialog: {
		URI uri(m.getValue("sip_From"));
		String entity;
		entity << "sip:" << uri.getUser() << "@" << uri.getHost();
		if (s_module.forceDlgID()) {
		    String id = msg.getValue("dialog.id");
		    if (id) {
			forceParam(msg,"dialog.callid",id);
			forceParam(msg,"dialog.localtag",id);
			forceParam(msg,"dialog.remotetag",id);
			forceParam(msg,"dialog.remoteuri",entity);
		    }
		}
		createDialogBody(body,msg,entity);
	    }
	    break;
	case YSipFeatures::MWI:
	    createMWIBody(body,msg);
	    break;
	default:
	    Debug(&s_module,DebugNote,"NOTIFY. Invalid event='%s'",
		event?event->c_str():"");
	    return false;
    }
    m.addParam("xsip_body",body);

    XDebug(&s_module,DebugAll,
	"NOTIFY. notifier=%s subscriber=%s event=%s notifyto=%s",
	msg.getValue("notifier"),msg.getValue("subscriber"),
	event->c_str(),msg.getValue("notifyto"));
    return Engine::dispatch(m);
}

// Create the body for 'dialog' event notification
void YSipNotifyHandler::createDialogBody(String& dest, const Message& src,
	const String& entity)
{
    dest = "";
    dest << "<?xml version=\"1.0\"?>";
    XmlElement* xml = new XmlElement("dialog-info");
    xml->setXmlns(String::empty(),true,"urn:ietf:params:xml:ns:dialog-info");
    xml->setAttributeValid("version",src.getValue("notifyseq"));
    String prefix;
    if (src.getBoolValue("cdr"))
	prefix = "cdr.";
    // We always send partial data (only dialogs changed since last notification)
    // state will be 'full' if we send data for all active dialogs
    const char* id = src.getValue("dialog.id");
    xml->setAttribute("notify-state",(prefix || id) ? "partial" : "full");
    xml->setAttributeValid("entity",entity);
    // Append dialog data
    XmlElement* dlg = 0;
    if (prefix == "cdr.") {
	const char* state = 0;
	const String& oper = src[prefix + "operation"];
	// Set dialog status
	if (oper == "initialize")
	    state = "trying";
	else if (oper == "finalize")
	    state = "terminated";
	else {
	    const String& status = src[prefix + "status"];
	    if (status == "connected" || status == "answered")
		state = "confirmed";
	    else if (status == "incoming" || status == "outgoing" ||
		status == "calling" || status == "ringing" || status == "progressing")
		state = "early";
	    else if (status == "redirected")
		state = "rejected";
	    else if (status == "destroyed")
		state = "terminated";
	}
	// Set dialog direction
	const char* direction = 0;
	if (state) {
	    // Directions are reversed because we are talking about the remote end
	    const String& dir = src[prefix + "direction"];
	    if (dir == "incoming")
		direction = "initiator";
	    else if (dir == "outgoing")
		direction = "recipient";
	}
	const String& id = direction ? src[prefix + "chan"] : String::empty();
	if (id) {
	    dlg = new XmlElement("dialog");
	    dlg->setAttribute("id",id);
	    dlg->setAttribute("call-id",id);
	    const char* external = src.getValue(prefix + "external");
	    dlg->setAttributeValid("local-tag",src.getValue(prefix + "local-tag",external));
	    dlg->setAttributeValid("remote-tag",src.getValue(prefix + "remote-tag",external));
	    dlg->setAttribute("direction",direction);
	    // "state" child of "dialog"
	    XmlElement* st = new XmlElement("state");
	    st->addText(state);
	    dlg->addChild(st);
	    // "remote" child of "dialog"
	    XmlElement* remote = new XmlElement("remote");
	    XmlElement* target = new XmlElement("target");
	    target->setAttributeValid("uri",entity);
	    remote->addChild(target);
	    dlg->addChild(remote);
	}
    }
    else {
	const char* state = src.getValue("dialog.state");
	if (id && *id && state && *state) {
	    dlg = new XmlElement("dialog");
	    dlg->setAttribute("id",id);
	    dlg->setAttributeValid("call-id",src.getValue("dialog.callid"));
	    dlg->setAttributeValid("local-tag",src.getValue("dialog.localtag"));
	    dlg->setAttributeValid("remote-tag",src.getValue("dialog.remotetag"));
	    String dir = src.getValue("dialog.direction");
	    if (dir == "incoming")
		dlg->setAttribute("direction","initiator");
	    else if (dir == "outgoing")
		dlg->setAttribute("direction","recipient");
	    // "state" child of "dialog"
	    XmlElement* st = new XmlElement("state");
	    st->addText(state);
	    dlg->addChild(st);
	    // "remote" child of "dialog"
	    String tmp = src.getValue("dialog.remoteuri");
	    if (tmp) {
		XmlElement* remote = new XmlElement("remote");
		XmlElement* target = new XmlElement("target");
		target->setAttribute("uri",tmp);
		remote->addChild(target);
		dlg->addChild(remote);
	    }
	}
    }
    if (dlg)
	xml->addChild(dlg);
    if (s_verboseXml)
	xml->toString(dest,true,"\r\n","  ");
    else
	xml->toString(dest);
    dest << "\r\n";
    TelEngine::destruct(xml);
}

// Create the body for 'message-summary' event notification
void YSipNotifyHandler::createMWIBody(String& dest, const Message& src)
{
    // See RFC3458 6.2 for message classes
    dest = "Messages-Waiting: ";
    unsigned int n = (unsigned int)src.getIntValue("message-summary.voicenew",0);
    unsigned int o = (unsigned int)src.getIntValue("message-summary.voiceold",0);
    if (n || o) {
	dest << (n ? "yes" : "no");
	dest << "\r\nVoice-Message: " << n << "/" << o << "\r\n";
    }
    else
	dest << "no\r\n";
}


/**
 * YSipFeatures
 */
YSipFeatures::YSipFeatures()
    : Module("sipfeatures","misc")
{
    Output("Loaded module SIP Features");
}

YSipFeatures::~YSipFeatures()
{
    Output("Unloading module SIP Features");
}

void YSipFeatures::initialize()
{
    Output("Initializing module SIP Features");
    static bool first = true;
    Configuration cfg(Engine::configFile("sipfeatures"));
    m_expiresMin = cfg.getIntValue("general","expires_min",EXPIRES_MIN);
    m_expiresMax = cfg.getIntValue("general","expires_max",EXPIRES_MAX);
    m_expiresDef = cfg.getIntValue("general","expires_def",EXPIRES_DEF);
    m_forceDlgID = cfg.getBoolValue("general","forcedialogdata",true);
    s_verboseXml = cfg.getBoolValue("general","verbosexml",true);

    // Build the list of allowed events
    NamedList* evs = cfg.getSection("allow_events");
    bool def = evs ? evs->getBoolValue("default",true) : true;
    int iAllowed = 0;
    for (int i= 0; i < KNOWN_EVENTS; i++) {
	if (!s_events[i].token)
	    break;
	NamedString* ns = evs ? evs->getParam(s_events[i].token) : 0;
	bool allowed = ns ? ns->toBoolean(def) : def;
	if (allowed) {
	    s_allowedEvents[iAllowed].token = s_events[i].token;
	    s_allowedEvents[iAllowed++].value = s_events[i].value;
	}
    }
    s_allowedEvents[iAllowed].token = 0;
    s_allowedEvents[iAllowed].value = 0;

    if (debugAt(DebugNote)) {
	String tmp;
	for (int i= 0; s_allowedEvents[i].token; i++)
	    tmp.append(s_allowedEvents[i].token,",");
	if (tmp)
	    Debug(this,DebugAll,"Allowed subscriptions: %s",tmp.c_str());
	else
	    Debug(this,DebugNote,"Subscriptions not allowed");
    }

    // Done with reload options
    if (!first)
	return;
    first = false;
    setup();
    Engine::install(new YSipSubscribeHandler);
    Engine::install(new YSipNotifyHandler);
}

// Check expiring time from a received message.
int YSipFeatures::checkExpire(Message& msg, int noExpires, const char* param)
{
    String tmp(msg.getValue("sip_expires"));
    if (!tmp && param)
	tmp = msg.getValue(param);
    int expires = tmp.toInteger(-1);
    if (expires < 0)
	expires = (noExpires < 0 ? m_expiresDef : noExpires);
    if (expires > m_expiresMax)
	expires = m_expiresMax;
    if (expires && (expires < m_expiresMin)) {
	msg.setParam("osip_Min-Expires",String(m_expiresMin));
	msg.setParam("code","423");      // Interval too brief
	return -1;
    }
    return expires;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
