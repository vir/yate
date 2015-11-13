/**
 * wavefile.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Wave file driver (record+playback)
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

class WaveSource : public ThreadedSource
{
public:
    static WaveSource* create(const String& file, CallEndpoint* chan,
	bool autoclose, bool autorepeat, const NamedString* param);
    ~WaveSource();
    virtual void run();
    virtual void cleanup();
    virtual void attached(bool added);
    void setNotify(const String& id);
private:
    WaveSource(const char* file, CallEndpoint* chan, bool autoclose);
    void init(const String& file, bool autorepeat);
    void detectAuFormat();
    void detectWavFormat();
    void detectIlbcFormat();
    bool computeDataRate();
    void notify(WaveSource* source, const char* reason = 0);
    CallEndpoint* m_chan;
    Stream* m_stream;
    DataBlock m_data;
    bool m_swap;
    unsigned m_rate;
    unsigned m_brate;
    int64_t m_repeatPos;
    unsigned m_total;
    u_int64_t m_time;
    String m_id;
    bool m_autoclose;
    bool m_nodata;
};

class WaveConsumer : public DataConsumer
{
public:
    enum Header {
	None = 0,
	Au,
	Ilbc,
    };
    WaveConsumer(const String& file, CallEndpoint* chan, unsigned maxlen,
	const char* format, bool append, const NamedString* param);
    ~WaveConsumer();
    virtual bool setFormat(const DataFormat& format);
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags);
    virtual void attached(bool added);
    inline void setNotify(const String& id)
	{ m_id = id; }
private:
    void writeIlbcHeader() const;
    void writeAuHeader();
    CallEndpoint* m_chan;
    Stream* m_stream;
    bool m_swap;
    bool m_locked;
    bool m_created;
    Header m_header;
    unsigned m_total;
    unsigned m_maxlen;
    u_int64_t m_time;
    String m_id;
};

class WaveChan : public Channel
{
public:
    WaveChan(const String& file, bool record, unsigned maxlen, const NamedList& msg, const NamedString* param = 0);
    ~WaveChan();
    bool attachSource(const char* source);
    bool attachConsumer(const char* consumer);
};

class Disconnector : public Thread
{
public:
    Disconnector(CallEndpoint* chan, const String& id, WaveSource* source, WaveConsumer* consumer, bool disc, const char* reason = 0);
    virtual ~Disconnector();
    virtual void run();
    bool init();
private:
    RefPointer<CallEndpoint> m_chan;
    Message* m_msg;
    WaveSource* m_source;
    WaveConsumer* m_consumer;
    bool m_disc;
};

class AttachHandler;

class WaveFileDriver : public Driver
{
public:
    WaveFileDriver();
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest);
protected:
    void statusParams(String& str);
private:
    AttachHandler* m_handler;
};

Mutex s_mutex(false,"WaveFile");
int s_reading = 0;
int s_writing = 0;
bool s_dataPadding = true;
bool s_pubReadable = false;

INIT_PLUGIN(WaveFileDriver);


class AttachHandler : public MessageHandler
{
public:
    AttachHandler()
	: MessageHandler("chan.attach",100,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class RecordHandler : public MessageHandler
{
public:
    RecordHandler()
	: MessageHandler("chan.record",100,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};


typedef struct {
    uint32_t sign;
    uint32_t offs;
    uint32_t len;
    uint32_t form;
    uint32_t freq;
    uint32_t chan;
} AuHeader;

#define ILBC_HEADER_LEN 9

static const char* ilbcFormat(Stream& stream)
{
    char header[ILBC_HEADER_LEN+1];
    if (stream.readData(&header,ILBC_HEADER_LEN) == ILBC_HEADER_LEN) {
	header[ILBC_HEADER_LEN] = '\0';
	if (::strcmp(header,"#!iLBC20\n") == 0)
	    return "ilbc20";
	else if (::strcmp(header,"#!iLBC30\n") == 0)
	    return "ilbc30";
    }
    return 0;
}


WaveSource* WaveSource::create(const String& file, CallEndpoint* chan, bool autoclose, bool autorepeat, const NamedString* param)
{
    WaveSource* tmp = new WaveSource(file,chan,autoclose);
    NamedPointer* ptr = YOBJECT(NamedPointer,param);
    if (ptr) {
	Stream* stream = YOBJECT(Stream,ptr);
	if (stream) {
	    DDebug(&__plugin,DebugInfo,"WaveSource using Stream %p [%p]",stream,tmp);
	    tmp->m_stream = stream;
	    ptr->takeData();
	}
	else {
	    DataBlock* data = YOBJECT(DataBlock,ptr);
	    if (data) {
		DDebug(&__plugin,DebugInfo,"WaveSource using DataBlock %p len=%u [%p]",
		    data,data->length(),tmp);
		tmp->m_stream = new MemoryStream(*data);
	    }
	}
    }
    tmp->init(file,autorepeat);
    return tmp;
}

void WaveSource::init(const String& file, bool autorepeat)
{
    if (!m_stream) {
	if (file == "-") {
	    m_nodata = true;
	    m_rate = 8000;
	    m_brate = 8000;
	    start("Wave Source");
	    return;
	}
	m_stream = new File;
	if (!static_cast<File*>(m_stream)->openPath(file,false,true,false,false,true)) {
	    Debug(DebugWarn,"Opening '%s': error %d: %s",
		file.c_str(), m_stream->error(), ::strerror(m_stream->error()));
	    delete m_stream;
	    m_stream = 0;
	    m_format.clear();
	    notify(this,"error");
	    return;
	}
    }
    if (file.endsWith(".gsm"))
	m_format = "gsm";
    else if (file.endsWith(".alaw") || file.endsWith(".A"))
	m_format = "alaw";
    else if (file.endsWith(".mulaw") || file.endsWith(".u"))
	m_format = "mulaw";
    else if (file.endsWith(".2slin"))
	m_format = "2*slin";
    else if (file.endsWith(".2alaw"))
	m_format = "2*alaw";
    else if (file.endsWith(".2mulaw"))
	m_format = "2*mulaw";
    else if (file.endsWith(".ilbc20"))
	m_format = "ilbc20";
    else if (file.endsWith(".ilbc30"))
	m_format = "ilbc30";
    else if (file.endsWith(".g729"))
	m_format = "g729";
    else if (file.endsWith(".au"))
	detectAuFormat();
    else if (file.endsWith(".wav"))
	detectWavFormat();
    else if (file.endsWith(".lbc"))
	detectIlbcFormat();
    else if (!file.endsWith(".slin"))
	Debug(DebugMild,"Unknown format for playback file '%s', assuming signed linear",file.c_str());
    if (computeDataRate()) {
	if (autorepeat)
	    m_repeatPos = m_stream->seek(Stream::SeekCurrent);
	start("Wave Source");
    }
    else {
	Debug(DebugWarn,"Unable to compute data rate for file '%s'",file.c_str());
	notify(this,"error");
    }
}

WaveSource::WaveSource(const char* file, CallEndpoint* chan, bool autoclose)
    : m_chan(chan), m_stream(0), m_swap(false), m_rate(8000), m_brate(0), m_repeatPos(-1),
      m_total(0), m_time(0), m_autoclose(autoclose),
      m_nodata(false)
{
    Debug(&__plugin,DebugAll,"WaveSource::WaveSource(\"%s\",%p) [%p]",file,chan,this);
    s_mutex.lock();
    s_reading++;
    s_mutex.unlock();
}

WaveSource::~WaveSource()
{
    Debug(&__plugin,DebugAll,"WaveSource::~WaveSource() [%p] total=%u stamp=%lu",this,m_total,timeStamp());
    stop();
    if (m_time) {
        m_time = Time::now() - m_time;
	if (m_time) {
	    m_time = (m_total*(u_int64_t)1000000 + m_time/2) / m_time;
	    Debug(&__plugin,DebugInfo,"WaveSource rate=" FMT64U " b/s",m_time);
	}
    }
    delete m_stream;
    m_stream = 0;
    s_mutex.lock();
    s_reading--;
    s_mutex.unlock();
}

void WaveSource::detectAuFormat()
{
    AuHeader header;
    if ((m_stream->readData(&header,sizeof(header)) != sizeof(header)) ||
	(ntohl(header.sign) != 0x2E736E64)) {
	Debug(DebugMild,"Invalid .au file header, assuming raw signed linear");
	m_stream->seek(0);
	return;
    }
    m_stream->seek(ntohl(header.offs));
    int samp = ntohl(header.freq);
    int chan = ntohl(header.chan);
    m_rate = samp;
    m_brate = samp;
    switch (ntohl(header.form)) {
	case 1:
	    m_format = "mulaw";
	    break;
	case 27:
	    m_format = "alaw";
	    break;
	case 3:
	    m_brate *= 2;
	    m_swap = true;
	    break;
	default:
	    Debug(DebugMild,"Unknown .au format 0x%0X, assuming signed linear",ntohl(header.form));
    }
    if (samp != 8000)
	m_format << "/" << samp;
    if (chan > 1) {
	m_format = String(chan) + "*" + m_format;
	m_brate *= chan;
    }
}

void WaveSource::detectWavFormat()
{
    Debug(DebugMild,".wav not supported yet, assuming raw signed linear");
}

void WaveSource::detectIlbcFormat()
{
    const char* fmt = ilbcFormat(*m_stream);
    if (fmt) {
	m_format = fmt;
	return;
    }
    Debug(DebugMild,"Invalid iLBC file, assuming raw signed linear");
    m_stream->seek(0);
}

bool WaveSource::computeDataRate()
{
    if (m_brate)
	return true;
    const FormatInfo* info = m_format.getInfo();
    if (!info)
	return false;
    m_rate = info->sampleRate;
    m_brate = info->dataRate();
    return (m_brate != 0);
}

void WaveSource::run()
{
    unsigned long ts = 0;
    int r = 0;
    // internally reference if used for override or replace purpose
    bool noChan = (0 == m_chan);
    // wait until at least one consumer is attached
    unsigned int fast = 4;
    while (!r) {
	lock();
	r = m_consumers.count();
	unlock();
	if (r)
	    ;
	else if (fast) {
	    --fast;
	    Thread::yield();
	}
	else
	    Thread::idle();
	if (!looping(noChan)) {
	    notify(0,"replaced");
	    return;
	}
    }
    unsigned int blen = (m_brate*20)/1000;
    DDebug(&__plugin,DebugAll,"Consumer found, starting to play data with rate %d [%p]",m_brate,this);
    m_data.assign(0,blen);
    u_int64_t tpos = 0;
    m_time = tpos;
    while ((r > 0) && looping(noChan)) {
	r = m_stream ? m_stream->readData(m_data.data(),m_data.length()) : m_data.length();
	if (r < 0) {
	    if (m_stream->canRetry()) {
		if (looping(noChan)) {
		    r = 1;
		    continue;
		}
		r = 0;
	    }
	    break;
	}
	// start counting time after the first successful read
	if (!tpos)
	    m_time = tpos = Time::now();
	if (!r) {
	    if (m_repeatPos >= 0) {
		DDebug(&__plugin,DebugAll,"Autorepeating from offset " FMT64 " [%p]",
		    m_repeatPos,this);
		m_stream->seek(m_repeatPos);
		m_data.assign(0,blen);
		r = 1;
		continue;
	    }
	    break;
	}
	if (r < (int)m_data.length()) {
	    // if desired and possible extend last byte to fill buffer
	    if (s_dataPadding && ((m_format == "mulaw") || (m_format == "alaw"))) {
		unsigned char* d = (unsigned char*)m_data.data();
		unsigned char last = d[r-1];
		while (r < (int)m_data.length())
		    d[r++] = last;
	    }
	    else
		m_data.assign(m_data.data(),r);
	}
	if (m_swap) {
	    uint16_t* p = (uint16_t*)m_data.data();
	    for (int i = 0; i < r; i+= 2) {
		*p = ntohs(*p);
		++p;
	    }
	}
	int64_t dly = tpos - Time::now();
	if (dly > 0) {
	    XDebug(&__plugin,DebugAll,"WaveSource sleeping for " FMT64 " usec",dly);
	    Thread::usleep((unsigned long)dly);
	}
	if (!looping(noChan))
	    break;
	Forward(m_data,ts);
	ts += m_data.length()*m_rate/m_brate;
	m_total += r;
	tpos += (r*(u_int64_t)1000000/m_brate);
    }
    if (r)
	notify(0,"replaced");
    else {
	Debug(&__plugin,DebugAll,"WaveSource '%s' end of data (%u played) chan=%p [%p]",
	    m_id.c_str(),m_total,m_chan,this);
	notify(this,"eof");
    }
}

void WaveSource::cleanup()
{
    RefPointer<CallEndpoint> chan;
    if (m_chan) {
	DataEndpoint::commonMutex().lock();
	chan = m_chan;
	m_chan = 0;
	DataEndpoint::commonMutex().unlock();
    }
    Debug(&__plugin,DebugAll,"WaveSource cleanup, total=%u, chan=%p [%p]",
	m_total,(void*)chan,this);
    if (chan)
	chan->clearData(this);
    ThreadedSource::cleanup();
}

void WaveSource::attached(bool added)
{
    if (!added && m_chan && !m_chan->alive()) {
	DDebug(&__plugin,DebugInfo,"WaveSource clearing dead chan %p [%p]",m_chan,this);
	m_chan = 0;
    }
}

void WaveSource::setNotify(const String& id)
{
    m_id = id;
    if (!(m_stream || m_nodata))
	notify(this);
}

void WaveSource::notify(WaveSource* source, const char* reason)
{
    RefPointer<CallEndpoint> chan;
    if (m_chan) {
	DataEndpoint::commonMutex().lock();
	if (source)
	    chan = m_chan;
	m_chan = 0;
	DataEndpoint::commonMutex().unlock();
    }
    if (!chan) {
	if (m_id) {
	    DDebug(&__plugin,DebugAll,"WaveSource enqueueing notify message [%p]",this);
	    Message* m = new Message("chan.notify");
	    m->addParam("targetid",m_id);
	    if (reason)
		m->addParam("reason",reason);
	    Engine::enqueue(m);
	}
    }
    else if (m_id || m_autoclose) {
	DDebug(&__plugin,DebugInfo,"Preparing '%s' disconnector for '%s' chan %p '%s' source=%p [%p]",
	    reason,m_id.c_str(),(void*)chan,chan->id().c_str(),source,this);
	Disconnector *disc = new Disconnector(chan,m_id,source,0,m_autoclose,reason);
	if (!disc->init() && m_autoclose)
	    chan->clearData(source);
    }
    stop();
}


WaveConsumer::WaveConsumer(const String& file, CallEndpoint* chan, unsigned maxlen,
    const char* format, bool append, const NamedString* param)
    : m_chan(chan), m_stream(0), m_swap(false), m_locked(false), m_created(true), m_header(None),
      m_total(0), m_maxlen(maxlen), m_time(0)
{
    Debug(&__plugin,DebugAll,"WaveConsumer::WaveConsumer(\"%s\",%p,%u,\"%s\",%s,%p) [%p]",
	file.c_str(),chan,maxlen,format,String::boolText(append),param,this);
    s_mutex.lock();
    s_writing++;
    s_mutex.unlock();
    if (format) {
	m_locked = true;
	m_format = format;
    }
    NamedPointer* ptr = YOBJECT(NamedPointer,param);
    if (ptr) {
	m_stream = YOBJECT(Stream,ptr);
	if (m_stream) {
	    DDebug(&__plugin,DebugInfo,"WaveConsumer using Stream %p [%p]",m_stream,this);
	    ptr->takeData();
	}
    }
    if (file == "-")
	return;
    else if (file.endsWith(".gsm"))
	m_format = "gsm";
    else if (file.endsWith(".alaw") || file.endsWith(".A"))
	m_format = "alaw";
    else if (file.endsWith(".mulaw") || file.endsWith(".u"))
	m_format = "mulaw";
    else if (file.endsWith(".2slin"))
	m_format = "2*slin";
    else if (file.endsWith(".2alaw"))
	m_format = "2*alaw";
    else if (file.endsWith(".2mulaw"))
	m_format = "2*mulaw";
    else if (file.endsWith(".ilbc20"))
	m_format = "ilbc20";
    else if (file.endsWith(".ilbc30"))
	m_format = "ilbc30";
    else if (file.endsWith(".g729"))
	m_format = "g729";
    else if (file.endsWith(".lbc")) {
	m_header = Ilbc;
	if (!m_format.startsWith("ilbc"))
	    m_locked = false;
    }
    else if (file.endsWith(".au"))
	m_header = Au;
    else if (!file.endsWith(".slin"))
	Debug(DebugMild,"Unknown format for recorded file '%s', assuming signed linear",file.c_str());
    if (m_stream)
	return;
    m_stream = new File;
    if (!static_cast<File*>(m_stream)->openPath(file,true,append,true,append,true,s_pubReadable)) {
	Debug(DebugWarn,"Creating '%s': error %d: %s",
	    file.c_str(), m_stream->error(), ::strerror(m_stream->error()));
	delete m_stream;
	m_stream = 0;
	return;
    }
    if (append && m_stream->seek(Stream::SeekEnd) > 0) {
	// remember to skip writing the header when appending to non-empty file
	m_created = false;
	switch (m_header) {
	    // TODO: Au
	    case Ilbc:
		if (m_stream->seek(0) == 0) {
		    const char* fmt = ilbcFormat(*m_stream);
		    m_stream->seek(Stream::SeekEnd);
		    if (fmt) {
			if (m_format != fmt)
			    Debug(DebugInfo,"Detected format %s for file '%s'",fmt,file.c_str());
			m_locked = true;
			m_format = fmt;
		    }
		    else
			Debug(DebugMild,"Could not detect iLBC format of '%s'",file.c_str());
		}
		break;
	    default:
		break;
	}
    }
}

WaveConsumer::~WaveConsumer()
{
    Debug(&__plugin,DebugAll,"WaveConsumer::~WaveConsumer() [%p] total=%u stamp=%lu",this,m_total,timeStamp());
    if (m_time) {
        m_time = Time::now() - m_time;
	if (m_time) {
	    m_time = (m_total*(u_int64_t)1000000 + m_time/2) / m_time;
	    Debug(&__plugin,DebugInfo,"WaveConsumer rate=" FMT64U " b/s",m_time);
	}
    }
    delete m_stream;
    m_stream = 0;
    s_mutex.lock();
    s_writing--;
    s_mutex.unlock();
}

void WaveConsumer::writeIlbcHeader() const
{
    if (m_format == "ilbc20")
	m_stream->writeData("#!iLBC20\n",ILBC_HEADER_LEN);
    else if (m_format == "ilbc30")
	m_stream->writeData("#!iLBC30\n",ILBC_HEADER_LEN);
    else
	Debug(DebugMild,"Invalid iLBC format '%s', not writing header",m_format.c_str());
}

void WaveConsumer::writeAuHeader()
{
    AuHeader header;
    String fmt = m_format;
    int chans = 1;
    int rate = 8000;
    int sep = fmt.find('*');
    if (sep > 0)
	fmt >> chans >> "*";
    sep = fmt.find('/');
    if (sep > 0) {
	rate = fmt.substr(sep+1).toInteger(rate);
	fmt.assign(fmt,sep);
    }
    if (fmt == "slin") {
	m_swap = true;
	header.form = htonl(3);
    }
    else if (fmt == "mulaw")
	header.form = htonl(1);
    else if (fmt == "alaw")
	header.form = htonl(27);
    else {
	Debug(DebugMild,"Invalid au format '%s', not writing header",m_format.c_str());
	return;
    }
    header.sign = htonl(0x2E736E64);
    header.offs = htonl(sizeof(header));
    header.freq = htonl(rate);
    header.chan = htonl(chans);
    header.len = 0;
    m_stream->writeData(&header,sizeof(header));
}

bool WaveConsumer::setFormat(const DataFormat& format)
{
    if (m_locked || (format == "slin"))
	return false;
    bool ok = false;
    switch (m_header) {
	case Ilbc:
	    if ((format == "ilbc20") || (format == "ilbc30"))
		ok = true;
	    else if (!m_format.startsWith("ilbc")) {
		m_format = "ilbc20";
		Debug(DebugNote,"WaveConsumer switching to default '%s'",m_format.c_str());
	    }
	    break;
	case Au:
	    if ((format.find("mulaw") >= 0) || (format.find("alaw") >= 0) || (format.find("slin") >= 0))
		ok = true;
	    break;
	default:
	    break;
    }
    if (ok) {
	DDebug(DebugInfo,"WaveConsumer new format '%s'",format.c_str());
	m_format = format;
	m_locked = true;
	return true;
    }
    return false;
}

unsigned long WaveConsumer::Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
{
    if (!data.null()) {
	if (!m_time)
	    m_time = Time::now();
	if (m_stream) {
	    if (m_created) {
		m_created = false;
		switch (m_header) {
		    case Ilbc:
			writeIlbcHeader();
			break;
		    case Au:
			writeAuHeader();
			break;
		    default:
			break;
		}
	    }
	    if (m_swap) {
		unsigned int n = data.length();
		DataBlock swapped(0,n);
		const uint16_t* s = (const uint16_t*)data.data();
		uint16_t* d = (uint16_t*)swapped.data();
		for (unsigned int i = 0; i < n; i+= 2)
		    *d++ = htons(*s++);
		m_stream->writeData(swapped);
	    }
	    else
		m_stream->writeData(data);
	}
	m_total += data.length();
	if (m_maxlen && (m_total >= m_maxlen)) {
	    m_maxlen = 0;
	    delete m_stream;
	    m_stream = 0;
	    RefPointer<CallEndpoint> chan;
	    if (m_chan) {
		DataEndpoint::commonMutex().lock();
		chan = m_chan;
		m_chan = 0;
		DataEndpoint::commonMutex().unlock();
	    }
	    if (chan) {
		DDebug(&__plugin,DebugInfo,"Preparing 'maxlen' disconnector for '%s' chan %p '%s' in consumer [%p]",
		    m_id.c_str(),(void*)chan,chan->id().c_str(),this);
		Disconnector *disc = new Disconnector(chan,m_id,0,this,false,"maxlen");
		disc->init();
	    }
	}
	return invalidStamp();
    }
    return 0;
}

void WaveConsumer::attached(bool added)
{
    if (!added && m_chan && !m_chan->alive()) {
	DDebug(&__plugin,DebugInfo,"WaveConsumer clearing dead chan %p [%p]",m_chan,this);
	m_chan = 0;
    }
}


Disconnector::Disconnector(CallEndpoint* chan, const String& id, WaveSource* source, WaveConsumer* consumer, bool disc, const char* reason)
    : Thread("WaveDisconnector"),
      m_chan(chan), m_msg(0), m_source(0), m_consumer(consumer), m_disc(disc)
{
    if (id) {
	Message* m = new Message("chan.notify");
	if (m_chan)
	    m->addParam("id",m_chan->id());
	m->addParam("targetid",id);
	if (reason)
	    m->addParam("reason",reason);
	m->userData(m_chan);
	m_msg = m;
    }
}

Disconnector::~Disconnector()
{
    if (m_msg) {
	DDebug(&__plugin,DebugAll,"Disconnector enqueueing notify message [%p]",this);
	Engine::enqueue(m_msg);
    }
}

bool Disconnector::init()
{
    if (error() || !startup()) {
	Debug(&__plugin,DebugGoOn,"Error starting disconnector thread %p",this);
	delete this;
	return false;
    }
    return true;
}

void Disconnector::run()
{
    DDebug(&__plugin,DebugAll,"Disconnector::run() chan=%p msg=%p source=%p disc=%s [%p]",
	(void*)m_chan,m_msg,m_source,String::boolText(m_disc),this);
    if (!m_chan)
	return;
    if (m_source) {
	if (!m_chan->clearData(m_source))
	    Debug(&__plugin,DebugNote,"Source %p in channel %p was replaced with %p",
		m_source,(void*)m_chan,m_chan->getSource());
	if (m_disc)
	    m_chan->disconnect("eof");
    }
    else {
	if (m_msg)
	    m_chan->clearData(m_consumer);
	else
	    m_chan->disconnect();
    }
}


WaveChan::WaveChan(const String& file, bool record, unsigned maxlen, const NamedList& msg, const NamedString* param)
    : Channel(__plugin)
{
    Debug(this,DebugAll,"WaveChan::WaveChan(%s) [%p]",(record ? "record" : "play"),this);
    if (record) {
	setConsumer(new WaveConsumer(file,this,maxlen,msg.getValue("format"),
	    msg.getBoolValue("append"),param));
	getConsumer()->deref();
    }
    else {
	setSource(WaveSource::create(file,this,true,msg.getBoolValue("autorepeat"),param));
	getSource()->deref();
    }
}

WaveChan::~WaveChan()
{
    Debug(this,DebugAll,"WaveChan::~WaveChan() %s [%p]",id().c_str(),this);
}

bool WaveChan::attachSource(const char* source)
{
    if (TelEngine::null(source))
	return false;
    Message m("chan.attach");
    m.userData(this);
    m.addParam("id",id());
    m.addParam("source",source);
    m.addParam("single",String::boolText(true));
    return Engine::dispatch(m);
}

bool WaveChan::attachConsumer(const char* consumer)
{
    if (TelEngine::null(consumer))
	return false;
    Message m("chan.attach");
    m.userData(this);
    m.addParam("id",id());
    m.addParam("consumer",consumer);
    m.addParam("single",String::boolText(true));
    return Engine::dispatch(m);
}


bool AttachHandler::received(Message &msg)
{
    int more = 4;
    String src(msg.getValue("source"));
    if (src.null())
	more--;
    else {
	static const Regexp r("^wave/\\([^/]*\\)/\\(.*\\)$");
	if (src.matches(r)) {
	    if (src.matchString(1) == "play") {
		src = src.matchString(2);
		more--;
	    }
	    else {
		Debug(DebugWarn,"Could not attach source with method '%s', use 'play'",
		    src.matchString(1).c_str());
		src.clear();
	    }
	}
	else
	    src.clear();
    }

    String cons(msg.getValue("consumer"));
    if (cons.null())
	more--;
    else {
	static const Regexp r("^wave/\\([^/]*\\)/\\(.*\\)$");
	if (cons.matches(r)) {
	    if (cons.matchString(1) == "record") {
		cons = cons.matchString(2);
		more--;
	    }
	    else {
		Debug(DebugWarn,"Could not attach consumer with method '%s', use 'record'",
		    cons.matchString(1).c_str());
		cons.clear();
	    }
	}
	else
	    cons.clear();
    }

    String ovr(msg.getValue("override"));
    if (ovr.null())
	more--;
    else {
	static const Regexp r("^wave/\\([^/]*\\)/\\(.*\\)$");
	if (ovr.matches(r)) {
	    if (ovr.matchString(1) == "play") {
		ovr = ovr.matchString(2);
		more--;
	    }
	    else {
		Debug(DebugWarn,"Could not attach override with method '%s', use 'play'",
		    ovr.matchString(1).c_str());
		ovr.clear();
	    }
	}
	else
	    ovr.clear();
    }

    String repl(msg.getValue("replace"));
    if (repl.null())
	more--;
    else {
	static const Regexp r("^wave/\\([^/]*\\)/\\(.*\\)$");
	if (repl.matches(r)) {
	    if (repl.matchString(1) == "play") {
		repl = repl.matchString(2);
		more--;
	    }
	    else {
		Debug(DebugWarn,"Could not attach replacement with method '%s', use 'play'",
		    repl.matchString(1).c_str());
		repl.clear();
	    }
	}
	else
	    repl.clear();
    }

    if (src.null() && cons.null() && ovr.null() && repl.null())
	return false;

    // if single attach was requested we can return true if everything is ok
    bool ret = msg.getBoolValue("single");

    CallEndpoint* ch = YOBJECT(CallEndpoint,msg.userData());
    if (!ch) {
	if (!src.null())
	    Debug(DebugWarn,"Wave source '%s' attach request with no data channel!",src.c_str());
	if (!cons.null())
	    Debug(DebugWarn,"Wave consumer '%s' attach request with no data channel!",cons.c_str());
	if (!ovr.null())
	    Debug(DebugWarn,"Wave override '%s' attach request with no data channel!",ovr.c_str());
	return false;
    }

    const char* notify = msg.getValue("notify");
    unsigned int maxlen = msg.getIntValue("maxlen");

    if (!src.null()) {
	WaveSource* s = WaveSource::create(src,ch,false,msg.getBoolValue("autorepeat"),msg.getParam("source"));
	ch->setSource(s);
	s->setNotify(msg.getValue("notify_source",notify));
	s->deref();
	msg.clearParam("source");
    }

    if (!cons.null()) {
	WaveConsumer* c = new WaveConsumer(cons,ch,maxlen,msg.getValue("format"),
	    msg.getBoolValue("append"),msg.getParam("consumer"));
	c->setNotify(msg.getValue("notify_consumer",notify));
	ch->setConsumer(c);
	c->deref();
	msg.clearParam("consumer");
    }

    while (!ovr.null()) {
	DataEndpoint::commonMutex().lock();
	RefPointer<DataConsumer> c = ch->getConsumer();
	DataEndpoint::commonMutex().unlock();
	if (!c) {
	    Debug(DebugWarn,"Wave override '%s' attach request with no consumer!",ovr.c_str());
	    ret = false;
	    break;
	}
	WaveSource* s = WaveSource::create(ovr,0,false,msg.getBoolValue("autorepeat"),msg.getParam("override"));
	s->setNotify(msg.getValue("notify_override",notify));
	if (DataTranslator::attachChain(s,c,true))
	    msg.clearParam("override");
	else {
	    Debug(DebugWarn,"Failed to override attach wave '%s' to consumer %p",
		ovr.c_str(),(void*)c);
	    ret = false;
	}
	s->deref();
	break;
    }

    while (!repl.null()) {
	DataEndpoint::commonMutex().lock();
	RefPointer<DataConsumer> c = ch->getConsumer();
	DataEndpoint::commonMutex().unlock();
	if (!c) {
	    Debug(DebugWarn,"Wave replacement '%s' attach request with no consumer!",repl.c_str());
	    ret = false;
	    break;
	}
	WaveSource* s = WaveSource::create(repl,0,false,msg.getBoolValue("autorepeat"),msg.getParam("replace"));
	s->setNotify(msg.getValue("notify_replace",notify));
	if (DataTranslator::attachChain(s,c,false))
	    msg.clearParam("replace");
	else {
	    Debug(DebugWarn,"Failed to replacement attach wave '%s' to consumer %p",
		repl.c_str(),(void*)c);
	    ret = false;
	}
	s->deref();
	break;
    }

    // Stop dispatching if we handled all requested
    return ret && !more;
}


bool RecordHandler::received(Message &msg)
{
    int more = 2;

    String c1(msg.getValue("call"));
    if (c1.null())
	more--;
    else {
	static const Regexp r("^wave/\\([^/]*\\)/\\(.*\\)$");
	if (c1.matches(r)) {
	    if (c1.matchString(1) == "record") {
		c1 = c1.matchString(2);
		more--;
	    }
	    else {
		Debug(DebugWarn,"Could not attach call recorder with method '%s', use 'record'",
		    c1.matchString(1).c_str());
		c1.clear();
	    }
	}
	else
	    c1.clear();
    }

    String c2(msg.getValue("peer"));
    if (c2.null())
	more--;
    else {
	static const Regexp r("^wave/\\([^/]*\\)/\\(.*\\)$");
	if (c2.matches(r)) {
	    if (c2.matchString(1) == "record") {
		c2 = c2.matchString(2);
		more--;
	    }
	    else {
		Debug(DebugWarn,"Could not attach peer recorder with method '%s', use 'record'",
		    c2.matchString(1).c_str());
		c2.clear();
	    }
	}
	else
	    c2.clear();
    }

    if (c1.null() && c2.null())
	return false;

    const char* notify = msg.getValue("notify");
    unsigned int maxlen = msg.getIntValue("maxlen");

    CallEndpoint* ch = YOBJECT(CallEndpoint,msg.userData());
    RefPointer<DataEndpoint> de = YOBJECT(DataEndpoint,msg.userData());
    if (ch && !de)
	de = ch->setEndpoint();

    if (!de) {
	if (!c1.null())
	    Debug(DebugWarn,"Wave source '%s' call record with no data channel!",c1.c_str());
	if (!c2.null())
	    Debug(DebugWarn,"Wave source '%s' peer record with no data channel!",c2.c_str());
	return false;
    }

    bool append = msg.getBoolValue("append");
    const char* format = msg.getValue("format");
    if (!c1.null()) {
	WaveConsumer* c = new WaveConsumer(c1,ch,maxlen,format,append,msg.getParam("call"));
	c->setNotify(msg.getValue("notify_call",notify));
	de->setCallRecord(c);
	c->deref();
    }

    if (!c2.null()) {
	WaveConsumer* c = new WaveConsumer(c2,ch,maxlen,format,append,msg.getParam("peer"));
	c->setNotify(msg.getValue("notify_peer",notify));
	de->setPeerRecord(c);
	c->deref();
    }

    // Stop dispatching if we handled all requested
    return !more;
}


bool WaveFileDriver::msgExecute(Message& msg, String& dest)
{
    static const Regexp r("^\\([^/]*\\)/\\(.*\\)$");
    if (!dest.matches(r))
	return false;

    bool meth = false;
    if (dest.matchString(1) == "record")
	meth = true;
    else if (dest.matchString(1) != "play") {
	Debug(DebugWarn,"Invalid wavefile method '%s', use 'record' or 'play'",
	    dest.matchString(1).c_str());
	return false;
    }

    unsigned int maxlen = msg.getIntValue("maxlen");
    CallEndpoint* ch = YOBJECT(CallEndpoint,msg.userData());
    if (ch) {
	Debug(this,DebugInfo,"%s wave file '%s'", (meth ? "Record to" : "Play from"),
	    dest.matchString(2).c_str());
	WaveChan *c = new WaveChan(dest.matchString(2),meth,maxlen,msg,msg.getParam("callto"));
	c->initChan();
	if (meth)
	    c->attachSource(msg.getValue("source"));
	else
	    c->attachConsumer(msg.getValue("consumer"));
	if (ch->connect(c,msg.getValue("reason"))) {
	    c->callConnect(msg);
	    msg.setParam("peerid",c->id());
	    c->deref();
	    return true;
	}
	else {
	    c->destruct();
	    return false;
	}
    }
    Message m("call.route");
    m.copyParams(msg,msg[YSTRING("copyparams")]);
    m.clearParam(YSTRING("callto"));
    m.clearParam(YSTRING("id"));
    m.setParam("module",name());
    m.setParam("cdrtrack",String::boolText(false));
    m.copyParam(msg,YSTRING("called"));
    m.copyParam(msg,YSTRING("caller"));
    m.copyParam(msg,YSTRING("callername"));
    String callto(msg.getValue(YSTRING("direct")));
    if (callto.null()) {
	const char *targ = msg.getValue(YSTRING("target"));
	if (!targ)
	    targ = msg.getValue(YSTRING("called"));
	if (!targ) {
	    Debug(DebugWarn,"Wave outgoing call with no target!");
	    return false;
	}
	m.setParam("called",targ);
	if (!m.getValue(YSTRING("caller")))
	    m.setParam("caller",prefix() + dest);
	if (!Engine::dispatch(m)) {
	    Debug(DebugWarn,"Wave outgoing call but no route!");
	    return false;
	}
	callto = m.retValue();
	m.retValue().clear();
    }
    m = "call.execute";
    m.setParam("callto",callto);
    WaveChan *c = new WaveChan(dest.matchString(2),meth,maxlen,msg);
    c->initChan();
    if (meth)
	c->attachSource(msg.getValue("source"));
    else
	c->attachConsumer(msg.getValue("consumer"));
    m.setParam("id",c->id());
    m.userData(c);
    if (Engine::dispatch(m)) {
	msg.setParam("id",c->id());
	msg.copyParam(m,YSTRING("peerid"));
	c->deref();
	return true;
    }
    Debug(DebugWarn,"Wave outgoing call not accepted!");
    c->destruct();
    return false;
}

void WaveFileDriver::statusParams(String& str)
{
    str.append("play=",",") << s_reading;
    str << ",record=" << s_writing;
    Driver::statusParams(str);
}

WaveFileDriver::WaveFileDriver()
    : Driver("wave","misc"), m_handler(0)
{
    Output("Loaded module WaveFile");
}

void WaveFileDriver::initialize()
{
    Output("Initializing module WaveFile");
    setup();
    s_dataPadding = Engine::config().getBoolValue("hacks","datapadding",true);
    s_pubReadable = Engine::config().getBoolValue("hacks","wavepubread",false);
    if (!m_handler) {
	m_handler = new AttachHandler;
	Engine::install(m_handler);
	Engine::install(new RecordHandler);
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
