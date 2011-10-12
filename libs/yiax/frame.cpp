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

#include <string.h>  // For memcpy()
#include <stdio.h>   // For sprintf()

using namespace TelEngine;

inline void setStringFromInteger(String& dest, u_int32_t value, u_int8_t length)
{
    char tmp[11];
    switch (length) {
	case 1:
	    sprintf(tmp,"%0#4x",(u_int8_t)value);
	    break;
	case 2:
	    sprintf(tmp,"%0#6x",(u_int16_t)value);
	    break;
	default:
	    sprintf(tmp,"%0#10x",value);
	    break;
    }
    dest = tmp;
}

/*
 * IAXInfoElement
 */
TokenDict IAXInfoElement::s_ieData[] = {
    {"CALLED_NUMBER",     CALLED_NUMBER},
    {"CALLING_NUMBER",    CALLING_NUMBER},
    {"CALLING_ANI",       CALLING_ANI},
    {"CALLING_NAME",      CALLING_NAME},
    {"CALLED_CONTEXT",    CALLED_CONTEXT},
    {"USERNAME",          USERNAME},
    {"PASSWORD",          PASSWORD},
    {"CAPABILITY",        CAPABILITY},
    {"FORMAT",            FORMAT},
    {"LANGUAGE",          LANGUAGE},
    {"VERSION",           VERSION},
    {"ADSICPE",           ADSICPE},
    {"DNID",              DNID},
    {"AUTHMETHODS",       AUTHMETHODS},
    {"CHALLENGE",         CHALLENGE},
    {"MD5_RESULT",        MD5_RESULT},
    {"RSA_RESULT",        RSA_RESULT},
    {"APPARENT_ADDR",     APPARENT_ADDR},
    {"REFRESH",           REFRESH},
    {"DPSTATUS",          DPSTATUS},
    {"CALLNO",            CALLNO},
    {"CAUSE",             CAUSE},
    {"IAX_UNKNOWN",       IAX_UNKNOWN},
    {"MSGCOUNT",          MSGCOUNT},
    {"AUTOANSWER",        AUTOANSWER},
    {"MUSICONHOLD",       MUSICONHOLD},
    {"TRANSFERID",        TRANSFERID},
    {"RDNIS",             RDNIS},
    {"PROVISIONING",      PROVISIONING},
    {"AESPROVISIONING",   AESPROVISIONING},
    {"DATETIME",          DATETIME},
    {"DEVICETYPE",        DEVICETYPE},
    {"SERVICEIDENT",      SERVICEIDENT},
    {"FIRMWAREVER",       FIRMWAREVER},
    {"FWBLOCKDESC",       FWBLOCKDESC},
    {"FWBLOCKDATA",       FWBLOCKDATA},
    {"PROVVER",           PROVVER},
    {"CALLINGPRES",       CALLINGPRES},
    {"CALLINGTON",        CALLINGTON},
    {"CALLINGTNS",        CALLINGTNS},
    {"SAMPLINGRATE",      SAMPLINGRATE},
    {"CAUSECODE",         CAUSECODE},
    {"ENCRYPTION",        ENCRYPTION},
    {"ENKEY",             ENKEY},
    {"CODEC_PREFS",       CODEC_PREFS},
    {"RR_JITTER",         RR_JITTER},
    {"RR_LOSS",           RR_LOSS},
    {"RR_PKTS",           RR_PKTS},
    {"RR_DELAY",          RR_DELAY},
    {"RR_DROPPED",        RR_DROPPED},
    {"RR_OOO",            RR_OOO},
    {"CALLTOKEN",         CALLTOKEN},
    {0,0}
};

void IAXInfoElement::toBuffer(DataBlock& buf)
{
    unsigned char d[2] = {m_type,0};
    buf.assign(d,sizeof(d));
}

void IAXInfoElement::toString(String& buf)
{
    buf << "";
}

/*
 * IAXInfoElementString
 */
void IAXInfoElementString::toBuffer(DataBlock& buf)
{
    unsigned char d[2] = {type(),m_strData.length()};
    buf.assign(d,sizeof(d));
    buf.append(data());
}

/*
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
    unsigned char d[6] = {type(),m_length};

    switch (m_length) {
	case 1:
	    d[2] = (unsigned char)m_numericData;
	    break;
	case 2:
	    d[2] = (unsigned char)(m_numericData >> 8);
	    d[3] = (unsigned char)m_numericData;
	    break;
	case 4:
	    d[2] = (unsigned char)(m_numericData >> 24);
	    d[3] = (unsigned char)(m_numericData >> 16);
	    d[4] = (unsigned char)(m_numericData >> 8);
	    d[5] = (unsigned char)m_numericData;
	    break;
    }
    buf.assign(d,2 + m_length);
}

void IAXInfoElementNumeric::toString(String& buf)
{
    String tmp;
    setStringFromInteger(tmp,m_numericData,m_length);
    buf << tmp;
}

/*
 * IAXInfoElementBinary
 */
void IAXInfoElementBinary::toBuffer(DataBlock& buf)
{
    unsigned char d[2] = {type(),m_data.length()};
    buf.assign(d,sizeof(d));
    buf += m_data;
}

void IAXInfoElementBinary::toString(String& buf)
{
    if (!m_data.length())
	return;
    String tmp;
    tmp.hexify(m_data.data(),m_data.length(),' ');
    buf << tmp;
}

IAXInfoElementBinary* IAXInfoElementBinary::packIP(const SocketAddr& addr)
{
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

/*
 * IAXIEList
 */
IAXIEList::IAXIEList()
    : m_invalidIEList(false)
{
    XDebug(DebugInfo,"IAXIEList::IAXIEList() [%p]",this);
}

IAXIEList::IAXIEList(const IAXFullFrame* frame, bool incoming)
    : m_invalidIEList(false)
{
    XDebug(DebugInfo,"IAXIEList::IAXIEList(%p,%u) [%p]",frame,incoming,this);
    if (frame)
	createFromFrame(frame,incoming);
}

IAXIEList::~IAXIEList()
{
    XDebug(DebugInfo,"IAXIEList::~IAXIEList() [%p]",this);
}

void IAXIEList::insertVersion()
{
    if (!getIE(IAXInfoElement::VERSION))
	m_list.insert(new IAXInfoElementNumeric(IAXInfoElement::VERSION,IAX_PROTOCOL_VERSION,2));
}

bool IAXIEList::createFromFrame(const IAXFullFrame* frame, bool incoming)
{
    m_invalidIEList = false;
    m_list.clear();
    if (!frame)
	return true;
    if (frame->type() == IAXFrame::Voice || frame->type() == IAXFrame::Video)
	return true;
    unsigned char* data = (unsigned char*)(((IAXFullFrame*)frame)->data().data());
    unsigned int len = ((IAXFullFrame*)frame)->data().length();
    // Skip header for outgoing frames
    if (!incoming) {
	data += 12;
	len -= 12;
    }
    u_int16_t i;       // Current index of IE buffer
    u_int32_t value;
    if (frame->type() == IAXFrame::Text)
    {
	// Create even if text is empty
	appendString(IAXInfoElement::textframe,data,len);
	return true;
    }
    if (len < 2) {
	m_invalidIEList = len ? true : false;
	return !m_invalidIEList;
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
		    appendString((IAXInfoElement::Type)data[i-1],data+i+1,data[i]);
		else
		    appendString((IAXInfoElement::Type)data[i-1],0,0);
		i += data[i] + 1;
		break;
	    // Binary
	    case IAXInfoElement::CODEC_PREFS:        // LIST of strings
		if (data[i])
		    appendBinary((IAXInfoElement::Type)data[i-1],data+i+1,data[i]);
		else
		    appendBinary((IAXInfoElement::Type)data[i-1],0,0);
		i += data[i] + 1;
		break;
	    case IAXInfoElement::APPARENT_ADDR:
	    case IAXInfoElement::PROVISIONING:
	    case IAXInfoElement::AESPROVISIONING:
	    case IAXInfoElement::SERVICEIDENT:       // Length must be 6
	    case IAXInfoElement::FWBLOCKDATA:        // Length can be 0
	    case IAXInfoElement::ENKEY:
	    case IAXInfoElement::CALLTOKEN:
		if (data[i-1] == IAXInfoElement::SERVICEIDENT && data[i] != 6) {
		    i = 0xFFFF;
		    break;
		}
		appendBinary((IAXInfoElement::Type)data[i-1],data+i+1,data[i]);
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
		appendNumeric((IAXInfoElement::Type)data[i-1],value,4);
		i += 5;
		break;
	    // 2 bytes
	    case IAXInfoElement::VERSION:
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
		appendNumeric((IAXInfoElement::Type)data[i-1],value,2);
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
		appendNumeric((IAXInfoElement::Type)data[i-1],value,1);
		i += 2;
		break;
	    // None
	    case IAXInfoElement::AUTOANSWER:
		if (data[i]) {
		    i = 0xFFFF;
		    break;
		}
		appendNull(IAXInfoElement::AUTOANSWER);
		i += 1;
		break;
	    default:
		Debug(DebugInfo,"IAX Frame(%u,%u) with unknown IE identifier %u [%p]",
		    frame->type(),frame->subclass(),data[i-1],frame);
		appendBinary((IAXInfoElement::Type)data[i-1],data+i+1,data[i]);
		i += data[i] + 1;
	}
	if (i == 0xFFFF)
	    break;
	if (i == len -1)
	    i = 0xFFFF;
	else
	    i++;
    }
    m_invalidIEList = i == 0xFFFF;
    if (!m_invalidIEList)
	return true;
    Debug(DebugWarn,"IAXIEList::createFromFrame. Frame(%u,%u) with invalid IE [%p]",frame->type(),frame->subclass(),frame);
    return false;
}

void IAXIEList::toBuffer(DataBlock& buf)
{
    DataBlock data;
    buf.clear();
    ObjList* obj = m_list.skipNull();
    for (; obj; obj = obj->skipNext()) {
	IAXInfoElement* ie = static_cast<IAXInfoElement*>(obj->get());
	ie->toBuffer(data);
	buf.append(data);
    }
}

void IAXIEList::toString(String& dest, const char* indent)
{
    ObjList* obj = m_list.skipNull();
    for (; obj; obj = obj->skipNext()) {
	IAXInfoElement* ie = static_cast<IAXInfoElement*>(obj->get());
	dest << indent;
	if (ie->type() == IAXInfoElement::textframe) {
	    ie->toString(dest);
	    continue;
	}
	const char* name = IAXInfoElement::ieText(ie->type());
	if (name)
	    dest << name;
	else {
	    u_int8_t t = ie->type();
	    String tmp;
	    tmp.hexify(&t,1);
	    dest << "0x" << tmp;
	}
	if (ie->type() != IAXInfoElement::AUTOANSWER)
	    dest << ": ";
	switch (ie->type()) {
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
	    case IAXInfoElement::MUSICONHOLD:
	    case IAXInfoElement::RDNIS:
	    case IAXInfoElement::DEVICETYPE:
		ie->toString(dest);
		break;
	    case IAXInfoElement::CODEC_PREFS:		//TODO: LIST of strings ?
		{
		const char* tmp = (const char*)((static_cast<IAXInfoElementBinary*>(ie))->data().data());
		String s(tmp,(static_cast<IAXInfoElementBinary*>(ie))->length());
		dest << s;
		}
		break;
	    // Binary
	    case IAXInfoElement::APPARENT_ADDR:
		{
		SocketAddr addr;
		IAXInfoElementBinary::unpackIP(addr,static_cast<IAXInfoElementBinary*>(ie));
		dest << addr.host() << ':' << addr.port();
		}
		break;
	    case IAXInfoElement::PROVISIONING:
	    case IAXInfoElement::AESPROVISIONING:
	    case IAXInfoElement::SERVICEIDENT:
	    case IAXInfoElement::FWBLOCKDATA:
	    case IAXInfoElement::ENKEY:
    	    case IAXInfoElement::CALLTOKEN:
		ie->toString(dest);
		break;
	    // 4 bytes
	    case IAXInfoElement::CAPABILITY:
	    case IAXInfoElement::FORMAT:
	    case IAXInfoElement::AUTHMETHODS:
		{
		ie->toString(dest);
		u_int32_t val = (static_cast<IAXInfoElementNumeric*>(ie))->data();
		String tmp;
		if (ie->type() == IAXInfoElement::AUTHMETHODS)
		    IAXAuthMethod::authList(tmp,val,',');
		else
		    IAXFormat::formatList(tmp,val);
		dest << " (" << tmp << ')';
		}
		break;
	    case IAXInfoElement::DATETIME:		//TODO: print more data
		ie->toString(dest);
		break;
	    case IAXInfoElement::SAMPLINGRATE:
		dest << (unsigned int)((static_cast<IAXInfoElementNumeric*>(ie))->data()) << " Hz";
		break;
	    case IAXInfoElement::RR_LOSS:
		{
		u_int32_t val = (static_cast<IAXInfoElementNumeric*>(ie))->data();
		unsigned int percent = (unsigned int)(val & 0xFF000000);
		unsigned int count = (unsigned int)(val & 0x00FFFFFF);
		dest << count << " (" << percent << "%)";
		}
		break;
	    case IAXInfoElement::RR_JITTER:
	    case IAXInfoElement::RR_PKTS:
	    case IAXInfoElement::RR_DROPPED:
	    case IAXInfoElement::RR_OOO:
	    case IAXInfoElement::RR_DELAY:
		dest << (unsigned int)((static_cast<IAXInfoElementNumeric*>(ie))->data());
		break;
	    case IAXInfoElement::TRANSFERID:
	    case IAXInfoElement::PROVVER:
	    case IAXInfoElement::FWBLOCKDESC:
		ie->toString(dest);
		break;
	    // 2 bytes
	    case IAXInfoElement::REFRESH:
		dest << (unsigned int)((static_cast<IAXInfoElementNumeric*>(ie))->data());
		dest << " second(s)";
		break;
	    case IAXInfoElement::ADSICPE:
	    case IAXInfoElement::DPSTATUS:		//TODO: print more data
	    case IAXInfoElement::CALLNO:
	    case IAXInfoElement::CALLINGTNS:		//TODO: print more data
	    case IAXInfoElement::FIRMWAREVER:
	    case IAXInfoElement::VERSION:
		ie->toString(dest);
		break;
	    // 1 byte
	    case IAXInfoElement::IAX_UNKNOWN:
	    case IAXInfoElement::CALLINGPRES:		//TODO: print more data
	    case IAXInfoElement::CALLINGTON:		//TODO: print more data
	    case IAXInfoElement::CAUSECODE:		//TODO: print more data
	    case IAXInfoElement::ENCRYPTION:		//TODO: print more data
		ie->toString(dest);
		break;
	    case IAXInfoElement::MSGCOUNT:
		{
		u_int16_t val = (static_cast<IAXInfoElementNumeric*>(ie))->data();
		dest << (int)((u_int8_t)val) << "new. " << (int)(val >> 8) << "old";
		}
		break;
	    // None
	    case IAXInfoElement::AUTOANSWER:
		break;
	    default: ;
		ie->toString(dest);
	}
    }
}

IAXInfoElement* IAXIEList::getIE(IAXInfoElement::Type type)
{
    for (ObjList* l = m_list.skipNull(); l; l = l->next()) {
	IAXInfoElement* ie = static_cast<IAXInfoElement*>(l->get());
	if (ie && ie->type() == type)
	    return ie;
    }
    return 0;
}

bool IAXIEList::getString(IAXInfoElement::Type type, String& dest)
{
    IAXInfoElementString* ie = static_cast<IAXInfoElementString*>(getIE(type));
    dest.clear();
    if (!ie)
	return false;
    dest = ie->data();
    return true;
}

bool IAXIEList::getNumeric(IAXInfoElement::Type type, u_int32_t& dest)
{
    IAXInfoElementNumeric* ie = static_cast<IAXInfoElementNumeric*>(getIE(type));
    if (!ie)
	return false;
    dest = ie->data();
    return true;
}

bool IAXIEList::getBinary(IAXInfoElement::Type type, DataBlock& dest)
{
    IAXInfoElementBinary* ie = static_cast<IAXInfoElementBinary*>(getIE(type));
    dest.clear();
    if (!ie)
	return false;
    dest = ie->data();
    return true;
}

/*
 * IAXAuthMethod
 */
TokenDict IAXAuthMethod::s_texts[] = {
    {"Text", Text},
    {"MD5",  MD5},
    {"RSA",  RSA},
    {0,0}
};

void IAXAuthMethod::authList(String& dest, u_int16_t auth, char sep)
{
    dest = "";
    bool first = true;
    for (u_int16_t i = 0; s_texts[i].value; i++) {
	if (0 == (s_texts[i].value & auth))
	    continue;
	if (first)
	    first = false;
	else
	    dest += sep;
	dest += s_texts[i].token;
    }
}

/*
 * IAXFormat
 */
const TokenDict IAXFormat::s_formats[] = {
    {"G.723.1",      G723_1},
    {"GSM",          GSM},
    {"G.711 mu-law", ULAW},
    {"G.711 a-law",  ALAW},
    {"G.726",        G726},
    {"IMA ADPCM",    ADPCM},
    {"SLIN",         SLIN},
    {"LPC10",        LPC10},
    {"G.729",        G729},
    {"SPEEX",        SPEEX},
    {"ILBC",         ILBC},
    {"G.726 AAL2",   G726AAL2},
    {"G.722",        G722},
    {"AMR",          AMR},
    {"JPEG",         JPEG},
    {"PNG",          PNG},
    {"H261",         H261},
    {"H263",         H263},
    {"H263p",        H263p},
    {"H264",         H264},
    {0,0}
};

const TokenDict IAXFormat::s_types[] = {
    {"audio",   Audio},
    {"video",   Video},
    {"image",   Image},
    {0,0}
};

// Set format
void IAXFormat::set(u_int32_t* fmt, u_int32_t* fmtIn, u_int32_t* fmtOut)
{
    if (fmt)
	m_format = mask(*fmt,m_type);
    if (fmtIn)
	m_formatIn = mask(*fmtIn,m_type);
    if (fmtOut)
	m_formatOut = mask(*fmtOut,m_type);
}

void IAXFormat::formatList(String& dest, u_int32_t formats, const TokenDict* dict,
    const char* sep)
{
    if (!dict)
	dict = s_formats;
    for (; dict->value; dict++)
	if (0 != (dict->value & formats))
	    dest.append(dict->token,sep);
}

// Pick a format from a list of capabilities
u_int32_t IAXFormat::pickFormat(u_int32_t formats, u_int32_t format)
{
    if (0 != (format & formats))
        return format;
    if (!formats)
	return 0;
    format = 1;
    for (unsigned int i = 0; i < (8 * sizeof(u_int32_t)); i++, format = format << 1)
	if (0 != (format & formats))
	    return format;
    return 0;
}

// Encode a formats list
u_int32_t IAXFormat::encode(const String& formats, const TokenDict* dict, char sep)
{
    if (!dict)
	return 0;
    u_int32_t mask = 0;
    ObjList* list = formats.split(',',false);
    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	int fmt = lookup(o->get()->toString(),dict);
	if (fmt > 0)
	    mask |= fmt;
    }
    TelEngine::destruct(list);
    return mask;
}


/*
* IAXControl
*/
TokenDict IAXControl::s_types[] = {
        {"NEW",        New},
        {"PING",       Ping},
        {"PONG",       Pong},
        {"ACK",        Ack},
        {"HANGUP",     Hangup},
        {"REJECT",     Reject},
        {"ACCEPT",     Accept},
        {"AUTHREQ",    AuthReq},
        {"AUTHREP",    AuthRep},
        {"INVAL",      Inval},
        {"LAGRQ",      LagRq},
        {"LAGRP",      LagRp},
        {"REGREQ",     RegReq},
        {"REGAUTH",    RegAuth},
        {"REGACK",     RegAck},
        {"REGREJ",     RegRej},
        {"REGREL",     RegRel},
        {"VNAK",       VNAK},
        {"DPREQ",      DpReq},
        {"DPREP",      DpRep},
        {"DIAL",       Dial},
        {"TXREQ",      TxReq},
        {"TXCNT",      TxCnt},
        {"TXACC",      TxAcc},
        {"TXREADY",    TxReady},
        {"TXREL",      TxRel},
        {"TXREJ",      TxRej},
        {"QUELCH",     Quelch},
        {"UNQUELCH",   Unquelch},
        {"POKE",       Poke},
        {"MWI",        MWI},
        {"UNSUPPORT",  Unsupport},
        {"TRANSFER",   Transfer},
        {"PROVISION",  Provision},
        {"FWDOWNL",    FwDownl},
        {"FWDATA",     FwData},
        {"CALLTOKEN",  CallToken},
        {0,0}
	};

/*
* IAXFrame
*/
TokenDict IAXFrame::s_types[] = {
        {"DTMF",    DTMF},
        {"VOICE",   Voice},
        {"VIDEO",   Video},
        {"CONTROL", Control},
        {"NULL",    Null},
        {"IAX",     IAX},
        {"TEXT",    Text},
        {"IMAGE",   Image},
        {"HTML",    HTML},
        {"NOISE",   Noise},
        {0,0}
	};

IAXFrame::IAXFrame(Type type, u_int16_t sCallNo, u_int32_t tStamp, bool retrans,
		   const unsigned char* buf, unsigned int len, bool mark)
    : m_data((char*)buf,len,true), m_retrans(retrans), m_type(type),
      m_sCallNo(sCallNo), m_tStamp(tStamp), m_mark(mark)
{
//    XDebug(DebugAll,"IAXFrame::IAXFrame(%u) [%p]",type,this);
}

IAXFrame::~IAXFrame()
{
//    XDebug(DebugAll,"IAXFrame::~IAXFrame() [%p]",this);
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
	u_int32_t sc = 0;
	bool mark = false;
	if (buf[10] != IAXFrame::Video)
	    sc = IAXFrame::unpackSubclass(buf[11]);
	else {
	    mark = 0 != (buf[11] & 0x40);
	    // Clear the mark flag
	    sc = IAXFrame::unpackSubclass(buf[11] & 0xbf);
	}
	u_int32_t ts = (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];
	return new IAXFullFrame((IAXFrame::Type)buf[10],sc,scn,dcn,buf[8],buf[9],ts,retrans,buf+12,len-12,mark);
    }
    // Meta frame ?
    if (scn == 0) {
	if (dcn & 0x8000) {
	    // Meta video
	    if (len < 6)
		return 0;
	    // Timestamp: lowest 15 bits of transmiter timestamp
	    scn = ((buf[4] & 0x7f) << 8) | buf[5];
	    bool mark = (0 != (buf[4] & 0x80));
	    return new IAXFrame(IAXFrame::Video,dcn & 0x7fff,scn & 0x7fff,false,buf+6,len-6,mark);
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
	    // Trunk timestamps (mini frames)
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
	else {
	    // No trunk timestamps
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
	return 0;
    }
    // Mini frame
    return new IAXFrame(IAXFrame::Voice,scn,dcn,false,buf+4,len-4);
}

// Build a miniframe buffer
void IAXFrame::buildMiniFrame(DataBlock& dest, u_int16_t sCallNo, u_int32_t tStamp,
    void* data, unsigned int len)
{
    unsigned char header[4] = {sCallNo >> 8,sCallNo & 0xff,tStamp >> 8,tStamp & 0xff};
    dest.assign(header,4);
    dest.append(data,len);
}

// Build a video meta frame buffer
void IAXFrame::buildVideoMetaFrame(DataBlock& dest, u_int16_t sCallNo, u_int32_t tStamp,
    bool mark, void* data, unsigned int len)
{
    unsigned char header[6] = {0,0};
    header[2] = 0x80 | ((sCallNo >> 8) & 0x7f);
    header[3] = (unsigned char)sCallNo;
    header[4] = (tStamp >> 8) & 0x7f;
    if (mark)
	header[4] |= 0x80;
    header[5] = tStamp;
    dest.assign(header,6);
    dest.append(data,len);
}

u_int8_t IAXFrame::packSubclass(u_int32_t value)
{
    if (value < 0x80)
	return (u_int8_t)value;
    if (value == 0x80)
	return 0x87;
    if ((value > 0x9f) && (value <= 0xff)) {
	Debug(DebugMild,"IAXFrame nonstandard pack %u",value);
	return 0;
    }
    // No need to start from zero, we already know it's >= 2^8
    u_int32_t v = 0x100;
    for (u_int8_t i = 8; i < 32; i++) {
	if (v == value)
	    return i | 0x80;
	v <<= 1;
    }
    Debug(DebugGoOn,"IAXFrame could not pack subclass %u (0x%x)",value,value);
    return 0;
}

u_int32_t IAXFrame::unpackSubclass(u_int8_t value)
{
    if (value > 0x9f) {
	DDebug(DebugMild,"IAXFrame nonstandard unpack %u",value);
	return 0;
    }
    if (value & 0x80)
	return 1 << (value & 0x7f);
    return value;
}

IAXFullFrame* IAXFrame::fullFrame()
{
    return 0;
}

/*
 * IAXFullFrame
 */
TokenDict IAXFullFrame::s_controlTypes[] = {
        {"HANGUP",      Hangup},
//        {"RING",        Ring},
        {"RINGING",     Ringing},
        {"ANSWER",      Answer},
        {"BUSY",        Busy},
        {"CONGESTION",  Congestion},
        {"FLASHHOOK",   FlashHook},
        {"OPTION",      Option},
        {"KEYRADIO",    KeyRadio},
        {"UNKEYRADIO",  UnkeyRadio},
        {"PROGRESSING", Progressing},
        {"PROCEEDING",  Proceeding},
        {"HOLD",        Hold},
        {"UNHOLD",      Unhold},
        {"VIDUPDATE",   VidUpdate},
        {0,0}
	};

IAXFullFrame::IAXFullFrame(Type type, u_int32_t subclass, u_int16_t sCallNo, u_int16_t dCallNo,
	unsigned char oSeqNo, unsigned char iSeqNo,
	u_int32_t tStamp, bool retrans,
	const unsigned char* buf, unsigned int len, bool mark)
    : IAXFrame(type,sCallNo,tStamp,retrans,buf,len,mark),
      m_dCallNo(dCallNo), m_oSeqNo(oSeqNo), m_iSeqNo(iSeqNo), m_subclass(subclass),
      m_ieList(0)
{
    XDebug(DebugAll,
	"IAXFullFrame() incoming type=%u subclass=%u callno=(%u,%u) seq=(%u,%u) ts=%u retrans=%u [%p]",
	type,subclass,sCallNo,dCallNo,oSeqNo,iSeqNo,tStamp,retrans,this);
}

IAXFullFrame::IAXFullFrame(Type type, u_int32_t subclass, u_int16_t sCallNo, u_int16_t dCallNo,
	unsigned char oSeqNo, unsigned char iSeqNo,
	u_int32_t tStamp,
	const unsigned char* buf, unsigned int len, bool mark)
    : IAXFrame(type,sCallNo,tStamp,false,0,0,mark),
      m_dCallNo(dCallNo), m_oSeqNo(oSeqNo), m_iSeqNo(iSeqNo), m_subclass(subclass),
      m_ieList(0)
{
    XDebug(DebugAll,
	"IAXFullFrame() outgoing type=%u subclass=%u callno=(%u,%u) seq=(%u,%u) ts=%u [%p]",
	type,subclass,sCallNo,dCallNo,oSeqNo,iSeqNo,tStamp,this);
    setDataHeader();
    if (buf)
	m_data.append((void*)buf,(unsigned int)len);
}

// Constructor. Constructs an outgoing full frame
IAXFullFrame::IAXFullFrame(Type type, u_int32_t subclass, u_int16_t sCallNo, u_int16_t dCallNo,
		 unsigned char oSeqNo, unsigned char iSeqNo,
		 u_int32_t tStamp, IAXIEList* ieList, u_int16_t maxlen, bool mark)
    : IAXFrame(type,sCallNo,tStamp,false,0,0,mark),
      m_dCallNo(dCallNo), m_oSeqNo(oSeqNo), m_iSeqNo(iSeqNo), m_subclass(subclass),
      m_ieList(ieList)
{
    XDebug(DebugAll,
	"IAXFullFrame() outgoing type=%u subclass=%u callno=(%u,%u) seq=(%u,%u) ts=%u [%p]",
	type,subclass,sCallNo,dCallNo,oSeqNo,iSeqNo,tStamp,this);
    updateBuffer(maxlen);
}

void IAXFullFrame::toString(String& dest, const SocketAddr& local,
	const SocketAddr& remote, bool incoming)
{
#define STARTLINE(indent) "\r\n" << indent
    const char* enclose = "\r\n-----";
    dest << enclose;
    String stmp;
    setStringFromInteger(stmp,type(),1);
    dest << STARTLINE("") << typeText(type()) << " (" << stmp << ")";
    String extra;
    // Subclass
    String subc;
    switch (type()) {
	case IAXFrame::IAX:
	case IAXFrame::Control:
	    subc = (type() == IAXFrame::IAX ? IAXControl::typeText(subclass()) :
		controlTypeText(subclass()));
	    break;
	case IAXFrame::DTMF:
	    subc << (char)subclass();
	    break;
	case IAXFrame::Video:
	    extra << "Mark: " << String::boolText(mark());
	    // fallthrough
	case IAXFrame::Voice:
	case IAXFrame::Image:
	    IAXFormat::formatList(subc,subclass());
	    break;
	case IAXFrame::Null:
	    subc = "Subclass: ";
	    break;
	case IAXFrame::Text:
	    subc = "Subclass: ";
	    break;
	case IAXFrame::HTML:
	    subc = "Subclass: ";
	    break;
	case IAXFrame::Noise:
	    subc << (unsigned int)(subclass()) << " -dBov";
	    break;
	default:
	    ;
    }
    setStringFromInteger(stmp,subclass(),4);
    dest << " - " << (subc ? subc.c_str() : "???") << " (" << stmp << ")";
    // Addresses
    if (incoming)
	dest << STARTLINE("  ") << "Incoming from ";
    else
	dest << STARTLINE("  ") << "Outgoing to ";
    dest << remote.host() << ':' << remote.port();
    dest << " (Local address: ";
    dest << local.host() << ':' << local.port() << ')';
    // Transaction numbers
    dest << STARTLINE("  ") << "Call (Local:Remote): ";
    if (incoming)
	dest << destCallNo();
    else
	dest << sourceCallNo();
    dest << ':';
    if (incoming)
	dest << sourceCallNo();
    else
	dest << destCallNo();
    // Info
    dest << ". Timestamp: " << (unsigned int)(timeStamp());
    dest << ". Retrans: " << String::boolText(retrans());
    dest << ". Sequence numbers: Out: " << oSeqNo() << " In: " << iSeqNo();
    if (extra)
	dest << STARTLINE("  ") << extra;
    // IEs
    updateIEList(incoming);
    if (!m_ieList->empty()) {
	String aux;
	aux << STARTLINE("  ");
	m_ieList->toString(dest,aux);
    }
    if (m_ieList->empty()) {
	dest << STARTLINE("  ");
	if (m_ieList->invalidIEList())
	    dest << "Error parsing Information Element(s)";
	else
	    dest << "No Information Element(s)";
    }
    dest << enclose;
#undef STARTLINE
}

// Rebuild frame buffer from the list of IEs
void IAXFullFrame::updateBuffer(u_int16_t maxlen)
{
    setDataHeader();
    if (!m_ieList)
	return;
    DataBlock tmp;
    m_ieList->toBuffer(tmp);
    if (tmp.length() <= maxlen)
	m_data += tmp;
    else
	Debug(DebugNote,"Frame(%u,%u) buffer too long (%u > %u) [%p]",
	    type(),subclass(),tmp.length(),maxlen,this);
}

// Update IE list from buffer if not already done
bool IAXFullFrame::updateIEList(bool incoming)
{
    if (!m_ieList)
	m_ieList = new IAXIEList(this,incoming);
    return !m_ieList->invalidIEList();
}

// Remove the IE list
IAXIEList* IAXFullFrame::removeIEList(bool delObj)
{
    if (!m_ieList)
	return 0;
    IAXIEList* old = m_ieList;
    m_ieList = 0;
    if (delObj) {
	delete old;
	old = 0;
    }
    return old;
}

IAXFullFrame::~IAXFullFrame()
{
    XDebug(DebugAll,"IAXFullFrame::~IAXFullFrame(%u,%u) [%p]",type(),m_subclass,this);
}

IAXFullFrame* IAXFullFrame::fullFrame()
{
    return this;
}

// Destroyed notification. Clear data
void IAXFullFrame::destroyed()
{
    removeIEList();
    IAXFrame::destroyed();
}

// Build frame buffer header
void IAXFullFrame::setDataHeader()
{
    unsigned char header[12];
    // Full frame flag + Source call number
    header[0] = 0x80 | (unsigned char)(sourceCallNo() >> 8);
    header[1] = (unsigned char)(sourceCallNo());
    // Retrans + Destination call number
    header[2] = (unsigned char)(destCallNo() >> 8);  // retrans is false: bit 7 is 0
    header[3] = (unsigned char)destCallNo();
    // Timestamp
    header[4] = (unsigned char)(timeStamp() >> 24);
    header[5] = (unsigned char)(timeStamp() >> 16);
    header[6] = (unsigned char)(timeStamp() >> 8);
    header[7] = (unsigned char)timeStamp();
    // oSeqNo + iSeqNo
    header[8] = m_oSeqNo;
    header[9] = m_iSeqNo;
    // Type
    header[10] = type();
    // Subclass
    header[11] = packSubclass(m_subclass);
    if (mark())
	header[11] |= 0x40;
    // Set data
    m_data.assign(header,sizeof(header));
}

/*
 * IAXFrameOut
 */
void IAXFrameOut::setRetrans()
{
    if (!m_retrans) {
	m_retrans = true;
	((unsigned char*)m_data.data())[2] |= 0x80;
    }
}

void IAXFrameOut::transmitted()
{
    if (m_retransCount) {
	m_retransCount--;
	m_retransTimeInterval *= 2;
	m_nextTransTime += m_retransTimeInterval;
   }
}

void IAXFrameOut::adjustAuthTimeout(u_int64_t nextTransTime)
{
    if (!(type() == IAXFrame::IAX && (subclass() == IAXControl::AuthReq || subclass() ==IAXControl::RegAuth)))
	return;
    m_retransCount = 1;
    m_nextTransTime = nextTransTime;
}

/*
* IAXMetaTrunkFrame
*/
#define IAX2_METATRUNK_HEADERLENGTH 8
#define IAX2_MINIFRAME_HEADERLENGTH 6

IAXMetaTrunkFrame::IAXMetaTrunkFrame(IAXEngine* engine, const SocketAddr& addr)
    : Mutex(true,"IAXMetaTrunkFrame"),
      m_data(0), m_dataAddIdx(IAX2_METATRUNK_HEADERLENGTH), m_engine(engine), m_addr(addr)
{
    m_data = new u_int8_t[m_engine->maxFullFrameDataLen()];
    // Meta indicator
    *(u_int16_t*)m_data = 0;
    // Meta command & Command data (use timestamps)
    m_data[2] = 1;
    m_data[3] = 1;
    // Frame timestamp
    setTimestamp((u_int32_t)Time::msecNow());
}

IAXMetaTrunkFrame::~IAXMetaTrunkFrame()
{ 
    m_engine->removeTrunkFrame(this);
    delete[] m_data; 
}

void IAXMetaTrunkFrame::setTimestamp(u_int32_t tStamp)
{
    m_timestamp = tStamp;
    m_data[4] = (u_int8_t)(tStamp >> 24);
    m_data[5] = (u_int8_t)(tStamp >> 16);
    m_data[6] = (u_int8_t)(tStamp >> 8);
    m_data[7] = (u_int8_t)tStamp;
}

bool IAXMetaTrunkFrame::add(u_int16_t sCallNo, const DataBlock& data, u_int32_t tStamp)
{
    Lock lock(this);
    bool b = true;
    // Do we have data ?
    if (!data.length())
	return b;
    // If no more room, send it
    if (m_dataAddIdx + data.length() + IAX2_MINIFRAME_HEADERLENGTH > m_engine->maxFullFrameDataLen())
	b = send((u_int32_t)Time::msecNow());
    // Is the first mini frame ?
    if (m_dataAddIdx == IAX2_METATRUNK_HEADERLENGTH)
	m_timestamp = (u_int32_t)Time::msecNow();
    // Add the mini frame
    m_data[m_dataAddIdx++] = (u_int8_t)(data.length() >> 8);
    m_data[m_dataAddIdx++] = (u_int8_t)data.length();
    m_data[m_dataAddIdx++] = (u_int8_t)(sCallNo >> 8);
    m_data[m_dataAddIdx++] = (u_int8_t)sCallNo;
    m_data[m_dataAddIdx++] = (u_int8_t)(tStamp >> 8);
    m_data[m_dataAddIdx++] = (u_int8_t)tStamp;
    memcpy(m_data + m_dataAddIdx,data.data(),data.length());
    m_dataAddIdx += data.length();
    return b;
}

bool IAXMetaTrunkFrame::send(u_int32_t tStamp)
{
    Lock lock(this);
    setTimestamp(tStamp);
    bool b = m_engine->writeSocket(m_data,m_dataAddIdx,m_addr);
    // Reset index & timestamp
    m_dataAddIdx = IAX2_METATRUNK_HEADERLENGTH;
    m_timestamp = 0;
    return b;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
