/**
 * gtk2mozilla.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Mozilla embedded widget for the Gtk2 based universal telephony client
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


#include "gtk2client.h"
#include <gtkmozembed.h>

using namespace TelEngine;
namespace { // anonymous

// Embedded Mozilla widget
class MozWidget : public Widget
{
public:
    MozWidget(const String& text);
    virtual ~MozWidget();
    virtual bool setText(const String& text);
    virtual bool getText(String& text);
    void setTextAsync();
protected:
    String m_url;
};

WIDGET_FACTORY("gtk2","mozilla",MozWidget)

static Mutex s_mutex;

static gboolean mozIntervalCb(gpointer dat)
{
    if (dat) {
	// interval callback called from glib directly
	gdk_threads_enter();
	static_cast<MozWidget*>(dat)->setTextAsync();
	gdk_threads_leave();
    }
    return false;
}

MozWidget::MozWidget(const String& text)
{
    DDebug(ClientDriver::self(),DebugAll,"MozWidget::MozWidget()");
    widget(gtk_moz_embed_new());
    setText(text);
}

MozWidget::~MozWidget()
{
    DDebug(ClientDriver::self(),DebugAll,"MozWidget::~MozWidget()");
}

bool MozWidget::setText(const String& text)
{
    if (widget() && text) {
	s_mutex.lock();
	m_url = text;
	gtk_timeout_add(1,mozIntervalCb,this);
	s_mutex.unlock();
	return true;
    }
    return false;
}

bool MozWidget::getText(String& text)
{
    if (widget()) {
	char* url = gtk_moz_embed_get_location(GTK_MOZ_EMBED(widget()));
	text = url;
	delete url;
	return true;
    }
    return false;
}

void MozWidget::setTextAsync()
{
    s_mutex.lock();
    if (widget() && m_url) {
	Debug(ClientDriver::self(),DebugAll,"MozWidget async url='%s'",m_url.c_str());
	gtk_moz_embed_load_url(GTK_MOZ_EMBED(widget()),m_url.c_str());
	m_url.clear();
    }
    s_mutex.unlock();
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
