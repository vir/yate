/**
 * ybladerf.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * BladeRF radio interface
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2015 Null Team
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

#include <yatephone.h>
#include <yateradio.h>
#include <libusb-1.0/libusb.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

//#define DEBUG_LUSB_TRANSFER_CALLBACK  // Debug libusb transfer callback
//#define DEBUGGER_DEVICE_METH          // Instantiate a Debugger to trace some device methods
//#define DEBUG_DEVICE_TX               // Special (verbose) TX debug
//#define DEBUG_DEVICE_RX               // Special (verbose) RX debug
//#define DEBUG_DEVICE_AUTOCAL          // Special (verbose) autocalibration debug

using namespace TelEngine;
namespace { // anonymous

class BrfLibUsbDevice;                   // A bladeRF device using libusb
class BrfInterface;                      // A bladeRF radio interface
class BrfModule;                         // The module

// GPIO
// Configure FPGA to send smaller buffers (USB 2)
#define BRF_GPIO_SMALL_DMA_XFER (1 << 7)

// SPI flash page size, in bytes
#define BRF_FLASH_PAGE_SIZE 256

#define SI5338_F_VCO (38400000UL * 66UL)

// Vendor commands
#define BRF_USB_CMD_QUERY_FPGA_STATUS         1
#define BRF_USB_CMD_BEGIN_PROG                2
#define BRF_USB_CMD_RF_RX                     4
#define BRF_USB_CMD_RF_TX                     5
#define BRF_USB_CMD_READ_CAL_CACHE          110

#define BRF_SAMPLERATE_MIN 80000u
#define BRF_SAMPLERATE_MAX 0xffffffffu

// Frequency bounds
#define BRF_FREQUENCY_MIN 232500000u
#define BRF_FREQUENCY_MAX 3800000000u

// Frequency offset interval
#define BRF_FREQ_OFFS_DEF 128
#define BRF_FREQ_OFFS_MIN 64
#define BRF_FREQ_OFFS_MAX 192

#define BRF_RXVGA1_GAIN_MIN     5
#define BRF_RXVGA1_GAIN_MAX     30
#define BRF_RXVGA2_GAIN_MIN     0
#define BRF_RXVGA2_GAIN_MAX     30
#define BRF_TXVGA1_GAIN_MIN     -35
#define BRF_TXVGA1_GAIN_MAX     -4
#define BRF_TXVGA1_GAIN_DEF     -14
#define BRF_TXVGA2_GAIN_MIN     0
#define BRF_TXVGA2_GAIN_MAX     25

#define VCO_HIGH 0x02
#define VCO_NORM 0x00
#define VCO_LOW 0x01

#define MAKE_TOKEN_NAME_PREFIX(PREFIX,x) {#x, PREFIX##x}

static inline bool brfIsLowBand(unsigned int hz)
{
    return hz < 1500000000u;
}

#define BRF_ALTSET_INVALID -1         // Invalid value
#define BRF_ALTSET_IDLE    0          // Used for idle mode
#define BRF_ALTSET_RF_LINK 1          // Send and receive samples. Also used for
                                      // sending commands to peripherals (VCTCXO, Si5338, LMS6002D)
#define BRF_ALTSET_SPI_FLASH 2        // Update the firmware on the board
#define BRF_ALTSET_FPGA    3          // FPGA operations
#define BRF_ALTSET_MIN BRF_ALTSET_IDLE
#define BRF_ALTSET_MAX BRF_ALTSET_FPGA
static const TokenDict s_altSetDict[] = {
    MAKE_TOKEN_NAME_PREFIX(BRF_ALTSET_,INVALID),
    MAKE_TOKEN_NAME_PREFIX(BRF_ALTSET_,IDLE),
    MAKE_TOKEN_NAME_PREFIX(BRF_ALTSET_,RF_LINK),
    MAKE_TOKEN_NAME_PREFIX(BRF_ALTSET_,SPI_FLASH),
    MAKE_TOKEN_NAME_PREFIX(BRF_ALTSET_,FPGA),
    {0,0}
};
static inline const char* altSetName(int val)
{
    return lookup(val,s_altSetDict);
}

// USB endpoints
#define BRF_ENDP_TX_SAMPLES 0x01       // Endpoint for TX data samples
#define BRF_ENDP_TX_CTRL    0x02       // Endpoint for ctrl RF
#define BRF_ENDP_RX_SAMPLES 0x81       // Endpoint for RX data samples
#define BRF_ENDP_RX_CTRL    0x82       // Endpoint for ctrl RF

// DC calibrate modules
// Auto calibration order is given in LMS6002D calibration guide, Section 4.7
#define BRF_CALIBRATE_LPF_TUNING    0  // DC offset cancellation of LPF
#define BRF_CALIBRATE_LPF_BANDWIDTH 1  // LPF bandwidth tunning
#define BRF_CALIBRATE_TX_LPF        2  // DC offset cancellation of TX LPF
#define BRF_CALIBRATE_RX_LPF        3  // DC offset cancellation of RX LPF
#define BRF_CALIBRATE_RX_VGA2       4  // DC offset cancellation of RX VGA2
#define BRF_CALIBRATE_FIRST         BRF_CALIBRATE_LPF_TUNING
#define BRF_CALIBRATE_LAST          BRF_CALIBRATE_RX_VGA2
#define BRF_CALIBRATE_MAX_SUBMODULES 5
static const TokenDict s_calModuleDict[] = {
    MAKE_TOKEN_NAME_PREFIX(BRF_CALIBRATE_,LPF_TUNING),
    MAKE_TOKEN_NAME_PREFIX(BRF_CALIBRATE_,LPF_BANDWIDTH),
    MAKE_TOKEN_NAME_PREFIX(BRF_CALIBRATE_,TX_LPF),
    MAKE_TOKEN_NAME_PREFIX(BRF_CALIBRATE_,RX_LPF),
    MAKE_TOKEN_NAME_PREFIX(BRF_CALIBRATE_,RX_VGA2),
    {0,0}
};
static inline const char* calModName(int val)
{
    return lookup(val,s_calModuleDict);
}
static const char* s_calRxTxLpfNames[] = {"DC_I","DC_Q"};
static const char* s_calRxVga2Names[] = {"VGA2_DC_REF","VGA2A_DC_I","VGA2A_DC_Q","VGA2B_DC_I","VGA2B_DC_Q"};
struct BrfCalDesc
{
    uint8_t clkEnMask;
    uint8_t addr;
    uint8_t subModules;
    const char** subModName;
};
static const BrfCalDesc s_calModuleDesc[] = {
    // BRF_CALIBRATE_LPF_TUNING
    {0x20,0x00,1,0},
    // BRF_CALIBRATE_LPF_BANDWIDTH
    {0,0,1,0},
    // BRF_CALIBRATE_TX_LPF
    {0x02,0x30,2,s_calRxTxLpfNames},
    // BRF_CALIBRATE_RX_LPF
    {0x08,0x50,2,s_calRxTxLpfNames},
    // BRF_CALIBRATE_RX_VGA2
    {0x10,0x60,5,s_calRxVga2Names}
};

// Maximum values for Rx/Tx DC offset I and Q
#define BRF_RX_DC_OFFSET_MAX 63
#define BRF_TX_DC_OFFSET_MIN -128
#define BRF_TX_DC_OFFSET_MAX 127
static inline int16_t decodeDCOffs(bool tx, uint8_t val)
{
    if (tx) {
	bool negative = ((val & 0x80) == 0);
	return negative ? ((int16_t)val - 128) : (int16_t)(val & 0x7f);
    }
    bool negative = ((val & 0x40) != 0);
    return negative ? -(int16_t)(val & 0x3f) : (int16_t)(val & 0x3f);
}

// Calculate Rx DC offset correction
#define BRF_RX_DC_OFFSET_ERROR 10
#define BRF_RX_DC_OFFSET_COEF 1.5
#define BRF_RX_DC_OFFSET_AVG_DAMPING 1024
#define BRF_RX_DC_OFFSET_DEF (BRF_RX_DC_OFFSET_ERROR * BRF_RX_DC_OFFSET_AVG_DAMPING)
static inline double brfRxDcOffset(double val)
{
    return (val * BRF_RX_DC_OFFSET_COEF + BRF_RX_DC_OFFSET_ERROR) *
	BRF_RX_DC_OFFSET_AVG_DAMPING;
}

// FPGA correction
#define BRF_FPGA_CORR_MAX 4096

// libusb defaults
#define LUSB_SYNC_TIMEOUT 50           // Sync transfer timeout def val (in milliseconds)
#define LUSB_CTRL_TIMEOUT 500          // Control transfer timeout def val (in milliseconds)
#define LUSB_BULK_TIMEOUT 500          // Bulk transfer timeout def val (in milliseconds)

// libusb control transfer
#define LUSB_CTRLTRANS_IFACE_VENDOR (LIBUSB_RECIPIENT_INTERFACE | LIBUSB_REQUEST_TYPE_VENDOR)
#define LUSB_CTRLTRANS_IFACE_VENDOR_IN (LUSB_CTRLTRANS_IFACE_VENDOR | LIBUSB_ENDPOINT_IN)
#define LUSB_CTRLTRANS_DEV_VENDOR (LIBUSB_RECIPIENT_DEVICE | LIBUSB_REQUEST_TYPE_VENDOR)
#define LUSB_CTRLTRANS_DEV_VENDOR_IN (LUSB_CTRLTRANS_DEV_VENDOR | LIBUSB_ENDPOINT_IN)
#define LUSB_CTRLTRANS_DEV_VENDOR_OUT (LUSB_CTRLTRANS_DEV_VENDOR | LIBUSB_ENDPOINT_OUT)

// Board reference clock (in Hz)
static const uint64_t s_freqRefClock = 38400000;

static inline unsigned int bytes2samplesf(unsigned int bytes)
{
    return bytes / (2 * sizeof(float));
}

static inline unsigned int samplesf2bytes(unsigned int samples)
{
    return samples * 2 * sizeof(float);
}

static inline unsigned int samplesi2bytes(unsigned int samples)
{
    return samples * 2 * sizeof(int16_t);
}

static inline const char* encloseDashes(String& s, bool extra = false)
{
    static const String s1 = "\r\n-----";
    if (s)
	s = s1 + (extra ? "\r\n" : "") + s + s1;
    return s.safe();
}

#define BRF_ARRAY_LEN(a) (sizeof(a) / sizeof(a[0]))

// Utility: check timeout or cancelled
static inline unsigned int checkCancelled(String* error = 0)
{
    if (!Thread::check(false))
	return 0;
    if (error)
	*error = "Cancelled";
    return RadioInterface::Cancelled;
}

static String& appendComplex(String& s, float* f, unsigned int n)
{
    char c[320];
    n /= 2;
    if (!(f && n))
	return s;
    String tmp;
    unsigned int a = n / 4;
    while (a--) {
	::sprintf(c,"(%.3f,%.3f) (%.3f,%.3f) (%.3f,%.3f) (%.3f,%.3f)",f[0],f[1],f[2],f[3],f[4],f[5],f[6],f[7]);
	f += 8;
	tmp.append(c," ");
    }
    a = n % 4;
    while (a--) {
	::sprintf(c,"(%.3f,%.3f)",f[0],f[1]);
	f += 2;
	tmp.append(c," ");
    }
    return s.append(tmp);
}

static inline String& appendComplex(String& s, DataBlock& d)
{
    return appendComplex(s,(float*)d.data(0),d.length() / sizeof(float));
}

static inline const char* onStr(bool on)
{
    return on ? "on" : "off";
}

static inline const char* enableStr(bool on)
{
    return on ? "enable" : "disable";
}

static inline const char* enabledStr(bool on)
{
    return on ? "Enabled" : "Disabled";
}

static inline const char* brfDir(bool tx)
{
    return tx ? "TX" : "RX";
}

static inline char mixer(bool pre)
{
    return pre ? '1' : '2';
}

static inline char brfIQ(bool i)
{
    return i ? 'I' : 'Q';
}

static inline const char* activeStr(bool on)
{
    return on ? "active" : "inactive";
}

static inline String& dumpFloatG(String& buf, double val, const char* prefix = 0,
    const char* suffix = 0)
{
    return buf.printf("%s%g%s",TelEngine::c_safe(prefix),val,TelEngine::c_safe(suffix));
}

inline String& addIntervalInt(String& s, int minVal, int maxVal, const char* sep = " ")
{
    String tmp;
    return s.append(tmp.printf("[%d..%d]",minVal,maxVal),sep);
}

static inline bool retMsgError(NamedList& list, const char* what, const char* param = 0)
{
    NamedString* ns = new NamedString("error",what);
    if (!TelEngine::null(param))
	*ns << " '" << param << "'";
    list.setParam(ns);
    return false;
}

static inline bool retParamError(NamedList& list, const char* param)
{
    if (list.getParam(param))
	return retMsgError(list,"Missing parameter",param);
    return retMsgError(list,"Invalid parameter",param);
}

static inline bool retValFailure(NamedList& list, unsigned int code)
{
    return retMsgError(list,String(code) + " " + RadioInterface::errorName(code));
}

static inline bool getFirstStr(String& dest, String& line)
{
    int pos = line.find(' ');
    if (pos >= 0) {
	dest = line.substr(0,pos);
	line = line.substr(pos + 1);
    }
    else {
	dest = line;
	line.clear();
    }
    return !dest.null();
}

// Convert 4 bytes to version string (MSB -> LSB: patch.minor.major)
static inline void ver2str(String& dest, uint32_t ver)
{
    dest << (uint8_t)ver << ".";
    dest << (uint8_t)(ver >> 8) << ".";
    uint16_t patch = (uint8_t)(ver >> 16) | (uint8_t)(ver >> 24);
    dest << patch;
}

// Code is expecting this array to have 15 elements
#define BRF_FILTER_BW_MIN 1500000u
#define BRF_FILTER_BW_MAX 20000000u
static uint32_t s_bandSet[] = {
    BRF_FILTER_BW_MIN, 1750000u, 2500000u, 2750000u, 3000000u,
    3840000u,          5000000u, 5500000u, 6000000u, 7000000u,
    8750000u,          10000000u,12000000u,14000000u,BRF_FILTER_BW_MAX
};

#if 0
static uint32_t s_freqLimits[] = {
    BRF_FREQUENCY_MIN, 285625000,         0x27,
    285625000,         336875000,         0x2f,
    336875000,         405000000,         0x37,
    405000000,         465000000,         0x3f,
    465000000,         571250000,         0x26,
    571250000,         673750000,         0x2e,
    673750000,         810000000,         0x36,
    810000000,         930000000,         0x3e,
    930000000,         1142500000,        0x25,
    1142500000,        1347500000,        0x2d,
    1347500000,        1620000000,        0x35,
    1620000000,        1860000000,        0x3d,
    1860000000u,       2285000000u,       0x24,
    2285000000u,       2695000000u,       0x2c,
    2695000000u,       3240000000u,       0x34,
    3240000000u,       BRF_FREQUENCY_MAX, 0x3c,
    0,0,0
};
#else
static uint32_t s_freqLimits[] = {
    BRF_FREQUENCY_MIN, 285625000,         0x27,
    285625000,         336875000,         0x2f,
    336875000,         405000000,         0x37,
    405000000,         475000000,         0x3f,
    475000000,         571250000,         0x26,
    571250000,         673750000,         0x2e,
    673750000,         810000000,         0x36,
    810000000,         945000000,         0x3e,
    945000000,         1142500000,        0x25,
    1142500000,        1350000000,        0x2d,
    1350000000,        1620000000,        0x35,
    1620000000,        1890000000,        0x3d,
    1890000000u,       2285000000u,       0x24,
    2285000000u,       2695000000u,       0x2c,
    2695000000u,       3240000000u,       0x34,
    3240000000u,       BRF_FREQUENCY_MAX, 0x3c,
    0,0,0
};
#endif

struct BrfRationalRate
{
    uint64_t integer;
    uint64_t numerator;
    uint64_t denominator;
};

struct Si5338MultiSynth
{
    uint8_t index;                       // Multisynth to program (0-3)
    uint16_t base;                       // Base address of the multisynth
    BrfRationalRate requested;           // Requested sample rates
    BrfRationalRate actual;              // Actual sample rates
    uint8_t enable;                      // Enables for A and/or B outputs
    uint32_t a, b, c, r;                 // f_out = fvco / (a + b/c) / r
    uint32_t p1, p2, p3;                 // (a, b, c) in multisynth (p1, p2, p3) form
    uint8_t regs[10];                    // p1, p2, p3) in register form
};

static const uint8_t s_rxvga1_set[BRF_RXVGA1_GAIN_MAX + 1] = {
    2,  2,  2,  2,   2,   2,   14,  26,  37,  47,  56,  63,  70,  76,  82,  87,
    91, 95, 99, 102, 104, 107, 109, 111, 113, 114, 116, 117, 118, 119, 120,
};

static const uint8_t s_rxvga1_get[121] = {
    5,  5,  5,  5,  5,  5,  5,  5,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
    6,  6,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  8,  8,  8,  8,  8,  8,
    8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  9,  9,  9,  9,  10, 10, 10, 10, 10,
    10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 13, 13,
    13, 13, 13, 13, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 16, 16, 16, 16, 17,
    17, 17, 18, 18, 18, 18, 19, 19, 19, 20, 20, 21, 21, 22, 22, 22, 23, 24, 24,
    25, 25, 26, 27, 28, 29, 30
};

// Init radio caps with default values
static void initRadioCaps(RadioCapability& caps)
{
    caps.maxPorts = 1;
    caps.currPorts = 1;
    caps.maxTuneFreq = BRF_FREQUENCY_MAX;
    caps.minTuneFreq = BRF_FREQUENCY_MIN;
    caps.maxSampleRate = BRF_SAMPLERATE_MAX;
    caps.minSampleRate = BRF_SAMPLERATE_MIN;
    caps.maxFilterBandwidth = BRF_FILTER_BW_MAX;
    caps.minFilterBandwidth = BRF_FILTER_BW_MIN;
}

// Utility macros (call methods)
#define BRF_FUNC_CALL_(result,func,cond,instr) { \
    result = func; \
    if (cond) { instr; } \
}
// Call a method, remember the first error
#define BRF_FUNC_CALL(func) BRF_FUNC_CALL_(unsigned int tmp,func,!status && tmp,status = tmp; error = 0)
#define BRF_FUNC_CALL_BREAK(func) BRF_FUNC_CALL_(status,func,status,break)
#define BRF_FUNC_CALL_RET(func) BRF_FUNC_CALL_(status,func,status,return status)

// Read samples from string
static bool readSamples(DataBlock& buf, const String& list)
{
    ObjList* l = list.split(',');
    unsigned int n = l->count();
    if (n < 2 || (n % 2) != 0) {
	TelEngine::destruct(l);
	return false;
    }
    buf.resize(n * sizeof(float));
    float* f = (float*)buf.data(0);
    for (ObjList* o = l->skipNull(); f && o; o = o->skipNext()) {
	*f = static_cast<String*>(o->get())->toDouble();
	if (*f >= -1 && *f <= 1)
	    f++;
	else
	    f = 0;
    }
    TelEngine::destruct(l);
    if (!f)
	buf.clear();
    return f != 0;
}

static int16_t s_sampleEnergize = 2047;

// Energize a number. Refer the input value to the requested energy
static inline int16_t sampleScale(float value, float scale)
{
    value *= scale;
    return (int16_t)((value >= 0.0F) ? (value + 0.5F) : (value - 0.5F));
}
static inline int16_t energize(float value, float scale, int16_t refVal, unsigned int& clamp)
{
    int16_t v = sampleScale(value,scale);
    if (v > refVal) {
	clamp++;
	return refVal;
    }
    if (v < -refVal) {
	clamp++;
	return -refVal;
    }
    return v;
}
static inline void brfCopyTxData(int16_t* dest, float* src, unsigned int samples,
    float scaleI, int16_t maxI, float scaleQ, int16_t maxQ, unsigned int& clamped)
{
    for (; samples; samples--) {
	*dest++ = htole16(energize(*src++,scaleI,maxI,clamped));
	*dest++ = htole16(energize(*src++,scaleQ,maxQ,clamped));
    }
}

class BrfDuration
{
public:
    inline BrfDuration(uint64_t start = Time::now())
	: m_start(start), m_durationUs(0)
	{}
    inline void stop() {
	    if (!m_durationUs)
		m_durationUs = Time::now() - m_start;
	}
    inline const char* secStr() {
	    stop();
	    m_str.printf("%u.%usec",(unsigned int)(m_durationUs / 1000000),
		(unsigned int)(m_durationUs % 1000000) / 1000);
	    return m_str;
	}

protected:
    uint64_t m_start;
    uint64_t m_durationUs;
    String m_str;
};

class BrfPeripheral : public String
{
public:
    inline BrfPeripheral(const char* name, uint8_t devId)
	: String(name),
	m_devId(devId), m_tx(false), m_rx(false), m_haveTrackAddr(false) {
	    lowCase = name;
	    lowCase.toLower();
	    set(false,false);
	}
    inline uint8_t devId() const
	{ return m_devId; }
    inline bool trackDir(bool tx) const
	{ return tx ? m_tx : m_rx; }
    inline bool haveTrackAddr() const
	{ return m_haveTrackAddr; }
    void set(bool tx, bool rx, const String& addr = String::empty());
    // Check for addr track range, return first match addr or -1 if not found
    inline int isTrackRange(uint8_t addr, uint8_t len) const {
	    for (; addr < sizeof(m_trackAddr) && len; len--, addr++)
		if (m_trackAddr[addr])
		    return addr;
	    return -1;
	}
    inline bool isTrackAddr(uint8_t addr) const
	{ return addr < sizeof(m_trackAddr) && m_trackAddr[addr]; }

    String lowCase;

protected:
    uint8_t m_devId;                     // Device id
    bool m_tx;
    bool m_rx;
    bool m_haveTrackAddr;
    uint8_t m_trackAddr[128];
};

// Device calibration data
class BrfCalData
{
public:
    inline BrfCalData(int mod) {
	    ::memset(this,0,sizeof(BrfCalData));
	    module = mod;
	    desc = &s_calModuleDesc[module];
	}
    inline const char* modName() const
	{ return calModName(module); }
    int module;
    const BrfCalDesc* desc;
    uint8_t clkEn;
    uint8_t inputMixer;
    uint8_t loOpt;
    uint8_t lnaGain;
    int rxVga1;
    int rxVga2;
    uint8_t rxVga2GainAB;
    // LPF_BANDWIDTH
    uint8_t txVGA2PwAmp;
    uint8_t txPPL;
    uint8_t enLPFCAL;
    uint8_t clkLPFCAL;
    uint8_t nInt;
    uint8_t nFrac1;
    uint8_t nFrac2;
    uint8_t nFrac3;
};

// libusb transfer
class LusbTransfer : public Mutex
{
public:
    inline LusbTransfer(BrfLibUsbDevice* dev = 0)
	: Mutex(false,"LusbTransfer"),
	device(dev), ep(255), transfer(0), status(0), m_running(false)
	{}
    inline ~LusbTransfer()
	{ reset(); }
    inline bool running() const
	{ return m_running; }
    inline void running(bool start) {
	    m_running = start;
	    if (m_running) {
		status = 0;
		error.clear();
	    }
	}
    inline bool alloc() {
	    if (transfer)
		return true;
	    transfer = ::libusb_alloc_transfer(0);
	    if (transfer)
		return true;
	    error = "Failed to allocate libusb transfer";
	    status = RadioInterface::Failure;
	    return false;
	}
    inline void reset() {
	    cancel();
	    if (transfer)
		::libusb_free_transfer(transfer);
	    transfer = 0;
	    m_running = false;
	}
    // Fill bulk transfer. Allocate if not already done
    // Return true on success (change state to Init)
    bool fillBulk(uint8_t* data, unsigned int len, unsigned int tout);
    bool submit();
    unsigned int cancel(String* error = 0);

    BrfLibUsbDevice* device;
    uint8_t ep;
    libusb_transfer* transfer;
    unsigned int status;
    String error;
protected:
    bool m_running;
};

// Holds RX/TX related data
// Hold samples read/write related data
class BrfDevStatus
{
public:
    inline BrfDevStatus(bool tx)
	: rfEnabled(false), frequency(0), vga1(0), vga1Changed(false), vga2(0), lpf(0),
	dcOffsetI(0), dcOffsetQ(0), fpgaCorrPhase(0), fpgaCorrGain(0),
	powerBalance(0), lpfBw(0), sampleRate(0),
	m_tx(tx)
	{}
    inline BrfDevStatus(const BrfDevStatus& src)
	{ *this = src; }
    inline BrfDevStatus& operator=(const BrfDevStatus& src) {
	    ::memcpy(this,&src,sizeof(src));
	    return *this;
	}
    inline bool tx() const
	{ return m_tx; }
    bool rfEnabled;                      // RF enabled flag
    unsigned int frequency;              // Used frequency
    unsigned int freqOffset;             // Used frequency offset
    int vga1;                            // VGA1 gain
    bool vga1Changed;                    // VGA1 was set by us
    int vga2;                            // VGA2 gain
    int lpf;                             // LPF status
    int dcOffsetI;                       // Current I (in-phase) DC offset
    int dcOffsetQ;                       // Current Q (quadrature) DC offset
    int fpgaCorrPhase;                   // Current FPGA phase correction
    int fpgaCorrGain;                    // Current FPGA gain correction
    float powerBalance;                  // Current power balance
    unsigned int lpfBw;                  // LPF bandwidth
    unsigned int sampleRate;             // Sampling rate
protected:
    bool m_tx;                           // Direction
};

// Holds RX/TX related data
// Hold samples read/write related data
class BrfDevIO : public BrfDevStatus
{
public:
    inline BrfDevIO(bool tx)
	: BrfDevStatus(tx),
	showBuf(0), showBufData(true), checkTs(0),
	mutex(false,tx ? "BrfDevIoTx" : "BrfDevIoRx"),
	showDcOffsChange(0), showFpgaPhaseChange(0), showPowerBalanceChange(0),
	startTime(0), transferred(0),
	timestamp(0), lastTs(0), buffers(0), hdrLen(0), bufSamples(0),
	bufSamplesLen(0), crtBuf(0), crtBufSampOffs(0), newBuffer(true),
	syncFlags(0), syncTs(0), syncStatus(tx),
	dataDumpParams(""), dataDump(0), dataDumpFile(brfDir(tx)),
	upDumpParams(""), upDump(0), upDumpFile(String(brfDir(tx)) + "-APP"),
#ifdef LITTLE_ENDIAN
	m_bufEndianOk(true)
#else
	m_bufEndianOk(false)
#endif
	{}
    void resetSamplesBuffer(unsigned int nSamples, unsigned int hLen,
	unsigned int nBuffers = 1) {
	    bufSamples = nSamples;
	    bufSamplesLen = samplesi2bytes(bufSamples);
	    hdrLen = hLen;
	    bufLen = hdrLen + bufSamplesLen;
	    buffers = nBuffers ? nBuffers : 1;
	    buffer.assign(0,buffers * bufLen);
	    resetPosTime();
	}
    inline void resetPosTime() {
	    resetBufPos(m_tx);
	    timestamp = 0;
	    lastTs = 0;
	    startTime = 0;
	    transferred = 0;
	}
    inline void setRf(bool on) {
	    rfEnabled = on;
	    syncFlags = 0;
	    resetPosTime();
	}
    inline bool advanceBuffer() {
	    if (crtBuf < buffers)
		setCrtBuf(crtBuf + 1);
	    newBuffer = true;
	    return crtBuf < buffers;
	}
    inline uint8_t* bufStart(unsigned int index)
	{ return buffer.data(index * bufLen); }
    inline int16_t* samples(unsigned int index)
	{ return (int16_t*)(bufStart(index) + hdrLen); }
    inline int16_t* samplesEOF(unsigned int index)
	{ return (int16_t*)(bufStart(index) + bufLen); }
    // Retrieve a pointer to current buffer samples start (including offset)
    //  and available samples number
    inline int16_t* crtBufSamples(unsigned int& avail) {
	    avail = bufSamples - crtBufSampOffs;
	    return samples(crtBuf) + crtBufSampOffs * 2;
	}
    inline uint64_t bufTs(unsigned int index) {
	    // Skip reserved
	    uint32_t* u = (uint32_t*)(bufStart(index) + 4);
	    // Get timestamp (LOW32 + HI32, little endian)
	    uint64_t ts = le32toh((uint32_t)(*u++ >> 1));
	    return ts | ((uint64_t)le32toh(*u) << 31);
	}
    inline void setBufTs(unsigned int index, uint64_t ts) {
	    uint32_t* u = (uint32_t*)(bufStart(index));
	    *u++ = htole32(0xdeadbeef);
	    *u++ = htole32((uint32_t)(ts << 1));
	    *u++ = htole32((uint32_t)(ts >> 31));
	    *u = htole32((uint32_t)-1);
	}
    inline void resetBufPos(bool start = true) {
#ifndef LITTLE_ENDIAN
	    m_bufEndianOk = false;
#endif
	    setCrtBuf(start ? 0 : buffers);
	    newBuffer = true;
	}
#ifdef LITTLE_ENDIAN
    inline void fixEndian()
	{}
#else
    inline void fixEndian() {
	    if (m_bufEndianOk)
		return;
	    m_bufEndianOk = true;
	    uint8_t* d = buffer.data(0);
	    for (unsigned int i = 0; i < buffers; i++) {
		d += hdrLen;
		for (uint8_t* last = d + bufSamplesLen; d != last; d += 2) {
		    uint8_t tmp = *d;
		    *d = d[1];
		    d[1] = tmp;
		}
	    }
	}
#endif
    inline void dumpInt16Samples(String& s, unsigned int index, unsigned int sampOffs = 0,
	int nSamples = -1) {
	    int16_t* p = samples(index) + sampOffs * 2;
	    unsigned int n = bufSamples - sampOffs;
	    if (nSamples > 0 && nSamples < (int)n)
		n = nSamples;
	    for (int16_t* last = (p + n * 2); p != last; p += 2) {
		if (s)
		    s << " ";
		s << p[0] << "," << p[1];
	    }
	}

    int showBuf;                         // Show buffers
    bool showBufData;                    // Display buffer data
    int checkTs;                         // Check IO buffers timestamp
    Mutex mutex;                         // Protect data changes when needed
    unsigned int showDcOffsChange;       // Show DC offset changed debug message
    unsigned int showFpgaPhaseChange;    // Show FPGA PHASE changed debug message
    unsigned int showPowerBalanceChange; // Show power balance changed debug message
    uint64_t startTime;                  // Absolute time for start (first TX/RX)
    uint64_t transferred;                // The number of samples transferred
    // TX/RX data
    uint64_t timestamp;                  // Last timestamp to/from device
    uint64_t lastTs;                     // Last buffer timestamp (used when checking)
    unsigned int buffers;                // The number of buffers
    unsigned int hdrLen;                 // Header length in bytes
    unsigned int bufSamples;             // Length of a single buffer in samples (without header)
    unsigned int bufSamplesLen;          // Length of a single buffer in bytes (without header)
    unsigned int bufLen;                 // Length of a single buffer (in bytes)
    unsigned int crtBuf;                 // Current buffer
    unsigned int crtBufSampOffs;         // Current buffer samples offset
    bool newBuffer;                      // New buffer to process
    DataBlock buffer;                    // I/O buffer
    // Sync timestamp
    unsigned int syncFlags;
    uint64_t syncTs;
    BrfDevStatus syncStatus;
    // File dump
    NamedList dataDumpParams;
    int dataDump;
    RadioDataFile dataDumpFile;
    NamedList upDumpParams;
    int upDump;
    RadioDataFile upDumpFile;

protected:
    // Reset current buffer to start
    inline void setCrtBuf(unsigned int index = 0) {
	    crtBuf = index;
	    crtBufSampOffs = 0;
	}
    bool m_bufEndianOk;
};

// Temporary change alt setting. Restore on destruction
class BrfDevTmpAltSet
{
public:
    inline BrfDevTmpAltSet(BrfLibUsbDevice* dev)
	: m_device(dev), m_oper(0), m_tmpAltSet(BRF_ALTSET_INVALID)
	{}
    inline BrfDevTmpAltSet(BrfLibUsbDevice* dev, int altSet,
	unsigned int& status, String* error, const char* oper)
	: m_device(dev), m_oper(0), m_tmpAltSet(BRF_ALTSET_INVALID)
	{ status = set(altSet,error,oper); }
    // Temporary change to RF_LINK
    inline BrfDevTmpAltSet(BrfLibUsbDevice* dev, unsigned int& status, String* error,
	const char* oper)
	: m_device(dev), m_oper(0), m_tmpAltSet(BRF_ALTSET_INVALID)
	{ status = set(BRF_ALTSET_RF_LINK,error,oper); }
    inline ~BrfDevTmpAltSet()
	{ restore(); }
    unsigned int set(int altSet, String* error, const char* oper);
    inline unsigned int set(String* error, const char* oper)
	{ return set(BRF_ALTSET_RF_LINK,error,oper); }
    unsigned int restore();
private:
    BrfLibUsbDevice* m_device;
    const char* m_oper;
    int m_tmpAltSet;
};

class BrfThread;
class BrfSerialize;

class BrfLibUsbDevice : public GenObject
{
    friend class BrfDevTmpAltSet;
    friend class BrfThread;
    friend class BrfModule;
    friend class BrfSerialize;
public:
    enum UartDev {
	UartDevGPIO = 0,
	UartDevLMS,
	UartDevVCTCXO,
	UartDevSI5338,
	UartDevCount
    };
    enum Endpoint {
	EpSendSamples = 0,
	EpSendCtrl,
	EpReadSamples,
	EpReadCtrl,
	EpCount
    };
    // LNA selection
    enum LmsLna {
	LmsLnaNone = 0,                  // Disable all LNAs
	LmsLna1,                         // Enable LNA1 (300MHz - 2.8GHz)
	LmsLna2,                         // Enable LNA2 (1.5GHz - 3.8GHz)
	LmsLna3                          // Enable LNA3 (Unused on the bladeRF)
    };
    // PA Selection
    enum LmsPa {
	LmsPaNone = 0,                   // All PAs disabled
	LmsPa1,                          // PA1 Enable (300MHz - 2.8GHz)
	LmsPa2,                          // PA2 Enable (1.5GHz - 3.8GHz)
	LmsPaAux,                        // AUX PA Enable (for RF Loopback)
    };
    // LNA gain values
    enum LnaGain {
	LnaGainUnhandled = 0,
	LnaGainBypass = 1,
	LnaGainMid = 2,
	LnaGainMax = 3,
    };
    enum CorrectionType {
	CorrLmsI,
	CorrLmsQ,
	CorrFpgaPhase,
	CorrFpgaGain,
    };
    // Loopback mode
    enum Loopback {
	LoopNone = 0,                    // Disabled
	LoopFirmware,                    // Firmware loopback
	LoopLpfToRxOut,                  // Baseband: TX LPF -> RX out
	LoopLpfToVga2,                   // Baseband: TX LPF -> RX VGA2
	LoopVga1ToVga2,                  // Baseband: TX VGA1 -> RX VGA2
	LoopLpfToLpf,                    // Baseband: TX LPF -> RX LPF
	LoopVga1ToLpf,                   // Baseband: TX VGA1 -> RX LPF
	LoopRfLna1,                      // RF: mixer after PA -> LNA1
	LoopRfLna2,                      // RF: mixer after PA -> LNA2
	LoopRfLna3,                      // RF: mixer after PA -> LNA3
	LoopUnknown
    };
    enum Lpf {
	LpfInvalid = 0,
	LpfDisabled = 1,
	LpfBypass,
	LpfNormal,
    };
    // Flags used to restore dev status
    enum StatusFlags {
	DevStatFreq = 0x0001,
	DevStatVga1 = 0x0002,
	DevStatVga2 = 0x0004,
	DevStatLpf = 0x0008,
	DevStatDcI = 0x0010,
	DevStatDcQ = 0x0020,
	DevStatLpfBw = 0x0040,
	DevStatSampleRate = 0x0080,
	DevStatFpgaPhase = 0x0100,
	DevStatFpgaGain = 0x0200,
	DevStatPowerBalance = 0x0400,
	DevStatTs = 0x1000,
	DevStatVga = DevStatVga1 | DevStatVga2,
	DevStatDc = DevStatDcI | DevStatDcQ,
    };
    ~BrfLibUsbDevice();
    inline BrfInterface* owner() const
	{ return m_owner; }
    inline libusb_device_handle* handle() const
	{ return m_devHandle; }
    inline RadioCapability& capabilities()
	{ return m_radioCaps; }
    inline bool validPort(unsigned int port) const
	{ return port < m_radioCaps.currPorts; }
    inline int speed() const
	{ return m_devSpeed; }
    inline int bus() const
	{ return m_devBus; }
    inline int addr() const
	{ return m_devAddr; }
    inline const String& serial() const
	{ return m_devSerial; }
    inline const String& fwVerStr() const
	{ return m_devFwVerStr; }
    inline const String& fpgaFile() const
	{ return m_devFpgaFile; }
    inline const String& fpgaMD5() const
	{ return m_devFpgaMD5; }
    inline const String& fpgaVerStr() const
	{ return m_devFpgaVerStr; }
    inline bool exiting() const
	{ return m_exiting; }
    inline void exiting(bool on)
	{ m_exiting = on; }
    inline bool closing() const
	{ return m_closing; }
    inline unsigned int cancelled(String* error = 0) {
	    if (exiting() || closing()) {
		if (error)
		    *error = "Exiting";
		return RadioInterface::Cancelled;
	    }
	    return checkCancelled(error);
	}
    inline int showBuf(bool tx, int val, bool tsOnly) {
	    Lock lck(m_dbgMutex);
	    getIO(tx).showBufData = !tsOnly;
	    return (getIO(tx).showBuf = val);
	}
    inline int checkTs(bool tx, int val) {
	    Lock lck(m_dbgMutex);
	    return (getIO(tx).checkTs = val);
	}
    inline int showRxDCInfo(int val) {
	    Lock lck(m_dbgMutex);
	    return (m_rxShowDcInfo = val);
	}
    inline unsigned int bufSamples(bool tx)
	{ return getIO(tx).bufSamples; }
    inline unsigned int bufCount(bool tx)
	{ return getIO(tx).buffers; }
    inline unsigned int totalSamples(bool tx) {
	    BrfDevIO& io = getIO(tx);
	    return io.buffers * io.bufSamples;
	}
    unsigned int setTxPattern(const String& pattern);
    void dumpStats(String& buf, const char* sep);
    void dumpTimestamps(String& buf, const char* sep);
    void dumpDev(String& buf, bool info, bool state, const char* sep,
	bool fromStatus = false, bool withHdr = true);
    void dumpBoardStatus(String& buf, const char* sep);
    unsigned int dumpPeripheral(uint8_t dev, uint8_t addr, uint8_t len, String* buf = 0);
    // Module reload
    void reLoad(const NamedList* params = 0);
    void setDataDump(int dir = 0, int level = 0, const NamedList* params = 0);
    // Initialize the device.
    // Call the reset method in order to set the device to a known state
    bool open(const NamedList& params);
    // Close the device.
    void close();
    // Power on the radio
    // Enable timestamps, enable RF TX/RX
    unsigned int powerOn();
    // Send an array of samples waiting to be transmitted
    // samples: The number of I/Q samples (i.e. half buffer lengh)
    unsigned int syncTx(uint64_t ts, float* data, unsigned int samples,
	float* powerScale = 0);
    // Receive data from the Rx interface of the bladeRF device
    // samples: The number of I/Q samples (i.e. half buffer lengh)
    unsigned int syncRx(uint64_t& ts, float* data, unsigned int& samples);
    // Set the frequency on the Tx or Rx side
    unsigned int setFrequency(uint32_t hz, bool tx);
    // Retrieve frequency
    unsigned int getFrequency(uint32_t& hz, bool tx);
    // Set frequency offset
    unsigned int setFreqOffset(int offs, int* newVal = 0);
    // Get frequency offset
    unsigned int getFreqOffset(int& offs);
    // Set the LPF bandwidth for a specific module
    unsigned int setLpfBandwidth(uint32_t band, bool tx);
    // Get the LPF bandwidth for a specific module
    unsigned int getLpfBandwidth(uint32_t& band, bool tx);
    // LPF set/get
    unsigned int setLpf(int lpf, bool tx);
    unsigned int getLpf(int& lpf, bool tx);
    // Set the sample rate on a specific module
    unsigned int setSamplerate(uint32_t value, bool tx);
    // Get the sample rate on a specific module
    unsigned int getSamplerate(uint32_t& value, bool tx);
    // Set the pre-mixer gain on transmission (interval [-35..-4])
    // Set the post-mixer gain setting on transmission (interval: [0..25])
    unsigned int setTxVga(int vga, bool preMixer);
    // Set the post-mixer gain setting on transmission (interval: [0..25])
    inline unsigned int setTxVga1(int vga)
	{ return setTxVga(vga,true); }
    inline unsigned int setTxVga2(int vga)
	{ return setTxVga(vga,false); }
    // Retrieve the pre/post mixer gain setting on transmission
    unsigned int getTxVga(int& vga, bool preMixer);
    inline unsigned int getTxVga1(int& vga)
	{ return getTxVga(vga,true); }
    inline unsigned int getTxVga2(int& vga)
	{ return getTxVga(vga,false); }
    // Set TX power balance
    unsigned int setTxIQBalance(float value);
    // Enable or disable the pre/post mixer gain on the receive side
    unsigned int enableRxVga(bool on, bool preMixer);
    inline unsigned int enableRxVga1(bool on)
	{ return enableRxVga(on,true); }
    inline unsigned int enableRxVga2(bool on)
	{ return enableRxVga(on,false); }
    // Set the pre-mixer RX gain setting on the receive side (interval [5..30])
    // Set the post-mixer RX gain setting (interval [0..30])
    unsigned int setRxVga(int vga, bool preMixer);
    inline unsigned int setRxVga1(int vga)
	{ return setRxVga(vga,true); }
    inline unsigned int setRxVga2(int vga)
	{ return setRxVga(vga,false); }
    // Retrieve the pre/post mixer rx gain setting
    unsigned int getRxVga(int& vga, bool preMixer);
    inline unsigned int getRxVga1(int& vga)
	{ return getRxVga(vga,true); }
    inline unsigned int getRxVga2(int& vga)
	{ return getRxVga(vga,false); }
    // Set pre and post mixer value
    unsigned int setGain(bool tx, int val, int* newVal = 0);
    // Auto calibrate
    unsigned int calibrate();
    // Set Tx/Rx DC I/Q offset correction
    unsigned int setDcOffset(bool tx, bool i, int16_t value);
    // Retrieve Tx/Rx DC I/Q offset correction
    unsigned int getDcOffset(bool tx, bool i, int16_t& value);
    // Set/Get FPGA correction
    unsigned int setFpgaCorr(bool tx, int corr, int16_t value);
    unsigned int getFpgaCorr(bool tx, int corr, int16_t& value);
    // Retrieve TX/RX timestamp
    unsigned int getTimestamp(bool tx, uint64_t& ts);
    // Write LMS register
    unsigned int writeLMS(uint8_t addr, uint8_t value, uint8_t* rst = 0);
    // Enable or disable loopback
    unsigned int setLoopback(const char* name = 0);
    unsigned int setLoopback(int mode = 0);
    // Release data
    virtual void destruct();
    // Create an interface
    static BrfLibUsbDevice* create(BrfInterface* owner);
    // Utilities
    static const char* speedStr(int speed) {
	    switch (speed) {
		case LIBUSB_SPEED_SUPER:
		    return "SUPER";
		case LIBUSB_SPEED_HIGH:
		    return "HIGH";
	    }
	    return "Unknown";
	}
    static uint64_t reduceFurther(uint64_t v1, uint64_t v2);
    static void reduceRational(BrfRationalRate& rate);
    static inline void rationalDouble(BrfRationalRate& rate) {
	    rate.integer *= 2;
	    rate.numerator *= 2;
	    reduceRational(rate);
	}
    static void calcSrate(Si5338MultiSynth& synth, BrfRationalRate& rate);
    static unsigned int calcMultiSynth(Si5338MultiSynth& synth,
	BrfRationalRate& rate, String* error = 0);
    static void packRegs(Si5338MultiSynth& synth);
    static void unpackRegs(Si5338MultiSynth& synth);
    // Set error string
    static inline unsigned int setError(unsigned int code, String* buf,
	const char* error, const char* prefix = 0) {
	    if (!(buf && code))
		return code;
	    String tmp = prefix;
	    tmp.append((error && *error) ? error : RadioInterface::errorName(code)," - ");
	    buf->append(tmp," - ");
	    return code;
	}
    static inline unsigned int setUnkValue(String& buf, const char* unsupp = 0,
	const char* invalid = 0) {
	    if (unsupp)
		buf << "Unsupported " << unsupp;
	    else if (invalid)
		buf << "Invalid " << invalid;
	    else
		buf << "Unknown value";
	    return RadioInterface::OutOfRange;
	}
    static inline unsigned int setUnhandled(String& buf, int val, const char* what = 0) {
	    buf << "Unhandled";
	    buf.append(what," ");
	    buf << val;
	    return RadioInterface::OutOfRange;
	}
    // Print libusb error
    static inline String& appendLusbError(String& buf, int code, const char* prefix = 0) {
	    buf << prefix << "(" << code << " '" << ::libusb_error_name(code) << "')";
	    return buf;
	}
    static unsigned int lusb2ifaceError(int code);
    // Utility: check libusb result against LIBUSB_SUCCESS
    // Print libusb result to string on failure and return radio iface code
    static inline unsigned int lusbCheckSuccess(int code, String* error,
	const char* prefix = 0) {
	    if (code == LIBUSB_TRANSFER_COMPLETED || code == LIBUSB_SUCCESS)
		return 0;
	    if (error)
		appendLusbError(*error,code,prefix);
	    return lusb2ifaceError(code);
	}
    // Retrieve UART addr for FPGA correction
    static inline int fpgaCorrAddr(bool tx, int corr, String& what) {
	    if (corr == CorrFpgaPhase) {
		what = "PHASE";
		return tx ? 10 : 6;
	    }
	    if (corr == CorrFpgaGain) {
		what = "GAIN";
		return tx ? 8 : 4;
	    }
	    what = corr;
	    return -1;
	}
    // Retrieve LMS addr for I/Q correction
    static inline uint8_t lmsCorrIQAddr(bool tx, bool i) {
	    if (tx)
		return i ? 0x42 : 0x43;
	    return i ? 0x71 : 0x72;
	}
    // Retrieve LMS addr for TX/RX VGA 1/2
    static inline uint8_t lmsVgaAddr(bool tx, bool preMixer) {
	    if (tx)
		return preMixer ? 0x41 : 0x45;
	    return preMixer ? 0x76 : 0x65;
	}
    // Retrieve LMS LPF base address
    static inline uint8_t lmsLpfAddr(bool tx)
	{ return tx ? 0x34 : 0x54; }
    // Retrieve LMS PLL freq config addr
    static inline uint8_t lmsFreqAddr(bool tx)
	{ return tx ? 0x10 : 0x20; }

private:
    BrfLibUsbDevice(BrfInterface* owner);
    inline void doClose() {
	    m_closing = true;
	    closeDevice();
	    clearDeviceList();
	    m_closing = false;
	}
    unsigned int setStatus(const BrfDevStatus& stat, unsigned int flags,
	String* error = 0);
    unsigned int setStatus(const BrfDevStatus& statTx, unsigned int flagsTx,
	const BrfDevStatus& statRx, unsigned int flagsRx, String* error = 0) {
	    unsigned int status = setStatus(statRx,flagsRx,error);
	    return status == 0 ? setStatus(statTx,flagsRx,error) : status;
	}
    void internalDumpDev(String& buf, bool info, bool state, const char* sep,
	bool internal, bool fromStatus = false, bool withHdr = true);
    unsigned int internalPowerOn(bool rfLink, bool tx = true, bool rx = true,
	String* error = 0);
    // Send an array of samples waiting to be transmitted
    // samples: The number of I/Q samples (i.e. half buffer lengh)
    unsigned int send(uint64_t ts, float* data, unsigned int samples,
	float* powerScale = 0);
    void sendTxPatternChanged();
    void sendCopyTxPattern(int16_t* buf, unsigned int avail,
	float scaleI, int16_t maxI, float scaleQ, int16_t maxQ, unsigned int& clamped);
    unsigned int recv(uint64_t& ts, float* data, unsigned int& samples);
    unsigned int internalSetSampleRate(bool tx, uint32_t value, String* error = 0);
    // Update FPGA (load, get version)
    unsigned int updateFpga(const NamedList& params);
    unsigned int internalSetFpgaCorr(bool tx, int corr, int16_t value,
	String* error = 0, int clampLevel = DebugNote);
    unsigned int internalGetFpgaCorr(bool tx, int corr, int16_t* value = 0,
	String* error = 0);
    unsigned int internalSetTxVga(int vga, bool preMixer, String* error = 0);
    unsigned int internalGetTxVga(int* vga, bool preMixer, String* error = 0);
    // Enable or disable the pre/post mixer gain on the receive side
    unsigned int internalEnableRxVga(bool on, bool preMixer, String* error = 0);
    unsigned int internalSetRxVga(int vga, bool preMixer, String* error = 0);
    unsigned int internalGetRxVga(int* vga, bool preMixer, String* error = 0);
    inline unsigned int internalRxVga(bool read, int& vga, bool preMixer,
	String* error = 0) {
	    if (read)
		return internalGetRxVga(&vga,preMixer,error);
	    return internalSetRxVga(vga,preMixer,error);
	}
    inline unsigned int internalSetVga(bool tx, int vga, bool preMixer,
	String* error = 0) {
	    if (tx)
		return internalSetTxVga(vga,preMixer,error);
	    return internalSetRxVga(vga,preMixer,error);
	}
    unsigned int internalSetGain(bool tx, int val, int* newVal = 0, String* error = 0);
    unsigned int internalSetTxIQBalance(bool newGain, float newBalance = 0,
	const char* param = 0);
    inline unsigned int internalSetCorrectionIQ(bool tx, int I, int Q,
	String* error = 0) {
	    unsigned int status = internalSetDcOffset(tx,true,I,error);
	    return !status ? internalSetDcOffset(tx,false,Q,error) : status;
	}
    inline unsigned int internalSetDcCorr(int txI, int txQ, int rxI, int rxQ,
	String* error = 0) {
	    unsigned int status = 0;
	    BRF_FUNC_CALL(internalSetCorrectionIQ(true,txI,txQ,error));
	    BRF_FUNC_CALL(internalSetCorrectionIQ(false,rxI,rxQ,error));
	    return status;
	}
    unsigned int internalSetFreqOffs(int val, int* newVal, String* error = 0);
    unsigned int internalSetFrequency(bool tx, uint32_t hz, String* error = 0);
    unsigned int internalGetFrequency(bool tx, uint32_t* hz = 0, String* error = 0);
    // Retrieve TX/RX timestamp
    unsigned int internalGetTimestamp(bool tx, uint64_t& ts, String* error = 0);
    // Retrieve and set frequency
    inline unsigned int restoreFreq(bool tx, String* error = 0) {
	    uint32_t hz = 0;
	    unsigned int status = internalGetFrequency(tx,&hz,error);
	    return status == 0 ? internalSetFrequency(tx,hz,error) : status;
	}
    // Alt interface setting change
    unsigned int lusbSetAltInterface(int altSetting, String* error = 0);
    // Wrapper for libusb_control_transfer
    unsigned int lusbCtrlTransfer(uint8_t reqType, int8_t request, uint16_t value,
	uint16_t index, uint8_t* data, uint16_t len, String* error = 0,
	unsigned int tout = 0);
    // Wrapper for libusb synchronous bulk transfer
    unsigned int lusbBulkTransfer(uint8_t endpoint, uint8_t* data, unsigned int len,
	unsigned int* transferred = 0, String* error = 0, unsigned int tout = 0);
    // Make an async usb transfer
    unsigned int syncTransfer(int ep, uint8_t* data, unsigned int len, String* error = 0);
    // Select amplifier (PA/LNA) from low/high frequency
    inline unsigned int selectPaLna(bool tx, bool lowBand, String* error) {
	    if (tx)
		return paSelect(lowBand ? LmsPa1 : LmsPa2,error);
	    return lnaSelect(lowBand ? LmsLna1 : LmsLna2,error);
	}
    // Read the value of a specific GPIO register
    unsigned int gpioRead(uint8_t addr, uint32_t& value, uint8_t len, String* error = 0,
	const char* loc = 0);
    // Write a value to a specific GPIO register
    unsigned int gpioWrite(uint8_t addr, uint32_t value, uint8_t len, String* error = 0,
	const char* loc = 0);
    // Read the lms configuration
    inline unsigned int lmsRead(uint8_t addr, uint8_t& data, String* error = 0,
	const char* loc = 0)
	{ return accessPeripheralRead(UartDevLMS,addr,data,error,loc); }
    inline unsigned int lmsRead2(uint8_t addr1, uint8_t& data1,
	uint8_t addr2, uint8_t& data2, String* error = 0, const char* loc = 0) {
	    unsigned int status = lmsRead(addr1,data1,error,loc);
	    return status == 0 ? lmsRead(addr2,data2,error,loc) : status;
	}
    // Write the lms configuration
    unsigned int lmsWrite(const String& str, String* error = 0);
    inline unsigned int lmsWrite(uint8_t addr, uint8_t data, String* error = 0,
	const char* loc = 0)
	{ return accessPeripheralWrite(UartDevLMS,addr,data,error,loc); }
    inline unsigned int lmsWrite2(uint8_t addr1, uint8_t data1,
	uint8_t addr2, uint8_t data2, String* error = 0, const char* loc = 0) {
	    unsigned int status = lmsWrite(addr1,data1,error,loc);
	    return status == 0 ? lmsWrite(addr2,data2,error,loc) : status;
	}
    inline unsigned int lms(bool read, uint8_t addr, uint8_t& data,
	String* error = 0, const char* loc = 0) {
	    if (read)
		return lmsRead(addr,data,error,loc);
	    return lmsWrite(addr,data,error,loc);
	}
    // Read address from LMS, clear mask, set val (using OR) and write it back
    inline unsigned int lmsSet(uint8_t addr, uint8_t val, uint8_t clearMask,
	String* error = 0) {
	    uint8_t data = 0;
	    unsigned int status = lmsRead(addr,data,error);
	    return status ? status : lmsWrite(addr,(data & ~clearMask) | val,error);
	}
    // Read address from LMS, set val (using OR) and write it back
    inline unsigned int lmsSet(uint8_t addr, uint8_t val, String* error = 0) {
	    uint8_t data = 0;
	    unsigned int status = lmsRead(addr,data,error);
	    return status ? status : lmsWrite(addr,data | val,error);
	}
    // Read address from LMS, clear mask and write it back
    inline unsigned int lmsReset(uint8_t addr, uint8_t clearMask, String* error = 0) {
	    uint8_t data = 0;
	    unsigned int status = lmsRead(addr,data,error);
	    return status ? status : lmsWrite(addr,(data & ~clearMask),error);
	}
    // Reset LMS addr using mask. Optionally set a value
    inline unsigned int lmsChange(uint8_t addr, uint8_t* maskReset, uint8_t* maskSet,
	String* error) {
	    if (maskReset && maskSet)
		return lmsSet(addr,*maskSet,*maskReset,error);
	    if (maskReset)
		return lmsReset(addr,*maskReset,error);
	    return maskSet ? lmsSet(addr,*maskSet,error) : 0;
	}
    inline unsigned int lmsChangeMask(uint8_t addr, uint8_t mask, bool set,
	String* error)
	{ return lmsChange(addr,set ? 0 : &mask,set ? &mask : 0,error); }
    // LNA
    unsigned int lnaSelect(int lna, String* error = 0);
    unsigned int lnaEnable(bool on, String* error = 0);
    unsigned int lnaGainSet(uint8_t value, String* error = 0);
    unsigned int lnaGainGet(uint8_t& value, String* error = 0);
    inline unsigned int lnaGain(bool read, uint8_t& value, String* error = 0) {
	    if (read)
		return lnaGainGet(value,error);
	    return lnaGainSet(value,error);
	}
    // LPF set/get
    unsigned int internalSetLpfBandwidth(bool tx, uint32_t band, String* error = 0);
    unsigned int internalSetLpf(bool tx, int lpf, String* error = 0);
    unsigned int internalGetLpf(bool tx, int* lpf, String* error = 0);
    // Fill the m_list member of the class
    unsigned int updateDeviceList(String* error = 0);
    inline void clearDeviceList() {
	    if (!m_list)
		return;
	    ::libusb_free_device_list(m_list,1);
	    m_dev = 0;
	    m_list = 0;
	    m_listCount = 0;
	}
    // Enable/disable RF and sample circulation on both RX and TX sides
    inline unsigned int enableRfBoth(bool on, bool frontEndOnly, String* error = 0) {
	    unsigned int status = enableRf(true,on,frontEndOnly,error);
	    if (status == 0)
		return enableRf(false,on,frontEndOnly,error);
	    return status;
	}
    unsigned int enableRf(bool tx, bool on, bool frontEndOnly = false, String* error = 0);
    // Read the FPGA version
    unsigned int getFpgaVersion(uint32_t& version);
    // Check if fpga is loaded
    // Return NoError/NotInitialized (result OK) or other error on failure
    unsigned int checkFpga();
    // Restore device after loading the FPGA
    unsigned int restoreAfterFpgaLoad(String* error = 0);
    // Reset the Usb interface using an ioctl call
    unsigned int resetUsb(String* error = 0);
    // Set the VCTCXO configuration to the correct value
    unsigned int tuneVcocap(uint8_t addr, String* error = 0);
    // Send requests to the bladeRF device regarding the FPGA image configuration.
    unsigned int vendorCommand(uint8_t cmd, uint8_t endpoint, uint8_t* data, uint16_t len,
	String* error = 0);
    inline unsigned int vendorCommand(uint8_t cmd, uint8_t endpoint, int32_t& data,
	String* error = 0)
	{ return vendorCommand(cmd,endpoint,(uint8_t*)&data,sizeof(data),error); }
    inline unsigned int vendorCommand0_4(uint8_t cmd, uint8_t endpoint, String* error = 0) {
	    uint32_t dummy = 0;
	    return vendorCommand(cmd,endpoint,(uint8_t*)&dummy,4,error);
	}
    // Access the bladeRF board in order to transmit data
    unsigned int accessPeripheral(uint8_t dev, bool tx, uint8_t addr,
	uint8_t* data, String* error = 0, uint8_t len = 1, const char* loc = 0);
    inline unsigned int accessPeripheralWrite(uint8_t dev, uint8_t addr, uint8_t data,
	String* error = 0, const char* loc = 0)
	{ return accessPeripheral(dev,true,addr,&data,error,1,loc); }
    inline unsigned int accessPeripheralRead(uint8_t dev, uint8_t addr, uint8_t& data,
	String* error = 0, const char* loc = 0)
	{ return accessPeripheral(dev,false,addr,&data,error,1,loc); }
    inline unsigned int setSi5338(uint8_t addr, uint8_t data, String* error = 0)
	{ return accessPeripheralWrite(UartDevSI5338,addr,data,error); }
    inline unsigned int getSi5338(uint8_t addr, uint8_t& data, String* error = 0)
	{ return accessPeripheralRead(UartDevSI5338,addr,data,error); }
    unsigned int internalSetDcOffset(bool tx, bool i, int16_t value, String* error = 0);
    unsigned int internalGetDcOffset(bool tx, bool i, int16_t* value, String* error = 0);
    unsigned int enableTimestamps(bool on = true, String* error = 0);
    unsigned int updateStatus(String* error = 0);
    unsigned int paSelect(int pa, String* error = 0);
    void clampFrequency(uint32_t& val, bool tx, const char* loc);
    int clampInt(int val, int minVal, int maxVal, const char* what = 0,
	int level = DebugNote);
    inline int clampIntParam(const NamedList& params, const String& param,
	int defVal, int minVal, int maxVal, int level = DebugConf)
	{ return clampInt(params.getIntValue(param,defVal),minVal,maxVal,param,level); }
    unsigned int openDevice(bool claim, String* error = 0);
    void closeDevice();
    void getDevStrDesc(String& data, uint8_t index, const char* what);
    // Read pages from device using contrt to 0 (from -35)ol transfer
    unsigned int ctrlTransferReadPage(uint8_t request, DataBlock& buf, String* error = 0);
    // Read calibration cache page from device
    unsigned int readCalCache(String* error = 0);
    // Retrieve a filed from a buffer of elements
    // Buffer format: 1 byte length + data + 2 bytes CRC16
    // Return error string on failure
    const char* getBufField(String& value, const char* field);
    // Read calibration cache field
    unsigned int getCalField(String& value, const String& name,
	const char* desc = 0, String* error = 0);
    String& dumpCalCache(String& dest);
    // Update speed related data
    unsigned int updateSpeed(const NamedList& params, String* error = 0);
    // Compute Rx avg values, autocorrect offsets if configured
    void computeRx(uint64_t ts);
    // Check io timestamps
    void ioBufCheckTs(bool tx, unsigned int nBufs = 0);
    // Alter data
    void updateAlterData(const NamedList& params);
    void rxAlterData(bool first);
    // Calibration utilities
    unsigned int calibrateAuto(String* error);
    unsigned int calBackupRestore(BrfCalData& bak, bool backup, String* error);
    unsigned int calInitFinal(BrfCalData& bak, bool init, String* error);
    unsigned int dcCalProcPrepare(const BrfCalData& bak, uint8_t subMod, String& error);
    unsigned int dcCalProc(const BrfCalData& bak, uint8_t subMod, uint8_t dcCnt,
	uint8_t& dcReg, String& error);
    unsigned int dcCalProcPost(const BrfCalData& bak, uint8_t subMod, uint8_t dcReg,
	String& error);
    unsigned int calLPFBandwidth(const BrfCalData& bak, uint8_t subMod, uint8_t dcCnt,
			   uint8_t& dcReg, String& error);
    // Read data, ignore any buffer related errors
    unsigned int dummyRead(float* buf, unsigned int samples, String* error);
    unsigned int readBuffer(uint64_t ts, float* buf, unsigned int samples,
	String* error);
    unsigned int readComputeDcOffsets(uint8_t dcI, uint8_t dcQ,
	float* buf, unsigned int samples, String* error,
	float& totalPower, float& rxDcOffset, float& txDcOffset);
    unsigned int readComputeDcOffsetsCorr(int* corr, float* powerBalance, float* buf,
	unsigned int samples, String* error, float& totalPower, float& rxDcOffset);
    void calibrateBBStarting(const char* what);
    unsigned int calibrateBBTxDc(int& dcI, int& dcQ, float* buf, unsigned int samples,
	String* error);
    unsigned int calibrateBBTxPhase(bool bruteForce, int& corr, float* buf, unsigned int samples,
	String* error);
    unsigned int calibrateBBTxGain(bool bruteForce, float& corr, float* buf, unsigned int samples,
	String* error);
    unsigned int calibrateBB(String* error);
    // Set error string or put a debug message
    unsigned int showError(unsigned int code, const char* error, const char* prefix,
	String* buf, int level = DebugNote);
    void printIOBuffer(bool tx, const char* loc, int index = -1, unsigned int nBufs = 0);
    void dumpIOBuffer(BrfDevIO& io, unsigned int nBufs);
    void updateIODump(BrfDevIO& io);
    inline BrfDevIO& getIO(bool tx)
	{ return tx ? m_txIO : m_rxIO; }
    inline unsigned int checkDbgInt(int& val, unsigned int step = 1) {
	    if (!(val && step))
		return 0;
	    Lock lck(m_dbgMutex);
	    if (val < 0)
		return step;
	    if (val >= (int)step)
		val -= step;
	    else {
		step = val;
		val = 0;
	    }
	    return step;
	}
    // Enable or disable loopback
    unsigned int internalSetLoopback(int mode = 0, String* error = 0);
    unsigned int setLoopbackPath(int mode, String& error);
    unsigned int enableRfLoopback(bool on, String& error);
    void dumpLoopbackStatus();
    void dumpLmsModulesStatus();
    unsigned int internalDumpPeripheral(uint8_t dev, uint8_t addr, uint8_t len,
	String* buf, uint8_t lineLen);
    inline int decodeLpf(uint8_t reg1, uint8_t reg2) const {
	    int on = reg1 & (1 << 1);
	    int bypass = reg2 & (1 << 6);
	    if (on)
		return bypass ? LpfInvalid : LpfNormal;
	    return bypass ? LpfBypass : LpfDisabled;
	}
    // Set RXVGA2 DECODE bit (LMS addr 0x64 bit 0)
    // 0(true): Decode control signals
    // 1(false): Use control signal from test mode registers
    inline unsigned int setRxVga2Decode(bool on, String* error)
	{ return on ? lmsReset(0x64,0x01,error) : lmsSet(0x64,0x01,error); }
    // Request changes (synchronous TX). Wait for change
    inline unsigned int syncTxStatus(unsigned int flagsTx, unsigned int flagsRx,
	String* error = 0) {
	    m_txIO.syncFlags = flagsTx;
	    m_rxIO.syncFlags = flagsRx;
	    unsigned int intervals = (m_syncTout / Thread::idleMsec()) + 1;
	    unsigned int status = 0;
	    while (m_txIO.syncFlags || m_rxIO.syncFlags) {
		Thread::idle();
		BRF_FUNC_CALL_RET(cancelled(error));
		if ((intervals--) == 0) {
		    m_txIO.syncFlags = 0;
		    m_rxIO.syncFlags = 0;
		    return setError(RadioInterface::Failure,error,"Sync TS timeout");
		}
	    }
	    return 0;
	}
    inline bool setRxDcAuto(bool value) {
	    if (m_rxDcAuto != value)
		return !(m_rxDcAuto = value);
	    return m_rxDcAuto;
	}

    BrfInterface* m_owner;               // The interface owning the device
    bool m_exiting;                      // Exiting flag
    bool m_closing;                      // Closing flag
    bool m_closingDevice;                // Closing device flag
    Mutex m_dbgMutex;
    // libusb
    libusb_context* m_context;           // libusb context
    libusb_device** m_list;              // List of devices
    unsigned int m_listCount;            // Device list length
    libusb_device_handle* m_devHandle;   // Device handle
    libusb_device* m_dev;                // Pointer to used device (in m_list)
    // Device info
    RadioCapability m_radioCaps;
    int m_devBus;                        // Device bus
    int m_devAddr;                       // Device address
    int m_devSpeed;                      // Device speed
    String m_devSerial;                  // Device serial number
    String m_devFwVerStr;                // Device firmware version string
    String m_devFpgaVerStr;              // Device FPGA version string
    String m_devFpgaFile;                // Device FPGA file (nul if not loaded)
    String m_devFpgaMD5;                 // FPGA MD5
    uint16_t m_ctrlTransferPage;         // Control transfer page size
    DataBlock m_calCache;
    //
    NamedList m_calibration;             // Calibration parameters
    unsigned int m_syncTout;             // Sync transfer timeout (in milliseconds)
    unsigned int m_ctrlTout;             // Control transfer timeout (in milliseconds)
    unsigned int m_bulkTout;             // Bulk transfer timeout (in milliseconds)
    int m_altSetting;
    int m_rxShowDcInfo;                  // Output Rx DC info
    bool m_rxDcAuto;                     // Automatically adjust Rx DC offset
    int m_rxDcOffsetMax;                 // Rx DC offset correction
    int m_rxDcAvgI;                      // Current average for I (in-phase) DC RX offset
    int m_rxDcAvgQ;                      // Current average for Q (quadrature) DC RX offset
    int m_freqOffset;                    // Master clock frequency adjustment
    BrfDevIO m_txIO;
    BrfDevIO m_rxIO;
    LusbTransfer m_usbTransfer[EpCount]; // List of USB transfers
    int m_loopback;                      // Current loopback mode
    uint64_t m_rxTimestamp;              // RX timestamp
    uint64_t m_rxResyncCandidate;        // RX: timestamp resync value
    unsigned int m_rxTsPastIntervalMs;   // RX: allowed timestamp in the past interval in 1 read operation
    unsigned int m_rxTsPastSamples;      // RX: How many samples in the past to allow in 1 read operation
    float m_warnClamped;                 // TX: Warn clamped threshold (percent)
    unsigned int m_minBufsSend;          // Minimum buffers to send
    unsigned int m_silenceTimeMs;        // Silence timestamp related debug messages (in millseconds)
    uint64_t m_silenceTs;                // Silence timestamp related debug messages
    // TX power scale
    bool m_calibrated;
    float m_txPowerBalance;
    bool m_txPowerBalanceChanged;
    float m_txPowerScaleI;
    float m_txPowerScaleQ;
    float m_wrPowerScaleI;
    float m_wrPowerScaleQ;
    int16_t m_wrMaxI;
    int16_t m_wrMaxQ;
    // Alter data
    NamedList m_rxAlterDataParams;
    bool m_rxAlterData;
    int16_t m_rxAlterIncrement;
    String m_rxAlterTsJumpPatern;        // Pattern used to alter rx timestamps (kept to check changes)
    bool m_rxAlterTsJumpSingle;          // Stop altering ts after first pass
    DataBlock m_rxAlterTsJump;           // Values to alter rx timestamps
    unsigned int m_rxAlterTsJumpPos;     // Current position in rx alter timestamps
    bool m_txPatternChanged;
    String m_txPatternStr;
    DataBlock m_txPattern;
    DataBlock m_txPatternBuffer;
    unsigned int m_txPatternBufPos;
    unsigned int m_txPatternBufSamples;
    // Calibration
    BrfThread* m_sendThread;
    Mutex m_sendThreadMutex;
};

// Initialize data used to wait for interface Tx busy
// Clear the flag when destroyed
class BrfSerialize
{
public:
    inline BrfSerialize(BrfLibUsbDevice* dev, bool tx, bool waitNow = true)
	: status(0), m_device(dev), m_io(m_device->getIO(tx)), m_lock(0) {
	    if (waitNow)
	        wait();
	}
    inline ~BrfSerialize()
	{ drop(); }
    inline void drop()
	{ m_lock.drop(); }
    inline void wait() {
	    if (m_lock.acquire(m_io.mutex)) {
		if ((status = m_device->cancelled()) != 0)
		    drop();
	    }
	    else
		status = m_device->showError(RadioInterface::Failure,
		    "Failed to serialize",brfDir(m_io.tx()),0,DebugWarn);
	}
    unsigned int status;
protected:
    BrfLibUsbDevice* m_device;
    BrfDevIO& m_io;
    Lock m_lock;
};

class BrfInterface : public RadioInterface
{
    YCLASS(BrfInterface,RadioInterface)
    friend class BrfModule;
public:
    ~BrfInterface();
    inline BrfLibUsbDevice* device() const
	{ return m_dev; }
    inline bool isDevice(void* dev) const
	{ return m_dev && m_dev == (BrfLibUsbDevice*)dev; }
    // Module reload
    inline void reLoad() {
	    if (m_dev)
		m_dev->reLoad();
	}
    virtual unsigned int initialize(const NamedList& params = NamedList::empty());
    virtual unsigned int setParams(NamedList& params, bool shareFate = true);
    virtual unsigned int setDataDump(int dir = 0, int level = 0,
	const NamedList* params = 0) {
	    if (!m_dev)
		return Failure;
	    m_dev->setDataDump(dir,level,params);
	    return 0;
	}
    virtual unsigned int send(uint64_t when, float* samples, unsigned size,
	float* powerScale = 0);
    virtual unsigned int recv(uint64_t& when, float* samples, unsigned& size);
    unsigned int setFrequency(uint64_t hz, bool tx);
    unsigned int getFrequency(uint64_t& hz, bool tx) const;
    virtual unsigned int setTxFreq(uint64_t hz)
	{ return setFrequency(hz,true); }
    virtual unsigned int getTxFreq(uint64_t& hz) const
	{ return getFrequency(hz,true); }
    virtual unsigned int setRxFreq(uint64_t hz)
	{ return setFrequency(hz,false); }
    virtual unsigned int getRxFreq(uint64_t& hz) const
	{ return getFrequency(hz,false); }
    virtual unsigned int setFreqOffset(int offs, int* newVal = 0)
	{ return m_dev->setFreqOffset(offs,newVal); }
    virtual unsigned int setSampleRate(uint64_t hz);
    virtual unsigned int getSampleRate(uint64_t& hz) const;
    virtual unsigned int setFilter(uint64_t hz);
    virtual unsigned int getFilterWidth(uint64_t& hz) const;
    unsigned int setRxGain(int val, unsigned port, bool preMixer);
    virtual unsigned int setRxGain1(int val, unsigned port)
	{ return setRxGain(val,port,true); }
    virtual unsigned int setRxGain2(int val, unsigned port)
	{ return setRxGain(val,port,false); }
    unsigned int setTxGain(int val, unsigned port, bool preMixer);
    virtual unsigned int setTxGain1(int val, unsigned port)
	{ return setTxGain(val,port,true); }
    virtual unsigned int setTxGain2(int val, unsigned port)
	{ return setTxGain(val,port,false); }
    virtual unsigned int getTxTime(uint64_t& time) const
	{ return m_dev->getTimestamp(true,time); }
    virtual unsigned int getRxTime(uint64_t& time) const
	{ return m_dev->getTimestamp(false,time); }
    virtual unsigned int setTxPower(const unsigned dBm)
	{ return setTxGain2(dBm,0); }
    virtual unsigned int setPorts(unsigned ports) {
	    if (ports == m_radioCaps->currPorts)
		return 0;
	    return ports ? NotSupported : OutOfRange;
	}
    // Set pre and post mixer value
    unsigned int setGain(bool tx, int val, unsigned int port,
	int* newValue = 0) const;
    virtual unsigned status(int port = -1) const
	{ return (m_totalErr & FatalErrorMask); }
    virtual unsigned int setLoopback(const char* name = 0)
	{ return m_dev ? m_dev->setLoopback(name) : NotInitialized; }
    virtual unsigned int calibrate()
	{ return m_dev->calibrate(); }

protected:
    BrfInterface(const char* name);
    // Method to call after creation to init the interface
    virtual BrfLibUsbDevice* init();
    virtual void destroyed();

private:
    BrfLibUsbDevice* m_dev;              // Used device
    int m_txFreqCorr;
};

class BrfThread : public Thread
{
public:
    inline BrfThread(BrfInterface* ifc, const char* name)
	: Thread(name), m_name(name), m_iface(ifc)
	{}
    // Device send thread
    inline BrfThread(BrfLibUsbDevice* dev, const char* name = "BrfDevSend")
	: Thread(name), m_name(name), m_iface(0), m_device(dev)
	{}
    ~BrfThread()
	{ notify(); }
    inline const char* name() const
	{ return m_name; }
    virtual void cleanup()
	{ notify(); }
    // Start this thread. Set destination pointer on success. Delete object on failure
    BrfThread* start();
    // Stop thread
    static void cancelThread(BrfThread*& th, Mutex* mtx, unsigned int waitMs,
	DebugEnabler* dbg, void* ptr);
protected:
    virtual void run();
    void notify();

    String m_name;
    RefPointer<BrfInterface> m_iface;
    BrfLibUsbDevice* m_device;
};

class BrfTest : public BrfInterface, public Mutex
{
    YCLASS(BrfTest,BrfInterface)
    friend class BrfModule;
    friend class BrfThread;
public:
    enum State {
	Idle = 0,
	Running,
	Stopping
    };
    BrfTest(const char* name, const NamedList& params, const NamedList& devOpen,
	const NamedList& cmds);
    inline int state() const
	{ return m_state; }
    inline void pause() {
	    m_pause = true;
	    Thread::msleep(100);
	}
    inline void resume() {
	    if (!m_pause)
		return;
	    Thread::msleep(100);
	    updateTs(true);
	    updateTs(false);
	    m_pause = false;
	}
    bool start();
    void stop();
    bool execute(const String& cmd, const String& param, String& error,
	bool fatal, const NamedList* params = 0);

protected:
    bool execute(const NamedList& cmds, const char* prefix, String& error);
    bool runInit();
    void run();
    void runSendOnly();
    void runSendRecv();
    void dumpStats();
    bool workerTerminated(BrfThread* th);
    bool checkPause(bool tx);
    bool write();
    bool read();
    inline void updateTs(bool tx) {
	    uint64_t ts = 0;
	    if ((tx ? getTxTime(ts) : getRxTime(ts)) == 0)
		(tx ? m_sendTs : m_readTs) = ts;
	}
    inline void resetBufs(unsigned int samples) {
	    m_bufs.reset(samples,0);
	    m_crt.assign(0,samplesf2bytes(m_bufs.bufSamples()));
	    m_aux = m_crt;
	    m_extra = m_crt;
	    m_bufs.crt.samples = (float*)m_crt.data(0);
	    m_bufs.aux.samples = (float*)m_aux.data(0);
	    m_bufs.extra.samples = (float*)m_extra.data(0);
	}

    int m_state;
    bool m_pause;
    bool m_pauseSend;
    bool m_pauseRead;
    unsigned int m_sendBufCount;
    bool m_sendOnly;
    BrfThread* m_worker;
    // Send data
    uint64_t m_sentSamples;
    uint64_t m_sendTs;
    DataBlock m_sendBufData;
    float* m_sendBuf;
    unsigned int m_sendBufSamples;
    // Read data
    RadioReadBufs m_bufs;
    uint64_t m_readTs;
    unsigned int m_skippedBuffs;
    DataBlock m_crt;
    DataBlock m_aux;
    DataBlock m_extra;
    // Params
    NamedList m_params;
    NamedList m_devOpen;
    NamedList m_cmds;
};

class BrfModule : public Module
{
    friend class BrfInterface;
public:
    enum Relay {
	RadioCreate = Private,
    };
    BrfModule();
    ~BrfModule();
    bool findIfaceByDevice(RefPointer<BrfInterface>& iface, void* dev);
    inline bool findIface(RefPointer<BrfInterface>& iface, const String& n) {
	    Lock lck(this);
	    ObjList* o = m_ifaces.find(n);
	    if (o)
		iface = static_cast<BrfInterface*>(o->get());
	    return iface != 0;
	}
    inline void setTest(bool on, BrfTest* ptr = 0) {
	    Lock lck(this);
	    if (on) {
		m_test = 0;
		Lock lck(ptr);
		if (ptr->state() == BrfTest::Running)
		    m_test = ptr;
	    }
	    else if (ptr && m_test == ptr)
		m_test = 0;
	}

protected:
    virtual void initialize();
    virtual bool received(Message& msg, int id);
    virtual void statusModule(String& str);
    virtual void statusParams(String& str);
    virtual void statusDetail(String& str);
    virtual bool commandComplete(Message& msg, const String& partLine,
	const String& partWord);
    BrfInterface* createIface(const NamedList& params);
    void completeIfaces(String& dest, const String& partWord);
    bool onCmdControl(BrfInterface* ifc, Message& msg);
    bool onCmdStatus(String& retVal, String& line);
    bool onCmdGain(BrfInterface* ifc, Message& msg, int tx = -1, bool preMixer = true);
    bool onCmdCorrection(BrfInterface* ifc, Message& msg, int tx = -1, int corr = 0);
    bool onCmdLmsWrite(BrfInterface* ifc, Message& msg);
    bool onCmdBufOutput(BrfInterface* ifc, Message& msg);
    bool onCmdShow(BrfInterface* ifc, Message& msg, const String& what = String::empty());
    bool test(const String& cmd, NamedList& list);
    void setDebugPeripheral(const NamedList& list);
    void setSampleEnergize(const String& value);

    unsigned int m_ifaceId;
    ObjList m_ifaces;
    RefPointer<BrfTest> m_test;
};

static bool s_usbContextInit = false;            // USB library init flag
INIT_PLUGIN(BrfModule);
static Configuration s_cfg;                      // Configuration file (protected by plugin mutex)
static const String s_modCmds[] = {"test","help",""};
static const String s_ifcCmds[] = {
    "txgain1", "txgain2", "rxgain1", "rxgain2",
    "txdci", "txdcq", "txfpgaphase", "txfpgagain",
    "rxdci", "rxdcq", "rxfpgaphase", "rxfpgagain",
    "showstatus", "showboardstatus", "showstatistics", "showtimestamps", "showlms",
    "vgagain","correction","lmswrite",
    "bufoutput","rxdcoutput","txpattern","show",""};
// libusb
static unsigned int s_lusbSyncTransferTout = LUSB_SYNC_TIMEOUT; // Sync transfer timeout def val (in milliseconds)
static unsigned int s_lusbCtrlTransferTout = LUSB_CTRL_TIMEOUT; // Control transfer timeout def val (in milliseconds)
static unsigned int s_lusbBulkTransferTout = LUSB_BULK_TIMEOUT; // Bulk transfer timeout def val (in milliseconds)

static BrfPeripheral s_uartDev[BrfLibUsbDevice::UartDevCount] = {
    BrfPeripheral("GPIO",0x00),
    BrfPeripheral("LMS",0x10),
    BrfPeripheral("VCTCXO",0x20),
    BrfPeripheral("SI5338",0x30)
};

static const TokenDict s_usbEndpoint[] = {
    {"SEND_SAMPLES",  BrfLibUsbDevice::EpSendSamples},
    {"SEND_CTRL",     BrfLibUsbDevice::EpSendCtrl},
    {"READ_SAMPLES",  BrfLibUsbDevice::EpReadSamples},
    {"READ-CTRL",     BrfLibUsbDevice::EpReadCtrl},
    {0,0}
};

static const TokenDict s_loopback[] = {
    {"firmware",      BrfLibUsbDevice::LoopFirmware},
    {"lpf-to-rxout",  BrfLibUsbDevice::LoopLpfToRxOut},
    {"lpf-to-vga2",   BrfLibUsbDevice::LoopLpfToVga2},
    {"vga1-to-vga2",  BrfLibUsbDevice::LoopVga1ToVga2},
    {"lpf-to-lpf",    BrfLibUsbDevice::LoopLpfToLpf},
    {"vga1-to-lpf",   BrfLibUsbDevice::LoopVga1ToLpf},
    {"pa-to-lna1",    BrfLibUsbDevice::LoopRfLna1},
    {"pa-to-lna2",    BrfLibUsbDevice::LoopRfLna2},
    {"pa-to-lna3",    BrfLibUsbDevice::LoopRfLna3},
    {"none",          BrfLibUsbDevice::LoopNone},
    {0,0}
};
static const TokenDict s_pa[] = {
    {"AUXPA",  BrfLibUsbDevice::LmsPaAux},
    {"PA1",    BrfLibUsbDevice::LmsPa1},
    {"PA2",    BrfLibUsbDevice::LmsPa2},
    {0,0}
};
static const TokenDict s_lpf[] = {
    {"disabled",  BrfLibUsbDevice::LpfDisabled},
    {"bypassed",  BrfLibUsbDevice::LpfBypass},
    {"normal",    BrfLibUsbDevice::LpfNormal},
    {0,0}
};
static const TokenDict s_lnaGain[] = {
    {"BYPASS",    BrfLibUsbDevice::LnaGainBypass},
    {"MID",       BrfLibUsbDevice::LnaGainMid},
    {"MAX",       BrfLibUsbDevice::LnaGainMax},
    {"Unhandled", BrfLibUsbDevice::LnaGainUnhandled},
    {0,0}
};

static bool completeStrList(String& dest, const String& partWord, const String* ptr)
{
    if (!ptr)
	return false;
    while (*ptr)
	Module::itemComplete(dest,*ptr++,partWord);
    return false;
}

static inline void loadCfg(bool safe = true, NamedList* s1 = 0)
{
    Lock lck(safe ? 0 : &__plugin);
    s_cfg = Engine::configFile("ybladerf");
    s_cfg.load();
    if (!TelEngine::null(s1)) {
	NamedList* tmp = s_cfg.getSection(*s1);
	if (tmp)
	    *s1 = *tmp;
	else
	    s1->assign("");
    }
}

static void lusbSetDebugLevel(int level = -1)
{
    // No lock needed: this function is called from plugin init (loads the config) or
    //  context init (locks the plugin)
    if (!s_usbContextInit)
	return;
    if (level < 0) {
	String* l = s_cfg.getKey(YSTRING("libusb"),YSTRING("debug_level"));
	::libusb_set_debug(0,l ? l->toInteger(0,0,0) : 0);
    }
    else
	::libusb_set_debug(0,level);
}

// libusb transfer stream callback
static void lusbTransferCb(libusb_transfer* transfer)
{
    if (!transfer) {
	DDebug(&__plugin,DebugWarn,"lusbTransferCb() called with NULL transfer");
	return;
    }
    LusbTransfer* t = (LusbTransfer*)(transfer->user_data);
#ifdef DEBUG_LUSB_TRANSFER_CALLBACK
    int level = DebugAll;
    if (transfer->status != LIBUSB_TRANSFER_COMPLETED)
	level = DebugNote;
    if (!__plugin.debugAt(level))
	return;
    RefPointer<BrfInterface> ifc;
    String ifcInfo;
    if (t && __plugin.findIfaceByDevice(ifc,t->device)) {
	ifcInfo.printf("(%p %s)",(BrfInterface*)ifc,ifc->debugName());
	ifc = 0;
    }
    String x;
    String tmp;
    x << "\r\ninterface=" << ifcInfo.safe("not found");
    x << tmp.printf("\r\nhandle=%p",transfer->dev_handle);
    x << tmp.printf("\r\nuser_data=%p",transfer->user_data);
    x << tmp.printf("\r\nflags=0x%x",transfer->flags);
    x << "\r\ntype=";
    switch (transfer->type) {
	case LIBUSB_TRANSFER_TYPE_CONTROL:
	    x << "CONTROL";
	    break;
	case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS:
	    x << "ISOCHRONOUS";
	    break;
	case LIBUSB_TRANSFER_TYPE_BULK:
	    x << "BULK";
	    break;
	case LIBUSB_TRANSFER_TYPE_INTERRUPT:
	    x << "INTERRUPT";
	    break;
	//case LIBUSB_TRANSFER_TYPE_BULK_STREAM:
	//    x << "STREAM";
	//    break;
	default:
	    x << (unsigned int)(transfer->type);
    }
    String endp;
    switch (transfer->endpoint) {
	case BRF_ENDP_TX_SAMPLES:
	    endp = lookup(BrfLibUsbDevice::EpSendSamples,s_usbEndpoint);
	    break;
	case BRF_ENDP_TX_CTRL:
	    endp = lookup(BrfLibUsbDevice::EpSendCtrl,s_usbEndpoint);
	    break;
	case BRF_ENDP_RX_SAMPLES:
	    endp = lookup(BrfLibUsbDevice::EpReadSamples,s_usbEndpoint);
	    break;
	case BRF_ENDP_RX_CTRL:
	    endp = lookup(BrfLibUsbDevice::EpReadCtrl,s_usbEndpoint);
	    break;
	default:
	    endp.printf("0x%x",transfer->endpoint);
    }
    x << "\r\nendpoint=" << endp;
    x << "\r\ntimeout=" << transfer->timeout << "ms";
    BrfLibUsbDevice::appendLusbError(x,transfer->status,"\r\nstatus=");
    x << "\r\ncurrent_buffer_len=" << transfer->length;
    x << "\r\ntransferred=" << transfer->actual_length;
    Debug(&__plugin,level,"lusbTransferCb(%p)%s",transfer,encloseDashes(x));
#endif
    if (!t)
	return;
    Lock lck(t);
    if (transfer->status == LIBUSB_TRANSFER_COMPLETED &&
	transfer->length != transfer->actual_length) {
	t->status = RadioInterface::HardwareIOError;
	t->error.printf("Incomplete transfer %u/%u",transfer->actual_length,transfer->length);
    }
    else
	t->status = BrfLibUsbDevice::lusbCheckSuccess(transfer->status,&t->error);
    t->running(false);
}


//
// BrfPeripheral
//
void BrfPeripheral::set(bool tx, bool rx, const String& addr)
{
    bool changed = m_tx != tx || m_rx != rx;
    String oldTrackAddr;
    if (m_haveTrackAddr)
	oldTrackAddr.hexify(m_trackAddr,sizeof(m_trackAddr));
    m_tx = tx;
    m_rx = rx;
    m_haveTrackAddr = false;
    ::memset(m_trackAddr,0,sizeof(m_trackAddr));
    if ((m_tx || m_rx) && addr && addr != oldTrackAddr) {
	DataBlock tmp;
	if (tmp.unHexify(addr)) {
	    uint8_t* d = tmp.data(0);
	    for (unsigned int i = 0; i < tmp.length(); i++, d++) {
		if (*d < 128) {
		    m_trackAddr[*d] = 1;
		    m_haveTrackAddr = true;
		}
		else
		    Debug(&__plugin,DebugConf,
			"Ignoring invalid track address 0x%x for peripheral '%s'",*d,c_str());
	    }
	}
	else
	    Debug(&__plugin,DebugConf,
		"Ignoring invalid track addresses for peripheral '%s'",c_str());
    }
    String newTrackAddr;
    if (m_haveTrackAddr) {
	newTrackAddr.hexify(m_trackAddr,sizeof(m_trackAddr));
	changed = (newTrackAddr != oldTrackAddr);
    }
    else if (oldTrackAddr)
	changed = true;
    if (!changed)
	return;
    String state;
    if (m_tx || m_rx) {
	String ta;
	if (m_haveTrackAddr) {
	    String tmp;
	    for (uint8_t i = 0; i < BRF_ARRAY_LEN(m_trackAddr); i++)
		if (m_trackAddr[i])
		    ta.append(tmp.hexify(&i,1)," ");
	}
	Debug(&__plugin,DebugAll,
	    "%s peripheral debug changed: tx=%s rx=%s tracked_addr=%s",
	    c_str(),String::boolText(m_tx),String::boolText(m_rx),ta.safe());
    }
    else
	Debug(&__plugin,DebugAll,"%s peripheral debug is disabled",c_str());
}


//
// LusbTransfer
//
bool LusbTransfer::fillBulk(uint8_t* data, unsigned int len, unsigned int tout)
{
    if (!alloc())
	return false;
    ::libusb_fill_bulk_transfer(transfer,device->handle(),ep,data,len,lusbTransferCb,
	this,tout);
    return true;
}

bool LusbTransfer::submit()
{
    status = BrfLibUsbDevice::lusbCheckSuccess(::libusb_submit_transfer(transfer),
	&error,"libusb_submit_transfer() failed ");
    return status == 0;
}

unsigned int LusbTransfer::cancel(String* error)
{
    if (!transfer)
	return 0;
    int code = ::libusb_cancel_transfer(transfer);
    if (code == LIBUSB_SUCCESS)
	return 0;
    m_running = false;
    if (code == LIBUSB_ERROR_NOT_FOUND)
	return 0;
    return BrfLibUsbDevice::lusbCheckSuccess(code,error,
	"libusb_cancel_transfer() failed ");
}


//
// BrfDevTmpAltSet
// Temporary change alt setting. Restore on destruction
//
unsigned int BrfDevTmpAltSet::set(int altSet, String* error, const char* oper)
{
    restore();
    if (!m_device || m_device->m_altSetting == altSet)
	return 0;
    unsigned int status = m_device->lusbSetAltInterface(altSet,error);
    if (status)
	return status;
    m_oper = oper;
    m_tmpAltSet = altSet;
    DDebug(m_device->owner(),DebugAll,
	"Temporary changed alt interface to %s for '%s' [%p]",
	altSetName(m_tmpAltSet),m_oper,m_device->owner());
    return 0;
}

unsigned int BrfDevTmpAltSet::restore()
{
    if (m_tmpAltSet == BRF_ALTSET_INVALID)
	return 0;
    String e;
    unsigned int status = m_device->lusbSetAltInterface(m_tmpAltSet,&e);
    if (status == 0)
	DDebug(m_device->owner(),DebugAll,
	    "Restored alt interface to %s after '%s' [%p]",
	    altSetName(m_tmpAltSet),m_oper,m_device->owner());
    else
	Debug(m_device->owner(),DebugGoOn,
	    "Failed to restore alt interface after '%s': %s [%p]",
	    m_oper,e.c_str(),m_device->owner());
    m_tmpAltSet = BRF_ALTSET_INVALID;
    return status;
}


//
// BrfLibUsbDevice
//
#define BRF_CHECK_DEV(text) { \
    if (!m_devHandle) { \
	Debug(m_owner,DebugGoOn,"%s: not open [%p]",text,m_owner); \
	return RadioInterface::NotInitialized; \
    } \
}

#define BRF_TX_SERIALIZE_(waitNow,instr) \
    BrfSerialize txSerialize(this,true,waitNow); \
    if (txSerialize.status) \
	instr
#define BRF_TX_SERIALIZE             BRF_TX_SERIALIZE_(true,return txSerialize.status)
#define BRF_TX_SERIALIZE_NONE        BRF_TX_SERIALIZE_(true,return)
#define BRF_TX_SERIALIZE_BOOL        BRF_TX_SERIALIZE_(true,return false)

#define BRF_RX_SERIALIZE_(waitNow,instr) \
    BrfSerialize rxSerialize(this,false,waitNow); \
    if (rxSerialize.status) \
	instr
#define BRF_RX_SERIALIZE             BRF_RX_SERIALIZE_(true,return rxSerialize.status)
#define BRF_RX_SERIALIZE_NONE        BRF_RX_SERIALIZE_(true,return)
#define BRF_RX_SERIALIZE_BOOL        BRF_RX_SERIALIZE_(true,return false)

BrfLibUsbDevice::BrfLibUsbDevice(BrfInterface* owner)
    : m_owner(owner),
    m_exiting(false),
    m_closing(false),
    m_closingDevice(false),
    m_dbgMutex(false,"BrfDevDbg"),
    m_context(0),
    m_list(0),
    m_listCount(0),
    m_devHandle(0),
    m_dev(0),
    m_devBus(-1),
    m_devAddr(-1),
    m_devSpeed(LIBUSB_SPEED_HIGH),
    m_ctrlTransferPage(0),
    m_calibration(""),
    m_syncTout(s_lusbSyncTransferTout),
    m_ctrlTout(s_lusbCtrlTransferTout),
    m_bulkTout(s_lusbBulkTransferTout),
    m_altSetting(BRF_ALTSET_INVALID),
    m_rxShowDcInfo(0),
    m_rxDcAuto(true),
    m_rxDcOffsetMax(BRF_RX_DC_OFFSET_DEF),
    m_rxDcAvgI(0),
    m_rxDcAvgQ(0),
    m_freqOffset(BRF_FREQ_OFFS_DEF),
    m_txIO(true),
    m_rxIO(false),
    m_loopback(0),
    m_rxTimestamp(0),
    m_rxResyncCandidate(0),
    m_rxTsPastIntervalMs(200),
    m_rxTsPastSamples(0),
    m_warnClamped(0),
    m_minBufsSend(1),
    m_silenceTimeMs(0),
    m_silenceTs(0),
    m_calibrated(false),
    m_txPowerBalance(1),
    m_txPowerBalanceChanged(false),
    m_txPowerScaleI(1),
    m_txPowerScaleQ(1),
    m_wrPowerScaleI(s_sampleEnergize),
    m_wrPowerScaleQ(s_sampleEnergize),
    m_wrMaxI(s_sampleEnergize),
    m_wrMaxQ(s_sampleEnergize),
    m_rxAlterDataParams(""),
    m_rxAlterData(false),
    m_rxAlterIncrement(0),
    m_rxAlterTsJumpSingle(true),
    m_rxAlterTsJumpPos(0),
    m_txPatternChanged(false),
    m_txPatternBufPos(0),
    m_txPatternBufSamples(0),
    m_sendThread(0),
    m_sendThreadMutex("BrfDevSendThread")
{
    DDebug(&__plugin,DebugAll,"BrfLibUsbDevice(%p) [%p]",m_owner,this);
    m_usbTransfer[EpSendSamples].device = this;
    m_usbTransfer[EpSendSamples].ep = BRF_ENDP_TX_SAMPLES;
    m_usbTransfer[EpSendCtrl].device = this;
    m_usbTransfer[EpSendCtrl].ep = BRF_ENDP_TX_CTRL;
    m_usbTransfer[EpReadSamples].device = this;
    m_usbTransfer[EpReadSamples].ep = BRF_ENDP_RX_SAMPLES;
    m_usbTransfer[EpReadCtrl].device = this;
    m_usbTransfer[EpReadCtrl].ep = BRF_ENDP_RX_CTRL;
    m_rxIO.vga1 = BRF_RXVGA1_GAIN_MAX + 1;
    m_rxIO.dcOffsetI = BRF_RX_DC_OFFSET_MAX + 1;
    m_rxIO.dcOffsetQ = BRF_RX_DC_OFFSET_MAX + 1;
    m_txIO.vga1 = BRF_TXVGA1_GAIN_MIN - 1;
    m_txIO.vga2 = BRF_TXVGA2_GAIN_MIN - 1;
    m_txIO.dcOffsetI = BRF_RX_DC_OFFSET_MAX + 1;
    m_txIO.dcOffsetQ = BRF_RX_DC_OFFSET_MAX + 1;
    initRadioCaps(m_radioCaps);
}

BrfLibUsbDevice::~BrfLibUsbDevice()
{
    DDebug(&__plugin,DebugAll,"~BrfLibUsbDevice(%p) [%p]",m_owner,this);
    doClose();
}

unsigned int BrfLibUsbDevice::setTxPattern(const String& pattern)
{
    Lock lck(m_dbgMutex);
    if (m_txPatternStr == pattern)
	return 0;
    bool hadData = (m_txPattern.length() != 0);
    m_txPattern.clear();
    m_txPatternStr.clear();
    m_txPatternChanged = true;
    String tmp;
    bool readArray = true;
    bool dumpArray = false;
    if (pattern == "circle")
	tmp = "1,0,0,1,-1,0,0,-1";
    else if (pattern == "zero")
	tmp = "0,0";
    else if (pattern == "random") {
	readArray = false;
	unsigned int samples = totalSamples(true);
	m_txPattern.resize(samplesf2bytes(samples));
	float* f = (float*)m_txPattern.data(0);
	for (float* last = f + m_txPattern.length() / sizeof(float); f != last; f++) {
	    long int r = Random::random();
	    if (!r)
		continue;
	    uint64_t v = (r >= 0) ? r : -r;
	    *f = (float)(((v % 2) == 0) ? 1 : -1) * (v % 2047) / 2047;
	}
	//dumpArray = true;
    }
    else if (pattern == "increment") {
	readArray = false;
	unsigned int samples = 557;
	m_txPattern.resize(samplesf2bytes(samples));
	float* f = (float*)m_txPattern.data(0);
	for (unsigned int i = 1; i <= samples; i++, f += 2)
	    f[0] = f[1] = (float)i / 2047;
	//dumpArray = true;
    }
    const String* p = tmp ? (const String*)&tmp : &pattern;
    if (readArray && *p && !readSamples(m_txPattern,*p)) {
	Debug(m_owner,DebugNote,"Invalid tx pattern '%s' [%p]",p->c_str(),m_owner);
	return RadioInterface::Failure;
    }
    if (m_txPattern.length()) {
	m_txPatternStr = pattern;
	String s;
	if (dumpArray) {
	    appendComplex(s,m_txPattern);
	    encloseDashes(s,true);
	}
	Debug(m_owner,DebugInfo,"TX pattern set to '%s' [%p]%s",
	    m_txPatternStr.c_str(),m_owner,s.safe());
    }
    else if (hadData)
	Debug(m_owner,DebugInfo,"TX pattern cleared [%p]",m_owner);
    return 0;
}

static inline String& dumpIOAvg(String& buf, BrfDevIO& io, uint64_t now)
{
    if (io.startTime && io.transferred) {
	unsigned int sec = (unsigned int)((now - io.startTime) / 1000000);
	if (sec) {
	    buf = io.transferred / sec;
	    return (buf << " samples/sec");
	}
    }
    return (buf = "-");
}

void BrfLibUsbDevice::dumpStats(String& buf, const char* sep)
{
    BRF_RX_SERIALIZE_NONE;
    BRF_TX_SERIALIZE_NONE;
    String s;
    uint64_t now = Time::now();
    buf.append("TxTS=",sep) << m_txIO.timestamp;
    buf << sep << "RxTS=" << m_rxIO.timestamp;
    buf << sep << "TxAvg=" << dumpIOAvg(s,m_txIO,now);
    buf << sep << "RxAvg=" << dumpIOAvg(s,m_rxIO,now);
}

static inline void buildTimestampReport(String& buf, bool tx, uint64_t our, uint64_t board,
    unsigned int code, bool app = true)
{
    if (!code) {
	const char* what = app ? "app" : "crt";
	int64_t delta = (int64_t)(our - board);
	buf.printf("%s: %s=" FMT64U "\tboard=" FMT64U "\tdelta=" FMT64 "\t%s_position: %s",
	    brfDir(tx),what,our,board,delta,what,(delta < 0 ? "past" : "future"));
    }
    else
	buf << brfDir(tx) << ": failure - " << RadioInterface::errorName(code);
}

void BrfLibUsbDevice::dumpTimestamps(String& buf, const char* sep)
{
    BRF_TX_SERIALIZE_NONE;
    uint64_t tsTx = 0;
    uint64_t ourTx = m_txIO.lastTs;
    unsigned int codeTx = internalGetTimestamp(true,tsTx);
    txSerialize.drop();
    BRF_RX_SERIALIZE_NONE;
    uint64_t tsRx = 0;
    uint64_t ourRx = m_rxIO.timestamp;
    unsigned int codeRx = internalGetTimestamp(false,tsRx);
    uint64_t rx = m_rxTimestamp;
    rxSerialize.drop();
    String s;
    String sTx;
    String sRx;
    String sRxTs;
    buildTimestampReport(sTx,true,ourTx,tsTx,codeTx);
    buildTimestampReport(sRx,false,ourRx,tsRx,codeRx);
    if (!codeRx)
	buildTimestampReport(sRxTs,false,rx,tsRx,codeRx,false);
    buf.append(sTx,sep) << sep << sRx;
    buf.append(sRxTs,sep);
}

void BrfLibUsbDevice::dumpDev(String& buf, bool info, bool state, const char* sep,
    bool fromStatus, bool withHdr)
{
    if (!(info || state))
	return;
    BRF_RX_SERIALIZE_NONE;
    BRF_TX_SERIALIZE_NONE;
    internalDumpDev(buf,info,state,sep,false,fromStatus,withHdr);
}

void BrfLibUsbDevice::dumpBoardStatus(String& buf, const char* sep)
{
#define ADD_INTERVAL(minVal,maxVal) if (!code) addIntervalInt(buf,minVal,maxVal);
#define BOARD_STATUS_SET_TMP(func,instr_ok) { \
    code = func; \
    if (!code) \
	instr_ok; \
    else \
	tmp.printf("ERROR %u %s",code,RadioInterface::errorName(code)); \
}
#define BOARD_STATUS_SET(func,instr_ok,prefix,suffix) { \
    BOARD_STATUS_SET_TMP(func,instr_ok); \
    buf.append(prefix + tmp + (!code ? suffix : ""),sep); \
}
#define dumpDevAppend(func,val,prefix,suffix) BOARD_STATUS_SET(func,tmp = (int64_t)val,prefix,suffix)
#define dumpDevAppendFreq(func,val,prefix,suffix) \
    BOARD_STATUS_SET(func,dumpFloatG(tmp,(double)val / 1000000,0,"MHz"),prefix,suffix)
#define reportLpf(tx) { \
    BOARD_STATUS_SET(getLpf(intVal,tx),tmp = lookup(intVal,s_lpf),(tx ? "TxLpf=" : "RxLpf="),0); \
    if (!code) { \
	BOARD_STATUS_SET_TMP(getLpfBandwidth(u32Val,tx),dumpFloatG(tmp,(double)u32Val / 1000000,0,"MHz")); \
	buf << " BW: " << tmp; \
    } \
}
    int intVal = 0;
    int16_t int16Val = 0;
    uint32_t u32Val = 0;
    uint64_t u64Val = 0;
    unsigned int code = 0;
    String tmp;
    dumpDevAppend(getTimestamp(false,u64Val),u64Val,"RxTS=",0);
    dumpDevAppend(getTimestamp(true,u64Val),u64Val,"TxTS=",0);
    dumpDevAppend(getRxVga1(intVal),intVal,"RxVGA1="," dB");
    ADD_INTERVAL(BRF_RXVGA1_GAIN_MIN,BRF_RXVGA1_GAIN_MAX);
    dumpDevAppend(getRxVga2(intVal),intVal,"RxVGA2="," dB");
    ADD_INTERVAL(BRF_RXVGA2_GAIN_MIN,BRF_RXVGA2_GAIN_MAX);
    dumpDevAppend(getTxVga1(intVal),intVal,"TxVGA1="," dB");
    ADD_INTERVAL(BRF_TXVGA1_GAIN_MIN,BRF_TXVGA1_GAIN_MAX);
    dumpDevAppend(getTxVga2(intVal),intVal,"TxVGA2="," dB");
    ADD_INTERVAL(BRF_TXVGA2_GAIN_MIN,BRF_TXVGA2_GAIN_MAX);
    dumpDevAppend(getDcOffset(false,true,int16Val),int16Val,"RxDCCorrI=",0);
    ADD_INTERVAL(-BRF_RX_DC_OFFSET_MAX,BRF_RX_DC_OFFSET_MAX);
    dumpDevAppend(getDcOffset(false,false,int16Val),int16Val,"RxDCCorrQ=",0);
    ADD_INTERVAL(-BRF_RX_DC_OFFSET_MAX,BRF_RX_DC_OFFSET_MAX);
    dumpDevAppend(getDcOffset(true,true,int16Val),int16Val,"TxDCCorrI=",0);
    ADD_INTERVAL(BRF_TX_DC_OFFSET_MIN,BRF_TX_DC_OFFSET_MAX);
    dumpDevAppend(getDcOffset(true,false,int16Val),int16Val,"TxDCCorrQ=",0);
    ADD_INTERVAL(BRF_TX_DC_OFFSET_MIN,BRF_TX_DC_OFFSET_MAX);
    dumpDevAppend(getFpgaCorr(false,CorrFpgaPhase,int16Val),int16Val,"RxCorrFpgaPhase=",0);
    ADD_INTERVAL(-BRF_FPGA_CORR_MAX,BRF_FPGA_CORR_MAX);
    dumpDevAppend(getFpgaCorr(false,CorrFpgaGain,int16Val),int16Val,"RxCorrFpgaGain=",0);
    ADD_INTERVAL(-BRF_FPGA_CORR_MAX,BRF_FPGA_CORR_MAX);
    dumpDevAppend(getFpgaCorr(true,CorrFpgaPhase,int16Val),int16Val,"TxCorrFpgaPhase=",0);
    ADD_INTERVAL(-BRF_FPGA_CORR_MAX,BRF_FPGA_CORR_MAX);
    dumpDevAppend(getFpgaCorr(true,CorrFpgaGain,int16Val),int16Val,"TxCorrFpgaGain=",0);
    ADD_INTERVAL(-BRF_FPGA_CORR_MAX,BRF_FPGA_CORR_MAX);
    dumpDevAppendFreq(getFrequency(u32Val,false),u32Val,"RxFreq=",0);
    dumpDevAppendFreq(getFrequency(u32Val,true),u32Val,"TxFreq=",0);
    dumpDevAppend(getSamplerate(u32Val,false),u32Val,"RxSampRate=",0);
    dumpDevAppend(getSamplerate(u32Val,true),u32Val,"TxSampRate=",0);
    reportLpf(false);
    reportLpf(true);
    {
	BRF_TX_SERIALIZE_NONE;
	String tmp;
	buf.append("calibration-cache=" + dumpCalCache(tmp),sep);
    }
#undef ADD_INTERVAL
#undef BOARD_STATUS_SET_TMP
#undef BOARD_STATUS_SET
#undef dumpDevAppend
#undef dumpDevAppendFreq
#undef reportLpf
}

unsigned int BrfLibUsbDevice::dumpPeripheral(uint8_t dev, uint8_t addr, uint8_t len, String* buf)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("dumpPeripheral()");
    addr = clampInt(addr,0,0x7f);
    len = clampInt(len,1,128 - addr);
    return internalDumpPeripheral(dev,addr,len,buf,16);
}

// Module reload
void BrfLibUsbDevice::reLoad(const NamedList* params)
{
    NamedList dummy("");
    if (!params) {
	Lock lck(&__plugin);
	dummy = *s_cfg.createSection(YSTRING("general"));
	params = &dummy;
    }
    m_warnClamped = (float)params->getIntValue(YSTRING("warn_clamped"),0,0,100);
    setDataDump();
    checkTs(true,params->getIntValue(YSTRING("txcheckts"),0));
    checkTs(false,params->getIntValue(YSTRING("rxcheckts"),-1));
    updateAlterData(*params);
}

// dir: 0=both negative=rx positive=tx
// level: 0: both negative=app positive=device
void BrfLibUsbDevice::setDataDump(int dir, int level, const NamedList* p)
{
    static const String prefix[] = {"tx-data","tx-app","rx-data","rx-app"};

    NamedList dummy("");
    if (!p) {
	Lock lck(__plugin);
	dummy = *s_cfg.createSection(YSTRING("filedump"));
	p = &dummy;
    }
    NamedList* upd[4] = {0,0,0,0};
    if (dir >= 0) {
	if (level >= 0)
	    upd[0] = &m_txIO.dataDumpParams;
	if (level <= 0)
	    upd[1] = &m_txIO.upDumpParams;
    }
    if (dir <= 0) {
	if (level >= 0)
	    upd[2] = &m_rxIO.dataDumpParams;
	if (level <= 0)
	    upd[3] = &m_rxIO.upDumpParams;
    }
    Lock lck(m_dbgMutex);
    for (unsigned int i = 0; i < 4; i++) {
	if (!upd[i])
	    continue;
	const String& mode = (*p)[prefix[i] + "-mode"];
	int n = 0;
	if (mode == YSTRING("count")) {
	    String param = prefix[i] + "-count";
	    const String& s = (*p)[param];
	    if (s) {
		n = s.toInteger(-1);
		if (n <= 0) {
		    Debug(m_owner,DebugConf,"%s set to '%s': disabling dump [%p]",
			param.c_str(),s.c_str(),m_owner);
		    n = 0;
		}
	    }
	    else
		n = 10;
	}
	else if (mode.toBoolean())
	    n = -1;
	String file;
	if (n) {
	    file = (*p)[prefix[i] + "-file"];
	    if (!file)
		file = prefix[i] + "-${boardserial}";
	}
	upd[i]->clearParams();
	if (file) {
	    upd[i]->addParam("file",file);
	    upd[i]->addParam("count",String(n));
	}
	// Signal change
	upd[i]->assign("1");
    }
}

// Initialize the device.
// Call the reset method in order to set the device to a known state
bool BrfLibUsbDevice::open(const NamedList& params)
{
    BRF_RX_SERIALIZE_BOOL;
    BRF_TX_SERIALIZE_BOOL;
    doClose();
    String e;
    unsigned int status = 0;
    while (true) {
	BRF_FUNC_CALL_BREAK(resetUsb(&e));
	BRF_FUNC_CALL_BREAK(openDevice(true,&e));
	BRF_FUNC_CALL_BREAK(updateSpeed(params,&e));
	m_calCache.clear();
	readCalCache();
	status = updateFpga(params);
	if (status) {
	    e = "Failed to load FPGA";
	    break;
	}
	status = lusbSetAltInterface(BRF_ALTSET_IDLE,&e);
	if (status)
	    break;
	m_freqOffset = clampIntParam(params,"RadioFrequencyOffset",
	    BRF_FREQ_OFFS_DEF,BRF_FREQ_OFFS_MIN,BRF_FREQ_OFFS_MAX);
	// Init TX/RX buffers
	m_rxDcAuto = params.getBoolValue("rx_dc_autocorrect",true);
	m_rxShowDcInfo = params.getIntValue("rx_dc_showinfo");
	m_rxDcOffsetMax = BRF_RX_DC_OFFSET_DEF;
	m_rxIO.dcOffsetI = BRF_RX_DC_OFFSET_MAX + 1;
	m_rxIO.dcOffsetQ = BRF_RX_DC_OFFSET_MAX + 1;
	int tmpInt = 0;
	int i = 0;
	int q = 0;
	m_rxResyncCandidate = 0;
	m_rxTsPastIntervalMs = clampIntParam(params,"rx_ts_past_error_interval",200,50,10000);
#if 1
	i = clampIntParam(params,"RX.OffsetI",0,-BRF_RX_DC_OFFSET_MAX,BRF_RX_DC_OFFSET_MAX);
	q = clampIntParam(params,"RX.OffsetQ",0,-BRF_RX_DC_OFFSET_MAX,BRF_RX_DC_OFFSET_MAX);
	BRF_FUNC_CALL_BREAK(internalSetCorrectionIQ(false,i,q,&e));
#endif
	BRF_FUNC_CALL_BREAK(internalEnableRxVga(true,true,&e));
	BRF_FUNC_CALL_BREAK(internalEnableRxVga(true,false,&e));
	i = clampIntParam(params,"TX.OffsetI",0,BRF_TX_DC_OFFSET_MIN,BRF_TX_DC_OFFSET_MAX);
	q = clampIntParam(params,"TX.OffsetQ",0,BRF_TX_DC_OFFSET_MIN,BRF_TX_DC_OFFSET_MAX);
	BRF_FUNC_CALL_BREAK(internalSetCorrectionIQ(true,i,q,&e));
	// Set RX gain
	m_rxIO.vga1 = BRF_RXVGA1_GAIN_MAX + 1;
	BRF_FUNC_CALL_BREAK(internalSetGain(false,BRF_RXVGA2_GAIN_MIN));
	// Pre/post mixer TX VGA
	m_txIO.vga1Changed = false;
	const String& txVga1 = params["tx_vga1"];
	if (txVga1)
	    BRF_FUNC_CALL_BREAK(internalSetTxVga(txVga1.toInteger(BRF_TXVGA1_GAIN_DEF),true,&e));
	const String& txVga2 = params["tx_vga2"];
	if (txVga2)
	    BRF_FUNC_CALL_BREAK(internalSetTxVga(txVga2.toInteger(BRF_TXVGA2_GAIN_MIN),false,&e));
	// Set FPGA correction
	tmpInt = clampIntParam(params,"tx_fpga_corr_phase",0,-BRF_FPGA_CORR_MAX,BRF_FPGA_CORR_MAX);
	status = internalSetFpgaCorr(true,CorrFpgaPhase,tmpInt,&e,DebugConf);
	if (status)
	    break;
	tmpInt = clampIntParam(params,"tx_fpga_corr_gain",0,-BRF_FPGA_CORR_MAX,BRF_FPGA_CORR_MAX);
	status = internalSetFpgaCorr(true,CorrFpgaGain,tmpInt,&e,DebugConf);
	if (status)
	    break;
	// Make sure we have the correct values for status
	BRF_FUNC_CALL_BREAK(updateStatus(&e));
	// Set tx I/Q balance
	const String& txPB = params["tx_powerbalance"];
	internalSetTxIQBalance(false,txPB.toDouble(1),"tx_powerbalance");
	// Set some optional params
	setTxPattern(params["txpattern"]);
	showBuf(true,params.getIntValue("txbufoutput",0),
	    params.getBoolValue("txbufoutput_nodata"));
	showBuf(false,params.getIntValue("rxbufoutput",0),
	    params.getBoolValue("rxbufoutput_nodata"));
	m_silenceTimeMs = clampIntParam(params,"silence_time",5000,0,60000);
	break;
    }
    if (status) {
	Debug(m_owner,DebugWarn,"Failed to open USB device: %s [%p]",
	    e.safe("Unknown error"),m_owner);
	doClose();
	return false;
    }
    String s;
    internalDumpDev(s,true,false,"\r\n",true);
    Debug(m_owner,DebugAll,"Opened device [%p]%s",m_owner,encloseDashes(s,true));
    txSerialize.drop();
    rxSerialize.drop();
    reLoad(&params);
    return true;
}

// Close the device
void BrfLibUsbDevice::close()
{
    BRF_RX_SERIALIZE_NONE;
    BRF_TX_SERIALIZE_NONE;
    doClose();
}

// Power on the radio
// Enable timestamps, enable RF TX/RX
unsigned int BrfLibUsbDevice::powerOn()
{
    BRF_RX_SERIALIZE_BOOL;
    BRF_TX_SERIALIZE_BOOL;
    BRF_CHECK_DEV("powerOn()");
    return internalPowerOn(true);
}

// Send an array of samples waiting to be transmitted
unsigned int BrfLibUsbDevice::syncTx(uint64_t ts, float* data, unsigned int samples,
    float* powerScale)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("syncTx()");
    unsigned int code = send(ts,data,samples,powerScale);
    if (code == RadioInterface::HardwareIOError) {
	txSerialize.drop();
	Thread::yield();
    }
    return code;
}

// Receive data from the Rx interface of the bladeRF device
unsigned int BrfLibUsbDevice::syncRx(uint64_t& ts, float* data, unsigned int& samples)
{
    BRF_RX_SERIALIZE;
    BRF_CHECK_DEV("syncRx()");
    unsigned int code = recv(ts,data,samples);
    if (code == RadioInterface::HardwareIOError) {
	rxSerialize.drop();
	Thread::yield();
    }
    return code;
}

unsigned int BrfLibUsbDevice::setFrequency(uint32_t hz, bool tx)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("setFrequency()");
    return internalSetFrequency(tx,hz);
}

unsigned int BrfLibUsbDevice::getFrequency(uint32_t& hz, bool tx)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("getFrequency()");
    return internalGetFrequency(tx,&hz);
}

// Set frequency offset
unsigned int BrfLibUsbDevice::setFreqOffset(int offs, int* newVal)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("setFreqOffset()");
    return internalSetFreqOffs(offs,newVal);
}

// Get frequency offset
unsigned int BrfLibUsbDevice::getFreqOffset(int& offs)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("getFreqOffset()");
    String val;
    unsigned int status = getCalField(val,"DAC","DAC_TRIM");
    if (status == 0)
	offs = val.toInteger();
    return status;
}

// Set the bandwidth for a specific module
unsigned int BrfLibUsbDevice::setLpfBandwidth(uint32_t band, bool tx)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("setLpfBandwidth()");
    return internalSetLpfBandwidth(tx,band);
}

// Get the bandwidth for a specific module
unsigned int BrfLibUsbDevice::getLpfBandwidth(uint32_t& band, bool tx)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("getLpfBandwidth()");
    String e;
    unsigned int status = lusbSetAltInterface(BRF_ALTSET_RF_LINK,&e);
    if (status == 0) {
	uint8_t data = 0;
	status = lmsRead(lmsLpfAddr(tx),data,&e);
	if (status == 0) {
	    data >>= 2;
	    data &= 0xf;
	    band = s_bandSet[15 - data];
	    getIO(tx).lpfBw = band;
	}
    }
    if (status == 0)
	XDebug(m_owner,DebugAll,"Got %s LPF bandwidth %u [%p]",brfDir(tx),band,m_owner);
    else
	Debug(m_owner,DebugNote,"Failed to retrieve %s LPF bandwidth: %s [%p]",
	    brfDir(tx),e.c_str(),m_owner);
    return status;
}

unsigned int BrfLibUsbDevice::setLpf(int lpf, bool tx)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("setLpf()");
    return internalSetLpf(tx,lpf);
}

unsigned int BrfLibUsbDevice::getLpf(int& lpf, bool tx)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("getLpf()");
    return internalGetLpf(tx,&lpf);
}

// Set the sample rate on a specific module
unsigned int BrfLibUsbDevice::setSamplerate(uint32_t value, bool tx)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("setSamplerate()");
    return internalSetSampleRate(tx,value);
}

// Get the sample rate on a specific module
unsigned int BrfLibUsbDevice::getSamplerate(uint32_t& value, bool tx)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("getSamplerate()");
    String e;
    unsigned int status = lusbSetAltInterface(BRF_ALTSET_RF_LINK,&e);
    while (!status) {
	BrfRationalRate rate;
	Si5338MultiSynth synth;
	uint8_t val = 0;

	synth.index = 1;
	if (tx)
	    synth.index = 2;
	synth.base = 53 + synth.index * 11;
	// Read the enable bits
	if ((status = getSi5338(36 + synth.index,val,&e)) != 0)
	    break;
	synth.enable = val&7;
	// Read all of the multisynth registers
	for (int i = 0; i < 10; i++)
	    if ((status = getSi5338(synth.base + i,synth.regs[i],&e)) != 0)
		break;
	if (status)
	    break;
	// Populate the RxDIV value from the register
	if ((status = getSi5338(31 + synth.index,val,&e)) != 0)
	    break;
	// RxDIV is stored as a power of 2, so restore it on readback
	val = (val>>2)&7;
	synth.r = (1<<val);
	// Unpack the regs into appropriate values
	unpackRegs(synth);
	calcSrate(synth,rate);
	if (rate.integer > 0xffffffff) {
	    e = "The value for the sample rate is too big";
	    status = RadioInterface::Failure;
	    break;
	}
	if (rate.numerator)
	    Debug(m_owner,DebugMild,
		"Truncating the %s fractional part of the samplerate [%p]",
		brfDir(tx),m_owner);
	value = (uint32_t)rate.integer;
	getIO(tx).sampleRate = value;
	break;
    }
    if (status == 0)
	XDebug(m_owner,DebugAll,"Got %s samplerate %u [%p]",brfDir(tx),value,m_owner);
    else
	Debug(m_owner,DebugNote,"Failed to get %s samplerate: %s [%p]",
	    brfDir(tx),e.c_str(),m_owner);
    return status;
}

// Set the pre-mixer gain on transmission (interval [-35..-4])
// Set the post-mixer gain setting on transmission (interval: [0..25])
unsigned int BrfLibUsbDevice::setTxVga(int vga, bool preMixer)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("setTxVga()");
    return internalSetTxVga(vga,preMixer);
}

// Retrieve the pre/post mixer gain setting on transmission
unsigned int BrfLibUsbDevice::getTxVga(int& vga, bool preMixer)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("getTxVga()");
    return internalGetTxVga(&vga,preMixer);
}

// Set TX power balance
unsigned int BrfLibUsbDevice::setTxIQBalance(float value)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("setTxIQBalance()");
    return internalSetTxIQBalance(false,value);
}

// Enable or disable the pre/post mixer gain on the receive side
unsigned int BrfLibUsbDevice::enableRxVga(bool on, bool preMixer)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("enableRxVga()");
    return internalEnableRxVga(on,preMixer);
}

// Set the pre-mixer gain setting on the receive side (interval [5..30])
// Set the post-mixer Rx gain setting (interval [0..30])
unsigned int BrfLibUsbDevice::setRxVga(int vga, bool preMixer)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("setRxVga()");
    return internalSetRxVga(vga,preMixer);
}

// Retrieve the pre/post mixer rx gain setting
unsigned int BrfLibUsbDevice::getRxVga(int& vga, bool preMixer)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("getRxVga()");
    return internalGetRxVga(&vga,preMixer);
}

// Set pre and post mixer value
unsigned int BrfLibUsbDevice::setGain(bool tx, int val, int* newVal)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("setGain()");
    return internalSetGain(tx,val,newVal);
}

// Auto calibrate DC offsets
unsigned int BrfLibUsbDevice::calibrate()
{
    BRF_RX_SERIALIZE;
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("calibrate()");
#ifdef DEBUG
    Debugger d(DebugAll,"CALIBRATE"," %s [%p]",m_owner->debugName(),m_owner);
#endif
    Debug(m_owner,DebugInfo,"Calibrating ... [%p]",m_owner);
    String e;
    unsigned int status = calibrateAuto(&e);
    if (!status)
	status = calibrateBB(&e);
    m_calibrated = (status == 0);
    updateStatus();
    if (status) {
	if (status == RadioInterface::Cancelled)
	    Debug(m_owner,DebugInfo,"Calibration cancelled [%p]",m_owner);
	else
	    Debug(m_owner,DebugWarn,"Calibration failed: %s [%p]",e.c_str(),m_owner);
	return status;	
    }
    Debug(m_owner,DebugInfo,"Calibration finished [%p]",m_owner);
    return 0;
}

// Set Tx/Rx DC I/Q offset correction
unsigned int BrfLibUsbDevice::setDcOffset(bool tx, bool i, int16_t value)
{
    int rxDcAutoRestore = -1;
    // Temporary disable RX auto correct
    if (!tx) {
	BRF_RX_SERIALIZE;
	BRF_CHECK_DEV("setDcOffset()");
	rxDcAutoRestore = setRxDcAuto(false) ? 1 : 0;
    }
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("setDcOffset()");
    unsigned int status = internalSetDcOffset(tx,i,value);
    if (tx)
	return status;
    if (status == 0) {
	// Don't restore old RX DC autocorrect: the values are set by the upper layer
	if (rxDcAutoRestore > 0)
	    Debug(m_owner,DebugInfo,
		"Disabled RX DC autocorrect: I/Q values set by the upper layer [%p]",this);
    }
    else if (rxDcAutoRestore > 0)
	// Failure: restore old RX DC autocorrect
	m_rxDcAuto = true;
    return status;
}

// Retrieve Tx/Rx DC I/Q offset correction
unsigned int BrfLibUsbDevice::getDcOffset(bool tx, bool i, int16_t& value)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("getDcOffset()");
    unsigned int status = internalGetDcOffset(tx,i,&value);
    return status;
}

unsigned int BrfLibUsbDevice::setFpgaCorr(bool tx, int corr, int16_t value)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("setFpgaCorr()");
    return internalSetFpgaCorr(tx,corr,value);
}

unsigned int BrfLibUsbDevice::getFpgaCorr(bool tx, int corr, int16_t& value)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("getFpgaCorr()");
    int16_t v = 0;
    unsigned int status = internalGetFpgaCorr(tx,corr,&v);
    value = v;
    return status;
}

unsigned int BrfLibUsbDevice::getTimestamp(bool tx, uint64_t& ts)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("getTimestamp()");
    return internalGetTimestamp(tx,ts);
}

unsigned int BrfLibUsbDevice::writeLMS(uint8_t addr, uint8_t value, uint8_t* rst)
{
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("writeLMS()");
    if (rst)
	return lmsSet(addr,value,*rst);
    return lmsWrite(addr,value);
}

unsigned int BrfLibUsbDevice::setLoopback(const char* name)
{
    int mode = LoopNone;
    if (!TelEngine::null(name))
	mode = lookup(name,s_loopback,LoopUnknown);
    if (mode == LoopUnknown) {
	Debug(m_owner,DebugNote,"Unknown loopback mode '%s' [%p]",name,m_owner);
	return RadioInterface::OutOfRange;
    }
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("setLoopback()");
    return internalSetLoopback(mode);
}

// Enable or disable loopback
unsigned int BrfLibUsbDevice::setLoopback(int mode)
{
    if (!lookup(mode,s_loopback)) {
	Debug(m_owner,DebugNote,"Unknown loopback mode %d [%p]",mode,m_owner);
	return RadioInterface::OutOfRange;
    }
    BRF_TX_SERIALIZE;
    BRF_CHECK_DEV("setLoopback()");
    return internalSetLoopback(mode);
}

// Release data
void BrfLibUsbDevice::destruct()
{
    doClose();
    for (unsigned int i = 0; i < EpCount; i++)
	m_usbTransfer[i].reset();
    GenObject::destruct();
}

// Create an interface
BrfLibUsbDevice* BrfLibUsbDevice::create(BrfInterface* owner)
{
    if (!s_usbContextInit) {
	Lock lck(__plugin);
	if (!s_usbContextInit) {
	    int status = ::libusb_init(0);
	    if (status != LIBUSB_SUCCESS) {
		String tmp;
		Debug(&__plugin,DebugNote,"Failed to initialize libusb %s",
		    appendLusbError(tmp,status).c_str());
		return 0;
	    }
	    Debug(&__plugin,DebugAll,"Initialized libusb context");
	    s_usbContextInit = true;
	    lusbSetDebugLevel();
	}
    }
    return new BrfLibUsbDevice(owner);
}

uint64_t BrfLibUsbDevice::reduceFurther(uint64_t v1, uint64_t v2)
{
    if (!(v1 && v2))
	return 1;
    while (v2) {
	uint64_t tmp = v1 % v2;
	v1 = v2;
	v2 = tmp;	
    }
    return v1;
}

void BrfLibUsbDevice::reduceRational(BrfRationalRate& rate)
{
    while (rate.denominator > 0 && rate.numerator >= rate.denominator) {
	rate.numerator = rate.numerator - rate.denominator;
	rate.integer++;
    }
    // Reduce what's left of the fraction
    uint64_t val = reduceFurther(rate.numerator,rate.denominator);
    if (val) {
	rate.numerator /= val;
	rate.denominator /= val;
    }
}

void BrfLibUsbDevice::calcSrate(Si5338MultiSynth& synth, BrfRationalRate& rate)
{
    BrfRationalRate tmp;
    tmp.integer = synth.a;
    tmp.numerator = synth.b;
    tmp.denominator = synth.c;
    rate.integer = 0;
    rate.numerator = SI5338_F_VCO * tmp.denominator;
    rate.denominator = (uint64_t)synth.r * 2 *
	(tmp.integer * tmp.denominator + tmp.numerator);
    reduceRational(rate);
}

unsigned int BrfLibUsbDevice::calcMultiSynth(Si5338MultiSynth& synth,
    BrfRationalRate& rate, String* error)
{
    BrfRationalRate tmp;

    // Double requested frequency since LMS requires 2:1 clock:sample rate
    rationalDouble(rate);
    // Find a suitable R value
    uint8_t rValue = 1;
    uint8_t rPower = 0;
    while (rate.integer < 5000000 && rValue < 32) {
	rationalDouble(rate);
	rValue <<= 1;
	rPower++;
    }
    if (rValue == 32 && rate.integer < 5000000)
	return setError(RadioInterface::Failure,error,"Multi synth calculation failed");
    // Find suitable MS (a, b, c) values
    tmp.integer = 0;
    tmp.numerator= SI5338_F_VCO * rate.denominator;
    tmp.denominator= rate.integer * rate.denominator + rate.numerator;
    reduceRational(tmp);
    // Check values to make sure they are OK
    if (tmp.integer < 8 || tmp.integer > 567)
	return setError(RadioInterface::Failure,error,
	    "Multi synth calculation - the integer part is out of bounds");
    // Loss of precision if numeratoror denominatorare greater than 2^30-1
    bool warn = true;
    while (tmp.numerator > (1 << 30) || tmp.denominator > (1 << 30)) {
	if (warn) {
	    warn = false;
	    Debug(&__plugin,DebugMild,
		"Multi synth calculation: numerator or denominator are too big, we'll loose precision");
	}
	tmp.numerator >>= 1;
	tmp.denominator >>= 1;
    }
    if (tmp.integer > 0xffffffff || tmp.numerator > 0xffffffff ||
	tmp.denominator > 0xffffffff)
	return setError(RadioInterface::Failure,error,
	    "Multi synth calculation - rate parts are too big");
    synth.a = (uint32_t)tmp.integer;
    synth.b = (uint32_t)tmp.numerator;
    synth.c = (uint32_t)tmp.denominator;
    synth.r = rValue;
    // Pack the registers
    packRegs(synth);
    return 0;
}

void BrfLibUsbDevice::packRegs(Si5338MultiSynth& synth)
{
    uint64_t tmp = (uint64_t)synth.a * synth.c + synth.b;
    tmp = tmp * 128 ;
    tmp = tmp / synth.c - 512;
    synth.p1 = (uint32_t)tmp;
    tmp = (uint64_t)synth.b * 128;
    tmp = tmp % synth.c;
    synth.p2 = (uint32_t)tmp;
    synth.p3 = synth.c;
    // Set regs
    synth.regs[0] = (uint8_t)synth.p1;
    synth.regs[1] = (uint8_t)(synth.p1 >> 8);
    synth.regs[2] = (uint8_t)((synth.p2 & 0x3f) << 2) | ((synth.p1 >> 16) & 0x3);
    synth.regs[3] = (uint8_t)(synth.p2 >> 6);
    synth.regs[4] = (uint8_t)(synth.p2 >> 14);
    synth.regs[5] = (uint8_t)(synth.p2 >> 22);
    synth.regs[6] = (uint8_t)synth.p3;
    synth.regs[7] = (uint8_t)(synth.p3 >> 8);
    synth.regs[8] = (uint8_t)(synth.p3 >> 16);
    synth.regs[9] = (uint8_t)(synth.p3 >> 24);
}

void BrfLibUsbDevice::unpackRegs(Si5338MultiSynth& synth)
{
    // Populate
    synth.p1 = ((synth.regs[2] & 3) << 16) | (synth.regs[1] << 8) | (synth.regs[0]);
    synth.p2 = (synth.regs[5] << 22) | (synth.regs[4] << 14) |
	(synth.regs[3] << 6) | ((synth.regs[2] >> 2) & 0x3f);
    synth.p3 = ((synth.regs[9] & 0x3f) << 24) | (synth.regs[8] << 16) |
	(synth.regs[7] << 8) | (synth.regs[6]);
    // c = p3
    synth.c = synth.p3;
    // a = (p1+512)/128
    // NOTE: The +64 is for rounding purposes.
    synth.a = (synth.p1 + 512) / 128;
    // b = (((p1+512)-128*a)*c + (b % c) + 64)/128
    uint64_t tmp = (synth.p1 + 512) - 128 * (uint64_t)synth.a;
    tmp = (tmp * synth.c) + synth.p2;
    tmp = (tmp + 64) / 128;
    synth.b = (uint32_t)tmp;
}

unsigned int BrfLibUsbDevice::lusb2ifaceError(int code)
{
    switch (code) {
	case LIBUSB_ERROR_ACCESS:        // Access denied (insufficient permissions)
	case LIBUSB_TRANSFER_ERROR:      // Transfer failed
	case LIBUSB_ERROR_BUSY:          // Resource busy
	case LIBUSB_ERROR_INVALID_PARAM: // Invalid parameter
	case LIBUSB_ERROR_NO_MEM:        // Insufficient memory
	case LIBUSB_ERROR_OTHER:         // Unknown error
	    return RadioInterface::Failure;
	case LIBUSB_ERROR_TIMEOUT:       // Operation timed out
	case LIBUSB_TRANSFER_TIMED_OUT:  // Transfer timed out
	    return RadioInterface::Timeout;
	case LIBUSB_ERROR_INTERRUPTED:   // System call interrupted (perhaps due to signal)
	case LIBUSB_TRANSFER_CANCELLED:  // Transfer was cancelled
	    return RadioInterface::Cancelled;
	case LIBUSB_TRANSFER_STALL:      // For bulk/interrupt endpoints: halt condition detected (endpoint stalled)
                                         // For control endpoints: control request not supported
	    return RadioInterface::HardwareIOError;
	case LIBUSB_ERROR_NOT_FOUND:     // Entity not found
	case LIBUSB_ERROR_NO_DEVICE:     // No such device (it may have been disconnected)
	case LIBUSB_TRANSFER_NO_DEVICE:  // Device was disconnected
	    return RadioInterface::HardwareIOError;
	case LIBUSB_ERROR_IO:            // Input/output error
	case LIBUSB_ERROR_PIPE:          // Pipe error
	    return RadioInterface::HardwareIOError;
	case LIBUSB_ERROR_OVERFLOW:      // Overflow
	case LIBUSB_TRANSFER_OVERFLOW:   // Device sent more data than requested
	    return RadioInterface::Failure;
	case LIBUSB_ERROR_NOT_SUPPORTED: // Operation not supported or unimplemented on this platform
	    return RadioInterface::NotSupported;
	case LIBUSB_SUCCESS:             // Success (no error)
	    return RadioInterface::NoError;
#if LIBUSB_TRANSFER_COMPLETED != LIBUSB_SUCCESS
	case LIBUSB_TRANSFER_COMPLETED:  // Transfer completed without error
                                         // Note that this does not indicate that the entire amount of
                                         //   requested data was transferred.
	    return RadioInterface::NoError;
#endif
    }
    return RadioInterface::Failure;
}

unsigned int BrfLibUsbDevice::setStatus(const BrfDevStatus& stat, unsigned int flags,
    String* error)
{
    unsigned int status = 0;
    BRF_FUNC_CALL_RET(cancelled(error));
    unsigned int tmp = 0;
#define SET_STATUS_FUNC(flag,func) \
    if ((flags & flag) != 0) { \
	tmp = func; \
	if (tmp && !status) { \
	    error = 0; \
	    status = tmp; \
	} \
    }
    XDebug(m_owner,DebugAll,"Set %s status 0x%x [%p]",brfDir(stat.tx()),flags,m_owner);
    SET_STATUS_FUNC(DevStatLpfBw,internalSetLpfBandwidth(stat.tx(),stat.lpfBw,error));
    SET_STATUS_FUNC(DevStatSampleRate,internalSetSampleRate(stat.tx(),stat.sampleRate,error));
    SET_STATUS_FUNC(DevStatFreq,internalSetFrequency(stat.tx(),stat.frequency,error));
    SET_STATUS_FUNC(DevStatVga1,internalSetVga(stat.tx(),stat.vga1,true,error));
    SET_STATUS_FUNC(DevStatVga2,internalSetVga(stat.tx(),stat.vga2,false,error));
    SET_STATUS_FUNC(DevStatLpf,internalSetLpf(stat.tx(),stat.lpf,error));
    SET_STATUS_FUNC(DevStatDcI,internalSetDcOffset(stat.tx(),true,stat.dcOffsetI,error));
    SET_STATUS_FUNC(DevStatDcQ,internalSetDcOffset(stat.tx(),false,stat.dcOffsetQ,error));
    SET_STATUS_FUNC(DevStatFpgaPhase,internalSetFpgaCorr(stat.tx(),CorrFpgaPhase,
	stat.fpgaCorrPhase,error));
    SET_STATUS_FUNC(DevStatFpgaGain,internalSetFpgaCorr(stat.tx(),CorrFpgaGain,
	stat.fpgaCorrGain,error));
    SET_STATUS_FUNC(DevStatPowerBalance,internalSetTxIQBalance(false,stat.powerBalance));
    return status;
}

void BrfLibUsbDevice::internalDumpDev(String& buf, bool info, bool state,
    const char* sep, bool internal, bool fromStatus, bool withHdr)
{
    String tmp;
    if (state) {
	if (withHdr) {
	    buf.append("RxVGA1=",sep) << m_rxIO.vga1;
	    buf << sep << "RxVGA2=" << m_rxIO.vga2;
	    buf << sep << "RxDCCorrI=" << m_rxIO.dcOffsetI;
	    buf << sep << "RxDCCorrQ=" << m_rxIO.dcOffsetQ;
	    buf << sep << "TxVGA1=" << m_txIO.vga1;
	    buf << sep << "TxVGA2=" << m_txIO.vga2;
	    buf << sep << dumpFloatG(tmp,(double)m_rxIO.frequency / 1000000,"RxFreq=","MHz");
	    if (internal) {
		buf << sep << "TxDCCorrI=" << m_txIO.dcOffsetI;
		buf << sep << "TxDCCorrQ=" << m_txIO.dcOffsetQ;
	    }
	    buf << sep << dumpFloatG(tmp,(double)m_txIO.frequency / 1000000,"TxFreq=","MHz");
	    buf << sep << "FreqOffset=" << m_freqOffset;
	    buf << sep << "RxSampRate=" << m_rxIO.sampleRate;
	    buf << sep << "TxSampRate=" << m_txIO.sampleRate;
	    buf << sep << "RxLpfBw=" << m_rxIO.lpfBw;
	    buf << sep << "TxLpfBw=" << m_txIO.lpfBw;
	    buf << sep << "RxRF=" << onStr(m_rxIO.rfEnabled);
	    buf << sep << "TxRF=" << onStr(m_txIO.rfEnabled);
	    if (internal) {
		buf << sep << "RxLPF=" << lookup(m_rxIO.lpf,s_lpf);
		buf << sep << "TxLPF=" << lookup(m_txIO.lpf,s_lpf);
		buf << sep << "TxCorrFpgaPhase=" << m_txIO.fpgaCorrPhase;
	    }
	}
	else {
	    buf << "|" << m_rxIO.vga1;
	    buf << "|" << m_rxIO.vga2;
	    buf << "|" << m_rxIO.dcOffsetI;
	    buf << "|" << m_rxIO.dcOffsetQ;
	    buf << "|" << m_txIO.vga1;
	    buf << "|" << m_txIO.vga2;
	    buf << "|" << dumpFloatG(tmp,(double)m_rxIO.frequency / 1000000,0,"MHz");
	    buf << "|" << dumpFloatG(tmp,(double)m_txIO.frequency / 1000000,0,"MHz");
	    buf << "|" << m_freqOffset;
	    buf << "|" << m_rxIO.sampleRate;
	    buf << "|" << m_txIO.sampleRate;
	    buf << "|" << m_rxIO.lpfBw;
	    buf << "|" << m_txIO.lpfBw;
	    buf << "|" << onStr(m_rxIO.rfEnabled);
	    buf << "|" << onStr(m_txIO.rfEnabled);
	}
    }
    if (!info)
	return;
    if (withHdr) {
	buf.append("Address=",sep) << "USB/" << bus() << "/" << addr();
	buf << sep << "Serial=" << serial();
	buf << sep << "Speed=" << speedStr(speed());
	buf << sep << "Firmware=" << fwVerStr();
	buf << sep << "FPGA=" << fpgaVerStr();
	if (!fromStatus) {
	    buf.append(fpgaFile()," - ");
	    buf.append(fpgaMD5()," - MD5: ");
	}
    }
    else {
	if (buf)
	    buf << "|";
	buf << "USB/" << bus() << "/" << addr();
	buf << "|" << serial();
	buf << "|" << speedStr(speed());
	buf << "|" << fwVerStr();
	buf << "|" << fpgaVerStr();
    }
}

unsigned int BrfLibUsbDevice::internalPowerOn(bool rfLink, bool tx, bool rx, String* error)
{
    String e;
    unsigned int status = 0;
    BrfDevTmpAltSet tmpAltSet(this);
    if (rfLink)
	status = lusbSetAltInterface(BRF_ALTSET_RF_LINK,&e);
    else
	status = tmpAltSet.set(&e,"Power ON/OFF");
    bool warn = (tx != m_txIO.rfEnabled) || (rx != m_rxIO.rfEnabled);
    while (status == 0) {
	if (tx || rx)
	    BRF_FUNC_CALL_BREAK(enableTimestamps(true,&e));
	BRF_FUNC_CALL_BREAK(enableRf(true,tx,false,&e));
	BRF_FUNC_CALL_BREAK(enableRf(false,rx,false,&e))
	if (tx || rx) {
	    String extra;
	    if (!(tx && rx))
		extra << ", " << brfDir(tx) << " only";
	    Debug(m_owner,DebugNote,"Powered ON the radio%s [%p]",extra.safe(),m_owner);
	}
	else if (warn)
	    Debug(m_owner,DebugNote,"Powered OFF the radio [%p]",m_owner);
	return 0;
    }
    if (!warn)
	return 0;
    e.printf(1024,"Power %s failed: %s",((tx || rx) ? "ON" :"OFF"),e.c_str());
    return showError(status,e,0,error);
}

// Send an array of samples waiting to be transmitted
unsigned int BrfLibUsbDevice::send(uint64_t ts, float* data, unsigned int samples,
    float* powerScale)
{
#ifndef DEBUG_DEVICE_TX
    XDebug(m_owner,DebugAll,"send(" FMT64U ",%p,%u) [%p]",ts,data,samples,m_owner);
#endif
    BrfDevIO& io = m_txIO;
    if (!io.startTime)
	io.startTime = Time::now();
    if (io.dataDumpParams || io.upDumpParams)
	updateIODump(io);
    if (!(data && samples))
	return 0;
#ifdef DEBUG_DEVICE_TX
    Debugger debug(DebugInfo,"BrfLibUsbDevice::send()",
	" %s: ts=" FMT64U " (expected=" FMT64U ") samples=%u [%p]",
	m_owner->debugName(),ts,io.timestamp,samples,m_owner);
#endif
    if (io.upDumpFile.valid() && !(checkDbgInt(io.upDump) &&
	io.upDumpFile.write(ts,data,samplesf2bytes(samples),owner())))
	io.upDumpFile.terminate(owner());
    // Check timestamp
    if (io.timestamp != ts) {
	if (io.timestamp && m_owner->debugAt(DebugAll)) {
	    String s;
	    s << "(our=" << io.timestamp << " requested=" << ts << ")";
	    if (io.crtBuf || io.crtBufSampOffs)
		s << ", dropping previous data " <<
		    (io.crtBuf * io.bufSamples + io.crtBufSampOffs) << " samples";
	    Debug(m_owner,DebugAll,"TX: timestamps don't match %s [%p]",s.c_str(),m_owner);
	}
	io.resetBufPos();
	io.timestamp = ts;
    }
    float scale = 0;
    float* scaleI = &m_wrPowerScaleI;
    float* scaleQ = &m_wrPowerScaleQ;
    int16_t* maxI = &m_wrMaxI;
    int16_t* maxQ = &m_wrMaxQ;
    if (m_txPowerBalanceChanged) {
	m_txPowerBalanceChanged = false;
	m_wrPowerScaleI = m_txPowerScaleI * s_sampleEnergize;
	m_wrPowerScaleQ = m_txPowerScaleQ * s_sampleEnergize;
	m_wrMaxI = sampleScale(m_txPowerScaleI,s_sampleEnergize);
	m_wrMaxQ = sampleScale(m_txPowerScaleQ,s_sampleEnergize);
	if (io.showPowerBalanceChange == 0)
	    Debug(m_owner,DebugInfo,"TX using power scale I=%g Q=%g maxI=%d maxQ=%d [%p]",
		m_wrPowerScaleI,m_wrPowerScaleQ,m_wrMaxI,m_wrMaxQ,m_owner);
    }
    if (powerScale && m_wrPowerScaleI == s_sampleEnergize) {
	scale = *powerScale * s_sampleEnergize;
	scaleI = scaleQ = &scale;
	maxI = maxQ = &s_sampleEnergize;
    }
    if (m_txPatternChanged)
	sendTxPatternChanged();
    unsigned int clamped = 0;
    String e;
    unsigned int status = lusbSetAltInterface(BRF_ALTSET_RF_LINK,&e);
    unsigned int reqSend = samples;
    while (!status) {
	while (samples && io.crtBuf < io.buffers) {
	    unsigned int avail = 0;
	    int16_t* start = io.crtBufSamples(avail);
	    if (avail > samples)
		avail = samples;
	    // New buffer: set the timestamp
	    if (!io.crtBufSampOffs)
		io.setBufTs(io.crtBuf,io.timestamp);
	    samples -= avail;
#ifdef DEBUG_DEVICE_TX
	    Debugger loopDbg(DebugAll,"TX: processing buffer",
		" %u/%u ts=" FMT64U " avail=%u remains=%u [%p]",
		io.crtBuf + 1,io.buffers,io.timestamp,avail,samples,m_owner);
#endif
	    io.crtBufSampOffs += avail;
	    io.timestamp += avail;
	    if (m_txPatternBufSamples == 0) {
		brfCopyTxData(start,data,avail,*scaleI,*maxI,*scaleQ,*maxQ,clamped);
		data += avail * 2;
	    }
	    else
		sendCopyTxPattern(start,avail,*scaleI,*maxI,*scaleQ,*maxQ,clamped);
	    if (io.crtBufSampOffs >= io.bufSamples)
		io.advanceBuffer();
	}
	unsigned int nBuf = io.crtBuf;
	unsigned int oldBufSampOffs = nBuf ? io.crtBufSampOffs : 0;
	if (m_txIO.syncFlags || m_rxIO.syncFlags) {
	    if ((m_txIO.syncFlags & ~DevStatTs) != 0)
		setStatus(m_txIO.syncStatus,m_txIO.syncFlags);
	    if ((m_rxIO.syncFlags & ~DevStatTs) != 0)
		setStatus(m_rxIO.syncStatus,m_rxIO.syncFlags);
	    if ((io.syncFlags & DevStatTs) != 0)
		io.syncTs = ts + nBuf * io.bufSamples;
	    m_txIO.syncFlags = 0;
	    m_rxIO.syncFlags = 0;
	}
	if (nBuf < m_minBufsSend)
	    break;
	if (checkDbgInt(io.checkTs))
	    ioBufCheckTs(true,nBuf);
	else
	    io.lastTs = io.timestamp;
	unsigned int nPrint = checkDbgInt(io.showBuf,nBuf);
	if (nPrint)
	    printIOBuffer(true,"SEND",-1,nPrint);
	if (io.dataDumpFile.valid())
	    dumpIOBuffer(io,nBuf);
	status = syncTransfer(EpSendSamples,io.bufStart(0),io.bufLen * nBuf,&e);
	// Reset buffer to start
	io.resetBufPos();
	// Copy partial buffer from end
	if (oldBufSampOffs) {
#ifdef DEBUG_DEVICE_TX
	    Debug(m_owner,DebugMild,"TX: copying buffer %u to start [%p]",nBuf,m_owner);
#endif
	    ::memcpy(io.bufStart(0),io.bufStart(nBuf),io.hdrLen + samplesi2bytes(oldBufSampOffs));
	    io.crtBufSampOffs = oldBufSampOffs;
	}
	if (status)
	    break;
	io.transferred += nBuf * io.bufSamples;
    }
    if (status == 0) {
	if (clamped) {
	    float percent = 100 * (float)clamped / reqSend;
	    Debug(m_owner,(percent < m_warnClamped) ? DebugAll : DebugNote,
		"Output buffer clamped %u/%u (%.2f%%) [%p]",clamped,reqSend,percent,m_owner);
	}
	if (samples)
	    Debug(DebugFail,"Exiting with non 0 samples");
    }
    else if (status != RadioInterface::Cancelled)
	Debug(m_owner,DebugNote,"Send failed (TS=" FMT64U "): %s [%p]",
	    io.timestamp,e.c_str(),m_owner);
    return status;
}

void BrfLibUsbDevice::sendTxPatternChanged()
{
    Lock lck(m_dbgMutex);
    if (!m_txPatternChanged)
	return;
    m_txPatternChanged = false;
    m_txPatternBuffer.clear();
    m_txPatternBufSamples = bytes2samplesf(m_txPattern.length());
    if (m_txPatternBufSamples) {
	// Round up to use full TX buffers
	unsigned int bufs = (totalSamples(true) + m_txPatternBufSamples - 1) /
	    m_txPatternBufSamples;
	unsigned int n = samplesf2bytes(m_txPatternBufSamples);
	while (bufs--)
	    m_txPatternBuffer.append(m_txPattern.data(0),n);
	m_txPatternBufSamples = bytes2samplesf(m_txPatternBuffer.length());
	Debug(m_owner,DebugNote,"Using send pattern %u samples [%p]",
	    m_txPatternBufSamples,m_owner);
    }
    else
	m_txPatternBuffer.clear();
    m_txPatternBufPos = m_txPatternBufSamples;
}

void BrfLibUsbDevice::sendCopyTxPattern(int16_t* buf, unsigned int avail,
    float scaleI, int16_t maxI, float scaleQ, int16_t maxQ, unsigned int& clamped)
{
    while (avail) {
	if (m_txPatternBufPos == m_txPatternBufSamples)
	    m_txPatternBufPos = 0;
	unsigned int cp = m_txPatternBufSamples - m_txPatternBufPos;
	if (cp > avail)
	    cp = avail;
	float* b = (float*)m_txPatternBuffer.data(0) + m_txPatternBufPos * 2;
	avail -= cp;
	m_txPatternBufPos += cp;
	brfCopyTxData(buf,b,cp,scaleI,maxI,scaleQ,maxQ,clamped);
    }
}

// Receive data from the Rx interface of the bladeRF device
// Remember: a sample is an I/Q pair
unsigned int BrfLibUsbDevice::recv(uint64_t& ts, float* data, unsigned int& samples)
{
#ifndef DEBUG_DEVICE_RX
    XDebug(m_owner,DebugAll,"recv(" FMT64U ",%p,%u) [%p]",ts,data,samples,m_owner);
#endif
    BrfDevIO& io = m_rxIO;
    if (!io.startTime)
	io.startTime = Time::now();
    if (io.dataDumpParams || io.upDumpParams)
	updateIODump(io);
    if (!(data && samples))
	return 0;
#ifdef DEBUG_DEVICE_RX
    Debugger debug(DebugInfo,"BrfLibUsbDevice::recv()",
	" %s: ts=" FMT64U " samples=%u data=(%p) [%p]",
	m_owner->debugName(),ts,samples,data,m_owner);
#endif
    unsigned int samplesCopied = 0;
    unsigned int samplesLeft = samples;
    float* cpDest = data;
    uint64_t crtTs = ts;
    String e;
    unsigned int status = lusbSetAltInterface(BRF_ALTSET_RF_LINK,&e);
    unsigned int nSamplesInPast = 0;
    while (!status) {
	while (samplesLeft && io.crtBuf < io.buffers) {
	    // Retrieve buffer timestamp
	    uint64_t bufTs = io.bufTs(io.crtBuf);
	    if (io.crtBufSampOffs)
		bufTs += io.crtBufSampOffs;
	    int64_t resync = (io.newBuffer && bufTs != m_rxTimestamp) ?
		(int64_t)(bufTs - m_rxTimestamp) : 0;
#ifdef DEBUG_DEVICE_RX
	    String deltaStr;
	    if (resync)
		deltaStr << " " << (resync > 0 ? "future=" : "past=") <<
		    (resync > 0 ? resync : -resync);
	    Debugger loopDbg(DebugAll,"RX: processing buffer",
		" %u/%u rx_ts=" FMT64U " (resync_ts=" FMT64U ") "
		"ts=" FMT64U " crt_ts=" FMT64U "%s [%p]",
		io.crtBuf + 1,io.buffers,m_rxTimestamp,m_rxResyncCandidate,
		bufTs,crtTs,deltaStr.safe(),m_owner);
#endif
	    if (resync) {
		if ((resync > -1000 && resync < 1000) || bufTs == m_rxResyncCandidate) {
		    Debug(m_owner,bufTs > m_silenceTs ? DebugNote : DebugAll,
			"RX: timestamp adjusted by " FMT64 " to " FMT64U " [%p]",
			resync,bufTs,m_owner);
		    m_rxTimestamp = bufTs;
		    m_rxResyncCandidate = 0;
		}
		else {
		    Debug(m_owner,bufTs > m_silenceTs ? DebugWarn : DebugAll,
			"RX: timestamp jumped by " FMT64 " to " FMT64U " in buffer %u/%u [%p]",
			resync,m_rxTimestamp,io.crtBuf + 1,io.buffers,m_owner);
		    m_rxResyncCandidate = bufTs;
		}
	    }
	    io.newBuffer = false;
	    unsigned int avail = 0;
	    int16_t* start = io.crtBufSamples(avail);
	    if (avail > samplesLeft)
		avail = samplesLeft;
	    // Check timestamp
	    if (m_rxTimestamp > crtTs) {
		// Buffer timestamp is in the future
#ifdef DEBUG_DEVICE_RX
		if (crtTs)
		    Debug(m_owner,DebugNote,
			"RX: timestamp in future in buffer %u/%u requested="
			FMT64U " found=" FMT64U " [%p]",
			io.crtBuf + 1,io.buffers,crtTs,m_rxTimestamp,m_owner);
#endif
		// Pad with 0
		uint64_t delta = m_rxTimestamp - crtTs;
		if (delta > samplesLeft)
		    delta = samplesLeft;
		crtTs += delta;
		samplesLeft -= delta;
		samplesCopied += delta;
		::memset(cpDest,0,2 * delta * sizeof(float));
		cpDest += 2 * delta;
#ifdef DEBUG_DEVICE_RX
		Debug(m_owner,DebugAll,
		    "RX: zeroed %u samples status=%u/%u remains=%u [%p]",
		    (unsigned int)delta,samplesCopied,samples,samplesLeft,m_owner);
#endif
		if (!samplesLeft)
		    break;
		if (avail > samplesLeft)
		    avail = samplesLeft;
	    }
	    else if (m_rxTimestamp < crtTs) {
		// Timestamp in the past: check if can use some data, skip buffer
		unsigned int skipSamples = avail;
		uint64_t delta = crtTs - m_rxTimestamp;
		if (delta < skipSamples)
		    skipSamples = delta;
#ifdef DEBUG_DEVICE_RX
		Debug(m_owner,DebugNote,
		    "RX: skipping %u/%u samples in buffer %u/%u:"
		    " timestamp in the past by " FMT64U " [%p]",
		    skipSamples,avail,io.crtBuf + 1,io.buffers,delta,m_owner);
#endif
		avail -= skipSamples;
		nSamplesInPast += skipSamples;
		io.crtBufSampOffs += skipSamples;
		m_rxTimestamp += skipSamples;
		if (m_rxResyncCandidate)
		    m_rxResyncCandidate += skipSamples;
		if (io.crtBufSampOffs >= io.bufSamples) {
		    io.advanceBuffer();
		    continue;
		}
	    }
	    // We have some valid data: reset samples in the past counter
	    if (avail)
		nSamplesInPast = 0;
	    int16_t* last = start + avail * 2;
	    // Copy data
	    static const float s_mul = 1.0 / 2048;
	    while (start != last) {
		*cpDest++ = *start++ * s_mul;
		*cpDest++ = *start++ * s_mul;
	    }
	    samplesCopied += avail;
	    samplesLeft -= avail;
	    m_rxTimestamp += avail;
	    if (m_rxResyncCandidate)
		m_rxResyncCandidate += avail;
#ifdef DEBUG_DEVICE_RX
	    Debug(m_owner,DebugAll,
		"RX: copied %u samples from buffer %u/%u status=%u/%u remains=%u [%p]",
		avail,io.crtBuf + 1,io.buffers,samplesCopied,
		samples,samplesLeft,m_owner);
#endif
	    // Advance buffer offset, advance the buffer if we used all data
	    io.crtBufSampOffs += avail;
	    if (io.crtBufSampOffs >= io.bufSamples) {
		io.advanceBuffer();
		crtTs += avail;
	    }
	}
	if (!samplesLeft)
	    break;
	if (nSamplesInPast > m_rxTsPastSamples) {
	    // Don't signal error if we have some valid data
	    // This will allow the upper layer to update timestamps
	    // Read operation may fail on subsequent reads
	    if (!samplesCopied) {
		e = "Too much data in the past";
		status = RadioInterface::Failure;
	    }
	    break;
	}
	status = syncTransfer(EpReadSamples,io.bufStart(0),io.buffer.length(),&e);
	if (status)
	    break;
	io.resetBufPos();
	if (io.dataDumpFile.valid())
	    dumpIOBuffer(io,io.buffers);
	io.transferred += io.buffers * io.bufSamples;
	io.fixEndian();
	unsigned int nPrint = checkDbgInt(io.showBuf,io.buffers);
	if (nPrint)
	    printIOBuffer(false,"RECV",-1,nPrint);
	if (m_rxAlterData)
	    rxAlterData(true);
	if (checkDbgInt(io.checkTs))
	    ioBufCheckTs(false);
	if (m_rxDcAuto || m_rxShowDcInfo)
	    computeRx(crtTs);
	if (m_rxAlterData)
	    rxAlterData(false);
    }
    samples = samplesCopied;
#ifdef DEBUG_DEVICE_RX
    Debug(m_owner,DebugAll,
	"BrfLibUsbDevice::recv() exiting status=%u ts=" FMT64U " samples=%u [%p]",
	status,ts,samples,m_owner);
#endif
    if (status == 0) {
	m_rxIO.timestamp = ts;
	if (io.upDumpFile.valid() && !(checkDbgInt(io.upDump) &&
	    io.upDumpFile.write(ts,data,samplesf2bytes(samples),owner())))
	    io.upDumpFile.terminate(owner());
    }
    else if (status != RadioInterface::Cancelled)
	Debug(m_owner,DebugNote,"Recv failed: %s [%p]",e.c_str(),m_owner);
    return status;
}

unsigned int BrfLibUsbDevice::internalSetSampleRate(bool tx, uint32_t value,
    String* error)
{
    String e;
    unsigned int status = lusbSetAltInterface(BRF_ALTSET_RF_LINK,&e);
    while (!status) {
	Si5338MultiSynth synth;
	BrfRationalRate rate;

	rate.integer = value;
	rate.numerator = 0; // keeping the numerator and the donominator
	rate.denominator = 1; // for future use
	// Enforce minimum sample rate
	reduceRational(rate);
	if (rate.integer < BRF_SAMPLERATE_MIN)
	    Debug(m_owner,DebugGoOn,
		"Requested sample rate is smaller than the allowed minimum value [%p]",
		m_owner);
	// Setup the multisynth enables and index
	synth.enable = 0x01;
	synth.index = 1;
	if (tx) {
	    synth.enable |= 0x02;
	    synth.index = 2;
	}
	// Update the base address register
	synth.base = 53 + synth.index * 11;
	// Calculate multisynth values
	BRF_FUNC_CALL_BREAK(calcMultiSynth(synth,rate,&e));
	// Program it to the part
	// Write out the enables
	uint8_t val = 0;
	BRF_FUNC_CALL_BREAK(getSi5338(36 + synth.index,val,&e));
	val &= ~(7);
	val |= synth.enable;
	BRF_FUNC_CALL_BREAK(setSi5338(36 + synth.index,val,&e));
	// Write out the registers
	for (int i = 0 ; i < 10; i++)
	    BRF_FUNC_CALL_BREAK(setSi5338(synth.base + i,*(synth.regs + i),&e));
	if (status)
	    break;
	// Calculate r_power from c_count
	uint8_t rPower = 0;
	uint8_t rCount = synth.r >> 1;
	while (rCount) {
	    rCount >>= 1;
	    rPower++;
	}
	// Set the r value to the log2(r_count) to match Figure 18
	val = 0xc0;
	val |= (rPower<<2);
	BRF_FUNC_CALL_BREAK(setSi5338(31 + synth.index,val,&e));
	if (getIO(tx).sampleRate != value) {
	    getIO(tx).sampleRate = value;
	    Debug(m_owner,DebugInfo,"%s samplerate set to %u [%p]",
		brfDir(tx),value,m_owner);
	}
	if (!tx) {
	    unsigned int samplesMs = (value + 999) / 1000;
	    // Calculate RX samples allowed to be in the past
	    m_rxTsPastSamples = m_rxTsPastIntervalMs * samplesMs;
	    // Calculate RX timestamp silence
	    m_silenceTs = m_silenceTimeMs * samplesMs;
	}
	return 0;
    }
    e.printf(1024,"Failed to set %s samplerate %u: %s",brfDir(tx),value,e.c_str());
    return showError(status,e,0,error);
}

// Update FPGA (load, get version)
unsigned int BrfLibUsbDevice::updateFpga(const NamedList& params)
{
    const String& oper = params[YSTRING("fpga_load")];
    int load = 0;
    if (!oper)
	load = 1;
    else if (oper == YSTRING("auto")) {
	unsigned int code = checkFpga();
	if (code == RadioInterface::NoError)
	    load = -1;
	else {
	    load = 1;
	    if (code != RadioInterface::NotInitialized)
		Debug(m_owner,DebugNote,"Forcing FPGA load (check failure) [%p]",m_owner);
	}
    }
    else
	load = oper.toBoolean(true) ? 1 : 0;
    if (load > 0)
	Debug(m_owner,DebugAll,"Updating FPGA [%p]",m_owner);
    else
	Debug(m_owner,DebugInfo,"Skipping FPGA load: %s [%p]",
	    (load ? "checked, already loaded" : "disabled by config"),m_owner);
    m_devFpgaFile.clear();
    m_devFpgaVerStr.clear();
    m_devFpgaMD5.clear();
    String e;
    unsigned int status = 0;
    while (load > 0) {
	MD5 md5;
	String val;
	status = getCalField(val,"B","FPGA size",&e);
	if (status)
	    break;
	String fName;
	if (val == YSTRING("115") || val == YSTRING("40"))
	    fName = params.getValue("fpga_file_" + val,
		"${sharedpath}/data/hostedx" + val + ".rbf");
	else {
	    e << "Unknown FPGA size value '" << val << "'";
	    status = RadioInterface::Failure;
	    break;
	}
	Engine::runParams().replaceParams(fName);
	const char* oper = 0;
	// Read the FPGA contents
	File f;
	DataBlock buf;
	if (f.openPath(fName,false,true)) {
	    int64_t len = f.length();
	    if (len > 0) {
		buf.assign(0,len);
		int rd = f.readData(buf.data(),buf.length());
		if (rd != len)
		    oper = "read";
	    }
	    else if (f.error())
		oper = "detect length";
	}
	else
	    oper = "open";
	if (oper) {
	    status = RadioInterface::Failure;
	    String tmp;
	    Thread::errorString(tmp,f.error());
	    e << "File '" << fName << "' " << oper << " failed (" <<
		f.error() << " '" << tmp << "')";
	    break;
	}
	md5 << buf;
	Debug(m_owner,DebugAll,"Loading FPGA from '%s' len=%u [%p]",
	    fName.c_str(),buf.length(),m_owner);
	// Write the FPGA
	BrfDevTmpAltSet tmpAltSet(this,BRF_ALTSET_FPGA,status,&e,"FPGA load");
	if (status)
	    break;
	status = vendorCommand0_4(BRF_USB_CMD_BEGIN_PROG,LIBUSB_ENDPOINT_IN,&e);
	if (status == 0) {
	    status = lusbBulkTransfer(BRF_ENDP_TX_CTRL,buf.data(0),buf.length(),
		0,&e,3 * m_bulkTout);
	    if (status == 0) {
		status = vendorCommand0_4(BRF_USB_CMD_QUERY_FPGA_STATUS,
		    LIBUSB_ENDPOINT_IN,&e);
		if (status)
		    e = "Failed to end FPGA programming - " + e;
	    }
	    else
		e = "Failed to send FPGA image - " + e;
	}
	else
	    e = "Failed to start FPGA programming - " + e;
	tmpAltSet.restore();
	status = restoreAfterFpgaLoad(&e);
	if (status == 0) {
	    m_devFpgaFile = fName;
	    m_devFpgaMD5 = md5.hexDigest();
	    Debug(m_owner,DebugAll,"Loaded FPGA from '%s' [%p]",fName.c_str(),m_owner);
	}
	break;
    }
    if (status == 0) {
	uint32_t ver = 0;
	if (getFpgaVersion(ver) == 0)
	    ver2str(m_devFpgaVerStr,ver);
    }
    else
	Debug(m_owner,DebugWarn,"Failed to load FPGA: %s [%p]",e.c_str(),m_owner);
    return status;
}

unsigned int BrfLibUsbDevice::internalSetFpgaCorr(bool tx, int corr, int16_t value,
    String* error, int lvl)
{
    XDebug(m_owner,DebugAll,"internalSetFpgaCorr(%u,%d,%d) [%p]",tx,corr,value,m_owner);
    String e;
    unsigned int status = 0;
    int orig = value;
    String what;
    int a = fpgaCorrAddr(tx,corr,what);
    int* changed = 0;
    if (a >= 0) {
	BrfDevIO& io = getIO(tx);
	if (corr == CorrFpgaGain) {
	    changed = &io.fpgaCorrGain;
	    orig = clampInt(orig,-BRF_FPGA_CORR_MAX,BRF_FPGA_CORR_MAX,"FPGA GAIN",lvl);
	    value = orig + BRF_FPGA_CORR_MAX;
	}
	else if (corr == CorrFpgaPhase) {
	    changed = (io.showFpgaPhaseChange == 0) ? &io.fpgaCorrPhase : 0;
	    orig = clampInt(orig,-BRF_FPGA_CORR_MAX,BRF_FPGA_CORR_MAX,"FPGA PHASE",lvl);
	    value = orig;
	}
	status = gpioWrite(a,value,2,&e);
    }
    else
	status = setUnkValue(e,0,"FPGA corr value " + String(corr));
    if (status) {
	e.printf(1024,"Failed to set %s FPGA corr %s to %d (from %d) - %s [%p]",
	    brfDir(tx),what.c_str(),value,orig,e.c_str(),m_owner);
	return showError(status,e,0,error);
    }
    if (changed && *changed != orig)
	Debug(m_owner,DebugInfo,"%s FPGA corr %s set to %d (from %d) [%p]",
	    brfDir(tx),what.c_str(),value,orig,m_owner);
    return 0;
}

unsigned int BrfLibUsbDevice::internalGetFpgaCorr(bool tx, int corr, int16_t* value,
    String* error)
{
    int16_t v = 0;
    String e;
    unsigned int status = 0;
    String what;
    int a = fpgaCorrAddr(tx,corr,what);
    if (a >= 0) {
	uint32_t u = 0;
	status = gpioRead(a,u,2,&e);
	if (status == 0) {
	    v = (int)u;
	    if (corr == CorrFpgaGain)
		v -= BRF_FPGA_CORR_MAX;
	    if (value)
		*value = v;
	}
    }
    else
	status = setUnkValue(e,0,"FPGA corr value " + String(corr));
    if (status) {
	e.printf(1024,"Failed to retrieve %s FPGA corr %s - %s [%p]",
	    brfDir(tx),what.c_str(),e.c_str(),m_owner);
	return showError(status,e,0,error);
    }
    XDebug(m_owner,DebugAll,"Got %s FPGA corr %s %d [%p]",
	brfDir(tx),what.c_str(),v,m_owner);
    if (corr == CorrFpgaGain)
	getIO(tx).fpgaCorrGain = v;
    else if (corr == CorrFpgaPhase)
	getIO(tx).fpgaCorrPhase = v;
    return 0;
}

unsigned int BrfLibUsbDevice::internalSetTxVga(int vga, bool preMixer, String* error)
{
    String e;
    unsigned int status = 0;
    BrfDevTmpAltSet tmpAltSet(this,status,&e,"TX VGA set");
    while (status == 0) {
	uint8_t addr = lmsVgaAddr(true,preMixer);
	uint8_t data = 0;
	BRF_FUNC_CALL_BREAK(lmsRead(addr,data,&e));
	if (preMixer) {
	    vga = clampInt(vga,BRF_TXVGA1_GAIN_MIN,BRF_TXVGA1_GAIN_MAX,"TX VGA1");
	    data = (uint8_t)((vga - BRF_TXVGA1_GAIN_MIN) & 0x1f);
	}
	else {
	    vga = clampInt(vga,BRF_TXVGA2_GAIN_MIN,BRF_TXVGA2_GAIN_MAX,"TX VGA2");
	    data &= ~0xf8;
	    data |= (uint8_t)(vga << 3);
	}
	BRF_FUNC_CALL_BREAK(lmsWrite(addr,data,&e));
	if (preMixer)
	    m_txIO.vga1Changed = true;
	int& old = preMixer ? m_txIO.vga1 : m_txIO.vga2;
	if (old != vga) {
	    old = vga;
	    Debug(m_owner,DebugInfo,"TX VGA%c set to %ddB (0x%x) [%p]",
		mixer(preMixer),vga,data,m_owner);
	    if (!preMixer)
		internalSetTxIQBalance(true);
	}
	return 0;
    }
    e.printf(1024,"Failed to set TX VGA%c to from %d: %s",mixer(preMixer),vga,e.c_str());
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::internalGetTxVga(int* vga, bool preMixer, String* error)
{
    uint8_t data = 0;
    String e;
    int v = 0;
    unsigned int status = lmsRead(lmsVgaAddr(true,preMixer),data,&e);
    if (status == 0) {
	if (preMixer) {
	    v = (int)(data & 0x1f) + BRF_TXVGA1_GAIN_MIN;
	    m_txIO.vga1 = v;
	}
	else {
	    v = (data >> 3) & 0x1f;
	    if (v > BRF_TXVGA2_GAIN_MAX)
		v = BRF_TXVGA2_GAIN_MAX;
	    m_txIO.vga2 = v;
	}
	if (vga)
	    *vga = v;
	XDebug(m_owner,DebugAll,"Got TX VGA%c %ddB (0x%x) [%p]",
	    mixer(preMixer),v,data,m_owner);
	return 0;
    }
    e.printf(1024,"Failed to retrieve TX VGA%c: %s",mixer(preMixer),e.c_str());
    return showError(status,e,0,error);
}

// Enable or disable the pre/post mixer gain on the receive side
unsigned int BrfLibUsbDevice::internalEnableRxVga(bool on, bool preMixer, String* error)
{
    XDebug(m_owner,DebugAll,"internalEnableRxVga(%u,%u) [%p]",on,preMixer,m_owner);
    String e;
    unsigned int status = 0;
    BrfDevTmpAltSet tmpAltSet(this,status,&e,"Enable RX VGA");
    while (status == 0) {
	uint8_t addr = preMixer ? 0x7d : 0x64;
	uint8_t data = 0;
	BRF_FUNC_CALL_BREAK(lmsRead(addr,data,&e));
	bool old = false;
	if (preMixer) {
	    old = (data & 0x08) == 0;
	    if (on)
		data &= ~0x08;
	    else
		data |= 0x08;
	}
	else {
	    old = (data & 0x02) == 1;
	    if (on)
		data |= 0x02;
	    else
		data &= ~0x02;
	}
	BRF_FUNC_CALL_BREAK(lmsWrite(addr,data,&e));
	if (old != on)
	    Debug(m_owner,DebugInfo,"%s RX VGA%c [%p]",
		enabledStr(on),mixer(preMixer),m_owner);
	return 0;
    }
    e.printf(1024,"Failed to %s RX VGA%c: %s",enableStr(on),mixer(preMixer),e.c_str());
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::internalSetRxVga(int vga, bool preMixer, String* error)
{
    String e;
    unsigned int status = 0;
    BrfDevTmpAltSet tmpAltSet(this,status,&e,"RX VGA set");
    while (status == 0) {
	uint8_t addr = lmsVgaAddr(false,preMixer);
	uint8_t data = 0;
	BRF_FUNC_CALL_BREAK(lmsRead(addr,data,&e));
	int orig = vga;
	bool changed = false;
	if (preMixer) {
	    vga = clampInt(vga,BRF_RXVGA1_GAIN_MIN,BRF_RXVGA1_GAIN_MAX,"RX VGA1");
	    data = (uint8_t)((data & ~0x7f) | s_rxvga1_set[vga]);
	    BRF_FUNC_CALL_BREAK(lmsWrite(addr,data,&e));
	    changed = (m_rxIO.vga1 != vga);
	    m_rxIO.vga1 = vga;
	}
	else {
	    vga = clampInt(vga / 3 * 3,BRF_RXVGA2_GAIN_MIN,BRF_RXVGA2_GAIN_MAX,"RX VGA2");
	    data = (uint8_t)((data & ~0x1f) | (vga / 3));
	    BRF_FUNC_CALL_BREAK(lmsWrite(addr,data,&e));
	    changed = (m_rxIO.vga2 != vga);
	    m_rxIO.vga2 = vga;
	    m_rxDcOffsetMax = (int)brfRxDcOffset(clampInt(orig,BRF_RXVGA2_GAIN_MIN,BRF_RXVGA2_GAIN_MAX));
	}
	if (changed)
	    Debug(m_owner,DebugInfo,"RX VGA%c set to %ddB 0x%x (from %d) [%p]",
		mixer(preMixer),vga,data,orig,m_owner);
	return 0;
    }
    e.printf(1024,"Failed to set RX VGA%c to %d: %s",mixer(preMixer),vga,e.c_str());
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::internalGetRxVga(int* vga, bool preMixer, String* error)
{
    uint8_t data = 0;
    String e;
    unsigned int status = lmsRead(lmsVgaAddr(false,preMixer),data,&e);
    if (status == 0) {
	int v = 0;
	if (preMixer) {
	    int idx = (data & 0x7f);
	    m_rxIO.vga1 = v = s_rxvga1_get[idx < 121 ? idx : 120];
	}
	else {
	    m_rxIO.vga2 = v = (data & 0x1f) * 3;
	    m_rxDcOffsetMax = (int)brfRxDcOffset(clampInt(v,BRF_RXVGA2_GAIN_MIN,BRF_RXVGA2_GAIN_MAX));
	}
	XDebug(m_owner,DebugAll,"Got RX VGA%c %ddB (reg=0x%x) [%p]",
	    mixer(preMixer),v,data,m_owner);
	if (vga)
	    *vga = v;
	return 0;
    }
    e.printf(1024,"Failed to retrieve RX VGA%c: %s",mixer(preMixer),e.c_str());
    return showError(status,e,0,error);
}

// Set pre and post mixer value
unsigned int BrfLibUsbDevice::internalSetGain(bool tx, int val, int* newVal, String* error)
{
    int vga1 = 0;
    if (tx) {
	vga1 = (m_txIO.vga1Changed && m_txIO.vga1 >= BRF_TXVGA1_GAIN_MIN) ?
	    m_txIO.vga1 : BRF_TXVGA1_GAIN_DEF;
	val = clampInt(val + BRF_TXVGA2_GAIN_MAX,BRF_TXVGA2_GAIN_MIN,BRF_TXVGA2_GAIN_MAX);
    }
    else {
	vga1 = m_rxIO.vga1 > BRF_RXVGA1_GAIN_MAX ? BRF_RXVGA1_GAIN_MAX : m_rxIO.vga1;
	val = clampInt(val,BRF_RXVGA2_GAIN_MIN,BRF_RXVGA2_GAIN_MAX);
    }
    unsigned int status = internalSetVga(tx,vga1,true,error);
    if (!status)
	status = internalSetVga(tx,val,false,error);
    if (status == 0 && newVal) {
	*newVal = val;
	if (tx)
	    *newVal -= BRF_TXVGA2_GAIN_MAX;
    }
    return status;
}

unsigned int BrfLibUsbDevice::internalSetTxIQBalance(bool newGain, float newBalance,
    const char* param)
{
    bool dbg = true;
    if (!newGain) {
	if (newBalance <= 0 || newBalance >= 2) {
	    if (!param) {
		Debug(m_owner,DebugNote,
		    "Failed to set power balance to %g expected interval (0..2) [%p]",
		    newBalance,m_owner);
		return RadioInterface::OutOfRange;
	    }
	    Debug(m_owner,DebugConf,"Invalid %s=%g defaults to 1 [%p]",
		param,newBalance,m_owner);
	    newBalance = 1;
	}
	if (m_txPowerBalance != newBalance) {
	    dbg = (m_txIO.showPowerBalanceChange == 0);
	    if (dbg)
		Debug(m_owner,DebugInfo,"TX power balance changed %g -> %g [%p]",
		    m_txPowerBalance,newBalance,m_owner);
	    m_txPowerBalance = newBalance;
	}
    }
    float oldI = m_txPowerScaleI;
    float oldQ = m_txPowerScaleQ;
    // Update TX power scale
    m_txPowerScaleI = m_txPowerScaleQ = 1;
    if (m_txPowerBalance > 1)
	m_txPowerScaleQ /= m_txPowerBalance;
    else if (m_txPowerBalance < 1)
	m_txPowerScaleI *= m_txPowerBalance;
    if (oldI == m_txPowerScaleI && oldQ == m_txPowerScaleQ)
	return 0;
    if (dbg)
	Debug(m_owner,DebugInfo,"TX power scale changed I: %g -> %g Q: %g -> %g [%p]",
	    oldI,m_txPowerScaleI,oldQ,m_txPowerScaleQ,m_owner);
    m_txPowerBalanceChanged = true;
    return 0;
}

unsigned int BrfLibUsbDevice::internalSetFreqOffs(int val, int* newVal, String* error)
{
    val = clampInt(val,BRF_FREQ_OFFS_MIN,BRF_FREQ_OFFS_MAX,"FrequencyOffset");
    String e;
    unsigned int status = gpioWrite(0x22,(val & 0xff) << 8,2,&e);
    if (status == 0) {
	if (m_freqOffset != val) {
	    Debug(m_owner,DebugInfo,"FrequencyOffset set to %d [%p]",val,m_owner);
	    m_freqOffset = val;
	}
	if (newVal)
	    *newVal = val;
	return 0;
    }
    return showError(status,e,"FrequencyOffset set failed",error);
}

unsigned int BrfLibUsbDevice::internalSetFrequency(bool tx, uint32_t hz, String* error)
{
    XDebug(m_owner,DebugAll,"BrfLibUsbDevice::setFrequency(%u,%s) [%p]",
	hz,brfDir(tx),m_owner);
    String e;
    unsigned int status = 0;
    BrfDevTmpAltSet tmpAltSet(this,status,&e,"frequency set");
    while (!status) {
	status = RadioInterface::Failure;
	uint8_t addr = lmsFreqAddr(tx);
	clampFrequency(hz,tx,"setFrequency");
	uint8_t pllFreq = 0xff;
	for (int i = 0; s_freqLimits[i]; i += 3)
	    if (hz >= s_freqLimits[i] && hz <= s_freqLimits[i + 1]) {
		pllFreq = s_freqLimits[i + 2];
		break;
	    }
	if (pllFreq == 0xff) {
	    status = setUnkValue(e,"frequency " + String(hz));
	    break;
	}
	// Integer part
	uint64_t vco_x = ((uint64_t)1) << ((pllFreq & 7) - 3);
	uint64_t tmp = (vco_x * hz) / s_freqRefClock;
	if (tmp > 0xffff) {
	    e.printf("The integer part " FMT64U " of the frequency is too big",tmp);
	    status = RadioInterface::Failure;
	    break;
	}
	uint16_t nint = (uint16_t)tmp;
	// Fractional part
	tmp = (1 << 23) * (vco_x * hz - nint * s_freqRefClock);
	tmp = (tmp + s_freqRefClock / 2) / s_freqRefClock;
	if (tmp > 0xffffffff) {
	    e.printf("The fractional part " FMT64U " of the frequency is too big",tmp);
	    status = RadioInterface::Failure;
	    break;
	}
	uint32_t nfrac = (uint32_t)tmp;
	bool lowBand = brfIsLowBand(hz);
	// Reset CLK_EN for Rx/Tx DSM SPI
	BRF_FUNC_CALL_BREAK(lmsSet(0x09,0x05,&e));
	// Set PLL frequency and output buffer selection
	pllFreq <<= 2;
	pllFreq |= (lowBand ? 0x01 : 0x02);
	BRF_FUNC_CALL_BREAK(lmsWrite(addr + 5,pllFreq,&e));
	// Set frequency NINT/NFRAC
	uint8_t d[4] = {(uint8_t)(nint >> 1),
	    (uint8_t)(((nint & 1) << 7) | ((nfrac >> 16) & 0x7f)),
	    (uint8_t)(nfrac >> 8),(uint8_t)nfrac};
	BRF_FUNC_CALL_BREAK(accessPeripheral(UartDevLMS,true,addr,d,&e,4));
	// Set PLL currents (ICHP=1.2mA, OFFUP=30uA, OFFDOWN=0mA)
	BRF_FUNC_CALL_BREAK(lmsSet(addr + 6,0x0c,0x1f,&e));
	BRF_FUNC_CALL_BREAK(lmsSet(addr + 7,0x03,0x1f,&e));
	BRF_FUNC_CALL_BREAK(lmsSet(addr + 8,0x00,0x1f,&e));
	// Loop through the VCOCAP to figure out optimal values
	BRF_FUNC_CALL_BREAK(tuneVcocap(addr,&e));
	// Reset CLK_EN for Rx/Tx DSM SPI
	BRF_FUNC_CALL_BREAK(lmsReset(0x09,0x05,&e));
	// Select PA/LNA amplifier (don't do it if loopback is enabled)
	if (m_loopback == LoopNone)
	    BRF_FUNC_CALL_BREAK(selectPaLna(tx,lowBand,&e));
	// Set GPIO band according to the frequency
	uint32_t gpio = 0;
	BRF_FUNC_CALL_BREAK(gpioRead(0,gpio,4,&e));
	uint32_t band = lowBand ? 2 : 1;
	int shift = tx ? 3 : 5;
	gpio &= (uint32_t)~(3 << shift);
	gpio |= (uint32_t)(band << shift);
	BRF_FUNC_CALL_BREAK(gpioWrite(0,gpio,4,&e));
	BRF_FUNC_CALL_BREAK(internalSetFreqOffs(m_freqOffset,0,&e));
	break;
    }
    if (status) {
	e.printf(1024,"Failed to set %s frequency to %uHz - %s",brfDir(tx),hz,e.c_str());
	return showError(status,e,0,error);
    }
    if (getIO(tx).frequency != hz) {
	getIO(tx).frequency = hz;
	Debug(m_owner,DebugInfo,"%s frequency set to %gMHz offset=%u [%p]",
	    brfDir(tx),(double)hz / 1000000,m_freqOffset,m_owner);
    }
    return 0;
}

unsigned int BrfLibUsbDevice::internalGetFrequency(bool tx, uint32_t* hz, String* error)
{
    String e;
    unsigned int status = 0;
    uint32_t freq = 0;
    BrfDevTmpAltSet tmpAltSet(this,status,&e,tx ? "TX frequency get" : "RX frequency get");
    while (!status) {
	uint8_t addr = lmsFreqAddr(tx);
	uint8_t data = 0;
	uint64_t fint = 0; // Integer part of the freq
	// Reading the integer part of the frequency
	BRF_FUNC_CALL_BREAK(lmsRead(addr + 0,data,&e));
	fint = (uint64_t)data << 24;
	BRF_FUNC_CALL_BREAK(lmsRead(addr + 1,data,&e));
	fint |= (data & 0x80) << 16;
	// Read the fractionary part of the frequency
	fint |= ((uint64_t)data & 0x7f) << 16;
	BRF_FUNC_CALL_BREAK(lmsRead(addr + 2,data,&e));
	fint |= (uint64_t)data << 8;
	BRF_FUNC_CALL_BREAK(lmsRead(addr + 3,data,&e));
	fint |= data;
	// read the freq division
	BRF_FUNC_CALL_BREAK(lmsRead(addr + 5,data,&e));
	uint32_t division = data >> 2; // VCO division ratio
	division = 1 << ((division & 7) + 20);
	freq = (uint32_t)(((s_freqRefClock * fint) + (division >> 1)) / division);
	break;
    }
    if (status == 0) {
	getIO(tx).frequency = freq;
	if (hz)
	    *hz = freq;
	XDebug(m_owner,DebugAll,"Got %s frequency %uHz [%p]",brfDir(tx),freq,m_owner);
	return 0;
    }
    e.printf(1024,"Failed to retrieve %s frequency - %s",brfDir(tx),e.c_str());
    return showError(status,e,0,error);
}

// Retrieve TX/RX timestamp
unsigned int BrfLibUsbDevice::internalGetTimestamp(bool tx, uint64_t& ts, String* error)
{
    String e;
    unsigned int status = 0;
    while (true) {
	uint32_t low = 0;
	uint32_t high = 0;
	uint8_t addr = tx ? 0x18 : 0x10;
	BRF_FUNC_CALL_BREAK(gpioRead(addr,low,4,&e));
	BRF_FUNC_CALL_BREAK(gpioRead(addr + 4,high,4,&e));
	ts = ((uint64_t)high << 31) | (low >> 1);
	XDebug(m_owner,DebugAll,"Got %s ts=" FMT64U " [%p]",brfDir(tx),ts,m_owner);
	return 0;
    }
    e.printf(1024,"Failed to retrieve %s timestamp - %s",brfDir(tx),e.c_str());
    return showError(status,e,0,error);
}

// USB peripheral operation method
unsigned int BrfLibUsbDevice::lusbSetAltInterface(int val, String* error)
{
    if (m_altSetting == val)
	return 0;
    unsigned int status = 0;
    String e;
    if (val >= BRF_ALTSET_MIN && val <= BRF_ALTSET_MAX)
	status = lusbCheckSuccess(
	    ::libusb_set_interface_alt_setting(m_devHandle,0,val),&e);
    else
	status = setUnkValue(e);
    if (status == 0) {
	DDebug(m_owner,DebugAll,"Alt interface changed %s -> %s [%p]",
	    altSetName(m_altSetting),altSetName(val),m_owner);
	m_altSetting = val;
	return 0;
    }
    String prefix;
    prefix << "Failed to change alt interface to ";
    if (val >= BRF_ALTSET_MIN && val <= BRF_ALTSET_MAX)
	prefix << altSetName(val);
    else
	prefix << val;
    return showError(status,e,prefix,error);
}

// Wrapper for libusb_control_transfer
unsigned int BrfLibUsbDevice::lusbCtrlTransfer(uint8_t reqType, int8_t request,
    uint16_t value, uint16_t index, uint8_t* data, uint16_t len, String* error,
    unsigned int tout)
{
#ifdef DEBUGGER_DEVICE_METH
    String tmp;
    //tmp.hexify(data,len,' ');
    //tmp = " data=" + tmp;
    Debugger d(DebugAll,"BrfLibUsbDevice::lusbCtrlTransfer()",
	" (0x%x,0x%x,0x%x,%u,%p,%u,%u)%s [%p]",
	reqType,request,value,index,data,len,tout,tmp.safe(),m_owner);
#endif
    int code = ::libusb_control_transfer(m_devHandle,reqType,request,value,
	index,data,len,tout ? tout : m_ctrlTout);
    if (code == (int)len)
	return 0;
    String e;
    unsigned int status = (code < 0) ? lusbCheckSuccess(code,&e) :
	RadioInterface::Failure;
    return showError(status,e,"Incomplete USB CTRL transfer",error);
}

// Wrapper for libusb bulk transfer
unsigned int BrfLibUsbDevice::lusbBulkTransfer(uint8_t endpoint, uint8_t* data,
    unsigned int len, unsigned int* transferred, String* error, unsigned int tout)
{
#ifdef DEBUGGER_DEVICE_METH
    Debugger d(DebugAll,"BrfLibUsbDevice::lusbBulkTransfer()",
	" (0x%x,%p,%u,%u) [%p]",endpoint,data,len,tout,m_owner);
#endif
    int nIO = 0;
    int code = ::libusb_bulk_transfer(m_devHandle,endpoint,
	data,len,&nIO,tout ? tout : m_bulkTout);
    if (transferred)
	*transferred = nIO;
    if ((int)len == nIO)
	return 0;
    String e;
    unsigned int status = (code < 0) ? lusbCheckSuccess(code,&e) :
	RadioInterface::Failure;
    return showError(status,e,"Incomplete USB BULK transfer",error);
}

// Make an async usb transfer
unsigned int BrfLibUsbDevice::syncTransfer(int ep, uint8_t* data, unsigned int len,
    String* error)
{
    LusbTransfer& t = m_usbTransfer[ep];
#ifdef DEBUG
    if ((ep == EpReadSamples || ep == EpSendSamples) &&
	!getIO(ep == EpSendSamples).mutex.locked())
	Debug(m_owner,DebugFail,"syncTransfer() %s not locked [%p]",
	    brfDir(ep == EpSendSamples),m_owner);
    if (t.running())
	Debug(m_owner,DebugFail,"EP %s transfer is running [%p]",
	    lookup(ep,s_usbEndpoint),m_owner);
#endif
#ifdef DEBUGGER_DEVICE_METH
    String tmp;
    //tmp.hexify(data,len,' ');
    //tmp = " data=" + tmp;
    Debugger d(DebugAll,"BrfLibUsbDevice::syncTransfer()",
	" (%s,%p,%u) transfer=(%p)%s [%p]",
	lookup(ep,s_usbEndpoint),data,len,&t,tmp.safe(),m_owner);
#else
    XDebug(m_owner,DebugAll,"syncTransfer ep=%s data=(%p) len=%u [%p]",
	lookup(ep,s_usbEndpoint),data,len,m_owner);
#endif
    t.running(true);
    unsigned int cStatus = 0;
    bool checkCancelled = !m_closingDevice;
    if (t.fillBulk(data,len,m_syncTout) && t.submit()) {
	while (t.running()) {
	    struct timeval tv;
	    tv.tv_usec = 3 * Thread::idleUsec();
	    tv.tv_sec = 0;
	    ::libusb_handle_events_timeout_completed(m_context,&tv,0);
	    if (checkCancelled && t.running() && cStatus == 0 &&
		(cStatus = cancelled()) != 0) {
		Debug(m_owner,DebugInfo,"Cancelling transfer %s [%p]",
		    lookup(ep,s_usbEndpoint),m_owner);
		t.cancel();
	    }
	}
    }
    Lock lck(t);
    t.running(false);
    if (checkCancelled && !t.status)
	t.status = cancelled(&t.error);
    return showError(t.status,t.error,"SYNC transfer failed",error);
}

// Read the value of a specific GPIO register
unsigned int BrfLibUsbDevice::gpioRead(uint8_t addr, uint32_t& value, uint8_t len,
    String* error, const char* loc)
{
    len = clampInt(len,1,sizeof(value),"GPIO read items",DebugGoOn);
    uint8_t t[sizeof(value)];
    unsigned int status = accessPeripheral(UartDevGPIO,false,addr,t,error,len,loc);
    if (status)
	return status;
    value = 0;
    // Data is in little endian order
#ifdef LITTLE_ENDIAN
    for (uint8_t i = 0; i < len; i++)
	value |= (uint64_t)(t[i] << (i * 8));
#else
    for (uint8_t i = 0; i < len; i++)
	value |= (uint64_t)(t[i] << ((len - i - 1) * 8));
#endif
    return 0;
}

// Write a value to a specific GPIO register
unsigned int BrfLibUsbDevice::gpioWrite(uint8_t addr, uint32_t value, uint8_t len,
    String* error, const char* loc)
{
    if (addr == 0) {
	if (m_devSpeed == LIBUSB_SPEED_SUPER)
	    value &= ~BRF_GPIO_SMALL_DMA_XFER;
	else if (m_devSpeed == LIBUSB_SPEED_HIGH)
	    value |= BRF_GPIO_SMALL_DMA_XFER;
	else
	    Debug(m_owner,DebugGoOn,"GPIO write: unhandled speed [%p]",m_owner);
    }
    len = clampInt(len,1,sizeof(value),"GPIO write items",DebugGoOn);
    uint8_t t[sizeof(value)];
    // Data is in little endian order
#ifdef LITTLE_ENDIAN
    for (uint8_t i = 0; i < len; i++)
	t[i] = (uint8_t)(value >> (i * 8));
#else
    for (uint8_t i = 0; i < len; i++)
	t[i] = (uint8_t)(value >> ((len - i - 1) * 8));
#endif
    return accessPeripheral(UartDevGPIO,true,addr,t,error,len,loc);
}

unsigned int BrfLibUsbDevice::lmsWrite(const String& str, String* error)
{
    if (!str)
	return 0;
    String e;
    unsigned int status = 0;
    while (true) {
	DataBlock db;
	if (!db.unHexify(str)) {
	    status = setError(RadioInterface::Failure,&e,"Invalid hex string");
	    break;
	}
	if ((db.length() % 2) != 0) {
	    status = setError(RadioInterface::Failure,&e,"Invalid string length");
	    break;
	}
	Debug(m_owner,DebugAll,"Writing '%s' to LMS [%p]",str.c_str(),m_owner);
	uint8_t* b = db.data(0);
	for (unsigned int i = 0; !status && i < db.length(); i += 2) {
	    b[i] &= ~0x80;
	    status = lmsWrite(b[i],b[i + 1],&e);
	}
	if (!status)
	    return 0;
	break;
    }
    e.printf(1024,"LMS write '%s' failed - %s",str.c_str(),e.c_str());
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::lnaSelect(int lna, String* error)
{
    String e;
    unsigned int status = 0;
    bool valid = (lna >= 0 && lna <= 3);
    while (valid) {
	uint8_t data = 0;
	BRF_FUNC_CALL_BREAK(lmsRead(0x75,data,&e));
	BRF_FUNC_CALL_BREAK(lmsWrite(0x75,(data & ~0x30) | (lna << 4),&e));
	int old = (data >> 4) & 0x03;
	int level = old != lna ? DebugInfo : DebugAll;
	if (lna != LmsLnaNone)
	    Debug(m_owner,level,"LNA %d selected [%p]",lna,m_owner);
	else
	    Debug(m_owner,level,"LNAs disabled [%p]",m_owner);
	return 0;
    }
    if (!valid)
	status = setUnkValue(e);
    if (lna != LmsLnaNone)
	e.printf(1024,"Failed to select LNA %d - %s",lna,e.c_str());
    else
	e.printf(1024,"Failed to disable LNAs - %s",e.c_str());
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::lnaEnable(bool on, String* error)
{
    String e;
    unsigned int status = 0;
    while (true) {
	uint8_t data = 0;
	BRF_FUNC_CALL_BREAK(lmsRead(0x7d,data,&e));
	BRF_FUNC_CALL_BREAK(lmsWrite(0x7d,on ? (data & ~0x01) : (data | 0x01),&e));
	Debug(m_owner,on == ((data & 0x01) == 0) ? DebugAll : DebugInfo,
	    "%s LNA [%p]",enabledStr(on),m_owner);
	return 0;
    }
    e.printf("Failed to %s LNA - %s",enableStr(on),e.c_str());
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::lnaGainSet(uint8_t value, String* error)
{
    const char* what = lookup(value,s_lnaGain);
    XDebug(m_owner,DebugAll,"lnaGainSet(%u,'%s') [%p]",value,what,m_owner);
    String e;
    unsigned int status = 0;
    if (!what || value == LnaGainUnhandled)
	status = setUnkValue(e);
    while (status == 0) {
	uint8_t data = 0;
	BRF_FUNC_CALL_BREAK(lmsRead(0x75,data,&e));
	uint8_t old = (uint8_t)((data >> 6) & 0x03);
	data &= ~(3 << 6);
	data |= ((value & 3) << 6);
	BRF_FUNC_CALL_BREAK(lmsWrite(0x75,data,&e));
	if (old != value)
	    Debug(m_owner,DebugInfo,"LNA GAIN set to %s [%p]",what,m_owner);
	return 0;
    }
    e.printf(1024,"Failed to set LNA GAIN %u (%s) - %s",value,what,e.c_str());
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::lnaGainGet(uint8_t& value, String* error)
{
    uint8_t data = 0;
    unsigned int status = lmsRead(0x75,data,error,"LNA gain read register");
    if (status)
	return status;
    data >>= 6;
    data &= 3;
    value = data;
    if (value != LnaGainUnhandled)
	return 0;
    String e;
    e.printf("LNA gain read abnormal value 0x%x",data);
    return showError(RadioInterface::OutOfRange,e,0,error);
}

unsigned int BrfLibUsbDevice::internalSetLpfBandwidth(bool tx, uint32_t band,
    String* error)
{
    String e;
    unsigned int status = lusbSetAltInterface(BRF_ALTSET_RF_LINK,&e);
    while (!status) {
	uint8_t data = 0;
	uint8_t reg = lmsLpfAddr(tx);
	BRF_FUNC_CALL_BREAK(lmsRead(reg,data,&e));
	unsigned int i = 0;
	for (; i < 15 && band > s_bandSet[i]; i++)
	    ;
	uint8_t bw = (uint8_t)(15 - i);
	data &= ~0x3c; // Clear out previous bandwidth setting
	data |= (bw << 2); // Apply new bandwidth setting
	BRF_FUNC_CALL_BREAK(lmsWrite(reg,data,&e));
	getIO(tx).lpfBw = s_bandSet[i];
	Debug(m_owner,DebugAll,"%s LPF bandwidth set to %u (from %u, reg=0x%x) [%p]",
	    brfDir(tx),getIO(tx).lpfBw,band,data,m_owner);
	return 0;
    }
    e.printf(1024,"Failed to set %s LPF bandwidth %u: %s",brfDir(tx),band,e.c_str());
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::internalSetLpf(bool tx, int lpf, String* error)
{
    const char* what = lookup(lpf,s_lpf);
    XDebug(m_owner,DebugAll,"internalSetLpf(%u,%d,'%s') [%p]",tx,lpf,what,m_owner);
    uint8_t addr = lmsLpfAddr(tx);
    uint8_t reg1 = 0;
    uint8_t reg2 = 0;
    String e;
    unsigned int status = 0;
    if (what)
	status = lmsRead2(addr,reg1,addr + 1,reg2,&e);
    else
	status = setUnkValue(e,0,"value");
    if (status == 0) {
	// Clear EN_LPF
	switch (lpf) {
	    case LpfDisabled:
		reg1 &= 0xfd;  // Disable LPF: reset EN_LPF
		reg2 &= 0xbf;  // Normal operation: reset BYP_EN_LPF
		break;
	    case LpfBypass:
		reg1 &= 0xfd;  // Disable LPF: reset EN_LPF
		reg1 |= 0x40;  // Bypass LPF: set BYP_EN_LPF
		break;
	    case LpfNormal:
		reg1 |= 0x02;  // Enable LPF: set EN_LPF
		reg2 &= 0xbf;  // Normal operation: reset BYP_EN_LPF
		break;
	    default:
		status = setUnkValue(e,0,"value");
	}
	if (status == 0) {
	    status = lmsWrite2(addr,reg1,addr + 1,reg2,&e);
	    if (status == 0) {
		if (getIO(tx).lpf != lpf) {
		    getIO(tx).lpf = lpf;
		    Debug(m_owner,DebugInfo,"%s LPF set to '%s' [%p]",
			brfDir(tx),what,m_owner);
		}
		return 0;
	    }
	}
    }
    e.printf(1024,"Failed to set %s LPF %u (%s) - %s",
	brfDir(tx),lpf,TelEngine::c_safe(what),e.c_str());
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::internalGetLpf(bool tx, int* lpf, String* error)
{
    uint8_t addr = lmsLpfAddr(tx);
    uint8_t reg1 = 0;
    uint8_t reg2 = 0;
    String e;
    unsigned int status = lmsRead2(addr,reg1,addr + 1,reg2,&e);
    if (status == 0) {
	int l = decodeLpf(reg1,reg2);
	if (l != LpfInvalid) {
	    getIO(tx).lpf = l;
	    if (lpf)
		*lpf = l;
	    XDebug(m_owner,DebugAll,"Got %s LPF %d (%s) [%p]",
		brfDir(tx),l,lookup(l,s_lpf),m_owner);
	    return 0;
	}
	status = RadioInterface::OutOfRange;
	e = "Invalid values, enabled and bypassed";
    }
    e.printf(1024,"Failed to retrieve %s LPF - %s",brfDir(tx),e.c_str());
    return showError(status,e,0,error);
}

// Fill the m_list member of the class
unsigned int BrfLibUsbDevice::updateDeviceList(String* error)
{
    clearDeviceList();
    int n = ::libusb_get_device_list(m_context,&m_list);
    if (n >= 0) {
	m_listCount = n;
	return 0;
    }
    String e;
    unsigned int status = lusbCheckSuccess(n,&e);
    return showError(status,e,"Failed to enumerate USB devices",error);
}

// Enable/disable RF and sample circulation on both RX and TX sides
unsigned int BrfLibUsbDevice::enableRf(bool tx, bool on, bool frontEndOnly, String* error)
{
#ifdef DEBUGGER_DEVICE_METH
    Debugger d(DebugAll,"BrfLibUsbDevice::enableRf()",
	" tx=%s on=%s frontEndOnly=%s [%p]",String::boolText(tx),String::boolText(on),
	String::boolText(frontEndOnly),m_owner);
#endif
    BrfDevIO& io = getIO(tx);
    unsigned int status = 0;
    String e;
    if (!m_devHandle) {
	if (!on) {
	    io.setRf(false);
	    return 0;
	}
	status = RadioInterface::NotInitialized;
	e = "Not open";
    }
    while (!status) {
	// RF front end
	uint8_t addr = tx ? 0x40 : 0x70;
	uint8_t val = tx ? 0x02 : 0x01;
	status = lmsChangeMask(addr,val,on,&e);
	if (status || frontEndOnly)
	    break;
	// Samples circulation
	uint8_t request = tx ? BRF_USB_CMD_RF_TX : BRF_USB_CMD_RF_RX;
	uint32_t buf = (uint32_t)-1;
	uint16_t value = on ? 1 : 0;
	status = lusbCtrlTransfer(LUSB_CTRLTRANS_IFACE_VENDOR_IN,
	    request,value,0,(uint8_t*)&buf,sizeof(buf),&e);
	if (status == 0 && le32toh(buf))
	    status = setError(RadioInterface::Failure,&e,"Device failure");
	if (status)
	    e = "Samples circulation change failed - " + e;
	break;
    }
    if (io.rfEnabled == on) {
	io.setRf(on && status == 0);
	return status;
    }
    io.setRf(on && status == 0);
    const char* fEnd = frontEndOnly ? " front end" : "";
    if (status == 0) {
	Debug(m_owner,DebugAll,"%s RF %s%s [%p]",
	    enabledStr(on),brfDir(tx),fEnd,m_owner);
	return 0;
    }
    e.printf(1024,"Failed to %s RF %s%s - %s",enableStr(on),brfDir(tx),fEnd,e.c_str());
    return showError(status,e,0,error);
}

// Read the FPGA version
unsigned int BrfLibUsbDevice::getFpgaVersion(uint32_t& version)
{
#ifdef DEBUGGER_DEVICE_METH
    Debugger d(DebugAll,"BrfLibUsbDevice::getFpgaVersion()");
#endif
    BRF_CHECK_DEV("getFpgaVersion()");
    String e;
    unsigned int status = 0;
    BrfDevTmpAltSet tmpAltSet(this,status,&e,"FPGA version get");
    if (!status)
	status = gpioRead(0x0c,version,4,&e);
    if (status)
	Debug(m_owner,DebugNote,"Failed to retrieve FPGA version: %s [%p]",
	    e.c_str(),m_owner);
    return status;
}

// Check if fpga is loaded
unsigned int BrfLibUsbDevice::checkFpga()
{
    String error;
    int32_t data = 0;
    unsigned int status = vendorCommand(BRF_USB_CMD_QUERY_FPGA_STATUS,BRF_ENDP_RX_SAMPLES,
	data,&error);
    if (status == 0) {
	if (le32toh(data)) {
	    Debug(m_owner,DebugAll,"The FPGA is already configured [%p]",m_owner);
	    return 0;
	}
	Debug(m_owner,DebugAll,"The FPGA is not configured [%p]",m_owner);
	return RadioInterface::NotInitialized;
    }
    Debug(m_owner,DebugNote,"FPGA check failed: %s [%p]",error.c_str(),m_owner);
    return status;
}

// Restore device after loading the FPGA
unsigned int BrfLibUsbDevice::restoreAfterFpgaLoad(String* error)
{
    String e;
    unsigned int status = 0;
    BrfDevTmpAltSet tmpAltSet(this,status,&e,"Restore after FPGA load");
    while (!status) {
	uint32_t gpio = 0;
	status = gpioRead(0,gpio,4,&e);
	if (status)
	    break;
	if (gpio & 0x7fff) {
	    e.printf("Unexpected FPGA state 0x%x",gpio);
	    status = RadioInterface::Failure;
	    break;
	}
	// Enable the LMS and select the low band
	status = gpioWrite(0,0x57,4,&e,"Failed to enable LMS and/or low band");
	if (status)
	    break;
	// Disable the TX/RX
	if ((status = enableRfBoth(false,true,&e)) != 0)
	    break;
	// Enabling LMS on TX side
	status = lmsWrite(0x05,0x3e,&e,"Failed to enable LMS TX");
	if (status)
	    break;
	status = lmsWrite(0x47,0x40,&e,"Could not set the bias current for the LO");
	if (status)
	    break;
	status = lmsWrite(0x59,0x09,&e,"Could not set the ADC");
	if (status)
	    break;
	status = lmsWrite(0x64,0x36,&e,"Could not set the common mode for the ADC");
	if (status)
	    break;
	status = lmsWrite(0x79,0x37,&e,"Could not set the LNA gain");
	break;
    }
    if (status == 0) {
	XDebug(m_owner,DebugAll,"Restored device after FPGA load [%p]",m_owner);
	return 0;
    }
    return showError(status,e,"Failed to restore device after FPGA load",error);
}

// Reset the Usb interface using an ioctl call
unsigned int BrfLibUsbDevice::resetUsb(String* error)
{
    String e;
    unsigned int status = openDevice(false,&e);
    if (status)
	return showError(status,e,"USB reset failed",error);
    status = lusbCheckSuccess(::libusb_reset_device(m_devHandle),&e,"USB reset failed ");
    if (!status)
	Debug(m_owner,DebugAll,"Reset USB device bus=%d addr=%d [%p]",
	    m_devBus,m_devAddr,m_owner);
    closeDevice();
    return showError(status,e,0,error);
}

// Set the VCTCXO configuration to the correct value
unsigned int BrfLibUsbDevice::tuneVcocap(uint8_t addr, String* error)
{
    uint8_t data = 0;
    unsigned int status = lmsRead(addr + 9,data,error,"VCTCXO tune");
    if (status)
	return status;
    uint8_t vcocap = 32;
    uint8_t vtune = 0;
    uint8_t step = vcocap >> 1;
    data &= ~(0x3f);
    for (int i = 0; i < 6; i++) {
	if ((status = lmsWrite(addr + 9,vcocap | data,error,"VCTCXO tune")) != 0)
	    return status;
	if ((status = lmsRead(addr + 10,vtune,error,"VCTCXO tune")) != 0)
	    return status;
	vtune >>= 6;
	if (vtune == VCO_NORM) {
	    XDebug(m_owner,DebugInfo,"tuneVcocap: Found normal VCO [%p]",m_owner);
	    break;
	}
	if (vtune == VCO_HIGH) {
	    XDebug(m_owner,DebugInfo,"tuneVcocap: VCO high [%p]",m_owner);
	    vcocap += step;
	}
	else if (vtune == VCO_LOW) {
	    XDebug(m_owner,DebugInfo,"tuneVcocap: VCO low [%p]",m_owner);
	    vcocap -= step ;
	}
	else
	    return setError(RadioInterface::Failure,error,
		"VCTCXO tune - invalid tunning");
	step >>= 1;
    }
    if (vtune != VCO_NORM)
	return setError(RadioInterface::Failure,error,"VCTCXO tune - tunning not locked");
    uint8_t start = vcocap;
    while (start > 0 && vtune == VCO_NORM) {
	start--;
	if ((status = lmsWrite(addr + 9,start | data,error,"VCTCXO tune")) != 0)
	    return status;
	if ((status = lmsRead(addr + 10,vtune,error,"VCTCXO tune")) != 0)
	    return status;
	vtune >>= 6;
    }
    start++;
    XDebug(m_owner,DebugInfo,"tuneVcocap: Found lower limit %u [%p]",start,m_owner);
    if ((status = lmsWrite(addr + 9,vcocap | data,error,"VCTCXO tune")) != 0)
	return status;
    if ((status = lmsRead(addr + 10,vtune,error,"VCTCXO tune")) != 0)
	return status;
    vtune >>= 6;
    uint8_t stop = vcocap;
    while (stop < 64 && vtune == VCO_NORM) {
	stop++;
	if ((status = lmsWrite(addr + 9,stop | data,error,"VCTCXO tune")) != 0)
	    return status;
	if ((status = lmsRead(addr + 10,vtune,error,"VCTCXO tune")) != 0)
	    return status;
	vtune >>= 6;
    }
    stop--;
    XDebug(m_owner,DebugAll,"tuneVcocap: Found lower limit %u [%p]",stop,m_owner);
    vcocap = (start + stop) >> 1;
    XDebug(m_owner,DebugInfo,"tuneVcocap: VCOCAP=%u [%p]",vcocap,m_owner);
    if ((status = lmsWrite(addr + 9,vcocap | data,error,"VCTCXO tune")) != 0)
	return status;
    if ((status = lmsRead(addr + 10,vtune,error,"VCTCXO tune")) != 0)
	return status;
    vtune >>= 6;
    DDebug(m_owner,DebugInfo,"tuneVcocap: VCTCXO=%u [%p]",vtune,m_owner);
    if (vtune == VCO_NORM)
	return 0;
    return setError(RadioInterface::Failure,error,"VCTCXO tune failed");
}

// Send requests to the bladeRF device regarding the FPGA image configuration.
unsigned int BrfLibUsbDevice::vendorCommand(uint8_t cmd, uint8_t ep, uint8_t* data,
    uint16_t len, String* error)
{
#ifdef DEBUGGER_DEVICE_METH
    Debugger d(DebugAll,"BrfLibUsbDevice::vendorCommand()",
	" (0x%x,0x%x,%p,%u) [%p]",cmd,ep,data,len,m_owner);
#endif
    String e;
    unsigned int status = lusbCtrlTransfer(LUSB_CTRLTRANS_IFACE_VENDOR | ep,
	cmd,0,0,data,len,&e);
    if (!status)
	return 0;
    e.printf(1024,"Vendor command 0x%x endpoint=0x%x failed - %s",cmd,ep,e.c_str());
    return showError(status,e,0,error);
}

// Access the bladeRF board in order to transmit data
unsigned int BrfLibUsbDevice::accessPeripheral(uint8_t dev, bool tx, uint8_t addr,
    uint8_t* data, String* error, uint8_t len, const char* loc)
{
    if (dev >= UartDevCount)
	return RadioInterface::Failure;
    const BrfPeripheral& uartDev = s_uartDev[dev];
#ifdef DEBUGGER_DEVICE_METH
    String tmp;
    if (tx)
	tmp.hexify(data,len,' ');
    Debugger debug(DebugInfo,"BrfLibUsbDevice::accessPeripheral()",
	" dev=%s dir=%s addr=0x%x len=%u bits=%s [%p]",
	uartDev.c_str(),brfDir(tx),addr,len,tmp.safe(),m_owner);
#endif
    String e;
    unsigned int status = 0;
    uint8_t a = addr;
    uint8_t* d = data;
    uint8_t n = len;
    uint8_t maskDirDev = (uint8_t)((tx ? 0x40 : 0x80) | uartDev.devId());
    uint8_t buf[16] = {(uint8_t)'N'};
#define BRF_ACCESS_PERIPHERAL(nItems) \
{ \
    buf[1] = (uint8_t)(maskDirDev | nItems); \
    uint8_t* b = &buf[2]; \
    ::memset(b,0,sizeof(buf) - 2); \
    for (uint8_t i = 0; i < nItems; i++) { \
	*b++ = a + i; \
	if (tx) \
	    *b = d[i]; \
	b++; \
    } \
    status = syncTransfer(EpSendCtrl,buf,sizeof(buf),&e); \
    if (!status) \
	status = syncTransfer(EpReadCtrl,buf,sizeof(buf),&e); \
    if (status == 0 && !tx) { \
	b = &buf[3]; \
	for (uint8_t i = 0; i < nItems; i++, b += 2) \
	    d[i] = *b; \
    } \
}
    if (n > 7) {
	n = len % 7;
	for (uint8_t full = len / 7; !status && full; full--, a += 7, d += 7)
	    BRF_ACCESS_PERIPHERAL(7);
    }
    if (n && !status)
	BRF_ACCESS_PERIPHERAL(n);
#undef BRF_ACCESS_PERIPHERAL
    if (status) {
	e.printf(1024,"%s%s%s %s failed addr=0x%x len=%d - %s",
	    TelEngine::c_safe(loc),(loc ? " - " : ""),uartDev.c_str(),
	    tx ? "write" : "read",addr,len,e.c_str());
	return showError(status,e,0,error);
    }
    if (!(uartDev.trackDir(tx) && m_owner->debugAt(DebugAll)))
	return 0;
    String s;
    if (!uartDev.haveTrackAddr())
	Debug(m_owner,DebugAll,"%s %s addr=0x%x len=%u '%s' [%p]",
	    uartDev.c_str(),brfDir(tx),addr,len,s.hexify(data,len,' ').c_str(),m_owner);
    else if (uartDev.isTrackRange(addr,len)) {
	uint8_t a = addr;
	for (unsigned int i = 0; i < len && a < 256; i++, a++)
	    if (uartDev.isTrackAddr(a)) {
		String tmp;
		s.append(tmp.printf("(0x%x=0x%x)",a,data[i])," ");
	    }
	if (s)
	    Debug(m_owner,DebugAll,"%s %s %s [%p]",
		uartDev.c_str(),brfDir(tx),s.c_str(),m_owner);
    }
    return 0;
}

unsigned int BrfLibUsbDevice::internalSetDcOffset(bool tx, bool i, int16_t value,
    String* error)
{
    int& old = i ? getIO(tx).dcOffsetI : getIO(tx).dcOffsetQ;
    if (old == value)
	return 0;
    uint8_t addr = lmsCorrIQAddr(tx,i);
    String e;
    uint8_t data = 0;
    unsigned int status = 0;
    while (true) {
	if (tx) {
	    if (value < BRF_TX_DC_OFFSET_MIN || value > BRF_TX_DC_OFFSET_MAX) {
		status = setUnkValue(e,0,"value");
		break;
	    }
	}
	else if (value < -BRF_RX_DC_OFFSET_MAX || value > BRF_RX_DC_OFFSET_MAX) {
	    status = setUnkValue(e,0,"value");
	    break;
	}
	BrfDevTmpAltSet tmpAltSet(this,status,&e,"DC offset set");
	if (status)
	    break;
	status = lmsRead(addr,data,&e);
	if (status)
	    break;
	if (tx)
	    // MSB bit is the sign (1: positive)
	    data = 128 + value;
	else {
	    // MSB bit has nothing to do with RX DC offset
	    // Bit 6 is the sign bit (1: negative)
	    uint8_t b7 = (uint8_t)(data & 0x80);
	    if (value >= 0)
		data = (uint8_t)((value >= 64) ? 0x3f : (value & 0x3f));
	    else {
		data = (uint8_t)((value <= -64) ? 0x3f : ((-value) & 0x3f));
		data |= 0x40;
	    }
	    data |= b7;
	}
	status = lmsWrite(addr,data,&e);
	break;
    }
    if (status == 0) {
	int tmp = decodeDCOffs(tx,data);
	if (tmp != old) {
	    old = tmp;
	    if (getIO(tx).showDcOffsChange == 0)
		Debug(m_owner,DebugAll,"%s DC offset %c set to %d (from %d) reg=0x%x [%p]",
		    brfDir(tx),brfIQ(i),old,value,data,m_owner);
	}
	return 0;
    }
    e.printf(1024,"%s DC offset %c set to %d failed - %s",
	brfDir(tx),brfIQ(i),value,e.c_str());
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::internalGetDcOffset(bool tx, bool i, int16_t* value,
    String* error)
{
    uint8_t addr = lmsCorrIQAddr(tx,i);
    String e;
    uint8_t data = 0;
    unsigned int status = 0;
    BrfDevTmpAltSet tmpAltSet(this,status,&e,"DC offset get");
    if (!status)
	status = lmsRead(addr,data,&e);
    if (!status) {
	int& old = i ? getIO(tx).dcOffsetI : getIO(tx).dcOffsetQ;
	old = decodeDCOffs(tx,data);
	if (value)
	    *value = old;
	XDebug(m_owner,DebugAll,"Got %s DC offset %c %d (0x%x) [%p]",
	    brfDir(tx),brfIQ(i),old,data,m_owner);
	return 0;
    }
    e.printf(1024,"%s DC offset %c get failed - %s",brfDir(tx),brfIQ(i),e.c_str());
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::enableTimestamps(bool on, String* error)
{
    String e;
    unsigned int status = 0;
    while (true) {
	uint32_t val = 0;
	BRF_FUNC_CALL_BREAK(gpioRead(0,val,4,&e));
	if (on)
	    val |= 0x10000;
	else
	    val &= ~0x10000;
	BRF_FUNC_CALL_BREAK(gpioWrite(0,val,4,&e));
	if (on) {
	    BRF_FUNC_CALL_BREAK(gpioRead(0,val,4,&e));
	    if ((val & 0x10000) == 0) {
		status = setError(RadioInterface::Failure,&e,"not enabled");
		break;
	    }
	}
	Debug(m_owner,DebugAll,"%s timestamps [%p]",enabledStr(on),m_owner);
	return 0;
    }
    e.printf(1024,"Failed to %s timestamps - %s",enableStr(on),e.c_str());
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::updateStatus(String* error)
{
    unsigned int status = 0;
    // Frequency
    BRF_FUNC_CALL(internalGetFrequency(true));
    BRF_FUNC_CALL(internalGetFrequency(false));
    // Update VGA data
    BRF_FUNC_CALL(internalGetTxVga(0,true,error));
    BRF_FUNC_CALL(internalGetTxVga(0,false,error));
    BRF_FUNC_CALL(internalGetRxVga(0,true,error));
    BRF_FUNC_CALL(internalGetRxVga(0,false,error));
    // LPF
    internalGetLpf(true,0,error);
    internalGetLpf(false,0,error);
    // Update DC offsets
    BRF_FUNC_CALL(internalGetDcOffset(true,true,0,error));
    BRF_FUNC_CALL(internalGetDcOffset(true,false,0,error))
    BRF_FUNC_CALL(internalGetDcOffset(false,true,0,error));
    BRF_FUNC_CALL(internalGetDcOffset(false,false,0,error));
    // Update FPGA correction
    BRF_FUNC_CALL(internalGetFpgaCorr(true,CorrFpgaGain,0,error));
    BRF_FUNC_CALL(internalGetFpgaCorr(true,CorrFpgaPhase,0,error));
    BRF_FUNC_CALL(internalGetFpgaCorr(false,CorrFpgaGain,0,error));
    BRF_FUNC_CALL(internalGetFpgaCorr(false,CorrFpgaPhase,0,error));
    return status;
}

unsigned int BrfLibUsbDevice::paSelect(int pa, String* error)
{
    String e;
    unsigned int status = 0;
    while (true) {
	uint8_t data = 0;
	BRF_FUNC_CALL_BREAK(lmsRead(0x44,data,&e));
	// PA_EN bits 4-3: PA 1/2/none - bit 2: AUXPA (0: powered up, 1: powered down)
	bool changed = false;
	switch (pa) {
	    case LmsPaAux:
		changed = (data & 0x04) != 0;
		status = lmsWrite(0x44,data & ~0x04,&e);
		break;
	    case LmsPa1:
		changed = (data & 0x18) != 0x08;
		status = lmsWrite(0x44,(data & ~0x18) | 0x08,&e);
		break;
	    case LmsPa2:
		changed = (data & 0x18) != 0x10;
		status = lmsWrite(0x44,(data & ~0x18) | 0x10,&e);
		break;
	    case LmsPaNone:
		changed = (data & 0x18) != 0;
		status = lmsWrite(0x44,data & ~0x18,&e);
		break;
	    default:
		Debug(m_owner,DebugFail,"Unhandled PA %d [%p]",pa,m_owner);
		status = setUnkValue(e);
	}
	if (status)
	    break;
	int level = changed ? DebugInfo : DebugAll;
	if (pa != LmsPaNone)
	    Debug(m_owner,level,"%s enabled [%p]",lookup(pa,s_pa),m_owner);
	else
	    Debug(m_owner,level,"PAs disabled [%p]",m_owner);
	return 0;
    }
    if (pa != LmsPaNone)
	e.printf(1024,"Failed to enable PA %s - %s",lookup(pa,s_pa),e.c_str());
    else
	e.printf(1024,"Failed to disable PAs - %s",e.c_str());
    return showError(status,e,0,error);
}

// Clamp frequency value
void BrfLibUsbDevice::clampFrequency(uint32_t& val, bool tx, const char* loc)
{
    if (val >= BRF_FREQUENCY_MIN && val <= BRF_FREQUENCY_MAX)
	return;
    uint32_t c = val < BRF_FREQUENCY_MIN ? BRF_FREQUENCY_MIN : BRF_FREQUENCY_MAX;
    Debug(m_owner,DebugNote,"%s: clamping %s frequency %u to %u [%p]",
	loc,brfDir(tx),val,c,m_owner);
    val = c;
}

int BrfLibUsbDevice::clampInt(int val, int minVal, int maxVal, const char* what,
    int level)
{
    if (val >= minVal && val <= maxVal)
	return val;
    int c = val < minVal ? minVal : maxVal;
    if (what)
	Debug(m_owner,level,"Clamping %s %d -> %d [%p]",what,val,c,m_owner);
    return c;
}

unsigned int BrfLibUsbDevice::openDevice(bool claim, String* error)
{
    closeDevice();
    m_dev = 0;
    unsigned int status = updateDeviceList(error);
    if (status)
	return status;
    unsigned int failedDesc = 0;
    libusb_device_descriptor desc;
    for (unsigned int i = 0; !m_dev && i < m_listCount; i++) {
	if (::libusb_get_device_descriptor(m_list[i],&desc)) {
	    failedDesc++;
	    continue;
	}
	if (desc.idVendor == 7504 && desc.idProduct == 24678)
	    m_dev = m_list[i];
    }
    if (!m_dev) {
	String e = "No device found";
	if (failedDesc)
	    e << " (failed to retrieve " << failedDesc <<
		" device descriptor(s))";
	return setError(RadioInterface::NotInitialized,error,e);
    }
    m_devBus = ::libusb_get_bus_number(m_dev);
    m_devAddr = ::libusb_get_device_address(m_dev);
    m_devSpeed = ::libusb_get_device_speed(m_dev);
    DDebug(m_owner,DebugAll,"Opening device bus=%u addr=%u [%p]",bus(),addr(),m_owner);
    status = lusbCheckSuccess(::libusb_open(m_dev,&m_devHandle),
	error,"Failed to open the libusb device ");
    if (status)
	return status;
    getDevStrDesc(m_devSerial,desc.iSerialNumber,"serial number");
    getDevStrDesc(m_devFwVerStr,4,"firmware version");
    if (claim)
	status = lusbCheckSuccess(::libusb_claim_interface(m_devHandle,0),
	    error,"Failed to claim the interface ");
    return status;
}

void BrfLibUsbDevice::closeDevice()
{
    if (!m_devHandle)
	return;
    m_closingDevice = true;
    internalPowerOn(false,false,false);
    m_closingDevice = false;
    ::libusb_close(m_devHandle);
    m_devHandle = 0;
    m_devBus = -1;
    m_devAddr = -1;
    m_devSpeed = LIBUSB_SPEED_HIGH;
    m_devSerial.clear();
    m_devFwVerStr.clear();
    m_devFpgaVerStr.clear();
    m_devFpgaFile.clear();
    m_devFpgaMD5.clear();
    m_txIO.dataDumpFile.terminate(owner());
    m_txIO.upDumpFile.terminate(owner());
    m_rxIO.dataDumpFile.terminate(owner());
    m_rxIO.upDumpFile.terminate(owner());
    Debug(m_owner,DebugAll,"Device closed [%p]",m_owner);
}

void BrfLibUsbDevice::getDevStrDesc(String& data, uint8_t index, const char* what)
{
    unsigned char buf[256];
    int len = ::libusb_get_string_descriptor_ascii(m_devHandle,index,buf,sizeof(buf) - 1);
    if (len >= 0) {
	buf[len] = 0;
	data = (const char*)buf;
	return;
    }
    data.clear();
    String tmp;
    Debug(m_owner,DebugInfo,"Failed to retrieve device %s %s [%p]",
	what,appendLusbError(tmp,len).c_str(),m_owner);
}

// Read pages from device using control transfer
unsigned int BrfLibUsbDevice::ctrlTransferReadPage(uint8_t request, DataBlock& buf,
    String* error)
{
    if (!m_ctrlTransferPage)
	return setError(RadioInterface::Failure,error,"Invalid CTRL transfer page size");
    buf.resize(BRF_FLASH_PAGE_SIZE);
    uint8_t* b = buf.data(0);
    // Retrieve data from the firmware page buffer
    for (unsigned int offs = 0; offs < buf.length(); offs += m_ctrlTransferPage) {
        unsigned int status = lusbCtrlTransfer(LUSB_CTRLTRANS_DEV_VENDOR_IN,
	    request,0,offs,b + offs,m_ctrlTransferPage,error);
	if (status)
	    return status;
    }
    return 0;
}

// Read calibration cache page from device
unsigned int BrfLibUsbDevice::readCalCache(String* error)
{
    unsigned int status = 0;
    BrfDevTmpAltSet tmpAltSet(this,BRF_ALTSET_SPI_FLASH,status,error,
	"read calibration cache");
    m_calCache.clear();
    if (status == 0)
	return ctrlTransferReadPage(BRF_USB_CMD_READ_CAL_CACHE,m_calCache,error);
    return status;
}

static uint16_t crc16(uint8_t* buf, unsigned int len)
{
    uint16_t crc = 0;
    for (uint8_t* last = buf + len; buf < last; buf++) {
        crc ^= (uint16_t)(((uint16_t)*buf) << 8);
        for (int i = 0; i < 8; i++) {
            if ((crc & 0x8000) != 0)
		crc = (uint16_t)(crc << 1) ^ 0x1021;
            else
		crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

// Retrieve a filed from a buffer of elements
// Buffer format: 1 byte length + data + 2 bytes CRC16
const char* BrfLibUsbDevice::getBufField(String& value, const char* field)
{
    if (TelEngine::null(field))
	return "empty-field";
    uint8_t* b = m_calCache.data(0);
    unsigned int len = m_calCache.length();
    if (!len)
	return "calibration-cache-not-loaded";
    for (uint8_t dataLen = 0; len; len -= dataLen, b += dataLen) {
        dataLen = *b;
	// No more data ?
        if (dataLen == 0xff)
	    return "unexpected end of data";
	// Do we have enough data ?
	if (len < (dataLen + 2u))
	    return "wrong data - invalid field length";
        uint16_t crc = le32toh(*(uint16_t*)(b + dataLen + 1));
        uint16_t crcCheck = crc16(b,dataLen + 1);
        if (crcCheck != crc)
	    return "wrong data - invalid CRC";
	unsigned int fLen = 0;
	const char* s = (const char*)(b + 1);
	const char* f = field;
	for (; fLen <= dataLen && *f && *s == *f; s++, f++)
	    fLen++;
	if (!*f) {
	    value.assign(s,dataLen - fLen);
	    return 0;
	}
	dataLen += 3;
    }
    return "not found";
}

// Read calibration cache field
unsigned int BrfLibUsbDevice::getCalField(String& value, const String& name,
    const char* desc, String* error)
{
    String e = getBufField(value,name);
    if (!e)
	return 0;
    e.printf(2048,"Failed to retrieve calibration cache field '%s' (%s) - %s",
	name.c_str(),desc,e.c_str());
    return showError(RadioInterface::Failure,e,0,error);
}

String& BrfLibUsbDevice::dumpCalCache(String& dest)
{
    String e;
    dest.append("(LEN|VALUE|CRC)"," ");
    uint8_t* b = m_calCache.data(0);
    unsigned int len = m_calCache.length();
    for (uint8_t dataLen = 0; len; len -= dataLen, b += dataLen) {
        dataLen = *b;
	// No more data ?
        if (dataLen == 0xff) {
	    len = 0;
	    break;
	}
	dest << " " << dataLen;
	// Do we have enough data ?
	if (len < (dataLen + 2u)) {
	    dest << "-|-";
	    break;
	}
	String crcS;
	crcS.hexify(b + dataLen + 1,2);
        uint16_t crc = le32toh(*(uint16_t*)(b + dataLen + 1));
        uint16_t crcCheck = crc16(b,dataLen + 1);
        if (crcCheck != crc)
	    crcS << "(invalid)";
	dest << "|" << String((const char*)(b + 1),dataLen) << "|" << crcS;
	dataLen += 3;
    }
    if (len)
	dest << " garbage=" << len;
    return dest;
}

// Update speed related data
unsigned int BrfLibUsbDevice::updateSpeed(const NamedList& params, String* error)
{
    if (speed() == LIBUSB_SPEED_SUPER || speed() == LIBUSB_SPEED_HIGH) {
	unsigned int brfBufSamples = 508;
	unsigned int nBuffers = 4;
	if (speed() == LIBUSB_SPEED_HIGH) {
	    brfBufSamples = 252;
	    nBuffers = 8;
	}
	m_txIO.resetSamplesBuffer(brfBufSamples,16,nBuffers);
	if (m_minBufsSend > nBuffers)
	    m_minBufsSend = nBuffers;
	m_rxIO.resetSamplesBuffer(brfBufSamples,16,nBuffers);
	m_minBufsSend = clampIntParam(params,"tx_min_buffers",nBuffers,1,nBuffers);
	if (speed() == LIBUSB_SPEED_SUPER) {
	    m_radioCaps.rxLatency = clampIntParam(params,"rx_latency_super",4000,0,150000);
	    m_radioCaps.txLatency = clampIntParam(params,"tx_latency_super",10000,0,150000);
	    m_ctrlTransferPage = BRF_FLASH_PAGE_SIZE;
	}
	else {
	    m_radioCaps.rxLatency = clampIntParam(params,"rx_latency_high",7000,0,150000);
	    m_radioCaps.txLatency = clampIntParam(params,"tx_latency_high",20000,0,150000);
	    m_ctrlTransferPage = 64;
	}
	return 0;
    }
    m_minBufsSend = 1;
    m_radioCaps.rxLatency = 0;
    m_radioCaps.txLatency = 0;
    m_ctrlTransferPage = 0;
    String e;
    e << "Unsupported USB speed " << m_devSpeed;
    return setError(RadioInterface::OutOfRange,error,e);
}

// Check timestamps after reading from device
void BrfLibUsbDevice::ioBufCheckTs(bool tx, unsigned int nBufs)
{
    String invalid;
    BrfDevIO& io = getIO(tx);
    if (!nBufs)
	nBufs = io.buffers;
    unsigned int i = 0;
    if (!io.lastTs) {
	io.lastTs = io.bufTs(0);
	i = 1;
    }
    for (; i < nBufs; i++) {
	uint64_t crt = io.bufTs(i);
	if ((io.lastTs + io.bufSamples) != crt) {
	    if (!invalid)
		invalid << ": invalid timestamps (buf=ts/delta)";
	    invalid << " " << (i + 1) << "=" << crt << "/" << (int64_t)(crt - io.lastTs);
	}
	io.lastTs = crt;
    }
    if (invalid)
	Debug(m_owner,invalid ? DebugMild : DebugAll,"%s buf_samples=%u: %u buffers%s [%p]",
	    brfDir(tx),io.bufSamples,nBufs,invalid.safe(),m_owner);
}

void BrfLibUsbDevice::updateAlterData(const NamedList& params)
{
    Lock lck(m_dbgMutex);
    m_rxAlterDataParams = params;
    m_rxAlterDataParams.assign("-");
    m_rxAlterData = true;
}

void BrfLibUsbDevice::rxAlterData(bool first)
{
    while (m_rxAlterDataParams.c_str()) {
	Lock lck(m_dbgMutex);
	if (!m_rxAlterDataParams.c_str())
	    break;
	if (m_rxAlterDataParams.getBoolValue(YSTRING("rx_alter_increment"))) {
	    if (!m_rxAlterIncrement)
		m_rxAlterIncrement = 1;
	}
	else
	    m_rxAlterIncrement = 0;
	m_rxAlterData = (m_rxAlterIncrement != 0);
	const String& tsJumpPattern = m_rxAlterDataParams[YSTRING("rx_alter_ts_jump_pattern")];
	if (tsJumpPattern != m_rxAlterTsJumpPatern) {
	    m_rxAlterTsJumpPatern = tsJumpPattern;
	    ObjList* list = m_rxAlterTsJumpPatern.split(',');
	    m_rxAlterTsJump.overAlloc(10 * sizeof(int64_t));
	    m_rxAlterTsJump.resize(list->count() * sizeof(int64_t));
	    int64_t* d = (int64_t*)m_rxAlterTsJump.data();
	    bool ok = false;
	    unsigned int index = 0;
	    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
		const String* s = static_cast<String*>(o->get());
		if (!s->startsWith("rep_")) {
		    d[index] = s->toInt64();
		    if (d[index])
			ok = true;
		    index++;
		    continue;
		}
		int64_t lastVal = index ? d[index - 1] : 0;
		unsigned int repeat = s->substr(4).toInteger(0,0,0);
		if (repeat < 2) {
		    d[index++] = lastVal;
		    continue;
		}
		DataBlock tmp = m_rxAlterTsJump;
		m_rxAlterTsJump.resize(tmp.length() + (sizeof(int64_t) * (repeat - 1)));
		d = (int64_t*)m_rxAlterTsJump.data();
		::memcpy(d,tmp.data(),index * sizeof(int64_t));
		while (repeat--)
		    d[index++] = lastVal;
	    }
	    TelEngine::destruct(list);
	    if (!ok)
		m_rxAlterTsJump.clear();
	    m_rxAlterTsJumpPos = 0;
	}
	m_rxAlterTsJumpSingle = m_rxAlterDataParams.getBoolValue(
	    YSTRING("rx_alter_ts_jump_single"),true);
	if (m_rxAlterTsJump.length())
	    m_rxAlterData = true;
	m_rxAlterDataParams.assign("");
	m_rxAlterDataParams.clear();
	if (!m_rxAlterData)
	    return;
    }
    BrfDevIO& io = m_rxIO;
    if (first) {
	// Change timestamps
	if (m_rxAlterTsJump.length()) {
	    int64_t* d = (int64_t*)m_rxAlterTsJump.data();
	    unsigned int len = m_rxAlterTsJump.length() / sizeof(int64_t);
	    for (unsigned int i = 0; i < io.buffers; i++) {
		if (d[m_rxAlterTsJumpPos])
		    io.setBufTs(i,io.bufTs(i) + d[m_rxAlterTsJumpPos]);
		m_rxAlterTsJumpPos++;
		if (m_rxAlterTsJumpPos >= len) {
		    m_rxAlterTsJumpPos = 0;
		    if (m_rxAlterTsJumpSingle) {
			m_rxAlterTsJump.clear();
			// Signal update on next call
			m_rxAlterData = true;
			break;
		    }
		}
	    }
	}
    }
    else {
	// Change radio data
	if (m_rxAlterIncrement && !first) {
	    for (unsigned int i = 0; i < io.buffers; i++) {
		int16_t* p = io.samples(i);
		int16_t* last = io.samplesEOF(i);
		while (p != last) {
		    *p++ = m_rxAlterIncrement;
		    *p++ = -m_rxAlterIncrement;
		    m_rxAlterIncrement++;
		    if (m_rxAlterIncrement >= 2048)
			m_rxAlterIncrement = 1;
		}
	    }
	}
    }
#if 0
    if (!first)
	printIOBuffer(false,"alter");
#endif
}

unsigned int BrfLibUsbDevice::calLPFBandwidth(const BrfCalData& bak, uint8_t subMod,
			     uint8_t dcCnt, uint8_t& dcReg, String& e)
{
#ifdef DEBUG_DEVICE_AUTOCAL
    Debugger d(DebugAll,"CAL PROC"," submod=%u dcCnt=0x%x [%p]",subMod,dcCnt,m_owner);
#endif
    uint8_t data = 0;
    unsigned int status = 0;
    // Programing and Calibration Guide 4.5
    // PLL Reference Clock Frequency == 40MHz?
    if (s_freqRefClock != 40000000) {
	// Power down TxVGA2 -- (is optional)
	BRF_FUNC_CALL_RET(lmsSet(0x44,0x0c,0x0c,&e));
	// Enable TxPPL Register 0x14 set bit 4 to 1
	BRF_FUNC_CALL_RET(lmsSet(0x14,0x08,&e));

	// Produce 320 MHz
	// TODO FIXME The values are hard codded for 38.4 MHz as reference clock
	BRF_FUNC_CALL_RET(lmsWrite(0x10,0x42,&e));
	BRF_FUNC_CALL_RET(lmsWrite(0x11,0xaa,&e));
	BRF_FUNC_CALL_RET(lmsWrite(0x12,0xaa,&e));
	BRF_FUNC_CALL_RET(lmsWrite(0x13,0xaa,&e));
	// TopSPI:CLKSEL_LPFCAL = 0
	BRF_FUNC_CALL_RET(lmsReset(0x06,0x08,&e));
	// Power up LPF tuning clock generation block TOPSPI:PD_CLKLPFCAL = 0
	BRF_FUNC_CALL_RET(lmsReset(0x06,0x04,&e));
    }

    BRF_FUNC_CALL_RET(lmsRead(0x54,data,&e));
    BRF_FUNC_CALL_RET(lmsSet(0x07,(data >> 2) & 0x0f,0x0f,&e));
    // TopSPI:En_CAL_LPFCAL=1(enable)
    BRF_FUNC_CALL_RET(lmsSet(0x07,0x80,&e));
    // TopSPI:RST_CAL_LPFCAL=1 (RST active)
    BRF_FUNC_CALL_RET(lmsSet(0x06,0x01,&e));
    // Reset signal used at the beginning of calibration cycle.
    // Reset signal needs to be longer than 100ns
    Thread::msleep(1);
    // TopSPI:RST_CAL_LPFCAL=0 (RST inactive)
    BRF_FUNC_CALL_RET(lmsReset(0x06,0x01,&e));
    // RCCAL = TopSPI::RCCAL_LPFCAL
    BRF_FUNC_CALL_RET(lmsRead(0x01,data,&e));
    dcReg = data >> 5;
    BRF_FUNC_CALL_RET(lmsSet(0x56,dcReg << 4,0x70,&e));
    DDebug(m_owner,DebugAll,"%s calibrated submodule %u -> %u [%p]",
	   bak.modName(),subMod,dcReg,m_owner);
    return 0;
}

unsigned int BrfLibUsbDevice::calibrateAuto(String* error)
{
#ifdef DEBUG_DEVICE_AUTOCAL
    Debugger debug(DebugAll,"AUTOCALIBRATION"," '%s' [%p]",m_owner->debugName(),m_owner);
#endif
    Debug(m_owner,DebugInfo,"Autocalibration starting ... [%p]",m_owner);
    String e;
    unsigned int status = internalSetDcCorr(0,0,0,0,&e);
    int8_t calVal[BRF_CALIBRATE_LAST][BRF_CALIBRATE_MAX_SUBMODULES];
    ::memset(calVal,-1,sizeof(calVal));
    BrfDuration duration;
    for (int m = BRF_CALIBRATE_FIRST; !status && m <= BRF_CALIBRATE_LAST; m++) {
	BrfCalData bak(m);
#ifdef DEBUG_DEVICE_AUTOCAL
	Debugger d(DebugAll,"AUTOCALIBRATION"," module: %s [%p]",bak.modName(),m_owner);
#endif
	if ((status = cancelled(&e)) != 0)
	    break;
	Debug(m_owner,DebugAll,"Calibrating %s [%p]",bak.modName(),m_owner);
	if ((status = calBackupRestore(bak,true,&e)) != 0)
	    break;
	status = calInitFinal(bak,true,&e);
	for (uint8_t subMod = 0; !status && subMod < bak.desc->subModules; subMod++) {
	    status = dcCalProcPrepare(bak,subMod,e);
	    if (!status) {
		uint8_t dcReg = 0;
		if (m == BRF_CALIBRATE_LPF_BANDWIDTH)
		   status = calLPFBandwidth(bak,subMod,31,dcReg,e);
		else
		    status = dcCalProc(bak,subMod,31,dcReg,e);
		if (!status) {
		    calVal[m][subMod] = dcReg;
		    status = dcCalProcPost(bak,subMod,dcReg,e);
		}
	    }
	    if (status)
		e.printf(2048,"Failed to calibrate module %s - %s",
		    bak.modName(),e.c_str());
	}
	unsigned int tmp = calInitFinal(bak,false,status ? 0 : &e);
	if (!status)
	    status = tmp;
	tmp = calBackupRestore(bak,false,status ? 0 : &e);
	if (!status)
	    status = tmp;
	if (status)
	    break;
	Debug(m_owner,DebugAll,"Calibrated %s [%p]",bak.modName(),m_owner);
    }
    duration.stop();
    if (status) {
	e = "Autocalibration failed - " + e;
	return showError(status,e,0,error);
    }
    String s;
#ifdef DEBUG
    for (int m = BRF_CALIBRATE_FIRST; m <= BRF_CALIBRATE_LAST; m++) {
	if (m == BRF_CALIBRATE_LPF_BANDWIDTH)
	    continue;
	const BrfCalDesc& d = s_calModuleDesc[m];
	String t;
	if (d.subModules > 1)
	    for (uint8_t sm = 0; sm < d.subModules; sm++) {
		t.printf("\r\n%s - %s: %d",calModName(m),d.subModName[sm],calVal[m][sm]);
		s << t;
	    }
	else
	    s << t.printf("\r\n%s: %d",calModName(m),calVal[m][0]);
    }
#endif
    Debug(m_owner,DebugInfo,"Autocalibration finished in %s [%p]%s",
	duration.secStr(),m_owner,encloseDashes(s));
    return 0;
}

unsigned int BrfLibUsbDevice::calBackupRestore(BrfCalData& bak, bool backup,
    String* error)
{
    const char* what = backup ? "backup" : "restore";
#ifdef DEBUG_DEVICE_AUTOCAL
    Debugger d(DebugAll,"CAL BACKUP/RESTORE"," %s [%p]",what,m_owner);
#endif
    unsigned int status = 0;
    String e;
    while (true) {
	// We will backup the data in the CLK_EN register in case something goes wrong
	status = lms(backup,0x09,bak.clkEn,&e);
	if (status)
	    break;
	if (bak.module == BRF_CALIBRATE_RX_LPF || bak.module == BRF_CALIBRATE_RX_VGA2) {
//	    BRF_FUNC_CALL_BREAK(lms(backup,0x71,bak.inputMixer,&e));
//	    BRF_FUNC_CALL_BREAK(lms(backup,0x7c,bak.loOpt,&e));
	    BRF_FUNC_CALL_BREAK(lnaGain(backup,bak.lnaGain,&e));
	    BRF_FUNC_CALL_BREAK(internalRxVga(backup,bak.rxVga1,true,&e));
	    if (bak.module == BRF_CALIBRATE_RX_VGA2) {
		BRF_FUNC_CALL_BREAK(lms(backup,0x68,bak.rxVga2GainAB,&e));
	    }
	    status = internalRxVga(backup,bak.rxVga2,false,&e);
	    break;
	}
	if (bak.module == BRF_CALIBRATE_TX_LPF ||
	    bak.module == BRF_CALIBRATE_LPF_TUNING) {
	    DDebug(m_owner,DebugAll,"calBackupRestore: nothing to do for %s [%p]",
		bak.modName(),this);
	    break;
	}
	if (bak.module == BRF_CALIBRATE_LPF_BANDWIDTH) {
	    BRF_FUNC_CALL_BREAK(lms(backup,0x06,bak.clkLPFCAL,&e));
	    BRF_FUNC_CALL_BREAK(lms(backup,0x07,bak.enLPFCAL,&e));
	    BRF_FUNC_CALL_BREAK(lms(backup,0x14,bak.txPPL,&e));
	    BRF_FUNC_CALL_BREAK(lms(backup,0x44,bak.txVGA2PwAmp,&e));

	    BRF_FUNC_CALL_BREAK(lms(backup,0x10,bak.nInt,&e));
	    BRF_FUNC_CALL_BREAK(lms(backup,0x11,bak.nFrac1,&e));
	    BRF_FUNC_CALL_BREAK(lms(backup,0x12,bak.nFrac2,&e));
	    BRF_FUNC_CALL_BREAK(lms(backup,0x13,bak.nFrac3,&e));
	    break;
	}
	status = setUnhandled(e,bak.module,"module");
	break;
    }
    if (status == 0)
	return 0;
    e.printf(2048,"Failed to %s calibration data for module %s - %s",
	what,bak.modName(),e.c_str());
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::calInitFinal(BrfCalData& bak, bool init, String* error)
{
    const char* what = init ? "initialize" : "finalize";
#ifdef DEBUG_DEVICE_AUTOCAL
    String lmsDump;
#if 0
    if (init) {
	internalDumpPeripheral(UartDevLMS,0,128,&lmsDump,16);
	encloseDashes(lmsDump);
    }
#endif
    Debugger d(DebugAll,"CAL INIT/FINAL"," %s [%p]%s",what,m_owner,lmsDump.safe());
#endif
    String e;
    unsigned int status = 0;
    while (true) {
	// Enable the appropriate CLK_EN bit
	if (init)
	    status = lmsWrite(0x09,bak.clkEn | bak.desc->clkEnMask,&e);
	if (status)
	    break;
	if (bak.module == BRF_CALIBRATE_LPF_TUNING || 
		bak.module == BRF_CALIBRATE_LPF_BANDWIDTH) {
	    DDebug(m_owner,DebugAll,"calInitFinal(%s): nothing to do for %s [%p]",
		what,bak.modName(),this);
	    break;
	}
	// Enable special conditions
	if (bak.module == BRF_CALIBRATE_RX_LPF || bak.module == BRF_CALIBRATE_RX_VGA2) {
	    if (bak.module == BRF_CALIBRATE_RX_VGA2) {
		// Set RXVGA2 DECODE on init/finalize
		if (!init)
		    BRF_FUNC_CALL_BREAK(setRxVga2Decode(true,&e));
		// TODO: Check it BRF_FUNC_CALL_BREAK(lmsChangeMask(0x63,0xc0,!init,&e));
	    }
	    else {
		// FAQ 5.26 (rev 1.0r13) DC comparators should be
		//  powered up before calibration and then powered down after it
		BRF_FUNC_CALL_BREAK(lmsChangeMask(0x5f,0x80,!init,&e));
		if (init) {
		    BRF_FUNC_CALL_BREAK(lmsSet(0x56,0x04,&e));
		}
		else {
		    BRF_FUNC_CALL_BREAK(lmsReset(0x56,0x04,&e));
		}
	    }
	    // Done for finalize
	    if (!init)
		break;
#if 0
	    // TODO: Check it !!! It is really necessary ?
	    // Connect LNA to the external pads and internally terminate
	    status = lmsWrite2(0x71,bak.inputMixer & 0x7f,0x7c,bak.loOpt | 0x04,&e);
	    if (status)
		break;
#endif
	    // FAQ 4.2 (rev 1.0r13): Attempt to calibrate RX at max gain
	    BRF_FUNC_CALL_BREAK(lnaGainSet(LnaGainMax,&e));
	    BRF_FUNC_CALL_BREAK(internalSetRxVga(BRF_RXVGA1_GAIN_MAX,true,&e));
	    BRF_FUNC_CALL_BREAK(internalSetRxVga(BRF_RXVGA2_GAIN_MAX,false,&e));
	    if (bak.module == BRF_CALIBRATE_RX_VGA2)
		status = setRxVga2Decode(true,&e);
	    break;
	}
	if (bak.module == BRF_CALIBRATE_TX_LPF) {
	    // TX_DACBUF_PD (TX data DAC buffers)
	    // LMS6002 Quick starter manual, Section 6.1
	    // No signal should be applied to DACs (power down: bit is 1)
	    // PD_DCOCMP_LPF (DC offset comparator of DC offset cancellation)
	    // It must be powered down (bit set to 1) when calibrating
	    if (init) {
		//BRF_FUNC_CALL_BREAK(lmsSet(0x36,0x80,&e));
		BRF_FUNC_CALL_BREAK(lmsSet(0x36,0x04,&e));
		BRF_FUNC_CALL_BREAK(lmsReset(0x3f,0x80,&e));
	    }
	    else {
		//BRF_FUNC_CALL_BREAK(lmsReset(0x36,0x80,&e));
		BRF_FUNC_CALL_BREAK(lmsReset(0x36,0x04,&e));
		BRF_FUNC_CALL_BREAK(lmsSet(0x3f,0x80,&e));
	    }
	    break;
	}
	status = setUnhandled(e,bak.module,"module");
	break;
    }
    if (status == 0)
	return 0;
    e.printf(2048,"Failed to %s calibration for module %s - %s",
	what,bak.modName(),e.c_str());
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::dcCalProcPrepare(const BrfCalData& bak, uint8_t subMod,
    String& e)
{
#ifdef DEBUG_DEVICE_AUTOCAL
    Debugger d(DebugAll,"CAL PREPARE"," submod=%u [%p]",subMod,m_owner);
#endif
    if (bak.module != BRF_CALIBRATE_RX_VGA2)
	return 0;
    // Prepare RX VGA2 calibration
    if (subMod > 4)
	return setUnhandled(e,subMod,"submodule");
    // RXVGA2 DC REF module (subMod 0)
    // Set RXVGA2GAIN A and B to default values
    if (subMod == 0)
	return lmsWrite(0x68,0x01,&e);
    // VGA2 A/B I/Q channels
    // Set DECODE bit to direct signal on start
    if (subMod == 1) {
	unsigned int status = setRxVga2Decode(false,&e);
	if (status)
	    return status;
    }
    // subMod 1: set RXVGA2GAIN A=18dB and B=0
    // subMod 3: set RXVGA2GAIN A=0 and B=18dB
    if (subMod == 1 || subMod == 3)
	return lmsWrite(0x68,(subMod == 1) ? 0x06 : 0x60,&e);
    return 0;
}

unsigned int BrfLibUsbDevice::dcCalProc(const BrfCalData& bak, uint8_t subMod,
    uint8_t dcCnt, uint8_t& dcReg, String& e)
{
#ifdef DEBUG_DEVICE_AUTOCAL
    Debugger d(DebugAll,"CAL PROC"," submod=%u dcCnt=0x%x [%p]",subMod,dcCnt,m_owner);
#endif
    // Set active calibration module address
    uint8_t data = 0;
    unsigned int status = 0;
    BRF_FUNC_CALL_RET(lmsRead(bak.desc->addr + 3,data,&e));
    data &= ~(0x07);
    data |= subMod & 0x07;
    BRF_FUNC_CALL_RET(lmsWrite(bak.desc->addr + 3,data,&e));
    // Set CNTVAL
    BRF_FUNC_CALL_RET(lmsWrite(bak.desc->addr + 2,dcCnt & 0x1f,&e));
    // DC_LOAD: Auto load DC_CNTVAL (1: load, 0: don't load)
    data |= 0x10;
    BRF_FUNC_CALL_RET(lmsWrite(bak.desc->addr + 3,data,&e));
    // Disable auto load of DC_CNTVAL, just in case something goes wrong
    data &= ~0x10;
    BRF_FUNC_CALL_RET(lmsWrite(bak.desc->addr + 3,data,&e));
    uint8_t clbrStart = data | 0x20;
    uint8_t clbrStop = data & ~0x20;
    // See Section 4.1: General DC calibration procedure
    bool first = true;
    while (true) {
	// Calibrate
	// Enable and disable DC_START_CLBR
	BRF_FUNC_CALL_RET(lmsWrite2(bak.desc->addr + 3,clbrStart,bak.desc->addr + 3,clbrStop,&e));
	// We should wait for 6.4 us for calibration to end
	Thread::msleep(1);
	dcReg = 0xff;
	for (unsigned int i = 0; i < 30; i++) {
	    String tmp;
	    BRF_FUNC_CALL_RET(cancelled(&e));
	    // Poll for DC_CLBR_DONE
	    status = lmsRead(bak.desc->addr + 1,data,&tmp);
	    if (status) {
		Debug(m_owner,DebugMild,"%s [%p]",e.c_str(),m_owner);
		status = 0;
		continue;
	    }
	    if ((data & 0x02) != 0)
		continue;
	    // Read DC_REG
	    BRF_FUNC_CALL_RET(lmsRead(bak.desc->addr,data,&e));
	    dcReg = (data & 0x3f);
	    break;
	}
	if (dcReg == 0xff)
	    return setError(RadioInterface::Failure,&e,"Calibration loop timeout");
	if (first) {
	    if (dcReg != 31)
		break;
	    first = false;
	    continue;
	}
	if (dcReg == 0) {
	    e << "Algorithm does not converge for submodule " << subMod;
	    return RadioInterface::Failure;
	}
	break;
    }
    DDebug(m_owner,DebugAll,"%s calibrated submodule %u -> %u [%p]",
	bak.modName(),subMod,dcReg,m_owner);
    return 0;
}

unsigned int BrfLibUsbDevice::dcCalProcPost(const BrfCalData& bak, uint8_t subMod,
    uint8_t dcReg, String& e)
{
#ifdef DEBUG_DEVICE_AUTOCAL
    Debugger d(DebugAll,"CAL PROC POST"," submod=%u dcReg=0x%x [%p]",subMod,dcReg,m_owner);
#endif
    unsigned int status = 0;
    if (bak.module == BRF_CALIBRATE_LPF_TUNING) {
	// Set DC_REG in TX/RX LPF DCO_DACCAL
	uint8_t addr[] = {0x55,0x35};
	for (uint8_t i = 0; !status && i < sizeof(addr); i++)
	    status = lmsSet(addr[i],dcReg,0x3f,&e);
	if (status)
	    e.printf("Failed to set DCO_DACCAL - %s",e.c_str());
    }
    return status;
}

// Read data, ignore any buffer related errors
unsigned int BrfLibUsbDevice::dummyRead(float* buf, unsigned int samples, String* error)
{
    int oldTsCheck = m_rxIO.checkTs;
    m_rxIO.checkTs = 0;
    unsigned int status = syncTxStatus(DevStatTs,0,error);
    if (!status)
	status = readBuffer(m_txIO.syncTs + 3 * samples,buf,samples,error);
    m_rxIO.checkTs = oldTsCheck;
    return status;
}

unsigned int BrfLibUsbDevice::readBuffer(uint64_t ts, float* buf, unsigned int samples,
    String* error)
{
    unsigned int status = 0;
    while (samples) {
	unsigned int n = samples;
	BRF_FUNC_CALL_RET(recv(ts,buf,n));
	BRF_FUNC_CALL_RET(cancelled(error));
	ts += n;
	buf += n * 2;
	samples -= n;
    }
    return 0;
}

static inline void cxMult(const float i1, const float q1, const float i2, const float q2,
    float &ir, float &qr)
{
    ir = i1 * i2 - q1 * q2;
    qr = i1 * q2 + i2 * q1;
}

static void calculateDcOffsets(BrfLibUsbDevice* dev, float* input, unsigned samples,
    float& totalPower, float& rxDcOffset, float* txDcOffset = 0)
{
    if ((samples % 4) != 0)
	Debug(dev->owner(),DebugFail,"Buffer samples (%u) should be multiple of 4 [%p]",
	    samples,dev->owner());

    // one cycle of a -pi/4 complex sinusoid
    // note that is NOT the same as the test tone cycle array
    static const float ci[4] = {+1,0,-1,0};
    static const float cq[4] = {0,-1,0,+1};

    float sumE = 0.0F;
    float sumRxI = 0.0F;
    float sumRxQ = 0.0F;
    float sumTxI = 0.0F;
    float sumTxQ = 0.0F;
    float* ip = input;
    // note that i counts complex pairs, not floats
    for (unsigned i = 0; i < samples; i++) {
	const float vi = *ip++;
	const float vq = *ip++;
	sumE += vi * vi + vq * vq;
	sumRxI += vi;
	sumRxQ += vq;
	float si, sq;
	cxMult(vi,vq,ci[i % 4],cq[i % 4],si,sq);
	sumTxI += si;
	sumTxQ += sq;
    }
    totalPower = sumE / samples;
    float meanRxI = sumRxI / samples;
    float meanRxQ = sumRxQ / samples;
    rxDcOffset = meanRxI * meanRxI + meanRxQ * meanRxQ;
    if (txDcOffset) {
	float meanTxI = sumTxI / samples;
	float meanTxQ = sumTxQ / samples;
	*txDcOffset = meanTxI * meanTxI+meanTxQ * meanTxQ;
    }
}

unsigned int BrfLibUsbDevice::readComputeDcOffsets(uint8_t dcI, uint8_t dcQ,
    float* buf, unsigned int samples, String* error,
    float& totalPower, float& rxDcOffset, float& txDcOffset)
{
    unsigned int status = 0;
    // Apply I/Q offset
    m_txIO.syncStatus.dcOffsetI = decodeDCOffs(true,dcI);
    m_txIO.syncStatus.dcOffsetQ = decodeDCOffs(true,dcQ);
    BRF_FUNC_CALL_RET(syncTxStatus(DevStatTs | DevStatDc,0,error));
    BRF_FUNC_CALL_RET(readBuffer(m_txIO.syncTs + 3 * samples,buf,samples,error));
    calculateDcOffsets(this,buf,samples,totalPower,rxDcOffset,&txDcOffset);
    return 0;
}

unsigned int BrfLibUsbDevice::readComputeDcOffsetsCorr(int* corr, float* powerBalance,
    float* buf, unsigned int samples, String* error, float& totalPower, float& rxDcOffset)
{
    unsigned int status = 0;
    if (corr) {
	// Apply TX FPGA phase correction
	m_txIO.syncStatus.fpgaCorrPhase = *corr;
	BRF_FUNC_CALL_RET(syncTxStatus(DevStatTs | DevStatFpgaPhase,0,error));
    }
    else if (powerBalance) {
	// Apply TX power balance
	m_txIO.syncStatus.powerBalance = *powerBalance;
	BRF_FUNC_CALL_RET(syncTxStatus(DevStatTs | DevStatPowerBalance,0,error));
    }
    else
	return showError(RadioInterface::OutOfRange,
	    "readComputeDcOffsetsCorr: invalid params",0,0,DebugFail);
    BRF_FUNC_CALL_RET(readBuffer(m_txIO.syncTs + 3 * samples,buf,samples,error));
    calculateDcOffsets(this,buf,samples,totalPower,rxDcOffset);
    return 0;
}

void BrfLibUsbDevice::calibrateBBStarting(const char* what)
{
    String s;
#if 0
    internalDumpDev(s,false,true,"\r\n",true,false,true);
    String dev;
    internalDumpPeripheral(UartDevLMS,0,128,&dev,16);
    s << "\r\nLMS:" << dev;
    dev = "";
    internalDumpPeripheral(UartDevGPIO,0,128,&dev,16);
    s << "\r\nGPIO:" << dev;
    dev = "";
    internalDumpPeripheral(UartDevSI5338,0,128,&dev,16);
    s << "\r\nSI5338:" << dev;
#endif
    Debug(m_owner,DebugInfo,"%s starting [%p]%s",what,m_owner,encloseDashes(s,true));
}

// Utility used in BB calibration
static inline void updateBBResult(int8_t& dir, float& last, float crt, bool first)
{
    if (first || last == crt)
	dir = 0;
    else
	dir = (last > crt) ? -1 : 1;
    last = crt;
}

static inline const char* dirStr(int8_t dir)
{
    return dir ? (dir > 0 ? "up" : "down") : "=";
}

struct BBDirChg
{
    inline BBDirChg(uint8_t maxChg) {
	    ::memset(this,0,sizeof(*this));
	    dirNotChgMax = maxChg;
	}
    inline bool update(uint8_t dir) {
	    if (lastDir != dir) {
		checkDirChg = true;
		lastDir = dir;
		dirNotChg = 1;
	    }
	    else
		dirNotChg++;
	    return !(checkDirChg && dirNotChgMax && dirNotChg >= dirNotChgMax);
	}

    int8_t lastDir;
    bool checkDirChg;
    uint8_t dirNotChg;
    uint8_t dirNotChgMax;
};

unsigned int BrfLibUsbDevice::calibrateBBTxDc(int& dcI, int& dcQ, float* buf,
    unsigned int samples, String* error)
{
//#define BRF_BB_TX_DC_TRACE
    BrfDuration duration;
    const char* oper = "Baseband TX DC calibration";
    calibrateBBStarting(oper);
    dcI = 128;
    dcQ = 128;
    int tmpDcI = 0;
    int tmpDcQ = 0;
    float power = 0;
    float rxDc = 0;
    float txDc = 0;
    uint8_t innerLoops[] = {18,18,31,31,5,5};
    uint8_t steps[] = {15,15,1,1,1,1};
    uint8_t dirNotChgStop[] = {4,4,5,5,2,2};
    String e;
    m_txIO.showDcOffsChange++;
    unsigned int status = dummyRead(buf,samples,&e);
    if (!status)
	status = readComputeDcOffsets(dcI,dcQ,buf,samples,&e,power,rxDc,txDc);
    float bestDcOffs = txDc;
    float lastDcOffs = txDc;
#ifdef BRF_BB_TX_DC_TRACE
    Output("Starting with I/Q=%d/%d power=%g rxDC=%g txDC=%g",dcI,dcQ,power,rxDc,txDc);
#endif
    for (unsigned int i = 0; !status && i < BRF_ARRAY_LEN(innerLoops); i++) {
	bool iLoop = ((i % 2) == 0);
	#define SET_I_Q_TEMP(_i,_q) tmpDcI = clampInt(_i,0,255); tmpDcQ = clampInt(_q,0,255); break
	switch (i) {
	    case 0: SET_I_Q_TEMP(255,128);
	    case 1: SET_I_Q_TEMP(dcI,255);
	    case 2: SET_I_Q_TEMP(dcI + 15,dcQ);
	    case 3: SET_I_Q_TEMP(dcI,dcQ + 15);
	    case 4: SET_I_Q_TEMP(dcI + 2,dcQ);
	    case 5: SET_I_Q_TEMP(dcI,dcQ + 2);
	}
	#undef SET_I_Q_TEMP
	int& bestIQ = iLoop ? dcI : dcQ;
	int& iq = iLoop ? tmpDcI : tmpDcQ;
#ifdef BRF_BB_TX_DC_TRACE
	Output("%d: LoopMode=%c I=%d Q=%d (best_I/Q=%d/%d)",
	    i + 1,brfIQ(iLoop),tmpDcI,tmpDcQ,dcI,dcQ);
#endif
	BBDirChg dirChg(dirNotChgStop[i]);
	for (unsigned int n = 0; n < innerLoops[i]; n++) {
	    status = readComputeDcOffsets(tmpDcI,tmpDcQ,buf,samples,&e,power,rxDc,txDc);
	    if (status)
		break;
	    int8_t dir = 0;
	    updateBBResult(dir,lastDcOffs,txDc,n == 0);
	    // First loop set dir to 'equal', avoid direction change in second loop
	    if (n == 1)
		dirChg.lastDir = dir;
#ifdef BRF_BB_TX_DC_TRACE
	    String dump;
	    dump.printf(1024,"  I=%-3d Q=%-3d power=%.6f\tdiff=%.6f\trxDC=%.6f\ttxDC=%f\t%s",
		tmpDcI,tmpDcQ,power,power - (rxDc + txDc),rxDc,txDc,dirStr(dir));
	    if (bestDcOffs > txDc)
		dump << "\t" << brfIQ(iLoop) << " " << bestIQ << " -> " << iq;
	    Output("%s",dump.c_str());
#endif
	    if (bestDcOffs > txDc) {
		bestDcOffs = txDc;
		bestIQ = iq;
	    }
	    if (!dirChg.update(dir))
		break;
	    if (iq - steps[i] < 0)
		break;
	    iq -= steps[i];
	}
    }
    duration.stop();
    m_txIO.showDcOffsChange--;
    if (status == 0) {
	dcI = decodeDCOffs(true,dcI);
	dcQ = decodeDCOffs(true,dcQ);
	Debug(m_owner,DebugInfo,"%s finished in %s I=%d Q=%d [%p]",
	    oper,duration.secStr(),dcI,dcQ,m_owner);
	return 0;
    }
    e.printf(2048,"%s failed - %s",oper,e.c_str());
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::calibrateBBTxPhase(bool bruteForce, int& corr, float* buf,
    unsigned int samples, String* error)
{
//#define BRF_BB_PHASE_TRACE
    BrfDuration duration;
    const char* oper = "Baseband TX PHASE calibration";
    calibrateBBStarting(oper);
    m_txIO.showFpgaPhaseChange++;
    corr = 0;
    float power = 0;
    float rxDc = 0;
    String e;
    unsigned int status = dummyRead(buf,samples,&e);
    if (bruteForce) {
	float imagePower = (float)0xffffffffffffffff;
#ifdef BRF_BB_PHASE_TRACE
	Output("Starting BRUTE FORCE image=%g",imagePower);
	unsigned int n = 0;
#endif
	int i = status ? (BRF_FPGA_CORR_MAX + 1) : -BRF_FPGA_CORR_MAX;
	for (; i <= BRF_FPGA_CORR_MAX; i++) {
	    status = readComputeDcOffsetsCorr(&i,0,buf,samples,&e,power,rxDc);
	    if (status)
		break;
	    float image = power - rxDc;
#ifdef BRF_BB_PHASE_TRACE
	    n++;
	    if (n < 10)
		Output("  %-5d\tbest=%d\tpower=%.6f\trxDC=%.6f\timage=%.6f",i,corr,power,rxDc,image);
	    else if ((n % 500) == 0)
		Output("  %-5d\tbest=%d",i,corr);
#endif
	    if (imagePower <= image)
		continue;
	    imagePower = image;
	    corr = i;
	}
    }
    else {
	if (!status)
	    status = readComputeDcOffsetsCorr(&corr,0,buf,samples,&e,power,rxDc);
	float imagePower = power - rxDc;
	float lastImage = imagePower;
#if 0
	unsigned int innerLoops[] = {BRF_FPGA_CORR_MAX,200,10};
	unsigned int steps[] = {200,10,1};
	uint8_t dirNotChgStop[] = {20,0,0};
#else
	unsigned int innerLoops[] = {BRF_FPGA_CORR_MAX,99};
	unsigned int steps[] = {50,1};
	uint8_t dirNotChgStop[] = {50,0};
#endif
#ifdef BRF_BB_PHASE_TRACE
	Output("Starting with power=%g rxDC=%g image=%g",power,rxDc,imagePower);
#endif
	for (unsigned int i = 0; !status && i < BRF_ARRAY_LEN(innerLoops); i++) {
	    int v = clampInt(corr + innerLoops[i],-BRF_FPGA_CORR_MAX,BRF_FPGA_CORR_MAX);
	    int minV = clampInt(corr - innerLoops[i],-BRF_FPGA_CORR_MAX,BRF_FPGA_CORR_MAX);
#ifdef BRF_BB_PHASE_TRACE
	    Output("Loop %u: best=%d interval=[%d..%d] step=%u",i,corr,minV,v,steps[i]);
#endif
	    BBDirChg dirChg(dirNotChgStop[i]);
	    unsigned int nMax = innerLoops[i] * 2 + 1;
	    for (unsigned int n = 0; n < nMax; n++) {
		status = readComputeDcOffsetsCorr(&v,0,buf,samples,&e,power,rxDc);
		if (status)
		    break;
		float image = power - rxDc;
		int8_t dir = 0;
		updateBBResult(dir,lastImage,image,n == 0);
		// First loop set dir to 'equal', avoid direction change in second loop
		if (n == 1)
		    dirChg.lastDir = dir;
#ifdef BRF_BB_PHASE_TRACE
		String dump;
		dump.printf("  %-5d\tpower=%.6f\trxDC=%.6f\timage=%.6f\t%s",
		    v,power,rxDc,image,dirStr(dir));
		if (imagePower > image)
		    dump << "\t" << corr << " -> " << v;
		Output("%s",dump.c_str());
#endif
		if (imagePower > image) {
		    imagePower = image;
		    corr = v;
		}
		if (!dirChg.update(dir))
		    break;
		int tmp = v - steps[i];
		if (tmp >= minV)
		    v = tmp;
		else if ((minV - tmp) < (int)steps[i])
		    v = minV;
		else
		    break;
	    }
	}
    }
    duration.stop();
    m_txIO.showFpgaPhaseChange--;
    if (status == 0) {
	Debug(m_owner,DebugInfo,"%s finished in %s corr=%d [%p]",
	    oper,duration.secStr(),corr,m_owner);
	return 0;
    }
    e.printf(2048,"%s failed - %s",oper,e.c_str());
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::calibrateBBTxGain(bool bruteForce, float& corr, float* buf,
    unsigned int samples, String* error)
{
//#define BRF_BB_GAIN_TRACE
    BrfDuration duration;
    const char* oper = "Baseband TX GAIN calibration";
    calibrateBBStarting(oper);
    m_txIO.showPowerBalanceChange++;
    corr = 1;
    float power = 0;
    float rxDc = 0;
    String e;
    unsigned int status = dummyRead(buf,samples,&e);
    if (bruteForce) {
	// Brute force
	float imagePower = (float)0xffffffffffffffff;
	float crt = 0.9;
	float step = 0.0005;
#ifdef BRF_BB_GAIN_TRACE
	Output("Starting BRUTE FORCE start=%g step=%g image=%g",crt,step,imagePower);
	unsigned int n = 0;
#endif
	if (status)
	    crt = 3;
	for (; crt < 1.1; crt += step) {
	    status = readComputeDcOffsetsCorr(0,&crt,buf,samples,&e,power,rxDc);
	    if (status)
		break;
	    float image = power - rxDc;
#ifdef BRF_BB_GAIN_TRACE
	    n++;
	    if (n < 10)
		Output("  %g\t\tbest=%g\tpower=%.6f\trxDC=%.6f\timage=%.6f",crt,corr,power,rxDc,image);
	    else if ((n % 30) == 0)
		Output("  %g\tbest=%g",crt,corr);
#endif
	    if (imagePower <= image)
		continue;
	    imagePower = image;
	    corr = crt;
	}
    }
    else {
	if (!status)
	    status = readComputeDcOffsetsCorr(0,&corr,buf,samples,&e,power,rxDc);
	float imagePower = power - rxDc;
	float lastImage = imagePower;
#if 0
	float innerBounds[] = {1,0.5,0.2,0.1,0.05};
	unsigned int innerIntervals[] = {10,20,20,20,100};
#else
	float innerBounds[] = {0.1,0.01};
	unsigned int innerIntervals[] = {500,100};
#endif
#ifdef BRF_BB_GAIN_TRACE
	Output("Starting with power=%g rxDC=%g image=%g",power,rxDc,imagePower);
#endif
	for (unsigned int i = 0; !status && i < BRF_ARRAY_LEN(innerBounds); i++) {
	    float crt = corr + innerBounds[i];
	    if (crt >= 2)
		crt = 1.999999;
	    unsigned int intervals = innerIntervals[i] + 1;
	    float step = innerBounds[i] * 2 / intervals;
#ifdef BRF_BB_GAIN_TRACE
	    Output("Loop %u: best=%g corrStart=%g intervals=%u step=%.5f",i,corr,crt,intervals,step);
#endif
	    BBDirChg dirChg(0);
	    for (unsigned int n = 0; n < intervals && crt > 0; n++, crt -= step) {
		status = readComputeDcOffsetsCorr(0,&crt,buf,samples,&e,power,rxDc);
		if (status)
		    break;
		float image = power - rxDc;
		int8_t dir = 0;
		updateBBResult(dir,lastImage,image,n == 0);
#ifdef BRF_BB_GAIN_TRACE
		String dump;
		dump.printf("  %.6f\tpower=%.6f\trxDC=%.6f\timage=%.6f\t%s",
		    crt,power,rxDc,image,dirStr(dir));
		if (imagePower > image)
		    dump << "\t" << corr << " -> " << crt;
		Output("%s",dump.c_str());
#endif
		if (imagePower > image) {
		    imagePower = image;
		    corr = crt;
		}
	    }
	}
    }
    duration.stop();
    m_txIO.showPowerBalanceChange--;
    if (status == 0) {
	Debug(m_owner,DebugInfo,"%s finished in %s corr=%g [%p]",
	    oper,duration.secStr(),corr,m_owner);
	return 0;
    }
    e.printf(2048,"%s failed - %s",oper,e.c_str());
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::calibrateBB(String* error)
{
    Debug(m_owner,DebugInfo,"Calibrating BB TX [%p]",m_owner);
    String e;
    unsigned int status = 0;
    String pattern = m_txPatternStr;
    int oldLoop = m_loopback;
    BrfDevStatus txOld(m_txIO);
    BrfDevStatus rxOld(m_rxIO);
    bool oldRxDcAuto = setRxDcAuto(false);
    BrfDevTmpAltSet tmpAltSet(this,status,&e,"Calibrate BB");
    BrfDuration duration;
    while (status == 0) {
	bool paOn = false;
	bool lowBand = brfIsLowBand(txOld.frequency);
	int lp = lowBand ? LoopRfLna1 : LoopRfLna2;
	const char* pattern = "circle";
	const char* lmsWr = 0;
	bool twice = true;
	bool bruteForce = true;
	unsigned int samples = m_rxIO.bufSamples * m_rxIO.buffers;
	NamedList p("calibrate-bb");
	loadCfg(false,&p);
	if (p.c_str()) {
	    lp = lookup(p["loopback"],s_loopback,lp);
	    paOn = p.getBoolValue("transmit");
	    pattern = p.getValue("txpattern",pattern);
	    lmsWr = p.getValue("lms-write");
	    twice = p.getBoolValue("phase-gain-twice",true);
	    bruteForce = p.getBoolValue("phase-gain-bruteforce",true);
	    samples = p.getIntValue("samples",samples,512);
	}
	if ((samples % 4) != 0)
	    samples = 4 * (samples + 3) / 4;
	BRF_FUNC_CALL_BREAK(internalSetLoopback(lp,&e));
	// Set VGA
	BRF_FUNC_CALL_BREAK(internalSetVga(true,-14,true,&e));
	BRF_FUNC_CALL_BREAK(internalSetVga(true,25,false,&e));
	BRF_FUNC_CALL_BREAK(internalSetVga(false,30,true,&e));
	BRF_FUNC_CALL_BREAK(internalSetVga(false,0,false,&e));
	// 0x64: VCM (bits 5-2): RX VGA2 output common voltage control
	// Make sure we have the correct value to operate
	BRF_FUNC_CALL_BREAK(lmsSet(0x64,0x34,0x3c,&e));
	if (paOn)
	    BRF_FUNC_CALL_BREAK(selectPaLna(true,lowBand,&e));
	setTxPattern(pattern);
	if (lmsWr)
	    lmsWrite(lmsWr);

	//dumpLoopbackStatus();
	//dumpLmsModulesStatus();
	//internalDumpPeripheral(UartDevLMS,0,128,0,16);

	// Start sending thread
	m_sendThread = (new BrfThread(this))->start();
	if (!m_sendThread) {
	    status = RadioInterface::Failure;
	    e << "Failed to start send data thread";
	    break;
	}
	
	unsigned int Fc = 850000000;
	m_calibration.assign("");
	m_calibration.clearParams();
	DataBlock d(0,samplesf2bytes(samples));
	float* buf = (float*)d.data(0);
	int dcI = 0;
	int dcQ = 0;
	int phase = 0;
	float corrBalance = 0;
	
	Debug(m_owner,DebugAll,"BB calibration: samples=%u Fc=%u samplerate=%u [%p]",
	    samples,Fc,m_txIO.sampleRate,m_owner);
	
	// Calibrate TX DC
	m_txIO.syncStatus.frequency = Fc + m_txIO.sampleRate / 4;
	m_rxIO.syncStatus.frequency = Fc;
	BRF_FUNC_CALL_BREAK(syncTxStatus(DevStatFreq,DevStatFreq,&e));
	BRF_FUNC_CALL_BREAK(calibrateBBTxDc(dcI,dcQ,buf,samples,&e));
	BRF_FUNC_CALL_BREAK(internalSetCorrectionIQ(true,dcI,dcQ,&e));
	// Calibrate TX FPGA PHASE
	m_txIO.syncStatus.frequency = Fc;
	m_rxIO.syncStatus.frequency = Fc - m_txIO.sampleRate / 2;
	BRF_FUNC_CALL_BREAK(syncTxStatus(DevStatFreq,DevStatFreq,&e));
	BRF_FUNC_CALL_BREAK(calibrateBBTxPhase(bruteForce,phase,buf,samples,&e));
	BRF_FUNC_CALL_BREAK(internalSetFpgaCorr(true,CorrFpgaPhase,phase,&e));
	// Calibrate TX power balance
	BRF_FUNC_CALL_BREAK(calibrateBBTxGain(bruteForce,corrBalance,buf,samples,&e));
	BRF_FUNC_CALL_BREAK(internalSetTxIQBalance(false,corrBalance));
	if (twice) {
	    BRF_FUNC_CALL_BREAK(calibrateBBTxPhase(bruteForce,phase,buf,samples,&e));
	    BRF_FUNC_CALL_BREAK(internalSetFpgaCorr(true,CorrFpgaPhase,phase,&e));
	    // Calibrate TX power balance
	    BRF_FUNC_CALL_BREAK(calibrateBBTxGain(bruteForce,corrBalance,buf,samples,&e));
	    BRF_FUNC_CALL_BREAK(internalSetTxIQBalance(false,corrBalance));
	}
	m_calibration.assign(String(Fc));
	m_calibration.addParam("tx_dc_i",String(dcI));
	m_calibration.addParam("tx_dc_q",String(dcQ));
	m_calibration.addParam("tx_fpga_corr_phase",String(phase));
	m_calibration.addParam("tx_powerbalance",String(corrBalance));
	break;
    }
    duration.stop();
    setRxDcAuto(oldRxDcAuto);
    Debug(m_owner,DebugAll,"Finalizing BB calibration [%p]",m_owner);
    BrfThread::cancelThread(m_sendThread,&m_sendThreadMutex,1000,m_owner,m_owner);
    // Restore loopback
    internalSetLoopback(oldLoop);
    // Restore status
    unsigned int restore = DevStatFreq | DevStatVga | DevStatLpfBw;
    setStatus(txOld,restore,rxOld,restore);
    // Restore TX pattern
    setTxPattern(pattern);
    if (status == 0) {
	String tmp;
	m_calibration.dump(tmp,"\r\n");
	Debug(m_owner,DebugInfo,"Calibrated BB in %s [%p]%s",
	    duration.secStr(),m_owner,encloseDashes(tmp,true));
	return 0;
    }
    e.printf(1024,"BB calibration failed: %s",e.c_str());
    return showError(status,e,0,error);
}

static inline void computeMinMax(int& minVal, int& maxVal, int val)
{
    if (minVal > val)
	minVal = val;
    if (maxVal < val)
	maxVal = val;
}

static inline void computeRxAdjustPeak(int& p, int val, uint64_t& peakTs, uint64_t& ts)
{
    if (p >= val)
	return;
    p = val;
    peakTs = ts;
}

// DC offsets compensation feedback using an exponential moving average
static inline int computeCorrection(int& rxDcAvg, int offs, int avg, int dcOffsMax)
{
    rxDcAvg = avg + ((BRF_RX_DC_OFFSET_AVG_DAMPING - 1) * rxDcAvg /
	BRF_RX_DC_OFFSET_AVG_DAMPING);
    if ((rxDcAvg > dcOffsMax)) {
	if (offs < BRF_RX_DC_OFFSET_MAX) {
	    offs++;
	    rxDcAvg = 0;
	}
    }
    else if ((rxDcAvg < -dcOffsMax) && (offs > -BRF_RX_DC_OFFSET_MAX)) {
	offs--;
        rxDcAvg = 0;
    }
    return offs;
}

// Compute Rx avg values, autocorrect offsets if configured
void BrfLibUsbDevice::computeRx(uint64_t ts)
{
    unsigned int dbg = checkDbgInt(m_rxShowDcInfo);
    if (!(dbg || m_rxDcAuto))
	return;
    // Compute averages and peak values
    int dcIMin = 32767;
    int dcIMax = -32767;
    int dcIAvg = 0;
    int dcQMin = 32767;
    int dcQMax = -32767;
    int dcQAvg = 0;
    int peak = 0;
    uint64_t peakTs = 0;
    for (unsigned int i = 0; i < m_rxIO.buffers; i++) {
	int16_t* d = m_rxIO.samples(i);
	for (int16_t* last = m_rxIO.samplesEOF(i); d != last;) {
	    int dcI = *d++;
	    int dcQ = *d++;
	    dcIAvg += dcI;
	    dcQAvg += dcQ;
	    if (!dbg)
		continue;
	    computeMinMax(dcIMin,dcIMax,dcI);
	    computeMinMax(dcQMin,dcQMax,dcQ);
	    computeRxAdjustPeak(peak,dcIMax,peakTs,ts);
	    computeRxAdjustPeak(peak,-dcIMin,peakTs,ts);
	    computeRxAdjustPeak(peak,dcQMax,peakTs,ts);
	    computeRxAdjustPeak(peak,-dcQMin,peakTs,ts);
	    ts++;
	}
    }
    int div = m_rxIO.buffers * m_rxIO.bufSamples;
    dcIAvg /= div;
    dcQAvg /= div;
    if (dbg)
	Debug(m_owner,DebugInfo,
	    "RX DC values min/avg/max I=%d/%d/%d Q=%d/%d/%d peak=%d TS=" FMT64U " [%p]",
	    dcIMin,dcIAvg,dcIMax,dcQMin,dcQAvg,dcQMax,peak,peakTs,m_owner);
    if (!m_rxDcAuto)
	return;
    int corrI = computeCorrection(m_rxDcAvgI,m_rxIO.dcOffsetI,dcIAvg,m_rxDcOffsetMax);
    int corrQ = computeCorrection(m_rxDcAvgQ,m_rxIO.dcOffsetQ,dcQAvg,m_rxDcOffsetMax);
    if (corrI == m_rxIO.dcOffsetI && corrQ == m_rxIO.dcOffsetQ)
	return;
    BRF_TX_SERIALIZE_NONE;
    DDebug(m_owner,DebugInfo,"Adjusting Rx DC offsets I=%d Q=%d [%p]",
	corrI,corrQ,m_owner);
    internalSetCorrectionIQ(false,corrI,corrQ);
}

// Set error string or put a debug message
unsigned int BrfLibUsbDevice::showError(unsigned int code, const char* error,
    const char* prefix, String* buf, int level)
{
    if (buf)
	return setError(code,buf,error,prefix);
    String tmp;
    setError(code,&tmp,error,prefix);
    if (code != RadioInterface::Cancelled)
	Debug(m_owner,level,"%s [%p]",tmp.c_str(),m_owner);
    else
	Debug(m_owner,DebugAll,"%s [%p]",tmp.c_str(),m_owner);
    return code;
}

void BrfLibUsbDevice::printIOBuffer(bool tx, const char* loc, int index, unsigned int nBufs)
{
    BrfDevIO& io = getIO(tx);
    if (!nBufs)
	nBufs = io.buffers;
    for (unsigned int i = 0; i < nBufs; i++) {
	if (index >= 0 && index != (int)i)
	    continue;
	String s;
	if (io.showBufData)
	    io.dumpInt16Samples(s,i);
	Output("%s: %s [%s] buffer %u TS=" FMT64U " [%p]%s",
	    m_owner->debugName(),brfDir(tx),loc,i,io.bufTs(i),m_owner,
	    encloseDashes(s,true));
    }
}

void BrfLibUsbDevice::dumpIOBuffer(BrfDevIO& io, unsigned int nBufs)
{
    nBufs = checkDbgInt(io.dataDump,nBufs);
    for (unsigned int i = 0; i < nBufs; i++)
	if (!io.dataDumpFile.write(io.bufTs(i),io.samples(i),io.bufSamplesLen,owner()))
	    nBufs = 0;
    if (!nBufs)
	io.dataDumpFile.terminate(owner());
}

void BrfLibUsbDevice::updateIODump(BrfDevIO& io)
{
    Lock lck(m_dbgMutex);
    NamedList lst[2] = {io.dataDumpParams,io.upDumpParams};
    io.dataDumpParams.assign("");
    io.dataDumpParams.clearParams();
    io.upDumpParams.assign("");
    io.upDumpParams.clearParams();
    lck.drop();
    NamedList p(Engine::runParams());
    p.addParam("boardserial",serial());
    for (uint8_t i = 0; i < 2; i++) {
	NamedList& nl = lst[i];
	if (!nl)
	    continue;
	RadioDataFile& f = i ? io.upDumpFile : io.dataDumpFile;
	int& dump = i ? io.upDump : io.dataDump;
	f.terminate(owner());
	dump = 0;
	int n = 0;
	String file = nl[YSTRING("file")];
	if (file) {
	    n = nl.getIntValue(YSTRING("count"),-1);
	    if (!n)
		file = 0;
	}
	if (file)
	    p.replaceParams(file);
	if (!file)
	    continue;
	RadioDataDesc d;
	if (!i) {
	    d.m_elementType = RadioDataDesc::Int16;
	    d.m_littleEndian = true;
	}
	if (f.open(file,&d,owner()))
	    dump = n;
    }
}

// Enable or disable loopback
unsigned int BrfLibUsbDevice::internalSetLoopback(int mode, String* error)
{
    if (m_loopback == mode)
	return 0;
    const char* what = lookup(mode,s_loopback);
#if 1
    XDebug(m_owner,DebugAll,"internalSetLoopback(%d) '%s' [%p]",mode,what,m_owner);
#else
    Debugger d(DebugAll,"BrfLibUsbDevice::internalSetLoopback()"," %d '%s' [%p]",
	mode,what,m_owner);
#endif
    String e;
    unsigned int status = 0;
    BrfDevTmpAltSet tmpAltSet(this,status,&e,"Set loopback");
    int lna = LmsLnaNone;
    while (status == 0) {
	// Disable everything before enabling the loopback
	BRF_FUNC_CALL_BREAK(lnaSelect(LmsLnaNone,&e));
	BRF_FUNC_CALL_BREAK(paSelect(LmsPaNone,&e));
	BRF_FUNC_CALL_BREAK(setLoopbackPath(LoopNone,e));
	// Prepare the loopback (enable / disable modules)
	switch (mode) {
	    case LoopFirmware:
		status = RadioInterface::OutOfRange;
		e = "Not implemented";
		break;
	    case LoopLpfToRxOut:
		// Disable RX VGA2 and LPF
		BRF_FUNC_CALL_BREAK(internalEnableRxVga(false,false,&e));
		BRF_FUNC_CALL_BREAK(internalSetLpf(false,LpfDisabled,&e));
		break;
	    case LoopLpfToVga2:
	    case LoopVga1ToVga2:
		// Disable RX VGA1 and LPF
		BRF_FUNC_CALL_BREAK(internalEnableRxVga(false,false,&e));
		BRF_FUNC_CALL_BREAK(internalSetLpf(false,LpfDisabled,&e));
		break;
	    case LoopLpfToLpf:
	    case LoopVga1ToLpf:
		// Disable RX VGA1, Enable RX LPF and RX VGA2
		BRF_FUNC_CALL_BREAK(internalEnableRxVga(false,true,&e));
		BRF_FUNC_CALL_BREAK(internalSetLpf(false,LpfNormal,&e));
		BRF_FUNC_CALL_BREAK(internalEnableRxVga(true,false,&e));
		break;
	    case LoopRfLna1:
		lna = LmsLna1;
		// falthrough
	    case LoopRfLna2:
		if (lna == LmsLnaNone)
		    lna = LmsLna2;
		// falthrough
	    case LoopRfLna3:
		if (lna == LmsLnaNone)
		    lna = LmsLna2;
		// Select PA AUX and enable LNA
		BRF_FUNC_CALL_BREAK(paSelect(LmsPaAux,&e));
		BRF_FUNC_CALL_BREAK(lnaEnable(true,&e));
		BRF_FUNC_CALL_BREAK(lnaSelect(lna,&e));
		// Enable RX path
		BRF_FUNC_CALL_BREAK(internalEnableRxVga(true,true,&e));
		BRF_FUNC_CALL_BREAK(internalSetLpf(false,LpfNormal,&e));
	    	BRF_FUNC_CALL_BREAK(internalEnableRxVga(true,false,&e));
		// Select output buffer in RX PLL
		BRF_FUNC_CALL_BREAK(lmsSet(0x25,lna,0x03,&e));
		status = enableRfLoopback(true,e);
		break;
	    case LoopNone:
		BRF_FUNC_CALL_BREAK(restoreFreq(true,&e));
		BRF_FUNC_CALL_BREAK(internalEnableRxVga(true,true,&e));
		BRF_FUNC_CALL_BREAK(internalSetLpf(false,LpfNormal,&e));
		BRF_FUNC_CALL_BREAK(internalEnableRxVga(true,false,&e));
		BRF_FUNC_CALL_BREAK(enableRfLoopback(false,e));
		BRF_FUNC_CALL_BREAK(lnaEnable(true,&e));
		status = restoreFreq(false,&e);
		break;
	    default:
		Debug(m_owner,DebugStub,"Loopback: unhandled value %d [%p]",mode,m_owner);
		status = setUnkValue(e,"mode " + String(mode));
	}
	if (!status)
	    status = setLoopbackPath(mode,e);
	break;
    }
    if (status == 0) {
	Debug(m_owner,DebugNote,"Loopback changed '%s' -> '%s' [%p]",
	    lookup(m_loopback,s_loopback),what,m_owner);
	m_loopback = mode;
	return 0;
    }
    if (mode != LoopNone)
	e.printf(1024,"Failed to set loopback to %d (%s): %s",
	    mode,what,e.c_str());
    else
	e.printf(1024,"Failed to disable loopback: %s",e.c_str());
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::setLoopbackPath(int mode, String& error)
{
    const char* what = lookup(mode,s_loopback);
    XDebug(m_owner,DebugAll,"setLoopbackPath(%d) '%s' [%p]",mode,what,m_owner);
    uint8_t rf = 0;
    uint8_t baseband = 0;
    String e;
    unsigned int status = lmsRead2(0x08,rf,0x46,baseband,&e);
    if (status == 0) {
	// Stop all loopbacks
	rf &= ~0x7f;
	baseband &= ~0x0c;
	switch (mode) {
	    case LoopFirmware:
		status = RadioInterface::OutOfRange;
		e = "Not implemented";
		break;
	    case LoopLpfToRxOut:
		rf |= 0x10;       // LBEN_OPIN
		baseband |= 0x04; // LOOPBBEN[1:0] 1
		break;
	    case LoopLpfToVga2:
		rf |= 0x20;       // LBEN_VGA2IN
		baseband |= 0x04; // LOOPBBEN[1:0] 1
		break;
	    case LoopVga1ToVga2:
		rf |= 0x20;       // LBEN_VGA2IN
		baseband |= 0x08; // LOOPBBEN[1:0] 2
		break;
	    case LoopLpfToLpf:
		rf |= 0x40;       // LBEN_LPFIN
		baseband |= 0x04; // LOOPBBEN[1:0] 1
		break;
	    case LoopVga1ToLpf:
		rf |= 0x40;       // LBEN_LPFIN
		baseband |= 0x08; // LOOPBBEN[1:0] 2
		break;
	    case LoopRfLna1:
		rf |= 0x01;
		break;
	    case LoopRfLna2:
		rf |= 0x02;
		break;
	    case LoopRfLna3:
		rf |= 0x03;
		break;
	    case LoopNone:
		break;
	    default:
		Debug(m_owner,DebugStub,"Loopback path set: unhandled value %d [%p]",
		    mode,m_owner);
		status = setUnkValue(e,"mode " + String(mode));
	}
	if (status == 0)
	    status = lmsWrite2(0x08,rf,0x46,baseband,&e);
    }
    if (status == 0)
	Debug(m_owner,DebugAll,"Loopback path switches configured for '%s' [%p]",
	    what,m_owner);
    else
	error << "Failed to configure path switches - " << e;
    return status;
}

unsigned int BrfLibUsbDevice::enableRfLoopback(bool on, String& error)
{
    String e;
    unsigned int status = lmsChangeMask(0x0b,0x01,on,&e);
    if (status == 0) {
	Debug(m_owner,DebugAll,"%s RF loopback [%p]",enabledStr(on),m_owner);
	return 0;
    }
    error << "Failed to " << enableStr(on) << " RF loopback -" << e;
    return status;
}

void BrfLibUsbDevice::dumpLoopbackStatus()
{
#define BRF_LS_RESULT(name,mask) \
    s << "\r\n  " << name << ": " << (status ? "ERROR" : tmp.printf("0x%x",data & mask).c_str())
#define BRF_LS_RESULT_OPEN(name,mask,valueOpen) \
    BRF_LS_RESULT(name,mask); \
    if (status == 0) \
	s << " - " << (((data & mask) == valueOpen) ? "open" : "closed");
    String s;
    String tmp;
    uint8_t data = 0;
    unsigned int status = 0;
    // TX Path
    s << "\r\nTX PATH:";
    status = lmsRead(0x35,data);
    BRF_LS_RESULT("BYP_EN_LPF",0x40); // LPF bypass enable (1: bypass, 0: normal)
    if (!status)
	s << " - " << lookup((data & 0x40) == 0x40 ? LpfBypass : LpfNormal,s_lpf);
    status = lmsRead(0x46,data);
    BRF_LS_RESULT_OPEN("LOOPBBEN[1:0]",0x0c,0x00); // Baseband loopback swithes control (00: open, 11: closed)
    status = lmsRead(0x08,data);
    BRF_LS_RESULT_OPEN("LBEN_OPIN",0x10,0x00); // enabled: RX VGA2 and RXLPF should be disabled
    BRF_LS_RESULT_OPEN("LBEN_VGA2IN",0x20,0x00); // enabled: LPF should be disabled
    BRF_LS_RESULT_OPEN("LBEN_LPFIN",0x40,0x00); // enabled: RXTIA should be disabled
    BRF_LS_RESULT("LBRFEN (TXMIX)",0x0f); // LNAs should be disabled
    if (!status) {
	s << " - ";
	switch (data & 0x0f) {
	    case 0: s << "open"; break;
	    case 1: s << "LNA1"; break;
	    case 2: s << "LNA2"; break;
	    case 3: s << "LNA3"; break;
	    default: s << "invalid";
	}
    }
    status = lmsRead(0x0b,data);
    BRF_LS_RESULT_OPEN("PD[0] (RF loopback)",0x01,0x00);
    s << "\r\nRX PATH:";
    status = lmsRead(0x55,data);
    BRF_LS_RESULT("BYP_EN_LPF",0x40); // LPF bypass enable
    if (!status)
	s << " - " << lookup((data & 0x40) == 0x40 ? LpfBypass : LpfNormal,s_lpf);
    status = lmsRead(0x09,data);
    BRF_LS_RESULT_OPEN("RXOUTSW",0x80,0x00); // RXOUTSW switch
    Debug(m_owner,DebugAll,"Loopback switches: [%p]%s",m_owner,encloseDashes(s));
#undef BRF_LS_RESULT
#undef BRF_LS_RESULT_OPEN
}

void BrfLibUsbDevice::dumpLmsModulesStatus()
{
#define BRF_MS_RESULT(name,result) \
    s << "\r\n  " << name << ": " << (status ? "ERROR" : result)
#define BRF_MS_RESULT_ACTIVE(name,result,mask,valueActive) \
    BRF_MS_RESULT(name,result); \
    if (status == 0) \
	s << tmp.printf("0x%x - %s",data & mask,activeStr((data & mask) == valueActive))
    String s;
    String tmp;
    int tmpInt = 0;
    uint8_t data = 0;
    unsigned int status = 0;
    // TX
    s << "\r\nTX:";
    status = internalGetLpf(true,&tmpInt);
    BRF_MS_RESULT("LPF",lookup(tmpInt,s_lpf));
    status = lmsRead(0x44,data);
    BRF_MS_RESULT("AUXPA",tmp.printf("0x%x - %s",data & 0x04,enableStr((data & 0x04) == 0)));
    BRF_MS_RESULT("PA",tmp.printf("0x%x - %d",data & 0x18,((data & 0x18) >> 3)));
    s << "\r\nRX:";
    status = lmsRead(0x75,data);
    tmp.clear();
    if (status == 0)
	tmp.printf("0x%x (%d)",data & 0x30,(data & 0x30) >> 4);
    BRF_MS_RESULT("LNA","");
    s << "Selected: " << tmp.safe("ERROR");
    status = lmsRead(0x7d,data);
    if (status == 0)
	s << tmp.printf(" - (0x%x %s)",data & 0x01,activeStr((data & 0x01) == 0));
    else
	s << " - Active: ERROR";
    BRF_MS_RESULT_ACTIVE("VGA1","",0x08,0);
    status = internalGetLpf(false,&tmpInt);
    BRF_MS_RESULT("LPF",lookup(tmpInt,s_lpf));
    status = lmsRead(0x64,data);
    BRF_MS_RESULT_ACTIVE("VGA2","",0x02,0x02);
    Debug(m_owner,DebugAll,"LMS modules status: [%p]%s",m_owner,encloseDashes(s));
#undef BRF_MS_RESULT_ACTIVE
#undef BRF_MS_RESULT
}

unsigned int BrfLibUsbDevice::internalDumpPeripheral(uint8_t dev, uint8_t addr,
    uint8_t len, String* buf, uint8_t lineLen)
{
    uint8_t data[256];
    unsigned int status = 0;
    BRF_FUNC_CALL_RET(accessPeripheral(dev,false,addr,data,0,len));
    bool outHere = (buf == 0);
    String s;
    if (!buf)
	buf = &s;
    if (lineLen) {
	String s1, s2;
	uint8_t* d = data;
	uint8_t a = addr;
	uint8_t n = len / lineLen;
	for (; n; n--, d += lineLen, a += lineLen)
	    *buf << "\r\n" << s1.hexify(&a,1) << "\t" << s2.hexify(d,lineLen,' ');
	n = len % lineLen;
	if (n)
	    *buf << "\r\n" << s1.hexify(&a,1) << "\t" << s2.hexify(d,n,' ');
    }
    else
	buf->hexify(data,len,' ');
    if (outHere)
	Output("%s %s status (addr=0x%x):%s",
	    m_owner->debugName(),s_uartDev[dev].c_str(),addr,encloseDashes(*buf));
    return 0;
}


//
// BrfInterface
//
BrfInterface::BrfInterface(const char* name)
    : RadioInterface(name),
    m_dev(0),
    m_txFreqCorr(0)
{
    debugChain(&__plugin);
    Debug(this,DebugAll,"Interface created [%p]",this);
}

BrfInterface::~BrfInterface()
{
    Debug(this,DebugAll,"Interface destroyed [%p]",this);
}

BrfLibUsbDevice* BrfInterface::init()
{
    m_dev = BrfLibUsbDevice::create(this);
    m_radioCaps = &m_dev->capabilities();
    Debug(this,DebugAll,"Created device (%p) [%p]",m_dev,this);
    return m_dev;
}

unsigned int BrfInterface::initialize(const NamedList& params)
{
    return m_dev->powerOn();
}

unsigned int BrfInterface::setParams(NamedList& params, bool shareFate)
{
    unsigned int code = 0;
    NamedList failed("");
#define SETPARAMS_HANDLE_CODE(c) { \
    if (c) { \
	if (!code || code == Pending) \
	    code = c; \
	failed.addParam(cmd + "_failed",String(c)); \
	if (shareFate && c != Pending) \
	    break; \
    } \
}
#define METH_CALL(func) { \
    unsigned int c = func(); \
    SETPARAMS_HANDLE_CODE(c); \
    continue; \
}
#define METH_CALL_1(func,value) { \
    unsigned int c = func(value); \
    SETPARAMS_HANDLE_CODE(c); \
    continue; \
}
#ifdef XDEBUG
    String tmp;
    params.dump(tmp,"\r\n");
    Debug(this,DebugAll,"setParams [%p]%s",this,encloseDashes(tmp,true));
#endif
    for (ObjList* o = params.paramList()->skipNull(); o; o = o->skipNext()) {
	NamedString* ns = static_cast<NamedString*>(o->get());
	if (!ns->name().startsWith("cmd:"))
	    continue;
	String cmd = ns->name().substr(4);
	if (!cmd)
	    continue;
	if (cmd == YSTRING("setSampleRate"))
	    METH_CALL_1(setSampleRate,(uint64_t)ns->toInt64());
	if (cmd == YSTRING("setFilter"))
	    METH_CALL_1(setFilter,(uint64_t)ns->toInt64());
	if (cmd == "calibrate")
	    METH_CALL(calibrate);
	Debug(this,DebugNote,"setParams: unhandled cmd '%s' [%p]",cmd.c_str(),this);
	SETPARAMS_HANDLE_CODE(NotSupported);
    }
#undef SETPARAMS_HANDLE_CODE
#undef METH_CALL
#undef METH_CALL_1
    if (code)
	params.copyParams(failed);
#ifdef XDEBUG
    tmp.clear();
    params.dump(tmp,"\r\n");
    Debug(this,DebugAll,"setParams [%p]%s",this,encloseDashes(tmp,true));
#endif
    return code;
}

unsigned int BrfInterface::send(uint64_t when, float* samples, unsigned size,
    float* powerScale)
{
    return m_dev->syncTx(when,samples,size,powerScale);
}

unsigned int BrfInterface::recv(uint64_t& when, float* samples, unsigned int& size)
{
    return m_dev->syncRx(when,samples,size);
}

unsigned int BrfInterface::setFrequency(uint64_t hz, bool tx)
{
    XDebug(this,DebugAll,"BrfInterface::setFrequency(" FMT64U ",%s) [%p]",
	hz,brfDir(tx),this);
    if (hz > 0xffffffff) {
	Debug(this,DebugNote,
	    "Failed to set %s frequency " FMT64U ": out of range [%p]",
	    brfDir(tx),hz,this);
	return OutOfRange;
    }
    uint32_t freq = (uint32_t)hz;
    unsigned int status = m_dev->setFrequency(freq,tx);
    if (status)
	return status;
    uint32_t tmp = 0;
    status = m_dev->getFrequency(tmp,tx);
    if (status)
	return status;
    if (tmp != freq) {
	Debug(this,DebugNote,"Failed to set %s frequency requested=%u read=%u [%p]",
	    brfDir(tx),freq,tmp,this);
	return NotExact;
    }
    return 0;
}

unsigned int BrfInterface::getFrequency(uint64_t& hz, bool tx) const
{
    uint32_t freq = 0;
    unsigned int status = m_dev->getFrequency(freq,tx);
    if (status == 0)
	hz = freq;
    return status;
}

unsigned int BrfInterface::setSampleRate(uint64_t hz)
{
    XDebug(this,DebugAll,"BrfInterface::setSampleRate(" FMT64U ") [%p]",hz,this);
    uint32_t srate = (uint32_t)hz;
    unsigned int status = m_dev->setSamplerate(srate,true);
    if (status)
	return status;
    status = m_dev->setSamplerate(srate,false);
    if (status)
	return status;
    uint32_t tmp = 0;
    status = m_dev->getSamplerate(tmp,true);
    if (status)
	return status;
    if (tmp != srate) {
	Debug(this,DebugNote,"Failed to set TX samplerate requested=%u read=%u [%p]",
	    srate,tmp,this);
	return NotExact;
    }
    status = m_dev->getSamplerate(tmp,false);
    if (status)
	return status;
    if (tmp != srate) {
	Debug(this,DebugNote,"Failed to set RX samplerate requested=%u read=%u [%p]",
	    srate,tmp,this);
	return NotExact;
    }
    return NoError;
}

unsigned int BrfInterface::getSampleRate(uint64_t& hz) const
{
    uint32_t srate = 0;
    unsigned int status = m_dev->getSamplerate(srate,true);
    if (status == 0)
	hz = srate;
    return status;
}

unsigned int BrfInterface::setFilter(uint64_t hz)
{
    XDebug(this,DebugAll,"BrfInterface::setFilter(" FMT64U ") [%p]",hz,this);
    if (hz > 0xffffffff) {
	Debug(this,DebugNote,
	    "Failed to set filter " FMT64U ": out of range [%p]",
	    hz,this);
	return OutOfRange;
    }
    uint32_t band = hz;
    unsigned int status = m_dev->setLpfBandwidth(band,true);
    if (status)
	return status;
    status = m_dev->setLpfBandwidth(band,false);
    if (status)
	return status;
    uint32_t tmp = 0;
    status = m_dev->getLpfBandwidth(tmp,true);
    if (status)
	return status;
    if (tmp != band){
	Debug(this,DebugNote,"Failed to set TX filter band requested=%u read=%u [%p]",
	    band,tmp,this);
	return NotExact;
    }
    status = m_dev->getLpfBandwidth(tmp,false);
    if (status)
	return status;
    if (tmp != band){
	Debug(this,DebugNote,"Failed to set RX filter band requested=%u read=%u [%p]",
	    band,tmp,this);
	return NotExact;
    }
    return NoError;
}

unsigned int BrfInterface::getFilterWidth(uint64_t& hz) const
{
    uint32_t band = 0;
    unsigned int status = m_dev->getLpfBandwidth(band,true);
    if (status == 0)
	hz = band;
    return status;
}

unsigned int BrfInterface::setRxGain(int val, unsigned port, bool preMixer)
{
    XDebug(this,DebugAll,"BrfInterface::setRxGain(%d,%u,VGA%c) [%p]",
	val,port,mixer(preMixer),this);
    if (!m_dev->validPort(port))
	return OutOfRange;
    unsigned int status = m_dev->enableRxVga(true,preMixer);
    if (status)
	return status;
    status = m_dev->setRxVga(val,preMixer);
    if (status)
	return status;
    int tmp = 0;
    status = m_dev->getRxVga(tmp,preMixer);
    if (status)
	return status;
    if (tmp == val)
	return NoError;
    Debug(this,DebugNote,"Failed to set RX VGA%c requested=%d read=%d [%p]",
	mixer(preMixer),val,tmp,this);
    return NotExact;
}

unsigned int BrfInterface::setTxGain(int val, unsigned port, bool preMixer)
{
    XDebug(this,DebugAll,"BrfInterface::setTxGain(%d,%u,VGA%c) [%p]",
	val,port,mixer(preMixer),this);
    if (!m_dev->validPort(port))
	return OutOfRange;
    unsigned int status = m_dev->setTxVga(val,preMixer);
    if (status)
	return status;
    int tmp = 0;
    status = m_dev->getTxVga(tmp,preMixer);
    if (status)
	return status;
    if (tmp == val)
	return NoError;
    Debug(this,DebugNote,"Failed to set TX VGA%c requested=%d read=%d [%p]",
	mixer(preMixer),val,tmp,this);
    return NotExact;
}

// Calibration. Automatic tx/rx gain setting
// Set pre and post mixer value
unsigned int BrfInterface::setGain(bool tx, int val, unsigned int port,
    int* newVal) const
{
    if (!m_dev->validPort(port))
	return OutOfRange;
    return m_dev->setGain(tx,val,newVal);
}

void BrfInterface::destroyed()
{
    Debug(this,DebugAll,"Destroying device=(%p) [%p]",m_dev,this);
    Lock lck(__plugin);
    __plugin.m_ifaces.remove(this,false);
    lck.drop();
    TelEngine::destruct(m_dev);
    RadioInterface::destroyed();
}


//
// BrfModule
//
BrfModule::BrfModule()
    : Module("bladerf","misc",true),
    m_ifaceId(0)
{
    String tmp;
#ifdef HAVE_LIBUSB_VER
    const libusb_version* ver = ::libusb_get_version();
    tmp.printf(" using libusb %u.%u.%u.%u",ver->major,ver->minor,ver->micro,ver->nano);
    if (!TelEngine::null(ver->rc))
	tmp << " rc='" << ver->rc << "'";
    if (!TelEngine::null(ver->describe))
	tmp << " desc='" << ver->describe << "'";
#else
    tmp = " using old libusb 1.0";
#endif
    Output("Loaded module BladeRF%s",tmp.safe());
}

BrfModule::~BrfModule()
{
    Output("Unloading module BladeRF");
    if (m_ifaces.skipNull())
	Debug(this,DebugWarn,"Exiting with %u interface(s) in list!!!",m_ifaces.count());
    else if (s_usbContextInit) {
	::libusb_exit(0);
	Debug(this,DebugAll,"Cleared libusb context");
    }
}

bool BrfModule::findIfaceByDevice(RefPointer<BrfInterface>& iface, void* dev)
{
    if (!dev)
	return false;
    Lock lck(this);
    for (ObjList* o = m_ifaces.skipNull(); o; o = o->skipNext()) {
	iface = static_cast<BrfInterface*>(o->get());
	if (iface && iface->isDevice(dev))
	    return true;
	iface = 0;
    }
    return false;
}

void BrfModule::initialize()
{
    Output("Initializing module BladeRF");
    lock();
    loadCfg();
    NamedList gen(*s_cfg.createSection(YSTRING("general")));
    NamedList lusb(*s_cfg.createSection(YSTRING("libusb")));
    unlock();
    if (!relayInstalled(RadioCreate)) {
	setup();
	installRelay(Halt);
	installRelay(Control);
	installRelay(RadioCreate,"radio.create",gen.getIntValue("priority",90));
    }
    lusbSetDebugLevel();
    s_lusbSyncTransferTout = lusb.getIntValue(YSTRING("sync_transfer_timeout"),
	LUSB_SYNC_TIMEOUT,20,500);
    s_lusbCtrlTransferTout = lusb.getIntValue(YSTRING("ctrl_transfer_timeout"),
	LUSB_CTRL_TIMEOUT,200,2000);
    s_lusbBulkTransferTout = lusb.getIntValue(YSTRING("bulk_transfer_timeout"),
	LUSB_BULK_TIMEOUT,200,2000);
    setDebugPeripheral(gen);
    setSampleEnergize(gen[YSTRING("sampleenergize")]);
    // Reload interfaces
    lock();
    if (m_ifaces.skipNull()) {
	ListIterator iter(m_ifaces);
	for (GenObject* gen = 0; (gen = iter.get()) != 0; ) {
	    RefPointer<BrfInterface> iface = static_cast<BrfInterface*>(gen);
	    if (!iface)
		continue;
	    unlock();
	    iface->reLoad();
	    iface = 0;
	    lock();
	}
    }
    unlock();
}

bool BrfModule::received(Message& msg, int id)
{
    if (id == RadioCreate) {
	// Override parameters from received params
	const String& what = msg[YSTRING("radio_driver")];
	if (what && what != YSTRING("bladerf"))
	    return false;
	BrfInterface* ifc = createIface(msg);
	if (ifc)
	    msg.setParam(new NamedPointer("interface",ifc,name()));
	else
	    msg.setParam(YSTRING("error"),"failure");
	return ifc != 0;
    }
    if (id == Control) {
	const String& comp = msg[YSTRING("component")];
	RefPointer<BrfInterface> ifc;
	if (comp == name() || findIface(ifc,comp))
	    return onCmdControl(ifc,msg);
	return false;
    }
    else if (id == Halt) {
	NamedList dummy("");
	test("stop",dummy);
    }
    return Module::received(msg,id);
}

void BrfModule::statusModule(String& str)
{
    Module::statusModule(str);
}

void BrfModule::statusParams(String& str)
{
    Module::statusParams(str);
    Lock lck(this);
    str.append("ifaces=",",") << m_ifaces.count();
}

void BrfModule::statusDetail(String& str)
{
    Module::statusDetail(str);
}

bool BrfModule::commandComplete(Message& msg, const String& partLine,
    const String& partWord)
{
    if (partLine == YSTRING("control")) {
	itemComplete(msg.retValue(),name(),partWord);
	completeIfaces(msg.retValue(),partWord);
	return false;
    }
    String tmp = partLine;
    if (tmp.startSkip("control")) {
	if (tmp == name())
	    return completeStrList(msg.retValue(),partWord,s_modCmds);
	RefPointer<BrfInterface> ifc;
	if (findIface(ifc,tmp))
	    return completeStrList(msg.retValue(),partWord,s_ifcCmds);
    }
    return Module::commandComplete(msg,partLine,partWord);
}

BrfInterface* BrfModule::createIface(const NamedList& params)
{
//    Debugger d(debugLevel(),"BrfModule::createIface()");
    Lock lck(this);
    NamedList p(*s_cfg.createSection("general"));
    // Allow using a different interface profile
    // Override general parameters
    const String& profile = params[YSTRING("profile")];
    NamedList* sect = 0;
    if (profile && profile != YSTRING("general"))
	sect = s_cfg.getSection(profile);
    if (sect) {
	for (const ObjList* o = sect->paramList()->skipNull(); o; o = o->skipNext()) {
	    const NamedString* ns = static_cast<const NamedString*>(o->get());
	    p.setParam(ns->name(),*ns);
	}
    }
    // Override parameters from received params
    String prefix = params.getValue(YSTRING("radio_params_prefix"),"radio.");
    if (prefix)
	p.copySubParams(params,prefix,true,true);
    BrfInterface* ifc = new BrfInterface(name() + "/" + String(++m_ifaceId));
    m_ifaces.append(ifc)->setDelete(false);
    BrfLibUsbDevice* dev = ifc->init();
    lck.drop();
    if (dev && dev->open(p))
	return ifc;
    TelEngine::destruct(ifc);
    return 0;
}

void BrfModule::completeIfaces(String& dest, const String& partWord)
{
    Lock lck(this);
    for (ObjList* o = m_ifaces.skipNull(); o; o = o->skipNext()) {
	RefPointer<BrfInterface> ifc = static_cast<BrfInterface*>(o->get());
	if (ifc)
	    itemComplete(dest,ifc->toString(),partWord);
    }
}

bool BrfModule::onCmdControl(BrfInterface* ifc, Message& msg)
{
    static const char* s_help =
	"\r\ncontrol ifc_name txgain1 [value=]"
	"\r\n  Set or retrieve TX VGA 1 mixer gain"
	"\r\ncontrol ifc_name txgain2 [value=]"
	"\r\n  Set or retrieve TX VGA 2 mixer gain"
	"\r\ncontrol ifc_name rxgain1 [value=]"
	"\r\n  Set or retrieve RX VGA 1 mixer gain"
	"\r\ncontrol ifc_name rxgain2 [value=]"
	"\r\n  Set or retrieve RX VGA 2 mixer gain"
	"\r\ncontrol ifc_name txdci [value=]"
	"\r\n  Set or retrieve TX DC I correction"
	"\r\ncontrol ifc_name txdcq [value=]"
	"\r\n  Set or retrieve TX DC Q correction"
	"\r\ncontrol ifc_name txfpgaphase [value=]"
	"\r\n  Set or retrieve TX FPGA PHASE correction"
	"\r\ncontrol ifc_name txfpgagain [value=]"
	"\r\n  Set or retrieve TX FPGA GAIN correction"
	"\r\ncontrol ifc_name rxdci [value=]"
	"\r\n  Set or retrieve RX DC I correction"
	"\r\ncontrol ifc_name rxdcq [value=]"
	"\r\n  Set or retrieve RX DC Q correction"
	"\r\ncontrol ifc_name rxfpgaphase [value=]"
	"\r\n  Set or retrieve RX FPGA PHASE correction"
	"\r\ncontrol ifc_name rxfpgagain [value=]"
	"\r\n  Set or retrieve RX FPGA GAIN correction"
	"\r\ncontrol ifc_name showstatus"
	"\r\n  Output interface status"
	"\r\ncontrol ifc_name showboardstatus"
	"\r\n  Output board status"
	"\r\ncontrol ifc_name showstatistics"
	"\r\n  Output interface statistics"
	"\r\ncontrol ifc_name showtimestamps"
	"\r\n  Output interface and board timestamps"
	"\r\ncontrol ifc_name showlms [addr=] [len=]"
	"\r\n  Output LMS registers"
	"\r\ncontrol ifc_name lmswrite addr= value= [resetmask=]"
	"\r\n  Set LMS value at given address. Use reset mask for partial register set"
	"\r\ncontrol ifc_name bufoutput tx=boolean [count=] [nodata=boolean]"
	"\r\n  Set TX/RX buffer output"
	"\r\ncontrol ifc_name rxdcoutput [count=]"
	"\r\n  Set interface RX DC info output"
	"\r\ncontrol ifc_name txpattern [pattern=]"
	"\r\n  Set interface TX pattern"
	"\r\ncontrol ifc_name vgagain tx=boolean vga={1|2} [gain=]"
	"\r\n  Set or retrieve TX/RX VGA mixer gain"
	"\r\ncontrol ifc_name correction tx=boolean corr={dc-i|dc-q|fpga-gain|fpga-phase} [value=]"
	"\r\n  Set or retrieve TX/RX DC I/Q or FPGA GAIN/PHASE correction"
	"\r\ncontrol ifc_name show [info=status|statistics|timestamps|boardstatus|peripheral] [peripheral=all|list(lms,gpio,vctcxo,si5338)] [addr=] [len=]"
	"\r\n  Verbose output various interface info"
	"\r\ncontrol bladerf test oper=start|stop|pause|resume|exec"
	"\r\n  Test commands"
	"\r\ncontrol bladerf help"
	"\r\n  Display control commands help";

    const String& cmd = msg[YSTRING("operation")];
    // Module commands
    if (!ifc) {
	if (cmd == YSTRING("test"))
	    return test(msg[YSTRING("oper")],msg);
	if (cmd == YSTRING("help")) {
	    msg.retValue() << s_help;
	    return true;
	}
	return false;
    }
    // Interface commands
    if (cmd == YSTRING("txgain1"))
	return onCmdGain(ifc,msg,1,true);
    if (cmd == YSTRING("txgain2"))
	return onCmdGain(ifc,msg,1,false);
    if (cmd == YSTRING("rxgain1"))
	return onCmdGain(ifc,msg,0,true);
    if (cmd == YSTRING("rxgain2"))
	return onCmdGain(ifc,msg,0,false);
    if (cmd == YSTRING("vgagain"))
	return onCmdGain(ifc,msg);
    if (cmd == YSTRING("txdci"))
	return onCmdCorrection(ifc,msg,1,BrfLibUsbDevice::CorrLmsI);
    if (cmd == YSTRING("txdcq"))
	return onCmdCorrection(ifc,msg,1,BrfLibUsbDevice::CorrLmsQ);
    if (cmd == YSTRING("txfpgaphase"))
	return onCmdCorrection(ifc,msg,1,BrfLibUsbDevice::CorrFpgaPhase);
    if (cmd == YSTRING("txfpgagain"))
	return onCmdCorrection(ifc,msg,1,BrfLibUsbDevice::CorrFpgaGain);
    if (cmd == YSTRING("rxdci"))
	return onCmdCorrection(ifc,msg,0,BrfLibUsbDevice::CorrLmsI);
    if (cmd == YSTRING("rxdcq"))
	return onCmdCorrection(ifc,msg,0,BrfLibUsbDevice::CorrLmsQ);
    if (cmd == YSTRING("rxfpgaphase"))
	return onCmdCorrection(ifc,msg,0,BrfLibUsbDevice::CorrFpgaPhase);
    if (cmd == YSTRING("rxfpgagain"))
	return onCmdCorrection(ifc,msg,0,BrfLibUsbDevice::CorrFpgaGain);
    if (cmd == YSTRING("correction"))
	return onCmdCorrection(ifc,msg);
    if (cmd == YSTRING("lmswrite"))
	return onCmdLmsWrite(ifc,msg);
    if (cmd == YSTRING("bufoutput"))
	return onCmdBufOutput(ifc,msg);
    if (cmd == YSTRING("rxdcoutput")) {
	if (!ifc->device())
	    return retMsgError(msg,"No device");
	ifc->device()->showRxDCInfo(msg.getIntValue(YSTRING("count")));
	return true;
    }
    if (cmd == YSTRING("txpattern")) {
	if (!ifc->device())
	    return retMsgError(msg,"No device");
	ifc->device()->setTxPattern(msg[YSTRING("pattern")]);
	return true;
    }
    if (cmd == YSTRING("showstatus"))
	return onCmdShow(ifc,msg,YSTRING("status"));
    if (cmd == YSTRING("showboardstatus"))
	return onCmdShow(ifc,msg,YSTRING("boardstatus"));
    if (cmd == YSTRING("showstatistics"))
	return onCmdShow(ifc,msg,YSTRING("statistics"));
    if (cmd == YSTRING("showtimestamps"))
	return onCmdShow(ifc,msg,YSTRING("timestamps"));
    if (cmd == YSTRING("showlms"))
	return onCmdShow(ifc,msg,YSTRING("lms"));
    if (cmd == YSTRING("show"))
	return onCmdShow(ifc,msg);
    return false;
}

bool BrfModule::onCmdStatus(String& retVal, String& line)
{
    static const String s_devInfo("withdevinfo");
    String ifcName;
    bool devInfo = false;
    if (line && getFirstStr(ifcName,line)) {
	if (ifcName == s_devInfo) {
	    devInfo = true;
	    ifcName.clear();
	}
	else if (line) {
	    String tmp;
	    devInfo = getFirstStr(tmp,line) && (tmp == s_devInfo);
	}
    }
    String extra;
    String stats;
    String info;
    if (ifcName) {
	stats << "interface=" << ifcName;
	RefPointer<BrfInterface> ifc;
	if (findIface(ifc,ifcName) && ifc->device());
	    ifc->device()->dumpDev(info,devInfo,true,",",true,true);
    }
    else {
	unsigned int n = 0;
	lock();
	ListIterator iter(m_ifaces);
	for (GenObject* gen = 0; (gen = iter.get()) != 0; ) {
	    RefPointer<BrfInterface> ifc = static_cast<BrfInterface*>(gen);
	    if (!ifc)
		continue;
	    unlock();
	    n++;
	    BrfLibUsbDevice* dev = ifc->device();
	    if (dev) {
		String tmp;
		dev->dumpDev(tmp,devInfo,true,",",true,false);
		info.append(ifc->toString(),",") << "=" << tmp;
	    }
	    lock();
	}
	unlock();
	extra << "format=RxVGA1|RxVGA2|RxDCCorrI|RxDCCorrQ|TxVGA1|TxVGA2|"
	    "RxFreq|TxFreq|RxSampRate|TxSampRate|RxLpfBw|TxLpfBw|RxRF|TxRF";
	if (devInfo)
	    extra << "|Address|Serial|Speed|Firmware|FPGA";
	stats << "count=" << n;
    }
    retVal << "module=" << name();
    retVal.append(extra,",") << ";";
    if (stats)
	retVal << stats << ";";
    retVal << info;
    retVal << "\r\n";
    return true;
}

#define BRF_GET_BOOL_PARAM(bDest,param) \
    const String& tmpGetBParamStr = msg[YSTRING(param)]; \
    if (!tmpGetBParamStr.isBoolean()) \
	return retParamError(msg,param); \
    bDest = tmpGetBParamStr.toBoolean();

// control ifc_name vgagain tx=boolean mixer={1|2} [value=]
// control ifc_name {tx|rx}vga{1|2}gain [value=]
bool BrfModule::onCmdGain(BrfInterface* ifc, Message& msg, int tx, bool preMixer)
{
    if (!ifc->device())
	return retMsgError(msg,"No device");
    const String* value = 0;
    if (tx >= 0)
	value = msg.getParam(YSTRING("value"));
    else {
	bool tmpTx = true;
	BRF_GET_BOOL_PARAM(tmpTx,"tx");
	const String& what = msg[YSTRING("vga")];
	preMixer = (what == YSTRING("1"));
	if (!preMixer && what != YSTRING("2"))
	    return retParamError(msg,"vga");
	tx = tmpTx ? 1 : 0;
	value = msg.getParam(YSTRING("gain"));
    }
    unsigned int code = 0;
    int crt = 0;
    if (!TelEngine::null(value))
	code = tx ? ifc->device()->setTxVga(value->toInteger(),preMixer) :
	    ifc->device()->setRxVga(value->toInteger(),preMixer);
    if (!code)
   	code = tx ? ifc->device()->getTxVga(crt,preMixer) :
	    ifc->device()->getRxVga(crt,preMixer);
    if (code)
	return retValFailure(msg,code);
    msg.setParam(YSTRING("value"),String(crt));
    msg.retValue() = crt;
    return true;
}

// control ifc_name correction tx=boolean corr={dc-i/dc-q/fpga-gain/fpga-phase} [value=]
bool BrfModule::onCmdCorrection(BrfInterface* ifc, Message& msg, int tx, int corr)
{
    if (!ifc->device())
	return retMsgError(msg,"No device");
    if (tx < 0) {
	bool tmpTx = true;
	BRF_GET_BOOL_PARAM(tmpTx,"tx");
	const String& corrStr = msg[YSTRING("corr")];
	if (corrStr == YSTRING("dc-i"))
	    corr = BrfLibUsbDevice::CorrLmsI;
	else if (corrStr == YSTRING("dc-q"))
	    corr = BrfLibUsbDevice::CorrLmsQ;
	else if (corrStr == YSTRING("fpga-phase"))
	    corr = BrfLibUsbDevice::CorrFpgaPhase;
	else if (corrStr == YSTRING("fpga-gain"))
	    corr = BrfLibUsbDevice::CorrFpgaGain;
	else
	    return retParamError(msg,"corr");
	tx = tmpTx ? 1 : 0;
    }
    const String& value = msg[YSTRING("value")];
    unsigned int code = 0;
    int16_t crt = 0;
    if (corr == BrfLibUsbDevice::CorrLmsI || corr == BrfLibUsbDevice::CorrLmsQ) {
	bool i = (corr == BrfLibUsbDevice::CorrLmsI);
	if (value)
	    code = ifc->device()->setDcOffset(tx,i,value.toInteger());
	if (!code)
	    code = ifc->device()->getDcOffset(tx,i,crt);
    }
    else {
	if (value)
	    code = ifc->device()->setFpgaCorr(tx,corr,value.toInteger());
	if (!code)
	    code = ifc->device()->getFpgaCorr(tx,corr,crt);
    }
    if (code)
	return retValFailure(msg,code);
    msg.setParam(YSTRING("value"),String(crt));
    msg.retValue() = crt;
    return true;
}

// control ifc_name lmswrite addr= value= [resetmask=]
bool BrfModule::onCmdLmsWrite(BrfInterface* ifc, Message& msg)
{
    if (!ifc->device())
	return retMsgError(msg,"No device");
    int addr = msg.getIntValue(YSTRING("addr"),-1);
    if (addr < 0 || addr > 127)
	return retParamError(msg,"addr");
    int val = msg.getIntValue(YSTRING("value"),-1);
    if (val < 0 || val > 255)
	return retParamError(msg,"value");
    const String& rstStr = msg[YSTRING("resetmask")];
    unsigned int code = 0;
    if (rstStr) {
	uint8_t rst = (uint8_t)rstStr.toInteger();
	code = ifc->device()->writeLMS(addr,val,&rst);
    }
    else
	code = ifc->device()->writeLMS(addr,val);
    if (!code)
	return true;
    return retValFailure(msg,code);
}

// control ifc_name bufoutput tx=boolean [count=value] [nodata=false]
bool BrfModule::onCmdBufOutput(BrfInterface* ifc, Message& msg)
{
    if (!ifc->device())
	return retMsgError(msg,"No device");
    bool tx = true;
    BRF_GET_BOOL_PARAM(tx,"tx");
    ifc->device()->showBuf(tx,msg.getIntValue(YSTRING("count")),
	msg.getBoolValue(YSTRING("nodata")));
    return true;
}

// control ifc_name show [info=status|statistics|timestamps|boardstatus|peripheral] [peripheral=all|list(lms,gpio,vctcxo,si5338)] [addr=] [len=]
bool BrfModule::onCmdShow(BrfInterface* ifc, Message& msg, const String& what)
{
    if (!ifc->device())
	return retMsgError(msg,"No device");
    String info;
    if (what)
	info = what;
    else
	info = msg.getValue(YSTRING("info"),"status");
    String str;
    if (info == YSTRING("status"))
	ifc->device()->dumpDev(str,true,true,"\r\n");
    else if (info == YSTRING("boardstatus"))
	ifc->device()->dumpBoardStatus(str,"\r\n");
    else if (info == YSTRING("statistics"))
	ifc->device()->dumpStats(str,"\r\n");
    else if (info == YSTRING("timestamps"))
	ifc->device()->dumpTimestamps(str,"\r\n");
    else if (info == YSTRING("peripheral") || info == YSTRING("lms")) {
	String peripheralList;
	if (what)
	    peripheralList = what;
	else {
	    peripheralList = msg.getValue(YSTRING("peripheral"),"all");
	    if (peripheralList == YSTRING("all"))
		peripheralList = "lms,gpio,vctcxo,si5338";
	}
	uint8_t addr = (uint8_t)msg.getIntValue("addr",0,0);
	uint8_t len = (uint8_t)msg.getIntValue("len",128,1);
	ObjList* lst = peripheralList.split(',');
	for (ObjList* o = lst->skipNull(); o; o = o->skipNext()) {
	    String* s = static_cast<String*>(o->get());
	    s->toUpper();
	    for (uint8_t i = 0; i < BrfLibUsbDevice::UartDevCount; i++) {
		if (*s != s_uartDev[i])
		    continue;
		String tmp;
		ifc->device()->dumpPeripheral(i,addr,len,&tmp);
		if (tmp)
		    str.append(s_uartDev[i],"\r\n") << tmp;
		break;
	    }
	}
	TelEngine::destruct(lst);
    }
    else
	return retParamError(msg,"info");
    if (str) {
	char buf[50];
	Debugger::formatTime(buf);
	Output("Interface '%s' info=%s time=%s [%p]%s",
	    ifc->debugName(),info.c_str(),buf,ifc,encloseDashes(str,true));
    }
    return true;
}

// control module_name test oper={start|stop|.....} params...
bool BrfModule::test(const String& cmd, NamedList& list)
{
    static bool s_exec = false;

    lock();
    while (s_exec) {
	unlock();
	Thread::idle();
	if (Thread::check(false))
	    return false;
	lock();
    }
    s_exec = true;
    RefPointer<BrfTest> crt = m_test;
    unlock();
    bool ok = true;
    bool start = (cmd == YSTRING("start"));
    if (start || cmd == YSTRING("stop")) {
	const String& name = start ? list[YSTRING("name")] : String::empty();
	if (start && !name) {
	    s_exec = false;
	    return retParamError(list,"name");
	}
	lock();
	m_test = 0;
	unlock();
	// Start / Stop
	bool haveTest = (crt != 0);
	if (crt) {
	    crt->stop();
	    crt = 0;
	}
	if (start && !Thread::check(false)) {
	    Lock lck(this);
	    // Reload config
	    loadCfg();
	    NamedList* sect = s_cfg.getSection(name);
	    ok = (sect != 0);
	    if (ok) {
		NamedList p(*s_cfg.createSection(sect->getValue("init_section",*sect)));
		NamedList cmds(*s_cfg.createSection(sect->getValue("cmds_section",*sect)));
		NamedList devOpen(*s_cfg.createSection(sect->getValue("dev_section","general")));
		BrfTest* ifc = new BrfTest(this->name() + "/" + name,p,devOpen,cmds);
		m_ifaces.append(ifc)->setDelete(false);
		BrfLibUsbDevice* dev = ifc->init();
		lck.drop();
		ok = dev && ifc->start();
		if (ok)
		    setTest(true,ifc);
		TelEngine::destruct(ifc);
		Debug(this,ok ? DebugInfo : DebugNote,"Test '%s' %s",
		    name.c_str(),(ok ? "started" : "failed to start"));
	    }
	    else
		Debug(this,DebugConf,"Can't test '%s': no config section",name.c_str());
	}
	else if (!start && haveTest)
	    Debug(this,DebugInfo,"Test stopped");
    }
    else {
	String error;
	if (crt) {
	    if (cmd == YSTRING("pause"))
		crt->pause();
	    else if (cmd == YSTRING("resume"))
		crt->resume();
	    else if (cmd == YSTRING("exec")) {
		const String& c = list[YSTRING("command")];
		if (c)
		    ok = crt->execute(c,list[YSTRING("value")],error,true);
		else {
		    ok = false;
		    error = "Empty command";
		}
	    }
	    else {
		ok = false;
		error = "Unknown test command";
	    }
	}
	else
	    ok = false;
	if (error)
	    list.setParam("error",error);
    }
    s_exec = false;
    return ok;
}

void BrfModule::setDebugPeripheral(const NamedList& params)
{
    for (uint8_t i = 0; i < BrfLibUsbDevice::UartDevCount; i++) {
	BrfPeripheral& p = s_uartDev[i];
	const String& tmp = params[p.lowCase + "_debug"];
	bool tx = false;
	bool rx = false;
	if (tmp) {
	    if (tmp == YSTRING("tx"))
		tx = true;
	    else if (tmp == YSTRING("rx"))
		rx = true;
	    else if (tmp == YSTRING("both"))
		tx = rx = true;
	}
	p.set(tx,rx,params[p.lowCase + "_trackaddr"]);
    }
}

void BrfModule::setSampleEnergize(const String& value)
{
    Lock lck(this);
    int val = value.toInteger(2047);
    if (val == (int)s_sampleEnergize)
	return;
    if (val < 1 || val > 2047) {
	Debug(this,DebugConf,"Invalid sampleenergize=%s",value.c_str());
	return;
    }
    Debug(this,DebugInfo,"sampleenergize changed %u -> %d",s_sampleEnergize,val);
    s_sampleEnergize = val;
    // Notify devices
    ListIterator iter(m_ifaces);
    for (GenObject* gen = 0; (gen = iter.get()) != 0; ) {
	RefPointer<BrfInterface> ifc = static_cast<BrfInterface*>(gen);
	if (!ifc)
	    continue;
	lck.drop();
	// Just set flags used by the device to update data using sample energize value
	if (ifc->device()) {
	    ifc->device()->m_txPowerBalanceChanged = true;
	    ifc->device()->m_txPatternChanged = true;
	}
	ifc = 0;
	lck.acquire(this);
    }
}


//
// BrfThread
//
// Start this thread. Set destination pointer on success. Delete object on failure
BrfThread* BrfThread::start()
{
    if (startup())
	return this;
    Debug(m_iface,DebugNote,"Failed to start worker '%s' [%p]",name(),(void*)m_iface);
    delete this;
    return 0;
}

void BrfThread::cancelThread(BrfThread*& th, Mutex* mtx, unsigned int waitMs,
    DebugEnabler* dbg, void* ptr)
{
    if (!th)
	return;
    Lock lck(mtx);
    if (!th)
	return;
    //Debugger d(DebugAll,"BrfThread::cancelThread()"," [%p]",ptr);
    th->cancel();
    lck.drop();
    unsigned int intervals = (waitMs / Thread::idleMsec()) + 1;
    bool cancelled = Thread::check(false);
    while (th && intervals-- && (cancelled || !Thread::check(false)))
	Thread::idle();
    if (!th)
	return;
    lck.acquire(mtx);
    if (!th)
	return;
    Debug(dbg,DebugWarn,"Hard cancelling (%p) '%s' worker [%p]",th,th->name(),ptr);
    th->cancel(true);
    th = 0;
}

void BrfThread::run()
{
    BrfInterface* i = m_iface ? (BrfInterface*)m_iface :
	(m_device ? m_device->owner() : 0);
    if (!i)
	return;
    Debug(i,DebugAll,"Worker (%p) '%s' started [%p]",this,name(),i);
    BrfTest* test = YOBJECT(BrfTest,m_iface);
    if (test)
	test->run();
    else if (m_device) {
	unsigned int samples = m_device->totalSamples(true);
	DataBlock buf(0,samplesf2bytes(samples));
	float* b = (float*)buf.data(0);
	uint64_t ts = 0;
	m_device->internalGetTimestamp(true,ts);
	Debug(m_device->owner(),DebugAll,
	    "Start sending at ts=" FMT64U " chunk=%u samples [%p]",
	    ts,samples,m_device->owner());
	unsigned int status = 0;
	while (!status) {
	    BRF_FUNC_CALL_BREAK(m_device->send(ts,b,samples));
	    BRF_FUNC_CALL_BREAK(m_device->cancelled());
	    ts += samples;
	}
    }
    notify();
}

void BrfThread::notify()
{
    RefPointer<BrfInterface> ifc = m_iface;
    m_iface = 0;
    BrfLibUsbDevice* dev = m_device;
    m_device = 0;
    if (!(ifc || dev))
	return;
    BrfInterface* i = ifc ? (BrfInterface*)ifc : dev->owner();
    bool ok = (m_name == Thread::currentName());
    Debug(i,ok ? DebugAll : DebugWarn,"Worker (%p) '%s' terminated [%p]",this,name(),i);
    BrfTest* test = YOBJECT(BrfTest,ifc);
    if (test) {
	if (test->workerTerminated(this))
	    test->stop();
    }
    else if (dev) {
	Lock lck(dev->m_sendThreadMutex);
	if (dev->m_sendThread == this)
	    dev->m_sendThread = 0;
    }
    ifc = 0;
}


//
// BrfTest
//
BrfTest::BrfTest(const char* name, const NamedList& params, const NamedList& devOpen,
    const NamedList& cmds)
    : BrfInterface(name), Mutex(false,"BrfTest"),
    m_state(Idle),
    m_pause(false),
    m_pauseSend(false),
    m_pauseRead(false),
    m_sendBufCount(0),
    m_sendOnly(false),
    m_worker(0),
    m_sentSamples(0),
    m_sendTs(0),
    m_sendBuf(0),
    m_sendBufSamples(0),
    m_readTs(0),
    m_skippedBuffs(0),
    m_params(params),
    m_devOpen(devOpen),
    m_cmds(cmds)
{
}

bool BrfTest::start()
{
    Lock lck(this);
    if (m_state == Running)
	return true;
    if (m_state != Idle)
	return false;
    Debug(this,DebugInfo,"Starting ... [%p]",this);
    String e;
    while (true) {
	m_worker = (new BrfThread(this,"BrfTest"))->start();
	if (!m_worker)
	    e = "Failed to start worker(s)";
	break;
    }
    if (e)
	Debug(this,DebugNote,"Start failure: %s [%p]",e.c_str(),this);
    else
	m_state = Running;
    return m_state == Running;
}

void BrfTest::stop()
{
    if (device())
	device()->exiting(true);
    __plugin.setTest(false,this);
    if (m_state == Stopping || m_state == Idle)
	return;
    Lock lck(this);
    if (m_state == Stopping || m_state == Idle)
	return;
    m_state = Stopping;
    Debug(this,DebugInfo,"Stopping ... [%p]",this);
    lck.drop();
    BrfThread::cancelThread(m_worker,this,1000,this,this);
    lck.acquire(this);
    m_state = Idle;
    Debug(this,DebugInfo,"Stopped [%p]",this);
}

bool BrfTest::execute(const String& cmd, const String& param, String& error,
    bool fatal, const NamedList* params)
{
    XDebug(this,DebugAll,"execute(%s,%s) [%p]",cmd.c_str(),param.c_str(),this);
    String e;
    unsigned int c = RadioInterface::Failure;
    if (cmd == YSTRING("loopback"))
	c = device()->setLoopback(param);
    else if (cmd == YSTRING("samplerate"))
	c = setSampleRate(param.toInteger());
    else if (cmd == YSTRING("filter"))
	c = setFilter(param.toInteger());
    else if (cmd == YSTRING("txfrequency"))
	c = setFrequency(param.toInteger(),true);
    else if (cmd == YSTRING("rxfrequency"))
	c = setFrequency(param.toInteger(),false);
    else if (cmd == YSTRING("calibrate"))
	c = device()->calibrate();
    else if (cmd == YSTRING("powerbalance")) {
	if (param)
	    c = device()->setTxIQBalance((float)param.toDouble(-2));
	else
	    e = "Missing required parameter";
    }
    else if (cmd == YSTRING("txpattern"))
	c = device()->setTxPattern(param);
    else {
	Debug(this,DebugNote,"Unhandled command '%s' [%p]",cmd.c_str(),this);
	return true;
    }
    if (c == 0 || !fatal)
	return true;
    error.printf("'%s' failed with %u '%s'",cmd.c_str(),c,errorName(c));
    error.append(e," - ");
    return false;
}

bool BrfTest::runInit()
{
    String e;
    int level = DebugNote;
    while (true) {
	m_sendBufCount = m_params.getIntValue("send_count",0,0);
	m_sendOnly = m_params.getBoolValue("send_only",true);
	// Open device
	if (!device()->open(m_devOpen)) {
	    e = "Device open failed";
	    break;
	}
	if (!execute(m_cmds,"init:",e))
	    break;
	if (initialize()) {
	    e = "Initialize failure";
	    break;
	}
	if (!execute(m_cmds,"cmd:",e))
	    break;
	m_sendBufSamples = m_params.getIntValue("send_samples",0,0,5000);
	if (!m_sendBufSamples)
	    m_sendBufSamples = device()->bufSamples(true);
	if (!m_sendBufSamples) {
	    e = "Send buf samples is 0";
	    break;
	}
	m_sendBufData.resize(samplesf2bytes(m_sendBufSamples));
	m_sendBuf = (float*)m_sendBufData.data(0);
	break;
    }
    if (e)
	Debug(this,level,"Init failure: %s [%p]",e.c_str(),this);
    return e.null();
}

bool BrfTest::execute(const NamedList& cmds, const char* prefix, String& error)
{
    for (const ObjList* o = cmds.paramList()->skipNull(); o; o = o->skipNext()) {
	const NamedString* ns = static_cast<const NamedString*>(o->get());
	String s = ns->name();
	if (s.startSkip(prefix,false) &&
	    !execute(s,*ns,error,cmds.getBoolValue(s + "_fatal",true),&cmds))
	    return false;
    }
    return true;
}

void BrfTest::run()
{
    Debug(this,DebugInfo,"Running [%p]",this);
    if (!runInit())
	return;
    if (m_sendOnly)
	runSendOnly();
    else
	runSendRecv();
}

void BrfTest::runSendOnly()
{
    Debug(this,DebugInfo,"Running send only test (send %u samples) [%p]",
	m_sendBufSamples,this);
    m_sendTs = 0;
    m_sentSamples = 0;
    while (!Thread::check(false) && write())
	;
    dumpStats();
}

void BrfTest::runSendRecv()
{
    String s;
    s << "\r\nsend_count=" << m_sendBufCount;
    Debug(this,DebugInfo,"Running send/recv test [%p]%s",this,encloseDashes(s));
    m_sentSamples = 0;
    // Set RX buf
    // Read multiple of device RX samples, at least sent samples
    unsigned int rxSamples = device()->bufSamples(false);
    if (rxSamples < m_sendBufSamples) {
	unsigned int rest = m_sendBufSamples % rxSamples;
	rxSamples = m_sendBufSamples + rxSamples - rest;
    }
    resetBufs(rxSamples);
    while (!Thread::check(false) && write() && read()) {
	if (m_sendBufCount) {
	    m_sendBufCount--;
	    if (!m_sendBufCount)
		break;
	}
    }
    dumpStats();
}

void BrfTest::dumpStats()
{
    String s;
    s << "\r\nsent=" << m_sentSamples << " samples";
    Debug(this,DebugInfo,"Terminated [%p]%s",this,encloseDashes(s));
}

bool BrfTest::workerTerminated(BrfThread* th)
{
    Lock lck(this);
    if (m_worker == th) {
	m_worker = 0;
	return true;
    }
    return false;
}

bool BrfTest::checkPause(bool tx)
{
    bool& paused = tx ? m_pauseSend : m_pauseRead;
    if (m_pause) {
	if (!paused) {
	    paused = true;
	    Debug(this,DebugInfo,"%s paused [%p]",brfDir(tx),this);
	}
	Thread::idle();
	return false;
    }
    if (paused) {
	paused = false;
	Debug(this,DebugInfo,"%s resumed [%p]",brfDir(tx),this);
    }
    return true;
}

bool BrfTest::write()
{
    if (!checkPause(true)) {
	if (device()->cancelled())
	    return false;
	return true;
    }
    if (!m_sendTs)
	updateTs(true);
    unsigned int code = device()->syncTx(m_sendTs,m_sendBuf,m_sendBufSamples);
    if (!code)
	code = device()->cancelled();
    if (!code) {
	m_sendTs += m_sendBufSamples;
	m_sentSamples += m_sendBufSamples;
    }
    else if (code != Cancelled)
	Debug(this,DebugNote,"Send error: %u '%s' [%p]",code,errorName(code),this);
    return code == 0;
}

bool BrfTest::read()
{
    if (!checkPause(false)) {
	if (device()->cancelled())
	    return false;
	return true;
    }
    if (!m_readTs)
	updateTs(false);
    m_skippedBuffs = 0;
    unsigned int code = RadioInterface::read(m_readTs,m_bufs,m_skippedBuffs);
    if (!code)
	code = device()->cancelled();
    if (code && code != Cancelled)
	Debug(this,DebugNote,"Device read error: %u '%s' [%p]",code,errorName(code),this);
    return code == 0;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet enc=utf-8: */
