/**
 * mux.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Data multiplex
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

#include <yatephone.h>

#include <string.h>

using namespace TelEngine;
namespace { // anonymous

class MuxConsumer;                       // Consumer used to push data a source multiplexer
class MuxSource;                         // A data source multiplexer
class MuxModule;                         // The module
class ChanAttachHandler;                 // chan.attach handler

// Consumer used to push data to a MuxSource
class MuxConsumer : public DataConsumer
{
    friend class MuxSource;
public:
    MuxConsumer(MuxSource* owner, unsigned int chan, const char* format, bool reference);
    virtual ~MuxConsumer()
	{}
    // Send data to the owner, if any
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags);
protected:
    virtual void destroyed();
private:
    MuxSource* m_owner;                  // The owner of this consumer
    RefPointer<RefObject> m_ref;         // Reference keeper
    unsigned int m_channel;              // The channel allocated by the owner
    unsigned int m_bufferFilled;         // The number of samples in the buffer kept by the owner
    unsigned int m_overErrors;           // Buffer overrun errors
};

// A data source multiplexer
class MuxSource : public DataSource, public DebugEnabler
{
    friend class MuxConsumer;
public:
    MuxSource(const String& id, const char* targetid, const char* format,
	const NamedList& params, String& error, bool reference);
    virtual ~MuxSource()
	{}
    inline const String& id() const
	{ return m_id; }
    inline const String& targetid() const
	{ return m_targetid; }
    inline unsigned int channels()
	{ return m_channels; }
    // Get the consumer for a specific channel
    inline MuxConsumer* getConsumer(unsigned int channel) {
	return channel < m_channels ? m_consumers[channel] : 0;
    }
    // Check if a channel's consumer exists and has a source attached
    inline bool hasSource(unsigned int channel) {
	    return channel < m_channels && m_consumers[channel] &&
		m_consumers[channel]->getConnSource();
	}
    // Set/remove the source for a channel's consumer
    // channel: The channel whose source will be replaced
    bool setSource(unsigned int channel, DataSource* source = 0);
    // Multiplex received data from consumers and forward it
    void consume(MuxConsumer& cons, const DataBlock& data, unsigned long tStamp);
    virtual const String& toString() const
	{ return m_id; }
protected:
    // Set/remove a consumer. Fill it's buffer with idle values if removed
    void setConsumer(unsigned int channel, MuxConsumer* pCons = 0);
    // Clear consumers list (remove their owner before)
    virtual void destroyed();
private:
    // Forward the buffer if at least one channel is filled. Reset data
    // Fill incomplete channel buffers with idle value before forwarding the data
    void forwardBuffer();
    // Fill (interlaced samples) buffer with samples of received data
    // If no data, fill the free space with idle value
    void fillBuffer(unsigned int channel, unsigned int& filled,
	unsigned char* data = 0, unsigned int samples = 0);

    Mutex m_lock;                        // Lock consumers changes and data processing
    String m_id;                         // The id wthin this module
    String m_targetid;                   // The id of the target (user)
    MuxConsumer** m_consumers;           // The consumers
    unsigned int m_channels;             // The number of consumers
    unsigned int m_full;                 // The number of consumers with full buffers
    unsigned char m_idleValue;           // Filling value for missing data
    unsigned int m_sampleLen;            // The format sample length
    unsigned int m_maxSamples;           // Maximum samples in a channel buffer
    unsigned int m_delta;                // Offset to write samples for each channel (m_channels * m_sampleLen)
    DataBlock m_buffer;                  // Multiplex buffer
    unsigned int m_error;                // The number of data length violation error
};

// The module
class MuxModule : public Module
{
public:
    enum {
	Attach = Private,
	Record = (Private << 1)
    };
    MuxModule();
    ~MuxModule();
    virtual void initialize();
    // Append source
    inline void append(MuxSource* src) {
	    if (!src)
		return;
	    Lock lock(this);
	    m_sources.append(src)->setDelete(false);
	}
    // Remove source
    inline void remove(MuxSource* src) {
	    Lock lock(this);
	    m_sources.remove(src,false);
	}
protected:
    // Respond to a request to attach/change a multiplexer
    bool chanAttach(Message& msg);
    // Create a multiplexer for bidirectional recording
    bool chanRecord(Message& msg);
    virtual bool received(Message& msg, int id);
    virtual void statusParams(String& str);
    virtual void statusDetail(String& str);
private:
    bool m_first;                        // First init flag
    String m_prefix;                     // Module's prefix (name/)
    unsigned int m_id;                   // Next sources's id
    ObjList m_sources;                   // Source list
};


/**
 * Module data and function
 */
static Configuration s_cfg;              // Configuration file
static unsigned int s_chanBuffer;        // The buffer length of one channel of a data source multiplexer
static unsigned char s_idleValue;        // Idle value for source multiplexer to fill when no data
static String s_defFormat;               // Default format for MuxSource
static MuxModule plugin;

// Dictionary containig the supported formats and sample lengths
static const TokenDict s_dictSampleLen[] = {
    {"mulaw", 1},
    {"alaw",  1},
    {"slin",  2},
    {0,0},
};

// Request a data source for a channel
inline DataSource* getChannelSource(GenObject* target, unsigned int channel)
{
    if (!target)
	return 0;
    String chNo;
    chNo << "DataSource" << channel;
    GenObject* ret = (GenObject*)target->getObject(chNo);
    chNo.clear();
    return ret ? static_cast<DataSource*>(ret->getObject(YATOM("DataSource"))) : 0;
}


/**
 * MuxConsumer
 */
#define ENABLER (m_owner?(DebugEnabler*)m_owner:(DebugEnabler*)&plugin)
#define OWNERNAME (m_owner?m_owner->debugName():"(null)")

MuxConsumer::MuxConsumer(MuxSource* owner, unsigned int chan, const char* format, bool reference)
    : DataConsumer(format),
    m_owner(owner),
    m_channel(chan),
    m_bufferFilled(0),
    m_overErrors(0)
{
    if (m_owner)
	m_owner->setConsumer(m_channel,this);
    if (reference)
	m_ref = owner;
    DDebug(ENABLER,DebugAll,"MuxConsumer(%s,%u) created [%p]",OWNERNAME,m_channel,this);
}

unsigned long MuxConsumer::Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
{
    if (m_owner) {
	m_owner->consume(*this,data,tStamp);
	return invalidStamp();
    }
    return 0;
}

void MuxConsumer::destroyed()
{
    plugin.lock();
    DDebug(ENABLER,DebugAll,"MuxConsumer(%s,%u) destroyed [%p]",
	OWNERNAME,m_channel,this);
    if (m_owner)
	m_owner->setConsumer(m_channel);
    m_owner = 0;
    plugin.unlock();
    m_ref = 0;
    DataConsumer::destroyed();
}

#undef ENABLER
#undef OWNERNAME


/**
 * MuxSource
 */
MuxSource::MuxSource(const String& id, const char* targetid, const char* format,
	const NamedList& params, String& error, bool reference)
    : DataSource(format),
    m_lock(true,"MuxSource::lock"),
    m_id(id),
    m_targetid(targetid),
    m_consumers(0),
    m_channels(0),
    m_full(0),
    m_idleValue(0),
    m_sampleLen(0),
    m_maxSamples(0),
    m_delta(0),
    m_error(0)
{
    debugName(m_id);
    debugChain(&plugin);

    const char* channelFormat = 0;
    // Set the number of channels and the format
    while (true) {
	int pos = getFormat().find("*");
	if (pos < 1)
	    break;
	int channels = getFormat().substr(0,pos).toInteger();
	if (channels < 2)
	    break;
	m_channels = (unsigned int)channels;
	// Get the format of each channel and the sample length. Skip channel count and '*' from format
	pos++;
	if ((unsigned int)pos < getFormat().String::length()) {
	    channelFormat = getFormat().c_str() + pos;
	    m_sampleLen = lookup(channelFormat,s_dictSampleLen,0);
	}
	break;
    }
    if (!m_sampleLen) {
	error << "Unsupported format '" << getFormat() << "'";
	return;
    }
    m_delta = m_channels * m_sampleLen;

    m_idleValue = params.getIntValue("idlevalue",s_idleValue);
    unsigned int chanBuffer = params.getIntValue("chanbuffer",s_chanBuffer);

    // Adjust channel buffer to be multiple of sample length and not lesser then it
    if (chanBuffer < m_sampleLen)
	chanBuffer = m_sampleLen;
    m_maxSamples = chanBuffer / m_sampleLen;
    chanBuffer = m_maxSamples * m_sampleLen;
    m_buffer.assign(0,m_channels * chanBuffer);

    // Create consumers
    m_consumers = new MuxConsumer*[m_channels];
    ::memset(m_consumers,0,m_channels * sizeof(MuxConsumer*));
    for (unsigned int i = 0; i < m_channels; i++)
	new MuxConsumer(this,i,channelFormat,reference);

    Debug(this,DebugAll,
	"Created channels=%u format=%s sample=%u buffer=%u targetid=%s [%p]",
	m_channels,getFormat().c_str(),m_sampleLen,m_buffer.length(),targetid,this);

    plugin.append(this);
}

// Set/remove a consumer. Fill it's buffer with idle value if removed
void MuxSource::setConsumer(unsigned int channel, MuxConsumer* pCons)
{
    Lock lock(m_lock);
    if (!m_consumers || channel >= m_channels || (pCons && pCons->m_owner != this))
	return;
    MuxConsumer* old = m_consumers[channel];
    if (old == pCons)
	return;
    plugin.lock();
    m_consumers[channel] = 0;
    if (old) {
	old->m_overErrors = 0;
	old->m_owner = 0;
    }
    plugin.unlock();
    m_consumers[channel] = pCons;
    if (pCons)
	Debug(this,DebugAll,"Consumer for channel %u set to (%p) [%p]",
	    channel,pCons,this);
    else {
	unsigned int tmp = 0;
	fillBuffer(channel,tmp);
	Debug(this,DebugAll,"Removed consumer (%p) for channel %u [%p]",
	    old,channel,this);
    }
}

// Set/remove the source for a channel's consumer
bool MuxSource::setSource(unsigned int channel, DataSource* source)
{
    Lock lock(m_lock);
    if (channel >= m_channels || !m_consumers[channel])
	return false;

    DataSource* old = m_consumers[channel]->getConnSource();
    if (old == source)
	return true;
    if (old) {
	old->detach(m_consumers[channel]);
	if (!m_consumers[channel]) {
	    Debug(this,DebugNote,
		"Channel %u consumer vanished after detaching from source (%p) [%p]",
		channel,old,this);
	    return false;
	}
	else
	    Debug(this,DebugAll,"Channel %u detached from source (%p) [%p]",channel,old,this);
    }
    if (!source)
	return true;
    source->attach(m_consumers[channel]);
    Debug(this,DebugAll,"Channel %u attached to source (%p) [%p]",channel,source,this);
    return true;
}

// Multiplex received data from consumers and forward it
// Forward multiplexed buffer if chan already filled
// If received data is not greater then free space:
//     Fill chan buffer with data
//     If all channels are filled, forward the multiplexed buffer
// Otherwise:
//     Fill free chan buffer
//     Forward buffer and consume the rest
void MuxSource::consume(MuxConsumer& consumer, const DataBlock& data, unsigned long tStamp)
{
    if (!data.length() || consumer.m_owner != this)
	return;
    Lock lock(m_lock,100000);
    if (!(lock.locked() && alive())) {
	Debug(this,DebugMild,"Locking failed, dropping %u bytes [%p]",data.length(),this);
	return;
    }
    XDebug(this,DebugAll,"Consuming %u bytes on channel %u [%p]",
	   data.length(),consumer.m_channel,this);
    if ((data.length() % m_sampleLen) && !m_error) {
	Debug(this,DebugWarn,"Wrong sample (received %u bytes) on channel %u [%p]",
	    data.length(),consumer.m_channel,this);
	m_error++;
    }
    unsigned int samples = data.length() / m_sampleLen;

    // Forward buffer if already filled for this channel
    if (consumer.m_bufferFilled == m_maxSamples) {
	consumer.m_overErrors++;
	if (0 == consumer.m_overErrors % 5)
	    DDebug(this,DebugMild,"Buffer overrun on channel %u [%p]",
		consumer.m_channel,this);
	forwardBuffer();
    }

    unsigned int freeSamples = m_maxSamples - consumer.m_bufferFilled;
    unsigned char* buf = (unsigned char*)data.data();

    if (samples <= freeSamples) {
	fillBuffer(consumer.m_channel,consumer.m_bufferFilled,buf,samples);
	if (m_full == m_channels)
	    forwardBuffer();
	XDebug(this,DebugAll,"Consumed all %u bytes on channel %u [%p]",
	   samples * m_sampleLen,consumer.m_channel,this);
	return;
    }

    // Received more samples that free space in buffer
    fillBuffer(consumer.m_channel,consumer.m_bufferFilled,buf,freeSamples);
    forwardBuffer();
    unsigned int consumed = freeSamples * m_sampleLen;
    DDebug(this,DebugAll,"Consumed only %u/%u bytes on channel %u [%p]",
	consumed,data.length(),consumer.m_channel,this);
    DataBlock rest(buf + consumed,data.length() - consumed,false);
    consume(consumer,rest,tStamp);
    rest.clear(false);
}

void MuxSource::destroyed()
{
    Lock2 lock(plugin,m_lock);
    plugin.remove(this);

    if (m_consumers) {
	for (unsigned int i = 0; i < m_channels; i++) {
	    if (!m_consumers[i])
		continue;
	    setSource(i);
	    if (m_consumers[i]->m_overErrors > 10)
		Debug(this,DebugMild,
		    "Removing consumer on channel %u with %u overrun errors [%p]",
		    i,m_consumers[i]->m_overErrors,this);
	    m_consumers[i]->m_overErrors = 0;
	    m_consumers[i]->m_owner = 0;
	    TelEngine::destruct(m_consumers[i]);
	}
	delete[] m_consumers;
	m_consumers = 0;
    }
    lock.drop();
    if (!m_error)
	Debug(this,DebugAll,"Destroyed targetid=%s [%p]",m_targetid.c_str(),this);
    else
	Debug(this,DebugMild,"Destroyed targetid=%s data length errors=%u [%p]",
	    m_targetid.c_str(),m_error,this);
    DataSource::destroyed();
}

// Forward the buffer if at least one channel is filled. Reset data
// Fill incomplete channel buffers with idle value before forwarding the data
void MuxSource::forwardBuffer()
{
    if (!m_full)
	return;
    // Fill incomplete buffers. Reset data
    for (unsigned int i = 0; i < m_channels; i++) {
	if (!m_consumers[i])
	    continue;
	if (m_consumers[i]->m_bufferFilled < m_maxSamples) {
	    XDebug(this,DebugAll,"Filling %u idle values on channel %u [%p]",
		m_sampleLen * (m_maxSamples - m_consumers[i]->m_bufferFilled),i,this);
	    fillBuffer(m_consumers[i]->m_channel,m_consumers[i]->m_bufferFilled);
	}
	m_consumers[i]->m_bufferFilled = 0;
    }
    m_full = 0;
    XDebug(this,DebugAll,"Forwarding buffer [%p]",this);
    Forward(m_buffer);
}

// Fill interlaced samples buffer with samples of received data
// If no data, fill the free space with idle value
void MuxSource::fillBuffer(unsigned int channel, unsigned int& filled,
	unsigned char* data, unsigned int samples)
{
    unsigned char* buf = (unsigned char*)m_buffer.data();
    buf += m_sampleLen * (channel + filled * m_channels);
    // Fill received data
    if (data) {
	if (samples > m_maxSamples - filled)
	    samples = m_maxSamples - filled;
	filled += samples;
	if (filled == m_maxSamples)
	    m_full++;
	switch (m_sampleLen) {
	    case 1:
		for (; samples; samples--, buf += m_delta)
		    *buf = *data++;
		break;
	    case 2:
		for (; samples; samples--, buf += m_delta) {
		    buf[0] = *data++;
		    buf[1] = *data++;
		}
		break;
	    default:
		for (; samples; samples--, buf += m_delta, data += m_sampleLen)
		    ::memcpy(buf,data,m_sampleLen);
	}
	return;
    }
    // Fill with idle value
    samples = m_maxSamples - filled;
    filled = m_maxSamples;
    m_full++;
    switch (m_sampleLen) {
	case 1:
	    for (; samples; samples--, buf += m_delta)
		*buf = m_idleValue;
	    break;
	case 2:
	    for (; samples; samples--, buf += m_delta)
		buf[0] = buf[1] = m_idleValue;
	    break;
	default:
	    for (; samples; samples--, buf += m_delta, data += m_sampleLen)
		::memset(buf,m_idleValue,m_sampleLen);
    }
}


/**
 * MuxModule
 * Early init, late cleanup since we provide services to other modules
 */
MuxModule::MuxModule()
    : Module("mux","misc",true),
    m_first(true),
    m_id(1)
{
    Output("Loaded module MUX");
    m_prefix << debugName() << "/";
}

MuxModule::~MuxModule()
{
    Output("Unloading module MUX");
}

void MuxModule::initialize()
{
    Output("Initializing module MUX");
    s_cfg = Engine::configFile("mux");
    s_cfg.load();

    // Startup
    if (m_first) {
	setup();
	installRelay(Attach,"chan.attach",100);
	installRelay(Record,"chan.record",100);
    }

    s_chanBuffer = s_cfg.getIntValue("general","chanbuffer",160);
    if (s_chanBuffer < 1)
	s_chanBuffer = 1;
    unsigned int ui = s_cfg.getIntValue("general","idlevalue",255);
    s_idleValue = (ui <= 255 ? ui : 255);

    const char* format = s_cfg.getValue("general","format");
    if (!lookup(format,s_dictSampleLen))
	format = "alaw";
    s_defFormat.clear();
    s_defFormat << "2*" << format;

    m_first = false;
}

bool MuxModule::received(Message& msg, int id)
{
    switch (id) {
	case Attach:
	    return chanAttach(msg);
	case Record:
	    return chanRecord(msg);
    }
    return Module::received(msg,id);
}

// Respond to a request to attach/change a multiplexer
bool MuxModule::chanAttach(Message& msg)
{
    String id = msg.getValue("source");

    if (Engine::exiting() || !id.startSkip(m_prefix,false))
	return false;

    GenObject* sender = msg.userData();
    if (!sender) {
	msg.setParam("error","No userdata");
	return false;
    }

    MuxSource* src = 0;
    String error;
    const char* targetid = msg.getValue("notify");
    // Check if should fail on channel source attach failure
    bool failOne = msg.getBoolValue("fail",false);

    if (!id) {
	lock();
	id << m_prefix << m_id++;
	unlock();

	src = new MuxSource(id,targetid,msg.getValue("format",s_defFormat),msg,error,false);

	// Set channel sources
	if (!error) {
	    unsigned int count = 0;
	    for (unsigned int channel = 0; channel < src->channels(); channel++) {
		if (src->setSource(channel,getChannelSource(sender,channel)))
		    count++;
		else if (failOne) {
		    error << "Attach failure on channel " << channel;
		    break;
		}
	    }
	    if (!error && !count && msg.getBoolValue("failempty",false))
		error = "Attach failure on all channels";
	}
	if (!error) {
	    msg.userData(src);
	    msg.setParam("id",src->debugName());
	}
    }
    else {
	Lock lock(plugin);
	ObjList* o = m_sources.find(id);
	src = o ? static_cast<MuxSource*>(o->get()) : 0;
	if (!(src && src->ref()))
	    return false;
	lock.drop();
	id = src->debugName();

	// Modify sources
	unsigned int n = msg.count();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* ns = msg.getParam(i);
	    if (!(ns && ns->name() == "channel"))
		continue;
	    unsigned int channel = ns->toInteger(src->channels());
	    if (channel < src->channels() &&
		src->setSource(channel,getChannelSource(sender,channel)))
		continue;
	    if (failOne) {
		if (channel >= src->channels())
		    error << "Invalid channel=" << *ns;
		else
		    error << "Attach failure on channel " << channel;
		break;
	    }
	}
    }

    if (error) {
	Debug(this,DebugNote,"MuxSource failure id=%s targetid=%s error='%s'",
	    id.c_str(),targetid,error.c_str());
	msg.setParam("error",error);
    }
    TelEngine::destruct(src);
    return !error;
}

// Create a multiplexer for bidirectional recording
bool MuxModule::chanRecord(Message& msg)
{
    NamedString* both = msg.getParam(YSTRING("both"));
    if (TelEngine::null(both))
	return false;

    CallEndpoint* ch = YOBJECT(CallEndpoint,msg.userData());
    RefPointer<DataEndpoint> de = YOBJECT(DataEndpoint,msg.userData());

    if (*both == "-") {
	if (ch && !de)
	    de = ch->getEndpoint();
	if (de) {
	    de->setCallRecord();
	    de->setPeerRecord();
	}
	return msg.getBoolValue(YSTRING("single"));
    }

    if (ch && !de)
	de = ch->setEndpoint();
    if (!de) {
	Debug(DebugWarn,"Consumer '%s' both record with no data channel!",both->c_str());
	return false;
    }

    const char* targetid = msg.getValue(YSTRING("notify"));
    String format = msg.getValue(YSTRING("format"),s_defFormat);
    String muxFormat = format;
    muxFormat.startSkip("2*",false);
    switch (muxFormat.toInteger(s_dictSampleLen)) {
	case 1:
	case 2:
	    break;
	default:
	    format = "slin";
	    muxFormat = "slin";
    }
    muxFormat = "2*" + muxFormat;

    Message m("chan.record");
    m.addParam("call",*both);
    if (targetid)
	m.addParam("notify",targetid);
    m.addParam("format",format);
    m.copyParam(msg,YSTRING("append"));
    m.copyParam(msg,YSTRING("maxlen"));
    m.addParam("call_account",msg.getValue(YSTRING("both_account")),false);
    m.addParam("call_query",msg.getValue(YSTRING("both_query")),false);
    m.addParam("call_fallback",msg.getValue(YSTRING("both_fallback")),false);
    m.addParam("single",String::boolText(true));
    DataEndpoint* ep = new DataEndpoint;
    m.userData(ep);
    Engine::dispatch(m);
    RefPointer<DataConsumer> c = ep->getCallRecord();
    m.userData(0);
    TelEngine::destruct(ep);
    if (!c)
	return false;
    String error;
    String id;
    lock();
    id << m_prefix << m_id++;
    unlock();
    MuxSource* s = new MuxSource(id,targetid,muxFormat,msg,error,true);
    if (error.null()) {
	if (DataTranslator::attachChain(s,c)) {
	    // Consumers are kept referenced by the DataEndpoint
	    DataConsumer* dc = s->getConsumer(0);
	    de->setCallRecord(dc);
	    TelEngine::destruct(dc);
	    dc = s->getConsumer(1);
	    de->setPeerRecord(dc);
	    TelEngine::destruct(dc);
	}
	else
	    error = "Translator chain attach failure";
    }
    // Source is kept referenced by the consumers
    TelEngine::destruct(s);
    c = 0;
    if (error)
	msg.setParam("error",error);
    return error.null() && msg.getBoolValue(YSTRING("single"));
}

void MuxModule::statusParams(String& str)
{
    Module::statusParams(str);
    str.append("count=",",") << m_sources.count() << ",format=channels|targetid";
}

void MuxModule::statusDetail(String& str)
{
    Module::statusDetail(str);
    for (ObjList* o = m_sources.skipNull(); o; o = o->skipNext()) {
	MuxSource* s = static_cast<MuxSource*>(o->get());
	str.append(s->id(),",") << "=" << s->channels() << "|" << s->targetid();
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
