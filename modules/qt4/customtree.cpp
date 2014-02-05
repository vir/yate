/**
 * customtree.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Custom QtTree based objects
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

#include "customtree.h"

#ifndef _WINDOWS
#include <dirent.h>
#include <sys/stat.h>
#endif

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
	    m_types.append(new String("FileListTree"));
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

// Inc/dec an integer value
class SafeInt
{
public:
    inline SafeInt(int* value)
	: m_value(value) {
	    if (m_value)
		(*m_value)++;
	}
    inline ~SafeInt() {
	    if (m_value)
		(*m_value)--;
	}
protected:
    int* m_value;
};

// Utility class used to restore selection
class TreeRestoreSel
{
public:
    inline TreeRestoreSel(QtCustomTree* tree, const String& check = String::empty())
	: m_tree(tree) {
	    if (!tree)
		return;
	    tree->getSelect(m_sel);
	    if (m_sel && check && m_sel != check)
		m_sel.clear();
	}
    inline ~TreeRestoreSel() {
	    if (m_tree && m_sel)
		m_tree->setSelect(m_sel);
	}
private:
    QtCustomTree* m_tree;
    String m_sel;
};


static CustomTreeFactory s_factory;
static const String s_noGroupId(MD5("Yate").hexDigest() + "_NOGROUP");
static const String s_offline("offline");
static NamedList s_delegateCommon("");

// Set size from string
static inline void setSize(QSize& size, const String& s)
{
    if (!s)
	return;
    int pos = s.find(',');
    if (pos < 0) {
	int val = s.toInteger(0,0,0);
	size = QSize(val,val);
    }
    else
	size = QSize(s.substr(0,pos).toInteger(0,0,0),s.substr(pos + 1).toInteger(0,0,0));
}

// Utility: compare strings
// return -1 if s1 < s2, 0 if s1 == s2, 1 if s1 > s2
static inline int compareStr(const QString& s1, const QString& s2,
    Qt::CaseSensitivity cs)
{
    if (cs == Qt::CaseSensitive) {
	if (s1 == s2)
	    return 0;
	return (s1 < s2) ? -1 : 1;
    }
    return s1.compare(s2,cs);
}

// Utility: compare a single key item
static bool caseInsensitiveLessThan(const QtTreeItemKey& left,
    const QtTreeItemKey& right)
{
    return compareStr(left.second,right.second,Qt::CaseInsensitive) < 0;
}

// Utility: compare a single key item
static bool caseInsensitiveGreaterThan(const QtTreeItemKey& left,
    const QtTreeItemKey& right)
{
    return compareStr(left.second,right.second,Qt::CaseInsensitive) > 0;
}

// Utility: compare a single key item
static bool caseSensitiveLessThan(const QtTreeItemKey& left,
    const QtTreeItemKey& right)
{
    return compareStr(left.second,right.second,Qt::CaseSensitive) < 0;
}

// Utility: compare a single key item
static bool caseSensitiveGreaterThan(const QtTreeItemKey& left,
    const QtTreeItemKey& right)
{
    return compareStr(left.second,right.second,Qt::CaseSensitive) > 0;
}

// Utility: sort
static inline void stableSort(QVector<QtTreeItemKey>& v,
    Qt::SortOrder order, Qt::CaseSensitivity cs)
{
    if (order == Qt::AscendingOrder) {
	if (cs == Qt::CaseInsensitive)
	    qStableSort(v.begin(),v.end(),caseInsensitiveLessThan);
	else
	    qStableSort(v.begin(),v.end(),caseSensitiveLessThan);
    }
    else if (cs == Qt::CaseInsensitive)
	qStableSort(v.begin(),v.end(),caseInsensitiveGreaterThan);
    else
	qStableSort(v.begin(),v.end(),caseSensitiveGreaterThan);
}

// Utility: sort

// Retrieve a string from a list
static inline const String& objListItem(ObjList* list, int index)
{
    GenObject* gen = list ? (*list)[index] : 0;
    return gen ? gen->toString() : String::empty();
}

int replaceHtmlParams(String& str, const NamedList& list, bool spaceEol = false)
{
    int p1 = 0;
    int cnt = 0;
    while ((p1 = str.find("${",p1)) >= 0) {
	int p2 = str.find('}',p1 + 2);
	if (p2 <= 0)
	    return -1;
	String param = str.substr(p1 + 2,p2 - p1 - 2);
	param.trimBlanks();
	int defValPos = param.find('$');
	if (defValPos < 0)
	    param = list.getValue(param);
	else {
	    // param is in ${<name>$<default>} format
	    String def = param.substr(defValPos + 1);
	    param = list.getValue(param.substr(0,defValPos).trimBlanks());
	    if (!param && def)
		param = list.getValue(def.trimBlanks());
	}
	if (param)
	    Client::plain2html(param,spaceEol);
	str = str.substr(0,p1) + param + str.substr(p2 + 1);
	// advance search offset past the string we just replaced
	p1 += param.length();
	cnt++;
    }
    return cnt;
}


/*
 * QtCellGridDraw
 */
// Set draw pen
void QtCellGridDraw::setPen(Position pos, QPen pen)
{
#define QtCellGridSetPen(val,p) \
    if (0 != (pos & val)) { \
	p = pen; \
	m_flags |= val; \
    }
    QtCellGridSetPen(Left,m_left);
    QtCellGridSetPen(Top,m_top);
    QtCellGridSetPen(Right,m_right);
    QtCellGridSetPen(Bottom,m_bottom);
}

// Set draw pens from a list of parameters
void QtCellGridDraw::setPen(const NamedList& params)
{
    setPen(Left,params);
    setPen(Top,params);
    setPen(Right,params);
    setPen(Bottom,params);
}

// Set pen from parameters list
void QtCellGridDraw::setPen(Position pos, const NamedList& params)
{
    String prefix("griddraw_");
    if (pos == Left)
	prefix << "left";
    else if (pos == Top)
	prefix << "top";
    else if (pos == Right)
	prefix << "right";
    else if (pos == Bottom)
	prefix << "bottom";
    else
	return;
    QPen pen;
    bool ok = false;
    const String& color = params[prefix + "_color"];
    if (color) {
	ok = true;
	if (color[0] == '#')
	    pen.setColor(QColor(color.substr(1).toInteger(0,16)));
	else
	    pen.setColor(QColor(color));
    }
    if (ok)
	setPen(pos,pen);
}

// Draw the borders
void QtCellGridDraw::draw(QPainter* p, QRect& rect, bool isFirstRow, bool isFirstColumn,
    bool isLastRow, bool isLastColumn) const
{
    if (!(p && flag(Pos)))
	return;
    if (0 != (m_flags & Left) && (!isFirstColumn || flag(DrawStart))) {
	p->setPen(m_left);
	p->drawLine(rect.topLeft(),rect.bottomLeft());
    }
    if (0 != (m_flags & Top) && (!isFirstRow || flag(DrawStart))) {
	p->setPen(m_top);
	p->drawLine(rect.topLeft(),rect.topRight());
    }
    if (0 != (m_flags & Right) && (!isLastColumn || flag(DrawEnd))) {
	p->setPen(m_right);
	p->drawLine(rect.topRight(),rect.bottomRight());
    }
    if (0 != (m_flags & Bottom) && (!isLastRow || flag(DrawEnd))) {
	p->setPen(m_bottom);
	p->drawLine(rect.bottomLeft(),rect.bottomRight());
    }
}


//
// QtTreeDrag
//
QtTreeDrag::QtTreeDrag(QObject* parent, const NamedList* params)
    : QObject(parent),
    m_urlBuilder(0)
{
    if (!params)
	return;
    const String& fmt = (*params)[YSTRING("_yate_drag_url_template")];
    if (fmt)
	setUrlBuilder(fmt,(*params)[YSTRING("_yate_drag_url_queryparams")]);
}

// Set the URL builder, set to NULL if fmt is empty
void QtTreeDrag::setUrlBuilder(const String& fmt, const String& queryParams)
{
    if (m_urlBuilder)
	QtClient::deleteLater(m_urlBuilder);
    if (fmt)
	m_urlBuilder = new QtUrlBuilder(this,fmt,queryParams);
    else
	m_urlBuilder = 0;
}

// Build MIME data for a list of items
QMimeData* QtTreeDrag::mimeData(const QList<QTreeWidgetItem*> items) const
{
    if (!m_urlBuilder)
	return 0;
    int n = items.size();
    if (n < 1)
	return 0;
    QList<QUrl> urls;
    for (int i = 0; i < n; i++) {
	QtTreeItem* it = static_cast<QtTreeItem*>(items[i]);
	QUrl url = m_urlBuilder->build(*it);
	if (!url.isEmpty())
	    urls.append(url);
    }
    QMimeData* data = new QMimeData;
    if (urls.size() > 0)
	data->setUrls(urls);
    return data;
}


//
// QtTreeItemProps
//
// Set a button's action, create if it not found
bool QtTreeItemProps::setPaintButtonAction(const String& name, const String& action)
{
    QtPaintButtonDesc* b = QtPaintButtonDesc::find(m_paintItemsDesc,name);
    if (b)
	b->m_params.assign(action);
    return b != 0;
}

// Set a button's parameter, create it if not found
bool QtTreeItemProps::setPaintButtonParam(const String& name, const String& param,
    const String& value)
{
    if (!(name && param))
	return false;
    QtPaintButtonDesc* b = QtPaintButtonDesc::find(m_paintItemsDesc,name);
    if (!b)
	return false;
    if (param == YSTRING("_yate_iconsize"))
	setSize(b->m_iconSize,value);
    else if (param == YSTRING("_yate_size"))
	setSize(b->m_size,value);
    else
	b->m_params.setParam(param,value);
    return true;
}


//
// QtTreeItem
//
QtTreeItem::QtTreeItem(const char* id, int type, const char* text, bool storeExp)
    : QTreeWidgetItem(type),
    NamedList(id),
    m_storeExp(storeExp),
    m_heightDelta(0),
    m_filtered(true),
    m_extraPaintRight(0)
{
    if (!TelEngine::null(text))
	QTreeWidgetItem::setText(0,QtClient::setUtf8(text));
    XDebug(ClientDriver::self(),DebugAll,"QtTreeItem(%s) type=%d [%p]",id,type,this);
}

QtTreeItem::~QtTreeItem()
{
    TelEngine::destruct(m_extraPaintRight);
    XDebug(ClientDriver::self(),DebugAll,"~QtTreeItem(%s) type=%d [%p]",c_str(),type(),this);
}

// Set a column's icon from a list of parameter cname_image
void QtTreeItem::setImage(int col, const String& cname, const NamedList& list, int role)
{
    String* s = cname ? list.getParam(cname + "_image") : 0;
    if (!s)
	return;
    if (role <= Qt::UserRole)
	QTreeWidgetItem::setIcon(col,QIcon(QtClient::setUtf8(*s)));
    else
	setData(col,role,QtClient::setUtf8(*s));
}

// Update item filtered flag
bool QtTreeItem::setFilter(const NamedList* filter)
{
    if (!filter) {
	m_filtered = true;
	return true;
    }
    int params = 0;
    m_filtered = false;
    NamedIterator iter(*this);
    for (const NamedString* ns = 0; 0 != (ns = iter.get()); params++) {
	if (!*ns)
	    continue;
	const String& match = (*filter)[ns->name()];
	if (ns->find(match) >= 0) {
	    m_filtered = true;
	    break;
	}
    }
    if (!params)
	m_filtered = true;
    return m_filtered;
}

// Set extra data to paint on right side of the item
void QtTreeItem::setExtraPaintRight(QtPaintItems* obj)
{
    TelEngine::destruct(m_extraPaintRight);
    m_extraPaintRight = obj;
    QVariant var;
    if (m_extraPaintRight)
	var = QtRefObjectHolder::setVariant(m_extraPaintRight);
    setData(0,QtCustomTree::RoleQtDrawItems,var);
}

// Set extra paint buttons on right side of the item
void QtTreeItem::setExtraPaintRightButtons(const String& list, QtTreeItemProps* props)
{
    if (!list) {
	setExtraPaintRight(0);
	return;
    }
    QtPaintItems* items = new QtPaintItems(list);
    if (props) {
	ObjList* pList = list.split(',');
	for (ObjList* o = pList->skipNull(); o; o = o->skipNext()) {
	    String* s = static_cast<String*>(o->get());
	    QtPaintButtonDesc* b = QtPaintButtonDesc::find(props->m_paintItemsDesc,*s,false);
	    if (b)
		items->append(*b);
	}
	TelEngine::destruct(pList);
    }
    items->itemsAdded();
    setExtraPaintRight(items);
}


/*
 * QtCustomTree
 */
QtCustomTree::QtCustomTree(const char* name, const NamedList& params, QWidget* parent,
    bool applyParams)
    : QtTree(name,parent),
    m_notifyItemChanged(false),
    m_hasCheckableCols(false),
    m_menu(0),
    m_autoExpand(false),
    m_rowHeight(-1),
    m_changing(0),
    m_filter(0),
    m_haveWidgets(false),
    m_haveDrawQtItems(false),
    m_setCurrentColumn(-1),
    m_drop(0),
    m_acceptDropOnEmpty(QtDrop::Ask),
    m_drag(0),
    m_drawBranches(false),
    m_timerTriggerSelect(0),
    m_lastItemDrawHover(0)
{
    setIndentation(0);
    setUniformRowHeights(false);
    setFrameShape(QFrame::NoFrame);
    setRootIsDecorated(false);
    // Add item props translation
    addItemType(QTreeWidgetItem::Type,"default");
    NamedIterator iter(params);
    int typeN = 0;
    for (const NamedString* ns = 0; 0 != (ns = iter.get());) {
	if (ns->name() == YSTRING("buildprops"))
	    // Build properties
	    QtClient::buildProps(this,*ns);
	else if (ns->name() == YSTRING("_yate_tree_additemtype")) {
	    // Add item types
	    if (*ns)
		addItemType(TypeCount + typeN++,*ns);
	}
	else if (ns->name() == YSTRING("vertical_scroll_policy")) {
	    // Vertical scroll policy
	    if (*ns == YSTRING("item"))
		QTreeWidget::setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
	    else if (*ns == YSTRING("pixel"))
		QTreeWidget::setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	}
	else if (ns->name() == YSTRING("_yate_set_draganddrop")) {
	    // Drag & Drop
	    bool drag = false;
	    bool drop = false;
	    QtDragAndDrop::checkEnable(*ns,drag,drop);
	    if (drag) {
		m_drag = new QtTreeDrag(this,&params);
		setDragEnabled(true);
	    }
	    if (drop) {
		m_drop = new QtListDrop(this,&params);
		setAcceptDrops(true);
	    }
	}
	else if (ns->name() == YSTRING("_yate_widgetattributes"))
	    QtClient::setWidgetAttributes(this,*ns);
	else if (ns->name() == YSTRING("_yate_set_currentcolumn"))
	    // Current column to set when index changes
	    m_setCurrentColumn = getColumnNo(*ns);
	else if (ns->name() == YSTRING("_yate_busywidget"))
	    QtClient::buildBusy(this,this,*ns,params);
	else if (ns->name() == YSTRING("property:rootIsDecorated"))
	    m_drawBranches = true;
    }
    QTreeWidgetItem* hdr = headerItem();
    if (hdr) {
	String* columns = params.getParam("columns");
	if (TelEngine::null(columns))
	    hdr->setHidden(true);
	else {
	    QHeaderView* header = QTreeView::header();
	    ObjList* id = columns->split(',',false);
	    ObjList* title = params["columns.title"].split(',',true);
	    ObjList* width = params["columns.width"].split(',',true);
	    ObjList* sizeMode = params["columns.resize"].split(',',true);
	    ObjList* check = params["columns.check"].split(',',false);
	    ObjList* emptyTitle = params["columns.allowemptytitle"].split(',',false);
	    setColumnCount(id->count());
	    int n = 0;
	    for (ObjList* o = id->skipNull(); o; o = o->skipNext(), n++) {
		String* name = static_cast<String*>(o->get());
		String caption = objListItem(title,n);
		if (!caption) {
		    String tmp = *name;
		    if (!emptyTitle->find(tmp.toLower()))
			caption = *name;
		}
		hdr->setText(n,QtClient::setUtf8(caption));
		hdr->setData(n,RoleId,QtClient::setUtf8(name->toLower()));
		int ww = objListItem(width,n).toInteger(-1);
		if (ww > 0)
		    setColumnWidth(n,ww);
		if (check->find(*name)) {
		    hdr->setData(n,RoleCheckable,QVariant(true));
		    m_hasCheckableCols = true;
		}
		// Header
		if (!header)
		    continue;
		const String& szMode = header ? objListItem(sizeMode,n) : String::empty();
		if (szMode == "fixed")
		    header->setResizeMode(n,QHeaderView::Fixed);
		else if (szMode == "stretch")
		    header->setResizeMode(n,QHeaderView::Stretch);
		else if (szMode == "contents")
		    header->setResizeMode(n,QHeaderView::ResizeToContents);
		else
		    header->setResizeMode(n,QHeaderView::Interactive);
	    }
	    TelEngine::destruct(id);
	    TelEngine::destruct(title);
	    TelEngine::destruct(width);
	    TelEngine::destruct(sizeMode);
	    TelEngine::destruct(check);
	    TelEngine::destruct(emptyTitle);
	}
    }
    // Create item delegates
    if (!s_delegateCommon) {
	s_delegateCommon.assign(" ");
	s_delegateCommon.addParam("role_display",String(RoleHtmlDelegate));
	s_delegateCommon.addParam("role_image",String(RoleImage));
	s_delegateCommon.addParam("role_background",String(RoleBackground));
	s_delegateCommon.addParam("role_margins",String(RoleMargins));
	s_delegateCommon.addParam("role_qtdrawitems",String(RoleQtDrawItems));
    }
    QList<QAbstractItemDelegate*> dlgs = QtItemDelegate::buildDelegates(this,params,&s_delegateCommon);
    QStringList cNames;
    for (int i = 0; i < dlgs.size(); i++) {
	QtItemDelegate* dlg = qobject_cast<QtItemDelegate*>(dlgs[i]);
	if (!dlg) {
	    delete dlgs[i];
	    continue;
	}
	if (cNames.size() < 1)
	    cNames = columnIDs();
	dlg->updateColumns(cNames);
	QList<int>& cols = dlg->columns();
	for (int i = 0; i < cols.size(); i++)
	    setItemDelegateForColumn(cols[i],dlg);
	if (cols.size() < 1)
	    setItemDelegate(dlg);
    }
    if (hdr && !dlgs.size()) {
	String* htmlDlg = params.getParam("htmldelegate");
	if (!TelEngine::null(htmlDlg)) {
	    ObjList* l = htmlDlg->split(',',false);
	    for (ObjList* o = l->skipNull(); o; o = o->skipNext()) {
		String* s = static_cast<String*>(o->get());
		int col = s->toInteger(-1);
		if (col < 0)
		    col = getColumn(*s);
		if (col < 0 || col >= columnCount())
		    continue;
		hdr->setData(col,RoleHtmlDelegate,true);
		String prefix;
		prefix << name << ".htmldelegate." << col;
		NamedList pp(prefix);
		pp.copySubParams(params,String("delegateparam.") + *s + ".");
		pp.setParam(prefix + ".role_display",String(RoleHtmlDelegate));
		pp.setParam(prefix + ".role_image",String(RoleImage));
		pp.setParam(prefix + ".role_background",String(RoleBackground));
		pp.setParam(prefix + ".role_margins",String(RoleMargins));
		pp.setParam(prefix + ".role_qtdrawitems",String(RoleQtDrawItems));
		QtHtmlItemDelegate* dlg = new QtHtmlItemDelegate(this,pp);
		XDebug(ClientDriver::self(),DebugNote,
		    "QtCustomTree(%s) setting html item delegate (%p,%s) for column %d [%p]",
		    name,dlg,dlg->toString().c_str(),col,this);
		setItemDelegateForColumn(col,dlg);
	    }
	    TelEngine::destruct(l);
	}
    }
    // Grid
    m_gridDraw.setPen(params);
    // Connect signals
    QtClient::connectObjects(this,SIGNAL(itemSelectionChanged()),
	this,SLOT(itemSelChangedSlot()));
    QtClient::connectObjects(this,SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)),
	this,SLOT(itemDoubleClickedSlot(QTreeWidgetItem*,int)));
    QtClient::connectObjects(this,SIGNAL(itemActivated(QTreeWidgetItem*,int)),
	this,SLOT(itemDoubleClickedSlot(QTreeWidgetItem*,int)));
    QtClient::connectObjects(this,SIGNAL(itemExpanded(QTreeWidgetItem*)),
	this,SLOT(itemExpandedSlot(QTreeWidgetItem*)));
    QtClient::connectObjects(this,SIGNAL(itemCollapsed(QTreeWidgetItem*)),
	this,SLOT(itemCollapsedSlot(QTreeWidgetItem*)));
    QtClient::connectObjects(this,SIGNAL(itemChanged(QTreeWidgetItem*,int)),
	this,SLOT(itemChangedSlot(QTreeWidgetItem*,int)));
    // Set params
    applyItemViewProps(params);
    if (applyParams)
	setParams(params);
}

// Destructor
QtCustomTree::~QtCustomTree()
{
    TelEngine::destruct(m_filter);
}

// Method re-implemented from QTreeWidget.
// Draw item grid if set
void QtCustomTree::drawRow(QPainter* p, const QStyleOptionViewItem& opt,
    const QModelIndex& idx) const
{
    QTreeWidget::drawRow(p,opt,idx);
    if (m_gridDraw.flag(QtCellGridDraw::Pos)) {
	p->save();
	int row = idx.row();
	int lastCol = columnCount() - 1;
	for (int i = 0; i <= lastCol; i++) {
	    QModelIndex s = idx.sibling(row,i);
	    if (s.isValid()) {
		QRect r = visualRect(s);
		m_gridDraw.draw(p,r,!row,!i,false,i == lastCol);
	    }
	}
	p->restore();
    }
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
    SafeInt safeChg(&m_changing);
    bool ok = QtUIWidget::setParams(params);
    ok = QtUIWidget::setParams(this,params) && ok;
    buildMenu(m_menu,params.getParam(YSTRING("menu")));
    NamedString* filter = params.getParam(YSTRING("filter"));
    if (filter) {
	TelEngine::destruct(m_filter);
	NamedList* p = YOBJECT(NamedList,filter);
	if (p && p->count())
	    m_filter = new NamedList(*p);
	checkItemFilter();
    }
    return ok;
}

// Retrieve an item
bool QtCustomTree::getTableRow(const String& item, NamedList* data)
{
    SafeInt safeChg(&m_changing);
    QtTreeItem* it = find(item);
    DDebug(ClientDriver::self(),DebugAll,"QtCustomTree(%s)::getTableRow(%s) found=%p [%p]",
	name().c_str(),item.c_str(),it,this);
    if (!it)
	return false;
    if (data) {
	data->copyParams(*it);
	// Get checked items
	if (m_hasCheckableCols) {
	    QTreeWidgetItem* hdr = headerItem();
	    int n = hdr ? columnCount() : 0;
	    for (int i = 0; i < n; i++) {
		if (!hdr->data(i,RoleCheckable).toBool())
		    continue;
		String id;
		getItemData(id,*hdr,i);
		if (!id)
		    continue;
		bool checked = it->checkState(i) != Qt::Unchecked;
		data->setParam("check:" + id,String::boolText(checked));
	    }
	}
	QWidget* w = itemWidget(it,0);
	if (w)
	    getParams(w,*data);
    }
    return true;
}

bool QtCustomTree::setTableRow(const String& item, const NamedList* data)
{
    DDebug(ClientDriver::self(),DebugAll,"QtCustomTree(%s)::setTableRow(%s,%p) [%p]",
	name().c_str(),item.c_str(),data,this);
    QtTreeItem* it = find(item);
    if (!it)
	return false;
    if (!data)
	return true;
    SafeTree tree(this);
    SafeInt safeChg(&m_changing);
    return updateItem(*it,*data);
}

// Add a new account or contact
bool QtCustomTree::addTableRow(const String& item, const NamedList* data, bool atStart)
{
    DDebug(ClientDriver::self(),DebugAll,"QtCustomTree(%s)::addTableRow(%s,%p,%u) [%p]",
	name().c_str(),item.c_str(),data,atStart,this);
    if (!data)
	return false;
    if (find(item))
	return false;
    SafeTree tree(this);
    SafeInt safeChg(&m_changing);
    QtTreeItem* parent = 0;
    int type = QTreeWidgetItem::Type;
    if (data) {
	type = itemType((*data)["item_type"]);
	const String& pName = (*data)["parent"];
	if (pName) {
	    parent = find(pName);
	    if (!parent) {
		Debug(ClientDriver::self(),DebugAll,
		    "QtCustomTree(%s)::addTableRow(%s,%p,%u) parent '%s' not found [%p]",
		    name().c_str(),item.c_str(),data,atStart,pName.c_str(),this);
		return false;
	    }
	}
    }
    QtTreeItem* it = new QtTreeItem(item,type);
    if (data)
	it->copyParams(*data);
    if (addChild(it,atStart,parent))
	return !data || updateItem(*it,*data);
    TelEngine::destruct(it);
    return false;
}

// Remove an item from tree
bool QtCustomTree::delTableRow(const String& item)
{
    if (!item)
	return false;
    SafeInt safeChg(&m_changing);
    QtTreeItem* it = find(item);
    DDebug(ClientDriver::self(),DebugAll,"QtCustomTree(%s)::delTableRow(%s) found=%p [%p]",
	name().c_str(),item.c_str(),it,this);
    if (!it)
	return false;
    removeItem(it);
    return true;
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
    SafeInt safeChg(&m_changing);
    DDebug(ClientDriver::self(),DebugAll,"QtCustomTree(%s)::updateTableRows() [%p]",
	name().c_str(),this);
    SafeTree tree(this);
    scheduleDelayedItemsLayout();
    QList<QTreeWidgetItem*> removed;
    bool ok = false;
    NamedIterator iter(*data);
    for (const NamedString* ns = 0; 0 != (ns = iter.get());) {
	if (!ns->name())
	    continue;
	QtTreeItem* item = find(ns->name());
	if (!ns->null()) {
	    NamedList* params = YOBJECT(NamedList,ns);
	    if (!params) {
		ok = (0 != item) || ok;
		continue;
	    }
	    if (item)
		ok = updateItem(*item,*params) || ok;
	    else if (ns->toBoolean())
		ok = addTableRow(ns->name(),params,atStart) || ok;
	}
	else if (item) {
	    removed.append(item);
	    ok = true;
	}
    }
    removeItems(removed);
    executeDelayedItemsLayout();
    return ok;
}

// Retrieve the current selection
bool QtCustomTree::setSelect(const String& item)
{
    QtTreeItem* it = item ? find(item) : 0;
    DDebug(ClientDriver::self(),DebugAll,"QtCustomTree(%s)::setSelect(%s) found=%p [%p]",
	name().c_str(),item.c_str(),it,this);
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
    DDebug(ClientDriver::self(),DebugAll,"QtCustomTree(%s)::getSelect(%s) found=%u [%p]",
	name().c_str(),item.c_str(),ok,this);
    if (ok)
	item = (static_cast<QtTreeItem*>(list[0]))->id();
    return ok;
}

// Retrieve multiple selection
bool QtCustomTree::getSelect(NamedList& items)
{
    QList<QTreeWidgetItem*> sel = selectedItems();
    DDebug(ClientDriver::self(),DebugAll,"QtCustomTree(%s)::getSelect(%p) found=%u [%p]",
	name().c_str(),&items,sel.size(),this);
    addItems(items,sel);
    return 0 != sel.size();
}

// Remove all items from tree
bool QtCustomTree::clearTable()
{
    DDebug(ClientDriver::self(),DebugAll,"QtCustomTree(%s)::clearTable() [%p]",
	name().c_str(),this);
    SafeInt safeChg(&m_changing);
    QTreeWidget::clear();
    return true;
}

// Catch item selection changed signal
void QtCustomTree::itemSelChangedSlot()
{
    stopSelectTriggerTimer();
    QList<QTreeWidgetItem*> sel = selectedItems();
    int nSel = sel.size();
    DDebug(ClientDriver::self(),DebugAll,
	"QtCustomTree(%s)::itemSelChangedSlot() sel=%d [%p]",
	name().c_str(),nSel,this);
    if (m_haveWidgets) {
	for (int i = 0; i < nSel; i++)
	    applyStyleSheet(static_cast<QtTreeItem*>(sel[i]),true);
    }
    if (nSel <= 0)
	onSelect(this,&(String::empty()));
    else if (nSel == 1)
	onSelect(this,&(static_cast<QtTreeItem*>(sel[0])->toString()));
    else {
	NamedList list("");
	addItems(list,sel);
	onSelectMultiple(this,&list);
    }
}

// Re-implemented from QTreeWidget
void QtCustomTree::timerEvent(QTimerEvent* ev)
{
    if (m_timerTriggerSelect && ev->timerId() == m_timerTriggerSelect) {
	stopSelectTriggerTimer();
	itemSelChangedSlot();
	return;
    }
    QtTree::timerEvent(ev);
}

// Re-implemented from QTreeWidget
void QtCustomTree::drawBranches(QPainter* painter, const QRect& rect,
    const QModelIndex& index) const
{
    if (m_drawBranches)
	QtTree::drawBranches(painter,rect,index);
}

// Re-implemented from QTreeWidget
QMimeData* QtCustomTree::mimeData(const QList<QTreeWidgetItem*> items) const
{
    QMimeData* data = m_drag ? m_drag->mimeData(items) : 0;
    return data ? data : QtTree::mimeData(items);
}

// Re-implemented from QAbstractItemView
void QtCustomTree::selectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
{
    QTreeWidget::selectionChanged(selected,deselected);
    QList<QtTreeItem*> unsel;
    QModelIndexList unselIndexes = deselected.indexes();
    if (unselIndexes.size() > 0)
	unsel = findItems(unselIndexes);
    DDebug(ClientDriver::self(),DebugAll,
	"QtCustomTree(%s)::onSelChanged() desel=%d [%p]",
	name().c_str(),unsel.size(),this);
    if (m_haveWidgets)
	for (int i = 0; i < unsel.size(); i++)
	    applyStyleSheet(unsel[i],false);
}

// Re-implemented from QAbstractItemView
void QtCustomTree::currentChanged(const QModelIndex& current, const QModelIndex& previous)
{
    QtTree::currentChanged(current,previous);
    if (m_setCurrentColumn >= 0 && m_setCurrentColumn != current.column() &&
	m_setCurrentColumn < columnCount()) {
	QTreeWidgetItem* it = itemFromIndex(current);
	if (it) {
	    QModelIndex idx = indexFromItem(it,m_setCurrentColumn);
	    if (idx.isValid())
		setCurrentIndex(idx);
	}
    }
}

// Re-implemented from QWidget
void QtCustomTree::dragEnterEvent(QDragEnterEvent* e)
{
    if (m_drop)
	handleDropEvent(e);
#ifdef XDEBUG
    String tmp = " ";
    QtClient::dumpMime(tmp,e->mimeData());
    Debug(ClientDriver::self(),DebugAll,"QtCustomTree(%s) DRAG ENTER MIME: [%p]%s",
	name().c_str(),this,tmp.safe());
#endif
}

// Re-implemented from QWidget
void QtCustomTree::dropEvent(QDropEvent* e)
{
    if (m_drop && m_drop->started())
	handleDropEvent(e);
}

// Re-implemented from QWidget
void QtCustomTree::dragMoveEvent(QDragMoveEvent* e)
{
    if (m_drop && m_drop->started())
	handleDropEvent(e);
}

// Re-implemented from QWidget
void QtCustomTree::dragLeaveEvent(QDragLeaveEvent* e)
{
    if (m_drop && m_drop->started())
	m_drop->reset();
}

// Re-implemented from QWidget
void QtCustomTree::mouseMoveEvent(QMouseEvent* e)
{
    QtTree::mouseMoveEvent(e);
    if (m_haveDrawQtItems) {
	QtTreeItem* it = static_cast<QtTreeItem*>(itemAt(e->pos()));
	if (m_lastItemDrawHover && m_lastItemDrawHover != it &&
	    m_lastItemDrawHover->extraPaintRight() &&
	    m_lastItemDrawHover->extraPaintRight()->setHover(false)) {
	    m_lastItemDrawHover->extraPaintRight()->setPressed(false);
	    QtTree::repaint(m_lastItemDrawHover->extraPaintRight()->displayRect());
	}
	if (it && it->extraPaintRight()) {
	    if (it->extraPaintRight()->displayRect().contains(e->pos())) {
		if (it->extraPaintRight()->setHover(e->pos()))
		    QtTree::repaint(it->extraPaintRight()->displayRect());
	    }
	    else if (it->extraPaintRight()->setHover(false))
		QtTree::repaint(it->extraPaintRight()->displayRect());
	}
	m_lastItemDrawHover = it;
    }
}

// Re-implemented from QWidget
void QtCustomTree::mousePressEvent(QMouseEvent* e)
{
    QtTree::mousePressEvent(e);
    if (e->button() == Qt::LeftButton &&
	m_lastItemDrawHover && m_lastItemDrawHover->extraPaintRight() &&
	m_lastItemDrawHover->extraPaintRight()->displayRect().contains(e->pos()) &&
	m_lastItemDrawHover->extraPaintRight()->mousePressed(true,e->pos()))
	QtTree::repaint(m_lastItemDrawHover->extraPaintRight()->displayRect());
}

// Re-implemented from QWidget
void QtCustomTree::mouseReleaseEvent(QMouseEvent* e)
{
    QtTree::mouseReleaseEvent(e);
    if (e->button() == Qt::LeftButton &&
	m_lastItemDrawHover && m_lastItemDrawHover->extraPaintRight()) {
	String action;
	if (m_lastItemDrawHover->extraPaintRight()->mousePressed(false,e->pos(),&action)) {
	    if (action)
		triggerAction(m_lastItemDrawHover->id(),action,this);
	    QtTree::repaint(m_lastItemDrawHover->extraPaintRight()->displayRect());
	}
    }
}

// Re-implemented from QTreeView
void QtCustomTree::rowsAboutToBeRemoved(const QModelIndex& parent, int start, int end)
{
    if (m_lastItemDrawHover) {
	QModelIndex idx = indexFromItem(m_lastItemDrawHover);
	if (idx.isValid() && idx.row() >= start && idx.row() <= end)
	    m_lastItemDrawHover = 0;
    }
    QtTree::rowsAboutToBeRemoved(parent,start,end);
}

// Retrieve tree sorting
QString QtCustomTree::getSorting()
{
    String t;
    QHeaderView* h = isSortingEnabled() ? QTreeView::header() : 0;
    if (h) {
	int col = h->sortIndicatorSection();
	int sort = h->sortIndicatorOrder();
	if (col >= 0 && col < columnCount()) {
	    String id;
	    QTreeWidgetItem* hdr = headerItem();
	    if (hdr)
		getItemData(id,*hdr,col);
	    t << (id ? id : String(col)) << "," << String::boolText(sort == Qt::AscendingOrder);
	}
    }
    return QtClient::setUtf8(t);
}

// Set tree sorting
void QtCustomTree::updateSorting(const String& key, Qt::SortOrder sort)
{
    SafeInt safeChg(&m_changing);
    QHeaderView* h = QTreeView::header();
    if (!h)
	return;
    int col = key.toInteger(-1);
    if (col < 0)
	col = getColumn(key);
    if (col >= 0 && col < columnCount())
	h->setSortIndicator(col,sort);
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
    DDebug(ClientDriver::self(),DebugAll,"QtCustomTree(%s)::getOptions() [%p]",
	name().c_str(),this);
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

// Retrieve model index for a given item
QModelIndex QtCustomTree::modelIndex(const String& item, const String* what)
{
    int col = TelEngine::null(what) ? 0 : getColumn(*what);
    if (col < 0)
	return QModelIndex();
    QtTreeItem* it = find(item);
    if (it)
	return indexFromItem(it,col);
    return QModelIndex();
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

// Find all tree items from model
QList<QtTreeItem*> QtCustomTree::findItems(QModelIndexList list)
{
    QList<QtTreeItem*> l;
    QTreeWidgetItem* root = invisibleRootItem();
    if (!root)
	return l;
    for (int i = 0; i < list.size(); i++) {
	QModelIndex& idx = list[i];
	if (!idx.isValid())
	    continue;
	QtTreeItem* it = static_cast<QtTreeItem*>(itemFromIndex(idx));
	if (it && !l.contains(it))
	    l.append(it);
    }
    return l;
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
    SafeInt safeChg(&m_changing);
    QTreeWidgetItem* root = parent ? static_cast<QTreeWidgetItem*>(parent) : invisibleRootItem();
    if (!root)
	return 0;
    DDebug(ClientDriver::self(),DebugAll,
	"QtCustomTree(%s) adding child '%s' type=%d parent=%p pos=%d",
	name().c_str(),child->id().c_str(),child->type(),parent,pos);
    setItemRowHeight(child);
    if (pos < 0 || pos >= root->childCount())
	root->addChild(child);
    else
	root->insertChild(pos,child);
    setupItem(child);
    itemAdded(*child,parent);
    return child;
}

// Add a list of children to a given item
void QtCustomTree::addChildren(QList<QTreeWidgetItem*> list, int pos, QtTreeItem* parent)
{
    SafeInt safeChg(&m_changing);
    QTreeWidgetItem* root = parent ? static_cast<QTreeWidgetItem*>(parent) : invisibleRootItem();
    if (!root)
	return;
    for (int i = 0; i < list.size(); i++)
	setItemRowHeight(list[i]);
    if (pos < 0 || pos >= root->childCount())
	root->addChildren(list);
    else
	root->insertChildren(pos,list);
    for (int i = 0; i < list.size(); i++) {
	QtTreeItem* item = static_cast<QtTreeItem*>(list[i]);
	if (!item)
	    continue;
	setupItem(item);
	itemAdded(*item,parent);
    }
}

// Setup an item. Load its widget if not found
void QtCustomTree::setupItem(QtTreeItem* item)
{
    if (!item)
	return;
    SafeInt safeChg(&m_changing);
    // Set widget
    QWidget* w = itemWidget(item,0);
    if (!w) {
	w = loadWidgetType(this,item->id(),itemPropsName(item->type()));
	if (w) {
	    m_haveWidgets = true;
	    w->setAutoFillBackground(true);
	    XDebug(ClientDriver::self(),DebugAll,
		"QtCustomTree(%s) set widget (%p,%s) for child '%s' [%p]",
		name().c_str(),w,YQT_OBJECT_NAME(w),item->id().c_str(),this);
	    // Adjust widget to row height if configured,
	    // or row height to widget otherwise
	    QSize sz = item->sizeHint(0);
	    int h = getItemRowHeight(item->type());
	    if (h > 0)
		w->setFixedHeight(sz.height() + item->m_heightDelta);
	    else {
		sz.setHeight(w->height());
		item->setSizeHint(0,sz);
	    }
	    setItemWidget(item,0,w);
	    applyStyleSheet(item,item->isSelected());
	}
    }
    // Set checkable columns
    uncheckItem(*item);
}

// Set and item's row height hint
void QtCustomTree::setItemRowHeight(QTreeWidgetItem* item)
{
    if (!item)
	return;
    int h = getItemRowHeight(item->type());
    if (h <= 0)
	return;
    QSize sz = item->sizeHint(0);
    sz.setHeight(h + (static_cast<QtTreeItem*>(item))->m_heightDelta);
    item->setSizeHint(0,sz);
    QWidget* w = itemWidget(item,0);
    if (w)
	w->setFixedHeight(sz.height());
}

// Retrieve a list with column IDs
QStringList QtCustomTree::columnIDs()
{
    QStringList tmp;
    QTreeWidgetItem* hdr = headerItem();
    int n = hdr ? columnCount() : 0;
    for (int i = 0; i < n; i++)
	tmp.append(hdr->data(i,RoleId).toString());
    return tmp;
}

// Retrieve a column name
bool QtCustomTree::getColumnName(String& buf, int col)
{
    QTreeWidgetItem* hdr = 0;
    if (col >= 0 && col < columnCount())
	hdr = headerItem();
    if (!hdr)
	return false;
    getItemData(buf,*hdr,col);
    return true;
}

// Retrieve a column by it's id
int QtCustomTree::getColumn(const String& id)
{
    QTreeWidgetItem* hdr = headerItem();
    int n = hdr ? columnCount() : 0;
    for (int i = 0; i < n; i++) {
	String tmp;
	getItemData(tmp,*hdr,i);
	if (tmp == id)
	    return i;
    }
    return -1;
}

// Show or hide empty children.
void QtCustomTree::showEmptyChildren(bool show, QtTreeItem* parent)
{
    QTreeWidgetItem* root = parent ? static_cast<QTreeWidgetItem*>(parent) : invisibleRootItem();
    if (!root)
	return;
    SafeTree tree(this);
    SafeInt safeChg(&m_changing);
    int n = root->childCount();
    for (int i = 0; i < n; i++) {
	QtTreeItem* item = static_cast<QtTreeItem*>(root->child(i));
	if (!item)
	    continue;
	if (show) {
	    showItem(*item,true);
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
	showItem(*item,child != 0);
    }
}

// Set the expanded/collapsed image of an item
void QtCustomTree::setStateImage(QtTreeItem& item, QtTreeItemProps* p)
{
    if (!p)
	p = treeItemProps(item.type());
    if (!(p && p->m_stateWidget))
	return;
    SafeInt safeChg(&m_changing);
    NamedList tmp("");
    const String& img = item.isExpanded() ? p->m_stateExpandedImg : p->m_stateCollapsedImg;
    tmp.addParam("image:" + p->m_stateWidget,img);
    tmp.addParam(p->m_stateWidget + "_image",img);
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

// Set an item props accept drop
void QtCustomTree::setItemAcceptDrop(QString value)
{
    String tmp;
    QtUIWidgetItemProps* p = getItemProps(value,tmp);
    p->m_acceptDrop = QtDrop::acceptDropType(tmp,QtDrop::None);
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

// Set an item props height
void QtCustomTree::setItemHeight(QString value)
{
    String tmp;
    QtTreeItemProps* p = YOBJECT(QtTreeItemProps,getItemProps(value,tmp));
    if (p)
	p->m_height = tmp.toInteger(-1);
}

// Set an item props background
void QtCustomTree::setItemBg(QString value)
{
    String tmp;
    QtTreeItemProps* p = YOBJECT(QtTreeItemProps,getItemProps(value,tmp));
    if (!p)
	return;
    if (tmp) {
	if (tmp[0] == '#')
	    p->m_bg = QBrush(QColor(tmp.substr(1).toInteger(0,16)));
	else if (tmp.startSkip("color:",false))
	    p->m_bg = QBrush(QColor(tmp.c_str()));
	else
	    p->m_bg = QBrush();
    }
    else
	p->m_bg = QBrush();
}

// Set an item props margins
// Order: left,top,right,bottom
void QtCustomTree::setItemMargins(QString value)
{
    String tmp;
    QtTreeItemProps* p = YOBJECT(QtTreeItemProps,getItemProps(value,tmp));
    if (!p)
	return;
    p->m_margins = QRect();
    if (!tmp)
	return;
    ObjList* list = tmp.split(',');
    int i = 0;
    for (ObjList* o = list; o; o = o->next(), i++) {
	int val = o->get() ? o->get()->toString().toInteger() : 0;
	if (i == 0)
	    p->m_margins.setLeft(val);
	else if (i == 1)
	    p->m_margins.setTop(val);
	else if (i == 2)
	    p->m_margins.setRight(val);
	else if (i == 3)
	    p->m_margins.setBottom(val);
    }
    TelEngine::destruct(list);
}

// Set an item props editable
void QtCustomTree::setItemEditable(QString value)
{
    String tmp;
    QtTreeItemProps* p = YOBJECT(QtTreeItemProps,getItemProps(value,tmp));
    if (p)
	p->m_editable = tmp.toBoolean();
}

// Set an item's paint button and action
// Format [type:][button_name:]action_name
void QtCustomTree::setItemPaintButton(QString value)
{
    String tmp;
    QtTreeItemProps* p = YOBJECT(QtTreeItemProps,getItemProps(value,tmp));
    if (!p)
	return;
    if (!tmp)
	return;
    int pos = tmp.find(':');
    if (pos < 0) {
	p->setPaintButtonAction(tmp,tmp);
	return;
    }
    String name = tmp.substr(0,pos);
    if (name)
	p->setPaintButtonAction(name,tmp.substr(pos + 1));
}

// Set an item's paint button parameter
// Format [type:]button_name:param_name[:param_value]
void QtCustomTree::setItemPaintButtonParam(QString value)
{
    String tmp;
    QtTreeItemProps* p = YOBJECT(QtTreeItemProps,getItemProps(value,tmp));
    if (!p)
	return;
    int pos = tmp.find(':');
    if (pos < 1)
	return;
    String name = tmp.substr(0,pos);
    tmp = tmp.substr(pos + 1);
    if (!tmp)
	return;
    pos = tmp.find(':');
    if (!pos)
	return;
    if (pos > 0)
	p->setPaintButtonParam(name,tmp.substr(0,pos),tmp.substr(pos + 1));
    else
	p->setPaintButtonParam(name,tmp);
}

// Retrieve a comma separated list with column widths
QString QtCustomTree::colWidths()
{
    if (!columnCount())
	return QString();
    String t;
    int cols = columnCount();
    for (int i = 0; i < cols; i++)
	t.append(String(columnWidth(i)),",");
    return QtClient::setUtf8(t);
}

// Set column widths
void QtCustomTree::setColWidths(QString widths)
{
    if (!columnCount())
	return;
    QStringList list = widths.split(",");
    for (int i = 0; i < list.size(); i++) {
	if (!list[i].length())
	    continue;
	int width = list[i].toInt();
	if (width > 0)
	    setColumnWidth(i,width);
    }
}

// Set sorting (column and order)
void QtCustomTree::setSorting(QString s)
{
    SafeInt safeChg(&m_changing);
    if (!s.length()) {
	updateSorting(String::empty(),Qt::AscendingOrder);
	return;
    }
    String key;
    String order;
    int pos = s.indexOf(QChar(','));
    if (pos >= 0) {
	QtClient::getUtf8(key,s.left(pos));
	QtClient::getUtf8(order,s.right(s.length() - pos - 1));
    }
    else
	QtClient::getUtf8(key,s);
    updateSorting(key,order.toBoolean(true) ? Qt::AscendingOrder : Qt::DescendingOrder);
}

// Retrieve items expanded status value
QString QtCustomTree::itemsExpStatus()
{
    String tmp;
    for (int i = 0; i < m_expStatus.size(); i++) {
	String val;
	val << m_expStatus[i].first.uriEscape(',') << "=" <<
	    String::boolText(m_expStatus[i].second > 0);
	tmp.append(val,",");
    }
    return QtClient::setUtf8(tmp);
}

// Set items expanded status value
void QtCustomTree::setItemsExpStatus(QString s)
{
    m_expStatus.clear();
    QStringList list = s.split(",",QString::SkipEmptyParts);
    for (int i = 0; i < list.size(); i++) {
	String id;
	String value;
	int pos = list[i].lastIndexOf('=');
	if (pos > 0) {
	    QtClient::getUtf8(id,list[i].left(pos));
	    int n = list[i].size() - pos - 1;
	    if (n)
		QtClient::getUtf8(value,list[i].right(n));
	}
	else
	    QtClient::getUtf8(id,list[i]);
	if (id) {
	    id = id.uriUnescape();
	    m_expStatus.append(QtTokenDict(id,value.toBoolean(m_autoExpand) ? 1 : 0));
	}
    }
}

// Add items as list parameter
void QtCustomTree::addItems(NamedList& dest, QList<QTreeWidgetItem*> items)
{
    for (int i = 0; i < items.size(); i++)
	dest.addParam(static_cast<QtTreeItem*>(items[i])->toString(),"");
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
    if (item->m_storeExp)
	setStoreExpStatus(item->id(),item->isExpanded());
    QtTreeItemProps* props = treeItemProps(item);
    if (props) {
	setStateImage(*item,props);
	applyItemStatistics(*item,props);
    }
}

// Process item changed signal
void QtCustomTree::onItemChanged(QtTreeItem* item, int column)
{
    if (m_changing || !m_notifyItemChanged || !item)
	return;
    NamedList p("");
    QString s = item->text(column);
    if (s.size() > 0) {
	String col;
	getColumnName(col,column);
	if (col)
	    QtClient::getUtf8(p,"text." + col,s);
    }
    closePersistentEditor(static_cast<QTreeWidgetItem*>(item),column);
    triggerAction(item->id(),"listitemchanged",this,&p);
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
    SafeInt safeChg(&m_changing);
    SafeTree safeTree(this);
    DDebug(ClientDriver::self(),DebugAll,"QtCustomTree(%s)::updateItem(%p,%s) [%p]",
	name().c_str(),&item,item.id().c_str(),this);
    bool all = (&params == &item);
    if (!all)
	item.copyParams(params);
    const NamedList& p = all ? (const NamedList&)item : params;
    QTreeWidgetItem* hdr = headerItem();
    QtTreeItemProps* props = treeItemProps(item.type());
    int n = columnCount();
    QModelIndex idx;
    for (int col = 0; col < n; col++) {
	if (!col) {
	    String* hp = params.getParam(YSTRING("_yate_itemheight_delta"));
	    if (hp) {
		item.m_heightDelta = hp->toInteger();
		setItemRowHeight(&item);
		doItemsLayout();
	    }
	    if (props) {
		String* showActions = params.getParam(YSTRING("_yate_showactions"));
		QtPaintItems* pItems = item.extraPaintRight();
		if (showActions &&
		    ((!pItems && *showActions) || (pItems && *showActions != pItems->name()))) {
		    item.setExtraPaintRightButtons(*showActions,props);
		    m_haveDrawQtItems = true;
		    setMouseTracking(true);
		}
	    }
	}
	QWidget* w = itemWidget(&item,col);
	if (w) {
	    QtUIWidget::setParams(w,p);
	    continue;
	}
	if (!hdr)
	    continue;
	String id;
	getItemData(id,*hdr,col);
	item.setText(col,id,p);
	item.setCheckState(col,id,p);
	int imageRole = Qt::UserRole;
	if (props) {
	    // Set brush
	    if (props->m_bg != QBrush())
		item.setData(col,RoleBackground,props->m_bg);
	    if (idx.isValid())
		idx = idx.sibling(idx.row(),col);
	    else
		idx = indexFromItem(&item,col);
	    QtHtmlItemDelegate* html = 0;
	    if (idx.isValid())
		html = qobject_cast<QtHtmlItemDelegate*>(itemDelegate(idx));
	    if (html) {
		// HTML delegate
		imageRole = html->roleImage();
		if (html->roleDisplayText() == RoleHtmlDelegate) {
		    QStringList qList;
		    String s = props->m_styleSheet;
		    if (s)
			replaceHtmlParams(s,item,true);
		    qList.append(QtClient::setUtf8(s));
		    s = props->m_selStyleSheet;
		    if (s) {
			replaceHtmlParams(s,item);
			qList.append(QtClient::setUtf8(s));
		    }
		    item.setData(col,RoleHtmlDelegate,qList);
		}
	    }
	}
	item.setImage(col,id,p,imageRole);
    }
    applyItemTooltip(item);
    checkItemFilter(&item,false);
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
    SafeInt safeChg(&m_changing);
    checkItemFilter(&item,false);
    bool on = m_autoExpand;
    if (item.m_storeExp) {
	int n = getStoreExpStatus(item.id());
	if (n >= 0)
	    on = (n > 0);
	else
	    setStoreExpStatus(item.id(),on);
    }
    QtTreeItemProps* props = treeItemProps(item);
    bool editable = false;
    item.setExpanded(on);
    if (props) {
	setStateImage(item,props);
	applyItemTooltip(item,props);
	applyItemStatistics(item,props);
	applyItemMargins(item,true,props);
	editable = props->m_editable;
    }
    if (editable)
	item.setFlags(item.flags() | Qt::ItemIsEditable);
    else
	item.setFlags(item.flags() & ~Qt::ItemIsEditable);
    if (parent)
	applyItemStatistics(*parent);
}

// Handle item visiblity changes
void QtCustomTree::itemVisibleChanged(QtTreeItem& item)
{
    SafeInt safeChg(&m_changing);
    // Uncheck columns for invisible item
    if (item.isHidden())
	uncheckItem(item);
}

// Check item filter
void QtCustomTree::checkItemFilter(QtTreeItem* item, bool recursive)
{
    QTreeWidgetItem* root = 0;
    if (item) {
	item->setFilter(m_filter);
	itemFilterChanged(*item);
	if (recursive)
	    root = static_cast<QTreeWidgetItem*>(item);
    }
    else if (recursive)
	root = invisibleRootItem();
    int nc = root ? root->childCount() : 0;
    for (int i = 0; i < nc; i++) {
	QtTreeItem* it = static_cast<QtTreeItem*>(root->child(i));
	checkItemFilter(it,true);
    }
}

// Handle item filter changes
void QtCustomTree::itemFilterChanged(QtTreeItem& item)
{
    showItem(item,item.filterMatched());
}

// Uncheck all checkable columns in a given item
void QtCustomTree::uncheckItem(QtTreeItem& item)
{
    if (!m_hasCheckableCols)
	return;
    SafeInt safeChg(&m_changing);
    QTreeWidgetItem* hdr = headerItem();
    int n = hdr ? columnCount() : 0;
    for (int i = 0; i < n; i++)
	if (hdr->data(i,RoleCheckable).toBool())
	    item.setCheckState(i,false);
}

// Remove an item
void QtCustomTree::removeItem(QtTreeItem* it, bool* setSelTimer)
{
    if (!it)
	return;
    bool sel = shouldSetSelTimer(*it);
    QTreeWidgetItem* parent = it->parent();
    if (parent && parent != invisibleRootItem()) {
	parent->removeChild(it);
	applyItemStatistics(*static_cast<QtTreeItem*>(parent));
    }
    TelEngine::destruct(it);
    if (setSelTimer)
	*setSelTimer = sel;
    else if (sel)
	startSelectTriggerTimer();
}

// Remove a list of items
void QtCustomTree::removeItems(QList<QTreeWidgetItem*> items)
{
    bool setSelTimer = false;
    for (int i = 0; i < items.size(); i++) {
	bool sel = false;
	removeItem(static_cast<QtTreeItem*>(items[i]),&sel);
	setSelTimer = setSelTimer || sel;
    }
    if (setSelTimer)
	startSelectTriggerTimer();
}

// Update a tree item's tooltip
void QtCustomTree::applyItemTooltip(QtTreeItem& item, QtTreeItemProps* p)
{
    if (!p)
	p = treeItemProps(item);
    if (!(p && p->m_toolTip))
	return;
    String tooltip = p->m_toolTip;
    item.replaceParams(tooltip);
    for (int n = columnCount() - 1; n >= 0; n--)
	item.setToolTip(n,QtClient::setUtf8(tooltip));
}

// Fill a list with item statistics.
void QtCustomTree::fillItemStatistics(QtTreeItem& item, NamedList& list)
{
    list.addParam("count",String(item.childCount()));
}

// Update a tree item's statistics
void QtCustomTree::applyItemStatistics(QtTreeItem& item, QtTreeItemProps* p)
{
    if (!p)
	p = treeItemProps(item);
    if (!(p && p->m_statsTemplate))
	return;
    SafeInt safeChg(&m_changing);
    String text;
    if (!item.isExpanded()) {
	text = p->m_statsTemplate;
	NamedList list("");
	fillItemStatistics(item,list);
	list.replaceParams(text);
    }
    NamedList params("");
    if (p->m_statsWidget)
	params.addParam(p->m_statsWidget,text);
    else
	params.addParam("statistics",text);
    updateItem(item,params);
}

// Update a tree item's margins
void QtCustomTree::applyItemMargins(QtTreeItem& item, bool set, QtTreeItemProps* p)
{
    if (!p)
	p = treeItemProps(item);
    if (!p)
	return;
    for (int n = columnCount() - 1; n >= 0; n--)
	item.setData(n,RoleMargins,set ? p->m_margins : QRect());
}

// Store (update) to or remove from item expanded status storage an item
void QtCustomTree::setStoreExpStatus(const String& id, bool on, bool store)
{
    if (!id)
	return;
    for (int i = 0; i < m_expStatus.size(); i++)
	if (m_expStatus[i].first == id) {
	    m_expStatus[i].second = on ? 1 : 0;
	    return;
	}
    m_expStatus.append(QtTokenDict(id,on ? 1 : 0));
}

// Retrieve the expanded status of an item from storage
int QtCustomTree::getStoreExpStatus(const String& id)
{
    if (!id)
	return -1;
    for (int i = 0; i < m_expStatus.size(); i++)
	if (m_expStatus[i].first == id)
	    return m_expStatus[i].second;
    return -1;
}

// Handle drop events
bool QtCustomTree::handleDropEvent(QDropEvent* e)
{
    if (!m_drop)
	return false;
    QDragMoveEvent* move = 0;
    QDragEnterEvent* enter = 0;
    if (e->type() == QEvent::DragMove) {
	if (!m_drop->started())
	    return false;
	move = static_cast<QDragMoveEvent*>(e);
    }
    else if (e->type() == QEvent::DragEnter) {
	if (!m_drop->start(*static_cast<QDragEnterEvent*>(e)))
	    return false;
	// Init drop accept params
	String always;
	String none;
	String ask;
	for (ObjList* o = m_itemPropsType.skipNull(); o ; o = o->skipNext()) {
	    NamedInt* ni = static_cast<NamedInt*>(o->get());
	    QtUIWidgetItemProps* p = QtUIWidget::getItemProps(*ni);
	    if (!p)
		continue;
	    if (p->m_acceptDrop == QtDrop::Always)
		always.append(*ni,",");
	    else if (p->m_acceptDrop == QtDrop::None)
		none.append(*ni,",");
	    else if (p->m_acceptDrop == QtDrop::Ask)
		ask.append(*ni,",");
	}
	m_drop->setAcceptOnEmpty(m_acceptDropOnEmpty);
	m_drop->updateAcceptType(always,QtDrop::Always);
	m_drop->updateAcceptType(none,QtDrop::None);
	m_drop->updateAcceptType(ask,QtDrop::Ask);
	enter = static_cast<QDragEnterEvent*>(e);
	move = static_cast<QDragMoveEvent*>(e);
    }
    else if (e->type() == QEvent::Drop) {
	if (!m_drop->started())
	    return false;
    }
    else
	return false;
    int acceptDrop = QtDrop::None;
    QtTreeItem* it = static_cast<QtTreeItem*>(itemAt(e->pos()));
    if (it)
	acceptDrop = m_drop->getAcceptType(itemPropsName(it->type()));
    else
	acceptDrop = m_drop->acceptOnEmpty();
    // Done if drop event
    if (e->type() == QEvent::Drop) {
	bool ok = false;
	// Notify ?
	if (acceptDrop != QtDrop::None) {
	    if (it) {
		m_drop->params().setParam(YSTRING("item"),it->toString());
		m_drop->params().setParam(YSTRING("item_type"),itemPropsName(it->type()));
	    }
	    ok = triggerAction(QtDrop::s_notifyClientDrop,m_drop->params(),this);
	}
	m_drop->reset();
	if (ok)
	    e->accept();
	else
	    e->ignore();
	return ok;
    }
    if (acceptDrop == QtDrop::Ask) {
	if (it) {
	    m_drop->params().setParam(YSTRING("item"),it->toString());
	    m_drop->params().setParam(YSTRING("item_type"),itemPropsName(it->type()));
	}
	if (triggerAction(QtDrop::s_askClientAcceptDrop,m_drop->params(),this)) {
	    if (enter && !m_drop->params().getBoolValue(YSTRING("_yate_accept_drop"),true)) {
		m_drop->reset();
		enter->ignore(rect());
		return false;
	    }
	    // Update allowed item types and empty space
	    m_drop->updateAccept(m_drop->params());
	    if (it)
		acceptDrop = m_drop->getAcceptType(itemPropsName(it->type()));
	    else
	    	acceptDrop = m_drop->acceptOnEmpty();
	}
    }
    if (it && move) {
	if (acceptDrop != QtDrop::None)
	    move->accept(QTreeWidget::visualItemRect(it));
	else
	    move->ignore(QTreeWidget::visualItemRect(it));
    }
    else if (acceptDrop != QtDrop::None)
	e->accept();
    else
	e->ignore();
    if (enter)
	enter->acceptProposedAction();
    return true;
}

// Check if an item has any selected child
bool QtCustomTree::hasSelectedChild(QtTreeItem& item)
{
    for (int i = item.childCount() - 1; i >= 0; i--) {
	QtTreeItem* ch = static_cast<QtTreeItem*>(item.child(i));
	if (ch && (ch->isSelected() || hasSelectedChild(*ch)))
	    return true;
    }
    return false;
}


/*
 * ContactList
 */
ContactList::ContactList(const char* name, const NamedList& params, QWidget* parent)
    : QtCustomTree(name,params,parent,false),
    m_flatList(true),
    m_showOffline(true),
    m_hideEmptyGroups(true),
    m_expStatusGrp(true),
    m_menuContact(0),
    m_menuChatRoom(0),
    m_sortOrder(Qt::AscendingOrder),
    m_compareNameCs(Qt::CaseSensitive)
{
    XDebug(ClientDriver::self(),DebugAll,"ContactList(%s) [%p]",name,this);
    // Add item props translation
    addItemType(TypeContact,"contact");
    addItemType(TypeChatRoom,"chatroom");
    addItemType(TypeGroup,"group");
    m_savedIndent = indentation();
    m_noGroupText = "None";
    setParams(params);
}

// Set params
bool ContactList::setParams(const NamedList& params)
{
    SafeInt safeChg(&m_changing);
    bool ok = QtCustomTree::setParams(params);
    buildMenu(m_menuContact,params.getParam("contactmenu"));
    buildMenu(m_menuChatRoom,params.getParam("chatroommenu"));
    return ok;
}

// Update a contact
bool ContactList::setTableRow(const String& item, const NamedList* data)
{
    SafeInt safeChg(&m_changing);
    DDebug(ClientDriver::self(),DebugAll,"ContactList(%s)::setTableRow(%s,%p)",
	name().c_str(),item.c_str(),data);
    ContactItem* c = findContact(item);
    if (!c)
	return false;
    if (!data)
	return true;
    SafeTree tree(this);
    bool changed = c->updateName(*data,m_compareNameCs);
    if (!changed && !m_flatList)
	changed = c->groupsWouldChange(*data);
    if (!changed)
	updateContact(item,*data);
    else
	replaceContact(*c,*data);
    listChanged();
    return true;
}

// Add a new account or contact
bool ContactList::addTableRow(const String& item, const NamedList* data, bool atStart)
{
    SafeInt safeChg(&m_changing);
    DDebug(ClientDriver::self(),DebugAll,"ContactList(%s)::addTableRow(%s,%p,%u)",
	name().c_str(),item.c_str(),data,atStart);
    if (!data)
	return false;
    if (find(item))
	return false;
    SafeTree tree(this);
    addContact(item,*data);
    listChanged();
    return true;
}

// Remove an item from tree
bool ContactList::delTableRow(const String& item)
{
    SafeInt safeChg(&m_changing);
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
    SafeInt safeChg(&m_changing);
    bool ok = false;
    QList<QTreeWidgetItem*> list;
    QTreeWidgetItem* root = invisibleRootItem();
    bool empty = root && !root->childCount();
    NamedIterator iter(*data);
    for (const NamedString* ns = 0; 0 != (ns = iter.get());) {
	if (!ns->name())
	    continue;
	if (!ns->null()) {
	    NamedList* params = YOBJECT(NamedList,ns);
	    if (!empty) {
		if (!params)
		    ok = (0 != find(ns->name())) || ok;
		else if (ns->toBoolean() || find(ns->name()))
		    ok = updateContact(ns->name(),*params) || ok;
	    }
	    else if (params)
		list.append(createContact(ns->name(),*params));
	}
	else
	    ok = removeContact(ns->name()) || ok;
    }
    if (!empty)
	listChanged();
    else {
	setContacts(list);
	ok = true;
    }
    return ok;
}

// Count online/total contacts in a group.
void ContactList::countContacts(QtTreeItem* grp, int& total, int& online)
{
    QList<QtTreeItem*> c = findItems(TypeContact,grp,true,false);
    QList<QtTreeItem*> r = findItems(TypeChatRoom,grp,true,false);
    total = c.size() + r.size();
    online = 0;
    for (int i = 0; i < c.size(); i++)
	if (!(static_cast<ContactItem*>(c[i]))->offline())
	    online++;
    for (int j = 0; j < r.size(); j++)
	if (!(static_cast<ContactItem*>(r[j]))->offline())
	    online++;
}

// Contact list changed notification
void ContactList::listChanged()
{
    // Hide empty groups
    if (!m_flatList)
	showEmptyChildren(!m_hideEmptyGroups);
    // Update contact count in groups
    if (!m_flatList) {
	QList<QtTreeItem*> grps = findItems(TypeGroup,0,true,false);
	for (int i = 0; i < grps.size(); i++) {
	    if (!grps[i])
		continue;
	    applyItemStatistics(*(grps[i]));
	}
    }
}

// Find a contact
ContactItem* ContactList::findContact(const String& id, QList<QtTreeItem*>* list)
{
    QList<QtTreeItem*> local;
    if (!list)
	list = &local;
    *list = findItems(id);
    for (int i = 0; i < list->size(); i++) {
	QtTreeItem* it = static_cast<QtTreeItem*>((*list)[i]);
	if (isContactType(it->type()) && it->id() == id)
	    return static_cast<ContactItem*>(it);
    }
    return 0;
}

// Set '_yate_nogroup_caption' property
void ContactList::setNoGroupCaption(QString value)
{
    SafeInt safeChg(&m_changing);
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
    SafeTree tree(this);
    SafeInt safeChg(&m_changing);
    TreeRestoreSel sel(this);
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
	// Make sure the list contains valid pointers
	for (int i = 0; i < c.size();)
	    if (c[i])
		i++;
	    else
		c.removeAt(i);
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
    setContacts(c);
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
    SafeTree tree(this);
    SafeInt safeChg(&m_changing);
    String sel;
    getSelect(sel);
    setCurrentItem(0);
    QList<QtTreeItem*> list = findItems(TypeContact);
    for (int i = 0; i < list.size(); i++) {
	ContactItem* c = static_cast<ContactItem*>(list[i]);
	if (!c)
	    continue;
	if (c->offline())
	    showItem(*c,m_showOffline);
    }
    listChanged();
    // Avoid selecting a hidden item
    QtTreeItem* it = sel ? find(sel) : 0;
    if (it && !it->isHidden())
	setCurrentItem(it);
}

// Retrieve tree sorting
QString ContactList::getSorting()
{
    if (!m_sortKey)
	return QtCustomTree::getSorting();
    String tmp = m_sortKey;
    tmp << "," << String::boolText(m_sortOrder == Qt::AscendingOrder);
    return QtClient::setUtf8(tmp);
}

// Set tree sorting
void ContactList::updateSorting(const String& key, Qt::SortOrder sort)
{
    SafeInt safeChg(&m_changing);
    if (!isSortingEnabled()) {
	m_sortKey = key;
	m_sortOrder = sort;
    }
    else
	QtCustomTree::updateSorting(key,sort);
}

// Optimized add. Set the whole tree
void ContactList::setContacts(QList<QTreeWidgetItem*>& list)
{
    SafeInt safeChg(&m_changing);
    // Add contacts to tree
    if (m_flatList) {
	sortContacts(list);
	addChildren(list,-1,0);
    }
    else {
	ContactItemList cil;
	for (int i = 0; i < list.size(); i++)
	    createContactTree(static_cast<ContactItem*>(list[i]),cil);
	if (cil.m_groups.size()) {
	    addChildren(cil.m_groups);
	    for (int i = 0; i < cil.m_groups.size(); i++) {
		sortContacts(cil.m_contacts[i]);
		QtTreeItem* grp = static_cast<QtTreeItem*>(cil.m_groups[i]);
		addChildren(cil.m_contacts[i],-1,grp);
	    }
	}
    }
    listChanged();
}

// Create a contact
ContactItem* ContactList::createContact(const String& id, const NamedList& params)
{
    ContactItem* c = new ContactItem(id,params);
    c->copyParams(params);
    c->updateName(params,m_compareNameCs);
    return c;
}

// Add or update a contact
bool ContactList::updateContact(const String& id, const NamedList& params)
{
    if (TelEngine::null(id))
	return false;
    DDebug(ClientDriver::self(),DebugAll,"ContactList(%s)::updateContact(%s)",
	name().c_str(),id.c_str());
    SafeInt safeChg(&m_changing);
    QList<QtTreeItem*> list;
    ContactItem* c = findContact(id,&list);
    if (!c) {
	addContact(id,params);
	return true;
    }
    bool changed = c->updateName(params,m_compareNameCs);
    if (!changed && !m_flatList)
	changed = c->groupsWouldChange(params);
    if (!changed) {
	for (int i = 0; i < list.size(); i++)
	    if (isContactType(list[i]->type()) && list[i]->id() == id)
		updateContact(*(static_cast<ContactItem*>(list[i])),params);
    }
    else
	replaceContact(*c,params);
    return true;
}

// Remove a contact from tree
bool ContactList::removeContact(const String& id)
{
    DDebug(ClientDriver::self(),DebugAll,"ContactList(%s)::removeContact(%s)",
	name().c_str(),id.c_str());
    SafeInt safeChg(&m_changing);
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
    QtCustomTree::updateItem(c,params);
    // Show/hide
    if (c.type() == TypeContact && !m_showOffline)
	showItem(c,!c.offline());
    return true;
}

// Update a contact
bool ContactList::updateItem(QtTreeItem& item, const NamedList& params)
{
    if (isContactType(item.type()))
	return updateContact(*static_cast<ContactItem*>(&item),params);
    return QtCustomTree::updateItem(item,params);
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
    if (item->type() == TypeChatRoom) {
	if (m_menuChatRoom)
	    return m_menuChatRoom;
    }
    else if (item->type() == TypeGroup)
	return m_menu;
    return QtCustomTree::contextMenu(item);
}

// Item added notification
void ContactList::itemAdded(QtTreeItem& item, QtTreeItem* parent)
{
    SafeInt safeChg(&m_changing);
    QtCustomTree::itemAdded(item,parent);
    DDebug(ClientDriver::self(),DebugAll,"ContactList(%s)::itemAdded(%p,%p) type=%d id=%s",
	name().c_str(),&item,parent,item.type(),item.id().c_str());
    if (isContactType(item.type())) {
	ContactItem* c = static_cast<ContactItem*>(&item);
	updateContact(*c,*c);
	return;
    }
    if (item.type() != TypeGroup)
	return;
    // Set group name
    QWidget* w = itemWidget(&item,0);
    if (!w) {
	QtCustomTree::updateItem(item,item);
	return;
    }
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

// Fill a list with item statistics
void ContactList::fillItemStatistics(QtTreeItem& item, NamedList& list)
{
    if (item.type() != TypeGroup)
	return;
    int total = 0;
    int online = 0;
    countContacts(&item,total,online);
    list.addParam("total",String(total));
    list.addParam("online",String(online));
}

// Update a tree item's margins
void ContactList::applyItemMargins(QtTreeItem& item, bool set, QtTreeItemProps* p)
{
    set = !m_flatList && item.type() != TypeGroup;
    QtCustomTree::applyItemMargins(item,set,p);
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
    QtTreeItem* g = createGroup(grp,gText,m_expStatusGrp);
    if (!addChild(g,pos))
	TelEngine::destruct(g);
    return g;
}

// Add a contact
void ContactList::addContact(const String& id, const NamedList& params)
{
    SafeInt safeChg(&m_changing);
    ContactItem* c = createContact(id,params);
    if (m_flatList) {
	addContact(c);
	return;
    }
    ContactItemList cil;
    createContactTree(c,cil);
    for (int i = 0; i < cil.m_groups.size(); i++) {
	QtTreeItem* cg = static_cast<QtTreeItem*>(cil.m_groups[i]);
	if (cil.m_contacts[i].size()) {
	    ContactItem* item = static_cast<ContactItem*>((cil.m_contacts[i])[0]);
	    QtTreeItem* grp = getGroup(cg->id() != s_noGroupId ? cg->id() : String::empty());
	    if (grp)
		addContact(item,grp);
	    else
		TelEngine::destruct(item);
	}
	TelEngine::destruct(cg);
    }
}

// Add a contact to a specified parent
void ContactList::addContact(ContactItem* c, QtTreeItem* parent)
{
    if (!c)
	return;
    SafeInt safeChg(&m_changing);
    int pos = -1;
    if (m_sortKey == "name") {
	bool asc = (m_sortOrder == Qt::AscendingOrder);
	QTreeWidgetItem* p = parent ? (QTreeWidgetItem*)parent : invisibleRootItem();
	int n = p ? p->childCount() : 0;
	for (int i = 0; i < n; i++) {
	    ContactItem* item = static_cast<ContactItem*>(p->child(i));
	    int comp = compareStr(c->m_name,item->m_name,m_compareNameCs);
	    if (comp && (asc == (comp < 0))) {
		pos = i;
		break;
	    }
	}
    }
    QtCustomTree::addChild(c,pos,parent);
}

// Replace an existing contact. Remove it and add it again
void ContactList::replaceContact(ContactItem& c, const NamedList& params)
{
    if (!c)
	return;
    TreeRestoreSel sel(this,c.id());
    SafeInt safeChg(&m_changing);
    String id = c.id();
    NamedList p(c);
    p.copyParams(params);
    removeContact(id);
    addContact(id,p);
}

// Create contact structure (groups and lists)
void ContactList::createContactTree(ContactItem* c, ContactItemList& cil)
{
    if (!c)
	return;
    SafeInt safeChg(&m_changing);
    bool noGrp = true;
    ObjList* grps = c->groups();
    for (ObjList* o = grps->skipNull(); o; o = o->skipNext()) {
	String* grp = static_cast<String*>(o->get());
	if (grp->null())
	    continue;
	noGrp = false;
	int index = cil.getGroupIndex(*grp,*grp,m_expStatusGrp);
	if (o->skipNext())
	    cil.m_contacts[index].append(createContact(c->id(),*c));
	else
	    cil.m_contacts[index].append(c);
    }
    TelEngine::destruct(grps);
    if (noGrp) {
	int index = cil.getGroupIndex(s_noGroupId,m_noGroupText,m_expStatusGrp);
	cil.m_contacts[index].append(c);
    }
}

// Sort contacts
void ContactList::sortContacts(QList<QTreeWidgetItem*>& list)
{
    if (!list.size())
	return;
    SafeInt safeChg(&m_changing);
    if (m_sortKey == "name") {
	QVector<QtTreeItemKey> v(list.size());
	for (int i = 0; i < list.size(); i++) {
	    v[i].first = list[i];
	    v[i].second = (static_cast<ContactItem*>(list[i]))->m_name;
	}
	stableSort(v,m_sortOrder,m_compareNameCs);
	for (int i = 0; i < list.size(); i++)
	    list[i] = v[i].first;
    }
}


/*
 * ContactItem
 */
// Update name. Return true if changed
bool ContactItem::updateName(const NamedList& params, Qt::CaseSensitivity cs)
{
    const String* name = params.getParam("name");
    if (!name)
	return false;
    QString s = QtClient::setUtf8(*name);
    if (!compareStr(m_name,s,cs))
	return false;
    m_name = s;
    return true;
}

// Check if groups would change
bool ContactItem::groupsWouldChange(const NamedList& params)
{
    String* grps = params.getParam("groups");
    if (!grps)
	return false;
    bool changed = false;
    ObjList* cgroups = groups();
    ObjList* newList = Client::splitUnescape(*grps);
    ObjList* o = 0;
    for (o = newList->skipNull(); o && !changed; o = o->skipNext())
	changed = !cgroups->find(o->get()->toString());
    for (o = cgroups->skipNull(); o && !changed; o = o->skipNext())
	changed = !newList->find(o->get()->toString());
    TelEngine::destruct(newList);
    TelEngine::destruct(cgroups);
    return changed;
}

// Check if the contact status is 'offline'
bool ContactItem::offline()
{
    String* status = getParam("status");
    return status && *status == s_offline;
}


/*
 * ContactItemList
 */
int ContactItemList::getGroupIndex(const String& id, const String& text, bool expStat)
{
    for (int i = 0; i < m_groups.size(); i++) {
	QtTreeItem* item = static_cast<QtTreeItem*>(m_groups[i]);
	if (item->id() == id)
	    return i;
    }
    int pos = m_groups.size();
    if (pos && id != s_noGroupId &&
	(static_cast<QtTreeItem*>(m_groups[pos - 1]))->id() == s_noGroupId)
	pos--;
    m_groups.insert(pos,ContactList::createGroup(id,text,expStat));
    m_contacts.insert(pos,QtTreeItemList());
    return pos;
}

//
// FileItem
//
FileItem::FileItem(int type, const char* name, const String& path,
    QFileIconProvider* prov)
    : String(name),
    m_type(type), m_icon(0)
{
    FileListTree::buildFileFullName(m_fullName,path,name);
    if (prov)
	m_icon = new QIcon(FileListTree::fileIcon(type,m_fullName,prov));
}

FileItem::FileItem(const String& path, QFileIconProvider* prov)
    : String(FileListTree::s_upDir),
    m_type(FileListTree::TypeDir), m_icon(0)
{
    Client::removeLastNameInPath(m_fullName,path);
    if (prov)
	m_icon = new QIcon(FileListTree::fileIcon(m_type,m_fullName,prov));
}

FileItem::~FileItem()
{
    if (m_icon)
	delete m_icon;
}


//
// DirListThread
//
// Skip special directories (. or ..)
static inline bool skipSpecial(const char* s)
{
    return *s && *s == '.' && (!s[1] || (s[1] == '.' && !s[2]));
}

void DirListThread::run()
{
    ObjList* dirs = m_listDirs ? &m_dirs : 0;
    ObjList* files = m_listFiles ? &m_files : 0;
    XDebug(QtDriver::self(),DebugAll,"DirListThread(%s) starting [%p]",
	m_dir.c_str(),this);
#ifdef _WINDOWS
    String name(m_dir);
    if (!name.endsWith("\\"))
	name << "\\";
    name << "*";
    // Init find
    WIN32_FIND_DATAA d;
    HANDLE hFind = ::FindFirstFileA(name,&d);
    if (hFind == INVALID_HANDLE_VALUE) {
	m_error = ::GetLastError();
	if (m_error == ERROR_NO_MORE_FILES)
	    m_error = 0;
	runTerminated();
	return;
    }
    // Enumerate content
    ::SetLastError(0);
    do {
	if (isFinished())
	    break;
        if (d.dwFileAttributes & FILE_ATTRIBUTE_DEVICE ||
	    skipSpecial(d.cFileName))
	    continue;
        if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
	    if (dirs)
		dirs = addItem(FileListTree::TypeDir,d.cFileName,m_dirs,dirs);
        }
	else if (files)
	    files = addItem(FileListTree::TypeFile,d.cFileName,m_files,files);
    }
    while (::FindNextFileA(hFind,&d));
    if (isRunning()) {
	m_error = ::GetLastError();
	if (m_error == ERROR_NO_MORE_FILES)
	    m_error = 0;
    }
    else
	m_error = ERROR_CANCELLED;
    ::FindClose(hFind);
#else
    errno = 0;
    DIR* dir = ::opendir(m_dir);
    if (!dir) {
	m_error = errno;
	runTerminated();
	return;
    }
    struct dirent* entry;
    while ((entry = ::readdir(dir)) != 0) {
	if (isFinished())
	    break;
	if (skipSpecial(entry->d_name))
	    continue;
#ifdef _DIRENT_HAVE_D_TYPE
	if (entry->d_type == DT_DIR) {
	    if (dirs)
		dirs = addItem(FileListTree::TypeDir,entry->d_name,m_dirs,dirs);
	}
	else if (entry->d_type == DT_REG && files)
	    files = addItem(FileListTree::TypeFile,entry->d_name,m_files,files);
#else
	struct stat stat_buf;
	String p;
	p << m_dir << "/" << entry->d_name;
	if (::stat(p,&stat_buf))
	    break;
	if (S_ISDIR(stat_buf.st_mode)) {
	    if (dirs)
		dirs = addItem(FileListTree::TypeDir,entry->d_name,m_dirs,dirs);
	}
	else if (S_ISREG(stat_buf.st_mode) && files)
	    files = addItem(FileListTree::TypeFile,entry->d_name,m_files,files);
#endif // _DIRENT_HAVE_D_TYPE
    }
    if (isRunning())
	m_error = errno;
    else
	m_error = ECANCELED;
    ::closedir(dir);
#endif // _WINDOWS
    runTerminated();
}

ObjList* DirListThread::addItemSort(ObjList& list, FileItem* it)
{
    if (!it)
	return 0;
    ObjList* o = list.skipNull();
    bool asc = (m_sort == QtClient::SortAsc);
    for (; o; o = o->skipNext()) {
	FileItem* crt = static_cast<FileItem*>(o->get());
	int cmp = m_caseSensitive ? ::strcmp(*it,*crt) : ::strcasecmp(*it,*crt);
	if (!cmp)
	    continue;
	// Ascending ?
	if (asc) {
	    if (cmp > 0)
		continue;
	}
	else if (cmp < 0)
	    continue;
	return o->insert(it);
    }
    if (o)
	return o->append(it);
    return list.append(it);
}

// Called when terminated from run()
void DirListThread::runTerminated()
{
    XDebug(QtDriver::self(),DebugAll,"DirListThread(%s) finished error=%d [%p]",
	m_dir.c_str(),m_error,this);
    if (m_error)
	return;
    // Add up dir
    if (m_listDirs && m_listUpDir)
	m_dirs.insert(new FileItem(m_dir,m_iconProvider));
}


//
// FileListTree
//
const String FileListTree::s_upDir = "..";

static inline void setRootPath(String& path)
{
#ifdef _WINDOWS
    path = "";
#else
    path = "/";
#endif
}

static inline int getPathType(const String& s, int defVal)
{
    if (s == YSTRING("upthenhome"))
	return FileListTree::PathUpThenHome;
    if (s == YSTRING("home"))
	return FileListTree::PathHome;
    if (s == YSTRING("root"))
	return FileListTree::PathRoot;
    if (s == YSTRING("none"))
	return FileListTree::PathNone;
    return defVal;
}

static inline void removePathSepEnd(String& s)
{
    Client::removeEndsWithPathSep(s,s);
}

// Constructor
FileListTree::FileListTree(const char* name, const NamedList& params, QWidget* parent)
    : QtCustomTree(name,params,parent,false),
    m_fileSystemList(false),
    m_autoChangeDir(true),
    m_listFiles(false),
    m_sort(QtClient::SortNone),
    m_listOnFailure(PathUpThenHome),
    m_iconProvider(0),
    m_dirListThread(0)
{
    XDebug(ClientDriver::self(),DebugAll,"FileListTree(%s) [%p]",name,this);
    // Add item props translation
    addItemType(TypeDir,"dir");
    addItemType(TypeFile,"file");
    addItemType(TypeDrive,"drive");
    // Set some defaults
    if (params.getBoolValue(YSTRING("filelist_default_itemstyle"))) {
	setItemStyle("dir:<html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\"> li { white-space: pre-wrap; }</style></head><body style=\" font-size:12px; font-weight:bold; font-style:normal;\"><p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">${name}</p></body></html>");
	setItemStyle("file:<html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\"> li { white-space: pre-wrap; }</style></head><body style=\" font-size:12px; font-weight:400; font-style:normal;\"><p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">${name}</p></body></html>");
	setItemStyle("drive:<html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\"> li { white-space: pre-wrap; }</style></head><body style=\" font-size:12px; font-weight:bold; font-style:normal;\"><p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">${name}</p></body></html>");
    }
    String* ihs = params.getParam(YSTRING("filelist_default_itemheight"));
    int ih = 0;
    if (ihs) {
	if (ihs->toBoolean())
	    ih = 16;
	else
	    ih = ihs->toInteger();
    }
    if (ih > 0) {
	String tmp(ih);
	setItemHeight(QtClient::setUtf8("dir:" + tmp));
	setItemHeight(QtClient::setUtf8("file:" + tmp));
	setItemHeight(QtClient::setUtf8("drive:" + tmp));
	setUniformRowHeights(true);
    }
    // Display contents from file system
    m_fileSystemList = params.getBoolValue(YSTRING("filelist_filesystemlist"));
    if (m_fileSystemList) {
	m_sort = QtClient::SortAsc;
	m_nameParam = params.getValue(YSTRING("filelist_filesystemlist_name_column"),"name");
	m_autoChangeDir = params.getBoolValue(YSTRING("filelist_filesystemlist_autochangedir"),true);
	m_listFiles = params.getBoolValue(YSTRING("filelist_filesystemlist_listfiles"),true);
	if (params.getBoolValue(YSTRING("filelist_filesystemlist_showicons")))
	    m_iconProvider = new QFileIconProvider;
	m_listOnFailure = getPathType(params[YSTRING("filelist_filesystemlist_listonfailure")],
	    PathUpThenHome);
    }
    setParams(params);
    if (m_fileSystemList) {
	String* s = params.getParam(YSTRING("filelist_filesystemlist_startpath"));
	if (s) {
	    int t = getPathType(*s,PathRoot);
	    if (t == PathHome)
		setFsPath(QDir::homePath());
	    else if (t != PathNone)
		setFsPath();
	}
    }
}

// Destructor
FileListTree::~FileListTree()
{
    setDirListThread(false);
    if (m_iconProvider)
	delete m_iconProvider;
}

// Set _yate_filesystem_path property
void FileListTree::setFsPath(QString path)
{
    if (!m_fileSystemList)
	return;
    String tmp;
    QtClient::getUtf8(tmp,QDir::toNativeSeparators(path));
    setFsPath(tmp);
}

// Set _yate_refresh property
void FileListTree::setRefresh(QString val)
{
    if (m_fileSystemList)
	setFsPath(m_fsPath);
}

// Change file system path, refresh data
void FileListTree::setFsPath(const String& path, bool force)
{
    if (!m_fileSystemList)
	return;
    if (!force && m_fsPath == path)
	return;
    String old = m_fsPath;
    m_fsPath = path;
    removePathSepEnd(m_fsPath);
    if (!m_fsPath) {
	setRootPath(m_fsPath);
#ifdef _WINDOWS
	ObjList tmp;
	QFileInfoList l = QDir::drives();
	for (int i = 0; i < l.size(); i++) {
	    QFileInfo fi = l[i];
	    String n;
	    QtClient::getUtf8(n,QDir::toNativeSeparators(fi.absoluteFilePath()));
	    removePathSepEnd(n);
	    if (n)
		tmp.append(new FileItem(TypeDrive,n,String::empty(),m_iconProvider));
	}
	refresh(0,0,&tmp);
	m_acceptDropOnEmpty = QtDrop::None;
	return;
#endif
    }
    if (setDirListThread(true)) {
	m_acceptDropOnEmpty = QtDrop::Always;
	clearTable();
    }
    else {
	m_acceptDropOnEmpty = QtDrop::None;
	m_fsPath = old;
    }
}

static void addFileItems(QList<QTreeWidgetItem*>& items, ObjList* list,
    const String& nameParam, int iconCol)
{
    if (!list)
	return;
    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	FileItem* f = static_cast<FileItem*>(o->get());
	QtTreeItem* it = new QtTreeItem(f->m_fullName,f->m_type);
	it->addParam(nameParam,*f);
	if (iconCol >= 0 && f->m_icon)
	    it->setIcon(iconCol,*(f->m_icon));
	items.append(it);
    }
}

// Directory listing thread finished notification
void FileListTree::refresh(ObjList* dirs, ObjList* files, ObjList* drives)
{
    clearTable();
    SafeInt safeChg(&m_changing);
    QList<QTreeWidgetItem*> list;
    int col = getColumnNo(m_nameParam);
    addFileItems(list,drives,m_nameParam,col);
    addFileItems(list,dirs,m_nameParam,col);
    addFileItems(list,files,m_nameParam,col);
    addChildren(list);
    for (int i = 0; i < list.size(); i++) {
	QtTreeItem* it = static_cast<QtTreeItem*>(list[i]);
	updateItem(*it,*it);
    }
}

// Sort a list of items
void FileListTree::sortItems(QList<QTreeWidgetItem*>& list, int type)
{
    if (type != TypeDir && type != TypeFile)
	return;
    QVector<QtTreeItemKey> v(list.size());
    for (int i = 0; i < list.size(); i++) {
	v[i].first = list[i];
	v[i].second = QtClient::setUtf8(static_cast<QtTreeItem*>(list[i])->getValue(m_nameParam));
    }
    stableSort(v,Qt::AscendingOrder,Qt::CaseInsensitive);
    for (int i = 0; i < list.size(); i++)
	list[i] = v[i].first;
}

// Retrieve the icon for a given item
QIcon FileListTree::icon(QtTreeItem& item)
{
    return fileIcon(item.type(),item.toString(),m_iconProvider);
}

// Retrieve the icon for a given item type
QIcon FileListTree::fileIcon(int type, const String& name, QFileIconProvider* provider)
{
    if (!provider)
	return QIcon();
    if (type == TypeDir || type == TypeFile) {
	QFileInfo fi(QtClient::setUtf8(name));
	return provider->icon(fi);
    }
    if (type == TypeDrive)
	return provider->icon(QFileIconProvider::Drive);
    return QIcon();
}

// Catch dir list thread terminate signal
void FileListTree::onDirThreadTerminate()
{
    if (!m_dirListThread)
	return;
    QThread* th = qobject_cast<QThread*>(sender());
    if (th != m_dirListThread)
	return;
    DirListThread* t = static_cast<DirListThread*>(th);
    bool ok = !t->m_error;
    if (ok)
	refresh(t->m_listDirs ? &t->m_dirs : 0,t->m_listFiles ? &t->m_files : 0);
    else if (QtDriver::self() && QtDriver::self()->debugAt(DebugNote)) {
	String s;
	Thread::errorString(s,t->m_error);
	Debug(QtDriver::self(),DebugNote,"FileListTree(%s) failed to list '%s': %d '%s' [%p]",
	    name().c_str(),m_fsPath.c_str(),t->m_error,s.c_str(),this);
    }
    QtBusyWidget::showBusyChild(this,false);
    resetThread();
    if (ok)
	return;
    if (m_listOnFailure == PathUpThenHome) {
	// Up dir, then home
	if (!isHomePath()) {
	    if (isRootPath(m_fsPath)) {
		setFsPath(QDir::homePath());
		return;
	    }
	    int pos = m_fsPath.rfind(*Engine::pathSeparator());
	    if (pos >= 0)
		setFsPath(m_fsPath.substr(0,pos));
	    else
		setFsPath();
	    return;
	}
    }
    else if (m_listOnFailure == PathRoot) {
	// Try root path
	if (!isRootPath(m_fsPath)) {
	    setFsPath();
	    return;
	}
    }
    // Always try home path if something set
    if (m_listOnFailure != PathNone && !isHomePath()) {
	setFsPath(QDir::homePath());
	return;
    }
    m_fsPath = "";
    m_acceptDropOnEmpty = QtDrop::None;
    refresh(0,0);
}

// Start/stop dir list thread
bool FileListTree::setDirListThread(bool on)
{
    QtBusyWidget::showBusyChild(this,false);
    resetThread();
    if (!on)
	return true;
    DirListThread* t = new DirListThread(0,m_fsPath,true,m_listFiles);
    if (!QtClient::connectObjects(t,SIGNAL(finished()),
	this,SLOT(onDirThreadTerminate()))) {
	t->deleteLater();
	return false;
    }
    t->m_iconProvider = m_iconProvider;
    t->m_listUpDir = !isRootPath(m_fsPath);
    t->m_sort = m_sort;
    QtBusyWidget::showBusyChild(this,true);
    m_dirListThread = t;
    m_dirListThread->start();
    return true;
}

// Process item double click
void FileListTree::onItemDoubleClicked(QtTreeItem* item, int column)
{
    if (item && item->type() != TypeFile && m_fileSystemList && m_autoChangeDir)
	setFsPath(item->toString());
    else
	QtCustomTree::onItemDoubleClicked(item,column);
}

void FileListTree::resetThread()
{
    if (!m_dirListThread)
	return;
    QThread* t = m_dirListThread;
    m_dirListThread = 0;
    t->disconnect();
    t->exit();
    t->deleteLater();
}


//
// QtPaintItemDesc
//
QtPaintButtonDesc* QtPaintItemDesc::button()
{
    return 0;
}


//
// QtPaintButtonDesc
//
QtPaintButtonDesc* QtPaintButtonDesc::button()
{
    return this;
}

// Find a button in a list
QtPaintButtonDesc* QtPaintButtonDesc::find(ObjList& list, const String& name,
    bool create)
{
    if (!name)
	return 0;
    ObjList* o = list.find(name);
    if (!o && create)
	o = list.append(new QtPaintButtonDesc(name));
    return o ? static_cast<QtPaintItemDesc*>(o->get())->button() : 0;
}


//
// QtPaintItem
//
// Set hover state
bool QtPaintItem::setHover(bool on)
{
    if (m_hover == on)
	return false;
    m_hover = on;
    return true;
}

// Set pressed state
bool QtPaintItem::setPressed(bool on)
{
    if (m_pressed == on)
	return false;
    m_pressed = on;
    return true;
}

// Retrieve the item name
const String& QtPaintItem::toString() const
{
    return name();
}


//
// QtPaintButton
//
QtPaintButton::QtPaintButton(QtPaintButtonDesc& desc)
    : QtPaintItem(desc,desc.m_size),
    m_image(0),
    m_iconSize(desc.m_iconSize),
    m_iconOffset(0,0)
{
    if (m_iconSize.width() > m_size.width())
	m_iconSize.setWidth(m_size.width());
    if (m_iconSize.height() > m_size.height())
	m_iconSize.setHeight(m_size.height());
    m_iconOffset.setWidth((m_size.width() - m_iconSize.width()) / 2);
    m_iconOffset.setHeight((m_size.height() - m_iconSize.height()) / 2);
    m_action = desc.m_params;
    m_image = &m_normalImage;
    loadImages(desc.m_params);
    updateOptState();
}

// Load button images
void QtPaintButton::loadImages(const NamedList& params)
{
    loadImage(m_normalImage,params,YSTRING("_yate_normal_icon"));
    if (!loadImage(m_hoverImage,params,YSTRING("_yate_hover_icon")))
	m_hoverImage = m_normalImage;
    if (!loadImage(m_pressedImage,params,YSTRING("_yate_pressed_icon")))
	m_pressedImage = m_normalImage;
}

// Set hover state
bool QtPaintButton::setHover(bool on)
{
    if (!QtPaintItem::setHover(on))
	return false;
    if (!on)
	m_pressed = false;
    updateOptState();
    return true;
}

// Set pressed state
bool QtPaintButton::setPressed(bool on)
{
    if (!QtPaintItem::setPressed(on))
	return false;
    updateOptState();
    return true;
}

// Draw the button
void QtPaintButton::draw(QPainter* painter, const QRect& rect)
{
    m_displayRect = rect;
    if (!(painter && m_image))
	return;
    QPoint p(m_iconOffset.width() + rect.x(),m_iconOffset.height() + rect.y());
    painter->drawPixmap(p,*m_image);
}

// Load an image, adjust its size
bool QtPaintButton::loadImage(QPixmap& pixmap, const NamedList& params, const String& param)
{
    if (!QtClient::getSkinPathPixmapFromCache(pixmap,params[param]))
	return false;
    // Adjust size
    if (pixmap.size() != m_iconSize)
	pixmap = pixmap.scaled(m_iconSize,Qt::KeepAspectRatio);
    return true;
}

// Update option state
void QtPaintButton::updateOptState()
{
    if (m_enabled) {
	if (m_hover) {
	    if (!m_pressed)
	    	m_image = &m_hoverImage;
	    else
		m_image = &m_pressedImage;
	}
	else if (m_pressed)
	    m_image = &m_pressedImage;
	else
	    m_image = &m_normalImage;
    }
    else
	m_image = &m_normalImage;
}


//
// QtPaintItems
//
// Add an item from description
void QtPaintItems::append(QtPaintItemDesc& desc)
{
    QtPaintButtonDesc* bDesc = desc.button();
    if (!bDesc)
	return;
    QtPaintButton* b = new QtPaintButton(*bDesc);
    m_items.remove(b->toString());
    m_items.append(b);
}

// Calculate area needed to paint
void QtPaintItems::itemsAdded()
{
    m_size.setHeight(0);
    m_size.setWidth(0);
    for (ObjList* o = m_items.skipNull(); o; o = o->skipNext()) {
	QtPaintItem* item = static_cast<QtPaintItem*>(o->get());
	if (m_size.width())
	    m_size.setWidth(m_size.width() + m_itemSpace);
	m_size.setWidth(m_size.width() + item->size().width());
	if (m_size.height() < item->size().height())
	    m_size.setHeight(item->size().height());
    }
    if (m_size.width())
	m_size.setWidth(m_size.width() + m_margins.x() + m_margins.width());
    if (m_size.height())
	m_size.setHeight(m_size.height() + m_margins.y() + m_margins.height());
}

// Set hover. Update item at position
bool QtPaintItems::setHover(const QPoint& pos)
{
    bool chg = setHover(true);
    if (m_lastItemHover) {
	if (m_lastItemHover->displayRect().contains(pos))
	    return chg;
	m_lastItemHover->setHover(false);
	m_lastItemHover = 0;
	chg = true;
    }
    for (ObjList* o = m_items.skipNull(); !m_lastItemHover && o; o = o->skipNext()) {
	QtPaintItem* it = static_cast<QtPaintItem*>(o->get());
	if (it->displayRect().contains(pos))
	    m_lastItemHover = it;
    }
    if (m_lastItemHover)
	chg = m_lastItemHover->setHover(true) || chg;
    return chg;
}

// Set hover state
bool QtPaintItems::setHover(bool on)
{
    if (!QtPaintItem::setHover(on))
	return false;
    if (on)
	return true;
    if (m_lastItemHover) {
	m_lastItemHover->setHover(false);
	m_lastItemHover = 0;
    }
    return true;
}

// Mouse pressed/released. Update item at position
bool QtPaintItems::mousePressed(bool on, const QPoint& pos, String* action)
{
    bool chg = false;
    if (m_lastItemHover) {
	if (m_lastItemHover->displayRect().contains(pos)) {
	    chg = m_lastItemHover->setPressed(on);
	    if (chg && !on && action)
		*action = m_lastItemHover->action();
	}
	else
	    chg = m_lastItemHover->setPressed(false);
    }
    return setPressed(on);
}

// Set pressed state
bool QtPaintItems::setPressed(bool on)
{
    bool chg = !on && m_lastItemHover && m_lastItemHover->setPressed(false);
    return QtPaintItem::setPressed(on) || chg;
}

// Draw items.
void QtPaintItems::draw(QPainter* painter, const QRect& rect)
{
    m_displayRect = rect;
    if (!painter)
	return;
    painter->save();
    int maxX = rect.x() + rect.width();
    int x = rect.x() + m_margins.x();
    int y = rect.y() + m_margins.y();
    for (ObjList* o = m_items.skipNull(); o && (x < maxX); o = o->skipNext()) {
	QtPaintItem* item = static_cast<QtPaintItem*>(o->get());
	QRect r(x,y,item->size().width(),item->size().height());
	painter->setClipRect(r);
	item->draw(painter,r);
	x += item->size().width() + m_itemSpace;
    }
    painter->restore();
}


//
// QtItemDelegate
//
QtItemDelegate::QtItemDelegate(QObject* parent, const NamedList& params)
    : QItemDelegate(parent),
    String(params),
    m_drawFocus(true),
    m_roleDisplayText(Qt::DisplayRole),
    m_roleImage(Qt::UserRole),
    m_roleBackground(Qt::UserRole),
    m_roleMargins(Qt::UserRole),
    m_roleQtDrawItems(Qt::UserRole)
{
    static const String s_drawfocus = "drawfocus";
    static const String s_columns = "columns";
    static const String s_editableCols = "editable_cols";
    static const String s_role_display = "role_display";
    static const String s_role_image = "role_image";
    static const String s_role_background = "role_background";
    static const String s_role_margins = "role_margins";
    static const String s_role_qtdrawitems = "role_qtdrawitems";
    static const String s_noRoles = "noroles";
    static const String s_noImageRole = "noimagerole";

    String pref = params;
    if (pref)
	pref << ".";
    NamedIterator iter(params);
    bool noRoleImage = false;
    bool noRoles = false;
    for (const NamedString* ns = 0; 0 != (ns = iter.get());) {
	if (ns->name() == s_drawfocus)
	    m_drawFocus = ns->toBoolean();
	else if (ns->name() == s_columns)
	    m_columnsStr = QtClient::setUtf8(*ns).split(',',QString::SkipEmptyParts);
	else if (ns->name() == s_editableCols)
	    m_editableColsStr = QtClient::setUtf8(*ns).split(',',QString::SkipEmptyParts);
	else if (ns->name() == s_noImageRole)
	    noRoleImage = ns->toBoolean();
	else if (ns->name() == s_noRoles)
	    noRoles = ns->toBoolean();
	else if (pref && ns->name().startsWith(pref,false)) {
	    // Handle parameters set from code (not configurable)
	    String tmp = ns->name().substr(pref.length());
	    if (tmp == s_role_display)
		m_roleDisplayText = ns->toInteger(Qt::DisplayRole);
	    else if (tmp == s_role_image)
		m_roleImage = ns->toInteger(Qt::UserRole);
	    else if (tmp == s_role_background)
		m_roleBackground = ns->toInteger(Qt::UserRole);
	    else if (tmp == s_role_margins)
		m_roleMargins = ns->toInteger(Qt::UserRole);
	    else if (tmp == s_role_qtdrawitems)
		m_roleQtDrawItems = ns->toInteger(Qt::UserRole);
	}
    }
    // Disable role(s)
    if (noRoles) {
	m_roleDisplayText = Qt::DisplayRole;
	m_roleImage = Qt::UserRole;
	m_roleBackground = Qt::UserRole;
	m_roleMargins = Qt::UserRole;
    }
    else {
	if (noRoleImage)
	    m_roleImage = Qt::UserRole;
    }
#ifdef XDEBUG
    String dump;
    params.dump(dump," ");
    Debug(DebugAll,"QtItemDelegate(%s) created: %s [%p]",c_str(),dump.c_str(),this);
#endif
}

// Utility: translate name to int value
static void setIntListName(QList<int>& dest, QStringList& values, QStringList& cNames,
    bool unique = true)
{
    dest.clear();
    for (int i = 0; i < values.size(); i++) {
	bool ok = false;
	int val = values[i].toInt(&ok);
	if (!ok)
	    val = cNames.indexOf(values[i]);
	if (val >= 0 && !(unique && dest.contains(val)))
	    dest.append(val);
    }
}

// Update column position from column names.
// 'cNames' must be the column names in their order, starting from 0
void QtItemDelegate::updateColumns(QStringList& cNames)
{
    setIntListName(m_columns,m_columnsStr,cNames);
    setIntListName(m_editableCols,m_editableColsStr,cNames);
}

void QtItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
    const QModelIndex& index) const
{
    QStyleOptionViewItemV3 opt = setOptions(index,option);
    const QStyleOptionViewItemV2* v2 = qstyleoption_cast<const QStyleOptionViewItemV2*>(&option);
    opt.features = v2 ? v2->features : QStyleOptionViewItemV2::ViewItemFeatures(QStyleOptionViewItemV2::None);
    const QStyleOptionViewItemV3* v3 = qstyleoption_cast<const QStyleOptionViewItemV3*>(&option);
    opt.locale = v3 ? v3->locale : QLocale();
    opt.widget = v3 ? v3->widget : 0;
    // Prepare painter
    painter->save();
    // Retrieve check
    QRect checkRect;
    Qt::CheckState checkState = Qt::Unchecked;
    QVariant checkVar = index.data(Qt::CheckStateRole);
    if (checkVar.isValid()) {
	checkState = static_cast<Qt::CheckState>(checkVar.toInt());
	checkRect = check(opt,opt.rect,checkVar);
    }
    // Retrieve image (decoration)
    QPixmap pixmap;
    QRect decorationRect;
    bool isStd = (m_roleImage <= Qt::UserRole);
    QVariant pVar = index.data(isStd ? Qt::DecorationRole : m_roleImage);
    if (pVar.isValid()) {
	if (isStd)
	    pixmap = decoration(opt,pVar);
	else {
	    QString file = pVar.toString();
	    QtClient::getPixmapFromCache(pixmap,file);
	    // Resize the pixmap
	    if (!pixmap.isNull())
		pixmap = pixmap.scaled(opt.decorationSize.width(),
		    opt.decorationSize.height(),Qt::KeepAspectRatio);
	}
	decorationRect = QRect(QPoint(0,0),pixmap.size());
    }
    // Retrieve text to display
    QString text = getDisplayText(opt,index);
    QRect displayRect = opt.rect;
    displayRect.setWidth(INT_MAX/256);
    displayRect = textRectangle(painter,displayRect,opt.font,text);
    // Retrieve margins and apply them
    QRect margins;
    if (m_roleMargins != Qt::UserRole) {
	pVar = index.data(m_roleMargins);
	if (pVar.type() == QVariant::Rect) {
	    margins = pVar.toRect();
	    applyMargins(opt.rect,margins,true);
	}
    }
    QtPaintItems* extraPaint = 0;
    if (m_roleQtDrawItems != Qt::UserRole) {
	pVar = index.data(m_roleQtDrawItems);
	if (pVar.type() == QVariant::UserType) {
	    QtRefObjectHolder holder = qVariantValue<QtRefObjectHolder>(pVar);
	    extraPaint = static_cast<QtPaintItems*>((RefObject*)holder.m_refObj);
	}
    }
    // Calculate layout
    doLayout(opt,&checkRect,&decorationRect,&displayRect,false);
    // Draw the item
    if (m_roleMargins != Qt::UserRole)
	applyMargins(opt.rect,margins,false);
    drawBackground(painter,opt,index);
    if (m_roleMargins != Qt::UserRole)
	applyMargins(opt.rect,margins,true);
    drawCheck(painter,opt,checkRect,checkState);
    drawDecoration(painter,opt,decorationRect,pixmap);
    if (extraPaint && extraPaint->size().width()) {
	// Steal extra paint area from text display
	int w = extraPaint->size().width();
	if (w < displayRect.width())
	    displayRect.setWidth(displayRect.width() - w);
	else {
	    w = displayRect.width();
	    displayRect.setWidth(0);
	}
	int y = displayRect.y();
	int h = extraPaint->size().height();
	int delta = (displayRect.height() - h) / 2;
	if (delta) {
	    if (delta > 0)
		y += delta;
	    else
		h += delta;
	}
	QRect rect(displayRect.x() + displayRect.width(),y,w,h);
	extraPaint->draw(painter,rect);
    }
    drawDisplay(painter,opt,displayRect,text);
    if (m_roleMargins != Qt::UserRole)
	applyMargins(opt.rect,margins,false);
    drawFocus(painter,opt,displayRect);
    // Restore painter
    painter->restore();
}

// Build a list of delegates. Return a list of QtItemDelegate
QList<QAbstractItemDelegate*> QtItemDelegate::buildDelegates(QObject* parent, const NamedList& params,
    const NamedList* common, const String& prefix)
{
    QList<QAbstractItemDelegate*> list;
    String pref = prefix;
    for (int n = 0; true; n++) {
	if (n)
	    pref << "." << n;
	NamedString* ns = params.getParam(pref);
	if (!ns) {
	    if (n)
		break;
	    continue;
	}
	NamedList p(pref);
	pref << ".";
	p.copySubParams(params,pref);
	if (common) {
	    NamedIterator iter(*common);
	    for (const NamedString* ns = 0; 0 != (ns = iter.get());)
		p.addParam(pref + ns->name(),*ns);
	}
	QAbstractItemDelegate* dlg = build(parent,*ns,p);
	if (dlg)
	    list.append(dlg);
    }
    return list;
}

// Build a delegate
QAbstractItemDelegate* QtItemDelegate::build(QObject* parent, const String& cls,
    NamedList& params)
{
    if (!cls || cls == YSTRING("QtItemDelegate"))
	return new QtItemDelegate(parent,params);
    if (cls == YSTRING("QtHtmlItemDelegate"))
	return new QtHtmlItemDelegate(parent,params);
    QObject* obj = (QObject*)UIFactory::build(cls,String::empty(),&params);
    if (!obj)
	return 0;
    QAbstractItemDelegate* d = qobject_cast<QAbstractItemDelegate*>(obj);
    if (d)
	return d;
    delete obj;
    return 0;
}

// Retrieve display text for a given index
QString QtItemDelegate::getDisplayText(const QStyleOptionViewItem& opt,
    const QModelIndex& index) const
{
    QVariant var = index.data(m_roleDisplayText);
    if (var.type() == QVariant::StringList) {
	QStringList list = var.toStringList();
	if (!list.size())
	    return QString();
	// 1 item or not selected: return the first string
	if (list.size() == 1 || 0 == (opt.state & QStyle::State_Selected))
	    return list[0];
	return list[1];
    }
    if (var.canConvert(QVariant::String))
	return var.toString();
    return QString();
}

void QtItemDelegate::drawBackground(QPainter* painter, const QStyleOptionViewItem& opt,
    const QModelIndex& index) const
{
    QVariant var;
    if (m_roleBackground != Qt::UserRole)
	var = index.data(m_roleBackground);
    if (!var.isValid()) {
	QItemDelegate::drawBackground(painter,opt,index);
	return;
    }
    if (qVariantCanConvert<QBrush>(var)) {
	QPointF oldBO = painter->brushOrigin();
	painter->setBrushOrigin(opt.rect.topLeft());
	painter->fillRect(opt.rect,qvariant_cast<QBrush>(var));
	painter->setBrushOrigin(oldBO);
    }
    else
	Debug(DebugNote,"QtItemDelegate(%s) unhandled background variant type=%s",
	    c_str(),var.typeName());
}

void QtItemDelegate::drawDecoration(QPainter* painter, const QStyleOptionViewItem& opt,
    const QRect& rect, const QPixmap& pixmap) const
{
    if (pixmap.isNull() || !rect.isValid())
	return;
    QPoint p = QStyle::alignedRect(opt.direction,opt.decorationAlignment,
	pixmap.size(),rect).topLeft();
    painter->drawPixmap(p,pixmap);
}

void QtItemDelegate::drawFocus(QPainter* painter, const QStyleOptionViewItem& opt,
    const QRect& rect) const
{
    if (!m_drawFocus)
	return;
    QItemDelegate::drawFocus(painter,opt,rect);
}

QWidget* QtItemDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option,
    const QModelIndex& index) const
{
    if (m_editableCols.size() && !m_editableCols.contains(index.column()))
	return 0;
    return QItemDelegate::createEditor(parent,option,index);
}

// Apply item margins
void QtItemDelegate::applyMargins(QRect& dest, const QRect& src, bool inc) const
{
    if (inc) {
	dest.setLeft(dest.left() + src.left());
	dest.setTop(dest.top() + src.top());
	dest.setRight(dest.right() - src.right());
	dest.setBottom(dest.bottom() - src.bottom());
    }
    else {
	dest.setLeft(dest.left() - src.left());
	dest.setTop(dest.top() - src.top());
	dest.setRight(dest.right() + src.right());
	dest.setBottom(dest.bottom() + src.bottom());
    }
}


//
// QtHtmlItemDelegate
//
void QtHtmlItemDelegate::drawDisplay(QPainter* painter, const QStyleOptionViewItem& opt,
    const QRect& rect, const QString& text) const
{
    if (text.isEmpty())
	return;
    QTextDocument doc;
    doc.setHtml(text);
    QAbstractTextDocumentLayout* layout = doc.documentLayout();
    if (!layout)
	return;
    QAbstractTextDocumentLayout::PaintContext context;
    painter->save();
    painter->setClipRect(rect);
    QSize sz(layout->documentSize().toSize());
    int y = rect.y();
    if (sz.height()) {
	// Align vcenter and bottom (top is the default for document)
	if (0 != (opt.displayAlignment & Qt::AlignVCenter))
	    y += (rect.height() - sz.height()) / 2;
	else if (0 != (opt.displayAlignment & Qt::AlignBottom))
	    y += rect.height() - sz.height();
    }
    painter->translate(rect.x(),y);
    layout->draw(painter,context);
    painter->restore();
}


//
// CustomTreeFactory
//
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
    if (type == "FileListTree")
        return new FileListTree(name,*params,parentWidget);
    if (type == "QtCustomTree")
        return new QtCustomTree(name,*params,parentWidget);
    return 0;
}

}; // anonymous namespace

#include "customtree.moc"

/* vi: set ts=8 sw=4 sts=4 noet: */
