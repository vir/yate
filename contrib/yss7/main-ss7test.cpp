#include "yatess7.h"

using namespace TelEngine;

int main()
{
    Debugger::enableOutput(true,true);
    debugLevel(DebugAll);
    Output("SS7 library test starting");
    SS7CodePoint scp(2,141,4);
    String s;
    s << "Code point " << scp.pack(SS7CodePoint::ITU) << " is " << scp;
    Output(s);
    s.clear();
    scp.unpack(SS7CodePoint::ITU,2114);
    s << "Code point " << scp.pack(SS7CodePoint::ITU) << " is " << scp;
    Output(s);
    SignallingEngine* engine = new SignallingEngine;
    SS7Router* router = new SS7Router;
    engine->insert(router);
    SS7MTP3* network = new SS7MTP3;
    router->attach(network);
    SS7MTP2* link = new SS7MTP2;
    network->attach(link);
    NamedList ifdefs("WpInterface");
    ifdefs.addParam("card","wanpipe1");
    ifdefs.addParam("device","w1g1");
    SignallingInterface* iface = static_cast<SignallingInterface*>(SignallingFactory::build(ifdefs,&ifdefs));
    if (iface) {
	link->SignallingReceiver::attach(iface);
	iface->control(SignallingInterface::Enable);
    }
    else
	Debug(DebugWarn,"Failed to create '%s'",ifdefs.c_str());
    engine->start("SS7test",Thread::Normal,20000);
    Thread::msleep(100);
    delete engine;
    Output("SS7 library test stopped");
    return 0;
}
