/**
 * dumper.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Signalling Stack - implements the support for SS7, ISDN and PSTN
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

#include "yatesig.h"
#include <yatephone.h>


using namespace TelEngine;

SignallingDumper::SignallingDumper(Type type, bool network)
    : m_type(type), m_network(network), m_output(0)
{
}

SignallingDumper::~SignallingDumper()
{
    terminate();
}

void SignallingDumper::setStream(Stream* stream, bool writeHeader)
{
    if (stream == m_output)
	return;
    Stream* tmp = m_output;
    m_output = stream;
    if (writeHeader)
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
	str.hexify(buf,len,' ');
	str = "0 " + str + "\r\n";
	int wr = m_output->writeData(str);
	return (wr == (int)str.length());
    }
    Time t;
    struct timeval tv;
    u_int32_t hdr[4];
    t.toTimeval(&tv);
    DataBlock hdr2;
    switch (m_type) {
	case Q931:
	case Q921:
	case Hdlc:
	    {
		// add LAPD pseudoheader - see wiretap/libpcap.c
		hdr2.assign(0,16);
		unsigned char* ptr2 = (unsigned char*)hdr2.data();
		// packet type: outgoing 4, sniffed 3, incoming 0
		ptr2[0] = 0x00;
		ptr2[1] = sent ? 0x04 : 0x00;
		// address: are we the network side?
		ptr2[6] = m_network ? 1 : 0;
		// ETH_P_LAPD
		ptr2[14] = 0x00;
		ptr2[15] = 0x30;
	    }
	    break;
	default:
	    break;
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
	case Q931:
	case Q921:
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

// Create a dumper from file
SignallingDumper* SignallingDumper::create(DebugEnabler* dbg, const char* filename, Type type,
	bool network, bool create, bool append)
{
    if (!filename)
	return 0;
    File* f = new File;
    if (f->openPath(filename,true,false,create,append,true))
	return SignallingDumper::create(f,type,network);
    Debug(dbg,DebugWarn,"Failed to create dumper '%s'",filename);
    delete f;
    return 0;
}

// Create a dumper from stream
SignallingDumper* SignallingDumper::create(Stream* stream, Type type, bool network, bool writeHeader)
{
    if (!stream)
	return 0;
    if (!stream->valid()) {
	delete stream;
	return 0;
    }
    SignallingDumper* dumper = new SignallingDumper(type,network);
    dumper->setStream(stream,writeHeader);
    return dumper;
}


void SignallingDumpable::setDumper(SignallingDumper* dumper)
{
    if (dumper == m_dumper)
	return;
    SignallingDumper* tmp = m_dumper;
    m_dumper = dumper;
    delete tmp;
}

bool SignallingDumpable::setDumper(const String& name, bool create, bool append)
{
    if (name.null())
	setDumper();
    else {
	SignallingDumper::Type type = m_type;
	if (name.endsWith(".raw"))
	    type = SignallingDumper::Raw;
	else if (name.endsWith(".hex") || name.endsWith(".txt"))
	    type = SignallingDumper::Hexa;
	SignallingDumper* dumper = SignallingDumper::create(0,name,type,m_dumpNet,create,append);
	if (dumper)
	    setDumper(dumper);
	else
	    return false;
    }
    return true;
}

bool SignallingDumpable::control(NamedList& params, SignallingComponent* owner)
{
    String* tmp = params.getParam(YSTRING("operation"));
    if (!(tmp && (*tmp == YSTRING("sigdump"))))
	return false;
    tmp = params.getParam(YSTRING("component"));
    if (tmp && *tmp && owner && (owner->toString() != *tmp))
	return false;
    tmp = params.getParam(YSTRING("completion"));
    if (tmp) {
	if (!owner)
	    return false;
	String part = params.getValue(YSTRING("partword"));
	return Module::itemComplete(*tmp,owner->toString(),part);
    }
    tmp = params.getParam(YSTRING("file"));
    if (tmp)
	return TelEngine::controlReturn(&params,setDumper(*tmp));
    return TelEngine::controlReturn(&params,false);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
