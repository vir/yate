/**
 * dsoundchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * DirectSound channel driver for Windows.
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
namespace { // anonymous

// we should use the primary sound buffer else we will lose sound while we have no input focus
static bool s_primary = true;

// default sampling rate
static int s_rate = 8000;

class DSoundSource : public DataSource
{
    friend class DSoundRec;
public:
    DSoundSource(int rate = 8000);
    ~DSoundSource();
    bool control(NamedList& msg);
private:
    DSoundRec* m_dsound;
};

class DSoundConsumer : public DataConsumer
{
    friend class DSoundPlay;
public:
    DSoundConsumer(int rate = 8000, bool stereo = false);
    ~DSoundConsumer();
    virtual unsigned long Consume(const DataBlock &data, unsigned long tStamp, unsigned long flags);
    bool control(NamedList& msg);
private:
    DSoundPlay* m_dsound;
    bool m_stereo;
};

// all DirectSound play related objects are created in this thread's apartment
class DSoundPlay : public Thread, public Mutex
{
public:
    DSoundPlay(DSoundConsumer* owner, DWORD rate, LPGUID device = 0);
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
    bool control(NamedList& msg);
private:
    DSoundConsumer* m_owner;
    DWORD m_rate;
    LPGUID m_device;
    LPDIRECTSOUND m_ds;
    LPDIRECTSOUNDBUFFER m_dsb;
    DWORD m_buffSize;
    DWORD m_chunk;
    DataBlock m_buf;
    u_int64_t m_start;
    u_int64_t m_total;
};

// all DirectSound record related objects are created in this thread's apartment
class DSoundRec : public Thread
{
public:
    DSoundRec(DSoundSource* owner, DWORD rate, LPGUID device = 0);
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
    bool control(NamedList& msg);
private:
    DSoundSource* m_owner;
    DWORD m_rate;
    LPGUID m_device;
    LPDIRECTSOUNDCAPTURE m_ds;
    LPDIRECTSOUNDCAPTUREBUFFER m_dsb;
    DWORD m_buffSize;
    DWORD m_readPos;
    u_int64_t m_start;
    u_int64_t m_total;
    int m_rshift;
};

class DSoundChan : public Channel
{
public:
    DSoundChan(int rate = 8000);
    virtual ~DSoundChan();
};

class AttachHandler;

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

class AttachHandler : public MessageHandler
{
public:
    AttachHandler()
	: MessageHandler("chan.attach",100,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};


DSoundPlay::DSoundPlay(DSoundConsumer* owner, DWORD rate, LPGUID device)
    : Thread("DirectSound Play",High), Mutex(false,"DSoundPlay"),
      m_owner(0), m_rate(rate), m_device(device), m_ds(0), m_dsb(0),
      m_buffSize(0), m_chunk(320), m_start(0), m_total(0)
{
    if (owner && owner->ref())
	m_owner = owner;
}

DSoundPlay::~DSoundPlay()
{
    if (m_start && m_total) {
	unsigned int rate = (unsigned int)(m_total * 1000000 / (Time::now() - m_start));
	Debug(&__plugin,DebugInfo,"DSoundPlay transferred %u bytes/s, total " FMT64U,rate,m_total);
    }
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
    if (FAILED(hr = m_ds->SetCooperativeLevel(wnd,s_primary ? DSSCL_WRITEPRIMARY : DSSCL_EXCLUSIVE))) {
	Debug(DebugGoOn,"Could not set the DirectSound cooperative level, code 0x%X",hr);
	return false;
    }

    // Set channel number depending data
    WORD nChannels = 1;
    DWORD nAvgBytesPerSec = 2 * m_rate;  // nSamplesPerSec * nBlockAlign.
    WORD nBlockAlign = 2;                // nChannels * wBitsPerSample / 8
    if (m_owner && m_owner->m_stereo) {
	nChannels = 2;
	nAvgBytesPerSec = 4 * m_rate;
	nBlockAlign = 4;
    }
    m_chunk = nChannels * m_rate / 25;   // 20ms of 2-byte samples

    WAVEFORMATEX fmt;
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = nChannels;
    fmt.nSamplesPerSec = m_rate;
    fmt.nAvgBytesPerSec = nAvgBytesPerSec;
    fmt.nBlockAlign = nBlockAlign;
    fmt.wBitsPerSample = 16;
    fmt.cbSize = 0;
    DSBUFFERDESC bdesc;
    ZeroMemory(&bdesc, sizeof(bdesc));
    bdesc.dwSize = sizeof(bdesc);
    bdesc.dwFlags = DSBCAPS_CTRLVOLUME;
    if (s_primary)
	bdesc.dwFlags |= DSBCAPS_PRIMARYBUFFER | DSBCAPS_STICKYFOCUS;
    else {
	bdesc.dwFlags |= DSBCAPS_GLOBALFOCUS;
	// we have to set format when creating secondary buffers
	bdesc.dwBufferBytes = 4*m_chunk;
	bdesc.lpwfxFormat = &fmt;
    }
    if (FAILED(hr = m_ds->CreateSoundBuffer(&bdesc, &m_dsb, NULL)) || !m_dsb) {
	Debug(DebugGoOn,"Could not create the DirectSound buffer, code 0x%X",hr);
	return false;
    }
    // format can be changed only for primary buffers
    if (s_primary && FAILED(hr = m_dsb->SetFormat(&fmt))) {
	Debug(DebugGoOn,"Could not set the DirectSound buffer format, code 0x%X",hr);
	return false;
    }
    if (FAILED(hr = m_dsb->GetFormat(&fmt,sizeof(fmt),0))) {
	Debug(DebugGoOn,"Could not get the DirectSound buffer format, code 0x%X",hr);
	return false;
    }
    if ((fmt.wFormatTag != WAVE_FORMAT_PCM) ||
	(fmt.nChannels != nChannels) ||
	(fmt.nSamplesPerSec != m_rate) ||
	(fmt.wBitsPerSample != 16)) {
	Debug(DebugGoOn,"DirectSound does not support %dHz 16bit %s PCM format, "
	    "got fmt=%u, chans=%d samp=%d size=%u",m_rate,nChannels == 1 ? "mono" : "stereo",
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
    Debug(&__plugin,DebugInfo,"DSoundPlay is initialized and running");
    if (m_owner)
	m_owner->m_dsound = this;
    else
	return;
    DWORD writeOffs = 0;
    DWORD margin = m_chunk/4;
    bool first = true;
    while (m_owner->refcount() > 1) {
	msleep(1,true);
	if (first) {
	    if ((m_buf.length() < 2*m_chunk) || !m_dsb)
		continue;
	    first = false;
	    m_dsb->GetCurrentPosition(NULL,&writeOffs);
	    writeOffs = (margin + writeOffs) % m_buffSize;
	    Debug(&__plugin,DebugAll,"DSoundPlay has %u in buffer and starts playing at %u",
		m_buf.length(),writeOffs);
	    m_start = Time::now();
	}
	while (m_dsb) {
	    DWORD playPos = 0;
	    DWORD writePos = 0;
	    bool adjust = false;
	    // check if we slipped behind and advance our pointer if so
	    if (SUCCEEDED(m_dsb->GetCurrentPosition(&playPos,&writePos))) {
		if (playPos < writePos)
		    // not wrapped - have to adjust if our pointer falls between play and write
		    adjust = (playPos < writeOffs) && (writeOffs < writePos);
		else
		    // only write offset has wrapped - adjust if we are outside
		    adjust = (writeOffs < writePos) || (playPos <= writeOffs) ;
	    }
	    if (adjust) {
		DWORD adjOffs = (margin + writePos) % m_buffSize;
		Debug(&__plugin,DebugNote,"Slip detected, changing write offs from %u to %u, p=%u w=%u",
		    writeOffs,adjOffs,playPos,writePos);
		writeOffs = adjOffs;
	    }
	    bool hasData = (m_buf.length() >= m_chunk);
	    if (!(adjust || hasData)) {
		// don't fill the buffer if we still have at least one chunk until underflow
		if ((m_buffSize + writeOffs - writePos) % m_buffSize >= m_chunk)
		    break;
	    }
	    void* buf = 0;
	    void* buf2 = 0;
	    DWORD len = 0;
	    DWORD len2 = 0;
	    // locking will prevent us to skip ahead and overwrite the play position
	    HRESULT hr = m_dsb->Lock(writeOffs,m_chunk,&buf,&len,&buf2,&len2,0);
	    if (FAILED(hr)) {
		writeOffs = 0;
		if ((hr == DSERR_BUFFERLOST) && SUCCEEDED(m_dsb->Restore())) {
		    m_dsb->Play(0,0,DSBPLAY_LOOPING);
		    m_dsb->GetCurrentPosition(NULL,&writeOffs);
		    writeOffs = (margin + writeOffs) % m_buffSize;
		    Debug(&__plugin,DebugAll,"DirectSound buffer lost and restored, playing at %u",
			writeOffs);
		}
		else {
		    lock();
		    m_buf.clear();
		    unlock();
		}
		continue;
	    }
	    lock();
	    if (hasData) {
		::memcpy(buf,m_buf.data(),len);
		if (buf2)
		    ::memcpy(buf2,((const char*)m_buf.data())+len,len2);
	    }
	    else {
		::memset(buf,0,len);
		if (buf2)
		    ::memset(buf2,0,len2);
	    }
	    m_dsb->Unlock(buf,len,buf2,len2);
	    m_total += m_chunk;
	    m_buf.cut(-(int)m_chunk);
	    unlock();
#ifdef DEBUG
	    if (!hasData)
		Debug(&__plugin,DebugInfo,"Underflow, filled %u bytes at %u, p=%u w=%u",
		    m_chunk,writeOffs,playPos,writePos);
#endif
	    writeOffs += m_chunk;
	    if (writeOffs >= m_buffSize)
		writeOffs -= m_buffSize;
	    XDebug(&__plugin,DebugAll,"Locked %p,%d %p,%d",buf,len,buf2,len2);
	}
    }
}

bool DSoundPlay::control(NamedList& msg)
{
    bool ok = false;
    LONG val = 0;
    int outValue = msg.getIntValue("out_volume",-1);
    HRESULT hr; // we need it for debugging
    if ((outValue >= 0) && (outValue <= 100)) {
	// convert 0...100 to 0...-50.00 dB
	val = (outValue - 100) * 50;
	ok = ((hr = m_dsb->SetVolume(val)) == DS_OK);
    }

    if ((hr = m_dsb->GetVolume(&val)) == DS_OK) {
	// convert back 0...-50.0 dB to 0...100, watch out for values up to -100.00 dB
	outValue = (5000 + val) / 50;
	if (outValue < 0)
	    outValue = 0;
	msg.setParam("out_volume", String(outValue));
    }
    return TelEngine::controlReturn(&msg,ok);
}

void DSoundPlay::cleanup()
{
    Debug(DebugInfo,"DSoundPlay cleaning up");
    if (m_owner) {
	m_owner->m_dsound = 0;
	if (m_owner->refcount() > 1)
	    Debug(&__plugin,DebugWarn,"DSoundPlay destroyed while consumer is still active");
	TelEngine::destruct(m_owner);
    }
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
    if (m_buf.length() + data.length() <= m_buffSize + m_chunk)
	m_buf += data;
    else
	Debug(&__plugin,DebugMild,"DSoundPlay skipped %u bytes, buffer is full",data.length());
    unlock();
}


DSoundRec::DSoundRec(DSoundSource* owner, DWORD rate, LPGUID device)
    : Thread("DirectSound Rec",High),
      m_owner(0), m_rate(rate), m_device(device), m_ds(0), m_dsb(0),
      m_buffSize(0), m_readPos(0), m_start(0), m_total(0), m_rshift(0)
{
    if (owner && owner->ref())
	m_owner = owner;
}

DSoundRec::~DSoundRec()
{
    if (m_start && m_total) {
	unsigned int rate = (unsigned int)(m_total * 1000000 / (Time::now() - m_start));
	Debug(&__plugin,DebugInfo,"DSoundRec transferred %u bytes/s, total " FMT64U,rate,m_total);
    }
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
    fmt.nSamplesPerSec = m_rate;
    fmt.nAvgBytesPerSec = 2 * m_rate;
    fmt.nBlockAlign = 2;
    fmt.wBitsPerSample = 16;
    fmt.cbSize = 0;
    DSCBUFFERDESC bdesc;
    ZeroMemory(&bdesc, sizeof(bdesc));
    bdesc.dwSize = sizeof(bdesc);
    bdesc.dwFlags = DSCBCAPS_WAVEMAPPED;
    bdesc.dwBufferBytes = 4 * m_rate / 25;
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
	(fmt.nSamplesPerSec != m_rate) ||
	(fmt.wBitsPerSample != 16)) {
	Debug(DebugGoOn,"DirectSoundCapture does not support %dHz 16bit mono PCM format, "
	    "got fmt=%u, chans=%d samp=%d size=%u",m_rate,
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
    Debug(&__plugin,DebugInfo,"DSoundRec is initialized and running");
    DWORD chunk = m_rate / 25; // 20ms of 2-byte samples
    m_start = Time::now();
    if (m_owner)
	m_owner->m_dsound = this;
    else
	return;
    while (m_owner->refcount() > 1) {
	msleep(1,true);
	if (m_dsb) {
	    DWORD pos = 0;
	    if (FAILED(m_dsb->GetCurrentPosition(0,&pos)))
		continue;
	    if (pos < m_readPos)
		pos += m_buffSize;
	    pos -= m_readPos;
	    if (pos < chunk)
		continue;
	    void* buf = 0;
	    void* buf2 = 0;
	    DWORD len = 0;
	    DWORD len2 = 0;
	    if (FAILED(m_dsb->Lock(m_readPos,chunk,&buf,&len,&buf2,&len2,0)))
		continue;
	    DataBlock data(0,len+len2);
	    ::memcpy(data.data(),buf,len);
	    if (buf2)
		::memcpy(((char*)data.data())+len,buf2,len2);
	    m_dsb->Unlock(buf,len,buf2,len2);
	    m_total += (len+len2);
	    m_readPos += (len+len2);
	    if (m_readPos >= m_buffSize)
		m_readPos -= m_buffSize;
	    if (m_rshift) {
		// apply volume attenuation
		signed short* s = (signed short*)data.data();
		for (unsigned int n = data.length() / 2; n--; s++)
		    *s >>= m_rshift;
	    }
	    if (m_owner)
		m_owner->Forward(data);
	}
    }
}

void DSoundRec::cleanup()
{
    Debug(&__plugin,DebugInfo,"DSoundRec cleaning up");
    if (m_owner) {
	m_owner->m_dsound = 0;
	if (m_owner->refcount() > 1)
	    Debug(&__plugin,DebugWarn,"DSoundRec destroyed while source is still active");
	TelEngine::destruct(m_owner);
    }
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

bool DSoundRec::control(TelEngine::NamedList &msg)
{
    bool ok = false;
    int inValue = msg.getIntValue("in_volume",-1);
    if ((inValue >= 0) && (inValue <= 100)) {
	// convert 0...100 to a 10...0 right shift count
	m_rshift = (105 - inValue) / 10;
	ok = true;
    }

    inValue = (10 - m_rshift) * 10;
    msg.setParam("in_volume", String(inValue));
    return TelEngine::controlReturn(&msg,ok);
}


DSoundSource::DSoundSource(int rate)
    : m_dsound(0)
{
    if (rate != 8000)
	m_format << "/" << rate;
    DSoundRec* ds = new DSoundRec(this,rate);
    ds->startup();
}

DSoundSource::~DSoundSource()
{
    if (m_dsound)
	m_dsound->terminate();
}

bool DSoundSource::control(NamedList& msg)
{
    if (m_dsound)
	return m_dsound->control(msg);
    return TelEngine::controlReturn(&msg,false);
}


DSoundConsumer::DSoundConsumer(int rate, bool stereo)
    : DataConsumer(stereo ? "2*slin" : "slin"),
    m_dsound(0), m_stereo(stereo)
{
    if (rate != 8000)
	m_format << "/" << rate;
    DSoundPlay* ds = new DSoundPlay(this,rate);
    ds->startup();
}

DSoundConsumer::~DSoundConsumer()
{
    if (m_dsound)
	m_dsound->terminate();
}

unsigned long DSoundConsumer::Consume(const DataBlock &data, unsigned long tStamp, unsigned long flags)
{
    if (m_dsound) {
	m_dsound->put(data);
	return invalidStamp();
    }
    return 0;
}

bool DSoundConsumer::control(NamedList& msg)
{
    if (m_dsound)
	return m_dsound->control(msg);
    return false;
}


DSoundChan::DSoundChan(int rate)
    : Channel(__plugin)
{
    Debug(this,DebugAll,"DSoundChan::DSoundChan(%d) [%p]",rate,this);

    setConsumer(new DSoundConsumer(rate));
    getConsumer()->deref();
    Thread::msleep(50);
    setSource(new DSoundSource(rate));
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

    DataEndpoint *dd = static_cast<DataEndpoint*>(msg.userObject(YATOM("DataEndpoint")));
    if (!dd) {
	CallEndpoint *ch = static_cast<CallEndpoint*>(msg.userObject(YATOM("CallEndpoint")));
	if (ch)
	    dd = ch->setEndpoint();
    }
    if (!dd) {
	Debug(&__plugin,DebugWarn,"DSound attach request with no control or data channel!");
	return false;
    }

    int rate = msg.getIntValue("rate",s_rate);

    if (cons) {
	DSoundConsumer* c = new DSoundConsumer(rate,msg.getBoolValue("stereo"));
	dd->setConsumer(c);
	c->deref();
	Thread::msleep(50);
    }

    if (src) {
	DSoundSource* s = new DSoundSource(rate);
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
	DSoundChan *ds = new DSoundChan(msg.getIntValue("rate",s_rate));
	if (ch->connect(ds,msg.getValue("reason"))) {
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
	DSoundChan *ds = new DSoundChan(msg.getIntValue("rate",8000));
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
    Configuration cfg(Engine::configFile("dsoundchan"));
    s_rate = cfg.getIntValue("general","rate",8000);
    // prefer primary buffer as we try to retain control of audio board
    s_primary = cfg.getBoolValue("general","primary",true);
    if (!m_handler) {
	m_handler = new AttachHandler;
	Engine::install(m_handler);
    }
}

}; // anonymous namespace

#endif /* _WINDOWS */

/* vi: set ts=8 sw=4 sts=4 noet: */
