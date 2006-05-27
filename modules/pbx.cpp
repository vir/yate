/**
 * pbx.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Basic PBX message handlers
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

#include <yatephone.h>

using namespace TelEngine;
namespace { // anonymous

class ConnHandler : public MessageHandler
{
public:
    ConnHandler()
	: MessageHandler("chan.connect",90)
	{ }
    virtual bool received(Message &msg);
};
			

class PbxPlugin : public Plugin
{
public:
    PbxPlugin();
    virtual ~PbxPlugin();
    virtual void initialize();
    bool m_first;
};

// Utility function to get a pointer to a call endpoint (or its peer) by id
static CallEndpoint* locateChan(const String& id, bool peer = false)
{
    if (id.null())
	return 0;
    Message m("chan.locate");
    m.addParam("id",id);
    if (!Engine::dispatch(m))
	return 0;
    CallEndpoint* ce = static_cast<CallEndpoint*>(m.userObject("CallEndpoint"));
    if (!ce)
	return 0;
    return peer ? ce->getPeer() : ce;
}


bool ConnHandler::received(Message &msg)
{
    RefPointer<CallEndpoint> c1(locateChan(msg.getValue("id"),msg.getBoolValue("id_peer")));
    RefPointer<CallEndpoint> c2(locateChan(msg.getValue("targetid"),msg.getBoolValue("targetid_peer")));
    if (!(c1 && c2))
	return false;
    return c1->connect(c2,msg.getValue("reason"));
}


PbxPlugin::PbxPlugin()
    : Plugin("PBX"), m_first(true)
{
    Output("Loaded module PBX");
}

PbxPlugin::~PbxPlugin()
{
    Output("Unloading module PBX");
}

void PbxPlugin::initialize()
{
    if (m_first) {
	m_first = false;
	Engine::install(new ConnHandler);
    }
}

INIT_PLUGIN(PbxPlugin);

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
