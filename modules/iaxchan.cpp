/**
 * iaxchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * IAX channel
 */


#include <telengine.h>
#include <telephony.h>
#include <yateversn.h>

#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

extern "C" {
#include <iax/iax-client.h>
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

static int s_ast_formats = 0;
static Configuration s_cfg;
static Mutex s_mutex;

static ObjList m_calls;
class YateIAXConnection;

class YateIAXAudioConsumer : public DataConsumer
{
public:
    YateIAXAudioConsumer(YateIAXConnection *conn, iax_session *session,
	int ast_format = AST_FORMAT_SLINEAR, const char *format = "slin");

    ~YateIAXAudioConsumer()
	{ Debug(DebugAll,"YateIAXAudioConsumer::~YateIAXAudioConsumer() [%p]",this); }

    virtual void Consume(const DataBlock &data);

private:
    YateIAXConnection *m_conn;
    iax_session *m_session;
    DataBlock m_buffer;
    int m_ast_format;
};


class YateIAXEndPoint : public Thread
{
public:
    YateIAXEndPoint();
    ~YateIAXEndPoint();
    static bool Init(void);
    void answer(iax_event *e);
    void run(void);
    void terminateall(void);
    YateIAXConnection *findconn(iax_session *session);
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
    void startAudio(int format);
    void sourceAudio(void *buffer, int len, int format);
    void handleEvent(iax_event *event);
    inline iax_session *session() const
	{ return m_session; }
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
		case IAX_EVENT_REJECT:
		case IAX_EVENT_HANGUP:
		    if ((conn = findconn(e->session)) != 0) {
			conn->abort();
			conn->destruct();
		    }
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

void YateIAXEndPoint::answer(iax_event *e)
{
    Message *m = new Message("route");
    m->addParam("driver","iax");
//    m->addParam("id",String(e->did));
    m->addParam("callername",e->ies.calling_number);
    m->addParam("called",e->ies.called_number);
    Debug(DebugInfo,"callername %s and called %s",e->ies.calling_number,e->ies.called_number);
    Engine::dispatch(m);
    if (m->retValue() != NULL) {
	s_mutex.lock();
	::iax_accept(e->session);
	s_mutex.unlock();
	YateIAXConnection *conn = new YateIAXConnection(e->session);
	
	*m = "call";
	m->userData(conn);
	m->addParam("callto",m->retValue());
	m->retValue() = 0;
	if (!Engine::dispatch(m))
	{
	    conn->reject("I haven't been able to connect you with the other module");
	    delete conn;
	    delete m;
	    return;
	}
	conn->deref();
	s_mutex.lock();
	::iax_answer(e->session);
	s_mutex.unlock();
	conn->startAudio(e->ies.format);
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
	    startAudio(event->ies.format);
	    break;
	case IAX_EVENT_VOICE:
	    sourceAudio(event->data,event->datalen,event->subclass);
	    break;
#if 0
	case IAX_EVENT_DTMF:
	    break;
	case IAX_EVENT_TIMEOUT:
	    break;
	case IAX_EVENT_RINGA:
	    break;
	case IAX_EVENT_BUSY:
	    break;
	case IAX_EVENT_ANSWER:
	    break; 
#endif
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

void YateIAXConnection::startAudio(int format)
{
    if (getConsumer())
	return;
    int masked = format & s_ast_formats;
    const TokenDict *frm = dict_iaxformats;
    for (; frm->token; frm++) {
	if (frm->value & masked)
	    break;
    }
    if (!frm->token) {
	Debug(DebugGoOn,"IAX format 0x%X (local: 0x%X, common: 0x%X) not available in [%p]",
	    format,s_ast_formats,masked,this);
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
	Debug(DebugAll,"Creating IAX DataSource format \"%s\" (0x%X) in [%p]",frm,format,this);
	m_ast_format = format;
	setSource(new DataSource(frm));
	getSource()->deref();
    }
    if ((format == m_ast_format) && getSource()) {
	DataBlock data(buffer,len,false);
	getSource()->Forward(data);
	data.clear(false);
    }
}

void YateIAXConnection::disconnected()
{
    Debug(DebugAll,"YateIAXConnection::disconnected()");
}

YateIAXAudioConsumer::YateIAXAudioConsumer(YateIAXConnection *conn, iax_session *session, int ast_format, const char *format)
    : DataConsumer(format), m_conn(conn), m_session(session), m_ast_format(ast_format)
{
    Debug(DebugAll,"YateIAXAudioConsumer::YateIAXAudioConsumer(%p) [%p]",conn,this);
}

void YateIAXAudioConsumer::Consume(const DataBlock &data)
{
    ::iax_send_voice(m_session,m_ast_format,(char *)data.data(),data.length());
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
/*	Engine::install(new H323Dropper("drop"));
	Engine::install(new H323Stopper("engine.halt"));
	Engine::install(new StatusHandler);*/
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
