/**
 * customtable.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Custom table implementation
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2010-2014 Null Team
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

#include "customtable.h"

using namespace TelEngine;
namespace { // anonymous

// The factory
class CustomTableFactory : public UIFactory
{
public:
    inline CustomTableFactory(const char* name = "CustomTableFactory")
	: UIFactory(name)
	{ m_types.append(new String("CustomTable")); }
    virtual void* create(const String& type, const char* name, NamedList* params = 0);
};

// Utility class used to disable/enable a table sorting and widget update flag
class SafeWidget
{
public:
    SafeWidget(QTableWidget* table)
	: m_widget(table), m_table(0) {
	    if (m_widget)
		m_widget->setUpdatesEnabled(false);
	    if (table && table->isSortingEnabled()) {
		m_table = table;
		m_table->setSortingEnabled(false);
	    }
	}
    ~SafeWidget()
	{ drop(); }
    inline void drop() {
	    if (m_table)
		m_table->setSortingEnabled(true);
	    if (m_widget)
		m_widget->setUpdatesEnabled(true);
	    m_widget = 0;
	    m_table = 0;
	}
private:
    QWidget* m_widget;
    QTableWidget* m_table;
};

static CustomTableFactory s_factory;

static inline const String& objListItem(ObjList* list, int index)
{
    GenObject* gen = list ? (*list)[index] : 0;
    return gen ? gen->toString() : String::empty();
}


/*
 * CustomTable
 */
// Constructor for a custom table
CustomTable::CustomTable(const char *name, const NamedList& params, QWidget* parent)
    : QtTable(name,parent),
    m_rowHeight(0), m_horzHeader(true),
    m_notifyItemChanged(false), m_notifySelChgOnRClick(true),
    m_contextMenu(0), m_changing(false)
{
    // Build properties
    QtClient::buildProps(this,params["buildprops"]);
    // Set horizontal header
    QHeaderView* h = horizontalHeader();
    if (h)
	h->setHighlightSections(false);
    ObjList* cols = params["hheader_columns"].split(',',false);
    ObjList* title = params["hheader_columns_title"].split(',',true);
    ObjList* check = params["hheader_columns_check"].split(',',false);
    ObjList* size = params["hheader_columns_size"].split(',',true);
    ObjList* resize = params["hheader_columns_resize"].split(',',true);
    ObjList* emptyTitle = params["hheader_columns_allowemptytitle"].split(',',true);
    int n = cols->count();
    setColumnCount(n);
    for (int i = 0; i < n; i++) {
	String id = objListItem(cols,i);
	String text = objListItem(title,i);
	if (!text) {
	    String tmp = id;
	    if (!emptyTitle->find(tmp.toLower()))
		text = id;
	}
	QTableWidgetItem* it = new QTableWidgetItem(QtClient::setUtf8(text));
	id.toLower();
	it->setData(ColumnId,QVariant(QtClient::setUtf8(id)));
	if (check->find(id))
	    it->setData(ColumnItemCheckable,QVariant(true));
	setHorizontalHeaderItem(i,it);
	if (!h)
	    continue;
	// Set column width
	int w = objListItem(size,i).toInteger();
	if (w > 0)
	    h->resizeSection(i,w);
	// Set column resize mode
	const String& resizeMode = objListItem(resize,i);
	if (resizeMode == "fixed")
	    h->setResizeMode(i,QHeaderView::Fixed);
	else if (resizeMode == "stretch")
	    h->setResizeMode(i,QHeaderView::Stretch);
	else if (resizeMode == "contents")
	    h->setResizeMode(i,QHeaderView::ResizeToContents);
	else
	    h->setResizeMode(i,QHeaderView::Interactive);
    }
    TelEngine::destruct(cols);
    TelEngine::destruct(title);
    TelEngine::destruct(check);
    TelEngine::destruct(size);
    TelEngine::destruct(resize);
    TelEngine::destruct(emptyTitle);
    // Init properties
    m_saveProps << "_yate_col_widths";
    m_saveProps << "_yate_sorting";
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    // Connect signals
    QtClient::connectObjects(this,SIGNAL(cellChanged(int,int)),this,SLOT(itemCellChanged(int,int)));
    // Apply parameters
    setParams(params);
}

CustomTable::~CustomTable()
{
}

bool CustomTable::setParams(const NamedList& params)
{
    SafeWidget tbl(this);
    QtUIWidget::setParams(params);
    unsigned int n = params.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* param = params.getParam(i);
	if (!param)
	    continue;
	if (param->name() == "filtervalue")
	    setFilter(*param);
	else if (param->name() == "dynamiccellclicked")
	    setProperty("dynamicCellClicked",QVariant(QString(*param)));
	else if (param->name() == "dynamicnoitemselchanged")
	    setProperty("dynamicNoItemSelChanged",QVariant(QString(*param)));
	else if (param->name().startsWith("property:")) {
	    String prop = param->name().substr(9);
	    QWidget* target = this;
	    if (prop.startSkip("hheader:",false))
		target = horizontalHeader();
	    if (target)
		QtClient::setProperty(target,prop,*param);
	}
	else if (param->name() == "menu") {
	    // Re-build the context menu
	    if (m_contextMenu) {
		QtClient::deleteLater(m_contextMenu);
		m_contextMenu = 0;
	    }
	    NamedList* menu = static_cast<NamedList*>(param->getObject(YATOM("NamedList")));
	    if (menu) {
		// Get parent window receiving menu events
		QtWindow* wnd = static_cast<QtWindow*>(window());
		if (wnd)
		    m_contextMenu = QtClient::buildMenu(*menu,*menu,wnd,SLOT(action()),
			SLOT(toggled(bool)),this);
	    }
	}
	else if (param->name() == "notifyselchgonrightclick")
	    m_notifySelChgOnRClick = param->toBoolean(m_notifySelChgOnRClick);
	else if (param->name() == "filterby") {
	    setFilter();
	    m_filterBy.clear();
	    ObjList* list = param->split(',',false);
	    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
		String* s = static_cast<String*>(o->get());
		m_filterBy.append(QtClient::setUtf8(s->toLower()));
	    }
	    TelEngine::destruct(list);
	}
    }
    tbl.drop();
    return true;
}

bool CustomTable::getOptions(NamedList& items)
{
    int n = rowCount();
    for (int i = 0; i < n; i++) {
	String id;
	if (getId(id,i) && id)
	    items.addParam(id,"");
    }
    return true;
}

bool CustomTable::addTableRow(const String& item, const NamedList* data, bool atStart)
{
    DDebug(ClientDriver::self(),DebugAll,"CustomTable(%s)::addTableRow(%s,%p,%u)",
	name().c_str(),item.c_str(),data,atStart);
    SafeWidget tbl(this);
    int row = atStart ? 0 : rowCount();
    insertRow(row);
    if (setRow(row,data,item))
	return true;
    removeRow(row);
    return false;
}

// Add or set one or more table row(s). Screen update is locked while changing the table.
// Each data list element is a NamedPointer carrying a NamedList with item parameters.
// The name of an element is the item to update.
// Set element's value to boolean value 'true' to add a new item if not found
//  or 'false' to set an existing one. Set it to empty string to delete the item
bool CustomTable::updateTableRows(const NamedList* data, bool atStart)
{
    if (!data)
	return true;
    DDebug(ClientDriver::self(),DebugAll,"CustomTable(%s)::updateTableRows(%p,%u)",
	name().c_str(),data,atStart);
    // Remember selected item
    bool ok = true;
    SafeWidget tbl(this);
    unsigned int n = data->length();
    ObjList add;
    // Delete and update rows
    for (unsigned int i = 0; i < n; i++) {
	if (Client::exiting())
	    break;
	// Get item and the list of parameters
	NamedString* ns = data->getParam(i);
	if (!ns)
	    continue;
	// Delete ?
	if (ns->null()) {
	    int row = getRow(ns->name());
	    if (row >= 0)
		removeRow(row);
	    else
		ok = false;
	    continue;
	}
	// Set item or postpone add
	int row = getRow(ns->name());
	if (row >= 0)
	    setRow(row,YOBJECT(NamedList,ns));
	else if (ns->toBoolean())
	    add.append(ns)->setDelete(false);
	else
	    ok = false;
    }
    n = add.count();
    if (n) {
	int row = rowCount();
	if (row < 0)
	    row = 0;
	// Append if not requested to insert at start or table is empty
	if (!(atStart && row))
	    setRowCount(row + n);
	else {
	    for (unsigned int i = 0; i < n; i++)
		insertRow(0);
	}
	for (ObjList* o = add.skipNull(); o; row++, o = o->skipNext()) {
	    NamedString* ns = static_cast<NamedString*>(o->get());
	    if (!setRow(row,YOBJECT(NamedList,ns),ns->name()))
		ok = false;
	}
    }
    return ok;
}

bool CustomTable::delTableRow(const String& item)
{
    SafeWidget tbl(this);
    int row = getRow(item);
    DDebug(ClientDriver::self(),DebugAll,"CustomTable(%s)::delTableRow(%s) found=%d",
	name().c_str(),item.c_str(),row);
    if (row < 0)
	return false;
    removeRow(row);
    return true;
}

bool CustomTable::setTableRow(const String& item, const NamedList* data)
{
    SafeWidget tbl(this);
    int row = getRow(item);
    DDebug(ClientDriver::self(),DebugAll,"CustomTable(%s)::setTableRow(%s,%p) found=%d",
	name().c_str(),item.c_str(),data,row);
    if (row < 0)
	return false;
    return setRow(row,data);
}

bool CustomTable::getTableRow(const String& item, NamedList* data)
{
    int row = getRow(item);
    DDebug(ClientDriver::self(),DebugAll,"CustomTable(%s)::getTableRow(%s,%p) found=%d",
	name().c_str(),item.c_str(),data,row);
    if (row < 0)
	return false;
    if (!data)
	return true;
    int n = columnCount();
    for (int i = 1; i < n; i++) {
	String name;
	bool checkable = false;
	QTableWidgetItem* h = getColumnId(name,checkable,i);
	if (!(h && name))
	    continue;
	QTableWidgetItem* it = QTableWidget::item(row,i);
	if (!it)
	    continue;
	NamedString* ns = new NamedString(name);
	QtClient::getUtf8(*ns,it->text());
	data->setParam(ns);
	if (checkable)
	    data->setParam("check:" + name,String::boolText(it->checkState() == Qt::Checked));
    }
    return true;
}

bool CustomTable::clearTable()
{
    setRowCount(0);
    return true;
}

// Set the selected entry
bool CustomTable::setSelect(const String& item)
{
    if (!item)
	return true;
    int row = getRow(item);
    DDebug(ClientDriver::self(),DebugAll,"CustomTable(%s)::setSelect(%s) found=%d",
	name().c_str(),item.c_str(),row);
    if (row < 0)
	return false;
    setCurrentCell(row,1);
    return true;
}

bool CustomTable::getSelect(String& item)
{
    int row = currentRow();
    QTableWidgetItem* it = 0;
    if (row >= 0) {
	it = QTableWidget::item(row,0);
	if (it)
	    QtClient::getUtf8(item,it->text());
    }
    DDebug(ClientDriver::self(),DebugAll,"CustomTable(%s)::getSelect() found=(%d,%s)",
	name().c_str(),row,item.c_str());
    return it != 0;
}

// Find a table row by its item id
int CustomTable::getRow(const String& item)
{
    const QString tmp = QtClient::setUtf8(item);
    for (int i = 0; i < rowCount(); i++) {
	QTableWidgetItem* it = this->item(i,0);
	if (it && it->text() == tmp)
	    return i;
    }
    return -1;
}

// Find a table row id by its row index
bool CustomTable::getId(String& item, int row)
{
    QTableWidgetItem* it = this->item(row,0);
    if (it)
	QtClient::getUtf8(item,it->text());
    return it != 0;
}

// Find a column by its label. Return -1 if not found
QTableWidgetItem* CustomTable::getColumnId(String& id, bool& checkable, int col)
{
    QTableWidgetItem* it = horizontalHeaderItem(col);
    if (!it)
	return 0;
    QVariant var = it->data(ColumnId);
    if (var.type() == QVariant::String)
	QtClient::getUtf8(id,var.toString());
    else {
	QtClient::getUtf8(id,it->text());
	id.toLower();
    }
    var = it->data(ColumnItemCheckable);
    checkable = var.toBool();
    return it;
}

// Find a column by its label. Return -1 if not found
int CustomTable::getColumn(const QString& name, bool hidden, bool caseInsensitive)
{
    static QString ht("hidden:");
    QString what = name;
    if (hidden)
	what.insert(0,ht);
    Qt::CaseSensitivity cs = caseInsensitive ? Qt::CaseInsensitive : Qt::CaseSensitive;
    int n = columnCount();
    for (int i = 0; i < n; i++) {
	QTableWidgetItem* it = horizontalHeaderItem(i);
	if (!it)
	    continue;
	QVariant var = it->data(ColumnId);
	if (var.type() == QVariant::String) {
	    if (0 == var.toString().compare(what,cs))
		return i;
	}
	else if (0 == it->text().compare(what,cs))
	    return i;
    }
    return -1;
}

// (de)activate enter key press action
void CustomTable::setEnterPressNotify(bool value)
{
    QAction* act = qFindChild<QAction*>(this,m_enterKeyActionName);
    if (act) {
	if (!value) {
	    QWidget::removeAction(act);
	    QtClient::deleteLater(act);
	}
	return;
    }
    if (!value)
	return;
    act = new QAction("",this);
    act->setObjectName(m_enterKeyActionName);
    act->setShortcut(QKeySequence(Qt::Key_Return));
    act->setShortcutContext(Qt::WidgetShortcut);
    act->setProperty("_yate_autoconnect",QVariant(false));
    QWidget::addAction(act);
    QtClient::connectObjects(act,SIGNAL(triggered()),this,SLOT(actionTriggered()));
}

// Retrieve table columns widths
QString CustomTable::getColWidths()
{
    String widths;
    int n = columnCount();
    for (int i = 0; i < n; i++)
	widths.append(String(columnWidth(i)),",",true);
    return QtClient::setUtf8(widths);
}

// Set the table columns widths string
void CustomTable::setColWidths(QString value)
{
    QHeaderView* hdr = horizontalHeader();
    bool skipLast = hdr && hdr->stretchLastSection();
    QStringList list = value.split(',');
    for (int i = 0; i < list.size(); i++) {
	if (skipLast && i == columnCount() - 1)
	    break;
	bool ok = true;
	int w = list[i].toInt(&ok);
	if (ok && w >= 0)
	    setColumnWidth(i,w);
    }
}

// Retrieve table sorting
QString CustomTable::getSorting()
{
    String sorting;
    if (isSortingEnabled()) {
	QHeaderView* h = horizontalHeader();
	int col = h ? h->sortIndicatorSection() : -1;
	if (col >= 0)
	    sorting << col << "," <<
		String::boolText(Qt::AscendingOrder == h->sortIndicatorOrder());
    }
    return QtClient::setUtf8(sorting);
}

// Set the table sorting
void CustomTable::setSorting(QString value)
{
    QStringList list = value.split(',');
    if (list.size() < 2)
	return;
    bool ok = true;
    int col = list[0].toInt(&ok);
    if (ok && col >= 0 && col < columnCount()) {
	String tmp;
	QtClient::getUtf8(tmp,list[1]);
	sortItems(col,tmp.toBoolean(true) ? Qt::AscendingOrder : Qt::DescendingOrder);
    }
}

// Setup a row
bool CustomTable::setRow(int row, const NamedList* data, const String& item)
{
    DDebug(ClientDriver::self(),DebugAll,"CustomTable(%s)::setRow(%d,%p,%s)",
	name().c_str(),row,data,item.c_str());
    m_changing = true;
    int n = columnCount();
    // First init
    if (item) {
	// Set row id
	setItem(row,0,new QTableWidgetItem(QtClient::setUtf8(item)));
	// Set row height
	if (m_rowHeight > 0)
	    QTableWidget::setRowHeight(row,m_rowHeight);
	// Set checkable columns
	for (int i = 1; i < n; i++) {
	    String name;
	    bool checkable = false;
	    getColumnId(name,checkable,i);
	    if (!checkable)
		continue;
	    QTableWidgetItem* it = QTableWidget::item(row,i);
	    if (!it) {
		it = new QTableWidgetItem;
		setItem(row,i,it);
	    }
	    it->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
	    it->setCheckState(Qt::Unchecked);
	}
    }
    if (!data) {
	m_changing = false;
	return true;
    }
    for (int i = 1; i < n; i++) {
	String name;
	bool checkable = false;
	getColumnId(name,checkable,i);
	if (!name)
	    continue;
	String* text = data->getParam(name);
	String* img = data->getParam(name + "_image");
	String* check = checkable ? data->getParam("check:" + name) : 0;
	if (!(text || img || check))
	    continue;
	QTableWidgetItem* it = QTableWidget::item(row,i);
	if (!it) {
	    it = new QTableWidgetItem;
	    setItem(row,i,it);
	    if (!checkable)
		it->setFlags(it->flags() & ~Qt::ItemFlags(Qt::ItemIsUserCheckable));
	    else {
		it->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
		it->setCheckState(Qt::Unchecked);
	    }
	}
	if (text)
	    it->setText(QtClient::setUtf8(*text));
	if (check)
	    it->setCheckState(check->toBoolean() ? Qt::Checked : Qt::Unchecked);
	if (img)
	    it->setIcon(QIcon(QtClient::setUtf8(*img)));
    }
    m_changing = false;
    return true;
}

// Handle item cell content changes
void CustomTable::onCellChanged(int row, int col)
{
    if (m_changing || row < 0 || !m_notifyItemChanged)
	return;
    String item;
    getId(item,row);
    if (item)
	triggerAction(item,"listitemchanged",this);
}

void CustomTable::contextMenuEvent(QContextMenuEvent* e)
{
    int yMax = rowCount() * rowHeight(0);
    if (yMax < e->y())
	return;
    if (m_contextMenu)
	m_contextMenu->exec(e->globalPos());
}

// Catch a mouse press event
// Disable selection change signal on right button events
void CustomTable::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton && !m_notifySelChgOnRClick) {
	int row = rowAt(event->y());
	if (row >= 0 && row != currentRow()) {
	    // Disconnect and re-connect only if connected
	    QtWindow* wnd = 0;
	    QVariant var = property("dynamicNoItemSelChanged");
	    if (!var.toBool())
		wnd = QtClient::parentWindow(this);
	    if (wnd)
		disconnect(this,SIGNAL(itemSelectionChanged()),
		    wnd,SLOT(selectionChanged()));
	    setCurrentCell(row,1);
	    if (wnd)
		QtClient::connectObjects(this,SIGNAL(itemSelectionChanged()),
		    wnd,SLOT(selectionChanged()));
	    event->accept();
	}
	return;
    }
    QTableWidget::mousePressEvent(event);
}

// Slot for triggered signals received from actions added to the table
void CustomTable::actionTriggered()
{
    if (!sender() || currentRow() < 0)
	return;
    if (sender()->objectName() == m_enterKeyActionName)
	onAction(this);
}

// Set filter (hide not matching items)
void CustomTable::setFilter(const String& value)
{
    DDebug(ClientDriver::self(),DebugAll,"CustomTable(%s)::setFilter(%s)",
	name().c_str(),value.c_str());
    SafeWidget tbl(this);
    QString tmp = QtClient::setUtf8(value);
    if (tmp == m_filterValue)
	return;
    m_filterValue = tmp;
    // Match rows and show or hide them
    int rows = rowCount();
    int cols = columnCount();
    for (int row = 0; row < rows; row++)
	for (int col = 0; col < cols; col++)
	    if (updateFilter(row,col))
		break;
}

// Check if the current filter matches a row. Show it if matched, hide it otherwise.
bool CustomTable::updateFilter(int row, int col)
{
    bool hide = !rowFilterMatch(row,col);
    if (hide == isRowHidden(row))
	return false;
    setRowHidden(row,hide);
    return true;
}

// Check if the current filter matches a row
bool CustomTable::rowFilterMatch(int row, int col)
{
    for (int i = m_filterBy.size() - 1; i >= 0; i--) {
	QTableWidgetItem* hdr = horizontalHeaderItem(col);
	if (!hdr || hdr->text() != m_filterBy[i])
	    continue;
	QTableWidgetItem* it = item(row,col);
	if (it && it->text().contains(m_filterValue,Qt::CaseInsensitive))
	    return true;
    }
    return false;
}


/*
 * CustomTableFactory
 */
// Build CustomTable
void* CustomTableFactory::create(const String& type, const char* name, NamedList* params)
{
    if (!params)
	return 0;
    QWidget* parentWidget = 0;
    String* wndname = params->getParam("parentwindow");
    if (!TelEngine::null(wndname)) {
	String* wName = params->getParam("parentwidget");
	QtWindow* wnd = static_cast<QtWindow*>(Client::self()->getWindow(*wndname));
	if (wnd && !TelEngine::null(wName))
	    parentWidget = qFindChild<QWidget*>(wnd,QtClient::setUtf8(*wName));
    }
    if (type == "CustomTable")
	return new CustomTable(name,*params,parentWidget);
    return 0;
}

}; // anonymous namespace

#include "customtable.moc"

/* vi: set ts=8 sw=4 sts=4 noet: */
