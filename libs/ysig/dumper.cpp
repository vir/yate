/**
 * dumper.cpp
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

SignallingDumper::SignallingDumper(Type type)
    : m_type(type), m_output(0)
{
}

SignallingDumper::~SignallingDumper()
{
    terminate();
}

void SignallingDumper::setStream(Stream* stream)
{
    if (stream == m_output)
	return;
    Stream* tmp = m_output;
    m_output = stream;
    head();
    delete tmp;
}

// Check if dumper is active
bool SignallingDumper::active() const
{
    return m_output && m_output->valid();
}

// Close the output stream
void SignallingDumper::terminate()
{
    setStream();
}

// Dump the actual data
bool SignallingDumper::dump(void* buf, unsigned int len, bool sent, int link)
{
    if (!(active() && buf && len))
	return false;
    if (m_type == Raw) {
	int wr = m_output->writeData(buf,len);
	return (wr == (int)len);
    }
    else if (m_type == Hexa) {
	String str;
	str.hexify(buf,len);
	str = "0 " + str + "\n";
	int wr = m_output->writeData(str);
	return (wr == (int)str.length());
    }
    Time t;
    struct timeval tv;
    u_int32_t hdr[4];
    t.toTimeval(&tv);
    DataBlock hdr2;
    if (m_type == Hdlc) {
	// add LAPD pseudoheader
	hdr2.assign(0,16);
	unsigned char* ptr2 = (unsigned char*)hdr2.data();
	ptr2[6] = sent ? 1 : 0;
	ptr2[14] = 0x00;
	ptr2[15] = 0x30;
    }
    hdr[0] = tv.tv_sec;
    hdr[1] = tv.tv_usec;
    hdr[2] = len + hdr2.length();
    hdr[3] = hdr[2];
    DataBlock blk(hdr,sizeof(hdr));
    blk += hdr2;
    DataBlock dat(buf,len,false);
    blk += dat;
    dat.clear(false);
    int wr = m_output->writeData(blk);
    return (wr == (int)blk.length());
}

// Write whatever header the format needs
void SignallingDumper::head()
{
    if (!active())
	return;
    if (m_type == Raw || m_type == Hexa)
	return;
    u_int32_t hdr[6];
    hdr[0] = 0xa1b2c3d4; // libpcap magic
    // FIXME: handle big endian
    hdr[1] = 0x00040002; // version lo, hi
    hdr[2] = 0; // offset from GMT
    hdr[3] = 0; // timestamp accuracy
    hdr[4] = 65535; // rather arbitrary snaplen
    switch (m_type) {
	case Hdlc:
	    hdr[5] = 177; // DLT_LINUX_LAPD
	    break;
	case Mtp2:
	    hdr[5] = 140; // DLT_MTP2
	    break;
	case Mtp3:
	    hdr[5] = 141; // DLT_MTP3
	    break;
	case Sccp:
	    hdr[5] = 142; // DLT_SCCP
	    break;
	default:
	    // compiler, please shut up
	    break;
    }
    m_output->writeData(hdr,sizeof(hdr));
}

// Create a dumper
SignallingDumper* SignallingDumper::create(DebugEnabler* dbg, const char* filename, Type type,
	bool create, bool append)
{
    if (!filename)
	return 0;
    SignallingDumper* dumper = 0;
    File* f = new File;
    if (f->openPath(filename,true,false,create,append,true)) {
	dumper = new SignallingDumper(type);
	dumper->setStream(f);
    }
    else {
	Debug(dbg,DebugWarn,"Failed to create dumper '%s'",filename);
	delete f;
    }
    return dumper;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
