/**
 * widgetlist.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Custom widget list objects
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 *
 * This software is distributed under multiple licenses;
 * see the COPYING file in the main directory for licensing
 * information for this specific distribution.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef __WIDGETLIST_H
#define __WIDGETLIST_H

#include "qt4client.h"

using namespace TelEngine;
namespace { // anonymous

class WidgetListTabWidget;               // A tab widget client of a widget list
class WidgetListStackedWidget;           // A stacked widget client of a widget list
class WidgetList;                        // A widget list

class WidgetListTabWidget : public QTabWidget
{
public:
    /**
     * Constructor
     * @param parent WidgetList parent
     */
    WidgetListTabWidget(WidgetList* parent, const NamedList& params);

    /**
     * Set tab text color
     * @param index Tab index
     * @param color Tab text color
     */
    inline void setTabTextColor(int index, QColor color) {
	    QTabBar* bar = tabBar();
	    if (bar)
		bar->setTabTextColor(index,color);
	}

    /**
     * Retrieve the tab text color
     * @param index Tab index
     * @return Text color of the given index
     */
    inline QColor tabTextColor(int index) {
	    QTabBar* bar = tabBar();
	    return bar ? bar->tabTextColor(index) : QColor();
	}

protected:
    /**
     * Build and set a close button for a given tab or a global close if index is negative
     * Connect the button to parent's slot.
     * This method is called from tabInserted() with non negative index
     */
    void setCloseButton(int index = -1);

    /**
     * Tab inserted. Set tab close button if needed
     */
    virtual void tabInserted(int index);

    /**
     * Tab removed. Notify the parent
     */
    virtual void tabRemoved(int index);
};

class WidgetListStackedWidget : public QStackedWidget
{
public:
    /**
     * Constructor
     * @param parent WidgetList parent
     */
    WidgetListStackedWidget(WidgetList* parent, const NamedList& params);
};

/**
 * This class holds a basic widget list container
 * @short A widget list
 */
class WidgetList : public QtCustomWidget
{
    friend class WidgetListTabWidget;
    YCLASS(WidgetList,QtCustomWidget)
    Q_CLASSINFO("WidgetList","Yate")
    Q_OBJECT
    Q_PROPERTY(bool _yate_hidewndwhenempty READ hideWndWhenEmpty WRITE setHideWndWhenEmpty(bool))
    Q_PROPERTY(QString _yate_hidewidgetwhenempty READ hideWidgetWhenEmpty WRITE setHideWidgetWhenEmpty(QString))
    Q_PROPERTY(QString _yate_itemui READ itemUi WRITE setItemUi(QString))
    Q_PROPERTY(QString _yate_itemstyle READ itemStyle WRITE setItemStyle(QString))
    Q_PROPERTY(QString _yate_itemtextparam READ itemTextParam WRITE setItemTextParam(QString))
    Q_PROPERTY(QString _yate_itemimageparam READ itemImageParam WRITE setItemImageParam(QString))
    Q_PROPERTY(QString _yate_flashitem READ flashItem WRITE setFlashItem(QString))
public:
    /**
     * Delete item button type
     */
    enum DelItem {
	DelItemNone = 0,                 // No delete item button
	DelItemGlobal,                   // Global (delete selected) button
	DelItemSingle,                   // Delete button on each item
	DelItemNative,                   // Delete button on each item: use native if available
    };

    /**
     * Constructor
     * @param name Object name
     * @param params Object parameters
     * @param parent Optional parent
     */
    WidgetList(const char* name, const NamedList& params, QWidget* parent);

    /**
     * Find an item widget by index
     * @param index Item index
     * @return QWidget pointer or 0
     */
    QWidget* findItemByIndex(int index);

    /**
     * Set widget parameters
     * @param params Parameter list
     * @return True on success
     */
    virtual bool setParams(const NamedList& params);

    /**
     * Get widget's items
     * @param items List to fill with widget's items
     * @return True
     */
    virtual bool getOptions(NamedList& items);

    /**
     * Retrieve item parameters
     * @param item Item id
     * @param data List to be filled with parameters
     * @return True on success
     */
    virtual bool getTableRow(const String& item, NamedList* data = 0);

    /**
     * Add a new item
     * @param item Item id
     * @param data Item parameters
     * @param asStart True to insert at start, false to append
     * @return True on success
     */
    virtual bool addTableRow(const String& item, const NamedList* data = 0,
	bool atStart = false);

    /**
     * Add/set/delete one or more item(s)
     * @param data The list of items to add/set/delete
     * @param atStart True to add new items at start, false to add them to the end
     * @return True if the operation was successfull
     */
    virtual bool updateTableRows(const NamedList* data, bool atStart = false);

    /**
     * Delete an item
     * @param item Item id
     * @return True on success
     */
    virtual bool delTableRow(const String& item);

    /**
     * Set existing item parameters
     * @param item Item id
     * @param data Item parameters
     * @return True on success
     */
    virtual bool setTableRow(const String& item, const NamedList* data);

    /**
     * Delete all items
     * @return True on success
     */
    virtual bool clearTable();

    /**
     * Select (set active) an item
     * @param item Item id
     * @return True on success
     */
    virtual bool setSelect(const String& item);

    /**
     * Retrieve the selected (active) item
     * @param item Item id
     * @return True on success
     */
    virtual bool getSelect(String& item);

    /**
     * Retrieve a QObject list containing tree item widgets
     * @return The list of container item widgets
     */
    virtual QList<QObject*> getContainerItems();

    /**
     * Select an item by its index
     * @param index Item index to select
     * @return True on success
     */
    virtual bool setSelectIndex(int index);

    /**
     * Retrieve the 0 based index of the current item
     * @return The index of the current item (-1 on error or container empty)
     */
    virtual int currentItemIndex();

    /**
     * Retrieve the number of items in container
     * @return The number of items in container (-1 on error)
     */
    virtual int itemCount();

    /**
     * Retrieve _yate_hidewndwhenempty property value
     * @return _yate_hidewndwhenempty property value
     */
    bool hideWndWhenEmpty()
	{ return m_hideWndWhenEmpty; }

    /**
     * Set _yate_hidewndwhenempty property value. Apply it if changed
     * @param value The new value of _yate_hidewndwhenempty property
     */
    void setHideWndWhenEmpty(bool value);

    /**
     * Retrieve _yate_hidewidgetwhenempty property value
     * @return _yate_hidewidgetwhenempty property value
     */
    QString hideWidgetWhenEmpty()
	{ return QtClient::setUtf8(m_hideWidgetWhenEmpty); }

    /**
     * Set _yate_hidewidgetwhenempty property value. Apply it if changed
     * @param value The new value of _yate_hidewidgetwhenempty property
     */
    void setHideWidgetWhenEmpty(QString value);

    /**
     * Read _yate_itemui property accessor: does nothing
     * This method is here to stop MOC compiler complaining about missing READ accessor function
     */
    QString itemUi()
	{ return QString(); }

    /**
     * Set an item props ui
     * @param value Item props ui. Format [type:]ui_name
     */
    void setItemUi(QString value) {
	    String tmp;
	    QtUIWidgetItemProps* p = getItemProps(value,tmp);
	    p->m_ui = tmp;
	}

    /**
     * Read _yate_itemstyle property accessor: does nothing
     * This method is here to stop MOC compiler complaining about missing READ accessor function
     */
    QString itemStyle()
	{ return QString(); }

    /**
     * Set an item props style sheet
     * @param value Item props style sheet. Format [type:]stylesheet
     */
    void setItemStyle(QString value) {
	    String tmp;
	    QtUIWidgetItemProps* p = getItemProps(value,tmp);
	    p->m_styleSheet = tmp;
	}

    /**
     * Retrieve _yate_itemtextparam property value
     * @return The value of _yate_itemtextparam property
     */
    QString itemTextParam()
	{ return QtClient::setUtf8(m_itemTextParam); }

    /**
     * Set _yate_itemtextparam property value
     * @param value The new value of _yate_itemtextparam property
     */
    void setItemTextParam(QString value)
	{ QtClient::getUtf8(m_itemTextParam,value); }

    /**
     * Retrieve _yate_itemimageparam property value
     * @return The value of _yate_itemimageparam property
     */
    QString itemImageParam()
	{ return QtClient::setUtf8(m_itemImgParam); }

    /**
     * Set _yate_itemimageparam property value
     * @param value The new value of _yate_itemimageparam property
     */
    void setItemImageParam(QString value)
	{ QtClient::getUtf8(m_itemImgParam,value); }

    /**
     * Read _yate_flashitem property accessor: does nothing
     * This method is here to stop MOC compiler complaining about missing READ accessor function
     */
    QString flashItem()
	{ return QString(); }

    /**
     * Start/stop item flash
     * @param value Item value. Format bool_value:item_id
     */
    void setFlashItem(QString value);

public slots:
    /**
     * Handle item children actions
     */
    void itemChildAction()
	{ onAction(sender()); }

    /**
     * Handle item children toggles
     */
    void itemChildToggle(bool on)
	{ onToggle(sender(),on); }

    /**
     * Handle selection changes
     */
    void currentChanged(int index);

    /**
     * Item removed slot. Notify the client when empty
     */
    void itemRemoved(int index);

    /**
     * Handle item children select
     */
    void itemChildSelect()
	{ onSelect(sender()); }

    /**
     * Handle item close action
     */
    void closeItem(int index = -1);

protected:
    /**
     * Handle children events
     */
    virtual bool eventFilter(QObject* watched, QEvent* event);

    /**
     * Hide the parent window or widget if the container is empty
     */
    void hideEmpty();

    /**
     * Insert/add a widget item
     * @param w Widget to append or insert (it will be deleted and reset on failure)
     * @param atStart True to insert, false to add
     * @return True on success
     */
    bool addItem(QWidget*& w, bool atStart);

    /**
     * Retrieve the selected item widget
     * @return QWidget pointer or 0
     */
    QWidget* selectedItem();

    /**
     * Set delete item type
     * @param type The new delete item type
     */
    void setDelItemType(int type);

    /**
     * Retrieve delete item object properties
     * @param params Parameter list
     * @param first True if called from constructor: update delete item type also
     */
    void updateDelItemProps(const NamedList& params, bool first = false);

    /**
     * Apply delete item object properties
     * @param obj The object
     */
    void applyDelItemProps(QObject* obj);

    bool m_hideWndWhenEmpty;             // Hide the parent window when the container is empty
    String m_hideWidgetWhenEmpty;        // Widget to hide when the container is empty
    WidgetListTabWidget* m_tab;          // Tab widget if used
    WidgetListStackedWidget* m_pages;    // Stacked widget if used
    int m_delItemType;                   // Delete item type
    NamedList m_delItemProps;            // Delete item widget properties
    String m_delItemActionPrefix;        // Delete item action prefix
    String m_itemTextParam;              // Hook this parameter to set item text
    String m_itemImgParam;               // Hook this parameter to set item image
};

}; // anonymous namespace

#endif // __WIDGETLIST_H

/* vi: set ts=8 sw=4 sts=4 noet: */
