/**
 * Plugin.cpp
 * This file is part of the YATE Project http://YATE.null.ro
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

#include "yatengine.h"

using namespace TelEngine;

Plugin::Plugin(const char* name, bool earlyInit)
    : m_name(name), m_early(earlyInit)
{
    Debug(DebugAll,"Plugin::Plugin(\"%s\",%s) [%p]",name,String::boolText(earlyInit),this);
    debugName(m_name);
    Engine::Register(this);
}

Plugin::~Plugin()
{
    Debugger debug("Plugin::~Plugin()"," [%p]",this);
    Engine::Register(this,false);
    debugName(0);
}

void* Plugin::getObject(const String& name) const
{
    if (name == YSTRING("Plugin"))
	return const_cast<Plugin*>(this);
    return GenObject::getObject(name);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
