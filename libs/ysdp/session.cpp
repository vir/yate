/**
 * session.cpp
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

#include <yatesdp.h>

namespace TelEngine {

/*
 * SDPSession
 */
SDPSession::SDPSession(SDPParser* parser)
    : m_parser(parser), m_mediaStatus(MediaMissing),
      m_rtpForward(false), m_sdpForward(false), m_rtpMedia(0),
      m_sdpSession(0), m_sdpVersion(0), m_sdpHash(YSTRING_INIT_HASH),
      m_secure(m_parser->m_secure), m_rfc2833(m_parser->m_rfc2833),
      m_ipv6(false), m_enabler(0), m_ptr(0)
{
    setSdpDebug();
}

SDPSession::SDPSession(SDPParser* parser, NamedList& params)
    : m_parser(parser), m_mediaStatus(MediaMissing),
      m_rtpForward(false), m_sdpForward(false), m_rtpMedia(0),
      m_sdpSession(0), m_sdpVersion(0), m_sdpHash(YSTRING_INIT_HASH),
      m_ipv6(false), m_enabler(0), m_ptr(0)
{
    setSdpDebug();
    m_rtpForward = params.getBoolValue("rtp_forward");
    m_secure = params.getBoolValue("secure",parser->m_secure);
    m_rfc2833 = parser->m_rfc2833;
    setRfc2833(params.getParam("rfc2833"));
}

SDPSession::~SDPSession()
{
    resetSdp();
}

// Set new media list. Return true if changed
bool SDPSession::setMedia(ObjList* media)
{
    if (media == m_rtpMedia)
	return false;
    DDebug(m_enabler,DebugAll,"SDPSession::setMedia(%p) [%p]",media,m_ptr);
    ObjList* tmp = m_rtpMedia;
    m_rtpMedia = media;
    bool chg = m_rtpMedia != 0;
    if (tmp) {
	chg = false;
	for (ObjList* o = tmp->skipNull(); o; o = o->skipNext()) {
	    SDPMedia* m = static_cast<SDPMedia*>(o->get());
	    if (media && m->sameAs(static_cast<SDPMedia*>((*media)[*m]),m_parser->ignorePort()))
		continue;
	    chg = true;
	    mediaChanged(*m);
	}
	TelEngine::destruct(tmp);
    }
    printRtpMedia("Set media");
    return chg;
}

// Put the list of net media in a parameter list
void SDPSession::putMedia(NamedList& msg, ObjList* mList, bool putPort)
{
    if (!mList)
	return;
    bool audio = false;
    bool other = false;
    for (mList = mList->skipNull(); mList; mList = mList->skipNext()) {
	SDPMedia* m = static_cast<SDPMedia*>(mList->get());
	m->putMedia(msg,putPort);
	if (m->isAudio())
	    audio = true;
	else
	    other = true;
    }
    if (other && !audio)
	msg.setParam("media",String::boolText(false));
}

// Update the RFC 2833 availability and payload
void SDPSession::setRfc2833(const String& value)
{
    if (value.toBoolean(true)) {
	m_rfc2833 = value.toInteger(m_parser->m_rfc2833);
	if (m_rfc2833 < 96 || m_rfc2833 > 127)
	    m_rfc2833 = value.toBoolean(false) ? 101 : m_parser->m_rfc2833;
    }
    else
	m_rfc2833 = -1;
}

// Build and dispatch a chan.rtp message for a given media. Update media on success
bool SDPSession::dispatchRtp(SDPMedia* media, const char* addr, bool start,
    bool pick, RefObject* context)
{
    DDebug(m_enabler,DebugAll,"SDPSession::dispatchRtp(%p,%s,%u,%u,%p) [%p]",
	media,addr,start,pick,context,m_ptr);
    Message* m = buildChanRtp(media,addr,start,context);
    if (m)
	dispatchingRtp(m,media);
    if (!(m && Engine::dispatch(m))) {
	TelEngine::destruct(m);
	return false;
    }
    media->update(*m,start);
    if (!pick) {
	TelEngine::destruct(m);
	return true;
    }
    m_rtpForward = false;
    m_rtpLocalAddr = m->getValue("localip",m_rtpLocalAddr);
    m_mediaStatus = m_rtpLocalAddr.null() ? MediaMuted : MediaStarted;
    const char* sdpPrefix = m->getValue("osdp-prefix","osdp");
    if (sdpPrefix) {
	unsigned int n = m->length();
	for (unsigned int j = 0; j < n; j++) {
	    const NamedString* param = m->getParam(j);
	    if (!param)
		continue;
	    String tmp = param->name();
	    if (tmp.startSkip(sdpPrefix,false) && tmp.startSkip("_",false) && tmp)
	        media->parameter(tmp,*param,false);
	}
    }
    if (m_secure) {
	int tag = m->getIntValue("crypto_tag",1);
	tag = m->getIntValue("ocrypto_tag",tag);
	const String* suite = m->getParam("ocrypto_suite");
	const String* key = m->getParam("ocrypto_key");
	const String* params = m->getParam("ocrypto_params");
	if (suite && key && (tag >= 0)) {
	    String sdes(tag);
	    sdes << " " << *suite << " " << *key;
	    if (params)
		sdes << " " << *params;
	    media->crypto(sdes,false);
	}
    }
    TelEngine::destruct(m);
    return true;
}

// Repeatedly calls dispatchRtp() for each media in the list
// Update it on success. Remove it on failure
bool SDPSession::dispatchRtp(const char* addr, bool start, RefObject* context)
{
    if (!m_rtpMedia)
	return false;
    DDebug(m_enabler,DebugAll,"SDPSession::dispatchRtp(%s,%u,%p) [%p]",
	addr,start,context,m_ptr);
    bool ok = false;
    ObjList* o = m_rtpMedia->skipNull();
    while (o) {
	SDPMedia* m = static_cast<SDPMedia*>(o->get());
	if (dispatchRtp(m,addr,start,true,context)) {
	    ok = true;
	    o = o->skipNext();
	}
	else {
	    Debug(m_enabler,DebugMild,
		"Removing failed SDP media '%s' format '%s' from offer [%p]",
		m->c_str(),m->format().safe(),m_ptr);
	    o->remove();
	    o = o->skipNull();
	}
    }
    return ok;
}

// Try to start RTP for all media
bool SDPSession::startRtp(RefObject* context)
{
    if (m_rtpForward || !m_rtpMedia || (m_mediaStatus != MediaStarted))
	return false;
    DDebug(m_enabler,DebugAll,"SDPSession::startRtp(%p) [%p]",context,m_ptr);
    bool ok = false;
    for (ObjList* o = m_rtpMedia->skipNull(); o; o = o->skipNext()) {
	SDPMedia* m = static_cast<SDPMedia*>(o->get());
	ok = dispatchRtp(m,m_rtpAddr,true,false,context) || ok;
    }
    return ok;
}

// Update from parameters. Build a default SDP if no media is found in params
bool SDPSession::updateSDP(const NamedList& params)
{
    DDebug(m_enabler,DebugAll,"SDPSession::updateSdp('%s') [%p]",params.c_str(),m_ptr);
    bool defaults = true;
    const char* sdpPrefix = params.getValue("osdp-prefix","osdp");
    ObjList* lst = 0;
    unsigned int n = params.length();
    String defFormats;
    m_parser->getAudioFormats(defFormats);
    for (unsigned int i = 0; i < n; i++) {
	const NamedString* p = params.getParam(i);
	if (!p)
	    continue;
	// search for rtp_port or rtp_port_MEDIANAME parameters
	String tmp(p->name());
	if (!tmp.startSkip("media",false))
	    continue;
	if (tmp && (tmp[0] != '_'))
	    continue;
	// since we found at least one media declaration disable defaults
	defaults = false;
	// now tmp holds the suffix for the media, null for audio
	bool audio = tmp.null();
	// check if media is supported, default only for audio
	if (!p->toBoolean(audio))
	    continue;
	String fmts = params.getValue("formats" + tmp);
	if (audio && fmts.null())
	    fmts = defFormats;
	if (fmts.null())
	    continue;
	String trans = params.getValue("transport" + tmp,"RTP/AVP");
	String crypto;
	if (m_secure)
	    crypto = params.getValue("crypto" + tmp);
	if (audio)
	    tmp = "audio";
	else
	    tmp >> "_";
	SDPMedia* rtp = 0;
	// try to take the media descriptor from the old list
	if (m_rtpMedia) {
	    ObjList* om = m_rtpMedia->find(tmp);
	    if (om)
		rtp = static_cast<SDPMedia*>(om->remove(false));
	}
	bool append = false;
	if (rtp)
	    rtp->update(fmts);
	else {
	    rtp = new SDPMedia(tmp,trans,fmts);
	    append = true;
	}
	rtp->crypto(crypto,false);
	if (sdpPrefix) {
	    for (unsigned int j = 0; j < n; j++) {
		const NamedString* param = params.getParam(j);
		if (!param)
		    continue;
		tmp = param->name();
		if (tmp.startSkip(sdpPrefix + rtp->suffix() + "_",false) && (tmp.find('_') < 0))
		    rtp->parameter(tmp,*param,append);
	    }
	}
	if (!lst)
	    lst = new ObjList;
	lst->append(rtp);
    }
    if (defaults && !lst) {
	lst = new ObjList;
	lst->append(new SDPMedia("audio","RTP/AVP",params.getValue("formats",defFormats)));
    }
    return setMedia(lst);
}


// Update RTP/SDP data from parameters
// Return true if media changed
bool SDPSession::updateRtpSDP(const NamedList& params)
{
    DDebug(m_enabler,DebugAll,"SDPSession::updateRtpSDP(%s) [%p]",params.c_str(),m_ptr);
    String addr;
    ObjList* tmp = updateRtpSDP(params,addr,m_rtpMedia);
    if (tmp) {
	bool chg = (m_rtpLocalAddr != addr);
	m_rtpLocalAddr = addr;
	return setMedia(tmp) || chg;
    }
    return false;
}

// Utility used in createSDP
static int addIP(String& buf, const char* addr, int family = SocketAddr::Unknown)
{
    if (family != SocketAddr::IPv4 && family != SocketAddr::IPv6) {
	if (addr) {
	    family = SocketAddr::family(addr);
	    if (family != SocketAddr::IPv4 && family != SocketAddr::IPv6)
		family = SocketAddr::IPv4;
	}
	else
	    family = SocketAddr::IPv4;
    }
    if (family == SocketAddr::IPv4)
	buf << "IN IP4 ";
    else
	buf << "IN IP6 ";
    if (!TelEngine::null(addr))
	buf << addr;
    else if (family == SocketAddr::IPv4)
	buf << SocketAddr::ipv4NullAddr();
    else
	buf << SocketAddr::ipv6NullAddr();
    return family;
}

// Creates a SDP body from transport address and list of media descriptors
// Use own list if given media list is 0
MimeSdpBody* SDPSession::createSDP(const char* addr, ObjList* mediaList)
{
    DDebug(m_enabler,DebugAll,"SDPSession::createSDP('%s',%p) [%p]",addr,mediaList,m_ptr);
    if (!mediaList)
	mediaList = m_rtpMedia;
    // if we got no media descriptors we simply create no SDP
    if (!mediaList)
	return 0;
    if (!m_sdpSession)
	m_sdpVersion = m_sdpSession = Time::secNow();

    // override the address with the externally advertised if needed
    if (addr && m_rtpNatAddr)
	addr = m_rtpNatAddr;
    if (!m_originAddr)
	m_originAddr = addr ? addr : m_host.safe();
    // no address means on hold or muted
    String origin;
    int f = addIP(origin,m_originAddr);
    String conn;
    addIP(conn,addr,f);

    MimeSdpBody* sdp = new MimeSdpBody(true);
    sdp->addLine("v","0");
    // insert incomplete origin just for hashing purpose
    NamedString* org = sdp->addLine("o",origin);
    sdp->addLine("s",m_parser->m_sessionName);
    sdp->addLine("c",conn);
    sdp->addLine("t","0 0");

    Lock lock(m_parser);
    bool defcodecs = m_parser->m_codecs.getBoolValue("default",true);
    for (ObjList* ml = mediaList->skipNull(); ml; ml = ml->skipNext()) {
	SDPMedia* m = static_cast<SDPMedia*>(ml->get());
	int rfc2833 = 0;
	if ((m_rfc2833 >= 0) && m->isAudio()) {
	    if (!m_rtpForward) {
		rfc2833 = m->rfc2833().toInteger(m_rfc2833);
		if (rfc2833 < 96 || rfc2833 > 127)
		    rfc2833 = 101;
	    }
	    else if (m->rfc2833().toBoolean(true)) {
		rfc2833 = m->rfc2833().toInteger();
		if (rfc2833 < 96 || rfc2833 > 127)
		    rfc2833 = 0;
	    }
	}
	String mline(m->fmtList());
	ObjList* l = mline.split(',',false);
	mline = *m;
	mline << " " << (m->localPort() ? m->localPort().c_str() : "0") << " " << m->transport();
	ObjList* map = m->mappings().split(',',false);
	ObjList rtpmap;
	ObjList* dest = &rtpmap;
	String frm;
	int ptime = 0;
	ObjList* f = l;
	for (; f; f = f->next()) {
	    const String* s = static_cast<const String*>(f->get());
	    if (s) {
		int mode = 0;
		if (*s == "g729b")
		    continue;
		int payload = s->toInteger(SDPParser::s_payloads,-1);
		int defcode = payload;
		String tmp = *s;
		tmp << "=";
		bool found = false;
		for (ObjList* pl = map; pl; pl = pl->next()) {
		    const String* mapping = static_cast<const String*>(pl->get());
		    if (!mapping)
			continue;
		    if (mapping->startsWith(tmp)) {
			payload = -1;
			tmp = *mapping;
			tmp >> "=" >> payload;
			found = true;
			XDebug(m_enabler,DebugAll,"RTP mapped payload %d for '%s' [%p]",
			    payload,s->c_str(),m_ptr);
			break;
		    }
		    String tmp2 = *mapping;
		    int pload;
		    tmp2 >> "=" >> pload;
		    if (payload == pload) {
			XDebug(m_enabler,DebugAll,"RTP conflict for payload %d, allocating new [%p]",
			    payload,m_ptr);
			payload = -1;
			u_int32_t bmap = 0;
			for (ObjList* sl = map; sl; sl = sl->next()) {
			    mapping = static_cast<const String*>(sl->get());
			    if (!mapping)
				continue;
			    tmp2 = *mapping;
			    pload = 0;
			    tmp2 >> "=" >> pload;
			    if (pload >= 96 && pload < 127)
				bmap |= 1 << (pload - 96);
			}
			// allocate free and non-standard is possible
			for (pload = 96; pload < 127; pload++) {
			    if (pload == rfc2833)
				continue;
			    if (lookup(pload,SDPParser::s_rtpmap))
				continue;
			    if ((bmap & (1 << (pload - 96))) == 0) {
				payload = pload;
				break;
			    }
			}
			if (payload >= 0)
			    break;
			// none free, allocate from "standard" ones too
			for (pload = 96; pload < 127; pload++) {
			    if (pload == rfc2833)
				continue;
			    if ((bmap & (1 << (pload - 96))) == 0) {
				payload = pload;
				break;
			    }
			}
			break;
		    }
		}
		if (payload >= 0) {
		    if (!found) {
			tmp = *s;
			tmp << "=" << payload;
			map->append(new String(tmp));
		    }
		    if (defcode < 0)
			defcode = payload;
		    const char* map = lookup(defcode,SDPParser::s_rtpmap);
		    if (map && m_parser->m_codecs.getBoolValue(*s,defcodecs && DataTranslator::canConvert(*s))) {
			if (*s == "ilbc20")
			    ptime = mode = 20;
			else if (*s == "ilbc30")
			    ptime = mode = 30;
			frm << " " << payload;
			String* temp = new String("rtpmap:");
			*temp << payload << " " << map;
			dest = dest->append(temp);
			if (mode) {
			    temp = new String("fmtp:");
			    *temp << payload << " mode=" << mode;
			    dest = dest->append(temp);
			}
			if (*s == "g729") {
			    temp = new String("fmtp:");
			    *temp << payload << " annexb=" <<
				((0 != l->find("g729b")) ? "yes" : "no");
			    dest = dest->append(temp);
			}
			else if (*s == "amr") {
			    temp = new String("fmtp:");
			    *temp << payload << " octet-align=0";
			    dest = dest->append(temp);
			}
			else if (*s == "amr-o") {
			    temp = new String("fmtp:");
			    *temp << payload << " octet-align=1";
			    dest = dest->append(temp);
			}
		    }
		}
	    }
	}
	TelEngine::destruct(l);
	TelEngine::destruct(map);

	if (rfc2833 && frm) {
	    // claim to support telephone events
	    frm << " " << rfc2833;
	    String* s = new String;
	    *s << "rtpmap:" << rfc2833 << " telephone-event/8000";
	    dest = dest->append(s);
	}

	if (frm.null()) {
	    if (m->isAudio() || !m->fmtList()) {
		Debug(m_enabler,DebugMild,"No formats for '%s', excluding from SDP [%p]",
		    m->c_str(),m_ptr);
		continue;
	    }
	    Debug(m_enabler,DebugInfo,"Assuming formats '%s' for media '%s' [%p]",
		m->fmtList(),m->c_str(),m_ptr);
	    frm << " " << m->fmtList();
	    // brutal but effective
	    for (char* p = const_cast<char*>(frm.c_str()); *p; p++) {
		if (*p == ',')
		    *p = ' ';
	    }
	}

	if (ptime) {
	    String* temp = new String("ptime:");
	    *temp << ptime;
	    dest = dest->append(temp);
	}

	sdp->addLine("m",mline + frm);
	bool enc = false;
	if (m->isModified()) {
	    unsigned int n = m->length();
	    for (unsigned int i = 0; i < n; i++) {
		const NamedString* param = m->getParam(i);
		if (param) {
		    const char* type = "a";
		    String tmp = param->name();
		    if (tmp.startSkip("BW-",false)) {
			if (!tmp)
			    continue;
			type = "b";
		    }
		    else
			enc = enc || (tmp == "encryption");
		    if (*param)
			tmp << ":" << *param;
		    sdp->addLine(type,tmp);
		}
	    }
	}
	for (f = rtpmap.skipNull(); f; f = f->skipNext()) {
	    String* s = static_cast<String*>(f->get());
	    if (s)
		sdp->addLine("a",*s);
	}
	if (addr && m->localCrypto()) {
	    sdp->addLine("a","crypto:" + m->localCrypto());
	    if (!enc)
		sdp->addLine("a","encryption:optional");
	}
    }
    // increment version if body hash changed
    if ((YSTRING_INIT_HASH != m_sdpHash) && (sdp->hash() != m_sdpHash))
	m_sdpVersion++;
    m_sdpHash = sdp->hash();
    // insert version in the origin line
    origin.clear();
    origin << "yate " << m_sdpSession << " " << m_sdpVersion << " " << *org;
    *org = origin;

    return sdp;
}

// Creates a SDP body for the current media status
MimeSdpBody* SDPSession::createSDP()
{
    switch (m_mediaStatus) {
	case MediaStarted:
	    return createSDP(getRtpAddr());
	case MediaMuted:
	    return createSDP(0);
	default:
	    return 0;
    }
}

// Creates a SDP from RTP address data present in message
MimeSdpBody* SDPSession::createPasstroughSDP(NamedList& msg, bool update,
    bool allowEmptyAddr)
{
    XDebug(m_enabler,DebugAll,"createPasstroughSDP(%s,%u,%u) [%p]",
	msg.c_str(),update,allowEmptyAddr,m_ptr);
    String tmp = msg.getValue("rtp_forward");
    msg.clearParam("rtp_forward");
    if (!(m_rtpForward && tmp.toBoolean()))
	return 0;
    String* raw = msg.getParam("sdp_raw");
    if (raw) {
	m_sdpForward = m_sdpForward || m_parser->sdpForward();
	if (m_sdpForward) {
	    msg.setParam("rtp_forward","accepted");
	    return new MimeSdpBody("application/sdp",raw->safe(),raw->length());
	}
    }
    String addr;
    ObjList* lst = updateRtpSDP(msg,addr,update ? m_rtpMedia : 0,allowEmptyAddr);
    if (!lst)
	return 0;
    MimeSdpBody* sdp = createSDP(addr,lst);
    if (update) {
	m_rtpLocalAddr = addr;
	setMedia(lst);
    }
    else
	TelEngine::destruct(lst);
    if (sdp)
	msg.setParam("rtp_forward","accepted");
    return sdp;
}

// Update media format lists from parameters
void SDPSession::updateFormats(const NamedList& msg, bool changeMedia)
{
    if (!m_rtpMedia)
	return;
    unsigned int n = msg.length();
    unsigned int i;
    if (changeMedia) {
	// check if any media is to be removed
	for (i = 0; i < n; i++) {
	    const NamedString* p = msg.getParam(i);
	    if (!p)
		continue;
	    // search for media_MEDIANAME parameters
	    String tmp = p->name();
	    if (!tmp.startSkip("media",false))
		continue;
	    if (tmp && (tmp[0] != '_'))
		continue;
	    // only check for explicit disabled media
	    if (p->toBoolean(true))
		continue;
	    if (tmp.null())
		tmp = "audio";
	    else
		tmp = tmp.substr(1);
	    SDPMedia* rtp = static_cast<SDPMedia*>(m_rtpMedia->operator[](tmp));
	    if (!rtp)
		continue;
	    Debug(m_enabler,DebugNote,"Removing disabled media '%s' [%p]",
		tmp.c_str(),m_ptr);
	    m_rtpMedia->remove(rtp,false);
	    mediaChanged(*rtp);
	    TelEngine::destruct(rtp);
	}
    }
    for (i = 0; i < n; i++) {
	const NamedString* p = msg.getParam(i);
	if (!p)
	    continue;
	// search for formats_MEDIANAME parameters
	String tmp = p->name();
	if (!tmp.startSkip("formats",false))
	    continue;
	if (tmp && (tmp[0] != '_'))
	    continue;
	const char* trans = 0;
	// make sure we don't re-add explicitely disabled media
	if (changeMedia && msg.getBoolValue("media"+tmp,true))
	    trans = msg.getValue("transport"+tmp);
	if (tmp.null())
	    tmp = "audio";
	else
	    tmp = tmp.substr(1);
	SDPMedia* rtp = static_cast<SDPMedia*>(m_rtpMedia->operator[](tmp));
	if (rtp) {
	    if (rtp->update(*p))
		Debug(m_enabler,DebugNote,"Formats for '%s' changed to '%s' [%p]",
		    tmp.c_str(),rtp->formats().c_str(),m_ptr);
	}
	else if (*p) {
	    Debug(m_enabler,DebugNote,"Got formats '%s' for absent media '%s' [%p]",
		p->c_str(),tmp.c_str(),m_ptr);
	    if (trans) {
		rtp = new SDPMedia(tmp,trans,p->c_str());
		m_rtpMedia->append(rtp);
		mediaChanged(*rtp);
	    }
	}
    }
    String sdpPrefix = msg.getValue("osdp-prefix");
    if (!sdpPrefix)
	return;
    sdpPrefix += "_";
    for (i = 0; i < n; i++) {
	const NamedString* param = msg.getParam(i);
	if (!param)
	    continue;
	String tmp = param->name();
	if (!tmp.startSkip(sdpPrefix,false))
	    continue;
	int sep = tmp.find("_");
	String media("audio");
	if (sep > 0) {
	    media = tmp.substr(0,sep);
	    tmp = tmp.substr(sep + 1);
	}
	if (tmp.null() || (tmp.find("_") >= 0))
	    continue;
	SDPMedia* rtp = static_cast<SDPMedia*>(m_rtpMedia->operator[](media));
	if (rtp) {
	    DDebug(m_enabler,DebugInfo,"Updating %s parameter '%s' to '%s'",
		media.c_str(),tmp.c_str(),param->c_str());
	    rtp->parameter(tmp,*param,false);
	}
    }
}

// Add raw SDP forwarding parameter
bool SDPSession::addSdpParams(NamedList& msg, const MimeBody* body)
{
    if (!(m_sdpForward && body))
	return false;
    const MimeSdpBody* sdp =
	static_cast<const MimeSdpBody*>(body->isSDP() ? body : body->getFirst("application/sdp"));
    if (!sdp)
	return false;
    const DataBlock& raw = sdp->getBody();
    String tmp((const char*)raw.data(),raw.length());
    return addSdpParams(msg,tmp);
}

// Add raw SDP forwarding parameter
bool SDPSession::addSdpParams(NamedList& msg, const String& rawSdp)
{
    if (!m_sdpForward)
	return false;
    msg.setParam("rtp_forward","yes");
    msg.addParam("sdp_raw",rawSdp);
    return true;
}

// Add RTP forwarding parameters to a message
bool SDPSession::addRtpParams(NamedList& msg, const String& natAddr,
    const MimeBody* body, bool force, bool allowEmptyAddr)
{
    XDebug(m_enabler,DebugAll,"addRtpParams(%s,%s,%p,%u,%u) media=%p rtpaddr=%s [%p]",
	msg.c_str(),natAddr.c_str(),body,force,allowEmptyAddr,m_rtpMedia,
	m_rtpAddr.c_str(),m_ptr);
    if (!(m_rtpMedia && (m_rtpAddr || allowEmptyAddr)))
	return false;
    putMedia(msg,false);
    if (force || (!startRtp() && m_rtpForward)) {
	if (natAddr)
	    msg.addParam("rtp_nat_addr",natAddr);
	msg.addParam("rtp_forward","yes");
	msg.addParam("rtp_addr",m_rtpAddr);
	for (ObjList* o = m_rtpMedia->skipNull(); o; o = o->skipNext()) {
	    SDPMedia* m = static_cast<SDPMedia*>(o->get());
	    msg.addParam("rtp_port" + m->suffix(),m->remotePort());
	    if (m->isAudio())
		msg.addParam("rtp_rfc2833",m->rfc2833());
	}
	addSdpParams(msg,body);
	return true;
    }
    return false;
}

// Reset this object to default values
void SDPSession::resetSdp(bool all)
{
    m_mediaStatus = MediaMissing;
    TelEngine::destruct(m_rtpMedia);
    m_rtpForward = false;
    m_sdpForward = false;
    m_externalAddr.clear();
    m_rtpAddr.clear();
    m_rtpLocalAddr.clear();
    m_sdpSession = 0;
    m_sdpVersion = 0;
    m_host.clear();
    if (all) {
	m_secure = m_parser->secure();
	m_rfc2833 = m_parser->rfc2833();
    }
}

// Build a populated chan.rtp message
Message* SDPSession::buildChanRtp(SDPMedia* media, const char* addr, bool start, RefObject* context)
{
    if (!(media && addr))
	return 0;
    Message* m = buildChanRtp(context);
    if (!m)
	return 0;
    if (media->id())
	m->addParam("rtpid",media->id());
    m->addParam("media",*media);
    m->addParam("transport",media->transport());
    m->addParam("direction","bidir");
    if (media->format())
	m->addParam("format",media->format());
    m->addParam("ipv6_support",String::boolText(m_ipv6));
    if (m_rtpLocalAddr)
	m->addParam("localip",m_rtpLocalAddr);
    m->addParam("remoteip",addr);
    if (start) {
	m->addParam("remoteport",media->remotePort());
	String tmp = media->format();
	tmp << "=";
	ObjList* mappings = media->mappings().split(',',false);
	for (ObjList* pl = mappings; pl; pl = pl->next()) {
	    String* mapping = static_cast<String*>(pl->get());
	    if (!mapping)
		continue;
	    if (mapping->startsWith(tmp)) {
		tmp = *mapping;
		tmp >> "=";
		m->addParam("payload",tmp);
		break;
	    }
	}
	m->addParam("evpayload",media->rfc2833());
	TelEngine::destruct(mappings);
    }
    if (m_secure) {
	if (media->remoteCrypto()) {
	    String sdes = media->remoteCrypto();
	    static const Regexp r("^\\([0-9]\\+\\) \\+\\([^ ]\\+\\) \\+\\([^ ]\\+\\) *\\(.*\\)$");
	    if (sdes.matches(r)) {
		m->addParam("secure",String::boolText(true));
		m->addParam("crypto_tag",sdes.matchString(1));
		m->addParam("crypto_suite",sdes.matchString(2));
		m->addParam("crypto_key",sdes.matchString(3));
		if (sdes.matchLength(4))
		    m->addParam("crypto_params",sdes.matchString(4));
	    }
	    else
		Debug(m_enabler,DebugWarn,"Invalid SDES: '%s' [%p]",sdes.c_str(),m_ptr);
	}
	else if (media->securable())
	    m->addParam("secure",String::boolText(true));
    }
    else
	media->crypto(0,true);
    unsigned int n = media->length();
    for (unsigned int i = 0; i < n; i++) {
	const NamedString* param = media->getParam(i);
	if (!param)
	    continue;
	m->addParam("sdp_" + param->name(),*param);
    }
    return m;
}

// Check if local RTP data changed for at least one media
bool SDPSession::localRtpChanged() const
{
    if (!m_rtpMedia)
	return false;
    for (ObjList* o = m_rtpMedia->skipNull(); o; o = o->skipNext()) {
	SDPMedia* m = static_cast<SDPMedia*>(o->get());
	if (m->localChanged())
	    return true;
    }
    return false;
}

// Set or reset the local RTP data changed flag for all media
void SDPSession::setLocalRtpChanged(bool chg)
{
    if (!m_rtpMedia)
	return;
    for (ObjList* o = m_rtpMedia->skipNull(); o; o = o->skipNext())
	(static_cast<SDPMedia*>(o->get()))->setLocalChanged(chg);
}

// Update RTP/SDP data from parameters
ObjList* SDPSession::updateRtpSDP(const NamedList& params, String& rtpAddr, ObjList* oldList,
    bool allowEmptyAddr)
{
    XDebug(DebugAll,"SDPSession::updateRtpSDP(%s,%s,%p,%u)",
	params.c_str(),rtpAddr.c_str(),oldList,allowEmptyAddr);
    rtpAddr = params.getValue("rtp_addr");
    if (!(rtpAddr || allowEmptyAddr))
	return 0;
    const char* sdpPrefix = params.getValue("osdp-prefix","osdp");
    ObjList* lst = 0;
    unsigned int n = params.length();
    for (unsigned int i = 0; i < n; i++) {
	const NamedString* p = params.getParam(i);
	if (!p)
	    continue;
	// search for rtp_port or rtp_port_MEDIANAME parameters
	String tmp = p->name();
	if (!tmp.startSkip("rtp_port",false))
	    continue;
	if (tmp && (tmp[0] != '_'))
	    continue;
	// now tmp holds the suffix for the media, null for audio
	bool audio = tmp.null();
	// check if media is supported, default only for audio
	if (!params.getBoolValue("media" + tmp,audio))
	    continue;
	int port = p->toInteger();
	if (!(port || allowEmptyAddr))
	    continue;
	const char* fmts = params.getValue("formats" + tmp);
	if (!fmts)
	    continue;
	String trans = params.getValue("transport" + tmp,"RTP/AVP");
	if (audio)
	    tmp = "audio";
	else
	    tmp >> "_";
	SDPMedia* rtp = 0;
	// try to take the media descriptor from the old list
	if (oldList) {
	    ObjList* om = oldList->find(tmp);
	    if (om)
		rtp = static_cast<SDPMedia*>(om->remove(false));
	}
	bool append = false;
	if (rtp)
	    rtp->update(fmts,-1,port);
	else {
	    rtp = new SDPMedia(tmp,trans,fmts,-1,port);
	    append = true;
	}
	if (sdpPrefix) {
	    for (unsigned int j = 0; j < n; j++) {
		const NamedString* param = params.getParam(j);
		if (!param)
		    continue;
		tmp = param->name();
		if (tmp.startSkip(sdpPrefix + rtp->suffix() + "_",false) && (tmp.find('_') < 0))
		    rtp->parameter(tmp,*param,append);
	    }
	}
	rtp->mappings(params.getValue("rtp_mapping" + rtp->suffix()));
	if (audio)
	    rtp->rfc2833(params.getIntValue("rtp_rfc2833",-1));
	rtp->crypto(params.getValue("crypto" + rtp->suffix()),false);
	if (!lst)
	    lst = new ObjList;
	lst->append(rtp);
    }
    return lst;
}

// Media changed notification.
void SDPSession::mediaChanged(const SDPMedia& media)
{
    XDebug(m_enabler,DebugAll,"SDPSession::mediaChanged('%s' %p)%s%s [%p]",
	media.c_str(),&media,(media.id() ? " id=" : ""),media.id().safe(),m_ptr);
}

// Dispatch rtp notification
void SDPSession::dispatchingRtp(Message*& msg, SDPMedia* media)
{
    XDebug(m_enabler,DebugAll,"SDPSession::dispatchingRtp(%p,%p) [%p]",msg,media,m_ptr);
}

// Set data used in debug
void SDPSession::setSdpDebug(DebugEnabler* enabler, void* ptr)
{
    m_enabler = enabler ? enabler : static_cast<DebugEnabler*>(m_parser);
    m_ptr = ptr ? ptr : (void*)this;
}

// Print current media to output
void SDPSession::printRtpMedia(const char* reason)
{
    if (!(m_rtpMedia && m_enabler->debugAt(DebugAll)))
	return;
    String tmp;
    for (ObjList* o = m_rtpMedia->skipNull(); o; o = o->skipNext()) {
    	SDPMedia* m = static_cast<SDPMedia*>(o->get());
	if (tmp)
	    tmp << " ";
	tmp << m->c_str() << "=" << m->formats();
    }
    Debug(m_enabler,DebugAll,"%s: %s [%p]",reason,tmp.c_str(),m_ptr);
}

};   // namespace TelEngine

/* vi: set ts=8 sw=4 sts=4 noet: */
