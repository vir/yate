/**
 * customtable.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * A custom table
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

#ifndef __CUSTOMTABLE_H
#define __CUSTOMTABLE_H

#include <qt4client.h>

using namespace TelEngine;
namespace { // anonymous

class CustomTable : public QtTable
{
    YCLASS(CustomTable,QtTable)
    Q_CLASSINFO("CustomTable","Yate")
    Q_OBJECT
    Q_PROPERTY(QStringList _yate_save_props READ saveProps WRITE setSaveProps(QStringList))
    Q_PROPERTY(bool _yate_notifyitemchanged READ getNotifyItemChanged WRITE setNotifyItemChanged(bool))
    Q_PROPERTY(bool _yate_horizontalheader READ getHHeader WRITE setHHeader(bool))
    Q_PROPERTY(bool _yate_notifyonenterpressed READ enterPressNotify WRITE setEnterPressNotify(bool))
    Q_PROPERTY(int _yate_rowheight READ getRowHeight WRITE setRowHeight(int))
    Q_PROPERTY(QString _yate_col_widths READ getColWidths WRITE setColWidths(QString))
    Q_PROPERTY(QString _yate_sorting READ getSorting WRITE setSorting(QString))
public:
    /**
     * Table item data roles
     */
    enum CustomRoles {
	ColumnId = Qt::UserRole + 1,             // Column id
	ColumnItemCheckable = Qt::UserRole + 2,  // Column items are checkable
    };

    /**
     * Constructor
     * @param name The name of the table
     * @param params Parameters for building the table
     * @param parent Optional parent
     */
    CustomTable(const char* name, const NamedList& params, QWidget* parent = 0);

    /**
     * Destructor
     */
    ~CustomTable();

    /**
     * Check if the table has a filter set
     * @return True if a filter is set
     */
    inline bool hasFilter() const
	{ return 0 != m_filterBy.count() && m_filterValue.length(); }

    /**
     * Function for setting the properties of the table
     * @param params List that contains the properties to be set and their values
     * @return True if it has succeeded, false if it hasn't
     */
    virtual bool setParams(const NamedList& params);

    /**
     * Obtain all the entries that the table contains
     * @param items List to be filled with all the entries the table contains
     * @return True if there are elements, false if the table is empty
     */
    virtual bool getOptions(NamedList& items);

    /**
     * Add a new entry to the table
     * @param item The new entry's object name
     * @param data The parameters for building the new entry
     * @param asStart True if the entry is to be inserted at the start of
     *   the table, false if it is to be appended
     * @return True if the entry has been added, false otherwise
     */
    virtual bool addTableRow(const String& item, const NamedList* data = 0,
	bool atStart = false);

    /**
     * Add or set one or more table row(s). Screen update is locked while changing the table.
     * Each data list element is a NamedPointer carrying a NamedList with item parameters.
     * The name of an element is the item to update.
     * Set element's value to boolean value 'true' to add a new item if not found
     *  or 'false' to set an existing one. Set it to empty string to delete the item
     * @param data The list of items to add/set/delete
     * @param atStart True to add new items at start, false to add them to the end
     * @return True if the operation was successfull
     */
    virtual bool updateTableRows(const NamedList* data, bool atStart = false);

    /**
     * Delete an entry from the table
     * @param item Name of the object to be deleted
     * @return True if the entry has been deleted, false otherwise
     */
    virtual bool delTableRow(const String& item);

    /**
     * Set/change the properties of a table entry
     * @param item Name of the entry for which the properties will be set
     * @param data List of properties to be set and their values
     * @return True if the entry has been found and set, false if the entry hasn't been found
     */
    virtual bool setTableRow(const String& item, const NamedList* data);

    /** Get the values of requested properties for an entry
     * @param item Name of the searched entry
     * @param data List of the properties for which the value is requested.
     *  It will be filled wiht the properties' values
     * @return True if the entry is found and the list filled,
     *  false if the entry is not found
     */
    virtual bool getTableRow(const String& item, NamedList* data = 0);

    /**
     * Delete all table content
     * @return True if it succeeds
     */
    virtual bool clearTable();

    /**
     * Set the selected entry
     * @param item String containing the new selection
     * @return True if the operation was successfull
     */
    virtual bool setSelect(const String& item);

    /**
     * Obtain the selected entry
     * @param item String in which the selected entry name is to be returned
     * @return True if something is selected, false otherwise
     */
    virtual bool getSelect(String& item);

    /**
     * Retrieve the 0 based index of the current item
     * @return The index of the current item (-1 on error or container empty)
     */
    virtual int currentItemIndex()
	{ return QTableWidget::currentRow(); }

    /**
     * Retrieve the number of items in container
     * @return The number of items in container (-1 on error)
     */
    virtual int itemCount()
	{ return QTableWidget::rowCount(); }

    /**
     * Find a table row by its item id
     * @param item Item name to find
     * @return The row or -1 if not found
     */
    int getRow(const String& item);

    /**
     * Find a table row id by its row index
     * @param item Item id to fill
     * @param row Table row
     * @return True if the row item was found
     */
    bool getId(String& item, int row);

    /**
     * Find a table column id by its column index
     * @param id Column id to fill
     * @param checkable Column checkable flag
     * @param row Table row
     * @return QTableWidgetItem pointer or 0 if not found
     */
    QTableWidgetItem* getColumnId(String& id, bool& checkable, int col);

    /**
     * Find a column by its label. Return -1 if not found
     * @param text Column label text to find
     * @param hidden True to find a hidden column (search by 'hidden:' prefix)
     * @param caseInsensitive True to make a case insensitive comparison
     * @return The column index or -1 if not found
     */
    int getColumn(const QString& text, bool hidden = false, bool caseInsensitive = true);

    /**
     * Find a column by its label. Return -1 if not found
     * @param text Column label text to find
     * @param hidden True to find a hidden column (search by 'hidden:' prefix)
     * @param caseInsensitive True to make a case insensitive comparison
     * @return The column index or -1 if not found
     */
    inline int getColumn(const char* text, bool hidden = false, bool caseInsensitive = true)
	{ return getColumn(QtClient::setUtf8(text),hidden,caseInsensitive); }

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
     * Check if the horizontal header should be visible
     * @return True if the horizontal header should be visible
     */
    bool getHHeader()
	{ return m_horzHeader; }

    /**
     * Show/hide the horizontal header
     * @param on True to show the horizontal header, false to hide it
     */
    void setHHeader(bool on) {
	    m_horzHeader = on;
	    QHeaderView* h = horizontalHeader();
	    if (h)
		h->setVisible(on);
	}

    /**
     * Check if enter key press action is active. Does nothing
     * This method is here to stop MOC compiler complaining about missing READ accessor function
     * @return False
     */
    bool enterPressNotify()
	{ return false; }

    /**
     * (de)activate enter key press action
     * @param value True to activate the enter key press action, false to disable it
     */
    void setEnterPressNotify(bool value);

    /**
     * Retrieve the table's default row height
     * @return Table's default row height
     */
    int getRowHeight()
	{ return m_rowHeight; }

    /**
     * Set the table's default row height
     * @param value Table's new default row height
     */
    void setRowHeight(int value)
	{ m_rowHeight = value; }

    /**
     * Retrieve table columns widths
     * @return Comma separated list of columns widths
     */
    QString getColWidths();

    /**
     * Set the table columns widths string
     * @param value Comma separated list of columns widths
     */
    void setColWidths(QString value);

    /**
     * Retrieve table sorting
     * @return Table sorting string
     */
    QString getSorting();

    /**
     * Set the table sorting
     * @param value Table sorting value
     */
    void setSorting(QString value);

protected:
    /**
     * Setup a row
     * @param row An existing row index
     * @param data Row parameters
     * @param item Set the row's id if not empty
     * @return True on success
     */
    virtual bool setRow(int row, const NamedList* data,
	const String& item = String::empty());

    /**
     * Handle item cell content changes
     * @param row Item row
     * @param col Item column
     */
    virtual void onCellChanged(int row, int col);

    /**
     * Catch a context menu event and show the context menu
     * @param e Context menu event
     */
    virtual void contextMenuEvent(QContextMenuEvent* e);

    /**
     * Catch a mouse press event
     * Disable selection change signal on right button events
     * @param event Mouse press event
     */
    virtual void mousePressEvent(QMouseEvent* event);

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
     * Handle item cell changed
     */
    void itemCellChanged(int row, int col)
	{ onCellChanged(row,col); }

    /**
     * Slot for triggered signals received from actions added to the table
     */
    void actionTriggered();

private:
    /**
     * Set filter (hide not matching items)
     * @param value Filter value
     */
    void setFilter(const String& value = String::empty());

    /**
     * Check if the current filter matches a row. Show it if matched, hide it otherwise.
     * @param row The row to check
     * @param col The column containing the widget to check
     * @return True if the row visibility changed
     */
    bool updateFilter(int row, int col);

    /**
     * Check if the current filter matches a row
     * @param row The row to check
     * @param col The column containing the widget to check
     * @return True if match
     */
    bool rowFilterMatch(int row, int col);

    int m_rowHeight;
    bool m_horzHeader;                   // Show/hide the horizontal header
    bool m_notifyItemChanged;            // Notify 'listitemchanged' action
    bool m_notifySelChgOnRClick;         // Notify selection changed on mouse right button click
    QMenu* m_contextMenu;
    QString m_enterKeyActionName;        // The name of the Enter key pressed action
    // Filter
    QStringList m_filterBy;              // List of cell widget children name whose text is used to filter
                                         //  the table rows
    QString m_filterValue;               // The filter value
    // Notifications
    bool m_changing;                     // Content is changing from client (not from user):
                                         //  avoid notifications
};

}; // anonymous namespace

#endif // __CUSTOMTABLE_H

/* vi: set ts=8 sw=4 sts=4 noet: */
