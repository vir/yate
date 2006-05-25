/**
 * gsmcodec.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * GSM 6.10 codec using libgsm
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004, 2005 Null Team
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

extern "C" {
#include <gsm.h>

typedef gsm_signal gsm_block[160];
}

using namespace TelEngine;

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
    virtual void Consume(const DataBlock& data, unsigned long tStamp);
private:
    bool m_encoding;
    gsm m_gsm;
    DataBlock m_data;
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

void GsmCodec::Consume(const DataBlock& data, unsigned long tStamp)
{
    if (!(m_gsm && getTransSource()))
	return;
    ref();
    m_data += data;
    DataBlock outdata;
    int frames,consumed;
    if (m_encoding) {
	frames = m_data.length() / sizeof(gsm_block);
	consumed = frames * sizeof(gsm_block);
	if (frames) {
	    outdata.assign(0,frames*sizeof(gsm_frame));
	    for (int i=0; i<frames; i++)
		::gsm_encode(m_gsm,
		    (gsm_signal*)(((gsm_block *)m_data.data())+i),
		    (gsm_byte*)(((gsm_frame *)outdata.data())+i));
	}
	if (!tStamp)
	    tStamp = timeStamp() + (consumed / 2);
    }
    else {
	frames = m_data.length() / sizeof(gsm_frame);
	consumed = frames * sizeof(gsm_frame);
	if (frames) {
	    outdata.assign(0,frames*sizeof(gsm_block));
	    for (int i=0; i<frames; i++)
		::gsm_decode(m_gsm,
		    (gsm_byte*)(((gsm_frame *)m_data.data())+i),
		    (gsm_signal*)(((gsm_block *)outdata.data())+i));
	}
	if (!tStamp)
	    tStamp = timeStamp() + (frames*sizeof(gsm_block) / 2);
    }
    XDebug("GsmCodec",DebugAll,"%scoding %d frames of %d input bytes (consumed %d) in %d output bytes",
	m_encoding ? "en" : "de",frames,m_data.length(),consumed,outdata.length());
    if (frames) {
	m_data.cut(-consumed);
	getTransSource()->Forward(outdata,tStamp);
    }
    deref();
}

GsmPlugin::GsmPlugin()
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

/* vi: set ts=8 sw=4 sts=4 noet: */
