/**
 * NamedList.cpp
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

#include "telengine.h"

using namespace TelEngine;

NamedList::NamedList(const char *name)
    : String(name)
{
}

NamedList &NamedList::addParam(NamedString *param)
{
#ifdef DEBUG
    Debug(DebugInfo,"NamedList::addParam(%p) [\"%s\",\"%s\"]",
        param,param->name().c_str(),param->c_str());
#endif
    m_params.append(param);
    return *this;
}

NamedList &NamedList::addParam(const char *name, const char *value)
{
#ifdef DEBUG
    Debug(DebugInfo,"NamedList::addParam(\"%s\",\"%s\")",name,value);
#endif
    m_params.append(new NamedString(name, value));
    return *this;
}

NamedList &NamedList::setParam(NamedString *param)
{
#ifdef DEBUG
    Debug(DebugInfo,"NamedList::setParam(%p) [\"%s\",\"%s\"]",
        param,param->name().c_str(),param->c_str());
#endif
    NamedString *s = getParam(param->name());
    if (s) {
	*s = param->c_str();
	param->destruct();
    }
    else
	m_params.append(param);
    return *this;
}

NamedList &NamedList::setParam(const char *name, const char *value)
{
#ifdef DEBUG
    Debug(DebugInfo,"NamedList::setParam(\"%s\",\"%s\")",name,value);
#endif
    NamedString *s = getParam(name);
    if (s)
	*s = value;
    else
	m_params.append(new NamedString(name, value));
    return *this;
}

NamedList &NamedList::clearParam(const String &name)
{
#ifdef DEBUG
    Debug(DebugInfo,"NamedList::clearParam(\"%s\")",name.c_str());
#endif
    ObjList *p = &m_params;
    while (p) {
        NamedString *s = static_cast<NamedString *>(p->get());
        if (s && (s->name() == name))
            p->remove();
	else
	    p = p->next();
    }
    return *this;
}

NamedString *NamedList::getParam(const String &name) const
{
#ifdef DEBUG
    Debug(DebugInfo,"NamedList::getParam(\"%s\")",name.c_str());
#endif
    const ObjList *p = &m_params;
    for (;p;p=p->next()) {
        NamedString *s = static_cast<NamedString *>(p->get());
        if (s && (s->name() == name))
            return s;
    }
    return 0;
}

NamedString *NamedList::getParam(unsigned int index) const
{
#ifdef DEBUG
    Debug(DebugInfo,"NamedList::getParam(%u)",index);
#endif
    const ObjList *p = m_params[index];
    return p ? static_cast<NamedString *>(p->get()) : 0;
}

const char *NamedList::getValue(const String &name, const char *defvalue) const
{
#ifdef DEBUG
    Debug(DebugInfo,"NamedList::getValue(\"%s\",\"%s\")",name.c_str(),defvalue);
#endif
    const NamedString *s = getParam(name);
    return s ? s->c_str() : defvalue;
}
