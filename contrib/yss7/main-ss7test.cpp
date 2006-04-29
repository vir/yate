#include "yatess7.h"

using namespace TelEngine;

int main()
{
    debugLevel(DebugAll);
    Output("SS7 library test starting");
    SignallingEngine* engine = new SignallingEngine;
    SS7Router* router = new SS7Router;
    engine->insert(router);
    SS7MTP3* network = new SS7MTP3;
    router->attach(network);
    SS7MTP2* link = new SS7MTP2;
    network->attach(link);
    engine->start("SS7test",Thread::Normal,20000);
    Thread::msleep(100);
    delete engine;
    Output("SS7 library test stopped");
}
