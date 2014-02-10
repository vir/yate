/**
 * DataFormat.cpp
 * This file is part of the YATE Project http://YATE.null.ro
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

#include "yatephone.h"

#include <string.h>
#include <stdlib.h>

namespace TelEngine {

static const FormatInfo s_formats[] = {
    FormatInfo("slin", 160, 10000, "audio", 8000, 1, true),
    FormatInfo("alaw", 80),
    FormatInfo("mulaw", 80),
    FormatInfo("slin/16000", 320, 10000, "audio", 16000, 1, true),
    FormatInfo("alaw/16000", 160, 10000, "audio", 16000),
    FormatInfo("mulaw/16000", 160, 10000, "audio", 16000),
    FormatInfo("slin/32000", 640, 10000, "audio", 32000, 1, true),
    FormatInfo("alaw/32000", 160, 10000, "audio", 32000),
    FormatInfo("mulaw/32000", 160, 10000, "audio", 32000),
    FormatInfo("2*slin", 320, 10000, "audio", 8000, 2),
    FormatInfo("2*slin/16000", 640, 10000, "audio", 16000, 2),
    FormatInfo("2*slin/32000", 1280, 10000, "audio", 32000, 2),
    FormatInfo("2*alaw", 160, 10000, "audio", 8000, 2),
    FormatInfo("2*mulaw", 160, 10000, "audio", 8000, 2),
    FormatInfo("gsm", 33, 20000),
    FormatInfo("ilbc20", 38, 20000),
    FormatInfo("ilbc30", 50, 30000),
//    FormatInfo("speex", 0),
    FormatInfo("g729", 10, 10000),
    FormatInfo("plain", 0, 0, "text", 0),
    FormatInfo("raw", 0, 0, "data", 0),
};

// FIXME: put proper conversion costs everywhere below

static TranslatorCaps s_simpleCaps[] = {
    { s_formats+0, s_formats+1, 1 },
    { s_formats+0, s_formats+2, 1 },
    { s_formats+1, s_formats+0, 1 },
    { s_formats+1, s_formats+2, 1 },
    { s_formats+2, s_formats+0, 1 },
    { s_formats+2, s_formats+1, 1 },
    { s_formats+9, s_formats+12, 1 },
    { s_formats+9, s_formats+13, 1 },
    { s_formats+12, s_formats+9, 1 },
    { s_formats+13, s_formats+9, 1 },
    { s_formats+12, s_formats+13, 1 },
    { s_formats+13, s_formats+12, 1 },
    { 0, 0, 0 }
};

static TranslatorCaps s_simpleCaps16k[] = {
    { s_formats+3, s_formats+4, 1 },
    { s_formats+3, s_formats+5, 1 },
    { s_formats+4, s_formats+3, 1 },
    { s_formats+4, s_formats+5, 1 },
    { s_formats+5, s_formats+3, 1 },
    { s_formats+5, s_formats+4, 1 },
    { 0, 0, 0 }
};

static TranslatorCaps s_simpleCaps32k[] = {
    { s_formats+6, s_formats+7, 1 },
    { s_formats+6, s_formats+8, 1 },
    { s_formats+7, s_formats+6, 1 },
    { s_formats+7, s_formats+8, 1 },
    { s_formats+8, s_formats+6, 1 },
    { s_formats+8, s_formats+7, 1 },
    { 0, 0, 0 }
};

static TranslatorCaps s_resampCaps[] = {
    { s_formats+0, s_formats+3, 2 },
    { s_formats+0, s_formats+6, 2 },
    { s_formats+3, s_formats+0, 2 },
    { s_formats+3, s_formats+6, 2 },
    { s_formats+6, s_formats+0, 2 },
    { s_formats+6, s_formats+3, 2 },
    { 0, 0, 0 }
};

static TranslatorCaps s_stereoCaps[] = {
    { s_formats+0, s_formats+9, 1 },
    { s_formats+9, s_formats+0, 2 },
    { s_formats+3, s_formats+10, 1 },
    { s_formats+10, s_formats+3, 2 },
    { s_formats+6, s_formats+11, 1 },
    { s_formats+11, s_formats+6, 2 },
    { 0, 0, 0 }
};

static Mutex s_dataMutex(true,"DataEndpoint");
static Mutex s_consSrcMutex(false,"DataConsumer::Source");

class ThreadedSourcePrivate : public Thread
{
    friend class ThreadedSource;
public:
    ThreadedSourcePrivate(ThreadedSource* source, const char* name, Thread::Priority prio)
	: Thread(name,prio), m_source(source) { }

protected:
    virtual void run()
	{
	    m_source->run();
	    // execute cleanup from this thread if possible
	    cleanup();
	}

    virtual void cleanup()
	{
	    RefPointer<ThreadedSource> source = m_source;
	    m_source = 0;
	    if (source)
		source->cleanup();
	}

private:
    RefPointer<ThreadedSource> m_source;
};

// slin/alaw/mulaw converter
class SimpleTranslator : public DataTranslator
{
public:
    SimpleTranslator(const DataFormat& sFormat, const DataFormat& dFormat)
	: DataTranslator(sFormat,dFormat), m_valid(false) {
	    if (!getTransSource())
		return;
	    int nchan = m_format.numChannels();
	    if (nchan != getTransSource()->getFormat().numChannels())
		return;
	    m_valid = true;
	    m_sFmt = m_format;
	    m_dFmt = getTransSource()->getFormat();
	    if (nchan != 1) {
		// get rid of the channel prefix
		m_sFmt >> "*";
		m_dFmt >> "*";
	    }
	}
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
	{
	    if (!ref())
		return 0;
	    unsigned long len = 0;
	    if (m_valid && getTransSource() && m_buffer.convert(data,m_sFmt,m_dFmt)) {
		if (tStamp == invalidStamp()) {
		    unsigned int delta = data.length();
		    if (delta > m_buffer.length())
			delta = m_buffer.length();
		    tStamp = m_timestamp + delta;
		}
		m_timestamp = tStamp;
		len = getTransSource()->Forward(m_buffer,tStamp,flags);
	    }
	    deref();
	    return len;
	}
private:
    bool m_valid;
    String m_sFmt;
    String m_dFmt;
    DataBlock m_buffer;
};

// slin basic mono resampler
class ResampTranslator : public DataTranslator
{
private:
    int m_sRate, m_dRate;
    short m_last;
public:
    ResampTranslator(const DataFormat& sFormat, const DataFormat& dFormat)
	: DataTranslator(sFormat,dFormat),
	m_sRate(sFormat.sampleRate()), m_dRate(dFormat.sampleRate()), m_last(0)
	{ }
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
	{
	    unsigned int n = data.length();
	    if (!n || (n & 1) || !m_sRate || !m_dRate || !ref())
		return 0;
	    unsigned long len = 0;
	    n /= 2;
	    DataSource* src = getTransSource();
	    if (src) {
		long delta = tStamp - m_timestamp;
		short* s = (short*) data.data();
		DataBlock oblock;
		if (m_dRate > m_sRate) {
		    int mul = m_dRate / m_sRate;
		    // linear interpolation between existing samples
		    delta *= mul;
		    oblock.assign(0,2*n*mul);
		    short* d = (short*) oblock.data();
		    while (n--) {
			short v = *s++;
			for (int i = 1; i <= mul; i++)
			    *d++ = ((m_last * (mul - i)) + (v * i)) / mul;
			m_last = v;
		    }
		}
		else {
		    int div = m_sRate / m_dRate;
		    // average an integer number of samples
		    delta /= div;
		    n /= div;
		    oblock.assign(0,2*n);
		    short* d = (short*) oblock.data();
		    while (n--) {
			int v = 0;
			for (int i = 0; i < div; i++)
			    v += *s++;
			v /= div;
			// saturate average result
			if (v > 32767)
			    v = 32767;
			if (v < -32767)
			    v = -32767;
			*d++ = v;
		    }
		}
		if (src->timeStamp() != invalidStamp())
		    delta += src->timeStamp();
		len = src->Forward(oblock, delta, flags);
	    }
	    deref();
	    return len;
	}
};

// slin simple mono-stereo converter
class StereoTranslator : public DataTranslator
{
private:
    int m_sChans, m_dChans;
public:
    StereoTranslator(const DataFormat& sFormat, const DataFormat& dFormat)
	: DataTranslator(sFormat,dFormat),
	m_sChans(sFormat.numChannels()), m_dChans(dFormat.numChannels())
	{ }
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
	{
	    unsigned int n = data.length();
	    if (!n || (n & 1) || !ref())
		return 0;
	    unsigned long len = 0;
	    n /= 2;
	    if (getTransSource()) {
		short* s = (short*) data.data();
		DataBlock oblock;
		if ((m_sChans == 1) && (m_dChans == 2)) {
		    oblock.assign(0,n*4);
		    short* d = (short*) oblock.data();
		    // duplicate the sample for each channel
		    while (n--) {
			short v = *d++ = *s++;
			*d++ = v;
		    }
		}
		else if ((m_sChans == 2) && (m_dChans == 1)) {
		    n /= 2;
		    oblock.assign(0,2*n);
		    short* d = (short*) oblock.data();
		    // average the channels
		    while (n--) {
			int v = *s++;
			v += *s++;
			v /= 2;
			// saturate average result
			if (v > 32767)
			    v = 32767;
			if (v < -32767)
			    v = -32767;
			*d++ = v;
		    }
		}
		len = getTransSource()->Forward(oblock, tStamp, flags);
	    }
	    deref();
	    return len;
	}
};

class SimpleFactory : public TranslatorFactory
{
public:
    SimpleFactory(const TranslatorCaps* caps, const char* name)
	: TranslatorFactory(name), m_caps(caps)
	{ }
    virtual DataTranslator* create(const DataFormat& sFormat, const DataFormat& dFormat)
	{ return converts(sFormat,dFormat) ? new SimpleTranslator(sFormat,dFormat) : 0; }
    virtual const TranslatorCaps* getCapabilities() const
	{ return m_caps; }
private:
    const TranslatorCaps* m_caps;
};

class ResampFactory : public TranslatorFactory
{
public:
    ResampFactory() : TranslatorFactory("resample")
	{ }
    virtual DataTranslator* create(const DataFormat& sFormat, const DataFormat& dFormat)
	{ return converts(sFormat,dFormat) ? new ResampTranslator(sFormat,dFormat) : 0; }
    virtual const TranslatorCaps* getCapabilities() const
	{ return s_resampCaps; }
};

class StereoFactory : public TranslatorFactory
{
public:
    StereoFactory() : TranslatorFactory("stereo")
	{ }
    virtual DataTranslator* create(const DataFormat& sFormat, const DataFormat& dFormat)
	{ return converts(sFormat,dFormat) ? new StereoTranslator(sFormat,dFormat) : 0; }
    virtual const TranslatorCaps* getCapabilities() const
	{ return s_stereoCaps; }
};

class ChainedFactory : public TranslatorFactory
{
public:
    ChainedFactory(TranslatorFactory* factory1, TranslatorFactory* factory2, const FormatInfo* info);
    virtual ~ChainedFactory();
    virtual void removed(const TranslatorFactory* factory);
    virtual DataTranslator* create(const DataFormat& sFormat, const DataFormat& dFormat);
    virtual const TranslatorCaps* getCapabilities() const
	{
	    return (m_factory1 && m_factory1->getCapabilities() &&
		m_factory2 && m_factory2->getCapabilities()) ? m_capabilities : 0;
	}
    virtual unsigned int length() const
	{ return m_length; }
    virtual const FormatInfo* intermediate() const;
    virtual bool intermediate(const FormatInfo* info) const;
    virtual const char* name() const
	{ return m_name.c_str(); }
private:
    TranslatorFactory* m_factory1;
    TranslatorFactory* m_factory2;
    String m_name;
    DataFormat m_format;
    unsigned int m_length;
    const TranslatorCaps* m_capabilities;
};

};

using namespace TelEngine;


int FormatInfo::guessSamples(int len) const
{
    if (!(frameTime && frameSize))
	return 0;
    return (len / frameSize) * sampleRate * (long)frameTime / 1000000;
}

int FormatInfo::dataRate() const
{
    if (!frameTime)
	return 0;
    return frameSize * 1000000 / frameTime;
}

typedef struct _flist {
    struct _flist* next;
    const FormatInfo* info;
} flist;

static flist* s_flist = 0;

const FormatInfo* FormatRepository::getFormat(const String& name)
{
    if (name.null())
	return 0;
    // search in the static list first
    for (unsigned int i = 0; i < (sizeof(s_formats)/sizeof(FormatInfo)); i++)
	if (name == s_formats[i].name)
	    return s_formats+i;
    // then try the installed formats
    for (flist* l = s_flist; l; l = l->next)
	if (name == l->info->name)
	    return l->info;
    return 0;
}

const FormatInfo* FormatRepository::addFormat(const String& name, int fsize, int ftime, const String& type, int srate, int nchan)
{
    if (name.null() || type.null())
	return 0;

    const FormatInfo* f = getFormat(name);
    if (f) {
	// found by name - check if it exactly matches what we have already
	if ((fsize != f->frameSize) ||
	    (ftime != f->frameTime) ||
	    (srate != f->sampleRate) ||
	    (nchan != f->numChannels) ||
	    (type != f->type)) {
		Debug(DebugWarn,"Tried to register '%s' format '%s' fsize=%d ftime=%d srate=%d nchan=%d",
		    type.c_str(),name.c_str(),fsize,ftime,srate,nchan);
		return 0;
	}
	return f;
    }
    // not in list - add a new one to the installed formats
    DDebug(DebugInfo,"Registering '%s' format '%s' fsize=%d ftime=%d srate=%d nchan=%d",
	type.c_str(),name.c_str(),fsize,ftime,srate,nchan);
    f = new FormatInfo(::strdup(name),fsize,ftime,::strdup(type),srate,nchan);
    flist* l = new flist;
    l->info = f;
    l->next = s_flist;
    s_flist = l;
    return f;
}


void DataFormat::changed()
{
    m_parsed = 0;
    String::changed();
}

const FormatInfo* DataFormat::getInfo() const
{
    if (!(m_parsed || null()))
	m_parsed = FormatRepository::getFormat(*this);
    return m_parsed;
}


void DataConsumer::destroyed()
{
    if (m_source || m_override) {
	// this should not happen - but scream bloody murder if so
	Debug(DebugFail,"DataConsumer destroyed with source=%p override=%p [%p]",
	    m_source,m_override,this);
    }
    if (m_source)
	m_source->detach(this);
    if (m_override)
	m_override->detach(this);
    DataNode::destroyed();
}

void* DataConsumer::getObject(const String& name) const
{
    if (name == YATOM("DataConsumer"))
	return const_cast<DataConsumer*>(this);
    return DataNode::getObject(name);
}

unsigned long DataConsumer::Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags, DataSource* source)
{
    if (source == m_override)
	tStamp += m_overrideTsDelta;
    else if (m_override || (source != m_source))
	return 0;
    else
	tStamp += m_regularTsDelta;
    u_int64_t tsTime = Time::now();
    unsigned long len = Consume(data,tStamp,flags);
    m_timestamp = tStamp;
    m_lastTsTime = tsTime;
    return len;
}

bool DataConsumer::synchronize(DataSource* source)
{
    if (!source)
	return false;
    bool override = false;
    if (source == m_override)
	override = true;
    else if (source != m_source)
	return false;
    if (!(m_timestamp || m_regularTsDelta || m_overrideTsDelta)) {
	// first time
	m_timestamp = source->timeStamp();
	return true;
    }
    const FormatInfo* info = getFormat().getInfo();
    int64_t dt = 0;
    if (info) {
	// adjust timestamp for possible silence or gaps in data, at least 25ms
	dt = Time::now() - m_lastTsTime;
	if (dt >= 25000) {
	    dt = (dt * info->sampleRate) / 1000000;
	    DDebug(DebugInfo,"Data gap, offsetting consumer timestamps by " FMT64 " [%p]",dt,this);
	}
	else
	    dt = 0;
    }
    dt += m_timestamp - source->timeStamp();
    DDebug(DebugInfo,"Offsetting consumer %s timestamps by " FMT64 " [%p]",
	(override ? "override" : "regular"),dt,this);
    if (override)
	m_overrideTsDelta = (long int)dt;
    else
	m_regularTsDelta = (long int)dt;
    return true;
}


bool DataSource::valid() const
{
    Lock mylock(const_cast<DataSource*>(this));
    if (!m_translator)
	return true;
    // this is a translator's source - check if we have at least one valid consumer
    for (ObjList* l = m_consumers.skipNull(); l; l=l->skipNext()) {
	DataConsumer* c = static_cast<DataConsumer *>(l->get());
	if (c->valid())
	    return true;
    }
    return false;
}

unsigned long DataSource::Forward(const DataBlock& data, unsigned long tStamp, unsigned long flags)
{
    Lock mylock(this,100000);
    // we DON'T refcount here, we rely on the mutex to keep us safe
    if (!(mylock.locked() && alive())) {
	DDebug(DebugInfo,"Forwarding on a dead DataSource! [%p]",this);
	return 0;
    }

    // try to evaluate amount of samples in this packet
    const FormatInfo* f = m_format.getInfo();
    unsigned long nSamp = f ? f->guessSamples(data.length()) : 0;

    // if no timestamp provided - try to use next expected
    if (tStamp == invalidStamp())
	tStamp = m_nextStamp;
    // still no timestamp known - wild guess based on this packet size
    if (tStamp == invalidStamp()) {
	DDebug(DebugNote,"Unknown timestamp - assuming %lu + %lu [%p]",
	    m_timestamp,nSamp,this);
	tStamp = m_timestamp + nSamp;
    }
    unsigned long len = invalidStamp();
    bool empty = true;
    ObjList* l = m_consumers.skipNull();
    while (l) {
	DataConsumer* c = static_cast<DataConsumer *>(l->get());
	unsigned long ll = c->Consume(data,tStamp,flags,this);
	if (ll || c->valid()) {
	    // get the minimum data amount forwarded to all consumers
	    if (len > ll)
		len = ll;
	    l = l->skipNext();
	    empty = false;
	}
	else {
	    DDebug(DebugInfo,"Consumer %p becomes invalid [%p]",c,this);
	    detachInternal(c);
	    // do not advance in list, we just removed the current element
	    l = l->skipNull();
	}
    }
    if (empty)
	len = 0;
    m_timestamp = tStamp;
    m_nextStamp = nSamp ? (tStamp + nSamp) : invalidStamp();
    return len;
}

bool DataSource::attach(DataConsumer* consumer, bool override)
{
    if (!alive()) {
	DDebug(DebugFail,"Attaching a dead DataSource! [%p]",this);
	return false;
    }
    DDebug(DebugAll,"DataSource [%p] attaching consumer%s [%p]",
	this,(override ? " as override" : ""),consumer);
    if (!(consumer && consumer->ref()))
	return false;
    Lock mylock(this);
    DataSource*& src = override ? consumer->m_override : consumer->m_source;
    if (src != this) {
	s_consSrcMutex.lock();
	RefPointer<DataSource> src2(src);
	if (src2 && (this != src2)) {
	    s_consSrcMutex.unlock();
	    src2->detach(consumer);
	    s_consSrcMutex.lock();
	    if (src) {
		Debug(DebugGoOn,"DataSource %p raced us attaching consumer %p [%p]",
		    src,consumer,this);
		s_consSrcMutex.unlock();
		consumer->deref();
		return false;
	    }
	}
	src = this;
	s_consSrcMutex.unlock();
    }
    consumer->synchronize(this);
    m_consumers.append(consumer);
    return true;
}

bool DataSource::detach(DataConsumer* consumer)
{
    if (!consumer)
	return false;
    if (!ref()) {
	DDebug(DebugFail,"Detaching a dead DataSource! [%p]",this);
	return false;
    }
    DDebug(DebugAll,"DataSource [%p] detaching consumer [%p]",this,consumer);
    // lock the source to prevent races with the Forward method
    lock();
    bool ok = detachInternal(consumer);
    unlock();
    deref();
    return ok;
}

bool DataSource::detachInternal(DataConsumer* consumer)
{
    if (!consumer)
	return false;
    DataConsumer *temp = static_cast<DataConsumer *>(m_consumers.remove(consumer,false));
    if (temp) {
	s_consSrcMutex.lock();
	if (temp->m_source == this)
	    temp->m_source = 0;
	if (temp->m_override == this)
	    temp->m_override = 0;
	s_consSrcMutex.unlock();
	temp->deref();
	return true;
    }
    DDebug(DebugInfo,"DataSource [%p] has no consumer [%p]",this,consumer);
    return false;
}

void DataSource::destroyed()
{
    m_translator = 0;
    clear();
    DataNode::destroyed();
}

void DataSource::clear()
{
    // keep the source locked to prevent races with the Forward method
    lock();
    while (detachInternal(static_cast<DataConsumer*>(m_consumers.get())))
	;
    unlock();
}

void DataSource::synchronize(unsigned long tStamp)
{
    Lock mylock(this,100000);
    if (!(mylock.locked() && alive())) {
	DDebug(DebugInfo,"Synchronizing on a dead DataSource! [%p]",this);
	return;
    }
    m_timestamp = tStamp;
    m_nextStamp = invalidStamp();
    ObjList *l = m_consumers.skipNull();
    for (; l; l=l->skipNext()) {
	DataConsumer *c = static_cast<DataConsumer *>(l->get());
	c->synchronize(this);
    }
}

void* DataSource::getObject(const String& name) const
{
    if (name == YATOM("DataSource"))
	return const_cast<DataSource*>(this);
    return DataNode::getObject(name);
}


DataEndpoint::DataEndpoint(CallEndpoint* call, const char* name)
    : m_name(name), m_source(0), m_consumer(0),
      m_peer(0), m_call(call),
      m_peerRecord(0), m_callRecord(0)
{
    DDebug(DebugAll,"DataEndpoint::DataEndpoint(%p,'%s') [%p]",call,name,this);
    if (m_call)
	m_call->m_data.append(this);
}

void DataEndpoint::destroyed()
{
    DDebug(DebugAll,"DataEndpoint::destroyed() '%s' call=%p [%p]",
	m_name.c_str(),m_call,this);
    if (m_call)
	m_call->m_data.remove(this,false);
    disconnect();
    setPeerRecord();
    setCallRecord();
    clearSniffers();
    setSource();
    setConsumer();
    RefObject::destroyed();
}

void* DataEndpoint::getObject(const String& name) const
{
    if (name == YATOM("DataEndpoint"))
	return const_cast<DataEndpoint*>(this);
    return RefObject::getObject(name);
}

const String& DataEndpoint::toString() const
{
    return m_name;
}

Mutex* DataEndpoint::mutex() const
{
    return m_call ? m_call->mutex() : 0;
}

Mutex& DataEndpoint::commonMutex()
{
    return s_dataMutex;
}

bool DataEndpoint::connect(DataEndpoint* peer)
{
    if (!peer) {
	disconnect();
	return false;
    }
    Lock lock(s_dataMutex);
    if (peer == m_peer)
	return true;
    DDebug(DebugInfo,"DataEndpoint '%s' connecting peer %p to [%p]",m_name.c_str(),peer,this);

    ref();
    peer->ref();
    disconnect();
    peer->disconnect();
    bool native = (name() == peer->name()) && nativeConnect(peer);

    if (!native) {
	XDebug(DebugInfo,"DataEndpoint s=%p c=%p peer @%p s=%p c=%p [%p]",
	    getSource(),getConsumer(),peer,peer->getSource(),peer->getConsumer(),this);
	DataSource* s = getSource();
	DataConsumer* c = peer->getConsumer();
	if (s && c)
	    DataTranslator::attachChain(s,c);
	c = peer->getPeerRecord();
	if (s && c)
	    DataTranslator::attachChain(s,c);

	s = peer->getSource();
	c = getConsumer();
	if (s && c)
	    DataTranslator::attachChain(s,c);
	c = getPeerRecord();
	if (s && c)
	    DataTranslator::attachChain(s,c);
    }

    m_peer = peer;
    peer->m_peer = this;

    return true;
}

bool DataEndpoint::disconnect()
{
    Lock lock(s_dataMutex);
    if (!m_peer)
	return false;
    DDebug(DebugInfo,"DataEndpoint '%s' disconnecting peer %p from [%p]",m_name.c_str(),m_peer,this);

    DataSource* s = getSource();
    DataConsumer* c = m_peer->getConsumer();
    if (s && c)
	DataTranslator::detachChain(s,c);
    c = m_peer->getPeerRecord();
    if (s && c)
	DataTranslator::detachChain(s,c);

    s = m_peer->getSource();
    c = getConsumer();
    if (s && c)
	DataTranslator::detachChain(s,c);
    c = getPeerRecord();
    if (s && c)
	DataTranslator::detachChain(s,c);

    DataEndpoint* temp = m_peer;
    m_peer = 0;
    temp->m_peer = 0;
    lock.drop();
    temp->deref();
    return deref();
}

void DataEndpoint::setSource(DataSource* source)
{
    Lock lock(s_dataMutex);
    if (source == m_source)
	return;
    DataConsumer* c1 = m_peer ? m_peer->getConsumer() : 0;
    DataConsumer* c2 = m_peer ? m_peer->getPeerRecord() : 0;
    DataSource* temp = m_source;
    XDebug(DebugInfo,"DataEndpoint::setSource(%p) peer=%p s=%p c1=%p c2=%p cr=%p [%p]",
	    source,m_peer,temp,c1,c2,m_callRecord,this);
    if (c1)
	c1->ref();
    if (c2)
	c2->ref();
    if (m_callRecord)
	m_callRecord->ref();
    m_source = 0;
    if (temp) {
	if (c1) {
	    DataTranslator::detachChain(temp,c1);
	    if (c1->getConnSource())
		Debug(DebugWarn,"consumer source not cleared in %p",c1);
	}
	if (c2) {
	    DataTranslator::detachChain(temp,c2);
	    if (c2->getConnSource())
		Debug(DebugWarn,"consumer source not cleared in %p",c2);
	}
	if (m_callRecord) {
	    DataTranslator::detachChain(temp,m_callRecord);
	    if (m_callRecord->getConnSource())
		Debug(DebugWarn,"consumer source not cleared in %p",m_callRecord);
	}
	ObjList* l = m_sniffers.skipNull();
	for (; l; l = l->skipNext())
	    DataTranslator::detachChain(temp,static_cast<DataConsumer*>(l->get()));
	temp->attached(false);
    }
    if (source) {
	source->ref();
	if (c1)
	    DataTranslator::attachChain(source,c1);
	if (c2)
	    DataTranslator::attachChain(source,c2);
	if (m_callRecord)
	    DataTranslator::attachChain(source,m_callRecord);
	ObjList* l = m_sniffers.skipNull();
	for (; l; l = l->skipNext())
	    DataTranslator::attachChain(source,static_cast<DataConsumer*>(l->get()));
	source->attached(true);
    }
    m_source = source;
    if (m_callRecord)
	m_callRecord->deref();
    lock.drop();
    TelEngine::destruct(temp);
    TelEngine::destruct(c1);
    TelEngine::destruct(c2);
}

void DataEndpoint::setConsumer(DataConsumer* consumer)
{
    Lock lock(s_dataMutex);
    if (consumer == m_consumer)
	return;
    DataSource* source = m_peer ? m_peer->getSource() : 0;
    DataConsumer* temp = m_consumer;
    XDebug(DebugInfo,"DataEndpoint::setConsumer(%p) peer=%p c=%p ps=%p [%p]",
	    consumer,m_peer,temp,source,this);
    if (consumer) {
	if (consumer->ref()) {
	    if (source)
		DataTranslator::attachChain(source,consumer);
	}
	else
	    consumer = 0;
    }
    m_consumer = consumer;
    if (source && temp)
	DataTranslator::detachChain(source,temp);
    if (temp) {
	s_consSrcMutex.lock();
	RefPointer<DataSource> src = temp->getConnSource();
	s_consSrcMutex.unlock();
	if (src) {
	    src->detach(temp);
	    src = 0;
	}
	s_consSrcMutex.lock();
	src = temp->getOverSource();
	s_consSrcMutex.unlock();
	if (src) {
	    src->detach(temp);
	    src = 0;
	}
	temp->attached(false);
    }
    if (consumer)
	consumer->attached(true);
    lock.drop();
    TelEngine::destruct(temp);
}

void DataEndpoint::setPeerRecord(DataConsumer* consumer)
{
    Lock lock(s_dataMutex);
    if (consumer == m_peerRecord)
	return;
    DataSource* source = m_peer ? m_peer->getSource() : 0;
    DataConsumer* temp = m_peerRecord;
    XDebug(DebugInfo,"DataEndpoint::setPeerRecord(%p) peer=%p pr=%p ps=%p [%p]",
	    consumer,m_peer,temp,source,this);
    if (consumer) {
	if (consumer->ref()) {
	    if (source)
		DataTranslator::attachChain(source,consumer);
	}
	else
	    consumer = 0;
    }
    m_peerRecord = consumer;
    if (source && temp)
	DataTranslator::detachChain(source,temp);
    if (temp)
	temp->attached(false);
    if (consumer)
	consumer->attached(true);
    lock.drop();
    TelEngine::destruct(temp);
}

void DataEndpoint::setCallRecord(DataConsumer* consumer)
{
    Lock lock(s_dataMutex);
    if (consumer == m_callRecord)
	return;
    DataConsumer* temp = m_callRecord;
    XDebug(DebugInfo,"DataEndpoint::setCallRecord(%p) cr=%p s=%p [%p]",
	    consumer,temp,m_source,this);
    if (consumer) {
	if (consumer->ref()) {
	    if (m_source)
		DataTranslator::attachChain(m_source,consumer);
	}
	else
	    consumer = 0;
    }
    m_callRecord = consumer;
    if (temp && m_source)
	DataTranslator::detachChain(m_source,temp);
    if (temp)
	temp->attached(false);
    if (consumer)
	consumer->attached(true);
    lock.drop();
    TelEngine::destruct(temp);
}

bool DataEndpoint::addSniffer(DataConsumer* sniffer)
{
    if (!sniffer)
	return false;
    Lock lock(s_dataMutex);
    if (m_sniffers.find(sniffer))
	return false;
    if (!sniffer->ref())
	return false;
    XDebug(DebugInfo,"DataEndpoint::addSniffer(%p) s=%p [%p]",
	sniffer,m_source,this);
    m_sniffers.append(sniffer);
    if (m_source)
	DataTranslator::attachChain(m_source,sniffer);
    sniffer->attached(true);
    return true;
}

bool DataEndpoint::delSniffer(DataConsumer* sniffer)
{
    if (!sniffer)
	return false;
    Lock lock(s_dataMutex);
    XDebug(DebugInfo,"DataEndpoint::delSniffer(%p) s=%p [%p]",
	sniffer,m_source,this);
    if (!m_sniffers.remove(sniffer,false))
	return false;
    if (m_source)
	DataTranslator::detachChain(m_source,sniffer);
    sniffer->attached(false);
    sniffer->deref();
    return true;
}

void DataEndpoint::clearSniffers()
{
    Lock lock(s_dataMutex);
    for (;;) {
	DataConsumer* sniffer = static_cast<DataConsumer*>(m_sniffers.remove(false));
	if (!sniffer)
	    return;
	XDebug(DebugInfo,"DataEndpoint::clearSniffers() sn=%p s=%p [%p]",
	    sniffer,m_source,this);
	if (m_source)
	    DataTranslator::detachChain(m_source,sniffer);
	sniffer->attached(false);
	sniffer->deref();
    }
}

bool DataEndpoint::clearData(DataNode* node)
{
    if (!node)
	return false;
    Lock lock(s_dataMutex);
    bool ok = delSniffer(static_cast<DataConsumer*>(node));
    if (m_callRecord == node) {
	setCallRecord();
	ok = true;
    }
    if (m_peerRecord == node) {
	setPeerRecord();
	ok = true;
    }
    if (m_consumer == node) {
	setConsumer();
	ok = true;
    }
    if (m_source == node) {
	setSource();
	ok = true;
    }
    return ok;
}

// Change source(s) or consumer(s)
bool DataEndpoint::control(NamedList& params)
{
    // TODO how do we handle this case????? operation-status
    return (m_source && m_source->control(params)) ||
	(m_consumer && m_consumer->control(params)) ||
	(m_peerRecord && m_peerRecord->control(params)) ||
	(m_callRecord && m_callRecord->control(params));
}


void ThreadedSource::destroyed()
{
    if (m_thread)
	Debug(DebugFail,"ThreadedSource destroyed holding thread %p [%p]",m_thread,this);
    DataSource::destroyed();
}

bool ThreadedSource::start(const char* name, Thread::Priority prio)
{
    Lock mylock(this);
    if (!m_thread) {
	ThreadedSourcePrivate* thread = new ThreadedSourcePrivate(this,name,prio);
	if (thread->startup()) {
	    m_thread = thread;
	    return true;
	}
	delete thread;
	return false;
    }
    return m_thread->running();
}

void ThreadedSource::stop()
{
    Lock mylock(this);
    ThreadedSourcePrivate* tmp = m_thread;
    m_thread = 0;
    if (!tmp || tmp->running())
	return;
    Debug(DebugInfo,"ThreadedSource deleting stopped thread %p [%p]",tmp,this);
    mylock.drop();
    delete tmp;
}

void ThreadedSource::cleanup()
{
    lock();
    m_thread = 0;
    unlock();
}

Thread* ThreadedSource::thread() const
{
    return m_thread;
}

bool ThreadedSource::running() const
{
    Lock mylock(const_cast<ThreadedSource*>(this));
    return m_thread && m_thread->running();
}

bool ThreadedSource::looping(bool runConsumers) const
{
    Lock mylock(const_cast<ThreadedSource*>(this));
    if ((refcount() <= 1) && !(runConsumers && alive() && m_consumers.count()))
	return false;
    return m_thread && !m_thread->check(false) &&
	m_thread->isCurrent() && !Engine::exiting();
}


DataTranslator::DataTranslator(const char* sFormat, const char* dFormat)
    : DataConsumer(sFormat)
{
    DDebug(DebugAll,"DataTranslator::DataTranslator('%s','%s') [%p]",sFormat,dFormat,this);
    m_tsource = new DataSource(dFormat);
    m_tsource->setTranslator(this);
}

DataTranslator::DataTranslator(const char* sFormat, DataSource* source)
    : DataConsumer(sFormat), m_tsource(source)
{
    DDebug(DebugAll,"DataTranslator::DataTranslator('%s',%p) [%p]",sFormat,source,this);
    m_tsource->setTranslator(this);
}

DataTranslator::~DataTranslator()
{
    DDebug(DebugAll,"DataTranslator::~DataTranslator() [%p]",this);
    DataSource *temp = m_tsource;
    m_tsource = 0;
    if (temp) {
	temp->setTranslator(0);
	temp->deref();
    }
}

void* DataTranslator::getObject(const String& name) const
{
    if (name == YATOM("DataTranslator"))
	return const_cast<DataTranslator*>(this);
    return DataConsumer::getObject(name);
}

DataTranslator* DataTranslator::getFirstTranslator()
{
    DataSource* tsource = getConnSource();
    if (!tsource)
	return this;
    DataTranslator* trans = tsource->getTranslator();
    return trans ? trans->getFirstTranslator() : this;
}

const DataTranslator* DataTranslator::getFirstTranslator() const
{
    const DataSource* tsource = getConnSource();
    if (!tsource)
	return this;
    const DataTranslator* trans = tsource->getTranslator();
    return trans ? trans->getFirstTranslator() : this;
}

bool DataTranslator::synchronize(DataSource* source)
{
    if (!DataConsumer::synchronize(source))
	return false;
    if (m_tsource)
	m_tsource->synchronize(timeStamp());
    return true;
}

Mutex DataTranslator::s_mutex(true,"DataTranslator");
ObjList DataTranslator::s_factories;
unsigned int DataTranslator::s_maxChain = 3;
static ObjList s_compose;
static SimpleFactory s_sFactory(s_simpleCaps,"g711");
static SimpleFactory s_sFactory16k(s_simpleCaps16k,"g711wb");
static SimpleFactory s_sFactory32k(s_simpleCaps32k,"g711uwb");
// FIXME
static ResampFactory s_rFactory;
static StereoFactory s_stereoFactory;

void DataTranslator::setMaxChain(unsigned int maxChain)
{
    if (maxChain < 1)
	maxChain = 1;
    if (maxChain > 4)
	maxChain = 4;
    s_maxChain = maxChain;
}

void DataTranslator::install(TranslatorFactory* factory)
{
    if (!factory)
	return;
    Lock lock(s_mutex);
    if (s_factories.find(factory))
	return;
    s_factories.append(factory)->setDelete(false);
    s_compose.append(factory)->setDelete(false);
}

void DataTranslator::compose()
{
    for (;;) {
	TranslatorFactory* factory = static_cast<TranslatorFactory*>(s_compose.remove(false));
	if (!factory)
	    break;
	compose(factory);
    }
}

void DataTranslator::compose(TranslatorFactory* factory)
{
    const TranslatorCaps* caps = factory->getCapabilities();
    XDebug(DebugInfo,"Composing TranslatorFactory '%s' (%u,'%s'->'%s'->'%s')",
	factory->name(),factory->length(),caps ? caps->src->name : "",
	factory->intermediate() ? factory->intermediate()->name : "",
	caps ? caps->dest->name : "");
    if ((!caps) || (factory->length() >= s_maxChain))
	return;
    Lock lock(s_mutex);
    // now see if we can build some conversion chains with this factory
    ListIterator iter(s_factories);
    while (TranslatorFactory* f2 = static_cast<TranslatorFactory*>(iter.get())) {
	// do not combine with itself
	if (f2 == factory)
	    continue;
	// don't try to build a too long chain
	if ((factory->length() + f2->length()) > s_maxChain) {
	    XDebug(DebugAll,"Chain would be too long");
	    continue;
	}
	const FormatInfo* fi;
	// and avoid loops
	if (factory->intermediate(fi = f2->intermediate()) ||
	    f2->intermediate(fi = factory->intermediate())) {
	    XDebug(DebugAll,"Would directly loop through format '%s'",fi->name);
	    continue;
	}
	const TranslatorCaps* c2 = f2->getCapabilities();
	XDebug(DebugInfo,"Composing '%s' with '%s' (%u,'%s'->'%s'->'%s')",
	    factory->name(),f2->name(),f2->length(),c2 ? c2->src->name : "",
	    f2->intermediate() ? f2->intermediate()->name : "",
	    c2 ? c2->dest->name : "");
	for (; c2 && c2->src && c2->dest; c2++) {
	    if (!(c2->src->converter || c2->dest->converter))
		continue;
	    if (factory->intermediate(fi = c2->src) || factory->intermediate(fi = c2->dest)) {
		XDebug(DebugAll,"Factory '%s' already has intermediate '%s'",factory->name(),fi->name);
		break;
	    }
	    for (const TranslatorCaps* c = caps; c->src && c->dest; c++) {
		if (!(c->src->converter || c->dest->converter))
		    continue;
		if (f2->intermediate(fi = c->src) || f2->intermediate(fi = c->dest)) {
		    XDebug(DebugAll,"Factory '%s' already has intermediate '%s'",f2->name(),fi->name);
		    break;
		}
		if ((c->src == c2->dest) && c->src->converter) {
		    if (canConvert(c2->src,c->dest)) {
			XDebug(DebugAll,"Can already convert '%s' to '%s'",c2->src->name,c->dest->name);
			continue;
		    }
		    DDebug(DebugInfo,"Building chain (%s)%s%s -> (%s) -> %s%s(%s)",
			c2->src->name,
			f2->intermediate() ? " -> " : "",
			f2->intermediate() ? f2->intermediate()->name : "",
			c->src->name,
			factory->intermediate() ? factory->intermediate()->name : "",
			factory->intermediate() ? " -> " : "",
			c->dest->name);
		    new ChainedFactory(f2,factory,c->src);
		    break;
		}
		if ((c2->src == c->dest) && c2->src->converter) {
		    if (canConvert(c->src,c2->dest)) {
			XDebug(DebugAll,"Can already convert '%s' to '%s'",c->src->name,c2->dest->name);
			continue;
		    }
		    DDebug(DebugInfo,"Building chain (%s)%s%s -> (%s) -> %s%s(%s)",
			c->src->name,
			factory->intermediate() ? " -> " : "",
			factory->intermediate() ? factory->intermediate()->name : "",
			c->dest->name,
			f2->intermediate() ? f2->intermediate()->name : "",
			f2->intermediate() ? " -> " : "",
			c2->dest->name);
		    new ChainedFactory(factory,f2,c->dest);
		    break;
		}
	    }
	}
    }
}

void DataTranslator::uninstall(TranslatorFactory* factory)
{
    if (!factory)
	return;
    s_mutex.lock();
    s_compose.remove(factory,false);
    s_factories.remove(factory,false);
    // notify chained factories about the removal
    ListIterator iter(s_factories);
    while (TranslatorFactory* f = static_cast<TranslatorFactory*>(iter.get()))
	f->removed(factory);
    s_mutex.unlock();
}

ObjList* DataTranslator::srcFormats(const DataFormat& dFormat, int maxCost, unsigned int maxLen, ObjList* lst)
{
    const FormatInfo* fi = dFormat.getInfo();
    if (!fi)
	return lst;
    s_mutex.lock();
    compose();
    ObjList* l = s_factories.skipNull();
    for (; l; l=l->skipNext()) {
	TranslatorFactory* f = static_cast<TranslatorFactory*>(l->get());
	if (maxLen && (f->length() > maxLen))
	    continue;
	const TranslatorCaps* caps = f->getCapabilities();
	for (; caps && caps->src && caps->dest; caps++) {
	    if (caps->dest == fi) {
		if ((maxCost >= 0) && (caps->cost > maxCost))
		    continue;
		if (!lst)
		    lst = new ObjList;
		else if (lst->find(caps->src->name))
		    continue;
		lst->append(new String(caps->src->name));
	    }
	}
    }
    s_mutex.unlock();
    return lst;
}

ObjList* DataTranslator::destFormats(const DataFormat& sFormat, int maxCost, unsigned int maxLen, ObjList* lst)
{
    const FormatInfo* fi = sFormat.getInfo();
    if (!fi)
	return lst;
    s_mutex.lock();
    compose();
    ObjList* l = s_factories.skipNull();
    for (; l; l=l->skipNext()) {
	TranslatorFactory* f = static_cast<TranslatorFactory*>(l->get());
	if (maxLen && (f->length() > maxLen))
	    continue;
	const TranslatorCaps* caps = f->getCapabilities();
	for (; caps && caps->src && caps->dest; caps++) {
	    if (caps->src == fi) {
		if ((maxCost >= 0) && (caps->cost > maxCost))
		    continue;
		if (!lst)
		    lst = new ObjList;
		else if (lst->find(caps->dest->name))
		    continue;
		lst->append(new String(caps->dest->name));
	    }
	}
    }
    s_mutex.unlock();
    return lst;
}

// helper function to avoid duplicating large amounts of code
static void mergeOne(ObjList*& lst, const ObjList* formats, const DataFormat& fmt, const FormatInfo* fi, bool sameRate, bool sameChans)
{
    if (!fi)
	return;
    const DataFormat fmti(fi);
    if (lst && lst->find(fmti))
	return;
    if (formats->find(fmti))
	return;
    const FormatInfo* fo = fmt.getInfo();
    if (fo == fi)
	return;
    if (sameRate && (fo->sampleRate != fi->sampleRate))
	return;
    if (sameChans && (fo->numChannels != fi->numChannels))
	return;
    if (DataTranslator::canConvert(fmt,fmti)) {
	if (!lst)
	    lst = new ObjList;
	lst->append(new String(fmti));
    }
}

ObjList* DataTranslator::allFormats(const ObjList* formats, bool existing, bool sameRate, bool sameChans)
{
    if (!formats)
	return 0;
    ObjList* lst = 0;
    s_mutex.lock();
    compose();
    const ObjList* fmts;
    if (existing) {
	// put existing formats first
	for (fmts = formats; fmts; fmts = fmts->next()) {
	    const String* fmt = static_cast<const String*>(fmts->get());
	    if (!(fmt && *fmt))
		continue;
	    const FormatInfo* fo = FormatRepository::getFormat(*fmt);
	    if (fo) {
		if (!lst)
		    lst = new ObjList;
		lst->append(new String(fo->name));
	    }
	}
    }
    for (fmts = formats; fmts; fmts = fmts->next()) {
	const String* fmt = static_cast<const String*>(fmts->get());
	if (!(fmt && *fmt))
	    continue;
	const FormatInfo* fo = FormatRepository::getFormat(*fmt);
	if (!fo)
	    continue;
	const DataFormat fmto(fo);

	// search in the static list first
	for (unsigned int i = 0; i < (sizeof(s_formats)/sizeof(FormatInfo)); i++)
	    mergeOne(lst,formats,fmto,s_formats+i,sameRate,sameChans);
	// then try the installed formats
	for (flist* l = s_flist; l; l = l->next)
	    mergeOne(lst,formats,fmto,l->info,sameRate,sameChans);
    }
    s_mutex.unlock();
    return lst;
}

ObjList* DataTranslator::allFormats(const String& formats, bool existing, bool sameRate, bool sameChans)
{
    ObjList* fmts = formats.split(',',false);
    if (!fmts)
	return 0;
    ObjList* lst = allFormats(fmts,existing,sameRate,sameChans);
    TelEngine::destruct(fmts);
    return lst;
}

bool DataTranslator::canConvert(const DataFormat& fmt1, const DataFormat& fmt2)
{
    if (fmt1 == fmt2)
	return true;
    const FormatInfo* fi1 = fmt1.getInfo();
    const FormatInfo* fi2 = fmt2.getInfo();
    if (!(fi1 && fi2))
	return false;
    Lock lock(s_mutex);
    compose();
    return canConvert(fi1,fi2);
}

bool DataTranslator::canConvert(const FormatInfo* fmt1, const FormatInfo* fmt2)
{
    if (fmt1 == fmt2)
	return true;
    bool ok1 = false, ok2 = false;
    ObjList* l = s_factories.skipNull();
    for (; l; l=l->skipNext()) {
	TranslatorFactory* f = static_cast<TranslatorFactory*>(l->get());
	const TranslatorCaps* caps = f->getCapabilities();
	for (; caps && caps->src && caps->dest; caps++) {
	    if ((!ok1) && (caps->src == fmt1) && (caps->dest == fmt2))
		ok1 = true;
	    if ((!ok2) && (caps->src == fmt2) && (caps->dest == fmt1))
		ok2 = true;
	    if (ok1 && ok2)
		return true;
	}
    }
    return false;
}

int DataTranslator::cost(const DataFormat& sFormat, const DataFormat& dFormat)
{
    int c = -1;
    const FormatInfo* src = sFormat.getInfo();
    const FormatInfo* dest = dFormat.getInfo();
    if (!(src && dest))
	return c;
    s_mutex.lock();
    compose();
    ObjList* l = s_factories.skipNull();
    for (; l; l=l->skipNext()) {
	TranslatorFactory* f = static_cast<TranslatorFactory*>(l->get());
	const TranslatorCaps* caps = f->getCapabilities();
	for (; caps && caps->src && caps->dest; caps++) {
	    if ((c == -1) || (c > caps->cost)) {
		if ((caps->src == src) && (caps->dest == dest))
		    c = caps->cost;
	    }
	}
    }
    s_mutex.unlock();
    return c;
}

DataTranslator* DataTranslator::create(const DataFormat& sFormat, const DataFormat& dFormat)
{
    if (sFormat == dFormat) {
	DDebug(DebugAll,"Not creating identity DataTranslator for '%s'",sFormat.c_str());
	return 0;
    }

    DataTranslator *trans = 0;
    bool counting = getObjCounting();
    NamedCounter* saved = Thread::getCurrentObjCounter(counting);

    s_mutex.lock();
    compose();
    ObjList *l = s_factories.skipNull();
    for (; l; l=l->skipNext()) {
	TranslatorFactory* f = static_cast<TranslatorFactory*>(l->get());
	if (counting)
	    Thread::setCurrentObjCounter(f->objectsCounter());
	trans = f->create(sFormat,dFormat);
	if (trans) {
	    Debug(DebugAll,"Created DataTranslator %p for '%s' -> '%s' by factory %p (len=%u)",
		trans,sFormat.c_str(),dFormat.c_str(),f,f->length());
	    break;
	}
    }
    s_mutex.unlock();
    if (counting)
	Thread::setCurrentObjCounter(saved);

    if (!trans)
	Debug(DebugInfo,"No DataTranslator created for '%s' -> '%s'",
	    sFormat.c_str(),dFormat.c_str());
    return trans;
}

bool DataTranslator::attachChain(DataSource* source, DataConsumer* consumer, bool override)
{
    XDebug(DebugInfo,"DataTranslator::attachChain [%p] '%s' -> [%p] '%s'",
	source,(source ? source->getFormat().c_str() : ""),
	consumer,(consumer ? consumer->getFormat().c_str() : ""));
    if (!source || !consumer)
	return false;
    if (!source->getFormat() || !consumer->getFormat()) {
	Debug(DebugInfo,"DataTranslator::attachChain [%p] '%s' -> [%p] '%s' not possible",
	source,source->getFormat().c_str(),
	consumer,consumer->getFormat().c_str());
	return false;
    }

    bool retv = false;
    // first attempt to connect directly, changing format if possible
    if ((source->getFormat() == consumer->getFormat()) ||
	// don't attempt to change consumer format for overrides
	(!override && consumer->setFormat(source->getFormat())) ||
	source->setFormat(consumer->getFormat())) {
	source->attach(consumer,override);
	retv = true;
    }
    else {
	// then try to create a translator or chain of them
	DataTranslator* trans2 = create(source->getFormat(),consumer->getFormat());
	if (trans2) {
	    DataTranslator* trans = trans2->getFirstTranslator();
	    trans2->getTransSource()->attach(consumer,override);
	    source->attach(trans);
	    trans->deref();
	    retv = true;
	}
    }
    NDebug(retv ? DebugAll : DebugWarn,"DataTranslator::attachChain [%p] '%s' -> [%p] '%s' %s",
	source,source->getFormat().c_str(),consumer,consumer->getFormat().c_str(),
	retv ? "succeeded" : "failed");
    return retv;
}

bool DataTranslator::detachChain(DataSource* source, DataConsumer* consumer)
{
    Debugger debug(DebugAll,"DataTranslator::detachChain","(%p,%p)",source,consumer);
    if (!source || !consumer)
	return false;

    s_consSrcMutex.lock();
    RefPointer<DataSource> tsource = consumer->getConnSource();
    s_consSrcMutex.unlock();
    if (tsource) {
	if (source->detach(consumer))
	    return true;
	tsource->lock();
	RefPointer<DataTranslator> trans = tsource->getTranslator();
	tsource->unlock();
	if (trans && detachChain(source,trans))
	    return true;
	Debug(DebugWarn,"DataTranslator failed to detach chain [%p] -> [%p]",source,consumer);
    }
    return false;
}


TranslatorFactory::~TranslatorFactory()
{
    DataTranslator::uninstall(this);
}

void TranslatorFactory::removed(const TranslatorFactory* factory)
{
}

unsigned int TranslatorFactory::length() const
{
    return 1;
}

const FormatInfo* TranslatorFactory::intermediate() const
{
    return 0;
}

bool TranslatorFactory::intermediate(const FormatInfo* info) const
{
    return false;
}

bool TranslatorFactory::converts(const DataFormat& sFormat, const DataFormat& dFormat) const
{
    const FormatInfo* src = sFormat.getInfo();
    const FormatInfo* dest = dFormat.getInfo();
    const TranslatorCaps* caps = getCapabilities();
    if (!(src && dest && caps))
	return false;
    for (; caps->src && caps->dest; caps++) {
	if ((caps->src == src) && (caps->dest == dest))
	    return true;
    }
    return false;
}


ChainedFactory::ChainedFactory(TranslatorFactory* factory1, TranslatorFactory* factory2, const FormatInfo* info)
    : TranslatorFactory("chained"),
      m_factory1(factory1), m_factory2(factory2), m_format(info),
      m_length(factory1->length()+factory2->length()), m_capabilities(0)
{
    XDebug(DebugInfo,"ChainedFactory::ChainedFactory('%s' (%d,'%s'), '%s' (%d,'%s'),'%s') len=%u [%p]",
	factory1->name(),factory1->length(),factory1->intermediate() ? factory1->intermediate()->name : "",
	factory2->name(),factory2->length(),factory2->intermediate() ? factory2->intermediate()->name : "",
	info->name,m_length,this);
    m_name << factory1->name() << "(" << info->name << ")" << factory2->name();
    if (!info->converter)
	Debug(DebugMild,"Building chain factory '%s' using non-converter format",m_name.c_str());
    const TranslatorCaps* cap1 = factory1->getCapabilities();
    const TranslatorCaps* cap2 = factory2->getCapabilities();
    int c1 = 0;
    int c2 = 0;
    const TranslatorCaps* c;
    for (c = cap1; c && c->src && c->dest; c++)
	if ((c->src == info) || (c->dest == info))
	    c1++;
    for (c = cap2; c && c->src && c->dest; c++)
	if ((c->src == info) || (c->dest == info))
	    c2++;
    // we overallocate
    int ccount = c1 * c2;
    int i = 0;
    TranslatorCaps* caps = new TranslatorCaps[ccount+1];
    for (; cap1 && cap1->src && cap1->dest; cap1++) {
	if (cap1->src == info) {
	    for (c = cap2; c && c->src && c->dest; c++)
		if (c->dest == info) {
		    caps[i].src = c->src;
		    caps[i].dest = cap1->dest;
		    caps[i].cost = cap1->cost + c->cost;
		    XDebug(DebugAll,"Capab[%d] '%s' -> '%s' cost %d",
			i,caps[i].src->name,caps[i].dest->name,caps[i].cost);
		    i++;
		}
	}
	else if (cap1->dest == info) {
	    for (c = cap2; c && c->src && c->dest; c++)
		if (c->src == info) {
		    caps[i].src = cap1->src;
		    caps[i].dest = c->dest;
		    caps[i].cost = cap1->cost + c->cost;
		    XDebug(DebugAll,"Capab[%d] '%s' -> '%s' cost %d",
			i,caps[i].src->name,caps[i].dest->name,caps[i].cost);
		    i++;
		}
	}
    }
    caps[i].src = 0;
    caps[i].dest = 0;
    caps[i].cost = 0;
    m_capabilities = caps;
}

ChainedFactory::~ChainedFactory()
{
    XDebug(DebugInfo,"ChainedFactory::~ChainedFactory() '%s' [%p]",
	m_name.c_str(),this);
    delete[] const_cast<TranslatorCaps*>(m_capabilities);
    m_capabilities = 0;
}

void ChainedFactory::removed(const TranslatorFactory* factory)
{
    if ((factory == m_factory1) || (factory == m_factory2))
	destruct();
}

const FormatInfo* ChainedFactory::intermediate() const
{
    return m_format.getInfo();
}

bool ChainedFactory::intermediate(const FormatInfo* info) const
{
    if (!info)
	return false;
    return (m_format.getInfo() == info) ||
	m_factory1->intermediate(info) ||
	m_factory2->intermediate(info);
}

DataTranslator* ChainedFactory::create(const DataFormat& sFormat, const DataFormat& dFormat)
{
    if (!converts(sFormat,dFormat))
	return 0;
    bool counting = getObjCounting();
    NamedCounter* saved = Thread::getCurrentObjCounter(counting);
    if (counting)
	Thread::setCurrentObjCounter(m_factory1->objectsCounter());
    DataTranslator* trans = m_factory1->create(sFormat,m_format);
    DataTranslator* trans2 = 0;
    if (trans) {
	if (counting)
	    Thread::setCurrentObjCounter(m_factory2->objectsCounter());
	trans2 = m_factory2->create(m_format,dFormat);
    }
    else {
	// try the other way around
	if (counting)
	    Thread::setCurrentObjCounter(m_factory2->objectsCounter());
	trans = m_factory2->create(sFormat,m_format);
	if (!trans) {
	    if (counting)
		Thread::setCurrentObjCounter(saved);
	    return 0;
	}
	if (counting)
	    Thread::setCurrentObjCounter(m_factory1->objectsCounter());
	trans2 = m_factory1->create(m_format,dFormat);
    }
    if (counting)
	Thread::setCurrentObjCounter(saved);

    if (trans2) {
	XDebug(DebugInfo,"Chaining translators: '%s' %p --(%s)-> %p '%s' [%p]",
	    sFormat.c_str(),trans,m_format.c_str(),trans2,dFormat.c_str(),this);
	// trans2 may be a chain itself so find the first translator
	DataTranslator* trans1 = trans2->getFirstTranslator();
	trans->getTransSource()->attach(trans1);
	trans1->deref();
    }
    else
	// trans may be a chain itself so clear from first translator
	trans->getFirstTranslator()->destruct();
    return trans2;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
