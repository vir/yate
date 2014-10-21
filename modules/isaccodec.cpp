/**
 * isaccodec.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * iSAC codec using iSAC library based on WebRTC project.
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
 *
 *
 * Copyright (c) 2011, The WebRTC project authors. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 *  * Neither the name of Google nor the names of its contributors may
 *    be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <yatephone.h>

extern "C" {
#include "signal_processing_library.h"
#ifdef ISAC_FIXED
#include "isacfix.h"
#else
#include "isac.h"
#endif
}

// ISAC frame size (in milliseconds) to set in encoder and format info
// 0: use default (don't set), 30/60ms otherwise
#define ISAC_FRAME_SIZE_MS 30

// Coding mode:
// 0: Channel-adaptive: the bit rate is adjusted by the encoder
// 1: Channel-independent: fixed bit rate
#define ISAC_CODING_ADAPTIVE 0
#define ISAC_CODING_INDEPENDENT 1

#ifndef ISAC_CODING_MODE
#define ISAC_CODING_MODE ISAC_CODING_INDEPENDENT
//#define ISAC_CODING_MODE ISAC_CODING_ADAPTIVE
#endif

// Channel independent: REQUIRED: set it to 32000 (default library value)
// Channel adaptive: set it to 0 to use default
#define ISAC_RATE 32000

// Maximum number of concealed lost frames, can be 1 or 2
#define ISAC_MAX_PLC 2

#ifdef NO_ISAC_PLC
#undef ISAC_MAX_PLC
#define ISAC_MAX_PLC 1
#endif

using namespace TelEngine;
namespace { // anonymous

class iSACCodec : public DataTranslator
{
public:
    iSACCodec(const char* sFormat, const char* dFormat, bool encoding);
    ~iSACCodec();
    inline bool valid() const
	{ return m_isac != 0; }
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp,
	unsigned long flags);
    void timerTick();
private:
    // Retrieve the ISAC error
    inline WebRtc_Word16 isacGetError() const
	{
#ifdef ISAC_FIXED
	    return WebRtcIsacfix_GetErrorCode(m_isac);
#else
	    return WebRtcIsac_GetErrorCode(m_isac);
#endif
	}
    // Check error after encode/decode
    // Forward data if result is greater then 0 and return the number of bytes forwarded
    // Update last error. Output a debug message if error changed
    unsigned long processCodecResult(WebRtc_Word16 result, unsigned int inBytes,
	unsigned long tStamp, unsigned long flags);
    // Initialize the codec structure. Return false on failure
    bool isacInit();
    // Release the ISAC structure
    void isacFree();

    bool m_encoding;                     // Encoder/decoder flag
#ifdef ISAC_FIXED
    ISACFIX_MainStruct* m_isac;          // ISAC library structure
#else
    ISACStruct* m_isac;
#endif
    WebRtc_Word16 m_error;               // Last error
    DataBlock m_outData;                 // Codec output
    WebRtc_Word16 m_mode;                // Encoder mode (chan adaptive/instantaneous)
    unsigned int m_encodeChunk;          // Encoder input data length in bytes
    unsigned long m_tStamp;              // Encoder timestamp
    DataBlock m_buffer;                  // Encoder buffer for incomplete data
    // Statistics
    unsigned long m_inPackets;
    unsigned long m_outPackets;
    unsigned long m_inBytes;
    unsigned long m_outBytes;
    unsigned long m_failedBytes;
};

class iSACFactory : public TranslatorFactory
{
public:
    iSACFactory();
    virtual const TranslatorCaps* getCapabilities() const
	{ return m_caps; }
    virtual DataTranslator* create(const DataFormat& sFormat, const DataFormat& dFormat);
private:
    const TranslatorCaps* m_caps;
};

class iSACModule : public Module
{
public:
    iSACModule();
    ~iSACModule();
    inline void incCount() {
	    Lock mylock(this);
	    m_count++;
	}
    inline void decCount() {
	    Lock mylock(this);
	    m_count--;
	}
    virtual void initialize();
    virtual bool isBusy() const
	{ return (m_count != 0); }
protected:
    virtual void statusParams(String& str);
private:
    int m_count;                         // Current number of codecs
    iSACFactory* m_factory;              // Factory used to create codecs
};


INIT_PLUGIN(iSACModule);

static TranslatorCaps s_caps[] = {
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 }
};

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow)
	return !__plugin.isBusy();
    return true;
}


/*
 * iSACFactory
 */
iSACCodec::iSACCodec(const char* sFormat, const char* dFormat, bool encoding)
    : DataTranslator(sFormat,dFormat), m_encoding(encoding),
    m_isac(0), m_error(0), m_mode(ISAC_CODING_MODE), m_encodeChunk(0), m_tStamp(0),
    m_inPackets(0), m_outPackets(0), m_inBytes(0), m_outBytes(0), m_failedBytes(0)
{
    Debug(&__plugin,DebugAll,"iSACCodec(\"%s\",\"%s\",%scoding) [%p]",
	sFormat,dFormat,m_encoding ? "en" : "de",this);
    __plugin.incCount();
    isacInit();
}

iSACCodec::~iSACCodec()
{
    isacFree();
    Debug(&__plugin,DebugAll,
	"iSACCodec(%scoding) destroyed packets in/out=%lu/%lu bytes in/out/failed=%lu/%lu/%lu [%p]",
	m_encoding ? "en" : "de",m_inPackets,m_outPackets,m_inBytes,m_outBytes,m_failedBytes,this);
    __plugin.decCount();
}

unsigned long iSACCodec::Consume(const DataBlock& data, unsigned long tStamp,
    unsigned long flags)
{
    XDebug(&__plugin,DebugAll,"%scoder::Consume(%u,%lu,%lu) buffer=%u [%p]",
	m_encoding ? "En" : "De",data.length(),tStamp,flags,m_buffer.length(),this);
    m_inBytes += data.length();
    m_inPackets++;
    if (!(valid() && getTransSource())) {
	m_failedBytes += data.length();
	return 0;
    }
    if (data.null() && (flags & DataSilent))
	return getTransSource()->Forward(data,tStamp,flags);
    ref();
    WebRtc_Word16 res = 0;
    WebRtc_Word16* out = (WebRtc_Word16*)m_outData.data();
    unsigned long len = 0;
    if (m_encoding) {
	// NOTE: draft-ietf-avt-rtp-isac-00.txt section 3.4:
	// More than one iSAC payload block MUST NOT be included in an RTP packet by a sender
	// Forward data when encoded, don't accumulate the encoder output

	if (tStamp == invalidStamp())
	    tStamp = 0;
	tStamp -= m_tStamp;
	// Avoid copying data if our buffer is empty
	const DataBlock* inDataBlock = &data;
	if (m_buffer.length()) {
	    tStamp -= (m_buffer.length() / 2);
	    m_buffer += data;
	    inDataBlock = &m_buffer;
	}
	const unsigned char* ptr = (const unsigned char*)inDataBlock->data();
	unsigned int remaining = inDataBlock->length();
	unsigned int tsChunk = m_encodeChunk / 2;
	while (remaining >= m_encodeChunk) {
	    // Encode returns the number of bytes set in output buffer
#ifdef ISAC_FIXED
	    res = WebRtcIsacfix_Encode(m_isac,(const WebRtc_Word16*)ptr,out);
#else
	    res = WebRtcIsac_Encode(m_isac,(const WebRtc_Word16*)ptr,out);
#endif
	    remaining -= m_encodeChunk;
	    ptr += m_encodeChunk;
	    m_tStamp += tsChunk;
	    unsigned long l = processCodecResult(res,m_encodeChunk,tStamp,flags);
	    if (res > 0) {
		tStamp += m_tStamp;
		m_tStamp = 0;
	    }
	    if (!len)
		len = l;
	    else if (len != invalidStamp() && l != invalidStamp())
		len += l;
	}
	if (!remaining)
	    m_buffer.clear();
	else
	    m_buffer.assign((void*)ptr,remaining);
    }
    else {
#ifndef NO_ISAC_PLC
	if (flags & DataMissed) {
	    // guess how many frames were lost
	    int lost = (tStamp - timeStamp()) / m_encodeChunk;
	    if (lost <= 0)
		lost = 1;
	    else if (lost > ISAC_MAX_PLC)
		lost = ISAC_MAX_PLC;
#ifdef ISAC_FIXED
	    res = WebRtcIsacfix_DecodePlc(m_isac,out,lost);
#else
	    res = WebRtcIsac_DecodePlc(m_isac,out,lost);
#endif
	    DDebug(&__plugin,DebugNote,"Loss Concealment %d samples [%p]",res,this);
	    if (res > 0) {
		flags &= ~DataMissed;
		unsigned long ts = tStamp;
		if (data.length())
		    ts -= res;
		processCodecResult(res*2,0,ts,flags);
	    }
	}
#endif
	if (data.length()) {
	    // NOTE: We must workaround the following issues in WebRtcIsacfix_Decode:
	    // - It doesn't honor the 'const' qualifier of the input buffer on
	    //   little endian machines, it changes it!
	    //   Copy data to out buffer to avoid altering source data for another data consumer
	    // - It makes read/write access past buffer end for odd buffer length
	    const DataBlock* inDataBlock = &data;
	    if (0 != (data.length() & 0x01)) {
		m_buffer.assign(0,data.length() + 1);
		::memcpy(m_buffer.data(),data.data(),data.length());
		inDataBlock = &m_buffer;
	    }
#ifndef BIGENDIAN
	    else {
		m_buffer = data;
		inDataBlock = &m_buffer;
	    }
#endif
	    WebRtc_Word16 speechType = 0;
#ifdef ISAC_FIXED
	    res = WebRtcIsacfix_Decode(m_isac,(const WebRtc_UWord16*)
		    inDataBlock->data(),data.length(),out,&speechType);
#else
	    res = WebRtcIsac_Decode(m_isac,(const WebRtc_UWord16*)
		    inDataBlock->data(),data.length(),out,&speechType);
#endif
	    // Decode returns the number of decoded samples
	    if (res > 0)
		res *= 2;
	    len = processCodecResult(res,data.length(),tStamp,flags);
	}
    }
    deref();
    return len;
}

// Check error after encode/decode
// Forward data if result is greater then 0 and return the number of bytes forwarded
// Update last error. Output a debug message if error changed
unsigned long iSACCodec::processCodecResult(WebRtc_Word16 result, unsigned int inBytes,
    unsigned long tStamp, unsigned long flags)
{
    XDebug(&__plugin,DebugAll,"%scoded %u --> %d tStamp=%lu [%p]",
	m_encoding ? "En" : "De",inBytes,result,tStamp,this);
    if (result >= 0) {
	m_error = 0;
	if (!result)
	    return 0;
	m_outPackets++;
	m_outBytes += (unsigned long)result;
	DataBlock tmp(m_outData.data(),result,false);
	DDebug(&__plugin,DebugAll,"%scoder forwarding %u tStamp=%lu [%p]",
	    m_encoding ? "En" : "De",tmp.length(),tStamp,this);
	unsigned long len = getTransSource()->Forward(tmp,tStamp,flags);
	tmp.clear(false);
	return len;
    }
    m_failedBytes += inBytes;
    WebRtc_Word16 err = isacGetError();
    if (m_error != err) {
	m_error = err;
	Debug(&__plugin,DebugNote,"%scoder failed %u bytes error=%d [%p]",
	    m_encoding ? "En" : "De",inBytes,m_error,this);
    }
    return 0;
}

// Initialize the ISAC structure. Return false on failure
bool iSACCodec::isacInit()
{
    if (m_isac)
	return true;
    // Create the isac structure
    WebRtc_Word16 res = -1;
    int sampleRate = getFormat().sampleRate();
#ifdef ISAC_FIXED
    res = WebRtcIsacfix_Create(&m_isac);
#else
    res = WebRtcIsac_Create(&m_isac);
#endif
    if (res) {
	Debug(&__plugin,DebugWarn,"iSACCodec failed to allocate ISAC data [%p]",this);
	m_isac = 0;
	return false;
    }
    // Init the codec
    if (m_encoding) {
#ifdef ISAC_FIXED
	res = WebRtcIsacfix_EncoderInit(m_isac,m_mode);
#else
	res = WebRtcIsac_EncoderInit(m_isac,m_mode);
	WebRtcIsac_SetEncSampRate(m_isac,sampleRate == 16000 ? kIsacWideband : kIsacSuperWideband);
#endif

	if (sampleRate == 16000) {
	    m_outData.assign(0,400);
	    m_encodeChunk = 320;
	}
#ifndef ISAC_FIXED
	else if (sampleRate == 32000) {
	    m_outData.assign(0,800);
	    m_encodeChunk = 640;
	}
#endif
	else {
	    Debug(&__plugin,DebugWarn,"Bad iSAC sample Rate %d",sampleRate);
	    return false;
	}
    }
    else {
#ifdef ISAC_FIXED
	res = WebRtcIsacfix_DecoderInit(m_isac);
#else
	res = WebRtcIsac_DecoderInit(m_isac);
	WebRtcIsac_SetDecSampRate(m_isac,sampleRate == 16000 ? kIsacWideband : kIsacSuperWideband);
#endif
	m_encodeChunk = (sampleRate == 16000) ? 480 : 960;
	// Decode may return 480 or 960 samples except when doing PLC
	m_outData.assign(0,m_encodeChunk * 2 * ISAC_MAX_PLC);
    }
    if (res == 0) {
	if (m_encoding) {
	    // Set frame size if instructed
	    WebRtc_Word16 fs = ISAC_FRAME_SIZE_MS;
	    if (fs) {
		WebRtc_Word16 err = 0;
		// Isac fixed point implementation rate:
		// Channel adaptive: 0 to use default
		// Channel independent use default value (32000)
		WebRtc_Word16 rateBps = sampleRate;
		if (m_mode == ISAC_CODING_INDEPENDENT) {
#ifdef ISAC_FIXED
		    err = WebRtcIsacfix_Control(m_isac,rateBps,fs);
#else
		    err = WebRtcIsac_Control(m_isac,rateBps,fs);
#endif
		}
		else {
		    // Enforce frame size: 1: fix, 0: let the codec change it
		    WebRtc_Word16 efs = 1;
#ifdef ISAC_FIXED
		    err = WebRtcIsacfix_ControlBwe(m_isac,rateBps,fs,efs);
#else
		    err = WebRtcIsac_ControlBwe(m_isac,rateBps,fs,efs);
#endif
		}
		if (err == 0)
		    XDebug(&__plugin,DebugAll,"Encoder set framesize=%dms [%p]",fs,this);
		else
		    Debug(&__plugin,DebugNote,
		        "Encoder failed to set framesize=%dms error=%d [%p]",
			fs,isacGetError(),this);
	    }
	}
	DDebug(&__plugin,DebugAll,"iSACCodec initialized [%p]",this);
	return true;
    }
    m_error = isacGetError();
    Debug(&__plugin,DebugWarn,"iSACCodec failed to initialize error=%d [%p]",
	m_error,this);
    isacFree();
    return false;
}

// Release the ISAC structure
void iSACCodec::isacFree()
{
    if (!m_isac)
	return;
    XDebug(&__plugin,DebugAll,"iSACCodec releasing ISAC [%p]",this);
#ifdef ISAC_FIXED
    WebRtcIsacfix_Free(m_isac);
#else
    WebRtcIsac_Free(m_isac);
#endif
    m_isac = 0;
}


/*
 * iSACFactory
 */
iSACFactory::iSACFactory()
    : TranslatorFactory("isac"),
    m_caps(s_caps)
{
}

DataTranslator* iSACFactory::create(const DataFormat& sFormat, const DataFormat& dFormat)
{
    iSACCodec* codec = 0;
    if (sFormat == YSTRING("slin/16000")) {
	if (dFormat == YSTRING("isac/16000"))
	    codec = new iSACCodec(sFormat,dFormat,true);
    }
    else if (dFormat == YSTRING("slin/16000")) {
	if (sFormat == YSTRING("isac/16000"))
	    codec = new iSACCodec(sFormat,dFormat,false);
    }
#ifndef ISAC_FIXED
    if (sFormat == YSTRING("slin/32000")) {
	if (dFormat == YSTRING("isac/32000"))
	    codec = new iSACCodec(sFormat,dFormat,true);
    }
    else if (dFormat == YSTRING("slin/32000")) {
	if (sFormat == YSTRING("isac/32000"))
	    codec = new iSACCodec(sFormat,dFormat,false);
    }
#endif
    if (codec && !codec->valid())
	TelEngine::destruct(codec);
    return codec;
}


/*
 * iSACModule
 */
iSACModule::iSACModule()
    : Module("isaccodec","misc"),
    m_count(0), m_factory(0)
{
    char ver[65] = {0};
    char splVer[65] = {0};
    const char* type = 0;
    WebRtcSpl_get_version(splVer,64);
#ifdef ISAC_FIXED
    WebRtcIsacfix_version(ver);
    type = "fixed point";
#else
    WebRtcIsac_version(ver);
    type = "floating point";
#endif
    Output("Loaded module iSAC %s - based on WebRTC iSAC library version %s (SPL version %s)",
	type,ver,splVer);
    const FormatInfo* f = FormatRepository::addFormat("isac/16000",0,
	ISAC_FRAME_SIZE_MS * 1000,"audio",16000);
    s_caps[0].src = s_caps[1].dest = f;
    s_caps[0].dest = s_caps[1].src = FormatRepository::getFormat("slin/16000");
    // FIXME: put proper conversion costs
    s_caps[0].cost = s_caps[1].cost = 10;
#ifndef ISAC_FIXED
    const FormatInfo* f32 = FormatRepository::addFormat("isac/32000",0,
	ISAC_FRAME_SIZE_MS * 1000,"audio",32000);
    s_caps[2].src = s_caps[3].dest = f32;
    s_caps[2].dest = s_caps[3].src = FormatRepository::getFormat("slin/32000");
    // FIXME: put proper conversion costs
    s_caps[2].cost = s_caps[3].cost = 10;
#endif
    m_factory = new iSACFactory;
}

iSACModule::~iSACModule()
{
    Output("Unloading module iSAC with %d codecs still in use",m_count);
    TelEngine::destruct(m_factory);
}

void iSACModule::initialize()
{
    static bool s_first = true;
    Output("Initializing module iSAC");
    if (s_first) {
	installRelay(Level);
	installRelay(Status);
	installRelay(Command);
	s_first = false;
    }
}

void iSACModule::statusParams(String& str)
{
    str << "codecs=" << m_count;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
