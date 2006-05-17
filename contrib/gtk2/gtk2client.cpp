/**
 * gtk2client.cpp
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


#include "gtk2client.h"

#include <gdk/gdkkeysyms.h>

using namespace TelEngine;

static int s_shown = 0;
static GtkWidget* s_moving = 0;
static Configuration s_cfg;
static Configuration s_save;
static ObjList s_factories;
static bool s_clickInfo = false;

#define INVALID_POS (-1000000)
#define MAX_CONTAINER_DEPTH 20
#define MAX_COLUMNS_NUMBER 50

#ifdef _WINDOWS
#define BUGGY_IDLE
#define ONE_THREAD true
#define DEFAULT_DEVICE "dsound/*"
#else
#undef BUGGY_IDLE
#define ONE_THREAD false
#define DEFAULT_DEVICE "oss//dev/dsp"
#endif

// Internal class used to recursively find a widget
class WidgetFinder {
public:
    inline WidgetFinder(const String& name)
	: m_name(name), m_widget(0)
	{ }
    GtkWidget* find(GtkContainer* container);
private:
    static void findCb(GtkWidget* wid, gpointer dat);
    const String& m_name;
    GtkWidget* m_widget;
};

void WidgetFinder::findCb(GtkWidget* wid, gpointer dat)
{
    WidgetFinder* f = static_cast<WidgetFinder*>(dat);
    if ((!f) || f->m_widget)
	return;
    const gchar* name = gtk_widget_get_name(wid);
    if (f->m_name == name) {
	f->m_widget = wid;
	return;
    }
    if GTK_IS_CONTAINER(wid)
	gtk_container_foreach(GTK_CONTAINER(wid),findCb,dat);
}

GtkWidget* WidgetFinder::find(GtkContainer* container)
{
    gtk_container_foreach(container,findCb,this);
    XDebug(GTKDriver::self(),DebugAll,"WidgetFinder::find '%s' found %p",m_name.c_str(),m_widget);
    return m_widget;
}

// Default Widget behaviour is to use GTKWindow's static methods
bool Widget::setText(const String& text)
    { return GTKWindow::setText(m_widget,text); }
bool Widget::setCheck(bool checked)
    { return GTKWindow::setCheck(m_widget,checked); }
bool Widget::setSelect(const String& item)
    { return GTKWindow::setSelect(m_widget,item); }
bool Widget::setUrgent(bool urgent)
    { return GTKWindow::setUrgent(m_widget,urgent); }
bool Widget::hasOption(const String& item)
    { return GTKWindow::hasOption(m_widget,item); }
bool Widget::addOption(const String& item, bool atStart, const String& text)
    { return GTKWindow::addOption(m_widget,item,atStart,text); }
bool Widget::delOption(const String& item)
    { return GTKWindow::delOption(m_widget,item); }
bool Widget::addTableRow(const String& item, const NamedList* data, bool atStart)
    { return GTKWindow::addTableRow(m_widget,item,data,atStart); }
bool Widget::delTableRow(const String& item)
    { return GTKWindow::delTableRow(m_widget,item); }
bool Widget::setTableRow(const String& item, const NamedList* data)
    { return GTKWindow::setTableRow(m_widget,item,data); }
bool Widget::getTableRow(const String& item, NamedList* data)
    { return GTKWindow::getTableRow(m_widget,item,data); }
bool Widget::clearTable()
    { return GTKWindow::clearTable(m_widget); }
bool Widget::getText(String& text)
    { return GTKWindow::getText(m_widget,text); }
bool Widget::getCheck(bool& checked)
    { return GTKWindow::getCheck(m_widget,checked); }
bool Widget::getSelect(String& item)
    { return GTKWindow::getSelect(m_widget,item); }

typedef GtkWidget* (*GBuilder) (const gchar *label);

// Internal class used to build widgets
class WidgetMaker
{
public:
    const char* name;
    GBuilder builder;
    const gchar* sig;
    GCallback cb;
};

static bool validPos(int x, int y)
{
    return (-10000 < x) && (x < 10000) && (-10000 < y) && (y < 10000);
}

static gboolean gtkIdleCb(gpointer dat)
{
    if (dat) {
	// idle and timeout callbacks are called from glib directly
	// so gtk/gdk thread safety is not assured by default
	gdk_threads_enter();
	static_cast<GTKClient*>(dat)->idleActions();
	gdk_threads_leave();
    }
    return true;
}

static gboolean debugCbInfo(GtkWidget* wid)
{
    gchar* wp = NULL;
    gchar* wcp = NULL;
    gtk_widget_path(wid,NULL,&wp,NULL);
    gtk_widget_class_path(wid,NULL,&wcp,NULL);
    Debug(GTKDriver::self(),DebugAll,"debugCbInfo widget %p path '%s' class path '%s'",
	wid,wp,wcp);
    g_free(wp);
    g_free(wcp);
    return FALSE;
}

static void attachDebug(GtkWidget* wid)
{
    if (wid && s_clickInfo)
	g_signal_connect(G_OBJECT(wid),"button_press_event",G_CALLBACK(debugCbInfo),0);
}

static Widget* getWidget(GtkWidget* wid)
{
    if (!wid)
	return 0;
    return static_cast<Widget*>(g_object_get_data((GObject*)wid,"Yate::Widget"));
}

static GTKWindow* getWidgetWindow(GtkWidget* wid)
{
    if (!wid)
	return 0;
    GtkWidget* top = gtk_widget_get_toplevel(wid);
    if (!top)
	return 0;
    return static_cast<GTKWindow*>(g_object_get_data((GObject*)top,"Yate::Window"));
}

static bool getOptionText(GtkOptionMenu* opt, gint index, String& text)
{
    GtkWidget* menu = gtk_option_menu_get_menu(opt);
    if (!menu)
	return false;
    GList* menuItems = gtk_container_get_children(GTK_CONTAINER(menu));
    GList* l = menuItems;
    while (index && l) {
	index--;
	l = g_list_next(l);
    }
    bool ok = false;
    if (l && GTK_IS_MENU_ITEM(l->data)) {
	GtkWidget* mnu = (GtkWidget*)(l->data);
	GtkWidget* lbl = gtk_bin_get_child(GTK_BIN(mnu));
	if (!lbl)
	    lbl = (GtkWidget*)g_object_get_data((GObject*)mnu,"Yate::Label");
	if (GTK_IS_LABEL(lbl)) {
	    text = gtk_label_get_text(GTK_LABEL(lbl));
	    ok = true;
	}
    }
    g_list_free(menuItems);
    return ok;
}

static int getOptionIndex(GtkOptionMenu* opt, const String& item)
{
    GtkWidget* menu = gtk_option_menu_get_menu(opt);
    if (!menu)
	return -1;
    GList* menuItems = gtk_container_get_children(GTK_CONTAINER(menu));
    GList* l = menuItems;
    int index = 0;
    int pos = -1;
    while (l) {
	if (GTK_IS_MENU_ITEM(l->data)) {
	    GtkWidget* mnu = (GtkWidget*)(l->data);
	    GtkWidget* lbl = gtk_bin_get_child(GTK_BIN(mnu));
	    if (!lbl)
		lbl = (GtkWidget*)g_object_get_data((GObject*)mnu,"Yate::Label");
	    if (GTK_IS_LABEL(lbl) && (item == gtk_label_get_text(GTK_LABEL(lbl)))) {
		pos = index;
		break;
	    }
	}
	index++;
	l = g_list_next(l);
    }
    g_list_free(menuItems);
    return pos;
}

static GtkWidget* getOptionItem(GtkOptionMenu* opt, const String& item)
{
    GtkWidget* menu = gtk_option_menu_get_menu(opt);
    if (!menu)
	return 0;
    GList* menuItems = gtk_container_get_children(GTK_CONTAINER(menu));
    GList* l = menuItems;
    GtkWidget* ret = 0;
    while (l) {
	if (GTK_IS_MENU_ITEM(l->data)) {
	    GtkWidget* mnu = (GtkWidget*)(l->data);
	    GtkWidget* lbl = gtk_bin_get_child(GTK_BIN(mnu));
	    if (!lbl)
		lbl = (GtkWidget*)g_object_get_data((GObject*)mnu,"Yate::Label");
	    if (GTK_IS_LABEL(lbl) && (item == gtk_label_get_text(GTK_LABEL(lbl)))) {
		ret = mnu;
		break;
	    }
	}
	l = g_list_next(l);
    }
    g_list_free(menuItems);
    return ret;
}

static GtkWidget* getListItem(GtkList* lst, const String& item)
{
    GList* listItems = gtk_container_get_children(GTK_CONTAINER(lst));
    GList* l = listItems;
    GtkWidget* ret = 0;
    while (l) {
	if (GTK_IS_LIST_ITEM(l->data)) {
	    GtkWidget* it = (GtkWidget*)(l->data);
	    GtkWidget* lbl = gtk_bin_get_child(GTK_BIN(it));
	    if (!lbl)
		lbl = (GtkWidget*)g_object_get_data((GObject*)it,"Yate::Label");
	    if (GTK_IS_LABEL(lbl) && (item == gtk_widget_get_name(lbl))) {
		ret = it;
		break;
	    }
	}
	l = g_list_next(l);
    }
    g_list_free(listItems);
    return ret;
}

static gboolean widgetCbAction(GtkWidget* wid, gpointer dat)
{
    Debug(GTKDriver::self(),DebugAll,"widgetCbAction(%p,%p)",wid,dat);
    if (GTKClient::changing())
	return FALSE;
    GTKWindow* wnd = getWidgetWindow(wid);
    return wnd && wnd->action(wid);
}

static gboolean widgetCbToggle(GtkWidget* wid, gpointer dat)
{
    Debug(GTKDriver::self(),DebugAll,"widgetCbToggle(%p,%p)",wid,dat);
    if (GTKClient::changing())
	return FALSE;
    GTKWindow* wnd = getWidgetWindow(wid);
    if (!wnd)
	wnd = static_cast<GTKWindow*>(dat);
    if (!wnd)
	return FALSE;
    gboolean active = FALSE;
    if (GTK_IS_TOGGLE_BUTTON(wid))
	active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid));
    else if (GTK_IS_CHECK_MENU_ITEM(wid))
	active = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(wid));
    return wnd->toggle(wid,active);
}

static gboolean widgetCbSelected(GtkOptionMenu* opt, gpointer dat)
{
    Debug(GTKDriver::self(),DebugAll,"widgetCbSelected(%p,%p)",opt,dat);
    if (GTKClient::changing())
	return FALSE;
    GTKWindow* wnd = getWidgetWindow((GtkWidget*)opt);
    return wnd && wnd->select(opt,gtk_option_menu_get_history(opt));
}

static gboolean widgetCbSelection(GtkList* lst, GtkListItem* item, gpointer dat)
{
    Debug(GTKDriver::self(),DebugAll,"widgetCbSelection(%p,%p,%p)",lst,item,dat);
    g_object_set_data((GObject*)lst,"Yate::ListItem",item);
    if (GTKClient::changing())
	return FALSE;
    GTKWindow* wnd = getWidgetWindow((GtkWidget*)lst);
    return wnd && wnd->select(lst,item);
}

static gboolean widgetCbCursorChanged(GtkTreeView* view, gpointer dat)
{
    Debug(GTKDriver::self(),DebugAll,"widgetCbCursorChanged(%p,%p)",view,dat);
    if (GTKClient::changing())
	return FALSE;
    GTKWindow* wnd = getWidgetWindow((GtkWidget*)view);
    return wnd && wnd->select(view);
    GtkTreePath* path = 0;
    gtk_tree_view_get_cursor(view,&path,NULL);
    if (!path)
	return FALSE;
    gtk_tree_path_free(path);
}

static gboolean widgetCbRowActivated(GtkTreeView* view, GtkTreePath* arg1, GtkTreeViewColumn* arg2, gpointer dat)
{
    Debug(GTKDriver::self(),DebugAll,"widgetCbRowActivated(%p,%p,%p,%p)",view,arg1,arg2,dat);
    if (GTKClient::changing())
	return FALSE;
    GTKWindow* wnd = getWidgetWindow((GtkWidget*)view);
    return wnd && wnd->action((GtkWidget*)view);
}

static gboolean widgetCbMinimize(GtkWidget* wid, gpointer dat)
{
    DDebug(GTKDriver::self(),DebugAll,"widgetCbMinimize(%p,%p)",wid,dat);
    GtkWidget* top = gtk_widget_get_toplevel(wid);
    if (!top)
	return FALSE;
    gtk_window_iconify((GtkWindow*)top);
    return TRUE;
}

static gboolean widgetCbMaximize(GtkWidget* wid, gpointer dat)
{
    DDebug(GTKDriver::self(),DebugAll,"widgetCbMaximize(%p,%p)",wid,dat);
    GtkWidget* top = gtk_widget_get_toplevel(wid);
    if (!top)
	return FALSE;
    GTKWindow* wnd = getWidgetWindow(wid);
    if (wnd && (wnd->state() & GDK_WINDOW_STATE_MAXIMIZED))
	gtk_window_unmaximize((GtkWindow*)top);
    else
	gtk_window_maximize((GtkWindow*)top);
    return TRUE;
}

static gboolean widgetCbHide(GtkWidget* wid, gpointer dat)
{
    DDebug(GTKDriver::self(),DebugAll,"widgetCbHide(%p,%p)",wid,dat);
    if (GTKClient::changing())
	return FALSE;
    GTKWindow* wnd = getWidgetWindow(wid);
    if (wnd) {
	wnd->hide();
	return TRUE;
    }
    return FALSE;
}

static gboolean widgetCbShow(GtkWidget* wid, gpointer dat)
{
    const gchar* name = gtk_widget_get_name(wid);
    Debug(GTKDriver::self(),DebugAll,"widgetCbShow(%p,%p) '%s'",wid,dat,name);
    return GTKClient::setVisible(name);
}

static gboolean widgetCbChanged(GtkRange* range, gpointer dat)
{
    const gchar* name = gtk_widget_get_name((GtkWidget*)range);
    Debug(GTKDriver::self(),DebugAll,"widgetCbChanged(%p,%p) '%s'",range,dat,name);
    return FALSE;
}

static gboolean widgetCbSwitch(GtkNotebook* nbk, GtkNotebookPage* page, guint page_num, gpointer dat)
{
    const gchar* name = gtk_widget_get_name(GTK_WIDGET(nbk));
    Debug(GTKDriver::self(),DebugAll,"widgetCbSwitch(%p,%p,%u,%p) '%s'",nbk,page,page_num,dat,name);
    return FALSE;
}

static gboolean widgetCbHelp(GtkWidget* wid, GtkWidgetHelpType typ, gpointer dat)
{
    Debug(GTKDriver::self(),DebugAll,"widgetCbHelp(%p,%d,%p)",wid,typ,dat);
    return FALSE;
}

// Hopefully we'll have no threading issues.
static GtkRadioButton* s_radioGroup = 0;
static String s_skinPath;

static GtkWidget* gtkRadioButtonNew(const gchar* text)
{
    GtkWidget* btn = 0;
    if (s_radioGroup) {
	if (null(text))
	    btn = gtk_radio_button_new_from_widget(s_radioGroup);
	else
	    btn = gtk_radio_button_new_with_label_from_widget(s_radioGroup,text);
    }
    else {
	if (null(text))
	    btn = gtk_radio_button_new(NULL);
	else
	    btn = gtk_radio_button_new_with_label(NULL,text);
	s_radioGroup = GTK_RADIO_BUTTON(btn);
    }
    return btn;
}

static GtkWidget* gtkCheckButtonNew(const gchar* text)
{
    if (null(text))
	return gtk_check_button_new();
    else
	return gtk_check_button_new_with_label(text);
}

static GtkWidget* populateIcon(const gchar* str)
{
    if (null(str))
	return 0;
    String text(str);
    String icon;
    Regexp r("^\"\\([^\"]*\\)\" *\\(.*\\)$");
    if (text.matches(r)) {
	icon = s_skinPath + text.matchString(1);
	text = text.matchString(2);
    }
    if (icon && text) {
	GtkWidget* box = gtk_vbox_new(FALSE,1);
	gtk_container_add(GTK_CONTAINER(box),gtk_image_new_from_file(icon.c_str()));
	gtk_container_add(GTK_CONTAINER(box),gtk_label_new(text.c_str()));
	gtk_widget_show_all(box);
	return box;
    }
    else if (icon)
	return gtk_image_new_from_file(icon.c_str());
    else if (text)
	return gtk_label_new(text.c_str());
    return 0;
}

static GtkWidget* populateButton(GtkWidget* btn, const gchar* str)
{
    if (null(str) || !btn)
	return btn;
    GtkWidget* icon = populateIcon(str);
    if (icon)
	gtk_container_add(GTK_CONTAINER(btn),icon);
    return btn;
}

static GtkWidget* gtkButtonNew(const gchar* text)
{
    return populateButton(gtk_button_new(),text);
}

static GtkWidget* gtkToggleButtonNew(const gchar* text)
{
    return populateButton(gtk_toggle_button_new(),text);
}

static GtkWidget* gtkLeftLabelNew(const gchar* text)
{
    GtkWidget* lbl = gtk_label_new(text);
    if (lbl)
	gtk_misc_set_alignment((GtkMisc*)lbl,0,0);
    return lbl;
}

static GtkWidget* gtkEntryNewWithText(const gchar* text)
{
    GtkWidget* ent = gtk_entry_new();
    if (text)
	gtk_entry_set_text((GtkEntry*)ent,text);
    return ent;
}

static GtkWidget* gtkComboNewWithText(const gchar* text)
{
    GtkWidget* combo = gtk_combo_new();
    if (combo) {
	GtkWidget* ent = GTK_COMBO(combo)->entry;
	if (ent) {
	    gtk_entry_set_text((GtkEntry*)ent,text);
	    attachDebug(ent);
	}
	attachDebug(GTK_COMBO(combo)->list);
    }
    return combo;
}

static GtkWidget* gtkMenuItemNew(const gchar* name, const gchar* text = 0)
{
    if (!text)
	text = name;
    // We don't use gtk_menu_item_new_with_label as we need to
    //  work around not getting the GtkLabel out of GtkMenuItem
    GtkWidget* item = gtk_menu_item_new();
    GtkWidget* label = gtk_label_new(text);
    g_object_set_data((GObject*)item,"Yate::Label",label);
    if (name)
	gtk_widget_set_name(label,name);
    gtk_container_add(GTK_CONTAINER(item),label);
    attachDebug(item);
    attachDebug(label);
    return item;
}

static GtkWidget* gtkListItemNew(const gchar* name, const gchar* text = 0)
{
    if (!text)
	text = name;
    GtkWidget* item = gtk_list_item_new();
    GtkWidget* label = gtk_label_new(text);
    gtk_misc_set_alignment((GtkMisc*)label,0,0);
    g_object_set_data((GObject*)item,"Yate::Label",label);
    if (name)
	gtk_widget_set_name(label,name);
    gtk_container_add(GTK_CONTAINER(item),label);
    attachDebug(item);
    attachDebug(label);
    return item;
}

static GtkWidget* gtkOptionMenuNew(const gchar* text)
{
    GtkWidget* opt = gtk_option_menu_new();
    if (opt) {
	GtkWidget* mnu = gtk_menu_new();
	if (mnu) {
	    String tmp(text);
	    ObjList* l = tmp.split(',');
	    for (ObjList* i = l; i; i = i->next()) {
		String* s = static_cast<String*>(i->get());
		if (s && *s)
		    gtk_menu_shell_append(GTK_MENU_SHELL(mnu),gtkMenuItemNew(s->c_str()));
	    }
	    if (l)
		l->destruct();
	    gtk_option_menu_set_menu(GTK_OPTION_MENU(opt),mnu);
	}
    }
    return opt;
}

static GtkWidget* gtkListNew(const gchar* text)
{
    GtkWidget* lst = gtk_list_new();
    if (lst) {
	GList* list = 0;
	String tmp(text);
	ObjList* l = tmp.split(',');
	for (ObjList* i = l; i; i = i->next()) {
	    String* s = static_cast<String*>(i->get());
	    if (s && *s)
		list = g_list_append(list,gtkListItemNew(s->c_str()));
	}
	if (l)
	    l->destruct();
	if (list)
	    gtk_list_append_items(GTK_LIST(lst),list);
    }
    return lst;
}

static GtkWidget* gtkTableNew(const gchar* text)
{
    String tmp(text);
    ObjList* lst = tmp.split(',',false);
    if (!lst)
	return 0;
    int ncol = lst->count();
    // data column number zero is reserved as row identification
    GType types[MAX_COLUMNS_NUMBER+1];
    if (ncol > MAX_COLUMNS_NUMBER)
	ncol = MAX_COLUMNS_NUMBER;
    int i;
    for (i = 0; i <= ncol; i++)
	types[i] = G_TYPE_STRING;
    GtkTreeModel* store = GTK_TREE_MODEL(gtk_list_store_newv(ncol+1,types));
    GtkWidget* table = gtk_tree_view_new_with_model(store);
    for (i = 0; i < ncol; i++) {
	String* col = static_cast<String*>((*lst)[i]);
	if (!col)
	    continue;
	GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
	// although undocumented we MUST set the "text" attribute which says which data to render
	GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(col->safe(),renderer,
	    "text",i+1,
	    NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(table),column);
    }
    // we can now unref the store as the view will hold a reference on it
    g_object_unref(G_OBJECT(store));
    lst->destruct();
    g_signal_connect(G_OBJECT(table),"row_activated",G_CALLBACK(widgetCbRowActivated),0);
    return table;
}

static GtkWidget* gtkHscaleNew(const gchar* text)
{
    return gtk_hscale_new_with_range(0,100,10);
}

static GtkWidget* gtkVscaleNew(const gchar* text)
{
    return gtk_vscale_new_with_range(0,100,10);
}

static WidgetMaker s_widgetMakers[] = {
    { "label", gtkLeftLabelNew, 0, 0 },
    { "editor", gtkEntryNewWithText, "activate", G_CALLBACK(widgetCbAction) },
    { "button", gtkButtonNew, "clicked", G_CALLBACK(widgetCbAction) },
    { "toggle", gtkToggleButtonNew, "toggled", G_CALLBACK(widgetCbToggle) },
    { "check", gtkCheckButtonNew, "toggled", G_CALLBACK(widgetCbToggle) },
    { "radio", gtkRadioButtonNew, "toggled", G_CALLBACK(widgetCbToggle) },
    { "combo", gtkComboNewWithText, 0, 0 },
    { "option", gtkOptionMenuNew, "changed", G_CALLBACK(widgetCbSelected) },
    { "list", gtkListNew, "select-child", G_CALLBACK(widgetCbSelection) },
    { "table", gtkTableNew, "cursor-changed", G_CALLBACK(widgetCbCursorChanged) },
    { "frame", gtk_frame_new, 0, 0 },
    { "image", gtk_image_new_from_file, 0, 0 },
    { "hseparator", (GBuilder)gtk_hseparator_new, 0, 0 },
    { "vseparator", (GBuilder)gtk_vseparator_new, 0, 0 },
    { "button_show", gtkButtonNew, "clicked", G_CALLBACK(widgetCbShow) },
    { "button_icon", gtkButtonNew, "clicked", G_CALLBACK(widgetCbMinimize) },
    { "button_hide", gtkButtonNew, "clicked", G_CALLBACK(widgetCbHide) },
    { "button_max", gtkButtonNew, "clicked", G_CALLBACK(widgetCbMaximize) },
    { "hscale", gtkHscaleNew, "value-changed", G_CALLBACK(widgetCbChanged) },
    { "vscale", gtkVscaleNew, "value-changed", G_CALLBACK(widgetCbChanged) },
    { 0, 0, 0, 0 },
};
//    { "", gtk__new, "", },

static gboolean windowCbState(GtkWidget* wid, GdkEventWindowState* evt, gpointer dat)
{
    DDebug(GTKDriver::self(),DebugAll,"windowCbState data %p",dat);
    GTKWindow* wnd = static_cast<GTKWindow*>(dat);
    if (wnd && evt)
	wnd->state(evt->new_window_state);
    return FALSE;
}

static gboolean windowCbConfig(GtkWidget* wid, GdkEventConfigure* evt, gpointer dat)
{
    XDebug(GTKDriver::self(),DebugAll,"windowCbConfig data %p",dat);
    if (wid != s_moving)
	return FALSE;
    GTKWindow* wnd = static_cast<GTKWindow*>(dat);
    if (wnd)
	wnd->geometry(evt->x,evt->y,evt->width,evt->height);
    return FALSE;
}

static gboolean windowCbClose(GtkWidget* wid, GdkEvent* evt, gpointer dat)
{
    DDebug(GTKDriver::self(),DebugAll,"windowCbClose event %d data %p",evt->type,dat);
    GTKWindow* wnd = static_cast<GTKWindow*>(dat);
    if (wnd) {
	wnd->hide();
	return TRUE;
    }
    return FALSE;
}

static gboolean windowCbClick(GtkWidget* wid, GdkEventButton* evt, gpointer dat)
{
    DDebug(GTKDriver::self(),DebugAll,"windowCbClick event %d data %p",evt->type,dat);
    if (wid && s_clickInfo)
	debugCbInfo(wid);
    GTKWindow* wnd = static_cast<GTKWindow*>(dat);
    if (evt->type != GDK_BUTTON_PRESS)
	return FALSE;
    if (wnd && (evt->button == 3)) {
	wnd->menu((int)evt->x_root,(int)evt->y_root);
	return TRUE;
    }
    if (evt->button != 1)
	return FALSE;
    if (wnd && !wnd->dragable())
	return FALSE;
    GtkWidget* top = gtk_widget_get_toplevel(wid);
    if (top) {
	s_moving = top;
	if (wnd)
	    wnd->prepare();
	gtk_window_begin_move_drag((GtkWindow*)top,evt->button,(int)evt->x_root,(int)evt->y_root,evt->time);
	return TRUE;
    }
    return FALSE;
}

#ifdef XDEBUG
static gboolean windowCbEvent(GtkWidget* wid, GdkEvent* evt, gpointer dat)
{
    Debug(GTKDriver::self(),DebugAll,"windowCbEvent widget %p event %d data %p",wid,evt->type,dat);
    return FALSE;
}
#endif

static TokenDict s_layoutNames[] = {
    { "fixed", GTKWindow::Fixed },
    { "table", GTKWindow::Table },
    { "infinite", GTKWindow::Infinite },
    { "hbox", GTKWindow::HBox },
    { "vbox", GTKWindow::VBox },
    { "boxed", GTKWindow::Boxed },
    { "tabbed", GTKWindow::Tabbed },
    { "framed", GTKWindow::Framed },
    { "scroll", GTKWindow::Scroll },
    { 0, 0 },
};

static TokenDict s_directions[] = {
    { "left", GTK_POS_LEFT },
    { "right", GTK_POS_RIGHT },
    { "top", GTK_POS_TOP },
    { "bottom", GTK_POS_BOTTOM },
    { 0, 0 },
};

static TokenDict s_shadows[] = {
    { "none", GTK_SHADOW_NONE },
    { "in", GTK_SHADOW_IN },
    { "out", GTK_SHADOW_OUT },
    { "etched_in", GTK_SHADOW_ETCHED_IN },
    { "etched_out", GTK_SHADOW_ETCHED_OUT },
    { 0, 0 },
};

static TokenDict s_reliefs[] = {
    { "full", GTK_RELIEF_NORMAL },
    { "half", GTK_RELIEF_HALF },
    { "none", GTK_RELIEF_NONE },
    { 0, 0 },
};


Widget::Widget()
    : m_widget(0)
{
    Debug(GTKDriver::self(),DebugAll,"Widget::Widget() [%p]",this);
}

Widget::~Widget()
{
    Debug(GTKDriver::self(),DebugAll,"Widget::~Widget() [%p]",this);
    widget(0);
}

void Widget::widget(GtkWidget* wid)
{
    if (wid == m_widget)
	return;
    if (m_widget) {
	g_object_set_data((GObject*)m_widget,"Yate::Widget",0);
	m_widget = 0;
    }
    if (wid) {
	g_object_set_data((GObject*)wid,"Yate::Widget",this);
	g_signal_connect(G_OBJECT(wid),"destroy",G_CALLBACK(destroyCb),this);
	m_widget = wid;
    }
}

void Widget::destroyed()
{
    m_widget = 0;
    delete this;
}

void Widget::destroyCb(GtkObject* obj, gpointer dat)
{
    Debug(GTKDriver::self(),DebugAll,"widgetCbDestroy object %p data %p",obj,dat);
    Widget* w = static_cast<Widget*>(dat);
    if (w)
	w->destroyed();
}


GTKWindow::GTKWindow(const char* id, bool decorated, Layout layout)
    : Window(id), m_decorated(decorated), m_dragable(false), m_layout(layout),
      m_widget(0), m_filler(0), m_state(0),
      m_posX(INVALID_POS), m_posY(INVALID_POS), m_sizeW(0), m_sizeH(0)
{
    m_widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_object_set_data((GObject*)m_widget,"Yate::Window",this);
    gtk_window_set_role((GtkWindow*)m_widget,id);
//    gtk_window_set_type_hint((GtkWindow*)m_widget,GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_window_set_decorated((GtkWindow*)m_widget,decorated);
//    gtk_window_set_resizable((GtkWindow*)m_widget,FALSE);
    gtk_widget_add_events(m_widget,GDK_BUTTON_PRESS_MASK);
    gtk_widget_add_events(m_widget,GDK_BUTTON_RELEASE_MASK);
    g_signal_connect(G_OBJECT(m_widget),"button_press_event",G_CALLBACK(windowCbClick),this);
    g_signal_connect(G_OBJECT(m_widget),"delete_event",G_CALLBACK(windowCbClose),this);
    g_signal_connect(G_OBJECT(m_widget),"configure_event",G_CALLBACK(windowCbConfig),this);
    g_signal_connect(G_OBJECT(m_widget),"window_state_event",G_CALLBACK(windowCbState),this);
    g_signal_connect(G_OBJECT(m_widget),"show_help",G_CALLBACK(widgetCbHelp),this);
#ifdef XDEBUG
    g_signal_connect(G_OBJECT(m_widget),"event",G_CALLBACK(windowCbEvent),this);
#endif
}

GTKWindow::~GTKWindow()
{
    prepare();
    m_widget = 0;
    if (validPos(m_posX,m_posY)) {
	Debug(GTKDriver::self(),DebugAll,"saving '%s' %d,%d",m_id.c_str(),m_posX,m_posY);
	s_save.setValue(m_id,"x",m_posX);
	s_save.setValue(m_id,"y",m_posY);
	s_save.setValue(m_id,"w",m_sizeW);
	s_save.setValue(m_id,"h",m_sizeH);
	if (!m_master)
	    s_save.setValue(m_id,"visible",m_visible);
    }
}

GtkWidget* GTKWindow::find(const String& name) const
{
    if (!(m_filler && name))
	return 0;
    WidgetFinder wf(name);
    return wf.find(GTK_CONTAINER(m_filler));
}

GtkWidget* GTKWindow::container(Layout layout) const
{
    DDebug(GTKDriver::self(),DebugAll,"Creating container type %s (%d)",
	lookup(layout,s_layoutNames,"unknown"),layout);
    switch (layout) {
	case Fixed:
	    return gtk_fixed_new();
	case Table:
	    return gtk_table_new(100,100,FALSE);
	case Infinite:
	    return gtk_layout_new(NULL,NULL);
	case HBox:
	    return gtk_hbox_new(FALSE,0);
	case VBox:
	    return gtk_vbox_new(FALSE,0);
	case Boxed:
	    return gtk_event_box_new();
	case Tabbed:
	    {
		GtkWidget* nbk = gtk_notebook_new();
		g_signal_connect(G_OBJECT(nbk),"switch_page",G_CALLBACK(widgetCbSwitch),0);
		return nbk;
	    }
	case Framed:
	    return gtk_frame_new(NULL);
	case Scroll:
	    return gtk_scrolled_window_new(NULL,NULL);
	default:
	    break;
    }
    return 0;
}

GtkWidget* GTKWindow::container(const String& layout) const
{
    return container((Layout)layout.toInteger(s_layoutNames,Unknown));
}

GtkWidget* GTKWindow::filler()
{
    if (!m_filler) {
	m_filler = container(m_layout);
	if (!m_filler)
	    m_filler = container(HBox);
	if (m_filler)
	    gtk_container_add(GTK_CONTAINER(m_widget),m_filler);
    }
    return m_filler;
}

void GTKWindow::insert(GtkWidget* wid, int x, int y, int w, int h)
{
    Debug(GTKDriver::self(),DebugAll,"Inserting %dx%d widget at %d,%d (%p in %p)",
	w,h,x,y,wid,filler());
    gtk_widget_set_size_request(wid,w,h);
    if (GTK_IS_FIXED(filler()))
	gtk_fixed_put(GTK_FIXED(filler()),wid,x,y);
    else if (GTK_IS_LAYOUT(filler()))
	gtk_layout_put(GTK_LAYOUT(filler()),wid,x,y);
    else if (GTK_IS_BOX(filler()))
	gtk_box_pack_start(GTK_BOX(filler()),wid,(x > 0),(x > 1),y);
    else if (GTK_IS_SCROLLED_WINDOW(filler()) && !GTK_IS_TREE_VIEW(wid))
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(filler()),wid);
    else
	gtk_container_add(GTK_CONTAINER(filler()),wid);
    if (GTK_IS_NOTEBOOK(filler()) && m_tabName)
	gtk_notebook_set_tab_label(GTK_NOTEBOOK(m_filler),wid,populateIcon(m_tabName));
    m_tabName.clear();
}

GtkWidget* GTKWindow::build(const String& type, const String& text)
{
    WidgetMaker* def = s_widgetMakers;
    for (; def->name; def++) {
	if (type == def->name) {
	    GtkWidget* wid = def->builder(text.safe());
	    if (def->sig && def->cb)
		g_signal_connect(G_OBJECT(wid),def->sig,def->cb,0);
	    return wid;
	}
    }
    GenObject* o = s_factories[type];
    WidgetFactory* f = YOBJECT(WidgetFactory,o);
    Widget* w = f ? f->build(text) : 0;
    return w ? w->widget() : 0;
}

void GTKWindow::populate()
{
    gtk_widget_set_name(m_widget,m_id);
    NamedList* sect = s_cfg.getSection(m_id);
    if (!sect)
	return;
    m_dragable = sect->getBoolValue("dragable",true);
    s_radioGroup = 0;
    GtkWidget* containerStack[MAX_CONTAINER_DEPTH];
    GtkTooltips* tips = 0;
    int depth = 0;
    if (m_layout == Unknown)
	m_layout = (Layout)sect->getIntValue("layout",s_layoutNames,GTKWindow::Unknown);
    GtkWidget* lastWidget = filler();
    gtk_widget_set_size_request(lastWidget,sect->getIntValue("width",-1),sect->getIntValue("height",-1));
    int n = sect->length();
    for (int i = 0; i < n; i++) {
	NamedString* p = sect->getParam(i);
	if (!p)
	    continue;
	int x = 0;
	int y = 0;
	int w = -1;
	int h = -1;
	String s(*p);
	s >> x >> "," >> y >> "," >> w >> "," >> h >> ",";
	String act;
	int pos = s.find(',');
	if (pos >= 0) {
	    act = s.substr(0,pos);
	    s = s.substr(pos+1);
	}
	GtkWidget* wid = build(p->name(),s.safe());
	if (wid) {
	    lastWidget = wid;
	    attachDebug(wid);
	    if (act)
		gtk_widget_set_name(wid,act);
	    insert(wid,x,y,w,h);
	    continue;
	}
	if (p->name() == "leave") {
	    lastWidget = 0;
	    if (depth > 0) {
		Debug(GTKDriver::self(),DebugAll,"Popping container off stack of depth %d",depth);
		depth--;
		m_filler = containerStack[depth];
	    }
	    continue;
	}
	else if (p->name() == "tabname") {
	    m_tabName = *p;
	    continue;
	}
	else if (p->name() == "newradio") {
	    s_radioGroup = 0;
	    continue;
	}
	else if (p->name() == "tooltip") {
	    if (*p && lastWidget) {
		if (!tips)
		    tips = gtk_tooltips_new();
		gtk_tooltips_set_tip(tips,lastWidget,p->c_str(),NULL);
	    }
	    else
		Debug(GTKDriver::self(),DebugInfo,"Could not set tooltip '%s' on widget %p",
		    p->c_str(),lastWidget);
	    continue;
	}
	else if (p->name() == "accelerator") {
	    guint keyval = gdk_keyval_from_name(p->safe());
	    if (lastWidget && (keyval != GDK_VoidSymbol)) {
		Debug(GTKDriver::self(),DebugInfo,"Seting accelerator '%s' (0x%06X) on widget %p",
		    p->c_str(),keyval,lastWidget);
		gtk_window_add_mnemonic((GtkWindow*)m_widget,keyval,lastWidget);
	    }
	    else
		Debug(GTKDriver::self(),DebugWarn,"Could not set accelerator '%s' on widget %p",
		    p->c_str(),lastWidget);
	    continue;
	}
	else if (p->name().startsWith("property:")) {
	    if (!lastWidget)
		continue;
	    String tmp = p->name();
	    tmp >> "property:";
	    Debug(GTKDriver::self(),DebugAll,"Setting property '%s' to '%s' in %p",
		tmp.c_str(),p->c_str(),lastWidget);
	    if (tmp.startSkip("int:",false) && tmp)
		g_object_set(G_OBJECT(lastWidget),tmp.c_str(),p->toInteger(),NULL);
	    else if (tmp.startSkip("bool:",false) && tmp)
		g_object_set(G_OBJECT(lastWidget),tmp.c_str(),p->toBoolean(),NULL);
	    else if (tmp.startSkip("str:",false) && tmp)
		g_object_set(G_OBJECT(lastWidget),tmp.c_str(),p->safe(),NULL);
	    else if (tmp.startSkip("pos:",false) && tmp)
		g_object_set(G_OBJECT(lastWidget),tmp.c_str(),p->toInteger(s_directions),NULL);
	    else if (tmp.startSkip("align:",false) && tmp)
		g_object_set(G_OBJECT(lastWidget),tmp.c_str(),(gfloat)p->toInteger(50)*0.01,NULL);
	    else if (tmp.startSkip("relief:",false) && tmp)
		g_object_set(G_OBJECT(lastWidget),tmp.c_str(),p->toInteger(s_reliefs),NULL);
	    else if (tmp.startSkip("shadow:",false) && tmp)
		g_object_set(G_OBJECT(lastWidget),tmp.c_str(),p->toInteger(s_shadows),NULL);
	}
	if (depth >= MAX_CONTAINER_DEPTH)
	    continue;
	wid = container(p->name());
	if (wid) {
	    lastWidget = wid;
	    attachDebug(wid);
	    if (act)
		gtk_widget_set_name(wid,act);
	    insert(wid,x,y,w,h);
	    containerStack[depth] = m_filler;
	    depth++;
	    m_filler = wid;
	    Debug(GTKDriver::self(),DebugAll,"Pushed container %p on stack of depth %d",wid,depth);
	}
    }
    s_radioGroup = 0;
}

void GTKWindow::title(const String& text)
{
    Window::title(text);
    gtk_window_set_title((GtkWindow*)m_widget,m_title.safe());
    setText("title",text);
}

void GTKWindow::init()
{
    title(s_cfg.getValue(m_id,"title",m_id));
    m_master = s_cfg.getBoolValue(m_id,"master");
    m_popup = s_cfg.getBoolValue(m_id,"popup");
    if (!m_master)
	gtk_window_set_type_hint((GtkWindow*)m_widget,GDK_WINDOW_TYPE_HINT_TOOLBAR);
    m_posX = s_save.getIntValue(m_id,"x",m_posX);
    m_posY = s_save.getIntValue(m_id,"y",m_posY);
    m_sizeW = s_save.getIntValue(m_id,"w",m_sizeW);
    m_sizeH = s_save.getIntValue(m_id,"h",m_sizeH);
    bool initial = s_cfg.getBoolValue(m_id,"visible",m_master);
    initial = s_save.getBoolValue(m_id,"visible",initial);
    restore();
    // we realize the widget explicitely to avoid a gtk-win32 bug
    gtk_widget_realize(m_widget);
    // popup windows are not displayed initially
    if (m_popup || !initial) {
	gtk_widget_show_all(filler());
	return;
    }
    gtk_widget_show_all(m_widget);
    m_visible = true;
    if (m_master)
	++s_shown;
    if (GTKClient::self())
	GTKClient::self()->setCheck(m_id,true);
}

void GTKWindow::show()
{
    Debug(GTKDriver::self(),DebugAll,"Window::show() '%s'",m_id.c_str());
    if (m_visible)
	return;
    if (m_master)
	++s_shown;
    gtk_widget_show(m_widget);
    m_visible = true;
    restore();
    if (GTKClient::self())
	GTKClient::self()->setCheck(m_id,true);
}

void GTKWindow::hide()
{
    Debug(GTKDriver::self(),DebugAll,"Window::hide() '%s'",m_id.c_str());
    if (!m_visible)
	return;
    prepare();
    gtk_window_set_modal(GTK_WINDOW(m_widget),FALSE);
    gtk_window_set_transient_for(GTK_WINDOW(m_widget),NULL);
    gtk_widget_hide(m_widget);
    m_visible = false;
    if (m_master)
	--s_shown;
    if (GTKClient::self()) {
	GTKClient::self()->setCheck(m_id,false);
	if (!s_shown)
	    GTKClient::self()->allHidden();
    }
}

void GTKWindow::size(int width, int height)
{
    if (!(width && height))
	return;
    m_sizeW = width;
    m_sizeH = height;
    if (m_widget)
	gtk_window_resize((GtkWindow*)m_widget,m_sizeW,m_sizeH);
}

void GTKWindow::move(int x, int y)
{
    if (!validPos(x,y))
	return;
    m_posX = x;
    m_posY = y;
    if (m_widget)
	gtk_window_move((GtkWindow*)m_widget,m_posX,m_posY);
}

void GTKWindow::moveRel(int dx, int dy)
{
    if (!validPos(m_posX,m_posY))
	return;
    move(m_posX+dx,m_posY+dy);
}

void GTKWindow::geometry(int x, int y, int w, int h)
{
    if (!validPos(m_posX,m_posY))
	return;
    int dx = x - m_posX;
    int dy = y - m_posY;
    m_posX = x;
    m_posY = y;
    m_sizeW = w;
    m_sizeH = h;
    if (!m_visible)
	return;
    XDebug(GTKDriver::self(),DebugAll,"geometry '%s' %d,%d %dx%d moved %d,%d",
	m_id.c_str(),x,y,w,h,dx,dy);
    if (GTKClient::self() && (dx || dy) && m_master && !m_decorated)
	GTKClient::self()->moveRelated(this,dx,dy);
}

bool GTKWindow::prepare()
{
    if (!(m_widget && m_visible))
	return false;
    gtk_window_get_position((GtkWindow*)m_widget,&m_posX,&m_posY);
    gtk_window_get_size((GtkWindow*)m_widget,&m_sizeW,&m_sizeH);
    return true;
}

bool GTKWindow::restore()
{
    if (!m_widget)
	return false;
    if (!validPos(m_posX,m_posY))
	return false;
    move(m_posX,m_posY);
    size(m_sizeW,m_sizeH);
    return true;
}

bool GTKWindow::setParams(const NamedList& params)
{
    bool ok = Window::setParams(params);
    if (params.getBoolValue("modal"))
	gtk_window_set_modal(GTK_WINDOW(m_widget),TRUE);
    return ok;
}

void GTKWindow::setOver(const Window* parent)
{
    GTKWindow* gwnd = YOBJECT(GTKWindow,parent);
    if (gwnd) {
	gtk_window_set_transient_for(GTK_WINDOW(m_widget),GTK_WINDOW(gwnd->widget()));
	if (gwnd->prepare() && validPos(gwnd->m_posX,gwnd->m_posY))
	    move(gwnd->m_posX + (gwnd->m_sizeW - m_sizeW) / 2,
		gwnd->m_posY + (gwnd->m_sizeH - m_sizeH) / 2);
    }
}

bool GTKWindow::action(GtkWidget* wid)
{
    const gchar* name = gtk_widget_get_name(wid);
    Debug(GTKDriver::self(),DebugAll,"action '%s' wid=%p [%p]",
	name,wid,this);
    return GTKClient::self() && GTKClient::self()->action(this,name);
}

bool GTKWindow::toggle(GtkWidget* wid, gboolean active)
{
    const gchar* name = gtk_widget_get_name(wid);
    Debug(GTKDriver::self(),DebugAll,"toggle '%s' wid=%p active=%s [%p]",
	name,wid,String::boolText(active),this);
    return GTKClient::self() && GTKClient::self()->toggle(this,name,active);
}

bool GTKWindow::select(GtkOptionMenu* opt, gint selected)
{
    const gchar* name = gtk_widget_get_name((GtkWidget*)opt);
    Debug(GTKDriver::self(),DebugAll,"select '%s' opt=%p item=%d [%p]",
	name,opt,selected,this);
    String item(name);
    item += selected;
    getOptionText(opt,selected,item);
    return GTKClient::self() && GTKClient::self()->select(this,name,item);
}

bool GTKWindow::select(GtkList* lst, GtkListItem* item)
{
    const gchar* name = gtk_widget_get_name((GtkWidget*)lst);
    Debug(GTKDriver::self(),DebugAll,"select '%s' lst=%p item=%p [%p]",
	name,lst,item,this);
    GtkWidget* lbl = gtk_bin_get_child(GTK_BIN(item));
    if (!lbl)
	lbl = (GtkWidget*)g_object_get_data((GObject*)item,"Yate::Label");
    if (GTK_IS_LABEL(lbl)) {
	String item(gtk_widget_get_name(lbl));
	String val(gtk_label_get_text(GTK_LABEL(lbl)));
	return GTKClient::self() && GTKClient::self()->select(this,name,item,val);
    }
    return false;
}

bool GTKWindow::select(GtkTreeView* view)
{
    const gchar* name = gtk_widget_get_name((GtkWidget*)view);
    Debug(GTKDriver::self(),DebugAll,"select '%s' view=%p [%p]",
	name,view,this);

    GtkTreeModel* model = gtk_tree_view_get_model(view);
    if (!model)
	return false;

    GtkTreePath* path = 0;
    gtk_tree_view_get_cursor(view,&path,NULL);
    if (!path)
	return FALSE;
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter(model,&iter,path))
	return false;
    gtk_tree_path_free(path);

    String item;
    gchar* val = 0;
    // column 0 is reserved for row/item name
    gtk_tree_model_get(model,&iter,0,&val,-1);
    item = val;
    g_free(val);

    return GTKClient::self() && GTKClient::self()->select(this,name,item);
}

bool GTKWindow::setShow(const String& name, bool visible)
{
    GtkWidget* wid = find(name);
    if (!wid)
	return false;
    if (visible)
	gtk_widget_show(wid);
    else
	gtk_widget_hide(wid);
    return true;
}

bool GTKWindow::hasElement(const String& name)
{
    return (find(name) != 0);
}

bool GTKWindow::setActive(const String& name, bool active)
{
    GtkWidget* wid = find(name);
    if (!wid)
	return false;
    gtk_widget_set_sensitive(wid,active);
    return true;
}

bool GTKWindow::setText(const String& name, const String& text)
{
    GtkWidget* wid = find(name);
    if (!wid)
	return false;
    Widget* yw = getWidget(wid);
    return yw ? yw->setText(text) : setText(wid,text);
}

bool GTKWindow::setText(GtkWidget* wid, const String& text)
{
    XDebug(GTKDriver::self(),DebugAll,"GTKWindow::setText(%p,'%s')",wid,text.safe());
    if (GTK_IS_LABEL(wid)) {
	if (text.startsWith("<markup>"))
	    gtk_label_set_markup(GTK_LABEL(wid),text.safe());
	else
	    gtk_label_set_text(GTK_LABEL(wid),text.safe());
	return true;
    }
    if (GTK_IS_BUTTON(wid)) {
	gtk_button_set_label(GTK_BUTTON(wid),text.safe());
	return true;
    }
    if (GTK_IS_ENTRY(wid)) {
	gtk_entry_set_text(GTK_ENTRY(wid),text.safe());
	return true;
    }
    if (GTK_IS_COMBO(wid)) {
	gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(wid)->entry),text.safe());
	return true;
    }
    if (GTK_IS_ADJUSTMENT(wid)) {
	gtk_adjustment_set_value(GTK_ADJUSTMENT(wid),text.toDouble());
	return true;
    }
    return false;
}

bool GTKWindow::setCheck(const String& name, bool checked)
{
    GtkWidget* wid = find(name);
    if (!wid)
	return false;
    Widget* yw = getWidget(wid);
    return yw ? yw->setCheck(checked) : setCheck(wid,checked);
}

bool GTKWindow::setCheck(GtkWidget* wid, bool checked)
{
    XDebug(GTKDriver::self(),DebugAll,"GTKWindow::setCheck(%p,%d)",wid,checked);
    if (GTK_IS_TOGGLE_BUTTON(wid)) {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wid),checked);
	return true;
    }
    if (GTK_IS_CHECK_MENU_ITEM(wid)) {
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wid),checked);
	return true;
    }
    return false;
}

bool GTKWindow::setSelect(const String& name, const String& item)
{
    GtkWidget* wid = find(name);
    if (!wid)
	return false;
    Widget* yw = getWidget(wid);
    return yw ? yw->setSelect(item) : setSelect(wid,item);
}

bool GTKWindow::setSelect(GtkWidget* wid, const String& item)
{
    XDebug(GTKDriver::self(),DebugAll,"GTKWindow::setSelect(%p,'%s')",wid,item.safe());
    if (GTK_IS_OPTION_MENU(wid)) {
	GtkOptionMenu* opt = GTK_OPTION_MENU(wid);
	int i = getOptionIndex(opt,item);
	if (i >= 0) {
	    gtk_option_menu_set_history(opt,i);
	    return true;
	}
	return false;
    }
    return false;
}

bool GTKWindow::setUrgent(const String& name, bool urgent)
{
    GtkWidget* wid = find(name);
    if (!wid)
	return false;
    Widget* yw = getWidget(wid);
    return yw ? yw->setUrgent(urgent) : setUrgent(wid,urgent);
}

bool GTKWindow::setUrgent(GtkWidget* wid, bool urgent)
{
    XDebug(GTKDriver::self(),DebugAll,"GTKWindow::setUrgent(%p,%d)",wid,urgent);
    return false;
}

bool GTKWindow::hasOption(const String& name, const String& item)
{
    GtkWidget* wid = find(name);
    if (!wid)
	return false;
    Widget* yw = getWidget(wid);
    return yw ? yw->hasOption(item) : hasOption(wid,item);
}

bool GTKWindow::hasOption(GtkWidget* wid, const String& item)
{
    XDebug(GTKDriver::self(),DebugAll,"GTKWindow::hasOption(%p,'%s')",wid,item.safe());
    if (GTK_IS_OPTION_MENU(wid)) {
	GtkOptionMenu* opt = GTK_OPTION_MENU(wid);
	GtkWidget* it = getOptionItem(opt,item);
	return (it != 0);
    }
    if (GTK_IS_LIST(wid)) {
	GtkList* lst = GTK_LIST(wid);
	GtkWidget* it = getListItem(lst,item);
	return (it != 0);
    }
    return false;
}

bool GTKWindow::addOption(const String& name, const String& item, bool atStart, const String& text)
{
    GtkWidget* wid = find(name);
    if (!wid)
	return false;
    Widget* yw = getWidget(wid);
    return yw ? yw->addOption(item,atStart,text) : addOption(wid,item,atStart,text);
}

bool GTKWindow::addOption(GtkWidget* wid, const String& item, bool atStart, const String& text)
{
    XDebug(GTKDriver::self(),DebugAll,"GTKWindow::addOption(%p,'%s')",wid,item.safe());
    if (GTK_IS_OPTION_MENU(wid)) {
	if (getOptionItem(GTK_OPTION_MENU(wid),item))
	    return true;
	GtkWidget* mnu = gtk_option_menu_get_menu(GTK_OPTION_MENU(wid));
	if (!GTK_IS_MENU(mnu))
	    return false;
	GtkWidget* child = gtkMenuItemNew(item,text);
	if (child) {
	    if (atStart)
		gtk_menu_shell_prepend(GTK_MENU_SHELL(mnu),child);
	    else
		gtk_menu_shell_append(GTK_MENU_SHELL(mnu),child);
	    gtk_widget_show_all(child);
	    return true;
	}
	return false;
    }
    if (GTK_IS_LIST(wid)) {
	GtkWidget* li = gtkListItemNew(item,text);
	if (!li)
	    return false;
	GList* list = g_list_append(NULL,li);
	if (list) {
	    if (atStart)
		gtk_list_prepend_items(GTK_LIST(wid),list);
	    else
		gtk_list_append_items(GTK_LIST(wid),list);
	    gtk_widget_show_all(li);
	    return true;
	}
	return false;
    }
    return false;
}

bool GTKWindow::delOption(const String& name, const String& item)
{
    GtkWidget* wid = find(name);
    if (!wid)
	return false;
    Widget* yw = getWidget(wid);
    return yw ? yw->delOption(item) : delOption(wid,item);
}

bool GTKWindow::delOption(GtkWidget* wid, const String& item)
{
    XDebug(GTKDriver::self(),DebugAll,"GTKWindow::delOption(%p,'%s')",wid,item.safe());
    if (GTK_IS_OPTION_MENU(wid)) {
	GtkOptionMenu* opt = GTK_OPTION_MENU(wid);
	GtkWidget* it = getOptionItem(opt,item);
	if (it) {
	    gtk_widget_destroy(it);
	    return true;
	}
	return false;
    }
    if (GTK_IS_LIST(wid)) {
	GtkList* lst = GTK_LIST(wid);
	GtkWidget* sel = (GtkWidget*)g_object_get_data((GObject*)wid,"Yate::ListItem");
	GtkWidget* it = getListItem(lst,item);
	if (it) {
	    if (it == sel)
		g_object_set_data((GObject*)wid,"Yate::ListItem",NULL);
	    GList* list = g_list_append(NULL,it);
	    gtk_list_remove_items(lst,list);
	    return true;
	}
	return false;
    }
    return false;
}

bool GTKWindow::addTableRow(const String& name, const String& item, const NamedList* data, bool atStart)
{
    GtkWidget* wid = find(name);
    if (!wid)
	return false;
    Widget* yw = getWidget(wid);
    return yw ? yw->addTableRow(item,data,atStart) : addTableRow(wid,item,data,atStart);
}

bool GTKWindow::delTableRow(const String& name, const String& item)
{
    GtkWidget* wid = find(name);
    if (!wid)
	return false;
    Widget* yw = getWidget(wid);
    return yw ? yw->delTableRow(item) : delTableRow(wid,item);
}

bool GTKWindow::setTableRow(const String& name, const String& item, const NamedList* data)
{
    GtkWidget* wid = find(name);
    if (!wid)
	return false;
    Widget* yw = getWidget(wid);
    return yw ? yw->setTableRow(item,data) : setTableRow(wid,item,data);
}

bool GTKWindow::getTableRow(const String& name, const String& item, NamedList* data)
{
    GtkWidget* wid = find(name);
    if (!wid)
	return false;
    Widget* yw = getWidget(wid);
    return yw ? yw->getTableRow(item,data) : getTableRow(wid,item,data);
}

bool GTKWindow::clearTable(const String& name)
{
    GtkWidget* wid = find(name);
    if (!wid)
	return false;
    Widget* yw = getWidget(wid);
    return yw ? yw->clearTable() : clearTable(wid);
}

bool GTKWindow::addTableRow(GtkWidget* wid, const String& item, const NamedList* data, bool atStart)
{
    DDebug(GTKDriver::self(),DebugInfo,"GTKWindow::addTableRow(%p,'%s',%p,%s)",
	wid,item.c_str(),data,String::boolText(atStart));
    if (GTK_IS_TREE_VIEW(wid)) {
	GtkTreeView* view = GTK_TREE_VIEW(wid);
	GtkTreeModel* model = gtk_tree_view_get_model(view);
	GtkListStore* store = GTK_LIST_STORE(model);
	if (!store)
	    return false;

	int ncol = gtk_tree_model_get_n_columns(model);
	DDebug(GTKDriver::self(),DebugInfo,"adding %d columns row to store %p of view %p",
	    ncol,store,view);
	GtkTreeIter iter;
	if (atStart)
	    gtk_list_store_insert(store,&iter,0);
	else
	    gtk_list_store_append(store,&iter);

	if (!data)
	    return true;

	// data column number zero is used as row identification
	gtk_list_store_set(store,&iter,0,item.safe(),-1);

	for (int i = 0; i < ncol; i++) {
	    GtkTreeViewColumn* column = gtk_tree_view_get_column(view,i);
	    if (!column)
		continue;
	    String name = gtk_tree_view_column_get_title(column);
	    name.toLower();
	    const String* param = data->getParam(name);
	    if (param)
		gtk_list_store_set(store,&iter,i+1,param->safe(),-1);
	}
	return true;
    }
    return false;
}

// Helper function to find a row by name and set a GetTreeIter to it
static bool findTableRow(GtkTreeModel* model, const String& item, GtkTreeIter* iter)
{
    if (!gtk_tree_model_get_iter_first(model,iter))
	return false;
    for (;;) {
	gchar* val = 0;
	// column 0 is reserved for row/item name
	gtk_tree_model_get(model,iter,0,&val,-1);
	bool found = (item == val);
	g_free(val);
	if (found)
	    return true;
	if (!gtk_tree_model_iter_next(model,iter))
	    break;
    }
    return false;
}

static bool findTableRow(GtkTreeView* view, const String& item, GtkTreeIter* iter)
{
    GtkTreeModel* model = gtk_tree_view_get_model(view);
    if (!model)
	return false;
    if (item.null()) {
	// find currently selected table row
	GtkTreePath* path = 0;
	gtk_tree_view_get_cursor(view,&path,NULL);
	if (!path)
	    return false;
	bool ok = gtk_tree_model_get_iter(model,iter,path);
	gtk_tree_path_free(path);
	return ok;
    }
    return findTableRow(model,item,iter);
}

bool GTKWindow::delTableRow(GtkWidget* wid, const String& item)
{
    DDebug(GTKDriver::self(),DebugInfo,"GTKWindow::delTableRow(%p,'%s')",
	wid,item.c_str());
    if (GTK_IS_TREE_VIEW(wid)) {
	GtkTreeView* view = GTK_TREE_VIEW(wid);
	GtkTreeModel* model = gtk_tree_view_get_model(view);
	if (!model)
	    return false;
	GtkListStore* store = GTK_LIST_STORE(model);
	if (!store)
	    return false;
	GtkTreeIter iter;
	if (!findTableRow(model,item,&iter))
	    return false;
	gtk_list_store_remove(store,&iter);
	return true;
    }
    return false;
}

bool GTKWindow::setTableRow(GtkWidget* wid, const String& item, const NamedList* data)
{
    DDebug(GTKDriver::self(),DebugInfo,"GTKWindow::setTableRow(%p,'%s',%p)",
	wid,item.c_str(),data);
    if (GTK_IS_TREE_VIEW(wid)) {
	GtkTreeView* view = GTK_TREE_VIEW(wid);
	GtkTreeModel* model = gtk_tree_view_get_model(view);
	if (!model)
	    return false;
	GtkListStore* store = GTK_LIST_STORE(model);
	if (!store)
	    return false;
	GtkTreeIter iter;
	if (!findTableRow(view,item,&iter))
	    return false;

	int ncol = gtk_tree_model_get_n_columns(model);
	for (int i = 0; i < ncol; i++) {
	    GtkTreeViewColumn* column = gtk_tree_view_get_column(view,i);
	    if (!column)
		continue;
	    String name = gtk_tree_view_column_get_title(column);
	    name.toLower();
	    const String* param = data->getParam(name);
	    if (param)
		gtk_list_store_set(store,&iter,i+1,param->safe(),-1);
	}
    }
    return false;
}

bool GTKWindow::getTableRow(GtkWidget* wid, const String& item, NamedList* data)
{
    DDebug(GTKDriver::self(),DebugInfo,"GTKWindow::getTableRow(%p,'%s',%p)",
	wid,item.c_str(),data);
    if (GTK_IS_TREE_VIEW(wid)) {
	GtkTreeView* view = GTK_TREE_VIEW(wid);
	GtkTreeModel* model = gtk_tree_view_get_model(view);
	if (!model)
	    return false;
	GtkTreeIter iter;
	if (!findTableRow(view,item,&iter)) {
	    Debug(GTKDriver::self(),DebugMild,"Could not find row '%s' in table %p",
		item.c_str(),wid);
	    return false;
	}
	if (data) {
	    int ncol = gtk_tree_model_get_n_columns(model);
	    for (int i = 0; i < ncol; i++) {
		GtkTreeViewColumn* column = gtk_tree_view_get_column(view,i);
		if (!column)
		    continue;
		String name = gtk_tree_view_column_get_title(column);
		name.toLower();
		gchar* val = 0;
		// read past column 0 which is reserved for row/item name
		gtk_tree_model_get(model,&iter,i+1,&val,-1);
		data->setParam(name,val);
		g_free(val);
	    }
	}
	return true;
    }
    return false;
}

bool GTKWindow::clearTable(GtkWidget* wid)
{
    DDebug(GTKDriver::self(),DebugInfo,"GTKWindow::clearTable(%p)",wid);
    if (GTK_IS_TREE_VIEW(wid)) {
	GtkTreeView* view = GTK_TREE_VIEW(wid);
	GtkTreeModel* model = gtk_tree_view_get_model(view);
	if (!model)
	    return false;
	GtkListStore* store = GTK_LIST_STORE(model);
	if (!store)
	    return false;
	gtk_list_store_clear(store);
	return true;
    }
    return false;
}

bool GTKWindow::getText(const String& name, String& text)
{
    GtkWidget* wid = find(name);
    if (!wid)
	return false;
    Widget* yw = getWidget(wid);
    return yw ? yw->getText(text) : getText(wid,text);
}

bool GTKWindow::getText(GtkWidget* wid, String& text)
{
    XDebug(GTKDriver::self(),DebugAll,"GTKWindow::getText(%p)",wid);
    if (GTK_IS_LABEL(wid)) {
	text = gtk_label_get_text(GTK_LABEL(wid));
	return true;
    }
    if (GTK_IS_ENTRY(wid)) {
	text = gtk_entry_get_text(GTK_ENTRY(wid));
	return true;
    }
    if (GTK_IS_COMBO(wid)) {
	text = gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(wid)->entry));
	return true;
    }
    if (GTK_IS_OPTION_MENU(wid)) {
	GtkOptionMenu* opt = GTK_OPTION_MENU(wid);
	return getOptionText(opt,gtk_option_menu_get_history(opt),text);
    }
    if (GTK_IS_LIST(wid)) {
	GtkWidget* it = (GtkWidget*)g_object_get_data((GObject*)wid,"Yate::ListItem");
	if (it) {
	    GtkWidget* lbl = gtk_bin_get_child(GTK_BIN(it));
	    if (!lbl)
		lbl = (GtkWidget*)g_object_get_data((GObject*)it,"Yate::Label");
	    if (GTK_IS_LABEL(lbl)) {
		text = gtk_label_get_text(GTK_LABEL(lbl));
		return true;
	    }
	}
	return false;
    }
    return false;
}

bool GTKWindow::getCheck(const String& name, bool& checked)
{
    GtkWidget* wid = find(name);
    if (!wid)
	return false;
    Widget* yw = getWidget(wid);
    return yw ? yw->getCheck(checked) : getCheck(wid,checked);
}

bool GTKWindow::getCheck(GtkWidget* wid, bool& checked)
{
    XDebug(GTKDriver::self(),DebugAll,"GTKWindow::getCheck(%p)",wid);
    if (GTK_IS_TOGGLE_BUTTON(wid)) {
	checked = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid));
	return true;
    }
    if (GTK_IS_CHECK_MENU_ITEM(wid)) {
	checked = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(wid));
	return true;
    }
    return false;
}

bool GTKWindow::getSelect(const String& name, String& item)
{
    GtkWidget* wid = find(name);
    if (!wid)
	return false;
    Widget* yw = getWidget(wid);
    return yw ? yw->getSelect(item) : getSelect(wid,item);
}

bool GTKWindow::getSelect(GtkWidget* wid, String& item)
{
    XDebug(GTKDriver::self(),DebugAll,"GTKWindow::getSelect(%p)",wid);
    if (GTK_IS_OPTION_MENU(wid)) {
	GtkOptionMenu* opt = GTK_OPTION_MENU(wid);
	return getOptionText(opt,gtk_option_menu_get_history(opt),item);
    }
    if (GTK_IS_LIST(wid)) {
	GtkWidget* it = (GtkWidget*)g_object_get_data((GObject*)wid,"Yate::ListItem");
	if (it) {
	    GtkWidget* lbl = gtk_bin_get_child(GTK_BIN(it));
	    if (!lbl)
		lbl = (GtkWidget*)g_object_get_data((GObject*)it,"Yate::Label");
	    if (GTK_IS_LABEL(lbl)) {
		item = gtk_widget_get_name(lbl);
		return true;
	    }
	}
	return false;
    }
    if (GTK_IS_TREE_VIEW(wid)) {
	GtkTreeView* view = GTK_TREE_VIEW(wid);
	GtkTreeModel* model = gtk_tree_view_get_model(view);
	if (!model)
	    return false;

	GtkTreePath* path = 0;
	gtk_tree_view_get_cursor(view,&path,NULL);
	if (!path)
	    return false;
	GtkTreeIter iter;
	if (!gtk_tree_model_get_iter(model,&iter,path))
	    return false;
	gtk_tree_path_free(path);

	gchar* val = 0;
	// column 0 is reserved for row/item name
	gtk_tree_model_get(model,&iter,0,&val,-1);
	item = val;
	g_free(val);
	return true;
    }
    return false;
}

void GTKWindow::menu(int x, int y)
{
    GtkWidget* mnu = 0;
    ObjList* wnds = GTKClient::listWindows();
    for (ObjList* l = wnds; l; l = l->next()) {
	String* s = static_cast<String*>(l->get());
	if (!s || s->null())
	    continue;
	Window* w = GTKClient::getWindow(*s);
	if (!w)
	    continue;
	if (!Engine::config().getBoolValue("client","fullmenu")) {
	    if (w->master() || w->popup())
		continue;
	}
	if (!mnu)
	    mnu = gtk_menu_new();
	GtkWidget* item = gtk_check_menu_item_new_with_label(w->title().safe());
	gtk_widget_set_name(item,s->c_str());
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),w->visible());
	g_signal_connect(G_OBJECT(item),"toggled",G_CALLBACK(widgetCbToggle),this);
	gtk_menu_shell_append((GtkMenuShell*)mnu,item);
    }
    delete wnds;
    if (!mnu)
	return;
    gtk_widget_show_all(mnu);
    gtk_menu_popup((GtkMenu*)mnu,NULL,NULL,NULL,NULL,3,gtk_get_current_event_time());
}


GTKClient::GTKClient()
    : Client("GTKClient")
{
    m_oneThread = Engine::config().getBoolValue("client","onethread",ONE_THREAD);
    s_skinPath = Engine::config().getValue("client","skinbase");
    if (s_skinPath.null())
	s_skinPath << Engine::modulePath() << Engine::pathSeparator() << "skin";
    if (!s_skinPath.endsWith(Engine::pathSeparator()))
	s_skinPath << Engine::pathSeparator();
    String skin(Engine::config().getValue("client","skin","default"));
    if (skin)
	s_skinPath << skin;
    if (!s_skinPath.endsWith(Engine::pathSeparator()))
	s_skinPath << Engine::pathSeparator();
    s_cfg = s_skinPath + "gtk2client.ui";
    s_cfg.load();
    s_save = Engine::configFile("gtk2client");
    s_save.load();
}

GTKClient::~GTKClient()
{
    m_windows.clear();
    s_save.save();
}

void GTKClient::lock()
{
    XDebug(GTKDriver::self(),DebugAll,"GTKClient::lock()");
    gdk_threads_enter();
}

void GTKClient::unlock()
{
    XDebug(GTKDriver::self(),DebugAll,"GTKClient::unlock()");
    gdk_flush();
    gdk_threads_leave();
}

void GTKClient::main()
{
    if (!m_windows.count()) {
	Debug(DebugGoOn,"Gtk Client refusing to start with no windows loaded!");
	Engine::halt(1);
    }
    lock();
    gtk_main();
    unlock();
}

void GTKClient::allHidden()
{
    Debug(GTKDriver::self(),DebugInfo,"All %u windows hidden",m_windows.count());
    gtk_main_quit();
}

bool GTKClient::createWindow(const String& name)
{
    Window* w = 0;
    GenObject* o = s_factories[name];
    const WindowFactory* f = YOBJECT(WindowFactory,o);
    if (f)
	w = f->build();
    else
	w = new GTKWindow(name,s_cfg.getBoolValue(name,"decorated"));
    if (!w) {
	Debug(GTKDriver::self(),DebugGoOn,"Could not create window '%s'",name.c_str());
	return false;
    }
    w->populate();
    m_windows.append(w);
    return true;
}

void GTKClient::loadWindows()
{
    gtk_rc_parse(s_skinPath + "gtk2client.rc");
    unsigned int n = s_cfg.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* l = s_cfg.getSection(i);
	if (l && l->getBoolValue("enabled",true))
	    createWindow(*l);
    }
#ifdef BUGGY_IDLE
    // don't use gtk_idle_add - it hogs the CPU on Windows
    g_timeout_add(1,gtkIdleCb,this);
#else
    // but on Linux the 1ms timeout makes the UI crawl...
    gtk_idle_add(gtkIdleCb,this);
#endif
}


GTKDriver::GTKDriver()
{
}

GTKDriver::~GTKDriver()
{
}

void GTKDriver::initialize()
{
    Output("Initializing module GTK2 client");
    s_device = Engine::config().getValue("client","device",DEFAULT_DEVICE);
    if (!GTKClient::self())
    {
	s_clickInfo = Engine::config().getBoolValue("client","clickinfo");
	debugCopy();
	new GTKClient;
	GTKClient::self()->startup();
    }
    setup();
}

bool GTKDriver::factory(UIFactory* factory, const char* type)
{
    if (!type) {
	s_factories.remove(factory,false);
	return true;
    }
    String tmp(type);
    if (tmp == "gtk2") {
	s_factories.append(factory)->setDelete(false);
	return true;
    }
    return false;
}

WindowFactory::WindowFactory(const char* type, const char* name)
    : UIFactory(type,name)
{
}
    

WidgetFactory::WidgetFactory(const char* type, const char* name)
    : UIFactory(type,name)
{
}

/* vi: set ts=8 sw=4 sts=4 noet: */
