/**
 * yatephone.h
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
struct ImageInfo {
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
struct FormatInfo {
    /**
     * Standard no-blanks lowercase format name
     */
    const char* name;

    /**
     * Format type: "audio", "video", "text"
     */
    const char* type;

    /**
     * Data rate in octets/second, 0 for variable
     */
    int dataRate;

    /**
     * Frame size in octets/frame, 0 for non-framed formats
     */
    int frameSize;

    /**
     * Rate in samples/second (audio) or 1e-6 frames/second (video), 0 for unknown
     */
    int sampleRate;

    /**
     * Number of channels, typically 1
     */
    int numChannels;

    /**
     * Guess the number of samples in an encoded data block
     * @param len Length of the data block in octets
     * @return Number of samples or 0 if unknown
     */
    int guessSamples(int len) const;

    /**
     * Default constructor - used to initialize arrays
     */
    inline FormatInfo()
	: name(0), type("audio"),
	  dataRate(0), frameSize(0),
	  sampleRate(8000), numChannels(1)
	{ }

    /**
     * Normal constructor
     */
    inline FormatInfo(const char* _name, int drate, int fsize = 0, const char* _type = "audio", int srate = 8000, int nchan = 1)
	: name(_name), type(_type),
	  dataRate(drate), frameSize(fsize),
	  sampleRate(srate), numChannels(nchan)
	{ }
};

class Channel;
class Driver;

/**
 * A structure to build (mainly static) translator capability tables.
 * A table of such structures must end with an entry with null format names.
 */
struct TranslatorCaps {
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
private:
    FormatRepository();
    FormatRepository& operator=(const FormatRepository&);
    virtual void dummy() const = 0;
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
     * @param drate Data rate in octets/second, 0 for variable
     * @param fsize Frame size in octets/frame, 0 for non-framed formats
     * @param type Format type: "audio", "video", "text"
     * @param srate Rate in samples/second (audio) or 1e-6 frames/second (video), 0 for unknown
     * @param nchan Number of channels, typically 1
     * @return Pointer to the format info or NULL if another incompatible
     *  format with the same name was already registered
    */
    static const FormatInfo* addFormat(const String& name, int drate, int fsize, const String& type = "audio", int srate = 8000, int nchan = 1);
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
 * A generic data handling object
 */
class YATE_API DataNode : public RefObject
{
public:
    /**
     * Construct a DataNode
     * @param format Description of the data format, default none
     */
    inline DataNode(const FormatInfo* format = 0)
	: m_format(format), m_timestamp(0) { }

    /**
     * Construct a DataNode from a format name
     * @param format Name of the data format, default none
     */
    inline DataNode(const String& format)
	: m_format(FormatRepository::getFormat(format)), m_timestamp(0) { }

    /**
     * Get the computing cost of converting the data to the format asked
     * @param format Name of the format to check for
     * @return -1 if unsupported, 0 for native format else cost in KIPS
     */
    virtual int costFormat(const FormatInfo* format)
	{ return -1; }

    /**
     * Change the format used to transfer data
     * @param format Description of the format to set for data
     * @return True if the format changed successfully, false if not changed
     */
    virtual bool setFormat(const FormatInfo* format = 0)
	{ return false; }

    /**
     * Change the format used to transfer data
     * @param format Name of the format to set for data
     * @return True if the format changed successfully, false if not changed
     */
    inline bool setFormat(const String& format)
	{ return setFormat(FormatRepository::getFormat(format)); }

    /**
     * Get the description of the format currently in use
     * @return Pointer to the data format
     */
    inline const FormatInfo* getFormat() const
	{ return m_format; }

    /**
     * Get the name of the format currently in use
     * @return Name of the data format
     */
    inline const char* getFormatName() const
	{ return m_format ? m_format->name : 0; }

    /**
     * Get the current position in the data stream
     * @return Timestamp of current data position
     */
    inline unsigned long timeStamp() const
	{ return m_timestamp; }

protected:
    /**
     * Change the format used to transfer data
     * @param format Description of the format to set for data
     * @return True if the format changed successfully, false if not changed
     */
    inline void setFormatInternal(const FormatInfo* format = 0)
	{ m_format = format; }

    /**
     * Change the format used to transfer data
     * @param format Name of the format to set for data
     * @return True if the format changed successfully, false if not changed
     */
    inline void setFormatInternal(const String& format)
	{ setFormatInternal(FormatRepository::getFormat(format)); }

    const FormatInfo* m_format;
    unsigned long m_timestamp;
};

/**
 * A data consumer
 */
class YATE_API DataConsumer : public DataNode
{
    friend class DataSource;
public:
    /**
     * Consumer constructor
     * @param format Description of the data format
     */
    inline DataConsumer(const FormatInfo *format)
	: DataNode(format), m_source(0) { }

    /**
     * Consumer constructor
     * @param format Name of the data format, default "slin" (Signed Linear)
     */
    inline DataConsumer(const String& format = "slin")
	: DataNode(format), m_source(0) { }

    /**
     * Consumes the data sent to it from a source
     * @param data The raw data block to process; an empty block ends data
     * @param timeDelta Timestamp increment of data - typically samples
     */
    virtual void Consume(const DataBlock& data, unsigned long timeDelta) = 0;

    /**
     * Get the data source of this object if it's connected
     * @return A pointer to the DataSource object or NULL
     */
    inline DataSource* getConnSource() const
	{ return m_source; }

    /**
     * Get the data source of a translator object
     * @return A pointer to the DataSource object or NULL
     */
    virtual DataSource* getTransSource() const
	{ return 0; }

private:
    inline void setSource(DataSource* source)
	{ m_source = source; }
    DataSource* m_source;
};

/**
 * A data source
 */
class YATE_API DataSource : public DataNode
{
    friend class DataTranslator;
public:
    /**
     * Source constructor
     * @param format Description of the data format
     */
    inline DataSource(const FormatInfo* format)
	: DataNode(format), m_translator(0) { }

    /**
     * Source constructor
     * @param format Name of the data format, default "slin" (Signed Linear)
     */
    inline DataSource(const String& format = "slin")
	: DataNode(format), m_translator(0) { }

    /**
     * Source's destructor - detaches all consumers
     */
    virtual ~DataSource();
    
    /**
     * Forwards the data to its consumers
     * @param data The raw data block to forward; an empty block ends data
     * @param timeDelta Timestamp increment of data - typically samples
     */
    void Forward(const DataBlock& data, unsigned long timeDelta = 0);

    /**
     * Attach a data consumer
     * @param consumer Data consumer to attach
     * @return True on success, false on failure
     */
    bool attach(DataConsumer* consumer);

    /**
     * Detach a data consumer
     * @param consumer Data consumer to detach
     * @return True on success, false on failure
     */
    bool detach(DataConsumer* consumer);

    /**
     * Detach all data consumers
     */
    inline void clear()
	{ m_consumers.clear(); }

    /**
     * Get the master translator object if this source is part of a translator
     * @return A pointer to the DataTranslator object or NULL
     */
    inline DataTranslator* getTranslator() const
	{ return m_translator; }

protected:
    /**
     * The current position in the data - format dependent, usually samples
     */
    inline void setTranslator(DataTranslator* translator)
	{ m_translator = translator; }
    DataTranslator* m_translator;
    ObjList m_consumers;
    Mutex m_mutex;
};

/**
 * A data source with a thread of its own
 */
class YATE_API ThreadedSource : public DataSource
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
    bool start(const char* name = "ThreadedSource");

    /**
     * Stops and destroys the worker thread if running
     */
    void stop();

    /**
     * Return a pointer to the worker thread
     * @return Pointer to running worker thread or NULL
     */
    Thread* thread() const;

protected:
    /**
     * Threaded Source constructor
     * @param format Description of the data format
     */
    inline ThreadedSource(const FormatInfo* format)
	: DataSource(format), m_thread(0) { }

    /**
     * Threaded Source constructor
     * @param format Name of the data format, default "slin" (Signed Linear)
     */
    inline ThreadedSource(const String& format = "slin")
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
    DataTranslator(const char* sFormat, DataSource* source = 0);

    /**
     * Destroys the translator and its source
     */
    ~DataTranslator();

    /**
     * Get the data source of a translator object
     * @return A pointer to the DataSource object or NULL
     */
    virtual DataSource* getTransSource() const
	{ return m_tsource; }

    /**
     * Get a textual list of formats supported for a given output format.
     * @param dFormat Name of destination format
     * @return Space separated list of source formats
     */
    static String srcFormats(const String& dFormat = "slin");

    /**
     * Get a textual list of formats supported for a given input format
     * @param sFormat Name of source format
     * @return Space separated list of destination formats
     */
    static String destFormats(const String& sFormat = "slin");

    /**
     * Finds the cost of a translator given the source and destination format names
     * @param sFormat Name of the source format (data received from the consumer)
     * @param dFormat Name of the destination format (data supplied to the source)
     * @return Cost of best (cheapest) codec or -1 if no known codec exists
     */
    static int cost(const String& sFormat, const String& dFormat);

    /**
     * Creates a translator given the source and destination format names
     * @param sFormat Name of the source format (data received from the consumer)
     * @param dFormat Name of the destination format (data supplied to the source)
     * @return A pointer to a DataTranslator object or NULL if no known codec exists
     */
    static DataTranslator* create(const String& sFormat, const String& dFormat);

    /**
     * Attach a consumer to a source, possibly trough a chain of translators
     * @param source Source to attach the chain to
     * @param consumer Consumer where the chain ends
     * @return True if successfull, false if no translator chain could be built
     */
    static bool attachChain(DataSource* source, DataConsumer* consumer);

    /**
     * Detach a consumer from a source, possibly trough a chain of translators
     * @param source Source to dettach the chain from
     * @param consumer Consumer where the chain ends
     * @return True if successfull, false if source and consumers were not attached
     */
    static bool detachChain(DataSource* source, DataConsumer* consumer);

protected:
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
    DataSource* m_tsource;
    static Mutex s_mutex;
    static ObjList s_factories;
};

/**
 * A factory for constructing data translators by format name
 * conversion of data from one type to another
 * @short An unidirectional data translator (codec)
 */
class YATE_API TranslatorFactory : public GenObject
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
    virtual DataTranslator* create(const String& sFormat, const String& dFormat) = 0;

    /**
     * Get the capabilities table of this translator
     * @return A pointer to the first element of the capabilities table
     */
    virtual const TranslatorCaps* getCapabilities() const = 0;
};

/**
 * The DataEndpoint holds an endpoint capable of performing unidirectional
 * or bidirectional data transfers
 * @short A data transfer endpoint capable of sending and/or receiving data
 */
class YATE_API DataEndpoint : public RefObject
{
public:

    /**
     * Creates an empty data endpoint
     */
    DataEndpoint(Channel* chan = 0, const char* name = "audio");

    /**
     * Destroys the endpoint, source and consumer
     */
    ~DataEndpoint();

    /**
     * Get a string identification of the endpoint
     * @return A reference to this endpoint's name
     */
    virtual const String& toString() const;

    /**
     * Connect the source and consumer of the endpoint to a peer
     * @param peer Pointer to the peer data endpoint
     * @return True if connected, false if incompatible source/consumer
     */
    bool connect(DataEndpoint* peer);

    /**
     * Disconnect from the connected endpoint
     */
    inline void disconnect()
	{ disconnect(false); }

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

    /*
     * Get a pointer to the peer endpoint
     * @return A pointer to the peer endpoint or NULL
     */
    inline DataEndpoint* getPeer() const
	{ return m_peer; }

    /*
     * Get a pointer to the owner channel
     * @return A pointer to the owner channel or NULL
     */
    inline Channel* getChannel() const
	{ return m_channel; }

    /**
     * Get the name set in constructor
     * @return A reference to the name as hashed string
     */
    inline const String& name() const
	{ return m_name; }

protected:
    /**
     * Connect notification method
     */
    virtual void connected() { }

    /**
     * Disconnect notification method
     * @param final True if this disconnect was called from the destructor
     */
    virtual void disconnected(bool final) { }

    /**
     * Attempt to connect the endpoint to a peer of the same type
     * @param peer Pointer to the endpoint data driver
     * @return True if connected, false if failed native connection
     */
    virtual bool nativeConnect(DataEndpoint* peer)
	{ return false; }

    /*
     * Set the peer endpoint pointer
     * @param peer A pointer to the new peer or NULL
     */
    void setPeer(DataEndpoint* peer);

private:
    void disconnect(bool final);
    String m_name;
    DataSource* m_source;
    DataConsumer* m_consumer;
    DataEndpoint* m_peer;
    Channel* m_channel;
};

/**
 * Module is a descendent of Plugin specialized in implementing modules
 * @short A Plugin that implements a module
 */
class YATE_API Module : public Plugin, public Mutex, public MessageReceiver, public DebugEnabler
{
private:
    bool m_init;
    String m_name;
    String m_type;
    u_int64_t m_changed;
    static unsigned int s_delay;

public:
    /**
     * Retrive the name of the module
     * @return The module's name as String
     */
    inline const String& name() const
	{ return m_name; }

    /**
     * Retrive the type of the module
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
     * Retrive the global update notification delay
     * @return Update delay value in usec
     */
    inline static unsigned int updateDelay()
	{ return s_delay; }

    /**
     * Set the global update notification delay
     * @param delay New update delay value in usec, 0 to disable
     */
    inline static void updateDelay(unsigned int delay)
	{ s_delay = delay; }

protected:
    /**
     * IDs of the installed relays
     */
    enum {
	// Module messages
	Status,
	Timer,
	Level,
	// Driver messages
	Execute,
	Drop,
	// Channel messages
	Ringing,
	Answered,
	Tone,
	Text,
	Masquerade,
	Locate,
    } RelayID;

    /**
     * Constructor
     * @param name Plugin name of this driver
     * @param type Type of the driver: "misc", "route", etc.
     */
    Module(const char* name, const char* type = 0);

    /**
     * This method is called to initialize the loaded module
     */
    virtual void initialize();

    /**
     * Install standard message relays
     */
    void setup();

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
     * Set the local debugging level
     * @param msg Debug setting message
     * @param target String to match for local settings
     */
    virtual bool setDebug(Message& msg, const String& target);

private:
    Module(); // no default constructor please
};

/**
 * A class that holds common channel related features (a.k.a. call leg)
 * @short An abstract communication channel
 */
class YATE_API Channel : public RefObject, public DebugEnabler
{
    friend class Driver;
    friend class Router;
    friend class DataEndpoint;

private:
    Channel* m_peer;
    Driver* m_driver;
    bool m_outgoing;
    String m_id;

protected:
    String m_status;
    String m_address;
    String m_targetid;
    String m_billid;
    ObjList m_data;

public:
    /**
     * Destructor
     */
    virtual ~Channel();

    /**
     * Get a string representation of this channel
     * @return A reference to the name of this object
     */
    virtual const String& toString() const
	{ return m_id; }

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
     * @return A new allocated and parameter filled message
     */
    Message* message(const char* name, bool minimal = false) const;

    /**
     * Notification on remote ringing
     * @param msg Notification message
     * @return True to stop processing the message, false to let it flow
     */
    virtual bool msgRinging(Message& msg);

    /**
     * Notification on remote answered
     * @param msg Notification message
     * @return True to stop processing the message, false to let it flow
     */
    virtual bool msgAnswered(Message& msg);

    /**
     * Notification on remote tone(s)
     * @param msg Notification message
     * @param tones Pointer to the received tone(s)
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
     * @return True if initiated call drop, false if failed
     */
    virtual bool msgDrop(Message& msg);

    /**
     * Notification on success of incoming call
     * @param msg Notification call.execute message
     */
    virtual void callAccept(Message& msg);

    /**
     * Notification on failure of incoming call
     * @param error Standard error keyword
     * @param reason Textual failure reason
     */
    virtual void callReject(const char* error, const char* reason = 0);

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
    bool isOutgoing() const
	{ return m_outgoing; }

    /**
     * Get the direction of the channel
     * @return True if the channel is an incoming call (generated remotely)
     */
    bool isIncoming() const
	{ return !m_outgoing; }

    /**
     * Get the direction of the channel as string
     * @return "incoming" or "outgoing" according to the direction
     */
    const char* direction() const;

    /**
     * Get the unique channel identifier
     * @return A String holding the unique channel id
     */
    inline const String& id() const
	{ return m_id; }

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
     * Get the connected peer channel
     * @return Pointer to connected peer channel or NULL
     */
    inline Channel* getPeer() const
	{ return m_peer; }

    /**
     * Connect the channel to a peer.
     * @param peer Pointer to the peer channel.
     * @return True if connected, false if an error occured.
     */
    bool connect(Channel* peer);

    /**
     * Disconnect from the connected peer channel.
     * @param reason Text that describes disconnect reason.
     */
    inline void disconnect(const char* reason = 0)
	{ disconnect(false,reason); }

    /**
     * Get the driver of this channel
     * @return Pointer to this channel's driver
     */
    inline Driver* driver() const
	{ return m_driver; }

    /**
     * Get a data endpoint of this object
     * @param type Type of data endpoint: "audio", "video", "text"
     * @return A pointer to the DataEndpoint object or NULL if not found
     */
    DataEndpoint* getEndpoint(const char* type = "audio") const;

    /**
     * Get a data endpoint of this object, create if required
     * @param type Type of data endpoint: "audio", "video", "text"
     * @return A pointer to the DataEndpoint object or NULL if an error occured
     */
    DataEndpoint* addEndpoint(const char* type = "audio");

    /**
     * Set a data source of this object
     * @param source A pointer to the new source or NULL
     * @param type Type of data node: "audio", "video", "text"
     */
    void setSource(DataSource* source = 0, const char* type = "audio");

    /**
     * Get a data source of this object
     * @param type Type of data node: "audio", "video", "text"
     * @return A pointer to the DataSource object or NULL
     */
    DataSource* getSource(const char* type = "audio") const;

    /**
     * Set the data consumer of this object
     * @param consumer A pointer to the new consumer or NULL
     * @param type Type of data node: "audio", "video", "text"
     */
    void setConsumer(DataConsumer* consumer = 0, const char* type = "audio");

    /**
     * Get the data consumer of this object
     * @param type Type of data node: "audio", "video", "text"
     * @return A pointer to the DataConsumer object or NULL
     */
    DataConsumer* getConsumer(const char* type = "audio") const;

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
     * Set the current status of the channel
     * @param newstat The new status as String
     */
    inline void status(const char* newstat)
	{ m_status = newstat; }

    /**
     * Connect notification method.
     */
    virtual void connected() { }

    /**
     * Disconnect notification method.
     * @param final True if this disconnect was called from the destructor.
     * @param reason Text that describes disconnect reason.
     */
    virtual void disconnected(bool final, const char* reason) { }

    /*
     * Set the peer channel pointer.
     * @param peer A pointer to the new peer or NULL.
     * @param reason Text describing the reason in case of disconnect.
     */
    void setPeer(Channel* peer, const char* reason = 0);

private:
    void init();
    void disconnect(bool final, const char* reason);
    Channel(); // no default constructor please
};

/**
 * Driver is a module specialized for implementing channel drivers
 * @short A Channel driver module
 */
class YATE_API Driver : public Module
{
    friend class Router;

private:
    bool m_init;
    String m_prefix;
    ObjList m_chans;
    int m_routing;
    int m_routed;
    unsigned int m_nextid;

public:
    /**
     * Retrive the prefix that is used as base for all channels
     * @return The driver's prefix
     */
    inline const String& prefix() const
	{ return m_prefix; }

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
     * Get the next unique numeric id from a sequence
     * @return A dierv unique number that increments by 1 at each call
     */
    unsigned int nextid();

protected:
    /**
     * Constructor
     * @param name Plugin name of this driver
     * @param type Type of the driver: "fixchan", "varchan", etc.
     */
    Driver(const char* name, const char* type = 0);

    /**
     * Install standard message relays and set up the prefix
     * @param prefix Prefix to use with channels of this driver
     */
    void setup(const char* prefix = 0);

    /**
     * Message receiver handler
     * @param msg The received message
     * @param id The identifier with which the relay was created
     * @return True to stop processing, false to try other handlers
     */
    virtual bool received(Message &msg, int id);

    /**
     * Drop all current channels
     */
    virtual void dropAll();

    /**
     * Opportunity to modify the update message
     * @param msg Status update message
     */
    virtual void genUpdate(Message& msg);

    /**
     * Create an outgoing calling channel
     * @param msg Call execute message
     * @param dest Destination of the new call
     * @return True if outgoing call was created
     */
    virtual bool msgExecute(Message& msg, String& dest) = 0;

    /**
     * Status message handler that is invoked only for matching messages.
     * @param msg Status message
     */
    virtual void msgStatus(Message& msg);

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
     * Build the clannel list part of the status answer
     * @param str String variable to fill up
     */
    virtual void statusChannels(String& str);

    /**
     * Set the local debugging level
     * @param msg Debug setting message
     * @param target String to match for local settings
     */
    virtual bool setDebug(Message& msg, const String& target);

private:
    Driver(); // no default constructor please
};

/**
 * Asynchronous call routing thread
 * @short Call routing thread
 */
class YATE_API Router : public Thread
{
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

}; // namespace TelEngine

#endif /* __YATEPHONE_H */
