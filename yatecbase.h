/*
 * yatecbase.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Common base classes for all telephony clients
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef __YATECBASE_H
#define __YATECBASE_H

#ifndef __cplusplus
#error C++ is required
#endif

#include <yatephone.h>

/**
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

class Window;                            // A generic window
class UIWidget;                          // A custom widget
class UIFactory;                         // Base factory used to build custom widgets
class Client;                            // The client
class ClientChannel;                     // A client telephony channel
class ClientDriver;                      // The client telephony driver
class ClientLogic;                       // The client's default logic
class ClientAccount;                     // A client account
class ClientAccountList;                 // A client account list
class ClientContact;                     // A client contact
class ClientResource;                    // A client contact's resource
class DurationUpdate;                    // Class used to update UI durations
class ClientSound;                       // A sound file


/**
 * A window is the basic user interface element.
 * Everything inside is implementation specific functionality.
 * @short An abstract user interface window
 */
class YATE_API Window : public GenObject
{
    friend class Client;
public:
    /**
     * Constructor, creates a new windows with an ID
     * @param id String identifier of the new window
     */
    Window(const char* id = 0);

    /**
     * Destructor
     */
    virtual ~Window();

    /**
     * Retrive the standard name of this Window, used to search in lists
     * @return Identifier of this window
     */
    virtual const String& toString() const;

    /*
     * Get the window's title (may not be displayed on screen)
     * @return Title of this window
     */
    virtual void title(const String& text);

    /**
     * Set the contextual information previously associated with this window
     * @param text New contextual information
     */
    virtual void context(const String& text);

    /**
     * Set window parameters or widget contents
     * @param params List of parameters to set in the window and its widgets
     * @return True if all parameters could be set
     */
    virtual bool setParams(const NamedList& params);

    /**
     * Force this window on top of another one which becomes its parent
     * @param parent Window to force as parent of this one
     */
    virtual void setOver(const Window* parent) = 0;

    /**
     * Check if this window has an element by name
     * @param name Name of the element to search for
     * @return True if one element with the given name exists
     */
    virtual bool hasElement(const String& name) = 0;

    /**
     * Set an element as interactive in the window
     * @param name Name of the element
     * @param active True to make interactive, false to disallow interaction
     * @return True if the operation was successfull
     */
    virtual bool setActive(const String& name, bool active) = 0;

    /**
     * Set an element as receiving input in the window
     * @param name Name of the element
     * @param select Also select the content of the focused element
     * @return True if the operation was successfull
     */
    virtual bool setFocus(const String& name, bool select = false) = 0;

    /**
     * Set the visibility of an element in the window
     * @param name Name of the element
     * @param visible True to make element visible, false to hide it
     * @return True if the operation was successfull
     */
    virtual bool setShow(const String& name, bool visible) = 0;

    /**
     * Set the displayed text of an element in the window
     * @param name Name of the element
     * @param text Text value to set in the element
     * @param richText True if the text contains format data
     * @return True if the operation was successfull
     */
    virtual bool setText(const String& name, const String& text,
	bool richText = false) = 0;

    /**
     * Set the checked or toggled status of an element in the window
     * @param name Name of the element
     * @param checked True to make element checked or toggled
     * @return True if the operation was successfull
     */
    virtual bool setCheck(const String& name, bool checked) = 0;

    /**
     * Set the selection of an item in an element in the window
     * @param name Name of the element
     * @param item Name of the item that should be selected
     * @return True if the operation was successfull
     */
    virtual bool setSelect(const String& name, const String& item) = 0;

    /**
     * Flag an element as requiring immediate attention
     * @param name Name of the element
     * @param urgent True if the element requires immediate attention
     * @return True if the operation was successfull
     */
    virtual bool setUrgent(const String& name, bool urgent) = 0;

    /**
     * Check if an element has an item by its name
     * @param name Name of the element to search for
     * @param item Name of the item that should be searched
     * @return True if one item with the given name exists in the element
     */
    virtual bool hasOption(const String& name, const String& item) = 0;

    /**
     * Add an item to an element that supports such an operation (list)
     * @param name Name of the element
     * @param item Name of the item to add
     * @param atStart True to insert item on the first position, false to append
     * @param text Displayed text to associate with the item (not all lists support it)
     * @return True if the operation was successfull
     */
    virtual bool addOption(const String& name, const String& item, bool atStart = false,
	const String& text = String::empty()) = 0;

    /**
     * Get an element's items
     * @param name Name of the element to search for
     * @param items List to fill with element's items
     * @return True if the element exists
     */
    virtual bool getOptions(const String& name, NamedList* items) = 0;

    /**
     * Remove an item from an element (list)
     * @param name Name of the element
     * @param item Name of the item to remove
     * @return True if the operation was successfull
     */
    virtual bool delOption(const String& name, const String& item) = 0;

    /**
     * Append or insert text lines to a widget
     * @param name The name of the widget
     * @param lines List containing the lines
     * @param max The maximum number of lines allowed to be displayed. Set to 0 to ignore
     * @param atStart True to insert, false to append
     * @return True on success
     */
    virtual bool addLines(const String& name, const NamedList* lines, unsigned int max, 
	bool atStart = false);

    /**
     * Add a row to a table owned by this window
     * @param name Name of the element
     * @param item Name of the item to add
     * @param data Table's columns to set
     * @param atStart True to insert, false to append
     * @return True if the operation was successfull
     */
    virtual bool addTableRow(const String& name, const String& item,
	const NamedList* data = 0, bool atStart = false);

    /**
     * Delete a row from a table owned by this window
     * @param name Name of the element
     * @param item Name of the item to remove
     * @return True if the operation was successfull
     */
    virtual bool delTableRow(const String& name, const String& item);

    /**
     * Update a row from a table owned by this window
     * @param name Name of the element
     * @param item Name of the item to update
     * @param data Data to update
     * @return True if the operation was successfull
     */
    virtual bool setTableRow(const String& name, const String& item, const NamedList* data);

    /**
     * Set a table row or add a new one if not found
     * @param name Name of the element
     * @param item Table item to set/add
     * @param data Optional list of parameters used to set row data
     * @param atStart True to add item at start, false to add them to the end
     * @return True if the operation was successfull
     */
    virtual bool updateTableRow(const String& name, const String& item,
	const NamedList* data = 0, bool atStart = false);

    /**
     * Add or set one or more table row(s). Screen update is locked while changing the table.
     * Each data list element is a NamedPointer carrying a NamedList with item parameters.
     * The name of an element is the item to update.
     * Set element's value to boolean value 'true' to add a new item if not found
     *  or 'false' to set an existing one. Set it to empty string to delete the item
     * @param name Name of the table
     * @param data The list of items to add/set/delete
     * @param atStart True to add new items at start, false to add them to the end
     * @return True if the operation was successfull
     */
    virtual bool updateTableRows(const String& name, const NamedList* data,
	bool atStart = false);

    /**
     * Retrieve a row from a table owned by this window
     * @param name Name of the element
     * @param item Name of the item to retrieve
     * @param data List to fill with table's columns contents
     * @return True if the operation was successfull
     */
    virtual bool getTableRow(const String& name, const String& item, NamedList* data = 0);

    /**
     * Clear (delete all rows) a table owned by this window
     * @param name Name of the element
     * @return True if the operation was successfull
     */
    virtual bool clearTable(const String& name);

    /**
     * Get an element's text
     * @param name Name of the element
     * @param text The destination string
     * @param richText True to get the element's roch text if supported.
     * @return True if the operation was successfull
     */
    virtual bool getText(const String& name, String& text, bool richText = false) = 0;

    /**
     * Get the checked state of a checkable control
     * @param name Name of the element
     * @param checked The checked state of the control
     * @return True if the operation was successfull
     */
    virtual bool getCheck(const String& name, bool& checked) = 0;

    /**
     * Retrieve an element's selection
     * @param name Name of the element
     * @param item String to fill with selection's contents
     * @return True if the operation was successfull
     */
    virtual bool getSelect(const String& name, String& item) = 0;

    /**
     * Set a property for this window or for a widget owned by it
     * @param name Name of the element
     * @param item Property's name
     * @param value Property's value
     * @return True on success
     */
    virtual bool setProperty(const String& name, const String& item, const String& value)
	{ return false; }

    /**
     * Get a property from this window or from a widget owned by it
     * @param name Name of the element
     * @param item Property's name
     * @param value Property's value
     * @return True on success
     */
    virtual bool getProperty(const String& name, const String& item, String& value)
	{ return false; }

    /**
     * Populate the window if not already done
     */
    inline void populate() {
	    if (m_populated)
		return;
	    doPopulate();
	    m_populated = true;
	}

    /**
     * Initialize the window if not already done
     */
    inline void init() {
	    if (m_initialized)
		return;
	    doInit();
	    m_initialized = true;
	}

    /**
     * Show this window
     */
    virtual void show() = 0;

    /**
     * Hide this window
     */
    virtual void hide() = 0;

    /**
     * Resize this window
     * @param width The new width
     * @param height The new width
     */
    virtual void size(int width, int height) = 0;

    /**
     * Move this window
     * @param x The x coordinate of the upper left corner
     * @param y The y coordinate of the upper left corner
     */
    virtual void move(int x, int y) = 0;

    /**
     * Move this window related to its current position
     * @param dx The value to be added to the current x coordinate of the upper left corner
     * @param dy The value to be added to the current y coordinate of the upper left corner
     */
    virtual void moveRel(int dx, int dy) = 0;

    /**
     * Checkes if this window is related to the given window
     * @param wnd The window to check for any relation
     * @return False if wnd is this window or a master one
     */
    virtual bool related(const Window* wnd) const;

    virtual void menu(int x, int y) = 0;

    /**
     * Check if this window can be closed
     * @return True if this window can be closed, false to prevent hiding it
     */
    virtual bool canClose()
	{ return true; }

    /**
     * Retrive the standard name of this Window
     * @return Identifier of this window
     */
    inline const String& id() const
	{ return m_id; }

    /*
     * Get the window's title (may not be displayed on screen)
     * @return Title of this window
     */
    inline const String& title() const
	{ return m_title; }

    /**
     * Get the contextual information previously associated with this window
     * @return String contextual information
     */
    inline const String& context() const
	{ return m_context; }

    /**
     * Get the visibility status of this window
     * @return True if window is visible, false if it's hidden
     */
    inline bool visible() const
	{ return m_visible; }

    /**
     * Set the visibility status of this window
     * @param yes True if window should be visible
     */
    inline void visible(bool yes)
	{ if (yes) show(); else hide(); }

    /**
     * Check if this window is a master (topmost) window
     * @return True if this window is topmost
     */
    inline bool master() const
	{ return m_master; }

    /**
     * Check if this window is a popup window
     * @return True if this window is initially hidden
     */
    inline bool popup() const
	{ return m_popup; }

    /**
     * Check if a string is a parameter prefix handled by setParams().
     * Exact prefix match is not a valid one
     * @param prefix String to check
     * @return True if the given prefix is a valid one
     */
    static bool isValidParamPrefix(const String& prefix);

protected:
    virtual void doPopulate() = 0;
    virtual void doInit() = 0;

    String m_id;
    String m_title;
    String m_context;
    bool m_visible;
    bool m_master;
    bool m_popup;
    bool m_saveOnClose;                  // Save window's data when destroyed

private:
    bool m_populated;                    // Already populated flag
    bool m_initialized;                  // Already initialized flag
};

class YATE_API UIWidget : public String
{
public:
    /**
     * Constructor, creates a new widget
     * @param name The widget's name
     */
    inline UIWidget(const char* name = 0)
	: String(name)
	{}

    /**
     * Destructor
     */
    virtual ~UIWidget()
	{}

    /**
     * Retrive the standard name of this Window
     * @return Identifier of this window
     */
    inline const String& name() const
	{ return toString(); }

    /**
     * Set widget's parameters
     * @param params List of parameters
     * @return True if all parameters could be set
     */
    virtual bool setParams(const NamedList& params)
	{ return false; }

    /**
     * Get widget's items
     * @param items List to fill with widget's items
     * @return False on failure (e.g. not initialized)
     */
    virtual bool getOptions(NamedList& items)
	{ return false; }

    /**
     * Add a row to a table
     * @param item Name of the item to add
     * @param data Table's columns to set
     * @param atStart True to insert, false to append
     * @return True if the operation was successfull
     */
    virtual bool addTableRow(const String& item, const NamedList* data = 0,
	bool atStart = false)
	{ return false; }

    /**
     * Delete a row from a table
     * @param item Name of the item to remove
     * @return True if the operation was successfull
     */
    virtual bool delTableRow(const String& item)
	{ return false; }

    /**
     * Update a table's row
     * @param item Name of the item to update
     * @param data Data to update
     * @return True if the operation was successfull
     */
    virtual bool setTableRow(const String& item, const NamedList* data)
	{ return false; }

    /**
     * Retrieve a row from a table
     * @param item Name of the item to retrieve
     * @param data List to fill with table's columns contents
     * @return True if the operation was successfull
     */
    virtual bool getTableRow(const String& item, NamedList* data = 0)
	{ return false; }

    /**
     * Clear (delete all rows) a table
     * @return True if the operation was successfull
     */
    virtual bool clearTable()
	{ return false; }

    /**
     * Retrieve the widget's selection
     * @param item String to fill with selection's contents
     * @return True if the operation was successfull
     */
    virtual bool getSelect(String& item)
	{ return false; }
};

/**
 * Each instance of UIFactory creates special user interface elements by type.
 * Keeps a global list with all factories. The list doesn't own the facotries 
 * @short A static user interface creator
 */
class YATE_API UIFactory : public String
{
public:
    /**
     * Constructor. Append itself to the factories list
     */
    UIFactory(const char* name);

    /**
     * Destructor. Remove itself from list
     */
    virtual ~UIFactory();

    /**
     * Check if this factory can build an object of a given type
     * @param type Object type to check
     * @return True if this factory can build the object
     */
    inline bool canBuild(const String& type)
	{ return 0 != m_types.find(type); }

    /**
     * Ask this factory to create an object of a given type
     * @param type Object's type
     * @param name Object' name
     * @param params Optional object parameters
     * @return Valid pointer or 0 if failed to build it
     */
    virtual void* create(const String& type, const char* name, NamedList* params = 0) = 0;

    /**
     * Ask all factories to create an object of a given type
     * @param type Object's type
     * @param name Object' name
     * @param params Optional object parameters
     * @param factory Optional factory name used to create the requested object. If non 0,
     *  this will be the only factory asked to create the object
     * @return Valid pointer or 0 if failed to build it
     */
    static void* build(const String& type, const char* name, NamedList* params = 0,
	const char* factory = 0);

protected:
    ObjList m_types;                     // List of object types this factory can build

private:
    static ObjList s_factories;          // Registered factories list
};

/**
 * Singleton class that holds the User Interface's main thread and methods
 * @short Thread that runs the User Interface
 */
class YATE_API Client : public Thread, public MessageReceiver
{
    friend class Window;
    friend class ClientChannel;
    friend class ClientDriver;
    friend class ClientLogic;
public:
    /**
     * Message relays installed by this receiver.
     */
    enum MsgID {
	CallCdr            = 0,
	UiAction           = 1,
	UserLogin          = 2,
	UserNotify         = 3,
	ResourceNotify     = 4,
	ResourceSubscribe  = 5,
	XmppIq             = 7,
	ClientChanUpdate   = 8,
	// Handlers not automatically installed
	ChanNotify         = 9,
	// NOTE: Keep the MsgIdCount in sync: it can be used by other parties to install
	//  other relays
	MsgIdCount         = 10
    };

    /**
     * Client boolean options mapped to UI toggles
     */
    enum ClientToggle {
	OptMultiLines             = 0,   // Accept incoming calls
	OptAutoAnswer             = 1,   // Auto answer incoming calls
	OptRingIn                 = 2,   // Enable/disable incoming ringer
	OptRingOut                = 3,   // Enable/disable outgoing ringer
	OptActivateLastOutCall    = 4,   // Set the last outgoing call active
	OptActivateLastInCall     = 5,   // Set the last incoming call active
	OptActivateCallOnSelect   = 6,   // Set the active call when selected in channel list (don't require double click)
	OptKeypadVisible          = 7,   // Show/hide keypad
	OptCount                  = 8
    };

    /**
     * Known voip protocols
     */
    enum Protocol {
	SIP           = 0,
	JABBER        = 1,
	H323          = 2,
	IAX           = 3,
	OtherProtocol = 4
    };

    /**
     * Constructor
     * @param name The client's name
     */
    Client(const char *name = 0);

    /**
     * Destructor
     */
    virtual ~Client();

    /**
     * Run the client's thread
     */
    virtual void run();

    /**
     * Cleanup when thread terminates
     */
    virtual void cleanup();

    /**
     * Execute the client
     */
    virtual void main() = 0;

    /**
     * Lock the client
     */
    virtual void lock() = 0;

    /**
     * Unlock the client
     */
    virtual void unlock() = 0;

    /**
     * Lock the client only if we are using more then 1 thread
     */
    inline void lockOther()
	{ if (!m_oneThread) lock(); }

    /**
     * Unlock the client only if we are using more then 1 thread
     */
    inline void unlockOther()
	{ if (!m_oneThread) unlock(); }

    /**
     * Handle all windows closed event from UI
     */
    virtual void allHidden() = 0;

    /**
     * Load windows and optionally (re)initialize the client's options.
     * @param file The resource file describing the windows. Set to 0 to use the default one
     * @param init True to (re)initialize the client
     */
    void loadUI(const char* file = 0, bool init = true);

    /**
     * Terminate application
     */
    virtual void quit() = 0;

    /**
     * Process a received message. Check for a logic to process it
     * @param msg Received message
     * @param id Message id
     * @return True if a logic processed the message (stop dispatching it)
     */
    virtual bool received(Message& msg, int id);

    /**
     * Create a window with a given name
     * @param name The window's name
     * @param alias Window name alias after succesfully loaded.
     *  Set to empty string to use the given name
     * @return True on success
     */
    virtual bool createWindowSafe(const String& name,
	const String& alias = String::empty());

    /**
     * Ask to an UI factory to create an object in the UI's thread
     * @param dest Destination to be filled with the newly create object's address
     * @param type Object's type
     * @param name Object's name
     * @param params Optional object parameters
     * @return True on success
     */
    virtual bool createObject(void** dest, const String& type, const char* name,
	NamedList* params = 0);

    /**
     * Hide/destroy a window with a given name
     * @param name The window's name
     * @param hide True to hide, false to close
     * @return True on success
     */
    virtual bool closeWindow(const String& name, bool hide = true);

    /**
     * Install/uninstall a debugger output hook
     * @param active True to install, false to uninstall the hook
     * @return True on success
     */
    virtual bool debugHook(bool active);

    /**
     * Add a log line
     * @param text Text to add
     * @return True on success
     */
    virtual bool addToLog(const String& text);

    /**
     * Set the status text
     * @param text Status text
     * @param wnd Optional window owning the status control
     * @return True on success
     */
    virtual bool setStatus(const String& text, Window* wnd = 0);

    /**
     * Set the status text safely
     * @param text Status text
     * @param wnd Optional window owning the status control
     * @return True on success
     */
    bool setStatusLocked(const String& text, Window* wnd = 0);

    /**
     * Set multiple window parameters
     * @param params The parameter list
     * @param wnd Optional window whose params are to be set
     * @param skip Optional window to skip if wnd is 0 
     * @return True on success
     */
    bool setParams(const NamedList* params, Window* wnd = 0, Window* skip = 0);

    /**
     * Handle actions from user interface. Enqueue an ui.event message if
     *  the action is not handled by a client logic
     * @param wnd The window in which the user did something
     * @param name The action's name
     * @param params Optional action parameters
     * @return True if the action was handled by a client logic
     */
    virtual bool action(Window* wnd, const String& name, NamedList* params = 0);

    /**
     * Handle actions from checkable widgets. Enqueue an ui.event message if
     *  the action is not handled by a client logic
     * @param wnd The window in which the user did something
     * @param name The object's name
     * @param active Object's state
     * @return True if the action was handled by a client logic
     */
    virtual bool toggle(Window* wnd, const String& name, bool active);

    /**
     * Handle 'select' actions from user interface. Enqueue an ui.event message if
     *  the action is not handled by a client logic
     * @param wnd The window in which the user selected the object
     * @param name The action's name
     * @param item Item identifying the selection
     * @param text Selection's text
     * @return True if the action was handled by a client logic
     */
    virtual bool select(Window* wnd, const String& name, const String& item, const String& text = String::empty());

    /**
     * Check if the client is using more then 1 thread
     * @return True if the client is using more then 1 thread
     */
    inline bool oneThread() const
	{ return m_oneThread; }

    /**
     * Get the currently selected line
     * @return The selected line
     */
    inline int line() const
	{ return m_line; }

    /**
     * Set the selected line
     * @param newLine The selected line
     */
    void line(int newLine);

    bool hasElement(const String& name, Window* wnd = 0, Window* skip = 0);
    bool setActive(const String& name, bool active, Window* wnd = 0, Window* skip = 0);
    bool setFocus(const String& name, bool select = false, Window* wnd = 0, Window* skip = 0);
    bool setShow(const String& name, bool visible, Window* wnd = 0, Window* skip = 0);
    bool setText(const String& name, const String& text, bool richText = false, 
	Window* wnd = 0, Window* skip = 0);
    bool setCheck(const String& name, bool checked, Window* wnd = 0, Window* skip = 0);
    bool setSelect(const String& name, const String& item, Window* wnd = 0, Window* skip = 0);
    bool setUrgent(const String& name, bool urgent, Window* wnd = 0, Window* skip = 0);
    bool hasOption(const String& name, const String& item, Window* wnd = 0, Window* skip = 0);

    /**
     * Get an element's items
     * @param name Name of the element to search for
     * @param items List to fill with element's items
     * @param wnd Optional window owning the element
     * @param skip Optional window to skip when searching for the element
     * @return True if the element exists
     */
    virtual bool getOptions(const String& name, NamedList* items,
	Window* wnd = 0, Window* skip = 0);

    bool addOption(const String& name, const String& item, bool atStart,
	const String& text = String::empty(), Window* wnd = 0, Window* skip = 0);
    bool delOption(const String& name, const String& item, Window* wnd = 0, Window* skip = 0);

    /**
     * Append or insert text lines to a widget
     * @param name The name of the widget
     * @param lines List containing the lines
     * @param max The maximum number of lines allowed to be displayed. Set to 0 to ignore
     * @param atStart True to insert, false to append
     * @param wnd Optional window owning the widget
     * @param skip Optional window to skip if wnd is 0
     * @return True on success
     */
    virtual bool addLines(const String& name, const NamedList* lines, unsigned int max, 
	bool atStart = false, Window* wnd = 0, Window* skip = 0);

    bool addTableRow(const String& name, const String& item, const NamedList* data = 0,
	bool atStart = false, Window* wnd = 0, Window* skip = 0);
    bool delTableRow(const String& name, const String& item, Window* wnd = 0, Window* skip = 0);
    bool setTableRow(const String& name, const String& item, const NamedList* data,
	Window* wnd = 0, Window* skip = 0);
    bool getTableRow(const String& name, const String& item, NamedList* data = 0,
	Window* wnd = 0, Window* skip = 0);
    bool clearTable(const String& name, Window* wnd = 0, Window* skip = 0);

    /**
     * Set a table row or add a new one if not found
     * @param name Name of the element
     * @param item Table item to set/add
     * @param data Optional list of parameters used to set row data
     * @param atStart True to add item at start, false to add them to the end
     * @param wnd Optional window owning the element
     * @param skip Optional window to skip if wnd is 0
     * @return True if the operation was successfull
     */
    bool updateTableRow(const String& name, const String& item, const NamedList* data = 0,
	bool atStart = false, Window* wnd = 0, Window* skip = 0);

    /**
     * Add or set one or more table row(s). Screen update is locked while changing the table.
     * Each data list element is a NamedPointer carrying a NamedList with item parameters.
     * The name of an element is the item to update.
     * Set element's value to boolean value 'true' to add a new item if not found
     *  or 'false' to set an existing one. Set it to empty string to delete the item
     * @param name Name of the table
     * @param data The list of items to add/set/delete
     * @param atStart True to add new items at start, false to add them to the end
     * @param wnd Optional window owning the element
     * @param skip Optional window to skip if wnd is 0
     * @return True if the operation was successfull
     */
    bool updateTableRows(const String& name, const NamedList* data, bool atStart = false,
	Window* wnd = 0, Window* skip = 0);

    /**
     * Get an element's text
     * @param name Name of the element
     * @param text The destination string
     * @param richText True to get the element's roch text if supported.
     * @param wnd Optional window owning the element
     * @param skip Optional window to skip if wnd is 0
     * @return True if the operation was successfull
     */
    bool getText(const String& name, String& text, bool richText = false, Window* wnd = 0, Window* skip = 0);

    bool getCheck(const String& name, bool& checked, Window* wnd = 0, Window* skip = 0);
    bool getSelect(const String& name, String& item, Window* wnd = 0, Window* skip = 0);

    /**
     * Set a property
     * @param name Name of the element
     * @param item Property's name
     * @param value Property's value
     * @param wnd Optional target window
     * @param skip Optional window to skip if wnd is 0
     * @return True on success
     */
    virtual bool setProperty(const String& name, const String& item, const String& value,
	Window* wnd = 0, Window* skip = 0);

    /**
     * Get a property
     * @param name Name of the element
     * @param item Property's name
     * @param value Property's value
     * @param wnd Optional target window
     * @param skip Optional window to skip if wnd is 0
     * @return True on success
     */
    virtual bool getProperty(const String& name, const String& item, String& value,
	Window* wnd = 0, Window* skip = 0);

    void moveRelated(const Window* wnd, int dx, int dy);
    inline bool initialized() const
	{ return m_initialized; }
    inline static Client* self()
	{ return s_client; }
    inline static bool changing()
	{ return (s_changing > 0); }
    static Window* getWindow(const String& name);
    static bool setVisible(const String& name, bool show = true);
    static bool getVisible(const String& name);
    static bool openPopup(const String& name, const NamedList* params = 0, const Window* parent = 0);
    static bool openMessage(const char* text, const Window* parent = 0, const char* context = 0);
    static bool openConfirm(const char* text, const Window* parent = 0, const char* context = 0);
    static ObjList* listWindows();
    void idleActions();

    /**
     * Show a file open dialog window
     * This method isn't using the proxy thread since it's usually called on UI action
     * @param parent Dialog window's parent
     * @param params Dialog window's params. Parameters that can be specified include 'caption',
     *  'dir', 'filters', 'selectedfilter', 'confirmoverwrite', 'choosedir'.
     *  The parameter 'filters' may be a pipe ('|') separated list of filters
     * @param files List of selected file(s). Allow multiple file selection if non 0
     * @param file The selected file if multiple file selection is disabled
     * @return True on success
     */
    virtual bool chooseFile(Window* parent, const NamedList& params,
	NamedList* files, String* file)
	{ return false; }

    /**
     * Request to a logic to set a client's parameter. Save the settings file
     *  and/or update interface
     * @param param Parameter's name
     * @param value The value of the parameter
     * @param save True to save the configuration file
     * @param update True to update the interface
     * @return True on success, false if the parameter doesn't exist, the value 
     *  is incorrect or failed to save the file
     */
    virtual bool setClientParam(const String& param, const String& value,
	bool save, bool update);

    /**
     * Called when the user pressed the backspace key
     * @param name The active widget (it might be the window itself)
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool backspace(const String& name, Window* wnd = 0);

    /**
     * Create and install a message relay owned by this client.
     * The new relay will be unistalled when the client is terminated
     * @param name Message name
     * @param id Relay id
     * @param prio Message priority
     */
    void installRelay(const char* name, int id, int prio);

    /**
     * Call routing handler called by the driver
     * @param msg The call.route message
     */
    virtual bool callRouting(Message& msg)
	{ return true;}

    /**
     * IM message routing handler called by the driver
     * @param msg The im.route message
     */
    virtual bool imRouting(Message& msg)
	{ return true;}

    /**
     * Process an IM message
     * @param msg The im.execute of chan.text message
     */
    virtual bool imExecute(Message& msg);

    /**
     * Build an incoming channel.
     * Answer it if succesfully connected and auto answer is set.
     * Reject it if multiline is false and the driver is busy.
     * Set the active one if requested by config and there is no active channel.
     * Start the ringer if there is no active channel
     * @param msg The call.execute message
     * @param dest The destination (target)
     * @return True if a channel was created and connected
     */
    virtual bool buildIncomingChannel(Message& msg, const String& dest);

    /**
     * Build an outgoing channel
     * @param params Call parameters
     * @return True if a channel was created its router started
     */
    virtual bool buildOutgoingChannel(NamedList& params);

    /**
     * Call execute handler called by the driver.
     * Ask the logics to create the channel
     * @param msg The call.execute message
     * @param dest The destination (target)
     * @return True if a channel was created and connected
     */
    bool callIncoming(Message& msg, const String& dest);

    /**
     * Answer an incoming call
     * @param id The accepted channel's id
     * @return True on success
     */
    void callAnswer(const String& id);

    /**
     * Terminate a call
     * @param id The channel's id
     * @param reason Optional termination reason
     * @param error Optional termination error
     * @return True on success
     */
    void callTerminate(const String& id, const char* reason = 0, const char* error = 0);

    /**
     * Get the active channel if any
     * @return Referenced pointer to the active channel or 0
     */
    ClientChannel* getActiveChannel();

    /**
     * Start/stop ringer. The ringer is started only if not disabled
     * @param in True if the request is for the incoming call alert, false if it
     *  is for the outgoing call ringing alert
     * @param on True to start, false to stop the sound
     * @return True on success
     */
    virtual bool ringer(bool in, bool on);

    /**
     * Create a sound object. Append it to the global list
     * @param name The name of sound object
     * @param file The file to play (should contain the whole path and the file name)
     * @param device Optional device used to play the file. Set to 0 to use the default one
     * @return True on success, false if a sound with the given name already exists
     */
    virtual bool createSound(const char* name, const char* file, const char* device = 0)
	{ return false; }

    /**
     * Send digits on selected channel
     * @param digits The digits to send
     * @param id The channel id. Use the active one if empty
     * @return True on success
     */
    bool emitDigits(const char* digits, const String& id = String::empty());

    /**
     * Send a digit on selected channel
     * @param digit The digit to send
     * @param id The channel id. Use the active one if empty
     * @return True on success
     */
    inline bool emitDigit(char digit, const String& id = String::empty()) {
	    char s[2] = {digit,0}; 
	    return emitDigits(s,id);
	}

    /**
     * Get a boolean option of this client
     * @param toggle Options's id to retrieve
     * @return True on success
     */
    inline bool getBoolOpt(ClientToggle toggle)
	{ return toggle < OptCount ? m_toggles[toggle] : false; }

    /**
     * Set a boolean option of this client
     * @param toggle Options's id to set
     * @param value Value to set
     * @param updateUi True to update UI
     * @return True if the option's value changed
     */
    bool setBoolOpt(ClientToggle toggle, bool value, bool updateUi = false);

    /**
     * Build a date/time string from UTC time
     * @param dest Destination string
     * @param secs Seconds since EPOCH
     * @param format Format string used to build the destination
     * @param utc True to build UTC time instead of local time
     * @return True on success
     */
    virtual bool formatDateTime(String& dest, unsigned int secs, const char* format,
	bool utc = false)
	{ return false; }

    /**
     * Engine start notification. Notify all registered logics
     * @param msg The engine.start message
     */
    virtual void engineStart(Message& msg);

    /**
     * Check if the client is exiting
     * @return True if the client therad is exiting
     */
    static inline bool exiting()
	{ return s_exiting; }

    /**
     * Add a logic to the list. The added object is not owned by the client  
     * @param logic Pointer to the logic to add
     * @return True on success. False if the pointer is 0 or already added
     */
    static bool addLogic(ClientLogic* logic);

    /**
     * Remove a logic from the list without destroying it
     * @param logic Pointer to the logic to remove
     */
    static void removeLogic(ClientLogic* logic);

    /**
     * Convenience method to retrieve a logic
     * @param name The logic's name
     * @return ClientLogic pointer or 0
     */
    static ClientLogic* findLogic(const String& name);

    /**
     * Build an 'ui.event' message
     * @param event Event's name
     * @param wnd Optional window to add to message
     * @param name Optional 'name' parameter value
     * @param params Other optional parameters to be added to the message
     * @return Valid Message pointer
     */
    static Message* eventMessage(const String& event, Window* wnd = 0,
	const char* name = 0, NamedList* params = 0);

    /**
     * Save a configuration file. Call openMessage() on failure
     * @param cfg The configuration file to save
     * @param parent The parent of the error window if needded
     * @param showErr True to open a message popup on failure
     * @return True on success
     */
    static bool save(Configuration& cfg, Window* parent = 0, bool showErr = true);

    /**
     * Check if a string names a client's boolean option
     * @param name String to check
     * @return Valid client option index or OptCount if not found
     */
    static ClientToggle getBoolOpt(const String& name);

    /**
     * Set the flag indicating that the client should tick the logics
     */
    static inline void setLogicsTick()
	{ s_idleLogicsTick = true; }

    /**
     * Get the protocol from a string
     */
    static inline Protocol getProtocol(const String& proto) {
	    for (int i = 0; i < OtherProtocol; i++)
		if (proto == s_protocols[i])
		    return (Protocol)i;
	    return OtherProtocol;
	}

    /**
     * Get the protocol name from an integer value
     */
    static inline const String& getProtocol(int proto)
	{ return proto < OtherProtocol ? s_protocols[proto] : String::empty(); }

    static Configuration s_settings;     // Client settings
    static Configuration s_actions;      // Logic preferrences
    static Configuration s_accounts;     // Accounts
    static Configuration s_contacts;     // Contacts
    static Configuration s_providers;    // Provider settings
    static Configuration s_history;      // Call log
    static Configuration s_calltoHistory;  // Dialed destinations history
    // Holds a not selected/set value match
    static Regexp s_notSelected;
    // Parameters that are applied from provider template
    static const char* s_provParams[];
    // Account options string list
    static ObjList s_accOptions;
    // The list of protocols supported by the client
    static String s_protocols[OtherProtocol];
    // Paths
    static String s_skinPath;
    static String s_soundPath;
    // Ring name for incoming channels
    static String s_ringInName;
    // Ring name for outgoing channels
    static String s_ringOutName;
    // Status widget's name
    static String s_statusWidget;
    // Widget displaying the debug text
    static String s_debugWidget;
    // The list of cient's toggles
    static String s_toggles[OptCount];

protected:
    virtual bool createWindow(const String& name,
	const String& alias = String::empty()) = 0;
    virtual void loadWindows(const char* file = 0) = 0;
    virtual void initWindows();
    virtual void initClient();
    virtual void exitClient()
	{}
    inline bool needProxy() const
	{ return m_oneThread && !isCurrent(); }
    bool driverLockLoop();
    static bool driverLock(long maxwait = 0);
    static void driverUnlock();

    static bool s_exiting;               // Exiting flag

    ObjList m_windows;
    bool m_initialized;
    int m_line;
    bool m_oneThread;
    bool m_toggles[OptCount];
    ObjList m_relays;                    // Message relays installed by this receiver
    static Client* s_client;
    static int s_changing;
    static ObjList s_logics;
    static bool s_idleLogicsTick;        // Call logics' timerTick()
};

/**
 * This class implements a Channel used by client programs
 * @short Channel used by client programs
 */
class YATE_API ClientChannel : public Channel
{
    friend class ClientDriver;
public:
    /**
     * Channel notifications
     */
    enum Notification {
	Startup,
	Destroyed,
	Active,
	OnHold,
	Noticed,
	AddrChanged,
	Routed,
	Accepted,
	Rejected,
	Progressing,
	Ringing,
	Answered,
	Transfer,
	Conference,
	Unknown
    };

    // Incoming (from engine) consructor
    ClientChannel(const Message& msg, const String& peerid);
    // Outgoing (to engine) constructor
    ClientChannel(const String& target, const NamedList& params);
    virtual ~ClientChannel();
    virtual bool msgProgress(Message& msg);
    virtual bool msgRinging(Message& msg);
    virtual bool msgAnswered(Message& msg);
    virtual bool msgDrop(Message& msg, const char* reason);
    virtual bool callRouted(Message& msg);
    virtual void callAccept(Message& msg);
    virtual void callRejected(const char* error, const char* reason, const Message* msg);

    /**
     * Answer an incoming call. Set media channels. Enqueue a clientchan.update message
     */
    void callAnswer();

    /**
     * Get the remote party of this channel
     * @return The remote party of this channel
     */
    inline const String& party() const
	{ return m_party; }

    /**
     * Check if this channel is in conference
     * @return True if this channel is in conference
     */
    inline bool conference() const
	{ return m_conference; }

    /**
     * Get the transferred peer's id
     * @return The transferred peer's id
     */
    inline const String& transferId() const
	{ return m_transferId; }

    /**
     * Get the client data
     * @return RefObject pointer or 0
     */
    inline RefObject* clientData() const
	{ return m_clientData; }

    /**
     * Set/reset the client data.
     * If a new client data is set its reference counter is increased
     * @param obj The new client data
     */
    inline void setClientData(RefObject* obj = 0) {
	    TelEngine::destruct(m_clientData);
	    if (obj && obj->ref())
		m_clientData = obj;
	}

    /**
     * Attach/detach media channels
     * @param open True to open, false to close
     * @param replace True to replace media if already open. Ignored if open is false
     * @return True on success
     */
    bool setMedia(bool open = false, bool replace = false);

    /**
     * Set/reset this channel's data source/consumer
     * @param active True to set active, false to set inactive (mute)
     * @param update True to enqueue an update message
     * @return True on success
     */
    bool setActive(bool active, bool update = true);

    /**
     * Set/reset the transferred peer's id. Enqueue clientchan.update if changed.
     * Open media when reset if the channel is active and answered
     * @param target The transferred peer's id. Leave it blank to reset
     */
    void setTransfer(const String& target = String::empty());

    /**
     * Set/reset the conference data. Enqueue clientchan.update if changed.
     * Open media when reset if the channel is active and answered
     * @param target The confeernce room's name. Leave it blank to reset
     */
    void setConference(const String& target = String::empty());

    /**
     * Get the peer consumer's data format
     * @return The peer consumer's data format
     */
    inline const String& peerOutFormat() const
	{ return m_peerOutFormat; }

    /**
     * Get the peer source's data format
     * @return The peer source's data format
     */
    inline const String& peerInFormat() const
	{ return m_peerInFormat; }

    /**
     * Check if this channel is the active one
     * @return True if this channel is the active one
     */
    inline bool active() const
	{ return m_active; }

    /**
     * Check if this channel was noticed
     * @return True if this channel was noticed
     */
    inline bool isNoticed() const
	{ return m_noticed; }

    /**
     * Notice this channel. Enqueue a clientchan.update message
     */
    void noticed();

    /**
     * Get this channel's line
     * @return This channel's line
     */
    inline int line() const
	{ return m_line; }

    /**
     * Set this channel's line
     * @param newLine This channel's line
     */
    void line(int newLine);

    /**
     * Update channel. Enqueue a clientchan.update message with the given operation.
     * Enqueue other channel status messages if required
     * @param notif The value of the notify parameter
     * @param chan Set the channel as message's user data
     * @param updatePeer True to update peer's data formats
     * @param engineMsg Optional message to enqueue in the engine
     * @param minimal Set to true to fill in only a minimum of engine message's parameters
     * @param data Set the channel as engine message's user data
     */
    void update(int notif, bool chan = true,
	bool updatePeer = true, const char* engineMsg = 0,
	bool minimal = false, bool data = false);

    /**
     * Lookup for a notification id
     * @param notif The notification's name
     * @param def Default value to return if not found
     * @return The result
     */
    static int lookup(const char* notif, int def = Unknown)
	{ return TelEngine::lookup(notif,s_notification,def); }

    /**
     * Lookup for a notification name
     * @param notif The notification's id
     * @param def Default value to return if not found
     * @return The result
     */
    static const char* lookup(int notif, const char* def = 0)
	{ return TelEngine::lookup(notif,s_notification,def); }

    /**
     * Channel notifications dictionary
     */
    static TokenDict s_notification[];

protected:
    virtual void destroyed();
    virtual void disconnected(bool final, const char* reason);
    // Check for a source in channel's peer or a received message's user data
    inline bool peerHasSource(Message& msg) {
    	    CallEndpoint* ch = getPeer();
	    if (!ch)
		ch = static_cast<CallEndpoint*>(msg.userObject("CallEndpoint"));
	    return ch && ch->getSource();
	}
    // Check if our consumer's source sent any data
    // Don't set the silence flag is already reset
    void checkSilence();

    String m_party;                      // Remote party
    String m_peerOutFormat;              // Peer consumer's data format
    String m_peerInFormat;               // Peer source's data format
    String m_reason;                     // Termination reason
    String m_peerId;                     // Peer's id (used to re-connect)
    bool m_noticed;                      // Incoming channel noticed flag
    int m_line;                          // Channel's line (address)
    bool m_active;                       // Channel active flag
    bool m_silence;                      // True if the peer did't sent us any audio data
    bool m_conference;                   // True if this channel is in conference
    String m_transferId;                 // Transferred id or empty if not transferred
    RefObject* m_clientData;             // Obscure data used by client logics
};

/**
 * Abstract client Driver that implements some of the specific functionality
 * @short Base Driver with client specific functions
 */
class YATE_API ClientDriver : public Driver
{
    friend class ClientChannel;          // Reset active channel's id
public:
    ClientDriver();
    virtual ~ClientDriver();
    virtual void initialize() = 0;
    virtual bool msgExecute(Message& msg, String& dest);
    virtual void msgTimer(Message& msg);
    virtual bool msgRoute(Message& msg);
    virtual bool received(Message& msg, int id);

    /**
     * Get the active channel's id
     * @return The active channel's id
     */
    inline const String& activeId() const
	{ return m_activeId; }

    /**
     * Set/reset the active channel.
     * Does nothing if the selected channel is the active one.
     * Put the active channel on hold before trying to set the active channel
     * @param id The new active channel's id. Set to empty if don't want
     *  to set a new active channel
     * @return True on success
     */
    bool setActive(const String& id = String::empty());

    /**
     * Find a channel by its line
     * @param line The line to find
     * @return ClientChannel pointer of 0
     */
    ClientChannel* findLine(int line);

    /**
     * Get the global client driver object's address
     * @return The global client driver object's address
     */
    inline static ClientDriver* self()
	{ return s_driver; }

    /**
     * Get the current audio device's name
     * @return The current audio device's name
     */
    inline static const String& device()
	{ return s_device; }

    /**
     * Drop all calls belonging to the active driver
     * @param reason Optional drop reason
     */
    static void dropCalls(const char* reason = 0);

    /**
     * Attach/detach client channels peers' source/consumer
     * @param id The id of the channel to tranfer
     * @param target The transfer target. Leave blank to reset the channel's transfer id
     * @return True on success
     */
    static bool setAudioTransfer(const String& id, const String& target = String::empty());

    /**
     * Attach/detach a client channel to/from a conference room
     * @param id The id of the channel to process
     * @param in True to enter the conference room, false to exit from it
     * @param confName Optional id of the conference. Set to 0 to use the default one
     *  Ignored if 'in' is false
     * @return True on success
     */
    static bool setConference(const String& id, bool in, const String* confName = 0);

    /**
     * Get a referenced channel found by its id
     * @param id The id of the channel to find
     * @return Referenced ClientChannel pointer or 0
     */
    static ClientChannel* findChan(const String& id);

    /**
     * Get a referenced channel whose stored peer is the given one
     * @param peer Peer id to check
     * @return Referenced ClientChannel pointer or 0
     */
    static ClientChannel* findChanByPeer(const String& peer);

    /**
     * Get the active channel
     * @return Referenced ClientChannel pointer or 0
     */
    static ClientChannel* findActiveChan()
	{ return self() ? findChan(self()->activeId()) : 0; }

    /**
     * The name to use when the client is in conference
     */
    static String s_confName;

    /**
     * Indicates wether a channel should drop its former peer when
     *  terminated while in conference
     */
    static bool s_dropConfPeer;

protected:
    void setup();
    static ClientDriver* s_driver;
    static String s_device;
    String m_activeId;                   // The active channel's id
};

/**
 * This class implements the logic behind different actions in the client.
 * It specifies the way the graphical interface of the client will behave
 *  in different circumstances.
 * @short Base client functionality
 */
class YATE_API ClientLogic : public GenObject
{
    friend class Client;
public:
    /**
     * Constructor. Append itself to the client's list
     */
    ClientLogic();

    /**
     * Constructor. Append itself to the client's list
     * @param name The name of this logic (module)
     * @param priority The priority of this logic
     */
    ClientLogic(const char* name, int priority);

    /**
     * Destructor. Remove itself from the client's list
     */
    virtual ~ClientLogic();

    /**
     * Function that returns the name of the logic
     * @return The name of this client logic
     */
    virtual const String& toString() const;

    /**
     * Get the priority of this logic
     * @return This logic's priority
     */
    inline int priority() const
	{ return m_prio; }

    /**
     * Process a request to set client parameters
     * @param params The parameter list
     * @return True on success
     */
    bool setParams(const NamedList& params);

    /**
     * Handle actions from user interface
     * @param wnd The window in which the user did something
     * @param name The action's name
     * @param params Optional action parameters
     * @return True if the action was handled
     */
    virtual bool action(Window* wnd, const String& name, NamedList* params = 0);

    /**
     * Handle actions from checkable widgets
     * @param wnd The window in which the user did something
     * @param name The object's name
     * @param active Object's state
     * @return True if the action was handled by a client logic
     */
    virtual bool toggle(Window* wnd, const String& name, bool active);

    /**
     * Handle 'select' actions from user interface
     * @param wnd The window in which the user did something
     * @param name The object's name
     * @param item Item identifying the selection
     * @param text Selection's text
     * @return True if the action was handled
     */
    virtual bool select(Window* wnd, const String& name, const String& item,
	const String& text = String::empty());

    /**
     * Set a client's parameter. Save the settings file and/or update interface
     * @param param Parameter's name
     * @param value The value of the parameter
     * @param save True to save the configuration file
     * @param update True to update the interface
     * @return True on success
     */
    virtual bool setClientParam(const String& param, const String& value,
	bool save, bool update);

    /**
     * Process an IM message
     * @param msg The im.execute of chan.text message
     */
    virtual bool imIncoming(Message& msg)
	{ return false; }

    /**
     * Call execute handler called by the client.
     * The default logic ask the client to build an incoming channel
     * @param msg The call.execute message
     * @param dest The destination (target)
     * @return True if a channel was created and connected
     */
    virtual bool callIncoming(Message& msg, const String& dest)
	{ return Client::self() && Client::self()->buildIncomingChannel(msg,dest); }

    /**
     * Called when the user trigger a call start action
     * The default logic fill the parameter list and ask the client to create an outgoing channel
     * @param params List of call parameters
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool callStart(NamedList& params, Window* wnd = 0);

    /**
     * Called when a digit is pressed. The default logic will send the digit(s)
     *  as DTMFs on the active channel
     * @param params List of parameters. It should contain a 'digits' parameter
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool digitPressed(NamedList& params, Window* wnd = 0);

    /**
     * Called when the user selected a line
     * @param name The line name
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool line(const String& name, Window* wnd = 0);

    /**
     * Show/hide widget(s) or window(s) on 'display'/'show' action
     * @param params Widget(s) or window(s) to show/hide
     * @param widget True if the operation indicates widget(s), false otherwise
     * @param wnd Optional window owning the action sender
     * @return False if failed to show/hide all widgets/windows
     */
    virtual bool display(NamedList& params, bool widget, Window* wnd = 0);

    /**
     * Called when the user pressed the backspace key.
     * The default behaviour is to erase the last digit from the "callto" box 
     * @param name The active widget (it might be the window itself)
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool backspace(const String& name, Window* wnd = 0);

    /**
     * Called when the user pressed a command action.
     * The default behaviour is to enqueue an engine.command message
     * @param name The command name
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool command(const String& name, Window* wnd = 0);

    /**
     * Called when the user changed the toggled state of a "debug:" widget.
     * The default behaviour is to enqueue an engine.debug message
     * @param name The debug action content (following the prefix)
     * @param active The widget's state
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool debug(const String& name, bool active, Window* wnd = 0);

    /**
     * Called when the user wants to add a new account or edit an existing one
     * @param newAcc True to add a new account, false to edit an exisiting one
     * @param params Initial parameters
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool editAccount(bool newAcc, NamedList* params, Window* wnd = 0);

    /**
     * Called when the user wants to save account data
     * @param params Initial parameters
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool acceptAccount(NamedList* params, Window* wnd = 0);

    /**
     * Called when the user wants to delete an existing account
     * @param account The account's name. Set to empty to delete the current selection
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool delAccount(const String& account, Window* wnd = 0);

    /**
     * Add/set an account. Login if required
     * @param account The account's parameters. The name of the list must be the account's name
     * @param login True to login the account
     * @param save True to save the accounts file. If true and file save fails the method will fail
     * @return True on success
     */
    virtual bool updateAccount(const NamedList& account, bool login, bool save);

    /**
     * Login/logout an account
     * @param account The account's parameters. The name of the list must be the account's name
     * @param login True to login the account, false to logout
     * @return True on success
     */
    virtual bool loginAccount(const NamedList& account, bool login);

    /**
     * Add/set a contact
     * @param contact The contact's parameters. The name of the list must be the contacts's id (name).
     *  If it starts with 'client/' this is a contact updated from server: it can't be changed
     * @param save True to save data to contacts file
     * @param update True to update the interface
     * @return True on success
     */
    virtual bool updateContact(const NamedList& contact, bool save, bool update);

    /**
     * Called when the user wants to save account data
     * @param params Initial parameters
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool acceptContact(NamedList* params, Window* wnd = 0);

    /**
     * Called when the user wants to add a new contact or edit an existing one
     * @param newCont True to add a new contact, false to edit an existing one
     * @param params Optional initial parameters
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool editContact(bool newCont, NamedList* params = 0, Window* wnd = 0);

    /**
     * Called when the user wants to delete an existing contact
     * @param contact The contact's id. Set to empty to delete the current selection
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool delContact(const String& contact, Window* wnd = 0);

    /**
     * Called when the user wants to call an existing contact
     * @param params Optional parameters
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool callContact(NamedList* params = 0, Window* wnd = 0);

    /**
     * Add/set account providers data
     * @param provider The provider's parameters. The name of the list must be the provider's id (name)
     * @param save True to save data to providers file
     * @param update True to update the interface
     * @return True on success
     */
    virtual bool updateProviders(const NamedList& provider, bool save, bool update);

    /**
     * Update the call log history
     * @param params Call log data
     * @param save True to save data to history file
     * @param update True to update the interface
     * @return True
    */
    virtual bool callLogUpdate(NamedList& params, bool save, bool update);

    /**
     * Clear the specified log and the entries from the history file and save the history file
     * @param table Tebale to clear
     * @param direction The call direction to clear (incoming,outgoing).
     *  Note that the direction is the value saved from call.cdr messages.
     *  If empty, all log entries will be cleared
     * @return True
    */
    virtual bool callLogClear(const String& table, const String& direction);

    /**
     * Make an outgoing call to a target picked from the call log
     * @param billid The bill id of the call
     * @return True on success (call initiated)
    */
    virtual bool callLogCall(const String& billid);

    /**
     * Create a contact from a call log entry
     * @param billid The bill id of the call
     * @return True on success (address book popup window was displayed)
    */
    virtual bool callLogCreateContact(const String& billid);

    /** 
     * Process help related actions
     * @param action The action's name
     * @param wnd The window owning the control
     * @return True on success
     */
    virtual bool help(const String& action, Window* wnd);

    /**
     * Called by the client after loaded the callto history file
     * @return True to tell the client to stop notifying other logics
     */
    virtual bool calltoLoaded();

    /**
     * Called by the client after loaded the windows
     */
    virtual void loadedWindows()
	{}

    /**
     * Called by the client after loaded and intialized the windows
     */
    virtual void initializedWindows();

    /**
     * Called by the client after loaded and intialized the windows and
     *  loaded configuration files.
     * The default logic update client settings
     * @return True to stop processing this notification
     */
    virtual bool initializedClient();

    /**
     * Called by the client before exiting.
     * The default logic save client settings
     */
    virtual void exitingClient();

    /**
     * Process ui.action message
     * @param msg Received message
     * @param stopLogic Set to true on exit to tell the client to stop asking other logics
     * @return True to stop further processing by the engine
     */
    virtual bool handleUiAction(Message& msg, bool& stopLogic);

    /**
     * Process call.cdr message
     * @param msg Received message
     * @param stopLogic Set to true on exit to tell the client to stop asking other logics
     * @return True to stop further processing by the engine
     */
    virtual bool handleCallCdr(Message& msg, bool& stopLogic);

    /**
     * Process user.login message
     * @param msg Received message
     * @param stopLogic Set to true on exit to tell the client to stop asking other logics
     * @return True to stop further processing by the engine
     */
    virtual bool handleUserLogin(Message& msg, bool& stopLogic);

    /**
     * Process user.notify message
     * @param msg Received message
     * @param stopLogic Set to true on exit to tell the client to stop asking other logics
     * @return True to stop further processing by the engine
     */
    virtual bool handleUserNotify(Message& msg, bool& stopLogic);

    /**
     * Process resource.notify message
     * @param msg Received message
     * @param stopLogic Set to true on exit to tell the client to stop asking other logics
     * @return True to stop further processing by the engine
     */
    virtual bool handleResourceNotify(Message& msg, bool& stopLogic);

    /**
     * Process resource.subscribe message
     * @param msg Received message
     * @param stopLogic Set to true on exit to tell the client to stop asking other logics
     * @return True to stop further processing by the engine
     */
    virtual bool handleResourceSubscribe(Message& msg, bool& stopLogic);

    /**
     * Process xmpp.iq message
     * @param msg Received message
     * @param stopLogic Set to true on exit to tell the client to stop asking other logics
     * @return True to stop further processing by the engine
     */
    virtual bool handleXmppIq(Message& msg, bool& stopLogic)
	{ return false; }

    /**
     * Process clientchan.update message
     * @param msg Received message
     * @param stopLogic Set to true on exit to tell the client to stop asking other logics
     * @return True to stop further processing by the engine
     */
    virtual bool handleClientChanUpdate(Message& msg, bool& stopLogic);

    /**
     * Default message processor called for id's not defined in client.
     * Descendants may override it to process custom messages installed by
     *  them and relayed through the client
     * @param msg Received message
     * @param id Message id
     * @param stopLogic Set to true on exit to tell the client to stop asking other logics
     * @return True to stop further processing by the engine
     */
    virtual bool defaultMsgHandler(Message& msg, int id, bool& stopLogic);

    /**
     * Update from UI or from a given value the selected item in channels list.
     * The selected channel may not be the active one
     * @param item Optional new value for current selection. Set to 0 to upadte from UI
     */
    virtual void updateSelectedChannel(const String* item = 0);

    /**
     * Engine start notification
     * @param msg The engine.start message
     */
    virtual void engineStart(Message& msg)
	{}

    /**
     * Add a duration object to this client's list
     * @param duration The object to add
     * @param autoDelete True to delete the object when the list is cleared
     * @return True on success 
     */
    bool addDurationUpdate(DurationUpdate* duration, bool autoDelete = false);

    /**
     * Remove a duration object from list
     * @param name The name of the object to remove
     * @param delObj True to destroy the object, false to remove it
     * @return True on success 
     */
    bool removeDurationUpdate(const String& name, bool delObj = false);

    /**
     * Remove a duration object from list
     * @param duration The object to remove
     * @param delObj True to destroy the object, false to remove it
     * @return True on success 
     */
    bool removeDurationUpdate(DurationUpdate* duration, bool delObj = false);

    /**
     * Find a duration update by its name
     * @param name The name of the object to find
     * @param ref True to increase its reference counter before returning
     * @return DurationUpdate pointer or 0
     */
    DurationUpdate* findDurationUpdate(const String& name, bool ref = true);

    /**
     * Remove all duration objects
     */
    void clearDurationUpdate();

protected:
    /**
     * Method called by the client when idle.
     * This method is called in the UI's thread
     * @param time The current time
     */
    virtual void idleTimerTick(Time& time);

    /**
     * Enable call actions for a selected channel
     * @param id Channel id
     * @return True on success
     */
    virtual bool enableCallActions(const String& id);

    /**
     * Fill call start parameter list from UI
     * @param p The list of parameters to fill
     * @param wnd Optional window owning the widget triggering the action
     * @return True on success
     */
    virtual bool fillCallStart(NamedList& p, Window* wnd = 0);

    /**
     * Notification on selection changes in channels list.
     * Enable call actions for currently selected channel
     * @param old The old selection
     */
    virtual void channelSelectionChanged(const String& old);

    String m_name;                       // Logic's name
    String m_selectedChannel;            // The currently selected channel
    int m_prio;                          // Logics priority
    ObjList m_durationUpdate;            // Duration updates
    Mutex m_durationMutex;               // Lock duration operations
    String m_transferInitiated;          // Tranfer initiated id
    bool m_accShowAdvanced;              // Show/hide the account advanced options
};

/**
 * This class holds data about an account that can be registered to a server.
 * It also holds a list of contacts belonging to this account.
 * The account's id is kept in a lower case URI
 * @short A client account
 */
class YATE_API ClientAccount : public RefObject, public Mutex
{
    friend class ClientContact;
public:
    /**
     * Constructor
     * @param proto The account's protocol
     * @param user The account's username
     * @param host The account's host
     * @param startup True if the account should login at startup
     */
    ClientAccount(const char* proto, const char* user,
	const char* host, bool startup);

    /**
     * Constructor. Build an account from a list of parameters
     * @param params The list of parameters used to build this account
     */
    ClientAccount(const NamedList& params);

    /**
     * Get this account's URI
     * @return This account's URI
     */
    inline const URI& uri() const
	{ return m_uri; }

    /**
     * Get this account's id
     * @return This account's id
     */
    inline const URI& id() const
	{ return m_id; }

    /**
     * Get this account's contacts. The caller should lock the account while browsing the list
     * @return This account's contacts list
     */
    inline ObjList& contacts()
	{ return m_contacts; }

    /**
     * Get a string representation of this object
     * @return The account's compare id
     */
    virtual const String& toString() const
	{ return m_id; }

    /**
     * Get this account's resource
     * @return ClientResource pointer or 0
     */
    ClientResource* resource(bool ref = false);

    /**
     * Set/reset this account's resource
     * @param res The new account's resource
     */
    void setResource(ClientResource* res = 0);

    /**
     * Find a contact by its id
     * @param id The id of the desired contact
     * @param ref True to obtain a referenced pointer
     * @return ClientContact pointer or 0 if not found
     */
    virtual ClientContact* findContact(const String& id, bool ref = false);

    /**
     * Find a contact having a given id and resource
     * @param id The id of the desired contact
     * @param resid The id of the desired resource
     * @param ref True to obtain a referenced pointer
     * @return ClientContact pointer or 0 if not found
     */
    virtual ClientContact* findContact(const String& id, const String& resid, 
	bool ref = false);

    /**
     * Build a contact and append it to the list
     * @param id The contact's id
     * @param name The contact's name
     * @return ClientContact pointer or 0 if a contact with the given id already exists
     */
    virtual ClientContact* appendContact(const String& id, const char* name);

    /**
     * Build a contact and append it to the list
     * @param params Contact parameters
     * @return ClientContact pointer or 0 if a contact with the same id already exists
     */
    virtual ClientContact* appendContact(const NamedList& params);

    /**
     * Remove a contact from list. Reset contact's owner
     * @param id The contact's id
     * @param delObj True to delete the object if found
     * @return ClientContact pointer if found and not deleted or 0
     */
    virtual ClientContact* removeContact(const String& id, bool delObj = true);

    /**
     * Build a login/logout message from account's data
     * @param login True to login, false to logout
     * @param msg Optional message name. Default to 'user.login'
     * @return A valid Message pointer
     */
    virtual Message* userlogin(bool login, const char* msg = "user.login");

    /**
     * Build an account id
     * @param dest Destination URI
     * @param proto The account's protocol
     * @param user The account's username
     * @param host The account's host
     */
    static void buildAccountId(URI& dest, const char* proto, const char* user, const char* host) {
	    URI u(proto,user,host);
	    dest = u.toLower();
	}

    String m_password;                   // Account's password
    String m_server;                     // Account's server (name or IP address)
    int m_port;                          // Server's port used to connect to
    String m_options;                    // Account's options
    bool m_startup;                      // Enable/disable flag
    String m_outbound;                   // Outbound server (if any)
    int m_expires;                       // Registration interval for protocols supporting it
    bool m_connected;                    // Logged in/out flag

protected:
    // Remove from owner. Release data
    virtual void destroyed();
    // Method used by the contact to append itself to this account's list
    virtual void appendContact(ClientContact* contact);
    // Set ID and URI
    inline void setIdUri(const char* proto, const char* user,
	const char* host) {
	    buildAccountId(m_id,proto,user,host);
	    m_uri = String(user) + "@" + host;
	}

    URI m_id;                            // The account's id
    URI m_uri;                           // Account's URI
    ClientResource* m_resource;          // Account's resource
    ObjList m_contacts;                  // Account's contacts
};

/**
 * This class holds an account list
 * @short A client account list
 */
class YATE_API ClientAccountList : public String, public Mutex
{
public:
    /**
     * Constructor
     * @param name List's name used for debug purposes 
     */
    inline ClientAccountList(const char* name)
	: String(name), Mutex(true)
	{}

    /**
     * Get the accounts list
     * @return The accounts list
     */
    inline ObjList& accounts()
	{ return m_accounts; }

    /**
     * Find an account
     * @param id The account's id
     * @param ref True to get a referenced pointer
     * @return ClientAccount pointer or 0 if not found
     */
    virtual ClientAccount* findAccount(const String& id, bool ref = false);

    /**
     * Find an account's contact
     * @param account The account's id
     * @param id The contact's id
     * @param ref True to get a referenced pointer
     * @return ClientContact pointer or 0 if not found
     */
    virtual ClientContact* findContact(const String& account, const String& id, bool ref = false);

    /**
     * Find an account's contact from a built id
     * @param builtId The string containign the account and the contact
     * @param ref True to get a referenced pointer
     * @return ClientContact pointer or 0 if not found
     */
    virtual ClientContact* findContact(const String& builtId, bool ref = false);

    /**
     * Append a new account. The account's reference counter is increased before
     * @param account The account to append
     * @return True on succes, false if an account with the same id already exists
     */
    virtual bool appendAccount(ClientAccount* account);

    /**
     * Remove an account
     * @param id The account's id
     */
    virtual void removeAccount(const String& id);

protected:
    ObjList m_accounts;
};

/**
 * A client contact
 * The contact is using the owner's mutex to lock it's operations
 * @short A client contact
 */
class YATE_API ClientContact : public RefObject
{
    friend class ClientAccount;
public:
    /**
     * Constructor. Append itself to the owner's list
     * @param owner The contact's owner
     * @param id The contact's id
     * @param name Optional display name. Defaults to the id's value if 0
     * @param chat True to create the chat window
     */
    ClientContact(ClientAccount* owner, const char* id, const char* name = 0,
	bool chat = false);

    /**
     * Constructor. Build a contact from a list of parameters.
     . Append itself to the owner's list
     * @param owner The contact's owner
     * @param params The list of parameters used to build this contact
     * @param chat True to create the chat window
     */
    ClientContact(ClientAccount* owner, NamedList& params, bool chat);

    /**
     * Get this contact's account
     * @return This contact's account
     */
    inline ClientAccount* account()
	{ return m_owner; }

    /**
     * Get this contact's URI
     * @return This contact's URI
     */
    inline const URI& uri() const
	{ return m_uri; }

    /**
     * Get the resource list of this contact
     * @return The resource list of this contact
     */
    inline ObjList& resources()
	{ return m_resources; }

    /**
     * Get the group list of this contact
     * @return The group list of this contact
     */
    inline ObjList& groups()
	{ return m_groups; }

    /**
     * Get a string representation of this object
     * @return The contact's id
     */
    virtual const String& toString() const
	{ return m_id; }

    /**
     * Build a contact id to be used in UI
     * @param dest Destination string
     */
    inline void buildContactId(String& dest)
	{  buildContactId(dest,m_owner?m_owner->toString():String::empty(),m_id); }

    /**
     * Check if a window is this contact's chat
     * @param wnd The window to check
     * @return True if the given window is this contact's chat
     */
    inline bool isChatWnd(Window* wnd)
	{ return wnd && wnd->toString() == m_chatWndName; }

    /**
     * Check if this contact has a chat window
     * @return True if this contact has a chat window
     */
    inline bool hasChat()
	{ return Client::self() && Client::self()->getWindow(m_chatWndName); }

    /**
     * Check if this contact's chat window is visible
     * @return True if this contact's chat window is visible
     */
    inline bool isChatVisible()
	{ return Client::self() && Client::self()->getVisible(m_chatWndName); }

    /**
     * Show or hide this contact's chat window
     * @param active The chat window's visibility flag
     * @return True on success
     */
    inline bool showChat(bool active)
	{ return Client::self() ? Client::self()->setVisible(m_chatWndName,active) : false; }

    /**
     * Get the chat window
     * @return Valid Window pointer or 0
     */
    inline Window* getChatWnd() const
	{ return Client::self() ? Client::self()->getWindow(m_chatWndName) : 0; }

    /**
     * Create the chat window
     * @param force True to destroy the current one if any
     * @param name The window's name
     */
    void createChatWindow(bool force = false, const char* name = "chat");

    /**
     * Close (desrtoy) the chat window
     */
    inline void destroyChatWindow() {
	    if (m_chatWndName && Client::self())
		Client::self()->closeWindow(m_chatWndName,false);
	}

    /**
     * Find a group this contact might belong to
     * @param group The name of the group to find
     * @return String pointer or 0 if not found
     */
    virtual String* findGroup(const String& group);

    /**
     * Append a group to this contact
     * @param group Group's name
     * @return False if the group already exists
     */
    virtual bool appendGroup(const String& group);

    /**
     * Remove a contact's group
     * @param group Group's name
     * @return False if the group was not found
     */
    virtual bool removeGroup(const String& group);

    /**
     * Find a resource having a given id
     * @param id The id of the desired resource
     * @param ref True to obtain a referenced pointer
     * @return ClientResource pointer or 0 if not found
     */
    virtual ClientResource* findResource(const String& id, bool ref = false);

    /**
     * Get the first resource with audio capability
     * @param ref True to obtain a referenced pointer
     * @return ClientResource pointer or 0 if not found
     */
    virtual ClientResource* findAudioResource(bool ref = false);

    /**
     * Append a resource having a given id
     * @param id The id of the desired resource
     * @return ClientResource pointer or 0 if a resource with the given name already exists
     */
    virtual ClientResource* appendResource(const String& id);

    /**
     * Remove a resource having a given id
     * @param id The id of the desired resource
     * @return True if the resource was removed
     */
    virtual bool removeResource(const String& id);

    /**
     * Check if a window is a chat one
     * @param wnd The window to check
     * @return True if the given window's name starts with the chat refix
     */
    static inline bool isChatWndPrefix(Window* wnd)
	{ return wnd && wnd->toString().startsWith(s_chatPrefix); }

    /**
     * Build a contact id to be used in UI
     * @param dest Destination string
     * @param account Account owning the contact
     * @param contact The contact's id
     */
    static inline void buildContactId(String& dest, const String& account,
	const String& contact)
	{ dest << String(account).toLower() << "|" << String(contact).toLower(); }

    /**
     * Split a contact id
     * @param src Source string
     * @param account Account name
     * @param contact Contact's name
     */
    static inline void splitContactId(const String& src, String& account,
	String& contact) {
	    int pos = src.find('|');
	    if (pos < 1) {
		account = src;
		return;
	    }
	    account = src.substr(0,pos);
	    contact = src.substr(pos + 1);
	}

    // Chat window prefix
    static String s_chatPrefix;

    String m_name;                       // Contact's display name
    String m_subscription;               // Presence subscription state

protected:
    // Remove from owner. Destroy the chat window. Release data
    virtual void destroyed();

    ClientAccount* m_owner;              // The account owning this contact
    String m_id;                         // The contact's id
    URI m_uri;                           // The contact's URI
    ObjList m_resources;                 // The contact's resource list
    ObjList m_groups;                    // The group(s) this contract belongs to 

private:
    String m_chatWndName;                // Chat window name if any
};

/**
 * This class holds data about a client account/contact resource
 * @short A client contact's resource
 */
class YATE_API ClientResource : public RefObject
{
public:
    /**
     * Constructor
     * @param id The resource's id
     * @param audio True (default) if the resource has audio capability
     * @param name Optional display name. Defaults to the id's value if 0
     */
    inline ClientResource(const char* id, const char* name = 0, bool audio = true)
	: m_name(name ? name : id), m_audio(audio), m_priority(0), m_id(id)
	{}

    /**
     * Get a string representation of this object
     * @return The account's id
     */
    virtual const String& toString() const
	{ return m_id; }

    String m_name;                       // Account's display name
    bool m_audio;                        // Audio capability flag
    int m_priority;                      // Resource priority
    String m_status;                     // Resource status string

protected:
    String m_id;                         // The account's id
};

/**
 * Class used to update UI durations. The string keeps the object's id.
 * This object can be used to keep additional data associated with a client channel
 * @short An UI time updater
 */
class YATE_API DurationUpdate : public RefObject
{
public:
    /**
     * Constructor. Add itself to logic's list
     * @param logic The client logic used to update this duration object
     * @param owner True if the logic is owning this object
     * @param id Object id
     * @param name Object name (widget or column name)
     * @param start Start time in seconds
     */
    inline DurationUpdate(ClientLogic* logic, bool owner, const char* id,
	const char* name, unsigned int start = Time::secNow())
	: m_id(id), m_logic(0), m_name(name),	m_startTime(start)
	{ setLogic(logic,owner); }

    /**
     * Destructor
     */
    virtual ~DurationUpdate();

    /**
     * Get a string representation of this object
     * @return This duration's id
     */
    virtual const String& toString() const;

    /**
     * Set the logic used to update this duration object. Remove from the old one
     * @param logic The client logic used to update this duration object
     * @param owner True if the logic is owning this object
     */
    void setLogic(ClientLogic* logic = 0, bool owner = true);

    /**
     * Update UI if duration is non 0
     * @param secNow Current time in seconds
     * @param table The table to update. Set to 0 to update text widgets
     * @param wnd Optional window to update
     * @param skip Optional window to skip if wnd is 0
     * @param force Set to true to update even if duration is 0
     * @return The duration
     */
    virtual unsigned int update(unsigned int secNow, const String* table = 0,
	Window* wnd = 0, Window* skip = 0, bool force = false);

    /**
     * Build a duration string representation and add the parameter to a list
     * @param dest Destination list
     * @param secNow Current time in seconds
     * @param force Set to true to add the parameter even if duration is 0
     * @return The duration
     */
    virtual unsigned int buildTimeParam(NamedList& dest, unsigned int secNow,
	bool force = false);

    /**
     * Build a duration string representation hh:mm:ss. The hours are added only if non 0
     * @param dest Destination string
     * @param secNow Current time in seconds
     * @param force Set to true to build even if duration is 0
     * @return The duration
     */
    virtual unsigned int buildTimeString(String& dest, unsigned int secNow,
	bool force = false);

    /**
     * Build a duration string representation and add the parameter to a list
     * @param dest Destination list
     * @param param Parameter to add
     * @param secStart Starting time in seconds
     * @param secNow Current time in seconds
     * @param force Set to true to add the parameter even if duration is 0
     * @return The duration
     */
    static unsigned int buildTimeParam(NamedList& dest, const char* param, unsigned int secStart,
	unsigned int secNow, bool force = false);

    /**
     * Build a duration string representation hh:mm:ss. The hours are added only if non 0
     * @param dest Destination string
     * @param secStart Starting time in seconds
     * @param secNow Current time in seconds
     * @param force Set to true to build even if duration is 0
     * @return The duration
     */
    static unsigned int buildTimeString(String& dest, unsigned int secStart, unsigned int secNow,
	bool force = false);

protected:
    /**
     * Release memory. Remove from updater
     */
    virtual void destroyed();

    String m_id;                         // Duration's id
    ClientLogic* m_logic;                // Client logic having this object in its list
    String m_name;                       // Widget/column name
    unsigned int m_startTime;            // Start time
};

/**
 * This class holds a sound file along with an output device used to play it
 * @short A sound file
 */
class YATE_API ClientSound : public String
{
public:
    /**
     * Constructor
     * @param name The name of this object
     * @param file The file to play (should contain the whole path and the file name)
     * @param device Optional device used to play the file. Set to 0 to use the default one
     */
    inline ClientSound(const char* name, const char* file, const char* device = 0)
	: String(name), m_file(file), m_device(device), m_repeat(-1), m_started(false)
	{}

    /**
     * Destructor. Stop playing the file
     */
    virtual ~ClientSound()
	{ stop(); }

    /**
     * Stop playing. Release memory
     */
    virtual void destruct() {
	    stop();
	    String::destruct();
	}

    /**
     * Check if this sound is started
     * @return True if this sound is started
     */
    inline bool started() const
	{ return m_started; }

    /**
     * Get the file played by this sound
     * @return The file played by this sound
     */
    inline const String& file() const
	{ return m_file; }

    /**
     * Set the file played by this sound.
     * The new file will not be used until the next time the sound is started
     * @param filename The new file played by this sound
     */
    inline void file(const char* filename)
	{ m_file = filename; }

    /**
     * Start playing the file
     * @param repeat The number of times to play the file if positive,
     *  play until explicitely stopped otherwise
     * @param force True to start playing the file even if already started
     * @return True on success
     */
    bool start(int repeat = -1, bool force = true);

    /**
     * Stop playing the file
     */
    void stop();

    /**
     * Check if a sound is started
     * @param name The name of the sound to check
     * @return True if the given sound is started
     */
    static bool started(const String& name);

    /**
     * Start playing a given sound
     * @param name The name of the sound to play
     * @param repeat The number of times to play the file if positive,
     *  play until explicitely stopped otherwise
     * @param force True to start playing the file even if already started
     * @return True on success
     */
    static bool start(const String& name, int repeat = -1, bool force = true);

    /**
     * Stop playing a given sound
     * @param name The name of the sound to stop
     */
    static void stop(const String& name);

    /**
     * Find a sound object
     * @param token The token used to match the sound
     * @param byName True to match the sound's name, false to match its file
     * @return True on success
     */
    static ClientSound* find(const String& token, bool byName = true);

    /**
     * The list of sounds
     */
    static ObjList s_sounds;

    /**
     * Mutex used to lock the sounds list operations
     */
    static Mutex s_soundsMutex;

protected:
    virtual bool doStart()
	{ return false; }
    virtual void doStop()
	{}

    String m_file;
    String m_device;
    int m_repeat;
    bool m_started;
};

}; // namespace TelEngine

#endif /* __YATECBASE_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
