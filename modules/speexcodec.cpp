/**
 * speexcodec.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 * 
 * Speex codec module written by Olaf Conradi .
 * Updated by Mikael Magnusson, inspired by codec_speex from iaxclient
 * 
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
 * Copyright (C) 2006 Mikael Magnusson
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

#include <yatephone.h>

extern "C"  {
#include <speex.h>
}

using namespace TelEngine;

static TranslatorCaps caps[] = {
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 }
};

static Mutex s_cmutex;
static int s_count = 0;

/**
 * Query Speex for its version.
 *
 * @param major Returns major version number.
 * @param minor Returns minor version number.
 * @param micro Returns micro version number.
 */
static void QuerySpeexVersion(int& major, int& minor, int& micro)
{
    Debug(DebugAll,"QuerySpeexVersion()");
    speex_lib_ctl(SPEEX_LIB_GET_MAJOR_VERSION, &major);
    speex_lib_ctl(SPEEX_LIB_GET_MINOR_VERSION, &minor);
    speex_lib_ctl(SPEEX_LIB_GET_MICRO_VERSION, &micro);
}

/**
 * Query Speex for its capabilities in a certain mode.
 *
 * @param type As defined by speex.h, one of SPEEX_MODEID_{UWB, WB, NB}.
 * @param mode Quality mode.
 * @param fsize Returns size of one frame in bytes.
 * @param ftime Returns length of one frame in microseconds.
 * @param srate Returns sample rate in Hz.
 */
static void QuerySpeexCodec(int type, int mode, int& fsize, int& ftime, int& srate)
{
    Debug(DebugAll,"QuerySpeexCodec(%d, %d)", type, mode);

    void *state;
    int bitrate, samples, temp;

    switch (type) {
	case SPEEX_MODEID_UWB:
	    state = speex_encoder_init(&speex_uwb_mode);
	    break;
	case SPEEX_MODEID_WB:
	    state = speex_encoder_init(&speex_wb_mode);
	    break;
	case SPEEX_MODEID_NB:
	default:
	    state = speex_encoder_init(&speex_nb_mode);
	    break;
    }

    speex_encoder_ctl(state, SPEEX_SET_MODE, &mode);
    speex_encoder_ctl(state, SPEEX_GET_BITRATE, &bitrate);
    speex_encoder_ctl(state, SPEEX_GET_SAMPLING_RATE, &srate);
    speex_encoder_ctl(state, SPEEX_GET_FRAME_SIZE, &samples);
    speex_encoder_destroy(state);

    XDebug("speexcodec", DebugAll,
	    "type: %d, mode: %d, bitrate: %d bps, samplerate: %d Hz, samples: %d",
	    type, mode, bitrate, srate, samples);

    temp = srate / samples;
    fsize = (bitrate / temp + 7) / 8;       // blocksize in bytes
    ftime = 1000000 / temp;                 // in microseconds

    XDebug("speexcodec", DebugAll,
	    "frame size: %d bytes, duration: %d microseconds",
	    fsize, ftime);
}

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
    SpeexCodec(const char* sFormat, const char* dFormat, bool encoding, int type, int mode);
    ~SpeexCodec();
    virtual void Consume(const DataBlock& data, unsigned long tStamp);
private:
    bool m_encoding;
    DataBlock m_data;

    void *m_state;
    SpeexBits *m_bits;

    const FormatInfo *m_sFormatInfo;
    const FormatInfo *m_dFormatInfo;
    unsigned int m_bsamples;
    unsigned int m_bsize;
};

SpeexCodec::SpeexCodec(const char* sFormat, const char* dFormat, bool encoding, int type, int mode)
    : DataTranslator(sFormat,dFormat), m_encoding(encoding),
      m_state(NULL), m_bits(NULL)
{
    Debug("speexcodec", DebugAll,
	    "SpeexCodec::SpeexCodec(\"%s\",\"%s\",%scoding) [%p]",
	    sFormat,dFormat, m_encoding ? "en" : "de", this);

    m_sFormatInfo = FormatRepository::getFormat(sFormat);
    m_dFormatInfo = FormatRepository::getFormat(dFormat);

    m_bits = new SpeexBits;
    speex_bits_init(m_bits);

    if (encoding) {
	switch (type) {
	    case SPEEX_MODEID_UWB:
		m_state = speex_encoder_init(&speex_uwb_mode);
		break;
	    case SPEEX_MODEID_WB:
		m_state = speex_encoder_init(&speex_wb_mode);
		break;
	    case SPEEX_MODEID_NB:
	    default:
		m_state = speex_encoder_init(&speex_nb_mode);
		break;
	}

	if (m_state) {
	    speex_encoder_ctl(m_state, SPEEX_SET_MODE, &mode);
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

void SpeexCodec::Consume(const DataBlock& data, unsigned long tStamp)
{
    if (!(m_state && m_bits && getTransSource()))
	return;
    if (!ref())
	return;

    m_data += data;
    DataBlock outdata;

    unsigned int frames = 0;
    unsigned int consumed = 0;
    int frame_size = 0;
    int ret = 0;

    speex_decoder_ctl(m_state, SPEEX_GET_FRAME_SIZE, &frame_size);
//     frame_size = 35;

    if (m_encoding) {
	frame_size = m_dFormatInfo->frameSize;
	frames = m_data.length() / m_bsize;
	consumed = frames * m_bsize;

	if (frames) {
	    outdata.assign(0, frames * frame_size);
	    char* d = (char*)outdata.data();
	    short* s = (short*)m_data.data();

	    for (unsigned int i = 0; i < frames; i++) {
		speex_bits_reset(m_bits);
		speex_encode_int(m_state, s, m_bits);
		d += speex_bits_write(m_bits, d, frame_size);
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

    if (!m_encoding)
    XDebug("SpeexCodec", DebugAll,
	    "%scoding %d frames of %d input bytes (consumed %d) in %d output bytes, frame size %d, time %d, ret %d",
	   m_encoding ? "en" : "de", frames, m_data.length(), consumed, outdata.length(), frame_size, tStamp, ret);

    if (frames) {
	m_data.cut(-consumed);
	getTransSource()->Forward(outdata, tStamp);
    }

    deref();
}

SpeexPlugin::SpeexPlugin()
{
    int major, minor, micro;
    QuerySpeexVersion(major, minor, micro);
    Output("Loaded module Speex - based on libspeex-%d.%d.%d",
	    major, minor, micro);

    const FormatInfo* f;
    int fsize, ftime, srate;

    QuerySpeexCodec(SPEEX_MODEID_NB, 2, fsize, ftime, srate);
    f = FormatRepository::addFormat("speex-5k95", fsize, ftime, "audio", srate, 1);
    caps[0].src = caps[1].dest = f;
    caps[0].dest = caps[1].src = FormatRepository::getFormat("slin");

    QuerySpeexCodec(SPEEX_MODEID_NB, 3, fsize, ftime, srate);
    f = FormatRepository::addFormat("speex-8k", fsize, ftime, "audio", srate, 1);
    caps[2].src = caps[3].dest = f;
    caps[2].dest = caps[3].src = FormatRepository::getFormat("slin");

    QuerySpeexCodec(SPEEX_MODEID_NB, 4, fsize, ftime, srate);
    f = FormatRepository::addFormat("speex-11k", fsize, ftime, "audio", srate, 1);
    caps[4].src = caps[5].dest = f;
    caps[4].dest = caps[5].src = FormatRepository::getFormat("slin");

    QuerySpeexCodec(SPEEX_MODEID_NB, 5, fsize, ftime, srate);
    f = FormatRepository::addFormat("speex-15k", fsize, ftime, "audio", srate, 1);
    caps[6].src = caps[7].dest = f;
    caps[6].dest = caps[7].src = FormatRepository::getFormat("slin");

    QuerySpeexCodec(SPEEX_MODEID_NB, 6, fsize, ftime, srate);
    f = FormatRepository::addFormat("speex-18k2", fsize, ftime, "audio", srate, 1);
    caps[8].src = caps[9].dest = f;
    caps[8].dest = caps[9].src = FormatRepository::getFormat("slin");

    QuerySpeexCodec(SPEEX_MODEID_WB, 2, fsize, ftime, srate);
    f = FormatRepository::addFormat("speex-7k75/16000", fsize, ftime, "audio", srate, 1);
    caps[10].src = caps[11].dest = f;
    caps[10].dest = caps[11].src = FormatRepository::getFormat("slin/16000");

    QuerySpeexCodec(SPEEX_MODEID_WB, 3, fsize, ftime, srate);
    f = FormatRepository::addFormat("speex-9k8/16000", fsize, ftime, "audio", srate, 1);
    caps[12].src = caps[13].dest = f;
    caps[12].dest = caps[13].src = FormatRepository::getFormat("slin/16000");

    QuerySpeexCodec(SPEEX_MODEID_WB, 4, fsize, ftime, srate);
    f = FormatRepository::addFormat("speex-12k8/16000", fsize, ftime, "audio", srate, 1);
    caps[14].src = caps[15].dest = f;
    caps[14].dest = caps[15].src = FormatRepository::getFormat("slin/16000");

    QuerySpeexCodec(SPEEX_MODEID_WB, 5, fsize, ftime, srate);
    f = FormatRepository::addFormat("speex-16k9/16000", fsize, ftime, "audio", srate, 1);
    caps[16].src = caps[17].dest = f;
    caps[16].dest = caps[17].src = FormatRepository::getFormat("slin/16000");

    QuerySpeexCodec(SPEEX_MODEID_WB, 6, fsize, ftime, srate);
    f = FormatRepository::addFormat("speex-20k6/16000", fsize, ftime, "audio", srate, 1);
    caps[18].src = caps[19].dest = f;
    caps[18].dest = caps[19].src = FormatRepository::getFormat("slin/16000");

    QuerySpeexCodec(SPEEX_MODEID_UWB, 6, fsize, ftime, srate);
    f = FormatRepository::addFormat("speex/32000", fsize, ftime, "audio", srate, 1);
    caps[20].src = caps[21].dest = f;
    caps[20].dest = caps[21].src = FormatRepository::getFormat("slin/32000");
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
    if (sFormat == "slin") {
	if (dFormat == "speex-5k95")
	    return new SpeexCodec(sFormat, dFormat, true, SPEEX_MODEID_NB, 2);
	else if (dFormat == "speex-8k")
	    return new SpeexCodec(sFormat, dFormat, true, SPEEX_MODEID_NB, 3);
	else if (dFormat == "speex-11k")
	    return new SpeexCodec(sFormat, dFormat, true, SPEEX_MODEID_NB, 4);
	else if (dFormat == "speex-15k")
	    return new SpeexCodec(sFormat, dFormat, true, SPEEX_MODEID_NB, 5);
	else if (dFormat == "speex-18k2")
	    return new SpeexCodec(sFormat, dFormat, true, SPEEX_MODEID_NB, 6);
    }
    else if (sFormat == "slin/16000") {
	if (dFormat == "speex-7k75/16000")
	    return new SpeexCodec(sFormat, dFormat, true, SPEEX_MODEID_WB, 2);
	else if (dFormat == "speex-9k8/16000")
	    return new SpeexCodec(sFormat, dFormat, true, SPEEX_MODEID_WB, 3);
	else if (dFormat == "speex-12k8/16000")
	    return new SpeexCodec(sFormat, dFormat, true, SPEEX_MODEID_WB, 4);
	else if (dFormat == "speex-16k8/16000")
	    return new SpeexCodec(sFormat, dFormat, true, SPEEX_MODEID_WB, 5);
	else if (dFormat == "speex-20k6/16000")
	    return new SpeexCodec(sFormat, dFormat, true, SPEEX_MODEID_WB, 6);
    }
    else if (sFormat == "slin/32000") {
	if (dFormat == "speex/32000")
	    return new SpeexCodec(sFormat, dFormat, true, SPEEX_MODEID_UWB, 6);
    }
    else if (dFormat == "slin") {
	if (sFormat == "speex-5k95")
	    return new SpeexCodec(sFormat, dFormat, false, SPEEX_MODEID_NB, 2);
	else if (sFormat == "speex-8k")
	    return new SpeexCodec(sFormat, dFormat, false, SPEEX_MODEID_NB, 3);
	else if (sFormat == "speex-11k")
	    return new SpeexCodec(sFormat, dFormat, false, SPEEX_MODEID_NB, 4);
	else if (sFormat == "speex-15k")
	    return new SpeexCodec(sFormat, dFormat, false, SPEEX_MODEID_NB, 5);
	else if (sFormat == "speex-18k2")
	    return new SpeexCodec(sFormat, dFormat, false, SPEEX_MODEID_NB, 6);
    }
    else if (dFormat == "slin/16000") {
	if (sFormat == "speex-7k75/16000")
	    return new SpeexCodec(sFormat, dFormat, false, SPEEX_MODEID_WB, 2);
	else if (sFormat == "speex-9k8/16000")
	    return new SpeexCodec(sFormat, dFormat, false, SPEEX_MODEID_WB, 3);
	else if (sFormat == "speex-12k8/16000")
	    return new SpeexCodec(sFormat, dFormat, false, SPEEX_MODEID_WB, 4);
	else if (sFormat == "speex-16k8/16000")
	    return new SpeexCodec(sFormat, dFormat, false, SPEEX_MODEID_WB, 5);
	else if (sFormat == "speex-20k6/16000")
	    return new SpeexCodec(sFormat, dFormat, false, SPEEX_MODEID_WB, 6);
    }
    else if (dFormat == "slin/32000") {
	if (sFormat == "speex/32000")
	    return new SpeexCodec(sFormat, dFormat, false, SPEEX_MODEID_UWB, 6);
    }
    return 0;
}

const TranslatorCaps* SpeexPlugin::getCapabilities() const
{
    return caps;
}

INIT_PLUGIN(SpeexPlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
