/**
 * layer2.cpp
 * Yet Another SS7 Stack
 * This file is part of the YATE Project http://YATE.null.ro 
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2006 Null Team
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

#include "yatess7.h"


using namespace TelEngine;

SS7MSU::~SS7MSU()
{
}

bool SS7MSU::valid() const
{
    return (3 < length()) && (length() < 273);
}


unsigned int SS7Layer2::status() const
{
    return ProcessorOutage;
}

bool SS7Layer2::control(Operation oper, NamedList* params)
{
    return false;
}


SS7MTP2::SS7MTP2(unsigned int status)
    : Mutex(false), m_status(status),
      m_bsn(0), m_fsn(0), m_bib(false), m_fib(false)
{
}

unsigned int SS7MTP2::status() const
{
    return m_status;
}

bool SS7MTP2::control(Operation oper, NamedList* params)
{
    switch (oper) {
	case Pause:
	case Resume:
	case Align:
	    break;
	case Status:
	    return (m_status == NormalAlignment);
	default:
	    return SignallingReceiver::control((SignallingInterface::Operation)oper,params);
    }
}

// Transmit a MSU retaining a copy for retransmissions
bool SS7MTP2::transmitMSU(const SS7MSU& msu)
{
    if (msu.length() < 3) {
	Debug(engine(),DebugMild,"Asked to send MSU of length %u [%p]",
	    msu.length(),this);
	return false;
    }
    XDebug(engine(),DebugAll,"SS7MTP2::transmitMSU(%p) len=%u [%p]",
	&msu,msu.length(),this);
    // if we don't have an attached interface don't bother
    if (!iface())
	return false;

    DataBlock* packet = new DataBlock(0,3);
    *packet += msu;

    // set BSN+BIB, FSN+FIB, LENGTH in the 3 extra bytes
    unsigned char* buf = (unsigned char*)packet->data();
    buf[2] = (msu.length() > 0x3f) ? 0x3f : msu.length() & 0x3f;
    // lock the object so we can safely use member variables
    Lock lock(this);
    buf[0] = m_bib ? m_bsn | 0x80 : m_bsn;
    buf[1] = m_fib ? m_fsn | 0x80 : m_fsn;
    m_fsn = (m_fsn + 1) & 0x7f;
    m_queue.append(packet);
    return transmitPacket(*packet,false);
}

// Decode a received packet into signalling units
bool SS7MTP2::receivedPacket(const DataBlock& packet)
{
    Debug("STUB",DebugWarn,"Please implement SS7MTP2::receivedPacket()");
    if (packet.length() < 3) {
	XDebug(engine(),DebugMild,"Received short packet of length %u [%p]",
	    packet.length(),this)
	return false;
    }
    const unsigned char* buf = (const unsigned char*)packet.data();
    unsigned int len = buf[2] & 0x3f;
    if ((len == 0x3f) && (packet.length() > 0x42))
	len = packet.length() - 3;
    else if (len != (packet.length() - 3)) {
	XDebug(engine(),DebugMild,"Received packet with length indicator %u but length %u [%p]",
	    len,packet.length(),this);
	return false;
    }
    bool ok = true;
    // packet length is valid, check sequence numbers
    unsigned char bsn = buf[0] & 0x7f;
    bool bib = (buf[0] & 0x80) != 0;
    unsigned char fsn = buf[1] & 0x7f;
    bool fib = (buf[1] & 0x80) != 0;

    //TODO: implement Q.703 6.3.1

    switch (len) {
	case 2:
	    processLSSU(buf[3] + (buf[4] << 8));
	    return ok;
	case 1:
	    processLSSU(buf[3]);
	    return ok;
	case 0:
	    processFISU();
	    return ok;
    }
    SS7MSU msu((void*)(buf+3),len,false);
    ok = receivedMSU(msu);
    msu.clear(false);
    return ok;
}

// Process incoming FISU
void SS7MTP2::processFISU()
{
}

// Process incoming LSSU
void SS7MTP2::processLSSU(unsigned int status)
{
    Debug("STUB",DebugWarn,"Please implement SS7MTP2::processLSSU()");
}

// Emit a locally generated LSSU
bool SS7MTP2::transmitLSSU(unsigned int status)
{
    unsigned char buf[5];
    buf[2] = 1;
    buf[3] = status & 0xff;
    status = (status >> 8) & 0xff;
    if (status) {
	// we need 2-byte LSSU to fit
	buf[2] = 2;
	buf[4] = status;
    }
    // lock the object so we can safely use member variables
    lock();
    buf[0] = m_bib ? m_bsn | 0x80 : m_bsn;
    buf[1] = m_fib ? m_fsn | 0x80 : m_fsn;
    DataBlock packet(buf,buf[2]+3,false);
    bool ok = transmitPacket(packet,true);
    unlock();
    packet.clear(false);
    return ok;
}

// Emit a locally generated FISU
bool SS7MTP2::transmitFISU()
{
    unsigned char buf[3];
    buf[2] = 0;
    // lock the object so we can safely use member variables
    lock();
    buf[0] = m_bib ? m_bsn | 0x80 : m_bsn;
    buf[1] = m_fib ? m_fsn | 0x80 : m_fsn;
    DataBlock packet(buf,3,false);
    bool ok = transmitPacket(packet,true);
    unlock();
    packet.clear(false);
    return ok;
}

void SS7MTP2::startAlignment()
{
}

void SS7MTP2::abortAlignment()
{
}

/* vi: set ts=8 sw=4 sts=4 noet: */
