/**
 * ysipchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Sip Channel
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

#include <unistd.h>
#include <string.h>


#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

/**
 * we include also the sip stack headers
 */

#include <ysip.h>

using namespace TelEngine;

static Configuration s_cfg;


class SIPHandler : public MessageHandler
{
public:
    SIPHandler(const char *name) : MessageHandler(name) { }
    virtual bool received(Message &msg);
};

class YateUDPParty : public SIPParty
{
public:
    YateUDPParty(int fd,struct sockaddr_in sin);
    ~YateUDPParty();
    virtual void transmit(SIPEvent* event);
protected:
    int m_netfd;
    struct sockaddr_in m_sin;
};

class YateSIPEngine : public SIPEngine
{
};

class YateSIPEndPoint : public Thread
{
public:
    YateSIPEndPoint();
    ~YateSIPEndPoint();
    bool Init(void);
   // YateSIPConnection *findconn(int did);
  //  void terminateall(void);
    void run(void);
    void incoming(SIPEvent* e);
    inline ObjList &calls()
	{ return m_calls; }
private:
    ObjList m_calls;
    int m_localport;
    int m_port;
    int m_netfd;
    YateSIPEngine *m_engine;

};

class SIPPlugin : public Plugin
{
public:
    SIPPlugin();
    ~SIPPlugin();
    virtual void initialize();
private:
    SIPHandler *m_handler;
    YateSIPEndPoint *m_endpoint;
};


YateUDPParty::YateUDPParty(int fd,struct sockaddr_in sin)
    : m_netfd(fd), m_sin(sin)
{
}

YateUDPParty::~YateUDPParty()
{
    m_netfd = -1;
}

void YateUDPParty::transmit(SIPEvent* event)
{
    Debug(DebugAll,"Sending to %s:%d",inet_ntoa(m_sin.sin_addr),ntohs(m_sin.sin_port));
    ::sendto(m_netfd,
	event->getMessage()->getBuffer().data(),
	event->getMessage()->getBuffer().length(),
	0,
	(struct sockaddr *) &m_sin,
	sizeof(m_sin)
    );
}

YateSIPEndPoint::YateSIPEndPoint()
{
    m_netfd = -1;
    Debug(DebugAll,"YateSIPEndPoint::YateSIPEndPoint() [%p]",this);
}

YateSIPEndPoint::~YateSIPEndPoint()
{
    Debug(DebugAll,"YateSIPEndPoint::~YateSIPEndPoint() [%p]",this);
}

bool YateSIPEndPoint::Init ()
{
    /*
     * This part have been taking from libiax after i have lost my sip driver for bayonne
     */
    struct sockaddr_in sin;
    int port = s_cfg.getIntValue("general","port",5060);
    m_localport = port;
    int flags;
    if (m_netfd > -1) {
	Debug(DebugInfo,"Already initialized.");
	return 0;
    }
    m_netfd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (m_netfd < 0) {
	Debug(DebugFail,"Unable to allocate UDP socket\n");
	return -1;
    }
    
    int sinlen = sizeof(sin);
    
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    sin.sin_port = htons((short)port);
    if (::bind(m_netfd, (struct sockaddr *) &sin, sinlen) < 0) {
	Debug(DebugFail,"Unable to bind to preferred port.  Using random one instead.");
    }
    
    if (::getsockname(m_netfd, (struct sockaddr *) &sin, (socklen_t *)&sinlen) < 0) {
	::close(m_netfd);
	m_netfd = -1;
	Debug(DebugFail,"Unable to figure out what I'm bound to.");
    }
    if ((flags = ::fcntl(m_netfd, F_GETFL)) < 0) {
	::close(m_netfd);
	m_netfd = -1;
	Debug(DebugFail,"Unable to retrieve socket flags.");
    }
    if (::fcntl(m_netfd, F_SETFL, flags | O_NONBLOCK) < 0) {
	::close(m_netfd);
	m_netfd = -1;
	Debug(DebugFail,"Unable to set non-blocking mode.");
    }
    port = ntohs(sin.sin_port);
    Debug(DebugInfo,"Started on port %d\n", port);
    m_port = port;
    m_engine = new YateSIPEngine();
    return true;
}

void YateSIPEndPoint::run ()
{
    fd_set fds;
    struct timeval tv;
    int retval;
    char buf[1500];
    struct sockaddr_in sin;
    /* Watch stdin (fd 0) to see when it has input. */
    for (;;)
    {
	FD_ZERO(&fds);
	FD_SET(m_netfd, &fds);
	/* Wait up to 20000 microseconds. */
        tv.tv_sec = 0;
	tv.tv_usec = 20000;

	retval = select(m_netfd+1, &fds, NULL, NULL, &tv);
	if (retval)
	{
	    // we got the dates
	    int sinlen = sizeof(sin);
	    
	    int res = ::recvfrom(m_netfd, buf, sizeof(buf)-1, 0, (struct sockaddr *) &sin,(socklen_t *) &sinlen);
	    if (res < 0) {
		if (errno != EAGAIN) {
		    Debug(DebugFail,"Error on read: %s\n", strerror(errno));
		}
	    } else {
		// we got already the buffer and here we start to do "good" stuff
		buf[res]=0;
		m_engine->addMessage(new YateUDPParty(m_netfd,sin),buf,res);
	    //	Output("res %d buf %s",res,buf);
	    }
	}
//	m_engine->process();
	SIPEvent* e = m_engine->getEvent();
	if (e) {
	    if ((e->getState() == SIPTransaction::Trying) && !e->isOutgoing()) {
		incoming(e);
		delete e;
	    }
	    else
		m_engine->processEvent(e);
	}
    }
}

void YateSIPEndPoint::incoming(SIPEvent* e)
{
    Message *m = new Message("call.preroute");
    m->addParam("driver","sip");
    Engine::dispatch(m);
    *m = "call.route";
    m->retValue().clear();
    if (Engine::dispatch(m) && m->retValue()) {
	e->getTransaction()->setResponse(500, "Server Internal Error");
    }
    else
	e->getTransaction()->setResponse(404, "Not Found");
    delete m;
}

bool SIPHandler::received(Message &msg)
{
    Debug(DebugInfo,msg.getValue("called"));
    return false;
}

SIPPlugin::SIPPlugin()
    : m_handler(0), m_endpoint(0)
{
    Output("Loaded module SIP Channel");
}

SIPPlugin::~SIPPlugin()
{
    Output("Unloading module SIP Channel");
}

void SIPPlugin::initialize()
{
    Output("Initializing module SIP Channel");
    s_cfg = Engine::configFile("sipchan");
    s_cfg.load();
    if (!m_endpoint) {
	m_endpoint = new YateSIPEndPoint();
	if(!(m_endpoint->Init()))
		{
		    delete m_endpoint;
		    m_endpoint = 0;
		    return;
		}
	else 
	    m_endpoint->startup();
    }
    if (!m_handler) {
	m_handler = new SIPHandler("call.execute");
	Engine::install(m_handler);
    }
}

INIT_PLUGIN(SIPPlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
