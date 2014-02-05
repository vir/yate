/**
 * management.cpp
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

#include <string.h>

using namespace TelEngine;

#define MAKE_NAME(x) { #x, SS7MsgSNM::x }
static const TokenDict s_snm_names[] = {
    // this list must be kept in synch with the header
    MAKE_NAME(COO),
    MAKE_NAME(ECO),
    MAKE_NAME(RCT),
    MAKE_NAME(TFP),
    MAKE_NAME(RST),
    MAKE_NAME(RSP), // alias
    MAKE_NAME(LIN),
    MAKE_NAME(TRA),
    MAKE_NAME(DLC),
    MAKE_NAME(UPU),
    MAKE_NAME(COA),
    MAKE_NAME(ECA),
    MAKE_NAME(TFC),
    MAKE_NAME(TCP),
    MAKE_NAME(TFPA), // alias
    MAKE_NAME(RSR),
    MAKE_NAME(LUN),
    MAKE_NAME(TRW),
    MAKE_NAME(CSS),
    MAKE_NAME(XCO),
    MAKE_NAME(TFR),
    MAKE_NAME(RCP),
    MAKE_NAME(LIA),
    MAKE_NAME(CNS),
    MAKE_NAME(XCA),
    MAKE_NAME(TCR),
    MAKE_NAME(RCR),
    MAKE_NAME(LUA),
    MAKE_NAME(CNP),
    MAKE_NAME(CBD),
    MAKE_NAME(TFA),
    MAKE_NAME(LID),
    MAKE_NAME(CBA),
    MAKE_NAME(TCA),
    MAKE_NAME(TFAA), // alias
    MAKE_NAME(LFU),
    MAKE_NAME(LLT),
    MAKE_NAME(LLI), // alias
    MAKE_NAME(LRT),
    MAKE_NAME(LRI), // alias
    { 0, 0 }
};

static const TokenDict s_snm_group[] = {
    // this list must be kept in synch with the header
    MAKE_NAME(CHM),
    MAKE_NAME(ECM),
    MAKE_NAME(FCM),
    MAKE_NAME(TFM),
    MAKE_NAME(RSM),
    MAKE_NAME(MIM),
    MAKE_NAME(TRM),
    MAKE_NAME(DLM),
    MAKE_NAME(UFC),
    { 0, 0 }
};
#undef MAKE_NAME

#define TIMER5M 300000

namespace { //anonymous

class SnmPending : public SignallingMessageTimer, public SS7Label
{
public:
    inline SnmPending(SS7MSU* msg, const SS7Label& label, int txSls, u_int64_t interval, u_int64_t global = 0)
	: SignallingMessageTimer(interval,global), SS7Label(label),
	  m_msu(msg), m_txSls(txSls)
	{ }

    inline ~SnmPending()
	{ TelEngine::destruct(m_msu); }

    inline SS7MSU& msu() const
	{ return *m_msu; }

    inline int txSls() const
	{ return m_txSls; }

    inline SS7MsgSNM::Type snmType() const
	{ return (SS7MsgSNM::Type)m_msu->at(length()+1,0); }

    inline const char* snmName() const
	{ return SS7MsgSNM::lookup(snmType(),"Unknown"); }

    inline bool matches(const SS7Label& lbl) const
	{ return opc() == lbl.dpc() && dpc() == lbl.opc() && sls() == lbl.sls(); }

private:
    SS7MSU* m_msu;
    int m_txSls;
};

}; // anonymous namespace

// Constructor
SS7MsgSNM::SS7MsgSNM(unsigned char type)
    : SignallingMessage(lookup((Type)type,"Unknown")),
    m_type(type)
{
}

void SS7MsgSNM::toString(String& dest, const SS7Label& label, bool params) const
{
    const char* enclose = "\r\n-----";
    dest = enclose;
    dest << "\r\n" << name() << " [label=" << label << ']';
    if (params) {
	unsigned int n = m_params.length();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* s = m_params.getParam(i);
	    if (s)
		dest << "\r\n  " << s->name() << "='" << *s << "'";
	}
    }
    dest << enclose;
}

// Parse a received buffer and build a message from it
SS7MsgSNM* SS7MsgSNM::parse(SS7Management* receiver, unsigned char type,
    SS7PointCode::Type pcType, const unsigned char* buf, unsigned int len)
{
    const char* pct = SS7PointCode::lookup(pcType);
    if (!pct)
	return 0;
    SS7MsgSNM* msg = new SS7MsgSNM(type);
    msg->params().addParam("pointcodetype",pct);
#ifdef XDEBUG
    String tmp;
    tmp.hexify((void*)buf,len,' ');
    Debug(receiver,DebugAll,"Decoding msg=%s pctype=%s buf: %s [%p]",
	msg->name(),pct,tmp.c_str(),receiver);
#endif
    // TODO: parse the rest of the message. Check extra bytes (message specific)
    if (!(buf && len))
	return msg;
    do {
	// TFP,TFR,TFA: Q.704 15.7, RST,RSR: Q.704 15.10
	// There must be at least 2 bytes in buffer
	if (type == TFP || type == TFR || type == TFA || type == TFC ||
	    type == RST || type == RSR) {
	    // 2 bytes destination
	    SS7PointCode pc;
	    unsigned char spare;
	    if (pc.assign(pcType,buf,len,&spare)) {
		String tmp;
		tmp << pc;
		msg->params().addParam("destination",tmp);
		if (spare) {
		    tmp.hexify(&spare,1);
		    msg->params().addParam("spare",tmp);
		}
	    }
	    else
		Debug(receiver,DebugNote,
		    "Failed to decode destination for msg=%s len=%u [%p]",
		    msg->name(),len,receiver);
	    break;
	}
	// COO,COA,XCO,XCA: changeover sequence, slc
	else if (type == COO || type == COA || type == XCO || type == XCA) {
	    int seq = -1;
	    int slc = -1;
	    switch (pcType) {
		case SS7PointCode::ITU:
		    if (len >= 1)
			seq = buf[0];
		    if ((type == XCO || type == XCA) && (len >= 3))
			seq |= (((unsigned int)buf[1]) << 8) | (((unsigned int)buf[2]) << 16);
		    break;
		case SS7PointCode::ANSI:
		    if (len >= 2) {
			slc = buf[0] & 0x0f;
			seq = (buf[0] >> 4) | (((unsigned int)buf[1]) << 4);
			if ((type == XCO || type == XCA) && (len >= 4))
			    seq |= (((unsigned int)buf[2]) << 12) | (((unsigned int)buf[3]) << 20);
		    }
		    break;
		default:
		    Debug(DebugStub,"Please implement COO decoding for type %u",pcType);
	    }
	    if (seq >= 0)
		msg->params().addParam("sequence",String(seq));
	    if (slc >= 0)
		msg->params().addParam("slc",String(slc));
	}
	// CBD,CBA: changeback code, slc
	else if (type == CBD || type == CBA) {
	    int code = -1;
	    int slc = -1;
	    switch (pcType) {
		case SS7PointCode::ITU:
		    if (len >= 1)
			code = buf[0];
		    break;
		case SS7PointCode::ANSI:
		    if (len >= 2) {
			slc = buf[0] & 0x0f;
			code = (buf[0] >> 4) | (((unsigned int)buf[1]) << 4);
		    }
		    break;
		default:
		    Debug(DebugStub,"Please implement CBD decoding for type %u",pcType);
	    }
	    if (code >= 0)
		msg->params().addParam("code",String(code));
	    if (slc >= 0)
		msg->params().addParam("slc",String(slc));
	}
	// UPU: user part ID, unavailability cause
	else if (type == UPU) {
	    SS7PointCode pc;
	    unsigned char spare;
	    if (pc.assign(pcType,buf,len,&spare)) {
		String tmp;
		tmp << pc;
		msg->params().addParam("destination",tmp);
		if (spare) {
		    tmp.hexify(&spare,1);
		    msg->params().addParam("spare",tmp);
		}
		unsigned int dlen = SS7PointCode::length(pcType);
		if (dlen < len) {
		    msg->params().addParam("part",String((unsigned int)buf[dlen] & 0x0f));
		    msg->params().addParam("cause",String((unsigned int)buf[dlen] >> 4));
		}
	    }
	    else
		Debug(receiver,DebugNote,
		    "Failed to decode destination for msg=%s len=%u [%p]",
		    msg->name(),len,receiver);
	}
    } while (false);
    return msg;
}

const TokenDict* SS7MsgSNM::names()
{
    return s_snm_names;
}


#define MAKE_NAME(x) { #x, SS7MsgMTN::x }
static const TokenDict s_mtn_names[] = {
    // this list must be kept in synch with the header
    MAKE_NAME(SLTM),
    MAKE_NAME(SLTA),
    { 0, 0 }
};
#undef MAKE_NAME

const TokenDict* SS7MsgMTN::names()
{
    return s_mtn_names;
}


// Control operations
static const TokenDict s_dict_control[] = {
    { "prohibit", SS7MsgSNM::TFP },
    { "restrict", SS7MsgSNM::TFR },
    { "congest", SS7MsgSNM::TFC },
    { "allow", SS7MsgSNM::TFA },
    { "restart", SS7MsgSNM::TRA },
    { "changeover", SS7MsgSNM::COO },
    { "changeback", SS7MsgSNM::CBD },
    { "link-inhibit", SS7MsgSNM::LIN },
    { "link-uninhibit", SS7MsgSNM::LUN },
    { "link-force-uninhibit", SS7MsgSNM::LFU },
    { "test-congestion", SS7MsgSNM::RCT },
    { "test-prohibited", SS7MsgSNM::RST },
    { "test-restricted", SS7MsgSNM::RSR },
    { 0, 0 }
};

SS7Management::SS7Management(const NamedList& params, unsigned char sio)
    : SignallingComponent(params.safe("SS7Management"),&params,"ss7-snm"),
      SS7Layer4(sio,&params),
      m_changeMsgs(true), m_changeSets(false), m_neighbours(true)
{
    m_changeMsgs = params.getBoolValue(YSTRING("changemsgs"),m_changeMsgs);
    m_changeSets = params.getBoolValue(YSTRING("changesets"),m_changeSets);
    m_neighbours = params.getBoolValue(YSTRING("neighbours"),m_neighbours);
}


HandledMSU SS7Management::receivedMSU(const SS7MSU& msu, const SS7Label& label, SS7Layer3* network, int sls)
{
    if (msu.getSIF() != sif())
	return HandledMSU::Rejected;
    if (network) {
	unsigned int local = network->getLocal(label.type());
	if (local && label.dpc().pack(label.type()) != local)
	    return HandledMSU::Rejected;
    }
    SS7Router* router = YOBJECT(SS7Router,SS7Layer4::network());
    if (router && (router != network)) {
	unsigned int local = router->getLocal(label.type());
	if (local && label.dpc().pack(label.type()) != local)
	    return HandledMSU::Rejected;
    }

    unsigned int len = msu.length() - label.length() - 1;
    // according to Q.704 there should be at least the heading codes (8 bit)
    const unsigned char* buf = msu.getData(label.length()+1,1);
    if (!buf)
	return false;
    RefPointer<SS7MsgSNM> msg = SS7MsgSNM::parse(this,buf[0],label.type(),buf+1,len-1);
    if (!msg)
	return false;
    msg->deref();

    if (debugAt(DebugInfo)) {
	String tmp;
	msg->toString(tmp,label,debugAt(DebugAll));
	const char* name = network ? network->toString().c_str() : 0;
	Debug(this,DebugInfo,"Received %u bytes message (%p) on %s:%d%s",
	    len,static_cast<void*>(msg),name,sls,tmp.c_str());
    }

    String addr;
    addr << label;
    if (m_neighbours && (msg->type() != SS7MsgSNM::UPU)) {
	int prio = -1;
	if (router)
	    prio = (int)router->getRoutePriority(label.type(),label.opc());
	else if (network)
	    prio = (int)network->getRoutePriority(label.type(),label.opc());
	if (prio) {
	    Debug(this,DebugMild,"Refusing %s message from %s node %s",
		msg->name(),(prio > 0 ? "non-neighboor" : "unknown"),addr.c_str());
	    return false;
	}
    }

    SS7Label lbl(label,label.sls(),0);
    {
	String tmp;
	tmp << SS7PointCode::lookup(label.type()) << "," << addr;
	// put the addresses with commas as we need them
	char* fix = const_cast<char*>(tmp.c_str());
	for (; *fix; fix++)
	    if (':' == *fix)
		*fix = ',';
	msg->params().addParam("address",tmp);
	tmp.clear();
	tmp << SS7PointCode::lookup(label.type()) << "," << lbl;
	fix = const_cast<char*>(tmp.c_str());
	for (; *fix; fix++)
	    if (':' == *fix)
		*fix = ',';
	msg->params().addParam("back-address",tmp);
    }
    switch (msg->group()) {
	case SS7MsgSNM::CHM:
	case SS7MsgSNM::ECM:
	case SS7MsgSNM::MIM:
	    {
		// for ANSI the SLC is not stored in SLS but in a separate field
		int slc = msg->params().getIntValue(YSTRING("slc"),-1);
		if (slc >= 0 && slc <= 255)
		    lbl.setSls((unsigned char)slc);
	    }
	    // check if the addressed link exists
	    if (router && !router->inhibit(lbl,0,0)) {
		Debug(this,DebugMild,"Received %s for inexistent %s on SLS %d [%p]",
		    msg->name(),addr.c_str(),sls,this);
		return false;
	    }
    }

    if (msg->type() == SS7MsgSNM::TFP ||
	msg->type() == SS7MsgSNM::TFR ||
	msg->type() == SS7MsgSNM::TFA ||
	msg->type() == SS7MsgSNM::TFC ||
	msg->type() == SS7MsgSNM::RST ||
	msg->type() == SS7MsgSNM::RSR) {
	String dest = msg->params().getValue(YSTRING("destination"));
	if (!dest.null()) {
	    const char* oper = lookup(msg->type(),s_dict_control);
	    Debug(this,DebugInfo,"%s (label=%s): Traffic %s to dest=%s [%p]",
		    msg->name(),addr.c_str(),oper,dest.c_str(),this);
	    if (router && oper) {
		NamedList* ctrl = router->controlCreate(oper);
		if (ctrl) {
		    ctrl->copyParams(msg->params());
		    ctrl->setParam("automatic",String::boolText(true));
		    router->controlExecute(ctrl);
		}
	    }
	}
	else
	    Debug(this,DebugNote,"Received %s (label=%s) without destination [%p]",
		    msg->name(),addr.c_str(),this);
    }
    else if (msg->type() == SS7MsgSNM::TRA) {
	String dest;
	dest << label.opc();
	Debug(this,DebugInfo,"%s (label=%s): Traffic can restart to dest=%s [%p]",
	    msg->name(),addr.c_str(),dest.c_str(),this);
	if (router) {
	    NamedList* ctrl = router->controlCreate("allowed");
	    if (ctrl) {
		ctrl->copyParams(msg->params());
		ctrl->setParam("destination",dest);
		ctrl->setParam("automatic",String::boolText(true));
		router->controlExecute(ctrl);
	    }
	}
    }
    else if (msg->type() == SS7MsgSNM::COO ||
	msg->type() == SS7MsgSNM::XCO ||
	msg->type() == SS7MsgSNM::ECO) {
	if (!len--)
	    return false;
	const unsigned char* s = msu.getData(label.length()+2,len);
	if (!s)
	    return false;
	Debug(this,DebugAll,"%s (code len=%u) [%p]",msg->name(),len,this);
	if (!m_changeMsgs)
	    return true;
	if (inhibit(lbl,SS7Layer2::Inactive)) {
	    String link;
	    link << msg->params().getValue(YSTRING("pointcodetype")) << "," << lbl;
	    Debug(this,DebugNote,"Changeover order on %s",link.c_str());
	    int seq = msg->params().getIntValue(YSTRING("sequence"),-1);
	    if (seq >= 0)
		recover(lbl,seq);
	    seq = router ? router->getSequence(lbl) : -1;
	    if (seq >= 0) {
		int len = 2;
		unsigned char data[5];
		data[0] = SS7MsgSNM::COA;
		if (seq & 0xff000000) {
		    seq &= 0x00ffffff;
		    if (msg->type() != SS7MsgSNM::COO || (seq & 0x00ffff80)) {
			data[0] = SS7MsgSNM::XCA;
			len += 2;
		    }
		}
		switch (label.type()) {
		    case SS7PointCode::ITU:
			data[1] = (unsigned char)seq;
			if (len >= 4) {
			    data[2] = (unsigned char)(seq >> 8);
			    data[3] = (unsigned char)(seq >> 16);
			}
			break;
		    case SS7PointCode::ANSI:
			data[1] = (unsigned char)((msg->params().getIntValue(YSTRING("slc"),sls) & 0x0f) | (seq << 4));
			data[2] = (unsigned char)(seq >> 4);
			len++;
			if (len >= 5) {
			    data[3] = (unsigned char)(seq >> 12);
			    data[4] = (unsigned char)(seq >> 20);
			}
			break;
		    default:
			Debug(DebugStub,"Please implement COO for type %u",label.type());
			return false;
		}
		return transmitMSU(SS7MSU(msu.getSIO(),lbl,&data,len),lbl,sls) >= 0;
	    }
	    else {
		// postpone an ECA in case we are unable to send a COA/XCA
		static unsigned char data = SS7MsgSNM::ECA;
		return postpone(new SS7MSU(msu.getSIO(),lbl,&data,1),lbl,sls,0,200);
	    }
	}
	else
	    Debug(this,DebugMild,"Unexpected %s %s [%p]",msg->name(),addr.c_str(),this);
    }
    else if (msg->type() == SS7MsgSNM::COA ||
	msg->type() == SS7MsgSNM::XCA ||
	msg->type() == SS7MsgSNM::ECA) {
	if (!len--)
	    return false;
	Debug(this,DebugAll,"%s (code len=%u) [%p]",msg->name(),len,this);
	if (!m_changeMsgs)
	    return true;
	lock();
	SnmPending* pend = 0;
	for (ObjList* l = m_pending.skipNull(); l; l = l->skipNext()) {
	    SnmPending* p = static_cast<SnmPending*>(l->get());
	    const unsigned char* ptr = p->msu().getData(p->length()+1,1);
	    if (!(ptr && p->matches(label)))
		continue;
	    switch (ptr[0]) {
		case SS7MsgSNM::COO:
		case SS7MsgSNM::XCO:
		case SS7MsgSNM::ECO:
		    break;
		default:
		    continue;
	    }
	    pend = static_cast<SnmPending*>(m_pending.remove(p,false));
	    break;
	}
	unlock();
	if (pend) {
	    String link;
	    link << msg->params().getValue(YSTRING("pointcodetype")) << "," << *pend;
	    Debug(this,DebugNote,"Changeover acknowledged on %s",link.c_str());
	    inhibit(*pend,SS7Layer2::Inactive);
	    int seq = msg->params().getIntValue(YSTRING("sequence"),-1);
	    if (seq >= 0)
		recover(*pend,seq);
	}
	else
	    Debug(this,DebugMild,"Unexpected %s %s [%p]",msg->name(),addr.c_str(),this);
	TelEngine::destruct(pend);
    }
    else if (msg->type() == SS7MsgSNM::CBD) {
	if (!len--)
	    return false;
	const unsigned char* s = msu.getData(label.length()+2,len);
	if (!s)
	    return false;
	Debug(this,DebugAll,"%s (code len=%u) [%p]",msg->name(),len,this);
	if (!m_changeMsgs)
	    return true;
	if (inhibit(lbl,0,SS7Layer2::Inactive)) {
	    String link;
	    link << msg->params().getValue(YSTRING("pointcodetype")) << "," << lbl;
	    Debug(this,DebugNote,"Changeback declaration on %s",link.c_str());
	    SS7MSU answer(msu.getSIO(),lbl,0,len+1);
	    unsigned char* d = answer.getData(lbl.length()+1,len+1);
	    if (!d)
		return false;
	    *d++ = SS7MsgSNM::CBA;
	    while (len--)
		*d++ = *s++;
	    return transmitMSU(answer,lbl,sls) >= 0;
	}
	else
	    Debug(this,DebugMild,"Unexpected %s %s [%p]",msg->name(),addr.c_str(),this);
    }
    else if (msg->type() == SS7MsgSNM::CBA) {
	if (!len--)
	    return false;
	Debug(this,DebugAll,"%s (code len=%u) [%p]",msg->name(),len,this);
	if (!m_changeMsgs)
	    return true;
	lock();
	SnmPending* pend = 0;
	for (ObjList* l = m_pending.skipNull(); l; l = l->skipNext()) {
	    SnmPending* p = static_cast<SnmPending*>(l->get());
	    if (p->msu().length() != msu.length())
		continue;
	    const unsigned char* ptr = p->msu().getData(p->length()+1,len+1);
	    if (!ptr || (ptr[0] != SS7MsgSNM::CBD))
		continue;
	    if (::memcmp(ptr+1,buf+1,len) || !p->matches(label))
		continue;
	    pend = static_cast<SnmPending*>(m_pending.remove(p,false));
	    break;
	}
	unlock();
	if (pend) {
	    String link;
	    link << msg->params().getValue(YSTRING("pointcodetype")) << "," << *pend;
	    Debug(this,DebugNote,"Changeback acknowledged on %s",link.c_str());
	    inhibit(*pend,0,SS7Layer2::Inactive);
	}
	else
	    Debug(this,DebugMild,"Unexpected %s %s [%p]",msg->name(),addr.c_str(),this);
	TelEngine::destruct(pend);
    }
    else if (msg->type() == SS7MsgSNM::LIN) {
	Debug(this,DebugAll,"%s (code len=%u) [%p]",msg->name(),len,this);
	if (router) {
	    bool ok = router->inhibit(lbl,SS7Layer2::Remote,0,true);
	    unsigned char data = ok ? SS7MsgSNM::LIA : SS7MsgSNM::LID;
	    if (ok) {
		static unsigned char lrt = SS7MsgSNM::LRT;
		postpone(new SS7MSU(msu.getSIO(),lbl,&lrt,1),lbl,sls,0,TIMER5M);
	    }
	    return transmitMSU(SS7MSU(msu.getSIO(),lbl,&data,1),lbl,sls) >= 0;
	}
    }
    else if (msg->type() == SS7MsgSNM::LIA || msg->type() == SS7MsgSNM::LUA) {
	Debug(this,DebugAll,"%s (code len=%u) [%p]",msg->name(),len,this);
	unsigned char test = (msg->type() == SS7MsgSNM::LIA) ? SS7MsgSNM::LIN : SS7MsgSNM::LUN;
	lock();
	SnmPending* pend = 0;
	for (ObjList* l = m_pending.skipNull(); l; l = l->skipNext()) {
	    SnmPending* p = static_cast<SnmPending*>(l->get());
	    const unsigned char* ptr = p->msu().getData(p->length()+1,1);
	    if (!(ptr && p->matches(label)))
		continue;
	    if (ptr[0] != test)
		continue;
	    pend = static_cast<SnmPending*>(m_pending.remove(p,false));
	    break;
	}
	unlock();
	if (pend) {
	    if (test == SS7MsgSNM::LIN) {
		inhibit(*pend,SS7Layer2::Local);
		static unsigned char llt = SS7MsgSNM::LLT;
		postpone(new SS7MSU(msu.getSIO(),*pend,&llt,1),*pend,sls,0,TIMER5M);
	    }
	    else {
		inhibit(*pend,0,SS7Layer2::Local);
		lock();
		for (ObjList* l = m_pending.skipNull(); l; l = l->skipNext()) {
		    SnmPending* p = static_cast<SnmPending*>(l->get());
		    const unsigned char* ptr = p->msu().getData(p->length()+1,1);
		    if (!ptr || (ptr[0] != SS7MsgSNM::LLT))
			continue;
		    if (!p->matches(label))
			continue;
		    m_pending.remove(p);
		    break;
		}
		unlock();
	    }
	}
	else
	    Debug(this,DebugMild,"Unexpected %s %s [%p]",msg->name(),addr.c_str(),this);
    }
    else if (msg->type() == SS7MsgSNM::LUN) {
	Debug(this,DebugAll,"%s (code len=%u) [%p]",msg->name(),len,this);
	if (router && router->inhibit(lbl,0,SS7Layer2::Remote)) {
	    lock();
	    for (ObjList* l = m_pending.skipNull(); l; ) {
		SnmPending* p = static_cast<SnmPending*>(l->get());
		const unsigned char* ptr = p->msu().getData(p->length()+1,1);
		if (ptr && ((ptr[0] == SS7MsgSNM::LRT) || (ptr[0] == SS7MsgSNM::LFU)) && p->matches(label)) {
		    m_pending.remove(p);
		    l = m_pending.skipNull();
		}
		else
		    l = l->skipNext();
	    }
	    unlock();
	    static unsigned char lua = SS7MsgSNM::LUA;
	    return transmitMSU(SS7MSU(msu.getSIO(),lbl,&lua,1),lbl,sls) >= 0;
	}
    }
    else if (msg->type() == SS7MsgSNM::LID) {
	Debug(this,DebugAll,"%s (code len=%u) [%p]",msg->name(),len,this);
	bool found = false;
	lock();
	for (ObjList* l = m_pending.skipNull(); l; l = l->skipNext()) {
	    SnmPending* p = static_cast<SnmPending*>(l->get());
	    const unsigned char* ptr = p->msu().getData(p->length()+1,1);
	    if (!ptr || (ptr[0] != SS7MsgSNM::LIN))
		continue;
	    if (!p->matches(label))
		continue;
	    m_pending.remove(p);
	    found = true;
	    break;
	}
	unlock();
	if (found)
	    Debug(this,DebugWarn,"Remote refused to inhibit link %d",label.sls());
	else
	    Debug(this,DebugMild,"Unexpected %s %s [%p]",msg->name(),addr.c_str(),this);
    }
    else if (msg->type() == SS7MsgSNM::LFU) {
	Debug(this,DebugAll,"%s (code len=%u) [%p]",msg->name(),len,this);
	static unsigned char data = SS7MsgSNM::LUN;
	int global = 0;
	// if link is locally inhibited execute the complete procedure
	if (router && router->inhibited(lbl,SS7Layer2::Local))
	    global = 2400;
	return postpone(new SS7MSU(msu.getSIO(),lbl,&data,1),lbl,sls,1200,global);
    }
    else if (msg->type() == SS7MsgSNM::LRT) {
	Debug(this,DebugAll,"%s (code len=%u) [%p]",msg->name(),len,this);
	if (router && router->inhibited(lbl,SS7Layer2::Local))
	    return true;
	static unsigned char data = SS7MsgSNM::LUN;
	return postpone(new SS7MSU(msu.getSIO(),lbl,&data,1),lbl,sls,1200,2400);
    }
    else if (msg->type() == SS7MsgSNM::LLT) {
	Debug(this,DebugAll,"%s (code len=%u) [%p]",msg->name(),len,this);
	if (router && router->inhibited(lbl,SS7Layer2::Remote))
	    return true;
	static unsigned char data = SS7MsgSNM::LFU;
	return postpone(new SS7MSU(msu.getSIO(),lbl,&data,1),lbl,sls,1200,2400);
    }
    else if (msg->type() == SS7MsgSNM::UPU) {
	Debug(this,DebugNote,"Unavailable part %s at %s, cause %s",
	    msg->params().getValue(YSTRING("part"),"?"),
	    msg->params().getValue(YSTRING("destination"),"?"),
	    msg->params().getValue(YSTRING("cause"),"?"));
	if (router) {
	    unsigned char part = msg->params().getIntValue(YSTRING("part"),-1);
	    unsigned char cause = msg->params().getIntValue(YSTRING("cause"),-1);
	    SS7PointCode pc;
	    if (part > SS7MSU::MTNS && part <= 0x0f && cause <= 0x0f &&
		pc.assign(msg->params().getValue(YSTRING("destination")),label.type()))
		router->receivedUPU(label.type(),pc,(SS7MSU::Services)part,
		    cause,label,sls);
	}
    }
    else {
	String tmp;
	tmp.hexify((void*)buf,len,' ');
	String params;
	unsigned int n = msg->params().count();
	if (n)
	    for (unsigned int i = 0; i < n; i++) {
		NamedString* ns = static_cast<NamedString*>(msg->params().getParam(i));
		if (ns)
		    params.append(String(ns->name()) + "=" + *ns,",");
	    }
	Debug(this,DebugMild,
	    "Unhandled SNM type=%s group=%s label=%s params:%s len=%u: %s ",
	    msg->name(),lookup(msg->group(),s_snm_group,"Spare"),
	    addr.c_str(),params.c_str(),len,tmp.c_str());
    }

    return true;
}

bool SS7Management::control(NamedList& params)
{
    String* ret = params.getParam(YSTRING("completion"));
    const String* oper = params.getParam(YSTRING("operation"));
    const char* cmp = params.getValue(YSTRING("component"));
    int cmd = -1;
    if (!TelEngine::null(oper)) {
	cmd = oper->toInteger(s_dict_control,cmd);
	if (cmd < 0)
	    cmd = oper->toInteger(s_snm_names,cmd);
    }

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

    m_changeMsgs = params.getBoolValue(YSTRING("changemsgs"),m_changeMsgs);
    m_changeSets = params.getBoolValue(YSTRING("changesets"),m_changeSets);
    m_neighbours = params.getBoolValue(YSTRING("neighbours"),m_neighbours);
    const String* addr = params.getParam(YSTRING("address"));
    if (cmd < 0 || TelEngine::null(addr))
	return SignallingComponent::control(params);
    // TYPE,opc,dpc,sls,spare
    SS7PointCode::Type t = SS7PointCode::Other;
    ObjList* l = addr->split(',');
    if (l->at(0) && (t = SS7PointCode::lookup(l->at(0)->toString())) != SS7PointCode::Other) {
	unsigned char netInd = ni();
	if (network())
	    netInd = network()->getNI(t,netInd);
	unsigned char txSio = getSIO(params,ssf(),prio(),netInd);
	SS7PointCode opc,dpc;
	int sls = -1;
	int spare = 0;
	if (l->at(1) && opc.assign(l->at(1)->toString(),t) &&
	    l->at(2) && dpc.assign(l->at(2)->toString(),t)) {
	    if (l->at(3))
		sls = l->at(3)->toString().toInteger(sls);
	    if (l->at(4))
		spare = l->at(4)->toString().toInteger(spare);
	    TelEngine::destruct(l);
	    SS7Label lbl(t,dpc,opc,sls,spare);
	    int txSls = sls;
	    switch (cmd) {
		case SS7MsgSNM::COO:
		case SS7MsgSNM::COA:
		case SS7MsgSNM::XCO:
		case SS7MsgSNM::XCA:
		case SS7MsgSNM::CBD:
		case SS7MsgSNM::CBA:
		case SS7MsgSNM::LIN:
		case SS7MsgSNM::LIA:
		case SS7MsgSNM::LID:
		case SS7MsgSNM::LUN:
		case SS7MsgSNM::LUA:
		case SS7MsgSNM::LFU:
		    txSls = (txSls + 1) & 0xff;
	    }
	    txSls = params.getIntValue(YSTRING("linksel"),txSls) & 0xff;
	    String tmp;
	    tmp << SS7PointCode::lookup(lbl.type()) << "," << lbl;
	    Debug(this,DebugAll,"Sending %s to %s on %d [%p]",
		SS7MsgSNM::lookup((SS7MsgSNM::Type)cmd),tmp.c_str(),txSls,this);
	    switch (cmd) {
		// Messages containing a destination point code
		case SS7MsgSNM::TFP:
		case SS7MsgSNM::TFA:
		case SS7MsgSNM::TFR:
		case SS7MsgSNM::TFC:
		case SS7MsgSNM::RST:
		case SS7MsgSNM::RSR:
		    {
			addr = params.getParam(YSTRING("destination"));
			SS7PointCode dest(opc);
			if (TelEngine::null(addr) || dest.assign(*addr,t)) {
			    unsigned char data[5];
			    int len = SS7PointCode::length(t)+1;
			    data[0] = cmd;
			    return TelEngine::controlReturn(&params,dest.store(t,data+1,spare) &&
				((cmd == SS7MsgSNM::TFP) ?
				    postpone(new SS7MSU(txSio,lbl,data,len),lbl,txSls,1000) :
				    (transmitMSU(SS7MSU(txSio,lbl,data,len),lbl,txSls) >= 0)));
			}
		    }
		    return TelEngine::controlReturn(&params,false);
		// Messages sent with just the code
		case SS7MsgSNM::ECO:
		case SS7MsgSNM::TRA:
		case SS7MsgSNM::LIA:
		case SS7MsgSNM::LUA:
		case SS7MsgSNM::LID:
		case SS7MsgSNM::LLT:
		case SS7MsgSNM::LRT:
		case SS7MsgSNM::RCT:
		case SS7MsgSNM::CSS:
		case SS7MsgSNM::CNS:
		case SS7MsgSNM::CNP:
		    {
			unsigned char data = cmd;
			return TelEngine::controlReturn(&params,transmitMSU(SS7MSU(txSio,lbl,&data,1),lbl,txSls) >= 0);
		    }
		// Messages postponed with just the code
		case SS7MsgSNM::LIN:
		    {
			unsigned char data = cmd;
			return TelEngine::controlReturn(&params,postpone(new SS7MSU(txSio,lbl,&data,1),lbl,txSls,2500,5000));
		    }
		case SS7MsgSNM::LUN:
		case SS7MsgSNM::LFU:
		    {
			unsigned char data = cmd;
			return TelEngine::controlReturn(&params,postpone(new SS7MSU(txSio,lbl,&data,1),lbl,txSls,1200,2400));
		    }
		// Changeover messages
		case SS7MsgSNM::COO:
		case SS7MsgSNM::COA:
		case SS7MsgSNM::XCO:
		case SS7MsgSNM::XCA:
		    if (params.getBoolValue(YSTRING("emergency"),false)) {
			unsigned char data = (SS7MsgSNM::COO == cmd) ? SS7MsgSNM::ECO : SS7MsgSNM::ECA;
			return TelEngine::controlReturn(&params,transmitMSU(SS7MSU(txSio,lbl,&data,1),lbl,txSls) >= 0);
		    }
		    else {
			int seq = params.getIntValue(YSTRING("sequence"),0) & 0x00ffffff;
			if (SS7MsgSNM::COO == cmd || SS7MsgSNM::COA == cmd)
			    seq &= 0x7f;
			int len = 2;
			unsigned char data[5];
			data[0] = cmd;
			switch (t) {
			    case SS7PointCode::ITU:
				data[1] = (unsigned char)seq;
				if (SS7MsgSNM::XCO == cmd || SS7MsgSNM::XCA == cmd) {
				    data[2] = (unsigned char)(seq >> 8);
				    data[3] = (unsigned char)(seq >> 16);
				    len += 2;
				}
				break;
			    case SS7PointCode::ANSI:
				data[1] = (unsigned char)((params.getIntValue(YSTRING("slc"),sls) & 0x0f) | (seq << 4));
				data[2] = (unsigned char)(seq >> 4);
				len = 3;
				if (SS7MsgSNM::XCO == cmd || SS7MsgSNM::XCA == cmd) {
				    data[3] = (unsigned char)(seq >> 12);
				    data[4] = (unsigned char)(seq >> 20);
				    len += 2;
				}
				break;
			    default:
				Debug(DebugStub,"Please implement COO for type %u",t);
				return TelEngine::controlReturn(&params,false);
			}
			return TelEngine::controlReturn(&params,(cmd == SS7MsgSNM::COA)
			    ? transmitMSU(SS7MSU(txSio,lbl,&data,len),lbl,txSls) >= 0
			    : postpone(new SS7MSU(txSio,lbl,&data,len),lbl,txSls,1800,0,true));
		    }
		// Changeback messages
		case SS7MsgSNM::CBD:
		case SS7MsgSNM::CBA:
		    {
			int code = params.getIntValue(YSTRING("code"),0);
			int len = 2;
			unsigned char data[3];
			data[0] = cmd;
			switch (t) {
			    case SS7PointCode::ITU:
				data[1] = (unsigned char)code;
				break;
			    case SS7PointCode::ANSI:
				data[1] = (unsigned char)((params.getIntValue(YSTRING("slc"),sls) & 0x0f) | (code << 4));
				data[2] = (unsigned char)(code >> 4);
				len = 3;
				break;
			    default:
				Debug(DebugStub,"Please implement CBD for type %u",t);
				return TelEngine::controlReturn(&params,false);
			}
			return TelEngine::controlReturn(&params,(cmd == SS7MsgSNM::CBA)
			    ? transmitMSU(SS7MSU(txSio,lbl,&data,len),lbl,txSls) >= 0
			    : postpone(new SS7MSU(txSio,lbl,&data,len),lbl,txSls,1000,2000,true));
		    }
		default:
		    if (cmd >= 0)
			Debug(this,DebugStub,"Unimplemented control %s (%d) [%p]",
			    lookup(cmd,s_snm_names,"???"),cmd,this);
	    }
	}
    }
    TelEngine::destruct(l);
    return TelEngine::controlReturn(&params,false);
}

void SS7Management::notify(SS7Layer3* network, int sls)
{
    Debug(this,DebugAll,"SS7Management::notify(%p,%d) [%p]",network,sls,this);
    if (network && (sls >= 0)) {
	DDebug(this,DebugInfo,"Link %d inhibitions: 0x%02X [%p]",
	    sls,network->inhibited(sls),this);
	bool linkUp = network->operational(sls);
	if (linkUp && !network->inhibited(sls,SS7Layer2::Inactive))
	    return;
	bool linkAvail[257];
	bool force = true;
	int txSls;
	bool localLink = false;
	for (txSls = 0; m_changeMsgs && (txSls < 256); txSls++)
	    localLink = (linkAvail[txSls] = (txSls != sls) && network->inService(txSls)) || localLink;
	// if no link is available in linkset rely on another linkset
	linkAvail[256] = m_changeSets && !localLink;
	for (unsigned int i = 0; m_changeMsgs && (i < YSS7_PCTYPE_COUNT); i++) {
	    SS7PointCode::Type type = static_cast<SS7PointCode::Type>(i+1);
	    unsigned int local = network->getLocal(type);
	    if (!local && SS7Layer4::network())
		local = SS7Layer4::network()->getLocal(type);
	    if (!local)
		continue;
	    String addr;
	    addr << SS7PointCode::lookup(type) << "," << SS7PointCode(type,local);
	    Debug(this,DebugNote,"Link %s:%d is %s [%p]",addr.c_str(),sls,
		(linkUp ? "up" : "down"),this);
	    const char* oper = linkUp ? "changeback" : "changeover";
	    ObjList* routes = getNetRoutes(network,type);
	    if (routes)
		routes = routes->skipNull();
	    for (; routes; routes = routes->skipNext()) {
		const SS7Route* r = static_cast<const SS7Route*>(routes->get());
		if (r && !r->priority()) {
		    // found adjacent node, emit change orders to it
		    int seq = -1;
		    txSls = 0;
		    if (!linkUp && network->inhibited(sls,SS7Layer2::Inactive)) {
			// already inactive, fix sequences if possible
			seq = network->getSequence(sls);
			DDebug(this,DebugAll,"Got sequence %d for link %s:%d [%p]",
			    seq,addr.c_str(),sls,this);
			if (seq < 0)
			    return;
			txSls = 256;
		    }
		    String tmp = addr;
		    tmp << "," << SS7PointCode(type,r->packed()) << "," << sls;
		    String slc(sls);
		    for (; txSls <= 256; txSls++) {
			if (!linkAvail[txSls])
			    continue;
			NamedList* ctl = controlCreate(oper);
			if (!ctl)
			    continue;
			Debug(this,DebugAll,"Sending Link %d %s %s on %d [%p]",
			    sls,oper,tmp.c_str(),txSls,this);
			ctl->setParam("address",tmp);
			ctl->setParam("slc",slc);
			ctl->setParam("linksel",String(txSls & 0xff));
			if (linkUp)
			    ctl->setParam("code",String((txSls + sls) & 0xff));
			else {
			    if (seq < 0)
				seq = network->getSequence(sls);
			    DDebug(this,DebugAll,"Got sequence number %d [%p]",seq,this);
			    if (seq >= 0)
				ctl->setParam("sequence",String(seq));
			    else
				ctl->setParam("emergency",String::boolText(true));
			}
			ctl->setParam("automatic",String::boolText(true));
			controlExecute(ctl);
			force = false;
		    }
		    while (seq >= 0) {
			// scan pending list for matching ECA, turn them into COA/XCA
			SS7Label label(type,local,r->packed(),sls);
			lock();
			SnmPending* pend = 0;
			for (ObjList* l = m_pending.skipNull(); l; l = l->skipNext()) {
			    SnmPending* p = static_cast<SnmPending*>(l->get());
			    const unsigned char* ptr = p->msu().getData(p->length()+1,1);
			    if (!(ptr && p->matches(label)))
				continue;
			    if (ptr[0] != SS7MsgSNM::ECA)
				continue;
			    pend = static_cast<SnmPending*>(m_pending.remove(p,false));
			    break;
			}
			unlock();
			if (pend) {
			    const char* cmd = "COA";
			    if (seq & 0xff000000) {
				seq &= 0x00ffffff;
				cmd = "XCA";
			    }
			    Debug(this,DebugInfo,"Turning pending ECA into %s with sequence %d [%p]",
				cmd,seq,this);
			    NamedList* ctl = controlCreate(cmd);
			    if (ctl) {
				ctl->setParam("address",tmp);
				ctl->setParam("slc",slc);
				ctl->setParam("linksel",String(pend->txSls()));
				ctl->setParam("sequence",String(seq));
				ctl->setParam("automatic",String::boolText(true));
				controlExecute(ctl);
				force = false;
			    }
			    TelEngine::destruct(pend);
			}
			else
			    break;
		    }
		}
	    }
	}
	if (force) {
	    if (linkUp) {
		Debug(this,DebugMild,"Could not changeback link %d, activating anyway [%p]",sls,this);
		network->inhibit(sls,0,SS7Layer2::Inactive);
	    }
	    else {
		Debug(this,DebugMild,"Could not changeover link %d, deactivating anyway [%p]",sls,this);
		network->inhibit(sls,SS7Layer2::Inactive,0);
	    }
	}
    }
}

bool SS7Management::postpone(SS7MSU* msu, const SS7Label& label, int txSls,
	u_int64_t interval, u_int64_t global, bool force, const Time& when)
{
    lock();
    unsigned int len = msu->length();
    for (ObjList* l = m_pending.skipNull(); l; l = l->skipNext()) {
	SnmPending* p = static_cast<SnmPending*>(l->get());
	if (p->txSls() != txSls || p->msu().length() != len)
	    continue;
	if (::memcmp(msu->data(),p->msu().data(),len))
	    continue;
	const unsigned char* buf = msu->getData(label.length()+1,1);
	Debug(this,DebugAll,"Refusing to postpone duplicate %s on %d",
	    SS7MsgSNM::lookup((SS7MsgSNM::Type)buf[0],"???"),txSls);
	TelEngine::destruct(msu);
	break;
    }
    unlock();
    if (msu && ((interval == 0) || (transmitMSU(*msu,label,txSls) >= 0) || force)) {
	lock();
	m_pending.add(new SnmPending(msu,label,txSls,interval,global),when);
	unlock();
	return true;
    }
    TelEngine::destruct(msu);
    return false;
}

bool SS7Management::timeout(const SS7MSU& msu, const SS7Label& label, int txSls, bool final)
{
    DDebug(this,DebugAll,"Timeout %u%s [%p]",txSls,(final ? " final" : ""),this);
    if (!final)
	return true;
    const unsigned char* buf = msu.getData(label.length()+1,1);
    if (!buf)
	return false;
    String link;
    link << SS7PointCode::lookup(label.type()) << "," << label;
    switch (buf[0]) {
	case SS7MsgSNM::COO:
	case SS7MsgSNM::XCO:
	case SS7MsgSNM::ECO:
	    Debug(this,DebugNote,"Changeover timed out on %s",link.c_str());
	    inhibit(label,SS7Layer2::Inactive);
	    break;
	case SS7MsgSNM::ECA:
	    Debug(this,DebugNote,"Emergency changeover acknowledge on %s",link.c_str());
	    transmitMSU(msu,label,txSls);
	    break;
	case SS7MsgSNM::CBD:
	    Debug(this,DebugNote,"Changeback timed out on %s",link.c_str());
	    inhibit(label,0,SS7Layer2::Inactive);
	    break;
	case SS7MsgSNM::LIN:
	    Debug(this,DebugWarn,"Link inhibit timed out on %s",link.c_str());
	    break;
	case SS7MsgSNM::LUN:
	    Debug(this,DebugWarn,"Link uninhibit timed out on %s",link.c_str());
	    break;
	case SS7MsgSNM::LRT:
	    if (inhibited(label,SS7Layer2::Remote))
		postpone(new SS7MSU(msu),label,txSls,TIMER5M);
	    break;
	case SS7MsgSNM::LLT:
	    if (inhibited(label,SS7Layer2::Local))
		postpone(new SS7MSU(msu),label,txSls,TIMER5M);
	    break;
	case SS7MsgSNM::TFP:
	    return false;
    }
    return true;
}

bool SS7Management::timeout(SignallingMessageTimer& timer, bool final)
{
    SnmPending& msg = static_cast<SnmPending&>(timer);
    if (final) {
	String addr;
	addr << msg;
	Debug(this,DebugInfo,"Expired %s control sequence to %s [%p]",
	    msg.snmName(),addr.c_str(),this);
    }
    return timeout(msg.msu(),msg,msg.txSls(),final);
}

void SS7Management::timerTick(const Time& when)
{
    for (;;) {
	if (!lock(SignallingEngine::maxLockWait()))
	    break;
	SnmPending* msg = static_cast<SnmPending*>(m_pending.timeout(when));
	unlock();
	if (!msg)
	    break;
	if (!msg->global().started() || msg->global().timeout(when.msec()))
	    timeout(*msg,true);
	else if (timeout(*msg,false)) {
	    transmitMSU(msg->msu(),*msg,msg->txSls());
	    m_pending.add(msg,when);
	    msg = 0;
	}
	TelEngine::destruct(msg);
    }
}

bool SS7Management::inhibit(const SS7Label& link, int setFlags, int clrFlags)
{
    SS7Router* router = YOBJECT(SS7Router,SS7Layer4::network());
    return router && router->inhibit(link,setFlags,clrFlags);
}

bool SS7Management::inhibited(const SS7Label& link, int flags)
{
    SS7Router* router = YOBJECT(SS7Router,SS7Layer4::network());
    return router && router->inhibited(link,flags);
}

void SS7Management::recover(const SS7Label& link, int sequence)
{
    SS7Router* router = YOBJECT(SS7Router,SS7Layer4::network());
    if (router)
	router->recoverMSU(link,sequence);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
