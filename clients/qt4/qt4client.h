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
    virtual bool chooseFile(Window* parent, const NamedList& params,
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
     * Set or get an object's property
     * @param set True to set, false to get the property
     * @param obj The object
     * @param name Property's name
     * @param value Property's value
     * @return False if the property doesn't exist or has a type not supported by String
     */
    static bool property(bool set, QObject* obj, const char* name, String& value);

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
    QtWindow(const char* name, const char* description);
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
    virtual bool delTableRow(const String& name, const String& item);
    virtual bool setTableRow(const String& name, const String& item, const NamedList* data);
    virtual bool getTableRow(const String& name, const String& item, NamedList* data = 0);
    virtual bool clearTable(const String& name);
    virtual bool getText(const String& name, String& text);
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
    virtual void closeEvent(QCloseEvent* event);

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
    virtual void mousePressEvent(QMouseEvent* event);
    virtual void mouseReleaseEvent(QMouseEvent* event);
    virtual void mouseMoveEvent(QMouseEvent* event);
    // Update window position and size
    void updatePosSize();
    // Get the widget with this window's content
    inline QWidget* wndWidget()
	{ return findChild<QWidget*>(m_widget); }

    String m_description;
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
