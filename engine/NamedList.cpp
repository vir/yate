/**
 * NamedList.cpp
 * This file is part of the YATE Project http://YATE.null.ro
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

#include "yateclass.h"

using namespace TelEngine;

NamedList::NamedList(const char* name)
    : String(name)
{
}

NamedList::NamedList(const NamedList& original)
    : String(original)
{
    for (unsigned int i = 0; i < original.length(); i++) {
	const NamedString* p = original.getParam(i);
	if (p)
	    m_params.append(new NamedString(p->name(),*p));
    }
}

NamedList& NamedList::addParam(NamedString* param)
{
    XDebug(DebugInfo,"NamedList::addParam(%p) [\"%s\",\"%s\"]",
        param,param->name().c_str(),param->c_str());
    m_params.append(param);
    return *this;
}

NamedList& NamedList::addParam(const char* name, const char* value)
{
    XDebug(DebugInfo,"NamedList::addParam(\"%s\",\"%s\")",name,value);
    m_params.append(new NamedString(name, value));
    return *this;
}

NamedList& NamedList::setParam(NamedString* param)
{
    XDebug(DebugInfo,"NamedList::setParam(%p) [\"%s\",\"%s\"]",
        param,param->name().c_str(),param->c_str());
    NamedString *s = getParam(param->name());
    if (s) {
	*s = param->c_str();
	param->destruct();
    }
    else
	m_params.append(param);
    return *this;
}

NamedList& NamedList::setParam(const char* name, const char* value)
{
    XDebug(DebugInfo,"NamedList::setParam(\"%s\",\"%s\")",name,value);
    NamedString *s = getParam(name);
    if (s)
	*s = value;
    else
	m_params.append(new NamedString(name, value));
    return *this;
}

NamedList& NamedList::clearParam(const String& name)
{
    XDebug(DebugInfo,"NamedList::clearParam(\"%s\")",name.c_str());
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

NamedString* NamedList::getParam(const String& name) const
{
    XDebug(DebugInfo,"NamedList::getParam(\"%s\")",name.c_str());
    const ObjList *p = m_params.skipNull();
    for (;p;p=p->skipNext()) {
        NamedString *s = static_cast<NamedString *>(p->get());
        if (s->name() == name)
            return s;
    }
    return 0;
}

NamedString* NamedList::getParam(unsigned int index) const
{
    XDebug(DebugInfo,"NamedList::getParam(%u)",index);
    return static_cast<NamedString *>(m_params[index]);
}

const char* NamedList::getValue(const String& name, const char* defvalue) const
{
    XDebug(DebugInfo,"NamedList::getValue(\"%s\",\"%s\")",name.c_str(),defvalue);
    const NamedString *s = getParam(name);
    return s ? s->c_str() : defvalue;
}

int NamedList::getIntValue(const String& name, int defvalue) const
{
    const NamedString *s = getParam(name);
    return s ? s->toInteger(defvalue) : defvalue;
}

int NamedList::getIntValue(const String& name, const TokenDict* tokens, int defvalue) const
{
    const NamedString *s = getParam(name);
    return s ? s->toInteger(tokens,defvalue) : defvalue;
}

double NamedList::getDoubleValue(const String& name, double defvalue) const
{
    const NamedString *s = getParam(name);
    return s ? s->toDouble(defvalue) : defvalue;
}

bool NamedList::getBoolValue(const String& name, bool defvalue) const
{
    const NamedString *s = getParam(name);
    return s ? s->toBoolean(defvalue) : defvalue;
}

int NamedList::replaceParams(String& str, bool sqlEsc, char extraEsc) const
{
    int p1;
    int cnt = 0;
    while ((p1 = str.find("${")) >= 0) {
	int p2 = str.find('}',p1+2);
	if (p2 > 0) {
	    String tmp = str.substr(p1+2,p2-p1-2);
	    tmp.trimBlanks();
	    DDebug(DebugAll,"NamedList replacing parameter '%s' [%p]",tmp.c_str(),this);
	    tmp = getValue(tmp);
	    if (sqlEsc)
		tmp = tmp.sqlEscape(extraEsc);
	    str = str.substr(0,p1) + tmp + str.substr(p2+1);
	    // put a limit to avoid infinite loops
	    if (++cnt >= 1000) {
		Debug(DebugWarn,"NamedList reached count %d replacing into '%s' [%p]",cnt,str.c_str(),this);
		return -1;
	    }
	}
	else
	    return -1;
    }
    return cnt;
}
			
/* vi: set ts=8 sw=4 sts=4 noet: */
