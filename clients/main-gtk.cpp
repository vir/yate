/**
 * gtkclient.cpp
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <yatephone.h>

#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <malloc.h>
#include <gtk/gtk.h>

#define STATUS_IDLE	    0
#define STATUS_RINGIN	    1
#define STATUS_RINGOUT	    2
#define STATUS_INCALL	    3


using namespace TelEngine;

typedef struct _g_atexit_func {
    struct _g_atexit_func* next;
    void (*func)(void);
} g_atexit_func_t;

static g_atexit_func_t* _g_atexit_func_head = (g_atexit_func_t*)0;


//this is a hack for gtk to close himself nice and without a core.
extern "C"
void g_atexit(GVoidFunc func)
{
    g_atexit_func_t* it;
    if(!func)
	return;
    if(!(it = (g_atexit_func_t*)malloc(sizeof(g_atexit_func_t))))
	return;
    it->func = func;
    it->next = _g_atexit_func_head;
    _g_atexit_func_head = it;
    Debug(DebugInfo, "g_atexit: registered function %p\n", func);
}

void g_atexit_unwind()
{
    g_atexit_func_t* it;
    while(_g_atexit_func_head) {
        _g_atexit_func_head->func();
	Debug(DebugInfo, "g_atexit_unwind: called function %p\n", _g_atexit_func_head->func);
	it = _g_atexit_func_head;
	_g_atexit_func_head = _g_atexit_func_head->next;
	free(it);
    }
}


class GtkClient;

GtkClient *s_client = 0;

static Configuration s_cfg;

class GtkClient : public Thread
{
public:
    GtkClient(void)
	: Thread("GtkClient"), m_yate(0), status(0)
	{ Debug(DebugInfo,"GtkClient::GtkClient"); s_client = this;}
    ~GtkClient(void);
    GtkWidget* 
    create_yate		  (void);
static gboolean 
    on_yate_destroy_event (GtkWidget       *widget,
                                    GdkEvent        *event,
                                    gpointer         user_data)
    { s_client->m_yate = 0;Engine::halt(0);return true;}

static void
    on_connection_activate	    (GtkMenuItem     *menuitem,
                                     gpointer         user_data)
    {}
static void
    on_properties1_activate         (GtkMenuItem     *menuitem,
                                     gpointer         user_data)
    {}
    

static void
    gtk_call			    (GtkWidget     *button,
                                     gpointer         data);
static void
    gtk_hangup			    (GtkWidget     *button,
                                     gpointer         data);

static void 
    gtk_buttons			    (GtkWidget *button, 
                                     gpointer         data);
    
    void run(void);	
    void set_state(int new_state);
    
    GtkWidget *m_statusbar;
    GtkWidget* m_yate;
    int status;
    GtkWidget *m_call;
    GtkWidget *m_hangup;
    GtkWidget *m_address;
};


GtkWidget*  GtkClient::create_yate (void)
{
  GtkWidget *yate;
  GtkWidget *vpaned1;
  GtkWidget *menu;
  GtkWidget *connection;
  GtkWidget *connection_menu;
  GtkAccelGroup *connection_menu_accels;
  GtkWidget *properties;
  GtkWidget *fixed1;
  GtkWidget *b1;
  GtkWidget *b2;
  GtkWidget *b3;
  GtkWidget *b4;
  GtkWidget *b5;
  GtkWidget *b6;
  GtkWidget *b7;
  GtkWidget *b8;
  GtkWidget *b9;
  GtkWidget *b0;
  GtkWidget *bgrid;
  GtkWidget *bstar;
  GtkWidget *combo_entry1;
  GtkWidget *label1;

  yate = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_object_set_data (GTK_OBJECT (yate), "yate", yate);
  gtk_widget_set_usize (yate, 300, 300);
  gtk_window_set_title (GTK_WINDOW (yate), ("YateClient"));
  gtk_window_set_default_size (GTK_WINDOW (yate), 300, 300);
  gtk_window_set_policy (GTK_WINDOW (yate), FALSE, FALSE, FALSE);

  vpaned1 = gtk_vpaned_new ();
  gtk_widget_ref (vpaned1);
  gtk_object_set_data_full (GTK_OBJECT (yate), "vpaned1", vpaned1,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (vpaned1);
  gtk_container_add (GTK_CONTAINER (yate), vpaned1);
  gtk_widget_set_usize (vpaned1, 304, 306);
  gtk_paned_set_handle_size (GTK_PANED (vpaned1), 1);
  gtk_paned_set_gutter_size (GTK_PANED (vpaned1), 1);
  gtk_paned_set_position (GTK_PANED (vpaned1), 28);

  menu = gtk_menu_bar_new ();
  gtk_widget_ref (menu);
  gtk_object_set_data_full (GTK_OBJECT (yate), "menu", menu,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (menu);
  gtk_paned_pack1 (GTK_PANED (vpaned1), menu, FALSE, TRUE);

  connection = gtk_menu_item_new_with_label (("Connection"));
  gtk_widget_ref (connection);
  gtk_object_set_data_full (GTK_OBJECT (yate), "connection", connection,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (connection);
  gtk_container_add (GTK_CONTAINER (menu), connection);

  connection_menu = gtk_menu_new ();
  gtk_widget_ref (connection_menu);
  gtk_object_set_data_full (GTK_OBJECT (yate), "connection_menu", connection_menu,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (connection), connection_menu);
  connection_menu_accels = gtk_menu_ensure_uline_accel_group (GTK_MENU (connection_menu));

  properties = gtk_menu_item_new_with_label (("Properties"));
  gtk_widget_ref (properties);
  gtk_object_set_data_full (GTK_OBJECT (yate), "properties", properties,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (properties);
  gtk_container_add (GTK_CONTAINER (connection_menu), properties);

  fixed1 = gtk_fixed_new ();
  gtk_widget_ref (fixed1);
  gtk_object_set_data_full (GTK_OBJECT (yate), "fixed1", fixed1,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (fixed1);
  gtk_paned_pack2 (GTK_PANED (vpaned1), fixed1, TRUE, TRUE);

  m_call = gtk_button_new_with_label (("Call"));
  gtk_widget_ref (m_call);
  gtk_object_set_data_full (GTK_OBJECT (yate), "call", m_call,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (m_call);
  gtk_fixed_put (GTK_FIXED (fixed1), m_call, 16, 56);
  gtk_widget_set_uposition (m_call, 16, 56);
  gtk_widget_set_usize (m_call, 96, 40);

  m_hangup = gtk_button_new_with_label (("Reject"));
  gtk_widget_ref (m_hangup);
  gtk_object_set_data_full (GTK_OBJECT (yate), "hangup", m_hangup,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (m_hangup);
  gtk_fixed_put (GTK_FIXED (fixed1), m_hangup, 120, 56);
  gtk_widget_set_uposition (m_hangup, 120, 56);
  gtk_widget_set_usize (m_hangup, 96, 40);

  b1 = gtk_button_new_with_label (("1"));
  gtk_widget_ref (b1);
  gtk_object_set_data_full (GTK_OBJECT (yate), "b1", b1,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (b1);
  gtk_fixed_put (GTK_FIXED (fixed1), b1, 16, 104);
  gtk_widget_set_uposition (b1, 16, 104);
  gtk_widget_set_usize (b1, 32, 24);

  b2 = gtk_button_new_with_label (("2"));
  gtk_widget_ref (b2);
  gtk_object_set_data_full (GTK_OBJECT (yate), "b2", b2,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (b2);
  gtk_fixed_put (GTK_FIXED (fixed1), b2, 56, 104);
  gtk_widget_set_uposition (b2, 56, 104);
  gtk_widget_set_usize (b2, 32, 24);

  b3 = gtk_button_new_with_label (("3"));
  gtk_widget_ref (b3);
  gtk_object_set_data_full (GTK_OBJECT (yate), "b3", b3,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (b3);
  gtk_fixed_put (GTK_FIXED (fixed1), b3, 96, 104);
  gtk_widget_set_uposition (b3, 96, 104);
  gtk_widget_set_usize (b3, 32, 24);

  b4 = gtk_button_new_with_label (("4"));
  gtk_widget_ref (b4);
  gtk_object_set_data_full (GTK_OBJECT (yate), "b4", b4,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (b4);
  gtk_fixed_put (GTK_FIXED (fixed1), b4, 16, 136);
  gtk_widget_set_uposition (b4, 16, 136);
  gtk_widget_set_usize (b4, 32, 24);

  b5 = gtk_button_new_with_label (("5"));
  gtk_widget_ref (b5);
  gtk_object_set_data_full (GTK_OBJECT (yate), "b5", b5,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (b5);
  gtk_fixed_put (GTK_FIXED (fixed1), b5, 56, 136);
  gtk_widget_set_uposition (b5, 56, 136);
  gtk_widget_set_usize (b5, 32, 24);

  b6 = gtk_button_new_with_label (("6"));
  gtk_widget_ref (b6);
  gtk_object_set_data_full (GTK_OBJECT (yate), "b6", b6,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (b6);
  gtk_fixed_put (GTK_FIXED (fixed1), b6, 96, 136);
  gtk_widget_set_uposition (b6, 96, 136);
  gtk_widget_set_usize (b6, 32, 24);

  b7 = gtk_button_new_with_label (("7"));
  gtk_widget_ref (b7);
  gtk_object_set_data_full (GTK_OBJECT (yate), "b7", b7,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (b7);
  gtk_fixed_put (GTK_FIXED (fixed1), b7, 16, 168);
  gtk_widget_set_uposition (b7, 16, 168);
  gtk_widget_set_usize (b7, 32, 24);

  b8 = gtk_button_new_with_label (("8"));
  gtk_widget_ref (b8);
  gtk_object_set_data_full (GTK_OBJECT (yate), "b8", b8,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (b8);
  gtk_fixed_put (GTK_FIXED (fixed1), b8, 56, 168);
  gtk_widget_set_uposition (b8, 56, 168);
  gtk_widget_set_usize (b8, 32, 24);

  b9 = gtk_button_new_with_label (("9"));
  gtk_widget_ref (b9);
  gtk_object_set_data_full (GTK_OBJECT (yate), "b9", b9,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (b9);
  gtk_fixed_put (GTK_FIXED (fixed1), b9, 96, 168);
  gtk_widget_set_uposition (b9, 96, 168);
  gtk_widget_set_usize (b9, 32, 24);

  b0 = gtk_button_new_with_label (("0"));
  gtk_widget_ref (b0);
  gtk_object_set_data_full (GTK_OBJECT (yate), "b0", b0,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (b0);
  gtk_fixed_put (GTK_FIXED (fixed1), b0, 56, 200);
  gtk_widget_set_uposition (b0, 56, 200);
  gtk_widget_set_usize (b0, 32, 24);

  bgrid = gtk_button_new_with_label (("#"));
  gtk_widget_ref (bgrid);
  gtk_object_set_data_full (GTK_OBJECT (yate), "bgrid", bgrid,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (bgrid);
  gtk_fixed_put (GTK_FIXED (fixed1), bgrid, 96, 200);
  gtk_widget_set_uposition (bgrid, 96, 200);
  gtk_widget_set_usize (bgrid, 32, 24);

  bstar = gtk_button_new_with_label (("*"));
  gtk_widget_ref (bstar);
  gtk_object_set_data_full (GTK_OBJECT (yate), "bstar", bstar,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (bstar);
  gtk_fixed_put (GTK_FIXED (fixed1), bstar, 16, 200);
  gtk_widget_set_uposition (bstar, 16, 200);
  gtk_widget_set_usize (bstar, 32, 24);

  m_address = gtk_combo_new ();
  gtk_widget_ref (m_address);
  gtk_object_set_data_full (GTK_OBJECT (yate), "address", m_address,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (m_address);
  gtk_fixed_put (GTK_FIXED (fixed1), m_address, 16, 24);
  gtk_widget_set_uposition (m_address, 16, 24);
  gtk_widget_set_usize (m_address, 272, 24);

  combo_entry1 = GTK_COMBO (m_address)->entry;
  gtk_widget_ref (combo_entry1);
  gtk_object_set_data_full (GTK_OBJECT (yate), "combo_entry1", combo_entry1,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (combo_entry1);

  label1 = gtk_label_new (("Called address:"));
  gtk_widget_ref (label1);
  gtk_object_set_data_full (GTK_OBJECT (yate), "label1", label1,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (label1);
  gtk_fixed_put (GTK_FIXED (fixed1), label1, 16, 8);
  gtk_widget_set_uposition (label1, 16, 8);
  gtk_widget_set_usize (label1, 88, 16);
  gtk_misc_set_alignment (GTK_MISC (label1), 0, 0.5);

  m_statusbar = gtk_statusbar_new ();
  gtk_widget_ref (m_statusbar);
  gtk_object_set_data_full (GTK_OBJECT (yate), "statusbar", m_statusbar,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (m_statusbar);
  gtk_fixed_put (GTK_FIXED (fixed1), m_statusbar, 0, 248);
  gtk_widget_set_uposition (m_statusbar, 0, 248);
  gtk_widget_set_usize (m_statusbar, 304, 24);


// here we setup the signals for things that have to do with window.  
  gtk_signal_connect (GTK_OBJECT (yate), "destroy",
                      GTK_SIGNAL_FUNC (on_yate_destroy_event),
                      NULL);
  gtk_signal_connect (GTK_OBJECT (connection), "activate",
                      GTK_SIGNAL_FUNC (on_connection_activate),
                      NULL);
  gtk_signal_connect (GTK_OBJECT (properties), "activate",
                      GTK_SIGNAL_FUNC (on_properties1_activate),
                      NULL);
//here we setup the signals for call and hangup
  gtk_signal_connect (GTK_OBJECT (m_call),"clicked",
			GTK_SIGNAL_FUNC(gtk_call),
			NULL);  
  gtk_signal_connect (GTK_OBJECT (m_hangup),"clicked",
			GTK_SIGNAL_FUNC(gtk_hangup),
			NULL);  

// here we setup the signals for those 12 buttons
  gtk_signal_connect (GTK_OBJECT (b0),"clicked",
			GTK_SIGNAL_FUNC(gtk_buttons),
			NULL);  
  gtk_signal_connect (GTK_OBJECT (b1),"clicked",
			GTK_SIGNAL_FUNC(gtk_buttons),
			NULL);  
  gtk_signal_connect (GTK_OBJECT (b2),"clicked",
			GTK_SIGNAL_FUNC(gtk_buttons),
			NULL);  
  gtk_signal_connect (GTK_OBJECT (b3),"clicked",
			GTK_SIGNAL_FUNC(gtk_buttons),
			NULL);  
  gtk_signal_connect (GTK_OBJECT (b4),"clicked",
			GTK_SIGNAL_FUNC(gtk_buttons),
			NULL);  
  gtk_signal_connect (GTK_OBJECT (b5),"clicked",
			GTK_SIGNAL_FUNC(gtk_buttons),
			NULL);  
  gtk_signal_connect (GTK_OBJECT (b6),"clicked",
			GTK_SIGNAL_FUNC(gtk_buttons),
			NULL);  
  gtk_signal_connect (GTK_OBJECT (b7),"clicked",
			GTK_SIGNAL_FUNC(gtk_buttons),
			NULL);  
  gtk_signal_connect (GTK_OBJECT (b8),"clicked",
			GTK_SIGNAL_FUNC(gtk_buttons),
			NULL);  
  gtk_signal_connect (GTK_OBJECT (b9),"clicked",
			GTK_SIGNAL_FUNC(gtk_buttons),
			NULL);  
  gtk_signal_connect (GTK_OBJECT (bstar),"clicked",
			GTK_SIGNAL_FUNC(gtk_buttons),
			NULL);  
  gtk_signal_connect (GTK_OBJECT (bgrid),"clicked",
			GTK_SIGNAL_FUNC(gtk_buttons),
			NULL);  
    

  
  return yate;
}
    
void GtkClient::gtk_buttons (GtkWidget *button, gpointer data)
{
    gchar *button_label;
    gtk_label_get(GTK_LABEL(GTK_BIN(button)->child), &button_label);
    //Debug(DebugInfo,"string is %s",(const char *)button_label);
    //String buttonl(button_label);
    if (s_client->status != STATUS_IDLE) {
	Message* m = new Message("chan.masquerade");
	m->addParam ("id", "oss/");
	m->addParam ("text", (const char *)button_label);
	m->addParam ("message", "chan.dtmf");
	Engine::enqueue(m);
    }
    gtk_entry_append_text(GTK_ENTRY(GTK_COMBO(s_client->m_address)->entry), button_label);
}
void GtkClient::gtk_call (GtkWidget *button, gpointer data)
{
    Debug(DebugInfo,"aici sunt %d",s_client->status);
    switch(s_client->status)
    {
	case STATUS_IDLE:
	    {
	    Message m("call.execute");
	    gchar *address = gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(s_client->m_address)->entry));
	    if (::strchr(address,'/'))
		m.addParam("direct",address);
	    else
		m.addParam("target",address);
	    m.addParam("callto","oss///dev/dsp");
	    m.addParam("caller","oss///dev/dsp");
	    m.addParam("id","oss///dev/dsp");
	    if (Engine::dispatch(m))
		s_client->set_state(STATUS_RINGOUT);
	    else 
		gtk_statusbar_push((GtkStatusbar *)s_client->m_statusbar,1,"call failed");
	    }
	    break;
	case STATUS_RINGIN:
	    s_client->set_state(STATUS_INCALL);
	    break;
    }
}


void GtkClient::gtk_hangup (GtkWidget *button, gpointer data)
{
    switch(s_client->status)
    {
	case STATUS_RINGIN:
	    break;
	case STATUS_RINGOUT:
	case STATUS_INCALL:
	    Message m("call.drop");
	    m.addParam("id","oss/");
	    Engine::dispatch(m);
	    break;
    }
    s_client->set_state(STATUS_IDLE);
}

GtkClient::~GtkClient(void)
{
    Debug(DebugInfo,"GtkClient::~GtkClient");
    if (m_yate)
    {
	gtk_widget_destroy(m_yate);
	s_client = 0;
    }
}


void GtkClient::set_state(int new_state)
{
    if (new_state == status)
	return;
    status = new_state;
    switch (status)
    {
    case STATUS_IDLE:
	gtk_label_set(GTK_LABEL(GTK_BIN(s_client->m_call)->child), "Call");
	gtk_label_set(GTK_LABEL(GTK_BIN(s_client->m_hangup)->child), "Reject");
	break;
    case STATUS_RINGIN:
	gtk_label_set(GTK_LABEL(GTK_BIN(s_client->m_call)->child), "Answer");
	gtk_label_set(GTK_LABEL(GTK_BIN(s_client->m_hangup)->child), "Reject");
	break;
    case STATUS_RINGOUT:
	gtk_label_set(GTK_LABEL(GTK_BIN(s_client->m_call)->child), "Call");
	gtk_label_set(GTK_LABEL(GTK_BIN(s_client->m_hangup)->child), "Hangup");
	break;
    case STATUS_INCALL:
	gtk_label_set(GTK_LABEL(GTK_BIN(s_client->m_call)->child), "Call");
	gtk_label_set(GTK_LABEL(GTK_BIN(s_client->m_hangup)->child), "Hangup");
	break;
    }
    
}

void GtkClient::run()
{
    m_yate = create_yate ();
    gtk_widget_show (m_yate);
    gdk_threads_enter();
    gtk_main ();
    gdk_threads_leave();
    Debug(DebugInfo,"Gtk main loop exited");
}




class GtkClientHandler : public MessageHandler
{
public:
    GtkClientHandler(int prio)
	: MessageHandler("call.route",prio) { }
    virtual bool received(Message &msg);
};
	
bool GtkClientHandler::received(Message &msg)
{
    String caller(msg.getValue("caller"));
    Debug(DebugInfo,"caller %s",caller.c_str());
    if (caller == "oss///dev/dsp")
	return false;
    String called(msg.getValue("called"));
    if (called.null())
	return false;
    String mesg;
    mesg << "You have a call from " << caller << " for " << called ;
    gdk_threads_enter();
    gtk_statusbar_push((GtkStatusbar *)s_client->m_statusbar,1,(gchar *)((const char *)mesg));
    s_client->set_state(STATUS_RINGIN);
    gdk_threads_leave();

    u_int64_t t = Time::now() + 10000000;
    while (Time::now() < t) 
    { 
	if(s_client->status == STATUS_INCALL)
	{
	    msg.retValue() = String("oss///dev/dsp");	    
//	    Debug(DebugInfo,"yesssss");
	    
	    
	    return true;
	} else if (s_client->status == STATUS_IDLE)
	{
//	    Debug(DebugInfo,"oooooooo nooooooooooooooooo");
	    return false;
	}
	usleep(50000);
    }

    
    return false;
};

class GtkClientPlugin : public Plugin
{
public:
    GtkClientPlugin();
    ~GtkClientPlugin();
    virtual void initialize();
    virtual bool isBusy() const
	{ return true; }
private:
    MessageHandler *m_route;
    bool m_init;
};

GtkClientPlugin::GtkClientPlugin()
    : m_route(0), m_init(false)
{
    Output("Loaded module GtkClient");
}

GtkClientPlugin::~GtkClientPlugin()
{
    Output("Unloading module GtkClient");
    g_atexit_unwind();
}

void GtkClientPlugin::initialize()
{
    Output("Initializing module GtkClient");
    if (m_init)
	return;
    // gtk can only be initialized once so take care of it
    m_init = true;
    s_cfg = Engine::configFile("gtkclient");
    s_cfg.load();
    int priority = s_cfg.getIntValue("priorities","route",20);
    if (priority) {
	g_thread_init(NULL);
	if (gtk_init_check(0,0))
	{
	    m_route = new GtkClientHandler(priority);
	    Engine::install(m_route);
	    GtkClient *gtk = new GtkClient;
	    gtk->startup();
	}
    }
//    int argc = 0;
//    char *argv[] = {"yate",0};
//    gtk_init (&argc,(char ***)&argv);
}

INIT_PLUGIN(GtkClientPlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
