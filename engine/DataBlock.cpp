/**
 * DataBlock.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004 Null Team
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

#include "telephony.h"

#include <string.h>
#include <stdlib.h>

namespace TelEngine{

extern "C" {
#include "tables/all.h"
}

class ThreadedSourcePrivate : public Thread
{
public:
    ThreadedSourcePrivate(ThreadedSource *source, const char *name)
	: Thread(name), m_source(source) { }

protected:
    virtual void run()
	{ m_source->run(); }

    virtual void cleanup()
	{ m_source->m_thread = 0; m_source->cleanup(); }

private:
    ThreadedSource *m_source;
};

class SimpleTranslator : public DataTranslator
{
public:
    SimpleTranslator(const String &sFormat, const String &dFormat)
	: DataTranslator(sFormat,dFormat) { }
    virtual void Consume(const DataBlock &data, unsigned long timeDelta)
	{
	    ref();
	    if (getTransSource()) {
		DataBlock oblock;
		if (oblock.convert(data, m_format, getTransSource()->getFormat()))
		    getTransSource()->Forward(oblock, timeDelta);
	    }
	    deref();
	}
};

};

using namespace TelEngine;

DataBlock::DataBlock()
    : m_data(0), m_length(0)
{
}

DataBlock::DataBlock(const DataBlock &value)
    : m_data(0), m_length(0)
{
    assign(value.data(),value.length());
}

DataBlock::DataBlock(void *value, unsigned int len, bool copyData)
    : m_data(0), m_length(0)
{
    assign(value,len,copyData);
}

DataBlock::~DataBlock()
{
    clear();
}

void DataBlock::clear(bool deleteData)
{
    m_length = 0;
    if (m_data) {
	void *data = m_data;
	m_data = 0;
	if (deleteData)
	    ::free(data);
    }
}

DataBlock& DataBlock::assign(void *value, unsigned int len, bool copyData)
{
    if ((value != m_data) || (len != m_length)) {
	void *odata = m_data;
	m_length = 0;
	m_data = 0;
	if (len) {
	    if (copyData) {
		void *data = ::malloc(len);
		if (value)
		    ::memcpy(data,value,len);
		else
		    ::memset(data,0,len);
		m_data = data;
	    }
	    else
		m_data = value;
	    if (m_data)
		m_length = len;
	}
	if (odata && (odata != m_data))
	    ::free(odata);
    }
    return *this;
}

void DataBlock::truncate(unsigned int len)
{
    if (!len)
	clear();
    else if (len < m_length)
	assign(m_data,len);
}

void DataBlock::cut(int len)
{
    if (!len)
	return;

    int ofs = 0;
    if (len < 0)
	ofs = len = -len;

    if ((unsigned)len >= m_length) {
	clear();
	return;
    }

    assign(ofs+(char *)m_data,m_length - len);
}

DataBlock& DataBlock::operator=(const DataBlock &value)
{
    assign(value.data(),value.length());
    return *this;
}

void DataBlock::append(const DataBlock &value)
{
    if (m_length) {
	if (value.length()) {
	    unsigned int len = m_length+value.length();
	    void *data = ::malloc(len);
	    ::memcpy(data,m_data,m_length);
	    ::memcpy(m_length+(char*)data,value.data(),value.length());
	    assign(data,len,false);
	}
    }
    else
	assign(value.data(),value.length());
}

void DataBlock::insert(const DataBlock &value)
{
    unsigned int vl = value.length();
    if (m_length) {
	if (vl) {
	    unsigned int len = m_length+vl;
	    void *data = ::malloc(len);
	    ::memcpy(data,value.data(),vl);
	    ::memcpy(vl+(char*)data,m_data,m_length);
	    assign(data,len,false);
	}
    }
    else
	assign(value.data(),vl);
}

bool DataBlock::convert(const DataBlock &src, const String &sFormat,
    const String &dFormat, unsigned maxlen)
{
    if (sFormat == dFormat) {
	operator=(src);
	return true;
    }
    unsigned sl = 0, dl = 0;
    void *ctable = 0;
    if (sFormat == "slin") {
	sl = 2;
	dl = 1;
	if (dFormat == "alaw")
	    ctable = s2a;
	else if (dFormat == "mulaw")
	    ctable = s2u;
    }
    else if (sFormat == "alaw") {
	sl = 1;
	if (dFormat == "mulaw") {
	    dl = 1;
	    ctable = a2u;
	}
	else if (dFormat == "slin") {
	    dl = 2;
	    ctable = a2s;
	}
    }
    else if (sFormat == "mulaw") {
	sl = 1;
	if (dFormat == "alaw") {
	    dl = 1;
	    ctable = u2a;
	}
	else if (dFormat == "slin") {
	    dl = 2;
	    ctable = u2s;
	}
    }
    clear();
    if (!ctable)
	return false;
    unsigned len = src.length();
    if (maxlen && (maxlen < len))
	len = maxlen;
    len /= sl;
    if (!len)
	return true;
    assign(0,len*dl);
    if ((sl == 1) && (dl == 1)) {
	unsigned char *s = (unsigned char *) src.data();
	unsigned char *d = (unsigned char *) data();
	unsigned char *c = (unsigned char *) ctable;
	while (len--)
	    *d++ = c[*s++];
    }
    else if ((sl == 1) && (dl == 2)) {
	unsigned char *s = (unsigned char *) src.data();
	unsigned short *d = (unsigned short *) data();
	unsigned short *c = (unsigned short *) ctable;
	while (len--)
	    *d++ = c[*s++];
    }
    else if ((sl == 2) && (dl == 1)) {
	unsigned short *s = (unsigned short *) src.data();
	unsigned char *d = (unsigned char *) data();
	unsigned char *c = (unsigned char *) ctable;
	while (len--)
	    *d++ = c[*s++];
    }
    return true;
}

void DataSource::Forward(const DataBlock &data, unsigned long timeDelta)
{
    Lock lock(m_mutex);
    ref();
    ObjList *l = &m_consumers;
    for (; l; l=l->next()) {
	DataConsumer *c = static_cast<DataConsumer *>(l->get());
	if (c)
	    c->Consume(data,timeDelta);
    }
    m_timestamp += timeDelta;
    deref();
}

bool DataSource::attach(DataConsumer *consumer)
{
    DDebug(DebugInfo,"DataSource [%p] attaching consumer [%p]",this,consumer);
    if (!consumer)
	return false;
    Lock lock(m_mutex);
    consumer->ref();
    if (consumer->getConnSource())
	consumer->getConnSource()->detach(consumer);
    m_consumers.append(consumer);
    consumer->setSource(this);
    return true;
}

bool DataSource::detach(DataConsumer *consumer)
{
    DDebug(DebugInfo,"DataSource [%p] detaching consumer [%p]",this,consumer);
    if (!consumer)
	return false;
    Lock lock(m_mutex);
    DataConsumer *temp = static_cast<DataConsumer *>(m_consumers.remove(consumer,false));
    if (temp) {
	temp->setSource(0);
	temp->deref();
	return true;
    }
    DDebug(DebugWarn,"DataSource [%p] has no consumer [%p]",this,consumer);
    return false;
}

DataSource::~DataSource()
{
    while (detach(static_cast<DataConsumer *>(m_consumers.get()))) ;
}

DataEndpoint::~DataEndpoint()
{
    disconnect();
    setSource();
    setConsumer();
}

bool DataEndpoint::connect(DataEndpoint *peer)
{
    Debug(DebugInfo,"DataEndpoint peer address is [%p]",peer);
    if (!peer) {
	disconnect();
	return false;
    }
    if (peer == m_peer)
	return true;

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

	s = peer->getSource();
	c = getConsumer();
	if (s && c)
	    DataTranslator::attachChain(s,c);
    }

    m_peer = peer;
    peer->setPeer(this);
    connected();

    return true;
}

void DataEndpoint::disconnect(const char *reason)
{
    if (!m_peer)
	return;

    DataSource *s = getSource();
    DataConsumer *c = m_peer->getConsumer();
    if (s && c)
	DataTranslator::detachChain(s,c);

    s = m_peer->getSource();
    c = getConsumer();
    if (s && c)
	DataTranslator::detachChain(s,c);

    DataEndpoint *temp = m_peer;
    m_peer = 0;
    temp->setPeer(0,reason);
    temp->deref();
    disconnected(reason);
    deref();
}

void DataEndpoint::setPeer(DataEndpoint *peer, const char *reason)
{
    m_peer = peer;
    if (m_peer)
	connected();
    else
	disconnected(reason);
}

void DataEndpoint::setSource(DataSource *source)
{
    if (source == m_source)
	return;
    DataConsumer *consumer = m_peer ? m_peer->getConsumer() : 0;
    DataSource *temp = m_source;
    if (consumer)
	consumer->ref();
    m_source = 0;
    if (temp) {
	if (consumer) {
	    DataTranslator::detachChain(temp,consumer);
	    if (consumer->getConnSource())
		Debug(DebugWarn,"consumer source not cleared in %p",consumer);
	}
	temp->deref();
    }
    if (source) {
	source->ref();
	if (consumer)
	    DataTranslator::attachChain(source,consumer);
    }
    m_source = source;
    if (consumer)
	consumer->deref();
}

void DataEndpoint::setConsumer(DataConsumer *consumer)
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

ThreadedSource::~ThreadedSource()
{
    stop();
}

bool ThreadedSource::start(const char *name)
{
    if (!m_thread) {
	m_thread = new ThreadedSourcePrivate(this,name);
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

Thread *ThreadedSource::thread() const
{
    return m_thread;
}

DataTranslator::DataTranslator(const char *sFormat, const char *dFormat)
    : DataConsumer(sFormat)
{
    m_tsource = new DataSource(dFormat);
    m_tsource->setTranslator(this);
}

DataTranslator::DataTranslator(const char *sFormat, DataSource *source)
    : DataConsumer(sFormat), m_tsource(source)
{
    m_tsource->setTranslator(this);
}

DataTranslator::~DataTranslator()
{
    DataSource *temp = m_tsource;
    m_tsource = 0;
    if (temp) {
	temp->setTranslator(0);
	temp->deref();
    }
}

Mutex DataTranslator::s_mutex;
ObjList DataTranslator::s_factories;

void DataTranslator::install(TranslatorFactory *factory)
{
    s_mutex.lock();
    s_factories.append(factory);
    s_mutex.unlock();
}

void DataTranslator::uninstall(TranslatorFactory *factory)
{
    s_mutex.lock();
    s_factories.remove(factory,false);
    s_mutex.unlock();
}

String DataTranslator::srcFormats(const String &dFormat)
{
    String s;
    s_mutex.lock();
    ObjList *l = &s_factories;
    for (; l; l=l->next()) {
	TranslatorFactory *f = static_cast<TranslatorFactory *>(l->get());
	if (f) {
	    const TranslatorCaps *caps = f->getCapabilities();
	    for (; caps && caps->src.name; caps++) {
		if (dFormat == caps->dest.name) {
		    if (!s.null())
			s << " ";
		    s << caps->src.name << "/" << caps->cost;
		}
	    }
	}
    }
    s_mutex.unlock();
    return s;
}

String DataTranslator::destFormats(const String &sFormat)
{
    String s;
    s_mutex.lock();
    ObjList *l = &s_factories;
    for (; l; l=l->next()) {
	TranslatorFactory *f = static_cast<TranslatorFactory *>(l->get());
	if (f) {
	    const TranslatorCaps *caps = f->getCapabilities();
	    for (; caps && caps->src.name; caps++) {
		if (sFormat == caps->src.name) {
		    if (!s.null())
			s << " ";
		    s << caps->dest.name << "/" << caps->cost;
		}
	    }
	}
    }
    s_mutex.unlock();
    return s;
}

int DataTranslator::cost(const String &sFormat, const String &dFormat)
{
    int c = -1;
    s_mutex.lock();
    ObjList *l = &s_factories;
    for (; l; l=l->next()) {
	TranslatorFactory *f = static_cast<TranslatorFactory *>(l->get());
	if (f) {
	    const TranslatorCaps *caps = f->getCapabilities();
	    for (; caps && caps->src.name; caps++) {
		if ((c == -1) || (c > caps->cost)) {
		    if ((sFormat == caps->src.name) && (dFormat == caps->dest.name))
			c = caps->cost;
		}
	    }
	}
    }
    s_mutex.unlock();
    return c;
}

DataTranslator *DataTranslator::create(const String &sFormat, const String &dFormat)
{
    if (sFormat == dFormat) {
	Debug(DebugInfo,"Not creating identity DataTranslator for \"%s\"",sFormat.c_str());
	return 0;
    }

    DataTranslator *trans = 0;

    s_mutex.lock();
    ObjList *l = &s_factories;
    for (; l; l=l->next()) {
	TranslatorFactory *f = static_cast<TranslatorFactory *>(l->get());
	if (f) {
	    trans = f->create(sFormat,dFormat);
	    if (trans)
		break;
	}
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
    else {
	int level = DebugWarn;
	if (sFormat.null() || dFormat.null())
	    level = DebugInfo;
	Debug(level,"No DataTranslator created for \"%s\" -> \"%s\"",
	    sFormat.c_str(),dFormat.c_str());
    }
    return trans;
}

bool DataTranslator::attachChain(DataSource *source, DataConsumer *consumer)
{
    if (!source || !consumer)
	return false;

    bool retv = false;
    if (source->getFormat() == consumer->getFormat()) {
	source->attach(consumer);
	retv = true;
    }
    else {
	// TODO: try to create a chain of translators, recurse if we have to
	DataTranslator *trans = create(source->getFormat(),consumer->getFormat());
	if (trans) {
	    trans->getTransSource()->attach(consumer);
	    source->attach(trans);
	    retv = true;
	}
    }
    NDebug(DebugAll,"DataTranslator::attachChain [%p] \"%s\" -> [%p] \"%s\" %s",
	source,source->getFormat().c_str(),consumer,consumer->getFormat().c_str(),
	retv ? "succeeded" : "failed");
    return retv;
}

bool DataTranslator::detachChain(DataSource *source, DataConsumer *consumer)
{
    Debugger debug(DebugAll,"DataTranslator::detachChain","(%p,%p)",source,consumer);
    if (!source || !consumer)
	return false;

    DataSource *tsource = consumer->getConnSource();
    if (tsource) {
	if (source->detach(consumer))
	    return true;
	DataTranslator *trans = tsource->getTranslator();
	if (trans && detachChain(source,trans)) {
	    trans->deref();
	    return true;
	}
	Debug(DebugWarn,"DataTranslator failed to detach chain [%p] -> [%p]",source,consumer);
    }
    return false;
}
