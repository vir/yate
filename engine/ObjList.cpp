/**
 * ObjList.cpp
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

ObjList::ObjList()
    : m_next(0), m_obj(0), m_delete(true)
{
#ifdef DEBUG
    Debug(DebugAll,"ObjList::ObjList() [%p]",this);
#endif
}

ObjList::~ObjList()
{
#ifdef DEBUG
    Debugger debug("ObjList::~ObjList()"," [%p]",this);
#endif
    if (m_obj) {
	GenObject *tmp = m_obj;
	m_obj = 0;
	if (m_delete) {
#ifdef DEBUG
	    Debug(DebugInfo,"ObjList::~ObjList() deleting %p",tmp);
#endif
	    tmp->destruct();
	}
    }
    if (m_next)
	m_next->destruct();
}

unsigned int ObjList::length() const
{
    unsigned int c = 0;
    const ObjList *n = this;
    while (n) {
	c++;
	n = n->next();
    }
    return c;
}

unsigned int ObjList::count() const
{
    unsigned int c = 0;
    const ObjList *n = this;
    while (n) {
	if (n->get())
	    c++;
	n = n->next();
    }
    return c;
}

ObjList *ObjList::last() const
{
    const ObjList *n = this;
    while (n->next())
	n = n->next();
    return const_cast<ObjList *>(n);
}

ObjList *ObjList::operator[](int index) const
{
    if (index < 0)
	return 0;
    ObjList *obj = const_cast<ObjList *>(this);
    for (;obj;obj=obj->next(),index--)
	if (!index) break;
    return obj;
}

ObjList *ObjList::find(const GenObject *obj) const
{
#ifdef DEBUG
    Debugger debug("ObjList::find","(%p) [%p]",obj,this);
#endif
    const ObjList *n = this;
    while (n && (n->get() != obj))
	n = n->next();
#ifdef DEBUG
    Debug(DebugInfo,"ObjList::find returning %p",n);
#endif
    return const_cast<ObjList *>(n);
}

GenObject *ObjList::set(const GenObject *obj, bool delold)
{
    if (m_obj == obj)
	return 0;
    GenObject *tmp = m_obj;
    m_obj = const_cast<GenObject *>(obj);
    if (delold && tmp) {
	tmp->destruct();
	return 0;
    }
    return tmp;
}

ObjList *ObjList::insert(const GenObject *obj)
{
#ifdef DEBUG
    Debugger debug("ObjList::insert","(%p) [%p]",obj,this);
#endif
    if (m_obj) {
	ObjList *n = new ObjList();
	n->set(m_obj);
	set(obj,false);
	n->m_next = m_next;
	m_next = n;
    }
    else
	m_obj = const_cast<GenObject *>(obj);
    return this;
}

ObjList *ObjList::append(const GenObject *obj)
{
#ifdef DEBUG
    Debugger debug("ObjList::append","(%p) [%p]",obj,this);
#endif
    ObjList *n = last();
    if (n->get()) {
	n->m_next = new ObjList();
	n = n->m_next;
    }
    n->set(obj);
    return n;
}

GenObject *ObjList::remove(bool delobj)
{
    GenObject *tmp = m_obj;

    if (m_next) {
	ObjList *n = m_next;
	m_obj = n->get();
	m_next = n->next();
	n->m_obj = 0;
	n->m_next = 0;
	n->destruct();
    }
    else
	m_obj = 0;

    if (delobj && tmp) {
#ifdef DEBUG
	Debug(DebugInfo,"ObjList::remove() deleting %p",tmp);
#endif
	tmp->destruct();
	tmp = 0;
    }
    return tmp;
}

GenObject *ObjList::remove(GenObject *obj, bool delobj)
{
    ObjList *n = find(obj);
    return n ? n->remove(delobj) : 0;
}

void ObjList::clear()
{
#ifdef DEBUG
    Debugger debug("ObjList::clear()"," [%p]",this);
#endif
    ObjList *n = m_next;
    m_next = 0;
    remove(m_delete);
    if (n)
	n->destruct();
}
