/**
 * telengine.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004 Null Team
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

#ifndef __TELENGINE_H
#define __TELENGINE_H

#ifndef __cplusplus
#error C++ is required
#endif

struct timeval;
	
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
void abortOnBug();

/**
 * Set the abort on bug flag. The default flag state is false.
 * @return The old state of the flag.
 */
bool abortOnBug(bool doAbort);

/**
 * Standard debugging levels.
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
 * Retrive the current debug level
 * @return The current debug level
 */
int debugLevel();

/**
 * Set the current debug level.
 * @param level The desired debug level
 * @return The new debug level (may be different)
 */
int debugLevel(int level);

/**
 * Check if debugging output should be generated
 * @param level The desired debug level
 * @return True if messages should be output, false otherwise
 */
bool debugAt(int level);

#if 0
/**
 * Convenience macro.
 * Does the same as @ref Debug if DEBUG is #defined (compiling for debugging)
 *  else it does not get compiled at all.
 */
bool DDebug(int level, const char *format, ...);

/**
 * Convenience macro.
 * Does the same as @ref Debug if DEBUG is #defined (compiling for debugging)
 *  else it does not get compiled at all.
 */
bool DDebug(const char *facility, int level, const char *format, ...);

/**
 * Convenience macro.
 * Does the same as @ref Debug if NDEBUG is not #defined
 *  else it does not get compiled at all (compiling for mature release).
 */
bool NDebug(int level, const char *format, ...);

/**
 * Convenience macro.
 * Does the same as @ref Debug if NDEBUG is not #defined
 *  else it does not get compiled at all (compiling for mature release).
 */
bool NDebug(const char *facility, int level, const char *format, ...);
#endif

#ifdef DEBUG
#define DDebug(arg...) Debug(arg)
#else
#define DDebug(arg...)
#endif

#ifndef NDEBUG
#define NDebug(arg...) Debug(arg)
#else
#define NDebug(arg...)
#endif

/**
 * Outputs a debug string.
 * @param level The level of the message
 * @param format A printf() style format string
 * @return True if message was output, false otherwise
 */
bool Debug(int level, const char *format, ...) FORMAT_CHECK(2);

/**
 * Outputs a debug string for a specific facility.
 * @param facility Facility that outputs the message
 * @param level The level of the message
 * @param format A printf() style format string
 * @return True if message was output, false otherwise
 */
bool Debug(const char *facility, int level, const char *format, ...) FORMAT_CHECK(3);

/**
 * Outputs a string to the debug console with formatting
 * @param facility Facility that outputs the message
 * @param format A printf() style format string
 */
void Output(const char *format, ...) FORMAT_CHECK(1);

/**
 * This class is used as an automatic variable that logs messages on creation
 *  and destruction (when the instruction block is left or function returns)
 * @short An object that logs messages on creation and destruction
 */
class Debugger
{
public:
    /**
     * The constructor prints the method entry message and indents.
     * @param name Name of the function or block entered, must be static
     * @param format printf() style format string
     */
    Debugger(const char *name, const char *format = 0, ...);

    /**
     * The constructor prints the method entry message and indents.
     * @param level The level of the message
     * @param name Name of the function or block entered, must be static
     * @param format printf() style format string
     */
    Debugger(int level, const char *name, const char *format = 0, ...);

    /**
     * The destructor prints the method leave message and deindents.
     */
    ~Debugger();

    /**
     * Set the output callback
     * @param outFunc Pointer to the output function, NULL to use stderr
     */
    static void setOutput(void (*outFunc)(const char *) = 0);

    /**
     * Set the interactive output callback
     * @param outFunc Pointer to the output function, NULL to disable
     */
    static void setIntOut(void (*outFunc)(const char *) = 0);

    /**
     * Enable or disable the debug output
     */
    static void enableOutput(bool enable = true);

private:
    const char *m_name;
};

/**
 * A structure to build (mainly static) Token-to-ID translation tables.
 * A table of such structures must end with an entry with a null token
 */
struct TokenDict {
    const char *token;
    int value;
};

class String;

/**
 * An object with just a public virtual destructor
 */
class GenObject
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
};

/**
 * A reference counted object.
 * Whenever using multiple inheritance you should inherit this class virtually.
 */
class RefObject : public GenObject
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
    inline int ref()
	{ return ++m_refcount; }

    /**
     * Decrements the reference counter, destroys the object if it reaches zero
     * <pre>
     * // Deref this object, return quickly if the object was deleted
     * if (deref()) return;
     * </pre>
     * @return True if the object was deleted, false if it still exists
     */
    inline bool deref()
	{ int i = --m_refcount; if (i == 0) delete this; return (i <= 0); }

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
 * A simple single-linked object list handling class
 * @short An object list class
 */
class ObjList : public GenObject
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
    inline GenObject *get() const
	{ return m_obj; }

    /**
     * Set the object associated to this list item
     * @param obj Pointer to the new object to set
     * @param delold True to delete the old object (default)
     * @return Pointer to the old object if not destroyed
     */
    GenObject *set(const GenObject *obj, bool delold = true);

    /**
     * Get the next item in the list
     * @return Pointer to the next item in list or NULL
     */
    inline ObjList *next() const
	{ return m_next; }

    /**
     * Get the last item in the list
     * @return Pointer to the last item in list
     */
    ObjList *last() const;

    /**
     * Indexing operator
     * @param index Index of the item to retrive
     * @return Pointer to the item or NULL
     */
    ObjList *operator[](int index) const;

    /**
     * Get the item in the list that holds an object
     * @param obj Pointer to the object to search for
     * @return Pointer to the found item or NULL
     */
    ObjList *find(const GenObject *obj) const;

    /**
     * Get the item in the list that holds an object by String value
     * @param str String value (toString) of the object to search for
     * @return Pointer to the found item or NULL
     */
    ObjList *find(const String &str) const;

    /**
     * Insert an object at this point
     * @param obj Pointer to the object to insert
     * @return A pointer to the inserted list item
     */
    ObjList *insert(const GenObject *obj);

    /**
     * Append an object to the end of the list
     * @param obj Pointer to the object to append
     * @return A pointer to the inserted list item
     */
    ObjList *append(const GenObject *obj);

    /**
     * Delete this list item
     * @param delold True to delete the object (default)
     * @return Pointer to the object if not destroyed
     */
    GenObject *remove(bool delobj = true);

    /**
     * Delete the list item that holds a given object
     * @param obj Object to search in the list
     * @param delobj True to delete the object (default)
     * @return Pointer to the object if not destroyed
     */
    GenObject *remove(GenObject *obj, bool delobj = true);

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
    ObjList *m_next;
    GenObject *m_obj;
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
class String : public GenObject
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
    String(const char *value, int len = -1);

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
    String(const String &value);

    /**
     * Destroys the string, disposes the memory.
     */
    virtual ~String();

    /**
     * A static null String
     */
    static const String &empty();

    /**
     * Get the value of the stored string.
     * @return The stored C string which may be NULL.
     */
    inline const char *c_str() const
	{ return m_string; }

    /**
     * Get a valid non-NULL C string.
     * @return The stored C string or "".
     */
    inline const char *safe() const
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
    static unsigned int hash(const char *value);

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
    int toInteger(const TokenDict *tokens, int defvalue = 0, int base = 0) const;

    /**
     * Convert the string to a boolean value.
     * @param defvalue Default to return if the string is not a bool
     * @return The boolean interpretation or defvalue.
     */
    bool toBoolean(bool defvalue = false) const;

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
    String& assign(const char *value, int len = -1);

    /**
     * Assignment operator.
     */
    inline String& operator=(const String &value)
	{ return operator=(value.c_str()); }

    /**
     * Assignment from char* operator.
     * @see TelEngine::strcpy
     */
    String& operator=(const char *value);

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
	{ return operator=(value ? "true" : "false"); }

    /**
     * Appending operator for strings.
     * @see TelEngine::strcat
     */
    String& operator+=(const char *value);

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
	{ return operator+=(value ? "true" : "false"); }

    /**
     * Equality operator.
     */
    bool operator==(const char *value) const;

    /**
     * Inequality operator.
     */
    bool operator!=(const char *value) const;

    /**
     * Fast equality operator.
     */
    bool operator==(const String &value) const;

    /**
     * Fast inequality operator.
     */
    bool operator!=(const String &value) const;

    /**
     * Stream style appending operator for C strings
     */
    inline String& operator<<(const char *value)
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
    String& operator>>(const char *skip);

    /**
     * Stream style extraction operator for single characters
     */
    String& operator>>(char &store);

    /**
     * Stream style extraction operator for integers
     */
    String& operator>>(int &store);

    /**
     * Stream style extraction operator for unsigned integers
     */
    String& operator>>(unsigned int &store);

    /**
     * Stream style extraction operator for booleans
     */
    String& operator>>(bool &store);

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
    int find(const char *what, unsigned int offs = 0) const;

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
    bool startsWith(const char *what, bool wordBreak = false) const;

    /**
     * Checks if the string ends with a substring
     * @param what Substring to search for
     * @param wordBreak Check if a word boundary precedes the substring
     * @return True if the substring occurs at the end of the string
     */
    bool endsWith(const char *what, bool wordBreak = false) const;

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
    bool startSkip(const char *what, bool wordBreak = true);

    /**
     * Checks if matches another string
     * @param value String to check for match
     * @return True if matches, false otherwise
     */
    virtual bool matches(const String &value) const
	{ return operator==(value); }

    /**
     * Checks if matches a regular expression and fill the match substrings
     * @param rexp Regular expression to check for match
     * @return True if matches, false otherwise
     */
    bool matches(Regexp &rexp);

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
    String replaceMatches(const String &templ) const;

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
    static String msgEscape(const char *str, char extraEsc = 0);

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
    static String msgUnescape(const char *str, int *errptr = 0, char extraEsc = 0);

    /**
     * Decode an escaped string back to its raw form
     * @param errptr Pointer to an integer to receive the place of 1st error
     * @param extraEsc Character to unescape other than the default ones
     * @return The string with special characters unescaped
     */
    inline String msgUnescape(int *errptr = 0, char extraEsc = 0) const
	{ return msgUnescape(c_str(),errptr,extraEsc); }

protected:
    /**
     * Called whenever the value changed (except in constructors).
     */
     virtual void changed();

private:
    void clearMatches();
    char *m_string;
    unsigned int m_length;
    // i hope every C++ compiler now knows about mutable...
    mutable unsigned int m_hash;
    StringMatchPrivate *m_matches;
};

/**
 * Utility function to replace NULL string pointers with an empty string
 * @param str Pointer to a C string that may be NULL
 * @return Original pointer or pointer to an empty string
 */
inline const char *c_safe(const char *str)
    { return str ? str : ""; }

/**
 * Concatenation operator for strings.
 */
String operator+(const String &s1, const String &s2);

/**
 * Concatenation operator for strings.
 */
String operator+(const String &s1, const char *s2);

/**
 * Concatenation operator for strings.
 */
String operator+(const char *s1, const String &s2);

/**
 * Prevent careless programmers from overwriting the string
 * @see TelEngine::String::operator=
 */
inline char *strcpy(String dest, const char *src)
    { dest = src; return (char *)dest.c_str(); }

/**
 * Prevent careless programmers from overwriting the string
 * @see TelEngine::String::operator+=
 */
inline char *strcat(String dest, const char *src)
    { dest += src; return (char *)dest.c_str(); }

/**
 * Utility function to look up a string in a token table,
 * interpret as number if it fails
 * @param str String to look up
 * @param tokens Pointer to the token table
 * @param defvalue Value to return if lookup and conversion fail
 * @param base Default base to use to convert to number
 */
int lookup(const char *str, const TokenDict *tokens, int defvalue = 0, int base = 0);

/**
 * Utility function to look up a number in a token table
 * @param value Value to search for
 * @param tokens Pointer to the token table
 * @param defvalue Value to return if lookup fails
 */
const char *lookup(int value, const TokenDict *tokens, const char *defvalue = 0);


/**
 * A regular expression matching class.
 * @short A regexp matching class
 */
class Regexp : public String
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
     */
    Regexp(const char *value);

    /**
     * Copy constructor.
     * @param value Initial value of the regexp.
     */
    Regexp(const Regexp &value);

    /**
     * Destroys the regexp, disposes the memory.
     */
    virtual ~Regexp();

    /**
     * Assignment from char* operator.
     */
    inline Regexp& operator=(const char *value)
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
    bool matches(const char *value);

    /**
     * Checks if the pattern matches a string
     * @param value String to check for match
     * @return True if matches, false otherwise
     */
    virtual bool matches(const String &value) const
	{ return matches(value.safe()); }

protected:
    /**
     * Called whenever the value changed (except in constructors) to recompile.
     */
    virtual void changed();

private:
    void cleanup();
    bool matches(const char *value, StringMatchPrivate *matches);
    void *m_regexp;
};

/**
 * A string class with a hashed string name
 * @short A named string class.
 */
class NamedString : public String
{
public:
    /**
     * Creates a new named string.
     * @param name Name of this string
     * @param value Initial value of the string.
     */
    NamedString(const char *name, const char *value = 0);

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
    inline NamedString& operator=(const char *value)
	{ String::operator=(value); return *this; }

private:
    NamedString(); // no default constructor please
    String m_name;
};

/**
 * The Time class holds a time moment with microsecond accuracy
 * @short A time holding class
 */
class Time
{
public:
    /**
     * Constructs a Time object from the current time
     */
    Time()
	: m_time(now()) { }

    /**
     * Constructs a Time object from a given time
     * @param usec Time in microseconds
     */
    Time(unsigned long long usec)
	: m_time(usec) { }

    /**
     * Constructs a Time object from a timeval structure
     * @param tv Pointer to the timeval structure
     */
    Time(struct timeval *tv)
	: m_time(fromTimeval(tv)) { }

    /**
     * Get time in seconds
     * @return Time in seconds since the Epoch
     */
    inline unsigned long sec() const
	{ return (m_time+500000) / 1000000; }

    /**
     * Get time in milliseconds
     * @return Time in milliseconds since the Epoch
     */
    inline unsigned long long msec() const
	{ return (m_time+500) / 1000; }

    /**
     * Get time in microseconds
     * @return Time in microseconds since the Epoch
     */
    inline unsigned long long usec() const
	{ return m_time; }

    /**
     * Conversion to microseconds operator
     */
    inline operator unsigned long long() const
	{ return m_time; }

    /**
     * Assignment operator.
     */
    inline Time& operator=(unsigned long long usec)
	{ m_time = usec; return *this; }

    /**
     * Offsetting operator.
     */
    inline Time& operator+=(long long delta)
	{ m_time += delta; return *this; }

    /**
     * Offsetting operator.
     */
    inline Time& operator-=(long long delta)
	{ m_time -= delta; return *this; }

    /**
     * Fill in a timeval struct from a value in microseconds
     * @param tv Pointer to the timeval structure
     */
    inline void toTimeval(struct timeval *tv) const
	{ toTimeval(tv, m_time); }

    /**
     * Fill in a timeval struct from a value in microseconds
     * @param tv Pointer to the timeval structure
     * @param usec Time to convert to timeval
     */
    static void toTimeval(struct timeval *tv, unsigned long long usec);

    /**
     * Convert time in a timeval struct to microseconds
     * @param tv Pointer to the timeval structure
     * @return Corresponding time in microseconds or zero if tv is NULL
     */
    static unsigned long long fromTimeval(struct timeval *tv);

    /**
     * Get the current system time in microseconds
     * @return Time in microseconds since the Epoch
     */
    static unsigned long long now();

private:
    unsigned long long m_time;
};

/**
 * This class holds a named list of named strings
 * @short A named string container class
 */
class NamedList : public String
{
public:
    /**
     * Creates a new named list.
     * @param name Name of the list - must not be NULL or empty
     */
    NamedList(const char *name);

    /**
     * Get the number of parameters
     * @return Count of named strings
     */
    inline unsigned int length() const
	{ return m_params.length(); }

    /**
     * Add a named string to the parameter list.
     * @param param Parameter to add
     */
    NamedList &addParam(NamedString *param);

    /**
     * Add a named string to the parameter list.
     * @param name Name of the new string
     * @param value Value of the new string
     */
    NamedList &addParam(const char *name, const char *value);

    /**
     * Set a named string in the parameter list.
     * @param param Parameter to set or add
     */
    NamedList &setParam(NamedString *param);

    /**
     * Set a named string in the parameter list.
     * @param name Name of the string
     * @param value Value of the string
     */
    NamedList &setParam(const char *name, const char *value);

    /**
     * Clars all instances of a named string in the parameter list.
     * @param name Name of the string to remove
     */
    NamedList &clearParam(const String &name);

    /**
     * Locate a named string in the parameter list.
     * @param name Name of parameter to locate
     * @return A pointer to the named string or NULL.
     */
    NamedString *getParam(const String &name) const;

    /**
     * Locate a named string in the parameter list.
     * @param index Index of the parameter to locate
     * @return A pointer to the named string or NULL.
     */
    NamedString *getParam(unsigned int index) const;

    /**
     * Retrive the value of a named parameter.
     * @param name Name of parameter to locate
     * @param defvalue Default value to return if not found
     * @return The string contained in the named parameter or the default
     */
    const char *getValue(const String &name, const char *defvalue = 0) const;

private:
    NamedList(); // no default constructor please
    NamedList(const NamedList &value); // no copy constructor
    NamedList& operator=(const NamedList &value); // no assignment please
    ObjList m_params;
};

/**
 * A class for parsing and quickly accessing INI style configuration files
 * @short Configuration file handling
 */
class Configuration : public String
{
public:
    /**
     * Create an empty configuration
     */
    Configuration();

    /**
     * Create a configuration from a file
     * @param filename Name of file to initialize from
     */
    Configuration(const char *filename);

    /**
     * Assignment from string operator
     */
    inline Configuration& operator=(const String &value)
	{ String::operator=(value); return *this; }

    /**
     * Get the number of sections
     * @return Count of sections
     */
    inline unsigned int sections() const
	{ return m_sections.length(); }

    /**
     * Retrive an entire section
     * @param index Index of the section
     * @return The section's content or NULL if no such section
     */
    NamedList *getSection(unsigned int index) const;

    /**
     * Retrive an entire section
     * @param sect Name of the section
     * @return The section's content or NULL if no such section
     */
    NamedList *getSection(const String &sect) const;

    /**
     * Locate a key/value pair in the section.
     * @param sect Name of the section
     * @param key Name of the key in section
     * @return A pointer to the key/value pair or NULL.
     */
    NamedString *getKey(const String &sect, const String &key) const;

    /**
     * Retrive the value of a key in a section.
     * @param sect Name of the section
     * @param key Name of the key in section
     * @param defvalue Default value to return if not found
     * @return The string contained in the key or the default
     */
    const char *getValue(const String &sect, const String &key, const char *defvalue = 0) const;

    /**
     * Retrive the numeric value of a key in a section.
     * @param sect Name of the section
     * @param key Name of the key in section
     * @param defvalue Default value to return if not found
     * @return The number contained in the key or the default
     */
    int getIntValue(const String &sect, const String &key, int defvalue = 0) const;

    /**
     * Retrive the numeric value of a key in a section trying first a table lookup.
     * @param sect Name of the section
     * @param key Name of the key in section
     * @param tokens A pointer to an array of tokens to try to lookup
     * @param defvalue Default value to return if not found
     * @return The number contained in the key or the default
     */
    int getIntValue(const String &sect, const String &key, const TokenDict *tokens, int defvalue = 0) const;

    /**
     * Retrive the boolean value of a key in a section.
     * @param sect Name of the section
     * @param key Name of the key in section
     * @param defvalue Default value to return if not found
     * @return The boolean value contained in the key or the default
     */
    bool getBoolValue(const String &sect, const String &key, bool defvalue = false) const;

    /**
     * Deletes an entire section
     * @param sect Name of section to delete, NULL to delete all
     */
    void clearSection(const char *sect = 0);

    /**
     * Deletes a key/value pair
     * @param sect Name of section
     * @param key Name of the key to delete
     */
    void clearKey(const String &sect, const String &key);

    /**
     * Add the value of a key in a section.
     * @param sect Name of the section, will be created if missing
     * @param key Name of the key to add in the section
     * @param value Value to set in the key
     */
    void addValue(const String &sect, const char *key, const char *value = 0);

    /**
     * Set the value of a key in a section.
     * @param sect Name of the section, will be created if missing
     * @param key Name of the key in section, will be created if missing
     * @param value Value to set in the key
     */
    void setValue(const String &sect, const char *key, const char *value = 0);

    /**
     * Set the numeric value of a key in a section.
     * @param sect Name of the section, will be created if missing
     * @param key Name of the key in section, will be created if missing
     * @param value Value to set in the key
     */
    void setValue(const String &sect, const char *key, int value);

    /**
     * Set the boolean value of a key in a section.
     * @param sect Name of the section, will be created if missing
     * @param key Name of the key in section, will be created if missing
     * @param value Value to set in the key
     */
    void setValue(const String &sect, const char *key, bool value);

    /**
     * Load the configuration from file
     * @return True if successfull, false for failure
     */
    bool load();

    /**
     * Save the configuration to file
     * @return True if successfull, false for failure
     */
    bool save() const;

private:
    Configuration(const Configuration &value); // no copy constructor
    Configuration& operator=(const Configuration &value); // no assignment please
    ObjList *getSectHolder(const String &sect) const;
    ObjList *makeSectHolder(const String &sect);
    ObjList m_sections;
};

class MessageDispatcher;

/**
 * This class holds the messages that are moved around in the engine.
 * @short A message container class
 */
class Message : public NamedList
{
    friend class MessageDispatcher;
public:
    /**
     * Creates a new message.
     *
     * @param name Name of the message - must not be NULL or empty
     * @param retval Default return value
     */
    Message(const char *name, const char *retval = 0);

    /**
     * Retrive a reference to the value returned by the message.
     * @return A reference to the value the message will return
     */
    inline String &retValue()
	{ return m_return; }

    /**
     * Retrive the obscure data associated with the message
     * @return Pointer to arbitrary user data
     */
    inline void *userData() const
	{ return m_data; }

    /**
     * Set obscure data associated with the message
     * @param _data Pointer to arbitrary user data
     */
    inline void userData(void *_data)
	{ m_data = _data; }

    /**
     * Retrive a reference to the creation time of the message.
     * @return A reference to the Time when the message was created
     */
    inline Time &msgTime()
	{ return m_time; }

    /**
     * Name assignment operator
     */
    inline Message& operator=(const char *value)
	{ String::operator=(value); return *this; }

    /**
     * Encode the message into a string adequate for sending for processing
     * to an external communication interface
     * @param id Unique identifier to add to the string
     */
    String encode(const char *id) const;

    /**
     * Encode the message into a string adequate for sending as answer
     * to an external communication interface
     * @param received True if message was processed locally
     * @param id Unique identifier to add to the string
     */
    String encode(bool received, const char *id) const;

    /**
     * Decode a string from an external communication interface for processing
     * in the engine. The message is modified accordingly.
     * @param str String to decode
     * @param id A String object in which the identifier is stored
     * @return -2 for success, -1 if the string was not a text form of a
     * message, index of first erroneous character if failed
     */
    int decode(const char *str, String &id);

    /**
     * Decode a string from an external communication interface that is an
     * answer to a specific external processing request.
     * @param str String to decode
     * @param received Pointer to variable to store the dispatch return value
     * @param id The identifier expected
     * @return -2 for success, -1 if the string was not the expected answer,
     * index of first erroneous character if failed
     */
    int decode(const char *str, bool &received, const char *id);

protected:
    /**
     * Notify the message it has been dispatched
     */
    virtual void dispatched(bool accepted)
	{ }

private:
    Message(); // no default constructor please
    Message(const Message &value); // no copy constructor
    Message& operator=(const Message &value); // no assignment please
    String m_return;
    Time m_time;
    void *m_data;
    void commonEncode(String &str) const;
    int commonDecode(const char *str, int offs);
};

/**
 * A message handler
 */
class MessageHandler : public String
{
    friend class MessageDispatcher;
public:
    /**
     * Creates a new message handler.
     * @param name Name of the handled message - may be NULL
     * @param priority Priority of the handler, 0 = top
     */
    MessageHandler(const char *name, unsigned priority = 100);

    /**
     * Handler destructor.
     */
    virtual ~MessageHandler();

    /**
     * This method is called whenever the registered name matches the message.
     * @param msg The received message
     * @return True to stop processing, false to try other handlers
     */
    virtual bool received(Message &msg) = 0;

    /**
     * Find out the priority of the handler
     * @return Stored priority of the handler, 0 = top
     */
    inline unsigned priority() const
	{ return m_priority; }

private:
    unsigned m_priority;
    MessageDispatcher *m_dispatcher;
};

/**
 * A message receiver to be invoked by a message relay;
 */
class MessageReceiver : public GenObject
{
public:
    /**
     * This method is called from the message relay.
     * @param msg The received message
     * @param id The identifier with which the relay was created
     * @return True to stop processing, false to try other handlers
     */
    virtual bool received(Message &msg, int id) = 0;
};

/**
 * A message handler that allows to relay several messages to a single receiver
 */
class MessageRelay : public MessageHandler
{
public:
    /**
     * Creates a new message relay.
     * @param name Name of the handled message - may be NULL
     * @param receiver Receiver of th relayed messages
     * @param id Numeric identifier to pass to receiver
     * @param priority Priority of the handler, 0 = top
     */
    MessageRelay(const char *name, MessageReceiver *receiver, int id, int priority = 1)
	: MessageHandler(name,priority), m_receiver(receiver), m_id(id) { }

    /**
     * This method is called whenever the registered name matches the message.
     * @param msg The received message
     * @return True to stop processing, false to try other handlers
     */
    virtual bool received(Message &msg)
	{ return m_receiver ? m_receiver->received(msg,m_id) : false; }

private:
    MessageReceiver *m_receiver;
    int m_id;
};

class MutexPrivate;
class ThreadPrivate;

/**
 * Mutex support
 */
class Mutex
{
    friend class MutexPrivate;
public:
    /**
     * Construct a new unlocked mutex
     */
    Mutex();

    /**
     * Copt constructor creates a shared mutex
     * @param original Reference of the mutex to share
     */
    Mutex(const Mutex &orginal);

    /**
     * Destroy the mutex
     */
    ~Mutex();

    /**
     * Assignment operator makes the mutex shared with the original
     * @param original Reference of the mutex to share
     */
    Mutex& operator=(const Mutex &original);

    /**
     * Attempt to lock the mutex and eventually wait for it
     * @param maxait Time in microseconds to wait for the mutex, -1 wait forever
     * @return True if successfully locked, false on failure
     */
    bool lock(long long int maxwait = -1);

    /**
     * Unlock the mutex, does never wait
     */
    void unlock();

    /**
     * Check if the mutex is unlocked (try to lock and unlock the mutex)
     * @param maxait Time in microseconds to wait for the mutex, -1 wait forever
     * @return True if successfully locked and unlocked, false on failure
     */
    bool check(long long int maxwait = -1);

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
    MutexPrivate *privDataCopy() const;
    MutexPrivate *m_private;
};

/**
 * A lock is a stack allocated (automatic) object that locks a mutex on
 * creation and unlocks it on destruction - typically when exiting a block
 * @short Mutex locking object
 */
class Lock
{
public:
    /**
     * Create the lock, try to lock the mutex
     * @param mutex Reference to the mutex to lock
     * @param maxait Time in microseconds to wait for the mutex, -1 wait forever
     */
    inline Lock(Mutex &mutex, long long int maxwait = -1)
	{ m_mutex = mutex.lock(maxwait) ? &mutex : 0; }

    /**
     * Destroy the lock, unlock the mutex if it was locked
     */
    inline ~Lock()
	{ if (m_mutex) m_mutex->unlock(); }

    /**
     * Return a pointer to the mutex this lock holds
     * @return A mutex pointer or NULL if locking failed
     */
    inline Mutex *mutex() const
	{ return m_mutex; }

    /**
     * Unlock the mutex if it was locked and drop the reference to it
     */
    inline void drop()
	{ if (m_mutex) m_mutex->unlock(); m_mutex = 0; }

private:
    Mutex *m_mutex;
};

/**
 * Thread support class
 */
class Thread
{
    friend class ThreadPrivate;
public:
    /**
     * This method is called in the newly created thread.
     * When it returns the thread terminates.
     */
    virtual void run() = 0;

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
     */
    static void yield();

    /**
     * Get a pointer to the currently running thread
     * @return A pointer to the current thread or NULL for main thread
     */
    static Thread *current();

    /**
     * Get the number of threads
     * @return Count of threads except the main one
     */
    static int count();

    /**
     * Terminates the current thread.
     */
    static void exit();

    /**
     * Terminates the specified thread.
     */
    void cancel();

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
     */
    Thread(const char *name = 0);

    /**
     * The destructor is called when the thread terminates
     */
    virtual ~Thread();

private:
    ThreadPrivate *m_private;
};

/**
 * A message dispatcher
 */
class MessageDispatcher : public GenObject
{
public:
    /**
     * Creates a new message dispatcher.
     */
    MessageDispatcher();

    /**
     * Destroys the dispatcher and the installed handlers.
     */
    ~MessageDispatcher();

    /**
     * Installs a handler in the dispatcher.
     * @param handler A pointer to the handler to install
     * @return True on success, false on failure
     */
    bool install(MessageHandler *handler);

    /**
     * Uninstalls a handler from the dispatcher.
     * @param handler A pointer to the handler to uninstall
     * @return True on success, false on failure
     */
    bool uninstall(MessageHandler *handler);

    /**
     * Dispatch a message to the installed handlers
     * @param msg The message to dispatch
     * @return True if one handler accepted it, false if all ignored
     */
    bool dispatch(Message &msg);

    /**
     * Get the number of messages waiting in the queue
     * @return Count of messages in the queue
     */
    inline unsigned int queueLength() const
	{ return m_messages.count(); }

    /**
     * Put a message in the waiting queue
     * @param msg The message to enqueue, will be destroyed after dispatching
     * @return True if successfully queued, false otherwise
     */
    bool enqueue(Message *msg);

    /**
     * Dispatch all messages from the waiting queue
     */
    void dequeue();

    /**
     * Dispatch one message from the waiting queue
     * @return True if success, false if the queue is empty
     */
    bool dequeueOne();

    /**
     * Clear all the message handlers
     */
    inline void clear()
	{ m_handlers.clear(); }

    /**
     * Install or remove a hook to catch messages after being dispatched
     * @param hookFunc Pointer to a callback function
     */
    inline void setHook(void (*hookFunc)(Message &, bool) = 0)
	{ m_hook = hookFunc; }

private:
    ObjList m_handlers;
    ObjList m_messages;
    Mutex m_mutex;
    void (*m_hook)(Message &, bool);
};

/**
 * Initialization and information about plugins.
 * Plugins are located in @em shared libraries that are loaded at runtime.
 *
 *<pre>
 * // Create static Plugin object by using the provided macro
 * INIT_PLUGIN(Plugin);
 *</pre>
 * @short Plugin support
 */
class Plugin : public GenObject
{
public:
    /**
     * Creates a new Plugin container.
     * @param name the undecorated name of the library that contains the plugin
     */
    Plugin(const char *name);

    /**
     * Creates a new Plugin container.
     * Alternate constructor which is also the default.
     */
    Plugin();

    /**
     * Destroys the plugin.
     * The destructor must never be called directly - the Loader will do it when @ref refCount() reaches zero.
     */
    virtual ~Plugin();

    /**
     * Initialize the plugin after it was loaded and registered.
     */
    virtual void initialize() = 0;

    /**
     * Check if the module is actively used.
     * @return True if the plugin is in use, false if should be ok to restart
     */
    virtual bool isBusy() const
	{ return false; }
};

/**
 * Macro to create static instance of the plugin
 * @param pclass Class of the plugin to create
 */
#define INIT_PLUGIN(pclass) static pclass __plugin

/**
 * This class holds global information about the engine.
 * Note: this is a singleton class.
 *
 * @short Engine globals
 */
class Engine
{
    friend class EnginePrivate;
public:
    /**
     * Main entry point to be called directly from a wrapper program
     * @param argc Argument count
     * @param argv Argument array
     * @param environ Environment variables
     * @return Program exit code
     */
    static int main(int argc, const char **argv, const char **environ);

    /**
     * Run the engine.
     * @return Error code, 0 for success
     */
    int run();

    /**
     * Get a pointer to the unique instance.
     * @return A pointer to the singleton instance of the engine
     */
    static Engine *self();

    /**
     * Register or unregister a plugin to the engine.
     * @param plugin A pointer to the plugin to (un)register
     * @param reg True to register (default), false to unregister
     * @return True on success, false on failure
     */
    static bool Register(const Plugin *plugin, bool reg = true);

    /**
     * The configuration directory path
     */
    inline static String configFile(const char *name)
	{ return s_cfgpath+"/"+name+s_cfgsuffix; }

    /**
     * The configuration directory path
     */
    inline static String &configPath()
	{ return s_cfgpath; }

    /**
     * The module loading path
     */
    inline static String &modulePath()
	{ return s_modpath; }

    /**
     * The module suffix
     */
    inline static String &moduleSuffix()
	{ return s_modsuffix; }

    /**
     * Reinitialize the plugins
     */
    static void init();

    /**
     * Stop the engine and the entire program
     * @param code Return code of the program
     */
    static void halt(unsigned int code);

    /**
     * Check if the engine is currently exiting
     * @return True if exiting, false in normal operation
     */
    static bool exiting()
	{ return (s_haltcode != -1); }

    /**
     * Installs a handler in the dispatcher.
     * @param handler A pointer to the handler to install
     * @return True on success, false on failure
     */
    static bool install(MessageHandler *handler);

    /**
     * Uninstalls a handler drom the dispatcher.
     * @param handler A pointer to the handler to uninstall
     * @return True on success, false on failure
     */
    static bool uninstall(MessageHandler *handler);

    /**
     * Enqueue a message in the message queue
     * @param msg Pointer to the message to enqueue
     * @return True if enqueued, false on error (already queued)
     */
    static bool enqueue(Message *msg);

    /**
     * Convenience function.
     * Enqueue a new parameterless message in the message queue
     * @param name Name of the empty message to put in queue
     * @return True if enqueued, false on error (already queued)
     */
    inline static bool enqueue(const char *name)
	{ return (name && *name) ? enqueue(new Message(name)) : false; }

    /**
     * Dispatch a message to the registered handlers
     * @param msg Pointer to the message to dispatch
     * @return True if one handler accepted it, false if all ignored
     */
    static bool dispatch(Message *msg);

    /**
     * Dispatch a message to the registered handlers
     * @param msg The message to dispatch
     * @return True if one handler accepted it, false if all ignored
     */
    static bool dispatch(Message &msg);

    /**
     * Convenience function.
     * Dispatch a parameterless message to the registered handlers
     * @param name The name of the message to create and dispatch
     * @return True if one handler accepted it, false if all ignored
     */
    static bool dispatch(const char *name);

    /**
     * Install or remove a hook to catch messages after being dispatched
     * @param hookFunc Pointer to a callback function
     */
    inline void setHook(void (*hookFunc)(Message &, bool) = 0)
	{ m_dispatcher.setHook(hookFunc); }

    /**
     * Get a count of plugins that are actively in use
     * @return Count of plugins in use
     */
    int usedPlugins();

protected:
    /**
     * Destroys the engine and everything. You must not call it directly,
     * @ref run() will do it for you.
     */
    ~Engine();

    /**
     * Loads one plugin from a shared object file
     * @return True if success, false on failure
     */
    bool loadPlugin(const char *file);

    /**
     * Loads the plugins from the plugins directory
     */
    void loadPlugins();

    /**
     * Initialize all registered plugins
     */
    void initPlugins();

private:
    Engine();
    ObjList m_libs;
    MessageDispatcher m_dispatcher;
    static Engine *s_self;
    static String s_cfgpath;
    static String s_cfgsuffix;
    static String s_modpath;
    static String s_modsuffix;
    static int s_haltcode;
    static int s_maxworkers;
    static bool s_init;
    static bool s_dynplugin;
};

}; // namespace TelEngine

#endif /* __TELENGINE_H */
