/**
 * yatertp.h
 * Yet Another RTP Stack
 * This file is part of the YATE Project http://YATE.null.ro
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
class RTPReceiver;
class RTPSecure;

/**
 * A base class that contains just placeholders to process raw RTP and RTCP packets.
 * @short Base class to ease creation of RTP forwarders
 */
class YRTP_API RTPProcessor : public GenObject
{
    friend class UDPSession;
    friend class UDPTLSession;
    friend class RTPGroup;
    friend class RTPTransport;
    friend class RTPSender;
    friend class RTPReceiver;

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
    virtual void rtpData(const void* data, int len);

    /**
     * This method is called to send or process a RTCP packet
     * @param data Pointer to raw RTCP data
     * @param len Length of the data packet
     */
    virtual void rtcpData(const void* data, int len);

    /**
     * Retrieve MGCP P: style comma separated session parameters
     * @param stats String to append parameters to
     */
    virtual void getStats(String& stats) const;

    /**
     * Increase the counter for number of RTP packets received from a wrong source
     */
    virtual inline void incWrongSrc()
	{  }

    /**
     * Get the number of RTP packets that were received from a wrong source
     * @return Number of RTP packets received from a wrong source
     */
    inline unsigned int wrongSrc()
	{ return m_wrongSrc; }

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

    unsigned int m_wrongSrc;

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
     * @param msec Minimum time to sleep in loop in milliseconds
     * @param prio Thread priority to run this group
     */
    RTPGroup(int msec = 0, Priority prio = Normal);

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

    /**
     * Set the system global minimum time to sleep in loop
     * @param msec Minimum time to sleep in loop in milliseconds
     */
    static void setMinSleep(int msec);

    /**
     * Add a RTP processor to this group
     * @param proc Pointer to the RTP processor to add
     */
    void join(RTPProcessor* proc);

    /**
     * Remove a RTP processor from this group
     * @param proc Pointer to the RTP processor to remove
     */
    void part(RTPProcessor* proc);

private:
    ObjList m_processors;
    bool m_listChanged;
    unsigned long m_sleep;
};

/**
 * Class that holds sockets and addresses for transporting RTP and RTCP packets.
 * @short Low level transport for RTP and RTCP
 */
class YRTP_API RTPTransport : public RTPProcessor
{
public:
    /**
     * Activation status of the transport
     */
    enum Activation {
	Inactive,
	Bound,
	Active
    };

    /**
     * Type of transported data
     */
    enum Type {
	Unknown,
	RTP,
	UDPTL
    };

    /**
     * Constructor, creates an unconnected transport
     * @param type Type of check to apply to the data
     */
    RTPTransport(Type type = RTP);

    /**
     * Destructor
     */
    virtual ~RTPTransport();

    /**
     * Destroys the object, disposes the memory. Do not call delete directly.
     */
    virtual void destruct();

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
     * @param rtcp Enable RTCP transport
     * @return True if address set, false if a failure occured
     */
    bool localAddr(SocketAddr& addr, bool rtcp = true);

    /**
     * Set the remote network address of the RTP transport
     * @param addr New remote RTP transport address
     * @param sniff Automatically adjust the address from the first incoming packet
     * @return True if address set, false if a failure occured
     */
    bool remoteAddr(SocketAddr& addr, bool sniff = false);

    /**
     * Set the size of the operating system's buffers for the RTP and RTCP sockets
     * @param bufLen Requested length of the buffer
     * @return True if the buffer length was set
     */
    bool setBuffer(int bufLen = 4096);

    /**
     * Set the Type Of Service for the RTP socket
     * @param tos Type Of Service bits to set
     * @return True if operation was successfull, false if an error occured
     */
    inline bool setTOS(int tos)
	{ return m_rtpSock.setTOS(tos); }

    /**
     * Get the RTP socket used by this transport
     * @return Pointer to the RTP socket
     */
    inline Socket* rtpSock()
	{ return &m_rtpSock; }

    /**
     * Get the RTCP socket used by this transport
     * @return Pointer to the RTCP socket
     */
    inline Socket* rtcpSock()
	{ return &m_rtcpSock; }

    /**
     * Drill a hole in a firewall or NAT for the RTP and RTCP sockets
     * @return True if at least a packet was sent for the RTP socket
     */
    bool drillHole();

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
    Type m_type;
    RTPProcessor* m_processor;
    RTPProcessor* m_monitor;
    Socket m_rtpSock;
    Socket m_rtcpSock;
    SocketAddr m_localAddr;
    SocketAddr m_remoteAddr;
    SocketAddr m_remoteRTCP;
    SocketAddr m_remotePref;
    SocketAddr m_rxAddrRTP;
    SocketAddr m_rxAddrRTCP;
    bool m_autoRemote;
    bool m_warnSendErrorRtp;
    bool m_warnSendErrorRtcp;
};

/**
 * A dejitter buffer that can be inserted in the receive data path to
 *  absorb variations in packet arrival time. Incoming packets are stored
 *  and forwarded at fixed intervals.
 * @short Dejitter buffer for incoming data packets
 */
class YRTP_API RTPDejitter : public RTPProcessor
{
public:
    /**
     * Constructor of a new jitter attenuator
     * @param receiver RTP receiver which gets the delayed packets
     * @param mindelay Minimum length of the dejitter buffer in microseconds
     * @param maxdelay Maximum length of the dejitter buffer in microseconds
     */
    RTPDejitter(RTPReceiver* receiver, unsigned int mindelay, unsigned int maxdelay);

    /**
     * Destructor - drops the packets and shows statistics
     */
    virtual ~RTPDejitter();

    /**
     * Process and store one RTP data packet
     * @param marker True if the marker bit is set in data packet
     * @param payload Payload number
     * @param timestamp Sampling instant of the packet data
     * @param data Pointer to data block to process
     * @param len Length of the data block in bytes
     * @return True if the data packet was queued
     */
    virtual bool rtpRecv(bool marker, int payload, unsigned int timestamp,
	const void* data, int len);

    /**
     * Clear the delayed packets queue and all variables
     */
    void clear();

protected:
    /**
     * Method called periodically to keep the data flowing
     * @param when Time to use as base in all computing
     */
    virtual void timerTick(const Time& when);

private:
    ObjList m_packets;
    RTPReceiver* m_receiver;
    unsigned int m_minDelay;
    unsigned int m_maxDelay;
    unsigned int m_headStamp;
    unsigned int m_tailStamp;
    u_int64_t m_headTime;
    u_int64_t m_sampRate;
    unsigned char m_fastRate;
};

/**
 * Base class that holds common sender and receiver methods
 * @short Common send/recv variables holder
 */
class YRTP_API RTPBaseIO
{
    friend class RTPSession;
    friend class RTPSecure;
public:
    /**
     * Default constructor.
     */
    inline RTPBaseIO(RTPSession* session = 0)
	: m_session(session), m_secure(0),
	  m_ssrcInit(true), m_ssrc(0), m_ts(0),
	  m_seq(0), m_rollover(0), m_secLen(0), m_mkiLen(0),
	  m_evTs(0), m_evNum(-1), m_evVol(-1),
	  m_ioPackets(), m_ioOctets(0), m_tsLast(0),
	  m_dataType(-1), m_eventType(-1), m_silenceType(-1)
	{ }

    /**
     * Destructor
     */
    virtual ~RTPBaseIO();

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
     * Return SSRC value, initialize to a new, random value if needed
     * @return Current value of SSRC
     */
    unsigned int ssrcInit();

    /**
     * Requesting generation/grabbing of a new SSRC
     */
    inline void reset()
	{ m_ssrcInit = true; }

    /**
     * Get the value of the current SSRC, zero if not initialized yet
     * @return Value of SSRC, zero if not initialized
     */
    inline unsigned int ssrc() const
	{ return m_ssrcInit ? 0 : m_ssrc; }

    /**
     * Force a new known SSRC for all further packets
     */
    inline void ssrc(unsigned int src)
	{ m_ssrc = src; m_ssrcInit = false; }

    /**
     * Get the current sequence number
     * @return Sequence number
     */
    inline u_int16_t seq() const
	{ return m_seq; }

    /**
     * Get the value of the rollover counter
     * @return How many times the seqeunce has rolled over since SSRC changed
     */
    inline u_int32_t rollover() const
	{ return m_rollover; }

    /**
     * Get the full current sequence number including rollovers
     * @return Full 48 bit current sequence number
     */
    inline u_int64_t fullSeq() const
	{ return m_seq | (((u_int64_t)m_rollover) << 16); }

    /**
     * Retrieve the number of packets exchanged on current session
     * @return Number of packets exchanged
     */
    inline u_int32_t ioPackets() const
	{ return m_ioPackets; }

    /**
     * Retrieve the number of payload octets exchanged on current session
     * @return Number of octets exchanged except headers and padding
     */
    inline u_int32_t ioOctets() const
	{ return m_ioOctets; }

    /**
     * Get the timestamp of the last packet as transmitted over the wire
     * @return Timestamp of last packet sent or received
     */
    inline unsigned int tsLast() const
	{ return m_ts + m_tsLast; }

    /**
     * Get the session this object belongs to
     * @return Pointer to RTP session or NULL
     */
    inline RTPSession* session() const
	{ return m_session; }

    /**
     * Get the security provider of this sender or receiver
     * @return A pointer to the RTPSecure or NULL
     */
    inline RTPSecure* security() const
	{ return m_secure; }

    /**
     * Set the security provider of this sender or receiver
     * @param secure Pointer to the new RTPSecure or NULL
     */
    void security(RTPSecure* secure);

protected:
    /**
     * Method called periodically to keep the data flowing
     * @param when Time to use as base in all computing
     */
    virtual void timerTick(const Time& when) = 0;

    /**
     * Set the length of the added / expected security info block
     * @param len Length of security information portion
     * @param key Length of master key identifier
     */
    inline void secLength(u_int32_t len, u_int32_t key = 0)
	{ m_secLen = len; m_mkiLen = key; }

    RTPSession* m_session;
    RTPSecure* m_secure;
    bool m_ssrcInit;
    u_int32_t m_ssrc;
    u_int32_t m_ts;
    u_int16_t m_seq;
    u_int32_t m_rollover;
    u_int16_t m_secLen;
    u_int16_t m_mkiLen;
    u_int32_t m_evTs;
    int m_evNum;
    int m_evVol;
    u_int32_t m_ioPackets;
    u_int32_t m_ioOctets;
    unsigned int m_tsLast;

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
    friend class RTPDejitter;
public:
    /**
     * Constructor
     */
    inline RTPReceiver(RTPSession* session = 0)
	: RTPBaseIO(session),
	  m_ioLostPkt(0), m_dejitter(0),
	  m_seqSync(0), m_seqCount(0), m_warn(true), m_warnSeq(1),
	  m_seqLost(0), m_wrongSSRC(0), m_syncLost(0)
	{ }

    /**
     * Destructor - gets rid of the jitter buffer if present
     */
    virtual ~RTPReceiver();

    /**
     * Retrieve the number of lost packets in current session
     * @return Number of packets in sequence gaps
     */
    inline u_int32_t ioPacketsLost() const
	{ return m_ioLostPkt; }


    /**
     * Set a new dejitter buffer in this receiver
     * @param dejitter New dejitter buffer to set, NULL to remove
     */
    void setDejitter(RTPDejitter* dejitter);

    /**
     * Allocate and set a new dejitter buffer in this receiver
     * @param mindelay Minimum length of the dejitter buffer in microseconds
     * @param maxdelay Maximum length of the dejitter buffer in microseconds
     */
    inline void setDejitter(unsigned int mindelay, unsigned int maxdelay)
	{ setDejitter(new RTPDejitter(this,mindelay,maxdelay)); }

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
    * @param marker True if marker bit is set in the RTP packet
    */
    virtual void rtpNewSSRC(u_int32_t newSsrc, bool marker);

    /**
     * Retrieve the statistical data from this receiver in a NamedList. Reset all the data.
     * @param stat NamedList to populate with the values for different counters
     */
    virtual void stats(NamedList& stat) const;

protected:
    /**
     * Method called periodically to finish lingering events
     * @param when Time to use as base in all computing
     */
    virtual void timerTick(const Time& when);

    /**
     * Method called to decipher RTP data in-place.
     * The default implementation calls session's @ref RTPSecure::rtpDecipher()
     * @param data Pointer to data block to decipher
     * @param len Length of data including any padding
     * @param secData Pointer to security data if applicable
     * @param ssrc SSRC of the packet to decipher
     * @param seq Full (48 bit) seqence number of the packet including rollovers
     * @return True is the packet was deciphered correctly or can't tell
     */
    virtual bool rtpDecipher(unsigned char* data, int len, const void* secData, u_int32_t ssrc, u_int64_t seq);

    /**
     * Method called to check the integrity of the RTP packet.
     * The default implementation calls session's @ref RTPSecure::rtpCheckIntegrity()
     * @param data Pointer to RTP header and data
     * @param len Length of header, data and padding
     * @param authData Pointer to authentication data
     * @param ssrc SSRC of the packet to validate
     * @param seq Full (48 bit) seqence number of the packet including rollovers
     * @return True is the packet passed integrity checks
     */
    virtual bool rtpCheckIntegrity(const unsigned char* data, int len, const void* authData, u_int32_t ssrc, u_int64_t seq);

    u_int32_t m_ioLostPkt;

private:
    void rtpData(const void* data, int len);
    void rtcpData(const void* data, int len);
    bool decodeEvent(bool marker, unsigned int timestamp, const void* data, int len);
    bool decodeSilence(bool marker, unsigned int timestamp, const void* data, int len);
    void finishEvent(unsigned int timestamp);
    bool pushEvent(int event, int duration, int volume, unsigned int timestamp);
    RTPDejitter* m_dejitter;
    u_int16_t m_seqSync;
    u_int16_t m_seqCount;
    bool m_warn;
    int m_warnSeq;                       // Warn on invalid sequence (1: DebugWarn, -1: DebugInfo)
    unsigned int m_seqLost;
    unsigned int m_wrongSSRC;
    unsigned int m_syncLost;
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
     * @param session RTP session the sender belongs
     * @param randomTs Initialize a random timestamp offset
     */
    RTPSender(RTPSession* session = 0, bool randomTs = true);

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


    /**
     * Get the payload padding size
     * @return Chunk size to pad the payload to a multiple of
     */
    inline int padding() const
	{ return m_padding; }

    /**
     * Set the padding to a multiple of a data chunk
     * @param chunk Size to pad the payload to a multiple of
     * @return True if the new chunk size is valid
     */
    bool padding(int chunk);

    /**
     * Retrieve the statistical data from this receiver in a NamedList. Reset all the data.
     * @param stat NamedList to populate with the values for different counters
     */
    virtual void stats(NamedList& stat) const;

protected:
    /**
     * Method called periodically to send events and buffered data
     * @param when Time to use as base in all computing
     */
    virtual void timerTick(const Time& when);

    /**
     * Method called to encipher RTP payload data in-place.
     * The default implementation calls session's @ref RTPSecure::rtpEncipher()
     * @param data Pointer to data block to encipher
     * @param len Length of payload data to be encrypted including any padding
     */
    virtual void rtpEncipher(unsigned char* data, int len);

    /**
     * Method called to add integrity information to the RTP packet.
     * The default implementation calls session's @ref RTPSecure::rtpAddIntegrity()
     * @param data Pointer to the RTP packet to protect
     * @param len Length of RTP data to be encrypted including header and padding
     * @param authData Address to write the integrity data to
     */
    virtual void rtpAddIntegrity(const unsigned char* data, int len, unsigned char* authData);


private:
    int m_evTime;
    unsigned char m_padding;
    DataBlock m_buffer;
    bool sendEventData(unsigned int timestamp);
};

/**
 * A base class for RTP, SRTP or UDPTL sessions
 * @short RTP or UDPTL session
 */
class YRTP_API UDPSession : public RTPProcessor
{
public:
    /**
     * Destructor - cleans up any remaining resources
     */
    virtual ~UDPSession();

    /**
     * Create a new RTP or UDP transport for this session.
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
     * @param msec Minimum time to sleep in group loop in milliseconds
     * @param prio Thread priority to run the new group
     * @return True if initialized, false on some failure
     */
    bool initGroup(int msec = 0, Thread::Priority prio = Thread::Normal);

    /**
     * Set the remote network address of the RTP transport of this session
     * @param addr New remote RTP transport address
     * @param sniff Automatically adjust the address from the first incoming packet
     * @return True if address set, false if a failure occured
     */
    inline bool remoteAddr(SocketAddr& addr, bool sniff = false)
	{ return m_transport && m_transport->remoteAddr(addr,sniff); }

    /**
     * Set the size of the operating system's buffers for the RTP and RTCP transport sockets
     * @param bufLen Requested length of the buffer
     * @return True if the buffer length was set
     */
    inline bool setBuffer(int bufLen = 4096)
	{ return m_transport && m_transport->setBuffer(bufLen); }

    /**
     * Set the Type Of Service for the RTP transport socket
     * @param tos Type Of Service bits to set
     * @return True if operation was successfull, false if an error occured
     */
    inline bool setTOS(int tos)
	{ return m_transport && m_transport->setTOS(tos); }

    /**
     * Get the main transport socket used by this session
     * @return Pointer to the RTP or UDPTL socket, NULL if no transport exists
     */
    inline Socket* rtpSock()
	{ return m_transport ? m_transport->rtpSock() : 0; }

    /**
     * Drill a hole in a firewall or NAT for the RTP and RTCP sockets
     * @return True if at least a packet was sent for the RTP socket
     */
    inline bool drillHole()
	{ return m_transport && m_transport->drillHole(); }

    /**
     * Set the interval until receiver timeout is detected
     * @param interval Milliseconds until receiver times out, zero to disable
     */
    void setTimeout(int interval);

    /**
     * Get the RTP/RTCP transport of data handled by this session.
     * @return A pointer to the RTPTransport of this session
     */
    inline RTPTransport* transport() const
	{ return m_transport; }

    /**
     * Set the UDP transport of data handled by this session
     * @param trans A pointer to the new RTPTransport for this session
     */
    virtual void transport(RTPTransport* trans);

protected:
    /**
     * Default constructor
     */
    UDPSession();

    /**
     * Method called when the receiver timed out
     * @param initial True if no packet was ever received in this session
     */
    virtual void timeout(bool initial);

    RTPTransport* m_transport;
    u_int64_t m_timeoutTime;
    u_int64_t m_timeoutInterval;
};

/**
 * An unidirectional or bidirectional RTP session
 * @short Full RTP session
 */
class YRTP_API RTPSession : public UDPSession, public Mutex
{
public:
    /**
     * Direction of the session
     */
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
     * Retrieve MGCP P: style comma separated session parameters
     * @param stats String to append parameters to
     */
    virtual void getStats(String& stats) const;

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
    * @param marker True if marker bit is set in the RTP packet
    */
    virtual void rtpNewSSRC(u_int32_t newSsrc, bool marker);

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
     * Create a cipher when required for SRTP
     * @param name Name of the cipher to create
     * @param dir Direction the cipher must be able to handle
     * @return Pointer to newly allocated Cipher or NULL
     */
    virtual Cipher* createCipher(const String& name, Cipher::Direction dir);

    /**
     * Check if a cipher is supported for SRTP
     * @param name Name of the cipher to check
     * @return True if the specified cipher is supported
     */
    virtual bool checkCipher(const String& name);

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
	{ Lock lck(this); return m_send && m_send->rtpSend(marker,payload,timestamp,data,len); }

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
	{ Lock lck(this); return m_send && m_send->rtpSendData(marker,timestamp,data,len); }

    /**
     * Send one RTP event
     * @param event Event code to send
     * @param duration Duration of the event as number of samples
     * @param volume Attenuation of the tone, zero for don't care
     * @param timestamp Sampling instant of the packet data, zero to use current
     * @return True if data sending was attempted
     */
    inline bool rtpSendEvent(int event, int duration, int volume = 0, unsigned int timestamp = 0)
	{ Lock lck(this); return m_send && m_send->rtpSendEvent(event,duration,volume,timestamp); }

    /**
     * Send one RTP key event
     * @param key Key to send
     * @param duration Duration of the event as number of samples
     * @param volume Attenuation of the tone, zero for don't care
     * @param timestamp Sampling instant of the packet data, zero to use current
     * @return True if data sending was attempted
     */
    inline bool rtpSendKey(char key, int duration, int volume = 0, unsigned int timestamp = 0)
	{ Lock lck(this); return m_send && m_send->rtpSendKey(key,duration,volume,timestamp); }

    /**
     * Retrieve the number of lost packets in current received
     * @return Number of packets in sequence gaps
     */
    inline u_int32_t ioPacketsLost() const
	{ return m_recv ? m_recv->ioPacketsLost() : 0; }

    /**
     * Get the payload padding size
     * @return Chunk size to pad the payload to a multiple of
     */
    inline int padding() const
	{ return m_send ? m_send->padding() : 0; }

    /**
     * Set the padding to a multiple of a data chunk
     * @param chunk Size to pad the payload to a multiple of
     * @return True if the new chunk size is valid
     */
    inline bool padding(int chunk)
	{ return m_send && m_send->padding(chunk); }

    /**
     * Allocate and set a new dejitter buffer for the receiver in the session
     * @param mindelay Minimum length of the dejitter buffer in microseconds
     * @param maxdelay Maximum length of the dejitter buffer in microseconds
     */
    inline void setDejitter(unsigned int mindelay = 20, unsigned int maxdelay = 50)
	{ if (m_recv) m_recv->setDejitter(mindelay,maxdelay); }

    /**
     * Set the RTP/RTCP transport of data handled by this session
     * @param trans A pointer to the new RTPTransport for this session
     */
    virtual void transport(RTPTransport* trans);

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
     * @param rtcp Enable RTCP in this session
     * @return True if address set, false if a failure occured
     */
    inline bool localAddr(SocketAddr& addr, bool rtcp = true)
	{ Lock lck(this); return m_transport && m_transport->localAddr(addr,rtcp); }

    /**
     * Get the stored security provider or of the sender
     * @return A pointer to the RTPSecure or NULL
     */
    inline RTPSecure* security() const
	{ return m_send ? m_send->security() : m_secure; }

    /**
     * Store a security provider for the sender
     * @param secure Pointer to the new RTPSecure or NULL
     */
    void security(RTPSecure* secure);

    /**
     * Set the RTCP report interval
     * @param interval Average interval between reports in msec, zero to disable
     */
    void setReports(int interval);

    /**
     * Put the collected statistical data
     * @param stats NamedList to populate with the data
     */
    virtual void getStats(NamedList& stats) const;

    /**
     * Increase the counter for number of RTP packets received from a wrong source
     */
    virtual void incWrongSrc();

    /**
     * Set the packet with invalid sequence warn mode
     * @param on True to show a message at DebugWarn level,
     *  false to show at DebugInfo level
     */
    inline void setWarnSeq(bool on)
	{ m_warnSeq = on ? 1 : -1; }

protected:
    /**
     * Method called periodically to push any asynchronous data or statistics
     * @param when Time to use as base in all computing
     */
    virtual void timerTick(const Time& when);

    /**
     * Send a RTCP report
     * @param when Time to use as base for timestamps
     */
    void sendRtcpReport(const Time& when);

    /**
     * Send a RTCP BYE when the sender is stopped or replaced
     */
    void sendRtcpBye();

private:
    Direction m_direction;
    RTPSender* m_send;
    RTPReceiver* m_recv;
    RTPSecure* m_secure;
    u_int64_t m_reportTime;
    u_int64_t m_reportInterval;
    int m_warnSeq;                       // Warn on invalid sequence (1: DebugWarn, -1: DebugInfo)
};

/**
 * A bidirectional UDPTL session usable for T.38
 * @short UDPTL session
 */
class YRTP_API UDPTLSession : public UDPSession, public Mutex
{
public:
    /**
     * Destructor
     */
    ~UDPTLSession();

    /**
     * Set the local network address of the RTP transport of this session
     * @param addr New local RTP transport address
     * @return True if address set, false if a failure occured
     */
    inline bool localAddr(SocketAddr& addr)
	{ Lock lck(this); return m_transport && m_transport->localAddr(addr,false); }

    /**
     * Get the maximum UDPTL packet length
     * @return Maximum length of UDPTL packet length in bytes
     */
    inline u_int16_t maxLen() const
	{ return m_maxLen; }

    /**
     * Get the maximum number of UDPTL secondary IFPs
     * @return Maximum number of secondary IFPs, zero if disabled
     */
    inline u_int8_t maxSec() const
	{ return m_maxSec; }

    /**
     * This method is called to send or process an UDPTL packet
     * @param data Pointer to raw UDPTL data
     * @param len Length of the data packet
     */
    virtual void rtpData(const void* data, int len);

    /**
     * Send UDPTL data over the transport, add older blocks for error recovery
     * @param data Pointer to IFP block to send as primary
     * @param len Length of primary IFP block
     * @param seq Sequence number to incorporate in message
     * @return True if data block was sent, false if an error occured
     */
    bool udptlSend(const void* data, int len, u_int16_t seq);

protected:
    /**
     * UDPTL Session constructor
     * @param maxLen Maximum length of UDPTL packet, at least longest primary IFP + 5 bytes
     * @param maxSec Maximum number of secondary IFPs, set to zero to disable
     */
    UDPTLSession(u_int16_t maxLen = 250, u_int8_t maxSec = 2);

    /**
     * Method called periodically to push any asynchronous data or statistics
     * @param when Time to use as base in all computing
     */
    virtual void timerTick(const Time& when);

    /**
     * Create a new UDPTL transport for this session.
     * Override this method to create objects derived from RTPTransport.
     * @return Pointer to the new transport or NULL on failure
     */
    virtual RTPTransport* createTransport();

    /**
     * Method called when UDPTL data is received
     * @param data Pointer to IFP block
     * @param len Length of the IFP block
     * @param seq Sequence number of the block
     * @param recovered True if the IFP block was recovered after data loss
     */
    virtual void udptlRecv(const void* data, int len, u_int16_t seq, bool recovered) = 0;

private:
    void recoverSec(const unsigned char* data, int len, u_int16_t seq, int nSec);
    u_int16_t m_rxSeq;
    u_int16_t m_txSeq;
    u_int16_t m_maxLen;
    u_int8_t m_maxSec;
    bool m_warn;
    ObjList m_txQueue;
};

/**
 * Security and integrity implementation
 * @short SRTP implementation
 */
class YRTP_API RTPSecure : public GenObject
{
    friend class RTPReceiver;
    friend class RTPSender;
    friend class RTPSession;
public:
    /**
     * Default constructor, builds an inactive implementation
     */
    RTPSecure();

    /**
     * Constructor that creates an active implementation
     * @param suite Cryptographic suite to use by default
     */
    RTPSecure(const String& suite);

    /**
     * Constructor that copies the basic crypto lengths
     * @param other Security provider to copy parameters from
     */
    RTPSecure(const RTPSecure& other);

    /**
     * Destructor
     */
    virtual ~RTPSecure();

    /**
     * Get the owner of this security instance
     * @return Pointer to RTPBaseIO or NULL
     */
    inline RTPBaseIO* owner() const
	{ return m_owner; }

    /**
     * Set the owner of this security instance
     * @param newOwner Pointer to new RTPBaseIO owning this security instance
     */
    void owner(RTPBaseIO* newOwner);

    /**
     * Get the current RTP cipher if set
     * @return Pointer to current RTP cipher or NULL
     */
    inline Cipher* rtpCipher() const
	{ return m_rtpCipher; }

    /**
     * Check if the systems supports requirements for activating SRTP
     * @param session RTP session to use for cipher checking, NULL to use owner session
     * @return True if it looks like SRTP can be activated later
     */
    virtual bool supported(RTPSession* session = 0) const;

    /**
     * Set up the cryptographic parameters
     * @param suite Descriptor of the encryption and authentication algorithms
     * @param keyParams Keying material and related parameters
     * @param paramList Optional session parameters as list of Strings
     * @return True if the session parameters were applied successfully
     */
    virtual bool setup(const String& suite, const String& keyParams, const ObjList* paramList = 0);

    /**
     * Create a set of cryptographic parameters
     * @param suite Reference of returned cryptographic suite description
     * @param keyParams Reference to returned keying material
     * @param buildMaster Create random master key and salt if not already set
     * @return True if security instance is valid and ready
     */
    virtual bool create(String& suite, String& keyParams, bool buildMaster = true);

protected:
    /**
     * Initialize security related variables in the RTP session
     */
    virtual void init();

    /**
     * Method called to encipher RTP payload data in-place
     * @param data Pointer to data block to encipher
     * @param len Length of payload data to be encrypted including any padding
     */
    virtual void rtpEncipher(unsigned char* data, int len);

    /**
     * Method called to add integrity information to the RTP packet
     * @param data Pointer to the RTP packet to protect
     * @param len Length of RTP data to be encrypted including header and padding
     * @param authData Address to write the integrity data to
     */
    virtual void rtpAddIntegrity(const unsigned char* data, int len, unsigned char* authData);

    /**
     * Method called to decipher RTP data in-place
     * @param data Pointer to data block to decipher
     * @param len Length of data including any padding
     * @param secData Pointer to security data if applicable
     * @param ssrc SSRC of the packet to decipher
     * @param seq Full (48 bit) seqence number of the packet including rollovers
     * @return True is the packet was deciphered correctly or can't tell
     */
    virtual bool rtpDecipher(unsigned char* data, int len, const void* secData, u_int32_t ssrc, u_int64_t seq);

    /**
     * Method called to check the integrity of the RTP packet
     * @param data Pointer to RTP header and data
     * @param len Length of header, data and padding
     * @param authData Pointer to authentication data
     * @param ssrc SSRC of the packet to validate
     * @param seq Full (48 bit) seqence number of the packet including rollovers
     * @return True is the packet passed integrity checks
     */
    virtual bool rtpCheckIntegrity(const unsigned char* data, int len, const void* authData, u_int32_t ssrc, u_int64_t seq);

    /**
     * Internal method implementing key derivation
     * @param cipher Cipher used for key derivation
     * @param key Reference to derived key output
     * @param len Desired length of the key, should be at most cipher block length
     * @param label Derived key type
     * @param index Packet index after being divided by KDR
     * @return True if success, false if invalid parameters or missing cipher
     */
    bool deriveKey(Cipher& cipher, DataBlock& key, unsigned int len, unsigned char label, u_int64_t index = 0);

private:
    RTPBaseIO* m_owner;
    Cipher* m_rtpCipher;
    DataBlock m_masterKey;
    DataBlock m_masterSalt;
    DataBlock m_cipherKey;
    DataBlock m_cipherSalt;
    SHA1 m_authIpad;
    SHA1 m_authOpad;
    u_int32_t m_rtpAuthLen;
    bool m_rtpEncrypted;
};

}

#endif /* __YATERTP_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
