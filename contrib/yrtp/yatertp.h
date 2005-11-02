/**
 * yatertp.h
 * Yet Another RTP Stack
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004, 2005 Null Team
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

#ifndef __YATERTP_H
#define __YATERTP_H

#include <yateclass.h>

#ifdef _WINDOWS

#ifdef LIBYRTP_EXPORTS
#define YRTP_API __declspec(dllexport)
#else
#ifndef LIBYRTP_STATIC
#define YRTP_API __declspec(dllimport)
#endif
#endif

#endif /* _WINDOWS */

#ifndef YRTP_API
#define YRTP_API
#endif

/**
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

class RTPGroup;
class RTPTransport;
class RTPSession;
class RTPSender;

/**
 * A base class that contains just placeholders to process raw RTP and RTCP packets.
 * @short Base class to ease creation of RTP forwarders
 */
class YRTP_API RTPProcessor : public GenObject
{
    friend class RTPGroup;
    friend class RTPTransport;
    friend class RTPSession;
    friend class RTPSender;

public:
    /**
     * Constructor - processor should be later inserted in a RTP group
     */
    RTPProcessor();

    /**
     * Destructor - removes itself from the RTP group
     */
    virtual ~RTPProcessor();

    /**
     * Get the RTP group to which this processor belongs
     * @return Pointer to the RTP group this processor has joined
     */
    inline RTPGroup* group() const
	{ return m_group; }

    /**
     * This method is called to send or process a RTP packet
     * @param data Pointer to raw RTP data
     * @param len Length of the data packet
     */
    virtual void rtpData(const void* data, int len) = 0;

    /**
     * This method is called to send or process a RTCP packet
     * @param data Pointer to raw RTCP data
     * @param len Length of the data packet
     */
    virtual void rtcpData(const void* data, int len) = 0;

protected:
    /**
     * Set a new RTP group for this processor
     * @param newgrp New group to join this processor, the old one will be left
     */
    void group(RTPGroup* newgrp);

    /**
     * Method called periodically to keep the data flowing
     * @param when Time to use as base in all computing
     */
    virtual void timerTick(const Time& when) = 0;

private:
    RTPGroup* m_group;
};

/**
 * Several possibly related RTP processors share the same RTP group which
 *  holds the thread that keeps them running.
 * @short A group of RTP processors handled by the same thread
 */
class YRTP_API RTPGroup : public GenObject, public Mutex, public Thread
{
    friend class RTPProcessor;

public:
    /**
     * Constructor
     * @param prio Thread priority to run this group
     */
    RTPGroup(Priority prio = Normal);

    /**
     * Group destructor, removes itself from all remaining processors
     */
    virtual ~RTPGroup();

    /**
     * Inherited thread cleanup
     */
    virtual void cleanup();

    /**
     * Inherited thread run method
     */
    virtual void run();

protected:
    /**
     * Add a RTP processor to this group
     * @param processor Pointer to the RTP processor to add
     */
    void join(RTPProcessor* proc);

    /**
     * Remove a RTP processor from this group
     * @param processor Pointer to the RTP processor to remove
     */
    void part(RTPProcessor* proc);

private:
    ObjList m_processors;
};

/**
 * Class that holds sockets and addresses for transporting RTP and RTCP packets.
 * @short Low level transport for RTP and RTCP
 */
class YRTP_API RTPTransport : public RTPProcessor
{
public:
    enum Activation {
	Inactive,
	Bound,
	Active
    };

    /**
     * Constructor, creates an unconnected transport
     * @param grp RTP group to join
     */
    RTPTransport();

    /**
     * Destructor
     */
    virtual ~RTPTransport();

    /**
     * Set the RTP/RTCP processor of data received by this transport
     * @param processor A pointer to the RTPProcessor for this transport
     */
    void setProcessor(RTPProcessor* processor = 0);

    /**
     * Set the RTP/RTCP monitor of data received by this transport
     * @param monitor A pointer to a second RTPProcessor for this transport
     */
    void setMonitor(RTPProcessor* monitor = 0);

    /**
     * Get the local network address of the RTP transport
     * @return Reference to the local RTP transport address
     */
    inline const SocketAddr& localAddr() const
	{ return m_localAddr; }

    /**
     * Get the remote network address of the RTP transport
     * @return Reference to the remote RTP transport address
     */
    inline const SocketAddr& remoteAddr() const
	{ return m_remoteAddr; }

    /**
     * Set the local network address of the RTP transport
     * @param addr New local RTP transport address
     * @return True if address set, false if a failure occured
     */
    bool localAddr(SocketAddr& addr);

    /**
     * Set the remote network address of the RTP transport
     * @param addr New remote RTP transport address
     * @param sniff Automatically adjust the address from the first incoming packet
     * @return True if address set, false if a failure occured
     */
    bool remoteAddr(SocketAddr& addr, bool sniff = false);

    /**
     * Set the Type Of Service for the RTP socket
     * @param tos Type Of Service bits to set
     * @return True if operation was successfull, false if an error occured
     */
    inline bool setTOS(int tos)
	{ return m_rtpSock.setTOS(tos); }

protected:
    /**
     * Method called periodically to read data out of sockets
     * @param when Time to use as base in all computing
     */
    virtual void timerTick(const Time& when);

    /**
     * This method is called to send a RTP packet
     * @param data Pointer to raw RTP data
     * @param len Length of the data packet
     */
    virtual void rtpData(const void* data, int len);

    /**
     * This method is called to send a RTCP packet
     * @param data Pointer to raw RTCP data
     * @param len Length of the data packet
     */
    virtual void rtcpData(const void* data, int len);

private:
    RTPProcessor* m_processor;
    RTPProcessor* m_monitor;
    Socket m_rtpSock;
    Socket m_rtcpSock;
    SocketAddr m_localAddr;
    SocketAddr m_remoteAddr;
    SocketAddr m_remoteRTCP;
    bool m_autoRemote;
};

/**
 * Base class that holds common sender and receiver methods
 * @short Common send/recv variables holder
 */
class YRTP_API RTPBaseIO
{
    friend class RTPSession;
public:
    /**
     * Default constructor.
     */
    inline RTPBaseIO(RTPSession* session = 0)
	: m_session(session), m_ssrc(0), m_ts(0), m_seq(0),
	  m_evTs(0), m_evNum(-1), m_evVol(-1),
	  m_dataType(-1), m_eventType(-1), m_silenceType(-1)
	{ }

    /**
     * Do-nothing destructor
     */
    virtual ~RTPBaseIO()
	{ }

    /**
     * Get the payload type for data packets
     * @return Payload type, -1 if not set
     */
    inline int dataPayload() const
	{ return m_dataType; }

    /**
     * Set the payload type for data packets
     * @param type Payload type, -1 to disable
     * @return True if changed, false if invalid payload type
     */
    bool dataPayload(int type);

    /**
     * Get the payload type for event packets
     * @return Payload type, -1 if not set
     */
    inline int eventPayload() const
	{ return m_eventType; }

    /**
     * Set the payload type for event packets
     * @param type Payload type, -1 to disable
     * @return True if changed, false if invalid payload type
     */
    bool eventPayload(int type);

    /**
     * Get the payload type for Silence event packets
     * @return Payload type, -1 if not set
     */
    inline int silencePayload() const
	{ return m_silenceType; }

    /**
     * Set the payload type for Silence event packets.
     * Thanks, Silence, for a new and incompatible way of sending events.
     * @param type Payload type, -1 to disable
     * @return True if changed, false if invalid payload type
     */
    bool silencePayload(int type);

    /**
     * Reset the SSRC requesting generation/grabbing of a new one
     */
    inline void reset()
	{ m_ssrc = 0; }

    /**
     * Get the value of the current SSRC, zero if not initialized yet
     */
    inline unsigned int ssrc() const
	{ return m_ssrc; }

    /**
     * Force a new known SSRC for all further packets
     */
    inline void ssrc(unsigned int src)
	{ m_ssrc = src; }

protected:
    /**
     * Method called periodically to keep the data flowing
     * @param when Time to use as base in all computing
     */
    virtual void timerTick(const Time& when) = 0;

    RTPSession* m_session;
    u_int32_t m_ssrc;
    u_int32_t m_ts;
    u_int16_t m_seq;
    u_int32_t m_evTs;
    int m_evNum;
    int m_evVol;

private:
    int m_dataType;
    int m_eventType;
    int m_silenceType;
};

/**
 * Class that handles incoming RTP and RTCP packets
 * @short RTP/RTCP packet receiver
 */
class YRTP_API RTPReceiver : public RTPBaseIO
{
    friend class RTPSession;
public:
    /**
     * Constructor
     */
    inline RTPReceiver(RTPSession* session = 0)
	: RTPBaseIO(session), m_warn(true)
	{ }

    /**
     * Do-nothing destructor
     */
    virtual ~RTPReceiver()
	{ }

    /**
     * Process one RTP payload packet.
     * Default behaviour is to call rtpRecvData() or rtpRecvEvent().
     * @param marker Set to true if the marker bit is set
     * @param payload Payload number
     * @param timestamp Sampling instant of the packet data
     * @param data Pointer to data block to process
     * @param len Length of the data block in bytes
     * @return True if data was handled
     */
    virtual bool rtpRecv(bool marker, int payload, unsigned int timestamp,
	const void* data, int len);

    /**
     * Process one RTP data packet
     * @param marker Set to true if the marker bit is set
     * @param timestamp Sampling instant of the packet data
     * @param data Pointer to data block to process
     * @param len Length of the data block in bytes
     * @return True if data was handled
     */
    virtual bool rtpRecvData(bool marker, unsigned int timestamp,
	const void* data, int len);

    /**
     * Process one RTP event
     * @param event Received event code
     * @param key Received key (for events 0-16) or zero
     * @param duration Duration of the event as number of samples
     * @param volume Attenuation of the tone, zero for don't care
     * @param timestamp Sampling instant of the initial packet data
     * @return True if data was handled
     */
    virtual bool rtpRecvEvent(int event, char key, int duration,
	int volume, unsigned int timestamp);

    /**
     * Method called for unknown payload types just before attempting
     *  to call rtpRecvData(). This is a good opportunity to change the
     *  payload type and continue.
     * @param payload Payload number
     * @param timestamp Sampling instant of the unexpected packet data
     */
    virtual void rtpNewPayload(int payload, unsigned int timestamp);

    /**
    * Method called when a packet with an unexpected SSRC is received
    *  just before processing further. This is a good opportunity to
    *  change the SSRC and continue
    * @param newSsrc SSRC received in packet
    */
    virtual void rtpNewSSRC(u_int32_t newSsrc);

protected:
    /**
     * Method called periodically to finish lingering events
     * @param when Time to use as base in all computing
     */
    virtual void timerTick(const Time& when);

private:
    void rtpData(const void* data, int len);
    void rtcpData(const void* data, int len);
    bool decodeEvent(bool marker, unsigned int timestamp, const void* data, int len);
    bool decodeSilence(bool marker, unsigned int timestamp, const void* data, int len);
    void finishEvent(unsigned int timestamp);
    bool pushEvent(int event, int duration, int volume, unsigned int timestamp);
    bool m_warn;
};

/**
 * Class that builds and sends RTP and RTCP packets
 * @short RTP/RTCP packet sender
 */
class YRTP_API RTPSender : public RTPBaseIO
{
public:
    /**
     * Constructor
     */
    inline RTPSender(RTPSession* session = 0)
	: RTPBaseIO(session), m_evTime(0), m_tsLast(0)
	{ }

    /**
     * Do-nothing destructor
     */
    virtual ~RTPSender()
	{ }

    /**
     * Send one RTP payload packet
     * @param marker Set to true if the marker bit must be set
     * @param payload Payload number
     * @param timestamp Sampling instant of the packet data
     * @param data Pointer to data block to send
     * @param len Length of the data block
     * @return True if data sending was attempted
     */
    bool rtpSend(bool marker, int payload, unsigned int timestamp,
	const void* data, int len);

    /**
     * Send one RTP data packet
     * @param marker Set to true if the marker bit must be set
     * @param timestamp Sampling instant of the packet data
     * @param data Pointer to data block to send
     * @param len Length of the data block
     * @return True if data sending was attempted
     */
    bool rtpSendData(bool marker, unsigned int timestamp,
	const void* data, int len);

    /**
     * Send one RTP event
     * @param event Event code to send
     * @param duration Duration of the event as number of samples
     * @param volume Attenuation of the tone, zero for don't care
     * @param timestamp Sampling instant of the packet data, zero to use current
     * @return True if data sending was attempted
     */
    bool rtpSendEvent(int event, int duration, int volume = 0, unsigned int timestamp = 0);

    /**
     * Send one RTP key event
     * @param key Key to send
     * @param duration Duration of the event as number of samples
     * @param volume Attenuation of the tone, zero for don't care
     * @param timestamp Sampling instant of the packet data, zero to use current
     * @return True if data sending was attempted
     */
    bool rtpSendKey(char key, int duration, int volume = 0, unsigned int timestamp = 0);

protected:
    /**
     * Method called periodically to send events and buffered data
     * @param when Time to use as base in all computing
     */
    virtual void timerTick(const Time& when);

private:
    int m_evTime;
    unsigned int m_tsLast;
    bool sendEventData(unsigned int timestamp);
};

/**
 * An unidirectional or bidirectional RTP session
 * @short Full RTP session
 */
class YRTP_API RTPSession : public RTPProcessor
{
public:
    enum Direction {
	FullStop = 0,
	RecvOnly = 1,
	SendOnly = 2,
	SendRecv = 3
    };

    /**
     * Default constructor, creates a detached session
     */
    RTPSession();

    /**
     * Destructor - shuts down the session and destroys the transport
     */
    virtual ~RTPSession();

    /**
     * This method is called to process a RTP packet.
     * @param data Pointer to raw RTP data
     * @param len Length of the data packet
     */
    virtual void rtpData(const void* data, int len);

    /**
     * This method is called to process a RTCP packet.
     * @param data Pointer to raw RTCP data
     * @param len Length of the data packet
     */
    virtual void rtcpData(const void* data, int len);

    /**
     * Process one RTP data packet
     * @param marker Set to true if the marker bit is set
     * @param timestamp Sampling instant of the packet data
     * @param data Pointer to data block to process
     * @param len Length of the data block in bytes
     * @return True if data was handled
     */
    virtual bool rtpRecvData(bool marker, unsigned int timestamp,
	const void* data, int len);

    /**
     * Process one RTP event
     * @param event Received event code
     * @param key Received key (for events 0-16) or zero
     * @param duration Duration of the event as number of samples
     * @param volume Attenuation of the tone, zero for don't care
     * @param timestamp Sampling instant of the initial packet data
     * @return True if data was handled
     */
    virtual bool rtpRecvEvent(int event, char key, int duration,
	int volume, unsigned int timestamp);

    /**
     * Method called for unknown payload types just before attempting
     *  to call rtpRecvData(). This is a good opportunity to change the
     *  payload type and continue.
     * @param payload Payload number
     * @param timestamp Sampling instant of the unexpected packet data
     */
    virtual void rtpNewPayload(int payload, unsigned int timestamp);

    /**
    * Method called when a packet with an unexpected SSRC is received
    *  just before processing further. This is a good opportunity to
    *  change the SSRC and continue
    * @param newSsrc SSRC received in packet
    */
    virtual void rtpNewSSRC(u_int32_t newSsrc);

    /**
     * Create a new RTP sender for this session.
     * Override this method to create objects derived from RTPSender.
     * @return Pointer to the new sender or NULL on failure
     */
    virtual RTPSender* createSender();

    /**
     * Create a new RTP receiver for this session.
     * Override this method to create objects derived from RTPReceiver.
     * @return Pointer to the new receiver or NULL on failure
     */
    virtual RTPReceiver* createReceiver();

    /**
     * Create a new RTP transport for this session.
     * Override this method to create objects derived from RTPTransport.
     * @return Pointer to the new transport or NULL on failure
     */
    virtual RTPTransport* createTransport();

    /**
     * Initialize the RTP session, attach a transport if there is none
     * @return True if initialized, false on some failure
     */
    bool initTransport();

    /**
     * Initialize the RTP session, attach a group if none is present
     * @return True if initialized, false on some failure
     */
    bool initGroup();

    /**
     * Send one RTP payload packet
     * @param marker Set to true if the marker bit must be set
     * @param payload Payload number
     * @param timestamp Sampling instant of the packet data
     * @param data Pointer to data block to send
     * @param len Length of the data block
     * @return True if data sending was attempted
     */
    inline bool rtpSend(bool marker, int payload, unsigned int timestamp,
	const void* data, int len)
	{ return m_send && m_send->rtpSend(marker,payload,timestamp,data,len); }

    /**
     * Send one RTP data packet
     * @param marker Set to true if the marker bit must be set
     * @param timestamp Sampling instant of the packet data
     * @param data Pointer to data block to send
     * @param len Length of the data block
     * @return True if data sending was attempted
     */
    inline bool rtpSendData(bool marker, unsigned int timestamp,
	const void* data, int len)
	{ return m_send && m_send->rtpSendData(marker,timestamp,data,len); }

    /**
     * Send one RTP event
     * @param event Event code to send
     * @param duration Duration of the event as number of samples
     * @param volume Attenuation of the tone, zero for don't care
     * @param timestamp Sampling instant of the packet data, zero to use current
     * @return True if data sending was attempted
     */
    inline bool rtpSendEvent(int event, int duration, int volume = 0, unsigned int timestamp = 0)
	{ return m_send && m_send->rtpSendEvent(event,duration,volume,timestamp); }

    /**
     * Send one RTP key event
     * @param key Key to send
     * @param duration Duration of the event as number of samples
     * @param volume Attenuation of the tone, zero for don't care
     * @param timestamp Sampling instant of the packet data, zero to use current
     * @return True if data sending was attempted
     */
    inline bool rtpSendKey(char key, int duration, int volume = 0, unsigned int timestamp = 0)
	{ return m_send && m_send->rtpSendKey(key,duration,volume,timestamp); }

    /**
     * Get the RTP/RTCP transport of data handled by this session.
     * @return A pointer to the RTPTransport of this session
     */
    inline RTPTransport* transport() const
	{ return m_transport; }

    /**
     * Set the RTP/RTCP transport of data handled by this session
     * @param trans A pointer to the new RTPTransport for this session
     */
    void transport(RTPTransport* trans);

    /**
     * Get the RTP/RTCP sender of this session
     * @return A pointer to the RTPSender of this session
     */
    inline RTPSender* sender() const
	{ return m_send; }

    /**
     * Set the RTP/RTCP sender of this session
     * @param send A pointer to the new RTPSender of this session or NULL
     */
    void sender(RTPSender* send);

    /**
     * Get the RTP/RTCP receiver of this session
     * @return A pointer to the RTPReceiver of this session
     */
    inline RTPReceiver* receiver() const
	{ return m_recv; }

    /**
     * Set the RTP/RTCP receiver of this session
     * @param recv A pointer to the new RTPReceiver of this session or NULL
     */
    void receiver(RTPReceiver* recv);

    /**
     * Get the direction of this session
     * @return Session's direction as a Direction enum
     */
    inline Direction direction() const
	{ return m_direction; }

    /**
     * Set the direction of this session. A transport must exist for this
     *  method to succeed.
     * @param dir New Direction for this session
     * @return True if direction was set, false if a failure occured
     */
    bool direction(Direction dir);

    /**
     * Add a direction of this session. A transport must exist for this
     *  method to succeed.
     * @param dir New Direction to add for this session
     * @return True if direction was set, false if a failure occured
     */
    inline bool addDirection(Direction dir)
	{ return direction((Direction)(m_direction | dir)); }

    /**
     * Delete a direction of this session. A transport must exist for this
     *  method to succeed.
     * @param dir Direction to remove for this session
     * @return True if direction was set, false if a failure occured
     */
    inline bool delDirection(Direction dir)
	{ return direction((Direction)(m_direction & ~dir)); }

    /**
     * Set the data payload type for both receiver and sender.
     * @param type Payload type, -1 to disable
     * @return True if changed, false if invalid payload type
     */
    bool dataPayload(int type);

    /**
     * Set the event payload type for both receiver and sender.
     * @param type Payload type, -1 to disable
     * @return True if changed, false if invalid payload type
     */
    bool eventPayload(int type);

    /**
     * Set the silence payload type for both receiver and sender.
     * @param type Payload type, -1 to disable
     * @return True if changed, false if invalid payload type
     */
    bool silencePayload(int type);

    /**
     * Set the local network address of the RTP transport of this session
     * @param addr New local RTP transport address
     * @return True if address set, false if a failure occured
     */
    inline bool localAddr(SocketAddr& addr)
	{ return m_transport && m_transport->localAddr(addr); }

    /**
     * Set the remote network address of the RTP transport of this session
     * @param addr New remote RTP transport address
     * @param sniff Automatically adjust the address from the first incoming packet
     * @return True if address set, false if a failure occured
     */
    inline bool remoteAddr(SocketAddr& addr, bool sniff = false)
	{ return m_transport && m_transport->remoteAddr(addr,sniff); }

    /**
     * Set the Type Of Service for the RTP transport socket
     * @param tos Type Of Service bits to set
     * @return True if operation was successfull, false if an error occured
     */
    inline bool setTOS(int tos)
	{ return m_transport && m_transport->setTOS(tos); }

protected:
    /**
     * Method called periodically to push any asynchronous data or statistics
     * @param when Time to use as base in all computing
     */
    virtual void timerTick(const Time& when);

private:
    RTPTransport* m_transport;
    Direction m_direction;
    RTPSender* m_send;
    RTPReceiver* m_recv;
};

}

#endif /* __YATERTP_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
