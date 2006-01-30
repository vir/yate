/**
 * gtk2client.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * A Gtk based universal telephony client
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

#include <yatecbase.h>

#ifdef _WINDOWS
                                                                                
#ifdef LIBYGTK2_EXPORTS
#define YGTK2_API __declspec(dllexport)
#else
#ifndef LIBYGTK2_STATIC
#define YGTK2_API __declspec(dllimport)
#endif
#endif
                                                                                
#endif /* _WINDOWS */
                                                                                
#ifndef YGTK2_API
#define YGTK2_API
#endif

#include <string.h>
#include <stdio.h>
#include <malloc.h>

#include <gtk/gtk.h>

namespace TelEngine {

class GTKWindow;

class YGTK2_API GTKClient : public Client
{
    friend class GTKWindow;
public:
    GTKClient();
    virtual ~GTKClient();
    virtual void main();
    virtual void lock();
    virtual void unlock();
    virtual void allHidden();
    virtual bool createWindow(const String& name);
protected:
    virtual void loadWindows();
};

class YGTK2_API GTKDriver : public ClientDriver
{
public:
    GTKDriver();
    virtual ~GTKDriver();
    virtual void initialize();
    virtual bool factory(UIFactory* factory, const char* type = 0);
};

class YGTK2_API Widget
{
public:
    Widget();
    virtual ~Widget();
    inline GtkWidget* widget() const
	{ return m_widget; }
    virtual bool setText(const String& text);
    virtual bool setCheck(bool checked);
    virtual bool setSelect(const String& item);
    virtual bool setUrgent(bool urgent);
    virtual bool addOption(const String& item, bool atStart = false, const String& text = String::empty());
    virtual bool delOption(const String& item);
    virtual bool addTableRow(const String& item, const NamedList* data = 0, bool atStart = false);
    virtual bool delTableRow(const String& item);
    virtual bool setTableRow(const String& item, const NamedList* data);
    virtual bool clearTable();
    virtual bool getText(String& text);
    virtual bool getCheck(bool& checked);
    virtual bool getSelect(String& item);
protected:
    void widget(GtkWidget* wid);
private:
    GtkWidget* m_widget;
    void destroyed();
    static void destroyCb(GtkObject* obj, gpointer dat);
};

class YGTK2_API GTKWindow : public Window
{
    friend class GTKClient;
    YCLASS(GTKWindow,Window)
public:
    enum Layout {
	Unknown = 0,
	Fixed,
	Table,
	Infinite,
	HBox,
	VBox,
	Boxed,
	Tabbed,
	Framed,
	Scroll,
    };
    GTKWindow(const char* id = 0, bool decorated = false, Layout layout = Unknown);
    virtual ~GTKWindow();
    virtual void title(const String& text);
    virtual bool setParams(const NamedList& params);
    virtual bool hasElement(const String& name);
    virtual void setOver(const Window* parent);
    virtual bool setActive(const String& name, bool active);
    virtual bool setShow(const String& name, bool visible);
    virtual bool setText(const String& name, const String& text);
    virtual bool setCheck(const String& name, bool checked);
    virtual bool setSelect(const String& name, const String& item);
    virtual bool setUrgent(const String& name, bool urgent);
    virtual bool addOption(const String& name, const String& item, bool atStart = false, const String& text = String::empty());
    virtual bool delOption(const String& name, const String& item);
    virtual bool addTableRow(const String& name, const String& item, const NamedList* data = 0, bool atStart = false);
    virtual bool delTableRow(const String& name, const String& item);
    virtual bool setTableRow(const String& name, const String& item, const NamedList* data);
    virtual bool clearTable(const String& name);
    virtual bool getText(const String& name, String& text);
    virtual bool getCheck(const String& name, bool& checked);
    virtual bool getSelect(const String& name, String& item);
    virtual void populate();
    virtual void init();
    virtual void show();
    virtual void hide();
    virtual void size(int width, int height);
    virtual void move(int x, int y);
    virtual void moveRel(int dx, int dy);
    virtual void geometry(int x, int y, int w, int h);
    virtual GtkWidget* filler();
    virtual GtkWidget* container(Layout layout) const;
    virtual GtkWidget* container(const String& layout) const;
    virtual GtkWidget* build(const String& type, const String& text);
    GtkWidget* find(const String& name) const;
    virtual void insert(GtkWidget* wid, int x = 0, int y = 0, int w = -1, int h = -1);
    virtual bool action(GtkWidget* wid);
    virtual bool toggle(GtkWidget* wid, gboolean active);
    virtual bool select(GtkOptionMenu* opt, gint selected);
    virtual bool select(GtkList* lst, GtkListItem* item);
    virtual void menu(int x, int y);
    inline GtkWidget* widget() const
	{ return m_widget; }
    inline int state() const
	{ return m_state; }
    inline void state(int gdkState)
	{ m_state = gdkState; }
    bool prepare();
    bool restore();
    static bool setText(GtkWidget* wid, const String& text);
    static bool setCheck(GtkWidget* wid, bool checked);
    static bool setSelect(GtkWidget* wid, const String& item);
    static bool setUrgent(GtkWidget* wid, bool urgent);
    static bool addOption(GtkWidget* wid, const String& item, bool atStart = false, const String& text = String::empty());
    static bool delOption(GtkWidget* wid, const String& item);
    static bool addTableRow(GtkWidget* wid, const String& item, const NamedList* data = 0, bool atStart = false);
    static bool delTableRow(GtkWidget* wid, const String& item);
    static bool setTableRow(GtkWidget* wid, const String& item, const NamedList* data);
    static bool clearTable(GtkWidget* wid);
    static bool getText(GtkWidget* wid, String& text);
    static bool getCheck(GtkWidget* wid, bool& checked);
    static bool getSelect(GtkWidget* wid, String& item);
protected:
    GtkWidget* m_widget;
    GtkWidget* m_filler;
    String m_tabName;
    int m_layout;
    int m_state;
    gint m_posX;
    gint m_posY;
    gint m_sizeW;
    gint m_sizeH;
};

/**
 * Each instance of WindowFactory creates special windows by name
 * @short A static window creator
 */
class YGTK2_API WindowFactory : public UIFactory
{
   YCLASS(WindowFactory,UIFactory)
public:
   WindowFactory(const char* type, const char* name);
   virtual Window* build() const = 0;
};
	                                                                                       
#define WINDOW_FACTORY(type,name,cls) \
class cls##__Factory : public WindowFactory \
{ public: cls##__Factory() : WindowFactory(type,name) {} \
Window* build() const { return new cls(name); } }; \
cls##__Factory cls##__FactoryInstance;
		
/**
 * Each instance of WidgetFactory creates special widgets by name
 * @short A static widget creator
 */
class YGTK2_API WidgetFactory : public UIFactory
{
    YCLASS(WidgetFactory,UIFactory)
public:
    WidgetFactory(const char* type, const char* name);
    virtual Widget* build(const String& text) const = 0;
};

#define WIDGET_FACTORY(type,name,cls) \
class cls##__Factory : public WidgetFactory \
{ public: cls##__Factory() : WidgetFactory(type,name) {} \
Widget* build(const String& text) const { return new cls(text); } }; \
cls##__Factory cls##__FactoryInstance;

}; // namespace TelEngine

/* vi: set ts=8 sw=4 sts=4 noet: */
