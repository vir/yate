/**
 * wavefile.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Wave file driver (record+playback)
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

#include <telengine.h>
#include <telephony.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>


using namespace TelEngine;

class WaveSource : public ThreadedSource
{
public:
    WaveSource(const String& file, DataEndpoint *chan, bool autoclose = true);
    ~WaveSource();
    virtual void run();
    virtual void cleanup();
    inline void setNotify(const String& id)
	{ m_id = id; }
private:
    void detectAuFormat();
    void detectWavFormat();
    DataEndpoint *m_chan;
    DataBlock m_data;
    int m_fd;
    bool m_swap;
    unsigned m_brate;
    unsigned m_total;
    unsigned long long m_time;
    String m_id;
    bool m_autoclose;
};

class WaveConsumer : public DataConsumer
{
public:
    WaveConsumer(const String& file, DataEndpoint *chan = 0, unsigned maxlen = 0);
    ~WaveConsumer();
    virtual void Consume(const DataBlock &data, unsigned long timeDelta);
    inline void setNotify(const String& id)
	{ m_id = id; }
private:
    DataEndpoint *m_chan;
    int m_fd;
    unsigned m_total;
    unsigned m_maxlen;
    unsigned long long m_time;
    String m_id;
};

class WaveChan : public DataEndpoint
{
public:
    WaveChan(const String& file, bool record, unsigned maxlen = 0);
    ~WaveChan();
    virtual void disconnected(bool final, const char *reason);
    inline const String &id() const
	{ return m_id; }
private:
    String m_id;
    static int s_nextid;
};

class ConsDisconnector : public Thread
{
public:
    ConsDisconnector(DataEndpoint *chan, const String& id)
	: m_chan(chan), m_id(id) { }
    virtual void run();
private:
    DataEndpoint *m_chan;
    String m_id;
};

class WaveHandler : public MessageHandler
{
public:
    WaveHandler() : MessageHandler("call.execute") { }
    virtual bool received(Message &msg);
};

class AttachHandler : public MessageHandler
{
public:
    AttachHandler() : MessageHandler("chan.attach") { }
    virtual bool received(Message &msg);
};

class WaveFilePlugin : public Plugin
{
public:
    WaveFilePlugin();
    virtual void initialize();
private:
    WaveHandler *m_handler;
};

WaveSource::WaveSource(const String& file, DataEndpoint *chan, bool autoclose)
    : m_chan(chan), m_fd(-1), m_swap(false), m_brate(16000),
      m_total(0), m_time(0), m_autoclose(autoclose)
{
    Debug(DebugAll,"WaveSource::WaveSource(\"%s\",%p) [%p]",file.c_str(),chan,this);
    if (file == "-") {
	start("WaveSource");
	return;
    }
    m_fd = ::open(file.safe(),O_RDONLY|O_NOCTTY);
    if (m_fd < 0) {
	Debug(DebugGoOn,"Opening '%s': error %d: %s",
	    file.c_str(), errno, ::strerror(errno));
	m_format = "";
	return;
    }
    if (file.endsWith(".gsm")) {
	m_format = "gsm";
	m_brate = 1650;
    }
    else if (file.endsWith(".alaw") || file.endsWith(".A")) {
	m_format = "alaw";
	m_brate = 8000;
    }
    else if (file.endsWith(".mulaw") || file.endsWith(".u")) {
	m_format = "mulaw";
	m_brate = 8000;
    }
    else if (file.endsWith(".au"))
	detectAuFormat();
    else if (file.endsWith(".wav"))
	detectWavFormat();
    else if (!file.endsWith(".slin"))
	Debug(DebugMild,"Unknown format for file '%s', assuming signed linear",file.c_str());
    start("WaveSource");
}

WaveSource::~WaveSource()
{
    Debug(DebugAll,"WaveSource::~WaveSource() [%p] total=%u stamp=%lu",this,m_total,timeStamp());
    if (m_time) {
        m_time = Time::now() - m_time;
	if (m_time) {
	    m_time = (m_total*1000000ULL + m_time/2) / m_time;
	    Debug(DebugInfo,"WaveSource rate=%llu b/s",m_time);
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
    if (samp != 8000)
	m_format = String(samp) + "/" + m_format;
    if (chan != 1)
	m_format = String(chan) + "*" + m_format;
}

void WaveSource::detectWavFormat()
{
    Debug(DebugMild,".wav not supported yet, assuming raw signed linear");
}

void WaveSource::run()
{
    m_data.assign(0,(m_brate*20)/1000);
    int r = 0;
    unsigned long long tpos = Time::now();
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
	long long dly = tpos - Time::now();
	if (dly > 0) {
	    DDebug("WaveSource",DebugAll,"Sleeping for %lld usec",dly);
	    ::usleep((unsigned long)dly);
	}
	Forward(m_data,m_data.length()*8000/m_brate);
	m_total += r;
	tpos += (r*1000000ULL/m_brate);
    } while (r > 0);
    Debug(DebugAll,"WaveSource [%p] end of data [%p] [%s] ",this,m_chan,m_id.c_str());
    if (m_chan && !m_id.null()) {
	Message *m = new Message("chan.notify");
	m->addParam("targetid",m_id);
	m->userData(m_chan);
	Engine::enqueue(m);
	m_chan->setSource();
    }
}

void WaveSource::cleanup()
{
    Debug(DebugAll,"WaveSource [%p] cleanup, total=%u",this,m_total);
    if (m_chan && m_autoclose)
	m_chan->disconnect("eof");
}

WaveConsumer::WaveConsumer(const String& file, DataEndpoint *chan, unsigned maxlen)
    : m_chan(chan), m_fd(-1), m_total(0), m_maxlen(maxlen), m_time(0)
{
    Debug(DebugAll,"WaveConsumer::WaveConsumer(\"%s\",%p,%u) [%p]",
	file.c_str(),chan,maxlen,this);
    if (file == "-")
	return;
    else if (file.endsWith(".gsm"))
	m_format = "gsm";
    else if (file.endsWith(".alaw") || file.endsWith(".A"))
	m_format = "alaw";
    else if (file.endsWith(".mulaw") || file.endsWith(".u"))
	m_format = "mulaw";
    m_fd = ::creat(file.safe(),S_IRUSR|S_IWUSR);
    if (m_fd < 0)
	Debug(DebugGoOn,"Creating '%s': error %d: %s",
	    file.c_str(), errno, ::strerror(errno));
}

WaveConsumer::~WaveConsumer()
{
    Debug(DebugAll,"WaveConsumer::~WaveConsumer() [%p] total=%u stamp=%lu",this,m_total,timeStamp());
    if (m_time) {
        m_time = Time::now() - m_time;
	if (m_time) {
	    m_time = (m_total*1000000ULL + m_time/2) / m_time;
	    Debug(DebugInfo,"WaveConsumer rate=%llu b/s",m_time);
	}
    }
    if (m_fd >= 0) {
	::close(m_fd);
	m_fd = -1;
    }
}

void WaveConsumer::Consume(const DataBlock &data, unsigned long timeDelta)
{
    if (!data.null()) {
	if (!m_time)
	    m_time = Time::now();
	if (m_fd >= 0)
	    ::write(m_fd,data.data(),data.length());
	m_total += data.length();
	m_timestamp += timeDelta;
	if (m_maxlen && (m_total >= m_maxlen)) {
	    m_maxlen = 0;
	    if (m_fd >= 0) {
		::close(m_fd);
		m_fd = -1;
	    }
	    if (m_chan) {
		ConsDisconnector *disc = new ConsDisconnector(m_chan,m_id);
		m_chan = 0;
		if (disc->error()) {
		    Debug(DebugFail,"Error creating disconnector thread %p",disc);
		    delete disc;
		}
		else
		    disc->startup();
	    }
	}
    }
}

void ConsDisconnector::run()
{
    DDebug(DebugAll,"ConsDisconnector chan=%p id='%s'",m_chan,m_id.c_str());
    if (m_id) {
	m_chan->setConsumer();
	Message *m = new Message("chan.notify");
	m->addParam("targetid",m_id);
	m->userData(m_chan);
	Engine::enqueue(m);
    }
    else
	m_chan->disconnect();
}

Mutex mutex;
int WaveChan::s_nextid = 1;

WaveChan::WaveChan(const String& file, bool record, unsigned maxlen)
    : DataEndpoint("wavefile")
{
    Debug(DebugAll,"WaveChan::WaveChan(%s) [%p]",(record ? "record" : "play"),this);
    mutex.lock();
    m_id << "wave/" << s_nextid++;
    mutex.unlock();
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
    Debug(DebugAll,"WaveChan::~WaveChan() %s [%p]",m_id.c_str(),this);
}

void WaveChan::disconnected(bool final, const char *reason)
{
    Debugger debug("WaveChan::disconnected()"," '%s' [%p]",reason,this);
}

bool WaveHandler::received(Message &msg)
{
    String dest(msg.getValue("callto"));
    if (dest.null())
	return false;
    Regexp r("^wave/\\([^/]*\\)/\\(.*\\)$");
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
    DataEndpoint *dd = static_cast<DataEndpoint *>(msg.userData());
    if (dd) {
	Debug(DebugInfo,"%s wave file '%s'", (meth ? "Record to" : "Play from"),
	    dest.matchString(2).c_str());
	WaveChan *c = new WaveChan(dest.matchString(2),meth,maxlen);
	if (dd->connect(c)) {
	    c->deref();
	    return true;
	}
	else {
	    c->destruct();
	    return false;
	}
    }

    const char *targ = msg.getValue("target");
    if (!targ) {
	Debug(DebugWarn,"Wave outgoing call with no target!");
	return false;
    }
    Message m("call.route");
    m.addParam("id",dest);
    m.addParam("caller",dest);
    m.addParam("called",targ);
    if (Engine::dispatch(m)) {
	m = "call.execute";
	m.addParam("callto",m.retValue());
	m.retValue() = 0;
	WaveChan *c = new WaveChan(dest.matchString(2),meth,maxlen);
	m.setParam("id",c->id());
	m.userData(c);
	if (Engine::dispatch(m)) {
	    c->deref();
	    return true;
	}
	Debug(DebugWarn,"Wave outgoing call not accepted!");
	c->destruct();
    }
    else
	Debug(DebugWarn,"Wave outgoing call but no route!");
    return false;
}

bool AttachHandler::received(Message &msg)
{
    int more = 2;
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
    if (src.null() && cons.null())
	return false;

    String ml(msg.getValue("maxlen"));
    unsigned maxlen = ml.toInteger(0);
    DataEndpoint *dd = static_cast<DataEndpoint *>(msg.userData());
    if (!dd) {
	if (!src.null())
	    Debug(DebugWarn,"Wave source '%s' attach request with no data channel!",src.c_str());
	if (!cons.null())
	    Debug(DebugWarn,"Wave consumer '%s' attach request with no data channel!",cons.c_str());
	return false;
    }

    if (!src.null()) {
	WaveSource* s = new WaveSource(src,dd,false);
	s->setNotify(msg.getValue("notify"));
	dd->setSource(s);
	s->deref();
    }

    if (!cons.null()) {
	WaveConsumer* c = new WaveConsumer(cons,dd,maxlen);
	c->setNotify(msg.getValue("notify"));
	dd->setConsumer(c);
	c->deref();
    }

    // Stop dispatching if we handled all requested
    return !more;
}

WaveFilePlugin::WaveFilePlugin()
    : m_handler(0)
{
    Output("Loaded module WaveFile");
}

void WaveFilePlugin::initialize()
{
    Output("Initializing module WaveFile");
    if (!m_handler) {
	m_handler = new WaveHandler;
	Engine::install(m_handler);
	Engine::install(new AttachHandler);
    }
}

INIT_PLUGIN(WaveFilePlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
