/**
 * yatephone.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Drivers, channels and telephony related classes
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

#ifndef __YATEPHONE_H
#define __YATEPHONE_H

#ifndef __cplusplus
#error C++ is required
#endif

#include <yatengine.h>

/**
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

/**
 * A structure to hold information about a static picture or video frame.
 */
struct YATE_API ImageInfo {
    /**
     * Width of the image in pixels
     */
    int width;

    /**
     * Height of the image in pixels
     */
    int height;

    /**
     * Bit depth of the image, 0 for unknown/irrelevant
     */
    int depth;
};

/**
 * A structure to hold information about a data format.
 */
struct YATE_API FormatInfo {
    /**
     * Standard no-blanks lowercase format name
     */
    const char* name;

    /**
     * Format type: "audio", "video", "text"
     */
    const char* type;

    /**
     * Frame size in octets/frame, 0 for non-framed formats
     */
    int frameSize;

    /**
     * Frame time in microseconds, 0 for variable
     */
    int frameTime;

    /**
     * Rate in samples/second (audio) or 1e-6 frames/second (video), 0 for unknown
     */
    int sampleRate;

    /**
     * Number of channels, typically 1
     */
    int numChannels;

    /**
     * If this is a valid candidate for conversion
     */
    bool converter;

    /**
     * Guess the number of samples in an encoded data block
     * @param len Length of the data block in octets
     * @return Number of samples or 0 if unknown
     */
    int guessSamples(int len) const;

    /**
     * Get the data rate in bytes/s
     * @return Data rate or 0 if variable/undefined
     */
    int dataRate() const;

    /**
     * Default constructor - used to initialize arrays
     */
    inline FormatInfo()
	: name(0), type("audio"),
	  frameSize(0), frameTime(0),
	  sampleRate(8000), numChannels(1),
	  converter(false)
	{ }

    /**
     * Normal constructor
     */
    inline explicit FormatInfo(const char* _name, int fsize = 0, int ftime = 10000,
	const char* _type = "audio", int srate = 8000, int nchan = 1, bool convert = false)
	: name(_name), type(_type),
	  frameSize(fsize), frameTime(ftime),
	  sampleRate(srate), numChannels(nchan),
	  converter(convert)
	{ }
};

class DataEndpoint;
class CallEndpoint;
class Driver;

/**
 * A structure to build (mainly static) translator capability tables.
 * A table of such structures must end with an entry with null format names.
 */
struct YATE_API TranslatorCaps {
    /** Description of source (input) data format */
    const FormatInfo* src;
    /** Description of destination (output) data format */
    const FormatInfo* dest;
    /** Computing cost in KIPS of converting a stream from src to dest */
    int cost;
};

/**
 * This is just a holder for the list of media formats supported by Yate
 * @short A repository for media formats
 */
class YATE_API FormatRepository
{
    YNOCOPY(FormatRepository); // no automatic copies please
private:
    FormatRepository();
public:
    /**
     * Retrieve a format by name and type
     * @param name Standard name of the format to find
     * @return Pointer to the format info or NULL if not found
     */
    static const FormatInfo* getFormat(const String& name);

    /**
     * Add a new format to the repository
     * @param name Standard no-blanks lowercase format name
     * @param fsize Data frame size in octets/frame, 0 for non-framed formats
     * @param ftime Data frame duration in microseconds, 0 for variable
     * @param type Format type: "audio", "video", "text"
     * @param srate Rate in samples/second (audio) or 1e-6 frames/second (video), 0 for unknown
     * @param nchan Number of channels, typically 1
     * @return Pointer to the format info or NULL if another incompatible
     *  format with the same name was already registered
    */
    static const FormatInfo* addFormat(const String& name, int fsize, int ftime, const String& type = "audio", int srate = 8000, int nchan = 1);
};

/**
 * An extension of a String that can parse data formats
 * @short A Data format
 */
class YATE_API DataFormat : public NamedList
{
public:
    /**
     * Creates a new, empty format string.
     */
    inline DataFormat()
	: NamedList((const char*)0), m_parsed(0)
	{ }

    /**
     * Creates a new initialized format.
     * @param value Initial value of the format
     */
    inline DataFormat(const char* value)
	: NamedList(value), m_parsed(0)
	{ }

    /**
     * Copy constructor.
     * @param value Initial value of the format
     */
    inline DataFormat(const DataFormat& value)
	: NamedList(value), m_parsed(value.getInfo())
	{ }

    /**
     * Constructor from String reference
     * @param value Initial value of the format
     */
    inline DataFormat(const String& value)
	: NamedList(value), m_parsed(0)
	{ }

    /**
     * Constructor from NamedList reference
     * @param value Initial value of the format and parameters
     */
    inline DataFormat(const NamedList& value)
	: NamedList(value), m_parsed(0)
	{ }

    /**
     * Constructor from String pointer.
     * @param value Initial value of the format
     */
    inline DataFormat(const String* value)
	: NamedList(value ? value->c_str() : (const char*)0), m_parsed(0)
	{ }

    /**
     * Constructor from format information
     * @param format Pointer to existing FormatInfo
     */
    inline explicit DataFormat(const FormatInfo* format)
	: NamedList(format ? format->name : (const char*)0), m_parsed(format)
	{ }

    /**
     * Assignment operator.
     */
    inline DataFormat& operator=(const DataFormat& value)
	{ NamedList::operator=(value); m_parsed = value.getInfo(); return *this; }

    /**
     * Retrieve a pointer to the format information
     * @return Pointer to the associated format info or NULL if error
     */
    const FormatInfo* getInfo() const;

    /**
     * Retrieve the frame size
     * @param defValue Default value to return if format is unknown
     * @return Frame size in octets/frame, 0 for non-framed, defValue if unknown
     */
    inline int frameSize(int defValue = 0) const
	{ return getInfo() ? getInfo()->frameSize : defValue; }

    /**
     * Retrieve the frame time
     * @param defValue Default value to return if format is unknown
     * @return Frame time in microseconds, 0 for variable, defValue if unknown
     */
    inline int frameTime(int defValue = 0) const
	{ return getInfo() ? getInfo()->frameTime : defValue; }

    /**
     * Retrieve the sample rate
     * @param defValue Default value to return if format is unknown
     * @return Rate in samples/second (audio) or 1e-6 frames/second (video),
     *  0 for unknown, defValue if unknown format
     */
    inline int sampleRate(int defValue = 0) const
	{ return getInfo() ? getInfo()->sampleRate : defValue; }

    /**
     * Retrieve the number of channels
     * @param defValue Default value to return if format is unknown
     * @return Number of channels (typically 1), defValue if unknown format
     */
    inline int numChannels(int defValue = 1) const
	{ return getInfo() ? getInfo()->numChannels : defValue; }

protected:
    /**
     * Called whenever the value changed (except in constructors).
     */
    virtual void changed();

private:
    mutable const FormatInfo* m_parsed;
};

/**
 * A generic data handling object
 */
class YATE_API DataNode : public RefObject
{
    friend class DataEndpoint;
    YNOCOPY(DataNode); // no automatic copies please
public:
    /**
     * Flags associated with the DataBlocks forwarded between nodes
     */
    enum DataFlags {
	DataStart   = 0x0001,
	DataEnd     = 0x0002,
	DataMark    = 0x0004,
	DataSilent  = 0x0008,
	DataMissed  = 0x0010,
	DataError   = 0x0020,
	DataPrivate = 0x0100
    };

    /**
     * Construct a DataNode
     * @param format Description of the data format, default none
     */
    inline explicit DataNode(const char* format = 0)
	: m_format(format), m_timestamp(0)
	{ }

    /**
     * Get the computing cost of converting the data to the format asked
     * @param format Name of the format to check for
     * @return -1 if unsupported, 0 for native format else cost in KIPS
     */
    virtual int costFormat(const DataFormat& format)
	{ return -1; }

    /**
     * Change the format used to transfer data
     * @param format Name of the format to set for data
     * @return True if the format changed successfully, false if not changed
     */
    virtual bool setFormat(const DataFormat& format)
	{ return false; }

    /**
     * Get the description of the format currently in use
     * @return Pointer to the data format
     */
    inline const DataFormat& getFormat() const
	{ return m_format; }

    /**
     * Get the current position in the data stream
     * @return Timestamp of current data position
     */
    inline unsigned long timeStamp() const
	{ return m_timestamp; }

    /**
     * Check if this data node is still valid
     * @return True if still valid, false if node should be removed
     */
    virtual bool valid() const
	{ return true; }

    /**
     * Modify node parameters
     * @param params The list of parameters to change
     * @return True if processed
     */
    virtual bool control(NamedList& params)
	{ return false; }

    /**
     * Get the internal representation of an invalid or unknown timestamp
     * @return Invalid timestamp - unsigned long conversion of -1
     */
    inline static unsigned long invalidStamp()
	{ return (unsigned long)-1; }

protected:
    /**
     * Owner attach and detach notification.
     * This method is called with @ref DataEndpoint::commonMutex() held
     * @param added True if a new owner was added, false if it was removed
     */
    virtual void attached(bool added)
	{ }

    DataFormat m_format;
    unsigned long m_timestamp;
};

class DataSource;
class DataTranslator;
class TranslatorFactory;
class ThreadedSourcePrivate;

/**
 * A data consumer
 */
class YATE_API DataConsumer : public DataNode
{
    friend class DataSource;

public:
    /**
     * Consumer constructor
     * @param format Name of the data format, default "slin" (Signed Linear)
     */
    inline explicit DataConsumer(const char* format = "slin")
	: DataNode(format),
	  m_source(0), m_override(0),
	  m_regularTsDelta(0), m_overrideTsDelta(0), m_lastTsTime(0)
	{ }

    /**
     * Destruct notification - complains loudly if still attached to a source
     */
    virtual void destroyed();

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Consumes the data sent to it from a source
     * @param data The raw data block to process
     * @param tStamp Timestamp of data - typically samples
     * @param flags Indicator flags associated with the data block
     * @return Number of samples actually consumed,
     *  use invalidStamp() to indicate that all data was consumed,
     *  return zero for consumers that become invalid
     */
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags) = 0;

    /**
     * Get the data source of this object if it's connected
     * @return A pointer to the DataSource object or NULL
     */
    inline DataSource* getConnSource() const
	{ return m_source; }

    /**
     * Get the override data source of this object if it's connected
     * @return A pointer to the DataSource object or NULL
     */
    inline DataSource* getOverSource() const
	{ return m_override; }

    /**
     * Get the data source of a translator object
     * @return A pointer to the DataSource object or NULL
     */
    virtual DataSource* getTransSource() const
	{ return 0; }

protected:
    /**
     * Synchronize the consumer with a source
     * @param source Data source to copy the timestamp from
     * @return True if we could synchronize with the source
     */
    virtual bool synchronize(DataSource* source);

private:
    unsigned long Consume(const DataBlock& data, unsigned long tStamp,
	unsigned long flags, DataSource* source);
    DataSource* m_source;
    DataSource* m_override;
    long m_regularTsDelta;
    long m_overrideTsDelta;
    u_int64_t m_lastTsTime;
};

/**
 * A data source
 */
class YATE_API DataSource : public DataNode, public Mutex
{
    friend class DataTranslator;
    YNOCOPY(DataSource); // no automatic copies please
public:
    /**
     * Source constructor
     * @param format Name of the data format, default "slin" (Signed Linear)
     */
    inline explicit DataSource(const char* format = "slin")
	: DataNode(format), Mutex(false,"DataSource"),
	  m_nextStamp(invalidStamp()), m_translator(0) { }

    /**
     * Source's destruct notification - detaches all consumers
     */
    virtual void destroyed();

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Check if this data source is still valid
     * @return True if still valid, false if node should be removed
     */
    virtual bool valid() const;

    /**
     * Forwards the data to its consumers
     * @param data The raw data block to forward
     * @param tStamp Timestamp of data - typically samples
     * @param flags Indicator flags associated with the data block
     * @return Number of samples actually forwarded to all consumers
     */
    unsigned long Forward(const DataBlock& data, unsigned long tStamp = invalidStamp(),
	unsigned long flags = 0);

    /**
     * Attach a data consumer
     * @param consumer Data consumer to attach
     * @param override Attach as temporary source override
     * @return True on success, false on failure
     */
    bool attach(DataConsumer* consumer, bool override = false);

    /**
     * Detach a data consumer
     * @param consumer Data consumer to detach
     * @return True on success, false on failure
     */
    bool detach(DataConsumer* consumer);

    /**
     * Detach all data consumers
     */
    void clear();

    /**
     * Get the master translator object if this source is part of a translator
     * @return A pointer to the DataTranslator object or NULL
     */
    inline DataTranslator* getTranslator() const
	{ return m_translator; }

    /**
     * Synchronize the source and attached consumers with another timestamp
     * @param tStamp New timestamp of data - typically samples
     */
    void synchronize(unsigned long tStamp);

    /**
     * Get the next expected position in the data stream
     * @return Timestamp of next expected data position, may be invalid/unknown
     */
    inline unsigned long nextStamp() const
	{ return m_nextStamp; }

protected:
    unsigned long m_nextStamp;
    ObjList m_consumers;
private:
    inline void setTranslator(DataTranslator* translator) {
	    Lock mylock(this);
	    m_translator = translator;
	}
    bool detachInternal(DataConsumer* consumer);
    DataTranslator* m_translator;
};

/**
 * A data source with a thread of its own
 * @short Data source with own thread
 */
class YATE_API ThreadedSource : public DataSource
{
    friend class ThreadedSourcePrivate;
public:
    /**
     * The destruction notification, checks that the thread is gone
     */
    virtual void destroyed();

    /**
     * Starts the worker thread
     * @param name Static name of this thread
     * @param prio Thread's priority
     * @return True if started, false if an error occured
     */
    bool start(const char* name = "ThreadedSource", Thread::Priority prio = Thread::Normal);

    /**
     * Stops and destroys the worker thread if running
     */
    void stop();

    /**
     * Return a pointer to the worker thread
     * @return Pointer to running worker thread or NULL
     */
    Thread* thread() const;

    /**
     * Check if the data thread is running
     * @return True if the data thread was started and is running
     */
    bool running() const;

protected:
    /**
     * Threaded Source constructor
     * @param format Name of the data format, default "slin" (Signed Linear)
     */
    inline explicit ThreadedSource(const char* format = "slin")
	: DataSource(format), m_thread(0)
	{ }

    /**
     * The worker method. You have to reimplement it as you need
     */
    virtual void run() = 0;

    /**
     * The cleanup after thread method, deletes the source if already
     *  dereferenced and set for asynchronous deletion
     */
    virtual void cleanup();

    /**
     * Check if the calling thread should keep looping the worker method
     * @param runConsumers True to keep running as long consumers are attached
     * @return True if the calling thread should remain in the run() method
     */
    bool looping(bool runConsumers = false) const;

private:
    ThreadedSourcePrivate* m_thread;
};

/**
 * The DataTranslator holds a translator (codec) capable of unidirectional
 * conversion of data from one type to another.
 * @short An unidirectional data translator (codec)
 */
class YATE_API DataTranslator : public DataConsumer
{
    friend class TranslatorFactory;
public:
    /**
     * Construct a data translator.
     * @param sFormat Name of the source format (data received from the consumer)
     * @param dFormat Name of the destination format (data supplied to the source)
     */
    DataTranslator(const char* sFormat, const char* dFormat);

    /**
     * Creates a data translator from an existing source,
     *  does not increment the source's reference counter.
     * @param sFormat Name of the source format (data received from the consumer)
     * @param source Optional pointer to a DataSource object
     */
    explicit DataTranslator(const char* sFormat, DataSource* source = 0);

    /**
     * Destroys the translator and its source
     */
    ~DataTranslator();

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Check if the data translator has a valid source
     * @return True if still valid, false if node should be removed
     */
    virtual bool valid() const
	{ return m_tsource && m_tsource->valid(); }

    /**
     * Get the data source of a translator object
     * @return A pointer to the DataSource object or NULL
     */
    virtual DataSource* getTransSource() const
	{ return m_tsource; }

    /**
     * Get the first translator from a chain
     * @return Pointer to the first translator in a chain
     */
    DataTranslator* getFirstTranslator();

    /**
     * Constant version to get the first translator from a chain
     * @return Pointer to the first translator in a chain
     */
    const DataTranslator* getFirstTranslator() const;

    /**
     * Get a list of formats supported for a given output format.
     * @param dFormat Name of destination format
     * @param maxCost Maximum cost of candidates to consider, -1 to accept all
     * @param maxLen Maximum length of codec chains to consider, 0 to accept all
     * @param lst Initial list, will append to it if not empty
     * @return List of source format names, must be freed by the caller
     */
    static ObjList* srcFormats(const DataFormat& dFormat = "slin", int maxCost = -1, unsigned int maxLen = 0, ObjList* lst = 0);

    /**
     * Get a list of formats supported for a given input format
     * @param sFormat Name of source format
     * @param maxCost Maximum cost of candidates to consider, -1 to accept all
     * @param maxLen Maximum length of codec chains to consider, 0 to accept all
     * @param lst Initial list, will append to it if not empty
     * @return List of destination format names, must be freed by the caller
     */
    static ObjList* destFormats(const DataFormat& sFormat = "slin", int maxCost = -1, unsigned int maxLen = 0, ObjList* lst = 0);

    /**
     * Get a list of formats supported by transcoding for a given format list
     * @param formats List of data format names
     * @param existing Also return formats already existing in the initial list
     * @param sameRate Only return formats with same sampling rate
     * @param sameChans Only return formats with same number of channels
     * @return List of format names, must be freed by the caller
     */
    static ObjList* allFormats(const ObjList* formats, bool existing = true, bool sameRate = true, bool sameChans = true);

    /**
     * Get a list of formats supported by transcoding for a given format list
     * @param formats Data format names as comma separated list
     * @param existing Also return formats already existing in the initial list
     * @param sameRate Only return formats with same sampling rate
     * @param sameChans Only return formats with same number of channels
     * @return List of format names, must be freed by the caller
     */
    static ObjList* allFormats(const String& formats, bool existing = true, bool sameRate = true, bool sameChans = true);

    /**
     * Check if bidirectional conversion can be performed by installed translators
     * @param fmt1 Name of the first data format
     * @param fmt2 Name of the second data format
     * @return True if translators can be created for both directions
     */
    static bool canConvert(const DataFormat& fmt1, const DataFormat& fmt2 = "slin");

    /**
     * Finds the cost of a translator given the source and destination format names
     * @param sFormat Name of the source format (data received from the consumer)
     * @param dFormat Name of the destination format (data supplied to the source)
     * @return Cost of best (cheapest) codec or -1 if no known codec exists
     */
    static int cost(const DataFormat& sFormat, const DataFormat& dFormat);

    /**
     * Creates a translator given the source and destination format names
     * @param sFormat Name of the source format (data received from the consumer)
     * @param dFormat Name of the destination format (data supplied to the source)
     * @return A pointer to a DataTranslator object or NULL if no known codec exists
     */
    static DataTranslator* create(const DataFormat& sFormat, const DataFormat& dFormat);

    /**
     * Attach a consumer to a source, possibly trough a chain of translators
     * @param source Source to attach the chain to
     * @param consumer Consumer where the chain ends
     * @param override Attach chain for temporary source override
     * @return True if successfull, false if no translator chain could be built
     */
    static bool attachChain(DataSource* source, DataConsumer* consumer, bool override = false);

    /**
     * Detach a consumer from a source, possibly trough a chain of translators
     * @param source Source to dettach the chain from
     * @param consumer Consumer where the chain ends
     * @return True if successfull, false if source and consumers were not attached
     */
    static bool detachChain(DataSource* source, DataConsumer* consumer);

    /**
     * Set the length of the longest translator chain we are allowed to create
     * @param maxChain Desired longest chain length
     */
    static void setMaxChain(unsigned int maxChain);

protected:
    /**
     * Synchronize the consumer with a source
     * @param source Data source to copy the timestamp from
     * @return True if we could synchronize with the source
     */
    virtual bool synchronize(DataSource* source);

    /**
     * Install a Translator Factory in the list of known codecs
     * @param factory A pointer to a TranslatorFactory instance
     */
    static void install(TranslatorFactory* factory);

    /**
     * Remove a Translator Factory from the list of known codecs
     * @param factory A pointer to a TranslatorFactory instance
     */
    static void uninstall(TranslatorFactory* factory);

private:
    DataTranslator(); // No default constructor please
    static void compose();
    static void compose(TranslatorFactory* factory);
    static bool canConvert(const FormatInfo* fmt1, const FormatInfo* fmt2);
    DataSource* m_tsource;
    static Mutex s_mutex;
    static ObjList s_factories;
    static unsigned int s_maxChain;
};

/**
 * A factory for constructing data translators by format name
 * conversion of data from one type to another
 * @short An unidirectional data translator (codec)
 */
class YATE_API TranslatorFactory : public GenObject
{
    YNOCOPY(TranslatorFactory); // no automatic copies please
protected:
    /**
     * Constructor - registers the factory in the global list
     * @param name Static name of the factory, used for debugging
     */
    inline explicit TranslatorFactory(const char* name = 0)
	: m_name(name ? name : "?")
	{ m_counter = Thread::getCurrentObjCounter(true); DataTranslator::install(this); }

public:
    /**
     * Destructor - unregisters from the global list
     */
    virtual ~TranslatorFactory();

    /**
     * Notification that another factory was removed from the list
     * @param factory Pointer to the factory that just got removed
     */
    virtual void removed(const TranslatorFactory* factory);

    /**
     * Creates a translator given the source and destination format names
     * @param sFormat Name of the source format (data received from the consumer)
     * @param dFormat Name of the destination format (data supplied to the source)
     * @return A pointer to the end of a DataTranslator chain or NULL
     */
    virtual DataTranslator* create(const DataFormat& sFormat, const DataFormat& dFormat) = 0;

    /**
     * Get the capabilities table of this translator
     * @return A pointer to the first element of the capabilities table
     */
    virtual const TranslatorCaps* getCapabilities() const = 0;

    /**
     * Check if this factory can build a translator for given data formats
     * @param sFormat Name of the source format
     * @param dFormat Name of the destination format
     * @return True if a conversion between formats is possible
     */
    virtual bool converts(const DataFormat& sFormat, const DataFormat& dFormat) const;

    /**
     * Get the length of the translator chain built by this factory
     * @return How many translators will build the factory
     */
    virtual unsigned int length() const;

    /**
     * Check if a data format is used as intermediate in a translator chain
     * @param info Format to check for
     * @return True if the format is used internally as intermediate
     */
    virtual bool intermediate(const FormatInfo* info) const;

    /**
     * Get the intermediate format used by a translator chain
     * @return Pointer to intermediate format or NULL
     */
    virtual const FormatInfo* intermediate() const;

    /**
     * Get the name of this factory, useful for debugging purposes
     * @return Name of the factory as specified in the constructor
     */
    virtual const char* name() const
	{ return m_name; }

    /**
     * Retrive the objects counter associated to this factory
     * @return Pointer to factory's objects counter or NULL
     */
    inline NamedCounter* objectsCounter() const
	{ return m_counter; }

private:
    const char* m_name;
    NamedCounter* m_counter;
};

/**
 * The DataEndpoint holds an endpoint capable of performing unidirectional
 * or bidirectional data transfers
 * @short A data transfer endpoint capable of sending and/or receiving data
 */
class YATE_API DataEndpoint : public RefObject
{
    YNOCOPY(DataEndpoint); // no automatic copies please
public:
    /**
     * Creates an empty data endpoint
     */
    explicit DataEndpoint(CallEndpoint* call = 0, const char* name = "audio");

    /**
     * Endpoint destruct notification, clears source and consumer
     */
    virtual void destroyed();

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Get a string identification of the endpoint
     * @return A reference to this endpoint's name
     */
    virtual const String& toString() const;

    /**
     * Get the mutex that serializes access to this data endpoint, if any
     * @return Pointer to the call's mutex object or NULL
     */
    Mutex* mutex() const;

    /**
     * Get the big mutex that serializes access to all data endpoints
     * @return A reference to the mutex
     */
    static Mutex& commonMutex();

    /**
     * Connect the source and consumer of the endpoint to a peer
     * @param peer Pointer to the peer data endpoint
     * @return True if connected, false if incompatible source/consumer
     */
    bool connect(DataEndpoint* peer);

    /**
     * Disconnect from the connected endpoint
     * @return True if the object was deleted, false if it still exists
     */
    bool disconnect();

    /**
     * Set the data source of this object
     * @param source A pointer to the new source or NULL
     */
    void setSource(DataSource* source = 0);

    /**
     * Get the data source of this object
     * @return A pointer to the DataSource object or NULL
     */
    inline DataSource* getSource() const
	{ return m_source; }

    /**
     * Set the data consumer of this object
     * @param consumer A pointer to the new consumer or NULL
     */
    void setConsumer(DataConsumer* consumer = 0);

    /**
     * Get the data consumer of this object
     * @return A pointer to the DataConsumer object or NULL
     */
    inline DataConsumer* getConsumer() const
	{ return m_consumer; }

    /**
     * Set the data consumer for recording peer generated data.
     * This will be connected to the peer data source.
     * @param consumer A pointer to the new consumer or NULL
     */
    void setPeerRecord(DataConsumer* consumer = 0);

    /**
     * Get the data consumer used for recording peer generated data.
     * @return A pointer to the DataConsumer object or NULL
     */
    inline DataConsumer* getPeerRecord() const
	{ return m_peerRecord; }

    /**
     * Set the data consumer for recording local call generated data
     * This will be connected to the local data source.
     * @param consumer A pointer to the new consumer or NULL
     */
    void setCallRecord(DataConsumer* consumer = 0);

    /**
     * Get the data consumer used for recording local call generated data.
     * @return A pointer to the DataConsumer object or NULL
     */
    inline DataConsumer* getCallRecord() const
	{ return m_callRecord; }

    /**
     * Clear a data node from any slot of this object
     * @param node Pointer to DataSource or DataConsumer to clear
     * @return True if the node was removed from at least one slot
     */
    bool clearData(DataNode* node);

    /**
     * Adds a data consumer to the list of sniffers of the local call data
     * @param sniffer Pointer to the DataConsumer to add to sniffer list
     * @return True if the sniffer was added to list, false if NULL or already added
     */
    bool addSniffer(DataConsumer* sniffer);

    /**
     * Remove a data consumer from the list of sniffers of the local call data
     * @param sniffer Pointer to the DataConsumer to remove from sniffer list
     * @return True if the sniffer was removed from list
     */
    bool delSniffer(DataConsumer* sniffer);

    /**
     * Find a sniffer by name
     * @param name Name of the sniffer to find
     * @return Pointer to DataConsumer or NULL if not found
     */
    inline DataConsumer* getSniffer(const String& name)
	{ return static_cast<DataConsumer*>(m_sniffers[name]); }

    /**
     * Removes all sniffers from the list and dereferences them
     */
    void clearSniffers();

    /**
     * Get a pointer to the peer endpoint
     * @return A pointer to the peer endpoint or NULL
     */
    inline DataEndpoint* getPeer() const
	{ return m_peer; }

    /**
     * Get a pointer to the owner call
     * @return A pointer to the owner call or NULL
     */
    inline CallEndpoint* getCall() const
	{ return m_call; }

    /**
     * Get the name set in constructor
     * @return A reference to the name as hashed string
     */
    inline const String& name() const
	{ return m_name; }

    /**
     * Clear the owner call endpoint.
     * Works only if the caller provides the correct owner pointer
     * @param call Pointer to the call endpoint that is to be cleared
     */
    inline void clearCall(const CallEndpoint* call)
	{ if (call == m_call) m_call = 0; }

    /**
     * Modify data parameters
     * @param params The list of parameters to change
     * @return True if processed
     */
    virtual bool control(NamedList& params);

protected:
    /**
     * Attempt to connect the endpoint to a peer of the same type
     * @param peer Pointer to the endpoint data driver
     * @return True if connected, false if failed native connection
     */
    virtual bool nativeConnect(DataEndpoint* peer)
	{ return false; }

private:
    String m_name;
    DataSource* m_source;
    DataConsumer* m_consumer;
    DataEndpoint* m_peer;
    CallEndpoint* m_call;
    DataConsumer* m_peerRecord;
    DataConsumer* m_callRecord;
    ObjList m_sniffers;
};

/**
 * A class that holds common call control and data related features
 * @short An abstract call endpoint
 */
class YATE_API CallEndpoint : public RefObject
{
    friend class DataEndpoint;
    YNOCOPY(CallEndpoint); // no automatic copies please
private:
    CallEndpoint* m_peer;
    const void* m_lastPeer;
    String m_id;
    String m_lastPeerId;

protected:
    ObjList m_data;
    Mutex* m_mutex;

public:
    /**
     * Destruct notification, performs cleanups
     */
    virtual void destroyed();

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Get a string representation of this channel
     * @return A reference to the name of this object
     */
    virtual const String& toString() const
	{ return m_id; }

    /**
     * Get the unique channel identifier
     * @return A String holding the unique channel id
     */
    inline const String& id() const
	{ return m_id; }

    /**
     * Get the connected peer call
     * @return Pointer to connected peer call or NULL
     */
    inline CallEndpoint* getPeer() const
	{ return m_peer; }

    /**
     * Get the connected peer call id in a caller supplied String
     * @param id String to fill in
     * @return True if the call endpoint has a peer
     */
    bool getPeerId(String& id) const;

    /**
     * Get the connected peer call id
     * @return Connected peer call id or empty string
     */
    String getPeerId() const;

    /**
     * Get the last connected peer call id in a caller supplied String
     * @param id String to fill in
     * @return True if the call endpoint ever had a peer
     */
    bool getLastPeerId(String& id) const;

    /**
     * Copy the current peer ID as the last connected peer ID, does nothing if not connected
     */
    void setLastPeerId();

    /**
     * Get the mutex that serializes access to this call endpoint, if any
     * @return Pointer to the call's mutex object or NULL
     */
    inline Mutex* mutex() const
	{ return m_mutex; }

    /**
     * Get the big mutex that serializes access to all call endpoints
     * @return A reference to the mutex
     */
    static Mutex& commonMutex();

    /**
     * Connect the call endpoint to a peer.
     * @param peer Pointer to the peer call endpoint.
     * @param reason Text that describes connect reason.
     * @param notify Call disconnected() notification method on old peer
     * @return True if connected, false if an error occured.
     */
    bool connect(CallEndpoint* peer, const char* reason = 0, bool notify = true);

    /**
     * Disconnect from the connected peer call endpoint.
     * @param reason Text that describes disconnect reason.
     * @param notify Call disconnected() notification method on old peer
     * @param params Optional pointer to extra parameters for disconnect cause
     * @return True if the object was deleted, false if it still exists
     */
    inline bool disconnect(const char* reason = 0, bool notify = true, const NamedList* params = 0)
	{ return disconnect(false,reason,notify,params); }

    /**
     * Disconnect from the connected peer call endpoint and notify old peer.
     * @param reason Text that describes disconnect reason.
     * @param params Extra parameters for disconnect cause
     * @return True if the object was deleted, false if it still exists
     */
    inline bool disconnect(const char* reason, const NamedList& params)
	{ return disconnect(false,reason,true,&params); }

    /**
     * Get a data endpoint of this object
     * @param type Type of data endpoint: "audio", "video", "text"
     * @return A pointer to the DataEndpoint object or NULL if not found
     */
    DataEndpoint* getEndpoint(const String& type = CallEndpoint::audioType()) const;

    /**
     * Get a data endpoint of this object, create if required
     * @param type Type of data endpoint: "audio", "video", "text"
     * @return A pointer to the DataEndpoint object or NULL if an error occured
     */
    DataEndpoint* setEndpoint(const String& type = CallEndpoint::audioType());

    /**
     * Clear one or all data endpoints of this object
     * @param type Type of data endpoint: "audio", "video", "text", NULL to clear all
     */
    void clearEndpoint(const String& type = String::empty());

    /**
     * Set a data source of this object
     * @param source A pointer to the new source or NULL
     * @param type Type of data node: "audio", "video", "text"
     */
    void setSource(DataSource* source = 0, const String& type = CallEndpoint::audioType());

    /**
     * Get a data source of this object
     * @param type Type of data node: "audio", "video", "text"
     * @return A pointer to the DataSource object or NULL
     */
    DataSource* getSource(const String& type = CallEndpoint::audioType()) const;

    /**
     * Set the data consumer of this object
     * @param consumer A pointer to the new consumer or NULL
     * @param type Type of data node: "audio", "video", "text"
     */
    void setConsumer(DataConsumer* consumer = 0, const String& type = CallEndpoint::audioType());

    /**
     * Get the data consumer of this object
     * @param type Type of data node: "audio", "video", "text"
     * @return A pointer to the DataConsumer object or NULL
     */
    DataConsumer* getConsumer(const String& type = CallEndpoint::audioType()) const;

    /**
     * Clear a data node from any slot of a DataEndpoint of this object
     * @param node Pointer to DataSource or DataConsumer to clear
     * @param type Type of data node: "audio", "video", "text"
     * @return True if the node was removed from at least one slot
     */
    bool clearData(DataNode* node, const String& type = CallEndpoint::audioType());

    /**
     * Return the defaul audio type "audio"
     * @return Return a string naming the "audio" type
     */
    static const String& audioType();

protected:
    /**
     * Constructor
     */
    CallEndpoint(const char* id = 0);

    /**
     * Connect notification method.
     * @param reason Text that describes connect reason.
     */
    virtual void connected(const char* reason) { }

    /**
     * Disconnect notification method.
     * @param final True if this disconnect was called from the destructor.
     * @param reason Text that describes disconnect reason.
     */
    virtual void disconnected(bool final, const char* reason) { }

    /**
     * Set disconnect parameters
     * @param params Pointer to disconnect cause parameters, NULL to reset them
     */
    virtual void setDisconnect(const NamedList* params) { }

    /**
     * Set the peer call endpoint pointer.
     * @param peer A pointer to the new peer or NULL.
     * @param reason Text describing the reason in case of disconnect.
     * @param notify Call notification methods - connected() or disconnected()
     * @param params Optional pointer to extra parameters for disconnect cause
     */
    void setPeer(CallEndpoint* peer, const char* reason = 0, bool notify = true, const NamedList* params = 0);

    /**
     * Set a foreign data endpoint in this object
     * @param endPoint Data endpoint to set, will replace one with same type
     */
    void setEndpoint(DataEndpoint* endPoint);

    /**
     * Set a new ID for this call endpoint
     * @param newId New ID to set to this call
     */
    virtual void setId(const char* newId);

private:
    bool disconnect(bool final, const char* reason, bool notify, const NamedList* params);
};

/**
 * Module is a descendent of Plugin specialized in implementing modules
 * @short A Plugin that implements a module
 */
class YATE_API Module : public Plugin, public Mutex, public MessageReceiver
{
    YNOCOPY(Module); // no automatic copies please
private:
    bool m_init;
    int m_relays;
    String m_type;
    Regexp m_filter;
    u_int64_t m_changed;
    static unsigned int s_delay;

public:
    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Retrieve the type of the module
     * @return The module's type as String
     */
    inline const String& type() const
	{ return m_type; }

    /**
     * Mark the driver statistics "dirty" therefore triggring a delayed
     *  status update.
     */
    void changed();

    /**
     * Retrieve the global update notification delay
     * @return Update delay value in seconds
     */
    inline static unsigned int updateDelay()
	{ return s_delay; }

    /**
     * Set the global update notification delay
     * @param delay New update delay value in seconds, 0 to disable
     */
    inline static void updateDelay(unsigned int delay)
	{ s_delay = delay; }

    /**
     * Check if a debug filter is installed
     * @return True if debugging should be filtered
     */
    inline bool filterInstalled() const
	{ return !m_filter.null(); }

    /**
     * Check by filter rule if debugging should be active
     * @param item Value of the item to match
     * @return True if debugging should be activated
     */
    bool filterDebug(const String& item) const;

    /**
     * Helper function to complete just one item on a command line
     * @param itemList Tab separated list of possible values to complete
     * @param item Item to possibly insert in the list
     * @param partWord Partial word to complete, may be empty
     * @return True if the item was added to list, false if it didn't match
     */
    static bool itemComplete(String& itemList, const String& item, const String& partWord);

protected:
    /**
     * IDs of the installed relays
     */
    enum {
	// Module messages
	Status     = 0x00000001,
	Timer      = 0x00000002,
	Level      = 0x00000004,
	Command    = 0x00000008,
	Help       = 0x00000010,
	Halt       = 0x00000020,
	Route	   = 0x00000040,
	Stop	   = 0x00000080,
	// Driver messages
	Execute    = 0x00000100,
	Drop       = 0x00000200,
	// Channel messages
	Locate     = 0x00000400,
	Masquerade = 0x00000800,
	Ringing    = 0x00001000,
	Answered   = 0x00002000,
	Tone       = 0x00004000,
	Text       = 0x00008000,
	Progress   = 0x00010000,
	Update     = 0x00020000,
	Transfer   = 0x00040000,
	Control	   = 0x00080000,
	// Instant messaging related
	MsgExecute = 0x00100000,
	// Last possible public ID
	PubLast    = 0x0fffffff,
	// Private messages base ID
	Private    = 0x10000000
    } RelayID;

    /**
     * Find the name of a specific Relay ID
     * @param id RelayID of the message
     * @return Pointer to name of the message or NULL if not found
     */
    static const char* messageName(int id);

    /**
     * Find the ID or a specific Relay name
     * @param name Name of the Relay to search for
     * @return ID of the Relay, zero if not found
     */
    static inline int relayId(const char* name)
	{ return lookup(name,s_messages); }

    /**
     * Constructor
     * @param name Plugin name of this driver
     * @param type Type of the driver: "misc", "route", etc.
     * @param earlyInit True to attempt to initialize module before others
     */
    Module(const char* name, const char* type = 0, bool earlyInit = false);

    /**
     * Destructor
     */
    virtual ~Module();

    /**
     * This method is called to initialize the loaded module
     */
    virtual void initialize();

    /**
     * Install standard message relays
     */
    void setup();

    /**
     * Check if a specific relay ID is installed
     * @param id RelayID to test for
     * @return True if such a relay is installed
     */
    inline bool relayInstalled(int id) const
	{ return (id & m_relays) != 0; }

    /**
     * Install a standard message relay
     * @param id RelayID of the new relay to create
     * @param priority Priority of the handler, 0 = top
     * @return True if installed or already was one installed
     */
    bool installRelay(int id, unsigned priority = 100);

    /**
     * Install a standard message relay
     * @param name Name of the relay to create, must match a RelayID
     * @param priority Priority of the handler, 0 = top
     * @return True if installed or already was one installed
     */
    bool installRelay(const char* name, unsigned priority = 100);

    /**
     * Install a custom message relay
     * @param id RelayID of the new relay to create
     * @param name Name of the custom relay to create
     * @param priority Priority of the handler, 0 = top
     * @return True if installed or already was one installed
     */
    bool installRelay(int id, const char* name, unsigned priority = 100);

    /**
     * Install a custom message relay
     * @param relay Custom message relay
     * @return True if installed, false if there was already one with same ID
     */
    bool installRelay(MessageRelay* relay);

    /**
     * Uninstall a message relay
     * @param relay Pointer to message relay
     * @param delRelay True to delete the relay after removing it
     * @return True if uninstalled, false if if was not present
     */
    bool uninstallRelay(MessageRelay* relay, bool delRelay = true);

    /**
     * Uninstall a message relay
     * @param id RelayID to uninstall, relay will be deleted
     * @param delRelay True to delete the relay after removing it
     * @return True if uninstalled, false if if was not present
     */
    bool uninstallRelay(int id, bool delRelay = true);

    /**
     * Uninstall all installed relays in preparation for unloading
     * @return True if all relays were uninstalled, false if something wrong
     */
    bool uninstallRelays();

    /**
     * Message receiver handler
     * @param msg The received message
     * @param id The identifier with which the relay was created
     * @return True to stop processing, false to try other handlers
     */
    virtual bool received(Message &msg, int id);

    /**
     * Opportunity to modify the update message
     * @param msg Status update message
     */
    virtual void genUpdate(Message& msg);

    /**
     * Timer message handler.
     * @param msg Time message
     */
    virtual void msgTimer(Message& msg);

    /**
     * Status message handler that is invoked only for matching messages.
     * @param msg Status message
     */
    virtual void msgStatus(Message& msg);

    /**
     * Routing message handler that is invoked for all call.route messages.
     * @param msg Call routing message
     * @return True to stop processing the message, false to try other handlers
     */
    virtual bool msgRoute(Message& msg);

    /**
     * Handler for special commands and line completion requests.
     * By default it calls @ref commandExecute() or @ref commandComplete().
     * @param msg Command message
     * @return True to stop processing the message, false to try other handlers
     */
    virtual bool msgCommand(Message& msg);

    /**
     * Build the module identification part of the status answer
     * @param str String variable to fill up
     */
    virtual void statusModule(String& str);

    /**
     * Build the parameter reporting part of the status answer
     * @param str String variable to fill up
     */
    virtual void statusParams(String& str);

    /**
     * Build the details reporting part of the status answer
     * @param str String variable to fill up
     */
    virtual void statusDetail(String& str);

    /**
     * Execute a specific command
     * @param retVal String to append the textual command output to
     * @param line Command line to attempt to execute
     * @return True to stop processing the message, false to try other handlers
     */
    virtual bool commandExecute(String& retVal, const String& line);

    /**
     * Complete a command line
     * @param msg Message to return completion into
     * @param partLine Partial line to complete, excluding the last word
     * @param partWord Partial word to complete
     * @return True to stop processing the message, false to try other handlers
     */
    virtual bool commandComplete(Message& msg, const String& partLine, const String& partWord);

    /**
     * Set the local debugging level
     * @param msg Debug setting message
     * @param target String to match for local settings
     */
    virtual bool setDebug(Message& msg, const String& target);

private:
    Module(); // no default constructor please
    static TokenDict s_messages[];
    ObjList m_relayList;
};

/**
 * A class that holds common channel related features (a.k.a. call leg)
 * @short An abstract communication channel
 */
class YATE_API Channel : public CallEndpoint, public DebugEnabler, public MessageNotifier
{
    friend class Driver;
    friend class Router;
    YNOCOPY(Channel); // no automatic copies please
private:
    NamedList m_parameters;
    Driver* m_driver;
    bool m_outgoing;
    u_int64_t m_timeout;
    u_int64_t m_maxcall;
    u_int64_t m_maxPDD;          // Timeout while waiting for some progress on outgoing calls
    u_int64_t m_dtmfTime;
    unsigned int m_toutAns;
    unsigned int m_dtmfSeq;
    String m_dtmfText;
    String m_dtmfDetected;

protected:
    String m_status;
    String m_address;
    String m_targetid;
    String m_billid;
    bool m_answered;

public:
    /**
     * Destructor
     */
    virtual ~Channel();

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Get the big mutex that serializes access to all disconnect parameter lists
     * @return A reference to the mutex
     */
    static Mutex& paramMutex();

    /**
     * Put channel variables into a message
     * @param msg Message to fill in
     * @param minimal True to fill in only a minimum of parameters
     */
    virtual void complete(Message& msg, bool minimal = false) const;

    /**
     * Create a filled notification message
     * @param name Name of the message to create
     * @param minimal Set to true to fill in only a minimum of parameters
     * @param data Set the channel as message data
     * @return A new allocated and parameter filled message
     */
    Message* message(const char* name, bool minimal = false, bool data = false);

    /**
     * Create a filled notification message, copy some parameters from another message
     * @param name Name of the message to create
     * @param original Parameters to copy from, can be NULL
     * @param params Comma separated list of parameters to copy,
     *  if NULL will be taken from the "copyparams" parameter of original
     * @param minimal Set to true to fill in only a minimum of parameters
     * @param data Set the channel as message data
     * @return A new allocated and parameter filled message
     */
    Message* message(const char* name, const NamedList* original, const char* params = 0, bool minimal = false, bool data = false);

    /**
     * Create a filled notification message, copy some parameters from another message
     * @param name Name of the message to create
     * @param original Parameters to copy from
     * @param params Comma separated list of parameters to copy,
     *  if NULL will be taken from the "copyparams" parameter of original
     * @param minimal Set to true to fill in only a minimum of parameters
     * @param data Set the channel as message data
     * @return A new allocated and parameter filled message
     */
    inline Message* message(const char* name, const NamedList& original, const char* params = 0, bool minimal = false, bool data = false)
	{ return message(name,&original,params,minimal,data); }

    /**
     * Notification on remote call making some progress, not enabled by default
     * @param msg Notification message
     * @return True to stop processing the message, false to let it flow
     */
    virtual bool msgProgress(Message& msg);

    /**
     * Notification on remote ringing
     * @param msg Notification message
     * @return True to stop processing the message, false to let it flow
     */
    virtual bool msgRinging(Message& msg);

    /**
     * Notification on remote answered. Note that the answered flag will be set
     * @param msg Notification message
     * @return True to stop processing the message, false to let it flow
     */
    virtual bool msgAnswered(Message& msg);

    /**
     * Notification on remote tone(s)
     * @param msg Notification message
     * @param tone Pointer to the received tone(s)
     * @return True to stop processing the message, false to let it flow
     */
    virtual bool msgTone(Message& msg, const char* tone);

    /**
     * Notification on remote text messaging (sms)
     * @param msg Notification message
     * @param text Pointer to the received text
     * @return True to stop processing the message, false to let it flow
     */
    virtual bool msgText(Message& msg, const char* text);

    /**
     * Notification on current call drop request
     * @param msg Notification message
     * @param reason Pointer to drop reason text or NULL if none provided
     * @return True if initiated call drop, false if failed
     */
    virtual bool msgDrop(Message& msg, const char* reason);

    /**
     * Notification on native transfer request
     * @param msg Notification message
     * @return True to stop processing the message, false to let it flow
     */
    virtual bool msgTransfer(Message& msg);

    /**
     * Notification on call parameters update request
     * @param msg Notification message
     * @return True to stop processing the message, false to let it flow
     */
    virtual bool msgUpdate(Message& msg);

    /**
     * Notification on message masquerade as channel request
     * @param msg Message already modified to masquerade as this channel
     * @return True to stop processing the message, false to masquerade it
     */
    virtual bool msgMasquerade(Message& msg);

    /**
     * Status message handler that is invoked only for messages to this channel
     * @param msg Status message
     */
    virtual void msgStatus(Message& msg);

    /**
     * Control message handler that is invoked only for messages to this channel
     * @param msg Control message
     * @return True to stop processing the message, false to let it flow
     */
    virtual bool msgControl(Message& msg);

    /**
     * Timer check method, by default handles channel timeouts
     * @param msg Timer message
     * @param tmr Current time against which timers are compared
     */
    virtual void checkTimers(Message& msg, const Time& tmr);

    /**
     * Notification on progress of prerouting incoming call
     * @param msg Notification call.preroute message just after being dispatched
     * @param handled True if a handler claimed having handled prerouting
     * @return True to continue with the call, false to abort the route
     */
    virtual bool callPrerouted(Message& msg, bool handled);

    /**
     * Notification on progress of routing incoming call
     * @param msg Notification call.route message just after being dispatched
     * @return True to continue with the call, false to abort the route
     */
    virtual bool callRouted(Message& msg);

    /**
     * Notification on success of incoming call
     * @param msg Notification call.execute message just after being dispatched
     */
    virtual void callAccept(Message& msg);

    /**
     * Notification on failure of incoming call
     * @param error Standard error keyword
     * @param reason Textual failure reason
     * @param msg Pointer to message causing the rejection, if any
     */
    virtual void callRejected(const char* error, const char* reason = 0, const Message* msg = 0);

    /**
     * Common processing after connecting the outgoing call, should be called
     *  from Driver's msgExecute()
     * @param msg Notification call.execute message while being dispatched
     */
    virtual void callConnect(Message& msg);

    /**
     * Set the local debugging level
     * @param msg Debug setting message
     */
    virtual bool setDebug(Message& msg);

    /**
     * Get the current status of the channel
     * @return The current status as String
     */
    inline const String& status() const
	{ return m_status; }

    /**
     * Get the current link address of the channel
     * @return The protocol dependent address as String
     */
    inline const String& address() const
	{ return m_address; }

    /**
     * Get the direction of the channel
     * @return True if the channel is an outgoing call (generated locally)
     */
    inline bool isOutgoing() const
	{ return m_outgoing; }

    /**
     * Get the direction of the channel
     * @return True if the channel is an incoming call (generated remotely)
     */
    inline bool isIncoming() const
	{ return !m_outgoing; }

    /**
     * Check if the call was answered or not
     * @return True if the call was answered
     */
    inline bool isAnswered() const
	{ return m_answered; }

    /**
     * Get the direction of the channel as string
     * @return "incoming" or "outgoing" according to the direction
     */
    const char* direction() const;

    /**
     * Get the driver of this channel
     * @return Pointer to this channel's driver
     */
    inline Driver* driver() const
	{ return m_driver; }

    /**
     * Get the time this channel will time out
     * @return Timeout time or zero if no timeout
     */
    inline u_int64_t timeout() const
	{ return m_timeout; }

    /**
     * Set the time this channel will time out
     * @param tout New timeout time or zero to disable
     */
    inline void timeout(u_int64_t tout)
	{ m_timeout = tout; }

    /**
     * Get the time this channel will time out on outgoing calls
     * @return Timeout time or zero if no timeout
     */
    inline u_int64_t maxcall() const
	{ return m_maxcall; }

    /**
     * Set the time this channel will time out on outgoing calls
     * @param tout New timeout time or zero to disable
     */
    inline void maxcall(u_int64_t tout)
	{ m_maxcall = tout; }

    /**
     * Set the time this channel will time out on outgoing calls
     * @param msg Reference of message possibly holding "maxcall" parameter
     * @param defTout Default timeout to apply, negative to not alter
     */
    inline void setMaxcall(const Message& msg, int defTout = -1)
	{ setMaxcall(&msg,defTout); }

    /**
     * Set the time this channel will time out on outgoing calls
     * @param msg Pointer to message possibly holding "maxcall" parameter
     * @param defTout Default timeout to apply, negative to not alter
     */
    void setMaxcall(const Message* msg, int defTout = -1);

    /**
     * Get the time this channel will time out while waiting for some progress
     *  on outgoing calls
     * @return Timeout time or zero if no timeout
     */
    inline u_int64_t maxPDD() const
	{ return m_maxPDD; }

    /**
     * Set the time this channel will time out while waiting for some progress
     *  on outgoing calls
     * @param tout New timeout time or zero to disable
     */
    inline void maxPDD(u_int64_t tout)
	{ m_maxPDD = tout; }

    /**
     * Set the time this channel will time out while waiting for some progress
     *  on outgoing calls
     * @param msg Reference of message possibly holding "maxpdd" parameter
     */
    void setMaxPDD(const Message& msg);

    /**
     * Get the connected channel identifier.
     * @return A String holding the unique channel id of the target or an empty
     *  string if this channel is not connected to a target.
     */
    inline const String& targetid() const
	{ return m_targetid; }

    /**
     * Get the billing identifier.
     * @return An identifier of the call or account that will be billed for
     *  calls made by this channel.
     */
    inline const String& billid() const
	{ return m_billid; }

    /**
     * Add the channel to the parent driver list
     * This method must be called exactly once after the object is fully constructed
     */
    void initChan();

    /**
     * Start a routing thread for this channel, dereference dynamic channels
     * @param msg Pointer to message to route, typically a "call.route", will be
     *  destroyed after routing fails or completes
     * @return True if routing thread started successfully, false if failed
     */
    bool startRouter(Message* msg);

    /**
     * Allocate an unique (per engine run) call ID
     * @return Unique call ID number
     */
    static unsigned int allocId();

    /**
     * Enable or disable debugging according to driver's filter rules
     * @param item Value of the item to match
     */
    void filterDebug(const String& item);

    /**
     * Get the disconnect parameters list
     * @return Constant reference to disconnect parameters
     */
    inline const NamedList& parameters() const
	{ return m_parameters; }

    /**
     * Notification for dispatched messages
     * @param msg Message that was dispatched
     * @param handled Result of handling the message
     */
    virtual void dispatched(const Message& msg, bool handled);

protected:
    /**
     * Constructor
     */
    Channel(Driver* driver, const char* id = 0, bool outgoing = false);

    /**
     * Alternate constructor provided for convenience
     */
    Channel(Driver& driver, const char* id = 0, bool outgoing = false);

    /**
     * Perform destruction time cleanup. You can call this method earlier
     *  if destruction is to be postponed.
     */
    void cleanup();

    /**
     * Remove the channel from the parent driver list
     */
    void dropChan();

    /**
     * This method is overriden to safely remove the channel from the parent
     *  driver list before actually destroying the channel.
     */
    virtual void zeroRefs();

    /**
     * Connect notification method.
     * @param reason Text that describes connect reason.
     */
    virtual void connected(const char* reason);

    /**
     * Disconnect notification method.
     * @param final True if this disconnect was called from the destructor.
     * @param reason Text that describes disconnect reason.
     */
    virtual void disconnected(bool final, const char* reason);

    /**
     * Set disconnect parameters
     * @param params Pointer to disconnect cause parameters, NULL to reset them
     */
    virtual void setDisconnect(const NamedList* params);

    /**
     * Notification after chan.disconnected handling
     * @param msg The chan.disconnected message
     * @param handled True if the message was handled
     */
    virtual void endDisconnect(const Message& msg, bool handled);

    /**
     * Set a new ID for this channel
     * @param newId New ID to set to this channel
     */
    virtual void setId(const char* newId);

    /**
     * Create a properly populated chan.disconnect message
     * @param reason Channel disconnect reason if available
     * @return A new allocated and parameter filled chan.disconnected message
     */
    virtual Message* getDisconnect(const char* reason);

    /**
     * Set the current status of the channel.
     * Note that a value of "answered" will set the answered flag
     * @param newstat The new status as String
     */
    void status(const char* newstat);

    /**
     * Build the parameter reporting part of the status answer
     * @param str String variable to fill up
     */
    virtual void statusParams(String& str);

    /**
     * Set the current direction of the channel
     * @param outgoing True if this is an outgoing call channel
     */
    inline void setOutgoing(bool outgoing = true)
	{ m_outgoing = outgoing; }

    /**
     * Add sequence number to chan.dtmf message, check for duplicates
     * @param msg chan.dtmf message to apply sequence number
     * @return True if the message is a duplicate (same tone, different method)
     */
    bool dtmfSequence(Message& msg);

    /**
     * Add sequence number to chan.dtmf and enqueue it, delete if duplicate
     * @param msg chan.dtmf message to sequence and enqueue
     * @return True if the message was enqueued, false if was a duplicate
     */
    bool dtmfEnqueue(Message* msg);

    /**
     * Attempt to install an override data source to send DTMF inband.
     * Needs a tone generator module capable to override with "tone/dtmfstr/xyz"
     * @param tone Pointer to the tone sequence to send
     * @return True on success
     */
    bool dtmfInband(const char* tone);

    /**
     * Attempt to install a data sniffer to detect inband tones
     * Needs a tone detector module capable of attaching sniffer consumers.
     * @param sniffer Name of the sniffer to install, default will detect all tones
     * @return True on success
     */
    bool toneDetect(const char* sniffer = 0);

    /**
     * Get the disconnect parameters list
     * @return Reference to disconnect parameters
     */
    inline NamedList& parameters()
	{ return m_parameters; }

private:
    void init();
    Channel(); // no default constructor please
};

/**
 * Driver is a module specialized for implementing channel drivers
 * @short A Channel driver module
 */
class YATE_API Driver : public Module
{
    friend class Router;
    friend class Channel;

private:
    bool m_init;
    bool m_varchan;
    String m_prefix;
    ObjList m_chans;
    int m_routing;
    int m_routed;
    int m_total;
    unsigned int m_nextid;
    int m_timeout;
    int m_maxroute;
    int m_maxchans;
    int m_chanCount;
    bool m_dtmfDups;

public:
    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Retrieve the prefix that is used as base for all channels
     * @return The driver's prefix
     */
    inline const String& prefix() const
	{ return m_prefix; }

    /**
     * Check if this driver is for dynamic (variable number) channels
     * @return True if the channels are dynamic, false for fixed
     */
    inline bool varchan() const
	{ return m_varchan; }

    /**
     * Get the list of channels of this driver
     * @return A reference to the channel list
     */
    inline ObjList& channels()
	{ return m_chans; }

    /**
     * Find a channel by id
     * @param id Unique identifier of the channel to find
     * @return Pointer to the channel or NULL if not found
     */
    virtual Channel* find(const String& id) const;

    /**
     * Check if the driver is actively used.
     * @return True if the driver is in use, false if should be ok to restart
     */
    virtual bool isBusy() const;

    /**
     * Drop all current channels
     * @param msg Notification message
     */
    virtual void dropAll(Message &msg);

    /**
     * Check if new connections can be accepted
     * @param routers Set to true to check routing threads for incoming connections
     * @return True if at least one new connection can be accepted, false if not
     */
    virtual bool canAccept(bool routers = true);

    /**
     * Check if new incoming connections can be routed
     * @return True if at least one new connection can be routed, false if not
     */
    virtual bool canRoute();

    /**
     * Get the next unique numeric id from a sequence
     * @return A driver unique number that increments by 1 at each call
     */
    unsigned int nextid();

    /**
     * Get the current (last used) unique numeric id from a sequence
     * @return The driver unique number
     */
    inline unsigned int lastid() const
	{ return m_nextid; }

    /**
     * Get the default driver timeout
     * @return Timeout value in milliseconds
     */
    inline int timeout() const
	{ return m_timeout; }

    /**
     * Get the number of calls currently in the routing stage
     * @return Number of router threads currently running
     */
    inline int routing() const
	{ return m_routing; }

    /**
     * Get the number of calls successfully routed
     * @return Number of calls that have gone past the routing stage
     */
    inline int routed() const
	{ return m_routed; }

    /**
     * Get the total number of calls ever created
     * @return Number of channels ever created for this driver
     */
    inline int total() const
	{ return m_total; }

    /**
     * Get the number of running channels
     * @return Number of channels running at this time
     */
    inline int chanCount() const
	{ return m_chanCount; }

    /**
     * Get the maximum number of running channels for this driver
     * @return Maximum number of calls to run simultaneously, zero to accept all
     */
    inline int maxChans() const
	{ return m_maxchans; }

protected:
    /**
     * Constructor
     * @param name Plugin name of this driver
     * @param type Type of the driver: "fixchans", "varchans", etc.
     */
    Driver(const char* name, const char* type = 0);

    /**
     * This method is called to initialize the loaded module
     */
    virtual void initialize();

    /**
     * Install standard message relays and set up the prefix
     * @param prefix Prefix to use with channels of this driver
     * @param minimal Install just a minimal set of message relays
     */
    void setup(const char* prefix = 0, bool minimal = false);

    /**
     * Message receiver handler
     * @param msg The received message
     * @param id The identifier with which the relay was created
     * @return True to stop processing, false to try other handlers
     */
    virtual bool received(Message &msg, int id);

    /**
     * Opportunity to modify the update message
     * @param msg Status update message
     */
    virtual void genUpdate(Message& msg);

    /**
     * Check if driver owns a client line (registered to an external server)
     * @param line Name of the line to check
     * @return True if this driver owns  line with the specified name
     *
     */
    virtual bool hasLine(const String& line) const;

    /**
     * Routing message handler. The default implementation routes to this
     *  driver if it owns a line named in the "account" or "line" parameter.
     * @param msg Call routing message
     * @return True to stop processing the message, false to try other handlers
     */
    virtual bool msgRoute(Message& msg);

    /**
     * Create an outgoing calling channel
     * @param msg Call execute message
     * @param dest Destination of the new call
     * @return True if outgoing call was created
     */
    virtual bool msgExecute(Message& msg, String& dest) = 0;

    /**
     * Complete a command line
     * @param msg Message to return completion into
     * @param partLine Partial line to complete, excluding the last word
     * @param partWord Partial word to complete
     * @return True to stop processing the message, false to try other handlers
     */
    virtual bool commandComplete(Message& msg, const String& partLine, const String& partWord);

    /**
     * Build the module identification part of the status answer
     * @param str String variable to fill up
     */
    virtual void statusModule(String& str);

    /**
     * Build the parameter reporting part of the status answer
     * @param str String variable to fill up
     */
    virtual void statusParams(String& str);

    /**
     * Build the channel list part of the status answer
     * @param str String variable to fill up
     */
    virtual void statusDetail(String& str);

    /**
     * Set the local debugging level
     * @param msg Debug setting message
     * @param target String to match for local settings
     */
    virtual bool setDebug(Message& msg, const String& target);

    /**
     * Load the global limits from the main config file
     */
    virtual void loadLimits();

    /**
     * Set if this driver is for dynamic (variable number) channels
     * @param variable True if the channels are dynamic, false for fixed
     */
    inline void varchan(bool variable)
	{ m_varchan = variable; }

    /**
     * Set the default driver timeout
     * @param tout New timeout in milliseconds or zero to disable
     */
    inline void timeout(int tout)
	{ m_timeout = tout; }

    /**
     * Set the maximum number of routing messages for this driver
     * @param ncalls Number of calls to route simultaneously, zero to accept all
     */
    inline void maxRoute(int ncalls)
	{ m_maxroute = ncalls; }

    /**
     * Set the maximum number of running channels for this driver
     * @param ncalls Number of calls to run simultaneously, zero to accept all
     */
    inline void maxChans(int ncalls)
	{ m_maxchans = ncalls; }

    /**
     * Set the DTMF duplicates allowed flag
     * @param duplicates True to allow DTMF duplicate messages
     */
    inline void dtmfDups(bool duplicates)
	{ m_dtmfDups = duplicates; }

private:
    Driver(); // no default constructor please
};

/**
 * Asynchronous call routing thread
 * @short Call routing thread
 */
class YATE_API Router : public Thread
{
    YNOCOPY(Router); // no automatic copies please
private:
    Driver* m_driver;
    String m_id;
    Message* m_msg;

public:
    /**
     * Constructor - creates a new routing thread
     * @param driver Pointer to the driver that asked for routing
     * @param id Unique identifier of the channel being routed
     * @param msg Pointer to an already filled message
     */
    Router(Driver* driver, const char* id, Message* msg);

    /**
     * Main thread running method
     */
    virtual void run();

    /**
     * Actual routing method
     * @return True if call was successfully routed
     */
    virtual bool route();

    /**
     * Thread cleanup handler
     */
    virtual void cleanup();

protected:
    /**
     * Get the routed channel identifier
     * @return Unique id of the channel being routed
     */
    const String& id() const
	{ return m_id; }
};

/**
 * This helper class holds generic account parameters that are applied to calls
 * @short Settings for an account handling calls
 */
class YATE_API CallAccount
{
    YNOCOPY(CallAccount);
private:
    Mutex* m_mutex;
    NamedList m_inbParams;
    NamedList m_outParams;
    NamedList m_regParams;

public:
    /**
     * Make a copy of the inbound and outbound parameter templates
     * @param params List of parameters to copy from
     */
    void pickAccountParams(const NamedList& params);


    /**
     * Patch the inbound call parameters
     * @param params List of parameters to be patched
     */
    void setInboundParams(NamedList& params);

    /**
     * Patch the outbound call parameters
     * @param params List of parameters to be patched
     */
    void setOutboundParams(NamedList& params);

    /**
     * Patch registration parameters
     * @param params List of parameters to be patched
     */
    void setRegisterParams(NamedList& params);

    /**
     * Accessor for the inbound call parameters list
     * @return Reference to the inbound call parameters
     */
    inline const NamedList& inboundParams() const
	{ return m_inbParams; }

    /**
     * Accessor for the outbound call parameters list
     * @return Reference to the outbound call parameters
     */
    inline const NamedList& outboundParams() const
	{ return m_outParams; }

    /**
     * Accessor for the registration parameters list
     * @return Reference to the registration parameters
     */
    inline const NamedList& registerParams() const
	{ return m_regParams; }

protected:
    /**
     * Constructor
     * @param mutex The mutex that is used to lock object's variables
     */
    inline CallAccount(Mutex* mutex)
	: m_mutex(mutex), m_inbParams(""), m_outParams(""), m_regParams("")
	{ }
};

/**
 * Find if a string appears to be an E164 phone number
 * @param str String to check
 * @return True if str appears to be a valid E164 number
 */
YATE_API bool isE164(const char* str);

}; // namespace TelEngine

#endif /* __YATEPHONE_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
