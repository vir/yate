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

#ifndef _WINDOWS
#error This module is only for Windows
#else

#include <string.h>

// initialize the GUIDs so we don't need to link against dsound.lib
#include <initguid.h>
#include <dsound.h>

using namespace TelEngine;

// we should use the primary sound buffer else we will lose sound while we have no input focus
#define USE_PRIMARY_BUFFER

// 20ms chunk, 100ms buffer
#define CHUNK_SIZE 320
#define MIN_SIZE (3*CHUNK_SIZE)
#define BUF_SIZE (4*CHUNK_SIZE)
#define MAX_SIZE (5*CHUNK_SIZE)

class DSoundSource : public DataSource
{
    friend class DSoundRec;
public:
    DSoundSource();
    ~DSoundSource();
private:
    DSoundRec* m_dsound;
};

class DSoundConsumer : public DataConsumer
{
    friend class DSoundPlay;
public:
    DSoundConsumer();
    ~DSoundConsumer();
    virtual void Consume(const DataBlock &data, unsigned long timeDelta);
private:
    DSoundPlay* m_dsound;
};

// all DirectSound play related objects are created in this thread's apartment
class DSoundPlay : public Thread, public Mutex
{
public:
    DSoundPlay(DSoundConsumer* owner, LPGUID device = 0);
    virtual ~DSoundPlay();
    virtual void run();
    virtual void cleanup();
    bool init();
    inline void terminate()
	{ m_owner = 0; }
    inline LPDIRECTSOUND dsound() const
	{ return m_ds; }
    inline LPDIRECTSOUNDBUFFER buffer() const
	{ return m_dsb; }
    void put(const DataBlock& data);
private:
    DSoundConsumer* m_owner;
    LPGUID m_device;
    LPDIRECTSOUND m_ds;
    LPDIRECTSOUNDBUFFER m_dsb;
    DWORD m_buffSize;
    DWORD m_writePos;
    DataBlock m_buf;
};

// all DirectSound record related objects are created in this thread's apartment
class DSoundRec : public Thread
{
public:
    DSoundRec(DSoundSource* owner, LPGUID device = 0);
    virtual ~DSoundRec();
    virtual void run();
    virtual void cleanup();
    bool init();
    inline void terminate()
	{ m_owner = 0; Thread::msleep(10); }
    inline LPDIRECTSOUNDCAPTURE dsound() const
	{ return m_ds; }
    inline LPDIRECTSOUNDCAPTUREBUFFER buffer() const
	{ return m_dsb; }
private:
    DSoundSource* m_owner;
    LPGUID m_device;
    LPDIRECTSOUNDCAPTURE m_ds;
    LPDIRECTSOUNDCAPTUREBUFFER m_dsb;
    DWORD m_buffSize;
    DWORD m_readPos;
};

class DSoundChan : public Channel
{
public:
    DSoundChan();
    virtual ~DSoundChan();
};

class AttachHandler : public MessageHandler
{
public:
    AttachHandler() : MessageHandler("chan.attach")
	{ }
    virtual bool received(Message &msg);
};

class SoundDriver : public Driver
{
    friend class DSoundPlay;
    friend class DSoundRec;
public:
    SoundDriver();
    ~SoundDriver();
    virtual void initialize();
    virtual bool msgExecute(Message& msg, String& dest);
private:
    AttachHandler* m_handler;
};

INIT_PLUGIN(SoundDriver);

DSoundPlay::DSoundPlay(DSoundConsumer* owner, LPGUID device)
    : Thread("DirectSound Play",High),
      m_owner(owner), m_device(device), m_ds(0), m_dsb(0),
      m_buffSize(0), m_writePos(0)
{
}

DSoundPlay::~DSoundPlay()
{
    if (m_owner)
	m_owner->m_dsound = 0;
}

bool DSoundPlay::init()
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
    if (FAILED(hr = m_ds->Initialize(m_device))) {
	Debug(DebugGoOn,"Could not initialize the DirectSound object, code 0x%X",hr);
	return false;
    }
    HWND wnd = GetForegroundWindow();
    if (!wnd)
	wnd = GetDesktopWindow();
#ifdef USE_PRIMARY_BUFFER
    if (FAILED(hr = m_ds->SetCooperativeLevel(wnd,DSSCL_WRITEPRIMARY))) {
#else
    if (FAILED(hr = m_ds->SetCooperativeLevel(wnd,DSSCL_EXCLUSIVE))) {
#endif
	Debug(DebugGoOn,"Could not set the DirectSound cooperative level, code 0x%X",hr);
	return false;
    }
    WAVEFORMATEX fmt;
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = 8000;
    fmt.nAvgBytesPerSec = 16000;
    fmt.nBlockAlign = 2;
    fmt.wBitsPerSample = 16;
    fmt.cbSize = 0;
    DSBUFFERDESC bdesc;
    ZeroMemory(&bdesc, sizeof(bdesc));
    bdesc.dwSize = sizeof(bdesc);
#ifdef USE_PRIMARY_BUFFER
    bdesc.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_STICKYFOCUS;
#else
    bdesc.dwFlags = DSBCAPS_LOCSOFTWARE ;
    bdesc.dwBufferBytes = BUF_SIZE;
    bdesc.lpwfxFormat = &fmt;
#endif
    if (FAILED(hr = m_ds->CreateSoundBuffer(&bdesc, &m_dsb, NULL)) || !m_dsb) {
	Debug(DebugGoOn,"Could not create the DirectSound buffer, code 0x%X",hr);
	return false;
    }
#ifdef USE_PRIMARY_BUFFER
    if (FAILED(hr = m_dsb->SetFormat(&fmt))) {
	Debug(DebugGoOn,"Could not set the DirectSound buffer format, code 0x%X",hr);
	return false;
    }
#endif
    if (FAILED(hr = m_dsb->GetFormat(&fmt,sizeof(fmt),0))) {
	Debug(DebugGoOn,"Could not get the DirectSound buffer format, code 0x%X",hr);
	return false;
    }
    if ((fmt.wFormatTag != WAVE_FORMAT_PCM) ||
	(fmt.nChannels != 1) ||
	(fmt.nSamplesPerSec != 8000) ||
	(fmt.wBitsPerSample != 16)) {
	Debug(DebugGoOn,"DirectSound does not support 8000Hz 16bit mono PCM format, "
	    "got fmt=%u, chans=%d samp=%d size=%u",
	    fmt.wFormatTag,fmt.nChannels,fmt.nSamplesPerSec,fmt.wBitsPerSample);
	return false;
    }
    DSBCAPS caps;
    caps.dwSize = sizeof(caps);
    if (FAILED(hr = m_dsb->GetCaps(&caps))) {
	Debug(DebugGoOn,"Could not get the DirectSound buffer capabilities, code 0x%X",hr);
	return false;
    }
    m_buffSize = caps.dwBufferBytes;
    Debug(&__plugin,DebugInfo,"DirectSound buffer size %u",m_buffSize);
    if (FAILED(hr = m_dsb->Play(0,0,DSBPLAY_LOOPING))) {
	if ((hr != DSERR_BUFFERLOST) || FAILED(hr = m_dsb->Restore())) {
	    Debug(DebugGoOn,"Could not play the DirectSound buffer, code 0x%X",hr);
	    return false;
	}
	m_dsb->Play(0,0,DSBPLAY_LOOPING);
    }
    return true;
}

void DSoundPlay::run()
{
    if (!init())
	return;
    if (m_owner)
	m_owner->m_dsound = this;
    bool first = true;
    Debug(&__plugin,DebugInfo,"DSoundPlay is initialized and running");
    while (m_owner) {
	msleep(1,true);
	if (first) {
	    if (m_buf.length() < MIN_SIZE)
		continue;
	    first = false;
	    m_dsb->GetCurrentPosition(NULL,&m_writePos);
	    Debug(&__plugin,DebugAll,"DSoundPlay has %u in buffer and starts playing at %u",
		m_buf.length(),m_writePos);
	}
	while (m_dsb && (m_buf.length() >= CHUNK_SIZE)) {
	    void* buf = 0;
	    void* buf2 = 0;
	    DWORD len = 0;
	    DWORD len2 = 0;
	    HRESULT hr = m_dsb->Lock(m_writePos,CHUNK_SIZE,&buf,&len,&buf2,&len2,0);
	    if (FAILED(hr)) {
		m_writePos = 0;
		if ((hr == DSERR_BUFFERLOST) && SUCCEEDED(m_dsb->Restore())) {
		    m_dsb->Play(0,0,DSBPLAY_LOOPING);
		    m_dsb->GetCurrentPosition(NULL,&m_writePos);
		    Debug(&__plugin,DebugAll,"DirectSound buffer lost and restored, playing at %u",
			m_writePos);
		}
		else {
		    lock();
		    m_buf.clear();
		    unlock();
		}
		continue;
	    }
	    lock();
	    ::memcpy(buf,m_buf.data(),len);
	    if (buf2)
		::memcpy(buf2,((const char*)m_buf.data())+len,len2);
	    m_dsb->Unlock(buf,len,buf2,len2);
	    m_writePos += CHUNK_SIZE;
	    if (m_writePos >= m_buffSize)
		m_writePos -= m_buffSize;
	    m_buf.cut(-CHUNK_SIZE);
	    unlock();
	    XDebug(&__plugin,DebugAll,"Locked %p,%d %p,%d",buf,len,buf2,len2);
	}
    }
}

void DSoundPlay::cleanup()
{
    Debug(DebugInfo,"DSoundPlay cleaning up");
    if (m_dsb) {
	m_dsb->Stop();
	m_dsb->Release();
	m_dsb = 0;
    }
    if (m_ds) {
	m_ds->Release();
	m_ds = 0;
    }
    ::CoUninitialize();
}

void DSoundPlay::put(const DataBlock& data)
{
    if (!m_dsb)
	return;
    lock();
    if (m_buf.length() + data.length() <= MAX_SIZE)
	m_buf += data;
#ifdef DDEBUG
    else
	Debug(&__plugin,DebugMild,"DSoundPlay skipped %u bytes, buffer is full",data.length());
#endif
    unlock();
}

DSoundRec::DSoundRec(DSoundSource* owner, LPGUID device)
    : Thread("DirectSound Rec"),
      m_owner(owner), m_device(device), m_ds(0), m_dsb(0),
      m_buffSize(0), m_readPos(0)
{
}

DSoundRec::~DSoundRec()
{
    if (m_owner)
	m_owner->m_dsound = 0;
}

bool DSoundRec::init()
{
    HRESULT hr;
    if (FAILED(hr = ::CoInitializeEx(NULL,COINIT_MULTITHREADED))) {
	Debug(DebugGoOn,"Could not initialize the COM library, code 0x%X",hr);
	return false;
    }
    if (FAILED(hr = ::CoCreateInstance(CLSID_DirectSoundCapture, NULL, CLSCTX_INPROC_SERVER,
	IID_IDirectSoundCapture, (void**)&m_ds)) || !m_ds) {
	Debug(DebugGoOn,"Could not create the DirectSoundCapture object, code 0x%X",hr);
	return false;
    }
    if (FAILED(hr = m_ds->Initialize(m_device))) {
	Debug(DebugGoOn,"Could not initialize the DirectSoundCapture object, code 0x%X",hr);
	return false;
    }
    WAVEFORMATEX fmt;
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = 8000;
    fmt.nAvgBytesPerSec = 16000;
    fmt.nBlockAlign = 2;
    fmt.wBitsPerSample = 16;
    fmt.cbSize = 0;
    DSCBUFFERDESC bdesc;
    ZeroMemory(&bdesc, sizeof(bdesc));
    bdesc.dwSize = sizeof(bdesc);
    bdesc.dwFlags = DSCBCAPS_WAVEMAPPED;
    bdesc.dwBufferBytes = BUF_SIZE;
    bdesc.lpwfxFormat = &fmt;
    if (FAILED(hr = m_ds->CreateCaptureBuffer(&bdesc, &m_dsb, NULL)) || !m_dsb) {
	Debug(DebugGoOn,"Could not create the DirectSoundCapture buffer, code 0x%X",hr);
	return false;
    }
    if (FAILED(hr = m_dsb->GetFormat(&fmt,sizeof(fmt),0))) {
	Debug(DebugGoOn,"Could not get the DirectSoundCapture buffer format, code 0x%X",hr);
	return false;
    }
    if ((fmt.wFormatTag != WAVE_FORMAT_PCM) ||
	(fmt.nChannels != 1) ||
	(fmt.nSamplesPerSec != 8000) ||
	(fmt.wBitsPerSample != 16)) {
	Debug(DebugGoOn,"DirectSoundCapture does not support 8000Hz 16bit mono PCM format, "
	    "got fmt=%u, chans=%d samp=%d size=%u",
	    fmt.wFormatTag,fmt.nChannels,fmt.nSamplesPerSec,fmt.wBitsPerSample);
	return false;
    }
    DSCBCAPS caps;
    caps.dwSize = sizeof(caps);
    if (FAILED(hr = m_dsb->GetCaps(&caps))) {
	Debug(DebugGoOn,"Could not get the DirectSoundCapture buffer capabilities, code 0x%X",hr);
	return false;
    }
    m_buffSize = caps.dwBufferBytes;
    Debug(&__plugin,DebugInfo,"DirectSoundCapture buffer size %u",m_buffSize);
    if (FAILED(hr = m_dsb->Start(DSCBSTART_LOOPING))) {
	Debug(DebugGoOn,"Could not record to the DirectSoundCapture buffer, code 0x%X",hr);
	return false;
    }
    return true;
}

void DSoundRec::run()
{
    if (!init())
	return;
    if (m_owner)
	m_owner->m_dsound = this;
    Debug(&__plugin,DebugInfo,"DSoundRec is initialized and running");
    while (m_owner) {
	msleep(1,true);
	if (m_dsb) {
	    DWORD pos = 0;
	    if (FAILED(m_dsb->GetCurrentPosition(0,&pos)))
		continue;
	    if (pos < m_readPos)
		pos += m_buffSize;
	    pos -= m_readPos;
	    if (pos < CHUNK_SIZE)
		continue;
	    void* buf = 0;
	    void* buf2 = 0;
	    DWORD len = 0;
	    DWORD len2 = 0;
	    if (FAILED(m_dsb->Lock(m_readPos,CHUNK_SIZE,&buf,&len,&buf2,&len2,0)))
		continue;
	    DataBlock data(0,len+len2);
	    ::memcpy(data.data(),buf,len);
	    if (buf2)
		::memcpy(((char*)data.data())+len,buf2,len2);
	    m_dsb->Unlock(buf,len,buf2,len2);
	    m_readPos += (len+len2);
	    if (m_readPos >= m_buffSize)
		m_readPos -= m_buffSize;
	    if (m_owner)
		m_owner->Forward(data);
	}
    }
}

void DSoundRec::cleanup()
{
    Debug(&__plugin,DebugInfo,"DSoundRec cleaning up");
    if (m_dsb) {
	m_dsb->Stop();
	m_dsb->Release();
	m_dsb = 0;
    }
    if (m_ds) {
	m_ds->Release();
	m_ds = 0;
    }
    ::CoUninitialize();
}

DSoundSource::DSoundSource()
    : m_dsound(0)
{
    DSoundRec* ds = new DSoundRec(this);
    ds->startup();
}

DSoundSource::~DSoundSource()
{
    if (m_dsound)
	m_dsound->terminate();
}

DSoundConsumer::DSoundConsumer()
    : m_dsound(0)
{
    DSoundPlay* ds = new DSoundPlay(this);
    ds->startup();
}

DSoundConsumer::~DSoundConsumer()
{
    if (m_dsound)
	m_dsound->terminate();
}

void DSoundConsumer::Consume(const DataBlock &data, unsigned long timeDelta)
{
    if (m_dsound)
	m_dsound->put(data);
}

DSoundChan::DSoundChan()
    : Channel(__plugin)
{
    Debug(this,DebugAll,"DSoundChan::DSoundChan() [%p]",this);

    setConsumer(new DSoundConsumer);
    getConsumer()->deref();
    Thread::msleep(50);
    setSource(new DSoundSource);
    getSource()->deref();
    Thread::msleep(50);
}

DSoundChan::~DSoundChan()
{
    Debug(this,DebugAll,"DSoundChan::~DSoundChan()  [%p]",this);
}

bool AttachHandler::received(Message &msg)
{
    int more = 2;

    String src(msg.getValue("source"));
    if (src.null())
	more--;
    else if (!src.startSkip("dsound/",false))
	 src = "";

    String cons(msg.getValue("consumer"));
    if (cons.null())
	more--;
    else if (!cons.startSkip("dsound/",false))
	cons = "";

    if (src.null() && cons.null())
	return false;

    DataEndpoint *dd = static_cast<DataEndpoint*>(msg.userObject("DataEndpoint"));
    if (!dd) {
	CallEndpoint *ch = static_cast<CallEndpoint*>(msg.userObject("CallEndpoint"));
	if (ch)
	    dd = ch->setEndpoint();
    }
    if (!dd) {
	Debug(&__plugin,DebugWarn,"DSound attach request with no control or data channel!");
	return false;
    }

    if (cons) {
	DSoundConsumer* c = new DSoundConsumer;
	dd->setConsumer(c);
	c->deref();
	Thread::msleep(50);
    }

    if (src) {
	DSoundSource* s = new DSoundSource;
	dd->setSource(s);
	s->deref();
	Thread::msleep(50);
    }


    // Stop dispatching if we handled all requested
    return !more;
}

bool SoundDriver::msgExecute(Message& msg, String& dest)
{
    CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userData());
    if (ch) {
	DSoundChan *ds = new DSoundChan;
	if (ch->connect(ds)) {
	    msg.setParam("peerid",ds->id());
	    ds->deref();
	}
	else {
	    ds->destruct();
	    return false;
	}
    }
    else {
	Message m("call.route");
	m.addParam("module",name());
	String callto(msg.getValue("direct"));
	if (callto.null()) {
	    const char *targ = msg.getValue("target");
	    if (!targ) {
		Debug(&__plugin,DebugWarn,"DSound outgoing call with no target!");
		return false;
	    }
	    callto = msg.getValue("caller");
	    if (callto.null())
		callto << prefix() << dest;
	    m.addParam("called",targ);
	    m.addParam("caller",callto);
	    if (!Engine::dispatch(m)) {
		Debug(&__plugin,DebugWarn,"DSound outgoing call but no route!");
		return false;
	    }
	    callto = m.retValue();
	    m.retValue().clear();
	}
	m = "call.execute";
	m.addParam("callto",callto);
	DSoundChan *ds = new DSoundChan;
	m.setParam("targetid",ds->id());
	m.userData(ds);
	if (Engine::dispatch(m)) {
	    ds->deref();
	    return true;
	}
	Debug(&__plugin,DebugWarn,"DSound outgoing call not accepted!");
	ds->destruct();
	return false;
    }
    return true;
}

SoundDriver::SoundDriver()
    : Driver("dsound","misc"),
      m_handler(0)
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
    setup(0,true); // no need to install notifications
    Driver::initialize();
    if (!m_handler) {
	m_handler = new AttachHandler;
	Engine::install(m_handler);
    }
}

#endif /* _WINDOWS */

/* vi: set ts=8 sw=4 sts=4 noet: */
