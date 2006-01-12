/**
 * Iterator.cpp
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

ListIterator::ListIterator(ObjList& list)
    : m_objList(&list), m_hashList(0),
      m_objects(0), m_length(0), m_current(0)
{
    m_length = list.count();
    if (!m_length)
	return;
    m_objects = new GenObject* [m_length];
    unsigned int i = 0;
    for (ObjList* l = list.skipNull(); i < m_length; l = l->skipNext()) {
	if (!l)
	    break;
	m_objects[i++] = l->get();
    }
    while (i < m_length)
	m_objects[i++] = 0;
}

ListIterator::ListIterator(HashList& list)
    : m_objList(0), m_hashList(&list),
      m_objects(0), m_length(0), m_current(0)
{
    m_length = list.count();
    if (!m_length)
	return;
    m_objects = new GenObject* [m_length];
    unsigned int i = 0;
    for (unsigned int n = 0; n < list.length(); n++) {
	ObjList* l = list.getList(n);
	if (!l)
	    continue;
	for (l = l->skipNull(); i < m_length; l = l->skipNext()) {
	    if (!l)
		break;
	    m_objects[i++] = l->get();
	}
    }
    while (i < m_length)
	m_objects[i++] = 0;
}

ListIterator::~ListIterator()
{
    m_length = 0;
    delete m_objects;
}

GenObject* ListIterator::get(unsigned int index) const
{
    if ((index >= m_length) || !m_objects)
	return 0;
    GenObject* obj = m_objects[index];
    if (!obj)
	return 0;
    if (m_objList) {
	if (m_objList->find(obj) && obj->alive())
	    return obj;
    }
    else if (m_hashList) {
	if (m_hashList->find(obj) && obj->alive())
	    return obj;
    }
    return 0;
}

GenObject* ListIterator::get()
{
    while (m_current < m_length) {
	GenObject* obj = get(m_current++);
	if (obj)
	    return obj;
    }
    return 0;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
