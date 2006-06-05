/**
 * cdrfile.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Write the CDR to a text file
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <yatengine.h>

#include <stdio.h>

using namespace TelEngine;
namespace { // anonymous

class CdrFileHandler : public MessageHandler
{
public:
    CdrFileHandler(const char *name)
	: MessageHandler(name), m_file(0) { }
    virtual ~CdrFileHandler();
    virtual bool received(Message &msg);
    void init(const char *fname, bool tabsep, const char* format);
private:
    FILE *m_file;
    String m_format;
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

void CdrFileHandler::init(const char *fname, bool tabsep, const char* format)
{
    Lock lock(m_lock);
    if (m_file)
	::fclose(m_file);
    m_format = format;
    if (m_format.null())
	m_format = tabsep
	    ? "${time}\t${billid}\t${chan}\t${address}\t${caller}\t${called}\t${billtime}\t${ringtime}\t${duration}\t${direction}\t${status}\t${reason}"
	    : "${time},\"${billid}\",\"${chan}\",\"${address}\",\"${caller}\",\"${called}\",${billtime},${ringtime},${duration},\"${direction}\",\"${status}\",\"${reason}\"";
    m_file = fname ? ::fopen(fname,"a") : 0;
}

bool CdrFileHandler::received(Message &msg)
{
    String op(msg.getValue("operation"));
    if (op != "finalize")
	return false;

    Lock lock(m_lock);
    if (m_file && m_format) {
	String str = m_format;
	str += "\n";
	msg.replaceParams(str);
	::fputs(str.safe(),m_file);
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
	m_handler->init(file,cfg.getBoolValue("general","tabs"),cfg.getValue("general","format"));
}

INIT_PLUGIN(CdrFilePlugin);

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
