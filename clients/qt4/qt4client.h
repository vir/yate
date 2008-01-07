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

#include <QtGui>
#include <QSound>

namespace TelEngine {

class QtWindow;

class YQT4_API QtClient : public Client
{
    friend class QtWindow;
public:
    QtClient();
    virtual ~QtClient();
    virtual void run();
    virtual void main();
    virtual void lock();
    virtual void unlock();
    virtual void allHidden();
    virtual bool createWindow(const String& name);
    virtual bool action(Window* wnd, const String& name);
protected:
    virtual void loadWindows();
private:
    QApplication* m_app;
};

class YQT4_API QtDriver : public ClientDriver
{
public:
    QtDriver();
    virtual ~QtDriver();
    virtual void initialize();
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
    virtual bool setText(const String& name, const String& text);
    virtual bool setCheck(const String& name, bool checked);
    virtual bool setSelect(const String& name, const String& item);
    virtual bool setUrgent(const String& name, bool urgent);
    virtual bool hasOption(const String& name, const String& item);
    virtual bool addOption(const String& name, const String& item, bool atStart = false, const String& text = String::empty());
    virtual bool delOption(const String& name, const String& item);

    virtual bool addTableRow(const String& name, const String& item, const NamedList* data = 0, bool atStart = false);
    virtual bool delTableRow(const String& name, const String& item);
    virtual bool setTableRow(const String& name, const String& item, const NamedList* data);
    virtual bool getTableRow(const String& name, const String& item, NamedList* data = 0);
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
    virtual bool related(const Window* wnd) const;
    virtual void menu(int x, int y) ;
    virtual void closeEvent(QCloseEvent* event);
    bool select(const String& name, const String& item, const String& text = String::empty());
    void keyPressEvent(QKeyEvent* e);

public slots:
    void setVisible(bool visible);
    void enableDebugOptions(bool enable);
    void chooseFile();
    void enableFileChoosing(bool enable);
    void select(int value);
    void selectionToCallto();

private slots:
    void disableCombo();
    void focus();
    void idleActions();
    void action();
    void openUrl(const QString& link);

protected:
    String m_description;
    bool m_keysVisible;
    QStringList channelsList;
    int m_x, m_y, m_width, m_height;
    bool m_visible;
    QSound* m_ringtone;
};

}; // namespace TelEngine

/* vi: set ts=8 sw=4 sts=4 noet: */
