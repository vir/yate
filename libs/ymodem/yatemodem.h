/**
 * yatemodem.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Modem
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

#ifndef __YATEMODEM_H
#define __YATEMODEM_H

#include <yateclass.h>

#ifdef _WINDOWS

#ifdef LIBYMODEM_EXPORTS
#define YMODEM_API __declspec(dllexport)
#else
#ifndef LIBYMODEM_STATIC
#define YMODEM_API __declspec(dllimport)
#endif
#endif

#endif /* _WINDOWS */

#ifndef YMODEM_API
#define YMODEM_API
#endif


/**
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

class BitAccumulator;                    // 1-byte length bit accumulator
class FSKModem;                          // Frequency Shift Keying modulator/demodulator
class UART;                              // UART receiver/transmitter
class UARTBuffer;                        // A byte accumulator used by an UART
class ETSIModem;                         // An analog signal processor as defined by ETSI
// Internal forward declarations
class BitBuffer;                         // Used to accumulate all bits to be printed to output
class FSKFilter;                         // The internal signal filter


/**
 * This class encapsulates an 8 bits length buffer used to accumulate bits
 * @short A 1-byte length bit accumulator
 */
class YMODEM_API BitAccumulator
{
public:
    /**
     * Constructor
     * @param dataBits The buffer size. Values interval 1..8
     */
    inline BitAccumulator(unsigned char dataBits)
	: m_crtByte(0), m_crtPos(0), m_dataBits(dataBits), m_oddParity(false)
	{}

    /**
     * Get the buffer size
     * @return The buffer size
     */
    inline unsigned char dataBits() const
	{ return m_dataBits; }

    /**
     * Set the buffer size. Reset the accumulator
     * @param value The new buffer size. Values interval 1..8
     */
    inline void dataBits(unsigned char value) {
	    m_dataBits = value;
	    reset();
	}

    /**
     * Reset the accumulator. Returns the old data
     * @param oddParity Optional pointer to get the parity of old data
     * @return The old data
     */
    inline unsigned char reset(bool* oddParity = 0) {
	    unsigned char tmp = m_crtByte;
	    m_crtByte = m_crtPos = 0;
	    if (oddParity)
		*oddParity = m_oddParity;
	    m_oddParity = false;
	    return tmp;
	}

    /**
     * Accumulate a bit. Reset accumulator when full
     * @param bit The bit value to accumulate
     * @param oddParity Optional pointer to get the data parity when full
     * @return The accumulated byte or a value greater then 255 if incomplete
     */
    inline unsigned int accumulate(bool bit, bool* oddParity = 0) {
	    if (bit) {
		m_crtByte |= (1 << m_crtPos);
		m_oddParity = !m_oddParity;
	    }
	    m_crtPos++;
	    if (m_crtPos != m_dataBits)
		return 0xffff;
	    return reset(oddParity);
	}

private:
    unsigned char m_crtByte;             // Current partial byte
    unsigned char m_crtPos;              // Current free bit position
    unsigned char m_dataBits;            // The length of a data byte (interval: 1..8)
    bool m_oddParity;                    // The parity of the current byte value (true: odd)
};


/**
 * This is a modulator/demodulator class attached to an UART. Used to demodulate bits
 *  from frequency modulated signal and send them to an UART
 * @short A Frequency Shift Keying modem
 */
class YMODEM_API FSKModem
{
public:
    /**
     * Modem type enumeration
     */
    enum Type {
	ETSI = 0,                        // ETSI caller id signal: MARK:1200 SPACE:2200 BAUDRATE:1200
	                                 //  SAMPLERATE:8000 SAMPLES/BIT:7 STOPBITS:1 PARITY:NONE
	TypeCount = 1
	// NOTE: Don't change these values: they are used as array indexes
    };

    /**
     * Constructor
     * @param params Modem parameters (including modemtype)
     * @param uart The UART attached to this modem
     */
    FSKModem(const NamedList& params, UART* uart);

    /**
     * Destructor
     */
    ~FSKModem();

    /**
     * Check if this modem is terminated. Need reset if so.
     * The modem can terminate processing on UART's request
     * @return True if this modem is terminated
     */
    inline bool terminated() const
	{ return m_terminated; }

    /**
     * Get the type of this modem
     * @return The modem type
     */
    inline int type() const
	{ return m_type; }

    /**
     * Reset modem to its initial state
     */
    void reset();

    /**
     * Data processor. Demodulate received data. Feed the UART with received bits
     * @param data The data to process
     * @return False to stop feedding data (terminated)
     */
    bool demodulate(const DataBlock& data);

    /**
     * Create a buffer containing the modulated representation of a message.
     * A data pattern (depending on modem's type) will be added before the message.
     * A mark pattern (2ms long) will be added after the message.
     * Reset the modem before each request to modulate
     * @param dest Destination buffer
     * @param data Message data (each byte will be enclosed in start/stop/parity bits)
     */
    void modulate(DataBlock& dest, const DataBlock& data);

    /**
     * Append a raw buffer to a data block
     * @param dest Destination buffer
     * @param buf Buffer to append to destination
     * @param len the number of bytes to append starting with buf
     */
    static inline void addRaw(DataBlock& dest, void* buf, unsigned int len) {
	    DataBlock tmp(buf,len,false);
	    dest += tmp;
	    tmp.clear(false);
	}

    /**
     * Keep the modem type names. Useful to configure the modem
     */
    static TokenDict s_typeName[];

private:
    int m_type;                          // Modem type
    bool m_terminated;                   // Terminated flag (need reset if true)
    FSKFilter* m_filter;                 // Internal filter used to demodulate received data
    UART* m_uart;                        // The UART using this modem's services
    DataBlock m_buffer;                  // Partial input buffer when used to demodulate or modulate data
    BitBuffer* m_bits;                   // Bit buffer used when debugging
};


/**
 * Accumulate data bits received from a modem
 * @short An UART receiver/transmitter
 */
class YMODEM_API UART : public DebugEnabler
{
public:
    /**
     * UART state enumeration
     */
    enum State {
	Idle,                            // Not started
	BitStart,                        // Waiting for start bit (SPACE)
	BitData,                         // Accumulate data bits
	BitParity,                       // Waiting for parity bit(s)
	BitStop,                         // Waiting for stop bit (MARK)
	UARTError,                       // Error
    };

    /**
     * UART error enumeration
     */
    enum Error {
	EFraming,                        // Frame error: invalid stop bit(s)
	EParity,                         // Parity error
	EChksum,                         // Message checksum error
	EInvalidData,                    // Invalid (inconsistent) data
	EUnknown,                        // Unknown error
	EStopped,                        // Aborted by descendants
	ENone
    };

    /**
     * Constructor
     * @param state The initial state of this UART
     * @param params The UART's parameters
     * @param name The name of this debug enabler
     */
    UART(State state, const NamedList& params, const char* name = 0);

    /**
     * Destructor
     */
    virtual ~UART()
	{}

    /**
     * Get the current state of this UART
     * @return The current state of this UART as enumeration
     */
    inline State state() const
	{ return m_state; }

    /**
     * Get the current error state of this UART, if any
     * @return The current error state of this UART as enumeration
     */
    inline Error error() const
	{ return m_error; }

    /**
     * Get the type of this UART's modem
     * @return The type of this UART's modem
     */
    inline int modemType() const
	{ return m_modem.type(); }

    /**
     * Get the data bit accumulator used by this UART
     * @return The data bit accumulator used by this UART
     */
    inline const BitAccumulator& accumulator() const
	{ return m_accumulator; }

    /**
     * Reset this UART
     * @param newState The state to reset to
     */
    virtual void reset(State newState = Idle);

    /**
     * Send data to the enclosed modem to be demodulated
     * @param data The data to process
     * @return False to stop processing
     */
    inline bool demodulate(const DataBlock& data)
	{ return m_modem.demodulate(data); }

    /**
     * Create a buffer containing the modulated representation of a list of parameters
     * @param dest Destination buffer
     * @param params The list containing the values to be modulated
     * @return False on failure (an 'error' parameter will be set in params)
     */
    inline bool modulate(DataBlock& dest, NamedList& params) {
	    DataBlock data;
	    if (!createMsg(params,data))
		return false;
	    m_modem.modulate(dest,data);
	    return true;
	}

    /**
     * Create a buffer containing the modulated representation of another one
     * @param dest Destination buffer
     * @param src Source buffer
     */
    inline void modulate(DataBlock& dest, const DataBlock& src)
	{ m_modem.modulate(dest,src); }

    /**
     * Push a bit of data into this UART. Once a data byte is accumulated, push it back to itself
     * @param value The bit to be processed
     * @return False to stop feeding data
     */
    bool recvBit(bool value);

    /**
     * Push a data byte into this UART
     * @param data The byte to be processed
     * @return False to stop feeding data
     */
    virtual bool recvByte(unsigned char data)
	{ return false; }

    /**
     * Notification from modem that the FSK start was detected
     * @return False to stop the modem
     */
    virtual bool fskStarted()
	{ return true; }

    /**
     * Keeps the names associated with UART errors
     */
    static TokenDict s_errors[];

protected:
    /**
     * Process an accumulated byte in Idle state
     * @param data The byte to process
     * @return Negative to stop, positive to change state to BitStart, 0 to continue
     */
    virtual int idleRecvByte(unsigned char data)
	{ return false; }

    /**
     * Create a buffer containing the byte representation of a message to be sent
     * @param params The list containing message parameters
     * @param data Destination message data buffer
     * @return False on failure
     */
    virtual bool createMsg(NamedList& params, DataBlock& data)
	{ return false; }

    /**
     * Set the error state of this UART
     * @param e The error
     * @return False
     */
    bool error(Error e);

private:
    // Change this UART's state
    void changeState(State newState);

    FSKModem m_modem;                    // The modem used by this UART
    State m_state;                       // The state of this UART
    Error m_error;                       // The error type if state is error
    int m_parity;                        // Used parity: 0=none, -1=odd, 1=even
    bool m_expectedParity;               // The expected value of the parity bit if used
    BitAccumulator m_accumulator;        // The data bits accumulator
};


/**
 * This class is used by an UART to accumulate messages with known length
 * @short A fixed length byte accumulator used by an UART
 */
class YMODEM_API UARTBuffer
{
public:
    /**
     * Constructor
     * @param client The client of this buffer
     */
    inline UARTBuffer(UART* client)
	: m_client(client)
	{ reset(); }

    /**
     * Get the accumulated data
     * @return The accumulated data
     */
    inline const DataBlock& buffer() const
	{ return m_buffer; }

    /**
     * Get the free space length in the buffer
     * @return The free space length
     */
    inline unsigned int free() const
	{ return m_free; }

    /**
     * Reset the buffer
     * @param len The new length of the buffer. Set to 0 to left the length unchanged
     */
    inline void reset(unsigned int len = 0) {
	    m_buffer.clear();
	    m_crtIdx = m_free = 0;
	    if (len) {
		m_buffer.assign(0,len);
		m_free = len;
	    }
	}

    /**
     * Accumulate data
     * @param value The value to append to the buffer
     * @return False on buffer overflow
     */
    inline bool accumulate(unsigned char value) {
	    if (m_free) {
		((unsigned char*)m_buffer.data())[m_crtIdx++] = value;
		m_free--;
		return true;
	    }
	    Debug(m_client,DebugNote,"Buffer overflow");
	    return false;
	}

private:
    UART* m_client;                      // The client
    unsigned int m_crtIdx;               // Current index n buffer
    unsigned int m_free;                 // Free buffer length
    DataBlock m_buffer;                  // The buffer
};


/**
 * This class implements a modem/UART pair used to demodulate/decode analog signal as defined
 *  in ETSI EN 300 659-1, ETSI EN 300 659-2, ETSI EN 300 659-3
 * @short An analog signal processor as defined by ETSI
 */
class YMODEM_API ETSIModem : public UART
{
public:
    /**
     * The state of this ETSI decoder
     */
    enum State {
	StateError,                      // Error encountered: need reset
	WaitFSKStart,                    // Waiting for data start pattern
	WaitMark,                        // Waiting for mark pattern
	WaitMsg,                         // Wait a message
	WaitMsgLen,                      // Received message: wait length
	WaitParam,                       // Wait a parameter
	WaitParamLen,                    // Received parameter: wait length
	WaitData,                        // Received parameter length: wait data
	WaitChksum,                      // Wait checksum
    };

    /**
     * Message type defined in ETSI EN 659-3 5.2
     */
    enum MsgType {
	MsgCallSetup = 0x80,             // Call setup
	MsgMWI       = 0x82,             // Message waiting indicator
	MsgCharge    = 0x86,             // Advise of charge
	MsgSMS       = 0x89,             // Short message service
    };

    /**
     * Message parameters defined in ETSI EN 659-3 5.3
     */
    enum MsgParam {
	DateTime         = 0x01,         // 8		Date and Time
	CallerId         = 0x02,         // max. 20	Calling Line Identity
	CalledId         = 0x03,         // max. 20	Called Line Identity
	CallerIdReason   = 0x04,         // 1		Reason for Absence of Calling Line Identity
	CallerName       = 0x07,         // max. 50	Calling Party Name
	CallerNameReason = 0x08,         // 1		Reason for absence of Calling Party Name
	VisualIndicator  = 0x0B,         // 1		Visual Indicator
	MessageId        = 0x0D,         // 3		Message Identification
	LastMsgCLI       = 0x0E,         // max. 20	Last Message CLI
	CompDateTime     = 0x0F,         // 8 or 10	Complementary Date and Time
	CompCallerId     = 0x10,         // max. 20	Complementary Calling Line Identity
	CallType         = 0x11,         // 1		Call type
	FirstCalledId    = 0x12,         // max. 20	First Called Line Identity
	MWICount         = 0x13,         // 1		Number of Messages
	FwdCallType      = 0x15,         // 1		Type of Forwarded call
	CallerType       = 0x16,         // 1		Type of Calling user
	RedirNumber      = 0x1A,         // max. 20	Redirecting Number
	Charge           = 0x20,         // 14		Charge
	AdditionalCharge = 0x21,         // 14		Additional Charge
	Duration         = 0x23,         // 6		Duration of the Call
	NetworkID        = 0x30,         // max. 20	Network Provider Identity
	CarrierId        = 0x31,         // max. 20	Carrier Identity
	SelectFunction   = 0x40,         // 2-21        Selection of Terminal Function
	Display          = 0x50,         // max. 253	Display Information
	ServiceInfo      = 0x55,         // 1		Service Information
	Extension        = 0xE0,         // 10		Extension for network operator use
	Unknown
    };

    /**
     * Constructor
     * @param params Decoder parameters
     * @param name The name of this debug enabler
     */
    ETSIModem(const NamedList& params, const char* name = 0);

    /**
     * Destructor
     */
    virtual ~ETSIModem();

    /**
     * Reset this decoder (modem and UART)
     */
    virtual void reset();

    /**
     * Push a data byte into this decoder. Reset this UART and call decode after validated a received message
     * @param data The byte to be processed
     * @return False to stop feeding data
     */
    virtual bool recvByte(unsigned char data);

    /**
     * Keeps the text associated with message type enumeration
     */
    static TokenDict s_msg[];

    /**
     * Keeps the text associated with parameter type enumeration
     */
    static TokenDict s_msgParams[];

protected:
    /**
     * Process an accumulated byte in Idle state
     * @param data The byte to process
     * @return Negative to stop, positive to change state to BitStart, 0 to continue
     */
    virtual int idleRecvByte(unsigned char data);

    /**
     * Process a list of received message parameters
     * @param msg The message type as enumeration
     * @param params Message parameters
     * @return False to stop processing data
     */
    virtual bool recvParams(MsgType msg, const NamedList& params)
	{ return false; }

    /**
     * Process (decode) a valid received buffer. Call recvParams() after decoding the message
     * @param msg The message type as enumeration
     * @param buffer The accumulated data bytes
     * @return False to stop processing data
     */
    virtual bool decode(MsgType msg, const DataBlock& buffer);

    /**
     * Create a buffer containing the byte representation of a message to be sent
     * @param params The list containing message parameters.
     *  The name of the list must be a valid (known) message
     * @param data Destination message data buffer
     * @return False on failure (an 'error' parameter will be set in params)
     */
    virtual bool createMsg(NamedList& params, DataBlock& data);

private:
    // Change decoder's state
    void changeState(State newState);

    UARTBuffer m_buffer;                 // The buffer used to accumulate messages
    State m_state;                       // Decoder state
    unsigned char m_waitSeizureCount;    // Expected number of channel seizure bytes in a row
    unsigned char m_crtSeizureCount;     // Current number of channel seizure bytes in a row
    unsigned char m_crtMsg;              // Current message id
    unsigned char m_crtParamLen;         // Current receiving parameter length
    unsigned int m_chksum;               // Current calculated checksum
};

}

#endif /* __YATEMODEM_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
