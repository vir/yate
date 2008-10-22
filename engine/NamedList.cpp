/**
 * NamedList.cpp
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

#include "yateclass.h"

using namespace TelEngine;

static const NamedList s_empty("");

const NamedList& NamedList::empty()
{
    return s_empty;
}

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

void* NamedList::getObject(const String& name) const
{
    if (name == "NamedList")
	return const_cast<NamedList*>(this);
    return String::getObject(name);
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
    if (!param)
	return *this;
    ObjList* p = m_params.find(param->name());
    if (p)
	p->set(param);
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

NamedList& NamedList::clearParam(const String& name, char childSep)
{
    XDebug(DebugInfo,"NamedList::clearParam(\"%s\",'%1s')",
	name.c_str(),&childSep);
    String tmp;
    if (childSep)
	tmp << name << childSep;
    ObjList *p = &m_params;
    while (p) {
        NamedString *s = static_cast<NamedString *>(p->get());
        if (s && ((s->name() == name) || s->name().startsWith(tmp)))
            p->remove();
	else
	    p = p->next();
    }
    return *this;
}

NamedList& NamedList::copyParam(const NamedList& original, const String& name, char childSep)
{
    XDebug(DebugInfo,"NamedList::copyParam(%p,\"%s\",'%1s')",
	&original,name.c_str(),&childSep);
    if (!childSep) {
	// faster and simpler - used in most cases
	const NamedString* s = original.getParam(name);
	return s ? setParam(name,*s) : clearParam(name);
    }
    clearParam(name,childSep);
    String tmp;
    tmp << name << childSep;
    unsigned int n = original.length();
    for (unsigned int i = 0; i < n; i++) {
	const NamedString* s = original.getParam(i);
        if (s && ((s->name() == name) || s->name().startsWith(tmp)))
	    addParam(s->name(),*s);
    }
    return *this;
}

NamedList& NamedList::copyParams(const NamedList& original)
{
    XDebug(DebugInfo,"NamedList::copyParams(%p) [%p]",&original,this);
    for (unsigned int i = 0; i < original.length(); i++) {
	const NamedString* p = original.getParam(i);
	if (p)
	    setParam(p->name(),*p);
    }
    return *this;
}

NamedList& NamedList::copyParams(const NamedList& original, ObjList* list, char childSep)
{
    XDebug(DebugInfo,"NamedList::copyParams(%p,%p,'%1s') [%p]",
	&original,list,&childSep,this);
    for (; list; list = list->next()) {
	GenObject* obj = list->get();
	if (!obj)
	    continue;
	String name = obj->toString();
	name.trimBlanks();
	if (name)
	    copyParam(original,name,childSep);
    }
    return *this;
}

NamedList& NamedList::copyParams(const NamedList& original, const String& list, char childSep)
{
    XDebug(DebugInfo,"NamedList::copyParams(%p,\"%s\",'%1s') [%p]",
	&original,list.c_str(),&childSep,this);
    ObjList* l = list.split(',',false);
    if (l) {
	copyParams(original,l,childSep);
	l->destruct();
    }
    return *this;
}

int NamedList::getIndex(const NamedString* param) const
{
    if (!param)
	return -1;
    const ObjList *p = &m_params;
    for (int i=0; p; p=p->next(),i++) {
        if (static_cast<const NamedString *>(p->get()) == param)
            return i;
    }
    return -1;
}

int NamedList::getIndex(const String& name) const
{
    const ObjList *p = &m_params;
    for (int i=0; p; p=p->next(),i++) {
        NamedString *s = static_cast<NamedString *>(p->get());
        if (s && (s->name() == name))
            return i;
    }
    return -1;
}

NamedString* NamedList::getParam(const String& name) const
{
    XDebug(DebugInfo,"NamedList::getParam(\"%s\")",name.c_str());
    const ObjList *p = m_params.skipNull();
    for (; p; p=p->skipNext()) {
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

const String& NamedList::operator[](const String& name) const
{
    const String* s = getParam(name);
    return s ? *s : String::empty();
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
    int p1 = 0;
    int cnt = 0;
    while ((p1 = str.find("${",p1)) >= 0) {
	int p2 = str.find('}',p1+2);
	if (p2 > 0) {
	    String tmp = str.substr(p1+2,p2-p1-2);
	    tmp.trimBlanks();
	    DDebug(DebugAll,"NamedList replacing parameter '%s' [%p]",tmp.c_str(),this);
	    tmp = getValue(tmp);
	    if (sqlEsc)
		tmp = tmp.sqlEscape(extraEsc);
	    str = str.substr(0,p1) + tmp + str.substr(p2+1);
	    // advance search offset past the string we just replaced
	    p1 += tmp.length();
	    cnt++;
	}
	else
	    return -1;
    }
    return cnt;
}
			
/* vi: set ts=8 sw=4 sts=4 noet: */
