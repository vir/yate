/**
 * customtree.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Custom QtTree based objects
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

#ifndef __CUSTOMTREE_H
#define __CUSTOMTREE_H

#include "qt4client.h"

using namespace TelEngine;
namespace { // anonymous

class QtCellGridDraw;                    // Draw cell grid
class QtTreeItemProps;                   // Tree widget container item properties
class QtTreeDrag;                        // Drag data builder
class QtTreeItem;                        // A tree widget item
class QtCustomTree;                      // A custom tree widget
class ContactList;                       // A contact list tree
class ContactItem;                       // A contact list contact
class ContactItemList;                   // Groups and contact items belonging to them
class FileListTree;                      // Specialized tree showing directories and files
class QtPaintItemDesc;                   // Generic item description (base class)
class QtPaintButtonDesc;                 // Button description
class QtPaintItem;                       // Custom painted item
class QtPaintButton;                     // Custom painted button
class QtPaintItems;                      // Holds items to paint
class QtPaintImages;                     // Holds images to paint
class QtItemDelegate;                    // Custom item delegate
class QtHtmlItemDelegate;                // Custom HTML item delegate

typedef QList<QTreeWidgetItem*> QtTreeItemList;
typedef QPair<QTreeWidgetItem*,QString> QtTreeItemKey;
typedef QPair<String,int> QtTokenDict;


/**
 * Utility used to draw a cell grid (borders)
 * @short Draw cell grid (borders)
 */
class QtCellGridDraw
{
public:
    /**
     * Position and flags enumeration
     */
    enum Position {
	None = 0,
	Left = 1,
	Top = 2,
	Right = 4,
	Bottom = 8,
	DrawStart = 16,
	DrawEnd = 32,
	// Masks
	Pos = Left | Top | Right | Bottom,
    };

    /**
     * Constructor
     * @param flags Optional flags
     */
    explicit inline QtCellGridDraw(int flags = 0)
	: m_flags(flags)
	{}

    /**
     * Retrieve specific flags if set
     * @param val Flags to retrieve
     * @return Draw flags masked with given value
     */
    inline int flag(int val) const
	{ return (m_flags & val); }

    /**
     * Set draw pen
     * @param pos Position to set pen (Left, Right, Top or Bottom)
     * @param pen Pen to set
     */
    void setPen(Position pos, QPen pen);

    /**
     * Set draw pens from a list of parameters
     * @param params Parameter list
     */
    void setPen(const NamedList& params);

    /**
     * Set pen from parameters list
     * @param pos Position to set
     * @param params Parameter list
     */
    void setPen(Position pos, const NamedList& params);

    /**
     * Draw the borders
     * @param p The painter to use
     * @param rect Cell rectangle
     * @param isFirstRow True if drawing the first row
     * @param isFirstColumn True if drawing the first column
     * @param isLastRow True if drawing the last row
     * @param isLastColumn True if drawing the last column
     */
    void draw(QPainter* p, QRect& rect, bool isFirstRow, bool isFirstColumn,
	bool isLastRow, bool isLastColumn) const;

protected:
    int m_flags;
    QPen m_left;
    QPen m_top;
    QPen m_right;
    QPen m_bottom;
};


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
	m_height(-1), m_editable(false)
	{}

    /**
     * Set a button's action, create if it not found
     * @param name Button name
     * @param action Button action
     * @return True on success, false if 'name' was found but is not a button
     */
    bool setPaintButtonAction(const String& name, const String& action);

    /**
     * Set a button's parameter, create it if not found
     * @param name Button name
     * @param param Parameter to set
     * @param value Parameter value
     * @return True on success, false if 'name' was found but is not a button
     */
    bool setPaintButtonParam(const String& name, const String& param,
	const String& value = String::empty());

    int m_height;                        // Item height
    String m_stateWidget;                // Item widget or column showing the state
    String m_stateExpandedImg;           // Image to show when expanded
    String m_stateCollapsedImg;          // Image to show when collapsed
    String m_toolTip;                    // Tooltip template
    String m_statsWidget;                // Item widget showing statistics while collapsed
    String m_statsTemplate;              // Statistics template (may include ${count} for children count)
    QBrush m_bg;                         // Item background
    QRect m_margins;                     // Item internal margins
    bool m_editable;                     // Item is editable
    ObjList m_paintItemsDesc;            // Paint items description
};


/**
 * This class holds data used to build tree drag data
 * @short Drag data builder
 */
class QtTreeDrag : public QObject, public GenObject
{
    YCLASS(QtTreeDrag,GenObject)
    Q_CLASSINFO("QtTreeDrag","Yate")
    Q_OBJECT
public:
    /**
     * Constructor
     * @param parent Object parent
     * @param params Optional parameters
     */
    QtTreeDrag(QObject* parent, const NamedList* params = 0);

    /**
     * Set the URL builder, set to NULL if fmt is empty
     * @param format Format to use when building base URL
     * @param queryParams Query params to add to URL
     */
    void setUrlBuilder(const String& fmt = String::empty(),
	const String& queryParams = String::empty());

    /**
     * Build MIME data for a list of items
     * @param item The list
     * @return QMimeData pointer, 0 on failure
     */
    QMimeData* mimeData(const QList<QTreeWidgetItem*> items) const;

protected:
    QtUrlBuilder* m_urlBuilder;
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
     * Check if the item is filtered (filter matched)
     * @return True if the item is filtered
     */
    inline bool filterMatched() const
	{ return m_filtered; }

    /**
     * Update item filtered flag. Set it to true if the parameter list pointer is 0
     * @param filter Filter parameter list
     * @return Filtered value
     */
    bool setFilter(const NamedList* filter);

    /**
     * Retrieve extra data to paint on right side of the item
     * @return QtPaintItems pointer held by this item (may be 0)
     */
    inline QtPaintItems* extraPaintRight() const
	{ return m_extraPaintRight; }

    /**
     * Set extra data to paint on right side of the item
     * @param obj Object to set
     */
    void setExtraPaintRight(QtPaintItems* obj = 0);

    /**
     * Set extra paint buttons on right side of the item
     * @param list Buttons list
     * @param props Item props containing the description
     */
    void setExtraPaintRightButtons(const String& list, QtTreeItemProps* props);

    /**
     * Save/restore item expanded status
     */
    bool m_storeExp;

    /**
     * Item height delta from global item size
     */
    int m_heightDelta;

protected:
    bool m_filtered;                     // Item filtered flag
    QtPaintItems* m_extraPaintRight;     // Extra items to paint on right side
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
    Q_PROPERTY(bool _yate_notifyitemchanged READ getNotifyItemChanged WRITE setNotifyItemChanged(bool))
    Q_PROPERTY(QString _yate_itemui READ itemUi WRITE setItemUi(QString))
    Q_PROPERTY(QString _yate_itemstyle READ itemStyle WRITE setItemStyle(QString))
    Q_PROPERTY(QString _yate_itemselectedstyle READ itemSelectedStyle WRITE setItemSelectedStyle(QString))
    Q_PROPERTY(QString _yate_itemacceptdrop READ itemAcceptDrop WRITE setItemAcceptDrop(QString))
    Q_PROPERTY(QString _yate_itemacceptdroponempty READ itemAcceptDropOnEmpty WRITE setItemAcceptDropOnEmpty(QString))
    Q_PROPERTY(QString _yate_itemstatewidget READ itemStateWidget WRITE setItemStateWidget(QString))
    Q_PROPERTY(QString _yate_itemexpandedimage READ itemExpandedImage WRITE setExpandedImage(QString))
    Q_PROPERTY(QString _yate_itemcollapsedimage READ itemCollapsedImage WRITE setItemCollapsedImage(QString))
    Q_PROPERTY(QString _yate_itemtooltip READ itemTooltip WRITE setItemTooltip(QString))
    Q_PROPERTY(QString _yate_itemstatswidget READ itemStatsWidget WRITE setItemStatsWidget(QString))
    Q_PROPERTY(QString _yate_itemstatstemplate READ itemStatsTemplate WRITE setItemStatsTemplate(QString))
    Q_PROPERTY(QString _yate_itemheight READ itemHeight WRITE setItemHeight(QString))
    Q_PROPERTY(QString _yate_itembackground READ itemBg WRITE setItemBg(QString))
    Q_PROPERTY(QString _yate_itemmargins READ itemMargins WRITE setItemMargins(QString))
    Q_PROPERTY(QString _yate_itemeditable READ itemEditable WRITE setItemEditable(QString))
    Q_PROPERTY(QString _yate_itempaintbutton READ itemPaintButton WRITE setItemPaintButton(QString))
    Q_PROPERTY(QString _yate_itempaintbuttonparam READ itemPaintButtonParam WRITE setItemPaintButtonParam(QString))
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
	RoleMargins,                     // Role containing item internal margins
	RoleQtDrawItems,                 // Role containing extra display data (QObject descendent)
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
     * Method re-implemented from QTreeWidget.
     * Draw item grid if set
     */
    virtual void drawRow(QPainter* p, const QStyleOptionViewItem& opt,
	const QModelIndex& idx) const;

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
     * Retrieve multiple selection
     * @param items List to be to filled with selection's contents
     * @return True if the operation was successfull
     */
    virtual bool getSelect(NamedList& items);

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
     * Retrieve model index for a given item
     * @param item Item to edit
     * @param what Optional sub-item
     * @return Model index for the item, can be invalid
     */
     virtual QModelIndex modelIndex(const String& item, const String* what = 0);

    /**
     * Update a tree item
     * @param item Item to update
     * @param params Item parameters
     * @return True on success
     */
    virtual bool updateItem(QtTreeItem& item, const NamedList& params);

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
     * Find all tree items from model
     * @param list Model index list
     * @return The list of items
     */
     QList<QtTreeItem*> findItems(QModelIndexList list);

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
    inline int getItemRowHeight(int type) const {
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
    inline QtTreeItemProps* treeItemProps(int type) const {
	    QtUIWidgetItemProps* pt = QtUIWidget::getItemProps(itemPropsName(type));
	    return YOBJECT(QtTreeItemProps,pt);
	}

    /**
     * Retrieve item properties associated with a given item
     * @param item Item address
     * @return QtTreeItemProps poinetr or 0 if not found
     */
    inline QtTreeItemProps* treeItemProps(QtTreeItem& item) const
	{ return treeItemProps(item.type()); }

    /**
     * Retrieve item properties associated with a given item
     * @param item Item pointer
     * @return QtTreeItemProps poinetr or 0 if not found
     */
    inline QtTreeItemProps* treeItemProps(QtTreeItem* item) const
	{ return item ? treeItemProps(item->type()) : 0; }

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
     * Retrieve a list with column IDs
     * @return QStringList containing column IDs
     */
    QStringList columnIDs();

    /**
     * Retrieve a column id by column number
     * @param buf Destination buffer
     * @param col column number
     * @return True if found
     */
    bool getColumnName(String& buf, int col);

    /**
     * Retrieve a column by it's id
     * @param id The column id to find
     * @return Column number, -1 if not found
     */
    int getColumn(const String& id);

    /**
     * Convert a value to int, retrieve a column index
     * @param str Column number or name
     * @return Column number, -1 if not found
     */
    inline int getColumnNo(const String& str) {
	    int val = str.toInteger(-1);
	    return val >= 0 ? val : getColumn(str);
	}

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
     * @param props Optional pointer to item props, detect it if 0
     */
    void setStateImage(QtTreeItem& item, QtTreeItemProps* props = 0);

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
     * Check if this table is notifying item changed
     * @return True if this table is notifying item changed
     */
    bool getNotifyItemChanged()
	{ return m_notifyItemChanged; }

    /**
     * Set/reset item changed notification flag
     * @param on True to notify item changes, false to disable the notification
     */
    void setNotifyItemChanged(bool on)
	{ m_notifyItemChanged = on; }

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
     * Read _yate_itemacceptdrop property accessor: does nothing
     * This method is here to stop MOC compiler complaining about missing READ accessor function
     */
    QString itemAcceptDrop()
	{ return QString(); }

    /**
     * Set an item props accept drop
     * @param value Item props accept drop. Format [type:]acceptdrop
     */
    void setItemAcceptDrop(QString value);

    /**
     * Read _yate_itemacceptdroponempty property accessor: does nothing
     * This method is here to stop MOC compiler complaining about missing READ accessor function
     */
    QString itemAcceptDropOnEmpty()
	{ return QString(); }

    /**
     * Set accept drop on empty space
     * @param value Accept drop on empty space
     */
    void setItemAcceptDropOnEmpty(QString value) {
	    String tmp;
	    QtClient::getUtf8(tmp,value);
	    m_acceptDropOnEmpty = QtDrop::acceptDropType(tmp,QtDrop::None);
	}

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
     * Read _yate_itemmargins property accessor: does nothing
     * This method is here to stop MOC compiler complaining about missing READ accessor function
     */
    QString itemMargins()
	{ return QString(); }

    /**
     * Set an item props margins
     * @param value Item props margins. Format [type:]margins where
     *  margins is a comma separated list of item internal margins left,top,right,bottom
     */
    void setItemMargins(QString value);

    /**
     * Read _yate_itemeditable property accessor: does nothing
     * This method is here to stop MOC compiler complaining about missing READ accessor function
     */
    QString itemEditable()
	{ return QString(); }

    /**
     * Set an item props editable
     * @param value Item props margins. Format [type:]editable
     */
    void setItemEditable(QString value);

    /**
     * Read _yate_itempaintbutton property accessor: does nothing
     * This method is here to stop MOC compiler complaining about missing READ accessor function
     */
    QString itemPaintButton()
	{ return QString(); }

    /**
     * Set an item's paint button and action
     * @param value Item paint button action. Format [type:][button_name:]action_name
     */
    void setItemPaintButton(QString value);

    /**
     * Read _yate_itempaintbuttonparam property accessor: does nothing
     * This method is here to stop MOC compiler complaining about missing READ accessor function
     */
    QString itemPaintButtonParam()
	{ return QString(); }

    /**
     * Set an item's paint button parameter
     * @param value Item paint button parameter. Format [type:]button_name:param_name[:param_value]
     */
    void setItemPaintButtonParam(QString value);

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

    /**
     * Add items as list parameter
     * @param Parameter list
     * *param items Items list
     */
    static void addItems(NamedList& dest, QList<QTreeWidgetItem*> items);

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

    /**
     * Catch item changed signal
     */
    void itemChangedSlot(QTreeWidgetItem* item, int column)
	{ onItemChanged(static_cast<QtTreeItem*>(item),column); }

    /**
     * Catch item selection changed signal
     */
    void itemSelChangedSlot();

protected:
    /**
     * Re-implemented from QTreeWidget
     */
    virtual void timerEvent(QTimerEvent* e);

    /**
     * Re-implemented from QTreeWidget
     */
    virtual void drawBranches(QPainter* painter, const QRect& rect,
	const QModelIndex& index) const;

    /**
     * Re-implemented from QTreeWidget
     */
    virtual QMimeData* mimeData(const QList<QTreeWidgetItem*> items) const;

    /**
     * Re-implemented from QAbstractItemView
     */
    virtual void selectionChanged(const QItemSelection& selected,
	const QItemSelection& deselected);

    /**
     * Re-implemented from QAbstractItemView
     */
    virtual void currentChanged(const QModelIndex& current, const QModelIndex& previous);

    /**
     * Re-implemented from QWidget
     */
    virtual void dragEnterEvent(QDragEnterEvent* e);

    /**
     * Re-implemented from QWidget
     */
    virtual void dropEvent(QDropEvent* e);

    /**
     * Re-implemented from QWidget
     */
    virtual void dragMoveEvent(QDragMoveEvent* e);

    /**
     * Re-implemented from QWidget
     */
    virtual void dragLeaveEvent(QDragLeaveEvent* e);

    /**
     * Re-implemented from QWidget
     */
    virtual void mouseMoveEvent(QMouseEvent* e);

    /**
     * Re-implemented from QWidget
     */
    virtual void mousePressEvent(QMouseEvent* e);

    /**
     * Re-implemented from QWidget
     */
    virtual void mouseReleaseEvent(QMouseEvent* e);

    /**
     * Re-implemented from QTreeView
     */
    virtual void rowsAboutToBeRemoved(const QModelIndex& parent, int start, int end);

    /**
     * Retrieve the item props name associated with tree item type
     * @param type Item type
     * @return Item props name or empty if not found
     */
    inline const String& itemPropsName(int type) const
	{ return NamedInt::lookupName(m_itemPropsType,type); }

    /**
     * Retrieve the item type integer value from associated string (name)
     * @param name Item type name
     * @return Associated item type integer value. QTreeWidgetItem::Type if not found
     */
    inline int itemType(const String& name) const
	{ return NamedInt::lookup(m_itemPropsType,name,QTreeWidgetItem::Type); }

    /**
     * Add item prop to name translation
     * @param type Item type
     * @param name Type name
     */
    inline void addItemType(int type, const char* name)
	{ NamedInt::addToListUniqueName(m_itemPropsType,new NamedInt(name,type)); }

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
     * Process item changed signal
     */
    virtual void onItemChanged(QtTreeItem* item, int column);

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
     * Handle item visiblity changes
     * @param item Changed item
     */
    virtual void itemVisibleChanged(QtTreeItem& item);

    /**
     * Check item filter
     * @param item Optional item. Check root if 0
     * @param recursive True to check recursive (check children's children also)
     */
    void checkItemFilter(QtTreeItem* item = 0, bool recursive = true);

    /**
     * Handle item filter changes
     * @param item The item
     */
    virtual void itemFilterChanged(QtTreeItem& item);

    /**
     * Uncheck all checkable columns in a given item
     * @param item The item
     */
    virtual void uncheckItem(QtTreeItem& item);

    /**
     * Remove an item
     * @param item Item to remove
     * @param setSelTimer Optional boolean to be set if select trigger timer should be started,
     *  set it to 0 to let this method handle the timer
     */
    virtual void removeItem(QtTreeItem* item, bool* setSelTimer = 0);

    /**
     * Remove a list of items
     * @param items Items to remove
     */
    virtual void removeItems(QList<QTreeWidgetItem*> items);

    /**
     * Update a tree item's tooltip
     * @param item Item to update
     * @param props Optional pointer to item props, detect it if 0
     */
    virtual void applyItemTooltip(QtTreeItem& item, QtTreeItemProps* props = 0);

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
     * @param props Optional pointer to item props, detect it if 0
     */
    void applyItemStatistics(QtTreeItem& item, QtTreeItemProps* props = 0);

    /**
     * Update a tree item's margins
     * @param item Item to update
     * @param set True to set from item props, false to set an empty rect
     * @param props Optional pointer to item props, detect it if 0
     */
    virtual void applyItemMargins(QtTreeItem& item, bool set = true,
	QtTreeItemProps* props = 0);

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

    /**
     * Handle drop events
     * @param e The event
     * @return True if accepted
     */
    bool handleDropEvent(QDropEvent* e);

    /**
     * Check if an item has any selected child
     * @param item The item to check
     * @return True if it has at least 1 selected child
     */
    bool hasSelectedChild(QtTreeItem& item);

    /**
     * Check if select trigger timer should be started
     * @param item The item to check
     * @return True if select trigger timer should be started
     */
    inline bool shouldSetSelTimer(QtTreeItem& item)
	{ return !item.isSelected() && hasSelectedChild(item); }

    /**
     * Stop select trigger timer
     */
    inline void stopSelectTriggerTimer() {
	    if (!m_timerTriggerSelect)
		return;
	    killTimer(m_timerTriggerSelect);
	    m_timerTriggerSelect = 0;
	}

    /**
     * Start select trigger timer
     */
    inline void startSelectTriggerTimer() {
	    stopSelectTriggerTimer();
	    m_timerTriggerSelect = startTimer(500);
	}

    bool m_notifyItemChanged;            // Notify 'listitemchanged' action
    bool m_hasCheckableCols;             // True if we have checkable columns
    QMenu* m_menu;                       // Tree context menu
    bool m_autoExpand;                   // Items are expanded when added
    int m_rowHeight;                     // Tree row height
    ObjList m_itemPropsType;             // Tree item type to item props translation
    QList<QtTokenDict> m_expStatus;      // List of stored item IDs and expanded status
    QtCellGridDraw m_gridDraw;
    int m_changing;                      // Content is changing from client (not from user):
                                         //  avoid notifications
    NamedList* m_filter;                 // Item filter
    bool m_haveWidgets;                  // True if we loaded any widget
    bool m_haveDrawQtItems;              // True if we have any custom drawn data in items
    int m_setCurrentColumn;              // Column to set when current index changed
    QtListDrop* m_drop;                  // Drop handler
    int m_acceptDropOnEmpty;             // Accept drop on widget surface not occupied by any item
    QtTreeDrag* m_drag;                  // Drag data builder
    bool m_drawBranches;                 // Allow parent to draw branches
    int m_timerTriggerSelect;            // Trigger select timer id
    QtTreeItem* m_lastItemDrawHover;     // Last item we used to update custom drawn hover
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
     * Update a tree item
     * @param item Item to update
     * @param params Item parameters
     * @return True on success
     */
    virtual bool updateItem(QtTreeItem& item, const NamedList& params);

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
     * Update a tree item's margins
     * @param item Item to update
     * @param set True to set from item props, false to set an empty rect
     * @param props Optional pointer to item props, detect it if 0
     */
    virtual void applyItemMargins(QtTreeItem& item, bool set = true,
	QtTreeItemProps* props = 0);

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


/**
 * File list item description. The String holds the file name
 * @short A file list item
 */
class FileItem : public String
{
public:
    /**
     * Constructor
     * @param type File item type
     * @param name File name
     * @param path File path
     * @param prov Optional file icon provider
     */
    FileItem(int type, const char* name, const String& path,
	QFileIconProvider* prov = 0);

    /**
     * Constructor. Build a FileListTree up directory
     * @param path The path
     * @param prov Optional file icon provider
     */
    FileItem(const String& path, QFileIconProvider* prov = 0);

    /**
     * Destructor
     */
    ~FileItem();

    int m_type;
    String m_fullName;
    QIcon* m_icon;
};


/**
 * Load local directory content
 * @short Thread used to load local directory content
 */
class DirListThread : public QThread
{
    Q_CLASSINFO("DirListThread","Yate")
    Q_OBJECT
public:
    inline DirListThread(QObject* parent, const String& dir, bool dirs = true,
	bool files = true)
	: QThread(parent),
	m_dir(dir), m_error(0), m_listDirs(dirs), m_listUpDir(false),
	m_listFiles(files), m_iconProvider(0), m_sort(QtClient::SortNone),
	m_caseSensitive(false)
	{}
    virtual void run();

    String m_dir;
    int m_error;
    bool m_listDirs;
    bool m_listUpDir;
    bool m_listFiles;
    ObjList m_dirs;
    ObjList m_files;
    QFileIconProvider* m_iconProvider;
    int m_sort;
    bool m_caseSensitive;

protected:
    inline ObjList* addItem(int type, const char* name, ObjList& list, ObjList* last) {
	    FileItem* it = new FileItem(type,name,m_dir,m_iconProvider);
	    if (m_sort == QtClient::SortNone)
		return last->append(it);
	    return addItemSort(list,it);
	}
    ObjList* addItemSort(ObjList& list, FileItem* it);
    // Called when terminated from run()
    void runTerminated();
};


/**
 * This class holds a file list tree
 * @short Specialized tree showing directories and files
 */
class FileListTree : public QtCustomTree
{
    YCLASS(FileListTree,QtCustomTree)
    Q_CLASSINFO("FileListTree","Yate")
    Q_OBJECT
    Q_PROPERTY(QString _yate_filesystem_path READ fsPath WRITE setFsPath(QString))
    Q_PROPERTY(QString _yate_refresh READ refresh WRITE setRefresh(QString))
public:
    enum FileListPathType {
	PathNone = 0,
	PathRoot,
	PathHome,
	PathUpThenHome,
    };

    /**
     * List item type enumeration
     */
    enum ItemType {
	TypeDir = QtCustomTree::TypeCount,
	TypeFile = QtCustomTree::TypeCount + 1,
	TypeDrive = QtCustomTree::TypeCount + 2,
    };

    /**
     * Constructor
     * @param name The name of the object
     * @param params List parameters
     * @param parent List parent
     */
    FileListTree(const char* name, const NamedList& params, QWidget* parent);

    /**
     * Destructor
     */
    ~FileListTree();

    /**
     * Retrieve _yate_filesystem_path property
     */
    QString fsPath()
	{ return QtClient::setUtf8(m_fsPath); }

    /**
     * Set _yate_filesystem_path property
     */
    void setFsPath(QString path);

    /**
     * Read _yate_refresh property accessor: does nothing
     * This method is here to stop MOC compiler complaining about missing READ accessor function
     */
    QString refresh()
	{ return QString(); }

    /**
     * Set _yate_refresh property
     */
    void setRefresh(QString val);

    /**
     * Change file system path, refresh data
     * @param path New path
     * @param force Force updating even if path didn't changed
     */
    void setFsPath(const String& path = String::empty(), bool force = true);

    /**
     * Check if current path is home path
     * @return True if current path is the home one
     */
    inline bool isHomePath()
	{ return fsPath() == QDir::toNativeSeparators(QDir::homePath()); }

    /**
     * Refresh data
     * @param dir List of directory children
     * @param files List of files children
     * @param drives List of drives children
     */
    void refresh(ObjList* dirs, ObjList* files, ObjList* drives = 0);

    /**
     * Sort a list of items
     * @param list Items to sort
     * @param type Item type
     */
    virtual void sortItems(QList<QTreeWidgetItem*>& list, int type);

    /**
     * Retrieve the icon for a given item
     * @param item The item
     * @return The item icon
     */
    virtual QIcon icon(QtTreeItem& item);

    /**
     * Retrieve the icon for a given item type
     * @param type Item type
     * @param name Item name
     * @param provider The icon provider
     * @return The item icon
     */
    static QIcon fileIcon(int type, const String& name, QFileIconProvider* provider);

    /**
     * Build file full name
     * @param buf Destination buffer
     * @param path File path
     * @param name File name
     */
    static inline void buildFileFullName(String& buf, const char* path, const char* name) {
	    buf = path;
	    if (!isRootPath(buf))
		buf << Engine::pathSeparator();
#ifndef _WINDOWS
	    else if (!buf)
		buf << Engine::pathSeparator();
#endif
	    buf << name;
	}

    /**
     * Check if a path root
     * @param path Path to check
     * @return True if the given path is root
     */
    static inline bool isRootPath(const String& path) {
#ifdef _WINDOWS
	    return path.null();
#else
	    return path.null() || (path.at(0) == '/' && !path.at(1));
#endif
	}

    static const String s_upDir;

protected slots:

    /**
     * Catch dir list thread terminate signal
     */
    void onDirThreadTerminate();

protected:
    /**
     * Start/stop dir list thread
     */
    bool setDirListThread(bool on);

    /**
     * Process item double click
     * @param item The item
     * @param column Clicked column
     */
    virtual void onItemDoubleClicked(QtTreeItem* item, int column);

    /**
     * Reset the thread
     */
    void resetThread();

    bool m_fileSystemList;               // Show file system dir content
    bool m_autoChangeDir;                // Auto change directory
    bool m_listFiles;                    // List files in current directory
    int m_sort;                          // Sort files
    int m_listOnFailure;                 // What to list when fails current directory
    QFileIconProvider* m_iconProvider;   // The icon provider
    String m_nameParam;                  // Item name column
    String m_fsPath;                     // Current path
    QThread* m_dirListThread;            // Dir list thread
};


/**
 * This class implements a generic item description
 * @short Generic item description (base class)
 */
class QtPaintItemDesc : public String
{
    YCLASS(QtPaintItemDesc,String)
public:
    /**
     * Constructor
     * @param name Object name
     */
    inline QtPaintItemDesc(const char* name = 0)
	: String(name)
	{}

    /**
     * Get a QtPaintButtonDesc from this object
     * @return QtPaintButtonDesc pointer or 0
     */
    virtual QtPaintButtonDesc* button();

    QSize m_size;
};


/**
 * This class implements a generic item description
 * @short Generic item description (base class)
 */
class QtPaintButtonDesc : public QtPaintItemDesc
{
    YCLASS(QtPaintButtonDesc,QtPaintItemDesc)
public:
    /**
     * Constructor
     * @param name Object name
     */
    inline QtPaintButtonDesc(const char* name = 0)
	: QtPaintItemDesc(name), m_params(""), m_iconSize(16,16)
	{ m_size = QSize(16,16); }

    /**
     * Get a QtPaintButtonDesc from this object
     * @return QtPaintButtonDesc pointer
     */
    virtual QtPaintButtonDesc* button();

    /**
     * Find a button in a list
     * @param list List to search in
     * @param name Button name
     * @param create True (default) to create if not found
     * @return QtPaintButtonDesc pointer or 0 if found and not a button
     */
    static QtPaintButtonDesc* find(ObjList& list, const String& name,
	bool create = true);

    NamedList m_params;
    QSize m_iconSize;
};


/**
 * This class implements an item to be painted
 * @short An item to be painted
 */
class QtPaintItem : public RefObject
{
    YCLASS(QtPaintItem,GenObject)
public:
    /**
     * Constructor
     * @param name Object name
     * @param size Object size
     */
    inline QtPaintItem(const char* name, QSize size)
	: m_enabled(true), m_hover(false), m_pressed(false),
	m_size(size), m_name(name)
	{}

    /**
     * Retrieve the item name
     * @return Item name
     */
    inline const String& name() const
	{ return m_name; }

    /**
     * Retrieve item pressed state
     * @return Item pressed state
     */
    inline bool pressed() const
	{ return m_pressed; }

    /**
     * Retrieve item size
     * @return Item size
     */
    inline const QSize& size() const
	{ return m_size; }

    /**
     * Retrieve the rectangle this item is drawn in
     * @return Item display rectangle
     */
    inline const QRect& displayRect() const
	{ return m_displayRect; }

    /**
     * Retrieve the item action
     * @return Item action
     */
    inline const String& action() const
	{ return m_action; }

    /**
     * Set hover state
     * @param on Hover state
     * @return True if hover state changed
     */
    virtual bool setHover(bool on);

    /**
     * Set pressed state
     * @param on Pressed state
     * @return True if pressed state changed
     */
    virtual bool setPressed(bool on);

    /**
     * Draw the item
     * @param painter Painter used to draw
     * @param rect Rectangle to paint in
     */
    virtual void draw(QPainter* painter, const QRect& rect) = 0;

    /**
     * Retrieve the item name
     * @return Item name
     */
    virtual const String& toString() const;

protected:
    bool m_enabled;
    bool m_hover;
    bool m_pressed;
    String m_action;
    QSize m_size;
    QRect m_displayRect;

private:
    String m_name;
};


/**
 * This class implements an item to be painted
 * @short An item to be painted
 */
class QtPaintButton : public QtPaintItem
{
    YCLASS(QtPaintButton,QtPaintItem)
public:
    /**
     * Constructor
     * @param desc Button description
     */
    QtPaintButton(QtPaintButtonDesc& desc);

    /**
     * Load button images
     * @param params Parameters list
     */
    void loadImages(const NamedList& params);

    /**
     * Set hover state
     * @param on Hover state
     * @True if hover state changed
     */
    virtual bool setHover(bool on);

    /**
     * Set pressed state
     * @param on Pressed state
     * @return True if pressed state changed
     */
    virtual bool setPressed(bool on);

    /**
     * Draw the item
     * @param painter Painter used to draw
     * @param rect Rectangle to paint in
     */
    virtual void draw(QPainter* painter, const QRect& rect);

protected:
    // Load an image, adjust its size
    bool loadImage(QPixmap& pixmap, const NamedList& params, const String& param);
    // Update state options
    void updateOptState();

    QPixmap m_normalImage;
    QPixmap m_hoverImage;
    QPixmap m_pressedImage;
    QPixmap* m_image;
    QSize m_iconSize;
    QSize m_iconOffset;                       // Draw icon offset
};


/**
 * This class implements a list of items to be painted
 * @short Custom item delegate
 */
class QtPaintItems : public QtPaintItem
{
    YCLASS(QtPaintItems,QtPaintItem)
public:
    /**
     * Constructor
     * @param name Object name
     */
    inline QtPaintItems(const char* name = 0)
	: QtPaintItem(name,QSize(0,0)),
	m_margins(10,0,6,0), m_itemSpace(4),
	m_lastItemHover(0)
	{}

    /**
     * Add an item from description
     * @param desc Item description
     */
    void append(QtPaintItemDesc& desc);

    /**
     * Calculate area needed to paint.
     * This method should be called after all items are set
     */
    void itemsAdded();

    /**
     * Set hover. Update item at position
     * @param pos Position to check
     * @return True if state changed (needs repaint)
     */
    bool setHover(const QPoint& pos);

    /**
     * Set hover state
     * @param on Hover state
     * @return True if hover state changed
     */
    virtual bool setHover(bool on);

    /**
     * Mouse pressed/released. Update item at position
     * @param on Pressed state
     * @param pos Position to check
     * @param action Pointer to action to be set on mouse release
     * @return True if state changed (needs repaint)
     */
    bool mousePressed(bool on, const QPoint& pos, String* action = 0);

    /**
     * Set pressed state
     * @param on Pressed state
     * @return True if pressed state changed
     */
    virtual bool setPressed(bool on);

    /**
     * Draw items
     * @param painter Painter used to draw
     * @param rect Rect to paint in
     */
    virtual void draw(QPainter* painter, const QRect& rect);

protected:
    ObjList m_items;
    QRect m_margins;
    int m_itemSpace;
    QtPaintItem* m_lastItemHover;        // Last item we handle mouse hover for
};


/**
 * This class implements a custom item delegate
 * @short Custom item delegate
 */
class QtItemDelegate : public QItemDelegate, public String
{
    YCLASS(QtItemDelegate,String)
    Q_CLASSINFO("QtItemDelegate","Yate")
    Q_OBJECT
public:
    QtItemDelegate(QObject* parent, const NamedList& params = NamedList::empty());
    virtual void paint(QPainter* painter, const QStyleOptionViewItem& option,
	const QModelIndex& index) const;
    inline QList<int>& columns()
	{ return m_columns; }
    inline int roleDisplayText() const
	{ return m_roleDisplayText; }
    inline int roleImage() const
	{ return m_roleImage; }
    // Update column position from column names.
    // 'cNames' must be the column names in their order, starting from 0
    void updateColumns(QStringList& cNames);
    // Build a list of delegates
    static QList<QAbstractItemDelegate*> buildDelegates(QObject* parent, const NamedList& params,
	const NamedList* common = 0, const String& prefix = "itemdelegate");
    // Build a delegate
    static QAbstractItemDelegate* build(QObject* parent, const String& cls, NamedList& params);
protected:
    // Retrieve display text for a given index
    virtual QString getDisplayText(const QStyleOptionViewItem& opt,
	const QModelIndex& index) const;
    // Inherited methods
    virtual void drawBackground(QPainter* painter, const QStyleOptionViewItem& opt,
	const QModelIndex& index) const;
    virtual void drawDecoration(QPainter* painter, const QStyleOptionViewItem& opt,
	const QRect& rect, const QPixmap& pixmap) const;
    virtual void drawFocus(QPainter* painter, const QStyleOptionViewItem& opt,
	const QRect& rect) const;
    virtual QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
	const QModelIndex& index) const;
    // Apply item margins
    void applyMargins(QRect& dest, const QRect& src, bool inc) const;

    bool m_drawFocus;                    // Draw focus
    int m_roleDisplayText;               // Item display role to handle
    int m_roleImage;                     // Item role containing image file name
    int m_roleBackground;                // Item background role to handle
    int m_roleMargins;                   // Item internal margins role to handle
    int m_roleQtDrawItems;               // Item draw extra role to handle
    QStringList m_columnsStr;            // Column names this delegate should be set for
    QStringList m_editableColsStr;       // List of editable column names
    QList<int> m_editableCols;           // List of editable columns
    QList<int> m_columns;                // List of editable columns
};


/**
 * This class implements a custom item delegate used to display HTML texts
 * @short Custom HTML item delegate
 */
class QtHtmlItemDelegate : public QtItemDelegate
{
    YCLASS(QtHtmlItemDelegate,QtItemDelegate)
    Q_CLASSINFO("QtHtmlItemDelegate","Yate")
    Q_OBJECT
public:
    QtHtmlItemDelegate(QObject* parent, const NamedList& params = NamedList::empty())
	: QtItemDelegate(parent,params)
	{}
protected:
    virtual void drawDisplay(QPainter* painter, const QStyleOptionViewItem& opt,
	const QRect& rect, const QString& text) const;
};

}; // anonymous namespace

#endif // __CUSTOMTREE_H

/* vi: set ts=8 sw=4 sts=4 noet: */
