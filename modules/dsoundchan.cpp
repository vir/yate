/**
 * dsoundchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * DirectSound channel driver for Windows.
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

// define DCOM before including windows.h so advanced COM functions can be compiled
#define _WIN32_DCOM

#include <yatephone.h>

#include <string.h>

// initialize the GUIDs so we don't need to link against dsound.lib
#include <initguid.h>
#include <dsound.h>

using namespace TelEngine;

// all COM objects are created in this thread's apartment
class DSound : public Thread
{
public:
    DSound();
    virtual ~DSound();
    virtual void run();
    virtual void cleanup();
    bool init();
    inline LPDIRECTSOUND dsound()
	{ return m_ds; }
    inline LPDIRECTSOUNDCAPTURE dcapture()
	{ return m_dsc; }
private:
    LPDIRECTSOUND m_ds;
    LPDIRECTSOUNDCAPTURE m_dsc;
};

class SoundSource : public ThreadedSource
{
public:
    ~SoundSource();
    virtual void run();
    inline const String &name()
	{ return m_name; }
private:
    /*SoundSource(const String &tone);
    static const Tone *getBlock(const String &tone);
    String m_name;
    const Tone *m_tone;
    DataBlock m_data;
    unsigned m_brate;
    unsigned m_total;
    u_int64_t m_time;*/
    String m_name;
};

class SoundChan;

class SoundConsumer : public DataConsumer
{
public:
    SoundConsumer(SoundChan *chan,String device = "/dev/dsp"){}

    ~SoundConsumer();

    virtual void Consume(const DataBlock &data, unsigned long timeDelta){}

private:
    SoundChan *m_chan;
    unsigned m_total;
    u_int64_t m_time;
};

class SoundChan : public Channel
{
public:
    SoundChan(const String &tone);
    ~SoundChan();
    LPDIRECTSOUNDBUFFER m_dsb;
};

class AttachHandler : public MessageHandler
{
public:
    AttachHandler() : MessageHandler("chan.attach") { }
    virtual bool received(Message &msg);
};

class SoundDriver : public Driver
{
    friend class DSound;
public:
    SoundDriver();
    ~SoundDriver();
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest);
    inline LPDIRECTSOUND dsound()
	{ return m_dsound ? m_dsound->dsound() : 0; }
    inline LPDIRECTSOUNDCAPTURE dcapture()
	{ return m_dsound ? m_dsound->dcapture() : 0; }
protected:
    void statusModule(String& str);
    void statusParams(String& str);
private:
    DSound* m_dsound;
    AttachHandler* m_handler;
};

INIT_PLUGIN(SoundDriver);

DSound::DSound()
    : Thread("DirectSound"), m_ds(0), m_dsc(0)
{
}

DSound::~DSound()
{
    __plugin.m_dsound = 0;
}

bool DSound::init()
{
    HRESULT hr;
    if (FAILED(hr = ::CoInitializeEx(NULL,COINIT_MULTITHREADED))) {
	Debug(DebugGoOn,"Could not initialize the COM library, code 0x%X",hr);
	return false;
    }
    if (FAILED(hr = ::CoCreateInstance(CLSID_DirectSound, NULL, CLSCTX_INPROC_SERVER,
	IID_IDirectSound, (void**)&m_ds)) || !m_ds) {
	Debug(DebugGoOn,"Could not create the DirectSound object, code 0x%X",hr);
	return false;
    }
    if (FAILED(hr = IDirectSound_Initialize(m_ds, 0))) {
	Debug(DebugGoOn,"Could not initialize the DirectSound object, code 0x%X",hr);
	return false;
    }
    if (FAILED(hr = ::CoCreateInstance(CLSID_DirectSoundCapture, NULL, CLSCTX_INPROC_SERVER,
	IID_IDirectSoundCapture, (void**)&m_dsc)) || !m_dsc) {
	Debug(DebugGoOn,"Could not create the DirectSoundCapture object, code 0x%X",hr);
	return false;
    }
    if (FAILED(hr = IDirectSoundCapture_Initialize(m_dsc, 0))) {
	Debug(DebugGoOn,"Could not initialize the DirectSoundCapture object, code 0x%X",hr);
	return false;
    }
    return true;
}

void DSound::run()
{
    if (!init())
	return;
    Debug(DebugInfo,"DirectSound is initialized and running");
    for (;;) {
	msleep(1,true);
    }
}

void DSound::cleanup()
{
    if (m_dsc) {
	m_dsc->Release();
	m_dsc = 0;
    }
    if (m_ds) {
	m_ds->Release();
	m_ds = 0;
    }
    ::CoUninitialize();
}

SoundChan::SoundChan(const String &tone)
    : Channel(__plugin)
{
    Debug(DebugAll,"ToneChan::ToneChan(\"\") [%p]",this);

    DSBUFFERDESC dsbdesc;
    ZeroMemory(&dsbdesc, sizeof(DSBUFFERDESC));
    dsbdesc.dwSize = sizeof(DSBUFFERDESC);
    dsbdesc.dwFlags = DSBCAPS_PRIMARYBUFFER;
    if FAILED(__plugin.dsound()->CreateSoundBuffer(&dsbdesc, &m_dsb, NULL))
        return;
   /* ToneSource *t = ToneSource::getTone(tone);
    if (t) {
	setSource(t);
	t->deref();
    }
    else
	Debug(DebugWarn,"No source tone '%s' in ToneChan [%p]",tone.c_str(),this);
	*/
}

SoundChan::~SoundChan()
{
    Debug(DebugAll,"SoundChan::~SoundChan()  [%p]",this);
}

bool AttachHandler::received(Message &msg)
{
    return TRUE;
}

bool SoundDriver::msgExecute(Message& msg, String& dest)
{
    /*
    Channel *dd = static_cast<Channel*>(msg.userData());
    if (dd) {
	ToneChan *tc = new ToneChan(dest);
	if (dd->connect(tc))
	    tc->deref();
	else {
	    tc->destruct();
	    return false;
	}
    }
    else {
	const char *targ = msg.getValue("target");
	if (!targ) {
	    Debug(DebugWarn,"Tone outgoing call with no target!");
	    return false;
	}
	Message m("call.route");
	m.addParam("module","tone");
	m.addParam("caller",dest);
	m.addParam("called",targ);
	if (Engine::dispatch(m)) {
	    m = "call.execute";
	    m.addParam("callto",m.retValue());
	    m.retValue().clear();
	    ToneChan *tc = new ToneChan(dest);
	    m.setParam("targetid",tc->id());
	    m.userData(tc);
	    if (Engine::dispatch(m)) {
		tc->deref();
		return true;
	    }
	    Debug(DebugWarn,"Tone outgoing call not accepted!");
	    tc->destruct();
	}
	else
	    Debug(DebugWarn,"Tone outgoing call but no route!");
	return false;
    }*/
    return true;
}

void SoundDriver::statusModule(String& str)
{
    Module::statusModule(str);
}

void SoundDriver::statusParams(String& str)
{
    //str << "sound=" << tones.count() << ",chans=" << channels().count();
}

SoundDriver::SoundDriver()
    : Driver("dsound","misc"), m_dsound(0), m_handler(0)
{
    Output("Loaded module DirectSound");
}

SoundDriver::~SoundDriver()
{
    Output("Unloading module DirectSound");
    channels().clear();
}

void SoundDriver::initialize()
{
    Output("Initializing module DirectSound");
    if (!m_dsound) {
	m_dsound = new DSound;
	m_dsound->startup();
    }
    setup(0,true); // no need to install notifications
    Driver::initialize();
    if (!m_handler) {
	m_handler = new AttachHandler;
	Engine::install(m_handler);
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
