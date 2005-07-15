/**
 * yatengine.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Engine, plugins and messages related classes
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
public:
    /**
     * Create an empty configuration
     */
    Configuration();

    /**
     * Create a configuration from a file
     * @param filename Name of file to initialize from
     */
    Configuration(const char* filename);

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
     * Retrive an entire section
     * @param index Index of the section
     * @return The section's content or NULL if no such section
     */
    NamedList* getSection(unsigned int index) const;

    /**
     * Retrive an entire section
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
     * Retrive the value of a key in a section.
     * @param sect Name of the section
     * @param key Name of the key in section
     * @param defvalue Default value to return if not found
     * @return The string contained in the key or the default
     */
    const char* getValue(const String& sect, const String& key, const char* defvalue = 0) const;

    /**
     * Retrive the numeric value of a key in a section.
     * @param sect Name of the section
     * @param key Name of the key in section
     * @param defvalue Default value to return if not found
     * @return The number contained in the key or the default
     */
    int getIntValue(const String& sect, const String& key, int defvalue = 0) const;

    /**
     * Retrive the numeric value of a key in a section trying first a table lookup.
     * @param sect Name of the section
     * @param key Name of the key in section
     * @param tokens A pointer to an array of tokens to try to lookup
     * @param defvalue Default value to return if not found
     * @return The number contained in the key or the default
     */
    int getIntValue(const String& sect, const String& key, const TokenDict* tokens, int defvalue = 0) const;

    /**
     * Retrive the boolean value of a key in a section.
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
     * @return True if successfull, false for failure
     */
    bool load();

    /**
     * Save the configuration to file
     * @return True if successfull, false for failure
     */
    bool save() const;

private:
    Configuration(const Configuration& value); // no copy constructor
    Configuration& operator=(const Configuration& value); // no assignment please
    ObjList *getSectHolder(const String& sect) const;
    ObjList *makeSectHolder(const String& sect);
    ObjList m_sections;
};

class MessageDispatcher;

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
     */
    Message(const char* name, const char* retval = 0);

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
     * Retrive a reference to the value returned by the message.
     * @return A reference to the value the message will return
     */
    inline String& retValue()
	{ return m_return; }

    /**
     * Retrive the object associated with the message
     * @return Pointer to arbitrary user RefObject
     */
    inline RefObject* userData() const
	{ return m_data; }

    /**
     * Set obscure data associated with the message.
     * The user data is reference counted to avoid stray pointers.
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
     * Retrive a reference to the creation time of the message.
     * @return A reference to the Time when the message was created
     */
    inline Time& msgTime()
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
     * Notify the message it has been dispatched
     */
    virtual void dispatched(bool accepted)
	{ }

private:
    Message(); // no default constructor please
    Message(const Message& value); // no copy constructor
    Message& operator=(const Message& value); // no assignment please
    String m_return;
    Time m_time;
    RefObject* m_data;
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
public:
    /**
     * Creates a new message handler.
     * @param name Name of the handled message - may be NULL
     * @param priority Priority of the handler, 0 = top
     */
    MessageHandler(const char* name, unsigned priority = 100);

    /**
     * Handler destructor.
     */
    virtual ~MessageHandler();

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
     * Retrive the filter (if installed) associated to this handler
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

private:
    unsigned m_priority;
    MessageDispatcher* m_dispatcher;
    NamedString* m_filter;
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
public:
    /**
     * Creates a new message relay.
     * @param name Name of the handled message - may be NULL
     * @param receiver Receiver of th relayed messages
     * @param id Numeric identifier to pass to receiver
     * @param priority Priority of the handler, 0 = top
     */
    MessageRelay(const char* name, MessageReceiver* receiver, int id, int priority = 100)
	: MessageHandler(name,priority), m_receiver(receiver), m_id(id) { }

    /**
     * This method is called whenever the registered name matches the message.
     * @param msg The received message
     * @return True to stop processing, false to try other handlers
     */
    virtual bool received(Message& msg)
	{ return m_receiver ? m_receiver->received(msg,m_id) : false; }

private:
    MessageReceiver* m_receiver;
    int m_id;
};

/**
 * An abstract class to implement hook methods called after any message has
 *  been dispatched.
 * @short Post-dispatching message hook
 */
class YATE_API MessagePostHook : public GenObject
{
public:
    /**
     * This method is called after a message was dispatched.
     * @param msg The already dispatched message message
     * @param handled True if a handler claimed to have handled the message
     */
    virtual void dispatched(const Message& msg, bool handled) = 0;
};

/**
 * The dispatcher class is a hub that holds a list of handlers to be called
 *  for the messages that pass trough the hub. It can also handle a queue of
 *  messages that are typically dispatched by a separate thread.
 * @short A message dispatching hub
 */
class YATE_API MessageDispatcher : public GenObject
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
    bool install(MessageHandler* handler);

    /**
     * Uninstalls a handler from the dispatcher.
     * @param handler A pointer to the handler to uninstall
     * @return True on success, false on failure
     */
    bool uninstall(MessageHandler* handler);

    /**
     * Dispatch a message to the installed handlers
     * @param msg The message to dispatch
     * @return True if one handler accepted it, false if all ignored
     */
    bool dispatch(Message& msg);

    /**
     * Put a message in the waiting queue
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
     * Clear all the message handlers and post-dispatch hooks
     */
    inline void clear()
	{ m_handlers.clear(); m_hooks.clear(); }

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
     * Install or remove a hook to catch messages after being dispatched
     * @param hook Pointer to a post-dispatching message hook
     * @param remove Set to True to remove the hook instead of adding
     */
    void setHook(MessagePostHook* hook, bool remove = false);

private:
    ObjList m_handlers;
    ObjList m_messages;
    ObjList m_hooks;
    Mutex m_mutex;
    unsigned int m_changes;
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
class YATE_API Plugin : public GenObject
{
public:
    /**
     * Creates a new Plugin container.
     * @param name the undecorated name of the library that contains the plugin
     */
    Plugin(const char* name);

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
class YATE_API Engine
{
    friend class EnginePrivate;
public:
    /**
     * Running modes - run the engine as Console, Client or Server.
     */
    enum RunMode {
	Console = 0,
	Client = 1,
	Server = 2
    };

    /**
     * Main entry point to be called directly from a wrapper program
     * @param argc Argument count
     * @param argv Argument array
     * @param env Environment variables
     * @param client True to parse arguments and run as a client
     * @param fail Fail and return after parsing command line arguments
     * @return Program exit code
     */
    static int main(int argc, const char** argv, const char** env,
	RunMode mode = Console, bool fail = false);

    /**
     * Display the help information on console
     * @param errout Display on stderr intead of stdout
     */
    static void help(bool client, bool errout = false);

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
     * Register or unregister a plugin to the engine.
     * @param plugin A pointer to the plugin to (un)register
     * @param reg True to register (default), false to unregister
     * @return True on success, false on failure
     */
    static bool Register(const Plugin* plugin, bool reg = true);

    /**
     * The configuration directory path
     */
    static String configFile(const char* name);

    /**
     * The configuration directory path
     */
    inline static String& configPath()
	{ return s_cfgpath; }

    /**
     * The configuration file suffix
     */
    inline static String& configSuffix()
	{ return s_cfgsuffix; }

    /**
     * The module loading path
     */
    inline static String& modulePath()
	{ return s_modpath; }

    /**
     * The relative extra module loading path. This is empty by default but
     *  can be set by a main program to a value before calling @ref main()
     */
    inline static String& extraPath()
	{ return s_extramod; }

    /**
     * The module suffix
     */
    inline static String& moduleSuffix()
	{ return s_modsuffix; }

    /**
     * The operating system specific path element separator
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
    static bool install(MessageHandler* handler);

    /**
     * Uninstalls a handler drom the dispatcher.
     * @param handler A pointer to the handler to uninstall
     * @return True on success, false on failure
     */
    static bool uninstall(MessageHandler* handler);

    /**
     * Enqueue a message in the message queue
     * @param msg The message to enqueue, will be destroyed after dispatching
     * @return True if enqueued, false on error (already queued)
     */
    static bool enqueue(Message* msg);

    /**
     * Convenience function.
     * Enqueue a new parameterless message in the message queue
     * @param name Name of the parameterless message to put in queue
     * @return True if enqueued, false on error (already queued)
     */
    inline static bool enqueue(const char* name)
	{ return (name && *name) ? enqueue(new Message(name)) : false; }

    /**
     * Dispatch a message to the registered handlers
     * @param msg Pointer to the message to dispatch
     * @return True if one handler accepted it, false if all ignored
     */
    static bool dispatch(Message* msg);

    /**
     * Dispatch a message to the registered handlers
     * @param msg The message to dispatch
     * @return True if one handler accepted it, false if all ignored
     */
    static bool dispatch(Message& msg);

    /**
     * Convenience function.
     * Dispatch a parameterless message to the registered handlers
     * @param name The name of the message to create and dispatch
     * @return True if one handler accepted it, false if all ignored
     */
    static bool dispatch(const char* name);

    /**
     * Install or remove a hook to catch messages after being dispatched
     * @param hook Pointer to a post-dispatching message hook
     * @param remove Set to True to remove the hook instead of adding
     */
    inline void setHook(MessagePostHook* hook, bool remove = false)
	{ m_dispatcher.setHook(hook,remove); }

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
     * Loads the plugins from an extra plugins directory
     * @param relPath Path to the extra directory, relative to the main modules
     * @return True if the directory could at least be opened
     */
    bool loadPluginDir(const String& relPath);

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
    bool loadPlugin(const char* file);

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
    static Engine* s_self;
    static String s_cfgpath;
    static String s_cfgsuffix;
    static String s_modpath;
    static String s_extramod;
    static String s_modsuffix;
    static int s_haltcode;
};

}; // namespace TelEngine

#endif /* __YATENGINE_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
