/**
 * layer3.cpp
 * Yet Another SS7 Stack
 * This file is part of the YATE Project http://YATE.null.ro 
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

#include "yatess7.h"


using namespace TelEngine;

void SS7Layer3::attach(SS7L3User* l3user)
{
    if (m_l3user == l3user)
	return;
    Debug(toString(),DebugStub,"Please implement SS7Layer3::attach()");
    m_l3user = l3user;
    if (!l3user)
	return;
    insert(l3user);
    l3user->attach(this);
}


SS7MTP3::SS7MTP3(SS7CodePoint::Type type)
    : SS7Layer3(type)
{
    setName("mtp3");
}

void SS7MTP3::attach(SS7Layer2* link)
{
    if (!link)
	return;
    Debug(toString(),DebugStub,"Please implement SS7MTP3::attach()");
    SignallingComponent::insert(link);
    if (!m_links.find(link))
	m_links.append(new GenPointer<SS7Layer2>(link));
    link->attach(this);
}

bool SS7MTP3::transmitMSU(const SS7MSU& msu, int sls)
{
    Debug(toString(),DebugStub,"Please implement SS7MTP3::transmitMSU(%p,%d) type=%d [%p]",
	&msu,sls,type(),this);
    return false;
}

bool SS7MTP3::receivedMSU(const SS7MSU& msu, SS7Layer2* link)
{
    Debug(toString(),DebugStub,"Please implement SS7MTP3::receivedMSU(%p,%p) type=%d [%p]",
	&msu,link,type(),this);
    unsigned int llen = SS7Label::length(type());
    if (!llen) {
	Debug(toString(),DebugWarn,"Received MSU but codepoint type is unconfigured [%p]",this);
	return false;
    }
    // check MSU length against SIO + label length
    if (msu.length() <= llen) {
	Debug(engine(),DebugMild,"Received short MSU of length %u [%p]",
	    msu.length(),this);
	return false;
    }
    SS7Label label(type(),msu);
#ifdef DEBUG
    if (debugAt(DebugInfo)) {
	String tmp;
	tmp << label << " (" << label.spc().pack(type()) << ":" << label.dpc().pack(type()) << ":" << label.sls() << ")";
	Debug(toString(),DebugInfo,"MSU address: %s",tmp.c_str());
    }
#endif

    return false;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
