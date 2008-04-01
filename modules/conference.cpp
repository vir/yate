/**
 * conference.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Conference room data mixer
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
 *
 * N-way mixing with self echo suppresion idea by Andrew McDonald
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

#include <yatephone.h>

using namespace TelEngine;
namespace { // anonymous

// size of the outgoing data blocks in bytes - divide by 2 to get samples
#define DATA_CHUNK 320

// minimum amount of buffered data when we start mixing
#define MIN_BUFFER 480

// maximum size we allow the buffer to grow
#define MAX_BUFFER 960


// Absolute maximum possible energy (square +-32767 wave) - do not change
#define ENERGY_MAX 1073676289

// Default / minimum noise threshold
#define ENERGY_MIN 256

// Attack / decay rates for computing average energy
#define DECAY_TOTAL 1000
#define DECAY_STORE 995
#define ATTACK_RATE (DECAY_TOTAL-DECAY_STORE)

// Shift for noise margin
#define SHIFT_LEVEL 5
// Shift for noise decay rate
#define SHIFT_RAISE 7

// some sanity checks
#if DECAY_TOTAL <= DECAY_STORE
#error DECAY_TOTAL must be higher than DECAY_STORE
#endif

#if SHIFT_LEVEL >= SHIFT_RAISE
#error SHIFT_RAISE must be higher than SHIFT_LEVEL
#endif

class ConfConsumer;
class ConfSource;
class ConfChan;

// The list of conference rooms
static ObjList s_rooms;

// Mutex that protects the source while accessed by the consumer
static Mutex s_srcMutex;

// Hold the number of the newest allocated dynamic room
static int s_roomAlloc = 0;

// The conference room holds a list of connected channels and does the mixing.
// It does also act as a data source for the sum of all channels
class ConfRoom : public DataSource
{
public:
    virtual void destroyed();
    static ConfRoom* get(const String& name, const NamedList* params = 0);
    virtual const String& toString() const
	{ return m_name; }
    inline ObjList& channels()
	{ return m_chans; }
    inline int rate() const
	{ return m_rate; }
    inline int users() const
	{ return m_users; }
    inline bool full() const
	{ return m_users >= m_maxusers; }
    inline ConfChan* recorder() const
	{ return m_record; }
    inline const String& notify() const
	{ return m_notify; }
    void mix(ConfConsumer* cons = 0);
    void addChannel(ConfChan* chan, bool player = false);
    void delChannel(ConfChan* chan);
    void dropAll(const char* reason = 0);
    void msgStatus(Message& msg);
    bool setRecording(const NamedList& params);
private:
    ConfRoom(const String& name, const NamedList& params);
    String m_name;
    ObjList m_chans;
    String m_notify;
    String m_playerId;
    bool m_lonely;
    ConfChan* m_record;
    int m_rate;
    int m_users;
    int m_maxusers;
};

// A conference channel is just a dumb holder of its data channels
class ConfChan : public Channel
{
    YCLASS(ConfChan,Channel)
public:
    ConfChan(const String& name, const NamedList& params, bool counted, bool utility);
    ConfChan(ConfRoom* room, bool voice = false);
    virtual ~ConfChan();
    virtual bool msgTone(Message& msg, const char* tone);
    virtual bool msgText(Message& msg, const char* text);
    inline bool isCounted() const
	{ return m_counted; }
    inline bool isUtility() const
	{ return m_utility; }
    inline bool isRecorder() const
	{ return m_room && (m_room->recorder() == this); }
    void populateMsg(Message& msg) const;
private:
    void alterMsg(Message& msg, const char* event);
    RefPointer<ConfRoom> m_room;
    bool m_counted;
    bool m_utility;
    bool m_billing;
};

// The data consumer computes energy and noise levels (if required) and
//  triggers the mixing of data in the conference room
class ConfConsumer : public DataConsumer
{
    friend class ConfRoom;
    friend class ConfChan;
    friend class ConfSource;
public:
    ConfConsumer(ConfRoom* room, bool smart = false)
	: m_room(room), m_src(0), m_muted(false), m_smart(smart),
	  m_energy2(ENERGY_MIN), m_noise2(ENERGY_MIN)
	{ }
    ~ConfConsumer()
	{ }
    virtual void Consume(const DataBlock& data, unsigned long tStamp);
    unsigned int energy() const;
    unsigned int noise() const;
    inline unsigned int energy2() const
	{ return m_energy2; }
    inline unsigned int noise2() const
	{ return m_noise2; }
    inline bool muted() const
	{ return m_muted; }
    inline bool hasSignal() const
	{ return (!m_muted) && (m_buffer.length() > 1) && (m_energy2 >= m_noise2); }
private:
    void consumed(const int* mixed, unsigned int samples);
    void dataForward(const int* mixed, unsigned int samples);
    ConfRoom* m_room;
    ConfSource* m_src;
    bool m_muted;
    bool m_smart;
    unsigned int m_energy2;
    unsigned int m_noise2;
    DataBlock m_buffer;
};

// Per channel data source with that channel's data removed from the mix
class ConfSource : public DataSource
{
    friend class ConfChan;
public:
    ConfSource(ConfConsumer* cons);
    ~ConfSource();
private:
    RefPointer<ConfConsumer> m_cons;
};

// Handler for call.conference message to join both legs of a call in conference
class ConfHandler : public MessageHandler
{
public:
    inline ConfHandler(unsigned int priority)
	: MessageHandler("call.conference",priority)
	{ }
    virtual ~ConfHandler()
	{ }
    virtual bool received(Message& msg);
};

// The driver just holds all the channels (not conferences)
class ConferenceDriver : public Driver
{
public:
    ConferenceDriver();
    virtual ~ConferenceDriver();
    bool checkRoom(String& room, bool existing, bool counted);
    bool unload();
protected:
    virtual void initialize();
    virtual bool received(Message &msg, int id);
    virtual bool msgExecute(Message& msg, String& dest);
    virtual bool commandComplete(Message& msg, const String& partLine, const String& partWord);
    virtual void statusParams(String& str);
private:
    ConfHandler* m_handler;
};

INIT_PLUGIN(ConferenceDriver);

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow)
	return __plugin.unload();
    return true;
}

// Count the position of the most significant 1 bit - pretty close to logarithm
static unsigned int binLog(unsigned int x)
{
    unsigned int v = 0;
    while (x >>= 1)
	v++;
    return v;
}


// Get a pointer to a conference by name, optionally creates it with given parameters
// If a pointer is returned it must be dereferenced by the caller
// Thread safe
ConfRoom* ConfRoom::get(const String& name, const NamedList* params)
{
    if (name.null())
	return 0;
    Lock lock(&__plugin);
    ObjList* l = s_rooms.find(name);
    ConfRoom* room = l ? static_cast<ConfRoom*>(l->get()) : 0;
    if (room && !room->ref())
	room = 0;
    if (params && !room)
	room = new ConfRoom(name,*params);
    return room;
}

// Private constructor, always called from ConfRoom::get() with mutex hold
ConfRoom::ConfRoom(const String& name, const NamedList& params)
    : m_name(name), m_lonely(false), m_record(0),
      m_rate(8000), m_users(0), m_maxusers(10)
{
    DDebug(&__plugin,DebugAll,"ConfRoom::ConfRoom('%s',%p) [%p]",
	name.c_str(),&params,this);
    m_rate = params.getIntValue("rate",m_rate);
    m_maxusers = params.getIntValue("maxusers",m_maxusers);
    m_notify = params.getValue("notify");
    m_lonely = params.getBoolValue("lonely");
    s_rooms.append(this);
    // possibly create outgoing call to room record utility channel
    setRecording(params);
    // emit room creation notification
    if (m_notify) {
	Message* m = new Message("chan.notify");
	m->userData(this);
	m->addParam("targetid",m_notify);
	m->addParam("event","created");
	m->addParam("room",m_name);
	m->addParam("maxusers",String(m_maxusers));
	m->addParam("caller",params.getValue("caller"));
	m->addParam("called",params.getValue("called"));
	m->addParam("billid",params.getValue("billid"));
	m->addParam("username",params.getValue("username"));
	Engine::enqueue(m);
    }
}

void ConfRoom::destroyed()
{
    DDebug(&__plugin,DebugAll,"ConfRoom::destroyed() '%s' [%p]",m_name.c_str(),this);
    // plugin must be locked as the destructor is called when room is dereferenced
    Lock lock(&__plugin);
    s_rooms.remove(this,false);
    m_chans.clear();
    if (m_notify) {
	Message* m = new Message("chan.notify");
	m->addParam("targetid",m_notify);
	m->addParam("event","destroyed");
	m->addParam("room",m_name);
	m->addParam("maxusers",String(m_maxusers));
	Engine::enqueue(m);
    }
    DataSource::destroyed();
}

// Add one channel to the room
void ConfRoom::addChannel(ConfChan* chan, bool player)
{
    if (!chan)
	return;
    m_chans.append(chan);
    if (player)
	m_playerId = chan->id();
    if (chan->isCounted())
	m_users++;
    if (m_notify && !chan->isUtility()) {
	String tmp(m_users);
	Message* m = new Message("chan.notify");
	m->addParam("id",chan->id());
	m->addParam("targetid",m_notify);
	chan->populateMsg(*m);
	m->addParam("event","joined");
	m->addParam("room",m_name);
	m->addParam("maxusers",String(m_maxusers));
	m->addParam("users",tmp);
	if (m_playerId)
	    m->addParam("player",m_playerId);
	Engine::enqueue(m);
    }
}

// Remove one channel from the room
void ConfRoom::delChannel(ConfChan* chan)
{
    if (!chan)
	return;
    Lock lock(mutex());
    if (chan == m_record)
	m_record = 0;
    if (m_playerId && (chan->id() == m_playerId))
	m_playerId.clear();
    if (m_chans.remove(chan,false) && chan->isCounted())
	m_users--;
    bool alone = (m_users == 1);
    lock.drop();
    if (m_notify && !chan->isUtility()) {
	String tmp(m_users);
	Message* m = new Message("chan.notify");
	m->addParam("id",chan->id());
	m->addParam("targetid",m_notify);
	chan->populateMsg(*m);
	m->addParam("event","left");
	m->addParam("room",m_name);
	m->addParam("maxusers",String(m_maxusers));
	m->addParam("users",tmp);
	// easy to check parameter indicating one user will be left alone
	if (m_lonely)
	    m->addParam("lonely",String::boolText(alone));
	if (m_playerId)
	    m->addParam("player",m_playerId);
	Engine::enqueue(m);
    }

    // cleanup if there are only 1 or 0 (if lonely==true) real users left
    if (m_users <= (m_lonely ? 0 : 1))
	// all channels left are utility or the lonely user - drop them
	dropAll("hangup");
}

// Drop all channels attached to the room, the lock must not be hold
void ConfRoom::dropAll(const char* reason)
{
    // make sure we continue to exist at least as long as the iterator
    if (!ref())
	return;
    ListIterator iter(m_chans);
    while (ConfChan* ch = static_cast<ConfChan*>(iter.get()))
	ch->disconnect(reason);
    deref();
}

// Retrive status information about this room
void ConfRoom::msgStatus(Message& msg)
{
    Lock lock(mutex());
    msg.retValue().clear();
    msg.retValue() << "name=" << __plugin.prefix() << m_name;
    msg.retValue() << ",type=conference";
    msg.retValue() << ";module=" << __plugin.name();
    msg.retValue() << ",room=" << m_name;
    msg.retValue() << ",maxusers=" << m_maxusers;
    msg.retValue() << ",lonely=" << m_lonely;
    msg.retValue() << ",users=" << m_users;
    msg.retValue() << ",chans=" << m_chans.count();
    if (m_notify)
	msg.retValue() << ",notify=" << m_notify;
    if (m_playerId)
	msg.retValue() << ",player=" << m_playerId;
    msg.retValue() << "\r\n";
}

// Create or stop outgoing call to room record utility channel
bool ConfRoom::setRecording(const NamedList& params)
{
    const String* record = params.getParam("record");
    // only do something if the "record" parameter is present
    if (!record)
	return false;
    // keep us safe - we may drop the recording of a lonely channel
    if (!ref())
	return false;

    // stop any old recording channel
    ConfChan* ch = m_record;
    m_record = 0;
    if (ch) {
	DDebug(&__plugin,DebugCall,"Stopping record leg '%s'",ch->id().c_str());
	ch->disconnect(params.getValue("reason","hangup"));
    }
    // create recorder if "record" is anything but "", "no", "false" or "disable"
    if (*record && (*record != "-") && record->toBoolean(true)) {
	String warn = params.getValue("recordwarn");
	ch = new ConfChan(this,!warn.null());
	DDebug(&__plugin,DebugCall,"Starting record leg '%s' to '%s'",
	    ch->id().c_str(),record->c_str());
	Message* m = ch->message("call.execute");
	m->userData(ch);
	m->setParam("callto",*record);
	m->setParam("cdrtrack",String::boolText(false));
	m->setParam("caller",params.getValue("caller"));
	m->setParam("called",params.getValue("called"));
	m->setParam("billid",params.getValue("billid"));
	m->setParam("username",params.getValue("username"));
	m->addParam("room",m_name);
	if (m_notify)
	    m->setParam("targetid",m_notify);
	Engine::enqueue(m);
	if (warn) {
	    // play record warning to the entire conference
	    m = ch->message("chan.attach",true,true);
	    m->addParam("override",warn);
	    m->addParam("single",String::boolText(true));
	    m->addParam("room",m_name);
	    Engine::enqueue(m);
	}
	else
	    Debug(&__plugin,DebugNote,"Recording '%s' without playing tone!",m_name.c_str());
	if (m_notify) {
	    m = ch->message("chan.notify",true,true);
	    m->addParam("id",ch->id());
	    m->addParam("targetid",m_notify);
	    ch->populateMsg(*m);
	    m->addParam("event","recording");
	    m->addParam("room",m_name);
	    m->addParam("maxusers",String(m_maxusers));
	    m->addParam("users",String(m_users));
	    m->addParam("record",*record);
	    Engine::enqueue(m);
	}
	m_record = ch;
	ch->deref();
    }

    deref();
    return true;
}

// Mix in buffered data from all channels, only if we have enough in buffer
void ConfRoom::mix(ConfConsumer* cons)
{
    unsigned int len = MAX_BUFFER;
    unsigned int mlen = 0;
    Lock mylock(mutex());
    // find out the minimum and maximum amount of data in buffers
    ObjList* l = m_chans.skipNull();
    for (; l; l = l->skipNext()) {
	ConfChan* ch = static_cast<ConfChan*>(l->get());
	ConfConsumer* co = static_cast<ConfConsumer*>(ch->getConsumer());
	if (co) {
	    unsigned int buffered = co->m_buffer.length();
	    if (len > buffered)
		len = buffered;
	    if (mlen < buffered)
		mlen = buffered;
	}
    }
    XDebug(DebugAll,"ConfRoom::mix() buffer %u - %u [%p]",len,mlen,this);
    mlen += MIN_BUFFER;
    // do we have at least minimum amount of data in buffer?
    if (mlen <= MAX_BUFFER)
	return;
    mlen -= MAX_BUFFER;
    // make sure we mix in enough data to prevent channels from overflowing
    if (len < mlen)
	len = mlen;
    unsigned int chunks = len / DATA_CHUNK;
    if (!chunks)
	return;
    len = chunks * DATA_CHUNK / sizeof(int16_t);
    DataBlock mixbuf(0,len*sizeof(int));
    int* buf = (int*)mixbuf.data();
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
	}
    }
    // we finished mixing - notify consumers about it
    for (l = m_chans.skipNull(); l; l = l->skipNext()) {
	ConfChan* ch = static_cast<ConfChan*>(l->get());
	ConfConsumer* co = static_cast<ConfConsumer*>(ch->getConsumer());
	if (co)
	    co->consumed(buf,len);
    }
    DataBlock data(0,len*sizeof(int16_t));
    int16_t* p = (int16_t*)data.data();
    for (unsigned int i=0; i < len; i++) {
	int val = buf[i];
	// saturate symmetrically the result of addition
	*p++ = (val < -32767) ? -32767 : ((val > 32767) ? 32767 : val);
    }
    mixbuf.clear();
    mylock.drop();
    Forward(data);
}


// Compute the energy level and noise threshold, store the data and call mixer
void ConfConsumer::Consume(const DataBlock& data, unsigned long tStamp)
{
    if (m_muted || data.null() || !m_room)
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
		min2 = (unsigned int)sum2;
	}
	m_energy2 = (unsigned int)sum2;
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
    // make sure looping back conferences is not fatal
    if (!m_room->mutex()->lock(150000)) {
	Debug(&__plugin,DebugWarn,"Failed to lock room '%s' - data loopback? [%p]",
	    m_room->toString().c_str(),this);
	// mute the channel to avoid getting back here
	m_muted = true;
	return;
    }
    if (m_buffer.length()+data.length() <= MAX_BUFFER)
	m_buffer += data;
    m_room->mutex()->unlock();
    if (m_buffer.length() >= MIN_BUFFER)
	m_room->mix(this);
}

// Take out of the buffer the samples mixed in or skipped
//  this method is called with the room locked
void ConfConsumer::consumed(const int* mixed, unsigned int samples)
{
    if (!samples)
	return;
    dataForward(mixed,samples);
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
	    m_energy2 = (unsigned int)sum2;
	}
	return;
    }
    samples *= sizeof(int16_t);
    m_buffer.cut(-(int)samples);
}

// Substract our own data from the mix and send it on the no-echo source
void ConfConsumer::dataForward(const int* mixed, unsigned int samples)
{
    if (!(m_src && mixed))
	return;
    // static lock is used while we reference the source
    s_srcMutex.lock();
    RefPointer<ConfSource> src = m_src;
    s_srcMutex.unlock();
    if (!src)
	return;

    int16_t* d = (int16_t*)m_buffer.data();
    unsigned int n = m_buffer.length() / 2;
    DataBlock data(0,samples*sizeof(int16_t));
    int16_t* p = (int16_t*)data.data();
    for (unsigned int i=0; i < samples; i++) {
	int val = *mixed++;
	// substract our own data if we contributed - only as much as we have
	if ((i < n) && hasSignal())
	    val -= d[i];
	// saturate symmetrically the result of additions and substraction
	*p++ = (val < -32767) ? -32767 : ((val > 32767) ? 32767 : val);
    }
    src->Forward(data);
}

unsigned int ConfConsumer::energy() const
{
    return binLog(m_energy2);
}

unsigned int ConfConsumer::noise() const
{
    return binLog(m_noise2);
}


ConfSource::ConfSource(ConfConsumer* cons)
    : m_cons(cons)
{
    if (m_cons)
	m_cons->m_src = this;
}

ConfSource::~ConfSource()
{
    if (m_cons) {
	s_srcMutex.lock();
	m_cons->m_src = 0;
	s_srcMutex.unlock();
    }
}


// Constructor of a new conference leg, creates or attaches to an existing
//  conference room; noise and echo suppression are also set here
ConfChan::ConfChan(const String& name, const NamedList& params, bool counted, bool utility)
    : Channel(__plugin,0,true),
      m_counted(counted), m_utility(utility), m_billing(false)
{
    DDebug(this,DebugAll,"ConfChan::ConfChan(%s,%p) %s [%p]",
	name.c_str(),&params,id().c_str(),this);
    // much of the defaults depend if this is an utility channel or not
    m_billing = params.getBoolValue("billing",false);
    bool smart = params.getBoolValue("smart",!m_utility);
    bool echo = params.getBoolValue("echo",m_utility);
    bool voice = params.getBoolValue("voice",true);
    m_room = ConfRoom::get(name,&params);
    if (m_room) {
	m_address = name;
	if (!m_utility) {
	    int tout = params.getIntValue("timeout", driver() ? driver()->timeout() : 0);
	    if (tout > 0)
		timeout(Time::now() + tout*(u_int64_t)1000);
	}
	m_room->addChannel(this,params.getBoolValue("player",false));
	RefPointer<ConfConsumer> cons;
	if (voice) {
	    cons = new ConfConsumer(m_room,smart);
	    setConsumer(cons);
	    cons->deref();
	}
	if (echo || !cons)
	    setSource(m_room);
	else {
	    ConfSource* src = new ConfSource(cons);
	    setSource(src);
	    src->deref();
	}
	// no need to keep it referenced - m_room wil do it automatically
	m_room->deref();
    }
    if (m_billing) {
	Message* s = message("chan.startup",params);
	s->setParam("caller",params.getValue("caller"));
	s->setParam("called",params.getValue("called"));
	s->setParam("billid",params.getValue("billid"));
	s->setParam("username",params.getValue("username"));
	Engine::enqueue(s);
    }
}

// Constructor of an utility conference leg (incoming call)
ConfChan::ConfChan(ConfRoom* room, bool voice)
    : Channel(__plugin),
      m_counted(false), m_utility(true), m_billing(false)
{
    DDebug(this,DebugAll,"ConfChan::ConfChan(%p,%s) %s [%p]",
	room,String::boolText(voice),id().c_str(),this);
    m_room = room;
    if (m_room) {
	m_address = m_room->toString();
	m_room->addChannel(this);
	if (voice) {
	    ConfConsumer* cons = new ConfConsumer(m_room);
	    setConsumer(cons);
	    cons->deref();
	}
	setSource(m_room);
    }
}

// Destructor - remove itself from room and optionally emit chan.hangup
ConfChan::~ConfChan()
{
    DDebug(this,DebugAll,"ConfChan::~ConfChan() %s [%p]",id().c_str(),this);
    Lock lock(&__plugin);
    // keep the room referenced until we are done
    RefPointer<ConfRoom> room = m_room;
    // remove ourselves from the room's mixer
    if (room)
	room->delChannel(this);
    // now we can safely remove the data streams
    clearEndpoint();
    if (m_billing)
	Engine::enqueue(message("chan.hangup"));
}

// Intercept DTMF messages, possibly turn them into room notifications
bool ConfChan::msgTone(Message& msg, const char* tone)
{
    alterMsg(msg,"dtmf");
    return false;
}

// Intercept text messages, possibly turn them into room notifications
bool ConfChan::msgText(Message& msg, const char* text)
{
    alterMsg(msg,"text");
    return false;
}

// Populate messages with common conference leg parameters
void ConfChan::populateMsg(Message& msg) const
{
    msg.setParam("counted",String::boolText(m_counted));
    msg.setParam("utility",String::boolText(m_utility));
    msg.setParam("room",m_address);
}

// Alter messages, possibly turn them into room event notifications
void ConfChan::alterMsg(Message& msg, const char* event)
{
    String* target = msg.getParam("targetid");
    // if the message is already targeted to something else don't touch it
    if (target && *target && (id() != *target))
	return;
    populateMsg(msg);
    // if we were the target or it was none send it to the room's notifier
    if (m_room && m_room->notify()) {
	msg = "chan.notify";
	const char* peerid = msg.getValue("id");
	if (peerid)
	    msg.setParam("peerid",peerid);
	msg.setParam("id",id());
	msg.setParam("event",event);
	msg.setParam("users",String(m_room->users()));
	msg.setParam("full",String::boolText(m_room->full()));
	msg.setParam("targetid",m_room->notify());
    }
}


bool ConfHandler::received(Message& msg)
{
    String room = msg.getValue("room");
    // if a room name is provided it must be like room/SOMETHING
    if (room && (!room.startSkip(__plugin.prefix(),false) || room.null()))
	return false;
    // we don't need a RefPointer for this one as the message keeps it referenced
    CallEndpoint* chan = YOBJECT(CallEndpoint,msg.userData());
    if (!chan) {
	bool ok = false;
	ConfRoom* cr = ConfRoom::get(room);
	if (cr) {
	    ok = cr->setRecording(msg);
	    cr->deref();
	}
	if (!ok)
	    Debug(&__plugin,DebugNote,"Conference request with no channel!");
	return ok;
    }
    if (chan->getObject("ConfChan")) {
	Debug(&__plugin,DebugWarn,"Conference request from a conference leg!");
	return false;
    }

    bool utility = msg.getBoolValue("utility",false);
    bool counted = msg.getBoolValue("counted",!utility);
    if (!__plugin.checkRoom(room,msg.getBoolValue("existing"),counted))
	return false;

    const char* reason = msg.getValue("reason","conference");

    RefPointer<CallEndpoint> peer = chan->getPeer();
    if (peer) {
	ConfChan* conf = YOBJECT(ConfChan,peer);
	if (conf) {
	    // caller's peer is already a conference - check if the same
	    if (conf->address() == room) {
		Debug(&__plugin,DebugNote,"Do-nothing conference request to the same room");
		return true;
	    }
	    // not same - we just drop old conference leg
	    peer = 0;
	}
    }

    // create a conference leg or even a room for the caller
    ConfChan *c = new ConfChan(room,msg,counted,utility);
    if (chan->connect(c,reason,false)) {
	msg.setParam("peerid",c->id());
	c->deref();
	msg.setParam("room",__plugin.prefix()+room);
	if (peer) {
	    // create a conference leg for the old peer too
	    ConfChan *p = new ConfChan(room,msg,counted,utility);
	    peer->connect(p,reason,false);
	    p->deref();
	}
	return true;
    }
    c->destruct();
    return false;
}


// Message received override to drop entire rooms
bool ConferenceDriver::received(Message &msg, int id)
{
    while ((id == Drop) || (id == Status) && prefix()) {
	String dest;
	switch (id) {
	    case Drop:
		dest = msg.getValue("id");
		break;
	    case Status:
		dest = msg.getValue("module");
		break;
	}
	if (!dest.startSkip(prefix(),false))
	    break;
	ConfRoom* room = ConfRoom::get(dest);
	if (!room)
	    break;
	switch (id) {
	    case Drop:
		room->dropAll(msg.getValue("reason"));
		room->deref();
		break;
	    case Status:
		room->msgStatus(msg);
		room->deref();
		break;
	}
	return true;
    }
    return Driver::received(msg,id);
}

/* "call.execute" message
    Input parameters - per room:
	"callto" - must be of the form "conf/NAME" where NAME is the name of
	    the conference; an empty name will be replaced with a random one
	"existing" - set to true to always attach to an existing conference;
	    the call will fail if a conference with that name doesn't exist
	"maxusers" - maximum number of users allowed to connect to this
	    conference, not counting utility channels
	"lonely" - set to true to allow lonely users to remain in conference
	    else they will be disconnected
	"notify" - ID used for "chan.notify" room notifications, an empty
	    string (default) will disable notifications
	"record" - route that will make an outgoing record-only call
    Input parameters - per conference leg:
	"utility" - true creates a channel that is used for housekeeping
	    tasks like recording or playing prompts to everybody
	"counted" - set to false (default for utility) to not count the
	    conference leg against maxusers or disconnect criteria
	"voice" - set to false to have the conference leg just listen to the
	    voice mix without being able to talk
	"smart" - set to false to disable energy and noise level calculation
	"echo" - set to true to hear back the voice this channel has injected
	    in the conference
	"billing" - set to true to generate "chan.startup" and "chan.hangup"
	    messages for billing purposes
    Return parameters:
	"room" - name of the conference room in the form "conf/NAME"
	"peerid" - channel ID of the conference leg
*/

// Handle call.execute by creating or attaching to an existing conference
bool ConferenceDriver::msgExecute(Message& msg, String& dest)
{
    bool utility = msg.getBoolValue("utility",false);
    bool counted = msg.getBoolValue("counted",!utility);
    if (!checkRoom(dest,msg.getBoolValue("existing"),counted))
	return false;
    CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userData());
    if (ch) {
	ConfChan *c = new ConfChan(dest,msg,counted,utility);
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
    // conference will never make outgoing calls
    Debug(DebugWarn,"Conference call with no call endpoint!");
    return false;
}

// Check if a room exists, allocates a new room name if not and asked so
bool ConferenceDriver::checkRoom(String& room, bool existing, bool counted)
{
    ConfRoom* conf = ConfRoom::get(room);
    if (existing && !conf)
	return false;
    if (conf) {
	bool ok = !(counted && conf->full());
	conf->deref();
	return ok;
    }
    if (room.null()) {
	// allocate an atomically incremented room number
	lock();
	room << "x-" << ++s_roomAlloc;
	unlock();
    }
    return true;
}

bool ConferenceDriver::unload()
{
    Lock lock(this,500000);
    if (!lock.mutex())
	return false;
    if (isBusy() || s_rooms.count())
	return false;
    uninstallRelays();
    Engine::uninstall(m_handler);
    m_handler = 0;
    return true;
}

ConferenceDriver::ConferenceDriver()
    : Driver("conf","misc"), m_handler(0)
{
    Output("Loaded module Conference");
}

ConferenceDriver::~ConferenceDriver()
{
    Output("Unloading module Conference");
    s_rooms.clear();
}

bool ConferenceDriver::commandComplete(Message& msg, const String& partLine, const String& partWord)
{
    bool ok = Driver::commandComplete(msg,partLine,partWord);
    if (ok && String(msg.getValue("complete")) == "channels") {
	String tmp = partWord;
	if (tmp.startSkip(prefix(),false)) {
	    lock();
	    ObjList* l = s_rooms.skipNull();
	    for (; l; l=l->skipNext()) {
		ConfRoom* r = static_cast<ConfRoom*>(l->get());
		if (tmp.null() || r->toString().startsWith(tmp))
		    msg.retValue().append(prefix() + r->toString(),"\t");
	    }
	    unlock();
	}
    }
    return ok;
}

void ConferenceDriver::statusParams(String& str)
{
    Driver::statusParams(str);
    str.append("rooms=",",") << s_rooms.count();
}

void ConferenceDriver::initialize()
{
    Output("Initializing module Conference");
    // install intercept relays with a priority slightly higher than default
    installRelay(Tone,75);
    installRelay(Text,75);
    setup();
    if (m_handler)
	return;
    m_handler = new ConfHandler(150);
    Engine::install(m_handler);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
