/**
 * conference.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Conference room data mixer
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

#include <yatephone.h>

#include <stdlib.h>

using namespace TelEngine;

#define DATA_CHUNK 320
#define MIN_BUFFER 960
#define MAX_BUFFER 1600

static ObjList s_rooms;

class ConfRoom : public DataSource
{
public:
    ConfRoom(const String& name);
    ~ConfRoom();
    static ConfRoom* create(const String& name);
    virtual const String& toString() const
	{ return m_name; }
    inline ObjList& channels()
	{ return m_chans; }
    void mix();
private:
    String m_name;
    ObjList m_chans;
};

class ConfChan : public Channel
{
public:
    ConfChan(const String& name);
    ~ConfChan();
};

class ConfConsumer : public DataConsumer
{
    friend class ConfRoom;
public:
    ConfConsumer(ConfRoom* room)
	: m_room(room)
	{ }
    ~ConfConsumer()
	{ }
    virtual void Consume(const DataBlock& data, unsigned long timeDelta);
private:
    ConfRoom* m_room;
    DataBlock m_buffer;
};

class ConferenceDriver : public Driver
{
public:
    ConferenceDriver();
    virtual ~ConferenceDriver();
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest);
};

INIT_PLUGIN(ConferenceDriver);

ConfRoom* ConfRoom::create(const String& name)
{
    if (name.null())
	return 0;
    ObjList* l = s_rooms.find(name);
    ConfRoom* room = l ? static_cast<ConfRoom*>(l->get()) : 0;
    if (room)
	room->ref();
    else
	room = new ConfRoom(name);
    return room;
}

ConfRoom::ConfRoom(const String& name)
    : m_name(name)
{
    Debug(&__plugin,DebugAll,"ConfRoom::ConfRoom('%s') [%p]",name.c_str(),this);
    s_rooms.append(this);
}

ConfRoom::~ConfRoom()
{
    Debug(&__plugin,DebugAll,"ConfRoom::~ConfRoom() '%s' [%p]",m_name.c_str(),this);
    s_rooms.remove(this,false);
    m_chans.clear();
}

void ConfRoom::mix()
{
    unsigned int len = MAX_BUFFER;
    unsigned int mlen = 0;
    ObjList* l = m_chans.skipNull();
    for (;l;l = l->skipNext()) {
	ConfChan* ch = static_cast<ConfChan*>(l->get());
	ConfConsumer* co = static_cast<ConfConsumer*>(ch->getConsumer());
	if (co) {
	    if (co->m_buffer.length() < len)
		len = co->m_buffer.length();
	    if (co->m_buffer.length() > mlen)
		mlen = co->m_buffer.length();
	}
    }
    XDebug(DebugAll,"ConfRoom::mix() buffer %u - %u [%p]",len,mlen,this);
    mlen = mlen + MIN_BUFFER - MAX_BUFFER;
    if (len < mlen)
	len = mlen;
    len /= DATA_CHUNK;
    if (!len)
	return;
    len *= DATA_CHUNK / sizeof(int16_t);
    int* buf = (int*)::calloc(len,sizeof(int));
    l = m_chans.skipNull();
    for (;l;l = l->skipNext()) {
	ConfChan* ch = static_cast<ConfChan*>(l->get());
	ConfConsumer* co = static_cast<ConfConsumer*>(ch->getConsumer());
	if (co && co->m_buffer.length()) {
	    unsigned int n = co->m_buffer.length() / 2;
	    if (n > len)
		n = len;
	    const int16_t* p = (const int16_t*)co->m_buffer.data();
	    for (unsigned int i=0; i < n; i++)
		buf[i] += *p++;
	    n *= sizeof(int16_t);
	    co->m_buffer.cut(-(int)n);
	}
    }
    DataBlock data(0,len*sizeof(int16_t));
    int16_t* p = (int16_t*)data.data();
    for (unsigned int i=0; i < len; i++) {
	int val = buf[i];
	*p++ = (val < -32768) ? -32768 : ((val > 32767) ? 32767 : val);
    }
    ::free(buf);
    Forward(data);
}

void ConfConsumer::Consume(const DataBlock& data, unsigned long timeDelta)
{
    if (data.null() || !m_room)
	return;
    Lock lock(&__plugin);
    if (m_buffer.length()+data.length() < MAX_BUFFER)
	m_buffer += data;
    if (m_buffer.length() >= MIN_BUFFER)
	m_room->mix();
}

ConfChan::ConfChan(const String& name)
    : Channel(__plugin)
{
    Debug(this,DebugAll,"ConfChan::ConfChan(%s) %s [%p]",name.c_str(),id().c_str(),this);
    Lock lock(&__plugin);
    ConfRoom* room = ConfRoom::create(name);
    if (room) {
	setSource(room);
	room->deref();
	room->channels().append(this);
	setConsumer(new ConfConsumer(room));
	getConsumer()->deref();
    }
}

ConfChan::~ConfChan()
{
    Debug(this,DebugAll,"ConfChan::~ConfChan() %s [%p]",id().c_str(),this);
    Lock lock(&__plugin);
    setConsumer();
    if (getSource()) {
	static_cast<ConfRoom*>(getSource())->channels().remove(this,false);
	setSource();
    }
}

bool ConferenceDriver::msgExecute(Message& msg, String& dest)
{
    if (dest.null())
	dest << "x-" << (unsigned int)::random();
    CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userData());
    if (ch) {
	ConfChan *c = new ConfChan(dest);
	if (ch->connect(c)) {
	    c->deref();
	    msg.setParam("room",prefix()+dest);
	    return true;
	}
	else {
	    c->destruct();
	    return false;
	}
    }
    Debug(DebugWarn,"Conference call with no call endpoint!");
    return false;
}

ConferenceDriver::ConferenceDriver()
    : Driver("conf","misc")
{
    Output("Loaded module Conference");
}

ConferenceDriver::~ConferenceDriver()
{
    Output("Unloading module Conference");
    s_rooms.clear();
}

void ConferenceDriver::initialize()
{
    Output("Initializing module Conference");
    setup();
}

/* vi: set ts=8 sw=4 sts=4 noet: */
