/**
 * yatecbase.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Common base classes for all telephony clients
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

class Flags32;                           // Keeps a 32bit length flag mask
class NamedInt;                          // A named integer value
class Window;                            // A generic window
class UIWidget;                          // A custom widget
class UIFactory;                         // Base factory used to build custom widgets
class Client;                            // The client
class ClientChannel;                     // A client telephony channel
class ClientDriver;                      // The client telephony driver
class ClientLogic;                       // Base class for all client logics
class DefaultLogic;                      // The client's default logic
class ClientWizard;                      // A client wizard
class ClientAccount;                     // A client account
class ClientAccountList;                 // A client account list
class ClientContact;                     // A client contact
class ClientResource;                    // A client contact's resource
class MucRoomMember;                     // A MUC room member
class MucRoom;                           // An account's MUC room contact
class DurationUpdate;                    // Class used to update UI durations
class ClientSound;                       // A sound file
class ClientFileItem;                    // Base class for file/dir items
class ClientDir;                         // A directory
class ClientFile;                        // A file


/**
 * This class keeps a 32bit length flag mask
 * @short A 32 bit length list of flags
 */
class YATE_API Flags32
{
public:
    /**
     * Constructor
     */
    inline Flags32()
	: m_flags(0)
	{}

    /**
     * Constructor
     * @param value Flags value
     */
    inline Flags32(u_int32_t value)
	: m_flags(value)
	{}

    /**
     * Retrieve flags value
     * @return The flags
     */
    inline u_int32_t flags() const
	{ return m_flags; }

    /**
     * Set flags
     * @param mask Flag(s) to set
     */
    inline void set(u_int32_t mask)
	{ m_flags = m_flags | mask; }

    /**
     * Reset flags
     * @param mask Flag(s) to reset
     */
    inline void reset(u_int32_t mask)
	{ m_flags = m_flags & ~mask; }

    /**
     * Check if a mask of flags is set
     * @param mask Flag(s) to check
     * @return The flags of mask which are set, 0 if no mask flag is set
     */
    inline u_int32_t flag(u_int32_t mask) const
	{ return (m_flags & mask); }

    /**
     * Set or reset flags
     * @param mask Flag(s)
     * @param on True to set, false to reset
     */
    inline void changeFlag(u_int32_t mask, bool on) {
	    if (on)
		set(mask);
	    else
		reset(mask);
	}

    /**
     * Set or reset flags, check if changed
     * @param mask Flag(s)
     * @param ok True to set, false to reset
     * @return True if any flag contained in mask changed
     */
    inline bool changeFlagCheck(u_int32_t mask, bool ok) {
	    if ((0 != flag(mask)) == ok)
		return false;
	    changeFlag(mask,ok);
	    return true;
	}

    /**
     * Change flags
     * @param value New flags value
     */
    inline void change(u_int32_t value)
	{ m_flags = value; }

    /**
     * Conversion to u_int32_t operator
     */
    inline operator u_int32_t() const
	{ return m_flags; }

    /**
     * Asignement from int operator
     */
    inline const Flags32& operator=(int value)
	{ m_flags = value; return *this; }

protected:
    u_int32_t m_flags;
};

/**
 * This class holds a name integer value
 * @short A named integer value
 */
class YATE_API NamedInt: public String
{
    YCLASS(NamedInt,String)
public:
    /**
     * Constructor
     * @param name Name
     * @param val The value
     */
    inline NamedInt(const char* name, int val = 0)
	: String(name), m_value(val)
	{}

    /**
     * Copy constructor
     * @param other Source object
     */
    inline NamedInt(const NamedInt& other)
	: String(other), m_value(other.value())
	{}

    /**
     * Retrieve the value
     * @return The integer value
     */
    inline int value() const
	{ return m_value; }

    /**
     * Set the value
     * @param val The new integer value
     */
    inline void setValue(int val)
	{ m_value = val; }

    /**
     * Add an item to a list. Replace existing item with the same name
     * @param list The list
     * @param obj The object
     */
    static void addToListUniqueName(ObjList& list, NamedInt* obj);

    /**
     * Clear all items with a given value
     * @param list The list
     * @param val Value to remove
     */
    static void clearValue(ObjList& list, int val);

    /**
     * Get an item's value from name
     * @param list The list containing the item
     * @param name Item name
     * @param defVal Value to return if not found
     * @return Item value
     */
    static inline int lookup(const ObjList& list, const String& name, int defVal = 0) {
	    ObjList* o = list.find(name);
	    return o ? (static_cast<NamedInt*>(o->get()))->value() : defVal;
	}

    /**
     * Get an item's name from value
     * @param list The list containing the item
     * @param val Item value
     * @param defVal Name to return if not found
     * @return Item name
     */
    static inline const String& lookupName(const ObjList& list, int val,
	const String& defVal = String::empty()) {
	    for (ObjList* o = list.skipNull(); o; o = o->skipNext()) {
		NamedInt* ni = static_cast<NamedInt*>(o->get());
		if (ni->value() == val)
		    return *ni;
	    }
	    return defVal;
	}

protected:
    int m_value;

private:
    NamedInt() {}
};

/**
 * A window is the basic user interface element.
 * Everything inside is implementation specific functionality.
 * @short An abstract user interface window
 */
class YATE_API Window : public GenObject
{
    YCLASS(Window,GenObject)
    friend class Client;
    YNOCOPY(Window); // no automatic copies please
public:
    /**
     * Constructor, creates a new windows with an ID
     * @param id String identifier of the new window
     */
    explicit Window(const char* id = 0);

    /**
     * Destructor
     */
    virtual ~Window();

    /**
     * Retrieve the standard name of this Window, used to search in lists
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
     * Append or update several table rows at once
     * @param name Name of the element
     * @param data Parameters to initialize the rows with
     * @param prefix Prefix to match (and remove) in parameter names
     * @return True if all the operations were successfull
     */
    virtual bool setMultipleRows(const String& name, const NamedList& data, const String& prefix = String::empty());

    /**
     * Insert a row into a table owned by this window
     * @param name Name of the element
     * @param item Name of the item to insert
     * @param before Name of the item to insert before
     * @param data Table's columns to set
     * @return True if the operation was successfull
     */
    virtual bool insertTableRow(const String& name, const String& item,
	const String& before, const NamedList* data = 0);

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
     * Show or hide control busy state
     * @param name Name of the element
     * @param on True to show, false to hide
     * @return True if all the operations were successfull
     */
    virtual bool setBusy(const String& name, bool on) = 0;

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
     * Retrieve an element's multiple selection
     * @param name Name of the element
     * @param items List to be to filled with selection's contents
     * @return True if the operation was successfull
     */
    virtual bool getSelect(const String& name, NamedList& items) = 0;

    /**
     * Build a menu from a list of parameters.
     * See Client::buildMenu() for more info
     * @param params Menu build parameters
     * @return True on success
     */
    virtual bool buildMenu(const NamedList& params) = 0;

    /**
     * Remove a menu (from UI and memory)
     * See Client::removeMenu() for more info
     * @param params Menu remove parameters
     * @return True on success
     */
    virtual bool removeMenu(const NamedList& params) = 0;

    /**
     * Set an element's image
     * @param name Name of the element
     * @param image Image to set
     * @param fit Fit image in element (defaults to false)
     * @return True on success
     */
    virtual bool setImage(const String& name, const String& image, bool fit = false) = 0;

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
     * Retrieve the standard name of this Window
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
     * Check if this window is the active one
     * @return True if window is active
     */
    inline bool active() const
	{ return m_active; }

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
     * Create a modal dialog
     * @param name Dialog name (resource config section)
     * @param title Dialog title
     * @param alias Optional dialog alias (used as dialog object name)
     * @param params Optional dialog parameters
     * @return True on success
     */
    virtual bool createDialog(const String& name, const String& title,
	const String& alias = String::empty(), const NamedList* params = 0) = 0;

    /**
     * Destroy a modal dialog
     * @param name Dialog name
     * @return True on success
     */
    virtual bool closeDialog(const String& name) = 0;

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
    bool m_active;
    bool m_master;
    bool m_popup;
    bool m_saveOnClose;                  // Save window's data when destroyed

private:
    bool m_populated;                    // Already populated flag
    bool m_initialized;                  // Already initialized flag
};

class YATE_API UIWidget : public String
{
    YCLASS(UIWidget,String)
    YNOCOPY(UIWidget); // no automatic copies please
public:
    /**
     * Constructor, creates a new widget
     * @param name The widget's name
     */
    inline explicit UIWidget(const char* name = 0)
	: String(name)
	{ }

    /**
     * Destructor
     */
    virtual ~UIWidget()
	{ }

    /**
     * Retrieve the standard name of this Window
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

    /** Append or update several table rows at once
     * @param data Parameters to initialize the rows with
     * @param prefix Prefix to match (and remove) in parameter names
     * @return True if all the operations were successfull
     */
    virtual bool setMultipleRows(const NamedList& data, const String& prefix = String::empty())
	{ return false; }

    /**
     * Add or set one or more table row(s). Screen update is locked while changing the table.
     * Each data list element is a NamedPointer carrying a NamedList with item parameters.
     * The name of an element is the item to update.
     * Set element's value to boolean value 'true' to add a new item if not found
     *  or 'false' to set an existing one. Set it to empty string to delete the item
     * @param data The list of items to add/set/delete
     * @param atStart True to add new items at start, false to add them to the end
     * @return True if the operation was successfull
     */
    virtual bool updateTableRows(const NamedList* data, bool atStart = false)
	{ return false; }

    /**
     * Insert a row into a table
     * @param item Name of the item to insert
     * @param before Name of the item to insert before
     * @param data Table's columns to set
     * @return True if the operation was successfull
     */
    virtual bool insertTableRow(const String& item, const String& before,
	const NamedList* data = 0)
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
     * Set the widget's selection
     * @param item String containing the new selection
     * @return True if the operation was successfull
     */
    virtual bool setSelect(const String& item)
	{ return false; }

    /**
     * Retrieve the widget's selection
     * @param item String to fill with selection's contents
     * @return True if the operation was successfull
     */
    virtual bool getSelect(String& item)
	{ return false; }

    /**
     * Retrieve widget's multiple selection
     * @param items List to be to filled with selection's contents
     * @return True if the operation was successfull
     */
    virtual bool getSelect(NamedList& items)
	{ return false; }

    /**
     * Append or insert text lines to this widget
     * @param lines List containing the lines
     * @param max The maximum number of lines allowed to be displayed. Set to 0 to ignore
     * @param atStart True to insert, false to append
     * @return True on success
     */
    virtual bool addLines(const NamedList& lines, unsigned int max, bool atStart = false)
	{ return false; }

    /**
     * Set the displayed text of this widget
     * @param text Text value to set
     * @param richText True if the text contains format data
     * @return True on success
     */
    virtual bool setText(const String& text, bool richText = false)
	{ return false; }

    /**
     * Retrieve the displayed text of this widget
     * @param text Text value
     * @param richText True to retrieve formatted data
     * @return True on success
     */
    virtual bool getText(String& text, bool richText = false)
	{ return false; }

    /**
     * Show or hide control busy state
     * @param on True to show, false to hide
     * @return True if all the operations were successfull
     */
    virtual bool setBusy(bool on)
	{ return false; }
};

/**
 * Each instance of UIFactory creates special user interface elements by type.
 * Keeps a global list with all factories. The list doesn't own the facotries
 * @short A static user interface creator
 */
class YATE_API UIFactory : public String
{
    YCLASS(UIFactory,String)
    YNOCOPY(UIFactory); // no automatic copies please
public:
    /**
     * Constructor. Append itself to the factories list
     */
    explicit UIFactory(const char* name);

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
 * Singleton class that holds the User Interface's main  methods
 * @short Class that runs the User Interface
 */
class YATE_API Client : public MessageReceiver
{
    YCLASS(Client,MessageReceiver)
    friend class Window;
    friend class ClientChannel;
    friend class ClientDriver;
    friend class ClientLogic;
public:
    /**
     * Message relays installed by this receiver.
     */
    enum MsgID {
	CallCdr = 0,
	UiAction,
	UserLogin,
	UserNotify,
	ResourceNotify,
	ResourceSubscribe,
	ClientChanUpdate,
	UserRoster,
	ContactInfo,
	// Handlers not automatically installed
	ChanNotify,
	MucRoom,
	// IDs used only to postpone messages
	MsgExecute,
	EngineStart,
	TransferNotify,
	UserData,
	FileInfo,
	// Starting value for custom relays
	MsgIdCount
    };

    /**
     * Client boolean options mapped to UI toggles
     */
    enum ClientToggle {
	OptMultiLines = 0,               // Accept incoming calls
	OptAutoAnswer,                   // Auto answer incoming calls
	OptRingIn,                       // Enable/disable incoming ringer
	OptRingOut,                      // Enable/disable outgoing ringer
	OptActivateLastOutCall,          // Set the last outgoing call active
	OptActivateLastInCall,           // Set the last incoming call active
	OptActivateCallOnSelect,         // Set the active call when selected in channel
	                                 //  list (don't require double click)
	OptKeypadVisible,                // Show/hide keypad
	OptOpenIncomingUrl,              // Open an incoming URL in call.execute message
	OptAddAccountOnStartup,          // Open account add window on startup
	OptDockedChat,                   // Show all contacts chat in the same window
	OptDestroyChat,                  // Destroy contact chat when contact is removed/destroyed
	OptNotifyChatState,              // Notify chat states
	OptShowEmptyChat,                // Display received empty chat in chat history
	OptSendEmptyChat,                // Send empty chat
	OptCount
    };

    /**
     * Tray icon valuers used in stack
     */
    enum TrayIconType {
	TrayIconMain = 0,
	TrayIconInfo = 1000,
	TrayIconIncomingChat = 3000,
	TrayIconNotification = 5000,
	TrayIconIncomingCall = 10000,
    };

    /**
     * Constructor
     * @param name The client's name
     */
    explicit Client(const char *name = 0);

    /**
     * Destructor
     */
    virtual ~Client();

    /**
     * Start up the client thread
     * @return True if the client thread is started, false otherwise
     */
    virtual bool startup();

    /**
     * Run the client's main loop
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
     * Set the client's thread
     * @param th The thread on which the client will run on
     */
    inline void setThread(Thread* th)
	{ m_clientThread = th; }

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
     * Open an URL (link) in the client's thread
     * @param url The URL to open
     * @return True on success
     */
    bool openUrlSafe(const String& url);

    /**
     * Open an URL (link)
     * @param url The URL to open
     * @return True on success
     */
    virtual bool openUrl(const String& url) = 0;

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
     * Create a modal dialog owned by a given window
     * @param name Dialog name (resource config section)
     * @param parent Parent window
     * @param title Dialog title
     * @param alias Optional dialog alias (used as dialog object name)
     * @param params Optional dialog parameters
     * @return True on success
     */
    virtual bool createDialog(const String& name, Window* parent, const String& title,
	const String& alias = String::empty(), const NamedList* params = 0);

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
     * Destroy a modal dialog
     * @param name Dialog name
     * @param wnd Window owning the dialog
     * @param skip Optional window to skip if wnd is null
     * @return True on success
     */
    virtual bool closeDialog(const String& name, Window* wnd, Window* skip = 0);

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
     * Handle 'select' with multiple items actions from user interface. Enqueue an ui.event message if
     *  the action is not handled by a client logic
     * @param wnd The window in which the user selected the object
     * @param name The action's name
     * @param items List containing the selection
     * @return True if the action was handled by a client logic
     */
    virtual bool select(Window* wnd, const String& name, const NamedList& items);

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
    bool addLines(const String& name, const NamedList* lines, unsigned int max,
	bool atStart = false, Window* wnd = 0, Window* skip = 0);

    bool addTableRow(const String& name, const String& item, const NamedList* data = 0,
	bool atStart = false, Window* wnd = 0, Window* skip = 0);

    /**
     * Append or update several table rows at once
     * @param name Name of the element
     * @param data Parameters to initialize the rows with
     * @param prefix Prefix to match (and remove) in parameter names
     * @param wnd Optional window owning the element
     * @param skip Optional window to skip if wnd is 0
     * @return True if all the operations were successfull
     */
    bool setMultipleRows(const String& name, const NamedList& data, const String& prefix = String::empty(), Window* wnd = 0, Window* skip = 0);

    /**
     * Insert a row into a table owned by this window
     * @param name Name of the element
     * @param item Name of the item to insert
     * @param before Name of the item to insert before
     * @param data Table's columns to set
     * @param wnd Optional window owning the element
     * @param skip Optional window to skip if wnd is 0
     * @return True if the operation was successfull
     */
    bool insertTableRow(const String& name, const String& item,
	const String& before, const NamedList* data = 0,
	Window* wnd = 0, Window* skip = 0);

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
     * Show or hide control busy state
     * @param name Name of the element
     * @param on True to show, false to hide
     * @param wnd Optional window owning the element
     * @param skip Optional window to skip if wnd is 0
     * @return True if all the operations were successfull
     */
    bool setBusy(const String& name, bool on, Window* wnd = 0, Window* skip = 0);

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
     * Retrieve an element's multiple selection
     * @param name Name of the element
     * @param items List to be to filled with selection's contents
     * @param wnd Optional window owning the element
     * @param skip Optional window to skip if wnd is 0
     * @return True if the operation was successfull
     */
    bool getSelect(const String& name, NamedList& items, Window* wnd = 0, Window* skip = 0);

    /**
     * Build a menu from a list of parameters and add it to a target.
     * @param params Menu build parameters (list name is the menu name).
     * Each menu item is indicated by a parameter item:[item_name]=[display_text].
     * A separator will be added if 'item_name' is empty.
     * A new item will be created if 'display_text' is not empty.
     * Set 'display_text' to empty string to use an existing item.
     * Item image can be set by an 'image:item_name' parameter.
     * If the item parameter is a NamedPointer carrying a NamedList a submenu will be created.
     * Menu item properties can be set from parameters with format
     *  property:item_name:property_name=value.
     * The following parameters can be set:
     *  - title: menu display text (defaults to menu name)
     *  - owner: optional menu owner (the window building the menu is
     *   assumed to be the owner if this parameter is empty)
     *  - target: optional menu target (defaults to owner)
     *  - before: optional item to insert before if the target is a menu container
     *   (another menu or a menu bar)
     *  - before_separator: check if a separator already exists before the item
     *   'before' and insert the menu before the separator
     * @param wnd Optional target window
     * @param skip Optional window to skip if wnd is 0
     * @return True on success
     */
    bool buildMenu(const NamedList& params, Window* wnd = 0, Window* skip = 0);

    /**
     * Remove a menu (from UI and memory)
     * @param params Menu remove parameters.
     * The following parameters can be set:
     *  - owner: optional menu owner (the window building the menu is
     *   assumed to be the owner if this parameter is empty)
     * @param wnd Optional target window
     * @param skip Optional window to skip if wnd is 0
     * @return True on success
     */
    bool removeMenu(const NamedList& params, Window* wnd = 0, Window* skip = 0);

    /**
     * Set an element's image
     * @param name Name of the element
     * @param image Image to set
     * @param wnd Optional target window
     * @param skip Optional window to skip if wnd is 0
     * @return True on success
     */
    virtual bool setImage(const String& name, const String& image,
	Window* wnd = 0, Window* skip = 0);

    /**
     * Set an element's image. Request to fit the image in element
     * @param name Name of the element
     * @param image Image to set
     * @param wnd Optional target window
     * @param skip Optional window to skip if wnd is 0
     * @return True on success
     */
    virtual bool setImageFit(const String& name, const String& image,
	Window* wnd = 0, Window* skip = 0);

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
    inline static void setSelf(Client* client)
	{ s_client = client; }

    /**
     * Check if the client object still exists and the client or engine is not exiting
     * @return True if the client is valid (running) or the method is called from client's thread
     */
    static inline bool valid()
	{ return self() && (self()->isUIThread() || !(exiting() || Engine::exiting())); }

    /**
     * Check if a message is sent by the client
     * @param msg The message to check
     * @return True if the message has a 'module' parameter with the client driver's name
     */
    static bool isClientMsg(Message& msg);

    inline static bool changing()
	{ return (s_changing > 0); }
    static Window* getWindow(const String& name);
    static bool setVisible(const String& name, bool show = true, bool activate = false);
    static bool getVisible(const String& name);
    static bool openPopup(const String& name, const NamedList* params = 0, const Window* parent = 0);
    static bool openMessage(const char* text, const Window* parent = 0, const char* context = 0);
    static bool openConfirm(const char* text, const Window* parent = 0, const char* context = 0);
    static ObjList* listWindows();
    void idleActions();

    /**
     * Postpone a copy of a message to be dispatched from the UI thread
     * @param msg Message to be postponed
     * @param id Identifier of the message to be used on dispatch
     * @param copyUserData Copy source user data in postponed message
     * @return True if the UI thread was not current so the message was postponed
     */
    bool postpone(const Message& msg, int id, bool copyUserData = false);

    /**
     * Show a file open/save dialog window
     * This method isn't using the proxy thread since it's usually called on UI action
     * @param parent Dialog window's parent
     * @param params Dialog window's params. Parameters that can be specified include 'caption',
     *  'dir', 'filters', 'selectedfilter', 'confirmoverwrite', 'choosedir'.
     * @return True on success
     */
    virtual bool chooseFile(Window* parent, NamedList& params)
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
     * Remove the last character of the given widget
     * @param name The widget (it might be the window itself)
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
     * @param setActive True to activate the answered channel
     * @return True on success
     */
    void callAnswer(const String& id, bool setActive = true);

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
     * Check if the client is exiting
     * @return True if the client therad is exiting
     */
    static inline bool exiting()
	{ return s_exiting; }

    /**
     * Retrieve the active state of a window
     * @param name Window name
     * @return True if the window is found and it's active
     */
    static bool getActive(const String& name);

    /**
     * Build a message to be sent by the client.
     * Add module, line and operation parameters
     * @param msg Message name
     * @param account The account sending the message
     * @param oper Optional operation parameter
     * @return Message pointer
     */
    static Message* buildMessage(const char* msg, const String& account,
	const char* oper = 0);

    /**
     * Build a resource.notify message
     * @param online True to build an 'online' message, false to build an 'offline' one
     * @param account The account sending the message
     * @param from Optional resource to add to message
     * @return Message pointer
     */
    static Message* buildNotify(bool online, const String& account,
	const ClientResource* from = 0);

    /**
     * Build a resource.subscribe or resource.notify message to request a subscription
     *  or respond to a request
     * @param request True to build a request, false to build a response
     * @param ok True to build a subscribe(d) message, false to build an unsubscribe(d) message
     * @param account The account to use for the message
     * @param contact The destination contact
     * @param proto Optional protocol
     * @return Valid Message pointer
     */
    static Message* buildSubscribe(bool request, bool ok, const String& account,
	const String& contact, const char* proto = 0);

    /**
     * Build an user.roster message
     * @param update True to build an update, false to build a delete request
     * @param account The account to use for the message
     * @param contact The contact to update or delete
     * @param proto Optional protocol
     * @return Valid Message pointer
     */
    static Message* buildUserRoster(bool update, const String& account,
	const String& contact, const char* proto = 0);

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
     * Append URI escaped String items to a String buffer
     * @param buf Destination string
     * @param list Source list
     * @param sep Destination list separator. It will be escaped in each added string
     * @param force True to allow appending empty strings
     */
    static void appendEscape(String& buf, ObjList& list, char sep = ',', bool force = false);

    /**
     * Splits a string at a delimiter character. URI unescape each string in result
     * @param buf Source string
     * @param sep Character where to split the string. It will be unescaped in each string
     * @param emptyOk True if empty strings should be inserted in list
     * @return A newly allocated list of strings, must be deleted after use
     */
    static ObjList* splitUnescape(const String& buf, char sep = ',', bool emptyOk = false);

    /**
     * Remove characters from a given string
     * @param buf Source string
     * @param chars Characters to remove from input string
     */
    static void removeChars(String& buf, const char* chars);

    /**
     * Fix a phone number. Remove extra '+' from begining.
     * Remove requested characters. Adding '+' to characters to remove won't remove
     *  the plus sign from the begining.
     * Clear the number if a non digit char is found
     * @param number Phone number to fix
     * @param chars Optional characters to remove from number
     */
    static void fixPhoneNumber(String& number, const char* chars = 0);

    /**
     * Add a tray icon to a window's stack. Update it if already there.
     * Show it if it's the first one and the client is started.
     * This method must be called from client's thread
     * @param wndName The window owning the icon
     * @param prio Tray icon priority. The list is kept in ascending order
     * @param params Tray icon parameters. It will be consumed
     * @return True on success
     */
    static bool addTrayIcon(const String& wndName, int prio, NamedList* params);

    /**
     * Remove a tray icon from a window's stack.
     * Show the next one if it's the first
     * This method must be called from client's thread
     * @param wndName The window owning the icon
     * @param name Tray icon name
     * @return True on success
     */
    static bool removeTrayIcon(const String& wndName, const String& name);

    /**
     * Update the first tray icon in a window's stack.
     * Remove any existing icon the the stack is empty
     * This method must be called from client's thread
     * @param wndName The window owning the icon
     * @return True on success
     */
    static bool updateTrayIcon(const String& wndName);

    /**
     * Generate a GUID string in the format 8*HEX-4*HEX-4*HEX-4*HEX-12*HEX
     * @param buf Destination string
     * @param extra Optional string whose hash will be inserted in the GUID
     */
    static void generateGuid(String& buf, const String& extra = String::empty());

    /**
     * Replace plain text chars with HTML escape or markup
     * @param buf Destination string
     * @param spaceEol True to replace end of line with space instead of html markup
     */
    static void plain2html(String& buf, bool spaceEol = false);

    /**
     * Find a list parameter by its value
     * @param list The list
     * @param value Parameter value
     * @param skip Optional parameter to skip
     * @return NamedString pointer, 0 if not found
     */
    static NamedString* findParamByValue(NamedList& list, const String& value,
	NamedString* skip = 0);

    /**
     * Decode flags from dictionary values found in a list of parameters
     * Flags are allowed to begin with '!' to reset
     * @param dict The dictionary containing the flags
     * @param params The list of parameters used to update the flags
     * @param prefix Optional parameter prefix
     * @return Decoded flags
     */
    static int decodeFlags(const TokenDict* dict, const NamedList& params,
	const String& prefix = String::empty());

    /**
     * Decode flags from dictionary values and comma separated list.
     * Flags are allowed to begin with '!' to reset
     * @param dict The dictionary containing the flags
     * @param flags The list of flags
     * @param defVal Default value to return if empty or no non 0 value is found in dictionary
     * @return Decoded flags
     */
    static int decodeFlags(const TokenDict* dict, const String& flags, int defVal = 0);

    /**
     * Add path separator at string end. Set destination string.
     * Source and dstination may be the same string
     * @param dest Destination string
     * @param path Source string
     * @param sep Path separator, use Engine::pathSeparator() if 0
     */
    static void addPathSep(String& dest, const String& path, char sep = 0);

    /**
     * Fix path separator. Set it to platform default
     * @param path The path
     */
    static void fixPathSep(String& path);

    /**
     * Remove path separator from string end. Set destination string.
     * Source and dstination may be the same string
     * @param dest Destination string
     * @param path Source string
     * @param sep Path separator, use Engine::pathSeparator() if 0
     * @return True if destination string is not empty
     */
    static bool removeEndsWithPathSep(String& dest, const String& path, char sep = 0);

    /**
     * Set destination from last item in path.
     * Source and dstination may be the same string
     * @param dest Destination string
     * @param path Source string
     * @param sep Path separator, use Engine::pathSeparator() if 0
     * @return True if destination string is not empty
     */
    static bool getLastNameInPath(String& dest, const String& path, char sep = 0);

    /**
     * Remove last name in path, set destination from remaining.
     * If the path ends with 'sep', only 'sep' is removed
     * If the path don't contain 'sep' dest is set to empty.
     * Source and dstination may be the same string
     * @param dest Destination string
     * @param path Source string
     * @param sep Path separator, use Engine::pathSeparator() if 0
     * @param equalOnly Optional string to match last item.
     *  Don't remove (set destination to empty) if not equal
     * @return True if removed
     */
    static bool removeLastNameInPath(String& dest, const String& path, char sep = 0,
	const String& equalOnly = String::empty());

    /**
     * Add a formatted log line
     * @param format Text format
     * @return True on success
     */
    static bool addToLogFormatted(const char* format, ...);

    static Configuration s_settings;     // Client settings
    static Configuration s_actions;      // Logic preferrences
    static Configuration s_accounts;     // Accounts
    static Configuration s_contacts;     // Contacts
    static Configuration s_providers;    // Provider settings
    static Configuration s_history;      // Call log
    static Configuration s_calltoHistory;  // Dialed destinations history
    // Holds a not selected/set value match
    static Regexp s_notSelected;
    // Regexp used to check if a string is a GUID in the format
    // 8*HEX-4*HEX-4*HEX-4*HEX-12*HEX
    static Regexp s_guidRegexp;
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
    // Maximum remote users allowed to enter in conference
    static int s_maxConfPeers;
    // Engine started flag
    static bool s_engineStarted;

protected:
    /**
     * Create the default logic
     * The default implementation creates a DefaultLogic object
     * @return ClientLogic pointer or 0
     */
    virtual ClientLogic* createDefaultLogic();
    virtual bool createWindow(const String& name,
	const String& alias = String::empty()) = 0;
    virtual void loadWindows(const char* file = 0) = 0;
    virtual void initWindows();
    virtual void initClient();
    virtual void exitClient()
	{}
    virtual bool isUIThread()
	{ return Thread::current() == m_clientThread; }
    inline bool needProxy() const
	{ return m_oneThread && !(Client::self() && Client::self()->isUIThread()); }
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
    ClientLogic* m_defaultLogic;         // The default logic
    static Client* s_client;
    static int s_changing;
    static ObjList s_logics;
    static bool s_idleLogicsTick;        // Call logics' timerTick()
    Thread* m_clientThread;
};

/**
 * This class implements a Channel used by client programs
 * @short Channel used by client programs
 */
class YATE_API ClientChannel : public Channel
{
    YCLASS(ClientChannel,Channel)
    friend class ClientDriver;
    YNOCOPY(ClientChannel); // no automatic copies please
public:
    /**
     * Channel notifications
     */
    enum Notification {
	Startup,
	Destroyed,
	Active,
	OnHold,
	Mute,
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
	AudioSet,
	Unknown
    };

    /**
     * Channel slave type
     */
    enum SlaveType {
	SlaveNone = 0,
	SlaveTransfer,
	SlaveConference,
    };

    /**
     * Incoming (from engine) constructor
     * @param msg The call.execute message
     * @param peerid The peer's id
     */
    ClientChannel(const Message& msg, const String& peerid);

    /**
     * Outgoing (to engine) constructor
     * @param target The target to call
     * @param params Call parameters
     * @param st Optional slave
     * @param masterChan Master channel id if slave, ignored otherwise
     */
    ClientChannel(const String& target, const NamedList& params, int st = SlaveNone,
	const String& masterChan = String::empty());

    /**
     * Constructor for utility channels used to play notifications
     * @param soundId The id of the sound to play
     */
    explicit ClientChannel(const String& soundId);

    virtual ~ClientChannel();

    /**
     * Init and start router for an outgoing (to engine), not utility, channel
     * @param target The target to call
     * @param params Call parameters
     * @return True on success
     */
    bool start(const String& target, const NamedList& params);

    virtual bool msgProgress(Message& msg);
    virtual bool msgRinging(Message& msg);
    virtual bool msgAnswered(Message& msg);
    virtual bool msgDrop(Message& msg, const char* reason);
    virtual bool callRouted(Message& msg);
    virtual void callAccept(Message& msg);
    virtual void callRejected(const char* error, const char* reason, const Message* msg);

    /**
     * Answer an incoming call. Set media channels. Enqueue a clientchan.update message
     * @param setActive True to activate the channel
     */
    void callAnswer(bool setActive = true);

    /**
     * Get the slave type of this channel
     * @return The slave type of this channel
     */
    inline int slave() const
	{ return m_slave; }

    /**
     * Retrieve channel slaves.
     * This method is not thread safe
     * @return Channel slaves list
     */
    inline ObjList& slaves()
	{ return m_slaves; }

    /**
     * Retrieve channel slaves number. This method is thread safe
     * @return Channel slaves list
     */
    inline unsigned int slavesCount() const {
	    Lock lock(m_mutex);
	    return m_slaves.count();
	}

    /**
     * Add a slave id. This method is thread safe
     * @param sid Slave id to add
     */
    inline void addSlave(const String& sid) {
	    Lock lock(m_mutex);
	    if (!m_slaves.find(sid))
		m_slaves.append(new String(sid));
	}

    /**
     * Remove a slave id. This method is thread safe
     * @param sid Slave id to remove
     */
    inline void removeSlave(const String& sid) {
	    Lock lock(m_mutex);
	    m_slaves.remove(sid);
	}

    /**
     * Get the master channel id if any
     * @return The master channel id of this channel
     */
    inline const String& master() const
	{ return m_master; }

    /**
     * Retrieve channel client parameters
     * @return Channel client parameters list
     */
    inline const NamedList& clientParams() const
	{ return m_clientParams; }

    /**
     * Get the remote party of this channel
     * @return The remote party of this channel
     */
    inline const String& party() const
	{ return m_party; }

    /**
     * Get the remote party name of this channel
     * @return The remote party name of this channel
     */
    inline const String& partyName() const
	{ return m_partyName ? m_partyName : m_party; }

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
     * Set/reset this channel's muted flag. Set media if 'on' is false and the channel is active
     * @param on True to reset outgoing media, false to set outgoing media
     * @param update True to enqueue an update message
     * @return True on success
     */
    bool setMuted(bool on, bool update = true);

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
     * Check if this channel is muted
     * @return True if this channel is muted
     */
    inline bool muted() const
	{ return m_muted; }

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
     * Retrieve peer used to reconnect. This method is thread safe
     * @param buf Destination buffer
     */
    inline void getReconnPeer(String& buf) {
	    Lock lck(m_mutex);
	    buf = m_peerId;
	}
    /**
     * Check if the peer used to reconnect is alive
     * @return True if the peer used to reconnect is alive
     */
    inline bool hasReconnPeer()
	{ return 0 != getReconnPeer(false); }

    /**
     * Get peer used to reconnect
     * @param ref True to return a referenced pointer
     * @return CallEndpoint pointer or 0 if not found
     */
    CallEndpoint* getReconnPeer(bool ref = true);

    /**
     * Drop peer used to reconnect
     */
    void dropReconnPeer(const char* reason = 0);

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
     * Lookup for a slave type
     * @param notif The slave type name
     * @param def Default value to return if not found
     * @return The result
     */
    static int lookupSlaveType(const char* notif, int def = SlaveNone)
	{ return TelEngine::lookup(notif,s_slaveTypes,def); }

    /**
     * Channel notifications dictionary
     */
    static const TokenDict s_notification[];

    /**
     * Channel notifications dictionary
     */
    static const TokenDict s_slaveTypes[];

protected:
    virtual void destroyed();
    virtual void connected(const char* reason);
    virtual void disconnected(bool final, const char* reason);
    // Check for a source in channel's peer or a received message's user data
    inline bool peerHasSource(Message& msg) {
    	    CallEndpoint* ch = getPeer();
	    if (!ch)
		ch = static_cast<CallEndpoint*>(msg.userObject(YATOM("CallEndpoint")));
	    return ch && ch->getSource();
	}
    // Check if our consumer's source sent any data
    // Don't set the silence flag is already reset
    void checkSilence();

    int m_slave;                         // Slave type
    String m_master;                     // Master channel id
    String m_party;                      // Remote party
    String m_partyName;                  // Remote party name
    String m_peerOutFormat;              // Peer consumer's data format
    String m_peerInFormat;               // Peer source's data format
    String m_reason;                     // Termination reason
    String m_peerId;                     // Peer's id (used to re-connect)
    bool m_noticed;                      // Incoming channel noticed flag
    int m_line;                          // Channel's line (address)
    bool m_active;                       // Channel active flag
    bool m_silence;                      // True if the peer did't sent us any audio data
    bool m_conference;                   // True if this channel is in conference
    bool m_muted;                        // True if this channel is muted (no data source))
    String m_transferId;                 // Transferred id or empty if not transferred
    RefObject* m_clientData;             // Obscure data used by client logics
    bool m_utility;                      // Regular client channel flag
    String m_soundId;                    // The id of the sound to play
    ObjList m_slaves;                    // Data managed by the default logic
    NamedList m_clientParams;            // Channel client parameters
};

/**
 * Abstract client Driver that implements some of the specific functionality
 * @short Base Driver with client specific functions
 */
class YATE_API ClientDriver : public Driver
{
    YCLASS(ClientDriver,Driver)
    friend class ClientChannel;          // Reset active channel's id
    YNOCOPY(ClientDriver);               // No automatic copies please
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
     * @param buildFromChan Build conference name from channel id if true
     * @return True on success
     */
    static bool setConference(const String& id, bool in, const String* confName = 0,
	bool buildFromChan = false);

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
    static inline ClientChannel* findActiveChan()
	{ return self() ? findChan(self()->activeId()) : 0; }

    /**
     * Drop a channel
     * @param chan Channel id
     * @param reason Optional reason
     * @param peer Set it to true to drop a client channel peer used to reconnect
     */
    static void dropChan(const String& chan, const char* reason = 0, bool peer = false);

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
 * @short Base class for all client logics
 */
class YATE_API ClientLogic : public GenObject
{
    YCLASS(ClientLogic,GenObject)
    friend class Client;
    YNOCOPY(ClientLogic); // no automatic copies please
public:
    /**
     * Destructor. Remove itself from the client's list
     */
    virtual ~ClientLogic();

    /**
     * Get the name of this logic
     * @return This logic's name
     */
    inline const String& name() const
	{ return m_name; }

    /**
     * Get the priority of this logic
     * @return This logic's priority
     */
    inline int priority() const
	{ return m_prio; }

    /**
     * Function that returns the name of the logic
     * @return The name of this client logic
     */
    virtual const String& toString() const;

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
    virtual bool action(Window* wnd, const String& name, NamedList* params = 0)
	{ return false; }

    /**
     * Handle actions from checkable widgets
     * @param wnd The window in which the user did something
     * @param name The object's name
     * @param active Object's state
     * @return True if the action was handled by a client logic
     */
    virtual bool toggle(Window* wnd, const String& name, bool active)
	{ return false; }

    /**
     * Handle 'select' actions from user interface
     * @param wnd The window in which the user did something
     * @param name The object's name
     * @param item Item identifying the selection
     * @param text Selection's text
     * @return True if the action was handled
     */
    virtual bool select(Window* wnd, const String& name, const String& item,
	const String& text = String::empty())
	{ return false; }

    /**
     * Handle 'select' with multiple items actions from user interface
     * @param wnd The window in which the user did something
     * @param name The object's name
     * @param items List of selected items
     * @return True if the action was handled
     */
    virtual bool select(Window* wnd, const String& name, const NamedList& items)
	{ return false; }

    /**
     * Set a client's parameter. Save the settings file and/or update interface
     * @param param Parameter's name
     * @param value The value of the parameter
     * @param save True to save the configuration file
     * @param update True to update the interface
     * @return True on success
     */
    virtual bool setClientParam(const String& param, const String& value,
	bool save, bool update)
	{ return false; }

    /**
     * Process an IM message
     * @param msg The im.execute or chan.text message
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
	{ return false; }

    /**
     * Check presence of all necessary data to make a call
     * @param params List of call parameters
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool validateCall(NamedList& params, Window* wnd = 0)
	{ return true; }

    /**
     * Called when the user trigger a call start action
     * The default logic fill the parameter list and ask the client to create an outgoing channel
     * @param params List of call parameters
     * @param wnd Optional window containing the widget that triggered the action
     * @param cmd Optional command (widget name)
     * @return True on success
     */
    virtual bool callStart(NamedList& params, Window* wnd = 0,
	const String& cmd = String::empty())
	{ return false; }

    /**
     * Called when the user selected a line
     * @param name The line name
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool line(const String& name, Window* wnd = 0);

    /**
     * Show/hide widget(s) or window(s) on 'display'/'show' action.
     * @param params Widget(s) or window(s) to show/hide
     * @param widget True if the operation indicates widget(s), false otherwise
     * @param wnd Optional window owning the widget(s) to show or hide
     * @return False if failed to show/hide all widgets/windows
     */
    virtual bool display(NamedList& params, bool widget, Window* wnd = 0);

    /**
     * Erase the last digit from the given widget and set focus on it
     * @param name The widget (it might be the window itself)
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool backspace(const String& name, Window* wnd = 0);

    /**
     * Enqueue an engine.command message
     * @param name The command line
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool command(const String& name, Window* wnd = 0);

    /**
     * Enqueue an engine.debug message.
     * @param name The debug action content (following the prefix). The format
     *  of this parameter must be 'module:active-true:active-false'.
     *  The line parameter of the message will be filled with 'active-true' if
     *  active is true and with 'active-false' if active is false
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
    virtual bool editAccount(bool newAcc, NamedList* params, Window* wnd = 0)
	{ return false; }

    /**
     * Called when the user wants to save account data
     * @param params Initial parameters
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool acceptAccount(NamedList* params, Window* wnd = 0)
	{ return false; }

    /**
     * Called when the user wants to delete an existing account
     * @param account The account's name. Set to empty to delete the current selection
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool delAccount(const String& account, Window* wnd = 0)
	{ return false; }

    /**
     * Add/set an account. Login if required
     * @param account The account's parameters. The name of the list must be the account's name
     * @param login True to login the account
     * @param save True to save the accounts file. If true and file save fails the method will fail
     * @return True on success
     */
    virtual bool updateAccount(const NamedList& account, bool login, bool save)
	{ return false; }

    /**
     * Login/logout an account
     * @param account The account's parameters. The name of the list must be the account's name
     * @param login True to login the account, false to logout
     * @return True on success
     */
    virtual bool loginAccount(const NamedList& account, bool login)
	{ return false; }

    /**
     * Add/set a contact
     * @param contact The contact's parameters. The name of the list must be the contacts's id (name).
     *  If it starts with 'client/' this is a contact updated from server: it can't be changed
     * @param save True to save data to contacts file
     * @param update True to update the interface
     * @return True on success
     */
    virtual bool updateContact(const NamedList& contact, bool save, bool update)
	{ return false; }

    /**
     * Called when the user wants to save account data
     * @param params Initial parameters
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool acceptContact(NamedList* params, Window* wnd = 0)
	{ return false; }

    /**
     * Called when the user wants to add a new contact or edit an existing one
     * @param newCont True to add a new contact, false to edit an existing one
     * @param params Optional initial parameters
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool editContact(bool newCont, NamedList* params = 0, Window* wnd = 0)
	{ return false; }

    /**
     * Called when the user wants to delete an existing contact
     * @param contact The contact's id. Set to empty to delete the current selection
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool delContact(const String& contact, Window* wnd = 0)
	{ return false; }

    /**
     * Called when the user wants to call an existing contact
     * @param params Optional parameters
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool callContact(NamedList* params = 0, Window* wnd = 0)
	{ return false; }

    /**
     * Add/set account providers data
     * @param provider The provider's parameters. The name of the list must be the provider's id (name)
     * @param save True to save data to providers file
     * @param update True to update the interface
     * @return True on success
     */
    virtual bool updateProviders(const NamedList& provider, bool save, bool update)
	{ return false; }

    /**
     * Update the call log history
     * @param params Call log data
     * @param save True to save data to history file
     * @param update True to update the interface
     * @return True
    */
    virtual bool callLogUpdate(const NamedList& params, bool save, bool update)
	{ return false; }

    /**
     * Remove a call log item
     * @param billid The bill id of the call
     * @return True on success
    */
    virtual bool callLogDelete(const String& billid)
	{ return false; }

    /**
     * Clear the specified log and the entries from the history file and save the history file
     * @param table Tebale to clear
     * @param direction The call direction to clear (incoming,outgoing).
     *  Note that the direction is the value saved from call.cdr messages.
     *  If empty, all log entries will be cleared
     * @return True
    */
    virtual bool callLogClear(const String& table, const String& direction)
	{ return false; }

    /**
     * Make an outgoing call to a target picked from the call log
     * @param billid The bill id of the call
     * @param wnd Optional window starting the action
     * @return True on success (call initiated)
    */
    virtual bool callLogCall(const String& billid, Window* wnd = 0)
	{ return false; }

    /**
     * Create a contact from a call log entry
     * @param billid The bill id of the call
     * @return True on success (address book popup window was displayed)
    */
    virtual bool callLogCreateContact(const String& billid)
	{ return false; }

    /**
     * Process help related actions
     * @param action The action's name
     * @param wnd The window owning the control
     * @return True on success
     */
    virtual bool help(const String& action, Window* wnd)
	{ return false; }

    /**
     * Called by the client after loaded the callto history file
     * @return True to tell the client to stop notifying other logics
     */
    virtual bool calltoLoaded()
	{ return false; }

    /**
     * Called by the client after loaded the windows
     */
    virtual void loadedWindows()
	{}

    /**
     * Called by the client after loaded and intialized the windows
     */
    virtual void initializedWindows()
	{}

    /**
     * Called by the client after loaded and intialized the windows and
     *  loaded configuration files.
     * The default logic update client settings
     * @return True to stop processing this notification
     */
    virtual bool initializedClient()
	{ return false; }

    /**
     * Called by the client before exiting.
     * The default logic save client settings
     */
    virtual void exitingClient()
	{}

    /**
     * Process ui.action message
     * @param msg Received message
     * @param stopLogic Set to true on exit to tell the client to stop asking other logics
     * @return True to stop further processing by the engine
     */
    virtual bool handleUiAction(Message& msg, bool& stopLogic)
	{ return false; }

    /**
     * Process call.cdr message
     * @param msg Received message
     * @param stopLogic Set to true on exit to tell the client to stop asking other logics
     * @return True to stop further processing by the engine
     */
    virtual bool handleCallCdr(Message& msg, bool& stopLogic)
	{ return false; }

    /**
     * Process user.login message
     * @param msg Received message
     * @param stopLogic Set to true on exit to tell the client to stop asking other logics
     * @return True to stop further processing by the engine
     */
    virtual bool handleUserLogin(Message& msg, bool& stopLogic)
	{ return false; }

    /**
     * Process user.notify message
     * @param msg Received message
     * @param stopLogic Set to true on exit to tell the client to stop asking other logics
     * @return True to stop further processing by the engine
     */
    virtual bool handleUserNotify(Message& msg, bool& stopLogic)
	{ return false; }

    /**
     * Process user.roster message
     * @param msg Received message
     * @param stopLogic Set to true on exit to tell the client to stop asking other logics
     * @return True to stop further processing by the engine
     */
    virtual bool handleUserRoster(Message& msg, bool& stopLogic)
	{ return false; }

    /**
     * Process resource.notify message
     * @param msg Received message
     * @param stopLogic Set to true on exit to tell the client to stop asking other logics
     * @return True to stop further processing by the engine
     */
    virtual bool handleResourceNotify(Message& msg, bool& stopLogic)
	{ return false; }

    /**
     * Process resource.subscribe message
     * @param msg Received message
     * @param stopLogic Set to true on exit to tell the client to stop asking other logics
     * @return True to stop further processing by the engine
     */
    virtual bool handleResourceSubscribe(Message& msg, bool& stopLogic)
	{ return false; }

    /**
     * Process clientchan.update message
     * @param msg Received message
     * @param stopLogic Set to true on exit to tell the client to stop asking other logics
     * @return True to stop further processing by the engine
     */
    virtual bool handleClientChanUpdate(Message& msg, bool& stopLogic)
	{ return false; }

    /**
     * Process contact.info message
     * @param msg Received message
     * @param stopLogic Set to true on exit to tell the client to stop asking other logics
     * @return True to stop further processing by the engine
     */
    virtual bool handleContactInfo(Message& msg, bool& stopLogic)
	{ return false; }

    /**
     * Default message processor called for id's not defined in client.
     * Descendants may override it to process custom messages installed by
     *  them and relayed through the client
     * @param msg Received message
     * @param id Message id
     * @param stopLogic Set to true on exit to tell the client to stop asking other logics
     * @return True to stop further processing by the engine
     */
    virtual bool defaultMsgHandler(Message& msg, int id, bool& stopLogic)
	{ return false; }

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
    virtual bool addDurationUpdate(DurationUpdate* duration, bool autoDelete = false);

    /**
     * Remove a duration object from list
     * @param name The name of the object to remove
     * @param delObj True to destroy the object, false to remove it
     * @return True on success
     */
    virtual bool removeDurationUpdate(const String& name, bool delObj = false);

    /**
     * Remove a duration object from list
     * @param duration The object to remove
     * @param delObj True to destroy the object, false to remove it
     * @return True on success
     */
    virtual bool removeDurationUpdate(DurationUpdate* duration, bool delObj = false);

    /**
     * Find a duration update by its name
     * @param name The name of the object to find
     * @param ref True to increase its reference counter before returning
     * @return DurationUpdate pointer or 0
     */
    virtual DurationUpdate* findDurationUpdate(const String& name, bool ref = true);

    /**
     * Remove all duration objects
     */
    virtual void clearDurationUpdate();

    /**
     * Release memory. Remove from client's list
     */
    virtual void destruct();

    /**
     * Retrieve the remote party from CDR parameters
     * @param params CDR parameters
     * @param outgoing True if the call was an outgoing one
     * @return The remote party (may be empty)
     */
    static const String& cdrRemoteParty(const NamedList& params, bool outgoing)
	{ return outgoing ? params[YSTRING("called")] : params[YSTRING("caller")]; }

    /**
     * Retrieve the remote party from CDR parameters
     * @param params CDR parameters
     * @return The remote party (may be empty)
     */
    static const String& cdrRemoteParty(const NamedList& params) {
	    const String& dir = params[YSTRING("direction")];
	    if (dir == YSTRING("incoming"))
		return cdrRemoteParty(params,true);
	    if (dir == YSTRING("outgoing"))
		return cdrRemoteParty(params,false);
	    return String::empty();
	}

    /**
     * Init static logic lists.
     * Called by the client when start running
     */
    static void initStaticData();

    /**
     * Save a contact into a configuration file
     * @param cfg The configuration file
     * @param c The contact to save
     * @param save True to save the file
     * @return True on success
     */
    static bool saveContact(Configuration& cfg, ClientContact* c, bool save = true);

    /**
     * Delete a contact from a configuration file
     * @param cfg The configuration file
     * @param c The contact to delete
     * @param save True to save the file
     * @return True on success
     */
    static bool clearContact(Configuration& cfg, ClientContact* c, bool save = true);

    // Account options string list
    static ObjList s_accOptions;
    // Parameters that are applied from provider template
    static const char* s_provParams[];
    // The list of protocols supported by the client
    static ObjList s_protocols;
    // Mutext used to lock protocol list
    static Mutex s_protocolsMutex;

protected:
    /**
     * Constructor. Append itself to the client's list
     * @param name The name of this logic (module)
     * @param priority The priority of this logic
     */
    ClientLogic(const char* name, int priority);

    /**
     * Method called by the client when idle.
     * This method is called in the UI's thread
     * @param time The current time
     */
    virtual void idleTimerTick(Time& time)
	{}

    ObjList m_durationUpdate;            // Duration updates
    Mutex m_durationMutex;               // Lock duration operations

private:
    ClientLogic() {}                     // No default constructor

    String m_name;                       // Logic's name
    int m_prio;                          // Logics priority
};

class FtManager;

/**
 * This class implements the default client behaviour.
 * @short The client's default logic
 */
class YATE_API DefaultLogic : public ClientLogic
{
    YCLASS(DefaultLogic,ClientLogic)
    YNOCOPY(DefaultLogic); // no automatic copies please
public:
    /**
     * Constructor
     * @param name The name of this logic
     * @param prio The priority of this logic
     */
    explicit DefaultLogic(const char* name = "default", int prio = -100);

    /**
     * Destructor
     */
    ~DefaultLogic();

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
     * Handle 'select' with multiple items actions from user interface
     * @param wnd The window in which the user did something
     * @param name The object's name
     * @param items List of selected items
     * @return True if the action was handled
     */
    virtual bool select(Window* wnd, const String& name, const NamedList& items);

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
    virtual bool imIncoming(Message& msg);

    /**
     * Call execute handler called by the client.
     * The default logic ask the client to build an incoming channel
     * @param msg The call.execute message
     * @param dest The destination (target)
     * @return True if a channel was created and connected
     */
    virtual bool callIncoming(Message& msg, const String& dest);

    /**
     * Check presence of all necessary data to make a call
     * @param params List of call parameters
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool validateCall(NamedList& params, Window* wnd = 0);

    /**
     * Called when the user trigger a call start action
     * The default logic fill the parameter list and ask the client to create an outgoing channel
     * @param params List of call parameters
     * @param wnd Optional window containing the widget that triggered the action
     * @param cmd Optional command (widget name)
     * @return True on success
     */
    virtual bool callStart(NamedList& params, Window* wnd = 0,
	const String& cmd = String::empty());

    /**
     * Called when a digit is pressed. The default logic will send the digit(s)
     *  as DTMFs on the active channel
     * @param params List of parameters. It should contain a 'digits' parameter
     * @param wnd Optional window containing the widget that triggered the action
     * @return True on success
     */
    virtual bool digitPressed(NamedList& params, Window* wnd = 0);

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
     * Add/set an account
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
    virtual bool callLogUpdate(const NamedList& params, bool save, bool update);

    /**
     * Remove a call log item
     * @param billid The bill id of the call
     * @return True on success
    */
    virtual bool callLogDelete(const String& billid);

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
     * @param wnd Optional window starting the action
     * @return True on success (call initiated)
    */
    virtual bool callLogCall(const String& billid, Window* wnd = 0);

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
     * Process user.roster message
     * @param msg Received message
     * @param stopLogic Set to true on exit to tell the client to stop asking other logics
     * @return True to stop further processing by the engine
     */
    virtual bool handleUserRoster(Message& msg, bool& stopLogic);

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
     * Process clientchan.update message
     * @param msg Received message
     * @param stopLogic Set to true on exit to tell the client to stop asking other logics
     * @return True to stop further processing by the engine
     */
    virtual bool handleClientChanUpdate(Message& msg, bool& stopLogic);

    /**
     * Process contact.info message
     * @param msg Received message
     * @param stopLogic Set to true on exit to tell the client to stop asking other logics
     * @return True to stop further processing by the engine
     */
    virtual bool handleContactInfo(Message& msg, bool& stopLogic);

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
     * Engine start notification. Connect startup accounts
     * @param msg The engine.start message
     */
    virtual void engineStart(Message& msg);

    /**
     * Show incoming call notification for a given channel
     * @param chan The channel
     */
    virtual void showInCallNotification(ClientChannel* chan);

    /**
     * Close incoming call notification for a given id
     * @param id The notification id to close
     */
    virtual void closeInCallNotification(const String& id);

    /**
     * Build an account id from protocol, username, host
     * @param accId Destination string
     * @param proto Account protocol
     * @param user Account username
     * @param host Account host
     * @return Destination string address
     */
    static inline String& buildAccountId(String& accId, const String& proto,
	const String& user, const String& host) {
	    accId = proto + ":" + user + "@" + host;
	    return accId;
	}

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

    /**
     * Fill contact edit/delete active parameters
     * @param list Destination list
     * @param active True to activate, false to deactivate
     * @param item Optional selected item to check in contacts list if active
     * @param del True to fill delete active parameter
     */
    virtual void fillContactEditActive(NamedList& list, bool active, const String* item = 0,
	bool del = true);

    /**
     * Fill log contact active parameter
     * @param list Destination list
     * @param active True to activate, false to deactivate
     * @param item Optional selected item to check in calls log list if active
     */
    virtual void fillLogContactActive(NamedList& list, bool active, const String* item = 0);

    /**
     * Clear a list/table. Handle specific lists like CDR, accounts, contacts
     * @param action The list. May contain an optional confirmation text to display.
     *  Format: 'list_name[:confirmation_text]'
     * @param wnd Window owning the list/table
     * @return True on success
     */
    virtual bool clearList(const String& action, Window* wnd);

    /**
     * Delete a list/table item. Handle specific lists like CDR, accounts, contacts, mucs
     * @param list The list
     * @param item Item to delete
     * @param wnd Window owning the list/table
     * @param confirm Request confirmation for known list
     * @return True on success
     */
    virtual bool deleteItem(const String& list, const String& item, Window* wnd,
	bool confirm);

    /**
     * Handle list/table checked items deletion.
     * Handle specific lists like CDR, accounts, contacts
     * @param list The list
     * @param wnd Window owning the list/table
     * @param confirm Request confirmation for known list
     * @return True on success
     */
    virtual bool deleteCheckedItems(const String& list, Window* wnd, bool confirm);

    /**
     * Handle list/table selection or checked items deletion.
     * Handle specific lists like CDR, accounts, contacts
     * @param action Action to handle. May contain an optional confirmation text to display.
     *  Format: 'list_name[:confirmation_text]'
     * @param wnd Window owning the list/table
     * @param checked Set it to true to handle checked items deletion
     * @return True on success
     */
    virtual bool deleteSelectedItem(const String& action, Window* wnd, bool checked = false);

    /**
     * Handle text changed notification
     * @param params Notification parameters
     * @param wnd Window notifying the event
     * @return True if handled
     */
    virtual bool handleTextChanged(NamedList* params, Window* wnd);

    /**
     * Handle file transfer actions
     * @param name Action name
     * @param wnd Window notifying the event
     * @param params Optional action parameters
     * @return True if handled
     */
    virtual bool handleFileTransferAction(const String& name, Window* wnd, NamedList* params = 0);

    /**
     * Handle file transfer notifications.
     * This method is called from logic message handler
     * @param msg Notification message
     * @param stopLogic Stop notifying other logics if set to true on return
     * @return True if handled
     */
    virtual bool handleFileTransferNotify(Message& msg, bool& stopLogic);

    /**
     * Handle user.data messages.
     * @param msg The message
     * @param stopLogic Stop notifying other logics if set to true on return
     * @return True if handled
     */
    virtual bool handleUserData(Message& msg, bool& stopLogic);

    /**
     * Handle file.info messages.
     * @param msg The message
     * @param stopLogic Stop notifying other logics if set to true on return
     * @return True if handled
     */
    virtual bool handleFileInfo(Message& msg, bool& stopLogic);

    /**
     * Show a generic notification
     * @param text Notification text
     * @param account Optional concerned account
     * @param contact Optional concerned contact
     * @param title Notification title
     */
    virtual void notifyGenericError(const String& text,
	const String& account = String::empty(),
	const String& contact = String::empty(),
	const char* title = "Error");

    /**
     * Show/hide no audio notification
     * @param show Show or hide notification
     * @param micOk False if microphone open failed
     * @param speakerOk False if speaker open failed
     * @param chan Optional failed channel
     */
    virtual void notifyNoAudio(bool show, bool micOk = false, bool speakerOk = false,
	ClientChannel* chan = 0);

    /**
     * (Un)Load account's saved chat rooms or a specific room in contact list
     * @param load True to load, false to unload
     * @param acc The account owning the chat rooms
     * @param room The room to update, ignored if acc is not 0
     */
    virtual void updateChatRoomsContactList(bool load, ClientAccount* acc,
	MucRoom* room = 0);

    /**
     * Join a MUC room. Create/show chat. Update its status
     * @param room The room
     * @param force True to disconnect if connecting or online and re-connect
     */
    virtual void joinRoom(MucRoom* room, bool force = false);

    String m_selectedChannel;            // The currently selected channel
    String m_transferInitiated;          // Tranfer initiated id

private:
    // Add/set an account changed in UI
    // replace: Optional editing account to replace
    bool updateAccount(const NamedList& account, bool save,
	const String& replace = String::empty(), bool loaded = false);
    // Add/edit an account
    bool internalEditAccount(bool newAcc, const String* account, NamedList* params, Window* wnd);
    // Handle dialog actions. Return true if handled
    bool handleDialogAction(const String& name, bool& retVal, Window* wnd);
    // Handle chat and contact related actions. Return true if handled
    bool handleChatContactAction(const String& name, Window* wnd);
    // Handle chat contact edit ok button press. Return true if handled
    bool handleChatContactEditOk(const String& name, Window* wnd);
    // Handle chat room contact edit ok button press. Return true if handled
    bool handleChatRoomEditOk(const String& name, Window* wnd);
    // Handle actions from MUCS window. Return true if handled
    bool handleMucsAction(const String& name, Window* wnd, NamedList* params);
    // Handle ok button in muc invite window. Return true if handled
    bool handleMucInviteOk(Window* wnd);
    // Handle select from MUCS window. Return true if handled
    bool handleMucsSelect(const String& name, const String& item, Window* wnd,
	const String& text = String::empty());
    // Handle resource.notify messages from MUC rooms
    // The account was already checked
    bool handleMucResNotify(Message& msg, ClientAccount* acc, const String& contact,
	const String& instance, const String& operation);
    // Show/hide the notification area (messages).
    // Update rows if requested. Add/remove tray notification/info icon
    bool showNotificationArea(bool show, Window* wnd, NamedList* upd = 0,
	const char* notif = "notification");
    // Show a roster change or failure notification
    void showUserRosterNotification(ClientAccount* a, const String& oper,
	Message& msg, const String& contactUri = String::empty(),
	bool newContact = true);
    // Handle actions from notification area. Return true if handled
    bool handleNotificationAreaAction(const String& action, Window* wnd);
    // Save a contact to config. Save chat rooms if the contact is a chat room
    bool storeContact(ClientContact* c);
    // Handle ok from account password/credentials input window
    bool handleAccCredInput(Window* wnd, const String& name, bool inputPwd);
    // Handle channel show/hide transfer/conference toggles
    bool handleChanShowExtra(Window* wnd, bool show, const String& chan, bool conf);
    // Handle conf/transfer start actions in channel item
    bool handleChanItemConfTransfer(bool conf, const String& name, Window* wnd);
    // Handle file share(d) related action
    bool handleFileShareAction(Window* wnd, const String& name, NamedList* params);
    // Handle file share(d) related select
    bool handleFileShareSelect(Window* wnd, const String& name, const String& item,
	const String& text, const NamedList* items);
    // Handle file share(d) item changes from UI
    bool handleFileShareItemChanged(Window* wnd, const String& name, const String& item,
	const NamedList& params);
    // Handle file share(d) drop events
    bool handleFileShareDrop(bool askOnly, Window* wnd, const String& name,
	NamedList& params, bool& retVal);
    // Handle list item change action
    bool handleListItemChanged(Window* wnd, const String& list, const String& item,
	const NamedList& params);
    // Handle drop events
    bool handleDrop(bool askOnly, Window* wnd, const String& name,
	NamedList& params);
    // Handle file share info changed notification
    void handleFileSharedChanged(ClientAccount* a, const String& contact,
	const String& inst);

    ClientAccountList* m_accounts;       // Accounts list (always valid)
    FtManager* m_ftManager;              // Private file manager
};


/**
 * This class holds an account
 * @short An account
 */
class YATE_API ClientAccount : public RefObject, public Mutex
{
    friend class ClientContact;
    friend class MucRoom;
    YCLASS(ClientAccount,RefObject)
    YNOCOPY(ClientAccount); // no automatic copies please
public:
    /**
     * Constructor
     * @param proto The account's protocol
     * @param user The account's username
     * @param host The account's host
     * @param startup True if the account should login at startup
     * @param contact Optional account's own contact
     */
    explicit ClientAccount(const char* proto, const char* user, const char* host,
	bool startup, ClientContact* contact = 0);

    /**
     * Constructor. Build an account from a list of parameters
     * @param params The list of parameters used to build this account.
     *  The list's name will be used as account id
     * @param contact Optional account's own contact
     */
    explicit ClientAccount(const NamedList& params, ClientContact* contact = 0);

    /**
     * Get this account's parameters
     * @return This account's parameter list
     */
    inline const NamedList& params() const
	{ return m_params; }

    /**
     * Get this account's contacts. The caller should lock the account while browsing the list
     * @return This account's contacts list
     */
    inline ObjList& contacts()
	{ return m_contacts; }

    /**
     * Get this account's muc rooms. The caller should lock the account while browsing the list
     * @return This account's mucs list
     */
    inline ObjList& mucs()
	{ return m_mucs; }

    /**
     * Retrieve account own contact
     * @return ClientContact pointer
     */
    inline ClientContact* contact() const
	{ return m_contact; }

    /**
     * Set or reset account own contact
     * @param contact New account contact (may be NULL to reset it)
     */
    void setContact(ClientContact* contact);

    /**
     * Retrieve the account's protocol
     * @return The account's protocol
     */
    inline const String& protocol() const
	{ return m_params[YSTRING("protocol")]; }

    /**
     * Check if the account's protocol has chat support
     * @return True if this account has chat support
     */
    inline bool hasChat() const
	{ return protocol() == YSTRING("jabber"); }

    /**
     * Check if the account's protocol has presence support
     * @return True if this account has presence support
     */
    inline bool hasPresence() const
	{ return protocol() == YSTRING("jabber"); }

    /**
     * Check if the account should be logged in at startup
     * @return True if the account should be logged in at startup
     */
    inline bool startup() const
	{ return m_params.getBoolValue(YSTRING("enabled"),true); }

    /**
     * Set the account's startup login flag
     * @param ok The account's startup login flag value
     */
    inline void startup(bool ok)
	{ m_params.setParam("enabled",String::boolText(ok)); }

    /**
     * Get a string representation of this object
     * @return The account's compare id
     */
    virtual const String& toString() const
	{ return m_params; }

    /**
     * Get this account's resource
     * @return ClientResource pointer
     */
    ClientResource* resource(bool ref);

    /**
     * Get this account's resource
     * @return ClientResource reference
     */
    inline ClientResource& resource() const
	{ return *m_resource; }

    /**
     * Set this account's resource
     * @param res The new account's resource (ignored if 0)
     */
    void setResource(ClientResource* res);

    /**
     * Save or remove this account to/from client accounts file.
     * Parameters starting with "internal." are not saved
     * @param ok True to save, false to remove
     * @param savePwd True to save the password
     * @return True on success
     */
    bool save(bool ok = true, bool savePwd = true);

    /**
     * Find a contact by its id
     * @param id The id of the desired contact
     * @param ref True to obtain a referenced pointer
     * @return ClientContact pointer (may be account's own contact) or 0 if not found
     */
    virtual ClientContact* findContact(const String& id, bool ref = false);

    /**
     * Find a contact by name and/or uri. Account own contact is ignored
     * @param name Optional name to check (may be a pointer to an empty string)
     * @param uri Optional uri to check (may be a pointer to an empty string)
     * @param skipId Optional contact to skip
     * @param ref True to obtain a referenced pointer
     * @return ClientContact pointer or 0 if not found
     */
    virtual ClientContact* findContact(const String* name = 0, const String* uri = 0,
	const String* skipId = 0, bool ref = false);

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
     * Find a contact by its URI (build an id from account and uri)
     * @param uri The contact's uri
     * @param ref True to get a referenced pointer
     * @return ClientContact pointer or 0 if not found
     */
    virtual ClientContact* findContactByUri(const String& uri, bool ref = false);

    /**
     * Find a MUC room by its id
     * @param id Room id
     * @param ref True to obtain a referenced pointer
     * @return MucRoom pointer or 0 if not found
     */
    virtual MucRoom* findRoom(const String& id, bool ref = false);

    /**
     * Find a MUC room by its uri
     * @param uri Room uri
     * @param ref True to obtain a referenced pointer
     * @return MucRoom pointer or 0 if not found
     */
    virtual MucRoom* findRoomByUri(const String& uri, bool ref = false);

    /**
     * Find any contact (regular or MUC room) by its id
     * @param id The id of the desired contact
     * @param ref True to obtain a referenced pointer
     * @return ClientContact pointer (may be account's own contact) or 0 if not found
     */
    virtual ClientContact* findAnyContact(const String& id, bool ref = false);

    /**
     * Build a contact and append it to the list
     * @param id The contact's id
     * @param name The contact's name
     * @param uri Optional contact URI
     * @return ClientContact pointer or 0 if a contact with the given id already exists
     */
    virtual ClientContact* appendContact(const String& id, const char* name,
	const char* uri = 0);

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
     * Clear MUC rooms. This method is thread safe
     * @param saved True to clear saved rooms
     * @param temp True to clear temporary rooms
     */
    virtual void clearRooms(bool saved, bool temp);

    /**
     * Build a login/logout message from account's data
     * @param login True to login, false to logout
     * @param msg Optional message name. Default to 'user.login'
     * @return A valid Message pointer
     */
    virtual Message* userlogin(bool login, const char* msg = "user.login");

    /**
     * Build a message used to update or query account userdata.
     * Add account MUC rooms if data is 'chatrooms' and update
     * @param update True to update, false to query
     * @param data Data to update or query
     * @param msg Optional message name. Default to 'user.data'
     * @return A valid Message pointer
     */
    virtual Message* userData(bool update, const String& data,
	const char* msg = "user.data");

    /**
     * Fill a list used to update a account's list item
     * @param list Parameter list to fill
     */
    virtual void fillItemParams(NamedList& list);

    /**
     * Retrieve account data directory
     * @return Account data directory
     */
    inline const String& dataDir() const
	{ return m_params[YSTRING("datadirectory")]; }

    /**
     * Set account directory in application data directory. Make sure it exists.
     * Move all files from the old one if changed
     * @param errStr Optional string to be filled with error string
     * @param saveAcc Save data directory parameter in client accounts
     * @return True on success
     */
    virtual bool setupDataDir(String* errStr = 0, bool saveAcc = true);

    /**
     * Load configuration file from data directory
     * @param cfg Optional configuration file to load.
     *  Load account's conf file if 0
     * @param file File name. Defaults to 'account.conf'
     * @return True on success
     */
    virtual bool loadDataDirCfg(Configuration* cfg = 0,
	const char* file = "account.conf");

    /**
     * Load contacts from configuration file
     * @param cfg Optional configuration file to load.
     *  Load from account's conf file if 0
     */
    virtual void loadContacts(Configuration* cfg = 0);

    /**
     * Clear account data directory
     * @param errStr Optional string to be filled with error string
     * @return True if all files were succesfully removed
     */
    virtual bool clearDataDir(String* errStr = 0);

    NamedList m_params;                  // Account parameters
    Configuration m_cfg;                 // Account conf file

protected:
    // Remove from owner. Release data
    virtual void destroyed();
    // Method used by the contact to append itself to this account's list
    virtual void appendContact(ClientContact* contact, bool muc = false);

    ObjList m_contacts;                  // Account's contacts
    ObjList m_mucs;                      // Account's MUC contacts

private:
    ClientResource* m_resource;          // Account's resource
    ClientContact* m_contact;            // Account's contact data
};

/**
 * This class holds an account list
 * @short A client account list
 */
class YATE_API ClientAccountList : public String, public Mutex
{
    YCLASS(ClientAccountList,String)
    YNOCOPY(ClientAccountList); // no automatic copies please
public:
    /**
     * Constructor
     * @param name List's name used for debug purposes
     * @param localContacts Optional account owning locally stored contacts
     */
    inline explicit ClientAccountList(const char* name, ClientAccount* localContacts = 0)
	: String(name), Mutex(true,"ClientAccountList"),
	  m_localContacts(localContacts)
	{ }

    /**
     * Destructor
     */
    ~ClientAccountList();

    /**
     * Get the accounts list
     * @return The accounts list
     */
    inline ObjList& accounts()
	{ return m_accounts; }

    /**
     * Retrieve the account owning locally stored contacts
     * @return ClientAccount pointer or 0
     */
    inline ClientAccount* localContacts() const
	{ return m_localContacts; }

    /**
     * Check if a contact is locally stored
     * @param c The contact to check
     * @return True if the contact owner is the account owning locally stored contacts
     */
    bool isLocalContact(ClientContact* c) const;

    /**
     * Check if a contact is locally stored
     * @param id Contact id to check
     * @return True if the contact owner is the account owning locally stored contacts
     */
    inline bool isLocalContact(const String& id) const
	{ return m_localContacts && m_localContacts->findContact(id); }

    /**
     * Find an account
     * @param id The account's id
     * @param ref True to get a referenced pointer
     * @return ClientAccount pointer or 0 if not found
     */
    virtual ClientAccount* findAccount(const String& id, bool ref = false);

    /**
     * Find an account's contact by its URI (build an id from account and uri)
     * @param account The account's id
     * @param uri The contact's uri
     * @param ref True to get a referenced pointer
     * @return ClientContact pointer or 0 if not found
     */
    virtual ClientContact* findContactByUri(const String& account, const String& uri,
	bool ref = false);

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
     * Find a contact an instance id
     * @param id The id
     * @param instance Optional pointer to String to be filled with instance id
     * @param ref True to get a referenced pointer
     * @return ClientContact pointer or 0 if not found
     */
    virtual ClientContact* findContactByInstance(const String& id, String* instance = 0,
	bool ref = false);

    /**
     * Find a MUC room by its id
     * @param id Room id
     * @param ref True to obtain a referenced pointer
     * @return MucRoom pointer or 0 if not found
     */
    virtual MucRoom* findRoom(const String& id, bool ref = false);

    /**
     * Find a MUC room by member id
     * @param id Room member id
     * @param ref True to obtain a referenced pointer
     * @return MucRoom pointer or 0 if not found
     */
    virtual MucRoom* findRoomByMember(const String& id, bool ref = false);

    /**
     * Find any contact (regular or MUC room) by its id
     * @param id The id of the desired contact
     * @param ref True to obtain a referenced pointer
     * @return ClientContact pointer (may be account's own contact) or 0 if not found
     */
    virtual ClientContact* findAnyContact(const String& id, bool ref = false);

    /**
     * Check if there is a single registered account and return it
     * @param skipProto Optional account protocol to skip
     * @param ref True to get a referenced pointer
     * @return ClientAccount pointer or 0 if not found
     */
    virtual ClientAccount* findSingleRegAccount(const String* skipProto = 0,
	bool ref = false);

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

private:
    ClientAccountList() {}               // Avoid using the default constructor
    ClientAccount* m_localContacts;      // Account owning locally stored contacts
};

/**
 * A client contact
 * The contact is using the owner's mutex to lock it's operations
 * @short A client contact
 */
class YATE_API ClientContact : public RefObject
{
    friend class ClientAccount;
    YCLASS(ClientContact,RefObject)
    YNOCOPY(ClientContact); // no automatic copies please
public:
    /**
     * Subscription flags
     */
    enum Subscription {
	SubFrom = 0x01,
	SubTo = 0x02,
    };

    /**
     * Constructor. Append itself to the owner's list
     * @param owner The contact's owner
     * @param id The contact's id
     * @param name Optional display name. Defaults to the id's value if 0
     * @param uri Optional contact URI
     */
    explicit ClientContact(ClientAccount* owner, const char* id, const char* name = 0,
	const char* uri = 0);

    /**
     * Constructor. Build a contact from a list of parameters.
     * Append itself to the owner's list
     * @param owner The contact's owner
     * @param params The list of parameters used to build this contact
     * @param id Optional contact id
     * @param uri Optional contact URI
     */
    explicit ClientContact(ClientAccount* owner, const NamedList& params, const char* id = 0,
	const char* uri = 0);

    /**
     * Get this contact's account
     * @return This contact's account
     */
    inline ClientAccount* account()
	{ return m_owner; }

    /**
     * Get this contact account's name (id)
     * @return This contact account name (id) or an empty string if none
     */
    inline const String& accountName() const
	{ return m_owner ? m_owner->toString() : String::empty(); }

    /**
     * Get this contact's URI
     * @return This contact's URI
     */
    inline const URI& uri() const
	{ return m_uri; }

    /**
     * Set this contact's URI
     * @param u New contact URI
     */
    inline void setUri(const char* u)
	{ m_uri = u; }

    /**
     * Retrieve contact subscription
     * @return Contact subscription string
     */
    inline const String& subscriptionStr() const
	{ return m_subscription; }

    /**
     * Check if contact is subscribed to our presence
     * @return True if contact is subscribed to our presence
     */
    inline bool subscriptionFrom() const
	{ return 0 != m_sub.flag(SubFrom); }

    /**
     * Check we are subscribed to contact's presence
     * @return True if we are subscribed to contact's presence
     */
    inline bool subscriptionTo() const
	{ return 0 != m_sub.flag(SubTo); }

    /**
     * Set contact's subscription
     * @param value Subscription value
     * @return True if changed
     */
    bool setSubscription(const String& value);

    /**
     * Get the resource list of this contact
     * @return The resource list of this contact
     */
    inline ObjList& resources()
	{ return m_resources; }

    /**
     * Check if the contact is online (the online flag is set or has at least 1 resource in list)
     * @return True if the contact is online
     */
    inline bool online() const
	{ return m_online || 0 != m_resources.skipNull(); }

    /**
     * Set the online flag
     * @param on The new value for online flag
     */
    inline void setOnline(bool on)
	{ m_online = on; }

    /**
     * Get the group list of this contact
     * @return The group list of this contact
     */
    inline ObjList& groups()
	{ return m_groups; }

    /**
     * Check if the contact is locally saved
     * @param defVal Default value to return if parameter is invalid
     * @return True if the contact is locally saved
     */
    inline bool local(bool defVal = false) const
	{ return m_params.getBoolValue(YSTRING("local"),defVal); }

    /**
     * Set contact locally saved flag
     * @param on The new value for locally saved flag
     */
    inline void setLocal(bool on)
	{ m_params.setParam("local",String::boolText(on)); }

    /**
     * Check if the contact is saved on server
     * @param defVal Default value to return if parameter is invalid
     * @return True if the contact is saved on server
     */
    inline bool remote(bool defVal = false) const
	{ return m_params.getBoolValue(YSTRING("remote"),defVal); }

    /**
     * Set contact server saved flag
     * @param on The new value for server saved flag
     */
    inline void setRemote(bool on)
	{ m_params.setParam("remote",String::boolText(on)); }

    /**
     * Set/reset the docked chat flag for non MucRoom contact
     * @param on The new value for docked chat flag
     */
    inline void setDockedChat(bool on) {
	    if (!mucRoom())
		m_dockedChat = on;
	}

    /**
     * Remove account prefix from contact id and URI unescape the result
     * @param buf Destination buffer
     */
    inline void getContactSection(String& buf) {
	    String pref;
	    buf = toString();
	    buf.startSkip(buildContactId(pref,accountName(),String::empty()),false);
	    buf = buf.uriUnescape();
	}

    /**
     * Get a string representation of this object
     * @return The contact's id
     */
    virtual const String& toString() const
	{ return m_id; }

    /**
     * Return a MucRoom contact from this one
     * @return MucRoom pointer or 0
     */
    virtual MucRoom* mucRoom()
	{ return 0; }

    /**
     * Build a contact instance id to be used in UI
     * @param dest Destination string
     * @param inst Instance name
     * @return Destination string
     */
    inline String& buildInstanceId(String& dest, const String& inst = String::empty())
	{ return buildContactInstanceId(dest,m_id,inst); }

    /**
     * Build a string from prefix and contact id hash
     * @param buf Destination string
     * @param prefix Optional prefix
     */
    inline void buildIdHash(String& buf, const String& prefix = String::empty()) {
	    MD5 md5(m_id);
	    buf = prefix + md5.hexDigest();
	}

    /**
     * Check if a window is this contact's chat
     * @param wnd The window to check
     * @return True if the given window is this contact's chat
     */
    inline bool isChatWnd(Window* wnd)
	{ return wnd && wnd->toString() == m_chatWndName; }

    /**
     * Check if this contact has a chat widget (window or docked item)
     * @return True if this contact has a chat window or docked item
     */
    bool hasChat();

    /**
     * Flash chat window/item to notify the user
     * @param on True to start, false to stop flashing
     */
    virtual void flashChat(bool on = true);

    /**
     * Send chat to contact (enqueue a msg.execute message)
     * @param body Chat body
     * @param res Optional target instance
     * @param type Optional message type parameter
     * @param state Optional chat state
     * @return True on success
     */
    virtual bool sendChat(const char* body, const String& res = String::empty(),
	const String& type = String::empty(), const char* state = "active");

    /**
     * Retrieve the contents of the chat input widget
     * @param text Chat input text
     * @param name Chat input widget name
     */
    virtual void getChatInput(String& text, const String& name = "message");

    /**
     * Set the chat input widget text
     * @param text Chat input text
     * @param name Chat input widget name
     */
    virtual void setChatInput(const String& text = String::empty(),
	const String& name = "message");

    /**
     * Retrieve the contents of the chat history widget
     * @param text Chat history text
     * @param richText Retrieve rich/plain text flag
     * @param name Chat history widget name
     */
    virtual void getChatHistory(String& text, bool richText = false,
	const String& name = "history");

    /**
     * Set the contents of the chat history widget
     * @param text Chat history text
     * @param richText Set rich/plain text flag
     * @param name Chat history widget name
     */
    virtual void setChatHistory(const String& text, bool richText = false,
	const String& name = "history");

    /**
     * Add an entry to chat history
     * @param what Item to add (chat_in, chat_out, ...)
     * @param params Chat history item parameters (it will be consumed and zeroed)
     * @param name Chat history widget name
     */
    virtual void addChatHistory(const String& what, NamedList*& params,
	const String& name = "history");

    /**
     * Retrieve a chat widget' property
     * @param name Widget name
     * @param prop Property name
     * @param value Destination buffer
     */
    virtual void getChatProperty(const String& name, const String& prop, String& value);

    /**
     * Set a chat widget' property
     * @param name Widget name
     * @param prop Property name
     * @param value Property value
     */
    virtual void setChatProperty(const String& name, const String& prop, const String& value);

    /**
     * Check if this contact's chat window is visible
     * @return True if this contact's chat window is visible
     */
    inline bool isChatVisible()
	{ return Client::self() && Client::self()->getVisible(m_chatWndName); }

    /**
     * Show or hide this contact's chat window or docked item
     * @param visible True to show, false to hide the window or destroy the docked item
     * @param active True to activate the window or select the docked item if shown
     * @return True on success
     */
    virtual bool showChat(bool visible, bool active = false);

    /**
     * Get the chat window
     * @return Valid Window pointer or 0
     */
    Window* getChatWnd();

    /**
     * Create the chat window
     * @param force True to destroy the current one if any
     * @param name The window's name. Defaults to global name if empty
     */
    virtual void createChatWindow(bool force = false, const char* name = 0);

    /**
     * Update contact parameters in chat window
     * @param params Parameters to set
     * @param title Optional window title to set (ignored if docked)
     * @param icon Optional window icon to set (ignored if docked)
     */
    virtual void updateChatWindow(const NamedList& params, const char* title = 0,
	const char* icon = 0);

    /**
     * Check if the contact chat is active
     * @return True if the contact's chat window/page is active
     */
    virtual bool isChatActive();

    /**
     * Close the chat window or destroy docked chat item
     */
    void destroyChatWindow();

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
     * Replace contact's groups from a list of parameters
     * @param list The list of parameters
     * @param param The parameter name to handle
     * @return True if the list changed
     */
    virtual bool setGroups(const NamedList& list, const String& param);

    /**
     * Find the resource with the lowest status
     * @param ref True to obtain a referenced pointer
     * @return ClientResource pointer or 0 if not found
     */
    virtual ClientResource* status(bool ref = false);

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
     * Get the first resource with file transfer capability capability
     * @param ref True to obtain a referenced pointer
     * @return ClientResource pointer or 0 if not found
     */
    virtual ClientResource* findFileTransferResource(bool ref = false);

    /**
     * Append a resource having a given id
     * @param id The id of the desired resource
     * @return ClientResource pointer or 0 if a resource with the given name already exists
     */
    virtual ClientResource* appendResource(const String& id);

    /**
     * Insert a resource in the list by its priority.
     * If the resource is already there it will be extracted and re-inserted
     * @param res The resource to insert
     * @return True on success, false a resource with the same name already exists
     */
    virtual bool insertResource(ClientResource* res);

    /**
     * Remove a resource having a given id
     * @param id The id of the desired resource
     * @return True if the resource was removed
     */
    virtual bool removeResource(const String& id);

    /**
     * Retrieve files and folders we share with this contact
     * @return List of files and folders we share with this contact
     */
    inline NamedList& share()
	{ return m_share; }

    /**
     * Check if the list of share contains something
     * @return True if share list is not empty
     */
    inline bool haveShare() const
	{ return 0 != m_share.getParam(0); }

    /**
     * (re)load share list
     */
    virtual void updateShare();

    /**
     * Save share list
     */
    virtual void saveShare();

    /**
     * Clear share list
     */
    virtual void clearShare();

    /**
     * Set a directory we share with this contact
     * If share name is not empty it must be unique. Fails if another share has the same name
     * @param name Share name
     * @param path Directory path
     * @param save True to save now if changed
     * @return True if changed
     */
    virtual bool setShareDir(const String& name, const String& path, bool save = true);

    /**
     * Remove a share item
     * @param name Share name
     * @param save True to save now if changed
     * @return True if changed
     */
    virtual bool removeShare(const String& name, bool save = true);

    /**
     * Retrieve shared data
     * @return Shared data list
     */
    inline ObjList& shared()
	{ return m_shared; }

    /**
     * Check if the list of shared contains something
     * @return True if shared list is not empty
     */
    bool haveShared() const;

    /**
     * Retrieve shared data for a given resource
     * @param name Resource name
     * @param create True to create if not found
     * @return True if changed
     */
    virtual ClientDir* getShared(const String& name, bool create = false);

    /**
     * Remove shared data
     * @param name Resource name to remove, empty to remove all
     * @param removed Optional pointer to removed directory
     * @return True if changed
     */
    virtual bool removeShared(const String& name = String::empty(), ClientDir** removed = 0);

    /**
     * Build a contact id to be used in UI (all strings are URI escaped using extra '|' character)
     * @param dest Destination string
     * @param account Account owning the contact
     * @param contact The contact's id
     * @return Destination string
     */
    static inline String& buildContactId(String& dest, const String& account,
	const String& contact) {
	    dest << account.uriEscape('|') << "|" << String::uriEscape(contact,'|').toLower();
	    return dest;
	}

    /**
     * Retrieve the account part of a contact id
     * @param src Source string
     * @param account Account id (URI unescaped)
     */
    static inline void splitContactId(const String& src, String& account) {
	    int pos = src.find('|');
	    if (pos >= 0)
		account = src.substr(0,pos).uriUnescape();
	    else
		account = src.uriUnescape();
	}

    /**
     * Split a contact instance id in account/contact/instance parts
     * @param src Source string
     * @param account Account id (URI unescaped)
     * @param contact Contact id
     * @param instance Optional pointer to a String to be filled with instance id (URI unescaped)
     */
    static void splitContactInstanceId(const String& src, String& account,
	String& contact, String* instance = 0);

    /**
     * Build a contact instance id to be used in UI
     * @param dest Destination string
     * @param cId Contact id
     * @param inst Instance name
     * @return Destination string
     */
    static inline String& buildContactInstanceId(String& dest, const String& cId,
	const String& inst = String::empty()) {
	    dest << cId << "|" << inst.uriEscape('|');
	    return dest;
	}

    // Chat window prefix
    static String s_chatPrefix;
    // Docked chat window name
    static String s_dockedChatWnd;
    // Docked chat widget name
    static String s_dockedChatWidget;
    // MUC rooms window name
    static String s_mucsWnd;
    // Chat input widget name
    static String s_chatInput;

    String m_name;                       // Contact's display name
    NamedList m_params;                  // Optional contact extra params

protected:
    /**
     * Constructor. Append itself to the owner's list
     * @param owner The contact's owner
     * @param id The contact's id
     * @param mucRoom True if this contact is a MUC room
     */
    explicit ClientContact(ClientAccount* owner, const char* id, bool mucRoom);

    /**
     * Remove from owner
     */
    void removeFromOwner();

    /**
     * Remove from owner. Destroy the chat window. Release data
     */
    virtual void destroyed();

    ClientAccount* m_owner;              // The account owning this contact
    bool m_online;                       // Online flag
    String m_id;                         // The contact's id
    String m_subscription;               // Presence subscription state
    Flags32 m_sub;                       // Subscription flags
    URI m_uri;                           // The contact's URI
    ObjList m_resources;                 // The contact's resource list
    ObjList m_groups;                    // The group(s) this contact belongs to
    bool m_dockedChat;                   // Docked chat flag
    String m_chatWndName;                // Chat window name if any
    NamedList m_share;                   // List of files and folders we share
    ObjList m_shared;                    // List of shared. Each entry is a ClientDir whose name is the resource
};

/**
 * This class holds data about a client account/contact resource
 * @short A client contact's resource
 */
class YATE_API ClientResource : public RefObject
{
    YCLASS(ClientResource,RefObject)
    YNOCOPY(ClientResource); // no automatic copies please
public:
    /**
     * Resource status
     */
    enum Status {
	Unknown = 0,
	Offline = 1,
	Connecting = 2,
	Online = 3,
	Busy = 4,
	Dnd = 5,
	Away = 6,
	Xa = 7,
    };

    /**
     * Resource capabilities
     */
    enum Capability {
	CapAudio = 0x00000001,           // Audio
	CapFileTransfer = 0x00000002,    // File transfer support
	CapFileInfo = 0x00000004,        // File info share support
	CapRsm = 0x00000008,             // Result set management support
    };

    /**
     * Constructor
     * @param id The resource's id
     * @param name Optional display name. Defaults to the id's value if 0
     * @param audio True (default) if the resource has audio capability
     */
    inline explicit ClientResource(const char* id, const char* name = 0, bool audio = true)
	: m_id(id), m_name(name ? name : id), m_caps(audio ? CapAudio : 0),
	m_priority(0), m_status(Offline)
	{ }

    /**
     * Get a string representation of this object
     * @return The resource id
     */
    virtual const String& toString() const
	{ return m_id; }

    /**
     * Check if the resource is online
     * @return True if the resource is online
     */
    inline bool online() const
	{ return m_status > Connecting; }

    /**
     * Check if the resource is offline
     * @return True if the resource is offline
     */
    inline bool offline() const
	{ return m_status == Offline; }

    /**
     * Retrieve resource status name
     * @return Resource status name
     */
    inline const char* statusName() const
	{ return lookup(m_status,s_statusName); }

    /**
     * Retrieve resource status text or associated status display text
     * @return Resource status text
     */
    inline const char* text() const
	{ return m_text ? m_text.c_str() : statusDisplayText(m_status); }

    /**
     * Retrieve resource capabilities
     * @return Resource capabilities flags
     */
    inline Flags32& caps()
	{ return m_caps; }

    /**
     * Update resource audio capability
     * @param ok The new audio capability value
     * @return True if changed
     */
    inline bool setAudio(bool ok)
	{ return m_caps.changeFlagCheck(CapAudio,ok); }

    /**
     * Update resource file transfer capability
     * @param ok The new file transfer value
     * @return True if changed
     */
    inline bool setFileTransfer(bool ok)
	{ return m_caps.changeFlagCheck(CapFileTransfer,ok); }

    /**
     * Update resource priority
     * @param prio Resource priority
     * @return True if changed
     */
    inline bool setPriority(int prio) {
	    if (m_priority == prio)
		return false;
	    m_priority = prio;
	    return true;
	}

    /**
     * Update resource status
     * @param stat Resource status
     * @return True if changed
     */
    inline bool setStatus(int stat) {
	    if (m_status == stat)
		return false;
	    m_status = stat;
	    return true;
	}

    /**
     * Update resource status text
     * @param text Resource status text
     * @return True if changed
     */
    inline bool setStatusText(const String& text = String::empty()) {
	    if (m_text == text)
		return false;
	    m_text = text;
	    return true;
	}

    /**
     * Retrieve the status display text associated with a given resource status
     * @param status The status to find
     * @param defVal Text to return if none found
     * @return Status display text or the default value if not found
     */
    static inline const char* statusDisplayText(int status, const char* defVal = 0)
	{ return lookup(status,s_statusName,defVal); }

    /**
     * Resource status names
     */
    static const TokenDict s_statusName[];

    /**
     * resource.notify capability names
     */
    static const TokenDict s_resNotifyCaps[];

    String m_id;                         // The resource id
    String m_name;                       // Resource display name
    Flags32 m_caps;                      // Resource capabilities
    int m_priority;                      // Resource priority
    int m_status;                        // Resource status
    String m_text;                       // Resource status text
};

/**
 * This class holds data about a MUC room member.
 * The resource name holds the nickname
 * @short A MUC room member
 */
class YATE_API MucRoomMember : public ClientResource
{
    YCLASS(MucRoomMember,ClientResource)
    YNOCOPY(MucRoomMember); // no automatic copies please
public:
    /**
     * Member affiliation to the room
     */
    enum Affiliation {
	AffUnknown = 0,
	AffNone,
	Outcast,
	Member,
	Admin,
	Owner
    };

    /**
     * Member role after joining the room
     */
    enum Role {
	RoleUnknown = 0,
	RoleNone,                        // No role (out of room)
	Visitor,                         // Can view room chat
	Participant,                     // Can only send chat
	Moderator                        // Room moderator: can kick members
    };

    /**
     * Constructor
     * @param id Member internal id
     * @param nick Member nickname
     * @param uri Member uri
     */
    inline explicit MucRoomMember(const char* id, const char* nick, const char* uri = 0)
	: ClientResource(id,nick),
	m_uri(uri), m_affiliation(AffNone), m_role(RoleNone)
	{}

    /**
     * Affiliation names
     */
    static const TokenDict s_affName[];

    /**
     * Role names
     */
    static const TokenDict s_roleName[];

    String m_uri;                        // Member uri, if known
    String m_instance;                   // Member instance, if known
    int m_affiliation;                   // Member affiliation to the room
    int m_role;                          // Member role when present in room ('none' means not present)
};

/**
 * This class holds a client account's MUC room contact.
 * The list of resources contains MucRoomMember items.
 * Contact nick is held by own MucRoomMember name
 * The contact uri is the room uri
 * The contact name is the room name
 * The contact resource member uri is the account's uri
 * @short An account's MUC room contact
 */
class YATE_API MucRoom : public ClientContact
{
    YCLASS(MucRoom,ClientContact)
    YNOCOPY(MucRoom); // no automatic copies please
public:
    /**
     * Constructor. Append itself to the owner's list
     * @param owner The contact's owner
     * @param id The contact's id
     * @param name Room name
     * @param uri Room uri
     * @param nick Optional room nick
     */
    explicit MucRoom(ClientAccount* owner, const char* id, const char* name, const char* uri,
	const char* nick = 0);

    /**
     * Retrieve room resource
     * @return Room resource
     */
    inline MucRoomMember& resource()
	{ return *m_resource; }

    /**
     * Check if a given resource is the contact's member
     * @param item Member pointer to check
     * @return True if the given resource member is the contact itself
     */
    inline bool ownMember(MucRoomMember* item) const
	{ return m_resource == item; }

    /**
     * Check if a given resource is the contact's member
     * @param item Member id to check
     * @return True if the given resource member is the contact itself
     */
    inline bool ownMember(const String& item) const
	{ return m_resource->toString() == item; }

    /**
     * Check if the user has joined the room
     * @return True if the user is in the room
     */
    inline bool available() const {
	    return m_resource->online() &&
		m_resource->m_role > MucRoomMember::RoleNone;
	}

    /**
     * Check if room chat can be sent
     * @return True if the user is allowed to send chat to room
     */
    inline bool canChat() const
	{ return available() && m_resource->m_role >= MucRoomMember::Visitor; }

    /**
     * Check if private chat can be sent
     * @return True if the user is allowed to send private chat
     */
    inline bool canChatPrivate() const
	{ return available(); }

    /**
     * Check if the user can change room subject
     * @return True if the user can change room subject
     */
    inline bool canChangeSubject() const
	{ return available() && m_resource->m_role == MucRoomMember::Moderator; }

    /**
     * Check if join invitations can be sent
     * @return True if the user is allowed to invite contacts
     */
    inline bool canInvite() const
	{ return available(); }

    /**
     * Check if the user can kick a given room member
     * @param member Room member
     * @return True if the user can kick the member
     */
    bool canKick(MucRoomMember* member) const;

    /**
     * Check if the user can ban a given room member
     * @param member Room member
     * @return True if the user can ban the member
     */
    bool canBan(MucRoomMember* member) const;

    /**
     * Build a muc.room message. Add the room parameter
     * @param oper Operation parameter
     * @return Message pointer
     */
    inline Message* buildMucRoom(const char* oper) {
	    Message* m = Client::buildMessage("muc.room",accountName(),oper);
	    m->addParam("room",uri());
	    return m;
	}

    /**
     * Build a muc.room message used to login/logoff
     * @param join True to login, false to logoff
     * @param history True to request room history. Ignored if join is false
     * @param sNewer Request history newer then given seconds. Ignored if 0 or history is false
     * @return Message pointer
     */
    Message* buildJoin(bool join, bool history = true, unsigned int sNewer = 0);

    /**
     * Return a MucRoom contact from this one
     * @return MucRoom pointer or 0
     */
    virtual MucRoom* mucRoom()
	{ return this; }

    /**
     * Find the resource with the lowest status (room resource)
     * @param ref True to obtain a referenced pointer
     * @return ClientResource pointer or 0 if not found
     */
    virtual ClientResource* status(bool ref = false)
	{ return (!ref || m_resource->ref()) ? m_resource : 0; }

    /**
     * Retrieve a room member (or own member) by its nick
     * @param nick Nick to find
     * @return MucRoomMember pointer or 0 if not found
     */
    MucRoomMember* findMember(const String& nick);

    /**
     * Retrieve a room member (or own member) by its contact and instance
     * @param contact Member's contact
     * @param instance Member's instance
     * @return MucRoomMember pointer or 0 if not found
     */
    MucRoomMember* findMember(const String& contact, const String& instance);

    /**
     * Retrieve a room member (or own member) by its id
     * @param id Member id to find
     * @return MucRoomMember pointer or 0 if not found
     */
    MucRoomMember* findMemberById(const String& id);

    /**
     * Check if a given member has chat displayed
     * @param id Member id
     * @return True if the member has chat displayed
     */
    bool hasChat(const String& id);

    /**
     * Flash chat window/item to notify the user
     * @param id Member id
     * @param on True to start, false to stop flashing
     */
    virtual void flashChat(const String& id, bool on = true);

    /**
     * Retrieve the contents of the chat input widget
     * @param id Member id
     * @param text Chat input text
     * @param name Chat input widget name
     */
    virtual void getChatInput(const String& id, String& text, const String& name = "message");

    /**
     * Set the chat input widget text
     * @param id Member id
     * @param text Chat input text
     * @param name Chat input widget name
     */
    virtual void setChatInput(const String& id, const String& text = String::empty(),
	const String& name = "message");

    /**
     * Retrieve the contents of the chat history widget
     * @param id Member id
     * @param text Chat history text
     * @param richText Retrieve rich/plain text flag
     * @param name Chat history widget name
     */
    virtual void getChatHistory(const String& id, String& text, bool richText = false,
	const String& name = "history");

    /**
     * Set the contents of the chat history widget
     * @param id Member id
     * @param text Chat history text
     * @param richText Set rich/plain text flag
     * @param name Chat history widget name
     */
    virtual void setChatHistory(const String& id, const String& text, bool richText = false,
	const String& name = "history");

    /**
     * Add an entry to chat history
     * @param id Member id
     * @param what Item to add (chat_in, chat_out, ...)
     * @param params Chat history item parameters (it will be consumed and zeroed)
     * @param name Chat history widget name
     */
    virtual void addChatHistory(const String& id, const String& what, NamedList*& params,
	const String& name = "history");

    /**
     * Set a chat widget' property
     * @param id Member id
     * @param name Widget name
     * @param prop Property name
     * @param value Property value
     */
    virtual void setChatProperty(const String& id, const String& name, const String& prop,
	const String& value);

    /**
     * Show or hide a member's chat
     * @param id Member id
     * @param visible True to show, false to hide
     * @param active True to activate the chat
     * @return True on success
     */
    virtual bool showChat(const String& id, bool visible, bool active = false);

    /**
     * Create a member's chat
     * @param id Member id
     * @param force True to destroy the current one if any
     * @param name The window's name. Defaults to global name if empty
     */
    virtual void createChatWindow(const String& id, bool force = false, const char* name = 0);

    /**
     * Update member parameters in chat window
     * @param id Member id
     * @param params Parameters to set
     */
    virtual void updateChatWindow(const String& id, const NamedList& params);

    /**
     * Check if a member's chat is active
     * @return True if the members's chat page is active
     */
    virtual bool isChatActive(const String& id);

    /**
     * Close a member's chat or all chats
     * @param id Member id. Let it empty to clear all chats
     */
    void destroyChatWindow(const String& id = String::empty());

    /**
     * Retrieve a room member (or own member) by its id
     * @param id The id of the desired member
     * @param ref True to obtain a referenced pointer
     * @return ClientResource pointer or 0 if not found
     */
    virtual ClientResource* findResource(const String& id, bool ref = false);

    /**
     * Append a member having a given nick
     * @param nick Member nick
     * @return ClientResource pointer or 0 if a resource with the given name already exists
     */
    virtual ClientResource* appendResource(const String& nick);

    /**
     * Insert a resource in the list by its priority.
     * If the resource is already there it will be extracted and re-inserted
     * @param res The resource to insert
     * @return True on success, false a resource with the same name already exists
     */
    virtual bool insertResource(ClientResource* res)
	{ return false; }

    /**
     * Remove a contact having a given nick
     * @param nick The contact nick
     * @param delChat True to delete the chat
     * @return True if the contact was removed
     */
    virtual bool removeResource(const String& nick, bool delChat = false);

    /**
     * Room password
     */
    String m_password;

protected:
    // Release data. Destroy all chats
    virtual void destroyed();

private:
    unsigned int m_index;                // Index used to build member id
    MucRoomMember* m_resource;           // Account room identity and status
};

/**
 * Class used to update UI durations. The string keeps the object's id.
 * This object can be used to keep additional data associated with a client channel
 * @short An UI time updater
 */
class YATE_API DurationUpdate : public RefObject
{
    YCLASS(DurationUpdate,RefObject)
    YNOCOPY(DurationUpdate); // no automatic copies please
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
    YCLASS(ClientSound,String)
    YNOCOPY(ClientSound); // no automatic copies please
public:
    /**
     * Constructor
     * @param name The name of this object
     * @param file The file to play (should contain the whole path and the file name)
     * @param device Optional device used to play the file. Set to 0 to use the default one
     */
    inline ClientSound(const char* name, const char* file, const char* device = 0)
	: String(name), m_native(false), m_file(file), m_device(device), m_repeat(0),
	m_started(false), m_stereo(false)
	{ }

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
     * Check if this sound is a system dependent one
     * @return True if the sound is played using a system dependent method,
     *  false if played using a yate module (like wavefile)
     */
    inline bool native() const
	{ return m_native; }

    /**
     * Check if this sound is started
     * @return True if this sound is started
     */
    inline bool started() const
	{ return m_started; }

    /**
     * Get the device used to play this sound
     * @return The device used to play sound
     */
    inline const String& device() const
	{ return m_device; }

    /**
     * Set the device used to play this sound
     * @param dev The device used to play sound
     */
    inline void device(const char* dev)
	{ Lock lock(s_soundsMutex); m_device = dev; }

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
     * @param stereo True if the file contains 2 channel audio
     */
    inline void file(const char* filename, bool stereo)
	{ Lock lock(s_soundsMutex); m_file = filename; m_stereo = stereo; }

    /**
     * Set the repeat counter.
     * @param count The number of times to play the sound,
     *  0 to repeat until explicitely stopped
     */
    inline void setRepeat(unsigned int count)
	{ m_repeat = count; }

    /**
     * Check if this sound's file contains 2 channel audio
     * @return True if the sound file contains 2 channel audio
     */
    inline bool stereo() const
	{ return m_stereo; }

    /**
     * Start playing the file
     * @param force True to start playing the file even if already started
     * @return True on success
     */
    bool start(bool force = true);

    /**
     * Stop playing the file
     */
    void stop();

    /**
     * Set/reset channel on sound start/stop
     * @param chan The channel id
     * @param ok Operation: true to start, false to stop
     */
    void setChannel(const String& chan, bool ok);

    /**
     * Attach this sound to a channel
     * @param chan The channel to attach to
     * @return True on success
     */
    bool attachSource(ClientChannel* chan);

    /**
     * Build a client sound
     * @param id The name of the object
     * @param file The file to play (should contain the whole path and the file name)
     * @param device Optional device used to play the file. Set to 0 to use the default one
     * @param repeat The number of times to play the sound,
     *  0 to repeat until explicitely stopped
     * @param resetExisting True to reset the file of an already created sound
     * @param stereo True if the sound file contains 2 channel audio
     * @return True on success, false if the sound already exists
     */
    static bool build(const String& id, const char* file, const char* device = 0,
	unsigned int repeat = 0, bool resetExisting = true, bool stereo = false);

    /**
     * Check if a sound is started
     * @param name The name of the sound to check
     * @return True if the given sound is started
     */
    static bool started(const String& name);

    /**
     * Start playing a given sound
     * @param name The name of the sound to play
     * @param force True to start playing the file even if already started
     * @return True on success
     */
    static bool start(const String& name, bool force = true);

    /**
     * Stop playing a given sound
     * @param name The name of the sound to stop
     */
    static void stop(const String& name);

    /**
     * Find a sound object
     * @param token The token used to match the sound
     * @param byName True to match the sound's name, false to match its file
     * @return ClientSound pointer or 0 if not found
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

    /**
     * The prefix to be added to the file when an utility channel is started
     *  or a sound is played in a regular client channel
     */
    static String s_calltoPrefix;

protected:
    virtual bool doStart();
    virtual void doStop();

    bool m_native;                       // Native (system dependent) sound
    String m_file;
    String m_device;
    unsigned int m_repeat;
    bool m_started;
    bool m_stereo;
    String m_channel;                    // Utility channel using this sound
};

/**
 * Base class for file/dir items
 * @short A file/directory item
 */
class YATE_API ClientFileItem : public GenObject
{
    YCLASS(ClientFileItem,GenObject)
    YNOCOPY(ClientFileItem); // no automatic copies please
public:
    /**
     * Constructor
     * @param name Item name
     */
    inline ClientFileItem(const char* name)
	: m_name(name)
	{}

    /**
     * Retrieve the item name
     * @return Item name
     */
    inline const String& name() const
	{ return m_name; }

    /**
     * Check if this item is a directory
     * @return ClientDir pointer or 0
     */
    virtual ClientDir* directory()
	{ return 0; }

    /**
     * Check if this item is a file
     * @return ClientDir pointer or 0
     */
    virtual ClientFile* file()
	{ return 0; }

    /**
     * Retrieve the item name
     * @return Item name
     */
    virtual const String& toString() const
	{ return name(); }

private:
    ClientFileItem() {}                  // No default constructor
    String m_name;
};

/**
 * This class holds directory info
 * @short A directory
 */
class YATE_API ClientDir: public ClientFileItem
{
    YCLASS(ClientDir,ClientFileItem)
public:
    /**
     * Constructor
     * @param name Directory name
     */
    inline ClientDir(const char* name)
	: ClientFileItem(name), m_updated(false)
	{}

    /**
     * Copy constructor. Copy known children types
     * @param other Source object
     */
    inline ClientDir(const ClientDir& other)
	: ClientFileItem(other.name()), m_updated(other.updated())
	{ copyChildren(other.m_children); }

    /**
     * Retrieve the children list
     * @return Children list
     */
    inline ObjList& children()
	{ return m_children; }

    /**
     * Check if children were updated
     * @return True if children list was updated
     */
    inline bool updated() const
	{ return m_updated; }

    /**
     * Set children updated flag
     * @return New value for children updated flag
     */
    inline void updated(bool on)
	{ m_updated = on; }

    /**
     * Recursively check if all (sub)directores were updated
     * @return True if all (sub)directores were updated
     */
    bool treeUpdated() const;

    /**
     * Build and add a sub-directory if not have one already
     * Replace an existing file with the same name
     * @param name Directory name
     * @return ClientDir pointer or 0 on failure
     */
    ClientDir* addDir(const String& name);

    /**
     * Build sub directories from path
     * @param path Directory path
     * @param sep Path separator
     * @return ClientDir pointer or 0 on failure
     */
    ClientDir* addDirPath(const String& path, const char* sep = "/");

    /**
     * Add a copy of known children types
     * @param list List of ClientFileItem objects to copy
     */
    void copyChildren(const ObjList& list);

    /**
     * Add a list of children, consume the objects
     * @param list List of ClientFileItem objects to add
     */
    void addChildren(ObjList& list);

    /**
     * Add an item. Remove another item with the same name if exists
     * @param item Item to add
     * @return True on success
     */
    bool addChild(ClientFileItem* item);

    /**
     * Find a child by path
     * @param path Item path
     * @param sep Path separator
     * @return ClientFileItem pointer or 0
     */
    ClientFileItem* findChild(const String& path, const char* sep = "/");

    /**
     * Find a child by name
     * @param name Item name
     * @return ClientFileItem pointer or 0
     */
    inline ClientFileItem* findChildName(const String& name) {
	    ObjList* o = m_children.find(name);
	    return o ? static_cast<ClientFileItem*>(o->get()) : 0;
	}

    /**
     * Check if this item is a directory
     * @return ClientDir pointer
     */
    virtual ClientDir* directory()
	{ return this; }

protected:
    ObjList m_children;
    bool m_updated;
};

/**
 * This class holds file info
 * @short A file
 */
class YATE_API ClientFile: public ClientFileItem
{
    YCLASS(ClientFile,ClientFileItem)
public:
    /**
     * Constructor
     * @param name File name
     * @param params Optional file parameters
     */
    inline ClientFile(const char* name, const NamedList* params = 0)
	: ClientFileItem(name), m_params("") {
	    if (params)
		m_params.copyParams(*params);
	}

    /**
     * Copy constructor
     * @param other Source object
     */
    inline ClientFile(const ClientFile& other)
	: ClientFileItem(other.name()), m_params(other.params())
	{}

    /**
     * Retrieve item parameters
     * @return Item parameters
     */
    inline NamedList& params()
	{ return m_params; }

    /**
     * Retrieve item parameters
     * @return Item parameters
     */
    inline const NamedList& params() const
	{ return m_params; }

    /**
     * Check if this item is a file
     * @return ClientFile pointer
     */
    virtual ClientFile* file()
	{ return this; }

protected:
    NamedList m_params;
};

}; // namespace TelEngine

#endif /* __YATECBASE_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
