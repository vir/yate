/**
 * main-ss7test.cpp
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

using namespace TelEngine;

class FakeL2 : public SS7Layer2
{
public:
    virtual bool operational() const
	{ return true; }
    virtual bool transmitMSU(const SS7MSU& msu)
	{ return false; }
    virtual ObjList* recoverMSU()
	{ return 0; }
    inline bool fakeMSU(const SS7MSU& msu)
	{
	    String tmp;
	    tmp.hexify(msu.data(),msu.length(),' ');
	    DDebug(DebugInfo,"Fake MSU: 0 %s",tmp.c_str());
	    return receivedMSU(msu);
	}
};

int main()
{
    Debugger::enableOutput(true,true);
    debugLevel(DebugAll);
    Output("SS7 library test starting");
    SS7PointCode scp(2,141,4);
    String s;
    s << "Point code " << scp.pack(SS7PointCode::ITU) << " is " << scp;
    Output("%s",s.c_str());
    s.clear();
    scp.unpack(SS7PointCode::ITU,2114);
    s << "Point code " << scp.pack(SS7PointCode::ITU) << " is " << scp;
    Output("%s",s.c_str());
    SignallingEngine* engine = new SignallingEngine;
    NamedList params("");
    SS7Router* router = new SS7Router(params);
    engine->insert(router);
    // create a MTP3 with default type ITU
    params.addParam("pointcodetype","ITU");
    SS7MTP3* network = new SS7MTP3(params);
    // set a different type for international
    network->setType(SS7PointCode::ANSI,SS7MSU::International);
    router->attach(network);
    SS7MTP2* link = new SS7MTP2(params);
    network->attach(link);
    SS7ISUP* isup = new SS7ISUP(params);
    router->attach(isup);
    FakeL2* fl2 = new FakeL2;
    network->attach(fl2);
    NamedList ifdefs("WpInterface");
    ifdefs.addParam("card","wanpipe1");
    ifdefs.addParam("device","w1g1");
    SignallingInterface* iface = YSIGCREATE(SignallingInterface,&ifdefs);
    if (iface) {
	link->SignallingReceiver::attach(iface);
	iface->control(SignallingInterface::Enable);
    }
    else
	Debug(DebugWarn,"Failed to create '%s'",ifdefs.c_str());
    engine->start("SS7test",Thread::Normal,20000);
    Thread::msleep(500);
    // this MSU is adapted from ethereal's BICC sample
    unsigned char buf[] = {
0x85, 0x42, 0x48, 0x10, 0x02, 0x12, 0x00, 0x01, 0x10, 0x60, 0x01, 0x0a,
0x00, 0x02, 0x06, 0x04, 0x02, 0x10, 0x08, 0x91, 0x0a, 0x08, 0x03, 0x13,
0x31, 0x04, 0x08, 0x00, 0x10, 0xf8, 0x08, 0x01, 0x80, 0x1d, 0x03, 0x80,
0x90, 0xa3, 0x3f, 0x07, 0x04, 0x13, 0x68, 0x31, 0x04, 0x80, 0x88, 0x78,
0xc6, 0x85, 0x81, 0xc0, 0x00, 0x00, 0x01, 0x82, 0x83, 0x02, 0x02, 0x83,
0x83, 0x9c, 0x88, 0x04, 0x8d, 0x85, 0x05, 0x85, 0x85, 0x02, 0x05, 0x80,
0x80, 0x05, 0x83, 0x85, 0x01, 0x01, 0x07, 0x82, 0x83, 0x04, 0x08, 0x1e,
0x81, 0x83, 0x20, 0x20, 0x76, 0x3d, 0x30, 0x0d, 0x0a, 0x6f, 0x3d, 0x2d,
0x20, 0x30, 0x20, 0x31, 0x20, 0x49, 0x4e, 0x20, 0x49, 0x50, 0x34, 0x20,
0x31, 0x39, 0x32, 0x2e, 0x31, 0x36, 0x38, 0x2e, 0x31, 0x38, 0x39, 0x2e,
0x32, 0x30, 0x30, 0x0d, 0x0a, 0x73, 0x3d, 0x30, 0x0d, 0x0a, 0x63, 0x3d,
0x49, 0x4e, 0x20, 0x49, 0x50, 0x34, 0x20, 0x31, 0x39, 0x32, 0x2e, 0x31,
0x36, 0x38, 0x2e, 0x31, 0x38, 0x39, 0x2e, 0x32, 0x30, 0x30, 0x0d, 0x0a,
0x74, 0x3d, 0x30, 0x20, 0x30, 0x0d, 0x0a, 0x61, 0x3d, 0x69, 0x70, 0x62,
0x63, 0x70, 0x3a, 0x31, 0x20, 0x52, 0x65, 0x71, 0x75, 0x65, 0x73, 0x74,
0x0d, 0x0a, 0x6d, 0x3d, 0x61, 0x75, 0x64, 0x69, 0x6f, 0x20, 0x34, 0x30,
0x30, 0x37, 0x32, 0x20, 0x52, 0x54, 0x50, 0x2f, 0x41, 0x56, 0x50, 0x20,
0x31, 0x30, 0x30, 0x0d, 0x0a, 0x61, 0x3d, 0x72, 0x74, 0x70, 0x6d, 0x61,
0x70, 0x3a, 0x31, 0x30, 0x30, 0x20, 0x56, 0x4e, 0x44, 0x2e, 0x33, 0x47,
0x50, 0x50, 0x2e, 0x49, 0x55, 0x46, 0x50, 0x2f, 0x31, 0x36, 0x30, 0x30,
0x30, 0x0d, 0x0a, 0x09, 0x82, 0x83, 0x01, 0x00
    };
    SS7MSU msu(buf,sizeof(buf));
    fl2->fakeMSU(msu);
    Thread::msleep(100);
    SS7Label label(SS7PointCode::ANSI,1234,1256,0);
    NamedList list("");
    list.addParam("CalledPartyNumber","40218989989.");
    list.addParam("CalledPartyNumber.nature","4");
    list.addParam("CalledPartyNumber.plan","private");
    list.addParam("ForwardCallIndicators","international");
    list.addParam("OptionalForwardCallIndicators","CUG+out,CLIR-requested");
    list.addParam("NatureOfConnectionIndicators","cont-check-this,echodev");
    list.addParam("CallingPartyCategory","10");
    list.addParam("CallingPartyNumber","12345");
    list.addParam("CallingPartyNumber.complete","false");
    list.addParam("CallingPartyNumber.restrict","restricted");
    list.addParam("CallingPartyNumber.screened","network-provided");
    list.addParam("NoSuchParameter","ignore me!");
    SS7MSU* iam = isup->createMSU(SS7MsgISUP::IAM,SS7MSU::International,label,11,&list);
    if (iam) {
	fl2->fakeMSU(*iam);
	delete iam;
    }
    Thread::msleep(500);
    delete engine;
    Output("SS7 library test stopped");
    return 0;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
