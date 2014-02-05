/**
 * speexcodec.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Speex codec module written by Olaf Conradi .
 * Updated by Mikael Magnusson, inspired by codec_speex from iaxclient
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 * Copyright (C) 2006 Mikael Magnusson
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

extern "C"  {
#include <speex/speex.h>
#ifdef _WINDOWS
/* For some reason the DLL does not export the mode variables */
#define speex_nb_mode (*speex_lib_get_mode(SPEEX_MODEID_NB))
#define speex_wb_mode (*speex_lib_get_mode(SPEEX_MODEID_WB))
#define speex_uwb_mode (*speex_lib_get_mode(SPEEX_MODEID_UWB))
#endif
}

using namespace TelEngine;

static TranslatorCaps caps[7];

static Mutex s_cmutex(false,"SpeexCodec");
static int s_count = 0;

class SpeexPlugin : public Plugin, public TranslatorFactory
{
public:
    SpeexPlugin();
    ~SpeexPlugin();
    virtual void initialize() { }
    virtual bool isBusy() const;
    virtual DataTranslator* create(const DataFormat& sFormat, const DataFormat& dFormat);
    virtual const TranslatorCaps* getCapabilities() const;
};

class SpeexCodec : public DataTranslator
{
public:
    SpeexCodec(const char* sFormat, const char* dFormat, bool encoding, int type);
    ~SpeexCodec();
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags);
private:
    bool m_encoding;
    DataBlock m_data;

    void *m_state;
    SpeexBits *m_bits;
    int m_frameSize;

    const FormatInfo *m_sFormatInfo;
    const FormatInfo *m_dFormatInfo;
    unsigned int m_bsamples;
    unsigned int m_bsize;
};

SpeexCodec::SpeexCodec(const char* sFormat, const char* dFormat, bool encoding, int type)
    : DataTranslator(sFormat,dFormat), m_encoding(encoding),
      m_state(NULL), m_bits(NULL), m_frameSize(0)
{
    Debug("speexcodec", DebugAll,
	    "SpeexCodec::SpeexCodec(\"%s\",\"%s\",%scoding,%d) [%p]",
	    sFormat,dFormat, m_encoding ? "en" : "de",type,this);

    m_sFormatInfo = FormatRepository::getFormat(sFormat);
    m_dFormatInfo = FormatRepository::getFormat(dFormat);

    m_bits = new SpeexBits;
    speex_bits_init(m_bits);

    if (encoding) {
	int mode = 6;
	switch (type) {
	    case SPEEX_MODEID_UWB:
		m_state = speex_encoder_init(&speex_uwb_mode);
		break;
	    case SPEEX_MODEID_WB:
		m_state = speex_encoder_init(&speex_wb_mode);
		break;
	    case SPEEX_MODEID_NB:
	    default:
		mode = 3;
		m_state = speex_encoder_init(&speex_nb_mode);
		break;
	}

	if (m_state) {
	    int srate = m_dFormatInfo->sampleRate;
	    int samples = 0;
	    int bitrate = 0;
	    speex_encoder_ctl(m_state, SPEEX_SET_MODE, &mode);
	    speex_encoder_ctl(m_state, SPEEX_GET_BITRATE, &bitrate);
	    speex_encoder_ctl(m_state, SPEEX_GET_SAMPLING_RATE, &srate);
	    speex_encoder_ctl(m_state, SPEEX_GET_FRAME_SIZE, &samples);
	    // compute frame size, round up to bytes
	    if (srate)
		m_frameSize = ((bitrate * samples / srate) + 7) / 8;
	    DDebug(DebugInfo,"Speex encoder frame size=%d [%p]",m_frameSize,this);
	}

	// Number of samples per frame in this Speex mode.
	m_bsamples = m_dFormatInfo->sampleRate *
	    (long)m_dFormatInfo->frameTime / 1000000;
    }
    else {
	switch (type) {
	    case SPEEX_MODEID_UWB:
		m_state = speex_decoder_init(&speex_uwb_mode);
		break;
	    case SPEEX_MODEID_WB:
		m_state = speex_decoder_init(&speex_wb_mode);
		break;
	    case SPEEX_MODEID_NB:
	    default:
		m_state = speex_decoder_init(&speex_nb_mode);
		break;
	}

	// Number of samples per frame in this Speex mode.
	m_bsamples = m_sFormatInfo->sampleRate *
	    (long)m_sFormatInfo->frameTime / 1000000;
    }

    // Size of one slin block for one frame of Speex data.
    m_bsize = m_bsamples * sizeof(short);

    s_cmutex.lock();
    s_count++;
    s_cmutex.unlock();
}

SpeexCodec::~SpeexCodec()
{
    Debug(DebugAll,"SpeexCodec::~SpeexCodec() [%p]", this);

    if (m_state) {
	if (m_encoding)
	    speex_encoder_destroy(m_state);
	else
	    speex_decoder_destroy(m_state);
	m_state = NULL;
    }

    if (m_bits) {
	speex_bits_destroy(m_bits);
	delete m_bits;
	m_bits = NULL;
    }

    s_cmutex.lock();
    s_count--;
    s_cmutex.unlock();
}

unsigned long SpeexCodec::Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
{
    if (!(m_state && m_bits && getTransSource()))
	return 0;
    if (!ref())
	return 0;

    if (m_encoding && (tStamp != invalidStamp()) && !m_data.null())
	tStamp -= (m_data.length() / 2);
    m_data += data;
    DataBlock outdata;

    unsigned int frames = 0;
    unsigned int consumed = 0;
    int frame_size = 0;
    int ret = 0;

    speex_decoder_ctl(m_state, SPEEX_GET_FRAME_SIZE, &frame_size);
//     frame_size = 35;

    if (m_encoding) {
	frames = m_data.length() / m_bsize;
	consumed = frames * m_bsize;

	if (frames) {
	    outdata.assign(0, frames * m_frameSize);
	    char* d = (char*)outdata.data();
	    char* s = (char*)m_data.data();

	    for (unsigned int i = 0; i < frames; i++) {
		speex_bits_reset(m_bits);
		speex_encode_int(m_state, (short*)s, m_bits);
		d += speex_bits_write(m_bits, d, m_frameSize);
		s += m_bsize;
	    }
	}
    }
    else {
	char* s = (char*)data.data();
	DataBlock tmp;

	tmp.assign(0, m_bsize);
	speex_bits_read_from(m_bits, s, data.length());
	consumed = data.length();

	short* d = (short*)tmp.data();

	while(speex_bits_remaining(m_bits)) {
	    ret = speex_decode_int(m_state, m_bits, d);
	    frames++;

	    if (ret == 0) {
		outdata += tmp;
		if(1) {
		    int bits_left = speex_bits_remaining(m_bits) % 8;
		    if(bits_left)
			speex_bits_advance(m_bits, bits_left);
		}
	    } else if (ret == -1) {
		int bits_left = speex_bits_remaining(m_bits) % 8;
		if(bits_left >= 5)
		    speex_bits_advance(m_bits, bits_left);
		else
		    break;
	    }
	}
    }

    if (!tStamp)
	tStamp = timeStamp() + frames * m_bsamples;

    XDebug("SpeexCodec", DebugAll,
	    "%scoding %d frames of %d input bytes (consumed %d) in %d output bytes, frame size %d, time %lu, ret %d",
	   m_encoding ? "en" : "de", frames, m_data.length(), consumed, outdata.length(), frame_size, tStamp, ret);

    unsigned long len = 0;
    if (frames) {
	m_data.cut(-(int)consumed);
	len = getTransSource()->Forward(outdata, tStamp, flags);
    }

    deref();
    return len;
}

SpeexPlugin::SpeexPlugin()
    : Plugin("speexcodec"), TranslatorFactory("speex")
{
    int major, minor, micro;
    speex_lib_ctl(SPEEX_LIB_GET_MAJOR_VERSION, &major);
    speex_lib_ctl(SPEEX_LIB_GET_MINOR_VERSION, &minor);
    speex_lib_ctl(SPEEX_LIB_GET_MICRO_VERSION, &micro);
    Output("Loaded module Speex - based on libspeex-%d.%d.%d",
	    major, minor, micro);

    const FormatInfo* f = FormatRepository::addFormat("speex", 0, 20000);
    caps[0].src = caps[1].dest = f;
    caps[0].dest = caps[1].src = FormatRepository::getFormat("slin");
    f = FormatRepository::addFormat("speex/16000", 0, 20000, "audio", 16000);
    caps[2].src = caps[3].dest = f;
    caps[2].dest = caps[3].src = FormatRepository::getFormat("slin/16000");
    f = FormatRepository::addFormat("speex/32000", 0, 20000, "audio", 32000);
    caps[4].src = caps[5].dest = f;
    caps[4].dest = caps[5].src = FormatRepository::getFormat("slin/32000");

    caps[6].src = caps[6].dest = 0;
}

SpeexPlugin::~SpeexPlugin()
{
    Output("Unloading module Speex with %d codecs still in use", s_count);
}

bool SpeexPlugin::isBusy() const
{
    return (s_count != 0);
}

DataTranslator* SpeexPlugin::create(const DataFormat& sFormat, const DataFormat& dFormat)
{
    if (sFormat == "slin" && dFormat == "speex")
	return new SpeexCodec(sFormat, dFormat, true, SPEEX_MODEID_NB);
    else if (sFormat == "slin/16000" && dFormat == "speex/16000")
	return new SpeexCodec(sFormat, dFormat, true, SPEEX_MODEID_WB);
    else if (sFormat == "slin/32000" && dFormat == "speex/32000")
	return new SpeexCodec(sFormat, dFormat, true, SPEEX_MODEID_UWB);
    else if (dFormat == "slin" && sFormat == "speex")
	return new SpeexCodec(sFormat, dFormat, false, SPEEX_MODEID_NB);
    else if (dFormat == "slin/16000" && sFormat == "speex/16000")
	return new SpeexCodec(sFormat, dFormat, false, SPEEX_MODEID_WB);
    else if (dFormat == "slin/32000" && sFormat == "speex/32000")
	return new SpeexCodec(sFormat, dFormat, false, SPEEX_MODEID_UWB);
    return 0;
}

const TranslatorCaps* SpeexPlugin::getCapabilities() const
{
    return caps;
}

INIT_PLUGIN(SpeexPlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
