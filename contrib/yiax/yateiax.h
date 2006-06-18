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

class IAXInfoElement;
class IAXInfoElementString;
class IAXInfoElementNumeric;
class IAXInfoElementBinary;
class IAXFullFrame;
class IAXEvent;
class IAXEngine;

#define IAX_PROTOCOL_VERSION         0x0002           /* Protocol version */

#define IAX2_MAX_CALLNO              32767            /* Max call number value */
#define IAX2_MAX_TRANSINFRAMELIST    127              /* Max transaction frame list */

/**
 * This class holds a single Information Element with no data
 * @short A single IAX2 Information Element
 */
class YIAX_API IAXInfoElement : public RefObject
{
public:
    enum Type {                  // Type     Length     Note
	textframe = 0x00,					// Generated for IAXFrame::Text
        CALLED_NUMBER = 0x01,    // Text     -          -
        CALLING_NUMBER = 0x02,   // Text     -          -
        CALLING_ANI = 0x03,      // Text     -          -
        CALLING_NAME = 0x04,     // Text     -          -
        CALLED_CONTEXT = 0x05,   // Text     -          -
        USERNAME = 0x06,         // Text     -          -
        PASSWORD = 0x07,         // Text     -          -
        CAPABILITY = 0x08,       // DW       4          Mask
        FORMAT = 0x09,           // DW       4          -
        LANGUAGE = 0x0a,         // Text     -          -
        VERSION = 0x0b,          // W        2          Value: 0x0002
        ADSICPE = 0x0c,          // W        2          -
        DNID = 0x0d,             // Text     -          -
        AUTHMETHODS = 0x0e,      // W        2          Mask
        CHALLENGE = 0x0f,        // Text     -          -
        MD5_RESULT = 0x10,       // Text     -          -
        RSA_RESULT = 0x11,       // Text     -          -
        APPARENT_ADDR = 0x12,    // BIN      -          -
        REFRESH = 0x13,          // W        2          -
        DPSTATUS = 0x14,         // W        2          Mask
        CALLNO = 0x15,           // W        2          Max value: IAX2_MAX_CALLNO
        CAUSE = 0x16,            // Text     -          -
        IAX_UNKNOWN = 0x17,      // B        1          -
        MSGCOUNT = 0x18,         // W        2          Format
        AUTOANSWER = 0x19,       //
        MUSICONHOLD = 0x1a,      // Text     -          Optional
        TRANSFERID = 0x1b,       // DW       4          -
        RDNIS = 0x1c,            // Text     -          -
        PROVISIONING = 0x1d,     // BIN      -          -
        AESPROVISIONING = 0x1e,  // BIN      -          -
        DATETIME = 0x1f,         // DW       4          Format
        DEVICETYPE = 0x20,       // Text     -          -
        SERVICEIDENT = 0x21,     // BIN      6          -
        FIRMWAREVER = 0x22,      // W        2          -
        FWBLOCKDESC = 0x23,      // DW       4          -
        FWBLOCKDATA = 0x24,      // BIN      -          Length = 0  :   END
        PROVVER = 0x25,          // DW       4          -
        CALLINGPRES = 0x26,      // B        1          -
        CALLINGTON = 0x27,       // B        1          -
        CALLINGTNS = 0x28,       // W        2          Format
        SAMPLINGRATE = 0x29,     // DW       4          -
        CAUSECODE = 0x2a,        // B        1          -
        ENCRYPTION = 0x2b,       // B        1          Mask
        ENKEY = 0x2c,            // BIN      -          -
        CODEC_PREFS = 0x2d,      // Text     -          LIST
        RR_JITTER = 0x2e,        // DW       4          -
        RR_LOSS = 0x2f,          // DW       4          Format
        RR_PKTS = 0x30,          // DW       4          -
        RR_DELAY = 0x31,         // W        2          -
        RR_DROPPED = 0x32,       // DW       4          -
        RR_OOO = 0x33,           // DW       4          -
    };

    /**
     * Constructor
     * @param type Type of the IE
     */
    IAXInfoElement(Type type);

    /**
     * Destructor
     */
    virtual ~IAXInfoElement();

    /**
     * Get the type of this IE
     * @return Type of the IE
     */
    inline Type type() const
        { return m_type; }

    /**
     * Add binary data to buf for packing.
     * @param buf Binary data to add
     */
    virtual void toBuffer(DataBlock& buf);

    /**
     * Get the name of an IE goven the type
     * @param ieCode Numeric code of the IE
     * @return Name of the IE, NULL if unknown
     */
    static const char* ieText(u_int8_t ieCode);

protected:
    /**
     * Type of this IE
     */
    Type m_type;
};

/**
 * This class holds a single text Information Element that is attached to a message
 */
class YIAX_API IAXInfoElementString : public IAXInfoElement
{
public:
    inline IAXInfoElementString(Type type, unsigned char* buf, unsigned char len) : IAXInfoElement(type), m_strData((const char*)buf,(int)len)
        {}

    virtual ~IAXInfoElementString() {}

    inline int length() const
        { return m_strData.length(); }

    inline String& data() 
        { return m_strData; }

    virtual void toBuffer(DataBlock& buf);

protected:
    String m_strData;           /* IE text data */
};

/**
 * This class holds a 1, 2 & 4 byte length Information Element that is attached to a message
 */
class IAXInfoElementNumeric : public IAXInfoElement
{
public:
    IAXInfoElementNumeric(Type type, u_int32_t val, u_int8_t len);
	
    virtual ~IAXInfoElementNumeric() {}	

    inline int length() const
        { return m_length; }

    inline u_int32_t data() const
	{ return m_numericData; }

    virtual void toBuffer(DataBlock& buf);

protected:
    u_int8_t m_length;          /* IE data length */
    u_int32_t m_numericData;    /* IE numeric data */
};

/**
 * This class holds a single binary Information Element that is attached to a message
 */
class YIAX_API IAXInfoElementBinary : public IAXInfoElement
{
public:
    IAXInfoElementBinary(Type type, unsigned char* buf, unsigned char len) : IAXInfoElement(type), m_data(buf,len)
        {}

    virtual ~IAXInfoElementBinary() {}

    inline int length() const
        { return m_data.length(); }

    inline DataBlock& data()
        { return m_data; }

    virtual void toBuffer(DataBlock& buf);

    static IAXInfoElementBinary* packIP(const SocketAddr& addr, bool ipv4 = true);

    static bool unpackIP(SocketAddr& addr, IAXInfoElementBinary* ie);

protected:
    DataBlock m_data;           /* IE binary data */
};

/**
 * This class holds the enumeration values for authentication methods
 */
class YIAX_API IAXAuthMethod
{
public:
    enum Type {
        Text = 1,
        MD5 = 2,
        RSA = 4,
    };
};

/**
 * This class holds the enumeration values for audio and video formats
 * @short Wrapper class for audio and video formats
 */
class YIAX_API IAXFormat 
{
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

    static const char* audioText(u_int8_t audio);

    static const char* videoText(u_int8_t video);

    static TokenDict audioData[];
    static TokenDict videoData[];
};

/**
 * This class holds the enumeration values for IAX control (subclass)
 */
class YIAX_API IAXControl 
{
public:
    enum Type {
        New  = 0x01,
        Ping = 0x02,
        Pong = 0x03,
        Ack = 0x04,
        Hangup = 0x05,
        Reject = 0x06,
        Accept = 0x07,
        AuthReq= 0x08,
        AuthRep = 0x09,
        Inval = 0x0a,
        LagRq = 0x0b,
        LagRp = 0x0c,
        RegReq = 0x0d,
        RegAuth = 0x0e,
        RegAck = 0x0f,
        RegRej = 0x10,
        RegRel = 0x11,
        VNAK = 0x12,
        DpReq = 0x13,
        DpRep = 0x14,
        Dial = 0x15,
        TxReq = 0x16,
        TxCnt = 0x17,
        TxAcc = 0x18,
        TxReady = 0x19,
        TxRel = 0x1a,
        TxRej = 0x1b,
        Quelch = 0x1c,
        Unquelch = 0x1d,
        Poke = 0x1e,
	//Reserved = 0x1f,
        MWI = 0x20,
        Unsupport = 0x21,
        Transfer = 0x22,
        Provision = 0x23,
        FwDownl = 0x24,
        FwData = 0x25,
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
        Noise   = 0x0a,
    };

    /**
     * Constructs an incoming frame
     */
    IAXFrame(Type type, u_int16_t sCallNo, u_int32_t tStamp, bool retrans,
	     const unsigned char* buf, unsigned int len);

    virtual ~IAXFrame();

    inline DataBlock& data()
        { return m_data; }

    inline Type type() const
	{ return m_type; }

    inline bool retrans() const
	{ return m_retrans; }

    bool setRetrans();

    inline u_int16_t sourceCallNo() const
	{ return m_sCallNo; }

    inline u_int32_t timeStamp() const
	{ return m_tStamp; }

//    inline void setTimeStamp(u_int32_t tStamp)
//        { m_tStamp = tStamp; }

    inline u_int32_t subclass() const
	{ return m_subclass; }

    virtual const IAXFullFrame* fullFrame() const;

    /**
     * Parse a received buffer ans returns a IAXFrame pointer if valid
     */
    static IAXFrame* parse(const unsigned char* buf, unsigned int len, IAXEngine* engine = 0, const SocketAddr* addr = 0);

    /**
     * Pack the the value received according to IAX protocol
     * @return The packed subclass or 0 if invalid (>127 && not a power of 2)
     */
    static u_int8_t packSubclass(u_int32_t value);

    /**
     * Unpack the the value received according to IAX protocol
     * @return The unpacked subclass 
     */
    static u_int32_t IAXFrame::unpackSubclass(u_int8_t value);

    /**
     * Gets the IE list of a frame.
     * @return IE list if any. Null pointer and invalid set to true if any invalid IE was found in frame.
     *   Null pointer and invalid set to false if the list is empty.
     */
    static ObjList* getIEList(const IAXFullFrame* frame, bool& invalid);

    /**
     * Create a buffer containing an IE list.
     * @return True on success.
     */
    static bool createIEListBuffer(ObjList* ieList, DataBlock& buf);

    static IAXInfoElement* getIE(ObjList* ieList, IAXInfoElement::Type type);

protected:
    Type m_type;               /* Frame type */
    DataBlock m_data;          /* Frame data if incoming, packed frame for outgoing */
    bool m_retrans;            // Retransmission flag
    u_int16_t m_sCallNo;       // Source call number
    u_int32_t m_tStamp;        // Timestamp
    u_int32_t m_subclass;      // Subclass
};

class YIAX_API IAXFullFrame : public IAXFrame
{
public:
    enum ControlType {
        //TODO: validate codes with asterisk, libiax, rfc-draft
        Hangup = 0x01,
        //Ring = 0x02,
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

    /**
     * Constructs a full frame from an incoming frame
     */
    IAXFullFrame(Type type, u_int32_t subClass, u_int16_t sCallNo, u_int16_t dCallNo,
		 unsigned char oSeqNo, unsigned char iSeqNo,
		 u_int32_t tStamp, bool retrans,
		 const unsigned char* buf, unsigned int len);

    /**
     * Constructs a full frame for outgoing (m_data will contain the entire frame)
     */
    IAXFullFrame(Type type, u_int32_t subClass, u_int16_t sCallNo, u_int16_t dCallNo,
		 unsigned char oSeqNo, unsigned char iSeqNo,
		 u_int32_t tStamp,
		 const unsigned char* buf = 0, unsigned int len = 0);

    virtual ~IAXFullFrame();

    inline u_int16_t destCallNo() const
        { return m_dCallNo; }

    inline unsigned char oSeqNo() const
        { return m_oSeqNo; }

    inline unsigned char iSeqNo() const
        { return m_iSeqNo; }

    virtual const IAXFullFrame* fullFrame() const;

private:
    u_int16_t m_dCallNo;      // Destination call number
    unsigned char m_oSeqNo;   // Out sequence number
    unsigned char m_iSeqNo;   // In sequence number
};

class YIAX_API IAXFrameOut : public IAXFullFrame
{
public:

    inline IAXFrameOut(Type type, u_int32_t subClass, u_int16_t sCallNo, u_int16_t dCallNo,
                       unsigned char oSeqNo, unsigned char iSeqNo, u_int32_t tStamp, const unsigned char* buf, unsigned int len,
                       u_int16_t retransCount, u_int32_t retransInterval, bool ackOnly)
        : IAXFullFrame(type,subClass,sCallNo,dCallNo,oSeqNo,iSeqNo,tStamp,buf,len),
          m_ack(false), m_ackOnly(ackOnly), m_retransCount(retransCount), m_retransTimeInterval(retransInterval),
	  m_nextTransTime(Time::msecNow() + m_retransTimeInterval)
	{}

    virtual ~IAXFrameOut()
	{}
	
    inline bool timeout() const
        { return !(bool)m_retransCount; }
	
    inline bool needRetrans(u_int64_t time)
        { return !ack() && time > m_nextTransTime; }

    inline void transmitted() {
            if (m_retransCount) {
                m_retransCount--;
                m_retransTimeInterval *= 2;
		m_nextTransTime += m_retransTimeInterval;
	    }
	}
	
    inline bool ack()
	{ return m_ack; }

    inline void setAck()
	{ m_ack = true; }

    inline bool ackOnly()
	{ return m_ackOnly; }

protected:
    bool m_ack;                        /* Acknoledge flag */
    bool m_ackOnly;                    /* frame need only Ack as response */
    u_int16_t m_retransCount;          /* Retransmission counter */
    u_int32_t m_retransTimeInterval;   /* Retransmission interval */
    u_int64_t m_nextTransTime;         /* Next transmission time */
};

class IAXConnectionlessTransaction;

/**
 * Handle transactions of type New
 */
class YIAX_API IAXTransaction : public RefObject, public Mutex
{
    friend class IAXEngine;
public:
    enum Type {
	Incorrect,
	New,
	RegReq,
	RegRel,
	Poke,
	FwDownl,
    };

    enum State {
	/* *** New */
        Connected,		     /* Call leg established (Accepted). */
	/* Outgoing */
	NewSent,		     /* New sent */ 
 						/* *** Send: */ 
						/* Hangup --> Terminated */
 						/* *** Receive: */ 
						/* AuthReq --> NewSent_AuthReqRecv */
						/* Accept -->  Connected */
						/* Reject, Hangup --> Terminating */
	NewSent_AuthReqRecv,         /* AuthReq received */
 						/* *** Send: */ 
						/* AuthRep --> NewSent_AuthRepSent */
						/* Hangup --> Terminated */
 						/* *** Receive: */ 
						/* Reject, Hangup --> Terminating */
	NewSent_AuthRepSent,         /* AuthRep sent */
 						/* *** Send: */ 
						/* Hangup --> Terminated */
 						/* *** Receive: */ 
						/* Accept -->  Connected */
						/* Reject, Hangup --> Terminating */
	/* Incoming */
	NewRecv,		     /* New received */ 
 						/* *** Send: */ 
						/* AuthReq --> NewRecv_AuthReqSent */
						/* Accept -->  Connected */
						/* Hangup --> Terminated */
 						/* *** Receive: */ 
						/* Reject, Hangup --> Terminating */
	NewRecv_AuthReqSent,         /* AuthReq sent */
 						/* *** Send: */ 
						/* Hangup --> Terminated */
 						/* *** Receive: */ 
						/* AuthRep --> NewRecv_AuthRepRecv */
						/* Reject, Hangup --> Terminating */
	NewRecv_AuthRepRecv,         /* AuthRep received */
 						/* *** Send: */ 
						/* Accept -->  Connected */
						/* Hangup --> Terminated */
 						/* *** Receive: */ 
						/* Reject, Hangup --> Terminating */
	/* *** RegReq/RegRel */
	/* Outgoing */
	RegSent,                     /* RegReq/RegRel sent */
 						/* *** Send: */ 
						/* RegRej --> Terminated */
 						/* *** Receive: */ 
						/* RegAuth -->  RegSent_RegAuthRecv */
						/* RegAck (if RegReq) -->  Terminating */
						/* RegRej --> Terminating */
	RegSent_RegAuthRecv,         /* RegAuth received */ 
 						/* *** Send: */ 
						/* RegReq/RegRel --> RegSent_RegSent */
						/* RegRej --> Terminated */
 						/* *** Receive: */ 
						/* RegRej --> Terminating */
	RegSent_RegSent,             /* RegReq/RegRel sent */
 						/* *** Send: */ 
						/* RegRej --> Terminated */
 						/* *** Receive: */ 
						/* RegAck --> Terminating */
						/* RegRej --> Terminating */
	/* Incoming */
	RegRecv,                     /* RegReq/RegRel received */
 						/* *** Send: */ 
						/* RegAuth -->  RegRecv_RegAuthSent */
						/* RegAck (if RegReq) -->  Terminated */
						/* RegRej --> Terminated */
 						/* *** Receive: */ 
						/* RegRej --> Terminating */
	RegRecv_RegAuthSent,         /* RegAuth sent */
 						/* *** Send: */ 
						/* RegRej --> Terminated */
 						/* *** Receive: */ 
						/* RegReq/RegRel --> RegRecv_RegRecv */
						/* RegRej --> Terminating */
	RegRecv_RegRecv,             /* RegReq/RegRel received */
 						/* *** Send: */ 
						/* RegAck --> Terminating */
						/* RegRej --> Terminated */
 						/* *** Receive: */ 
						/* RegRej --> Terminating */
	/* *** Poke */
	PokeSent,                    /* Poke sent: wait for Pong --> Terminated */
	/* FwDownl */
	/* Not initialized or terminated */
	Unknown,                     /* Initial state. */
	Terminated,                  /* Terminated. No more frames accepted. */
        Terminating,                 /* Terminating. Wait for ACK or timeout to terminate. */
    };	

    /**
     * Destructor
     */
    virtual ~IAXTransaction();

    /**
     * Constructs an incoming transaction from a received full frame with an IAX
     *  control message that needs a new transaction
     * @param engine The engine that owns this transaction
     * @param frame A valid full frame
     * @param lcallno Local call number
     * @param addr Address from where the frame was received
     * @param data Pointer to arbitrary user data
     */
    static IAXTransaction* factoryIn(IAXEngine* engine, IAXFullFrame* frame, u_int16_t lcallno, const SocketAddr& addr, void* data = 0);

    /**
     * Constructs an outgoing transaction with an IAX control message that needs a new transaction
     * @param engine The engine that owns this transaction
     * @param type Transaction type: see Type enumeration
     * @param lcallno Local call number
     * @param addr Address to use
     * @param data Pointer to arbitrary user data
     * @param buf Pointer to already packed IE data for frame
     * @param len buf length
     */
    static IAXTransaction* factoryOut(IAXEngine* engine, Type type, u_int16_t lcallno, const SocketAddr& addr, void* data = 0,
	const unsigned char* buf = 0, unsigned int len = 0);

    /**
     * The IAX engine this transaction belongs to
     * @return Pointer to the IAXEngine of this transaction
     */
    inline IAXEngine* getEngine() const
        { return m_engine; }

    /**
     * Get the type of this transaction
     * @return The type of the transaction as enumeration
     */
    inline Type type() const
        { return m_type; }

    /**
     * Get the state of this transaction
     * @return The state of the transaction as enumeration
     */
    inline State state() const
        { return m_state; }

    /**
     * Get the timestamp of this transaction
     */
    inline u_int64_t timeStamp() const
        { return Time::msecNow() - m_timeStamp; }

    /**
     * Get the direction of this transaction
     */
    inline bool outgoing() const
        { return m_localInitTrans; }

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
     * Process a frame from remote peer
     * If successful and frame is a full frame increment m_iSeqNo
     * This method is thread safe.
     * @param frame IAX frame belonging to this transaction to process
     * @return 'this' if successful or NULL if the frame is invalid
     */
    virtual IAXTransaction* processFrame(IAXFrame* frame);

    /**
     * Process received mini frame data
     * @param data Received data
     * @param tStamp Mini frame timestamp 
     * @param voice True if received mini frame inside a Voice full frame
     * @return 'this' if successful or 0
     */
    IAXTransaction* processMedia(DataBlock& data, u_int32_t tStamp, bool voice = false);

    /**
     * Send media data to remote peer
     * @param data Data to send
     * @return 'this' if successful or 0
     */
    IAXTransaction* sendMedia(const DataBlock& data, u_int8_t format);

    /**
     * Get an IAX event from the queue.
     * This method is thread safe.
     * @return Pointer to an IAXEvent or 0 if none available
     */
    IAXEvent* getEvent(u_int64_t time);

    /**
     */
    static unsigned char getMaxFrameList();

    /**
     */
    static bool setMaxFrameList(unsigned char value);

    /**
     * Send an ANSWER frame to remote peer.
     * This method is thread safe.
     * @return False if the current transaction state is not Connected.
     */
    inline bool sendAnswer()
	{ return sendConnected(IAXFullFrame::Answer); }

    /**
     * Send an ACCEPT frame to remote peer.
     * This method is thread safe.
     * @param format Media format for transmission.
     * @return False if the current transaction state is not NewRecv or NewRecv_AuthRepRecv.
     */
    bool sendAccept(u_int32_t format);

    /**
     * Send a HANGUP frame to remote peer.
     * This method is thread safe.
     * @param cause Optional reason for hangup.
     * @param code Optional code of reason.
     * @return False if the current transaction state does not allow it.
     */
    bool sendHangup(const char* cause = 0, u_int8_t code = 0);

    /**
     * Send a REJECT/REGREJ frame to remote peer.
     * This method is thread safe.
     * @param cause Optional reason for reject.
     * @param code Optional code of reason.
     * @return False if the current transaction state or type does not allow it.
     */
    bool sendReject(const char* cause = 0, u_int8_t code = 0);

    /**
     * Send an AUTHREQ frame to remote peer.
     * This method is thread safe.
     * @param password Password
     * @param authmethod Authentication method
     * @param authdata Authentication data (No need if authmethod is IAXAuthMethod::Text)
     * @return False if the current transaction state is not NewRecv.
     */
    bool sendAuthReq(String& username, IAXAuthMethod::Type authmethod = IAXAuthMethod::Text, String* authdata = 0);

    /**
     * Send an AUTHREP frame to remote peer.
     * This method is thread safe.
     * @param iedata Data to send.
     * @param auth Authentication method used to create iedata.
     * @return False if the current transaction state is not NewSent_AuthReqRecv.
     */
    bool sendAuthRep(String& iedata, IAXAuthMethod::Type auth = IAXAuthMethod::Text);

    /**
     * Send a DTMF frame to remote peer.
     * This method is thread safe.
     * @param dtmf DTMF char to send.
     * @return False if the current transaction state is not Connected or dtmf is grater then 127.
     */
    inline bool sendDtmf(u_int8_t dtmf)
	{ return dtmf <= 127 ? sendConnected((IAXFullFrame::ControlType)dtmf,IAXFrame::DTMF) : false; }

    /**
     * Send a TEXT frame to remote peer.
     * This method is thread safe.
     * @param text Text to send.
     * @return False if the current transaction state is not Connected.
     */
    bool sendText(const char* text);

    /**
     * Send a NOISE frame to remote peer.
     * This method is thread safe.
     * @param noise Noise value to send.
     * @return False if the current transaction state is not Connected or dtmf is grater then 127.
     */
    inline bool sendNoise(u_int8_t noise)
	{ return noise <= 127 ? sendConnected((IAXFullFrame::ControlType)noise,IAXFrame::Noise) : false; }

    /**
     * Test if this transaction is a connectionless one.
     * @return An IAXConnectionlessTransaction pointer if true.
     */
    virtual IAXConnectionlessTransaction* connectionless();

protected:
    /**
     * Constructor: constructs an incoming transaction from a received full frame with an IAX
     *  control message that needs a new transaction
     * @param engine The engine that owns this transaction
     * @param frame A valid full frame
     * @param lcallno Local call number
     * @param addr Address from where the frame was received
     * @param data Pointer to arbitrary user data
     */
    IAXTransaction(IAXEngine* engine, IAXFullFrame* frame, u_int16_t lcallno, const SocketAddr& addr, void* data = 0);

    /**
     * Constructor: constructs an outgoing transaction with an IAX control message that needs a new transaction
     * @param engine The engine that owns this transaction
     * @param type Transaction type: see Type enumeration
     * @param lcallno Local call number
     * @param addr Address to use
     * @param data Pointer to arbitrary user data
     * @param buf Pointer to already packed IE data for frame
     * @param len buf length
     */
    IAXTransaction(IAXEngine* engine, Type type, u_int16_t lcallno, const SocketAddr& addr, void* data = 0,
	const unsigned char* buf = 0, unsigned int len = 0);

    /**
     * Increment sequence number (inbound or outbound) for the frames that need it.
     * @param frame Received frame if @ref inbound is true, otherwise the transmitted frame.
     * @param inbound True for inbound frames.
     * @return True if incremented.
     */
    bool incrementSeqNo(const IAXFullFrame* frame, bool inbound);

    /**
     * Test if frame is acceptable.
     * @param frame Frame to test.
     * @return True if @ref frame can be added to incoming frame list.
     */
    bool isFrameAcceptable(const IAXFullFrame* frame);

    /**
     * Change the transaction state.
     * @param newState the new transaction state.
     * @return False if trying to change a termination state into a non termination state.
     */
    bool changeState(State newState);

    /**
     * Terminate the transaction.
     * @param evType IAXEvent type to generate.
     * @param evClass IAXEvent (frame) type.
     * @param evSubclass IAXEvent (frame) subclass.
     * @param ieList Pointer to IE list if any.
     * @return Pointer to a valid IAXEvent with the reason.
     */
    IAXEvent* terminate(u_int8_t evType, u_int8_t evClass = 0, u_int8_t evSubclass = 0, ObjList* ieList = 0);

    /**
     * Terminate the transaction. Wait for ACK to terminate. No more events will be generated 
     * @param evType IAXEvent type to generate.
     * @param evClass IAXEvent (frame) type.
     * @param evSubclass IAXEvent (frame) subclass.
     * @param ieList Pointer to IE list if any.
     * @return Pointer to a valid IAXEvent with the reason.
     */
    IAXEvent* waitForTerminate(u_int8_t evType, u_int8_t evClass = 0, u_int8_t evSubclass = 0, ObjList* ieList = 0);

    /**
     * Send a full frame to remote peer and put it in transmission list
     * This method is thread safe.
     * tStamp = 0 : use transaction timestamp
     */
    void postFrame(IAXFrame::Type type, u_int32_t subclass, void* data = 0, u_int16_t len = 0, u_int32_t tStamp = 0,
		bool ackOnly = false);

    /**
     * Send a full frame to remote peer.
     * @return True on success.
     */
    bool sendFrame(IAXFrameOut* frame);

    /**
     * Create an event from a frame.
     * @param evType Event type
     * @param frameOut Frame to create from
     * @param newState The transaction new state
     * @return Pointer to an IAXEvent or 0 (Invalid IE list)
     */
    IAXEvent* createEvent(u_int8_t evType, const  IAXFullFrame* frame, State newState);

    /**
     * Create an event from a received frame that is a response to a sent frame and 
     *  change the transaction state to newState. Remove the response from incoming list.
     * @param frame Frame to create response for
     * @param findType Frame type to find
     * @param findSubclass Frame subclass to find
     * @param evType Event type to generate
     * @param newState New transaction state if an event was generated
     * @return Pointer to an IAXEvent or 0 (Invalid IE list)
     */
    IAXEvent* createResponse(IAXFrameOut* frame, u_int8_t findType, u_int8_t findSubclass, u_int8_t evType, State newState);

    /**
     * Find a response for a previously sent frame.
     * @param frame Frame to find response for
     * @param delFrame Delete @ref frame flag. If true on exit, @ref frame will be deleted
     * @return Pointer to an IAXEvent or 0
     */
    virtual IAXEvent* getEventResponse(IAXFrameOut* frame, bool& delFrame);

    /**
     * Find an incoming frame that would start a transaction.
     * @param frame Frame process
     * @param delFrame Delete @ref frame flag. If true on exit, @ref frame will be deleted
     * @return Pointer to an IAXEvent or 0
     */
    IAXEvent* getEventStartTrans(IAXFullFrame* frame, bool& delFrame);

    /**
     * Find a request inside a transaction.
     * If delFrame is true on exit frame will be deleted.
     * @param frame Frame to process
     * @param delFrame Delete @ref frame flag. If true on exit, @ref frame will be deleted
     * @return Pointer to an IAXEvent or 0
     */
    virtual IAXEvent* getEventRequest(IAXFullFrame* frame, bool& delFrame);

    /**
     * Search for a frame in m_inFrames having the given type and subclass
     * @param type Frame type to find.
     * @param subclass Frame subclass to find.
     * @return Pointer to frame or 0.
     */
    IAXFullFrame* findInFrame(IAXFrame::Type type, u_int32_t subclass);

    /**
     * Search in m_inFrames for a frame with the same timestamp as frameOut and deletes it.
     * @param frameOut Frame to find response for
     * @param type Frame type to find
     * @param subclass Frame subclass to find
     * @return True if found.
     */
    bool findInFrameTimestamp(const IAXFullFrame* frameOut, IAXFrame::Type type = IAXFrame::IAX, u_int32_t subclass = IAXControl::Ack);

    /**
     * Send a frame to remote peer in state Connected.
     * This method is thread safe.
     * @param subclass Frame subclass to send.
     * @return False if the current transaction state is not Connected.
     */
    bool sendConnected(IAXFullFrame::ControlType subclass, IAXFrame::Type frametype = IAXFrame::Control);

    /**
     * Send an ACK frame.
     * @param frame Aknoledged frame.
     */
    void sendAck(const IAXFullFrame* frame);

    /**
     * Send an INVAL frame.
     */
    void sendInval();

    /**
     * Internal protocol outgoing frames processing (e.g. IAX PING, LAGRQ).
     * @param frame Frame to process
     * @param delFrame Delete @ref frame flag. If true on exit, @ref frame will be deleted
     * @return 0.
     */
    IAXEvent* processInternalOutgoingRequest(IAXFrameOut* frame, bool& delFrame);

    /**
     * Internal protocol incoming frames processing
     * @param frame Frame to process
     * @param delFrame Delete @ref frame flag. If true on exit, @ref frame will be deleted
     * @return 0.
     */
    IAXEvent* processInternalIncomingRequest(const IAXFullFrame* frame, bool& delFrame);

    /**
     * Process mid call control frames.
     * @param frame Frame to process.
     * @param delFrame Delete @ref frame flag. If true on exit, @ref frame will be deleted
     * @return A valid IAXEvent or 0.
     */
    IAXEvent* processMidCallControl(const IAXFullFrame* frame, bool& delFrame);

    /**
     * Process mid call IAX control frames.
     * @param frame Frame to process.
     * @param delFrame Delete @ref frame flag. If true on exit, @ref frame will be deleted
     * @return A valid IAXEvent or 0.
     */
    IAXEvent* processMidCallIAXControl(const IAXFullFrame* frame, bool& delFrame);

    /**
     * Test if @ref frame is e Reject/RegRej frame
     * @param frame Frame to process.
     * @param delFrame Delete @ref frame flag. If true on exit, @ref frame will be deleted
     * @return A valid IAXEvent or 0.
     */
    IAXEvent* remoteRejectCall(const IAXFullFrame* frame, bool& delFrame);

    /**
     * Terminate the transaction if state is Terminating and timeout.
     * @return A valid IAXEvent or 0.
     */
    IAXEvent* getEventTerminating(u_int64_t time);

    /* Params */
    bool m_localInitTrans;                     /* True: local initiated transaction */
    bool m_localReqEnd;                        /* Local client requested terminate */
    Type m_type;                               /* Transaction type */
    State m_state;                             /* Transaction state */
    u_int64_t m_timeStamp;                     /* Transaction creation timestamp */
    u_int32_t m_timeout;                       /* Transaction timeout (in seconds) on remote termination request */
    SocketAddr m_addr;                         /* Socket */
    u_int16_t m_lCallNo;                       /* Local peer call id */
    u_int16_t m_rCallNo;                       /* Remote peer call id */
    unsigned char m_oSeqNo;                    /* Outgoing frame sequence number */
    unsigned char m_iSeqNo;                    /* Incoming frame sequence number */
    IAXEngine* m_engine;                       /* Engine that owns this transaction */
    void* m_private;                           /* User data */
    u_int16_t m_lastMiniFrameOut;              /* Last transmitted mini frame timestamp */ 
    u_int32_t m_lastMiniFrameIn;               /* Last received mini frame timestamp */ 
    Mutex m_mutexInMedia;                      /* Keep received media thread safe */
    /* Outgoing frames management */
    ObjList m_outFrames;                       /* Transaction & protocol control outgoing frames */
    u_int16_t m_retransCount;                  /* Retransmission counter. 0 --> Timeout */
    u_int32_t m_retransInterval;               /* Frame retransmission interval */
    /* Incoming frames management */
    ObjList m_inFrames;                        /* Transaction & protocol control incoming frames */
    static unsigned char m_maxInFrames;        /* Max frames number allowed in m_inFrames */
    /* Call leg management */
    u_int32_t m_pingInterval;
    u_int64_t m_timeToNextPing;
    /* Statistics */
    u_int32_t m_inTotalFramesCount;            /* Total received frames */
    u_int32_t m_inOutOfOrderFrames;            /* Total out of order frames */
    u_int32_t m_inDroppedFrames;               /* Total dropped frames */
};

/**
 * Keep registration data
 */
class YIAX_API IAXRegData
{
public:
    inline IAXRegData() : m_expire(60), m_userdata(0)
	{}
    inline IAXRegData(const char* name) : m_expire(60), m_name(name), m_userdata(0)
	{}
    inline IAXRegData(const String& username, const String& password, const String& callingNo, const String& callingName,
	u_int16_t expire, const String name, void* userdata)
	: m_username(username), m_password(password), m_callingNo(callingNo), m_callingName(callingName),
	  m_expire(expire), m_name(name), m_userdata(userdata)
	{}

    String m_username;                  /* Username */
    String m_password;                  /* Password */
    String m_callingNo;                 /* Calling number */
    String m_callingName;               /* Calling name */
    u_int32_t m_expire;                 /* Expire time */
    String m_name;                      /* */
    void* m_userdata;                   /* */
};

/**
 * Handle transactions of type RegReg, RegRel, Poke
 */
class YIAX_API IAXConnectionlessTransaction : public IAXTransaction
{
    friend class IAXTransaction;
    friend class IAXEngine;
public:
    virtual ~IAXConnectionlessTransaction();

    inline String& username()
	{ return m_username; }

    inline String& password()
	{ return m_password; }

    inline String& challenge()
	{ return m_challenge; }

    inline u_int16_t expire()
	{ return m_expire; }

    inline void* userdata()
	{ return m_userdata; }

    /**
     * Process a frame from remote peer
     * If successful and frame is a full frame increment m_iSeqNo
     * This method is thread safe.
     * @param frame IAX frame belonging to this transaction to process
     * @return 'this' if successful or NULL if the frame is invalid
     */
    virtual IAXTransaction* processFrame(IAXFrame* frame);

    /**
     * Fill an IAXRegData structure from this transaction
     * @param regdata Destination
     * @return 'this' if successful or NULL if the frame is invalid
     */
    void fillRegData(IAXRegData& regdata);

    /**
     * Send a REGREQ/REGREL (response to REGAUTH) frame to remote peer
     * This method is thread safe.
     * @param authdata Data to send.
     * @param authmethod Authentication method used to create @ref authdata.
     */
    bool sendReg(String& authdata, IAXAuthMethod::Type authmethod = IAXAuthMethod::Text);


    /**
     * Send an REGAUTH frame to remote peer
     * This method is thread safe
     * @param password Password
     * @param authmethod Authentication method
     * @param authdata Authentication data (No need if authmethod is IAXAuthMethod::Text)
     * @return False if the current transaction state is not RegRecv and type is not RegReq or RegRel
     */
    bool sendRegAuth(String& password, IAXAuthMethod::Type authmethod = IAXAuthMethod::Text, String* authdata = 0);

    /**
     * Send an REGACK frame to remote peer
     * This method is thread safe
     * @return False if the current transaction state is not RegRecv and type is RegReq or 
     *  state is not RegRecv_RegRecv and type is RegReq or RegRel
     */
    bool sendRegAck();

    /**
     * Test if this transaction is a connectionless one.
     * @return A pointer to this transaction.
     */
    virtual IAXConnectionlessTransaction* connectionless();

protected:
    /**
     * Constructor: constructs an incoming transaction from a received full frame with an IAX
     *  control message that needs a new transaction
     * @param engine The engine that owns this transaction
     * @param frame A valid full frame
     * @param lcallno Local call number
     * @param addr Address from where the frame was received
     * @param data Pointer to arbitrary user data
     */
    IAXConnectionlessTransaction(IAXEngine* engine, IAXFullFrame* frame, u_int16_t lcallno, const SocketAddr& addr, void* data = 0);

    /**
     * Constructor: constructs an outgoing transaction with an IAX control message that needs a new transaction
     * @param engine The engine that owns this transaction
     * @param type Transaction type: see Type enumeration
     * @param lcallno Local call number
     * @param addr Address to use
     * @param data Pointer to arbitrary user data. For a RegReq/RegRel transaction MUST be an IAXRegData pointer
     * @param buf Pointer to already packed IE data for frame
     * @param len buf length
     */
    IAXConnectionlessTransaction(IAXEngine* engine, Type type, u_int16_t lcallno, const SocketAddr& addr, void* data = 0,
	const unsigned char* buf = 0, unsigned int len = 0);

    /**
     * Find a response for a previously sent frame.
     * @param frame Frame to find response for
     * @param delFrame Delete @ref frame flag. If true on exit, @ref frame will be deleted
     * @return Pointer to an IAXEvent or 0
     */
    virtual IAXEvent* getEventResponse(IAXFrameOut* frame, bool& delFrame);

    /**
     * Find a request inside a transaction.
     * @param frame Frame to find response for
     * @param delFrame Delete @ref frame flag. If true on exit, @ref frame will be deleted
     * @return Pointer to an IAXEvent or 0
     */
    virtual IAXEvent* getEventRequest(IAXFullFrame* frame, bool& delFrame);

private:
    String m_username;                  /* Username */
    String m_password;                  /* Password */
    String m_callingNo;                 /* Calling number */
    String m_callingName;               /* Calling name */
    String m_challenge;                 /* Challenge */
    u_int16_t m_expire;                 /* Expire time */
    String m_name;                      /* */
    void* m_userdata;                   /* */
};

class YIAX_API IAXEvent
{
    friend class IAXTransaction;
    friend class IAXConnectionlessTransaction;
public:
    /**
     * Types of events
     */
    enum Type {
        Invalid = 0,
	Unexpected,
	Terminated,
        Timeout,
	NotImplemented,
	/* NewCall */
	NewCall,
	AuthReq,
	AuthRep,
	Accept,
	Hangup,
	Reject,
	Busy,
	Voice,
	Text,
	Dtmf,
	Noise,
	Answer,
	Quelch,
	Unquelch,
	Progressing,
	Ringing,
	/* NewRegistration */
	NewRegistration,
	RegRecv,
	RegAuth,
	RegAck,
    };

    /**
     * Destructor.
     * If final and transaction is terminated, remove the transaction from its engine queue.
     * Dereferences the transaction possibly causing its destruction.
     */
    ~IAXEvent();

    /**
     * Get the type of this event.
     * @return The type of the event as enumeration
     */
    inline Type type() const
	{ return m_type; }

    /**
     * Check if this is a transaction finalization event.
     * @return True if the transaction has finalized and will be destroyed
     */
    inline bool final() const
        { return m_final; }

    /**
     * Set the final flag.
     */
    inline void setFinal()
        { m_final = true; }

    /**
     * Get the type of the frame that generated the event.
     * If 0 (internal event), the getEvent must delete the event.
     * @return Frame type.
     */
    inline u_int8_t frameType()
	{ return m_frameType; }

    /**
     * Get the subclass of the frame that generated the event.
     * @return Frame subclass.
     */
    inline u_int8_t subclass()
	{ return m_subClass; }

    /**
     * The IAX engine this event belongs to, if any
     */
    inline IAXEngine* getEngine() const
	{ return m_transaction ? m_transaction->getEngine() : 0; }

    /**
     * The IAX transaction that generated the event, if any
     */
    inline IAXTransaction* getTransaction() const
	{ return m_transaction; }

    /**
     * Return the opaque user data stored in the transaction
     */
    inline void* getUserData() const
	{ return m_transaction ? m_transaction->getUserData() : 0; }

    /**
     * Get an IE from the list if it exists
     */
     inline IAXInfoElement* getIE(IAXInfoElement::Type type)
	{ return IAXFrame::getIE(m_ieList,type); }

    /**
     * Get IE list count
     */
     inline u_int32_t getIECount()
	{ return m_ieList ? m_ieList->count() : 0; }

protected:
    /**
     * Constructor
     * @param transaction IAX transaction that generated the event
     */
    IAXEvent(Type type, bool final, IAXTransaction* transaction, u_int8_t frameType = 0, u_int8_t subclass = 0, ObjList* ieList = 0);

private:
    Type m_type;                     /* Event type */
    u_int8_t m_frameType;            /* Frame type */
    u_int8_t m_subClass;             /* Frame subclass */
    bool m_final;                    /* Final event flag */
    IAXTransaction* m_transaction;   /* Transaction that generated this event */
    ObjList* m_ieList;               /* IAXInfoElement list */
};

class YIAX_API IAXEngine : public DebugEnabler, public Mutex
{
public:
    /**
     * Constructor
     * @param transCount Number of entries in the transaction hash table
     * @param retransCount Number of frame retransmission before timeout
     * @param retransTime Default retransmission time
     * @param maxFullFrameDataLen Max full frame IE list (buffer) length
     * @param transTimeout Timeout (in seconds) of transactions belonging to this engine
     */
    IAXEngine(int transCount, int retransCount, int retransInterval, int maxFullFrameDataLen, u_int32_t transTimeout);

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
     * Process media from remote peer. Descendents must override this method.
     * @param transaction IAXTransaction that owns the call leg
     * @param data Media data. 
     * @param tStamp Media timestamp. 
     */
    virtual void processMedia(IAXTransaction* transaction, DataBlock& data, u_int32_t tStamp) 
	{}

    /**
     * Event processor method. Keeps calling @ref getEvent() and passing
     *  any events to @ref processEvent() until there are no more events.
     * @return True if at least one event was processed
     */
    bool process();

    /**
     * Get default frame retransmission counter.
     * @return Frame retransmission counter
     */
    inline u_int16_t retransCount()
        { return m_retransCount; }

    /**
     * Get default frame retransmission starting interval.
     * @return Frame retransmission starting interval
     */
    inline u_int16_t retransInterval()
        { return m_retransInterval; }

    /**
     * Get the timeout (in seconds) of transactions belonging to this engine.
     * @return Timeout (in seconds) of transactions belonging to this engine
     */
    inline u_int32_t transactionTimeout()
        { return m_transactionTimeout; }

    /**
     * Read data from socket 
     */
    void readSocket(SocketAddr& addr);

    /**
     * Write data to socket.
     * @return True on success
     */
    bool writeSocket(const void* buf, int len, const SocketAddr& addr);

    /**
     * Read events.
     */
    void runGetEvents();

    /**
     * Removes a transaction from queue. Free the allocated local call number.
     * Does not delete it.
     * @param transaction Transaction to remove.
     */
    void removeTransaction(IAXTransaction* transaction);

    /**
     * Return the transaction count.
     * This method is thread safe.
     * @return True if transaction still exists.
     */
    u_int32_t transactionCount();

    /**
     * Send an Ack to a remote peer to keep alive.
     * @param addr Address to send.
     */
    void keepAlive(SocketAddr& addr);

protected:
    /**
     * Default event for connection transactions handler. This method may be overriden to perform custom
     *  processing.
     * This method is thread safe.
     */
    virtual void processEvent(IAXEvent* event);

    /**
     * Default event for connectionless transactions handler. This method may be overriden to perform custom
     *  processing.
     * This method is thread safe.
     */
    virtual void processConnectionlessEvent(IAXEvent* event);

    /**
     * Get an IAX event from the queue.
     * This method is thread safe.
     * @return Pointer to an IAXEvent or NULL if none is available
     */
    IAXEvent* getEvent(u_int64_t time);

    /**
     * Generate call number. Update used call numbers list
     * @return Call number or 0 if none available
     */
    u_int16_t generateCallNo();

    /**
     * Generate call number. Update used call numbers list
     */
    void releaseCallNo(u_int16_t lcallno);

    /**
     * Start a transaction based on a local request.
     * @param type Transaction type
     * @param addr Remote address to send the request
     * @param ieList First frame IE list
     * @param regdata Pointer to IAXRegData for RegReq/RegRel transactions
     * @return IAXTransaction pointer on success.
     */
    IAXTransaction* startLocalTransaction(IAXTransaction::Type type, const SocketAddr& addr, ObjList* ieList, IAXRegData* regdata = 0);

private:
    Socket m_socket;                             /* Socket */
    ObjList** m_transList;                       /* Full transactions */
    ObjList m_incompleteTransList;               /* Incomplete transactions (no remote call number) */
    int m_transListCount;                        /* m_transList count */
    u_int16_t m_retransCount;                    /* Retransmission counter for each transaction belonging to this engine */
    u_int16_t m_retransInterval;                 /* Retransmission interval default value */
    bool m_lUsedCallNo[IAX2_MAX_CALLNO + 1];     /* Used local call numnmbers flags */
    int m_lastGetEvIndex;                        /* getEvent: keep last array entry */
    /* Parameters */
    int m_maxFullFrameDataLen;                   /* Max full frame data (IE list) length */
    u_int16_t m_startLocalCallNo;                /* Start index of local call number allocation */
    u_int32_t m_transactionTimeout;              /* Timeout (in seconds) of transactions belonging to this engine */
};

}

#endif /* __YATEIAX_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
