/**
 * qt4client.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * A Qt-4 based universal telephony client
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

#include "qt4client.h"
#include <QtUiTools>

#ifdef _WINDOWS
#define DEFAULT_DEVICE "dsound/*"
#elif defined(__linux__)
#define DEFAULT_DEVICE "alsa/default"
#else
#define DEFAULT_DEVICE "oss//dev/dsp"
#endif

namespace TelEngine {

// Utility: get an UTF8 representation of a QT string
#define qtGetUtf8(str) str.toUtf8().constData()

// Utility: set a QT string from an UTF8 char buffer
inline QString qtSetUtf8(const char* src)
{
    return QString::fromUtf8(src?src:"");
}

// Factory used to create objects in client's thread
class Qt4ClientFactory : public UIFactory
{
public:
    Qt4ClientFactory(const char* name = "Qt4ClientFactory");
    virtual void* create(const String& type, const char* name, NamedList* params = 0);
};

// Class used for temporary operations on QT widgets
// Keeps a pointer to a widget and its type
// NOTE: The methods of this class don't check the widget pointer
class QtWidget
{
public:
    enum Type {
	PushButton     = 0,
	CheckBox       = 1,
	Table          = 2,
	ListBox        = 3,
	ComboBox       = 4,
	Tab            = 5,
	StackWidget    = 6,
	TextEdit       = 7,
	Label          = 8,
	LineEdit       = 9,
	AbstractButton = 10,
	Slider         = 11,
	ProgressBar    = 12,
	Unknown        = 13,             // Unknown type
	Action,                          // QAction descendant
	CustomTable,                     // QtTable descendant
	Missing                          // Invalid pointer
    };
    // Set widget from object
    inline QtWidget(QObject* w)
	: m_widget(0), m_action(0), m_type(Missing) {
	    if (!w)
		return;
	    if (w->inherits("QWidget"))
		m_widget = static_cast<QWidget*>(w);
	    else if (w->inherits("QAction"))
		m_action = static_cast<QAction*>(w);
	    m_type = getType();
	}
    // Set widget from object and type
    inline QtWidget(QWidget* w, int t)
	: m_widget(w), m_action(0), m_type(t) {
	    if (!m_widget)
		m_type = Missing;
	}
    // Set widget/action from object and name
    inline QtWidget(QtWindow* wnd, const String& name)
	: m_widget(0), m_action(0), m_type(Missing) {
	    m_widget = qFindChild<QWidget*>(wnd,name.c_str());
	    if (!m_widget)
		m_action = qFindChild<QAction*>(wnd,name.c_str());
	    m_type = getType();
	}
    inline bool valid() const
	{ return type() != Missing; }
    inline bool invalid() const
	{ return type() == Missing; }
    inline int type() const
	{ return m_type; }
    inline const char* name() {
	    return m_widget ? qtGetUtf8(m_widget->objectName()) :
		(m_action ? qtGetUtf8(m_action->objectName()) : "");
	}
    inline operator QWidget*()
	{ return m_widget; }
    inline bool inherits(const char* classname)
	{ return m_widget && m_widget->inherits(classname); }
    inline bool inherits(Type t)
	{ return inherits(s_types[t]); }
    inline QWidget* operator ->()
	{ return m_widget; }
    // Static cast methods
    inline QPushButton* button()
	{ return static_cast<QPushButton*>(m_widget); }
    inline QCheckBox* check()
	{ return static_cast<QCheckBox*>(m_widget); }
    inline QTableWidget* table()
	{ return static_cast<QTableWidget*>(m_widget); }
    inline QListWidget* list()
	{ return static_cast<QListWidget*>(m_widget); }
    inline QComboBox* combo()
	{ return static_cast<QComboBox*>(m_widget); }
    inline QTabWidget* tab()
	{ return static_cast<QTabWidget*>(m_widget); }
    inline QStackedWidget* stackWidget()
	{ return static_cast<QStackedWidget*>(m_widget); }
    inline QTextEdit* textEdit()
	{ return static_cast<QTextEdit*>(m_widget); }
    inline QLabel* label()
	{ return static_cast<QLabel*>(m_widget); }
    inline QLineEdit* lineEdit()
	{ return static_cast<QLineEdit*>(m_widget); }
    inline QAbstractButton* abstractButton()
	{ return static_cast<QAbstractButton*>(m_widget); }
    inline QSlider* slider()
	{ return static_cast<QSlider*>(m_widget); }
    inline QProgressBar* progressBar()
	{ return static_cast<QProgressBar*>(m_widget); }
    inline QtTable* customTable()
	{ return qobject_cast<QtTable*>(m_widget); }
    inline QAction* action()
	{ return m_action; }

    int getType() {
	    if (m_widget) {
		String cls = m_widget->metaObject()->className(); 
		for (int i = 0; i < Unknown; i++)
		    if (s_types[i] == cls)
			return i;
		if (customTable())
		    return CustomTable;
		return Unknown;
	    }
	    if (m_action && m_action->inherits("QAction"))
		return Action;
	    return Missing;
	}
    static String s_types[Unknown];
protected:
    QWidget* m_widget;
    QAction* m_action;
    int m_type;
private:
    QtWidget() {}
};

// Class used for temporary operations on QTableWidget objects
// NOTE: The methods of this class don't check the table pointer
class TableWidget : public GenObject
{
public:
    TableWidget(QTableWidget* table, bool tmp = true);
    TableWidget(QtWindow* wnd, const String& name, bool tmp = true);
    TableWidget(QtWidget& table, bool tmp = true);
    ~TableWidget();
    inline QTableWidget* table()
	{ return m_table; }
    inline bool valid()
	{ return m_table != 0; }
    inline QtTable* customTable()
	{ return (valid()) ? qobject_cast<QtTable*>(m_table) : 0; }
    inline const String& name()
	{ return m_name; }
    inline int rowCount()
	{ return m_table->rowCount(); }
    inline int columnCount()
	{ return m_table->columnCount(); }
    inline void setHeaderText(int col, const char* text) {
	    if (col < columnCount())
		m_table->setHorizontalHeaderItem(col,
		    new QTableWidgetItem(text ? text : ""));
	}
    inline bool getHeaderText(int col, String& dest, bool lower = true) {
    	    QTableWidgetItem* item = m_table->horizontalHeaderItem(col);
	    if (item)	{
		dest = qtGetUtf8(item->text());
		if (lower)
		    dest.toLower();
	    }
	    return item != 0;
	}
    // Get the current selection's row
    inline int crtRow() {
	    QList<QTableWidgetItem*> items = m_table->selectedItems();
	    if (items.size())
		return items[0]->row();
	    return -1;
	}
    inline void repaint()
	{ m_table->repaint(); }
    inline void addRow(int index)
	{ m_table->insertRow(index); }
    inline void delRow(int index) {
	    if (index >= 0)
		m_table->removeRow(index);
	}
    inline void addColumn(int index, int width = -1, const char* name = 0) {
	    m_table->insertColumn(index);
	    if (width >= 0)
		m_table->setColumnWidth(index,width); 
	    setHeaderText(index,name);
	}
    inline void setImage(int row, int col, const String& image) {
	    QTableWidgetItem* item = m_table->item(row,col);
	    if (item)
		item->setIcon(QIcon(image.c_str()));
	}
    inline void addCell(int row, int col, const String& value) {
	    QTableWidgetItem* item = new QTableWidgetItem(value.safe());
	    m_table->setItem(row,col,item);
	}
    inline void setCell(int row, int col, const String& value, bool addNew = true) {
	    QTableWidgetItem* item = m_table->item(row,col);
	    if (item)
		item->setText(value.safe());
	    else if (addNew)
		addCell(row,col,value);
	}
    inline bool getCell(int row, int col, String& dest, bool lower = false) {
    	    QTableWidgetItem* item = m_table->item(row,col);
	    if (item) {
		dest = qtGetUtf8(item->text());
		if (lower)
		    dest.toLower();
		return true;
	    }
	    return false;
	}
    inline void setID(int row, const String& value)
	{ setCell(row,0,value); }
    // Update a row from a list of parameters
    void updateRow(int row, const NamedList& data);
    // Find a row by the first's column value. Return -1 if not found 
    int getRow(const String& item);
    // Find a column by its label. Return -1 if not found 
    int getColumn(const String& name, bool caseInsentive = true);
    // Save/Load the table column's widths from/to a comma separated list
    void colWidths(bool save, const String& section);
protected:
    void init(bool tmp);
private:
    QTableWidget* m_table;               // The table
    String m_name;                       // Table's name
    int m_sortControl;                   // Flag used to set/reset sorting attribute of the table
};

}; // namespace TelEngine

using namespace TelEngine;

// Dynamic properies
static String s_propFrameless = "dynamicFrameless";   // Windows: show/hide the border
static String s_propHHeader = "dynamicHHeader";       // Tables: show/hide the horizontal header
static String s_qtPropPrefix = "_q_";                 // QT dynamic properties prefix
//
static Qt4ClientFactory s_qt4Factory;
static Configuration s_cfg;
static Configuration s_save;
static Configuration s_callHistory;

String QtWidget::s_types[QtWidget::Unknown] = {
    "QPushButton",
    "QCheckBox",
    "QTableWidget",
    "QListWidget",
    "QComboBox",
    "QTabWidget",
    "QStackedWidget",
    "QTextEdit",
    "QLabel",
    "QLineEdit",
    "QAbstractButton",
    "QSlider",
    "QProgressBar"
};

// Connect a sender's signal to a receiver's slot
#define YQT_CONNECT(sender,signal,receiver,method,sName) \
    if (QObject::connect(sender,SIGNAL(signal),receiver,SLOT(method))) \
	DDebug(QtDriver::self(),DebugAll,"Connected sender=%s signal=%s to receiver=%s", \
	    qtGetUtf8(sender->objectName()),sName,qtGetUtf8(receiver->objectName())); \
    else \
	Debug(QtDriver::self(),DebugWarn,"Failed to connect sender=%s signal=%s to receiver=%s", \
	    qtGetUtf8(sender->objectName()),sName,qtGetUtf8(receiver->objectName()))


// Utility: get a list row containing the given text
static int findListRow(QListWidget& list, const String& item)
{
    QString it(qtSetUtf8(item));
    for (int i = 0; i < list.count(); i++) {
	QListWidgetItem* tmp = list.item(i);
	if (tmp && it == tmp->text())
	    return i;
    }
    return -1;
}

// Utility: find a stacked widget's page with the given name
static int findStackedWidget(QStackedWidget& w, const String& name)
{
    QString n(qtSetUtf8(name));
    for (int i = 0; i < w.count(); i++) {
	QWidget* page = w.widget(i);
	if (page && n == page->objectName())
	    return i;
    }
    return -1;
}

// Utility: Insert a widget into another
static void setWidget(QWidget* parent, QWidget* child)
{
    if (!(parent && child))
	return;
    QVBoxLayout* layout = new QVBoxLayout;
    layout->setSpacing(0);
#if QT_VERSION < 0x040300
    layout->setMargin(0);
#else
    layout->setContentsMargins(0,0,0,0);
#endif
    layout->addWidget(child);
    QLayout* l = parent->layout();
    if (l)
	delete l;
    parent->setLayout(layout);
}

// Utility function used to get the name of a control
static bool translateName(QtWidget& w, String& name)
{
    if (w.invalid())
	return false;
    if (w.type() != QtWidget::Action)
	if (w->accessibleName().isEmpty()) 
	    name = qtGetUtf8(w->objectName());
	else
	    name = qtGetUtf8(w->accessibleName());
    else
	name = qtGetUtf8(w.action()->objectName());
    return true;
}

// Utility: raise a select event if a list is empty
inline void raiseSelectIfEmpty(int count, Window* wnd, const String& name)
{
    if (count <= 0 && Client::self())
	Client::self()->select(wnd,name,String::empty());
}


/**
 * Qt4ClientFactory
 */
Qt4ClientFactory::Qt4ClientFactory(const char* name)
    : UIFactory(name)
{
    m_types.append(new String("QSound"));
}

// Build QSound
void* Qt4ClientFactory::create(const String& type, const char* name, NamedList* params)
{
    if (type == "QSound")
	return new QSound(name);
    return 0;
}


/**
 * TableWidget
 */
TableWidget::TableWidget(QTableWidget* table, bool tmp)
    : m_table(table), m_sortControl(-1)
{
    if (!m_table)
	return;
    init(tmp);
}

TableWidget::TableWidget(QtWindow* wnd, const String& name, bool tmp)
    : m_table(0), m_sortControl(-1)
{
    if (wnd)
	m_table = qFindChild<QTableWidget*>(wnd,name.c_str());
    if (!m_table)
	return;
    init(tmp);
}

TableWidget::TableWidget(QtWidget& table, bool tmp)
    : m_table(static_cast<QTableWidget*>((QWidget*)table)), m_sortControl(-1)
{
    if (m_table)
	init(tmp);
}

TableWidget::~TableWidget()
{
    if (!m_table)
	return;
    if (m_sortControl >= 0)
	m_table->setSortingEnabled((bool)m_sortControl);
    m_table->verticalHeader()->hide();
    m_table->repaint();
}

// Update a row from a list of parameters
void TableWidget::updateRow(int row, const NamedList& data)
{
    int ncol = columnCount();
    for(int i = 0; i < ncol; i++) {
	String header;
	if (!getHeaderText(i,header))
	    continue;
	NamedString* tmp = data.getParam(header);
	if (tmp)
	    setCell(row,i,*tmp);
	// Set image
	tmp = data.getParam(header + "_image");
	if (tmp)
	    setImage(row,i,*tmp);
    }
}

// Find a row by the first's column value. Return -1 if not found 
int TableWidget::getRow(const String& item)
{
    int n = rowCount();
    for (int i = 0; i < n; i++) {
	String val;
	if (getCell(i,0,val) && item == val)
	    return i;
    }
    return -1;
}

// Find a column by its label. Return -1 if not found 
int TableWidget::getColumn(const String& name, bool caseInsensitive)
{
    int n = columnCount();
    for (int i = 0; i < n; i++) {
	String val;
	if (!getHeaderText(i,val,false))
	    continue;
	if ((caseInsensitive && (name |= val)) || (name == val))
	    return i;
    }
    return -1;
}

// Load/save the table column's widths to/from a comma separated list
void TableWidget::colWidths(bool save, const String& section)
{
    String param(m_name + "_col_widths");
    if (save) {
	unsigned int n = columnCount();
	String widths;
	for (unsigned int i = 0; i < n; i++)
	    widths.append(String(m_table->columnWidth(i)),",",true);
	s_save.setValue(section,param,widths);
	return;
    }
    // Load
    String widths = s_save.getValue(section,param);
    ObjList* list = widths.split(',');
    unsigned int col = 0;
    for (ObjList* o = list->skipNull(); o; o = o->skipNext(), col++) {
	int width = (static_cast<String*>(o->get()))->toInteger(-1);
	if (width >= 0)
	    m_table->setColumnWidth(col,width); 
    }
    TelEngine::destruct(list);
}

void TableWidget::init(bool tmp)
{
    m_name = qtGetUtf8(m_table->objectName());
    if (tmp)
	m_sortControl = m_table->isSortingEnabled() ? 1 : 0; 
}


/**
 * QtWindow
 */
QtWindow::QtWindow()
    : m_x(0), m_y(0), m_width(0), m_height(0),
    m_maximized(false), m_mainWindow(false), m_moving(false)
{
}

QtWindow::QtWindow(const char* name, const char* description)
    : Window(name), m_description(description),
    m_x(0), m_y(0), m_width(0), m_height(0),
    m_maximized(false), m_mainWindow(false)
{
    setObjectName(name);
    setAccessibleName(description);
}

QtWindow::~QtWindow()
{
    // Save settings
    if (m_saveOnClose) {
	m_maximized = isMaximized();
	s_save.setValue(m_id,"maximized",String::boolText(m_maximized));
	// Don't save position if maximized: keep the old one
	if (!m_maximized) {
	    s_save.setValue(m_id,"x",m_x);
	    s_save.setValue(m_id,"y",m_y);
	    s_save.setValue(m_id,"width",m_width);
	    s_save.setValue(m_id,"height",m_height);
	}
	s_save.setValue(m_id,"visible",m_visible);
	// Save tables
	QList<QTableWidget*> tables = qFindChildren<QTableWidget*>(this);
	for (int i = 0; i < tables.size(); i++) {
	    TableWidget t(tables[i]);
	    t.colWidths(true,m_id);
	}
    }
}

// Set windows title
void QtWindow::title(const String& text)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::title(%s) [%p]",text.c_str(),this);
    Window::title(text);
    QWidget::setWindowTitle(text.c_str());
}

void QtWindow::context(const String& text)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::context(%s) [%p]",text.c_str(),this);
    m_context = text;
}

bool QtWindow::setParams(const NamedList& params)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::setParams() [%p]",this);
    if (params.getBoolValue("modal"))
	setWindowModality(Qt::ApplicationModal);
    if (params.getBoolValue("minimized"))
	QWidget::setWindowState(Qt::WindowMinimized);
    return Window::setParams(params);
}

void QtWindow::setOver(const Window* parent)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::setOver(%p) [%p]",parent,this);
    QWidget::raise();
}

bool QtWindow::hasElement(const String& name)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::hasElement(%s) [%p]",name.c_str(),this);
    QtWidget w(this,name);
    return w.valid();
}

bool QtWindow::setActive(const String& name, bool active)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::setActive(%s,%s) [%p]",
	name.c_str(),String::boolText(active),this);
    QtWidget w(this,name);
    if (w.invalid())
	return false;
    if (w.type() != QtWidget::Action)
	w->setEnabled(active);
    else
	w.action()->setEnabled(active);
    return true;
}

bool QtWindow::setFocus(const String& name, bool select)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::setFocus(%s,%s) [%p]",
	name.c_str(),String::boolText(select),this);
    QtWidget w(this,name);
    if (w.invalid())
	return false;
    w->setFocus();
    switch (w.type()) {
	case  QtWidget::ComboBox:
	    if (w.combo()->isEditable() && select)
		w.combo()->lineEdit()->selectAll();
	    break;
    }
    return true;
}

bool QtWindow::setShow(const String& name, bool visible)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::setShow(%s,%s) [%p]",
	name.c_str(),String::boolText(visible),this);
    QtWidget w(this,name);
    if (w.invalid())
	return false;
    if (w.type() != QtWidget::Action)
	w->setVisible(visible);
    else
	w.action()->setVisible(visible);
    return true;
}

bool QtWindow::setText(const String& name, const String& text,
	bool richText)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) setText(%s,%s) [%p]",
	m_id.c_str(),name.c_str(),text.c_str(),this);

    QtWidget w(this,name);
    if (w.invalid())
	return false;
    switch (w.type()) {
	case QtWidget::CheckBox:
	    w.check()->setText(text.c_str());
	    return true;
	case QtWidget::LineEdit:
	    w.lineEdit()->setText(text.c_str());
	    return true;
	case QtWidget::TextEdit:
	    if (richText) {
		w.textEdit()->clear();
		w.textEdit()->insertHtml(text.c_str());
	    }
	    else
		w.textEdit()->setText(text.c_str());
	    return true;
	case QtWidget::Label:
	    w.label()->setText(text.c_str());
	    return true;
	case QtWidget::ComboBox:
	    if (w.combo()->lineEdit())
		w.combo()->lineEdit()->setText(text.c_str());
	    else
		setSelect(name,text);
	    return true;
	case QtWidget::Action:
	    w.action()->setText(text.c_str());
	    return true;
    }

    // Handle some known base classes having a setText() method
    if (w.inherits(QtWidget::AbstractButton))
	w.abstractButton()->setText(text.c_str());
    else
	return false;
    return true;
}

bool QtWindow::setCheck(const String& name, bool checked)
{
    XDebug(QtDriver::self(), DebugInfo, "QtWindow::setCheck(%s,%s) [%p]",
	name.c_str(),String::boolText(checked),this);
    QtWidget w(this,name);
    if (w.invalid())
	return false;
    if (w.inherits(QtWidget::AbstractButton))
	w.abstractButton()->setChecked(checked);
    else if (w.type() == QtWidget::Action)
	w.action()->setChecked(checked);
    else
	return false;
    return true;
}

bool QtWindow::setSelect(const String& name, const String& item)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::setSelect(%s,%s) [%p]",
	name.c_str(),item.c_str(),this);

    QtWidget w(this,name);
    if (w.invalid() || !item)
	return false;

    int d = 0;
    switch (w.type()) {
	case QtWidget::Table:
	    {
		TableWidget t(w);
		int row = t.getRow(item);
		if (row < 0)
		    return false;
		t.table()->setCurrentCell(row,0);
		return true;
	    }
	case QtWidget::ComboBox:
	    d = w.combo()->findText(item.c_str());
	    if (d < 0)
		return false;
	    w.combo()->setCurrentIndex(d);
	    return true;
	case QtWidget::ListBox:
	    d = findListRow(*(w.list()),item);
	    if (d >= 0)
		w.list()->setCurrentRow(d);
	    return d >= 0;
	case QtWidget::Slider:
	    w.slider()->setValue(item.toInteger());
	    return true;
	case QtWidget::StackWidget:
	    d = item.toInteger(-1);
	    while (d < 0) {
		d = findStackedWidget(*(w.stackWidget()),item);
		if (d >= 0)
		    break;
		// Check for a default widget
		String def;
		def << qtGetUtf8(w.stackWidget()->objectName()) << "_default";
		d = findStackedWidget(*(w.stackWidget()),def);
		break;
	    }
	    if (d >= 0 && d < w.stackWidget()->count()) {
		w.stackWidget()->setCurrentIndex(d);
		return true;
	    }
	    return false;
	case QtWidget::ProgressBar:
	    d = item.toInteger();
	    if (d >= w.progressBar()->minimum() && d <= w.progressBar()->maximum())
		w.progressBar()->setValue(d);
	    else if (d < w.progressBar()->minimum())
		w.progressBar()->setValue(w.progressBar()->minimum());
	    else
		w.progressBar()->setValue(w.progressBar()->maximum());
	    return true;
    }
    return false;
}

bool QtWindow::setUrgent(const String& name, bool urgent)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::setUrgent(%s,%s) [%p]",
	name.c_str(),String::boolText(urgent),this);
    return false;
}

bool QtWindow::hasOption(const String& name, const String& item)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::hasOption(%s,%s) [%p]",
	name.c_str(),item.c_str(),this);
    QtWidget w(this,name);
    if (w.invalid())
	return false;
    switch (w.type()) {
	case QtWidget::ComboBox:
	    return -1 != w.combo()->findText(item.c_str());
	case QtWidget::Table:
	    return getTableRow(name,item);
	case QtWidget::ListBox:
	    return 0 <= findListRow(*(w.list()),item);
    }
    return false;
}

bool QtWindow::addOption(const String& name, const String& item, bool atStart,
	const String& text)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) addOption(%s,%s,%s,%s) [%p]",
	m_id.c_str(),name.c_str(),item.c_str(),
	String::boolText(atStart),text.c_str(),this);

    QtWidget w(this,name);
    switch (w.type()) {
	case QtWidget::ComboBox:
	    if (atStart) {
		w.combo()->insertItem(0,item.c_str());
		if (w.combo()->lineEdit())
		    w.combo()->lineEdit()->setText(w.combo()->itemText(0));
	    }
	    else 
		w.combo()->addItem(item.c_str());
	    return true;
	case QtWidget::Table:
	    return addTableRow(name,item,0,atStart);
	case QtWidget::ListBox:
	    if (atStart)
		w.list()->insertItem(0,item.c_str());
	    else
		w.list()->addItem(item.c_str());
	    return true;
    }
    return false;
}

bool QtWindow::delOption(const String& name, const String& item)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) delOption(%s,%s) [%p]",
	m_id.c_str(),name.c_str(),item.c_str(),this);

    QtWidget w(this,name);
    if (w.invalid())
	return false;
    int row = -1;
    switch (w.type()) {
	case QtWidget::ComboBox:
	    row = w.combo()->findText(item.c_str());
	    if (row >= 0) {
		w.combo()->removeItem(row);
		raiseSelectIfEmpty(w.combo()->count(),this,name);
	    }
	    break;
	case QtWidget::Table:
	    return delTableRow(name,item);
	case QtWidget::ListBox:
	    row = findListRow(*(w.list()),item);
	    if (row >= 0) {
		QStringListModel* model = (QStringListModel*)w.list()->model();
		if (!(model && model->removeRow(row)))
		    row = -1;
		raiseSelectIfEmpty(w.list()->count(),this,name);
	    }
	    break;
    }
    return row >= 0;
}

bool QtWindow::getOptions(const String& name, NamedList* items)
{
    Debug(QtDriver::self(),DebugAll,"QtWindow(%s) getOptions(%s,%p) [%p]",
	m_id.c_str(),name.c_str(),items,this);

    QtWidget w(this,name);
    if (w.invalid())
	return false;
    if (!items)
	return true;

    switch (w.type()) {
	case QtWidget::ComboBox:
	    for (int i = 0; i < w.combo()->count(); i++)
		items->addParam(qtGetUtf8(w.combo()->itemText(i)),"");
	    break;
	case QtWidget::Table:
	    {
		TableWidget t(w.table(),false);
		for (int i = 0; i < t.rowCount(); i++) {
		    String item;
		    if (t.getCell(i,0,item) && item)
			items->addParam(item,"");
		}
	    }
	    break;
	case QtWidget::ListBox:
	    for (int i = 0; i < w.list()->count(); i++) {
		QListWidgetItem* tmp = w.list()->item(i);
		if (tmp)
		    items->addParam(qtGetUtf8(tmp->text()),"");
	    }
	    break;
	case QtWidget::CustomTable:
	    return w.customTable()->getOptions(*items);
    }
    return true;
}

// Append or insert text lines to a widget
bool QtWindow::addLines(const String& name, const NamedList* lines, unsigned int max, 
	bool atStart)
{
    DDebug(ClientDriver::self(),DebugAll,"QtWindow(%s) addLines('%s',%p,%u,%s) [%p]",
	m_id.c_str(),name.c_str(),lines,max,String::boolText(atStart),this);

    QtWidget w(this,name);
    if (w.invalid())
	return false;
    if (!lines)
	return true;
    unsigned int count = lines->count();
    if (!count)
	return true;

    switch (w.type()) {
	case QtWidget::TextEdit:
	    // Limit the maximum number of paragraphs
	    if (max) {
		QTextDocument* doc = w.textEdit()->document();
		if (!doc)
		    return false;
		doc->setMaximumBlockCount((int)max);
	    }
	    {
		// FIXME: delete lines from begining if appending and the number
		//  of lines exceeds the maximum allowed
		unsigned int n = lines->length();
		QString s = w.textEdit()->toPlainText();
		int pos = atStart ? 0 : s.length();
		for (unsigned int i = 0; i < n; i++) {
		    NamedString* ns = lines->getParam(i);
		    if (!ns)
			continue;
		    if (ns->name().endsWith("\n"))
			s.insert(pos,ns->name().c_str());
		    else {
			String tmp = ns->name() + "\n";
			s.insert(pos,tmp.c_str());
			pos++;
		    }
		    pos += (int)ns->name().length();
		}
		w.textEdit()->setText(s);
		// Scroll down if added at end
		if (!atStart) {
		    QScrollBar* bar = w.textEdit()->verticalScrollBar();
		    if (bar)
			bar->setSliderPosition(bar->maximum());
		}
	    }
	    return true;
	case QtWidget::Table:
	    // TODO: implement
	    break;
	case QtWidget::ComboBox:
	    // TODO: implement
	    break;
	case QtWidget::ListBox:
	    // TODO: implement
	    break;
    }
    return false;
}

bool QtWindow::addTableRow(const String& name, const String& item,
	const NamedList* data, bool atStart)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) addTableRow(%s,%s,%p,%s) [%p]",
	m_id.c_str(),name.c_str(),item.c_str(),data,String::boolText(atStart),this);

    TableWidget tbl(this,name);
    if (!tbl.valid())
	return false;

    QtTable* custom = tbl.customTable();
    if (custom)
	return custom->addTableRow(item,data,atStart);

    int row = atStart ? 0 : tbl.rowCount();
    tbl.addRow(row);
    // Set item (the first column) and the rest of data
    tbl.setID(row,item);
    if (data)
	tbl.updateRow(row,*data);
    return true;
}

bool QtWindow::delTableRow(const String& name, const String& item)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::delTableRow(%s,%s) [%p]",
	name.c_str(),item.c_str(),this);
    TableWidget tbl(this,name);
    if (!tbl.valid())
	return false;
    QtTable* custom = tbl.customTable();
    if (custom)
	custom->delTableRow(item);
    else
	tbl.delRow(tbl.getRow(item));
    raiseSelectIfEmpty(tbl.rowCount(),this,name);
    return true;
}

bool QtWindow::setTableRow(const String& name, const String& item, const NamedList* data)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) setTableRow(%s,%s,%p) [%p]",
	m_id.c_str(),name.c_str(),item.c_str(),data,this);

    TableWidget tbl(this,name);
    if (!tbl.valid())
	return false;

    QtTable* custom = tbl.customTable();
    if (custom)
	return custom->setTableRow(item,data);

    int row = tbl.getRow(item);
    if (row < 0)
	return false;
    if (data)
	tbl.updateRow(row,*data);
    return true;
}

bool QtWindow::getTableRow(const String& name, const String& item, NamedList* data)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::getTableRow(%s,%s,%p) [%p]",
	name.c_str(),item.c_str(),data,this);

    TableWidget tbl(this,name);
    if (!tbl.valid())
	return false;

    QtTable* custom = tbl.customTable();
    if (custom)
	return custom->getTableRow(item,data);

    int row = tbl.getRow(item);
    if (row < 0)
	return false;
    if (!data)
	return true;
    int n = tbl.columnCount();
    for (int i = 0; i < n; i++) {
	String name;
	if (!tbl.getHeaderText(i,name))
	    continue;
	String value;
	if (tbl.getCell(row,i,value))
	    data->setParam(name,value);
    }
    return true;
}

bool QtWindow::clearTable(const String& name)
{
    DDebug(QtDriver::self(),DebugAll,"QtWindow::clearTable(%s) [%p]",name.c_str(),this);
    QtWidget w(this,name);
    if (w.invalid())
	return false;
    switch (w.type()) {
	case QtWidget::Table:
	    while (w.table()->rowCount())
		w.table()->removeRow(0);
	    return true;
	case QtWidget::TextEdit:
	    w.textEdit()->clear();
	    return true;
	case QtWidget::ListBox:
	    w.list()->clear();
	    return true;
	case QtWidget::CustomTable:
	    return w.customTable()->clearTable();
    }
    return false;
}

bool QtWindow::getText(const String& name, String& text)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) getText(%s) [%p]",
	m_id.c_str(),name.c_str(),this);
    QtWidget w(this,name);
    if (w.invalid())
	return false;
    switch (w.type()) {
	case QtWidget::ComboBox:
	    text = qtGetUtf8(w.combo()->currentText());
	    return true;
	case QtWidget::LineEdit:
	    text = qtGetUtf8(w.lineEdit()->text());
	    return true;
	case QtWidget::TextEdit:
	    text = qtGetUtf8(w.textEdit()->toPlainText());
	    return true;
	case QtWidget::Label:
	    text = qtGetUtf8(w.label()->text());
	    return true;
	case QtWidget::Action:
	    text = qtGetUtf8(w.action()->text());
	    return true;
	default:
	    if (w.inherits(QtWidget::AbstractButton)) {
		text = qtGetUtf8(w.abstractButton()->text());
		return true;
	    }
     }
     return false;
}

bool QtWindow::getCheck(const String& name, bool& checked)
{
    DDebug(QtDriver::self(),DebugAll,"QtWindow::getCheck(%s) [%p]",name.c_str(),this);

    QtWidget w(this,name);
    if (w.invalid())
	return false;
    if (w.inherits(QtWidget::AbstractButton))
	checked = w.abstractButton()->isChecked();
    else if (w.type() == QtWidget::Action)
	checked = w.action()->isChecked();
    else
	return false;
    return true;
}

bool QtWindow::getSelect(const String& name, String& item)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::getSelect(%s) [%p]",name.c_str(),this);
    QtWidget w(this,name);
    if (w.invalid())
	return false;
    switch (w.type()) {
	case QtWidget::ComboBox:
	    if (w.combo()->lineEdit() && w.combo()->lineEdit()->selectedText().isEmpty())
		return false;
	    item = qtGetUtf8(w.combo()->currentText());
	    return true;
	case QtWidget::Table:
	    {
		TableWidget t(w);
		int row = t.crtRow();
		return row >= 0 ? t.getCell(row,0,item) : false; 
	    }
	case QtWidget::ListBox:
	    {
		QListWidgetItem* crt = w.list()->currentItem();
		if (!crt)
		    return false;
		item = qtGetUtf8(crt->text());
	    }
	    return true;
	case QtWidget::Slider:
	    item = w.slider()->value();
	    return true;
	case QtWidget::ProgressBar:
	    item = w.progressBar()->value();
	    return true;
	case QtWidget::CustomTable:
	    return w.customTable()->getSelect(item);
    }
    return false;
}

// Set a property for this window or for a widget owned by it
bool QtWindow::setProperty(const String& name, const String& item, const String& value)
{
    if (name == m_id)
	return QtClient::property(true,wndWidget(),item,(String&)value);
    QtWidget w(this,name);
    if (w.invalid())
	return false;
    return QtClient::property(true,w,item,(String&)value);
}

// Get a property from this window or from a widget owned by it
bool QtWindow::getProperty(const String& name, const String& item, String& value)
{
    if (name == m_id)
	return QtClient::property(false,wndWidget(),item,value);
    QtWidget w(this,name);
    if (w.invalid())
	return false;
    return QtClient::property(false,w,item,value);
}

void QtWindow::closeEvent(QCloseEvent* event)
{
    hide();
    if (m_mainWindow && Client::self())
	Client::self()->quit();
    QWidget::closeEvent(event);
}

void QtWindow::action()
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) action() sender=%s [%p]",
	m_id.c_str(),qtGetUtf8(sender()->objectName()),this);
    if (!QtClient::self() || QtClient::changing())
	return;
    QtWidget w(sender());
    String name;
    if (translateName(w,name))
	QtClient::self()->action(this,name);
}

// Toggled actions
void QtWindow::toggled(bool on)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) toggled=%s sender=%s [%p]",
	m_id.c_str(),String::boolText(on),qtGetUtf8(sender()->objectName()),this);
    if (!QtClient::self() || QtClient::changing())
	return;
    QtWidget w(sender());
    String name;
    if (translateName(w,name))
	QtClient::self()->toggle(this,name,on);
}

void QtWindow::openUrl(const QString& link)
{
    QDesktopServices::openUrl(QUrl(link));
}

void QtWindow::doubleClick()
{
    if (QtClient::self() && sender())
	Client::self()->action(this,qtGetUtf8(sender()->objectName()));
}

// A widget's selection changed
void QtWindow::selectionChanged()
{
    if (!(QtClient::self() && sender()))
	return;
    String name = qtGetUtf8(sender()->objectName());
    String item;
    getSelect(name,item);
    Client::self()->select(this,name,item);
}

// Load a widget from file
QWidget* QtWindow::loadUI(const char* fileName, QWidget* parent,
	const char* uiName, const char* path)
{
    if (!(fileName && *fileName && parent))
	return 0;

    QUiLoader loader;
    if (!(path && *path))
	path = Client::s_skinPath.c_str();
    loader.setWorkingDirectory(QDir(path));
    QFile file(fileName);
    const char* err = 0;
    QWidget* w = 0;
    if (!file.exists())
	err = "file not found";
    else {
	w = loader.load(&file,parent);
	if (!w)
	    err = "loader failed";
    }
    file.close();
    if (!w)
	Debug(DebugWarn,"Failed to load widget '%s' from '%s': %s",
	    uiName,fileName,err);
    return w;
}

// Filter events to apply dynamic properties changes
bool QtWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (!obj)
	return false;
#if QT_VERSION >= 0x040200
    if (event->type() == QEvent::DynamicPropertyChange) {
	String name = qtGetUtf8(obj->objectName());
	QDynamicPropertyChangeEvent* ev = static_cast<QDynamicPropertyChangeEvent*>(event);
	String prop = ev->propertyName().constData();
	// Avoid QT's internal dynamic properties
	if (prop.startsWith(s_qtPropPrefix))
	    return QWidget::eventFilter(obj,event);
	// Return false for now on: it's our property
	QtWidget w(obj);
	if (w.invalid())
	    return false;
	QVariant var = obj->property(prop);
	if (var.isNull())
	    return false;
	bool ok = false;
	if (prop == s_propHHeader) {
	    // Show/hide the horizontal header
	    ok = ((w.type() == QtWidget::Table || w.type() == QtWidget::CustomTable) &&
		var.type() == QVariant::Bool &&	w.table()->horizontalHeader());
	    if (ok)
		w.table()->horizontalHeader()->setVisible(var.toBool());
	}
	if (ok)
	    DDebug(ClientDriver::self(),DebugAll,
		"Applied dynamic property='%s' for object='%s'",
		prop.c_str(),name.c_str());
	else
	    Debug(ClientDriver::self(),DebugNote,
		"Failed to apply dynamic property='%s' for object='%s'",
		prop.c_str(),name.c_str());
	return false;
    }
#endif
    return QWidget::eventFilter(obj,event);
}

// Handle key pressed events
void QtWindow::keyPressEvent(QKeyEvent* event)
{
    if (!Client::self())
	return QWidget::keyPressEvent(event);
    if (event->key() == Qt::Key_Backspace)
	Client::self()->backspace(m_id,this);
    return QWidget::keyPressEvent(event);
}

// Show hide window. Notify the client
void QtWindow::setVisible(bool visible)
{
    if (visible) {
	QWidget::move(m_x,m_y);
	resize(m_width,m_height);	
    }
    QWidget::setVisible(visible);
    // Notify the client on window visibility changes
    bool changed = (m_visible != visible);
    m_visible = visible;
    if (changed && Client::self())
	Client::self()->toggle(this,"window_visible_changed",m_visible);
}

// Show the window
void QtWindow::show()
{
    setVisible(true);
    if (!m_maximized)
	m_maximized = isMaximized();
    if (m_maximized) {
	m_maximized = false;
	setWindowState(Qt::WindowMaximized);
    }
}

// Hide the window
void QtWindow::hide()
{
    setVisible(false);
}

void QtWindow::size(int width, int height)
{
    DDebug(QtDriver::self(),DebugAll,"QtWindow::size(%d,%d) [%p]",width,height,this);
}

void QtWindow::move(int x, int y)
{
    DDebug(QtDriver::self(),DebugAll,"QtWindow::move(%d,%d) [%p]",x,y,this);
}

void QtWindow::moveRel(int dx, int dy)
{
    DDebug(QtDriver::self(),DebugAll,"QtWindow::moveRel(%d,%d) [%p]",dx,dy,this);
}

bool QtWindow::related(const Window* wnd) const
{
    DDebug(QtDriver::self(),DebugAll,"QtWindow::related(%p) [%p]",wnd,this);
    return false;
}

void QtWindow::menu(int x, int y)
{
    DDebug(QtDriver::self(),DebugAll,"QtWindow::menu(%d,%d) [%p]",x,y,this);
}

// Load UI file and setup the window
void QtWindow::doPopulate()
{
    Debug(QtDriver::self(),DebugAll,"Populating window '%s' [%p]",m_id.c_str(),this);
    QWidget* formWidget = loadUI(m_description,this,m_id);
    if (!formWidget)
	return;
    QSize frame = frameSize();
    setMinimumSize(formWidget->minimumSize().width(),formWidget->minimumSize().height());
    setMaximumSize(formWidget->maximumSize().width(),formWidget->maximumSize().height());
    resize(formWidget->width(),formWidget->height());
    setWidget(this,formWidget);
    m_widget = formWidget->objectName();
    title(qtGetUtf8(formWidget->windowTitle()));
    setWindowIcon(formWidget->windowIcon());
}

// Initialize window
void QtWindow::doInit()
{
    DDebug(QtDriver::self(),DebugAll,"Initializing window '%s' [%p]",
	m_id.c_str(),this);

    // Check if this is a frameless window
    String frameLess;
    getProperty(m_id,s_propFrameless,frameLess);
    if (frameLess.toBoolean())
	setWindowFlags(Qt::FramelessWindowHint);

    // Load window data
    m_mainWindow = s_cfg.getBoolValue(m_id,"mainwindow");
    m_saveOnClose = s_cfg.getBoolValue(m_id,"save",true);
    NamedList* sect = s_save.getSection(m_id);
    if (sect) {
	m_maximized = sect->getBoolValue("maximized");
	m_x = sect->getIntValue("x",pos().x());
	m_y = sect->getIntValue("y",pos().y());
	m_width = sect->getIntValue("width",width());
	m_height = sect->getIntValue("height",height());
	m_visible = sect->getBoolValue("visible");
    }
    else {
	Debug(QtDriver::self(),DebugNote,"Window(%s) not found in config [%p]",
	    m_id.c_str(),this);
	m_visible = s_cfg.getBoolValue(m_id,"visible");
    }
    m_visible = m_mainWindow || m_visible;

    // Create custom widgets
    QList<QFrame*> frm = qFindChildren<QFrame*>(this);
    for (int i = 0; i < frm.size(); i++) {
	String create = qtGetUtf8(frm[i]->accessibleName());
	if (!create.startSkip("customwidget;",false))
	    continue;
	ObjList* list = create.split(';',false);
	String type;
	String name;
	NamedList params("");
	int what = 0;
	for (ObjList* o = list->skipNull(); o; o = o->skipNext(), what++) {
	    GenObject* p = o->get();
	    if (what == 0)
		type = p->toString();
	    else if (what == 1)
		name = p->toString();
	    else {
		// Decode param
		int pos = p->toString().find('=');
		if (pos != -1)
		    params.addParam(p->toString().substr(0,pos),p->toString().substr(pos+1));
	    }
	}
	TelEngine::destruct(list);
	// Handle known types
	setWidget(frm[i],(QWidget*)UIFactory::build(type,name,&params));
    }

    // Connect actions' signal
    QList<QAction*> actions = qFindChildren<QAction*>(this);
    for (int i = 0; i < actions.size(); i++)
	if (actions[i]->isCheckable())
	    YQT_CONNECT(actions[i],toggled(bool),this,toggled(bool),"toggled()");
	else
	    YQT_CONNECT(actions[i],triggered(),this,action(),"triggered()");

    // Connect combo boxes signals
    QList<QComboBox*> combos = qFindChildren<QComboBox*>(this);
    for (int i = 0; i < combos.size(); i++)
	YQT_CONNECT(combos[i],activated(int),this,selectionChanged(),"activated(int)");

    // Connect abstract buttons (check boxes and radio/push/tool buttons) signals
    QList<QAbstractButton*> buttons = qFindChildren<QAbstractButton*>(this);
    for(int i = 0; i < buttons.size(); i++)
	if (buttons[i]->isCheckable())
	    YQT_CONNECT(buttons[i],toggled(bool),this,toggled(bool),"toggled(bool)");
	else
	    YQT_CONNECT(buttons[i],clicked(),this,action(),"clicked()");

    // Connect group boxes signals
    QList<QGroupBox*> grp = qFindChildren<QGroupBox*>(this);
    for(int i = 0; i < grp.size(); i++)
	if (grp[i]->isCheckable())
	    YQT_CONNECT(grp[i],toggled(bool),this,toggled(bool),"toggled(bool)");

    // Connect sliders signals
    QList<QSlider*> sliders = qFindChildren<QSlider*>(this);
    for (int i = 0; i < sliders.size(); i++)
	YQT_CONNECT(sliders[i],valueChanged(int),this,selectionChanged(),"valueChanged(int)");

    // Connect list boxes signals
    QList<QListWidget*> lists = qFindChildren<QListWidget*>(this);
    for (int i = 0; i < lists.size(); i++) {
	YQT_CONNECT(lists[i],itemDoubleClicked(QListWidgetItem*),this,doubleClick(),"itemDoubleClicked(QListWidgetItem*)");
	YQT_CONNECT(lists[i],itemActivated(QListWidgetItem*),this,doubleClick(),"itemDoubleClicked(QListWidgetItem*)");
	YQT_CONNECT(lists[i],currentRowChanged(int),this,selectionChanged(),"currentRowChanged(int)");
    }

    // Process tables:
    // Insert a column and connect signals
    // Hide columns starting with "hidden:"
    QList<QTableWidget*> tables = qFindChildren<QTableWidget*>(this);
    for (int i = 0; i < tables.size(); i++) {
	TableWidget t(tables[i]);
	// Insert the column containing the ID
	t.addColumn(0,0,"hidden:id");
	// Set column widths
	t.colWidths(false,m_id);
	// Hide columns
	for (int i = 0; i < t.columnCount(); i++) {
	    String name;
	    t.getHeaderText(i,name,false);
	    if (name.startsWith("hidden:"))
		t.table()->setColumnHidden(i,true);
	}
	// Connect signals
	YQT_CONNECT(t.table(),cellDoubleClicked(int,int),this,doubleClick(),"cellDoubleClicked(int,int)");
	YQT_CONNECT(t.table(),itemDoubleClicked(QTableWidgetItem*),this,doubleClick(),"itemDoubleClicked(QTableWidgetItem*)");
	YQT_CONNECT(t.table(),itemSelectionChanged(),this,selectionChanged(),"itemSelectionChanged()");
	// Optionally connect cell clicked
	// This is done when we want to generate a select() or action() from cell clicked
	String cellClicked;
	getProperty(t.name(),"dynamicCellClicked",cellClicked);
	if (cellClicked)
	    if (cellClicked == "selectionChanged")
		YQT_CONNECT(t.table(),cellClicked(int,int),this,selectionChanged(),"cellClicked(int,int)");
	    else if (cellClicked == "doubleClick")
		YQT_CONNECT(t.table(),cellClicked(int,int),this,doubleClick(),"cellClicked(int,int)");
    }

#if QT_VERSION >= 0x040200
    // Install event filer and apply dynamic properties
    QList<QObject*> w = qFindChildren<QObject*>(this);
    for (int i = 0; i < w.size(); i++) {
	// dynamicPropertyNames() was added in 4.2
	String name = qtGetUtf8(w[i]->objectName());
	QList<QByteArray> props = w[i]->dynamicPropertyNames();
	// Check for our dynamic properties
	int j = 0;
	for (j = 0; j < props.size(); j++)
	    if (!props[j].startsWith(s_qtPropPrefix))
		break;
	if (j == props.size())
	    continue;
	// Add event hook to be used when a dynamic property changes
	w[i]->installEventFilter(this);
	// Fake dynamic property change to apply them
	for (j = 0; j < props.size(); j++) {
	    if (props[j].startsWith(s_qtPropPrefix))
		continue;
	    QDynamicPropertyChangeEvent ev(props[j]);
	    eventFilter(w[i],&ev);
	}
    }
#endif

    qRegisterMetaType<QModelIndex>("QModelIndex");
    qRegisterMetaType<QTextCursor>("QTextCursor");

    // Force window visibility change notification by changing the visibility flag
    // Some controls might need to be updated
    m_visible = !m_visible;
    if (m_visible)
	hide();
    else
	show();
}

// Mouse button pressed notification
void QtWindow::mousePressEvent(QMouseEvent* event)
{
    if (Qt::LeftButton == event->button()) {
	m_movePos = event->globalPos();
	m_moving = true;
    }
}

// Mouse button release notification
void QtWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (Qt::LeftButton == event->button())
	m_moving = false;
}

// Move the window if the moving flag is set
void QtWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_moving)
	return;
    int cx = event->globalPos().x() - m_movePos.x();
    int cy = event->globalPos().y() - m_movePos.y();
    if (cx || cy) {
	m_movePos = event->globalPos();
	QWidget::move(x() + cx,y() + cy);
    }
}

// Update window position and size
void QtWindow::updatePosSize()
{
    QPoint point = pos();
    m_x = point.x();
    m_y = point.y();
    m_width = width();
    m_height = height();
}


/**
 * QtClient
 */
QtClient::QtClient()
    : Client("QtClient")
{
    m_oneThread = Engine::config().getBoolValue("client","onethread",true);

    s_save = Engine::configFile("qt4client",true);
    s_save.load();
}

QtClient::~QtClient()
{
}

void QtClient::cleanup()
{
    Client::cleanup();
    m_events.clear();
    Client::save(s_save);
    m_app->quit();
    delete m_app;
}

void QtClient::run()
{
    int argc = 0;
    char* argv =  0;
    m_app = new QApplication(argc,&argv);
    Debug(ClientDriver::self(),DebugInfo,"QT client start running (version=%s)",qVersion());
    if (!QSound::isAvailable())
	Debug(ClientDriver::self(),DebugWarn,"QT sounds are not available");
    // Create events proxy
    m_events.append(new QtEventProxy(QtEventProxy::Timer));
    m_events.append(new QtEventProxy(QtEventProxy::AllHidden,m_app));
    Client::run();
}

void QtClient::main()
{
    m_app->exec();
}

void QtClient::lock()
{}

void QtClient::unlock()
{}

void QtClient::allHidden()
{
    Debug(QtDriver::self(),DebugInfo,"QtClient::allHiden()");
    quit();
}

bool QtClient::createWindow(const String& name, const String& alias)
{
    String wName = alias ? alias : name;
    QtWindow* w = new QtWindow(wName,s_skinPath + s_cfg.getValue(name,"description"));
    if (w) {
	Debug(QtDriver::self(),DebugAll,"Created window %s (%p)",wName.c_str(),w);
	// Remove the old window
	ObjList* o = m_windows.find(wName);
	if (o)
	    Client::self()->closeWindow(wName,false);
	w->populate();
	m_windows.append(w);
	return true;
    }
    else
	Debug(QtDriver::self(),DebugGoOn,"Could not create window %s",wName.c_str());
    return false;
}

void QtClient::loadWindows(const char* file)
{
    if (!file)
	s_cfg = s_skinPath + "qt4client.rc";
    else
	s_cfg = String(file);
    s_cfg.load();
    Debug(QtDriver::self(),DebugInfo,"Loading Windows");
    unsigned int n = s_cfg.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* l = s_cfg.getSection(i);
	if (l && l->getBoolValue("enabled",true))
	    createWindow(*l);
    }
}

// Open a file open dialog window
// Parameters that can be specified include 'caption',
//  'dir', 'filter', 'selectedfilter', 'confirmoverwrite', 'choosedir'
// files List of selected file(s). Allow multiple file selection if non 0
// file The selected file if multiple file selection is disabled
bool QtClient::chooseFile(Window* parent, const NamedList& params,
    NamedList* files, String* file)
{
    if (!(files || file))
	return false;

    const char* caption = params.getValue("caption");
    const char* dir = params.getValue("dir");
    QString* filters = 0;
    NamedString* f = params.getParam("filters");
    if (f) {
	filters = new QString;
	ObjList* obj = f->split('|',false);
	for (ObjList* o = obj->skipNull(); o; o = o->skipNext()) {
	    if (!filters->isEmpty())
		filters->append(";;");
	    filters->append((static_cast<String*>(o->get()))->c_str());
	}
	TelEngine::destruct(obj);
    }
    NamedString* tmp = params.getParam("selectedfilter");
    QString* sFilter = tmp ? new QString(tmp->c_str()) : 0;
    QFileDialog::Options options;
    if (params.getBoolValue("choosedir"))
	options |= QFileDialog::ShowDirsOnly;
    if (params.getBoolValue("confirmoverwrite"))
	options |= QFileDialog::DontConfirmOverwrite;

    QWidget* p = static_cast<QtWindow*>(parent);
    if (files) {
	QStringList list = QFileDialog::getOpenFileNames((QWidget*)p,caption,
	    dir,filters?*filters:QString::null,sFilter,options);
	for (int i = 0; i < list.size(); i++)
	    files->addParam("file",qtGetUtf8(list[i]));
    }
    else {
	QString str = QFileDialog::getOpenFileName((QWidget*)p,caption,dir,
	    filters?*filters:QString::null,sFilter,options);
	*file = qtGetUtf8(str);
    }

    if (filters)
	delete filters;
    if (sFilter)
	delete sFilter;
    return true;
}

bool QtClient::action(Window* wnd, const String& name, NamedList* params)
{
    String tmp = name;
    if (tmp.startSkip("openurl:",false))
	return QDesktopServices::openUrl(QUrl(tmp.safe()));
    return Client::action(wnd,name,params);
}

// Create a sound object. Append it to the global list
bool QtClient::createSound(const char* name, const char* file, const char* device)
{
    if (!(name && *name && file && *file))
	return false;
    Lock lock(ClientSound::s_soundsMutex);
    if (ClientSound::s_sounds.find(name))
	return false;
    ClientSound::s_sounds.append(new QtSound(name,file,device));
    DDebug(ClientDriver::self(),DebugAll,"Added sound=%s file=%s device=%s",
	name,file,device);
    return true;
}

bool QtClient::formatDateTime(String& dest, unsigned int secs, const char* format)
{
    if (!(format && *format))
	return false;
    dest = qtGetUtf8(formatDateTime(secs,format));
    return true;
}

// Set/get an object's property
bool QtClient::property(bool set, QObject* obj, const char* name, String& value)
{
    if (!(obj && name && *name))
	return false;
    QVariant var = obj->property(name);
    const char* err = "unknown error";
    bool ok = true;
    switch (var.type()) {
	case QVariant::String:
	    if (set)
		ok = obj->setProperty(name,QVariant(qtSetUtf8(value)));
	    else
		value = qtGetUtf8(var.toString());
	    break;
	case QVariant::Bool:
	    if (set)
		ok = obj->setProperty(name,QVariant(value.toBoolean()));
	    else
		value = var.toBool();
	    break;
	case QVariant::Int:
	    if (set)
		ok = obj->setProperty(name,QVariant(value.toInteger()));
	    else
		value = var.toInt();
	    break;
	case QVariant::UInt:
	    if (set)
		ok = obj->setProperty(name,QVariant((unsigned int)value.toInteger()));
	    else
		value = var.toUInt();
	    break;
	case QVariant::Double:
	    if (set)
		ok = obj->setProperty(name,QVariant(value.toDouble()));
	    else {
		var.convert(QVariant::String);
		value = qtGetUtf8(var.toString());
	    }
	    break;
	case QVariant::Icon:
	    if (set)
		ok = obj->setProperty(name,QVariant(QIcon(value.c_str())));
	    break;
	case QVariant::Pixmap:
	    if (set)
		ok = obj->setProperty(name,QVariant(QPixmap(qtSetUtf8(value))));
	    break;
	case QVariant::Invalid:
	    ok = false;
	    err = "no such property";
	    break;
	default:
	    ok = false;
	    err = "unsupported type";
    }
    if (ok)
	DDebug(ClientDriver::self(),DebugAll,"%s %s=%s for object '%s'",
	    set?"Set":"Got",name,value.c_str(),qtGetUtf8(obj->objectName()));
    else
	DDebug(ClientDriver::self(),DebugNote,
	    "Failed to %s %s=%s (type=%s) for object '%s': %s",
	    set?"set":"get",name,value.c_str(),var.typeName(),
	    qtGetUtf8(obj->objectName()),err);
    return ok;
}


/**
 * QtDriver
 */
QtDriver::QtDriver()
    : m_init(false)
{
}

QtDriver::~QtDriver()
{
}

void QtDriver::initialize()
{
    Output("Initializing module Qt4 client");
    s_device = Engine::config().getValue("client","device",DEFAULT_DEVICE);
    if (!QtClient::self()) {
	debugCopy();
	new QtClient;
	QtClient::self()->startup();
    }
    if (!m_init) {
	m_init = true;
	setup();
    }
}

/**
 * QtEventProxy
 */
QtEventProxy::QtEventProxy(Type type, QApplication* app)
{
#define SET_NAME(n) { m_name = n; setObjectName(m_name.c_str()); }
    switch (type) {
	case Timer:
	    SET_NAME("qtClientTimerProxy");
	    {
		QTimer* timer = new QTimer(this);
		timer->setObjectName("qtClientIdleTimer");
		YQT_CONNECT(timer,timeout(),this,timerTick(),"timeout()");
		timer->start(1);
	    }
	    break;
	case AllHidden:
	    SET_NAME("qtClientAllHidden");
	    if (app)
		YQT_CONNECT(app,lastWindowClosed(),this,allHidden(),"lastWindowClosed()");
	    break;
	default:
	    return;
    }
#undef SET_NAME
}

void QtEventProxy::timerTick()
{
    if (Client::self())
	Client::self()->idleActions();
}

void QtEventProxy::allHidden()
{
    if (Client::self())
	Client::self()->allHidden();
}

/**
 * QtSound
 */
bool QtSound::doStart()
{
    doStop();
    if (Client::self())
	Client::self()->createObject((void**)&m_sound,"QSound",m_file);
    if (m_sound)
	DDebug(ClientDriver::self(),DebugAll,"Sound(%s) started file=%s",
	    c_str(),m_file.c_str());
    else
	Debug(ClientDriver::self(),DebugNote,"Sound(%s) failed to start file=%s",
	    c_str(),m_file.c_str());
    m_sound->setLoops(m_repeat);
    m_sound->play();
    return true;
}

void QtSound::doStop()
{
    if (!m_sound)
	return;
    m_sound->stop();
    delete m_sound;
    DDebug(ClientDriver::self(),DebugAll,"Sound(%s) stopped",c_str());
    m_sound = 0;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
