/**
 * Cipher.cpp
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
    if (name == YATOM("Cipher"))
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
