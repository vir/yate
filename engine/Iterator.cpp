/**
 * Iterator.cpp
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

#include "yateclass.h"

using namespace TelEngine;

ListIterator::ListIterator(ObjList& list, int offset)
    : m_objects(0), m_hashes(0)
{
    assign(list,offset);
}

ListIterator::ListIterator(HashList& list, int offset)
    : m_objects(0), m_hashes(0)
{
    assign(list,offset);
}

ListIterator::~ListIterator()
{
    m_length = 0;
    delete[] m_objects;
    if (m_hashes)
	delete[] m_hashes;
}

void ListIterator::clear()
{
    m_length = 0;
    m_current = 0;
    m_objList = 0;
    m_hashList = 0;
    GenObject** tmp = m_objects;
    m_objects = 0;
    delete[] tmp;
    if (m_hashes) {
	unsigned int* tmph = m_hashes;
	m_hashes = 0;
	delete[] tmph;
    }
}

void ListIterator::assign(ObjList& list, int offset)
{
    clear();
    m_objList = &list;
    m_length = list.count();
    if (!m_length)
	return;
    m_objects = new GenObject* [m_length];
    offset = (m_length - offset) % m_length;
    unsigned int i = 0;
    for (ObjList* l = list.skipNull(); i < m_length; l = l->skipNext()) {
	if (!l)
	    break;
	m_objects[((i++) + offset) % m_length] = l->get();
    }
    while (i < m_length)
	m_objects[((i++) + offset) % m_length] = 0;
}

void ListIterator::assign(HashList& list, int offset)
{
    clear();
    m_hashList = &list;
    m_length = list.count();
    if (!m_length)
	return;
    m_objects = new GenObject* [m_length];
    m_hashes = new unsigned int[m_length];
    offset = (m_length - offset) % m_length;
    unsigned int i = 0;
    for (unsigned int n = 0; n < list.length(); n++) {
	ObjList* l = list.getList(n);
	if (!l)
	    continue;
	for (l = l->skipNull(); i < m_length; l = l->skipNext()) {
	    if (!l)
		break;
	    unsigned int idx = ((i++) + offset) % m_length;
	    m_objects[idx] = l->get();
	    m_hashes[idx] = l->get()->toString().hash();
	}
    }
    while (i < m_length)
	m_objects[((i++) + offset) % m_length] = 0;
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
	if (m_hashList->find(obj,m_hashes[index]) && obj->alive())
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


const NamedString* NamedIterator::get()
{
    if (!m_item)
	return 0;
    const NamedString* item = static_cast<const NamedString*>(m_item->get());
    m_item = m_item->skipNext();
    return item;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
