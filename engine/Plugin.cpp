/**
 * Plugin.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 */

#include "telengine.h"

using namespace TelEngine;

Plugin::Plugin()
{
    Debug(DebugAll,"Plugin::Plugin() [%p]",this);
    Engine::Register(this);
}

Plugin::Plugin(const char *name)
{
    Debug(DebugAll,"Plugin::Plugin(\"%s\") [%p]",name,this);
    Engine::Register(this);
}

Plugin::~Plugin()
{
    Debugger debug("Plugin::~Plugin()"," [%p]",this);
    Engine::Register(this,false);
}
