/**
 * HashList.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 *
 * Idea and initial implementation (as HashTable) by Maciek Kaminski
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

HashList::HashList(unsigned int size)
    : m_size(size), m_lists(0)
{
    XDebug(DebugAll,"HashList::HashList(%u) [%p]",size,this);
    if (m_size < 1)
	m_size = 1;
    if (m_size > 1024)
	m_size = 1024;
    m_lists = new ObjList* [m_size];
    for (unsigned int i = 0; i < m_size; i++)
	m_lists[i] = 0;
}

HashList::~HashList()
{
    XDebug(DebugAll,"HashList::~HashList() [%p]",this);
    clear();
    delete[] m_lists;
}

void* HashList::getObject(const String& name) const
{
    if (name == YATOM("HashList"))
	return const_cast<HashList*>(this);
    return GenObject::getObject(name);
}

unsigned int HashList::count() const
{
    unsigned int c = 0;
    for (unsigned int i = 0; i < m_size; i++)
	if (m_lists[i])
	    c += m_lists[i]->count();
    return c;
}

GenObject* HashList::operator[](const String& str) const
{
    ObjList *obj = find(str);
    return obj ? obj->get() : 0;
}

ObjList* HashList::find(const GenObject* obj) const
{
    XDebug(DebugAll,"HashList::find(%p) [%p]",obj,this);
    if (!obj)
	return 0;
    ObjList* found = 0;
    for (unsigned int i = 0; !found && i < m_size; i++)
	if (m_lists[i])
	    found = m_lists[i]->find(obj);
    return found;
}

ObjList* HashList::find(const GenObject* obj, unsigned int hash) const
{
    XDebug(DebugAll,"HashList::find(%p,%u) [%p]",obj,hash,this);
    if (!obj)
	return 0;
    unsigned int i = hash % m_size;
    return m_lists[i] ? m_lists[i]->find(obj) : 0;
}

ObjList* HashList::find(const String& str) const
{
    XDebug(DebugAll,"HashList::find(\"%s\") [%p]",str.c_str(),this);
    unsigned int i = str.hash() % m_size;
    return m_lists[i] ? m_lists[i]->find(str) : 0;
}

ObjList* HashList::append(const GenObject* obj)
{
    XDebug(DebugAll,"HashList::append(%p) [%p]",obj,this);
    if (!obj)
	return 0;
    unsigned int i = obj->toString().hash() % m_size;
    if (!m_lists[i])
	m_lists[i] = new ObjList;
    return m_lists[i]->append(obj);
}

GenObject* HashList::remove(GenObject* obj, bool delobj, bool useHash)
{
    ObjList* n = 0;
    if (useHash && obj)
	n = find(obj,obj->toString().hash());
    else
	n = find(obj);
    return n ? n->remove(delobj) : 0;
}

void HashList::clear()
{
    XDebug(DebugAll,"HashList::clear() [%p]",this);
    for (unsigned int i = 0; i < m_size; i++)
	TelEngine::destruct(m_lists[i]);
}

bool HashList::resync(GenObject* obj)
{
    XDebug(DebugAll,"HashList::resync(%p) [%p]",obj,this);
    if (!obj)
	return false;
    unsigned int i = obj->toString().hash() % m_size;
    if (m_lists[i] && m_lists[i]->find(obj))
	return false;
    for (unsigned int n = 0; n < m_size; n++) {
	if ((n == i) || !m_lists[n])
	    continue;
	ObjList* l = m_lists[n]->find(obj);
	if (!l)
	    continue;
	bool autoDel = l->autoDelete();
	m_lists[n]->remove(obj,false);
	if (!m_lists[i])
	    m_lists[i] = new ObjList;
	m_lists[i]->append(obj)->setDelete(autoDel);
	return true;
    }
    return false;
}

bool HashList::resync()
{
    XDebug(DebugAll,"HashList::resync() [%p]",this);
    bool moved = false;
    for (unsigned int n = 0; n < m_size; n++) {
	ObjList* l = m_lists[n];
	while (l) {
	    GenObject* obj = l->get();
	    if (obj) {
		unsigned int i = obj->toString().hash() % m_size;
		if (i != n) {
		    bool autoDel = l->autoDelete();
		    m_lists[n]->remove(obj,false);
		    if (!m_lists[i])
			m_lists[i] = new ObjList;
		    m_lists[i]->append(obj)->setDelete(autoDel);
		    moved = true;
		    continue;
		}
	    }
	    l = l->next();
	}
    }
    return moved;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
