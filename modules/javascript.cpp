/**
 * javascript.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Javascript support based on libyscript
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2011 Null Team
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

#include <yatephone.h>
#include <yatescript.h>

using namespace TelEngine;
namespace { // anonymous

class JavascriptModule : public Module
{
public:
    JavascriptModule();
    ~JavascriptModule();
    virtual void initialize();
    bool unload();
protected:
    virtual bool commandExecute(String& retVal, const String& line);
    virtual bool commandComplete(Message& msg, const String& partLine, const String& partWord);
};


INIT_PLUGIN(JavascriptModule);

UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow)
	return __plugin.unload();
    return true;
}

JavascriptModule::JavascriptModule()
    : Module("javascript","misc",true)
{
    Output("Loaded module Javascript");
}

JavascriptModule::~JavascriptModule()
{
    Output("Unloading module Javascript");
}

bool JavascriptModule::commandExecute(String& retVal, const String& line)
{
    if (!line.startsWith("js "))
	return false;
    String cmd = line.substr(3).trimSpaces();
    if (cmd.null())
	return false;
    ExpOperation* rval = 0;
    ScriptRun::Status st = JsParser::eval(cmd,&rval);
    if (rval)
	retVal << "'" << rval->name() << "'='" << *rval << "'\r\n";
    else
	retVal << ScriptRun::textState(st) << "\r\n";
    TelEngine::destruct(rval);
    return true;
}

bool JavascriptModule::commandComplete(Message& msg, const String& partLine, const String& partWord)
{
    if (partLine.null() && partWord.null())
	return false;
    if (partLine.null() || (partLine == "help"))
	itemComplete(msg.retValue(),"js",partWord);
    return Module::commandComplete(msg,partLine,partWord);
}

bool JavascriptModule::unload()
{
    uninstallRelays();
    return true;
}

void JavascriptModule::initialize()
{
    Output("Initializing module Javascript");
    setup();
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
