/**
 * modem.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Modem
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

#include "yatemodem.h"

#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace TelEngine;

// Amplitudes for the sine generator (mark and space) used to modulate data
#define MARK_AMPLITUDE  6300
#define SPACE_AMPLITUDE 6300

// Pattern length in ms to add after a modulated message
#define PATTERN_AFTER   2

// Uncomment this to view the bits decoded by the modem
#define YMODEM_BUFFER_BITS

// Constant values used by the FSK filter to modulate/demodulate data
class FilterConst
{
public:
    // Build constants used by this filter
    FilterConst(FSKModem::Type type);
    // Release memory
    ~FilterConst();
    // Calculate how many samples do we need to modulate n bits
    unsigned int bufLen(unsigned int nbits);
    // Get timing samples and advance the index
    inline unsigned int timingSamples(unsigned int& index) {
	    unsigned int tmp = bitSamples[index];
	    if (++index == bitSamplesLen)
		index = 0;
	    return tmp;
	}

    // Signal properties
    float markFreq;                      // Mark frequency
    float spaceFreq;                     // Space frequency
    float sampleRate;                    // Sampling rate
    float baudRate;                      // Transmission baud rate (bps)
    // Modulation/demodulation data
    double markCoef;                     // Mark coefficient
    double spaceCoef;                    // Space coefficient
    // Data used to demodulate signals
    unsigned int spb;                    // The number of samples per bit (also the length of all buffers)
    unsigned int halfSpb;                // Half of the spb value (used to filter data)
    float bitLen;                        // The exact value of bit length in samples
    float halfBitLen;                    // Half the bit length
    float markGain;
    float spaceGain;
    float lowbandGain;
    float* mark;
    float* space;
    float* lowband;
    // Data used to modulate signals
    double accSin;                       // Accumulate sine radians during modulation
                                         // This value is updated after the header is modulated
                                         // and used as start value for each message data
    unsigned int* bitSamples;            // Array of bit samples nedded to maintain the modulation timing
    unsigned int bitSamplesLen;          // The length of the bitSamples array
    DataBlock header;                    // Modulated message header
                                         // e.g. ETSI: channel seizure pattern + marks
};

struct FilterData
{
    ~FilterData() {
	    if (xbuf)
		delete[] xbuf;
	    if (ybuf)
		delete[] ybuf;
	}

    inline void init(unsigned int len) {
	    xbuf = new float[len];
	    ybuf = new float[len];
	    ::memset(xbuf,0,len*sizeof(float));
	    ::memset(ybuf,0,len*sizeof(float));
	}

    float* xbuf;
    float* ybuf;
};


namespace TelEngine {

// BitBuffer
class BitBuffer
{
public:
    inline BitBuffer()
	: m_accumulator(8)
	{}
    inline const DataBlock& buffer() const
	{ return m_buffer; }
    inline void reset() {
	    m_buffer.clear();
	    m_accumulator.reset();
	}
    // Accumulate a bit. Add data bytes to buffer once accumulated
    inline bool accumulate(bool bit) {
#ifdef YMODEM_BUFFER_BITS
	    unsigned int res = m_accumulator.accumulate(bit);
	    if (res > 255)
		return false;
	    unsigned char c = (unsigned char)res;
	    DataBlock tmp(&c,1,false);
	    m_buffer += tmp;
	    tmp.clear(false);
#endif
	    return true;
	}
    // Operator used to accumulate a bit
    inline BitBuffer& operator+=(bool bit)
	{ accumulate(bit); return *this; }
    // Print bits to output
    void printBits(DebugEnabler* dbg, unsigned int linelen = 80);
private:
    DataBlock m_buffer;                  // The data byte buffer
    BitAccumulator m_accumulator;        // The bit accumulator
};

// The FSK sample filter
class FSKFilter
{
public:
    FSKFilter(int type);
    // Get the constants used by this filter
    inline FilterConst* constants()
	{ return m_const; }
    // Check if FSK modulation was already detected
    inline bool fskStarted() const
	{ return m_fskStarted > 0; }
    // Process data to demodulate a bit
    // Return negative if buffer ended, 0/1 if found a bit
    int getBit(short*& samples, unsigned int& len);
    // Filter data until a start bit is found (used to wait for FSK modulation to start)
    // Return true if a start bit is found, false if all buffer was processed with no result
    bool waitFSK(short*& samples, unsigned int& len);
    // Add a modulated bit to a destination buffer. Advance the buffer's index
    void addBit(short* samples, unsigned int& index, bool bit);
    // Add a modulated data byte to a destination buffer
    // dataBits must not be 0 or greater the then 8
    inline void addByte(short* samples, unsigned int& index,
	unsigned char value, unsigned char dataBits) {
	    for (unsigned int i = 0; i < dataBits; i++, value >>= 1)
		addBit(samples,index,(bool)(value & 0x01));
	}
    // Add a complete modulated byte to a destination buffer
    // The data is enclosed by start/stop bits
    inline void addByteFull(short* samples, unsigned int& index,
	unsigned char value, unsigned char dataBits) {
	    addBit(samples,index,false);
	    addByte(samples,index,value,dataBits);
	    addBit(samples,index,true);
	}
    // Modulate data to a buffer. Reset the destination's length
    // dataBits must not be 0 or greater then 8
    // Returns the current sine accumulator value
    double addBuffer(DataBlock& dest, const DataBlock& src, unsigned char dataBits, bool full);
private:
    // Apply mark, space and low band filter
    float filter(short*& samples, unsigned int& len);

    int m_fskStarted;                    // Flag indicating the FSK modulation start
    float m_lastFiltered;                // The last result of the filter
    float m_processed;                   // How much of a bit length was processed in (this is used for clock recovery)
    unsigned int m_index;                // Current index in buffer
    FilterConst* m_const;                // Constants used by this filter
    FilterData m_mark;
    FilterData m_space;
    FilterData m_lowband;
    // Data use to modulate signals
    double m_accSin;                     // Accumulate sine radians during modulation
    unsigned int m_bitSamples;           // Current index in the filter constant's bitSamples array
};

}


/**
 * Static module data
 */
static const char* s_libName = "libyatemodem";

FilterConst s_filterConst[FSKModem::TypeCount] = {
    FilterConst(FSKModem::ETSI)
};


/**
 * FilterConst
 */
// Build constants used by this filter
FilterConst::FilterConst(FSKModem::Type type)
{
    static float m[7] = {-5.6297236492e-02, 4.2915323820e-01, -1.2609358633e+00, 2.2399213250e+00,
		         -2.9928879142e+00, 2.5990173742e+00, 0.0000000000e+00};
    static float s[7] = {-5.6297236492e-02, -1.1421579050e-01, -4.8122536483e-01, -4.0121072432e-01,
		         -7.4834487567e-01, -6.9170822332e-01, 0.0000000000e+00};
    static float l[7] = {-7.8390522307e-03, 8.5209627801e-02, -4.0804129163e-01, 1.1157139955e+00,
		         -1.8767603680e+00, 1.8916395224e+00, 0.0000000000e+00};

    switch (type) {
	case FSKModem::ETSI:
	    break;
	default:
	    ::memset(this,0,sizeof(*this));
	    return;
    }

    // ETSI

    // Signal properties
    markFreq = 1200.0;
    spaceFreq = 2200.0;
    sampleRate = 8000.0;
    baudRate = 1200.0;

    // Mark/space coefficients for modulation/demodulation
    markCoef = 2 * M_PI * markFreq / sampleRate;
    spaceCoef = 2 * M_PI * spaceFreq / sampleRate;

    spb = 7;
    halfSpb = spb / 2;
    bitLen = sampleRate / baudRate;
    halfBitLen = bitLen / 2;
    markGain = 9.8539686961e-02;
    spaceGain = 9.8531161839e-02;
    lowbandGain = 3.1262119724e-03;
    mark = new float[spb+1];
    space = new float[spb+1];
    lowband = new float[spb+1];

    for (unsigned int i = 0; i < spb; i++) {
	mark[i] = m[i];
	space[i] = s[i];
	lowband[i] = l[i];
    }

    // Build the array of bit samples nedded to maintain the modulation timing
    bitSamplesLen = 3;
    bitSamples = new unsigned int[bitSamplesLen];
    bitSamples[0] = bitSamples[2] = 7;
    bitSamples[1] = 6;

    accSin = 0;
    // Build message header
    // ETSI channel seizure signal + Mark (stop bits) signal
    // 300 continuous bits of alternating 0 and 1 + 180 of 1 (mark) bits
    // 480 bits: 60 bytes. Byte 38: 01011111
    // This is the data header to be sent with ETSI messages
    unsigned char* hdr = new unsigned char[60];
    ::memset(hdr,0x55,37);
    hdr[37] = 0xf5;
    ::memset(&hdr[38],0xff,22);
    DataBlock src;
    FSKModem::addRaw(src,hdr,60);
    FSKFilter filter(type);
    // Keep the sine accumulator to be used when modulating data
    accSin = filter.addBuffer(header,src,8,false);

    Debug(s_libName,DebugInfo,"Initialized filter tables for type '%s' headerlen=%u",
	lookup(FSKModem::ETSI,FSKModem::s_typeName),header.length());
}

// Release memory
FilterConst::~FilterConst()
{
    if (!spb)
	return;
    delete[] mark;
    delete[] space;
    delete[] lowband;
    delete[] bitSamples;
}

// Calculate how many samples do we need to modulate n bits
unsigned int FilterConst::bufLen(unsigned int n)
{
    if (!bitSamples)
	return 0;
    unsigned int count = 0;
    // Each entry in bitSamples contain the number of samples nedded for current bit
    for (unsigned int idx = 0; n; n--)
	count += timingSamples(idx);
    return count;
}

/**
 * BitBuffer
 */
void BitBuffer::printBits(DebugEnabler* dbg, unsigned int linelen)
{
#ifdef YMODEM_BUFFER_BITS
    if ((dbg && !dbg->debugAt(DebugAll)) || (!dbg && !TelEngine::debugAt(DebugAll)))
	return;

    ObjList lines;
    String* s = new String;
    unsigned char* p = (unsigned char*)m_buffer.data();
    for (unsigned int i = 0; i < m_buffer.length(); i++, p++) {
	for (unsigned char pos = 0; pos < 8; pos++) {
	    char c = (*p & (1 << pos)) ? '1' : '0';
	    *s += c;
	}
	if (s->length() == linelen) {
	    lines.append(s);
	    s = new String;
	}
    }
    if (s->length())
	lines.append(s);
    else
	TelEngine::destruct(s);
    String tmp;
    for (ObjList* o = lines.skipNull(); o; o = o->skipNext())
	tmp << "\r\n" << *(static_cast<String*>(o->get()));
    Debug(dbg,DebugAll,"Decoded %u bits:%s",m_buffer.length()*8,tmp.c_str());
#endif
}

/**
 * FSKFilter
 */
FSKFilter::FSKFilter(int type)
    : m_fskStarted(-1),
    m_lastFiltered(0),
    m_processed(0),
    m_accSin(0),
    m_bitSamples(0)
{
    switch (type) {
	case FSKModem::ETSI:
	    break;
	default:
	    return;
    }

    m_index = 0;
    m_const = s_filterConst + type;
    // Update the sine accumulator from constants (current value after created the header)
    m_accSin = m_const->accSin;
    m_mark.init(1+m_const->spb);
    m_space.init(1+m_const->spb);
    m_lowband.init(1+m_const->spb);
}

// Process data to demodulate a bit
// Return negative if buffer ended, 0/1 if found a bit
inline int FSKFilter::getBit(short*& samples, unsigned int& len)
{
    float ds = m_const->bitLen / 32.;

    bool transition = false;
    while (len) {
	float filtered = filter(samples,len);
	// Check if this a bit transition
	if (filtered * m_lastFiltered < 0 && !transition) {
	    if (m_processed < m_const->halfBitLen)
		m_processed += ds;
	    else
		m_processed -= ds;
	    transition = true;
	}
	m_lastFiltered = filtered;
	m_processed += 1.;
	// Processed a bit: adjust clock (bit length) and return the result
	if (m_processed > m_const->bitLen) {
	    m_processed -= m_const->bitLen;
	    return filtered > 0 ? 1 : 0;;
	}
    }
    return -1;
}

// Filter data until a start bit is found (used to wait for FSK modulation to start)
// Return true if a start bit is found, false if all buffer was processed with no result
inline bool FSKFilter::waitFSK(short*& samples, unsigned int& len)
{
    if (fskStarted())
	return true;
    if (!len)
	return false;

    float tmp = 0;

    if (m_fskStarted == -1) {
	while (tmp >= -0.5) {
	    if (!len)
		return false;
	    tmp = filter(samples,len);
	}
	m_fskStarted = 0;
    }

    // Wait for 0.5 bits before starting the demodulation
    tmp = 1;
    while (tmp > 0) {
	if (len < m_const->halfSpb)
	    return false;
	for(unsigned int i = m_const->halfSpb; i; i--)
	    tmp = filter(samples,len);
    }

    m_fskStarted = 1;
    return true;
}

// Add a modulated bit to a destination buffer
inline void FSKFilter::addBit(short* samples, unsigned int& index, bool bit)
{
    // Get the number of samples nedded for this bit and advance the index
    unsigned int n = m_const->timingSamples(m_bitSamples);
    // Build and store the modulated samples
    if (bit)
	for(; n; n--) {
	    m_accSin += m_const->markCoef;
	    samples[index++] = (short)(MARK_AMPLITUDE * sin(m_accSin));
	}
    else
	for(; n; n--) {
	    m_accSin += m_const->spaceCoef;
	    samples[index++] = (short)(SPACE_AMPLITUDE * sin(m_accSin));
	}
}

// Modulate data to a buffer
// dataBits must not be 0 or greater then 8
// full=true: add data bytes (start bit + data + stop bit)
double FSKFilter::addBuffer(DataBlock& dest, const DataBlock& src,
	unsigned char dataBits, bool full)
{
    // Calculate the destination length. Add 2 more bits if full
    if (m_const)
	if (full)
	    dest.assign(0,m_const->bufLen(src.length() * (dataBits + 2)) * sizeof(short));
	else
	    dest.assign(0,m_const->bufLen(src.length() * dataBits) * sizeof(short));
    else
	dest.clear();
    if (!dest.length())
	return m_accSin;

    // Build modulated buffer
    unsigned char* srcData = (unsigned char*)(src.data());
    short* destData = (short*)(dest.data());
    unsigned int index = 0;
    if (full)
	for (unsigned int i = 0; i < src.length(); i++)
	    addByteFull(destData,index,srcData[i],dataBits);
    else
	for (unsigned int i = 0; i < src.length(); i++)
	    addByte(destData,index,srcData[i],dataBits);
    return m_accSin;
}

// Apply mark/space and low band filter
inline float FSKFilter::filter(short*& samples, unsigned int& len)
{
#define SPB m_const->spb
#define MOD(val) ((val) & SPB)

    short sample = *samples++;
    len--;

    // Mark filter
    m_mark.xbuf[MOD(m_index+6)] = sample * m_const->markGain;
    float mark = m_mark.xbuf[MOD(m_index+6)] - m_mark.xbuf[m_index]
	+ 3 * (m_mark.xbuf[MOD(m_index+2)] - m_mark.xbuf[MOD(m_index+4)]);
    for (unsigned int i = 0; i < 6; i++)
	mark += m_mark.ybuf[MOD(m_index+i)] * m_const->mark[i];
    m_mark.ybuf[MOD(m_index+6)] = mark;

    // Space filter
    m_space.xbuf[MOD(m_index+6)] = sample * m_const->spaceGain;
    float space = m_space.xbuf[MOD(m_index+6)] - m_space.xbuf[m_index]
	+ 3 * (m_space.xbuf[MOD(m_index+2)] - m_space.xbuf[MOD(m_index+4)]);
    for (unsigned int i = 0; i < 6; i++)
	space += m_space.ybuf[MOD(m_index+i)] * m_const->space[i];
    m_space.ybuf[MOD(m_index+6)] = space;

    // Low band filter
    float result = mark * mark - space * space;
    m_lowband.xbuf[MOD(m_index+6)] = result * m_const->lowbandGain;
    result =    (m_lowband.xbuf[m_index]        + m_lowband.xbuf[MOD(m_index+6)])
	 + 6  * (m_lowband.xbuf[MOD(m_index+1)] + m_lowband.xbuf[MOD(m_index+5)])
	 + 15 * (m_lowband.xbuf[MOD(m_index+2)] + m_lowband.xbuf[MOD(m_index+4)])
	 + 20 *  m_lowband.xbuf[MOD(m_index+3)];
    for (unsigned int i = 0; i < 6; i++)
	result += m_lowband.ybuf[MOD(m_index+i)] * m_const->lowband[i];
    m_lowband.ybuf[MOD(m_index+6)] = result;

    // Increase index
    m_index = MOD(m_index+1);

#undef SPB
#undef MOD

    return result;
}


/**
 * FSKModem
 */
TokenDict FSKModem::s_typeName[] = {
    {"etsi", FSKModem::ETSI},
    {0,0}
};

FSKModem::FSKModem(const NamedList& params, UART* uart)
    : m_type(ETSI),
    m_terminated(false),
    m_filter(0),
    m_uart(uart),
    m_bits(0)
{
    if (!m_uart) {
	Debug(DebugWarn,"Request to create FSK modem without UART");
	m_terminated = true;
	return;
    }

    const char* typeName = params.getValue("modemtype");
    if (typeName && *typeName)
	m_type = lookup(typeName,s_typeName);

    switch (m_type) {
	case ETSI:
	    break;
	default:
	    Debug(m_uart,DebugWarn,"Unknown modem type='%s' [%p]",typeName,m_uart);
	    m_terminated = true;
	    return;
    }

#ifdef YMODEM_BUFFER_BITS
    if (params.getBoolValue("bufferbits",false))
	m_bits = new BitBuffer;
#endif

    reset();
    XDebug(m_uart,DebugAll,"Modem created type='%s' [%p]",lookup(m_type,s_typeName),this);
}

FSKModem::~FSKModem()
{
    if (m_filter)
	delete m_filter;
    if (m_bits) {
	m_bits->printBits(m_uart);
	delete m_bits;
    }
    XDebug(m_uart,DebugAll,"Modem destroyed [%p]",this);
}

// Reset state. Clear buffer
void FSKModem::reset()
{
    m_terminated = false;
    m_buffer.clear();
    if (m_filter)
	delete m_filter;
    m_filter = 0;
    if (m_type < TypeCount)
	m_filter = new FSKFilter(m_type);
    if (m_bits)
	m_bits->reset();
}

// Data processor. Feed the collector
// Return false to stop processing
bool FSKModem::demodulate(const DataBlock& data)
{
    if (m_terminated)
	return false;
    if (!data.length())
	return true;

    // Prepare buffer to process
    void* buffer = 0;                    // Original buffer
    unsigned int len = 0;                // Data length in bytes
    if (m_buffer.length()) {
	m_buffer += data;
	buffer = m_buffer.data();
	len = m_buffer.length();
    }
    else {
	buffer = data.data();
	len = data.length();
    }
    short* samples = (short*)buffer;          // Data to process
    unsigned int count = len / sizeof(short); // The number of available samples

    XDebug(m_uart,DebugAll,"Demodulating %u bytes [%p]",len,m_uart);

    // Wait at least 6 samples to process
    while (count > 6) {
	// Check if FSK modulation was detected
	if (!m_filter->fskStarted()) {
	    if (!m_filter->waitFSK(samples,count))
		break;
	    DDebug(m_uart,DebugInfo,"FSK modulation started [%p]",m_uart);
	    m_terminated = !m_uart->fskStarted();
	    if (m_terminated)
		break;
#ifdef YMODEM_BUFFER_BITS
	    if (m_bits)
		m_bits->accumulate(false);
#endif
	    m_terminated = !m_uart->recvBit(false);
	}

	// FSK started: get bits and send them to the UART
	for (int bit = 1; bit >= 0 && !m_terminated; ) {
	    bit = m_filter->getBit(samples,count);
	    if (bit >= 0) {
#ifdef YMODEM_BUFFER_BITS
		if (m_bits)
		    m_bits->accumulate(bit != 0);
#endif
		m_terminated = !m_uart->recvBit(bit != 0);
	    }
	}
	break;
    }
    // Keep the unprocessed bytes
    unsigned int rest = count * sizeof(short) + len % sizeof(short);

    if (rest) {
	DataBlock tmp;
	if (m_buffer.data()) {
	   tmp.assign(buffer,len,false);
	   m_buffer.clear(false);
	}
	m_buffer.assign(samples,rest);
    }
    else
	m_buffer.clear();

    return !m_terminated;
}

// Create a buffer containing the modulated representation of a message
void FSKModem::modulate(DataBlock& dest, const DataBlock& data)
{
#ifdef DEBUG
    String tmp;
    tmp.hexify(data.data(),data.length(),' ');
    Debug(m_uart,DebugAll,"Modulating '%s' [%p]",tmp.safe(),m_uart);
#endif

    if (!(data.length() && m_filter && m_filter->constants()))
	return;

    DataBlock tmpData;
    m_filter->addBuffer(tmpData,data,m_uart->accumulator().dataBits(),true);
    dest += m_filter->constants()->header;
    dest += tmpData;
    // Build pattern after
    unsigned int nbits = (unsigned int)(m_filter->constants()->baudRate / 1000 * PATTERN_AFTER);
    DataBlock p(0,(nbits+7)/8);
    DataBlock tmpAfter;
    m_filter->addBuffer(tmpAfter,p,m_uart->accumulator().dataBits(),false);
    dest += tmpAfter;
    DDebug(m_uart,DebugAll,"Modulated header=%u data=%u pattern=%u [%p]",
	m_filter->constants()->header.length(),
	tmpData.length(),tmpAfter.length(),m_uart);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
