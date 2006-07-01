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

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

using namespace TelEngine;
namespace { // anonymous

class WaveSource : public ThreadedSource
{
public:
    WaveSource(const String& file, CallEndpoint* chan, bool autoclose = true);
    ~WaveSource();
    virtual void run();
    virtual void cleanup();
    void setNotify(const String& id);
private:
    void detectAuFormat();
    void detectWavFormat();
    void detectIlbcFormat();
    bool computeDataRate();
    bool notify();
    CallEndpoint* m_chan;
    DataBlock m_data;
    int m_fd;
    bool m_swap;
    unsigned m_brate;
    unsigned m_total;
    u_int64_t m_time;
    String m_id;
    bool m_autoclose;
    bool m_autoclean;
    bool m_nodata;
};

class WaveConsumer : public DataConsumer
{
public:
    WaveConsumer(const String& file, CallEndpoint* chan = 0, unsigned maxlen = 0);
    ~WaveConsumer();
    virtual void Consume(const DataBlock& data, unsigned long tStamp);
    inline void setNotify(const String& id)
	{ m_id = id; }
private:
    CallEndpoint* m_chan;
    int m_fd;
    unsigned m_total;
    unsigned m_maxlen;
    u_int64_t m_time;
    String m_id;
};

class WaveChan : public Channel
{
public:
    WaveChan(const String& file, bool record, unsigned maxlen = 0);
    ~WaveChan();
};

class Disconnector : public Thread
{
public:
    Disconnector(CallEndpoint* chan, const String& id, bool source, bool disc);
    virtual ~Disconnector();
    virtual void run();
    bool init();
private:
    RefPointer<CallEndpoint> m_chan;
    Message* m_msg;
    bool m_source;
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

INIT_PLUGIN(WaveFileDriver);

WaveSource::WaveSource(const String& file, CallEndpoint* chan, bool autoclose)
    : m_chan(chan), m_fd(-1), m_swap(false), m_brate(0),
      m_total(0), m_time(0), m_autoclose(autoclose), m_autoclean(false),
      m_nodata(false)
{
    Debug(&__plugin,DebugAll,"WaveSource::WaveSource(\"%s\",%p) [%p]",file.c_str(),chan,this);
    if (file == "-") {
	m_nodata = true;
	m_brate = 8000;
	start("WaveSource");
	return;
    }
    m_fd = ::open(file.safe(),O_RDONLY|O_NOCTTY|O_BINARY);
    if (m_fd < 0) {
	Debug(DebugWarn,"Opening '%s': error %d: %s",
	    file.c_str(), errno, ::strerror(errno));
	m_format.clear();
	notify();
	return;
    }
    if (file.endsWith(".gsm"))
	m_format = "gsm";
    else if (file.endsWith(".alaw") || file.endsWith(".A"))
	m_format = "alaw";
    else if (file.endsWith(".mulaw") || file.endsWith(".u"))
	m_format = "mulaw";
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
	Debug(DebugMild,"Unknown format for file '%s', assuming signed linear",file.c_str());
    if (computeDataRate())
	start("WaveSource");
    else {
	Debug(DebugWarn,"Unable to compute data rate for file '%s'",file.c_str());
	notify();
    }
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
    if (m_fd >= 0) {
	::close(m_fd);
	m_fd = -1;
    }
}

void WaveSource::detectAuFormat()
{
    struct {
	uint32_t sign;
	uint32_t offs;
	uint32_t len;
	uint32_t form;
	uint32_t freq;
	uint32_t chan;
    } header;
    if ((::read(m_fd,&header,sizeof(header)) != sizeof(header)) ||
	(ntohl(header.sign) != 0x2E736E64)) {
	Debug(DebugMild,"Invalid .au file header, assuming raw signed linear");
	::lseek(m_fd,0,SEEK_SET);
	return;
    }
    ::lseek(m_fd,ntohl(header.offs),SEEK_SET);
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
#if 0
    if (samp != 8000)
	m_format = String(samp) + "/" + m_format;
    if (chan != 1)
	m_format = String(chan) + "*" + m_format;
#endif
}

void WaveSource::detectWavFormat()
{
    Debug(DebugMild,".wav not supported yet, assuming raw signed linear");
}

#define ILBC_HEADER_LEN 9
void WaveSource::detectIlbcFormat()
{
    char header[ILBC_HEADER_LEN+1];
    if (::read(m_fd,&header,ILBC_HEADER_LEN) == ILBC_HEADER_LEN) {
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
	Thread::yield(true);
	if (!alive())
	    return;
    }
    m_data.assign(0,(m_brate*20)/1000);
    // start counting time from now
    u_int64_t tpos = Time::now();
    m_time = tpos;
    do {
	r = (m_fd >= 0) ? ::read(m_fd,m_data.data(),m_data.length()) : m_data.length();
	if (r < 0) {
	    if (errno == EINTR) {
		r = 1;
		continue;
	    }
	    break;
	}
	if (r < (int)m_data.length())
	    m_data.assign(m_data.data(),r);
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
	if (!alive())
	    break;
	Forward(m_data,ts);
	ts += m_data.length()*8000/m_brate;
	m_total += r;
	tpos += (r*(u_int64_t)1000000/m_brate);
    } while (r > 0);
    Debug(&__plugin,DebugAll,"WaveSource [%p] end of data [%p] [%s] ",this,m_chan,m_id.c_str());
    // at cleanup time deref the data source if we start no disconnector thread
    m_autoclean = !notify();
}

void WaveSource::cleanup()
{
    Debug(&__plugin,DebugAll,"WaveSource [%p] cleanup, total=%u",this,m_total);
    if (m_autoclean) {
	if (m_chan && (m_chan->getSource() == this))
	    m_chan->setSource();
	else
	    deref();
    }
}

void WaveSource::setNotify(const String& id)
{
    m_id = id;
    if ((m_fd < 0) && !m_nodata)
	notify();
}

bool WaveSource::notify()
{
    if (m_id || m_autoclose) {
	Disconnector *disc = new Disconnector(m_chan,m_id,true,m_autoclose);
	return disc->init();
    }
    return false;
}

WaveConsumer::WaveConsumer(const String& file, CallEndpoint* chan, unsigned maxlen)
    : m_chan(chan), m_fd(-1), m_total(0), m_maxlen(maxlen), m_time(0)
{
    Debug(&__plugin,DebugAll,"WaveConsumer::WaveConsumer(\"%s\",%p,%u) [%p]",
	file.c_str(),chan,maxlen,this);
    if (file == "-")
	return;
    else if (file.endsWith(".gsm"))
	m_format = "gsm";
    else if (file.endsWith(".alaw") || file.endsWith(".A"))
	m_format = "alaw";
    else if (file.endsWith(".mulaw") || file.endsWith(".u"))
	m_format = "mulaw";
    else if (file.endsWith(".ilbc20"))
	m_format = "ilbc20";
    else if (file.endsWith(".ilbc30"))
	m_format = "ilbc30";
    m_fd = ::open(file.safe(),O_WRONLY|O_CREAT|O_TRUNC|O_NOCTTY|O_BINARY,S_IRUSR|S_IWUSR);
    if (m_fd < 0)
	Debug(DebugWarn,"Creating '%s': error %d: %s",
	    file.c_str(), errno, ::strerror(errno));
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
    if (m_fd >= 0) {
	::close(m_fd);
	m_fd = -1;
    }
}

void WaveConsumer::Consume(const DataBlock& data, unsigned long tStamp)
{
    if (!data.null()) {
	if (!m_time)
	    m_time = Time::now();
	if (m_fd >= 0)
	    ::write(m_fd,data.data(),data.length());
	m_total += data.length();
	if (m_maxlen && (m_total >= m_maxlen)) {
	    m_maxlen = 0;
	    if (m_fd >= 0) {
		::close(m_fd);
		m_fd = -1;
	    }
	    if (m_chan) {
		Disconnector *disc = new Disconnector(m_chan,m_id,false,false);
		m_chan = 0;
		disc->init();
	    }
	}
    }
}

Disconnector::Disconnector(CallEndpoint* chan, const String& id, bool source, bool disc)
    : m_chan(chan), m_msg(0), m_source(source), m_disc(disc)
{
    if (id) {
	Message *m = new Message("chan.notify");
	m->addParam("targetid",id);
	m->userData(chan);
	m_msg = m;
    }
}

Disconnector::~Disconnector()
{
    if (m_msg)
	Engine::enqueue(m_msg);
}

bool Disconnector::init()
{
    if (error()) {
	Debug(DebugFail,"Error creating disconnector thread %p",this);
	delete this;
	return false;
    }
    return startup();
}

void Disconnector::run()
{
    DDebug(&__plugin,DebugAll,"Disconnector::run() chan=%p msg=%p",
	(void*)m_chan,m_msg);
    if (!m_chan)
	return;
    if (m_source) {
	m_chan->setSource();
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

WaveChan::WaveChan(const String& file, bool record, unsigned maxlen)
    : Channel(__plugin)
{
    Debug(this,DebugAll,"WaveChan::WaveChan(%s) [%p]",(record ? "record" : "play"),this);
    if (record) {
	setConsumer(new WaveConsumer(file,this,maxlen));
	getConsumer()->deref();
    }
    else {
	setSource(new WaveSource(file,this));
	getSource()->deref();
    }
}

WaveChan::~WaveChan()
{
    Debug(this,DebugAll,"WaveChan::~WaveChan() %s [%p]",id().c_str(),this);
}

bool AttachHandler::received(Message &msg)
{
    int more = 3;
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
		src = "";
	    }
	}
	else
	    src = "";
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
		cons = "";
	    }
	}
	else
	    cons = "";
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
		ovr = "";
	    }
	}
	else
	    ovr = "";
    }

    if (src.null() && cons.null() && ovr.null())
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
	WaveSource* s = new WaveSource(src,ch,false);
	ch->setSource(s);
	s->setNotify(msg.getValue("notify"));
	s->deref();
	msg.clearParam("source");
    }

    if (!cons.null()) {
	WaveConsumer* c = new WaveConsumer(cons,ch,maxlen);
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
	WaveSource* s = new WaveSource(ovr,0,false);
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
	WaveConsumer* c = new WaveConsumer(c1,ch,maxlen);
	c->setNotify(msg.getValue("notify"));
	de->setCallRecord(c);
	c->deref();
    }

    if (!c2.null()) {
	WaveConsumer* c = new WaveConsumer(c2,ch,maxlen);
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
	WaveChan *c = new WaveChan(dest.matchString(2),meth,maxlen);
	if (ch->connect(c,msg.getValue("reason"))) {
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
    WaveChan *c = new WaveChan(dest.matchString(2),meth,maxlen);
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
    if (!m_handler) {
	m_handler = new AttachHandler;
	Engine::install(m_handler);
	Engine::install(new RecordHandler);
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
