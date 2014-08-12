/**
 * zlibcompress.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * ZLib support
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

#include <yatephone.h>
#include <string.h>

#include <zlib.h>

#ifndef Z_TEXT
#define Z_TEXT Z_ASCII
#endif

using namespace TelEngine;
namespace { // anonymous

class ZLibStream;                        // zlib stream wrapper along with output buffer
class ZLibComp;                          // ZLib (de)compressor
class ZLibModule;                        // The module


// (de)compressor output buffer minimum/default values
#define COMP_MIN_VAL 128
#define COMP_DEF_VAL 256
#define DECOMP_MIN_VAL 256
#define DECOMP_DEF_VAL 1024


/*
 * A zlib stream wrapper along with output buffer
 */
class ZLibStream : public DataBlock
{
public:
    // Constructor. The owner must always be valid
    // Reset the owner if failed to initialize
    ZLibStream(ZLibComp* owner, bool comp, const NamedList& params);
    // Destructor
    ~ZLibStream();
    // Check if valid
    inline bool valid() const
	{ return m_owner != 0; }
    // Finalize the (de)compression
    inline void finalize()
	{ m_finalize = true; }
    // Push data
    int write(const void* buf, unsigned int len, bool flush);
    // Read data
    int read(DataBlock& buf, bool flush);
private:
    // Check an error code. Show a debug message if not ok.
    // Return true if code is Z_OK
    bool checkError(int code, const char* text);
    // Retrieve the length of output buffer (negative on error)
    int outBufLen();

    ZLibComp* m_owner;                   // The owner
    bool m_comp;                         // (de)compressor flag
    z_stream_s m_zlib;                   // zlib library structure
    bool m_finalize;                     // Finalize flag
};

/*
 * A ZLib (de)compressor
 */
class ZLibComp : public Compressor
{
public:
    // Constructor
    ZLibComp(const char* name);
    // Destructor. Reset library data
    virtual ~ZLibComp();
    // Initialize
    virtual bool init(bool comp = true, bool decomp = true,
	const NamedList& params = NamedList::empty());
    // Finalize the (de)compression
    virtual void finalize(bool comp) {
	    if (comp) {
		if (m_comp)
		    m_comp->finalize();
	    }
	    else if (m_decomp)
		m_decomp->finalize();
	}
    // Push data to compressor
    virtual int writeComp(const void* buf, unsigned int len, bool flush)
	{ return m_comp ? m_comp->write(buf,len,flush) : -1; }
    // Read data from compressor
    virtual int readComp(DataBlock& buf, bool flush)
	{ return m_comp ? m_comp->read(buf,flush) : -1; }
    // Push data to decompressor
    virtual int writeDecomp(const void* buf, unsigned int len, bool flush)
	{ return m_decomp ? m_decomp->write(buf,len,flush) : -1; }
    // Read data from decompressor
    virtual int readDecomp(DataBlock& buf, bool flush)
	{ return m_decomp ? m_decomp->read(buf,flush) : -1; }
protected:
    ZLibStream* m_comp;
    ZLibStream* m_decomp;
};

/*
 * The module
 */
class ZLibModule : public Module
{
public:
    enum {
	ZLibHandler = Private
    };
    ZLibModule();
    ~ZLibModule();
    virtual void initialize();
protected:
    virtual bool received(Message& msg, int id);
};


INIT_PLUGIN(ZLibModule);
static unsigned int s_compOutBuflen = COMP_DEF_VAL;     // Compressor output buffer length
static unsigned int s_decompOutBuflen = DECOMP_DEF_VAL; // Decompressor output buffer length
static int s_level = Z_DEFAULT_COMPRESSION;             // Default compression level

// Compression level dictionary
static const TokenDict s_compressionLevel[] = {
    {"none",    Z_NO_COMPRESSION},
    {"speed",   Z_BEST_SPEED},
    {"size",    Z_BEST_COMPRESSION},
    {"default", Z_DEFAULT_COMPRESSION},
    {0,0}
};

// Retrieve (de)compressor output buffer length from parameter list
static unsigned int outBufLenParam(const NamedList& params, bool comp,
    unsigned int defVal)
{
    const char* p = comp ? "compressor_buflen" : "decompressor_buflen";
    int val = params.getIntValue(p,defVal);
    if (val == (int)defVal)
	return defVal;
    if (comp)
	return (unsigned int)(val >= COMP_MIN_VAL ? val : COMP_MIN_VAL);
    return (unsigned int)(val >= DECOMP_MIN_VAL ? val : DECOMP_MIN_VAL);
}


/*
 * ZLibStream
 */
// Constructor
ZLibStream::ZLibStream(ZLibComp* owner, bool comp, const NamedList& params)
    : m_owner(owner), m_comp(comp), m_finalize(false)
{
    if (!m_owner)
	return;
    // Set output buffer length
    unsigned int defVal = comp ? s_compOutBuflen : s_decompOutBuflen;
    unsigned int n = outBufLenParam(params,comp,defVal);
    assign(0,n);
    // Init zlib structure
    ::memset(&m_zlib,0,sizeof(z_stream_s));
    m_zlib.next_out = (Bytef*)data();
    m_zlib.avail_out = length();
    m_zlib.zalloc = Z_NULL;
    m_zlib.zfree = Z_NULL;
    m_zlib.opaque = Z_NULL;
    int code = Z_OK;
    if (comp) {
	m_zlib.data_type = Z_UNKNOWN;
	const String& data = params["data_type"];
	if (data == "text")
	    m_zlib.data_type = Z_TEXT;
	else if (data == "binary")
	    m_zlib.data_type = Z_BINARY;
	// Init compressor
	code = deflateInit2(&m_zlib,
	    params.getIntValue("compress_level",s_compressionLevel,s_level),
	    Z_DEFLATED,                  // single supported compression method
	    15,                          // default windowBits
	    8,                           // default memLevel
	    Z_DEFAULT_STRATEGY);         // default strategy
    }
    else {
	code = inflateInit2(&m_zlib,
	    15);           // default windowBits
    }
    if (!checkError(code,"failed to initialize"))
	m_owner = 0;
}

// Destructor
ZLibStream::~ZLibStream()
{
    // Release only if properly initialized
    if (!m_owner)
	return;
    Debug(&__plugin,DebugInfo,"ZLibComp(%s) %scompressed %lu --> %lu bytes [%p]",
	m_owner->toString().c_str(),m_comp ? "" : "de",
	m_zlib.total_in,m_zlib.total_out,m_owner);
    int code = 0;
    if (m_comp)
	code = deflateEnd(&m_zlib);
    else
	code = inflateEnd(&m_zlib);
#ifdef DEBUG
    checkError(code,"release failure");
#else
    YIGNORE(code);
#endif
}

// Push data
int ZLibStream::write(const void* buf, unsigned int len, bool flush)
{
    if (!(buf && len)) {
	if (!flush)
	    return 0;
	buf = 0;
	len = 0;
    }
    XDebug(&__plugin,DebugAll,"ZLibComp(%s,%u)::write(%p,%u,%u) avail out %u [%p]",
	m_owner->toString().c_str(),m_comp,buf,len,flush,m_zlib.avail_out,this);
    m_zlib.next_in = (Bytef*)buf;
    m_zlib.avail_in = len;
    int fl = m_finalize ? Z_FINISH : (flush ? Z_SYNC_FLUSH : Z_NO_FLUSH);
    int code = m_comp ? deflate(&m_zlib,fl) : inflate(&m_zlib,fl);
    int ret = -1;
    if (code == Z_OK || code == Z_BUF_ERROR) {
	ret = len;
	if (m_zlib.avail_in <= len)
	    ret -= m_zlib.avail_in;
	else
	    ret = 0;
    }
    else
	checkError(code,"write failed");
    return ret;
}

// Read data
int ZLibStream::read(DataBlock& buf, bool flush)
{
    XDebug(&__plugin,DebugAll,"ZLibComp(%s,%u)::read(%u) avail out %u [%p]",
	m_owner->toString().c_str(),m_comp,flush,m_zlib.avail_out,this);
    int ret = -1;
    bool firstPass = true;
    while (true) {
	int bufLen = outBufLen();
	if (bufLen < 0)
	    break;
	if (ret < 0)
	    ret = 0;
	if (!bufLen) {
	    if (!(flush && firstPass))
		break;
	    // First pass with no output data
	    // Try to flush some input and check if we have data
	    firstPass = false;
	    if (write(0,0,true) < 0)
		break;
	    bufLen = outBufLen();
	    if (bufLen <= 0)
		break;
	}
	buf.append(data(),bufLen);
	ret += bufLen;
	m_zlib.next_out = (Bytef*)data();
	m_zlib.avail_out = length();
	// Try to flush some input
	if (!flush || write(0,0,true) < 0)
	    break;
    }
    return ret;
}

// Check an error code. Show a debug message if not ok.
// Return true if code is Z_OK
bool ZLibStream::checkError(int code, const char* text)
{
    if (code == Z_OK)
	return true;
    // Try to obtain an error from library stream structure or
    //  the text associated with the error code
    const char* error = m_zlib.msg;
    if (TelEngine::null(error))
	error = zError(code);
    if (TelEngine::null(error)) {
	switch (code) {
#define MAKE_ERROR(value) case value: error = #value; break
	    MAKE_ERROR(Z_STREAM_END);
	    MAKE_ERROR(Z_NEED_DICT);
	    MAKE_ERROR(Z_ERRNO);
	    MAKE_ERROR(Z_STREAM_ERROR);
	    MAKE_ERROR(Z_DATA_ERROR);
	    MAKE_ERROR(Z_MEM_ERROR);
	    MAKE_ERROR(Z_BUF_ERROR);
	    MAKE_ERROR(Z_VERSION_ERROR);
#undef MAKE_ERROR
	    default:
		error = "Unknown error";
	}
    }
    Debug(&__plugin,DebugNote,"ZLibComp(%s,%u) %s %d: '%s' [%p]",
	m_owner->toString().c_str(),m_comp,text,code,error,m_owner);
    return false;
}

// Retrieve the length of output buffer (negative on error)
int ZLibStream::outBufLen()
{
    static bool firstError = true;
    int bufLen = m_zlib.next_out - (Bytef*)data();
    if (bufLen >= 0 && (unsigned int)bufLen <= length())
	return bufLen;
    // The library set the output buffer out of bounds
    if (firstError) {
	firstError = false;
	Debug(&__plugin,DebugFail,"ZLibComp(%s,%u) output buffer out of bounds [%p]",
	    m_owner->toString().c_str(),m_comp,m_owner);
    }
    return -1;
}


/*
 * ZLibComp
 */
// Constructor. Initialize zlib structure to default values
ZLibComp::ZLibComp(const char* name)
    : Compressor("zlib",name),
    m_comp(0),
    m_decomp(0)
{
    XDebug(&__plugin,DebugAll,"ZLibComp(%s) [%p]",c_str(),this);
}

// Destructor. Reset library data
ZLibComp::~ZLibComp()
{
    XDebug(&__plugin,DebugAll,"~ZLibComp(%s) [%p]",c_str(),this);
    TelEngine::destruct(m_comp);
    TelEngine::destruct(m_decomp);
}

// Initialize
bool ZLibComp::init(bool comp, bool decomp, const NamedList& params)
{
    if (!(comp || decomp))
	return false;
    bool ok = true;
    if (comp && !m_comp) {
	m_comp = new ZLibStream(this,true,params);
	if (!m_comp->valid()) {
	    TelEngine::destruct(m_comp);
	    ok = false;
	}
    }
    if (ok && decomp && !m_decomp) {
	m_decomp = new ZLibStream(this,false,params);
	if (!m_decomp->valid()) {
	    TelEngine::destruct(m_decomp);
	    ok = false;
	}
    }
    return ok;
}


/*
 * ZLibModule
 */
ZLibModule::ZLibModule()
    : Module("zlibcompress","misc",true)
{
    Output("Loaded module ZLib - using zlib library version %s",zlibVersion());
}

ZLibModule::~ZLibModule()
{
    Output("Unloading module ZLib");
}

void ZLibModule::initialize()
{
    static bool first = true;

    Output("Initializing module ZLib");
    Configuration cfg(Engine::configFile("zlibcompress"));
    NamedList dummy("");
    NamedList* gen = cfg.getSection("general");
    if (!gen)
	gen = &dummy;
    if (first) {
	first = false;
	setup();
	// Check version (inflateInit() and deflateInit() will fail if version check fails)
	const char* libVer = zlibVersion();
	if (libVer && *libVer == *ZLIB_VERSION)
	    installRelay(ZLibHandler,"engine.compress");
	else
	    Debug(this,DebugWarn,"Library version '%s' not compatible with built version '%s'",
		libVer,ZLIB_VERSION);
    }

    s_compOutBuflen = outBufLenParam(*gen,true,COMP_DEF_VAL);
    s_decompOutBuflen = outBufLenParam(*gen,false,DECOMP_DEF_VAL);
    s_level = gen->getIntValue("compress_level",s_compressionLevel,Z_DEFAULT_COMPRESSION);

    if (debugAt(DebugAll)) {
	String s;
	s << " compressor_buflen=" << s_compOutBuflen;
	s << " decompressor_buflen=" << s_decompOutBuflen;
	s << " compress_level=" << ::lookup(s_level,s_compressionLevel);
	Debug(this,DebugAll,"Initialized%s",s.c_str());
    }
}

bool ZLibModule::received(Message& msg, int id)
{
    if (id == ZLibHandler) {
	bool ok = true;
	const String& format = msg["format"];
	if (format != "zlib") {
	    const String& formats = msg["formats"];
	    ObjList* list = formats.split(',',false);
	    ok = 0 != list->find("zlib");
	    TelEngine::destruct(list);
	}
	if (!ok)
	    return false;
	if (msg.getBoolValue("test"))
	    return true;
	Compressor** pp = static_cast<Compressor**>(msg.userObject(YATOM("Compressor*")));
	if (!pp) {
	    Debug(this,DebugGoOn,"No pointer in %s message",msg.c_str());
	    return false;
	}
	bool comp = msg.getBoolValue("comp",true);
	bool decomp = msg.getBoolValue("decomp",true);
	Compressor* rc = new ZLibComp(msg["name"]);
	if (comp || decomp)
	    ok = rc->init(comp,decomp,msg);
	if (ok) {
	    TelEngine::destruct(*pp);
	    *pp = rc;
	}
	else
	    TelEngine::destruct(rc);
	return ok;
    }
    return Module::received(msg,id);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
