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

class Client;
class ClientChannel;
class ClientDriver;

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
     * @return True if the operation was successfull
     */
    virtual bool setText(const String& name, const String& text) = 0;

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
    virtual bool addOption(const String& name, const String& item, bool atStart = false, const String& text = String::empty()) = 0;

    /**
     * Remove an item from an element (list)
     * @param name Name of the element
     * @param item Name of the item to remove
     * @return True if the operation was successfull
     */
    virtual bool delOption(const String& name, const String& item) = 0;

    virtual bool addTableRow(const String& name, const String& item, const NamedList* data = 0, bool atStart = false);
    virtual bool delTableRow(const String& name, const String& item);
    virtual bool setTableRow(const String& name, const String& item, const NamedList* data);
    virtual bool getTableRow(const String& name, const String& item, NamedList* data = 0);
    virtual bool clearTable(const String& name);
    virtual bool getText(const String& name, String& text) = 0;
    virtual bool getCheck(const String& name, bool& checked) = 0;
    virtual bool getSelect(const String& name, String& item) = 0;
    virtual void populate() = 0;
    virtual void init() = 0;
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void size(int width, int height) = 0;
    virtual void move(int x, int y) = 0;
    virtual void moveRel(int dx, int dy) = 0;
    virtual bool related(const Window* wnd) const;
    virtual void menu(int x, int y) = 0;

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

protected:
    String m_id;
    String m_title;
    String m_context;
    bool m_visible;
    bool m_master;
    bool m_popup;
};

/**
 * Each instance of UIFactory creates special user interface elements by name
 * @short A static user interface creator
 */
class YATE_API UIFactory : public String
{
public:
    UIFactory(const char* type, const char* name);
    virtual ~UIFactory();
};

/**
 * Singleton class that holds the User Interface's main thread and methods
 * @short Thread that runs the User Interface
 */
class YATE_API Client : public Thread
{
    friend class Window;
    friend class ClientChannel;
    friend class ClientDriver;
public:
    Client(const char *name = 0);
    virtual ~Client();
    virtual void run();
    virtual void main() = 0;
    virtual void lock() = 0;
    virtual void unlock() = 0;
    inline void lockOther()
	{ if (!m_oneThread) lock(); }
    inline void unlockOther()
	{ if (!m_oneThread) unlock(); }
    virtual void allHidden() = 0;
    virtual bool createWindow(const String& name) = 0;
    virtual bool setStatus(const String& text, Window* wnd = 0);
    bool setStatusLocked(const String& text, Window* wnd = 0);
    virtual bool action(Window* wnd, const String& name);
    virtual bool toggle(Window* wnd, const String& name, bool active);
    virtual bool select(Window* wnd, const String& name, const String& item, const String& text = String::empty());
    virtual bool callRouting(const String& caller, const String& called, Message* msg = 0);
    virtual bool callIncoming(const String& caller, const String& dest = String::empty(), Message* msg = 0);
    virtual void updateCDR(const Message& msg);
    void clearActive(const String& id);
    void callAccept(const char* callId = 0);
    void callReject(const char* callId = 0);
    void callHangup(const char* callId = 0);
    bool callStart(const String& target, const String& line = String::empty(),
	const String& proto = String::empty(), const String& account = String::empty());
    bool emitDigit(char digit);
    inline bool oneThread() const
	{ return m_oneThread; }
    inline int line() const
	{ return m_line; }
    void line(int newLine);
    bool hasElement(const String& name, Window* wnd = 0, Window* skip = 0);
    bool setActive(const String& name, bool active, Window* wnd = 0, Window* skip = 0);
    bool setFocus(const String& name, bool select = false, Window* wnd = 0, Window* skip = 0);
    bool setShow(const String& name, bool visible, Window* wnd = 0, Window* skip = 0);
    bool setText(const String& name, const String& text, Window* wnd = 0, Window* skip = 0);
    bool setCheck(const String& name, bool checked, Window* wnd = 0, Window* skip = 0);
    bool setSelect(const String& name, const String& item, Window* wnd = 0, Window* skip = 0);
    bool setUrgent(const String& name, bool urgent, Window* wnd = 0, Window* skip = 0);
    bool hasOption(const String& name, const String& item, Window* wnd = 0, Window* skip = 0);
    bool addOption(const String& name, const String& item, bool atStart, const String& text = String::empty(), Window* wnd = 0, Window* skip = 0);
    bool delOption(const String& name, const String& item, Window* wnd = 0, Window* skip = 0);
    bool addTableRow(const String& name, const String& item, const NamedList* data = 0, bool atStart = false, Window* wnd = 0, Window* skip = 0);
    bool delTableRow(const String& name, const String& item, Window* wnd = 0, Window* skip = 0);
    bool setTableRow(const String& name, const String& item, const NamedList* data, Window* wnd = 0, Window* skip = 0);
    bool getTableRow(const String& name, const String& item, NamedList* data = 0, Window* wnd = 0, Window* skip = 0);
    bool clearTable(const String& name, Window* wnd = 0, Window* skip = 0);
    bool getText(const String& name, String& text, Window* wnd = 0, Window* skip = 0);
    bool getCheck(const String& name, bool& checked, Window* wnd = 0, Window* skip = 0);
    bool getSelect(const String& name, String& item, Window* wnd = 0, Window* skip = 0);
    void moveRelated(const Window* wnd, int dx, int dy);
    inline bool initialized() const
	{ return m_initialized; }
    inline static Client* self()
	{ return s_client; }
    inline static bool changing()
	{ return (s_changing > 0); }
    inline const String& activeId() const
	{ return m_activeId; }
    static Window* getWindow(const String& name);
    static bool setVisible(const String& name, bool show = true);
    static bool getVisible(const String& name);
    static bool openPopup(const String& name, const NamedList* params = 0, const Window* parent = 0);
    static bool openMessage(const char* text, const Window* parent = 0, const char* context = 0);
    static bool openConfirm(const char* text, const Window* parent = 0, const char* context = 0);
    static ObjList* listWindows();
    void idleActions();
protected:
    virtual void loadWindows() = 0;
    virtual void initWindows();
    virtual void initClient();
    virtual void setChannelDisplay(ClientChannel* chan);
    virtual bool updateCallHist(const NamedList& params);
    void addChannel(ClientChannel* chan);
    void delChannel(ClientChannel* chan);
    void setChannel(ClientChannel* chan);
    void setChannelInternal(ClientChannel* chan);
    void selectChannel(ClientChannel* chan, bool force = false);
    void updateFrom(const String& id);
    void updateFrom(const ClientChannel* chan);
    void enableAction(const ClientChannel* chan, const String& action);
    inline bool needProxy() const
	{ return m_oneThread && !isCurrent(); }
    bool driverLockLoop();
    static bool driverLock(long maxwait = 0);
    static void driverUnlock();
    ObjList m_windows;
    String m_activeId;
    bool m_initialized;
    int m_line;
    bool m_oneThread;
    bool m_multiLines;
    bool m_autoAnswer;
    static Client* s_client;
    static int s_changing;
};

/**
 * This class implements a Channel used by client programs
 * @short Channel used by client programs
 */
class YATE_API ClientChannel : public Channel
{
    friend class ClientDriver;
public:
    ClientChannel(const String& party, const char* target = 0, const Message* msg = 0);
    virtual ~ClientChannel();
    virtual bool msgProgress(Message& msg);
    virtual bool msgRinging(Message& msg);
    virtual bool msgAnswered(Message& msg);
    virtual bool callRouted(Message& msg);
    virtual void callAccept(Message& msg);
    virtual void callRejected(const char* error, const char* reason, const Message* msg);
    virtual bool enableAction(const String& action) const;
    void callAnswer();
    bool openMedia(bool replace = false);
    void closeMedia();
    inline const String& party() const
	{ return m_party; }
    inline const String& description() const
	{ return m_desc; }
    inline bool flashing() const
	{ return m_flashing; }
    inline void noticed()
	{ m_flashing = false; }
    inline int line() const
	{ return m_line; }
    void line(int newLine);
protected:
    void update(bool client = true);
    String m_party;
    String m_desc;
    u_int64_t m_time;
    int m_line;
    bool m_flashing;
    bool m_canAnswer;
    bool m_canTransfer;
    bool m_canConference;
};

/**
 * Abstract client Driver that implements some of the specific functionality
 * @short Base Driver with client specific functions
 */
class YATE_API ClientDriver : public Driver
{
public:
    ClientDriver();
    virtual ~ClientDriver();
    virtual void initialize() = 0;
    virtual bool factory(UIFactory* factory, const char* type);
    virtual bool msgExecute(Message& msg, String& dest);
    virtual void msgTimer(Message& msg);
    virtual bool msgRoute(Message& msg);
    ClientChannel* findLine(int line);
    inline static ClientDriver* self()
	{ return s_driver; }
    inline static const String& device()
	{ return s_device; }
protected:
    void setup();
    static ClientDriver* s_driver;
    static String s_device;
};

}; // namespace TelEngine

#endif /* __YATECBASE_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
