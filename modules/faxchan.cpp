/**
 * faxchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * This module is based on SpanDSP (a series of DSP components for telephony),
 * written by Steve Underwood <steveu@coppice.org>.
 *
 * This great software can be found at http://soft-switch.org/
 *
 * Fax driver (transmission+receiving)
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

// For SpanDSP we have to ask for various C99 stuff
#define __STDC_LIMIT_MACROS

#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <spandsp.h>

#include <yatephone.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifdef SPANDSP_PRE006
#define fax_get_t30_state(x) (&(x)->t30_state)
#define t38_get_t38_state(x) (&(x)->t38)
#define t38_get_t30_state(x) (&(x)->t30_state)
#else
#define t38_get_t38_state(x) (&(x)->t38_fe.t38)
#define t38_get_t30_state(x) (&(x)->t30)
#endif

using namespace TelEngine;

namespace { // anonymous

#define FAX_DATA_CHUNK 320
#define T38_DATA_CHUNK 160
#define T38_TIMER_MSEC 20
#define CALL_END_DELAY 300

class FaxWrapper;

// A thread to run the fax data
class FaxThread : public Thread
{
public:
    inline FaxThread(FaxWrapper* wrapper)
	: Thread("Fax Wrapper"), m_wrap(wrapper)
	{ }
    virtual void run();
private:
    RefPointer<FaxWrapper> m_wrap;
};

class FaxSource : public DataSource
{
public:
    FaxSource(FaxWrapper* wrapper, const char* format);
    ~FaxSource();
private:
    RefPointer<FaxWrapper> m_wrap;
};

class FaxConsumer : public DataConsumer
{
public:
    FaxConsumer(FaxWrapper* wrapper, const char* format);
    ~FaxConsumer();
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags);
private:
    RefPointer<FaxWrapper> m_wrap;
};

// This class encapsulates an abstract T.30 fax interface
class FaxWrapper : public RefObject, public Mutex, public DebugEnabler
{
    friend class FaxSource;
    friend class FaxConsumer;
public:
    void debugName(const char* name);
    void setECM(bool enable);
    bool startup(CallEndpoint* chan = 0);
    virtual void cleanup();
    virtual void run() = 0;
    virtual void rxData(const DataBlock& data, unsigned long tStamp) = 0;
    void phaseB(int result);
    void phaseD(int result);
    void phaseE(int result);
    void endDocument(int result);
    inline t30_state_t* t30() const
	{ return m_t30; }
    inline bool eof() const
	{ return m_eof; }
    inline bool haveEndpoint() const
	{ return m_source || m_consumer; }
    inline void reset(bool source)
	{
	    if (source)
		m_source = 0;
	    else
		m_consumer = 0;
	    if (!haveEndpoint())
		m_chan = 0;
	    check();
	}
protected:
    FaxWrapper();
    void init(t30_state_t* t30, const char* ident, const char* file, bool sender);
    bool newPage();
    String m_name;
    String m_error;
    t30_state_t* m_t30;
    FaxSource* m_source;
    FaxConsumer* m_consumer;
    CallEndpoint* m_chan;
    bool m_eof;
    bool m_new;
    bool m_lastPageSent;
};

// An audio fax terminal, sends or receives a local file
class FaxTerminal : public FaxWrapper
{
public:
    FaxTerminal(const char *file, const char *ident, bool sender, bool iscaller, const Message& msg);
    virtual ~FaxTerminal();
    virtual void run();
    virtual void rxData(const DataBlock& data, unsigned long tStamp);
private:
    void rxBlock(void *buff, int len);
    int txBlock();
    fax_state_t m_fax;
    int m_lastr;
};

// A digital fax terminal
class T38Terminal : public FaxWrapper
{
public:
    T38Terminal(const char *file, const char *ident, bool sender, bool iscaller, const Message& msg, int version);
    virtual ~T38Terminal();
    virtual void run();
    virtual void rxData(const DataBlock& data, unsigned long tStamp);
private:
    int txData(const void* buf, int len, int seq, int count);
    static int txHandler(t38_core_state_t* t38s, void* userData,
	const uint8_t* buf, int len, int count);
    t38_terminal_state_t m_t38;
};

// A gateway between analogic and digital fax
class T38Gatway : public FaxWrapper
{
private:
    t38_gateway_state_t m_t38;
};

// A channel (terminal) that sends or receives a local TIFF file
class FaxChan : public Channel
{
    friend class FaxWrapper;
    YCLASS(FaxChan,Channel)
public:
    enum Type {
	Unknown,
	Detect,
	Switch,
	Analog,
	Digital,
    };
    FaxChan(bool outgoing, const char *file, bool sender, Message& msg);
    virtual ~FaxChan();
    virtual void destroyed();
    virtual void complete(Message& msg, bool minimal = false) const;
    virtual bool msgAnswered(Message& msg);
    virtual bool msgUpdate(Message& msg);
    void answer(Message& msg, const char* targetid);
    inline const String& localId() const
	{ return m_localId; }
    inline const String& remoteId() const
	{ return m_remoteId; }
    inline bool isSender() const
	{ return m_sender; }
    inline bool isCaller() const
	{ return m_caller; }
    void setParams(Message& msg, Type type, int t38version = -1);
    Type guessType(const Message& msg);
    static int guessT38(const Message& msg, int version = 0);
private:
    bool startup(FaxWrapper* wrap, const char* type = "audio", const char* format = "slin");
    bool startup(Message& msg);
    void updateInfo(t30_state_t* t30, const char* reason = 0);
    String m_localId;
    String m_remoteId;
    String m_reason;
    Type m_type;
    int m_t38version;
    bool m_sender;
    bool m_caller;
    bool m_ecm;
    int m_pages;
};

// Driver and plugin
class FaxDriver : public Driver
{
public:
    FaxDriver();
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest);
    virtual bool setDebug(Message& msg, const String& target);
private:
    bool m_first;
};

static FaxDriver plugin;
static bool s_debug = false;
static const TokenDict s_types[] = {
    { "autodetect",  FaxChan::Detect },
    { "detect",      FaxChan::Detect },
    { "autoswitch",  FaxChan::Switch },
    { "switch",      FaxChan::Switch },
    { "analog",      FaxChan::Analog },
    { "digital",     FaxChan::Digital },
    { 0, 0 }
};

class FaxHandler : public MessageHandler
{
public:
    FaxHandler(const char *name)
	: MessageHandler(name,100,plugin.name())
	{ }
    virtual bool received(Message &msg);
};

FaxSource::FaxSource(FaxWrapper* wrapper, const char* format)
    : DataSource(format), m_wrap(wrapper)
{
    DDebug(m_wrap,DebugAll,"FaxSource::FaxSource(%p,'%s') [%p]",wrapper,format,this);
    if (m_wrap)
	m_wrap->m_source = this;
}

FaxSource::~FaxSource()
{
    DDebug(m_wrap,DebugAll,"FaxSource::~FaxSource() [%p]",this);
    if (m_wrap && (m_wrap->m_source == this))
	m_wrap->reset(true);
    m_wrap = 0;
}


FaxConsumer::FaxConsumer(FaxWrapper* wrapper, const char* format)
    : DataConsumer(format), m_wrap(wrapper)
{
    DDebug(m_wrap,DebugAll,"FaxConsumer::FaxConsumer(%p,'%s') [%p]",wrapper,format,this);
    if (m_wrap)
	m_wrap->m_consumer = this;
}

FaxConsumer::~FaxConsumer()
{
    DDebug(m_wrap,DebugAll,"FaxConsumer::~FaxConsumer() [%p]",this);
    if (m_wrap && (m_wrap->m_consumer == this))
	m_wrap->reset(false);
    m_wrap = 0;
}

unsigned long FaxConsumer::Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
{
    if (data.null() || !m_wrap)
	return 0;
    m_wrap->rxData(data,tStamp);
    return invalidStamp();
}


static int phase_b_handler(t30_state_t* s, void* user_data, int result)
{
    if (user_data)
	static_cast<FaxWrapper*>(user_data)->phaseB(result);
    return T30_ERR_OK;
}

static int phase_d_handler(t30_state_t* s, void* user_data, int result)
{
    if (user_data)
	static_cast<FaxWrapper*>(user_data)->phaseD(result);
    return T30_ERR_OK;
}

static void phase_e_handler(t30_state_t* s, void* user_data, int result)
{
    if (user_data)
	static_cast<FaxWrapper*>(user_data)->phaseE(result);
}

static int document_handler(t30_state_t* s, void* user_data, int result)
{
    if (user_data)
	static_cast<FaxWrapper*>(user_data)->endDocument(result);
    return 0;
}

FaxWrapper::FaxWrapper()
    : Mutex(true,"FaxWrapper"),
      m_t30(0), m_source(0), m_consumer(0), m_chan(0),
      m_eof(false), m_new(false), m_lastPageSent(false)
{
    debugChain(&plugin);
    debugName(plugin.debugName());
}

// Set the debugging name, forward it to spandsp
void FaxWrapper::debugName(const char* name)
{
    if (name) {
	m_name = name;
	DebugEnabler::debugName(m_name);
    }
    if (m_t30) {
	int level = SPAN_LOG_SHOW_PROTOCOL|SPAN_LOG_SHOW_TAG|SPAN_LOG_SHOW_SEVERITY;
	// this is ugly - but spandsp's logging isn't fine enough
	if (s_debug && debugAt(DebugAll))
	    level |= SPAN_LOG_DEBUG;
	else if (s_debug && debugAt(DebugInfo))
	    level |= SPAN_LOG_FLOW;
	else if (debugAt(DebugNote))
	    level |= SPAN_LOG_PROTOCOL_WARNING;
	else if (debugAt(DebugMild))
	    level |= SPAN_LOG_PROTOCOL_ERROR;
	else if (debugAt(DebugWarn))
	    level |= SPAN_LOG_WARNING;
	else if (debugAt(DebugGoOn))
	    level |= SPAN_LOG_ERROR;
	span_log_set_tag(&m_t30->logging,m_name);
	span_log_set_level(&m_t30->logging,level);
    }
}

// Initialize terminal T.30 state
void FaxWrapper::init(t30_state_t* t30, const char* ident, const char* file, bool sender)
{
    t30_set_tx_ident(t30,c_safe(ident));
    t30_set_phase_e_handler(t30,phase_e_handler,this);
    t30_set_phase_d_handler(t30,phase_d_handler,this);
    t30_set_phase_b_handler(t30,phase_b_handler,this);
    t30_set_document_handler(t30,document_handler,(void*)this);
    m_t30 = t30;
    if (!file)
	return;
    if (sender)
	t30_set_tx_file(t30,file,-1,-1);
    else
	t30_set_rx_file(t30,file,-1);
}

// Set the ECM capability in T.30 state
void FaxWrapper::setECM(bool enable)
{
    if (!m_t30)
	return;
    t30_set_ecm_capability(m_t30,enable);
    if (enable)
	t30_set_supported_compressions(m_t30,T30_SUPPORT_T4_1D_COMPRESSION |
	    T30_SUPPORT_T4_2D_COMPRESSION | T30_SUPPORT_T6_COMPRESSION);
}

// Start the terminal's running thread
bool FaxWrapper::startup(CallEndpoint* chan)
{
    FaxThread* t = new FaxThread(this);
    if (t->startup()) {
	m_chan = chan;
	return true;
    }
    delete t;
    return false;
}

// Disconnect the channel if we can assume it's still there
void FaxWrapper::cleanup()
{
    if (m_chan && haveEndpoint())
	m_chan->disconnect(m_error);
}

// Atomically check if the page has changed
bool FaxWrapper::newPage()
{
    lock();
    bool changed = m_new;
    m_new = false;
    unlock();
    return changed;
}

// Called on intermediate states
void FaxWrapper::phaseB(int result)
{
    Debug(this,DebugInfo,"Phase B message 0x%X: %s [%p]",
	result,t30_frametype(result),this);
}

// Called after transferring a page
void FaxWrapper::phaseD(int result)
{
    const char* err = t30_frametype(result);
    Debug(this,DebugInfo,"Phase D message 0x%X: %s [%p]",
	result,err,this);
    lock();
    m_error = err;
    m_new = true;
    if (m_lastPageSent)
	m_error = "eof";
    unlock();
    FaxChan* chan = YOBJECT(FaxChan,m_chan);
    if (chan)
	chan->updateInfo(t30(),m_error);
}

// Called to report end of transfer
void FaxWrapper::phaseE(int result)
{
    const char* err = t30_completion_code_to_str(result);
    Debug(this,DebugInfo,"Phase E state 0x%X: %s [%p]",
	result,err,this);
    m_error = (T30_ERR_OK == result) ? "eof" : err;
    m_eof = true;
    FaxChan* chan = YOBJECT(FaxChan,m_chan);
    if (chan)
	chan->updateInfo(t30(),m_error);
}

void FaxWrapper::endDocument(int result)
{
    Debug(this,DebugInfo,"End document result 0x%X [%p]",result,this);
    m_lastPageSent = true;
}

// Constructor for the analog fax terminal
FaxTerminal::FaxTerminal(const char *file, const char *ident, bool sender, bool iscaller, const Message& msg)
    : m_lastr(0)
{
    Debug(this,DebugAll,"FaxTerminal::FaxTerminal(%s %s '%s','%s',%p) [%p]",
	(iscaller ? "caller" : "called"),
	(sender ? "transmit" : "receive"),
	file,ident,&msg,this);
    fax_init(&m_fax,iscaller);
    init(fax_get_t30_state(&m_fax),ident,file,sender);
    fax_set_transmit_on_idle(&m_fax,1);
}

FaxTerminal::~FaxTerminal()
{
    Debug(this,DebugAll,"FaxTerminal::~FaxTerminal() [%p]",this);
    fax_release(&m_fax);
}

// Run the terminal - send data blocks and sleep accordingly
void FaxTerminal::run()
{
    u_int64_t tpos = Time::now();
    int waitSentEnd = 10; // Run few cicles more to make sure that all data is sent
    while (haveEndpoint() && waitSentEnd > 0) {
	int r = txBlock();
	if (r < 0)
	    break;

	tpos += ((u_int64_t)1000000*r/16000);
	int64_t dly = tpos - Time::now();
	if (dly > 30000)
	    dly = 30000;
	if (dly > 0)
	    Thread::usleep(dly,true);
	if (m_eof)
	    waitSentEnd--;
    }
    FaxChan* chan = YOBJECT(FaxChan,m_chan);
    if (!chan || chan->isCaller())
	return;
    // Sleep a little bit to delay the call end message. In this way we give to 
    // the remote endpoint the chance to process all the send data.
    u_int64_t msec = Time::msecNow() + CALL_END_DELAY;
    while (haveEndpoint() && msec > Time::msecNow() && !Engine::exiting())
	Thread::idle();
}

// Build and send encoded audio data blocks
int FaxTerminal::txBlock()
{
    Lock lock(this);
    if (m_lastr < 0)
	return m_lastr;

    DataBlock data(0,FAX_DATA_CHUNK);
    int r = 2*fax_tx(&m_fax, (int16_t *) data.data(),data.length()/2);
    if (r != FAX_DATA_CHUNK && r != m_lastr)
	Debug(this,r ? DebugNote : DebugAll,"Generated %d bytes [%p]",r,this);
    m_lastr = r;
    lock.drop();
    if (m_source)
	m_source->Forward(data,DataNode::invalidStamp(),(newPage() ? DataNode::DataMark : 0));
    return data.length();
}

// Deliver small chunks of audio data to the decoder
void FaxTerminal::rxBlock(void *buff, int len)
{
    Lock lock(this);
    fax_rx(&m_fax, (int16_t *)buff,len/2);
}

// Break received audio data into manageable chunks, forward them to decoder
void FaxTerminal::rxData(const DataBlock& data, unsigned long tStamp)
{
    unsigned int pos = 0;
    while (pos < data.length())
    {
	// feed the decoder with small chunks of data (16 bytes/ms)
	int len = data.length() - pos;
	if (len > FAX_DATA_CHUNK)
	    len = FAX_DATA_CHUNK;
	rxBlock(((char *)data.data())+pos, len);
	pos += len;
    }
}


// Constructor for the digital fax terminal
T38Terminal::T38Terminal(const char *file, const char *ident, bool sender, bool iscaller, const Message& msg, int version)
{
    Debug(this,DebugAll,"T38Terminal::T38Terminal(%s %s '%s','%s',%p,%d) [%p]",
	(iscaller ? "caller" : "called"),
	(sender ? "transmit" : "receive"),
	file,ident,&msg,version,this);
    t38_terminal_init(&m_t38,iscaller,txHandler,this);
    t38_set_t38_version(t38_get_t38_state(&m_t38),version);
    bool tmp = msg.getBoolValue("t38fillbitremoval",0 != msg.getParam("sdp_image_T38FaxFillBitRemoval"));
    t38_set_fill_bit_removal(t38_get_t38_state(&m_t38),tmp ? 1 : 0);
    tmp = msg.getBoolValue("t38mmr",0 != msg.getParam("sdp_image_T38FaxTranscodingMMR"));
    t38_set_mmr_transcoding(t38_get_t38_state(&m_t38),tmp ? 1 : 0);
    tmp = msg.getBoolValue("t38jbig",0 != msg.getParam("sdp_image_T38FaxTranscodingJBIG"));
    t38_set_jbig_transcoding(t38_get_t38_state(&m_t38),tmp ? 1 : 0);
    init(t38_get_t30_state(&m_t38),ident,file,sender);
}

T38Terminal::~T38Terminal()
{
    Debug(this,DebugAll,"T38Terminal::~T38Terminal() [%p]",this);
    t38_terminal_release(&m_t38);
}

// Run the terminal
void T38Terminal::run()
{
    int waitSentEnd = 10; //  Run few cicles more to make sure that all data is sent
    while (haveEndpoint() && waitSentEnd > 0) {
	// the fake number of samples is just to compute timeouts
	if (t38_terminal_send_timeout(&m_t38,T38_DATA_CHUNK))
	    break;
	Thread::msleep(T38_TIMER_MSEC);
	if (m_eof)
	    waitSentEnd--;
    }
    FaxChan* chan = YOBJECT(FaxChan,m_chan);
    if (!chan || chan->isCaller())
	return;
    // Sleep a little bit to delay the call end message. In this way we give to
    // the remote endpoint the chance to process all the send data.
    u_int64_t msec = Time::msecNow() + CALL_END_DELAY;
    while (haveEndpoint() && msec > Time::msecNow() && !Engine::exiting())
	Thread::idle();
}

// Static callback that sends out T.38 data
int T38Terminal::txHandler(t38_core_state_t* t38s, void* userData,
    const uint8_t* buf, int len, int count)
{
    if (!(t38s && userData))
	return 1;
    return static_cast<T38Terminal*>(userData)->txData(buf,len,t38s->tx_seq_no,count);
}

// Handle received digital data
void T38Terminal::rxData(const DataBlock& data, unsigned long tStamp)
{
    t38_core_rx_ifp_packet(t38_get_t38_state(&m_t38),(uint8_t*)data.data(),data.length(),tStamp & 0xffff);
}

int T38Terminal::txData(const void* buf, int len, int seq, int count)
{
    if (!m_source)
	return 0;
    XDebug(this,DebugInfo,"T38Terminal::txData(%p,%d,%d,%d)",buf,len,seq,count);
    DataBlock data((void*)buf,len,false);
    m_source->Forward(data,seq,(newPage() ? DataNode::DataMark : 0));
    data.clear(false);
    return 0;
}


// Helper thread
void FaxThread::run()
{
    m_wrap->run();
    m_wrap->cleanup();
}


// Constructor for a generic fax terminal channel
FaxChan::FaxChan(bool outgoing, const char *file, bool sender, Message& msg)
    : Channel(plugin,0,outgoing),
      m_type(Unknown), m_t38version(0), m_sender(sender), m_pages(0)
{
    Debug(this,DebugAll,"FaxChan::FaxChan(%s \"%s\") [%p]",
	(sender ? "transmit" : "receive"),
	file,this);
    m_localId = msg.getValue("faxident",msg.getValue(outgoing ? "called" : "caller"));
    // outgoing means from Yate to file so the fax should answer by default
    m_caller = msg.getBoolValue("faxcaller",!outgoing);
    m_ecm = msg.getBoolValue("faxecm",true);
    m_address = file;
    Message* s = message("chan.startup",msg);
    if (outgoing)
	s->copyParams(msg,"caller,callername,called,billid,callto,username");
    Engine::enqueue(s);
}

// Destructor - clears all (audio, image) endpoints early
FaxChan::~FaxChan()
{
    Debug(DebugAll,"FaxChan::~FaxChan() [%p]",this);
}

// Destruction notification - virtual methods still valid
void FaxChan::destroyed()
{
    Engine::enqueue(message("chan.hangup"));
    Channel::destroyed();
}

// Fill in message parameters
void FaxChan::complete(Message& msg, bool minimal) const
{
    Channel::complete(msg,minimal);
    if (minimal)
	return;
    msg.addParam("reason",m_reason,false);
    msg.addParam("faxident_local",m_localId,false);
    msg.addParam("faxident_remote",m_remoteId,false);
    if (m_pages)
	msg.addParam("faxpages",String(m_pages));
    msg.addParam("faxtype",lookup(m_type,s_types),false);
    msg.addParam("faxecm",String::boolText(m_ecm));
    msg.addParam("faxcaller",String::boolText(m_caller));
}

// Build data channels, attaches a wrapper and starts it up
bool FaxChan::startup(FaxWrapper* wrap, const char* type, const char* format)
{
    wrap->debugName(debugName());
    FaxSource* fs = new FaxSource(wrap,format);
    setSource(fs,type);
    fs->deref();
    FaxConsumer* fc = new FaxConsumer(wrap,format);
    setConsumer(fc,type);
    fc->deref();
    wrap->setECM(m_ecm);
    bool ok = wrap->startup(this);
    wrap->deref();
    Debug(this,DebugInfo,"Fax startup %s in %s mode [%p]",
	(ok ? "succeeded" : "failed"),lookup(m_type,s_types,"unknown"),this);
    return ok;
}

// Attach and start an analog or digital wrapper
bool FaxChan::startup(Message& msg)
{
    Type t = guessType(msg);
    switch (t) {
	case Detect:
	case Switch:
	    m_t38version = guessT38(msg,m_t38version);
	    // fall through
	case Analog:
	    if (t == m_type)
		return true;
	    clearEndpoint();
	    m_type = t;
	    return startup(new FaxTerminal(address(),m_localId,m_sender,m_caller,msg));
	case Digital:
	    if (t == m_type)
		return true;
	    clearEndpoint();
	    m_type = t;
	    m_t38version = guessT38(msg,m_t38version);
	    return startup(new T38Terminal(address(),m_localId,m_sender,m_caller,msg,m_t38version),"image","t38");
	default:
	    return false;
    }
}

// Handler for an originator fax start request
bool FaxChan::msgAnswered(Message& msg)
{
    if (Channel::msgAnswered(msg)) {
	bool chg = (Switch == m_type);
	startup(msg);
	if (chg && (Analog == m_type)) {
	    Message* m = message("call.update");
	    m->addParam("operation","notify");
	    m->addParam("audio_changed",String::boolText(true));
	    setParams(*m,Digital,m_t38version);
	    Engine::enqueue(m);
	}
	return true;
    }
    return false;
}

// Handler for update notifications, the fax type may have changed
bool FaxChan::msgUpdate(Message& msg)
{
    const String* oper = msg.getParam("operation");
    if (oper && (*oper == "notify")) {
	Channel::msgUpdate(msg);
	return startup(msg);
    }
    return Channel::msgUpdate(msg);
}

// Handler for an answerer fax start request
void FaxChan::answer(Message& msg, const char* targetid)
{
    if (targetid)
	m_targetid = targetid;
    status("answered");
    startup(msg);
    Message* m = message("call.answered");
    setParams(*m,m_type,m_t38version);
    Engine::enqueue(m);
}

// Guess fax type from message parameters
FaxChan::Type FaxChan::guessType(const Message& msg)
{
    Type t = (Type)msg.getIntValue("faxtype",s_types,Unknown);
    if (Unknown == t) {
	// guess fax type from media offer
	const String* imgFmt = msg.getParam("formats_image");
	if (imgFmt && msg.getBoolValue("media_image") && (*imgFmt == "t38"))
	    t = msg.getBoolValue("media") ? Detect : Digital;
	else if (msg.getBoolValue("media",true))
	    t = Analog;
	Debug(this,DebugAll,"Guessed fax type: %s [%p]",lookup(t,s_types,"unknown"),this);
    }
    return t;
}

int FaxChan::guessT38(const Message& msg, int version)
{
    version = msg.getIntValue("sdp_image_T38FaxVersion",version);
    return msg.getIntValue("t38version",version);
}

// Set media parameters of message to best reflect fax type
void FaxChan::setParams(Message& msg, Type type, int t38version)
{
    bool audio = (Digital != type);
    msg.setParam("media",String::boolText(audio));
    if (audio && !msg.getValue("formats"))
	msg.setParam("formats","alaw,mulaw");
    switch (type) {
	case Digital:
	case Detect:
	    msg.setParam("media_image",String::boolText(true));
	    msg.setParam("formats_image","t38");
	    msg.setParam("transport_image","udptl");
	    if (t38version >= 0) {
		String ver(t38version);
		msg.setParam("t38version",ver);
		msg.setParam("osdp_image_T38FaxVersion",ver);
	    }
	    break;
	case Switch:
	    if (Unknown == m_type)
		m_type = type;
	    break;
	default:
	    break;
    }
}

void FaxChan::updateInfo(t30_state_t* t30, const char* reason)
{
    if (reason)
	m_reason = reason;
    const char* ident = t30_get_rx_ident(t30);
    if (!null(ident))
	m_remoteId = ident;

    t30_stats_t t;
    t30_get_transfer_statistics(t30, &t);
    if (!t.error_correcting_mode)
	m_ecm = false;
#ifdef SPANDSP_TXRXSTATS
    m_pages = t.pages_tx + t.pages_rx;
#else
    m_pages = t.pages_transferred;
#endif

    Debug(this,DebugAll,"bit rate %d", t.bit_rate);
    Debug(this,DebugAll,"error correction %d", t.error_correcting_mode);
    Debug(this,DebugAll,"pages transferred %d", m_pages);
    Debug(this,DebugAll,"image size %d x %d", t.width, t.length);
    Debug(this,DebugAll,"image resolution %d x %d", t.x_resolution, t.y_resolution);
    Debug(this,DebugAll,"bad rows %d", t.bad_rows);
    Debug(this,DebugAll,"longest bad row run %d", t.longest_bad_row_run);
    Debug(this,DebugAll,"compression type %d", t.encoding);
    Debug(this,DebugAll,"image size %d", t.image_size);

    Debug(this,DebugAll,"local ident '%s'", t30_get_tx_ident(t30));
    Debug(this,DebugAll,"remote ident '%s'", ident);
}


bool FaxDriver::msgExecute(Message& msg, String& dest)
{
    static const Regexp r("^\\([^/]*\\)/\\(.*\\)$");
    if (!dest.matches(r))
	return false;

    bool transmit = false;
    if ((dest.matchString(1) == "send") || (dest.matchString(1) == "transmit"))
	transmit = true;
    else if (dest.matchString(1) != "receive") {
	Debug(this,DebugWarn,"Invalid fax method '%s', use 'receive' or 'transmit'",
	    dest.matchString(1).c_str());
	return false;
    }
    dest = dest.matchString(2);
    if (transmit && !File::exists(dest)) {
	msg.setParam("error","noroute");
	msg.setParam("reason","File not found");
	return false;
    }

    RefPointer<FaxChan> fc;
    CallEndpoint* ce = YOBJECT(CallEndpoint,msg.userData());
    if (ce) {
	fc = new FaxChan(true,dest,transmit,msg);
	fc->initChan();
	fc->deref();
	if (fc->connect(ce,msg.getValue("reason"))) {
	    msg.setParam("peerid",fc->id());
	    msg.setParam("targetid",fc->id());
	    fc->answer(msg,msg.getValue("id",ce->id()));
	    return true;
	}
    }
    else {
	fc = new FaxChan(false,dest,transmit,msg);
	fc->initChan();
	fc->deref();
	Message m("call.route");
	fc->complete(m);
	fc->setParams(m,fc->guessType(msg),FaxChan::guessT38(msg));
	m.copyParams(msg,msg[YSTRING("copyparams")]);
	m.userData(fc);
	String callto = msg.getValue("caller");
	if (callto)
	    m.addParam("caller",callto);
	callto = msg.getValue("direct");
	if (callto.null()) {
	    const char* targ = msg.getValue("target");
	    if (!targ) {
		Debug(DebugWarn,"Outgoing fax call with no target!");
		return false;
	    }
	    m.addParam("called",targ);
	    if (!Engine::dispatch(m) || m.retValue().null()) {
		Debug(this,DebugWarn,"Outgoing fax call but no route!");
		return false;
	    }
	    callto = m.retValue();
	}
	m = "call.execute";
	m.addParam("callto",callto);
	m.retValue().clear();
	if (Engine::dispatch(m)) {
	    fc->callAccept(m);
	    return true;
	}
	Debug(this,DebugWarn,"Outgoing fax call not accepted!");
    }
    return false;
}

bool FaxDriver::setDebug(Message& msg, const String& target)
{
    if (target == "spandsp") {
	s_debug = msg.getBoolValue("line",s_debug);
	msg.retValue() << "Detailed spandsp debugging " << (s_debug ? "on" : "off") << "\r\n";
	return true;
    }
    return Driver::setDebug(msg,target);
}

FaxDriver::FaxDriver()
    : Driver("fax"), m_first(true)
{
    Output("Loaded module Fax");
}

void FaxDriver::initialize()
{
    Output("Initializing module Fax");
    setup(0,true);
    if (m_first) {
	m_first = false;
	installRelay(Answered);
	installRelay(Update,110);
	// TODO: add other handlers
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
