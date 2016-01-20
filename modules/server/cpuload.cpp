/**
 * cpuload.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Monitor CPU load and inform YATE about it
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

#include <yatengine.h>
#include <yatephone.h>
#include <string.h>
#include <unistd.h>

using namespace TelEngine;
namespace { // anonymous

class Cpu
{
public:
    Cpu();
    inline virtual ~Cpu() {}
    // Default implementation updates system load from "/proc/stat"
    inline virtual int getSystemLoad()
	{
	    Debug("CpuLoad",DebugStub,"System CPU load is not implemented"
		    " for this OS");
	    return -1;
	}
    // Updates yate CPU load information
    void updateYateLoad();
    // Set the number of cores.
    inline void setCore(int core)
	{ m_coreNumber = core; m_cpuDiscovered = false; }
    inline int getYateLoad()
	{ return (m_loadY + 50)/ 100; }
    inline int getYateUserLoad()
	{ return (m_loadYU + 50) / 100; }
    inline int getYateKernelLoad()
	{ return (m_loadYS + 50) / 100; }
    inline int getSLoad()
	{ return m_loadSystem < 0? m_loadSystem : (m_loadSystem + 50) / 100; }

protected:
    u_int64_t m_yateUser;      // Yate last time counter spend in user mode
    u_int64_t m_yateSystem;    // Yate last time counter spent in kernel mode
    u_int64_t m_sysUser;       // Last time value of system spent in user mode
    u_int64_t m_sysKer;        // Last time value of system spent in kernel mode
    u_int64_t m_sysNice;       // Last time value of system spent in nice mode
    int m_loadYU;        // Current Yate user CPU loading
    int m_loadYS;        // Current Yate kernel CPU loading
    int m_loadY;         // Current Yate CPU loading
    int m_loadSystem;    // Current System CPU loading
    int m_coreNumber;    // The number of CPU cores
    u_int64_t m_lastYateCheck;   // Last time when we checked for Yate CPU load
    u_int64_t m_lastSystemCheck; // Last time when we checked for System CPU load
    bool m_cpuDiscovered;        // Flag used in CPU core discovery
};

#ifdef _SC_CLK_TCK
class CpuStat : public Cpu
{
public:
    inline CpuStat() {}
    virtual int getSystemLoad();
    void close(File* f);
};
#endif

// Platform depended System CPU loading
class CpuPlatform : public Cpu
{
public:
    inline CpuPlatform() {}
    virtual int getSystemLoad();
};

class Interval : public String
{
public:
    Interval(const String& name, int up, int threshold, int down);
    bool hasValue(int value);
    inline int getThreshold()
	{ return m_threshold; }
    inline int getUp()
	{ return m_up; }
    inline int getDown()
	{ return m_down; }
private:
    int m_up;
    int m_threshold;
    int m_down;
};

class Target : public String
{
public:
    Target(const String& name, int osTimer, const String& monitor);
    virtual ~Target();
    void updateIntervals(const String& curent);
    void handleOscillation(const String& interval,int load);
    Interval* getInterval(int load);
    void sendNotify(int load);
    bool neighbors();
    inline void addInterval(Interval* i, bool ascendent)
	{ ascendent ? m_ascendent.append(i) : m_descendent.append(i); }
    inline void startTimer()
	{ m_oscillationEnd = m_oscillationTimeout == 0 ?
	    0 : Time::msecNow() + m_oscillationTimeout; }
    inline bool needInform()
	{ return Time::msecNow() >= m_oscillationEnd; }
    void manageLoad(int load);
    inline unsigned int getIntervalsCount()
	{ return m_ascendent.count(); }
private:
    String m_currentInterval;
    String m_previewsInterval;
    String m_lastNotified;
    String m_monitor;
    ObjList m_ascendent;
    ObjList m_descendent;
    u_int64_t m_oscillationEnd;
    int m_oscillationTimeout;
    int m_counter;
};

class CpuMonitor : public String
{
public:
    CpuMonitor(const String& name);
    virtual ~CpuMonitor();
    void initialize(const NamedList& params, int osTimer);
    void manageLoad(int load);
    bool addTarget(const NamedString& inc, int osTimer);
    inline void clearTargets()
	{ m_targets.clear(); }
private:
    ObjList m_targets;
    bool m_informed;
};

class CpuUpdater : public Thread, public Mutex
{
public:
    enum Monitors {
	YateUser,
	YateKernel,
	YateTotal,
	System,
	Unknown
    };
    CpuUpdater();
    virtual void run();
    void setCpu(Cpu* cpu);
    void initialize(const Configuration& params);
    inline void requestExit()
	{ m_exit = true; }
    bool update(Message& msg);
    inline Cpu* getCpu()
	{ return m_cpu; }
private:
    int m_updateInterval;    // The interval to update CPU load
    int m_oscillationTimer;  // Default value 5000
    int m_coreNumber;
    Cpu* m_cpu;
    bool m_exit;
    bool m_systemCpuSupport;
    CpuMonitor m_yateUser;
    CpuMonitor m_yateSys;
    CpuMonitor m_yateTotal;
    CpuMonitor m_system;
};

class CpuModule : public Module
{
public:
    CpuModule();
    ~CpuModule();
    virtual void initialize();
    virtual bool received(Message& m, int id);
    inline CpuUpdater* getUpdater()
	{ return m_updater; }
private:
    CpuUpdater* m_updater;
    bool m_init;
};

static CpuModule s_module;
static String s_address = "/proc/stat";
static int s_defaultHysteresis = 2;
static int s_bufLen = 4096;
static int s_smooth = 33;

class QueryHandler : public MessageHandler
{
public:
    inline QueryHandler(unsigned int priority = 100)
	: MessageHandler("monitor.query",priority,s_module.name())
	{ }
    virtual ~QueryHandler()
	{ }
    virtual bool received(Message& msg);
};

static TokenDict s_monitors[] = {
    {"userLoad",   CpuUpdater::YateUser},
    {"kernelLoad", CpuUpdater::YateKernel},
    {"totalLoad",  CpuUpdater::YateTotal},
    {"systemLoad", CpuUpdater::System},
    {0,0}
};

/**
 * Class CpuUpdater
 */

static String s_mutexName = "CpuMutex";

CpuUpdater::CpuUpdater()
    : Thread("CpuThread",Thread::Normal), Mutex(false,s_mutexName), m_updateInterval(1000),
    m_oscillationTimer(5000), m_coreNumber(1), m_cpu(0), m_exit(false), m_systemCpuSupport(true),
    m_yateUser(s_monitors[0].token), m_yateSys(s_monitors[1].token),
    m_yateTotal(s_monitors[2].token), m_system(s_monitors[3].token)
{
    DDebug(&s_module,DebugAll,"Creating CpuUpdater thread");
}

void CpuUpdater::run()
{
    int time = 0;//m_updateInterval;
    while (!m_exit) {
	if (time < m_updateInterval) {
	    Thread::msleep(50);
	    time += 50;
	    continue;
	}
	time = 0;
	Lock lock(this);
	m_cpu->updateYateLoad();
	m_yateUser.manageLoad(m_cpu->getYateUserLoad());
	m_yateSys.manageLoad(m_cpu->getYateKernelLoad());
	m_yateTotal.manageLoad(m_cpu->getYateLoad());
	if (!m_systemCpuSupport)
	    continue;
	int sys = m_cpu->getSystemLoad();
	if (sys < 0) {
	    Debug(&s_module,DebugNote,"System Cpu load not supported!");
	    m_systemCpuSupport = false;
	    continue;
	}
	m_system.manageLoad(sys);
	XDebug(&s_module,DebugAll,"YateLoading is : yu %d ; ys %d ; y %d ; s %d",
	       m_cpu->getYateUserLoad(),m_cpu->getYateKernelLoad(),m_cpu->getYateLoad(),sys);
    }
    delete m_cpu;
}

void CpuUpdater::setCpu(Cpu* cpu)
{
    if (!cpu)
	return;
    m_cpu = cpu;
    m_cpu->setCore(m_coreNumber);
}

// This method appends a target from chan.control message
bool CpuUpdater::update(Message& msg)
{
    const String& mon = msg[YSTRING("operation")];
    NamedString* inc = 0;
    for (unsigned int i = 0;i < msg.count();i++) {
	NamedString* ns = msg.getParam(i);
	if (!ns->name().startsWith("cpu.") || ns->name().length() <= 4)
	    continue;
	inc = new NamedString(ns->name().substr(4),*ns);
	break;
    }
    if (!inc) {
	DDebug(&s_module,DebugNote,"No target parameter for monitor %s",mon.c_str());
	return TelEngine::controlReturn(&msg,true);
    }
    Lock lock(this);
    bool ret = false;
    switch (lookup(mon,s_monitors,Unknown)) {
	case YateUser:
	    ret = m_yateUser.addTarget(*inc,m_oscillationTimer);
	    break;
	case YateKernel:
	    ret = m_yateSys.addTarget(*inc,m_oscillationTimer);
	    break;
	case YateTotal:
	    ret = m_yateTotal.addTarget(*inc,m_oscillationTimer);
	    break;
	case System:
	    ret = m_system.addTarget(*inc,m_oscillationTimer);
	    break;
	default:
	    Debug(&s_module,DebugNote,"Unknown cpu monitor %s",mon.c_str());
    }
    TelEngine::destruct(inc);
    return TelEngine::controlReturn(&msg,ret);
}

void CpuUpdater::initialize(const Configuration& params)
{
    NamedList* general = params.getSection("general");
    int osTimer = 0;
    if (general) {
	m_updateInterval = general->getIntValue("interval",1000);
	if (m_updateInterval < 1000) {
	    Debug(&s_module,DebugConf,"Minimum value for interval is 1000!");
	    m_updateInterval = 1000;
	}
	osTimer = general->getIntValue("oscillation_interval",5000);
	if (osTimer < 2 * m_updateInterval) {
	    Debug(&s_module,DebugConf,"Oscillation interval is to small!");
	    osTimer = 3 * m_updateInterval;
	}
	m_coreNumber = general->getIntValue("core_number",1);
	if (m_coreNumber < 1) {
	    Debug(&s_module,DebugConf,"Core number must be at least 1!");
	    m_coreNumber = 1;
	}
	if (m_cpu)
	    m_cpu->setCore(m_coreNumber);
    }
    Lock lock(this);
    NamedList* yateu = params.getSection(s_monitors[0].token);
    m_yateUser.clearTargets();
    if (yateu)
	m_yateUser.initialize(*yateu,osTimer);
    NamedList* yates = params.getSection(s_monitors[1].token);
    m_yateSys.clearTargets();
    if (yates)
	m_yateSys.initialize(*yates,osTimer);
    NamedList* yatet = params.getSection(s_monitors[2].token);
    m_yateTotal.clearTargets();
    if (yatet)
	m_yateTotal.initialize(*yatet,osTimer);
    NamedList* sys = params.getSection(s_monitors[3].token);
    m_system.clearTargets();
    if (sys)
	m_system.initialize(*sys,osTimer);
}

/**
 * Class Cpu
 */

Cpu::Cpu()
    : m_yateUser(0), m_yateSystem(0), m_sysUser(0), m_sysKer(0), m_sysNice(0),
    m_loadYU(0), m_loadYS(0), m_loadY(0), m_loadSystem(-1), m_coreNumber(1), m_lastYateCheck (0),
    m_lastSystemCheck(0), m_cpuDiscovered(false)
{
    SysUsage::init();
}

void Cpu::updateYateLoad()
{
    u_int64_t user = SysUsage::msecRunTime(SysUsage::UserTime);
    u_int64_t ker = SysUsage::msecRunTime(SysUsage::KernelTime);
    u_int64_t time = SysUsage::msecRunTime(SysUsage::WallTime);
    bool updateLoad = true;
    if (user < m_yateUser || ker < m_yateSystem || time < m_lastYateCheck) {
	Debug(&s_module,DebugInfo,"Negative values for yate CPU update"
	    " cu = " FMT64U " lu=" FMT64U " ck=" FMT64U
	    " lk=" FMT64U " ct=" FMT64U " lt=" FMT64U,
	    user,m_yateUser,ker,m_yateSystem,time,m_lastYateCheck);
	updateLoad = false;
    }
    if (updateLoad && (m_yateUser != 0 || m_yateSystem != 0)) {
	int inter = time - m_lastYateCheck;
	int usr = user - m_yateUser;
	int iload = (usr * 100) / inter;
	iload /= m_coreNumber;
	m_loadYU = (100 - s_smooth) * m_loadYU/100 + s_smooth*iload;
	int ke = ker - m_yateSystem;
	iload  = (ke * 100) / inter;
	iload /= m_coreNumber;
	m_loadYS = (100 - s_smooth) * m_loadYS/100 + s_smooth*iload;
	iload = ((usr + ke) * 100)/ inter;
	iload /= m_coreNumber;
	m_loadY = (100 - s_smooth) * m_loadY/100 + s_smooth*iload;
    }
    m_yateUser = user;
    m_yateSystem = ker;
    m_lastYateCheck = time;
}

/**
 * Class Interval
 *
 */

Interval::Interval(const String& name, int up, int threshold, int down)
    : String(name), m_up(up), m_threshold(threshold), m_down(down)
{
   DDebug(&s_module,DebugAll,"Creating interval %s with low = %d and hight = %d",
	  name.c_str(),down,up);
}

bool Interval::hasValue(int value)
{
    return value >= m_down && value <= m_up;
}

/**
 * Class Target
 */

Target::Target(const String& name, int osTimer, const String& monitor)
    : String(name), m_monitor(monitor), m_oscillationTimeout(osTimer), m_counter(0)
{
    DDebug(&s_module,DebugAll,"Creating target '%s' for monitor '%s' [%p]",
	   name.c_str(),monitor.c_str(),this);
}

Target::~Target()
{
    DDebug(&s_module,DebugAll,"Destroing target '%s' from monitor '%s' [%p]",
	   c_str(),m_monitor.c_str(),this);
    m_ascendent.clear();
    m_descendent.clear();
}

void Target::sendNotify(int load)
{
    if (m_lastNotified == m_currentInterval)
	return;
    Message* m = new Message("monitor.notify");
    int index = 0;
    String n("notify.");
    String v("value.");
    m->addParam(n + String(index),"monitor");
    m->addParam(v + String(index++),m_monitor);
    m->addParam(n + String(index),"target");
    m->addParam(v + String(index++),*this);
    m->addParam(n + String(index),"old");
    m->addParam(v + String(index++),m_previewsInterval);
    m->addParam(n + String(index),"new");
    m->addParam(v + String(index++),m_currentInterval);
    m->addParam(n + String(index),"cpu_load");
    m->addParam(v + String(index++),String(load));
    m->addParam(n + String(index),"counter");
    m->addParam(v + String(index++),String(m_counter));
    m->addParam("count",String(index));
    Engine::enqueue(m);
    m_lastNotified = m_currentInterval;
    startTimer();
    m_counter = 0;
}

void Target::handleOscillation(const String& interval,int load)
{
    updateIntervals(interval);
    if (!needInform())
	return;
    sendNotify(load);
}

Interval* Target::getInterval(int load)
{
    m_counter++;
    Interval* i1 = 0;
    Interval* i2 = 0;
    for (ObjList* o = m_ascendent.skipNull();o;o = o->skipNext()) {
	Interval* i = static_cast<Interval*>(o->get());
	if (!i)
	    continue;
	if (i->hasValue(load))
	    i1 = i;
    }
    for (ObjList* o = m_descendent.skipNull();o;o = o->skipNext()) {
	Interval* i = static_cast<Interval*>(o->get());
	if (!i)
	    continue;
	if (i->hasValue(load)) {
	    i2 = i;
	    break;
	}
    }
    if (!i1 || !i2)
	return 0;
    if (*i1 == *i2)
	return i1;
    if (*i1 == m_currentInterval || *i2 == m_currentInterval)
	return 0;
    ObjList* o = m_ascendent.find(m_currentInterval);
    if (!o)
	return 0;
    Interval* i = static_cast<Interval*>(o->get());
    if (load < i->getUp() && load < i->getDown())
	return i1->getUp() > i2->getUp() ? i1 : i2;
    else
	return i1->getDown() < i2->getDown() ? i1 : i2;
}

void Target::manageLoad(int load)
{
    Interval* i = getInterval(load);
    if (!i || *i == m_currentInterval) { // The interval has not been changed
	updateIntervals(m_currentInterval);
	sendNotify(load);
	return;
    }
    if (m_previewsInterval == *i && neighbors()) {
	// Oscillateing
	handleOscillation(*i,load);
	return;
    }
    updateIntervals(*i);
    sendNotify(load);
}

// Check if m_previewsInterval and m_currentInterval are neighbors
bool Target::neighbors()
{
    ObjList* o = m_ascendent.find(m_previewsInterval);
    if (!o)
	return false;
    Interval* i = static_cast<Interval*>(o->get());
    ObjList* o1 = m_ascendent.find(m_currentInterval);
    if (!o1)
	return false;
    Interval* i1 = static_cast<Interval*>(o1->get());
    return i->getDown() == i1->getUp() || i->getUp() == i1->getDown();
}

void Target::updateIntervals(const String& current)
{
    m_previewsInterval = m_currentInterval;
    m_currentInterval = current;
}

/**
 * Class CpuMonitor
 */

CpuMonitor::CpuMonitor(const String& name)
    : String(name), m_informed(false)
{
    DDebug(&s_module,DebugAll,"Creating CpuMonitor '%s' [%p]", name.c_str(), this);
}

CpuMonitor::~CpuMonitor()
{
    DDebug(&s_module,DebugAll,"Destroing CpuMonitor %s [%p]", c_str(),this);
    m_targets.clear();
}

void CpuMonitor::initialize(const NamedList& params,int osTimer)
{
    for (unsigned int i = 0; i < params.count(); i++) {
	NamedString* ns = params.getParam(i);
	if (ns)
	    addTarget(*ns,osTimer);
    }
}

void CpuMonitor::manageLoad(int load)
{
    if (load > 120 && !m_informed) {
	Debug(&s_module,DebugConf,"Please configure cpu core number");
	m_informed = true;
	return;
    }
    for (ObjList*o = m_targets.skipNull();o;o = o->skipNext()) {
	Target* i = static_cast<Target*>(o->get());
	if (!i)
	    continue;
	i->manageLoad(load);
    }
}

bool CpuMonitor::addTarget(const NamedString& incumbent, int osTimer)
{
    ObjList* intervals = incumbent.split(';'); // Obtain intervals
    if (!intervals)
	return false;
    ObjList* exists = m_targets.find(incumbent.name());
    if (exists) {
	Debug(&s_module,DebugConf,"Target '%s' already exists for monitor '%s'",
	      incumbent.name().c_str(),c_str());
	TelEngine::destruct(intervals);
	return false;
    }
    Target* target = new Target(incumbent.name(),osTimer,*this);
    Interval* prevUp = 0;
    Interval* prevDown = 0;
    for (ObjList* i = intervals->skipNull();i;i = i->skipNext()) {
	String* s = static_cast<String*>(i->get());
	if (!s || s->null())
	    continue;
	ObjList* o = s->split(',');
	if (!o)
	    continue;
	int count = o->count();
	String* n = static_cast<String*>(o->at(0)); // Interval name
	if (!n) {
	    TelEngine::destruct(o);
	    continue;
	}
	String name = *n;
	n = 0;
	int iUp = 100;
	if (count > 1) {
	    // Threshold value,if not exists we suppose it full CPU load (100%)
	    String* iu = static_cast<String*>(o->at(1));
	    iUp = iu ? iu->toInteger() : 100;
	    iu = 0;
	}
	int iHR = s_defaultHysteresis;
	if (count > 2) {
	    // Hysteresis value. If not exists then init to default
	    String* hr = static_cast<String*>(o->at(2));
	    if (hr) {
		iHR = hr->toInteger();
	    }
	    hr = 0;
	}
	TelEngine::destruct(o); // s->split()
	bool error = false;
	while (true) {
	    // If previews interval upper value is greater than current threshold
	    // then something may be wrong in configuration file
	    if (prevUp && prevUp->getUp() >= iUp) {
		error = true;
		break;
	    }
	    // If previews interval threshold is greater than current interval lower value or
	    // if this is first interval check if his boundaries are between 0-100
	    if ((prevDown && prevDown->getThreshold() >= (iUp - iHR)) ||
		    (!prevDown && (iUp - iHR) <= 0)) {
		error = true;
		break;
	    }
	    Interval* up = new Interval(name,iUp == 100 ? 100 : iUp + iHR,iUp,
			(prevUp) ? prevUp->getUp() : 0);
	    Interval* down = new Interval(name,iUp == 100? 100 : iUp - iHR,iUp,
			(prevDown) ? prevDown->getUp() : 0);
	    target->addInterval(up,true);
	    target->addInterval(down,false);
	    prevUp = up;
	    prevDown = down;
	    break;
	}
	if (error) {
	    Debug(&s_module,DebugConf,"Invalid intervals threshold for target %s",target->c_str());
	    TelEngine::destruct(target);
	    TelEngine::destruct(intervals);
	    return false;
	}
    }
    if (prevUp->getUp() != 100) {
	Debug(&s_module,DebugConf,"Invalid intervals! No interval reach 100");
	TelEngine::destruct(target);
	TelEngine::destruct(intervals);
	return false;
    }
    // Check if we have an valid target! If not remove it
    if (target->getIntervalsCount() < 2) {
	Debug(&s_module,DebugConf,"To few intervals for target '%s' from manager '%s'",
	      target->c_str(),c_str());
	TelEngine::destruct(target);
    } else
	m_targets.append(target);
    TelEngine::destruct(intervals);
    return true;
}

/**
 * Class QueryHandler
 */

bool QueryHandler::received(Message& msg)
{
    String target = msg.getValue("name");
    int manager = lookup(target,s_monitors,CpuUpdater::Unknown);
    CpuUpdater* cu = s_module.getUpdater();
    if (!cu)
	return false;
    Cpu* cpu = cu->getCpu();
    if (!cpu)
	return false;
    switch (manager) {
	case CpuUpdater::YateKernel:
	    msg.setParam("value",String(cpu->getYateKernelLoad()));
	    return true;
	case CpuUpdater::YateUser:
	    msg.setParam("value",String(cpu->getYateUserLoad()));
	    return true;
	case CpuUpdater::YateTotal:
	    msg.setParam("value",String(cpu->getYateLoad()));
	    return true;
	case CpuUpdater::System:
	    int sload = cpu->getSLoad();
	    if (sload < 0)
		return false;
	    msg.setParam("value",String(sload));
	    return true;
    }
    return false;
}

/**
 * Class CpuModule
 */

CpuModule::CpuModule()
    : Module("cpuload","misc",true), m_init(false)
{
    Output("Loaded module Cpu");
    m_updater = new CpuUpdater();
}

CpuModule::~CpuModule()
{
    Output("Unloading module Cpu");
}

bool CpuModule::received (Message &msg, int id)
{
    switch (id) {
	case Halt:
	    // Stop the thread!
	    if (m_updater)
		m_updater->requestExit();
	    break;
	case Control:
	    // Process chan.control message
	    const String* dest = msg.getParam("component");
	    if (dest && (*dest == "cpuload"))
		return m_updater->update(msg);
	    break;
    }
    return false;
}

void CpuModule::initialize()
{
    Output("Initializing module Cpu");
    Configuration cfg(Engine::configFile("cpuload"));
    m_updater->initialize(cfg);
    s_smooth = cfg.getIntValue("general","smooth",33);
    if (s_smooth < 5)
	s_smooth = 5;
    if (s_smooth > 50)
	s_smooth = 50;
    if (m_init)
	return;
    Cpu* c = new CpuPlatform();
    if (c->getSystemLoad() == -1) {
	delete c;
	c = 0;
    }
#ifdef _SC_CLK_TCK
    if (!c) {
	c = new CpuStat();
	if (c->getSystemLoad() == -1) {
	    delete c;
	    c = 0;
	}
    }
#endif
    if (!c)
	c = new Cpu();
    m_updater->setCpu(c);
    m_init = true;
    m_updater->startup();
    installRelay(Control);
    Engine::install(new QueryHandler());
    installRelay(Halt);
}

#ifdef _SC_CLK_TCK
/**
 * class CpuStat
 * Obtain system cpu load from "/proc/stat"
 */

void CpuStat::close(File* f)
{
    if (!f)
	return;
    f->terminate();
    delete f;
}

int CpuStat::getSystemLoad()
{
    File* f = new File();
    if (!f->openPath(s_address,false,true)) {
	DDebug(&s_module,DebugNote,"Failed to open %s",s_address.c_str());
	return -1;
    }
    u_int64_t time = Time::msecNow();
    char buf[s_bufLen + 1];
    int read = f->readData(buf,s_bufLen);
    if (read < 0) {
	Debug(&s_module,DebugNote,"Read data error %s",strerror(errno));
	close(f);
	return -1;
    }
    buf[read] = '\0';
    String s(buf,read);
    ObjList* ob = s.split('\n',false);
    if (!ob) {
	Debug(&s_module,DebugNote,"Invalid data read from %s", s_address.c_str());
	close(f);
	return -1;
    }
    int counter = 0;
    for (ObjList* o = ob->skipNull();o;o = o->skipNext()) {
	String* cpu = static_cast<String*>(o->get());
	if (!cpu) {
	    TelEngine::destruct(ob);
	    close(f);
	    return -1;
	}
	ObjList* val = cpu->split(' ',false);
	if (!val || val->count() < 4) {
	    if (val)
		TelEngine::destruct(val);
	    TelEngine::destruct(ob);
	    close(f);
	    return -1;
	}
	String* name = static_cast<String*>(val->at(0));
	if (!name) {
	    TelEngine::destruct(ob);
	    TelEngine::destruct(val);
	    close(f);
	    return -1;
	}
	if (*name == "cpu") {
	    String* u = static_cast<String*>(val->at(1));
	    int user = u->toInteger();
	    String* n = static_cast<String*>(val->at(2));
	    int nice = n->toInteger();
	    String* k = static_cast<String*>(val->at(3));
	    int kernel = k->toInteger();
	    int loading = 0;
	    long user_hz = ::sysconf(_SC_CLK_TCK);
	    if (user_hz == 0) {
		Debug(&s_module,DebugWarn,"UserHZ value is 0 !! Can not calculate "
			"system CPU loading");
		return -1;
	    }
	    if (m_cpuDiscovered) {
		// If we had discovered the CPU's number, calculate the loading
		loading = (user - m_sysUser) + (nice - m_sysNice) + (kernel - m_sysKer);
		int t = time - m_lastSystemCheck;
		if (t == 0)
		    return (m_loadSystem + 50) / 100;
		loading = ((loading *100) * (1000 / user_hz)) / t;
		loading /= m_coreNumber;
		m_loadSystem = (100 - s_smooth) * m_loadSystem/100 + s_smooth*loading;
	    } else
		m_loadSystem = 0;
	    m_sysUser = user;
	    m_sysNice = nice;
	    m_sysKer = kernel;
	    m_lastSystemCheck = time;
	    TelEngine::destruct(val);
	    if (m_cpuDiscovered) {
		TelEngine::destruct(ob);
		close(f);
		return (m_loadSystem + 50) / 100;
	    }
	    continue;
	}
	if (name->startsWith("cpu")) { // count cpu0, cpu1,...
	    counter ++;
	    TelEngine::destruct(val);
	    continue;
	}
	m_cpuDiscovered = true;
	if (m_coreNumber != counter && counter > 0) {
	    Debug(&s_module,(m_coreNumber == 1) ? DebugNote:DebugWarn,"Updating CPU core number from %d to %d",
		    m_coreNumber,counter);
	    m_coreNumber = counter;
	}
	TelEngine::destruct(val);
	TelEngine::destruct(ob);
	close(f);
	return 0;
    }
    TelEngine::destruct(ob);
    close(f);
    return -1;
}
#endif

/**
 * Class CpuPlatform
 * Platform depended system cpu load implementation
 */

int CpuPlatform::getSystemLoad()
{
#if defined(_WINDOWS)
    // Windows implementation
#elif defined(__FreeBSD__)
    // FreeBSD implementation
#elif defined(_MAC)
    // MAC OS implementation
#endif
    return -1;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
