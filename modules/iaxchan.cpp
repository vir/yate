/**
 * iaxchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * IAX channel
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

static int s_ast_formats = 0;
static Configuration s_cfg;
static Mutex s_mutex;

static ObjList m_calls;
class YateIAXConnection;

class IAXSource : public DataSource
{
public:
    IAXSource(const char *frm) : DataSource(frm),m_total(0),m_time(Time::now())
    { Debug(DebugInfo,"IAXSource::IAXSource [%p] frm %s",this,frm);};
    ~IAXSource();
    void Forward(const DataBlock &data);
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

    virtual void Consume(const DataBlock &data);

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
    bool accepting(iax_event *e);
    void answer(iax_event *e);
    void reg(iax_event *e);
    void run(void);
    void terminateall(void);
    YateIAXConnection *findconn(iax_session *session);
    YateIAXConnection *findconn(String ourcallid);
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
    void disconnected(void);
    void abort();
    int makeCall(char *cidnum, char *cidname, char *target = 0, char *lang = 0);
    void hangup(char *reason = "Unexpected problem");
    void reject(char *reason = "Unexpected problem");
    void startAudio(int format,int capability);
    void sourceAudio(void *buffer, int len, int format);
    void handleEvent(iax_event *event);
    inline iax_session *session() const
	{ return m_session; }
    String ourcallid;
    String partycallid;
    String calleraddress;
    String calledaddress;
private: 
    iax_session *m_session;
    bool m_final;
    int m_ast_format;
};


class IAXPlugin : public Plugin
{
public:
    IAXPlugin();
    virtual ~IAXPlugin();
    virtual void initialize();
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

class SMSHandler : public MessageHandler
{
public:
    SMSHandler(const char *name) : MessageHandler(name,100) { }
    virtual bool received(Message &msg);
};

class DTMFHandler : public MessageHandler
{
public:
    DTMFHandler(const char *name) : MessageHandler(name,100) { }
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

static IAXPlugin iplugin;

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
    int fd = iax_get_fd();
    int tos = s_cfg.getIntValue("general","tos",dict_tos,0);
    if (tos)
	    ::setsockopt(fd,IPPROTO_IP,IP_TOS,&tos,sizeof(tos));
    if (s_cfg.getBoolValue("general","debug",false))
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
    return true;
}


void YateIAXEndPoint::terminateall(void)
{
    
    Debug(DebugInfo,"YateIAXEndPoint::terminateall()");
    m_calls.clear();
}

// Handle regular conectionless events with a valid session
void YateIAXEndPoint::handleEvent(iax_event *event)
{
#ifdef DEBUG
    Debug("IAX Event",DebugAll,"Connectionless event %d/%d",event->etype,event->subclass);
#endif
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
#ifdef DEBUG
	    Debug("IAX Event",DebugAll,"event %d/%d",e->etype,e->subclass);
#endif
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
			conn->abort();
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
	iax_accept(e->session,frm->value);
	s_mutex.unlock();
	return 1;
    }	
    Message m("auth");
    if (e->ies.username)
        m.addParam("username",e->ies.username);
    else
	m.addParam("username",e->session->username);
    if (Engine::dispatch(m) && m.retValue().null())
    {
	s_mutex.lock();
	iax_accept(e->session,frm->value);
	s_mutex.unlock();
	return 1;
    }
    if (e->etype != IAX_EVENT_AUTHRP)
    {
	int methods = IAX_AUTH_MD5;
	String s(::rand());
	strncpy(e->session->username,e->ies.username,sizeof(e->session->username));
	strncpy(e->session->dnid,e->ies.called_number,sizeof(e->session->dnid));
	strncpy(e->session->callerid,e->ies.calling_name,sizeof(e->session->callerid));
	e->session->voiceformat = e->ies.format;
	e->session->peerformats = e->ies.capability;
	strncpy(e->session->challenge,s.safe(),sizeof(e->session->challenge));
	s_mutex.lock();
	iax_send_authreq(e->session, methods);
	s_mutex.unlock();
	return 0;
    }
    if (e->ies.md5_result)
    {	
	const char *ret = m.retValue();
	if (!ret)
	{
	    s_mutex.lock();
	    iax_send_regrej(e->session);
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
	    iax_accept(e->session,2);
	    s_mutex.unlock();
	    return 1;
	} else
	{
	    s_mutex.lock();
	    iax_send_regrej(e->session);
	    s_mutex.unlock();
	    return 0;
	}
    }	
    return 0;
}
    
void YateIAXEndPoint::answer(iax_event *e)
{
    if (!accepting(e))
	return;
    Message *m = new Message("route");
    m->addParam("driver","iax");
//  m->addParam("id",String(e->did));
    if (e->ies.calling_name)
        m->addParam("callername",e->ies.calling_name);
    else
        m->addParam("callername",e->session->callerid);
    if (e->ies.called_number)
        m->addParam("called",e->ies.called_number);
    else
        m->addParam("called",e->session->dnid);
    Debug(DebugInfo,"callername %s and called %s",e->ies.calling_number,e->ies.called_number);
    Engine::dispatch(m);
    if (m->retValue() != NULL) {
	YateIAXConnection *conn = new YateIAXConnection(e->session);
	//this have to be here to get the right called_address.
	conn->calledaddress = m->retValue();
	
	*m = "call";
	m->userData(conn);
	m->addParam("callto",m->retValue());
	m->addParam("partycallid",conn->ourcallid);
	m->retValue() = 0;
	if (!Engine::dispatch(m))
	{
	    conn->reject("I haven't been able to connect you with the other module");
	    delete conn;
	    delete m;
	    return;
	}
	/* i do this to setup the peercallid by getting ourcallid 
	* from the other party */
	String ourcallid(m->getValue("ourcallid"));
	Debug(DebugInfo,"partycallid %s",ourcallid.c_str());
	if (ourcallid)
	    conn->partycallid = ourcallid;
	conn->deref();
	s_mutex.lock();
	::iax_answer(e->session);
	s_mutex.unlock();
	int format,capability;
	if (e->ies.format != 0) 
	    format = e->ies.format;
	else 
	    format = e->session->voiceformat;
	if (e->ies.capability != 0) 
	    capability = e->ies.capability;
	else 
	    capability = e->session->peerformats;
	
	conn->startAudio(format,capability);
	Debug(DebugInfo,"The return value of the message is %s %p",m->retValue().c_str(),m->userData());

    } else {
	Debug(DebugInfo,"I haven't been able to find a route for this IAX call");
	s_mutex.lock();
	::iax_reject(e->session,"No route");
	::iax_destroy(e->session);
	s_mutex.unlock();
    }
//    String str = "The answer is: " ;
//    str << m->retValue();
//    Debug(DebugInfo,str);
    delete m;
}

void YateIAXEndPoint::reg(iax_event *e)
{
    Message m("auth");
    if (e->ies.username)
        m.addParam("username",e->ies.username);
    else
	m.addParam("username",e->session->username);
    if (Engine::dispatch(m) && m.retValue().null())
    {
	s_mutex.lock();
	iax_send_regack(e->session);
	s_mutex.unlock();
	return;
    }
    if (e->ies.md5_result)
    {	
	const char *ret = m.retValue();
	if (!ret)
	{
	    s_mutex.lock();
	    iax_send_regrej(e->session);
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
	    strncpy(e->session->username,e->ies.username,sizeof(e->session->username));
	    s_mutex.lock();
	    iax_send_regack(e->session);
	    s_mutex.unlock();
	} else
	{
	    s_mutex.lock();
	    iax_send_regrej(e->session);
	    s_mutex.unlock();
	}
	return;
    }	
    int methods = IAX_AUTH_MD5;
    String s(::rand());
    strncpy(e->session->challenge,s.safe(),sizeof(e->session->challenge));
    s_mutex.lock();
    iax_send_regauth(e->session, methods);
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

YateIAXConnection * YateIAXEndPoint::findconn(String ourcallid)
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

YateIAXConnection::YateIAXConnection(iax_session *session)
    : m_session(session), m_final(false), m_ast_format(0)
{
    Debug(DebugAll,"YateIAXConnection::YateIAXConnection() [%p]",this);
    if (!m_session) {
	s_mutex.lock();
	m_session = ::iax_session_new();
	s_mutex.unlock();
    }
    iplugin.m_endpoint->calls().append(this);
    ::iax_set_private(m_session,this);
    char buf[64];
    snprintf(buf,sizeof(buf),"%p",m_session);
    ourcallid=buf;
}


YateIAXConnection::~YateIAXConnection()
{
    Debugger debug(DebugAll,"YateIAXConnection::~YateIAXConnection()"," [%p]",this);
    iplugin.m_endpoint->calls().remove(this,false);
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

// Handle regular connection events with a valid session
void YateIAXConnection::handleEvent(iax_event *event)
{
#ifdef DEBUG
    Debug("IAX Event",DebugAll,"Connection event %d/%d in [%p]",event->etype,event->subclass,this);
#endif
    switch(event->etype) {
	case IAX_EVENT_ACCEPT:
	    startAudio(event->ies.format,event->ies.capability);
	    break;
	case IAX_EVENT_VOICE:
	    sourceAudio(event->data,event->datalen,event->subclass);
	    break;
	case IAX_EVENT_TEXT:
	    {
		Message m("sms");
		m.addParam("text",(char *)event->data);
		m.addParam("ourcallid",ourcallid.c_str());
		m.addParam("partycallid",partycallid.c_str());
		m.addParam("callerid",event->session->callerid);
		m.addParam("calledid",event->session->dnid);
		Engine::dispatch(m);
		Debug(DebugInfo,"this text is inside a call: %s",(char *)event->data);
	    }
	    break;
	case IAX_EVENT_DTMF:
	    {
		Message m("dtmf");
		/* this is because Paul wants this to be usable on non i386 */
		char buf[2];
		buf[0] = event->subclass;
		buf[1] = 0;
		m.addParam("text",buf);
		m.addParam("ourcallid",ourcallid.c_str());
		m.addParam("partycallid",partycallid.c_str());
		m.addParam("callerid",event->session->callerid);
		m.addParam("calledid",event->session->dnid);
		Engine::dispatch(m); 
		Debug(DebugInfo,"this text is inside a call: %d",event->subclass);
	    }
	    break;
#if 0
	case IAX_EVENT_TIMEOUT:
	    break;
	case IAX_EVENT_RINGA:
	    break;
	case IAX_EVENT_BUSY:
	    break;
#endif
	case IAX_EVENT_ANSWER:
	    startAudio(event->ies.format,event->ies.capability);
	    break; 
	default:
	    Debug(DebugInfo,"Unhandled connection IAX event %d/%d in [%p]",event->etype,event->subclass,this);
    }
}

// We must call this method when the IAX library already destroyed the session
void YateIAXConnection::abort()
{
    Debug(DebugAll,"YateIAXConnection::abort()");
    m_final = true;
    m_session = 0;
}

void YateIAXConnection::hangup(char *reason)
{
    Debug(DebugAll,"YateIAXConnection::hangup()");
    if (!m_final) {
	s_mutex.lock();
	m_final = true;
	::iax_hangup(m_session,reason);
	s_mutex.unlock();
    }
}

void YateIAXConnection::reject(char *reason)
{
    Debug(DebugAll,"YateIAXConnection::reject()");
    if (!m_final) {
	s_mutex.lock();
	m_final = true;
	::iax_reject(m_session,reason);
	s_mutex.unlock();
    }
}

int YateIAXConnection::makeCall(char *cidnum, char *cidname, char *target, char *lang)
{
    Lock lock(s_mutex);
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

void YateIAXConnection::disconnected()
{
    Debug(DebugAll,"YateIAXConnection::disconnected()");
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

void IAXSource::Forward(const DataBlock &data)
{
    m_total += data.length();
    DataSource::Forward(data);
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

void YateIAXAudioConsumer::Consume(const DataBlock &data)
{
    m_total += data.length();
    ::iax_send_voice(m_session,m_ast_format,(char *)data.data(),data.length());
}

bool SMSHandler::received(Message &msg)
{
    String partycallid(msg.getValue("partycallid"));
    if (!partycallid)
	return false;
    String text(msg.getValue("text"));
    if (!text)
	return false;
    Debug(DebugInfo,"text %s partycallid %s",text.c_str(),partycallid.c_str());
    YateIAXConnection *conn= iplugin.m_endpoint->findconn(partycallid);  
    if (!conn)
	Debug(DebugInfo,"conn is null");
    else {
	text << "\0";
	::iax_send_text(conn->session(),(char *)(text.c_str()));
	return true;
    }
    return false;
}

bool DTMFHandler::received(Message &msg)
{
    String partycallid(msg.getValue("partycallid"));
    if (!partycallid)
	return false;
    String text(msg.getValue("text"));
    if (!text)
	return false;
    Debug(DebugInfo,"text %s partycallid %s",text.c_str(),partycallid.c_str());
    YateIAXConnection *conn= iplugin.m_endpoint->findconn(partycallid);  
    if (!conn)
	Debug(DebugInfo,"conn is null");
    else {
	for (unsigned int i=0;i<text.length();i++)
	    ::iax_send_dtmf(conn->session(),(text[i]));
	return true;
    }
    return false;
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
    String ourcallid(msg.getValue("ourcallid"));
    if (ourcallid)
	conn->partycallid = ourcallid;
    conn->calledaddress = dest;
    int i = conn->makeCall((char *)msg.getValue("caller"),(char *)msg.getValue("callername"),(char *)dest.matchString(1).safe());
    if (i < 0) {
	Debug(DebugInfo,"call failed in iax_call with code %d",i);
	conn->destruct();
	return false;
    }
    DataEndpoint *dd = static_cast<DataEndpoint *>(msg.userData());
    if (dd && conn->connect(dd))
	conn->deref();
    return true;	
};

bool StatusHandler::received(Message &msg)
{
    const char *sel = msg.getValue("module");
    if (sel && ::strcmp(sel,"iaxchan") && ::strcmp(sel,"varchans"))
	return false;
    String st("iaxchan,type=varchans");
    st << ",chans=" << iplugin.m_endpoint->calls().count() << ",[LIST]";
    ObjList *l = &iplugin.m_endpoint->calls();
    for (; l; l=l->next()) {
	YateIAXConnection *c = static_cast<YateIAXConnection *>(l->get());
	if (c) {
	    st << ",iax/" << c->ourcallid << "=" << c->calledaddress << "/" << c->partycallid;
	}
    }
    msg.retValue() << st << "\n";
    return false;
}

bool DropHandler::received(Message &msg)
{
    String id(msg.getValue("id"));
    if (id.null()) {
	Debug("IAXDroper",DebugInfo,"Dropping all calls");
	ObjList *l = &iplugin.m_endpoint->calls();
	for (; l; l=l->next()) {
	    YateIAXConnection *c = static_cast<YateIAXConnection *>(l->get());
	    if(c)
		delete c;
	}
    }
    if (!id.startsWith("iax"))
	return false;
    id >> "/";
    YateIAXConnection *conn = iplugin.m_endpoint->findconn(id);
    if (conn) {
	Debug("IAXDropper",DebugInfo,"Dropping call '%s' [%p]",conn->ourcallid.c_str(),conn);
	delete conn;
	return true;
    }
    Debug("IAXDropper",DebugInfo,"Could not find call '%s'",id.c_str());
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


void IAXPlugin::initialize()
{
    Output("Initializing module IAX");
    s_cfg = Engine::configFile("iaxchan");
    s_cfg.load();
    if (!m_endpoint) {
	if (!YateIAXEndPoint::Init())
	    return;
	m_endpoint = new YateIAXEndPoint;
    }
    if (m_first) {
	m_first = false;
	Engine::install(new IAXHandler("call"));
	Engine::install(new SMSHandler("sms"));
	Engine::install(new DTMFHandler("dtmf"));
	Engine::install(new StatusHandler("status"));
	Engine::install(new DropHandler("drop"));
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
