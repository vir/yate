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
    : m_trans(None), m_socket(0)
{
}

SIGTRAN::~SIGTRAN()
{
    terminate();
}

// Terminate the transport
void SIGTRAN::terminate()
{
    Socket* tmp = m_socket;
    m_trans = None;
    m_socket = 0;
    delete tmp;
}

// Check if a stream in the transport is connected
bool SIGTRAN::connected(int streamId) const
{
    if ((m_trans == None) || !(m_socket && m_socket->valid()))
	return false;
    Debug(DebugStub,"Please implement SIGTRAN::connected()");
    return true;
}

// Attach a socket to the SIGTRAN instance
bool SIGTRAN::attach(Socket* socket, Transport trans)
{
    terminate();
    if ((trans == None) || !socket)
	return false;
    m_socket = socket;
    m_trans = trans;
    return true;
}

// Build the common header and transmit a message to the network
bool SIGTRAN::transmitMSG(unsigned char msgVersion, unsigned char msgClass,
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

// Generic message transmission method, can be overriden to improve performance
bool SIGTRAN::transmitMSG(const DataBlock& header, const DataBlock& msg, int streamId)
{
    if (!connected(streamId))
	return false;

    DataBlock tmp(header);
    tmp += msg;
    int len = tmp.length();
    return m_socket->send(tmp.data(),len) == len;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
