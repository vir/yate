/**
 * cdrfile.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Write the CDR to a text file
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

class CdrFileHandler;

class CdrFilePlugin : public Plugin
{
public:
    CdrFilePlugin();
    ~CdrFilePlugin();
    virtual void initialize();
private:
    CdrFileHandler *m_handler;
};

INIT_PLUGIN(CdrFilePlugin);

class CdrFileHandler : public MessageHandler, public Mutex
{
public:
    CdrFileHandler(const char *name)
	: MessageHandler(name,100,__plugin.name()),
	  Mutex(false,"CdrFileHandler"),
	  m_file(-1), m_combined(false)
	{ }
    virtual ~CdrFileHandler();
    virtual bool received(Message &msg);
    void init(const char *fname, bool tabsep, bool combined, const char* format);
private:
    int m_file;
    bool m_combined;
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

void CdrFileHandler::init(const char *fname, bool tabsep, bool combined, const char* format)
{
    Lock lock(this);
    if (m_file >= 0) {
	::close(m_file);
	m_file = -1;
    }
    m_format = format;
    m_combined = combined;
    if (m_format.null()) {
	m_format = tabsep
	    ? (combined
		? "${time}\t${billid}\t${chan}\t${address}\t${caller}\t${called}"
		    "\t${billtime}\t${ringtime}\t${duration}\t${status}\t${reason}"
		    "\t${out_leg.chan}\t${out_leg.address}\t${out_leg.billtime}"
		    "\t${out_leg.ringtime}\t${out_leg.duration}\t${out_leg.reason}"
		: "${time}\t${billid}\t${chan}\t${address}\t${caller}\t${called}"
		    "\t${billtime}\t${ringtime}\t${duration}\t${direction}\t${status}\t${reason}"
	      )
	    : (combined
		? "${time},\"${billid}\",\"${chan}\",\"${address}\",\"${caller}\",\"${called}\""
		    ",${billtime},${ringtime},${duration},\"${status}\",\"${reason}\""
		    ",\"${out_leg.chan}\",\"${out_leg.address}\",${out_leg.billtime}"
		    ",${out_leg.ringtime},${out_leg.duration},\"${out_leg.reason}\""
		: "${time},\"${billid}\",\"${chan}\",\"${address}\",\"${caller}\",\"${called}\""
		    ",${billtime},${ringtime},${duration},\"${direction}\",\"${status}\",\"${reason}\""
	      );
    }
    if (fname) {
	m_file = ::open(fname,O_WRONLY|O_CREAT|O_APPEND|O_LARGEFILE,0640);
	if (m_file < 0)
	    Alarm("cdrfile","system",DebugWarn,"Failed to open or create '%s': %s (%d)",
		fname,::strerror(errno),errno);
    }
}

bool CdrFileHandler::received(Message &msg)
{
    if (!msg.getBoolValue("cdrwrite_cdrfile",true))
	return false;
    String op(msg.getValue("operation"));
    if (op != (m_combined ? YSTRING("combined") : YSTRING("finalize")))
	return false;
    if (!msg.getBoolValue("cdrwrite",true))
        return false;

    Lock lock(this);
    if ((m_file >= 0) && m_format) {
	String str = m_format;
	str += EOLN;
	msg.replaceParams(str);
	YIGNORE(::write(m_file,str.c_str(),str.length()));
    }
    return false;
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
	m_handler->init(file,cfg.getBoolValue("general","tabs",true),
	    cfg.getBoolValue("general","combined",false),cfg.getValue("general","format"));
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
