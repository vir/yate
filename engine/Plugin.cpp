/**
 * Plugin.cpp
 * This file is part of the YATE Project http://YATE.null.ro
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

#include "yatengine.h"

using namespace TelEngine;

Plugin::Plugin(const char* name, bool earlyInit)
    : m_name(name), m_early(earlyInit)
{
    Debug(DebugAll,"Plugin::Plugin(\"%s\",%s) [%p]",name,String::boolText(earlyInit),this);
    debugName(m_name);
    m_counter = getObjCounter(m_name);
    Engine::Register(this);
}

Plugin::~Plugin()
{
    Debugger debug("Plugin::~Plugin()"," \"%s\" [%p]",m_name.c_str(),this);
    Engine::Register(this,false);
    debugName(0);
}

void* Plugin::getObject(const String& name) const
{
    if (name == YATOM("Plugin"))
	return const_cast<Plugin*>(this);
    return GenObject::getObject(name);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
