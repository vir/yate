/**
 * gsmcodec.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * GSM 6.10 codec using libgsm
 */

#include <telengine.h>
#include <telephony.h>

extern "C" {
#include <gsm.h>

typedef gsm_signal gsm_block[160];
}

using namespace TelEngine;

int count = 0;

class GsmPlugin : public Plugin, public TranslatorFactory
{
public:
    GsmPlugin();
    ~GsmPlugin();
    virtual void initialize() { }
    virtual DataTranslator *create(const String &sFormat, const String &dFormat);
};

class GsmCodec : public DataTranslator
{
public:
    GsmCodec(const char *sFormat, const char *dFormat, bool encoding);
    ~GsmCodec();
    virtual void Consume(const DataBlock &data);
private:
    bool m_encoding;
    gsm m_gsm;
    DataBlock m_data;
};

GsmCodec::GsmCodec(const char *sFormat, const char *dFormat, bool encoding)
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

void GsmCodec::Consume(const DataBlock &data)
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
    }
#ifdef DEBUG
    Debug("GsmCodec",DebugAll,"%scoding %d frames of %d input bytes (consumed %d) in %d output bytes",
	m_encoding ? "en" : "de",frames,m_data.length(),consumed,outdata.length());
#endif
    if (frames) {
	m_data.cut(-consumed);
	getTransSource()->Forward(outdata);
    }
    deref();
}

GsmPlugin::GsmPlugin()
{
    Output("Loaded module GSM - based on libgsm-%d.%d.%d",GSM_MAJOR,GSM_MINOR,GSM_PATCHLEVEL);
}

GsmPlugin::~GsmPlugin()
{
    Output("Unloading module GSM with %d codecs still in use",count);
}

DataTranslator *GsmPlugin::create(const String &sFormat, const String &dFormat)
{
    if (sFormat == "slin" && dFormat == "gsm")
	return new GsmCodec(sFormat,dFormat,true);
    else if (sFormat == "gsm" && dFormat == "slin")
	return new GsmCodec(sFormat,dFormat,false);
    else return 0;
}

INIT_PLUGIN(GsmPlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
