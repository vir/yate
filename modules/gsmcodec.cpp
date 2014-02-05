/**
 * gsmcodec.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * GSM 6.10 codec using libgsm
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

extern "C" {
#include <gsm.h>

typedef gsm_signal gsm_block[160];
}

using namespace TelEngine;
namespace { // anonymous

static TranslatorCaps caps[] = {
    { 0, 0 },
    { 0, 0 },
    { 0, 0 }
};

int count = 0;

class GsmPlugin : public Plugin, public TranslatorFactory
{
public:
    GsmPlugin();
    ~GsmPlugin();
    virtual void initialize() { }
    virtual bool isBusy() const;
    virtual DataTranslator* create(const DataFormat& sFormat, const DataFormat& dFormat);
    virtual const TranslatorCaps* getCapabilities() const;
};

class GsmCodec : public DataTranslator
{
public:
    GsmCodec(const char* sFormat, const char* dFormat, bool encoding);
    ~GsmCodec();
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags);
private:
    bool m_encoding;
    gsm m_gsm;
    DataBlock m_data;
    DataBlock m_outdata;
};

GsmCodec::GsmCodec(const char* sFormat, const char* dFormat, bool encoding)
    : DataTranslator(sFormat,dFormat), m_encoding(encoding), m_gsm(0)
{
    Debug(DebugAll,"GsmCodec::GsmCodec(\"%s\",\"%s\",%scoding) [%p]",
	sFormat,dFormat, m_encoding ? "en" : "de",this);
    count++;
    m_gsm = ::gsm_create();
}

GsmCodec::~GsmCodec()
{
    Debug(DebugAll,"GsmCodec::~GsmCodec() [%p]",this);
    count--;
    if (m_gsm) {
	gsm temp = m_gsm;
	m_gsm = 0;
	::gsm_destroy(temp);
    }
}

unsigned long GsmCodec::Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
{
    if (!(m_gsm && getTransSource()))
	return 0;
    if (data.null() && (flags & DataSilent))
	return getTransSource()->Forward(data,tStamp,flags);
    ref();
    if (m_encoding && (tStamp != invalidStamp()) && !m_data.null())
	tStamp -= (m_data.length() / 2);
    m_data += data;
    int frames,consumed;
    if (m_encoding) {
	frames = m_data.length() / sizeof(gsm_block);
	consumed = frames * sizeof(gsm_block);
	if (frames) {
	    m_outdata.resize(frames * sizeof(gsm_frame));
	    for (int i=0; i<frames; i++)
		::gsm_encode(m_gsm,
		    (gsm_signal*)(((gsm_block *)m_data.data())+i),
		    (gsm_byte*)(((gsm_frame *)m_outdata.data())+i));
	}
	if (!tStamp)
	    tStamp = timeStamp() + (consumed / 2);
    }
    else {
	frames = m_data.length() / sizeof(gsm_frame);
	consumed = frames * sizeof(gsm_frame);
	if (frames) {
	    m_outdata.resize(frames * sizeof(gsm_block));
	    for (int i=0; i<frames; i++)
		::gsm_decode(m_gsm,
		    (gsm_byte*)(((gsm_frame *)m_data.data())+i),
		    (gsm_signal*)(((gsm_block *)m_outdata.data())+i));
	}
	if (!tStamp)
	    tStamp = timeStamp() + (frames*sizeof(gsm_block) / 2);
    }
    XDebug("GsmCodec",DebugAll,"%scoding %d frames of %d input bytes (consumed %d) in %d output bytes",
	m_encoding ? "en" : "de",frames,m_data.length(),consumed,m_outdata.length());
    unsigned long len = 0;
    if (frames) {
	m_data.cut(-consumed);
	len = getTransSource()->Forward(m_outdata,tStamp,flags);
    }
    deref();
    return len;
}

GsmPlugin::GsmPlugin()
    : Plugin("gsmcodec"), TranslatorFactory("gsm")
{
    Output("Loaded module GSM - based on libgsm-%d.%d.%d",GSM_MAJOR,GSM_MINOR,GSM_PATCHLEVEL);
    const FormatInfo* f = FormatRepository::addFormat("gsm",33,20000);
    caps[0].src = caps[1].dest = f;
    caps[0].dest = caps[1].src = FormatRepository::getFormat("slin");
    // FIXME: put proper conversion costs
    caps[0].cost = caps[1].cost = 5;
}

GsmPlugin::~GsmPlugin()
{
    Output("Unloading module GSM with %d codecs still in use",count);
}

bool GsmPlugin::isBusy() const
{
    return (count != 0);
}

DataTranslator* GsmPlugin::create(const DataFormat& sFormat, const DataFormat& dFormat)
{
    if (sFormat == "slin" && dFormat == "gsm")
	return new GsmCodec(sFormat,dFormat,true);
    else if (sFormat == "gsm" && dFormat == "slin")
	return new GsmCodec(sFormat,dFormat,false);
    else return 0;
}

const TranslatorCaps* GsmPlugin::getCapabilities() const
{
    return caps;
}

INIT_PLUGIN(GsmPlugin);

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow)
	return !__plugin.isBusy();
    return true;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
