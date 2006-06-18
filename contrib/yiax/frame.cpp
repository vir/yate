/**
 * frame.cpp
 * Yet Another IAX2 Stack
 * This file is part of the YATE Project http://YATE.null.ro 
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <yateiax.h>

using namespace TelEngine;

//static u_int64_t iax_frame_allocated = 0;
//static u_int64_t iax_frame_released = 0;
//static u_int64_t iax_ies_allocated = 0;
//static u_int64_t iax_ies_released = 0;


IAXInfoElement::IAXInfoElement(Type type)
    : m_type(type)
{
//    iax_ies_allocated++;
}

IAXInfoElement::~IAXInfoElement()
{
//    iax_ies_released++;
}


/**
 * IAXInfoElement
 */
static TokenDict s_ieData[] = {
    {"CALLED_NUMBER",     IAXInfoElement::CALLED_NUMBER},
    {"CALLING_NUMBER",    IAXInfoElement::CALLING_NUMBER},
    {"CALLING_ANI",       IAXInfoElement::CALLING_ANI},
    {"CALLING_NAME",      IAXInfoElement::CALLING_NAME},
    {"CALLED_CONTEXT",    IAXInfoElement::CALLED_CONTEXT},
    {"USERNAME",          IAXInfoElement::USERNAME},
    {"PASSWORD",          IAXInfoElement::PASSWORD},
    {"CAPABILITY",        IAXInfoElement::CAPABILITY},
    {"FORMAT",            IAXInfoElement::FORMAT},
    {"LANGUAGE",          IAXInfoElement::LANGUAGE},
    {"VERSION",           IAXInfoElement::VERSION},
    {"ADSICPE",           IAXInfoElement::ADSICPE},
    {"DNID",              IAXInfoElement::DNID},
    {"AUTHMETHODS",       IAXInfoElement::AUTHMETHODS},
    {"CHALLENGE",         IAXInfoElement::CHALLENGE},
    {"MD5_RESULT",        IAXInfoElement::MD5_RESULT},
    {"RSA_RESULT",        IAXInfoElement::RSA_RESULT},
    {"APPARENT_ADDR",     IAXInfoElement::APPARENT_ADDR},
    {"REFRESH",           IAXInfoElement::REFRESH},
    {"DPSTATUS",          IAXInfoElement::DPSTATUS},
    {"CALLNO",            IAXInfoElement::CALLNO},
    {"CAUSE",             IAXInfoElement::CAUSE},
    {"IAX_UNKNOWN",       IAXInfoElement::IAX_UNKNOWN},
    {"MSGCOUNT",          IAXInfoElement::MSGCOUNT},
    {"AUTOANSWER",        IAXInfoElement::AUTOANSWER},
    {"MUSICONHOLD",       IAXInfoElement::MUSICONHOLD},
    {"TRANSFERID",        IAXInfoElement::TRANSFERID},
    {"RDNIS",             IAXInfoElement::RDNIS},
    {"PROVISIONING",      IAXInfoElement::PROVISIONING},
    {"AESPROVISIONING",   IAXInfoElement::AESPROVISIONING},
    {"DATETIME",          IAXInfoElement::DATETIME},
    {"DEVICETYPE",        IAXInfoElement::DEVICETYPE},
    {"SERVICEIDENT",      IAXInfoElement::SERVICEIDENT},
    {"FIRMWAREVER",       IAXInfoElement::FIRMWAREVER},
    {"FWBLOCKDESC",       IAXInfoElement::FWBLOCKDESC},
    {"FWBLOCKDATA",       IAXInfoElement::FWBLOCKDATA},
    {"PROVVER",           IAXInfoElement::PROVVER},
    {"CALLINGPRES",       IAXInfoElement::CALLINGPRES},
    {"CALLINGTON",        IAXInfoElement::CALLINGTON},
    {"CALLINGTNS",        IAXInfoElement::CALLINGTNS},
    {"SAMPLINGRATE",      IAXInfoElement::SAMPLINGRATE},
    {"CAUSECODE",         IAXInfoElement::CAUSECODE},
    {"ENCRYPTION",        IAXInfoElement::ENCRYPTION},
    {"ENKEY",             IAXInfoElement::ENKEY},
    {"CODEC_PREFS",       IAXInfoElement::CODEC_PREFS},
    {"RR_JITTER",         IAXInfoElement::RR_JITTER},
    {"RR_LOSS",           IAXInfoElement::RR_LOSS},
    {"RR_PKTS",           IAXInfoElement::RR_PKTS},
    {"RR_DELAY",          IAXInfoElement::RR_DELAY},
    {"RR_DROPPED",        IAXInfoElement::RR_DROPPED},
    {"RR_OOO",            IAXInfoElement::RR_OOO},
    {0, 0}
};

const char* IAXInfoElement::ieText(u_int8_t ieCode)
{
    return lookup(ieCode,s_ieData);
}

void IAXInfoElement::toBuffer(DataBlock& buf)
{
    unsigned char d[2] = {m_type,0};
    buf.assign(d,2);
}

/**
 * IAXInfoElementString
 */
void IAXInfoElementString::toBuffer(DataBlock& buf)
{
    unsigned char d[2] = {m_type,m_strData.length()};
    buf.assign(d,2);
    buf.append(data());
}

/**
 * IAXInfoElementNumeric
 */
IAXInfoElementNumeric::IAXInfoElementNumeric(Type type, u_int32_t val, u_int8_t len)
    : IAXInfoElement(type),
      m_length(len)
{
    switch (m_length) {
	case 4:
	    m_numericData = (u_int32_t)val;
	    break;
	case 2:
	    m_numericData = (u_int16_t)val;
	    break;
	case 1:
	    m_numericData = (u_int8_t)val;
	    break;
    }
}
	
void IAXInfoElementNumeric::toBuffer(DataBlock& buf)
{
    unsigned char d[6] = {m_type,m_length};

    switch (m_length) {
	case 1:
	    d[2] = (unsigned char)m_numericData;
	    break;
	case 2:
	    d[2] = ((unsigned short)m_numericData) >> 8;
	    d[3] = (unsigned char)m_numericData;
	    break;
	case 4:
	    d[2] = ((unsigned long)m_numericData) >> 24;
	    d[3] = ((unsigned long)m_numericData) >> 16;
	    d[4] = ((unsigned long)m_numericData) >> 8;
	    d[5] = (unsigned char)m_numericData;
	    break;
    }
    buf.assign(d,2 + m_length);
}

/**
 * IAXInfoElementBinary
 */
void IAXInfoElementBinary::toBuffer(DataBlock& buf)
{
    unsigned char d[2] = {m_type,m_data.length()};
    DataBlock data(d,2);
    buf.assign(d,2);
    buf += m_data;
}

IAXInfoElementBinary* IAXInfoElementBinary::packIP(const SocketAddr& addr, bool ipv4)
{
    if (!ipv4)
	return 0;
    return new IAXInfoElementBinary(IAXInfoElement::APPARENT_ADDR,(unsigned char*)(addr.address()),addr.length());
}

bool IAXInfoElementBinary::unpackIP(SocketAddr& addr, IAXInfoElementBinary* ie)
{
    addr.clear();
    if (!ie)
	return false;
    addr.assign((struct sockaddr*)(ie->data().data()),ie->data().length());
    return true;
}

/**
 * IAXFormat
 */
TokenDict IAXFormat::audioData[] = {
    {"gsm", IAXFormat::GSM},
    {"ilbc30", IAXFormat::ILBC},
    {"speex", IAXFormat::SPEEX},
    {"lpc10", IAXFormat::LPC10},
    {"mulaw", IAXFormat::ULAW},
    {"alaw", IAXFormat::ALAW},
    {"g723", IAXFormat::G723_1},
    {"g729", IAXFormat::G729A},
    {"adpcm", IAXFormat::ADPCM},
    {"mp3", IAXFormat::MP3},
    {"slin", IAXFormat::SLIN},
    {0, 0}
};

TokenDict IAXFormat::videoData[] = {
    {"jpeg", IAXFormat::JPEG},
    {"png", IAXFormat::PNG},
    {"h261", IAXFormat::H261},
    {"h263", IAXFormat::H263},
    {0, 0}
};

const char* IAXFormat::audioText(u_int8_t audio)
{
    for (int i = 0; audioData[i].value; i++)
	if (audioData[i].value == audio)
	    return audioData[i].token;
    return 0;
}

const char* IAXFormat::videoText(u_int8_t video)
{
    for (int i = 0; videoData[i].value; i++)
	if (videoData[i].value == video)
	    return videoData[i].token;
    return 0;
}

/**
 * IAXFrame
 */
IAXFrame::IAXFrame(Type type, u_int16_t sCallNo, u_int32_t tStamp, bool retrans,
		   const unsigned char* buf, unsigned int len)
    : m_type(type), m_data((char*)buf,len,true), m_retrans(retrans),
      m_sCallNo(sCallNo), m_tStamp(tStamp), m_subclass(0)
{
//    iax_frame_allocated++;
    XDebug(DebugAll,"IAXFrame::IAXFrame(%u) [%p]",type,this);
}

IAXFrame::~IAXFrame()
{
//    iax_frame_released++;
    XDebug(DebugAll,"IAXFrame::~IAXFrame() [%p]",this);
}

bool IAXFrame::setRetrans()
{
    if (!m_retrans) {
	m_retrans = true;
	((unsigned char*)m_data.data())[2] |= 0x80;
    }
    return true;
}

IAXFrame* IAXFrame::parse(const unsigned char* buf, unsigned int len, IAXEngine* engine, const SocketAddr* addr)
{
    if (len < 4)
	return 0;
    u_int16_t scn = (buf[0] << 8) | buf[1];
    u_int16_t dcn = (buf[2] << 8) | buf[3];
    // Full frame ?
    if (scn & 0x8000) {
	if (len < 12)
	    return 0;
	scn &= 0x7fff;
	bool retrans = false;
	if (dcn & 0x8000) {
	    retrans = true;
	    dcn &= 0x7fff;
	}
	u_int32_t sc = IAXFrame::unpackSubclass(buf[11]);
	u_int32_t ts = (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];
	return new IAXFullFrame((IAXFrame::Type)buf[10],sc,scn,dcn,buf[8],buf[9],ts,retrans,buf+12,len-12);
    }
    // Meta frame ?
    if (scn == 0) {
	if (dcn & 0x8000) {
	    // Meta video
	    if (len < 6)
		return 0;
	    scn = (buf[4] << 8) | buf[5];
	    bool retrans = false;
	    if (scn & 0x8000) {
		retrans = true;
		scn &= 0x7fff;
	    }
	    return new IAXFrame(IAXFrame::Video,dcn & 0x7fff,scn,retrans,buf+6,len-6);
	}
	// Meta trunk frame - we need to push chunks into the engine
	if (!(engine && addr))
	    return 0;
	if (len < 8)
	    return 0;
	// "meta command" should be 1
	if (buf[2] != 1)
	    return 0;
	bool tstamps = (buf[3] & 1) != 0;
//	u_int32_t ts = (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];
	buf += 8;
	len -= 8;
	if (tstamps) {
	    // Trunk timestamps
	    while (len >= 4) {
		u_int16_t dlen = (buf[2] << 8) | buf[3];
		if ((unsigned int)(dlen + 4) > len)
		    return 0;
		scn = (buf[0] << 8) | buf[1];
		bool retrans = false;
		if (scn & 0x8000) {
		    retrans = true;
		    scn &= 0x7fff;
		}
		IAXFrame* frame = new IAXFrame(IAXFrame::Voice,scn,0,retrans,buf+4,dlen);
		engine->addFrame(*addr,frame);
		frame->deref();
		dlen += 4;
		buf += dlen;
		len -= dlen;
	    }
	}
	else {
	    // No trunk timestamps
	    while (len >= 6) {
		u_int16_t dlen = (buf[0] << 8) | buf[1];
		if ((unsigned int)(dlen + 6) > len)
		    return 0;
		scn = (buf[2] << 8) | buf[3];
		bool retrans = false;
		if (scn & 0x8000) {
		    retrans = true;
		    scn &= 0x7fff;
		}
		dcn = (buf[4] << 8) | buf[5];
		IAXFrame* frame = new IAXFrame(IAXFrame::Voice,scn,dcn,retrans,buf+6,dlen);
		engine->addFrame(*addr,frame);
		frame->deref();
		dlen += 6;
		buf += dlen;
		len -= dlen;
	    }
	}
	return 0;
    }
    // Mini frame
    return new IAXFrame(IAXFrame::Voice,scn,dcn,false,buf+4,len-4);
}

u_int8_t IAXFrame::packSubclass(u_int32_t value)
{
    if (value < 0x80)
	return value;
    if (value == 0x80)
	return 0x87;
    if ((value > 0x9f) && (value <= 0xff)) {
	DDebug(DebugMild,"IAXFrame nonstandard pack %u",value);
	return value;
    }
    // no need to start from zero, we already know it's >= 2^8
    u_int32_t v = 0x100;
    for (u_int8_t i = 8; i < 32; i++) {
	if (v == value)
	    return i | 0x80;
	v <<= 1;
    }
    Debug(DebugGoOn,"IAXFrame could not pack subclass %u (0x%08x)",value,value);
    return 0;
}

u_int32_t IAXFrame::unpackSubclass(u_int8_t value) 
{
    if (value > 0x9f) {
	DDebug(DebugMild,"IAXFrame nonstandard unpack %u",value);
	return value;
    }
    if (value & 0x80)
	return 1 << value & 0x7f;
    return value;
}

const IAXFullFrame* IAXFrame::fullFrame() const
{
    return 0;
}

ObjList* IAXFrame::getIEList(const IAXFullFrame* frame, bool& invalid)
{
    unsigned char* data = (unsigned char*)(((IAXFullFrame*)frame)->data().data());
    unsigned int len = ((IAXFullFrame*)frame)->data().length();
    ObjList* ieList = new ObjList;
    u_int16_t i;       // Current index of IE buffer
    u_int32_t value;

    if (frame->type() == IAXFrame::Text)
    {
	// Create even if text is empty
	ieList->append(new IAXInfoElementString(IAXInfoElement::textframe,data,len));
	return ieList;
    }
    if (len < 2) {
	invalid = len ? true : false;
	delete ieList;
	return 0;
    }
    for (i = 1; i < len;) {
	if (i + (unsigned int)data[i] >= len) {
	    i = 0xFFFF;
	    break;
	}
	switch (data[i-1]) {
	    // Text
	    case IAXInfoElement::CALLED_NUMBER:
	    case IAXInfoElement::CALLING_NUMBER:
	    case IAXInfoElement::CALLING_ANI:
	    case IAXInfoElement::CALLING_NAME:
	    case IAXInfoElement::CALLED_CONTEXT:
	    case IAXInfoElement::USERNAME:
	    case IAXInfoElement::PASSWORD:
	    case IAXInfoElement::LANGUAGE:
	    case IAXInfoElement::DNID:
	    case IAXInfoElement::CHALLENGE:
	    case IAXInfoElement::MD5_RESULT:
	    case IAXInfoElement::RSA_RESULT:
	    case IAXInfoElement::CAUSE:
	    case IAXInfoElement::MUSICONHOLD:        // Optional
	    case IAXInfoElement::RDNIS: 
	    case IAXInfoElement::DEVICETYPE:
		if (data[i])
		    ieList->append(new IAXInfoElementString((IAXInfoElement::Type)data[i-1],data+i+1,data[i]));
		else
		    ieList->append(new IAXInfoElementString((IAXInfoElement::Type)data[i-1],0,0));
		i += data[i] + 1;
		break;
	    // Binary
	    case IAXInfoElement::CODEC_PREFS:        // LIST of strings
		if (data[i])
		    ieList->append(new IAXInfoElementString((IAXInfoElement::Type)data[i-1],data+i+1,data[i]));
		else
		    ieList->append(new IAXInfoElementBinary((IAXInfoElement::Type)data[i-1],0,0));
		i += data[i] + 1;
		break;
	    case IAXInfoElement::APPARENT_ADDR:
	    case IAXInfoElement::PROVISIONING:
	    case IAXInfoElement::AESPROVISIONING:
	    case IAXInfoElement::SERVICEIDENT:       // Length must be 6
	    case IAXInfoElement::FWBLOCKDATA:        // Length can be 0
	    case IAXInfoElement::ENKEY:
		if (data[i-1] != IAXInfoElement::FWBLOCKDATA && !data[i]) {
		    i = 0xFFFF;
		    break;
		}
		if (data[i-1] == IAXInfoElement::SERVICEIDENT && data[i] != 6) {
		    i = 0xFFFF;
		    break;
		}
		ieList->append(new IAXInfoElementBinary((IAXInfoElement::Type)data[i-1],data+i+1,data[i]));
		i += data[i] + 1;
		break;
	    // 4 bytes
	    case IAXInfoElement::CAPABILITY:
	    case IAXInfoElement::FORMAT:
	    case IAXInfoElement::TRANSFERID:
	    case IAXInfoElement::DATETIME:
	    case IAXInfoElement::PROVVER:
	    case IAXInfoElement::FWBLOCKDESC:
	    case IAXInfoElement::SAMPLINGRATE:
	    case IAXInfoElement::RR_JITTER:
	    case IAXInfoElement::RR_LOSS:
	    case IAXInfoElement::RR_PKTS:
	    case IAXInfoElement::RR_DROPPED:
	    case IAXInfoElement::RR_OOO:
		if (data[i] != 4) {
		    i = 0xFFFF;
		    break;
		}
		value = (data[i+1] << 24) | (data[i+2] << 16) | (data[i+3] << 8) | data[i+4];
		ieList->append(new IAXInfoElementNumeric((IAXInfoElement::Type)data[i-1],value,4));
		i += 5;
		break;
	    // 2 bytes
	    case IAXInfoElement::VERSION:             // Value: 0x0002
	    case IAXInfoElement::ADSICPE:
	    case IAXInfoElement::AUTHMETHODS:
	    case IAXInfoElement::REFRESH:
	    case IAXInfoElement::DPSTATUS:
	    case IAXInfoElement::CALLNO: 
	    case IAXInfoElement::MSGCOUNT:
	    case IAXInfoElement::CALLINGTNS:
	    case IAXInfoElement::FIRMWAREVER:
	    case IAXInfoElement::RR_DELAY:
		if (data[i] != 2) {
		    i = 0xFFFF;
		    break;
		}
		value = (data[i+1] << 8) | data[i+2];
		if (data[i-1] == IAXInfoElement::VERSION && value != IAX_PROTOCOL_VERSION) {
		    i = 0xFFFF;
		    break;
		}
		ieList->append(new IAXInfoElementNumeric((IAXInfoElement::Type)data[i-1],value,2));
		i += 3;
		break;
	    // 1 byte
	    case IAXInfoElement::IAX_UNKNOWN:
	    case IAXInfoElement::CALLINGPRES:
	    case IAXInfoElement::CALLINGTON:
	    case IAXInfoElement::CAUSECODE:
	    case IAXInfoElement::ENCRYPTION:
		if (data[i] != 1) {
		    i = 0xFFFF;
		    break;
		}
		value = data[i+1];
		ieList->append(new IAXInfoElementNumeric((IAXInfoElement::Type)data[i-1],value,1));
		i += 2;
		break;
	    // None
	    case IAXInfoElement::AUTOANSWER:
		if (data[i]) {
		    i = 0xFFFF;
		    break;
		}
		ieList->append(new IAXInfoElement(IAXInfoElement::AUTOANSWER));
		i += 1;
		break;
	    default:
		Debug(DebugWarn,"IAXFrame(%u,%u) with unknown IE identifier %u [%p]",
		    frame->type(),frame->subclass(),data[i-1],frame);
		i = 0xFFFF;
	}
	if (i == 0xFFFF)
	    break;
	if (i == len -1)
	    i = 0xFFFF;
	else
	    i++;
    }
    invalid = (bool)(i == 0xFFFF);
    if (!invalid)
	return ieList;
    Debug(DebugWarn,"IAXFrame(%u,%u) with invalid IE [%p]",
	frame->type(),frame->subclass(),frame);
    delete ieList;
    return 0;
}

bool IAXFrame::createIEListBuffer(ObjList* ieList, DataBlock& buf)
{
    if (!ieList)
	return false;
    DataBlock data;
    buf.clear();
    for (ObjList* l = ieList->skipNull(); l; l = l->next()) {
	IAXInfoElement* ie = static_cast<IAXInfoElement*>(l->get());
	if (!ie)
	    continue;
	ie->toBuffer(data);
	buf.append(data);
    }
    return true;
}

IAXInfoElement* IAXFrame::getIE(ObjList* ieList, IAXInfoElement::Type type)
{
    if (!ieList)
	return 0;
    for (ObjList* l = ieList->skipNull(); l; l = l->next()) {
	IAXInfoElement* ie = static_cast<IAXInfoElement*>(l->get());
	if (!(ie && ie->type() == type))
	   continue;
	return ie;
    }
    return 0;
}

/**
 * IAXFullFrame
 */
IAXFullFrame::IAXFullFrame(Type type, u_int32_t subClass, u_int16_t sCallNo, u_int16_t dCallNo,
	unsigned char oSeqNo, unsigned char iSeqNo,
	u_int32_t tStamp, bool retrans,
	const unsigned char* buf, unsigned int len)
    : IAXFrame(type,sCallNo,tStamp,retrans,buf,len),
      m_dCallNo(dCallNo), m_oSeqNo(oSeqNo), m_iSeqNo(iSeqNo)
{
    XDebug(DebugAll,"IAXFullFrame::IAXFullFrame(%u,%u) [%p]",
	type,subClass,this);
    m_subclass = subClass;
}

IAXFullFrame::IAXFullFrame(Type type, u_int32_t subClass, u_int16_t sCallNo, u_int16_t dCallNo,
	unsigned char oSeqNo, unsigned char iSeqNo,
	u_int32_t tStamp,
	const unsigned char* buf, unsigned int len)
    : IAXFrame(type,sCallNo,tStamp,false,0,0),
      m_dCallNo(dCallNo), m_oSeqNo(oSeqNo), m_iSeqNo(iSeqNo)
{
    XDebug(DebugAll,"IAXFullFrame::IAXFullFrame(%u,%u) [%p]",
	type,subClass,this);

    unsigned char header[12];
    DataBlock ie;

    m_subclass = subClass;
    // Full frame flag + Source call number
    header[0] = 0x80 | (unsigned char)(m_sCallNo >> 8);
    header[1] = (unsigned char)(m_sCallNo);
    // Retrans + Destination call number
    header[2] = (unsigned char)(m_dCallNo >> 8);  // retrans is false: bit 7 is 0
    header[3] = (unsigned char)m_dCallNo;
    // Timestamp
    header[4] = (unsigned char)(m_tStamp >> 24);
    header[5] = (unsigned char)(m_tStamp >> 16);
    header[6] = (unsigned char)(m_tStamp >> 8);
    header[7] = (unsigned char)m_tStamp;
    // oSeqNo + iSeqNo
    header[8] = m_oSeqNo;
    header[9] = m_iSeqNo;
    // Type
    header[10] = m_type;
    // Subclass
    header[11] = packSubclass(m_subclass);
    // Set data
    m_data.assign(header,12);
    if (buf) {
	ie.assign((void*)buf,(unsigned int)len);
	m_data += ie;
    }
}

IAXFullFrame::~IAXFullFrame()
{
}

const IAXFullFrame* IAXFullFrame::fullFrame() const
{
    return this;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
