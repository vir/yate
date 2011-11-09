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

class QtItemDelegate : public QItemDelegate, public String
{
    YCLASS(QtItemDelegate,String)
public:
    QtItemDelegate(QObject* parent, const NamedList& params = NamedList::empty());
    virtual void paint(QPainter* painter, const QStyleOptionViewItem& option,
	const QModelIndex& index) const;
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

    bool m_drawFocus;                    // Draw focus
    int m_roleDisplayText;               // Item display role to handle
    int m_roleImage;                     // Item role containing image file name
    int m_roleBackground;                // Item background role to handle
};

class QtHtmlItemDelegate : public QtItemDelegate
{
    YCLASS(QtHtmlItemDelegate,QtItemDelegate)
public:
    QtHtmlItemDelegate(QObject* parent, const NamedList& params = NamedList::empty())
	: QtItemDelegate(parent,params)
	{}
protected:
    virtual void drawDisplay(QPainter* painter, const QStyleOptionViewItem& opt,
	const QRect& rect, const QString& text) const;
};


QtItemDelegate::QtItemDelegate(QObject* parent, const NamedList& params)
    : QItemDelegate(parent),
    String(params),
    m_drawFocus(true),
    m_roleDisplayText(Qt::DisplayRole),
    m_roleImage(Qt::UserRole),
    m_roleBackground(Qt::UserRole)
{
    m_drawFocus = params.getBoolValue("drawfocus",true);
    // Handle parameters set from code (not configurable)
    if (params) {
	m_roleDisplayText = params.getIntValue(params + ".role_display",Qt::DisplayRole);
	m_roleImage = params.getIntValue(params + ".role_image",Qt::UserRole);
	m_roleBackground = params.getIntValue(params + ".role_background",Qt::UserRole);
    }
#ifdef XDEBUG
    String dump;
    params.dump(dump," ");
    Debug(DebugAll,"QtItemDelegate(%s) created: %s [%p]",c_str(),dump.c_str(),this);
#endif
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
    // Calculate layout
    doLayout(opt,&checkRect,&decorationRect,&displayRect,false);
    // Draw the item
    drawBackground(painter,opt,index);
    drawCheck(painter,opt,checkRect,checkState);
    drawDecoration(painter,opt,decorationRect,pixmap);
    drawDisplay(painter,opt,displayRect,text);
    drawFocus(painter,opt,displayRect);
    // Restore painter
    painter->restore();
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


static CustomTreeFactory s_factory;
static const String s_noGroupId(MD5("Yate").hexDigest() + "_NOGROUP");
static const String s_offline("offline");


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
 * QtTreeItem
 */
QtTreeItem::QtTreeItem(const char* id, int type, const char* text, bool storeExp)
    : QTreeWidgetItem(type),
    NamedList(id),
    m_storeExp(storeExp)
{
    if (!TelEngine::null(text))
	QTreeWidgetItem::setText(0,QtClient::setUtf8(text));
    XDebug(ClientDriver::self(),DebugAll,"QtTreeItem(%s) type=%d [%p]",id,type,this);
}

QtTreeItem::~QtTreeItem()
{
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


/*
 * QtCustomTree
 */
QtCustomTree::QtCustomTree(const char* name, const NamedList& params, QWidget* parent,
    bool applyParams)
    : QtTree(name,parent),
    m_hasCheckableCols(false),
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
	    ObjList* title = params["columns.title"].split(',',true);
	    ObjList* width = params["columns.width"].split(',',true);
	    ObjList* sizeMode = params["columns.resize"].split(',',true);
	    ObjList* check = params["columns.check"].split(',',false);
	    setColumnCount(id->count());
	    int n = 0;
	    for (ObjList* o = id->skipNull(); o; o = o->skipNext(), n++) {
		String* name = static_cast<String*>(o->get());
		String caption = objListItem(title,n);
		hdr->setText(n,QtClient::setUtf8(caption ? caption : *name));
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
	}
	// Set item delegate(s)
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
		QtHtmlItemDelegate* dlg = new QtHtmlItemDelegate(this,pp);
		XDebug(ClientDriver::self(),DebugNote,
		    "QtCustomTree(%s) setting html item delegate (%p,%s) for column %d [%p]",
		    name,dlg,dlg->toString().c_str(),col,this);
		setItemDelegateForColumn(col,dlg);
	    }
	    TelEngine::destruct(l);
	}
    }
    // Connect signals
    QtClient::connectObjects(this,SIGNAL(currentItemChanged(QTreeWidgetItem*,QTreeWidgetItem*)),
	this,SLOT(selectionChangedSlot(QTreeWidgetItem*,QTreeWidgetItem*)));
    QtClient::connectObjects(this,SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)),
	this,SLOT(itemDoubleClickedSlot(QTreeWidgetItem*,int)));
    QtClient::connectObjects(this,SIGNAL(itemActivated(QTreeWidgetItem*,int)),
	this,SLOT(itemDoubleClickedSlot(QTreeWidgetItem*,int)));
    QtClient::connectObjects(this,SIGNAL(itemExpanded(QTreeWidgetItem*)),
	this,SLOT(itemExpandedSlot(QTreeWidgetItem*)));
    QtClient::connectObjects(this,SIGNAL(itemCollapsed(QTreeWidgetItem*)),
	this,SLOT(itemCollapsedSlot(QTreeWidgetItem*)));
    // Set params
    if (applyParams)
	setParams(params);
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
    }
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
    NamedIterator iter(*data);
    for (const NamedString* ns = 0; 0 != (ns = iter.get());) {
	if (!ns->name())
	    continue;
	if (!ns->null()) {
	    NamedList* params = YOBJECT(NamedList,ns);
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
    // Set widget
    QWidget* w = itemWidget(item,0);
    if (!w) {
	w = loadWidgetType(this,item->id(),itemPropsName(item->type()));
	if (w) {
	    w->setAutoFillBackground(true);
	    setItemWidget(item,0,w);
	    XDebug(ClientDriver::self(),DebugAll,
		"QtTree(%s) set widget (%p,%s) for child '%s'",
		name().c_str(),w,YQT_OBJECT_NAME(w),item->id().c_str());
	    applyStyleSheet(item,item->isSelected());
	    // Adjust widget to row height if configured,
	    // or row height to widget otherwise
	    QSize sz = item->sizeHint(0);
	    int h = getItemRowHeight(item->type());
	    if (h > 0)
		w->resize(w->width(),sz.height());
	    else {
		sz.setHeight(w->height());
		item->setSizeHint(0,sz);
	    }
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
    sz.setHeight(h);
    item->setSizeHint(0,sz);
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
void QtCustomTree::setStateImage(QtTreeItem& item)
{
    QtTreeItemProps* p = treeItemProps(item.type());
    if (!(p && p->m_stateWidget))
	return;
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
    if (tmp.startSkip("color:",false))
	p->m_bg = QBrush(QColor(tmp.c_str()));
    else
	p->m_bg = QBrush();
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
	if (width >= 0)
	    setColumnWidth(i,width);
    }
}

// Set sorting (column and order)
void QtCustomTree::setSorting(QString s)
{
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
    if (item->m_storeExp)
	setStoreExpStatus(item->id(),item->isExpanded());
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
    const NamedList& p = all ? (const NamedList&)item : params;
    QTreeWidgetItem* hdr = headerItem();
    QtTreeItemProps* props = treeItemProps(item.type());
    int n = columnCount();
    for (int col = 0; col < n; col++) {
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
	    if (getBoolItemData(col,RoleHtmlDelegate,hdr)) {
		imageRole = RoleImage;
		// HTML delegate
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
	item.setImage(col,id,p,imageRole);
    }
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
    bool on = m_autoExpand;
    if (item.m_storeExp) {
	int n = getStoreExpStatus(item.id());
	if (n >= 0)
	    on = (n > 0);
	else
	    setStoreExpStatus(item.id(),on);
    }
    item.setExpanded(on);
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

// Handle item visiblity changes
void QtCustomTree::itemVisibleChanged(QtTreeItem& item)
{
    // Uncheck columns for invisible item
    if (item.isHidden())
	uncheckItem(item);
}

// Uncheck all checkable columns in a given item
void QtCustomTree::uncheckItem(QtTreeItem& item)
{
    if (!m_hasCheckableCols)
	return;
    QTreeWidgetItem* hdr = headerItem();
    int n = hdr ? columnCount() : 0;
    for (int i = 0; i < n; i++)
	if (hdr->data(i,RoleCheckable).toBool())
	    item.setCheckState(i,false);
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
    for (int n = columnCount() - 1; n >= 0; n--) 
	item.setToolTip(n,QtClient::setUtf8(tooltip));
}

// Fill a list with item statistics.
void QtCustomTree::fillItemStatistics(QtTreeItem& item, NamedList& list)
{
    list.addParam("count",String(item.childCount()));
}

// Update a tree item's statistics
void QtCustomTree::applyItemStatistics(QtTreeItem& item)
{
    QtTreeItemProps* p = treeItemProps(item.type());
    if (!p)
	return;
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
    m_itemPropsType.addParam(String((int)TypeContact),"contact");
    m_itemPropsType.addParam(String((int)TypeChatRoom),"chatroom");
    m_itemPropsType.addParam(String((int)TypeGroup),"group");
    m_savedIndent = indentation();
    m_noGroupText = "None";
    setParams(params);
}

// Set params
bool ContactList::setParams(const NamedList& params)
{
    bool ok = QtCustomTree::setParams(params);
    buildMenu(m_menuContact,params.getParam("contactmenu"));
    buildMenu(m_menuChatRoom,params.getParam("chatroommenu"));
    return ok;
}

// Update a contact
bool ContactList::setTableRow(const String& item, const NamedList* data)
{
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
