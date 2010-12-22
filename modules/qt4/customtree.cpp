/**
 * customtree.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Custom QtTree based objects
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2010 Null Team
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

#include "customtree.h"

using namespace TelEngine;
namespace { // anonymous

// The factory
class CustomTreeFactory : public UIFactory
{
public:
    inline CustomTreeFactory(const char* name = "CustomTreeFactory")
	: UIFactory(name) {
	    m_types.append(new String("ContactList"));
	    m_types.append(new String("QtCustomTree"));
	}
    virtual void* create(const String& type, const char* name, NamedList* params = 0);
};

// Utility class used to disable/enable a tree sorting flag
// Disable tree sorting upon creation and enable it on destruction
// Objects of this class should be created in methods changing
//  tree content
class SafeTree
{
public:
    inline SafeTree(QTreeWidget* tree)
	: m_tree(tree), m_sorting(false) {
	    if (!tree)
		return;
	    m_tree->setUpdatesEnabled(false);
	    if (tree->isSortingEnabled()) {
		m_sorting = tree->isSortingEnabled();
		m_tree->setSortingEnabled(false);
	    }
	}
    inline ~SafeTree() {
	    if (!m_tree)
		return;
	    if (m_sorting)
		m_tree->setSortingEnabled(true);
	    m_tree->setUpdatesEnabled(true);
	}
private:
    QTreeWidget* m_tree;
    bool m_sorting;
};


static CustomTreeFactory s_factory;
static const String s_noGroupId(String(Time::secNow()) + "_" + MD5("Yate").hexDigest());
static const String s_offline("offline");


/*
 * QtTreeItem
 */
QtTreeItem::QtTreeItem(const char* id, int type, const char* text)
    : QTreeWidgetItem(type),
    NamedList(id)
{
    if (!TelEngine::null(text))
	setText(0,QtClient::setUtf8(text));
    XDebug(ClientDriver::self(),DebugAll,"QtTreeItem(%s) type=%d [%p]",id,type,this);
}

QtTreeItem::~QtTreeItem()
{
    XDebug(ClientDriver::self(),DebugAll,"~QtTreeItem(%s) type=%d [%p]",c_str(),type(),this);
}


/*
 * QtCustomTree
 */
QtCustomTree::QtCustomTree(const char* name, const NamedList& params, QWidget* parent)
    : QtTree(name,parent),
    m_menu(0),
    m_autoExpand(false),
    m_rowHeight(-1),
    m_itemPropsType("")
{
    // Build properties
    QtClient::buildProps(this,params["buildprops"]);
    // Add item props translation
    m_itemPropsType.addParam(String((int)QTreeWidgetItem::Type),"default");
    // Add item types
    unsigned int n = params.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = params.getParam(i);
	if (ns && ns->name() == "_yate_tree_additemtype" && ns->c_str())
	    m_itemPropsType.setParam(String(TypeCount + i),*ns);
    }
    setIndentation(0);
    setUniformRowHeights(false);
    QTreeWidget::setFrameShape(QFrame::NoFrame);
    QTreeWidgetItem* hdr = headerItem();
    if (hdr) {
	String* columns = params.getParam("columns");
	if (TelEngine::null(columns))
	    hdr->setHidden(true);
	else {
	    QHeaderView* header = QTreeView::header();
	    ObjList* id = columns->split(',',false);
	    setColumnCount(id->count());
	    int n = 0;
	    for (ObjList* o = id->skipNull(); o; o = o->skipNext(), n++) {
		String* name = static_cast<String*>(o->get());
		String pref("column." + *name);
		String* cap = params.getParam(pref + ".caption");
		if (!cap)
		    cap = name;
		int ww = params.getIntValue(pref + ".width");
		if (ww > 0)
		    setColumnWidth(n,ww);
		String* sizeMode = header ? params.getParam(pref + ".size") : 0;
		if (!TelEngine::null(sizeMode)) {
		    if (*sizeMode == "fixed")
			header->setResizeMode(n,QHeaderView::Fixed);
		    else if (*sizeMode == "fitcontent")
			header->setResizeMode(n,QHeaderView::ResizeToContents);
		    else if (*sizeMode == "stretch")
			header->setResizeMode(n,QHeaderView::Stretch);
		}
		hdr->setData(n,-1,QtClient::setUtf8(*name));
		hdr->setText(n,QtClient::setUtf8(*cap));
	    }
	    TelEngine::destruct(id);
	}
    }
    setParams(params);
    // Connect signals
    QtClient::connectObjects(this,SIGNAL(currentItemChanged(QTreeWidgetItem*,QTreeWidgetItem*)),
	this,SLOT(selectionChangedSlot(QTreeWidgetItem*,QTreeWidgetItem*)));
    QtClient::connectObjects(this,SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)),
	this,SLOT(itemDoubleClickedSlot(QTreeWidgetItem*,int)));
    QtClient::connectObjects(this,SIGNAL(itemExpanded(QTreeWidgetItem*)),
	this,SLOT(itemExpandedSlot(QTreeWidgetItem*)));
    QtClient::connectObjects(this,SIGNAL(itemCollapsed(QTreeWidgetItem*)),
	this,SLOT(itemCollapsedSlot(QTreeWidgetItem*)));
}

// Destructor
QtCustomTree::~QtCustomTree()
{
}

// Retrieve item type definition from [type:]value. Create if not found
QtUIWidgetItemProps* QtCustomTree::getItemProps(QString& in, String& value)
{
    String type;
    int pos = in.indexOf(':');
    if (pos >= 0) {
	QtClient::getUtf8(type,in.left(pos));
	QtClient::getUtf8(value,in.right(in.length() - pos - 1));
    }
    else
	QtClient::getUtf8(value,in);
    if (!type)
	type = itemPropsName(QTreeWidgetItem::Type);
    QtUIWidgetItemProps* p = QtUIWidget::getItemProps(type);
    if (!p) {
	p = new QtTreeItemProps(type);
	m_itemProps.append(p);
    }
    return p;
}

// Set params
bool QtCustomTree::setParams(const NamedList& params)
{
    bool ok = QtUIWidget::setParams(params);
    ok = QtUIWidget::setParams(this,params) && ok;
    buildMenu(m_menu,params.getParam("menu"));
    return ok;
}

// Retrieve an item
bool QtCustomTree::getTableRow(const String& item, NamedList* data)
{
    QtTreeItem* it = find(item);
    DDebug(ClientDriver::self(),DebugAll,"QtCustomTree(%s)::getTableRow(%s) found=%p",
	name().c_str(),item.c_str(),it);
    if (!it)
	return false;
    if (data)
	data->copyParams(*it);
    return true;
}

bool QtCustomTree::setTableRow(const String& item, const NamedList* data)
{
    DDebug(ClientDriver::self(),DebugAll,"QtCustomTree(%s)::setTableRow(%s,%p)",
	name().c_str(),item.c_str(),data);
    QtTreeItem* it = find(item);
    if (!it)
	return false;
    if (!data)
	return true;
    SafeTree tree(this);
    return updateItem(*it,*data);
}

// Add a new account or contact
bool QtCustomTree::addTableRow(const String& item, const NamedList* data, bool atStart)
{
    DDebug(ClientDriver::self(),DebugAll,"QtCustomTree(%s)::addTableRow(%s,%p,%u)",
	name().c_str(),item.c_str(),data,atStart);
    if (!data)
	return false;
    if (find(item))
	return false;
    SafeTree tree(this);
    QtTreeItem* parent = 0;
    int type = QTreeWidgetItem::Type;
    if (data) {
	type = itemType((*data)["item_type"]);
	const String& pName = (*data)["parent"];
	if (pName) {
	    parent = find(pName);
	    if (!parent) {
		Debug(ClientDriver::self(),DebugAll,
		    "QtCustomTree(%s)::addTableRow(%s,%p,%u) parent '%s' not found",
		    name().c_str(),item.c_str(),data,atStart,pName.c_str());
		return false;
	    }
	}
    }
    QtTreeItem* it = new QtTreeItem(item,type);
    if (data)
	it->copyParams(*data);
    if (addChild(it,atStart,parent))
	return !data || updateItem(*it,*it);
    TelEngine::destruct(it);
    return false;
}

// Remove an item from tree
bool QtCustomTree::delTableRow(const String& item)
{
    if (!item)
	return false;
    QtTreeItem* it = find(item);
    DDebug(ClientDriver::self(),DebugAll,"QtCustomTree(%s)::delTableRow(%s) found=%p",
	name().c_str(),item.c_str(),it);
    if (it) {
	QTreeWidgetItem* parent = it->parent();
	if (parent) {
	    parent->removeChild(it);
	    QtTreeItem* p = 0;
	    if (parent != invisibleRootItem())
		p = static_cast<QtTreeItem*>(parent);
	    itemRemoved(*it,p);
	}
	delete it;
    }
    return it != 0;
}

// Add, set or remove one or more items.
// Each data list element is a NamedPointer carrying a NamedList with item parameters.
// The name of an element is the item to update.
// Set element's value to boolean value 'true' to add a new item if not found
//  or 'false' to set an existing one. Set it to empty string to delete the item
bool QtCustomTree::updateTableRows(const NamedList* data, bool atStart)
{
    if (!data)
	return true;
    DDebug(ClientDriver::self(),DebugAll,"QtCustomTree(%s)::updateTableRows()",
	name().c_str());
    SafeTree tree(this);
    bool ok = false;
    unsigned int n = data->length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = data->getParam(i);
	if (!(ns && ns->name()))
	    continue;
	if (!ns->null()) {
	    NamedList* params = static_cast<NamedList*>(ns->getObject("NamedList"));
	    QtTreeItem* item = find(ns->name());
	    if (!params) {
		ok = (0 != item) || ok;
		continue;
	    }
	    if (item)
		ok = updateItem(*item,*params) || ok;
	    else if (ns->toBoolean())
		ok = addTableRow(ns->name(),params,atStart) || ok;
	}
	else
	    ok = delTableRow(ns->name()) || ok;
    }
    return ok;
}

// Retrieve the current selection
bool QtCustomTree::setSelect(const String& item)
{
    QtTreeItem* it = item ? find(item) : 0;
    DDebug(ClientDriver::self(),DebugAll,"QtCustomTree(%s)::setSelect(%s) found=%p",
	name().c_str(),item.c_str(),it);
    if (it)
	setCurrentItem(it);
    else if (item)
	setCurrentItem(0);
    return it || !item;
}

// Retrieve the current selection
bool QtCustomTree::getSelect(String& item)
{
    QList<QTreeWidgetItem*> list = selectedItems();
    bool ok = list.size() > 0 && list[0];
    DDebug(ClientDriver::self(),DebugAll,"QtCustomTree(%s)::getSelect(%s) found=%u",
	name().c_str(),item.c_str(),ok);
    if (ok)
	item = (static_cast<QtTreeItem*>(list[0]))->id();
    return ok;
}

// Remove all items from tree
bool QtCustomTree::clearTable()
{
    DDebug(ClientDriver::self(),DebugAll,"QtCustomTree(%s)::clearTable()",name().c_str());
    QTreeWidget::clear();
    return true;
}

// Retrieve the item type integer value from associated string (name)
int QtCustomTree::itemType(const String& name) const
{
    unsigned int n = m_itemPropsType.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = m_itemPropsType.getParam(i);
	if (ns && *ns == name)
	    return ns->name().toInteger(QTreeWidgetItem::Type);
    }
    return QTreeWidgetItem::Type;
}

// Build a tree context menu
bool QtCustomTree::buildMenu(QMenu*& menu, NamedString* ns)
{
    if (!ns)
	return false;
    NamedList* p = YOBJECT(NamedList,ns);
    if (!p)
	return false;
    if (menu)
	QtClient::deleteLater(menu);
    // Check if we are part of a widget list container item
    QtUIWidget* container = QtUIWidget::container(this);
    if (!container)
	menu = QtClient::buildMenu(*p,0,0,0,0,this);
    else
	menu = container->buildWidgetItemMenu(this,p,String::empty(),false);
    return true;
}

// Retrieve all items' id
bool QtCustomTree::getOptions(NamedList& items)
{
    DDebug(ClientDriver::self(),DebugAll,"QtCustomTree(%s)::getOptions()",name().c_str());
    findItems(items);
    return true;
}

// Retrieve a QObject list containing container items
QList<QObject*> QtCustomTree::getContainerItems()
{
    QList<QObject*> list;
    QList<QtTreeItem*> items = findItems();
    for (int i = 0; i < items.size(); i++) {
	QWidget* w = itemWidget(items[i],0);
	if (w)
	    list.append(static_cast<QObject*>(w));
    }
    return list;
}

// Find a tree item
QtTreeItem* QtCustomTree::find(const String& id, QtTreeItem* start, bool includeStart,
    bool recursive)
{
    if (start && includeStart && id == start->id())
	return start;
    QTreeWidgetItem* root = start ? static_cast<QTreeWidgetItem*>(start) : invisibleRootItem();
    if (!root)
	return 0;
    int n = root->childCount();
    for (int i = 0; i < n; i++) {
	QtTreeItem* item = static_cast<QtTreeItem*>(root->child(i));
	if (!item)
	    continue;
	if (id == item->id() ||
	    (recursive && 0 != (item = find(id,item,false,true))))
	    return item;
    }
    return 0;
}

// Find all tree items
QList<QtTreeItem*> QtCustomTree::findItems(bool recursive, QtTreeItem* start)
{
    QList<QtTreeItem*> list;
    QTreeWidgetItem* root = start ? static_cast<QTreeWidgetItem*>(start) : invisibleRootItem();
    if (!root)
	return list;
    int n = root->childCount();
    for (int i = 0; i < n; i++) {
	QtTreeItem* item = static_cast<QtTreeItem*>(root->child(i));
	if (!item)
	    continue;
	list.append(item);
	if (recursive) {
	    QList<QtTreeItem*> tmp = findItems(true,item);
	    list += tmp;
	}
    }
    return list;
}

// Find all tree items having a given id
QList<QtTreeItem*> QtCustomTree::findItems(const String& id, QtTreeItem* start,
    bool includeStart, bool recursive)
{
    QList<QtTreeItem*> list;
    if (start && includeStart && id == start->id())
	list.append(start);
    QTreeWidgetItem* root = start ? static_cast<QTreeWidgetItem*>(start) : invisibleRootItem();
    if (!root)
	return list;
    int n = root->childCount();
    for (int i = 0; i < n; i++) {
	QtTreeItem* item = static_cast<QtTreeItem*>(root->child(i));
	if (!item)
	    continue;
	if (id == item->id())
	    list.append(item);
	if (recursive) {
	    QList<QtTreeItem*> tmp = findItems(id,item,false,true);
	    list += tmp;
	}
    }
    return list;
}

// Find all tree items having a given type
QList<QtTreeItem*> QtCustomTree::findItems(int type, QtTreeItem* start,
    bool includeStart, bool recursive)
{
    QList<QtTreeItem*> list;
    if (start && includeStart && type == start->type())
	list.append(start);
    QTreeWidgetItem* root = start ? static_cast<QTreeWidgetItem*>(start) : invisibleRootItem();
    if (!root)
	return list;
    int n = root->childCount();
    for (int i = 0; i < n; i++) {
	QtTreeItem* item = static_cast<QtTreeItem*>(root->child(i));
	if (!item)
	    continue;
	if (type == item->type())
	    list.append(item);
	if (recursive) {
	    QList<QtTreeItem*> tmp = findItems(type,item,false,true);
	    list += tmp;
	}
    }
    return list;
}

// Find al tree items
void QtCustomTree::findItems(NamedList& list, QtTreeItem* start, bool includeStart,
    bool recursive)
{
    if (start && includeStart)
	list.setParam(start->id(),"");
    QTreeWidgetItem* root = start ? static_cast<QTreeWidgetItem*>(start) : invisibleRootItem();
    if (!root)
	return;
    int n = root->childCount();
    for (int i = 0; i < n; i++) {
	QtTreeItem* item = static_cast<QtTreeItem*>(root->child(i));
	if (!item)
	    continue;
	list.setParam(item->id(),"");
	if (recursive)
	    findItems(list,item,false,true);
    }
}

// Add a child to a given item
QtTreeItem* QtCustomTree::addChild(QtTreeItem* child, int pos, QtTreeItem* parent)
{
    if (!child)
	return 0;
    QTreeWidgetItem* root = parent ? static_cast<QTreeWidgetItem*>(parent) : invisibleRootItem();
    if (!root)
	return 0;
    DDebug(ClientDriver::self(),DebugAll,
	"QtTree(%s) adding child '%s' type=%d parent=%p pos=%d",
	name().c_str(),child->id().c_str(),child->type(),parent,pos);
    if (pos < 0 || pos >= root->childCount())
	root->addChild(child);
    else
	root->insertChild(pos,child);
    // Set widget
    int h = m_rowHeight;
    if (!itemWidget(child,0)) {
	QWidget* w = loadWidgetType(this,child->id(),itemPropsName(child->type()));
	if (w) {
	    w->setAutoFillBackground(true);
	    setItemWidget(child,0,w);
	    XDebug(ClientDriver::self(),DebugAll,
		"QtTree(%s) set widget (%p,%s) for child '%s'",
		name().c_str(),w,YQT_OBJECT_NAME(w),child->id().c_str());
	    applyStyleSheet(child,child->isSelected());
	    h = w->height();
	}
    }
    if (h > 0) {
	QSize sz = child->sizeHint(0);
	sz.setHeight(h);
	child->setSizeHint(0,sz);
    }
    if (m_autoExpand)
	child->setExpanded(true);
    itemAdded(*child,parent);
    return child;
}

// Show or hide empty children.
void QtCustomTree::showEmptyChildren(bool show, QtTreeItem* parent)
{
    QTreeWidgetItem* root = parent ? static_cast<QTreeWidgetItem*>(parent) : invisibleRootItem();
    if (!root)
	return;
    SafeTree tree(this);
    int n = root->childCount();
    for (int i = 0; i < n; i++) {
	QTreeWidgetItem* item = root->child(i);
	if (!item)
	    continue;
	if (show) {
	    item->setHidden(false);
	    continue;
	}
	// Find a displayed child. Hide the item if not found
	QTreeWidgetItem* child = 0;
	int nc = item->childCount();
	for (int j = 0; j < nc; j++, child = 0) {
	    child = item->child(j);
	    if (child && !child->isHidden())
		break;
	}
	item->setHidden(!child);
    }
}

// Set the expanded/collapsed image of an item
void QtCustomTree::setStateImage(QtTreeItem& item)
{
    QtUIWidgetItemProps* pUi = QtUIWidget::getItemProps(itemPropsName(item.type()));
    QtTreeItemProps* p = YOBJECT(QtTreeItemProps,pUi);
    if (!(p && p->m_stateWidget))
	return;
    NamedList tmp("");
    tmp.addParam("image:" + p->m_stateWidget,
	item.isExpanded() ? p->m_stateExpandedImg : p->m_stateCollapsedImg);
    updateItem(item,tmp);
}

// Set an item props ui
void QtCustomTree::setItemUi(QString value)
{
    String tmp;
    QtUIWidgetItemProps* p = getItemProps(value,tmp);
    p->m_ui = tmp;
}

// Set an item props style sheet
void QtCustomTree::setItemStyle(QString value)
{
    String tmp;
    QtUIWidgetItemProps* p = getItemProps(value,tmp);
    p->m_styleSheet = tmp;
}

// Set an item props selected style sheet
void QtCustomTree::setItemSelectedStyle(QString value)
{
    String tmp;
    QtUIWidgetItemProps* p = getItemProps(value,tmp);
    p->m_selStyleSheet = tmp;
}

// Set an item props state widget name
void QtCustomTree::setItemStateWidget(QString value)
{
    String tmp;
    QtTreeItemProps* p = YOBJECT(QtTreeItemProps,getItemProps(value,tmp));
    if (p)
	p->m_stateWidget = tmp;
}

// Set an item's expanded image
void QtCustomTree::setExpandedImage(QString value)
{
    String tmp;
    QtTreeItemProps* p = YOBJECT(QtTreeItemProps,getItemProps(value,tmp));
    if (p)
	p->m_stateExpandedImg = Client::s_skinPath + tmp;
}

// Set an item's collapsed image
void QtCustomTree::setItemCollapsedImage(QString value)
{
    String tmp;
    QtTreeItemProps* p = YOBJECT(QtTreeItemProps,getItemProps(value,tmp));
    if (p)
	p->m_stateCollapsedImg = Client::s_skinPath + tmp;
}

// Set an item's tooltip template
void QtCustomTree::setItemTooltip(QString value)
{
    String tmp;
    QtTreeItemProps* p = YOBJECT(QtTreeItemProps,getItemProps(value,tmp));
    if (p)
	p->m_toolTip = tmp;
}

// Set an item's statistics widget name
void QtCustomTree::setItemStatsWidget(QString value)
{
    String tmp;
    QtTreeItemProps* p = YOBJECT(QtTreeItemProps,getItemProps(value,tmp));
    if (p)
	p->m_statsWidget = tmp;
}

// Set an item's statistics template
void QtCustomTree::setItemStatsTemplate(QString value)
{
    String tmp;
    QtTreeItemProps* p = YOBJECT(QtTreeItemProps,getItemProps(value,tmp));
    if (p)
	p->m_statsTemplate = tmp;
}

// Retrieve a comma separated list with column widths
QString QtCustomTree::colWidths()
{
    String t;
    int cols = columnCount();
    for (int i = 0; i < cols; i++)
	t.append(String(columnWidth(i)),",");
    return QtClient::setUtf8(t);
}

// Set column widths
void QtCustomTree::setColWidths(QString widths)
{
    QStringList list = widths.split(",");
    for (int i = 0; i < list.size(); i++) {
	int width = list[i].toInt();
	if (width >= 0)
	    setColumnWidth(i,width);
    }
}

// Retrieve tree sorting string (column and order)
QString QtCustomTree::sorting()
{
    String t;
    QHeaderView* h = QTreeView::header();
    if (h) {
	int col = h->sortIndicatorSection();
	int sort = h->sortIndicatorOrder();
	if (col >= 0 && col < columnCount() &&
	    (sort == Qt::AscendingOrder || sort == Qt::DescendingOrder))
	    t << col << "," << sort;
    }
    return QtClient::setUtf8(t);
}

// Set sorting (column and order)
void QtCustomTree::setSorting(QString s)
{
    QHeaderView* h = QTreeView::header();
    if (!h)
	return;
    QStringList list = s.split(",");
    if (list.size() >= 2) {
	int col = list[0].toInt();
	int sort = list[1].toInt();
	if (col >= 0 && col < columnCount() &&
	    (sort == Qt::AscendingOrder || sort == Qt::DescendingOrder))
	    h->setSortIndicator(col,(Qt::SortOrder)sort);
    }
}

// Apply item widget style sheet
void QtCustomTree::applyStyleSheet(QtTreeItem* item, bool selected)
{
    if (!item)
	return;
    QWidget* w = itemWidget(item,0);
    if (!w)
	return;
    QtUIWidgetItemProps* p = QtUIWidget::getItemProps(itemPropsName(item->type()));
    if (p)
	applyWidgetStyle(w,selected ? p->m_selStyleSheet : p->m_styleSheet);
}

// Process item selection changes
void QtCustomTree::onSelChanged(QtTreeItem* sel, QtTreeItem* prev)
{
    DDebug(ClientDriver::self(),DebugAll,
	"QtCustomTree(%s) onSelChanged sel=%s prev=%s [%p]",
	name().c_str(),sel ? sel->id().c_str() : "",
	prev ? prev->id().c_str() : "",this);
    applyStyleSheet(prev,false);
    applyStyleSheet(sel,true);
    // Fix: In the initial state, selection style is not set
    //  when the user clicks the header
    if (sel && !prev)
	setCurrentItem(sel);
    const String& id = sel ? sel->id() : String::empty();
    onSelect(this,&id);
}

// Process item double click
void QtCustomTree::onItemDoubleClicked(QtTreeItem* item, int column)
{
    if (item && Client::self())
	onAction(this);
}

// Item expanded/collapsed notification
void QtCustomTree::onItemExpandedChanged(QtTreeItem* item)
{
    if (!item)
	return;
    setStateImage(*item);
    applyItemStatistics(*item);
}

// Catch a context menu event and show the context menu
void QtCustomTree::contextMenuEvent(QContextMenuEvent* e)
{
    QtTreeItem* it = static_cast<QtTreeItem*>(itemAt(e->pos()));
    QMenu* menu = contextMenu(it);
    if (!menu)
	menu = m_menu;
    if (!menu)
	return;
    menu->exec(e->globalPos());
}

// Update a tree item
bool QtCustomTree::updateItem(QtTreeItem& item, const NamedList& params)
{
    DDebug(ClientDriver::self(),DebugAll,"QtCustomTree(%s)::updateItem(%p,%s)",
	name().c_str(),&item,item.id().c_str());
    bool all = (&params == &item);
    if (!all)
	item.copyParams(params);
    QWidget* w = itemWidget(&item,0);
    if (w)
	QtUIWidget::setParams(w,all ? (const NamedList&)item : params);
    applyItemTooltip(item);
    return true;
}

// Get the context menu associated with a given item
QMenu* QtCustomTree::contextMenu(QtTreeItem* item)
{
    return 0;
}

// Item added notification
void QtCustomTree::itemAdded(QtTreeItem& item, QtTreeItem* parent)
{
    setStateImage(item);
    applyItemTooltip(item);
    applyItemStatistics(item);
    if (parent)
	applyItemStatistics(*parent);
}

// Item removed notification
// The item will be deleted after returning from this notification
void QtCustomTree::itemRemoved(QtTreeItem& item, QtTreeItem* parent)
{
    if (parent)
	applyItemStatistics(*parent);
}

// Update a tree item's tooltip
void QtCustomTree::applyItemTooltip(QtTreeItem& item)
{
    QtUIWidgetItemProps* pt = QtUIWidget::getItemProps(itemPropsName(item.type()));
    QtTreeItemProps* p = YOBJECT(QtTreeItemProps,pt);
    String tooltip;
    if (p)
	tooltip = p->m_toolTip;
    if (!tooltip)
	return;
    item.replaceParams(tooltip);
    QWidget* w = itemWidget(&item,0);
    if (w)
	w->setToolTip(QtClient::setUtf8(tooltip));
    else
	item.setToolTip(0,QtClient::setUtf8(tooltip));
}

// Fill a list with item statistics.
void QtCustomTree::fillItemStatistics(QtTreeItem& item, NamedList& list)
{
    list.addParam("count",String(item.childCount()));
}

// Update a tree item's statistics
void QtCustomTree::applyItemStatistics(QtTreeItem& item)
{
    QtUIWidgetItemProps* pt = QtUIWidget::getItemProps(itemPropsName(item.type()));
    QtTreeItemProps* p = YOBJECT(QtTreeItemProps,pt);
    if (!(p && p->m_statsWidget))
	return;
    String text;
    if (!item.isExpanded()) {
	text = p->m_statsTemplate;
	NamedList list("");
	fillItemStatistics(item,list);
	list.replaceParams(text);
    }
    NamedList params("");
    params.addParam(p->m_statsWidget,text);
    updateItem(item,params);
}


/*
 * ContactList
 */
ContactList::ContactList(const char* name, const NamedList& params, QWidget* parent)
    : QtCustomTree(name,params,parent),
    m_flatList(true),
    m_showOffline(true),
    m_hideEmptyGroups(true),
    m_menuContact(0)
{
    XDebug(ClientDriver::self(),DebugAll,"ContactList(%s) [%p]",name,this);
    // Add item props translation
    m_itemPropsType.addParam(String((int)TypeContact),"contact");
    m_itemPropsType.addParam(String((int)TypeGroup),"group");
    m_savedIndent = indentation();
    m_groupCountWidget = params["groupcount"];
    m_noGroupText = "None";
    //FIXME
    m_saveProps << "_yate_flatlist";
    m_saveProps << "_yate_showofflinecontacts";
    m_saveProps << "_yate_hideemptygroups";
    setParams(params);
}

// Set params
bool ContactList::setParams(const NamedList& params)
{
    bool ok = QtCustomTree::setParams(params);
    buildMenu(m_menuContact,params.getParam("contactmenu"));
    return ok;
}

// Update a contact
bool ContactList::setTableRow(const String& item, const NamedList* data)
{
    DDebug(ClientDriver::self(),DebugAll,"ContactList(%s)::setTableRow(%s,%p)",
	name().c_str(),item.c_str(),data);
    QtTreeItem* it = find(item);
    if (!it)
	return false;
    if (!data)
	return true;
    SafeTree tree(this);
    bool ok = it->type() == TypeContact && updateContact(item,*data,false);
    listChanged();
    return ok;
}

// Add a new account or contact
bool ContactList::addTableRow(const String& item, const NamedList* data, bool atStart)
{
    DDebug(ClientDriver::self(),DebugAll,"ContactList(%s)::addTableRow(%s,%p,%u)",
	name().c_str(),item.c_str(),data,atStart);
    if (!data)
	return false;
    if (find(item))
	return false;
    SafeTree tree(this);
    bool ok = updateContact(item,*data,atStart);
    listChanged();
    return ok;
}

// Remove an item from tree
bool ContactList::delTableRow(const String& item)
{
    DDebug(ClientDriver::self(),DebugAll,"ContactList(%s)::delTableRow(%s)",
	name().c_str(),item.c_str());
    if (!item)
	return false;
    SafeTree tree(this);
    bool ok = removeContact(item);
    listChanged();
    return ok;
}

// Add, set or remove one or more contacts.
// Each data list element is a NamedPointer carrying a NamedList with item parameters.
// The name of an element is the item to update.
// Set element's value to boolean value 'true' to add a new item if not found
//  or 'false' to set an existing one. Set it to empty string to delete the item
bool ContactList::updateTableRows(const NamedList* data, bool atStart)
{
    if (!data)
	return true;
    DDebug(ClientDriver::self(),DebugAll,"ContactList(%s)::updateTableRows()",
	name().c_str());
    SafeTree tree(this);
    bool ok = false;
    unsigned int n = data->length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = data->getParam(i);
	if (!(ns && ns->name()))
	    continue;
	if (!ns->null()) {
	    NamedList* params = static_cast<NamedList*>(ns->getObject("NamedList"));
	    if (!params) {
		ok = (0 != find(ns->name())) || ok;
		continue;
	    }
	    if (ns->toBoolean() || find(ns->name()))
		ok = updateContact(ns->name(),*params,atStart) || ok;
	}
	else
	    ok = removeContact(ns->name()) || ok;
    }
    listChanged();
    return ok;
}

// Count online/total contacts in a group.
void ContactList::countContacts(QtTreeItem* grp, int& total, int& online)
{
    QList<QtTreeItem*> c = findItems(TypeContact,grp,true,false);
    total = c.size();
    online = 0;
    for (int i = 0; i < total; i++)
	if (!(static_cast<ContactItem*>(c[i]))->offline())
	    online++;
}

// Contact list changed notification
void ContactList::listChanged()
{
    // Hide empty groups
    if (!m_flatList)
	showEmptyChildren(!m_hideEmptyGroups);
    // Update contact count in groups
    if (!m_flatList && m_groupCountWidget) {
	QList<QtTreeItem*> grps = findItems(TypeGroup,0,true,false);
	for (int i = 0; i < grps.size(); i++) {
	    if (!grps[i])
		continue;
	    updateGroupCountContacts(*(grps[i]));
	}
    }
}

// Set '_yate_nogroup_caption' property
void ContactList::setNoGroupCaption(QString value)
{
    QtClient::getUtf8(m_noGroupText,value);
}

// Set contact grouping
void ContactList::setFlatList(bool flat)
{
    if (flat == m_flatList)
	return;

    QTreeWidgetItem* root = invisibleRootItem();
    if (!root)
	return;
    String sel;
    getSelect(sel);
    setCurrentItem(0);
    // Retrieve (take) contacts
    QList<QTreeWidgetItem*> c = root->takeChildren();
    // Shown by group: remove groups and contact duplicates
    if (!m_flatList) {
	for (int i = 0; i < c.size(); i++) {
	    c << c[i]->takeChildren();
	    if (c[i]->type() == TypeGroup) {
		delete c[i];
		c[i] = 0;
	    }
	}
	for (int i = 0; i < c.size(); i++) {
	    if (!c[i])
		continue;
	    for (int j = i + 1; j < c.size(); j++) {
		QtTreeItem* cc = static_cast<QtTreeItem*>(c[j]);
		if (cc && cc->id() == (static_cast<QtTreeItem*>(c[i]))->id()) {
		    delete c[j];
		    c[j] = 0;
		}
	    }
	}
    }
    // Set new grouping
    m_flatList = flat;
    // Save/restore indendation
    if (!m_flatList)
	setIndentation(m_savedIndent);
    else {
	m_savedIndent = indentation();
	setIndentation(0);
    }
    // Add contacts to tree
    for (int i = 0; i < c.size(); i++) {
	if (!c[i])
	    continue;
	ContactItem* cc = static_cast<ContactItem*>(c[i]);
        if (!m_flatList) {
	    ObjList* groups = cc->groups();
	    ObjList* o = groups->skipNull();
	    if (o) {
		// Add a copy of the contact for each group
		while (o) {
		    QtTreeItem* g = getGroup(o->get()->toString());
		    o = o->skipNext();
		    if (!g)
			continue;
		    if (!o)
		        cc = static_cast<ContactItem*>(addChild(cc,false,g));
		    else {
			ContactItem* ci = new ContactItem(cc->id(),*cc);
		        if (addChild(ci,false,g))
			    updateContact(*ci,*cc,true);
			else
			    TelEngine::destruct(ci);
		    }
		    if (!g->childCount())
			TelEngine::destruct(g);
		}
	    }
	    else {
		QtTreeItem* g = getGroup();
		if (g) {
		    cc = static_cast<ContactItem*>(addChild(cc,false,g));
		    if (!g->childCount())
			TelEngine::destruct(g);
		}
	    }
	    TelEngine::destruct(groups);
	}
	else
	    cc = static_cast<ContactItem*>(addChild(cc,false));
	if (!cc) {
	    // Delete failed contact
	    cc = static_cast<ContactItem*>(c[i]);
	    Debug(ClientDriver::self(),DebugWarn,
		"ContactList(%s)::setFlatList() deleting failed contact '%s'",
		name().c_str(),cc->id().c_str());
	    delete cc;
	}
    }
    listChanged();
    setSelect(sel);
}

// Show or hide offline contacts
void ContactList::setShowOffline(bool value)
{
    if (m_showOffline == value)
	return;
    m_showOffline = value;
    QTreeWidgetItem* root = invisibleRootItem();
    if (!root)
	return;
    String sel;
    getSelect(sel);
    setCurrentItem(0);
    QList<QtTreeItem*> list = findItems(TypeContact);
    for (int i = 0; i < list.size(); i++) {
	ContactItem* c = static_cast<ContactItem*>(list[i]);
	if (!c)
	    continue;
	if (c->offline())
	    c->setHidden(!m_showOffline);
    }
    listChanged();
    // Avoid selecting a hidden item
    QtTreeItem* it = sel ? find(sel) : 0;
    if (it && !it->isHidden())
	setCurrentItem(it);
}

// Retrieve the contact count to be shown in group
void ContactList::updateGroupCountContacts(QtTreeItem& item)
{
    if (item.type() != TypeGroup)
	return;
    String value;
    if (!item.isExpanded()) {
	int total = 0;
	int online = 0;
	countContacts(&item,total,online);
	value << "(" << online << "/" << total << ")";
    }
    NamedList tmp("");
    tmp.addParam(m_groupCountWidget,value);
    updateItem(item,tmp);
}

// Add or update a contact
bool ContactList::updateContact(const String& id, const NamedList& params,
    bool atStart)
{
    if (TelEngine::null(id))
	return false;
    bool ok = false;
    bool found = false;
    QList<QtTreeItem*> list = findItems(id);
    String* groups = params.getParam("groups");
    DDebug(ClientDriver::self(),DebugAll,"ContactList(%s)::updateContact(%s) groups=%s",
	name().c_str(),id.c_str(),TelEngine::c_safe(groups));
    if (!groups || m_flatList) {
	// No group changes or not shown by group. Update all contacts
	for (int i = 0; !found && i < list.size(); i++)
	    if (list[i]->type() == TypeContact && list[i]->id() == id) {
		ok = updateContact(*(static_cast<ContactItem*>(list[i])),params) || ok;
		found = true;
	    }
    }
    else {
	// Groups changed while shown by group
	ContactItem* c = 0;
	for (int i = 0; i < list.size(); i++)
	    if (list[i]->type() == TypeContact && list[i]->id() == id) {
		c = static_cast<ContactItem*>(list[i]);
		break;
	    }
	if (c) {
	    // Check if groups changed
	    ObjList removedGroups;
	    ObjList newGroups;
	    ObjList* cgroups = c->groups();
	    ObjList* newList = Client::splitUnescape(*groups);
	    ObjList* o;
	    for (o = newList->skipNull(); o; o = o->skipNext())
		if (!cgroups->find(o->get()->toString()))
		    newGroups.append(new String(o->get()->toString()));
	    for (o = cgroups->skipNull(); o; o = o->skipNext())
		if (!newList->find(o->get()->toString()))
		    removedGroups.append(new String(o->get()->toString()));
	    // Update
	    if (newGroups.skipNull() || removedGroups.skipNull()) {
		// Re-use removed items
		QList<ContactItem*> removed;
		// Remove contacts from groups
		for (o = removedGroups.skipNull(); o; o = o->skipNext())
		    removeContactFromGroup(removed,id,o->get()->toString());
		if (!cgroups->skipNull())
		    removeContactFromGroup(removed,id);
		// Add contacts to new groups
		o = newGroups.skipNull();
		if (o) {
		    for (; o; o = o->skipNext())
			addContactToGroup(id,params,atStart,o->get()->toString(),&removed,c);
		}
		else if (TelEngine::null(groups))
		    addContactToGroup(id,params,atStart,String::empty(),
			&removed,c);
		// Delete not used items
		for (int i = 0; i < removed.size(); i++)
		    if (removed[i])
			delete removed[i];
		// Refresh list
		list = findItems(id);
	    }
	    TelEngine::destruct(newList);
	    TelEngine::destruct(cgroups);
	    for (int i = 0; i < list.size(); i++) {
		if (list[i]->type() != TypeContact || list[i]->id() != id)
		    continue;
		ok = updateContact(*(static_cast<ContactItem*>(list[i])),params) || ok;
		found = true;
	    }
	}
    }

    if (found)
	return ok;

    if (!m_flatList) {
	// Add to all groups
	ObjList* grps = groups ? Client::splitUnescape(*groups) : 0;
	ObjList* o = grps ? grps->skipNull() : 0;
	if (o) {
	    for (; o; o = o->skipNext())
		ok = addContactToGroup(id,params,atStart,o->get()->toString()) || ok;
	}
	else
	    ok = addContactToGroup(id,params,atStart);
	TelEngine::destruct(grps);
    }
    else {
	ContactItem* c = new ContactItem(id,params);
	ok = addChild(c,atStart);
	if (ok)
	    ok = updateContact(*c,params);
	else
	    TelEngine::destruct(c);
    }
    return ok;
}

// Remove a contact from tree
bool ContactList::removeContact(const String& id)
{
    DDebug(ClientDriver::self(),DebugAll,"ContactList(%s)::removeContact(%s)",
	name().c_str(),id.c_str());
    if (m_flatList) {
	QtTreeItem* it = find(id,0,false,false);
	if (it)
	    delete it;
	return it != 0;
    }
    // Remove from each group
    QTreeWidgetItem* root = QTreeWidget::invisibleRootItem();
    if (!root)
	return false;
    bool ok = false;
    while (true) {
	int start = 0;
        int n = root->childCount();
	for (; start < n; start++) {
	    QtTreeItem* it = static_cast<QtTreeItem*>(root->child(start));
	    if (!it)
		continue;
	    QtTreeItem* c = find(id,it,false,false);
	    if (!c)
		continue;
	    ok = true;
	    delete c;
	    // Remove empty group and restart
	    if (!it->childCount()) {
		delete it;
		if (start < n - 1)
		    break;
	    }
	}
	if (start == n)
	    break;
    }
    return ok;
}

// Update a contact
bool ContactList::updateContact(ContactItem& c, const NamedList& params, bool all)
{
#ifdef DEBUG
    String tmp;
    params.dump(tmp," ");
    Debug(ClientDriver::self(),DebugAll,"ContactList(%s)::updateContact(%p,%s) all=%u %s",
	name().c_str(),&c,c.id().c_str(),all,tmp.safe());
#endif
    if (&params != &c)
	c.copyParams(params);
    else
	all = true;
    const NamedList* p = all ? &c : &params;
    QWidget* w = itemWidget(&c,0);
    if (w)
	QtUIWidget::setParams(w,*p);
    else {
	String* name = p->getParam("name");
	if (name)
	    c.setText(0,QtClient::setUtf8(*name));
	// TODO: update status, image ...
    }
    applyItemTooltip(c);
    // Show/hide
    if (!m_showOffline)
	c.setHidden(c.offline());
    return true;
}

// Update a contact
bool ContactList::updateItem(QtTreeItem& item, const NamedList& params)
{
    if (item.type() == TypeContact)
	return updateContact(*static_cast<ContactItem*>(&item),params);
    return QtCustomTree::updateItem(item,params);
}

// Item expanded/collapsed notification
void ContactList::onItemExpandedChanged(QtTreeItem* item)
{
    QtCustomTree::onItemExpandedChanged(item);
    // Update online/offline contacts
    if (item && item->type() == TypeGroup && m_groupCountWidget)
	updateGroupCountContacts(*item);
}

// Get the context menu associated with a given item
QMenu* ContactList::contextMenu(QtTreeItem* item)
{
    if (!item)
	return QtCustomTree::contextMenu(0);
    if (item->type() == TypeContact) {
	if (m_menuContact)
	    return m_menuContact;
    }
    else if (item->type() == TypeGroup)
	return m_menu;
    return QtCustomTree::contextMenu(item);
}

// Item added notification
void ContactList::itemAdded(QtTreeItem& item, QtTreeItem* parent)
{
    QtCustomTree::itemAdded(item,parent);
    DDebug(ClientDriver::self(),DebugAll,"ContactList(%s)::itemAdded(%p,%p) type=%d id=%s",
	name().c_str(),&item,parent,item.type(),item.id().c_str(),parent);
    if (item.type() == TypeContact) {
	ContactItem* c = static_cast<ContactItem*>(&item);
	updateContact(*c,*c);
	return;
    }
    if (item.type() != TypeGroup)
	return;
    // Set group name
    QWidget* w = itemWidget(&item,0);
    if (!w)
	return;
    QtWindow* wnd = QtClient::parentWindow(this);
    if (!wnd)
	return;
    String text;
    QtClient::getUtf8(text,item.text(0));
    String n;
    QtClient::getUtf8(n,w->objectName());
    String buf;
    wnd->setText(buildChildName(buf,n,"group"),text,false);
}

// Retrieve a group item from root or create a new one
QtTreeItem* ContactList::getGroup(const String& name, bool create)
{
    const String& grp = name ? name : s_noGroupId;
    if (!grp)
	return 0;
    // Check if the group already exists
    QList<QtTreeItem*> list = findItems(grp,0,false,false);
    for (int i = 0; i < list.size(); i++) {
	if (list[i]->id() == grp && list[i]->type() == TypeGroup)
	    return list[i];
    }
    if (!create)
	return 0;
    QTreeWidgetItem* root = invisibleRootItem();
    if (!root)
	return 0;
    const String& gText = name ? name : m_noGroupText;
    XDebug(ClientDriver::self(),DebugAll,"ContactList(%s) creating group id=%s text='%s'",
	this->name().c_str(),grp.c_str(),gText.c_str());
    // Always keep 'no group' the last one
    // Insert any other group before it
    int pos = -1;
    if (grp != s_noGroupId) {
	QtTreeItem* noGrp = getGroup(s_noGroupId,false);
	if (noGrp)
	    pos = root->indexOfChild(noGrp);
    }
    QtTreeItem* g = new QtTreeItem(grp,TypeGroup,gText);
    if (!addChild(g,pos))
	TelEngine::destruct(g);
    return g;
}

// Add a contact to a group item
bool ContactList::addContactToGroup(const String& id, const NamedList& params,
    bool atStart, const String& grp, QList<ContactItem*>* bucket,
    const NamedList* origParams)
{
    DDebug(ClientDriver::self(),DebugAll,
	"ContactList(%s)::addContactToGroup(%s,%p,%u,%s,%p,%p)",
	name().c_str(),id.c_str(),&params,atStart,grp.c_str(),bucket,origParams);
    QtTreeItem* g = getGroup(grp);
    if (!g)
	return false;
    int i = 0;
    ContactItem* c = 0;
    if (bucket) {
	for (i = 0; i < bucket->size(); i++)
	    if ((*bucket)[i]) {
		c = (*bucket)[i];
		break;
	    }
    }
    if (!c) {
	if (origParams) {
	    c = new ContactItem(id,*origParams);
	    c->copyParams(*origParams);
	}
	else
	    c = new ContactItem(id,params);
    }
    c->copyParams(params);
    bool oldContact = (bucket && i < bucket->size());
    if (addChild(c,atStart,g)) {
	if (oldContact)
	    (*bucket)[i] = 0;
	return true;
    }
    if (!oldContact) {
	TelEngine::destruct(c);
	// Remove empty group
	if (g->childCount() < 1)
	    delete g;
    }
    return false;
}

// Remove a contact from a group item and add it to a list
void ContactList::removeContactFromGroup(QList<ContactItem*> list, const String& id,
    const String& grp)
{
    DDebug(ClientDriver::self(),DebugAll,
	"ContactList(%s)::removeContactFromGroup(%s,%s)",
	name().c_str(),id.c_str(),grp.c_str());
    QtTreeItem* g = getGroup(grp,false);
    if (!g)
	return;
    QtTreeItem* it = find(id,g,false,false);
    if (!(it && it->type() == TypeContact))
	return;
    g->removeChild(it);
    list.append(static_cast<ContactItem*>(it));
    // Remove empty group
    if (g->childCount() < 1)
	delete g;
}


/*
 * ContactItem
 */
// Check if the contact status is 'offline'
bool ContactItem::offline()
{
    String* status = getParam("status");
    return status && *status == s_offline;
}


/*
 * CustomTreeFactory
 */
// Build objects
void* CustomTreeFactory::create(const String& type, const char* name, NamedList* params)
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
    if (type == "ContactList")
        return new ContactList(name,*params,parentWidget);
    if (type == "QtCustomTree")
        return new QtCustomTree(name,*params,parentWidget);
    return 0;
}

}; // anonymous namespace

#include "customtree.moc"

/* vi: set ts=8 sw=4 sts=4 noet: */
