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

/**
 * A base class that contains just placeholders to process raw RTP and RTCP packets.
 * @short Base class to ease creation of RTP forwarders
 */
class YRTP_API RTPProcessor : public GenObject
{
    friend class RTPGroup;
    friend class RTPTransport;
    friend class RTPSession;

public:
    /**
     * Constructor - inserts itself in a RTP group
     * @param grp RTP group to join
     */
    RTPProcessor(RTPGroup* grp = 0);

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
     * Constructor, creates a transport optionally joined to a group
     * @param grp RTP group to join
     */
    RTPTransport(RTPGroup* grp = 0);

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
     * @return True if address set, false if a failure occured
     */
    bool remoteAddr(SocketAddr& addr);

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
};

/**
 * An unidirectional or bidirectional RTP session
 * @short Full RTP session
 */
class YRTP_API RTPSession : public RTPProcessor
{
public:
    enum Direction {
	FullStop,
	RecvOnly,
	SendOnly,
	SendRecv
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
     * Process one RTP payload packet
     * @param marker Set to true if the marker bit is set
     * @param payload Payload number
     * @param timestamp Sampling instant of the packet data
     * @param data Pointer to data block to process
     * @param len Length of the data block
     * @return True if data was handled
     */
    virtual bool rtpRecv(bool marker, int payload, unsigned int timestamp,
	const void* data, int len);

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
     * Request a resync on the first packet arrived
     */
    inline void resync()
	{ m_sync = true; }

    /**
     * Get the RTP/RTCP transport of data handled by this session
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
     * Set the local network address of the RTP transport of this session
     * @param addr New local RTP transport address
     * @return True if address set, false if a failure occured
     */
    inline bool localAddr(SocketAddr& addr)
	{ return m_transport ? m_transport->localAddr(addr) : false; }

    /**
     * Set the remote network address of the RTP transport of this session
     * @param addr New remote RTP transport address
     * @return True if address set, false if a failure occured
     */
    inline bool remoteAddr(SocketAddr& addr)
	{ return m_transport ? m_transport->remoteAddr(addr) : false; }

protected:
    /**
     * Method called periodically to push any asynchronous data or statistics
     * @param when Time to use as base in all computing
     */
    virtual void timerTick(const Time& when);

    /**
     * This method is called to process a RTP packet
     * @param data Pointer to raw RTP data
     * @param len Length of the data packet
     */
    virtual void rtpData(const void* data, int len);

    /**
     * This method is called to process a RTCP packet
     * @param data Pointer to raw RTCP data
     * @param len Length of the data packet
     */
    virtual void rtcpData(const void* data, int len);

private:
    RTPTransport* m_transport;
    Direction m_direction;
    bool m_sync;
    u_int32_t m_rxSsrc;
    u_int32_t m_rxTs;
    u_int16_t m_rxSeq;
    u_int32_t m_txSsrc;
    u_int32_t m_txTs;
    u_int16_t m_txSeq;
};

}

#endif /* __YATERTP_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
