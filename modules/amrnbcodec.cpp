/**
 * amrnbcodec.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * AMR narrowband transcoder implemented using 3GPP codec
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
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

// IF1/GP3 is Bandwidth-Efficient Mode
// IF2 is Octet-aligned Mode (not supported here)

using namespace TelEngine;
namespace { // anonymous

#define MODNAME "amrnbcodec"

// Transcoding voice size, 20ms of 8kHz slin data
#define SAMPLES_FRAME 160

// Transcoding buffer size, 2 bytes per sample
#define BUFFER_SIZE   (2*SAMPLES_FRAME)

// Maximum compressed frame size
#define MAX_AMRNB_SIZE 33

// Maximum number of frames we are willing to decode in a packet
#define MAX_PKT_FRAMES  4

// Discontinuous Transmission (DTX)
static bool s_discontinuous = false;

// Change mode only to neighbor
static bool s_neighbor = false;

// Mode change period in frames
static uint8_t s_period = 1;

// Supported modes mask
static uint8_t s_mask = 0xff;

// Default mode
static Mode s_mode = MR122;


class AmrPlugin : public Plugin, public TranslatorFactory
{
public:
    AmrPlugin();
    ~AmrPlugin();
    virtual void initialize();
    virtual bool isBusy() const;
    virtual DataTranslator* create(const DataFormat& sFormat, const DataFormat& dFormat);
    virtual const TranslatorCaps* getCapabilities() const;
};

class AmrTrans : public DataTranslator
{
public:
    AmrTrans(const char* sFormat, const char* dFormat, void* amrState, bool octetAlign, bool encoding);
    virtual ~AmrTrans();
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags);
    inline bool valid() const
	{ return 0 != m_amrState; }
    static inline const char* alignName(bool align)
	{ return align ? "octet aligned" : "bandwidth efficient"; }
protected:
    void filterBias(short* buf, unsigned int len);
    bool dataError(const char* text = 0);
    virtual bool pushData(unsigned long& tStamp, unsigned long& flags) = 0;
    void* m_amrState;
    DataBlock m_data;
    int m_bias;
    bool m_encoding;
    bool m_showError;
    bool m_octetAlign;
    Mode m_cmr;
};

// Encoding specific class
class AmrEncoder : public AmrTrans
{
public:
    inline AmrEncoder(const char* sFormat, const char* dFormat, bool octetAlign, bool discont = false)
	: AmrTrans(sFormat,dFormat,::Encoder_Interface_init(discont ? 1 : 0),octetAlign,true),
	 m_mode(s_mode), m_desired(s_mode), m_change(0), m_mask(s_mask),
	 m_period(s_period), m_neighbor(false), m_silent(false)
	{ }
    virtual ~AmrEncoder();
    virtual bool control(NamedList& params);
protected:
    void setMode(Mode mode);
    virtual void attached(bool added);
    virtual bool pushData(unsigned long& tStamp, unsigned long& flags);
    Mode m_mode;
    Mode m_desired;
    int m_change;
    uint8_t m_mask;
    uint8_t m_period;
    bool m_neighbor;
    bool m_silent;
};

// Decoding specific class
class AmrDecoder : public AmrTrans
{
public:
    inline AmrDecoder(const char* sFormat, const char* dFormat, bool octetAlign)
	: AmrTrans(sFormat,dFormat,::Decoder_Interface_init(),octetAlign,false)
	{ }
    virtual ~AmrDecoder();
protected:
    virtual bool pushData(unsigned long& tStamp, unsigned long& flags);
};

// Module data
static int count = 0;                   // Created objects

static TranslatorCaps caps[] = {
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 }
};

// Voice bits per mode 0-7, 8 = Silence, 15 = No Data
static int modeBits[16] = {
    95, 103, 118, 134, 148, 159, 204, 244, 39,
    -1, -1, -1, -1, -1, -1, 0
};

// Table for bitrate to mode conversion
static const TokenDict s_modes[] = {
    { "4.75", MR475 },
    { "5.15", MR515 },
    { "5.90", MR59  },
    { "6.70", MR67  },
    { "7.40", MR74  },
    { "7.95", MR795 },
    { "10.2", MR102 },
    { "12.2", MR122 },
    { 0, 0 }
};

// Helper function, parses a mode-set string to a bit mask
static uint8_t parseMask(const String& modes, uint8_t defMask = 0xff)
{
    if (modes.null())
	return defMask;
    uint8_t mask = 0;
    ObjList* lst = modes.split(',',false);
    for (ObjList* l = lst->skipNull(); l; l = l->skipNext()) {
	int mode = static_cast<const String*>(l->get())->toInteger(s_modes,-1);
	if (mode >= MR475 && mode <= MR122)
	    mask |= (1 << mode);
    }
    TelEngine::destruct(lst);
    return mask ? mask : defMask;
}

// Helper function, parses a possibly NULL mode-set string pointer
inline static uint8_t parseMask(const String* modes, uint8_t defMask = 0xff)
{
    return modes ? parseMask(*modes) : defMask;
}

// Helper function returning nearest allowed mode
static Mode getMode(int mode, uint8_t mask, int oldMode)
{
    if (mode < MR475)
	mode = MR475;
    else if (mode > MR122)
	mode = MR122;
    if (mask & (1 << mode))
	return (Mode)mode;
    if (oldMode > mode) {
	for (int m = mode + 1; m <= MR122; m++)
	    if (mask & (1 << m))
		return (Mode)m;
    }
    while (--mode >= MR475)
	if (mask & (1 << mode))
	    return (Mode)mode;
    return (Mode)oldMode;
}

// Helper function returning nearest neighbor allowed mode
static Mode getNeighbor(int mode, uint8_t mask, int oldMode)
{
    if (mode < MR475)
	mode = MR475;
    else if (mode > MR122)
	mode = MR122;
    if (mode == oldMode)
	return (Mode)mode;
    int m;
    if (oldMode < mode) {
	for (m = oldMode + 1; m <= mode; m++) {
	    if (mask & (1 << m))
		break;
	}
    }
    else {
	for (m = oldMode - 1; m >= mode; m--) {
	    if (mask & (1 << m))
		break;
	}
    }
    if (m < MR475)
	m = MR475;
    else if (m > MR122)
	m = MR122;
    return (Mode)m;
}

// Helper function, gets a number of bits and advances pointer, return -1 for error
static int getBits(unsigned const char*& ptr, int& len, int& bpos, unsigned char bits)
{
    if (!ptr)
	return -1;
    if (!bits)
	return 0;
    int ret = 0;
    int mask = 0x80;
    while (bits--) {
	if (len <= 0)
	    return -1;
	if (((ptr[0] >> (7 - bpos)) & 1) != 0)
	    ret |= mask;
	mask = mask >> 1;
	if (++bpos >= 8) {
	    bpos = 0;
	    ptr++;
	    len--;
	}
    }
    return ret;
}


// Arbitrary type transcoder constructor
AmrTrans::AmrTrans(const char* sFormat, const char* dFormat, void* amrState, bool octetAlign, bool encoding)
    : DataTranslator(sFormat,dFormat),
      m_amrState(amrState), m_bias(0), m_encoding(encoding), m_showError(true),
      m_octetAlign(octetAlign), m_cmr(s_mode)
{
    Debug(MODNAME,DebugAll,"AmrTrans::AmrTrans('%s','%s',%p,%s,%s) [%p]",
	sFormat,dFormat,amrState,String::boolText(octetAlign),String::boolText(encoding),this);
    count++;
}

// Destructor, closes the channel
AmrTrans::~AmrTrans()
{
    Debug(MODNAME,DebugAll,"AmrTrans::~AmrTrans() [%p]",this);
    m_amrState = 0;
    count--;
}

// Actual transcoding of data
unsigned long AmrTrans::Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
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
void AmrTrans::filterBias(short* buf, unsigned int len)
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
bool AmrTrans::dataError(const char* text)
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
AmrEncoder::~AmrEncoder()
{
    Debug(MODNAME,DebugAll,"AmrEncoder::~AmrEncoder() %p [%p]",m_amrState,this);
    if (m_amrState)
	::Encoder_Interface_exit(m_amrState);
}

// Encode accumulated slin data and push it to the consumer
bool AmrEncoder::pushData(unsigned long& tStamp, unsigned long& flags)
{
    if (m_data.length() < BUFFER_SIZE)
	return false;

    if ((m_mode != m_desired) && (--m_change <= 0)) {
	Mode mode = m_neighbor
	    ? getNeighbor(m_desired,m_mask,m_mode)
	    : getMode(m_desired,m_mask,m_mode);
	if (mode == m_mode)
	    m_desired = mode;
	else {
	    DDebug(MODNAME,DebugAll,"Encode mode changing %d -> %d, desired %d",m_mode,mode,m_desired);
	    m_mode = mode;
	    m_change = (mode == m_desired) ? 0 : m_period;
	}
    }

    unsigned char unpacked[MAX_AMRNB_SIZE+1];
    int len = ::Encoder_Interface_Encode(m_amrState,m_mode,(short*)m_data.data(),unpacked,0);
    if ((len <= 0) || (len >= MAX_AMRNB_SIZE))
	return dataError("encoder");
    Mode mode = (Mode)((unpacked[0] >> 3) & 0x0f);
    if (mode > MRDTX) {
	// invalid mode returned in frame - don't send the the data at all
	m_data.cut(-BUFFER_SIZE);
	tStamp += SAMPLES_FRAME;
	return (0 != m_data.length());
    }
    bool silent = (mode == MRDTX);
    if (m_silent && !silent)
	flags |= DataMark;
    m_silent = silent;
    unpacked[len] = 0;
    XDebug(MODNAME,DebugAll,"Encoded mode %d frame to %d bytes first %02x [%p]",
	mode,len,unpacked[0],this);
    unsigned char buffer[MAX_AMRNB_SIZE];
    // build a TOC with just one entry
    if (m_octetAlign) {
	// 4 bit CMR, 4 bits reserved
	buffer[0] = (m_cmr << 4);
	// 1 bit follows (0), 4 bits of mode, 1 bit Q, 2 bits padding (0)
	buffer[1] = unpacked[0] & 0x7c;
	// AMR data
	for (int i = 1; i < len; i++)
	    buffer[i+1] = unpacked[i];
	len++;
    }
    else {
	// 4 bit CMR, 1 bit follows (forced 0), 3 bits of mode
	buffer[0] = (m_cmr << 4) | ((unpacked[0] >> 4) & 0x07);
	// 1 bit of mode and 1 bit Q
	unsigned char leftover = (unpacked[0] << 4) & 0xc0;
	// AMR data
	for (int i = 1; i < len; i++) {
	    buffer[i] = leftover | (unpacked[i] >> 2);
	    leftover = (unpacked[i] << 6) & 0xc0;
	}
	switch (modeBits[mode] & 7) {
	    case 0:
	    case 7:
		buffer[len++] = leftover;
	}
    }
    m_data.cut(-BUFFER_SIZE);
    DataBlock outData(buffer,len,false);
    getTransSource()->Forward(outData,tStamp,flags);
    outData.clear(false);
    tStamp += SAMPLES_FRAME;
    flags &= ~DataMark;
    m_showError = true;
    return (0 != m_data.length());
}

// Change the desired mode
void AmrEncoder::setMode(Mode mode)
{
    m_desired = mode;
    if (m_period)
	m_change = m_period;
    else
	m_mode = mode;
}

// Execute control operations
bool AmrEncoder::control(NamedList& params)
{
    bool ok = false;
    int mode = params.getIntValue(YSTRING("mode"),s_modes,-1);
    if (mode >= MR475 && mode <= MR122) {
	if (params.getBoolValue(YSTRING("force"))) {
	    m_mode = m_desired = (Mode)mode;
	    m_change = 0;
	}
	else
	    setMode(getMode(mode,m_mask,m_desired));
	ok = true;
    }
    mode = params.getIntValue(YSTRING("cmr"),s_modes,-1);
    if (mode >= MR475 && mode <= MR122) {
	m_cmr = (Mode)mode;
	ok = true;
    }
    return TelEngine::controlReturn(&params,AmrTrans::control(params) || ok);
}

// Callback to pick AMR parameters from consumer (typically RTP)
void AmrEncoder::attached(bool added)
{
    AmrTrans::attached(added);
    if (!added)
	return;
    ObjList* lst = getConsumers();
    if (!lst)
	return;
    RefPointer<DataConsumer> cons = static_cast<DataConsumer*>(lst->get());
    if (!cons)
	return;
    const DataFormat& fmt = cons->getFormat();
    m_mask = parseMask(fmt[YSTRING("mode-set")],s_mask);
    m_neighbor = fmt.getIntValue(YSTRING("mode-change-neighbor"),0) != 0;
    int tmp = fmt.getIntValue(YSTRING("mode-change-period"),s_period);
    if (tmp < 0)
	tmp = 0;
    else if (tmp > 4)
	tmp = 4;
    m_period = (uint8_t)tmp;
    Debug(MODNAME,DebugAll,"AmrEncoder picked mask=0x%02X neigh=%s period=%d [%p]",
	m_mask,String::boolText(m_neighbor),tmp,this);
    setMode(getMode(m_mode,m_mask,m_desired));
    m_cmr = m_desired;
}


// Decoder cleanup
AmrDecoder::~AmrDecoder()
{
    Debug(MODNAME,DebugAll,"AmrDecoder::~AmrDecoder() %p [%p]",m_amrState,this);
    if (m_amrState)
	::Decoder_Interface_exit(m_amrState);
}

// Decode AMR data and push it to the consumer
bool AmrDecoder::pushData(unsigned long& tStamp, unsigned long& flags)
{
    if (m_data.length() < 2)
	return false;
    unsigned const char* ptr = (unsigned const char*)m_data.data();
    int len = m_data.length();
    bool octetHint = m_octetAlign;
    // an octet aligned packet should have 0 in the 4 reserved bits of CMR
    //  and in the lower 2 bits of first TOC entry octet
    if ((ptr[0] & 0x0f) | (ptr[1] & 0x03))
	octetHint = false;
    else if (((ptr[1] & 0xc0) == 0) && (len > 14))
	octetHint = true;
    if (octetHint != m_octetAlign) {
	Debug(MODNAME,DebugNote,"Decoder switching from %s to %s mode [%p]",
	    alignName(m_octetAlign),alignName(octetHint),this);
	m_octetAlign = octetHint;
	// TODO: find and notify paired encoder about the new alignment
    }
    int bpos = 0;
    unsigned char cmr = getBits(ptr,len,bpos,4) >> 4;
    if (m_octetAlign)
	getBits(ptr,len,bpos,4);
    unsigned int tocLen = 0;
    unsigned char toc[MAX_PKT_FRAMES];
    int dataBits = 0;
    // read the TOC
    for (;;) {
	int ft = getBits(ptr,len,bpos,6);
	if (m_octetAlign)
	    getBits(ptr,len,bpos,2);
	if (ft < 0)
	    return dataError("TOC truncated");
	int nBits = modeBits[(ft >> 3) & 0x0f];
	// discard the entire packet if an invalid frame is found
	if (nBits < 0)
	    return dataError("invalid mode");
	if (m_octetAlign)
	    nBits = (nBits + 7) & (~7);
	dataBits += nBits;
	toc[tocLen++] = ft & 0x7c; // keep type and quality bit
	// does another TOC follow?
	if (0 == (ft & 0x80))
	    break;
	if (tocLen >= MAX_PKT_FRAMES)
	    return dataError("TOC too large");
    }
    if (dataBits > (8*len - bpos))
	return dataError("data truncated");
    // We read the TOC, now pick the following voice frames and decode
    for (unsigned int idx = 0; idx < tocLen; idx++) {
	if (m_octetAlign && (bpos != 0))
	    return dataError("internal alignment error");
	int mode = (toc[idx] >> 3) & 0x0f;
	bool good = 0 != (toc[idx] & 0x04);
	int nBits = modeBits[mode];
	XDebug(MODNAME,DebugAll,"Decoding %d bits %s mode %d frame %u [%p]",
	    nBits,(good ? "good" : "bad"),mode,idx,this);
	if (m_octetAlign)
	    nBits = (nBits + 7) & (~7);
	unsigned char unpacked[MAX_AMRNB_SIZE];
	unpacked[0] = toc[idx];
	for (unsigned int i = 1; i < MAX_AMRNB_SIZE; i++) {
	    int bits = (nBits <= 8) ? nBits : 8;
	    unpacked[i] = getBits(ptr,len,bpos,bits);
	    nBits -= bits;
	}
	short buffer[SAMPLES_FRAME];
	int type = (MRDTX == mode) ?
	    (good ? RxTypes::RX_SID_UPDATE : RxTypes::RX_SID_BAD) :
	    (good ? RxTypes::RX_SPEECH_GOOD : RxTypes::RX_SPEECH_DEGRADED);
	::Decoder_Interface_Decode(m_amrState,unpacked,buffer,type);
	DataBlock outData(buffer,BUFFER_SIZE,false);
	getTransSource()->Forward(outData,tStamp,flags);
	outData.clear(false);
	tStamp += SAMPLES_FRAME;
	flags &= ~DataMark;
    }
    if (bpos)
	len--;
    // now len holds how many bytes we should keep in data buffer
    m_data.cut(len-(int)m_data.length());
    // RFC 4687: CMR 15 means no codec mode change
    if (cmr != 15 && cmr != m_cmr) {
	Debug(MODNAME,DebugNote,"Remote CMR changed from %d to %d [%p]",
	    m_cmr,cmr,this);
	m_cmr = (Mode)cmr;
	// TODO: find and notify paired encoder about the mode change request
    }
    m_showError = true;
    return (0 != m_data.length());
}


// Plugin and translator factory
AmrPlugin::AmrPlugin()
    : Plugin("amrnbcodec"), TranslatorFactory("amr-nb")
{
    Output("Loaded module AMR-NB codec - based on 3GPP code");
    const FormatInfo* f = FormatRepository::addFormat("amr",0,20000);
    caps[0].src = caps[1].dest = f;
    f = FormatRepository::addFormat("amr-o",0,20000);
    caps[2].src = caps[3].dest = f;
    caps[0].dest = caps[1].src = caps[2].dest = caps[3].src = FormatRepository::getFormat("slin");
    // FIXME: put proper conversion costs
    caps[0].cost = caps[1].cost = caps[2].cost = caps[3].cost = 5;
}

AmrPlugin::~AmrPlugin()
{
    Output("Unloading module AMR-NB with %d codecs still in use",count);
}

bool AmrPlugin::isBusy() const
{
    return (count != 0);
}

// Create transcoder instance for requested formats
DataTranslator* AmrPlugin::create(const DataFormat& sFormat, const DataFormat& dFormat)
{
    if (sFormat == "slin") {
	if (dFormat == "amr")
	    return new AmrEncoder(sFormat,dFormat,false,s_discontinuous);
	if (dFormat == "amr-o")
	    return new AmrEncoder(sFormat,dFormat,true,s_discontinuous);
    }
    else if (dFormat == "slin") {
	if (sFormat == "amr")
	    return new AmrDecoder(sFormat,dFormat,false);
	if (sFormat == "amr-o")
	    return new AmrDecoder(sFormat,dFormat,true);
    }
    return 0;
}

const TranslatorCaps* AmrPlugin::getCapabilities() const
{
    return caps;
}

void AmrPlugin::initialize()
{
    Output("Initializing module AMR-NB");
    Configuration cfg(Engine::configFile("amrnbcodec"));
    s_mask = parseMask(cfg.getKey("general","mode-set"));
    s_mode = getMode(cfg.getIntValue("general","mode",s_modes,MR122),s_mask,MR122);
    s_discontinuous = cfg.getBoolValue("general","discontinuous",false);
    s_neighbor = cfg.getBoolValue("general","mode-change-neighbor",false);
    int tmp = cfg.getIntValue("general","mode-change-period",1);
    if (tmp < 0)
	tmp = 0;
    else if (tmp > 4)
	tmp = 4;
    s_period = tmp;
}


INIT_PLUGIN(AmrPlugin);

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow)
	return !__plugin.isBusy();
    return true;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
