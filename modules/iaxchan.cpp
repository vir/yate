/**
 * iaxchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * IAX channel
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
#include <yateversn.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>

extern "C" {
#include <iax-client.h>
#include <iax2-parser.h>
#include <md5.h>
};


using namespace TelEngine;

static TokenDict dict_iaxformats[] = {
    { "slin", AST_FORMAT_SLINEAR },
    { "gsm", AST_FORMAT_GSM },
    { "lpc10", AST_FORMAT_LPC10 },
    { "mulaw", AST_FORMAT_ULAW },
    { "alaw", AST_FORMAT_ALAW },
    { 0, 0 }
};

static TokenDict dict_tos[] = {
    { "lowdelay", Socket::LowDelay },
    { "throughput", Socket::MaxThroughput },
    { "reliability", Socket::MaxReliability },
    { "mincost", Socket::MinCost },
    { 0, 0 }
};

static bool s_debugging = true;
static int s_ast_formats = 0;
static Configuration s_cfg;

// mutex for iax library calls
static Mutex s_mutex;

class IAXConnection;

class IAXSource : public DataSource
{
public:
    IAXSource(const char *frm) : DataSource(frm),m_total(0),m_time(Time::now())
    { Debug(DebugInfo,"IAXSource::IAXSource [%p] frm %s",this,frm);};
    ~IAXSource();
    void Forward(const DataBlock &data, unsigned long timeDelta = 0);
private:
    unsigned m_total;
    u_int64_t m_time;
};

class IAXAudioConsumer : public DataConsumer
{
public:
    IAXAudioConsumer(IAXConnection *conn,
	int ast_format = AST_FORMAT_SLINEAR, const char *format = "slin");

    ~IAXAudioConsumer();

    virtual void Consume(const DataBlock &data, unsigned long timeDelta);

private:
    IAXConnection *m_conn;
    int m_ast_format;
    unsigned m_total;
    u_int64_t m_time;
};


class IAXEndPoint : public Thread
{
public:
    IAXEndPoint();
    ~IAXEndPoint();
    static bool Init(void);
    static void Setup(void);
    bool accepting(iax_event *e);
    void answer(iax_event *e);
    void reg(iax_event *e);
    void run(void);
    void handleEvent(iax_event *event);
};

class IAXConnection :  public Channel
{
public:
    IAXConnection(Driver* driver, const char* addr, iax_session *session = 0);
    ~IAXConnection();
    virtual void disconnected(bool final, const char *reason);
    void abort(int type = 0);
    int makeCall(const char* targid, char* cidnum, char* cidname, char* target = 0, char* lang = 0);
    bool startRouting(iax_event *e);
    void hangup(const char *reason = 0);
    virtual void callAccept(Message& msg);
    virtual void callReject(const char* error, const char* reason = 0);
    virtual bool msgRinging(Message& msg);
    virtual bool msgAnswered(Message& msg);
    virtual bool msgTone(Message& msg, const char* tone);
    virtual bool msgText(Message& msg, const char* text);
    virtual bool msgDrop(Message& msg, const char* reason);
    void startAudio(int format,int capability);
    void sourceAudio(void *buffer, int len, int format);
    void sendVoice(char* buffer, int len, int format);
    void handleEvent(iax_event *event);
    inline iax_session *session() const
	{ return m_session; }
    inline bool muted() const
	{ return m_muted; }
private: 
    iax_session *m_session;
    bool m_final;
    bool m_muted;
    int m_ast_format;
    int m_format;
    int m_capab;
    const char* m_reason;
};


class IAXDriver : public Driver
{
public:
    IAXDriver();
    virtual ~IAXDriver();
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest);
    IAXConnection *find(iax_session *session);
    IAXEndPoint *m_endpoint;
};

static IAXDriver iplugin;

static void iax_err_cb(const char *s)
{
    Debug("IAX",DebugWarn,"%s",s);
}

static void iax_out_cb(const char *s)
{
    if (s_debugging)
	Debug("IAX",DebugInfo,"%s",s);
}

IAXEndPoint::IAXEndPoint()
    : Thread("IAX EndPoint")
{
    Debug(&iplugin,DebugAll,"IAXEndPoint::IAXEndPoint() [%p]",this);
}

IAXEndPoint::~IAXEndPoint()
{
    Debug(&iplugin,DebugAll,"IAXEndPoint::~IAXEndPoint() [%p]",this);
    iplugin.m_endpoint = 0;
}

bool IAXEndPoint::Init(void)
{
    int port = s_cfg.getIntValue("general","port",4569);
    if (!port) {
	Debug(DebugInfo,"IAX is disabled by configuration");
	return false;
    }
    port = ::iax_init(port);
    if (port < 0) {
	Debug(DebugGoOn,"I can't initialize the IAX library");
	return false;
    }
    iax_set_error(iax_err_cb);
    iax_set_output(iax_out_cb);
    int tos = s_cfg.getIntValue("general","tos",dict_tos,0);
    if (tos) {
	Socket s(iax_get_fd());
	s.setTOS(static_cast<Socket::TOS>(tos));
	s.detach();
    }
    return true;
}

void IAXEndPoint::Setup(void)
{
    if (s_debugging = s_cfg.getBoolValue("general","debug",false))
	::iax_enable_debug();
    else
	::iax_disable_debug();
    int frm = 0;
    bool def = s_cfg.getBoolValue("formats","default",true);
    for (int i = 0; dict_iaxformats[i].token; i++) {
	if (s_cfg.getBoolValue("formats",dict_iaxformats[i].token,def && DataTranslator::canConvert(dict_iaxformats[i].token))) {
	    // TODO: check if we have a translator for this format
	    frm |= dict_iaxformats[i].value;
	}
    }
    if (!frm)
	Debug(DebugWarn,"All audio IAX formats are disabled");
    else {
	String s;
	for (unsigned u=(1<<31); u; u>>=1) {
	    if (frm & u) {
		const char *f = lookup(u,dict_iaxformats);
		if (f)
		    s << " " << f;
	    }
	}
	Debug(&iplugin,DebugInfo,"Available IAX formats:%s",s.safe());
    }
    s_ast_formats = frm;
    ::iax_set_formats(s_ast_formats);
}

// Handle regular conectionless events with a valid session
void IAXEndPoint::handleEvent(iax_event *event)
{
    DDebug(&iplugin,DebugAll,"Connectionless IAX event %d/%d",event->etype,event->subclass);
    switch (event->etype) {
#if 0
	case IAX_EVENT_REGREQ:
	    break;
	case IAX_EVENT_REGACK:
	    break;
	case IAX_EVENT_REGREJ:
	    break;
#endif
	case IAX_EVENT_TEXT:
	    {
		Debug(&iplugin,DebugInfo,"this text is outside a call: %s , a handle for this dosen't yet exist",(char *)event->data);
	    }
	    break;
	default:
	    Debug(&iplugin,DebugInfo,"Unhandled connectionless IAX event %d/%d",event->etype,event->subclass);
    }
}

void IAXEndPoint::run(void)
{
    iax_event *e;
    for (;;)
    {
	s_mutex.lock();
	int t = ::iax_time_to_next_event();
	s_mutex.unlock();
	// Sleep at most 10ms
	if ((t < 0) || (t > 10))
	    t = 10;
	Thread::msleep(t);
	for (;;) {
	    Thread::check();
	    s_mutex.lock();
	    e = ::iax_get_event(0);
	    s_mutex.unlock();
	    if (!e)
		break;
	    XDebug("IAX Event",DebugAll,"event %d/%d",e->etype,e->subclass);
	    IAXConnection *conn = 0;
	    // We first take care of the special events
	    switch(e->etype) {
		case IAX_EVENT_CONNECT:
		    answer(e);
		    break;
		case IAX_EVENT_TIMEOUT:
		case IAX_EVENT_REJECT:
		case IAX_EVENT_HANGUP:
		    if ((conn = iplugin.find(e->session)) != 0) {
			conn->abort(e->etype);
			conn->destruct();
		    }
		    else
			Debug(&iplugin,DebugInfo,"Could not find IAX connection to handle %d in session %p",
			    e->etype,e->session);
		    break;
		case IAX_EVENT_REGREQ:
		    reg(e);
		    break;
		case IAX_EVENT_AUTHRP:
		    answer(e);
		    break;
		default:
		    conn = (IAXConnection *)::iax_get_private(e->session);
		    if (conn)
			conn->handleEvent(e);
		    else
			handleEvent(e);
	    }
	    s_mutex.lock();
	    ::iax_event_free(e);
	    s_mutex.unlock();
	}
    }
}

bool IAXEndPoint::accepting(iax_event *e)
{
    int masked = e->ies.format & s_ast_formats;
    const TokenDict *frm = dict_iaxformats;
    for (; frm->token; frm++) {
	if (frm->value == masked)
	    break;
    }
    if (!frm->token) {
	masked = e->ies.capability & s_ast_formats;
	frm = dict_iaxformats;
	for (; frm->token; frm++) {
	    if (frm->value & masked)
		break;
	}
    }
    if (!frm->token) {
	Debug(DebugWarn,"IAX format 0x%X (local: 0x%X, remote: 0x%X, common: 0x%X) not available in [%p]",
	    e->ies.format,s_ast_formats,e->ies.capability,masked,this);
    }

    if (s_cfg.getBoolValue("users","unauth",false))
    {
	s_mutex.lock();
	::iax_accept(e->session,frm->value);
	s_mutex.unlock();
	return true;
    }	
    Message m("user.auth");
    if (e->ies.username)
	m.addParam("username",e->ies.username);
    else
	m.addParam("username",e->session->username);
    if (Engine::dispatch(m) && m.retValue().null())
    {
	s_mutex.lock();
	::iax_accept(e->session,frm->value);
	s_mutex.unlock();
	return true;
    }
    if (e->etype != IAX_EVENT_AUTHRP)
    {
	int methods = IAX_AUTH_MD5;
	String s(::rand());
	strncpy(e->session->username,c_safe(e->ies.username),sizeof(e->session->username));
	strncpy(e->session->dnid,c_safe(e->ies.called_number),sizeof(e->session->dnid));
	strncpy(e->session->callerid,c_safe(e->ies.calling_name),sizeof(e->session->callerid));
	e->session->voiceformat = e->ies.format;
	e->session->peerformats = e->ies.capability;
	strncpy(e->session->challenge,s.safe(),sizeof(e->session->challenge));
	s_mutex.lock();
	::iax_send_authreq(e->session, methods);
	s_mutex.unlock();
	return 0;
    }
    if (e->ies.md5_result)
    {	
	const char *ret = m.retValue();
	if (!ret)
	{
	    s_mutex.lock();
	    ::iax_send_regrej(e->session);
	    s_mutex.unlock();
	    return 0;
	}
	struct MD5Context md5;
	MD5Init(&md5);
	MD5Update(&md5, (const unsigned char *) e->session->challenge, strlen(e->session->challenge));
	MD5Update(&md5, (const unsigned char *) ret, strlen(ret));
	unsigned char reply[16];
	char realreply[256];
	MD5Final(reply, &md5);
	char *ptr = realreply; 
	for (int x=0;x<16;x++)
	    ptr+=sprintf(ptr,"%02x", (unsigned int)reply[x]);
	if (!strcmp(e->ies.md5_result,realreply))
	{
	    e->session->refresh = 100;
	    s_mutex.lock();
	    ::iax_accept(e->session,2);
	    s_mutex.unlock();
	    return true;
	} else
	{
	    s_mutex.lock();
	    ::iax_send_regrej(e->session);
	    s_mutex.unlock();
	    return false;
	}
    }	
    return false;
}
    
void IAXEndPoint::answer(iax_event *e)
{
    if (!accepting(e))
	return;
    String addr(::inet_ntoa(e->session->peeraddr.sin_addr));
    addr << ":" << ntohs(e->session->peeraddr.sin_port);
    IAXConnection *conn = new IAXConnection(&iplugin,addr,e->session);
    conn->startRouting(e);
}

void IAXEndPoint::reg(iax_event *e)
{
    Message m("user.auth");
    if (e->ies.username)
	m.addParam("username",e->ies.username);
    else
	m.addParam("username",e->session->username);
    if (Engine::dispatch(m) && m.retValue().null())
    {
	s_mutex.lock();
	::iax_send_regack(e->session);
	s_mutex.unlock();
	return;
    }
    if (e->ies.md5_result)
    {	
	const char *ret = m.retValue();
	if (!ret)
	{
	    s_mutex.lock();
	    ::iax_send_regrej(e->session);
	    s_mutex.unlock();
	    return;
	}
	struct MD5Context md5;
	MD5Init(&md5);
	MD5Update(&md5, (const unsigned char *) e->session->challenge, strlen(e->session->challenge));
	MD5Update(&md5, (const unsigned char *) ret, strlen(ret));
	unsigned char reply[16];
	char realreply[256];
	MD5Final(reply, &md5);
	char *ptr = realreply; 
	for (int x=0;x<16;x++)
	    ptr+=sprintf(ptr,"%02x", (unsigned int)reply[x]);
	if (!strcmp(e->ies.md5_result,realreply))
	{
	    e->session->refresh = 100;
	    strncpy(e->session->username,c_safe(e->ies.username),sizeof(e->session->username));
	    s_mutex.lock();
	    ::iax_send_regack(e->session);
	    s_mutex.unlock();
	} else
	{
	    s_mutex.lock();
	    ::iax_send_regrej(e->session);
	    s_mutex.unlock();
	}
	return;
    }	
    int methods = IAX_AUTH_MD5;
    String s(::rand());
    strncpy(e->session->challenge,s.safe(),sizeof(e->session->challenge));
    s_mutex.lock();
    ::iax_send_regauth(e->session, methods);
    s_mutex.unlock();
}

IAXConnection::IAXConnection(Driver* driver, const char* addr, iax_session *session)
    : Channel(driver,0,(session == 0)),
      m_session(session), m_final(false), m_muted(false),
      m_ast_format(0), m_format(0), m_capab(0), m_reason(0)
{
    Debug(this,DebugAll,"IAXConnection::IAXConnection() [%p]",this);
    m_address = addr;
    s_mutex.lock();
    if (!m_session)
	m_session = ::iax_session_new();
    ::iax_set_private(m_session,this);
    s_mutex.unlock();
    Message* m = message("chan.startup");
    m->addParam("direction",status());
    Engine::enqueue(m);
}


IAXConnection::~IAXConnection()
{
    Debugger debug(DebugAll,"IAXConnection::~IAXConnection()"," [%p]",this);
    status("destroyed");
    setConsumer();
    setSource();
    m_ast_format = 0;
    hangup();
    if (m_session) {
	s_mutex.lock();
	::iax_set_private(m_session,NULL);
	::iax_destroy(m_session);
	m_session = 0;
	s_mutex.unlock();
    }
}

bool IAXConnection::startRouting(iax_event *e)
{
    Message *m = message("call.route");
    if (e->ies.calling_name)
	m->addParam("callername",e->ies.calling_name);
    else
	m->addParam("callername",e->session->callerid);
    if (e->ies.called_number)
	m->addParam("called",e->ies.called_number);
    else
	m->addParam("called",e->session->dnid);

    if (e->ies.calling_number)
	m_address = e->ies.calling_number;
    else if (e->ies.username)
	m_address = e->ies.username;
    else if (e->ies.calling_ani)
	m_address = e->ies.calling_name;
    else if (e->ies.calling_name)
	m_address = e->ies.calling_ani;

    if (e->ies.format != 0) 
	m_format = e->ies.format;
    else 
	m_format = e->session->voiceformat;
    if (e->ies.capability != 0) 
	m_capab = e->ies.capability;
    else 
	m_capab = e->session->peerformats;

    return startRouter(m);
}

// Handle regular connection events with a valid session
void IAXConnection::handleEvent(iax_event *event)
{
    XDebug(this,DebugAll,"Connection IAX event %d/%d in [%p]",event->etype,event->subclass,this);
    switch(event->etype) {
	case IAX_EVENT_ACCEPT:
	    Debug(this,DebugInfo,"IAX ACCEPT inside a call [%p]",this);
	    startAudio(event->ies.format,event->ies.capability);
	    break;
	case IAX_EVENT_VOICE:
	    sourceAudio(event->data,event->datalen,event->subclass);
	    break;
	case IAX_EVENT_QUELCH:
	    m_muted = true;
	    break;
	case IAX_EVENT_UNQUELCH:
	    m_muted = false;
	    break;
	case IAX_EVENT_TEXT:
	    Debug(this,DebugInfo,"IAX TEXT inside a call: '%s' [%p]",(char *)event->data,this);
	    {
		Message* m = message("chan.text");
		m->addParam("text",(char *)event->data);
		m->addParam("callerid",event->session->callerid);
		m->addParam("calledid",event->session->dnid);
		Engine::enqueue(m);
	    }
	    break;
	case IAX_EVENT_DTMF:
	    Debug(this,DebugInfo,"IAX DTFM inside a call: %d [%p]",event->subclass,this);
	    {
		Message* m = message("chan.dtmf");
		/* this is because Paul wants this to be usable on non i386 */
		char buf[2];
		buf[0] = event->subclass;
		buf[1] = 0;
		m->addParam("text",buf);
		m->addParam("callerid",event->session->callerid);
		m->addParam("calledid",event->session->dnid);
		Engine::enqueue(m);
	    }
	    break;
#if 0
	case IAX_EVENT_TIMEOUT:
	    break;
	case IAX_EVENT_BUSY:
	    break;
#endif
	case IAX_EVENT_RINGA:
	    Debug(this,DebugInfo,"IAX RING inside a call [%p]",this);
	    Engine::enqueue(message("call.ringing"));
	    break; 
	case IAX_EVENT_ANSWER:
	    Debug(this,DebugInfo,"IAX ANSWER inside a call [%p]",this);
	    Engine::enqueue(message("call.answered"));
	    startAudio(event->ies.format,event->ies.capability);
	    break; 
	default:
	    Debug(this,DebugInfo,"Unhandled connection IAX event %d/%d in [%p]",event->etype,event->subclass,this);
    }
}

// We must call this method when the IAX library already destroyed the session
void IAXConnection::abort(int type)
{
    Debug(this,DebugAll,"IAXConnection::abort(%d) [%p]",type,this);
    // Session is / will be gone... get rid of all these really fast!
    m_session = 0;
    m_final = true;
    setConsumer();
    setSource();
    switch (type) {
	case IAX_EVENT_TIMEOUT:
	    m_reason = "Timeout";
	    break;
	case IAX_EVENT_REJECT:
	    m_reason = "Call rejected";
	    break;
	case IAX_EVENT_HANGUP:
	    m_reason = "Hangup";
	    break;
    }
}

void IAXConnection::hangup(const char *reason)
{
    Debug(this,DebugAll,"IAXConnection::hangup('%s') [%p]",reason,this);
    if (!reason)
	reason = m_reason;
    if (!reason)
	reason = Engine::exiting() ? "Server shutdown" : "Unexpected problem";
    if (!m_final) {
	s_mutex.lock();
	m_final = true;
	::iax_hangup(m_session,(char*)reason);
	s_mutex.unlock();
    }
    Message* m = message("chan.hangup",true);
    m->setParam("status","hangup");
    m->setParam("reason",reason);
    Engine::enqueue(m);
}

void IAXConnection::callAccept(Message& msg)
{
    Debug(this,DebugAll,"IAXConnection::callAccept() [%p]",this);
    startAudio(m_format,m_capab);
    Channel::callAccept(msg);
}

void IAXConnection::callReject(const char* error, const char* reason)
{
    Debug(this,DebugAll,"IAXConnection::callReject('%s','%s') [%p]",error,reason,this);
    Channel::callReject(error,reason);
    if (!reason)
	reason = m_reason;
    if (!reason)
	reason = error;
    if (!m_final) {
	m_final = true;
	if (m_session) {
	    s_mutex.lock();
	    ::iax_reject(m_session,(char*)reason);
	    s_mutex.unlock();
	}
    }
}

int IAXConnection::makeCall(const char* targid, char* cidnum, char* cidname, char* target, char* lang)
{
    Lock lock(s_mutex);
    m_address = target;
    m_targetid = targid;
    ::iax_set_formats(s_ast_formats);
    return ::iax_call(m_session,cidnum,cidname,target,lang,0);
}

void IAXConnection::startAudio(int format,int capability)
{
    if (getConsumer())
	return;
    int masked = format & s_ast_formats;
    const TokenDict *frm = dict_iaxformats;
    for (; frm->token; frm++) {
	if (frm->value == masked)
	    break;
    }
    if (!frm->token) {
	masked = capability & s_ast_formats;
	frm = dict_iaxformats;
	for (; frm->token; frm++) {
	    if (frm->value & masked)
		break;
	}
    }
    if (!frm->token) {
	Debug(DebugGoOn,"IAX format 0x%X (local: 0x%X, remote: 0x%X, common: 0x%X) not available in [%p]",
	    format,s_ast_formats,capability,masked,this);
	return;
    }
    Debug(this,DebugAll,"Creating IAX DataConsumer format \"%s\" (0x%X) in [%p]",frm->token,frm->value,this);
    setConsumer(new IAXAudioConsumer(this,frm->value,frm->token));
    getConsumer()->deref();
}

void IAXConnection::sourceAudio(void *buffer, int len, int format)
{
    format &= s_ast_formats;
    if (m_muted || !format)
	return;
    if (!buffer || (len < 0) || (len > 1024)) {
	Debug("IAXAudio",DebugGoOn,"Invalid buffer=%p or len=%d [%p]",buffer,len,this);
	return;
    }
    if (!getSource()) {
	// Exact match required - incoming data must be a single format
	const char *frm = lookup(format,dict_iaxformats);
	if (!frm)
	    return;
	Debug(this,DebugAll,"Creating IAXSource format \"%s\" (0x%X) in [%p]",frm,format,this);
	m_ast_format = format;
	setSource(new IAXSource(frm));
	getSource()->deref();
	// this is for some clients to work out with yate (firefly)
	startAudio(format,0);	
    }
    if ((format == m_ast_format) && getSource()) {
	DataBlock data(buffer,len,false);
	((IAXSource *)(getSource()))->Forward(data);
	data.clear(false);
    }
}

void IAXConnection::sendVoice(char* buffer, int len, int format)
{
    if (m_muted || !m_session)
	return;
    s_mutex.lock();
    ::iax_send_voice(m_session,format,buffer,len);
    s_mutex.unlock();
}

void IAXConnection::disconnected(bool final, const char *reason)
{
    Debug(this,DebugAll,"IAXConnection::disconnected() '%s'",reason);
    status("disconnected");
    // If we still have a connection this is the last chance to get transferred
    if (!(final || m_final)) {
	Message m("chan.disconnected");
	m.addParam("id",id());
	if (reason)
	    m.addParam("reason",reason);
	if (targetid()) {
	    // Announce our old party but at this point it may be destroyed
	    m.addParam("targetid",targetid());
	    m_targetid.clear();
	}
	m.userData(this);
	Engine::dispatch(m);
    }
}

IAXSource::~IAXSource()
{
    Debug(&iplugin,DebugAll,"IAXSource::~IAXSource() [%p] total=%u",this,m_total);
    if (m_time) {
	m_time = Time::now() - m_time;
	if (m_time) {
	    m_time = (m_total*(u_int64_t)1000000 + m_time/2) / m_time;
	    Debug(DebugInfo,"IAXSource rate=" FMT64U " b/s",m_time);
	}
    }
    
}

void IAXSource::Forward(const DataBlock &data, unsigned long timeDelta)
{
    m_total += data.length();
    DataSource::Forward(data, timeDelta);
}

IAXAudioConsumer::IAXAudioConsumer(IAXConnection *conn, int ast_format, const char *format)
    : DataConsumer(format), m_conn(conn),
      m_ast_format(ast_format), m_total(0), m_time(Time::now())
{
    Debug(&iplugin,DebugAll,"IAXAudioConsumer::IAXAudioConsumer(%p) [%p]",conn,this);
}

IAXAudioConsumer::~IAXAudioConsumer()
{
    Debug(&iplugin,DebugAll,"IAXAudioConsumer::~IAXAudioConsumer() [%p] total=%u",this,m_total);
    if (m_time) {
	m_time = Time::now() - m_time;
	if (m_time) {
	    m_time = (m_total*(u_int64_t)1000000 + m_time/2) / m_time;
	    Debug(DebugInfo,"IAXAudioConsumer rate=" FMT64U " b/s",m_time);
	}
    }
    
}

void IAXAudioConsumer::Consume(const DataBlock &data, unsigned long timeDelta)
{
    m_total += data.length();
    if (m_conn)
	m_conn->sendVoice((char *)data.data(),data.length(),m_ast_format);
}

bool IAXConnection::msgRinging(Message& msg)
{
    if (!m_session)
	return false;
    status("ringing");
    ::iax_ring_announce(m_session);
    return true;
}

bool IAXConnection::msgAnswered(Message& msg)
{
    if (!m_session)
	return false;
    status("answered");
    ::iax_answer(m_session);
    return true;
}

bool IAXConnection::msgTone(Message& msg, const char* tone)
{
    for (; tone && *tone; ++tone)
    ::iax_send_dtmf(session(),*tone);
    return true;
}

bool IAXConnection::msgText(Message& msg, const char* text)
{
    if (text)
	::iax_send_text(session(),(char *)text);
    return true;
}

bool IAXConnection::msgDrop(Message& msg, const char* reason)
{
    Debug(this,DebugInfo,"Dropping IAX call '%s' [%p]",id().c_str(),this);
    disconnect(reason);
    return true;
}

bool IAXDriver::msgExecute(Message& msg, String& dest)
{
    if (!msg.userData()) {
	Debug(DebugWarn,"IAX call found but no data channel!");
	return false;
    }
    IAXConnection *conn = new IAXConnection(this,dest);
    /* i do this to setup the peercallid by getting id 
     * from the other party */
    int i = conn->makeCall(msg.getValue("id"),(char *)msg.getValue("caller"),(char *)msg.getValue("callername"),(char *)dest.safe());
    if (i < 0) {
	Debug(DebugInfo,"call failed in iax_call with code %d",i);
	conn->destruct();
	return false;
    }
    Channel *ch = static_cast<Channel*>(msg.userData());
    if (ch && conn->connect(ch))
    {
	msg.setParam("peerid",conn->id());
	msg.setParam("targetid",conn->id());
	conn->deref();
    }
    return true;	
};

IAXConnection* IAXDriver::find(iax_session *session)
{ 
    ObjList *p = channels().skipNull();
    for (; p; p=p->skipNext()) { 
	IAXConnection *t = static_cast<IAXConnection *>(p->get()); 
	if (t->session() == session)
	    return t; 
    }
    return 0; 
}

IAXDriver::IAXDriver()
    : Driver("iax","varchans"), m_endpoint(0)
{
    Output("Loaded module IAX");
}

IAXDriver::~IAXDriver()
{
    Output("Unloading module IAX");
    lock();
    channels().clear();
    unlock();
    if (m_endpoint) {
	delete m_endpoint;
	m_endpoint = 0;
    }
}

void IAXDriver::initialize()
{
    Output("Initializing module IAX");
    lock();
    s_cfg = Engine::configFile("iaxchan");
    s_cfg.load();
    unlock();
    if (!m_endpoint) {
	if (!IAXEndPoint::Init())
	    return;
	m_endpoint = new IAXEndPoint;
	m_endpoint->startup();
    }
    IAXEndPoint::Setup();
    setup();
}

/* vi: set ts=8 sw=4 sts=4 noet: */
