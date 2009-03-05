/**
 * wavefile.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Wave file driver (record+playback)
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
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

#include <string.h>

using namespace TelEngine;
namespace { // anonymous

class WaveSource : public ThreadedSource
{
public:
    static WaveSource* create(const String& file, CallEndpoint* chan, bool autoclose = true, bool autorepeat = false);
    ~WaveSource();
    virtual void run();
    virtual void cleanup();
    virtual bool zeroRefsTest();
    void setNotify(const String& id);
    bool derefReady();
private:
    WaveSource(const char* file, CallEndpoint* chan, bool autoclose);
    void init(const String& file, bool autorepeat);
    void detectAuFormat();
    void detectWavFormat();
    void detectIlbcFormat();
    bool computeDataRate();
    bool notify(WaveSource* source, const char* reason = 0);
    CallEndpoint* m_chan;
    Stream* m_stream;
    DataBlock m_data;
    bool m_swap;
    unsigned m_brate;
    long m_repeatPos;
    unsigned m_total;
    u_int64_t m_time;
    String m_id;
    bool m_autoclose;
    bool m_autoclean;
    bool m_nodata;
    bool m_insert;
    volatile bool m_derefOk;
};

class WaveConsumer : public DataConsumer
{
public:
    enum Header {
	None = 0,
	Au,
	Ilbc,
    };
    WaveConsumer(const String& file, CallEndpoint* chan, unsigned maxlen, const char* format = 0);
    ~WaveConsumer();
    virtual bool setFormat(const DataFormat& format);
    virtual void Consume(const DataBlock& data, unsigned long tStamp);
    inline void setNotify(const String& id)
	{ m_id = id; }
private:
    void writeIlbcHeader() const;
    void writeAuHeader();
    CallEndpoint* m_chan;
    Stream* m_stream;
    bool m_swap;
    bool m_locked;
    Header m_header;
    unsigned m_total;
    unsigned m_maxlen;
    u_int64_t m_time;
    String m_id;
};

class WaveChan : public Channel
{
public:
    WaveChan(const String& file, bool record, unsigned maxlen, bool autorepeat, const char* format = 0);
    ~WaveChan();
};

class Disconnector : public Thread
{
public:
    Disconnector(CallEndpoint* chan, const String& id, WaveSource* source, bool disc, const char* reason = 0);
    virtual ~Disconnector();
    virtual void run();
    bool init();
private:
    RefPointer<CallEndpoint> m_chan;
    Message* m_msg;
    WaveSource* m_source;
    bool m_disc;
};

class AttachHandler : public MessageHandler
{
public:
    AttachHandler() : MessageHandler("chan.attach") { }
    virtual bool received(Message &msg);
};

class RecordHandler : public MessageHandler
{
public:
    RecordHandler() : MessageHandler("chan.record") { }
    virtual bool received(Message &msg);
};

class WaveFileDriver : public Driver
{
public:
    WaveFileDriver();
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest);
private:
    AttachHandler* m_handler;
};

bool s_asyncDelete = true;
bool s_dataPadding = true;
bool s_pubReadable = false;

#if defined (S_IRGRP) && defined(S_IROTH)
#define CREATE_MODE (S_IRUSR|S_IWUSR|(s_pubReadable?(S_IRGRP|S_IROTH):0))
#else
#define CREATE_MODE (S_IRUSR|S_IWUSR)
#endif

INIT_PLUGIN(WaveFileDriver);


typedef struct {
    uint32_t sign;
    uint32_t offs;
    uint32_t len;
    uint32_t form;
    uint32_t freq;
    uint32_t chan;
} AuHeader;

#define ILBC_HEADER_LEN 9


WaveSource* WaveSource::create(const String& file, CallEndpoint* chan, bool autoclose, bool autorepeat)
{
    WaveSource* tmp = new WaveSource(file,chan,autoclose);
    tmp->init(file,autorepeat);
    return tmp;
}

void WaveSource::init(const String& file, bool autorepeat)
{
    if (file == "-") {
	m_nodata = true;
	m_brate = 8000;
	start("WaveSource");
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
	asyncDelete(s_asyncDelete);
	start("WaveSource");
    }
    else {
	Debug(DebugWarn,"Unable to compute data rate for file '%s'",file.c_str());
	notify(this,"error");
    }
}

WaveSource::WaveSource(const char* file, CallEndpoint* chan, bool autoclose)
    : m_chan(chan), m_stream(0), m_swap(false), m_brate(0), m_repeatPos(-1),
      m_total(0), m_time(0), m_autoclose(autoclose), m_autoclean(false),
      m_nodata(false), m_insert(false), m_derefOk(true)
{
    Debug(&__plugin,DebugAll,"WaveSource::WaveSource(\"%s\",%p) [%p]",file,chan,this);
    if (m_chan)
	m_insert = true;
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
    char header[ILBC_HEADER_LEN+1];
    if (m_stream->readData(&header,ILBC_HEADER_LEN) == ILBC_HEADER_LEN) {
	header[ILBC_HEADER_LEN] = '\0';
	if (::strcmp(header,"#!iLBC20\n") == 0) {
	    m_format = "ilbc20";
	    return;
	}
	else if (::strcmp(header,"#!iLBC30\n") == 0) {
	    m_format = "ilbc30";
	    return;
	}
    }
    Debug(DebugMild,"Invalid iLBC file, assuming raw signed linear");
}

bool WaveSource::computeDataRate()
{
    if (m_brate)
	return true;
    const FormatInfo* info = m_format.getInfo();
    if (!info)
	return false;
    m_brate = info->dataRate();
    return (m_brate != 0);
}

void WaveSource::run()
{
    unsigned long ts = 0;
    int r = 0;
    // wait until at least one consumer is attached
    while (!r) {
	m_mutex.lock();
	r = m_consumers.count();
	m_mutex.unlock();
	Thread::yield();
	if (!alive()) {
	    notify(0,"replaced");
	    return;
	}
    }
    unsigned int blen = (m_brate*20)/1000;
    DDebug(&__plugin,DebugAll,"Consumer found, starting to play data with rate %d [%p]",m_brate,this);
    m_data.assign(0,blen);
    u_int64_t tpos = 0;
    m_time = tpos;
    do {
	r = m_stream ? m_stream->readData(m_data.data(),m_data.length()) : m_data.length();
	if (r < 0) {
	    if (m_stream->canRetry()) {
		r = 1;
		continue;
	    }
	    break;
	}
	// start counting time after the first successful read
	if (!tpos)
	    m_time = tpos = Time::now();
	if (!r) {
	    if (m_repeatPos >= 0) {
		DDebug(&__plugin,DebugAll,"Autorepeating from offset %ld [%p]",
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
	if (!alive()) {
	    notify(0,"replaced");
	    return;
	}
	Forward(m_data,ts);
	ts += m_data.length()*8000/m_brate;
	m_total += r;
	tpos += (r*(u_int64_t)1000000/m_brate);
    } while (r > 0);
    Debug(&__plugin,DebugAll,"WaveSource '%s' end of data (%u played) chan=%p [%p]",m_id.c_str(),m_total,m_chan,this);
    if (!ref()) {
	notify(0,"replaced");
	return;
    }
    // prevent disconnector thread from succeeding before notify returns
    m_derefOk = false;
    // at cleanup time deref the data source if we start no disconnector thread
    m_autoclean = !notify(this,"eof");
    if (!deref())
	m_derefOk = m_autoclean;
}

void WaveSource::cleanup()
{
    Lock lock(DataEndpoint::commonMutex());
    Debug(&__plugin,DebugAll,"WaveSource cleanup, total=%u, alive=%s, autoclean=%s chan=%p [%p]",
	m_total,String::boolText(alive()),String::boolText(m_autoclean),m_chan,this);
    clearThread();
    if (m_autoclean) {
	asyncDelete(false);
	if (m_insert) {
	    if (m_chan && (m_chan->getSource() == this))
		m_chan->setSource();
	}
	else
	    deref();
	return;
    }
    if (m_derefOk)
	ThreadedSource::cleanup();
    else
	m_derefOk = true;
}

bool WaveSource::zeroRefsTest()
{
    DDebug(&__plugin,DebugAll,"WaveSource::zeroRefsTest() chan=%p%s%s%s [%p]",
	m_chan,
	(thread() ? " thread" : ""),
	(m_autoclose ? " close" : ""),
	(m_autoclean ? " clean" : ""),
	this);
    // since this is a zombie it has no owner anymore and needs no removal
    m_chan = 0;
    m_autoclose = false;
    m_autoclean = false;
    return ThreadedSource::zeroRefsTest();
}

void WaveSource::setNotify(const String& id)
{
    m_id = id;
    if (!(m_stream || m_nodata))
	notify(this);
}

bool WaveSource::derefReady()
{
    for (int i = 0; i < 10; i++) {
	if (m_derefOk)
	    return true;
	Thread::yield();
    }
    Debug(&__plugin,DebugWarn,"Source not deref ready, waiting more... [%p]",this);
    Thread::msleep(10);
    return m_derefOk;
}

bool WaveSource::notify(WaveSource* source, const char* reason)
{
    if (!m_chan) {
	if (m_id) {
	    DDebug(&__plugin,DebugAll,"WaveSource enqueueing notify message [%p]",this);
	    Message* m = new Message("chan.notify");
	    m->addParam("targetid",m_id);
	    if (reason)
		m->addParam("reason",reason);
	    Engine::enqueue(m);
	}
	return false;
    }
    if (m_id || m_autoclose) {
	DDebug(&__plugin,DebugInfo,"Preparing '%s' disconnector for '%s' chan %p '%s' source=%p [%p]",
	    reason,m_id.c_str(),m_chan,(m_chan ? m_chan->id().c_str() : ""),source,this);
	Disconnector *disc = new Disconnector(m_chan,m_id,source,m_autoclose,reason);
	return disc->init();
    }
    return false;
}


WaveConsumer::WaveConsumer(const String& file, CallEndpoint* chan, unsigned maxlen, const char* format)
    : m_chan(chan), m_stream(0), m_swap(false), m_locked(false), m_header(None),
      m_total(0), m_maxlen(maxlen), m_time(0)
{
    Debug(&__plugin,DebugAll,"WaveConsumer::WaveConsumer(\"%s\",%p,%u,\"%s\") [%p]",
	file.c_str(),chan,maxlen,format,this);
    if (format) {
	m_locked = true;
	m_format = format;
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
    else if (file.endsWith(".lbc"))
	m_header=Ilbc;
    else if (file.endsWith(".au"))
	m_header=Au;
    else if (!file.endsWith(".slin"))
	Debug(DebugMild,"Unknown format for recorded file '%s', assuming signed linear",file.c_str());
    m_stream = new File;
    if (!static_cast<File*>(m_stream)->openPath(file,true,false,true,false,true)) {
	Debug(DebugWarn,"Creating '%s': error %d: %s",
	    file.c_str(), m_stream->error(), ::strerror(m_stream->error()));
	delete m_stream;
	m_stream = 0;
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

void WaveConsumer::Consume(const DataBlock& data, unsigned long tStamp)
{
    if (!data.null()) {
	if (!m_time)
	    m_time = Time::now();
	if (m_stream) {
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
	    m_header = None;
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
	    if (m_chan) {
		DDebug(&__plugin,DebugInfo,"Preparing 'maxlen' disconnector for '%s' chan %p '%s' in consumer [%p]",
		    m_id.c_str(),m_chan,(m_chan ? m_chan->id().c_str() : ""),this);
		Disconnector *disc = new Disconnector(m_chan,m_id,0,false,"maxlen");
		m_chan = 0;
		disc->init();
	    }
	}
    }
}


Disconnector::Disconnector(CallEndpoint* chan, const String& id, WaveSource* source, bool disc, const char* reason)
    : Thread("WaveDisconnector"),
      m_chan(chan), m_msg(0), m_source(0), m_disc(disc)
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
    if (source) {
	if (source->ref())
	    m_source = source;
	else {
	    Debug(&__plugin,DebugGoOn,"Disconnecting dead source %p, reason: '%s'",
		source,reason);
	    m_chan = 0;
	}
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
    if (error()) {
	Debug(&__plugin,DebugGoOn,"Error creating disconnector thread %p",this);
	delete this;
	return false;
    }
    return startup();
}

void Disconnector::run()
{
    DDebug(&__plugin,DebugAll,"Disconnector::run() chan=%p msg=%p source=%p disc=%s [%p]",
	(void*)m_chan,m_msg,m_source,String::boolText(m_disc),this);
    if (!m_chan)
	return;
    if (m_source) {
	if (m_chan->getSource() == m_source)
	    m_chan->setSource();
	else
	    Debug(&__plugin,DebugMild,"Source %p in channel %p was replaced with %p",
		m_source,(void*)m_chan,m_chan->getSource());
	if (!m_source->derefReady())
	    Debug(&__plugin,DebugGoOn,"Source %p is not deref ready, crash may occur",m_source);
	m_source->deref();
	if (m_disc)
	    m_chan->disconnect("eof");
    }
    else {
	if (m_msg)
	    m_chan->setConsumer();
	else
	    m_chan->disconnect();
    }
}


WaveChan::WaveChan(const String& file, bool record, unsigned maxlen, bool autorepeat, const char* format)
    : Channel(__plugin)
{
    Debug(this,DebugAll,"WaveChan::WaveChan(%s) [%p]",(record ? "record" : "play"),this);
    if (record) {
	setConsumer(new WaveConsumer(file,this,maxlen,format));
	getConsumer()->deref();
    }
    else {
	setSource(WaveSource::create(file,this,true,autorepeat));
	getSource()->deref();
    }
}

WaveChan::~WaveChan()
{
    Debug(this,DebugAll,"WaveChan::~WaveChan() %s [%p]",id().c_str(),this);
}


bool AttachHandler::received(Message &msg)
{
    int more = 4;
    String src(msg.getValue("source"));
    if (src.null())
	more--;
    else {
	Regexp r("^wave/\\([^/]*\\)/\\(.*\\)$");
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
	Regexp r("^wave/\\([^/]*\\)/\\(.*\\)$");
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
	Regexp r("^wave/\\([^/]*\\)/\\(.*\\)$");
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
	Regexp r("^wave/\\([^/]*\\)/\\(.*\\)$");
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

    String ml(msg.getValue("maxlen"));
    unsigned maxlen = ml.toInteger(0);
    CallEndpoint *ch = static_cast<CallEndpoint*>(msg.userData());
    if (!ch) {
	if (!src.null())
	    Debug(DebugWarn,"Wave source '%s' attach request with no data channel!",src.c_str());
	if (!cons.null())
	    Debug(DebugWarn,"Wave consumer '%s' attach request with no data channel!",cons.c_str());
	if (!ovr.null())
	    Debug(DebugWarn,"Wave override '%s' attach request with no data channel!",ovr.c_str());
	return false;
    }

    if (!src.null()) {
	WaveSource* s = WaveSource::create(src,ch,false,msg.getBoolValue("autorepeat"));
	ch->setSource(s);
	s->setNotify(msg.getValue("notify"));
	s->deref();
	msg.clearParam("source");
    }

    if (!cons.null()) {
	WaveConsumer* c = new WaveConsumer(cons,ch,maxlen,msg.getValue("format"));
	c->setNotify(msg.getValue("notify"));
	ch->setConsumer(c);
	c->deref();
	msg.clearParam("consumer");
    }

    while (!ovr.null()) {
	DataConsumer* c = ch->getConsumer();
	if (!c) {
	    Debug(DebugWarn,"Wave override '%s' attach request with no consumer!",ovr.c_str());
	    ret = false;
	    break;
	}
	WaveSource* s = WaveSource::create(ovr,0,false,msg.getBoolValue("autorepeat"));
	s->setNotify(msg.getValue("notify"));
	if (DataTranslator::attachChain(s,c,true))
	    msg.clearParam("override");
	else {
	    Debug(DebugWarn,"Failed to override attach wave '%s' to consumer %p",ovr.c_str(),c);
	    s->deref();
	    ret = false;
	}
	break;
    }

    while (!repl.null()) {
	DataConsumer* c = ch->getConsumer();
	if (!c) {
	    Debug(DebugWarn,"Wave replacement '%s' attach request with no consumer!",repl.c_str());
	    ret = false;
	    break;
	}
	WaveSource* s = WaveSource::create(repl,0,false,msg.getBoolValue("autorepeat"));
	s->setNotify(msg.getValue("notify"));
	if (DataTranslator::attachChain(s,c,false))
	    msg.clearParam("replace");
	else {
	    Debug(DebugWarn,"Failed to replacement attach wave '%s' to consumer %p",repl.c_str(),c);
	    s->deref();
	    ret = false;
	}
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
	Regexp r("^wave/\\([^/]*\\)/\\(.*\\)$");
	if (c1.matches(r)) {
	    if (c1.matchString(1) == "record") {
		c1 = c1.matchString(2);
		more--;
	    }
	    else {
		Debug(DebugWarn,"Could not attach call recorder with method '%s', use 'record'",
		    c1.matchString(1).c_str());
		c1 = "";
	    }
	}
	else
	    c1 = "";
    }

    String c2(msg.getValue("peer"));
    if (c2.null())
	more--;
    else {
	Regexp r("^wave/\\([^/]*\\)/\\(.*\\)$");
	if (c2.matches(r)) {
	    if (c2.matchString(1) == "record") {
		c2 = c2.matchString(2);
		more--;
	    }
	    else {
		Debug(DebugWarn,"Could not attach peer recorder with method '%s', use 'record'",
		    c2.matchString(1).c_str());
		c2 = "";
	    }
	}
	else
	    c2 = "";
    }

    if (c1.null() && c2.null())
	return false;

    String ml(msg.getValue("maxlen"));
    unsigned maxlen = ml.toInteger(0);

    CallEndpoint *ch = static_cast<CallEndpoint*>(msg.userObject("CallEndpoint"));
    DataEndpoint *de = static_cast<DataEndpoint*>(msg.userObject("DataEndpoint"));
    if (ch && !de)
	de = ch->setEndpoint();

    if (!de) {
	if (!c1.null())
	    Debug(DebugWarn,"Wave source '%s' call record with no data channel!",c1.c_str());
	if (!c2.null())
	    Debug(DebugWarn,"Wave source '%s' peer record with no data channel!",c2.c_str());
	return false;
    }

    if (!c1.null()) {
	WaveConsumer* c = new WaveConsumer(c1,ch,maxlen,msg.getValue("format"));
	c->setNotify(msg.getValue("notify"));
	de->setCallRecord(c);
	c->deref();
    }

    if (!c2.null()) {
	WaveConsumer* c = new WaveConsumer(c2,ch,maxlen,msg.getValue("format"));
	c->setNotify(msg.getValue("notify"));
	de->setPeerRecord(c);
	c->deref();
    }

    // Stop dispatching if we handled all requested
    return !more;
}


bool WaveFileDriver::msgExecute(Message& msg, String& dest)
{
    Regexp r("^\\([^/]*\\)/\\(.*\\)$");
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

    String ml(msg.getValue("maxlen"));
    unsigned maxlen = ml.toInteger(0);
    CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userData());
    if (ch) {
	Debug(this,DebugInfo,"%s wave file '%s'", (meth ? "Record to" : "Play from"),
	    dest.matchString(2).c_str());
	WaveChan *c = new WaveChan(dest.matchString(2),meth,maxlen,msg.getBoolValue("autorepeat"),msg.getValue("format"));
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
    m.addParam("module",name());
    String callto(msg.getValue("direct"));
    if (callto.null()) {
	const char *targ = msg.getValue("target");
	if (!targ) {
	    Debug(DebugWarn,"Wave outgoing call with no target!");
	    return false;
	}
	callto = msg.getValue("caller");
	if (callto.null())
	    callto << prefix() << dest;
	m.addParam("called",targ);
	m.addParam("caller",callto);
	if (!Engine::dispatch(m)) {
	    Debug(DebugWarn,"Wave outgoing call but no route!");
	    return false;
	}
	callto = m.retValue();
	m.retValue().clear();
    }
    m = "call.execute";
    m.addParam("callto",callto);
    WaveChan *c = new WaveChan(dest.matchString(2),meth,maxlen,msg.getBoolValue("autorepeat"),msg.getValue("format"));
    m.setParam("id",c->id());
    m.userData(c);
    if (Engine::dispatch(m)) {
	msg.setParam("id",c->id());
	c->deref();
	return true;
    }
    Debug(DebugWarn,"Wave outgoing call not accepted!");
    c->destruct();
    return false;
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
    s_asyncDelete = Engine::config().getBoolValue("hacks","asyncdelete",true);
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
