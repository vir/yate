/**
 * Plugin.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004 Null Team
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

#include "yatengine.h"

using namespace TelEngine;

Plugin::Plugin()
{
    Debug(DebugAll,"Plugin::Plugin() [%p]",this);
    Engine::Register(this);
}

Plugin::Plugin(const char* name)
{
    Debug(DebugAll,"Plugin::Plugin(\"%s\") [%p]",name,this);
    Engine::Register(this);
}

Plugin::~Plugin()
{
    Debugger debug("Plugin::~Plugin()"," [%p]",this);
    Engine::Register(this,false);
}
