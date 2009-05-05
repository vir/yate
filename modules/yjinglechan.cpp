/**
 * yjinglechan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Jingle channel
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
 * Author: Marian Podgoreanu
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <yatephone.h>
#include <yatemime.h>
#include <yateversn.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>

#include <yatejingle.h>

using namespace TelEngine;

namespace { // anonymous

class YJBEngine;                         // Jabber engine. Initiate protocol from Yate run mode
class YJGEngine;                         // Jingle service
class YJBMessage;                        // Message service
class YJBStreamService;                  // Stream start/stop event service
class YJBClientPresence;                 // Presence service for client streams
class YJBPresence;                       // Presence service
class YJBIqService;                      // Handle 'iq' stanzas not processed by other services
class YJGConnection;                     // Jingle channel
class YJGTransfer;                       // Transfer thread (route and execute)
class ResNotifyHandler;                  // resource.notify handler
class ResSubscribeHandler;               // resource.subscribe handler
class UserLoginHandler;                  // user.login handler
class XmppGenerateHandler;               // xmpp.generate handler
class XmppIqHandler;                     // xmpp.iq handler used to respond to unprocessed set/get stanzas
class YJGDriver;                         // The driver

// TODO:
//  Negotiate DTMF method. Accept remote peer's method;

// URI
#define BUILD_XMPP_URI(jid) (plugin.name() + ":" + jid)


/**
 * YJBEngine
 */
class YJBEngine : public JBEngine
{
public:
    inline YJBEngine(Protocol proto) : JBEngine(proto)
	{}
    virtual bool exiting() const
	{ return Engine::exiting(); }
    void initialize();
    // Setup the transport layer security for a stream
    virtual bool encryptStream(JBStream* stream);
};

/**
 * YJGEngine
 */
class YJGEngine : public JGEngine
{
public:
    inline YJGEngine(YJBEngine* engine, int prio)
	: JGEngine(engine,0,prio), m_requestSubscribe(true)
	{}
    inline bool requestSubscribe() const
	{ return m_requestSubscribe; }
    void initialize();
    virtual void processEvent(JGEvent* event);
private:
    bool m_requestSubscribe;             // Request subscribe before making a call
};

/**
 * YJBMessage
 */
class YJBMessage : public JBMessage
{
public:
    inline YJBMessage(YJBEngine* engine, int prio)
	: JBMessage(engine,0,prio)
	{}
    void initialize();
    virtual void processMessage(JBEvent* event);
};

/**
 * YJBStreamService
 */
class YJBStreamService : public JBService
{
public:
    YJBStreamService(JBEngine* engine, int prio)
	: JBService(engine,"jabberstreamservice",0,prio)
	{}
    virtual ~YJBStreamService()
	{}
    void initialize();
protected:
    // Process stream termination events
    virtual bool accept(JBEvent* event, bool& processed, bool& insert);
};

/**
 * YJBClientPresence
 */
class YJBClientPresence : public JBService
{
public:
    YJBClientPresence(JBEngine* engine, int prio)
	: JBService(engine,"clientpresence",0,prio)
	{}
    virtual ~YJBClientPresence()
	{}
    void initialize();
protected:
    // Process stream termination events
    virtual bool accept(JBEvent* event, bool& processed, bool& insert);
};

/**
 * YJBPresence
 */
class YJBPresence : public JBPresence
{
    friend class YUserPresence;
public:
    inline YJBPresence(JBEngine* engine, int prio)
	: JBPresence(engine,0,prio)
	{}
    void initialize();
    // Overloaded methods
    virtual bool notifyProbe(JBEvent* event);
    virtual bool notifySubscribe(JBEvent* event, Presence presence);
    virtual void notifySubscribe(XMPPUser* user, Presence presence);
    virtual bool notifyPresence(JBEvent* event, bool available);
    virtual void notifyPresence(XMPPUser* user, JIDResource* resource);
    virtual void notifyNewUser(XMPPUser* user);
    // Create & enqueue a message from received presence parameter.
    // Add status/operation/subscription parameters
    static Message* message(int presence, const char* from, const char* to,
	const char* subscription);
};

/**
 * YJBIqService
 */
class YJBIqService : public JBService
{
public:
    YJBIqService(JBEngine* engine, int prio)
	: JBService(engine,"jabberiqservice",0,prio)
	{}
    void initialize();
protected:
    // Process iq events
    virtual bool accept(JBEvent* event, bool& processed, bool& insert);
};

/**
 * YJGConnection
 */
class YJGConnection : public Channel
{
    YCLASS(YJGConnection,Channel)
    friend class YJGTransfer;
public:
    enum State {
	Pending,
	Active,
	Terminated,
    };
    // Flags controlling the state of the data source/consumer
    enum DataFlags {
	OnHoldRemote = 0x0001,           // Put on hold by remote party
	OnHoldLocal  = 0x0002,           // Put on hold by peer
	OnHold = OnHoldRemote | OnHoldLocal,
    };
    // File transfer status
    enum FileTransferStatus {
	FTNone,                          // No file transfer allowed
	FTIdle,                          // Nothing done yet
	FTWaitEstablish,                 // Waiting for SOCKS to be negotiated
	FTEstablished,                   // Transport succesfully setup
	FTRunning,                       // Running
	FTTerminated                     // Terminated
    };
    // File transfer host sender
    enum FileTransferHostSender {
	FTHostNone = 0,
	FTHostLocal,
	FTHostRemote,
    };
    // Outgoing constructor
    YJGConnection(Message& msg, const char* caller, const char* called, bool available,
	const char* file);
    // Incoming contructor
    YJGConnection(JGEvent* event);
    virtual ~YJGConnection();
    inline State state() const
	{ return m_state; }
    inline const JabberID& local() const
	{ return m_local; }
    inline const JabberID& remote() const
	{ return m_remote; }
    inline const String& reason() const
	{ return m_reason; }
    // Check session id
    inline bool isSid(const String& sid) {
	    Lock lock(m_mutex);
	    return m_session && sid == m_session->sid();
	}
    // Get jingle session id
    inline bool getSid(String& buf) {
	    Lock lock(m_mutex);
	    if (!m_session)
		return false;
	    buf = m_session->sid();
	    return true;
	}
    // Overloaded methods from Channel
    virtual void callAccept(Message& msg);
    virtual void callRejected(const char* error, const char* reason, const Message* msg);
    virtual bool callRouted(Message& msg);
    virtual void disconnected(bool final, const char* reason);
    virtual bool msgProgress(Message& msg);
    virtual bool msgRinging(Message& msg);
    virtual bool msgAnswered(Message& msg);
    virtual bool msgUpdate(Message& msg);
    virtual bool msgText(Message& msg, const char* text);
    virtual bool msgDrop(Message& msg, const char* reason);
    virtual bool msgTone(Message& msg, const char* tone);
    virtual bool msgTransfer(Message& msg);
    inline bool disconnect(const char* reason) {
	setReason(reason);
	return Channel::disconnect(m_reason);
    }
    // Route an incoming call
    bool route();
    // Process Jingle and Terminated events
    // Return false to terminate
    bool handleEvent(JGEvent* event);
    void hangup(const char* reason, const char* text = 0);
    // Process remote user's presence changes.
    // Make the call if outgoing and in Pending (waiting for presence information) state
    // Hangup if the remote user is unavailbale
    // Return true to disconnect
    bool presenceChanged(bool available);
    // Process a transfer request
    // Return true if the event was accepted
    bool processTransferRequest(JGEvent* event);
    // Transfer terminated notification from transfer thread
    void transferTerminated(bool ok, const char* reason = 0);
    // Get the remote party address (actually this is the address of the 
    //  local party's server)
    void getRemoteAddr(String& dest);
    // Process chan.notify messages
    // Handle SOCKS status changes for file transfer
    bool processChanNotify(Message& msg);

    // Check if a transfer can be initiated
    inline bool canTransfer() const
	{ return m_session && !m_transferring && isAnswered() && m_ftStatus == FTNone; }

    inline void updateResource(const String& resource) {
	if (!m_remote.resource() && resource)
	    m_remote.resource(resource);
    }
    inline void setReason(const char* reason) {
	if (!m_reason)
	    m_reason = reason;
    }
    // Check the status of the given data flag(s)
    inline bool dataFlags(int mask)
	{ return 0 != (m_dataFlags & mask); }

protected:
    // Process an ActContentAdd event
    void processActionContentAdd(JGEvent* event);
    // Process an ActContentAdd event
    void processActionTransportInfo(JGEvent* event);
    // Update a received candidate. Return true if changed
    bool updateCandidate(unsigned int component, JGSessionContent& local,
	JGSessionContent& recv);
    // Add a new content to the list
    void addContent(bool local, JGSessionContent* c);
    // Remove a content from list
    void removeContent(JGSessionContent* c);
    // Reset the current audio content
    // If the content is not re-usable (SRTP with local address),
    //  add a new identical content and remove the old old one from the session
    // Clear the endpoint
    void removeCurrentAudioContent(bool removeReq = false);
    // This method is used to set the current audio content
    // Clear the endpoint if the current content is replaced
    // Reset the current content. Try to use the given content
    // Else, find the first available content and try to use it
    // Send a transport info for the new current content
    // Return false on error
    bool resetCurrentAudioContent(bool session, bool earlyMedia,
	bool sendTransInfo = true, JGSessionContent* newContent = 0);
    // Start RTP for the current content
    // For raw udp transports, sends a 'trying' session info
    bool startRtp();
    // Check a received candidate's parameters
    // Return false if some parameter's value is incorrect
    bool checkRecvCandidate(JGSessionContent& content, JGRtpCandidate& candidate);
    // Check a received content(s). Fill received lists with accepted/rejected content(s)
    // The lists don't own their pointers
    // Return false on error
    bool processContentAdd(const JGEvent& event, ObjList& ok, ObjList& remove);
    // Remove contents. Confirm the received event
    // Return false if there are no more contents
    bool removeContents(JGEvent* event);
    // Build a RTP audio content. Add used codecs to the list
    // Build and init the candidate(s) if the content is a raw udp one
    JGSessionContent* buildAudioContent(JGRtpCandidates::Type type,
	JGSessionContent::Senders senders = JGSessionContent::SendBoth,
	bool rtcp = false, bool useFormats = true);
    // Build a file transfer content
    JGSessionContent* buildFileTransferContent(bool send, const char* filename,
	NamedList& params);
    // Reserve local port for a RTP session content
    bool initLocalCandidates(JGSessionContent& content, bool sendTransInfo);
    // Match a local content agaist a received one
    // Return false if there is no common media
    bool matchMedia(JGSessionContent& local, JGSessionContent& recv) const;
    // Find a session content in a list
    JGSessionContent* findContent(JGSessionContent& recv, const ObjList& list) const;
    // Set early media to remote
    void setEarlyMediaOut(Message& msg);
    // Enqueue a call.progress message from the current audio content
    // Used for early media
    void enqueueCallProgress();
    // Init/start file transfer. Try to change host direction on failure
    // If host dir succeeds, still return false, but don't terminate transfer
    bool setupSocksFileTransfer(bool start);
    // Change host sender. Return false on failure
    bool changeFTHostDir();
    // Get the RTP direction param from a content
    // FIXME: ignore content senders for early media ?
    inline const char* rtpDir(const JGSessionContent& c) {
	    if (c.senders() == JGSessionContent::SendInitiator)
		return isOutgoing() ? "send" : "receive";
	    if (c.senders() == JGSessionContent::SendResponder)
		return isOutgoing() ? "receive" : "send";
	    return "bidir";
	}
    // Build a RTP candidate
    inline JGRtpCandidate* buildCandidate(bool rtp = true) {
	    return new JGRtpCandidate(id() + "_candidate_" + String((int)::random()),
		rtp ? "1" : "2");
	}
    // Get the first file transfer content
    inline JGSessionContent* firstFTContent() {
	    ObjList* o = m_ftContents.skipNull();
	    return o ? static_cast<JGSessionContent*>(o->get()) : 0;
	}

private:
    // Handle hold/active/mute actions
    // Confirm the received element
    void handleAudioInfoEvent(JGEvent* event);

    Mutex m_mutex;                       // Lock transport and session
    State m_state;                       // Connection state
    JGSession* m_session;                // Jingle session attached to this connection
    JabberID m_local;                    // Local user's JID
    JabberID m_remote;                   // Remote user's JID
    ObjList m_audioContents;             // The list of negotiated audio contents
    JGSessionContent* m_audioContent;    // The current audio content
    String m_callerPrompt;               // Text to be sent to called before calling it
    String m_formats;                    // Formats received in call.execute
    String m_subject;                    // Connection subject
    bool m_sendRawRtpFirst;              // Send raw-rtp transport as the first content of outgoing session
    // Crypto (for contents created by us)
    bool m_useCrypto;
    bool m_cryptoMandatory;
    // Termination
    bool m_hangup;                       // Hang up flag: True - already hung up
    String m_reason;                     // Hangup reason
    // Timeouts
    u_int64_t m_timeout;                 // Timeout for not answered outgoing connections
    // Transfer
    bool m_transferring;                 // The call is already involved in a transfer
    String m_transferStanzaId;           // Sent transfer stanza id used to track the result
    JabberID m_transferTo;               // Transfer target
    JabberID m_transferFrom;             // Transfer source
    String m_transferSid;                // Session id for attended transfer
    XMLElement* m_recvTransferStanza;    // Received iq transfer element
    // On hold data
    int m_dataFlags;                     // The data status
    String m_onHoldOutId;                // The id of the hold stanza sent to remote
    String m_activeOutId;                // The id of the active stanza sent to remote
    // File transfer
    FileTransferStatus m_ftStatus;       // File transfer status
    int m_ftHostDirection;               // Which endpoint can send file transfer hosts
    String m_ftNotifier;                 // The notifier expected in chan.notify
    String m_ftStanzaId;
    String m_dstAddrDomain;              // SHA1(SID + local + remote) used by SOCKS
    ObjList m_ftContents;                // The list of negotiated file transfer contents
    ObjList m_streamHosts;               // The list of negotiated SOCKS stream hosts
};

/**
 * Transfer thread (route and execute)
 */
class YJGTransfer : public Thread
{
public:
    YJGTransfer(YJGConnection* conn, const char* subject = 0);
    virtual void run(void);
private:
    String m_transferorID;           // Transferor channel's id
    String m_transferredID;          // Transferred channel's id
    Driver* m_transferredDrv;        // Transferred driver's pointer
    JabberID m_to;                   // Transfer target
    JabberID m_from;                 // Transfer source
    String m_sid;                    // Session id for unattended transfer
    Message m_msg;
};

/**
 * resource.notify message handler
 */
class ResNotifyHandler : public MessageHandler
{
public:
    ResNotifyHandler() : MessageHandler("resource.notify") {}
    virtual bool received(Message& msg);
    static void process(const JabberID& from, const JabberID& to,
	const String& status, bool subFrom, NamedList* params = 0);
    static void sendPresence(JabberID& from, JabberID& to, const String& status,
	NamedList* params = 0);
};

/**
 * resource.subscribe message handler
 */
class ResSubscribeHandler : public MessageHandler
{
public:
    ResSubscribeHandler() : MessageHandler("resource.subscribe") {}
    virtual bool received(Message& msg);
};

/**
 * user.login handler
 */
class UserLoginHandler : public MessageHandler
{
public:
    UserLoginHandler() : MessageHandler("user.login") {}
    virtual bool received(Message& msg);
};

/**
 * xmpp.generate message handler
 */
class XmppGenerateHandler : public MessageHandler
{
public:
    inline XmppGenerateHandler() : MessageHandler("xmpp.generate") {}
    virtual bool received(Message& msg);
};

/**
 * xmpp.iq message handler
 */
class XmppIqHandler : public MessageHandler
{
public:
    inline XmppIqHandler(int prio = 1000) : MessageHandler("xmpp.iq",prio) {}
    virtual bool received(Message& msg);
};

/**
 * YJGDriver
 */
class YJGDriver : public Driver
{
public:
    // Message handlers
    enum {
	ChanNotify = Private
    };
    // Enumerate protocols supported by this module
    enum Protocol {
	Jabber     = 0,
	Xmpp       = 1,
	Jingle     = 2,
	ProtoCount = 3
    };
    // Additional driver status commands
    enum StatusCommands {
	StatusStreams  = 0,              // Show all streams
	StatusCmdCount = 1
    };
    YJGDriver();
    virtual ~YJGDriver();
    // Check if the channels should send single DTMFs
    inline bool singleTone() const
	{ return m_singleTone; }
    // Inherited methods
    virtual void initialize();
    virtual bool hasLine(const String& line) const;
    virtual bool msgExecute(Message& msg, String& dest);
    // Send IM messages
    virtual bool imExecute(Message& msg, String& dest);
    // Message handler: Disconnect channels, destroy streams, clear rosters
    virtual bool received(Message& msg, int id);
    // Try to create a JID from a message.
    // First try to get the 'username' parameter of the message. Then the 'from' parmeter
    // @param checkDomain True to check if jid's domain is valid
    // Return false if node or domain are 0 or domain is invalid
    bool getJidFrom(JabberID& jid, Message& msg, bool checkDomain = false);
    // Assign param value to jid.
    // @param checkDomain True to check if jid's domain is valid
    // Return false if node or domain are 0 or domain is invalid
    bool decodeJid(JabberID& jid, Message& msg, const char* param,
	bool checkDomain = false);
    // Create the presence notification command
    XMLElement* getPresenceCommand(JabberID& from, JabberID& to, bool available,
	XMLElement* presence = 0);
    // Process presence. Notify connections
    void processPresence(const JabberID& local, const JabberID& remote,
	bool available, bool audio);
    // Create a media string from a list
    void createMediaString(String& dest, ObjList& formats, char sep);
    // Find a connection by local and remote jid, optionally ignore local
    // resource (always ignore if local has no resource)
    YJGConnection* find(const JabberID& local, const JabberID& remote, bool anyResource = false);
    // Build and add XML child elements from a received message
    bool addChildren(NamedList& msg, XMLElement* xml = 0, ObjList* list = 0);
    // Get the destination (callto) from a call/im execute message
    bool getExecuteDest(Message& msg, String& dest);
    // Process a message received by a stream
    void processImMsg(JBEvent& event);
    // Search a client's roster to get a resource
    //  (with audio capabilities) for a subscribed user.
    // Set noSub to true if false is returned and the client
    //  is not subscribed to the remote user (or the remote user is not found).
    // Return false if user or resource is not found
    bool getClientTargetResource(JBClientStream* stream, JabberID& target, bool* noSub = 0);
    // Find a channel by its sid
    YJGConnection* findBySid(const String& sid);
    // Get a copy of the default file transfer proxy
    inline JGStreamHost* defFTProxy() {
	    Lock lock(this);
	    return m_ftProxy ? new JGStreamHost(*m_ftProxy) : 0;
	}

    // Check if this module handles a given protocol
    static bool canHandleProtocol(const String& proto) {
	for (unsigned int i = 0; i < ProtoCount; i++)
	    if (proto == s_protocol[i])
		return true;
	return false;
    }
    // Check if this module handles a given protocol
    static const char* defProtoName()
	{ return s_protocol[Jabber].c_str(); }
    // Protocols supported by this module
    static const String s_protocol[ProtoCount];
protected:
    // Handle command complete requests
    virtual bool commandComplete(Message& msg, const String& partLine,
	const String& partWord);
    // Additional driver status commands
    static String s_statusCmd[StatusCmdCount];
private:
    // Check and build caller and called for Component run mode
    // Caller: Set user if missing. Get default server identity for Yate Component
    // Try to get an available resource for the called party
    bool setComponentCall(JabberID& caller, JabberID& called, const char* cr,
	const char* cd, bool& available, String& error);

    bool m_init;
    bool m_singleTone;                   // Send single/batch DTMFs
    bool m_installIq;                    // Install the 'iq' service in jabber
                                         //  engine and xmpp. message handlers
    bool m_imToChanText;                 // Send received IM messages as chan.text if a channel is found
    JGStreamHost* m_ftProxy;             // Default file transfer proxy
    String m_statusCmd;                  //
};


/**
 * Local data
 */
static Configuration s_cfg;                       // The configuration file
static JGRtpMediaList s_knownCodecs(JGRtpMediaList::Audio);  // List of all known codecs
static JGRtpMediaList s_usedCodecs(JGRtpMediaList::Audio);   // List of used audio codecs
static String s_localAddress;                     // The local machine's address
static unsigned int s_pendingTimeout = 10000;     // Outgoing call pending timeout
static String s_anonymousCaller = "unk_caller";   // Caller name when missing
static bool s_attachPresToCmd = false;            // Attach presence to command (when used)
static bool s_userRoster = false;                 // Send client roster with user.roster or resource.notify
static bool s_useCrypto = false;
static bool s_cryptoMandatory = false;
static YJBEngine* s_jabber = 0;
static YJGEngine* s_jingle = 0;
static YJBMessage* s_message = 0;
static YJBPresence* s_presence = 0;
static YJBClientPresence* s_clientPresence = 0;
static YJBStreamService* s_stream = 0;
static YJBIqService* s_iqService = 0;
const String YJGDriver::s_protocol[YJGDriver::ProtoCount] = {"jabber", "xmpp", "jingle"};
static YJGDriver plugin;                          // The driver

// Error mapping
static TokenDict s_errMap[] = {
    {"normal",          JGSession::ReasonOk},
    {"normal-clearing", JGSession::ReasonOk},
    {"hangup",          JGSession::ReasonOk},
    {"busy",            JGSession::ReasonBusy},
    {"rejected",        JGSession::ReasonDecline},
    {"nomedia",         JGSession::ReasonMedia},
    {"transferred",     JGSession::ReasonTransfer},
    {"failure",         JGSession::ReasonUnknown},
    {"noroute",         JGSession::ReasonDecline},
    {"noconn",          JGSession::ReasonUnknown},
    {"noauth",          JGSession::ReasonUnknown},
    {"nocall",          JGSession::ReasonUnknown},
    {"noanswer",        JGSession::ReasonUnknown},
    {"forbidden",       JGSession::ReasonUnknown},
    {"offline",         JGSession::ReasonUnknown},
    {"congestion",      JGSession::ReasonUnknown},
    {"looping",         JGSession::ReasonUnknown},
    {"shutdown",        JGSession::ReasonUnknown},
    {"notransport",     JGSession::ReasonTransport},
    // Remote termination only
    {"failure",         JGSession::ReasonConn},
    {"failure",         JGSession::ReasonTransport},
    {"failure",         JGSession::ReasonNoError},
    {"failure",         JGSession::ReasonNoApp},
    {"failure",         JGSession::ReasonAltSess},
    {0,0}
};

// Get the number of private threads of a given type
// Force to 1 for client run mode
// Force at least 1 otherwise
inline int threadCount(const NamedList& params, const char* param)
{
    if (s_jabber->protocol() == JBEngine::Client)
	return 1;
    int t = params.getIntValue(param);
    return t < 1 ? 1 : t;
}

inline void addValidParam(Message& m, const char* param, const char* value)
{
    if (!null(value))
	m.addParam(param,value);
}

// Add formats to a list of jingle payloads
static void setMedia(JGRtpMediaList& dest, const String& formats,
    const JGRtpMediaList& src)
{
    ObjList* f = formats.split(',');
    for (ObjList* o = f->skipNull(); o; o = o->skipNext()) {
	String* format = static_cast<String*>(o->get());
	JGRtpMedia* a = src.findSynonym(*format);
	if (a)
	    dest.append(new JGRtpMedia(*a));
    }
    TelEngine::destruct(f);
}


/**
 * YJBEngine
 */
void YJBEngine::initialize()
{
    debugChain(&plugin);
    NamedList dummy("");
    NamedList* sect = s_cfg.getSection("general");
    if (!sect)
	sect = &dummy;
    // Force private processing. Force 1 thread for client run mode
    sect->setParam("private_process_threads",String(threadCount(*sect,"private_process_threads")));
    sect->setParam("private_receive_threads",String(threadCount(*sect,"private_receive_threads")));
    JBEngine::initialize(*sect);

    String defComponent;
    // Set server list if not client
    unsigned int count = (protocol() != Client) ? s_cfg.sections() : 0;
    for (unsigned int i = 0; i < count; i++) {
	const NamedList* comp = s_cfg.getSection(i);
	String name = comp ? comp->c_str() : "";
	if (!name || name == "general" || name == "codecs")
	    continue;

	const char* address = comp->getValue("address");
	String tmp = comp->getValue("port");
	int port = tmp.toInteger();
	if (!(address && port)) {
	    Debug(this,DebugNote,
		"Invalid address=%s or port=%s in configuration for %s",
		address,tmp.c_str(),name.c_str());
	    continue;
	}
	const char* password = comp->getValue("password");
	// Check identity. Construct the full identity
	String identity = comp->getValue("identity");
	if (!identity)
	    identity = name;
	String fullId;
	bool keepRoster = false;
	if (identity == name) {
	    String subdomain = comp->getValue("subdomain",s_cfg.getValue(
		"general","default_resource",defaultResource()));
	    identity = subdomain;
	    identity << '.' << name;
	    fullId = name;
	}
	else {
	    keepRoster = true;
	    fullId << '.' << name;
	    if (identity.endsWith(fullId)) {
		if (identity.length() == fullId.length()) {
		    Debug(this,DebugNote,"Invalid identity=%s in configuration for %s",
			identity.c_str(),name.c_str());
		    continue;
		}
		fullId = identity;
	    }
	    else {
		fullId = identity;
		fullId << '.' << name;
	    }
	    identity = fullId;
	}
	if (!identity)
	    continue;
	int flags = XMPPUtils::decodeFlags(comp->getValue("options"),
	    XMPPServerInfo::s_flagName);
	if (!comp->getBoolValue("auto_restart",true))
	    flags |= XMPPServerInfo::NoAutoRestart;
	if (keepRoster)
	    flags |= XMPPServerInfo::KeepRoster;
	XMPPServerInfo* server = new XMPPServerInfo(name,address,port,
	    password,identity,fullId,flags);
	bool startup = comp->getBoolValue("startup");
#ifdef DEBUG
	String f;
	XMPPUtils::buildFlags(f,flags,XMPPServerInfo::s_flagName);
	DDebug(this,DebugAll,
	    "Add server '%s' %s:%d ident=%s full-ident=%s options=%s",
	    name.c_str(),address,port,identity.c_str(),fullId.c_str(),
	    f.c_str());
#endif
	appendServer(server,startup);
	if (!defComponent || comp->getBoolValue("default"))
	    defComponent = name;
    }
    // Set default component server
    if (protocol() == Component)
	setComponentServer(defComponent);
}

// Setup the transport layer security for a stream
bool YJBEngine::encryptStream(JBStream* stream)
{
    if (!stream)
	return false;
    Message msg("socket.ssl");
    msg.userData(stream);
    msg.addParam("server",String::boolText(!stream->outgoing()));
    return Engine::dispatch(msg);
}


/**
 * YJGEngine
 */
void YJGEngine::initialize()
{
    debugChain(&plugin);
    NamedList dummy("");
    NamedList* sect = s_cfg.getSection("general");
    if (!sect)
	sect = &dummy;
    // Force private processing
    sect->setParam("private_process_threads",String(threadCount(*sect,"private_process_threads")));
    JGEngine::initialize(*sect);
    // Init data
    m_requestSubscribe = sect->getBoolValue("request_subscribe",true);
}

// Process jingle events
void YJGEngine::processEvent(JGEvent* event)
{
    if (!event)
	return;
    JGSession* session = event->session();
    // This should never happen !!!
    if (!session) {
	Debug(this,DebugWarn,"Received event without session");
	delete event;
	return;
    }
    YJGConnection* conn = static_cast<YJGConnection*>(session->userData());
    if (conn) {
	if (!conn->handleEvent(event) || event->final())
	    conn->disconnect(event->reason());
    }
    else {
	if (event->type() == JGEvent::Jingle &&
	    event->action() == JGSession::ActInitiate) {
	    if (event->session()->ref()) {
		conn = new YJGConnection(event);
		// Constructor failed ?
		if (conn->state() == YJGConnection::Pending)
		    TelEngine::destruct(conn);
		else if (!conn->route())
		    event->session()->userData(0);
	    }
	    else {
		Debug(this,DebugWarn,"Session ref failed for new connection");
		event->session()->hangup(JGSession::ReasonUnknown,"Internal error");
	    }
        }
	else {
	    DDebug(this,DebugAll,"Invalid (non initiate) event for new session");
	    event->confirmElement(XMPPError::SRequest,"Unknown session");
	}
    }
    delete event;
}


/**
 * YJBMessage
 */
void YJBMessage::initialize()
{
    debugChain(&plugin);
    NamedList dummy("");
    NamedList* sect = s_cfg.getSection("general");
    if (!sect)
	sect = &dummy;
    // Force sync (not enqueued) message processing
    sect->setParam("sync_process","true");
    JBMessage::initialize(*sect);
}

// Process a Jabber message
void YJBMessage::processMessage(JBEvent* event)
{
    if (!event)
	return;
    plugin.processImMsg(*event);
}


/**
 * YJBStreamService
 */
void YJBStreamService::initialize()
{
    debugChain(&plugin);
}

// Process stream termination events
bool YJBStreamService::accept(JBEvent* event, bool& processed, bool& insert)
{
    JBStream* stream = event ? event->stream() : 0;
    if (!stream)
	return false;
    if (event->type() != JBEvent::Terminated &&
	event->type() != JBEvent::Running &&
	event->type() != JBEvent::Destroy)
	return false;

    Message* m = new Message("user.notify");
    m->addParam("account",stream->name());
    m->addParam("protocol",plugin.defProtoName());
    m->addParam("username",stream->local().node());
    m->addParam("server",stream->local().domain());
    m->addParam("jid",stream->local());
    m->addParam("registered",String::boolText(event->type() == JBEvent::Running));
    if (event->type() != JBEvent::Running && event->text())
	m->addParam("reason",event->text());
    bool restart = (stream->state() != JBStream::Destroy && stream->flag(JBStream::AutoRestart));
    m->addParam("autorestart",String::boolText(restart));
    Engine::enqueue(m);
    return false;
}

/**
 * YJBClientPresence
 */
void YJBClientPresence::initialize()
{
    debugChain(&plugin);
}

// Process client presence and roster updates
bool YJBClientPresence::accept(JBEvent* event, bool& processed, bool& insert)
{
    if (!event)
	return false;

    processed = true;
    while (true) {
	if (event->type() != JBEvent::Presence &&
	    event->type() != JBEvent::IqClientRosterUpdate) {
	    Debug(this,DebugStub,"Can't accept unexpected event=%s [%p]",
		event->name(),this);
	    processed = false;
	    break;
	}

	// User roster update
	if (event->type() == JBEvent::IqClientRosterUpdate) {
	    if (!event->child())
		break;
	    // Send the whole roster in one message
	    if (s_userRoster) {
		Message* m = new Message("user.roster");
		m->addParam("module",plugin.name());
		m->addParam("protocol",plugin.defProtoName());
		if (event->stream() && event->stream()->name())
		    m->addParam("account",event->stream()->name());
		else if (event->to().node())
		    m->addParam("username",event->to().node());
		XMLElement* iq = event->releaseXML();
		XMLElement* query = iq->findFirstChild("query");
		if (query) {
		    XMLElement* item = 0;
		    int count = 0;
		    for (;;) {
			item = query->findNextChild(item,"item");
			if (!item)
			    break;
			const char* tmp = item->getAttribute("jid");
			if (!tmp)
			    continue;
			count++;
			String base("contact.");
			base << count;
			m->addParam(base,tmp);
			tmp = item->getAttribute("name");
			if (tmp)
			    m->addParam(base + ".name",tmp);
			tmp = item->getAttribute("subscription");
			if (tmp)
			    m->addParam(base + ".subscription",tmp);
			// Copy children
			XMLElement* child = item->findFirstChild();
			for (; child; child = item->findNextChild(child))
			    m->addParam(base + "." + child->name(),child->getText());
		    }
		    TelEngine::destruct(query);
		    m->addParam("contact.count",String(count));
		}
		m->addParam(new NamedPointer("xml",iq,"roster"));
		Engine::enqueue(m);
		break;
	    }
	    // Send the roster in individual resource.notify
	    XMLElement* item = event->child()->findFirstChild(XMLElement::Item);
	    for (; item; item = event->child()->findNextChild(item,XMLElement::Item)) {
		Message* m = YJBPresence::message(-1,0,event->to().bare(),
		    item->getAttribute("subscription"));
		if (event->stream() && event->stream()->name())
		    m->setParam("account",event->stream()->name());
		m->setParam("contact",item->getAttribute("jid"));
		addValidParam(*m,"contactname",item->getAttribute("name"));
		addValidParam(*m,"ask",item->getAttribute("ask"));
		// Copy children
		XMLElement* child = item->findFirstChild();
		for (; child; child = item->findNextChild(child))
		    addValidParam(*m,child->name(),child->getText());
		Engine::enqueue(m);
	    }
	    break;
	}

	// Presence
	const char* sub = 0;
	if (event->stream() && event->stream()->type() == JBEngine::Client) {
	    JBClientStream* stream = static_cast<JBClientStream*>(event->stream());
	    Lock lock(stream->roster());
	    XMPPUser* user = stream->getRemote(event->from());
	    if (user) {
		sub = XMPPDirVal::lookup((int)user->subscription());
		TelEngine::destruct(user);
	    }
	}

	Message* m = 0;
	JBPresence::Presence pres = JBPresence::presenceType(event->stanzaType());

	if (pres == JBPresence::None || pres == JBPresence::Unavailable) {
	    bool capAudio = false;
	    bool available = (pres == JBPresence::None);
	    JIDResource* res = 0;
	    if (event->element()) {
		res = new JIDResource(event->from().resource());
		if (res->fromXML(event->element())) {
		    capAudio = res->hasCap(JIDResource::CapAudio);
		    available = res->available();
		}
	    }
	    // Notify presence to module and enqueue message in engine
	    plugin.processPresence(event->to(),event->from(),available,capAudio);
	    m = YJBPresence::message(pres,event->from(),event->to(),sub);
	    if (res) {
		m->addParam("audio",String::boolText(capAudio));
		ObjList* o = res->infoXml()->skipNull();
		if (o || res->status()) {
		    String prefix = "jingle";
		    m->addParam("message-prefix",prefix);
		    prefix << ".";
		    unsigned int n = 1;
		    // Set status: avoid some meaningful values
		    if (res->status()) {
			if (res->status() != "subscribed" &&
			    res->status() != "unsubscribed" &&
			    res->status() != "offline")
			    m->setParam("status",res->status());
			else {
			    m->addParam(prefix + "1","status");
			    m->addParam(prefix + "1.",res->status());
			    n = 2;
			}
		    }
		    for (; o; o = o->skipNext(), n++) {
			XMLElement* e = static_cast<XMLElement*>(o->get());
			e->toList(*m,String(prefix + String(n)));
		    }
		}
		TelEngine::destruct(res);
	    }
	}
	else
	    switch (pres) {
		case JBPresence::Subscribe:
		case JBPresence::Unsubscribe:
		case JBPresence::Subscribed:
		case JBPresence::Unsubscribed:
		case JBPresence::Probe:
		    m = YJBPresence::message(pres,event->from().bare(),
			event->to().bare(),sub);
		    break;
		case JBPresence::Error:
		    if (event->text()) {
			m = YJBPresence::message(pres,event->from().bare(),
			    event->to().bare(),sub);
			m->setParam("error",event->text());
		    }
		    break;
		default:
		    Debug(this,DebugStub,"accept() not implemented for presence=%s [%p]",
			event->stanzaType().c_str(),this);
		    processed = false;
	    }

	if (m) {
	    if (event->stream() && event->stream()->name())
		m->setParam("account",event->stream()->name());
	    Engine::enqueue(m);
	}
	break;
    }

    return processed;
}


/**
 * YJBPresence
 */
void YJBPresence::initialize()
{
    debugChain(&plugin);
    NamedList dummy("");
    NamedList* sect = s_cfg.getSection("general");
    if (!sect)
	sect = &dummy;
    // Force private processing
    sect->setParam("private_process_threads",String(threadCount(*sect,"private_process_threads")));
    JBPresence::initialize(*sect);
}

bool YJBPresence::notifyProbe(JBEvent* event)
{
    XDebug(this,DebugAll,"notifyProbe local=%s remote=%s [%p]",
	event->to().c_str(),event->from().c_str(),this);
    Engine::enqueue(message(JBPresence::Probe,event->from().bare(),event->to().bare(),0));
    return true;
}

bool YJBPresence::notifySubscribe(JBEvent* event, Presence presence)
{
    XDebug(this,DebugAll,"notifySubscribe(%s) local=%s remote=%s [%p]",
	presenceText(presence),event->to().c_str(),event->from().c_str(),this);
    // Respond if auto subscribe
    if (!ignoreNonRoster() && event->stream() && autoSubscribe().from() &&
	(presence == JBPresence::Subscribe || presence == JBPresence::Unsubscribe)) {
	if (presence == JBPresence::Subscribe)
	    presence = JBPresence::Subscribed;
	else
	    presence = JBPresence::Unsubscribed;
	XMLElement* xml = createPresence(event->to().bare(),event->from().bare(),presence);
	event->stream()->sendStanza(xml);
	return true;
    }
    // Enqueue message
    Engine::enqueue(message(presence,event->from().bare(),event->to().bare(),0));
    return true;
}

void YJBPresence::notifySubscribe(XMPPUser* user, Presence presence)
{
    if (!user)
	return;
    XDebug(this,DebugAll,"notifySubscribe(%s) local=%s remote=%s [%p]",
	presenceText(presence),user->local()->jid().bare().c_str(),
	user->jid().bare().c_str(),this);
    Engine::enqueue(message(presence,user->jid().bare(),user->local()->jid().bare(),0));
}

bool YJBPresence::notifyPresence(JBEvent* event, bool available)
{
    // Check audio properties and availability for received resource
    bool capAudio = false;
    if (event && event->element()) {
	JIDResource* res = new JIDResource(event->from().resource());
	if (res->fromXML(event->element())) {
	    capAudio = res->hasCap(JIDResource::CapAudio);
	    available = res->available();
	}
	TelEngine::destruct(res);
    }
    Debug(this,DebugAll,"notifyPresence local=%s remote=%s available=%s [%p]",
	event->to().c_str(),event->from().c_str(),String::boolText(available),this);
    // Notify presence to module and enqueue message in engine
    plugin.processPresence(event->to(),event->from(),available,capAudio);
    Engine::enqueue(message(available ? JBPresence::None : JBPresence::Unavailable,
	event->from().bare(),event->to().bare(),0));
    return true;
}

// Notify plugin and enqueue message in engine
void YJBPresence::notifyPresence(XMPPUser* user, JIDResource* resource)
{
    if (!(user && resource))
	return;
    JabberID remote(user->jid().node(),user->jid().domain(),resource->name());
    Debug(this,DebugAll,"notifyPresence local=%s remote=%s available=%s [%p]",
	user->local()->jid().c_str(),remote.c_str(),
	String::boolText(resource->available()),this);
    plugin.processPresence(user->local()->jid(),remote,resource->available(),
	resource->hasCap(JIDResource::CapAudio));
    Engine::enqueue(message(resource->available() ? JBPresence::None : JBPresence::Unavailable,
	user->jid().bare(),user->local()->jid().bare(),
	String::boolText(user->subscription().to())));
}

void YJBPresence::notifyNewUser(XMPPUser* user)
{
    if (!user)
	return;
    DDebug(this,DebugAll,"notifyNewUser local=%s remote=%s. Adding default resource [%p]",
	user->local()->jid().bare().c_str(),user->jid().bare().c_str(),this);
    // Add local resource
    user->addLocalRes(new JIDResource(s_jabber->defaultResource(),JIDResource::Available,
	JIDResource::CapAudio));
}

Message* YJBPresence::message(int presence, const char* from, const char* to,
	const char* subscription)
{
    Message* m = 0;
    const char* status = 0;
    const char* operation = 0;
    switch (presence) {
	case JBPresence::None:
	    m = new Message("resource.notify");
	    status = "online";
	    break;
	case JBPresence::Unavailable:
	    m = new Message("resource.notify");
	    status = "offline";
	    break;
	case JBPresence::Subscribed:
	    m = new Message("resource.notify");
	    status = "subscribed";
	    break;
	case JBPresence::Unsubscribed:
	    m = new Message("resource.notify");
	    status = "unsubscribed";
	    break;
	case JBPresence::Probe:
	    m = new Message("resource.notify");
	    operation = "probe";
	    break;
	case JBPresence::Subscribe:
	    m = new Message("resource.subscribe");
	    operation = "subscribe";
	    break;
	case JBPresence::Unsubscribe:
	    m = new Message("resource.subscribe");
	    operation = "unsubscribe";
	    break;
	default:
	    m = new Message("resource.notify");
    }
    m->addParam("module",plugin.name());
    m->addParam("protocol",plugin.defProtoName());
    m->addParam("to",to);
    addValidParam(*m,"from",from);
    addValidParam(*m,"operation",operation);
    addValidParam(*m,"subscription",subscription);
    addValidParam(*m,"status",status);
    return m;
}


/**
 * YJBIqService
 */
void YJBIqService::initialize()
{
    debugChain(&plugin);
}

// Process events
bool YJBIqService::accept(JBEvent* event, bool& processed, bool& insert)
{
    if (!(event && event->element()))
	return false;

    processed = (event->element()->type() == XMLElement::Iq);
    if (!processed) {
	// Don't show the debug if it's a WriteFail event: this event may
	//  carry any failed stanza
	if (event->type() != JBEvent::WriteFail)
	    Debug(this,DebugStub,"Can't accept unexpected event=%s [%p]",
		event->name(),this);
	return false;
    }

    bool incoming = (event->type() != JBEvent::WriteFail);
    Message* m = new Message("xmpp.iq");
    m->addParam("module",plugin.name());
    if (event->stream())
	m->addParam("account",event->stream()->name());
    const JabberID* from = &(event->from());
    const JabberID* to = &(event->to());
    // Received stanza: get source/destination JID from stream if missing
    if (incoming) {
	if (to->null() && event->stream())
	    to = &(event->stream()->local());
	if (from->null() && event->stream())
	    from = &(event->stream()->remote());
    }
    addValidParam(*m,"from",*from);
    addValidParam(*m,"to",*to);
    m->addParam("type",event->stanzaType());
    addValidParam(*m,"id",event->id());
    addValidParam(*m,"username",from->node());
    if (!to->null())
	m->addParam("calleduri",BUILD_XMPP_URI(*to));
    if (!incoming)
	m->addParam("failure",String::boolText(true));
    XMLElement* xml = event->releaseXML();
    XMLElement* child = xml->findFirstChild();
    m->addParam(new NamedPointer("xml",xml,child?child->name():0));
    TelEngine::destruct(child);
    Engine::enqueue(m);
    return true;
}


/**
 * YJGConnection
 */
// Outgoing call
YJGConnection::YJGConnection(Message& msg, const char* caller, const char* called,
	bool available, const char* file)
    : Channel(&plugin,0,true),
    m_mutex(true,"YJGConnection"),
    m_state(Pending), m_session(0), m_local(caller),
    m_remote(called), m_audioContent(0),
    m_callerPrompt(msg.getValue("callerprompt")), m_sendRawRtpFirst(true),
    m_useCrypto(s_useCrypto), m_cryptoMandatory(s_cryptoMandatory),
    m_hangup(false), m_timeout(0), m_transferring(false), m_recvTransferStanza(0),
    m_dataFlags(0), m_ftStatus(FTNone), m_ftHostDirection(FTHostNone)
{
    m_subject = msg.getValue("subject");
    String uri = msg.getValue("diverteruri",msg.getValue("diverter"));
    // Skip protocol if present
    if (uri) {
	int pos = uri.find(':');
	m_transferFrom.set((pos >= 0) ? uri.substr(pos + 1) : uri);
    }
    // Get formats. Check if this is a file transfer session
    if (null(file)) {
	m_formats = msg.getValue("formats");
	if (!m_formats)
	    s_usedCodecs.createList(m_formats,true);
    }
    else {
	m_ftStatus = FTIdle;
	m_ftHostDirection = FTHostLocal;
	NamedString* oper = msg.getParam("operation");
	bool send = (oper && *oper == "send");
	m_ftContents.append(buildFileTransferContent(send,file,msg));
	// Add default proxy stream host if we have one
	JGStreamHost* sh = plugin.defFTProxy();
	if (sh)
	    m_streamHosts.append(sh);
    }
    Debug(this,DebugCall,"Outgoing%s. caller='%s' called='%s'%s%s [%p]",
	m_ftStatus != FTNone ? " file transfer" : "",caller,called,
	m_transferFrom ? ". Transferred from=": "",
	m_transferFrom.safe(),this);
    // Set timeout and maxcall
    int tout = msg.getIntValue("timeout",-1);
    if (tout > 0)
	timeout(Time::now() + tout*(u_int64_t)1000);
    else if (tout == 0)
	timeout(0);
    m_timeout = msg.getIntValue("maxcall",0) * (u_int64_t)1000;
    u_int64_t pendingTimeout = s_pendingTimeout * (u_int64_t)1000;
    u_int64_t timenow = Time::now();
    if (m_timeout && pendingTimeout >= m_timeout) {
	maxcall(timenow + m_timeout);
	m_timeout = 1;
    }
    else {
	maxcall(timenow + pendingTimeout);
	if (m_timeout) {
	    // Set a greater timeout for file transfer due to
	    // TCP connect
	    if (m_ftStatus == FTNone)
		m_timeout += timenow - pendingTimeout;
	    else
		m_timeout += timenow;
	}
    }
    XDebug(this,DebugInfo,"Time: " FMT64 ". Maxcall set to " FMT64 " us. [%p]",
	Time::now(),maxcall(),this);
    // Startup
    Message* m = message("chan.startup",msg);
    m->setParam("direction",status());
    m_targetid = msg.getValue("id");
    m->setParam("caller",msg.getValue("caller"));
    m->setParam("called",msg.getValue("called"));
    m->setParam("billid",msg.getValue("billid"));
    Engine::enqueue(m);
    // Make the call
    if (available)
	presenceChanged(true);
}

// Incoming call
YJGConnection::YJGConnection(JGEvent* event)
    : Channel(&plugin,0,false),
    m_mutex(true,"YJGConnection"),
    m_state(Active), m_session(event->session()),
    m_local(event->session()->local()), m_remote(event->session()->remote()),
    m_audioContent(0), m_sendRawRtpFirst(true),
    m_useCrypto(s_useCrypto), m_cryptoMandatory(s_cryptoMandatory),
    m_hangup(false), m_timeout(0), m_transferring(false), m_recvTransferStanza(0),
    m_dataFlags(0), m_ftStatus(FTNone), m_ftHostDirection(FTHostNone)
{
    if (event->jingle()) {
        // Check if this call is transferred
	XMLElement* trans = event->jingle()->findFirstChild(XMLElement::Transfer);
	if (trans) {
	    m_transferFrom.set(trans->getAttribute("from"));
	    TelEngine::destruct(trans);
	}
	// Get subject
	XMLElement* subject = event->jingle()->findFirstChild(XMLElement::Subject);
	if (subject) {
	    m_subject = subject->getText();
	    TelEngine::destruct(subject);
	}
    }
    Debug(this,DebugCall,"Incoming. caller='%s' called='%s'%s%s [%p]",
	m_remote.c_str(),m_local.c_str(),
	m_transferFrom ? ". Transferred from=" : "",
	m_transferFrom.safe(),this);
    // Set session
    m_session->userData(this);
    // Process incoming content(s)
    ObjList ok;
    ObjList remove;
    bool haveAudioSession = false;
    bool haveFTSession = false;
    if (processContentAdd(*event,ok,remove)) {
	for (ObjList* o = ok.skipNull(); o; o = o->skipNext()) {
	    JGSessionContent* c = static_cast<JGSessionContent*>(o->get());
	    switch (c->type()) {
		case JGSessionContent::RtpIceUdp:
		case JGSessionContent::RtpRawUdp:
		    haveAudioSession = haveAudioSession || c->isSession();
		    addContent(false,c);
		    break;
		case JGSessionContent::FileBSBOffer:
		case JGSessionContent::FileBSBRequest:
		    haveFTSession = haveFTSession || c->isSession();
		    m_ftContents.append(c);
		    break;
		default:
		    // processContentAdd() should return only known content types in ok list
		    // This a safeguard if we add new content type(s) and forget to process them
		    Debug(this,DebugStub,
			"Can't process incoming content '%s' of type %u [%p]",
			c->toString().c_str(),c->type(),this);
		    // Append this content to 'remove' list
		    // Let the list own it since we'll remove it from event's list
		    remove.append(c);
		    continue;
	    }
	    event->m_contents.remove(c,false);
	}
    }
    // XEP-0166 7.2.8 At least one content should have disposition=session
    // Change state to Pending on failure to terminate the session
    const char* error = 0;
    if (m_audioContents.skipNull()) {
	if (!haveAudioSession)
	    error = "No content with session disposition";
    }
    else if (m_ftContents.skipNull()) {
	m_ftStatus = FTIdle;
	m_ftHostDirection = FTHostRemote;
	m_session->buildSocksDstAddr(m_dstAddrDomain);
	if (haveFTSession) {
	    // TODO: Check data consistency: all file transfer contents should be
	    // identical (except for transport method, of course)
	}
	else
	    error = "No content with session disposition";
    }
    else
	error = "No acceptable session content(s) in initiate event";
    if (!error) {
	event->confirmElement();
	if (remove.skipNull())
	    m_session->sendContent(JGSession::ActContentRemove,remove);
	// We don't support mixed sessions for now
	// Remove file transfer contents if we have an audio session request
	if (m_audioContents.skipNull() && m_ftContents.skipNull()) {
	    Debug(this,DebugMild,"Denying file transfer in audio session [%p]",this);
	    m_session->sendContent(JGSession::ActContentRemove,m_ftContents);
	    m_ftContents.clear();
	}
    }
    else {
	m_state = Pending;
	setReason("failure");
	Debug(this,DebugNote,"%s [%p]",error,this);
	event->confirmElement(XMPPError::SBadRequest,error);
    }

    // Startup
    Message* m = message("chan.startup");
    m->setParam("direction",status());
    m->setParam("caller",m_remote.bare());
    m->setParam("called",m_local.node());
    Engine::enqueue(m);
}

// Release data
YJGConnection::~YJGConnection()
{
    TelEngine::destruct(m_recvTransferStanza);
    hangup(0);
    disconnected(true,m_reason);
    Debug(this,DebugCall,"Destroyed [%p]",this);
}

// Route an incoming call
bool YJGConnection::route()
{
    Message* m = message("call.preroute",false,true);
    m->addParam("username",m_remote.node());
    m->addParam("called",m_local.node());
    m->addParam("calleduri",BUILD_XMPP_URI(m_local));
    m->addParam("caller",m_remote.node());
    m->addParam("callername",m_remote.bare());
    m->addParam("calleruri",BUILD_XMPP_URI(m_remote));
    if (m_subject)
	m->addParam("subject",m_subject);
    m_mutex.lock();
    // TODO: add remote ip/port
    // Fill file transfer data
    JGSessionContent* c = firstFTContent();
    if (c) {
	m->addParam("format","data");
	if (c->type() == JGSessionContent::FileBSBOffer)
	    m->addParam("operation","receive");
	else if (c->type() == JGSessionContent::FileBSBRequest)
	    m->addParam("operation","send");
	m->addParam("file_name",c->m_fileTransfer.getValue("name"));
	int sz = c->m_fileTransfer.getIntValue("size",-1);
	if (sz >= 0)
	    m->addParam("file_size",String(sz));
	const char* md5 = c->m_fileTransfer.getValue("hash");
	if (!null(md5))
	    m->addParam("file_md5",md5);
	String* date = c->m_fileTransfer.getParam("date");
	if (!null(date)) {
	    unsigned int time = XMPPUtils::decodeDateTimeSec(*date);
	    if (time != (unsigned int)-1)
		m->addParam("file_time",String(time));
	}
    }
    m_mutex.unlock();
    return startRouter(m);
}

// Call accepted
// Init RTP. Accept session and transport. Send transport
void YJGConnection::callAccept(Message& msg)
{
    Debug(this,DebugCall,"callAccept [%p]",this);
    Channel::callAccept(msg);
}

void YJGConnection::callRejected(const char* error, const char* reason,
	const Message* msg)
{
    Debug(this,DebugCall,"callRejected. error=%s reason=%s [%p]",error,reason,this);
    hangup(error ? error : reason,reason);
    Channel::callRejected(error,reason,msg);
}

bool YJGConnection::callRouted(Message& msg)
{
    DDebug(this,DebugCall,"callRouted [%p]",this);
    return Channel::callRouted(msg);
}

void YJGConnection::disconnected(bool final, const char* reason)
{
    Debug(this,DebugCall,"disconnected. final=%u reason=%s [%p]",
	final,reason,this);
    TelEngine::destruct(m_audioContent);
    setReason(reason);
    Channel::disconnected(final,m_reason);
}

bool YJGConnection::msgProgress(Message& msg)
{
    DDebug(this,DebugInfo,"msgProgress [%p]",this);
    if (m_ftStatus == FTNone)
	setEarlyMediaOut(msg);
    return true;
}

bool YJGConnection::msgRinging(Message& msg)
{
    DDebug(this,DebugInfo,"msgRinging [%p]",this);
    if (m_ftStatus != FTNone)
	return true;
    m_mutex.lock();
    if (m_session && m_session->hasFeature(XMPPNamespace::JingleAppsRtpInfo)) {
	XMLElement* xml = XMPPUtils::createElement(XMLElement::Ringing,
	    XMPPNamespace::JingleAppsRtpInfo);
	m_session->sendInfo(xml);
    }
    m_mutex.unlock();
    setEarlyMediaOut(msg);
    return true;
}

bool YJGConnection::msgAnswered(Message& msg)
{
    Debug(this,DebugCall,"msgAnswered [%p]",this);
    if (m_ftStatus == FTNone) {
	clearEndpoint();
	m_mutex.lock();
	resetCurrentAudioContent(true,false,false);
	ObjList tmp;
	if (m_audioContent)
	    tmp.append(m_audioContent)->setDelete(false);
	else
	    Debug(this,DebugMild,"No session audio content available on answer time!!! [%p]",this);
	if (m_session)
	    m_session->accept(tmp);
	m_mutex.unlock();
	return Channel::msgAnswered(msg);
    }
    // File transfer connection
    Channel::msgAnswered(msg);
    if (m_ftStatus == FTEstablished) {
	if (setupSocksFileTransfer(true)) {
	    ObjList tmp;
	    JGSessionContent* c = firstFTContent();
	    if (c)
		tmp.append(c)->setDelete(false);
	    m_session->accept(tmp);
	}
	else
	    hangup("failure");
    }
    return true;
}

bool YJGConnection::msgUpdate(Message& msg)
{
    DDebug(this,DebugCall,"msgUpdate [%p]",this);
    Channel::msgUpdate(msg);

    if (m_ftStatus != FTNone)
	return false;

    NamedString* oper = msg.getParam("operation");
    bool req = (*oper == "request");
    bool notify = !req && (*oper == "notify");

    bool ok = false;

#define SET_ERROR_BREAK(error,reason) { \
    if (error) \
	msg.setParam("error",error); \
    if (reason) \
	msg.setParam("reason",reason); \
    break; \
}

    Lock lock(m_mutex);
    bool hold = msg.getBoolValue("hold");
    bool active = msg.getBoolValue("active");
    // Use a while to check session and break to method end
    while (m_session) {
        // Hold
	if (hold) {
	    // TODO: check if remote peer supports JingleRtpInfo
	    if (notify) {
		ok = true;
		break;
	    }
	    if (!req)
		break;
	    // Already put on hold
	    if (dataFlags(OnHold)) {
		if (dataFlags(OnHoldLocal))
		    SET_ERROR_BREAK("pending",0);
		SET_ERROR_BREAK("failure","Already on hold");
	    }
	    // Send XML. Copy any additional params
	    XMLElement* hold = XMPPUtils::createElement(XMLElement::Hold,
		XMPPNamespace::JingleAppsRtpInfo);
	    unsigned int n = msg.length();
	    for (unsigned int i = 0; i < n; i++) {
		NamedString* ns = msg.getParam(i);
		if (!(ns && ns->name().startsWith("hold.") && ns->name().at(5)))
		    continue;
		hold->setAttributeValid(ns->name().substr(5),*ns);
	    }
	    m_onHoldOutId << "hold" << Time::secNow();
	    if (!m_session->sendInfo(hold,&m_onHoldOutId)) {
		m_onHoldOutId = "";
		SET_ERROR_BREAK("noconn",0);
	    }
	    DDebug(this,DebugAll,"Sent hold request [%p]",this);
	    m_dataFlags |= OnHoldLocal;
	    removeCurrentAudioContent();
    	    ok = true;
	    break;
	}
        // Active
	if (active) {
	    // TODO: check if remote peer supports JingleRtpInfo
	    if (notify) {
		ok = true;
		break;
	    }
	    if (!req)
		break;
	    // Not on hold
	    if (!dataFlags(OnHold))
		SET_ERROR_BREAK("failure","Already active");
	    // Put on hold by remote
	    if (dataFlags(OnHoldRemote))
		SET_ERROR_BREAK("failure","Already on hold by the other party");
	    // Send XML. Copy additional attributes
	    XMLElement* active = XMPPUtils::createElement(XMLElement::Active,
		XMPPNamespace::JingleAppsRtpInfo);
	    unsigned int n = msg.length();
	    for (unsigned int i = 0; i < n; i++) {
		NamedString* ns = msg.getParam(i);
		if (!(ns && ns->name().startsWith("active.") && ns->name().at(5)))
		    continue;
		active->setAttributeValid(ns->name().substr(5),*ns);
	    }
	    m_activeOutId << "active" << Time::secNow();
	    if (!m_session->sendInfo(active,&m_activeOutId)) {
		m_activeOutId = "";
		SET_ERROR_BREAK("noconn",0);
	    }
	    DDebug(this,DebugAll,"Sent active request [%p]",this);
    	    ok = true;
	    break;
	}

	break;
    }

    if (!ok && req && (hold || active))
	Debug(this,DebugNote,"Failed to send '%s' request error='%s' reason='%s' [%p]",
	    hold ? "hold" : "active",msg.getValue("error"),msg.getValue("reason"),this);

#undef SET_ERROR_BREAK
    return ok;
}

// Send message to remote peer
bool YJGConnection::msgText(Message& msg, const char* text)
{
    DDebug(this,DebugCall,"msgText. '%s' [%p]",text,this);
    Lock lock(m_mutex);
    if (m_session) {
	m_session->sendMessage(text);
	return true;
    }
    return false;
}

// Hangup
bool YJGConnection::msgDrop(Message& msg, const char* reason)
{
    DDebug(this,DebugCall,"msgDrop('%s') [%p]",reason,this);
    setReason(reason ? reason : "dropped");
    if (!Channel::msgDrop(msg,m_reason))
	return false;
    hangup(m_reason);
    return true;
}

// Send tones to remote peer
bool YJGConnection::msgTone(Message& msg, const char* tone)
{
    DDebug(this,DebugCall,"msgTone. '%s' [%p]",tone,this);
    if (!(tone && *tone))
	return true;
    Lock lock(m_mutex);
    if (!m_session)
	return true;
    if (plugin.singleTone()) {
	char s[2] = {0,0};
	while (*tone) {
	    s[0] = *tone++;
	    m_session->sendDtmf(s);
	}
    }
    else
	m_session->sendDtmf(tone);
    return true;
}

// Send a transfer request
bool YJGConnection::msgTransfer(Message& msg)
{
    Lock lock(m_mutex);
    if (!canTransfer())
	return false;

    // Get transfer destination
    m_transferTo.set(msg.getValue("to"));

    // Check attended transfer request
    NamedString* chanId = msg.getParam("channelid");
    if (chanId) {
	bool ok = false;
	plugin.lock();
	YJGConnection* conn = static_cast<YJGConnection*>(plugin.Driver::find(*chanId));
	if (conn) {
	    ok = conn->getSid(m_transferSid);
	    if (!m_transferTo)
		m_transferTo = conn->remote();
	}
	plugin.unlock();

	if (!m_transferSid) {
	    Debug(this,DebugNote,"Attended transfer failed for conn=%s 'no %s' [%p]",
		chanId->c_str(),ok ? "session" : "connection",this);
	    return false;
	}

	// Don't transfer the same channel
	if (m_transferSid == m_session->sid()) {
	    Debug(this,DebugNote,
		"Attended transfer request for the same session! [%p]",this);
	    return false;
	}
    }
    else if (!m_transferTo) {
	DDebug(this,DebugNote,"Transfer request with empty target [%p]",this);
	return false;
    }
    // Try to get a resource for transfer target if incomplete
    if (!m_transferTo.isFull()) {
	const JBStream* stream = m_session ? m_session->stream() : 0;
	if (stream && stream->type() == JBEngine::Client)
	    plugin.getClientTargetResource((JBClientStream*)stream,m_transferTo);
    }

    // Send the transfer request
    XMLElement* trans = m_session->buildTransfer(m_transferTo,
	m_transferSid ? m_session->local() : String::empty(),m_transferSid);
    const char* subject = msg.getValue("subject");
    if (!null(subject))
	trans->addChild(new XMLElement(XMLElement::Subject,0,subject));
    m_transferring = m_session->sendInfo(trans,&m_transferStanzaId);
    Debug(this,m_transferring?DebugCall:DebugNote,"%s transfer to=%s sid=%s [%p]",
	m_transferring ? "Sent" : "Failed to send",m_transferTo.c_str(),
	m_transferSid.c_str(),this);
    if (!m_transferring)
	m_transferStanzaId = "";
    return m_transferring;
}

// Hangup the call. Send session terminate if not already done
void YJGConnection::hangup(const char* reason, const char* text)
{
    Lock lock(m_mutex);
    if (m_hangup)
	return;
    m_hangup = true;
    m_state = Terminated;
    m_ftStatus = FTTerminated;
    setReason(reason ? reason : (Engine::exiting() ? "shutdown" : "hangup"));
    if (!text && Engine::exiting())
	text = "Shutdown";
    if (m_transferring)
	transferTerminated(false,m_reason);
    Message* m = message("chan.hangup",true);
    m->setParam("status","hangup");
    m->setParam("reason",m_reason);
    Engine::enqueue(m);
    if (m_session) {
	m_session->userData(0);
	int res = lookup(m_reason,s_errMap,JGSession::ReasonUnknown);
	if (res == JGSession::ReasonUnknown && !text)
	    text = m_reason;
	m_session->hangup(res,text);
	TelEngine::destruct(m_session);
    }
    Debug(this,DebugCall,"Hangup. reason=%s [%p]",m_reason.c_str(),this);
}

// Handle Jingle events
// Return false to terminate
bool YJGConnection::handleEvent(JGEvent* event)
{
    if (!event)
	return true;
    Lock lock(m_mutex);
    if (m_hangup) {
	Debug(this,DebugInfo,"Ignoring event (%p,%u). Already hung up [%p]",
	    event,event->type(),this);
	return false;
    }

    if (event->type() == JGEvent::Terminated) {
	const char* reason = event->reason();
	Debug(this,DebugInfo,
	    "Session terminated with reason='%s' text='%s' [%p]",
	    reason,event->text().c_str(),this);
	// Check for Jingle reasons
	int res = JGSession::lookupReason(reason,JGSession::ReasonNone);
	if (res != JGSession::ReasonNone)
	    reason = lookup(res,s_errMap,reason);
	setReason(reason);
	return false;
    }

    bool response = false;
    switch (event->type()) {
	case JGEvent::Jingle:
	    break;
	case JGEvent::ResultOk:
	case JGEvent::ResultError:
	case JGEvent::ResultWriteFail:
	case JGEvent::ResultTimeout:
	    response = true;
	    break;
	default:
	    Debug(this,DebugStub,"Unhandled event (%p,%u) [%p]",
		event,event->type(),this);
	    return true;
    }

    // Process responses
    if (response) {
	XDebug(this,DebugAll,"Processing response event=%s id=%s [%p]",
	    event->name(),event->id().c_str(),this);

	bool rspOk = (event->type() == JGEvent::ResultOk);

	if (m_ftStanzaId && m_ftStanzaId == event->id()) {
	    m_ftStanzaId = "";
	    String usedHost;
	    bool ok = rspOk;
	    if (rspOk && event->element()) {
		XMLElement* query = event->element()->findFirstChild(XMLElement::Query);
		XMLElement* used = 0;
		if (query) {
		    used = query->findFirstChild(XMLElement::StreamHostUsed);
		    if (used)
			usedHost = used->getAttribute("jid");
		}
		TelEngine::destruct(query);
		TelEngine::destruct(used);
	    }
	    if (!ok) {
		// Result error: continue if we still can receive hosts
		ok = (event->type() == JGEvent::ResultError && isOutgoing());
		if (ok && m_ftStatus == FTWaitEstablish) 
		    m_ftStatus = FTIdle;
		clearEndpoint("data");
	    }
	    Debug(this,rspOk ? DebugAll : DebugMild,
		"Received result=%s to streamhost used=%s [%p]",
		event->name(),usedHost.c_str(),this);
	    return ok;
	}

	// Hold/active result
	bool hold = (m_onHoldOutId && m_onHoldOutId == event->id());
	if (hold || (m_activeOutId && m_activeOutId == event->id())) {
	    Debug(this,rspOk ? DebugAll : DebugMild,
		"Received result=%s to %s request [%p]",
		event->name(),hold ? "hold" : "active",this);

	    if (!hold)
		m_dataFlags &= ~OnHoldLocal;
	    Message* m = message("call.update");
	    m->userData(this);
	    m->addParam("operation","notify");
	    if (hold)
		m->addParam("hold",String::boolText(dataFlags(OnHold)));
	    else
		m->addParam("active",String::boolText(!dataFlags(OnHold)));
	    Engine::enqueue(m);
	    if (hold)
		m_onHoldOutId = "";
	    else {
		m_activeOutId = "";
		resetCurrentAudioContent(true,false);
	    }
	    return true;
	}

	// Check if this is a transfer request result
	if (m_transferring && m_transferStanzaId &&
	    m_transferStanzaId == event->id()) {
	    // Reset transfer
	    m_transferStanzaId = "";
	    m_transferring = false;
	    if (rspOk) {
		Debug(this,DebugInfo,"Transfer succeedded [%p]",this);
		// TODO: implement
	    }
	    else {
		Debug(this,DebugMild,"Transfer failed error=%s [%p]",
		    event->text().c_str(),this);
	    }
	    return true;
	}

	return true;
    }

    // Process jingle events
    switch (event->action()) {
	case JGSession::ActDtmf:
	    event->confirmElement();
	    Debug(this,DebugInfo,"Received dtmf(%s) '%s' [%p]",
		event->reason().c_str(),event->text().c_str(),this);
	    if (event->text()) {
		Message* m = message("chan.dtmf");
		m->addParam("text",event->text());
		m->addParam("detected","jingle");
		dtmfEnqueue(m);
	    }
	    break;
	case JGSession::ActTransportInfo:
	    if (m_ftStatus == FTNone)
		processActionTransportInfo(event);
	    else
		event->confirmElement(XMPPError::SRequest);
	    break;
	case JGSession::ActTransportAccept:
	    // TODO: handle it when (if) we'll send transport-replace
	    event->confirmElement(XMPPError::SRequest);
	    break;
	case JGSession::ActTransportReject:
	    // TODO: handle it when (if) we'll send transport-replace
	    event->confirmElement(XMPPError::SRequest);
	    break;
	case JGSession::ActTransportReplace:
	    // TODO: handle it
	    event->confirmElement();
	    Debug(this,DebugInfo,"Denying event(%s) [%p]",event->actionName(),this);
	    if (m_session)
		m_session->sendContent(JGSession::ActTransportReject,event->m_contents);
	    break;
	case JGSession::ActContentAccept:
	    if (m_ftStatus != FTNone) {
		event->confirmElement(XMPPError::SRequest);
		break;
	    }
	    event->confirmElement();
	    for (ObjList* o = event->m_contents.skipNull(); o; o = o->skipNext()) {
		JGSessionContent* c = static_cast<JGSessionContent*>(o->get());
		if (findContent(*c,m_audioContents))
		    Debug(this,DebugAll,"Event(%s) remote accepted content=%s [%p]",
			event->actionName(),c->toString().c_str(),this);
		else {
		    // We don't have such a content
		    Debug(this,DebugNote,
			"Event(%s) remote accepted missing content=%s [%p]",
			event->actionName(),c->toString().c_str(),this);
		}
	    }
	    if (!m_audioContent)
		resetCurrentAudioContent(isAnswered(),!isAnswered());
	    break;
	case JGSession::ActContentAdd:
	    if (m_ftStatus == FTNone)
		processActionContentAdd(event);
	    else
		event->confirmElement(XMPPError::SRequest);
	    break;
	case JGSession::ActContentModify:
	    // This event should modify the content 'senders' attribute
	    Debug(this,DebugInfo,"Denying event(%s) [%p]",event->actionName(),this);
	    event->confirmElement(XMPPError::SNotAllowed);
	    break;
	case JGSession::ActContentReject:
	    if (m_ftStatus != FTNone) {
		event->confirmElement(XMPPError::SRequest);
		break;
	    }
	    // XEP-0166 Notes - 16: terminate the session if there are no more contents
	    if (!removeContents(event))
		return true;
	    if (!m_audioContent)
		resetCurrentAudioContent(isAnswered(),!isAnswered());
	    break;
	case JGSession::ActContentRemove:
	    // XEP-0166 Notes - 16: terminate the session if there are no more contents
	    if (m_ftStatus == FTNone) {
		if (!removeContents(event))
		    return true;
		if (!m_audioContent)
		    resetCurrentAudioContent(isAnswered(),!isAnswered());
	    }
	    else {
		// Confirm and remove requested content(s)
		// Terminate if the first content is removed while negotiating
		event->confirmElement();
		for (ObjList* o = event->m_contents.skipNull(); o; o = o->skipNext()) {
		    JGSessionContent* c = static_cast<JGSessionContent*>(o->get());
		    JGSessionContent* cc = findContent(*c,m_ftContents);
		    if (cc) {
			if (cc == firstFTContent() && m_ftStatus != FTIdle)
			    return false;
			m_ftContents.remove(cc);
		    }
		}
		return 0 != m_ftContents.skipNull();
	    }
	    break;
	case JGSession::ActAccept:
	    if (isAnswered())
		break;
	    if (m_ftStatus != FTNone)
		return setupSocksFileTransfer(true);
	    // Update media
	    Debug(this,DebugCall,"Remote peer answered the call [%p]",this);
	    m_state = Active; 
	    removeCurrentAudioContent();
	    for (ObjList* o = event->m_contents.skipNull(); o; o = o->skipNext()) {
		JGSessionContent* recv = static_cast<JGSessionContent*>(o->get());
		JGSessionContent* c = findContent(*recv,m_audioContents);
		if (!c)
		    continue;
		// Update credentials for ICE-UDP
		c->m_rtpRemoteCandidates.m_password = recv->m_rtpRemoteCandidates.m_password;
		c->m_rtpRemoteCandidates.m_ufrag = recv->m_rtpRemoteCandidates.m_ufrag;
		// Update media
		if (!matchMedia(*c,*recv)) {
		    Debug(this,DebugInfo,"No common media for content=%s [%p]",
			c->toString().c_str(),this);
		    continue;
		}
		// Update transport(s)
		bool changed = updateCandidate(1,*c,*recv);
		changed = updateCandidate(2,*c,*recv) || changed;
		if (changed && !m_audioContent && recv->isSession())
		    resetCurrentAudioContent(true,false,true,c);
	    }
	    if (!m_audioContent)
		resetCurrentAudioContent(true,false,true);
	    maxcall(0);
	    status("answered");
	    Engine::enqueue(message("call.answered",false,true));
	    break;
	case JGSession::ActTransfer:
	    if (m_ftStatus == FTNone)
		processTransferRequest(event);
	    else
		event->confirmElement(XMPPError::SRequest);
	    break;
	case JGSession::ActRinging:
	    if (m_ftStatus == FTNone) {
		event->confirmElement();
		Engine::enqueue(message("call.ringing",false,true));
	    }
	    else
		event->confirmElement(XMPPError::SRequest);
	    break;
	case JGSession::ActHold:
	case JGSession::ActActive:
	case JGSession::ActMute:
	    if (m_ftStatus == FTNone)
		handleAudioInfoEvent(event);
	    else
		event->confirmElement(XMPPError::SRequest);
	    break;
	case JGSession::ActTrying:
	case JGSession::ActReceived:
	    if (m_ftStatus == FTNone) {
		event->confirmElement();
		Debug(this,DebugAll,"Received Jingle event (%p) with action=%s [%p]",
		    event,event->actionName(),this);
	    }
	    else
		event->confirmElement(XMPPError::SRequest);
	    break;
	case JGSession::ActStreamHost:
	    if (m_ftStatus != FTNone) {
		// Check if allowed
		if (m_ftHostDirection != FTHostRemote) {
		    event->confirmElement(XMPPError::SRequest);
		    break;
		}
		// Check if we already received it
		if (m_ftStatus != FTIdle) {
		    event->confirmElement(XMPPError::SRequest);
		    break;
		}
		event->setConfirmed();
		// Remember stanza id
		m_ftStanzaId = event->id();
		// Copy hosts from event
		ListIterator iter(event->m_streamHosts);
		for (GenObject* o = 0; 0 != (o = iter.get());) {
		    event->m_streamHosts.remove(o,false);
		    m_streamHosts.append(o);
		}
		if (!setupSocksFileTransfer(false)) {
		    if (m_ftStanzaId) {
			m_session->sendStreamHostUsed("",m_ftStanzaId);
			m_ftStanzaId = "";
		    }
		    if (!setupSocksFileTransfer(false))
			return false;
		}
	    }
	    else
		event->confirmElement(XMPPError::SRequest);
	    break;
	default:
	    Debug(this,DebugNote,
		"Received unexpected Jingle event (%p) with action=%s [%p]",
		event,event->actionName(),this);
    }
    return true;
}

// Process remote user's presence notifications
// Make the call if outgoing and in Pending (waiting for presence information) state
// Hangup if the remote user is unavailbale
// Return true to disconnect
bool YJGConnection::presenceChanged(bool available)
{
    Lock lock(m_mutex);
    if (m_state == Terminated)
	return false;
    maxcall(m_timeout);
    // Check if unavailable in any other states
    if (!available) {
	if (!m_hangup) {
	    DDebug(this,DebugCall,"Remote user is unavailable [%p]",this);
	    hangup("offline","Remote user is unavailable");
	}
	return true;
    }
    // Check if we are in pending state and remote peer is present
    if (!(isOutgoing() && m_state == Pending && available))
	return false;
    // Make the call
    Debug(this,DebugCall,"Calling. caller=%s called=%s [%p]",
	m_local.c_str(),m_remote.c_str(),this);
    m_state = Active;
    if (m_ftStatus == FTNone) {
	XMLElement* transfer = 0;
	if (m_transferFrom)
	    transfer = JGSession::buildTransfer(String::empty(),m_transferFrom);
	if (m_sendRawRtpFirst) {
	    addContent(true,buildAudioContent(JGRtpCandidates::RtpRawUdp));
	    addContent(true,buildAudioContent(JGRtpCandidates::RtpIceUdp));
	}
	else {
	    addContent(true,buildAudioContent(JGRtpCandidates::RtpIceUdp));
	    addContent(true,buildAudioContent(JGRtpCandidates::RtpRawUdp));
	}
	m_session = s_jingle->call(m_local,m_remote,m_audioContents,transfer,
	    m_callerPrompt,m_subject);
    }
    else
	m_session = s_jingle->call(m_local,m_remote,m_ftContents,0,
	    m_callerPrompt,m_subject);
    if (!m_session) {
	hangup("noconn");
	return true;
    }
    m_session->userData(this);
    if (m_ftStatus != FTNone) {
	m_session->buildSocksDstAddr(m_dstAddrDomain);
	if (!setupSocksFileTransfer(false)) {
	    if (m_ftStatus == FTTerminated) {
		hangup("noconn");
		return true;
	    }
	    // Send empty host
	    m_streamHosts.clear();
	    m_session->sendStreamHosts(m_streamHosts,&m_ftStanzaId);
	}
    }
    // Notify now ringing if the remote party doesn't support it
    if (m_ftStatus == FTNone && !m_session->hasFeature(XMPPNamespace::JingleAppsRtpInfo))
	Engine::enqueue(message("call.ringing",false,true));
    return false;
}

// Process a transfer request
bool YJGConnection::processTransferRequest(JGEvent* event)
{
    Lock lock(m_mutex);
    // Check if we can accept a transfer and if it is a valid request
    XMLElement* trans = 0;
    const char* reason = 0;
    XMPPError::Type error = XMPPError::SBadRequest;
    while (true) {
	if (!canTransfer()) {
	    error = XMPPError::SRequest;
	    reason = "Unacceptable in current state";
	    break;
	}
	trans = event->jingle() ? event->jingle()->findFirstChild(XMLElement::Transfer) : 0;
	if (!trans) {
	    reason = "Transfer element is misssing";
	    break;
	}
	m_transferTo.set(trans->getAttribute("to"));
	// Check transfer target
	if (!m_transferTo) {
	    reason = "Transfer target is misssing or incomplete";
	    break;
	}
	// Check sid: don't accept the replacement of the same session
	m_transferSid = trans->getAttribute("sid");
	if (m_transferSid && isSid(m_transferSid)) {
	    reason = "Can't replace the same session";
	    break;
	}
	m_transferFrom.set(trans->getAttribute("from"));
	break;
    }
    String subject;
    if (!reason && trans) {
	XMLElement* s = trans->findFirstChild(XMLElement::Subject);
	if (s) {
	    subject = s->getText();
	    TelEngine::destruct(s);
	}
    }
    TelEngine::destruct(trans);

    if (!reason) {
	TelEngine::destruct(m_recvTransferStanza);
	m_recvTransferStanza = event->releaseXML();
	event->setConfirmed();
	m_transferring = true;
	Debug(this,DebugCall,"Starting transfer to=%s from=%s sid=%s [%p]",
	    m_transferTo.c_str(),m_transferFrom.c_str(),m_transferSid.c_str(),this);
	bool ok = ((new YJGTransfer(this,subject))->startup());
	if (!ok)
	    transferTerminated(false,"Internal server error");
	return ok;
    }

    // Not acceptable
    Debug(this,DebugNote,
	"Refusing transfer request reason='%s' (transferring=%u answered=%u) [%p]",
	reason,m_transferring,isAnswered(),this);
    event->confirmElement(error,reason);
    return false;
}

// Transfer terminated notification from transfer thread
void YJGConnection::transferTerminated(bool ok, const char* reason)
{
    Lock lock(m_mutex);
    if (m_transferring && m_recvTransferStanza) {
	if (ok)
	    Debug(this,DebugCall,"Transfer succeedded [%p]",this);
	else
	    Debug(this,DebugNote,"Transfer failed error='%s' [%p]",reason,this);
    }
    if (m_session && m_recvTransferStanza) {
	XMPPError::Type err = ok ? XMPPError::NoError : XMPPError::SUndefinedCondition;
	m_session->confirm(m_recvTransferStanza,err,reason,XMPPError::TypeCancel);
	m_recvTransferStanza = 0;
    }
    // Reset transfer data
    TelEngine::destruct(m_recvTransferStanza);
    m_transferring = false;
    m_transferStanzaId = "";
    m_transferTo = "";
    m_transferFrom = "";
    m_transferSid = "";
}

// Get the remote party address (actually this is the address of the 
//  local party's server)
void YJGConnection::getRemoteAddr(String& dest)
{
    if (m_session && m_session->stream()) {
	dest = m_session->stream()->addr().host();
	return;
    }
    if (!s_jabber)
	return;
    JBStream* stream = 0;
    if (s_jabber->protocol() == JBEngine::Component)
	stream = s_jabber->getStream();
    else
	stream = s_jabber->getStream(&m_local,false);
    if (stream)
	dest = stream->addr().host();
    TelEngine::destruct(stream);
}

// Process an ActContentAdd event
void YJGConnection::processActionContentAdd(JGEvent* event)
{
    if (!event)
	return;

    ObjList ok;
    ObjList remove;
    if (!processContentAdd(*event,ok,remove)) {
	event->confirmElement(XMPPError::SConflict,"Duplicate content(s)");
	return;
    }

    ObjList* o = 0;
    event->confirmElement();
    if (m_session && remove.skipNull())
	m_session->sendContent(JGSession::ActContentRemove,remove);
    if (!ok.skipNull())
	return;
    for (o = ok.skipNull(); o; o = o->skipNext()) {
	JGSessionContent* c = static_cast<JGSessionContent*>(o->get());
	event->m_contents.remove(c,false);
	addContent(false,c);
    }

    if (!(m_audioContent || dataFlags(OnHold)))
	resetCurrentAudioContent(isAnswered(),!isAnswered());
    enqueueCallProgress();
}

// Process an ActTransportInfo event
void YJGConnection::processActionTransportInfo(JGEvent* event)
{
    if (!event)
	return;

    event->confirmElement();
    bool startAudioContent = false;
    JGSessionContent* newContent = 0;
    for (ObjList* o = event->m_contents.skipNull(); o; o = o->skipNext()) {
	JGSessionContent* c = static_cast<JGSessionContent*>(o->get());
	JGSessionContent* cc = findContent(*c,m_audioContents);
	if (!cc) {
	    Debug(this,DebugNote,"Event('%s') content '%s' not found [%p]",
		event->actionName(),c->toString().c_str(),this);
	    continue;
	}
	// Update credentials for ICE-UDP
	cc->m_rtpRemoteCandidates.m_password = c->m_rtpRemoteCandidates.m_password;
	cc->m_rtpRemoteCandidates.m_ufrag = c->m_rtpRemoteCandidates.m_ufrag;
        // Update transport(s)
	bool changed = updateCandidate(1,*cc,*c);
	changed = updateCandidate(2,*cc,*c) || changed;
	if (!changed)
	    continue;
	// Restart current content if the transport belongs to it or
	// replace or if the transport belongs to another one
	if (m_audioContent == cc) {
	    startAudioContent = true;
	    newContent = 0;
	}
	else
	    newContent = cc;
    }

    if (newContent) {
	if (!dataFlags(OnHold))
	    resetCurrentAudioContent(isAnswered(),!isAnswered(),true,newContent);
    }
    else if ((startAudioContent && !startRtp()) || !(m_audioContent || dataFlags(OnHold)))
	resetCurrentAudioContent(isAnswered(),!isAnswered());
    enqueueCallProgress();
}

// Update a received candidate. Return true if changed
bool YJGConnection::updateCandidate(unsigned int component, JGSessionContent& local,
    JGSessionContent& recv)
{
    JGRtpCandidate* rtpRecv = recv.m_rtpRemoteCandidates.findByComponent(component);
    if (!rtpRecv)
	return false;
    JGRtpCandidate* rtp = local.m_rtpRemoteCandidates.findByComponent(component);
    if (!rtp) {
	DDebug(this,DebugAll,"Adding remote transport '%s' in content '%s' [%p]",
	    rtpRecv->toString().c_str(),local.toString().c_str(),this);
	recv.m_rtpRemoteCandidates.remove(rtpRecv,false);
	local.m_rtpRemoteCandidates.append(rtpRecv);
	return true;
    }
    // Another candidate: replace
    // Same candidate with greater generation: replace
    if (rtp->toString() != rtpRecv->toString() ||
	rtp->m_generation.toInteger() < rtpRecv->m_generation.toInteger()) {
	DDebug(this,DebugAll,
	    "Replacing remote transport '%s' with '%s' in content '%s' [%p]",
	    rtp->toString().c_str(),rtpRecv->toString().c_str(),local.toString().c_str(),this);
	local.m_rtpRemoteCandidates.remove(rtp);
	recv.m_rtpRemoteCandidates.remove(rtpRecv,false);
	local.m_rtpRemoteCandidates.append(rtpRecv);
	return true;
    }
    return false;
}

// Add a new content to the list
void YJGConnection::addContent(bool local, JGSessionContent* c)
{
    Lock lock(m_mutex);
    m_audioContents.append(c);
    if (local)
	c->m_rtpRemoteCandidates.m_type = c->m_rtpLocalCandidates.m_type;
    else
	c->m_rtpLocalCandidates.m_type = c->m_rtpRemoteCandidates.m_type;
    if (c->m_rtpLocalCandidates.m_type == JGRtpCandidates::RtpIceUdp)
	c->m_rtpLocalCandidates.generateIceAuth();
    // Fill synonym for received media
    if (!local) {
	for (ObjList* o = c->m_rtpMedia.skipNull(); o; o = o->skipNext()) {
	    JGRtpMedia* m = static_cast<JGRtpMedia*>(o->get());
	    JGRtpMedia* tmp = s_knownCodecs.findMedia(m->toString());
	    if (tmp)
		m->m_synonym = tmp->m_synonym;
	}
    }
    Debug(this,DebugAll,"Added content='%s' type=%s initiator=%s [%p]",
	c->toString().c_str(),c->m_rtpLocalCandidates.typeName(),
	String::boolText(c->creator() == JGSessionContent::CreatorInitiator),this);
}

// Remove a content from list
void YJGConnection::removeContent(JGSessionContent* c)
{
    if (!c)
	return;
    Debug(this,DebugAll,"Removing content='%s' type=%s initiator=%s [%p]",
	c->toString().c_str(),c->m_rtpLocalCandidates.typeName(),
	String::boolText(c->creator() == JGSessionContent::CreatorInitiator),this);
    m_audioContents.remove(c);
}

// Reset the current audio content
// If the content is not re-usable (SRTP with local address),
//  add a new identical content and remove the old old one from the session
void YJGConnection::removeCurrentAudioContent(bool removeReq)
{
    if (!dataFlags(OnHold))
	clearEndpoint();
    if (!m_audioContent)
	return;

    Debug(this,DebugAll,"Resetting current audio content (%p,'%s') [%p]",
	m_audioContent,m_audioContent->toString().c_str(),this);

    // Remove from list if not re-usable
    bool check = (m_audioContent->isSession() == isAnswered());
    bool removeFromList = removeReq;
    if (check && (0 != m_audioContent->m_rtpMedia.m_cryptoLocal.skipNull())) {
	JGRtpCandidate* rtpLocal = m_audioContent->m_rtpLocalCandidates.findByComponent(1);
	if (rtpLocal && rtpLocal->m_address) {
	    removeFromList = true;
	    // Build a new content
	    JGSessionContent* c = buildAudioContent(m_audioContent->m_rtpLocalCandidates.m_type,
		m_audioContent->senders(),false,false);
	    if (m_audioContent->isEarlyMedia())
		c->setEarlyMedia();
	    // Copy media
	    c->m_rtpMedia.m_media = m_audioContent->m_rtpMedia.m_media;
	    c->m_rtpMedia.m_cryptoMandatory = m_audioContent->m_rtpMedia.m_cryptoMandatory;
	    for (ObjList* o = m_audioContent->m_rtpMedia.skipNull(); o; o = o->skipNext()) {
		JGRtpMedia* m = static_cast<JGRtpMedia*>(o->get());
		c->m_rtpMedia.append(new JGRtpMedia(*m));
	    }
	    // Append
	    addContent(true,c);
	    if (m_session)
		m_session->sendContent(JGSession::ActContentAdd,c);
	}
    }

    if (removeFromList) {
	if (!removeReq && m_session)
	    m_session->sendContent(JGSession::ActContentRemove,m_audioContent);
	removeContent(m_audioContent);
    }
    TelEngine::destruct(m_audioContent);
}

// This method is used to set the current audio content
// Reset the current content
// Find the first available content and try to use it
// Send a transport info for the new current content
// Return false on error
bool YJGConnection::resetCurrentAudioContent(bool session, bool earlyMedia,
    bool sendTransInfo, JGSessionContent* newContent)
{
    // Reset the current audio content
    removeCurrentAudioContent();

    // Set nothing if on hold
    if (dataFlags(OnHold))
	return false;

    if (!newContent) {
	// Pick up a new content. Try to find a content with remote candidates
	for (ObjList* o = m_audioContents.skipNull(); o; o = o->skipNext()) {
	    newContent = static_cast<JGSessionContent*>(o->get());
	    bool ok = newContent->isValidAudio() &&
		((session && newContent->isSession()) ||
		(earlyMedia && newContent->isEarlyMedia()));
	    if (ok && newContent->m_rtpRemoteCandidates.findByComponent(1))
		break;
	    newContent = 0;
	}
	// No content: choose the first suitable one
	if (!newContent) {
	    for (ObjList* o = m_audioContents.skipNull(); o; o = o->skipNext()) {
		newContent = static_cast<JGSessionContent*>(o->get());
		if (newContent->isValidAudio() &&
		    ((session && newContent->isSession()) ||
		    (earlyMedia && newContent->isEarlyMedia())))
		    break;
		newContent = 0;
	    }
	}
    }
    else if (!newContent->isValidAudio())
	return false;
   
    if (newContent && newContent->ref()) {
	m_audioContent = newContent;
	Debug(this,DebugAll,"Using audio content '%s' [%p]",
	    m_audioContent->toString().c_str(),this);
	JGRtpCandidate* rtp = m_audioContent->m_rtpLocalCandidates.findByComponent(1);
	if (!(rtp && rtp->m_address))
	    initLocalCandidates(*m_audioContent,sendTransInfo);
	return startRtp();
    }

    return false;
}

// Start RTP for the given content
// For raw udp transports, sends a 'trying' session info
bool YJGConnection::startRtp()
{
    if (!m_audioContent) {
	DDebug(this,DebugInfo,"Failed to start RTP: no audio content [%p]",this);
	return false;
    }

    JGRtpCandidate* rtpLocal = m_audioContent->m_rtpLocalCandidates.findByComponent(1);
    JGRtpCandidate* rtpRemote = m_audioContent->m_rtpRemoteCandidates.findByComponent(1);
    if (!(rtpLocal && rtpRemote)) {
	Debug(this,DebugNote,
	    "Failed to start RTP for content='%s' candidates local=%s remote=%s [%p]",
	    m_audioContent->toString().c_str(),String::boolText(0 != rtpLocal),
	    String::boolText(0 != rtpRemote),this);
	return false;
    }

    Message m("chan.rtp");
    m.userData(this);
    complete(m);
    m.addParam("direction",rtpDir(*m_audioContent));
    m.addParam("media","audio");
    m.addParam("getsession","true");
    ObjList* obj = m_audioContent->m_rtpMedia.skipNull();
    if (obj)
	m.addParam("format",(static_cast<JGRtpMedia*>(obj->get()))->m_synonym);
    m.addParam("localip",rtpLocal->m_address);
    m.addParam("localport",rtpLocal->m_port);
    m.addParam("remoteip",rtpRemote->m_address);
    m.addParam("remoteport",rtpRemote->m_port);
    //m.addParam("autoaddr","false");
    bool rtcp = (0 != m_audioContent->m_rtpLocalCandidates.findByComponent(2));
    m.addParam("rtcp",String::boolText(rtcp));

    String oldPort = rtpLocal->m_port;

    if (!Engine::dispatch(m)) {
	Debug(this,DebugNote,"Failed to start RTP for content='%s' [%p]",
	    m_audioContent->toString().c_str(),this);
	return false;
    }

    rtpLocal->m_port = m.getValue("localport");

    Debug(this,DebugAll,
	"RTP started for content='%s' local='%s:%s' remote='%s:%s' [%p]",
    	m_audioContent->toString().c_str(),
	rtpLocal->m_address.c_str(),rtpLocal->m_port.c_str(),
	rtpRemote->m_address.c_str(),rtpRemote->m_port.c_str(),this);

    if (oldPort != rtpLocal->m_port && m_session) {
	rtpLocal->m_generation = rtpLocal->m_generation.toInteger(0) + 1;
	m_session->sendContent(JGSession::ActTransportInfo,m_audioContent);
    }

    if (m_audioContent->m_rtpLocalCandidates.m_type == JGRtpCandidates::RtpIceUdp &&
	rtpRemote->m_address) {
	// Start STUN
	Message* msg = new Message("socket.stun");
	msg->userData(m.userData());
	// FIXME: check if these parameters are correct
	msg->addParam("localusername",m_audioContent->m_rtpRemoteCandidates.m_ufrag +
	    m_audioContent->m_rtpLocalCandidates.m_ufrag);
	msg->addParam("remoteusername",m_audioContent->m_rtpLocalCandidates.m_ufrag +
	    m_audioContent->m_rtpRemoteCandidates.m_ufrag);
	msg->addParam("remoteip",rtpRemote->m_address.c_str());
	msg->addParam("remoteport",rtpRemote->m_port);
	msg->addParam("userid",m.getValue("rtpid"));
	Engine::enqueue(msg);
    }
    else if (m_audioContent->m_rtpLocalCandidates.m_type == JGRtpCandidates::RtpRawUdp) {
	// Send trying
	if (m_session) {
	    XMLElement* trying = XMPPUtils::createElement(XMLElement::Trying,
		XMPPNamespace::JingleTransportRawUdpInfo);
	    m_session->sendInfo(trying);
	}
    }

    return true;
}

// Check a received candidate's parameters
// Return false if some parameter's value is incorrect
bool YJGConnection::checkRecvCandidate(JGSessionContent& content, JGRtpCandidate& c)
{
    // Check address and port for all
    if (!c.m_address || c.m_port.toInteger() <= 0)
	return false;
    if (content.m_rtpRemoteCandidates.m_type == JGRtpCandidates::RtpRawUdp) {
	// XEP-0177 4.2 these attributes are required
	return c.toString() && c.m_component && (c.m_generation.toInteger(-1) >= 0);
    }
    if (content.m_rtpRemoteCandidates.m_type == JGRtpCandidates::RtpIceUdp) {
	// XEP-0176 13 XML Schema: these attributes are required
	return c.toString() && c.m_component && (c.m_generation.toInteger(-1) >= 0) &&
	    c.m_network && c.m_priority && (c.m_protocol == "udp") && c.m_type;
    }
    return false;
}

// Check a received content(s). Fill received lists with accepted/rejected content(s)
// The lists don't own their pointers
// Return false on error
bool YJGConnection::processContentAdd(const JGEvent& event, ObjList& ok, ObjList& remove)
{
    for (ObjList* o = event.m_contents.skipNull(); o; o = o->skipNext()) {
	JGSessionContent* c = static_cast<JGSessionContent*>(o->get());

	bool fileTransfer = false;

	// Check content type
	switch (c->type()) {
	    case JGSessionContent::RtpIceUdp:
	    case JGSessionContent::RtpRawUdp:
		break;
	    case JGSessionContent::FileBSBOffer:
	    case JGSessionContent::FileBSBRequest:
		// File transfer contents can be added only in session initiate
		if (event.action() != JGSession::ActInitiate) {
		    Debug(this,DebugInfo,
			"Event(%s) content='%s':  [%p]",
			event.actionName(),c->toString().c_str(),this);
		    remove.append(c)->setDelete(false);
		    continue;
		}
		fileTransfer = true;
		break;
	    case JGSessionContent::Unknown:
	    case JGSessionContent::UnknownFileTransfer:
		Debug(this,DebugInfo,
		    "Event(%s) with unknown (unsupported) content '%s' [%p]",
		    event.actionName(),c->toString().c_str(),this);
		remove.append(c)->setDelete(false);
		continue;
	}

	// Check creator
	if ((isOutgoing() && c->creator() == JGSessionContent::CreatorInitiator) ||
	    (isIncoming() && c->creator() == JGSessionContent::CreatorResponder)) {
	    Debug(this,DebugInfo,
		"Event(%s) content='%s' has invalid creator [%p]",
		event.actionName(),c->toString().c_str(),this);
	    remove.append(c)->setDelete(false);
	    continue;
	}

	// Done if file transfer
	if (fileTransfer) {
	    ok.append(c)->setDelete(false);
	    continue;
	}

	// Check if we already have an audio content with the same name and creator
	if (findContent(*c,m_audioContents)) {
	    Debug(this,DebugInfo,
		"Event(%s) content='%s' is already added [%p]",
		event.actionName(),c->toString().c_str(),this);
	    return false;
	}

	// Check transport type
	if (c->m_rtpRemoteCandidates.m_type == JGRtpCandidates::Unknown) {
	    Debug(this,DebugInfo,
		"Event(%s) content='%s' has unknown transport type [%p]",
		event.actionName(),c->toString().c_str(),this);
	    remove.append(c)->setDelete(false);
	    continue;
	}

	// Check candidates
	// XEP-0177 Raw UDP: the content must contain valid transport data
	JGRtpCandidate* rtp = c->m_rtpRemoteCandidates.findByComponent(1);
	if (rtp) {
	    if (!checkRecvCandidate(*c,*rtp)) {
		Debug(this,DebugInfo,
		    "Event(%s) content='%s' has invalid RTP candidate [%p]",
		    event.actionName(),c->toString().c_str(),this);
	        remove.append(c)->setDelete(false);
		continue;
	    }
	}
	else if (c->m_rtpRemoteCandidates.m_type == JGRtpCandidates::RtpRawUdp) {
	    Debug(this,DebugInfo,
		"Event(%s) raw udp content='%s' without RTP candidate [%p]",
		event.actionName(),c->toString().c_str(),this);
	    remove.append(c)->setDelete(false);
	    continue;
	}
	JGRtpCandidate* rtcp = c->m_rtpRemoteCandidates.findByComponent(2);
	if (rtcp && !checkRecvCandidate(*c,*rtcp)) {
	    Debug(this,DebugInfo,
		"Event(%s) content='%s' has invalid RTCP candidate [%p]",
		event.actionName(),c->toString().c_str(),this);
	    remove.append(c)->setDelete(false);
	    continue;
	}

	// Check media
	// Fill a string with our capabilities for debug purposes
	String remoteCaps;
	if (debugAt(DebugInfo))
	    c->m_rtpMedia.createList(remoteCaps,false);
	// Check received media against the used codecs list
	// Compare 'id' and 'name'
	ListIterator iter(c->m_rtpMedia);
	for (GenObject* go; (go = iter.get());) {
	    JGRtpMedia* recv = static_cast<JGRtpMedia*>(go);
	    ObjList* used = s_usedCodecs.skipNull();
	    for (; used; used = used->skipNext()) {
		JGRtpMedia* local = static_cast<JGRtpMedia*>(used->get());
		if (local->m_id == recv->m_id && local->m_name == recv->m_name)
		    break;
	    }
	    if (!used)
		c->m_rtpMedia.remove(recv,true);
	}
	// Check if both parties have common media
	if (!c->m_rtpMedia.skipNull()) {
	    if (debugAt(DebugInfo)) {
		String localCaps;
		s_usedCodecs.createList(localCaps,false);
		Debug(this,DebugInfo,
		    "Event(%s) no common media for content='%s' local='%s' remote='%s' [%p]",
		    event.actionName(),c->toString().c_str(),localCaps.c_str(),
		    remoteCaps.c_str(),this);
	    }
	    remove.append(c)->setDelete(false);
	    continue;
	}

	// Check crypto
	bool error = false;
	ObjList* cr = c->m_rtpMedia.m_cryptoRemote.skipNull();
	for (; cr; cr = cr->skipNext()) {
	    JGCrypto* crypto = static_cast<JGCrypto*>(cr->get());
	    if (!(crypto->m_suite && crypto->m_keyParams)) {
		error = true;
		break;
	    }
	}
	if (error) {
	    Debug(this,DebugInfo,
		"Event(%s) content=%s with invalid crypto [%p]",
		event.actionName(),c->toString().c_str(),this);
	    remove.append(c)->setDelete(false);
	    continue;
	}

	// Ok
	ok.append(c)->setDelete(false);
    }

    return true;
}

// Remove contents
// Return false if there are no more contents
bool YJGConnection::removeContents(JGEvent* event)
{
    if (!event)
	return true;

    // Confirm and remove requested content(s)
    event->confirmElement();
    for (ObjList* o = event->m_contents.skipNull(); o; o = o->skipNext()) {
	JGSessionContent* c = static_cast<JGSessionContent*>(o->get());
	JGSessionContent* cc = findContent(*c,m_audioContents);
	if (cc) {
	    if (m_audioContent == cc)
		removeCurrentAudioContent(true);
	    else
		removeContent(cc);
	}
    }
    bool ok = 0 != m_audioContents.skipNull();
    if (!ok)
	Debug(this,DebugCall,"No more audio contents [%p]",this);
    return ok;
}

// Build a RTP audio content. Add used codecs to the list
// Build and init the candidate(s) if the content is a raw udp one
JGSessionContent* YJGConnection::buildAudioContent(JGRtpCandidates::Type type,
    JGSessionContent::Senders senders, bool rtcp, bool useFormats)
{
    String id;
    id << this->id() << "_content_" << (int)::random();
    JGSessionContent::Type t = JGSessionContent::Unknown;
    if (type == JGRtpCandidates::RtpRawUdp)
	t = JGSessionContent::RtpRawUdp;
    else if (type == JGRtpCandidates::RtpIceUdp)
	t = JGSessionContent::RtpIceUdp;
    JGSessionContent* c = new JGSessionContent(t,id,senders,
	isOutgoing() ? JGSessionContent::CreatorInitiator : JGSessionContent::CreatorResponder);

    // Add codecs
    c->m_rtpMedia.m_media = JGRtpMediaList::Audio;
    if (m_useCrypto && m_cryptoMandatory)
	c->m_rtpMedia.m_cryptoMandatory = true;
    if (useFormats)
	setMedia(c->m_rtpMedia,m_formats,s_usedCodecs);

    c->m_rtpLocalCandidates.m_type = c->m_rtpRemoteCandidates.m_type = type;

    if (type == JGRtpCandidates::RtpRawUdp || m_useCrypto)
	initLocalCandidates(*c,false);

    return c;
}

// Build a file transfer content
JGSessionContent* YJGConnection::buildFileTransferContent(bool send, const char* filename,
    NamedList& params)
{
    // Build the content
    String id;
    id << this->id() << "_content_" << (int)::random();
    JGSessionContent::Type t = JGSessionContent::Unknown;
    JGSessionContent::Senders s = JGSessionContent::SendUnknown;
    if (send) {
	t = JGSessionContent::FileBSBOffer;
	s = JGSessionContent::SendInitiator;
    }
    else {
	t = JGSessionContent::FileBSBRequest;
	s = JGSessionContent::SendResponder;
    }
    JGSessionContent* c = new JGSessionContent(t,id,s,JGSessionContent::CreatorInitiator);

    // Init file
    c->m_fileTransfer.addParam("name",filename);
    int sz = params.getIntValue("file_size",-1);
    if (sz >= 0)
	c->m_fileTransfer.addParam("size",String(sz));
    const char* hash = params.getValue("file_md5");
    if (!null(hash))
	c->m_fileTransfer.addParam("hash",hash);
    int date = params.getIntValue("file_time",-1);
    if (date >= 0) {
	String buf;
	XMPPUtils::encodeDateTimeSec(buf,date);
	c->m_fileTransfer.addParam("date",buf);
    }

    return c;
}

// Reserve local port for a RTP session content
bool YJGConnection::initLocalCandidates(JGSessionContent& content, bool sendTransInfo)
{
    JGRtpCandidate* rtp = content.m_rtpLocalCandidates.findByComponent(1);
    bool incGeneration = (0 != rtp);
    if (!rtp) {
	rtp = buildCandidate();
	content.m_rtpLocalCandidates.append(rtp);
    }

    // TODO: handle RTCP

    Message m("chan.rtp");
    m.userData(static_cast<CallEndpoint*>(this));
    complete(m);
    m.addParam("direction",rtpDir(content));
    m.addParam("media","audio");
    m.addParam("getsession","true");
    m.addParam("anyssrc","true");
    if (s_localAddress)
	m.addParam("localip",s_localAddress);
    else {
	JGRtpCandidate* remote = content.m_rtpRemoteCandidates.findByComponent(1);
	if (remote && remote->m_address)
	    m.addParam("remoteip",remote->m_address);
	else {
	    String rem;
	    getRemoteAddr(rem);
	    if (rem)
		m.addParam("remoteip",rem);
	}
    }
    ObjList* cr = content.m_rtpMedia.m_cryptoRemote.skipNull();
    if (cr) {
	JGCrypto* crypto = static_cast<JGCrypto*>(cr->get());
	m.addParam("secure",String::boolText(true));
	m.addParam("crypto_suite",crypto->m_suite);
	m.addParam("crypto_key",crypto->m_keyParams);
    }
    else if (m_useCrypto)
	m.addParam("secure",String::boolText(true));

    if (!Engine::dispatch(m)) {
	Debug(this,DebugNote,"Failed to init RTP for content='%s' [%p]",
	    content.toString().c_str(),this);
	return false;
    }

    NamedString* cSuite = m.getParam("ocrypto_suite");
    if (cSuite) {
	JGCrypto* crypto = new JGCrypto("1",*cSuite,m.getValue("ocrypto_key"));
	content.m_rtpMedia.m_cryptoLocal.append(crypto);
    }

    rtp->m_address = m.getValue("localip",s_localAddress);
    rtp->m_port = m.getValue("localport","-1");

    if (incGeneration) {
	rtp->m_generation = rtp->m_generation.toInteger(0) + 1;
	sendTransInfo = true;
    }
    // Send transport info
    if (sendTransInfo && m_session)
	m_session->sendContent(JGSession::ActTransportInfo,&content);

    return true;
}

// Match a local content agaist a received one
// Return false if there is no common media
bool YJGConnection::matchMedia(JGSessionContent& local, JGSessionContent& recv) const
{
    ListIterator iter(local.m_rtpMedia);
    for (GenObject* gen = 0; 0 != (gen = iter.get()); ) {
	JGRtpMedia* m = static_cast<JGRtpMedia*>(gen);
	if (!recv.m_rtpMedia.find(m->toString()))
	    local.m_rtpMedia.remove(m);
    }
    return 0 != local.m_rtpMedia.skipNull();
}

// Find a session content in a list
JGSessionContent* YJGConnection::findContent(JGSessionContent& recv,
    const ObjList& list) const
{
    for (ObjList* o = list.skipNull(); o; o = o->skipNext()) {
	JGSessionContent* c = static_cast<JGSessionContent*>(o->get());
	if (c->creator() == recv.creator() && c->toString() == recv.toString())
	    return c;
    }
    return 0;
}

// Set early media to remote
void YJGConnection::setEarlyMediaOut(Message& msg)
{
    if (isOutgoing() || isAnswered())
	return;

    // Don't set it if the peer don't have a source
    if (!(getPeer() && getPeer()->getSource() && msg.getBoolValue("earlymedia",true)))
	return;

    String formats = msg.getParam("formats");
    if (!formats)
	formats = getPeer()->getSource()->getFormat();
    if (!formats)
	return;

    Lock lock(m_mutex);
    if (m_audioContent && m_audioContent->isEarlyMedia())
	return;

    // Check if we already have an early media content
    JGSessionContent* c = 0;
    for (ObjList* o = m_audioContents.skipNull(); o; o = o->skipNext()) {
	c = static_cast<JGSessionContent*>(o->get());
	if (c->isValidAudio() && c->isEarlyMedia())
	    break;
	c = 0;
    }

    // Build a new content if not found
    if (!c) {
	c = buildAudioContent(JGRtpCandidates::RtpRawUdp,
	    JGSessionContent::SendResponder,false,false);
	setMedia(c->m_rtpMedia,formats,s_usedCodecs);
	c->setEarlyMedia();
	addContent(true,c);
    }

    resetCurrentAudioContent(false,true,false,c);
    if (m_session)
	m_session->sendContent(JGSession::ActContentAdd,c);
}

// Enqueue a call.progress message from the current audio content
// Used for early media
void YJGConnection::enqueueCallProgress()
{
    if (!(m_audioContent && m_audioContent->isEarlyMedia()))
	return;
    
    Message* m = message("call.progress");
    String formats;
    m_audioContent->m_rtpMedia.createList(formats,true);
    m->addParam("formats",formats);
    Engine::enqueue(m);
}

// Set file transfer stream host
bool YJGConnection::setupSocksFileTransfer(bool start)
{
    if (!m_session) {
	DDebug(this,DebugNote,"setupSocksFileTransfer: no session [%p]",this);
	return false;
    }
    JGSessionContent* c = firstFTContent();
    if (!c) {
	DDebug(this,DebugNote,"setupSocksFileTransfer: no contents [%p]",this);
	return false;
    }
    const char* dir = 0;
    if (c->type() == JGSessionContent::FileBSBOffer)
	dir = isOutgoing() ? "send" : "receive";
    else if (c->type() == JGSessionContent::FileBSBRequest)
	dir = isIncoming() ? "send" : "receive";
    else {
	DDebug(this,DebugNote,"setupSocksFileTransfer: no SOCKS contents [%p]",this);
	return false;
    }

    if (start) {
	Message m("chan.socks");
	m.userData(this);
	m.addParam("dst_addr_domain",m_dstAddrDomain);
	m.addParam("format","data");
	bool ok = Engine::dispatch(m);
	if (ok) {
	    m_ftStatus = FTRunning;
	    Debug(this,DebugAll,"Started SOCKS file transfer [%p]",this);
	}
	else {
	    setReason("notransport");
	    m_ftStatus = FTTerminated;
	    Debug(this,DebugNote,"Failed to start SOCKS file transfer [%p]",this);
	}
	return ok;
    }

    // Init transport
    const char* error = 0;
    while (true) {
	ObjList* o = m_streamHosts.skipNull();
	if (!o) {
	    // We can send hosts: try to get a local socks server
	    if (m_ftHostDirection == FTHostLocal) {
		Message m("chan.socks");
		m.userData(this);
		m.addParam("dst_addr_domain",m_dstAddrDomain);
		m.addParam("direction",dir);
		m.addParam("client",String::boolText(false));
		DDebug(this,DebugAll,"Trying to setup local SOCKS server [%p]",this);
		clearEndpoint("data");
		if (Engine::dispatch(m)) {
		    const char* addr = m.getValue("address");
		    int port = m.getIntValue("port");
		    if (!null(addr) && port > 0) {
			m_ftNotifier = m.getValue("notifier");
			m_streamHosts.append(new JGStreamHost(m_local,addr,port));
			m_ftStatus = FTWaitEstablish;
			// Send our stream host
			m_session->sendStreamHosts(m_streamHosts,&m_ftStanzaId);
			break;
		    }
		}
		error = "chan.socks failed";
	    }
	    else
		error = "no hosts";
	    break;
	}

	// Remove the first stream host if status is idle: it failed
	if (m_ftStatus != FTIdle) {
	    JGStreamHost* sh = static_cast<JGStreamHost*>(o->get());
	    Debug(this,DebugNote,"Removing failed streamhost '%s:%d' [%p]",
		sh->m_address.c_str(),sh->m_port,this);
	    o->remove();
	    o = m_streamHosts.skipNull();
	}

	while (o) {
	    Message m("chan.socks");
	    m.userData(this);
	    m.addParam("dst_addr_domain",m_dstAddrDomain);
	    m.addParam("direction",dir);
	    m.addParam("client",String::boolText(true));
	    JGStreamHost* sh = static_cast<JGStreamHost*>(o->get());
	    m.addParam("remoteip",sh->m_address);
	    m.addParam("remoteport",String(sh->m_port));
	    clearEndpoint("data");
	    if (Engine::dispatch(m)) {
		m_ftNotifier = m.getValue("notifier");
		break;
	    }
	    Debug(this,DebugNote,"Removing failed streamhost '%s:%d' [%p]",
		sh->m_address.c_str(),sh->m_port,this);
	    o->remove();
	    o = m_streamHosts.skipNull();
	}
	if (o)
	    m_ftStatus = FTWaitEstablish;
	else
	    error = "no more hosts";
	break;
    }

    if (!error) {
	DDebug(this,DebugAll,"Waiting SOCKS file transfer [%p]",this);
	return true;
    }

    // Check if we can still negotiate hosts
    if (changeFTHostDir()) {
	m_ftStatus = FTIdle;
	return false;
    }

    setReason("notransport");
    m_ftStatus = FTTerminated;
    Debug(this,DebugNote,"Failed to initialize SOCKS file transfer '%s' [%p]",
	error,this);
    return false;
}

// Change host sender. Return false on failure
bool YJGConnection::changeFTHostDir()
{
    // Outgoing: we've sent hosts, allow remote to sent hosts
    // Incoming: remote sent hosts, allow us to send hosts
    bool fromLocal = (m_ftHostDirection == FTHostRemote);
    if (m_ftHostDirection != FTHostNone && isOutgoing() != fromLocal) {
	m_ftHostDirection = fromLocal ? FTHostLocal : FTHostRemote;
	Debug(this,DebugAll,"Allowing %s party to send file transfer host(s) [%p]",
	    fromLocal ? "local" : "remote",this);
	return true;
    }
    if (m_ftHostDirection != FTHostNone)
	Debug(this,DebugNote,"No more host available [%p]",this); 
    m_ftHostDirection = FTHostNone;
    return false;
}

// Process chan.notify messages
// Handle SOCKS status changes for file transfer
bool YJGConnection::processChanNotify(Message& msg)
{
    NamedString* notifier = msg.getParam("id");
    if (!notifier)
	return false;
    Lock lock(m_mutex);
    if (m_state == Terminated)
	return true;
    if (*notifier == m_ftNotifier) {
	NamedString* status = msg.getParam("status");
	if (!status)
	    return false;
	if (*status == "established") {
	    // Safety check
	    if (m_state == Terminated || !m_session ||
		m_ftHostDirection == FTHostNone || !m_streamHosts.skipNull()) {
		hangup("failure");
		return true;
	    }
	    const String& jid = m_streamHosts.skipNull()->get()->toString();
	    if (isOutgoing()) {
		// Send hosts if the jid is not our's: we did't sent it
		if (m_ftHostDirection == FTHostLocal) {
		    if (m_local != jid)
			m_session->sendStreamHosts(m_streamHosts,&m_ftStanzaId);
		}
		else
		    m_session->sendStreamHostUsed(jid,m_ftStanzaId);
	    }
	    else {
		if (m_ftHostDirection == FTHostRemote)
		    m_session->sendStreamHostUsed(jid,m_ftStanzaId);
		// Accept the session
		if (isAnswered()) {
		    if (setupSocksFileTransfer(true)) {
			ObjList tmp;
			JGSessionContent* c = firstFTContent();
			if (c)
			    tmp.append(c)->setDelete(false);
			m_session->accept(tmp);
		    }
		    else
			hangup("failure");
		}
	    }
	    if (m_ftStatus != FTRunning && !m_hangup)
		m_ftStatus = FTEstablished;
	}
	else if (*status == "running") {
	    // Ignore it for now !!!
	}
	else if (*status == "terminated") {
	    if (m_ftStatus == FTWaitEstablish) {
		// Try to setup another stream host
		// Remember: setupSocksFileTransfer changes the host dir
		if (setupSocksFileTransfer(false))
		    return true;
		if (m_ftStatus != FTTerminated &&
		    m_ftHostDirection != FTHostNone && m_session) {
		    m_streamHosts.clear();
		    // Current host dir is remote: old one was local: send empty hosts
		    if (m_ftHostDirection == FTHostRemote) {
			m_session->sendStreamHosts(m_streamHosts,&m_ftStanzaId);
			return true;
		    }
		    // Respond and try to setup our hosts
		    if (m_ftStanzaId) {
			m_session->sendStreamHostUsed("",m_ftStanzaId);
			m_ftStanzaId = "";
		    }
		    if (setupSocksFileTransfer(false))
			return true;
		}
	    }
	    else if (m_ftStatus != FTIdle)
		hangup("failure");
	}
	return true;
    }
    return false;
}

// Handle hold/active/mute actions
// Confirm the received element
void YJGConnection::handleAudioInfoEvent(JGEvent* event)
{
    Lock lock(m_mutex);
    if (!(event && m_session))
	return;

    XMPPError::Type err = XMPPError::NoError;
    const char* text = 0;
    // Hold
    bool hold = event->action() == JGSession::ActHold;
    if (hold || event->action() == JGSession::ActActive) {
	if ((hold && !dataFlags(OnHold)) || (!hold && dataFlags(OnHoldRemote))) {
	    XMLElement* what = event->jingle() ? event->jingle()->findFirstChild(
		hold ? XMLElement::Hold : XMLElement::Active) : 0;
	    if (what) {
		if (hold)
		    m_dataFlags |= OnHoldRemote;
		else
		    m_dataFlags &= ~OnHoldRemote;
		const char* name = what->name();
		Message* m = message("call.update");
		m->addParam("operation","notify");
		m->userData(this);
		// Copy additional attributes
		// Reset param 'name': the second param of toList() is the prefix
		what->toList(*m,name);
		m->setParam(name,String::boolText(true));
		TelEngine::destruct(what);
		// Clear endpoint before dispatching the message
		// Our data source/consumer may be replaced
		if (hold)
		    clearEndpoint();
		Engine::dispatch(*m);
		TelEngine::destruct(m);
		// Reset data transport when put on hold
		removeCurrentAudioContent();
		// Update channel data source/consumer
		if (!hold)
		    resetCurrentAudioContent(true,false);
	    }
	    else
		err = XMPPError::SFeatureNotImpl;
	}
	// Respond with error if put on hold by the other party
	else if (dataFlags(OnHoldLocal)) {
	    err = XMPPError::SRequest;
	    text = "Already on hold by the other party";
	}
    }
    else if (event->action() == JGSession::ActMute) {
	// TODO: implement
	err = XMPPError::SFeatureNotImpl;
    }
    else
	err = XMPPError::SFeatureNotImpl;

    // Confirm received element
    if (err == XMPPError::NoError) {
	DDebug(this,DebugAll,"Accepted '%s' request [%p]",event->actionName(),this);
	event->confirmElement();
    }
    else {
	XMPPError e;
	Debug(this,DebugInfo,"Denying '%s' request error='%s' reason='%s' [%p]",
	    event->actionName(),e[err],text,this);
	event->confirmElement(err,text);
    }
}

/**
 * Transfer thread (route and execute)
 */
YJGTransfer::YJGTransfer(YJGConnection* conn, const char* subject)
    : Thread("Jingle transfer"),
    m_msg("call.route")
{
    if (!conn)
	return;
    m_transferorID = conn->id();
    Channel* ch = YOBJECT(Channel,conn->getPeer());
    if (!(ch && ch->driver()))
	return;
    m_transferredID = ch->id();
    m_transferredDrv = ch->driver();
    // Set transfer data from channel
    m_to.set(conn->m_transferTo.node(),conn->m_transferTo.domain(),conn->m_transferTo.resource());
    m_from.set(conn->m_transferFrom.node(),conn->m_transferFrom.domain(),conn->m_transferFrom.resource());
    m_sid = conn->m_transferSid;
    if (!m_from)
	m_from.set(conn->remote().node(),conn->remote().domain(),conn->remote().resource());
    // Build the routing message if unattended
    if (!m_sid) {
	m_msg.addParam("id",m_transferredID);
	if (conn->billid())
	    m_msg.addParam("billid",conn->billid());
	m_msg.addParam("caller",m_from.node());
	m_msg.addParam("called",m_to.node());
	m_msg.addParam("calleduri",BUILD_XMPP_URI(m_to));
	m_msg.addParam("diverter",m_from.bare());
	m_msg.addParam("diverteruri",BUILD_XMPP_URI(m_from));
	if (!null(subject))
	    m_msg.addParam("subject",subject);
	m_msg.addParam("reason",lookup(JGSession::ReasonTransfer,s_errMap));
    }
}

void YJGTransfer::run()
{
    DDebug(&plugin,DebugAll,"'%s' thread transferror=%s transferred=%s to=%s [%p]",
	name(),m_transferorID.c_str(),m_transferredID.c_str(),m_to.c_str(),this);
    String error;
    // Attended
    if (m_sid) {
	plugin.lock();
	RefPointer<Channel> chan = plugin.findBySid(m_sid);
	plugin.unlock();
	String peer = chan ? chan->getPeerId() : "";
	if (peer) {
	    Message m("chan.connect");
	    m.addParam("id",m_transferredID);
	    m.addParam("targetid",peer);
	    m.addParam("reason","transferred");
	    if (!Engine::dispatch(m))
		error = m.getValue("error","Failed to connect");
	}
	else
	    error << "No peer for sid=" << m_sid;
    }
    else {
        error = m_transferredDrv ? "" : "No driver for transferred connection";
	while (m_transferredDrv) {
	    // Unattended: route the call
#define SET_ERROR(err) { error << err; break; }
	    bool ok = Engine::dispatch(m_msg);
	    m_transferredDrv->lock();
	    RefPointer<Channel> chan = m_transferredDrv->find(m_transferredID);
	    m_transferredDrv->unlock();
    	    if (!chan)
		SET_ERROR("Connection vanished while routing");
	    if (!ok || (m_msg.retValue() == "-") || (m_msg.retValue() == "error"))
		SET_ERROR("call.route failed error=" << m_msg.getValue("error"));
	    // Execute the call
	    m_msg = "call.execute";
	    m_msg.setParam("callto",m_msg.retValue());
	    m_msg.clearParam("error");
	    m_msg.retValue().clear();
	    m_msg.userData(chan);
	    if (Engine::dispatch(m_msg))
		break;
	    SET_ERROR("'call.execute' failed error=" << m_msg.getValue("error"));
#undef SET_ERROR
	}
    }
    // Notify termination to transferor
    plugin.lock();
    YJGConnection* conn = static_cast<YJGConnection*>(plugin.Driver::find(m_transferorID));
    if (conn)
	conn->transferTerminated(!error,error);
    else
	DDebug(&plugin,DebugInfo,
	    "%s thread transfer terminated trans=%s error=%s [%p]",
	    name(),m_transferredID.c_str(),error.c_str(),this);
    plugin.unlock();
}


/**
 * resource.notify message handler
 */
bool ResNotifyHandler::received(Message& msg)
{
    // Avoid loopback message (if the same module: it's a message sent by this module)
    if (plugin.name() == msg.getValue("module"))
	return false;

    // Check status
    NamedString* status = msg.getParam("status");
    if (!status || status->null())
	return false;

    if (s_jabber && s_jabber->protocol() == JBEngine::Client) {
	NamedString* account = msg.getParam("account");
	if (!account || account->null())
	    return false;
	JBClientStream* stream = static_cast<JBClientStream*>(s_jabber->findStream(*account));
	if (!stream)
	    return false;
	const char* to = msg.getValue("to");
	XDebug(&plugin,DebugAll,"%s account=%s to=%s status=%s",
	    msg.c_str(),account->c_str(),to,status->c_str());
	XMLElement* pres = 0;
	bool ok = (*status == "subscribed");
	if (ok || *status == "unsubscribed")
	    pres = JBPresence::createPresence(0,to,
		ok?JBPresence::Subscribed:JBPresence::Unsubscribed);
	else {
	    Lock lock(stream->streamMutex());
	    JIDResource* res = stream->getResource();
	    if (res && res->ref()) {
		res->priority(msg.getIntValue("priority",res->priority()));
		if (*status == "online")
		    res->setPresence(true);
		else if (*status == "offline")
		    res->setPresence(false);
		else
		    res->status(*status);
		pres = JBPresence::createPresence(stream->local().bare(),to,
		   res->available()?JBPresence::None:JBPresence::Unavailable);
		res->addTo(pres,true);
		TelEngine::destruct(res);
	    }
	}
	ok = false;
	if (pres) {
	    plugin.addChildren(msg,pres);
	    JBStream::Error err = stream->sendStanza(pres);
	    ok = (err == JBStream::ErrorNone) || (err == JBStream::ErrorPending);
	}
	TelEngine::destruct(stream);
	return ok;
    }

    if (!s_presence)
	return false;

    JabberID from,to;
    // *** Check from/to
    bool broadcast = false;
    if (!plugin.getJidFrom(from,msg,true))
	return false;
    if (!s_presence->autoRoster())
	to = msg.getValue("to");
    else
	if (s_presence->addOnPresence().to() || s_presence->addOnSubscribe().to()) {
	    broadcast = (0 == msg.getParam("to"));
	    if (!(broadcast || plugin.decodeJid(to,msg,"to")))
		return false;
	}
	else if (!plugin.decodeJid(to,msg,"to"))
	    return false;
    // *** Everything is OK. Process the message
    XDebug(&plugin,DebugAll,"Received '%s' from '%s' with status '%s'",
	msg.c_str(),from.c_str(),status->c_str());
    // Broadcast
    if (broadcast) {
	NamedString* status = msg.getParam("status");
	if (status && (*status == "subscribed" || *status == "unsubscribed"))
	    return false;
	XMPPUserRoster* roster = s_presence->getRoster(from,false,0);
	if (!roster) {
	    Debug(&plugin,DebugNote,"Can't send presence from '%s': no roster",from.c_str());
	    return false;
	}
	bool unavail = (status && *status == "offline");
	roster->lock();
	for (ObjList* o = roster->users().skipNull(); o; o = o->skipNext()) {
	    XMPPUser* user = static_cast<XMPPUser*>(o->get());
	    const char* name = from.resource();
	    if (!name)
		name = s_jabber->defaultResource();
	    JIDResource* res = 0;
	    bool changed = false;
	    if (name) {
		changed = user->addLocalRes(new JIDResource(name,
		    unavail ? JIDResource::Unavailable : JIDResource::Available,
		    JIDResource::CapAudio),false);
		res = user->localRes().get(name);
	    }
	    else
		res = user->getAudio(true,true);
	    if (!res)
		continue;
	    res->infoXml()->clear();
	    res->priority(msg.getIntValue("priority",res->priority()));
	    plugin.addChildren(msg,0,res->infoXml());
	    if (unavail)
		changed = res->setPresence(false) || changed;
	    else {
		changed = res->setPresence(true) || changed;
	    	if (status && *status == "online") {
		    if (!res->status().null()) {
			res->status("");
			changed = true;
		    }
		}
		else if (status && *status != res->status()) {
		    res->status(*status);
		    changed = true;
		}
	    }
	    if (changed && user->subscription().from())
		user->sendPresence(res,0,true);
	    // Remove if unavailable
	    if (!res->available())
		user->removeLocalRes(res);
	}
	roster->unlock();
	TelEngine::destruct(roster);
    }
    else
	if (s_presence->addOnPresence().to() || s_presence->addOnSubscribe().to())
	    process(from,to,*status,msg.getBoolValue("subscription",false),&msg);
	else
	    sendPresence(from,to,*status,&msg);
    return true;
}

void ResNotifyHandler::process(const JabberID& from, const JabberID& to,
	const String& status, bool subFrom, NamedList* params)
{
    if (!s_presence)
	return;
    DDebug(&plugin,DebugAll,"ResNotifyHandler::process() from=%s to=%s status=%s",
	from.c_str(),to.c_str(),status.c_str());

    bool pres = (status != "subscribed") && (status != "unsubscribed");
    bool add = pres ? s_presence->addOnPresence().to() : s_presence->addOnSubscribe().to();
    XMPPUserRoster* roster = s_presence->getRoster(from,add,0);
    if (!roster)
	return;
    XMPPUser* user = roster->getUser(to,false,0);

    bool newUser = (0 == user);
    // Add new user
    if (newUser) {
	user = new XMPPUser(roster,to.node(),to.domain(),
	    subFrom ? XMPPDirVal::From : XMPPDirVal::None,false,false);
	if (!user->ref())
	    user = 0;
    }
    TelEngine::destruct(roster);
    if (!user)
	return;
    Lock lock(user);
    // Process
    for (;;) {
	// Subscription response
	if (!pres) {
	    if (status == "subscribed") {
		// Send only if not already subscribed to us
		if (!user->subscription().from())
		    user->sendSubscribe(JBPresence::Subscribed,0);
		break;
	    }
	    if (status == "unsubscribed") {
		// Send only if not already unsubscribed from us
		if (user->subscription().from())
		    user->sendSubscribe(JBPresence::Unsubscribed,0);
		break;
	    }
	    break;
	}

	// Presence
	JIDResource::Presence p = (status != "offline") ?
	    JIDResource::Available : JIDResource::Unavailable;
	const char* name = from.resource();
	if (!name)
	    name = s_jabber->defaultResource();
	JIDResource* res = 0;
	bool changed = false;
	if (name) {
	    changed = user->addLocalRes(new JIDResource(name,p,JIDResource::CapAudio),false);
	    res = user->localRes().get(name);
	}
	else
	    res = user->getAudio(true,true);
	if (!res) {
	    DDebug(&plugin,DebugNote,
		"ResNotifyHandler::process() from=%s to=%s status=%s: no resource named '%s'",
		from.c_str(),to.c_str(),status.c_str(),name);
	    break;
	}
	res->infoXml()->clear();
	if (params) {
	    res->priority(params->getIntValue("priority",res->priority()));
	    plugin.addChildren(*params,0,res->infoXml());
	}
	if (p == JIDResource::Unavailable)
	    changed = res->setPresence(false) || changed;
	else {
	    changed = res->setPresence(true) || changed;
	    if (status == "online") {
		if (!res->status().null()) {
		    res->status("");
		    changed = true;
		}
	    }
	    else {
		if (status != res->status()) {
		    res->status(status);
		    changed = true;
		}
	    }
	}

	if (changed && user->subscription().from())
	    user->sendPresence(res,0,true);
	// Remove if unavailable
	if (!res->available())
	    user->removeLocalRes(res);
	break;
    }
    lock.drop();
    TelEngine::destruct(user);
}

void ResNotifyHandler::sendPresence(JabberID& from, JabberID& to,
	const String& status, NamedList* params)
{
    if (!s_presence)
	return;
    JBPresence::Presence jbPresence;
    bool command = !s_presence->autoRoster();
    // Get presence type from status
    if (status == "online")
	jbPresence = JBPresence::None;
    else if (status == "offline")
	jbPresence = JBPresence::Unavailable;
    else {
	if (status == "subscribed")
	    jbPresence = JBPresence::Subscribed;
	else if (status == "unsubscribed") 
	    jbPresence = JBPresence::Unsubscribed;
	else
	    jbPresence = JBPresence::None;
	if (command && (jbPresence != JBPresence::None)) {
	    XDebug(&plugin,DebugNote,"Can't send command for status='%s'",status.c_str());
	    return;
	}
    }
    // Check if we can get a stream
    JBStream* stream = s_jabber->getStream();
    if (!stream)
	return;
    // Create XML element to be sent
    bool available = (jbPresence == JBPresence::None);
    XMLElement* stanza = 0;
    // Build the presence element:
    // Command: no 'from'/'to'
    XMLElement* pres = 0;
    if (!command)
	pres = stanza = JBPresence::createPresence(from,to,jbPresence);
    else if (s_attachPresToCmd && params)
	pres = JBPresence::createPresence(0,0,jbPresence);
    if (pres) {
	// Create resource info if available or command
	if (available) {
	    JIDResource* resource = new JIDResource(from.resource(),JIDResource::Available,
		JIDResource::CapAudio,params ? params->getIntValue("priority") : -1);
	    if (status != "online")
		resource->status(status);
	    resource->addTo(pres);
	    TelEngine::destruct(resource);
	}
	// Add extra children to presence
	if (params)
	    plugin.addChildren(*params,pres);
    }
    if (command) {
	if (to.domain().null())
	    to.domain(s_jabber->componentServer().c_str());
	stanza = plugin.getPresenceCommand(from,to,available,pres);
    }
    // Send
    DDebug(&plugin,DebugAll,"Sending presence%s '%s' from '%s' to '%s'",
	command ? " command" : "",
	String::boolText(available),from.c_str(),to.c_str());
    stream->sendStanza(stanza);
    TelEngine::destruct(stream);
}

/**
 * resource.subscribe message handler
 */
bool ResSubscribeHandler::received(Message& msg)
{
    // Avoid loopback message (if the same module: it's a message sent by this module)
    if (plugin.name() == msg.getValue("module"))
	return false;

    // Check operation
    NamedString* oper = msg.getParam("operation");
    if (!oper)
	return false;
    JBPresence::Presence presence;
    if (*oper == "subscribe")
	presence = JBPresence::Subscribe;
    else if (*oper == "probe")
	presence = JBPresence::Probe;
    else if (*oper == "unsubscribe")
	presence = JBPresence::Unsubscribe;
    else
	return false;

    XMLElement* pres = 0;
    JBStream* stream = 0;
    bool ok = false;
    while (true) {
	// Client stream
	NamedString* account = msg.getParam("account");
	if (account) {
	    stream = s_jabber->findStream(*account);
	    if (stream) {
		XDebug(&plugin,DebugAll,"%s account=%s to=%s operation=%s",
		    msg.c_str(),account->c_str(),msg.getValue("to"),oper->c_str());
		pres = JBPresence::createPresence(stream->local(),
		    msg.getValue("to"),presence);
		break;
	    }
	}

	// Component stream
	if (!s_presence || s_jabber->protocol() == JBEngine::Client)
	    break;
	JabberID from,to;
	// Check from/to
	if (!plugin.decodeJid(from,msg,"from",true))
	    break;
	if (!plugin.decodeJid(to,msg,"to"))
	    break;
	XDebug(&plugin,DebugAll,"%s from=%s to=%s operation=%s",
	    msg.c_str(),from.c_str(),to.c_str(),oper->c_str());
	// Don't automatically add
	if ((presence == JBPresence::Probe && !s_presence->addOnProbe().to()) ||
	    ((presence == JBPresence::Subscribe || presence == JBPresence::Unsubscribe) &&
	    !s_presence->addOnSubscribe().to())) {
	    stream = s_jabber->getStream();
	    if (stream)
		pres = JBPresence::createPresence(from,to,presence);
	    break;
	}
	// Add roster/user
	XMPPUserRoster* roster = s_presence->getRoster(from,true,0);
	XMPPUser* user = roster->getUser(to,false,0);
	// Add new user and local resource
	if (!user) {
	    user = new XMPPUser(roster,to.node(),to.domain(),XMPPDirVal::From,
		false,false);
	    s_presence->notifyNewUser(user);
	    if (!user->ref()) {
		TelEngine::destruct(roster);
		break;
	    }
	}
	TelEngine::destruct(roster);
	// Process
	ok = true;
	user->lock();
	for (;;) {
	    if (presence == JBPresence::Subscribe ||
		presence == JBPresence::Unsubscribe) {
		bool sub = (presence == JBPresence::Subscribe);
		// Already (un)subscribed: notify. NO: send request
		if (sub != user->subscription().to()) {
		    user->sendSubscribe(presence,0);
		    user->probe(0);
		}
		else
		    s_presence->notifySubscribe(user,sub?JBPresence::Subscribed:JBPresence::Unsubscribed);
		break;
	    }
	    // Respond if user has a resource with audio capabilities
	    JIDResource* res = user->getAudio(false,true);
	    if (res) {
		user->notifyResource(true,res->name());
		break;
	    }
	    // No audio resource for remote user: send probe
	    // Send probe fails: Assume remote user unavailable
	    if (!user->probe(0)) {
		XMLElement* xml = JBPresence::createPresence(to,from,JBPresence::Unavailable);
		JBEvent* event = new JBEvent(JBEvent::Presence,0,xml);
		s_presence->notifyPresence(event,false);
		TelEngine::destruct(event);
	    }
	    break;
	}
	user->unlock();
	TelEngine::destruct(user);
	break;
    }

    if (stream && !ok) {
	JBStream::Error err = stream->sendStanza(pres);
	pres = 0;
	ok = (err == JBStream::ErrorNone) || (err == JBStream::ErrorPending);
    }
    TelEngine::destruct(stream);
    TelEngine::destruct(pres);
    return ok;
}

/**
 * UserLoginHandler
 */
bool UserLoginHandler::received(Message& msg)
{
    if (!(s_jabber && s_jabber->protocol() == JBEngine::Client))
	return false;
    if (!plugin.canHandleProtocol(msg.getValue("protocol")))
	return false;
    NamedString* account = msg.getParam("account");
    if (!account || account->null())
	return false;
    // Check operation
    NamedString* oper = msg.getParam("operation");
    bool login = !oper || *oper == "login" || *oper == "create";
    if (!login && (!oper || (*oper != "logout" && *oper != "delete")))
	return false;

    Debug(&plugin,DebugAll,"user.login for account=%s operation=%s",
	account->c_str(),oper?oper->c_str():"");

    JBClientStream* stream = static_cast<JBClientStream*>(s_jabber->findStream(*account));
    bool ok = false;
    if (login) {
	if (!stream)
	    stream = s_jabber->createClientStream(msg);
	else
	    msg.setParam("error","User already logged in");
	ok = (0 != stream);
    }
    else if (stream) {
	if (stream->state() == JBStream::Running) {
	    XMLElement* xml = JBPresence::createPresence(0,0,JBPresence::Unavailable);
	    stream->sendStanza(xml);
	}
	const char* reason = msg.getValue("reason");
	if (!reason)
	    reason = Engine::exiting() ? "" : "Logout";
	XMPPError::Type err = Engine::exiting()?XMPPError::Shutdown:XMPPError::NoError;
	stream->terminate(true,0,err,reason,true);
	ok = true;
    }
    TelEngine::destruct(stream);
    return ok;
}

/**
 * XmppGenerateHandler
 */
bool XmppGenerateHandler::received(Message& msg)
{
    // Process only mesages not enqueued by this module
    if (!s_jabber || plugin.name() == msg.getValue("module"))
	return false;

    // Check protocol only if present
    const char* proto = msg.getValue("protocol");
    if (proto && !plugin.canHandleProtocol(proto))
	return false;

    // Try to get a stream to sent the stanza
    JBStream* stream = 0;
    if (s_jabber->protocol() == JBEngine::Client) {
	NamedString* account = msg.getParam("account");
	if (!account)
	    return false;
	stream = s_jabber->findStream(*account);
    }
    else {
	JabberID f(msg.getValue("from"));
	stream = s_jabber->getStream(f.null()?0:&f);
    }
    if (!stream)
	return false;

    // Get and send stanza
    bool ok = false;
    XMLElement* stanza = XMLElement::getXml(msg,true);
    if (stanza) {
	JBStream::Error res = stream->sendStanza(stanza,msg.getValue("id"));
	ok = (res == JBStream::ErrorNone || res == JBStream::ErrorPending);
    }
    TelEngine::destruct(stream);
    return ok;
}


/**
 * XmppIqHandler
 */
bool XmppIqHandler::received(Message& msg)
{
    // Process only mesages enqueued by this module
    if (plugin.name() != msg.getValue("module"))
	return false;
    // Ignore failed stanzas
    if (msg.getBoolValue("failure"))
	return false;
    // Respond only to type 'set' or 'get'
    NamedString* type = msg.getParam("type");
    if (!type || (*type != "set" && *type != "get"))
	return false;

    NamedString* account = msg.getParam("account");
    const char* from = msg.getValue("from");
    const char* to = msg.getValue("to");
    const char* id = msg.getValue("id");
    Debug(&plugin,DebugAll,"%s: account=%s from=%s to=%s id=%s returned to module",
	msg.c_str(),account?account->c_str():"",from,to,id);

    JBStream* stream = 0;
    if (account)
	stream = s_jabber->findStream(*account);
    else {
	JabberID f(from);
	stream = s_jabber->getStream(f.null()?0:&f);
    }
    if (!stream)
	return false;

    // Don't send error without id or received element:
    //  the sender won't be able to match the response
    XMLElement* recvStanza = XMLElement::getXml(msg,true);
    if (id || recvStanza) {
	XMLElement* stanza = XMPPUtils::createIq(XMPPUtils::IqError,to,from,id);
	// Add the first child of the received element
	stanza->addChild(recvStanza->removeChild());
	stanza->addChild(XMPPUtils::createError(XMPPError::TypeModify,XMPPError::SFeatureNotImpl));
	stream->sendStanza(stanza);
    }
    TelEngine::destruct(recvStanza);
    TelEngine::destruct(stream);
    // Return true to make sure nobody will respond again!!!
    return true;
}

/**
 * YJGDriver
 */
String YJGDriver::s_statusCmd[StatusCmdCount] = {"streams"};

YJGDriver::YJGDriver()
    : Driver("jingle","varchans"), m_init(false), m_singleTone(true), m_installIq(true),
    m_imToChanText(false), m_ftProxy(0)
{
    Output("Loaded module YJingle");
    m_statusCmd << "status " << name();
    Engine::extraPath("jingle");
}

YJGDriver::~YJGDriver()
{
    Output("Unloading module YJingle");
    TelEngine::destruct(s_jingle);
    TelEngine::destruct(s_message);
    TelEngine::destruct(s_presence);
    TelEngine::destruct(s_clientPresence);
    TelEngine::destruct(s_stream);
    TelEngine::destruct(s_iqService);
    TelEngine::destruct(s_jabber);
}

void YJGDriver::initialize()
{
    Output("Initializing module YJingle");
    s_cfg = Engine::configFile("yjinglechan");
    s_cfg.load();
    NamedList dummy("");
    NamedList* sect = s_cfg.getSection("general");
    if (!sect)
	sect = &dummy;

    if (!m_init) {
	m_init = true;

	// Init all known codecs
	s_knownCodecs.add("0",  "PCMU",    "8000",  "1", "mulaw");
	s_knownCodecs.add("2",  "G726-32", "8000",  "1", "g726");
	s_knownCodecs.add("3",  "GSM",     "8000",  "1", "gsm");
	s_knownCodecs.add("4",  "G723",    "8000",  "1", "g723");
	s_knownCodecs.add("7",  "LPC",     "8000",  "1", "lpc10");
	s_knownCodecs.add("8",  "PCMA",    "8000",  "1", "alaw");
	s_knownCodecs.add("9",  "G722",    "8000",  "1", "g722");
	s_knownCodecs.add("11", "L16",     "8000",  "1", "slin");
	s_knownCodecs.add("15", "G728",    "8000",  "1", "g728");
	s_knownCodecs.add("18", "G729",    "8000",  "1", "g729");
	s_knownCodecs.add("31", "H261",    "90000", "1", "h261");
	s_knownCodecs.add("32", "MPV",     "90000", "1", "mpv");
	s_knownCodecs.add("34", "H263",    "90000", "1", "h263");
	s_knownCodecs.add("98", "iLBC",    "8000",  "1", "ilbc");
	s_knownCodecs.add("98", "iLBC",    "8000",  "1", "ilbc20");
	s_knownCodecs.add("98", "iLBC",    "8000",  "1", "ilbc30");

	// Jabber protocol to use
	JBEngine::Protocol proto = (Engine::mode() == Engine::Client) ?
	    JBEngine::Client : JBEngine::Component;
	NamedString* p = sect->getParam("protocol");
	if (p)
	    proto = (JBEngine::Protocol)JBEngine::lookupProto(*p,proto);

	if (proto == JBEngine::Client)
	    m_installIq = true;
	else
	    m_installIq = sect->getBoolValue("installiq",true);

	// Create Jabber engine and services
	s_jabber = new YJBEngine(proto);
	s_jingle = new YJGEngine(s_jabber,0);
	s_message = new YJBMessage(s_jabber,1);
        s_stream = new YJBStreamService(s_jabber,0);
	// Create protocol dependent services
	// Don't create presence service for client protocol: presence is kept by client streams
	// Instantiate event handler for messages related to presence when running in client mode
	if (s_jabber->protocol() != JBEngine::Client)
	    s_presence = new YJBPresence(s_jabber,0);
	else
	    s_clientPresence = new YJBClientPresence(s_jabber,0);
	if (m_installIq)
	    s_iqService = new YJBIqService(s_jabber,100);

	// Attach services to the engine
	s_jabber->attachService(s_jingle,JBEngine::ServiceJingle);
	s_jabber->attachService(s_jingle,JBEngine::ServiceWriteFail);
	s_jabber->attachService(s_jingle,JBEngine::ServiceIq);
	s_jabber->attachService(s_jingle,JBEngine::ServiceStream);
	s_jabber->attachService(s_message,JBEngine::ServiceMessage);
	if (s_presence) {
	    s_jabber->attachService(s_presence,JBEngine::ServicePresence);
	    s_jabber->attachService(s_presence,JBEngine::ServiceDisco);
	}
	else if (s_clientPresence) {
	    s_jabber->attachService(s_clientPresence,JBEngine::ServicePresence);
	    s_jabber->attachService(s_clientPresence,JBEngine::ServiceRoster);
	}
	if (s_stream)
	    s_jabber->attachService(s_stream,JBEngine::ServiceStream);
	if (s_iqService) {
	    s_jabber->attachService(s_iqService,JBEngine::ServiceIq);
	    s_jabber->attachService(s_iqService,JBEngine::ServiceCommand);
	    s_jabber->attachService(s_iqService,JBEngine::ServiceDisco);
	    s_jabber->attachService(s_iqService,JBEngine::ServiceWriteFail);
	}

	// Driver setup
	installRelay(Halt);
	installRelay(Route);
	installRelay(Update);
	installRelay(Transfer);
	installRelay(ImExecute);
	installRelay(Progress);
	installRelay(ChanNotify,"chan.notify",100);
	Engine::install(new ResNotifyHandler);
	Engine::install(new ResSubscribeHandler);
	Engine::install(new XmppGenerateHandler);
	if (s_jabber->protocol() == JBEngine::Client)
	    Engine::install(new UserLoginHandler);
	if (m_installIq)
	    Engine::install(new XmppIqHandler);
	setup();
    }

    lock();

    // Initialize Jabber engine and services
    s_jabber->initialize();
    s_jingle->initialize();
    s_message->initialize();
    if (s_presence)
	s_presence->initialize();
    if (s_stream)
	s_stream->initialize();

    m_singleTone = sect->getBoolValue("singletone",true);
    s_localAddress = sect->getValue("localip");
    s_anonymousCaller = sect->getValue("anonymous_caller","unk_caller");
    s_pendingTimeout = sect->getIntValue("pending_timeout",10000);
    m_imToChanText = sect->getBoolValue("imtochantext",false);
    s_attachPresToCmd = sect->getBoolValue("addpresencetocommand",false);
    s_userRoster = sect->getBoolValue("user.roster",false);
    s_useCrypto = sect->getBoolValue("secure_rtp",false);
    s_cryptoMandatory = s_useCrypto;

    // Init codecs in use. Check each codec in known codecs list against the configuration
    s_usedCodecs.clear();
    bool defcodecs = s_cfg.getBoolValue("codecs","default",true);
    for (ObjList* o = s_knownCodecs.skipNull(); o; o = o->skipNext()) {
	JGRtpMedia* crt = static_cast<JGRtpMedia*>(o->get());
	bool enable = defcodecs && DataTranslator::canConvert(crt->m_synonym);
	if (s_cfg.getBoolValue("codecs",crt->m_synonym,enable))
	    s_usedCodecs.append(new JGRtpMedia(*crt));
    }

    TelEngine::destruct(m_ftProxy);
    const char* ftJid = sect->getValue("socks_proxy_jid");
    if (!null(ftJid)) {
	const char* ftAddr = sect->getValue("socks_proxy_ip");
	int ftPort = sect->getIntValue("socks_proxy_port",-1);
	if (!(null(ftAddr) || ftPort < 1))
	    m_ftProxy = new JGStreamHost(ftJid,ftAddr,ftPort);
	else
	    Debug(this,DebugNote,
		"Invalid addr/port (%s:%s) for default file transfer proxy",
		sect->getValue("socks_proxy_ip"),sect->getValue("socks_proxy_port"));
    }

    int dbg = DebugInfo;
    if (!s_localAddress)
	dbg = DebugNote;
    if (!s_usedCodecs.count())
	dbg = DebugWarn;

    if (debugAt(dbg)) {
	String s;
	s << " localip=" << s_localAddress ? s_localAddress.c_str() : "MISSING";
	s << " singletone=" << String::boolText(m_singleTone);
	s << " pending_timeout=" << s_pendingTimeout;
	s << " anonymous_caller=" << s_anonymousCaller;
	String media;
	if (!s_usedCodecs.createList(media,true))
	    media = "MISSING";
	s << " codecs=" << media;
	if (m_ftProxy)
	    s << " socks_proxy=" << m_ftProxy->c_str() << ":" <<
		m_ftProxy->m_address.c_str() << ":" << m_ftProxy->m_port;
	Debug(this,dbg,"Module initialized:%s",s.c_str());
    }

    unlock();
}

// Check if we have an existing stream (account)
bool YJGDriver::hasLine(const String& line) const
{
    JBStream* stream = (line && s_jabber) ? s_jabber->findStream(line) : 0;
    if (stream)
	stream->deref();
    return 0 != stream;
}

// Make an outgoing calls
// Build peers' JIDs and check if the destination is available
bool YJGDriver::msgExecute(Message& msg, String& dest)
{
    // Construct JIDs
    JabberID caller;
    JabberID called;
    bool available = true;
    String error;
    const char* errStr = "failure";
    bool sendSub = false;
    while (true) {
	if (!msg.userData()) {
	    error = "No data channel";
	    break;
	}
	// Component: delay check
	// Client: just check if caller/called are full JIDs
	if (s_jabber->protocol() == JBEngine::Component)
	    break;
	// Check if a stream exists. Try to get a resource for caller and/or called
	JBStream* stream = 0;
	NamedString* account = msg.getParam("line");
	if (account)
	    stream = s_jabber->findStream(*account);
	if (stream)
	    caller.set(stream->local().node(),stream->local().domain(),
		stream->local().resource());
	else {
	    caller.set(msg.getValue("caller"));
	    stream = s_jabber->getStream(&caller,false);
	}
	if (!(stream && stream->type() == JBEngine::Client)) {
	    error = "No stream";
	    TelEngine::destruct(stream);
	    break;
	}
	if (!caller.resource()) {
	    Debug(this,DebugAll,"Set resource '%s' for caller '%s'",
		stream->local().resource().c_str(),caller.c_str());
	    caller.resource(stream->local().resource());
	}
	called.set(dest);
	// Check if it's the same user
	if (caller.bare() &= called.bare()) {
	    if (!called.resource()) {
		XMPPUserRoster* roster = (static_cast<JBClientStream*>(stream))->roster();
		roster->ref();
		Lock2 lock(roster,&roster->resources());
		JIDResource* res = roster->resources().getAudio(true);
		if (res)
		    called.resource(res->name());
		lock.drop();
		TelEngine::destruct(roster);
	    }
	    if (!called.resource()) {
		error = "No resource available for called party";
		errStr = "offline";
	    }
	    else if (caller.resource() == called.resource())
		error = "Can't call the same resource";
	    break;
	}
	// No resource:
	// Check if we have it in the roster
	// Declare unavailable if the caller is subscribed to called's presence
	if (!(called.resource() ||
	    getClientTargetResource(static_cast<JBClientStream*>(stream),called,&sendSub) ||
	    sendSub)) {
	    error = "No resource available for called party";
	    errStr = "offline";
	}
	if (sendSub)
	    available = false;
	else if (error.null() && !(caller.isFull() && called.isFull()))
	    error << "Incomplete caller=" << caller << " or called=" << called;
	TelEngine::destruct(stream);
	break;
    }

    // Check if this is a file transfer
    String file;
    if (!error) {
	String* format = msg.getParam("format");
	if (format && *format == "data") {
	    // Check file. Remove path if present
	    file = msg.getValue("file_name");
	    int pos = file.rfind('/');
	    if (pos == -1)
		pos = file.rfind('\\');
	    if (pos != -1)
		file = file.substr(pos + 1);
	    if (file.null())
		error << "File transfer request with no file";
	}
    }

    if (error) {
	Debug(this,DebugNote,"Jingle call failed. %s",error.c_str());
	msg.setParam("error",errStr ? errStr : "noconn");
	return false;
    }

    // Component: prepare caller/called. check availability
    // Lock driver to prevent probe response to be processed before the channel
    //  is fully built
    Lock lock(this);
    if (s_jabber->protocol() == JBEngine::Component)
	setComponentCall(caller,called,msg.getValue("caller"),dest,available,error);
    if (error) {
	Debug(this,DebugNote,"Jingle call failed. %s",error.c_str());
	msg.setParam("error",errStr ? errStr : "noconn");
	return false;
    }
    Debug(this,DebugAll,
	"msgExecute. caller='%s' called='%s' available=%s filetransfer=%s",
	caller.c_str(),called.c_str(),String::boolText(available),
	String::boolText(!file.null()));
    // Send subscribe
    if (sendSub) {
	JBStream* stream = s_jabber->getStream(&caller,false);
	if (stream)
	    stream->sendStanza(JBPresence::createPresence(caller.bare(),called.bare(),
		JBPresence::Subscribe));
	TelEngine::destruct(stream);
    }
    YJGConnection* conn = new YJGConnection(msg,caller,called,available,file);
    bool ok = conn->state() != YJGConnection::Terminated;
    lock.drop();
    if (ok) {
	Channel* ch = static_cast<Channel*>(msg.userData());
	if (ch && conn->connect(ch,msg.getValue("reason"))) {
	    conn->callConnect(msg);
	    msg.setParam("peerid",conn->id());
	    msg.setParam("targetid",conn->id());
	}
    }
    else {
	Debug(this,DebugNote,"Jingle call failed to initialize. error=%s",
	    conn->reason().c_str());
	msg.setParam("error","failure");
    }
    TelEngine::destruct(conn);
    return ok;
}

// Send IM messages
bool YJGDriver::imExecute(Message& msg, String& dest)
{
    // Construct JIDs
    JabberID caller(msg.getValue("caller"));
    JabberID called(dest);
    String error;
    const char* errStr = "failure";
    JBStream* stream = 0;
    while (true) {
	// Component: prepare/check caller/called
	if (s_jabber->protocol() == JBEngine::Component) {
	    stream = s_jabber->getStream(0,false);
	    if (!stream)
		error = "No stream";
	    // Check caller:
	    // No node: use its domain part as node
	    if (!caller.node() && caller.domain()) {
	        String domain;
		if (!s_jabber->getServerIdentity(domain,!s_presence->autoRoster())) {
		    error = "No default server";
		    break;
		}
		String node = caller.domain();
		String res = caller.resource();
		caller.set(node,domain,res);
	    }
	    if (!caller.bare()) {
		error << "Invalid caller=" << caller;
		break;
	    }
	    if (!called) {
		error = "called is empty";
		break;
	    }
	    break;
	}
	// Check if a stream exists
	NamedString* account = msg.getParam("line");
	if (account)
	    stream = s_jabber->findStream(*account);
	if (!stream)
	    stream = s_jabber->getStream(&caller,false);
	if (!(stream && stream->type() == JBEngine::Client)) {
	    error = "No stream";
	    break;
	}
	// Reset caller
	caller.set("");
	// Caller must be at least bare JIDs
	if (!(called.node() && called.domain()))
	    error << "Incomplete called=" << called;
	break;
    }
    // Send the message
    if (!error) {
	const char* t = msg.getValue("xmpp_type",msg.getValue("type"));
	const char* id = msg.getValue("id");
	const char* stanzaId = msg.getValue("xmpp_id",id);
	JBMessage::MsgType type = JBMessage::msgType(t);
	XMLElement* im = 0;
	if (type == JBMessage::None) {
	    if (!t)
		im = JBMessage::createMessage(JBMessage::Chat,caller,called,stanzaId,0);
	    else
		im = JBMessage::createMessage(t,caller,called,stanzaId,0);
	}
        else
	    im = JBMessage::createMessage(type,caller,called,stanzaId,0);
	const char* subject = msg.getValue("subject");
	if (subject)
	    im->addChild(new XMLElement(XMLElement::Subject,0,subject));
	NamedString* b = msg.getParam("body");
	XMLElement* body = 0;
	if (b) {
	    body = new XMLElement(XMLElement::Body,0,*b);
	    NamedPointer* np = static_cast<NamedPointer*>(b->getObject("NamedPointer"));
	    MimeStringBody* sb = static_cast<MimeStringBody*>(np ? np->userObject("MimeStringBody") : 0);
	    if (sb) {
		String name = sb->getType();
		name.startSkip("text/",false);
		body->addChild(new XMLElement(name,0,sb->text()));
	    }
	}
	im->addChild(body);
	JBStream::Error result = stream->sendStanza(im,id);
	if (result == JBStream::ErrorContext || result == JBStream::ErrorNoSocket)
	    error = "Failed to send message";
    }
    TelEngine::destruct(stream);
    if (!error)
	return true;
    Debug(this,DebugNote,"Jabber message failed. %s",error.c_str());
    msg.setParam("error",errStr?errStr:"noconn");
    return false;
}

// Handle command complete requests
bool YJGDriver::commandComplete(Message& msg, const String& partLine,
	const String& partWord)
{
    bool status = partLine.startsWith("status");
    bool drop = !status && partLine.startsWith("drop");
    if (!(status || drop))
	return Driver::commandComplete(msg,partLine,partWord);

    // 'status' command
    Lock lock(this);
    // line='status jingle': add additional commands
    if (partLine == m_statusCmd) {
	for (unsigned int i = 0; i < StatusCmdCount; i++)
	    if (!partWord || s_statusCmd[i].startsWith(partWord))
		msg.retValue().append(s_statusCmd[i],"\t");
	return true;
    }

    if (partLine != "status" && partLine != "drop")
	return false;

    // Empty partial word or name start with it: add name and prefix
    if (!partWord || name().startsWith(partWord)) {
	msg.retValue().append(name(),"\t");
	if (channels().skipNull())
	    msg.retValue().append(prefix(),"\t");
	return false;
    }

    // Partial word starts with module prefix: add channels
    if (partWord.startsWith(prefix())) {
	for (ObjList* o = channels().skipNull(); o; o = o->skipNext()) {
	    CallEndpoint* c = static_cast<CallEndpoint*>(o->get());
	    if (c->id().startsWith(partWord))
		msg.retValue().append(c->id(),"\t");
	}
	return true;
    }
    return false;
}

// Check and build caller and called for Component run mode
// Caller: Set user if missing. Get default server identity for Yate Component
// Try to get an available resource for the called party
bool YJGDriver::setComponentCall(JabberID& caller, JabberID& called,
	const char* cr, const char* cd, bool& available, String& error)
{
    // Get identity for default server
    String domain;
    if (!s_jabber->getServerIdentity(domain,!s_presence->autoRoster())) {
	error = "No default server";
	return false;
    }
    if (!cr)
	cr = s_anonymousCaller;
    // Validate caller's JID
    if (!(cr && JabberID::valid(cr))) {
	error << "Invalid caller=" << cr;
	return false;
    }
    JabberID tmp(cr);
    if (tmp.node())
	caller.set(tmp.node(),domain,tmp.resource());
    else
	caller.set(tmp.domain(),domain,tmp.resource());
    called.set(cd);

    // Get an available resource for the remote user if we keep the roster
    // Send subscribe and probe if not
    if (s_presence->autoRoster()) {
	// Get remote user
	bool newPresence = false;
	XMPPUser* remote = s_presence->getRemoteUser(caller,called,true,0,
	    true,&newPresence);
	if (!remote) {
	    error = "Remote user is unavailable";
	    return false;
	}
	// Get a resource for the caller
	JIDResource* res = remote->getAudio(true,true);
	if (!res) {
	    s_presence->notifyNewUser(remote);
	    res = remote->getAudio(true,true);
	    // This should never happen !!!
	    if (!res) {
		TelEngine::destruct(remote);
		error = "Unable to get a resource for the caller";
		return false;
	    }
	}
	caller.resource(res->name());
	// Get a resource for the called
	res = remote->getAudio(false,true);
	available = (res != 0);
	if (!(newPresence || available)) {
	    if (!s_jingle->requestSubscribe()) {
		TelEngine::destruct(remote);
		error = "Remote peer is unavailable";
		return false;
	    }
	    remote->sendSubscribe(JBPresence::Subscribe,0);
	}
	if (available)
	    called.resource(res->name());
	else
	    if (!newPresence)
		remote->probe(0);
	TelEngine::destruct(remote);
    }
    else {
	available = false;
	// Get stream for default component
	JBStream* stream = s_jabber->getStream();
	if (!stream) {
	    error << "No stream for called=" << called;
	    return false;
	}
	if (!caller.resource())
	    caller.resource(s_jabber->defaultResource());
	// Send subscribe request and probe
	XMLElement* xml = 0;
	if (s_jingle->requestSubscribe()) {
	    xml = JBPresence::createPresence(caller.bare(),called.bare(),JBPresence::Subscribe);
	    stream->sendStanza(xml);
	}
	xml = JBPresence::createPresence(caller.bare(),called.bare(),JBPresence::Probe);
	stream->sendStanza(xml);
	TelEngine::destruct(stream);
    }
    return true;
}

// Message handler: Disconnect channels, destroy streams, clear rosters
bool YJGDriver::received(Message& msg, int id)
{
    // Execute: accept
    if (id == Execute) {
	String dest;
	if (getExecuteDest(msg,dest))
	    return msgExecute(msg,dest);
	return Driver::received(msg,Execute);
    }

    // Send message
    if (id == ImExecute) {
	String dest;
	if (getExecuteDest(msg,dest))
	    return imExecute(msg,dest);
	return Driver::received(msg,Execute);
    }

    if (id == Status) {
	String target = msg.getValue("module");
	// Target is the driver or channel
	if (!target || target == name() || target.startsWith(prefix()))
	    return Driver::received(msg,id);

	// Check additional commands
	if (!target.startSkip(name(),false))
	    return false;
	target.trimBlanks();
	int cmd = 0;
	for (; cmd < StatusCmdCount; cmd++)
	    if (s_statusCmd[cmd] == target)
		break;

	// Show streams
	if (cmd == StatusStreams && s_jabber) {
	    msg.retValue().clear();
	    msg.retValue() << "name=" << name();
	    msg.retValue() << ",type=" << type();
	    msg.retValue() << ",format=Account|State|Local|Remote";
	    s_jabber->lock();
	    msg.retValue() << ";count=" << s_jabber->streams().count();
	    for (ObjList* o = s_jabber->streams().skipNull(); o; o = o->skipNext()) {
		JBStream* stream = static_cast<JBStream*>(o->get());
		msg.retValue() << ";" << JBEngine::lookupProto(stream->type());
		msg.retValue() << "=" << stream->name();
		msg.retValue() << "|" << JBStream::lookupState(stream->state());
		msg.retValue() << "|" << stream->local();
		msg.retValue() << "|" << stream->remote();
	    }
	    s_jabber->unlock();
	    msg.retValue() << "\r\n";
	    return true;
	}
    }
    else if (id == ChanNotify) {
	String* module = msg.getParam("module");
	if (module && *module == name())
	    return false;
	String* chan = msg.getParam("notify");
	if (!chan)
	    return false;
	YJGConnection* ch = static_cast<YJGConnection*>(Driver::find(*chan));
	if (!ch)
	    return false;
	ch->processChanNotify(msg);
	if (ch->state() == YJGConnection::Terminated)
	    ch->disconnect(0);
	return true;
    }
    else if (id == Halt) {
	dropAll(msg);
	if (s_presence)
	    s_presence->cleanup();
	s_jabber->cleanup();
	s_jabber->cancelThreads();
	s_jingle->cancelThreads();
	if (s_presence)
	    s_presence->cancelThreads();
	s_jabber->detachService(s_presence);
	s_jabber->detachService(s_jingle);
	s_jabber->detachService(s_message);
	s_jabber->detachService(s_stream);
	s_jabber->detachService(s_clientPresence);
	s_jabber->detachService(s_iqService);
    }
    return Driver::received(msg,id);
}

bool YJGDriver::getJidFrom(JabberID& jid, Message& msg, bool checkDomain)
{
    String username = msg.getValue("username");
    if (username.null())
	return decodeJid(jid,msg,"from",checkDomain);
    String domain;
    s_jabber->getServerIdentity(domain,true);
    const char* res = msg.getValue("resource",s_jabber->defaultResource());
    jid.set(username,domain,res);
    return true;
}

bool YJGDriver::decodeJid(JabberID& jid, Message& msg, const char* param,
	bool checkDomain)
{
    jid.set(msg.getValue(param));
    if (jid.node().null() || jid.domain().null()) {
	Debug(this,DebugNote,"'%s'. Parameter '%s'='%s' is an invalid JID",
	    msg.c_str(),param,jid.c_str());
	return false;
    }
    if (checkDomain && !(s_presence && s_presence->validDomain(jid.domain()))) {
	Debug(this,DebugNote,"'%s'. Parameter '%s'='%s' has invalid (unknown) domain",
	    msg.c_str(),param,jid.c_str());
	return false;
    }
    return true;
}

XMLElement* YJGDriver::getPresenceCommand(JabberID& from, JabberID& to,
	bool available, XMLElement* presence)
{
    // Used only for debug purposes
    static int idCrt = 1;
    // Create 'x' child
    XMLElement* x = new XMLElement("x");
    x->setAttribute("xmlns","jabber:x:data");
    x->setAttribute("type","submit");
    // Field children of 'x' element
    XMLElement* field = new XMLElement("field");
    field->setAttribute("var","jid");
    XMLElement* value = new XMLElement("value",0,from);
    field->addChild(value);
    x->addChild(field);
    field = new XMLElement("field");
    field->setAttribute("var","available");
    value = new XMLElement("value",0,available ? "true" : "false");
    field->addChild(value);
    x->addChild(field);
    // 'command' stanza
    XMLElement* command = XMPPUtils::createElement(XMLElement::Command,XMPPNamespace::Command);
    command->setAttribute("node","USER_STATUS");
    command->addChild(x);
    // Add other children
    if (presence)
	command->addChild(presence);
    // 'iq' stanza
    String id = idCrt++;
    String domain;
    if (s_jabber->getServerIdentity(domain,false))
	from.domain(domain);
    XMLElement* iq = XMPPUtils::createIq(XMPPUtils::IqSet,from,to,id);
    iq->addChild(command);
    return iq;
}

void YJGDriver::processPresence(const JabberID& local, const JabberID& remote,
	bool available, bool audio)
{
    // Check if it is a brodcast and remote user has a resource
    bool broadcast = local.null();
    bool remoteRes = !remote.resource().null();
    DDebug(this,DebugAll,"Presence (%s). Local: '%s'. Remote: '%s'.",
	available?"available":"unavailable",local.c_str(),remote.c_str());
    // If a remote user became available notify only pending connections
    //   that match local bare jid and remote bare jid
    // No need to notify if remote user has no resource or no audio capability
    if (available) {
	if (!remoteRes || !audio)
	    return;
	lock();
	ObjList* obj = channels().skipNull();
	for (; obj; obj = obj->skipNext()) {
	    YJGConnection* conn = static_cast<YJGConnection*>(obj->get());
	    if (conn->state() != YJGConnection::Pending ||
		(!broadcast && (local.bare() |= conn->local().bare())) ||
		(remote.bare() |= conn->remote().bare()))
		continue;
	    conn->updateResource(remote.resource());
	    if (conn->presenceChanged(true))
		conn->disconnect(0);
	}
	unlock();
	return;
    }
    // Remote user is unavailable: notify all connections
    // Remote has no resource: match connections by bare jid
    lock();
    ObjList* obj = channels().skipNull();
    for (; obj; obj = obj->skipNext()) {
	YJGConnection* conn = static_cast<YJGConnection*>(obj->get());
	if ((!broadcast && local.bare() != conn->local().bare()) ||
	    !conn->remote().match(remote))
	    continue;
	if (conn->presenceChanged(false))
	    conn->disconnect(0);
    }
    unlock();
}

YJGConnection* YJGDriver::find(const JabberID& local, const JabberID& remote, bool anyResource)
{
    String bareJID = local.bare();
    if (bareJID == local)
	anyResource = true;
    Lock lock(this);
    ObjList* obj = channels().skipNull();
    for (; obj; obj = obj->skipNext()) {
	YJGConnection* conn = static_cast<YJGConnection*>(obj->get());
	if (!conn->remote().match(remote))
	    continue;
	if (anyResource) {
	    if (bareJID &= conn->local().bare())
		return conn;
	}
	else if (conn->local().match(local))
	    return conn;
    }
    return 0;
}

// Build an XML element from a received message
bool YJGDriver::addChildren(NamedList& msg, XMLElement* xml, ObjList* list)
{
    String prefix = msg.getValue("message-prefix");
    if (!(prefix && (xml || list)))
	return false;

    prefix << ".";
    bool added = false;
    for (unsigned int i = 1; i < 0xffffffff; i++) {
	String childPrefix(prefix + String(i));
	if (!msg.getValue(childPrefix))
	    break;
	XMLElement* child = new XMLElement(msg,childPrefix);
	if (xml)
	    xml->addChild(child);
	else
	    list->append(child);
	added = true;
    }
    return added;
}

// Get the destination from a call/im execute message
bool YJGDriver::getExecuteDest(Message& msg, String& dest)
{
    NamedString* callto = msg.getParam("callto");
    if (!callto)
	return false;
    int pos = callto->find('/');
    if (pos < 1)
	return false;
    dest = callto->substr(0,pos);
    if (!canHandleProtocol(dest))
	return false;
    dest = callto->substr(pos + 1);
    return true;
}

// Process a message received by a stream
void YJGDriver::processImMsg(JBEvent& event)
{
    DDebug(this,DebugInfo,"Message from=%s to=%s '%s'",
	event.from().c_str(),event.to().c_str(),event.text().c_str());

    if (!event.text())
	return;

    //JBMessage::MsgType type = JBMessage::msgType(event.stanzaType());

    Message* m = 0;
    YJGConnection* conn = 0;
    while (m_imToChanText) {
	conn = find(event.to().c_str(),event.from().c_str());
	if (!conn)
	    break;
	DDebug(this,DebugInfo,"Found conn=%p for message from=%s to=%s",
	    conn,event.from().c_str(),event.to().c_str());
	m = conn->message("chan.text");
	m->addParam("text",event.text());
	break;
    }
    if (!m) {
	m = new Message("msg.execute");
	m->addParam("caller",event.from());
	m->addParam("called",event.to());
	m->addParam("module",name());
	String billid;
	billid << Engine::runId() << "-" << Channel::allocId();
	m->addParam("billid",billid);
    }

    if (event.stream())
	m->addParam("account",event.stream()->name());

    // Fill the message
    if (event.id())
	m->addParam("id",event.id());
    if (event.stanzaType())
	m->addParam("type",event.stanzaType());
    XMLElement* xml = event.element();
    XMLElement* body = 0;
    if (xml) {
	XMLElement* e = xml->findFirstChild(XMLElement::Subject);
	if (e)
	    m->addParam("subject",e->getText());
	TelEngine::destruct(e);
	body = xml->findFirstChild(XMLElement::Body);
    }
    // FIXME: the body child may be repeated
    NamedPointer* p = new NamedPointer("body");
    if (body) {
	*p = body->getText();
	// FIXME: the body may have more then 1 child
	XMLElement* tmp = body->findFirstChild();
	if (tmp)
	    p->userData(new MimeStringBody("text/" + String(tmp->name()),tmp->getText()));
	TelEngine::destruct(tmp);
    }
    m->addParam(p);
    TelEngine::destruct(body);
    if (conn)
	Engine::enqueue(m);
    else {
	Engine::dispatch(*m);
	TelEngine::destruct(m);
    }
}

// Search a client's roster to get a resource
//  (with audio capabilities) for a subscribed user.
// Set noSub to true if false is returned and the client
//  is not subscribed to the remote user (or the remote user is not found).
// Return false if user or resource is not found
bool YJGDriver::getClientTargetResource(JBClientStream* stream,
    JabberID& target, bool* noSub)
{
    if (!stream)
	return false;
    XMPPUser* user = stream->getRemote(target);
    if (noSub)
	*noSub = (0 == user);
    if (!user)
	return false;
    user->lock();
    // Get an audio resource if available
    if (!target.resource()) {
	JIDResource* res = user->getAudio(false);
	if (res)
	    target.resource(res->name());
    }
    // No resource: check subscription to
    if (!target.resource() && noSub)
	*noSub = !user->subscription().to();
    user->unlock();
    TelEngine::destruct(user);
    return !target.resource().null();
}

// Find a channel by its sid
YJGConnection* YJGDriver::findBySid(const String& sid)
{
    if (!sid)
	return 0;
    Lock lock(this);
    for (ObjList* o = channels().skipNull(); o; o = o->skipNext()) {
	YJGConnection* conn = static_cast<YJGConnection*>(o->get());
	if (conn->isSid(sid))
	    return conn;
    }
    return 0;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
