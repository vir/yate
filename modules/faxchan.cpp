/**
 * faxchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * This module is based on SpanDSP (a series of DSP components for telephony),
 * written by Steve Underwood <steveu@coppice.org>.
 * 
 * This great software can be found at 
 * ftp://opencall.org/pub/spandsp/
 * 
 * Fax driver (transmission+receiving)
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

// For SpanDSP we have to ask for various C99 stuff
#define __USE_ISOC99
#define __STDC_LIMIT_MACROS

extern "C" {
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <tiffio.h>

#include "spandsp.h"
};

#include <telengine.h>
#include <telephony.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

using namespace TelEngine;

class FaxChan : public DataEndpoint
{
public:
    FaxChan(const char *file, bool receive, bool iscaller, const char *ident = 0);
    ~FaxChan();
    virtual void disconnected(bool final, const char *reason);
    void rxData(const DataBlock &data);
    void rxBlock(void *buff, int len);
    int txBlock();
    void phaseB(int result);
    void phaseD(int result);
    void phaseE(int result);
private:
    t30_state_t m_fax;
    Mutex m_mutex;
    DataBlock m_buf;
    int m_lastr;
    bool m_eof;
};

class FaxSource : public ThreadedSource
{
public:
    FaxSource(FaxChan *chan);
    ~FaxSource();
    virtual void run();
private:
    unsigned m_total;
    FaxChan *m_chan;
};

class FaxConsumer : public DataConsumer
{
public:
    FaxConsumer(FaxChan *chan);
    ~FaxConsumer();
    virtual void Consume(const DataBlock &data,unsigned long);
private:
    unsigned m_total;
    FaxChan *m_chan;
};

class FaxHandler : public MessageHandler
{
public:
    FaxHandler(const char *name) : MessageHandler(name) { }
    virtual bool received(Message &msg);
};

class FaxPlugin : public Plugin
{
public:
    FaxPlugin();
    virtual void initialize();
private:
    FaxHandler *m_handler;
};

FaxSource::FaxSource(FaxChan *chan)
    : m_total(0), m_chan(chan)
{
    Debug(DebugAll,"FaxSource::FaxSource(%p) [%p]",chan,this);
    start("FaxSource");
}

FaxSource::~FaxSource()
{
    Debug(DebugAll,"FaxSource::~FaxSource() [%p] total=%u",this,m_total);
}

void FaxSource::run()
{
    unsigned long long tpos = Time::now();
    for (;;) {
	int r = m_chan->txBlock();
	if (r < 0)
	    break;
	if (!r) {
	    r = 80;
	    DDebug(DebugAll,"FaxSource inserting %d bytes silence [%p]",r,this);
	    DataBlock data(0,r);
	    Forward(data);
	}

	m_total += r;
	tpos += (r*1000000ULL/16000);

	long long dly = tpos - Time::now();
	if (dly > 10000)
	    dly = 10000;
	if (dly > 0)
	    ::usleep((unsigned long)dly);
    }
    Debug(DebugAll,"FaxSource [%p] end of data total=%u",this,m_total);
}

FaxConsumer::FaxConsumer(FaxChan *chan)
    : m_total(0), m_chan(chan)
{
    Debug(DebugAll,"FaxConsumer::FaxConsumer(%p) [%p]",chan,this);
}

FaxConsumer::~FaxConsumer()
{
    Debug(DebugAll,"FaxConsumer::~FaxConsumer() [%p] total=%u",this,m_total);
}

void FaxConsumer::Consume(const DataBlock &data,unsigned long)
{
    if (data.null())
	return;
    m_total += data.length();
    m_chan->rxData(data);
}

static void phase_b_handler(t30_state_t *s, void *user_data, int result)
{
    if (user_data)
	static_cast<FaxChan*>(user_data)->phaseB(result);
}

static void phase_d_handler(t30_state_t *s, void *user_data, int result)
{
    if (user_data)
	static_cast<FaxChan*>(user_data)->phaseD(result);
}

static void phase_e_handler(t30_state_t *s, void *user_data, int result)
{
    if (user_data)
	static_cast<FaxChan*>(user_data)->phaseE(result);
}


FaxChan::FaxChan(const char *file, bool receive, bool iscaller, const char *ident)
    : DataEndpoint("faxfile"), m_lastr(0), m_eof(false)
{
    Debug(DebugAll,"FaxChan::FaxChan(%s \"%s\") [%p]",
	(receive ? "receive" : "transmit"),file,this);
    if (!ident)
	ident = "unknown";
    fax_init(&m_fax, iscaller, NULL);
    fax_set_local_ident(&m_fax, ident);
    if (receive)
	fax_set_rx_file(&m_fax, file);
    else
	fax_set_tx_file(&m_fax, file);
    
    //TODO add in the futher a callback to find number of pages and stuff like that.
    fax_set_phase_e_handler(&m_fax, phase_e_handler, this);
    fax_set_phase_d_handler(&m_fax, phase_d_handler, this);
    fax_set_phase_b_handler(&m_fax, phase_b_handler, this);
    m_fax.verbose = 1;

    setConsumer(new FaxConsumer(this));
    getConsumer()->deref();
    setSource(new FaxSource(this));
    getSource()->deref();
}

FaxChan::~FaxChan()
{
    Debug(DebugAll,"FaxChan::~FaxChan() [%p]",this);
    setConsumer();
    setSource();
}

int FaxChan::txBlock()
{
    Lock lock(m_mutex);
    if (m_lastr < 0)
	return m_lastr;
    int r = m_buf.length();
    if (r) {
	getSource()->Forward(m_buf);
	m_buf.clear();
    }
    else if (m_eof) {
	lock.drop();
	disconnect("eof");
	r = -1;
    }
    return r;
}

void FaxChan::rxBlock(void *buff, int len)
{
    Lock lock(m_mutex);
    fax_rx_process(&m_fax, (int16_t *)buff,len/2);

    DataBlock data(0,len);
    int r = 2*fax_tx_process(&m_fax, (int16_t *) data.data(),len/2);
    if (r != len && r != m_lastr)
	Debug("FaxChan",DebugWarn,"Generated %d bytes! [%p]",r,this);
    m_lastr = r;
    if (r <= 0) {
	return;
    }
    data.truncate(r);
    m_buf.append(data);
}

void FaxChan::rxData(const DataBlock &data)
{
    unsigned int pos = 0;
    while (pos < data.length())
    {
	int len = data.length() - pos;
	if (len > 80)
	    len = 80;
	rxBlock(((char *)data.data())+pos, len);
	pos += len;
    }
}

void FaxChan::phaseB(int result)
{
    Debug(DebugAll,"FaxChan::phaseB code 0x%X [%p]",result,this);
}

void FaxChan::phaseD(int result)
{
    Debug(DebugAll,"FaxChan::phaseD code 0x%X [%p]",result,this);

    t30_stats_t t;
    char ident[21];

    fax_get_transfer_statistics(&m_fax, &t);
    Debug("Fax",DebugAll,"bit rate %d", t.bit_rate);
    Debug("Fax",DebugAll,"pages transferred %d", t.pages_transferred);
    Debug("Fax",DebugAll,"image size %d x %d", t.columns, t.rows);
    Debug("Fax",DebugAll,"image resolution %d x %d", t.column_resolution, t.row_resolution);
    Debug("Fax",DebugAll,"bad rows %d", t.bad_rows);
    Debug("Fax",DebugAll,"longest bad row run %d", t.longest_bad_row_run);
    Debug("Fax",DebugAll,"compression type %d", t.encoding);
    Debug("Fax",DebugAll,"image size %d", t.image_size);

    fax_get_local_ident(&m_fax, ident);
    Debug("Fax",DebugAll,"local ident '%s'", ident);

    fax_get_far_ident(&m_fax, ident);
    Debug("Fax",DebugAll,"remote ident '%s'", ident);
}

void FaxChan::phaseE(int result)
{
    Debug(DebugAll,"FaxChan::phaseE code 0x%X [%p]",result,this);
    m_eof = true;
}

void FaxChan::disconnected(bool final, const char *reason)
{
    Debug(DebugInfo,"FaxChan::disconnected() '%s' [%p]",reason,this);
}

bool FaxHandler::received(Message &msg)
{
    String dest(msg.getValue("callto"));
    if (dest.null())
	return false;
    Regexp r("^fax/\\([^/]*\\)/\\([^/]*\\)/\\(.*\\)$");
    if (!dest.matches(r))
	return false;

    bool transmit = false;
    if (dest.matchString(1) == "transmit")
	transmit = true;
    else if (dest.matchString(1) != "receive") {
	Debug(DebugGoOn,"Invalid fax method '%s', use 'receive' or 'transmit'",
	    dest.matchString(1).c_str());
	return false;
    }
    bool iscaller = (dest.matchString(2) == "caller");

    FaxChan *fc = 0;
    if (transmit) {
	Debug(DebugInfo,"Transmit fax from file '%s'",dest.matchString(3).c_str());
	fc = new FaxChan(dest.matchString(3).c_str(),false,iscaller);
    }
    else {
	Debug(DebugInfo,"Receive fax into file '%s'",dest.matchString(3).c_str());
	fc = new FaxChan(dest.matchString(3).c_str(),true,iscaller);
    }
    DataEndpoint *dd = static_cast<DataEndpoint *>(msg.userData());
    if (dd) {
	if (dd->connect(fc)) {
	    fc->deref();
	    return true;
	}
    }
    else {
	const char *targ = msg.getValue("target");
	if (!targ) {
	    Debug(DebugWarn,"Fax outgoing call with no target!");
	    fc->destruct();
	    return false;
	}
	Message m("call.route");
	m.addParam("id",dest);
	m.addParam("caller",dest);
	m.addParam("called",targ);
	m.userData(fc);
	if (Engine::dispatch(m)) {
	    m = "call.execute";
	    m.addParam("callto",m.retValue());
	    m.retValue().clear();
	    if (Engine::dispatch(m)) {
		fc->deref();
		return true;
	    }
	    Debug(DebugWarn,"Fax outgoing call not accepted!");
	}
	else
	    Debug(DebugWarn,"Fax outgoing call but no route!");
    }
    fc->destruct();
    return false;
}

FaxPlugin::FaxPlugin()
    : m_handler(0)
{
    Output("Loaded module Fax");
}

void FaxPlugin::initialize()
{
    Output("Initializing module Fax");
    if (!m_handler) {
	m_handler = new FaxHandler("call.execute");
	Engine::install(m_handler);
    }
}

INIT_PLUGIN(FaxPlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
