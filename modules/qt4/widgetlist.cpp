/**
 * widgetlist.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Custom widget list objects
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

#include "widgetlist.h"

using namespace TelEngine;
namespace { // anonymous

// The factory
class WidgetListFactory : public UIFactory
{
public:
    inline WidgetListFactory(const char* name = "WidgetListFactory")
	: UIFactory(name)
	{ m_types.append(new String("WidgetList")); }
    virtual void* create(const String& type, const char* name, NamedList* params = 0);
};

static const TokenDict s_delItemDict[] = {
    {"global", WidgetList::DelItemGlobal},
    {"single", WidgetList::DelItemSingle},
    {"native", WidgetList::DelItemNative},
    {0,0}
};

static WidgetListFactory s_factory;


/*
 * WidgetListTabWidget
 */
WidgetListTabWidget::WidgetListTabWidget(WidgetList* parent, const NamedList& params)
    : QTabWidget(parent)
{
    // Configure delete item button
#if QT_VERSION >= 0x040500
    if (parent->m_delItemType == WidgetList::DelItemSingle ||
	parent->m_delItemType == WidgetList::DelItemNative) {
	// Set closable tabs
	bool closable = parent->m_delItemType == WidgetList::DelItemNative;
	setTabsClosable(closable);
	// Connect close signal if native close is used
	if (tabsClosable())
	    QtClient::connectObjects(this,SIGNAL(tabCloseRequested(int)),parent,SLOT(closeItem(int)));
    }
#else
    // Override settings: we don't support close button on tab page
    if (parent->m_delItemType != WidgetList::DelItemNone)
	parent->setDelItemType(WidgetList::DelItemGlobal);
#endif
    if (parent->m_delItemType == WidgetList::DelItemGlobal)
	setCloseButton();
}

// Build and set a close button for a given tab or a global close if index is negative
void WidgetListTabWidget::setCloseButton(int index)
{
    WidgetList* list = static_cast<WidgetList*>(parent());
    if (!list)
	return;
    // Check if we can set a close button
#if QT_VERSION >= 0x040500
    if (index >= 0) {
	if (list->m_delItemType != WidgetList::DelItemSingle || tabsClosable() || !tabBar())
	    return;
    }
    else if (list->m_delItemType != WidgetList::DelItemGlobal)
	return;
#else
    if (index >= 0 || list->m_delItemType != WidgetList::DelItemGlobal)
	return;
#endif
    // Build the button
    QToolButton* b = new QToolButton(this);
    b->setProperty("_yate_noautoconnect",QVariant(true));
    if (index >= 0) {
#if QT_VERSION >= 0x040500
	QWidget* w = widget(index);
	String item;
	QtUIWidget::getListItemIdProp(w,item);
	QtUIWidget::setListItemProp(b,QtClient::setUtf8(item));
	tabBar()->setTabButton(index,QTabBar::RightSide,b);
#else
	delete b;
	return;
#endif
    }
    else
	setCornerWidget(b,Qt::TopRightCorner);
    list->applyDelItemProps(b);
    QtClient::connectObjects(b,SIGNAL(clicked()),list,SLOT(closeItem()));
}

// Set tab close button if needed
void WidgetListTabWidget::tabInserted(int index)
{
#if QT_VERSION >= 0x040500
    if (!tabsClosable())
	setCloseButton(index);
#endif
    QTabWidget::tabInserted(index);
}

// Tab removed. Notify the parent
void WidgetListTabWidget::tabRemoved(int index)
{
    WidgetList* list = static_cast<WidgetList*>(parent());
    if (list)
	list->itemRemoved(index);
}

/*
 * WidgetListStackedWidget
 */
WidgetListStackedWidget::WidgetListStackedWidget(WidgetList* parent, const NamedList& params)
    : QStackedWidget(parent)
{
}

/*
 * WidgetList
 */
// Constructor
WidgetList::WidgetList(const char* name, const NamedList& params, QWidget* parent)
    : QtCustomWidget(name,parent),
    m_hideWndWhenEmpty(false),
    m_tab(0),
    m_pages(0),
    m_delItemType(DelItemNone),
    m_delItemProps("")
{
    // Build properties
    QtClient::buildProps(this,params["buildprops"]);
    // Retrieve the delete item props
    updateDelItemProps(params,true);
    const String& type = params["type"];
    XDebug(ClientDriver::self(),DebugAll,"WidgetList(%s) type=%s",name,type.c_str());
    QString wName = buildQChildName(params.getValue("widgetname","widget"));
    if (type == "tabs") {
	m_tab = new WidgetListTabWidget(this,params);
	m_tab->setObjectName(wName);
	QtClient::setWidget(this,m_tab);
	QtClient::connectObjects(m_tab,SIGNAL(currentChanged(int)),
	    this,SLOT(currentChanged(int)));
    }
    else if (type == "pages") {
	QWidget* hdr = 0;
	const String& header = params["header"];
	if (header)
	    hdr = QtWindow::loadUI(Client::s_skinPath + header,this,header);
	if (hdr)
	    hdr->setObjectName(QtClient::setUtf8("pages_header"));
	m_pages = new WidgetListStackedWidget(this,params);
	m_pages->setObjectName(wName);
	QVBoxLayout* newLayout = new QVBoxLayout;
	newLayout->setSpacing(0);
	newLayout->setContentsMargins(0,0,0,0);
	if (hdr)
	    newLayout->addWidget(hdr);
	newLayout->addWidget(m_pages);
	QLayout* l = layout();
	if (l)
	    delete l;
	setLayout(newLayout);
	QtClient::connectObjects(m_pages,SIGNAL(currentChanged(int)),
	    this,SLOT(currentChanged(int)));
	QtClient::connectObjects(m_pages,SIGNAL(widgetRemoved(int)),
	    this,SLOT(itemRemoved(int)));
    }
    // Set navigation
    QtUIWidget::initNavigation(params);
    setParams(params);
}

// Find an item widget by index
QWidget* WidgetList::findItemByIndex(int index)
{
    QWidget* w = 0;
    if (m_tab)
	w = m_tab->widget(index);
    else if (m_pages)
	w = m_pages->widget(index);
    return w;
}

// Set widget parameters
bool WidgetList::setParams(const NamedList& params)
{
    bool ok = QtUIWidget::setParams(params);
    ok = QtUIWidget::setParams(this,params) && ok;
    updateDelItemProps(params);
    return ok;
}

// Get widget's items
bool WidgetList::getOptions(NamedList& items)
{
    QList<QObject*> list = getContainerItems();
    for (int i = 0; i < list.size(); i++)
	if (list[i]->isWidgetType()) {
	    String id;
	    getListItemIdProp(list[i],id);
	    items.addParam(id,"");
	}
    return true;
}

// Retrieve item parameters
bool WidgetList::getTableRow(const String& item, NamedList* data)
{
    QWidget* w = findItem(item);
    DDebug(ClientDriver::self(),DebugAll,"WidgetList(%s)::getTableRow(%s,%p) found=%p",
	name().c_str(),item.c_str(),data,w);
    if (!w)
	return false;
    if (data)
	getParams(w,*data);
    return true;
}

// Add an item
bool WidgetList::addTableRow(const String& item, const NamedList* data, bool atStart)
{
    DDebug(ClientDriver::self(),DebugAll,"WidgetList(%s)::addTableRow(%s,%p,%u)",
	name().c_str(),item.c_str(),data,atStart);
    QWidget* parent = 0;
    if (item) {
	if (m_tab)
	    parent = m_tab;
	else
	    parent = m_pages;
    }
    if (!parent)
	return false;
    const String& type = data ? (*data)["item_type"] : String::empty();
    QWidget* w = loadWidgetType(parent,item,type);
    if (!w)
	return false;
    QtUIWidgetItemProps* p = QtUIWidget::getItemProps(type);
    if (p && p->m_styleSheet)
	applyWidgetStyle(w,p->m_styleSheet);
    if (addItem(w,atStart))
	setTableRow(item,data);
    return w != 0;
}

// Add or set one or more table row(s)
bool WidgetList::updateTableRows(const NamedList* data, bool atStart)
{
    DDebug(ClientDriver::self(),DebugAll,"WidgetList(%s)::updateTableRows(%p,%u)",
	name().c_str(),data,atStart);
    // Save the hide empty window flag
    bool oldHide = m_hideWndWhenEmpty;
    String oldWHide = m_hideWidgetWhenEmpty;
    m_hideWndWhenEmpty = false;
    m_hideWidgetWhenEmpty = "";
    bool ok = true;
    unsigned int n = data->length();
    for (unsigned int i = 0; i < n; i++) {
	if (Client::exiting())
	    break;
	// Get item and the list of parameters
	NamedString* ns = data->getParam(i);
	if (!ns)
	    continue;
	// Delete ?
	if (ns->null()) {
	    ok = delTableRow(ns->name()) && ok;
	    continue;
	}
	// Set existing item or add a new one
	if (getTableRow(ns->name()))
	    ok = setTableRow(ns->name(),YOBJECT(NamedList,ns)) && ok;
	else if (ns->toBoolean())
	    ok = addTableRow(ns->name(),YOBJECT(NamedList,ns),atStart) && ok;
	else
	    ok = false;
    }
    m_hideWndWhenEmpty = oldHide;
    m_hideWidgetWhenEmpty = oldWHide;
    QtUIWidget::updateNavigation();
    hideEmpty();
    return ok;
}

// Delete an item
bool WidgetList::delTableRow(const String& item)
{
    QWidget* w = findItem(item);
    DDebug(ClientDriver::self(),DebugAll,"WidgetList(%s)::delTableRow(%s) found=%p",
	name().c_str(),item.c_str(),w);
    if (!w)
	return false;
    QtClient::deleteLater(w);
    QtUIWidget::updateNavigation();
    hideEmpty();
    return true;
}

// Set existing item parameters
bool WidgetList::setTableRow(const String& item, const NamedList* data)
{
    QWidget* w = findItem(item);
    DDebug(ClientDriver::self(),DebugAll,"WidgetList(%s)::setTableRow(%s,%p) wid=%p",
	name().c_str(),item.c_str(),data,w);
    if (!w)
	return false;
    if (data) {
	if (m_tab) {
	    // Hook some parameters to set them in tab
	    String* name = m_itemTextParam ? data->getParam(m_itemTextParam) : 0;
	    if (name)
		m_tab->setTabText(m_tab->indexOf(w),QtClient::setUtf8(*name));
	    if (m_itemImgParam) {
		String* tmp = data->getParam("image:" + m_itemImgParam);
		if (tmp)
		    m_tab->setTabIcon(m_tab->indexOf(w),QIcon(QtClient::setUtf8(*tmp)));
	    }
	}
	QtUIWidget::setParams(w,*data);
    }
    return true;
}

// Delete all items
bool WidgetList::clearTable()
{
    if (m_tab || m_pages) {
	QList<QObject*> list = getContainerItems();
	for (int i = 0; i < list.size(); i++)
	    if (list[i]->isWidgetType())
		QtClient::deleteLater(list[i]);
    }
    else
	return false;
    QtUIWidget::updateNavigation();
    hideEmpty();
    return true;
}

// Select (set active) an item
bool WidgetList::setSelect(const String& item)
{
    QWidget* w = findItem(item);
    if (!w)
	return false;
    if (m_tab)
	m_tab->setCurrentWidget(w);
    else if (m_pages)
	m_pages->setCurrentWidget(w);
    else
	return false;
    QtUIWidget::updateNavigation();
    return true;
}

// Retrieve the selected (active) item
bool WidgetList::getSelect(String& item)
{
    QWidget* w = selectedItem();
    if (w)
	QtUIWidget::getListItemIdProp(w,item);
    DDebug(ClientDriver::self(),DebugAll,"WidgetList(%s)::getSelect() '%s' wid=%p",
	name().c_str(),item.c_str(),w);
    return w != 0;
}

// Retrieve a QObject list containing container items
QList<QObject*> WidgetList::getContainerItems()
{
    QList<QObject*> list;
    if (m_tab) {
	int n = m_tab->count();
	for (int i = 0; i < n; i++) {
	    QWidget* w = m_tab->widget(i);
	    if (w)
		list.append(static_cast<QObject*>(w));
	}
    }
    else if (m_pages) {
	int n = m_pages->count();
	for (int i = 0; i < n; i++) {
	    QWidget* w = m_pages->widget(i);
	    if (w)
		list.append(static_cast<QObject*>(w));
	}
    }
    return list;
}

// Select an item by its index
bool WidgetList::setSelectIndex(int index)
{
    if (index < 0 || index >= itemCount())
	return false;
    QWidget* w = findItemByIndex(index);
    String item;
    if (w)
	QtUIWidget::getListItemIdProp(w,item);
    return item ? setSelect(item) : false;
}

// Retrieve the 0 based index of the current item
int WidgetList::currentItemIndex()
{
    if (m_tab)
	return m_tab->currentIndex();
    if (m_pages)
	return m_pages->currentIndex();
    return -1;
}

// Retrieve the number of items in container
int WidgetList::itemCount()
{
    if (m_tab)
	return m_tab->count();
    if (m_pages)
	return m_pages->count();
    return -1;
}

// Set _yate_hidewndwhenempty property value. Apply it if changed
void WidgetList::setHideWndWhenEmpty(bool value)
{
    if (m_hideWndWhenEmpty == value)
	return;
    m_hideWndWhenEmpty = value;
    hideEmpty();
}

// Set _yate_hidewidgetwhenempty property value. Apply it if changed
void WidgetList::setHideWidgetWhenEmpty(QString value)
{
    String s;
    QtClient::getUtf8(s,value);
    if (m_hideWidgetWhenEmpty == s)
	return;
    m_hideWidgetWhenEmpty = s;
    hideEmpty();
}

// Start/stop item flash
void WidgetList::setFlashItem(QString value)
{
    if (!m_tab)
	return;
    String on;
    String item;
    int pos = value.indexOf(':');
    if (pos > 0) {
	QtClient::getUtf8(on,value.left(pos));
	QtClient::getUtf8(item,value.right(value.length() - pos - 1));
    }
    else
	return;
    QWidget* w = findItem(item);
    if (!w)
	return;
    int index = m_tab->indexOf(w);
    if (on.toBoolean())
	m_tab->setTabTextColor(index,QColor("green"));
    else
	m_tab->setTabTextColor(index,QColor("black"));
}

// Handle selection changes
void WidgetList::currentChanged(int index)
{
    String item;
    if (index >= 0 && index < itemCount()) {
	QWidget* w = findItemByIndex(index);
	if (w)
	    QtUIWidget::getListItemIdProp(w,item);
	// Avoid notifying no selection
	if (!item)
	    return;
    }
    QtWindow* wnd = item ? QtClient::parentWindow(this) : 0;
    if (wnd)
	Client::self()->select(wnd,name(),item);
}

// Item removed slot. Notify the client when empty
void WidgetList::itemRemoved(int index)
{
    if (itemCount())
	return;
    QtWindow* wnd = QtClient::parentWindow(this);
    if (wnd)
	Client::self()->select(wnd,name(),String::empty());
}

// Handle current item close action
void WidgetList::closeItem(int index)
{
    if (!m_delItemActionPrefix)
	return;
    String item;
    if (index < 0) {
	if (m_delItemType == DelItemSingle)
	    QtUIWidget::getListItemProp(sender(),item);
	else if (m_delItemType == DelItemGlobal)
	    getSelect(item);
    }
    else if (m_delItemType == DelItemNative) {
	// Signalled by tab native close button
	QWidget* w = findItemByIndex(index);
	if (w)
	    QtUIWidget::getListItemIdProp(w,item);
    }
    XDebug(ClientDriver::self(),DebugAll,
	"WidgetList(%s)::closeItem(%d) sender (%p,%s) found=%s",
	name().c_str(),index,sender(),YQT_OBJECT_NAME(sender()),item.c_str());
    QtWindow* wnd = item ? QtClient::parentWindow(this) : 0;
    if (wnd)
	Client::self()->action(wnd,m_delItemActionPrefix + item);
}

// Handle children events
bool WidgetList::eventFilter(QObject* watched, QEvent* event)
{
    if (!Client::valid())
	return QtCustomWidget::eventFilter(watched,event);
    if (event->type() == QEvent::KeyPress) {
	if (m_wndEvHooked) {
	    QtWindow* wnd = qobject_cast<QtWindow*>(watched);
	    if (wnd && wnd == getWindow()) {
		QString child;
		QWidget* sel = selectedItem();
		if (sel && buildQChildNameProp(child,sel,"_yate_keypress_redirect") &&
		    QtClient::sendEvent(*event,sel,child)) {
		    QWidget* wid = qFindChild<QWidget*>(sel,child);
		    if (wid)
			wid->setFocus();
		    return true;
		}
		return QtCustomWidget::eventFilter(watched,event);
	    }
	}
	bool filter = false;
	if (!filterKeyEvent(watched,static_cast<QKeyEvent*>(event),filter))
	    return QtCustomWidget::eventFilter(watched,event);
	return filter;
    }
    return QtCustomWidget::eventFilter(watched,event);
}

// Hide the parent window if the container is empty
void WidgetList::hideEmpty()
{
    if (itemCount() || !Client::valid() || !(m_hideWndWhenEmpty || m_hideWidgetWhenEmpty))
	return;
    QtWindow* wnd = QtClient::parentWindow(this);
    if (!wnd)
	return;
    if (m_hideWndWhenEmpty)
	Client::self()->setVisible(wnd->id(),false);
    if (m_hideWidgetWhenEmpty)
	wnd->setShow(m_hideWidgetWhenEmpty,false);
}

// Insert/add a widget item
bool WidgetList::addItem(QWidget*& w, bool atStart)
{
    if (!w)
	return false;
    int index = atStart ? 0 : itemCount();
    if (m_tab)
	m_tab->insertTab(index,w,QString());
    else if (m_pages)
	m_pages->insertWidget(index,w);
    else {
	QtClient::deleteLater(w);
	w = 0;
    }
    if (w)
	QtUIWidget::updateNavigation();
    return w != 0;
}

// Retrieve the selected item widget
QWidget* WidgetList::selectedItem()
{
    if (m_tab)
	return m_tab->currentWidget();
    if (m_pages)
	return m_pages->currentWidget();
    return 0;
}

// Set delete item type
void WidgetList::setDelItemType(int type)
{
    if (type == m_delItemType)
	return;
    m_delItemType = type;
    XDebug(ClientDriver::self(),DebugAll,"WidgetList(%s)::setDelItemType(%d = %s)",
	name().c_str(),type,lookup(type,s_delItemDict));
}

// Retrieve delete item object properties
void WidgetList::updateDelItemProps(const NamedList& params, bool first)
{
    static const String s_delItemProp = "delete_item_property:";
    if (first) {
	m_delItemActionPrefix = params.getValue("delete_item_action");
	if (m_delItemActionPrefix) {
	    setDelItemType(params.getIntValue("delete_item_type",s_delItemDict,DelItemNone));
	    if (m_delItemType != DelItemNone)
		m_delItemActionPrefix << ":" << this->name() << ":";
	    else
		m_delItemActionPrefix.clear();
	}
    }
    if (m_delItemType == DelItemNone)
	return;
    unsigned int n = params.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = params.getParam(i);
	if (!(ns && ns->name().startsWith(s_delItemProp)))
	    continue;
	String prop = ns->name().substr(s_delItemProp.length());
	if (!prop)
	    continue;
	m_delItemProps.setParam(prop,*ns);
	// TODO: Apply the property to all delete item objects if changed
    }
}

// Apply delete item object properties
void WidgetList::applyDelItemProps(QObject* obj)
{
    if (!obj)
	return;
    unsigned int n = m_delItemProps.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = m_delItemProps.getParam(i);
	if (!ns)
	    continue;
	DDebug(ClientDriver::self(),DebugAll,
	    "WidgetList(%s)::applyDelItemProps() %s=%s",
	    name().c_str(),ns->name().c_str(),ns->c_str());
	QtClient::setProperty(obj,ns->name(),*ns);
    }
}

/*
 * WidgetListFactory
 */
// Build objects
void* WidgetListFactory::create(const String& type, const char* name, NamedList* params)
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
    if (type == "WidgetList")
	return new WidgetList(name,*params,parentWidget);
    return 0;
}

}; // anonymous namespace

#include "widgetlist.moc"

/* vi: set ts=8 sw=4 sts=4 noet: */
