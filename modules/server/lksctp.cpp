/**
 * lksctp.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * SCTP sockets provider based on Linux Kernel SCTP
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2009-2010 Null Team
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
#include <netinet/sctp.h>

using namespace TelEngine;
namespace { // anonymous

class LKSocket;
class LKModule;
class LKHandler;

class LKSocket : public SctpSocket
{
public:
    LKSocket();
    LKSocket(SOCKET fd);
    virtual ~LKSocket();
    virtual bool bindx(ObjList& addrs);
    virtual bool connectx(ObjList& addrs);
    virtual int sendMsg(const void* buf, int length, int stream, int& flags);
    virtual int recvMsg(void* buf, int length, SocketAddr& addr, int& stream, int& flags);
    virtual Socket* accept(SocketAddr& addr);
    virtual bool setStreams(int inbound, int outbound);
    virtual bool getStreams(int& in, int& out);
    virtual bool subscribeEvents();
    virtual bool setPayload(u_int32_t payload)
	{ m_payload = payload; return true; }
    virtual int sendTo(void* buf, int buflen, int stream, SocketAddr& addr, int flags);
    virtual bool setParams(const NamedList& params);
    virtual bool valid() const;
    bool sctpDown(void* buf);
    bool sctpUp(void* buf);
    bool alive() const;
private:
    int m_inbound;
    int m_outbound;
    u_int32_t m_payload;
    sctp_assoc_t m_assocId;
};

class LKHandler : public MessageHandler
{
public:
    LKHandler() : MessageHandler("socket.sctp") { }
    virtual bool received(Message &msg);
};

class LKModule : public Module
{
public:
    LKModule();
    ~LKModule();
    virtual void initialize();
    virtual void statusParams(String& str);
private:
    bool m_init;
};

static LKModule plugin;
unsigned int s_count = 0;
const char* s_mutexName = "LKSctpCounter";
Mutex s_countMutex(true,s_mutexName);

/**
 * class LKSocket
 */

LKSocket::LKSocket()
    : m_payload(0)
{
    XDebug(&plugin,DebugAll,"Creating LKSocket [%p]",this);
    Lock lock(s_countMutex);
    s_count++;
}

LKSocket::LKSocket(SOCKET fd)
    : SctpSocket(fd),
      m_payload(0)
{
    XDebug(&plugin,DebugAll,"Creating LKSocket [%p]",this);
    Lock lock(s_countMutex);
    s_count++;
}

LKSocket::~LKSocket()
{
    XDebug(&plugin,DebugAll,"Destroying LKSocket [%p]",this);
    Lock lock(s_countMutex);
    s_count--;
}

bool LKSocket::bindx(ObjList& addresses)
{
    struct sockaddr addr[addresses.count()];
    int i = 0;
    for (ObjList* o = addresses.skipNull();o;o = o->skipNext()) {
	SocketAddr* a = static_cast<SocketAddr*>(o->get());
	addr[i++] = *(a->address());
    }
    int error = sctp_bindx(handle(),addr,addresses.count(),SCTP_BINDX_ADD_ADDR);
    return (error >= 0);
}

bool LKSocket::connectx(ObjList& addresses)
{
    struct sockaddr addr[addresses.count()];
    int i = 0;
    for (ObjList* o = addresses.skipNull();o;o = o->skipNext()) {
	SocketAddr* a = static_cast<SocketAddr*>(o->get());
	addr[i++] = *(a->address());
    }
#ifdef HAVE_SCTP_CONNECTX_4
    int error = sctp_connectx(handle(),addr,addresses.count(),NULL);
#else
    int error = sctp_connectx(handle(),addr,addresses.count());
#endif
    return (error >= 0);
}

Socket* LKSocket::accept(SocketAddr& addr)
{
    struct sockaddr address;
    socklen_t len = 0;
    SOCKET sock = acceptHandle(&address,&len);
    LKSocket* ret = (sock == invalidHandle()) ? 0 : new LKSocket(sock);
    if (ret)
	addr.assign(&address,len);
    return ret;
}

int LKSocket::recvMsg(void* buf, int length, SocketAddr& addr, int& stream, int& flags)
{
    sctp_sndrcvinfo sri;
    memset(&sri,0,sizeof(sri));
    struct sockaddr address;
    socklen_t len = 0;
    int flag = 0;
    int r = sctp_recvmsg(handle(),buf,length,&address,&len,&sri,&flag);
    addr.assign(&address,len);
    if (flag & MSG_NOTIFICATION) {
	if (sctpDown(buf)) {
	    flags = 1;
	}
	else if (sctpUp(buf))
	    flags = 2;
	else
	    flags = 0;
	r = -1;
    }
    stream = sri.sinfo_stream;
    return r;
}

int LKSocket::sendMsg(const void* buf, int length, int stream, int& flags)
{
    sctp_sndrcvinfo sri;
    memset(&sri,0,sizeof(sri));
    sri.sinfo_stream = stream;
    sri.sinfo_ppid = htonl(m_payload);
    int r = sctp_send(handle(),buf,length,&sri,flags);
    return r;
}

int LKSocket::sendTo(void* buf, int buflen, int stream, SocketAddr& addr, int flags)
{
    return sctp_sendmsg(handle(),buf,buflen,addr.address(),addr.length(),
	htonl(m_payload),flags,stream,0,0);
}

bool LKSocket::setStreams(int inbound, int outbound)
{
    sctp_initmsg initMsg;
    memset(&initMsg,0,sizeof(initMsg));
    initMsg.sinit_max_instreams = inbound;
    initMsg.sinit_num_ostreams = outbound;
    if (!setOption(IPPROTO_SCTP,SCTP_INITMSG,&initMsg,sizeof(initMsg))) {
	DDebug(&plugin,DebugNote,"Unable to set streams number. Error: %s",strerror(errno));
	return false;
    }
    return true;
}

bool LKSocket::subscribeEvents()
{
    struct sctp_event_subscribe events;
    bzero(&events, sizeof(events));
    events.sctp_data_io_event = 1;
    events.sctp_send_failure_event = 1;
    events.sctp_peer_error_event = 1;
    events.sctp_shutdown_event = 1;
    events.sctp_association_event = 1;
    return setOption(IPPROTO_SCTP,SCTP_EVENTS, &events, sizeof(events));
}

bool LKSocket::setParams(const NamedList& params)
{
    bool ret = false;
    bool aux = false;
    if (params.getParam(YSTRING("rto_initial")) || params.getParam(YSTRING("rto_max")) ||
	    params.getParam(YSTRING("rto_min"))) {
	struct sctp_rtoinfo rto;
	bzero(&rto, sizeof(rto));
	rto.srto_initial = params.getIntValue("rto_initial",0);
	rto.srto_max = params.getIntValue("rto_max",0);
	rto.srto_min = params.getIntValue("rto_min",0);
	aux = setOption(IPPROTO_SCTP,SCTP_RTOINFO, &rto, sizeof(rto));
	if (!aux)
	    Debug(&plugin,DebugNote,"Failed to set SCTP RTO params! Reason: %s",strerror(errno));
	ret |= aux;
    }
    struct sctp_paddrparams paddr_params;
    bzero(&paddr_params, sizeof(paddr_params));
    if (params.getParam(YSTRING("hb_interval")))
	paddr_params.spp_hbinterval = params.getIntValue(YSTRING("hb_interval"),0);
    if (params.getParam(YSTRING("max_retrans")))
	paddr_params.spp_pathmaxrxt = params.getIntValue(YSTRING("max_retrans"),0);
    bool hbEnabled = params.getBoolValue(YSTRING("hb_enabled"),true);
    paddr_params.spp_flags |= hbEnabled ? SPP_HB_ENABLE : SPP_HB_DISABLE;
    if (params.getParam(YSTRING("hb_0")))
#ifdef SPP_HB_TIME_IS_ZERO
	paddr_params.spp_flags |= SPP_HB_TIME_IS_ZERO;
#else
	Debug(&plugin,DebugNote,"HeartBeat 0 is not available");
#endif
    if (params.getParam(YSTRING("hb_demand")))
#ifdef SPP_HB_DEMAND
	paddr_params.spp_flags |= SPP_HB_DEMAND;
#else
	Debug(&plugin,DebugNote,"HeartBeat demand is not available");
#endif
    aux = setOption(IPPROTO_SCTP,SCTP_PEER_ADDR_PARAMS, &paddr_params, sizeof(paddr_params));
    ret |= aux;
    if (!aux)
	Debug(&plugin,DebugNote,"Failed to set SCTP paddr params! Reason: %s",strerror(errno));
#ifdef SCTP_DELAYED_ACK_TIME
#ifdef HAVE_SACK_INFO_STRUCT
    struct sctp_sack_info sack_info;
    bzero(&sack_info, sizeof(sack_info));
    if (params.getParam(YSTRING("sack_delay"))) {
	sack_info.sack_delay = params.getIntValue(YSTRING("sack_delay"));
	if (sack_info.sack_delay > 500)
	    sack_info.sack_delay = 500;
    }
    if (params.getParam(YSTRING("sack_freq")))
	sack_info.sack_freq = params.getIntValue(YSTRING("sack_freq"));
    aux = setOption(IPPROTO_SCTP,SCTP_DELAYED_ACK_TIME, &sack_info, sizeof(sack_info));
    ret |= aux;
    if (!aux)
	Debug(&plugin,DebugNote,"Failed to set SCTP sack params! Reason: %s",strerror(errno));
#elif HAVE_ASSOC_VALUE_STRUCT
    struct sctp_assoc_value sassoc_value;
    bzero(&sassoc_value, sizeof(sassoc_value));
    if (params.getParam(YSTRING("sack_delay"))) {
	sassoc_value.assoc_value = params.getIntValue(YSTRING("sack_delay"));
	if (sassoc_value.assoc_value > 500)
	    sassoc_value.assoc_value = 500;
    }
    if (params.getParam(YSTRING("sack_freq")))
	Debug(&plugin,DebugConf,"Unable to set sack_freq param! sack_info struct is missing!");
    aux = setOption(IPPROTO_SCTP,SCTP_DELAYED_ACK_TIME, &sassoc_value, sizeof(sassoc_value));
    ret |= aux;
    if (!aux)
	Debug(&plugin,DebugNote,"Failed to set SCTP sack params! Reason: %s",strerror(errno));
#else // HAVE_SACK_INFO_STRUCT
    Debug(&plugin,DebugConf,"SCTP delayed ack time is unavailable no struct present!!");
#endif
#else // SCTP_DELAYED_ACK_TIME
    Debug(&plugin,DebugConf,"SCTP delayed ack time is unavailable");
#endif
    aux = Socket::setParams(params);
    return ret || aux;
}

bool LKSocket::valid() const
{
    if (!Socket::valid())
	return false;
    return alive();
}

bool LKSocket::alive() const
{
    struct sctp_status status;
    int statusLen = sizeof(status);
    bzero(&status, statusLen);
    socklen_t len = statusLen;
    int ret = sctp_opt_info(handle(),m_assocId,SCTP_STATUS, &status, &len);
    if (ret < 0)
	return true;
    bool localUp = true;
    switch (status.sstat_state) {
	case SCTP_CLOSED:
	case SCTP_SHUTDOWN_PENDING:
	case SCTP_SHUTDOWN_SENT:
	case SCTP_SHUTDOWN_RECEIVED:
	case SCTP_SHUTDOWN_ACK_SENT:
	    localUp = false;
    }
    localUp = localUp && status.sstat_primary.spinfo_state == SCTP_ACTIVE;
    if (localUp)
	return true;
#ifdef DEBUG
#define MAKE_CASE(x,y) case SCTP_##x: \
	    Debug(&plugin,DebugNote,"%s sctp status : SCTP_%s",#y,#x); \
	    break;
    switch (status.sstat_primary.spinfo_state) {
	MAKE_CASE(ACTIVE,Remote);
	MAKE_CASE(INACTIVE,Remote);
    }
    switch (status.sstat_state) {
	MAKE_CASE(EMPTY,Local);
	MAKE_CASE(CLOSED,Local);
	MAKE_CASE(COOKIE_WAIT,Local);
	MAKE_CASE(COOKIE_ECHOED,Local);
	MAKE_CASE(ESTABLISHED,Local);
	MAKE_CASE(SHUTDOWN_PENDING,Local);
	MAKE_CASE(SHUTDOWN_SENT,Local);
	MAKE_CASE(SHUTDOWN_RECEIVED,Local);
	MAKE_CASE(SHUTDOWN_ACK_SENT,Local);
	default:
	    Debug(&plugin,DebugNote,"Unknown SCTP local State : 0x0%x",status.sstat_state);
    }
#undef MAKE_CASE
#endif
    return false;
}

bool LKSocket::getStreams(int& in, int& out)
{
    sctp_status status;
    memset(&status,0,sizeof(status));
    socklen_t len = sizeof(status);
    if (!getOption(IPPROTO_SCTP,SCTP_STATUS, &status,&len)) {
	DDebug(&plugin,DebugNote,"Unable to find the number of negotiated streams: %s",
	    strerror(errno));
	return false;
    }
    XDebug(&plugin,DebugAll,"Sctp streams inbound = %u , outbound = %u",
	status.sstat_instrms,status.sstat_outstrms);
    m_inbound = status.sstat_instrms;
    m_outbound = status.sstat_outstrms;
    return true;
}

bool LKSocket::sctpDown(void* buf)
{
    union sctp_notification *sn = (union sctp_notification *)buf;
    DDebug(&plugin,DebugInfo,"Event: 0x%X [%p]",sn->sn_header.sn_type,this);
    switch (sn->sn_header.sn_type) {
	case SCTP_SHUTDOWN_EVENT:
	case SCTP_SEND_FAILED:
	case SCTP_REMOTE_ERROR:
	    return true;
	case SCTP_ASSOC_CHANGE:
	  switch (sn->sn_assoc_change.sac_state) {
	      case SCTP_COMM_LOST:
	      case SCTP_SHUTDOWN_COMP:
	      case SCTP_CANT_STR_ASSOC:
	      case SCTP_RESTART:
		  return true;
	  }
    }
    return false;
}

bool LKSocket::sctpUp(void* buf)
{
    union sctp_notification *sn = (union sctp_notification *)buf;
    if (sn->sn_header.sn_type != SCTP_ASSOC_CHANGE)
	return false;
    switch (sn->sn_assoc_change.sac_state) {
	case SCTP_COMM_UP:
	    m_assocId = sn->sn_assoc_change.sac_assoc_id;
	    return true;
    }
    return false;
}

/**
 * class LKHandler
 */

bool LKHandler::received(Message &msg)
{
    Socket** ppSock = static_cast<Socket**>(msg.userObject("Socket*"));
    int fd = msg.getIntValue("handle",-1);
    *ppSock = new LKSocket(fd);
    return true;
}

/**
 * class LKModule
 */

LKModule::LKModule()
    : Module("lksctp","misc",true),
      m_init(false)
{
    Output("Loading module LKSCTP");
}

LKModule::~LKModule()
{
    Output("Unloading module LKSCTP");
}

void LKModule::initialize()
{
    if (!m_init) {
	Output("Initialize module LKSCTP");
	m_init = true;
	Engine::install(new LKHandler());
    }
    setup();
}

void LKModule::statusParams(String& str)
{
    str << "count=" << s_count;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
