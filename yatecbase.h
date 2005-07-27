/**
 * yatecbase.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Common base classes for all telephony clients
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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
    Window(const char* id = 0);
    virtual ~Window();
    virtual const String& toString() const;
    virtual void title(const String& text);
    virtual bool setParams(const NamedList& params);
    virtual void setOver(const Window* parent) = 0;
    virtual bool hasElement(const String& name) = 0;
    virtual bool setActive(const String& name, bool active) = 0;
    virtual bool setShow(const String& name, bool visible) = 0;
    virtual bool setText(const String& name, const String& text) = 0;
    virtual bool setCheck(const String& name, bool checked) = 0;
    virtual bool setSelect(const String& name, const String& item) = 0;
    virtual bool setUrgent(const String& name, bool urgent) = 0;
    virtual bool addOption(const String& name, const String& item, bool atStart = false, const String& text = String::empty()) = 0;
    virtual bool delOption(const String& name, const String& item) = 0;
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
    inline const String& id() const
	{ return m_id; }
    inline const String& title() const
	{ return m_title; }
    inline bool visible() const
	{ return m_visible; }
    inline void visible(bool yes)
	{ if (yes) show(); else hide(); }
    inline bool master() const
	{ return m_master; }
    inline bool popup() const
	{ return m_popup; }
protected:
    String m_id;
    String m_title;
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
    virtual void allHidden() = 0;
    virtual bool createWindow(const String& name) = 0;
    virtual bool setStatus(const String& text, Window* wnd = 0);
    bool setStatusLocked(const String& text, Window* wnd = 0);
    virtual bool action(Window* wnd, const String& name);
    virtual bool toggle(Window* wnd, const String& name, bool active);
    virtual bool select(Window* wnd, const String& name, const String& item, const String& text = String::empty());
    virtual bool callIncoming(const String& caller, const String& dest = String::empty(), Message* msg = 0);
    void clearActive(const String& id);
    void callAccept(const char* callId = 0);
    void callReject(const char* callId = 0);
    void callHangup(const char* callId = 0);
    bool callStart(const String& target, const String& line = String::empty(),
	const String& proto = String::empty(), const String& account = String::empty());
    bool emitDigit(char digit);
    inline int line() const
	{ return m_line; }
    void line(int newLine);
    bool hasElement(const String& name, Window* wnd = 0, Window* skip = 0);
    bool setActive(const String& name, bool active, Window* wnd = 0, Window* skip = 0);
    bool setShow(const String& name, bool visible, Window* wnd = 0, Window* skip = 0);
    bool setText(const String& name, const String& text, Window* wnd = 0, Window* skip = 0);
    bool setCheck(const String& name, bool checked, Window* wnd = 0, Window* skip = 0);
    bool setSelect(const String& name, const String& item, Window* wnd = 0, Window* skip = 0);
    bool setUrgent(const String& name, bool urgent, Window* wnd = 0, Window* skip = 0);
    bool addOption(const String& name, const String& item, bool atStart, const String& text = String::empty(), Window* wnd = 0, Window* skip = 0);
    bool delOption(const String& name, const String& item, Window* wnd = 0, Window* skip = 0);
    bool getText(const String& name, String& text, Window* wnd = 0, Window* skip = 0);
    bool getCheck(const String& name, bool& checked, Window* wnd = 0, Window* skip = 0);
    bool getSelect(const String& name, String& item, Window* wnd = 0, Window* skip = 0);
    void moveRelated(const Window* wnd, int dx, int dy);
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
    static ObjList* listWindows();
protected:
    virtual void loadWindows() = 0;
    virtual void initWindows();
    virtual void initClient();
    void addChannel(ClientChannel* chan);
    void delChannel(ClientChannel* chan);
    void setChannel(ClientChannel* chan);
    void setChannelInternal(ClientChannel* chan);
    void updateFrom(const String& id);
    void updateFrom(const ClientChannel* chan);
    void enableAction(const ClientChannel* chan, const String& action);
    ObjList m_windows;
    String m_activeId;
    int m_line;
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
    ClientChannel(const String& party, const char* target = 0);
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
