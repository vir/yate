/**
 * Cipher.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2008 Null Team
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

#include "yatengine.h"

using namespace TelEngine;

const TokenDict Cipher::s_directions[] = 
{
    { "bidir",   Cipher::Bidir   },
    { "encrypt", Cipher::Encrypt },
    { "decrypt", Cipher::Decrypt },
    { 0, 0 }
};

Cipher::~Cipher()
{
}

void* Cipher::getObject(const String& name) const
{
    if (name == YSTRING("Cipher"))
	return const_cast<Cipher*>(this);
    return GenObject::getObject(name);
}

bool Cipher::valid(Direction dir) const
{
    return true;
}

unsigned int Cipher::initVectorSize() const
{
    return 0;
}

unsigned int Cipher::bufferSize(unsigned int len) const
{
    unsigned int bSize = blockSize();
    if (bSize <= 1)
	return len;
    return bSize * ((len + bSize - 1) / bSize);
}

bool Cipher::bufferFull(unsigned int len) const
{
    unsigned int bSize = blockSize();
    if (bSize <= 1)
	return true;
    return 0 == (len % bSize);
}

bool Cipher::initVector(const void* vect, unsigned int len, Direction dir)
{
    return false;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
