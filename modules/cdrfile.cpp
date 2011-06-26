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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#ifdef _WINDOWS
#define EOLN "\r\n"
#else
#define EOLN "\n"
#endif

using namespace TelEngine;
namespace { // anonymous

class CdrFileHandler : public MessageHandler, public Mutex
{
public:
    CdrFileHandler(const char *name)
	: MessageHandler(name), Mutex(false,"CdrFileHandler"),
	  m_file(-1)
	{ }
    virtual ~CdrFileHandler();
    virtual bool received(Message &msg);
    void init(const char *fname, bool tabsep, const char* format);
private:
    int m_file;
    String m_format;
};

CdrFileHandler::~CdrFileHandler()
{
    Lock lock(this);
    if (m_file >= 0) {
	::close(m_file);
	m_file = -1;
    }
}

void CdrFileHandler::init(const char *fname, bool tabsep, const char* format)
{
    Lock lock(this);
    if (m_file >= 0) {
	::close(m_file);
	m_file = -1;
    }
    m_format = format;
    if (m_format.null())
	m_format = tabsep
	    ? "${time}\t${billid}\t${chan}\t${address}\t${caller}\t${called}\t${billtime}\t${ringtime}\t${duration}\t${direction}\t${status}\t${reason}"
	    : "${time},\"${billid}\",\"${chan}\",\"${address}\",\"${caller}\",\"${called}\",${billtime},${ringtime},${duration},\"${direction}\",\"${status}\",\"${reason}\"";
    if (fname) {
	m_file = ::open(fname,O_WRONLY|O_CREAT|O_APPEND|O_LARGEFILE,0640);
	if (m_file < 0)
	    Debug(DebugWarn,"Failed to open or create '%s': %s (%d)",
		fname,::strerror(errno),errno);
    }
}

bool CdrFileHandler::received(Message &msg)
{
    if (!msg.getBoolValue("cdrwrite_cdrfile",true))
	return false;
    String op(msg.getValue("operation"));
    if (op != "finalize")
	return false;
    if (!msg.getBoolValue("cdrwrite",true))
        return false;

    Lock lock(this);
    if ((m_file >= 0) && m_format) {
	String str = m_format;
	str += EOLN;
	msg.replaceParams(str);
	::write(m_file,str.c_str(),str.length());
    }
    return false;
};

class CdrFilePlugin : public Plugin
{
public:
    CdrFilePlugin();
    ~CdrFilePlugin();
    virtual void initialize();
private:
    CdrFileHandler *m_handler;
};

CdrFilePlugin::CdrFilePlugin()
    : Plugin("cdrfile",true),
      m_handler(0)
{
    Output("Loaded module CdrFile");
}

CdrFilePlugin::~CdrFilePlugin()
{
    Output("Unloading module CdrFile");
}

void CdrFilePlugin::initialize()
{
    Output("Initializing module CdrFile");
    Configuration cfg(Engine::configFile("cdrfile"));
    String file = cfg.getValue("general","file");
    Engine::self()->runParams().replaceParams(file);
    if (file && !m_handler) {
	m_handler = new CdrFileHandler("call.cdr");
	Engine::install(m_handler);
    }
    if (m_handler)
	m_handler->init(file,cfg.getBoolValue("general","tabs",true),cfg.getValue("general","format"));
}

INIT_PLUGIN(CdrFilePlugin);

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
