/**
 * mgcpgw.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Media Gateway Control Protocol - Gateway component
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
#include <yatemgcp.h>
#include <yatesdp.h>

#include <stdlib.h>

using namespace TelEngine;
namespace { // anonymous

class YMGCPEngine : public MGCPEngine
{
public:
    enum EpCommands {
	EpCmdUnk = 0,
	EpCmdRsip,
    };
    inline YMGCPEngine(const NamedList* params)
	: MGCPEngine(true,0,params)
	{ }
    virtual ~YMGCPEngine();
    virtual bool processEvent(MGCPTransaction* trans, MGCPMessage* msg);
    bool handleControl(const String& comp, Message& msg, bool& retVal);
    void completeControl(const String& partLine, const String& partWord, String& retVal);
    static const TokenDict s_epCmds[];
private:
    bool createConn(MGCPTransaction* trans, MGCPMessage* msg);
};

class MGCPChan : public Channel, public SDPSession
{
    YCLASS(MGCPChan,Channel);
public:
    enum IdType {
	CallId,
	ConnId,
	NtfyId,
    };
    MGCPChan(const char* connId = 0);
    virtual ~MGCPChan();
    virtual void callAccept(Message& msg);
    virtual bool msgTone(Message& msg, const char* tone);
    const String& getId(IdType type) const;
    bool processEvent(MGCPTransaction* tr, MGCPMessage* mm);
    bool initialEvent(MGCPTransaction* tr, MGCPMessage* mm, const MGCPEndpointId& id);
    void activate(bool standby);
protected:
    virtual void destroyed();
    virtual void disconnected(bool final, const char* reason);
    virtual Message* buildChanRtp(RefObject* context)
	{
	    Message* m = new Message("chan.rtp");
	    m->userData(context ? context : this);
	    return m;
	}
    virtual Message* buildChanRtp(SDPMedia* media, const char* addr, bool start, RefObject* context)
	{
	    Message* m = SDPSession::buildChanRtp(media,addr,start,context);
	    if (m)
		m->addParam("mgcp_allowed",String::boolText(false));
	    return m;
	}
    virtual void mediaChanged(const SDPMedia& media);
private:
    void endTransaction(int code = 407, const NamedList* params = 0, MimeSdpBody* sdp = 0);
    bool reqNotify(String& evt);
    bool setSignal(String& req);
    bool rqntParams(const MGCPMessage* mm);
    static void copyRtpParams(NamedList& dest, const NamedList& src);
    GenObject* m_this;
    MGCPTransaction* m_tr;
    SocketAddr m_addr;
    String m_connEp;
    String m_callId;
    String m_ntfyId;
    String m_rtpId;
    String m_stats;
    bool m_standby;
    bool m_isRtp;
    bool m_started;
};

class MGCPPlugin : public Driver
{
public:
    MGCPPlugin();
    virtual ~MGCPPlugin();
    virtual bool msgExecute(Message& msg, String& dest);
    virtual void initialize();
    RefPointer<MGCPChan> findConn(const String* id, MGCPChan::IdType type);
    inline RefPointer<MGCPChan> findConn(const String& id, MGCPChan::IdType type)
	{ return findConn(&id,type); }
    inline SDPParser& parser()
	{ return m_parser; }
    void activate(bool standby);
protected:
    virtual bool received(Message& msg, int id);
    virtual bool commandComplete(Message& msg, const String& partLine,
	const String& partWord);
    bool handleControl(Message& msg);
private:
    SDPParser m_parser;
};

class DummyCall : public CallEndpoint
{
public:
    inline DummyCall()
	: CallEndpoint("dummy")
	{ }
};

static Mutex s_mutex(false,"MGCP-GW");

static MGCPPlugin splugin;

static YMGCPEngine* s_engine = 0;

// preserve RTP session (local addr+port) even if remote address changed
static bool s_rtp_preserve = false;

// cluster and standby support
static bool s_cluster = false;

// warm standby mode
static bool s_standby = false;

// start time as UNIX time
String s_started;

const TokenDict YMGCPEngine::s_epCmds[] = {
    {"rsip", EpCmdRsip},
    {0,0}
};

static const String s_skipControlParams[] = {"component", "operation", "targetid", "handlers", ""};

// copy parameter (if present) with new name
bool copyRename(NamedList& dest, const char* dname, const NamedList& src, const String& sname)
{
    if (!sname)
	return false;
    const NamedString* value = src.getParam(sname);
    if (!value)
	return false;
    dest.setParam(dname,*value);
    return true;
}

// Find a string in a 0 terminated array
static bool findString(const String& what, const String* list)
{
    while (!TelEngine::null(list)) {
	if (*list == what)
	    return true;
	list++;
    }
    return false;
}


YMGCPEngine::~YMGCPEngine()
{
    s_engine = 0;
}

// process all MGCP events, distribute them according to their type
bool YMGCPEngine::processEvent(MGCPTransaction* trans, MGCPMessage* msg)
{
    DDebug(this,DebugAll,"YMGCPEngine::processEvent(%p,%p) [%p]",
	trans,msg,this);
    if (!trans)
	return false;
    s_mutex.lock();
    RefPointer<MGCPChan> chan = YOBJECT(MGCPChan,static_cast<GenObject*>(trans->userData()));
    s_mutex.unlock();
    if (chan)
	return chan->processEvent(trans,msg);
    if (!msg)
	return false;
    if (!trans->userData() && !trans->outgoing() && msg->isCommand()) {
	if (msg->name() == YSTRING("CRCX")) {
	    // create connection
	    if (!createConn(trans,msg))
		trans->setResponse(500); // unknown endpoint
	    return true;
	}
	if ((msg->name() == YSTRING("DLCX")) || // delete
	    (msg->name() == YSTRING("MDCX")) || // modify
	    (msg->name() == YSTRING("AUCX"))) { // audit
	    // connection must exist already
	    chan = splugin.findConn(msg->params.getParam(YSTRING("i")),MGCPChan::ConnId);
	    if (chan)
		return chan->processEvent(trans,msg);
	    trans->setResponse(515); // no connection
	    return true;
	}
	if (msg->name() == YSTRING("RQNT")) {
	    // request notify
	    chan = splugin.findConn(msg->params.getParam(YSTRING("x")),MGCPChan::NtfyId);
	    if (chan)
		return chan->processEvent(trans,msg);
	}
	if (msg->name() == YSTRING("EPCF")) {
	    // endpoint configuration
	    NamedList params("");
	    bool standby = msg->params.getBoolValue(YSTRING("x-standby"),s_standby);
	    if (standby != s_standby) {
		params << "Switching to " << (standby ? "standby" : "active") << " mode";
		Debug(this,DebugNote,"%s",params.c_str());
		s_standby = standby;
		splugin.activate(standby);
	    }
	    params.addParam("x-standby",String::boolText(s_standby));
	    trans->setResponse(200,&params);
	    return true;
	}
	if (msg->name() == YSTRING("AUEP")) {
	    // audit endpoint
	    NamedList params("");
	    params.addParam("MD",String(s_engine->maxRecvPacket()));
	    if (s_cluster) {
		params.addParam("x-standby",String::boolText(s_standby));
		params.addParam("x-started",s_started);
	    }
	    trans->setResponse(200,&params);
	    return true;
	}
	Debug(this,DebugMild,"Unhandled '%s' from '%s'",
	    msg->name().c_str(),msg->endpointId().c_str());
    }
    return false;
}

bool YMGCPEngine::handleControl(const String& comp, Message& msg, bool& retVal)
{
    retVal = false;
    Lock lock(this);
    MGCPEndpoint* ep = 0;
    for (ObjList* o = m_endpoints.skipNull(); o; o = o->skipNext()) {
	ep = static_cast<MGCPEndpoint*>(o->get());
	if (ep->toString() == comp)
	    break;
	ep = 0;
    }
    if (!ep)
	return false;
    MGCPEpInfo* peer = ep->peer();
    if (!peer)
	return true;
    String epId = ep->id();
    SocketAddr addr = peer->address();
    lock.drop();
    bool copyParams = true;
    String oper = msg[YSTRING("operation")];
    MGCPMessage* mm = 0;
    int cmd = lookup(oper.toLower(),s_epCmds);
    if (cmd == EpCmdRsip) {
	mm = new MGCPMessage(this,"RSIP",epId);
    }
    else {
	Debug(this,DebugNote,"Unknown ep control '%s'",msg.getValue("operation"));
	return true;
    }
    if (copyParams) {
	NamedIterator iter(msg);
	for (const NamedString* ns = 0; 0 != (ns = iter.get());)
	    if (!findString(ns->name(),s_skipControlParams))
		mm->params.addParam(ns->name(),*ns);
    }
    retVal = (0 != sendCommand(mm,addr));
    return true;
}

void YMGCPEngine::completeControl(const String& partLine, const String& partWord, String& retVal)
{
    if (!partLine) {
	// Complete endpoints
	Lock lock(this);
	for (ObjList* o = m_endpoints.skipNull(); o; o = o->skipNext()) {
	    MGCPEndpoint* ep = static_cast<MGCPEndpoint*>(o->get());
	    Module::itemComplete(retVal,ep->toString(),partWord);
	}
	return;
    }
    // Complete EP commands
    if (findEp(partLine)) {
	for (const TokenDict* d = s_epCmds; d->value; d++)
	    Module::itemComplete(retVal,d->token,partWord);
	return;
    }
}

// create a new connection
bool YMGCPEngine::createConn(MGCPTransaction* trans, MGCPMessage* msg)
{
    String id = msg->endpointId();
    const char* connId = msg->params.getValue(YSTRING("i"));
    DDebug(this,DebugInfo,"YMGCPEngine::createConn() id='%s' connId='%s'",id.c_str(),connId);
    if (connId && splugin.findConn(connId,MGCPChan::ConnId)) {
	trans->setResponse(539,"Connection exists");
	return true;
    }
    MGCPChan* chan = new MGCPChan(connId);
    chan->initChan();
    return chan->initialEvent(trans,msg,id);
}


MGCPChan::MGCPChan(const char* connId)
    : Channel(splugin),
      SDPSession(&splugin.parser()),
      m_this(0), m_tr(0), m_standby(s_standby), m_isRtp(false), m_started(false)
{
    DDebug(this,DebugAll,"MGCPChan::MGCPChan('%s') [%p]",connId,this);
    status("created");
    if (connId) {
	if (!m_standby)
	    Debug(this,DebugMild,"Using provided connection ID in active mode! [%p]",this);
	m_address = connId;
    }
    else {
	if (m_standby)
	    Debug(this,DebugMild,"Allocating connection ID in standby mode! [%p]",this);
	long int r = Random::random();
	m_address.hexify(&r,sizeof(r),0,true);
    }
    m_this = this;
}

MGCPChan::~MGCPChan()
{
    DDebug(this,DebugAll,"MGCPChan::~MGCPChan() [%p]",this);
    m_this = 0;
    endTransaction();
}


void MGCPChan::destroyed()
{
    m_this = 0;
    if (m_rtpMedia) {
	setMedia(0);
	clearEndpoint();
	if (m_callId && m_addr.valid()) {
	    MGCPMessage* mm = new MGCPMessage(s_engine,"DLCX",m_connEp);
	    mm->params.addParam("I",address());
	    mm->params.addParam("C",m_callId);
	    mm->params.addParam("P",m_stats,false);
	    s_engine->sendCommand(mm,m_addr);
	}
    }
}

void MGCPChan::disconnected(bool final, const char* reason)
{
    if (final || Engine::exiting())
	return;
    DummyCall* dummy = new DummyCall;
    connect(dummy);
    dummy->deref();
}

const String& MGCPChan::getId(IdType type) const
{
    switch (type) {
	case CallId:
	    return m_callId;
	case ConnId:
	    return address();
	case NtfyId:
	    return m_ntfyId;
	default:
	    return String::empty();
    }
}

void MGCPChan::activate(bool standby)
{
    if (standby == m_standby)
	return;
    Debug(this,DebugCall,"Switching to %s mode. [%p]",standby ? "standby" : "active",this);
    m_standby = standby;
}

void MGCPChan::endTransaction(int code, const NamedList* params, MimeSdpBody* sdp)
{
    Lock mylock(s_mutex);
    MGCPTransaction* tr = m_tr;
    m_tr = 0;
    if (tr) {
	tr->userData(0);
	mylock.drop();
	if (!tr->msgResponse()) {
	    Debug(this,DebugInfo,"Finishing transaction %p with code %d [%p]",tr,code,this);
	    tr->setResponse(code,params,sdp);
	    sdp = 0;
	}
    }
    TelEngine::destruct(sdp);
}

void MGCPChan::mediaChanged(const SDPMedia& media)
{
    SDPSession::mediaChanged(media);
    m_stats.clear();
    if (m_started && media.id() && media.transport()) {
	Message m("chan.rtp");
	m.addParam("rtpid",media.id());
	m.addParam("media",media);
	m.addParam("transport",media.transport());
	m.addParam("terminate",String::boolText(true));
	m.addParam("mgcp_allowed",String::boolText(false));
	Engine::dispatch(m);
	m_stats = m.getValue(YSTRING("stats"));
    }
}

// method called for each event requesting notification
bool MGCPChan::reqNotify(String& evt)
{
    Debug(this,DebugStub,"MGCPChan::reqNotify('%s') [%p]",evt.c_str(),this);
    return false;
}

// method called for each signal request
bool MGCPChan::setSignal(String& req)
{
    Debug(this,DebugStub,"MGCPChan::setSignal('%s') [%p]",req.c_str(),this);
    return false;
}

void MGCPChan::callAccept(Message& msg)
{
    NamedList params("");
    params.addParam("I",address());
    if (s_cluster || m_standby)
	params.addParam("x-standby",String::boolText(m_standby));
    MimeSdpBody* sdp = 0;
    if (!m_isRtp) {
	sdp = createRtpSDP(true);
	if (sdp) {
	    m_started = true;
	    params.addParam("M","sendrecv");
	}
	else {
	    // this address is usd just as a hint
	    const String& addr = msg["rtp_remoteip"];
	    if (addr)
		sdp = createRtpSDP(addr,msg);
	    params.addParam("M","inactive");
	}
    }
    endTransaction(200,&params,sdp);
}

bool MGCPChan::msgTone(Message& msg, const char* tone)
{
    if (null(tone))
	return false;
    if (!(m_connEp && m_addr.valid()))
	return false;
    MGCPMessage* mm = new MGCPMessage(s_engine,"NTFY",m_connEp);
    String tmp;
    while (char c = *tone++) {
	if (tmp)
	    tmp << ",";
	tmp << "D/" << c;
    }
    mm->params.addParam("X",m_ntfyId,false);
    mm->params.setParam("O",tmp);
    return s_engine->sendCommand(mm,m_addr) != 0;
}

bool MGCPChan::processEvent(MGCPTransaction* tr, MGCPMessage* mm)
{
    Debug(this,DebugInfo,"MGCPChan::processEvent(%p,%p) [%p]",tr,mm,this);
    if (!mm) {
	s_mutex.lock();
	if (m_tr == tr) {
	    Debug(this,DebugInfo,"Clearing transaction %p [%p]",tr,this);
	    m_tr = 0;
	    tr->userData(0);
	}
	s_mutex.unlock();
	return true;
    }
    if (!(m_tr || tr->userData())) {
	Debug(this,DebugInfo,"Acquiring transaction %p [%p]",tr,this);
	m_tr = tr;
	tr->userData(m_this);
    }
    NamedList params("");
    params.addParam("I",address());
    if (s_cluster || m_standby)
	params.addParam("x-standby",String::boolText(m_standby));
    if (mm->name() == YSTRING("DLCX")) {
	disconnect();
	status("deleted");
	setMedia(0);
	clearEndpoint();
	m_address.clear();
	m_callId.clear();
	params.addParam("P",m_stats,false);
	m_stats.clear();
	tr->setResponse(250,&params);
	return true;
    }
    if (mm->name() == YSTRING("MDCX")) {
	NamedString* param = mm->params.getParam(YSTRING("z2"));
	if (param) {
	    // native connect requested
	    RefPointer<MGCPChan> chan2 = splugin.findConn(*param,MGCPChan::ConnId);
	    if (!chan2) {
		tr->setResponse(515); // no connection
		return true;
	    }
	    if (!connect(chan2,mm->params.getValue(YSTRING("x-reason"),"bridged"))) {
		tr->setResponse(400); // unspecified error
		return true;
	    }
	}
	param = mm->params.getParam(YSTRING("x"));
	if (param)
	    m_ntfyId = *param;
	rqntParams(mm);
	MimeSdpBody* sdp = 0;
	if (m_isRtp) {
	    Message m("chan.rtp");
	    m.addParam("mgcp_allowed",String::boolText(false));
	    copyRtpParams(m,mm->params);
	    if (m_rtpId)
		m.setParam("rtpid",m_rtpId);
	    m.userData(this);
	    if (Engine::dispatch(m)) {
		copyRename(params,"x-localip",m,"localip");
		copyRename(params,"x-localport",m,"localport");
		m_rtpId = m.getValue(YSTRING("rtpid"),m_rtpId);
	    }
	}
	else {
	    sdp = static_cast<MimeSdpBody*>(mm->sdp[0]);
	    if (sdp) {
		String addr;
		ObjList* lst = splugin.parser().parse(sdp,addr);
		sdp = 0;
		if (lst) {
		    if (m_rtpAddr != addr) {
			m_rtpAddr = addr;
			Debug(this,DebugAll,"New RTP addr '%s'",m_rtpAddr.c_str());
			// clear all data endpoints - createRtpSDP will build new ones
			if (!s_rtp_preserve)
			    clearEndpoint();
		    }
		    setMedia(lst);
		    sdp = createRtpSDP(true);
		    m_started = true;
		}
	    }
	}
	tr->setResponse(200,&params,sdp);
	return true;
    }
    if (mm->name() == YSTRING("AUCX")) {
	tr->setResponse(200,&params);
	return true;
    }
    if (mm->name() == YSTRING("RQNT")) {
	tr->setResponse(rqntParams(mm) ? 200 : 538,&params);
	return true;
    }
    return false;
}

bool MGCPChan::rqntParams(const MGCPMessage* mm)
{
    if (!mm)
	return false;
    bool ok = true;
    // what we are requested to notify back
    const NamedString* req = mm->params.getParam(YSTRING("r"));
    if (req) {
	ObjList* lst = req->split(',');
	for (ObjList* item = lst->skipNull(); item; item = item->skipNext())
	    ok = reqNotify(*static_cast<String*>(item->get())) && ok;
	delete lst;
    }
    // what we must signal now
    req = mm->params.getParam(YSTRING("s"));
    if (req) {
	ObjList* lst = req->split(',');
	for (ObjList* item = lst->skipNull(); item; item = item->skipNext())
	    ok = setSignal(*static_cast<String*>(item->get())) && ok;
	delete lst;
    }
    return ok;
}

bool MGCPChan::initialEvent(MGCPTransaction* tr, MGCPMessage* mm, const MGCPEndpointId& id)
{
    Debug(this,DebugInfo,"MGCPChan::initialEvent(%p,%p,'%s') [%p]",
	tr,mm,id.id().c_str(),this);
    m_addr = tr->addr();
    m_connEp = id.id();
    m_callId = mm->params.getValue(YSTRING("c"));
    m_ntfyId = mm->params.getValue(YSTRING("x"));
    rqntParams(mm);

    MimeSdpBody* sdp = static_cast<MimeSdpBody*>(mm->sdp[0]);
    m_isRtp = mm->params.getParam(YSTRING("x-mediatype")) || mm->params.getParam(YSTRING("x-remoteip"));

    Message* m = message(m_isRtp ? "chan.rtp" : "call.route");
    m->addParam("mgcp_allowed",String::boolText(false));
    copyRtpParams(*m,mm->params);
    if (m_isRtp) {
	m->userData(this);
	bool ok = Engine::dispatch(m);
	if (!ok) {
	    delete m;
	    deref();
	    return false;
	}
	NamedList params("");
	params.addParam("I",address());
	if (s_cluster || m_standby)
	    params.addParam("x-standby",String::boolText(m_standby));
	copyRename(params,"x-localip",*m,"localip");
	copyRename(params,"x-localport",*m,"localport");
	m_rtpId = m->getValue(YSTRING("rtpid"));
	delete m;
	tr->setResponse(200,&params);
	DummyCall* dummy = new DummyCall;
	connect(dummy);
	dummy->deref();
	deref();
	return true;
    }
    if (sdp) {
	setMedia(splugin.parser().parse(sdp,m_rtpAddr,m_rtpMedia));
	if (m_rtpMedia) {
	    m_rtpForward = true;
	    m->addParam("rtp_addr",m_rtpAddr);
	    putMedia(*m);
	}
	if (splugin.parser().sdpForward()) {
	    m_rtpForward = true;
	    const DataBlock& raw = sdp->getBody();
	    String tmp((const char*)raw.data(),raw.length());
	    m->addParam("sdp_raw",tmp);
	}
    }
    // TODO: Handle the L: parameters if SDP is not set
    m_tr = tr;
    tr->userData(m_this);
    m->addParam("called",id.id());
    if (startRouter(m)) {
	tr->sendProvisional();
	return true;
    }
    return false;
}

void MGCPChan::copyRtpParams(NamedList& dest, const NamedList& src)
{
    copyRename(dest,"transport",src,"x-transport");
    copyRename(dest,"media",src,"x-media");
    copyRename(dest,"localip",src,"x-localip");
    copyRename(dest,"localport",src,"x-localport");
    copyRename(dest,"remoteip",src,"x-remoteip");
    copyRename(dest,"remoteport",src,"x-remoteport");
    copyRename(dest,"payload",src,"x-payload");
    copyRename(dest,"evpayload",src,"x-evpayload");
    copyRename(dest,"format",src,"x-format");
    copyRename(dest,"direction",src,"x-direction");
    copyRename(dest,"ssrc",src,"x-ssrc");
    copyRename(dest,"drillhole",src,"x-drillhole");
    copyRename(dest,"autoaddr",src,"x-autoaddr");
    copyRename(dest,"anyssrc",src,"x-anyssrc");
}


MGCPPlugin::MGCPPlugin()
    : Driver("mgcpgw","misc"),
      m_parser("mgcpgw","Gateway")
{
    Output("Loaded module MGCP-GW");
    m_parser.debugChain(this);
}

MGCPPlugin::~MGCPPlugin()
{
    Output("Unloading module MGCP-GW");
    delete s_engine;
}

bool MGCPPlugin::msgExecute(Message& msg, String& dest)
{
    Debug(this,DebugWarn,"Received execute request for gateway '%s'",dest.c_str());
    return false;
}

RefPointer<MGCPChan> MGCPPlugin::findConn(const String* id, MGCPChan::IdType type)
{
    if (!id || id->null())
	return 0;
    Lock lock(this);
    for (ObjList* l = channels().skipNull(); l; l = l->skipNext()) {
	MGCPChan* c = static_cast<MGCPChan*>(l->get());
	if (c->getId(type) == *id)
	    return c;
    }
    return 0;
}

void MGCPPlugin::activate(bool standby)
{
    lock();
    s_cluster = true;
    ListIterator iter(channels());
    while (GenObject* obj = iter.get()) {
	RefPointer<MGCPChan> chan = static_cast<MGCPChan*>(obj);
	if (chan) {
	    unlock();
	    chan->activate(standby);
	    lock();
	}
    }
    unlock();
}

void MGCPPlugin::initialize()
{
    Output("Initializing module MGCP Gateway");
    Configuration cfg(Engine::configFile("mgcpgw"));
    setup();
    NamedList* sect = cfg.getSection(YSTRING("engine"));
    if (s_engine && sect)
	s_engine->initialize(*sect);
    while (!s_engine) {
	if (!(sect && sect->getBoolValue(YSTRING("enabled"),true)))
	    break;
	s_started = Time::secNow();
	s_standby = cfg.getBoolValue("general","standby",false);
	s_cluster = s_standby || cfg.getBoolValue("general","cluster",false);
	s_engine = new YMGCPEngine(sect);
	s_engine->debugChain(this);
	int n = cfg.sections();
	for (int i = 0; i < n; i++) {
	    sect = cfg.getSection(i);
	    if (!sect)
		continue;
	    String name(*sect);
	    if (name.startSkip("ep") && name) {
		MGCPEndpoint* ep = new MGCPEndpoint(
		    s_engine,
		    sect->getValue(YSTRING("local_user"),name),
		    sect->getValue(YSTRING("local_host"),s_engine->address().host()),
		    sect->getIntValue(YSTRING("local_port"))
		);
		MGCPEpInfo* ca = ep->append(0,
		    sect->getValue(YSTRING("remote_host")),
		    sect->getIntValue(YSTRING("remote_port"),0)
		);
		if (ca) {
		    if (sect->getBoolValue("announce",true)) {
			MGCPMessage* mm = new MGCPMessage(s_engine,"RSIP",ep->toString());
			mm->params.addParam("RM","restart");
			if (s_cluster) {
			    mm->params.addParam("x-standby",String::boolText(s_standby));
			    mm->params.addParam("x-started",s_started);
			}
			s_engine->sendCommand(mm,ca->address());
		    }
		}
		else
		    Debug(this,DebugWarn,"Could not set remote endpoint for '%s'",
			name.c_str());
	    }
	}
    }
    m_parser.initialize(cfg.getSection("codecs"),cfg.getSection("hacks"),
	cfg.getSection("general"));
    s_rtp_preserve = cfg.getBoolValue("hacks","ignore_sdp_addr",false);
}

bool MGCPPlugin::received(Message& msg, int id)
{
    switch (id) {
	case Control:
	    if (handleControl(msg))
		return true;
	    break;
    }
    return Driver::received(msg,id);
}

bool MGCPPlugin::commandComplete(Message& msg, const String& partLine,
    const String& partWord)
{
    if (partLine == YSTRING("control")) {
	if (s_engine)
	    s_engine->completeControl(String::empty(),partWord,msg.retValue());
    }
    else {
	String tmp = partLine;
	if (tmp.startSkip("control")) {
	    if (s_engine)
		s_engine->completeControl(tmp,partWord,msg.retValue());
	}
    }
    return Driver::commandComplete(msg,partLine,partWord);
}

bool MGCPPlugin::handleControl(Message& msg)
{
    const String& comp = msg[YSTRING("component")];
    bool retVal = false;
    if (s_engine && s_engine->handleControl(comp,msg,retVal))
	return retVal;
    return false;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
