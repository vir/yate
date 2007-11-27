/**
 * modem.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Modem
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
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

#include "yatemodem.h"

#include <string.h>
#include <math.h>

using namespace TelEngine;

// Uncomment this to view the bits decoded by the modem
#define YMODEM_BUFFER_BITS

class Complex
{
public:
    inline Complex()
	{ set(0,0); }
    inline Complex(float r, float i)
	{ set (r,i); }
    inline void set(float r, float i)
	{ real = r; imag = i; }
    inline Complex& operator*=(float value) {
	    set(real * value,imag * value);
	    return *this;
	}
    inline Complex& operator*=(Complex& op) {
	    set(real * op.real - imag * op.imag,real * op.imag + imag * op.real);
	    return *this;
	}
    inline Complex& operator=(Complex& op) {
	    set(op.real,op.imag);
	    return *this;
	}

    float real;
    float imag;
};

// Constant values used by the FSK filter to modulate/demodulate data
class FilterConst
{
public:
    // Build constants used by this filter
    FilterConst(FSKModem::Type type);
    // Release memory
    ~FilterConst();
    // Calculate how many samples do we need to modulate nbits
    unsigned int calculateSamples(unsigned int nbits)
	{ return ((unsigned int)bitLen + 1) * nbits; }

    float markFreq;                      // Mark frequency
    float spaceFreq;                     // Space frequency
    float sampleRate;                    // Sampling rate
    float baudRate;                      // Transmission baud rate
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
    Complex bit[2];                      // Bit representation
    DataBlock header;                    // Message header if any (e.g. ETSI: channel seizure pattern + marks)
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
    // Add a modulated bit to a destination buffer
    void addBit(short* samples, unsigned int& index, unsigned int bit);
    // Add a modulated byte to a destination buffer
    void addByte(short* samples, unsigned int& index, unsigned char value, unsigned char dataBits);
private:
    // Apply mark, space and low band filter
    float filter(short*& samples, unsigned int& len);
    // Modulate a bit
    float modulate(unsigned int bit);

    int m_fskStarted;                    // Flag indicating the FSK modulation start
    float m_lastFiltered;                // The last result of the filter
    float m_processed;                   // How much of a bit length was processed in (this is used for clock recovery)
    unsigned int m_index;                // Current index in buffer
    FilterConst* m_const;                // Constants used by this filter
    FilterData m_mark;
    FilterData m_space;
    FilterData m_lowband;
    // Data use to modulate signals
    Complex m_crt;
};

}


/**
 * Static module data
 */
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
	    return;
    }

    // ETSI
    markFreq = 1200.0;
    spaceFreq = 2200.0;
    sampleRate = 8000.0;
    baudRate = 1200.0;
    spb = 7;
    halfSpb = spb / 2;
    bitLen = sampleRate / baudRate;
    halfBitLen = bitLen / 2;
    markGain = 9.8539686961e-02;
    spaceGain = 9.8531161839e-02;
    lowbandGain = 3.1262119724e-03;
    mark = new float[spb];
    space = new float[spb];
    lowband = new float[spb];

    for (unsigned int i = 0; i < spb; i++) {
	mark[i] = m[i];
	space[i] = s[i];
	lowband[i] = l[i];
    }

    float tmp = 2.0 * M_PI / sampleRate;
    bit[0].set(cos(tmp * spaceFreq),sin(tmp * spaceFreq));
    bit[1].set(cos(tmp * markFreq),sin(tmp * markFreq));

    // Build header
    // ETSI channel seizure signal + Mark (stop bits) signal
    // 300 continuous bits of alternating 0 and 1 + 180 of 0 (mark/start) bits
    // 480 bits: 60 bytes. Byte 38: 01010000
    // This is the data header to be sent with ETSI messages
    unsigned char* hdr = new unsigned char[60];
    ::memset(hdr,0x55,37);
    ::memset(&hdr[37],0x50,1);
    ::memset(&hdr[38],0x00,22);
    DataBlock src(hdr,60,false);

    unsigned int n = calculateSamples(src.length() * 8);
    header.assign(0,n * sizeof(short));
    unsigned char* srcData = (unsigned char*)(src.data());
    short* buf = (short*)(header.data());
    unsigned int index = 0;
    FSKFilter* filter = new FSKFilter(type);
    for (unsigned int i = 0; i < src.length(); i++)
	filter->addByte(buf,index,srcData[i],8);
    delete filter;

    src.clear(false);

    Output("FSK: initialized filter tables for type '%s' header %u samples",
	lookup(FSKModem::ETSI,FSKModem::s_typeName),n);
}

// Release memory
FilterConst::~FilterConst()
{
    if (!spb)
	return;
    delete[] mark;
    delete[] space;
    delete[] lowband;
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
    m_processed(0)
{
    switch (type) {
	case FSKModem::ETSI:
	    break;
	default:
	    return;
    }

    m_index = 0;
    m_const = s_filterConst + type;
    m_mark.init(m_const->spb);
    m_space.init(m_const->spb);
    m_lowband.init(m_const->spb);
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
inline void FSKFilter::addBit(short* samples, unsigned int& index, unsigned int bit)
{
    for (; m_processed < m_const->bitLen; m_processed += 1.)
	samples[index++] = (short)modulate(bit);
    m_processed -= m_const->bitLen;
}

// Add a modulated byte to a destination buffer
inline void FSKFilter::addByte(short* samples, unsigned int& index, unsigned char value, unsigned char dataBits)
{
    for (unsigned int i = 0; i < dataBits; i++, value >>= 1)
	addBit(samples,index,value & 0x01);
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
    result =    (m_lowband.xbuf[m_index]    +     m_lowband.xbuf[MOD(m_index+6)])
	 + 6  * (m_lowband.xbuf[MOD(m_index+1)] + m_lowband.xbuf[MOD(m_index+5)])
	 + 15 * (m_lowband.xbuf[MOD(m_index+2)] + m_lowband.xbuf[MOD(m_index+4)])
	 + 20 *  m_lowband.xbuf[MOD(m_index+3)];
    for (unsigned int i = 0; i < 6; i++)
	result += m_lowband.ybuf[MOD(m_index+i)] * m_const->lowband[i];
    m_lowband.ybuf[MOD(m_index+6)] = result;

    // Increase index
    m_index = MOD(++m_index);

#undef SPB
#undef MOD

    return result;
}

// Modulate a bit
inline float FSKFilter::modulate(unsigned int bit)
{
    m_crt *= m_const->bit[bit];
    m_crt *= 2.0 - (m_crt.real * m_crt.real + m_crt.imag * m_crt.imag);
    return rint(8192.0 * m_crt.real);
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
	    m_bits->accumulate(false);
	    m_terminated = !m_uart->recvBit(false);
	}

	// FSK started: get bits and send them to the UART
	for (int bit = 1; bit >= 0 && !m_terminated; ) {
	    bit = m_filter->getBit(samples,count);
	    if (bit >= 0) {
		m_bits->accumulate(bit);
		m_terminated = !m_uart->recvBit(bit);
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
void FSKModem::modulate(DataBlock& dest, const DataBlock& data, const DataBlock* header,
    bool useDefHeader)
{

#ifdef DEBUG
    String tmp;
    tmp.hexify(data.data(),data.length(),' ');
    Debug(m_uart,DebugAll,"modulating '%s' [%p]",tmp.safe(),m_uart);
#endif

    // Calculate the length of the needded buffer
    unsigned int bits = 0;
    if (header)
	bits = header->length() * 8;
    else if (useDefHeader)
	bits = m_filter->constants()->header.length() / sizeof(short);
    // Each data byte needs 2 more bits: start/stop
    // TODO: Fix it to use parity
    bits += data.length() * (m_uart->accumulator().dataBits() + 2);
    unsigned int n = m_filter->constants()->calculateSamples(bits);

    // Create buffer
    dest.assign(0,n * sizeof(short));
    DDebug(m_uart,DebugAll,"Created %u samples buffer to be sent [%p]",n,m_uart);

    short* buf = (short*)(dest.data());
    unsigned int index = 0;
    // Add header
    if (header) {
	DDebug(m_uart,DebugAll,"Adding %u header [%p]",header->length(),m_uart);
	unsigned char* src = (unsigned char*)(header->data());
	for (unsigned int i = 0; i < header->length();)
	    m_filter->addByte(buf,index,src[i],8);
    }
    else if (useDefHeader) {
	short* h = (short*)(m_filter->constants()->header.data());
	unsigned int len = m_filter->constants()->header.length() / sizeof(short);
	DDebug(m_uart,DebugAll,"Adding %u samples of precalculated header [%p]",len,m_uart);
	for (; index < len; index++)
	    buf[index] = h[index];
    }

    unsigned char* src = (unsigned char*)(data.data());
    for (unsigned int i = 0; i < data.length(); i++) {
	m_filter->addBit(buf,index,0);
	m_filter->addByte(buf,index,src[i],m_uart->accumulator().dataBits());
	m_filter->addBit(buf,index,1);
	if (index > dest.length()/sizeof(short))
	    DDebug(m_uart,DebugFail,"Index %u past buffer end %lu [%p]",index,dest.length()/sizeof(short),m_uart);
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
