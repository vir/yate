/**
 * tup.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Signalling Stack - implements the support for SS7, ISDN and PSTN
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

#include "yatesig.h"


using namespace TelEngine;

SS7TUP::SS7TUP(const NamedList& params, unsigned char sio)
    : SignallingComponent("SS7TUP",&params),
      SignallingCallControl(params,"tup."),
      SS7Layer4(sio,&params)
{
}

SS7TUP::~SS7TUP()
{
}

/* vi: set ts=8 sw=4 sts=4 noet: */
