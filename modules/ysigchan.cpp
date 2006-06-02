/**
 * ysigchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Signalling Channel
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
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

#include <yatephone.h>
#include <yatess7.h>

using namespace TelEngine;
namespace { // anonymous

class SigDriver : public Driver
{
public:
    SigDriver();
    ~SigDriver();
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest);
    inline SignallingEngine* engine() const
	{ return m_engine; }
private:
    void buildStack();
    SignallingEngine* m_engine;
};

static SigDriver plugin;

bool SigDriver::msgExecute(Message& msg, String& dest)
{
    Debug(this,DebugStub,"Signalling call!");
    return false;
}

SigDriver::SigDriver()
    : Driver("ss7","fixchans"), m_engine(0)
{
    Output("Loaded module Signalling Channel");
}

SigDriver::~SigDriver()
{
    Output("Unloading module Signalling Channel");
}

void SigDriver::buildStack()
{
    m_engine = new SignallingEngine;
    NamedList ifdefs("WpInterface");
    ifdefs.addParam("card","wanpipe9");
    ifdefs.addParam("device","w9g1");
    SignallingInterface* iface = static_cast<SignallingInterface*>(SignallingFactory::build(ifdefs,&ifdefs));
    if (!iface) {
	Debug(this,DebugGoOn,"Failed to create interface '%s'",ifdefs.c_str());
	return;
    }
    SS7MTP2* link = new SS7MTP2;
    m_engine->insert(link);
    link->SignallingReceiver::attach(iface);
    m_engine->start("SS7test",Thread::Normal,20000);
    iface->control(SignallingInterface::Enable);
    link->control(SS7Layer2::Align);
}

void SigDriver::initialize()
{
    Output("Initializing module Signalling Channel");
    setup();
    if (!m_engine)
	buildStack();
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
