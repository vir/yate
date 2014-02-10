/**
 * ccongestion.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Update call accept engine status from installed engine's monitors
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

#include <yatephone.h>

using namespace TelEngine;
namespace { // anonymous

class Monitor : public String
{
public:
    inline Monitor (const String& name)
	: String(name),m_value(0) {}
    inline void update(int value)
	{ m_value = value; }
    inline int getValue()
	{ return m_value; }
private:
    int m_value;
};

/*
NOTE!!! This module use YATE engine behavior. It expects that first engine worker thread
is created after all modules has been initialized
NOTE: when this module is initialized, from the conf file are created
chan.control messages that are enqueued and delivered to cpuload module when
the first engine worker thread is created.
*/
class CongestionModule : public Module
{
public:
    CongestionModule();
    ~CongestionModule();
    virtual void initialize();
    // Update a monitor state. If monitor does not exists, it will be appended
    void updateMonitor(const String& name, const String& step);
    // Find worst state and update engine's state
    void updateEngine();
private:
    bool m_init;
    ObjList m_monitors;
    Mutex m_monitorsBlocker;
};

static CongestionModule s_module;
static const char* s_mutexName = "CCongestion";


class CpuNotify : public MessageHandler
{
public:
    inline CpuNotify()
	: MessageHandler("monitor.notify",100,s_module.name())
	{ }
    virtual bool received(Message& m);
};


/**
 * class CpuNotify
 */

bool CpuNotify::received(Message& msg)
{
    int count = msg.getIntValue("count",0);
    String monitor;
    String newVal;
    const String param = "notify.";
    const String paramValue = "value.";
    for (int i = 0; i < count; i++) {
	const String& notif = msg[param + String(i)];
	const String& value = msg[paramValue + String(i)];
	if (notif == YSTRING("target") && value != YSTRING("engine"))
	    return false;
	if (notif == YSTRING("monitor"))
	    monitor = value;
	else if (notif == YSTRING("new"))
	    newVal = value;
    }
    s_module.updateMonitor(monitor,newVal);
    s_module.updateEngine();
    return false;
}

/**
 * Class CongestionModule
 */

CongestionModule::CongestionModule()
    : Module("ccongestion","misc"), m_init(false), m_monitorsBlocker(false,s_mutexName)
{
    Output("Loaded module CCongestion");
}

CongestionModule::~CongestionModule()
{
    Output("Unloading module CCongestion");
}

void CongestionModule::initialize()
{
    Output("Initializing module CCongestion");
    Configuration cfg(Engine::configFile("ccongestion"));
    if (!m_init) {
	m_init = true;
	Engine::install(new CpuNotify());
    }
    m_monitorsBlocker.lock();
    m_monitors.clear();
    m_monitorsBlocker.unlock();
    NamedList* cpu = cfg.getSection("cpu");
    if (cpu) {
	for (unsigned int i = 0;i < cpu->count();i++) {
	    NamedString* ns = cpu->getParam(i);
	    if (!ns)
		continue;
	    Message* m = new Message("chan.control");
	    m->addParam("targetid","cpuload");
	    m->addParam("component","cpuload");
	    m->addParam("operation",ns->name());
	    m->addParam("cpu.engine",*ns);
	    Engine::enqueue(m);
	}
    }
}

void CongestionModule::updateMonitor(const String& name, const String& value)
{
    int val = lookup(value,Engine::getCallAcceptStates(),Engine::Accept);
    Lock lock(m_monitorsBlocker);
    ObjList* o = m_monitors.find(name);
    if (o) {
	Monitor* mon = static_cast<Monitor*>(o->get());
	if (mon)
	    mon->update(val);
	return;
    }
    Monitor* mon = new Monitor(name);
    mon->update(val);
    m_monitors.append(mon);
}

void CongestionModule::updateEngine()
{
    Lock lock(m_monitorsBlocker);
    int val = 0;
    for (ObjList* o = m_monitors.skipNull();o;o = o->skipNext()) {
	Monitor* mon = static_cast<Monitor*>(o->get());
	if (!mon)
	    continue;
	if (mon->getValue() > val)
	    val = mon->getValue();
    }
    if (Engine::accept() == val)
	return;
    Engine::setAccept((Engine::CallAccept)val);
    DDebug(this,DebugInfo,"Updating cpu state to %s",lookup(val,Engine::getCallAcceptStates()));
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
