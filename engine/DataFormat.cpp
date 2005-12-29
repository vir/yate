/**
 * DataFormat.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004, 2005 Null Team
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "yatephone.h"

#include <string.h>
#include <stdlib.h>

namespace TelEngine {

class ThreadedSourcePrivate : public Thread
{
public:
    ThreadedSourcePrivate(ThreadedSource *source, const char *name, Thread::Priority prio)
	: Thread(name,prio), m_source(source) { }

protected:
    virtual void run()
	{ m_source->run(); }

    virtual void cleanup()
	{ m_source->m_thread = 0; m_source->cleanup(); }

private:
    ThreadedSource* m_source;
};

class SimpleTranslator : public DataTranslator
{
public:
    SimpleTranslator(const DataFormat& sFormat, const DataFormat& dFormat)
	: DataTranslator(sFormat,dFormat) { }
    virtual void Consume(const DataBlock& data, unsigned long tStamp)
	{
	    ref();
	    if (getTransSource()) {
		DataBlock oblock;
		if (oblock.convert(data, m_format, getTransSource()->getFormat())) {
		    if (!tStamp) {
			unsigned int delta = data.length();
			if (delta > oblock.length())
			    delta = oblock.length();
			tStamp = m_timestamp + delta;
		    }
		    m_timestamp = tStamp;
		    getTransSource()->Forward(oblock, tStamp);
		}
	    }
	    deref();
	}
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

static const FormatInfo s_formats[] = {
    FormatInfo("slin", 160),
    FormatInfo("alaw", 80),
    FormatInfo("mulaw", 80),
    FormatInfo("gsm", 33, 20000),
    FormatInfo("ilbc20", 38, 20000),
    FormatInfo("ilbc30", 50, 30000),
//    FormatInfo("speex", 0),
//    FormatInfo("g729", 10, 10000),
    FormatInfo("plain", 0, 0, "text", 0),
    FormatInfo("raw", 0, 0, "data", 0),
};

static flist* s_flist = 0;
static const char s_slin[] = "slin";

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


DataConsumer::~DataConsumer()
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
}

void* DataConsumer::getObject(const String& name) const
{
    if (name == "DataConsumer")
	return const_cast<DataConsumer*>(this);
    return DataNode::getObject(name);
}

void DataConsumer::Consume(const DataBlock& data, unsigned long tStamp, DataSource* source)
{
    if (source == m_override)
	tStamp += m_overrideTsDelta;
    else if (m_override || (source != m_source))
	return;
    Consume(data,tStamp);
    m_timestamp = tStamp;
}


void DataSource::Forward(const DataBlock& data, unsigned long tStamp)
{
    Lock lock(m_mutex);
    // no timestamp provided - try to guess
    if (!tStamp) {
	tStamp = m_timestamp;
	const FormatInfo* f = m_format.getInfo();
	if (f)
	    tStamp += f->guessSamples(data.length());
    }
    ref();
    ObjList *l = m_consumers.skipNull();
    for (; l; l=l->skipNext()) {
	DataConsumer *c = static_cast<DataConsumer *>(l->get());
	c->Consume(data,tStamp,this);
    }
    m_timestamp = tStamp;
    deref();
}

bool DataSource::attach(DataConsumer* consumer, bool override)
{
    DDebug(DebugAll,"DataSource [%p] attaching consumer%s [%p]",
	this,(override ? " as override" : ""),consumer);
    if (!consumer)
	return false;
    Lock lock(m_mutex);
    consumer->ref();
    if (override) {
	if (consumer->m_override)
	    consumer->m_override->detach(consumer);
	consumer->m_override = this;
	consumer->m_overrideTsDelta = consumer->m_timestamp - m_timestamp;
    }
    else {
	if (consumer->m_source)
	    consumer->m_source->detach(consumer);
	consumer->m_source = this;
    }
    m_consumers.append(consumer);
    return true;
}

bool DataSource::detach(DataConsumer* consumer)
{
    if (!consumer)
	return false;
    DDebug(DebugAll,"DataSource [%p] detaching consumer [%p]",this,consumer);
    // lock the source to prevent races with the Forward method
    m_mutex.lock();
    bool ok = detachInternal(consumer);
    m_mutex.unlock();
    return ok;
}

bool DataSource::detachInternal(DataConsumer* consumer)
{
    if (!consumer)
	return false;
    DataConsumer *temp = static_cast<DataConsumer *>(m_consumers.remove(consumer,false));
    if (temp) {
	if (temp->m_source == this)
	    temp->m_source = 0;
	if (temp->m_override == this)
	    temp->m_override = 0;
	temp->deref();
	return true;
    }
    DDebug(DebugInfo,"DataSource [%p] has no consumer [%p]",this,consumer);
    return false;
}

DataSource::~DataSource()
{
    clear();
}

void DataSource::clear()
{
    // keep the source locked to prevent races with the Forward method
    m_mutex.lock();
    while (detachInternal(static_cast<DataConsumer*>(m_consumers.get())))
	;
    m_mutex.unlock();
}

void* DataSource::getObject(const String& name) const
{
    if (name == "DataSource")
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

DataEndpoint::~DataEndpoint()
{
    DDebug(DebugAll,"DataEndpoint::~DataEndpoint() '%s' call=%p [%p]",
	m_name.c_str(),m_call,this);
    if (m_call)
	m_call->m_data.remove(this,false);
    disconnect();
    setPeerRecord();
    setCallRecord();
    setSource();
    setConsumer();
}

void* DataEndpoint::getObject(const String& name) const
{
    if (name == "DataEndpoint")
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

bool DataEndpoint::connect(DataEndpoint* peer)
{
    if (!peer) {
	disconnect();
	return false;
    }
    if (peer == m_peer)
	return true;
    DDebug(DebugInfo,"DataEndpoint '%s' connecting peer %p to [%p]",m_name.c_str(),peer,this);

    ref();
    disconnect();
    peer->ref();
    peer->disconnect();
    bool native = (name() == peer->name()) && nativeConnect(peer);

    if (!native) {
	DataSource *s = getSource();
	DataConsumer *c = peer->getConsumer();
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
    if (!m_peer)
	return false;
    DDebug(DebugInfo,"DataEndpoint '%s' disconnecting peer %p from [%p]",m_name.c_str(),m_peer,this);

    DataSource *s = getSource();
    DataConsumer *c = m_peer->getConsumer();
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

    DataEndpoint *temp = m_peer;
    m_peer = 0;
    temp->m_peer = 0;
    temp->deref();
    return deref();
}

void DataEndpoint::setSource(DataSource* source)
{
    if (source == m_source)
	return;
    DataConsumer *c1 = m_peer ? m_peer->getConsumer() : 0;
    DataConsumer *c2 = m_peer ? m_peer->getPeerRecord() : 0;
    DataSource *temp = m_source;
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
	temp->deref();
    }
    if (source) {
	source->ref();
	if (c1)
	    DataTranslator::attachChain(source,c1);
	if (c2)
	    DataTranslator::attachChain(source,c2);
	if (m_callRecord)
	    DataTranslator::attachChain(source,m_callRecord);
    }
    m_source = source;
    if (c1)
	c1->deref();
    if (c2)
	c2->deref();
    if (m_callRecord)
	m_callRecord->deref();
}

void DataEndpoint::setConsumer(DataConsumer* consumer)
{
    if (consumer == m_consumer)
	return;
    DataSource *source = m_peer ? m_peer->getSource() : 0;
    DataConsumer *temp = m_consumer;
    if (consumer) {
	consumer->ref();
	if (source)
	    DataTranslator::attachChain(source,consumer);
    }
    m_consumer = consumer;
    if (temp) {
	if (source)
	    DataTranslator::detachChain(source,temp);
	temp->deref();
    }
}

void DataEndpoint::setPeerRecord(DataConsumer* consumer)
{
    if (consumer == m_peerRecord)
	return;
    DataSource *source = m_peer ? m_peer->getSource() : 0;
    DataConsumer *temp = m_peerRecord;
    if (consumer) {
	consumer->ref();
	if (source)
	    DataTranslator::attachChain(source,consumer);
    }
    m_peerRecord = consumer;
    if (temp) {
	if (source)
	    DataTranslator::detachChain(source,temp);
	temp->deref();
    }
}

void DataEndpoint::setCallRecord(DataConsumer* consumer)
{
    if (consumer == m_callRecord)
	return;
    DataConsumer *temp = m_callRecord;
    if (consumer) {
	consumer->ref();
	if (m_source)
	    DataTranslator::attachChain(m_source,consumer);
    }
    m_callRecord = consumer;
    if (temp) {
	if (m_source)
	    DataTranslator::detachChain(m_source,temp);
	temp->deref();
    }
}


ThreadedSource::~ThreadedSource()
{
    stop();
}

bool ThreadedSource::start(const char* name, Thread::Priority prio)
{
    if (!m_thread) {
	m_thread = new ThreadedSourcePrivate(this,name,prio);
	m_thread->startup();
    }
    return m_thread->running();
}

void ThreadedSource::stop()
{
    if (m_thread) {
	delete m_thread;
	m_thread = 0;
    }
}

void ThreadedSource::cleanup()
{
}

Thread* ThreadedSource::thread() const
{
    return m_thread;
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
    if (name == "DataTranslator")
	return const_cast<DataTranslator*>(this);
    return DataConsumer::getObject(name);
}

Mutex DataTranslator::s_mutex;
ObjList DataTranslator::s_factories;

void DataTranslator::install(TranslatorFactory* factory)
{
    s_mutex.lock();
    s_factories.append(factory);
    s_mutex.unlock();
}

void DataTranslator::uninstall(TranslatorFactory* factory)
{
    s_mutex.lock();
    s_factories.remove(factory,false);
    s_mutex.unlock();
}

String DataTranslator::srcFormats(const DataFormat& dFormat)
{
    String s;
    s_mutex.lock();
    ObjList *l = s_factories.skipNull();
    for (; l; l=l->skipNext()) {
	TranslatorFactory *f = static_cast<TranslatorFactory *>(l->get());
	const TranslatorCaps *caps = f->getCapabilities();
	for (; caps && caps->src && caps->dest; caps++) {
	    if (dFormat == caps->dest->name) {
		if (!s.null())
		    s << " ";
		s << caps->src->name << "@" << caps->cost;
	    }
	}
    }
    s_mutex.unlock();
    return s;
}

String DataTranslator::destFormats(const DataFormat& sFormat)
{
    String s;
    s_mutex.lock();
    ObjList *l = s_factories.skipNull();
    for (; l; l=l->skipNext()) {
	TranslatorFactory *f = static_cast<TranslatorFactory *>(l->get());
	const TranslatorCaps *caps = f->getCapabilities();
	for (; caps && caps->src && caps->dest; caps++) {
	    if (sFormat == caps->src->name) {
		if (!s.null())
		    s << " ";
		s << caps->dest->name << "@" << caps->cost;
	    }
	}
    }
    s_mutex.unlock();
    return s;
}

bool DataTranslator::canConvert(const DataFormat& fmt1, const DataFormat& fmt2)
{
    if (fmt1 == fmt2)
	return true;
    // check conversions provided by SimpleTranslator
    if (((fmt1 == "slin") || (fmt1 == "alaw") || (fmt1 == "mulaw")) &&
	((fmt2 == "slin") || (fmt2 == "alaw") || (fmt2 == "mulaw")))
	return true;
    bool ok1 = false, ok2 = false;
    Lock lock(s_mutex);
    ObjList *l = s_factories.skipNull();
    for (; l; l=l->skipNext()) {
	TranslatorFactory *f = static_cast<TranslatorFactory *>(l->get());
	const TranslatorCaps *caps = f->getCapabilities();
	for (; caps && caps->src && caps->dest; caps++) {
	    if ((!ok1) && (fmt1 == caps->src->name) && (fmt2 == caps->dest->name))
		ok1 = true;
	    if ((!ok2) && (fmt2 == caps->src->name) && (fmt1 == caps->dest->name))
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
    s_mutex.lock();
    ObjList *l = s_factories.skipNull();
    for (; l; l=l->skipNext()) {
	TranslatorFactory *f = static_cast<TranslatorFactory *>(l->get());
	const TranslatorCaps *caps = f->getCapabilities();
	for (; caps && caps->src && caps->dest; caps++) {
	    if ((c == -1) || (c > caps->cost)) {
		if ((sFormat == caps->src->name) && (dFormat == caps->dest->name))
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
	DDebug(DebugAll,"Not creating identity DataTranslator for \"%s\"",sFormat.c_str());
	return 0;
    }

    DataTranslator *trans = 0;

    s_mutex.lock();
    ObjList *l = s_factories.skipNull();
    for (; l; l=l->skipNext()) {
	TranslatorFactory *f = static_cast<TranslatorFactory *>(l->get());
	trans = f->create(sFormat,dFormat);
	if (trans)
	    break;
    }
    s_mutex.unlock();


    if (!trans) {
	DataBlock empty,probe;
	if (probe.convert(empty,sFormat,dFormat))
	    trans = new SimpleTranslator(sFormat,dFormat);
    }

    if (trans)
	Debug(DebugAll,"Created DataTranslator [%p] for \"%s\" -> \"%s\"",
	    trans,sFormat.c_str(),dFormat.c_str());
    else
	Debug(DebugInfo,"No DataTranslator created for \"%s\" -> \"%s\"",
	    sFormat.c_str(),dFormat.c_str());
    return trans;
}

bool DataTranslator::attachChain(DataSource* source, DataConsumer* consumer, bool override)
{
    if (!source || !consumer || !source->getFormat() || !consumer->getFormat())
	return false;

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
	// next, try to create a single translator
	DataTranslator *trans = create(source->getFormat(),consumer->getFormat());
	if (trans) {
	    trans->getTransSource()->attach(consumer,override);
	    source->attach(trans);
	    trans->deref();
	    retv = true;
	}
	// finally, try to convert trough "slin" if possible
	else if ((source->getFormat() != s_slin) && (consumer->getFormat() != s_slin)) {
	    trans = create(source->getFormat(),s_slin);
	    if (trans) {
		DataTranslator *trans2 = create(s_slin,consumer->getFormat());
		if (trans2) {
		    trans2->getTransSource()->attach(consumer,override);
		    trans->getTransSource()->attach(trans2);
		    source->attach(trans);
		    trans2->deref();
		    trans->deref();
		    retv = true;
		}
		else
		    trans->destruct();
	    }
	}
    }
    NDebug(retv ? DebugAll : DebugWarn,"DataTranslator::attachChain [%p] \"%s\" -> [%p] \"%s\" %s",
	source,source->getFormat().c_str(),consumer,consumer->getFormat().c_str(),
	retv ? "succeeded" : "failed");
    return retv;
}

bool DataTranslator::detachChain(DataSource* source, DataConsumer* consumer)
{
    Debugger debug(DebugAll,"DataTranslator::detachChain","(%p,%p)",source,consumer);
    if (!source || !consumer)
	return false;

    DataSource *tsource = consumer->getConnSource();
    if (tsource) {
	if (source->detach(consumer))
	    return true;
	DataTranslator *trans = tsource->getTranslator();
	if (trans && detachChain(source,trans))
	    return true;
	Debug(DebugWarn,"DataTranslator failed to detach chain [%p] -> [%p]",source,consumer);
    }
    return false;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
