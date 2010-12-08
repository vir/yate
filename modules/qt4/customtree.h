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
	: QtUIWidgetItemProps(type)
	{}

    String m_stateWidget;                // Item widget showing the state
    String m_stateExpandedImg;           // Image to show when expanded
    String m_stateCollapsedImg;          // Image to show when collapsed
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
     */
    QtTreeItem(const char* id, int type = Type,	const char* text = 0);

    /**
     * Destructor
     */
    ~QtTreeItem();

    /**
     * Retrieve the item id
     * @return Item id
     */
    inline const String& id() const
	{ return toString(); }
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
    Q_PROPERTY(QString _yate_itemui READ itemUi WRITE setItemUi(QString))
    Q_PROPERTY(QString _yate_itemstyle READ itemStyle WRITE setItemStyle(QString))
    Q_PROPERTY(QString _yate_itemselectedstyle READ itemSelectedStyle WRITE setItemSelectedStyle(QString))
    Q_PROPERTY(QString _yate_itemstatewidget READ itemStateWidget WRITE setItemStateWidget(QString))
    Q_PROPERTY(QString _yate_itemexpandedimage READ itemExpandedImage WRITE setExpandedImage(QString))
    Q_PROPERTY(QString _yate_itemcollapsedimage READ itemCollapsedImage WRITE setItemCollapsedImage(QString))
    Q_PROPERTY(QString _yate_col_widths READ colWidths WRITE setColWidths(QString))
    Q_PROPERTY(QString _yate_sorting READ sorting WRITE setSorting(QString))
public:
    /**
     * List item type enumeration
     */
    enum ItemType {
	TypeCount = QTreeWidgetItem::UserType
    };

    /**
     * Constructor
     * @param name Object name
     * @param params Parameters
     * @param parent Optional parent
     */
    QtCustomTree(const char* name, const NamedList& params, QWidget* parent = 0);

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
    QString sorting();

    /**
     * Set sorting (column and order)
     * @param s Sorting string
     */
    void setSorting(QString s);

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
     * Build a tree context menu
     * @param menu Menu to replace on success
     * @param ns Pointer to received parameter
     * @return True on success
     */
    bool buildMenu(QMenu*& menu, NamedString* ns);

    /**
     * Retrieve all items' id
     * @param items List to fill with widget's items
     * @param parent Optional parent
     * @param recursive True to retrieve parent children
     */
    void getOptions(NamedList& items, QtTreeItem* parent = 0, bool recursive = true);

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
     */
    virtual void itemAdded(QtTreeItem& item);

    QMenu* m_menu;                       // Tree context menu
    bool m_autoExpand;                   // Items are expanded when added
    int m_rowHeight;                     // Tree row height
    NamedList m_itemPropsType;           // Tree item type to item props translation
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
public:
    /**
     * List item type enumeration
     */
    enum ItemType {
	TypeContact = QtCustomTree::TypeCount,
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

protected:
    // Update contact count in a group
    void updateGroupCountContacts(QtTreeItem& item);

    // Add or update a contact
    bool updateContact(const String& id, const NamedList& params, bool atStart);

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
     * Item expanded/collapsed notification
     * @param item The item
     */
    virtual void onItemExpandedChanged(QtTreeItem* item);

    /**
     * Get the context menu associated with a given item
     * @param item The item (can be 0)
     * @return QMenu pointer or 0
     */
    virtual QMenu* contextMenu(QtTreeItem* item);

    /**
     * Item added notification
     * @param item Added item
     */
    virtual void itemAdded(QtTreeItem& item);

    /**
     * Retrieve a group item from root or create a new one
     * @param name Group name or empry to use the empty group
     * @param create True to create if not found
     * @return QtTreeItem pointer or 0
     */
    QtTreeItem* getGroup(const String& name = String::empty(), bool create = true);

    /**
     * Add a contact to a group item
     *
     */
    bool addContactToGroup(const String& id,
	const NamedList& params, bool atStart, const String& grp = String::empty(),
	QList<ContactItem*>* bucket = 0, const NamedList* origParams = 0);

    /**
     * Remove a contact from a group item and add it to a list
     *
     */
    void removeContactFromGroup(QList<ContactItem*> list, const String& id,
	const String& grp = String::empty());

private:
    int m_savedIndent;
    bool m_flatList;                     // Flat list
    bool m_showOffline;                  // Show or hide offline contacts
    bool m_hideEmptyGroups;              // Show or hide empty groups
    String m_groupCountWidget;           // The name of the widget used to display
                                         //  online/count contacts
    String m_noGroupText;                // Group text to show for contacts not belonging to any group
    QMap<QString,QString> m_statusOrder; // Status order (names are mapped to status icons)
    QMenu* m_menuContact;
};

/**
 * This class holds a contact list contact tree item
 * @short A contact list contact
 */
class ContactItem : public QtTreeItem
{
    YCLASS(ContactItem,QtTreeItem)
public:
    inline ContactItem(const char* id, const NamedList& p = NamedList::empty())
	: QtTreeItem(id,ContactList::TypeContact,p.getValue("name"))
	{}
    // Build and return a list of groups
    inline ObjList* groups() const
	{ return Client::splitUnescape((*this)["groups"]); }
    // Check if the contact status is 'offline'
    bool offline();
};

}; // anonymous namespace

#endif // __CUSTOMTREE_H

/* vi: set ts=8 sw=4 sts=4 noet: */
