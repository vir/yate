/**
 * updater.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Auto updater logic and downloader for Qt-4 clients.
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

#ifndef __UPDATER_H
#define __UPDATER_H

#include <yatecbase.h>

#undef open
#undef read
#undef close
#undef write
#undef mkdir

#define QT_NO_DEBUG
#define QT_DLL
#define QT_GUI_LIB
#define QT_CORE_LIB
#define QT_THREAD_SUPPORT

#include <QObject>
#include <QHttp>

using namespace TelEngine;
namespace { // anonymous

class UpdateLogic;

/**
 * Proxy object so HTTP notification slots are created in the GUI thread
 */
class QtUpdateHttp : public QObject
{
    Q_CLASSINFO("QtUpdateHttp","Yate")
    Q_OBJECT
public:
    /**
     * Constructor
     * @param logic Qt update logic owning this object
     */
    inline QtUpdateHttp(UpdateLogic* logic)
	:  m_logic(logic)
	{ }
    /**
     * Create a QHttp object and attach its signals to this object
     * @return New QHttp object attached to this object's slots
     */
    QHttp* http();
private slots:
    void dataProgress(int done, int total);
    void requestDone(bool error);
private:
    UpdateLogic* m_logic;
};

}; // anonymous namespace

#endif /* __UPDATER_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
