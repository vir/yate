/**
 * iaxchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * IAX channel
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
#include <yateversn.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdlib.h>

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
    { "lowdelay", IPTOS_LOWDELAY },
    { "throughput", IPTOS_THROUGHPUT },
    { "reliability", IPTOS_RELIABILITY },
    { "mincost", IPTOS_MINCOST },
    { 0, 0 }
};

static bool s_debugging = true;
static int s_ast_formats = 0;
static Configuration s_cfg;
static Mutex s_mutex;
static Mutex s_route;

static ObjList m_calls;
class YateIAXConnection;

class IAXSource : public DataSource
{
public:
    IAXSource(const char *frm) : DataSource(frm),m_total(0),m_time(Time::now())
    { Debug(DebugInfo,"IAXSource::IAXSource [%p] frm %s",this,frm);};
    ~IAXSource();
    void Forward(const DataBlock &data, unsigned long timeDelta = 0);
private:
    unsigned m_total;
    unsigned long long m_time;
};
class YateIAXAudioConsumer : public DataConsumer
{
public:
    YateIAXAudioConsumer(YateIAXConnection *conn, iax_session *session,
	int ast_format = AST_FORMAT_SLINEAR, const char *format = "slin");

    ~YateIAXAudioConsumer();

    virtual void Consume(const DataBlock &data, unsigned long timeDelta);

private:
    YateIAXConnection *m_conn;
    iax_session *m_session;
    DataBlock m_buffer;
    int m_ast_format;
    unsigned m_total;
    unsigned long long m_time;
};


class YateIAXEndPoint : public Thread
{
public:
    YateIAXEndPoint();
    ~YateIAXEndPoint();
    static bool Init(void);
    static void Setup(void);
    bool accepting(iax_event *e);
    void answer(iax_event *e);
    void reg(iax_event *e);
    void run(void);
    void terminateall(void);
    YateIAXConnection *findconn(iax_session *session);
    YateIAXConnection *findconn(const String& ourcallid);
    void handleEvent(iax_event *event);

    inline ObjList &calls()
	{ return m_calls; }
private:
    ObjList m_calls;
};

class YateIAXConnection :  public DataEndpoint
{
public:
    YateIAXConnection(iax_session *session = 0);
    ~YateIAXConnection();
    virtual void disconnected(bool final, const char *reason);
    void abort(int type = 0);
    int makeCall(char *cidnum, char *cidname, char *target = 0, char *lang = 0);
    bool startRouting(iax_event *e);
    void hangup(const char *reason = 0);
    void reject(const char *reason = 0);
    void answered();
    void ringing();
    void startAudio(int format,int capability);
    void sourceAudio(void *buffer, int len, int format);
    void handleEvent(iax_event *event);
    inline iax_session *session() const
	{ return m_session; }
    inline void setStatus(const char* newstatus)
	{ m_status = newstatus; }
    inline const String& status() const
	{ return m_status; }
    String ourcallid;
    String targetid;
    String address;
private: 
    iax_session *m_session;
    bool m_final;
    int m_ast_format;
    String m_status;
    const char* m_reason;
};


class IAXPlugin : public Plugin
{
public:
    IAXPlugin();
    virtual ~IAXPlugin();
    virtual void initialize();
    virtual bool isBusy() const;
    void cleanup();
    YateIAXEndPoint *m_endpoint;
private:
    bool m_first;
};

class IAXHandler : public MessageHandler
{
public:
    IAXHandler(const char *name) : MessageHandler(name) { }
    virtual bool received(Message &msg);
};

class StatusHandler : public MessageHandler
{
public:
    StatusHandler(const char *name) : MessageHandler(name,100) { }
    virtual bool received(Message &msg);
};

class DropHandler : public MessageHandler
{
public:
    DropHandler(const char *name) : MessageHandler(name,100) { }
    virtual bool received(Message &msg);
};

class IAXConnHandler : public MessageReceiver
{
public:
    enum {
	Ringing,
	Answered,
	Transfer,
	DTMF,
	Text,
    };
    virtual bool received(Message &msg, int id);
};
										
class IAXMsgThread : public Thread
{
public:
    IAXMsgThread(Message *msg, const char *id, int format, int capab)
        : Thread("IAXMsgThread"), m_msg(msg), m_id(id), m_format(format), m_capab(capab)
	{ }
    virtual void run();
    virtual void cleanup();
    bool route();
    inline static int count()
        { return s_count; }
    inline static int routed()
        { return s_routed; }
private:
    Message *m_msg;
    String m_id;
    int m_format;
    int m_capab;
    static int s_count;
    static int s_routed;
};

static IAXPlugin iplugin;

static void iax_err_cb(const char *s)
{
    Debug("IAX",DebugWarn,"%s",s);
}

static void iax_out_cb(const char *s)
{
    if (s_debugging)
	Debug("IAX",DebugInfo,"%s",s);
}

YateIAXEndPoint::YateIAXEndPoint()
    : Thread("IAX EndPoint")
{
    Debug(DebugAll,"YateIAXEndPoint::YateIAXEndPoint() [%p]",this);
}

YateIAXEndPoint::~YateIAXEndPoint()
{
    Debug(DebugAll,"YateIAXEndPoint::~YateIAXEndPoint() [%p]",this);
    terminateall();
    iplugin.m_endpoint = 0;
}

bool YateIAXEndPoint::Init(void)
{
    int port = s_cfg.getIntValue("general","port",4569);
    if (!port) {
	Debug(DebugInfo,"IAX is disabled by configuration");
	return false;
    }
    if (!(::iax_init(port))) {
	Debug(DebugFail,"I can't initialize the IAX library");
	return false;
    }
    iax_set_error(iax_err_cb);
    iax_set_output(iax_out_cb);
    int tos = s_cfg.getIntValue("general","tos",dict_tos,0);
    if (tos)
	::setsockopt(iax_get_fd(),IPPROTO_IP,IP_TOS,&tos,sizeof(tos));
    return true;
}

void YateIAXEndPoint::Setup(void)
{
    if (s_debugging = s_cfg.getBoolValue("general","debug",false))
	::iax_enable_debug();
    else
	::iax_disable_debug();
    int frm = 0;
    bool def = s_cfg.getBoolValue("formats","default",true);
    for (int i = 0; dict_iaxformats[i].token; i++) {
	if (s_cfg.getBoolValue("formats",dict_iaxformats[i].token,def)) {
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
	Debug(DebugInfo,"Available IAX formats:%s",s.safe());
    }
    s_ast_formats = frm;
    ::iax_set_formats(s_ast_formats);
}

void YateIAXEndPoint::terminateall(void)
{
    Debug(DebugInfo,"YateIAXEndPoint::terminateall()");
    m_calls.clear();
}

// Handle regular conectionless events with a valid session
void YateIAXEndPoint::handleEvent(iax_event *event)
{
    DDebug("IAX Event",DebugAll,"Connectionless event %d/%d",event->etype,event->subclass);
    switch(event->etype) {
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
		Debug(DebugInfo,"this text is outside a call: %s , a handle for this dosen't yet exist",(char *)event->data);
	    }
	    break;
	default:
	    Debug(DebugInfo,"Unhandled connectionless IAX event %d/%d",event->etype,event->subclass);
    }
}

void YateIAXEndPoint::run(void)
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
	::usleep(t*1000);
	for (;;) {
	    s_mutex.lock();
	    e = ::iax_get_event(0);
	    s_mutex.unlock();
	    if (!e)
		break;
	    DDebug("IAX Event",DebugAll,"event %d/%d",e->etype,e->subclass);
	    YateIAXConnection *conn = 0;
	    // We first take care of the special events
	    switch(e->etype) {
		case IAX_EVENT_CONNECT:
		    answer(e);
		    break;
		case IAX_EVENT_TIMEOUT:
		case IAX_EVENT_REJECT:
		case IAX_EVENT_HANGUP:
		    if ((conn = findconn(e->session)) != 0) {
			conn->abort(e->etype);
			conn->destruct();
		    }
		    break;
		case IAX_EVENT_REGREQ:
		    reg(e);
		    break;
		case IAX_EVENT_AUTHRP:
		    answer(e);
		    break;
		default:
		    conn = (YateIAXConnection *)::iax_get_private(e->session);
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

bool YateIAXEndPoint::accepting(iax_event *e)
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
	Debug(DebugGoOn,"IAX format 0x%X (local: 0x%X, remote: 0x%X, common: 0x%X) not available in [%p]",
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
    
void YateIAXEndPoint::answer(iax_event *e)
{
    if (!accepting(e))
	return;
    YateIAXConnection *conn = new YateIAXConnection(e->session);
    if (!conn->startRouting(e)) {
	s_mutex.lock();
	conn->reject("Server error");
	s_mutex.unlock();
    }
}

void YateIAXEndPoint::reg(iax_event *e)
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

YateIAXConnection * YateIAXEndPoint::findconn(iax_session *session)
{ 
    ObjList *p = &m_calls; 
    for (; p; p=p->next()) { 
	YateIAXConnection *t = static_cast<YateIAXConnection *>(p->get()); 
	if (t && (t->session() == session))
	    return t; 
    }
    return 0; 
}

YateIAXConnection * YateIAXEndPoint::findconn(const String& ourcallid)
{ 
    ObjList *p = &m_calls; 
    for (; p; p=p->next()) { 
	YateIAXConnection *t = static_cast<YateIAXConnection *>(p->get()); 
	if (t && (t->ourcallid == ourcallid))
	{
	    return t; 
	}
    }
    return 0; 
}

int IAXMsgThread::s_count = 0;
int IAXMsgThread::s_routed = 0;

bool IAXMsgThread::route()
{
    Debug(DebugAll,"Routing thread for %s [%p]",m_id.c_str(),this);
    const char* err = (Engine::dispatch(m_msg) && !m_msg->retValue().null()) ? 0 : "No route";
    Lock lock(s_mutex);
    YateIAXConnection *conn = iplugin.m_endpoint->findconn(m_id);
    if (!conn) {
        Debug(DebugMild,"YateIAXConnection '%s' vanished while routing!",m_id.c_str());
	return false;
    }
    if (!err) {
        *m_msg = "call.execute";
        m_msg->addParam("callto",m_msg->retValue());
        m_msg->retValue().clear();
        m_msg->userData(static_cast<DataEndpoint *>(conn));
        if (Engine::dispatch(m_msg)) {
            Debug(DebugInfo,"Routing IAX call %s [%p] to '%s'",
		m_id.c_str(),conn,m_msg->getValue("callto"));
            conn->setStatus("routed");
            conn->targetid = m_msg->getValue("targetid");
	    conn->startAudio(m_format,m_capab);
	    if (conn->targetid.null()) {
                Debug(DebugInfo,"Answering now IAX call %s [%p] because we have no targetid",m_id.c_str(),conn);
                conn->answered();
            }
            conn->deref();
	    return true;
	}
        else
            err = "Not connected";
    }
    conn->setStatus("rejected");
    conn->reject(err);
    return false;
}

void IAXMsgThread::run()
{
    s_route.lock();
    s_count++;
    s_route.unlock();
    Debug(DebugAll,"Started routing thread for %s [%p]",m_id.c_str(),this);
    bool ok = route();
    s_route.lock();
    s_count--;
    if (ok)
        s_routed++;
    s_route.unlock();
}

void IAXMsgThread::cleanup()
{
    Debug(DebugAll,"Cleaning up routing thread for %s [%p]",m_id.c_str(),this);
    delete m_msg;
}

YateIAXConnection::YateIAXConnection(iax_session *session)
    : m_session(session), m_final(false), m_ast_format(0), m_reason(0)
{
    Debug(DebugAll,"YateIAXConnection::YateIAXConnection() [%p]",this);
    s_mutex.lock();
    if (m_session)
	m_status = "incoming";
    else {
	m_status = "outgoing";
	m_session = ::iax_session_new();
    }
    char buf[64];
    snprintf(buf,sizeof(buf),"iax/%p",m_session);
    ourcallid=buf;
    iplugin.m_endpoint->calls().append(this);
    ::iax_set_private(m_session,this);
    s_mutex.unlock();
    Message* m = new Message("chan.startup");
    m->addParam("id",ourcallid);
    m->addParam("direction",m_status);
    m->addParam("status","new");
    Engine::enqueue(m);
}


YateIAXConnection::~YateIAXConnection()
{
    Debugger debug(DebugAll,"YateIAXConnection::~YateIAXConnection()"," [%p]",this);
    setStatus("destroyed");
    s_mutex.lock();
    iplugin.m_endpoint->calls().remove(this,false);
    s_mutex.unlock();
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

bool YateIAXConnection::startRouting(iax_event *e)
{
    Message *m = new Message("call.route");
    m->addParam("driver","iax");
    m->addParam("id",ourcallid);
    if (e->ies.calling_name)
	m->addParam("callername",e->ies.calling_name);
    else
	m->addParam("callername",e->session->callerid);
    if (e->ies.called_number)
	m->addParam("called",e->ies.called_number);
    else
	m->addParam("called",e->session->dnid);

    if (e->ies.calling_number)
	address = e->ies.calling_number;
    else if (e->ies.username)
	address = e->ies.username;
    else if (e->ies.calling_ani)
	address = e->ies.calling_name;
    else if (e->ies.calling_name)
	address = e->ies.calling_ani;

    int format,capability;
    if (e->ies.format != 0) 
	format = e->ies.format;
    else 
	format = e->session->voiceformat;
    if (e->ies.capability != 0) 
	capability = e->ies.capability;
    else 
	capability = e->session->peerformats;

    IAXMsgThread *t = new IAXMsgThread(m,ourcallid,format,capability);
    if (!t->startup()) {
        Debug(DebugWarn,"Error starting routing thread! [%p]",this);
        delete t;
        setStatus("dropped");
        return false;
    }
    return true;
}

// Handle regular connection events with a valid session
void YateIAXConnection::handleEvent(iax_event *event)
{
    DDebug("IAX Event",DebugAll,"Connection event %d/%d in [%p]",event->etype,event->subclass,this);
    switch(event->etype) {
	case IAX_EVENT_ACCEPT:
	    Debug("IAX",DebugInfo,"ACCEPT inside a call [%p]",this);
	    startAudio(event->ies.format,event->ies.capability);
	    break;
	case IAX_EVENT_VOICE:
	    sourceAudio(event->data,event->datalen,event->subclass);
	    break;
	case IAX_EVENT_TEXT:
	    Debug("IAX",DebugInfo,"TEXT inside a call: '%s' [%p]",(char *)event->data,this);
	    {
		Message* m = new Message("chan.text");
		m->addParam("id",ourcallid);
		m->addParam("text",(char *)event->data);
		m->addParam("targetid",targetid.c_str());
		m->addParam("callerid",event->session->callerid);
		m->addParam("calledid",event->session->dnid);
		Engine::enqueue(m);
	    }
	    break;
	case IAX_EVENT_DTMF:
	    Debug("IAX",DebugInfo,"DTFM inside a call: %d [%p]",event->subclass,this);
	    {
		Message* m = new Message("chan.dtmf");
		/* this is because Paul wants this to be usable on non i386 */
		char buf[2];
		buf[0] = event->subclass;
		buf[1] = 0;
		m->addParam("id",ourcallid);
		m->addParam("text",buf);
		m->addParam("targetid",targetid.c_str());
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
	    Debug("IAX",DebugInfo,"RING inside a call [%p]",this);
	    {
		Message* m = new Message("call.ringing");
		m->addParam("id",ourcallid);
		m->addParam("targetid",targetid.c_str());
		Engine::enqueue(m);
	    }
	    break; 
	case IAX_EVENT_ANSWER:
	    Debug("IAX",DebugInfo,"ANSWER inside a call [%p]",this);
	    {
		Message* m = new Message("call.answered");
		m->addParam("id",ourcallid);
		m->addParam("targetid",targetid.c_str());
		Engine::enqueue(m);
	    }
	    startAudio(event->ies.format,event->ies.capability);
	    break; 
	default:
	    Debug(DebugInfo,"Unhandled connection IAX event %d/%d in [%p]",event->etype,event->subclass,this);
    }
}

// We must call this method when the IAX library already destroyed the session
void YateIAXConnection::abort(int type)
{
    Debug(DebugAll,"YateIAXConnection::abort(%d) [%p]",type,this);
    m_final = true;
    m_session = 0;
    // Session is gone... get rid of these two really fast!
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

void YateIAXConnection::hangup(const char *reason)
{
    Debug(DebugAll,"YateIAXConnection::hangup('%s') [%p]",reason,this);
    if (!reason)
	reason = m_reason;
    if (!reason)
	reason = "Unexpected problem";
    if (!m_final) {
	s_mutex.lock();
	m_final = true;
	::iax_hangup(m_session,(char*)reason);
	s_mutex.unlock();
    }
    Message* m = new Message("chan.hangup");
    m->addParam("id",ourcallid);
    m->addParam("status","hangup");
    m->addParam("reason",reason);
    Engine::enqueue(m);
}

void YateIAXConnection::reject(const char *reason)
{
    Debug(DebugAll,"YateIAXConnection::reject('%s') [%p]",reason,this);
    if (!reason)
	reason = m_reason;
    if (!reason)
	reason = "Unexpected problem";
    if (!m_final) {
	m_final = true;
	if (m_session)
	    ::iax_reject(m_session,(char*)reason);
    }
}

int YateIAXConnection::makeCall(char *cidnum, char *cidname, char *target, char *lang)
{
    Lock lock(s_mutex);
    address = target;
    ::iax_set_formats(s_ast_formats);
    return ::iax_call(m_session,cidnum,cidname,target,lang,0);
}

void YateIAXConnection::startAudio(int format,int capability)
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
    Debug(DebugAll,"Creating IAX DataConsumer format \"%s\" (0x%X) in [%p]",frm->token,frm->value,this);
    setConsumer(new YateIAXAudioConsumer(this,m_session,frm->value,frm->token));
    getConsumer()->deref();
}

void YateIAXConnection::sourceAudio(void *buffer, int len, int format)
{
    format &= s_ast_formats;
    if (!format)
	return;
    if (!getSource()) {
	// Exact match required - incoming data must be a single format
	const char *frm = lookup(format,dict_iaxformats);
	if (!frm)
	    return;
	Debug(DebugAll,"Creating IAXSource format \"%s\" (0x%X) in [%p]",frm,format,this);
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

void YateIAXConnection::disconnected(bool final, const char *reason)
{
    Debug(DebugAll,"YateIAXConnection::disconnected() '%s'",reason);
    setStatus("disconnected");
    // If we still have a connection this is the last chance to get transferred
    if (!(final || m_final)) {
	Message m("chan.disconnected");
	m.addParam("id",ourcallid.c_str());
	if (reason)
	    m.addParam("reason",reason);
	if (targetid) {
	    // Announce our old party but at this point it may be destroyed
	    m.addParam("targetid",targetid.c_str());
	    targetid.clear();
	}
	m.userData(this);
	Engine::dispatch(m);
    }
}


void YateIAXConnection::answered()
{
    if (!m_session)
	return;
    setStatus("answered");
    ::iax_answer(m_session);
}

void YateIAXConnection::ringing()
{
    if (!m_session)
	return;
    setStatus("ringing");
    ::iax_ring_announce(m_session);
}

IAXSource::~IAXSource()
{
    Debug(DebugAll,"IAXSource::~IAXSource() [%p] total=%u",this,m_total);
    if (m_time) {
	m_time = Time::now() - m_time;
	if (m_time) {
	    m_time = (m_total*1000000ULL + m_time/2) / m_time;
	    Debug(DebugInfo,"IAXSource rate=%llu b/s",m_time);
	}
    }
    
}

void IAXSource::Forward(const DataBlock &data, unsigned long timeDelta)
{
    m_total += data.length();
    DataSource::Forward(data, timeDelta);
}

YateIAXAudioConsumer::YateIAXAudioConsumer(YateIAXConnection *conn, iax_session *session, int ast_format, const char *format)
    : DataConsumer(format), m_conn(conn), m_session(session),
      m_ast_format(ast_format), m_total(0), m_time(Time::now())
{
    Debug(DebugAll,"YateIAXAudioConsumer::YateIAXAudioConsumer(%p) [%p]",conn,this);
}

YateIAXAudioConsumer::~YateIAXAudioConsumer()
{
    Debug(DebugAll,"YateIAXAudioConsumer::~YateIAXAudioConsumer() [%p] total=%u",this,m_total);
    if (m_time) {
	m_time = Time::now() - m_time;
	if (m_time) {
	    m_time = (m_total*1000000ULL + m_time/2) / m_time;
	    Debug(DebugInfo,"YateIAXAudioConsumer rate=%llu b/s",m_time);
	}
    }
    
}

void YateIAXAudioConsumer::Consume(const DataBlock &data, unsigned long timeDelta)
{
    m_total += data.length();
    ::iax_send_voice(m_session,m_ast_format,(char *)data.data(),data.length());
}

bool IAXConnHandler::received(Message &msg, int id)
{
    String callid(msg.getValue("targetid"));
    if (!callid.startsWith("iax/",false))
	return false;
    Lock lock(s_mutex);
    YateIAXConnection *conn= iplugin.m_endpoint->findconn(callid);
    if (!(conn && conn->session())) {
	Debug("IAX",DebugInfo,"Could not find valid connection '%s'",callid.c_str());
	return false;
    }
    switch (id) {
	case Answered:
	    conn->answered();
	    break;
	case Ringing:
	    conn->ringing();
	    break;
	case Transfer:
	    {
		String callto(msg.getValue("callto"));
		if (!callto)
		    return false;
		Debug(DebugInfo,"Transferring connection '%s' [%p] to '%s'",
		    callid.c_str(),conn,callto.c_str());
		Message m("call.execute");
		m.addParam("callto",callto.c_str());
		m.addParam("id",conn->ourcallid);
		m.userData(conn);
		if (Engine::dispatch(m)) {
		    String targetid(m.getValue("targetid"));
		    Debug(DebugInfo,"IAX [%p] transferred, new targetid '%s'",
			conn,targetid.c_str());
		    conn->targetid = targetid;
		    return true;
		}
	    }
	    break;
	case DTMF:
	    for (const char* t = msg.getValue("text"); t && *t; ++t)
		::iax_send_dtmf(conn->session(),*t);
	    break;
	case Text:
	    {
		const char* t = msg.getValue("text");
		if (t)
		    ::iax_send_text(conn->session(),(char *)t);
	    }
	    break;
    }
    return true;
}

bool IAXHandler::received(Message &msg)
{
    String dest(msg.getValue("callto"));
    if (dest.null())
	return false;
    Regexp r("^iax/\\(.*\\)$");
    if (!dest.matches(r))
	return false;
    if (!msg.userData()) {
	Debug(DebugFail,"IAX call found but no data channel!");
	return false;
    }
    String ip = dest.matchString(1);
    YateIAXConnection *conn = new YateIAXConnection();
    /* i do this to setup the peercallid by getting ourcallid 
     * from the other party */
    conn->targetid = msg.getValue("id");
    int i = conn->makeCall((char *)msg.getValue("caller"),(char *)msg.getValue("callername"),(char *)dest.matchString(1).safe());
    if (i < 0) {
	Debug(DebugInfo,"call failed in iax_call with code %d",i);
	conn->destruct();
	return false;
    }
    DataEndpoint *dd = static_cast<DataEndpoint *>(msg.userData());
    if (dd && conn->connect(dd))
    {
	msg.addParam("targetid",conn->ourcallid);
	conn->deref();
    }
    return true;	
};

bool StatusHandler::received(Message &msg)
{
    const char *sel = msg.getValue("module");
    if (sel && ::strcmp(sel,"iaxchan") && ::strcmp(sel,"varchans"))
	return false;
    String st("name=iaxchan,type=varchans,format=Status|Caller");
    st << ";chans=" << iplugin.m_endpoint->calls().count() << ";";
    ObjList *l = &iplugin.m_endpoint->calls();
    bool first = true;
    for (; l; l=l->next()) {
	YateIAXConnection *c = static_cast<YateIAXConnection *>(l->get());
	if (c) {
	    if (first)
		first = false;
	    else
		st << ",";
	    st << c->ourcallid << "=" << c->status() << "|" << c->address;
	}
    }
    msg.retValue() << st << "\n";
    return false;
}

bool DropHandler::received(Message &msg)
{
    String ourcallid(msg.getValue("id"));
    if (ourcallid.null()) {
	Debug("IAXDroper",DebugInfo,"Dropping all calls");
	ObjList *l = &iplugin.m_endpoint->calls();
	for (; l; l=l->next()) {
	    YateIAXConnection *c = static_cast<YateIAXConnection *>(l->get());
	    if(c)
		delete c;
	}
    }
    if (!ourcallid.startsWith("iax/"))
	return false;
    YateIAXConnection *conn = iplugin.m_endpoint->findconn(ourcallid);
    if (conn) {
	Debug("IAXDropper",DebugInfo,"Dropping call '%s' [%p]",conn->ourcallid.c_str(),conn);
	delete conn;
	return true;
    }
    Debug("IAXDropper",DebugInfo,"Could not find call '%s'",ourcallid.c_str());
    return false;
}

IAXPlugin::IAXPlugin()
    : m_endpoint(0), m_first(true)
{
    Output("Loaded module IAX");
}

void IAXPlugin::cleanup()
{
    if (m_endpoint) {
	delete m_endpoint;
	m_endpoint = 0;
    }
}

IAXPlugin::~IAXPlugin()
{
    Output("Unloading module IAX");
    cleanup();
}

bool IAXPlugin::isBusy() const
{
    return m_endpoint && (m_endpoint->calls().count() != 0);
}

void IAXPlugin::initialize()
{
    Output("Initializing module IAX");
    s_cfg = Engine::configFile("iaxchan");
    s_cfg.load();
    if (!m_endpoint) {
	if (!YateIAXEndPoint::Init())
	    return;
	m_endpoint = new YateIAXEndPoint;
	m_endpoint->startup();
    }
    YateIAXEndPoint::Setup();
    if (m_first) {
	m_first = false;
	IAXConnHandler* ch = new IAXConnHandler;
	Engine::install(new MessageRelay("call.ringing",ch,IAXConnHandler::Ringing));
	Engine::install(new MessageRelay("call.answered",ch,IAXConnHandler::Answered));
	Engine::install(new MessageRelay("call.transfer",ch,IAXConnHandler::Transfer));
	Engine::install(new MessageRelay("chan.dtmf",ch,IAXConnHandler::DTMF));
	Engine::install(new MessageRelay("chan.text",ch,IAXConnHandler::Text));

	Engine::install(new IAXHandler("call.execute"));
	Engine::install(new StatusHandler("engine.status"));
	Engine::install(new DropHandler("call.drop"));
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
