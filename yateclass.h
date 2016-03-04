/**
 * yateclass.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Base classes and types, not related to the engine or telephony
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

#ifndef __YATECLASS_H
#define __YATECLASS_H

#ifndef __cplusplus
#error C++ is required
#endif

#include <limits.h>
#include <sys/types.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>

#ifndef _WORDSIZE
#if defined(__arch64__) || defined(__x86_64__) \
    || defined(__amd64__) || defined(__ia64__) \
    || defined(__alpha__) || defined(__sparcv9) || defined(__mips64)
#define _WORDSIZE 64
#else
#define _WORDSIZE 32
#endif
#endif

#ifndef _WINDOWS
#if defined(WIN32) || defined(_WIN32)
#define _WINDOWS
#endif
#endif

#ifdef _WINDOWS

#include <windows.h>
#include <io.h>
#include <direct.h>

/**
 * Windows definitions for commonly used types
 */
typedef signed __int8 int8_t;
typedef unsigned __int8 u_int8_t;
typedef unsigned __int8 uint8_t;
typedef signed __int16 int16_t;
typedef unsigned __int16 u_int16_t;
typedef unsigned __int16 uint16_t;
typedef signed __int32 int32_t;
typedef unsigned __int32 u_int32_t;
typedef unsigned __int32 uint32_t;
typedef signed __int64 int64_t;
typedef unsigned __int64 u_int64_t;
typedef unsigned __int64 uint64_t;

typedef int pid_t;
typedef int socklen_t;
typedef unsigned long in_addr_t;

#ifndef strcasecmp
#define strcasecmp _stricmp
#endif

#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif

#define vsnprintf _vsnprintf
#define snprintf _snprintf
#define strdup _strdup
#define strtoll _strtoi64
#define open _open
#define dup2 _dup2
#define read _read
#define write _write
#define close _close
#define getpid _getpid
#define chdir _chdir
#define mkdir(p,m) _mkdir(p)
#define unlink _unlink

#define O_RDWR   _O_RDWR
#define O_RDONLY _O_RDONLY
#define O_WRONLY _O_WRONLY
#define O_APPEND _O_APPEND
#define O_BINARY _O_BINARY
#define O_EXCL   _O_EXCL
#define O_CREAT  _O_CREAT
#define O_TRUNC  _O_TRUNC
#define O_NOCTTY 0

#define S_IRUSR _S_IREAD
#define S_IWUSR _S_IWRITE
#define S_IXUSR 0
#define S_IRWXU (_S_IREAD|_S_IWRITE)

#ifdef LIBYATE_EXPORTS
#define YATE_API __declspec(dllexport)
#else
#ifndef LIBYATE_STATIC
#define YATE_API __declspec(dllimport)
#endif
#endif

#define FMT64 "%I64d"
#define FMT64U "%I64u"

#else /* _WINDOWS */

#include <sys/time.h>
#include <sys/socket.h>

#if defined(__FreeBSD__)
#include <netinet/in_systm.h>
#endif

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

/**
 * Non-Windows definitions for commonly used types
 */
#ifndef SOCKET
typedef int SOCKET;
#endif
#ifndef HANDLE
typedef int HANDLE;
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

#if _WORDSIZE == 64 && !defined(__APPLE__)
#define FMT64 "%ld"
#define FMT64U "%lu"
#else
#define FMT64 "%lld"
#define FMT64U "%llu"
#endif

#endif /* ! _WINDOWS */

#ifndef LLONG_MAX
#ifdef _I64_MAX
#define LLONG_MAX _I64_MAX
#else
#define LLONG_MAX 9223372036854775807LL
#endif
#endif

#ifndef LLONG_MIN
#ifdef _I64_MIN
#define LLONG_MIN _I64_MIN
#else
#define LLONG_MIN (-LLONG_MAX - 1LL)
#endif
#endif

#ifndef ULLONG_MAX
#ifdef _UI64_MAX
#define ULLONG_MAX _UI64_MAX
#else
#define ULLONG_MAX 18446744073709551615ULL
#endif
#endif

#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif

#ifndef IPTOS_LOWDELAY
#define IPTOS_LOWDELAY      0x10
#define IPTOS_THROUGHPUT    0x08
#define IPTOS_RELIABILITY   0x04
#endif
#ifndef IPTOS_MINCOST
#define IPTOS_MINCOST       0x02
#endif
#ifndef IPPROTO_SCTP
#define IPPROTO_SCTP        132
#endif

#ifndef YATE_API
#define YATE_API
#endif

#ifdef _WINDOWS
#undef RAND_MAX
#define RAND_MAX 2147483647
#endif

/**
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

#ifdef HAVE_GCC_FORMAT_CHECK
#define FORMAT_CHECK(f) __attribute__((format(printf,(f),(f)+1)))
#else
#define FORMAT_CHECK(f)
#endif

#define YIGNORE(v) while (v) { break; }

#ifdef HAVE_BLOCK_RETURN
#define YSTRING(s) (*({static const String str(s);&str;}))
#define YATOM(s) (*({static const String* str(0);str ? str : String::atom(str,s);}))
#else
#define YSTRING(s) (s)
#define YATOM(s) (s)
#endif

#define YSTRING_INIT_HASH ((unsigned) -1)

/**
 * Abort execution (and coredump if allowed) if the abort flag is set.
 * This function may not return.
 */
YATE_API void abortOnBug();

/**
 * Set the abort on bug flag. The default flag state is false.
 * @return The old state of the flag.
 */
YATE_API bool abortOnBug(bool doAbort);

/**
 * Standard debugging levels.
 * The DebugFail level is special - it is always displayed and may abort
 *  the program if @ref abortOnBug() is set.
 */
enum DebugLevel {
    DebugFail = 0,
    DebugTest = 1,
    DebugGoOn = 2,
    DebugConf = 3,
    DebugStub = 4,
    DebugWarn = 5,
    DebugMild = 6,
    DebugCall = 7,
    DebugNote = 8,
    DebugInfo = 9,
    DebugAll = 10
};

/**
 * Retrieve the current global debug level
 * @return The current global debug level
 */
YATE_API int debugLevel();

/**
 * Set the current global debug level.
 * @param level The desired debug level
 * @return The new global debug level (may be different)
 */
YATE_API int debugLevel(int level);

/**
 * Check if debugging output should be generated
 * @param level The global debug level we are testing
 * @return True if messages should be output, false otherwise
 */
YATE_API bool debugAt(int level);

/**
 * Get an ANSI string to colorize debugging output
 * @param level The debug level who's color is requested.
 *  Negative or out of range will reset to the default color
 * @return ANSI string that sets color corresponding to level
 */
YATE_API const char* debugColor(int level);

/**
 * Get the name of a debugging or alarm level
 * @param level The debug level
 * @return Short C string describing the level
 */
YATE_API const char* debugLevelName(int level);

/**
 * Holds a local debugging level that can be modified separately from the
 *  global debugging
 * @short A holder for a debug level
 */
class YATE_API DebugEnabler
{
public:
    /**
     * Constructor
     * @param level The initial local debug level
     * @param enabled Enable debugging on this object
     */
    inline DebugEnabler(int level = TelEngine::debugLevel(), bool enabled = true)
	: m_level(DebugFail), m_enabled(enabled), m_chain(0), m_name(0)
	{ debugLevel(level); }

    inline ~DebugEnabler()
	{ m_name = 0; m_chain = 0; }

    /**
     * Retrieve the current local debug level
     * @return The current local debug level
     */
    inline int debugLevel() const
	{ return m_chain ? m_chain->debugLevel() : m_level; }

    /**
     * Set the current local debug level.
     * @param level The desired debug level
     * @return The new debug level (may be different)
     */
    int debugLevel(int level);

    /**
     * Retrieve the current debug activation status
     * @return True if local debugging is enabled
     */
    inline bool debugEnabled() const
	{ return m_chain ? m_chain->debugEnabled() : m_enabled; }

    /**
     * Set the current debug activation status
     * @param enable The new debug activation status, true to enable
     */
    inline void debugEnabled(bool enable)
	{ m_enabled = enable; m_chain = 0; }

    /**
     * Get the current debug name
     * @return Name of the debug activation if set or NULL
     */
    inline const char* debugName() const
	{ return m_name; }

    /**
     * Check if debugging output should be generated
     * @param level The debug level we are testing
     * @return True if messages should be output, false otherwise
     */
    bool debugAt(int level) const;

    /**
     * Check if this enabler is chained to another one
     * @return True if local debugging is chained to other enabler
     */
    inline bool debugChained() const
	{ return m_chain != 0; }

    /**
     * Chain this debug holder to a parent or detach from existing one
     * @param chain Pointer to parent debug level, NULL to detach
     */
    inline void debugChain(const DebugEnabler* chain = 0)
	{ m_chain = (chain != this) ? chain : 0; }

    /**
     * Copy debug settings from another object or from engine globals
     * @param original Pointer to a DebugEnabler to copy settings from
     */
    void debugCopy(const DebugEnabler* original = 0);

protected:
    /**
     * Set the current debug name
     * @param name Static debug name or NULL
     */
    inline void debugName(const char* name)
	{ m_name = name; }

private:
    int m_level;
    bool m_enabled;
    const DebugEnabler* m_chain;
    const char* m_name;
};

#if 0 /* for documentation generator */
/**
 * Convenience macro.
 * Does the same as @ref Debug if DEBUG is \#defined (compiling for debugging)
 *  else it does not get compiled at all.
 */
void DDebug(int level, const char* format, ...);

/**
 * Convenience macro.
 * Does the same as @ref Debug if DEBUG is \#defined (compiling for debugging)
 *  else it does not get compiled at all.
 */
void DDebug(const char* facility, int level, const char* format, ...);

/**
 * Convenience macro.
 * Does the same as @ref Debug if DEBUG is \#defined (compiling for debugging)
 *  else it does not get compiled at all.
 */
void DDebug(const DebugEnabler* local, int level, const char* format, ...);

/**
 * Convenience macro.
 * Does the same as @ref Debug if XDEBUG is \#defined (compiling for extra
 * debugging) else it does not get compiled at all.
 */
void XDebug(int level, const char* format, ...);

/**
 * Convenience macro.
 * Does the same as @ref Debug if XDEBUG is \#defined (compiling for extra
 * debugging) else it does not get compiled at all.
 */
void XDebug(const char* facility, int level, const char* format, ...);

/**
 * Convenience macro.
 * Does the same as @ref Debug if XDEBUG is \#defined (compiling for extra
 * debugging) else it does not get compiled at all.
 */
void XDebug(const DebugEnabler* local, int level, const char* format, ...);

/**
 * Convenience macro.
 * Does the same as @ref Debug if NDEBUG is not \#defined
 *  else it does not get compiled at all (compiling for mature release).
 */
void NDebug(int level, const char* format, ...);

/**
 * Convenience macro.
 * Does the same as @ref Debug if NDEBUG is not \#defined
 *  else it does not get compiled at all (compiling for mature release).
 */
void NDebug(const char* facility, int level, const char* format, ...);

/**
 * Convenience macro.
 * Does the same as @ref Debug if NDEBUG is not \#defined
 *  else it does not get compiled at all (compiling for mature release).
 */
void NDebug(const DebugEnabler* local, int level, const char* format, ...);
#endif

#if defined(_DEBUG) || defined(DEBUG) || defined(XDEBUG)
#undef DEBUG
#define DEBUG	1
#endif

#ifdef DEBUG
#define DDebug Debug
#else
#ifdef _WINDOWS
#define DDebug do { break; } while
#else
#define DDebug(arg...)
#endif
#endif

#ifdef XDEBUG
#define XDebug Debug
#else
#ifdef _WINDOWS
#define XDebug do { break; } while
#else
#define XDebug(arg...)
#endif
#endif

#ifndef NDEBUG
#define NDebug Debug
#else
#ifdef _WINDOWS
#define NDebug do { break; } while
#else
#define NDebug(arg...)
#endif
#endif

/**
 * Outputs a debug string.
 * @param level The level of the message
 * @param format A printf() style format string
 */
YATE_API void Debug(int level, const char* format, ...) FORMAT_CHECK(2);

/**
 * Outputs a debug string for a specific facility.
 * @param facility Facility that outputs the message
 * @param level The level of the message
 * @param format A printf() style format string
 */
YATE_API void Debug(const char* facility, int level, const char* format, ...) FORMAT_CHECK(3);

/**
 * Outputs a debug string for a specific facility.
 * @param local Pointer to a DebugEnabler holding current debugging settings
 * @param level The level of the message
 * @param format A printf() style format string
 */
YATE_API void Debug(const DebugEnabler* local, int level, const char* format, ...) FORMAT_CHECK(3);

/**
 * Outputs a debug string and emits an alarm if a callback is installed
 * @param component Component that emits the alarm
 * @param level The level of the alarm
 * @param format A printf() style format string
 */
YATE_API void Alarm(const char* component, int level, const char* format, ...) FORMAT_CHECK(3);

/**
 * Outputs a debug string and emits an alarm if a callback is installed
 * @param component Pointer to a DebugEnabler holding component name and debugging settings
 * @param level The level of the alarm
 * @param format A printf() style format string
 */
YATE_API void Alarm(const DebugEnabler* component, int level, const char* format, ...) FORMAT_CHECK(3);

/**
 * Outputs a debug string and emits an alarm if a callback is installed
 * @param component Component that emits the alarm
 * @param info Extra alarm information
 * @param level The level of the alarm
 * @param format A printf() style format string
 */
YATE_API void Alarm(const char* component, const char* info, int level, const char* format, ...) FORMAT_CHECK(4);

/**
 * Outputs a debug string and emits an alarm if a callback is installed
 * @param component Pointer to a DebugEnabler holding component name and debugging settings
 * @param info Extra alarm information
 * @param level The level of the alarm
 * @param format A printf() style format string
 */
YATE_API void Alarm(const DebugEnabler* component, const char* info, int level, const char* format, ...) FORMAT_CHECK(4);

/**
 * Outputs a string to the debug console with formatting
 * @param format A printf() style format string
 */
YATE_API void Output(const char* format, ...) FORMAT_CHECK(1);

/**
 * This class is used as an automatic variable that logs messages on creation
 *  and destruction (when the instruction block is left or function returns).
 * IMPORTANT: the name is not copied so it should best be static.
 * @short An object that logs messages on creation and destruction
 */
class YATE_API Debugger
{
public:
    /**
     * Timestamp formatting
     */
    enum Formatting {
	None = 0,
	Relative,  // from program start
	Absolute,  // from EPOCH (1-1-1970)
	Textual,   // absolute GMT in YYYYMMDDhhmmss.uuuuuu format
	TextLocal, // local time in YYYYMMDDhhmmss.uuuuuu format
	TextSep,   // absolute GMT in YYYY-MM-DD_hh:mm:ss.uuuuuu format
	TextLSep,  // local time in YYYY-MM-DD_hh:mm:ss.uuuuuu format
    };

    /**
     * The constructor prints the method entry message and indents.
     * @param name Name of the function or block entered, must be static
     * @param format printf() style format string
     */
    explicit Debugger(const char* name, const char* format = 0, ...);

    /**
     * The constructor prints the method entry message and indents.
     * @param level The level of the message
     * @param name Name of the function or block entered, must be static
     * @param format printf() style format string
     */
    Debugger(int level, const char* name, const char* format = 0, ...);

    /**
     * The destructor prints the method leave message and deindents.
     */
    ~Debugger();

    /**
     * Set the output callback
     * @param outFunc Pointer to the output function, NULL to use stderr
     */
    static void setOutput(void (*outFunc)(const char*,int) = 0);

    /**
     * Set the interactive output callback
     * @param outFunc Pointer to the output function, NULL to disable
     */
    static void setIntOut(void (*outFunc)(const char*,int) = 0);

    /**
     * Set the alarm hook callback
     * @param alarmFunc Pointer to the alarm callback function, NULL to disable
     */
    static void setAlarmHook(void (*alarmFunc)(const char*,int,const char*,const char*) = 0);

    /**
     * Set the relay hook callback that will process all Output, Debug and Alarm
     * @param relayFunc Pointer to the relay callback function, NULL to disable
     */
    static void setRelayHook(void (*relayFunc)(int,const char*,const char*,const char*) = 0);

    /**
     * Enable or disable the debug output
     * @param enable Set to true to globally enable output
     * @param colorize Enable ANSI colorization of output
     */
    static void enableOutput(bool enable = true, bool colorize = false);

    /**
     * Retrieve the start timestamp
     * @return Start timestamp value in seconds
     */
    static uint32_t getStartTimeSec();

    /**
     * Retrieve the format of timestamps
     * @return The current formatting type for timestamps
     */
    static Formatting getFormatting();

    /**
     * Set the format of timestamps on output messages and set the time start reference
     * @param format Desired timestamp formatting
     * @param startTimeSec Optional start timestamp (in seconds)
     */
    static void setFormatting(Formatting format, uint32_t startTimeSec = 0);

    /**
     * Fill a buffer with a current timestamp prefix
     * @param buf Buffer to fill, must be at least 24 characters long
     * @param format Desired timestamp formatting
     * @return Length of the prefix written in buffer excluding final NUL
     */
    static unsigned int formatTime(char* buf, Formatting format = getFormatting());

    /**
     * Processes a preformatted string as Output, Debug or Alarm.
     * This method is intended to relay messages from other processes, DO NOT USE!
     * @param level The level of the debug or alarm, negative for an output
     * @param buffer Preformatted text buffer, MUST HAVE SPACE for at least strlen + 2
     * @param component Component that emits the alarm if applicable
     * @param info Extra alarm information if applicable
     */
    static void relayOutput(int level, char* buffer, const char* component = 0, const char* info = 0);

private:
    const char* m_name;
    int m_level;
};

/**
 * A structure to build (mainly static) Token-to-ID translation tables.
 * A table of such structures must end with an entry with a null token
 */
struct TokenDict {
    /**
     * Token to match
     */
    const char* token;

    /**
     * Value the token translates to
     */
    int value;
};

class String;
class Mutex;
class ObjList;
class NamedCounter;

#if 0 /* for documentation generator */
/**
 * Macro to ignore the result of a function
 * @param value Returned value to be ignored, must be interpretable as boolean
 */
void YIGNORE(primitive value);

/**
 * Macro to create a local static String if supported by compiler, use with caution
 * @param string Literal constant string
 * @return A const String& if supported, literal string if not supported
 */
constant YSTRING(const char* string);

/**
 * Macro to create a shared static String if supported by compiler, use with caution
 * @param string Literal constant string
 * @return A const String& if supported, literal string if not supported
 */
constant YATOM(const char* string);

/**
 * Macro to create a GenObject class from a base class and implement @ref GenObject::getObject
 * @param type Class that is declared
 * @param base Base class that is inherited
 */
void YCLASS(class type,class base);

/**
 * Macro to create a GenObject class from two base classes and implement @ref GenObject::getObject
 * @param type Class that is declared
 * @param base1 First base class that is inherited
 * @param base2 Second base class that is inherited
 */
void YCLASS2(class type,class base1,class base2);

/**
 * Macro to create a GenObject class from three base classes and implement @ref GenObject::getObject
 * @param type Class that is declared
 * @param base1 First base class that is inherited
 * @param base2 Second base class that is inherited
 * @param base3 Third base class that is inherited
 */
void YCLASS3(class type,class base1,class base2,class base3);

/**
 * Macro to implement @ref GenObject::getObject in a derived class
 * @param type Class that is declared
 * @param base Base class that is inherited
 */
void YCLASSIMP(class type,class base);

/**
 * Macro to implement @ref GenObject::getObject in a derived class
 * @param type Class that is declared
 * @param base1 First base class that is inherited
 * @param base2 Second base class that is inherited
 */
void YCLASSIMP2(class type,class base1,class base2);

/**
 * Macro to implement @ref GenObject::getObject in a derived class
 * @param type Class that is declared
 * @param base1 First base class that is inherited
 * @param base2 Second base class that is inherited
 * @param base3 Third base class that is inherited
 */
void YCLASSIMP3(class type,class base1,class base2,class base3);

/**
 * Macro to retrieve a typed pointer to an interface from an object
 * @param type Class we want to return
 * @param pntr Pointer to the object we want to get the interface from
 * @return Pointer to the class we want or NULL
 */
class* YOBJECT(class type,GenObject* pntr);

/**
 * Macro to disable automatic copy and assignment operators
 * @param type Class that is declared
 */
void YNOCOPY(class type);
#endif

#define YCLASS(type,base) \
public: virtual void* getObject(const String& name) const \
{ return (name == YATOM(#type)) ? const_cast<type*>(this) : base::getObject(name); }

#define YCLASS2(type,base1,base2) \
public: virtual void* getObject(const String& name) const \
{ if (name == YATOM(#type)) return const_cast<type*>(this); \
  void* tmp = base1::getObject(name); \
  return tmp ? tmp : base2::getObject(name); }

#define YCLASS3(type,base1,base2,base3) \
public: virtual void* getObject(const String& name) const \
{ if (name == YATOM(#type)) return const_cast<type*>(this); \
  void* tmp = base1::getObject(name); \
  if (tmp) return tmp; \
  tmp = base2::getObject(name); \
  return tmp ? tmp : base3::getObject(name); }

#define YCLASSIMP(type,base) \
void* type::getObject(const String& name) const \
{ return (name == YATOM(#type)) ? const_cast<type*>(this) : base::getObject(name); }

#define YCLASSIMP2(type,base1,base2) \
void* type::getObject(const String& name) const \
{ if (name == YATOM(#type)) return const_cast<type*>(this); \
  void* tmp = base1::getObject(name); \
  return tmp ? tmp : base2::getObject(name); }

#define YCLASSIMP3(type,base1,base2,base3) \
void* type::getObject(const String& name) const \
{ if (name == YATOM(#type)) return const_cast<type*>(this); \
  void* tmp = base1::getObject(name); \
  if (tmp) return tmp; \
  tmp = base2::getObject(name); \
  return tmp ? tmp : base3::getObject(name); }

#define YOBJECT(type,pntr) (static_cast<type*>(GenObject::getObject(YATOM(#type),pntr)))

#define YNOCOPY(type) private: \
type(const type&); \
void operator=(const type&)


/**
 * An object with just a public virtual destructor
 */
class YATE_API GenObject
{
    YNOCOPY(GenObject); // no automatic copies please
public:
    /**
     * Default constructor
     */
    GenObject();

    /**
     * Destructor.
     */
    virtual ~GenObject() { setObjCounter(0); }

    /**
     * Check if the object is still valid and safe to access.
     * Note that you should not trust this result unless the object is locked
     *  by other means.
     * @return True if the object is still useable
     */
    virtual bool alive() const;

    /**
     * Destroys the object, disposes the memory.
     */
    virtual void destruct();

    /**
     * Get a string representation of this object
     * @return A reference to a String representing this object
     *  which is either null, the object itself (for objects derived from
     *  String) or some form of identification
     */
    virtual const String& toString() const;

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Helper method to get the pointer to a derived class
     * @param name Name of the class we are asking for
     * @param obj Pointer to the object to get derived class from
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    static inline void* getObject(const String& name, const GenObject* obj)
	{ return obj ? obj->getObject(name) : 0; }

    /**
     * Get the global state of object counting
     * @return True if object counting is enabled
     */
    static inline bool getObjCounting()
	{ return s_counting; }

    /**
     * Set the global state of object counting
     * @param enable True to enable object counting, false to disable
     */
    static inline void setObjCounting(bool enable)
	{ s_counting = enable; }

    /**
     * Get the counter of this object
     * @return Pointer to current counter object
     */
    inline NamedCounter* getObjCounter() const
	{ return m_counter; }

    /**
     * Set the counter of this object
     * @param counter New counter object or NULL
     * @return Pointer to old counter object
     */
    NamedCounter* setObjCounter(NamedCounter* counter);

    /**
     * Retrieve or allocate an object counter
     * @param name Name of the counter
     * @param create True to create a new counter if needed
     * @return Pointer to existing or new counter object
     */
    static NamedCounter* getObjCounter(const String& name, bool create = true);

    /**
     * Access the object counters list
     * @return Reference to the global object counters list
     */
    static ObjList& getObjCounters();

private:
    NamedCounter* m_counter;
    static bool s_counting;
};

/**
 * Helper function that destroys a GenObject only if the pointer is non-NULL.
 * Use it instead of the delete operator.
 * @param obj Pointer (rvalue) to the object to destroy
 */
inline void destruct(GenObject* obj)
    { if (obj) obj->destruct(); }

/**
 * Helper template function that destroys a GenObject descendant if the pointer
 *  is non-NULL and also zeros out the pointer.
 * Use it instead of the delete operator.
 * @param obj Reference to pointer (lvalue) to the object to destroy
 */
template <class Obj> void destruct(Obj*& obj)
    { if (obj) { obj->destruct(); obj = 0; } }

/**
 * A reference counted object.
 * Whenever using multiple inheritance you should inherit this class virtually.
 */
class YATE_API RefObject : public GenObject
{
    YNOCOPY(RefObject); // no automatic copies please
public:
    /**
     * The constructor initializes the reference counter to 1!
     * Use deref() to destruct the object when safe
     */
    RefObject();

    /**
     * Destructor.
     */
    virtual ~RefObject();

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Check if the object is still referenced and safe to access.
     * Note that you should not trust this result unless the object is locked
     *  by other means.
     * @return True if the object is referenced and safe to access
     */
    virtual bool alive() const;

    /**
     * Increments the reference counter if not already zero
     * @return True if the object was successfully referenced and is safe to access
     */
    bool ref();

    /**
     * Decrements the reference counter, destroys the object if it reaches zero
     * <pre>
     * // Deref this object, return quickly if the object was deleted
     * if (deref()) return;
     * </pre>
     * @return True if the object may have been deleted, false if it still exists and is safe to access
     */
    bool deref();

    /**
     * Get the current value of the reference counter
     * @return The value of the reference counter
     */
    inline int refcount() const
	{ return m_refcount; }

    /**
     * Refcounted objects should just have the counter decremented.
     * That will destroy them only when the refcount reaches zero.
     */
    virtual void destruct();

    /**
     * Check if reference counter manipulations are efficient on this platform.
     * If platform does not support atomic operations a mutex pool is used.
     * @return True if refcount uses atomic integer operations
     */
    static bool efficientIncDec();

protected:
    /**
     * This method is called when the reference count reaches zero after
     *  unlocking the mutex if the call to zeroRefsTest() returned true.
     * The default behaviour is to delete the object.
     */
    virtual void zeroRefs();

    /**
     * Bring the object back alive by setting the reference counter to one.
     * Note that it works only if the counter was zero previously
     * @return True if the object was resurrected - its name may be Lazarus ;-)
     */
    bool resurrect();

    /**
     * Pre-destruction notification, called just before the object is deleted.
     * Unlike in the destructor it is safe to call virtual methods here.
     * Reimplementing this method allows to perform any object cleanups.
     */
    virtual void destroyed();

private:
    int m_refcount;
    Mutex* m_mutex;
};

/**
 * Internal helper class providing a non-inline method to RefPointer.
 * Please don't use this class directly, use @ref RefPointer instead.
 * @short Internal helper class
 */
class YATE_API RefPointerBase
{
protected:
    /**
     * Default constructor, initialize to null pointer
     */
    inline RefPointerBase()
	: m_pointer(0) { }

    /**
     * Set a new stored pointer
     * @param oldptr Pointer to the RefObject of the old stored object
     * @param newptr Pointer to the RefObject of the new stored object
     * @param pointer A void pointer to the derived class
     */
    void assign(RefObject* oldptr, RefObject* newptr, void* pointer);

    /**
     * The untyped stored pointer that should be casted to a @ref RefObject derived class
     */
    void* m_pointer;
};

/**
 * @short Templated smart pointer class
 */
template <class Obj = RefObject> class RefPointer : public RefPointerBase
{
protected:
    /**
     * Retrieve the stored pointer
     * @return A typed pointer
     */
    inline Obj* pointer() const
	{ return static_cast<Obj*>(m_pointer); }

    /**
     * Set a new stored pointer
     * @param object Pointer to the new stored object
     */
    inline void assign(Obj* object = 0)
	{ RefPointerBase::assign(pointer(),object,object); }

public:
    /**
     * Default constructor - creates a null smart pointer
     */
    inline RefPointer()
	{ }

    /**
     * Copy constructor, references the object
     * @param value Original RefPointer
     */
    inline RefPointer(const RefPointer<Obj>& value)
	: RefPointerBase()
	{ assign(value); }

    /**
     * Constructs an initialized smart pointer, references the object
     * @param object Pointer to object
     */
    inline RefPointer(Obj* object)
	{ assign(object); }

    /**
     * Destructs the pointer and dereferences the object
     */
    inline ~RefPointer()
	{ assign(); }

    /**
     * Assignment from smart pointer
     */
    inline RefPointer<Obj>& operator=(const RefPointer<Obj>& value)
	{ assign(value.pointer()); return *this; }

    /**
     * Assignment from regular pointer
     */
    inline RefPointer<Obj>& operator=(Obj* object)
	{ assign(object); return *this; }

    /**
     * Conversion to regular pointer operator
     * @return The stored pointer
     */
    inline operator Obj*() const
	{ return pointer(); }

    /**
     * Member access operator
     */
    inline Obj* operator->() const
	{ return pointer(); }

    /**
     * Dereferencing operator
     */
    inline Obj& operator*() const
	{ return *pointer(); }
};

/**
 * @short Templated pointer that can be inserted in a list
 */
template <class Obj = GenObject> class GenPointer : public GenObject
{
private:
    /**
     * The stored pointer
     */
    Obj* m_pointer;

public:
    /**
     * Default constructor - creates a null pointer
     */
    inline GenPointer()
	: m_pointer(0)
	{ }

    /**
     * Copy constructor
     * @param value Original GenPointer
     */
    inline GenPointer(const GenPointer<Obj>& value)
	: m_pointer(value)
	{ }

    /**
     * Constructs an initialized pointer
     * @param object Pointer to object
     */
    inline GenPointer(Obj* object)
	: m_pointer(object)
	{ }

    /**
     * Assignment from another GenPointer
     */
    inline GenPointer<Obj>& operator=(const GenPointer<Obj>& value)
	{ m_pointer = value; return *this; }

    /**
     * Assignment from regular pointer
     */
    inline GenPointer<Obj>& operator=(Obj* object)
	{ m_pointer = object; return *this; }

    /**
     * Conversion to regular pointer operator
     * @return The stored pointer
     */
    inline operator Obj*() const
	{ return m_pointer; }

    /**
     * Member access operator
     */
    inline Obj* operator->() const
	{ return m_pointer; }

    /**
     * Dereferencing operator
     */
    inline Obj& operator*() const
	{ return *m_pointer; }
};

/**
 * A simple single-linked object list handling class
 * @short An object list class
 */
class YATE_API ObjList : public GenObject
{
    YNOCOPY(ObjList); // no automatic copies please
public:
    /**
     * Creates a new, empty list.
     */
    ObjList();

    /**
     * Destroys the list and everything in it.
     */
    virtual ~ObjList();

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Get the number of elements in the list
     * @return Count of items
     */
    unsigned int length() const;

    /**
     * Get the number of non-null objects in the list
     * @return Count of items
     */
    unsigned int count() const;

    /**
     * Get the object associated to this list item
     * @return Pointer to the object or NULL
     */
    inline GenObject* get() const
	{ return m_obj; }

    /**
     * Set the object associated to this list item
     * @param obj Pointer to the new object to set
     * @param delold True to delete the old object (default)
     * @return Pointer to the old object if not destroyed
     */
    GenObject* set(const GenObject* obj, bool delold = true);

    /**
     * Get the next item in the list
     * @return Pointer to the next item in list or NULL
     */
    inline ObjList* next() const
	{ return m_next; }

    /**
     * Get the last item in the list
     * @return Pointer to the last item in list
     */
    ObjList* last() const;

    /**
     * Skip over NULL holding items in the list
     * @return Pointer to the first non NULL holding item in list or NULL
     */
    ObjList* skipNull() const;

    /**
     * Advance in the list skipping over NULL holding items
     * @return Pointer to the next non NULL holding item in list or NULL
     */
    ObjList* skipNext() const;

    /**
     * Get the object at a specific index in list
     * @param index Index of the object to retrieve
     * @return Pointer to the object or NULL
     */
    GenObject* at(int index) const;

    /**
     * Pointer-like indexing operator
     * @param index Index of the list item to retrieve
     * @return Pointer to the list item or NULL
     */
    ObjList* operator+(int index) const;

    /**
     * Array-like indexing operator with signed parameter
     * @param index Index of the object to retrieve
     * @return Pointer to the object or NULL
     */
    inline GenObject* operator[](signed int index) const
	{ return at(index); }

    /**
     * Array-like indexing operator with unsigned parameter
     * @param index Index of the object to retrieve
     * @return Pointer to the object or NULL
     */
    inline GenObject* operator[](unsigned int index) const
	{ return at(index); }

    /**
     * Array-like indexing operator
     * @param str String value of the object to locate
     * @return Pointer to the object or NULL
     */
    GenObject* operator[](const String& str) const;

    /**
     * Get the item in the list that holds an object
     * @param obj Pointer to the object to search for
     * @return Pointer to the found item or NULL
     */
    ObjList* find(const GenObject* obj) const;

    /**
     * Get the item in the list that holds an object by String value
     * @param str String value (toString) of the object to search for
     * @return Pointer to the found item or NULL
     */
    ObjList* find(const String& str) const;

    /**
     * Get the position in list of a GenObject by a pointer to it
     * @param obj Pointer to the object to search for
     * @return Index of object in list, -1 if not found
     */
    int index(const GenObject* obj) const;

    /**
     * Get the position in list of the first GenObject with a given value
     * @param str String value (toString) of the object to search for
     * @return Index of object in list, -1 if not found
     */
    int index(const String& str) const;

    /**
     * Insert an object at this point
     * @param obj Pointer to the object to insert
     * @param compact True to replace NULL values in list if possible
     * @return A pointer to the inserted list item
     */
    ObjList* insert(const GenObject* obj, bool compact = true);

    /**
     * Append an object to the end of the list
     * @param obj Pointer to the object to append
     * @param compact True to replace NULL values in list if possible
     * @return A pointer to the inserted list item
     */
    ObjList* append(const GenObject* obj, bool compact = true);

    /**
     * Set unique entry in this list. If not found, append it to the list
     * @param obj Pointer to the object to uniquely set in the list
     * @param compact True to replace NULL values in list if possible
     * @return A pointer to the set list item
     */
    ObjList* setUnique(const GenObject* obj, bool compact = true);

    /**
     * Delete this list item
     * @param delobj True to delete the object (default)
     * @return Pointer to the object if not destroyed
     */
    GenObject* remove(bool delobj = true);

    /**
     * Delete the list item that holds a given object
     * @param obj Object to search in the list
     * @param delobj True to delete the object (default)
     * @return Pointer to the object if not destroyed
     */
    GenObject* remove(GenObject* obj, bool delobj = true);

    /**
     * Delete the first list item that holds an object with a iven value
     * @param str String value (toString) of the object to remove
     * @param delobj True to delete the object (default)
     * @return Pointer to the object if not destroyed
     */
    GenObject* remove(const String& str, bool delobj = true);

    /**
     * Clear the list and optionally delete all contained objects
     */
    void clear();

    /**
     * Remove all empty objects in the list
     */
    void compact();

    /**
     * Get the automatic delete flag
     * @return True if will delete on destruct, false otherwise
     */
    inline bool autoDelete()
	{ return m_delete; }

    /**
     * Set the automatic delete flag
     * @param autodelete True to delete on destruct, false otherwise
     */
    inline void setDelete(bool autodelete)
	{ m_delete = autodelete; }

    /**
     * A static empty object list
     * @return Reference to a static empty list
     */
    static const ObjList& empty();

    /**
     * Sort this list
     * @param callbackCompare pointer to a callback function that should compare two objects.
     * <pre>
     *     obj1 First object of the comparation
     *     obj2 Second object of the comparation
     *     context Data context
     *     return 0 if the objects are equal; positive value if obj2 > obj1; negative value if obj1 > obj2
     *     Note: the function should expect receiving null pointers
     * </pre>
     * @param context Context data.
     */
    void sort(int (*callbackCompare)(GenObject* obj1, GenObject* obj2, void* context), void* context = 0);
private:
    ObjList* m_next;
    GenObject* m_obj;
    bool m_delete;
};

/**
 * Simple vector class that holds objects derived from GenObject
 * @short A vector holding GenObjects
 */
class YATE_API ObjVector : public GenObject
{
    YNOCOPY(ObjVector); // no automatic copies please
public:
    /**
     * Constructor of a zero capacity vector
     * @param autodelete True to delete objects on destruct, false otherwise
     */
    inline explicit ObjVector(bool autodelete = true)
	: m_length(0), m_objects(0), m_delete(autodelete)
	{ }

    /**
     * Constructor of an empty vector
     * @param maxLen Maximum number of objects the vector can hold
     * @param autodelete True to delete objects on destruct, false otherwise
     */
    ObjVector(unsigned int maxLen, bool autodelete = true);

    /**
     * Constructor from an object list
     * @param list List of objects to store in vector
     * @param move True to move elements from list, false to just copy the pointer
     * @param maxLen Maximum number of objects to put in vector, zero to put all
     * @param autodelete True to delete objects on destruct, false otherwise
     */
    ObjVector(ObjList& list, bool move = true, unsigned int maxLen = 0, bool autodelete = true);

    /**
     * Destroys the vector and the objects if automatic delete is set
     */
    virtual ~ObjVector();

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Get the capacity of the vector
     * @return Number of items the vector can hold
     */
    inline unsigned int length() const
	{ return m_length; }

    /**
     * Get the number of non-null objects in the vector
     * @return Count of items
     */
    unsigned int count() const;

    /**
     * Check if the vector is empty
     * @return True if the vector contains no objects
     */
    bool null() const;

    /**
     * Get the object at a specific index in vector
     * @param index Index of the object to retrieve
     * @return Pointer to the object or NULL
     */
    inline GenObject* at(int index) const
	{ return (index >= 0 && index < (int)m_length) ? m_objects[index] : 0; }

    /**
     * Indexing operator with signed parameter
     * @param index Index of the object to retrieve
     * @return Pointer to the object or NULL
     */
    inline GenObject* operator[](signed int index) const
	{ return at(index); }

    /**
     * Indexing operator with unsigned parameter
     * @param index Index of the object to retrieve
     * @return Pointer to the object or NULL
     */
    inline GenObject* operator[](unsigned int index) const
	{ return at(index); }

    /**
     * Clear the vector and assign objects from a list
     * @param list List of objects to store in vector
     * @param move True to move elements from list, false to just copy the pointer
     * @param maxLen Maximum number of objects to put in vector, zero to put all
     * @return Capacity of the vector
     */
    unsigned int assign(ObjList& list, bool move = true, unsigned int maxLen = 0);

    /**
     * Retrieve and remove an object from the vector
     * @param index Index of the object to retrieve
     * @return Pointer to the stored object, NULL for out of bound index
     */
    GenObject* take(unsigned int index);

    /**
     * Store an object in the vector
     * @param obj Object to store in vector
     * @param index Index of the object to store
     * @return True for success, false if index was out of bounds
     */
    bool set(GenObject* obj, unsigned int index);

    /**
     * Get the position in vector of a GenObject by a pointer to it
     * @param obj Pointer to the object to search for
     * @return Index of object in vector, -1 if not found
     */
    int index(const GenObject* obj) const;

    /**
     * Get the position in vector of the first GenObject with a given value
     * @param str String value (toString) of the object to search for
     * @return Index of object in vector, -1 if not found
     */
    int index(const String& str) const;

    /**
     * Clear the vector and optionally delete all contained objects
     */
    void clear();

    /**
     * Get the automatic delete flag
     * @return True if will delete objects on destruct, false otherwise
     */
    inline bool autoDelete()
	{ return m_delete; }

    /**
     * Set the automatic delete flag
     * @param autodelete True to delete objects on destruct, false otherwise
     */
    inline void setDelete(bool autodelete)
	{ m_delete = autodelete; }

private:
    unsigned int m_length;
    GenObject** m_objects;
    bool m_delete;
};

/**
 * A simple Array class derivated from RefObject
 * It uses one ObjList to keep the pointers to other ObjList's.
 * Data is organized in columns - the main ObjList holds pointers to one
 *  ObjList for each column.
 * This class has been written by Diana
 * @short A list based Array
 */
class YATE_API Array : public RefObject
{
public:
    /**
     * Creates a new empty array.
     * @param columns Initial number of columns
     * @param rows Initial number of rows
     */
    explicit Array(int columns = 0, int rows = 0);

    /**
     * Destructor. Destructs all objects in the array
     */
    virtual ~Array();

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Insert a row of objects
     * @param row List of objects to insert or NULL
     * @param index Number of the row to insert before, negative to append
     * @return True for success, false if index was larger than the array
     */
    bool addRow(ObjList* row = 0, int index = -1);

    /**
     * Insert a column of objects
     * @param column List of objects to insert or NULL
     * @param index Number of the column to insert before, negative to append
     * @return True for success, false if index was larger than the array
     */
    bool addColumn(ObjList* column = 0, int index = -1);

    /**
     * Delete an entire row of objects
     * @param index Number of the row to delete
     * @return True for success, false if index was out of bounds
     */
    bool delRow(int index);

    /**
     * Delete an entire column of objects
     * @param index Number of the column to delete
     * @return True for success, false if index was out of bounds
     */
    bool delColumn(int index);

    /**
     * Retrieve an object from the array
     * @param column Number of the column in the array
     * @param row Number of the row in the array
     * @return Pointer to the stored object, NULL for out of bound indexes
     */
    GenObject* get(int column, int row) const;

    /**
     * Retrieve and remove an object from the array
     * @param column Number of the column in the array
     * @param row Number of the row in the array
     * @return Pointer to the stored object, NULL for out of bound indexes
     */
    GenObject* take(int column, int row);

    /**
     * Store an object in the array
     * @param obj Object to store in the array
     * @param column Number of the column in the array
     * @param row Number of the row in the array
     * @return True for success, false if indexes were out of bounds
     */
    bool set(GenObject* obj, int column, int row);

    /**
     * Get the number of rows in the array
     * @return Total number of rows
     */
    inline int getRows() const
	{ return m_rows; }

    /**
     * Get the number of columns in the array
     * @return Total number of columns
     */
    inline int getColumns() const
	{ return m_columns; }

    /**
     * Retrieve a column.
     * Note: Use the returned list only to get or set data.
     *  List items must not be removed or appended
     * @param column Column to retrieve
     * @return Pointer to column list, NULL for out of bound indexes
     */
    inline ObjList* getColumn(int column) const {
	    if (column >= 0 || column < m_columns)
		return static_cast<ObjList*>(m_obj[column]);
	    return 0;
	}

private:
    int m_rows;
    int m_columns;
    ObjList m_obj;
};

class Regexp;
class StringMatchPrivate;

/**
 * A simple class to hold a single Unicode character and convert it to / from UTF-8
 * @short A single Unicode character
 */
class YATE_API UChar
{
public:
    /**
     * Constructor from unsigned numeric code
     * @param code Code of the Unicode character
     */
    inline explicit UChar(uint32_t code = 0)
	: m_chr(code)
	{ encode(); }

    /**
     * Constructor from signed numeric code
     * @param code Code of the Unicode character
     */
    inline explicit UChar(int32_t code)
	: m_chr((code < 0) ? 0 : code)
	{ encode(); }

    /**
     * Constructor from signed character
     * @param code Character to construct from
     */
    inline explicit UChar(signed char code)
	: m_chr((unsigned char)code)
	{ encode(); }

    /**
     * Constructor from unsigned character
     * @param code Character to construct from
     */
    inline explicit UChar(unsigned char code)
	: m_chr(code)
	{ encode(); }

    /**
     * Assignment operator from a character code
     * @param code Character code to assign
     * @return Reference to this object
     */
    inline UChar& operator=(uint32_t code)
	{ m_chr = code; encode(); return *this; }

    /**
     * Assignment operator from a character
     * @param code Character to assign
     * @return Reference to this object
     */
    inline UChar& operator=(char code)
	{ m_chr = (unsigned char)code; encode(); return *this; }

    /**
     * Get the Unicode value of the character
     * @return Code of the character as defined by Unicode
     */
    inline uint32_t code() const
	{ return m_chr; }

    /**
     * Get the value of the character as UTF-8 string.
     * @return The character as UTF-8 C string
     */
    inline const char* c_str() const
	{ return m_str; }

    /**
     * Conversion to "const char *" operator.
     * @return Pointer to the internally stored UTF-8 string
     */
    inline operator const char*() const
	{ return m_str; };

    /**
     * Decode the first Unicode character from an UTF-8 C string
     * @param str String to extract from, will be advanced past the character
     * @param maxChar Maximum accepted Unicode character code
     * @param overlong Accept overlong UTF-8 sequences (dangerous!)
     * @return True if an Unicode character was decoded from string
     */
    bool decode(const char*& str, uint32_t maxChar = 0x10ffff, bool overlong = false);

private:
    void encode();
    uint32_t m_chr;
    char m_str[8];
};

/**
 * A simple string handling class for C style (one byte) strings.
 * For simplicity and read speed no copy-on-write is performed.
 * Strings have hash capabilities and comparations are using the hash
 * for fast inequality check.
 * @short A C-style string handling class
 */
class YATE_API String : public GenObject
{
public:
    enum Align {
	Left = 0,
	Center,
	Right
    };

    /**
     * Creates a new, empty string.
     */
    String();

    /**
     * Creates a new initialized string.
     * @param value Initial value of the string
     * @param len Length of the data to copy, -1 for full string
     */
    String(const char* value, int len = -1);

    /**
     * Creates a new initialized string.
     * @param value Character to fill the string
     * @param repeat How many copies of the character to use
     */
    explicit String(char value, unsigned int repeat = 1);

    /**
     * Creates a new initialized string from a 32 bit integer.
     * @param value Value to convert to string
     */
    explicit String(int32_t value);

    /**
     * Creates a new initialized string from a 32 bit unsigned int.
     * @param value Value to convert to string
     */
    explicit String(uint32_t value);

    /**
     * Creates a new initialized string from a 64 bit integer.
     * @param value Value to convert to string
     */
    explicit String(int64_t value);

    /**
     * Creates a new initialized string from a 64 bit unsigned int.
     * @param value Value to convert to string
     */
    explicit String(uint64_t value);

    /**
     * Creates a new initialized string from a boolean.
     * @param value Value to convert to string
     */
    explicit String(bool value);

    /**
     * Creates a new initialized string from a double value.
     * @param value Value to convert to string
     */
    explicit String(double value);

    /**
     * Copy constructor.
     * @param value Initial value of the string
     */
    String(const String& value);

    /**
     * Constructor from String pointer.
     * @param value Initial value of the string
     */
    String(const String* value);

    /**
     * Destroys the string, disposes the memory.
     */
    virtual ~String();

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * A static null String
     * @return Reference to a static empty String
     */
    static const String& empty();

    /**
     * A standard text representation of boolean values
     * @param value Boolean value to convert
     * @return Pointer to a text representation of the value
     */
    inline static const char* boolText(bool value)
	{ return value ? "true" : "false"; }

    /**
     * Get the value of the stored string.
     * @return The stored C string which may be NULL.
     */
    inline const char* c_str() const
	{ return m_string; }

    /**
     * Get a valid non-NULL C string.
     * @return The stored C string or a static "".
     */
    inline const char* safe() const
	{ return m_string ? m_string : ""; }

    /**
     * Get a valid non-NULL C string with a provided default.
     * @param defStr Default C string to return if stored is NULL
     * @return The stored C string, the default or a static "".
     */
    inline const char* safe(const char* defStr) const
	{ return m_string ? m_string : (defStr ? defStr : ""); }

    /**
     * Get the length of the stored string.
     * @return The length of the stored string, zero for NULL.
     */
    inline unsigned int length() const
	{ return m_length; }

    /**
     * Checks if the string holds a NULL pointer.
     * @return True if the string holds NULL, false otherwise.
     */
    inline bool null() const
	{ return !m_string; }

    /**
     * Get the number of characters in a string assuming UTF-8 encoding
     * @param value C string to compute Unicode length
     * @param maxChar Maximum accepted Unicode character code
     * @param overlong Accept overlong UTF-8 sequences (dangerous!)
     * @return Count of Unicode characters, -1 if not valid UTF-8
     */
    static int lenUtf8(const char* value, uint32_t maxChar = 0x10ffff, bool overlong = false);

    /**
     * Get the number of characters in the string assuming UTF-8 encoding
     * @param maxChar Maximum accepted Unicode character code
     * @param overlong Accept overlong UTF-8 sequences (dangerous!)
     * @return Count of Unicode characters, -1 if not valid UTF-8
     */
    inline int lenUtf8(uint32_t maxChar = 0x10ffff, bool overlong = false) const
	{ return lenUtf8(m_string,maxChar,overlong); }


    /**
     * Fix an UTF-8 encoded string by replacing invalid sequences
     * @param replace String to replace invalid sequences, use U+FFFD if null
     * @param maxChar Maximum accepted Unicode character code
     * @param overlong Accept overlong UTF-8 sequences (dangerous!)
     * @return Count of invalid UTF-8 sequences that were replaced
     */
    int fixUtf8(const char* replace = 0, uint32_t maxChar = 0x10ffff, bool overlong = false);

    /**
     * Check if a string starts with UTF-8 Byte Order Mark
     * @param str String to check for BOM
     * @return True if the string starts with UTF-8 BOM
     */
    inline static bool checkBOM(const char* str)
	{ return str && (str[0] == '\357') && (str[1] == '\273') && (str[2] == '\277'); }

    /**
     * Check if this string starts with UTF-8 Byte Order Mark
     * @return True if the string starts with UTF-8 BOM
     */
    inline bool checkBOM() const
	{ return checkBOM(c_str()); }

    /**
     * Advance a const string past an UTF-8 Byte Order Mark
     * @param str String to check for and strip BOM
     * @return True if the string started with UTF-8 BOM
     */
    inline static bool stripBOM(const char*& str)
	{ return checkBOM(str) && (str += 3); }

    /**
     * Advance a string past an UTF-8 Byte Order Mark
     * @param str String to check for and strip BOM
     * @return True if the string started with UTF-8 BOM
     */
    inline static bool stripBOM(char*& str)
	{ return checkBOM(str) && (str += 3); }

    /**
     * Strip an UTF-8 Byte Order Mark from the start of this string
     * @return True if the string started with UTF-8 BOM
     */
    inline bool stripBOM()
	{ return checkBOM(c_str()) && &(*this = c_str() + 3); }

    /**
     * Get the hash of the contained string.
     * @return The hash of the string.
     */
    inline unsigned int hash() const
	{
	    if (m_hash == YSTRING_INIT_HASH)
		m_hash = hash(m_string);
	    return m_hash;
	}

    /**
     * Get the hash of an arbitrary string.
     * @param value C string to hash
     * @param h Old hash value for incremental hashing
     * @return The hash of the string.
     */
    static unsigned int hash(const char* value, unsigned int h = 0);

    /**
     * Clear the string and free the memory
     */
    void clear();

    /**
     * Extract the caracter at a given index
     * @param index Index of character in string
     * @return Character at given index or 0 if out of range
     */
    char at(int index) const;

    /**
     * Substring extraction
     * @param offs Offset of the substring, negative to count from end
     * @param len Length of the substring, -1 for everything possible
     * @return A copy of the requested substring
     */
    String substr(int offs, int len = -1) const;

    /**
     * Strip off leading and trailing blank characters
     */
    String& trimBlanks();

    /**
     * Strip off leading and trailing whitespace characters
     *  (blank, tabs, form-feed, newlines)
     */
    String& trimSpaces();

    /**
     * Override GenObject's method to return this String
     * @return A reference to this String
     */
    virtual const String& toString() const;

    /**
     * Convert the string to an integer value.
     * @param defvalue Default to return if the string is not a number
     * @param base Numeration base, 0 to autodetect
     * @param minvalue Minimum value allowed
     * @param maxvalue Maximum value allowed
     * @param clamp Control the out of bound values: true to adjust to the nearest
     *  bound, false to return the default value
     * @return The integer interpretation or defvalue.
     */
    int toInteger(int defvalue = 0, int base = 0, int minvalue = INT_MIN,
	int maxvalue = INT_MAX, bool clamp = true) const;

    /**
     * Convert the string to an integer value looking up first a token table.
     * @param tokens Pointer to an array of tokens to lookup first
     * @param defvalue Default to return if the string is not a token or number
     * @param base Numeration base, 0 to autodetect
     * @return The integer interpretation or defvalue.
     */
    int toInteger(const TokenDict* tokens, int defvalue = 0, int base = 0) const;

    /**
     * Convert the string to an long integer value.
     * @param defvalue Default to return if the string is not a number
     * @param base Numeration base, 0 to autodetect
     * @param minvalue Minimum value allowed
     * @param maxvalue Maximum value allowed
     * @param clamp Control the out of bound values: true to adjust to the nearest
     *  bound, false to return the default value
     * @return The long integer interpretation or defvalue.
     */
    long int toLong(long int defvalue = 0, int base = 0, long int minvalue = LONG_MIN,
	long int maxvalue = LONG_MAX, bool clamp = true) const;

    /**
     * Convert the string to an 64 bit integer value.
     * @param defvalue Default to return if the string is not a number
     * @param base Numeration base, 0 to autodetect
     * @param minvalue Minimum value allowed
     * @param maxvalue Maximum value allowed
     * @param clamp Control the out of bound values: true to adjust to the nearest
     *  bound, false to return the default value
     * @return The 64 bit integer interpretation or defvalue.
     */
    int64_t toInt64(int64_t defvalue = 0, int base = 0, int64_t minvalue = LLONG_MIN,
	int64_t maxvalue = LLONG_MAX, bool clamp = true) const;

    /**
     * Convert the string to a floating point value.
     * @param defvalue Default to return if the string is not a number
     * @return The floating-point interpretation or defvalue.
     */
    double toDouble(double defvalue = 0.0) const;

    /**
     * Convert the string to a boolean value.
     * @param defvalue Default to return if the string is not a bool
     * @return The boolean interpretation or defvalue.
     */
    bool toBoolean(bool defvalue = false) const;

    /**
     * Check if the string can be converted to a boolean value.
     * @return True if the string is a valid boolean.
     */
    bool isBoolean() const;

    /**
     * Turn the string to an all-uppercase string
     * @return A reference to this String
     */
    String& toUpper();

    /**
     * Turn the string to an all-lowercase string
     * @return A reference to this String
     */
    String& toLower();

    /**
     * Indexing operator with signed int
     * @param index Index of character in string
     * @return Character at given index or 0 if out of range
     */
    inline char operator[](signed int index) const
	{ return at(index); }

    /**
     * Indexing operator with unsigned int
     * @param index Index of character in string
     * @return Character at given index or 0 if out of range
     */
    inline char operator[](unsigned int index) const
	{ return at(index); }

    /**
     * Conversion to "const char *" operator.
     * @return Pointer to the internally stored string
     */
    inline operator const char*() const
	{ return m_string; };

    /**
     * Assigns a new value to the string from a character block.
     * @param value New value of the string
     * @param len Length of the data to copy, -1 for full string
     * @return Reference to the String
     */
    String& assign(const char* value, int len = -1);

    /**
     * Assigns a new value by filling with a repeated character
     * @param value Character to fill the string
     * @param repeat How many copies of the character to use
     * @return Reference to the String
     */
    String& assign(char value, unsigned int repeat = 1);

    /**
     * Build a hexadecimal representation of a buffer of data
     * @param data Pointer to data to dump
     * @param len Length of the data buffer
     * @param sep Separator character to use between octets
     * @param upCase Set to true to use upper case characters in hexa
     * @return Reference to the String
     */
    String& hexify(void* data, unsigned int len, char sep = 0, bool upCase = false);

    /**
     * Assignment operator.
     * @param value Value to assign to the string
     */
    inline String& operator=(const String& value)
	{ return operator=(value.c_str()); }

    /**
     * Assignment from String* operator.
     * @param value Value to assign to the string
     * @see TelEngine::strcpy
     */
    inline String& operator=(const String* value)
	{ return operator=(value ? value->c_str() : ""); }

    /**
     * Assignment from char* operator.
     * @param value Value to assign to the string
     * @see TelEngine::strcpy
     */
    String& operator=(const char* value);

    /**
     * Assignment operator for single characters.
     * @param value Value to assign to the string
     */
    String& operator=(char value);

    /**
     * Assignment operator for 32 bit integers.
     * @param value Value to assign to the string
     */
    String& operator=(int32_t value);

    /**
     * Assignment operator for 32 bit unsigned integers.
     * @param value Value to assign to the string
     */
    String& operator=(uint32_t value);

    /**
     * Assignment operator for 64 bit integers.
     * @param value Value to assign to the string
     */
    String& operator=(int64_t value);

    /**
     * Assignment operator for 64 bit unsigned integers.
     * @param value Value to assign to the string
     */
    String& operator=(uint64_t value);

    /**
     * Assignment operator for booleans.
     * @param value Value to assign to the string
     */
    inline String& operator=(bool value)
	{ return operator=(boolText(value)); }

    /**
     * Assignment operator for double.
     * @param value Value to assign to the string
     */
    String& operator=(double value);

    /**
     * Appending operator for strings.
     * @param value Value to assign to the string
     * @see TelEngine::strcat
     */
    inline String& operator+=(const char* value)
	{ return append(value,-1); }

    /**
     * Appending operator for single characters.
     * @param value Value to append to the string
     */
    String& operator+=(char value);

    /**
     * Appending operator for 32 bit integers.
     * @param value Value to append to the string
     */
    String& operator+=(int32_t value);

    /**
     * Appending operator for 32 bit unsigned integers.
     * @param value Value to append to the string
     */
    String& operator+=(uint32_t value);

    /**
     * Appending operator for 64 bit integers.
     * @param value Value to append to the string
     */
    String& operator+=(int64_t value);

    /**
     * Appending operator for 64 bit unsigned integers.
     * @param value Value to append to the string
     */
    String& operator+=(uint64_t value);

    /**
     * Appending operator for booleans.
     * @param value Value to append to the string
     */
    inline String& operator+=(bool value)
	{ return operator+=(boolText(value)); }

    /**
     * Appending operator for double.
     * @param value Value to append to the string
     */
    String& operator+=(double value);

    /**
     * Equality operator.
     */
    bool operator==(const char* value) const;

    /**
     * Inequality operator.
     */
    bool operator!=(const char* value) const;

    /**
     * Fast equality operator.
     */
    inline bool operator==(const String& value) const
	{ return (this == &value) || ((hash() == value.hash()) && operator==(value.c_str())); }

    /**
     * Fast inequality operator.
     */
    inline bool operator!=(const String& value) const
	{ return (this != &value) && ((hash() != value.hash()) || operator!=(value.c_str())); }

    /**
     * Case-insensitive equality operator.
     */
    bool operator&=(const char* value) const;

    /**
     * Case-insensitive inequality operator.
     */
    bool operator|=(const char* value) const;

    /**
     * Stream style appending operator for C strings
     */
    inline String& operator<<(const char* value)
	{ return operator+=(value); }

    /**
     * Stream style appending operator for single characters
     */
    inline String& operator<<(char value)
	{ return operator+=(value); }

    /**
     * Stream style appending operator for 32 bit integers
     */
    inline String& operator<<(int32_t value)
	{ return operator+=(value); }

    /**
     * Stream style appending operator for 32 bit unsigned integers
     */
    inline String& operator<<(uint32_t value)
	{ return operator+=(value); }

    /**
     * Stream style appending operator for 64 bit integers
     */
    inline String& operator<<(int64_t value)
	{ return operator+=(value); }

    /**
     * Stream style appending operator for 64 bit unsigned integers
     */
    inline String& operator<<(uint64_t value)
	{ return operator+=(value); }

    /**
     * Stream style appending operator for booleans
     */
    inline String& operator<<(bool value)
	{ return operator+=(value); }

    /**
     * Stream style appending operator for double
     */
    inline String& operator<<(double value)
	{ return operator+=(value); }

    /**
     * Stream style substring skipping operator.
     * It eats all characters up to and including the skip string
     */
    String& operator>>(const char* skip);

    /**
     * Stream style extraction operator for single characters
     */
    String& operator>>(char& store);

    /**
     * Stream style extraction operator for single Unicode characters
     */
    String& operator>>(UChar& store);

    /**
     * Stream style extraction operator for integers
     */
    String& operator>>(int& store);

    /**
     * Stream style extraction operator for unsigned integers
     */
    String& operator>>(unsigned int& store);

    /**
     * Stream style extraction operator for booleans
     */
    String& operator>>(bool& store);

    /**
     * Append a string to the current string
     * @param value String from which to append
     * @param len Length of the data to copy, -1 for full string
     * @return Reference to the String
     */
    String& append(const char* value, int len);

    /**
     * Conditional appending with a separator
     * @param value String to append
     * @param separator Separator to insert before the value
     * @param force True to allow appending empty strings
     */
    String& append(const char* value, const char* separator = 0, bool force = false);

    /**
     * List members appending with a separator
     * @param list Pointer to ObjList whose @ref GenObject::toString() of the items will be appended
     * @param separator Separator to insert before each item in list
     * @param force True to allow appending empty strings
     */
    String& append(const ObjList* list, const char* separator = 0, bool force = false);

    /**
     * List members appending with a separator
     * @param list Reference of ObjList whose @ref GenObject::toString() of the items will be appended
     * @param separator Separator to insert before each item in list
     * @param force True to allow appending empty strings
     */
    inline String& append(const ObjList& list, const char* separator = 0, bool force = false)
	{ return append(&list,separator,force); }

    /**
     * Explicit double append
     * @param value Value to append
     * @param decimals Number of decimals
     */
    String& append(double value, unsigned int decimals = 3);

    /**
     * Build a String in a printf style.
     * @param format The output format.
     * NOTE: The length of the resulting string will be at most 128 + length of format
     */
    String& printf(const char* format, ...) FORMAT_CHECK(2);

    /**
     * Build a String in a printf style.
     * @param length maximum length of the resulting string
     * @param format The output format.
     */
    String& printf(unsigned int length, const char* format,  ...) FORMAT_CHECK(3);

    /**
     * Build a fixed aligned string from str and append it.
     * @param fixedLength The fixed length in which the 'str' will be aligned.
     * @param str The string to align
     * @param len The number of characters to use from str.
     * @param fill Character to fill the empty space.
     * @param align The alignment mode.
     */
    String& appendFixed(unsigned int fixedLength, const char* str, unsigned int len = -1, char fill = ' ', int align = Left);

    /**
     * Build a fixed aligned string from str and append it.
     * @param fixedLength The fixed length in which the 'str' will be aligned.
     * @param str The string to align
     * @param fill Character to fill the empty space.
     * @param align The alignment mode.
     */
    inline String& appendFixed(unsigned int fixedLength, const String& str, char fill = ' ', int align = Left)
	{ return appendFixed(fixedLength,str.c_str(),str.length(),fill,align); }

    /**
     * Locate the first instance of a character in the string
     * @param what Character to search for
     * @param offs Offset in string to start searching from
     * @return Offset of character or -1 if not found
     */
    int find(char what, unsigned int offs = 0) const;

    /**
     * Locate the first instance of a substring in the string
     * @param what Substring to search for
     * @param offs Offset in string to start searching from
     * @return Offset of substring or -1 if not found
     */
    int find(const char* what, unsigned int offs = 0) const;

    /**
     * Locate the last instance of a character in the string
     * @param what Character to search for
     * @return Offset of character or -1 if not found
     */
    int rfind(char what) const;

    /**
     * Locate the last instance of a substring in the string
     * @param what Substring to search for
     * @return Offset of substring or -1 if not found
     */
    int rfind(const char* what) const;

    /**
     * Checks if the string starts with a substring
     * @param what Substring to search for
     * @param wordBreak Check if a word boundary follows the substring
     * @param caseInsensitive Compare case-insensitive if set
     * @return True if the substring occurs at the beginning of the string
     */
    bool startsWith(const char* what, bool wordBreak = false, bool caseInsensitive = false) const;

    /**
     * Checks if the string ends with a substring
     * @param what Substring to search for
     * @param wordBreak Check if a word boundary precedes the substring
     * @param caseInsensitive Compare case-insensitive if set
     * @return True if the substring occurs at the end of the string
     */
    bool endsWith(const char* what, bool wordBreak = false, bool caseInsensitive = false) const;

    /**
     * Checks if the string starts with a substring and removes it
     * @param what Substring to search for
     * @param wordBreak Check if a word boundary follows the substring;
     *  this parameter defaults to True because the intended use of this
     *  method is to separate commands from their parameters
     * @param caseInsensitive Compare case-insensitive if set
     * @return True if the substring occurs at the beginning of the string
     *  and also removes the substring; if wordBreak is True any word
     *  breaking characters are also removed
     */
    bool startSkip(const char* what, bool wordBreak = true, bool caseInsensitive = false);

    /**
     * Extract a substring up to a separator
     * @param sep Separator string to match after extracted fragment
     * @param store Reference to String variable to store extracted fragment
     * @return Reference to this string
     */
    String& extractTo(const char* sep, String& store);

    /**
     * Extract a boolean substring up to a separator
     * @param sep Separator string to match after extracted fragment
     * @param store Reference to boolean variable to store extracted fragment
     * @return Reference to this string
     */
    String& extractTo(const char* sep, bool& store);

    /**
     * Extract an integer value substring up to a separator
     * @param sep Separator string to match after extracted fragment
     * @param store Reference to integer variable to store extracted fragment
     * @param base Numeration base, 0 to autodetect
     * @return Reference to this string
     */
    String& extractTo(const char* sep, int& store, int base = 0);

    /**
     * Extract an integer or token value substring up to a separator
     * @param sep Separator string to match after extracted fragment
     * @param store Reference to integer variable to store extracted fragment
     * @param tokens Pointer to an array of tokens to lookup first
     * @param base Numeration base, 0 to autodetect
     * @return Reference to this string
     */
    String& extractTo(const char* sep, int& store, const TokenDict* tokens, int base = 0);

    /**
     * Extract a double value substring up to a separator
     * @param sep Separator string to match after extracted fragment
     * @param store Reference to double variable to store extracted fragment
     * @return Reference to this string
     */
    String& extractTo(const char* sep, double& store);

    /**
     * Checks if matches another string
     * @param value String to check for match
     * @return True if matches, false otherwise
     */
    virtual bool matches(const String& value) const
	{ return operator==(value); }

    /**
     * Checks if matches a regular expression and fill the match substrings
     * @param rexp Regular expression to check for match
     * @return True if matches, false otherwise
     */
    bool matches(const Regexp& rexp);

    /**
     * Get the offset of the last match
     * @param index Index of the submatch to return, 0 for full match
     * @return Offset of the last match, -1 if no match or not in range
     */
    int matchOffset(int index = 0) const;

    /**
     * Get the length of the last match
     * @param index Index of the submatch to return, 0 for full match
     * @return Length of the last match, 0 if no match or out of range
     */
    int matchLength(int index = 0) const;

    /**
     * Get a copy of a matched (sub)string
     * @param index Index of the submatch to return, 0 for full match
     * @return Copy of the matched substring
     */
    inline String matchString(int index = 0) const
	{ return substr(matchOffset(index),matchLength(index)); }

    /**
     * Create a string by replacing matched strings in a template
     * @param templ Template of the string to generate
     * @return Copy of template with "\0" - "\9" replaced with submatches
     */
    String replaceMatches(const String& templ) const;

    /**
     * Get the total number of submatches from the last match, 0 if no match
     * @return Number of matching subexpressions
     */
    int matchCount() const;

    /**
     * Splits the string at a delimiter character
     * @param separator Character where to split the string
     * @param emptyOK True if empty strings should be inserted in list
     * @return A newly allocated list of strings, must be deleted after use
     */
    ObjList* split(char separator, bool emptyOK = true) const;

    /**
     * Create an escaped string suitable for use in messages
     * @param str String to convert to escaped format
     * @param extraEsc Character to escape other than the default ones
     * @return The string with special characters escaped
     */
    static String msgEscape(const char* str, char extraEsc = 0);

    /**
     * Create an escaped string suitable for use in messages
     * @param extraEsc Character to escape other than the default ones
     * @return The string with special characters escaped
     */
    inline String msgEscape(char extraEsc = 0) const
	{ return msgEscape(c_str(),extraEsc); }

    /**
     * Decode an escaped string back to its raw form
     * @param str String to convert to unescaped format
     * @param errptr Pointer to an integer to receive the place of 1st error
     * @param extraEsc Character to unescape other than the default ones
     * @return The string with special characters unescaped
     */
    static String msgUnescape(const char* str, int* errptr = 0, char extraEsc = 0);

    /**
     * Decode an escaped string back to its raw form
     * @param errptr Pointer to an integer to receive the place of 1st error
     * @param extraEsc Character to unescape other than the default ones
     * @return The string with special characters unescaped
     */
    inline String msgUnescape(int* errptr = 0, char extraEsc = 0) const
	{ return msgUnescape(c_str(),errptr,extraEsc); }

    /**
     * Create an escaped string suitable for use in SQL queries
     * @param str String to convert to escaped format
     * @param extraEsc Character to escape other than the default ones
     * @return The string with special characters escaped
     */
    static String sqlEscape(const char* str, char extraEsc = 0);

    /**
     * Create an escaped string suitable for use in SQL queries
     * @param extraEsc Character to escape other than the default ones
     * @return The string with special characters escaped
     */
    inline String sqlEscape(char extraEsc = 0) const
	{ return sqlEscape(c_str(),extraEsc); }

    /**
     * Create an escaped string suitable for use in URIs
     * @param str String to convert to escaped format
     * @param extraEsc Character to escape other than the default ones
     * @param noEsc Optional pointer to string of characters that shouldn't be escaped
     * @return The string with special characters escaped
     */
    static String uriEscape(const char* str, char extraEsc = 0, const char* noEsc = 0);

    /**
     * Create an escaped string suitable for use in URI
     * @param extraEsc Character to escape other than the default ones
     * @param noEsc Optional pointer to string of characters that shouldn't be escaped
     * @return The string with special characters escaped
     */
    inline String uriEscape(char extraEsc = 0, const char* noEsc = 0) const
	{ return uriEscape(c_str(),extraEsc,noEsc); }

    /**
     * Decode an URI escaped string back to its raw form
     * @param str String to convert to unescaped format
     * @param errptr Pointer to an integer to receive the place of 1st error
     * @return The string with special characters unescaped
     */
    static String uriUnescape(const char* str, int* errptr = 0);

    /**
     * Decode an URI escaped string back to its raw form
     * @param errptr Pointer to an integer to receive the place of 1st error
     * @return The string with special characters unescaped
     */
    inline String uriUnescape(int* errptr = 0) const
	{ return uriUnescape(c_str(),errptr); }

    /**
     * Atom string support helper
     * @param str Reference to variable to hold the atom string
     * @param val String value to allocate to the atom
     * @return Pointer to shared atom string
     */
    static const String* atom(const String*& str, const char* val);

protected:
    /**
     * Called whenever the value changed (except in constructors).
     */
     virtual void changed();

private:
    void clearMatches();
    char* m_string;
    unsigned int m_length;
    // I hope every C++ compiler now knows about mutable...
    mutable unsigned int m_hash;
    StringMatchPrivate* m_matches;
};

/**
 * Utility function to retrieve a C string from a possibly NULL String pointer
 * @param str Pointer to a String that may be NULL
 * @return String data pointer or NULL
 */
inline const char* c_str(const String* str)
    { return str ? str->c_str() : (const char*)0; }

/**
 * Utility function to replace NULL C string pointers with an empty C string
 * @param str Pointer to a C string that may be NULL
 * @return Original pointer or pointer to an empty C string
 */
inline const char* c_safe(const char* str)
    { return str ? str : ""; }

/**
 * Utility function to replace NULL String pointers with an empty C string
 * @param str Pointer to a String that may be NULL
 * @return String data pointer or pointer to an empty C string
 */
inline const char* c_safe(const String* str)
    { return str ? str->safe() : ""; }

/**
 * Utility function to check if a C string is null or empty
 * @param str Pointer to a C string
 * @return True if str is NULL or starts with a NUL character
 */
inline bool null(const char* str)
    { return !(str && *str); }

/**
 * Utility function to check if a String is null or empty
 * @param str Pointer to a String
 * @return True if str is NULL or is empty
 */
inline bool null(const String* str)
    { return !str || str->null(); }

/**
 * Concatenation operator for strings.
 */
YATE_API String operator+(const String& s1, const String& s2);

/**
 * Concatenation operator for strings.
 */
YATE_API String operator+(const String& s1, const char* s2);

/**
 * Concatenation operator for strings.
 */
YATE_API String operator+(const char* s1, const String& s2);

/**
 * Prevent careless programmers from overwriting the string
 * @see TelEngine::String::operator=
 */
inline const char *strcpy(String& dest, const char* src)
    { dest = src; return dest.c_str(); }

/**
 * Prevent careless programmers from overwriting the string
 * @see TelEngine::String::operator+=
 */
inline const char *strcat(String& dest, const char* src)
    { dest += src; return dest.c_str(); }

/**
 * Utility function to look up a string in a token table,
 * interpret as number if it fails
 * @param str String to look up
 * @param tokens Pointer to the token table
 * @param defvalue Value to return if lookup and conversion fail
 * @param base Default base to use to convert to number
 */
YATE_API int lookup(const char* str, const TokenDict* tokens, int defvalue = 0, int base = 0);

/**
 * Utility function to look up a number in a token table
 * @param value Value to search for
 * @param tokens Pointer to the token table
 * @param defvalue Value to return if lookup fails
 */
YATE_API const char* lookup(int value, const TokenDict* tokens, const char* defvalue = 0);

class NamedList;

/**
 * Utility method to return from a chan.control handler
 * @param params The parameters list
 * @param ret The return value
 * @param retVal The error message
 * @return ret if the message was not generated from rmanager.
 */
YATE_API bool controlReturn(NamedList* params, bool ret, const char* retVal = 0);

/**
 * A regular expression matching class.
 * @short A regexp matching class
 */
class YATE_API Regexp : public String
{
    friend class String;
public:
    /**
     * Creates a new, empty regexp.
     */
    Regexp();

    /**
     * Creates a new initialized regexp.
     * @param value Initial value of the regexp.
     * @param extended True to use POSIX Extended Regular Expression syntax
     * @param insensitive True to not differentiate case
     */
    explicit Regexp(const char* value, bool extended = false, bool insensitive = false);

    /**
     * Copy constructor.
     * @param value Initial value of the regexp.
     */
    Regexp(const Regexp& value);

    /**
     * Destroys the regexp, disposes the memory.
     */
    virtual ~Regexp();

    /**
     * Assignment from char* operator.
     */
    inline Regexp& operator=(const char* value)
	{ String::operator=(value); return *this; }

    /**
     * Makes sure the regular expression is compiled
     * @return True if successfully compiled, false on error
     */
    inline bool compile() const
	{ return m_regexp || (m_compile && doCompile()); }

    /**
     * Checks if the pattern matches a given value
     * @param value String to check for match
     * @return True if matches, false otherwise
     */
    bool matches(const char* value) const;

    /**
     * Checks if the pattern matches a string
     * @param value String to check for match
     * @return True if matches, false otherwise
     */
    virtual bool matches(const String& value) const
	{ return Regexp::matches(value.safe()); }

    /**
     * Change the expression matching flags
     * @param extended True to use POSIX Extended Regular Expression syntax
     * @param insensitive True to not differentiate case
     */
    void setFlags(bool extended, bool insensitive);

    /**
     * Return the POSIX Extended syntax flag
     * @return True if using POSIX Extended Regular Expression syntax
     */
    bool isExtended() const;

    /**
     * Return the Case Insensitive flag
     * @return True if not differentiating case
     */
    bool isCaseInsensitive() const;

protected:
    /**
     * Called whenever the value changed (except in constructors) to recompile.
     */
    virtual void changed();

    /**
     * Compile the regular expression
     * @return True if successfully compiled, false on error
     */
    bool doCompile() const;

private:
    void cleanup();
    bool matches(const char* value, StringMatchPrivate* matchlist) const;
    mutable void* m_regexp;
    mutable bool m_compile;
    int m_flags;
};

/**
 * Indirected shared string offering access to atom strings
 * @short Atom string holder
 */
class Atom
{
public:
    /**
     * Constructor
     * @param value Atom's string value
     */
    inline explicit Atom(const char* value)
	: m_atom(0)
	{ String::atom(m_atom,value); }

    /**
     * Conversion to "const String &" operator
     * @return Pointer to the atom String
     */
    inline operator const String&() const
	{ return *m_atom; }

    /**
     * String method call operator
     * @return Pointer to the atom String
     */
    inline const String* operator->() const
	{ return m_atom; }

private:
    const String* m_atom;
};

/**
 * Holder for an event (output, debug or alarm) message
 * @short A captured event string with a debug level
 */
class YATE_API CapturedEvent : public String
{
    friend class Engine;
    YCLASS(CapturedEvent,String)
public:
    /**
     * Constructor
     * @param level Debugging level associated with the event
     * @param text Text description of the event
     */
    inline CapturedEvent(int level, const char* text)
	: String(text), m_level(level)
	{ }

    /**
     * Copy constructor
     * @param original Captured event to copy
     */
    inline CapturedEvent(const CapturedEvent& original)
	: String(original), m_level(original.level())
	{ }

    /**
     * Get the debugging level of the event
     * @return Debugging level associated with the event
     */
    inline int level() const
	{ return m_level; }


    /**
     * Get the capturing state of the output and debug messages
     * @return True if output and debug messages are being captured
     */
    inline static bool capturing()
	{ return s_capturing; }

    /**
     * Get the list of captured events
     * @return List of events captured from output and debugging
     */
    inline static const ObjList& events()
	{ return s_events; }

    /**
     * Add an event to the captured events list
     * @param level Debugging level associated with the event
     * @param text Text description of the event, must not be empty
     */
    inline static void append(int level, const char* text)
	{ if (text && *text) s_events.append(new CapturedEvent(level,text)); }

protected:
    /**
     * Get a writable list of captured events
     * @return List of events captured from output and debugging
     */
    inline static ObjList& eventsRw()
	{ return s_events; }

    /**
     * Enable or disable capturing of output and debug messages
     * @param capture True to capture internally the debugging messages
     */
    inline static void capturing(bool capture)
	{ s_capturing = capture; }

private:
    int m_level;
    static ObjList s_events;
    static bool s_capturing;
};

/**
 * A string class with a hashed string name
 * @short A named string class.
 */
class YATE_API NamedString : public String
{
    YNOCOPY(NamedString); // no automatic copies please
public:
    /**
     * Creates a new named string.
     * @param name Name of this string
     * @param value Initial value of the string
     */
    explicit NamedString(const char* name, const char* value = 0);

    /**
     * Retrieve the name of this string.
     * @return A hashed string with the name of the string
     */
    inline const String& name() const
	{ return m_name; }

    /**
     * Get a string representation of this object
     * @return A reference to the name of this object
     */
    virtual const String& toString() const;

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Value assignment operator
     */
    inline NamedString& operator=(const char* value)
	{ String::operator=(value); return *this; }

private:
    NamedString(); // no default constructor please
    String m_name;
};

/**
 * A named string holding a pointer to arbitrary data.
 * The pointer is owned by the object: it will be released when the object is
 *  destroyed or the string value changed
 * @short A named pointer class.
 */
class YATE_API NamedPointer : public NamedString
{
public:
    /**
     * Creates a new named pointer
     * @param name Name of this pointer
     * @param data Initial pointer value. The pointer will be owned by this object
     * @param value Initial string value
     */
    explicit NamedPointer(const char* name, GenObject* data = 0, const char* value = 0);

    /**
     * Destructor. Release the pointer
     */
    virtual ~NamedPointer();

    /**
     * Retrieve the pointer carried by this object
     * @return Pointer to arbitrary user GenObject
     */
    inline GenObject* userData() const
	{ return m_data; }

    /**
     * Retrieve the pointer carried by this object and release ownership.
     * The caller will own the returned pointer
     * @return Pointer to arbitrary user GenObject
     */
    GenObject* takeData();

    /**
     * Set obscure data carried by this object.
     * Note that a RefObject's reference counter should be increased before adding it to this named pointer
     * @param data Pointer to arbitrary user data
     */
    void userData(GenObject* data);

    /**
     * Get a pointer to a derived class of user data given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if user object id NULL or doesn't implement it
     */
    inline void* userObject(const String& name) const
	{ return m_data ? m_data->getObject(name) : 0; }

    /**
     * String value assignment operator
     */
    inline NamedPointer& operator=(const char* value)
	{ NamedString::operator=(value); return *this; }

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

protected:
    /**
     * Called whenever the string value changed. Release the pointer
     */
    virtual void changed();

private:
    NamedPointer(); // no default constructor please
    GenObject* m_data;
};

/**
 * An atomic counter with an associated name
 * @short Atomic counter with name
 */
class YATE_API NamedCounter : public String
{
    YNOCOPY(NamedCounter); // no automatic copies please
public:
    /**
     * Constructor
     * @param name Name of the counter
     */
    explicit NamedCounter(const String& name);

    /**
     * Check if the counter is enabled
     * @return True if the counter is enabled
     */
    inline bool enabled() const
	{ return m_enabled; }

    /**
     * Enable or disable the counter
     * @param val True to enable counter, false to disable
     */
    inline void enable(bool val)
	{ m_enabled = val; }

    /**
     * Increment the counter
     * @return Post-increment value of the counter
     */
    int inc();

    /**
     * Decrement the counter
     * @return Post-decrement value of the counter
     */
    int dec();

    /**
     * Get the current value of the counter
     * @return Value of the counter
     */
    inline int count() const
	{ return m_count; }

private:
    int m_count;
    bool m_enabled;
    Mutex* m_mutex;
};

/**
 * A hashed object list handling class. Objects placed in the list are
 *  distributed according to their String hash resulting in faster searches.
 * On the other hand an object placed in a hashed list must never change
 *  its String value or it becomes unfindable.
 * @short A hashed object list class
 */
class YATE_API HashList : public GenObject
{
    YNOCOPY(HashList); // no automatic copies please
public:
    /**
     * Creates a new, empty list.
     * @param size Number of classes to divide the objects
     */
    explicit HashList(unsigned int size = 17);

    /**
     * Destroys the list and everything in it.
     */
    virtual ~HashList();

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Get the number of hash entries
     * @return Count of hash entries
     */
    inline unsigned int length() const
	{ return m_size; }

    /**
     * Get the number of non-null objects in the list
     * @return Count of items
     */
    unsigned int count() const;

    /**
     * Retrieve one of the internal object lists. This method should be used
     *  only to iterate all objects in the list.
     * @param index Index of the internal list to retrieve
     * @return Pointer to the list or NULL
     */
    inline ObjList* getList(unsigned int index) const
	{ return (index < m_size) ? m_lists[index] : 0; }

    /**
     * Retrieve one of the internal object lists knowing the hash value.
     * @param hash Hash of the internal list to retrieve
     * @return Pointer to the list or NULL if never filled
     */
    inline ObjList* getHashList(unsigned int hash) const
	{ return getList(hash % m_size); }

    /**
     * Retrieve one of the internal object lists knowing the String value.
     * @param str String whose hash internal list is to retrieve
     * @return Pointer to the list or NULL if never filled
     */
    inline ObjList* getHashList(const String& str) const
	{ return getHashList(str.hash()); }

    /**
     * Array-like indexing operator
     * @param str String value of the object to locate
     * @return Pointer to the first object or NULL
     */
    GenObject* operator[](const String& str) const;

    /**
     * Get the item in the list that holds an object.
     * The item is searched sequentially in the lists, not using it's String hash
     * @param obj Pointer to the object to search for
     * @return Pointer to the found item or NULL
     */
    ObjList* find(const GenObject* obj) const;

    /**
     * Get the item in the list that holds an object
     * @param obj Pointer to the object to search for
     * @param hash Object hash used to identify the list it belongs to
     * @return Pointer to the found item or NULL
     */
    ObjList* find(const GenObject* obj, unsigned int hash) const;

    /**
     * Get the item in the list that holds an object by String value
     * @param str String value (toString) of the object to search for
     * @return Pointer to the first found item or NULL
     */
    ObjList* find(const String& str) const;

    /**
     * Appends an object to the hashed list
     * @param obj Pointer to the object to append
     * @return A pointer to the inserted list item
     */
    ObjList* append(const GenObject* obj);

    /**
     * Delete the list item that holds a given object
     * @param obj Object to search in the list
     * @param delobj True to delete the object (default)
     * @param useHash True to use object hash to identify the list it belongs to
     * @return Pointer to the object if not destroyed
     */
    GenObject* remove(GenObject* obj, bool delobj = true, bool useHash = false);

    /**
     * Delete the item in the list that holds an object by String value
     * @param str String value (toString) of the object to remove
     * @param delobj True to delete the object (default)
     * @return Pointer to the object if not destroyed
     */
    inline GenObject* remove(const String& str, bool delobj = true)
    {
	ObjList* n = find(str);
	return n ? n->remove(delobj) : 0;
    }

    /**
     * Clear the list and optionally delete all contained objects
     */
    void clear();

    /**
     * Resync the list by checking if a stored object belongs to the list
     *  according to its hash
     * @param obj Object to resync in the list
     * @return True if object was in the wrong list and had to be moved
     */
    bool resync(GenObject* obj);

    /**
     * Resync the list by checking if all stored objects belong to the list
     *  according to their hash
     * @return True if at least one object had to be moved
     */
    bool resync();

private:
    unsigned int m_size;
    ObjList** m_lists;
};

/**
 * An ObjList or HashList iterator that can be used even when list elements
 * are changed while iterating. Note that it will not detect that an item was
 * removed and another with the same address was inserted back in list.
 * @short Class used to iterate the items of a list
 */
class YATE_API ListIterator
{
    YNOCOPY(ListIterator); // no automatic copies please
public:
    /**
     * Constructor used to iterate through an ObjList.
     * The image of the list is frozen at the time the constructor executes
     * @param list List to get the objects from
     * @param offset First list element to iterate, will wrap around
     */
    ListIterator(ObjList& list, int offset = 0);

    /**
     * Constructor used to iterate through a HashList.
     * The image of the list is frozen at the time the constructor executes
     * @param list List to get the objects from
     * @param offset First list element to iterate, will wrap around
     */
    ListIterator(HashList& list, int offset = 0);

    /**
     * Destructor - frees the allocated memory
     */
    ~ListIterator();

    /**
     * Get the number of elements in the list
     * @return Count of items in the internal list
     */
    inline unsigned int length() const
	{ return m_length; }

    /**
     * Clear the iterator, disconnect from any list
     */
    void clear();

    /**
     * Assign an ObjList to the iterator, build a frozen image of the list
     * @param list List to get the objects from
     * @param offset First list element to iterate, will wrap around
     */
    void assign(ObjList& list, int offset = 0);

    /**
     * Assign a HashList to the iterator, build a frozen image of the list
     * @param list List to get the objects from
     * @param offset First list element to iterate, will wrap around
     */
    void assign(HashList& list, int offset = 0);

    /**
     * Get an arbitrary element in the iterator's list image.
     * Items that were removed from list or are not alive are not returned.
     * @param index Position to get the item from
     * @return Pointer to the list item or NULL if out of range or item removed
     */
    GenObject* get(unsigned int index) const;

    /**
     * Get the current element and advance the current index.
     * Items that were removed from list or are not alive are skipped over.
     * An example of typical usage:
     * <pre>
     * ListIterator iter(list);
     * while (GenObject* obj = iter.get()) {
     *     do_something_with(obj);
     * }
     * </pre>
     * @return Pointer to a list item or NULL if advanced past end (eof)
     */
    GenObject* get();

    /**
     * Check if the current pointer is past the end of the list
     * @return True if there are no more entries left
     */
    inline bool eof() const
	{ return m_current >= m_length; }

    /**
     * Reset the iterator index to the first position in the list
     */
    inline void reset()
	{ m_current = 0; }

private:
    ObjList* m_objList;
    HashList* m_hashList;
    GenObject** m_objects;
    unsigned int* m_hashes;
    unsigned int m_length;
    unsigned int m_current;
};

/**
 * The Time class holds a time moment with microsecond accuracy
 * @short A time holding class
 */
class YATE_API Time
{
public:
    /**
     * Constructs a Time object from the current time
     */
    inline Time()
	: m_time(now())
	{ }

    /**
     * Constructs a Time object from a given time
     * @param usec Time in microseconds
     */
    inline Time(u_int64_t usec)
	: m_time(usec)
	{ }

    /**
     * Constructs a Time object from a timeval structure pointer
     * @param tv Pointer to the timeval structure
     */
    inline explicit Time(const struct timeval* tv)
	: m_time(fromTimeval(tv))
	{ }

    /**
     * Constructs a Time object from a timeval structure
     * @param tv Reference of the timeval structure
     */
    inline explicit Time(const struct timeval& tv)
	: m_time(fromTimeval(tv))
	{ }

    /**
     * Do-nothing destructor that keeps the compiler from complaining
     *  about inlining derivates or members of Time type
     */
    inline ~Time()
	{ }

    /**
     * Get time in seconds
     * @return Time in seconds since the Epoch
     */
    inline u_int32_t sec() const
	{ return (u_int32_t)((m_time+500000) / 1000000); }

    /**
     * Get time in milliseconds
     * @return Time in milliseconds since the Epoch
     */
    inline u_int64_t msec() const
	{ return (m_time+500) / 1000; }

    /**
     * Get time in microseconds
     * @return Time in microseconds since the Epoch
     */
    inline u_int64_t usec() const
	{ return m_time; }

    /**
     * Conversion to microseconds operator
     */
    inline operator u_int64_t() const
	{ return m_time; }

    /**
     * Assignment operator.
     */
    inline Time& operator=(u_int64_t usec)
	{ m_time = usec; return *this; }

    /**
     * Offsetting operator.
     */
    inline Time& operator+=(int64_t delta)
	{ m_time += delta; return *this; }

    /**
     * Offsetting operator.
     */
    inline Time& operator-=(int64_t delta)
	{ m_time -= delta; return *this; }

    /**
     * Fill in a timeval struct from a value in microseconds
     * @param tv Pointer to the timeval structure
     */
    inline void toTimeval(struct timeval* tv) const
	{ toTimeval(tv, m_time); }

    /**
     * Fill in a timeval struct from a value in microseconds
     * @param tv Pointer to the timeval structure
     * @param usec Time to convert to timeval
     */
    static void toTimeval(struct timeval* tv, u_int64_t usec);

    /**
     * Convert time in a timeval struct to microseconds
     * @param tv Pointer to the timeval structure
     * @return Corresponding time in microseconds or zero if tv is NULL
     */
    static u_int64_t fromTimeval(const struct timeval* tv);

    /**
     * Convert time in a timeval struct to microseconds
     * @param tv Reference of the timeval structure
     * @return Corresponding time in microseconds
     */
    inline static u_int64_t fromTimeval(const struct timeval& tv)
	{ return fromTimeval(&tv); }

    /**
     * Get the current system time in microseconds
     * @return Time in microseconds since the Epoch
     */
    static u_int64_t now();

    /**
     * Get the current system time in milliseconds
     * @return Time in milliseconds since the Epoch
     */
    static u_int64_t msecNow();

    /**
     * Get the current system time in seconds
     * @return Time in seconds since the Epoch
     */
    static u_int32_t secNow();

    /**
     * Build EPOCH time from date/time components
     * @param year The year component of the date. Must be greater then 1969
     * @param month The month component of the date (1 to 12)
     * @param day The day component of the date (1 to 31)
     * @param hour The hour component of the time (0 to 23). The hour can be 24
     *  if minute and sec are 0
     * @param minute The minute component of the time (0 to 59)
     * @param sec The seconds component of the time (0 to 59)
     * @param offset Optional number of seconds to be added/substracted
     *  to/from result. It can't exceed the number of seconds in a day
     * @return EPOCH time in seconds, -1 on failure
     */
    static unsigned int toEpoch(int year, unsigned int month, unsigned int day,
	unsigned int hour, unsigned int minute, unsigned int sec, int offset = 0);

    /**
     * Split a given EPOCH time into its date/time components
     * @param epochTimeSec EPOCH time in seconds
     * @param year The year component of the date
     * @param month The month component of the date (1 to 12)
     * @param day The day component of the date (1 to 31)
     * @param hour The hour component of the time (0 to 23)
     * @param minute The minute component of the time (0 to 59)
     * @param sec The seconds component of the time (0 to 59)
     * @param wDay The day of the week (optional)
     * @return True on succes, false if conversion failed
     */
    static bool toDateTime(unsigned int epochTimeSec, int& year, unsigned int& month,
	unsigned int& day, unsigned int& hour, unsigned int& minute, unsigned int& sec,
	unsigned int* wDay = 0);

    /**
     * Check if an year is a leap one
     * @param year The year to check
     * @return True if the given year is a leap one
     */
    static inline bool isLeap(unsigned int year)
	{ return (year % 400 == 0 || (year % 4 == 0 && year % 100 != 0)); }

    /**
     * Retrieve the difference between local time and UTC in seconds east of UTC
     * @return Difference between local time and UTC in seconds
     */
    static int timeZone();

private:
    u_int64_t m_time;
};

/**
 * Implementation of a system independent pseudo random number generator
 * @short Pseudo random number generator
 */
class YATE_API Random
{
public:
    /**
     * Constructor
     * @param seed Number to use as initial sequence seed
     */
    inline Random(u_int32_t seed = Time::now() & 0xffffffff)
	: m_random(seed)
	{ }

    /**
     * Get the latest random number generated
     * @return Last random number generated
     */
    inline u_int32_t get() const
	{ return m_random; }

    /**
     * Set the pseudo random generator to a known state
     * @param seed Number to set as current state
     */
    inline void set(u_int32_t seed)
	{ m_random = seed; }

    /**
     * Advance the pseudo random sequence and return new value
     * @return Next random number in sequence
     */
    u_int32_t next();

    /**
     * Thread safe (and shared) replacement for library ::random()
     * @return Next random number in the global sequence
     */
    static long int random();

    /**
     * Thread safe (and shared) replacement for library ::srandom()
     * @param seed Number to set as seed in the global sequence
     */
    static void srandom(unsigned int seed);

private:
    u_int32_t m_random;
};

/**
 * The DataBlock holds a data buffer with no specific formatting.
 * @short A class that holds just a block of raw data
 */
class YATE_API DataBlock : public GenObject
{
public:

    /**
     * Constructs an empty data block
     * @param overAlloc How many bytes of memory to overallocate
     */
    DataBlock(unsigned int overAlloc = 0);

    /**
     * Copy constructor
     * @param value Data block to copy from
     */
    DataBlock(const DataBlock& value);

    /**
     * Copy constructor with overallocation
     * @param value Data block to copy from
     * @param overAlloc How many bytes of memory to overallocate
     */
    DataBlock(const DataBlock& value, unsigned int overAlloc);

    /**
     * Constructs an initialized data block
     * @param value Data to assign, may be NULL to fill with zeros
     * @param len Length of data, may be zero (then value is ignored)
     * @param copyData True to make a copy of the data, false to just insert the pointer
     * @param overAlloc How many bytes of memory to overallocate
     */
    DataBlock(void* value, unsigned int len, bool copyData = true, unsigned int overAlloc = 0);

    /**
     * Destroys the data, disposes the memory.
     */
    virtual ~DataBlock();

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * A static empty data block
     */
    static const DataBlock& empty();

    /**
     * Get a pointer to the stored data.
     * @return A pointer to the data or NULL.
     */
    inline void* data() const
	{ return m_data; }

    /**
     * Get a pointer to a byte range inside the stored data.
     * @param offs Byte offset inside the stored data
     * @param len Number of bytes that must be valid starting at offset
     * @return A pointer to the data or NULL if the range is not available.
     */
    inline unsigned char* data(unsigned int offs, unsigned int len = 1) const
	{ return (offs + len <= m_length) ? (static_cast<unsigned char*>(m_data) + offs) : 0; }

    /**
     * Get the value of a single byte inside the stored data
     * @param offs Byte offset inside the stored data
     * @param defvalue Default value to return if offset is outside data
     * @return Byte value at offset (0-255) or defvalue if offset outside data
     */
    inline int at(unsigned int offs, int defvalue = -1) const
	{ return (offs < m_length) ? static_cast<unsigned char*>(m_data)[offs] : defvalue; }

    /**
     * Checks if the block holds a NULL pointer.
     * @return True if the block holds NULL, false otherwise.
     */
    inline bool null() const
	{ return !m_data; }

    /**
     * Get the length of the stored data.
     * @return The length of the stored data, zero for NULL.
     */
    inline unsigned int length() const
	{ return m_length; }

    /**
     * Get the memory overallocation setting.
     * @return Amount of memory that will be overallocated.
     */
    inline unsigned int overAlloc() const
	{ return m_overAlloc; }

    /**
     * Set the memory overallocation.
     * @param bytes How many bytes of memory to overallocate
     */
    inline void overAlloc(unsigned int bytes)
	{ m_overAlloc = bytes; }

    /**
     * Clear the data and optionally free the memory
     * @param deleteData True to free the deta block, false to just forget it
     */
    void clear(bool deleteData = true);

    /**
     * Assign data to the object
     * @param value Data to assign, may be NULL to fill with zeros
     * @param len Length of data, may be zero (then value is ignored)
     * @param copyData True to make a copy of the data, false to just insert the pointer
     * @param allocated Real allocated data length in case it should not be copied
     */
    DataBlock& assign(void* value, unsigned int len, bool copyData = true, unsigned int allocated = 0);

    /**
     * Append data to the current block
     * @param value Data to append
     * @param len Length of data
     */
    inline void append(void* value, unsigned int len) {
	    DataBlock tmp(value,len,false);
	    append(tmp);
	    tmp.clear(false);
	}

    /**
     * Append data to the current block
     * @param value Data to append
     */
    void append(const DataBlock& value);

    /**
     * Append a String to the current block
     * @param value String to append
     */
    void append(const String& value);

    /**
     * Insert data before the current block
     * @param value Data to insert
     */
    void insert(const DataBlock& value);

    /**
     * Resize (re-alloc or free) this block if required size is not the same as the current one
     * @param len Required block size
     */
    inline void resize(unsigned int len) {
	    if (len != length())
		assign(0,len);
	}

    /**
     * Truncate the data block
     * @param len The maximum length to keep
     */
    void truncate(unsigned int len);

    /**
     * Cut off a number of bytes from the data block
     * @param len Amount to cut, positive to cut from end, negative to cut from start of block
     */
    void cut(int len);

    /**
     * Byte indexing operator with signed parameter
     * @param index Index of the byte to retrieve
     * @return Byte value at offset (0-255) or -1 if index outside data
     */
    inline int operator[](signed int index) const
	{ return at(index); }

    /**
     * Byte indexing operator with unsigned parameter
     * @param index Index of the byte to retrieve
     * @return Byte value at offset (0-255) or -1 if index outside data
     */
    inline int operator[](unsigned int index) const
	{ return at(index); }

    /**
     * Assignment operator.
     */
    DataBlock& operator=(const DataBlock& value);

    /**
     * Appending operator.
     */
    inline DataBlock& operator+=(const DataBlock& value)
	{ append(value); return *this; }

    /**
     * Appending operator for Strings.
     */
    inline DataBlock& operator+=(const String& value)
	{ append(value); return *this; }

    /**
     * Convert data from a different format
     * @param src Source data block
     * @param sFormat Name of the source format
     * @param dFormat Name of the destination format
     * @param maxlen Maximum amount to convert, 0 to use source
     * @return True if converted successfully, false on failure
     */
    bool convert(const DataBlock& src, const String& sFormat,
	const String& dFormat, unsigned maxlen = 0);

    /**
     * Build this data block from a hexadecimal string representation.
     * Each octet must be represented in the input string with 2 hexadecimal characters.
     * If a separator is specified, the octets in input string must be separated using
     *  exactly 1 separator. Only 1 leading or 1 trailing separators are allowed.
     * @param data Input character string
     * @param len Length of the input string
     * @param sep Separator character used between octets. 0 if no separator is expected
     * @return True if the input string was succesfully parsed, false otherwise
     */
    bool unHexify(const char* data, unsigned int len, char sep);

    /**
     * Build this data block from a hexadecimal string representation.
     * Each octet must be represented in the input string with 2 hexadecimal characters.
     * This method guesses if separators are used. If so the octets in input string must be
     *  separated using exactly 1 separator. Only 1 leading or 1 trailing separators are allowed.
     * @param data Input character string
     * @param len Length of the input string
     * @return True if the input string was succesfully parsed, false otherwise
     */
    bool unHexify(const char* data, unsigned int len);

    /**
     * Build this data block from a hexadecimal string representation.
     * This version parses a String and guesses separators presence.
     * @param data Input character string
     * @return True if the input string was succesfully parsed, false otherwise
     */
    inline bool unHexify(const String& data)
	{ return unHexify(data.c_str(),data.length()); }

    /**
     * Create an escaped string suitable for use in SQL queries
     * @param extraEsc Character to escape other than the default ones
     * @return A string with binary zeros and other special characters escaped
     */
    String sqlEscape(char extraEsc) const;

private:
    unsigned int allocLen(unsigned int len) const;
    void* m_data;
    unsigned int m_length;
    unsigned int m_allocated;
    unsigned int m_overAlloc;
};

/**
 * Abstract base class representing a hash calculator
 * @short An abstract hashing class
 */
class YATE_API Hasher
{
public:
    /**
     * Destroy the instance, free allocated memory
     */
    virtual ~Hasher();

    /**
     * Clear the digest and prepare for reuse
     */
    virtual void clear() = 0;

    /**
     * Finalize the digest computation, make result ready.
     * Subsequent calls to @ref update() will fail
     */
    virtual void finalize() = 0;

    /**
     * Returns a pointer to the raw 16-byte binary value of the message digest.
     * The digest is finalized if if wasn't already
     * @return Pointer to the raw digest data or NULL if some error occured
     */
    virtual const unsigned char* rawDigest() = 0;

    /**
     * Returns the standard hexadecimal representation of the message digest.
     * The digest is finalized if if wasn't already
     * @return A String which holds the hex digest or a null one if some error occured
     */
    inline const String& hexDigest()
	{ finalize(); return m_hex; }

    /**
     * Update the digest from a buffer of data
     * @param buf Pointer to the data to be included in digest
     * @param len Length of data in the buffer
     * @return True if success, false if @ref finalize() was already called
     */
    inline bool update(const void* buf, unsigned int len)
	{ return updateInternal(buf,len); }

    /**
     * Update the digest from the content of a DataBlock
     * @param data Data to be included in digest
     * @return True if success, false if @ref finalize() was already called
     */
    inline bool update(const DataBlock& data)
	{ return updateInternal(data.data(), data.length()); }

    /**
     * Update the digest from the content of a String
     * @param str String to be included in digest
     * @return True if success, false if @ref finalize() was already called
     */
    inline bool update(const String& str)
	{ return updateInternal(str.c_str(), str.length()); }

    /**
     * Digest updating operator for Strings
     * @param value String to be included in digest
     */
    inline Hasher& operator<<(const String& value)
	{ update(value); return *this; }

    /**
     * Digest updating operator for DataBlocks
     * @param data Data to be included in digest
     */
    inline Hasher& operator<<(const DataBlock& data)
	{ update(data); return *this; }

    /**
     * Digest updating operator for C strings
     * @param value String to be included in digest
     */
    Hasher& operator<<(const char* value);

    /**
     * Start a HMAC calculation, initialize the hash and the outer pad
     * @param opad Outer pad to be filled from key
     * @param key Secret key
     * @param keyLen Secret key length
     * @return True if hash and outer pad were successfully initialized
     */
    bool hmacStart(DataBlock& opad, const void* key, unsigned int keyLen);

    /**
     * Start a HMAC calculation, initialize the hash and the outer pad
     * @param opad Outer pad to be filled from key
     * @param key Secret key
     * @return True if hash and outer pad were successfully initialized
     */
    inline bool hmacStart(DataBlock& opad, const DataBlock& key)
	{ return hmacStart(opad,key.data(),key.length()); }

    /**
     * Start a HMAC calculation, initialize the hash and the outer pad
     * @param opad Outer pad to be filled from key
     * @param key Secret key string
     * @return True if hash and outer pad were successfully initialized
     */
    inline bool hmacStart(DataBlock& opad, const String& key)
	{ return hmacStart(opad,key.c_str(),key.length()); }

    /**
     * Finalize a HMAC calculation with this hash
     * @param opad Outer pad as filled by hmacStart
     * @return True on success, HMAC result is left in hasher
     */
    bool hmacFinal(const DataBlock& opad);

    /**
     * Compute a Message Authentication Code with this hash
     * @param key Secret key
     * @param keyLen Secret key length
     * @param msg Message to authenticate
     * @param msgLen Message length
     * @return True if HMAC was computed correctly, result is left in hasher
     */
    bool hmac(const void* key, unsigned int keyLen, const void* msg, unsigned int msgLen);

    /**
     * Compute a Message Authentication Code with this hash
     * @param key Secret key
     * @param msg Message to authenticate
     * @return True if HMAC was computed correctly, result is left in hasher
     */
    inline bool hmac(const DataBlock& key, const DataBlock& msg)
	{ return hmac(key.data(),key.length(),msg.data(),msg.length()); }

    /**
     * Compute a Message Authentication Code with this hash
     * @param key Secret key string
     * @param msg Message string to authenticate
     * @return True if HMAC was computed correctly, result is left in hasher
     */
    inline bool hmac(const String& key, const String& msg)
	{ return hmac(key.c_str(),key.length(),msg.c_str(),msg.length()); }

    /**
     * Return the length of the raw binary digest
     * @return Length of the digest in octets
     */
    virtual unsigned int hashLength() const = 0;

    /**
     * Return the size of the block used in HMAC calculations
     * @return HMAC block size in octets, usually 64
     */
    virtual unsigned int hmacBlockSize() const;

protected:
    /**
     * Default constructor
     */
    inline Hasher()
	: m_private(0)
	{ }

    /**
     * Update the digest from a buffer of data
     * @param buf Pointer to the data to be included in digest
     * @param len Length of data in the buffer
     * @return True if success, false if @ref finalize() was already called
     */
    virtual bool updateInternal(const void* buf, unsigned int len) = 0;

    void* m_private;
    String m_hex;
};

/**
 * A class to compute and check MD5 digests
 * @short A standard MD5 digest calculator
 */
class YATE_API MD5 : public Hasher
{
public:
    /**
     * Construct a fresh initialized instance
     */
    MD5();

    /**
     * Copy constructor
     * @param original MD5 instance to copy
     */
    MD5(const MD5& original);

    /**
     * Construct a digest from a buffer of data
     * @param buf Pointer to the data to be included in digest
     * @param len Length of data in the buffer
     */
    MD5(const void* buf, unsigned int len);

    /**
     * Construct a digest from a binary DataBlock
     * @param data Binary data to be included in digest
     */
    MD5(const DataBlock& data);

    /**
     * Construct a digest from a String
     * @param str String to be included in digest
     */
    MD5(const String& str);

    /**
     * Assignment operator.
     */
    MD5& operator=(const MD5& original);

    /**
     * Destroy the instance, free allocated memory
     */
    virtual ~MD5();

    /**
     * Clear the digest and prepare for reuse
     */
    virtual void clear();

    /**
     * Finalize the digest computation, make result ready.
     * Subsequent calls to @ref update() will fail
     */
    virtual void finalize();

    /**
     * Returns a pointer to the raw 16-byte binary value of the message digest.
     * The digest is finalized if if wasn't already
     * @return Pointer to the raw digest data or NULL if some error occured
     */
    virtual const unsigned char* rawDigest();

    /**
     * Return the length of the raw binary digest
     * @return Constant value of 16
     */
    inline static unsigned int rawLength()
	{ return 16; }

    /**
     * Return the length of the raw binary digest
     * @return Length of the digest in octets
     */
    virtual unsigned int hashLength() const
	{ return 16; }

protected:
    bool updateInternal(const void* buf, unsigned int len);

private:
    void init();
    unsigned char m_bin[16];
};

/**
 * A class to compute and check SHA1 digests
 * @short A standard SHA1 digest calculator
 */
class YATE_API SHA1 : public Hasher
{
public:
    /**
     * Construct a fresh initialized instance
     */
    SHA1();

    /**
     * Copy constructor
     * @param original SHA1 instance to copy
     */
    SHA1(const SHA1& original);

    /**
     * Construct a digest from a buffer of data
     * @param buf Pointer to the data to be included in digest
     * @param len Length of data in the buffer
     */
    SHA1(const void* buf, unsigned int len);

    /**
     * Construct a digest from a binary DataBlock
     * @param data Binary data to be included in digest
     */
    SHA1(const DataBlock& data);

    /**
     * Construct a digest from a String
     * @param str String to be included in digest
     */
    SHA1(const String& str);

    /**
     * Assignment operator.
     */
    SHA1& operator=(const SHA1& original);

    /**
     * Destroy the instance, free allocated memory
     */
    virtual ~SHA1();

    /**
     * Clear the digest and prepare for reuse
     */
    virtual void clear();

    /**
     * Finalize the digest computation, make result ready.
     * Subsequent calls to @ref update() will fail
     */
    virtual void finalize();

    /**
     * Returns a pointer to the raw 20-byte binary value of the message digest.
     * The digest is finalized if if wasn't already
     * @return Pointer to the raw digest data or NULL if some error occured
     */
    virtual const unsigned char* rawDigest();

    /**
     * Return the length of the raw binary digest
     * @return Constant value of 20
     */
    inline static unsigned int rawLength()
	{ return 20; }

    /**
     * Return the length of the raw binary digest
     * @return Length of the digest in octets
     */
    virtual unsigned int hashLength() const
	{ return 20; }

    /**
     * NIST FIPS 186-2 change notice 1 Pseudo Random Function.
     * Uses a b=160 bits SHA1 based G(t,c) function with no XSEEDj
     * @param out Block to fill with pseudo-random data
     * @param seed Data to use as RNG seed, must be 1 to 64 octets long
     * @param len Desired output length in octets, must be 1 to 512
     * @return True on success, false on invalid lengths
     */
    static bool fips186prf(DataBlock& out, const DataBlock& seed, unsigned int len);

protected:
    bool updateInternal(const void* buf, unsigned int len);

private:
    void init();
    unsigned char m_bin[20];
};

/**
 * A class to compute and check SHA256 digests
 * @short A standard SHA256 digest calculator
 */
class YATE_API SHA256 : public Hasher
{
public:
    /**
     * Construct a fresh initialized instance
     */
    SHA256();

    /**
     * Copy constructor
     * @param original SHA256 instance to copy
     */
    SHA256(const SHA256& original);

    /**
     * Construct a digest from a buffer of data
     * @param buf Pointer to the data to be included in digest
     * @param len Length of data in the buffer
     */
    SHA256(const void* buf, unsigned int len);

    /**
     * Construct a digest from a binary DataBlock
     * @param data Binary data to be included in digest
     */
    SHA256(const DataBlock& data);

    /**
     * Construct a digest from a String
     * @param str String to be included in digest
     */
    SHA256(const String& str);

    /**
     * Assignment operator.
     */
    SHA256& operator=(const SHA256& original);

    /**
     * Destroy the instance, free allocated memory
     */
    virtual ~SHA256();

    /**
     * Clear the digest and prepare for reuse
     */
    virtual void clear();

    /**
     * Finalize the digest computation, make result ready.
     * Subsequent calls to @ref update() will fail
     */
    virtual void finalize();

    /**
     * Returns a pointer to the raw 32-byte binary value of the message digest.
     * The digest is finalized if if wasn't already
     * @return Pointer to the raw digest data or NULL if some error occured
     */
    virtual const unsigned char* rawDigest();

    /**
     * Return the length of the raw binary digest
     * @return Constant value of 32
     */
    inline static unsigned int rawLength()
	{ return 32; }

    /**
     * Return the length of the raw binary digest
     * @return Length of the digest in octets
     */
    virtual unsigned int hashLength() const
	{ return 32; }

protected:
    bool updateInternal(const void* buf, unsigned int len);

private:
    void init();
    unsigned char m_bin[32];
};

/**
 * Base64 encoder/decoder class
 * @short Base64 encoder/decoder class
 */
class YATE_API Base64 : public DataBlock
{
    YNOCOPY(Base64); // no automatic copies please
public:
    /**
     * Constructor
     */
    inline Base64()
	{ }

    /**
     * Constructor. Set the buffer
     * @param src Initial data buffer
     * @param len Initial data buffer length
     * @param copyData True to make a copy of the received data
     */
    inline Base64(void* src, unsigned int len, bool copyData = true)
	: DataBlock(src,len,copyData)
	{ }

    /**
     * Encode this buffer to a destination string
     * @param dest Destination string
     * @param lineLen The length of a line. If non 0, a line break (CR/LF) will
     *  be inserted in the encoded data after each lineLine characters.
     *  No line break will be added after the last line. Use the lineAtEnd
     *  parameter to do that
     * @param lineAtEnd True to add a line break at the end of encoded data
     */
    void encode(String& dest, unsigned int lineLen = 0, bool lineAtEnd = false);

    /**
     * Decode this buffer to a destination one
     * @param dest Destination data buffer
     * @param liberal True to use 'liberal' rules when decoding. Some non alphabet
     *  characters (such as CR, LF, TAB, SPACE or the Base64 padding char '=')
     *  will be accepted and ignored. The resulting number of Base64 chars to
     *  decode must be a valid one
     * @return True on succes, false if an invalid (non Base64) character was
     *  found or the number of Base64 characters is invalid (must be a multiple
     *  of 4 plus 0, 2 or 3 characters) or the padding is incorrect
     */
    bool decode(DataBlock& dest, bool liberal = true);

    /**
     * Base64 append operator for Strings
     */
    inline Base64& operator<<(const String& value)
	{ append(value); return *this; }

    /**
     * Base64 append operator for DataBlocks
     */
    inline Base64& operator<<(const DataBlock& data)
	{ append(data); return *this; }

    /**
     * Base64 append operator for C strings
     */
    inline Base64& operator<<(const char* value)
	{ return operator<<(String(value)); }
};

class NamedIterator;

/**
 * This class holds a named list of named strings
 * @short A named string container class
 */
class YATE_API NamedList : public String
{
    friend class NamedIterator;
public:
    /**
     * Creates a new named list.
     * @param name Name of the list - must not be NULL or empty
     */
    explicit NamedList(const char* name);

    /**
     * Copy constructor
     * @param original Named list we are copying
     */
    NamedList(const NamedList& original);

    /**
     * Creates a named list with subparameters of another list.
     * @param name Name of the list - must not be NULL or empty
     * @param original Named list to copy parameters from
     * @param prefix Prefix to match and remove from parameter names
     */
    NamedList(const char* name, const NamedList& original, const String& prefix);

    /**
     * Assignment operator
     * @param value New name and parameters to assign
     * @return Reference to this NamedList
     */
    NamedList& operator=(const NamedList& value);

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Get the number of parameters
     * @return Count of named strings
     */
    inline unsigned int length() const
	{ return m_params.length(); }

    /**
     * Get the number of non-null parameters
     * @return Count of existing named strings
     */
    inline unsigned int count() const
	{ return m_params.count(); }

    /**
     * Clear all parameters
     */
    inline void clearParams()
	{ m_params.clear(); }

    /**
     * Add a named string to the parameter list.
     * @param param Parameter to add
     * @return Reference to this NamedList
     */
    NamedList& addParam(NamedString* param);

    /**
     * Add a named string to the parameter list.
     * @param name Name of the new string
     * @param value Value of the new string
     * @param emptyOK True to always add parameter, false to skip empty values
     * @return Reference to this NamedList
     */
    NamedList& addParam(const char* name, const char* value, bool emptyOK = true);

    /**
     * Set a named string in the parameter list.
     * @param param Parameter to set or add
     * @return Reference to this NamedList
     */
    inline NamedList& setParam(NamedString* param)
    {
	if (param)
	    m_params.setUnique(param);
	return *this;
    }

    /**
     * Set a named string in the parameter list.
     * @param name Name of the string
     * @param value Value of the string
     * @return Reference to this NamedList
     */
    NamedList& setParam(const String& name, const char* value);

    /**
     * Clears all instances of a named string in the parameter list.
     * @param name Name of the string to remove
     * @param childSep If set clears all child parameters in format name+childSep+anything
     * @return Reference to this NamedList
     */
    NamedList& clearParam(const String& name, char childSep = 0);

    /**
     * Remove a specific parameter
     * @param param Pointer to parameter to remove
     * @param delParam True to destroy the parameter
     * @return Reference to this NamedList
     */
    NamedList& clearParam(NamedString* param, bool delParam = true);

    /**
     * Copy a parameter from another NamedList, clears it if not present there
     * @param original NamedList to copy the parameter from
     * @param name Name of the string to copy or clear
     * @param childSep If set copies all child parameters in format name+childSep+anything
     * @return Reference to this NamedList
     */
    NamedList& copyParam(const NamedList& original, const String& name, char childSep = 0);

    /**
     * Copy all parameters from another NamedList, does not clear list first
     * @param original NamedList to copy the parameters from
     * @return Reference to this NamedList
     */
    NamedList& copyParams(const NamedList& original);

    /**
     * Copy multiple parameters from another NamedList, clears them if not present there
     * @param original NamedList to copy the parameters from
     * @param list List of objects (usually String) whose name (blanks stripped) is used as parameters names
     * @param childSep If set copies all child parameters in format name+childSep+anything
     * @return Reference to this NamedList
     */
    NamedList& copyParams(const NamedList& original, ObjList* list, char childSep = 0);

    /**
     * Copy multiple parameters from another NamedList, clears it if not present there
     * @param original NamedList to copy the parameter from
     * @param list Comma separated list of parameters to copy or clear
     * @param childSep If set copies all child parameters in format name+childSep+anything
     * @return Reference to this NamedList
     */
    NamedList& copyParams(const NamedList& original, const String& list, char childSep = 0);

    /**
     * Copy subparameters from another list
     * @param original Named list to copy parameters from
     * @param prefix Prefix to match in parameter names, must not be NULL
     * @param skipPrefix Skip over the prefix when building new parameter name
     * @param replace Set to true to replace list parameter instead of adding a new one
     * @return Reference to this NamedList
     */
    NamedList& copySubParams(const NamedList& original, const String& prefix,
	bool skipPrefix = true, bool replace = false);

    /**
     * Check if we have a parameter that starts with prefix
     * @param prefix Prefix to match in parameter name, must not be NULL
     * @return True if a parameter starts with prefix
     */
    bool hasSubParams(const char* prefix) const;

    /**
     * Get the index of a named string in the parameter list.
     * @param param Pointer to the parameter to locate
     * @return Index of the named string or -1 if not found
     */
    int getIndex(const NamedString* param) const;

    /**
     * Get the index of first matching named string in the parameter list.
     * @param name Name of parameter to locate
     * @return Index of the first matching named string or -1 if not found
     */
    int getIndex(const String& name) const;

    /**
     * Locate a named string in the parameter list.
     * @param name Name of parameter to locate
     * @return A pointer to the named string or NULL.
     */
    NamedString* getParam(const String& name) const;

    /**
     * Locate a named string in the parameter list.
     * @param index Index of the parameter to locate
     * @return A pointer to the named string or NULL.
     */
    NamedString* getParam(unsigned int index) const;

    /**
     * Parameter access operator
     * @param name Name of the parameter to return
     * @return String value of the parameter, @ref String::empty() if missing
     */
    const String& operator[](const String& name) const;

    /**
     * Retrieve the value of a named parameter.
     * @param name Name of parameter to locate
     * @param defvalue Default value to return if not found
     * @return The string contained in the named parameter or the default
     */
    const char* getValue(const String& name, const char* defvalue = 0) const;

    /**
     * Retrieve the numeric value of a parameter.
     * @param name Name of parameter to locate
     * @param defvalue Default value to return if not found
     * @param minvalue Minimum value allowed for the parameter
     * @param maxvalue Maximum value allowed for the parameter
     * @param clamp Control the out of bound values: true to adjust to the nearest
     *  bound, false to return the default value
     * @return The number contained in the named parameter or the default
     */
    int getIntValue(const String& name, int defvalue = 0, int minvalue = INT_MIN,
	int maxvalue = INT_MAX, bool clamp = true) const;

    /**
     * Retrieve the numeric value of a parameter trying first a table lookup.
     * @param name Name of parameter to locate
     * @param tokens A pointer to an array of tokens to try to lookup
     * @param defvalue Default value to return if not found
     * @return The number contained in the named parameter or the default
     */
    int getIntValue(const String& name, const TokenDict* tokens, int defvalue = 0) const;

    /**
     * Retrieve the 64-bit numeric value of a parameter.
     * @param name Name of parameter to locate
     * @param defvalue Default value to return if not found
     * @param minvalue Minimum value allowed for the parameter
     * @param maxvalue Maximum value allowed for the parameter
     * @param clamp Control the out of bound values: true to adjust to the nearest
     *  bound, false to return the default value
     * @return The number contained in the named parameter or the default
     */
    int64_t getInt64Value(const String& name, int64_t defvalue = 0, int64_t minvalue = LLONG_MIN,
	int64_t maxvalue = LLONG_MAX, bool clamp = true) const;

    /**
     * Retrieve the floating point value of a parameter.
     * @param name Name of parameter to locate
     * @param defvalue Default value to return if not found
     * @return The number contained in the named parameter or the default
     */
    double getDoubleValue(const String& name, double defvalue = 0.0) const;

    /**
     * Retrieve the boolean value of a parameter.
     * @param name Name of parameter to locate
     * @param defvalue Default value to return if not found
     * @return The boolean value contained in the named parameter or the default
     */
    bool getBoolValue(const String& name, bool defvalue = false) const;

    /**
     * Replaces all ${paramname} in a String with the corresponding parameters
     * @param str String in which the replacements will be made
     * @param sqlEsc True to apply SQL escaping to parameter values
     * @param extraEsc Character to escape other than the SQL default ones
     * @return Number of replacements made, -1 if an error occured
     */
    int replaceParams(String& str, bool sqlEsc = false, char extraEsc = 0) const;

    /**
     * Dumps the name and all parameters to a string in a human readable format.
     * No escaping takes place so this method should be used for debugging only
     * @param str String to which the name and parameters are appended
     * @param separator Separator string to use before each parameter
     * @param quote String quoting character, usually single or double quote
     * @param force True to insert the separator even in an empty string
     */
    void dump(String& str, const char* separator, char quote = 0, bool force = false) const;

    /**
     * A static empty named list
     * @return Reference to a static empty named list
     */
    static const NamedList& empty();

    /**
     * Get the parameters list
     * @return Pointer to the parameters list
     */
    inline ObjList* paramList()
	{ return &m_params; }

    /**
     * Get the parameters list
     * @return Pointer to the parameters list
     */
    inline const ObjList* paramList() const
	{ return &m_params; }

private:
    NamedList(); // no default constructor please
    ObjList m_params;
};

/**
 * An iterator for NamedString parameters of a NamedList.
 * Fast but unsafe, the list must not be modified during iteration.
 * @short NamedList parameters iterator
 */
class YATE_API NamedIterator
{
public:
    /**
     * Constructor
     * @param list NamedList whose parameters are iterated
     */
    inline NamedIterator(const NamedList& list)
	: m_list(&list), m_item(list.m_params.skipNull())
	{ }

    /**
     * Copy constructor, points to same list and position as the original
     * @param original Iterator to copy from
     */
    inline NamedIterator(const NamedIterator& original)
	: m_list(original.m_list), m_item(original.m_item)
	{ }

    /**
     * Assignment from list operator
     * @param list NamedList whose parameters are iterated
     */
    inline NamedIterator& operator=(const NamedList& list)
	{ m_list = &list; m_item = list.m_params.skipNull(); return *this; }

    /**
     * Assignment operator, points to same list and position as the original
     * @param original Iterator to copy from
     */
    inline NamedIterator& operator=(const NamedIterator& original)
	{ m_list = original.m_list; m_item = original.m_item; return *this; }

    /**
     * Get the current parameter and advance in the list
     * @return Pointer to list parameter or NULL if advanced past end (eof)
     */
    const NamedString* get();

    /**
     * Check if the iteration reached end of the parameters list
     */
    inline bool eof() const
	{ return !m_item; }

    /**
     * Reset the iterator to the first position in the parameters list
     */
    inline void reset()
	{ m_item = m_list->m_params.skipNull(); }

private:
    NamedIterator(); // no default constructor please
    const NamedList* m_list;
    const ObjList* m_item;
};

/**
 * Uniform Resource Identifier encapsulation and parser.
 * For efficiency reason the parsing is delayed as long as possible
 * @short Encapsulation for an URI
 */
class YATE_API URI : public String
{
public:
    /**
     * Empty URI constructor
     */
    URI();

    /**
     * Copy constructor
     * @param uri Original URI to copy
     */
    URI(const URI& uri);

    /**
     * Constructor from a String that gets parsed later
     * @param uri String form of the URI
     */
    explicit URI(const String& uri);

    /**
     * Constructor from a C string that gets parsed later
     * @param uri String form of the URI
     */
    explicit URI(const char* uri);

    /**
     * Constructor from URI components
     * @param proto Protocol - something like "http", "sip", etc.
     * @param user User component of the URI
     * @param host Hostname component of the URI
     * @param port Port part of the URI (optional)
     * @param desc Description part in front of the URI (optional)
     */
    URI(const char* proto, const char* user, const char* host, int port = 0, const char* desc = 0);

    /**
     * Calling this method ensures the string URI is parsed into components
     */
    void parse() const;

    /**
     * Assignment operator from URI
     * @param value New URI value to assign
     */
    inline URI& operator=(const URI& value)
	{ String::operator=(value); return *this; }

    /**
     * Assignment operator from String
     * @param value New URI value to assign
     */
    inline URI& operator=(const String& value)
	{ String::operator=(value); return *this; }

    /**
     * Assignment operator from C string
     * @param value New URI value to assign
     */
    inline URI& operator=(const char* value)
	{ String::operator=(value); return *this; }

    /**
     * Access method to the description part of the URI
     * @return Description part of the URI
     */
    inline const String& getDescription() const
	{ parse(); return m_desc; }

    /**
     * Access method to the protocol part of the URI
     * @return Protocol part of the URI
     */
    inline const String& getProtocol() const
	{ parse(); return m_proto; }

    /**
     * Access method to the user part of the URI
     * @return User component of the URI
     */
    inline const String& getUser() const
	{ parse(); return m_user; }

    /**
     * Access method to the host part of the URI
     * @return Hostname part of the URI
     */
    inline const String& getHost() const
	{ parse(); return m_host; }

    /**
     * Access method to the port part of the URI
     * @return Port of the URI, zero if not set
     */
    inline int getPort() const
	{ parse(); return m_port; }

    /**
     * Access method to the additional text part of the URI
     * @return Additional text of the URI including the separator
     */
    inline const String& getExtra() const
	{ parse(); return m_extra; }

protected:
    /**
     * Notification method called whenever the string URI has changed.
     * The default behaviour is to invalidate the parsed flag and cal the
     *  method inherited from @ref String.
     */
    virtual void changed();
    mutable bool m_parsed;
    mutable String m_desc;
    mutable String m_proto;
    mutable String m_user;
    mutable String m_host;
    mutable String m_extra;
    mutable int m_port;
};

class MutexPrivate;
class SemaphorePrivate;
class ThreadPrivate;

/**
 * An abstract base class for implementing lockable objects
 * @short Abstract interface for lockable objects
 */
class YATE_API Lockable
{
public:
    /**
     * Destructor
     */
    virtual ~Lockable();

    /**
     * Attempt to lock the object and eventually wait for it
     * @param maxwait Time in microseconds to wait, -1 wait forever
     * @return True if successfully locked, false on failure
     */
    virtual bool lock(long maxwait = -1) = 0;

    /**
     * Unlock the object, does never wait
     * @return True if successfully unlocked the object
     */
    virtual bool unlock() = 0;

    /**
     * Check if the object is currently locked - as it's asynchronous it
     *  guarantees nothing if other thread changes the status
     * @return True if the object was locked when the function was called
     */
    virtual bool locked() const = 0;

    /**
     * Check if the object is unlocked (try to lock and unlock it)
     * @param maxwait Time in microseconds to wait, -1 to wait forever
     * @return True if successfully locked and unlocked, false on failure
     */
    virtual bool check(long maxwait = -1);

    /**
     * Fully unlock the object, even if it was previously multiple locked.
     * There is no guarantee about the object status after the function returns.
     * This function should be used only if you understand it very well
     * @return True if the object was fully unlocked
     */
    virtual bool unlockAll();

    /**
     * Set a maximum wait time for debugging purposes
     * @param maxwait Maximum time in microseconds to wait for any lockable
     *  object when no time limit was requested, zero to disable limit
     */
    static void wait(unsigned long maxwait);

    /**
     * Get the maximum wait time used for debugging purposes
     * @return Maximum time in microseconds, zero if no maximum is set
     */
    static unsigned long wait();

    /**
     * Start actually using lockables, for platforms where these objects are not
     *  usable in global object constructors.
     * This method must be called at least once somewhere from main() but
     *  before creating any threads and without holding any object locked.
     */
    static void startUsingNow();

    /**
     * Enable some safety and sanity check features.
     * This provides a safer code and easier locking debugging at the price of performance penalty.
     * This method must be called early and not changed after initialization
     * @param safe True to enable locking safety measures, false to disable
     */
    static void enableSafety(bool safe = true);

    /**
     * Retrieve safety and sanity check features flag value
     * @return Locking safety measures flag value
     */
    static bool safety();
};

/**
 * A simple mutual exclusion for locking access between threads
 * @short Mutex support
 */
class YATE_API Mutex : public Lockable
{
    friend class MutexPrivate;
public:
    /**
     * Construct a new unlocked mutex
     * @param recursive True if the mutex has to be recursive (reentrant),
     *  false for a normal fast mutex
     * @param name Static name of the mutex (for debugging purpose only)
     */
    explicit Mutex(bool recursive = false, const char* name = 0);

    /**
     * Copy constructor, creates a shared mutex
     * @param original Reference of the mutex to share
     */
    Mutex(const Mutex& original);

    /**
     * Destroy the mutex
     */
    ~Mutex();

    /**
     * Assignment operator makes the mutex shared with the original
     * @param original Reference of the mutex to share
     */
    Mutex& operator=(const Mutex& original);

    /**
     * Attempt to lock the mutex and eventually wait for it
     * @param maxwait Time in microseconds to wait for the mutex, -1 wait forever
     * @return True if successfully locked, false on failure
     */
    virtual bool lock(long maxwait = -1);

    /**
     * Unlock the mutex, does never wait
     * @return True if successfully unlocked the mutex
     */
    virtual bool unlock();

    /**
     * Check if the mutex is currently locked - as it's asynchronous it
     *  guarantees nothing if other thread changes the mutex's status
     * @return True if the mutex was locked when the function was called
     */
    virtual bool locked() const;

    /**
     * Retrieve the name of the Thread (if any) holding the Mutex locked
     * @return Thread name() or NULL if thread not named
     */
    const char* owner() const;

    /**
     * Check if this mutex is recursive or not
     * @return True if this is a recursive mutex, false for a fast mutex
     */
    bool recursive() const;

    /**
     * Get the number of mutexes counting the shared ones only once
     * @return Count of individual mutexes
     */
    static int count();

    /**
     * Get the number of currently locked mutexes
     * @return Count of locked mutexes, -1 if unknown (not tracked)
     */
    static int locks();

    /**
     * Check if a timed lock() is efficient on this platform
     * @return True if a lock with a maxwait parameter is efficiently implemented
     */
    static bool efficientTimedLock();

private:
    MutexPrivate* privDataCopy() const;
    MutexPrivate* m_private;
};

/**
 * This class holds a Mutex array. Mutexes can be retrieved based on object pointers.
 * A mutex pool can be used to associate a smaller set of Mutex objects with a much
 *  larger set of objects needing lock.
 * @short A Mutex pool
 */
class YATE_API MutexPool
{
public:
    /**
     * Build the mutex pool
     * @param len The number of mutex objects to build. The length should be an
     *  odd number to obtain an optimal distribution of pointer based mutexes
     *  (usually pointers are aligned at even addresses): some mutexes might never
     *  get used if the length is an even number
     * @param recursive True if the mutex has to be recursive (reentrant),
     *  false for a normal fast mutex
     * @param name Static name of the mutex (for debugging purpose only)
     */
    MutexPool(unsigned int len = 13, bool recursive = false, const char* name = 0);

    /**
     * Destructor. Release data
     */
    ~MutexPool();

    /**
     * Build an index from object pointer (pointer value modulo array length).
     * Always cast the pointer to the same type when calling this method to
     *  make sure the same index is returned for a given object
     * @param ptr The pointer to object
     * @return Valid array index
     */
    inline unsigned int index(void* ptr) const
	{ return ((unsigned int)(unsigned long)ptr) % m_length; }

    /**
     * Retrieve the mutex associated with a given pointer.
     * Always cast the pointer to the same type when calling this method to
     *  make sure the same mutex is returned for a given object
     * @param ptr The pointer to object
     * @return Valid Mutex pointer
     */
    inline Mutex* mutex(void* ptr) const
	{ return m_data[index(ptr)]; }

    /**
     * Retrieve the mutex at a given index modulo array length
     * @param idx The index
     * @return Valid Mutex pointer
     */
    inline Mutex* mutex(unsigned int idx) const
	{ return m_data[idx % m_length]; }

private:
    String* m_name;                      // Mutex names
    Mutex** m_data;                      // The array
    unsigned int m_length;               // Array length
};

/**
 * A semaphore object for synchronizing threads, can also be used as a token bucket
 * @short Semaphore implementation
 */
class YATE_API Semaphore : public Lockable
{
    friend class SemaphorePrivate;
public:
    /**
     * Construct a new unlocked semaphore
     * @param maxcount Maximum unlock count, must be strictly positive
     * @param name Static name of the semaphore (for debugging purpose only)
     * @param initialCount Initial semaphore count, must not be greater than maxcount
     */
    explicit Semaphore(unsigned int maxcount = 1, const char* name = 0,
	unsigned int initialCount = 1);

    /**
     * Copy constructor, creates a shared semaphore
     * @param original Reference of the semaphore to share
     */
    Semaphore(const Semaphore& original);

    /**
     * Destroy the semaphore
     */
    ~Semaphore();

    /**
     * Assignment operator makes the semaphore shared with the original
     * @param original Reference of the semaphore to share
     */
    Semaphore& operator=(const Semaphore& original);

    /**
     * Attempt to get a lock on the semaphore and eventually wait for it
     * @param maxwait Time in microseconds to wait, -1 wait forever
     * @return True if successfully locked, false on failure
     */
    virtual bool lock(long maxwait = -1);

    /**
     * Unlock the semaphore, does never wait nor get over counter maximum
     * @return True if successfully unlocked
     */
    virtual bool unlock();

    /**
     * Check if the semaphore is currently locked (waiting) - as it's
     *  asynchronous it guarantees nothing if other thread changes status
     * @return True if the semaphore was locked when the function was called
     */
    virtual bool locked() const;

    /**
     * Get the number of semaphores counting the shared ones only once
     * @return Count of individual semaphores
     */
    static int count();

    /**
     * Get the number of currently locked (waiting) semaphores
     * @return Count of locked semaphores, -1 if unknown (not tracked)
     */
    static int locks();

    /**
     * Check if a timed lock() is efficient on this platform
     * @return True if a lock with a maxwait parameter is efficiently implemented
     */
    static bool efficientTimedLock();

private:
    SemaphorePrivate* privDataCopy() const;
    SemaphorePrivate* m_private;
};

/**
 * A lock is a stack allocated (automatic) object that locks a lockable object
 *  on creation and unlocks it on destruction - typically when exiting a block
 * @short Ephemeral mutex or semaphore locking object
 */
class YATE_API Lock
{
    YNOCOPY(Lock); // no automatic copies please
public:
    /**
     * Create the lock, try to lock the object
     * @param lck Reference to the object to lock
     * @param maxwait Time in microseconds to wait, -1 wait forever
     */
    inline Lock(Lockable& lck, long maxwait = -1)
	{ m_lock = lck.lock(maxwait) ? &lck : 0; }

    /**
     * Create the lock, try to lock the object
     * @param lck Pointer to the object to lock
     * @param maxwait Time in microseconds to wait, -1 wait forever
     */
    inline Lock(Lockable* lck, long maxwait = -1)
	{ m_lock = (lck && lck->lock(maxwait)) ? lck : 0; }

    /**
     * Destroy the lock, unlock the mutex if it was locked
     */
    inline ~Lock()
	{ if (m_lock) m_lock->unlock(); }

    /**
     * Return a pointer to the lockable object this lock holds
     * @return A pointer to a Lockable or NULL if locking failed
     */
    inline Lockable* locked() const
	{ return m_lock; }

    /**
     * Unlock the object if it was locked and drop the reference to it
     */
    inline void drop()
	{ if (m_lock) m_lock->unlock(); m_lock = 0; }

    /**
     * Attempt to acquire a new lock on another object
     * @param lck Pointer to the object to lock
     * @param maxwait Time in microseconds to wait, -1 wait forever
     * @return True if locking succeeded or same object was locked
     */
    inline bool acquire(Lockable* lck, long maxwait = -1)
	{ return (lck && (lck == m_lock)) ||
	    (drop(),(lck && (m_lock = lck->lock(maxwait) ? lck : 0))); }

    /**
     * Attempt to acquire a new lock on another object
     * @param lck Reference to the object to lock
     * @param maxwait Time in microseconds to wait, -1 wait forever
     * @return True if locking succeeded or same object was locked
     */
    inline bool acquire(Lockable& lck, long maxwait = -1)
	{ return acquire(&lck,maxwait); }

private:
    Lockable* m_lock;

    /** Make sure no Lock is ever created on heap */
    inline void* operator new(size_t);

    /** Never allocate an array of this class */
    inline void* operator new[](size_t);
};

/**
 * A dual lock is a stack allocated (automatic) object that locks a pair
 *  of mutexes on creation and unlocks them on destruction. The mutexes are
 *  always locked in the same order to prevent trivial deadlocks
 * @short Ephemeral double mutex locking object
 */
class YATE_API Lock2
{
    YNOCOPY(Lock2); // no automatic copies please
public:
    /**
     * Create the dual lock, try to lock each mutex
     * @param mx1 Pointer to the first mutex to lock
     * @param mx2 Pointer to the second mutex to lock
     * @param maxwait Time in microseconds to wait for each mutex, -1 wait forever
     */
    inline Lock2(Mutex* mx1, Mutex* mx2, long maxwait = -1)
	: m_mx1(0), m_mx2(0)
	{ lock(mx1,mx2,maxwait); }

    /**
     * Create the dual lock, try to lock each mutex
     * @param mx1 Reference to the first mutex to lock
     * @param mx2 Reference to the second mutex to lock
     * @param maxwait Time in microseconds to wait for each mutex, -1 wait forever
     */
    inline Lock2(Mutex& mx1, Mutex& mx2, long maxwait = -1)
	: m_mx1(0), m_mx2(0)
	{ lock(&mx1,&mx2,maxwait); }

    /**
     * Destroy the lock, unlock the mutex if it was locked
     */
    inline ~Lock2()
	{ drop(); }

    /**
     * Check if the locking succeeded
     * @return True if all mutexes were locked
     */
    inline bool locked() const
	{ return m_mx1 != 0; }

    /**
     * Lock in a new pair of mutexes. Any existing locks are dropped
     * @param mx1 Pointer to the first mutex to lock
     * @param mx2 Pointer to the second mutex to lock
     * @param maxwait Time in microseconds to wait for each mutex, -1 wait forever
     * @return True on success - non-NULL mutexes locked
     */
    bool lock(Mutex* mx1, Mutex* mx2, long maxwait = -1);

    /**
     * Lock in a new pair of mutexes
     * @param mx1 Reference to the first mutex to lock
     * @param mx2 Reference to the second mutex to lock
     * @param maxwait Time in microseconds to wait for each mutex, -1 wait forever
     * @return True on success - both locked
     */
    inline bool lock(Mutex& mx1, Mutex& mx2, long maxwait = -1)
	{ return lock(&mx1,&mx2,maxwait); }

    /**
     * Unlock both mutexes if they were locked and drop the references
     */
    void drop();

private:
    Mutex* m_mx1;
    Mutex* m_mx2;

    /** Make sure no Lock2 is ever created on heap */
    inline void* operator new(size_t);

    /** Never allocate an array of this class */
    inline void* operator new[](size_t);
};

/**
 * This class holds the action to execute a certain task, usually in a
 *  different execution thread.
 * @short Encapsulates a runnable task
 */
class YATE_API Runnable
{
public:
    /**
     * This method is called in another thread to do the actual job.
     * When it returns the job or thread terminates.
     */
    virtual void run() = 0;

    /**
     * Do-nothing destructor, placed here just to shut up GCC 4+
     */
    virtual ~Runnable();
};

/**
 * A thread is a separate execution context that exists in the same address
 *  space. Threads make better use of multiple processor machines and allow
 *  blocking one execution thread while allowing other to run.
 * @short Thread support class
 */
class YATE_API Thread : public Runnable
{
    friend class ThreadPrivate;
    friend class MutexPrivate;
    friend class SemaphorePrivate;
    YNOCOPY(Thread); // no automatic copies please
public:
    /**
     * Running priorities, their mapping is operating system dependent
     */
    enum Priority {
	Lowest,
	Low,
	Normal,
	High,
	Highest
    };

    /**
     * This method is called when the current thread terminates.
     */
    virtual void cleanup();

    /**
     * Actually starts running the new thread which lingers after creation
     * @return False if an error occured, true if started ok
     */
    bool startup();

    /**
     * Check if the thread creation failed
     * @return True if an error occured, false if created ok
     */
    bool error() const;

    /**
     * Check if the thread is running or not
     * @return True if running, false if it has terminated or no startup called
     */
    bool running() const;

    /**
     * Count how many Yate mutexes are kept locked by this thread
     * @return Number of Mutex locks held by this thread
     */
    inline int locks() const
	{ return m_locks; }

    /**
     * Check if the thread is currently helding or attempting to lock a mutex
     * @return True if the current thread is in an unsafe to cancel state
     */
    inline bool locked() const
	{ return m_locking || m_locks; }

    /**
     * Get the name of this thread
     * @return The pointer that was passed in the constructor
     */
    const char* name() const;

    /**
     * Get the name of the currently running thread
     * @return The pointer that was passed in the thread's constructor
     */
    static const char* currentName();

    /**
     * Give up the currently running timeslice. Note that on some platforms
     *  it also sleeps for the operating system's scheduler resolution
     * @param exitCheck Terminate the thread if asked so
     */
    static void yield(bool exitCheck = false);

    /**
     * Sleep for a system dependent period adequate for an idle thread.
     * On most operating systems this is a 5 msec sleep.
     * @param exitCheck Terminate the thread if asked so
     */
    static void idle(bool exitCheck = false);

    /**
     * Sleep for a number of seconds
     * @param sec Number of seconds to sleep
     * @param exitCheck Terminate the thread if asked so
     */
    static void sleep(unsigned int sec, bool exitCheck = false);

    /**
     * Sleep for a number of milliseconds
     * @param msec Number of milliseconds to sleep
     * @param exitCheck Terminate the thread if asked so
     */
    static void msleep(unsigned long msec, bool exitCheck = false);

    /**
     * Sleep for a number of microseconds
     * @param usec Number of microseconds to sleep, may be rounded to
     *  milliseconds on some platforms
     * @param exitCheck Terminate the thread if asked so
     */
    static void usleep(unsigned long usec, bool exitCheck = false);

    /**
     * Get the platform dependent idle sleep interval in microseconds
     * @return Number of microseconds each call to idle() will sleep
     */
    static unsigned long idleUsec();

    /**
     * Get the platform dependent idle sleep interval in milliseconds
     * @return Number of milliseconds each call to idle() will sleep
     */
    static unsigned long idleMsec();

    /**
     * Set the idle sleep interval or reset to platform default
     * @param msec Sleep interval in milliseconds, platform default if zero
     */
    static void idleMsec(unsigned long msec);

    /**
     * Get a pointer to the currently running thread
     * @return A pointer to the current thread or NULL for the main thread
     *  or threads created by other libraries
     */
    static Thread* current();

    /**
     * Get the number of Yate created threads
     * @return Count of current Thread objects
     */
    static int count();

    /**
     * Check if the current thread was asked to terminate.
     * @param exitNow If thread is marked as cancelled then terminate immediately
     * @return False if thread should continue running, true if it should stop
     */
    static bool check(bool exitNow = true);

    /**
     * Terminates the current thread.
     */
    static void exit();

    /**
     * Terminates the specified thread.
     * @param hard Kill the thread the hard way rather than just setting an exit check marker
     */
    void cancel(bool hard = false);

    /**
     * Check if this thread is the currently running thread
     * @return True if this is the current thread
     */
    inline bool isCurrent() const
	{ return current() == this; }

    /**
     * Get the object counter of this thread
     * @return Pointer to thread's counter for new objects
     */
    NamedCounter* getObjCounter() const;

    /**
     * Set the object counter of this thread
     * @param counter New counter object or NULL
     * @return Pointer to old counter object
     */
    NamedCounter* setObjCounter(NamedCounter* counter);

    /**
     * Get the object counter of the current thread
     * @param always Return the object even if counting is disabled
     * @return Pointer to current counter for new objects
     */
    static NamedCounter* getCurrentObjCounter(bool always = false);

    /**
     * Set the object counter of the current thread
     * @param counter New counter object or NULL
     * @return Pointer to old counter object
     */
    static NamedCounter* setCurrentObjCounter(NamedCounter* counter);

    /**
     * Convert a priority name to a thread priority level
     * @param name Name of the requested level
     * @param defvalue Priority to return in case of an invalid name
     * @return A thread priority level
     */
    static Priority priority(const char* name, Priority defvalue = Normal);

    /**
     * Convert a priority level to a textual name
     * @param prio Priority level to convert
     * @return Name of the level or NULL if an invalid argument was provided
     */
    static const char* priority(Priority prio);

    /**
     * Kills all other running threads. Ouch!
     * Must be called from the main thread or it does nothing.
     */
    static void killall();

    /**
     * On some platforms this method kills all other running threads.
     * Must be called after fork() but before any exec*() call.
     */
    static void preExec();

    /**
     * Get the last thread error
     * @return The value returned by GetLastError() (on Windows) or
     *  the value of C library 'errno' variable otherwise
     */
    static int lastError();

    /**
     * Get the last thread error's string from system.
     * @param buffer The destination string
     * @return True if an error string was retrieved. If false is returned, the buffer
     *  is filled with a generic string indicating an unknown error and its code
     */
    static inline bool errorString(String& buffer)
	{ return errorString(buffer,lastError()); }

    /**
     * Get an error string from system.
     * On Windows the code parameter must be a code returned by GetLastError().
     * Otherwise, the error code should be a valid value for the C library 'errno'
     *  variable
     * @param buffer The destination string
     * @param code The error code
     * @return True if an error string was retrieved. If false is returned, the buffer
     *  is filled with a generic string indicating an unknown error and its code
     */
    static bool errorString(String& buffer, int code);

protected:
    /**
     * Creates and starts a new thread
     * @param name Static name of the thread (for debugging purpose only)
     * @param prio Thread priority
     */
    Thread(const char *name = 0, Priority prio = Normal);

    /**
     * Creates and starts a new thread
     * @param name Static name of the thread (for debugging purpose only)
     * @param prio Thread priority level name
     */
    Thread(const char *name, const char* prio);

    /**
     * The destructor is called when the thread terminates
     */
    virtual ~Thread();

private:
    ThreadPrivate* m_private;
    int m_locks;
    bool m_locking;
};

/**
 * This class changes the current thread's object counter for its lifetime
 * @short Ephemeral object counter changer
 */
class YATE_API TempObjectCounter
{
    YNOCOPY(TempObjectCounter); // no automatic copies please
public:
    /**
     * Constructor, changes object counter if counting is enabled
     * @param counter Object counter to apply on the current thread
     * @param enable True to enable change, false to take no action
     */
    inline TempObjectCounter(NamedCounter* counter, bool enable = GenObject::getObjCounting())
	: m_saved(0), m_enabled(enable)
	{ if (m_enabled) m_saved = Thread::setCurrentObjCounter(counter); }

    /**
     * Constructor, changes object counter if counting is enabled
     * @param obj Object to copy the counter from
     * @param enable True to enable change, false to take no action
     */
    inline TempObjectCounter(const GenObject* obj, bool enable = GenObject::getObjCounting())
	: m_saved(0), m_enabled(enable && obj)
	{ if (m_enabled) m_saved = Thread::setCurrentObjCounter(obj->getObjCounter()); }

    /**
     * Constructor, changes object counter if counting is enabled
     * @param obj Object to copy the counter from
     * @param enable True to enable change, false to take no action
     */
    inline TempObjectCounter(const GenObject& obj, bool enable = GenObject::getObjCounting())
	: m_saved(0), m_enabled(enable)
	{ if (m_enabled) m_saved = Thread::setCurrentObjCounter(obj.getObjCounter()); }

    /**
     * Destructor, restores saved object counter
     */
    inline ~TempObjectCounter()
	{ if (m_enabled) Thread::setCurrentObjCounter(m_saved); }

private:
    NamedCounter* m_saved;
    bool m_enabled;
};

class Socket;

/**
 * Wrapper class to keep a socket address
 * @short A socket address holder
 */
class YATE_API SocketAddr : public GenObject
{
public:
    /**
     * Known address families
     */
    enum Family {
	Unknown = AF_UNSPEC,
	IPv4 = AF_INET,
	AfMax = AF_MAX,
	AfUnsupported = AfMax,
#ifdef AF_INET6
	IPv6 = AF_INET6,
#else
	IPv6 = AfUnsupported + 1,
#endif
#ifdef HAS_AF_UNIX
	Unix = AF_UNIX,
#else
	Unix = AfUnsupported + 2,
#endif
    };

    /**
     * Default constructor of an empty address
     */
    inline SocketAddr()
	: m_address(0), m_length(0)
	{ }

    /**
     * Copy constructor
     * @param value Address to copy
     */
    inline SocketAddr(const SocketAddr& value)
	: GenObject(),
	  m_address(0), m_length(0)
	{ assign(value.address(),value.length()); }

    /**
     * Constructor of a null address
     * @param family Family of the address to create
     * @param raw Raw address data
     */
    explicit SocketAddr(int family, const void* raw = 0);

    /**
     * Constructor that stores a copy of an address
     * @param addr Pointer to the address to store
     * @param len Length of the stored address, zero to use default
     */
    SocketAddr(const struct sockaddr* addr, socklen_t len = 0);

    /**
     * Destructor that frees and zeroes out everything
     */
    virtual ~SocketAddr();

    /**
     * Assignment operator
     * @param value Address to copy
     */
    inline SocketAddr& operator=(const SocketAddr& value)
	{ assign(value.address(),value.length()); return *this; }

    /**
     * Equality comparation operator
     * @param other Address to compare to
     * @return True if the addresses are equal
     */
    bool operator==(const SocketAddr& other) const;

    /**
     * Inequality comparation operator
     * @param other Address to compare to
     * @return True if the addresses are different
     */
    inline bool operator!=(const SocketAddr& other) const
	{ return !operator==(other); }

    /**
     * Clears up the address, frees the memory
     */
    void clear();

    /**
     * Assigns an empty address of a specific type
     * @param family Family of the address to create
     * @return True if the address family is supported
     */
    bool assign(int family);

    /**
     * Assigns a new address
     * @param addr Pointer to the address to store
     * @param len Length of the stored address, zero to use default
     */
    void assign(const struct sockaddr* addr, socklen_t len = 0);

    /**
     * Assigns a new address
     * @param addr Packed binary address to store
     * @return True if the address family is supported
     */
    bool assign(const DataBlock& addr);

    /**
     * Attempt to guess a local address that will be used to reach a remote one
     * @param remote Remote address to reach
     * @return True if guessed an address, false if failed
     */
    bool local(const SocketAddr& remote);

    /**
     * Check if a non-null address is held
     * @return True if a valid address is held, false if null
     */
    inline bool valid() const
	{ return m_length && m_address; }

    /**
     * Check if a null address is held
     * @return True if a null address is held
     */
    inline bool null() const
	{ return !(m_length && m_address); }

    /**
     * Get the family of the stored address
     * @return Address family of the stored address or zero (AF_UNSPEC)
     */
    inline int family() const
	{ return m_address ? m_address->sa_family : 0; }

    /**
     * Retrieve address family name
     * @return Address family name
     */
    inline const char* familyName()
	{ return lookupFamily(family()); }

    /**
     * Retrieve the sin6_scope_id value of an IPv6 address
     * @return The requested value (it may be 0), 0 if not available
     */
    inline unsigned int scopeId() const
	{ return scopeId(address()); }

    /**
     * Set the sin6_scope_id value of an IPv6 address
     * @param val Value to set
     * @return True on success, false if not available
     */
    inline bool scopeId(unsigned int val)
	{ return scopeId(address(),val); }

    /**
     * Get the host of this address
     * @return Host name as String
     */
    inline const String& host() const
	{ return m_host; }

    /**
     * Get the host and port of this address
     * @return Address String (host:port)
     */
    inline const String& addr() const {
	    if (!m_addr)
		updateAddr();
	    return m_addr;
	}

    /**
     * Set the hostname of this address.
     * Guess address family if not initialized
     * @param name Host to set
     * @return True if new host set, false if name could not be parsed
     */
    virtual bool host(const String& name);

    /**
     * Get the port of the stored address (if supported)
     * @return Port number of the socket address or zero
     */
    int port() const;

    /**
     * Set the port of the stored address (if supported)
     * @param newport Port number to set in the socket address
     * @return True if new port set, false if not supported
     */
    bool port(int newport);

    /**
     * Get the contained socket address
     * @return A pointer to the socket address
     */
    inline struct sockaddr* address() const
	{ return m_address; }

    /**
     * Get the length of the address
     * @return Length of the stored address
     */
    inline socklen_t length() const
	{ return m_length; }

    /**
     * Check if this address is empty or null
     * @return True if the address is empty or '0.0.0.0' (IPv4) or '::' IPv6
     */
    inline bool isNullAddr() const
	{ return isNullAddr(m_host,family()); }

    /**
     * Copy the host address to a buffer
     * @param addr Buffer to put the packed address into
     * @return Address family, Unknown on failure
     */
    int copyAddr(DataBlock& addr) const;

    /**
     * Check if an address family is supported by the library
     * @param family Family of the address to check
     * @return True if the address family is supported
     */
    static bool supports(int family);

    /**
     * Retrieve the family of an address
     * @param addr The address to check
     * @return Address family
     */
    static int family(const String& addr);

    /**
     * Convert the host address to a String
     * @param buf Destination buffer
     * @param addr Socket address
     * @return True on success, false if address family is not supported
     */
    static bool stringify(String& buf, struct sockaddr* addr);

    /**
     * Put a host address to a buffer
     * @param buf Destination buffer. It must be large enough to keep the address
     *  (4 bytes for IPv4, 16 bytes for IPv6)
     * @param host The host address
     * @param family Address family, set it to Unknown to detect
     * @return Address family, Unknown on failure
     */
    static inline int unStringify(uint8_t* buf, const String& host,
	int family = Unknown) {
	    SocketAddr sa(family);
	    return sa.host(host) ? copyAddr(buf,sa.address()) : Unknown;
	}

    /**
     * Copy a host address to a buffer
     * @param buf Destination buffer. It must be large enough to keep the address
     *  (4 bytes for IPv4, 16 bytes for IPv6)
     * @param addr The host address
     * @return Address family, Unknown on failure
     */
    static int copyAddr(uint8_t* buf, struct sockaddr* addr);

    /**
     * Retrieve the scope id value of an IPv6 address
     * @param addr The address
     * @return The requested value (it may be 0), 0 if not available
     */
    static inline unsigned int scopeId(struct sockaddr* addr) {
#ifdef AF_INET6
	    if (addr && addr->sa_family == AF_INET6)
		return ((struct sockaddr_in6*)addr)->sin6_scope_id;
#endif
	    return 0;
	}

    /**
     * Set the scope id value of an IPv6 address
     * @param addr Address to set
     * @param val Value to set
     * @return True on success, false if not available
     */
    static inline bool scopeId(struct sockaddr* addr, unsigned int val) {
#ifdef AF_INET6
	    if (addr && addr->sa_family == AF_INET6) {
		((struct sockaddr_in6*)addr)->sin6_scope_id = val;
		return true;
	    }
#endif
	    return false;
	}

    /**
     * Append an address to a buffer
     * @param buf Destination buffer
     * @param addr Address to append
     * @param family Address family, set it to Unknown to detect
     * @return Buffer address
     */
    static String& appendAddr(String& buf, const String& addr, int family = Unknown);

    /**
     * Append an address to a buffer in the form addr:port
     * @param buf Destination buffer
     * @param addr Address to append
     * @param port Port to append
     * @param family Address family, set it to Unknown to detect
     * @return Buffer address
     */
    static inline String& appendTo(String& buf, const String& addr, int port,
	int family = Unknown) {
	    appendAddr(buf,addr,family) << ":" << port;
	    return buf;
	}

    /**
     * Append an address to a buffer in the form addr:port
     * @param addr Address to append
     * @param port Port to append
     * @param family Address family, set it to Unknown to detect
     * @return A String with concatenated address and port
     */
    static inline String appendTo(const String& addr, int port, int family = Unknown) {
	    String buf;
	    appendTo(buf,addr,port,family);
	    return buf;
	}

    /**
     * Check if an address is empty or null
     * @param addr Address to check
     * @param family Address family, set it to Unknown to detect
     * @return True if the address is empty or '0.0.0.0' (IPv4) or '::' IPv6
     */
    static bool isNullAddr(const String& addr, int family = Unknown);

    /**
     * Split an interface from address
     * An interface may be present in addr after a percent char (e.g. fe80::23%eth0)
     * It is safe call this method with the same destination and source string
     * @param buf Source buffer
     * @param addr Destination buffer for address
     * @param iface Optional pointer to be filled with interface name
     */
    static void splitIface(const String& buf, String& addr, String* iface = 0);

    /**
     * Split an address into ip/port.
     * Handled formats: addr, addr:port, [addr], [addr]:port
     * It is safe call this method with the same destination and source string
     * @param buf Source buffer
     * @param addr Destination buffer for address
     * @param port Destination port
     * @param portPresent Set it to true if the port is always present after the last ':'.
     *  This will handle IPv6 addresses without square brackets and port present
     *  (e.g. fe80::23:5060 will split into addr=fe80::23 and port=5060)
     */
    static void split(const String& buf, String& addr, int& port, bool portPresent = false);

    /**
     * Retrieve address family name
     * @param family Address family to retrieve
     * @return Address family name
     */
    static inline const char* lookupFamily(int family)
	{ return lookup(family,s_familyName); }

    /**
     * Retrieve IPv4 null address
     * @return IPv4 null address (0.0.0.0)
     */
    static const String& ipv4NullAddr();

    /**
     * Retrieve IPv6 null address
     * @return IPv6 null address (::)
     */
    static const String& ipv6NullAddr();

    /**
     * Retrieve the family name dictionary
     * @return Pointer to family name dictionary
     */
    static const TokenDict* dictFamilyName();

protected:
    /**
     * Convert the host address to a String stored in m_host
     */
    virtual void stringify();

    /**
     * Store host:port in m_addr
     */
    virtual void updateAddr() const;

    struct sockaddr* m_address;
    socklen_t m_length;
    String m_host;
    mutable String m_addr;

private:
    static const TokenDict s_familyName[];
};

/**
 * Abstract interface for an object that filters socket received data packets
 * @short A filter for received socket data
 */
class YATE_API SocketFilter : public GenObject
{
    friend class Socket;
    YNOCOPY(SocketFilter); // no automatic copies please
public:
    /**
     * Constructor
     */
    SocketFilter();

    /**
     * Destructor, unregisters from socket
     */
    virtual ~SocketFilter();

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Run whatever actions required on idle thread runs
     * @param when Time when the idle run started
     */
    virtual void timerTick(const Time& when);

    /**
     * Notify this filter about a received block of data
     * @param buffer Buffer for received data
     * @param length Length of the data in buffer
     * @param flags Operating system specific bit flags of the operation
     * @param addr Address of the incoming data, may be NULL
     * @param adrlen Length of the valid data in address structure
     * @return True if this filter claimed the data
     */
    virtual bool received(void* buffer, int length, int flags, const struct sockaddr* addr, socklen_t adrlen) = 0;

    /**
     * Get the socket to which the filter is currently attached
     * @return Pointer to the socket of this filter
     */
    inline Socket* socket() const
	{ return m_socket; }

    /**
     * Check if the socket of this filter is valid
     * @return True if the filter has a valid socket
     */
    bool valid() const;

private:
    Socket* m_socket;
};

/**
 * Base class for encapsulating system dependent stream capable objects
 * @short An abstract stream class capable of reading and writing
 */
class YATE_API Stream
{
public:
    /**
     * Enumerate seek start position
     */
    enum SeekPos {
	SeekBegin,                       // Seek from start of stream
	SeekEnd,                         // Seek from stream end
	SeekCurrent                      // Seek from current position
    };

    /**
     * Destructor, terminates the stream
     */
    virtual ~Stream();

    /**
     * Get the error code of the last operation on this stream
     * @return Error code generated by the last operation on this stream
     */
    inline int error() const
	{ return m_error; }

    /**
     * Closes the stream
     * @return True if the stream was (already) closed, false if an error occured
     */
    virtual bool terminate() = 0;

    /**
     * Check if the last error code indicates a retryable condition
     * @return True if error was temporary and operation should be retried
     */
    virtual bool canRetry() const;

    /**
     * Check if the last error code indicates a non blocking operation in progress
     * @return True if a non blocking operation is in progress
     */
    virtual bool inProgress() const;

    /**
     * Check if this stream is valid
     * @return True if the stream is valid, false if it's invalid or closed
     */
    virtual bool valid() const = 0;

    /**
     * Set the blocking or non-blocking operation mode of the stream
     * @param block True if I/O operations should block, false for non-blocking
     * @return True if operation was successfull, false if an error occured
     */
    virtual bool setBlocking(bool block = true);

    /**
     * Write data to a connected stream
     * @param buffer Buffer for data transfer
     * @param length Length of the buffer
     * @return Number of bytes transferred, negative if an error occurred
     */
    virtual int writeData(const void* buffer, int length) = 0;

    /**
     * Write a C string to a connected stream
     * @param str String to send over the stream
     * @return Number of bytes transferred, negative if an error occurred
     */
    int writeData(const char* str);

    /**
     * Write a String to a connected stream
     * @param str String to send over the stream
     * @return Number of bytes transferred, negative if an error occurred
     */
    inline int writeData(const String& str)
	{ return writeData(str.c_str(), str.length()); }

    /**
     * Write a Data block to a connected stream
     * @param buf DataBlock to send over the stream
     * @return Number of bytes transferred, negative if an error occurred
     */
    inline int writeData(const DataBlock& buf)
	{ return writeData(buf.data(), buf.length()); }

    /**
     * Receive data from a connected stream
     * @param buffer Buffer for data transfer
     * @param length Length of the buffer
     * @return Number of bytes transferred, negative if an error occurred
     */
    virtual int readData(void* buffer, int length) = 0;

    /**
     * Find the length of the stream if it has one
     * @return Length of the stream or zero if length is not defined
     */
    virtual int64_t length();

    /**
     * Set the stream read/write pointer
     * @param pos The seek start as enumeration
     * @param offset The number of bytes to move the pointer from starting position
     * @return The new position of the stream read/write pointer. Negative on failure
     */
    virtual int64_t seek(SeekPos pos, int64_t offset = 0);

    /**
     * Set the read/write pointer from begin of stream
     * @param offset The position in stream to move the pointer
     * @return The new position of the stream read/write pointer. Negative on failure
     */
    inline int64_t seek(int64_t offset)
	{ return seek(SeekBegin,offset); }

    /**
     * Allocate a new pair of unidirectionally pipe connected streams
     * @param reader Reference of a pointer receiving the newly allocated reading side of the pipe
     * @param writer Reference of a pointer receiving the newly allocated writing side of the pipe
     * @return True is the stream pipe was created successfully
     */
    static bool allocPipe(Stream*& reader, Stream*& writer);

    /**
     * Allocate a new pair of bidirectionally connected streams
     * @param str1 Reference of a pointer receiving the newly allocated 1st end of the pair
     * @param str2 Reference of a pointer receiving the newly allocated 2nd end of the pair
     * @return True is the stream pair was created successfully
     */
    static bool allocPair(Stream*& str1, Stream*& str2);

    /**
     * Check if operating system supports unidirectional stream pairs
     * @return True if unidirectional pipes can be created
     */
    static bool supportsPipes();

    /**
     * Check if operating system supports bidirectional stream pairs
     * @return True if bidirectional pairs can be created
     */
    static bool supportsPairs();

protected:
    /**
     * Default constructor
     */
    inline Stream()
	: m_error(0)
	{ }

    /**
     * Clear the last error code
     */
    inline void clearError()
	{ m_error = 0; }

    int m_error;
};

/**
 * An implementation of a Stream that reads and writes data in a DataBlock
 * @short A Stream that operates on DataBlocks in memory
 */
class YATE_API MemoryStream : public Stream
{
    YNOCOPY(MemoryStream); // no automatic copies please
public:
    /**
     * Constructor of an empty stream
     */
    inline MemoryStream()
	: m_offset(0)
	{ }

    /**
     * Constructor of aan initialized stream
     * @param data Initial data to be copied in the memory stream
     */
    inline MemoryStream(const DataBlock& data)
	: m_data(data), m_offset(0)
	{ }

    /**
     * Get read-only access to the DataBlock held
     * @return Const reference to the DataBlock
     */
    inline const DataBlock& data() const
	{ return m_data; }

    /**
     * Do-nothing termination handler
     * @return True to signal the stream was closed
     */
    virtual bool terminate()
	{ return true; }
    /**
     * Do-nothing validity check
     * @return True to indicate the stream is valid
     */
    virtual bool valid() const
	{ return true; }

    /**
     * Write new data to the DataBlock at current position, advance pointer
     * @param buffer Buffer of source data
     * @param len Length of data to be written
     * @return Number of bytes written, negative on error
     */
    virtual int writeData(const void* buffer, int len);

    /**
     * Get data from internal DataBlock, advance pointer
     * @param buffer Buffer for getting the data
     * @param len Length of the buffer
     * @return Number of bytes read, negative on error, zero on end of data
     */
    virtual int readData(void* buffer, int len);

    /**
     * Get the length of the stream
     * @return Length of the DataBlock in memory
     */
    virtual int64_t length()
	{ return m_data.length(); }

    /**
     * Set the read/write pointer
     * @param pos The seek start as enumeration
     * @param offset The number of bytes to move the pointer from starting position
     * @return The new position of the stream read/write pointer. Negative on failure
     */
    virtual int64_t seek(SeekPos pos, int64_t offset = 0);

protected:
    /**
     * The DataBlock holding the data in memory
     */
    DataBlock m_data;

    /**
     * The current position for read/write operation
     */
    int64_t m_offset;
};

/**
 * Class to encapsulate a system dependent file in a system independent abstraction
 * @short A stream file class
 */
class YATE_API File : public Stream
{
    YNOCOPY(File); // no automatic copies please
public:
    /**
     * Default constructor, creates a closed file
     */
    File();

    /**
     * Constructor from an existing handle
     * @param handle Operating system handle to an open file
     */
    explicit File(HANDLE handle);

    /**
     * Destructor, closes the file
     */
    virtual ~File();

    /**
     * Opens a file from the filesystem pathname
     * @param name Name of the file according to the operating system's conventions
     * @param canWrite Open the file for writing
     * @param canRead Open the file for reading
     * @param create Create the file if it doesn't exist
     * @param append Set the write pointer at the end of an existing file
     * @param binary Open the file in binary mode if applicable
     * @param pubReadable If the file is created make it public readable
     * @param pubWritable If the file is created make it public writable
     * @return True if the file was successfully opened
     */
    virtual bool openPath(const char* name, bool canWrite = false, bool canRead = true,
	bool create = false, bool append = false, bool binary = false,
	bool pubReadable = false, bool pubWritable = false);

    /**
     * Closes the file handle
     * @return True if the file was (already) closed, false if an error occured
     */
    virtual bool terminate();

    /**
     * Attach an existing handle to the file, closes any existing first
     * @param handle Operating system handle to an open file
     */
    void attach(HANDLE handle);

    /**
     * Detaches the object from the file handle
     * @return The handle previously owned by this object
     */
    HANDLE detach();

    /**
     * Get the operating system handle to the file
     * @return File handle
     */
    inline HANDLE handle() const
	{ return m_handle; }

    /**
     * Check if the last error code indicates a retryable condition
     * @return True if error was temporary and operation should be retried
     */
    virtual bool canRetry() const;

    /**
     * Check if this file is valid
     * @return True if the file is valid, false if it's invalid or closed
     */
    virtual bool valid() const;

    /**
     * Get the operating system specific handle value for an invalid file
     * @return Handle value for an invalid file
     */
    static HANDLE invalidHandle();

    /**
     * Set the blocking or non-blocking operation mode of the file
     * @param block True if I/O operations should block, false for non-blocking
     * @return True if operation was successfull, false if an error occured
     */
    virtual bool setBlocking(bool block = true);

    /**
     * Find the length of the file if it has one
     * @return Length of the file or zero if length is not defined
     */
    virtual int64_t length();

    /**
     * Set the file read/write pointer
     * @param pos The seek start as enumeration
     * @param offset The number of bytes to move the pointer from starting position
     * @return The new position of the file read/write pointer. Negative on failure
     */
    virtual int64_t seek(SeekPos pos, int64_t offset = 0);

    /**
     * Write data to an open file
     * @param buffer Buffer for data transfer
     * @param length Length of the buffer
     * @return Number of bytes transferred, negative if an error occurred
     */
    virtual int writeData(const void* buffer, int length);

    /**
     * Read data from an open file
     * @param buffer Buffer for data transfer
     * @param length Length of the buffer
     * @return Number of bytes transferred, negative if an error occurred
     */
    virtual int readData(void* buffer, int length);

    /**
     * Retrieve the file's modification time (the file must be already opened)
     * @param secEpoch File creation time (seconds since Epoch)
     * @return True on success
     */
    bool getFileTime(unsigned int& secEpoch);

    /**
     * Build the MD5 hex digest of a file. The file must be opened for read access.
     * This method will move the file pointer
     * @param buffer Destination buffer
     * @return True on success
     */
    virtual bool md5(String& buffer);

    /**
     * Set a file's modification time.
     * @param name Path and name of the file
     * @param secEpoch File modification time (seconds since Epoch)
     * @param error Optional pointer to error code to be filled on failure
     * @return True on success
     */
    static bool setFileTime(const char* name, unsigned int secEpoch, int* error = 0);

    /**
     * Retrieve a file's modification time
     * @param name Path and name of the file
     * @param secEpoch File modification time (seconds since Epoch)
     * @param error Optional pointer to error code to be filled on failure
     * @return True on success
     */
    static bool getFileTime(const char* name, unsigned int& secEpoch, int* error = 0);

    /**
     * Check if a file exists
     * @param name The file to check
     * @param error Optional pointer to error code to be filled on failure
     * @return True if the file exists
     */
    static bool exists(const char* name, int* error = 0);

    /**
     * Rename (move) a file (or directory) entry from the filesystem
     * @param oldFile Path and name of the file to rename
     * @param newFile The new path and name of the file
     * @param error Optional pointer to error code to be filled on failure
     * @return True if the file was successfully renamed (moved)
     */
    static bool rename(const char* oldFile, const char* newFile, int* error = 0);

    /**
     * Deletes a file entry from the filesystem
     * @param name Absolute path and name of the file to delete
     * @param error Optional pointer to error code to be filled on failure
     * @return True if the file was successfully deleted
     */
    static bool remove(const char* name, int* error = 0);

    /**
     * Build the MD5 hex digest of a file.
     * @param name The file to build MD5 from
     * @param buffer Destination buffer
     * @param error Optional pointer to error code to be filled on failure
     * @return True on success
     */
    static bool md5(const char* name, String& buffer, int* error = 0);

    /**
     * Create a folder (directory). It only creates the last directory in the path
     * @param path The folder path
     * @param error Optional pointer to error code to be filled on failure
     * @param mode Optional file mode, ignored on some platforms
     * @return True on success
     */
    static bool mkDir(const char* path, int* error = 0, int mode = -1);

    /**
     * Remove an empty folder (directory)
     * @param path The folder path
     * @param error Optional pointer to error code to be filled on failure
     * @return True on success
     */
    static bool rmDir(const char* path, int* error = 0);

    /**
     * Enumerate a folder (directory) content.
     * Fill the given lists with children item names
     * @param path The folder path
     * @param dirs List to be filled with child directories.
     *  It can be NULL if not requested
     * @param files List to be filled with child files.
     *  It can be NULL if not requested
     * @param error Optional pointer to error code to be filled on failure
     * @return True on success
     */
    static bool listDirectory(const char* path, ObjList* dirs, ObjList* files,
	int* error = 0);

    /**
     * Create a pair of unidirectionally pipe connected streams
     * @param reader Reference to a File that becomes the reading side of the pipe
     * @param writer Reference to a File that becomes the writing side of the pipe
     * @return True is the pipe was created successfully
     */
    static bool createPipe(File& reader, File& writer);

protected:

    /**
     * Copy the last error code from the operating system
     */
    void copyError();

    HANDLE m_handle;
};

/**
 * This class encapsulates a system dependent socket in a system independent abstraction
 * @short A generic socket class
 */
class YATE_API Socket : public Stream
{
    YNOCOPY(Socket); // no automatic copies please
public:
    /**
     * Types of service
     */
    enum TOS {
	Normal         = 0,
	LowDelay       = IPTOS_LOWDELAY,
	MaxThroughput  = IPTOS_THROUGHPUT,
	MaxReliability = IPTOS_RELIABILITY,
	MinCost        = IPTOS_MINCOST,
    };

    /**
     * DiffServ bits
     */
    enum DSCP {
	DefaultPHB     = 0x00,
	// Class selectors
	CS0            = 0x00,
	CS1            = 0x20,
	CS2            = 0x40,
	CS3            = 0x60,
	CS4            = 0x80,
	CS5            = 0xa0,
	CS6            = 0xc0,
	CS7            = 0xe0,
	// Assured forwarding
	AF11           = 0x28,
	AF12           = 0x30,
	AF13           = 0x38,
	AF21           = 0x48,
	AF22           = 0x50,
	AF23           = 0x58,
	AF31           = 0x68,
	AF32           = 0x70,
	AF33           = 0x78,
	AF41           = 0x88,
	AF42           = 0x90,
	AF43           = 0x98,
	// Expedited forwarding
	ExpeditedFwd   = 0xb8,
	VoiceAdmit     = 0xb0,
    };

    /**
     * Default constructor, creates an invalid socket
     */
    Socket();

    /**
     * Constructor from an existing handle
     * @param handle Operating system handle to an existing socket
     */
    explicit Socket(SOCKET handle);

    /**
     * Constructor that also creates the socket handle
     * @param domain Communication domain for the socket (protocol family)
     * @param type Type specification of the socket
     * @param protocol Specific protocol for the domain, 0 to use default
     */
    Socket(int domain, int type, int protocol = 0);

    /**
     * Destructor - closes the handle if still open
     */
    virtual ~Socket();

    /**
     * Creates a new socket handle,
     * @param domain Communication domain for the socket (protocol family)
     * @param type Type specification of the socket
     * @param protocol Specific protocol for the domain, 0 to use default
     * @return True if socket was created, false if an error occured
     */
    virtual bool create(int domain, int type, int protocol = 0);

    /**
     * Closes the socket handle, terminates the connection
     * @return True if socket was (already) closed, false if an error occured
     */
    virtual bool terminate();

    /**
     * Attach an existing handle to the socket, closes any existing first
     * @param handle Operating system handle to an existing socket
     */
    void attach(SOCKET handle);

    /**
     * Detaches the object from the socket handle
     * @return The handle previously owned by this object
     */
    SOCKET detach();

    /**
     * Get the operating system handle to the socket
     * @return Socket handle
     */
    inline SOCKET handle() const
	{ return m_handle; }

    /**
     * Check if the last error code indicates a retryable condition
     * @return True if error was temporary and operation should be retried
     */
    virtual bool canRetry() const;

    /**
     * Check if the last error code indicates a non blocking operation in progress
     * @return True if a non blocking operation is in progress
     */
    virtual bool inProgress() const;

    /**
     * Check if this socket is valid
     * @return True if the handle is valid, false if it's invalid
     */
    virtual bool valid() const;

    /**
     * Get the operating system specific handle value for an invalid socket
     * @return Handle value for an invalid socket
     */
    static SOCKET invalidHandle();

    /**
     * Get the operating system specific return value of a failed operation
     * @return Return value of a failed socket operation
     */
    static int socketError();

    /**
     * Retrieve the keyword lookup table for TOS / DSCP values
     * @return Pointer to keyword dictionary for TOS and DSCP
     */
    static const TokenDict* tosValues();

    /**
     * Set socket options
     * @param level Level of the option to set
     * @param name Socket option for which the value is to be set
     * @param value Pointer to a buffer holding the value for the requested option
     * @param length Size of the supplied buffer
     * @return True if operation was successfull, false if an error occured
     */
    virtual bool setOption(int level, int name, const void* value = 0, socklen_t length = 0);

    /**
     * Set or reset socket IPv6 only option.
     * This option will tell to an IPv6 socket to accept only IPv6 packets.
     * IPv4 packets will be accepted if disabled.
     * This method will fail for non PF_INET6 sockets
     * @param on True to set, false to reset it
     * @return True if operation was successfull, false if an error occured
     */
    inline bool setIpv6OnlyOption(bool on) {
#if defined(IPPROTO_IPV6) && defined(IPV6_V6ONLY)
	    int value = on ? 1 : 0;
	    return setOption(IPPROTO_IPV6,IPV6_V6ONLY,&value,sizeof(value));
#else
	    return false;
#endif
	}

    /**
     * Get socket options
     * @param level Level of the option to set
     * @param name Socket option for which the value is to be set
     * @param buffer Pointer to a buffer to return the value for the requested option
     * @param length Pointer to size of the supplied buffer, will be filled on return
     * @return True if operation was successfull, false if an error occured
     */
    virtual bool getOption(int level, int name, void* buffer, socklen_t* length);

    /**
     * Set specific socket parameters.
     * @param params List of parameters
     */
    virtual bool setParams(const NamedList& params)
	{ return false; }

    /**
     * Get specific socket parameters.
     * @param params Coma separated list of parameters to obtain
     * @param result List of parameters to fill
     * @return True if operation was successful, false if an error occurred
     */
    virtual bool getParams(const String& params, NamedList& result)
	{ return false; }

    /**
     * Set the Type of Service or Differentiated Services Code Point on the IP level of this socket
     * @param tos New TOS or DiffServ bits
     * @return True if operation was successfull, false if an error occured
     */
    virtual bool setTOS(int tos);

    /**
     * Set the Type of Service or Differentiated Services Code Point on the IP level of this socket
     * @param tos Keyword describing new TOS or DSCP value
     * @param defTos Default TOS or DiffServ value to set if the keyword is not recognized
     * @return True if operation was successfull, false if an error occured
     */
    inline bool setTOS(const char* tos, int defTos = Normal)
	{ return setTOS(lookup(tos,tosValues(),defTos)); }

    /**
     * Retrieve the TOS / DSCP on the IP level of this socket
     * @return TOS or DiffServ value, Normal if not supported or an error occured
     */
    virtual int getTOS();

    /**
     * Set the blocking or non-blocking operation mode of the socket
     * @param block True if I/O operations should block, false for non-blocking
     * @return True if operation was successfull, false if an error occured
     */
    virtual bool setBlocking(bool block = true);

    /**
     * Set the local address+port reuse flag of the socket.
     * This method should be called before bind() or it will have no effect.
     * @param reuse True if other sockets may listen on same address+port
     * @param exclusive Grant exclusive access to the address
     * @return True if operation was successfull, false if an error occured
     */
    virtual bool setReuse(bool reuse = true, bool exclusive = false);

    /**
     * Set the way closing a socket is handled
     * @param seconds How much to block waiting for socket to close,
     *  negative to no wait (close in background), zero to reset connection
     * @return True if operation was successfull, false if an error occured
     */
    virtual bool setLinger(int seconds = -1);

    /**
     * Associates the socket with a local address
     * @param addr Address to assign to this socket
     * @param addrlen Length of the address structure
     * @return True if operation was successfull, false if an error occured
     */
    virtual bool bind(struct sockaddr* addr, socklen_t addrlen);

    /**
     * Associates the socket with a local address
     * @param addr Address to assign to this socket
     * @return True if operation was successfull, false if an error occured
     */
    inline bool bind(const SocketAddr& addr)
	{ return bind(addr.address(), addr.length()); }

    /**
     * Start listening for incoming connections on the socket
     * @param backlog Maximum length of the queue of pending connections, 0 for system maximum
     * @return True if operation was successfull, false if an error occured
     */
    virtual bool listen(unsigned int backlog = 0);

    /**
     * Create a new socket for an incoming connection attempt on a listening socket
     * @param addr Address to fill in with the address of the incoming connection
     * @param addrlen Length of the address structure on input, length of address data on return
     * @return Open socket to the new connection or NULL on failure
     */
    virtual Socket* accept(struct sockaddr* addr = 0, socklen_t* addrlen = 0);

    /**
     * Create a new socket for an incoming connection attempt on a listening socket
     * @param addr Address to fill in with the address of the incoming connection
     * @return Open socket to the new connection or NULL on failure
     */
    Socket* accept(SocketAddr& addr);

    /**
     * Create a new socket for an incoming connection attempt on a listening socket
     * @param addr Address to fill in with the address of the incoming connection
     * @param addrlen Length of the address structure on input, length of address data on return
     * @return Operating system handle to the new connection or @ref invalidHandle() on failure
     */
    SOCKET acceptHandle(struct sockaddr* addr = 0, socklen_t* addrlen = 0);

    /**
     * Update socket error from socket options.
     * This method should be called when select() indicates a non blocking operation
     *  completed.
     * Note: if false is returned, the socket error is the reason of getOption() failure
     * @return Return true on success
     */
    bool updateError();

    /**
     * Check if select() is efficient on this platform and worth using frequently
     * @return True if select() is efficiently implemented
     */
    static bool efficientSelect();

    /**
     * Check if a socket handle can be used in select
     * @param handle The socket handle to check
     * @return True if the socket handle can be safely used in select
     */
    static bool canSelect(SOCKET handle);

    /**
     * Check if this socket object can be used in a select
     * @return True if this socket can be safely used in select
     */
    virtual bool canSelect() const;

    /**
     * Connects the socket to a remote address
     * @param addr Address to connect to
     * @param addrlen Length of the address structure
     * @return True if operation was successfull, false if an error occured
     */
    virtual bool connect(struct sockaddr* addr, socklen_t addrlen);

    /**
     * Connects the socket to a remote address
     * @param addr Socket address to connect to
     * @return True if operation was successfull, false if an error occured
     */
    inline bool connect(const SocketAddr& addr)
	{ return connect(addr.address(), addr.length()); }

    /**
     * Asynchronously connects the socket to a remote address.
     * The socket must be selectable and in non-blocking operation mode
     * @param addr Address to connect to
     * @param addrlen Length of the address structure
     * @param toutUs Timeout interval in microseconds
     * @param timeout Optional boolean flag to signal timeout
     * @return True on success
     */
    virtual bool connectAsync(struct sockaddr* addr, socklen_t addrlen, unsigned int toutUs,
	bool* timeout = 0);

    /**
     * Asynchronously connects the socket to a remote address.
     * The socket must be selectable and in non-blocking operation mode
     * @param addr Socket address to connect to
     * @param toutUs Timeout interval in microseconds
     * @param timeout Optional boolean flag to signal timeout
     * @return True on success
     */
    inline bool connectAsync(const SocketAddr& addr, unsigned int toutUs,
	bool* timeout = 0)
	{ return connectAsync(addr.address(),addr.length(),toutUs,timeout); }

    /**
     * Shut down one or both directions of a full-duplex socket.
     * @param stopReads Request to shut down the read side of the socket
     * @param stopWrites Request to shut down the write side of the socket
     * @return True if operation was successfull, false if an error occured
     */
    virtual bool shutdown(bool stopReads, bool stopWrites);

    /**
     * Retrieve the address of the local socket of a connection
     * @param addr Address to fill in with the address of the local socket
     * @param addrlen Length of the address structure on input, length of address data on return
     * @return True if operation was successfull, false if an error occured
     */
    virtual bool getSockName(struct sockaddr* addr, socklen_t* addrlen);

    /**
     * Retrieve the address of the local socket of a connection
     * @param addr Address to fill in with the address of the local socket
     * @return True if operation was successfull, false if an error occured
     */
    bool getSockName(SocketAddr& addr);

    /**
     * Retrieve the address of the remote socket of a connection
     * @param addr Address to fill in with the address of the remote socket
     * @param addrlen Length of the address structure on input, length of address data on return
     * @return True if operation was successfull, false if an error occured
     */
    virtual bool getPeerName(struct sockaddr* addr, socklen_t* addrlen);

    /**
     * Retrieve the address of the remote socket of a connection
     * @param addr Address to fill in with the address of the remote socket
     * @return True if operation was successfull, false if an error occured
     */
    bool getPeerName(SocketAddr& addr);

    /**
     * Send a message over a connected or unconnected socket
     * @param buffer Buffer for data transfer
     * @param length Length of the buffer
     * @param addr Address to send the message to, if NULL will behave like @ref send()
     * @param adrlen Length of the address structure
     * @param flags Operating system specific bit flags that change the behaviour
     * @return Number of bytes transferred, @ref socketError() if an error occurred
     */
    virtual int sendTo(const void* buffer, int length, const struct sockaddr* addr, socklen_t adrlen, int flags = 0);

    /**
     * Send a message over a connected or unconnected socket
     * @param buffer Buffer for data transfer
     * @param length Length of the buffer
     * @param addr Address to send the message to
     * @param flags Operating system specific bit flags that change the behaviour
     * @return Number of bytes transferred, @ref socketError() if an error occurred
     */
    inline int sendTo(const void* buffer, int length, const SocketAddr& addr, int flags = 0)
	{ return sendTo(buffer, length, addr.address(), addr.length(), flags); }

    /**
     * Send a message over a connected socket
     * @param buffer Buffer for data transfer
     * @param length Length of the buffer
     * @param flags Operating system specific bit flags that change the behaviour
     * @return Number of bytes transferred, @ref socketError() if an error occurred
     */
    virtual int send(const void* buffer, int length, int flags = 0);

    /**
     * Write data to a connected stream socket
     * @param buffer Buffer for data transfer
     * @param length Length of the buffer
     * @return Number of bytes transferred, @ref socketError() if an error occurred
     */
    virtual int writeData(const void* buffer, int length);

    /**
     * Receive a message from a connected or unconnected socket
     * @param buffer Buffer for data transfer
     * @param length Length of the buffer
     * @param addr Address to fill in with the address of the incoming data
     * @param adrlen Length of the address structure on input, length of address data on return
     * @param flags Operating system specific bit flags that change the behaviour
     * @return Number of bytes transferred, @ref socketError() if an error occurred
     */
    virtual int recvFrom(void* buffer, int length, struct sockaddr* addr = 0, socklen_t* adrlen = 0, int flags = 0);

    /**
     * Receive a message from a connected or unconnected socket
     * @param buffer Buffer for data transfer
     * @param length Length of the buffer
     * @param addr Address to fill in with the address of the incoming data
     * @param flags Operating system specific bit flags that change the behaviour
     * @return Number of bytes transferred, @ref socketError() if an error occurred
     */
    int recvFrom(void* buffer, int length, SocketAddr& addr, int flags = 0);

    /**
     * Receive a message from a connected socket
     * @param buffer Buffer for data transfer
     * @param length Length of the buffer
     * @param flags Operating system specific bit flags that change the behaviour
     * @return Number of bytes transferred, @ref socketError() if an error occurred
     */
    virtual int recv(void* buffer, int length, int flags = 0);

    /**
     * Receive data from a connected stream socket
     * @param buffer Buffer for data transfer
     * @param length Length of the buffer
     * @return Number of bytes transferred, @ref socketError() if an error occurred
     */
    virtual int readData(void* buffer, int length);

    /**
     * Determines the availability to perform synchronous I/O of the socket
     * @param readok Address of a boolean variable to fill with readability status
     * @param writeok Address of a boolean variable to fill with writeability status
     * @param except Address of a boolean variable to fill with exceptions status
     * @param timeout Maximum time until the method returns, NULL for blocking
     * @return True if operation was successfull, false if an error occured
     */
    virtual bool select(bool* readok, bool* writeok, bool* except, struct timeval* timeout = 0);

    /**
     * Determines the availability to perform synchronous I/O of the socket
     * @param readok Address of a boolean variable to fill with readability status
     * @param writeok Address of a boolean variable to fill with writeability status
     * @param except Address of a boolean variable to fill with exceptions status
     * @param timeout Maximum time until the method returns, -1 for blocking
     * @return True if operation was successfull, false if an error occured
     */
    bool select(bool* readok, bool* writeok, bool* except, int64_t timeout);

    /**
     * Install a new packet filter in the socket
     * @param filter Pointer to the packet filter to install
     * @return True if the filter was installed
     */
    bool installFilter(SocketFilter* filter);

    /**
     * Removes a packet filter and optionally destroys it
     * @param filter Pointer to the packet filter to remove from socket
     * @param delobj Set to true to also delete the filter
     */
    void removeFilter(SocketFilter* filter, bool delobj = false);

    /**
     * Removes and destroys all packet filters
     */
    void clearFilters();

    /**
     * Run whatever actions required on idle thread runs.
     * The default implementation calls @ref SocketFilter::timerTick()
     *  for all installed filters.
     * @param when Time when the idle run started
     */
    virtual void timerTick(const Time& when);

    /**
     * Create a pair of bidirectionally connected sockets
     * @param sock1 Reference to first Socket to be paired
     * @param sock2 Reference to second Socket to be paired
     * @param domain Communication domain for the sockets (protocol family)
     * @return True is the stream pair was created successfully
     */
    static bool createPair(Socket& sock1, Socket& sock2, int domain = AF_UNIX);

protected:

    /**
     * Copy the last error code from the operating system
     */
    void copyError();

    /**
     * Copy the last error code from the operating system if an error occured, clear if not
     * @param retcode Operation return code to check, 0 for success
     * @param strict True to consider errors only return codes of @ref socketError()
     * @return True if operation succeeded (retcode == 0), false otherwise
     */
    bool checkError(int retcode, bool strict = false);

    /**
     * Apply installed filters to a received block of data
     * @param buffer Buffer for received data
     * @param length Length of the data in buffer
     * @param flags Operating system specific bit flags of the operation
     * @param addr Address of the incoming data, may be NULL
     * @param adrlen Length of the valid data in address structure
     * @return True if one of the filters claimed the data
     */
    bool applyFilters(void* buffer, int length, int flags, const struct sockaddr* addr = 0, socklen_t adrlen = 0);

    SOCKET m_handle;
    ObjList m_filters;
};

/**
 * The SctpSocket interface provides access to SCTP specific functions
 * @short Abstract SCTP Socket
 */
class YATE_API SctpSocket : public Socket
{
    YNOCOPY(SctpSocket); // no automatic copies please
public:
    /**
     * Constructor
     */
     inline SctpSocket()
	{ }

    /**
     * Constructor
     * @param fd File descriptor of an existing handle
     */
    inline explicit SctpSocket(SOCKET fd)
	: Socket(fd)
	{ }

    /**
     * Destructor
     */
    virtual ~SctpSocket();

    /**
     * Bind this socket to multiple addresses
     * @param addresses The list of addresses (SocketAddr)
     * @return True if the socket bind succeded
     */
    virtual bool bindx(ObjList& addresses) = 0;

    /**
     * Connect this socket to multiple addresses
     * @param addresses the list of addresses (SocketAddr)
     * @return True if the socket connect succeded
     */
    virtual bool connectx(ObjList& addresses) = 0;

    /**
     * Send a message over a connected or unconnected socket
     * @param buffer Buffer for data transfer
     * @param length Length of the buffer
     * @param stream The stream number
     * @param addr Address to send the message to, if NULL will behave like @ref send()
     * @param flags Operating system specific bit flags that change the behaviour
     * @return Number of bytes transferred, @ref socketError() if an error occurred
     */
    virtual int sendTo(void* buffer, int length, int stream, SocketAddr& addr, int flags) = 0;

    /**
     * Accept an incoming connection
     * @param addr The socket address of the incoming connection
     * @return A new SctpSocket if an incoming connection was detected
     */
    virtual Socket* accept(SocketAddr& addr)
	{ return 0; }

    /**
     * Send a buffer of data over a connected socket
     * @param buf The data to send
     * @param length Data length
     * @param stream The stream number to send over
     * @param flags Flags, gets altered on return
     * @return The number of bytes sent
     */
    virtual int sendMsg(const void* buf, int length, int stream, int& flags) = 0;

    /**
     * Receive data from a connected socket
     * @param buf The buffer where the data will be stored
     * @param length The buffer length
     * @param addr Gets the remote address from which the data was received
     * @param stream Gets the stream number on which the data was read
     * @param flags Flags, gets altered on return
     * @return The number of bytes read
     */
    virtual int recvMsg(void* buf, int length, SocketAddr& addr, int& stream, int& flags) = 0;

    /**
     * Set the number of streams
     * @param inbound The number of inbound streams
     * @param outbound The number of outbound streams
     * @return True if the number of streams was set
     */
    virtual bool setStreams(int inbound, int outbound) = 0;

    /**
     * Subscribe to SCTP events
     * This method should be called if we need to find from which stream the data came
     * @return True if subscription has succeeded
     */
    virtual bool subscribeEvents() = 0;

    /**
     * Get the number of negotiated streams
     * @param inbound Number of inbound streams
     * @param outbound Number of outbound streams
     * @return True if operation has succeded
     */
    virtual bool getStreams(int& inbound, int& outbound) = 0;

    /**
     * Set the SCTP payload protocol identifier (RFC 4960)
     * @param payload Payload identifier code
     * @return True if set successfully
     */
    virtual bool setPayload(u_int32_t payload) = 0;
};

/**
 * This class holds a DNS (resolver) record
 * @short A DNS record
 */
class YATE_API DnsRecord : public GenObject
{
    YCLASS(DnsRecord,GenObject)
    YNOCOPY(DnsRecord);
public:
    /**
     * Build a DNS record
     * @param ttl Record Time To Live
     * @param order Record order (priority)
     * @param pref Record preference
     */
    inline DnsRecord(int ttl, int order, int pref)
	: m_ttl(ttl), m_order(order), m_pref(pref)
	{}

    /**
     * Default constructor
     */
    inline DnsRecord()
	: m_order(0), m_pref(0)
	{}

    /**
     * Retrieve the Time To Live
     * @return Record TTL
     */
    inline int ttl() const
	{ return m_ttl; }

    /**
     * Retrieve the record order
     * @return Record order
     */
    inline int order() const
	{ return m_order; }

    /**
     * Retrieve the record preference
     * @return Record preference
     */
    inline int pref() const
	{ return m_pref; }

    /**
     * Dump a record for debug purposes
     * @param buf Destination buffer
     * @param sep Fields separator
     */
    virtual void dump(String& buf, const char* sep = " ");

    /**
     * Insert a DnsRecord into a list in the proper location given by order and preference
     * @param list Destination list
     * @param rec The item to insert
     * @param ascPref Order preference ascending
     * @return True on success, false on failure (already in the list)
     */
    static bool insert(ObjList& list, DnsRecord* rec, bool ascPref);

protected:
    int m_ttl;
    int m_order;
    int m_pref;
};

/**
 * This class holds a A, AAAA or TXT record from DNS
 * @short A text based DNS record
 */
class YATE_API TxtRecord : public DnsRecord
{
    YCLASS(TxtRecord,DnsRecord)
    YNOCOPY(TxtRecord);
public:
    /**
     * Build a TXT record
     * @param ttl Record Time To Live
     * @param text Text content of the record
     */
    inline TxtRecord(int ttl, const char* text)
	: DnsRecord(ttl,-1,-1), m_text(text)
	{}

    /**
     * Retrieve the record text
     * @return Record text
     */
    inline const String& text() const
	{ return m_text; }

    /**
     * Dump this record for debug purposes
     * @param buf Destination buffer
     * @param sep Fields separator
     */
    virtual void dump(String& buf, const char* sep = " ");

    /**
     * Copy a TxtRecord list into another one
     * @param dest Destination list
     * @param src Source list
     */
    static void copy(ObjList& dest, const ObjList& src);

protected:
    String m_text;

private:
    TxtRecord() {}                       // No default contructor
};

/**
 * This class holds a SRV (Service Location) record
 * @short A SRV record
 */
class YATE_API SrvRecord : public DnsRecord
{
    YCLASS(SrvRecord,DnsRecord)
    YNOCOPY(SrvRecord);
public:
    /**
     * Build a SRV record
     * @param ttl Record Time To Live
     * @param prio Record priority (order)
     * @param weight Record weight (preference)
     * @param addr Record address
     * @param port Record port
     */
    inline SrvRecord(int ttl, int prio, int weight, const char* addr, int port)
	: DnsRecord(ttl,prio,weight), m_address(addr), m_port(port)
	{}

    /**
     * Retrieve the record address
     * @return Record address
     */
    inline const String& address() const
	{ return m_address; }

    /**
     * Retrieve the record port
     * @return Record port
     */
    inline int port() const
	{ return m_port; }

    /**
     * Dump this record for debug purposes
     * @param buf Destination buffer
     * @param sep Fields separator
     */
    virtual void dump(String& buf, const char* sep = " ");

    /**
     * Copy a SrvRecord list into another one
     * @param dest Destination list
     * @param src Source list
     */
    static void copy(ObjList& dest, const ObjList& src);

protected:
    String m_address;
    int m_port;

private:
    SrvRecord() {}                       // No default contructor
};

/**
 * This class holds a NAPTR (Naming Authority Pointer) record
 * @short A NAPTR record
 */
class YATE_API NaptrRecord : public DnsRecord
{
    YCLASS(NaptrRecord,DnsRecord)
    YNOCOPY(NaptrRecord);
public:
    /**
     * Build a NAPTR record
     * @param ttl Record Time To Live
     * @param ord Record order
     * @param pref Record preference
     * @param flags Interpretation flags
     * @param serv Available services
     * @param regexp Substitution expression
     * @param next Next name to query
     */
    NaptrRecord(int ttl, int ord, int pref, const char* flags, const char* serv,
	const char* regexp, const char* next);

    /**
     * Replace the enclosed template in a given string if matching
     *  the substitution expression
     * @param str String to replace
     * @return True on success
     */
    bool replace(String& str) const;

    /**
     * Dump this record for debug purposes
     * @param buf Destination buffer
     * @param sep Fields separator
     */
    virtual void dump(String& buf, const char* sep = " ");

    /**
     * Retrieve record interpretation flags
     * @return Record interpretation flags
     */
    inline const String& flags() const
	{ return m_flags; }

    /**
     * Retrieve available services
     * @return Available services
     */
    inline const String& serv() const
	{ return m_service; }

    /**
     * Retrieve the regular expression match
     * @return Regular expression used in match
     */
    inline const Regexp& regexp() const
	{ return m_regmatch; }

    /**
     * Retrieve the template for replacing
     * @return Template used to replace the match
     */
    inline const String& repTemplate() const
	{ return m_template; }

    /**
     * Retrieve the next domain name to query
     * @return The next domain to query
     */
    inline const String& nextName() const
	{ return m_next; }

protected:
    String m_flags;
    String m_service;
    Regexp m_regmatch;
    String m_template;
    String m_next;

private:
    NaptrRecord() {}                     // No default contructor
};

/**
 * This class offers DNS query services
 * @short DNS services
 */
class YATE_API Resolver
{
public:
    /**
     * Resolver handled types
     */
    enum Type {
	Unknown,
	Srv,                             // SRV (Service Location)
	Naptr,                           // NAPTR (Naming Authority Pointer)
	A4,                              // A (Address)
	A6,                              // AAAA (IPv6 Address)
	Txt,                             // TXT (Text)
    };

    /**
     * Runtime check for resolver availability
     * @param type Optional type to check. Set it to Unknown (default) to check
     *  general resolver availability
     * @return True if the resolver is available on current platform
     */
    static bool available(Type type = Unknown);

    /**
     * Initialize the resolver in the current thread
     * @param timeout Query timeout. Negative to use default
     * @param retries The number of query retries. Negative to use default
     * @return True on success
     */
    static bool init(int timeout = -1, int retries = -1);

    /**
     * Make a query
     * @param type Query type as enumeration
     * @param dname Domain to query
     * @param result List of resulting record items
     * @param error Optional string to be filled with error string
     * @return 0 on success, error code otherwise (h_errno value on Linux)
     */
    static int query(Type type, const char* dname, ObjList& result, String* error = 0);

    /**
     * Make a SRV (Service Location) query
     * @param dname Domain to query
     * @param result List of resulting SrvRecord items
     * @param error Optional string to be filled with error string
     * @return 0 on success, error code otherwise (h_errno value on Linux)
     */
    static int srvQuery(const char* dname, ObjList& result, String* error = 0);

    /**
     * Make a NAPTR (Naming Authority Pointer) query
     * @param dname Domain to query
     * @param result List of resulting NaptrRecord items
     * @param error Optional string to be filled with error string
     * @return 0 on success, error code otherwise (h_errno value on Linux)
     */
    static int naptrQuery(const char* dname, ObjList& result, String* error = 0);

    /**
     * Make an A (IPv4 Address) query
     * @param dname Domain to query
     * @param result List of resulting TxtRecord items
     * @param error Optional string to be filled with error string
     * @return 0 on success, error code otherwise (h_errno value on Linux)
     */
    static int a4Query(const char* dname, ObjList& result, String* error = 0);

    /**
     * Make an AAAA (IPv6 Address) query
     * @param dname Domain to query
     * @param result List of resulting TxtRecord items
     * @param error Optional string to be filled with error string
     * @return 0 on success, error code otherwise (h_errno value on Linux)
     */
    static int a6Query(const char* dname, ObjList& result, String* error = 0);

    /**
     * Make a TXT (Text) query
     * @param dname Domain to query
     * @param result List of resulting TxtRecord items
     * @param error Optional string to be filled with error string
     * @return 0 on success, error code otherwise (h_errno value on Linux)
     */
    static int txtQuery(const char* dname, ObjList& result, String* error = 0);

    /**
     * Resolver type names
     */
    static const TokenDict s_types[];
};

/**
 * The Cipher class provides an abstraction for data encryption classes
 * @short An abstract cipher
 */
class YATE_API Cipher : public GenObject
{
public:
    /**
     * Cipher direction
     */
    enum Direction {
	Bidir,
	Encrypt,
	Decrypt,
    };

    /**
     * Get the dictionary of cipher directions
     * @return Pointer to the dictionary of cipher directions
     */
    inline static const TokenDict* directions()
	{ return s_directions; }

    /**
     * Get a direction from the dictionary given the name
     * @param name Name of the direction
     * @param defdir Default direction to return if name is empty or unknown
     * @return Direction associated with the given name
     */
    inline static Direction direction(const char* name, Direction defdir = Bidir)
	{ return (Direction)TelEngine::lookup(name,s_directions,defdir); }

    /**
     * Destructor
     */
    virtual ~Cipher();

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Check if the cipher instance is valid for a specific direction
     * @param dir Direction to check
     * @return True if the cipher is able to perform operation on given direction
     */
    virtual bool valid(Direction dir = Bidir) const;

    /**
     * Get the cipher block size
     * @return Cipher block size in bytes
     */
    virtual unsigned int blockSize() const = 0;

    /**
     * Get the initialization vector size
     * @return Initialization vector size in bytes, 0 if not applicable
     */
    virtual unsigned int initVectorSize() const;

    /**
     * Round up a buffer length to a multiple of block size
     * @param len Length of data to encrypt or decrypt in bytes
     * @return Length of required buffer in bytes
     */
    unsigned int bufferSize(unsigned int len) const;

    /**
     * Check if a buffer length is multiple of block size
     * @param len Length of data to encrypt or decrypt in bytes
     * @return True if buffer length is multiple of block size
     */
    bool bufferFull(unsigned int len) const;

    /**
     * Set the key required to encrypt or decrypt data
     * @param key Pointer to binary key data
     * @param len Length of key in bytes
     * @param dir Direction to set key for
     * @return True if the key was set successfully
     */
    virtual bool setKey(const void* key, unsigned int len, Direction dir = Bidir) = 0;

    /**
     * Set the key required to encrypt or decrypt data
     * @param key Binary key data block
     * @param dir Direction to set key for
     * @return True if the key was set successfully
     */
    inline bool setKey(const DataBlock& key, Direction dir = Bidir)
	{ return setKey(key.data(),key.length(),dir); }

    /**
     * Set the Initialization Vector if applicable
     * @param vect Pointer to binary Initialization Vector data
     * @param len Length of Initialization Vector in bytes
     * @param dir Direction to set the Initialization Vector for
     * @return True if the Initialization Vector was set successfully
     */
    virtual bool initVector(const void* vect, unsigned int len, Direction dir = Bidir);

    /**
     * Set the Initialization Vector is applicable
     * @param vect Binary Initialization Vector
     * @param dir Direction to set the Initialization Vector for
     * @return True if the Initialization Vector was set successfully
     */
    inline bool initVector(const DataBlock& vect, Direction dir = Bidir)
	{ return initVector(vect.data(),vect.length(),dir); }

    /**
     * Encrypt data
     * @param outData Pointer to buffer for output (encrypted) and possibly input data
     * @param len Length of output data, may not be multiple of block size
     * @param inpData Pointer to buffer containing input (unencrypted) data, NULL to encrypt in place
     * @return True if data was successfully encrypted
     */
    virtual bool encrypt(void* outData, unsigned int len, const void* inpData = 0) = 0;

    /**
     * Encrypt a DataBlock in place
     * @param data Data block to encrypt
     * @return True if data was successfully encrypted
     */
    inline bool encrypt(DataBlock& data)
	{ return encrypt(data.data(),data.length()); }

    /**
     * Decrypt data
     * @param outData Pointer to buffer for output (decrypted) and possibly input data
     * @param len Length of output data, may not be multiple of block size
     * @param inpData Pointer to buffer containing input (encrypted) data, NULL to decrypt in place
     * @return True if data was successfully decrypted
     */
    virtual bool decrypt(void* outData, unsigned int len, const void* inpData = 0) = 0;

    /**
     * Decrypt a DataBlock in place
     * @param data Data block to decrypt
     * @return True if data was successfully decrypted
     */
    inline bool decrypt(DataBlock& data)
	{ return decrypt(data.data(),data.length()); }

private:
    static const TokenDict s_directions[];
};

/**
 * The Compressor class provides an abstraction for data (de)compressor classes.
 * The String component keeps an optional object name to be used for debug purposes
 * @short An abstract data (de)compressor
 */
class YATE_API Compressor : public String
{
    YCLASS(Compressor,String)
    YNOCOPY(Compressor); // no automatic copies please
public:
    /**
     * Constructor
     * @param format Compression format
     * @param name Optional object name
     */
    inline Compressor(const char* format, const char* name = 0)
	: String(name), m_format(format)
	{}

    /**
     * Destructor
     */
    virtual ~Compressor()
	{}

    /**
     * Retrieve (de)compressor format
     * @return The format of this (de)compressor
     */
    inline const String& format() const
	{ return m_format; }

    /**
     * Initialize
     * @param comp True to initialize compressor
     * @param decomp True to initialize decompressor
     * @param params Optional parameters
     * @return True on success
     */
    virtual bool init(bool comp = true, bool decomp = true,
	const NamedList& params = NamedList::empty())
	{ return true; }

    /**
     * Finalize the (de)compression
     * @param comp True to finalize compression, false to finalize decompression
     */
    virtual void finalize(bool comp)
	{}

    /**
     * Compress the input buffer, flush all pending data,
     *  append compressed data to the received data block
     * @param buf Pointer to input data
     * @param len Length of input in bytes
     * @param dest Destination buffer
     * @return The number of bytes wrote to compressor, negative on error
     */
    virtual int compress(const void* buf, unsigned int len, DataBlock& dest);

    /**
     * Decompress the input buffer, flush all pending data,
     *  append decompressed data to the received data block
     * @param buf Pointer to input data
     * @param len Length of input in bytes
     * @param dest Destination buffer
     * @return The number of bytes wrote to decompressor, negative on error
     */
    virtual int decompress(const void* buf, unsigned int len, DataBlock& dest);

    /**
     * Push data to compressor. Flush compressor input if input buffer is NULL
     *  or the length is 0 and flush is true
     * @param buf Pointer to input data
     * @param len Length of input in bytes
     * @param flush True to compress all now, false to let the compressor accumulate
     *  more data for better compression
     * @return The number of bytes written, negative on error. An incomplete write may occur
     *  if the output buffer is full
     */
    virtual int writeComp(const void* buf, unsigned int len, bool flush) = 0;

    /**
     * Push data to compressor
     * @param data Input data block
     * @param flush True to compress all now, false to let the compressor accumulate
     *  more data for better compression
     * @return The number of bytes written, negative on error. An incomplete write may occur
     *  if the output buffer is full
     */
    inline int writeComp(const DataBlock& data, bool flush)
	{ return writeComp(data.data(),data.length(),flush); }

    /**
     * Push data to compressor
     * @param data Input string
     * @param flush True to compress all now, false to let the compressor accumulate
     *  more data for better compression
     * @return The number of bytes written, negative on error. An incomplete write may occur
     *  if the output buffer is full
     */
    inline int writeComp(const String& data, bool flush)
	{ return writeComp(data.c_str(),data.length(),flush); }

    /**
     * Read data from compressor. Append it to 'buf'
     * @param buf Destination data block
     * @param flush True to flush all compressor input data
     * @return The number of bytes read, negative on error
     */
    virtual int readComp(DataBlock& buf, bool flush) = 0;

    /**
     * Push data to decompressor
     * @param buf Pointer to input data
     * @param len Length of input in bytes
     * @param flush True to try to decompress all data
     * @return The number of bytes written, negative on error. An incomplete write may occur
     *  if the output buffer is full
     */
    virtual int writeDecomp(const void* buf, unsigned int len, bool flush) = 0;

    /**
     * Push data to decompressor
     * @param data Input data block
     * @param flush True to try to decompress all data
     * @return The number of bytes written, negative on error. An incomplete write may occur
     *  if the output buffer is full
     */
    inline int writeDecomp(const DataBlock& data, bool flush)
	{ return writeDecomp(data.data(),data.length(),flush); }

    /**
     * Push data to decompressor
     * @param data Input string
     * @param flush True to try to decompress all data
     * @return The number of bytes written, negative on error. An incomplete write may occur
     *  if the output buffer is full
     */
    inline int writeDecomp(const String& data, bool flush)
	{ return writeDecomp(data.c_str(),data.length(),flush); }

    /**
     * Read data from decompressor. Append it to 'buf'
     * @param buf Destination data block
     * @param flush True to flush all decompressor input data
     * @return The number of bytes read, negative on error
     */
    virtual int readDecomp(DataBlock& buf, bool flush) = 0;

protected:
    String m_format;
};

/**
 * The SysUsage class allows collecting some statistics about engine's usage
 *  of system resources
 * @short A class exposing system resources usage
 */
class YATE_API SysUsage
{
public:
    /**
     * Type of time usage requested
     */
    enum Type {
	WallTime,
	UserTime,
	KernelTime
    };

    /**
     * Initialize the system start variable
     */
    static void init();

    /**
     * Get the wall time used as start for the usage time
     * @return Time of the first direct or implicit call of @ref init()
     */
    static u_int64_t startTime();

    /**
     * Get the program's running time in microseconds
     * @param type Type of running time requested
     * @return Time in microseconds since the start of the program
     */
    static u_int64_t usecRunTime(Type type = WallTime);

    /**
     * Get the program's running time in milliseconds
     * @param type Type of running time requested
     * @return Time in milliseconds since the start of the program
     */
    static u_int64_t msecRunTime(Type type = WallTime);

    /**
     * Get the program's running time in seconds
     * @param type Type of running time requested
     * @return Time in seconds since the start of the program
     */
    static u_int32_t secRunTime(Type type = WallTime);

    /**
     * Get the program's running time in seconds
     * @param type Type of running time requested
     * @return Time in seconds since the start of the program
     */
    static double runTime(Type type = WallTime);

};

}; // namespace TelEngine

#endif /* __YATECLASS_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
