/**
 * ilbccodec.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * iLBC codec using iLBC library.
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 *
 * iLBC codec has been created based on the code sent by Faizan Naqvi (Tili)
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
#include <stdlib.h>

extern "C" {
#include "../libs/ilbc/iLBC_encode.h"
#include "../libs/ilbc/iLBC_decode.h"
}

using namespace TelEngine;
namespace { // anonymous

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

static Mutex s_cmutex(false,"iLBCCodec");
static int s_count = 0;

class iLBCFactory : public TranslatorFactory
{
public:
    inline iLBCFactory(const TranslatorCaps* caps)
	: TranslatorFactory("ilbc"), m_caps(caps)
	{ }
    virtual const TranslatorCaps* getCapabilities() const
	{ return m_caps; }
    virtual DataTranslator* create(const DataFormat& sFormat, const DataFormat& dFormat);
private:
    const TranslatorCaps* m_caps;
};

class iLBCPlugin : public Plugin
{
public:
    iLBCPlugin();
    ~iLBCPlugin();
    virtual void initialize() { }
    virtual bool isBusy() const;
private:
    iLBCFactory* m_ilbc20;
    iLBCFactory* m_ilbc30;
};

class iLBCCodec : public DataTranslator
{
public:
    iLBCCodec(const char* sFormat, const char* dFormat, bool encoding, int msec);
    ~iLBCCodec();
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags);
private:
    bool m_encoding;
    DataBlock m_data;
    DataBlock m_outdata;
    iLBC_Enc_Inst_t m_enc;
    iLBC_Dec_Inst_t m_dec;
    int m_mode;
};

iLBCCodec::iLBCCodec(const char* sFormat, const char* dFormat, bool encoding, int msec)
    : DataTranslator(sFormat,dFormat), m_encoding(encoding), m_mode(msec)
{
    Debug(DebugAll,"iLBCCodec::iLBCCodec(\"%s\",\"%s\",%scoding,%d) [%p]",
	sFormat,dFormat, m_encoding ? "en" : "de",msec,this);

    if (encoding) {
	memset(&m_enc,0,sizeof(m_enc));
	initEncode(&m_enc,m_mode);
    }
    else {
	memset(&m_dec,0,sizeof(m_dec));
	initDecode(&m_dec,m_mode,0);
    }
    s_cmutex.lock();
    s_count++;
    s_cmutex.unlock();
}

iLBCCodec::~iLBCCodec()
{
    Debug(DebugAll,"iLBCCodec::~ILBCCodec() [%p]",this);
    s_cmutex.lock();
    s_count--;
    s_cmutex.unlock();
}

unsigned long iLBCCodec::Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
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
	no_bytes=NO_OF_BYTES_20MS;
    } else {
	block = BLOCKL_30MS;
	no_bytes=NO_OF_BYTES_30MS;
    }
    if (m_encoding && (tStamp != invalidStamp()) && !m_data.null())
	tStamp -= (m_data.length() / 2);
    m_data += data;
    int frames,consumed;
    if (m_encoding) {
	frames = m_data.length() / (2 * block);
	consumed = frames * 2 * block;
	if (frames) {
	    m_outdata.resize(frames * no_bytes);
	    unsigned char* d = (unsigned char*)m_outdata.data();
	    const short* s = (const short*)m_data.data();
	    for (int i=0; i<frames; i++) {
		// convert one frame data from 16 bit signed linear to float
		float buffer[BLOCKL_MAX];
		for (int j=0; j<block; j++)
		    buffer[j] = *s++;
		// and now do the actual encoding directly to outdata
		::iLBC_encode(d,buffer,&m_enc);
		d += no_bytes;
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
	    short* d = (short*)m_outdata.data();
	    unsigned char* s = (unsigned char*)m_data.data();
	    for (int i=0; i<frames; i++) {
		// decode to a float values buffer
		float buffer[BLOCKL_MAX];
		if (flags & DataMissed) {
		    // ask the codec to perform Packet Loss Concealement
		    ::iLBC_decode(buffer,0,&m_dec,0);
		    flags &= ~DataMissed;
		    if (tStamp)
			tStamp -= block;
		}
		else {
		    ::iLBC_decode(buffer,s,&m_dec,1);
		    s += no_bytes;
		}
		// convert the buffer back to 16 bit integer
		for (int j=0; j<block; j++)
		    *d++ = (short)(buffer[j]);
	    }
	}
    }
    if (!tStamp)
	tStamp = timeStamp() + (frames * block);

    XDebug("iLBCCodec",DebugAll,"%scoding %d frames of %d input bytes (consumed %d) in %d output bytes",
	m_encoding ? "en" : "de",frames,m_data.length(),consumed,m_outdata.length());
    unsigned long len = 0;
    if (frames) {
	m_data.cut(-consumed);
	len = getTransSource()->Forward(m_outdata,tStamp,flags);
    }
    deref();
    return len;
}

iLBCPlugin::iLBCPlugin()
    : Plugin("ilbccodec"),
      m_ilbc20(0), m_ilbc30(0)
{
    Output("Loaded module iLBC - based on iLBC reference library");
    const FormatInfo* f = FormatRepository::addFormat("ilbc20",NO_OF_BYTES_20MS,20000);
    s_caps20[0].src = s_caps20[1].dest = f;
    s_caps20[0].dest = s_caps20[1].src = FormatRepository::getFormat("slin");
    // FIXME: put proper conversion costs
    s_caps20[0].cost = s_caps20[1].cost = 10;
    f = FormatRepository::addFormat("ilbc30",NO_OF_BYTES_30MS,30000);
    s_caps30[0].src = s_caps30[1].dest = f;
    s_caps30[0].dest = s_caps30[1].src = FormatRepository::getFormat("slin");
    // FIXME: put proper conversion costs
    s_caps30[0].cost = s_caps30[1].cost = 9;
    m_ilbc20 = new iLBCFactory(s_caps20);
    m_ilbc30 = new iLBCFactory(s_caps30);
}

iLBCPlugin::~iLBCPlugin()
{
    Output("Unloading module iLBC with %d codecs still in use",s_count);
    TelEngine::destruct(m_ilbc20);
    TelEngine::destruct(m_ilbc30);
}

bool iLBCPlugin::isBusy() const
{
    return (s_count != 0);
}

DataTranslator* iLBCFactory::create(const DataFormat& sFormat, const DataFormat& dFormat)
{
    if (sFormat == "slin") {
	// encoding from slin
	if (dFormat == "ilbc20")
	    return new iLBCCodec(sFormat,dFormat,true,20);
	else if (dFormat == "ilbc30")
	    return new iLBCCodec(sFormat,dFormat,true,30);
    }
    else if (dFormat == "slin") {
	// decoding to slin
	if (sFormat == "ilbc20")
	    return new iLBCCodec(sFormat,dFormat,false,20);
	else if (sFormat == "ilbc30")
	    return new iLBCCodec(sFormat,dFormat,false,30);
    }
    return 0;
}

INIT_PLUGIN(iLBCPlugin);

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow)
	return !__plugin.isBusy();
    return true;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
