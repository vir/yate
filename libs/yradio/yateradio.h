/**
 * yateradio.h
 * Radio library
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2011-2014 Null Team
 * Copyright (C) 2015 LEGBA Inc
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

#ifndef __YATERADIO_H
#define __YATERADIO_H

#include <yateclass.h>
#include <yatexml.h>

#ifdef _WINDOWS

#ifdef LIBYRADIO_EXPORTS
#define YRADIO_API __declspec(dllexport)
#else
#ifndef LIBYRADIO_STATIC
#define YRADIO_API __declspec(dllimport)
#endif
#endif

#endif /* _WINDOWS */

#ifndef YRADIO_API
#define YRADIO_API
#endif


namespace TelEngine {

class GSML3Codec;                        // GSM Layer codec
class RadioCapability;                   // Radio device capabilities
class RadioInterface;                    // Generic radio interface

class YRADIO_API GSML3Codec
{
    YNOCOPY(GSML3Codec);
public:
    /**
     * Codec flags
     */
    enum Flags {
	XmlDumpMsg = 0x01,
	XmlDumpIEs = 0x02,
	MSCoder    = 0x04,
    };

    /**
     * Codec return status
     */
    enum Status {
	NoError = 0,
	MsgTooShort,
	UnknownProto,
	ParserErr,
	MissingParam,
	IncorrectOptionalIE,
	IncorrectMandatoryIE,
	MissingMandatoryIE,
	UnknownMsgType,
    };

    /**
     * Protocol discriminator according to ETSI TS 124 007 V11.0.0, section 11.2.3.1.1
     */
    enum Protocol {
	GCC       = 0x00, // Group Call Control
	BCC       = 0x01, // Broadcast Call Control
	EPS_SM    = 0x02, // EPS Session Management
	CC        = 0x03, // Call Control; Call Related SS messages
	GTTP      = 0x04, // GPRS Transparent Transport Protocol (GTTP)
	MM        = 0x05, // Mobility Management
	RRM       = 0x06, // Radio Resources Management
	EPS_MM    = 0x07, // EPS Mobility Management
	GPRS_MM   = 0x08, // GPRS Mobility Management
	SMS       = 0x09, // SMS
	GPRS_SM   = 0x0a, // GPRS Session Management
	SS        = 0x0b, // Non Call Related SS messages
	LCS       = 0x0c, // Location services
	Extension = 0x0e, // reserved for extension of the PD to one octet length
	Test      = 0x0f, // used by tests procedures described in 3GPP TS 44.014, 3GPP TS 34.109 and 3GPP TS 36.509
	Unknown   = 0xff,
    };

    /**
     * IE types
     */
    enum Type {
	NoType = 0,
	T,
	V,
	TV,
	LV,
	TLV,
	LVE,
	TLVE,
    };

    /**
     * Type of XML data to generate
     */
    enum XmlType {
	Skip,
	XmlElem,
	XmlRoot,
    };

    /**
     * EPS Security Headers
     */
    enum EPSSecurityHeader {
	PlainNAS                           = 0x00,
	IntegrityProtect                   = 0x01,
	IntegrityProtectCiphered           = 0x02,
	IntegrityProtectNewEPSCtxt         = 0x03,
	IntegrityProtectCipheredNewEPSCtxt = 0x04,
	ServiceRequestHeader               = 0xa0,
    };

    /**
     * Constructor
     */
    GSML3Codec(DebugEnabler* dbg = 0);

    /**
     * Decode layer 3 message payload
     * @param in Input buffer containing the data to be decoded
     * @param len Length of input buffer
     * @param out XmlElement into which the decoded data is returned
     * @param params Encoder parameters
     * @return Parsing result: 0 (NoError) if succeeded, error status otherwise
     */
    unsigned int decode(const uint8_t* in, unsigned int len, XmlElement*& out, const NamedList& params = NamedList::empty());

    /**
     * Encode a layer 3 message
     * @param in Layer 3 message in XML form
     * @param out Output buffer into which to put encoded data
     * @param params Encoder parameters
     * @return Parsing result: 0 (NoError) if succeeded, error status otherwise
     */
    unsigned int encode(const XmlElement* in, DataBlock& out, const NamedList& params = NamedList::empty());

    /**
     * Decode layer 3 message from an existing XML
     * @param xml XML which contains layer 3 messages to decode and into which the decoded XML will be put
     * @param params Decoder parameters
     * @return Parsing result: 0 (NoError) if succeeded, error status otherwise
     */
    unsigned int decode(XmlElement* xml, const NamedList& params = NamedList::empty());

    /**
     * Encode a layer 3 message from an existing XML
     * @param xml XML which contains a layer 3 message in XML form. The message will be replaced with its encoded buffer
     * @param params Encoder parameters
     * @return Parsing result: 0 (NoError) if succeeded, error status otherwise
     */
    unsigned int encode(XmlElement* xml, const NamedList& params = NamedList::empty());

    /**
     * Set data used in debug
     * @param enabler The DebugEnabler to use (0 to to use the engine)
     * @param ptr Pointer to print, 0 to use the codec pointer
     */
    void setCodecDebug(DebugEnabler* enabler = 0, void* ptr = 0);

    /**
     * Retrieve codec flags
     * @return Codec flags
     */
    inline uint8_t flags() const
	{ return m_flags; }

    /**
     * Set codec flags
     * @param flgs Flags to set
     * @param reset Reset flags before setting these ones
     */
    inline void setFlags(uint8_t flgs, bool reset = false)
    {
	if (reset)
	    resetFlags();
	m_flags |= flgs;
    }

    /**
     * Reset codec flags
     * @param flgs Flags to reset. If 0, all flags are reset
     */
    inline void resetFlags(uint8_t flgs = 0)
    {
	if (flgs)
	    m_flags &= ~flgs;
	else
	    m_flags = 0;
    }

    /**
     * Activate printing of debug messages
     * @param on True to activate, false to disable
     */
    inline void setPrintDbg(bool on = false)
	{ m_printDbg = on; }

    /**
     * Get printing of debug messages flag
     * @return True if debugging is activated, false otherwise
     */
    inline bool printDbg() const
	{ return m_printDbg; }

    /**
     * Get DebugEnabler used by this codec
     * @return DebugEnabler used by the codec
     */
    inline DebugEnabler* dbg() const
	{ return m_dbg; }

    /**
     * Retrieve the codec pointer used for debug messages
     * @return Codec pointer used for debug messages
     */
    inline void* ptr() const
	{ return m_ptr; }

    /**
     * Decode GSM 7bit buffer
     * @param buf Input buffer
     * @param len Input buffer length
     * @param text Destination text
     * @param heptets Maximum number of heptets in buffer
     */
    static void decodeGSM7Bit(unsigned char* buf, unsigned int len, String& text,
	unsigned int heptets = (unsigned int)-1);

    /**
     * Encode GSM 7bit buffer
     * @param text Input text
     * @param buf Destination buffer
     * @return True if all characters were encoded correctly
     */
    static bool encodeGSM7Bit(const String& text, DataBlock& buf);

    /**
     * IE types dictionary
     */
    static const TokenDict s_typeDict[];

    /**
     * L3 Protocols dictionary
     */
    static const TokenDict s_protoDict[];

    /**
     * EPS Security Headers dictionary
     */
    static const TokenDict s_securityHeaders[];

    /**
     * Errors dictionary
     */
    static const TokenDict s_errorsDict[];

    /**
     * Mobility Management reject causes dictionary
     */
    static const TokenDict s_mmRejectCause[];

    /**
     * GPRS Mobility Management reject causes dictionary
     */
    static const TokenDict s_gmmRejectCause[];

private:

    unsigned int decodeXml(XmlElement* xml, const NamedList& params, const String& pduTag);
    unsigned int encodeXml(XmlElement* xml, const NamedList& params, const String& pduTag);
    void printDbg(int dbgLevel, const uint8_t* in, unsigned int len, XmlElement* xml, bool encode = false);

    uint8_t m_flags;                 // Codec flags
    // data used for debugging messages
    DebugEnabler* m_dbg;
    void* m_ptr;
    // activate debug
    bool m_printDbg;
};


/**
 * @short Radio device capabilities
 * Radio capability object describes the parameter ranges of the radio handware.
 */
class YRADIO_API RadioCapability
{
public:
    /**
     * Constructor
     */
    RadioCapability();

    unsigned maxPorts;                   // Available number of ports
    unsigned currPorts;                  // Number of used (available) ports
    uint64_t maxTuneFreq;                // Maximum allowed tuning frequency (in Hz)
    uint64_t minTuneFreq;                // Minimum allowed tuning frequency (in Hz)
    unsigned maxSampleRate;              // Maximum allowed sampling rate (in Hz)
    unsigned minSampleRate;              // Minimum allowed sampling rate (in Hz)
    unsigned maxFilterBandwidth;         // Maximum allowed anti-alias filter bandwidth (in Hz)
    unsigned minFilterBandwidth;         // Minimum allowed anti-alias filter bandwidth (in Hz)
    unsigned rxLatency;                  // Estimated radio latency (in samples)
    unsigned txLatency;                  // Estimated transmit latency (in samples)
};


/**
 * Keeps a buffer pointer with offset and valid samples
 * @short A buffer description
 */
class RadioBufDesc
{
public:
    /**
     * Constructor
     */
    inline RadioBufDesc()
	: samples(0), offs(0), valid(0)
	{}

    /**
     * Reset the buffer
     * @param value Offset and valid samples value
     */
    inline void reset(unsigned int value = 0)
	{ offs = valid = value; }

    /**
     * Reset the buffer
     * @param offset New offset
     * @param validS New valid samples value
     */
    inline void reset(unsigned int offset, unsigned int validS) {
	    offs = offset;
	    valid = validS;
	}

    /**
     * Check if the buffer is valid
     * @param minSamples Required minimum number of valid samples
     * @return True if valid, false otherwise
     */
    inline bool validSamples(unsigned int minSamples) const
	{ return !minSamples || minSamples >= offs || minSamples >= valid; }

    float* samples;                      // Current read buffer
    unsigned int offs;                   // Current buffer offset (in sample periods)
    unsigned int valid;                  // The number of valid samples in buffer
};


/**
 * Keeps buffers used by RadioInterface::read()
 * @short RadioInterface read buffers
 */
class RadioReadBufs : public GenObject
{
public:
    /**
     * Constructor
     * @param len Single buffer length (in sample periods)
     * @param validThres Optional threshold for valid samples. Used when read timestamp
     *  data is in the future and a portion of the buffer need to be reset.
     *  If valid samples are below threshold data won't be set or copied
     */
    inline RadioReadBufs(unsigned int len = 0, unsigned int validThres = 0)
	: m_bufSamples(len), m_validMin(validThres)
	{}

    /**
     * Reset buffers
     * @param len Single buffer length (in sample periods)
     * @param validThres Optional threshold for valid samples
     */
    inline void reset(unsigned int len, unsigned int validThres) {
	    m_bufSamples = len;
	    m_validMin = validThres;
	    crt.reset();
	    aux.reset();
	    extra.reset();
	}

    /**
     * Retrieve the length of a single buffer
     * @return Buffer length (in sample periods)
     */
    inline unsigned int bufSamples() const
	{ return m_bufSamples; }

    /**
     * Check if a given buffer is full (offset is at least buffer length)
     * @param buf Buffer to check
     * @return True if the given buffer is full, false otherwise
     */
    inline bool full(RadioBufDesc& buf) const
	{ return buf.offs >= m_bufSamples; }

    /**
     * Check if a given is valid (have enough valid samples)
     * @param buf Buffer to check
     * @return True if the given buffer is valid, false otherwise
     */
    inline bool valid(RadioBufDesc& buf) const
	{ return buf.validSamples(m_validMin); }

    /**
     * Dump data for debug purposes
     * @param buf Destination buffer
     * @return Destination buffer reference
     */
    String& dump(String& buf);

    RadioBufDesc crt;
    RadioBufDesc aux;
    RadioBufDesc extra;

protected:
    unsigned int m_bufSamples;           // Buffers length in sample periods
    unsigned int m_validMin;             // Valid samples threshold
};


/**
 * @short Generic radio interface
 *
 * @note Some parameters are quantized by the radio hardware. If the caller requests a parameter
 * value that cannot be matched exactly, the setting method will set the parameter to the
 * best avaiable match and return "NotExact". For such parameters, there is a corresponding
 * readback method to get the actual value used.
 *
 * @note If a method does not include a radio port number, then that method applies to all connected ports.
 *
 * @note The interface may control multiple radios, with each one appearing as a port. However, in this case,
 *  - all radios are to be synched on the sample clock
 *  - all radios are to be of the same hardware type
 *
 * @note If the performance of the radio hardware changes, the API indicates this with the RFHardwareChange
 * flag. If this flag appears in a result code, the application should:
 *  - read a new RadioCapabilties object
 *  - revisit all of the application-level parameter settings on the radio
 *  - check the status() method on each port to identify single-port errors
 */
class YRADIO_API RadioInterface : public RefObject, public DebugEnabler
{
    YCLASS(RadioInterface,RefObject);
public:
    /** Error code bit positions in the error code mask. */
    enum ErrorCode {
	NoError = 0,
	Failure = (1 << 1),              // Unknown error
	HardwareIOError = (1 << 2),      // Communication error with HW
	NotInitialized = (1 << 3),       // Interface not initialized
	NotSupported = (1 << 4),         // Feature not supported
	NotCalibrated = (1 << 5),        // The radio is not calibrated
	TooEarly = (1 << 6),             // Timestamp is in the past
	TooLate = (1 << 7),              // Timestamp is in the future
	OutOfRange = (1 << 8),           // A requested parameter setting is out of range
	NotExact = (1 << 9),             // The affected value is not an exact match to the requested one
	DataLost = (1 << 10),            // Received data lost due to slow reads
	Saturation = (1 << 11),          // Data contain values outside of +/-1+/-j
	RFHardwareFail = (1 << 12),      // Failure in RF hardware
	RFHardwareChange = (1 << 13),    // Change in RF hardware, not outright failure
	EnvironmentalFault = (1 << 14),  // Environmental spec exceeded for radio HW
	InvalidPort = (1 << 15),         // Invalid port number
	Pending = (1 << 16),             // Operation is pending
	Cancelled = (1 << 17),           // Operation cancelled
	Timeout = (1 << 18),             // Operation timeout
	// Masks
	// Errors requiring radio or port shutdown
	FatalErrorMask = HardwareIOError | RFHardwareFail | EnvironmentalFault | Failure,
	// Errors that can be cleared
	ClearErrorMask = TooEarly | TooLate | NotExact | DataLost | Saturation |
	    InvalidPort | Timeout,
	// Errors that are specific to a single call
	LocalErrorMask = NotInitialized | NotCalibrated | TooEarly | TooLate | OutOfRange |
	    NotExact | DataLost | Saturation | RFHardwareChange | InvalidPort,
    };

    /**
     * Retrieve the radio device path
     * @param devicePath Destination buffer
     * @return Error code (0 on success)
     */
    virtual unsigned int getInterface(String& devicePath) const
	{ return NotSupported; }

    /**
     * Retrieve radio capabilities
     * @return RadioCapability pointer, 0 if unknown or not implemented
     */
    virtual const RadioCapability* capabilities() const
	{ return m_radioCaps; }

    /**
     * Initialize the radio interface.
     * Any attempt to transmit or receive prior to this operation will return NotInitialized.
     * @param params Optional parameters list
     * @return Error code (0 on success)
     */
    virtual unsigned int initialize(const NamedList& params = NamedList::empty()) = 0;

    /**
     * Set radio loopback
     * @param name Loopback name (NULL for none)
     * @return Error code (0 on success)
     */
    virtual unsigned int setLoopback(const char* name = 0)
	{ return NotSupported; }

    /**
     * Set multiple interface parameters.
     * Each command must start with 'cmd:' to allow the code to detect unhandled commands.
     * Command sub-params should not start with the prefix
     * @param params Parameters list
     * @param shareFate True to indicate all parameters share the fate
     *  (return on first error, except for Pending), false to process all
     * @return Error code (0 on success). Failed command(s) will be set in the list
     *  with cmd_name.code=error_code
     */
    virtual unsigned int setParams(NamedList& params, bool shareFate = true) = 0;

    /**
     * Update (set/reset) interface data dump
     * @param dir Direction to update. 0: both, negative: RX only, positive: TX only
     * @param level Dump level to update. 0: both, negative: interface level (data
     *  sent/received by the upper layer), positive: device level (data sent to or
     *  read from radio device)
     * @param params Optional parameters
     * @return Error code (0 on success)
     */
    virtual unsigned int setDataDump(int dir = 0, int level = 0,
	const NamedList* params = 0) = 0;

    /**
     * Run internal calibration procedures and/or load calibration parmameters
     * @return Error code (0 on success)
     */
    virtual unsigned int calibrate()
	{ return NotSupported; }

    /**
     * Set the number of ports to be used
     * @param count The number of ports to be used
     * @return Error code (0 on success)
     */
    virtual unsigned int setPorts(unsigned count) = 0;

    /**
     * Return any persistent error codes.
     * ("Persistent" means a condition of the radio interface itself,
     * as opposed to a status code related to a specific method call)
     * @param port Port number to check, or -1 for all ports OR'd together
     * @return Error(s) mask
     */
    virtual unsigned int status(int port = -1) const = 0;

    /**
     * Clear all error codes that can be cleared.
     * Note the not all codes are clearable, like HW failures for example.
     */
    virtual void clearErrors()
	{ m_lastErr &= ~ClearErrorMask; }

    /**
     * Send a frame of complex samples at a given time, interleaved IQ format.
     * If there are gaps in the sample stream, the RadioInterface must zero-fill.
     * All ports are sent together in interleaved format; example:
     * I0 Q0 I1 Q1 I2 Q2 I0 Q0 I1 Q1 I2 Q2, etc for a 3-port system
     * Block until the send is complete.
     * Return TooEarly if the blocking time would be too long.
     * Return TooLate if any of the block is in the past.
     * @param when Time to schedule the transmission
     * @param samples Data to send (array of 2 * size * ports floats, interleaved IQ)
     * @param size The number of sample periods in the samples array
     * @param powerScale Optional pointer to power scale value
     * @return Error code (0 on success)
     */
    virtual unsigned int send(uint64_t when, float* samples, unsigned size,
	float* powerScale = 0) = 0;

    /**
     * Receive the next available samples and associated timestamp.
     * All ports are received together in interleaved format.
     *  e.g: I0 Q0 I1 Q1 I2 Q2 I0 Q0 I1 Q1 I2 Q2, etc for a 3-port system.
     * The method will wait for proper timestamp (to be at least requested timestamp).
     * The caller must increase the timestamp after succesfull read.
     * The method may return less then requested size.
     * @param when Input: current timestamp. Output: read data timestamp
     * @param samples Destination buffer (array of 2 * size * ports floats, interleaved IQ)
     * @param size Input: requested number of samples. Output: actual number of read samples
     * @return Error code (0 on success)
     */
    virtual unsigned int recv(uint64_t& when, float* samples, unsigned& size) = 0;

    /**
     * Receive the next available samples and associated timestamp.
     * Compensate timestamp difference.
     * Copy any valid data in the future to auxiliary buffers.
     * Adjust the timestamp (no need for the caller to do it).
     * Handles buffer rotation also.
     * All sample counters (length/offset) are expected in sample periods.
     * @param when Input: current timestamp. Output: next read data timestamp
     * @param bufs Buffers to use
     * @param skippedBufs The number of skipped full buffers
     * @return Error code (0 on success)
     */
    virtual unsigned int read(uint64_t& when, RadioReadBufs& bufs,
	unsigned int& skippedBufs);

    /**
     * Get the time of the data currently being received from the radio
     * @param when Destination buffer for requested radio time
     * @return Error code (0 on success)
     */
    virtual unsigned int getRxTime(uint64_t& when) const = 0;

    /**
     * Get the time of the the data currently being sent to the radio
     * @param when Destination buffer for requested radio time
     * @return Error code (0 on success)
     */
    virtual unsigned int getTxTime(uint64_t& when) const = 0;

    /**
     * Set the frequency offset
     * @param offs Frequency offset to set
     * @param newVal Optional pointer to value set by interface (requested value may
     *  be adjusted to fit specific device value)
     * @return Error code (0 on success)
     */
    virtual unsigned int setFreqOffset(int offs, int* newVal = 0) = 0;

    /**
     * Set the sample rate
     * @param hz Sample rate value in Hertz
     * @return Error code (0 on success)
     */
    virtual unsigned int setSampleRate(uint64_t hz) = 0;

    /**
     * Get the actual sample rate
     * @param hz Sample rate value in Hertz
     * @return Error code (0 on success)
     */
    virtual unsigned int getSampleRate(uint64_t& hz) const = 0;

    /**
     * Set the anti-aliasing filter BW
     * @param hz Anti-aliasing filter value in Hertz
     * @return Error code (0 on success)
     */
    virtual unsigned int setFilter(uint64_t hz) = 0;

    /**
     * Get the actual anti-aliasing filter BW
     * @param hz Anti-aliasing filter value in Hertz
     * @return Error code (0 on success)
     */
    virtual unsigned int getFilterWidth(uint64_t& hz) const = 0;

    /**
     * Set the transmit frequency in Hz
     * @param hz Transmit frequency value
     * @return Error code (0 on success)
     */
    virtual unsigned int setTxFreq(uint64_t hz) = 0;

    /**
     * Readback actual transmit frequency
     * @param hz Transmit frequency value
     * @return Error code (0 on success)
     */
    virtual unsigned int getTxFreq(uint64_t& hz) const = 0;

    /**
     * Set the output power in dBm.
     * This is power per active port, compensating for internal gain differences.
     * @param dBm The output power to set
     * @return Error code (0 on success)
     */
    virtual unsigned int setTxPower(unsigned dBm) = 0;

    /**
     * Set the receive frequency in Hz
     * @param hz Receive frequency value
     * @return Error code (0 on success)
     */
    virtual unsigned int setRxFreq(uint64_t hz) = 0;

    /**
     * Readback actual receive frequency
     * @param hz Receive frequency value
     * @return Error code (0 on success)
     */
    virtual unsigned int getRxFreq(uint64_t& hz) const = 0;

    /**
     * Set the transmit pre-mixer gain in dB wrt max
     * @return Error code (0 on success)
     */
    virtual unsigned int setTxGain1(int val, unsigned port)
	{ return NotSupported; }

    /**
     * Set the transmit post-mixer gain in dB wrt max
     * @return Error code (0 on success)
     */
    virtual unsigned int setTxGain2(int val, unsigned port)
	{ return NotSupported; }

    /**
     * Set the receive pre-mixer gain in dB wrt max
     * @return Error code (0 on success)
     */
    virtual unsigned int setRxGain1(int val, unsigned port)
	{ return NotSupported; }

    /**
     * Set the receive post-mixer gain in dB wrt max
     * @return Error code (0 on success)
     */
    virtual unsigned int setRxGain2(int val, unsigned port)
	{ return NotSupported; }

    /**
     * Automatic tx/rx gain setting
     * Set post mixer value. Return a value to be used by upper layer
     * @param tx Direction
     * @param val Value to set
     * @param port Port to use
     * @param newVal Optional pointer to value to use for calculation by the upper layer
     * @return Error code (0 on success)
     */
    virtual unsigned int setGain(bool tx, int val, unsigned int port,
	int* newVal = 0) const
	{ return NotSupported; }

    /**
     * Retrieve the interface name
     * @return Interface name
     */
    virtual const String& toString() const;

    /**
     * Retrieve the error string associated with a specific code
     * @param code Error code
     * @param defVal Optional default value to retrieve if not found
     * @return Valid TokenDict pointer
     */
    static inline const char* errorName(int code, const char* defVal = 0)
	{ return lookup(code,errorNameDict(),defVal); }

    /**
     * Retrieve the error name dictionary
     * @return Valid TokenDict pointer
     */
    static const TokenDict* errorNameDict();

protected:
    /**
     * Constructor
     * @param name Interface name
     */
    inline RadioInterface(const char* name)
	: m_lastErr(0), m_totalErr(0), m_radioCaps(0), m_name(name)
	{ debugName(m_name); }

    unsigned int m_lastErr;               // Last error that appeared during functioning
    unsigned int m_totalErr;              // All the errors that appeared
    RadioCapability* m_radioCaps;         // Radio capabilities

private:
    String m_name;
};


/**
 * @short Radio data file header
 * This class describes records in radio data files
 */
class YRADIO_API RadioDataDesc
{
public:
    /**
     * Samples data type
     */
    enum ElementType {
	Float = 0,
	Int16 = 1,
    };

    /**
     * Timestamp type
     */
    enum TsType {
	TsApp = 0,                       // Application level timestamp
	TsBoard = 1,                     // Board (device) level timestamp
    };

    /**
     * Constructor
     * @param eType Element type
     * @param tsType Racords timestamp type
     * @param sLen Sample length in elements
     * @param ports Number of device ports
     */
    inline RadioDataDesc(uint8_t eType = Float, uint8_t tsType = TsApp,
	uint8_t sLen = 2, uint8_t ports = 1)
	: m_elementType(eType), m_sampleLen(sLen), m_ports(ports ? ports : 1),
	m_tsType(tsType),
#ifdef LITTLE_ENDIAN
	m_littleEndian(true)
#else
	m_littleEndian(false)
#endif
	{
	    m_signature[0] = 'Y';
	    m_signature[1] = 'R';
	    m_signature[2] = 0;
	}

    uint8_t m_signature[3];              // File signature
    uint8_t m_elementType;               // Element data type
    uint8_t m_sampleLen;                 // Sample length in elements
    uint8_t m_ports;                     // The number of ports
    uint8_t m_tsType;                    // Records timestamp type
    bool m_littleEndian;                 // Endiannes
};


/**
 * @short Radio data file helper
 * This class implements utilities used to read or write radio data to/from file
 * The String contains object name used for debug
 */
class YRADIO_API RadioDataFile : public String
{
public:
    /**
     * Constructor
     * @param name Name used for debug
     * @param dropOnError Terminate on file/data error
     */
    RadioDataFile(const char* name, bool dropOnError = true);

    /**
     * Destructor
     */
    virtual ~RadioDataFile();

    /**
     * Retrieve data description
     * @return Data description object
     */
    inline const RadioDataDesc& desc() const
	{ return m_header; }

    /**
     * Check if enabled
     * @return True if enabled, false otherwise
     */
    inline bool valid() const
	{ return m_file.valid(); }

    /**
     * Check if machine endiannes is the same as file endiannes
     * @return True if they are the same
     */
    inline bool sameEndian() const
	{ return m_littleEndian == m_header.m_littleEndian; }

    /**
     * Open a file for read/write. Terminate current data dump if any.
     * Write or read the file header
     * @param fileName File to open
     * @param data Data description. Pass a NULL pointer for read or valid data for write
     * @param dbg Optional DebugEnabler pointer (show debug on failure)
     * @param error Optional destination for file operation error code
     * @return True on success, false on failure (read: error set to 0 means
     *  invalid file size, write: error set to 0 means invalid header)
     */
    bool open(const char* fileName, const RadioDataDesc* data,
	DebugEnabler* dbg = 0, int* error = 0);

    /**
     * Write a record to file
     * @param ts Record timestamp
     * @param buf Buffer to write
     * @param len Buffer length in bytes
     * @param dbg Optional DebugEnabler pointer (show debug on failure)
     * @param error Optional destination for file operation error code
     * @return True on success, false on failure (error set to 0 means invalid length)
     */
    bool write(uint64_t ts, const void* buf, uint32_t len, DebugEnabler* dbg = 0,
	int* error = 0);

    /**
     * Read a record from file
     * The method don't check the record length, this must be done by the upper layer
     * @param ts Record timestamp
     * @param buffer Destination buffer. It will be resized to read data length
     * @param dbg Optional DebugEnabler pointer (show debug on failure)
     * @param error Optional destination for file operation error code
     * @return True on success (empty buffer means file EOF),
     *  false on failure (error set to 0 means invalid record size)
     */
    bool read(uint64_t& ts, DataBlock& buffer, DebugEnabler* dbg = 0, int* error = 0);

    /**
     * Terminate data dump, close file
     * @param dbg Optional DebugEnabler pointer (show terminate message)
     */
    void terminate(DebugEnabler* dbg = 0);

    /**
     * Convert endiannes
     * @param buf The buffer to convert
     * @param bytes Element length in bytes
     * @return True on success, false if not supported
     */
    static bool fixEndian(DataBlock& buf, unsigned int bytes);

protected:
    bool ioError(bool send, DebugEnabler* dbg, int* error, const char* extra);

    bool m_littleEndian;                 // Machine endiannes
    bool m_dropOnError;                  // Terminate (close file) on error
    uint32_t m_chunkSize;                // Item size (used to check data validity)
    RadioDataDesc m_header;              // File header
    File m_file;                         // File to use
    DataBlock m_writeBuf;
};

}; // namespace TelEngine

#endif /* __YATERADIO_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
