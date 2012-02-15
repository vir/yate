/**
 * jsobject.cpp
 * Yet Another (Java)script library
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2011 Null Team
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

#include "yatescript.h"

using namespace TelEngine;

namespace { // anonymous

// Base class for all native objects that hold a NamedList
class JsNative : public JsObject
{
    YCLASS(JsNative,JsObject)
public:
    inline JsNative(const char* name, NamedList* list)
	: JsObject(name),
	  m_list(list)
	{ }
    virtual NamedList& list()
	{ return *m_list; }
    virtual const NamedList& list() const
	{ return *m_list; }
private:
    NamedList* m_list;
};

// Array object
class JsArray : public JsObject
{
    YCLASS(JsArray,JsObject)
public:
    inline JsArray()
	: JsObject("Array")
	{ }
};

// Function object
class JsFunction : public JsObject
{
    YCLASS(JsFunction,JsObject)
public:
    inline JsFunction()
	: JsObject("Function")
	{ }
};

// Object constructor
class JsConstructor : public JsFunction
{
    YCLASS(JsConstructor,JsFunction)
public:
    inline JsConstructor()
	{ }
};

// Date object
class JsDate : public JsObject
{
    YCLASS(JsDate,JsObject)
public:
    inline JsDate()
	: JsObject("Date")
	{
	    addParam(new ExpOperation(ExpEvaluator::OpcFunc,"now"));
	}
};

// Math class - not really an object, all methods are static
class JsMath : public JsObject
{
    YCLASS(JsMath,JsObject)
public:
    inline JsMath()
	: JsObject("Math")
	{
	    addParam(new ExpOperation(ExpEvaluator::OpcFunc,"abs"));
	}
};

}; // anonymous namespace


// Helper function that adds an object to a parent
static inline void addObject(NamedList& params, const char* name, NamedList* obj)
{
    params.addParam(new NamedPointer(name,obj,obj->toString()));
}


// Initialize standard globals in the execution context
void JsObject::initialize(ScriptContext& context)
{
    NamedList& params = context.params();
    static_cast<String&>(params) = "[Object Global]";
    if (!params.getParam(YSTRING("Object")))
	addObject(params,"Object",new JsObject);
    if (!params.getParam(YSTRING("Function")))
	addObject(params,"Function",new JsFunction);
    if (!params.getParam(YSTRING("Date")))
	addObject(params,"Date",new JsDate);
    if (!params.getParam(YSTRING("Math")))
	addObject(params,"Math",new JsMath);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
