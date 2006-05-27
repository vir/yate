/**
 * yateiax.h
 * Yet Another IAX2 Stack
 * This file is part of the YATE Project http://YATE.null.ro
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

#ifndef __YATEIAX_H
#define __YATEIAX_H

#include <yateclass.h>

#ifdef _WINDOWS

#ifdef LIBYIAX_EXPORTS
#define YIAX_API __declspec(dllexport)
#else
#ifndef LIBYIAX_STATIC
#define YIAX_API __declspec(dllimport)
#endif
#endif

#endif /* _WINDOWS */

#ifndef YIAX_API
#define YIAX_API
#endif

/** 
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

class IAXFullFrame;
class IAXEvent;
class IAXEngine;

/**
 * This class holds a single Information Element that is attached to a message
 * @short A single IAX2 Information Element
 */
class YIAX_API IAXInfoElement
{
public:
    enum Type {
	CALLED_NUMBER = 0x01,
	CALLING_NUMBER = 0x02,
	CALLING_ANI = 0x03,
	CALLING_NAME = 0x04,
	CALLED_CONTEXT = 0x05,
    };
};

/**
 * This class holds the enumeration values for audio and video formats
 * @short Wrapper class for audio and video formats
 */
class YIAX_API IAXFormat {
public:
    enum Audio {
	G723_1 = (1 <<  0),
	GSM    = (1 <<  1),
	ULAW   = (1 <<  2),
	ALAW   = (1 <<  3),
	MP3    = (1 <<  4),
	ADPCM  = (1 <<  5),
	SLIN   = (1 <<  6),
	LPC10  = (1 <<  7),
	G729A  = (1 <<  8),
	SPEEX  = (1 <<  9),
	ILBC   = (1 << 10),
    };
    enum Video {
	JPEG   = (1 << 16),
	PNG    = (1 << 17),
	H261   = (1 << 18),
	H263   = (1 << 19),
    };
};

class YIAX_API IAXFrame : public RefObject
{
public:
    enum Type {
	DTMF    = 0x01,
	Voice   = 0x02,
	Video   = 0x03,
	Control = 0x04,
	Null    = 0x05,
	IAX     = 0x06,
	Text    = 0x07,
	Image   = 0x08,
	HTML    = 0x09,
    };

    IAXFrame(Type type, u_int16_t sCallNo, u_int32_t tStamp, bool retrans,
	const unsigned char* buf, unsigned int len);
    
    virtual ~IAXFrame();

    inline Type type() const
	{ return m_type; }

    inline bool meta() const
	{ return m_meta; }

    inline u_int16_t sourceCallNo() const
	{ return m_sCallNo; }

    inline u_int32_t timeStamp() const
	{ return m_tStamp; }

    virtual const IAXFullFrame* fullFrame() const;

    static IAXFrame* parse(const unsigned char* buf, unsigned int len, IAXEngine* engine = 0, const SocketAddr* addr = 0);

protected:
    DataBlock m_raw;
    Type m_type;
    bool m_meta;
    u_int16_t m_sCallNo;
    u_int32_t m_tStamp;
    u_int32_t m_subclass;
};

class YIAX_API IAXFullFrame : public IAXFrame
{
public:
    enum ControlType {
	//TODO: validate codes with asterisk, libiax, rfc-draft
	Hangup = 0x01,
//	Ring = 0x02,
	Ringing = 0x03,
	Answer = 0x04,
	Busy = 0x05,
	Congestion = 0x08,
	FlashHook = 0x09,
	Option = 0x0b,
	KeyRadio = 0x0c,
	UnkeyRadio = 0x0d,
	Progressing = 0x0e,
	Proceeding = 0x0f,
	Hold = 0x10,
	Unhold = 0x11,
	VidUpdate = 0x12,
    };

    IAXFullFrame(Type type, u_int32_t subClass,
	u_int16_t sCallNo, u_int16_t dCallNo,
	unsigned char oSeqNo, unsigned char iSeqNo,
	u_int32_t tStamp, bool retrans,
	const unsigned char* buf, unsigned int len);

    inline u_int16_t destCallNo() const
	{ return m_dCallNo; }

    virtual const IAXFullFrame* fullFrame() const;
private:
    u_int16_t m_dCallNo;
};

class YIAX_API IAXTransaction : public RefObject
{
public:
    /**
     * Constructor
     * @param data Pointer to arbitrary user data
     */
    IAXTransaction(IAXEngine* engine, void* data = 0);

    /**
     * Destructor
     */
    virtual ~IAXTransaction();

    /**
     * The IAX engine this transaction belongs to
     * @return Pointer to the IAXEngine of this transaction
     */
    inline IAXEngine* getEngine() const
	{ return m_engine; }

    /**
     * Store a pointer to arbitrary user data
     * @param data User provided pointer
     */
    inline void setUserData(void* data)
	{ m_private = data; }

    /**
     * Return the opaque user data stored in the transaction
     * @return Pointer set by user
     */
    inline void* getUserData() const
	{ return m_private; }

    /**
     * Retrive the local call number
     * @return 15-bit local call number
     */
    inline u_int16_t localCallNo() const
	{ return m_lCallNo; }

    /**
     * Retrive the remote call number
     * @return 15-bit remote call number
     */
    inline u_int16_t remoteCallNo() const
	{ return m_rCallNo; }

    /**
     * Retrive the remote host+port address
     * @return A reference to the remote address
     */
    inline const SocketAddr& remoteAddr() const
	{ return m_addr; }

    /**
     * Attempt to process a frame
     * @param addr Address from where the frame was received
     * @param frame IAX frame that we attempt to process
     * @return True if the frame belongs to this transaction
     */
    bool process(const SocketAddr& addr, IAXFrame* frame);

    /**
     * Get an IAX event from the queue.
     * @return Pointer to an IAXEvent or NULL if none is available
     */
    IAXEvent* getEvent();

private:
    SocketAddr m_addr;
    u_int16_t m_lCallNo;
    u_int16_t m_rCallNo;
    IAXEngine* m_engine;
    void* m_private;
};

class YIAX_API IAXEvent
{
    friend class IAXTransaction;
public:
    /**
     * Types of events
     */
    enum Type {
	Invalid = 0,
	Timeout,
    };

    /**
     * Destructor.
     * Dereferences the transaction possibly causing its destruction.
     */
    ~IAXEvent();

    /**
     * Get the type of this transaction
     * @return The type of the transaction as enumeration
     */
    inline Type type() const
	{ return m_type; }

    /**
     * Check if this is a transaction finalization event
     * @return True if the transaction has finalized and will be destroyed
     */
    inline bool final() const
	{ return m_final; }

    /**
     * The IAX engine this event belongs to, if any
     */
    inline IAXEngine* getEngine() const
	{ return m_transaction ? m_transaction->getEngine() : 0; }

    /**
     * The IAX transaction that gererated the event, if any
     */
    inline IAXTransaction* getTransaction() const
	{ return m_transaction; }

    /**
     * Return the opaque user data stored in the transaction
     */
    inline void* getUserData() const
	{ return m_transaction ? m_transaction->getUserData() : 0; }

protected:
    /**
     * Constructor
     * @param transaction IAX transaction that generated the event
     */
    IAXEvent(Type type, bool final, IAXTransaction* transaction);

private:
    Type m_type;
    bool m_final;
    IAXTransaction* m_transaction;
};

class YIAX_API IAXEngine : public DebugEnabler, public Mutex
{
public:
    /**
     * Constructor
     * @param transCount Number of entries in the transaction hash table
     */
    IAXEngine(int transCount = 16);

    /**
     * Destructor
     */
    virtual ~IAXEngine();

    /**
     * Add a parsed frame to the transaction list
     * @param addr Address from which the message was received
     * @param frame A parsed IAX frame
     * @return Pointer to the transaction, NULL if invalid
     */
    IAXTransaction* addFrame(const SocketAddr& addr, IAXFrame* frame);

    /**
     * Add a raw frame to the transaction list
     * @param addr Address from which the message was received
     * @param buf Pointer to the start of the buffer holding the IAX frame
     * @param len Length of the message buffer
     * @return Pointer to the transaction, NULL if invalid
     */
    IAXTransaction* addFrame(const SocketAddr& addr, const unsigned char* buf, unsigned int len);

    /**
     * Get an IAX event from the queue.
     * This method is thread safe.
     * @return Pointer to an IAXEvent or NULL if none is available
     */
    IAXEvent* getEvent();

    /**
     * Default event handler. This method may be overriden to perform custom
     *  processing.
     * This method is thread safe.
     */
    virtual void processEvent(IAXEvent* event);

    /**
     * Event processor method. Keeps calling @ref getEvent() and passing
     *  any events to @ref processEvent() until there are no more events.
     * @return True if at least one event was processed
     */
    bool process();

private:
    ObjList** m_transList;
    int m_transListCount;
    u_int32_t m_callno;
};

}

#endif /* __YATEIAX_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
