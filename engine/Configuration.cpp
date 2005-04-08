/**
 * Configuration.cpp
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

#include <stdio.h>
#include <string.h>

using namespace TelEngine;

Configuration::Configuration()
{
}

Configuration::Configuration(const char* filename)
    : String(filename)
{
    load();
}

ObjList* Configuration::getSectHolder(const String& sect) const
{
    if (sect.null())
	return 0;
    return const_cast<ObjList*>(m_sections.find(sect));
}

ObjList* Configuration::makeSectHolder(const String& sect)
{
    if (sect.null())
	return 0;
    ObjList *l = getSectHolder(sect);
    if (!l)
	l = m_sections.append(new NamedList(sect));
    return l;
}

NamedList* Configuration::getSection(unsigned int index) const
{
    return static_cast<NamedList *>(m_sections[index]);
}

NamedList* Configuration::getSection(const String& sect) const
{
    ObjList *l = getSectHolder(sect);
    return l ? static_cast<NamedList *>(l->get()) : 0;
}

NamedString* Configuration::getKey(const String& sect, const String& key) const
{
    NamedList *l = getSection(sect);
    return l ? l->getParam(key) : 0;
}

const char* Configuration::getValue(const String& sect, const String& key, const char* defvalue) const
{
    const NamedString *s = getKey(sect,key);
    return s ? s->c_str() : defvalue;
}

int Configuration::getIntValue(const String& sect, const String& key, int defvalue) const
{
    const NamedString *s = getKey(sect,key);
    return s ? s->toInteger(defvalue) : defvalue;
}

int Configuration::getIntValue(const String& sect, const String& key, const TokenDict* tokens, int defvalue) const
{
    const NamedString *s = getKey(sect,key);
    return s ? s->toInteger(tokens,defvalue) : defvalue;
}

bool Configuration::getBoolValue(const String& sect, const String& key, bool defvalue) const
{
    const NamedString *s = getKey(sect,key);
    return s ? s->toBoolean(defvalue) : defvalue;
}

void Configuration::clearSection(const char* sect)
{
    if (sect) {
	ObjList *l = getSectHolder(sect);
	if (l)
	    l->remove();
    }
    else
	m_sections.clear();
}

void Configuration::clearKey(const String& sect, const String& key)
{
    NamedList *l = getSection(sect);
    if (l)
	l->clearParam(key);
}

void Configuration::addValue(const String& sect, const char* key, const char* value)
{
    DDebug(DebugInfo,"Configuration::addValue(\"%s\",\"%s\",\"%s\")",sect.c_str(),key,value);
    ObjList *l = makeSectHolder(sect);
    if (!l)
	return;
    NamedList *n = static_cast<NamedList *>(l->get());
    if (n)
	n->addParam(key,value);
}

void Configuration::setValue(const String& sect, const char* key, const char* value)
{
    DDebug(DebugInfo,"Configuration::setValue(\"%s\",\"%s\",\"%s\")",sect.c_str(),key,value);
    ObjList *l = makeSectHolder(sect);
    if (!l)
	return;
    NamedList *n = static_cast<NamedList *>(l->get());
    if (n)
	n->setParam(key,value);
}

void Configuration::setValue(const String& sect, const char* key, int value)
{
    char buf[32];
    ::sprintf(buf,"%d",value);
    setValue(sect,key,buf);
}

void Configuration::setValue(const String& sect, const char* key, bool value)
{
    setValue(sect,key,String::boolText(value));
}

bool Configuration::load()
{
    m_sections.clear();
    if (null())
	return false;
    FILE *f = ::fopen(c_str(),"r");
    if (f) {
	String sect;
	for (;;) {
	    char buf[1024];
	    if (!::fgets(buf,sizeof(buf),f))
		break;

	    char *pc = ::strchr(buf,'\r');
	    if (pc)
		*pc = 0;
	    pc = ::strchr(buf,'\n');
	    if (pc)
		*pc = 0;
	    pc = buf;
	    while (*pc == ' ' || *pc == '\t')
		pc++;
	    switch (*pc) {
		case 0:
		case ';':
		    continue;
	    }
	    String s(pc);
	    if (s[0] == '[') {
		int r = s.find(']');
		if (r > 0) {
		    sect = s.substr(1,r-1);
		    makeSectHolder(sect);
		}
		continue;
	    }
	    int q = s.find('=');
	    if (q > 0)
		addValue(sect,s.substr(0,q).trimBlanks(),s.substr(q+1).trimBlanks());
	}
	::fclose(f);
	return true;
    }
    Debug(DebugInfo,"Failed to open config file '%s'",c_str());
    return false;
}

bool Configuration::save() const
{
    if (null())
	return false;
    FILE *f = ::fopen(c_str(),"w");
    if (f) {
	ObjList *ol = m_sections.skipNull();
	for (;ol;ol=ol->skipNext()) {
	    NamedList *nl = static_cast<NamedList *>(ol->get());
	    ::fprintf(f,"[%s]\n",nl->c_str());
	    unsigned int n = nl->length();
	    for (unsigned int i = 0; i < n; i++) {
		NamedString *ns = nl->getParam(i);
		if (ns) {
		    const char *v = ns->c_str();
		    if (!v)
			v = "";
		    ::fprintf(f,"%s=%s\n",ns->name().c_str(),v);
		}
	    }
	}
	::fclose(f);
	return true;
    }
    return false;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
