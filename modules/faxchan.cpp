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

// For SpanDSP we have to ask for various C99 stuff
#define __STDC_LIMIT_MACROS

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

using namespace TelEngine;

namespace { // anonymous

#define FAX_DATA_CHUNK 320
#define T38_DATA_CHUNK 160

class FaxWrapper;

// A thread to run the fax data
class FaxThread : public Thread
{
public:
    inline FaxThread(FaxWrapper* wrapper)
	: Thread("Fax"), m_wrap(wrapper)
	{ }
    virtual void run();
private:
    RefPointer<FaxWrapper> m_wrap;
};

class FaxSource : public DataSource
{
public:
    FaxSource(FaxWrapper* wrapper, const char* format = "slin");
    ~FaxSource();
private:
    RefPointer<FaxWrapper> m_wrap;
};

class FaxConsumer : public DataConsumer
{
public:
    FaxConsumer(FaxWrapper* wrapper, const char* format = "slin");
    ~FaxConsumer();
    virtual void Consume(const DataBlock& data, unsigned long tStamp);
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
    inline t30_state_t* t30() const
	{ return m_t30; }
    inline bool eof() const
	{ return m_eof; }
protected:
    FaxWrapper();
    void init(t30_state_t* t30, const char* ident, const char* file, bool sender);
    String m_name;
    t30_state_t* m_t30;
    FaxSource* m_source;
    FaxConsumer* m_consumer;
    CallEndpoint* m_chan;
    bool m_eof;
};

// An audio fax terminal, sends or receives a local file
class FaxTerminal : public FaxWrapper
{
public:
    FaxTerminal(const char *file, const char *ident, bool sender, bool iscaller);
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
    T38Terminal(const char *file, const char *ident, bool sender, bool iscaller);
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
public:
    FaxChan(bool outgoing, const char *file, bool sender, Message& msg);
    virtual ~FaxChan();
    virtual bool msgAnswered(Message& msg);
    void answer(const char* targetid);
    inline const String& ident() const
	{ return m_ident; }
    inline bool isSender() const
	{ return m_sender; }
    inline bool isCaller() const
	{ return m_caller; }
private:
    bool startup(FaxWrapper* wrap, const char* type = "audio");
    bool startup(bool digital = false);
    String m_ident;
    bool m_sender;
    bool m_caller;
    bool m_ecm;
};

class FaxHandler : public MessageHandler
{
public:
    FaxHandler(const char *name) : MessageHandler(name) { }
    virtual bool received(Message &msg);
};

// Driver and plugin
class FaxDriver : public Driver
{
public:
    FaxDriver();
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest);
private:
    bool m_first;
};

static FaxDriver plugin;

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
    if (m_wrap && (m_wrap->m_source == this)) {
	m_wrap->m_source = 0;
	m_wrap->check();
    }
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
    if (m_wrap && (m_wrap->m_consumer == this)) {
	m_wrap->m_consumer = 0;
	m_wrap->check();
    }
    m_wrap = 0;
}

void FaxConsumer::Consume(const DataBlock& data, unsigned long tStamp)
{
    if (data.null() || !m_wrap)
	return;
    m_wrap->rxData(data,tStamp);
}


static void phase_b_handler(t30_state_t* s, void* user_data, int result)
{
    if (user_data)
	static_cast<FaxWrapper*>(user_data)->phaseB(result);
}

static void phase_d_handler(t30_state_t* s, void* user_data, int result)
{
    if (user_data)
	static_cast<FaxWrapper*>(user_data)->phaseD(result);
}

static void phase_e_handler(t30_state_t* s, void* user_data, int result)
{
    if (user_data)
	static_cast<FaxWrapper*>(user_data)->phaseE(result);
}


FaxWrapper::FaxWrapper()
    : Mutex(true),
      m_t30(0), m_source(0), m_consumer(0), m_chan(0), m_eof(false)
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
	int level = SPAN_LOG_SHOW_PROTOCOL|SPAN_LOG_SHOW_TAG;
	// this is ugly - but spandsp's logging isn't fine enough
	if (false)
	    ;
#ifdef XDEBUG
	else if (debugAt(DebugAll))
	    level |= SPAN_LOG_DEBUG;
#endif
#ifdef DDEBUG
	else if (debugAt(DebugInfo))
	    level |= SPAN_LOG_FLOW;
#endif
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
    if (!ident)
	ident = "anonymous";
    t30_set_local_ident(t30,ident);
    t30_set_phase_e_handler(t30,phase_e_handler,this);
    t30_set_phase_d_handler(t30,phase_d_handler,this);
    t30_set_phase_b_handler(t30,phase_b_handler,this);
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
    if (m_chan && (m_source || m_consumer))
	m_chan->disconnect();
}

// Called on intermediate states
void FaxWrapper::phaseB(int result)
{
    Debug(this,DebugInfo,"Phase B code 0x%X [%p]",result,this);
}

// Called after transferring a page
void FaxWrapper::phaseD(int result)
{
    Debug(this,DebugInfo,"Phase D code 0x%X [%p]",result,this);

    t30_stats_t t;
    char ident[21];

    t30_get_transfer_statistics(t30(), &t);
    Debug(this,DebugAll,"bit rate %d", t.bit_rate);
    Debug(this,DebugAll,"pages transferred %d", t.pages_transferred);
    Debug(this,DebugAll,"image size %d x %d", t.width, t.length);
    Debug(this,DebugAll,"image resolution %d x %d", t.x_resolution, t.y_resolution);
    Debug(this,DebugAll,"bad rows %d", t.bad_rows);
    Debug(this,DebugAll,"longest bad row run %d", t.longest_bad_row_run);
    Debug(this,DebugAll,"compression type %d", t.encoding);
    Debug(this,DebugAll,"image size %d", t.image_size);

    t30_get_local_ident(t30(), ident);
    Debug(this,DebugAll,"local ident '%s'", ident);

    t30_get_far_ident(t30(), ident);
    Debug(this,DebugAll,"remote ident '%s'", ident);
}

// Called to report end of transfer
void FaxWrapper::phaseE(int result)
{
    Debug(this,DebugInfo,"Phase E code 0x%X [%p]",result,this);
    m_eof = true;
}


// Constructor for the analog fax terminal
FaxTerminal::FaxTerminal(const char *file, const char *ident, bool sender, bool iscaller)
    : m_lastr(0)
{
    Debug(this,DebugAll,"FaxTerminal::FaxTerminal(%s %s \"%s\") [%p]",
	(iscaller ? "caller" : "called"),
	(sender ? "transmit" : "receive"),
	file,this);
    fax_init(&m_fax,iscaller);
    init(&m_fax.t30_state,ident,file,sender);
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
    while ((m_source || m_consumer) && !m_eof) {
	int r = txBlock();
	if (r < 0)
	    break;

	tpos += ((u_int64_t)1000000*r/16000);
	int64_t dly = tpos - Time::now();
	if (dly > 10000)
	    dly = 10000;
	if (dly > 0)
	    Thread::usleep(dly,true);
    }
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
	m_source->Forward(data);
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
T38Terminal::T38Terminal(const char *file, const char *ident, bool sender, bool iscaller)
{
    Debug(this,DebugAll,"T38Terminal::T38Terminal(%s %s \"%s\") [%p]",
	(iscaller ? "caller" : "called"),
	(sender ? "transmit" : "receive"),
	file,this);
    t38_terminal_init(&m_t38,iscaller,txHandler,this);
    t38_set_t38_version(&m_t38.t38,1);
    init(&m_t38.t30_state,ident,file,sender);
}

T38Terminal::~T38Terminal()
{
    Debug(this,DebugAll,"T38Terminal::~T38Terminal() [%p]",this);
}

// Run the terminal
void T38Terminal::run()
{
    Debug(this,DebugStub,"Please implement T38Terminal::run()");
    t38_terminal_send_timeout(&m_t38,T38_DATA_CHUNK);
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
    Debug(this,DebugStub,"Please implement T38Terminal::rxData()");
    t38_core_rx_ifp_packet(&m_t38.t38,tStamp,(uint8_t*)data.data(),data.length());
}

int T38Terminal::txData(const void* buf, int len, int seq, int count)
{
    Debug(this,DebugStub,"Please implement T38Terminal::txData()");
}


// Helper thread
void FaxThread::run()
{
    m_wrap->run();
    m_wrap->cleanup();
}


// Constructor for a generic fax terminal channel
FaxChan::FaxChan(bool outgoing, const char *file, bool sender, Message& msg)
    : Channel(plugin,0,outgoing), m_sender(sender)
{
    Debug(this,DebugAll,"FaxChan::FaxChan(%s \"%s\") [%p]",
	(sender ? "transmit" : "receive"),
	file,this);
    const char* ident = msg.getValue("faxident",msg.getValue("caller"));
    if (!ident)
	ident = "anonymous";
    m_ident = ident;
    // outgoing means from Yate to file so the fax should answer by default
    m_caller = msg.getBoolValue("faxcaller",!outgoing);
    m_ecm = msg.getBoolValue("faxecm");
    m_address = file;
    Engine::enqueue(message("chan.startup"));
}

// Destructor - clears all (audio, image) endpoints early
FaxChan::~FaxChan()
{
    Debug(DebugAll,"FaxChan::~FaxChan() [%p]",this);
    clearEndpoint();
    Engine::enqueue(message("chan.hangup"));
}

// Build data channels, attaches a wrapper and starts it up
bool FaxChan::startup(FaxWrapper* wrap, const char* type)
{
    wrap->debugName(debugName());
    FaxSource* fs = new FaxSource(wrap);
    setSource(fs,type);
    fs->deref();
    FaxConsumer* fc = new FaxConsumer(wrap);
    setConsumer(fc,type);
    fc->deref();
    wrap->setECM(m_ecm);
    bool ok = wrap->startup(this);
    wrap->deref();
    return ok;
}

// Attach and start an analog or digital wrapper
bool FaxChan::startup(bool digital)
{
    if (digital)
	return startup(new T38Terminal(address(),m_ident,m_sender,m_caller),"image");
    else
	return startup(new FaxTerminal(address(),m_ident,m_sender,m_caller));
}

// Handler for an originator fax start request
bool FaxChan::msgAnswered(Message& msg)
{
    if (Channel::msgAnswered(msg)) {
	startup();
	return true;
    }
    return false;
}

// Handler for an answerer fax start request
void FaxChan::answer(const char* targetid)
{
    if (targetid)
	m_targetid = targetid;
    status("answered");
    startup();
    Engine::enqueue(message("call.answered"));
}

bool FaxDriver::msgExecute(Message& msg, String& dest)
{
    Regexp r("^\\([^/]*\\)/\\(.*\\)$");
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

    RefPointer<FaxChan> fc;
    CallEndpoint* ce = static_cast<CallEndpoint *>(msg.userData());
    if (ce) {
	fc = new FaxChan(true,dest,transmit,msg);
	fc->deref();
	if (fc->connect(ce)) {
	    msg.setParam("peerid",fc->id());
	    msg.setParam("targetid",fc->id());
	    fc->answer(msg.getValue("id",ce->id()));
	    return true;
	}
    }
    else {
	fc = new FaxChan(false,dest,transmit,msg);
	fc->deref();
	Message m("call.route");
	fc->complete(m);
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

FaxDriver::FaxDriver()
    : Driver("fax"), m_first(true)
{
    Output("Loaded module Fax");
}

void FaxDriver::initialize()
{
    Output("Initializing module Fax");
    setup();
    if (m_first) {
	m_first = false;
	// TODO: add other handlers
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
