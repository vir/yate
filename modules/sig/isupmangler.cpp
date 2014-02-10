/**
 * isupmangler.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * ISUP parameter mangling in a STP
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

#include <yatephone.h>
#include <yatesig.h>

using namespace TelEngine;
namespace { // anonymous

class IsupIntercept : public SS7ISUP
{
    friend class IsupMangler;
    YCLASS(IsupIntercept,SS7ISUP)
public:
    enum What {
	None = 0, // No messages, just mangling
	Iam,      // IAM only
	Cdr,      // IAM,SAM,ACM,CPG,ANM,CON,SUS,RES,REL,RLC
	All
    };
    inline IsupIntercept(const NamedList& params)
	: SignallingComponent(params,&params), SS7ISUP(params),
	  m_used(true), m_symmetric(false), m_what(Iam),
	  m_cicMin(1), m_cicMax(16383),
	  m_setOpc(0), m_setDpc(0), m_setSls(-2), m_setCic(0),
	  m_resend(true)
	{ }
    virtual bool initialize(const NamedList* config);
    void dispatched(SS7MsgISUP& isup, const Message& msg, const SS7Label& label, int sls, bool accepted);
protected:
    virtual HandledMSU receivedMSU(const SS7MSU& msu, const SS7Label& label,
	SS7Layer3* network, int sls);
    virtual bool processMSU(SS7MsgISUP::Type type, unsigned int cic,
        const unsigned char* paramPtr, unsigned int paramLen,
	const SS7Label& label, SS7Layer3* network, int sls);
    bool shouldIntercept(SS7MsgISUP::Type type) const;
private:
    bool m_used;
    bool m_symmetric;
    What m_what;
    unsigned int m_cicMin, m_cicMax;
    int m_setOpc, m_setDpc, m_setSls, m_setCic;
    bool m_resend;
};

class IsupMessage : public Message
{
public:
    inline IsupMessage(const char* name, IsupIntercept* isup, SS7MsgISUP* msg,
	const SS7Label& label, int sls)
	: Message(name),
	  m_isup(isup), m_msg(msg), m_lbl(label), m_sls(sls), m_accepted(false)
	{ }
    inline ~IsupMessage()
	{ if (m_isup && m_msg) m_isup->dispatched(*m_msg,*this,m_lbl,m_sls,m_accepted); }
protected:
    virtual void dispatched(bool accepted)
	{ m_accepted = accepted; }
private:
    RefPointer<IsupIntercept> m_isup;
    RefPointer<SS7MsgISUP> m_msg;
    SS7Label m_lbl;
    int m_sls;
    bool m_accepted;
};

class IsupMangler : public Plugin
{
public:
    inline IsupMangler()
	: Plugin("isupmangler")
	{ Output("Loaded module ISUP Mangler"); }
    inline ~IsupMangler()
	{ Output("Unloading module ISUP Mangler"); }
    virtual void initialize();
};

static const TokenDict s_dict_what[] = {
    { "nothing", IsupIntercept::None },
    { "none", IsupIntercept::None },
    { "IAM", IsupIntercept::Iam },
    { "iam", IsupIntercept::Iam },
    { "CDR", IsupIntercept::Cdr },
    { "cdr", IsupIntercept::Cdr },
    { "All", IsupIntercept::All },
    { "all", IsupIntercept::All },
    { 0, 0 }
};

static const TokenDict s_dict_pc[] = {
    { "mirror", -1 },
    { 0, 0 }
};

static const TokenDict s_dict_sls[] = {
    { "cic", -1 },
    { "circuit", -1 },
    { 0, 0 }
};

static ObjList s_manglers;

INIT_PLUGIN(IsupMangler);


bool IsupIntercept::initialize(const NamedList* config)
{
    if (!config)
	return false;
    SS7ISUP::initialize(config);
    SS7Router* router = YOBJECT(SS7Router,network());
    m_resend = config->getBoolValue("resend",!(router && router->transferring()));
    m_symmetric = config->getBoolValue("symmetric",m_symmetric);
    m_what = (What)config->getIntValue("intercept",s_dict_what,m_what);
    m_cicMin = config->getIntValue("cic_min",m_cicMin);
    m_cicMax = config->getIntValue("cic_max",m_cicMax);
    m_setOpc = config->getIntValue("set:opc",s_dict_pc,m_setOpc);
    m_setDpc = config->getIntValue("set:dpc",s_dict_pc,m_setDpc);
    m_setSls = config->getIntValue("set:sls",s_dict_sls,m_setSls);
    m_setCic = config->getIntValue("set:cic",m_setCic);
    Debug(this,DebugAll,"Added %u Point Codes, intercepts %s %s, cic=%u-%u",
	setPointCode(*config),lookup(m_what,s_dict_what,"???"),
	(m_symmetric) ? "both ways" : "one way",
	m_cicMin,m_cicMax);
    return true;
}

bool IsupIntercept::shouldIntercept(SS7MsgISUP::Type type) const
{
    switch (type) {
	// almost always intercept IAM
	case SS7MsgISUP::IAM:
	    return (m_what >= Iam);
	// other CDR relevant messages
	case SS7MsgISUP::SAM:
	case SS7MsgISUP::ACM:
	case SS7MsgISUP::CPG:
	case SS7MsgISUP::ANM:
	case SS7MsgISUP::CON:
	case SS7MsgISUP::SUS:
	case SS7MsgISUP::RES:
	case SS7MsgISUP::REL:
	case SS7MsgISUP::RLC:
	    return (m_what >= Cdr);
	// we shouldn't mess with these messages
	case SS7MsgISUP::UPT:
	case SS7MsgISUP::UPA:
	case SS7MsgISUP::NRM:
	case SS7MsgISUP::PAM:
	case SS7MsgISUP::CNF:
	case SS7MsgISUP::USR:
	    return false;
	// intercepting all messages is risky
	default:
	    return (m_what >= All);
    }
}

HandledMSU IsupIntercept::receivedMSU(const SS7MSU& msu, const SS7Label& label, SS7Layer3* network, int sls)
{
    if (msu.getSIF() != sif())
	return HandledMSU::Rejected;
    if (!hasPointCode(label.dpc()) || !handlesRemotePC(label.opc())) {
	if (!m_symmetric || !hasPointCode(label.opc()) || !handlesRemotePC(label.dpc()))
	    return HandledMSU::Rejected;
    }
    // horrible - create a pair of writable aliases to alter data in place
    SS7MSU& rwMsu = const_cast<SS7MSU&>(msu);
    SS7Label& rwLbl = const_cast<SS7Label&>(label);
    // we should have at least 2 bytes CIC and 1 byte message type
    unsigned char* s = rwMsu.getData(label.length()+1,3);
    if (!s) {
	Debug(this,DebugNote,"Got short MSU");
	return HandledMSU::Rejected;
    }
    unsigned int len = msu.length()-label.length()-1;
    unsigned int cic = s[0] | (s[1] << 8);
    if (cic < m_cicMin || cic > m_cicMax)
	return HandledMSU::Rejected;

    SS7MsgISUP::Type type = (SS7MsgISUP::Type)s[2];
    String name = SS7MsgISUP::lookup(type);
    if (!name) {
        String tmp;
	tmp.hexify((void*)s,len,' ');
	Debug(this,DebugMild,"Received unknown ISUP type 0x%02x, cic=%u, length %u: %s",
	    type,cic,len,tmp.c_str());
	name = (int)type;
    }
    XDebug(this,DebugAll,"Received ISUP type %s, cic=%u, length %u",name.c_str(),cic,len);

    // intercepted as message or not, apply mangling now
    if (m_setCic) {
	cic += m_setCic;
	s[0] = (cic & 0xff);
	s[1] = ((cic >> 8) & 0xff);
    }
    bool changed = false;
    if (m_setSls >= -1) {
	changed = true;
	rwLbl.setSls(((m_setSls >= 0) ? m_setSls : cic) & 0xff);
    }
    if (m_setOpc || m_setDpc) {
	changed = true;
	SS7PointCode opc(label.opc());
	SS7PointCode dpc(label.dpc());
	if (m_setOpc > 0)
	    rwLbl.opc().unpack(label.type(),m_setOpc);
	else if (m_setOpc < 0)
	    rwLbl.opc() = dpc;
	if (m_setDpc > 0)
	    rwLbl.dpc().unpack(label.type(),m_setDpc);
	else if (m_setDpc < 0)
	    rwLbl.dpc() = opc;
    }
    if (changed)
	rwLbl.store(rwMsu.getData(1));

    if (shouldIntercept(type) && processMSU(type,cic,s+3,len-3,label,network,sls))
	return HandledMSU::Accepted;
    if (!(m_setDpc || m_resend))
	return HandledMSU::Rejected;
    // if we altered the DPC or we are no STP we should transmit as new message
    if (transmitMSU(rwMsu,rwLbl,rwLbl.sls()) >= 0)
	return HandledMSU::Accepted;
    Debug(this,DebugWarn,"Failed to forward mangled %s (%u) [%p]",
	SS7MsgISUP::lookup(type),cic,this);
    return HandledMSU::Failure;
}

bool IsupIntercept::processMSU(SS7MsgISUP::Type type, unsigned int cic,
    const unsigned char* paramPtr, unsigned int paramLen,
    const SS7Label& label, SS7Layer3* network, int sls)
{
    XDebug(this,DebugAll,"IsupIntercept::processMSU(%u,%u,%p,%u,%p,%p,%d) [%p]",
	type,cic,paramPtr,paramLen,&label,network,sls,this);

    SS7MsgISUP* msg = new SS7MsgISUP(type,cic);
    if (!SS7MsgISUP::lookup(type)) {
	String tmp;
	tmp.hexify(&type,1);
	msg->params().assign("Message_" + tmp);
    }
    if (!decodeMessage(msg->params(),type,label.type(),paramPtr,paramLen)) {
	TelEngine::destruct(msg);
	return false;
    }

    if (debugAt(DebugAll)) {
	String tmp;
	tmp << label;
	Debug(this,DebugAll,"Received message '%s' cic=%u label=%s",
	    msg->name(),msg->cic(),tmp.c_str());
    }
    IsupMessage* m = new IsupMessage("isup.mangle",this,msg,label,sls);
    String addr;
    addr << toString() << "/" << cic;
    m->addParam("address",addr);
    m->addParam("dpc",String(label.dpc().pack(label.type())));
    m->addParam("opc",String(label.opc().pack(label.type())));
    m->addParam("sls",String(label.sls()));
    m->addParam("slc",String(sls));
    m->addParam("cic",String(cic));
    m->copyParams(msg->params());
    TelEngine::destruct(msg);
    return Engine::enqueue(m);
}

void IsupIntercept::dispatched(SS7MsgISUP& isup, const Message& msg, const SS7Label& label, int sls, bool accepted)
{
    SS7MSU* msu = createMSU(isup.type(),ssf(),label,isup.cic(),&msg);
    if (!msu || (transmitMSU(*msu,label,sls) < 0))
	Debug(this,DebugWarn,"Failed to forward mangled %s (%u) [%p]",
	    SS7MsgISUP::lookup(isup.type()),isup.cic(),this);
    TelEngine::destruct(msu);
}


void IsupMangler::initialize()
{
    Output("Initializing module ISUP Mangler");
    SignallingEngine* engine = SignallingEngine::self();
    if (!engine) {
	Debug(DebugWarn,"SignallingEngine not yet created, cannot install ISUP manglers [%p]",this);
	return;
    }
    unsigned int n = s_manglers.length();
    unsigned int i;
    for (i = 0; i < n; i++) {
	IsupIntercept* isup = YOBJECT(IsupIntercept,s_manglers[i]);
	if (isup)
	    isup->m_used = false;
    }
    Configuration cfg(Engine::configFile("isupmangler"));
    n = cfg.sections();
    for (i = 0; i < n; i++) {
	NamedList* sect = cfg.getSection(i);
	if (TelEngine::null(sect))
	    continue;
	if (!sect->getBoolValue("enable",true))
	    continue;
	IsupIntercept* isup = YOBJECT(IsupIntercept,s_manglers[*sect]);
	if (!isup) {
	    isup = new IsupIntercept(*sect);
	    engine->insert(isup);
	    s_manglers.append(isup);
	}
	isup->m_used = true;
	isup->initialize(sect);
    }
    ListIterator iter(s_manglers);
    while (IsupIntercept* isup = YOBJECT(IsupIntercept,iter.get())) {
	if (!isup->m_used)
	    s_manglers.remove(isup);
    }
}

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow)
	s_manglers.clear();
    return true;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
