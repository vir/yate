/**
 * conference.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Conference room data mixer
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 *
 * N-way mixing with self echo suppresion idea by Andrew McDonald
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

#include <yatephone.h>

using namespace TelEngine;
namespace { // anonymous

// size of the outgoing data blocks in bytes - divide by 2 to get samples
#define DATA_CHUNK 320

// minimum amount of buffered data when we start mixing
#define MIN_BUFFER 480

// maximum size we allow the buffer to grow
#define MAX_BUFFER 960

// minimum notification interval in msec
#define MIN_INTERVAL 1000

// maximum and default number of speakers we track
#define MAX_SPEAKERS 8
#define DEF_SPEAKERS 3

// Speaking detector energy square hysteresis
#define SPEAK_HIST_MIN 16384
#define SPEAK_HIST_MAX 32768

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
static Mutex s_srcMutex(false,"Conference");

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
    inline int maxLock() const
	{ return m_maxLock; }
    inline bool timeout(const Time& time) const
	{ return m_expire && m_expire < time; }
    inline bool created()
	{ return m_created && !(m_created = false); }
    void mix(ConfConsumer* cons = 0);
    void addChannel(ConfChan* chan, bool player = false);
    void delChannel(ConfChan* chan);
    void addOwner(const String& id);
    void delOwner(const String& id);
    bool isOwned() const;
    void dropAll(const char* reason = 0);
    void msgStatus(Message& msg);
    bool setRecording(const NamedList& params);
    bool setParams(NamedList& params);
    void update(const NamedList& params);
private:
    ConfRoom(const String& name, const NamedList& params);
    // Set the expire time from 'lonely' parameter value
    // Set the lonely flag if called the first time (no users in conference)
    void setLonelyTimeout(const String& value);
    // Set the expire time
    void setExpire();
    String m_name;
    ObjList m_chans;
    ObjList m_owners;
    String m_notify;
    String m_playerId;
    bool m_lonely;
    bool m_created;
    ConfChan* m_record;
    int m_rate;
    int m_users;
    int m_maxusers;
    int m_maxLock;
    u_int64_t m_expire;
    unsigned int m_lonelyInterval;
    ConfChan* m_speakers[MAX_SPEAKERS];
    int m_trackSpeakers;
    int m_trackInterval;
    u_int64_t m_nextNotify;
    u_int64_t m_nextSpeakers;
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
    inline ConfRoom* room() const
	{ return m_room; }
    void populateMsg(Message& msg) const;
protected:
    void statusParams(String& str);
private:
    void alterMsg(Message& msg, const char* event);
    RefPointer<ConfRoom> m_room;
    bool m_counted;
    bool m_utility;
    bool m_billing;
    bool m_keepTarget;
};

// The data consumer computes energy and noise levels (if required) and
//  triggers the mixing of data in the conference room
class ConfConsumer : public DataConsumer
{
    friend class ConfRoom;
    friend class ConfChan;
    friend class ConfSource;
    YCLASS(ConfConsumer,DataConsumer);
public:
    ConfConsumer(ConfRoom* room, bool smart = false)
	: m_room(room), m_src(0), m_muted(false), m_smart(smart), m_speak(false),
	  m_energy2(ENERGY_MIN), m_noise2(ENERGY_MIN), m_envelope2(ENERGY_MIN)
	{ DDebug(DebugAll,"ConfConsumer::ConfConsumer(%p,%s) [%p]",room,String::boolText(smart),this); m_format = room->getFormat(); }
    ~ConfConsumer()
	{ DDebug(DebugAll,"ConfConsumer::~ConfConsumer() [%p]",this); }
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags);
    virtual bool control(NamedList& msg);
    unsigned int energy() const;
    unsigned int noise() const;
    unsigned int envelope() const;
    inline unsigned int energy2() const
	{ return m_energy2; }
    inline unsigned int noise2() const
	{ return m_noise2; }
    inline unsigned int envelope2() const
	{ return m_envelope2; }
    inline bool muted() const
	{ return m_muted; }
    inline bool smart() const
	{ return m_smart; }
    inline bool speaking() const
	{ return m_smart && m_speak && !m_muted; }
    inline bool hasSignal() const
	{ return (!m_muted) && (m_energy2 >= m_noise2); }
    inline bool shouldMix() const
	{ return hasSignal() && (m_buffer.length() > 1); }
private:
    void consumed(const int* mixed, unsigned int samples);
    void dataForward(const int* mixed, unsigned int samples);
    RefPointer<ConfRoom> m_room;
    ConfSource* m_src;
    bool m_muted;
    bool m_smart;
    bool m_speak;
    unsigned int m_energy2;
    unsigned int m_noise2;
    unsigned int m_envelope2;
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

// The driver just holds all the channels (not conferences)
class ConferenceDriver : public Driver
{
public:
    ConferenceDriver();
    virtual ~ConferenceDriver();
    bool checkRoom(String& room, bool existing, bool counted);
    bool unload();
    // Change the number of rooms needing timeout check
    void setConfToutCount(bool on);
protected:
    virtual void initialize();
    virtual bool received(Message &msg, int id);
    virtual bool msgExecute(Message& msg, String& dest);
    virtual bool commandComplete(Message& msg, const String& partLine, const String& partWord);
    virtual void statusParams(String& str);
private:
    MessageHandler* m_handler;
    MessageHandler* m_hangup;
    unsigned int m_confTout;             // The number of rooms needing timeout check
};

INIT_PLUGIN(ConferenceDriver);

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow)
	return __plugin.unload();
    return true;
}

// Handler for call.conference message to join both legs of a call in conference
class ConfHandler : public MessageHandler
{
public:
    inline ConfHandler(unsigned int priority)
	: MessageHandler("call.conference",priority,__plugin.name())
	{ }
    virtual ~ConfHandler()
	{ }
    virtual bool received(Message& msg);
};

// Handler for chan.hangup message
class HangupHandler : public MessageHandler
{
public:
    inline HangupHandler(unsigned int priority)
	: MessageHandler("chan.hangup",priority,__plugin.name())
	{ }
    virtual ~HangupHandler()
	{ }
    virtual bool received(Message& msg);
};

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
    if (params) {
	if (room)
	    room->update(*params);
	else
	    room = new ConfRoom(name,*params);
    }
    return room;
}

// Private constructor, always called from ConfRoom::get() with mutex hold
ConfRoom::ConfRoom(const String& name, const NamedList& params)
    : m_name(name), m_lonely(false), m_created(true), m_record(0),
      m_rate(8000), m_users(0), m_maxusers(10), m_maxLock(200),
      m_expire(0), m_lonelyInterval(0), m_nextNotify(0), m_nextSpeakers(0)
{
    DDebug(&__plugin,DebugAll,"ConfRoom::ConfRoom('%s',%p) [%p]",
	name.c_str(),&params,this);
    m_rate = params.getIntValue("rate",m_rate);
    m_maxusers = params.getIntValue("maxusers",m_maxusers);
    m_maxLock = params.getIntValue("waitlock",m_maxLock);
    m_notify = params.getValue("notify");
    m_trackSpeakers = params.getIntValue("speakers",0);
    if (m_trackSpeakers < 0)
	m_trackSpeakers = 0;
    else if (m_trackSpeakers > MAX_SPEAKERS)
	m_trackSpeakers = MAX_SPEAKERS;
    else if ((m_trackSpeakers == 0) && params.getBoolValue("speakers"))
	m_trackSpeakers = DEF_SPEAKERS;
    m_trackInterval = params.getIntValue("interval",3000);
    if (m_trackInterval <= 0)
	m_trackInterval = 0;
    else if (m_trackInterval < MIN_INTERVAL)
	m_trackInterval = MIN_INTERVAL;
    setLonelyTimeout(params["lonely"]);
    if (m_rate != 8000)
	m_format << "/" << m_rate;
    for (int i = 0; i < MAX_SPEAKERS; i++)
	m_speakers[i] = 0;
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
    if (m_expire)
	__plugin.setConfToutCount(false);
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
    if (chan->isCounted()) {
	m_users++;
	setExpire();
    }
    if (m_notify && !chan->isUtility()) {
	String tmp(m_users);
	Message* m = new Message("chan.notify");
	m->addParam("id",chan->id());
	m->addParam("targetid",m_notify);
	if (chan->getPeerId())
	    m->addParam("peerid",chan->getPeerId());
	chan->populateMsg(*m);
	m->addParam("event","joined");
	m->addParam("maxusers",String(m_maxusers));
	m->addParam("users",tmp);
	if (m_playerId)
	    m->addParam("player",m_playerId);
	if (chan->getLastPeerId(tmp))
	    m->setParam("lastpeerid",tmp);
	Engine::enqueue(m);
    }
}

// Remove one channel from the room
void ConfRoom::delChannel(ConfChan* chan)
{
    if (!chan)
	return;
    Lock mylock(this);
    if (chan == m_record)
	m_record = 0;
    if (m_playerId && (chan->id() == m_playerId))
	m_playerId.clear();
    if (m_chans.remove(chan,false) && chan->isCounted()) {
	m_users--;
	setExpire();
    }
    bool alone = (m_users == 1);
    bool notOwned = !isOwned();
    mylock.drop();
    if (m_notify && !chan->isUtility()) {
	String tmp(m_users);
	Message* m = new Message("chan.notify");
	m->addParam("id",chan->id());
	m->addParam("targetid",m_notify);
	chan->populateMsg(*m);
	m->addParam("event","left");
	m->addParam("maxusers",String(m_maxusers));
	m->addParam("users",tmp);
	// easy to check parameter indicating one user will be left alone
	if (m_lonely)
	    m->addParam("lonely",String::boolText(alone));
	if (m_playerId)
	    m->addParam("player",m_playerId);
	if (chan->getLastPeerId(tmp))
	    m->setParam("lastpeerid",tmp);
	Engine::enqueue(m);
    }

    // cleanup if there are only 1 or 0 (if lonely==true) real users left
    if (notOwned)
	// all channels left are utility or the lonely user - drop them
	dropAll("hangup");
}

// Add one owner channel
void ConfRoom::addOwner(const String& id)
{
    if (id.null() || m_owners.find(id))
	return;
    m_owners.append(new String(id));
    DDebug(&__plugin,DebugInfo,"Added owner '%s' to room '%s'",
	id.c_str(),m_name.c_str());
}

// Remove one owner channel from the room
void ConfRoom::delOwner(const String& id)
{
    Lock mylock(this);
    if (!m_owners.find(id))
	return;
    m_owners.remove(id);
    DDebug(&__plugin,DebugInfo,"Removed owner '%s' from room '%s'",
	id.c_str(),m_name.c_str());
    if (isOwned())
	return;
    // only utilities and a lonely user remains - drop them
    mylock.drop();
    dropAll("hangup");
}

// Check if a room is owned by at least one other channel
bool ConfRoom::isOwned() const
{
    if (!m_users)
	return false;
    if (m_lonely || (m_users > 1))
	return true;
    unsigned int c = m_owners.count();
    if (c > 1)
	return true;
    else if (c == 0)
	return false;
    // one user, one owner - check if the same
    const String& id = m_owners.skipNull()->get()->toString();
    for (ObjList* l = m_chans.skipNull(); l; l = l->skipNext()) {
	const ConfChan* chan = static_cast<const ConfChan*>(l->get());
	if (chan->isCounted()) {
	    String id2;
	    return chan->getPeerId(id2) && (id != id2);
	}
    }
    // should not reach here
    return true;
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

// Retrieve status information about this room
void ConfRoom::msgStatus(Message& msg)
{
    Lock mylock(this);
    msg.retValue().clear();
    msg.retValue() << "name=" << __plugin.prefix() << m_name;
    msg.retValue() << ",type=conference";
    msg.retValue() << ";module=" << __plugin.name();
    msg.retValue() << ",room=" << m_name;
    msg.retValue() << ",maxusers=" << m_maxusers;
    msg.retValue() << ",lonely=" << m_lonely;
    int exp = 0;
    if (m_lonely && m_expire)
	exp = (int)(((int64_t)m_expire - (int64_t)msg.msgTime().usec())/1000);
    msg.retValue() << ",expire=" << (int)exp;
    msg.retValue() << ",rate=" << m_rate;
    msg.retValue() << ",users=" << m_users;
    msg.retValue() << ",chans=" << m_chans.count();
    msg.retValue() << ",owners=" << m_owners.count();
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
	ch->initChan();
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
	m->setParam("maxlen",params.getValue("maxlen"));
	m->setParam("notify",params.getValue("notify"));
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

// Set miscellaneous parameters from and to conference message
bool ConfRoom::setParams(NamedList& params)
{
    // return room parameters
    params.setParam("newroom",String::boolText(created()));
    params.setParam("users",String(users()));
    // possibly set the caller or explicit ID as controller
    const String* ctl = params.getParam("confowner");
    if (TelEngine::null(ctl))
	return false;
    if (ctl->isBoolean()) {
	const String* id = params.getParam("id");
	if (!TelEngine::null(id)) {
	    if (ctl->toBoolean())
		addOwner(*id);
	    else
		delOwner(*id);
	}
    }
    else
	addOwner(*ctl);
    return true;
}

// Mix in buffered data from all channels, only if we have enough in buffer
void ConfRoom::mix(ConfConsumer* cons)
{
    unsigned int len = MAX_BUFFER;
    unsigned int mlen = 0;
    Lock mylock(this);
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
    int speakVol[MAX_SPEAKERS];
    ConfChan* speakChan[MAX_SPEAKERS];
    int spk;
    for (spk = 0; spk < MAX_SPEAKERS; spk++) {
	speakVol[spk] = 0;
	speakChan[spk] = 0;
    }
    len = chunks * DATA_CHUNK / sizeof(int16_t);
    DataBlock mixbuf(0,len*sizeof(int));
    int* buf = (int*)mixbuf.data();
    for (l = m_chans.skipNull(); l; l = l->skipNext()) {
	ConfChan* ch = static_cast<ConfChan*>(l->get());
	ConfConsumer* co = static_cast<ConfConsumer*>(ch->getConsumer());
	if (co) {
	    // avoid mixing in noise
	    if (co->shouldMix()) {
		unsigned int n = co->m_buffer.length() / 2;
#ifdef XDEBUG
		if (ch->debugAt(DebugAll)) {
		    int noise = co->noise();
		    int energy = co->energy() - noise;
		    if (energy < 0)
			energy = 0;
		    int tip = co->envelope() - energy - noise;
		    if (tip < 0)
			tip = 0;
		    Debug(ch,DebugAll,"Cons %p samp=%u |%s%s%s>",
			co,n,String('#',noise).safe(),
			String('=',energy).safe(),String('-',tip).safe());
		}
#endif
		if (n > len)
		    n = len;
		const int16_t* p = (const int16_t*)co->m_buffer.data();
		for (unsigned int i=0; i < n; i++)
		    buf[i] += *p++;
	    }
	    if (m_trackSpeakers && m_notify && !ch->isUtility() && co->speaking()) {
		int vol = co->envelope();
		for (spk = m_trackSpeakers-1; spk >= 0; spk--) {
		    if (vol <= speakVol[spk])
			break;
		    if (spk < MAX_SPEAKERS-1) {
			speakVol[spk+1] = speakVol[spk];
			speakChan[spk+1] = speakChan[spk];
		    }
		    speakVol[spk] = vol;
		    speakChan[spk] = ch;
		}
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
    Message* m = 0;
    while (m_trackSpeakers && m_notify) {
	u_int64_t now = Time::now();
	if (now < m_nextNotify)
	    break;
	bool notify = false;
	bool changed = false;
	// check if the list of speakers changed or not, exclude order change
	for (spk = 0; spk < m_trackSpeakers; spk++) {
	    changed = true;
	    for (int i = 0; i < m_trackSpeakers; i++) {
		if (m_speakers[spk] == speakChan[i]) {
		    changed = false;
		    break;
		}
	    }
	    if (changed)
		break;
	}
	if (!changed) {
	    for (spk = 0; spk < m_trackSpeakers; spk++) {
		changed = true;
		for (int i = 0; i < m_trackSpeakers; i++) {
		    if (m_speakers[i] == speakChan[spk]) {
			changed = false;
			break;
		    }
		}
		if (changed)
		    break;
	    }
	}
	// check if anything changed
	for (spk = 0; spk < m_trackSpeakers; spk++) {
	    if (m_speakers[spk] != speakChan[spk]) {
		m_speakers[spk] = speakChan[spk];
		notify = true;
	    }
	}
	// if we have speaker(s) notify periodically
	if (!notify)
	    notify = m_speakers[0] && (now >= m_nextSpeakers);
	if (notify) {
	    m = new Message("chan.notify");
	    m->userData(this);
	    m->addParam("targetid",m_notify);
	    m->addParam("event","speaking");
	    m->addParam("room",m_name);
	    m->addParam("maxusers",String(m_maxusers));
	    for (spk = 0; spk < m_trackSpeakers; spk++) {
		if (!speakChan[spk])
		    break;
		String param("speaker.");
		param << (spk+1);
		m->addParam(param,speakChan[spk]->id());
		String peer;
		if (speakChan[spk]->getPeerId(peer))
		    m->addParam(param + ".peer",peer);
		m->addParam(param + ".energy",String(speakVol[spk]));
	    }
	    m->addParam("speakers",String(spk));
	    m->addParam("changed",String::boolText(changed));
	    // repeat notification at least once every 5s if someone speaks
	    m_nextSpeakers = now + 5000000;
	    // limit the minimum interval of notification
	    if (m_trackInterval)
		m_nextNotify = now + 1000 * (int64_t)m_trackInterval;
	}
	break;
    }
    mylock.drop();
    Forward(data);
    if (m)
	Engine::enqueue(m);
}

// Update room data
void ConfRoom::update(const NamedList& params)
{
    String* l = params.getParam("lonely");
    if (l)
	setLonelyTimeout(*l);
}

// Set the expire time from 'lonely' parameter value
// Set the lonely flag if called the first time (no users in conference)
void ConfRoom::setLonelyTimeout(const String& value)
{
    int interval = value.toInteger(-1);
    if (!m_users) {
	m_lonely = value.toBoolean(interval >= 0);
	DDebug(&__plugin,DebugAll,"ConfRoom(%s) lonely=%u [%p]",m_name.c_str(),m_lonely,this);
    }
    if (!m_lonely || interval < 0)
	return;
    if (interval > 0 && interval < 1000)
	interval = 1000;
    if ((int)m_lonelyInterval == interval)
	return;
    m_lonelyInterval = interval;
    DDebug(&__plugin,DebugAll,"ConfRoom(%s) set lonely interval to %ums [%p]",
	m_name.c_str(),m_lonelyInterval,this);
    setExpire();
}

// Set the expire time
void ConfRoom::setExpire()
{
    if (!m_lonely)
	return;
    bool changed = true;
    if (m_users == 1 && m_lonelyInterval) {
	changed = !m_expire;
	m_expire = Time::now() + m_lonelyInterval * 1000;
    }
    else if (m_expire)
	m_expire = 0;
    else
	return;
    DDebug(&__plugin,DebugAll,"ConfRoom(%s) %s lonely timeout users=%d [%p]",
	m_name.c_str(),(m_expire ? "started" : "stopped"),m_users,this);
    if (changed)
	__plugin.setConfToutCount(m_expire != 0);
}


// Compute the energy level and noise threshold, store the data and call mixer
unsigned long ConfConsumer::Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
{
    if (m_muted || data.null() || !m_room)
	return 0;
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
	// compute envelope, faster attack than decay
	if (m_energy2 > m_envelope2)
	    m_envelope2 = (unsigned int)(((u_int64_t)m_envelope2 * 7 + m_energy2) >> 3);
	else
	    m_envelope2 = (unsigned int)(((u_int64_t)m_envelope2 * 15 + m_energy2) >> 4);
	// detect speech or noises, apply hysteresis
	m_speak = (m_envelope2 >> 1) > (m_noise2 + (m_speak ? SPEAK_HIST_MIN : SPEAK_HIST_MAX));
    }
    bool autoMute = true;
    int maxLock = 1000 * m_room->maxLock();
    if (maxLock < 0) {
	autoMute = false;
	maxLock = -maxLock;
    }
    // clamp lock timer between 50 and 500ms
    if (maxLock < 50000)
	maxLock = 50000;
    else if (maxLock > 500000)
	maxLock = 500000;
    // make sure looping back conferences is not fatal
    if (!m_room->lock(maxLock)) {
	Alarm(&__plugin,"bug",DebugWarn,"Failed to lock room '%s' - data loopback?%s [%p]",
	    m_room->toString().c_str(),(autoMute ? " Channel muted!" : ""),this);
	// mute the channel to avoid getting back here
	if (autoMute)
	    m_muted = true;
	return 0;
    }
    if (m_buffer.length()+data.length() <= MAX_BUFFER)
	m_buffer += data;
    m_room->unlock();
    if (m_buffer.length() >= MIN_BUFFER)
	m_room->mix(this);
    return invalidStamp();
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
	if ((i < n) && shouldMix())
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

unsigned int ConfConsumer::envelope() const
{
    return binLog(m_envelope2);
}

bool ConfConsumer::control(NamedList& msg)
{
    bool ok = false;
    const String* param = msg.getParam(YSTRING("mute"));
    if (param && param->isBoolean()) {
	m_muted = param->toBoolean();
	ok = true;
    }
    param = msg.getParam(YSTRING("smart"));
    if (param && param->isBoolean()) {
	m_smart = param->toBoolean();
	ok = true;
    }
    return TelEngine::controlReturn(&msg,DataConsumer::control(msg) || ok);
}


ConfSource::ConfSource(ConfConsumer* cons)
    : m_cons(cons)
{
    if (m_cons) {
	m_format = m_cons->getFormat();
	m_cons->m_src = this;
    }
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
      m_counted(counted), m_utility(utility), m_billing(false), m_keepTarget(true)
{
    DDebug(this,DebugAll,"ConfChan::ConfChan(%s,%p) %s [%p]",
	name.c_str(),&params,id().c_str(),this);
    // much of the defaults depend if this is an utility channel or not
    m_billing = params.getBoolValue("billing",false);
    m_keepTarget = params.getBoolValue("keeptarget",false);
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
	s->copyParams(params,"caller,callername,called,billid,callto,username");
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
    if (m_keepTarget) {
	String* target = msg.getParam("targetid");
	// if the message is already targeted to something else don't touch it
	if (target && *target && (id() != *target))
	    return;
    }
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
	String tmp;
	if (getLastPeerId(tmp))
	    msg.setParam("lastpeerid",tmp);
    }
}

void ConfChan::statusParams(String& str)
{
    Channel::statusParams(str);
    __plugin.lock();
    RefPointer<ConfConsumer> cons = YOBJECT(ConfConsumer,getConsumer());
    __plugin.unlock();
    if (cons) {
	bool sig = cons->hasSignal();
	str << ",mute=" << cons->muted();
	str << ",signal=" << sig;
	if (cons->smart() && !cons->muted()) {
	    str << ",noise=" << cons->noise();
	    if (sig)
		str << ",energy=" << cons->energy();
	}
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
	    ok = cr->setParams(msg) || ok;
	    cr->deref();
	}
	if (!ok)
	    Debug(&__plugin,DebugNote,"Conference request with no channel!");
	return ok;
    }
    if (chan->getObject(YATOM("ConfChan"))) {
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
    c->initChan();
    if (chan->connect(c,reason,false)) {
	msg.setParam("peerid",c->id());
	msg.setParam("room",__plugin.prefix()+room);
	if (peer) {
	    // create a conference leg for the old peer too
	    ConfChan *p = new ConfChan(room,msg,counted,utility);
	    p->initChan();
	    peer->connect(p,reason,false);
	    p->deref();
	}
	if (c->room())
	    c->room()->setParams(msg);
	c->deref();
	return true;
    }
    c->destruct();
    return false;
}


bool HangupHandler::received(Message& msg)
{
    const String* id = msg.getParam("id");
    if (TelEngine::null(id))
	return false;
    __plugin.lock();
    ListIterator iter(s_rooms);
    while (ConfRoom* room = static_cast<ConfRoom*>(iter.get())) {
	if (room->alive())
	    room->delOwner(*id);
    }
    __plugin.unlock();
    return false;
}


// Message received override to drop entire rooms
bool ConferenceDriver::received(Message &msg, int id)
{
    while (((id == Drop) || (id == Status)) && prefix()) {
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
    if (id == Timer) {
	// Use a while to break
	while (m_confTout) {
	    lock();
	    if (!m_confTout) {
		unlock();
		break;
	    }
	    ListIterator iter(s_rooms);
	    Time t;
	    for (;;) {
		RefPointer<ConfRoom> room = static_cast<ConfRoom*>(iter.get());
		unlock();
		if (!room)
		    break;
		if (room->timeout(t)) {
		    Lock mylock(room,500000);
		    if (mylock.locked() && !room->isOwned()) {
			Debug(this,DebugAll,"Room (%p) '%s' timed out",
			    (ConfRoom*)room,room->toString().c_str());
			mylock.drop();
			room->dropAll("timeout");
		    }
		}
		room = 0;
		lock();
	    }
	    break;
	}
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
	    else they will be disconnected. A valid integer (>= 0) will
	    be interpreted as lonely timeout interval (ms).
	    In the initial state a value of 0 indicates lonely=true
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
    CallEndpoint* ch = YOBJECT(CallEndpoint,msg.userData());
    if (ch) {
	ConfChan *c = new ConfChan(dest,msg,counted,utility);
	c->initChan();
	if (ch->connect(c,msg.getValue("reason"))) {
	    c->callConnect(msg);
	    msg.setParam("peerid",c->id());
	    msg.setParam("room",prefix()+dest);
	    if (c->room())
		c->room()->setParams(msg);
	    c->deref();
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
    if (!lock.locked())
	return false;
    if (isBusy() || s_rooms.count())
	return false;
    uninstallRelays();
    Engine::uninstall(m_handler);
    m_handler = 0;
    Engine::uninstall(m_hangup);
    m_hangup = 0;
    return true;
}

void ConferenceDriver::setConfToutCount(bool on)
{
    Lock lock(this);
    if (on)
	m_confTout++;
    else if (m_confTout)
	m_confTout--;
    DDebug(this,DebugAll,"Rooms timeout counter set to %u",m_confTout);
}

ConferenceDriver::ConferenceDriver()
    : Driver("conf","misc"),
      m_handler(0), m_hangup(0), m_confTout(0)
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
    m_hangup = new HangupHandler(150);
    Engine::install(m_hangup);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
