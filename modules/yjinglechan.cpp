/**
 * yjinglechan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Jingle channel
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 * Author: Marian Podgoreanu
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


/*
============================================================================
TODO:
   Check SRTP handling. Check if secure (mandatory) is handled properly
============================================================================
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

class YJGEngine;                         // Jingle engine
class YJGEngineWorker;                   // Jingle engine worker
class YJGConnection;                     // Jingle channel
class YJGTransfer;                       // Transfer thread (route and execute)
class YJGMessageHandler;                 // Module message handlers
class YJGDriver;                         // The driver

// URI
#define BUILD_XMPP_URI(jid) (plugin.name() + ":" + jid)


/*
 * YJGEngine
 */
class YJGEngine : public JGEngine
{
public:
    // Send a session's stanza (dispatch a jabber.iq message)
    virtual bool sendStanza(JGSession* session, XmlElement*& stanza);
    // Event processor
    virtual void processEvent(JGEvent* event);
};

/*
 * YJGEngineWorker
 */
class YJGEngineWorker : public Thread
{
public:
    inline YJGEngineWorker(Thread::Priority prio = Thread::Normal)
	: Thread("YJGEngineWorker",prio)
	{}
    virtual void run();
};

/*
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
    // Ringing flags
    enum RingFlags {
	// Internal
	RingRinging = 0x01,              // call.ringing was handled
	RingGotEarlyMedia = 0x02,        // Gor early media from peer
	RingContentSent = 0x04,          // Ring content sent
	// Settable
	RingNone = 0x04,                 // Don't send ringing
	RingNoEarlySession = 0x10,       // Don't use early session content
	RingWithContent = 0x20,          // Attach session audio content if possible
	RingWithContentOnly = 0x40,      // Send ringing only if we have a content to sent
    };
    // Outgoing constructor
    YJGConnection(Message& msg, const char* caller, const char* called, bool available,
	const NamedList& caps, const char* file, const char* localip);
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
    // Check ring flag
    inline bool ringFlag(int mask) const
	{ return 0 != (m_ringFlags & mask); }
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
	return Channel::disconnect(m_reason,parameters());
    }
    // Route an incoming call
    bool route();
    // Process Jingle and Terminated events
    // Return false to terminate
    bool handleEvent(JGEvent* event);
    void hangup(const char* reason = 0, const char* text = 0,
	JGSession::Reason send = JGSession::ReasonUnknown);
    // Process remote user's presence changes.
    // Make the call if outgoing and in Pending (waiting for presence information) state
    // Hangup if the remote user is unavailbale
    // Return true to disconnect
    bool presenceChanged(bool available, NamedList* params = 0);
    // Process a transfer request
    // Return true if the event was accepted
    bool processTransferRequest(JGEvent* event);
    // Transfer terminated notification from transfer thread
    void transferTerminated(bool ok, const char* reason = 0);
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

    // Ring flags names
    static const TokenDict s_ringFlgName[];
    // Retrieve ringing flags from string
    // defVal: default value if flags list is empty
    static int getRinging(const String& flags, DebugEnabler* enabler, int defVal = 0);
    static inline int getRinging(NamedList& params, DebugEnabler* enabler, int defVal = 0)
	{ return getRinging(params[YSTRING("jingle_ring")],enabler,defVal); }

protected:
    // Process an ActContentAdd event
    void processActionContentAdd(JGEvent* event);
    // Process an ActContentAdd event
    void processActionTransportInfo(JGEvent* event);
    // Handle answer (session accept) events for non file transfer
    void processActionAccept(JGEvent* ev);
    // Handle stream hosts events
    // Return false if the session was terminated
    bool processStreamHosts(JGEvent* ev);
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
    // Send ringing if requested
    // Return false on error
    bool resetCurrentAudioContent(bool session, bool earlyMedia,
	bool sendTransInfo = true, JGSessionContent* newContent = 0, bool sendRing = true);
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
    bool matchMedia(JGSessionContent& local, JGSessionContent& recv,
	bool& firstChanged, bool& telEvChanged) const;
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
    bool changeFTHostDir(bool resetState = true);
    // Drop file transfer data. Remove the first host in list
    void dropFT(bool removeFirst);
    // Drop file transfer hosts
    void dropFTHosts(bool local, const char* reason = 0);
    // Drop file transfer host
    void dropFTHost(JGStreamHost* sh, ObjList* remove, const char* reason = 0);
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
    JGRtpCandidate* buildCandidate(bool nonP2P = true, bool rtp = true);
    // Get the first file transfer content
    inline JGSessionContent* firstFTContent() {
	    ObjList* o = m_ftContents.skipNull();
	    return o ? static_cast<JGSessionContent*>(o->get()) : 0;
	}

private:
    // Handle hold/active/mute actions
    // Confirm the received element
    void handleAudioInfoEvent(JGEvent* event);
    // Check jingle version override from call.execute or resource caps
    void overrideJingleVersion(const NamedList& list, bool caps);
    // Override session flags
    void overrideJingleFlags(const NamedList& list, const char* param);
    // Copy chan/session parameters to a destination list
    void copySessionParams(NamedList& list, bool redirect = true);
    // Check media for a received content
    bool checkMedia(const JGEvent& event, JGSessionContent& c);
    // Clear and reset data related to a given type: audio ...
    void resetEp(const String& what, bool releaseContent = true);
    // Hangup and drop the call if failed to setup encryption
    void dropNoCrypto();
    // Send ringing
    void sendRinging(NamedList* params = 0);

    Mutex m_mutex;                       // Lock transport and session
    State m_state;                       // Connection state
    JGSession* m_session;                // Jingle session attached to this connection
    bool m_rtpStarted;                   // RTP started flag
    bool m_acceptRelay;                  // Accept to replace with a relay candidate
    JGSession::Version m_sessVersion;    // Jingle session version
    int m_sessFlags;                     // Session flags
    int m_ringFlags;                     // Ring flags
    JabberID m_local;                    // Local user's JID
    JabberID m_remote;                   // Remote user's JID
    ObjList m_audioContents;             // The list of negotiated audio contents
    JGSessionContent* m_audioContent;    // The current audio content
    JGRtpMediaList m_audioFormats;       // Audio formats used by this channel
    String m_callerPrompt;               // Text to be sent to called before calling it
    String m_subject;                    // Connection subject
    String m_line;                       // Connection line
    String m_localip;                    // Local address
    bool m_offerRawTransport;            // Offer RAW transport on outgoing session
    bool m_offerIceTransport;            // Offer ICE transport on outgoing session
    bool m_offerP2PTransport;            // Offer P2P transport on outgoing session
    bool m_offerGRawTransport;           // Offer Google raw transport on outgoing session
    unsigned int m_redirectCount;        // Redirect counter
    int m_dtmfMeth;                      // Used DMTF method
    String m_rtpId;                      // Started RTP id
    // Crypto (for contents created by us)
    bool m_secure;                       // The channel is using crypto
    bool m_secureRequired;               // Crypto is mandatory
    // Termination
    bool m_hangup;                       // Hang up flag: True - already hung up
    String m_reason;                     // Hangup reason
    // Timeouts
    int64_t m_presTimeout;               // Maxcall after waiting for presence
    // Transfer
    bool m_transferring;                 // The call is already involved in a transfer
    String m_transferStanzaId;           // Sent transfer stanza id used to track the result
    JabberID m_transferTo;               // Transfer target
    JabberID m_transferFrom;             // Transfer source
    String m_transferSid;                // Session id for attended transfer
    XmlElement* m_recvTransferStanza;    // Received iq transfer element
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
    bool m_connSocksServer;              // Try to build a socks listener if not configured
};

/*
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

/*
 * Module message handlers
 */
class YJGMessageHandler : public MessageHandler
{
public:
    enum {
	JabberIq         = 50,           // handleJabberIq()
	ChanNotify       = -2,           // handleChanNotify()
	EngineStart      = -3,           // handleEngineStart()
	ResNotify        = -4,           // handleResNotify()
	ResSubscribe     = 10,           // handleResSubscribe()
	UserNotify       = -5,           // handleUserNotify()
    };
    YJGMessageHandler(int handler, int prio);
protected:
    virtual bool received(Message& msg);
private:
    int m_handler;
};

/*
 * YJGDriver
 */
class YJGDriver : public Driver
{
public:
    // Dtmf type
    enum DtmfType {
	DtmfUnknown = 0,
	DtmfRfc2833,                     // Send RFC 2833 tones
	DtmfInband,                      // Send inband tones
	DtmfJingle,                      // Use the jingle protocol
	DtmfChat                         // Send chat
    };
    YJGDriver();
    virtual ~YJGDriver();
    // Check if a message was sent by us
    inline bool isModule(Message& msg) {
	    String* module = msg.getParam("module");
	    return module && *module == name();
	}
    // Build a message to be sent by us
    inline Message* message(const char* msg) const {
	    Message* m = new Message(msg);
	    m->addParam("module",name());
	    return m;
	}
    // Add local ip to a list of parameters
    inline bool addLocalIp(NamedList& list) {
	    Lock lock(this);
	    if (!m_localAddress)
		return false;
	    list.addParam("localip",m_localAddress);
	    return true;
	}
    // Set local ip from a list of parameter or configured address
    inline void setLocalIp(String& addr, NamedList& list) {
	    Lock lock(this);
	    addr = list.getValue("localip",m_localAddress);
	}
    // Check if a domain is handled by the module
    inline bool handleDomain(const String& domain) {
	    Lock lock(this);
	    return m_domains.find(domain) != 0;
	}
    // Retrieve the default resource
    inline void defaultResource(String& buf) {
	    Lock lock(this);
	    ObjList* o = m_resources.skipNull();
	    if (o)
		buf = static_cast<String*>(o->get());
	}
    // Check if a resource can be handled by the module
    inline bool handleResource(const String& name) {
	    if (m_handleAllRes)
		return true;
	    Lock lock(this);
	    return !m_resources.skipNull() || m_resources.find(name);
	}
    // Inherited methods
    virtual void initialize();
    virtual bool hasLine(const String& line) const;
    virtual bool msgExecute(Message& msg, String& dest);
    // Message handler: Disconnect channels, destroy streams, clear rosters
    virtual bool received(Message& msg, int id);
    // Handle jabber.iq messages
    bool handleJabberIq(Message& msg);
    // Handle resource.notify messages
    bool handleResNotify(Message& msg);
    // Handle resource.subscribe messages
    bool handleResSubscribe(Message& msg);
    // Handle user.notify messages
    bool handleUserNotify(Message& msg);
    // Handle chan.notify messages
    bool handleChanNotify(Message& msg);
    // Handle msg.execute messages. Send chan.text if enabled
    bool handleImExecute(Message& msg);
    // Handle engine.start message
    void handleEngineStart(Message& msg);
    // Search a client's roster to get a resource
    //  (with audio capabilities) for a subscribed user.
    // Set noSub to true if false is returned and the client
    //  is not subscribed to the remote user (or the remote user is not found).
    // Return false if user or resource is not found
    bool getClientTargetResource(JBClientStream* stream, JabberID& target, bool* noSub = 0);
    // Find a channel by id. Return a referenced pointer
    inline YJGConnection* findChan(const String& id) {
	    Lock lock(this);
	    YJGConnection* ch = static_cast<YJGConnection*>(find(id));
	    return (ch && ch->ref()) ? ch : 0;
	}
    // Find a connection by local and remote jid, optionally ignore local
    // resource (always ignore if local has no resource)
    YJGConnection* findByJid(const JabberID& local, const JabberID& remote,
	bool anyResource = false);
    // Find a channel by its sid
    YJGConnection* findBySid(const String& sid);
    // Get a copy of the default file transfer proxy
    inline JGStreamHost* defFTProxy() {
	    Lock lock(this);
	    return m_ftProxy ? new JGStreamHost(*m_ftProxy) : 0;
	}
    // Notify presence
    void notifyPresence(const JabberID& from, const char* to, bool online);
    // Build and dispatch a 'jabber.account' message. Returns it on success
    Message* checkAccount(const String& line, bool query = false,
	const JabberID* contact = 0) const;
private:
    // Update the list of domains
    void setDomains(const String& list);

    bool m_init;
    String m_localAddress;               // The local machine's address
    String m_anonymousCaller;            // Caller username when missing
    JGStreamHost* m_ftProxy;             // Default file transfer proxy
    ObjList m_handlers;                  // Message handlers list
    ObjList m_domains;                   // Domains handled by the module
    bool m_handleAllRes;                 // Handle all resources (ignore the list)
    ObjList m_resources;                 // Resources handled by the module
    XMPPFeatureList m_features;          // Domain or resource features to advertise
    XmlElement* m_entityCaps;            // ntity capabilities element built from features
};


/*
 * Local data
 */
static Configuration s_cfg;                       // The configuration file
static JGRtpMediaList s_knownCodecs(JGRtpMediaList::Audio);  // List of all known codecs
static JGRtpMediaList s_usedCodecs(JGRtpMediaList::Audio);   // List of used audio codecs
static unsigned int s_pendingTimeout = 10000;     // Outgoing call pending timeout
static bool s_requestSubscribe = true;            // Request subscribe before making a non client
                                                  // call with target without resource
static bool s_autoSubscribe = false;              // Automatically respond to (un)subscribe requests
static bool s_imToChanText = false;               // Send received IM messages as chan.text if a channel is found
static bool s_singleTone = true;                  // Send single/batch DTMFs
static bool s_useCrypto = false;                  // Offer crypto on outgoing calls
static bool s_cryptoMandatory = false;            // Offer mandatory crypto on outgoing calls
static bool s_acceptRelay = false;
static bool s_offerRawTransport = true;           // Offer RAW UDP transport on outgoing sessions
static bool s_offerIceTransport = true;           // Offer ICE UDP transport on outgoing sessions
static bool s_offerP2PTransport = false;          // Offer P2P UDP transport on outgoing sessions
static bool s_offerGRawTransport = false;         // Offer Google RAW UDP transport on outgoing sessions
static int s_priority = 0;                        // Resource priority for presence generated by this module
static unsigned int s_redirectCount = 0;          // Redirect counter
static int s_dtmfMeth = YJGDriver::DtmfJingle;    // Default DTMF method to use
static bool s_clearFilePath = false;              // Clear file path when sending a file transfer
static JGSession::Version s_sessVersion = JGSession::VersionUnknown; // Default jingle session version for outgoing calls
static int s_ringFlags = 0;                       // Default channel ring flags
static String s_capsNode = "http://yate.null.ro/yate/jingle/caps"; // node for entity capabilities
static bool s_serverMode = true;                  // Server/client mode
static YJGEngine* s_jingle = 0;
static YJGDriver plugin;                          // The driver
static bool s_ilbcDefault30 = true;               // Default ilbc format when ptime is unknown (30 or 20)

// Channel ring flags
const TokenDict YJGConnection::s_ringFlgName[] = {
    {"none",                RingNone},
    {"noearlysession",      RingNoEarlySession},
    {"sessioncontent",      RingWithContent},
    {"sessioncontentonly",  RingWithContentOnly},
    {0,0}
};

// Message handlers installed by the module
static const TokenDict s_msgHandler[] = {
    {"jabber.iq",           YJGMessageHandler::JabberIq},
    {"chan.notify",         YJGMessageHandler::ChanNotify},
    {"engine.start",        YJGMessageHandler::EngineStart},
    {"resource.notify",     YJGMessageHandler::ResNotify},
    {"resource.subscribe",  YJGMessageHandler::ResSubscribe},
    {"user.notify",         YJGMessageHandler::UserNotify},
    {0,0}
};

// Error mapping
static TokenDict s_errMap[] = {
    {"normal",          JGSession::ReasonOk},
    {"normal-clearing", JGSession::ReasonOk},
    {"hangup",          JGSession::ReasonOk},
    {"busy",            JGSession::ReasonBusy},
    {"rejected",        JGSession::ReasonDecline},
    {"nomedia",         JGSession::ReasonMedia},
    {"cancelled",       JGSession::ReasonCancel},
    {"failure",         JGSession::ReasonGeneral},
    {"noroute",         JGSession::ReasonDecline},
    {"noconn",          JGSession::ReasonDecline},
    {"noauth",          JGSession::ReasonGeneral},
    {"nocall",          JGSession::ReasonGeneral},
    {"noanswer",        JGSession::ReasonGeneral},
    {"forbidden",       JGSession::ReasonGeneral},
    {"congestion",      JGSession::ReasonGeneral},
    {"looping",         JGSession::ReasonGeneral},
    {"shutdown",        JGSession::ReasonGone},
    {"notransport",     JGSession::ReasonTransport},
    {"offline",         JGSession::ReasonGone},
    {"gone",            JGSession::ReasonGone},
    {"shutdown",        JGSession::ReasonGone},
    {"timeout",         JGSession::ReasonExpired},
    {"timeout",         JGSession::ReasonTimeout},
    // Remote termination only
    {"failure",         JGSession::ReasonConn},
    {"failure",         JGSession::ReasonTransport},
    {"failure",         JGSession::ReasonApp},
    {"failure",         JGSession::ReasonAltSess},
    {"failure",         JGSession::ReasonConn},
    {"failure",         JGSession::ReasonFailApp},
    {"failure",         JGSession::ReasonFailTransport},
    {"failure",         JGSession::ReasonParams},
    {"failure",         JGSession::ReasonSecurity},
    // Non jingle reasons
    {"transferred",     JGSession::Transferred},
    {"crypto-required", JGSession::CryptoRequired},
    {"invalid-crypto",  JGSession::InvalidCrypto},
    {0,0}
};

// Error mapping
static const TokenDict s_dictDtmfMeth[] = {
    {"rfc2833",  YJGDriver::DtmfRfc2833},
    {"inband",   YJGDriver::DtmfInband},
    {"jingle",   YJGDriver::DtmfJingle},
    {"chat",     YJGDriver::DtmfChat},
    {0,0}
};


// Check if a payload name is telephone event one
static inline bool isTelEvent(const String& name)
{
    return (name &= "telephone-event") || (name &= "tone") ||
	(name &= "audio/telephone-event");
};

// Add a parameter to a list.
// Optionally add it to a copy params string
static inline void jingleAddParam(NamedList& list, const char* param, const char* value,
    String* copy, bool emptyOk = true)
{
    if (TelEngine::null(param))
	return;
    list.addParam(param,value,emptyOk);
    if (copy)
	copy->append(param,",");
}

// Add secure parameters from crypto
static void addSecure(NamedList& list, JGCrypto* crypto)
{
    if (!crypto)
	return;
    list.addParam("secure",String::boolText(true));
    list.addParam("crypto_suite",crypto->m_suite);
    list.addParam("crypto_key",crypto->m_keyParams);
    // TODO: add session params
}

// Replace 'ilbc' to used ilbc20/30
static void adjustUsedIlbc(String& fmts)
{
    if (!fmts)
	return;
    ObjList* list = fmts.split(',',false);
    ObjList* o = list->find("ilbc");
    if (o) {
	JGRtpMedia* m = 0;
	plugin.lock();
	for (ObjList* l = s_usedCodecs.skipNull(); l; l = l->skipNext()) {
	    m = static_cast<JGRtpMedia*>(l->get());
	    if (m->m_name == "iLBC")
		break;
	    m = 0;
	}
	if (m)
	    *(static_cast<String*>(o->get())) = m->m_synonym;
	else
	    o->remove();
	plugin.unlock();
	fmts.clear();
	fmts.append(list,",");
    }
    TelEngine::destruct(list);
}

#ifdef DEBUG
// Utility function needed for debug: dump a candidate to a string
static void dumpCandidate(String& buf, JGRtpCandidate* c, char sep = ' ')
{
    if (!c)
	return;
    buf << "name=" << *c;
    buf << sep << "addr=" << c->m_address;
    buf << sep << "port=" << c->m_port;
    buf << sep << "component=" << c->m_component;
    buf << sep << "generation=" << c->m_generation;
    buf << sep << "network=" << c->m_network;
    buf << sep << "priority=" << c->m_priority;
    buf << sep << "protocol=" << c->m_protocol;
    buf << sep << "type=" << c->m_type;
    JGRtpCandidateP2P* p2p = YOBJECT(JGRtpCandidateP2P,c);
    if (p2p) {
	buf << sep << "username=" << p2p->m_username;
	buf << sep << "password=" << p2p->m_password;
    }
}
#endif


/*
 * YJGEngine
 */
// Send a session's stanza (dispatch a jabber.iq message)
bool YJGEngine::sendStanza(JGSession* session, XmlElement*& stanza)
{
    if (!(session && stanza)) {
	TelEngine::destruct(stanza);
	return false;
    }
    bool iq = stanza->toString() == XMPPUtils::s_tag[XmlTag::Iq];
    if (!(iq || stanza->toString() == XMPPUtils::s_tag[XmlTag::Message])) {
	TelEngine::destruct(stanza);
	return false;
    }
    DDebug(this,DebugAll,"sendStanza() session=(%p,%s) stanza=(%p,%s)",
	session,session->sid().c_str(),stanza,stanza->tag());
    Message m(iq ? "jabber.iq" : "msg.execute");
    m.addParam("module",plugin.name());
    if (session->line())
	m.addParam("line",session->line());
    if (iq) {
	m.addParam("from",session->local().bare());
	m.addParam("to",session->remote().bare());
	m.addParam("from_instance",session->local().resource());
	m.addParam("to_instance",session->remote().resource());
    }
    else {
	m.addParam("caller",session->local().bare());
	m.addParam("called",session->remote().bare());
	m.addParam("caller_instance",session->local().resource());
	m.addParam("called_instance",session->remote().resource());
    }
    m.addParam(new NamedPointer("xml",stanza));
    return Engine::dispatch(m);
}

// Process jingle events
void YJGEngine::processEvent(JGEvent* event)
{
    if (!event)
	return;
    JGSession* session = event->session();
    // This should never happen !!!
    if (!session) {
	DDebug(this,DebugStub,"Received event without session");
	delete event;
	return;
    }
    plugin.lock();
    YJGConnection* conn = static_cast<YJGConnection*>(session->userData());
    if (conn && !conn->ref()) {
	plugin.unlock();
	delete event;
	return;
    }
    plugin.unlock();
    if (conn) {
	if (!conn->handleEvent(event) || event->final())
	    conn->disconnect(event->reason());
	TelEngine::destruct(conn);
    }
    else {
	if (event->type() == JGEvent::Jingle &&
	    event->action() == JGSession::ActInitiate) {
	    bool ok = plugin.canAccept(true);
	    if (ok && event->session()->ref()) {
		conn = new YJGConnection(event);
		conn->initChan();
		// Constructor failed ?
		if (conn->state() == YJGConnection::Pending)
		    TelEngine::destruct(conn);
		else if (!conn->route()) {
		    Lock lck(plugin);
		    event->session()->userData(0);
		}
	    }
	    else if (!ok) {
		Debug(&plugin,DebugWarn,"Refusing new Jingle call, full or exiting");
		event->session()->hangup(event->session()->createReason(JGSession::ReasonGeneral));
	    }
	    else {
		Debug(this,DebugWarn,"Session ref failed for new connection");
		event->session()->hangup(event->session()->createReason(JGSession::ReasonGeneral));
	    }
        }
	else {
	    DDebug(this,DebugAll,"Invalid (non initiate) event for new session");
	    event->confirmElement(XMPPError::Request,"Unknown session");
	}
    }
    delete event;
}


/*
 * YJGEngineWorker
 */
void YJGEngineWorker::run()
{
    Debug(&plugin,DebugAll,"%s start running",currentName());
    while (true) {
	if (Thread::check(false) || Engine::exiting())
	    break;
	JGEvent* ev = s_jingle->getEvent(Time::msecNow());
	if (ev)
	    s_jingle->processEvent(ev);
	else
	    Thread::idle(false);
    }
    Debug(&plugin,DebugAll,"%s stop running",currentName());
}


/*
 * YJGConnection
 */
// Outgoing call
YJGConnection::YJGConnection(Message& msg, const char* caller, const char* called,
    bool available, const NamedList& caps, const char* file, const char* localip)
    : Channel(&plugin,0,true),
    m_mutex(true,"YJGConnection"),
    m_state(Pending), m_session(0), m_rtpStarted(false), m_acceptRelay(s_acceptRelay),
    m_sessVersion(s_sessVersion), m_sessFlags(s_jingle->sessionFlags()),
    m_ringFlags(s_ringFlags),
    m_local(caller), m_remote(called), m_audioContent(0),
    m_audioFormats(JGRtpMediaList::Audio),
    m_callerPrompt(msg.getValue("callerprompt")),
    m_localip(localip),
    m_offerRawTransport(true), m_offerIceTransport(true),
    m_offerP2PTransport(false), m_offerGRawTransport(false),
    m_redirectCount(s_redirectCount), m_dtmfMeth(s_dtmfMeth),
    m_secure(s_useCrypto), m_secureRequired(s_cryptoMandatory),
    m_hangup(false), m_presTimeout(-1), m_transferring(false), m_recvTransferStanza(0),
    m_dataFlags(0), m_ftStatus(FTNone), m_ftHostDirection(FTHostNone),
    m_connSocksServer(msg.getBoolValue("socksserver",true))
{
    int redir = msg.getIntValue("redirectcount",m_redirectCount);
    m_redirectCount = (redir >= 0) ? redir : 0;
    m_dtmfMeth = msg.getIntValue("dtmfmethod",s_dictDtmfMeth,s_dtmfMeth);
    m_secure = msg.getBoolValue("secure",m_secure);
    m_secureRequired = msg.getBoolValue("secure_required",m_secureRequired);
    overrideJingleVersion(msg,false);
    if (available)
	overrideJingleVersion(caps,true);
    overrideJingleFlags(msg,"ojingle_flags");
    if (m_sessVersion != JGSession::Version0) {
	m_offerRawTransport = msg.getBoolValue("offerrawudp",s_offerRawTransport);
	m_offerIceTransport = msg.getBoolValue("offericeudp",s_offerIceTransport);
	m_offerP2PTransport = msg.getBoolValue("offerp2p",s_offerP2PTransport);
	m_offerGRawTransport = msg.getBoolValue("offergraw",s_offerGRawTransport);
    }
    else
	m_offerRawTransport = false;
    m_subject = msg.getValue("subject");
    m_line = msg.getValue("line");
    String uri = msg.getValue("diverteruri",msg.getValue("diverter"));
    // Skip protocol if present
    if (uri) {
	int pos = uri.find(':');
	m_transferFrom.set((pos >= 0) ? uri.substr(pos + 1) : uri);
    }
    // Get formats. Check if this is a file transfer session
    if (null(file)) {
	String audio = msg["formats"];
	plugin.lock();
	if (audio)
	    adjustUsedIlbc(audio);
	else if (!s_usedCodecs.createList(audio,true))
	    audio = "alaw,mulaw";
	m_audioFormats.setMedia(s_usedCodecs,audio);
	plugin.unlock();
    }
    else {
	m_secure = false;
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
    setMaxcall(msg);
    setMaxPDD(msg);
    if (!available) {
	u_int64_t timeNow = Time::now();
	// Save maxcall for later, set presence retrieval timeout instead
	m_presTimeout = maxcall() ? maxcall() - timeNow : 0;
	if (s_pendingTimeout)
	    maxcall(s_pendingTimeout * (u_int64_t)1000 + timeNow);
    }
    XDebug(this,DebugInfo,"Time: " FMT64 ". Maxcall set to " FMT64 " us. [%p]",
	Time::now(),maxcall(),this);
    // Startup
    Message* m = message("chan.startup",msg);
    m->setParam("direction",status());
    m_targetid = msg.getValue("id");
    m->copyParams(msg,"caller,callername,called,billid,callto,username");
    Engine::enqueue(m);
    // Make the call
    if (available)
	presenceChanged(true);
}

// Incoming call
YJGConnection::YJGConnection(JGEvent* event)
    : Channel(&plugin,0,false),
    m_mutex(true,"YJGConnection"),
    m_state(Active), m_session(event->session()), m_rtpStarted(false), m_acceptRelay(s_acceptRelay),
    m_sessVersion(event->session()->version()), m_sessFlags(s_jingle->sessionFlags()),
    m_ringFlags(s_ringFlags),
    m_local(event->session()->local()), m_remote(event->session()->remote()),
    m_audioContent(0),
    m_audioFormats(JGRtpMediaList::Audio),
    m_offerRawTransport(true), m_offerIceTransport(true),
    m_offerP2PTransport(false), m_offerGRawTransport(false),
    m_redirectCount(0), m_dtmfMeth(s_dtmfMeth),
    m_secure(s_useCrypto), m_secureRequired(s_cryptoMandatory),
    m_hangup(false), m_presTimeout(-1), m_transferring(false), m_recvTransferStanza(0),
    m_dataFlags(0), m_ftStatus(FTNone), m_ftHostDirection(FTHostNone),
    m_connSocksServer(false)
{
    m_line = m_session->line();
    plugin.lock();
    m_audioFormats.setMedia(s_usedCodecs);
    plugin.unlock();
    // Update local ip in non server mode
    if (!s_serverMode && m_line) {
	Message* m = plugin.checkAccount(m_line);
	if (m) {
	    m_localip = m->getValue("localip");
	    TelEngine::destruct(m);
	}
    }
    if (event->jingle()) {
        // Check if this call is transferred
	XmlElement* trans = XMPPUtils::findFirstChild(*event->jingle(),XmlTag::Transfer);
	if (trans)
	    m_transferFrom = trans->getAttribute("from");
	// Get subject
	m_subject = XMPPUtils::subject(*event->jingle());
    }
    Debug(this,DebugCall,"Incoming. caller='%s' called='%s'%s%s [%p]",
	m_remote.c_str(),m_local.c_str(),
	m_transferFrom ? ". Transferred from=" : "",
	m_transferFrom.safe(),this);
    // Set session
    m_session->userData(this);
    if (m_sessVersion == JGSession::Version0)
	m_offerRawTransport = false;
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
		case JGSessionContent::RtpP2P:
		case JGSessionContent::RtpGoogleRawUdp:
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
		    Debug(this,DebugNote,
			"Can't process incoming content '%s' of type %u [%p]",
			c->toString().c_str(),c->type(),this);
		    // Append this content to 'remove' list
		    // Let the list own it since we'll remove it from event's list
		    remove.append(c);
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
	m_secure = false;
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
	    Debug(this,DebugNote,"Denying file transfer in audio session [%p]",this);
	    m_session->sendContent(JGSession::ActContentRemove,m_ftContents);
	    m_ftContents.clear();
	}
	// Send transport accept now for version 0
	if (m_sessVersion == JGSession::Version0) {
	    ObjList* o = m_audioContents.skipNull();
	    if (o)
		m_session->sendContent(JGSession::ActTransportAccept,
		    static_cast<JGSessionContent*>(o->get()));
	}
    }
    else {
	m_state = Pending;
	setReason("failure");
	Debug(this,DebugNote,"%s [%p]",error,this);
	event->confirmElement(XMPPError::BadRequest,error);
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
    hangup();
    disconnected(true,m_reason);
    Debug(this,DebugCall,"Destroyed [%p]",this);
}

// Route an incoming call
bool YJGConnection::route()
{
    Message* m = message("call.preroute",false,true);
    m->addParam("username",m_remote.node());
    m->addParam("in_line",m_line,false);
    m->addParam("called",m_local.node());
    m->addParam("calleduri",BUILD_XMPP_URI(m_local));
    m->addParam("caller",m_remote.node());
    m->addParam("callername",m_remote.bare());
    m->addParam("calleruri",BUILD_XMPP_URI(m_remote));
    if (m_subject)
	m->addParam("subject",m_subject);
    m->addParam("jingle_version",JGSession::lookupVersion(m_sessVersion));
    String flags;
    JGEngine::encodeFlags(flags,m_sessFlags,JGSession::s_flagName);
    m->addParam("jingle_flags",flags,false);
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
    else {
	JGRtpMediaList* mList = 0;
	if (m_audioContent)
	    mList = &m_audioContent->m_rtpMedia;
	else {
	    ObjList* o = m_audioContents.skipNull();
	    if (o)
		mList = &static_cast<JGSessionContent*>(o->get())->m_rtpMedia;
	}
	if (!mList)
	    mList = &m_audioFormats;
	String formats;
	mList->createList(formats,true);
	m->addParam("formats",formats,false);
    }
    m_mutex.unlock();
    return startRouter(m);
}

// Call accepted
// Init RTP. Accept session and transport. Send transport
void YJGConnection::callAccept(Message& msg)
{
    Debug(this,DebugCall,"callAccept [%p]",this);
    m_secure = msg.getBoolValue("secure",m_secure);
    m_secureRequired = msg.getBoolValue("secure_required",m_secureRequired);
    m_dtmfMeth = msg.getIntValue("dtmfmethod",s_dictDtmfMeth,m_dtmfMeth);
    m_connSocksServer = msg.getBoolValue("isocksserver",true);
    overrideJingleFlags(msg,"jingle_flags");
    Channel::callAccept(msg);
    Lock lock(m_mutex);
    if (m_session)
	m_session->setFlags(m_sessFlags);
}

void YJGConnection::callRejected(const char* error, const char* reason,
	const Message* msg)
{
    Debug(this,DebugCall,"callRejected. error=%s reason=%s [%p]",error,reason,this);
    if (!reason)
	reason = "rejected";
    hangup(error,reason);
    Channel::callRejected(error,reason,msg);
}

bool YJGConnection::callRouted(Message& msg)
{
    DDebug(this,DebugCall,"callRouted [%p]",this);
    // Update ringing
    m_ringFlags = getRinging(msg,this,m_ringFlags);
    // Update formats
    const String& formats = msg[YSTRING("formats")];
    if (formats) {
	m_mutex.lock();
	m_audioFormats.filterMedia(formats);
	for (ObjList* o = m_audioContents.skipNull(); o; o = o->skipNext())
	    static_cast<JGSessionContent*>(o->get())->m_rtpMedia.filterMedia(formats);
	m_mutex.unlock();
    }
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
    Channel::msgProgress(msg);
    if (m_ftStatus != FTNone)
	return true;
    if (ringFlag(RingWithContent) && msg.getBoolValue("earlymedia",true) &&
	getPeer() && getPeer()->getSource()) {
	m_ringFlags |= RingRinging;
	sendRinging(&msg);
    }
    setEarlyMediaOut(msg);
    return true;
}

bool YJGConnection::msgRinging(Message& msg)
{
    DDebug(this,DebugInfo,"msgRinging [%p]",this);
    Channel::msgRinging(msg);
    if (m_ftStatus != FTNone)
	return true;
    m_ringFlags |= RingRinging;
    sendRinging(&msg);
    setEarlyMediaOut(msg);
    return true;
}

bool YJGConnection::msgAnswered(Message& msg)
{
    Debug(this,DebugCall,"msgAnswered [%p]",this);
    m_presTimeout = -1;
    if (m_ftStatus == FTNone) {
	m_mutex.lock();
	if (!m_audioContent || ((m_sessVersion != JGSession::Version0) && m_audioContent->isEarlyMedia()))
	    resetCurrentAudioContent(true,false,true);
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
    if (TelEngine::null(oper))
	return false;

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
	    XmlElement* hold = XMPPUtils::createElement(XmlTag::Hold,
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
	    XmlElement* active = XMPPUtils::createElement(XmlTag::Active,
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
    if (m_session)
	return s_jingle->sendMessage(m_session,text);
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
    int meth = msg.getIntValue("method",s_dictDtmfMeth,m_dtmfMeth);
    Lock lock(m_mutex);
    // Inband and RFC 2833 require an active local RTP stream
    if (meth == YJGDriver::DtmfInband) {
	if (m_rtpStarted && dtmfInband(tone))
	    return true;
    }
    else if (meth == YJGDriver::DtmfRfc2833) {
	if (m_rtpStarted) {
	    msg.setParam("targetid",m_rtpId);
	    return false;
	}
    }
    if (!m_session)
	return false;
    if (s_singleTone) {
	char s[2] = {0,0};
	while (*tone) {
	    s[0] = *tone++;
	    if (meth != YJGDriver::DtmfChat)
		m_session->sendDtmf(s);
	    else
		s_jingle->sendMessage(m_session,s);
	}
    }
    else if (meth != YJGDriver::DtmfChat)
	m_session->sendDtmf(tone);
    else
	s_jingle->sendMessage(m_session,tone);
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
	YJGConnection* conn = static_cast<YJGConnection*>(plugin.find(*chanId));
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
//	const JBStream* stream = m_session ? m_session->stream() : 0;
//	if (stream && stream->type() == JBEngine::Client)
//	    plugin.getClientTargetResource((JBClientStream*)stream,m_transferTo);
    }

    // Send the transfer request
    XmlElement* trans = m_session->buildTransfer(m_transferTo,
	m_transferSid ? m_session->local() : String::empty(),m_transferSid);
    const char* subject = msg.getValue("subject");
    if (!null(subject))
	trans->addChild(XMPPUtils::createSubject(subject));
    m_transferring = m_session->sendInfo(trans,&m_transferStanzaId);
    Debug(this,m_transferring?DebugCall:DebugNote,"%s transfer to=%s sid=%s [%p]",
	m_transferring ? "Sent" : "Failed to send",m_transferTo.c_str(),
	m_transferSid.c_str(),this);
    if (!m_transferring)
	m_transferStanzaId = "";
    return m_transferring;
}

// Hangup the call. Send session terminate if not already done
void YJGConnection::hangup(const char* reason, const char* text, JGSession::Reason send)
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
    JGSession* sess = 0;
    if (m_session) {
	int res = send;
	if (res == JGSession::ReasonUnknown)
	    res = lookup(m_reason,s_errMap,JGSession::ReasonUnknown);
	XmlElement* xml = 0;
	switch (res) {
	    case JGSession::CryptoRequired:
	    case JGSession::InvalidCrypto:
		xml = m_session->createReason(JGSession::ReasonSecurity,text,
		    m_session->createRtpSessionReason(res));
		break;
	    case JGSession::Transferred:
		xml = m_session->createReason(JGSession::ReasonOk,text,
		    m_session->createTransferReason(res));
		break;
	    case JGSession::ReasonUnknown:
		break;
	    default:
		xml = m_session->createReason(res,text);
	}
	m_session->hangup(xml);
	sess = m_session;
	m_session = 0;
    }
    Debug(this,DebugCall,"Hangup. reason=%s [%p]",m_reason.c_str(),this);
    lock.drop();
    if (sess) {
	plugin.lock();
	sess->userData(0);
	plugin.unlock();
	TelEngine::destruct(sess);
    }
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
	// Handle redirect
	if (isOutgoing() && event->reason() == "redirect" && event->text()) {
	    bool validCounter = false;
	    if (m_redirectCount) {
		m_redirectCount--;
		validCounter = true;
	    }
	    // Handle here XMPP targets
	    // Let the pbx deal with other targets
	    if (validCounter && event->text().startsWith("xmpp:",false)) {
		JabberID callto(event->text().substr(5));
		if (callto.bare()) {
		    if (callto == m_remote) {
			Debug(this,DebugNote,"Got redirect to the same remote party! [%p]",this);
			callto.clear();
		    }
		}
		else {
		    Debug(this,DebugNote,"Got redirect to incomplete jid=%s [%p]",
			event->text().c_str(),this);
		    callto.clear();
		}
		String id;
		if (callto && getPeerId(id)) {
		    Message m("chan.masquerade");
		    m.addParam("message","call.execute");
		    m.addParam("id",id);
		    m.addParam("callto",plugin.prefix() + callto);
		    m.addParam("caller",m_local,false);
		    copySessionParams(m);
		    Debug(this,DebugCall,"Redirecting to '%s' [%p]",callto.c_str(),this);
		    lock.drop();
		    Engine::dispatch(m);
		}
	    }
	    else {
		URI uri(event->text());
		paramMutex().lock();
		parameters().clearParams();
		parameters().addParam("called",uri.getUser());
		parameters().addParam("calledname",uri.getDescription(),false);
		parameters().addParam("calleduri",event->text());
		parameters().addParam("copyparams","");
		copySessionParams(parameters());
		paramMutex().unlock();
	    }
	}
	const char* reason = event->reason();
	Debug(this,DebugInfo,
	    "Session terminated with reason='%s' text='%s' [%p]",
	    reason,event->text().c_str(),this);
	if (!TelEngine::null(reason)) {
	    int jingleReason = lookup(reason,JGSession::s_reasons,JGSession::ReasonGeneral);
	    setReason(lookup(jingleReason,s_errMap,reason));
	}
	return false;
    }

    bool response = false;
    switch (event->type()) {
	case JGEvent::Jingle:
	    break;
	case JGEvent::ResultOk:
	case JGEvent::ResultError:
	case JGEvent::ResultTimeout:
	    response = true;
	    break;
	default:
	    DDebug(this,DebugStub,"Unhandled event (%p,%u) [%p]",
		event,event->type(),this);
	    return true;
    }

    // Process responses
    if (response) {
	XDebug(this,DebugAll,"Processing response event=%s id=%s [%p]",
	    event->name(),event->id().c_str(),this);

	bool rspOk = (event->type() == JGEvent::ResultOk);

	if (event->action() == JGSession::ActInitiate) {
	    if (m_ftStatus == FTNone) {
		// Non file transfer session
		// Notify ringing if initiate was confirmed and the remote party doesn't support it
		if (rspOk && !m_session->hasFeature(XMPPNamespace::JingleAppsRtpInfo)) {
		    status("ringing");
		    Engine::enqueue(message("call.ringing",false,true));
		}
	    }
	    else {
		// File transfer session
		// Send stream host
		if (rspOk) {
		    bool ok = false;
		    if (m_session) {
			m_session->buildSocksDstAddr(m_dstAddrDomain);
			ok = setupSocksFileTransfer(false);
			if (!ok) {
			    ok = (m_ftStatus != FTTerminated);
			    if (ok) {
				// Send empty host
				m_streamHosts.clear();
				m_session->sendStreamHosts(m_streamHosts,&m_ftStanzaId);
			    }
			}
		    }
		    if (!ok)
			hangup("noconn");
		    return ok;
		}
	    }
	}

	if (m_ftStanzaId && m_ftStanzaId == event->id()) {
	    m_ftStanzaId = "";
	    String usedHost;
	    bool ok = rspOk;
	    if (rspOk && event->element()) {
		XmlElement* query = XMPPUtils::findFirstChild(*event->element(),XmlTag::Query);
		if (query) {
		    XmlElement* used = XMPPUtils::findFirstChild(*query,XmlTag::StreamHostUsed);
		    if (used)
			usedHost = used->getAttribute("jid");
		}
	    }
	    if (!ok) {
		dropFT(true);
		// Result error: continue if we still can receive hosts
		if (event->type() == JGEvent::ResultError && isOutgoing()) {
		    if (changeFTHostDir()) {
			ok = true;
			m_ftStatus = FTIdle;
		    }
		}
	    }
	    Debug(this,rspOk ? DebugAll : DebugInfo,
		"Received result=%s to streamhost used=%s [%p]",
		event->name(),usedHost.c_str(),this);
	    return ok;
	}

	// Hold/active result
	bool hold = (m_onHoldOutId && m_onHoldOutId == event->id());
	if (hold || (m_activeOutId && m_activeOutId == event->id())) {
	    Debug(this,rspOk ? DebugAll : DebugInfo,
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
		Debug(this,DebugNote,"Transfer failed error=%s [%p]",
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
	    Debug(this,DebugAll,"Received dtmf(%s) '%s' [%p]",
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
		event->confirmElement(XMPPError::Request);
	    break;
	case JGSession::ActTransportAccept:
	    // TODO: handle it when (if) we'll send transport-replace
	    event->confirmElement(XMPPError::Request);
	    break;
	case JGSession::ActTransportReject:
	    // TODO: handle it when (if) we'll send transport-replace
	    event->confirmElement(XMPPError::Request);
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
		event->confirmElement(XMPPError::Request);
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
		event->confirmElement(XMPPError::Request);
	    break;
	case JGSession::ActContentModify:
	    // This event should modify the content 'senders' attribute
	    Debug(this,DebugInfo,"Denying event(%s) [%p]",event->actionName(),this);
	    event->confirmElement(XMPPError::NotAllowed);
	    break;
	case JGSession::ActContentReject:
	    if (m_ftStatus != FTNone) {
		event->confirmElement(XMPPError::Request);
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
	    if (!isAnswered()) {
		if (m_ftStatus != FTNone)
		    return setupSocksFileTransfer(true);
		processActionAccept(event);
	    }
	    break;
	case JGSession::ActTransfer:
	    if (m_ftStatus == FTNone)
		processTransferRequest(event);
	    else
		event->confirmElement(XMPPError::Request);
	    break;
	case JGSession::ActRinging:
	    if (m_ftStatus == FTNone) {
		event->confirmElement();
		status("ringing");
		Engine::enqueue(message("call.ringing",false,true));
	    }
	    else
		event->confirmElement(XMPPError::Request);
	    break;
	case JGSession::ActHold:
	case JGSession::ActActive:
	case JGSession::ActMute:
	    if (m_ftStatus == FTNone)
		handleAudioInfoEvent(event);
	    else
		event->confirmElement(XMPPError::Request);
	    break;
	case JGSession::ActTrying:
	case JGSession::ActReceived:
	    if (m_ftStatus == FTNone) {
		event->confirmElement();
		Debug(this,DebugAll,"Received Jingle event (%p) with action=%s [%p]",
		    event,event->actionName(),this);
	    }
	    else
		event->confirmElement(XMPPError::Request);
	    break;
	case JGSession::ActStreamHost:
	    return processStreamHosts(event);
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
bool YJGConnection::presenceChanged(bool available, NamedList* params)
{
    Lock lock(m_mutex);
    if (m_state == Terminated)
	return false;
    if (m_presTimeout > 0)
	maxcall(m_presTimeout + Time::now());
    else if (m_presTimeout)
	maxcall(0);
    m_presTimeout = -1;
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

    bool ok = true;
    if (params) {
	// Check for required audio or file transfer
	if (m_ftStatus == FTNone)
	    ok = params->getBoolValue("caps.audio");
	else
	    ok = params->getBoolValue("caps.filetransfer");
    }
    if (!ok)
	return false;
    // Check for jingle version override
    if (params)
	overrideJingleVersion(*params,true);

    // Make the call
    Debug(this,DebugCall,"Calling. caller=%s called=%s [%p]",
	m_local.c_str(),m_remote.c_str(),this);
    m_state = Active;
    if (m_ftStatus == FTNone) {
	XmlElement* transfer = 0;
	if (m_transferFrom)
	    transfer = JGSession::buildTransfer(String::empty(),m_transferFrom);
	if (m_offerRawTransport)
	    addContent(true,buildAudioContent(JGRtpCandidates::RtpRawUdp));
	if (m_offerIceTransport)
	    addContent(true,buildAudioContent(JGRtpCandidates::RtpIceUdp));
	if (m_offerP2PTransport)
	    addContent(true,buildAudioContent(JGRtpCandidates::RtpP2P));
	if (m_offerGRawTransport)
	    addContent(true,buildAudioContent(JGRtpCandidates::RtpGoogleRawUdp));
	m_session = s_jingle->call(m_sessVersion,m_local,m_remote,m_audioContents,transfer,
	    m_callerPrompt,m_subject,m_line,&m_sessFlags);
	// Init now the transport for version 0
	if (m_session && m_session->version() == JGSession::Version0)
	    resetCurrentAudioContent(true,false);
    }
    else
	m_session = s_jingle->call(m_sessVersion,m_local,m_remote,m_ftContents,0,
	    m_callerPrompt,m_subject,m_line);
    if (!m_session) {
	hangup("noconn");
	return true;
    }
    m_session->userData(this);
    return false;
}

// Process a transfer request
bool YJGConnection::processTransferRequest(JGEvent* event)
{
    Lock lock(m_mutex);
    // Check if we can accept a transfer and if it is a valid request
    XmlElement* trans = 0;
    const char* reason = 0;
    XMPPError::Type error = XMPPError::BadRequest;
    while (true) {
	if (!canTransfer()) {
	    error = XMPPError::Request;
	    reason = "Unacceptable in current state";
	    break;
	}
	trans = event->jingle() ? XMPPUtils::findFirstChild(*event->jingle(),XmlTag::Transfer) : 0;
	if (!trans) {
	    reason = "Transfer element is misssing";
	    break;
	}
	m_transferTo = trans->getAttribute("to");
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
	m_transferFrom = trans->getAttribute("from");
	break;
    }
    String subject;
    if (!reason && trans)
	subject = XMPPUtils::subject(*trans);

    if (!reason) {
	TelEngine::destruct(m_recvTransferStanza);
	m_recvTransferStanza = event->releaseXml();
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
	if (ok)
	    m_session->confirmResult(m_recvTransferStanza);
	else
	    m_session->confirmError(m_recvTransferStanza,XMPPError::UndefinedCondition,
		reason,XMPPError::TypeCancel);
	TelEngine::destruct(m_recvTransferStanza);
    }
    // Reset transfer data
    TelEngine::destruct(m_recvTransferStanza);
    m_transferring = false;
    m_transferStanzaId = "";
    m_transferTo.set("");
    m_transferFrom.set("");
    m_transferSid = "";
}

int YJGConnection::getRinging(const String& flags, DebugEnabler* enabler, int defVal)
{
    if (flags)
	defVal = XMPPUtils::decodeFlags(flags,s_ringFlgName);
    // Set RingNoEarlySession if RingWithContent
    // Reset RingWithContentOnly if RingWithContent is not set
    if (0 != (defVal & RingWithContent))
	defVal |= RingNoEarlySession;
    else
	defVal &= ~RingWithContentOnly;
#ifdef DEBUG
    String tmp;
    XMPPUtils::buildFlags(tmp,defVal,s_ringFlgName);
    DDebug(enabler,DebugAll,"Got ring flags 0x%x '%s' from '%s'",defVal,tmp.safe(),flags.safe());
#endif
    return defVal;
}

// Process an ActContentAdd event
void YJGConnection::processActionContentAdd(JGEvent* event)
{
    if (!event)
	return;

    ObjList ok;
    ObjList remove;
    if (!processContentAdd(*event,ok,remove)) {
	event->confirmElement(XMPPError::Conflict,"Duplicate content(s)");
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
    DDebug(this,DebugAll,"processActionTransportInfo() event=%s' [%p]",
	event->actionName(),this);
    bool ok = m_sessVersion != JGSession::Version0;
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
        // Update transport(s)
	bool changed = updateCandidate(1,*cc,*c);
	// Version0: the session will give us only 1 content
	if (!changed && m_sessVersion == JGSession::Version0) {
	    ok = false;
	    break;
	}
	ok = true;
	// Update credentials for ICE-UDP
	cc->m_rtpRemoteCandidates.m_password = c->m_rtpRemoteCandidates.m_password;
	cc->m_rtpRemoteCandidates.m_ufrag = c->m_rtpRemoteCandidates.m_ufrag;
	// Check RTCP
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
    XDebug(this,DebugAll,
	"processActionTransportInfo() event=%s' start=%u crtAudiocontent=%p newContent=%p [%p]",
	event->actionName(),startAudioContent,m_audioContent,newContent,this);
    if (ok) {
	event->confirmElement();
	if (newContent) {
	    if ((m_rtpStarted || m_audioContent || m_rtpId.null()) && !dataFlags(OnHold))
		resetCurrentAudioContent(isAnswered(),!isAnswered(),true,newContent);
	}
	else if ((startAudioContent && !startRtp()) || !(m_audioContent || dataFlags(OnHold)))
	    resetCurrentAudioContent(isAnswered(),!isAnswered());
	else if (!isAnswered())
	    sendRinging();
    }
    else
	event->confirmElement(XMPPError::NotAcceptable);
    enqueueCallProgress();
}

// Handle answer (session accept) event for non file transfer
void YJGConnection::processActionAccept(JGEvent* event)
{
    // Update media
    Debug(this,DebugCall,"Remote peer answered the call [%p]",this);
    m_presTimeout = -1;
    m_state = Active;
    status("answered");
    for (ObjList* o = event->m_contents.skipNull(); o; o = o->skipNext()) {
	JGSessionContent* recv = static_cast<JGSessionContent*>(o->get());
	// Ignore non session contents
	if (!recv->isSession())
	    continue;
	JGSessionContent* c = findContent(*recv,m_audioContents);
	if (!c)
	    continue;
	// Update credentials for ICE-UDP
	// only if not version 0 (this version only sends media in accept)
	if (m_sessVersion != JGSession::Version0) {
	    c->m_rtpRemoteCandidates.m_password = recv->m_rtpRemoteCandidates.m_password;
	    c->m_rtpRemoteCandidates.m_ufrag = recv->m_rtpRemoteCandidates.m_ufrag;
	}
	// Update media
	bool mediaChanged = false;
	bool telEvChanged = false;
	if (!(checkMedia(*event,*recv) &&
	    matchMedia(*c,*recv,mediaChanged,telEvChanged))) {
	    if (c == m_audioContent)
		resetEp("audio");
	    continue;
	}
	c->m_rtpMedia.m_ready = true;
	// First media changed for current audio content
	// RTP don't support update: reset audio
	if (mediaChanged && c == m_audioContent)
	    resetEp("audio");
	// Update transport(s)
	bool changed = updateCandidate(1,*c,*recv);
	changed = updateCandidate(2,*c,*recv) || changed;
	if (!m_audioContent || (changed && c == m_audioContent))
	    resetCurrentAudioContent(true,false,true,c);
    }
    if (!m_audioContent)
	resetCurrentAudioContent(true,false,true);
    Engine::enqueue(message("call.answered",false,true));
}

// Handle stream hosts events
// Return false if the session was terminated
bool YJGConnection::processStreamHosts(JGEvent* ev)
{
    if (!ev)
	return true;
    if (m_ftStatus == FTNone) {
	ev->confirmElement(XMPPError::Request);
	return true;
    }
    // Check if allowed
    if (m_ftHostDirection != FTHostRemote) {
	// Check if we can change direction
	if (!changeFTHostDir(false)) {
	    ev->confirmElement(XMPPError::Request);
	    return true;
	}
	// Drop current FT host
	dropFT(true);
	// Remove local hosts
	dropFTHosts(true,"received remote host(s)");
	m_ftStatus = FTIdle;
    }
    // Check if we already received it
    if (m_ftStatus != FTIdle) {
	ev->confirmElement(XMPPError::Request);
	return true;
    }
    ev->setConfirmed();
    // Remember stanza id
    m_ftStanzaId = ev->id();
    // Copy hosts from event
    ObjList* o = ev->m_streamHosts.skipNull();
    while (o) {
	m_streamHosts.append(o->get());
	o->remove(false);
	o = o->skipNull();
    }
    if (setupSocksFileTransfer(false))
	return true;
    if (m_ftStanzaId) {
	m_session->sendStreamHostUsed("",m_ftStanzaId);
	m_ftStanzaId = "";
    }
    return setupSocksFileTransfer(false);
}

// Update a received candidate. Return true if changed
bool YJGConnection::updateCandidate(unsigned int component, JGSessionContent& local,
    JGSessionContent& recv)
{
    JGRtpCandidate* rtpRecv = recv.m_rtpRemoteCandidates.findByComponent(component);
    if (!rtpRecv)
	return false;
    JGRtpCandidate* rtp = local.m_rtpRemoteCandidates.findByComponent(component);
#ifdef DEBUG
    String s1;
    String s2;
    dumpCandidate(s1,rtpRecv);
    dumpCandidate(s2,rtp);
    Debug(this,DebugAll,"updateCandidate() recv: %s found: %s [%p]",
	s1.c_str(),s2.c_str(),this);
#endif
    // Version0 or p2p transport: check acceptable transport
    if (m_sessVersion == JGSession::Version0 ||
	local.type() == JGSessionContent::RtpP2P ||
	local.type() == JGSessionContent::RtpGoogleRawUdp) {
	// We only handle UDP based transports for now
	if (rtpRecv->m_protocol != "udp")
	    return false;
	// Only accept a relay as a second transport and only once
	if (m_acceptRelay && rtpRecv->m_type == "relay") {
	    m_acceptRelay = false;
	    if (rtp) {
		Debug(this,DebugNote,"Replacing remote transport type '%s' with a relay [%p]",
		    rtp->m_type.c_str(),this);
		local.m_rtpRemoteCandidates.remove(rtp);
		rtp = 0;
		if (local.m_rtpMedia.media() == JGRtpMediaList::Audio)
		    resetEp("audio",false);
	    }
	}
	// Any other transport type accepted only initially
	else if (rtp)
	    return false;
    }
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
    c->m_rtpMedia.m_ssrc = "";
    if (local)
	c->m_rtpRemoteCandidates.m_type = c->m_rtpLocalCandidates.m_type;
    else
	c->m_rtpLocalCandidates.m_type = c->m_rtpRemoteCandidates.m_type;
    if (c->m_rtpLocalCandidates.m_type == JGRtpCandidates::RtpIceUdp) {
	if (m_sessVersion != JGSession::Version0)
	    c->m_rtpLocalCandidates.generateIceAuth();
	else
	    c->m_rtpLocalCandidates.generateOldIceAuth();
    }
    String tmp;
#ifdef DEBUG
    JGRtpCandidate* rtp = local ? c->m_rtpLocalCandidates.findByComponent(1) :
	c->m_rtpRemoteCandidates.findByComponent(1);
    if (rtp) {
	tmp << " candidate: ";
	dumpCandidate(tmp,rtp);
    }
#endif
    Debug(this,DebugAll,"Added content='%s' type=%s initiator=%s%s [%p]",
	c->toString().c_str(),c->m_rtpLocalCandidates.typeName(),
	String::boolText(c->creator() == JGSessionContent::CreatorInitiator),tmp.safe(),this);
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
    if (m_rtpStarted || m_audioContent || dataFlags(OnHold)) {
	clearEndpoint();
	m_rtpId.clear();
	m_rtpStarted = false;
    }
    if (!m_audioContent)
	return;

    Debug(this,DebugAll,"Removing current audio content (%p,'%s') [%p]",
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
	    c->m_rtpMedia.m_cryptoRequired = m_audioContent->m_rtpMedia.m_cryptoRequired;
	    c->m_rtpMedia.setMedia(m_audioContent->m_rtpMedia);
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
    bool sendTransInfo, JGSessionContent* newContent, bool sendRing)
{
    DDebug(this,DebugAll,"Resetting current audio content (%s,%s,%s,%p,%s) [%p]",
	String::boolText(session),String::boolText(earlyMedia),
	String::boolText(sendTransInfo),newContent,String::boolText(sendRing),this);

    // Remove the current audio content
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
	// Reset ring content sent flag
	m_ringFlags &= ~RingContentSent;
	if (sendRing)
	    sendRinging();
	return startRtp();
    }

    return false;
}

// Start RTP for the given content
// For raw udp transports, sends a 'trying' session info
bool YJGConnection::startRtp()
{
    if (m_hangup)
	return false;
    if (!m_audioContent) {
	DDebug(this,DebugInfo,"Failed to start RTP: no audio content [%p]",this);
	return false;
    }

    if (m_sessVersion == JGSession::Version0 && m_rtpStarted)
	return true;

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
    m.setParam("direction",rtpDir(*m_audioContent));
    m.addParam("media","audio");
    m.addParam("getsession","true");
    ObjList* obj = m_audioContent->m_rtpMedia.skipNull();
    if (obj) {
	JGRtpMedia* media = static_cast<JGRtpMedia*>(obj->get());
	m.addParam("payload",media->m_id);
 	m.addParam("format",media->m_synonym);
    }
    m.addParam("evpayload",String(m_audioContent->m_rtpMedia.m_telEvent));
    m.addParam("localip",rtpLocal->m_address);
    m.addParam("localport",rtpLocal->m_port);
    m.addParam("remoteip",rtpRemote->m_address);
    m.addParam("remoteport",rtpRemote->m_port);
    //m.addParam("autoaddr","false");
    bool rtcp = (0 != m_audioContent->m_rtpLocalCandidates.findByComponent(2));
    m.addParam("rtcp",String::boolText(rtcp));
    // Crypto
    if (m_secure) {
	ObjList* cr = m_audioContent->m_rtpMedia.m_cryptoRemote.skipNull();
	if (cr && m_audioContent->m_rtpMedia.m_cryptoLocal.skipNull())
	    addSecure(m,static_cast<JGCrypto*>(cr->get()));
	else if (m_secureRequired) {
	    Debug(this,DebugNote,"No required crypto in current content [%p]",this);
	    dropNoCrypto();
	    return false;
	}
    }

    String oldPort = rtpLocal->m_port;

    if (!Engine::dispatch(m)) {
	Debug(this,DebugNote,"Failed to start RTP for content='%s' [%p]",
	    m_audioContent->toString().c_str(),this);
	return false;
    }

    m_rtpId = m.getValue("rtpid");
    rtpLocal->m_port = m.getValue("localport");

    String buf;
#ifdef DEBUG
    buf << ". Candidates local: ";
    dumpCandidate(buf,rtpLocal);
    buf << " remote: ";
    dumpCandidate(buf,rtpRemote);
#endif
    Debug(this,DebugAll,
	"RTP started for content='%s' local='%s:%s' remote='%s:%s'%s [%p]",
    	m_audioContent->toString().c_str(),
	rtpLocal->m_address.c_str(),rtpLocal->m_port.c_str(),
	rtpRemote->m_address.c_str(),rtpRemote->m_port.c_str(),buf.safe(),this);

    if (oldPort != rtpLocal->m_port && m_session) {
	rtpLocal->m_generation = rtpLocal->m_generation.toInteger(0) + 1;
	m_session->sendContent(JGSession::ActTransportInfo,m_audioContent);
    }

    if (rtpRemote->m_address &&
	(m_audioContent->m_rtpLocalCandidates.m_type == JGRtpCandidates::RtpIceUdp ||
	m_audioContent->m_rtpLocalCandidates.m_type == JGRtpCandidates::RtpP2P)) {
	m_rtpStarted = true;
	// Start STUN
	Message* msg = plugin.message("socket.stun");
	msg->userData(m.userData());
	String user;
	String pwd;
	if (m_audioContent->m_rtpLocalCandidates.m_type == JGRtpCandidates::RtpIceUdp) {
	    // FIXME: check if these parameters are correct
	    user = m_audioContent->m_rtpRemoteCandidates.m_ufrag +
		m_audioContent->m_rtpLocalCandidates.m_ufrag;
	    pwd = m_audioContent->m_rtpLocalCandidates.m_ufrag +
		m_audioContent->m_rtpRemoteCandidates.m_ufrag;
	}
	else {
	    JGRtpCandidateP2P* local = YOBJECT(JGRtpCandidateP2P,rtpLocal);
	    JGRtpCandidateP2P* remote = YOBJECT(JGRtpCandidateP2P,rtpRemote);
	    if (local && remote) {
		user = remote->m_username + local->m_username;
		pwd = local->m_username + remote->m_username;
	    }
	}
	msg->addParam("localusername",user);
	msg->addParam("remoteusername",pwd);
	msg->addParam("remoteip",rtpRemote->m_address.c_str());
	msg->addParam("remoteport",rtpRemote->m_port);
	msg->addParam("userid",m_rtpId);
	Engine::enqueue(msg);
    }
    else if (m_audioContent->m_rtpLocalCandidates.m_type == JGRtpCandidates::RtpRawUdp) {
	m_rtpStarted = true;
	// Send trying
	if (m_session) {
	    XmlElement* trying = XMPPUtils::createElement(XmlTag::Trying,
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
	    case JGSessionContent::RtpP2P:
	    case JGSessionContent::RtpGoogleRawUdp:
		break;
	    case JGSessionContent::FileBSBOffer:
	    case JGSessionContent::FileBSBRequest:
		// File transfer contents can be added only in session initiate
		if (event.action() != JGSession::ActInitiate) {
		    Debug(this,DebugInfo,
			"Event(%s) file transfer content='%s' in non initiate event [%p]",
			event.actionName(),c->toString().c_str(),this);
		    remove.append(c)->setDelete(false);
		    continue;
		}
		fileTransfer = true;
		break;
	    case JGSessionContent::Unknown:
	    case JGSessionContent::UnknownFileTransfer:
		Debug(this,DebugNote,
		    "Event(%s) with unknown (unsupported) content '%s' [%p]",
		    event.actionName(),c->toString().c_str(),this);
		remove.append(c)->setDelete(false);
		continue;
	}

	// Check creator
	if ((isOutgoing() && c->creator() == JGSessionContent::CreatorInitiator) ||
	    (isIncoming() && c->creator() == JGSessionContent::CreatorResponder)) {
	    Debug(this,DebugNote,
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
	    Debug(this,DebugNote,
		"Event(%s) content='%s' is already added [%p]",
		event.actionName(),c->toString().c_str(),this);
	    return false;
	}

	// Check transport type
	if (c->m_rtpRemoteCandidates.m_type == JGRtpCandidates::Unknown) {
	    Debug(this,DebugNote,
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
		Debug(this,DebugNote,
		    "Event(%s) content='%s' has invalid RTP candidate [%p]",
		    event.actionName(),c->toString().c_str(),this);
	        remove.append(c)->setDelete(false);
		continue;
	    }
	}
	else if (c->m_rtpRemoteCandidates.m_type == JGRtpCandidates::RtpRawUdp) {
	    Debug(this,DebugNote,
		"Event(%s) raw udp content='%s' without RTP candidate [%p]",
		event.actionName(),c->toString().c_str(),this);
	    remove.append(c)->setDelete(false);
	    continue;
	}
	JGRtpCandidate* rtcp = c->m_rtpRemoteCandidates.findByComponent(2);
	if (rtcp && !checkRecvCandidate(*c,*rtcp)) {
	    Debug(this,DebugNote,
		"Event(%s) content='%s' has invalid RTCP candidate [%p]",
		event.actionName(),c->toString().c_str(),this);
	    remove.append(c)->setDelete(false);
	    continue;
	}

	// Check media
	if (!checkMedia(event,*c)) {
	    remove.append(c)->setDelete(false);
	    continue;
	}
	c->m_rtpMedia.m_ready = true;

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
	    Debug(this,DebugNote,
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
    id << this->id() << "_content_" << (int)Random::random();
    JGSessionContent::Type t = JGSessionContent::Unknown;
    if (type == JGRtpCandidates::RtpRawUdp)
	t = JGSessionContent::RtpRawUdp;
    else if (type == JGRtpCandidates::RtpIceUdp)
	t = JGSessionContent::RtpIceUdp;
    else if (type == JGRtpCandidates::RtpP2P)
	t = JGSessionContent::RtpP2P;
    else if (type == JGRtpCandidates::RtpGoogleRawUdp)
	t = JGSessionContent::RtpGoogleRawUdp;
    JGSessionContent* c = new JGSessionContent(t,id,senders,
	isOutgoing() ? JGSessionContent::CreatorInitiator : JGSessionContent::CreatorResponder);

    // Add codecs
    c->m_rtpMedia.m_media = JGRtpMediaList::Audio;
    if (m_secure && m_secureRequired)
	c->m_rtpMedia.m_cryptoRequired = true;
    if (useFormats)
	c->m_rtpMedia.setMedia(m_audioFormats);
    if (m_sessVersion == JGSession::Version0 || type == JGRtpCandidates::RtpP2P ||
	type == JGRtpCandidates::RtpGoogleRawUdp){
	// Hack: set second telephone event for implementations expecting it
	c->m_rtpMedia.m_telEventName2 = "audio/telephone-event";
    }

    c->m_rtpLocalCandidates.m_type = c->m_rtpRemoteCandidates.m_type = type;

    if (type == JGRtpCandidates::RtpRawUdp || m_secure)
	initLocalCandidates(*c,false);

    return c;
}

// Build a file transfer content
JGSessionContent* YJGConnection::buildFileTransferContent(bool send, const char* filename,
    NamedList& params)
{
    // Build the content
    String id;
    id << this->id() << "_content_" << (int)Random::random();
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
    if (m_hangup)
	return false;
    JGRtpCandidate* rtp = content.m_rtpLocalCandidates.findByComponent(1);
    bool incGeneration = (0 != rtp);
    if (!rtp) {
	bool nonP2P = content.type() != JGSessionContent::RtpP2P &&
	    content.type() != JGSessionContent::RtpGoogleRawUdp;
	rtp = buildCandidate(nonP2P);
	content.m_rtpLocalCandidates.append(rtp);
    }

    // TODO: handle RTCP

    Message m("chan.rtp");
    m.userData(static_cast<CallEndpoint*>(this));
    complete(m);
    m.setParam("direction",rtpDir(content));
    m.addParam("media","audio");
    m.addParam("getsession","true");
    m.addParam("anyssrc","true");
    if (m_localip)
	m.addParam("localip",m_localip);
    else if (!plugin.addLocalIp(m)) {
	JGRtpCandidate* remote = content.m_rtpRemoteCandidates.findByComponent(1);
	if (remote && remote->m_address)
	    m.addParam("remoteip",remote->m_address);
    }
    if (m_secure) {
	ObjList* cr = content.m_rtpMedia.m_cryptoRemote.skipNull();
	if (cr)
	    addSecure(m,static_cast<JGCrypto*>(cr->get()));
	else if (m_secureRequired) {
	    // TODO: Terminate the call or try to use another content
	    Debug(this,DebugNote,"No required crypto in current content [%p]",this);
	    dropNoCrypto();
	    return false;
	}
    }

    if (!Engine::dispatch(m)) {
	Debug(this,DebugNote,"Failed to init RTP for content='%s' [%p]",
	    content.toString().c_str(),this);
	return false;
    }

    if (m_secure) {
	NamedString* cSuite = m.getParam("ocrypto_suite");
	if (cSuite) {
	    JGCrypto* crypto = new JGCrypto("1",*cSuite,m.getValue("ocrypto_key"));
	    content.m_rtpMedia.m_cryptoLocal.append(crypto);
	}
	else if (m_secureRequired) {
	    // Failed to setup encryption
	    // TODO: Terminate the call or try to use another content
	    Debug(this,DebugNote,"Failed to setup required crypto [%p]",this);
	    dropNoCrypto();
	    return false;
	}
    }

    m_rtpId = m.getValue("rtpid");
    plugin.setLocalIp(rtp->m_address,m);
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
bool YJGConnection::matchMedia(JGSessionContent& local, JGSessionContent& recv,
    bool& firstChanged, bool& telEvChanged) const
{
    bool first = true;
    ListIterator iter(local.m_rtpMedia);
    for (GenObject* gen = 0; 0 != (gen = iter.get()); first = false) {
	JGRtpMedia* m = static_cast<JGRtpMedia*>(gen);
	JGRtpMedia* found = recv.m_rtpMedia.findSynonym(m->m_synonym);
	// Check synonym, the content is already checked
	if (found) {
	    if (m->m_id == found->m_id)
		continue;
	    Debug(this,DebugAll,
		"Content '%s' remote changed payload id from %s to %s for '%s' [%p]",
		local.toString().c_str(),m->m_id.c_str(),found->m_id.c_str(),
		m->m_synonym.c_str(),this);
	    m->m_id = found->m_id;
	    if (first)
		firstChanged = true;
	    continue;
	}
	Debug(this,DebugAll,"Content '%s' removing media %s/%s from offer [%p]",
	    local.toString().c_str(),m->m_id.c_str(),m->m_synonym.c_str(),this);
	local.m_rtpMedia.remove(m);
	if (first)
	    firstChanged = true;
    }
    // Update telephone event payload id
    if (local.m_rtpMedia.m_telEvent != recv.m_rtpMedia.m_telEvent) {
	Debug(this,DebugAll,"Content '%s' changing tel event from %d to %d [%p]",
	    local.toString().c_str(),local.m_rtpMedia.m_telEvent,
	    recv.m_rtpMedia.m_telEvent,this);
	local.m_rtpMedia.m_telEvent = recv.m_rtpMedia.m_telEvent;
	telEvChanged = true;
    }
    if (local.m_rtpMedia.skipNull())
	return true;
    Debug(this,DebugInfo,"No common media for content=%s [%p]",
	local.toString().c_str(),this);
    return false;
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
    if (ringFlag(RingNoEarlySession) || isOutgoing() || isAnswered())
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
	plugin.lock();
	c->m_rtpMedia.setMedia(s_usedCodecs,formats);
	plugin.unlock();
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
    status("progressing");
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
	m.addParam("client",String::boolText(m_ftHostDirection != FTHostLocal));
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
	    dropFT(true);
	    // We can send hosts: try to get a local socks server
	    if (m_ftHostDirection == FTHostLocal) {
		Message m("chan.socks");
		m.userData(this);
		m.addParam("dst_addr_domain",m_dstAddrDomain);
		m.addParam("direction",dir);
		m.addParam("client",String::boolText(false));
		if (m_localip && !s_serverMode && m_connSocksServer)
		    m.addParam("localip",m_localip);
		DDebug(this,DebugAll,"Trying to setup local SOCKS server [%p]",this);
		if (Engine::dispatch(m)) {
		    const char* addr = m.getValue("address");
		    int port = m.getIntValue("port");
		    m_ftNotifier = m.getValue("notifier");
		    if (!null(addr) && port > 0) {
			m_streamHosts.append(new JGStreamHost(true,m_local,addr,port));
			m_ftStatus = FTWaitEstablish;
			// Send our stream host
			m_session->sendStreamHosts(m_streamHosts,&m_ftStanzaId);
			break;
		    }
		    dropFT(true);
		}
		error = "chan.socks failed";
	    }
	    else
		error = "no hosts";
	    break;
	}

	// Remove the first stream host if status is idle: it failed
	if (m_ftStatus != FTIdle) {
	    dropFT(false);
	    dropFTHost(0,o,"failed");
	    o = m_streamHosts.skipNull();
	}

	while (o) {
	    dropFT(false);
	    Message m("chan.socks");
	    m.userData(this);
	    m.addParam("dst_addr_domain",m_dstAddrDomain);
	    m.addParam("direction",dir);
	    m.addParam("client",String::boolText(true));
	    JGStreamHost* sh = static_cast<JGStreamHost*>(o->get());
	    m.addParam("remoteip",sh->m_address);
	    m.addParam("remoteport",String(sh->m_port));
	    if (Engine::dispatch(m)) {
		m_ftNotifier = m.getValue("notifier");
		break;
	    }
	    dropFTHost(sh,o,"failed");
	    o = m_streamHosts.skipNull();
	}
	if (o)
	    m_ftStatus = FTWaitEstablish;
	else
	    error = "no more hosts";
	break;
    }

    if (!error) {
	DDebug(this,DebugAll,"Waiting SOCKS file transfer notifier=%s [%p]",
	    m_ftNotifier.c_str(),this);
	return true;
    }

    // Check if we can still negotiate hosts
    if (changeFTHostDir()) {
	m_ftStatus = FTIdle;
	return false;
    }

    m_ftStatus = FTTerminated;
    Debug(this,DebugNote,"Failed to initialize SOCKS file transfer '%s' [%p]",
	error,this);
    hangup("notransport",0,JGSession::ReasonFailTransport);
    return false;
}

// Drop file transfer data. Remove the first host in list
void YJGConnection::dropFT(bool removeFirst)
{
    if (removeFirst) {
	// Remove first entry in hosts
	ObjList* o = m_streamHosts.skipNull();
	if (o)
	    dropFTHost(0,o);
    }
    m_ftNotifier.clear();
    clearEndpoint("data");
}

// Drop file transfer hosts
void YJGConnection::dropFTHosts(bool local, const char* reason)
{
    for (ObjList* o = m_streamHosts.skipNull(); o;) {
	JGStreamHost* sh = static_cast<JGStreamHost*>(o->get());
	if (sh->m_local != local)
	    o = o->skipNext();
	else {
	    dropFTHost(sh,o,reason);
	    o = o->skipNull();
	}
    }
}

// Drop file transfer host
void YJGConnection::dropFTHost(JGStreamHost* sh, ObjList* remove, const char* reason)
{
    if (!sh && remove)
	sh = static_cast<JGStreamHost*>(remove->get());
    if (!sh)
	return;
    Debug(this,DebugAll,"Removing %s streamhost '%s:%d' reason='%s' [%p]",
	sh->m_local ? "local" : "remote",sh->m_address.c_str(),sh->m_port,
	TelEngine::c_safe(reason),this);
    if (remove)
	remove->remove();
    else
	TelEngine::destruct(sh);
}

// Change host sender. Return false on failure
bool YJGConnection::changeFTHostDir(bool resetState)
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
    if (!resetState)
	return false;
    if (m_ftHostDirection != FTHostNone)
	Debug(this,DebugNote,"No more hosts available [%p]",this);
    m_ftHostDirection = FTHostNone;
    return false;
}

// Build a RTP candidate
JGRtpCandidate* YJGConnection::buildCandidate(bool nonP2P, bool rtp)
{
    if (nonP2P)
	return new JGRtpCandidate(id() + "_candidate_" + String((int)Random::random()),
	    rtp ? "1" : "2");
    JGRtpCandidateP2P* p2p = new JGRtpCandidateP2P;
    JGRtpCandidates::generateOldIceToken(p2p->m_username);
    JGRtpCandidates::generateOldIceToken(p2p->m_password);
    return p2p;
}

// Process chan.notify messages
// Handle SOCKS status changes for file transfer
bool YJGConnection::processChanNotify(Message& msg)
{
    XDebug(this,DebugAll,"processChanNotify notifier=%s status=%s",
	msg.getValue("id"),msg.getValue("status"));
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
	    XmlElement* what = 0;
	    if (event->jingle())
		what = XMPPUtils::findFirstChild(*event->jingle(),
		    hold ? XmlTag::Hold : XmlTag::Active);
	    if (what) {
		if (hold)
		    m_dataFlags |= OnHoldRemote;
		else
		    m_dataFlags &= ~OnHoldRemote;
		Message* m = message("call.update");
		m->addParam("operation","notify");
		m->userData(this);
		// Copy additional attributes
		// Reset param 'name': the second param of toList() is the prefix
		XMPPUtils::toList(*what,*m,what->tag());
		m->setParam(what->tag(),String::boolText(true));
		// Clear endpoint before dispatching the message
		// Our data source/consumer may be replaced
		if (hold) {
		    clearEndpoint();
		    m_rtpId.clear();
		    m_rtpStarted = false;
		}
		Engine::dispatch(*m);
		TelEngine::destruct(m);
		// Reset data transport when put on hold
		removeCurrentAudioContent();
		// Update channel data source/consumer
		if (!hold)
		    resetCurrentAudioContent(true,false);
	    }
	    else
		err = XMPPError::FeatureNotImpl;
	}
	// Respond with error if put on hold by the other party
	else if (dataFlags(OnHoldLocal)) {
	    err = XMPPError::Request;
	    text = "Already on hold by the other party";
	}
    }
    else if (event->action() == JGSession::ActMute) {
	// TODO: implement
	err = XMPPError::FeatureNotImpl;
    }
    else
	err = XMPPError::FeatureNotImpl;

    // Confirm received element
    if (err == XMPPError::NoError) {
	DDebug(this,DebugAll,"Accepted '%s' request [%p]",event->actionName(),this);
	event->confirmElement();
    }
    else {
	Debug(this,DebugInfo,"Denying '%s' request error='%s' reason='%s' [%p]",
	    event->actionName(),XMPPUtils::s_error[err].c_str(),text,this);
	event->confirmElement(err,text);
    }
}

// Check jingle version override from call.execute or resource caps
void YJGConnection::overrideJingleVersion(const NamedList& list, bool caps)
{
    String* ver = list.getParam(caps ? "caps.jingle_version" : "ojingle_version");
    if (!ver)
	return;
    JGSession::Version v = JGSession::lookupVersion(*ver);
    if (v != JGSession::VersionUnknown && v != m_sessVersion) {
	Debug(this,DebugAll,"Jingle version set to %s from %s",
	    ver->c_str(),caps ? "resource caps" : "routing");
	m_sessVersion = v;
    }
}

// Override session flags
void YJGConnection::overrideJingleFlags(const NamedList& list, const char* param)
{
    String* str = list.getParam(param);
    if (!str)
	return;
    m_sessFlags = JGEngine::decodeFlags(*str,JGSession::s_flagName);
    Debug(this,DebugAll,"Session flags set to %d from %s=%s [%p]",
	m_sessFlags,param,str->c_str(),this);
}

// Copy chan/session params to a destination list
void YJGConnection::copySessionParams(NamedList& list, bool redirect)
{
    String* copy = list.getParam("copyparams");
    if (redirect) {
	list.addParam("redirect",String::boolText(true));
	jingleAddParam(list,"redirectcount",String(m_redirectCount),copy);
	list.addParam("diverter",m_remote,false);
    }
    if (m_ftStatus == FTNone) {
	String formats;
	m_audioFormats.createList(formats,true);
	jingleAddParam(list,"formats",formats,copy,false);
    }
    else
	jingleAddParam(list,"format","data",copy);
    // Jingle session params
    jingleAddParam(list,"line",m_line,copy,false);
    jingleAddParam(list,"ojingle_version",
	JGSession::lookupVersion(m_sessVersion,""),copy,false);
    String flags;
    JGEngine::encodeFlags(flags,m_sessFlags,JGSession::s_flagName);
    jingleAddParam(list,"ojingle_flags",flags,copy,false);
    jingleAddParam(list,"callerprompt",m_callerPrompt,copy,false);
    jingleAddParam(list,"subject",m_subject,copy,false);
    jingleAddParam(list,"secure",String::boolText(m_secure),copy);
    jingleAddParam(list,"secure_required",String::boolText(m_secureRequired),copy);
    jingleAddParam(list,"offerrawudp",String::boolText(m_offerRawTransport),copy);
    jingleAddParam(list,"offericeudp",String::boolText(m_offerIceTransport),copy);
    jingleAddParam(list,"offerp2p",String::boolText(m_offerP2PTransport),copy);
    jingleAddParam(list,"offergraw",String::boolText(m_offerGRawTransport),copy);
    jingleAddParam(list,"dtmfmethod",lookup(m_dtmfMeth,s_dictDtmfMeth),copy,false);
    // File transfer
    JGSessionContent* c = firstFTContent();
    if (!c)
	return;
    const char* oper = 0;
    if (c->type() == JGSessionContent::FileBSBOffer)
	oper = "send";
    else if (c->type() == JGSessionContent::FileBSBRequest)
	oper = "receive";
    else
	return;
    const String& file = c->m_fileTransfer["name"];
    if (!file)
	return;
    jingleAddParam(list,"operation",oper,copy);
    jingleAddParam(list,"file_name",file,copy);
    jingleAddParam(list,"file_size",c->m_fileTransfer.getValue("size"),copy,false);
    jingleAddParam(list,"file_md5",c->m_fileTransfer.getValue("hash"),copy,false);
    unsigned int t = XMPPUtils::decodeDateTimeSec(c->m_fileTransfer["date"]);
    if (t != (unsigned int)-1)
	jingleAddParam(list,"file_time",String(t),copy);
}

// Check media in a received content
bool YJGConnection::checkMedia(const JGEvent& event, JGSessionContent& c)
{
    JGRtpMediaList& codecs = m_audioFormats;
    // Fill a string with our capabilities for debug purposes
    String remoteCaps;
    if (debugAt(DebugInfo))
	c.m_rtpMedia.createList(remoteCaps,false);
    ListIterator iter(c.m_rtpMedia);
    for (GenObject* go = 0; (go = iter.get());) {
	JGRtpMedia* recv = static_cast<JGRtpMedia*>(go);
	XDebug(this,DebugAll,"Checking received media %s/%s/%s/%s/%s/%s/%s [%p]",
	    recv->m_id.c_str(),recv->m_name.c_str(),recv->m_clockrate.c_str(),
	    recv->m_channels.c_str(),recv->m_pTime.c_str(),
	    recv->m_maxPTime.c_str(),recv->m_bitRate.c_str(),this);
	const char* reason = 0;
	int level = DebugNote;
	// Use a while() to break to the end
	while (true) {
	    // RTP payload id must be [0..127]
	    int payloadId = recv->m_id.toInteger(-1);
	    if (payloadId < 0 || payloadId > 127) {
		reason = "Invalid id";
		break;
	    }
	    // XEP 0167: Channels is an unsigned byte, defaults to 1
	    // We support only 1 channel for now
	    if (recv->m_channels.toInteger(1) != 1) {
		reason = "Invalid number of channels";
		break;
	    }
	    JGRtpMedia* found = 0;
	    // 0..95: static payloads: match by id
	    // > 95: dynamic payloads: match by name
	    if (payloadId < 96)
		found = codecs.findMedia(recv->m_id);
	    else if (recv->m_name) {
		// Remove tel event from offer
		if (isTelEvent(recv->m_name)) {
		    XDebug(this,DebugAll,"Removing tel event payload=%d '%s' [%p]",
			payloadId,recv->m_name.c_str(),this);
		    c.m_rtpMedia.m_telEvent = payloadId;
		    c.m_rtpMedia.m_telEventName = recv->m_name;
		    c.m_rtpMedia.remove(recv);
		    break;
		}
		for (ObjList* o = codecs.skipNull(); o; o = o->skipNext(), found = 0) {
		    found = static_cast<JGRtpMedia*>(o->get());
		    if ((found->m_name |= recv->m_name))
			continue;
		    if (recv->m_clockrate && recv->m_clockrate != found->m_clockrate)
			continue;
		    // Fix ilbc
		    if (recv->m_name &= "ilbc") {
			// RFC 3952 specifies
			// 30ms ptime = 13.33 kbit/s: check 13000
			// 20ms ptime = 15.2 kbit/s:  check 15000
			if (!recv->m_pTime && recv->m_bitRate) {
			    int val = recv->m_bitRate.toInteger() / 1000;
			    if (val == 13)
				recv->m_pTime = "30";
			    else if (val == 15)
				recv->m_pTime = "20";
			}
			if (!recv->m_pTime)
			    recv->m_pTime = (s_ilbcDefault30 ? "30" : "20");
			if (recv->m_pTime != found->m_pTime)
				continue;
		    }
		    break;
		}
	    }
	    else {
		// XEP 0167: name is mandatory for dynamic payloads
		reason = "Missing name for dynamic payload";
		break;
	    }
	    if (found) {
		XDebug(this,DebugAll,"Setting synonym=%s to received %s from %s/%s [%p]",
		    found->m_synonym.c_str(),recv->m_name.c_str(),
		    found->m_id.c_str(),found->m_name.c_str(),this);
		recv->m_synonym = found->m_synonym;
	    }
	    else {
		reason = "Codec disabled/unknown";
		level = DebugAll;
	    }
	    break;
	}
	if (!reason)
	    continue;
	Debug(this,level,
	    "Event(%s) removing payload id=%s %s/%s/%s/%s from content='%s': %s [%p]",
	    event.actionName(),recv->m_id.c_str(),recv->m_name.c_str(),
	    recv->m_clockrate.c_str(),recv->m_channels.c_str(),recv->m_pTime.c_str(),
	    c.toString().c_str(),reason,this);
	c.m_rtpMedia.remove(recv);
    }
    // Check if both parties have common media
    if (c.m_rtpMedia.skipNull()) {
#ifdef DEBUG
	String formats;
	c.m_rtpMedia.createList(formats,true);
	Debug(this,DebugAll,"Set formats '%s' in content '%s' [%p]",
	    formats.c_str(),c.toString().c_str(),this);
#endif
	return true;
    }
    if (debugAt(DebugInfo)) {
	String localCaps;
	codecs.createList(localCaps,false);
	Debug(this,DebugNote,
	    "Event(%s) no common media for content='%s' local='%s' remote='%s' [%p]",
	    event.actionName(),c.toString().c_str(),localCaps.c_str(),
	    remoteCaps.c_str(),this);
    }
    return false;
}

// Clear and reset audio related data
void YJGConnection::resetEp(const String& what, bool releaseContent)
{
    Debug(this,DebugAll,"Resetting endpoint '%s' [%p]",what.c_str(),this);
    clearEndpoint(what);
    Lock lock(m_mutex);
    if (!what || what == "audio") {
	m_rtpId.clear();
	m_rtpStarted = false;
	if (releaseContent)
	    TelEngine::destruct(m_audioContent);
    }
}

// Hangup and drop the call if failed to setup encryption
void YJGConnection::dropNoCrypto()
{
    const char* reason = "crypto-required";
    hangup(reason,"Failed to setup encryption");
    Message* m = new Message("call.drop");
    m->addParam("id",id());
    m->addParam("reason",reason);
    Engine::enqueue(m);
}

// Send ringing
void YJGConnection::sendRinging(NamedList* params)
{
    if (ringFlag(RingNone))
	return;
    bool sendContent = ringFlag(RingWithContent) && getPeer() && getPeer()->getSource();
    DDebug(this,DebugNote,"sendRinging flags=0x%x params=%p sendContent=%u [%p]",
	m_ringFlags,params,sendContent,this);
    if (params) {
	if (params->getBoolValue(YSTRING("earlymedia"),true))
	    m_ringFlags |= RingGotEarlyMedia;
	sendContent = sendContent && ringFlag(RingGotEarlyMedia);
    }
    else {
	// Added new content or changed one
	// Return if no ringing or content already sent
	if (!ringFlag(RingRinging) || ringFlag(RingContentSent))
	    return;
	sendContent = sendContent && ringFlag(RingGotEarlyMedia);
	// No need to send content: return
	if (!sendContent)
	    return;
    }
    Lock mylock(m_mutex);
    if (!m_session)
	return;
    XmlElement* rInfo = m_session->createRtpInfoXml(JGSession::RtpRinging);
    if (!rInfo)
	return;
    XmlElement* cXml = 0;
    if (sendContent) {
	if (!m_audioContent || m_audioContent->isEarlyMedia())
	    resetCurrentAudioContent(true,false,true,0,false);
	JGRtpCandidate* rtp = m_audioContent ? m_audioContent->m_rtpLocalCandidates.findByComponent(1) : 0;
	if (rtp && rtp->m_address) {
	    cXml = m_audioContent->toXml(false,true,true,true,false);
	    m_ringFlags |= RingContentSent;
	}
	else if (ringFlag(RingWithContentOnly)) {
	    TelEngine::destruct(rInfo);
	    return;
	}
    }
    m_session->sendInfo(rInfo,0,cXml);
}


/*
 * Transfer thread (route and execute)
 */
YJGTransfer::YJGTransfer(YJGConnection* conn, const char* subject)
    : Thread("Jingle Transfer"),
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
	m_msg.addParam("reason",lookup(JGSession::Transferred,s_errMap));
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
    YJGConnection* conn = static_cast<YJGConnection*>(plugin.find(m_transferorID));
    if (conn)
	conn->transferTerminated(!error,error);
#ifdef DEBUG
    else
	Debug(&plugin,DebugNote,
	    "%s thread transfer terminated trans=%s error=%s (transferor not found) [%p]",
	    name(),m_transferredID.c_str(),error.c_str(),this);
#endif
    plugin.unlock();
}


/*
 * JBMessageHandler
 */
YJGMessageHandler::YJGMessageHandler(int handler, int prio)
    : MessageHandler(lookup(handler,s_msgHandler),prio,plugin.name()),
      m_handler(handler)
{
}

bool YJGMessageHandler::received(Message& msg)
{
    switch (m_handler) {
	case JabberIq:
	    return !plugin.isModule(msg) && plugin.handleJabberIq(msg);
	case ResNotify:
	    return !plugin.isModule(msg) && plugin.handleResNotify(msg);
	case ResSubscribe:
	    return !plugin.isModule(msg) && plugin.handleResSubscribe(msg);
	case ChanNotify:
	    return !plugin.isModule(msg) && plugin.handleChanNotify(msg);
	case EngineStart:
	    plugin.handleEngineStart(msg);
	    return false;
	case UserNotify:
	    return !plugin.isModule(msg) && plugin.handleUserNotify(msg);
	default:
	    DDebug(&plugin,DebugStub,"YJGMessageHandler(%s) not handled!",msg.c_str());
    }
    return false;
}


/*
 * YJGDriver
 */
YJGDriver::YJGDriver()
    : Driver("jingle","varchans"), m_init(false), m_ftProxy(0), m_handleAllRes(false),
    m_entityCaps(0)
{
    Output("Loaded module YJingle");
    s_serverMode = !Engine::clientMode();
    if (s_serverMode)
	Engine::extraPath("jabber");
}

YJGDriver::~YJGDriver()
{
    Output("Unloading module YJingle");
    delete s_jingle;
    s_jingle = 0;
    TelEngine::destruct(m_entityCaps);
}

void YJGDriver::initialize()
{
    Output("Initializing module YJingle");

    lock();
    s_cfg = Engine::configFile("yjinglechan");
    s_cfg.load();
    NamedList dummy("");
    NamedList* sect = s_cfg.getSection("general");
    if (!sect)
	sect = &dummy;

    // Update now the server mode flag
    s_serverMode = sect->getBoolValue("servermode",!Engine::clientMode());

    if (!m_init) {
	m_init = true;

	// Init all known codecs
	s_knownCodecs.add("0",  "PCMU",    "8000",  "mulaw");
	s_knownCodecs.add("2",  "G726-32", "8000",  "g726");
	s_knownCodecs.add("3",  "GSM",     "8000",  "gsm");
	s_knownCodecs.add("4",  "G723",    "8000",  "g723");
	s_knownCodecs.add("7",  "LPC",     "8000",  "lpc10");
	s_knownCodecs.add("8",  "PCMA",    "8000",  "alaw");
	s_knownCodecs.add("9",  "G722",    "8000",  "g722");
	s_knownCodecs.add("11", "L16",     "8000",  "slin");
	s_knownCodecs.add("15", "G728",    "8000",  "g728");
	s_knownCodecs.add("18", "G729",    "8000",  "g729");
	s_knownCodecs.add("31", "H261",    "90000", "h261");
	s_knownCodecs.add("32", "MPV",     "90000", "mpv");
	s_knownCodecs.add("34", "H263",    "90000", "h263");
	s_knownCodecs.add("98", "iLBC",    "8000",  "ilbc");
	s_knownCodecs.add("98", "iLBC",    "8000",  "ilbc20", 0, "20", 0, "15200");
	s_knownCodecs.add("98", "iLBC",    "8000",  "ilbc30", 0, "30", 0, "13300");
	s_knownCodecs.add("102","speex",   "8000",  "speex");
	s_knownCodecs.add("103","speex",   "16000", "speex/16000");
	s_knownCodecs.add("104","speex",   "32000", "speex/32000");
	s_knownCodecs.add("105","ISAC",    "16000", "isac/16000");
	s_knownCodecs.add("106","ISAC",    "32000", "isac/32000");

	s_jingle = new YJGEngine;
	s_jingle->debugChain(this);
	// Driver setup
	setup();
	installRelay(Halt);
	installRelay(Route);
	installRelay(Update);
	installRelay(Transfer);
	installRelay(MsgExecute);
	installRelay(Progress);
	// Install handlers
	for (const TokenDict* d = s_msgHandler; d->token; d++) {
	    if (!Engine::clientMode() && d->value == YJGMessageHandler::UserNotify)
		continue;
	    int prio = d->value < 0 ? 100 : d->value;
	    if (d->value == YJGMessageHandler::ResNotify)
		prio = sect->getIntValue(d->token,prio);
	    YJGMessageHandler* h = new YJGMessageHandler(d->value,prio);
	    Engine::install(h);
	    m_handlers.append(h);
	}
	// Set features
	m_features.add(XMPPNamespace::Jingle);
	m_features.add(XMPPNamespace::JingleError);
	m_features.add(XMPPNamespace::JingleAppsRtpAudio);
	m_features.add(XMPPNamespace::JingleAppsRtp);
	m_features.add(XMPPNamespace::JingleAppsRtpInfo);
	m_features.add(XMPPNamespace::JingleAppsRtpError);
	m_features.add(XMPPNamespace::JingleTransportIceUdp);
	m_features.add(XMPPNamespace::JingleTransportRawUdp);
	m_features.add(XMPPNamespace::JingleTransfer);
	m_features.add(XMPPNamespace::JingleDtmf);
	m_features.add(XMPPNamespace::JingleAppsFileTransfer);
	m_features.add(XMPPNamespace::JingleSession);
	m_features.add(XMPPNamespace::JingleAudio);
	m_features.add(XMPPNamespace::JingleTransport);
	m_features.add(XMPPNamespace::DtmfOld);
	m_features.add(XMPPNamespace::DiscoInfo);
	m_features.add(XMPPNamespace::DiscoItems);
	m_features.add(XMPPNamespace::EntityCaps);
	if (s_serverMode)
	    m_features.m_identities.append(new JIDIdentity("gateway","telephony","Jingle Telephony Gateway"));
	else
	    m_features.m_identities.append(new JIDIdentity("client","pc"));
	m_features.updateEntityCaps();
	m_entityCaps = XMPPUtils::createEntityCaps(m_features.m_entityCapsHash,s_capsNode);

	(new YJGEngineWorker)->startup();
    }
    else {
	setDomains(sect->getValue("domains"));
	loadLimits();
    }
    s_jingle->initialize(*sect);

    if (s_serverMode) {
	s_requestSubscribe = sect->getBoolValue("request_subscribe",true);
	s_autoSubscribe = sect->getBoolValue("auto_subscribe",false);
	m_resources.clear();
	m_handleAllRes = false;
	const char* resources = sect->getValue("resources");
	if (resources) {
	    String resList(resources);
	    ObjList* list = resList.split(',',false);
	    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
		String* tmp = static_cast<String*>(o->get());
		if (!m_resources.find(*tmp))
		    m_resources.append(new String(*tmp));
	    }
	    TelEngine::destruct(list);
	}
	else {
	    m_handleAllRes = true;
	    m_resources.append(new String("yate"));
	}
    }
    else {
	s_requestSubscribe = false;
	s_autoSubscribe = false;
    }
    s_singleTone = sect->getBoolValue("singletone",true);
    s_pendingTimeout = sect->getIntValue("pending_timeout",10000);
    s_imToChanText = sect->getBoolValue("imtochantext",false);
    s_useCrypto = sect->getBoolValue("secure",false);
    s_cryptoMandatory = sect->getBoolValue("secure_required",false);
    s_acceptRelay = sect->getBoolValue("accept_relay",!s_serverMode);
    s_sessVersion = JGSession::lookupVersion(sect->getValue("jingle_version"),JGSession::Version1);
    s_ringFlags = YJGConnection::getRinging(*sect,this);
    m_anonymousCaller = sect->getValue("anonymous_caller","unk_caller");
    m_localAddress = sect->getValue("localip");
    s_offerRawTransport = sect->getBoolValue("offerrawudp",true);
    s_offerIceTransport = sect->getBoolValue("offericeudp",true);
    s_offerP2PTransport = sect->getBoolValue("offerp2p",false);
    s_offerGRawTransport = sect->getBoolValue("offergraw",false);
    int redir = sect->getIntValue("redirectcount");
    s_redirectCount = (redir >= 0) ? redir : 0;
    s_dtmfMeth = sect->getIntValue("dtmfmethod",s_dictDtmfMeth,DtmfJingle);
    s_clearFilePath = sect->getBoolValue("clear_file_path");
    // set max chans
    maxChans(sect->getIntValue("maxchans",maxChans()));

    int prio = sect->getIntValue("resource_priority");
    if (prio < -128)
	s_priority = -128;
    else if (prio > 127)
	s_priority = 127;
    else
	s_priority = prio;

    // Init codecs in use. Check each codec in known codecs list against the configuration
    s_usedCodecs.clear();
    bool defcodecs = s_cfg.getBoolValue("codecs","default",true);
    for (ObjList* o = s_knownCodecs.skipNull(); o; o = o->skipNext()) {
	JGRtpMedia* crt = static_cast<JGRtpMedia*>(o->get());
	if (crt->m_name &= "ilbc")
	    continue;
	bool enable = defcodecs && DataTranslator::canConvert(crt->m_synonym);
	if (s_cfg.getBoolValue("codecs",crt->m_synonym,enable))
	    s_usedCodecs.append(new JGRtpMedia(*crt));
    }
    // Special care for ilbc
    bool ilbc = s_cfg.getBoolValue("codecs","ilbc",defcodecs);
    if (ilbc) {
	String tmp = s_cfg.getValue("hacks","ilbc_forced","ilbc30");
	if (tmp != "ilbc20" && tmp != "ilbc30")
	    tmp = "ilbc30";
	JGRtpMedia* s = s_knownCodecs.findSynonym(tmp);
	if (s && DataTranslator::canConvert(s->m_synonym))
	    s_usedCodecs.append(new JGRtpMedia(*s));
	tmp = s_cfg.getValue("hacks","ilbc_default","ilbc30");
	s_ilbcDefault30 = (tmp != "ilbc20");
    }

    TelEngine::destruct(m_ftProxy);
    const char* ftJid = sect->getValue("socks_proxy_jid");
    if (!null(ftJid)) {
	const char* ftAddr = sect->getValue("socks_proxy_ip");
	int ftPort = sect->getIntValue("socks_proxy_port",-1);
	if (!(null(ftAddr) || ftPort < 1))
	    m_ftProxy = new JGStreamHost(true,ftJid,ftAddr,ftPort);
	else
	    Debug(this,DebugNote,
		"Invalid addr/port (%s:%s) for default file transfer proxy",
		sect->getValue("socks_proxy_ip"),sect->getValue("socks_proxy_port"));
    }

    int dbg = DebugInfo;
    if (!m_localAddress && s_serverMode)
	dbg = DebugNote;
    if (!s_usedCodecs.count())
	dbg = DebugWarn;

    if (debugAt(dbg)) {
	String s;
	s << " localip=" << (m_localAddress ? m_localAddress.c_str() : "MISSING");
	s << " jingle_version=" << JGSession::lookupVersion(s_sessVersion);
	s << " singletone=" << String::boolText(s_singleTone);
	s << " pending_timeout=" << s_pendingTimeout;
	s << " anonymous_caller=" << m_anonymousCaller;
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
    if (!line)
	return false;
    Message* m = checkAccount(line);
    if (!m)
	return false;
    TelEngine::destruct(m);
    return true;
}

// Make outgoing calls
// Build peers' JIDs and check if the destination is available
bool YJGDriver::msgExecute(Message& msg, String& dest)
{
    if (!msg.userData()) {
	Debug(this,DebugNote,"Jingle call failed. No data channel");
	msg.setParam("error","failure");
	return false;
    }
    JabberID caller;
    JabberID called;
    NamedList caps("");
    called.set(dest);
    if (!called.node()) {
	Debug(this,DebugNote,"Jingle call failed. Incomplete called '%s'",called.c_str());
	msg.setParam("error","failure");
	return false;
    }
    bool checkCalled = msg.getBoolValue("checkcalled",true);
    const char* line = msg.getValue("line");
    String localip;
    // Set caller
    if (s_serverMode) {
	const char* cr = msg.getValue("caller");
	caller.set(cr);
	if (!caller.node()) {
	    lock();
	    // has domain: 'cr' is the username: pick the default domain
	    // no domain: set default username and pick the default domain
	    if (!caller.domain())
		cr = m_anonymousCaller;
	    // The first component domain is the default one
	    ObjList* o = m_domains.skipNull();
	    if (o)
		caller.set(cr,o->get()->toString());
	    else
		caller.set("");
	    unlock();
	    if (!caller) {
		Debug(this,DebugNote,"Jingle call failed. No default server");
		msg.setParam("error","failure");
		return false;
	    }
	}
	// Check domain
	if (!handleDomain(caller.domain())) {
	    Debug(this,DebugNote,"Jingle call failed. Caller '%s' not in our domain(s)",
		caller.c_str());
	    msg.setParam("error","failure");
	    return false;
	}
	// Check/set the resource
	if (caller.bare() && !caller.resource()) {
	    caller.resource(msg.getValue("caller_instance"));
	    if (!caller.resource()) {
		String tmp;
		defaultResource(tmp);
		caller.resource(tmp);
	    }
	}
	if (caller.resource() && !handleResource(caller.resource())) {
	    Debug(this,DebugNote,"Jingle call failed. Invalid resource '%s'",
		caller.resource().c_str());
	    msg.setParam("error","failure");
	    return false;
	}
    }
    else {
	// Get line data
	if (!TelEngine::null(line)) {
	    Message* m = plugin.checkAccount(line,true,checkCalled ? &called : 0);
	    if (m) {
		caller.set(m->getValue("jid"));
		if (caller.isFull()) {
		    if (checkCalled && called && !called.resource())
			called.resource(m->getValue("instance"));
		    // Copy resource caps
		    unsigned int n = m->length();
		    for (unsigned int i = 0; i < n; i++) {
			NamedString* ns = m->getParam(i);
			if (ns && ns->name().startsWith("caps."))
			    caps.addParam(ns->name(),*ns);
		    }
		}
		else
		    caller.set("");
		localip = m->getValue("localip");
		TelEngine::destruct(m);
	    }
	    if (!caller)
		DDebug(this,DebugInfo,"No stream for line=%s",line);
	}
	if (!caller)
	    caller.set(msg.getValue("caller"));
    }
    if (!caller.isFull()) {
	Debug(this,DebugNote,"Jingle call failed. Incomplete caller '%s'",
	    caller.c_str());
	msg.setParam("error","failure");
	return false;
    }
    // Called party must always be full in client mode
    if (checkCalled && !(s_serverMode || called.isFull())) {
	Debug(this,DebugNote,"Jingle call failed. Incomplete called '%s'",
	    called.c_str());
	msg.setParam("error","failure");
	return false;
    }

    // Check if this is a file transfer
    String file;
    String* format = msg.getParam("format");
    if (format && *format == "data") {
	// Check file. Remove path if present
	file = msg.getValue("file_name");
	if (file && msg.getBoolValue(YSTRING("clear_file_path"),s_clearFilePath)) {
	    int pos = file.rfind('/');
	    if (pos == -1)
		pos = file.rfind('\\');
	    if (pos != -1)
		file = file.substr(pos + 1);
	}
	if (file.null()) {
	    Debug(this,DebugNote,"Jingle call failed. File transfer request with no file");
	    msg.setParam("error","failure");
	    return false;
	}
    }

    bool online = !(checkCalled && called.resource().null());
    bool local = (caller.domain() == called.domain());
    if (!online) {
	bool reqSub = false;
	// Get a resource
	// Synchronous probe targets (try to get resource and caps from stored data)
	Message* m = plugin.message("resource.notify");
	m->addParam("operation","probe");
	m->addParam("from",caller.bare());
	m->addParam("to",called.bare());
	m->addParam("to_local",String::boolText(local));
	m->addParam("sync",String::boolText(true));
	bool ok = Engine::dispatch(m);
	if (ok) {
	    int n = m->getIntValue("instance.count");
	    DDebug(this,DebugAll,"Checking %d instances for call from %s to %s",
		n,caller.c_str(),called.c_str());
	    String prefix("instance.");
	    for (int i = 1; i <= n; i++) {
		// TODO: avoid our own resources
		String pref(prefix + String(i));
		String* inst = m->getParam(pref);
		if (TelEngine::null(inst))
		    continue;
		pref << ".";
		bool cap = false;
		if (!file)
		    cap = m->getBoolValue(pref + "caps.audio");
		else
		    cap = m->getBoolValue(pref + "caps.filetransfer");
		if (!cap)
		    continue;
		called.resource(*inst);
		// Copy caps
		unsigned int count = m->count();
		String p(pref + "caps.");
		for (unsigned int j = 0; j < count; j++) {
		    NamedString* ns = m->getParam(j);
		    if (ns && ns->name().startsWith(p))
			caps.addParam(ns->name().substr(pref.length()),*ns);
		}
	    }
	    if (!called.resource())
		reqSub = s_requestSubscribe;
	}
	else
	    reqSub = s_serverMode && s_requestSubscribe;
	TelEngine::destruct(m);

	if (called.resource()) {
	    online = true;
	    Debug(this,DebugAll,"Found resource '%s' for called '%s'",
		called.resource().c_str(),called.bare().c_str());
	}
	else if (reqSub) {
	    Message* m = plugin.message("resource.subscribe");
	    m->addParam("operation","subscribe");
	    m->addParam("subscriber",caller.bare());
	    m->addParam("notifier",called.bare());
	    Engine::enqueue(m);
	}
	else {
	    Debug(this,DebugNote,"Jingle call failed. No resource available for called party");
	    msg.setParam("error","offline");
	    return false;
	}
    }

    // Lock driver to prevent probe response to be processed before the channel
    //  is fully built
    Lock lock(this);
    Debug(this,DebugAll,
	"msgExecute. caller='%s' called='%s' online=%s filetransfer=%s",
	caller.c_str(),called.c_str(),String::boolText(online),
	String::boolText(!file.null()));
    YJGConnection* conn = new YJGConnection(msg,caller,called,online,caps,file,localip);
    conn->initChan();
    bool ok = conn->state() != YJGConnection::Terminated;
    lock.drop();
    if (ok) {
	CallEndpoint* ch = YOBJECT(CallEndpoint,msg.userData());
	if (ch && conn->connect(ch,msg.getValue("reason"))) {
	    conn->callConnect(msg);
	    msg.setParam("peerid",conn->id());
	    msg.setParam("targetid",conn->id());
	}
    }
    else {
	Debug(this,DebugNote,"Jingle call failed to initialize error=%s",
	    conn->reason().c_str());
	msg.setParam("error","failure");
    }
    TelEngine::destruct(conn);
    return ok;
}

// Message handler: Disconnect channels, destroy streams, clear rosters
bool YJGDriver::received(Message& msg, int id)
{
    if (id == MsgExecute)
	return !isModule(msg) && handleImExecute(msg);
    if (id == Execute) {
	// Client only: handle call.execute with target starting jabber/
	if (s_serverMode)
	    return Driver::received(msg,id);
	String callto(msg.getValue("callto"));
	if (!callto.startSkip("jabber/",false))
	    return Driver::received(msg,id);
	return msgExecute(msg,callto);
    }
    if (id == Halt) {
	// Uninstall message handlers
	for (ObjList* o = m_handlers.skipNull(); o; o = o->skipNext()) {
	    YJGMessageHandler* h = static_cast<YJGMessageHandler*>(o->get());
	    Engine::uninstall(h);
	}
	dropAll(msg);
    }
    return Driver::received(msg,id);
}

// Handle jabber.iq messages
bool YJGDriver::handleJabberIq(Message& msg)
{
    JabberID to(msg.getValue("to"));
    if (s_serverMode && !(to.domain() && handleDomain(to.domain())))
	return false;
    if (to && !to.resource())
	to.resource(msg.getValue("to_instance"));
    const char* xmlns = msg.getValue("xmlns");
    bool session = false;
    bool discoInfo = false;
    bool discoItems = false;
    XMPPUtils::IqType t = XMPPUtils::iqType(msg.getValue("type"));
    // Let the jingle sessions match responses
    // Check handled namespaces if the iq is not an error or result
    if (t != XMPPUtils::IqResult && t != XMPPUtils::IqError &&
	!TelEngine::null(xmlns)) {
	int t = XMPPUtils::s_ns[xmlns];
	session = (t == XMPPNamespace::Jingle || t == XMPPNamespace::JingleSession ||
	    t == XMPPNamespace::ByteStreams);
	discoInfo = !session && (t == XMPPNamespace::DiscoInfo);
	discoItems = !(session || discoInfo) && (t == XMPPNamespace::DiscoItems);
	if (!(session || discoInfo || discoItems))
	    return false;
    }

    // No disco: check 'to' resource
    if (!(discoInfo || discoItems || (to.resource() && handleResource(to.resource()))))
	return false;

    XmlElement* xml = XMPPUtils::getXml(msg,"xml",0);
    if (!xml) {
	DDebug(this,DebugAll,"handleJabberIq() no xml element");
	return false;
    }
    JabberID from(msg.getValue("from"));
    if (!from.resource())
	from.resource(msg.getValue("from_instance"));

    DDebug(this,DebugAll,"handleJabberIq() from=%s to=%s xmlns=%s",
	from.c_str(),to.c_str(),xmlns);

    if (discoInfo || discoItems) {
	XmlElement* rsp = 0;
	const char* id = msg.getValue("id");
	XmlElement* query = XMPPUtils::findFirstChild(*xml,XmlTag::Query);
	String node = query ? query->attribute("node") : 0;
	if (TelEngine::null(node)) {
	    if (discoInfo)
		rsp = m_features.buildDiscoInfo(0,0,id);
	    else
		rsp = XMPPUtils::createIqDisco(false,false,0,0,id);
	}
	else {
	    // Disco info to our node#hash
	    if (discoInfo) {
		if (node == s_capsNode)
		    rsp = m_features.buildDiscoInfo(0,0,id,node);
		else {
		    int pos = node.find("#");
		    if (pos > 0 && node.substr(0,pos) == s_capsNode &&
			node.substr(pos + 1) == m_features.m_entityCapsHash)
			rsp = m_features.buildDiscoInfo(0,0,id,node);
		}
	    }
	    if (!rsp)
		rsp = XMPPUtils::createIqDisco(discoInfo,false,0,0,id,node);
	}
	TelEngine::destruct(xml);
	msg.setParam(new NamedPointer("response",rsp));
	return true;
    }

    XMPPError::Type error = XMPPError::NoError;
    String text;
    const String* id = msg.getParam("id");
    bool ok = s_jingle->acceptIq(t,from,to,id ? *id : String::empty(),xml,
	msg.getValue("line"),error,text);
    if (ok || error != XMPPError::NoError) {
	msg.setParam("respond",String::boolText(!ok));
	if (!ok) {
	    xml = XMPPUtils::createIqError(0,0,xml,XMPPError::TypeModify,error,text);
	    msg.setParam(new NamedPointer("response",xml));
	}
	return true;
    }
    // Put back the xml into the message
    msg.setParam(new NamedPointer("xml",xml));
    return false;
}

// Handle resource.notify messages
bool YJGDriver::handleResNotify(Message& msg)
{
    String* oper = msg.getParam("operation");
    if (TelEngine::null(oper))
	return false;
    // online/offline
    bool online = (*oper == "update" || *oper == "online");
    if (online  || *oper == "delete" || *oper == "offline") {
	JabberID remote(msg.getValue("contact"));
	// Add jingle caps for serviced domains if requested
	if (msg.getBoolValue("addjinglecaps") && handleDomain(remote.domain())) {
	    XmlElement* xml = YOBJECT(XmlElement,msg.getParam("xml"));
	    String* data = !xml ? msg.getParam("data") : 0;
	    XmlElement* dataXml = data ? XMPPUtils::getXml(*data) : 0;
	    if (xml || dataXml) {
		XmlElement* target = xml ? xml : dataXml;
		// Add entity caps if not already there
		if (!XMPPUtils::findFirstChild(*target,XmlTag::EntityCapsTag,
		    XMPPNamespace::EntityCaps)) {
		    target->addChild(new XmlElement(*m_entityCaps));
		    target->addChild(XMPPUtils::createEntityCapsGTalkV1(s_capsNode));
		    // Restore the data parameter
		    if (dataXml) {
			data->clear();
			dataXml->toString(*data);
		    }
		    msg.clearParam("addjinglecaps");
		}
		TelEngine::destruct(dataXml);
	    }
	}
	JabberID local;
	if (remote)
	    remote.resource(msg.getValue("instance"));
	else {
	    local.set(msg.getValue("to"));
	    Lock lock(this);
	    if (!handleDomain(local.domain()))
		return false;
	    lock.drop();
	    if (!local.resource())
		local.resource(msg.getValue("to_instance"));
	    remote.set(msg.getValue("from"));
	    if (!remote.resource())
		remote.resource(msg.getValue("from_instance"));
	}
	DDebug(this,DebugAll,"handleResNotify(%u) from=%s to=%s",
	    online,remote.c_str(),local.c_str());
	if (!remote)
	    return false;
	if (online) {
	    if (!remote.resource())
		return false;
	    Lock lock(this);
	    for (ObjList* o = channels().skipNull(); o; o = o->skipNext()) {
		YJGConnection* conn = static_cast<YJGConnection*>(o->get());
		if (conn->state() != YJGConnection::Pending)
		    continue;
		if (remote.bare() != conn->remote().bare())
		    continue;
		if (!local || conn->local().match(local)) {
		    conn->updateResource(remote.resource());
		    if (conn->presenceChanged(true,&msg))
			conn->disconnect(0);
		}
	    }
	    return false;
	}
	// Offline
	// Remote user is unavailable: notify all connections
	// Remote has no resource: match connections by bare jid
	Lock lock(this);
	for (ObjList* o = channels().skipNull(); o; o = o->skipNext()) {
	    YJGConnection* conn = static_cast<YJGConnection*>(o->get());
	    if (conn->remote().match(remote) && (!local ||
		local.bare() != conn->local().bare())) {
		if (conn->presenceChanged(false))
		    conn->disconnect(0);
	    }
	}
	return false;
    }
    String* src = msg.getParam("from");
    String* dest = msg.getParam("to");
    if (TelEngine::null(src) || TelEngine::null(dest))
	return false;
    // (un)subscribed
    bool sub = (*oper == "subscribed");
    if (sub || *oper == "unsubscribed") {
	// We are not interested in 'unsubscribed'
	if (!sub)
	    return false;

	return false;
    }
    // probe
    if (*oper == "probe") {
	if (!s_autoSubscribe)
	    return false;
	JabberID to(msg.getValue("to"));
	if (!to || !s_serverMode || !handleDomain(to.domain()))
	    return false;
	DDebug(this,DebugAll,"handleResNotify(probe) from=%s to=%s",
	    msg.getValue("from"),to.c_str());
	notifyPresence(to,msg.getValue("from"),true);
	return false;
    }
    return false;
}

// Handle resource.subscribe messages
bool YJGDriver::handleResSubscribe(Message& msg)
{
    if (!s_autoSubscribe)
	return false;
    String* oper = msg.getParam("operation");
    if (TelEngine::null(oper))
	return false;
    bool sub = (*oper == "subscribe");
    if (!sub && *oper != "unsubscribe")
	return false;
    JabberID notifier(msg.getValue("notifier"));
    if (!notifier || !s_serverMode || !handleDomain(notifier.domain()))
	return false;
    JabberID subscriber(msg.getValue("subscriber"));
    if (!subscriber)
	return false;
    subscriber.resource();
    DDebug(this,DebugAll,"handleResSubscribe(%s) from %s to %s",oper->c_str(),
	subscriber.c_str(),notifier.c_str());
    Message* m = message("resource.notify");
    m->addParam("from",notifier.bare());
    m->addParam("to",subscriber.bare());
    m->addParam("operation",sub ? "subscribed" : "unsubscribed");
    bool ok = Engine::enqueue(m);
    if (ok)
	notifyPresence(notifier,subscriber,sub);
    return ok;
}

// Handle user.notify messages
bool YJGDriver::handleUserNotify(Message& msg)
{
    if (!Engine::clientMode() || msg.getBoolValue("registered"))
	return false;
    // Local account is offline: disconnect it
    JabberID jid(msg.getValue("jid"));
    DDebug(this,DebugAll,"handleUserNotify(offline) jid=%s",jid.c_str());
    Lock lock(this);
    for (ObjList* o = channels().skipNull(); o; o = o->skipNext()) {
	YJGConnection* conn = static_cast<YJGConnection*>(o->get());
	if (jid == conn->local())
	    conn->disconnect("unregistered");
    }
    return false;
}

// Handle chan.notify messages
bool YJGDriver::handleChanNotify(Message& msg)
{
    String* chan = msg.getParam("notify");
    YJGConnection* ch = chan ? findChan(*chan) : 0;
    if (!ch)
	return false;
    ch->processChanNotify(msg);
    if (ch->state() == YJGConnection::Terminated)
	ch->disconnect(0);
    TelEngine::destruct(ch);
    return true;
}

// Handle msg.execute message
// Send chan.text message if enabled
bool YJGDriver::handleImExecute(Message& msg)
{
    if (!s_imToChanText)
	return false;
    // Set local (target) from callto/called parameter
    JabberID local;
    String* callto = msg.getParam("callto");
    if (TelEngine::null(callto))
	local.set(msg.getValue("called"));
    else if (callto->startsWith(prefix()))
	local.set(callto->substr(prefix().length()));
    else
        return false;
    if (!local)
	return false;
    if (!local.resource())
	local.resource(msg.getValue("called_instance"));
    Message* m = 0;
    Lock lock(this);
    // Check if target is in our domain(s)
    if (!(local.node() && handleDomain(local.domain())))
	return false;
    JabberID remote(msg.getValue("caller"));
    if (!remote.resource())
	remote.resource(msg.getValue("caller_resource"));
    if (!remote)
	return false;
    // NOTE: broadcast chat to all channels matching the bare jid if local resource is empty ?
    YJGConnection* conn = findByJid(local,remote);
    if (conn) {
	DDebug(this,DebugInfo,"Found conn=(%p,%s) for message from=%s to=%s",
	    conn,conn->debugName(),remote.c_str(),local.c_str());
	m = conn->message("chan.text");
    }
    lock.drop();
    if (m) {
	m->addParam("text",msg.getValue("body"));
	Engine::enqueue(m);
    }
    return m != 0;
}

// Handle engine.start message
void YJGDriver::handleEngineStart(Message& msg)
{
    setDomains(s_cfg.getValue("general","domains"));
}

// Find a connection by local and remote jid, optionally ignore local
// resource (always ignore if local has no resource)
YJGConnection* YJGDriver::findByJid(const JabberID& local, const JabberID& remote,
    bool anyResource)
{
    if (local.bare() == local)
	anyResource = true;
    ObjList* obj = channels().skipNull();
    for (; obj; obj = obj->skipNext()) {
	YJGConnection* conn = static_cast<YJGConnection*>(obj->get());
	if (!conn->remote().match(remote))
	    continue;
	if (anyResource) {
	    if (local.bare() == conn->local().bare())
		return conn;
	}
	else if (conn->local().match(local))
	    return conn;
    }
    return 0;
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

// Notify presence
void YJGDriver::notifyPresence(const JabberID& from, const char* to, bool online)
{
    if (!from)
	return;
    Lock lock(this);
    if (!handleDomain(from.domain()))
	return;
    for (ObjList* o = m_resources.skipNull(); o; o = o->skipNext()) {
	String* res = static_cast<String*>(o->get());
	if (!from.resource() || *res == from.resource()) {
	    Message* m = message("resource.notify");
	    m->addParam("from",from.bare());
	    m->addParam("to",to);
	    m->addParam("from_instance",*res);
	    m->addParam("operation",online ? "online" : "offline");
	    if (online) {
		XmlElement* xml = XMPPUtils::createPresence(0,0);
		XMPPUtils::setPriority(*xml,String(s_priority));
		xml->addChild(XMPPUtils::createEntityCapsGTalkV1(s_capsNode));
		xml->addChild(new XmlElement(*m_entityCaps));
		m->addParam(new NamedPointer("xml",xml));
	    }
	    Engine::enqueue(m);
	    if (from.resource())
		break;
	}
    }
}

// Build and dispatch a 'jabber.account' message. Returns it on success
Message* YJGDriver::checkAccount(const String& line, bool query,
    const JabberID* contact) const
{
    if (!line)
	return 0;
    Message* m = message("jabber.account");
    m->addParam("line",line);
    if (query)
	m->addParam("query",String::boolText(true));
    if (contact) {
	m->addParam("contact",contact->bare());
	if (contact->resource())
	    m->addParam("instance",contact->resource());
    }
    if (!Engine::dispatch(m))
	TelEngine::destruct(m);
    return m;
}

// Update the list of domains
void YJGDriver::setDomains(const String& list)
{
    Lock lock(this);
    ObjList* l = list.split(',',false);
    // Notify the domains not serviced anymore
    ObjList* o = m_domains.skipNull();
    while (o) {
	String* old = static_cast<String*>(o->get());
	if (!l->find(*old)) {
	    for (ObjList* ores = m_resources.skipNull(); ores; ores = ores->skipNext()) {
		Message* m = message("jabber.item");
		m->addParam("jid",*old + "/" + *static_cast<String*>(ores->get()));
		m->addParam("remove",String::boolText(true));
		Engine::enqueue(m);
	    }
	    o->remove();
	    o = o->skipNull();
	}
	else
	    o = o->skipNext();
    }
    // Notify the new domains
    for (o = l->skipNull(); o; o = o->skipNext()) {
	String* d = static_cast<String*>(o->get());
	if (m_domains.find(*d))
	    continue;
	m_domains.append(new String(*d));
	for (ObjList* ores = m_resources.skipNull(); ores; ores = ores->skipNext()) {
	    Message* m = message("jabber.item");
	    m->addParam("jid",*d + "/" + *static_cast<String*>(ores->get()));
	    Engine::enqueue(m);
	}
    }
    TelEngine::destruct(l);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
