/**
 * radio.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Generic radio interface
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2015 Null Team
 * Copyright (C) 2015 LEGBA Inc
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

#include <yateradio.h>
#include <string.h>

//#define DEBUG_RADIO_READ

#ifdef DEBUG_RADIO_READ
#define DebugRadioRead(args...) Debug(this,DebugAll,args)
#else
#ifdef _WINDOWS
#define DebugRadioRead do { break; } while
#else
#define DebugRadioRead(arg...)
#endif
#endif // DEBUG_RADIO_READ


using namespace TelEngine;

#define MAKE_NAME(x) {#x, RadioInterface::x}
static const TokenDict s_errorName[] = {
    MAKE_NAME(HardwareIOError),
    MAKE_NAME(NotInitialized),
    MAKE_NAME(NotSupported),
    MAKE_NAME(NotCalibrated),
    MAKE_NAME(TooEarly),
    MAKE_NAME(TooLate),
    MAKE_NAME(OutOfRange),
    MAKE_NAME(NotExact),
    MAKE_NAME(DataLost),
    MAKE_NAME(Saturation),
    MAKE_NAME(RFHardwareFail),
    MAKE_NAME(RFHardwareChange),
    MAKE_NAME(EnvironmentalFault),
    MAKE_NAME(InvalidPort),
    MAKE_NAME(Pending),
    MAKE_NAME(Cancelled),
    MAKE_NAME(Failure),
    MAKE_NAME(NoError),
    {0,0}
};
#undef MAKE_NAME

RadioCapability::RadioCapability()
{
    ::memset(this,0,sizeof(*this));
}


// Calculate the length of a sample in elements
static inline unsigned int sampleLen()
{
    return 2;
}

// Calculate the number of 'float' elements in given number of samples
static inline unsigned int samples2floats(unsigned int nSamples)
{
    return nSamples * sampleLen();
}

// Calculate the length in bytes of a given number of samples
static inline unsigned int samples2bytes(unsigned int nSamples)
{
    return samples2floats(nSamples) * sizeof(float);
}

// Advance float buffer by samples
static inline float* advanceSamples(float* buf, unsigned int nSamples)
{
    return buf + samples2floats(nSamples);
}

static inline void resetSamples(float* buf, unsigned int nSamples)
{
    ::memset(buf,0,samples2bytes(nSamples));
}

static inline void copySamples(float* dest, unsigned int destOffs,
    float* src, unsigned int srcOffs, unsigned int nSamples)
{
    ::memcpy(advanceSamples(dest,destOffs),advanceSamples(src,srcOffs),
	samples2bytes(nSamples));
}

// Move samples from buf to offset (in the same buffer)
// Copy data in the backward direction to avoid overlap
static inline void moveSamples(float* buf, unsigned int offs, unsigned int nSamples)
{
    if (!nSamples)
	return;
    unsigned int cp = samples2floats(nSamples);
    float* src = buf + cp - 1;
    float* dest = advanceSamples(buf,offs) + cp - 1;
    for (float* last = src - cp; src != last; --dest, --src)
	*dest = *src;
    resetSamples(buf,offs);
}


String& RadioReadBufs::dump(String& buf)
{
    return buf.printf("\r\n-----\r\ncrt:\t%u(%u)\t%u\t(%p)\r\naux:\t%u(%u)\t%u\t(%p)"
	"\r\nextra:\t\t%u\t(%p)\r\n-----",
	valid(crt),crt.valid,crt.offs,crt.samples,
	valid(aux),aux.valid,aux.offs,aux.samples,
	extra.offs,extra.samples);
}


// NOTE: This method assumes a single port is used
// E.g.: a sample is an I/Q pair
// If multiple ports are handled the sampleLen() function should handle the number of ports
unsigned int RadioInterface::read(uint64_t& when, RadioReadBufs& bufs,
    unsigned int& skippedBufs)
{
    String tmp;
    DebugRadioRead(">>> read: ts=" FMT64U " buf_samples=%u [%p]%s",
	when,bufs.bufSamples(),this,bufs.dump(tmp).c_str());
    // Switch buffers
    if (bufs.full(bufs.crt) && !bufs.aux.offs) {
	bufs.crt.reset();
	DebugRadioRead("read reset crt [%p]",this);
    }
    else if ((!bufs.crt.offs && bufs.aux.offs) || bufs.full(bufs.crt)) {
	bool emptyCrt = (bufs.crt.offs == 0);
	RadioBufDesc buf = bufs.crt;
	bufs.crt = bufs.aux;
	if (emptyCrt || !bufs.extra.offs) {
	    bufs.aux.samples = buf.samples;
	    bufs.aux.reset();
	}
	else {
	    bufs.aux = bufs.extra;
	    bufs.extra.samples = buf.samples;
	    bufs.extra.reset();
	}
	// Adjust timestamp with data already in buffer
	when += bufs.crt.offs;
	if (bufs.full(bufs.crt)) {
	    if (!bufs.valid(bufs.crt)) {
		skippedBufs = 1;
		bufs.crt.reset();
	    }
	    DebugRadioRead("<<< read ts=" FMT64U " (crt full) [%p]%s",
		when,this,bufs.dump(tmp).c_str());
	    return 0;
	}
	DebugRadioRead("read moved aux to crt [%p]%s",
	    this,bufs.dump(tmp).c_str());
    }
    skippedBufs = 0;
    unsigned int avail = bufs.bufSamples() - bufs.crt.offs;
    unsigned int rdSamples = avail;
    uint64_t ts = when;
    float* rdBuf = advanceSamples(bufs.crt.samples,bufs.crt.offs);
    unsigned int code = recv(ts,rdBuf,rdSamples);
    DebugRadioRead("read: code=%u read=%u/%u [%p]",code,rdSamples,avail,this);
    if (code || !rdSamples)
	return code;
    if (when == ts) {
	when += rdSamples;
	bufs.crt.offs += rdSamples;
	bufs.crt.valid += rdSamples;
	DebugRadioRead("<<< read ts=" FMT64U " OK [%p]%s",
	    when,this,bufs.dump(tmp).c_str());
	return 0;
    }
    // This should never happen !!!
    if (ts < when) {
	Debug(this,DebugFail,
	    "Read timestamp in the past by " FMT64U " at " FMT64U " [%p]",
	    (when - ts),when,this);
	return TooEarly;
    }
    // Timestamp is in the future
    uint64_t diff = ts - when;
    if (when)
	Debug(this,DebugNote,
	    "Read timestamp in the future by " FMT64U " at " FMT64U " [%p]",
	    diff,when,this);
    if (diff <= avail) {
	// The timestamp difference is inside available space
	// Read samples + NULLs won't exceed current + auxiliary buffer
	bufs.extra.reset();
	// We may copy some data
	unsigned int cpSamples = avail - diff;
	if (cpSamples > rdSamples)
	    cpSamples = rdSamples;
	// Copy data to auxiliary buffer if valid
	// Do nothing if invalid: we will ignore it at next read
	bufs.aux.reset(rdSamples - cpSamples);
	if (bufs.aux.offs && bufs.valid(bufs.aux))
	    copySamples(bufs.aux.samples,0,rdBuf,cpSamples,bufs.aux.offs);
	// Adjust available (used) space: copy samples + NULLs
	avail = diff + cpSamples;
	bufs.crt.reset(bufs.crt.offs + avail,bufs.crt.valid + cpSamples);
	if (bufs.valid(bufs.crt)) {
	    if (cpSamples)
		moveSamples(rdBuf,diff,cpSamples);
	    else
		resetSamples(rdBuf,avail);
	}
	else if (bufs.full(bufs.crt)) {
	    // Not enough valid samples in full buffer: skip it
	    skippedBufs++;
	    bufs.crt.reset();
	}
	// Adjust timestamp
	when += avail;
    }
    else {
	// Timestamp is outside buffer
	uint64_t delta = diff - avail;
	skippedBufs = delta / bufs.bufSamples();
	if (skippedBufs)
	    when += skippedBufs * bufs.bufSamples();
	// Advance current timestamp
	// Reset data in current buffer or skip it
	when += avail;
	bufs.crt.offs = bufs.bufSamples();
	if (bufs.valid(bufs.crt))
	    resetSamples(rdBuf,avail);
	else {
	    // Not enough valid samples in full buffer: skip it
	    skippedBufs++;
	    bufs.crt.reset();
	}
	// Setup the auxiliary buffers
	// Set data if valid
	// Do nothing with data if invalid: we will ignore them on subsequent reads
	unsigned int nullSamples = delta % bufs.bufSamples();
	unsigned int len = nullSamples + rdSamples;
	if (len <= bufs.bufSamples()) {
	    bufs.aux.reset(len,rdSamples);
	    bufs.extra.reset();
	}
	else {
	    bufs.aux.reset(bufs.bufSamples(),bufs.bufSamples() - nullSamples);
	    bufs.extra.reset(rdSamples - bufs.aux.valid);
	}
	if (bufs.valid(bufs.aux)) {
	    resetSamples(bufs.aux.samples,nullSamples);
	    copySamples(bufs.aux.samples,nullSamples,rdBuf,0,bufs.aux.valid);
	}
	if (bufs.extra.offs && bufs.valid(bufs.extra))
	    copySamples(bufs.extra.samples,0,rdBuf,bufs.aux.valid,bufs.extra.valid);
    }
    DebugRadioRead("<<< read (ts in future): ts=" FMT64U " skipped_bufs=%u [%p]%s",
	when,skippedBufs,this,bufs.dump(tmp).c_str());
    return 0;
}

const String& RadioInterface::toString() const
{
    return m_name;
}

const TokenDict* RadioInterface::errorNameDict()
{
    return s_errorName;
}

/* vi: set ts=8 sw=4 sts=4 noet enc=utf-8: */
