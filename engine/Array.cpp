/**
 * Array.cpp
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

Array::Array(int columns, int rows)
    : m_rows(rows), m_columns(columns)
{
    if (rows && columns) {
	ObjList* column = &m_obj;
	for (int i=0; i<columns; i++) {
	    ObjList *a = new ObjList();
	    // for the first column set the row directly
	    if (i == 0)
		column->set(a);
	    else
		column = column->append(a,false);
	    // and add some empty holders for each item
	    for (int k=1; k<rows; k++)
		a = a->append(0,false);
	}
    }
}

Array::~Array()
{
    m_rows = 0;
    m_columns = 0;
    m_obj.clear();
}

void* Array::getObject(const String& name) const
{
    if (name == YATOM("Array"))
	return const_cast<Array*>(this);
    return RefObject::getObject(name);
}

bool Array::addRow(ObjList* row, int index)
{
    if (index < 0)
	index = m_rows;
    if (index > m_rows)
	return false;
    for (int i=0; i<m_columns; i++) {
	ObjList* list = static_cast<ObjList*>(m_obj[i]);
	if (!list)
	    continue;
	GenObject* item = row ? (*row)[i] : 0;
	if (index == m_rows)
	    list->append(item,false);
	else {
	    list = (*list) + index;
	    if (list)
		list->insert(item,false);
	}
    }
    m_rows++;
    return true;
}

bool Array::addColumn(ObjList* column, int index)
{
    if (index < 0)
	index = m_columns;
    if (index > m_columns)
	return false;
    if (index == m_columns)
	(m_obj+index)->append(column,false);
    else
	(m_obj+index)->insert(column,false);
    m_columns++;
    return true;
}

bool Array::delRow(int index)
{
    if (index < 0 || index >= m_rows)
	return false;
    for (int i=0; i<m_columns; i++)
	((*(ObjList *)(m_obj[i]))+index)->remove();
    m_rows--;
    return true;
}

bool Array::delColumn(int index)
{
    if (index < 0 || index >= m_columns)
	return false;
    (m_obj+index)->remove();
    m_columns--;
    return true;
}

GenObject* Array::get(int column,int row) const
{
    if (column < 0 || column >= m_columns || row < 0 || row >= m_rows)
	return 0;
    ObjList* l = static_cast<ObjList*>(m_obj[column]);
    if (l)
	l = (*l)+row;
    if (l)
	return l->get();
    Debug(DebugFail,"Array %p get item holder (%d,%d) does not exist!",this,column,row);
    return 0;
}

GenObject* Array::take(int column,int row)
{
    if (column < 0 || column >= m_columns || row < 0 || row >= m_rows)
	return 0;
    ObjList* l = static_cast<ObjList*>(m_obj[column]);
    if (l)
	l = (*l)+row;
    if (l)
	return l->set(0,false);
    Debug(DebugFail,"Array %p take item holder (%d,%d) does not exist!",this,column,row);
    return 0;
}

bool Array::set(GenObject *obj, int column, int row)
{
    if (column < 0 || column >= m_columns || row < 0 || row >= m_rows)
	return false;
//    ((*(ObjList *)(m_obj[column]))+row)->set(obj);
//    return true;
    ObjList* l = static_cast<ObjList*>(m_obj[column]);
    if (l)
	l = (*l)+row;
    if (l) {
	l->set(obj);
	return true;
    }
    Debug(DebugFail,"Array %p set item holder (%d,%d) does not exist!",this,column,row);
    return false;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
