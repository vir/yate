/**
 * cdrfile.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Write the CDR to a text file
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004, 2005 Null Team
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

#include <yatengine.h>

#include <stdio.h>

using namespace TelEngine;

class CdrFileHandler : public MessageHandler
{
public:
    CdrFileHandler(const char *name)
	: MessageHandler(name), m_tabs(0), m_file(0) { }
    virtual ~CdrFileHandler();
    virtual bool received(Message &msg);
    void init(const char *fname, bool tabsep);
private:
    bool m_tabs;
    FILE *m_file;
    Mutex m_lock;
};

CdrFileHandler::~CdrFileHandler()
{
    Lock lock(m_lock);
    if (m_file) {
	::fclose(m_file);
	m_file = 0;
    }
}

void CdrFileHandler::init(const char *fname, bool tabsep)
{
    Lock lock(m_lock);
    if (m_file)
	::fclose(m_file);
    m_tabs = tabsep;
    m_file = fname ? ::fopen(fname,"a") : 0;
}

bool CdrFileHandler::received(Message &msg)
{
    String op(msg.getValue("operation"));
    if (op != "finalize")
	return false;

    Lock lock(m_lock);
    if (m_file) {
	const char *format = m_tabs
	    ? "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n"
	    : "%s,\"%s\",\"%s\",\"%s\",\"%s\",%s,%s,%s,\"%s\",\"%s\",\"%s\"\n";
	::fprintf(m_file,format,
	    c_safe(msg.getValue("time")),
	    c_safe(msg.getValue("chan")),
	    c_safe(msg.getValue("address")),
	    c_safe(msg.getValue("caller")),
	    c_safe(msg.getValue("called")),
	    c_safe(msg.getValue("billtime")),
	    c_safe(msg.getValue("ringtime")),
	    c_safe(msg.getValue("duration")),
	    c_safe(msg.getValue("status")),
	    c_safe(msg.getValue("direction")),
	    c_safe(msg.getValue("billid"))
	    );
	::fflush(m_file);
    }
    return false;
};
		    
class CdrFilePlugin : public Plugin
{
public:
    CdrFilePlugin();
    virtual void initialize();
private:
    CdrFileHandler *m_handler;
};

CdrFilePlugin::CdrFilePlugin()
    : m_handler(0)
{
    Output("Loaded module CdrFile");
}

void CdrFilePlugin::initialize()
{
    Output("Initializing module CdrFile");
    Configuration cfg(Engine::configFile("cdrfile"));
    const char *file = cfg.getValue("general","file");
    if (file && !m_handler) {
	m_handler = new CdrFileHandler("call.cdr");
	Engine::install(m_handler);
    }
    if (m_handler)
	m_handler->init(file,cfg.getBoolValue("general","tabs"));
}

INIT_PLUGIN(CdrFilePlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
