/**
 * telephony.h
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

#ifndef __TELEPHONY_H
#define __TELEPHONY_H

#ifndef __cplusplus
#error C++ is required
#endif

#include <telengine.h>
	
/**
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

/**
 * A structure to hold information about a data format.
 */
struct FormatInfo {
    /** Standard no-blanks lowcase format name */
    const char *name;
    /** Data rate in octets/second, 0 for variable */
    int rate;
    /** Frame size in octets, 0 for non-framed formats */
    int size;
};

/**
 * A structure to build (mainly static) translator capability tables.
 * A table of such structures must end with an entry with null format names.
 */
struct TranslatorCaps {
    /** Description of source (input) data format */
    FormatInfo src;
    /** Description of destination (output) data format */
    FormatInfo dest;
    /** Computing cost in KIPS of converting a stream from src to dest */
    int cost;
};

/**
 * The DataBlock holds a data buffer with no specific formatting.
 * @short A class that holds just a block of raw data
 */
class DataBlock : public GenObject
{
public:

    /**
     * Constructs an empty data block
     */
    DataBlock();

    /**
     * Copy constructor
     */
    DataBlock(const DataBlock &value);

    /**
     * Constructs an initialized data block
     * @param value Data to assign, may be NULL to fill with zeros
     * @param len Length of data, may be zero (then @ref value is ignored)
     * @param copyData True to make a copy of the data, false to just insert the pointer
     */
    DataBlock(void *value, unsigned int len, bool copyData = true);

    /**
     * Destroys the data, disposes the memory.
     */
    virtual ~DataBlock();

    /**
     * Get a pointer to the stored data.
     * @return A pointer to the data or NULL.
     */
    inline void *data() const
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
    DataBlock& assign(void *value, unsigned int len, bool copyData = true);

    /**
     * Append data to the current block
     * @param value Data to append
     */
    void append(const DataBlock &value);

    /**
     * Insert data before the current block
     * @param value Data to insert
     */
    void insert(const DataBlock &value);

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
    DataBlock& operator=(const DataBlock &value);

    /**
     * Appending operator.
     */
    inline DataBlock& operator+=(const DataBlock &value)
	{ append(value); return *this; }

    /**
     * Convert data from a different format
     * @param src Source data block
     * @param sFormat Name of the source format
     * @param dFormat Name of the destination format
     * @param maxlen Maximum amount to convert, 0 to use source
     * @return True if converted successfully, false on failure
     */
    bool convert(const DataBlock &src, const String &sFormat,
	const String &dFormat, unsigned maxlen = 0);

private:
    void *m_data;
    unsigned int m_length;
};

/**
 * A generic data handling object
 */
class DataNode : public RefObject
{
public:
    /**
     * Construct a DataNode
     * @param format Name of the data format, default none
     */
    DataNode(const char *format = 0)
	: m_format(format), m_timestamp(0) { }

    /**
     * Get the computing cost of converting the data to the format asked
     * @param format Name of the format to check for
     * @return -1 if unsupported, 0 for native format else cost in KIPS
     */
    virtual int costFormat(const String &format)
	{ return -1; }

    /**
     * Change the format used to transfer data
     * @param format Name of the format to set for data
     * @return True if the format changed successfully, false if not changed
     */
    virtual bool setFormat(const String &format)
	{ return false; }

    /**
     * Get the name of the format currently in use
     * @return Name of the data format
     */
    inline const String &getFormat() const
	{ return m_format; }

    /**
     * Get the current position in the data stream
     * @return Timestamp of current data position
     */
    inline unsigned long timeStamp() const
	{ return m_timestamp; }

protected:
    String m_format;
    unsigned long m_timestamp;
};

/**
 * A data consumer
 */
class DataConsumer : public DataNode
{
    friend class DataSource;
public:
    /**
     * Consumer constructor
     * @param format Name of the data format, default "slin" (Signed Linear)
     */
    DataConsumer(const char *format = "slin")
	: DataNode(format), m_source(0) { }

    /**
     * Consumes the data sent to it from a source
     * @param data The raw data block to process; an empty block ends data
     * @param timeDelta Timestamp increment of data - typically samples
     */
    virtual void Consume(const DataBlock &data, unsigned long timeDelta) = 0;

    /**
     * Get the data source of this object if it's connected
     * @return A pointer to the DataSource object or NULL
     */
    DataSource *getConnSource() const
	{ return m_source; }

    /**
     * Get the data source of a translator object
     * @return A pointer to the DataSource object or NULL
     */
    virtual DataSource *getTransSource() const
	{ return 0; }

private:
    inline void setSource(DataSource *source)
	{ m_source = source; }
    DataSource *m_source;
};

/**
 * A data source
 */
class DataSource : public DataNode
{
    friend class DataTranslator;
public:
    /**
     * Source constructor
     * @param format Name of the data format, default "slin" (Signed Linear)
     */
    DataSource(const char *format = "slin")
	: DataNode(format), m_translator(0) { }

    /**
     * Source's destructor - detaches all consumers
     */
    ~DataSource();
    
    /**
     * Forwards the data to its consumers
     * @param data The raw data block to forward; an empty block ends data
     * @param timeDelta Timestamp increment of data - typically samples
     */
    void Forward(const DataBlock &data, unsigned long timeDelta = 0);

    /**
     * Attach a data consumer
     * @param consumer Data consumer to attach
     * @return True on success, false on failure
     */
    bool attach(DataConsumer *consumer);

    /**
     * Detach a data consumer
     * @param consumer Data consumer to detach
     * @return True on success, false on failure
     */
    bool detach(DataConsumer *consumer);

    /**
     * Detach all data consumers
     */
    inline void clear()
	{ m_consumers.clear(); }

    /**
     * Get the master translator object if this source is part of a translator
     * @return A pointer to the DataTranslator object or NULL
     */
    DataTranslator *getTranslator() const
	{ return m_translator; }

protected:
    /**
     * The current position in the data - format dependent, usually samples
     */
    inline void setTranslator(DataTranslator *translator)
	{ m_translator = translator; }
    DataTranslator *m_translator;
    ObjList m_consumers;
    Mutex m_mutex;
};

/**
 * A data source with a thread of its own
 */
class ThreadedSource : public DataSource
{
    friend class ThreadedSourcePrivate;
public:
    /**
     * The destructor, stops the thread
     */
    virtual ~ThreadedSource();

    /**
     * Starts the worker thread
     * @return True if started, false if an error occured
     */
    bool start(const char *name = "ThreadedSource");

    /**
     * Stops and destroys the worker thread if running
     */
    void stop();

    /**
     * Return a pointer to the worker thread
     * @return Pointer to running worker thread or NULL
     */
    Thread *thread() const;

protected:
    /**
     * Threaded Source constructor
     * @param format Name of the data format, default "slin" (Signed Linear)
     */
    ThreadedSource(const char *format = "slin")
	: DataSource(format), m_thread(0) { }

    /**
     * The worker method. You have to reimplement it as you need
     */
    virtual void run() = 0;

    /**
     * The cleanup after thread method
     */
    virtual void cleanup();

private:
    ThreadedSourcePrivate *m_thread;
};

/**
 * The DataEndpoint holds an endpoint capable of performing unidirectional
 * or bidirectional data transfers
 * @short A data transfer endpoint capable of sending and/or receiving data
 */
class DataEndpoint : public RefObject
{
public:

    /**
     * Creates am empty data ednpoint
     */
    inline DataEndpoint(const char *name = 0)
	: m_name(name), m_source(0), m_consumer(0), m_peer(0) { }

    /**
     * Destroys the endpoint, source and consumer
     */
    ~DataEndpoint();

    /**
     * Connect the source and consumer of the endpoint to a peer
     * @param peer Pointer to the peer data endpoint
     * @return True if connected, false if incompatible source/consumer
     */
    bool connect(DataEndpoint *peer);

    /**
     * Disconnect from the connected endpoint
     * @param reason Text that describes disconnect reason
     */
    inline void disconnect(const char *reason = 0)
	{ disconnect(false,reason); }

    /**
     * Set the data source of this object
     * @param source A pointer to the new source or NULL
     */
    void setSource(DataSource *source = 0);

    /**
     * Get the data source of this object
     * @return A pointer to the DataSource object or NULL
     */
    DataSource *getSource() const
	{ return m_source; }

    /**
     * Set the data consumer of this object
     * @param consumer A pointer to the new consumer or NULL
     */
    void setConsumer(DataConsumer *consumer = 0);

    /**
     * Get the data consumer of this object
     * @return A pointer to the DataConsumer object or NULL
     */
    DataConsumer *getConsumer() const
	{ return m_consumer; }

    /*
     * Get a pointer to the peer endpoint
     * @return A pointer to the peer endpoint or NULL
     */
    inline DataEndpoint *getPeer() const
	{ return m_peer; }

    /**
     * Get the name set in constructor
     * @return A reference to the name as hashed string
     */
    inline const String &name() const
	{ return m_name; }

protected:
    /**
     * Connect notification method
     */
    virtual void connected() { }

    /**
     * Disconnect notification method
     * @param final True if this disconnect was called from the destructor
     * @param reason Text that describes disconnect reason
     */
    virtual void disconnected(bool final, const char *reason) { }

    /**
     * Attempt to connect the endpoint to a peer of the same type
     * @param peer Pointer to the endpoint data driver
     * @return True if connected, false if failed native connection
     */
    virtual bool nativeConnect(DataEndpoint *peer)
	{ return false; }

    /*
     * Set the peer endpoint pointer
     * @param peer A pointer to the new peer or NULL
     * @param reason Text describing the reason in case of disconnect
     */
    void setPeer(DataEndpoint *peer, const char *reason = 0);

private:
    void disconnect(bool final, const char *reason);
    String m_name;
    DataSource *m_source;
    DataConsumer *m_consumer;
    DataEndpoint *m_peer;
};

/**
 * The DataTranslator holds a translator (codec) capable of unidirectional
 * conversion of data from one type to another
 * @short An unidirectional data translator (codec)
 */
class DataTranslator : public DataConsumer
{
    friend class TranslatorFactory;
public:
    /**
     * Construct a data translator
     * @param sFormat Name of the source format (data received from the consumer)
     * @param dFormat Name of the destination format (data supplied to the source)
     */
    DataTranslator(const char *sFormat, const char *dFormat);

    /**
     * Creates a data translator from an existing source
     * does not increment the source's reference counter
     * @param sFormat Name of the source format (data received from the consumer)
     * @param source Optional pointer to a DataSource object
     */
    DataTranslator(const char *sFormat, DataSource *source = 0);

    /**
     * Destroys the translator and its source
     */
    ~DataTranslator();

    /**
     * Get the data source of a translator object
     * @return A pointer to the DataSource object or NULL
     */
    virtual DataSource *getTransSource() const
	{ return m_tsource; }

    /**
     * Get a textual list of formats supported for a given output format
     * @param dFormat Name of destination format
     * @return Space separated list of source formats
     */
    static String srcFormats(const String &dFormat = "slin");

    /**
     * Get a textual list of formats supported for a given inpput format
     * @param sFormat Name of source format
     * @return Space separated list of destination formats
     */
    static String destFormats(const String &sFormat = "slin");

    /**
     * Finds the cost of a translator given the source and destination format names
     * @param sFormat Name of the source format (data received from the consumer)
     * @param dFormat Name of the destination format (data supplied to the source)
     * @return Cost of best (cheapest) codec or -1 if no known codec exists
     */
    static int cost(const String &sFormat, const String &dFormat);

    /**
     * Creates a translator given the source and destination format names
     * @param sFormat Name of the source format (data received from the consumer)
     * @param dFormat Name of the destination format (data supplied to the source)
     * @return A pointer to a DataTranslator object or NULL if no known codec exists
     */
    static DataTranslator *create(const String &sFormat, const String &dFormat);

    /**
     * Attach a consumer to a source, possibly trough a chain of translators
     * @param source Source to attach the chain to
     * @param consumer Consumer where the chain ends
     * @return True if successfull, false if no translator chain could be built
     */
    static bool attachChain(DataSource *source, DataConsumer *consumer);

    /**
     * Detach a consumer from a source, possibly trough a chain of translators
     * @param source Source to dettach the chain from
     * @param consumer Consumer where the chain ends
     * @return True if successfull, false if source and consumers were not attached
     */
    static bool detachChain(DataSource *source, DataConsumer *consumer);

protected:
    /**
     * Install a Translator Factory in the list of known codecs
     * @param factory A pointer to a TranslatorFactory instance
     */
    static void install(TranslatorFactory *factory);

    /**
     * Remove a Translator Factory from the list of known codecs
     * @param factory A pointer to a TranslatorFactory instance
     */
    static void uninstall(TranslatorFactory *factory);

private:
    DataTranslator(); // No default constructor please
    DataSource *m_tsource;
    static Mutex s_mutex;
    static ObjList s_factories;
};

/**
 * A factory for constructing data translators by format name
 * conversion of data from one type to another
 * @short An unidirectional data translator (codec)
 */
class TranslatorFactory : public GenObject
{
public:
    TranslatorFactory()
	{ DataTranslator::install(this); }

    virtual ~TranslatorFactory()
	{ DataTranslator::uninstall(this); }

    /**
     * Creates a translator given the source and destination format names
     * @param sFormat Name of the source format (data received from the consumer)
     * @param dFormat Name of the destination format (data supplied to the source)
     * @return A pointer to a DataTranslator object or NULL
     */
    virtual DataTranslator *create(const String &sFormat, const String &dFormat) = 0;

    /**
     * Get the capabilities table of this translator
     * @return A pointer to the first element of the capabilities table
     */
    virtual const TranslatorCaps *getCapabilities() const = 0;
};

}; // namespace TelEngine

#endif /* __TELEPHONY_H */
