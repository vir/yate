/**
 * yatesdp.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * SDP media handling
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

#ifndef __YATESDP_H
#define __YATESDP_H

#ifndef __cplusplus
#error C++ is required
#endif

#include <yatemime.h>
#include <yatephone.h>

#ifdef _WINDOWS

#ifdef LIBYSDP_EXPORTS
#define YSDP_API __declspec(dllexport)
#else
#ifndef LIBYSDP_STATIC
#define YSDP_API __declspec(dllimport)
#endif
#endif

#endif /* _WINDOWS */

#ifndef YSDP_API
#define YSDP_API
#endif

/**
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

class SDPMedia;
class SDPSession;
class SDPParser;

/**
 * This class holds a single SDP media description
 * @short SDP media description
 */
class YSDP_API SDPMedia : public NamedList
{
public:
    /**
     * Constructor
     * @param media Media type name
     * @param transport Transport name
     * @param formats Comma separated list of formats
     * @param rport Optional remote media port
     * @param lport Optional local media port
     */
    SDPMedia(const char* media, const char* transport, const char* formats,
	int rport = -1, int lport = -1);

    /**
     * Destructor
     */
    virtual ~SDPMedia();

    /**
     * Check if this media type is audio
     * @return True if this media describe an audio one
     */
    inline bool isAudio() const
	{ return m_audio; }

    /**
     * Check if this media type is video
     * @return True if this media describe a video one
     */
    inline bool isVideo() const
	{ return m_video; }

    /**
     * Check if a media parameter changed
     * @return True if a media changed
     */
    inline bool isModified() const
	{ return m_modified; }

    /**
     * Set or reset media parameter changed flag
     * @param modified The new value of the media parameter changed flag
     */
    inline void setModified(bool modified = true)
	{ m_modified = modified; }

    /**
     * Retrieve the media suffix (built from type)
     * @return Media suffix
     */
    inline const String& suffix() const
	{ return m_suffix; }

    /**
     * Retrieve the media transport name
     * @return The media transport name
     */
    inline const String& transport() const
	{ return m_transport; }

    /**
     * Retrieve the media id
     * @return The media id
     */
    inline const String& id() const
	{ return m_id; }

    /**
     * Retrieve the current media format
     * @return The current media format
     */
    inline const String& format() const
	{ return m_format; }

    /**
     * Retrieve the formats set for this media
     * @return Comma separated list of media formats
     */
    inline const String& formats() const
	{ return m_formats; }

    /**
     * Retrieve the remote media port
     * @return The remote media port
     */
    inline const String& remotePort() const
	{ return m_rPort; }

    /**
     * Retrieve the local media port
     * @return The local media port
     */
    inline const String& localPort() const
	{ return m_lPort; }

    /**
     * Retrieve rtp payload mappings
     * @return Rtp payload mappings
     */
    inline const String& mappings() const
	{ return m_mappings; }

    /**
     * Set rtp payload mappings for this media
     * @param newMap New rtp payload mappings
     */
    inline void mappings(const char* newMap)
	{ if (newMap) m_mappings = newMap; }

    /**
     * Retrieve RFC2833 status or payload of this media
     * @return RFC2833 status or payload of this media
     */
    inline const String& rfc2833() const
	{ return m_rfc2833; }

    /**
     * Set RFC2833 status or payload of this media
     * @param payload SDP numeric payload to set.
     *  Set it to a negative value to reset RFC2833
     */
    inline void rfc2833(int payload)
	{
	    if (payload >= 0)
		m_rfc2833 = payload;
	    else
		m_rfc2833 = String::boolText(false);
	}

    /**
     * Retrieve remote crypto description
     * @return Remote crypto description
     */
    inline const String& remoteCrypto() const
	{ return m_rCrypto; }

    /**
     * Retrieve local crypto description
     * @return Local crypto description
     */
    inline const String& localCrypto() const
	{ return m_lCrypto; }

    /**
     * Check if this media is securable
     * @return True if this media is securable
     */
    inline bool securable() const
	{ return m_securable; }

    /**
     * Compare this media with another one
     * @param other The media to compare with
     * @param ignorePort Ignore differences caused only by port number
     * @return True if both media have the same formats, transport and remote port
     */
    inline bool sameAs(const SDPMedia* other, bool ignorePort = false) const
	{ return other && (other->formats() == m_formats) &&
	  (other->transport() == m_transport) &&
	  ((ignorePort && other->remotePort() && m_rPort) ||
	   (other->remotePort() == m_rPort)); }

    /**
     * Check if local part of this media changed
     * @return True if local part of this media changed
     */
    inline bool localChanged() const
	{ return m_localChanged; }

    /**
     * Set or reset local media changed flag
     * @param chg The new value for local media changed flag
     */
    inline void setLocalChanged(bool chg = false)
	{ m_localChanged = chg; }

    /**
     * Retrieve a formats list from this media
     * @return Comma separated list of media formats (from formats list,
     *  current format or a default G711, 'alaw,mulaw', list
     */
    const char* fmtList() const;

    /**
     * Update this media from formats and ports
     * @param formats New media formats
     * @param rport Optional remote media port
     * @param lport Optional local media port
     * @param force Force updating formats even if incompatible with old ones
     * @return True if media changed
     */
    bool update(const char* formats, int rport = -1, int lport = -1, bool force = false);

    /**
     * Update from a chan.rtp message (rtp id and local port)
     * @param msg The list of parameters
     * @param pickFormat True to update media format(s) from the list
     */
    void update(const NamedList& msg, bool pickFormat);

    /**
     * Add or replace a parameter by name and value, set the modified flag
     * @param name Parameter name
     * @param value Parameter value
     * @param append True to append, false to replace
     */
    void parameter(const char* name, const char* value, bool append);

    /**
     * Add or replace a parameter, set the modified flag
     * @param param The parameter
     * @param append True to append, false to replace
     */
    void parameter(NamedString* param, bool append);

    /**
     * Set a new crypto description, set the modified flag if changed.
     * Reset the media securable flag if the remote crypto is empty
     * @param desc The new crypto description
     * @param remote True to set the remote crypto, false to set the local one
     */
    void crypto(const char* desc, bool remote);

    /**
     * Put this net media in a parameter list
     * @param msg Destination list
     * @param putPort True to add remote media port
     */
    void putMedia(NamedList& msg, bool putPort = true);

private:
    bool m_audio;
    bool m_video;
    bool m_modified;
    bool m_securable;
    // local rtp data changed flag
    bool m_localChanged;
    // suffix used for this type
    String m_suffix;
    // transport protocol
    String m_transport;
    // list of supported format names
    String m_formats;
    // format used for sending data
    String m_format;
    // id of the local media channel
    String m_id;
    // remote media port
    String m_rPort;
    // mappings of RTP payloads
    String m_mappings;
    // local media port
    String m_lPort;
    // payload for telephone/event
    String m_rfc2833;
    // remote security descriptor
    String m_rCrypto;
    // local security descriptor
    String m_lCrypto;
};


/**
 * This class holds RTP/SDP data for multiple media types
 * NOTE: The SDPParser pointer held by this class is assumed to be non NULL
 * @short A holder for a SDP session
 */
class YSDP_API SDPSession
{
public:
    /**
     * RTP media status enumeration
     */
    enum {
	MediaMissing,
	MediaStarted,
	MediaMuted
    };

    /**
     * Constructor
     * @param parser The SDP parser whose data this object will use
     */
    SDPSession(SDPParser* parser);

    /**
     * Constructor
     * @param parser The SDP parser whose data this object will use
     * @param params SDP session parameters
     */
    SDPSession(SDPParser* parser, NamedList& params);

    /**
     * Destructor. Reset the object
     */
    virtual ~SDPSession();

    /**
     * Get RTP local host
     * @return RTP local host
     */
    inline const String& getHost() const
	{ return m_host; }

    /**
     * Get local RTP address
     * @return Local RTP address (external or local)
     */
    inline const String& getRtpAddr() const
	{ return m_externalAddr ? m_externalAddr : m_rtpLocalAddr; }

    /**
     * Set a new media list
     * @param media New media list
     * @return True if media changed
     */
    bool setMedia(ObjList* media);

    /**
     * Put specified media parameters into a list of parameters
     * @param msg Destination list
     * @param media List of SDP media information
     * @param putPort True to add the media port
     */
    static void putMedia(NamedList& msg, ObjList* media, bool putPort = true);

    /**
     * Put session media parameters into a list of parameters
     * @param msg Destination list
     * @param putPort True to add the media port
     */
    inline void putMedia(NamedList& msg, bool putPort = true)
	{ putMedia(msg,m_rtpMedia,putPort); }

    /**
     * Retrieve a single media description
     * @param name Name of the media to retrieve
     * @return Pointer to media descriptor, NULL if no such media set
     */
    SDPMedia* getMedia(const String& name) const
	{ return m_rtpMedia ? static_cast<SDPMedia*>((*m_rtpMedia)[name]) : 0; }

    /**
     * Update the RFC 2833 availability and payload
     * @param value String to get payload or availability
     */
    void setRfc2833(const String& value);

    /**
     * Update the RFC 2833 availability and payload
     * @param value Pointer to string to get payload or availability
     */
    inline void setRfc2833(const String* value)
	{ if (value) setRfc2833(*value); }

    /**
     * Build and dispatch a chan.rtp message for a given media. Update media on success
     * @param media The media to use
     * @param addr Remote RTP address
     * @param start True to request RTP start
     * @param pick True to update local parameters (other then media) from returned message
     * @param context Pointer to user provided context, optional
     * @return True if the message was succesfully handled
     */
    bool dispatchRtp(SDPMedia* media, const char* addr,	bool start, bool pick, RefObject* context = 0);

    /**
     * Calls dispatchRtp() for each media in the list
     * Update it on success. Remove it on failure
     * @param addr Remote RTP address
     * @param start True to request RTP start
     * @param context Pointer to user provided context, optional
     * @return True if the message was succesfully handled for at least one media
     */
    bool dispatchRtp(const char* addr, bool start, RefObject* context = 0);

    /**
     * Try to start RTP (calls dispatchRtp()) for each media in the list
     * @param context Pointer to user provided context, optional
     * @return True if at least one media was started
     */
    bool startRtp(RefObject* context = 0);

    /**
     * Update from parameters. Build a default SDP from parser formats if no media is found in params
     * @param params List of parameters to update from
     * @return True if media changed
     */
    bool updateSDP(const NamedList& params);

    /**
     * Update RTP/SDP data from parameters
     * @param params List of parameters to update from
     * @return True if media or local address changed
     */
    bool updateRtpSDP(const NamedList& params);

    /**
     * Creates a SDP body from transport address and list of media descriptors
     * @param addr The address to set. Use own host if empty
     * @param mediaList Optional media list. Use own list if the given one is 0
     * @return MimeSdpBody pointer or 0 if there is no media to set
     */
    MimeSdpBody* createSDP(const char* addr, ObjList* mediaList = 0);

    /**
     * Creates a SDP body for current media status
     * @return MimeSdpBody pointer or 0 if media is missing
     */
    MimeSdpBody* createSDP();

    /**
     * Creates a SDP from RTP address data present in message.
     * Use the raw SDP if present.
     * @param msg The list of parameters
     * @param update True to update RTP/SDP data if raw SDP is not found in the list
     * @param allowEmptyAddr Allow empty address in parameters (default: false)
     * @return MimeSdpBody pointer or 0
     */
    MimeSdpBody* createPasstroughSDP(NamedList& msg, bool update = true,
	bool allowEmptyAddr = false);

    /**
     * Creates a set of unstarted external RTP channels from remote addr and
     *  builds SDP from them
     * @param addr Remote RTP address used when dispatching the chan.rtp message
     * @param msg List of parameters used to update data
     * @return MimeSdpBody pointer or 0
     */
    inline MimeSdpBody* createRtpSDP(const char* addr, const NamedList& msg)
	{ updateSDP(msg); return createRtpSDP(addr,false); }

    /**
     * Creates a set of RTP channels from address and media info and builds SDP from them
     * @param addr Remote RTP address used when dispatching the chan.rtp message
     * @param start True to create a started RTP
     * @return MimeSdpBody pointer or 0
     */
    inline MimeSdpBody* createRtpSDP(const char* addr, bool start)
	{ return dispatchRtp(addr,start) ? createSDP(getRtpAddr()) : 0; }

    /**
     * Creates a set of started external RTP channels from remote addr and
     *  builds SDP from them
     * @param start True to create a started RTP
     * @return MimeSdpBody pointer or 0
     */
    inline MimeSdpBody* createRtpSDP(bool start)
	{
	    if (m_rtpAddr.null()) {
		m_mediaStatus = MediaMuted;
		return createSDP(0);
	    }
	    return createRtpSDP(m_rtpAddr,start);
	}

    /**
     * Update media format lists from parameters
     * @param msg Parameter list
     * @param changeMedia True to update media list if required
     */
    void updateFormats(const NamedList& msg, bool changeMedia = false);

    /**
     * Add raw SDP forwarding parameter from body if SDP forward is enabled
     * @param msg Destination list
     * @param body Mime body to process
     * @return True if the parameter was added
     */
    bool addSdpParams(NamedList& msg, const MimeBody* body);

    /**
     * Add raw SDP forwarding parameter if SDP forward is enabled
     * @param msg Destination list
     * @param rawSdp The raw sdp content
     * @return True if the parameter was added
     */
    bool addSdpParams(NamedList& msg, const String& rawSdp);

    /**
     * Add RTP forwarding parameters to a message (media and address)
     * @param msg Destination list
     * @param natAddr Optional NAT address if detected
     * @param body Pointer to the body to extract raw SDP from
     * @param force True to override RTP forward flag
     * @param allowEmptyAddr Allow empty address in parameters (default: false)
     * @return True if RTP data was added. Media is always added if present and
     *  remote address is not empty
     */
    bool addRtpParams(NamedList& msg, const String& natAddr = String::empty(),
	const MimeBody* body = 0, bool force = false, bool allowEmptyAddr = false);

    /**
     * Reset this object to default values
     * @param all True to reset all parameters including configuration
     */
    virtual void resetSdp(bool all = true);

    /**
     * Build a chan.rtp message and populate with media information
     * @param media The media list
     * @param addr Remote RTP address
     * @param start True to request RTP start
     * @param context Pointer to reference counted user provided context
     * @return The message with media information, NULL if media or addr are missing
     */
    virtual Message* buildChanRtp(SDPMedia* media, const char* addr, bool start, RefObject* context);

    /**
     * Build a chan.rtp message without media information
     * @param context Pointer to reference counted user provided context
     * @return The message with user data set but no media information
     */
    virtual Message* buildChanRtp(RefObject* context) = 0;

    /**
     * Check if local RTP data changed for at least one media
     * @return True if local RTP data changed for at least one media
     */
    bool localRtpChanged() const;

    /**
     * Set or reset the local RTP data changed flag for all media
     * @param chg The new value for local RTP data changed flag of all media
     */
    void setLocalRtpChanged(bool chg = false);

    /**
     * Update RTP/SDP data from parameters
     * @param params Parameter list
     * @param rtpAddr String to be filled with rtp address from the list
     * @param oldList Optional existing media list (found media will be removed
     *  from it and added to the returned list
     * @param allowEmptyAddr Allow empty address in parameters (default: false)
     * @return List of media or 0 if not found or rtpAddr is empty
     */
    static ObjList* updateRtpSDP(const NamedList& params, String& rtpAddr,
	ObjList* oldList = 0, bool allowEmptyAddr = false);

protected:
    /**
     * Media changed notification.
     * This method is called when setting new media and an old one changed
     * @param media Old media that changed
     */
    virtual void mediaChanged(const SDPMedia& media);

    /**
     * Dispatch rtp notification.
     * This method is called before dispatching the message.
     * Clear the message to stop dispatch
     * @param msg Message to dispatch
     * @param media Media for which the message is going to be dispatched
     */
    virtual void dispatchingRtp(Message*& msg, SDPMedia* media);

    /**
     * Set data used in debug
     * @param enabler The DebugEnabler to use (0 to to use the parser)
     * @param ptr Pointer to print, 0 to use the session pointer
     */
    void setSdpDebug(DebugEnabler* enabler = 0, void* ptr = 0);

    /**
     * Print current media to output
     * @param reason Reason to print
     */
    void printRtpMedia(const char* reason);

    SDPParser* m_parser;
    int m_mediaStatus;
    bool m_rtpForward;                   // Forward RTP flag
    bool m_sdpForward;                   // Forward SDP (only if RTP is forwarded)
    String m_originAddr;                 // Our SDP origin address
    String m_externalAddr;               // Our external IP address, possibly outside of a NAT
    String m_rtpAddr;                    // Remote RTP address
    String m_rtpLocalAddr;               // Local RTP address
    String m_rtpNatAddr;                 // Advertised local IP in sdp (override any local IP)
    ObjList* m_rtpMedia;                 // List of media descriptors
    int m_sdpSession;                    // Unique SDP session number
    int m_sdpVersion;                    // SDP version number, incremented each time we generate a new SDP
    unsigned int m_sdpHash;              // SDP content hash
    String m_host;
    bool m_secure;
    int m_rfc2833;                       // Payload of RFC 2833 for remote party
    bool m_ipv6;                         // IPv6 support

private:
    DebugEnabler* m_enabler;             // Debug enabler used for output
    void* m_ptr;                         // Pointer to show in debug messages
};

/**
 * This class holds a SDP parser and additional data used by SDP objects
 * @short A SDP parser
 */
class YSDP_API SDPParser : public DebugEnabler, public Mutex
{
    friend class SDPSession;

public:
    /**
     * Constructor
     * @param dbgName Debug name of this parser
     * @param sessName Name of the session in SDP
     * @param fmts Default media formats
     */
    inline SDPParser(const char* dbgName, const char* sessName, const char* fmts = "alaw,mulaw")
	: Mutex(true,"SDPParser"),
	  m_rfc2833(101),
	  m_sdpForward(false), m_secure(false), m_ignorePort(false),
	  m_sessionName(sessName), m_audioFormats(fmts),
	  m_codecs(""), m_hacks("")
	{ debugName(dbgName); }

    /**
     * Get the formats list
     * This method is thread safe
     * @param buf String to be filled with comma separated list of formats
     */
    inline void getAudioFormats(String& buf)
	{ Lock lock(this); buf = m_audioFormats; }

    /**
     * Get the RFC 2833 offer payload
     * @return Payload for RFC 2883 telephony events, negative if not offered
     */
    inline int rfc2833() const
	{ return m_rfc2833; }

    /**
     * Get the secure offer flag
     * @return True if SDES descriptors for SRTP will be offered
     */
    inline bool secure() const
	{ return m_secure; }

    /**
     * Get the SDP forward flag
     * @return True if raw SDP should be added to RTP forward offer
     */
    inline bool sdpForward() const
	{ return m_sdpForward; }

    /**
     * Get the RTP port change ignore flag
     * @return True if a port change should not cause an offer change
     */
    inline bool ignorePort() const
	{ return m_ignorePort; }

    /**
     * Parse a received SDP body
     * This method is thread safe
     * @param sdp Received SDP body
     * @param addr Remote address
     * @param oldMedia Optional list of existing media (an already existing media
     *  will be moved to returned list)
     * @param media Optional expected media type. If not empty this will be the
     *  only media type returned (if found)
     * @param force Force updating formats even if incompatible with old ones
     * @return List of SDPMedia objects, may be NULL
     */
    ObjList* parse(const MimeSdpBody& sdp, String& addr, ObjList* oldMedia = 0,
	const String& media = String::empty(), bool force = false);

    /**
     * Parse a received SDP body, returns NULL if SDP is not present
     * This method is thread safe
     * @param sdp Pointer to received SDP body
     * @param addr Remote address
     * @param oldMedia Optional list of existing media (an already existing media
     *  will be moved to returned list)
     * @param media Optional expected media type. If not empty this will be the
     *  only media type returned (if found)
     * @param force Force updating formats even if incompatible with old ones
     * @return List of SDPMedia objects, may be NULL
     */
    inline ObjList* parse(const MimeSdpBody* sdp, String& addr, ObjList* oldMedia = 0,
	const String& media = String::empty(), bool force = false)
	{ return sdp ? parse(*sdp,addr,oldMedia,media,force) : 0; }

    /**
     * Update configuration. This method should be called after a configuration file is loaded
     * @param codecs List of supported codecs
     * @param hacks List of hacks
     * @param general List of general settings
     */
    void initialize(const NamedList* codecs, const NamedList* hacks, const NamedList* general = 0);

    /**
     * Yate Payloads for the AV profile
     */
    static const TokenDict s_payloads[];

    /**
     * SDP Payloads for the AV profile
     */
    static const TokenDict s_rtpmap[];

private:
    int m_rfc2833;                       // RFC 2833 payload offered to remote
    bool m_sdpForward;                   // Include raw SDP for forwarding
    bool m_secure;                       // Offer SRTP
    bool m_ignorePort;                   // Ignore port only changes in SDP
    String m_sessionName;
    String m_audioFormats;               // Default audio formats to be advertised to remote party
    NamedList m_codecs;                  // Codecs configuration list
    NamedList m_hacks;                   // Various potentially standard breaking settings
};

}; // namespace TelEngine

#endif /* __YATESDP_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
