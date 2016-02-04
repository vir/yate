/**
 * yatengine.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Engine, plugins and messages related classes
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

#ifndef __YATENGINE_H
#define __YATENGINE_H

#ifndef __cplusplus
#error C++ is required
#endif

#include <yateclass.h>

/**
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

/**
 * A class for parsing and quickly accessing INI style configuration files
 * @short Configuration file handling
 */
class YATE_API Configuration : public String
{
    YNOCOPY(Configuration); // no automatic copies please
public:
    /**
     * Create an empty configuration
     */
    Configuration();

    /**
     * Create a configuration from a file
     * @param filename Name of file to initialize from
     * @param warn True to warn if the configuration could not be loaded
     */
    explicit Configuration(const char* filename, bool warn = true);

    /**
     * Assignment from string operator
     */
    inline Configuration& operator=(const String& value)
	{ String::operator=(value); return *this; }

    /**
     * Get the number of sections
     * @return Count of sections
     */
    inline unsigned int sections() const
	{ return m_sections.length(); }

    /**
     * Get the number of non null sections
     * @return Count of sections
     */
    inline unsigned int count() const
	{ return m_sections.count(); }

    /**
     * Retrieve an entire section
     * @param index Index of the section
     * @return The section's content or NULL if no such section
     */
    NamedList* getSection(unsigned int index) const;

    /**
     * Retrieve an entire section
     * @param sect Name of the section
     * @return The section's content or NULL if no such section
     */
    NamedList* getSection(const String& sect) const;

    /**
     * Locate a key/value pair in the section.
     * @param sect Name of the section
     * @param key Name of the key in section
     * @return A pointer to the key/value pair or NULL.
     */
    NamedString* getKey(const String& sect, const String& key) const;

    /**
     * Retrieve the value of a key in a section.
     * @param sect Name of the section
     * @param key Name of the key in section
     * @param defvalue Default value to return if not found
     * @return The string contained in the key or the default
     */
    const char* getValue(const String& sect, const String& key, const char* defvalue = 0) const;

    /**
     * Retrieve the numeric value of a key in a section.
     * @param sect Name of the section
     * @param key Name of the key in section
     * @param defvalue Default value to return if not found
     * @param minvalue Minimum value allowed for the parameter
     * @param maxvalue Maximum value allowed for the parameter
     * @param clamp Control the out of bound values: true to adjust to the nearest
     *  bound, false to return the default value
     * @return The number contained in the key or the default
     */
    int getIntValue(const String& sect, const String& key, int defvalue = 0,
	int minvalue = INT_MIN, int maxvalue = INT_MAX, bool clamp = true) const;

    /**
     * Retrieve the numeric value of a key in a section trying first a table lookup.
     * @param sect Name of the section
     * @param key Name of the key in section
     * @param tokens A pointer to an array of tokens to try to lookup
     * @param defvalue Default value to return if not found
     * @return The number contained in the key or the default
     */
    int getIntValue(const String& sect, const String& key, const TokenDict* tokens, int defvalue = 0) const;

    /**
     * Retrieve the 64-bit numeric value of a key in a section.
     * @param sect Name of the section
     * @param key Name of the key in section
     * @param defvalue Default value to return if not found
     * @param minvalue Minimum value allowed for the parameter
     * @param maxvalue Maximum value allowed for the parameter
     * @param clamp Control the out of bound values: true to adjust to the nearest
     *  bound, false to return the default value
     * @return The number contained in the key or the default
     */
    int64_t getInt64Value(const String& sect, const String& key, int64_t defvalue = 0,
	int64_t minvalue = LLONG_MIN, int64_t maxvalue = LLONG_MAX, bool clamp = true) const;

    /**
     * Retrieve the floating point value of a key in a section.
     * @param sect Name of the section
     * @param key Name of the key in section
     * @param defvalue Default value to return if not found
     * @return The numeric value contained in the key or the default
     */
    double getDoubleValue(const String& sect, const String& key, double defvalue = 0.0) const;

    /**
     * Retrieve the boolean value of a key in a section.
     * @param sect Name of the section
     * @param key Name of the key in section
     * @param defvalue Default value to return if not found
     * @return The boolean value contained in the key or the default
     */
    bool getBoolValue(const String& sect, const String& key, bool defvalue = false) const;

    /**
     * Deletes an entire section
     * @param sect Name of section to delete, NULL to delete all
     */
    void clearSection(const char* sect = 0);

    /**
     * Makes sure a section with a given name exists, creates if required
     * @param sect Name of section to check or create
     * @return The section's content or NULL if no such section
     */
    NamedList* createSection(const String& sect);

    /**
     * Deletes a key/value pair
     * @param sect Name of section
     * @param key Name of the key to delete
     */
    void clearKey(const String& sect, const String& key);

    /**
     * Add the value of a key in a section.
     * @param sect Name of the section, will be created if missing
     * @param key Name of the key to add in the section
     * @param value Value to set in the key
     */
    void addValue(const String& sect, const char* key, const char* value = 0);

    /**
     * Set the value of a key in a section.
     * @param sect Name of the section, will be created if missing
     * @param key Name of the key in section, will be created if missing
     * @param value Value to set in the key
     */
    void setValue(const String& sect, const char* key, const char* value = 0);

    /**
     * Set the numeric value of a key in a section.
     * @param sect Name of the section, will be created if missing
     * @param key Name of the key in section, will be created if missing
     * @param value Value to set in the key
     */
    void setValue(const String& sect, const char* key, int value);

    /**
     * Set the boolean value of a key in a section.
     * @param sect Name of the section, will be created if missing
     * @param key Name of the key in section, will be created if missing
     * @param value Value to set in the key
     */
    void setValue(const String& sect, const char* key, bool value);

    /**
     * Load the configuration from file
     * @param warn True to also warn if the configuration could not be loaded
     * @return True if successfull, false for failure
     */
    bool load(bool warn = true);

    /**
     * Save the configuration to file
     * @return True if successfull, false for failure
     */
    bool save() const;

private:
    ObjList *getSectHolder(const String& sect) const;
    ObjList *makeSectHolder(const String& sect);
    ObjList m_sections;
};

/**
 * Class that implements atomic / locked access and operations to its shared variables
 * @short Atomic access and operations to shared variables
 */
class YATE_API SharedVars : public Mutex
{
public:
    /**
     * Constructor
     */
    inline SharedVars()
	: Mutex(false,"SharedVars"), m_vars("")
	{ }

    /**
     * Get the string value of a variable
     * @param name Name of the variable
     * @param rval String to return the value into
     */
    void get(const String& name, String& rval);

    /**
     * Set the string value of a variable
     * @param name Name of the variable to set
     * @param val New value to assign to a variable
     */
    void set(const String& name, const char* val);

    /**
     * Create and set a variable only if the variable is not already set
     * @param name Name of the variable to set
     * @param val New value to assign to a variable
     * @return True if a new variable was created
     */
    bool create(const String& name, const char* val = 0);

    /**
     * Clear a variable
     * @param name Name of the variable to clear
     */
    void clear(const String& name);

    /**
     * Check if a variable exists
     * @param name Name of the variable
     * @return True if the variable exists
     */
    bool exists(const String& name);

    /**
     * Atomically increment a variable as unsigned integer
     * @param name Name of the variable
     * @param wrap Value to wrap around at, zero disables
     * @return Value of the variable before increment, zero if it was not defined or not numeric
     */
    unsigned int inc(const String& name, unsigned int wrap = 0);

    /**
     * Atomically decrement a variable as unsigned integer
     * @param name Name of the variable
     * @param wrap Value to wrap around at, zero disables (stucks at zero)
     * @return Value of the variable after decrement, zero if it was not defined or not numeric
     */
    unsigned int dec(const String& name, unsigned int wrap = 0);

private:
    NamedList m_vars;
};

class MessageDispatcher;
class MessageRelay;
class Engine;

/**
 * This class holds the messages that are moved around in the engine.
 * @short A message container class
 */
class YATE_API Message : public NamedList
{
    friend class MessageDispatcher;
public:
    /**
     * Creates a new message.
     *
     * @param name Name of the message - must not be NULL or empty
     * @param retval Default return value
     * @param broadcast Broadcast flag, true if handling the mesage must not stop it
     */
    explicit Message(const char* name, const char* retval = 0, bool broadcast = false);

    /**
     * Copy constructor.
     * Note that user data and notification are not copied
     * @param original Message we are copying from
     */
    Message(const Message& original);

    /**
     * Copy constructor that can alter the broadcast flag.
     * Note that user data and notification are not copied
     * @param original Message we are copying from
     * @param broadcast Broadcast flag, true if handling the mesage must not stop it
     */
    Message(const Message& original, bool broadcast);

    /**
     * Destruct the message and dereferences any user data
     */
    ~Message();

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

    /**
     * Retrieve a reference to the value returned by the message.
     * @return A reference to the value the message will return
     */
    inline String& retValue()
	{ return m_return; }

    /**
     * Retrieve a const reference to the value returned by the message.
     * @return A reference to the value the message will return
     */
    inline const String& retValue() const
	{ return m_return; }

    /**
     * Retrieve the object associated with the message
     * @return Pointer to arbitrary user RefObject
     */
    inline RefObject* userData() const
	{ return m_data; }

    /**
     * Set obscure data associated with the message.
     * The user data is reference counted to avoid stray pointers.
     * Note that setting new user data will disable any notification.
     * @param data Pointer to arbitrary user data
     */
    void userData(RefObject* data);

    /**
     * Get a pointer to a derived class of user data given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if user object id NULL or doesn't implement it
     */
    inline void* userObject(const String& name) const
	{ return m_data ? m_data->getObject(name) : 0; }


    /**
     * Enable or disable notification of any @ref MessageNotifier that was set
     *  as user data. This method must be called after userData()
     * @param notify True to have the message call the notifier
     */
    inline void setNotify(bool notify = true)
	{ m_notify = notify; }

    /**
     * Retrieve the broadcast flag
     * @return True if the message is a broadcast (handling does not stop it)
     */
    inline bool broadcast() const
	{ return m_broadcast; }

    /**
     * Retrieve a reference to the creation time of the message.
     * @return A reference to the @ref Time when the message was created
     */
    inline Time& msgTime()
	{ return m_time; }

    /**
     * Retrieve a const reference to the creation time of the message.
     * @return A reference to the @ref Time when the message was created
     */
    inline const Time& msgTime() const
	{ return m_time; }

    /**
     * Name assignment operator
     */
    inline Message& operator=(const char* value)
	{ String::operator=(value); return *this; }

    /**
     * Encode the message into a string adequate for sending for processing
     * to an external communication interface
     * @param id Unique identifier to add to the string
     */
    String encode(const char* id) const;

    /**
     * Encode the message into a string adequate for sending as answer
     * to an external communication interface
     * @param received True if message was processed locally
     * @param id Unique identifier to add to the string
     */
    String encode(bool received, const char* id) const;

    /**
     * Decode a string from an external communication interface for processing
     * in the engine. The message is modified accordingly.
     * @param str String to decode
     * @param id A String object in which the identifier is stored
     * @return -2 for success, -1 if the string was not a text form of a
     * message, index of first erroneous character if failed
     */
    int decode(const char* str, String& id);

    /**
     * Decode a string from an external communication interface that is an
     * answer to a specific external processing request.
     * @param str String to decode
     * @param received Pointer to variable to store the dispatch return value
     * @param id The identifier expected
     * @return -2 for success, -1 if the string was not the expected answer,
     * index of first erroneous character if failed
     */
    int decode(const char* str, bool& received, const char* id);

protected:
    /**
     * Notify the message it has been dispatched.
     * The default behaviour is to call the dispatched() method of the user
     *  data if it implements @ref MessageNotifier
     * @param accepted True if one handler accepted the message
     */
    virtual void dispatched(bool accepted);

private:
    Message(); // no default constructor please
    Message& operator=(const Message& value); // no assignment please
    String m_return;
    Time m_time;
    RefObject* m_data;
    bool m_notify;
    bool m_broadcast;
    void commonEncode(String& str) const;
    int commonDecode(const char* str, int offs);
};

/**
 * The purpose of this class is to hold a message received method that is
 *  called for matching messages. It holds as well the matching criteria
 *  and priority among other handlers.
 * @short A message handler
 */
class YATE_API MessageHandler : public String
{
    friend class MessageDispatcher;
    friend class MessageRelay;
    YNOCOPY(MessageHandler); // no automatic copies please
public:
    /**
     * Creates a new message handler.
     * @param name Name of the handled message - may be NULL
     * @param priority Priority of the handler, 0 = top
     * @param trackName Name to be used in handler tracking
     * @param addPriority True to append :priority to trackName
     */
    explicit MessageHandler(const char* name, unsigned priority = 100,
	const char* trackName = 0, bool addPriority = true);

    /**
     * Handler destructor.
     */
    virtual ~MessageHandler();

    /**
     * Destroys the object, performs cleanup first
     */
    virtual void destruct();

    /**
     * This method is called whenever the registered name matches the message.
     * @param msg The received message
     * @return True to stop processing, false to try other handlers
     */
    virtual bool received(Message& msg) = 0;

    /**
     * Find out the priority of the handler
     * @return Stored priority of the handler, 0 = top
     */
    inline unsigned priority() const
	{ return m_priority; }

    /**
     * Retrieve the tracking name of this handler
     * @return Name that is to be used in tracking operation
     */
    inline const String& trackName() const
	{ return m_trackName; }

    /**
     * Set a new tracking name for this handler.
     * Works only if the handler was not yet inserted into a dispatcher
     * @param name Name that is to be used in tracking operation
     */
    inline void trackName(const char* name)
	{ if (!m_dispatcher) m_trackName = name; }

    /**
     * Retrive the objects counter associated to this handler
     * @return Pointer to handler's objects counter or NULL
     */
    inline NamedCounter* objectsCounter() const
	{ return m_counter; }

    /**
     * Retrieve the filter (if installed) associated to this handler
     */
    inline const NamedString* filter() const
	{ return m_filter; }

    /**
     * Set a filter for this handler
     * @param filter Pointer to the filter to install, will be owned and
     *  destroyed by the handler
     */
    void setFilter(NamedString* filter);

    /**
     * Set a filter for this handler
     * @param name Name of the parameter to filter
     * @param value Value of the parameter to filter
     */
    inline void setFilter(const char* name, const char* value)
	{ setFilter(new NamedString(name,value)); }

    /**
     * Remove and destroy any filter associated to this handler
     */
    void clearFilter();

protected:
    /**
     * Remove the handler from its dispatcher, remove any installed filter.
     * This method is called internally from destruct and the destructor
     */
    void cleanup();

private:
    virtual bool receivedInternal(Message& msg);
    void safeNow();
    String m_trackName;
    unsigned m_priority;
    int m_unsafe;
    MessageDispatcher* m_dispatcher;
    NamedString* m_filter;
    NamedCounter* m_counter;
};

/**
 * A multiple message receiver to be invoked by a message relay
 * @short A multiple message receiver
 */
class YATE_API MessageReceiver : public GenObject
{
public:
    /**
     * This method is called from the message relay.
     * @param msg The received message
     * @param id The identifier with which the relay was created
     * @return True to stop processing, false to try other handlers
     */
    virtual bool received(Message& msg, int id) = 0;
};

/**
 * A message handler that allows to relay several messages to a single receiver
 * @short A message handler relay
 */
class YATE_API MessageRelay : public MessageHandler
{
    YNOCOPY(MessageRelay); // no automatic copies please
public:
    /**
     * Creates a new message relay.
     * @param name Name of the handled message - may be NULL
     * @param receiver Receiver of th relayed messages
     * @param id Numeric identifier to pass to receiver
     * @param priority Priority of the handler, 0 = top
     * @param trackName Name to be used in handler tracking
     * @param addPriority True to append :priority to trackName
     */
    inline MessageRelay(const char* name, MessageReceiver* receiver, int id,
	int priority = 100, const char* trackName = 0, bool addPriority = true)
	: MessageHandler(name,priority,trackName,addPriority),
	  m_receiver(receiver), m_id(id)
	{ }

    /**
     * This method is not called from MessageHandler through polymorphism
     *  and should not be used or reimplemented.
     * @param msg The received message
     * @return True if the receiver exists and has handled the message
     */
    virtual bool received(Message& msg)
	{ return m_receiver && m_receiver->received(msg,m_id); }

    /**
     * Get the ID of this message relay
     * @return Numeric identifier passed to receiver
     */
    inline int id() const
	{ return m_id; }

private:
    virtual bool receivedInternal(Message& msg);
    MessageReceiver* m_receiver;
    int m_id;
};

/**
 * An abstract class to implement hook methods called after any message has
 *  been dispatched. If an object implementing MessageNotifier is set as user
 *  data in a @ref Message then the dispatched() method will be called.
 * @short Post-dispatching message hook
 */
class YATE_API MessageNotifier
{
public:
    /**
     * Destructor. Keeps compiler form complaining.
     */
    virtual ~MessageNotifier();

    /**
     * This method is called after a message was dispatched.
     * @param msg The already dispatched message message
     * @param handled True if a handler claimed to have handled the message
     */
    virtual void dispatched(const Message& msg, bool handled) = 0;
};

/**
 * An abstract message notifier that can be inserted in a @ref MessageDispatcher
 *  to implement hook methods called after any message has been dispatched.
 * No new methods are provided - we only need the multiple inheritance.
 * @short Post-dispatching message hook that can be added to a list
 */
class YATE_API MessagePostHook : public RefObject, public MessageNotifier
{
};

/**
 * The dispatcher class is a hub that holds a list of handlers to be called
 *  for the messages that pass trough the hub. It can also handle a queue of
 *  messages that are typically dispatched by a separate thread.
 * @short A message dispatching hub
 */
class YATE_API MessageDispatcher : public GenObject, public Mutex
{
    friend class Engine;
    YNOCOPY(MessageDispatcher); // no automatic copies please
public:
    /**
     * Creates a new message dispatcher.
     * @param trackParam Name of the parameter used in tracking handlers
     */
    MessageDispatcher(const char* trackParam = 0);

    /**
     * Destroys the dispatcher and the installed handlers.
     */
    ~MessageDispatcher();

    /**
     * Retrieve the tracker parameter name
     * @return Name of the parameter used to track message dispatching
     */
    inline const String& trackParam() const
	{ return m_trackParam; }

    /**
     * Installs a handler in the dispatcher.
     * The handlers are installed in ascending order of their priorities.
     * There is NO GUARANTEE on the order of handlers with equal priorities
     *  although for avoiding uncertainity such handlers are sorted by address.
     * @param handler A pointer to the handler to install
     * @return True on success, false on failure
     */
    bool install(MessageHandler* handler);

    /**
     * Uninstalls a handler from the dispatcher.
     * @param handler A pointer to the handler to uninstall
     * @return True on success, false on failure
     */
    bool uninstall(MessageHandler* handler);

    /**
     * Synchronously dispatch a message to the installed handlers.
     * Handlers matching the message name and filter parameter are called in
     *  their installed order (based on priority) until one returns true.
     * If the message has the broadcast flag set all matching handlers are
     *  called and the return value is true if any handler returned true.
     * Note that in some cases when a handler is removed from the list
     *  other handlers with equal priority may be called twice.
     * @param msg The message to dispatch
     * @return True if one handler accepted it, false if all ignored
     */
    bool dispatch(Message& msg);

    /**
     * Put a message in the waiting queue for asynchronous dispatching
     * @param msg The message to enqueue, will be destroyed after dispatching
     * @return True if successfully queued, false otherwise
     */
    bool enqueue(Message* msg);

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
     * Set a limit to generate warning when a message took too long to dispatch
     * @param usec Warning time limit in microseconds, zero to disable
     */
    inline void warnTime(u_int64_t usec)
	{ m_warnTime = usec; }

    /**
     * Clear all the message handlers and post-dispatch hooks
     */
    inline void clear()
	{ m_handlers.clear(); m_hookAppend = &m_hooks; m_hooks.clear(); }

    /**
     * Get the number of messages waiting in the queue
     * @return Count of messages in the queue
     */
    unsigned int messageCount();

    /**
     * Get the number of handlers in this dispatcher
     * @return Count of handlers
     */
    unsigned int handlerCount();

    /**
     * Get the number of post-handling hooks in this dispatcher
     * @return Count of hooks
     */
    unsigned int postHookCount();

    /**
     * Install or remove a hook to catch messages after being dispatched
     * @param hook Pointer to a post-dispatching message hook
     * @param remove Set to True to remove the hook instead of adding
     */
    void setHook(MessagePostHook* hook, bool remove = false);

protected:
    /**
     * Set the tracked parameter name
     * @param paramName Name of the parameter used in tracking handlers
     */
    inline void trackParam(const char* paramName)
	{ m_trackParam = paramName; }

private:
    ObjList m_handlers;
    ObjList m_messages;
    ObjList m_hooks;
    Mutex m_hookMutex;
    ObjList* m_msgAppend;
    ObjList* m_hookAppend;
    String m_trackParam;
    unsigned int m_changes;
    u_int64_t m_warnTime;
    int m_hookCount;
    bool m_hookHole;
};

/**
 * Abstract class for message hook
 * @short Abstract message hook
 */
class YATE_API MessageHook : public RefObject
{
public:
    /**
     * Try to enqueue a message to this hook's queue
     * @param msg The message to enqueue
     * @return True if the message was enqueued.
     */
    virtual bool enqueue(Message* msg) = 0;

    /**
     * Clear this hook data
     */
    virtual void clear() = 0;

    /**
     * Check if the given message can be inserted in this queue
     * @param msg The message to check
     * @return True if the message can be inserted in this queue
     */
    virtual bool matchesFilter(const Message& msg) = 0;
};


/**
 * MessageQueue class allows to create a private queue for a message who matches
 * the specified filters.
 * @short A message queue
 */
class YATE_API MessageQueue : public MessageHook, public Mutex
{
    friend class Engine;
public:
    /**
     * Creates a new message queue.
     * @param hookName Name of the message served by this queue
     * @param numWorkers The number of workers who serve this queue
     */
    MessageQueue(const char* hookName, int numWorkers = 0);

    /**
     * Destroys the message queue
     */
    ~MessageQueue();

    /**
     * Append a message in the queue
     * @param msg The message to enqueue, will be destroyed after the processing is done
     * @return True if successfully queued, false otherwise
     */
    virtual bool enqueue(Message* msg);

    /**
     * Process a message from the waiting queue
     * @return False if the message queue is empty
     */
    bool dequeue();

    /**
     * Add a new filter to this queue
     * @param name The filter name
     * @param value The filter value
     */
    void addFilter(const char* name, const char* value);

    /**
     * Remove a filter form this queue
     * @param name The filter name
     */
    void removeFilter(const String& name);

    /**
     * Clear private data
     */
    virtual void clear();

    /**
     * Remove a thread from workers list
     * @param thread The thread to remove
     */
    void removeThread(Thread* thread);

    /**
     * Helper method to obtain the number of unprocessed messages in the queue
     * @return The number of queued messages.
     */
    inline unsigned int count() const
	{ return m_count; }

    /**
     * Obtain the filter list for this queue
     * @return The filter list
     */
    inline const NamedList& getFilters() const
	{ return m_filters; }

    /**
     * Check if the given message can be inserted in this queue
     * @param msg The message to check
     * @return True if the message can be inserted in this queue
     */
    virtual bool matchesFilter(const Message& msg);
protected:

    /**
     * Callback method for message processing
     * Default calls Engine::dispatch
     * @param msg The message to process
     */
    virtual void received(Message& msg);

private:
    NamedList m_filters;
    ObjList m_messages;
    ObjList m_workers;
    ObjList* m_append;
    unsigned int m_count;
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
class YATE_API Plugin : public GenObject, public DebugEnabler
{
public:
    /**
     * Creates a new Plugin container.
     * @param name the undecorated name of the library that contains the plugin
     * @param earlyInit True to initialize the plugin early
     */
    explicit Plugin(const char* name, bool earlyInit = false);

    /**
     * Destroys the plugin.
     * The destructor must never be called directly - the Loader will do it
     *  when the shared object's reference count reaches zero.
     */
    virtual ~Plugin();

    /**
     * Get a string representation of this object
     * @return Name of the plugin
     */
    virtual const String& toString() const
	{ return m_name; }

    /**
     * Get a pointer to a derived class given that class name
     * @param name Name of the class we are asking for
     * @return Pointer to the requested class or NULL if this object doesn't implement it
     */
    virtual void* getObject(const String& name) const;

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

    /**
     * Retrieve the name of the plugin
     * @return The plugin's name as String
     */
    inline const String& name() const
	{ return m_name; }

    /**
     * Retrive the objects counter associated to this plugin
     * @return Pointer to plugin's objects counter or NULL
     */
    inline NamedCounter* objectsCounter() const
	{ return m_counter; }

    /**
     * Check if the module is to be initialized early
     * @return True if the module should be initialized before regular ones
     */
    bool earlyInit() const
	{ return m_early; }

private:
    Plugin(); // no default constructor please
    String m_name;
    NamedCounter* m_counter;
    bool m_early;
};

#if 0 /* for documentation generator */
/**
 * Macro to create static instance of the plugin
 * @param pclass Class of the plugin to create
 */
void INIT_PLUGIN(class pclass);

/**
 * Macro to create the unloading function
 * @param unloadNow True if asked to unload immediately, false if just checking
 * @return True if the plugin can be unloaded, false if not
 */
bool UNLOAD_PLUGIN(bool unloadNow);
#endif

#define INIT_PLUGIN(pclass) static pclass __plugin
#ifdef DISABLE_UNLOAD
#define UNLOAD_PLUGIN(arg) static bool _unused_unload(bool arg)
#else
#ifdef _WINDOWS
#define UNLOAD_PLUGIN(arg) extern "C" __declspec(dllexport) bool _unload(bool arg)
#else
#define UNLOAD_PLUGIN(arg) extern "C" bool _unload(bool arg)
#endif
#endif

/**
 * Base class for engine running stage checkers.
 * Descendants may check specific conditions and decide to stop the engine.
 * There should be only one (static) instance of an engine checker
 * @short Engine checker interface
 */
class YATE_API EngineCheck
{
public:
    /**
     * Do-nothing destructor of base class
     */
    virtual ~EngineCheck()
	{ }

    /**
     * Check running conditions. This method is called by the engine in the initialize process
     * @param cmds Optional list of strings containing extra command line parameters
     *  (not parsed by the engine)
     * @return False to stop the program
     */
    virtual bool check(const ObjList* cmds) = 0;

    /**
     * Set the current engine checker
     * @param ptr The new engine checker. May be 0 to reset it
     */
    static void setChecker(EngineCheck* ptr = 0);
};

/**
 * Prototype for engine main loop callback
 */
typedef int (*EngineLoop)();

/**
 * This class holds global information about the engine.
 * Note: this is a singleton class.
 *
 * @short Engine globals
 */
class YATE_API Engine
{
    friend class EnginePrivate;
    friend class EngineCommand;
    YNOCOPY(Engine); // no automatic copies please
public:
    /**
     * Running modes - run the engine as Console, Client or Server.
     */
    enum RunMode {
	Stopped = 0,
	Console = 1,
	Server = 2,
	Client = 3,
	ClientProxy = 4,
    };

    enum CallAccept {
	Accept = 0,
	Partial = 1,
	Congestion = 2,
	Reject = 3,
    };

    /**
     * Plugin load and initialization modes.
     * Default is LoadLate that initailizes the plugin after others.
     * LoadEarly will move the plugin to the front of the init order.
     * LoadFail causes the plugin to be unloaded.
     */
    enum PluginMode {
	LoadFail = 0,
	LoadLate,
	LoadEarly
    };

    /**
     * Main entry point to be called directly from a wrapper program
     * @param argc Argument count
     * @param argv Argument array
     * @param env Environment variables
     * @param mode Mode the engine must run as - Console, Client or Server
     * @param loop Callback function to the main thread's loop
     * @param fail Fail and return after parsing command line arguments
     * @return Program exit code
     */
    static int main(int argc, const char** argv, const char** env,
	RunMode mode = Console, EngineLoop loop = 0, bool fail = false);

    /**
     * Display the help information on console
     * @param client Display help for client running mode
     * @param errout Display on stderr intead of stdout
     */
    static void help(bool client, bool errout = false);

    /**
     * Initialize the engine
     * @return Error code, 0 for success
     */
    int engineInit();

    /**
     * Do engine cleanup
     * @return Error code, 0 for success
     */
    int engineCleanup();

    /**
     * Run the engine.
     * @return Error code, 0 for success
     */
    int run();

    /**
     * Get a pointer to the unique instance.
     * @return A pointer to the singleton instance of the engine
     */
    static Engine* self();

    /**
     * Get the running mode of the engine
     * @return Engine's run mode as enumerated value
     */
    static RunMode mode()
	{ return s_mode; }

    /**
     * Get call accept status
     * @return Engine's call accept status as enumerated value
     */
    inline static CallAccept accept() {
	return (s_congestion && (s_accept < Congestion)) ? Congestion : s_accept;
    }

    /**
     * Set call accept status
     * @param ca New call accept status as enumerated value
     */
    inline static void setAccept(CallAccept ca) {
	s_accept = ca;
    }

    /**
     * Get call accept states
     * @return states Pointer to the call accept states TokenDict
     */
    inline static const TokenDict* getCallAcceptStates() {
	return s_callAccept;
    }

    /**
     * Alter the congestion state counter.
     * @param reason Reason to enter congested state, NULL to leave congestion
     */
    static void setCongestion(const char* reason = 0);

    /**
     * Get the congestion state counter
     * @return Zero if not congested else the number of congested components
     */
    static unsigned int getCongestion()
	{ return s_congestion; }

    /**
     * Check if the engine is running as telephony client
     * @return True if the engine is running in client mode
     */
    inline static bool clientMode()
	{ return (s_mode == Client) || (s_mode == ClientProxy); }

    /**
     * Register or unregister a plugin to the engine.
     * @param plugin A pointer to the plugin to (un)register
     * @param reg True to register (default), false to unregister
     * @return True on success, false on failure
     */
    static bool Register(const Plugin* plugin, bool reg = true);

    /**
     * Get the server node name, should be unique in a cluster
     * @return Node identifier string, defaults to host name
     */
    inline static const String& nodeName()
	{ return s_node; }

    /**
     * Get the application's shared directory path
     * @return The base path for shared files and directories
     */
    inline static const String& sharedPath()
	{ return s_shrpath; }

    /**
     * Get the filename for a specific configuration
     * @param name Name of the configuration requested
     * @param user True to build a user settings path
     * @return A full path configuration file name
     */
    static String configFile(const char* name, bool user = false);

    /**
     * Get the system or user configuration directory path
     * @param user True to get the user settings path
     * @return The directory path for system or user configuration files
     */
    static const String& configPath(bool user = false);

    /**
     * Get the configuration file suffix
     * @return The suffix for configuration files
     */
    inline static const String& configSuffix()
	{ return s_cfgsuffix; }

    /**
     * The module loading path
     */
    inline static const String& modulePath()
	{ return s_modpath; }

    /**
     * Add a relative extra module loading path. The list is empty by default
     *  but can be filled by a main program before calling @ref main()
     * @param path Relative path to extra modules to be loaded
     */
    static void extraPath(const String& path);

    /**
     * Set the per user application data path. This method must be called
     *  by a main program before calling @ref main() or @ref help()
     * Path separators are not allowed. The default is taken from CFG_DIR.
     * @param path Single relative path component to write user specific data
     */
    static void userPath(const String& path);

    /**
     * Get the module filename suffix
     * @return The suffix for module files
     */
    inline static const String& moduleSuffix()
	{ return s_modsuffix; }

    /**
     * Get the canonical path element separator for the operating system
     * @return The operating system specific path element separator
     */
    static const char* pathSeparator();

    /**
     * The global configuration of the engine.
     * You must use this resource with caution.
     * Note that sections [general], [modules], [preload] and [postload] are
     *  reserved by the engine. Also [telephony] is reserved by the drivers.
     * @return A reference to the read-only engine configuration
     */
    static const Configuration& config();

    /**
     * Get a - supposedly unique - instance ID
     * @return Unique ID of the current running instance
     */
    static unsigned int runId();

    /**
     * Get the engine parameters specific to this run.
     * @return A reference to the list of run specific parameters
     */
    inline static const NamedList& runParams()
	{ return s_params; }

    /**
     * Reinitialize the plugins
     */
    static void init();

    /**
     * Reinitialize one plugin
     * @param name Name of the plugin to initialize, emplty, "*" or "all" to initialize all
     * @return True if plugin(s) were reinitialized
     */
    static bool init(const String& name);

    /**
     * Stop the engine and the entire program
     * @param code Return code of the program
     */
    static void halt(unsigned int code);

    /**
     * Stop and restart the engine and the entire program
     * @param code Return code of the program
     * @param gracefull Attempt to wait until no plugin is busy
     * @return True if restart was initiated, false if exiting or no supervisor
     */
    static bool restart(unsigned int code, bool gracefull = false);

    /**
     * Check if the engine was started
     * @return True if the engine has gone through the start phase
     */
    static bool started()
	{ return s_started; }

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
    static bool install(MessageHandler* handler);

    /**
     * Uninstalls a handler drom the dispatcher.
     * @param handler A pointer to the handler to uninstall
     * @return True on success, false on failure
     */
    static bool uninstall(MessageHandler* handler);

    /**
     * Enqueue a message in the message queue for asynchronous dispatching
     * @param msg The message to enqueue, will be destroyed after dispatching
     * @param skipHooks True to append the message directly into the main queue
     * @return True if enqueued, false on error (already queued)
     */
    static bool enqueue(Message* msg, bool skipHooks = false);

    /**
     * Convenience function.
     * Enqueue a new parameterless message in the message queue
     * @param name Name of the parameterless message to put in queue
     * @param broadcast Broadcast flag, true if handling the mesage must not stop it
     * @return True if enqueued, false on error (already queued)
     */
    inline static bool enqueue(const char* name, bool broadcast = false)
	{ return name && *name && enqueue(new Message(name,0,broadcast)); }

    /**
     * Synchronously dispatch a message to the registered handlers
     * @param msg Pointer to the message to dispatch
     * @return True if one handler accepted it, false if all ignored
     */
    static bool dispatch(Message* msg);

    /**
     * Synchronously dispatch a message to the registered handlers
     * @param msg The message to dispatch
     * @return True if one handler accepted it, false if all ignored
     */
    static bool dispatch(Message& msg);

    /**
     * Convenience function.
     * Dispatch a parameterless message to the registered handlers
     * @param name The name of the message to create and dispatch
     * @param broadcast Broadcast flag, true if handling the mesage must not stop it
     * @return True if one handler accepted it, false if all ignored
     */
    static bool dispatch(const char* name, bool broadcast = false);

    /**
     * Install or remove a hook to catch messages after being dispatched
     * @param hook Pointer to a post-dispatching message hook
     * @param remove Set to True to remove the hook instead of adding
     */
    inline void setHook(MessagePostHook* hook, bool remove = false)
	{ m_dispatcher.setHook(hook,remove); }

    /**
     * Retrieve the tracker parameter name
     * @return Name of the parameter used to track message dispatching
     */
    inline static const String& trackParam()
	{ return s_self ? s_self->m_dispatcher.trackParam() : String::empty(); }

    /**
     * Appends a new message hook to the hooks list.
     * @param hook The message hook to append.
     * @return True if the message hook was successfully appended to the hooks list
     */
    static bool installHook(MessageHook* hook);

    /**
     * Remove a message hook from the hooks list.
     * @param hook The hook to remove.
     */
    static void uninstallHook(MessageHook* hook);

    /**
     * Get a count of plugins that are actively in use
     * @return Count of plugins in use
     */
    int usedPlugins();

    /**
     * Get the number of messages waiting in the queue
     * @return Count of messages in the queue
     */
    inline unsigned int messageCount()
	{ return m_dispatcher.messageCount(); }

    /**
     * Get the number of handlers in the dispatcher
     * @return Count of handlers
     */
    inline unsigned int handlerCount()
	{ return m_dispatcher.handlerCount(); }

    /**
     * Get the number of post-handling hooks in the dispatcher
     * @return Count of hooks
     */
    inline unsigned int postHookCount()
	{ return m_dispatcher.postHookCount(); }

    /**
     * Loads the plugins from an extra plugins directory or just an extra plugin
     * @param relPath Path to the extra directory, relative to the main modules
     * @return True if the plugin was loaded or the directory could at least be opened
     */
    bool loadPluginDir(const String& relPath);

    /**
     * Set the load and init mode of the currently loading @ref Plugin
     * @param mode Load and init mode, default LoadLate
     */
    static void pluginMode(PluginMode mode);

    /**
     * Retrive the list of captured events of a specific type
     * @param type Type of captured events, an empty name returns engine events
     * @return List containing captured events of specified type, NULL if not found
     */
    static const ObjList* events(const String& type);

    /**
     * Clear the list of captured events of a specific type
     * @param type Type of captured events, an empty name clear engine events
     */
    static void clearEvents(const String& type);

    /**
     * Access the engine's shared variables
     * @return Reference to the static variables shared between modules
     */
    static SharedVars& sharedVars();

    /**
     * Append command line arguments form current config.
     * The following parameters are added: -Dads, -v, -q, Debugger timestamp.
     * This method should be used when starting another libyate based application
     * @param line Destination string
     */
    static void buildCmdLine(String& line);

    /**
     * Initialize library from command line arguments.
     * Enable debugger output.
     * This method should be used in initialization stage of libyate based applications
     * @param line Command line arguments string
     * @param output Optional string to be filled with invalid argument errors
     *  or any output to be displyed later
     */
    static void initLibrary(const String& line, String* output = 0);

    /**
     * Cleanup library. Set late abort, kill all threads.
     * This method should be used in cleanup stage of libyate based applications
     * @return Halt code
     */
    static int cleanupLibrary();

protected:
    /**
     * Destroys the engine and everything. You must not call it directly,
     * @ref run() will do it for you.
     */
    ~Engine();

    /**
     * Loads one plugin from a shared object file
     * @param file Name of the plugin file to load
     * @param local Attempt to keep symbols local if supported by the system
     * @param nounload Never unload the module from memory, finalize if possible
     * @return True if success, false on failure
     */
    bool loadPlugin(const char* file, bool local = false, bool nounload = false);

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
    void internalStatisticsStart();
    void tryPluginFile(const String& name, const String& path, bool defload);
    ObjList m_libs;
    MessageDispatcher m_dispatcher;
    static Engine* s_self;
    static String s_node;
    static String s_shrpath;
    static String s_cfgsuffix;
    static String s_modpath;
    static String s_modsuffix;
    static ObjList s_extramod;
    static NamedList s_params;
    static int s_haltcode;
    static RunMode s_mode;
    static bool s_started;
    static unsigned int s_congestion;
    static CallAccept s_accept;
    static const TokenDict s_callAccept[];
};

}; // namespace TelEngine

#endif /* __YATENGINE_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
