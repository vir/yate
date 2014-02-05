/**
 * testpart.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Signalling Stack - implements the support for SS7, ISDN and PSTN
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2010-2014 Null Team
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

#define CMD_STOP   0
#define CMD_SINGLE 1
#define CMD_START  2
#define CMD_RESET  3

// Control operations
static const TokenDict s_dict_control[] = {
    { "stop", CMD_STOP, },
    { "single", CMD_SINGLE, },
    { "start", CMD_START, },
    { "reset", CMD_RESET, },
    { 0, 0 }
};

HandledMSU SS7Testing::receivedMSU(const SS7MSU& msu, const SS7Label& label, SS7Layer3* network, int sls)
{
    if (msu.getSIF() != sif())
	return HandledMSU::Rejected;
    String src;
    int lvl = DebugNote;
    if (m_lbl.type() != SS7PointCode::Other) {
	if (label.type() != m_lbl.type())
	    return HandledMSU::Rejected;
	if (label.opc() == m_lbl.opc() && label.dpc() == m_lbl.dpc()) {
	    src = "MYSELF!";
	    lvl = DebugWarn;
	}
	else if (label.dpc() != m_lbl.opc())
	    return HandledMSU::Rejected;
    }
    if (src.null())
	src << SS7PointCode::lookup(label.type()) << ":" << label.opc() << ":" << label.sls();

    // Q.782 2.3: 4 bytes message number, 2 bytes length (9 bits used), N bytes zeros
    const unsigned char* s = msu.getData(label,6);
    if (!s)
	return false;
    u_int32_t seq = s[0] + ((u_int32_t)s[1] << 8) +
	((u_int32_t)s[2] << 16) + ((u_int32_t)s[3] << 24);
    u_int16_t len = s[4] + ((u_int16_t)s[5] << 8);

    const unsigned char* t = msu.getData(label.length()+6,len);
    if (!t) {
	if (lvl > DebugMild)
	    lvl = DebugMild;
	Debug(this,lvl,"Received MTP_T from %s, seq %u, length %u with invalid test length %u [%p]",
	    src.c_str(),seq,msu.length(),len,this);
	return false;
    }
    String exp;
    if (m_exp && (seq != m_exp))
	exp << " (" << m_exp << ")";
    m_exp = seq + 1;
    Debug(this,lvl,"Received MTP_T seq %u%s length %u from %s on %s:%d",seq,exp.safe(),len,
	src.c_str(),(network ? network->toString().c_str() : "?"),sls);
    return true;
}

bool SS7Testing::sendTraffic()
{
    if (!m_lbl.length())
	return false;
    u_int32_t seq = m_seq++;
    u_int16_t len = m_len + 6;
    if (m_sharing)
	m_lbl.setSls(seq & 0xff);
    SS7MSU msu(sio(),m_lbl,0,len);
    unsigned char* d = msu.getData(m_lbl,len);
    if (!d)
	return false;
    for (unsigned int i = 0; i < 4; i++)
	*d++ = 0xff & (seq >> (8 * i));
    *d++ = m_len & 0xff;
    *d++ = (m_len >> 8) & 0xff;
    String dest;
    dest << SS7PointCode::lookup(m_lbl.type()) << ":" << m_lbl.dpc() << ":" << m_lbl.sls();
    Debug(this,DebugInfo,"Sending MTP_T seq %u length %u to %s",seq,m_len,dest.c_str());
    return transmitMSU(msu,m_lbl,m_lbl.sls()) >= 0;
}

void SS7Testing::notify(SS7Layer3* network, int sls)
{
}

void SS7Testing::timerTick(const Time& when)
{
    Lock mylock(this,SignallingEngine::maxLockWait());
    if (!(mylock.locked() && m_timer.timeout(when.msec())))
	return;
    m_timer.start(when.msec());
    sendTraffic();
}

bool SS7Testing::initialize(const NamedList* config)
{
    if (!config)
	return true;
    Lock engLock(engine());
    Lock mylock(this);
    setParams(*config);
    bool ok = SS7Layer4::initialize(config);
    if (ok && config->getBoolValue(YSTRING("autostart"),false)) {
	if (m_timer.interval() && m_lbl.length())
	    m_timer.start();
	sendTraffic();
    }
    return ok;
}

bool SS7Testing::control(NamedList& params)
{
    String* ret = params.getParam(YSTRING("completion"));
    const String* oper = params.getParam(YSTRING("operation"));
    const char* cmp = params.getValue(YSTRING("component"));
    int cmd = oper ? oper->toInteger(s_dict_control,-1) : -1;

    if (ret) {
	if (oper && (cmd < 0))
	    return false;
	String part = params.getValue(YSTRING("partword"));
	if (cmp) {
	    if (toString() != cmp)
		return false;
	    for (const TokenDict* d = s_dict_control; d->token; d++)
		Module::itemComplete(*ret,d->token,part);
	    return true;
	}
	return Module::itemComplete(*ret,toString(),part);
    }

    if (!(cmp && toString() == cmp))
	return false;
    if (cmd >= 0) {
	Lock mylock(this);
	setParams(params,true);
	switch (cmd) {
	    case CMD_STOP:
		m_timer.stop();
		return TelEngine::controlReturn(&params,true);
	    case CMD_START:
		if (!(m_timer.interval() && m_lbl.length()))
		    return TelEngine::controlReturn(&params,false);
		m_timer.start();
		return TelEngine::controlReturn(&params,sendTraffic());
	    case CMD_SINGLE:
		if (!m_lbl.length())
		    return TelEngine::controlReturn(&params,false);
		m_timer.stop();
		return TelEngine::controlReturn(&params,sendTraffic());
	    case CMD_RESET:
		m_timer.stop();
		m_lbl.assign(SS7PointCode::Other,m_lbl.opc(),m_lbl.dpc(),m_lbl.sls());
		return TelEngine::controlReturn(&params,true);
	}
    }
    return SignallingComponent::control(params);
}

void SS7Testing::setParams(const NamedList& params, bool setSeq)
{
    if (!m_timer.interval() || params.getParam(YSTRING("interval")))
	m_timer.interval(params,"interval",20,1000,true);
    m_len = params.getIntValue(YSTRING("length"),m_len);
    m_sharing = params.getBoolValue(YSTRING("sharing"),m_sharing);
    if (m_len > 1024)
	m_len = 1024;
    if (setSeq || !m_seq)
	m_seq = params.getIntValue(YSTRING("sequence"),m_seq);
    const String* lbl = params.getParam(YSTRING("address"));
    if (!TelEngine::null(lbl)) {
	// TYPE,opc,dpc,sls,spare
	SS7PointCode::Type t = SS7PointCode::Other;
	ObjList* l = lbl->split(',');
	const GenObject* o = l->at(0);
	if (o) {
	    t = SS7PointCode::lookup(o->toString());
	    if (t == SS7PointCode::Other)
		t = m_lbl.type();
	}
	if (t != SS7PointCode::Other) {
	    o = l->at(1);
	    if (o) {
		SS7PointCode c(m_lbl.opc());
		if (c.assign(o->toString(),t))
		    m_lbl.assign(t,m_lbl.dpc(),c,m_lbl.sls(),m_lbl.spare());
	    }
	    o = l->at(2);
	    if (o) {
		SS7PointCode c(m_lbl.dpc());
		if (c.assign(o->toString(),t))
		    m_lbl.assign(t,c,m_lbl.opc(),m_lbl.sls(),m_lbl.spare());
	    }
	    o = l->at(3);
	    if (o) {
		int sls = o->toString().toInteger(-1);
		if (sls >= 0)
		    m_lbl.setSls(sls);
	    }
	    o = l->at(4);
	    if (o) {
		int spare = o->toString().toInteger(-1);
		if (spare >= 0)
		    m_lbl.setSpare(spare);
	    }
	}
	delete l;
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
