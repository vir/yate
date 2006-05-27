/**
 * main-gtk2.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * A Gtk-2 based universal telephony client
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <yatephone.h>
#include <gtk/gtk.h>
#include "../contrib/gtk2/gtk2client.h"

using namespace TelEngine;

static GTKDriver gtkdriver;

extern "C" int main(int argc, const char** argv, const char** envp)
{
    g_thread_init(NULL);
    gdk_threads_init();
    bool fail = !gtk_init_check(&argc,(char ***)&argv);
    if (fail)
	g_warning("Cannot open display: '%s'",gdk_get_display());
    TelEngine::Engine::extraPath() = "gtk2";
    return TelEngine::Engine::main(argc,argv,envp,TelEngine::Engine::Client,fail);
}
/* vi: set ts=8 sw=4 sts=4 noet: */
