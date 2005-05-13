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

using namespace TelEngine;

static ObjList s_rooms;

class ConfRoom : public String
{
public:
    ConfRoom(const String& name);
    ~ConfRoom();
    inline ObjList& channels()
	{ return m_chans; }
    void mix();
private:
    ObjList m_chans;
    bool mixOne(int index);
};

class ConfChan : public Channel
{
public:
    ConfChan(const String& name);
    ~ConfChan();
    inline ConfRoom* room() const
	{ return m_room; }
private:
    ConfRoom* m_room;
};

class ConfSource : public DataSource
{
public:
    ConfSource(unsigned int bufsize = 320)
	: m_bufpos(0), m_buffer(0,bufsize & ~1)
	{ }
    ~ConfSource()
	{ }
    void put(short val);
private:
    unsigned m_bufpos;
    DataBlock m_buffer;
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
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest);
};

INIT_PLUGIN(ConferenceDriver);

ConfRoom::ConfRoom(const String& name)
    : String(name)
{
    Debug(&__plugin,DebugAll,"ConfRoom::ConfRoom('%s') [%p]",c_str(),this);
    s_rooms.append(this);
}

ConfRoom::~ConfRoom()
{
    Debug(&__plugin,DebugAll,"ConfRoom::~ConfRoom() '%s' [%p]",c_str(),this);
    s_rooms.remove(this,false);
    m_chans.clear();
}

void ConfRoom::mix()
{
    int i = 0;
    while (mixOne(i))
	i++;
    ObjList* l = m_chans.skipNull();
    for (;l;l = l->skipNext()) {
	ConfChan* ch = static_cast<ConfChan*>(l->get());
	ConfConsumer* co = static_cast<ConfConsumer*>(ch->getConsumer());
	if (co)
	    co->m_buffer.cut(-i);
    }
}

bool ConfRoom::mixOne(int index)
{
    int v = 0;
    ObjList* l = m_chans.skipNull();
    for (;l;l = l->skipNext()) {
	ConfChan* ch = static_cast<ConfChan*>(l->get());
	ConfConsumer* c = static_cast<ConfConsumer*>(ch->getConsumer());
	if (c) {
	}
    }
    return false;
}

void ConfSource::put(short val)
{
    ((short*)m_buffer.data())[m_bufpos >> 1] = val;
    m_bufpos += 2;
    if (m_bufpos >= m_buffer.length()) {
	m_bufpos = 0;
	Forward(m_buffer);
    }
}

void ConfConsumer::Consume(const DataBlock& data, unsigned long timeDelta)
{
    if (data.null() || !m_room)
	return;
    Lock lock(&__plugin);
    if (m_buffer.data())
	m_room->mix();
    m_buffer += data;
}

ConfChan::ConfChan(const String& name)
    : Channel(__plugin), m_room(0)
{
    Debug(this,DebugAll,"ConfChan::ConfChan(%s) %s [%p]",name.c_str(),id().c_str(),this);
    Lock lock(&__plugin);
    ObjList* r = s_rooms.find(name);
    if (r)
	m_room = static_cast<ConfRoom*>(r->get());
    else
	m_room = new ConfRoom(name);
    setConsumer(new ConfConsumer(m_room));
    getConsumer()->deref();
    setSource(new ConfSource);
    getSource()->deref();
    m_room->channels().append(this);
}

ConfChan::~ConfChan()
{
    Debug(this,DebugAll,"ConfChan::~ConfChan() %s [%p]",id().c_str(),this);
    Lock lock(&__plugin);
    m_room->channels().remove(this,false);
    if (!m_room->channels().count())
	delete m_room;
}

bool ConferenceDriver::msgExecute(Message& msg, String& dest)
{
    if (dest.null())
	dest << "x-" << ::random();
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

void ConferenceDriver::initialize()
{
    Output("Initializing module Conference");
    setup();
}

/* vi: set ts=8 sw=4 sts=4 noet: */
