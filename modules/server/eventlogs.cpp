/**
 * eventlogs.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Write the events and alerts to text log files
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2012-2014 Null Team
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

using namespace TelEngine;
namespace { // anonymous

class EventLogsHandler;

class EventLogsPlugin : public Plugin
{
public:
    EventLogsPlugin();
    ~EventLogsPlugin();
    virtual void initialize();
private:
    EventLogsHandler* m_handler;
};


#ifdef _WINDOWS
const char eoln[] = { '\r', '\n' };
#else
const char eoln[] = { '\n' };
#endif

static String s_baseDir;
static bool s_pubRead = false;

INIT_PLUGIN(EventLogsPlugin);


class EventLogsHandler : public MessageHandler, public Mutex
{
public:
    EventLogsHandler(const char *name)
	: MessageHandler(name,100,__plugin.name()),
	  Mutex(false,"EventLogs"),
	  m_mappings("")
	{ }
    virtual bool received(Message &msg);
    void init(const NamedList* mappings);
private:
    bool writeLog(const char* name, const String& line);
    NamedList m_mappings;
};


bool EventLogsHandler::writeLog(const char* name, const String& line)
{
    File f;
    if (!f.openPath(name,true,false,true,true,true,s_pubRead))
	return false;
    f.writeData(line.c_str(),line.length());
    f.writeData(eoln,sizeof(eoln));
    return true;
}

bool EventLogsHandler::received(Message &msg)
{
    if (!msg.getBoolValue(YSTRING("eventwrite_eventlogs"),true))
	return false;
    const String& from = msg[YSTRING("from")];
    if (from.null())
	return false;
    const String& text = msg[YSTRING("fulltext")];
    if (text.null())
	return false;
    String file;
    Lock lock(this);
    if (s_baseDir.null())
	return false;
    unsigned int len = m_mappings.length();
    for (unsigned int i = 0; i < len; i++) {
	const NamedString* n = m_mappings.getParam(i);
	if (!n)
	    continue;
	Regexp r(n->name());
	String tmp(from);
	if (tmp.matches(r)) {
	    file = tmp.replaceMatches(*n);
	    if (file)
		break;
	}
    }
    if (file) {
	if (!file.startsWith(Engine::pathSeparator()))
	    file = s_baseDir + file;
        if (!writeLog(file,text))
	    Debug(__plugin.name(),DebugWarn,"Failed to log to file '%s'",file.c_str());
    }
    return false;
};

void EventLogsHandler::init(const NamedList* mappings)
{
    m_mappings.clearParams();
    if (mappings)
	m_mappings.copyParams(*mappings);
    if (m_mappings.count() == 0)
	m_mappings.addParam("^[A-Za-z0-9_-]\\+","\\0.log");
}


EventLogsPlugin::EventLogsPlugin()
    : Plugin("eventlogs",true),
      m_handler(0)
{
    Output("Loaded module Event Logs");
}

EventLogsPlugin::~EventLogsPlugin()
{
    Output("Unloading module Event Logs");
}

void EventLogsPlugin::initialize()
{
    Output("Initializing module Event Logs");
    Configuration cfg(Engine::configFile("eventlogs"));
    String base = cfg.getValue("general","logs_dir");
    Engine::self()->runParams().replaceParams(base);
    if (base) {
	File::mkDir(base);
	if (!base.endsWith(Engine::pathSeparator()))
	    base += Engine::pathSeparator();
    }
    Lock lock(m_handler);
    s_baseDir = base;
    s_pubRead = cfg.getBoolValue("general","public_read");
    if (m_handler)
	m_handler->init(cfg.getSection("mappings"));
    else if (base) {
	m_handler = new EventLogsHandler("module.update");
	m_handler->init(cfg.getSection("mappings"));
	Engine::install(m_handler);
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
