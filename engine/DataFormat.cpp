/**
 * DataFormat.cpp
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

#include "yatephone.h"

#include <string.h>
#include <stdlib.h>

namespace TelEngine {

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
		if (oblock.convert(data, m_format, getTransSource()->getFormat())) {
		    if (!timeDelta) {
			timeDelta = data.length();
			if (timeDelta > oblock.length())
			    timeDelta = oblock.length();
		    }
		    getTransSource()->Forward(oblock, timeDelta);
		}
	    }
	    deref();
	}
};

};

using namespace TelEngine;


int FormatInfo::guessSamples(int len) const
{
    if (!dataRate)
	return 0;
    if (frameSize)
	len = frameSize * (len / frameSize);
    return len * sampleRate / dataRate;
}

typedef struct _flist {
    struct _flist* next;
    const FormatInfo* info;
} flist;

static const FormatInfo s_formats[] = {
    FormatInfo("slin", 16000),
    FormatInfo("alaw", 8000),
    FormatInfo("mulaw", 8000),
    FormatInfo("gsm", 1650, 33),
    FormatInfo("ilbc", 1667, 50),
    FormatInfo("speex", 0),
    FormatInfo("adpcm", 4000),
    FormatInfo("g723", 0),
    FormatInfo("g726", 4000),
    FormatInfo("g729", 1000, 20),
    FormatInfo("plain", 0, 0, "text", 0),
};

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

const FormatInfo* FormatRepository::addFormat(const String& name, int drate, int fsize, const String& type, int srate, int nchan)
{
    if (name.null() || type.null())
	return 0;

    const FormatInfo* f = getFormat(name);
    if (f) {
	// found by name - check if it exactly matches what we have already
	if ((drate != f->dataRate) ||
	    (fsize != f->frameSize) ||
	    (srate != f->sampleRate) ||
	    (nchan != f->numChannels) ||
	    (type != f->type)) {
		Debug(DebugWarn,"Tried to register '%s' format '%s' drate=%d fsize=%d srate=%d nchan=%d",
		    type.c_str(),name.c_str(),drate,fsize,srate,nchan);
		return 0;
	}
	return f;
    }
    // not in list - add a new one to the installed formats
    Debug(DebugInfo,"Registering '%s' format '%s' drate=%d fsize=%d srate=%d nchan=%d",
	type.c_str(),name.c_str(),drate,fsize,srate,nchan);
    f = new FormatInfo(::strdup(name),drate,fsize,::strdup(type),srate,nchan);
    flist* l = new flist;
    l->info = f;
    l->next = s_flist;
    s_flist = l;
    return f;
}

void DataSource::Forward(const DataBlock &data, unsigned long timeDelta)
{
    // no number of samples provided - try to guess
    if (!timeDelta) {
	const FormatInfo* f = FormatRepository::getFormat(m_format);
	if (f)
	    timeDelta = f->guessSamples(data.length());
    }
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
    // keep the source locked to prevent races with the Forward method
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
    disconnect(true,0);
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

void DataEndpoint::disconnect(bool final, const char *reason)
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
    disconnected(final,reason);
    deref();
}

void DataEndpoint::setPeer(DataEndpoint *peer, const char *reason)
{
    m_peer = peer;
    if (m_peer)
	connected();
    else
	disconnected(false,reason);
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
	    for (; caps && caps->src && caps->dest; caps++) {
		if (dFormat == caps->dest->name) {
		    if (!s.null())
			s << " ";
		    s << caps->src->name << "@" << caps->cost;
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
	    for (; caps && caps->src && caps->dest; caps++) {
		if (sFormat == caps->src->name) {
		    if (!s.null())
			s << " ";
		    s << caps->dest->name << "@" << caps->cost;
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
	    for (; caps && caps->src && caps->dest; caps++) {
		if ((c == -1) || (c > caps->cost)) {
		    if ((sFormat == caps->src->name) && (dFormat == caps->dest->name))
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
    else
	Debug(DebugInfo,"No DataTranslator created for \"%s\" -> \"%s\"",
	    sFormat.c_str(),dFormat.c_str());
    return trans;
}

bool DataTranslator::attachChain(DataSource *source, DataConsumer *consumer)
{
    if (!source || !consumer || source->getFormat().null() || consumer->getFormat().null())
	return false;

    bool retv = false;
    // first attempt to connect directly, changing format if possible
    if ((source->getFormat() == consumer->getFormat()) ||
	consumer->setFormat(source->getFormat()) ||
	source->setFormat(consumer->getFormat())) {
	source->attach(consumer);
	retv = true;
    }
    else {
	// next, try to create a single translator
	DataTranslator *trans = create(source->getFormat(),consumer->getFormat());
	if (trans) {
	    trans->getTransSource()->attach(consumer);
	    source->attach(trans);
	    retv = true;
	}
	// finally, try to convert trough "slin" if possible
	else if ((source->getFormat() != "slin") && (consumer->getFormat() != "slin")) {
	    trans = create(source->getFormat(),"slin");
	    if (trans) {
		DataTranslator *trans2 = create("slin",consumer->getFormat());
		if (trans2) {
		    trans2->getTransSource()->attach(consumer);
		    trans->getTransSource()->attach(trans2);
		    source->attach(trans);
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
