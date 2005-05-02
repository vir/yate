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

namespace TelEngine {

/**
 * A base class that contains just placeholders to process raw RTP and RTCP packets.
 * @short Base class to ease creation of RTP forwarders
 */
class YRTP_API RTPProcessor : public GenObject
{
public:
    /**
     * Do-nothing constructor
     */
    inline RTPProcessor()
	{ }

    /**
     * Do-nothing destructor
     */
    inline ~RTPProcessor()
	{ }

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

    RTPTransport();

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

    /**
     * Set the RTP/RTCP processor of data received by this transport
     * @param processor A pointer to the RTPProcessor for this transport
     */
    void setProcessor(RTPProcessor* processor);

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
    RTPProcessor* m_processor;
    Socket m_rtpSock;
    Socket m_rtcpSock;
    SocketAddr m_localAddr;
    SocketAddr m_remoteAddr;
    SocketAddr m_remoteRTCP;
};

class YRTP_API RTPSession : public RTPProcessor
{
public:
    enum Direction {
	FullStop,
	RecvOnly,
	SendOnly,
	SendRecv
    };

    RTPSession();

    virtual ~RTPSession();

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

protected:
    RTPTransport* m_transport;
};

}

#endif /* __YATERTP_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
