/**
 * efrcodec.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * GSM-EFR transcoder implemented using 3GPP AMR codec
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2015 Null Team
 * Author: Paul Chitescu
 *
 * AMR codec library by Stanislav Brabec at http://www.penguin.cz/~utx/amr
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
#include <interf_enc.h>
#include <interf_dec.h>
}
namespace RxTypes {
// There is a conflict between encoder and decoder so insulate in a namespace
#include <sp_dec.h>
};

using namespace TelEngine;
namespace { // anonymous

#define MODNAME "efrcodec"

// Transcoding voice size, 20ms of 8kHz slin data
#define SAMPLES_FRAME 160

// Transcoding buffer size, 2 bytes per sample
#define BUFFER_SIZE   (2*SAMPLES_FRAME)

// AMR Mode 7 (12.2) encoder frame size including mode
#define AMR_MR122_SIZE 32

// GSM-EFR frame size
#define EFR_FRAME_SIZE 31

class EfrPlugin : public Plugin, public TranslatorFactory
{
public:
    EfrPlugin();
    ~EfrPlugin();
    virtual void initialize();
    virtual bool isBusy() const;
    virtual DataTranslator* create(const DataFormat& sFormat, const DataFormat& dFormat);
    virtual const TranslatorCaps* getCapabilities() const;
};

class EfrTrans : public DataTranslator
{
public:
    EfrTrans(const char* sFormat, const char* dFormat, void* amrState, bool encoding);
    virtual ~EfrTrans();
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags);
    inline bool valid() const
	{ return 0 != m_amrState; }
protected:
    void filterBias(short* buf, unsigned int len);
    bool dataError(const char* text = 0);
    virtual bool pushData(unsigned long& tStamp, unsigned long& flags) = 0;
    void* m_amrState;
    DataBlock m_data;
    int m_bias;
    bool m_encoding;
    bool m_showError;
};

// Encoding specific class
class EfrEncoder : public EfrTrans
{
public:
    inline EfrEncoder(const char* sFormat, const char* dFormat)
	: EfrTrans(sFormat,dFormat,::Encoder_Interface_init(0),true)
	{ }
    virtual ~EfrEncoder();
protected:
    virtual bool pushData(unsigned long& tStamp, unsigned long& flags);
};

// Decoding specific class
class EfrDecoder : public EfrTrans
{
public:
    inline EfrDecoder(const char* sFormat, const char* dFormat)
	: EfrTrans(sFormat,dFormat,::Decoder_Interface_init(),false)
	{ }
    virtual ~EfrDecoder();
protected:
    virtual bool pushData(unsigned long& tStamp, unsigned long& flags);
};

// Module data
static int count = 0;                   // Created objects

static TranslatorCaps caps[] = {
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
};


// Arbitrary type transcoder constructor
EfrTrans::EfrTrans(const char* sFormat, const char* dFormat, void* amrState, bool encoding)
    : DataTranslator(sFormat,dFormat),
      m_amrState(amrState), m_bias(0), m_encoding(encoding), m_showError(true)
{
    Debug(MODNAME,DebugAll,"EfrTrans::EfrTrans('%s','%s',%p,%s) [%p]",
	sFormat,dFormat,amrState,String::boolText(encoding),this);
    count++;
}

// Destructor, closes the channel
EfrTrans::~EfrTrans()
{
    Debug(MODNAME,DebugAll,"EfrTrans::~EfrTrans() [%p]",this);
    m_amrState = 0;
    count--;
}

// Actual transcoding of data
unsigned long EfrTrans::Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
{
    if (!(m_amrState && getTransSource()))
	return 0;
    if (data.null() && (flags & DataSilent))
	return getTransSource()->Forward(data,tStamp,flags);
    ref();
    if (m_encoding && (tStamp != invalidStamp()) && !m_data.null())
	tStamp -= (m_data.length() / 2);
    m_data += data;
    if (m_encoding) {
	// the AMR encoder errors on biased silence so suppress it
	unsigned int len = data.length();
	if (len)
	    filterBias((short*)m_data.data(m_data.length() - len),len / 2);
    }
    while (pushData(tStamp,flags))
	;
    deref();
    return invalidStamp();
}

// High pass filter to get rid of the bias - for example when transcoding through A-Law
void EfrTrans::filterBias(short* buf, unsigned int len)
{
    if (!buf)
	return;
    while (len--) {
	int val = *buf;
	// work on integers using sample * 16
	m_bias = (m_bias * 63 + val * 16) / 64;
	// substract the averaged bias and saturate
	val -= m_bias / 16;
	if (val > 32767)
	    val = 32767;
	else if (val < -32767)
	    val = -32767;
	*buf++ = val;
    }
}

// Data error, report error 1st time and clear buffer
bool EfrTrans::dataError(const char* text)
{
    if (m_showError) {
	m_showError = false;
	const char* prefix = ": ";
	if (!text)
	    prefix = text = "";
	Debug(MODNAME,DebugWarn,"Error transcoding data%s%s [%p]",prefix,text,this);
    }
    m_data.clear();
    return false;
}


// Encoder cleanup
EfrEncoder::~EfrEncoder()
{
    Debug(MODNAME,DebugAll,"EfrEncoder::~EfrEncoder() %p [%p]",m_amrState,this);
    if (m_amrState)
	::Encoder_Interface_exit(m_amrState);
}

// Encode accumulated slin data and push it to the consumer
bool EfrEncoder::pushData(unsigned long& tStamp, unsigned long& flags)
{
    if (m_data.length() < BUFFER_SIZE)
	return false;

    unsigned char unpacked[AMR_MR122_SIZE];
    if (::Encoder_Interface_Encode(m_amrState,MR122,(short*)m_data.data(),unpacked,0) != AMR_MR122_SIZE)
	return dataError("encoder");
    if (((unpacked[0] >> 3) & 0x0f) != MR122) {
	// invalid mode returned in frame - don't send the the data at all
	m_data.cut(-BUFFER_SIZE);
	tStamp += SAMPLES_FRAME;
	return (0 != m_data.length());
    }

    unsigned char buffer[EFR_FRAME_SIZE];
    unsigned char leftover = 0xc0;
    for (int i = 1; i <= EFR_FRAME_SIZE; i++) {
	buffer[i-1] = leftover | (unpacked[i] >> 4);
	leftover = (unpacked[i] << 4) & 0xf0;
    }
    m_data.cut(-BUFFER_SIZE);
    DataBlock outData(buffer,EFR_FRAME_SIZE,false);
    getTransSource()->Forward(outData,tStamp,flags);
    outData.clear(false);
    tStamp += SAMPLES_FRAME;
    flags &= ~DataMark;
    m_showError = true;
    return (0 != m_data.length());
}


// Decoder cleanup
EfrDecoder::~EfrDecoder()
{
    Debug(MODNAME,DebugAll,"EfrDecoder::~EfrDecoder() %p [%p]",m_amrState,this);
    if (m_amrState)
	::Decoder_Interface_exit(m_amrState);
}

// Decode AMR data and push it to the consumer
bool EfrDecoder::pushData(unsigned long& tStamp, unsigned long& flags)
{
    if (m_data.length() < EFR_FRAME_SIZE)
	return false;

    unsigned const char* ptr = (unsigned const char*)m_data.data();
    if ((ptr[0] & 0xf0) != 0xc0)
	return dataError("invalid signature");

    unsigned char unpacked[AMR_MR122_SIZE];
    unpacked[0] = (MR122 << 3) | 0x04; // Quality bit set
    unsigned char leftover = (ptr[0] << 4) & 0xf0;
    for (int i = 1; i < EFR_FRAME_SIZE; i++) {
	unpacked[i] = leftover | (ptr[i] >> 4);
	leftover = (ptr[i] << 4) & 0xf0;
    }
    unpacked[AMR_MR122_SIZE-1] = leftover;
    short buffer[SAMPLES_FRAME];
    ::Decoder_Interface_Decode(m_amrState,unpacked,buffer,RxTypes::RX_SPEECH_GOOD);
    DataBlock outData(buffer,BUFFER_SIZE,false);
    getTransSource()->Forward(outData,tStamp,flags);
    outData.clear(false);
    tStamp += SAMPLES_FRAME;
    flags &= ~DataMark;
    m_data.cut(-EFR_FRAME_SIZE);
    m_showError = true;
    return (0 != m_data.length());
}


// Plugin and translator factory
EfrPlugin::EfrPlugin()
    : Plugin("efrcodec"), TranslatorFactory("gsm-efr")
{
    Output("Loaded module GSM-EFR codec - based on 3GPP AMR code");
    const FormatInfo* f = FormatRepository::addFormat("gsm-efr",EFR_FRAME_SIZE,20000);
    caps[0].src = caps[1].dest = f;
    caps[0].dest = caps[1].src = FormatRepository::getFormat("slin");
    // FIXME: put proper conversion costs
    caps[0].cost = caps[1].cost = 5;
}

EfrPlugin::~EfrPlugin()
{
    Output("Unloading module GSM-EFR with %d codecs still in use",count);
}

bool EfrPlugin::isBusy() const
{
    return (count != 0);
}

// Create transcoder instance for requested formats
DataTranslator* EfrPlugin::create(const DataFormat& sFormat, const DataFormat& dFormat)
{
    if ((sFormat == "slin") && (dFormat == "gsm-efr"))
	return new EfrEncoder(sFormat,dFormat);
    else if ((sFormat == "gsm-efr") && (dFormat == "slin"))
	return new EfrDecoder(sFormat,dFormat);
    return 0;
}

const TranslatorCaps* EfrPlugin::getCapabilities() const
{
    return caps;
}

void EfrPlugin::initialize()
{
    Output("Initializing module GSM-EFR");
}


INIT_PLUGIN(EfrPlugin);

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow)
	return !__plugin.isBusy();
    return true;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
