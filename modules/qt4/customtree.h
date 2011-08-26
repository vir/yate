/**
 * customtree.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Custom QtTree based objects
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

#ifndef __CUSTOMTREE_H
#define __CUSTOMTREE_H

#include "qt4client.h"

using namespace TelEngine;
namespace { // anonymous

class QtTreeItem;                        // A tree widget item
class QtCustomTree;                      // A custom tree widget
class ContactList;                       // A contact list tree
class ContactItem;                       // A contact list contact
class ContactItemList;                   // Groups and contact items belonging to them

typedef QList<QTreeWidgetItem*> QtTreeItemList;
typedef QPair<QTreeWidgetItem*,QString> QtTreeItemKey;
typedef QPair<String,int> QtTokenDict; 

/**
 * This class holds data about a tree widget container item
 * @short Tree widget container item properties
 */
class QtTreeItemProps : public QtUIWidgetItemProps
{
    YCLASS(QtTreeItemProps,QtUIWidgetItemProps)
public:
    /**
     * Constructor
     * @param type Item type
     */
    explicit inline QtTreeItemProps(const String& type)
	: QtUIWidgetItemProps(type),
	m_height(-1)
	{}

    int m_height;                        // Item height
    String m_stateWidget;                // Item widget or column showing the state
    String m_stateExpandedImg;           // Image to show when expanded
    String m_stateCollapsedImg;          // Image to show when collapsed
    String m_toolTip;                    // Tooltip template
    String m_statsWidget;                // Item widget showing statistics while collapsed
    String m_statsTemplate;              // Statistics template (may include ${count} for children count)
    QBrush m_bg;                         // Item background
};


/**
 * This class holds a custom tree widget item
 * @short A tree widget item
 */
class QtTreeItem : public QTreeWidgetItem, public NamedList
{
    YCLASS(QtTreeItem,NamedList)
public:
    /**
     * Constructor
     * @param id Item id
     * @param type Item type
     * @param text Optional text for item column 0
     * @param storeExp Set it to true to (re)store item expanded state
     */
    QtTreeItem(const char* id, int type = Type,	const char* text = 0, bool storeExp = false);

    /**
     * Destructor
     */
    ~QtTreeItem();

    /**
     * Set a column's text from a list of parameter cname
     * @param col Column to set
     * @param cname Column name
     * @param list The list containing the parameter
     */
    inline void setText(int col, const String& cname, const NamedList& list) {
	    String* s = cname ? list.getParam(cname) : 0;
	    if (s)
		QTreeWidgetItem::setText(col,QtClient::setUtf8(*s));
	}

    /**
     * Set a column's icon from a list of parameter cname_image
     * @param col Column to set
     * @param cname Column name
     * @param list The list containing the parameter
     * @param role Set image file path in this role if greater then Qt::UserRole
     */
    void setImage(int col, const String& cname, const NamedList& list,
	    int role = Qt::UserRole);

    /**
     * Set a column's check state from boolean value
     * @param col Column to set
     * @param check Check state
     */
    inline void setCheckState(int col, bool check)
	{ QTreeWidgetItem::setCheckState(col,check ? Qt::Checked : Qt::Unchecked); }

    /**
     * Set a column's check state from a list of parameter check:cname
     * @param col Column to set
     * @param cname Column name
     * @param list The list containing the parameter
     */
    inline void setCheckState(int col, const String& cname, const NamedList& list) {
	    String* s = cname ? list.getParam("check:" + cname) : 0;
	    if (s)
		setCheckState(col,s->toBoolean());
	}

    /**
     * Retrieve the item id
     * @return Item id
     */
    inline const String& id() const
	{ return toString(); }

    /**
     * Save/restore item expanded status
     */
    bool m_storeExp;
};

/**
 * This class holds a custom tree widget
 * @short QT based tree widget
 */
class QtCustomTree : public QtTree
{
    YCLASS(QtCustomTree,QtTree)
    Q_CLASSINFO("QtCustomTree","Yate")
    Q_OBJECT
    Q_PROPERTY(QStringList _yate_save_props READ saveProps WRITE setSaveProps(QStringList))
    Q_PROPERTY(bool autoExpand READ autoExpand WRITE setAutoExpand(bool))
    Q_PROPERTY(int rowHeight READ rowHeight WRITE setRowHeight(int))
    Q_PROPERTY(bool _yate_horizontalheader READ getHHeader WRITE setHHeader(bool))
    Q_PROPERTY(QString _yate_itemui READ itemUi WRITE setItemUi(QString))
    Q_PROPERTY(QString _yate_itemstyle READ itemStyle WRITE setItemStyle(QString))
    Q_PROPERTY(QString _yate_itemselectedstyle READ itemSelectedStyle WRITE setItemSelectedStyle(QString))
    Q_PROPERTY(QString _yate_itemstatewidget READ itemStateWidget WRITE setItemStateWidget(QString))
    Q_PROPERTY(QString _yate_itemexpandedimage READ itemExpandedImage WRITE setExpandedImage(QString))
    Q_PROPERTY(QString _yate_itemcollapsedimage READ itemCollapsedImage WRITE setItemCollapsedImage(QString))
    Q_PROPERTY(QString _yate_itemtooltip READ itemTooltip WRITE setItemTooltip(QString))
    Q_PROPERTY(QString _yate_itemstatswidget READ itemStatsWidget WRITE setItemStatsWidget(QString))
    Q_PROPERTY(QString _yate_itemstatstemplate READ itemStatsTemplate WRITE setItemStatsTemplate(QString))
    Q_PROPERTY(QString _yate_itemheight READ itemHeight WRITE setItemHeight(QString))
    Q_PROPERTY(QString _yate_itembackground READ itemBg WRITE setItemBg(QString))
    Q_PROPERTY(QString _yate_col_widths READ colWidths WRITE setColWidths(QString))
    Q_PROPERTY(QString _yate_sorting READ sorting WRITE setSorting(QString))
    Q_PROPERTY(QString _yate_itemsexpstatus READ itemsExpStatus WRITE setItemsExpStatus(QString))
public:
    /**
     * List item type enumeration
     */
    enum ItemType {
	TypeCount = QTreeWidgetItem::UserType
    };

    /**
     * List item data role
     */
    enum ItemDataRole {
	RoleId = Qt::UserRole + 1,       // Item id (used in headers)
	RoleCheckable,                   // Column checkable (used in headers)
	RoleHtmlDelegate,                // Headers: true if a column has a custom html item delegate
	                                 // Rows: QStringList with data
	RoleImage,                       // Role containing item image file name
	RoleBackground,                  // Role containing item background color
	RoleCount
    };

    /**
     * Constructor
     * @param name Object name
     * @param params Parameters
     * @param parent Optional parent
     * @param applyParams Apply parameters (call setParams())
     */
    QtCustomTree(const char* name, const NamedList& params, QWidget* parent = 0,
	bool applyParams = true);

    /**
     * Destructor
     */
    virtual ~QtCustomTree();

    /**
     * Retrieve item type definition from [type:]value. Create it if not found
     * @param in Input string
     * @param value Item property value
     * @return QtUIWidgetItemProps pointer or 0
     */
    virtual QtUIWidgetItemProps* getItemProps(QString& in, String& value);

    /**
     * Set object parameters
     * @param params Parameters list
     * @return True on success
     */
    virtual bool setParams(const NamedList& params);

    /**
     * Retrieve an item
     * @param item Item id
     * @param data Item parameters to fill
     * @return True on success
     */
    virtual bool getTableRow(const String& item, NamedList* data = 0);

    /**
     * Update an existing item
     * @param item Item id
     * @param data Item parameters
     * @return True on success
     */
    virtual bool setTableRow(const String& item, const NamedList* data);

    /**
     * Add a new entry (account or contact) to the tree
     * @param item Item id
     * @param data Item parameters
     * @param asStart True if the entry is to be inserted at the start of
     *   the table, false if it is to be appended
     * @return True if the entry has been added, false otherwise
     */
    virtual bool addTableRow(const String& item, const NamedList* data = 0,
	bool atStart = false);

    /**
     * Remove an item from tree
     * @param item Item id
     * @return True on success
     */
    virtual bool delTableRow(const String& item);

    /**
     * Add, set or remove one or more items.
     * Screen update is locked while changing the tree.
     * Each data list element is a NamedPointer carrying a NamedList with item parameters.
     * The name of an element is the item to update.
     * Set element's value to boolean value 'true' to add a new item if not found
     *  or 'false' to set an existing one. Set it to empty string to delete the item
     * @param data The list of items to add/set/delete
     * @param atStart True to add new items at start, false to add them to the end
     * @return True on success
     */
    virtual bool updateTableRows(const NamedList* data, bool atStart = false);

    /**
     * Set the widget's selection
     * @param item String containing the new selection
     * @return True if the operation was successfull
     */
    virtual bool setSelect(const String& item);

    /**
     * Retrieve the current selection
     * @param item String to fill with selected item id
     * @return True on success
     */
    virtual bool getSelect(String& item);

    /**
     * Remove all items from tree
     * @return True
     */
    virtual bool clearTable();

    /**
     * Retrieve all items' id
     * @param items List to fill with widget's items
     * @return True
     */
    virtual bool getOptions(NamedList& items);

    /**
     * Retrieve a QObject list containing tree item widgets
     * @return The list of container item widgets
     */
    virtual QList<QObject*> getContainerItems();

    /**
     * Find a tree item
     * @param id Item id
     * @param start Optional start item. Set it to 0 to start with root item
     * @param includeStart Include start item in id check.
     *  Set it to false to check start children only
     * @param recursive True to make a recursive search,
     *  false to check only start first level children
     * @return QTreeItem pointer or 0
     */
    virtual QtTreeItem* find(const String& id, QtTreeItem* start = 0,
	bool includeStart = true, bool recursive = true);

    /**
     * Find all tree items
     * @param recursive True to make a recursive search, false to add only direct children
     * @param parent Optional parent item. Set it to 0 to use the root item
     * @return The list of items
     */
     QList<QtTreeItem*> findItems(bool recursive = true, QtTreeItem* parent = 0);

    /**
     * Find all tree items having a given id
     * @param id Item id
     * @param start Optional start item. Set it to 0 to start with root item
     * @param includeStart Include start item in id check.
     *  Set it to false to check start children only
     * @param recursive True to make a recursive search,
     *  false to check only start first level children
     * @return The list of items
     */
     QList<QtTreeItem*> findItems(const String& id, QtTreeItem* start = 0,
	bool includeStart = true, bool recursive = true);

    /**
     * Find all tree items having a given type
     * @param id Item type
     * @param start Optional start item. Set it to 0 to start with root item
     * @param includeStart Include start item in id check.
     *  Set it to false to check start children only
     * @param recursive True to make a recursive search,
     *  false to check only start first level children
     * @return The list of items
     */
     QList<QtTreeItem*> findItems(int type, QtTreeItem* start = 0,
	bool includeStart = true, bool recursive = true);

    /**
     * Find all tree items
     * @param list List to fill
     * @param start Optional start item. Set it to 0 to start with root item
     * @param includeStart Include start item in id check.
     *  Set it to false to check start children only
     * @param recursive True to make a recursive search,
     *  false to check only start first level children
     */
     void findItems(NamedList& list, QtTreeItem* start = 0,
	bool includeStart = true, bool recursive = true);

    /**
     * Add a child to a given item
     * @param child Child to add
     * @param pos Position to insert. Negative to add after the last child
     * @param parent The parent item. Set it to 0 to add to the root
     * @return QtTreeItem pointer on failure, 0 on success
     */
    QtTreeItem* addChild(QtTreeItem* child, int pos = -1, QtTreeItem* parent = 0);

    /**
     * Add a child to a given item
     * @param child Child to add
     * @param atStart True to insert at start, false to add aftr the last child
     * @param parent The parent item. Set it to 0 to add to the root
     * @return QtTreeItem pointer on failure, 0 on success
     */
    inline QtTreeItem* addChild(QtTreeItem* child, bool atStart, QtTreeItem* parent = 0)
	{ return addChild(child,atStart ? 0 : -1,parent); }

    /**
     * Add a list of children to a given item
     * @param list Children to add
     * @param pos Position to insert. Negative to add after the last child
     * @param parent The parent item. Set it to 0 to add to the root
     */
    void addChildren(QList<QTreeWidgetItem*> list, int pos = -1, QtTreeItem* parent = 0);

    /**
     * Setup an item. Load its widget if not found
     * @param item Item to setup
     */
    void setupItem(QtTreeItem* item);

    /**
     * Retrieve and item's row height by type
     * @param item Item to set
     * @return Item row height
     */
    inline int getItemRowHeight(int type) {
	    QtTreeItemProps* p = treeItemProps(type);
	    return (p && p->m_height > 0) ? p->m_height : m_rowHeight;
	}

    /**
     * Set and item's row height hint
     * @param item Item to set
     */
    void setItemRowHeight(QTreeWidgetItem* item);

    /**
     * Retrieve item properties associated with a given type
     * @param type Item type
     * @return QtTreeItemProps poinetr or 0 if not found
     */
    inline QtTreeItemProps* treeItemProps(int type) {
	    QtUIWidgetItemProps* pt = QtUIWidget::getItemProps(itemPropsName(type));
	    return YOBJECT(QtTreeItemProps,pt);
	}

    /**
     * Retrieve string data associated with a column
     * @param buf Destination string
     * @param item The tree item whose data to retreive
     * @param column Column to retrieve
     * @param role Data role to retrieve, defaults to id
     */
    inline void getItemData(String& buf, QTreeWidgetItem& item, int column,
	int role = RoleId)
	{ QtClient::getUtf8(buf,item.data(column,role).toString()); }

    /**
     * Retrieve boolean data associated with a column
     * @param column Column to retrieve
     * @param role Data role to retrieve
     * @param item Optional item, use tree header item if 0
     * @return The boolean value for the given column and role
     */
    inline bool getBoolItemData(int column, int role, QTreeWidgetItem* item = 0) {
	    if (!item)
		item = headerItem();
	    return item && item->data(column,role).toBool();
	}

    /**
     * Retrieve a column by it's id
     * @param id The column id to find
     * @return Column number, -1 if not found
     */
    int getColumn(const String& id);

    /**
     * Show or hide an item
     * @param item The item
     * @param show True to show, false to hide
     */
    inline void showItem(QtTreeItem& item, bool show) {
	    if (item.isHidden() != show)
		return;
	    item.setHidden(!show);
	    itemVisibleChanged(item);
	}

    /**
     * Show or hide empty children.
     * An empty item is an item without children or with all children hidden
     * @param show True to show, false to hide
     * @param parent The parent item. Set it to 0 to add to the root
     */
    void showEmptyChildren(bool show, QtTreeItem* parent = 0);

    /**
     * Set the expanded/collapsed image of an item
     * @param item The item to set
     */
    void setStateImage(QtTreeItem& item);

    /**
     * Retrieve the auto expand property
     * @return The value of the auto expand property
     */
    bool autoExpand()
	{ return m_autoExpand; }

    /**
     * Set the auto expand property
     * @param autoExpand The new value of the auto expand property
     */
    void setAutoExpand(bool autoExpand)
	{ m_autoExpand = autoExpand; }

    /**
     * Retrieve the row height
     * @return The row height
     */
    int rowHeight()
	{ return m_rowHeight; }

    /**
     * Set the row height
     * @param h The new value of the row height
     */
    void setRowHeight(int h)
	{ m_rowHeight = h; }

    /**
     * Check if the horizontal header is visible
     * @return True if the horizontal is visible
     */
    bool getHHeader() {
	    QTreeWidgetItem* h = headerItem();
	    return h && !h->isHidden();
	}

    /**
     * Show/hide the horizontal header
     * @param on True to show the horizontal header, false to hide it
     */
    void setHHeader(bool on) {
	    QTreeWidgetItem* h = headerItem();
	    if (h)
		h->setHidden(!on);
	}

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
    void setItemUi(QString value);

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
    void setItemStyle(QString value);

    /**
     * Read _yate_itemselectedstyle property accessor: does nothing
     * This method is here to stop MOC compiler complaining about missing READ accessor function
     */
    QString itemSelectedStyle()
	{ return QString(); }

    /**
     * Set an item props selected style sheet
     * @param value Item props selected style sheet. Format [type:]stylesheet
     */
    void setItemSelectedStyle(QString value);

    /**
     * Read _yate_itemstatewidget property accessor: does nothing
     * This method is here to stop MOC compiler complaining about missing READ accessor function
     */
    QString itemStateWidget()
	{ return QString(); }

    /**
     * Set an item props state widget name
     * @param value Item props state widget name. Format [type:]widgetname
     */
    void setItemStateWidget(QString value);

    /**
     * Read _yate_itemexpandedimage property accessor: does nothing
     * This method is here to stop MOC compiler complaining about missing READ accessor function
     */
    QString itemExpandedImage()
	{ return QString(); }

    /**
     * Set an item's expanded image
     * @param value Item props expanded image. Format [type:]imagefile
     */
    void setExpandedImage(QString value);

    /**
     * Read _yate_itemcollapsedimage property accessor: does nothing
     * This method is here to stop MOC compiler complaining about missing READ accessor function
     */
    QString itemCollapsedImage()
	{ return QString(); }

    /**
     * Set an item's collapsed image
     * @param value Item props collapsed image. Format [type:]imagefile
     */
    void setItemCollapsedImage(QString value);

    /**
     * Read _yate_itemtooltip property accessor: does nothing
     * This method is here to stop MOC compiler complaining about missing READ accessor function
     */
    QString itemTooltip()
	{ return QString(); }

    /**
     * Set an item's tooltip template
     * @param value Item props tooltip template. Format [type:]imagefile
     */
    void setItemTooltip(QString value);

    /**
     * Read _yate_itemstatswidget property accessor: does nothing
     * This method is here to stop MOC compiler complaining about missing READ accessor function
     */
    QString itemStatsWidget()
	{ return QString(); }

    /**
     * Set an item's statistics widget name
     * @param value Item props statistics widget name. Format [type:]widget_name
     */
    void setItemStatsWidget(QString value);

    /**
     * Read _yate_itemstatstemplate property accessor: does nothing
     * This method is here to stop MOC compiler complaining about missing READ accessor function
     */
    QString itemStatsTemplate()
	{ return QString(); }

    /**
     * Set an item's statistics template
     * @param value Item props statistics template. Format [type:]template
     */
    void setItemStatsTemplate(QString value);

    /**
     * Read _yate_itemheight property accessor: does nothing
     * This method is here to stop MOC compiler complaining about missing READ accessor function
     */
    QString itemHeight()
	{ return QString(); }

    /**
     * Set an item props height
     * @param value Item props height. Format [type:]height
     */
    void setItemHeight(QString value);

    /**
     * Read _yate_itembackground property accessor: does nothing
     * This method is here to stop MOC compiler complaining about missing READ accessor function
     */
    QString itemBg()
	{ return QString(); }

    /**
     * Set an item props background
     * @param value Item props background. Format [type:]background
     */
    void setItemBg(QString value);

    /**
     * Retrieve a comma separated list with column widths
     * @return Comma separated list containing column widths
     */
    QString colWidths();

    /**
     * Set column widths
     * @param witdhs Comma separated list containing column widths
     */
    void setColWidths(QString widths);

    /**
     * Retrieve tree sorting string (column and order)
     * @return Sorting string
     */
    QString sorting()
	{ return getSorting(); }

    /**
     * Set sorting (column and order)
     * @param s Sorting string
     */
    void setSorting(QString s);

    /**
     * Retrieve items expanded status value
     * @return Items expanded status value
     */
    QString itemsExpStatus();

    /**
     * Set items expanded status value
     * param s Items expanded status value
     */
    void setItemsExpStatus(QString s);

protected slots:
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
     * Handle item children select
     */
    void itemChildSelect()
	{ onSelect(sender()); }

    /**
     * Catch item selection changes
     * @param sel Selected item
     * @param prev Previous selected item
     */
    void selectionChangedSlot(QTreeWidgetItem* sel, QTreeWidgetItem* prev)
	{ onSelChanged(static_cast<QtTreeItem*>(sel),static_cast<QtTreeItem*>(prev)); }

    /**
     * Catch item double click
     * @param item The item
     * @param column Clicked column
     */
    void itemDoubleClickedSlot(QTreeWidgetItem* item, int column)
	{ onItemDoubleClicked(static_cast<QtTreeItem*>(item),column); }

    /**
     * Catch item expanded signal
     * @param item The item
     */
    void itemExpandedSlot(QTreeWidgetItem* item)
	{ onItemExpandedChanged(static_cast<QtTreeItem*>(item)); }

    /**
     * Catch item collapsed signal
     * @param item The item
     */
    void itemCollapsedSlot(QTreeWidgetItem* item)
	{ onItemExpandedChanged(static_cast<QtTreeItem*>(item)); }

protected:
    /**
     * Retrieve the item props name associated with tree item type
     * @param type Item type
     * @return Item props name or empty if not found
     */
    inline const String& itemPropsName(int type) const
	{ return m_itemPropsType[String(type)]; }

    /**
     * Retrieve the item type integer value from associated string (name)
     * @param name Item type name
     * @return Associated item type integer value. QTreeWidgetItem::Type if not found
     */
    int itemType(const String& name) const;

    /**
     * Retrieve tree sorting
     * @return Sorting string
     */
    virtual QString getSorting();

    /**
     * Set tree sorting
     * @param key Sorting key
     * @param sort Sort order
     */
    virtual void updateSorting(const String& key, Qt::SortOrder sort);

    /**
     * Build a tree context menu
     * @param menu Menu to replace on success
     * @param ns Pointer to received parameter
     * @return True on success
     */
    bool buildMenu(QMenu*& menu, NamedString* ns);

    /**
     * Apply item widget style sheet
     * @param item Target item
     * @param selected True to apply selected item style
     */
    void applyStyleSheet(QtTreeItem* item, bool selected);

    /**
     * Update a tree item
     * @param item Item to update
     * @param params Item parameters
     * @return True on success
     */
    virtual bool updateItem(QtTreeItem& item, const NamedList& params);

    /**
     * Process item selection changes
     * @param sel Selected item
     * @param prev Previous selected item
     */
    virtual void onSelChanged(QtTreeItem* sel, QtTreeItem* prev);

    /**
     * Process item double click
     * @param item The item
     * @param column Clicked column
     */
    virtual void onItemDoubleClicked(QtTreeItem* item, int column);

    /**
     * Item expanded/collapsed notification
     * @param item The item
     */
    virtual void onItemExpandedChanged(QtTreeItem* item);

    /**
     * Catch a context menu event and show the context menu
     * @param e Context menu event
     */
    virtual void contextMenuEvent(QContextMenuEvent* e);

    /**
     * Get the context menu associated with a given item
     * @param item The item (can be 0)
     * @return QMenu pointer or 0
     */
    virtual QMenu* contextMenu(QtTreeItem* item);

    /**
     * Item added notification
     * @param item Added item
     * @param parent The parent of the added tree item. 0 if added to the root
     */
    virtual void itemAdded(QtTreeItem& item, QtTreeItem* parent);

    /**
     * Item removed notification.
     * The item will be deleted after returning from this notification
     * @param item Removed item
     * @param parent The tree item from which the item was removed. 0 if removed from root
     */
    virtual void itemRemoved(QtTreeItem& item, QtTreeItem* parent);

    /**
     * Handle item visiblity changes
     * @param item Changed item
     */
    virtual void itemVisibleChanged(QtTreeItem& item);

    /**
     * Uncheck all checkable columns in a given item
     * @param item The item
     */
    virtual void uncheckItem(QtTreeItem& item);

    /**
     * Update a tree item's tooltip
     * @param item Item to update
     */
    virtual void applyItemTooltip(QtTreeItem& item);

    /**
     * Fill a list with item statistics.
     * The default implementation fills a 'count' parameter with the number of item children
     * @param item The tree item
     * @param list The list to fill
     */
    virtual void fillItemStatistics(QtTreeItem& item, NamedList& list);

    /**
     * Update a tree item's statistics
     * @param item Item to update
     */
    void applyItemStatistics(QtTreeItem& item);

    /**
     * Store (update) to or remove from item expanded status storage an item
     * @param id Item id
     * @param on Expanded status
     * @param store True to store, false to remove
     */
    void setStoreExpStatus(const String& id, bool on, bool store = true);

    /**
     * Retrieve the expanded status of an item from storage
     * @param id Item id
     * @return 1 if expanded, 0 if collapsed, -1 if not found
     */
    int getStoreExpStatus(const String& id);

    bool m_hasCheckableCols;             // True if we have checkable columns
    QMenu* m_menu;                       // Tree context menu
    bool m_autoExpand;                   // Items are expanded when added
    int m_rowHeight;                     // Tree row height
    NamedList m_itemPropsType;           // Tree item type to item props translation
    QList<QtTokenDict> m_expStatus;      // List of stored item IDs and expanded status
};

/**
 * This class holds a contact list tree
 * @short A contact list tree
 */
class ContactList : public QtCustomTree
{
    YCLASS(ContactList,QtCustomTree)
    Q_CLASSINFO("ContactList","Yate")
    Q_OBJECT
    Q_PROPERTY(QString _yate_nogroup_caption READ noGroupCaption WRITE setNoGroupCaption(QString))
    Q_PROPERTY(bool _yate_flatlist READ flatList WRITE setFlatList(bool))
    Q_PROPERTY(bool _yate_showofflinecontacts READ showOffline WRITE setShowOffline(bool))
    Q_PROPERTY(bool _yate_hideemptygroups READ hideEmptyGroups WRITE setHideEmptyGroups(bool))
    Q_PROPERTY(bool _yate_comparecase READ cmpNameCs WRITE setCmpNameCs(bool))
public:
    /**
     * List item type enumeration
     */
    enum ItemType {
	TypeContact = QtCustomTree::TypeCount,
	TypeChatRoom = QtCustomTree::TypeCount + 1,
	TypeGroup,
    };

    /**
     * Constructor
     * @param name The name of the object
     * @param params List parameters
     * @param parent List parent
     */
    ContactList(const char* name, const NamedList& params, QWidget* parent);

    /**
     * Set list parameters
     * @param params Parameter list
     * @return True on success
     */
    virtual bool setParams(const NamedList& params);

    /**
     * Update an existing item
     * @param item Item id
     * @param data Item parameters
     * @return True on success
     */
    virtual bool setTableRow(const String& item, const NamedList* data);

    /**
     * Add a new entry (account or contact) to the tree
     * @param item Item id
     * @param data Item parameters
     * @param asStart True if the entry is to be inserted at the start of
     *   the table, false if it is to be appended
     * @return True if the entry has been added, false otherwise
     */
    virtual bool addTableRow(const String& item, const NamedList* data = 0,
	bool atStart = false);

    /**
     * Remove an item from tree
     * @param item Item id
     * @return True on success
     */
    virtual bool delTableRow(const String& item);

    /**
     * Add, set or remove one or more contacts.
     * Screen update is locked while changing the tree.
     * Each data list element is a NamedPointer carrying a NamedList with item parameters.
     * The name of an element is the item to update.
     * Set element's value to boolean value 'true' to add a new item if not found
     *  or 'false' to set an existing one. Set it to empty string to delete the item
     * @param data The list of items to add/set/delete
     * @param atStart True to add new items at start, false to add them to the end
     * @return True on success
     */
    virtual bool updateTableRows(const NamedList* data, bool atStart = false);

    /**
     * Count online/total contacts in a group.
     * @param grp The group item
     * @param total The number of contacts in the group
     * @param online The number of online contacts in the group
     */
    virtual void countContacts(QtTreeItem* grp, int& total, int& online);

    /**
     * Contact list changed notification
     * This method is called each time a contact is added, removed or changed or
     *  properties affecting display are changed
     */
    virtual void listChanged();

    /**
     * Find a contact
     * @param id Contact id
     * @param list Optional list to be filled with items having the given id
     * @return ContactItem pointer or 0 if not found
     */
    ContactItem* findContact(const String& id, QList<QtTreeItem*>* list = 0);

    /**
     * Retrieve the value of '_yate_nogroup_caption' property
     * @return The value of '_yate_nogroup_caption' property
     */
    QString noGroupCaption()
	{ return QtClient::setUtf8(m_noGroupText); }

    /**
     * Set '_yate_nogroup_caption' property
     * @param value The new value for '_yate_nogroup_caption' property
     */
    void setNoGroupCaption(QString value);

    /**
     * Check if the list is flat
     * @return True if contacts are not grouped
     */
    bool flatList()
	{ return m_flatList; }

    /**
     * Set the flat list property
     * @param flat The new value of the flat list property
     */
    void setFlatList(bool flat);

    /**
     * Check if offline contacts are shown
     * @return True if the list displaying offline contacts
     */
    bool showOffline()
	{ return m_showOffline; }

    /**
     * Show or hide offline contacts
     * @param value True to show, false to hide offline contacts
     */
    void setShowOffline(bool value);

    /**
     * Check if empty groups are hidden
     * @return True if empty groups are hidden
     */
    bool hideEmptyGroups()
	{ return m_hideEmptyGroups; }

    /**
     * Show or hide empty groups
     * @param value True to hide, false to show empty groups
     */
    void setHideEmptyGroups(bool value) {
	    if (m_hideEmptyGroups == value)
		return;
	    m_hideEmptyGroups = value;
	    if (!m_flatList)
		showEmptyChildren(!m_hideEmptyGroups);
	}

    /**
     * Retrieve contact name comparison
     * @return True if contact names are compared case sensitive
     */
    bool cmpNameCs()
	{ return m_compareNameCs == Qt::CaseSensitive; }

    /**
     * Set contact name comparison
     * @return True to compare contact names case sensitive
     */
    void setCmpNameCs(bool value)
	{ m_compareNameCs = (value ? Qt::CaseSensitive : Qt::CaseInsensitive); }

    /**
     * Check if a given type is a contact or chat room
     * @param type Type to check
     * @return True if the type is contact or chat room
     */
    static inline bool isContactType(int type)
	{ return type == TypeContact || type == TypeChatRoom; }

    /**
     * Get contact type from a string value
     * @param val The string
     * @return Contact type value
     */
    static inline int contactType(const String& val) {
	    if (!val || val != "chatroom")
		return TypeContact;
	    return TypeChatRoom;
	}

    /**
     * Create a group item
     * @param id Group id
     * @param name Group name
     * @param expStat Expanded state (re)store indicator
     * @return Valid QtTreeItem pointer
     */
    static inline QtTreeItem* createGroup(const String& id, const String& name, bool expStat) {
	    QtTreeItem* g = new QtTreeItem(id,TypeGroup,name,expStat);
	    g->addParam("name",name);
	    return g;
	}

protected:
    /**
     * Retrieve tree sorting
     * @return Sorting string
     */
    virtual QString getSorting();

    /**
     * Set tree sorting
     * @param key Sorting key
     * @param sort Sort order
     */
    virtual void updateSorting(const String& key, Qt::SortOrder sort);

    /**
     * Optimized add. Set the whole tree
     * @param list The list of contacts to set
     */
    void setContacts(QList<QTreeWidgetItem*>& list);

    /**
     * Create a contact
     * @param id Contact id
     * @param params Contact parameters
     * @return ContactItem pointer
     */
    ContactItem* createContact(const String& id, const NamedList& params);

    // Update contact count in a group
    void updateGroupCountContacts(QtTreeItem& item);

    // Add or update a contact
    bool updateContact(const String& id, const NamedList& params);

    // Update a contact
    bool updateContact(ContactItem& c, const NamedList& params, bool all = true);

    // Remove a contact from tree
    bool removeContact(const String& id);

    /**
     * Update a tree item
     * @param item Item to update
     * @param params Item parameters
     * @return True on success
     */
    virtual bool updateItem(QtTreeItem& item, const NamedList& params);

    /**
     * Get the context menu associated with a given item
     * @param item The item (can be 0)
     * @return QMenu pointer or 0
     */
    virtual QMenu* contextMenu(QtTreeItem* item);

    /**
     * Item added notification
     * @param item Added item
     * @param parent The parent of the added tree item. 0 if added to the root
     */
    virtual void itemAdded(QtTreeItem& item, QtTreeItem* parent);

    /**
     * Fill a list with item statistics.
     * The default implementation fills a 'count' parameter with the number of item children
     * @param item The tree item
     * @param list The list to fill
     */
    virtual void fillItemStatistics(QtTreeItem& item, NamedList& list);

    /**
     * Retrieve a group item from root or create a new one
     * @param name Group name or empry to use the empty group
     * @param create True to create if not found
     * @return QtTreeItem pointer or 0
     */
    QtTreeItem* getGroup(const String& name = String::empty(), bool create = true);

    /**
     * Add a contact to the list
     * @param id Contact id
     * @param params Contact parameters
     */
    void addContact(const String& id, const NamedList& params);

    /**
     * Add a contact to a specified parent
     * @param c The contact to add
     * @param grp Optional parent
     */
    void addContact(ContactItem* c, QtTreeItem* parent = 0);

    /**
     * Replace an existing contact. Remove it and add it again
     * @param c The contact item
     * @param params Contact parameters
     */
    void replaceContact(ContactItem& c, const NamedList& params);

    /**
     * Create contact structure (groups and lists)
     * @param c The contact to add
     * @param cil Contact structure
     */
    void createContactTree(ContactItem* c, ContactItemList& cil);

    /**
     * Compare two contacts's name
     * @param c1 First contact
     * @param c2 Second contact
     * @return -1 if c1 < c2, 0 if c1 == c2, 1 if c1 > c2
     */
    int compareContactName(ContactItem* c1, ContactItem* c2);

    /**
     * Sort contacts
     * @param list The list of contacts to sort
     */
    void sortContacts(QList<QTreeWidgetItem*>& list);

private:
    int m_savedIndent;
    bool m_flatList;                     // Flat list
    bool m_showOffline;                  // Show or hide offline contacts
    bool m_hideEmptyGroups;              // Show or hide empty groups
    bool m_expStatusGrp;                 // Save/restore groups expanded status
    String m_noGroupText;                // Group text to show for contacts not belonging to any group
    QMap<QString,QString> m_statusOrder; // Status order (names are mapped to status icons)
    QMenu* m_menuContact;
    QMenu* m_menuChatRoom;
    // Sorting
    String m_sortKey;                    // Sorting key
    Qt::SortOrder m_sortOrder;           // Sort order
    Qt::CaseSensitivity m_compareNameCs; // Contact name case comparison
};

/**
 * This class holds a contact list contact tree item
 * @short A contact list contact
 */
class ContactItem : public QtTreeItem
{
    YCLASS(ContactItem,QtTreeItem)
public:
    inline ContactItem(const char* id, const NamedList& p = NamedList::empty(),
	bool contact = true)
	: QtTreeItem(id,ContactList::contactType(p["type"]))
	{}
    // Build and return a list of groups
    inline ObjList* groups() const
	{ return Client::splitUnescape((*this)["groups"]); }
    // Update name. Return true if changed
    bool updateName(const NamedList& params, Qt::CaseSensitivity cs);
    // Check if groups would change
    bool groupsWouldChange(const NamedList& params);
    // Check if the contact status is 'offline'
    bool offline();

    QString m_name;
};

/**
 * Utility class used to hold contact groups along with contacts
 * @short Groups and contact items belonging to them
 */
class ContactItemList
{
public:
    /**
     * Retrieve a group. Create it if not found. Create contact list entry when a group is created
     * @param id Group id
     * @param text Group text
     * @param expStat Expanded state (re)store indicator for created item
     * @return Valid groups index
     */
    int getGroupIndex(const String& id, const String& text, bool expStat);

    QList<QTreeWidgetItem*> m_groups;
    QList<QtTreeItemList> m_contacts;
};

}; // anonymous namespace

#endif // __CUSTOMTREE_H

/* vi: set ts=8 sw=4 sts=4 noet: */
