/**
 * ybladerf.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * BladeRF radio interface
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2015, 2016 Null Team
 * Copyright (C) 2015, 2016 LEGBA Inc
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
#include <yatemath.h>
#include <libusb-1.0/libusb.h>

#ifdef __MMX__
#include <mmintrin.h>
#endif

#ifndef M_PI_2
#define M_PI_2 (M_PI / 2)
#endif
#ifndef M_PI_4
#define M_PI_4 (M_PI / 4)
#endif

#ifdef LITTLE_ENDIAN
#define BRF_LITTLE_ENDIAN (true)
#else
#define BRF_LITTLE_ENDIAN (false)
#endif

#define BRF_MAX_FLOAT ((float)0xffffffff)

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
#define BRF_SAMPLERATE_MAX 40000000 // Max supported by LMS6002D

#define MAX_SAMPLERATE_HIGH  4100000
#define MAX_SAMPLERATE_SUPER 40000000

// Frequency bounds
#define BRF_FREQUENCY_MIN 232500000u
#define BRF_FREQUENCY_MAX 3800000000u

// Frequency offset interval
#define BRF_FREQ_OFFS_DEF 128.0
#define BRF_FREQ_OFFS_MIN 64.0
#define BRF_FREQ_OFFS_MAX 192.0

#define BRF_MAX_DELAY_SUPER_SPEED_DEF   550
#define BRF_MAX_DELAY_HIGH_SPEED_DEF    750
#define BRF_BEST_DELAY_SUPER_SPEED_DEF  450
#define BRF_BEST_DELAY_HIGH_SPEED_DEF   600
#define BRF_KNOWN_DELAY_SUPER_SPEED_DEF 400
#define BRF_KNOWN_DELAY_HIGH_SPEED_DEF  500
#define BRF_SYSTEM_ACCURACY_DEF         300
#define BRF_ACCURACY_PPB_DEF            30

#define BRF_RXVGA1_GAIN_MIN     5
#define BRF_RXVGA1_GAIN_MAX     30
#define BRF_RXVGA1_GAIN_DEF     30
#define BRF_RXVGA2_GAIN_MIN     0
#define BRF_RXVGA2_GAIN_MAX     30
#define BRF_RXVGA2_GAIN_DEF     3
#define BRF_TXVGA1_GAIN_MIN     -35
#define BRF_TXVGA1_GAIN_MAX     -4
#define BRF_TXVGA1_GAIN_DEF     -14
#define BRF_TXVGA2_GAIN_MIN     0
#define BRF_TXVGA2_GAIN_MAX     25
#define BRF_TXVGA2_GAIN_DEF     0

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

static inline const char* dirStr(int8_t dir)
{
    return dir ? (dir > 0 ? "u" : "d") : "=";
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

static inline float getSampleLimit(const NamedList& p, double defVal = (double)2040 / 2047)
{
    float limit = (float)p.getDoubleValue(YSTRING("sample_limit"),defVal);
    return (limit < 0) ? -limit : ((limit <= 1.0F) ? limit : 1.0F);
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

static inline void getInterval(const String& s, int& iMin, int& iMax,
    int minDef = INT_MIN, int maxDef = INT_MAX)
{
    int pos = s.find('_');
    if (pos >= 0) {
	iMin = s.substr(0,pos).toInteger(minDef);
	iMax = s.substr(pos + 1).toInteger(maxDef);
    }
    else {
	iMin = s.toInteger(minDef);
	iMax = maxDef;
    }
    if (iMin > iMax)
	iMin = iMax;
}

static inline bool isInterval(int val, int iMin, int iMax, const String& interval)
{
    if (interval)
	getInterval(interval,iMin,iMax,iMin,iMax);
    return (iMin <= val) && (val <= iMax);
}

static inline String& addIntervalInt(String& s, int minVal, int maxVal, const char* sep = " ")
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
#define BRF_FILTER_BW_COUNT 16
#define BRF_FILTER_BW_MIN 1500000u
#define BRF_FILTER_BW_MAX 28000000u
static const uint32_t s_bandSet[BRF_FILTER_BW_COUNT] = {
    BRF_FILTER_BW_MIN, 1750000u, 2500000u, 2750000u, 3000000u,
    3840000u,          5000000u, 5500000u, 6000000u, 7000000u,
    8750000u,          10000000u,12000000u,14000000u,20000000u,
    BRF_FILTER_BW_MAX
};
static inline uint8_t bw2index(unsigned int value)
{
    uint8_t i = 0;
    for (; i < (BRF_FILTER_BW_COUNT - 1) && value > s_bandSet[i]; i++)
	;
    return i;
}
static inline unsigned int index2bw(uint8_t index)
{
    return index < BRF_FILTER_BW_COUNT ? s_bandSet[index] : BRF_FILTER_BW_MAX;
}

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

static unsigned int threadIdleIntervals(unsigned int ms)
{
    return 1 + ms / Thread::idleMsec();
}

static inline bool validFloatSample(float val)
{
    return (val >= -1.0F) && (val <= 1.0F);
}

static inline void setMinMax(float& minF, float& maxF, float val)
{
    if (maxF < val)
	maxF = val;
    if (minF > val)
	minF = val;
}

static unsigned int checkSampleLimit(const float* buf, unsigned int samples, float limit,
    String* error)
{
    unsigned int n = 2 * samples;
    for (unsigned int i = 0; i < n; ++i, ++buf)
	if (*buf < -limit || *buf > limit) {
	    if (error)
		error->printf("sample %c %f (at %u) out of range limit=%f",
		    brfIQ((i % 2) == 0),*buf,i / 2,limit);
	    return RadioInterface::Saturation;
	}
    return 0;
}

// Generate ComplexVector tone (exponential)
static void generateExpTone(ComplexVector& v, float omega, unsigned int len = 0)
{
    if (len)
	v.resetStorage(len);
    for (unsigned int i = 0; i < v.length(); ++i) {
	Complex c(0,i * omega);
	v[i] = c.exp();
    }
}

static String& replaceDumpParams(String& buf, NamedString* ns,
    bool addRunParams = false, NamedString* ns1 = 0, NamedString* ns2 = 0)
{
    NamedList p("");
    p.addParam("newline","\r\n");
    p.addParam("tab","\t");
    if (ns)
	p.addParam(ns);
    p.addParam("sec_now",String(Time::secNow()));
    char c[256];
    Debugger::formatTime(c,Debugger::TextSep);
    p.addParam("time",c);
    if (addRunParams)
	p.copyParams(Engine::runParams());
    if (ns1)
	p.addParam(ns1);
    if (ns2)
	p.addParam(ns2);
    p.replaceParams(buf);
    return buf;
}

// Allocate a new String. Replace params from format, return the new string
static inline String* replaceDumpParamsFmt(const String& fmt, NamedString* ns,
    bool addRunParams = false, NamedString* ns1 = 0, NamedString* ns2 = 0)
{
    String* s = new String(fmt);
    replaceDumpParams(*s,ns,addRunParams,ns1,ns2);
    return s;
}

// Dump Complex vector to a NamedString
static inline NamedString* dumpNsData(const ComplexVector& v, const char* name = "data")
{
    NamedString* ns = new NamedString(name);
    v.dump(*ns,Math::dumpComplex," ","%f%+fj");
    return ns;
}

// Dump float vector to a NamedString
static inline NamedString* dumpNsData(const FloatVector& v, const char* name = "data")
{
    NamedString* ns = new NamedString(name);
    v.dump(*ns,Math::dumpFloat,",","%f");
    return ns;
}

static inline bool boolSetError(String& s, const char* e = 0)
{
    s = e;
    return false;
}

// Parse a comma separated list of float values to complex vector
static bool parseVector(String& error, const String& str, ComplexVector& buf)
{
    if (!str)
	return boolSetError(error,"empty");
    ObjList* list = str.split(',');
    unsigned int len = list->length();
    if ((len < 2) || (len % 2) != 0) {
	TelEngine::destruct(list);
	return boolSetError(error,"invalid length");
    }
    buf.resetStorage(len / 2);
    ObjList* o = list;
    for (float* b = (float*)buf.data(); o; o = o->next(), b++) {
	if (!o->get())
	    continue;
	*b = (static_cast<String*>(o->get()))->toDouble();
	if (!validFloatSample(*b))
	    break;
    }
    TelEngine::destruct(list);
    if (!o)
	return true;
    buf.resetStorage(0);
    return boolSetError(error,"invalid data range");
}

static inline void generateCircleQuarter(Complex*& c, float amplitude, float i, float q,
    unsigned int loops, float angle, float iSign, float qSign)
{
    (c++)->set(i * amplitude,q * amplitude);
    if (!loops)
	return;
    float angleStep = M_PI_2 / (loops + 1);
    if (angle)
	angleStep = -angleStep;
    iSign *= amplitude;
    qSign *= amplitude;
    for (; loops; --loops, ++c) {
	angle += angleStep;
	c->set(iSign * ::cosf(angle),qSign * ::sinf(angle));
    }
}

// Parse a complex numbers pattern
// forcePeriodic=true: Force lenExtend=false and lenRequired=true for periodic
//                     patterns (like 'circle')
// lenExtend=true: Extend destination buffer to be minimum 'len'. 'lenRequired' is ignored
// lenRequired=true: 'len' MUST be a multiple of generated vector's length
static bool buildVector(String& error, const String& pattern, ComplexVector& vector,
    unsigned int len = 0, bool forcePeriodic = true, bool lenExtend = true,
    bool lenRequired = false, unsigned int* pLen = 0, float gain=1)
{
    if (!pattern)
	return boolSetError(error,"empty");
    bool isPeriodic = false;
    String p = pattern;
    ComplexVector v;
    // Check for circles
    if (p.startSkip("circle",false)) {
	unsigned int cLen = 4;
	bool rev = false;
	float div = 1;
	if (!p || p == YSTRING("_reverse"))
	    // circle[_reverse]
	    rev = !p.null();
	else if (p.startSkip("_div_",false)) {
	    // circle_div[_reverse]_{divisor}
	    rev = p.startSkip("reverse_",false);
	    if (!p)
		return boolSetError(error);
	    div = p.toDouble();
	}
	else if (p.startSkip("_points_",false)) {
	    // circle_points[_reverse]_{value}[_div_{divisor}]
	    rev = p.startSkip("reverse_",false);
	    if (!p)
		return boolSetError(error);
	    int pos = p.find('_');
	    if (pos < 0)
		cLen = p.toInteger(0,0,0);
	    else {
		// Expecting div
		cLen = p.substr(0,pos).toInteger(0,0,0);
		p = p.substr(pos + 1);
		if (!(p.startSkip("div_",false) && p))
		    return boolSetError(error);
		div = p.toDouble();
	    }
	}
	else
	    return boolSetError(error);
	// Circle length MUST be a multiple of 4
	if (!cLen || (cLen % 4) != 0)
	    return boolSetError(error,"invalid circle length");
	if (div < 1)
	    return boolSetError(error,"invalid circle div");
	v.resetStorage(cLen);
	Complex* c = v.data();
	float amplitude = gain / div;
	float direction = rev ? -1 : 1;
	unsigned int n = (cLen - 4) / 4;
	generateCircleQuarter(c,amplitude,1,0,n,0,1,direction);
	generateCircleQuarter(c,amplitude,0,direction,n,M_PI_2,-1,direction);
	generateCircleQuarter(c,amplitude,-1,0,n,0,-1,-direction);
	generateCircleQuarter(c,amplitude,0,-direction,n,M_PI_2,1,-direction);
	isPeriodic = true;
    }
    else if (pattern == YSTRING("zero")) {
	// Fill with 0
	vector.resetStorage(len ? len : 1);
	if (pLen)
	    *pLen = 1;
	return true;
    }
    else if (p.startSkip("fill_",false)) {
	// Fill with value: fill_{real}_{imag}
	int pos = p.find('_');
	if (pos < 1 || p.find('_',pos + 1) > 0)
	    return boolSetError(error);
	float re = p.substr(0,pos).toDouble();
	float im = p.substr(pos + 1).toDouble();
	if (validFloatSample(re) && validFloatSample(im)) {
	    vector.resetStorage(len ? len : 1);
	    vector.fill(Complex(re,im));
	    if (pLen)
		*pLen = 1;
	    return true;
	}
	return boolSetError(error,"invalid data range");
    }
    else if (!parseVector(error,pattern,v))
	// Parse list of values
	return false;
    if (!v.length())
	return boolSetError(error,"empty result");
    if (pLen)
	*pLen = v.length();
    if (isPeriodic && forcePeriodic) {
	lenExtend = false;
	lenRequired = true;
    }
    // Try to extend data
    if (!len || (len == v.length()) || !(lenExtend || lenRequired))
	vector = v;
    else {
	if (lenExtend) {
	    if (len < v.length())
		len = v.length();
	    unsigned int rest = len % v.length();
	    if (rest)
		len += v.length() - rest;
	}
	else if ((len < v.length()) || ((len % v.length()) != 0))
	    return boolSetError(error,"required/actual length mismatch");
	vector.resetStorage(len);
	for (unsigned int i = 0; (i + v.length()) <= len; i += v.length())
	    vector.slice(i,v.length()).copy(v,v.length());
    }
    return true;
}

static int16_t s_sampleEnergize = 2047;

// Energize a number. Refer the input value to the requested energy
static inline int16_t sampleScale(float value, float scale)
{
    value *= scale;
    return (int16_t)((value >= 0.0F) ? (value + 0.5F) : (value - 0.5F));
}

// len is number of complex samples (I&Q pairs)
static bool energize(const float* samples, int16_t* dest,
    const float iScale, const float qScale, const unsigned len)
{
    if (len % 2 != 0) {
	Debug("bladerf",DebugFail,"Energize len %u must be a multiple of 2",len);
	return false;
    }
    // len is number of complex pairs
    // N is number of scalars
    const unsigned N = len * 2;
#ifdef __MMX__
    const float rescale = 32767.0 / s_sampleEnergize;
    const float is2 = iScale * rescale;
    const float qs2 = qScale * rescale;

    // Intel intrinstics
    int32_t i32[4];
    const __m64* s32A = (__m64*) &i32[0];
    const __m64* s32B = (__m64*) &i32[2];
    for (unsigned i = 0; i < N; i += 4) {
	// apply I/Q correction and scaling and convert samples to 32 bits
	// gcc -O2/-O3 on intel uses cvttss2si or cvttps2dq depending on the processor
	i32[0] = is2 * samples[i + 0];
	i32[1] = qs2 * samples[i + 1];
	i32[2] = is2 * samples[i + 2];
	i32[3] = qs2 * samples[i + 3];
	// saturate 32 bits to 16
	// process 4 16-bit samples in a 64-bit block
	__m64* d64 = (__m64*) &dest[i];
	*d64 = _mm_packs_pi32(*s32A, *s32B);
    }
    // shift 16 bit down to 12 bits for BladeRF
    // This has to be done after saturation.
    for (unsigned i = 0; i < N; i++)
	dest[i] = dest[i] >> 4;
#else
    for (unsigned i = 0; i < N; i += 2) {
	// scale and saturate
	float iv = iScale * samples[i];
	if (iv > 2047)
	    iv = 2047;
	else if (iv < -2047)
	    iv = -2047;
	float qv = qScale * samples[i + 1];
	if (qv > 2047)
	    qv = 2047;
	else if (qv < -2047)
	    qv = -2047;
	// convert and save
	dest[i] = iv;
	dest[i + 1] = qv;
    }
#endif
    return true;
}

static inline void brfCopyTxData(int16_t* dest, const float* src, const unsigned samples,
    const float scaleI, int16_t maxI, const float scaleQ, int16_t maxQ, unsigned int& clamped,
    const long* ampTable=NULL)
{
    // scale and convert to 12-bit integers
    if (!energize(src, dest, scaleI, scaleQ, samples)) {
	::memset(dest,0,2 * samples * sizeof(int16_t));
	return;
    }
    if (ampTable) {
	int16_t *d1 = dest;
	const int16_t *s1 = dest;
	for (unsigned i = 0;  i < samples;  i++) {
	    // amplifier predistortion
	    // power of the sample, normalized to the energy scale
	    // this has a range of 0 .. (2*scale)-1
	    long xRe = *s1++;
	    long xIm = *s1++;
	    unsigned p = (xRe * xRe + xIm * xIm) >> 10; // 2 * (xRe*xRe + xIm*xIm)/2048
	    // get the correction factor, abs of 0..1
	    long corrRe = ampTable[p];
	    long corrIm = ampTable[p+1];
	    // apply the correction factor (complex multiplication), rescaled by 2048
	    *d1++ = (corrRe * xRe - corrIm * xIm) >> 11;
	    *d1++ = (corrRe * xIm + corrIm * xRe) >> 11;
	}
    }
    if (htole16(0x1234) != 0x1234)
	for (unsigned i = 0;  i < samples;  i++)
	    dest[i] = htole16(dest[i]);
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


class BrfDumpFile
{
public:
    inline BrfDumpFile(const NamedList* p = 0, const char* fName = 0,
	bool createAlways = false)
	: m_dumpOk(0), m_dumpFail(0), m_tmpDumpOk(0), m_tmpDumpFail(0),
	m_newFile(false) {
	    if (p)
		init(*p,fName,createAlways);
	}
    ~BrfDumpFile()
	{ writeData(true); }
    inline bool valid() const
	{ return m_file.valid(); }
    inline const String& fileName() const
	{ return m_fileName; }
    inline const File& file() const
	{ return m_file; }
    inline bool dumpHeader()
	{ return (m_newFile && valid()) ? !(m_newFile = false) : false; }
    inline bool dumpOk() const
	{ return m_tmpDumpOk != 0; }
    inline bool dumpFail() const
	{ return m_tmpDumpFail != 0; }
    inline void resetDumpOkFail() {
	    m_tmpDumpOk = m_dumpOk;
	    m_tmpDumpFail = m_dumpFail;
	}
    inline void append(String* s) {
	    if (s && *s)
		m_dump.append(s);
	    else
		TelEngine::destruct(s);
	}
    inline void appendFormatted(const FloatVector& data, const String& fmt)
	{ append(replaceDumpParamsFmt(fmt,dumpNsData(data))); }
    inline void appendFormatted(const ComplexVector& data, bool ok) {
	    const String& fmt = ok ? m_dumpFmtOk : m_dumpFmtFail;
	    if (!fmt)
		return;
	    append(replaceDumpParamsFmt(fmt,dumpNsData(data)));
	    int& what = ok ? m_tmpDumpOk : m_tmpDumpFail;
	    if (what > 0)
		what--;
	}
    // Dump vector data if format parameter is present
    inline void dumpDataFmt(const ComplexVector& v, const NamedList& params,
	const String& fmtParam) {
	    const String& fmt = params[fmtParam];
	    if (fmt)
		append(replaceDumpParamsFmt(fmt,dumpNsData(v)));
	}
    inline bool init(const NamedList& p, const char* fName, bool createAlways = false) {
	    writeData(true);
	    if (TelEngine::null(fName))
		fName = p[YSTRING("dump_file")];
	    if (TelEngine::null(fName))
		return false;
	    m_fileName = fName;
	    replaceDumpParams(m_fileName,0,true);
	    m_newFile = false;
	    if (createAlways || !m_file.openPath(m_fileName,true)) {
		if (!m_file.openPath(m_fileName,true,false,true,false,false,true,true))
		    return false;
		m_newFile = true;
	    }
	    else if (m_file.seek(Stream::SeekEnd) < 0) {
		m_file.terminate();
		return false;
	    }
	    m_dumpFmtOk = p[YSTRING("dump_buf_ok_format")];
	    m_dumpFmtFail = p[YSTRING("dump_buf_fail_format")];
	    m_dumpOk = m_dumpFmtOk ? p.getIntValue(YSTRING("dump_buf_ok")) : 0;
	    m_dumpFail = m_dumpFmtFail ? p.getIntValue(YSTRING("dump_buf_fail")) : 0;
	    resetDumpOkFail();
	    return true;
	}
    inline void writeData(bool finalize = false) {
	    if (!valid())
		return;
	    if (m_dump.skipNull()) {
		String buf;
		buf.append(m_dump);
		m_dump.clear();
		if (buf)
		    m_file.writeData(buf.c_str(),buf.length());
	    }
	    if (finalize)
		m_file.terminate();
	}

protected:
    int m_dumpOk;
    int m_dumpFail;
    int m_tmpDumpOk;
    int m_tmpDumpFail;
    String m_dumpFmtOk;
    String m_dumpFmtFail;
    ObjList m_dump;
    bool m_newFile;
    File m_file;
    String m_fileName;
};

class BrfPeripheral : public String
{
public:
    inline BrfPeripheral(const char* name, uint8_t devId)
	: String(name),
	m_devId(devId), m_tx(false), m_rx(false), m_haveTrackAddr(false),
	m_trackLevel(-1) {
	    lowCase = name;
	    lowCase.toLower();
	    setTrack(false,false);
	}
    inline uint8_t devId() const
	{ return m_devId; }
    inline bool trackDir(bool tx) const
	{ return tx ? m_tx : m_rx; }
    inline bool haveTrackAddr() const
	{ return m_haveTrackAddr; }
    inline int trackLevel(int level = DebugAll) const
	{ return (m_trackLevel >= 0) ? m_trackLevel : level; }
    void setTrack(bool tx, bool rx, const String& addr = String::empty(), int level = -1);
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
    int m_trackLevel;
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

// Thresholds used to adjust the number of internal buffers from sampling rate
class BrfBufsThreshold
{
public:
    inline BrfBufsThreshold()
	: sampleRate(0), bufferedSamples(0), txMinBufs(0)
	{}
    unsigned int sampleRate;
    unsigned int bufferedSamples;
    unsigned int txMinBufs;

    static const char* init(DataBlock& db, const String& str, const RadioCapability& caps);
    static BrfBufsThreshold* findThres(DataBlock& db, unsigned int sampleRate);
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
class BrfDevDirState
{
public:
    inline BrfDevDirState(bool tx)
	: showDcOffsChange(0), showFpgaCorrChange(0), showPowerBalanceChange(0),
	rfEnabled(false), frequency(0), vga1(0), vga1Changed(false), vga2(0), lpf(0),
	dcOffsetI(0), dcOffsetQ(0), fpgaCorrPhase(0), fpgaCorrGain(0),
	powerBalance(0), lpfBw(0), sampleRate(0), m_timestamp(0),
	m_tx(tx)
	{}
    inline BrfDevDirState(const BrfDevDirState& src)
	{ *this = src; }
    inline BrfDevDirState& operator=(const BrfDevDirState& src) {
	    ::memcpy(this,&src,sizeof(src));
	    return *this;
	}
    inline bool tx() const
	{ return m_tx; }

    unsigned int showDcOffsChange;       // Show DC offset changed debug message
    unsigned int showFpgaCorrChange;     // Show FPGA PHASE/GAIN changed debug message
    unsigned int showPowerBalanceChange; // Show power balance changed debug message
    bool rfEnabled;                      // RF enabled flag
    unsigned int frequency;              // Used frequency
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
    uint64_t m_timestamp;

protected:
    bool m_tx;                           // Direction
};

// Holds device data. May be used to backup and restore
class BrfDevState
{
public:
    inline BrfDevState(unsigned int chg = 0, unsigned int txChg = 0,
	unsigned int rxChg = 0)
	: m_changed(chg), m_txChanged(txChg), m_rxChanged(rxChg),
	m_loopback(0), m_loopbackParams(""), m_txPatternGain(1), m_rxDcAuto(true),
	m_tx(true), m_rx(false)
	{}
    inline BrfDevState(const BrfDevState& src, unsigned int chg = 0,
	unsigned int txChg = 0, unsigned int rxChg = 0)
	: m_loopbackParams(""), m_tx(true), m_rx(false) {
	    assign(src,false);
	    setFlags(chg,txChg,rxChg);
	}
    inline void setFlags(unsigned int chg = 0, unsigned int txChg = 0,
	unsigned int rxChg = 0) {
	    m_changed = chg;
	    m_txChanged = txChg;
	    m_rxChanged = rxChg;
	}
    inline void setLoopback(int lp, const NamedList& params) {
	    m_loopback = lp;
	    m_loopbackParams.clearParams();
	    m_loopbackParams.copyParams(params);
	}
    inline BrfDevState& assign(const BrfDevState& src, bool flags = true) {
	    if (flags)
		setFlags(src.m_changed,src.m_txChanged,src.m_rxChanged);
	    else
		setFlags();
	    setLoopback(src.m_loopback,src.m_loopbackParams);
	    m_txPattern = src.m_txPattern;
	    m_txPatternGain = src.m_txPatternGain;
	    m_rxDcAuto = src.m_rxDcAuto;
	    m_tx = src.m_tx;
	    m_rx = src.m_rx;
	    return *this;
	}
    inline BrfDevState& operator=(const BrfDevState& src)
	{ return assign(src); }

    unsigned int m_changed;              // Changed flags
    unsigned int m_txChanged;            // TX data changed flags
    unsigned int m_rxChanged;            // RX data changed flags
    int m_loopback;                      // Current loopback
    NamedList m_loopbackParams;          // Loopback params
    String m_txPattern;                  // Transmit pattern
    float m_txPatternGain;               // Transmit pattern gain
    bool m_rxDcAuto;                     // Automatically adjust Rx DC offset
    BrfDevDirState m_tx;
    BrfDevDirState m_rx;
};

class BrfFloatMinMax
{
public:
    inline BrfFloatMinMax()
	: value(0), min(BRF_MAX_FLOAT), max(-BRF_MAX_FLOAT)
	{}
    inline void set(float val) {
	    value = val;
	    setMinMax(min,max,val);
	}
    inline void reset(float val = 0) {
	    value = val;
	    min = BRF_MAX_FLOAT;
	    max = -BRF_MAX_FLOAT;
	}
    inline operator float()
	{ return value; }

    float value;
    float min;
    float max;
};

class BrfFloatAccum
{
public:
    inline BrfFloatAccum()
	: count(0)
	{}
    inline void append(float val)
	{ data[count++] = val; }
    inline void reset(unsigned int len) {
	    data.resetStorage(len);
	    count = 0;
	}
    inline void normalize()
	{ data.resize(count); }
    FloatVector data;
    unsigned int count;
};

struct BrfBbCalDataResult
{
    inline BrfBbCalDataResult()
	: status(0), cal(0), test(0), total(0),
	test_total(0), cal_test(0),testOk(false), calOk(false)
	{}
    unsigned int status;
    float cal;
    float test;
    float total;
    float test_total;                  // test / total
    float cal_test;                    // cal / test
    bool testOk;
    bool calOk;
};

class BrfBbCalData
{
public:
    inline BrfBbCalData(unsigned int nSamples, const NamedList& p)
	: m_stopOnRecvFail(0), m_repeatRxLoop(5), 
	m_best(0), m_cal_test(0),
	m_prevCal(0), m_testOk(false), m_calOk(false), m_params(p),
	m_tx(true), m_rx(false),
	m_calFreq(0), m_calSampleRate(0),
	m_dcI(0), m_dcQ(0), m_phase(0), m_gain(0),
	m_buffer(nSamples), m_calTone(nSamples), m_testTone(nSamples),
	m_calToneOmega(0), m_testToneOmega(0) {
	    prepareCalculate();
	    m_stopOnRecvFail = p.getIntValue(YSTRING("recv_fail_stop"),1);
	    m_repeatRxLoop = p.getIntValue(YSTRING("recv_fail_loops"),5,1,1000);
	}
    inline const String& prefix(bool dc) const {
	    static const String s_dcPrefix = "dc_";
	    static const String s_imbalancePrefix = "imbalance_";
	    return dc ? s_dcPrefix : s_imbalancePrefix;
	}
    inline float omega(bool cal) const
	{ return cal ? m_calToneOmega : m_testToneOmega; }
    inline float* buf() const
	{ return (float*)m_buffer.data(); }
    inline unsigned int samples() const
	{ return m_buffer.length(); }
    inline ComplexVector& buffer()
	{ return m_buffer; }
    inline const ComplexVector& calTone() const
	{ return m_calTone; }
    inline const ComplexVector& testTone() const
	{ return m_testTone; }
    inline void prepareCalculate() {
	    m_best = BRF_MAX_FLOAT;
	    m_prevCal = 0;
	    m_cal.reset(-1);
	    m_total.reset();
	    m_test.reset();
	}
    inline void resetBuffer(unsigned int nSamples)
	{ resetOmega(m_calToneOmega,m_testToneOmega,nSamples); }
    inline void resetOmega(float calToneOmega, float testToneOmega, unsigned int nSamples = 0) {
	    if (nSamples)
		m_buffer.resetStorage(nSamples);
	    m_calToneOmega = calToneOmega;
	    m_testToneOmega = testToneOmega;
	    generateExpTone(m_calTone,calToneOmega,m_buffer.length());
	    generateExpTone(m_testTone,testToneOmega,m_buffer.length());
	}
    inline void setResult(BrfBbCalDataResult& res) {
	    m_prevCal = m_cal.value;
	    m_cal.set(res.cal);
	    m_test.set(res.test);
	    m_total.set(res.total);
	    m_cal_test = res.cal_test;
	    m_test_total.set(res.test_total);
	    m_calOk = res.calOk;
	    m_testOk = res.testOk;
	}
    inline bool calculate(BrfBbCalDataResult& res) {
	    const Complex* last = 0;
	    const Complex* b = m_buffer.data(0,m_buffer.length(),last);
	    const Complex* calTone = m_calTone.data();
	    const Complex* testTone = m_testTone.data();
	    Complex calSum;
	    Complex testSum;
	    res.total = 0;
	    // Calculate calibrate/test energy using the narrow band integrator
	    // Calculate total buffer energy (power)
	    for (; b != last; ++b, ++calTone, ++testTone) {
		calSum += *calTone * *b;
		testSum += *testTone * *b;
		res.total += b->norm2();
	    }
	    res.cal = calSum.norm2() / samples();
	    res.test = testSum.norm2() / samples();
	    res.cal_test = res.test ? (res.cal / res.test) : -1;
	    res.test_total = res.total ? (res.test / res.total) : -1;
	    res.calOk = 0.0F <= res.cal_test && res.cal_test <= 0.001F;
	    res.testOk = 0.5F < res.test_total && res.test_total <= 1.0F;
#if 0
	    res.test /= samples();
	    res.total /= samples();
#endif
	    return res.testOk;
	}
    inline String& dump(String& s, bool full) {
	    float delta = 0;
	    if (m_prevCal >= 0.0F)
		delta = m_cal.value - m_prevCal;
	    const char* dir = dirStr(delta ? (delta > 0.0F ? 1 : -1) : 0);
	    if (full)
		return s.printf(1024,"%s cal:%-10f test:%-10f total:%-10f "
		    "test/total:%3s %.2f%% cal/test:%3s %.2f%%",
		    dir,m_cal.value,m_test.value,m_total.value,
		    (m_testOk ? "OK" : "BAD"),m_test_total.value * 100,
		    (m_calOk ? "OK" : "BAD"),m_cal_test * 100);
	    return s.printf(1024,"%s cal:%-10f delta=%-10f",dir,m_cal.value,delta);
	}
    inline String& dump(String& s, const BrfBbCalDataResult& res) {
	    return s.printf(1024,"cal:%-10f test:%-10f total:%-10f "
		"test/total:%3s %.2f%% cal/test:%3s %.2f%%",
		res.cal,res.test,res.total,(res.testOk ? "OK" : "BAD"),
		res.test_total * 100,(res.calOk ? "OK" : "BAD"),res.cal_test * 100);
	}
    inline const String& param(bool dc, const char* name) const
	{ return m_params[prefix(dc) + name]; }
    inline unsigned int uintParam(bool dc, const char* name, unsigned int defVal = 0,
	unsigned int minVal = 0, unsigned int maxVal = (unsigned int)LLONG_MAX) const
	{ return param(dc,name).toInt64(defVal,0,minVal,maxVal); }
    inline int intParam(bool dc, const char* name, int defVal = 0,
	int minVal = INT_MIN, int maxVal = INT_MAX) const
	{ return param(dc,name).toInteger(defVal,0,minVal,maxVal); }
    inline bool boolParam(bool dc, const char* name, bool defVal = false) const
	{ return param(dc,name).toBoolean(defVal); }
    void initCal(BrfLibUsbDevice& dev, bool dc, String& fName);
    void finalizeCal(const String& result);
    void dumpCorrStart(unsigned int pass, int corr, int corrVal, int fixedCorr,
	int fixedCorrVal, unsigned int range, unsigned int step,
	int calValMin, int calValMax);
    void dumpCorrEnd(bool dc);

    int m_stopOnRecvFail;                // Stop on data recv wrong result
    unsigned int m_repeatRxLoop;         // Repeat data read on wrong result
    float m_best;
    BrfFloatMinMax m_cal;                // Calculated calibrating value
    BrfFloatMinMax m_total;              // Calculated total value
    BrfFloatMinMax m_test;               // Calculated test value
    BrfFloatMinMax m_test_total;         // Calculated test/total value
    float m_cal_test;                    // cal / test
    float m_prevCal;                     // Previous calibrating value
    bool m_testOk;
    bool m_calOk;
    NamedList m_params;                  // Calibration parameters
    BrfFloatAccum m_calAccum;
    BrfFloatAccum m_testAccum;
    BrfFloatAccum m_totalAccum;
    BrfDumpFile m_dump;

    // Initial state
    BrfDevDirState m_tx;
    BrfDevDirState m_rx;
    // Calibration params
    unsigned int m_calFreq;
    unsigned int m_calSampleRate;
    // Calibration results
    int m_dcI;
    int m_dcQ;
    int m_phase;
    int m_gain;

protected:
    ComplexVector m_buffer;
    ComplexVector m_calTone;
    ComplexVector m_testTone;
    float m_calToneOmega;
    float m_testToneOmega;
};

// Holds RX/TX related data
// Hold samples read/write related data
class BrfDevIO
{
public:
    inline BrfDevIO(bool tx)
	: showBuf(0), showBufData(true), checkTs(0), dontWarnTs(0), checkLimit(0),
	mutex(false,tx ? "BrfDevIoTx" : "BrfDevIoRx"),
	startTime(0), transferred(0),
	timestamp(0), lastTs(0), buffers(0), hdrLen(0), bufSamples(0),
	bufSamplesLen(0), crtBuf(0), crtBufSampOffs(0), newBuffer(true),
	dataDumpParams(""), dataDump(0), dataDumpFile(brfDir(tx)),
	upDumpParams(""), upDump(0), upDumpFile(String(brfDir(tx)) + "-APP"),
	captureMutex(false,tx ? "BrfCaptureTx" : "BrfCaptureRx"),
	captureSemaphore(1,tx ? "BrfCaptureTx" : "BrfCaptureRx",1),
	captureBuf(0), captureSamples(0),
	captureTs(0), captureOffset(0), captureStatus(0),
	m_tx(tx), m_bufEndianOk(BRF_LITTLE_ENDIAN)
	{}
    inline bool tx() const
	{ return m_tx; }
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
    inline void reset()
	{ resetPosTime(); }
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
    inline void fixEndian() {
#ifndef LITTLE_ENDIAN
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
#endif
	}
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
    int dontWarnTs;                      // Don't warn on invalid buffer timestamp
    int checkLimit;                      // Check IO buffers sample limit
    Mutex mutex;                         // Protect data changes when needed
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
    BrfBufsThreshold firstBufsThres;     // Initial samplerate/bufs data
    // File dump
    NamedList dataDumpParams;
    int dataDump;
    RadioDataFile dataDumpFile;
    NamedList upDumpParams;
    int upDump;
    RadioDataFile upDumpFile;
    // Capture
    Mutex captureMutex;
    Semaphore captureSemaphore;
    float* captureBuf;
    unsigned int captureSamples;
    uint64_t captureTs;
    unsigned int captureOffset;
    unsigned int captureStatus;
    String captureError;

protected:
    // Reset current buffer to start
    inline void setCrtBuf(unsigned int index = 0) {
	    crtBuf = index;
	    crtBufSampOffs = 0;
	}
    bool m_tx;
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


/**
 * Clock discipline algorithm to sync the BladeRF's VCTCXO to local machine's clock.
 * It's intended to maintain (measure and correct) sampling rate drifts,
 *  caused by equipment aging and factory faults, within a desired range.
 * 
 * The accuracy of the measured drift is highly impacted by these factors:
 * - the drift of the local machine's clock against the real time
 * - the accuracy of pinning radio board samples to local machine timestamps
 * - the time allocated for measuring the average sampling rate (a.k.a. baseline)
 */
class BrfVctcxoDiscipliner {

public:
    /// Create a discipliner with default settings, waiting to be activated and configured through control messages.
    BrfVctcxoDiscipliner()
	: m_trimsLeft(0), m_confSampleRate(0), m_freqOffset(0), m_resumePoint(0),
	m_samples(0), m_timestamp(0), m_delay(0), m_bestDelay(0), m_maxDelay(0),
	m_knownDelay(0), m_systemAccuracy(BRF_SYSTEM_ACCURACY_DEF), m_accuracyPpb(BRF_ACCURACY_PPB_DEF),
	m_nextPinning(0), m_driftPpb(0), m_trace(false), m_dumpDelays(0)
    { }

    /// Handle clock discipline commands
    bool onCmdFreqCal(Message& msg, bool start = true);

    /// Postpone activity for the specified period and drop current data
    void postponeActivity(unsigned minutes, bool dropData = false);

    /// Stop activity and drop gathered data
    void disableDiscipline(bool onCmd = false);

    /**
     * Discipline the BladeRF's VCTCXO to local machine's clock
     * This method should be called when the BrfModule catches an engine.timer message
     * @param timestamp the call time in microseconds
     * @param drift     a drift expressed in ppb, to be forcefully corrected
     */
    void trimVctcxo(uint64_t timestamp = Time::now(), int drift = 0);

protected:
    /// Update the baseline interval
    void scheduleNextPinning(uint16_t delay);
    /// Update parameters and drop current data if the configuration is outdated
    bool outdatedConfig();
    /// If it's missing, do the initial sample measurement
    bool init();
    /// Trim the VCTCXO based on the measured drift and dispatch a message with the new frequency offset
    bool processData(int drift);
    /// Compute the average radio sampling rate and return the drift (ppb) for the current interval
    int measureDrift(uint64_t& samples, uint64_t& timestamp, uint16_t& delay);
    /// Get the most accurate radio board sample measurement out of a given number
    void samplesAndTimestamp(uint64_t& samples, uint64_t& timestamp, uint16_t& delay,
	unsigned maxIter = 10);
    /// Convert microseconds to minutes (floor division)
    inline unsigned usToMin(uint64_t us)
	{ return us / 60000000UL; }
    /// The BladeRF device associated with this object
    virtual BrfLibUsbDevice& dev() =0;

    int m_trimsLeft;           ///< number of scheduled clock trims, 0 toggles to idle, -1 toggles to always active
    unsigned m_confSampleRate; ///< configured sampling rate (Hz)
    float m_freqOffset;        ///< frequency offset, as a number converted from VCTCXO's voltage to pass through a digital to analog converter
    uint64_t m_resumePoint;    ///< when postponed, activity resumes after this timestamp (microsec)
    uint64_t m_samples;        ///< initial pinning of radio board samples
    uint64_t m_timestamp;      ///< initial pinning timestamp (microsec)
    uint16_t m_delay;          ///< initial pinning duration (microsec)
    uint16_t m_bestDelay;      ///< minimum pinning delay (microsec), used to identify highly accurate measurements
    uint16_t m_maxDelay;       ///< maximum pinning delay (microsec), used to identify highly inaccurate measurements
    uint16_t m_knownDelay;     ///< fixed, known pinning delay (microsec), used as reference for assuming delay variations
    uint16_t m_systemAccuracy; ///< overall accuracy of the sync mechanism (microsec)
    unsigned m_accuracyPpb;    ///< accuracy of the sampling rate measurement, expressed in ppb
    uint64_t m_nextPinning;    ///< final pinning of samples is scheduled after this timestamp (microsec)
    int m_driftPpb;            ///< drift of the sampling rate, expressed in ppb
    bool m_trace;              ///< this variable is used in test mode to display debug messages
    unsigned int m_dumpDelays; ///< number of delays to calculate and dump
    String m_delayStat;        ///< store delays every few seconds, then dump it for analysis

    static const float s_ppbPerUnit; ///< the unit of m_freqOffset expressed as ppb
    // 1.9 for max ppm range, expressed in ppb
    // 1.25 for voltage range conversion of 0.4 - 2.4V to 0 - 2.5V
    // 2 ** 8 for integer range of m_freqOffset
};


class BrfThread;
class BrfSerialize;

class BrfLibUsbDevice : public GenObject, public BrfVctcxoDiscipliner
{
    friend class BrfDevTmpAltSet;
    friend class BrfThread;
    friend class BrfModule;
    friend class BrfInterface;
    friend class BrfSerialize;
    friend class BrfDevState;
    friend class BrfBbCalData;
    friend class BrfVctcxoDiscipliner;
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
	LmsLna3,                         // Enable LNA3 (Unused on the bladeRF)
	LmsLnaDetect,
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
    // Correction types (LMS and FPGA). Keep them in the same order
    // (values are used as array index)
    enum CorrectionType {
	CorrLmsI = 0,
	CorrLmsQ,
	CorrFpgaPhase,
	CorrFpgaGain,
	CorrCount
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
	DevStatFreq              = 0x00000001,
	DevStatVga1              = 0x00000002,
	DevStatVga2              = 0x00000004,
	DevStatLpf               = 0x00000008,
	DevStatDcI               = 0x00000010,
	DevStatDcQ               = 0x00000020,
	DevStatLpfBw             = 0x00000040,
	DevStatSampleRate        = 0x00000080,
	DevStatFpgaPhase         = 0x00000100,
	DevStatFpgaGain          = 0x00000200,
	DevStatLoopback          = 0x00000400,
	DevStatRxDcAuto          = 0x00000800,
	DevStatTxPattern         = 0x00001000,
	DevStatTs                = 0x00002000,
	DevStatPowerBalance      = 0x10000000,
	DevStatAbortOnFail       = 0x80000000,
	DevStatVga = DevStatVga1 | DevStatVga2,
	DevStatDc = DevStatDcI | DevStatDcQ,
	DevStatFpga = DevStatFpgaPhase | DevStatFpgaGain,
    };
    // Calibration status
    enum CalStatus {
	Calibrate = 0,                   // Not calibrated (not done or failed)
	Calibrated,                      // Succesfully calibrated
	Calibrating,                     // Calibration in progress
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
    inline const char* speedStr() const
	{ return speedStr(speed()); }
    inline int bus() const
	{ return m_devBus; }
    inline int addr() const
	{ return m_devAddr; }
    inline const String& address() const
	{ return m_address; }
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
    inline const String& lmsVersion() const
	{ return m_lmsVersion; }
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
    inline int checkLimit(bool tx, int val) {
	    Lock lck(m_dbgMutex);
	    return (getIO(tx).checkLimit = val);
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
    inline float freqOffset() const
	{ return m_freqOffset; }
    // Open (on=false)/close RXOUTSW switch
    inline unsigned int setRxOut(bool on)
	{ return writeLMS(0x09,on ? 0x80 : 0x00,0x80); }
    unsigned int setTxPattern(const String& pattern, float gain = 1.0F);
    void dumpStats(String& buf, const char* sep);
    void dumpTimestamps(String& buf, const char* sep);
    void dumpDev(String& buf, bool info, bool state, const char* sep,
	bool fromStatus = false, bool withHdr = true);
    void dumpBoardStatus(String& buf, const char* sep);
    unsigned int dumpPeripheral(uint8_t dev, uint8_t addr, uint8_t len, String* buf = 0);
    // Module reload
    void reLoad(const NamedList* params = 0);
    void setDataDump(int dir = 0, int level = 0, const NamedList* params = 0);
    // Open the device
    // Call the reset method in order to set the device to a known state
    unsigned int open(const NamedList& params, String& error);
    // Initialize operating parameters
    unsigned int initialize(const NamedList& params);
    // Check if parameters are set
    unsigned int isInitialized(bool tx, bool rx, String* error);
    // Close the device.
    void close();
    // Power on the radio
    // Enable timestamps, enable RF TX/RX
    unsigned int powerOn();
    // Send an array of samples waiting to be transmitted
    // samples: The number of I/Q samples (i.e. half buffer lengh)
    unsigned int syncTx(uint64_t ts, float* data, unsigned int samples,
	float* powerScale = 0, bool internal = false);
    // Receive data from the Rx interface of the bladeRF device
    // samples: The number of I/Q samples (i.e. half buffer lengh)
    unsigned int syncRx(uint64_t& ts, float* data, unsigned int& samples,
	String* error = 0, bool internal = false);
    // Capture data
    unsigned int capture(bool tx, float* buf, unsigned int samples, uint64_t& ts,
	String* error = 0);
    // Set the frequency on the Tx or Rx side
    unsigned int setFrequency(uint64_t hz, bool tx);
    // Retrieve frequency
    unsigned int getFrequency(uint32_t& hz, bool tx);
    // Set frequency offset
    unsigned int setFreqOffset(float offs, float* newVal = 0, bool stopAutoCal = true);
    // Get frequency offset
    unsigned int getFreqOffset(float& offs);
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
    // Run check / calibration procedure
    unsigned int calibrate(bool sync = true, const NamedList& params = NamedList::empty(),
	String* error = 0, bool fromInit = false);
    // Set Tx/Rx DC I/Q offset correction
    unsigned int setDcOffset(bool tx, bool i, int16_t value);
    // Retrieve Tx/Rx DC I/Q offset correction
    unsigned int getDcOffset(bool tx, bool i, int16_t& value);
    // Set/Get FPGA correction
    unsigned int setFpgaCorr(bool tx, int corr, int16_t value);
    unsigned int getFpgaCorr(bool tx, int corr, int16_t& value);
    // Retrieve TX/RX timestamp
    unsigned int getTimestamp(bool tx, uint64_t& ts);
    // Retrieve TX radio timestamp (samples) with the current timestamp from the local machine
    unsigned int samplesAndTimestamp(uint64_t& samples, uint64_t& timestamp, uint16_t& delay,
	String* serializeErr);
    // Write LMS register(s)
    unsigned int writeLMS(uint8_t addr, uint8_t value, uint8_t* rst = 0,
	String* error = 0, bool internal = false);
    unsigned int writeLMS(uint8_t addr, uint8_t value, uint8_t rst,
	String* error = 0, bool internal = false)
	{ return writeLMS(addr,value,&rst,error,internal); }
    unsigned int writeLMS(const String& str, String* error = 0, bool internal = false);
    // Read LMS register(s)
    unsigned int readLMS(uint8_t addr, uint8_t& value, String* error = 0,
	bool internal = false);
    unsigned int readLMS(String& dest, const String* read, bool readIsInterleaved,
	String* error = 0, bool internal = false);
    // Check LMS registers
    unsigned int checkLMS(const String& what, String* error = 0, bool internal = false);
    // Enable or disable loopback
    unsigned int setLoopback(const char* name = 0,
	const NamedList& params = NamedList::empty());
    // Set parameter(s)
    unsigned int setParam(const String& param, const String& value,
	const NamedList& params = NamedList::empty());
    // Utility: run device send data
    void runSend(BrfThread* th);
    // Utility: run device recv data
    void runRecv(BrfThread* th);
    // Build notification message
    Message* buildNotify(const char* status = 0);
    inline void notifyFreqOffs() {
	Message* m = buildNotify();
	m->addParam("RadioFrequencyOffset",String(m_freqOffset));
	Engine::enqueue(m);
    }
    // Release data
    virtual void destruct();
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
    static inline unsigned int setErrorFail(String* buf, const char* error,
	const char* prefix = 0)
	{ return setError(RadioInterface::Failure,buf,error,prefix); }
    static inline unsigned int setErrorTimeout(String* buf, const char* error,
	const char* prefix = 0)
	{ return setError(RadioInterface::Timeout,buf,error,prefix); }
    static inline unsigned int setErrorNotInit(String* buf,
	const char* error = "not initialized", const char* prefix = 0)
	{ return setError(RadioInterface::NotInitialized,buf,error,prefix); }
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
    static inline uint8_t fpgaCorrAddr(bool tx, bool phase) {
	    if (phase)
		return tx ? 10 : 6;
	    return tx ? 8 : 4;
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

protected:
    virtual BrfLibUsbDevice& dev()
	{ return *this; }

private:
    BrfLibUsbDevice(BrfInterface* owner);
    void doClose();
    inline void resetTimestamps(bool tx) {
	    getIO(tx).reset();
	    if (!tx) {
		m_rxTimestamp = 0;
		m_rxResyncCandidate = 0;
	    }
	}
    // Batch state update
    unsigned int setState(BrfDevState& state, String* error = 0);
    // Request changes (synchronous TX). Wait for change
    inline unsigned int setStateSyncTx(unsigned int flags = 0, String* error = 0,
	bool fatal = true) {
	    m_syncTxState.setFlags(fatal ? DevStatAbortOnFail : 0,flags);
	    return setStateSync(error);
	}
    inline unsigned int setStateSyncRx(unsigned int flags = 0, String* error = 0,
	bool fatal = true) {
	    m_syncTxState.setFlags(fatal ? DevStatAbortOnFail : 0,0,flags);
	    return setStateSync(error);
	}
    inline unsigned int setStateSyncLoopback(int lp, const NamedList& params,
	String* error = 0) {
	    m_syncTxState.setFlags(DevStatLoopback);
	    m_syncTxState.setLoopback(lp,params);
	    return setStateSync(error);
	}
    unsigned int setStateSync(String* error = 0);
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
	float scaleI, int16_t maxI, float scaleQ, int16_t maxQ, unsigned int& clamped,
	const long* ampTable);
    unsigned int recv(uint64_t& ts, float* data, unsigned int& samples,
	String* error = 0);
    void captureHandle(BrfDevIO& io, const float* buf, unsigned int samples, uint64_t ts,
	unsigned int status, const String* error);
    unsigned int internalSetSampleRate(bool tx, uint32_t value, String* error = 0);
    inline unsigned int internalSetSampleRateBoth(uint32_t value, String* error = 0) {
	    unsigned int status = internalSetSampleRate(true,value,error);
	    return status ? status : internalSetSampleRate(false,value,error);
	}
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
    unsigned int setGainExp(float breakpoint, float max);
    unsigned int setPhaseExp(float breakpoint, float max);
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
    unsigned int internalSetFreqOffs(float val, float* newVal, String* error = 0);
    unsigned int internalSetFrequency(bool tx, uint64_t val, String* error = 0);
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
		return paSelect(lowBand,error);
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
    unsigned int lmsWrite(const String& str, bool updStat, String* error = 0);
    // Read the lms configuration
    // Read all if 'read' is null
    // 'read' non null: set 'readIsInterleaved' to true if 'read' is addr/value interleaved
    unsigned int lmsRead(String& dest, const String* read,
	bool readIsInterleaved, String* error = 0);
    // Check LMS registers
    unsigned int lmsCheck(const String& what, String* error = 0);
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
    inline unsigned int internalSetLpfBandwidthBoth(uint32_t band, String* error = 0) {
	    unsigned int status = internalSetLpfBandwidth(true,band,error);
	    if (!status)
		status = internalSetLpfBandwidth(false,band,error);
	    return status;
	}
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
    inline unsigned int enableRfFpgaBoth(bool on, String* error = 0) {
	    unsigned int status = enableRfFpga(true,on,error);
	    if (status == 0)
		return enableRfFpga(false,on,error);
	    return status;
	}
    unsigned int enableRf(bool tx, bool on, bool frontEndOnly = false, String* error = 0);
    unsigned int enableRfFpga(bool tx, bool on, String* error = 0);
    // Check if fpga is loaded
    // Return NoError/NotInitialized (result OK) or other error on failure
    unsigned int checkFpga();
    // Restore device after loading the FPGA
    unsigned int restoreAfterFpgaLoad(String* error = 0);
    // Change some LMS registers default value on open
    unsigned int openChangeLms(const NamedList& params, String* error = 0);
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
    inline unsigned int paSelect(bool lowBand, String* error = 0)
	{ return paSelect(lowBand ? LmsPa1 : LmsPa2,error); }
    unsigned int paSelect(int pa, String* error = 0);
    int64_t clampInt(int64_t val, int64_t minVal, int64_t maxVal, const char* what = 0,
	int level = DebugNote);
    inline int64_t clampIntParam(const NamedList& params, const String& param,
	int64_t defVal, int64_t minVal, int64_t maxVal, int level = DebugConf)
	{ return clampInt(params.getInt64Value(param,defVal),minVal,maxVal,param,level); }
    float clampFloat(float val, float minVal, float maxVal, const char* what = 0,
	int level = DebugNote);
    inline float clampFloatParam(const NamedList& params, const String& param,
	float defVal, float minVal, float maxVal, int level = DebugConf)
	{ return clampFloat(params.getDoubleValue(param,defVal),minVal,maxVal,param,level); }
    unsigned int openDevice(bool claim, String* error = 0);
    void closeDevice();
    void closeUsbDev();
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
    // Set I/O buffers
    void initBuffers(bool* txSet, unsigned int totalSamples, unsigned int txMinSend);
    // Compute Rx avg values, autocorrect offsets if configured
    void computeRx(uint64_t ts);
    // Check io timestamps
    void ioBufCheckTs(bool tx, unsigned int nBufs = 0);
    void setIoDontWarnTs(bool tx);
    // Check io samples limit
    void ioBufCheckLimit(bool tx, unsigned int nBufs = 0);
    // Alter data
    void updateAlterData(const NamedList& params);
    void rxAlterData(bool first);
    // Calibration utilities
    void dumpState(String& s, const NamedList& data, bool lockPub, bool force = false);
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
    unsigned int calibrateBbCorrection(BrfBbCalData& data, int corr, int range, int step,
	int pass, String* error);
    unsigned int prepareCalibrateBb(BrfBbCalData& data, bool dc, String* error);
    unsigned int calibrateBb(BrfBbCalData& data, bool dc, String* error);
    unsigned int calibrateBaseband(String* error);
    // amplifier linearization
    ComplexVector sweepPower(float startdB, float stopdB, float stepdB);
    unsigned int findGainExpParams(const ComplexVector& sweep, float startSweep, float stepSweep);
    unsigned int findPhaseExpParams(const ComplexVector& swee, float startSweep, float stepSweepp);
    unsigned int calculateAmpTable();
    //
    unsigned int loopbackCheck(String* error);
    unsigned int testVga(const char* loc, bool tx, bool preMixer, float omega = 0,
	String* error = 0);
    inline unsigned int testVgaCheck(const NamedList& p, const char* loc,
	float omega, String* error, const String& prefix = String::empty()) {
	    unsigned int status = 0;
	    #define BRF_TEST_VGA(param,tx,preMixer) \
	    if (!status && p.getBoolValue(prefix + param)) \
		status = testVga(loc,tx,preMixer,omega,error);
	    BRF_TEST_VGA("test_tx_vga1",true,true);
	    BRF_TEST_VGA("test_tx_vga2",true,false);
	    BRF_TEST_VGA("test_rx_vga1",false,true);
	    BRF_TEST_VGA("test_rx_vga2",false,false);
	    #undef BRF_TEST_VGA
	    return status;
	}
    // Set error string or put a debug message
    unsigned int showError(unsigned int code, const char* error, const char* prefix,
	String* buf, int level = DebugNote);
    void printIOBuffer(bool tx, const char* loc, int index = -1, unsigned int nBufs = 0);
    void dumpIOBuffer(BrfDevIO& io, unsigned int nBufs);
    void updateIODump(BrfDevIO& io);
    inline BrfDevIO& getIO(bool tx)
	{ return tx ? m_txIO : m_rxIO; }
    inline BrfDevDirState& getDirState(bool tx)
	{ return tx ? m_state.m_tx : m_state.m_rx; }
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
    unsigned int internalSetLoopback(int mode = 0,
	const NamedList& params = NamedList::empty(), String* error = 0);
    unsigned int setLoopbackPath(int mode, String& error);
    void dumpLoopbackStatus(String* dest = 0);
    void dumpLmsModulesStatus(String* dest = 0);
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
    inline bool setRxDcAuto(bool value) {
	    if (m_state.m_rxDcAuto != value)
		return !(m_state.m_rxDcAuto = value);
	    return m_state.m_rxDcAuto;
	}
    inline unsigned int getRxSamples(const NamedList& p, const char* name = "samples") {
	    unsigned int n = p.getIntValue(name,totalSamples(false),0);
	    if (n < 1000)
		return 1000;
	    if ((n % 4) == 0)
		return n;
	    return n + 4 - (n % 4);
	}
    // Start internal threads
    unsigned int startCalibrateThreads(String* error,
	const NamedList& params = NamedList::empty());
    // Pause/resume I/O internal threads pause
    unsigned int calThreadsPause(bool on, String* error = 0);
    // Stop internal threads
    void stopThreads();
    inline unsigned int checkDev(const char* loc) {
	    return m_devHandle ? 0 :
		showError(RadioInterface::NotInitialized,"not open",loc,0,DebugGoOn);
	}
    inline unsigned int checkCalStatus(const char* loc) {
	    return (Calibrating != m_calibrateStatus) ? 0 :
		showError(RadioInterface::NotCalibrated,"calibrating",loc,0,DebugGoOn);
	}
    inline unsigned int checkPubFuncEntry(bool internal, const char* loc) {
	    unsigned int status = 0;
	    BRF_FUNC_CALL_RET(checkDev(loc));
	    if (!internal)
		{ BRF_FUNC_CALL_RET(checkCalStatus(loc)); }
	    return 0;
	}
    unsigned int waitCancel(const char* loc, const char* reason, String* error);
    // Apply parameters from start notification message
    unsigned int applyStartParams(const NamedList& params, String* error);

    BrfInterface* m_owner;               // The interface owning the device
    String m_serial;                     // Serial number of device to use
    bool m_initialized;                  // Initialized flag
    bool m_exiting;                      // Exiting flag
    bool m_closing;                      // Closing flag
    bool m_closingDevice;                // Closing device flag
    bool m_notifyOff;                    // Notify power off
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
    String m_address;                    // Device address
    String m_devSerial;                  // Device serial number
    String m_devFwVerStr;                // Device firmware version string
    String m_devFpgaVerStr;              // Device FPGA version string
    String m_devFpgaFile;                // Device FPGA file (nul if not loaded)
    String m_devFpgaMD5;                 // FPGA MD5
    String m_lmsVersion;                 // LMS chip version
    uint16_t m_ctrlTransferPage;         // Control transfer page size
    DataBlock m_calCache;
    //
    unsigned int m_syncTout;             // Sync transfer timeout (in milliseconds)
    Semaphore m_syncSemaphore;
    unsigned int m_ctrlTout;             // Control transfer timeout (in milliseconds)
    unsigned int m_bulkTout;             // Bulk transfer timeout (in milliseconds)
    int m_altSetting;
    int m_rxShowDcInfo;                  // Output Rx DC info
    int m_rxDcOffsetMax;                 // Rx DC offset correction
    int m_rxDcAvgI;                      // Current average for I (in-phase) DC RX offset
    int m_rxDcAvgQ;                      // Current average for Q (quadrature) DC RX offset
    float m_freqOffset;                  // Master clock frequency adjustment
    bool m_txGainCorrSoftware;           // Use software TX GAIN correction
    BrfDevIO m_txIO;
    BrfDevIO m_rxIO;
    DataBlock m_bufThres;                // Thresholds for used buffers adjustment (BrfBufsThreshold)
    LusbTransfer m_usbTransfer[EpCount]; // List of USB transfers
    BrfDevState m_state;                 // State data used for current operation and backup / restore
    bool m_syncTxStateSet;               // 
    unsigned int m_syncTxStateCode;      // 
    String m_syncTxStateError;           // 
    BrfDevState m_syncTxState;           // Data used to sync set state in send method
    uint64_t m_rxTimestamp;              // RX timestamp
    uint64_t m_rxResyncCandidate;        // RX: timestamp resync value
    unsigned int m_rxTsPastIntervalMs;   // RX: allowed timestamp in the past interval in 1 read operation
    unsigned int m_rxTsPastSamples;      // RX: How many samples in the past to allow in 1 read operation
    float m_warnClamped;                 // TX: Warn clamped threshold (percent)
    unsigned int m_minBufsSend;          // Minimum buffers to send
    unsigned int m_silenceTimeMs;        // Silence timestamp related debug messages (in millseconds)
    uint64_t m_silenceTs;                // Silence timestamp related debug messages
    // TX power scale
    float m_txPowerBalance;
    bool m_txPowerBalanceChanged;
    float m_txPowerScaleI;
    float m_txPowerScaleQ;
    float m_wrPowerScaleI;
    float m_wrPowerScaleQ;
    int16_t m_wrMaxI;
    int16_t m_wrMaxQ;
    // amp linearization
    float m_gainExpBreak;	    // amp linearization gain exapansion breakpoint, dB power scale
    float m_gainExpSlope;	    // amp linearization gain exapansion slope, dB power gain
    float m_phaseExpBreak;	    // amp linearization phase expansion breakpoint, dB power scale
    float m_phaseExpSlope;	    // amp linearization phase expanaion slope, radians per power unit
    // The parameters above are used to generate this table
    long m_ampTable[2*2*2048];	    // amp linearization table, complex pairs indexed in normalized power units
    bool m_ampTableUse;
    // Alter data
    NamedList m_rxAlterDataParams;
    bool m_rxAlterData;
    int16_t m_rxAlterIncrement;
    String m_rxAlterTsJumpPatern;        // Pattern used to alter rx timestamps (kept to check changes)
    bool m_rxAlterTsJumpSingle;          // Stop altering ts after first pass
    DataBlock m_rxAlterTsJump;           // Values to alter rx timestamps
    unsigned int m_rxAlterTsJumpPos;     // Current position in rx alter timestamps
    bool m_txPatternChanged;
    ComplexVector m_txPattern;
    ComplexVector m_txPatternBuffer;
    unsigned int m_txPatternBufPos;
    // Check & calibration
    bool m_calLms;                       // Run LMS auto cal
    int m_calibrateStatus;
    int m_calibrateStop;
    NamedList m_calibration;             // Calibration parameters
    String m_devCheckFile;
    String m_bbCalDcFile;
    String m_bbCalImbalanceFile;
    BrfThread* m_calThread;
    BrfThread* m_sendThread;
    BrfThread* m_recvThread;
    Semaphore m_internalIoSemaphore;
    uint64_t m_internalIoTimestamp;
    unsigned int m_internalIoTxRate;
    unsigned int m_internalIoRxRate;
    bool m_internalIoRateChanged;
    Mutex m_threadMutex;
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
    inline bool devLocked() const
	{ return m_io.mutex.locked(); }
    inline unsigned int wait(String* error = 0, long maxwait = -1) {
	    if (m_lock.acquire(m_io.mutex,maxwait)) {
		if ((status = m_device->cancelled(error)) != 0)
		    drop();
	    }
	    else
		status = m_device->showError(RadioInterface::Failure,
		    "Failed to serialize",brfDir(m_io.tx()),error,DebugWarn);
	    return status;
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
    inline void setPending(unsigned int oper, unsigned int code = Pending)
	{ RadioInterface::setPending(oper,code); }
    void notifyError(unsigned int status, const char* str, const char* oper);
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
    virtual unsigned int setFreqOffset(float offs, float* newVal = 0)
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
    virtual void completeDevInfo(NamedList& p, bool full = false, bool retData = false);

protected:
    BrfInterface(const char* name);
    // Method to call after creation to init the interface
    unsigned int init(const NamedList& params, String& error);
    virtual void destroyed();

private:
    BrfLibUsbDevice* m_dev;              // Used device
};


class BrfThread : public Thread
{
public:
    enum Type {
	Unknown = 0,
	DevCalibrate,
	DevSend,
	DevRecv,
    };
    // Device thread
    BrfThread(BrfLibUsbDevice* dev, int type, const NamedList& p = NamedList::empty(),
	const char* name = 0, Thread::Priority prio = Thread::Normal);
    ~BrfThread()
	{ notify(); }
    inline const char* name() const
	{ return m_params; }
    inline BrfInterface* ifc() const
	{ return (m_device ? m_device->owner() : 0); }
    virtual void cleanup()
	{ notify(); }
    inline bool isPaused() const
	{ return m_paused; }
    // I/O pause check. Update timestamp on resume
    // This method is expected to be called from run()
    inline bool paused(bool tx, uint64_t& ts, unsigned int& status) {
	    if (!m_pauseToggle)
		return m_paused;
	    m_paused = !m_paused;
	    if (m_paused)
		status = 0;
	    else if (m_device)
		status = m_device->getTimestamp(tx,ts);
	    else
		status = RadioInterface::NotInitialized;
	    bool failed = (status && status != RadioInterface::Cancelled);
	    Debug(ifc(),failed ? DebugNote : DebugAll,"%s %s at ts=" FMT64U " [%p]",
		name(),(m_paused ? "paused" : "resume"),ts,ifc());
	    m_pauseToggle = false;
	    return m_paused;
	}
    // Start this thread. Delete object on failure and return 0
    BrfThread* start();
    // Pause / resume a thread
    static inline unsigned int pauseToggle(BrfThread*& th, Mutex* mtx, bool on,
	String* error = 0);
    static inline unsigned int pause(BrfThread*& th, Mutex* mtx, String* error = 0)
	{ return pauseToggle(th,mtx,true,error); }
    static inline unsigned int resume(BrfThread*& th, Mutex* mtx, String* error = 0)
	{ return pauseToggle(th,mtx,false,error); }
    // Stop thread
    static void cancelThread(BrfThread*& th, Mutex* mtx, unsigned int waitMs,
	DebugEnabler* dbg, void* ptr);

protected:
    virtual void run();
    void notify();

    int m_type;
    NamedList m_params;
    BrfLibUsbDevice* m_device;
    bool m_paused;
    bool m_pauseToggle;
    const char* m_priority;
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

protected:
    virtual void initialize();
    virtual bool received(Message& msg, int id);
    virtual void statusModule(String& str);
    virtual void statusParams(String& str);
    virtual void statusDetail(String& str);
    virtual bool commandComplete(Message& msg, const String& partLine,
	const String& partWord);
    bool createIface(NamedList& params);
    void completeIfaces(String& dest, const String& partWord);
    bool onCmdControl(BrfInterface* ifc, Message& msg);
    bool onCmdStatus(String& retVal, String& line);
    bool onCmdGain(BrfInterface* ifc, Message& msg, int tx = -1, bool preMixer = true);
    bool onCmdCorrection(BrfInterface* ifc, Message& msg, int tx = -1, int corr = 0);
    bool onCmdLmsWrite(BrfInterface* ifc, Message& msg);
    bool onCmdBufOutput(BrfInterface* ifc, Message& msg);
    bool onCmdShow(BrfInterface* ifc, Message& msg, const String& what = String::empty());
    bool onCmdFreqOffs(BrfInterface* ifc, Message& msg);
    bool onCmdFreqCal(BrfInterface* ifc, Message& msg, bool start = true);
    void setDebugPeripheral(const NamedList& list);
    void setSampleEnergize(const String& value);
    inline bool waitDisciplineFree() {
	    do {
		Lock lck(this);
		if (!m_disciplineBusy) {
		    m_disciplineBusy = true;
		    return true;
		}
		lck.drop();
		Thread::idle();
	    }
	    while (!Thread::check(false));
	    return false;
	}

    unsigned int m_ifaceId;
    ObjList m_ifaces;
    bool m_disciplineBusy;     ///< flag used to serialize the VCTCXO discipliner
    unsigned m_lastDiscipline; ///< the timestamp (sec) of the last call on the discipliner
};

static bool s_usbContextInit = false;            // USB library init flag
const float BrfVctcxoDiscipliner::s_ppbPerUnit = 19000 * 1.25 / 256;
INIT_PLUGIN(BrfModule);
static Configuration s_cfg;                      // Configuration file (protected by plugin mutex)
static const String s_modCmds[] = {"help",""};
static const String s_ifcCmds[] = {
    "txgain1", "txgain2", "rxgain1", "rxgain2",
    "txdci", "txdcq", "txfpgaphase", "txfpgagain",
    "rxdci", "rxdcq", "rxfpgaphase", "rxfpgagain",
    "showstatus", "showboardstatus", "showstatistics", "showtimestamps", "showlms",
    "vgagain","correction","lmswrite",
    "bufoutput","rxdcoutput","txpattern","show",
    "cal_stop", "cal_abort",
    "balance",
    "gainexp", "phaseexp",
     "freqoffs", "freqcalstart", "freqcalstop",
    ""};
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
static const TokenDict s_corr[] = {
    {"I",     BrfLibUsbDevice::CorrLmsI},
    {"Q",     BrfLibUsbDevice::CorrLmsQ},
    {"PHASE", BrfLibUsbDevice::CorrFpgaPhase},
    {"GAIN",  BrfLibUsbDevice::CorrFpgaGain},
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

static inline void loadCfg(Configuration* cfg = 0, bool warn = true)
{
    if (!cfg)
	cfg = &s_cfg;
    *cfg = Engine::configFile("ybladerf");
    cfg->load(warn);
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
void BrfPeripheral::setTrack(bool tx, bool rx, const String& addr, int level)
{
    bool changed = m_tx != tx || m_rx != rx;
    String oldTrackAddr;
    if (m_haveTrackAddr)
	oldTrackAddr.hexify(m_trackAddr,sizeof(m_trackAddr));
    m_tx = tx;
    m_rx = rx;
    m_trackLevel = level;
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
	    "%s peripheral debug changed: tx=%s rx=%s tracked_addr=%s level=%d",
	    c_str(),String::boolText(m_tx),String::boolText(m_rx),ta.safe(),level);
    }
    else
	Debug(&__plugin,DebugAll,"%s peripheral debug is disabled",c_str());
}


//
// BrfBufsThreshold
//
const char* BrfBufsThreshold::init(DataBlock& db, const String& str,
    const RadioCapability& caps)
{
    db.clear();
    if (!str)
	return 0;
    ObjList* list = str.split(',',false);
    unsigned int n = list->count();
    const char* result = 0;
    if (!n) {
	TelEngine::destruct(list);
	return 0;
    }
    db.assign(0,n * sizeof(BrfBufsThreshold));
    BrfBufsThreshold* t = (BrfBufsThreshold*)db.data(0);
    unsigned int i = 0;
    for (ObjList* o = list->skipNull(); o; o = o->skipNext(), ++i) {
	String& s = *static_cast<String*>(o->get());
	int pos1 = s.find('/');
	if (pos1 < 0) {
	    result = "invalid format";
	    break;
	}
	int64_t sRate = s.substr(0,pos1).trimBlanks().toInt64();
	int64_t bSamples = 0;
	int txMinBufs = 0;
	int pos2 = s.find('/',pos1 + 1);
	if (pos2 > pos1) {
	    String tmp = s.substr(pos2 + 1).trimBlanks();
	    if (tmp)
		txMinBufs = tmp.toInteger(-1);
	    bSamples = s.substr(pos1 + 1,pos2 - pos1 - 1).trimBlanks().toInt64(-1);
	}
	else
	    bSamples = s.substr(pos1 + 1).trimBlanks().toInt64(-1);
	XDebug(&__plugin,DebugAll,"BrfBufsThreshold::init() %u/%u '%s' -> "FMT64"/"FMT64"/%d",
	    i + 1,n,s.c_str(),sRate,bSamples,txMinBufs);
	if (sRate < caps.minSampleRate || sRate > caps.maxSampleRate)
	    result = "samplerate out of range";
	else if (bSamples <= 0 || bSamples > 0xffffffff)
	    result = "invalid buffered_samples";
	else if (txMinBufs < 0)
	    result = "invalid tx_min_buffers";
	else {
	    t[i].sampleRate = sRate;
	    t[i].bufferedSamples = bSamples;
	    t[i].txMinBufs = txMinBufs;
	    if (!i || t[i].sampleRate > t[i - 1].sampleRate ||
		t[i].bufferedSamples > t[i - 1].bufferedSamples)
		continue;
	    result = "not in ascending order";
	}
	break;
    }
    TelEngine::destruct(list);
    if (result) {
	db.clear();
	return result;
    }
#ifdef XDEBUG
    String s;
    for (i = 0; i < n; i++, t++)
	s << "\r\n" << t->sampleRate << "\t" << t->bufferedSamples << "\t" << t->txMinBufs;
    Output("Got %u BrfBufsThreshold:%s",n,encloseDashes(s));
#endif
    return 0;
}

BrfBufsThreshold* BrfBufsThreshold::findThres(DataBlock& db, unsigned int sampleRate)
{
    if (!(db.length() && sampleRate))
	return 0;
    unsigned int n = db.length() / sizeof(BrfBufsThreshold);
    BrfBufsThreshold* t = (BrfBufsThreshold*)db.data(0);
    for (unsigned int i = 0; i < n; i++, t++) {
	if (t->sampleRate <= sampleRate) {
	    // Last entry or less than next one: return it
	    if (i == n - 1 || sampleRate < t[1].sampleRate)
		return t;
	    continue;
	}
    }
    return 0;
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
// BrfBbCalData
//
void BrfBbCalData::initCal(BrfLibUsbDevice& dev, bool dc, String& fName)
{
    if (!fName)
	fName = param(dc,"file_dump");
    if (fName) {
	replaceDumpParams(fName,0,true);
	if (m_dump.init(m_params,fName)) {
	    if (m_dump.dumpHeader()) {
		const String& fmt = param(dc,"header_format");
		NamedString* ns = new NamedString("data");
		dev.dumpState(*ns,m_params,true,true);
		*ns <<
		    "\r\n\r\nOmega_Error: " << omega(true) <<
		    "\r\nOmega_Test: " << omega(false);
		String* s = new String(fmt.safe("TIME: ${time}${newline}${data}"));
		replaceDumpParams(*s,ns);
		m_dump.append(s);
	    }
	    m_dump.dumpDataFmt(calTone(),m_params,"dump_filter_cal");
	    m_dump.dumpDataFmt(testTone(),m_params,"dump_filter_test");
	}
    }
    else
        m_dump.writeData(true);

    unsigned int n = uintParam(dc,"dump_tone");
    if (n) {
	String cS, tS;
	if (n > calTone().length())
	    n = calTone().length();
	calTone().head(n).dump(cS,Math::dumpComplex," ","%.2f,%.2f");
	testTone().head(n).dump(tS,Math::dumpComplex," ","%.2f,%.2f");
	Output("Omega cal=%f test=%f\r\nCAL: %s\r\nTEST: %s",omega(true),omega(false),
	    cS.safe(),tS.safe());
    }
}

void BrfBbCalData::finalizeCal(const String& result)
{
    if (m_dump.valid()) {
	const String& fmt = m_params[YSTRING("dump_result_format")];
	if (fmt) {
	    NamedString* ns = new NamedString("data",result.safe("FAILURE"));
	    m_dump.append(replaceDumpParamsFmt(fmt,ns));
	}
    }
}

void BrfBbCalData::dumpCorrStart(unsigned int pass, int corr, int corrVal, int fixedCorr,
    int fixedCorrVal, unsigned int range, unsigned int step,
    int calValMin, int calValMax)
{
    const String& fmt = m_params[YSTRING("dump_pass_info_start")];
    if (fmt) {
	String* s = 0;
	if (fmt != YSTRING("-"))
	    s = new String(fmt);
	else
	    s = new String("${newline}${newline}${data}");
	NamedString* ns = new NamedString("data");
	ns->printf(1024,"Pass #%u calibrating %s (crt: %d) %s=%d "
	    "samples=%u range=%d step=%d interval=[%d..%d]",
	    pass,lookup(corr,s_corr),corrVal,lookup(fixedCorr,s_corr),fixedCorrVal,
	    samples(),range,step,calValMin,calValMax);
	replaceDumpParams(*s,ns);
	m_dump.append(s);
    }
    unsigned int n = 0;
    if (m_params[YSTRING("dump_accumulate_format")])
	n = range * 2 + 1;
    m_calAccum.reset(n);
    m_testAccum.reset(n);
    m_totalAccum.reset(n);
}

void BrfBbCalData::dumpCorrEnd(bool dc)
{
    if (m_calAccum.data.length()) {
	const String& accum = m_params[YSTRING("dump_accumulate_format")];
	if (accum) {
	    m_calAccum.normalize();
	    m_testAccum.normalize();
	    m_totalAccum.normalize();
	    String* s = new String(accum);
	    replaceDumpParams(*s,dumpNsData(m_calAccum.data,"data_cal"),false,
		dumpNsData(m_testAccum.data,"data_test"),
		dumpNsData(m_totalAccum.data,"data_total"));
	    m_dump.append(s);
	}
    }
    const String& fmt = m_params[YSTRING("dump_pass_info_end")];
    if (fmt) {
	String* s = 0;
	if (fmt != YSTRING("-"))
	    s = new String(fmt);
	else
	    s = new String("${newline}${data}");
	NamedString* ns = new NamedString("data");
	ns->printf(1024,"Result: %d/%d Min/Max: cal=%f/%f test=%f/%f total=%f/%f",
	    (dc ? m_dcI : m_phase),(dc ? m_dcQ : m_gain),m_cal.min,m_cal.max,
	    m_test.min,m_test.max,m_total.min,m_total.max);
	replaceDumpParams(*s,ns);
	m_dump.append(s);
    }
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
// BrfVctcxoDiscipliner
//
bool BrfVctcxoDiscipliner::onCmdFreqCal(Message& msg, bool start)
{
    if (start) {
	if (!m_trimsLeft) {
	    Debug(dev().owner(),DebugNote,"Frequency calibration is starting [%p]",dev().owner());
	    m_trimsLeft = -1;
	}
	const NamedString* s = msg.getParam(YSTRING("system_accuracy"));
	if (s) {
	    int us = s->toInteger(-1,0,0,2000);
	    if (us >= 0) {
		if (us != (int)m_systemAccuracy) {
		    postponeActivity(1,true);
		    m_systemAccuracy = us;
		    scheduleNextPinning(m_delay);
		}
	    }
	    else
		Debug(dev().owner(),DebugNote,"VCTCXO discipliner: ignoring invalid %s='%s' [%p]",
		    s->name().c_str(),s->c_str(),dev().owner());
	}
	s = msg.getParam(YSTRING("count"));
	if (s) {
	    int count = s->toInteger();
	    if (count >= 0)
		m_trimsLeft = (count) ? count : -1;
	    else
		Debug(dev().owner(),DebugNote,"VCTCXO discipliner: ignoring invalid %s='%s' [%p]",
		    s->name().c_str(),s->c_str(),dev().owner());
	}
    }
    else if (!m_trimsLeft) {
	msg.retValue() << "frequency calibration is currently disabled";
	return true;
    }
    // return current parameters
    if (m_trimsLeft > 0)
	msg.retValue() << "count=" << m_trimsLeft << " ";
    uint64_t usec = Time::now();
    unsigned int last = (!m_samples ? 0 : usToMin(m_nextPinning - m_timestamp));
    unsigned int remains = (!m_samples ? 0 : usToMin(m_nextPinning - usec));
    msg.retValue() << "measurement_interval=" << last << "min (" << remains <<
	"min left) system_accuracy=" << m_systemAccuracy << "us measurement_accuracy=" <<
	m_accuracyPpb << "ppb freqoffs=" << dev().m_freqOffset;
    if (m_resumePoint > usec)
	msg.retValue() << " (idling for " << usToMin(m_resumePoint - usec) << "min)";
    else if (!start && m_samples) {
	uint64_t samples = 0, timestamp = 0;
	uint16_t delay = 0;
	int ppb = measureDrift(samples,timestamp,delay);
	if (samples) {
	    String str;
	    msg.retValue() << (str.printf(" (current drift: ppb=%d interval=%gmin delay=%uus",
		ppb,(timestamp - m_timestamp) / 60.0e6F,delay));
	}
	else
	    msg.retValue() << " (drift measurement failed)";
    }
    return true;
}

void BrfVctcxoDiscipliner::postponeActivity(unsigned minutes, bool dropData)
{
    if (minutes) {
	m_resumePoint = usToMin(minutes) + Time::now();
	if (m_trace)
	    Debug(dev().owner(),DebugInfo,"VCTCXO discipliner: postpone %u min [%p]",minutes,dev().owner());
    }
    if (dropData && m_samples) {
	m_samples = 0;
	if (m_trace)
	    Debug(dev().owner(),DebugInfo,"VCTCXO discipliner: dropping current data [%p]",dev().owner());
    }
}

void BrfVctcxoDiscipliner::disableDiscipline(bool onCmd)
{
    if (!m_trimsLeft)
	return;
    m_trimsLeft = 0;
    postponeActivity(0,true);
    Debug(dev().owner(),DebugNote,"Frequency calibration is stopping (%s) [%p]",
	onCmd ? "changed by command" : "disabled",dev().owner());
    if (onCmd)
	dev().notifyFreqOffs();
}

void BrfVctcxoDiscipliner::trimVctcxo(uint64_t timestamp, int drift)
{
    // process a previously measured drift or as forced input
    if (processData(drift ? drift : m_driftPpb))
	return;
    // minimize activity until all prerequisites are met
    if (!m_trimsLeft || outdatedConfig() || m_resumePoint > timestamp || init())
	return;
    // Dump delays ?
    if (m_dumpDelays) {
	uint64_t samples = 0;
	uint64_t timestamp = 0;
	uint16_t delay = 0;
	String err;
	Thread::yield();
	dev().samplesAndTimestamp(samples,timestamp,delay,&err);
	if (samples) {
	    bool dump = (m_dumpDelays == 1);
	    m_dumpDelays--;
	    m_delayStat.append(String(delay)," ");
	    if (dump) {
		Output("VCTCXO discipliner delays: %s",m_delayStat.c_str());
		m_delayStat.clear();
	    }
	}
    }
    // wait the passing of the baseline interval before trying to determine the current drift
    if (m_nextPinning > timestamp)
	return;
    uint64_t samples = 0;
    uint16_t delay = 0;
    m_driftPpb = measureDrift(samples,timestamp,delay);
    // update the baseline interval if the measurement is valid
    if (!samples)
	return;
    scheduleNextPinning(delay);
    // drop the measured drift if the measurement isn't accurate
    if (m_nextPinning > timestamp) {
	if (m_trace)
	    Debug(dev().owner(),DebugInfo,
		"VCTCXO discipliner: inaccurate measurement rescheduled in %umin [%p]",
		usToMin(m_nextPinning - Time::now()),dev().owner());
	m_driftPpb = 0;
	return;
    }
    // replace the initial measurement
    m_samples = samples;
    m_timestamp = timestamp;
    m_delay = delay;
}

void BrfVctcxoDiscipliner::scheduleNextPinning(uint16_t delay)
{
    m_nextPinning = m_systemAccuracy;
    if (m_delay > m_knownDelay)
	m_nextPinning += m_delay - m_knownDelay;
    if (delay > m_knownDelay)
	m_nextPinning += delay - m_knownDelay;
    m_nextPinning *= 1000000000UL / m_accuracyPpb;
    m_nextPinning += m_timestamp;
    if (m_trace)
	Debug(dev().owner(),DebugInfo,
	    "VCTCXO discipliner: scheduled next pinning at %f (%umin) system_accuracy=%u "
	    "accuracy_ppb=%u delay(initial/current/known)=%u/%u/%u [%p]",
	    1.0e-6 * m_nextPinning,usToMin(m_nextPinning - m_timestamp),
	    m_systemAccuracy,m_accuracyPpb,m_delay,delay,m_knownDelay,dev().owner());
}

bool BrfVctcxoDiscipliner::outdatedConfig()
{
    // check if current configuration is already valid
    if (dev().getDirState(true).rfEnabled
	&& dev().m_calibrateStatus != BrfLibUsbDevice::Calibrating
	&& dev().m_freqOffset == m_freqOffset
	&& dev().getDirState(true).sampleRate == m_confSampleRate
	&& m_confSampleRate)
	return false;
    if (m_freqOffset != dev().m_freqOffset) {
	if (m_trace && m_freqOffset)
	    Debug(dev().owner(),DebugInfo,
		"VCTCXO discipliner: voltageDAC changed %g -> %g [%p]",
		m_freqOffset,dev().m_freqOffset,dev().owner());
	m_freqOffset = dev().m_freqOffset;
    }
    if (m_confSampleRate != dev().getDirState(true).sampleRate) {
	if (m_trace && m_confSampleRate)
	    Debug(dev().owner(),DebugInfo,
		"VCTCXO discipliner: configSampleRate changed %u -> %u [%p]",
		m_confSampleRate,dev().getDirState(true).sampleRate,dev().owner());
	m_confSampleRate = dev().getDirState(true).sampleRate;
    }
    postponeActivity(3,true);
    return true;
}

bool BrfVctcxoDiscipliner::init()
{
    if (!m_samples) {
	samplesAndTimestamp(m_samples,m_timestamp,m_delay,20);
	scheduleNextPinning(m_delay);
	return true;
    }
    return false;
}

bool BrfVctcxoDiscipliner::processData(int drift)
{
    if (!drift)
	return false;
    if (m_driftPpb && drift != m_driftPpb) {
	Debug(dev().owner(),DebugNote,
	    "VCTCXO discipliner: dropping last measured drift %dppb [%p]",
	    m_driftPpb,dev().owner());
	m_driftPpb = 0;
    }
    // transform the drift in voltageDAC units used for trimming the VCTCXO
    float trimDAC = -drift / s_ppbPerUnit;
    // limit the change in voltage (trimDAC = +/-10 => approx. +/-0.1V)
    const int limit = 12; // arbitrary
    if (trimDAC < -limit || trimDAC > limit) // clamp
	trimDAC = (trimDAC > limit) ? limit : -limit;
    float newOffs = dev().m_freqOffset + trimDAC;
    if (m_trace)
	Debug(dev().owner(),(!m_driftPpb) ? DebugInfo : DebugNote,
	    "VCTCXO discipliner: changing FrequencyOffset %g -> %g drift=%dppb [%p]",
	    dev().m_freqOffset,newOffs,drift,dev().owner());
    // trim the VCTCXO
    unsigned status = dev().setFreqOffset(newOffs,0,false);
    if (status) {
	// postpone activity for a minute to avoid a flood of debug messages
	postponeActivity(1);
	XDebug(dev().owner(),DebugNote,
	    "VCTCXO discipliner: failed to set FrequencyOffset to %g status=%u %s [%p]",
	    newOffs,status,RadioInterface::errorName(status),dev().owner());
	return true;
    }
    postponeActivity(1,true);
    // no more actions to be done if this was a forced drift correction
    if (!m_driftPpb)
	return true;
    // enqueue a feedback message with the adjusted frequency offset
    dev().notifyFreqOffs();
    // clear the pending drift
    m_driftPpb = 0;
    // decrease the number of scheduled trims, unless toggled on (-1)
    if (m_trimsLeft > 0) {
	m_trimsLeft--;
	if (!m_trimsLeft)
	    Debug(dev().owner(),DebugNote,
		"Frequency calibration is stopping (count=0) [%p]",dev().owner());
	else if (m_trace)
	    Debug(dev().owner(),DebugInfo,"VCTCXO discipliner: %d trims left [%p]",
		m_trimsLeft,dev().owner());
    }
    return true;
}

int BrfVctcxoDiscipliner::measureDrift(uint64_t& samples, uint64_t& timestamp, uint16_t& delay)
{
    samplesAndTimestamp(samples,timestamp,delay);
    // revoke the measurement results for invalid samples or timestamp
    if (samples < m_samples || timestamp < m_timestamp)
	samples = 0;
    if (!samples) {
	XDebug(dev().owner(),DebugInfo,"VCTCXO discipliner: invalid sample to timestamp pinning,"
	    " failed to measure drift [%p]",dev().owner());
	return 0;
    }
    // compute the average sample rate for the current interval,
    // expressed in Hz (the microsec timestamp gets converted to sec)
    double sampleRate = (double)(samples - m_samples) / (1.0e-6 * (timestamp - m_timestamp));
    int drift = 1.0e9 * (sampleRate / m_confSampleRate - 1);
    if (m_trace)
	Debug(dev().owner(),DebugInfo,"VCTCXO discipliner: measured drift=%dppb sampleRate "
	    "current=%f configured=%u deltaSamples=" FMT64U " deltaTs=" FMT64U " [%p]",
	drift,sampleRate,m_confSampleRate,samples - m_samples,timestamp - m_timestamp,dev().owner());
    return drift;
}

void BrfVctcxoDiscipliner::samplesAndTimestamp(uint64_t& samples, uint64_t& timestamp,
    uint16_t& delay, unsigned maxIter)
{
    static unsigned int s_stop = RadioInterface::NotInitialized | RadioInterface::NotCalibrated |
	RadioInterface::Cancelled;
    samples = 0;
    delay = m_maxDelay + 1;
    unsigned timeouts = 0;
    unsigned i = 0;
    for (; i < maxIter; i++) {
	uint64_t tempSamples = 0;
	uint64_t tempTs = 0;
	uint16_t tempDelay = 0;
	String serializeErr;
	Thread::yield();
	unsigned status = dev().samplesAndTimestamp(tempSamples,tempTs,tempDelay,&serializeErr);
	if (status) {
	    if (0 != (status & s_stop)) {
		postponeActivity(1);
		return;
	    }
	    if (status == RadioInterface::Failure && serializeErr)
		timeouts++;
	    else if (status & RadioInterface::FatalErrorMask) {
		disableDiscipline();
		return;
	    }
	}
	// drop invalid and imprecise measurements
	if (!tempSamples || tempDelay > delay)
	    continue;
	// higher accuracy measurement
	delay = tempDelay;
	samples = tempSamples;
	timestamp = tempTs;
	if (delay < m_knownDelay) {
	    if (m_trace)
		Debug(dev().owner(),DebugInfo,"VCTCXO discipliner: known delay changed %u -> %u [%p]",
		    m_knownDelay,delay * 19 / 20,dev().owner());
	    m_knownDelay = delay * 19 / 20;
	    scheduleNextPinning(m_delay);
	}
	// optimal measurement
	if (delay < m_bestDelay)
	    break;
    }
    if (m_trace)
	Debug(dev().owner(),(delay < m_maxDelay) ? DebugInfo : DebugNote,
	    "VCTCXO discipliner: got samples=" FMT64U " timestamp=%f delay=%u "
	    "(max=%u best=%u known=%u) iteration %u/%u timeouts=%u [%p]",
	    samples,1.0e-6 * timestamp,delay,m_maxDelay,m_bestDelay,
	    m_knownDelay,i,maxIter,timeouts,dev().owner());
}


//
// BrfLibUsbDevice
//
#define BRF_TX_SERIALIZE_(waitNow,instr) \
    BrfSerialize txSerialize(this,true,waitNow); \
    if (txSerialize.status) \
	instr
#define BRF_TX_SERIALIZE             BRF_TX_SERIALIZE_(true,return txSerialize.status)
#define BRF_TX_SERIALIZE_CHECK_DEV(loc) \
    BRF_TX_SERIALIZE_(true,return txSerialize.status); \
    txSerialize.status = checkDev(loc); \
    if (txSerialize.status) \
	return txSerialize.status;
#define BRF_TX_SERIALIZE_CHECK_PUB_ENTRY(internal,loc) \
    BRF_TX_SERIALIZE; \
    txSerialize.status = checkPubFuncEntry(internal,loc); \
    if (txSerialize.status) \
	return txSerialize.status;
#define BRF_TX_SERIALIZE_NONE        BRF_TX_SERIALIZE_(true,return)

#define BRF_RX_SERIALIZE_(waitNow,instr) \
    BrfSerialize rxSerialize(this,false,waitNow); \
    if (rxSerialize.status) \
	instr
#define BRF_RX_SERIALIZE             BRF_RX_SERIALIZE_(true,return rxSerialize.status)
#define BRF_RX_SERIALIZE_CHECK_PUB_ENTRY(internal,loc) \
    BRF_RX_SERIALIZE; \
    rxSerialize.status = checkPubFuncEntry(internal,loc); \
    if (rxSerialize.status) \
	return rxSerialize.status;
#define BRF_RX_SERIALIZE_NONE        BRF_RX_SERIALIZE_(true,return)

BrfLibUsbDevice::BrfLibUsbDevice(BrfInterface* owner)
    : m_owner(owner),
    m_initialized(false),
    m_exiting(false),
    m_closing(false),
    m_closingDevice(false),
    m_notifyOff(false),
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
    m_syncTout(s_lusbSyncTransferTout),
    m_syncSemaphore(1,"BrfSync",1),
    m_ctrlTout(s_lusbCtrlTransferTout),
    m_bulkTout(s_lusbBulkTransferTout),
    m_altSetting(BRF_ALTSET_INVALID),
    m_rxShowDcInfo(0),
    m_rxDcOffsetMax(BRF_RX_DC_OFFSET_DEF),
    m_rxDcAvgI(0),
    m_rxDcAvgQ(0),
    m_freqOffset(BRF_FREQ_OFFS_DEF),
    m_txGainCorrSoftware(true),
    m_txIO(true),
    m_rxIO(false),
    m_syncTxStateSet(false),
    m_syncTxStateCode(0),
    m_rxTimestamp(0),
    m_rxResyncCandidate(0),
    m_rxTsPastIntervalMs(200),
    m_rxTsPastSamples(0),
    m_warnClamped(0),
    m_minBufsSend(1),
    m_silenceTimeMs(0),
    m_silenceTs(0),
    m_txPowerBalance(1),
    m_txPowerBalanceChanged(false),
    m_txPowerScaleI(1),
    m_txPowerScaleQ(1),
    m_wrPowerScaleI(s_sampleEnergize),
    m_wrPowerScaleQ(s_sampleEnergize),
    m_wrMaxI(s_sampleEnergize),
    m_wrMaxQ(s_sampleEnergize),
    m_gainExpBreak(0),
    m_gainExpSlope(0),
    m_phaseExpBreak(0),
    m_phaseExpSlope(0),
    m_ampTableUse(false),
    m_rxAlterDataParams(""),
    m_rxAlterData(false),
    m_rxAlterIncrement(0),
    m_rxAlterTsJumpSingle(true),
    m_rxAlterTsJumpPos(0),
    m_txPatternChanged(false),
    m_txPatternBufPos(0),
    m_calLms(false),
    m_calibrateStatus(0),
    m_calibrateStop(0),
    m_calibration(""),
    m_calThread(0),
    m_sendThread(0),
    m_recvThread(0),
    m_internalIoSemaphore(1,"BrfDevSyncThreads",1),
    m_internalIoTimestamp(0),
    m_internalIoTxRate(0),
    m_internalIoRxRate(0),
    m_internalIoRateChanged(false),
    m_threadMutex("BrfDevInternalThread")
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
    m_state.m_rx.vga1 = BRF_RXVGA1_GAIN_MAX + 1;
    m_state.m_rx.dcOffsetI = BRF_RX_DC_OFFSET_MAX + 1;
    m_state.m_rx.dcOffsetQ = BRF_RX_DC_OFFSET_MAX + 1;
    m_state.m_tx.vga1 = BRF_TXVGA1_GAIN_MIN - 1;
    m_state.m_tx.vga2 = BRF_TXVGA2_GAIN_MIN - 1;
    m_state.m_tx.dcOffsetI = BRF_RX_DC_OFFSET_MAX + 1;
    m_state.m_tx.dcOffsetQ = BRF_RX_DC_OFFSET_MAX + 1;
    initRadioCaps(m_radioCaps);
}

BrfLibUsbDevice::~BrfLibUsbDevice()
{
    DDebug(&__plugin,DebugAll,"~BrfLibUsbDevice(%p) [%p]",m_owner,this);
    doClose();
}

unsigned int BrfLibUsbDevice::setTxPattern(const String& pattern, float gain)
{
    Lock lck(m_dbgMutex);
    if (m_state.m_txPattern == pattern && m_state.m_txPatternGain == gain)
	return 0;
    ComplexVector buf;
    unsigned int status = 0;
    String e;
    unsigned int pLen = 0;
    if (pattern &&
	!buildVector(e,pattern,buf,totalSamples(true),false,true,false,&pLen,gain)) {
	Debug(m_owner,DebugNote,"Invalid tx pattern '%s': %s [%p]",
	    pattern.c_str(),e.c_str(),m_owner);
	status = RadioInterface::Failure;
    }
    if (!status && buf.length()) {
	m_txPattern = buf;
	m_state.m_txPattern = pattern;
	m_state.m_txPatternGain = gain;
	if (m_owner && m_owner->debugAt(DebugNote)) {
	    String s;
	    if (!pLen)
		pLen = m_txPattern.length();
	    if (pLen > 30)
		pLen = 30;
	    m_txPattern.head(pLen).dump(s,Math::dumpComplex," ","%g,%g");
	    if (s.startsWith(m_state.m_txPattern))
		s.clear();
	    else
		s.printf(1024,"HEAD[%u]: %s",pLen,s.c_str());
	    Debug(m_owner,DebugInfo,"TX pattern set to '%s' gain=%.3f len=%u [%p]%s",
		m_state.m_txPattern.substr(0,100).c_str(),m_state.m_txPatternGain,
		m_txPattern.length(),m_owner,encloseDashes(s,true));
	}
    }
    else {
	if (m_state.m_txPattern)
	    Debug(m_owner,DebugInfo,"TX pattern cleared [%p]",m_owner);
	m_txPattern.resetStorage(0);
	m_state.m_txPattern.clear();
	m_state.m_txPatternGain = 1;
    }
    m_txPatternChanged = true;
    return status;
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
    BRF_TX_SERIALIZE_CHECK_DEV("dumpPeripheral()");
    if (dev != UartDevSI5338) {
	addr = clampInt(addr,0,0x7f);
	len = clampInt(len,1,128 - addr);
    }
    else {
	addr = clampInt(addr,0,256);
	len = clampInt(len,1,257 - addr);
    }
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
    checkLimit(false,params->getIntValue(YSTRING("rxchecklimit"),0));
    updateAlterData(*params);
    const NamedString* p = params->getParam(YSTRING("rxoutsw"));
    if (p)
	setRxOut(p->toBoolean());
    m_trace = params->getBoolValue(YSTRING("trace_discipliner"));
    if (!m_dumpDelays)
	m_dumpDelays = params->getIntValue(YSTRING("trace_discipliner_delays"),0,0);
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
unsigned int BrfLibUsbDevice::open(const NamedList& params, String& error)
{
    BRF_RX_SERIALIZE;
    BRF_TX_SERIALIZE;
    doClose();
    String e;
    unsigned int status = 0;
    while (true) {
	m_calLms = params.getBoolValue(YSTRING("lms_autocal"));
	m_serial = params[YSTRING("serial")];
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
	BRF_FUNC_CALL_BREAK(lusbSetAltInterface(BRF_ALTSET_IDLE,&e));
	BRF_FUNC_CALL_BREAK(openChangeLms(params,&e));
	BrfDevTmpAltSet tmpAltSet(this,status,&e,"Open device");
	if (status)
	    break;
	uint8_t data = 0;
	BRF_FUNC_CALL_BREAK(lmsRead(0x04,data,&e));
	m_lmsVersion.printf("0x%x (%u.%u)",data,(data >> 4),(data & 0x0f));
	BRF_FUNC_CALL_BREAK(tmpAltSet.restore());
	m_freqOffset = clampFloatParam(params,YSTRING("RadioFrequencyOffset"),
	    BRF_FREQ_OFFS_DEF,BRF_FREQ_OFFS_MIN,BRF_FREQ_OFFS_MAX);
	m_txGainCorrSoftware = params.getBoolValue(YSTRING("tx_fpga_corr_gain_software"),true);
	bool superSpeed = (speed() == LIBUSB_SPEED_SUPER);
	m_maxDelay = clampIntParam(params,YSTRING("max_delay"),superSpeed ?
	    BRF_MAX_DELAY_SUPER_SPEED_DEF : BRF_MAX_DELAY_HIGH_SPEED_DEF,100,2000);
	m_bestDelay = clampIntParam(params,YSTRING("best_delay"),superSpeed ?
	    BRF_BEST_DELAY_SUPER_SPEED_DEF : BRF_BEST_DELAY_HIGH_SPEED_DEF,100,m_maxDelay);
	m_knownDelay = clampIntParam(params,YSTRING("known_delay"),superSpeed ?
	    BRF_KNOWN_DELAY_SUPER_SPEED_DEF : BRF_KNOWN_DELAY_HIGH_SPEED_DEF,100,m_bestDelay);
	m_systemAccuracy = clampIntParam(params,YSTRING("system_accuracy"),
	    BRF_SYSTEM_ACCURACY_DEF,100,2000);
	m_accuracyPpb = clampIntParam(params,YSTRING("accuracy_ppb"),BRF_ACCURACY_PPB_DEF,10,200);
	// Init TX/RX buffers
	m_rxResyncCandidate = 0;
	m_state.m_rxDcAuto = params.getBoolValue("rx_dc_autocorrect",true);
	m_rxShowDcInfo = params.getIntValue("rx_dc_showinfo");
	m_rxDcOffsetMax = BRF_RX_DC_OFFSET_DEF;
	m_state.m_rx.dcOffsetI = BRF_RX_DC_OFFSET_MAX + 1;
	m_state.m_rx.dcOffsetQ = BRF_RX_DC_OFFSET_MAX + 1;
	int tmpInt = 0;
	int i = 0;
	int q = 0;
	i = clampIntParam(params,"RX.OffsetI",0,-BRF_RX_DC_OFFSET_MAX,BRF_RX_DC_OFFSET_MAX);
	q = clampIntParam(params,"RX.OffsetQ",0,-BRF_RX_DC_OFFSET_MAX,BRF_RX_DC_OFFSET_MAX);
	BRF_FUNC_CALL_BREAK(internalSetCorrectionIQ(false,i,q,&e));
	BRF_FUNC_CALL_BREAK(internalEnableRxVga(true,true,&e));
	BRF_FUNC_CALL_BREAK(internalEnableRxVga(true,false,&e));
	i = clampIntParam(params,"TX.OffsetI",0,BRF_TX_DC_OFFSET_MIN,BRF_TX_DC_OFFSET_MAX);
	q = clampIntParam(params,"TX.OffsetQ",0,BRF_TX_DC_OFFSET_MIN,BRF_TX_DC_OFFSET_MAX);
	BRF_FUNC_CALL_BREAK(internalSetCorrectionIQ(true,i,q,&e));
	// Set RX gain
	m_state.m_rx.vga1 = BRF_RXVGA1_GAIN_MAX + 1;
	BRF_FUNC_CALL_BREAK(internalSetGain(false,BRF_RXVGA2_GAIN_MIN));
	// Pre/post mixer TX VGA
	m_state.m_tx.vga1Changed = false;
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
	if (!m_txGainCorrSoftware) {
	    const String& txPB = params["tx_powerbalance"];
	    internalSetTxIQBalance(false,txPB.toDouble(1),"tx_powerbalance");
	}
	// Set some optional params
	setTxPattern(params["txpattern"]);
	showBuf(true,params.getIntValue("txbufoutput",0),
	    params.getBoolValue("txbufoutput_nodata"));
	showBuf(false,params.getIntValue("rxbufoutput",0),
	    params.getBoolValue("rxbufoutput_nodata"));
	m_silenceTimeMs = clampIntParam(params,"silence_time",5000,0,60000);
	m_rxTsPastIntervalMs = clampIntParam(params,"rx_ts_past_error_interval",200,50,10000);
	const String& sRateSamples = params[YSTRING("srate_buffered_samples")];
	if (sRateSamples) {
	    const char* s = BrfBufsThreshold::init(m_bufThres,sRateSamples,m_radioCaps);
	    if (s)
		Debug(m_owner,DebugConf,"Failed to parse srate_buffered_samples='%s': %s [%p]",
		    sRateSamples.c_str(),s,m_owner);
	}
	break;
    }
    if (status) {
	Debug(m_owner,DebugWarn,"Failed to open USB device: %s [%p]",
	    e.safe("Unknown error"),m_owner);
	doClose();
	error = e;
	return status;
    }
    String s;
    internalDumpDev(s,true,false,"\r\n",true);
    Debug(m_owner,DebugAll,"Opened device [%p]%s",m_owner,encloseDashes(s,true));
    txSerialize.drop();
    rxSerialize.drop();
    reLoad(&params);
    return status;
}

// Initialize operating parameters
unsigned int BrfLibUsbDevice::initialize(const NamedList& params)
{
    BRF_RX_SERIALIZE;
    BRF_TX_SERIALIZE_CHECK_PUB_ENTRY(false,"initialize()");
    if (m_initialized)
	return 0;
    String s;
    //params.dump(s,"\r\n");
    Debug(m_owner,DebugAll,"Initializing ... [%p]%s",m_owner,encloseDashes(s,true));
    String e;
    unsigned int status = 0;
    while (true) {
	// Check for radio operating params
	// FILTER, SAMPLERATE, BAND, TX/RX FREQ
	const String& bw = params["filter"];
	if (bw) {
	    unsigned int tmp = bw.toInteger(1,0,1);
	    BRF_FUNC_CALL_BREAK(internalSetLpfBandwidthBoth(tmp,&e));
	}
	const String& sr = params["samplerate"];
	if (sr) {
	    unsigned int tmp = sr.toInteger(1,0,1);
	    BRF_FUNC_CALL_BREAK(internalSetSampleRateBoth(tmp,&e));
	}
	for (int i = 0; i < 2; i++) {
	    bool tx = (i == 0);
	    const NamedString* ns = params.getParam(tx ? "txfrequency" : "rxfrequency");
	    if (!ns)
		continue;
	    BRF_FUNC_CALL_BREAK(internalSetFrequency(tx,ns->toInt64(),&e));
	}
	if (status)
	    break;
	BRF_FUNC_CALL_BREAK(internalPowerOn(true,true,true,&e));
	break;
    }
    if (!status) {
	txSerialize.drop();
	rxSerialize.drop();
	m_initialized = true;
	if (params.getBoolValue(YSTRING("calibrate"))) {
	    NamedList tmp("");
	    tmp.copySubParams(params,"calibrate_");
	    status = calibrate(tmp.getBoolValue(YSTRING("sync")),tmp,&e,true);
	}
	else {
	    m_notifyOff = true;
	    Message* m = buildNotify("start");
	    BrfDevDirState& dir = getDirState(true);
	    m->addParam("tx_frequency",String(dir.frequency));
	    m->addParam("tx_samplerate",String(dir.sampleRate));
	    m->addParam("tx_filter",String(dir.lpfBw));
	    Engine::dispatch(*m);
	    // Lock TX. Re-check our state. Apply params
	    txSerialize.wait();
	    if (!txSerialize.status) {
		status = checkPubFuncEntry(false,"initialize()");
		if (!status)
		    status = applyStartParams(*m,&e);
	    }
	    else
		status = txSerialize.status;
	    TelEngine::destruct(m);
	}
	if ((!status || status == RadioInterface::Pending) &&
	    m_owner && m_owner->debugAt(DebugAll)) {
	    String s;
#ifdef DEBUG
	    if (!status)
		internalDumpDev(s,false,true,"\r\n",true,true,true);
#endif
	    Debug(m_owner,DebugAll,"Initialized [%p]%s",m_owner,encloseDashes(s,true));
	}
	if (!status)
	    return 0;
    }
    if (status != RadioInterface::Pending)
	Debug(m_owner,DebugGoOn,"Failed to initialize: %s [%p]",
	    e.safe("Unknown error"),m_owner);
    return status;
}

// Check if parameters are set
unsigned int BrfLibUsbDevice::isInitialized(bool checkTx, bool checkRx, String* error)
{
    if (!m_initialized)
	return setErrorNotInit(error);
    for (int i = 0; i < 2; i++) {
	bool tx = (i == 0);
	if ((tx && !checkTx) || (!tx && !checkRx))
	    continue;
	BrfDevDirState& s = getDirState(tx);
	if (!s.frequency)
	    return setErrorNotInit(error,String(brfDir(tx)) + " frequency not set");
	if (!s.sampleRate)
	    return setErrorNotInit(error,String(brfDir(tx)) + " sample rate not set");
	if (!s.lpfBw)
	    return setErrorNotInit(error,String(brfDir(tx)) + " filter bandwidth not set");
    }
    return 0;
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
    BRF_RX_SERIALIZE;
    BRF_TX_SERIALIZE_CHECK_PUB_ENTRY(false,"powerOn()");
    return internalPowerOn(true);
}

// Send an array of samples waiting to be transmitted
unsigned int BrfLibUsbDevice::syncTx(uint64_t ts, float* data, unsigned int samples,
    float* powerScale, bool internal)
{
    BRF_TX_SERIALIZE_CHECK_PUB_ENTRY(internal,"syncTx()");
    unsigned int status = send(ts,data,samples,powerScale);
    if (status == RadioInterface::HardwareIOError) {
	txSerialize.drop();
	Thread::yield();
    }
    return status;
}

// Receive data from the Rx interface of the bladeRF device
unsigned int BrfLibUsbDevice::syncRx(uint64_t& ts, float* data, unsigned int& samples,
    String* error, bool internal)
{
    BRF_RX_SERIALIZE_CHECK_PUB_ENTRY(internal,"syncRx()");
    unsigned int status = recv(ts,data,samples,error);
    if (status == RadioInterface::HardwareIOError) {
	rxSerialize.drop();
	Thread::yield();
    }
    return status;
}

// Capture RX data
unsigned int BrfLibUsbDevice::capture(bool tx, float* buf, unsigned int samples,
    uint64_t& ts, String* error)
{
    if (!(buf && samples))
	return 0;
    BrfDevIO& io = getIO(tx);
    Lock lck(io.captureMutex);
    if (io.captureBuf)
	return setErrorFail(error,"Duplicate capture");
    io.captureSamples = samples;
    io.captureTs = ts;
    io.captureOffset = 0;
    io.captureStatus = 0;
    io.captureError.clear();
    io.captureBuf = buf;
    lck.drop();
    unsigned int tout = ((samples + 999) / 1000) * 20;
    unsigned int status = 0;
    unsigned int intervals = threadIdleIntervals(tout);
    while (!status && io.captureBuf) {
	io.captureSemaphore.lock(Thread::idleUsec());
	status = cancelled(error);
	if (!status && ((intervals--) == 0))
	    status = setErrorTimeout(error,"Capture timeout");
    }
    lck.acquire(io.captureMutex);
    if (!io.captureBuf) {
	ts = io.captureTs;
	if (io.captureStatus && error)
	    *error = io.captureError;
	return io.captureStatus;
    }
    io.captureBuf = 0;
    return status;
}

unsigned int BrfLibUsbDevice::setFrequency(uint64_t hz, bool tx)
{
    BRF_TX_SERIALIZE_CHECK_PUB_ENTRY(false,"setFrequency()");
    return internalSetFrequency(tx,hz);
}

unsigned int BrfLibUsbDevice::getFrequency(uint32_t& hz, bool tx)
{
    BRF_TX_SERIALIZE_CHECK_DEV("getFrequency()");
    return internalGetFrequency(tx,&hz);
}

// Set frequency offset
unsigned int BrfLibUsbDevice::setFreqOffset(float offs, float* newVal, bool stopAutoCal)
{
    BRF_TX_SERIALIZE_CHECK_PUB_ENTRY(false,"setFreqOffset()");
    unsigned int status = internalSetFreqOffs(offs,newVal);
    txSerialize.drop();
    if (!status && stopAutoCal && getDirState(true).rfEnabled)
	disableDiscipline(true);
    return status;
}

// Get frequency offset
unsigned int BrfLibUsbDevice::getFreqOffset(float& offs)
{
    BRF_TX_SERIALIZE_CHECK_DEV("getFreqOffset()");
    String val;
    unsigned int status = getCalField(val,"DAC","DAC_TRIM");
    if (status == 0) {
	offs = val.toInteger() / 256.0;
	// TODO m_freqOffset = offs ???
    }
    return status;
}

// Set the bandwidth for a specific module
unsigned int BrfLibUsbDevice::setLpfBandwidth(uint32_t band, bool tx)
{
    BRF_TX_SERIALIZE_CHECK_PUB_ENTRY(false,"setLpfBandwidth()");
    return internalSetLpfBandwidth(tx,band);
}

// Get the bandwidth for a specific module
unsigned int BrfLibUsbDevice::getLpfBandwidth(uint32_t& band, bool tx)
{
    BRF_TX_SERIALIZE_CHECK_DEV("getLpfBandwidth()");
    String e;
    unsigned int status = lusbSetAltInterface(BRF_ALTSET_RF_LINK,&e);
    if (status == 0) {
	uint8_t data = 0;
	status = lmsRead(lmsLpfAddr(tx),data,&e);
	if (status == 0) {
	    data >>= 2;
	    data &= 0xf;
	    band = index2bw(15 - data);
	    getDirState(tx).lpfBw = band;
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
    BRF_TX_SERIALIZE_CHECK_PUB_ENTRY(false,"setLpf()");
    return internalSetLpf(tx,lpf);
}

unsigned int BrfLibUsbDevice::getLpf(int& lpf, bool tx)
{
    BRF_TX_SERIALIZE_CHECK_DEV("getLpf()");
    return internalGetLpf(tx,&lpf);
}

// Set the sample rate on a specific module
unsigned int BrfLibUsbDevice::setSamplerate(uint32_t value, bool tx)
{
    BRF_TX_SERIALIZE_CHECK_PUB_ENTRY(false,"setSamplerate()");
    return internalSetSampleRate(tx,value);
}

// Get the sample rate on a specific module
unsigned int BrfLibUsbDevice::getSamplerate(uint32_t& value, bool tx)
{
    BRF_TX_SERIALIZE_CHECK_DEV("getSamplerate()");
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
	getDirState(tx).sampleRate = value;
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
    BRF_TX_SERIALIZE_CHECK_PUB_ENTRY(false,"setTxVga()");
    return internalSetTxVga(vga,preMixer);
}

// Retrieve the pre/post mixer gain setting on transmission
unsigned int BrfLibUsbDevice::getTxVga(int& vga, bool preMixer)
{
    BRF_TX_SERIALIZE_CHECK_DEV("getTxVga()");
    return internalGetTxVga(&vga,preMixer);
}

// Set TX gain expansion
unsigned int BrfLibUsbDevice::setGainExp(float breakpoint, float max)
{
    m_gainExpBreak = pow(10,breakpoint*0.1);
    // the base slope is 1, so max gain of 1 is a slope of zero
    // we correct for the by subtracting 1 from the max
    // we will add it back after we compute the expansion factor
    m_gainExpSlope = (max-1) / (2-breakpoint);
    calculateAmpTable();
    return 0;
}

// Set TX phase expansion
unsigned int BrfLibUsbDevice::setPhaseExp(float breakpoint, float max)
{
    m_phaseExpBreak = pow(10,breakpoint*0.1);
    // convert max to radians
    max = max * M_PI / 180.0;
    m_phaseExpSlope = max / (2-breakpoint);
    calculateAmpTable();
    return 0;
}

unsigned int BrfLibUsbDevice::setTxIQBalance(float value)
{
    BRF_TX_SERIALIZE_CHECK_PUB_ENTRY(false,"setTxIQBalance()");
    return internalSetTxIQBalance(false,value);
}

// Enable or disable the pre/post mixer gain on the receive side
unsigned int BrfLibUsbDevice::enableRxVga(bool on, bool preMixer)
{
    BRF_TX_SERIALIZE_CHECK_PUB_ENTRY(false,"enableRxVga()");
    return internalEnableRxVga(on,preMixer);
}

// Set the pre-mixer gain setting on the receive side (interval [5..30])
// Set the post-mixer Rx gain setting (interval [0..30])
unsigned int BrfLibUsbDevice::setRxVga(int vga, bool preMixer)
{
    BRF_TX_SERIALIZE_CHECK_PUB_ENTRY(false,"setRxVga()");
    return internalSetRxVga(vga,preMixer);
}

// Retrieve the pre/post mixer rx gain setting
unsigned int BrfLibUsbDevice::getRxVga(int& vga, bool preMixer)
{
    BRF_TX_SERIALIZE_CHECK_DEV("getRxVga()");
    return internalGetRxVga(&vga,preMixer);
}

// Set pre and post mixer value
unsigned int BrfLibUsbDevice::setGain(bool tx, int val, int* newVal)
{
    BRF_TX_SERIALIZE_CHECK_PUB_ENTRY(false,"setGain()");
    return internalSetGain(tx,val,newVal);
}

// Run check / calibration procedure
unsigned int BrfLibUsbDevice::calibrate(bool sync, const NamedList& params,
    String* error, bool fromInit)
{
    BRF_RX_SERIALIZE;
    BRF_TX_SERIALIZE_CHECK_PUB_ENTRY(false,"calibrate()");
#ifdef DEBUG
    Debugger d(DebugAll,"CALIBRATE"," %s sync=%s [%p]",
	m_owner->debugName(),String::boolText(sync),m_owner);
#endif
    int rxDcAutoRestore = -1;
    String e;
    unsigned int status = 0;
    if (!m_initialized)
	status = setError(RadioInterface::NotInitialized,&e,"not initialized");
    BrfDuration duration;
    // force the VCTCXO discipliner to drop it's data
    if (sync)
	postponeActivity(1,true);
    while (!status) {
	if (!sync) {
	    if (m_owner && fromInit)
		m_owner->setPending(RadioInterface::PendingInitialize);
	    BRF_FUNC_CALL_BREAK(startCalibrateThreads(&e,params));
	    status = RadioInterface::Pending;
	    break;
	}
	BrfDevTmpAltSet tmpAltSet(this,status,&e,"Calibrate");
	if (status)
	    break;
	rxDcAutoRestore = setRxDcAuto(false) ? 1 : 0;
	m_calibrateStatus = Calibrating;
	Debug(m_owner,DebugInfo,"Calibrating ... [%p]",m_owner);
	// Drop lock. We are going to use public functions to calibrate
	txSerialize.drop();
	rxSerialize.drop();
	// LMS autocalibration
	if (params.getBoolValue("device_autocal",true))
	    BRF_FUNC_CALL_BREAK(calibrateAuto(&e));
	// Check
	if (params.getBoolValue("loopback_check",true))
	    BRF_FUNC_CALL_BREAK(loopbackCheck(&e));
	// Baseband calibration
	BRF_FUNC_CALL_BREAK(calibrateBaseband(&e));
	break;
    }
    duration.stop();
    if (rxDcAutoRestore > 0)
	setRxDcAuto(true);
    // Avoid hard cancelling if we are in calibration thread
    if (m_calThread && m_calThread == Thread::current())
	m_calThread = 0;
    if (sync) {
	stopThreads();
	if (m_owner && fromInit)
	    m_owner->setPending(RadioInterface::PendingInitialize,status);
	// Calibration done
	m_calibrateStatus = status ? Calibrate : Calibrated;
	// Notify
	Message* m = buildNotify("calibrated");
	if (!status)
	    m->copyParams(m_calibration);
	else
	    m_owner->setError(*m,status,e);
	Engine::enqueue(m);
	if (!status) {
	    Debug(m_owner,DebugInfo,"Calibration finished in %s [%p]",
		duration.secStr(),m_owner);
	    return 0;
	}
    }
    else if (RadioInterface::Pending == status) {
	Debug(m_owner,DebugAll,"Async calibration started [%p]",m_owner);
	return status;
    }
    return showError(status,e.c_str(),"Calibration failed",error,DebugWarn);
}

// Set Tx/Rx DC I/Q offset correction
unsigned int BrfLibUsbDevice::setDcOffset(bool tx, bool i, int16_t value)
{
    int rxDcAutoRestore = -1;
    if (!tx) {
	// Temporary disable RX auto correct
	BRF_RX_SERIALIZE_CHECK_PUB_ENTRY(false,"setDcOffset()");
	rxDcAutoRestore = setRxDcAuto(false) ? 1 : 0;
    }
    BrfSerialize txSerialize(this,true,true);
    if (!txSerialize.status)
	txSerialize.status = checkPubFuncEntry(false,"setDcOffset()");
    if (txSerialize.status) {
	if (rxDcAutoRestore > 0)
	    m_state.m_rxDcAuto = true;
	return txSerialize.status;
    }
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
	m_state.m_rxDcAuto = true;
    return status;
}

// Retrieve Tx/Rx DC I/Q offset correction
unsigned int BrfLibUsbDevice::getDcOffset(bool tx, bool i, int16_t& value)
{
    BRF_TX_SERIALIZE_CHECK_DEV("getDcOffset()");
    return internalGetDcOffset(tx,i,&value);
}

unsigned int BrfLibUsbDevice::setFpgaCorr(bool tx, int corr, int16_t value)
{
    BRF_TX_SERIALIZE_CHECK_PUB_ENTRY(false,"setFpgaCorr()");
    return internalSetFpgaCorr(tx,corr,value);
}

unsigned int BrfLibUsbDevice::getFpgaCorr(bool tx, int corr, int16_t& value)
{
    BRF_TX_SERIALIZE_CHECK_DEV("getFpgaCorr()");
    int16_t v = 0;
    unsigned int status = internalGetFpgaCorr(tx,corr,&v);
    value = v;
    return status;
}

unsigned int BrfLibUsbDevice::getTimestamp(bool tx, uint64_t& ts)
{
    BRF_TX_SERIALIZE_CHECK_DEV("getTimestamp()");
    return internalGetTimestamp(tx,ts);
}

unsigned int BrfLibUsbDevice::samplesAndTimestamp(uint64_t& samples, uint64_t& timestamp,
    uint16_t& delay, String* serializeErr)
{
    BrfSerialize txSerialize(this,true,false);
    txSerialize.wait(serializeErr,12000);
    if (!txSerialize.status)
	txSerialize.status = checkDev("samplesAndTimestamp()");
    if (!txSerialize.status) {
	uint64_t initial = Time::now();
	txSerialize.status = internalGetTimestamp(true,samples);
	timestamp = Time::now();
	if (!txSerialize.status && timestamp > initial) {
	    delay = timestamp - initial;
	    timestamp = (timestamp + initial) / 2;
	    return 0;
	}
    }
    samples = 0;
    return txSerialize.status;
}

unsigned int BrfLibUsbDevice::writeLMS(uint8_t addr, uint8_t value, uint8_t* rst,
    String* error, bool internal)
{
    BRF_TX_SERIALIZE_CHECK_PUB_ENTRY(internal,"writeLMS()");
    if (rst)
	return lmsSet(addr,value,*rst,error);
    return lmsWrite(addr,value,error);
}

unsigned int BrfLibUsbDevice::writeLMS(const String& str, String* error, bool internal)
{
    if (!str)
	return 0;
    BRF_TX_SERIALIZE_CHECK_PUB_ENTRY(internal,"writeLMS()");
    return lmsWrite(str,!internal,error);
}

// Read LMS register(s)
unsigned int BrfLibUsbDevice::readLMS(uint8_t addr, uint8_t& value, String* error,
    bool internal)
{
    BRF_TX_SERIALIZE_CHECK_PUB_ENTRY(internal,"readLMS()");
    return lmsRead(addr & 0x7f,value,error);
}

unsigned int BrfLibUsbDevice::readLMS(String& dest, const String* read,
    bool readIsInterleaved, String* error, bool internal)
{
    BRF_TX_SERIALIZE_CHECK_PUB_ENTRY(internal,"readLMS()");
    return lmsRead(dest,read,readIsInterleaved,error);
}

// Check LMS registers
unsigned int BrfLibUsbDevice::checkLMS(const String& what, String* error, bool internal)
{
    if (!what)
	return 0;
    BRF_TX_SERIALIZE_CHECK_PUB_ENTRY(internal,"checkLMS()");
    return lmsCheck(what,error);
}

unsigned int BrfLibUsbDevice::setLoopback(const char* name, const NamedList& params)
{
    int mode = LoopNone;
    if (!TelEngine::null(name))
	mode = lookup(name,s_loopback,LoopUnknown);
    if (mode == LoopUnknown) {
	Debug(m_owner,DebugNote,"Unknown loopback mode '%s' [%p]",name,m_owner);
	return RadioInterface::OutOfRange;
    }
    BRF_TX_SERIALIZE_CHECK_PUB_ENTRY(false,"setLoopback()");
    return internalSetLoopback(mode,params);
}

// Set parameter(s)
unsigned int BrfLibUsbDevice::setParam(const String& param, const String& value,
    const NamedList& params)
{
    if (!param)
	return 0;
    if (param == YSTRING("calibrate_bb_dc_dump")) {
	Lock lck(m_dbgMutex);
	m_bbCalDcFile = value;
    }
    else if (param == YSTRING("calibrate_bb_imbalance_dump")) {
	Lock lck(m_dbgMutex);
	m_bbCalImbalanceFile = value;
    }
    else if (param == YSTRING("device_check_dump")) {
	Lock lck(m_dbgMutex);
	m_devCheckFile = value;
    }
    else {
	Debug(m_owner,DebugNote,"Unknown device param '%s' [%p]",param.c_str(),m_owner);
	return RadioInterface::NotSupported;
    }
    Debug(m_owner,DebugAll,"Handled param set '%s'='%s' [%p]",
	param.c_str(),value.c_str(),m_owner);
    return 0;
}

static inline unsigned int ts2buffers(uint64_t ts, unsigned int len)
{
    return (unsigned int)((ts + len - 1) / len);
}

// Utility: run device send data
void BrfLibUsbDevice::runSend(BrfThread* th)
{
    if (!th)
	return;
    unsigned int samples = 0;
    unsigned int rxLatency = 0;
    unsigned int txBuffers = 0;
    ComplexVector buf;
    bool wait = true;
    uint64_t rxTs = 0;
    uint64_t ts = 0;
    unsigned int status = getTimestamp(true,ts);
    uint64_t silence = ts + 200000;
    bool paused = true;
    while (!status && (0 == cancelled())) {
	if (th->paused(true,ts,status) || status) {
	    if (!status)
		Thread::idle();
	    silence = ts + 200000;
	    wait = true;
	    setIoDontWarnTs(true);
	    paused = true;
	    continue;
	}
	else if (paused) {
	    paused = false;
	    samples = totalSamples(true);
	    if (!samples)
		break;
	    if (samples != buf.length()) {
		rxLatency = (m_radioCaps.rxLatency + samples - 1) / samples;
		txBuffers = (m_radioCaps.txLatency + samples - 1) / samples;
		buf.resetStorage(samples);
	    }
	}
	// Wait for RX
	if (wait) {
	    while (!status && !m_internalIoSemaphore.lock(Thread::idleUsec()))
		status = cancelled();
	    if (status)
		break;
	    if (th->isPaused()) {
		m_internalIoSemaphore.unlock();
		continue;
	    }
	    Lock lck(m_threadMutex);
	    rxTs = m_internalIoTimestamp;
	}
	else
	    wait = true;
	uint64_t crtRxTs = rxTs + rxLatency;
	unsigned int sendCount = txBuffers;
	if (ts >= crtRxTs) {
	    // TX time is at least RX time. Start sending from it
	    unsigned int diff = ts2buffers(ts - crtRxTs,samples);
	    if (sendCount > diff)
		sendCount -= diff;
	    else
		sendCount = 0;
	}
	else {
	    // Underrun. Start sending from RX time
	    if (crtRxTs > silence) {
		unsigned int u = ts2buffers(crtRxTs - ts,samples);
		if (u > 1)
		    Debug(m_owner,u > 5 ? DebugNote : DebugAll,
			"Internal transmit underrun by %u buffer(s) [%p]",u,m_owner);
		else
		    DDebug(m_owner,DebugAll,
			"Internal transmit underrun by %u buffer(s) [%p]",u,m_owner);
	    }
	    ts = crtRxTs;
	}
	while (!status && sendCount--) {
	    status = syncTx(ts,(float*)buf.data(),buf.length(),0,true);
	    ts += buf.length();
	}
	if (status)
	    break;
	// Look again at RX time
	Lock lck(m_threadMutex);
	if (m_internalIoTimestamp < crtRxTs) {
	    wait = false;
	    rxTs = crtRxTs;
	}
    }
}

// Utility: run device send data
void BrfLibUsbDevice::runRecv(BrfThread* th)
{
    if (!th)
	return;
    ComplexVector buf(totalSamples(false));
    uint64_t ts = 0;
    unsigned int status = getTimestamp(false,ts);
    unsigned int txRate = 0;
    unsigned int rxRate = 0;
    m_internalIoRateChanged = true;
    bool paused = true;
    while (!status && (0 == cancelled())) {
	if (th->paused(false,ts,status) || status) {
	    if (!status) {
		m_internalIoSemaphore.unlock();
		Thread::idle();
		setIoDontWarnTs(false);
	    }
	    paused = true;
	    continue;
	}
	else if (paused) {
	    paused = false;
	    if (totalSamples(false) != buf.length())
		buf.resetStorage(totalSamples(false));
	}
	// Simulate some processing to avoid keeping the RX mutex locked
	generateExpTone(buf,0);
	buf.bzero();
	unsigned int len = buf.length();
	BRF_FUNC_CALL_BREAK(syncRx(ts,(float*)buf.data(),len,0,true));
	ts += len;
	m_threadMutex.lock();
	if (m_internalIoRateChanged) {
	    txRate = m_internalIoTxRate;
	    rxRate = m_internalIoRxRate;
	    m_internalIoRateChanged = false;
	}
	if (txRate != rxRate && txRate && rxRate)
	    m_internalIoTimestamp = (ts * txRate) / rxRate;
	else
	    m_internalIoTimestamp = ts;
	m_threadMutex.unlock();
	m_internalIoSemaphore.unlock();
    }
    m_internalIoSemaphore.unlock();
}

// Build notification message
Message* BrfLibUsbDevice::buildNotify(const char* status)
{
    Message* m = new Message("module.update",0,true);
    m->addParam("module",__plugin.name());
    m_owner->completeDevInfo(*m,true);
    m->addParam("status",status,false);
    return m;
}

// Release data
void BrfLibUsbDevice::destruct()
{
    doClose();
    for (unsigned int i = 0; i < EpCount; i++)
	m_usbTransfer[i].reset();
    GenObject::destruct();
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
	    return RadioInterface::HardwareNotAvailable;
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

void BrfLibUsbDevice::doClose()
{
    //Debugger d(DebugNote,"doClose "," %s [%p]",m_owner->debugName(),m_owner);
    m_closing = true;
    closeDevice();
    clearDeviceList();
    m_closing = false;
}

unsigned int BrfLibUsbDevice::setState(BrfDevState& state, String* error)
{
#define BRF_SET_STATE_COND(cond,flags,flag,func) { \
    if ((cond) && ((flags & flag) != 0)) { \
	unsigned int tmp = func; \
	if (tmp) { \
	    if (fatal) \
		return tmp; \
	    if (!status) { \
		error = 0; \
		status = tmp; \
	    } \
	} \
    } \
    flags &= ~flag; \
    if (!flags) \
	continue;\
}
#define BRF_SET_STATE(flags,flag,func) BRF_SET_STATE_COND(true,flags,flag,func)
#define BRF_SET_STATE_NOERROR(flags,flag,func) { \
    if ((flags & flag) != 0) { \
	func; \
	flags &= ~flag; \
    } \
    if (!flags) \
	continue;\
}
    unsigned int status = 0;
    BRF_FUNC_CALL_RET(cancelled(error));
    XDebug(m_owner,DebugAll,"Set state 0x%x / 0x%x / 0x%x [%p]",
	state.m_changed,state.m_txChanged,state.m_rxChanged,m_owner);
    bool fatal = (state.m_changed & DevStatAbortOnFail) != 0;
    state.m_changed &= ~DevStatAbortOnFail;
    // Direction related
    for (int i = 0; i < 2; i++) {
	bool tx = (i == 0);
	unsigned int& f = tx ? state.m_txChanged : state.m_rxChanged;
	if (!f)
	    continue;
	BrfDevDirState& s = tx ? state.m_tx : state.m_rx;
	BRF_SET_STATE(f,DevStatLpf,internalSetLpf(tx,s.lpf,error));
	BRF_SET_STATE_COND(s.lpfBw != 0,f,DevStatLpfBw,
	    internalSetLpfBandwidth(tx,s.lpfBw,error));
	BRF_SET_STATE_COND(s.sampleRate != 0,f,DevStatSampleRate,
	    internalSetSampleRate(tx,s.sampleRate,error));
	BRF_SET_STATE_COND(s.frequency != 0,f,DevStatFreq,
	    internalSetFrequency(tx,s.frequency,error));
	BRF_SET_STATE(f,DevStatVga1,internalSetVga(tx,s.vga1,true,error));
	BRF_SET_STATE(f,DevStatVga2,internalSetVga(tx,s.vga2,false,error));
	BRF_SET_STATE(f,DevStatDcI,internalSetDcOffset(tx,true,s.dcOffsetI,error));
	BRF_SET_STATE(f,DevStatDcQ,internalSetDcOffset(tx,false,s.dcOffsetQ,error));
	BRF_SET_STATE(f,DevStatFpgaPhase,internalSetFpgaCorr(tx,CorrFpgaPhase,
	    s.fpgaCorrPhase,error));
	BRF_SET_STATE(f,DevStatFpgaGain,internalSetFpgaCorr(tx,CorrFpgaGain,
	    s.fpgaCorrGain,error));
    }
    // Common
    while (state.m_changed) {
	BRF_SET_STATE(state.m_changed,DevStatLoopback,
	    internalSetLoopback(state.m_loopback,state.m_loopbackParams,error));
	BRF_SET_STATE_NOERROR(state.m_changed,DevStatRxDcAuto,
	    setRxDcAuto(state.m_rxDcAuto));
	BRF_SET_STATE_NOERROR(state.m_changed,DevStatTxPattern,
	    setTxPattern(state.m_txPattern,state.m_txPatternGain));
	break;
    }
    if (state.m_changed || state.m_txChanged || state.m_rxChanged)
	Debug(m_owner,DebugWarn,"Set state incomplete: 0x%x / 0x%x / 0x%x [%p]",
	    state.m_changed,state.m_txChanged,state.m_rxChanged,m_owner);
    return status;
#undef BRF_SET_STATE_COND
#undef BRF_SET_STATE
#undef BRF_SET_STATE_NOERROR
}

// Request changes (synchronous TX). Wait for change
unsigned int BrfLibUsbDevice::setStateSync(String* error)
{
    if (m_syncTxStateSet)
	return setErrorFail(error,"Sync set state overlapping");
    m_syncTxStateCode = 0;
    m_syncTxStateSet = true;
    unsigned int intervals = threadIdleIntervals(m_syncTout);
    unsigned int status = 0;
    while (m_syncTxStateSet && !status) {
	m_syncSemaphore.lock(Thread::idleUsec());
	status = cancelled(error);
	if (!status && m_syncTxStateSet && (intervals--) == 0)
	    status = setErrorTimeout(error,"Sync set state timeout");
    }
    m_syncTxStateSet = false;
    if (status)
	return status;
    if (!m_syncTxStateCode)
	return 0;
    return setError(m_syncTxStateCode,error,m_syncTxStateError);
}

void BrfLibUsbDevice::internalDumpDev(String& buf, bool info, bool state,
    const char* sep, bool internal, bool fromStatus, bool withHdr)
{
    String tmp;
    if (state) {
	BrfDevDirState& tx = getDirState(true);
	BrfDevDirState& rx = getDirState(false);
	if (withHdr) {
	    buf.append("RxVGA1=",sep) << rx.vga1;
	    buf << sep << "RxVGA2=" << rx.vga2;
	    buf << sep << "RxDCCorrI=" << rx.dcOffsetI;
	    buf << sep << "RxDCCorrQ=" << rx.dcOffsetQ;
	    buf << sep << "TxVGA1=" << tx.vga1;
	    buf << sep << "TxVGA2=" << tx.vga2;
	    buf << sep << dumpFloatG(tmp,(double)rx.frequency / 1000000,"RxFreq=","MHz");
	    if (internal) {
		buf << sep << "TxDCCorrI=" << tx.dcOffsetI;
		buf << sep << "TxDCCorrQ=" << tx.dcOffsetQ;
	    }
	    buf << sep << dumpFloatG(tmp,(double)tx.frequency / 1000000,"TxFreq=","MHz");
	    buf << sep << "FreqOffset=" << m_freqOffset;
	    buf << sep << "RxSampRate=" << rx.sampleRate;
	    buf << sep << "TxSampRate=" << tx.sampleRate;
	    buf << sep << "RxLpfBw=" << rx.lpfBw;
	    buf << sep << "TxLpfBw=" << tx.lpfBw;
	    buf << sep << "RxRF=" << onStr(rx.rfEnabled);
	    buf << sep << "TxRF=" << onStr(tx.rfEnabled);
	    if (internal) {
		buf << sep << "RxLPF=" << lookup(rx.lpf,s_lpf);
		buf << sep << "TxLPF=" << lookup(tx.lpf,s_lpf);
		buf << sep << "TxCorrFpgaPhase=" << tx.fpgaCorrPhase;
	    }
	}
	else {
	    buf << "|" << rx.vga1;
	    buf << "|" << rx.vga2;
	    buf << "|" << rx.dcOffsetI;
	    buf << "|" << rx.dcOffsetQ;
	    buf << "|" << tx.vga1;
	    buf << "|" << tx.vga2;
	    buf << "|" << dumpFloatG(tmp,(double)rx.frequency / 1000000,0,"MHz");
	    buf << "|" << dumpFloatG(tmp,(double)tx.frequency / 1000000,0,"MHz");
	    buf << "|" << m_freqOffset;
	    buf << "|" << rx.sampleRate;
	    buf << "|" << tx.sampleRate;
	    buf << "|" << rx.lpfBw;
	    buf << "|" << tx.lpfBw;
	    buf << "|" << onStr(rx.rfEnabled);
	    buf << "|" << onStr(tx.rfEnabled);
	}
    }
    if (!info)
	return;
    if (withHdr) {
	buf.append("Address=",sep) << address();
	buf << sep << "Serial=" << serial();
	buf << sep << "Speed=" << speedStr();
	buf << sep << "Firmware=" << fwVerStr();
	buf << sep << "FPGA=" << fpgaVerStr();
	if (!fromStatus) {
	    buf.append(fpgaFile()," - ");
	    buf.append(fpgaMD5()," - MD5: ");
	}
	buf << sep << "LMS_Ver=" << lmsVersion();
    }
    else {
	if (buf)
	    buf << "|";
	buf << address();
	buf << "|" << serial();
	buf << "|" << speedStr();
	buf << "|" << fwVerStr();
	buf << "|" << fpgaVerStr();
	buf << "|" << lmsVersion();
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
    bool warn = (tx != m_state.m_tx.rfEnabled) || (rx != m_state.m_rx.rfEnabled);
    while (status == 0) {
	if (tx || rx) {
	    BRF_FUNC_CALL_BREAK(enableTimestamps(true,&e));
	    if (m_calLms)
		BRF_FUNC_CALL_BREAK(calibrateAuto(&e));
	}
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
    BrfDevDirState& dirState = m_state.m_tx;
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
	if (m_calibrateStatus != Calibrating && io.timestamp &&
	    m_owner->debugAt(DebugAll)) {
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
    const long* ampTable = 0;
    if (m_ampTableUse)
	ampTable = m_ampTable;
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
	if (dirState.showPowerBalanceChange == 0)
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
	    if (m_txPatternBuffer.length() == 0) {
		brfCopyTxData(start,data,avail,*scaleI,*maxI,*scaleQ,*maxQ,clamped,ampTable);
		data += avail * 2;
	    }
	    else
		sendCopyTxPattern(start,avail,*scaleI,*maxI,*scaleQ,*maxQ,clamped,ampTable);
	    if (io.crtBufSampOffs >= io.bufSamples)
		io.advanceBuffer();
#ifdef XDEBUG
	    if (avail) {
		float sum = 0.0F;
		for (unsigned i=0; i<avail*2; i++) {
		    sum += start[i]*start[i];
		}
		const float rms = ::sqrtf(sum/avail);
		const float dB = 20.0F*log10f((rms+0.5F)/2048.0F);
		XDebug(m_owner,DebugAll,"energized ouput RMS level %f in linear amplitude (%f dB) [%p]",
			rms, dB, this);
	    }
#endif
	}
	unsigned int nBuf = io.crtBuf;
	unsigned int oldBufSampOffs = nBuf ? io.crtBufSampOffs : 0;
	if (m_syncTxStateSet) {
	    m_syncTxStateCode = setState(m_syncTxState,&m_syncTxStateError);
	    m_syncTxState.m_tx.m_timestamp = ts + nBuf * io.bufSamples;
	    m_syncTxStateSet = false;
    	    m_syncSemaphore.unlock();
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
    m_txPatternBuffer.steal(m_txPattern);
    if (m_txPatternBuffer.length())
	Debug(m_owner,DebugInfo,
	    "Using send pattern '%s' %u samples at TS=" FMT64U " [%p]",
	    m_state.m_txPattern.substr(0,50).c_str(),m_txPatternBuffer.length(),
	    m_txIO.timestamp,m_owner);
    m_txPatternBufPos = m_txPatternBuffer.length();
}

void BrfLibUsbDevice::sendCopyTxPattern(int16_t* buf, unsigned int avail,
    float scaleI, int16_t maxI, float scaleQ, int16_t maxQ, unsigned int& clamped,
    const long* ampTable)
{
    while (avail) {
	if (m_txPatternBufPos == m_txPatternBuffer.length())
	    m_txPatternBufPos = 0;
	unsigned int cp = m_txPatternBuffer.length() - m_txPatternBufPos;
	if (cp > avail)
	    cp = avail;
	float* b = (float*)m_txPatternBuffer.data(m_txPatternBufPos);
	avail -= cp;
	m_txPatternBufPos += cp;
	brfCopyTxData(buf,b,cp,scaleI,maxI,scaleQ,maxQ,clamped,ampTable);
    }
}

// Receive data from the Rx interface of the bladeRF device
// Remember: a sample is an I/Q pair
unsigned int BrfLibUsbDevice::recv(uint64_t& ts, float* data, unsigned int& samples,
    String* error)
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
	if (checkDbgInt(io.checkLimit))
	    ioBufCheckLimit(false);
	if (checkDbgInt(io.checkTs))
	    ioBufCheckTs(false);
	if (m_state.m_rxDcAuto || m_rxShowDcInfo)
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
    if (io.captureBuf)
	captureHandle(io,data,samples,ts,status,&e);
    if (status == 0) {
	m_rxIO.timestamp = ts;
	if (io.upDumpFile.valid() && !(checkDbgInt(io.upDump) &&
	    io.upDumpFile.write(ts,data,samplesf2bytes(samples),owner())))
	    io.upDumpFile.terminate(owner());
    }
    else if (error)
	return showError(status,e,"Recv failed",error);
    else if (status != RadioInterface::Cancelled)
	Debug(m_owner,DebugNote,"Recv failed: %s [%p]",e.c_str(),m_owner);
    return status;
}

void BrfLibUsbDevice::captureHandle(BrfDevIO& io, const float* buf, unsigned int samples,
    uint64_t ts, unsigned int status, const String* error)
{
//#define BRF_CAPTURE_HANDLE_TRACE
    Lock lck(io.captureMutex);
    if (!io.captureBuf)
	return;
    bool done = false;
    if (!status) {
	// Handle data
	unsigned int cp = 0;
	unsigned int bufOffs = 0;
	uint64_t tsCapture = io.captureTs + io.captureOffset;
	unsigned int samplesLeft = io.captureSamples - io.captureOffset;
#ifdef BRF_CAPTURE_HANDLE_TRACE
	Output("CAPTURE[%s] (%u) " FMT64U "/%u (" FMT64U ") IO: " FMT64U
	    "/%u (last=" FMT64U ") delta=" FMT64 " samplesLeft=%u",
	    brfDir(io.tx()),io.captureSamples,io.captureTs,io.captureOffset,tsCapture,
	    ts,samples,(ts + samples),(int64_t)(ts + samples - tsCapture),samplesLeft);
#endif
	if (tsCapture == ts)
	    cp = (samplesLeft < samples) ? samplesLeft : samples;
	else {
	    uint64_t lastTs = ts + samples;
	    bool useData = false;
	    bool reset = true;
	    if (tsCapture > ts) {
		useData = !io.captureOffset && (lastTs > tsCapture);
		reset = !useData && (lastTs >= tsCapture);
	    }
	    if (useData) {
		cp = (unsigned int)(lastTs - tsCapture);
		if (cp > samples)
		    cp = samples;
		if (cp > samplesLeft)
		    cp = samplesLeft;
		if (cp)
		    bufOffs = samples - cp;
	    }
	    else if (reset) {
		// Reset buffer (avoid data gaps)
		io.captureTs = lastTs;
		io.captureOffset = 0;
#ifdef BRF_CAPTURE_HANDLE_TRACE
		Output("  reset TS=" FMT64U,io.captureTs);
#endif
	    }
	}
	if (cp) {
	    unsigned int nCopy = samplesf2bytes(cp);
	    ::memcpy(io.captureBuf + 2 * io.captureOffset,buf + 2 * bufOffs,nCopy);
	    io.captureOffset += cp;
	    samplesLeft -= cp;
#ifdef BRF_CAPTURE_HANDLE_TRACE
	    Output("  cp=%u from=%u offset=%u samplesLeft=%u",
		cp,bufOffs,io.captureOffset,samplesLeft);
#endif
	}
	if (!samplesLeft) {
	    done = true;
	    io.captureStatus = 0;
	    io.captureError.clear();
	}
    }
    else {
	io.captureStatus = status;
	if (!TelEngine::null(error))
	    io.captureError = (io.tx() ? "Send failed: " : "Recv failed: ") + *error;
	else
	    io.captureError.clear();
	done = true;
    }
    if (!done)
	return;
    io.captureBuf = 0;
    io.captureSemaphore.unlock();
}

unsigned int BrfLibUsbDevice::internalSetSampleRate(bool tx, uint32_t value,
    String* error)
{
    String e;
    unsigned int status = 0;
    if (value <= m_radioCaps.maxSampleRate)
	status = lusbSetAltInterface(BRF_ALTSET_RF_LINK,&e);
    else {
	status = RadioInterface::InsufficientSpeed;
	e << "insufficient speed required=" << value << " max=" << m_radioCaps.maxSampleRate;
    }
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
		"Requested %s sample rate %u is smaller than allowed minimum value [%p]",
		brfDir(tx),value,m_owner);
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
	if (getDirState(tx).sampleRate != value) {
	    getDirState(tx).sampleRate = value;
	    Debug(m_owner,DebugInfo,"%s samplerate set to %u [%p]",
		brfDir(tx),value,m_owner);
	    // Signal sample rate change to internal TX/RX
	    Lock lck(m_threadMutex);
	    if (tx)
		m_internalIoTxRate = value;
	    else
		m_internalIoRxRate = value;
	    m_internalIoRateChanged = true;
	}
	if (!tx) {
	    unsigned int samplesMs = (value + 999) / 1000;
	    // Calculate RX samples allowed to be in the past
	    m_rxTsPastSamples = m_rxTsPastIntervalMs * samplesMs;
	    // Calculate RX timestamp silence
	    m_silenceTs = m_silenceTimeMs * samplesMs;
	}
	BrfDevIO& io = getIO(tx);
	bool first = !io.firstBufsThres.sampleRate;
	if (first) {
	    io.firstBufsThres.sampleRate = value;
	    io.firstBufsThres.bufferedSamples = totalSamples(tx);
	    io.firstBufsThres.txMinBufs = m_minBufsSend;
	}
	// Check for I/O buffers info
	const BrfBufsThreshold* t = BrfBufsThreshold::findThres(m_bufThres,value);
	if (!t && !first)
	    // No threshold set: rollback to first values
	    t = &io.firstBufsThres;
	if (t)
	    initBuffers(&tx,t->bufferedSamples,t->txMinBufs);
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
    if (status) {
	Debug(m_owner,DebugWarn,"Failed to load FPGA: %s [%p]",e.c_str(),m_owner);
	return status;
    }
    BrfDevTmpAltSet tmpAltSet(this,status,&e,"FPGA version get");
    if (status)
	return 0;
    uint32_t ver = 0;
    if (0 == gpioRead(0x0c,ver,4,&e))
	ver2str(m_devFpgaVerStr,ver);
    else
	Debug(m_owner,DebugNote,"Failed to retrieve FPGA version: %s [%p]",
	    e.c_str(),m_owner);
    return 0;
}

unsigned int BrfLibUsbDevice::internalSetFpgaCorr(bool tx, int corr, int16_t value,
    String* error, int lvl)
{
    XDebug(m_owner,DebugAll,"internalSetFpgaCorr(%u,%d,%d) [%p]",tx,corr,value,m_owner);
    String e;
    unsigned int status = 0;
    int orig = value;
    uint8_t addr = 0;
    int* old = 0;
    BrfDevDirState& io = getDirState(tx);
    bool setBoard = true;
    if (corr == CorrFpgaGain) {
	old = &io.fpgaCorrGain;
	addr = fpgaCorrAddr(tx,false);
	if (tx && m_txGainCorrSoftware) {
	    // Because FPGA Gain cal is broken in older images, we fake it in software.
	    // We should probably just keep faking it, even in newer FPGA images.
	    const float bal = 1 + 0.1*(((float)orig) / BRF_FPGA_CORR_MAX);
	    status = internalSetTxIQBalance(false,bal);
	    setBoard = false;
	}
	else {
	    orig = clampInt(orig,-BRF_FPGA_CORR_MAX,BRF_FPGA_CORR_MAX,"FPGA GAIN",lvl);
	    value = orig + BRF_FPGA_CORR_MAX;
	}
    }
    else if (corr == CorrFpgaPhase) {
	old = &io.fpgaCorrPhase;
	orig = clampInt(orig,-BRF_FPGA_CORR_MAX,BRF_FPGA_CORR_MAX,"FPGA PHASE",lvl);
	value = orig;
	addr = fpgaCorrAddr(tx,true);
    }
    else
	status = setUnkValue(e,0,"FPGA corr value " + String(corr));
    if (!status) {
	if (setBoard)
	    status = gpioWrite(addr,value,2,&e);
	if (!status) {
	    if (old) {
		if (io.showFpgaCorrChange == 0 && *old != orig)
		    Debug(m_owner,DebugInfo,"%s FPGA corr %s %s to %d (reg %d) [%p]",
			brfDir(tx),lookup(corr,s_corr),(setBoard ? "set" : "faked"),
			orig,value,m_owner);
		*old = orig;
	    }
	    return 0;
	}
    }
    e.printf(1024,"Failed to %s %s FPGA corr %s to %d (from %d) - %s [%p]",
	(setBoard ? "set" : "fake"),brfDir(tx),lookup(corr,s_corr),
	value,orig,e.c_str(),m_owner);
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::internalGetFpgaCorr(bool tx, int corr, int16_t* value,
    String* error)
{
    String e;
    unsigned int status = 0;
    uint8_t addr = 0;
    int* update = 0;
    BrfDevDirState& io = getDirState(tx);
    if (corr == CorrFpgaGain) {
	if (tx && m_txGainCorrSoftware) {
	    if (value)
		*value = io.fpgaCorrGain;
	    return 0;
	}
	update = &io.fpgaCorrGain;
	addr = fpgaCorrAddr(tx,false);
    }
    else if (corr == CorrFpgaPhase) {
	update = &io.fpgaCorrPhase;
	addr = fpgaCorrAddr(tx,true);
    }
    else
	status = setUnkValue(e,0,"FPGA corr value " + String(corr));
    if (!status) {
	uint32_t u = 0;
	status = gpioRead(addr,u,2,&e);
	if (status == 0) {
	    int v = (int)u;
	    if (corr == CorrFpgaGain)
		v -= BRF_FPGA_CORR_MAX;
	    if (value)
		*value = v;
	    XDebug(m_owner,DebugAll,"Got %s FPGA corr %s %d [%p]",
		brfDir(tx),lookup(corr,s_corr),v,m_owner);
	    if (update)
		*update = v;
	    return 0;
	}
    }
    e.printf(1024,"Failed to retrieve %s FPGA corr %s - %s [%p]",
	brfDir(tx),lookup(corr,s_corr),e.c_str(),m_owner);
    return showError(status,e,0,error);
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
	    m_state.m_tx.vga1Changed = true;
	int& old = preMixer ? m_state.m_tx.vga1 : m_state.m_tx.vga2;
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
	    m_state.m_tx.vga1 = v;
	}
	else {
	    v = (data >> 3) & 0x1f;
	    if (v > BRF_TXVGA2_GAIN_MAX)
		v = BRF_TXVGA2_GAIN_MAX;
	    m_state.m_tx.vga2 = v;
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
	    old = (data & 0x02) != 0;
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
	    changed = (m_state.m_rx.vga1 != vga);
	    m_state.m_rx.vga1 = vga;
	}
	else {
	    vga = clampInt(vga / 3 * 3,BRF_RXVGA2_GAIN_MIN,BRF_RXVGA2_GAIN_MAX,"RX VGA2");
	    data = (uint8_t)((data & ~0x1f) | (vga / 3));
	    BRF_FUNC_CALL_BREAK(lmsWrite(addr,data,&e));
	    changed = (m_state.m_rx.vga2 != vga);
	    m_state.m_rx.vga2 = vga;
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
	    m_state.m_rx.vga1 = v = s_rxvga1_get[idx < 121 ? idx : 120];
	}
	else {
	    m_state.m_rx.vga2 = v = (data & 0x1f) * 3;
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
	vga1 = (m_state.m_tx.vga1Changed && m_state.m_tx.vga1 >= BRF_TXVGA1_GAIN_MIN) ?
	    m_state.m_tx.vga1 : BRF_TXVGA1_GAIN_DEF;
	val = clampInt(val + BRF_TXVGA2_GAIN_MAX,BRF_TXVGA2_GAIN_MIN,BRF_TXVGA2_GAIN_MAX);
    }
    else {
	vga1 = (m_state.m_rx.vga1 > BRF_RXVGA1_GAIN_MAX) ?
	    BRF_RXVGA1_GAIN_MAX : m_state.m_rx.vga1;
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
	    dbg = (m_state.m_tx.showPowerBalanceChange == 0);
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

unsigned int BrfLibUsbDevice::internalSetFreqOffs(float val, float* newVal, String* error)
{
    val = clampFloat(val,BRF_FREQ_OFFS_MIN,BRF_FREQ_OFFS_MAX,"FrequencyOffset");
    String e;
    // val has an 8 bit integer range with float precision, which need to be converted to 16 bit integer
    uint32_t voltageDAC = val * 256;
    unsigned int status = gpioWrite(0x22,voltageDAC,2,&e);
    if (status)
	return showError(status,e,"FrequencyOffset set failed",error);
    if (m_freqOffset != val) {
	Debug(m_owner,(m_freqOffset != val) ? DebugInfo : DebugAll,
	    "FrequencyOffset changed %g -> %g [%p]",m_freqOffset,val,m_owner);
	m_freqOffset = val;
    }
    else
	Debug(m_owner,DebugAll,"FrequencyOffset set to %g [%p]",val,m_owner);
    if (newVal)
	*newVal = val;
    return 0;
}

unsigned int BrfLibUsbDevice::internalSetFrequency(bool tx, uint64_t val, String* error)
{
    XDebug(m_owner,DebugAll,"BrfLibUsbDevice::setFrequency("FMT64U",%s) [%p]",
	val,brfDir(tx),m_owner);
    String e;
    unsigned int status = 0;
    BrfDevTmpAltSet tmpAltSet(this,status,&e,"frequency set");
    if (val < BRF_FREQUENCY_MIN || val > BRF_FREQUENCY_MAX) {
	status = RadioInterface::OutOfRange;
	e = "Value out of range";
    }
    uint32_t hz = (uint32_t)val;
    while (!status) {
	uint8_t addr = lmsFreqAddr(tx);
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
	if (m_state.m_loopback == LoopNone)
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
	e.printf(1024,"Failed to set %s frequency to "FMT64U"Hz - %s",brfDir(tx),val,e.c_str());
	return showError(status,e,0,error);
    }
    if (getDirState(tx).frequency != hz) {
	getDirState(tx).frequency = hz;
	Debug(m_owner,DebugInfo,"%s frequency set to %gMHz offset=%g [%p]",
	    brfDir(tx),(double)hz / 1000000,m_freqOffset,m_owner);
    }
    else
	Debug(m_owner,DebugAll,"%s frequency set to %gMHz offset=%g [%p]",
	    brfDir(tx),(double)hz / 1000000,m_freqOffset,m_owner);
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
	getDirState(tx).frequency = freq;
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

unsigned int BrfLibUsbDevice::lmsWrite(const String& str, bool updStat, String* error)
{
    if (!str)
	return 0;
    String e;
    unsigned int status = 0;
    while (true) {
	DataBlock db;
	if (!db.unHexify(str)) {
	    status = setErrorFail(&e,"Invalid hex string");
	    break;
	}
	if ((db.length() % 2) != 0) {
	    status = setErrorFail(&e,"Invalid string length");
	    break;
	}
	Debug(m_owner,DebugAll,"Writing '%s' to LMS [%p]",str.c_str(),m_owner);
	uint8_t* b = db.data(0);
	for (unsigned int i = 0; !status && i < db.length(); i += 2) {
	    b[i] &= ~0x80;
	    status = lmsWrite(b[i],b[i + 1],&e);
	}
	if (!status && updStat)
	    status = updateStatus(&e);
	if (!status)
	    return 0;
	break;
    }
    e.printf(1024,"LMS write '%s' failed - %s",str.c_str(),e.c_str());
    return showError(status,e,0,error);
}

// Read the lms configuration
unsigned int BrfLibUsbDevice::lmsRead(String& dest, const String* read,
    bool readIsInterleaved, String* error)
{
    String e;
    unsigned int status = 0;
    while (true) {
	DataBlock db;
	if (read) {
	    DataBlock tmp;
	    if (!tmp.unHexify(*read)) {
		status = setErrorFail(&e,"Invalid hex string");
		break;
	    }
	    if (readIsInterleaved) {
		if ((tmp.length() % 2) != 0) {
		    status = setErrorFail(&e,"Invalid string length");
		    break;
		}
		db = tmp;
	    }
	    else {
		db.resize(tmp.length() * 2);
		for (unsigned int i = 0; i < tmp.length(); i++)
		    *db.data(i * 2) = tmp[i];
	    }
	}
	else {
	    db.resize(127 * 2);
	    for (uint8_t i = 0; i < 127; i++)
		*db.data(i * 2) = i;
	}
	Debug(m_owner,DebugAll,"Reading LMS [%p]",m_owner);
	uint8_t* b = db.data(0);
	for (unsigned int i = 0; !status && i < db.length(); i += 2) {
	    b[i] &= ~0x80;
	    status = lmsRead(b[i],b[i + 1],&e);
	}
	if (status)
	    break;
	dest.hexify(db.data(),db.length());
	return 0;
    }
    e.printf(1024,"LMS read '%s' failed - %s",TelEngine::c_safe(read),e.c_str());
    return showError(status,e,0,error);
}

// Check LMS registers
unsigned int BrfLibUsbDevice::lmsCheck(const String& what, String* error)
{
    if (!what)
	return 0;
    String e;
    unsigned int status = 0;
    while (true) {
	DataBlock db;
	bool haveMask = (what[0] == '+');
	unsigned int delta = haveMask ? 1 : 0;
	if (!db.unHexify(what.c_str() + delta,what.length() - delta)) {
	    status = setErrorFail(&e,"Invalid hex string");
	    break;
	}
	unsigned int div = haveMask ? 3 : 2;
	if ((db.length() % div) != 0) {
	    status = setErrorFail(&e,"Invalid string length");
	    break;
	}
	unsigned int n = db.length() / div;
	uint8_t b[4];
	uint8_t* d = db.data(0);
	String diff;
	String s;
	for (unsigned int i = 0; i < n; i++) {
	    b[0] = *d++ & ~0x80;
	    b[1] = *d++;
	    b[2] = 0;
	    b[3] = (div > 2) ? *d++ : 0xff;
	    BRF_FUNC_CALL_BREAK(lmsRead(b[0],b[2],&e));
	    if ((b[1] & b[3]) != (b[2] & b[3]))
		diff.append(s.hexify(b,div + 1)," ");
	}
	if (status)
	    break;
	if (error)
	    *error = diff;
	else if (diff)
	    Debug(m_owner,DebugNote,"Check LMS '%s' diff: %s [%p]",
		what.c_str(),diff.c_str(),m_owner);
	return 0;
    }
    e.printf(1024,"LMS check '%s' - %s",what.c_str(),e.c_str());
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
	    "%s LNA RXFE [%p]",enabledStr(on),m_owner);
	return 0;
    }
    e.printf("Failed to %s LNA RXFE - %s",enableStr(on),e.c_str());
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
	uint8_t i = bw2index(band);
	uint8_t bw = 15 - i;
	data &= ~0x3c; // Clear out previous bandwidth setting
	data |= (bw << 2); // Apply new bandwidth setting
	BRF_FUNC_CALL_BREAK(lmsWrite(reg,data,&e));
	bool changed = (getDirState(tx).lpfBw != s_bandSet[i]);
	getDirState(tx).lpfBw = s_bandSet[i];
	Debug(m_owner,changed ? DebugInfo : DebugAll,
	    "%s LPF bandwidth set to %u (from %u, reg=0x%x) [%p]",
	    brfDir(tx),getDirState(tx).lpfBw,band,data,m_owner);
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
		if (getDirState(tx).lpf != lpf) {
		    getDirState(tx).lpf = lpf;
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
	    getDirState(tx).lpf = l;
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
    BrfDevDirState& dirState = getDirState(tx);
    unsigned int status = 0;
    String e;
    resetTimestamps(tx);
    if (!m_devHandle) {
	if (!on) {
	    dirState.rfEnabled = false;
	    return 0;
	}
	status = RadioInterface::NotInitialized;
	e = "Not open";
    }
    if (!status) {
	// RF front end
	uint8_t addr = tx ? 0x40 : 0x70;
	uint8_t val = tx ? 0x02 : 0x01;
	status = lmsChangeMask(addr,val,on,&e);
	if (!status && !frontEndOnly)
	    status = enableRfFpga(tx,on,&e);
    }
    bool ok = on && (status == 0);
    if (dirState.rfEnabled == ok) {
	dirState.rfEnabled = ok;
	return status;
    }
    dirState.rfEnabled = ok;
    const char* fEnd = frontEndOnly ? " front end" : "";
    if (status == 0) {
	Debug(m_owner,DebugAll,"%s RF %s%s [%p]",
	    enabledStr(on),brfDir(tx),fEnd,m_owner);
	return 0;
    }
    e.printf(1024,"Failed to %s RF %s%s - %s",enableStr(on),brfDir(tx),fEnd,e.c_str());
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::enableRfFpga(bool tx, bool on, String* error)
{
    uint8_t request = tx ? BRF_USB_CMD_RF_TX : BRF_USB_CMD_RF_RX;
    uint32_t buf = (uint32_t)-1;
    uint16_t value = on ? 1 : 0;
    String e;
    unsigned int status = lusbCtrlTransfer(LUSB_CTRLTRANS_IFACE_VENDOR_IN,
	request,value,0,(uint8_t*)&buf,sizeof(buf),&e);
    if (status == 0 && le32toh(buf))
	status = setErrorFail(&e,"Device failure");
    if (!status)
	return 0;
    e.printf(1024,"FPGA RF %s failed - %s",enableStr(on),e.c_str());
    return showError(status,e,0,error);
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
	break;
    }
    if (status == 0) {
	XDebug(m_owner,DebugAll,"Restored device after FPGA load [%p]",m_owner);
	return 0;
    }
    return showError(status,e,"Failed to restore device after FPGA load",error);
}

// Change some LMS registers default value on open
unsigned int BrfLibUsbDevice::openChangeLms(const NamedList& params, String* error)
{
    // See Lime FAQ document Section 5.27
    static const String s_def = "4740592964367937";

    String e;
    unsigned int status = 0;
    BrfDevTmpAltSet tmpAltSet(this,status,&e,"Open change LMS");
    if (!status) {
	const String* s = params.getParam(YSTRING("open_write_lms"));
	if (s && *s != s_def)
	    Debug(m_owner,DebugNote,"Open: writing LMS '%s' [%p]",s->c_str(),m_owner);
	else
	    s = &s_def;
	status = lmsWrite(*s,false,&e);
    }
    if (status == 0) {
	XDebug(m_owner,DebugAll,"Changed default LMS values [%p]",m_owner);
	return 0;
    }
    return showError(status,e,"Failed to change LMS defaults",error);
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
    if (!uartDev.trackDir(tx))
	return 0;
    String s;
    if (!uartDev.haveTrackAddr()) {
	if (m_owner->debugAt(DebugAll))
	    Debug(m_owner,DebugAll,"%s %s addr=0x%x len=%u '%s' [%p]",
		uartDev.c_str(),brfDir(tx),addr,len,
		s.hexify(data,len,' ').c_str(),m_owner);
    }
    else {
	int level = uartDev.trackLevel();
	bool levelOk = !level || m_owner->debugAt(level);
	if (levelOk && (uartDev.isTrackRange(addr,len) >= 0)) {
	    unsigned int a = addr;
	    for (unsigned int i = 0; i < len && a < 256; i++, a++)
		if (uartDev.isTrackAddr(a)) {
		    String tmp;
		    if (!s)
			s << uartDev.c_str() << " " << brfDir(tx);
		    s.append(tmp.printf("(0x%x=0x%x)",(uint8_t)a,data[i])," ");
		}
	    if (s) {
		if (level)
		    Debug(m_owner,level,"%s [%p]",s.c_str(),m_owner);
		else {
		    char b[50];
		    Debugger::formatTime(b);
		    Output("%s<%s> %s [%p]",b,m_owner->debugName(),s.c_str(),m_owner);
		}
	    }
	}
    }
    return 0;
}

unsigned int BrfLibUsbDevice::internalSetDcOffset(bool tx, bool i, int16_t value,
    String* error)
{
    int& old = i ? getDirState(tx).dcOffsetI : getDirState(tx).dcOffsetQ;
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
	    if (getDirState(tx).showDcOffsChange == 0)
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
	int& old = i ? getDirState(tx).dcOffsetI : getDirState(tx).dcOffsetQ;
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
	    resetTimestamps(true);
	    resetTimestamps(false);
	    setIoDontWarnTs(true);
	    setIoDontWarnTs(false);
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
    // Frequency (only if already set)
    if (m_state.m_tx.frequency)
	BRF_FUNC_CALL(internalGetFrequency(true));
    if (m_state.m_rx.frequency)
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

int64_t BrfLibUsbDevice::clampInt(int64_t val, int64_t minVal, int64_t maxVal, const char* what,
    int level)
{
    if (val >= minVal && val <= maxVal)
	return val;
    int64_t c = val < minVal ? minVal : maxVal;
    if (what)
	Debug(m_owner,level,"Clamping %s " FMT64 " -> " FMT64 " [%p]",what,val,c,m_owner);
    return c;
}

float BrfLibUsbDevice::clampFloat(float val, float minVal, float maxVal, const char* what,
    int level)
{
    if (val >= minVal && val <= maxVal)
	return val;
    float c = val < minVal ? minVal : maxVal;
    if (what)
	Debug(m_owner,level,"Clamping %s %g -> %g [%p]",what,val,c,m_owner);
    return c;
}

unsigned int BrfLibUsbDevice::openDevice(bool claim, String* error)
{
    closeDevice();
    m_dev = 0;
    unsigned int status = updateDeviceList(error);
    if (status)
	return status;
    bool haveMatch = !m_serial.null();
    bool foundMatched = false;
    unsigned int failedDesc = 0;
    ObjList found;
    for (unsigned int i = 0; i < m_listCount; i++) {
	libusb_device_descriptor desc;
	if (::libusb_get_device_descriptor(m_list[i],&desc)) {
	    failedDesc++;
	    continue;
	}
	// OpenMoko 0x1d50 Product=0x6066
	// Nuand    0x2cf0 Product=0x5246
	if (!((desc.idVendor == 0x1d50 && desc.idProduct == 0x6066) ||
	    (desc.idVendor == 0x2cf0 && desc.idProduct == 0x5246)))
	    continue;
	m_dev = m_list[i];
	m_devBus = ::libusb_get_bus_number(m_dev);
	m_devAddr = ::libusb_get_device_address(m_dev);
	m_devSpeed = ::libusb_get_device_speed(m_dev);
	DDebug(m_owner,DebugAll,"Opening device bus=%u addr=%u [%p]",bus(),addr(),m_owner);
	String tmpError;
	unsigned int tmpStatus = lusbCheckSuccess(::libusb_open(m_dev,&m_devHandle),
	    &tmpError,"Failed to open the libusb device ");
	while (!tmpStatus) {
	    getDevStrDesc(m_devSerial,desc.iSerialNumber,"serial number");
	    if (haveMatch) {
		if (m_serial != m_devSerial)
		    break;
		foundMatched = true;
	    }
	    getDevStrDesc(m_devFwVerStr,4,"firmware version");
	    if (claim)
		tmpStatus = lusbCheckSuccess(::libusb_claim_interface(m_devHandle,0),
		    &tmpError,"Failed to claim the interface ");
	    if (!tmpStatus) {
		m_address.clear();
		m_address << "USB/" << bus() << "/" << addr();
		Debug(m_owner,DebugAll,"Opened device bus=%u addr=%u [%p]",bus(),addr(),m_owner);
		return 0;
	    }
	    break;
	}
	String* tmp = new String(m_devBus);
	*tmp << "/" << m_devAddr << "/" << m_devSerial;
	found.append(tmp);
	closeUsbDev();
	m_dev = 0;
	if (tmpStatus) {
	    status = tmpStatus;
	    if (error)
		*error = tmpError;
	}
	if (foundMatched)
	    break;
    }
    String e;
    if (haveMatch) {
	e << "serial='" << m_serial << "' [";
	if (!foundMatched)
	    e << "not ";
	e << "found] ";
    }
    if (found.count()) {
	e << "checked_devices=" << found.count();
	String failed;
	failed.append(found,",");
	e << " (" << failed << ")";
    }
    else if (!haveMatch)
	e << "no device found";
    if (failedDesc)
	e << " (failed_desc_retrieval=" << failedDesc << " device descriptor(s))";
    if (status)
	return setError(status,error,e);
    if (found.skipNull() && (!haveMatch || foundMatched))
	return setError(RadioInterface::NotInitialized,error,e);
    return setError(RadioInterface::HardwareNotAvailable,error,e);
}

void BrfLibUsbDevice::closeDevice()
{
    if (!m_devHandle)
	return;
    if (m_notifyOff) {
	Engine::enqueue(buildNotify("stop"));
	m_notifyOff = false;
    }
    //Debugger d(DebugNote,"closeDevice "," %s [%p]",m_owner->debugName(),m_owner);
    m_closingDevice = true;
    stopThreads();
    internalPowerOn(false,false,false);
    m_closingDevice = false;
    closeUsbDev();
    m_txIO.dataDumpFile.terminate(owner());
    m_txIO.upDumpFile.terminate(owner());
    m_rxIO.dataDumpFile.terminate(owner());
    m_rxIO.upDumpFile.terminate(owner());
    m_initialized = false;
    Debug(m_owner,DebugAll,"Device closed [%p]",m_owner);
}

void BrfLibUsbDevice::closeUsbDev()
{
    if (m_devHandle) {
	::libusb_close(m_devHandle);
	m_devHandle = 0;
    }
    m_devBus = -1;
    m_devAddr = -1;
    m_devSpeed = LIBUSB_SPEED_HIGH;
    m_devSerial.clear();
    m_devFwVerStr.clear();
    m_devFpgaVerStr.clear();
    m_devFpgaFile.clear();
    m_devFpgaMD5.clear();
    m_lmsVersion.clear();
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
    Debug(m_owner,DebugNote,"Failed to retrieve device %s %s [%p]",
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
    // NOTE calibration cache may be obsolete, readCalCache is called at initialization only
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
	initBuffers(0,params.getIntValue("buffered_samples",2048),
	    params.getIntValue("tx_min_buffers"));
	if (speed() == LIBUSB_SPEED_SUPER) {
	    m_radioCaps.rxLatency = clampIntParam(params,"rx_latency_super",4000,0,150000);
	    m_radioCaps.txLatency = clampIntParam(params,"tx_latency_super",10000,0,150000);
	    m_radioCaps.maxSampleRate = clampIntParam(params,"max_samplerate_super",
		MAX_SAMPLERATE_SUPER,2 * BRF_SAMPLERATE_MIN,BRF_SAMPLERATE_MAX);
	    m_ctrlTransferPage = BRF_FLASH_PAGE_SIZE;
	}
	else {
	    m_radioCaps.rxLatency = clampIntParam(params,"rx_latency_high",7000,0,150000);
	    m_radioCaps.txLatency = clampIntParam(params,"tx_latency_high",20000,0,150000);
	    m_radioCaps.maxSampleRate = clampIntParam(params,"max_samplerate_high",
		MAX_SAMPLERATE_HIGH,2 * BRF_SAMPLERATE_MIN,BRF_SAMPLERATE_MAX);
	    m_ctrlTransferPage = 64;
	}
	return 0;
    }
    m_minBufsSend = 1;
    m_radioCaps.rxLatency = 0;
    m_radioCaps.txLatency = 0;
    m_radioCaps.maxSampleRate = BRF_SAMPLERATE_MAX;
    m_ctrlTransferPage = 0;
    String e;
    e << "Unsupported USB speed " << m_devSpeed;
    return setError(RadioInterface::InsufficientSpeed,error,e);
}

// Set I/O buffers
void BrfLibUsbDevice::initBuffers(bool* txSet, unsigned int totalSamples, unsigned int txMinSend)
{
    totalSamples = clampInt(totalSamples,1024,16384,"buffered_samples",DebugConf);
    unsigned int bufSamples = (speed() == LIBUSB_SPEED_HIGH) ? 252 : 508;
    unsigned int nBuffs = totalSamples / bufSamples;
    if (!nBuffs)
	nBuffs = 1;
    for (int tx = 1; tx > -1; tx--) {
	if (txSet && *txSet != tx)
	    continue;
	BrfDevIO& io = getIO(tx);
	if (io.buffers == nBuffs && io.bufSamples == bufSamples)
	    continue;
	// Lock I/O to make sure we don't use the buffers
	BrfSerialize lck(this,tx,false);
	String error;
	for (unsigned int i = 0; !lck.devLocked() && i < 3; i++)
	    lck.wait(&error,1000000);
	if (!lck.devLocked()) {
	    Debug(m_owner,DebugGoOn,"Failed to initialize %s buffers: serialize [%p]",
		brfDir(tx),m_owner);
	    continue;
	}
	bool first = !io.buffers;
	io.resetSamplesBuffer(bufSamples,16,nBuffs);
	String extra;
	if (tx) {
	    if (txMinSend)
		m_minBufsSend = clampInt(txMinSend,1,nBuffs,"tx_min_buffers",DebugConf);
	    else
		m_minBufsSend = nBuffs;
	    extra << " tx_min_buffers=" << m_minBufsSend;
	}
	Debug(m_owner,first ? DebugAll : DebugInfo,
	    "Initialized I/O %s buffers=%u samples/buffer=%u total_bytes=%u%s [%p]",
	    brfDir(tx),io.buffers,io.bufSamples,io.buffer.length(),extra.safe(),m_owner);
	lck.drop();
	if (tx) {
	    // Regenerate TX pattern: it may have the same length as used buffers
	    Lock d(m_dbgMutex);
	    String pattern = m_state.m_txPattern;
	    m_state.m_txPattern = "";
	    float gain = m_state.m_txPatternGain;
	    d.drop();
	    setTxPattern(pattern,gain);
	}
    }
}

// Check timestamps before send / after read
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
    unsigned int dontWarn = checkDbgInt(getIO(tx).dontWarnTs,nBufs);
    for (; i < nBufs; i++) {
	uint64_t crt = io.bufTs(i);
	if (!dontWarn && (io.lastTs + io.bufSamples) != crt) {
	    if (!invalid)
		invalid << ": invalid timestamps (buf=ts/delta)";
	    invalid << " " << (i + 1) << "=" << crt << "/" << (int64_t)(crt - io.lastTs);
	}
	if (dontWarn)
	    dontWarn--;
	io.lastTs = crt;
    }
    if (invalid)
	Debug(m_owner,DebugMild,"%s buf_samples=%u: %u buffers%s [%p]",
	    brfDir(tx),io.bufSamples,nBufs,invalid.safe(),m_owner);
}

void BrfLibUsbDevice::setIoDontWarnTs(bool tx)
{
    BrfDevIO& io = getIO(tx);
    Lock lck(m_dbgMutex);
    io.dontWarnTs = io.buffers * 40;
    XDebug(m_owner,DebugAll,"%s don't warn ts set to %d [%p]",
	brfDir(tx),io.dontWarnTs,m_owner);
}

// Check samples limit before send / after read
void BrfLibUsbDevice::ioBufCheckLimit(bool tx, unsigned int nBufs)
{
    BrfDevIO& io = getIO(tx);
    if (!nBufs)
	nBufs = io.buffers;
    String invalid;
    String tmp;
    unsigned int check = 10;
    for (unsigned int i = 0; i < nBufs; i++) {
	int16_t* s = io.samples(i);
	int16_t* e = io.samplesEOF(i);
	for (unsigned int j = 0; check && s != e; s++, j++)
	    if (*s < -2048 || *s > 2047) {
		invalid << tmp.printf(" %c=%d (%u at %u)",
		    brfIQ((j % 2) == 0),*s,i + 1,j / 2);
		check--;
	    }
    }
    if (invalid)
	Debug(m_owner,DebugGoOn,"%s: sample value out of range buffers=%u:%s [%p]",
	    brfDir(tx),nBufs,invalid.c_str(),m_owner);
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

void BrfLibUsbDevice::dumpState(String& s, const NamedList& p, bool lockPub, bool force)
{
    BrfSerialize txSerialize(this,true,false);
    if (lockPub) {
	txSerialize.wait(0,5000000);
	if (txSerialize.status) {
	    if (RadioInterface::Failure == txSerialize.status)
		s << "Failed to retrieve state: lock failed";
	    return;
	}
    }

    String lmsModules, lpStatus, lms, lmsStr;
    if (p.getBoolValue(YSTRING("dump_dev"),force)) {
	BrfDevDirState& tx = getDirState(true);
	BrfDevDirState& rx = getDirState(false);
	s << "            TX / RX";
	s << "\r\nFREQ(Hz):   " << tx.frequency << " / " << rx.frequency;
	s << "\r\nVGA1:       " << tx.vga1 << " / " << rx.vga1;
	s << "\r\nVGA2:       " << tx.vga2 << " / " << rx.vga2;
	s << "\r\nSampleRate: " << tx.sampleRate << " / " << rx.sampleRate;
	s << "\r\nFilter:     " << tx.lpfBw << " / " << rx.lpfBw;
	s << "\r\ntxpattern:  " << m_state.m_txPattern;
	s << "\r\nloopback:   " << lookup(m_state.m_loopback,s_loopback);
	if (force) {
	    s << "\r\nSerial:     " << serial();
	    s << "\r\nSpeed:      " << speedStr();
	    s << "\r\nFirmware:   " << fwVerStr();
	    s << "\r\nFPGA:       " << fpgaVerStr();
	}
    }
    if (p.getBoolValue(YSTRING("dump_lms_modules"),force)) {
	dumpLmsModulesStatus(&lmsModules);
	s.append("LMS modules:","\r\n\r\n") << lmsModules;
    }
    if (p.getBoolValue(YSTRING("dump_loopback_status"),force)) {
	dumpLoopbackStatus(&lpStatus);
	s.append("Loopback switches:","\r\n\r\n") << lpStatus;
    }
    if (p.getBoolValue(YSTRING("dump_lms"),force)) {
	internalDumpPeripheral(UartDevLMS,0,128,&lms,16);
	s.append("LMS:","\r\n\r\n") << lms;
    }
    String readLms = p[YSTRING("dump_lms_str")];
    if (readLms) {
	if (readLms == "-")
	    lmsRead(lmsStr,0,false);
	else {
	    bool interleaved = (readLms[0] == '+');
	    if (interleaved)
		readLms = readLms.substr(1);
	    lmsRead(lmsStr,&readLms,interleaved);
	}
	s.append("LMS string:\r\n","\r\n\r\n") << lmsStr;
    }
}

// LMS autocalibration
unsigned int BrfLibUsbDevice::calibrateAuto(String* error)
{
    BrfSerialize txSerialize(this,true,false);
    BrfSerialize rxSerialize(this,false,false);
    unsigned int status = 0;
    // Pause I/O threads if calibration is running
    if (m_calibrateStatus == Calibrating) {
	BRF_FUNC_CALL_RET(calThreadsPause(true,error));
    }
    if (!rxSerialize.devLocked()) {
	BRF_FUNC_CALL_RET(rxSerialize.wait(error));
    }
    if (!txSerialize.devLocked()) {
	BRF_FUNC_CALL_RET(txSerialize.wait(error));
    }
#ifdef DEBUG_DEVICE_AUTOCAL
    Debugger debug(DebugAll,"AUTOCALIBRATION"," '%s' [%p]",m_owner->debugName(),m_owner);
#endif
    Debug(m_owner,DebugInfo,"LMS autocalibration starting ... [%p]",m_owner);

    BrfDuration duration;
    String e;
    BrfDevState oldState(m_state,0,DevStatDc,DevStatDc);
    // Set TX/RX DC I/Q to 0
    BrfDevState set0(DevStatAbortOnFail,DevStatDc,DevStatDc);
    status = setState(set0,&e);
    int8_t calVal[BRF_CALIBRATE_LAST][BRF_CALIBRATE_MAX_SUBMODULES];
    ::memset(calVal,-1,sizeof(calVal));
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
    setState(oldState);
    duration.stop();
    if (status) {
	e = "LMS autocalibration failed - " + e;
	return showError(status,e,0,error);
    }
    String s;
#ifdef DEBUG
    for (int m = BRF_CALIBRATE_FIRST; m <= BRF_CALIBRATE_LAST; m++) {
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
    Debug(m_owner,DebugInfo,"LMS autocalibration finished in %s [%p]%s",
	duration.secStr(),m_owner,encloseDashes(s));
    if (m_calibrateStatus != Calibrating)
	return 0;
    txSerialize.drop();
    rxSerialize.drop();
    return calThreadsPause(false,error);
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

unsigned int BrfLibUsbDevice::calibrateBbCorrection(BrfBbCalData& data,
    int corr, int range, int step, int pass, String* error)
{
    static const int corrPeer[CorrCount] = {CorrLmsQ, CorrLmsI,
	CorrFpgaGain, CorrFpgaPhase};
    static const unsigned int syncFlags[CorrCount] = {DevStatDcI, DevStatDcQ,
	DevStatFpgaPhase, DevStatFpgaGain};

    int* corrVal[CorrCount] = {&data.m_dcI, &data.m_dcQ, &data.m_phase, &data.m_gain};
    BrfDevDirState& t = m_syncTxState.m_tx;
    int* syncSet[CorrCount] = {&t.dcOffsetI, &t.dcOffsetQ, &t.fpgaCorrPhase, &t.fpgaCorrGain};

    bool dc = (CorrLmsI == corr || CorrLmsQ == corr);
    if (!dc && CorrFpgaPhase != corr && CorrFpgaGain != corr)
	return setErrorFail(error,"calibrateBbCorrection: unhandled corr");
    BrfDuration duration;
    // Set peer (fixed) correction
    *syncSet[corrPeer[corr]] = *corrVal[corrPeer[corr]];
    unsigned int status = setStateSyncTx(syncFlags[corrPeer[corr]],error);
    // Set calibration range
    int minV = dc ? BRF_TX_DC_OFFSET_MIN : -BRF_FPGA_CORR_MAX;
    int maxV = dc ? BRF_TX_DC_OFFSET_MAX : BRF_FPGA_CORR_MAX;
    int calVal = *corrVal[corr] - range;
    int calValMax = *corrVal[corr] + range;
    if (calVal < minV)
	calVal = minV;
    if (calValMax > maxV)
	calValMax = maxV;

    Debug(m_owner,DebugNote,"Calibrating %s pass=%d [%p]",
	lookup(corr,s_corr),pass,this);
    unsigned int trace = data.uintParam(dc,"trace");
    if (trace)
	Output("Pass #%u calibrating %s (crt: %d) %s=%d "
	    "samples=%u range=%d step=%d interval=[%d..%d]",
	    pass,lookup(corr,s_corr),*corrVal[corr],
	    lookup(corrPeer[corr],s_corr),*corrVal[corrPeer[corr]],
	    data.samples(),range,step,calVal,calValMax);
    bool traceRepeat = trace && data.boolParam(dc,"trace_repeat",true);
    bool traceFailed = trace && data.boolParam(dc,"trace_failed",true);
    bool accum = false;
    if (data.m_dump.valid()) {
	data.dumpCorrStart(pass,corr,*corrVal[corr],corrPeer[corr],
	    *corrVal[corrPeer[corr]],range,step,calVal,calValMax);
	accum = (0 != data.m_calAccum.data.length());
	data.m_dump.resetDumpOkFail();
    }

    float totalStop = data.m_params.getDoubleValue("stop_total_threshold",BRF_MAX_FLOAT);
    float limit = getSampleLimit(data.m_params,1);
    const char* waitReason = 0;

    // Allow TX/RX threads to properly start and synchronize
    Thread::msleep(100);
    data.prepareCalculate();
    int dumpTx = data.intParam(dc,"trace_dump_tx");
    BrfBbCalDataResult* res = new BrfBbCalDataResult[data.m_repeatRxLoop];
    unsigned int i = 0;
    // Disable DC/FPGA change debug message
    unsigned int& showCorrChange = dc ? m_state.m_tx.showDcOffsChange :
	m_state.m_tx.showFpgaCorrChange;
    showCorrChange++;
    if (!dc)
	m_state.m_tx.showPowerBalanceChange++;
    uint64_t ts = 0;
    uint64_t tsOffs = m_radioCaps.rxLatency;
    if (!dc)
	tsOffs += m_radioCaps.txLatency;
    for (; !status && calVal <= calValMax; calVal += step) {
	i = 0;
	*syncSet[corr] = calVal;
	BRF_FUNC_CALL_BREAK(setStateSyncTx(syncFlags[corr],error));
	ts = m_syncTxState.m_tx.m_timestamp + tsOffs;
	bool ok = false;
	for (; i < data.m_repeatRxLoop; ++i) {
	    res[i].status = 0;
	    if (traceRepeat && i) {
		String s;
		Output("  REPEAT[%u/%u] [%10s] %s=%-5d %s",i + 1,data.m_repeatRxLoop,
		    String(ts).c_str(),lookup(corr,s_corr),
		    calVal,data.dump(s,res[i-1]).c_str());
	    }
	    if (dumpTx) {
		if (dumpTx > 0)
		    showBuf(true,dumpTx,false);
		else
		    showBuf(true,-dumpTx,true);
	    }
	    ts += data.samples();
	    BRF_FUNC_CALL_BREAK(capture(false,data.buf(),data.samples(),ts,error));
	    if (m_calibrateStop)
		break;
	    if (trace > 4)
		showBuf(false,trace - 4,false);
	    ok = data.calculate(res[i]);
	    status = checkSampleLimit(data.buf(),data.samples(),limit,error);
	    if (status) {
		data.m_dump.appendFormatted(data.buffer(),false);
		if (trace) {
		    String s;
		    data.dump(s,true);
		    Output("  %s=%-5d [%10s] %s\tSAMPLE OUT OF RANGE",
			lookup(corr,s_corr),calVal,String(ts).c_str(),s.c_str());
		}
		res[i].status = status;
		if (i == (data.m_repeatRxLoop - 1))
		    break;
		status = 0;
		if (error)
		    error->clear();
		continue;
	    }
	    if (data.m_dump.valid() &&
		((ok && data.m_dump.dumpOk()) || (!ok && data.m_dump.dumpFail())))
		data.m_dump.appendFormatted(data.buffer(),ok);
	    res[i].status = ok ? 0 : RadioInterface::Failure;
	    if (ok)
		break;
	}
	if (status || m_calibrateStop)
	    break;
	if (i >= data.m_repeatRxLoop)
	    i = data.m_repeatRxLoop - 1;
	data.setResult(res[i]);
	bool better = (data.m_best > data.m_cal.value);
	if (accum) {
	    data.m_calAccum.append(data.m_cal);
	    data.m_testAccum.append(data.m_test);
	    data.m_totalAccum.append(data.m_total);
	}
	if (trace) {
	    String s;
	    if (trace > 1 && ok && (better || trace > 2))
		data.dump(s,trace > 2);
	    else if (!ok && traceFailed)
		data.dump(s,true);
	    if (s)
		Output("  %s=%-5d [%10s] %s%s",lookup(corr,s_corr),calVal,
		    String(ts).c_str(),s.c_str(),better ? "\tBEST" : "");
	}
	if (!ok && data.m_stopOnRecvFail) {
	    if (data.m_stopOnRecvFail < 0)
		waitReason = "Recv data check failure";
	    res[i].status = status = setErrorFail(error,"Recv data check failure");
	    break;
	}
	if (totalStop < data.m_total) {
	    waitReason = "Total error threshold reached";
	    res[i].status = status = setErrorFail(error,waitReason);
	    break;
	}
	// Update best values
	if (better) {
	    data.m_best = data.m_cal;
	    *corrVal[corr] = calVal;
	}
    }
    // Print last failures if we stopped due to data check failure
    if (status && !m_calibrateStop && status != RadioInterface::Cancelled &&
	(i == data.m_repeatRxLoop || res[i].status)) {
	String s;
	if (i < data.m_repeatRxLoop)
	    i++;
	for (unsigned int j = 0; j < i; j++) {
	    BrfBbCalDataResult& r = res[j];
	    String tmp;
	    s << tmp.printf(512,"\r\ntest_tone=%f total=%f test/total=%.2f cal_tone=%f cal/test=%.2f",
		r.test,r.total,r.test_total,r.cal,r.cal_test);
	    if (r.status == RadioInterface::Saturation)
		s << " (Sample out of range)";
	    else if (r.status) {
		if (error)
		    s << " (" << *error << ")";
		else
		    s << " (" << r.status << " " << RadioInterface::errorName(r.status) << ")";
	    }
	}
	Debug(owner(),DebugWarn,"BB Calibration (%s) stopping on data check failure."
	    " Signal values (test/total interval=(0.5-1]): [%p]\r\n-----%s\r\n-----",
	    lookup(corr,s_corr),this,s.c_str());
    }
    delete[] res;
    showCorrChange--;
    if (!dc)
	m_state.m_tx.showPowerBalanceChange--;
    duration.stop();
    if (trace)
	Output("  %d/%d [%s]: min/max - cal=%f/%f test=%f/%f total=%f/%f test/total=%.2f/%.2f",
	    (dc ? data.m_dcI : data.m_phase),(dc ? data.m_dcQ : data.m_gain),
	    duration.secStr(),
	    data.m_cal.min,data.m_cal.max,data.m_test.min,data.m_test.max,
	    data.m_total.min,data.m_total.max,data.m_test_total.min,data.m_test_total.max);
    if (data.m_dump.valid())
	data.dumpCorrEnd(dc);
    if (waitReason)
	return waitCancel("Calibration stopped",waitReason,error);
    return status;
}

unsigned int BrfLibUsbDevice::prepareCalibrateBb(BrfBbCalData& data, bool dc,
    String* error)
{
    Debug(m_owner,DebugAll,"prepareCalibrateBb dc=%d [%p]",dc,this);
    // Reset cal structure
    unsigned int status = 0;
    while (true) {
	BRF_FUNC_CALL_BREAK(isInitialized(true,true,error));
	unsigned int flags = DevStatFreq | DevStatLpfBw | DevStatSampleRate | DevStatVga;
	BrfDevState s(DevStatAbortOnFail | DevStatLoopback,flags,flags);
	s.m_tx.frequency = data.m_tx.frequency;
	s.m_tx.lpfBw = data.m_tx.lpfBw;
	s.m_tx.sampleRate = data.m_tx.sampleRate;
	data.m_calFreq = data.m_tx.frequency;
	data.m_calSampleRate = data.m_tx.sampleRate;
	unsigned int rxFreq = 0;
	unsigned int Fs = data.m_calSampleRate;
	unsigned int bw = data.m_rx.sampleRate;
	// Prepare device
	if (dc) {
	    // TX/RX frequency difference MUST be greater than 1MHz to avoid interferences
	    // rxFreq = FreqTx - (Fs / 4)
	    // Choose Fs (RX sample rate):
	    //   Fs > TxSampleRate
	    //   Fs / 4 > 1MHz => Fs > 4MHz
	    if (Fs < 4000000) {
		Fs = 4001000;
		bw = 3840000;
	    }
	    else {
		unsigned int delta = data.uintParam(dc,"samplerate_delta",10000);
		if (delta) {
		    Fs += delta;
		    // Round up to a multiple of 4 to avoid division errors
		    if ((Fs % 4) != 0)
			Fs = Fs + 4 - (Fs % 4);
		}
		// Choose next upper filter bandwidth after TX
		uint8_t bwIndex = bw2index(data.m_tx.lpfBw + 1);
		bw = index2bw(bwIndex);
		if (bw <= data.m_tx.lpfBw) {
		    // !!! OOPS !!!
		    return setErrorFail(error,"Unable to choose RX filter bandwidth");
		}
	    }
	    // cal, test
	    // For DC, test and cal differ by pi/2
	    // FIXME - This works only for RX and TX same sample rate.
	    rxFreq = data.m_tx.frequency - (Fs / 4);
	    data.resetOmega(-M_PI_2,-M_PI);
	}
	else {
	    // parameters for Gain/Phase calibration
	    // cal, test
	    // For phase/gain, test and cal differ by pi 
	    // FIXME - This works only for RX and TX same sample rate.
	    rxFreq = data.m_tx.frequency + (Fs / 4);
	    data.resetOmega(M_PI,0);
	}
	s.m_tx.lpfBw = bw;
	s.m_tx.sampleRate = Fs;
	s.m_rx.lpfBw = bw;
	s.m_rx.sampleRate = Fs;
	s.m_rx.frequency = rxFreq;
	s.m_tx.vga1 = data.intParam(dc,YSTRING("txvga1"),
	    BRF_TXVGA1_GAIN_DEF,BRF_TXVGA1_GAIN_MIN,BRF_TXVGA1_GAIN_MAX);
	s.m_tx.vga2 = data.intParam(dc,YSTRING("txvga2"),
	    20,BRF_TXVGA2_GAIN_MIN,BRF_TXVGA2_GAIN_MAX);
	s.m_rx.vga1 = data.intParam(dc,YSTRING("rxvga1"),
	    BRF_RXVGA1_GAIN_DEF,BRF_RXVGA1_GAIN_MIN,BRF_RXVGA1_GAIN_MAX);
	s.m_rx.vga2 = data.intParam(dc,YSTRING("rxvga2"),
	    BRF_RXVGA2_GAIN_DEF,BRF_RXVGA2_GAIN_MIN,BRF_RXVGA2_GAIN_MAX);
	if (dc) {
	    m_syncTxState.m_tx.fpgaCorrPhase = data.m_phase;
	    m_syncTxState.m_tx.fpgaCorrGain = data.m_gain;
	    s.m_tx.fpgaCorrPhase = data.m_phase;
	    s.m_tx.fpgaCorrGain = data.m_gain;
	    s.m_txChanged |= DevStatFpga;
	}
	else {
	    m_syncTxState.m_tx.dcOffsetI = data.m_dcI;
	    m_syncTxState.m_tx.dcOffsetQ = data.m_dcQ;
	    s.m_tx.dcOffsetI = data.m_dcI;
	    s.m_tx.dcOffsetQ = data.m_dcQ;
	    s.m_txChanged |= DevStatDc;
	}
	NamedList lpParams("");
	lpParams.copySubParams(data.m_params,YSTRING("loopback_"));
	int defLp =  brfIsLowBand(s.m_tx.frequency) ? LoopRfLna1 : LoopRfLna2;
	int lp = data.m_params.getIntValue(YSTRING("loopback"),s_loopback,defLp);
	s.setLoopback(lp,lpParams);
	// Stop I/O threads (use internal functions to set params)
	BRF_FUNC_CALL_BREAK(calThreadsPause(true,error));
	BRF_FUNC_CALL_BREAK(setState(s,error));
	// RX buffers may change: adjust it in cal data!
	unsigned int samples = getRxSamples(data.m_params);
	if (samples != data.samples())
	    data.resetBuffer(samples);
	// Toggle timestamps (reset FPGA timestamps)
	enableRfFpgaBoth(false);
	enableTimestamps(false);
	Thread::msleep(50);
	BRF_FUNC_CALL_BREAK(enableTimestamps(true,error));
	BRF_FUNC_CALL_BREAK(enableRfFpgaBoth(true,error));
	BRF_FUNC_CALL_BREAK(calThreadsPause(false,error));
	return 0;
    }
    return status;
}

unsigned int BrfLibUsbDevice::calibrateBb(BrfBbCalData& data, bool dc, String* error)
{
    const char* oper = dc ? "TX I/Q DC Offset (LO Leakage)" :
	"TX I/Q Imbalance";
    Debug(m_owner,DebugAll,"calibrateBb %s [%p]",oper,this);

    // VGA tests
    String e;
    unsigned int status = testVgaCheck(data.m_params,oper,data.omega(false),&e,
	data.prefix(dc));
    if (status) {
	e.printf(2048,"%s failed - %s",oper,e.c_str());
	return showError(status,e,0,error);
    }

    // FIXME: testing
    if (data.boolParam(dc,"disable"))
	return 0;

    // Prepare file dump
    m_dbgMutex.lock();
    String fName = dc ? m_bbCalDcFile : m_bbCalImbalanceFile;
    m_dbgMutex.unlock();
    data.initCal(*this,dc,fName);

    int level = DebugNote;
    bool dbg = m_owner && m_owner->debugAt(level);
    if (dbg || data.uintParam(dc,"trace")) {
	String s;
	if (data.boolParam(dc,"dump_status_start"))
	    dumpState(s,data.m_params,true);
	if (dbg)
	    Debug(m_owner,level,"%s calibration starting [%p]%s",oper,m_owner,encloseDashes(s,true));
	else
	    Output("%s calibration starting omega_cal=%f omega_test=%f [%p]%s",
		oper,data.omega(true),data.omega(false),m_owner,encloseDashes(s,true));
    }

    BrfDuration duration;
    int range = dc ? (BRF_TX_DC_OFFSET_MAX + 1) : BRF_FPGA_CORR_MAX;
    unsigned int loops = data.uintParam(dc,"loops",2,1,10);
    int step = dc ? 1 : 16*(1 << loops);
    unsigned int origSamples = 0;
    if (data.boolParam(dc,"increase_buffer",true))
	origSamples = data.samples();
    int corr1 = dc ? CorrLmsI : CorrFpgaPhase;
    int corr2 = dc ? CorrLmsQ : CorrFpgaGain;

    for (unsigned int pass = 1; !status && (range > 1) && pass <= loops; pass++) {
	BRF_FUNC_CALL_BREAK(calibrateBbCorrection(data,corr1,range,step,pass,&e));
	if (m_calibrateStop)
	    break;
	BRF_FUNC_CALL_BREAK(calibrateBbCorrection(data,corr2,range,step,pass,&e));
	if (m_calibrateStop)
	    break;
	range >>= 1;
	step >>= 1;
	if (!step || pass == (loops - 1))
	    step = 1;
	if (origSamples)
	    data.resetBuffer(data.samples() * 2);
    }

    if (origSamples)
	data.resetBuffer(origSamples);
    duration.stop();
    String result;
    if (!status) {
	if (dc)
	    result << "I=" << data.m_dcI << " " << "Q=" << data.m_dcQ;
	else
	    result << "PHASE=" << data.m_phase << " " << "GAIN=" << data.m_gain;
	Debug(m_owner,level,"%s calibration finished in %s %s [%p]",
	    oper,duration.secStr(),result.c_str(),m_owner);
    }

    // Dump result to file
    data.finalizeCal(result);

    // Wait for cancel ?
    if (!status && dc && !m_calibrateStop) {
	const String& i = data.m_params[YSTRING("stop_dc_i_out_of_range")];
	if (i && !isInterval(data.m_dcI,BRF_TX_DC_OFFSET_MIN,BRF_TX_DC_OFFSET_MAX,i))
	    status = waitCancel("Calibration stopped","DC I " +
		String(data.m_dcI) + " out of range " + i,&e);
	else {
	    const String& q = data.m_params[YSTRING("stop_dc_q_out_of_range")];
	    if (q && !isInterval(data.m_dcQ,BRF_TX_DC_OFFSET_MIN,BRF_TX_DC_OFFSET_MAX,q))
		status = waitCancel("Calibration stopped","DC Q " +
		    String(data.m_dcQ) + " out of range " + q,&e);
	}
    }

    if (!status)
	return 0;
    e.printf(2048,"%s failed - %s",oper,e.c_str());
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::calibrateBaseband(String* error)
{
    Configuration cfg;
    loadCfg(&cfg,false);
    NamedList& p = *cfg.createSection(YSTRING("calibrate-bb"));

    Debug(m_owner,DebugInfo,"Baseband calibration starting ... [%p]",m_owner);
    BrfDuration duration;
    m_calibrateStop = 0;
    String e;
    unsigned int status = 0;
    unsigned int chg = DevStatLoopback | DevStatTxPattern;
    unsigned int dirChg = DevStatFreq | DevStatSampleRate | DevStatVga | DevStatLpfBw;
    BrfDevState oldState(m_state,chg,dirChg,dirChg);
    setTxPattern(p.getValue(YSTRING("txpattern"),"circle"));
    BrfBbCalData data(getRxSamples(p),p);
    data.m_tx = m_state.m_tx;
    data.m_rx = m_state.m_rx;
    while (status == 0) {
	m_calibration.assign("");
	m_calibration.clearParams();
	BRF_FUNC_CALL_BREAK(writeLMS(p[YSTRING("lms_write")],&e,true));
	//
	// Calibrate TX LO Leakage (I/Q DC Offset)
	BRF_FUNC_CALL_BREAK(prepareCalibrateBb(data,true,&e));
	BRF_FUNC_CALL_BREAK(writeLMS(p[YSTRING("lms_write_alter")],&e,true));
	for (int n = data.intParam(true,"repeat",1,1); n && !m_calibrateStop; n--) {
	    data.m_dcI = data.m_dcQ = 0;
	    BRF_FUNC_CALL_BREAK(calibrateBb(data,true,&e));
	}
	if (status || m_calibrateStop) {
	    Debug(m_owner,DebugInfo,"Calibration stopping with status=%d stop=%d [%p]",
		status,m_calibrateStop,this);
	    break;
	}
	// Calibrate TX I/Q Imbalance
	// This will set TX DC I/Q also
	// test pattern and tuning data must change
	BRF_FUNC_CALL_BREAK(prepareCalibrateBb(data,false,&e));
	BRF_FUNC_CALL_BREAK(calibrateBb(data,false,&e));
	//
	// and do it all again
	// LO leakage
	if (status || m_calibrateStop) {
	    Debug(m_owner,DebugInfo,"Calibration stopping with status=%d stop=%d [%p]",
		status,m_calibrateStop,this);
	    break;
	}
	BRF_FUNC_CALL_BREAK(prepareCalibrateBb(data,true,&e));
	BRF_FUNC_CALL_BREAK(writeLMS(p[YSTRING("lms_write_alter")],&e,true));
	BRF_FUNC_CALL_BREAK(calibrateBb(data,true,&e));
	// I/Q balance
	if (status || m_calibrateStop) {
	    Debug(m_owner,DebugInfo,"Calibration stopping with status=%d stop=%d [%p]",
		status,m_calibrateStop,this);
	    break;
	}
	BRF_FUNC_CALL_BREAK(prepareCalibrateBb(data,false,&e));
	BRF_FUNC_CALL_BREAK(calibrateBb(data,false,&e));
	// Update calibrated data
	// Use initial tunning values: we may change them during calibration
	m_calibration.addParam("frequency",String(oldState.m_tx.frequency));
	m_calibration.addParam("samplerate",String(oldState.m_tx.sampleRate));
	m_calibration.addParam("filter",String(oldState.m_tx.lpfBw));
	m_calibration.addParam("cal_tx_dc_i",String(data.m_dcI));
	m_calibration.addParam("cal_tx_dc_q",String(data.m_dcQ));
	m_calibration.addParam("cal_tx_fpga_corr_phase",String(data.m_phase));
	m_calibration.addParam("cal_tx_fpga_corr_gain",String(data.m_gain));
	break;
    }
    Debug(m_owner,DebugAll,"Finalizing BB calibration [%p]",m_owner);

    // amplifier linearization
#if 0
    static const float startSweep = -20;
    static const float stopSweep = 0;
    static const float stepSweep = 1.0;
    ComplexVector sweep = sweepPower(startSweep, stopSweep, stepSweep);
    if (sweep.length()) {
	String tmp;
	sweep.dump(tmp,Math::dumpComplex," ","(%g,%g)");
	Debug(m_owner,DebugInfo,"amp sweep: %s [%p]",tmp.c_str(),this);
	findGainExpParams(sweep, startSweep, stepSweep);
	findPhaseExpParams(sweep, startSweep, stepSweep);
	calculateAmpTable();
    }
    else
	Debug(m_owner,DebugWarn,"amplifier calibration sweep failed");
#endif

    if (m_calibrateStop) {
	bool a = (m_calibrateStop < 0);
	m_calibrateStop = 0;
	Output("Calibration stopped: %s",(a ? "abort, no restore" : "restoring state"));
	if (a)
	    return status;
    }

    calThreadsPause(true);
    if (!status) {
	oldState.m_tx.dcOffsetI = data.m_dcI;
	oldState.m_tx.dcOffsetQ = data.m_dcQ;
	oldState.m_tx.fpgaCorrPhase = data.m_phase;
	oldState.m_tx.fpgaCorrGain = data.m_gain;
	oldState.m_txChanged |= DevStatDc | DevStatFpga;
	oldState.m_changed |= DevStatAbortOnFail;
	status = setState(oldState,&e);
    }
    else
	setState(oldState);
    writeLMS(p[YSTRING("lms_write_post")],0,true);
    duration.stop();
    if (status == 0) {
	String tmp;
	m_calibration.dump(tmp,"\r\n");
	Debug(m_owner,DebugNote,"Baseband calibration ended in %s [%p]%s",
	    duration.secStr(),m_owner,encloseDashes(tmp,true));
	return 0;
    }
    e.printf(1024,"BB calibration failed: %s",e.c_str());
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::loopbackCheck(String* error)
{
    Configuration cfg;
    loadCfg(&cfg,false);
    NamedList& p = *cfg.createSection(YSTRING("loopback-check"));
    // Prepare data dump
    m_dbgMutex.lock();
    BrfDumpFile dump(&p,m_devCheckFile);
    m_dbgMutex.unlock();

    Debug(m_owner,DebugNote,"Loopback check starting ... [%p]",m_owner);
    BrfDuration duration;
    String e;
    unsigned int status = 0;
    unsigned int chg = DevStatLoopback | DevStatTxPattern;
    unsigned int dirChg = DevStatFreq | DevStatVga | DevStatLpfBw | DevStatSampleRate;
    BrfDevState oldState(m_state,chg,dirChg,dirChg);
    setTxPattern(p.getValue(YSTRING("txpattern"),"circle"));
    while (status == 0) {
	unsigned int txFreq = p.getIntValue(YSTRING("txfrequency"),
	    m_state.m_tx.frequency,0);
	if (!txFreq)
	    BRF_FUNC_CALL_BREAK(setErrorFail(&e,"Frequency not set"));
	unsigned int nBuffs = p.getIntValue("buffers",10,1);
	unsigned int bw = m_state.m_tx.lpfBw;
	unsigned int sampleRate = m_state.m_tx.sampleRate;
	bw = p.getIntValue(YSTRING("bandwidth"),bw ? bw : 1500000,1500000);
	sampleRate = p.getIntValue(YSTRING("samplerate"),
	    sampleRate ? sampleRate : 2166667,2166667);
	if (!sampleRate)
	    BRF_FUNC_CALL_BREAK(setErrorFail(&e,"Sample rate not set"));
	// deltaFreq = RxFreq - TxFreq
	// deltaFreq MUST be
	// (1) 1MHz < deltaFreq  < (sampleRate / 2)
	// (2) deltaFreq != (sampleRate / 4)
	// Sampling rate MUST be greater than 2MHz
	unsigned int minDeltaFreq = 1000001;
	unsigned int maxDeltaFreq = sampleRate / 2 - 1;
	unsigned int deltaFreq = p.getIntValue(YSTRING("delta_freq"),
	    minDeltaFreq + (maxDeltaFreq - minDeltaFreq) / 2,minDeltaFreq,maxDeltaFreq);
	if (deltaFreq == (sampleRate / 4)) {
	    // TODO: Properly adjust it
	    Debug(m_owner,DebugStub,"Loopback check adjusting delta freq [%p]",m_owner);
	    deltaFreq += 1000;
	}
	// Sanity check
	if (deltaFreq <= 1000000 || (deltaFreq >= (sampleRate / 2)) ||
	    (deltaFreq == (sampleRate / 4))) {
	    e.printf("Invalid delta freq %u samplerate=%u",deltaFreq,sampleRate);
	    status = RadioInterface::Failure;
	    break;
	}
	unsigned int rxFreq = txFreq + deltaFreq;

	// Prepare device
	unsigned int flags = DevStatLpfBw | DevStatSampleRate | DevStatFreq | DevStatVga;
	BrfDevState s(DevStatAbortOnFail | DevStatLoopback,flags,flags);
	s.m_tx.lpfBw = bw;
	s.m_rx.lpfBw = bw;
	s.m_tx.sampleRate = sampleRate;
	s.m_rx.sampleRate = sampleRate;
	s.m_tx.frequency = txFreq;
	s.m_rx.frequency = rxFreq;
	s.m_tx.vga1 = p.getIntValue(YSTRING("txvga1"),
	    BRF_TXVGA1_GAIN_DEF,BRF_TXVGA1_GAIN_MIN,BRF_TXVGA1_GAIN_MAX);
	s.m_tx.vga2 = p.getIntValue(YSTRING("txvga2"),
	    BRF_TXVGA2_GAIN_DEF,BRF_TXVGA2_GAIN_MIN,BRF_TXVGA2_GAIN_MAX);
	s.m_rx.vga1 = p.getIntValue(YSTRING("rxvga1"),
	    BRF_RXVGA1_GAIN_DEF,BRF_RXVGA1_GAIN_MIN,BRF_RXVGA1_GAIN_MAX);
	s.m_rx.vga2 = p.getIntValue(YSTRING("rxvga2"),
	    BRF_RXVGA2_GAIN_DEF,BRF_RXVGA2_GAIN_MIN,BRF_RXVGA2_GAIN_MAX);
	NamedList lpParams("");
	lpParams.copySubParams(p,YSTRING("loopback_"));
	int defLp =  brfIsLowBand(txFreq) ? LoopRfLna1 : LoopRfLna2;
	int lp = p.getIntValue(YSTRING("loopback"),s_loopback,defLp);
	s.setLoopback(lp,lpParams);
	// Stop I/O threads (use internal functions to set params)
	BRF_FUNC_CALL_BREAK(calThreadsPause(true,&e));
	BRF_FUNC_CALL_BREAK(setState(s,&e));
	// Toggle timestamps (reset FPGA timestamps)
	enableRfFpgaBoth(false);
	enableTimestamps(false);
	Thread::idle();
	BRF_FUNC_CALL_BREAK(enableTimestamps(true,&e));
	BRF_FUNC_CALL_BREAK(enableRfFpgaBoth(true,&e));
	BRF_FUNC_CALL_BREAK(calThreadsPause(false,&e));

	// Utility: check / write LMS
	checkLMS(p[YSTRING("lms_check")],0,true);
	BRF_FUNC_CALL_BREAK(writeLMS(p[YSTRING("lms_write")],&e,true));

	Thread::msleep(50);

	// Set read / test signal buffers. Generate tone
	float omega = ((float)sampleRate / 4 - deltaFreq) * 2 * M_PI / sampleRate;
	ComplexVector buf(getRxSamples(p));
	ComplexVector testTone(buf.length());
	omega = -omega;
	generateExpTone(testTone,omega);

	float limit = getSampleLimit(p);

	ComplexVector testPattern;
	const String& pattern = p[YSTRING("test_pattern")];
	if (pattern) {
	    String ep;
	    if (!buildVector(ep,pattern,testPattern,buf.length(),true)) {
		status = RadioInterface::Failure;
		e << "invalid/unknown test_pattern='" << pattern << "' - " << ep;
		break;
	    }
	    if (testPattern.length() > buf.length())
		testPattern.resize(buf.length());
	}

	unsigned int trace = p.getIntValue(YSTRING("trace"),0,0);
	bool dumpTxTs = (trace > 1) && p.getBoolValue("dump_tx_ts");
	String t;
	if (trace) {
	    if (p.getBoolValue("dump_status_start"))
		dumpState(t,p,true);
	    String tmp;
	    unsigned int h = p.getIntValue("dump_test_tone",0,0);
	    if (h) {
		if (h > testTone.length())
		    h = testTone.length();
		tmp.printf("TEST TONE HEAD(%d):",h);
		testTone.head(h).dump(tmp,Math::dumpComplex," ","(%g,%g)");
	    }
	    if (testPattern.length()) {
		h = p.getIntValue("dump_test_pattern",0,0);
		if (h) {
		    String t2;
		    t2.printf("TEST PATTERN len=%u HEAD(%d):",testPattern.length(),h);
		    if (h > testPattern.length())
			h = testPattern.length();
		    testPattern.head(h).dump(t2,Math::dumpComplex," ","(%g,%g)");
		    tmp.append(t2,"\r\n");
		}
	    }
	    t.append(tmp,"\r\n");
	    Output("Loopback check: frequency tx=%u rx=%u (delta=%u omega=%f) "
		"samplerate=%u bandwidth=%u samples=%u buffers=%u [%p]%s",
		txFreq,rxFreq,deltaFreq,omega,sampleRate,bw,buf.length(),
		nBuffs,m_owner,encloseDashes(t,true));
	}
	else if (p.getBoolValue("dump_dev")) {
	    String t;
	    dumpState(t,p,true);
	    Debug(m_owner,DebugNote,"Loopback check. Device params: [%p]%s",this,encloseDashes(t));
	}
	// Dump header to file
	if (dump.dumpHeader()) {
	    String* tmp = new String;
	    dumpState(*tmp,p,true,true);
	    String extra;
	    extra.printf("\r\nSAMPLES: %u\r\nBUFFERS: %u\r\nomega: %f\r\ndelta_freq=%u",
		buf.length(),nBuffs,omega,deltaFreq);
	    *tmp << "\r\n" << extra << "\r\n";
	    dump.append(tmp);
	}

	// Run it
	int dumpRxBeforeRead = p.getIntValue(YSTRING("dump_before_read_rx"),0,0);
	int dumpTxBeforeRead = p.getIntValue(YSTRING("dump_before_read_tx"),0,0);
	unsigned int tmp = nBuffs / 4;
	unsigned int limitFailures =
	    p.getIntValue(YSTRING("sample_limit_allow_fail"),tmp,0,nBuffs - 1);
	unsigned int allowFail = p.getIntValue(YSTRING("allow_fail"),tmp,0,nBuffs - 1);
	for (int i = 0; i < (int)nBuffs; i++) {
	    if (dumpRxBeforeRead) {
		dumpRxBeforeRead--;
		showBuf(false,1,false);
	    }
	    if (dumpTxBeforeRead || dumpTxTs) {
		if (dumpTxBeforeRead)
		    dumpTxBeforeRead--;
		showBuf(true,1,dumpTxTs);
	    }
	    BRF_FUNC_CALL_BREAK(setStateSyncTx(0,&e));
	    uint64_t ts = m_syncTxState.m_tx.m_timestamp + m_radioCaps.rxLatency;
	    BRF_FUNC_CALL_BREAK(capture(false,(float*)buf.data(),buf.length(),ts,&e));
	    // Check for out of range values
	    status = checkSampleLimit((float*)buf.data(),buf.length(),limit,&e);
	    if (status) {
		if (trace)
		    Output("%-5u [%10s]\tsample invalid (remains=%d): %s",
			i,String(ts).c_str(),limitFailures,e.c_str());
		if (!limitFailures)
		    break;
		limitFailures--;
		i--;
		e.clear();
		status = 0;
		continue;
	    }
	    // Apply test pattern (check algorithm)
	    if (testPattern.length())
		buf.copy(testPattern,testPattern.length());
	    // Calculate test / total signal
	    const Complex* last = 0;
	    const Complex* b = buf.data(0,buf.length(),last);
	    Complex testSum;
	    float total = 0;
	    for (const Complex* tt = testTone.data(); b != last; ++b, ++tt) {
		total += b->norm2();
		testSum += *tt * *b;
	    }
	    float test = testSum.norm2() / buf.length();
	    bool ok = ((0.5 * total) < test) && (test <= total);
	    float ratio = total ? test / total : -1;
	    if (trace > 1)
		Output("%-5u [%10s]\ttest:%-15f total:%-15f %.2f %s",
		    i,String(ts).c_str(),test,total,ratio,(ok ? "" : "FAILURE"));

	    // Dump to file
	    if ((ok && dump.dumpOk()) || (!ok && dump.dumpFail())) {
		String* tmp = new String;
		tmp->printf("\r\n# %u [%s] test:%f total:%f\r\n",
		    i,(ok ? "SUCCESS" : "FAILURE"),test,total);
		dump.append(tmp);
		dump.appendFormatted(buf,ok);
	    }

	    if (ok)
		continue;
	    e.printf("test_tone_power=%f total_energy=%f (%.2f)",test,total,ratio);
	    if (!allowFail) {
		status = RadioInterface::Failure;
		break;
	    }
	    allowFail--;
	    DDebug(m_owner,DebugInfo,"Loopback check failure %s [%p]",e.safe(),m_owner);
	    e.clear();
	}
	if (status)
	    break;
	if (trace == 1)
	    Output("Loopback check succesfully ended");
	// VGA test
	BRF_FUNC_CALL_BREAK(testVgaCheck(p,"Loopback check",omega,&e));
	break;
    }
    Debug(m_owner,DebugAll,"Finalizing loopback check [%p]",m_owner);
    if (!status) {
	calThreadsPause(true);
	status = setState(oldState,&e);
	calThreadsPause(false);
    }
    duration.stop();
    if (status == 0) {
	Debug(m_owner,DebugNote,"Loopback check ended duration=%s [%p]",
	    duration.secStr(),m_owner);
	return 0;
    }
    e.printf(1024,"Loopback check failed: %s",e.c_str());
    return showError(status,e,0,error);
}

unsigned int BrfLibUsbDevice::testVga(const char* loc, bool tx, bool preMixer,
    float omega, String* error)
{
    Configuration cfg;
    loadCfg(&cfg,false);
    NamedList& params = *cfg.createSection("test-vga");
    String e;
    unsigned int status = 0;

    String what;
    what << (tx ? "tx_vga" : "rx_vga") << mixer(preMixer);
    String testName;
    testName.printf("Test %s VGA %c",brfDir(tx),mixer(preMixer));
    String fName = params.getValue("dump_file","test_${what}_${sec_now}");
    replaceDumpParams(fName,new NamedString("what",what),true,
	new NamedString("loopback",lookup(m_state.m_loopback,s_loopback)));
    BrfDumpFile dump(&NamedList::empty(),fName,true);
    if (!dump.valid()) {
	int e = dump.file().error();
	Debug(m_owner,DebugNote,"%s '%s' failed to create dump file '%s': %d [%p]",
	    testName.c_str(),loc,dump.fileName().c_str(),e,m_owner);
	return 0;
    }

    int start = 0;
    int end = 0;
    uint8_t mask = 0;
    uint8_t shift = 0;
    #define BRF_TEST_INIT(from,to,ma,sh) { \
	start = from; \
	end = to; \
	mask = ma; \
	shift = sh; \
    }
    if (tx) {
	if (preMixer)
	    BRF_TEST_INIT(BRF_TXVGA1_GAIN_MIN,BRF_TXVGA1_GAIN_MAX,0x1f,0)
	else
	    BRF_TEST_INIT(BRF_TXVGA2_GAIN_MIN,BRF_TXVGA2_GAIN_MAX,0xf8,3)
    }
    else if (preMixer)
	BRF_TEST_INIT(BRF_RXVGA1_GAIN_MIN,BRF_RXVGA1_GAIN_MAX,0x7f,0)
    else
	BRF_TEST_INIT(BRF_RXVGA2_GAIN_MIN,BRF_RXVGA2_GAIN_MAX,0x1f,0)
    #undef BRF_TEST_INIT
    unsigned int flags = preMixer ? DevStatVga1 : DevStatVga2;
    unsigned int len = end - start + 1;
    BrfDevState oldState(m_state,0,DevStatVga,DevStatVga);

    FloatVector totalMed(len);
    FloatVector totalMin(len);
    FloatVector totalMax(len);
    FloatVector totalDelta(len);
    FloatVector testMed(len);
    FloatVector testDelta(len);
    FloatVector testMin(len);
    FloatVector testMax(len);
    FloatVector testTotalMed(len);
    FloatVector testTotalDelta(len);
    FloatVector testTotalMin(len);
    FloatVector testTotalMax(len);
    totalMin.fill(BRF_MAX_FLOAT);
    totalMax.fill(-BRF_MAX_FLOAT);
    testMin.fill(BRF_MAX_FLOAT);
    testMax.fill(-BRF_MAX_FLOAT);
    testTotalMin.fill(BRF_MAX_FLOAT);
    testTotalMax.fill(-BRF_MAX_FLOAT);

    ComplexVector buf(getRxSamples(params));
    ComplexVector testTone(buf.length());
    generateExpTone(testTone,omega);

    const String& regFmt = params["dump_reg"];
    uint8_t addr = 0;
    DataBlock regVal;
    if (regFmt) {
	addr = lmsVgaAddr(tx,preMixer);
	regVal.resize(len);
    }
    bool div = params.getBoolValue("divide_by_samples");
    float limit = getSampleLimit(params);
    unsigned int nBuffs = params.getIntValue("buffers",10,2);

    // Set all VGA values to default
    m_syncTxState.m_tx.vga1 = BRF_TXVGA1_GAIN_DEF;
    m_syncTxState.m_tx.vga2 = BRF_TXVGA2_GAIN_DEF;
    m_syncTxState.m_rx.vga1 = BRF_RXVGA1_GAIN_DEF;
    m_syncTxState.m_rx.vga2 = BRF_RXVGA2_GAIN_DEF;
    m_syncTxState.setFlags(0,DevStatVga,DevStatVga);
    status = setStateSync(&e);
    // Dump the header now to have current VGA values
    const String& hdr = params["dump_header"];
    if (hdr) {
	NamedString* ns = new NamedString("data");
	if (loc)
	    *ns << "\r\n" << loc;
	String tmp;
	tmp.printf("\r\n%s\r\nRange: [%d..%d] (%u)\r\n\r\n",
	    testName.c_str(),start,end,len);
	*ns << tmp;
	dumpState(*ns,params,true,true);
	if (*ns)
	    *ns << "\r\n";
	*ns <<
	    "\r\nSAMPLES: " << buf.length() <<
	    "\r\nBUFFERS: " << nBuffs;
	dump.append(replaceDumpParamsFmt(hdr,ns));
    }

    String tmp;
    BrfDevDirState& setSync = tx ? m_syncTxState.m_tx : m_syncTxState.m_rx;
    int& set = preMixer ? setSync.vga1 : setSync.vga2;
    for (unsigned int i = 0; !status && i < len; i++) {
	set = start + i;
	m_syncTxState.setFlags(0,tx ? flags : 0,tx ? 0 : flags);
	BRF_FUNC_CALL_BREAK(setStateSync(&e));
	Thread::msleep(100);
	BRF_FUNC_CALL_BREAK(setStateSyncTx(0,&e));
	uint64_t ts = m_syncTxState.m_tx.m_timestamp + m_radioCaps.rxLatency;
	if (regVal.length())
	    readLMS(addr,*regVal.data(i),0,true);
	for (unsigned int n = 0; n < nBuffs; n++) {
	    BRF_FUNC_CALL_BREAK(capture(false,(float*)buf.data(),buf.length(),ts,&e));
	    ts += buf.length();
	    // Check for out of range values
	    BRF_FUNC_CALL_BREAK(checkSampleLimit((float*)buf.data(),buf.length(),
		limit,&e));
	    // Calculate test / total signal
	    const Complex* last = 0;
	    const Complex* b = buf.data(0,buf.length(),last);
	    Complex testSum;
	    float tmpTotal = 0;
	    for (const Complex* tt = testTone.data(); b != last; ++b, ++tt) {
		tmpTotal += b->norm2();
		testSum += *tt * *b;
	    }
	    float tmpTest = testSum.norm2() / buf.length();
	    if (div) {
		tmpTotal /= buf.length();
		tmpTest /= buf.length();
	    }
	    float t_t = 100 * (tmpTotal ? (tmpTest / tmpTotal) : 0);
	    setMinMax(totalMin[i],totalMax[i],tmpTotal);
	    setMinMax(testMin[i],testMax[i],tmpTest);
	    setMinMax(testTotalMin[i],testTotalMax[i],t_t);
	    totalMed[i] += tmpTotal;
	    testMed[i] += tmpTest;
	    testTotalMed[i] += t_t;
	}
	if (status)
	    break;
	totalMed[i] /= nBuffs;
	testMed[i] /= nBuffs;
	testTotalMed[i] /= nBuffs;
	if (totalMed[i])
	    totalDelta[i] = 100 * (totalMax[i] - totalMin[i]) / totalMed[i];
	if (testMed[i])
	    testDelta[i] = 100 * (testMax[i] - testMin[i]) / testMed[i];
	if (testTotalMed[i])
	    testTotalDelta[i] = 100 * (testTotalMax[i] - testTotalMin[i]) / testTotalMed[i];
    }
    m_syncTxState = oldState;
    setStateSync();
    Debug(m_owner,DebugInfo,"%s '%s' dumping to '%s' [%p]",
	testName.c_str(),loc,dump.fileName().c_str(),m_owner);
    String count(len);
    if (regVal.length()) {
	NamedString* a = new NamedString("address","0x" + tmp.hexify(&addr,1));
	NamedString* reg_val = new NamedString("data");
	NamedString* value = new NamedString("value");
	for (unsigned int i = 0; i < regVal.length(); i++) {
	    uint8_t* d = regVal.data(i);
	    reg_val->append("0x" + tmp.hexify(d,1),",");
	    value->append(String((*d & mask) >> shift),",");
	}
	dump.append(replaceDumpParamsFmt(regFmt,a,false,reg_val,value));
    }
    #define BRF_CHECK_FMT_DUMP_VECT(param,v) { \
	const String& fmt = params[param]; \
	if (fmt) \
	    dump.appendFormatted(v,fmt); \
    }
    BRF_CHECK_FMT_DUMP_VECT("dump_total_med",totalMed);
    BRF_CHECK_FMT_DUMP_VECT("dump_total_delta",totalDelta);
    BRF_CHECK_FMT_DUMP_VECT("dump_test_med",testMed);
    BRF_CHECK_FMT_DUMP_VECT("dump_test_delta",testDelta);
    BRF_CHECK_FMT_DUMP_VECT("dump_test_total_med",testTotalMed);
    BRF_CHECK_FMT_DUMP_VECT("dump_test_total_delta",testTotalDelta);
    #undef BRF_CHECK_FMT_DUMP_VECT
    const String& mm = params["dump_total_minmax"];
    if (mm)
	dump.append(replaceDumpParamsFmt(mm,new NamedString("count",count),
	    false,dumpNsData(totalMin,"total_min"),dumpNsData(totalMax,"total_max")));
    const String& extra = params["dump_extra"];
    if (extra)
	dump.append(replaceDumpParamsFmt(extra,new NamedString("count",count)));
    // Done dumping
    if (!status)
	return 0;
    e.printf(2048,"%s '%s' failed - %s",loc,testName.c_str(),e.c_str());
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
    if (!(dbg || m_state.m_rxDcAuto))
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
    if (!m_state.m_rxDcAuto)
	return;
    int corrI = computeCorrection(m_rxDcAvgI,m_state.m_rx.dcOffsetI,dcIAvg,m_rxDcOffsetMax);
    int corrQ = computeCorrection(m_rxDcAvgQ,m_state.m_rx.dcOffsetQ,dcQAvg,m_rxDcOffsetMax);
    if (corrI == m_state.m_rx.dcOffsetI && corrQ == m_state.m_rx.dcOffsetQ)
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
    switch (code) {
	case RadioInterface::Pending:
	case RadioInterface::Cancelled:
	    level = DebugAll;
	    break;
    }
    Debug(m_owner,level,"%s [%p]",tmp.c_str(),m_owner);
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
unsigned int BrfLibUsbDevice::internalSetLoopback(int mode, const NamedList& params,
    String* error)
{
    if (m_state.m_loopback == mode)
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
		    lna = LmsLna3;
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
		break;
	    case LoopNone:
		BRF_FUNC_CALL_BREAK(restoreFreq(true,&e));
		BRF_FUNC_CALL_BREAK(internalEnableRxVga(true,true,&e));
		BRF_FUNC_CALL_BREAK(internalSetLpf(false,LpfNormal,&e));
		BRF_FUNC_CALL_BREAK(internalEnableRxVga(true,false,&e));
		BRF_FUNC_CALL_BREAK(lnaEnable(true,&e));
		BRF_FUNC_CALL_BREAK(restoreFreq(false,&e));
		lna = LmsLnaDetect;
		break;
	    default:
		Debug(m_owner,DebugStub,"Loopback: unhandled value %d [%p]",mode,m_owner);
		status = setUnkValue(e,"mode " + String(mode));
	}
	if (status)
	    break;
	BRF_FUNC_CALL_BREAK(setLoopbackPath(mode,e));
	bool lowBand = brfIsLowBand(m_state.m_tx.frequency);
	if (lna == LmsLnaDetect)
	    BRF_FUNC_CALL_BREAK(lnaSelect(lowBand ? LmsLna1 : LmsLna2,&e));
	if (params.getBoolValue(YSTRING("transmit"),mode == LoopNone))
	    BRF_FUNC_CALL_BREAK(paSelect(lowBand,&e));
	break;
    }
    if (status == 0) {
	Debug(m_owner,DebugNote,"Loopback changed '%s' -> '%s' [%p]",
	    lookup(m_state.m_loopback,s_loopback),what,m_owner);
	m_state.setLoopback(mode,params);
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

void BrfLibUsbDevice::dumpLoopbackStatus(String* dest)
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
    s << "\r\nRX PATH:";
    status = lmsRead(0x55,data);
    BRF_LS_RESULT("BYP_EN_LPF",0x40); // LPF bypass enable
    if (!status)
	s << " - " << lookup((data & 0x40) == 0x40 ? LpfBypass : LpfNormal,s_lpf);
    status = lmsRead(0x09,data);
    BRF_LS_RESULT_OPEN("RXOUTSW",0x80,0x00); // RXOUTSW switch
    if (dest)
	*dest = s;
    else
	Debug(m_owner,DebugAll,"Loopback switches: [%p]%s",m_owner,encloseDashes(s));
#undef BRF_LS_RESULT
#undef BRF_LS_RESULT_OPEN
}

void BrfLibUsbDevice::dumpLmsModulesStatus(String* dest)
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
    if (dest)
	*dest = s;
    else
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

unsigned int BrfLibUsbDevice::startCalibrateThreads(String* error, const NamedList& params)
{
    static String s_s[3] = {"recv_data", "send_data", "calibrate"};
    static const char* s_n[3] = {"BrfDevRecv", "BrfDevSend", "BrfDevCalibrate"};
    static int s_t[3] = {BrfThread::DevRecv, BrfThread::DevSend, BrfThread::DevCalibrate};
    static Thread::Priority s_prio[3] = {Thread::High, Thread::High, Thread::Normal};

    stopThreads();
    BrfThread** threads[3] = {&m_recvThread,&m_sendThread,&m_calThread};
    int i = 0;
    for (; i < 3; i++) {
	BrfThread*& th = *threads[i];
	const char* prioStr = params.getValue(s_s[i] + "_priority");
	Thread::Priority prio = Thread::priority(prioStr,s_prio[i]);
	th = new BrfThread(this,s_t[i],params,s_n[i],prio);
	th = th->start();
	if (!th)
	    break;
    }
    if (i >= 3)
	return 0;
    stopThreads();
    String e;
    e << "Failed to start " << s_s[i] << " worker thread";
    return showError(RadioInterface::Failure,e,0,error);
}

unsigned int BrfLibUsbDevice::calThreadsPause(bool on, String* error)
{
    unsigned int status = 0;
    if (on) {
    	status = BrfThread::pause(m_sendThread,&m_threadMutex,error);
	if (!status)
	    status = BrfThread::pause(m_recvThread,&m_threadMutex,error);
    }
    else {
	status = BrfThread::resume(m_recvThread,&m_threadMutex,error);
	if (!status)
	    status = BrfThread::resume(m_sendThread,&m_threadMutex,error);
    }
    return status;
}

// Stop internal threads
void BrfLibUsbDevice::stopThreads()
{
    // Soft cancel
    BrfThread::cancelThread(m_calThread,&m_threadMutex,0,m_owner,m_owner);
    // Wait for a while (avoid calibrate failure due to I/O thread termination)
    if (m_calThread)
	Thread::msleep(20);
    BrfThread::cancelThread(m_sendThread,&m_threadMutex,0,m_owner,m_owner);
    BrfThread::cancelThread(m_recvThread,&m_threadMutex,0,m_owner,m_owner);
    // Hard cancel
    BrfThread::cancelThread(m_calThread,&m_threadMutex,1000,m_owner,m_owner);
    BrfThread::cancelThread(m_sendThread,&m_threadMutex,1000,m_owner,m_owner);
    BrfThread::cancelThread(m_recvThread,&m_threadMutex,1000,m_owner,m_owner);
    m_internalIoSemaphore.unlock();
    m_internalIoTimestamp = 0;
}

unsigned int BrfLibUsbDevice::waitCancel(const char* loc, const char* reason,
    String* error)
{
    Debug(m_owner,DebugGoOn,"%s: %s. Waiting for cancel... [%p]",loc,reason,m_owner);
    unsigned int status = 0;
    while (!status && !m_calibrateStop) {
	Thread::idle();
	status = cancelled(error);
    }
    return status;
}

// Apply parameters from start notification message
unsigned int BrfLibUsbDevice::applyStartParams(const NamedList& params, String* error)
{
    const NamedString* fOffs = 0;
    const NamedString* dc[] = {0,0};
    const NamedString* fpga[] = {0,0};
    bool haveParams = false;
    for (const ObjList* o = params.paramList()->skipNull(); o; o = o->skipNext()) {
	const NamedString* ns = static_cast<const NamedString*>(o->get());
	if (ns->name() == YSTRING("RadioFrequencyOffset"))
	    fOffs = ns;
	else if (ns->name() == YSTRING("tx_dc_i"))
	    dc[0] = ns;
	else if (ns->name() == YSTRING("tx_dc_q"))
	    dc[1] = ns;
	else if (ns->name() == YSTRING("tx_fpga_corr_phase"))
	    fpga[0] = ns;
	else if (ns->name() == YSTRING("tx_fpga_corr_gain"))
	    fpga[1] = ns;
	else
	    continue;
	haveParams = true;
    }
    if (!haveParams)
	return 0;
    unsigned int status = 0;
    if (fOffs) {
	float f = fOffs->toDouble(m_freqOffset);
	f = clampFloat(f,BRF_FREQ_OFFS_MIN,BRF_FREQ_OFFS_MAX,fOffs->name());
	BRF_FUNC_CALL_RET(internalSetFreqOffs(f,0,error));
    }
    if (dc[0] && dc[1]) {
	for (int i = 0; i < 2; i++) {
	    int val = dc[i]->toInteger();
	    val = clampInt(val,BRF_TX_DC_OFFSET_MIN,BRF_TX_DC_OFFSET_MAX,dc[i]->name());
	    BRF_FUNC_CALL_RET(internalSetDcOffset(true,i == 0,val,error));
	}
    }
    else if (dc[0] || dc[1])
	Debug(m_owner,DebugConf,"Initialize. Ignoring %s: tx_dc_%c is missing [%p]",
	    (dc[0] ? dc[0] : dc[1])->name().c_str(),(dc[0] ? 'q' : 'i'),m_owner);
    if (fpga[0] && fpga[1]) {
	for (int i = 0; i < 2; i++) {
	    int val = fpga[i]->toInteger();
	    val = clampInt(val,-BRF_FPGA_CORR_MAX,BRF_FPGA_CORR_MAX,fpga[i]->name());
	    BRF_FUNC_CALL_RET(internalSetFpgaCorr(true,i ? CorrFpgaGain : CorrFpgaPhase,val,error));
	}
    }
    else if (fpga[0] || fpga[1])
	Debug(m_owner,DebugConf,"Initialize. Ignoring %s: tx_fpga_corr_%s is missing [%p]",
	    (fpga[0] ? fpga[0] : fpga[1])->name().c_str(),(fpga[0] ? "gain" : "phase"),m_owner);
    return 0;
}


//
// BrfInterface
//
BrfInterface::BrfInterface(const char* name)
    : RadioInterface(name),
    m_dev(0)
{
    debugChain(&__plugin);
    Debug(this,DebugAll,"Interface created [%p]",this);
}

BrfInterface::~BrfInterface()
{
    Debug(this,DebugAll,"Interface destroyed [%p]",this);
}

void BrfInterface::notifyError(unsigned int status, const char* str, const char* oper)
{
    if (!status)
	return;
    Message* m = new Message("module.update",0,true);
    m->addParam("module",__plugin.name());
    m->addParam("status","failure");
    m->addParam("operation",oper,false);
    completeDevInfo(*m);
    setError(*m,status,str);
    Engine::enqueue(m);
}

unsigned int BrfInterface::init(const NamedList& params, String& error)
{
    if (m_dev)
	return 0;
    unsigned int status = 0;
    if (!s_usbContextInit) {
	Lock lck(__plugin);
	if (!s_usbContextInit) {
	    status = BrfLibUsbDevice::lusbCheckSuccess(::libusb_init(0),&error,"libusb init failed");
	    if (!status) {
		Debug(&__plugin,DebugAll,"Initialized libusb context");
		s_usbContextInit = true;
		lusbSetDebugLevel();
	    }
	    else
		Debug(this,DebugNote,"Failed to create device: %s [%p]",error.c_str(),this);
	}
    }
    if (!status) {
	m_dev = new BrfLibUsbDevice(this);
	m_radioCaps = &m_dev->capabilities();
	Debug(this,DebugAll,"Created device (%p) [%p]",m_dev,this);
	status = m_dev->open(params,error);
    }
    return status;
}

unsigned int BrfInterface::initialize(const NamedList& params)
{
    return m_dev->initialize(params);
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
#define METH_CALL_2(func,value1,value2) { \
    unsigned int c = func(value1,value2); \
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
	if (cmd == YSTRING("setTxFrequency"))
	    METH_CALL_2(setFrequency,(uint64_t)ns->toInt64(),true);
	if (cmd == YSTRING("setRxFrequency"))
	    METH_CALL_2(setFrequency,(uint64_t)ns->toInt64(),false);
	if (cmd == "calibrate")
	    METH_CALL(calibrate);
	if (cmd.startsWith("devparam:")) {
	    unsigned int c = NotInitialized;
	    if (m_dev) {
		NamedList p("");
		p.copySubParams(params,cmd + "_");
		c = m_dev->setParam(cmd.substr(9),*ns,p);
	    }
	    SETPARAMS_HANDLE_CODE(c);
	    continue;
	}
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
#ifdef XDEBUG
    if (size) {
	float sum = 0.0F;
	for (unsigned i=0; i<2*size; i++)
	    sum += samples[i]*samples[i];
	float dB = 10.0F*log10f(sum/size);
	float scaleDB = 0.0F;
	if (powerScale) scaleDB = 20.0F*log10f(*powerScale);
	XDebug(this,DebugAll,"Sending at time " FMT64U " power %f dB to be scaled %f dB [%p]",
		when, dB, scaleDB, this);
    }
#endif
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
    unsigned int status = m_dev->setFrequency(hz,tx);
    if (status)
	return status;
    uint32_t tmp = 0;
    status = m_dev->getFrequency(tmp,tx);
    if (status)
	return status;
    uint32_t freq = (uint32_t)hz;
    if (tmp == freq)
	return 0;
    int delta = tmp - freq;
    Debug(this,DebugNote,"Set %s frequency requested=%u read=%u delta=%d [%p]",
	brfDir(tx),freq,tmp,delta,this);
    return NotExact;
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
	return InvalidPort;
    unsigned int status = m_dev->enableRxVga(true,preMixer);
    if (status)
	return status;
    return m_dev->setRxVga(val,preMixer);
}

unsigned int BrfInterface::setTxGain(int val, unsigned port, bool preMixer)
{
    XDebug(this,DebugAll,"BrfInterface::setTxGain(%d,%u,VGA%c) [%p]",
	val,port,mixer(preMixer),this);
    if (!m_dev->validPort(port))
	return InvalidPort;
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

void BrfInterface::completeDevInfo(NamedList& p, bool full, bool retData)
{
    RadioInterface::completeDevInfo(p,full,retData);
    if (full && m_dev) {
	p.addParam("address",m_dev->address(),false);
	p.addParam("speed",String(m_dev->speedStr()).toLower());
	p.addParam("serial",m_dev->serial(),false);
    }
}

// Calibration. Automatic tx/rx gain setting
// Set pre and post mixer value
unsigned int BrfInterface::setGain(bool tx, int val, unsigned int port,
    int* newVal) const
{
    if (!m_dev->validPort(port))
	return InvalidPort;
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
    m_ifaceId(0),
    m_disciplineBusy(false),
    m_lastDiscipline(0)
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
	installRelay(Timer);
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
	if (Engine::exiting())
	    return false;
	// Override parameters from received params
	const String& what = msg[YSTRING("radio_driver")];
	if (what && what != YSTRING("bladerf"))
	    return false;
	return createIface(msg);
    }
    if (id == Control) {
	const String& comp = msg[YSTRING("component")];
	RefPointer<BrfInterface> ifc;
	if (comp == name() || findIface(ifc,comp))
	    return onCmdControl(ifc,msg);
	return false;
    }
    if (id == Timer && msg.msgTime().sec() > m_lastDiscipline + 4) {
	m_lastDiscipline = msg.msgTime().sec();
	// protect BrfVctcxoDiscipliner objects from multiple threads
	lock();
	if (!m_disciplineBusy) {
	    m_disciplineBusy = true;
	    ListIterator iter(m_ifaces);
	    for (GenObject* gen = iter.get(); gen != 0; gen = iter.get()) {
		RefPointer<BrfInterface> ifc = static_cast<BrfInterface*>(gen);
		if (!ifc)
		    continue;
		unlock();
		BrfLibUsbDevice* dev = ifc->device();
		// discipline VCTCXO according to BrfLibUsbDevice's local settings
		if (dev)
		    dev->trimVctcxo(msg.msgTime().usec());
		ifc = 0;
		lock();
	    }
	    m_disciplineBusy = false;
	}
	unlock();
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

bool BrfModule::createIface(NamedList& params)
{
//    Debugger d(debugLevel(),"BrfModule::createIface()");
    Lock lck(this);
    NamedList p(*s_cfg.createSection("general"));
    // Allow using a different interface profile
    // Override general parameters
    const String& profile = params[YSTRING("profile")];
    if (profile && profile != YSTRING("general")) {
	NamedList* sect = s_cfg.getSection(profile);
	if (sect)
	    p.copyParams(*sect);
    }
    // Override parameters from received params
    String prefix = params.getValue(YSTRING("radio_params_prefix"),"radio.");
    if (prefix)
	p.copySubParams(params,prefix,true,true);
    BrfInterface* ifc = new BrfInterface(name() + "/" + String(++m_ifaceId));
    lck.drop();
    String error;
    unsigned int status = ifc->init(p,error);
    if (!status) {
	ifc->completeDevInfo(params,true,true);
	Lock lck(this);
	m_ifaces.append(ifc)->setDelete(false);
	return true;
    }
    ifc->setError(params,status,error);
    ifc->notifyError(status,error,"create");
    TelEngine::destruct(ifc);
    return false;
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
	// FIXME:
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
	"\r\ncontrol ifc_name balance value="
	"\r\n  Set software IQ gain balance"
	"\r\ncontrol ifc_name gainexp bp= max="
	"\r\n  Set amp gain expansion breakpoint (dB) and +3 dB expansion (dB)"
	"\r\ncontrol ifc_name phaseexp bp= max="
	"\r\n  Set amp phase expansion breakpoint (dB) and +3 dB expansion (deg)"
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
	"\r\ncontrol ifc_name freqoffs [{value= [stop=YES|no]}|drift=]"
	"\r\n  Set (absolute value or a drift expressed in ppb to force a clock trim) or retrieve the frequency offset"
	"\r\ncontrol ifc_name show [info=status|statistics|timestamps|boardstatus|peripheral|freqcal] [peripheral=all|list(lms,gpio,vctcxo,si5338)] [addr=] [len=]"
	"\r\n  Verbose output various interface info"
	"\r\ncontrol ifc_name freqcalstart [system_accuracy=] [count=]"
	"\r\n  Start or re-configure the frequency calibration process"
	"\r\ncontrol ifc_name freqcalstop"
	"\r\n  Stop the frequency calibration process";

    const String& cmd = msg[YSTRING("operation")];
    // Module commands
    if (!ifc) {
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
    if (cmd == YSTRING("balance")) {
	if (!ifc->device())
	    return retMsgError(msg,"No device");
	const String txPB = msg[YSTRING("value")];
	const float val = txPB.toDouble(1);
	ifc->device()->setTxIQBalance(val);
	return true;
    }
    if (cmd == YSTRING("gainexp")) {
	if (!ifc->device())
	    return retMsgError(msg,"No device");
	const String bp = msg[YSTRING("bp")];
	const String max = msg[YSTRING("max")];
	ifc->device()->setGainExp(bp.toDouble(1),max.toDouble(1));
	return true;
    }
    if (cmd == YSTRING("phaseexp")) {
	if (!ifc->device())
	    return retMsgError(msg,"No device");
	const String bp = msg[YSTRING("bp")];
	const String max = msg[YSTRING("max")];
	ifc->device()->setPhaseExp(bp.toDouble(1),max.toDouble(1));
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
    if (cmd == YSTRING("freqoffs"))
	return onCmdFreqOffs(ifc,msg);
    if (cmd == YSTRING("freqcalstart"))
	return onCmdFreqCal(ifc,msg,true);
    if (cmd == YSTRING("freqcalstop")) {
	ifc->device()->disableDiscipline(true);
	msg.retValue() << "frequency calibration disabled";
	return true;
    }
    bool calStop = (cmd == YSTRING("cal_stop"));
    if (calStop || cmd == YSTRING("cal_abort")) {
	if (!ifc->device())
	    return retMsgError(msg,"No device");
	ifc->device()->m_calibrateStop = calStop ? 1 : -1;
	return true;
    }
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
	if (findIface(ifc,ifcName) && ifc->device())
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
	    extra << "|Address|Serial|Speed|Firmware|FPGA|LMS_Ver";
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

// control ifc_name show [info=status|statistics|timestamps|boardstatus|peripheral|freqcal] [peripheral=all|list(lms,gpio,vctcxo,si5338)] [addr=] [len=]
bool BrfModule::onCmdShow(BrfInterface* ifc, Message& msg, const String& what)
{
    if (!ifc->device())
	return retMsgError(msg,"No device");
    String info;
    if (what)
	info = what;
    else
	info = msg.getValue(YSTRING("info"),"status");
    if (info == YSTRING("freqcal"))
	return onCmdFreqCal(ifc,msg,false);
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

// control ifc_name freqoffs [{value|drift}=]
bool BrfModule::onCmdFreqOffs(BrfInterface* ifc, Message& msg)
{
    if (!ifc->device())
	return retMsgError(msg,"No device");
    const String* strRet = msg.getParam(YSTRING("value"));
    if (strRet) {
	float freqoffs = strRet->toDouble(-1);
	if (freqoffs > 0) {
	    bool stop = msg.getBoolValue(YSTRING("stop"),true);
	    unsigned code = ifc->device()->setFreqOffset(freqoffs,0,stop);
	    if (code)
		return retValFailure(msg,code);
	}
	else
	    return retParamError(msg,"value");
    }
    else if ((strRet = msg.getParam(YSTRING("drift"))) != 0) {
	int drift = strRet->toInteger();
	if (!drift)
	    return retParamError(msg,"drift");
	if (!waitDisciplineFree())
	    return false;
	ifc->device()->trimVctcxo(Time::now(),drift);
	m_disciplineBusy = false;
    }
    msg.retValue() << "freqoffs=" << ifc->device()->freqOffset();
    return true;
}

// control ifc_name freqcalstart [system_accuracy=] [count=] or control ifc_name show info=freqcal
bool BrfModule::onCmdFreqCal(BrfInterface* ifc, Message& msg, bool start)
{
    if (!ifc->device())
	return retMsgError(msg,"No device");
    if (!waitDisciplineFree())
	return false;
    bool ret = ifc->device()->onCmdFreqCal(msg,start);
    m_disciplineBusy = false;
    return ret;
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
	p.setTrack(tx,rx,params[p.lowCase + "_trackaddr"],
	    params.getIntValue(p.lowCase + "_level",-1));
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
// Device thread
BrfThread::BrfThread(BrfLibUsbDevice* dev, int type, const NamedList& params,
    const char* name, Thread::Priority prio)
    : Thread(name,prio),
    m_type(type),
    m_params(params),
    m_device(dev),
    m_paused(false),
    m_pauseToggle(false),
    m_priority(Thread::priority(prio))
{
    m_params.assign(name);
}

// Start this thread. Set destination pointer on success. Delete object on failure
BrfThread* BrfThread::start()
{
    if (startup())
	return this;
    Debug(ifc(),DebugNote,"Failed to start worker '%s' [%p]",name(),ifc());
    delete this;
    return 0;
}

// Pause / resume a thread
unsigned int BrfThread::pauseToggle(BrfThread*& th, Mutex* mtx, bool on, String* error)
{
    Lock lck(mtx);
    if (!th)
	return BrfLibUsbDevice::setErrorFail(error,"Worker abnormally terminated");
    if (th->m_paused == on)
	return 0;
    th->m_pauseToggle = true;
    lck.drop();
    for (unsigned int n = threadIdleIntervals(200); n; n--) {
	Thread::idle();
	Lock lck(mtx);
	if (!th)
	    return BrfLibUsbDevice::setErrorFail(error,"Worker abnormally terminated");
	if (!th->m_pauseToggle)
	    return 0;
	if (th->m_device) {
	    unsigned int status = th->m_device->cancelled(error);
	    if (status)
		return status;
	}
	else if (Thread::check(false))
	    return BrfLibUsbDevice::setError(RadioInterface::Cancelled,error,"Cancelled");
    }
    return BrfLibUsbDevice::setErrorTimeout(error,"Worker pause toggle timeout");
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
    if (!waitMs)
	return;
    unsigned int intervals = threadIdleIntervals(waitMs);
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
    if (!m_device)
	return;
    Debug(ifc(),DebugAll,"Worker (%p) '%s' started prio=%s [%p]",
	this,name(),m_priority,ifc());
    switch (m_type) {
	case DevCalibrate:
	    m_device->calibrate(true,m_params,0,true);
	    break;
	case DevSend:
	    m_device->runSend(this);
	    break;
	case DevRecv:
	    m_device->runRecv(this);
	    break;
	default:
	    ;
    }
    notify();
}

void BrfThread::notify()
{
    BrfLibUsbDevice* dev = m_device;
    m_device = 0;
    if (!dev)
	return;
    bool ok = ((String)m_params == Thread::currentName());
    Debug(dev->owner(),ok ? DebugAll : DebugWarn,"Worker (%p) '%s' terminated [%p]",
	this,name(),dev->owner());
    Lock lck(dev->m_threadMutex);
    if (dev->m_calThread == this)
	dev->m_calThread = 0;
    else if (dev->m_sendThread == this)
	dev->m_sendThread = 0;
    else if (dev->m_recvThread == this)
	dev->m_recvThread = 0;
}

// amplifier linearization functions

// mean complex gain between two vectors
static Complex meanComplexGain(const Complex* rx, const Complex* tx, unsigned length)
{
    // no data? unity gain
    if (!length)
	return Complex(1,0);
    Complex sum = 0;
    unsigned count = 0;
    for (unsigned i=0; i<length; i++) {
	if (i<8)
	    Debug(DebugAll,"meanComplexGain rx[%u]=%f%+f tx[%u]=%f%+f",
		i,rx[i].re(),rx[i].im(),i,tx[i].re(),tx[i].im());
	Complex gain = rx[i] / tx[i];
	sum += gain;
	count++;
    }
    return sum / length;
}

static unsigned findBreakAndSlope(const FloatVector& v, float startdB, float stepdB, float *bp, float *slope)
{
    if (v.length()==0) {
	Debug(DebugWarn,"findBreakAndSlope zero length vector");
	return -1;
    }
    unsigned imax = v.length()-1;
    // get the last two power values
    float lastdB = startdB + stepdB*imax;
    float pmax = pow(10,lastdB*0.1);
    float pmax_1 = pow(10,(lastdB-stepdB)*0.1);
    // slope at high end of the scale
    // defines a line through the two two samples
    *slope = (v[imax] - v[imax-1]) / (pmax - pmax_1);
    // breakpoint is the intersection of the two lines
    *bp = pmax - (v[imax] - v[0]) / (*slope);
    return 0;
}

unsigned BrfLibUsbDevice::findGainExpParams(const ComplexVector& sweep, float startdB, float stepdB)
{
    FloatVector gain(sweep.length());
    for (unsigned i=0; i<gain.length(); i++)
	gain[i] = sweep[i].norm2();
    if (findBreakAndSlope(gain, startdB, stepdB, &m_gainExpBreak, &m_gainExpSlope) < 0)
	return -1;
    Debug(m_owner,DebugInfo,"amp gain expansion: bp = %f linear slope = %f linear [%p]",
	m_gainExpBreak,m_gainExpSlope,this);
    return 0;
}

unsigned BrfLibUsbDevice::findPhaseExpParams(const ComplexVector& sweep, float startdB, float stepdB)
{
    FloatVector phase(sweep.length());
    for (unsigned i=0; i<phase.length(); i++)
	phase[i] = sweep[i].arg();
    if (findBreakAndSlope(phase, startdB, stepdB, &m_phaseExpBreak, &m_phaseExpSlope) < 0)
	return -1;
    Debug(m_owner,DebugInfo,"amp phase expansion: bp = %f linear slope = %f deg/lin [%p]",
	m_phaseExpBreak,180*m_phaseExpSlope/M_PI,this);
    return 0;
}

// sweep function
// sweeps power over a range and records gain and phase of the loopback signal
ComplexVector BrfLibUsbDevice::sweepPower(float startdB, float stopdB, float stepdB)
{
    Debug(m_owner,DebugInfo,"sweepPower start=%4.2f stop=%4.2f step=%4.2f",
	startdB, stopdB, stepdB);
    unsigned steps = 1 + (unsigned)((stopdB-startdB)/stepdB);
    ComplexVector sweep(steps);
    Complex rxBuf[2004];
    unsigned int status = 0;
    String e;
    for (unsigned step=0; step<steps; step++) {
	// set up the reference signal
	float dB = startdB + stepdB*step;
	float gain = pow(10,dB/10);
	setTxPattern("circle",gain);
	// receive the amp output
	Thread::msleep(10);
	BRF_FUNC_CALL_BREAK(setStateSyncTx(0,&e));
	uint64_t ts = m_syncTxState.m_tx.m_timestamp + m_radioCaps.rxLatency;
	BRF_FUNC_CALL_BREAK(capture(false,(float*)rxBuf,2004,ts,&e));
	// calculate and save the gain
	unsigned base = (4 - (ts % 4)) % 4;
	Complex sGain = meanComplexGain(rxBuf+2*base, m_txPatternBuffer.data(), 2000);
	Debug(m_owner,DebugAll,"sweepPower[%u] result=(%g,%g) when=" FMT64U " base=%u"
	    " power=%4.2f (%4.2f linear) gain=%4.2f dB @ %4.2f deg",
	    step,sGain.re(),sGain.im(),ts,base,dB,gain,
	    10*log10(sGain.norm2()),sGain.arg()*180/M_PI);
	sweep[step] = sGain;
    }
    if (status) {
	Debug(m_owner,DebugWarn,"sweep: %u %s",status,e.c_str());
	sweep.resetStorage(0);
    }
    return sweep;
}

// generic expansion function
// returns the expansion factor for this power level x
// x and breakpoint in linear power units
// slope in units of <whatever> / <linear power>
// expansion factor in units of <whatever>, determined by the units of the slope
inline float expansion(float x, float breakpoint, float slope)
{
    const float delta = x - breakpoint;
    if (delta<0) return 0;
    return delta*slope;
}

// recaluclate the amplifier linearization table
// should be called any time an amplifier linearization parameter changes
unsigned BrfLibUsbDevice::calculateAmpTable()
{
    float maxGain = 1 + expansion(2, m_gainExpBreak, m_gainExpSlope);
    float maxPhase = expansion(2, m_phaseExpBreak, m_phaseExpSlope);
    float midGain = 1 + expansion(1, m_gainExpBreak, m_gainExpSlope);
    float midPhase = expansion(1, m_phaseExpBreak, m_phaseExpSlope);
    Debug(m_owner,DebugInfo,
	"calculateAmpTable gBp=%4.2f gS=%4.2f g0=%4.2f gMax=%4.2f "
	"pBp=%4.2f pS=%+4.2f p0=%+4.2f deg pMax=%+4.2f deg",
	m_gainExpBreak, m_gainExpSlope, midGain, maxGain, m_phaseExpBreak,
	m_phaseExpSlope*180/M_PI, midPhase*180/M_PI, maxPhase*180/M_PI);
    for (unsigned i=0; i<2*2048; i++) {
	// normalized power level (0..2)
	float p = ((float)i) / 2048.0F;
	// base gain is 1 - this is where we compensate the 1 we subtracted from the max
	float gainExp = 1 + expansion(p, m_gainExpBreak, m_gainExpSlope);
	float phaseExp = expansion(p, m_phaseExpBreak, m_phaseExpSlope);
	Complex c(0,phaseExp);
	float adjGain = gainExp / maxGain;
	Complex adjust = c.exp() * adjGain;
	m_ampTable[2*i] = 2048*adjust.re();
	m_ampTable[2*i+1] = 2048*adjust.im();
    }
    m_ampTableUse = true;
    return 0;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet enc=utf-8: */
