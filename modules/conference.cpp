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

// Absolute maximum possible energy
#define ENERGY_MAX 1073676289

// Default / minimum noise threshold
#define ENERGY_MIN 256

// Attack / decay rates for computing average energy
#define DECAY_TOTAL 1000
#define DECAY_STORE 995
#define ATTACK_RATE (DECAY_TOTAL-DECAY_STORE)

#if DECAY_TOTAL <= DECAY_STORE
#error DECAY_TOTAL must be higher than DECAY_STORE
#endif

// Shift for noise margin
#define SHIFT_LEVEL 5
// Shift for noise decay rate
#define SHIFT_RAISE 7

#if SHIFT_LEVEL >= SHIFT_RAISE
#error SHIFT_RAISE must be higher than SHIFT_LEVEL
#endif

class ConfConsumer;

static ObjList s_rooms;

class ConfRoom : public DataSource
{
public:
    ConfRoom(const String& name);
    ~ConfRoom();
    static ConfRoom* get(const String& name, bool create = false);
    virtual const String& toString() const
	{ return m_name; }
    inline ObjList& channels()
	{ return m_chans; }
    void mix(ConfConsumer* cons);
private:
    String m_name;
    ObjList m_chans;
};

class ConfChan : public Channel
{
public:
    ConfChan(const String& name, bool smart = false);
    ~ConfChan();
};

class ConfConsumer : public DataConsumer
{
    friend class ConfRoom;
public:
    ConfConsumer(ConfRoom* room, bool smart = false)
	: m_room(room), m_smart(smart),
	  m_energy2(ENERGY_MIN), m_noise2(ENERGY_MIN)
	{ }
    ~ConfConsumer()
	{ }
    virtual void Consume(const DataBlock& data, unsigned long timeDelta);
    unsigned int energy() const;
    unsigned int noise() const;
    inline unsigned int energy2() const
	{ return m_energy2; }
    inline unsigned int noise2() const
	{ return m_noise2; }
    inline bool hasSignal() const
	{ return (m_buffer.length() > 1) && (m_energy2 >= m_noise2); }
private:
    void consumed(unsigned int samples);
    ConfRoom* m_room;
    bool m_smart;
    unsigned int m_energy2;
    unsigned int m_noise2;
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

ConfRoom* ConfRoom::get(const String& name, bool create)
{
    if (name.null())
	return 0;
    ObjList* l = s_rooms.find(name);
    ConfRoom* room = l ? static_cast<ConfRoom*>(l->get()) : 0;
    if (room)
	room->ref();
    else if (create)
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

void ConfRoom::mix(ConfConsumer* cons)
{
    unsigned int len = MAX_BUFFER;
    unsigned int mlen = 0;
    Lock mylock(mutex());
    ObjList* l = m_chans.skipNull();
    for (; l; l = l->skipNext()) {
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
    for (l = m_chans.skipNull(); l; l = l->skipNext()) {
	ConfChan* ch = static_cast<ConfChan*>(l->get());
	ConfConsumer* co = static_cast<ConfConsumer*>(ch->getConsumer());
	if (co) {
	    // avoid mixing in noise
	    if (co->hasSignal()) {
		unsigned int n = co->m_buffer.length() / 2;
		XDebug(ch,DebugAll,"Cons %p samp=%u |%s%s>",
		    co,n,String('#',co->noise()).safe(),
		    String('=',co->energy() - co->noise()).safe());
		if (n > len)
		    n = len;
		const int16_t* p = (const int16_t*)co->m_buffer.data();
		for (unsigned int i=0; i < n; i++)
		    buf[i] += *p++;
	    }
	    co->consumed(len);
	}
    }
    DataBlock data(0,len*sizeof(int16_t));
    int16_t* p = (int16_t*)data.data();
    for (unsigned int i=0; i < len; i++) {
	int val = buf[i];
	// saturate symmetrically the result of addition
	*p++ = (val < -32767) ? -32767 : ((val > 32767) ? 32767 : val);
    }
    ::free(buf);
    mylock.drop();
    Forward(data);
}

void ConfConsumer::Consume(const DataBlock& data, unsigned long timeDelta)
{
    if (data.null() || !m_room)
	return;
    if (m_smart) {
	// we need to compute the average energy and take decay into account
	int64_t sum2 = m_energy2;
	unsigned int min2 = ENERGY_MAX;
	unsigned int n = data.length() / 2;
	const int16_t* p = (const int16_t*)data.data();
	for (unsigned int i=0; i < n; i++) {
	    int32_t samp = *p++;
	    // use square of the energy as extracting the square root is expensive
	    sum2 = (sum2 * DECAY_STORE + (int64_t)(samp*samp) * ATTACK_RATE) / DECAY_TOTAL;
	    if (min2 > sum2)
		min2 = sum2;
	}
	m_energy2 = sum2;
	// TODO: find a better algorithm to adjust the noise threshold
	min2 += min2 >> SHIFT_LEVEL;
	// try to keep noise threshold slightly above minimum energy
	if (m_noise2 > min2)
	    m_noise2 = min2;
	else
	    m_noise2 += 1 + (m_noise2 >> SHIFT_RAISE);
	// but never below our arbitrary absolute minimum
	if (m_noise2 < ENERGY_MIN)
	    m_noise2 = ENERGY_MIN;
    }
    m_room->mutex()->lock();
    if (m_buffer.length()+data.length() < MAX_BUFFER)
	m_buffer += data;
    m_room->mutex()->unlock();
    if (m_buffer.length() >= MIN_BUFFER)
	m_room->mix(this);
}

void ConfConsumer::consumed(unsigned int samples)
{
    if (!samples)
	return;
    unsigned int n = m_buffer.length() / 2;
    if (samples > n) {
	// buffer underflowed
	m_buffer.clear();
	if (m_smart) {
	    // artificially decay for missing samples
	    n = samples - n;
	    int64_t sum2 = m_energy2;
	    while (n--)
		sum2 = (sum2 * DECAY_STORE) / DECAY_TOTAL;
	    m_energy2 = sum2;
	}
	return;
    }
    samples *= sizeof(int16_t);
    m_buffer.cut(-(int)samples);
}

static unsigned int binLog(unsigned int x)
{
    unsigned int v = 0;
    while (x >>= 1)
	v++;
    return v;
}

unsigned int ConfConsumer::energy() const
{
    return binLog(m_energy2);
}

unsigned int ConfConsumer::noise() const
{
    return binLog(m_noise2);
}


ConfChan::ConfChan(const String& name, bool smart)
    : Channel(__plugin)
{
    Debug(this,DebugAll,"ConfChan::ConfChan(%s,%s) %s [%p]",
	name.c_str(),String::boolText(smart),id().c_str(),this);
    Lock lock(&__plugin);
    ConfRoom* room = ConfRoom::get(name,true);
    if (room) {
	m_address = name;
	setSource(room);
	room->deref();
	room->channels().append(this);
	setConsumer(new ConfConsumer(room,smart));
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
    if (msg.getBoolValue("existing") && !ConfRoom::get(dest))
	return false;
    if (dest.null())
	dest << "x-" << (unsigned int)::random();
    CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userData());
    if (ch) {
	ConfChan *c = new ConfChan(dest,msg.getBoolValue("smart",true));
	if (ch->connect(c)) {
	    msg.setParam("peerid",c->id());
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
