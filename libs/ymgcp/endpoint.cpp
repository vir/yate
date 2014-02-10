/**
 * endpoint.cpp
 * Yet Another MGCP Stack
 * This file is part of the YATE Project http://YATE.null.ro
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

#include <yatemgcp.h>

using namespace TelEngine;

/**
 * MGCPEndpoint
 */
// Construct the id. Append itself to the engine's list
MGCPEndpoint::MGCPEndpoint(MGCPEngine* engine, const char* user,
	const char* host, int port, bool addPort)
    : MGCPEndpointId(user,host,port,addPort),
      Mutex(false,"MGCPEndpoint"),
      m_engine(engine)
{
    if (!m_engine) {
	Debug(DebugNote,"Can't construct endpoint without engine [%p]",this);
	return;
    }
    m_engine->attach(this);
}

// Remove itself from engine's list
MGCPEndpoint::~MGCPEndpoint()
{
    if (m_engine)
	m_engine->detach(this);
}

// Append info about a remote endpoint controlled by or controlling this endpoint.
// If the engine owning this endpoint is an MGCP gateway, only 1 remote peer (Call Agent) is allowed
MGCPEpInfo* MGCPEndpoint::append(const char* endpoint, const char* host, int port)
{
    if (!m_engine || (m_engine->gateway() && m_remote.count() >= 1))
	return 0;

    if (!endpoint)
	endpoint = user();
    bool addPort = (port >= 0);
    if (port < -1)
	port = -port;
    else if (port <= 0)
	port = m_engine->defaultPort(!m_engine->gateway());
    MGCPEpInfo* ep = new MGCPEpInfo(endpoint,host,port,addPort);
    if (!ep->valid() || find(ep->id()))
	TelEngine::destruct(ep);
    else
	m_remote.append(ep);
    return ep;
}

//  Find the info object associated with a remote peer
MGCPEpInfo* MGCPEndpoint::find(const String& epId)
{
    Lock lock(this);
    return static_cast<MGCPEpInfo*>(m_remote[epId]);
}

//  Find the info object associated with a remote peer by alias name
MGCPEpInfo* MGCPEndpoint::findAlias(const String& alias)
{
    if (alias.null())
	return 0;
    Lock lock(this);
    for (ObjList* o = m_remote.skipNull(); o; o = o->skipNext()) {
	MGCPEpInfo* info = static_cast<MGCPEpInfo*>(o->get());
	if (alias == info->alias)
	    return info;
    }
    return 0;
}

// Find the info object associated with an unique remote peer
MGCPEpInfo* MGCPEndpoint::peer()
{
    return (m_remote.count() == 1) ? static_cast<MGCPEpInfo*>(m_remote.get()) : 0;
}

/**
 * MGCPEndpointId
 */
// Set this endpoint id. Convert it to lower case
void MGCPEndpointId::set(const char* endpoint, const char* host, int port, bool addPort)
{
    m_id = "";
    m_endpoint = endpoint;
    m_endpoint.toLower();
    m_host = host;
    m_host.toLower();
    m_port = port;
    m_id << m_endpoint << "@" << m_host;
    if (m_port && addPort)
	m_id << ":" << m_port;
}


// Resolve the Ep Info host on first demand
const SocketAddr& MGCPEpInfo::address()
{
    if (m_resolve) {
	m_resolve = false;
	DDebug(DebugInfo,"Resolving MGCP host '%s'",host().c_str());
	m_address.host(host());
    }
    return m_address;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
