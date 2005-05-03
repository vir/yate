/**
 * yateclass.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Base classes and types, not related to the engine or telephony
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

#ifndef __YATECLASS_H
#define __YATECLASS_H

#ifndef __cplusplus
#error C++ is required
#endif

#include <sys/types.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>

#ifndef _WINDOWS
#ifdef WIN32
#define _WINDOWS
#endif
#endif

#ifdef _WINDOWS

#include <windows.h>
#include <io.h>

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

typedef int socklen_t;
typedef unsigned long in_addr_t;

#define vsnprintf _vsnprintf
#define snprintf _snprintf
#define strcasecmp _stricmp
#define strdup _strdup
#define random rand
#define open _open
#define dup2 _dup2
#define read _read
#define write _write
#define close _close
#define getpid _getpid

#define O_RDWR   _O_RDWR
#define O_RDONLY _O_RDONLY
#define O_WRONLY _O_WRONLY
#define O_APPEND _O_APPEND
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

#define FMT64 "%lld"
#define FMT64U "%llu"

#endif /* ! _WINDOWS */

#ifndef YATE_API
#define YATE_API
#endif

#ifndef IPTOS_LOWDELAY
#define IPTOS_LOWDELAY      0x10
#define IPTOS_THROUGHPUT    0x08
#define IPTOS_RELIABILITY   0x04
#define IPTOS_MINCOST       0x02
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
 * Enable timestamping of output messages and set the time start reference
 */
void setDebugTimestamp();

/**
 * Standard debugging levels.
 * The DebugFail level is special - it is always displayed and may abort
 *  the program if @ref abortOnBug() is set.
 */
enum DebugLevel {
    DebugFail = 0,
    DebugGoOn = 2,
    DebugWarn = 5,
    DebugMild = 7,
    DebugInfo = 9,
    DebugAll = 10
};

/**
 * Retrive the current global debug level
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
     */
    inline DebugEnabler(int level = TelEngine::debugLevel(), bool enabled = true)
	: m_level(DebugFail), m_enabled(enabled), m_chain(0)
	{ debugLevel(level); }

    /**
     * Retrive the current local debug level
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
     * Retrive the current debug activation status
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
     * Check if debugging output should be generated
     * @param level The debug level we are testing
     * @return True if messages should be output, false otherwise
     */
    bool debugAt(int level) const;

    /**
     * Chain this debug holder to a parent or detach from existing one
     * @param chain Pointer to parent debug level, NULL to detach
     */
    inline void debugChain(DebugEnabler* chain = 0)
	{ m_chain = (chain != this) ? chain : 0; }

private:
    int m_level;
    bool m_enabled;
    DebugEnabler* m_chain;
};

#if 0
/**
 * Convenience macro.
 * Does the same as @ref Debug if DEBUG is #defined (compiling for debugging)
 *  else it does not get compiled at all.
 */
void DDebug(int level, const char* format, ...);

/**
 * Convenience macro.
 * Does the same as @ref Debug if DEBUG is #defined (compiling for debugging)
 *  else it does not get compiled at all.
 */
void DDebug(const char* facility, int level, const char* format, ...);

/**
 * Convenience macro.
 * Does the same as @ref Debug if XDEBUG is #defined (compiling for extra
 * debugging) else it does not get compiled at all.
 */
void XDebug(int level, const char* format, ...);

/**
 * Convenience macro.
 * Does the same as @ref Debug if XDEBUG is #defined (compiling for extra
 * debugging) else it does not get compiled at all.
 */
void XDebug(const char* facility, int level, const char* format, ...);

/**
 * Convenience macro.
 * Does the same as @ref Debug if NDEBUG is not #defined
 *  else it does not get compiled at all (compiling for mature release).
 */
void NDebug(int level, const char* format, ...);

/**
 * Convenience macro.
 * Does the same as @ref Debug if NDEBUG is not #defined
 *  else it does not get compiled at all (compiling for mature release).
 */
void NDebug(const char* facility, int level, const char* format, ...);
#endif

#ifdef _DEBUG
#undef DEBUG
#define DEBUG
#endif

#ifdef XDEBUG
#undef DEBUG
#define DEBUG
#endif

#ifdef DEBUG
#define DDebug Debug
#else
#ifdef _WINDOWS
#define DDebug
#else
#define DDebug(arg...)
#endif
#endif

#ifdef XDEBUG
#define XDebug Debug
#else
#ifdef _WINDOWS
#define XDebug
#else
#define XDebug(arg...)
#endif
#endif

#ifndef NDEBUG
#define NDebug Debug
#else
#ifdef _WINDOWS
#define NDebug
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
 * Outputs a string to the debug console with formatting
 * @param facility Facility that outputs the message
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
     * The constructor prints the method entry message and indents.
     * @param name Name of the function or block entered, must be static
     * @param format printf() style format string
     */
    Debugger(const char* name, const char* format = 0, ...);

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
    static void setOutput(void (*outFunc)(const char*) = 0);

    /**
     * Set the interactive output callback
     * @param outFunc Pointer to the output function, NULL to disable
     */
    static void setIntOut(void (*outFunc)(const char*) = 0);

    /**
     * Enable or disable the debug output
     */
    static void enableOutput(bool enable = true);

private:
    const char* m_name;
};

/**
 * A structure to build (mainly static) Token-to-ID translation tables.
 * A table of such structures must end with an entry with a null token
 */
struct TokenDict {
    const char* token;
    int value;
};

class String;

/**
 * An object with just a public virtual destructor
 */
class YATE_API GenObject
{
public:
    /**
     * Destructor.
     */
    virtual ~GenObject() { }

    /**
     * Destroys the object, disposes the memory.
     */
    virtual void destruct()
	{ delete this; }

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
};

/**
 * A reference counted object.
 * Whenever using multiple inheritance you should inherit this class virtually.
 */
class YATE_API RefObject : public GenObject
{
public:
    /**
     * The constructor initializes the reference counter to 1!
     * Use deref() to destruct the object when safe
     */
    RefObject()
	: m_refcount(1) { }

    /**
     * Destructor.
     */
    virtual ~RefObject();

    /**
     * Increments the reference counter
     * @return The new reference count
     */
    int ref();

    /**
     * Decrements the reference counter, destroys the object if it reaches zero
     * <pre>
     * // Deref this object, return quickly if the object was deleted
     * if (deref()) return;
     * </pre>
     * @return True if the object was deleted, false if it still exists
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
    virtual void destruct()
	{ deref(); }

private:
    int m_refcount;
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
     * Retrive the stored pointer
     * @return A typed pointer
     */
    inline Obj* pointer() const
	{ return static_cast<Obj*>(m_pointer); }

    /**
     * Set a new stored pointer
     * @param object Pointer to the new stored object
     */
    void assign(Obj* object = 0)
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
	{ assign(const_cast<const Obj*>(value)); return *this; }

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
    inline Obj* operator->()
	{ return pointer(); }

    /**
     * Dereferencing operator
     */
    inline Obj& operator*()
	{ return *pointer(); }
};

/**
 * A simple single-linked object list handling class
 * @short An object list class
 */
class YATE_API ObjList : public GenObject
{
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
     * Pointer-like indexing operator
     * @param index Index of the list item to retrive
     * @return Pointer to the list item or NULL
     */
    ObjList* operator+(int index) const;

    /**
     * Array-like indexing operator
     * @param index Index of the object to retrive
     * @return Pointer to the object or NULL
     */
    GenObject* operator[](int index) const;

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
     * Insert an object at this point
     * @param obj Pointer to the object to insert
     * @return A pointer to the inserted list item
     */
    ObjList* insert(const GenObject* obj);

    /**
     * Append an object to the end of the list
     * @param obj Pointer to the object to append
     * @return A pointer to the inserted list item
     */
    ObjList* append(const GenObject* obj);

    /**
     * Delete this list item
     * @param delold True to delete the object (default)
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
     * Clear the list and optionally delete all contained objects
     */
    void clear();

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

private:
    ObjList* m_next;
    GenObject* m_obj;
    bool m_delete;
};

class Regexp;
class StringMatchPrivate;

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
    String(char value, unsigned int repeat = 1);

    /**
     * Creates a new initialized string from an integer.
     * @param value Value to convert to string
     */
    String(int value);

    /**
     * Creates a new initialized string from an unsigned int.
     * @param value Value to convert to string
     */
    String(unsigned int value);

    /**
     * Creates a new initialized string from a boolean.
     * @param value Value to convert to string
     */
    String(bool value);

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
     * Get the hash of the contained string.
     * @return The hash of the string.
     */
    unsigned int hash() const;

    /**
     * Get the hash of an arbitrary string.
     * @return The hash of the string.
     */
    static unsigned int hash(const char* value);

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
     * Override GenObject's method to return this String
     * @return A reference to this String
     */
    virtual const String& toString() const;

    /**
     * Convert the string to an integer value.
     * @param defvalue Default to return if the string is not a number
     * @param base Numeration base, 0 to autodetect
     * @return The integer interpretation or defvalue.
     */
    int toInteger(int defvalue = 0, int base = 0) const;

    /**
     * Convert the string to an integer value looking up first a token table.
     * @param tokens Pointer to an array of tokens to lookup first
     * @param defvalue Default to return if the string is not a token or number
     * @param base Numeration base, 0 to autodetect
     * @return The integer interpretation or defvalue.
     */
    int toInteger(const TokenDict* tokens, int defvalue = 0, int base = 0) const;

    /**
     * Convert the string to a boolean value.
     * @param defvalue Default to return if the string is not a bool
     * @return The boolean interpretation or defvalue.
     */
    bool toBoolean(bool defvalue = false) const;

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
     * Indexing operator
     * @param index Index of character in string
     * @return Character at given index or 0 if out of range
     */
    inline char operator[](int index) const
	{ return at(index); }

    /**
     * Conversion to "const char *" operator.
     */
    inline operator const char*() const
	{ return m_string; };

    /**
     * Assigns a new value to the string from a character block.
     * @param value New value of the string
     * @param len Length of the data to copy, -1 for full string
     */
    String& assign(const char* value, int len = -1);

    /**
     * Assignment operator.
     */
    inline String& operator=(const String& value)
	{ return operator=(value.c_str()); }

    /**
     * Assignment from String* operator.
     * @see TelEngine::strcpy
     */
    inline String& operator=(const String* value)
	{ return operator=(value ? value->c_str() : ""); }

    /**
     * Assignment from char* operator.
     * @see TelEngine::strcpy
     */
    String& operator=(const char* value);

    /**
     * Assignment operator for single characters.
     */
    String& operator=(char value);

    /**
     * Assignment operator for integers.
     */
    String& operator=(int value);

    /**
     * Assignment operator for unsigned integers.
     */
    String& operator=(unsigned int value);

    /**
     * Assignment operator for booleans.
     */
    inline String& operator=(bool value)
	{ return operator=(boolText(value)); }

    /**
     * Appending operator for strings.
     * @see TelEngine::strcat
     */
    String& operator+=(const char* value);

    /**
     * Appending operator for single characters.
     */
    String& operator+=(char value);

    /**
     * Appending operator for integers.
     */
    String& operator+=(int value);

    /**
     * Appending operator for unsigned integers.
     */
    String& operator+=(unsigned int value);

    /**
     * Appending operator for booleans.
     */
    inline String& operator+=(bool value)
	{ return operator+=(boolText(value)); }

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
    bool operator==(const String& value) const;

    /**
     * Fast inequality operator.
     */
    bool operator!=(const String& value) const;

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
     * Stream style appending operator for integers
     */
    inline String& operator<<(int value)
	{ return operator+=(value); }

    /**
     * Stream style appending operator for unsigned integers
     */
    inline String& operator<<(unsigned int value)
	{ return operator+=(value); }

    /**
     * Stream style appending operator for booleans
     */
    inline String& operator<<(bool value)
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
     * Conditional appending with a separator
     * @param value String to append
     * @param separator Separator to insert before the value
     * @param force True to allow appending empty strings
     */
    String& append(const char* value, const char* separator = 0, bool force = false);

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
     * Checks if the string starts with a substring
     * @param what Substring to search for
     * @param wordBreak Check if a word boundary follows the substring
     * @return True if the substring occurs at the beginning of the string
     */
    bool startsWith(const char* what, bool wordBreak = false) const;

    /**
     * Checks if the string ends with a substring
     * @param what Substring to search for
     * @param wordBreak Check if a word boundary precedes the substring
     * @return True if the substring occurs at the end of the string
     */
    bool endsWith(const char* what, bool wordBreak = false) const;

    /**
     * Checks if the string starts with a substring and removes it
     * @param what Substring to search for
     * @param wordBreak Check if a word boundary follows the substring;
     *  this parameter defaults to True because the intended use of this
     *  method is to separate commands from their parameters
     * @return True if the substring occurs at the beginning of the string
     *  and also removes the substring; if wordBreak is True any word
     *  breaking characters are also removed
     */
    bool startSkip(const char* what, bool wordBreak = true);

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
    bool matches(Regexp& rexp);

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
 * Utility function to replace NULL string pointers with an empty string
 * @param str Pointer to a C string that may be NULL
 * @return Original pointer or pointer to an empty string
 */
inline const char *c_safe(const char* str)
    { return str ? str : ""; }

/**
 * Utility function to check if a C string is null or empty
 * @param str Pointer to a C string
 * @return True if str is NULL or starts with a NUL character
 */
inline bool null(const char* str)
    { return !(str && *str); }

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
    Regexp(const char* value, bool extended = false, bool insensitive = false);

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
    bool compile();

    /**
     * Checks if the pattern matches a given value
     * @param value String to check for match
     * @return True if matches, false otherwise
     */
    bool matches(const char* value);

    /**
     * Checks if the pattern matches a string
     * @param value String to check for match
     * @return True if matches, false otherwise
     */
    virtual bool matches(const String& value) const
	{ return matches(value.safe()); }

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

private:
    void cleanup();
    bool matches(const char *value, StringMatchPrivate *matches);
    void* m_regexp;
    int m_flags;
};

/**
 * A string class with a hashed string name
 * @short A named string class.
 */
class YATE_API NamedString : public String
{
public:
    /**
     * Creates a new named string.
     * @param name Name of this string
     * @param value Initial value of the string.
     */
    NamedString(const char* name, const char* value = 0);

    /**
     * Retrive the name of this string.
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
     * Value assignment operator
     */
    inline NamedString& operator=(const char* value)
	{ String::operator=(value); return *this; }

private:
    NamedString(); // no default constructor please
    String m_name;
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
     * Constructs a Time object from a timeval structure
     * @param tv Pointer to the timeval structure
     */
    inline Time(struct timeval* tv)
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
    static u_int64_t fromTimeval(struct timeval* tv);

    /**
     * Get the current system time in microseconds
     * @return Time in microseconds since the Epoch
     */
    static u_int64_t now();

private:
    u_int64_t m_time;
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
     */
    DataBlock();

    /**
     * Copy constructor
     */
    DataBlock(const DataBlock& value);

    /**
     * Constructs an initialized data block
     * @param value Data to assign, may be NULL to fill with zeros
     * @param len Length of data, may be zero (then @ref value is ignored)
     * @param copyData True to make a copy of the data, false to just insert the pointer
     */
    DataBlock(void* value, unsigned int len, bool copyData = true);

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
     * Clear the data and optionally free the memory
     * @param deleteData True to free the deta block, false to just forget it
     */
    void clear(bool deleteData = true);

    /**
     * Assign data to the object
     * @param value Data to assign, may be NULL to fill with zeros
     * @param len Length of data, may be zero (then @ref value is ignored)
     * @param copyData True to make a copy of the data, false to just insert the pointer
     */
    DataBlock& assign(void* value, unsigned int len, bool copyData = true);

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

private:
    void* m_data;
    unsigned int m_length;
};

/**
 * A class to compute and check MD5 digests
 * @short A standard MD5 digest calculator
 */
class YATE_API MD5
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
     * Construct a digest from a String
     * @param str String to be included in digest
     */
    MD5(const DataBlock& data);

    /**
     * Construct a digest from a String
     * @param str String to be included in digest
     */
    MD5(const String& str);

    /**
     * Destroy the instance, free allocated memory
     */
    ~MD5();

    /**
     * Assignment operator.
     */
    MD5& operator=(const MD5& original);

    /**
     * Clear the digest and prepare for reuse
     */
    void clear();

    /**
     * Finalize the digest computation, make result ready.
     * Subsequent calls to @ref update() will fail
     */
    void finalize();

    /**
     * Update the digest from a buffer of data
     * @param buf Pointer to the data to be included in digest
     * @param len Length of data in the buffer
     * @return True if success, false if @ref finalize() was already called
     */
    bool update(const void* buf, unsigned int len);

    /**
     * Update the digest from the content of a DataBlock
     * @param data Data to be included in digest
     * @return True if success, false if @ref finalize() was already called
     */
    inline bool update(const DataBlock& data)
	{ return update(data.data(), data.length()); }

    /**
     * Update the digest from the content of a String
     * @param str String to be included in digest
     * @return True if success, false if @ref finalize() was already called
     */
    inline bool update(const String& str)
	{ return update(str.c_str(), str.length()); }

    /**
     * Returns a pointer to the raw 16-byte binary value of the message digest.
     * The digest is finalized if if wasn't already
     * @return Pointer to the raw digest data or NULL if some error occured
     */
    const unsigned char* rawDigest();

    /**
     * Returns the standard hexadecimal representation of the message digest.
     * The digest is finalized if if wasn't already
     * @return A String which holds the hex digest or a null one if some error occured
     */
    const String& hexDigest();

private:
    void init();
    void* m_private;
    String m_hex;
    unsigned char m_bin[16];
};

/**
 * This class holds a named list of named strings
 * @short A named string container class
 */
class YATE_API NamedList : public String
{
public:
    /**
     * Creates a new named list.
     * @param name Name of the list - must not be NULL or empty
     */
    NamedList(const char* name);

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
     * Add a named string to the parameter list.
     * @param param Parameter to add
     */
    NamedList& addParam(NamedString* param);

    /**
     * Add a named string to the parameter list.
     * @param name Name of the new string
     * @param value Value of the new string
     */
    NamedList& addParam(const char* name, const char* value);

    /**
     * Set a named string in the parameter list.
     * @param param Parameter to set or add
     */
    NamedList& setParam(NamedString* param);

    /**
     * Set a named string in the parameter list.
     * @param name Name of the string
     * @param value Value of the string
     */
    NamedList& setParam(const char* name, const char* value);

    /**
     * Clars all instances of a named string in the parameter list.
     * @param name Name of the string to remove
     */
    NamedList& clearParam(const String& name);

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
     * Retrive the value of a named parameter.
     * @param name Name of parameter to locate
     * @param defvalue Default value to return if not found
     * @return The string contained in the named parameter or the default
     */
    const char* getValue(const String& name, const char* defvalue = 0) const;

    /**
     * Retrive the numeric value of a parameter.
     * @param name Name of parameter to locate
     * @param defvalue Default value to return if not found
     * @return The number contained in the named parameter or the default
     */
    int getIntValue(const String& name, int defvalue = 0) const;

    /**
     * Retrive the numeric value of a parameter trying first a table lookup.
     * @param name Name of parameter to locate
     * @param tokens A pointer to an array of tokens to try to lookup
     * @param defvalue Default value to return if not found
     * @return The number contained in the named parameter or the default
     */
    int getIntValue(const String& name, const TokenDict* tokens, int defvalue = 0) const;

    /**
     * Retrive the boolean value of a parameter.
     * @param name Name of parameter to locate
     * @param defvalue Default value to return if not found
     * @return The boolean value contained in the named parameter or the default
     */
    bool getBoolValue(const String& name, bool defvalue = false) const;

private:
    NamedList(); // no default constructor please
    NamedList(const NamedList& value); // no copy constructor
    NamedList& operator=(const NamedList& value); // no assignment please
    ObjList m_params;
};

class MutexPrivate;
class ThreadPrivate;

/**
 * A simple mutual exclusion for locking access between threads
 * @short Mutex support
 */
class YATE_API Mutex
{
    friend class MutexPrivate;
public:
    /**
     * Construct a new unlocked fast mutex
     */
    Mutex();

    /**
     * Construct a new unlocked mutex
     * @param recursive True if the mutex has to be recursive (reentrant),
     *  false for a normal fast mutex
     */
    Mutex(bool recursive);

    /**
     * Copy constructor creates a shared mutex
     * @param original Reference of the mutex to share
     */
    Mutex(const Mutex& orginal);

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
     * @param maxait Time in microseconds to wait for the mutex, -1 wait forever
     * @return True if successfully locked, false on failure
     */
    bool lock(int64_t maxwait = -1);

    /**
     * Unlock the mutex, does never wait
     */
    void unlock();

    /**
     * Check if the mutex is currently locked - as it's asynchronous it
     *  guarantees nothing if other thread changes the mutex's status
     * @return True if the mutex was locked when the function was called
     */
    bool locked() const;

    /**
     * Check if the mutex is unlocked (try to lock and unlock the mutex)
     * @param maxait Time in microseconds to wait for the mutex, -1 wait forever
     * @return True if successfully locked and unlocked, false on failure
     */
    bool check(int64_t maxwait = -1);

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
     * @return Count of locked mutexes, should be zero at program exit
     */
    static int locks();

private:
    MutexPrivate* privDataCopy() const;
    MutexPrivate* m_private;
};

/**
 * A lock is a stack allocated (automatic) object that locks a mutex on
 *  creation and unlocks it on destruction - typically when exiting a block
 * @short Ephemeral mutex locking object
 */
class YATE_API Lock
{
public:
    /**
     * Create the lock, try to lock the mutex
     * @param mutex Reference to the mutex to lock
     * @param maxait Time in microseconds to wait for the mutex, -1 wait forever
     */
    inline Lock(Mutex& mutex, int64_t maxwait = -1)
	{ m_mutex = mutex.lock(maxwait) ? &mutex : 0; }

    /**
     * Create the lock, try to lock the mutex
     * @param mutex Pointer to the mutex to lock
     * @param maxait Time in microseconds to wait for the mutex, -1 wait forever
     */
    inline Lock(Mutex* mutex, int64_t maxwait = -1)
	{ m_mutex = (mutex && mutex->lock(maxwait)) ? mutex : 0; }

    /**
     * Destroy the lock, unlock the mutex if it was locked
     */
    inline ~Lock()
	{ if (m_mutex) m_mutex->unlock(); }

    /**
     * Return a pointer to the mutex this lock holds
     * @return A mutex pointer or NULL if locking failed
     */
    inline Mutex* mutex() const
	{ return m_mutex; }

    /**
     * Unlock the mutex if it was locked and drop the reference to it
     */
    inline void drop()
	{ if (m_mutex) m_mutex->unlock(); m_mutex = 0; }

private:
    Mutex* m_mutex;

    /** Make sure no Lock is ever created on heap */
    inline void* operator new(size_t);

    /** Never allocate an array of this class */
    inline void* operator new[](size_t);

    /** No copy constructor */
    inline Lock(const Lock&);
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
public:
    /**
     * Running priorities
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
     * @return True if an error occured, false if started ok
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
     * Give up the currently running timeslice
     * @param exitCheck Terminate the thread if asked so
     */
    static void yield(bool exitCheck = false);

    /**
     * Sleep for a number of seconds
     * @param sec Number of seconds to sleep
     * @param exitCheck Terminate the thread if asked so
     */
    static void sleep(unsigned int sec, bool exitCheck = false);

    /**
     * Sleep for a number of milliseconds
     * @param sec Number of milliseconds to sleep
     * @param exitCheck Terminate the thread if asked so
     */
    static void msleep(unsigned long msec, bool exitCheck = false);

    /**
     * Sleep for a number of microseconds
     * @param sec Number of microseconds to sleep
     * @param exitCheck Terminate the thread if asked so
     */
    static void usleep(unsigned long usec, bool exitCheck = false);

    /**
     * Get a pointer to the currently running thread
     * @return A pointer to the current thread or NULL for main thread
     */
    static Thread* current();

    /**
     * Get the number of threads
     * @return Count of threads except the main one
     */
    static int count();

    /**
     * Check if the current thread was asked to terminate.
     * @param justCheck If true and should terminate then terminate thread immediately
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
     * Kills all other running threads. Ouch!
     * Must be called from the main thread or it does nothing.
     */
    static void killall();

    /**
     * On some platforms this method kills all other running threads.
     * Must be called after fork() but before any exec*() call.
     */
    static void preExec();

protected:
    /**
     * Creates and starts a new thread
     * @param name Static name of the thread (for debugging purpose only)
     * @param prio Thread priority
     */
    Thread(const char *name = 0, Priority prio = Normal);

    /**
     * The destructor is called when the thread terminates
     */
    virtual ~Thread();

private:
    ThreadPrivate* m_private;
};

/**
 * Wrapper class to keep a socket address
 * @short A socket address holder
 */
class YATE_API SocketAddr : public GenObject
{
public:
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
	: m_address(0), m_length(0)
	{ assign(value.address(),value.length()); }

    /**
     * Constructor of a null address
     * @param family Family of the address to create
     */
    SocketAddr(int family);

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
     * Assigns a new address
     * @param addr Pointer to the address to store
     * @param len Length of the stored address, zero to use default
     */
    void assign(const struct sockaddr* addr, socklen_t len = 0);

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
     * Get the host of this address
     * @return Host name as String
     */
    inline const String& host() const
	{ return m_host; }

    /**
     * Set the hostname of this address
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

protected:
    /**
     * Convert the host address to a String stored in m_host
     */
    virtual void stringify();

    struct sockaddr* m_address;
    socklen_t m_length;
    String m_host;
};

/**
 * This class encapsulates a system dependent socket in a system independent abstraction
 * @short A generic socket class
 */
class YATE_API Socket
{
public:
    /**
     * Types of service
     */
    enum TOS {
	LowDelay       = IPTOS_LOWDELAY,
	MaxThroughput  = IPTOS_THROUGHPUT,
	MaxReliability = IPTOS_RELIABILITY,
	MinCost        = IPTOS_MINCOST,
    };

    /**
     * Default constructor, creates an invalid socket
     */
    Socket();

    /**
     * Constructor from an existing handle
     * @param handle Operating system handle to an existing socket
     */
    Socket(SOCKET handle);

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
    ~Socket();

    /**
     * Creates a new socket handle,
     * @param domain Communication domain for the socket (protocol family)
     * @param type Type specification of the socket
     * @param protocol Specific protocol for the domain, 0 to use default
     * @return True if socket was created, false if an error occured
     */
    bool create(int domain, int type, int protocol = 0);

    /**
     * Closes the socket handle, terminates the connection
     * @return True if socket was (already) closed, false if an error occured
     */
    bool terminate();

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
     * Get the error code of the last operation on this socket
     * @return Error code generated by the last operation on this socket
     */
    inline int error() const
	{ return m_error; }

    /**
     * Check if the last error code indicates a retryable condition
     * @return True if error was temporary and operation should be retried
     */
    bool canRetry() const;

    /**
     * Check if this socket is valid
     * @return True if the handle is valid, false if it's invalid
     */
    inline bool valid() const
	{ return (m_handle != invalidHandle()); }

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
     * Set socket options
     * @param name Socket option for which the value is to be set
     * @param value Pointer to a buffer holding the value for the requested option
     * @param length Size of the supplied buffer
     * @return True if operation was successfull, false if an error occured
     */
    bool setOption(int level, int name, const void* value = 0, socklen_t length = 0);

    /**
     * Get socket options
     * @param name Socket option for which the value is to be set
     * @param value Pointer to a buffer to return the value for the requested option
     * @param length Pointer to size of the supplied buffer, will be filled on return
     * @return True if operation was successfull, false if an error occured
     */
    bool getOption(int level, int name, void* buffer, socklen_t* length);

    /**
     * Set the Type of Service on the IP level of this socket
     * @param tos New TOS bits to set
     * @return True if operation was successfull, false if an error occured
     */
    bool setTOS(int tos);
    
    /**
     * Set the blocking or non-blocking operation mode of the socket
     * @param block True if I/O operations should block, false for non-blocking
     * @return True if operation was successfull, false if an error occured
     */
    bool setBlocking(bool block = true);

    /**
     * Associates the socket with a local address
     * @param addr Address to assign to this socket
     * @param addrlen Length of the address structure
     * @return True if operation was successfull, false if an error occured
     */
    bool bind(struct sockaddr* addr, socklen_t addrlen);

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
    bool listen(unsigned int backlog = 0);

    /**
     * Create a new socket for an incoming connection attempt on a listening socket
     * @param addr Address to fill in with the address of the incoming connection
     * @param addrlen Length of the address structure on input, length of address data on return
     * @return Open socket to the new connection or NULL on failure
     */
    Socket* accept(struct sockaddr* addr = 0, socklen_t* addrlen = 0);

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
     * Connects the socket to a remote address
     * @param addr Address to connect to
     * @param addrlen Length of the address structure
     * @return True if operation was successfull, false if an error occured
     */
    bool connect(struct sockaddr* addr, socklen_t addrlen);

    /**
     * Connects the socket to a remote address
     * @param addr Socket address to connect to
     * @return True if operation was successfull, false if an error occured
     */
    inline bool connect(const SocketAddr& addr)
	{ return connect(addr.address(), addr.length()); }

    /**
     * Retrive the address of the local socket of a connection
     * @param addr Address to fill in with the address of the local socket
     * @param addrlen Length of the address structure on input, length of address data on return
     * @return True if operation was successfull, false if an error occured
     */
    bool getSockName(struct sockaddr* addr, socklen_t* addrlen);

    /**
     * Retrive the address of the local socket of a connection
     * @param addr Address to fill in with the address of the local socket
     * @return True if operation was successfull, false if an error occured
     */
    bool getSockName(SocketAddr& addr);

    /**
     * Retrive the address of the remote socket of a connection
     * @param addr Address to fill in with the address of the remote socket
     * @param addrlen Length of the address structure on input, length of address data on return
     * @return True if operation was successfull, false if an error occured
     */
    bool getPeerName(struct sockaddr* addr, socklen_t* addrlen);

    /**
     * Retrive the address of the remote socket of a connection
     * @param addr Address to fill in with the address of the remote socket
     * @return True if operation was successfull, false if an error occured
     */
    bool getPeerName(SocketAddr& addr);

    /**
     * Send a message over a connected or unconnected socket
     * @param buffer Buffer for data transfer
     * @param length Length of the buffer
     * @param addr Address to send the message to
     * @param addrlen Length of the address structure
     * @param flags Operating system specific bit flags that change the behaviour
     * @return Number of bytes transferred, @ref socketError() if an error occurred
     */
    int sendTo(const void* buffer, int length, const struct sockaddr* addr, socklen_t adrlen, int flags = 0);

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
    int send(const void* buffer, int length, int flags = 0);

    /**
     * Write data to a connected stream socket
     * @param buffer Buffer for data transfer
     * @param length Length of the buffer
     * @return Number of bytes transferred, @ref socketError() if an error occurred
     */
    int writeData(const void* buffer, int length);

    /**
     * Write a C string to a connected stream socket
     * @param str String to send over the socket
     * @return Number of bytes transferred, @ref socketError() if an error occurred
     */
    int writeData(const char* str);

    /**
     * Receive a message from a connected or unconnected socket
     * @param buffer Buffer for data transfer
     * @param length Length of the buffer
     * @param addr Address to fill in with the address of the incoming data
     * @param addrlen Length of the address structure on input, length of address data on return
     * @param flags Operating system specific bit flags that change the behaviour
     * @return Number of bytes transferred, @ref socketError() if an error occurred
     */
    int recvFrom(void* buffer, int length, struct sockaddr* addr = 0, socklen_t* adrlen = 0, int flags = 0);

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
    int recv(void* buffer, int length, int flags = 0);

    /**
     * Receive data from a connected stream socket
     * @param buffer Buffer for data transfer
     * @param length Length of the buffer
     * @return Number of bytes transferred, @ref socketError() if an error occurred
     */
    int readData(void* buffer, int length);

    /**
     * Determines the availability to perform synchronous I/O of the socket
     * @param readok Address of a boolean variable to fill with readability status
     * @param writeok Address of a boolean variable to fill with writeability status
     * @param except Address of a boolean variable to fill with exceptions status
     * @param timeout Maximum time until the method returns, NULL for blocking
     * @return True if operation was successfull, false if an error occured
     */
    bool select(bool* readok, bool* writeok, bool* except, struct timeval* timeout = 0);

protected:
    /**
     * Clear the last error code
     */
    inline void clearError()
	{ m_error = 0; }

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

    int m_error;
    SOCKET m_handle;
};

}; // namespace TelEngine

#endif /* __YATECLASS_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
