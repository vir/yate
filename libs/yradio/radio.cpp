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

#ifndef htobe32
#include <byteswap.h>

#ifdef LITTLE_ENDIAN

#define htobe16(x) __bswap_16 (x)
#define htole16(x) (x)
#define be16toh(x) __bswap_16 (x)
#define le16toh(x) (x)
#define htobe32(x) __bswap_32 (x)
#define htole32(x) (x)
#define be32toh(x) __bswap_32 (x)
#define le32toh(x) (x)
#define htobe64(x) __bswap_64 (x)
#define htole64(x) (x)
#define be64toh(x) __bswap_64 (x)
#define le64toh(x) (x)

#else

#define htobe16(x) (x)
#define htole16(x) __bswap_16 (x)
#define be16toh(x) (x)
#define le16toh(x) __bswap_16 (x)
#define htobe32(x) (x)
#define htole32(x) __bswap_32 (x)
#define be32toh(x) (x)
#define le32toh(x) __bswap_32 (x)
#define htobe64(x) (x)
#define htole64(x) __bswap_64 (x)
#define be64toh(x) (x)
#define le64toh(x) __bswap_64 (x)

#endif

#endif // htobe32


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
    MAKE_NAME(Timeout),
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


//
// RadioDataFile
//
RadioDataFile::RadioDataFile(const char* name, bool dropOnError)
    : String(name),
    m_littleEndian(true),
    m_dropOnError(dropOnError),
    m_chunkSize(0),
    m_writeBuf(256)
{
    m_littleEndian = m_header.m_littleEndian;
}

RadioDataFile::~RadioDataFile()
{
    terminate();
}

static inline unsigned int elementSize(const RadioDataDesc& data)
{
    switch (data.m_elementType) {
	case RadioDataDesc::Float:
	    return sizeof(float);
	case RadioDataDesc::Int16:
	    return sizeof(int16_t);
    }
    return 0;
}

// Open a file for read/write. Terminate current data dump if any.
// For file write: read the file header
bool RadioDataFile::open(const char* fileName, const RadioDataDesc* data,
    DebugEnabler* dbg, int* error)
{
    terminate(dbg);
    if (TelEngine::null(fileName))
	return false;
    const char* fileOper = 0;
    String hdrError;
    while (true) {
	m_writeBuf.resize(sizeof(m_header));
	uint8_t* d = m_writeBuf.data(0);
	// Write
	if (data) {
	    fileOper = "write";
	    if ((elementSize(*data) * data->m_sampleLen * data->m_ports) == 0) {
		hdrError = "Invalid header data";
		break;
	    }
	    m_header = *data;
	    if (!m_file.openPath(fileName,true,false,true,false,true,true)) {
		fileOper = "open";
		break;
	    }
	    ::memcpy(d,m_header.m_signature,sizeof(m_header.m_signature));
	    d += sizeof(m_header.m_signature);
	    *d++ = m_header.m_elementType;
	    *d++ = m_header.m_sampleLen;
	    *d++ = m_header.m_ports;
	    *d++ = m_header.m_tsType;
	    *d++ = m_header.m_littleEndian ? 0 : 1;
	    int wr = m_file.writeData(m_writeBuf.data(),m_writeBuf.length());
	    if (wr == (int)m_writeBuf.length())
		fileOper = 0;
	    else if (wr >= 0)
		hdrError = "Incomplete header write";
	    break;
	}
	// Read
	fileOper = "open";
	if (!m_file.openPath(fileName))
	    break;
	fileOper = "read";
	int rd = m_file.readData(d,m_writeBuf.length());
	if (rd == (int)m_writeBuf.length()) {
	    ::memcpy(m_header.m_signature,d,sizeof(m_header.m_signature));
	    d += sizeof(m_header.m_signature);
	    m_header.m_elementType = *d++;
	    m_header.m_sampleLen = *d++;
	    m_header.m_ports = *d++;
	    m_header.m_tsType = *d++;
	    if (*d == 0 || *d == 1) {
		m_header.m_littleEndian = (*d == 0);
		fileOper = 0;
		break;
	    }
	    hdrError = "Invalid endiannes value";
	}
	else if (rd >= 0)
	    hdrError = "Invalid file size";
	break;
    }
    if (!fileOper) {
	m_chunkSize = elementSize(m_header) * m_header.m_sampleLen * m_header.m_ports;
	if (dbg)
	    Debug(dbg,DebugAll,"RadioDataFile[%s] opened file '%s' [%p]",
		c_str(),fileName,this);
	return true;
    }
    if (error)
	*error = hdrError ? 0 : m_file.error();
    if (!hdrError) {
	hdrError = m_file.error();
	String tmp;
	Thread::errorString(tmp,m_file.error());
	hdrError.append(tmp," - ");
    }
    Debug(dbg,DebugNote,"RadioDataFile[%s] file '%s' %s %s failed: %s [%p]",
	c_str(),fileName,(data ? "OUT" : "IN"),fileOper,hdrError.safe(),this);
    terminate();
    return false;
}

bool RadioDataFile::write(uint64_t ts, const void* buf, uint32_t len,
    DebugEnabler* dbg, int* error)
{
    int e = 0;
    if (error)
	*error = 0;
    else
	error = &e;
    if (!(buf && len))
	return false;
    if (m_chunkSize && (len % m_chunkSize) != 0)
	return ioError(true,dbg,0,"Invalid buffer length");
    m_writeBuf.resize(len + 12);
    uint8_t* d = m_writeBuf.data(0);
    if (m_littleEndian == m_header.m_littleEndian) {
	*(uint32_t*)d = len;
	*(uint64_t*)(d + 4) = ts;
    }
    else if (m_littleEndian) {
	*(uint32_t*)d = htobe32(len);
	*(uint64_t*)(d + 4) = htobe64(ts);
    }
    else {
	*(uint32_t*)d = htole32(len);
	*(uint64_t*)(d + 4) = htole64(ts);
    }
    ::memcpy(d + 12,buf,len);
    int wr = m_file.writeData(m_writeBuf.data(),m_writeBuf.length());
    if (wr != (int)m_writeBuf.length())
	return ioError(true,dbg,error,wr >= 0 ? "Incomplete write" : 0);
#ifdef XDEBUG
    String sHdr, s;
    sHdr.hexify(d,12,' ');
    s.hexify(d + 12,wr - 12,' ');
    Debug(dbg,DebugAll,"RadioDataFile[%s] wrote %d hdr=%s data=%s [%p]",
	c_str(),wr,sHdr.c_str(),s.c_str(),this);
#endif
    return true;
}

// Read a record from file
bool RadioDataFile::read(uint64_t& ts, DataBlock& buffer, DebugEnabler* dbg, int* error)
{
    int e = 0;
    if (error)
	*error = 0;
    else
	error = &e;
    uint8_t hdr[12];
    int rd = m_file.readData(hdr,sizeof(hdr));
    // EOF ?
    if (rd == 0) {
	buffer.resize(0);
	return true;
    }
    if (rd != sizeof(hdr))
	return ioError(false,dbg,error,rd > 0 ? "Incomplete read (invalid size?)" : 0);
    uint32_t len = 0;
    uint32_t* u = (uint32_t*)hdr;
    if (m_littleEndian == m_header.m_littleEndian)
	len = *u;
    else if (m_littleEndian)
	len = be32toh(*u);
    else
	len = le32toh(*u);
    uint64_t* p = (uint64_t*)&hdr[4];
    if (m_littleEndian == m_header.m_littleEndian)
	ts = *p;
    else if (m_littleEndian)
	len = be64toh(*p);
    else
	len = le64toh(*p);
    buffer.resize(len);
    if (!len)
	return ioError(false,dbg,0,"Empty record");
    rd = m_file.readData((void*)buffer.data(),len);
    if (rd != (int)len)
	return ioError(false,dbg,error,rd > 0 ? "Incomplete read (invalid size?)" : 0);
#ifdef XDEBUG
    String sHdr, s;
    sHdr.hexify(hdr,sizeof(hdr),' ');
    s.hexify((void*)buffer.data(),rd,' ');
    Debug(dbg,DebugAll,"RadioDataFile[%s] read %d hdr=%s data=%s [%p]",
	c_str(),rd + (int)sizeof(hdr),sHdr.c_str(),s.c_str(),this);
#endif
    return true;
}

void RadioDataFile::terminate(DebugEnabler* dbg)
{
    if (dbg && valid())
	Debug(dbg,DebugAll,"RadioDataFile[%s] closing file [%p]",c_str(),this);
    m_file.terminate();
}

// Convert endiannes
bool RadioDataFile::fixEndian(DataBlock& buf, unsigned int bytes)
{
    if (!bytes)
	return false;
    unsigned int n = buf.length() / bytes;
    if (bytes == 2) {
	for (uint16_t* p = (uint16_t*)buf.data(); n; n--, p++)
#ifdef LITTLE_ENDIAN
	    *p = be16toh(*p);
#else
	    *p = le16toh(*p);
#endif
	return true;
    }
    if (bytes == 4) {
	for (uint32_t* p = (uint32_t*)buf.data(); n; n--, p++)
#ifdef LITTLE_ENDIAN
	    *p = be32toh(*p);
#else
	    *p = le32toh(*p);
#endif
	return true;
    }
    if (bytes == 8) {
	for (uint64_t* p = (uint64_t*)buf.data(); n; n--, p++)
#ifdef LITTLE_ENDIAN
	    *p = be64toh(*p);
#else
	    *p = le64toh(*p);
#endif
	return true;
    }
    return false;
}

bool RadioDataFile::ioError(bool send, DebugEnabler* dbg, int* error, const char* extra)
{
    String s = extra;
    if (error) {
	String tmp;
	Thread::errorString(tmp,m_file.error());
	tmp.printf("(%d - %s)",m_file.error(),tmp.c_str());
	s.append(tmp," ");
    }
    Debug(dbg,DebugNote,"RadioDataFile[%s] file %s failed: %s [%p]",
	c_str(),(send ? "write" : "read"),s.safe(),this);
    if (error) {
	*error = m_file.error();
	if (m_dropOnError)
	    terminate();
    }
    return false;
}

/* vi: set ts=8 sw=4 sts=4 noet enc=utf-8: */
