/**
 * qt4client.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * A Qt-4 based universal telephony client
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

#include <yatecbase.h>

#ifdef _WINDOWS

#ifdef LIBYQT4_EXPORTS
#define YQT4_API __declspec(dllexport)
#else
#ifndef LIBYQT4_STATIC
#define YQT4_API __declspec(dllimport)
#endif
#endif

#endif /* _WINDOWS */
                                          
#ifndef YQT4_API
#define YQT4_API
#endif

#undef open
#undef read
#undef close
#undef write
#undef mkdir
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define QT_NO_DEBUG
#define QT_DLL
#define QT_GUI_LIB
#define QT_CORE_LIB
#define QT_THREAD_SUPPORT

#include <QtGui>
#include <QSound>

namespace TelEngine {

class QtEventProxy;                      // Proxy to global QT events
class QtClient;                          // The QT based client
class QtDriver;                          // The QT based telephony driver
class QtWindow;                          // A QT window
class QtCustomObject;                    // A custom QT object
class QtCustomWidget;                    // A custom QT widget
class QtTable;                           // A custom QT table widget
class QtSound;                           // A QT client sound

/**
 * Proxy to global QT events
 * @short A QT proxy class
 */
class YQT4_API QtEventProxy : public QObject, public GenObject
{
    YCLASS(QtEventProxy,GenObject)
    Q_CLASSINFO("QtEventProxy","Yate")
    Q_OBJECT

public:
    enum Type {
	Timer,
	AllHidden,
    };

    /**
     * Constructor
     * @param Event type
     * @param pointer to QT application when needed
     */
    QtEventProxy(Type type, QApplication* app = 0);

    /**
     * Get a string representation of this object
     * @return Object's name
     */
    virtual const String& toString() const
	{ return m_name; }

private slots:
    void timerTick();                    // Idle timer
    void allHidden();                    // All windows closed notification

private:
    String m_name;                       // Object name
};

class YQT4_API QtClient : public Client
{
    friend class QtWindow;
public:
    QtClient();
    virtual ~QtClient();
    virtual void run();
    virtual void cleanup();
    virtual void main();
    virtual void lock();
    virtual void unlock();
    virtual void allHidden();
    virtual bool createWindow(const String& name,
	const String& alias = String::empty());
    virtual bool action(Window* wnd, const String& name, NamedList* params = 0);
    virtual void quit() {
	    if (m_app)
		m_app->quit();
	    else
		Engine::halt(0);
	}

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
    virtual bool chooseFile(Window* parent, NamedList& params,
	NamedList* files, String* file);

    /**
     * Create a sound object. Append it to the global list
     * @param name The name of sound object
     * @param file The file to play (should contain the whole path and the file name)
     * @param device Optional device used to play the file. Set to 0 to use the default one
     * @return True on success, false if a sound with the given name already exists
     */
    virtual bool createSound(const char* name, const char* file, const char* device = 0);

    /**
     * Build a date/time string from UTC time
     * @param dest Destination string
     * @param secs Seconds since EPOCH
     * @param format Format string used to build the destination
     * @param utc True to build UTC time instead of local time
     * @return True on success
     */
    virtual bool formatDateTime(String& dest, unsigned int secs, const char* format,
	bool utc = false);

    /**
     * Build a date/time QT string from UTC time
     * @param secs Seconds since EPOCH
     * @param format Format string
     * @param utc True to build UTC time instead of local time
     * @return The formated string
     */
    static QString formatDateTime(unsigned int secs, const char* format,
	bool utc = false);

    /**
     * Get an UTF8 representation of a QT string
     * @param dest Destination string
     * @param src Source QT string
     */
    static inline void getUtf8(String& dest, const QString& src)
	{ dest = src.toUtf8().constData(); }

    /**
     * Get an UTF8 representation of a QT string and add it to a list of parameters
     * @param dest Destination list
     * @param param Parameter name/value
     * @param src Source QT string
     * @param setValue True to set the QT string as parameter value, false to set it
     *  as parameter name
     */
    static inline void getUtf8(NamedList& dest, const char* param,
	const QString& src, bool setValue = true) {
	    if (setValue)
		dest.addParam(param,src.toUtf8().constData());
	    else
		dest.addParam(src.toUtf8().constData(),param);
	}

    /**
     * Set a QT string from an UTF8 char buffer
     * @param str The buffer
     * @return A QT string filled with the buffer
     */
    static inline QString setUtf8(const char* str)
	{ return QString::fromUtf8(TelEngine::c_safe(str)); }

    /**
     * Set or an object's property
     * @param obj The object
     * @param name Property's name
     * @param value Property's value
     * @return False if the property doesn't exist or has a type not supported by String
     */
    static bool setProperty(QObject* obj, const char* name, const String& value);

    /**
     * Get an object's property
     * @param obj The object
     * @param name Property's name
     * @param value Property's value
     * @return False if the property doesn't exist or has a type not supported by String
     */
    static bool getProperty(QObject* obj, const char* name, String& value);

    /**
     * Build a menu object from a list of parameters.
     * Each menu item is indicated by a parameter starting with 'item:".
     * item:menu_name=Menu Text will create a menu item named 'menu_name' with 
     *  'Menu Text' as display name.
     * If the item parameter is a NamedPointer a submenu will be created.
     * Menu actions properties can be set from parameters with format:
     *  property:object_name:property_name=value
     * @param params The menu parameters. The list name is the object name
     * @param text The menu display text
     * @param receiver Object receiving menu actions
     * @param actionSlot The receiver's slot for menu signal triggered()
     * @param toggleSlot The receiver's slot for menu signal toggled()
     * @param parent Optional widget parent
     * @return QMenu pointer or 0 if failed to build it
     */
    static QMenu* buildMenu(NamedList& params, const char* text, QObject* receiver,
	 const char* actionSlot, const char* toggleSlot, QWidget* parent = 0);

    /**
     * Wrapper for QObject::connect() used to put a debug mesage on failure
     */
    static bool connectObjects(QObject* sender, const char* signal,
	 QObject* receiver, const char* slot);

protected:
    virtual void loadWindows(const char* file = 0);
private:
    QApplication* m_app;
    ObjList m_events;                    // Proxy events objects
};

class YQT4_API QtDriver : public ClientDriver
{
public:
    QtDriver();
    virtual ~QtDriver();
    virtual void initialize();
private:
    bool m_init;                         // Already initialized flag
};

class YQT4_API QtWindow : public QWidget, public Window
{
    YCLASS(QtWindow, Window)
    Q_CLASSINFO("QtWindow", "Yate")
    Q_OBJECT

    friend class QtClient;
public:
    QtWindow();
    QtWindow(const char* name, const char* description, const char* alias);
    virtual ~QtWindow();

    virtual void title(const String& text);
    virtual void context(const String& text);
    virtual bool setParams(const NamedList& params);
    virtual void setOver(const Window* parent);
    virtual bool hasElement(const String& name);
    virtual bool setActive(const String& name, bool active);
    virtual bool setFocus(const String& name, bool select = false);
    virtual bool setShow(const String& name, bool visible);

    /**
     * Set the displayed text of an element in the window
     * @param name Name of the element
     * @param text Text value to set in the element
     * @param richText True if the text contains format data
     * @return True if the operation was successfull
     */
    virtual bool setText(const String& name, const String& text,
	bool richText = false);

    virtual bool setCheck(const String& name, bool checked);
    virtual bool setSelect(const String& name, const String& item);
    virtual bool setUrgent(const String& name, bool urgent);

    virtual bool hasOption(const String& name, const String& item);
    virtual bool addOption(const String& name, const String& item, bool atStart = false, const String& text = String::empty());
    virtual bool delOption(const String& name, const String& item);
    virtual bool getOptions(const String& name, NamedList* items);

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

    virtual bool addTableRow(const String& name, const String& item, const NamedList* data = 0, bool atStart = false);

    virtual bool setMultipleRows(const String& name, const NamedList& data, const String& prefix);

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

    virtual bool delTableRow(const String& name, const String& item);
    virtual bool setTableRow(const String& name, const String& item, const NamedList* data);
    virtual bool getTableRow(const String& name, const String& item, NamedList* data = 0);
    virtual bool clearTable(const String& name);

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
     * Get an element's text
     * @param name Name of the element
     * @param text The destination string
     * @param richText True to get the element's roch text if supported.
     * @return True if the operation was successfull
     */
    virtual bool getText(const String& name, String& text, bool richText = false);

    virtual bool getCheck(const String& name, bool& checked);
    virtual bool getSelect(const String& name, String& item);

    /**
     * Set a property for this window or for a widget owned by it
     * @param name Name of the element
     * @param item Property's name
     * @param value Property's value
     * @return False if the property doesn't exist or has a type not supported by String
     */
    virtual bool setProperty(const String& name, const String& item, const String& value);

    /**
     * Get a property from this window or from a widget owned by it
     * @param name Name of the element
     * @param item Property's name
     * @param value Property's value
     * @return False if the property doesn't exist or has a type not supported by String
     */
    virtual bool getProperty(const String& name, const String& item, String& value);

    virtual void show();
    virtual void hide();
    virtual void size(int width, int height);
    virtual void move(int x, int y);
    virtual void moveRel(int dx, int dy);
    virtual bool related(const Window* wnd) const;
    virtual void menu(int x, int y) ;

    /**
     * Load a widget from file
     * @param fileName UI filename to load
     * @param parent The widget holding the loaded widget's contents
     * @param uiName The loaded widget's name (used for debug)
     * @param path Optional fileName path. Set to 0 to use the default one
     * @return QWidget pointer or 0 on failure 
     */
    static QWidget* loadUI(const char* fileName, QWidget* parent,
	const char* uiName, const char* path = 0);

    /**
     * Clear the UI cache
     * @param fileName Optional UI filename to clear. Clear all if 0
     */
    static void clearUICache(const char* fileName = 0);

protected:
    // Notify client on selection changes
    inline bool select(const String& name, const String& item,
	const String& text = String::empty()) {
	    if (!QtClient::self() || QtClient::changing())
		return false;
	    return QtClient::self()->select(this,name,item,text);
	}

    // Filter events to apply dynamic properties changes
    bool eventFilter(QObject* watched, QEvent* event);
    // Handle key pressed events
    void keyPressEvent(QKeyEvent* event);

public slots:
    void setVisible(bool visible);
    // A widget was double clicked
    void doubleClick();
    // A widget's selection changed
    void selectionChanged();
    // Clicked actions
    void action();
    // Toggled actions
    void toggled(bool);
    // System tray actions
    void sysTrayIconAction(QSystemTrayIcon::ActivationReason reason);

private slots:
    void openUrl(const QString& link);

protected:
    virtual void doPopulate();
    virtual void doInit();
    // Methods inherited from QWidget
    virtual void moveEvent(QMoveEvent* event)
	{ updatePosSize(); }
    virtual void resizeEvent(QResizeEvent* event)
	{ updatePosSize(); }
    virtual bool event(QEvent* ev);
    virtual void mousePressEvent(QMouseEvent* event);
    virtual void mouseReleaseEvent(QMouseEvent* event);
    virtual void mouseMoveEvent(QMouseEvent* event);
    virtual void closeEvent(QCloseEvent* event);
    virtual void changeEvent(QEvent* event);
    // Update window position and size
    void updatePosSize();
    // Get the widget with this window's content
    inline QWidget* wndWidget()
	{ return findChild<QWidget*>(m_widget); }

    String m_description;
    String m_oldId;                      // Old id used to retreive the config section in .rc
    bool m_keysVisible;
    int m_x;
    int m_y;
    int m_width;
    int m_height;
    bool m_maximized;
    bool m_mainWindow;                   // Main window flag: close app when this window is closed
    QString m_widget;                    // The widget with window's content
    bool m_moving;                       // Flag used to move the window on mouse move event
    QPoint m_movePos;                    // Old position used when moving the window
};

/**
 * This class encapsulates a custom QT object
 * @short A custom QT object
 */
class YQT4_API QtCustomObject : public QObject, public UIWidget
{
    YCLASS(QtCustomObject,UIWidget)
    Q_CLASSINFO("QtCustomObject","Yate")
    Q_OBJECT
public:
    /**
     * Constructor
     * @param name Object's name
     * @param parent Optional parent object
     */
    inline QtCustomObject(const char* name, QObject* parent = 0)
	: QObject(parent), UIWidget(name)
	{ setObjectName(name);	}

    /**
     * Parent changed notification
     */
    virtual void parentChanged()
	{}

private:
    QtCustomObject() {}                  // No default constructor
};

/**
 * This class encapsulates a custom QT widget
 * @short A custom QT widget
 */
class YQT4_API QtCustomWidget : public QWidget, public UIWidget
{
    YCLASS(QtCustomWidget,UIWidget)
    Q_CLASSINFO("QtCustomWidget","Yate")
    Q_OBJECT
public:
    /**
     * Constructor
     * @param name Widget's name
     * @param parent Optional parent widget
     */
    inline QtCustomWidget(const char* name, QWidget* parent = 0)
	: QWidget(parent), UIWidget(name)
	{ setObjectName(name);	}

private:
    QtCustomWidget() {}                  // No default constructor
};

/**
 * This class encapsulates a custom QT table
 * @short A custom QT table widget
 */
class YQT4_API QtTable : public QTableWidget, public UIWidget
{
    YCLASS(QtTable,UIWidget)
    Q_CLASSINFO("QtTable","Yate")
    Q_OBJECT
public:
    /**
     * Constructor
     * @param name Table's name
     */
    inline QtTable(const char* name)
	: UIWidget(name)
	{ setObjectName(name);	}

private:
    QtTable() {}                         // No default constructor
};

/**
 * QT specific sound
 * @short A QT client sound
 */
class YQT4_API QtSound : public ClientSound
{
    YCLASS(QtSound,ClientSound)
public:
    /**
     * Constructor
     * @param name The name of this object
     * @param file The file to play (should contain the whole path and the file name)
     * @param device Optional device used to play the file. Set to 0 to use the default one
     */
    inline QtSound(const char* name, const char* file, const char* device = 0)
	: ClientSound(name,file,device), m_sound(0)
	{}

protected:
    virtual bool doStart();
    virtual void doStop();

private:
    QSound* m_sound;
};

}; // namespace TelEngine

/* vi: set ts=8 sw=4 sts=4 noet: */
