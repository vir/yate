/**
 * ilbcwebrtc.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * iLBC codec using iLBC library based on WebRTC project.
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2012-2014 Null Team
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
#include "defines.h"
#include "ilbc.h"
}

using namespace TelEngine;
namespace { // anonymous

class iLBCwrCodec : public DataTranslator
{
public:
    iLBCwrCodec(const char* sFormat, const char* dFormat, bool encoding, int msec);
    ~iLBCwrCodec();
    virtual bool valid() const
	{ return m_enc || m_dec; }
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp,
	unsigned long flags);
private:
    bool m_encoding;                     // Encoder/decoder flag
    iLBC_encinst_t* m_enc;               // Encoder instance
    iLBC_decinst_t* m_dec;               // Decoder instance
    int m_mode;                          // Codec mode, 20/30 msec

    DataBlock m_data;                    // Incomplete input data
    DataBlock m_outdata;                 // Codec output
};

class iLBCwrFactory : public TranslatorFactory
{
public:
    iLBCwrFactory(const TranslatorCaps* caps);
    virtual const TranslatorCaps* getCapabilities() const
	{ return m_caps; }
    virtual DataTranslator* create(const DataFormat& sFormat, const DataFormat& dFormat);
private:
    const TranslatorCaps* m_caps;
};

class iLBCwrModule : public Module
{
public:
    iLBCwrModule();
    ~iLBCwrModule();
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
    iLBCwrFactory* m_ilbc20;             // Factory used to create 20ms codecs
    iLBCwrFactory* m_ilbc30;             // Factory used to create 30ms codecs
};


INIT_PLUGIN(iLBCwrModule);

static TranslatorCaps s_caps20[] = {
    { 0, 0 },
    { 0, 0 },
    { 0, 0 }
};

static TranslatorCaps s_caps30[] = {
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
 * iLBCwrCodec
 */
iLBCwrCodec::iLBCwrCodec(const char* sFormat, const char* dFormat, bool encoding, int msec)
    : DataTranslator(sFormat,dFormat), m_encoding(encoding),
    m_enc(0), m_dec(0), m_mode(msec)
{
    Debug(&__plugin,DebugAll,"iLBCwrCodec(\"%s\",\"%s\",%scoding,%d) [%p]",
	sFormat,dFormat,m_encoding ? "en" : "de",msec,this);
    __plugin.incCount();
    if (encoding) {
	::WebRtcIlbcfix_EncoderCreate(&m_enc);
	::WebRtcIlbcfix_EncoderInit(m_enc,msec);
    }
    else {
	::WebRtcIlbcfix_DecoderCreate(&m_dec);
	::WebRtcIlbcfix_DecoderInit(m_dec,msec);
    }
}

iLBCwrCodec::~iLBCwrCodec()
{
    if (m_enc)
	::WebRtcIlbcfix_EncoderFree(m_enc);
    if (m_dec)
	::WebRtcIlbcfix_DecoderFree(m_dec);
    Debug(&__plugin,DebugAll,
	"iLBCwrCodec(%scoding) destroyed [%p]",m_encoding ? "en" : "de",this);
    __plugin.decCount();
}

unsigned long iLBCwrCodec::Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
{
    if (!getTransSource())
	return 0;
    if (data.null() && (flags & DataSilent))
	return getTransSource()->Forward(data,tStamp,flags);
    ref();
    // block size in samples per frame, no_bytes frame length in bytes
    int block,no_bytes;
    if (m_mode == 20)
    {
	block = BLOCKL_20MS;
	no_bytes = NO_OF_BYTES_20MS;
    } else {
	block = BLOCKL_30MS;
	no_bytes = NO_OF_BYTES_30MS;
    }
    if (m_encoding && (tStamp != invalidStamp()) && !m_data.null())
	tStamp -= (m_data.length() / 2);
    m_data += data;
    // WebRTC will modify in-place the input data block!!!
    // Since we cut off m_data the consumed amount we need not worry
    // Also since iLBC frames are even length there is no fault on word access
    int no_words = no_bytes / sizeof(WebRtc_Word16);
    int frames,consumed;
    if (m_encoding) {
	frames = m_data.length() / (2 * block);
	consumed = frames * 2 * block;
	if (frames) {
	    m_outdata.resize(frames * no_bytes);
	    WebRtc_Word16* d = (WebRtc_Word16*)m_outdata.data();
	    WebRtc_Word16* s = (WebRtc_Word16*)m_data.data();
	    for (int i=0; i<frames; i++) {
		// do the actual encoding directly to outdata
		::WebRtcIlbcfix_Encode(m_enc,s,block,d);
		s += block;
		d += no_words;
	    }
	}
    }
    else {
	frames = m_data.length() / no_bytes;
	consumed = frames * no_bytes;
	if (flags & DataMissed)
	    frames++;
	if (frames) {
	    m_outdata.resize(frames * 2 * block);
	    WebRtc_Word16* d = (WebRtc_Word16*)m_outdata.data();
	    WebRtc_Word16* s = (WebRtc_Word16*)m_data.data();
	    for (int i=0; i<frames; i++) {
		if (flags & DataMissed) {
		    // ask the codec to perform Packet Loss Concealement
		    ::WebRtcIlbcfix_DecodePlc(m_dec,d,1);
		    flags &= ~DataMissed;
		    if (tStamp)
			tStamp -= block;
		}
		else {
		    WebRtc_Word16 speechType;
		    ::WebRtcIlbcfix_Decode(m_dec,s,no_bytes,d,&speechType);
		    s += no_words;
		}
		d += block;
	    }
	}
    }
    if (!tStamp)
	tStamp = timeStamp() + (frames * block);

    XDebug("iLBCwrCodec",DebugAll,"%scoding %d frames of %d input bytes (consumed %d) in %d output bytes",
	m_encoding ? "en" : "de",frames,m_data.length(),consumed,m_outdata.length());
    unsigned long len = 0;
    if (frames) {
	m_data.cut(-consumed);
	len = getTransSource()->Forward(m_outdata,tStamp,flags);
    }
    deref();
    return len;
}


/*
 * iLBCwrFactory
 */
iLBCwrFactory::iLBCwrFactory(const TranslatorCaps* caps)
    : TranslatorFactory("ilbc"),
    m_caps(caps)
{
}

DataTranslator* iLBCwrFactory::create(const DataFormat& sFormat, const DataFormat& dFormat)
{
    if (sFormat == "slin") {
	// encoding from slin
	if (dFormat == "ilbc20")
	    return new iLBCwrCodec(sFormat,dFormat,true,20);
	else if (dFormat == "ilbc30")
	    return new iLBCwrCodec(sFormat,dFormat,true,30);
    }
    else if (dFormat == "slin") {
	// decoding to slin
	if (sFormat == "ilbc20")
	    return new iLBCwrCodec(sFormat,dFormat,false,20);
	else if (sFormat == "ilbc30")
	    return new iLBCwrCodec(sFormat,dFormat,false,30);
    }
    return 0;
}


/*
 * iLBCwrModule
 */
iLBCwrModule::iLBCwrModule()
    : Module("ilbcwebrtc","misc"),
    m_count(0), m_ilbc20(0), m_ilbc30(0)
{
    char ver[24] = {0};
    ::WebRtcIlbcfix_version(ver);
    Output("Loaded module iLBC - based on WebRTC iLBC library version %s",ver);

    const FormatInfo* f = FormatRepository::addFormat("ilbc20",NO_OF_BYTES_20MS,20000);
    s_caps20[0].src = s_caps20[1].dest = f;
    s_caps20[0].dest = s_caps20[1].src = FormatRepository::getFormat("slin");
    // FIXME: put proper conversion costs
    s_caps20[0].cost = s_caps20[1].cost = 6;
    f = FormatRepository::addFormat("ilbc30",NO_OF_BYTES_30MS,30000);
    s_caps30[0].src = s_caps30[1].dest = f;
    s_caps30[0].dest = s_caps30[1].src = FormatRepository::getFormat("slin");
    // FIXME: put proper conversion costs
    s_caps30[0].cost = s_caps30[1].cost = 6;
    m_ilbc20 = new iLBCwrFactory(s_caps20);
    m_ilbc30 = new iLBCwrFactory(s_caps30);
}

iLBCwrModule::~iLBCwrModule()
{
    Output("Unloading module iLBC webrtc with %d codecs still in use",m_count);
    TelEngine::destruct(m_ilbc20);
    TelEngine::destruct(m_ilbc30);
}

void iLBCwrModule::initialize()
{
    static bool s_first = true;
    Output("Initializing module iLBC webrtc");
    if (s_first) {
	installRelay(Level);
	installRelay(Status);
	installRelay(Command);
	s_first = false;
    }
}

void iLBCwrModule::statusParams(String& str)
{
    str << "codecs=" << m_count;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
