/**
 * yatemath.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Math data types
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2015 Null Team
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

#ifndef __YATEMATH_H
#define __YATEMATH_H

#include <yateclass.h>
#include <math.h>
#include <string.h>

namespace TelEngine {

#ifdef DEBUG
#ifdef _WINDOWS
#define YMATH_FAIL(cond,...) { \
    if (!(cond)) \
	Debug(DebugFail,__VA_ARGS__); \
}
#else
#define YMATH_FAIL(cond,args...) { \
    if (!(cond)) \
	Debug(DebugFail,args); \
}
#endif
#else
#ifdef _WINDOWS
#define YMATH_FAIL do { break; } while
#else
#define YMATH_FAIL(arg...)
#endif
#endif

/**
 * This class implements a complex number
 * @short A Complex (float) number
 */
class YATE_API Complex
{
public:
    /**
     * Constructor
     */
    inline Complex()
	: m_real(0), m_imag(0)
	{}

    /**
     * Constructor
     * @param real The real part of the complex number
     * @param imag The imaginary part of a complex number
     */
    inline Complex(float real, float imag = 0)
	: m_real(real), m_imag(imag)
	{}

    /**
     * Copy constructor
     * @param c The source complex number
     */
    inline Complex(const Complex& c)
	: m_real(c.m_real), m_imag(c.m_imag)
	{}

    /**
     * Obtain the real part of the complex number
     * @return The real part
     */
    inline float re() const
	{ return m_real; }

    /**
     * Set the real part of the complex number
     * @param r The new real part value
     */
    inline void re(float r)
	{ m_real = r; }

    /**
     * Obtain the imaginary part of a complex number
     * @return The imaginary part
     */
    inline float im() const
	{ return m_imag; }

    /**
     * Set the imaginary part of the complex number
     * @param i The new imaginary part value
     */
    inline void im(float i)
	{ m_imag = i; }

    /**
     * Set data
     * @param r The real part of the complex number
     * @param i The imaginary part of a complex number
     * @return A reference to this object
     */
    inline Complex& set(float r = 0, float i = 0) {
	    m_real = r;
	    m_imag = i;
	    return *this;
	}

    /**
     * Equality operator
     * @param c Complex number to compare with
     * @return True if equal, false otherwise
     */
    inline bool operator==(const Complex& c) const
	{ return m_real == c.m_real && m_imag == c.m_imag; }

    /**
     * Inequality operator
     * @param c Complex number to compare with
     * @return True if not equal, false otherwise
     */
    inline bool operator!=(const Complex& c) const
	{ return m_real != c.m_real || m_imag != c.m_imag; }

    /**
     * Assignment operator
     * @param c Complex number to assign
     * @return A reference to this object
     */
    inline Complex& operator=(const Complex& c)
	{ return set(c.m_real,c.m_imag); }

    /**
     * Assignment operator. Set the real part, reset the imaginary one
     * @param real New real part value
     * @return A reference to this object
     */
    inline Complex& operator=(float real)
	{ return set(real); }

    /**
     * Addition operator
     * @param c Complex number to add
     * @return A reference to this object
     */
    inline Complex& operator+=(const Complex& c)
	{ return set(m_real + c.m_real,m_imag + c.m_imag); }

    /**
     * Addition operator. Add a value to the real part
     * @param real Value to add to real part
     * @return A reference to this object
     */
    inline Complex& operator+=(float real) {
	    m_real += real;
	    return *this;
	}

    /**
     * Substraction operator
     * @param c Complex number to substract from this one
     * @return A reference to this object
     */
    inline Complex& operator-=(const Complex& c)
	{ return set(m_real - c.m_real,m_imag - c.m_imag); }

    /**
     * Substraction operator. Substract a value a value from the real part
     * @param real Value to substract from real part
     * @return A reference to this object
     */
    inline Complex& operator-=(float real) {
	    m_real -= real;
	    return *this; 
	}

    /**
     * Multiplication operator
     * @param c Complex number to multiply with
     * @return A reference to this object
     */
    inline Complex& operator*=(const Complex& c) {
	    return set(m_real * c.m_real - m_imag * c.m_imag,
		m_real * c.m_imag + m_imag * c.m_real);
	}

    /**
     * Multiplication operator. Multiply this number with a float number
     * @param f Value to multiply with
     * @return A reference to this object
     */
    inline Complex& operator*=(float f)
	{ return set(m_real * f,m_imag * f); }

    /**
     * Division operator
     * @param c Complex number to devide with
     * @return A reference to this object
     */
    inline Complex& operator/=(const Complex& c) {
	    float tmp = c.norm2();
	    return set((m_real * c.m_real + m_imag * c.m_imag) / tmp,
		(-m_real * c.m_imag + m_imag * c.m_real) / tmp);
	}

    /**
     * Division operator
     * @param f Float number to devide with
     * @return A reference to this object
     */
    inline Complex& operator/=(float f)
	{ return set(m_real / f,m_imag / f); }

    /**
     * Compute the absolute value of this complex number
     * @return The result
     */
    inline float abs() const
	{ return ::sqrtf(norm2()); }

    /**
     * Compute the modulus value of this complex number
     * @return The result
     */
    inline float mod() const
	{ return abs(); }

    /**
     * Compute the the argument of this complex number
     * @return Ther result
     */
    inline float arg() const
	{ return ::atan(m_imag / m_real); }

    /**
     * Computes the exponential of this complex number
     * @return The result
     */
    inline Complex exp() const {
	    float r = ::expf(m_real);
	    return Complex(r * ::cosf(m_imag),r * ::sinf(m_imag));
	}

    /**
     * Compute the norm of this complex number
     * @return The result
     */
    inline float norm() const
	{ return abs(); }

    /**
     * Compute the norm2 value of this complex number
     * @return The result
     */
    inline float norm2() const
	{ return m_real * m_real + m_imag * m_imag; }

private:
    float m_real;                        // The real part
    float m_imag;                        // The imaginary part
};


/**
 * This class holds a ref counted storage
 * @short A fixed ref counted storage
 */
class YATE_API RefStorage : public RefObject
{
    YCLASS(RefStorage,RefObject)
    YNOCOPY(RefStorage); // No automatic copies please
public:
    /**
     * Constructor
     * @param value Data to assign, may be NULL to fill with zeros
     * @param len Length of data, may be zero (then value is ignored)
     */
    inline RefStorage(const void* value, unsigned int len)
	: m_data((void*)value,len)
	{}

    /**
     * Get the length of the stored data
     * @return The length of the stored data, zero for NULL
     */
    inline unsigned int length() const
	{ return m_data.length(); }

    /**
     * Get a pointer to a byte range inside the stored data
     * @param offs Byte offset inside the stored data
     * @param len Number of bytes that must be valid starting at offset (must not be 0)
     * @return A pointer to the data or NULL if the range is not available
     */
    inline void* data(unsigned int offs, unsigned int len) const
	{ return len ? m_data.data(offs,len) : 0; }

    /**
     * Copy data to this storage
     * @param buf Buffer to copy
     * @param len The number of bytes to copy
     * @param offs The start index in this storage
     * @return True on success, false if there is not enough space in our storage or
     *  the buffer pointer is NULL
     */
    inline bool set(const void* buf, unsigned int len, unsigned int offs = 0)
	{ return copy(data(offs,len),buf,len); }

    /**
     * Fill a buffer
     * @param dest Destination buffer
     * @param len The number of bytes to fill
     * @param val Value to fill with
     */
    static inline void fill(void* dest, unsigned int len, int val = 0) {
	    if (dest && len)
		::memset(dest,val,len);
	}

    /**
     * Copy data
     * @param dest Destination buffer
     * @param src Source buffer
     * @param len The number of bytes to copy
     * @return True on success, false if parameters are invalid
     */
    static inline bool copy(void* dest, const void* src, unsigned int len) {
	    if (!(len && dest && src))
		return len == 0;
	    if (dest != src)
		::memcpy(dest,src,len);
	    return true;
	}

    /**
     * Compare data
     * @param buf1 First buffer
     * @param buf2 Second buffer
     * @param len The number of bytes to compare
     * @return True if equal
     */
    static inline bool equals(const void* buf1, const void* buf2, unsigned int len) {
	    if (len && buf1 && buf2)
		return (buf1 == buf2) || (::memcmp(buf1,buf2,len) == 0);
	    return true;
	}

    /**
     * Split a string and append lines to another one
     * @param buf Destination string
     * @param str Input string
     * @param lineLen Line length, characters to copy
     * @param offset Offset in first line (if incomplete). No data will be
     *  added on first line if offset is greater then line length
     * @param linePrefix Prefix for new lines.
     *  Set it to empty string or 0 to use the suffix
     * @param suffix End of line for the last line
     * @return Destination string address
     */
    static String& dumpSplit(String& buf, const String& str, unsigned int lineLen,
	unsigned int offset = 0, const char* linePrefix = 0,
	const char* suffix = "\r\n");

private:
    RefStorage() {}; // No default constructor please

    DataBlock m_data;
};


/**
 * Base class for vector class(es).
 * Its purpose is to offer a common interface when processing lists
 * @short Base class for vector class(es)
 */
class YATE_API MathVectorBase : public GenObject
{
    YCLASS(MathVectorBase,GenObject)
public:
    /**
     * Destructor. Does nothing, keeps the compiler satisfied
     */
    virtual ~MathVectorBase()
	{}

    /**
     * Retrieve vector size in bytes
     * @return Vector size in bytes
     */
    virtual unsigned int vectorSize() const = 0;
};


/**
 * Template for vectors holding a fixed storage and a slice in it.
 * This class works with objects not holding pointers: it uses memcpy to copy data
 * @short A slice vector
 */
template <class Obj> class SliceVector : public MathVectorBase
{
public:
    /**
     * Constructor
     */
    inline SliceVector()
	: m_storage(0), m_data(0), m_length(0), m_maxLen(0)
	{}

    /**
     * Copy constructor.
     * Builds a slice of another vector
     * @param other Original vector
     */
    inline SliceVector(const SliceVector& other)
	: m_storage(0), m_data(0), m_length(0), m_maxLen(0)
	{ initSlice(false,other); }

    /**
     * Constructor.
     * Build the vector storage
     * @param len Length of data
     * @param buf Optional init buffer ('len' elements will be copied from it to storage)
     * @param maxLen Optional vector maximum length
     *  (it will be adjusted to be at least len)
     */
    explicit inline SliceVector(unsigned int len, const Obj* buf = 0,
	unsigned int maxLen = 0)
	: m_storage(0), m_data(0), m_length(0), m_maxLen(0)
	{ initStorage(len,buf,maxLen); }

    /**
     * Constructor.
     * Build a vector by concatenating two existing ones
     * @param v1 The first vector
     * @param v2 The second vector
     */
    explicit inline SliceVector(const SliceVector& v1, const SliceVector& v2)
	: m_storage(0), m_data(0), m_length(0), m_maxLen(0) {
	    if (!initStorage(v1.length(),v1.data(),v1.length() + v2.length()))
		return;
	    resizeMax();
	    m_storage->set(v2.data(),v2.size(),v1.size());
	}

    /**
     * Constructor.
     * Build a vector by concatenating three existing ones
     * @param v1 The first vector
     * @param v2 The second vector
     * @param v3 The third vector
     */
    explicit inline SliceVector(const SliceVector& v1, const SliceVector& v2,
	const SliceVector& v3)
	: m_storage(0), m_data(0), m_length(0), m_maxLen(0) {
	    unsigned int n = v1.length() + v2.length() + v3.length();
	    if (!initStorage(v1.length(),v1.data(),n))
		return;
	    resizeMax();
	    m_storage->set(v2.data(),v2.size(),v1.size());
	    m_storage->set(v3.data(),v3.size(),v1.size() + v2.size());
	}

    /**
     * Constructor.
     * Builds a slice of another vector
     * @param other Original vector
     * @param offs Offset in the original vector
     * @param len The number of elements (0 to use all available from offset)
     */
    explicit inline SliceVector(const SliceVector& other, unsigned int offs,
	unsigned int len = 0)
	: m_storage(0), m_data(0), m_length(0), m_maxLen(0)
	{ initSlice(false,other,offs,len); }

    /**
     * Destructor
     */
    virtual ~SliceVector()
	{ setData(); }

    /**
     * Get a pointer to data if 'len' elements are available from offset
     * @param offs The offset
     * @param len The number of elements to retrieve (must not be 0)
     * @return A pointer to data at requested offset,
     *  NULL if there is not enough data available
     */
    inline Obj* data(unsigned int offs, unsigned int len) {
	    if (len && length() && offs + len <= length())
		return m_data + offs;
	    return 0;
	}

    /**
     * Get a pointer to data if 'len' elements are available from offset
     * @param offs The offset
     * @param len The number of elements to retrieve (must not be 0)
     * @return A pointer to data at requested offset,
     *  NULL if there is not enough data available
     */
    inline const Obj* data(unsigned int offs, unsigned int len) const {
	    if (len && length() && offs + len <= length())
		return m_data + offs;
	    return 0;
	}

    /**
     * Get a pointer to data from offset to vector end
     * @param offs The offset
     * @return A pointer to data at requested offset, NULL if there is no data available
     */
    inline Obj* data(unsigned int offs = 0)
	{ return data(offs,length()); }

    /**
     * Get a pointer to data from offset to vector end
     * @param offs The offset
     * @return A pointer to data at requested offset, NULL if there is no data available
     */
    inline const Obj* data(unsigned int offs = 0) const
	{ return data(offs,length()); }

    /**
     * Get a pointer to data from offset to vector end
     * @param offs The offset
     * @param len The number of elements to retrieve (must not be 0)
     * @param eod Pointer to be filled with end of data element
     *  (pointer to first element after requested number of elements)
     * @return A pointer to data data from requested offset,
     *  NULL if there is not enough data available
     */
    inline Obj* data(unsigned int offs, unsigned int len, Obj*& eod) {
	    Obj* d = data(offs,len);
	    eod = end(d,len);
	    return d;
	}

    /**
     * Get a pointer to data from offset to vector end
     * @param offs The offset
     * @param len The number of elements to retrieve (must not be 0)
     * @param eod Pointer to be filled with end of data element
     *  (pointer to first element after requested number of elements)
     * @return A pointer to data data from requested offset,
     *  NULL if there is not enough data available
     */
    inline const Obj* data(unsigned int offs, unsigned int len, const Obj*& eod) const {
	    const Obj* d = data(offs,len);
	    eod = end(d,len);
	    return d;
	}

    /**
     * Get the length of the vector
     * @return The length of the vector
     */
    inline unsigned int length() const
	{ return m_length; }

    /**
     * Get the maximum length of the vector
     * @return The maximum length of the vector
     *  (0 if the vector don't have a storage buffer)
     */
    inline unsigned int maxLen() const
	{ return m_maxLen; }

    /**
     * Get the vector size in bytes
     * @return Vector size in bytes
     */
    inline unsigned int size() const
	{ return size(length()); }

    /**
     * Retrieve the available number of elements from given offset
     *  (not more than required number)
     * @param offs The offset
     * @param len Required number of elements (-1: all available from offset)
     * @return The available number of elements from given offset
     *  (may be less than required)
     */
    inline unsigned int available(unsigned int offs, int len = -1) const {
	    if (len && offs < length()) {
		unsigned int rest = length() - offs;
		return (len < 0 || rest <= (unsigned int)len) ? rest : (unsigned int)len;
	    }
	    return 0;
	}
	
    /**
     * Retrieve the available number of elements from given offset.
     * Clamp the available number of elements to requested value
     * @param clamp Maximum number of elements to check
     * @param offs The offset
     * @param len Required number of elements (-1: all available from offset)
     * @return The available number of elements from given offset
     */
    inline unsigned int availableClamp(unsigned int clamp, unsigned int offs = 0,
	int len = -1) const {
	    offs = available(offs,len);
	    return clamp <= offs ? clamp : offs;
	}

    /**
     * Retrieve vector size in bytes
     * @return Vector size in bytes
     */
    virtual unsigned int vectorSize() const
	{ return size(); }

    /**
     * Change the vector length without changing the contents.
     * If the vector length is increased the new elements' value are not reset
     * @param len New vector length
     * @return True on success, false if requested length is greater than max length
     */
    inline bool resize(unsigned int len) {
	    if (len <= maxLen()) {
		m_length = len;
		return true;
	    }
	    return false;
	}

    /**
     * Change the vector length to maximum allowed without changing the contents
     */
    inline void resizeMax()
	{ resize(maxLen()); }

    /**
     * Steal other vector's data
     * @param other Original vector
     */
    inline void steal(SliceVector& other) {
	    m_storage = other.m_storage;
	    m_data = other.m_data;
	    m_length = other.m_length;
	    m_maxLen = other.m_maxLen;
	    other.m_storage = 0;
	    other.m_data = 0;
	    other.m_length = other.m_maxLen = 0;
	}

    /**
     * Change the vector storage (re-allocate)
     * @param len New vector length (0 to clear vector data)
     * @param maxLen Optional vector maximum length
     *  (it will be adjusted to be at least len)
     */
    inline void resetStorage(unsigned int len, unsigned int maxLen = 0) {
	    setData();
	    initStorage(len,0,maxLen);
	}

    /**
     * Set a slice containing another vector
     * @param other Original vector
     * @param offs Offset in the original vector
     * @param len The number of elements (0 to use all available from offset)
     * @return True on success, false on failure
     */
    inline bool setSlice(const SliceVector& other, unsigned int offs = 0,
	unsigned int len = 0)
	{ return initSlice(true,other,offs,len); }

    /**
     * Retrieve vector head
     * @param len The number of elements to retrieve
     * @return A vector containing the first elements of this vector
     */
    inline SliceVector head(unsigned int len) const
	{ return slice(0,len); }

    /**
     * Retrieve vector head
     * @param dest Destination vector
     * @param len The number of elements to retrieve
     * @return True on success, false on failure (not enough data in our vector)
     */
    inline bool head(SliceVector& dest, unsigned int len) const
	{ return slice(dest,0,len); }

    /**
     * Retrieve vector tail (last elements)
     * @param len The number of elements to retrieve
     * @return A vector containing the last elements of this vector
     */
    inline SliceVector tail(unsigned int len) const {
	    if (len < length())
		return SliceVector(*this,length() - len,len);
	    return SliceVector();
	}

    /**
     * Retrieve vector tail (last elements)
     * @param len The number of elements to retrieve
     * @param dest Destination vector
     * @return True on success, false on failure (not enough data in our vector)
     */
    inline bool tail(SliceVector& dest, unsigned int len) const {
	    if (len <= length())
		return dest.initSlice(true,*this,length() - len,len);
	    dest.setData();
	    return false;
	}

    /**
     * Retrieve a vector slice
     * @param offs Offset in our vector
     * @param len The number of elements to retrieve
     * @return A vector containing the requested slice
     *  (empty if offset/length are invalid)
     */
    inline SliceVector slice(unsigned int offs, unsigned int len) const
	{ return SliceVector(*this,offs,len); }

    /**
     * Set a slice of this vector to another one.
     * The destination vector will be changed
     * @param dest Destination vector
     * @param offs Offset in our vector
     * @param len The number of elements (0 to use all available from offset)
     * @return True on success, false on failure (not enough data in our vector)
     */
    inline bool slice(SliceVector& dest, unsigned int offs,
	unsigned int len = 0) const
	{ return dest.initSlice(true,*this,offs,len); }

    /**
     * Copies elements from another vector to this one.
     * NOTE: This method don't check for overlapping data
     * @param src The source vector
     * @param len The number of elements to copy
     * @param offs The start index in our vector
     * @param srcOffs The start index in the source vector
     * @return True on success, false on failure (not enough data in source vector or
     *  not enough space in this vector)
     */
    inline bool copy(const SliceVector& src, unsigned int len,
	unsigned int offs = 0, unsigned int srcOffs = 0)
	{ return RefStorage::copy(data(offs,len),src.data(srcOffs,len),size(len)); }

    /**
     * Fill the buffer with 0
     * @param offs The offset
     * @param len The number of elements to retrieve (must not be 0)
     */
    inline void bzero(unsigned int offs, unsigned int len)
	{ RefStorage::fill(data(offs,len),size(len)); }

    /**
     * Fill the buffer with 0
     */
    inline void bzero()
	{ RefStorage::fill(data(),size()); }

    /**
     * Fill the vector with a given value
     * @param value The value to be set in this vector
     */
    inline void fill(const Obj& value) {
	    Obj* d = data();
	    for (Obj* last = end(d,length()); d != last; ++d)
		*d = value;
	}

    /**
     * Apply an unary function to all elements in this vector
     * @param func Function to apply
     */
    inline void apply(void (*func)(Obj&)) {
	    Obj* d = data();
	    for (Obj* last = end(d,length()); d != last; ++d)
		(*func)(*d);
	}

    /**
     * Sum vector values
     * @return The sum of the vector elements
     */
    inline Obj sum() const {
	    Obj result(0);
	    const Obj* d = data();
	    for (const Obj* last = end(d,length()); d != last; ++d)
		result += *d;
	    return result;
	}

    /**
     * Apply a function to all vector elements and take the sum of the results
     * @param func Function to apply
     * @return The result
     */
    inline Obj sumApply(Obj (*func)(const Obj&)) const {
	    Obj result(0);
	    const Obj* d = data();
	    for (const Obj* last = end(d,length()); d != last; ++d)
		result += (*func)(*d);
	    return result;
	}

    /**
     * Apply a function to all vector elements and take the sum of the results
     * @param func Function to apply
     * @return The result
     */
    inline float sumApplyF(float (*func)(const Obj&)) const {
	    float result = 0;
	    const Obj* d = data();
	    for (const Obj* last = end(d,length()); d != last; ++d)
		result += (*func)(*d);
	    return result;
	}

    /**
     * Sum this vector with another one
     * @param other Vector to sum with this one
     * @return True on sucess, false on failure (vectors don't have the same length)
     */
    inline bool sum(const SliceVector& other) {
	    if (length() != other.length())
		return false;
	    const Obj* od = other.m_data;
	    Obj* d = data();
	    for (Obj* last = end(d,length()); d != last; ++d, ++od)
		*d += *od;
	    return true;
	}

    /**
     * Add a value to each element of the vector
     * @param value Value to add
     */
    inline void sum(const Obj& value) {
	    Obj* d = data();
	    for (Obj* last = end(d,length()); d != last; ++d)
		*d += value;
	}

    /**
     * Substract another vector from this one
     * @param other Vector to substract
     * @return True on sucess, false on failure (vectors don't have the same length)
     */
    inline bool sub(const SliceVector& other) {
	    if (length() != other.length())
		return false;
	    const Obj* od = other.m_data;
	    Obj* d = data();
	    for (Obj* last = end(d,length()); d != last; ++d, ++od)
		*d -= *od;
	    return true;
	}

    /**
     * Substract a value from each element of the vector
     * @param value Value to substract
     */
    inline void sub(const Obj& value) {
	    Obj* d = data();
	    for (Obj* last = end(d,length()); d != last; ++d)
		*d -= value;
	}

    /**
     * Multiply this vector with another one
     * @param other Vector to multiply with
     * @return True on sucess, false on failure (vectors don't have the same length)
     */
    inline bool mul(const SliceVector& other) {
	    if (length() != other.length())
		return false;
	    const Obj* od = other.m_data;
	    Obj* d = data();
	    for (Obj* last = end(d,length()); d != last; ++d, ++od)
		*d *= *od;
	    return true;
	}

    /**
     * Multiply this vector with a value
     * @param value Value to multiply with
     */
    inline void mul(const Obj& value) {
	    Obj* d = data();
	    for (Obj* last = end(d,length()); d != last; ++d)
		*d *= value;
	}

    /**
     * Multiply this vector with a value
     * @param value Value to multiply with
     */
    inline void mul(float value) {
	    Obj* d = data();
	    for (Obj* last = end(d,length()); d != last; ++d)
		*d *= value;
	}

    /**
     * Indexing operator with unsigned int
     * @param index Index of element to retrieve
     * @return The element at requested index
     */
    inline Obj& operator[](unsigned int index) {
	    YMATH_FAIL(index < m_length,
		"SliceVector::operator[] index out of bounds [%p]",this);
	    return m_data[index];
	}

    /**
     * Indexing operator with unsigned int
     * @param index Index of element to retrieve
     * @return The element at requested index
     */
    inline const Obj& operator[](unsigned int index) const {
	    YMATH_FAIL(index < m_length,
		"SliceVector::operator[] index out of bounds [%p]",this);
	    return m_data[index];
	}

    /**
     * Indexing operator with signed int
     * @param index Index of element to retrieve
     * @return The element at requested index
     */
    inline Obj& operator[](signed int index) {
	    YMATH_FAIL((unsigned int)index < m_length,
		"SliceVector::operator[] index out of bounds [%p]",this);
	    return m_data[index];
	}

    /**
     * Indexing operator with signed int
     * @param index Index of element to retrieve
     * @return The element at requested index
     */
    inline const Obj& operator[](signed int index) const {
	    YMATH_FAIL((unsigned int)index < m_length,
		"SliceVector::operator[] index out of bounds [%p]",this);
	    return m_data[index];
	}

    /**
     * Equality operator
     * @param other Original vector
     * @return True if the vectors are equal, false otherwise
     */
    inline bool operator==(const SliceVector& other) const
	{ return equals(other); }

    /**
     * Inequality operator
     * @param other Original vector
     * @return True if the vectors are not equal, false otherwise
     */
    inline bool operator!=(const SliceVector& other) const
	{ return !equals(other); }

    /**
     * Asignment operator. Builds a slice of another vector
     * @param other Original vector
     * @return A reference to this vector
     */
    inline SliceVector& operator=(const SliceVector& other) {
	    setSlice(other);
	    return *this;
	}

    /**
     * Sum this vector with another one
     * @param other Vector to sum with this one
     * @return A reference to this vector
     */
    inline SliceVector& operator+=(const SliceVector& other) {
	    YMATH_FAIL(length() == other.length(),
		"SliceVector(+=): invalid lengths [%p]",this);
	    sum(other);
	    return *this;
	}

    /**
     * Add a value to each element of the vector
     * @param value Value to add
     * @return A reference to this vector
     */
    inline SliceVector& operator+=(const Obj& value) {
	    sum(value);
	    return *this;
	}

    /**
     * Substract another vector from this one
     * @param other Vector to substract
     * @return A reference to this vector
     */
    inline SliceVector& operator-=(const SliceVector& other) {
	    YMATH_FAIL(length() == other.length(),
		"SliceVector(-=): invalid lengths [%p]",this);
	    sub(other);
	    return *this;
	}

    /**
     * Substract a value from each element of the vector
     * @param value Value to substract
     * @return A reference to this vector
     */
    inline SliceVector& operator-=(const Obj& value) {
	    sub(value);
	    return *this;
	}

    /**
     * Multiply this vector with another one
     * @param other Vector to multiply with
     * @return A reference to this vector
     */
    inline SliceVector& operator*=(const SliceVector& other) {
	    YMATH_FAIL(length() == other.length(),
		"SliceVector(*=): invalid lengths [%p]",this);
	    mul(other);
	    return *this;
	}

    /**
     * Multiply this vector with a value
     * @param value Value to multiply with
     * @return A reference to this vector
     */
    inline SliceVector& operator*=(const Obj& value) {
	    mul(value);
	    return *this;
	}

    /**
     * Multiply this vector with a value
     * @param value Value to multiply with
     * @return A reference to this vector
     */
    inline SliceVector& operator*=(float value) {
	    mul(value);
	    return *this;
	}

    /**
     * Compare this vector to another one (compare storage)
     * @param other Vector to compare with
     * @return True if they are equal
     */
    inline bool equals(const SliceVector& other) const {
	    return length() == other.length() &&
		RefStorage::equals(data(),other.data(),size());
	}

    /**
     * Dump data to a string (append)
     * @param buf Destination string
     * @param func Pointer to function who appends the object to a String
     *  (0 to dump all available from offset)
     * @param sep Vector elements separator
     * @param fmt Optional format to use
     * @return Destination string address
     */
    String& dump(String& buf,
	String& (*func)(String& s, const Obj& o, const char* sep, const char* fmt),
	const char* sep = ",", const char* fmt = 0) const {
	    const Obj* d = data();
	    if (!(d && func))
		return buf;
	    String localBuf;
	    for (const Obj* last = end(d,length()); d != last; ++d)
		(*func)(localBuf,*d,sep,fmt);
	    return buf.append(localBuf);
	}

    /**
     * Dump this vector to string, split it and append lines to a buffer.
     * Line prefix length is not included when line length is calculated.
     * Separator length is included in line length
     * @param buf Destination string
     * @param lineLen Line length in bytes
     * @param func Pointer to function who append the object to a String
     * @param offset Offset in first line (if incomplete). No data will be
     *  added on first line if offset is greater then line length
     * @param linePrefix Prefix for new lines. Empty string to use the suffix
     * @param suffix String to always add to final result (even if no data dumped)
     * @param sep Vector elements separator
     * @param fmt Optional format to use
     * @return Destination string address
     */
    String& dump(String& buf, unsigned int lineLen,
	String& (*func)(String& s, const Obj& o, const char* sep, const char* fmt),
	unsigned int offset = 0, const char* linePrefix = 0,
	const char* suffix = "\r\n", const char* sep = ",", const char* fmt = 0) const {
	    const Obj* d = data();
	    if (!(d && func))
		return buf.append(suffix);
	    if (TelEngine::null(linePrefix))
		linePrefix = suffix;
	    if (!lineLen || TelEngine::null(linePrefix))
		return dump(buf,func,sep,fmt) << suffix;
	    String localBuf;
	    for (const Obj* last = end(d,length()); d != last;) {
		String tmp;
		(*func)(tmp,*d,0,fmt);
		if (++d != last)
		    tmp << sep;
		offset += tmp.length();
		if (offset > lineLen) {
		    localBuf << linePrefix;
		    offset = tmp.length();
		}
		localBuf << tmp;
	    }
	    return buf << localBuf << suffix;
	}

    /**
     * Hexify data
     * @param buf Destination string
     * @param sep Optional separator
     * @return Destination string address
     */
    inline String& hexify(String& buf, char sep = 0) const
	{ return buf.hexify((void*)data(),size(),sep); }
	
    /**
     * Hexify data, split it and append lines to a string
     * @param buf Destination string
     * @param lineLen Line length, characters to copy
     * @param offset Offset in first line (if incomplete). No data will be
     *  added on first line if offset is greater then line length
     * @param linePrefix Prefix for new lines.
     *  Set it to empty string or 0 to use the suffix
     * @param suffix End of line for the last line
     * @return Destination string address
     */
    inline String& dumpHex(String& buf, unsigned int lineLen,
	unsigned int offset = 0, const char* linePrefix = 0,
	const char* suffix = "\r\n") const {
	    String h;
	    return RefStorage::dumpSplit(buf,hexify(h),lineLen,offset,linePrefix,suffix);
	}
	
    /**
     * Reset storage from a hexadecimal string representation.
     * Clears the vector at start, i.e. the vector will be empty on failure.
     * The vector may be empty on success also.
     * Each octet must be represented in the input string with 2 hexadecimal characters.
     * If a separator is specified, the octets in input string must be separated using
     *  exactly 1 separator. Only 1 leading or 1 trailing separators are allowed.
     * @param str Input character string
     * @param len Length of input string
     * @param sep Separator character used between octets.
     *  [-128..127]: expected separator (0: no separator is expected).
     *  Detect the separator if other value is given
     * @return 0 on success, negative if unhexify fails,
     *  positive if the result is not a multiple of Obj size
     */
    int unHexify(const char* str, unsigned int len, int sep = 255) {
	    setData();
	    DataBlock db;
	    bool ok = (sep < -128 || sep > 127) ? db.unHexify(str,len) :
		db.unHexify(str,len,(char)sep);
	    if (ok && (db.length() % objSize() == 0)) {
		initStorage(db.length() / objSize(),(const Obj*)db.data(0,db.length()));
		return 0;
	    }
	    return ok ? 1 : -1;
	}

    /**
     * Unhexify data
     * @param str Input string
     * @param sep Separator character used between octets
     * @return See unHexify(const char*,unsigned int,char)
     */
    inline int unHexify(const String& str, int sep = 255)
	{ return unHexify(str.c_str(),str.length(),sep); }

    /**
     * Retrieve the object size
     * @return Obj size in bytes
     */
    static inline unsigned int objSize()
	{ return sizeof(Obj); }

    /**
     * Retrieve the length in bytes of a buffer containing 'count' objects
     * @param len Buffer length
     * @return Buffer length in bytes
     */
    static inline unsigned int size(unsigned int len)
	{ return len * objSize(); }

protected:
    // Return end-of-data pointer from start and given length
    inline Obj* end(Obj* start, unsigned int len)
	{ return start ? (start + len) : 0; }
    inline const Obj* end(const Obj* start, unsigned int len) const
	{ return start ? (start + len) : 0; }
    // Set data. Reset storage if we don't have a valid data pointer
    // Return true if we have valid data
    inline bool setData(Obj* data = 0, unsigned int len = 0, unsigned int maxLen = 0) {
	    m_data = data;
	    if (m_data) {
		m_length = len;
		m_maxLen = maxLen;
	    }
	    else {
		m_length = m_maxLen = 0;
		TelEngine::destruct(m_storage);
	    }
	    return m_data != 0;
	}
    // Build storage, update data. This method assumes our data is cleared
    // If data is given 'len' elements will be copied from it to storage
    inline bool initStorage(unsigned int len, const Obj* data = 0,
	unsigned int maxLen = 0) {
	    if (maxLen < len)
		maxLen = len;
	    if (!maxLen)
		return false;
	    if (!data || maxLen == len)
		m_storage = new RefStorage(data,size(maxLen));
	    else {
		m_storage = new RefStorage(0,size(maxLen));
		m_storage->set(data,size(len));
	    }
	    return setData((Obj*)m_storage->data(0,1),len,maxLen);
	}
    // Build storage from slice and update data.
    // Clear data if requested
    inline bool initSlice(bool del, const SliceVector& other, unsigned int offs = 0,
	unsigned int len = 0) {
	    if (!len)
		len = other.length();
	    Obj* d = (Obj*)other.data(offs,len);
	    if (!d) {
		if (del)
		    setData();
		return len == 0;
	    }
	    if (m_storage == other.m_storage)
		return setData(d,len,len);
	    RefStorage* tmp = other.m_storage;
	    if (tmp->ref()) {
		TelEngine::destruct(m_storage);
		m_storage = tmp;
		return setData(d,len,len);
	    }
	    Debug(DebugFail,"SliceVector storage ref() failed");
	    return del ? setData() : false;
	}

    RefStorage* m_storage;               // Vector storage
    Obj* m_data;                         // Pointer to data
    unsigned int m_length;               // Data length
    unsigned int m_maxLen;               // Max storage
};

typedef SliceVector<Complex> ComplexVector;
typedef SliceVector<float> FloatVector;
typedef SliceVector<uint8_t> ByteVector;

/**
 * This vector holds bit values using 1 byte. It implements methods operating on bits.
 * NOTE: The array indexing operator allows setting invalid values (not 1 or 0).
 *  The pack/unpack methods are safe (they will handle non 0 values as bit 1).
 *  The comparison operators may fail for vectors containing values other than 0 or 1.
 * @short A slice vector holding bits
 */
class YATE_API BitVector : public ByteVector
{
public:
    /**
     * Constructor
     */
    inline BitVector()
	{}

    /**
     * Copy constructor.
     * Builds a slice of another vector
     * @param other Original vector
     */
    inline BitVector(const BitVector& other)
	: ByteVector(other)
	{}

    /**
     * Constructor.
     * Build the vector storage
     * @param len Length of data
     * @param maxLen Optional vector maximum length
     *  (it will be adjusted to be at least len)
     */
    explicit inline BitVector(unsigned int len, unsigned int maxLen = 0)
	: ByteVector(len,0,maxLen)
	{}

    /**
     * Constructor.
     * Builds a slice of another vector
     * @param other Original vector
     * @param offs Offset in the original vector
     * @param len The number of elements (0 to use all available from offset)
     */
    explicit inline BitVector(const BitVector& other, unsigned int offs,
	unsigned int len = 0)
	: ByteVector(other,offs,len)
	{}

    /**
     * Constructor. Build from string bits
     * @param str String bits ('1' -> 1, else -> 0)
     * @param maxLen Optional vector maximum length
     */
    explicit BitVector(const char* str, unsigned int maxLen = 0);

    /**
     * Check if this vector contains valid values (0 or 1)
     * @return True on success, false if the vector contains values other than 0 or 1
     */
    bool valid() const;

    /**
     * Set float bit values from this vector (0 -> 0.0F, non 0 -> 1.0F).
     * The destination vector will be resized to this vector's length
     * @param dest The destination vector
     * @return True on success, false if destination resize failed
     */
    bool get(FloatVector& dest) const;

    /**
     * Initializes this vector from float values (0.0F -> 0, non 0 -> 1).
     * The vector will be resized to input length
     * @param input The input vector
     * @return True on success, false if resize failed
     */
    bool set(const FloatVector& input);

    /**
     * Apply XOR on vector elements using a given value's bits, MSB first.
     * Given v31,v30,...,v0 the value's bits in MSB order the result will be
     *  data()[offs] ^= v31, data()[offs+1] ^= v30 ...
     * @param value Value to use
     * @param offs Start position in this BitVector
     * @param len The number of bits to use
     */
    void xorMsb(uint32_t value, unsigned int offs = 0, uint8_t len = 32);

    /**
     * Apply XOR on vector elements using a given value's bits, MSB first.
     * Given v15,v14,...,v0 the value's bits in MSB order the result will be
     *  data()[offs] ^= v15, data()[offs+1] ^= v14 ...
     * @param value Value to use
     * @param offs Start position in this BitVector
     * @param len The number of bits to use
     */
    inline void xorMsb16(uint16_t value, unsigned int offs = 0, uint8_t len = 16)
	{ return xorMsb((uint32_t)value << 16,offs,len <= 16 ? len : 16); }

    /**
     * Pack up to 64 bits, LSB-first (i.e. first bit goes to LSB in destination)
     * @param offs The start offset
     * @param len The number of elements to be packed (-1 to pack all available).
     *  No more than 64 bits will be packed
     * @return The packed 64-bit value
     */
    uint64_t pack(unsigned int offs = 0, int len = -1) const;

    /**
     * Unpack up to 64 bits into this vector, LSB first
     * @param value Value to unpack
     * @param offs Optional start offset
     * @param len The number of bits to unpack
     */
    void unpack(uint64_t value, unsigned int offs = 0, uint8_t len = 64);

    /**
     * Unpack up to 32 bits into this vector (MSB to LSB).
     * MSB from value is the first unpacked bit
     * @param value The value to be unpacked
     * @param offs Optional start offset
     * @param len The number of bits to unpack
     */
    void unpackMsb(uint32_t value, unsigned int offs = 0, uint8_t len = 32);

    /**
     * Unpack up to 16 bits into this vector (MSB to LSB).
     * MSB from value is the first unpacked bit
     * @param value The value to be unpacked
     * @param offs Optional start offset
     * @param len The number of bits to unpack
     */
    inline void unpackMsb16(uint16_t value, unsigned int offs = 0, uint8_t len = 16)
	{ unpackMsb((uint32_t)value << 16,offs,len <= 16 ? len : 16); }

    /**
     * Pack bits into a ByteVector (LSB source to MSB in destination).
     * MSB of first byte in destination will have the same value of the
     *  first bit in this vector.
     * Remaining elements in destination are left untouched
     * @param dest Destination vector
     * @return True on success, false on failure (not enough space in destination vector)
     */
    bool pack(ByteVector& dest) const;

    /**
     * Unpack a ByteVector into this BitVector.
     * MSB of the first element in source goes to first bit in this vector.
     * Remaining bits are left untouched
     * @param src Source byte vector
     * @return True on success, false if there is not enough space to unpack
     */
    bool unpack(const ByteVector& src);

    /**
     * Append bits to string
     * @param buf Destination string
     * @param offs Optional starting index
     * @param len The number of elements to be added (negative to add all)
     * @return Destination string address
     */
    String& appendTo(String& buf, unsigned int offs = 0, int len = -1) const;

    /**
     * Build a String from vector bits and return it
     * @param offs Optional starting index
     * @param len The number of elements to be added (negative to add all)
     * @return A newly created String containing vector bits
     */
    inline String toString(unsigned int offs, int len = -1) const {
	    String tmp;
	    return appendTo(tmp,offs,len);
	}

    /**
     * Set a slice containing another vector
     * @param other Original vector
     * @param offs Offset in the original vector
     * @param len The number of elements (0 to use all available from offset)
     * @return True on success, false on failure
     */
    inline bool setSlice(const BitVector& other, unsigned int offs = 0,
	unsigned int len = 0)
	{ return initSlice(true,other,offs,len); }

    /**
     * Retrieve vector head
     * @param len The number of elements to retrieve
     * @return A vector containing the first elements of this vector
     */
    inline BitVector head(unsigned int len) const
	{ return slice(0,len); }

    /**
     * Retrieve vector head
     * @param dest Destination vector
     * @param len The number of elements to retrieve
     * @return True on success, false on failure (not enough data in our vector)
     */
    inline bool head(BitVector& dest, unsigned int len) const
	{ return slice(dest,0,len); }

    /**
     * Retrieve vector tail (last elements)
     * @param len The number of elements to retrieve
     * @return A vector containing the last elements of this vector
     */
    inline BitVector tail(unsigned int len) const {
	    if (len < length())
		return BitVector(*this,length() - len,len);
	    return BitVector();
	}

    /**
     * Retrieve vector tail (last elements)
     * @param len The number of elements to retrieve
     * @param dest Destination vector
     * @return True on success, false on failure (not enough data in our vector)
     */
    inline bool tail(BitVector& dest, unsigned int len) const {
	    if (len <= length())
		return dest.initSlice(true,*this,length() - len,len);
	    dest.setData();
	    return false;
	}

    /**
     * Retrieve a vector slice
     * @param offs Offset in our vector
     * @param len The number of elements to retrieve
     * @return A vector containing the requested slice
     *  (empty if offset/length are invalid)
     */
    inline BitVector slice(unsigned int offs, unsigned int len) const
	{ return BitVector(*this,offs,len); }

    /**
     * Set a slice of this vector to another one.
     * The destination vector will be changed
     * @param dest Destination vector
     * @param offs Offset in our vector
     * @param len The number of elements (0 to use all available from offset)
     * @return True on success, false on failure (not enough data in our vector)
     */
    inline bool slice(BitVector& dest, unsigned int offs, unsigned int len = 0) const
	{ return dest.initSlice(true,*this,offs,len); }
};


/**
 * This class global Math utility methods
 * @short Math utilities
 */
class YATE_API Math
{
public:
    /**
     * Dump a Complex number to a String
     * @param buf Destination string
     * @param val Value to dump
     * @param sep Optional separator
     * @param fmt Format to use ("%g%+gi" if not given)
     * @return Destination string address
     */
    static String& dumpComplex(String& buf, const Complex& val, const char* sep = 0,
	const char* fmt = 0);

    /**
     * Dump a float number to a String
     * @param buf Destination string
     * @param val Value to dump
     * @param sep Optional separator
     * @param fmt Format to use ("%g" if not given)
     * @return Destination string address
     */
    static String& dumpFloat(String& buf, const float& val, const char* sep = 0,
	const char* fmt = 0);
};


/**
 * Addition operator
 * @param c1 First number
 * @param c2 Second number
 * @return The result
 */
inline Complex operator+(const Complex& c1, const Complex& c2)
{
    Complex tmp(c1);
    return (tmp += c2);
}

/**
 * Addition operator
 * @param c A Complex number
 * @param f A float value
 * @return The result
 */
inline Complex operator+(const Complex& c, float f)
{
    Complex tmp(c);
    return (tmp += f);
}

/**
 * Addition operator
 * @param f The float value
 * @param c The Complex number
 * @return The result
 */
inline Complex operator+(float f, const Complex& c)
{
    return operator+(c,f);
}

/**
 * Substraction operator
 * @param c1 First number
 * @param c2 Second number
 * @return The result
 */
inline Complex operator-(const Complex& c1, const Complex& c2)
{
    Complex tmp(c1);
    return (tmp -= c2);
}

/**
 * Substraction operator
 * @param c A Complex number
 * @param f A float value
 * @return The result
 */
inline Complex operator-(const Complex& c, float f)
{
    Complex tmp(c);
    return (tmp -= f);
}

/**
 * Multiplication operator
 * @param c1 First number
 * @param c2 Second number
 * @return The result
 */
inline Complex operator*(const Complex& c1, const Complex& c2)
{
    Complex tmp(c1);
    return (tmp *= c2);
}

/**
 * Multiplication operator
 * @param c A Complex number
 * @param f A float value
 * @return The result
 */
inline Complex operator*(const Complex& c, float f)
{
    Complex tmp(c);
    return (tmp *= f);
}

/**
 * Multiplication operator
 * @param f A float value
 * @param c A Complex number
 * @return The result
 */
inline Complex operator*(float f, const Complex& c)
{
    return operator*(c,f);
}

/**
 * Division operator
 * @param c1 First number
 * @param c2 Second number
 * @return The result
 */
inline Complex operator/(const Complex& c1, const Complex& c2)
{
    Complex tmp(c1);
    return (tmp /= c2);
}

/**
 * Division operator
 * @param c A Complex number
 * @param f A float value
 * @return The result
 */
inline Complex operator/(const Complex& c, float f)
{
    Complex tmp(c);
    return (tmp /= f);
}

/**
 * Append operator: append a Complex number to a String
 * @param str Destination string
 * @param c Complex number to append
 * @return Destination string reference
 */
inline String& operator<<(String& str, const Complex& c)
{
    return Math::dumpComplex(str,c);
}

/**
 * Append operator: append a BitVector to a String
 * @param str Destination string
 * @param b Vector to append
 * @return Destination string reference
 */
inline String& operator<<(String& str, const BitVector& b)
{
    return b.appendTo(str);
}

}; // namespace TelEngine

#endif /* __YATEMATH_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
