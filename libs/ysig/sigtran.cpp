/**
 * sigtran.cpp
 * This file is part of the YATE Project http://YATE.null.ro 
 *
 * Yet Another Signalling Stack - implements the support for SS7, ISDN and PSTN
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

#include "yatesig.h"


using namespace TelEngine;

#define MAKE_NAME(x) { #x, SIGTRAN::x }
static const TokenDict s_classes[] = {
    // this list must be kept in synch with the header
    MAKE_NAME(MGMT),
    MAKE_NAME(TRAN),
    MAKE_NAME(SSNM),
    MAKE_NAME(ASPSM),
    MAKE_NAME(ASPTM),
    MAKE_NAME(QPTM),
    MAKE_NAME(MAUP),
    MAKE_NAME(CLMSG),
    MAKE_NAME(COMSG),
    MAKE_NAME(RKM),
    MAKE_NAME(IIM),
    MAKE_NAME(M2PA),
    { 0, 0 }
};
#undef MAKE_NAME

const TokenDict* SIGTRAN::classNames()
{
    return s_classes;
}

SIGTRAN::SIGTRAN()
    : m_trans(0)
{
}

SIGTRAN::~SIGTRAN()
{
    attach(0);
}

// Check if a stream in the transport is connected
bool SIGTRAN::connected(int streamId) const
{
    m_transMutex.lock();
    RefPointer<SIGTransport> trans = m_trans;
    m_transMutex.unlock();
    return trans && trans->connected(streamId);
}

// Attach a transport to the SIGTRAN instance
void SIGTRAN::attach(SIGTransport* trans)
{
    Lock lock(m_transMutex);
    if (trans == m_trans)
	return;
    if (trans && trans->ref())
	trans = 0;
    SIGTransport* tmp = m_trans;
    m_trans = trans;
    lock.drop();
    if (tmp) {
	tmp->attach(0);
	tmp->destruct();
    }
    if (trans)
	trans->attach(this);
}

// Transmit a SIGTRAN message over the attached transport
bool SIGTRAN::transmitMSG(unsigned char msgVersion, unsigned char msgClass,
    unsigned char msgType, const DataBlock& msg, int streamId) const
{
    m_transMutex.lock();
    RefPointer<SIGTransport> trans = m_trans;
    m_transMutex.unlock();
    return trans && trans->transmitMSG(msgVersion,msgClass,msgType,msg,streamId);
}


// Attach or detach an user adaptation layer
void SIGTransport::attach(SIGTRAN* sigtran)
{
    if (m_sigtran != sigtran) {
	m_sigtran = sigtran;
	attached(sigtran != 0);
    }
}

// Request processing from the adaptation layer
bool SIGTransport::processMSG(unsigned char msgVersion, unsigned char msgClass,
    unsigned char msgType, const DataBlock& msg, int streamId) const
{
    return m_sigtran && m_sigtran->processMSG(msgVersion,msgClass,msgType,msg,streamId);
}

// Build the common header and transmit a message to the network
bool SIGTransport::transmitMSG(unsigned char msgVersion, unsigned char msgClass,
    unsigned char msgType, const DataBlock& msg, int streamId)
{
    if (!connected(streamId))
	return false;

    unsigned char hdr[8];
    unsigned int len = 8 + msg.length();
    hdr[0] = msgVersion;
    hdr[1] = 0;
    hdr[2] = msgClass;
    hdr[3] = msgType;
    hdr[4] = 0xff & (len >> 24);
    hdr[5] = 0xff & (len >> 16);
    hdr[6] = 0xff & (len >> 8);
    hdr[7] = 0xff & len;

    DataBlock header(hdr,8,false);
    bool ok = transmitMSG(header,msg,streamId);
    header.clear(false);
    return ok;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
