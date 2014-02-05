/**
 * coreaudio.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * CoreAudio sound channel driver for Mac OS X.
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


#include <yatephone.h>

#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>

#define FRAME_SIZE 320
#define DEFAULT_SAMPLE_RATE 8000

using namespace TelEngine;

namespace { //anonymous

class CoreAudioSource : public ThreadedSource
{
public:
    CoreAudioSource(unsigned int rate = DEFAULT_SAMPLE_RATE);
    ~CoreAudioSource();
    // inherited methods
    bool init();
    virtual void run();
    virtual void cleanup();
    virtual bool control(NamedList& params);

    // append to the internal buffer data read from input source
    void sendData(AudioBufferList *buf);
    // provide data to the AudioConverter taken from the internal buffer
    DataBlock getData(UInt32 pkts);

    // helper function for allocating buffers
    AudioBufferList* allocateAudioBufferList(UInt32 numChannels, UInt32 size);
    // helper function for freeing buffers
    void destroyAudioBufferList(AudioBufferList* list);
    // helper function for obtaining an AudioConverter
    OSStatus buildConverter(AudioStreamBasicDescription inFormat, AudioConverterRef* ac);

    // obtain the output format of the AudioUnit
    inline AudioStreamBasicDescription outFormat() const
	{ return m_outDevFormat; }
    // obtain the output sample rate
    inline unsigned int rate() const
	{ return m_rate; }

private:
    // callback for obtaining data from input source
    static OSStatus inputCallback(void* inRefCon, AudioUnitRenderActionFlags* ioActionFlags, const AudioTimeStamp* inTimeStamp,
				  UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList* ioData);
    // default input AudioUnit
    AudioUnit fAudioUnit;
    // input buffer
    AudioBufferList* m_inAudioBuffer;
    // sample rate converter
    AudioConverterRef m_audioConvert;
    // input device id
    AudioDeviceID fInputDevID;
    // output format of the AudioUnit
    AudioStreamBasicDescription	m_outDevFormat;
    // the desired format for yate, passed to the sample rate converter
    AudioStreamBasicDescription	m_convertToFormat;
    // total amount of bytes sent
    unsigned int m_total;
    // check if the AudioDevice supports setting the volume
    bool m_volSettable;
    // number of channels for the device
    unsigned int m_channels;
    // internal buffer
    DataBlock m_data;
    // output sample rate
    unsigned int m_rate;
};

class CoreAudioConsumer : public DataConsumer, public Mutex
{
public:
    CoreAudioConsumer(unsigned int rate = DEFAULT_SAMPLE_RATE);
    ~CoreAudioConsumer();
    bool init();
    // inherited methods
    virtual unsigned long Consume(const DataBlock &data, unsigned long tStamp, unsigned long flags);
    virtual bool control(NamedList& params);
    virtual void getData(AudioBufferList* buf);
    // obtain the input sample rate
    inline unsigned int rate() const
	{ return m_rate; }

private:
    // callback through which the AudioUnit requires data to play
    static OSStatus outputCallback(void* inRefCon, AudioUnitRenderActionFlags* ioActionFlags, const AudioTimeStamp* inTimeStamp,
				  UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList* ioData);
    // the AudioUnit
    AudioUnit fAudioUnit;
    // total amount of data written to the output
    unsigned int m_total;
    // check if the AudioDevice supports setting the volume
    bool m_volSettable;
    // number of channels for the device
    unsigned int m_channels;
    // AudioDevice ID
    AudioDeviceID fOutputDevID;
    // internal buffer
    DataBlock m_data;
    // input sample rate
    unsigned int m_rate;
};

class CoreAudioChan : public CallEndpoint
{
public:
    CoreAudioChan(const String& dev, unsigned int rate = DEFAULT_SAMPLE_RATE);
    ~CoreAudioChan();
    bool init();
    virtual void disconnected(bool final, const char *reason);
    void answer();
    inline void setTarget(const char* target = 0)
	{ m_target = target; }
    inline const String& getTarget() const
	{ return m_target; }
    inline unsigned int rate() const
	{ return m_rate; }
private:
    String m_dev;
    String m_target;
    unsigned int m_rate;
};

class CoreAudioHandler;

class CoreAudioPlugin : public Plugin
{
public:
    CoreAudioPlugin();
    virtual void initialize();
    virtual bool isBusy() const;
private:
    CoreAudioHandler* m_handler;
};

static CoreAudioChan* s_audioChan = 0;

INIT_PLUGIN(CoreAudioPlugin);

class CoreAudioHandler : public MessageHandler
{
public:
    CoreAudioHandler(const char *name)
	: MessageHandler(name,100,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class StatusHandler : public MessageHandler
{
public:
    StatusHandler()
	: MessageHandler("engine.status",100,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class DropHandler : public MessageHandler
{
public:
    DropHandler()
	: MessageHandler("call.drop",100,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class MasqHandler : public MessageHandler
{
public:
    MasqHandler(int prio)
	: MessageHandler("chan.masquerade",prio,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

class AttachHandler : public MessageHandler
{
public:
    AttachHandler()
	: MessageHandler("chan.attach",100,__plugin.name())
	{ }
    virtual bool received(Message &msg);
};

// test if a device permits setting the volume
static bool checkVolumeSettable(AudioDeviceID devId, UInt32 inChannel,Boolean isInput)
{
    Boolean isWritable = false;

    AudioObjectPropertyScope volumeScope = isInput ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput;
    AudioObjectPropertyAddress volumeAddress = {kAudioDevicePropertyVolumeScalar,volumeScope,inChannel};

    Boolean hasProperty = AudioObjectHasProperty(devId,&volumeAddress);
    if (!hasProperty) {
        DDebug(DebugAll, "CoreAudio - %s AudioUnit does not have 'kAudioDevicePropertyVolumeScalar' property on channel %u",
               (isInput ? "Input" : "Output"),(unsigned int)inChannel);
        return false;
    }

    OSStatus err = AudioObjectIsPropertySettable(devId,&volumeAddress,&isWritable);
    if (err != noErr) {
	DDebug(DebugAll, "CoreAudio - %s AudioUnit Failed to get if volume property is settable on channel=%u, err=%4.4s, %ld",(isInput ? "Input" : "Output"),
               (unsigned int)inChannel,(char*)&err,(long int)err);
	return false;
    }
    return isWritable;
}

// callback for the sample rate converter
OSStatus convertCallback(AudioConverterRef inAudioConverter, UInt32* ioNumberDataPackets, AudioBufferList* ioData,
			 AudioStreamPacketDescription**	outDataPacketDescription, void* inUserData)
{
    CoreAudioSource* src = static_cast<CoreAudioSource*> (inUserData);
    if (!src)
	return 1;
    // try to get data with the required length
    DataBlock data = src->getData(*ioNumberDataPackets);
    if (data.length() > 0)
	XDebug(DebugInfo,"CoreAudio::convertCallBack() packetsReq=%d pktsAvailable=%d", (int)*ioNumberDataPackets,data.length()/2);

    // signal that we have no data to convert and return
    if (data.length() == 0) {
	*ioNumberDataPackets = 0;
	return 1;
    }
    // determine how much we can read into the converter's input buffer
    UInt32 maxPackets = data.length() / src->outFormat().mBytesPerFrame;
    if (*ioNumberDataPackets > maxPackets)
	*ioNumberDataPackets = maxPackets;
    else
	maxPackets = *ioNumberDataPackets;

    // fill the converters input buffer
    ioData->mBuffers[0].mData = data.data();
    ioData->mBuffers[0].mDataByteSize = maxPackets * src->outFormat().mBytesPerFrame;
    ioData->mBuffers[0].mNumberChannels = 1;
    data.cut(-(maxPackets * src->outFormat().mBytesPerFrame));
    return noErr;
}

CoreAudioSource::CoreAudioSource(unsigned int rate)
    : m_inAudioBuffer(0), m_audioConvert(NULL), fInputDevID(0),
      m_total(0), m_volSettable(false), m_channels(0), m_rate(rate)
{
    Debug(DebugAll,"CoreAudioSource::CoreAudioSource() [%p]",this);
    if (m_rate != DEFAULT_SAMPLE_RATE)
	m_format << "/" << m_rate;
}

CoreAudioSource::~CoreAudioSource()
{
    Debug(DebugAll,"CoreAudioSource::~CoreAudioSource() [%p] total=%u",this,m_total);
    OSStatus err = AudioOutputUnitStop(fAudioUnit);
    if(err != noErr)
	Debug(DebugInfo,"CoreAudioSource::~CoreAudioSource() [%p] - Failed to stop AU",this);
    err = AudioUnitUninitialize(fAudioUnit);
    if(err != noErr)
	Debug(DebugInfo,"CoreAudioSource::~CoreAudioSource() [%p] - Failed to uninitialize AU",this);
    destroyAudioBufferList(m_inAudioBuffer);
}

bool CoreAudioSource::init()
{
    OSStatus err = noErr;
    UInt32  param;

    // open the AudioOutputUnit, provide description
#ifdef MAC_OS_X_VERSION_10_6
    AudioComponent component;
    AudioComponentDescription description;
#else
    Component component;
    ComponentDescription description;
#endif

    description.componentType = kAudioUnitType_Output;
    description.componentSubType = kAudioUnitSubType_HALOutput;
    description.componentManufacturer = kAudioUnitManufacturer_Apple;
    description.componentFlags = 0;
    description.componentFlagsMask = 0;

#ifdef MAC_OS_X_VERSION_10_6
    if((component = AudioComponentFindNext(NULL,&description)))
	err = AudioComponentInstanceNew(component,&fAudioUnit);
#else
    if((component = FindNextComponent(NULL,&description)))
	err = OpenAComponent(component,&fAudioUnit);
#endif

    if(err != noErr) {
        fAudioUnit = NULL;
        Debug(DebugInfo,"CoreAudioSource::init() [%p] - failed to open component error==%4.4s, %ld",this,(char*)&err,(long int)err);
        return false;
    }

    // configure AudioOutputUnit for input, enable input on the AUHAL
    param = 1;
    err = AudioUnitSetProperty(fAudioUnit,kAudioOutputUnitProperty_EnableIO,kAudioUnitScope_Input,1,&param,sizeof(UInt32));
    if (err == noErr) {
	// disable output on the AUHAL
	param = 0;
	err = AudioUnitSetProperty(fAudioUnit,kAudioOutputUnitProperty_EnableIO,kAudioUnitScope_Output,0,&param,sizeof(UInt32));
    }
    else {
	Debug(DebugInfo,"CoreAudioSource::init() [%p] - failed to configure AudioUnit for input error==%4.4s, %ld",this,(char*)&err,(long int)err);
	return false;
    }

    // select the default input device
    param = sizeof(AudioDeviceID);

    AudioObjectPropertyAddress devAddress = {kAudioHardwarePropertyDefaultInputDevice,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMaster};
    err = AudioObjectGetPropertyData(kAudioObjectSystemObject,&devAddress,0,NULL,&param,&fInputDevID);
    if(err != noErr) {
	Debug(DebugInfo,"CoreAudioSource::init() [%p] - failed to get input device error==%4.4s, %ld",this,(char*)&err,(long int)err);
	return false;
    }

    // set the current device to the AudioUnit
    err = AudioUnitSetProperty(fAudioUnit,kAudioOutputUnitProperty_CurrentDevice,kAudioUnitScope_Global,0,&fInputDevID,sizeof(AudioDeviceID));
    if (err != noErr) {
	Debug(DebugInfo,"CoreAudioSource::init() [%p] - failed to set AU input device=%4.4s, %ld",this,(char*)&err,(long int)err);
	return false;
    }

    // setup render callback
    AURenderCallbackStruct callback;
    callback.inputProc = CoreAudioSource::inputCallback;
    callback.inputProcRefCon = this;
    err = AudioUnitSetProperty(fAudioUnit,kAudioOutputUnitProperty_SetInputCallback,kAudioUnitScope_Global,0,&callback,sizeof(AURenderCallbackStruct));
    if (err != noErr) {
	Debug(DebugInfo,"CoreAudioSource::init() [%p] - could not set callback error==%4.4s, %ld",this,(char*)&err,(long int)err);
	return false;
    }

    // get hardware device format
    param = sizeof(AudioStreamBasicDescription);
    AudioStreamBasicDescription devFormat;
    err = AudioUnitGetProperty(fAudioUnit,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,1,&devFormat,&param);
    if(err != noErr) {
	Debug(DebugInfo,"CoreAudioSource::init() [%p] - failed to get input device AudioStreamBasicDescription error==%4.4s, %ld",this,(char*)&err,(long int)err);
	return false;
    }

    DDebug(DebugInfo,"CoreAudioSource::init() [%p] - hardware device input format is : channels/frame=%u, sampleRate=%f, bits/channel=%u, "
	   "bytes/frame=%u, frames/packet=%u, bytes/packet=%u, formatFlags=0x%x",
	   this,(unsigned int)devFormat.mChannelsPerFrame,devFormat.mSampleRate,(unsigned int)devFormat.mBitsPerChannel,
	   (unsigned int)devFormat.mBytesPerFrame,(unsigned int)devFormat.mFramesPerPacket,(unsigned int)devFormat.mBytesPerPacket,
	   (unsigned int)devFormat.mFormatFlags);

    m_outDevFormat.mChannelsPerFrame = 1;
    m_outDevFormat.mSampleRate = devFormat.mSampleRate;
    m_outDevFormat.mFormatID = kAudioFormatLinearPCM;
    m_outDevFormat.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    m_outDevFormat.mFormatFlags &= ~kAudioFormatFlagIsBigEndian;
#if __BIG_ENDIAN__
    m_outDevFormat.mFormatFlags |= kAudioFormatFlagIsBigEndian;
#endif
    m_outDevFormat.mBytesPerFrame = sizeof(int16_t) * devFormat.mChannelsPerFrame;
    m_outDevFormat.mBitsPerChannel = m_outDevFormat.mBytesPerFrame * 8;
    m_outDevFormat.mFramesPerPacket = 1;
    m_outDevFormat.mBytesPerPacket = m_outDevFormat.mBytesPerFrame;

    // remembering the number of channels for the device
    m_channels = devFormat.mChannelsPerFrame;

    // set the AudioUnit output data format
    err = AudioUnitSetProperty(fAudioUnit,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Output,1,&m_outDevFormat,sizeof(AudioStreamBasicDescription));
    if(err != noErr) {
	Debug(DebugInfo, "CoreAudioSource::init() [%p] - failed to set output data format error==%4.4s, %ld",this,(char*)&err,(long int)err);
	return false;
    }

    DDebug(DebugInfo,"CoreAudioSource::init() [%p] - AudioUnit output format is : channels/frame=%u, sampleRate=%f, bits/channel=%u, "
	   "bytes/frame=%u, frames/packet=%u, bytes/packet=%u, formatFlags=0x%x",
	   this,(unsigned int)m_outDevFormat.mChannelsPerFrame,m_outDevFormat.mSampleRate,(unsigned int)m_outDevFormat.mBitsPerChannel,
	   (unsigned int)m_outDevFormat.mBytesPerFrame,(unsigned int)m_outDevFormat.mFramesPerPacket,(unsigned int)m_outDevFormat.mBytesPerPacket,
	   (unsigned int)m_outDevFormat.mFormatFlags);

    // obtain a sample rate converter
    err = buildConverter(m_outDevFormat,&m_audioConvert);
    if (err != noErr) {
	Debug(DebugInfo,"CoreAudioSource::init() [%p] - failed to get sample rate converter error==%4.4s, %ld",this,(char*)&err,(long int)err);
	return false;
    }

    // get the number of frames in the IO buffer
    UInt32 audioSamples;
    param = sizeof(UInt32);
    err = AudioUnitGetProperty(fAudioUnit,kAudioDevicePropertyBufferFrameSize,kAudioUnitScope_Global,0,&audioSamples,&param);
    if(err != noErr) {
	Debug(DebugInfo,"CoreAudioSource::init() [%p] - failed to get audio sample size error==%4.4s, %ld",this,(char*)&err,(long int)err);
	return false;
    }

    // Initialize the AU
    err = AudioUnitInitialize(fAudioUnit);
    if(err != noErr) {
	Debug(DebugInfo,"CoreAudioSource::init() [%p] - Failed to initialize AU error==%4.4s, %ld",this,(char*)&err,(long int)err);
	return false;
    }

    // allocate AudioBufferList
    m_inAudioBuffer = allocateAudioBufferList(m_outDevFormat.mChannelsPerFrame,audioSamples *  m_outDevFormat.mBytesPerFrame);
    if(m_inAudioBuffer == NULL) {
	Debug(DebugInfo,"CoreAudioSource::init() [%p] - Failed to allocate audio buffers",this);
	return false;
    }

    // Start pulling for audio data
    err = AudioOutputUnitStart(fAudioUnit);
    if(err != noErr) {
	Debug(DebugInfo,"CoreAudioSource::init() [%p] - Failed to start the AudioUnit error==%4.4s, %ld",this,(char*)&err,(long int)err);
	return false;
    }
    else
	Debug(DebugInfo,"CoreAudioSource::init() [%p] - AudioUnit started",this);

    // check if the device lets us set the volume
    m_volSettable = false;
    for (unsigned int i = 0; i <= m_channels; i++)
        m_volSettable = checkVolumeSettable(fInputDevID,i,true) || m_volSettable;
    Debug(DebugAll,"CoreAudioSource::init() [%p] - volume %s settable",this,(m_volSettable ? "is" : "isn't"));

    return start("CoreAudioSource");
}

OSStatus CoreAudioSource::buildConverter(AudioStreamBasicDescription inputFormat, AudioConverterRef* ac)
{
    m_convertToFormat.mChannelsPerFrame = 1;
    m_convertToFormat.mSampleRate = rate();
    m_convertToFormat.mFormatID = kAudioFormatLinearPCM;
    m_convertToFormat.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    m_convertToFormat.mFormatFlags &= ~kAudioFormatFlagIsBigEndian;
#if __BIG_ENDIAN__
    m_convertToFormat.mFormatFlags |= kAudioFormatFlagIsBigEndian;
#endif
    m_convertToFormat.mBitsPerChannel = sizeof(int16_t) * 8;
    m_convertToFormat.mBytesPerFrame = m_convertToFormat.mBitsPerChannel / 8;
    m_convertToFormat.mFramesPerPacket = 1;
    m_convertToFormat.mBytesPerPacket = m_convertToFormat.mBytesPerFrame;

    DDebug(DebugInfo,"CoreAudioSource::buildConverter() [%p] - AudioConverter output format is : channels/frame=%u, sampleRate=%f, bits/channel=%u, "
	   "bytes/frame=%u, frames/packet=%u, bytes/packet=%u, formatFlags=0x%x",
           this,(unsigned int)m_convertToFormat.mChannelsPerFrame,m_convertToFormat.mSampleRate,(unsigned int)m_convertToFormat.mBitsPerChannel,
	   (unsigned int)m_convertToFormat.mBytesPerFrame,(unsigned int)m_convertToFormat.mFramesPerPacket,(unsigned int)m_convertToFormat.mBytesPerPacket,
	   (unsigned int)m_convertToFormat.mFormatFlags);

    OSStatus err = noErr;
    err = AudioConverterNew(&inputFormat,&m_convertToFormat,ac);
    if (err != noErr) {
	Debug(DebugInfo,"CoreAudioSource::buildConverter() [%p] failed to get converter error==%4.4s, %ld",this,(char*)&err,(long int)err);
	return err;
    }

    // set channel map
    SInt32 channelMap[] = { 0 };
    err = AudioConverterSetProperty(*ac, kAudioConverterChannelMap, sizeof(SInt32), channelMap);
    // set converter complexity
    UInt32 size = sizeof(kAudioConverterSampleRateConverterComplexity_Mastering);
    UInt32 prop = kAudioConverterSampleRateConverterComplexity_Mastering;
    err = AudioConverterSetProperty(*ac,kAudioConverterSampleRateConverterComplexity,size,&prop);
    if (err != noErr)
	Debug(DebugInfo,"CoreAudioSource::buildConverter() [%p] failed to set converter complexity error==%4.4s, %ld",this,(char*)&err,(long int)err);
    return noErr;
}

AudioBufferList* CoreAudioSource::allocateAudioBufferList(UInt32 numChannels, UInt32 size)
{
    AudioBufferList* list;
    DDebug(DebugAll,"CoreAudioSource::allocateAudioBufferList(channels= %d,size=%d) [%p]",(int)numChannels,(int)size,this);
    list = (AudioBufferList*)calloc(1, sizeof(AudioBufferList) + numChannels * sizeof(AudioBuffer));
    if(list == NULL)
	return NULL;

    list->mNumberBuffers = numChannels;
    for(UInt32 i = 0; i < numChannels; ++i) {
	list->mBuffers[i].mNumberChannels = 1;
	list->mBuffers[i].mDataByteSize = size;
	list->mBuffers[i].mData = malloc(size);
	if(list->mBuffers[i].mData == NULL) {
	    destroyAudioBufferList(list);
	    return NULL;
	}
    }
    return list;
}

void CoreAudioSource::destroyAudioBufferList(AudioBufferList* list)
{
    DDebug(DebugAll,"CoreAudioSource::destroyAudioBufferList(list=%p) [%p]",list,this);
    if(list) {
	for(UInt32 i = 0; i < list->mNumberBuffers; i++) {
	    if(list->mBuffers[i].mData)
		free(list->mBuffers[i].mData);
	}
	free(list);
    }
}

void CoreAudioSource::sendData(AudioBufferList* buf)
{
    // append to internal buffer data we receive from input
    if (!buf)
	return;
    lock();
    for (unsigned int i = 0; i < m_outDevFormat.mChannelsPerFrame; i++)
	m_data.append(buf->mBuffers[i].mData,buf->mBuffers[i].mDataByteSize);
    XDebug(DebugAll,"CoreAudioSource::sendData(buffer=%p,buffer_length=%d), internal buffer length=%d [%p]",buf,(int)buf->mBuffers[0].mDataByteSize,m_data.length(),this);
    unlock();
}

DataBlock CoreAudioSource::getData(UInt32 pkts)
{
    // return to the converter a data block with the required size or the maximum available
    DataBlock data;
    lock();
    if (pkts * m_outDevFormat.mBytesPerFrame > m_data.length())
	pkts = m_data.length() / m_outDevFormat.mBytesPerFrame;
    data.assign(m_data.data(),pkts * m_outDevFormat.mBytesPerFrame);
    m_data.cut(-pkts * m_outDevFormat.mBytesPerFrame );
    unlock();
    return data;
}

OSStatus CoreAudioSource::inputCallback(void* inRefCon, AudioUnitRenderActionFlags* ioActionFlags, const AudioTimeStamp* inTimeStamp,
							UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList* ioData)
{
    CoreAudioSource* source = (CoreAudioSource*) inRefCon;
    OSStatus err = noErr;
    // Render into audio buffer
    err = AudioUnitRender(source->fAudioUnit,ioActionFlags,inTimeStamp,inBusNumber,inNumberFrames,source->m_inAudioBuffer);
    if(err)
	Debug(DebugInfo,"CoreAudioSource::inputCallback() [%p] AudioUnitRender() failed with error=%4.4s, %ld",source,(char*)&err,(long int)err);

    source->sendData(source->m_inAudioBuffer);
    return err;
}

void CoreAudioSource::run()
{
    DataBlock frame;
    AudioBufferList fillBufList;
    fillBufList.mNumberBuffers = 1;
    fillBufList.mBuffers[0].mNumberChannels = 1;
    fillBufList.mBuffers[0].mData = new char[FRAME_SIZE];

    do {
	if (!looping())
	    break;
	if (frame.length() < FRAME_SIZE) {
	    // try to get more data from input and convert it to the desired sample rate
	    UInt32 outBuffSize = FRAME_SIZE / m_convertToFormat.mBytesPerPacket;
	    //AudioStreamPacketDescription* pktDesc = NULL;
	    fillBufList.mBuffers[0].mDataByteSize = FRAME_SIZE;
	    OSStatus err = AudioConverterFillComplexBuffer(m_audioConvert,convertCallback,this,&outBuffSize,&fillBufList,NULL/*pktDesc*/);
	    if (err != noErr && err != 1)
		Debug(DebugInfo,"CoreAudioSource::run() - AudioConvertFillComplexBuffer() failed with error=%4.4s, %ld", (char*)&err, (long int)err);
	    if (outBuffSize == 0) {
		Thread::idle();
		continue;
	    }
	    frame.append(fillBufList.mBuffers[0].mData,outBuffSize * m_convertToFormat.mBytesPerPacket);
	}

	if (frame.length() >= FRAME_SIZE) {
	    // we have enough data to send forward
	    DataBlock data(frame.data(),FRAME_SIZE,false);
	    Forward(data);
	    data.clear(false);
	    frame.cut(-FRAME_SIZE);
	    m_total += FRAME_SIZE;
	}
    }
    while (true);
    delete [] (char*)fillBufList.mBuffers[0].mData;
    Debug(DebugAll,"CoreAudioSource [%p] end of data",this);
}

void CoreAudioSource::cleanup()
{
    Debug(DebugAll,"CoreAudioSource [%p] cleanup, total=%u",this,m_total);
    AudioConverterDispose(m_audioConvert);
    ThreadedSource::cleanup();
}

bool CoreAudioSource::control(NamedList& params)
{
    DDebug(DebugAll,"CoreAudioSource::control() [%p]",this);
    if (!m_volSettable)
	return TelEngine::controlReturn(&params,false);
    int vol = params.getIntValue("in_volume",-1);
    if (vol == -1) {
	Debug(DebugAll,"CoreAudioSource::control() [%p] - invalid value to set for volume",this);
	return TelEngine::controlReturn(&params,false);
    }
    Float32 volValue = vol / 100.0;

    bool setVolStatus = false;
    bool getVolStatus = false;
    int setVolValue = 0;
    for (unsigned int i = 0; i <= m_channels; i++) {
        AudioObjectPropertyAddress volumeAddress = {kAudioDevicePropertyVolumeScalar,kAudioDevicePropertyScopeInput,i};
        OSStatus err = AudioObjectSetPropertyData(fInputDevID,&volumeAddress,0,NULL,sizeof(Float32),&volValue);
        if (err != noErr)
            DDebug(DebugAll,"CoreAudioSource::control() [%p] - set volume failed with error=%4.4s, %ld on channel %u",this,(char*)&err,(long int)err,i);
        setVolStatus = (err == noErr) || setVolStatus;

        // get the actual set volume value
        Float32 setVolumePerChannel = 0;
        UInt32 size = sizeof(setVolumePerChannel);
        err = AudioObjectGetPropertyData(fInputDevID,&volumeAddress,0,NULL,&size,&setVolumePerChannel);
        if (err != noErr)
            DDebug(DebugAll,"CoreAudioSource::control() [%p] - get volume failed with error=%4.4s, %ld on channel %u",this,(char*)&err,(long int)err,i);
        else {
            if (setVolValue / 100.0 < setVolumePerChannel)
                setVolValue = setVolumePerChannel * 100;
        }
        getVolStatus = (err == noErr) || getVolStatus;
    }
    if (getVolStatus)
        params.setParam("in_volume",String(setVolValue));
    if (!setVolStatus)
        Debug(DebugAll,"CoreAudioSource::control() [%p] - set volume failed on all channels",this);

    if(params.getParam("out_volume"))
	return TelEngine::controlReturn(&params,false);
    return TelEngine::controlReturn(&params,setVolStatus);
}


CoreAudioConsumer::CoreAudioConsumer(unsigned int rate)
    : Mutex(false,"CoreAudioConsumer"),
      m_total(0), m_volSettable(false), m_channels(0), fOutputDevID(0), m_rate(rate)
{
    Debug(DebugAll,"CoreAudioConsumer::CoreAudioConsumer() [%p]",this);
    if (m_rate != DEFAULT_SAMPLE_RATE)
	m_format << "/" << m_rate;
}

CoreAudioConsumer::~CoreAudioConsumer()
{
    Debug(DebugAll,"CoreAudioConsumer::~CoreAudioConsumer() [%p] total=%u",this,m_total);
    OSStatus err = AudioOutputUnitStop(fAudioUnit);
    if(err != noErr)
	Debug(DebugInfo,"CoreAudioConsumer::~CoreAudioConsumer() [%p] - Failed to stop output AudioUnit error=%4.4s, %ld",this,(char*)&err,(long int)err);
    err = AudioUnitUninitialize(fAudioUnit);
    if(err != noErr)
    	Debug(DebugInfo,"CoreAudioConsumer::~CoreAudioConsumer() [%p] - Failed to uninitialize the AudioUnit error=%4.4s, %ld",this,(char*)&err,(long int)err);
}

bool CoreAudioConsumer::init()
{
    OSStatus err = noErr;

    // open the AudioOutputUnit, provide description
#ifdef MAC_OS_X_VERSION_10_6
    AudioComponent component;
    AudioComponentDescription description;
#else
    Component component;
    ComponentDescription description;
#endif

    description.componentType = kAudioUnitType_Output;
    description.componentSubType = kAudioUnitSubType_DefaultOutput;
    description.componentManufacturer = kAudioUnitManufacturer_Apple;
    description.componentFlags = 0;
    description.componentFlagsMask = 0;

#ifdef MAC_OS_X_VERSION_10_6
    if((component = AudioComponentFindNext(NULL,&description)))
	err = AudioComponentInstanceNew(component,&fAudioUnit);
#else
    if((component = FindNextComponent(NULL,&description)))
	err = OpenAComponent(component,&fAudioUnit);
#endif

    if(err != noErr) {
        Debug(DebugInfo,"CoreAudioConsumer::init() [%p] - failed to open component error==%4.4s, %ld",this,(char*)&err,(long int)err);
        fAudioUnit = NULL;
        return false;
    }

    // set up the callback to generate output to the output unit
    AURenderCallbackStruct callback;
    callback.inputProc = CoreAudioConsumer::outputCallback;
    callback.inputProcRefCon = this;
    err = AudioUnitSetProperty (fAudioUnit,kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,0,&callback,sizeof(callback));
    if (err != noErr)
	Debug(DebugInfo,"CoreAudioConsumer::init() [%p]- callback could not be set error=%4.4s, %ld",this,(char*)&err,(long int)err);

    // provide the input format of the date we're supplying
    AudioStreamBasicDescription inputFormat;
    inputFormat.mSampleRate = rate();
    inputFormat.mFormatID = kAudioFormatLinearPCM;
    inputFormat.mBitsPerChannel = sizeof(int16_t) * 8; //  = 16
    inputFormat.mBytesPerFrame = inputFormat.mBitsPerChannel / 8;
    inputFormat.mFramesPerPacket = 1;
    inputFormat.mBytesPerPacket = inputFormat.mBytesPerFrame;
    inputFormat.mChannelsPerFrame = 1;
    inputFormat.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    inputFormat.mFormatFlags &= ~kLinearPCMFormatFlagIsNonInterleaved;

    err = AudioUnitSetProperty(fAudioUnit,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,0,&inputFormat,sizeof(AudioStreamBasicDescription));
    if (err != noErr) {
	Debug(DebugInfo,"CoreAudioConsumer::init() [%p] - set input format failed error==%4.4s, %ld",this,(char*)&err,(long int)err);
	return false;
    }
    DDebug(DebugInfo,"CoreAudioConsumer::init() [%p] - intput format is : channels/frame=%u, sampleRate=%f, bits/channel=%u, "
	   "bytes/frame=%u, frames/packet=%u, bytes/packet=%u, formatFlags=0x%x",
	   this,(unsigned int)inputFormat.mChannelsPerFrame,inputFormat.mSampleRate,(unsigned int)inputFormat.mBitsPerChannel,
	   (unsigned int)inputFormat.mBytesPerFrame,(unsigned int)inputFormat.mFramesPerPacket,(unsigned int)inputFormat.mBytesPerPacket,
	   (unsigned int)inputFormat.mFormatFlags);

    // initialize the AudioUnit
    err = AudioUnitInitialize(fAudioUnit);
    if (err != noErr) {
	Debug(DebugInfo,"CoreAudioConsumer::init() [%p] - AudioUnitInitialize failed error=%4.4s, %ld",this,(char*)&err,(long int)err);
	return false;
    }

    // start the AudioUnit
    err = AudioOutputUnitStart(fAudioUnit);
    if (err != noErr) {
	Debug(DebugInfo,"CoreAudioConsumer::init() [%p] - AudioUnitStart failed error=%4.4s, %ld",this,(char*)&err,(long int)err);
	return false;
    }

    // get the id of the default output device
    UInt32 param = sizeof(AudioDeviceID);
    param = sizeof(AudioDeviceID);
    AudioObjectPropertyAddress devAddress = {kAudioHardwarePropertyDefaultOutputDevice,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMaster};
    err = AudioObjectGetPropertyData(kAudioObjectSystemObject,&devAddress,0,NULL,&param,&fOutputDevID);
    if(err != noErr)
	Debug(DebugMild,"CoreAudioConsumer::init() [%p] - Failed to get the device id of the output device error==%4.4s, %ld",this,(char*)&err,(long int)err);

    // get hardware device format
    param = sizeof(AudioStreamBasicDescription);
    AudioStreamBasicDescription devFormat;
    err = AudioUnitGetProperty(fAudioUnit,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Output,0,&devFormat,&param);
    if(err != noErr) {
	Debug(DebugInfo,"CoreAudioConsumer::init() [%p] - failed to get input device AudioStreamBasicDescription error==%4.4s, %ld",this,(char*)&err,(long int)err);
	// we didn't get the hardware format, but it's a safe bet that we have at least 1 channel
        m_channels = 1;
    }
    else {
        m_channels = devFormat.mChannelsPerFrame;

        DDebug(DebugInfo,"CoreAudioConsumer::init() [%p] - hardware device input format is : channels/frame=%u, sampleRate=%f, bits/channel=%u, "
               "bytes/frame=%u, frames/packet=%u, bytes/packet=%u, formatFlags=0x%x",
               this,(unsigned int)devFormat.mChannelsPerFrame,devFormat.mSampleRate,(unsigned int)devFormat.mBitsPerChannel,
               (unsigned int)devFormat.mBytesPerFrame,(unsigned int)devFormat.mFramesPerPacket,(unsigned int)devFormat.mBytesPerPacket,
               (unsigned int)devFormat.mFormatFlags);
    }

    m_volSettable = false;
    for (unsigned int i = 0; i <= m_channels; i++)
        m_volSettable = checkVolumeSettable(fOutputDevID,i,false) || m_volSettable;
    Debug(DebugAll,"CoreAudioConsumer::init() - volume %s settable",(m_volSettable ? "is" : "isn't"));
    return true;
}

void CoreAudioConsumer::getData(AudioBufferList* buf)
{
    if (!buf)
        return;
    // put the data into the output buffer
    UInt32 len = buf->mBuffers[0].mDataByteSize; // there should be only one buffer;
    lock();
    if (m_data.length() == 0) {
        ::memset(buf->mBuffers[0].mData,0,len);
        unlock();
        return;
    }
    if (len > m_data.length())
	len = m_data.length();
    if (len > 0) {
	::memcpy(buf->mBuffers[0].mData,m_data.data(),len);
	m_data.cut(-len);
    }
    unlock();
}

OSStatus CoreAudioConsumer::outputCallback(void* inRefCon, AudioUnitRenderActionFlags* ioActionFlags, const AudioTimeStamp* inTimeStamp,
							   UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList* ioData)
{
    CoreAudioConsumer* dst = static_cast<CoreAudioConsumer*>(inRefCon);
    if (!dst)
	return 1;
    XDebug(DebugAll,"CoreAudioConsumer::outputCallback() [%p] inNumberFrames=%d buffersCount=%d buffersize=%d",dst,(unsigned int)inNumberFrames,
	   (unsigned int)ioData->mNumberBuffers,(unsigned int)ioData->mBuffers[0].mDataByteSize);
    dst->getData(ioData);
    return noErr;
}

unsigned long CoreAudioConsumer::Consume(const DataBlock &data, unsigned long tStamp, unsigned long flags)
{
    // append to the internal buffer received data
    if (data.null())
	return 0;
    lock();
    m_total += data.length();
    m_data.append(data);
    unlock();
    return invalidStamp();
}

bool CoreAudioConsumer::control(NamedList& params)
{
    DDebug(DebugAll,"CoreAudioConsumer::control() [%p]",this);
    if (!m_volSettable)
	return TelEngine::controlReturn(&params,false);
    int vol = params.getIntValue("out_volume",-1);
    if (vol == -1) {
	Debug(DebugAll,"CoreAudioConsumer::control() [%p] invalid value to set for volume",this);
	return TelEngine::controlReturn(&params,false);
    }
    Float32 volValue = vol / 100.0;

    bool setVolStatus = false;
    bool getVolStatus = false;
    int setVolValue = 0;
    for (unsigned int i = 0; i <= m_channels; i++) {
        // set the volume for the output on every channel
        AudioObjectPropertyAddress volumeAddress = {kAudioDevicePropertyVolumeScalar,kAudioDevicePropertyScopeOutput,i};
        OSStatus err = AudioObjectSetPropertyData(fOutputDevID,&volumeAddress,0,NULL,sizeof(Float32),&volValue);
        if (err != noErr)
            DDebug(DebugAll,"CoreAudioConsumer::control() [%p] - set volume failed with error=%4.4s, %ld on channel %u",this,(char*)&err,(long int)err,i);
        setVolStatus = (err == noErr) || setVolStatus;

        // get the actual set volume value
        Float32 setVolumePerChannel = 0;
        UInt32 size = sizeof(setVolumePerChannel);
        err = AudioObjectGetPropertyData(fOutputDevID,&volumeAddress,0,NULL,&size,&setVolumePerChannel);
        if (err != noErr)
            DDebug(DebugAll,"CoreAudioComsumer::control() [%p] - get volume failed with error=%4.4s, %ld on channel %u",this,(char*)&err,(long int)err,i);
        else {
            if (setVolValue / 100.0 < setVolumePerChannel)
                setVolValue = setVolumePerChannel * 100;
        }
        getVolStatus = (err == noErr) || getVolStatus;
    }
    if (getVolStatus)
        params.setParam("out_volume",String(setVolValue));
    if (!setVolStatus)
        Debug(DebugAll,"CoreAudioConsumer::control() [%p] - set volume failed on all channels",this);

    return TelEngine::controlReturn(&params,setVolStatus);
}

CoreAudioChan::CoreAudioChan(const String& dev, unsigned int rate)
    : CallEndpoint("coreaudio"),
      m_dev(dev), m_rate(rate)
{
    Debug(DebugAll,"CoreAudioChan::CoreAudioChan ('%s') [%p]",dev.c_str(),this);
    s_audioChan = this;
}

CoreAudioChan::~CoreAudioChan()
{
    Debug(DebugAll,"CoreAudioChan::~CoreAudioChan() [%p]",this);
    setTarget();
    setSource();
    setConsumer();
    s_audioChan = 0;
}

bool CoreAudioChan::init()
{
    CoreAudioSource* source = new CoreAudioSource(rate());
    if (!source->init()) {
	source->deref();
	return false;
    }
    setSource(source);
    source->deref();
    CoreAudioConsumer* cons = new CoreAudioConsumer(rate());
    if (!cons->init()) {
	cons->deref();
	setSource();
	return false;
    }
    setConsumer(cons);
    cons->deref();
    return true;
}

void CoreAudioChan::disconnected(bool final, const char *reason)
{
    Debug(DebugInfo,"CoreAudioChan::disconnected() '%s' [%p]",reason,this);
    setTarget();
}

void CoreAudioChan::answer()
{
    Message* m = new Message("call.answered");
    m->addParam("module","coreaudio");
    String tmp("coreaudio/");
    tmp += m_dev;
    m->addParam("id",tmp);
    if (m_target)
	m->addParam("targetid",m_target);
    Engine::enqueue(m);
}

bool CoreAudioHandler::received(Message &msg)
{
    Debug(DebugInfo,"CoreAudio received call.execute");
    String dest(msg.getValue("callto"));
    if (dest.null())
        return false;
    static const Regexp r("^coreaudio/\\(.*\\)$");
    if (!dest.matches(r))
        return false;
    if (s_audioChan) {
        msg.setParam("error","busy");
        return false;
    }
    CoreAudioChan *chan = new CoreAudioChan(dest.matchString(1).c_str(),msg.getIntValue("rate",DEFAULT_SAMPLE_RATE));
    if (!chan->init()) 	{
        chan->destruct();
        return false;
    }
    CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userData());
    Debug(DebugInfo,"We are routing to device '%s'",dest.matchString(1).c_str());
    if (ch && chan->connect(ch,msg.getValue("reason"))) {
        chan->setTarget(msg.getValue("id"));
        msg.setParam("peerid",dest);
        msg.setParam("targetid",dest);
        chan->answer();
        chan->deref();
    }
    else {
        const char *direct = msg.getValue("direct");
        if (direct) {
	    Message m("call.execute");
	    m.addParam("module","audiocore");
	    m.addParam("cdrtrack",String::boolText(false));
	    m.addParam("id",dest);
	    m.addParam("caller",dest);
	    m.addParam("callto",direct);
	    m.userData(chan);
	    if (Engine::dispatch(m)) {
	        chan->setTarget(m.getValue("targetid"));
	        msg.addParam("targetid",chan->getTarget());
		chan->deref();
		return true;
	    }
	    Debug(DebugInfo,"CoreAudio outgoing call not accepted!");
	    chan->destruct();
	    return false;
	}
	const char *targ = msg.getValue("target");
	if (!targ) {
	    Debug(DebugWarn,"CoreAudio outgoing call with no target!");
	    chan->destruct();
	    return false;
	}
	Message m("call.route");
	m.addParam("module","audiocore");
	m.addParam("cdrtrack",String::boolText(false));
	m.addParam("id",dest);
	m.addParam("caller",dest);
	m.addParam("called",targ);
	if (Engine::dispatch(m)) {
	    m = "call.execute";
	    m.addParam("callto",m.retValue());
	    m.retValue() = 0;
	    m.userData(chan);
	    if (Engine::dispatch(m)) {
		chan->setTarget(m.getValue("targetid"));
		msg.addParam("targetid",chan->getTarget());
		chan->deref();
		return true;
	    }
	    Debug(DebugInfo,"CoreAudio outgoing call not accepted!");
	}
	else
	    Debug(DebugWarn,"CoreAudio outgoing call but no route!");
	chan->destruct();
	return false;
    }

    return true;
}

bool StatusHandler::received(Message &msg)
{
    const String* sel = msg.getParam("module");
    if (sel && (*sel != "coreaudio"))
        return false;
    msg.retValue() << "name=coreaudio,type=misc;chan=" << (s_audioChan != 0 ) << "\r\n";
    return false;
}

bool MasqHandler::received(Message &msg)
{
    String id(msg.getValue("id"));
    if (msg.getParam("message") && id.startsWith("coreaudio/")) {
	msg = msg.getValue("message");
	msg.clearParam("message");
	if (s_audioChan) {
	    msg.addParam("targetid",s_audioChan->getTarget());
	    msg.userData(s_audioChan);
	}
    }
    return false;
}

bool DropHandler::received(Message &msg)
{
    String id(msg.getValue("id"));
    if (id.null() || id.startsWith("coreaudio/")) {
	if (s_audioChan) {
	    Debug("CoreAudio",DebugInfo,"ping call");
	    s_audioChan->disconnect();
	}
	return !id.null();
    }
    return false;
}

bool AttachHandler::received(Message& msg)
{
    int more = 2;
    String src(msg.getValue("source"));
    if (src.null())
    	more--;
    else {
    	if (!src.startSkip("coreaudio/",false))
	    src = "";
    }

    String cons(msg.getValue("consumer"));
    if (cons.null())
	more--;
    else {
	if (!cons.startSkip("coreaudio/",false))
	    cons = "";
    }

    if (src.null() && cons.null())
	return false;
    if (src && cons && (src != cons)) {
	Debug(DebugWarn,"CoreAudio asked to attach source '%s' and consumer '%s'",src.c_str(),cons.c_str());
	return false;
    }

    RefPointer<DataEndpoint> dd = static_cast<DataEndpoint*>(msg.userObject(YATOM("DataEndpoint")));
    if (!dd) {
	CallEndpoint *ch = static_cast<CallEndpoint*>(msg.userObject(YATOM("CallEndpoint")));
	if (ch) {
	    DataEndpoint::commonMutex().lock();
	    dd = ch->setEndpoint();
	    DataEndpoint::commonMutex().unlock();
	}
    }
    if (!dd) {
	Debug(DebugWarn,"CoreAudio attach request with no control or data channel!");
	return false;
    }

    if (src) {
	CoreAudioSource* s = new CoreAudioSource(msg.getIntValue("rate",DEFAULT_SAMPLE_RATE));
	if (s->init())
	    dd->setSource(s);
	s->deref();
    }

    if (cons) {
	CoreAudioConsumer* c = new CoreAudioConsumer(msg.getIntValue("rate",DEFAULT_SAMPLE_RATE));
	if (c->init())
	    dd->setConsumer(c);
	c->deref();
    }

    // Stop dispatching if we handled all requested
    return !more;
}

CoreAudioPlugin::CoreAudioPlugin()
	: Plugin("coreaudio"),
	  m_handler(0)
{
    Output("Loaded module CoreAudio");
}

void CoreAudioPlugin::initialize()
{
    Output("Initializing module CoreAudio");
    if (!m_handler) {
	m_handler = new CoreAudioHandler("call.execute");
	Engine::install(m_handler);
	Engine::install(new MasqHandler(10));
	Engine::install(new DropHandler());
	Engine::install(new StatusHandler());
	Engine::install(new AttachHandler());
    }
}

bool CoreAudioPlugin::isBusy() const
{
    return (s_audioChan != 0);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
