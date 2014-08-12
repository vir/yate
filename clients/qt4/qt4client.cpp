/**
 * qt4client.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * A Qt-4 based universal telephony client
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

#include "qt4client.h"
#include <QtUiTools>

#ifdef _WINDOWS
#define DEFAULT_DEVICE "dsound/*"
#define PLATFORM_LOWERCASE_NAME "windows"
#elif defined(__APPLE__)
#define DEFAULT_DEVICE "coreaudio/*"
#define PLATFORM_LOWERCASE_NAME "apple"
#elif defined(__linux__)
#define DEFAULT_DEVICE "alsa/default"
#define PLATFORM_LOWERCASE_NAME "linux"
#else
#define DEFAULT_DEVICE "oss//dev/dsp"
#define PLATFORM_LOWERCASE_NAME "unknown"
#endif

namespace TelEngine {

static unsigned int s_allHiddenQuit = 0; // Quit on all hidden notification if this counter is 0

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
	SpinBox        = 13,
	Calendar       = 14,
	Splitter       = 15,
	TextBrowser    = 16,
	Unknown,                         // Unknown type
	Action,                          // QAction descendant
	CustomTable,                     // QtTable descendant
	CustomTree,                      // QtTree descendant
	CustomWidget,                    // QtCustomWidget descendant
	CustomObject,                    // QtCustomObject descendant
	Missing                          // Invalid pointer
    };
    // Set widget from object
    inline QtWidget(QObject* w)
	: m_widget(0), m_action(0), m_object(0), m_type(Missing) {
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
	: m_widget(w), m_action(0), m_object(0), m_type(t) {
	    if (!m_widget)
		m_type = Missing;
	}
    // Set widget/action from object and name
    inline QtWidget(QObject* parent, const String& name)
	: m_widget(0), m_action(0), m_object(0), m_type(Missing) {
	    QString what = QtClient::setUtf8(name);
	    m_widget = qFindChild<QWidget*>(parent,what);
	    if (!m_widget) {
		m_action = qFindChild<QAction*>(parent,what);
		if (!m_action)
		    m_object = qFindChild<QObject*>(parent,what);
	    }
	    m_type = getType();
	}
    inline bool valid() const
	{ return type() != Missing; }
    inline bool invalid() const
	{ return type() == Missing; }
    inline int type() const
	{ return m_type; }
    inline operator QWidget*()
	{ return m_widget; }
    inline bool inherits(const char* classname)
	{ return m_widget && m_widget->inherits(classname); }
    inline bool inherits(Type t)
	{ return inherits(s_types[t]); }
    inline QWidget* widget()
	{ return m_widget; }
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
    inline QSpinBox* spinBox()
	{ return static_cast<QSpinBox*>(m_widget); }
    inline QCalendarWidget* calendar()
	{ return static_cast<QCalendarWidget*>(m_widget); }
    inline QSplitter* splitter()
	{ return static_cast<QSplitter*>(m_widget); }
    inline QtTable* customTable()
	{ return qobject_cast<QtTable*>(m_widget); }
    inline QtTree* customTree()
	{ return qobject_cast<QtTree*>(m_widget); }
    inline QtCustomWidget* customWidget()
	{ return qobject_cast<QtCustomWidget*>(m_widget); }
    inline QtCustomObject* customObject()
	{ return qobject_cast<QtCustomObject*>(m_object); }
    inline UIWidget* uiWidget() {
	    switch (type()) {
		case CustomTable:
		    return static_cast<UIWidget*>(customTable());
		case CustomWidget:
		    return static_cast<UIWidget*>(customWidget());
		case CustomObject:
		    return static_cast<UIWidget*>(customObject());
		case CustomTree:
		    return static_cast<UIWidget*>(customTree());
	    }
	    return 0;
	}

    inline QAction* action()
	{ return m_action; }

    // Find a combo box item
    inline int findComboItem(const String& item) {
	    QComboBox* c = combo();
	    return c ? c->findText(QtClient::setUtf8(item)) : -1;
	}
    // Add an item to a combo box
    inline bool addComboItem(const String& item, bool atStart) {
	    QComboBox* c = combo();
	    if (!c)
		return false;
	    QString it(QtClient::setUtf8(item));
	    if (atStart)
		c->insertItem(0,it);
	    else
		c->addItem(it);
	    return true;
	}
    // Find a list box item
    inline int findListItem(const String& item) {
	    QListWidget* l = list();
	    if (!l)
		return -1;
	    QString it(QtClient::setUtf8(item));
	    for (int i = l->count(); i >= 0 ; i--) {
		QListWidgetItem* tmp = l->item(i);
		if (tmp && it == tmp->text())
		    return i;
	    }
	    return -1;
	}
    // Add an item to a list box
    inline bool addListItem(const String& item, bool atStart) {
	    QListWidget* l = list();
	    if (!l)
		return false;
	    QString it(QtClient::setUtf8(item));
	    if (atStart)
		l->insertItem(0,it);
	    else
		l->addItem(it);
	    return true;
	}

    int getType() {
	    if (m_widget) {
		String cls = m_widget->metaObject()->className();
		for (int i = 0; i < Unknown; i++)
		    if (s_types[i] == cls)
			return i;
		if (customTable())
		    return CustomTable;
		if (customWidget())
		    return CustomWidget;
		if (customTree())
		    return CustomTree;
		return Unknown;
	    }
	    if (m_action && m_action->inherits("QAction"))
		return Action;
	    if (customObject())
		return CustomObject;
	    return Missing;
	}
    static String s_types[Unknown];
protected:
    QWidget* m_widget;
    QAction* m_action;
    QObject* m_object;
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
    TableWidget(QWidget* wid, const String& name, bool tmp = true);
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
		    new QTableWidgetItem(QtClient::setUtf8(text)));
	}
    inline bool getHeaderText(int col, String& dest, bool lower = true) {
    	    QTableWidgetItem* item = m_table->horizontalHeaderItem(col);
	    if (item) {
		QtClient::getUtf8(dest,item->text());
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
		item->setIcon(QIcon(QtClient::setUtf8(image)));
	}
    inline void addCell(int row, int col, const String& value) {
	    QTableWidgetItem* item = new QTableWidgetItem(QtClient::setUtf8(value));
	    m_table->setItem(row,col,item);
	}
    inline void setCell(int row, int col, const String& value, bool addNew = true) {
	    QTableWidgetItem* item = m_table->item(row,col);
	    if (item)
		item->setText(QtClient::setUtf8(value));
	    else if (addNew)
		addCell(row,col,value);
	}
    inline bool getCell(int row, int col, String& dest, bool lower = false) {
    	    QTableWidgetItem* item = m_table->item(row,col);
	    if (item) {
		QtClient::getUtf8(dest,item->text());
		if (lower)
		    dest.toLower();
		return true;
	    }
	    return false;
	}
    inline void setID(int row, const String& value)
	{ setCell(row,0,value); }
    // Add or set a row
    void updateRow(const String& item, const NamedList* data, bool atStart);
    // Update a row from a list of parameters
    void updateRow(int row, const NamedList& data);
    // Find a row by the first's column value. Return -1 if not found
    int getRow(const String& item);
    // Find a column by its label. Return -1 if not found
    int getColumn(const String& name, bool caseInsentive = true);
protected:
    void init(bool tmp);
private:
    QTableWidget* m_table;               // The table
    String m_name;                       // Table's name
    int m_sortControl;                   // Flag used to set/reset sorting attribute of the table
};

// Store an UI loaded from file to avoid loading it again
class UIBuffer : public String
{
public:
    inline UIBuffer(const String& name, QByteArray* buf)
	: String(name), m_buffer(buf)
	{ s_uiCache.append(this); }
    inline QByteArray* buffer()
	{ return m_buffer; }
    // Remove from list. Release memory
    virtual void destruct();
    // Return an already loaded UI. Load from file if not found.
    // Add URLs paths when missing
    static UIBuffer* build(const String& name);
    // Find a buffer
    static UIBuffer* find(const String& name);
    // Buffer cache
    static ObjList s_uiCache;
private:
    QByteArray* m_buffer;                // The buffer
};

}; // namespace TelEngine

using namespace TelEngine;

// Dynamic properies
static const String s_propsSave = "_yate_save_props"; // Save properties property name
static const String s_propColWidths = "_yate_col_widths"; // Column widths
static const String s_propSorting = "_yate_sorting";  // Table/List sorting
static const String s_propSizes = "_yate_sizes";      // Size int array
static const String s_propShowWndWhenActive = "_yate_showwnd_onactive"; // Show another window when a window become active
static String s_propHHeader = "dynamicHHeader";       // Tables: show/hide the horizontal header
static String s_propAction = "dynamicAction";         // Prefix for properties that would trigger some action
static String s_propWindowFlags = "_yate_windowflags"; // Window flags
static const String s_propContextMenu = "_yate_context_menu"; // Context menu name
static String s_propHideInactive = "dynamicHideOnInactive"; // Hide inactive window
static const String s_yatePropPrefix = "_yate_";      // Yate dynamic properties prefix
static NamedList s_qtStyles("");                      // Qt styles classname -> internal name
//
static Qt4ClientFactory s_qt4Factory;
static Configuration s_cfg;
static Configuration s_save;
ObjList UIBuffer::s_uiCache;

// Values used to configure window title bar and border
static TokenDict s_windowFlags[] = {
    // Window type
    {"popup",              Qt::Popup},
    {"tool",               Qt::Tool},
    {"subwindow",          Qt::SubWindow},
#ifdef _WINDOWS
    {"notificationtype",   Qt::Tool},
#else
    {"notificationtype",   Qt::SubWindow},
#endif
    // Window flags
    {"title",              Qt::WindowTitleHint},
    {"sysmenu",            Qt::WindowSystemMenuHint},
    {"maximize",           Qt::WindowMaximizeButtonHint},
    {"minimize",           Qt::WindowMinimizeButtonHint},
    {"help",               Qt::WindowContextHelpButtonHint},
    {"stayontop",          Qt::WindowStaysOnTopHint},
    {"frameless",          Qt::FramelessWindowHint},
#if QT_VERSION >= 0x040500
    {"close",              Qt::WindowCloseButtonHint},
#endif
    {0,0}
};

// Widget attribute names
static const TokenDict s_widgetAttributes[] = {
    {"macshowfocusrect",   Qt::WA_MacShowFocusRect},
    {0,0}
};

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
    "QProgressBar",
    "QSpinBox",
    "QCalendarWidget",
    "QSplitter",
    "QTextBrowser",
};

// QVariant type translation dictionary
static const TokenDict s_qVarType[] = {
    {"string",       QVariant::String},
    {"bool",         QVariant::Bool},
    {"int",          QVariant::Int},
    {"uint",         QVariant::UInt},
    {"stringlist",   QVariant::StringList},
    {"icon",         QVariant::Icon},
    {"pixmap",       QVariant::Pixmap},
    {"double",       QVariant::Double},
    {"keysequence",  QVariant::KeySequence},
    {0,0}
};

// Qt alignment flags translation
static const TokenDict s_qAlign[] = {
    {"left",      Qt::AlignLeft},
    {"right",     Qt::AlignRight},
    {"hcenter",   Qt::AlignHCenter},
    {"justify",   Qt::AlignJustify},
    {"top",       Qt::AlignTop},
    {"bottom",    Qt::AlignBottom},
    {"vcenter",   Qt::AlignVCenter},
    {"center",    Qt::AlignCenter},
    {"absolute",  Qt::AlignAbsolute},
    {0,0}
};

// Qt alignment flags translation
static const TokenDict s_qEditTriggers[] = {
    {"currentchanged", QAbstractItemView::CurrentChanged},
    {"doubleclick",    QAbstractItemView::DoubleClicked},
    {"selclick",       QAbstractItemView::SelectedClicked},
    {"editkeypress",   QAbstractItemView::EditKeyPressed},
    {"anykeypress",    QAbstractItemView::AnyKeyPressed},
    {"all",            QAbstractItemView::AllEditTriggers},
    {0,0}
};

// QtClientSort name
static const TokenDict s_sorting[] = {
    {"ascending",      QtClient::SortAsc},
    {"descending",     QtClient::SortDesc},
    {"none",           QtClient::SortNone},
    {0,0}
};

// Handler for QT library messages
static void qtMsgHandler(QtMsgType type, const char* text)
{
    int dbg = DebugAll;
    switch (type) {
	case QtDebugMsg:
	    dbg = DebugInfo;
	    break;
	case QtWarningMsg:
	    dbg = DebugWarn;
	    break;
	case QtCriticalMsg:
	    dbg = DebugGoOn;
	    break;
	case QtFatalMsg:
	    dbg = DebugFail;
	    break;
    }
    Debug("QT",dbg,"%s",text);
}

// Build a list of parameters from a string
// Return the number of parameters found
static unsigned int str2Params(NamedList& params, const String& buf, char sep = '|')
{
    ObjList* list = 0;
    // Check if we have another separator
    if (buf.startsWith("separator=")) {
	sep = buf.at(10);
	list = buf.substr(11).split(sep,false);
    }
    else
	list = buf.split(sep,false);
    unsigned int n = 0;
    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	int pos = s->find('=');
	if (pos < 1)
	    continue;
	params.addParam(s->substr(0,pos),s->substr(pos + 1));
	n++;
    }
    TelEngine::destruct(list);
    return n;
}

// Utility: fix QT path separator on Windows
// (display paths using only one separator to the user)
static inline QString fixPathSep(QString str)
{
#ifdef _WINDOWS
    QString tmp = str;
    tmp.replace(QChar('/'),QtClient::setUtf8(Engine::pathSeparator()));
    return tmp;
#else
    return str;
#endif
}

// Utility: find a stacked widget's page with the given name
static int findStackedWidget(QStackedWidget& w, const String& name)
{
    QString n(QtClient::setUtf8(name));
    for (int i = 0; i < w.count(); i++) {
	QWidget* page = w.widget(i);
	if (page && n == page->objectName())
	    return i;
    }
    return -1;
}

// Utility function used to get the name of a control
// The name of the control indicates actions, toggles ...
// The action name alias can contain parameters
static bool translateName(QtWidget& w, String& name, NamedList** params = 0)
{
    if (w.invalid())
	return false;
    if (w.type() != QtWidget::Action)
	QtClient::getIdentity(w.widget(),name);
    else
	QtClient::getIdentity(w.action(),name);
    if (!name)
	return true;
    // Check params
    int pos = name.find('|');
    if (pos < 1)
	return true;
    if (params) {
	*params = new NamedList("");
	if (!str2Params(**params,name.substr(pos + 1)))
	    TelEngine::destruct(*params);
    }
    name = name.substr(0,pos);
    return true;
}

// Utility: raise a select event if a list is empty
static inline void raiseSelectIfEmpty(int count, Window* wnd, const String& name)
{
    if (!Client::exiting() && count <= 0 && Client::self())
	Client::self()->select(wnd,name,String::empty());
}

// Add dynamic properties from a list of parameters
// Parameter format:
// property_name:property_type=property_value
static void addDynamicProps(QObject* obj, NamedList& props)
{
    static String typeString = "string";
    static String typeBool = "bool";
    static String typeInt = "int";

    if (!obj)
	return;
    unsigned int n = props.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = props.getParam(i);
	if (!(ns && ns->name()))
	    continue;
	int pos = ns->name().find(':');
	if (pos < 1)
	    continue;

	String prop = ns->name().substr(0,pos);
	String type = ns->name().substr(pos + 1);
	QVariant var;
	if (type == typeString)
	    var.setValue(QString(ns->c_str()));
	else if (type == typeBool)
	    var.setValue(ns->toBoolean());
	else if (type == typeInt)
	    var.setValue(ns->toInteger());

	if (var.type() != QVariant::Invalid) {
	    obj->setProperty(prop,var);
	    DDebug(ClientDriver::self(),DebugAll,
		"Object '%s': added dynamic property %s='%s' type=%s",
		YQT_OBJECT_NAME(obj),prop.c_str(),ns->c_str(),var.typeName());
	}
	else
	    Debug(ClientDriver::self(),DebugStub,
		"Object '%s': dynamic property '%s' type '%s' is not supported",
		YQT_OBJECT_NAME(obj),prop.c_str(),type.c_str());
    }
}

// Find a QSystemTrayIcon child of an object
static inline QSystemTrayIcon* findSysTrayIcon(QObject* obj, const char* name)
{
    return qFindChild<QSystemTrayIcon*>(obj,QtClient::setUtf8(name));
}

// Utility used to create an object's property if not found
// Add it to a list of strings
// Return true if the list changed
static bool createProperty(QObject* obj, const char* name, QVariant::Type t,
    QtWindow* wnd, QStringList* list)
{
    if (!obj || TelEngine::null(name))
	return false;
    QVariant var = obj->property(name);
    if (var.type() == QVariant::Invalid)
	obj->setProperty(name,QVariant(t));
    else if (var.type() != t) {
	if (wnd)
	    Debug(QtDriver::self(),DebugNote,
		"Window(%s) child '%s' already has a %s property '%s' [%p]",
		wnd->toString().c_str(),YQT_OBJECT_NAME(obj),var.typeName(),name,wnd);
	return false;
    }
    if (!list)
	return false;
    QString s = QtClient::setUtf8(name);
    if (list->contains(s))
	return false;
    *list << s;
    return true;
}

// Replace file path in URLs in a character array
static void addFilePathUrl(QByteArray& a, const String& file)
{
    if (!file)
	return;
    QString path = QDir::fromNativeSeparators(QtClient::setUtf8(file));
    // Truncate after last path separator (lastIndexOf() returns -1 if not found)
    path.truncate(path.lastIndexOf(QString("/")) + 1);
    if (!path.size())
	return;
    int start = 0;
    int end = -1;
    while ((start = a.indexOf("url(",end + 1)) > 0) {
	start += 4;
	end = a.indexOf(")",start);
	if (end <= start)
	    break;
	// Add
	int len = end - start;
	QByteArray tmp = a.mid(start,len);
	if (tmp.indexOf('/') != -1)
	    continue;
	tmp.insert(0,path);
	a.replace(start,len,tmp);
    }
}

// Read data from file and append it to a string buffer
// Optionally append suffix characters to file name
static bool appendStyleSheet(QString& buf, const char* file,
    const char* suffix1 = 0, const char* suffix2 = 0)
{
    if (TelEngine::null(file))
	return false;
    String shf = file;
    const char* oper = 0;
    int pos = shf.rfind('/');
    if (pos < 0)
	pos = shf.rfind('\\');
    if (pos < 0)
	shf = Client::s_skinPath + shf;
    int level = DebugNote;
    if (!(TelEngine::null(suffix1) && TelEngine::null(suffix2))) {
	level = DebugAll;
	int dotPos = shf.rfind('.');
	if (dotPos > pos) {
	    String tmp = shf.substr(0,dotPos);
	    tmp.append(suffix1,"_");
	    tmp.append(suffix2,"_");
	    shf = tmp + shf.substr(dotPos);
	}
    }
    DDebug(ClientDriver::self(),DebugAll,"Loading stylesheet file '%s'",shf.c_str());
    QFile f(QtClient::setUtf8(shf));
    if (f.open(QIODevice::ReadOnly)) {
	QByteArray a = f.readAll();
	if (a.size()) {
	    addFilePathUrl(a,shf);
	    buf += QString::fromUtf8(a.constData());
	}
	else if (f.error() != QFile::NoError)
	    oper = "read";
    }
    else
	oper = "open";
    if (!oper)
	return true;
    Debug(ClientDriver::self(),level,"Failed to %s stylesheet file '%s': %d '%s'",
	oper,shf.c_str(),f.error(),f.errorString().toUtf8().constData());
    return false;
}

// Split an integer string list
// Result list length can be set by indicating a length
static QList<int> buildIntList(const String& buf, int len = 0)
{
    QList<int> ret;
    ObjList* list = buf.split(',');
    int pos = 0;
    ObjList* o = list;
    while (o || pos < len) {
	int val = 0;
	if (o) {
	    if (o->get())
		val = o->get()->toString().toInteger();
	    o = o->next();
	}
	ret.append(val);
	pos++;
	if (pos == len)
	    break;
    }
    TelEngine::destruct(list);
    return ret;
}

// Retrieve an object's property
// Check platform dependent value
static inline bool getPropPlatform(QObject* obj, const String& name, String& val)
{
    if (!(obj && name))
	return false;
    if (QtClient::getProperty(obj,name,val))
	return true;
    return QtClient::getProperty(obj,name + "_os" + PLATFORM_LOWERCASE_NAME,val);
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
    if (type == YSTRING("QSound"))
	return new QSound(QtClient::setUtf8(name));
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

TableWidget::TableWidget(QWidget* wid, const String& name, bool tmp)
    : m_table(0), m_sortControl(-1)
{
    if (wid)
	m_table = qFindChild<QTableWidget*>(wid,QtClient::setUtf8(name));
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
    m_table->repaint();
}

// Add or set a row
void TableWidget::updateRow(const String& item, const NamedList* data, bool atStart)
{
    int row = getRow(item);
    // Add a new one ?
    if (row < 0) {
	row = atStart ? 0 : rowCount();
	addRow(row);
	setID(row,item);
    }
    // Update
    if (data)
	updateRow(row,*data);
}

// Update a row from a list of parameters
void TableWidget::updateRow(int row, const NamedList& data)
{
    int ncol = columnCount();
    for (int i = 0; i < ncol; i++) {
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
    // Init vertical header
    String* rowText = data.getParam(YSTRING("row_text"));
    String* rowImg = data.getParam(YSTRING("row_image"));
    if (rowText || rowImg) {
	QTableWidgetItem* item = m_table->verticalHeaderItem(row);
	if (!item) {
	    item = new QTableWidgetItem;
	    m_table->setVerticalHeaderItem(row,item);
	}
	if (rowText)
	    item->setText(QtClient::setUtf8(*rowText));
	if (rowImg)
	    item->setIcon(QIcon(QtClient::setUtf8(*rowImg)));
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
	if ((caseInsensitive && (name &= val)) || (!caseInsensitive && name == val))
	    return i;
    }
    return -1;
}

void TableWidget::init(bool tmp)
{
    QtClient::getUtf8(m_name,m_table->objectName());
    if (tmp) {
	m_sortControl = m_table->isSortingEnabled() ? 1 : 0;
	if (m_sortControl)
	    m_table->setSortingEnabled(false);
    }
}

/**
 * UIBuffer
 */
// Remove from list. Release memory
void UIBuffer::destruct()
{
    s_uiCache.remove(this,false);
    if (m_buffer) {
	delete m_buffer;
	m_buffer = 0;
    }
    String::destruct();
}

// Return an already loaded UI. Load from file if not found.
// Add URLs paths when missing
UIBuffer* UIBuffer::build(const String& name)
{
    // Check if already loaded from the same location
    UIBuffer* buf = find(name);
    if (buf)
	return buf;

    // Load
    QFile file(QtClient::setUtf8(name));
    file.open(QIODevice::ReadOnly);
    QByteArray* qArray = new QByteArray;
    *qArray = file.readAll();
    file.close();
    if (!qArray->size()) {
	delete qArray;
	return 0;
    }
    // Add URLs path when missing
    addFilePathUrl(*qArray,name);
    return new UIBuffer(name,qArray);
}

// Find a buffer
UIBuffer* UIBuffer::find(const String& name)
{
    ObjList* o = s_uiCache.find(name);
    return o ? static_cast<UIBuffer*>(o->get()) : 0;
}


/**
 * QtWindow
 */
QtWindow::QtWindow()
    : m_x(0), m_y(0), m_width(0), m_height(0),
    m_maximized(false), m_mainWindow(false), m_moving(0)
{
}

QtWindow::QtWindow(const char* name, const char* description, const char* alias, QtWindow* parent)
    : QWidget(parent, Qt::Window),
    Window(alias ? alias : name), m_description(description), m_oldId(name),
    m_x(0), m_y(0), m_width(0), m_height(0),
    m_maximized(false), m_mainWindow(false), m_moving(0)
{
    setObjectName(QtClient::setUtf8(m_id));
}

QtWindow::~QtWindow()
{
    // Update all hidden counter for tray icons owned by this window
    QList<QSystemTrayIcon*> trayIcons = qFindChildren<QSystemTrayIcon*>(this);
    if (trayIcons.size() > 0) {
	if (s_allHiddenQuit >= (unsigned int)trayIcons.size())
	    s_allHiddenQuit -= trayIcons.size();
	else {
	    Debug(QtDriver::self(),DebugFail,
		"QtWindow(%s) destroyed with all hidden counter %u greater then tray icons %d [%p]",
		m_id.c_str(),s_allHiddenQuit,trayIcons.size(),this);
	    s_allHiddenQuit = 0;
	}
    }

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
	// Set dynamic properties to be saved for native QT objects
	QList<QTableWidget*> tables = qFindChildren<QTableWidget*>(this);
	for (int i = 0; i < tables.size(); i++) {
	    if (qobject_cast<QtTable*>(tables[i]))
		continue;
	    // Column widths
	    unsigned int n = tables[i]->columnCount();
	    String widths;
	    for (unsigned int j = 0; j < n; j++)
		widths.append(String(tables[i]->columnWidth(j)),",",true);
	    tables[i]->setProperty(s_propColWidths,QVariant(QtClient::setUtf8(widths)));
	    // Sorting
	    String sorting;
	    if (tables[i]->isSortingEnabled()) {
		QHeaderView* h = tables[i]->horizontalHeader();
		int col = h ? h->sortIndicatorSection() : -1;
		if (col >= 0)
		    sorting << col << "," <<
			String::boolText(Qt::AscendingOrder == h->sortIndicatorOrder());
	    }
	    tables[i]->setProperty(s_propSorting,QVariant(QtClient::setUtf8(sorting)));
	}
	QList<QSplitter*> spl = qFindChildren<QSplitter*>(this);
	for (int i = 0; i < spl.size(); i++) {
	    String sizes;
	    QtClient::intList2str(sizes,spl[i]->sizes());
	    QtClient::setProperty(spl[i],s_propSizes,sizes);
	}
	// Save child objects properties
	QList<QObject*> child = qFindChildren<QObject*>(this);
	for (int i = 0; i < child.size(); i++) {
	    NamedList props("");
	    if (!QtClient::getProperty(child[i],s_propsSave,props))
		continue;
	    unsigned int n = props.length();
	    for (unsigned int j = 0; j < n; j++) {
		NamedString* ns = props.getParam(j);
		if (ns && ns->name())
		    QtClient::saveProperty(child[i],ns->name(),this);
	    }
	}
    }
}

// Set windows title
void QtWindow::title(const String& text)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::title(%s) [%p]",text.c_str(),this);
    Window::title(text);
    QWidget::setWindowTitle(QtClient::setUtf8(text));
}

void QtWindow::context(const String& text)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::context(%s) [%p]",text.c_str(),this);
    m_context = text;
}

bool QtWindow::setParams(const NamedList& params)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::setParams() [%p]",this);

    setUpdatesEnabled(false);
    // Check for custom widget params
    if (params == YSTRING("customwidget")) {
	// Each parameter is a list of parameters for a custom widget
	// Parameter name is the widget's name
	unsigned int n = params.length();
	bool ok = true;
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* ns = params.getParam(i);
	    NamedList* nl = static_cast<NamedList*>(ns ? ns->getObject(YATOM("NamedList")) : 0);
	    if (!(nl && ns->name()))
		continue;
	    // Find the widget and set its params
	    QtWidget w(this,ns->name());
	    if (w.type() == QtWidget::CustomTable)
		ok = w.customTable()->setParams(*nl) && ok;
	    else if (w.type() == QtWidget::CustomWidget)
		ok = w.customWidget()->setParams(*nl) && ok;
	    else if (w.type() == QtWidget::CustomObject)
		ok = w.customObject()->setParams(*nl) && ok;
	    else
		ok = false;
	}
	setUpdatesEnabled(true);
	return ok;
    }
    // Check for system tray icon params
    if (params == YSTRING("systemtrayicon")) {
	// Each parameter is a list of parameters for a system tray icon
	// Parameter name is the widget's name
	// Parameter value indicates delete/create/set an existing one
	unsigned int n = params.length();
	bool ok = false;
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* ns = params.getParam(i);
	    if (!(ns && ns->name()))
		continue;
	    QSystemTrayIcon* trayIcon = findSysTrayIcon(this,ns->name());
	    // Delete
	    if (ns->null()) {
		if (trayIcon) {
		    // Reactivate program termination when the last window was hidden
		    if (s_allHiddenQuit)
			s_allHiddenQuit--;
		    else
			Debug(QtDriver::self(),DebugFail,
			    "QtWindow(%s) all hidden counter is 0 while deleting '%s' tray icon [%p]",
			    m_id.c_str(),YQT_OBJECT_NAME(trayIcon),this);
		    QtClient::deleteLater(trayIcon);
		}
		continue;
	    }
	    NamedList* nl = YOBJECT(NamedList,ns);
	    if (!nl)
		continue;
	    // Create a new one if needed
	    if (!trayIcon) {
		if (!ns->toBoolean())
		    continue;
		trayIcon = new QSystemTrayIcon(this);
		trayIcon->setObjectName(QtClient::setUtf8(ns->name()));
		QtClient::connectObjects(trayIcon,SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
		    this,SLOT(sysTrayIconAction(QSystemTrayIcon::ActivationReason)));
		// Deactivate program termination when the last window was hidden
		s_allHiddenQuit++;
	    }
	    ok = true;
	    // Add dynamic properties
	    // TODO: track the properties, clear the old ones if needed
	    addDynamicProps(trayIcon,*nl);
	    // Set icon and tooltip
	    NamedString* tmp = nl->getParam(YSTRING("icon"));
	    if (tmp && *tmp)
		trayIcon->setIcon(QIcon(QtClient::setUtf8(*tmp)));
	    tmp = nl->getParam(YSTRING("tooltip"));
	    if (tmp && *tmp)
		trayIcon->setToolTip(QtClient::setUtf8(*tmp));
	    // Check context menu
	    NamedString* menu = nl->getParam(YSTRING("menu"));
	    if (menu) {
		QMenu* oldMenu = trayIcon->contextMenu();
		if (oldMenu)
		    delete oldMenu;
		NamedList* nlMenu = YOBJECT(NamedList,menu);
		trayIcon->setContextMenu(nlMenu ? QtClient::buildMenu(*nlMenu,*menu,this,
		    SLOT(action()),SLOT(toggled(bool)),this) : 0);
	    }
	    if (nl->getBoolValue(YSTRING("show"),true))
		trayIcon->setVisible(true);
	}
	setUpdatesEnabled(true);
	return ok;
    }
    // Parameters for the widget whose name is the list name
    if(params) {
	QtWidget w(this, params);
	// Handle UIWidget descendants
	UIWidget* uiw = w.uiWidget();
	if (uiw) {
	    bool ok = uiw->setParams(params);
	    setUpdatesEnabled(true);
	    return ok;
	}
	if (w.type() == QtWidget::Calendar) {
	    int year = params.getIntValue(YSTRING("year"));
	    int month = params.getIntValue(YSTRING("month"));
	    int day = params.getIntValue(YSTRING("day"));
	    w.calendar()->setCurrentPage(year, month);
	    w.calendar()->setSelectedDate(QDate(year, month, day));
	    setUpdatesEnabled(true);
	    return true;
	}
    }

    // Window or other parameters
    if (params.getBoolValue(YSTRING("modal"))) {
	if (parentWindow())
	    setWindowModality(Qt::WindowModal);
	else
	    setWindowModality(Qt::ApplicationModal);
    }
    if (params.getBoolValue(YSTRING("minimized")))
	QWidget::setWindowState(Qt::WindowMinimized);
    bool ok = Window::setParams(params);
    setUpdatesEnabled(true);
    return ok;
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
    bool ok = (name == m_id);
    if (ok) {
	if (QWidget::isMinimized())
	    QWidget::showNormal();
	QWidget::activateWindow();
	QWidget::raise();
    }
    QtWidget w(this,name);
    if (w.invalid())
	return ok;
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
    // Check system tray icons
    QSystemTrayIcon* trayIcon = findSysTrayIcon(this,name);
    if (trayIcon) {
	trayIcon->setVisible(visible);
	return true;
    }
    // Widgets
    QtWidget w(this,name);
    if (w.invalid())
	return false;
    setUpdatesEnabled(false);
    if (w.type() != QtWidget::Action)
	w->setVisible(visible);
    else
	w.action()->setVisible(visible);
    setUpdatesEnabled(true);
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
    UIWidget* uiw = w.uiWidget();
    if (uiw)
	return uiw->setText(text,richText);
    switch (w.type()) {
	case QtWidget::CheckBox:
	    w.check()->setText(QtClient::setUtf8(text));
	    return true;
	case QtWidget::LineEdit:
	    w.lineEdit()->setText(QtClient::setUtf8(text));
	    return true;
	case QtWidget::TextBrowser:
	case QtWidget::TextEdit:
	    if (richText) {
		w.textEdit()->clear();
		w.textEdit()->insertHtml(QtClient::setUtf8(text));
	    }
	    else
		w.textEdit()->setText(QtClient::setUtf8(text));
	    {
		QScrollBar* bar = w.textEdit()->verticalScrollBar();
		if (bar)
		    bar->setSliderPosition(bar->maximum());
	    }
	    return true;
	case QtWidget::Label:
	    w.label()->setText(QtClient::setUtf8(text));
	    return true;
	case QtWidget::ComboBox:
	    if (w.combo()->lineEdit())
		w.combo()->lineEdit()->setText(QtClient::setUtf8(text));
	    else
		setSelect(name,text);
	    return true;
	case QtWidget::Action:
	    w.action()->setText(QtClient::setUtf8(text));
	    return true;
	case QtWidget::SpinBox:
	    w.spinBox()->setValue(text.toInteger());
	    return true;
    }

    // Handle some known base classes having a setText() method
    if (w.inherits(QtWidget::AbstractButton))
	w.abstractButton()->setText(QtClient::setUtf8(text));
    else
	return false;
    return true;
}

bool QtWindow::setCheck(const String& name, bool checked)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::setCheck(%s,%s) [%p]",
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
    if (w.invalid())
	return false;
    UIWidget* uiw = w.uiWidget();
    if (uiw)
	return uiw->setSelect(item);

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
	    if (item) {
		d = w.findComboItem(item);
		if (d < 0)
		    return false;
	        w.combo()->setCurrentIndex(d);
	    }
	    else if (w.combo()->lineEdit())
		w.combo()->lineEdit()->setText("");
	    else
		return false;
	    return true;
	case QtWidget::ListBox:
	    d = w.findListItem(item);
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
		String def = YQT_OBJECT_NAME(w.stackWidget());
		def << "_default";
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
	case QtWidget::Tab:
	    d = w.tab()->count() - 1;
	    for (QString tmp = QtClient::setUtf8(item); d >= 0; d--) {
		QWidget* wid = w.tab()->widget(d);
		if (wid && wid->objectName() == tmp)
		    break;
	    }
	    if (d >= 0 && d < w.tab()->count()) {
		w.tab()->setCurrentIndex(d);
		return true;
	    }
	    return false;

    }
    return false;
}

bool QtWindow::setUrgent(const String& name, bool urgent)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::setUrgent(%s,%s) [%p]",
	name.c_str(),String::boolText(urgent),this);

    if (name == m_id) {
	QApplication::alert(this,0);
	return true;
    }

    QtWidget w(this,name);
    if (w.invalid())
	return false;
    w->raise();
    return true;
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
	    return -1 != w.findComboItem(item);
	case QtWidget::Table:
	    return getTableRow(name,item);
	case QtWidget::ListBox:
	    return -1 != w.findListItem(item);
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
	    w.addComboItem(item,atStart);
	    if (atStart && w.combo()->lineEdit())
		w.combo()->lineEdit()->setText(w.combo()->itemText(0));
	    return true;
	case QtWidget::Table:
	    return addTableRow(name,item,0,atStart);
	case QtWidget::ListBox:
	    return w.addListItem(item,atStart);
    }
    return false;
}

bool QtWindow::delOption(const String& name, const String& item)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) delOption(%s,%s) [%p]",
	m_id.c_str(),name.c_str(),item.c_str(),this);
    return delTableRow(name,item);
}

bool QtWindow::getOptions(const String& name, NamedList* items)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) getOptions(%s,%p) [%p]",
	m_id.c_str(),name.c_str(),items,this);

    QtWidget w(this,name);
    if (w.invalid())
	return false;
    if (!items)
	return true;
    UIWidget* uiw = w.uiWidget();
    if (uiw)
	return uiw->getOptions(*items);

    switch (w.type()) {
	case QtWidget::ComboBox:
	    for (int i = 0; i < w.combo()->count(); i++)
		QtClient::getUtf8(*items,"",w.combo()->itemText(i),false);
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
		    QtClient::getUtf8(*items,"",tmp->text(),false);
	    }
	    break;
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
    UIWidget* uiw = w.uiWidget();
    if (uiw)
	return uiw->addLines(*lines,max,atStart);
    unsigned int count = lines->length();
    if (!count)
	return true;

    switch (w.type()) {
	case QtWidget::TextBrowser:
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
		QString s = w.textEdit()->toPlainText();
		int pos = atStart ? 0 : s.length();
		for (unsigned int i = 0; i < count; i++) {
		    NamedString* ns = lines->getParam(i);
		    if (!ns)
			continue;
		    if (ns->name().endsWith("\n"))
			s.insert(pos,QtClient::setUtf8(ns->name()));
		    else {
			String tmp = ns->name() + "\n";
			s.insert(pos,QtClient::setUtf8(tmp));
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
	    if (atStart) {
		for (; count; count--) {
		    NamedString* ns = lines->getParam(count - 1);
		    if (ns)
			w.combo()->insertItem(0,QtClient::setUtf8(ns->name()));
		}
		if (w.combo()->lineEdit())
		    w.combo()->lineEdit()->setText(w.combo()->itemText(0));
	    }
	    else {
		for (unsigned int i = 0; i < count; i++) {
		    NamedString* ns = lines->getParam(i);
		    if (ns)
			w.combo()->addItem(QtClient::setUtf8(ns->name()));
		}
	    }
	    return true;
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

    QtWidget w(this,name);
    if (w.invalid())
	return false;
    UIWidget* uiw = w.uiWidget();
    if (uiw)
	return uiw->addTableRow(item,data,atStart);
    // Handle basic QTableWidget
    if (w.type() != QtWidget::Table)
	return false;
    TableWidget tbl(w.table());
    int row = atStart ? 0 : tbl.rowCount();
    tbl.addRow(row);
    // Set item (the first column) and the rest of data
    tbl.setID(row,item);
    if (data)
	tbl.updateRow(row,*data);
    return true;
}

// Insert or update multiple rows in a single operation
bool QtWindow::setMultipleRows(const String& name, const NamedList& data, const String& prefix)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) setMultipleRows('%s',%p,'%s') [%p]",
	m_id.c_str(),name.c_str(),&data,prefix.c_str(),this);

    QtWidget w(this,name);
    if (w.invalid())
	return false;
    UIWidget* uiw = w.uiWidget();
    return uiw && uiw->setMultipleRows(data,prefix);
}

// Insert a row into a table owned by this window
bool QtWindow::insertTableRow(const String& name, const String& item,
    const String& before, const NamedList* data)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) insertTableRow(%s,%s,%s,%p) [%p]",
	m_id.c_str(),name.c_str(),item.c_str(),before.c_str(),data,this);

    QtWidget w(this,name);
    if (w.invalid())
	return false;
    UIWidget* uiw = w.uiWidget();
    if (uiw)
	return uiw->insertTableRow(item,before,data);
    if (w.type() != QtWidget::Table)
	return false;
    TableWidget tbl(w.table());
    int row = tbl.getRow(before);
    if (row == -1)
	row = tbl.rowCount();
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
    QtWidget w(this,name);
    if (w.invalid())
	return false;
    int row = -1;
    int n = 0;
    switch (w.type()) {
	case QtWidget::Table:
	case QtWidget::CustomTable:
	    {
		TableWidget tbl(w.table());
		QtTable* custom = tbl.customTable();
		if (custom) {
		    if (custom->delTableRow(item))
			row = 0;
		}
		else {
		    row = tbl.getRow(item);
		    if (row >= 0)
			tbl.delRow(row);
		}
		n = tbl.rowCount();
	    }
	    break;
	case QtWidget::ComboBox:
	    row = w.findComboItem(item);
	    if (row >= 0) {
		w.combo()->removeItem(row);
		n = w.combo()->count();
	    }
	    break;
	case QtWidget::ListBox:
	    row = w.findListItem(item);
	    if (row >= 0) {
		QStringListModel* model = (QStringListModel*)w.list()->model();
		if (!(model && model->removeRow(row)))
		    row = -1;
		n = w.list()->count();
	    }
	    break;
	default:
	    UIWidget* uiw = w.uiWidget();
	    if (uiw && uiw->delTableRow(item)) {
		row = 0;
		// Don't notify empty: we don't know it
		n = 1;
	    }
    }
    if (row < 0)
	return false;
    if (!n)
	raiseSelectIfEmpty(0,this,name);
    return true;
}

bool QtWindow::setTableRow(const String& name, const String& item, const NamedList* data)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) setTableRow(%s,%s,%p) [%p]",
	m_id.c_str(),name.c_str(),item.c_str(),data,this);

    QtWidget w(this,name);
    if (w.invalid())
	return false;
    UIWidget* uiw = w.uiWidget();
    if (uiw)
	return uiw->setTableRow(item,data);
    if (w.type() != QtWidget::Table)
	return false;
    TableWidget tbl(w.table());
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

    QtWidget w(this,name);
    if (w.invalid())
	return false;
    UIWidget* uiw = w.uiWidget();
    if (uiw)
	return uiw->getTableRow(item,data);
    if (w.type() != QtWidget::Table)
	return false;
    TableWidget tbl(w.table(),false);
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

// Set a table row or add a new one if not found
bool QtWindow::updateTableRow(const String& name, const String& item,
    const NamedList* data, bool atStart)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) updateTableRow('%s','%s',%p,%s) [%p]",
	m_id.c_str(),name.c_str(),item.c_str(),data,String::boolText(atStart),this);
    QtWidget w(this,name);
    if (w.invalid())
	return false;
    switch (w.type()) {
	case QtWidget::Table:
	case QtWidget::CustomTable:
	    {
		TableWidget tbl(w.table());
		QtTable* custom = tbl.customTable();
		if (custom) {
		    if (custom->getTableRow(item))
			return custom->setTableRow(item,data);
		    return custom->addTableRow(item,data,atStart);
		}
		tbl.updateRow(item,data,atStart);
		return true;
	    }
	case QtWidget::CustomTree:
	    {
		QtTree* custom = w.customTree();
		if (custom) {
		    if (custom->getTableRow(item))
			return custom->setTableRow(item,data);
		    return custom->addTableRow(item,data,atStart);
		}
		return false;
	    }
	case QtWidget::ComboBox:
	    return w.findComboItem(item) >= 0 || w.addComboItem(item,atStart);
	case QtWidget::ListBox:
	    return w.findListItem(item) >= 0 || w.addListItem(item,atStart);
    }
    return false;
}

// Add or set one or more table row(s). Screen update is locked while changing the table.
// Each data list element is a NamedPointer carrying a NamedList with item parameters.
// The name of an element is the item to update.
// Element's value not empty: update the item
// Else: delete it
bool QtWindow::updateTableRows(const String& name, const NamedList* data, bool atStart)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) updateTableRows('%s',%p,%s) [%p]",
	m_id.c_str(),name.c_str(),data,String::boolText(atStart),this);

    QtWidget w(this,name);
    if (w.invalid())
	return false;
    UIWidget* uiw = w.uiWidget();
    if (uiw) {
	bool ok = uiw->updateTableRows(data,atStart);
	QtTable* ct = w.customTable();
	if (ct)
	    raiseSelectIfEmpty(ct->rowCount(),this,name);
	return ok;
    }
    if (w.type() != QtWidget::Table)
	return false;
    if (!data)
	return true;
    TableWidget tbl(w.table());
    bool ok = true;
    tbl.table()->setUpdatesEnabled(false);
    ObjList add;
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
	    int row = tbl.getRow(ns->name());
	    if (row >= 0)
		tbl.delRow(row);
	    else
		ok = false;
	    continue;
	}
	// Set existing row or postpone add
	int row = tbl.getRow(ns->name());
	if (row >= 0) {
	    const NamedList* params = YOBJECT(NamedList,ns);
	    if (params)
		tbl.updateRow(row,*params);
	}
	else if (ns->toBoolean())
	    add.append(ns)->setDelete(false);
	else
	    ok = false;
    }
    n = add.count();
    if (n) {
	int row = tbl.rowCount();
	if (row < 0)
	    row = 0;
	// Append if not requested to insert at start or table is empty
	if (!(atStart && row))
	    tbl.table()->setRowCount(row + n);
	else {
	    for (unsigned int i = 0; i < n; i++)
		tbl.table()->insertRow(0);
	}
	for (ObjList* o = add.skipNull(); o; row++, o = o->skipNext()) {
	    NamedString* ns = static_cast<NamedString*>(o->get());
	    tbl.setID(row,ns->name());
	    const NamedList* params = YOBJECT(NamedList,ns);
	    if (params)
		tbl.updateRow(row,*params);
	}
    }
    tbl.table()->setUpdatesEnabled(true);
    raiseSelectIfEmpty(tbl.rowCount(),this,name);
    return ok;
}

bool QtWindow::clearTable(const String& name)
{
    DDebug(QtDriver::self(),DebugAll,"QtWindow::clearTable(%s) [%p]",name.c_str(),this);
    QtWidget w(this,name);
    if (w.invalid())
	return false;
    UIWidget* uiw = w.uiWidget();
    if (uiw)
	return uiw->clearTable();
    bool ok = true;
    if (w.widget())
	w->setUpdatesEnabled(false);
    switch (w.type()) {
	case QtWidget::Table:
	    w.table()->setRowCount(0);
	    break;
	case QtWidget::TextBrowser:
	case QtWidget::TextEdit:
	    w.textEdit()->clear();
	    break;
	case QtWidget::ListBox:
	    w.list()->clear();
	    break;
	case QtWidget::ComboBox:
	    w.combo()->clear();
	    break;
	default:
	    ok = false;
    }
    if (w.widget())
	w->setUpdatesEnabled(true);
    return ok;
}

// Show or hide control busy state
bool QtWindow::setBusy(const String& name, bool on)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) setBusy(%s,%u) [%p]",
	m_id.c_str(),name.c_str(),on,this);
    if (name == m_id)
	return QtBusyWidget::showBusyChild(this,on);
    QtWidget w(this,name);
    if (w.invalid())
	return false;
    UIWidget* uiw = w.uiWidget();
    if (uiw)
	return uiw->setBusy(on);
    if (w.widget())
	return QtBusyWidget::showBusyChild(w.widget(),on);
    return false;
}

bool QtWindow::getText(const String& name, String& text, bool richText)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) getText(%s) [%p]",
	m_id.c_str(),name.c_str(),this);
    QtWidget w(this,name);
    if (w.invalid())
	return false;
    UIWidget* uiw = w.uiWidget();
    if (uiw)
	return uiw->getText(text,richText);
    switch (w.type()) {
	case QtWidget::ComboBox:
	    QtClient::getUtf8(text,w.combo()->currentText());
	    return true;
	case QtWidget::LineEdit:
	    QtClient::getUtf8(text,w.lineEdit()->text());
	    return true;
	case QtWidget::TextBrowser:
	case QtWidget::TextEdit:
	    if (!richText)
		QtClient::getUtf8(text,w.textEdit()->toPlainText());
	    else
		QtClient::getUtf8(text,w.textEdit()->toHtml());
	    return true;
	case QtWidget::Label:
	    QtClient::getUtf8(text,w.label()->text());
	    return true;
	case QtWidget::Action:
	    QtClient::getUtf8(text,w.action()->text());
	    return true;
	case QtWidget::SpinBox:
	    text = w.spinBox()->value();
	    return true;
	default:
	    if (w.inherits(QtWidget::AbstractButton)) {
		QtClient::getUtf8(text,w.abstractButton()->text());
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
    UIWidget* uiw = w.uiWidget();
    if (uiw)
	return uiw->getSelect(item);
    switch (w.type()) {
	case QtWidget::ComboBox:
	    if (w.combo()->lineEdit() && w.combo()->lineEdit()->selectedText().isEmpty())
		return false;
	    QtClient::getUtf8(item,w.combo()->currentText());
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
		QtClient::getUtf8(item,crt->text());
	    }
	    return true;
	case QtWidget::Slider:
	    item = w.slider()->value();
	    return true;
	case QtWidget::ProgressBar:
	    item = w.progressBar()->value();
	    return true;
	case QtWidget::Tab:
	    {
		item = "";
		QWidget* wid = w.tab()->currentWidget();
		if (wid)
		    QtClient::getUtf8(item,wid->objectName());
	    }
	    return true;
	case QtWidget::StackWidget:
	    {
		item = "";
		QWidget* wid = w.stackWidget()->currentWidget();
		if (wid)
		    QtClient::getUtf8(item,wid->objectName());
	    }
	    return true;
    }
    return false;
}

// Retrieve an element's multiple selection
bool QtWindow::getSelect(const String& name, NamedList& items)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::getSelect(%p) [%p]",&items,this);
    QtWidget w(this,name);
    if (w.invalid())
	return false;
    UIWidget* uiw = w.uiWidget();
    if (uiw)
	return uiw->getSelect(items);
    switch (w.type()) {
	case QtWidget::ComboBox:
	case QtWidget::Table:
	case QtWidget::ListBox:
	case QtWidget::Slider:
	case QtWidget::ProgressBar:
	case QtWidget::Tab:
	case QtWidget::StackWidget:
	    DDebug(QtDriver::self(),DebugStub,"QtWindow::getSelect(%p) not implemented for '%s; [%p]",
		&items,w.widget()->metaObject()->className(),this);
    }
    return false;
}

// Build a menu from a list of parameters
bool QtWindow::buildMenu(const NamedList& params)
{
    QWidget* parent = this;
    // Retrieve the owner
    const String& owner = params[YSTRING("owner")];
    if (owner && owner != m_id) {
	parent = qFindChild<QWidget*>(this,QtClient::setUtf8(owner));
	if (!parent) {
	    DDebug(QtDriver::self(),DebugNote,
		"QtWindow(%s) buildMenu(%s) owner '%s' not found [%p]",
		m_id.c_str(),params.c_str(),owner.c_str(),this);
	    return false;
	}
    }
    QWidget* target = parent;
    const String& t = params[YSTRING("target")];
    if (t) {
	target = qFindChild<QWidget*>(this,QtClient::setUtf8(t));
	if (!target) {
	    DDebug(QtDriver::self(),DebugNote,
		"QtWindow(%s) buildMenu(%s) target '%s' not found [%p]",
		m_id.c_str(),params.c_str(),t.c_str(),this);
	    return false;
	}
    }
    // Remove existing menu
    removeMenu(params);
    QMenu* menu = QtClient::buildMenu(params,params.getValue(YSTRING("title"),params),this,
	SLOT(action()),SLOT(toggled(bool)),parent);
    if (!menu) {
	DDebug(QtDriver::self(),DebugNote,
	    "QtWindow(%s) failed to build menu '%s' target='%s' [%p]",
	    m_id.c_str(),params.c_str(),YQT_OBJECT_NAME(target),this);
	return false;
    }
    DDebug(QtDriver::self(),DebugAll,"QtWindow(%s) built menu '%s' target='%s' [%p]",
	m_id.c_str(),params.c_str(),YQT_OBJECT_NAME(target),this);
    QMenuBar* mbOwner = qobject_cast<QMenuBar*>(target);
    QMenu* mOwner = !mbOwner ? qobject_cast<QMenu*>(target) : 0;
    if (mbOwner || mOwner) {
	QAction* before = 0;
	const String& bef = params[YSTRING("before")];
	// Retrieve the action to insert before
	if (bef) {
	    QString cmp = QtClient::setUtf8(bef);
	    QList<QAction*> list = target->actions();
	    for (int i = 0; !before && i < list.size(); i++) {
		// Check action name or menu name if the action is associated with a menu
		if (list[i]->objectName() == cmp)
		    before = list[i];
		else if (list[i]->menu() && list[i]->menu()->objectName() == cmp)
		    before = list[i]->menu()->menuAction();
		if (before && i && list[i - 1]->isSeparator() &&
		    params.getBoolValue(YSTRING("before_separator"),true))
		    before = list[i - 1];
	    }
	}
	// Insert the menu
	if (mbOwner)
	    mbOwner->insertMenu(before,menu);
	else
	    mOwner->insertMenu(before,menu);
    }
    else {
	QToolButton* tb = qobject_cast<QToolButton*>(target);
	if (tb)
	    tb->setMenu(menu);
	else {
	    QPushButton* pb = qobject_cast<QPushButton*>(target);
	    if (pb)
		pb->setMenu(menu);
	    else if (!QtClient::setProperty(target,s_propContextMenu,params))
		target->addAction(menu->menuAction());
	}
    }
    return true;
}

// Remove a menu
bool QtWindow::removeMenu(const NamedList& params)
{
    QWidget* parent = this;
    // Retrieve the owner
    const String& owner = params[YSTRING("owner")];
    if (owner && owner != m_id) {
	parent = qFindChild<QWidget*>(this,QtClient::setUtf8(owner));
	if (!parent)
	    return false;
    }
    QMenu* menu = qFindChild<QMenu*>(parent,QtClient::setUtf8(params));
    if (!menu)
	return false;
    QtClient::deleteLater(menu);
    return true;
}

// Set an element's image
bool QtWindow::setImage(const String& name, const String& image, bool fit)
{
    if (!name)
	return false;
    if (name == m_id)
	return QtClient::setImage(this,image);
    QObject* obj = qFindChild<QObject*>(this,QtClient::setUtf8(name));
    return obj && QtClient::setImage(obj,image,fit);
}

// Set a property for this window or for a widget owned by it
bool QtWindow::setProperty(const String& name, const String& item, const String& value)
{
    if (name == m_id)
	return QtClient::setProperty(wndWidget(),item,value);
    QObject* obj = qFindChild<QObject*>(this,QtClient::setUtf8(name));
    return obj ? QtClient::setProperty(obj,item,value) : false;
}

// Get a property from this window or from a widget owned by it
bool QtWindow::getProperty(const String& name, const String& item, String& value)
{
    if (name == m_id)
	return QtClient::getProperty(wndWidget(),item,value);
    QObject* obj = qFindChild<QObject*>(this,QtClient::setUtf8(name));
    return obj ? QtClient::getProperty(obj,item,value) : false;
}

void QtWindow::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    // Don't update pos if not shown normal
    if (!isShownNormal())
	return;
    m_x = pos().x();
    m_y = pos().y();
    DDebug(QtDriver::self(),DebugAll,"QtWindow(%s) moved x=%d y=%d [%p]",
	m_id.c_str(),m_x,m_y,this);
}

void QtWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // Don't update size if not shown normal
    if (!isShownNormal())
	return;
    m_width = width();
    m_height = height();
    DDebug(QtDriver::self(),DebugAll,"QtWindow(%s) resized width=%d height=%d [%p]",
	m_id.c_str(),m_width,m_height,this);
}

bool QtWindow::event(QEvent* ev)
{
    static const String s_activeChg("window_active_changed");
    if (ev->type() == QEvent::WindowDeactivate) {
	String hideProp;
	QtClient::getProperty(wndWidget(),s_propHideInactive,hideProp);
	if (hideProp && hideProp.toBoolean())
	    setVisible(false);
	m_active = false;
	Client::self()->toggle(this,s_activeChg,false);
    }
    else if (ev->type() == QEvent::WindowActivate) {
	m_active = true;
	Client::self()->toggle(this,s_activeChg,true);
	String wName;
	if (getPropPlatform(wndWidget(),s_propShowWndWhenActive,wName) && wName)
	    Client::setVisible(wName);
    }
    else if (ev->type() == QEvent::ApplicationDeactivate) {
	if (m_active) {
	    m_active = false;
	    Client::self()->toggle(this,s_activeChg,true);
	}
    }
    return QWidget::event(ev);
}

void QtWindow::closeEvent(QCloseEvent* event)
{
    // NOTE: Don't access window's data after calling hide():
    //  some logics might destroy the window when hidden

    // Notify window closed
    String tmp;
    if (Client::self() &&
	QtClient::getProperty(wndWidget(),"_yate_windowclosedaction",tmp))
	Client::self()->action(this,tmp);

    // Hide the window when requested
    if (QtClient::getBoolProperty(wndWidget(),"_yate_hideonclose")) {
	event->ignore();
	hide();
	return;
    }

    QWidget::closeEvent(event);
    if (m_mainWindow && Client::self()) {
	Client::self()->quit();
	return;
    }
    if (QtClient::getBoolProperty(wndWidget(),"_yate_destroyonclose")) {
	XDebug(QtDriver::self(),DebugAll,
	    "Window(%s) closeEvent() set delete later [%p]",m_id.c_str(),this);
	QObject::deleteLater();
	// Safe to call hide(): the window will be deleted when control returns
	//  to the main loop
    }
    hide();
}

void QtWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange)
	m_maximized = isMaximized();
    QWidget::changeEvent(event);
}

void QtWindow::action()
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) action() sender=%s [%p]",
	m_id.c_str(),YQT_OBJECT_NAME(sender()),this);
    if (!QtClient::self() || QtClient::changing())
	return;
    String name;
    NamedList* params = 0;
    if (!QtClient::getBoolProperty(sender(),"_yate_translateidentity"))
	QtClient::getIdentity(sender(),name);
    else {
	QtWidget w(sender());
	translateName(w,name,&params);
    }
    if (name)
	QtClient::self()->action(this,name,params);
    TelEngine::destruct(params);
}

// Toggled actions
void QtWindow::toggled(bool on)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) toggled=%s sender=%s [%p]",
	m_id.c_str(),String::boolText(on),YQT_OBJECT_NAME(sender()),this);
    QtClient::updateToggleImage(sender());
    if (!QtClient::self() || QtClient::changing())
	return;
    QtWidget w(sender());
    String name;
    if (translateName(w,name))
	QtClient::self()->toggle(this,name,on);
}

// System tray actions
void QtWindow::sysTrayIconAction(QSystemTrayIcon::ActivationReason reason)
{
    String action;
    switch (reason) {
	case QSystemTrayIcon::Context:
	    QtClient::getProperty(sender(),s_propAction + "Context",action);
	    break;
	case QSystemTrayIcon::DoubleClick:
	    QtClient::getProperty(sender(),s_propAction + "DoubleClick",action);
	    break;
	case QSystemTrayIcon::Trigger:
	    QtClient::getProperty(sender(),s_propAction + "Trigger",action);
	    break;
	case QSystemTrayIcon::MiddleClick:
	    QtClient::getProperty(sender(),s_propAction + "MiddleClick",action);
	    break;
	default:
	    return;
    }
    if (action)
	Client::self()->action(this,action);
}

// Choose file window was accepted
void QtWindow::chooseFileAccepted()
{
    QFileDialog* dlg = qobject_cast<QFileDialog*>(sender());
    if (!dlg)
	return;
    String action;
    QtClient::getUtf8(action,dlg->objectName());
    if (!action)
	return;
    NamedList params("");
    QDir dir = dlg->directory();
    if (dir.absolutePath().length())
	QtClient::getUtf8(params,"dir",fixPathSep(dir.absolutePath()));
    QStringList files = dlg->selectedFiles();
    for (int i = 0; i < files.size(); i++)
	QtClient::getUtf8(params,"file",fixPathSep(files[i]));
    if (dlg->fileMode() != QFileDialog::DirectoryOnly &&
	dlg->fileMode() != QFileDialog::Directory) {
	QString filter = dlg->selectedFilter();
	if (filter.length())
	    QtClient::getUtf8(params,"filter",filter);
    }
    Client::self()->action(this,action,&params);
}

// Choose file window was cancelled
void QtWindow::chooseFileRejected()
{
    QFileDialog* dlg = qobject_cast<QFileDialog*>(sender());
    if (!dlg)
	return;
    String action;
    QtClient::getUtf8(action,dlg->objectName());
    if (!action)
	return;
    Client::self()->action(this,action,0);
}

void QtWindow::openUrl(const QString& link)
{
    QDesktopServices::openUrl(QUrl(link));
}

void QtWindow::doubleClick()
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) doubleClick() sender=%s [%p]",
	m_id.c_str(),YQT_OBJECT_NAME(sender()),this);
    if (QtClient::self() && sender())
	Client::self()->action(this,YQT_OBJECT_NAME(sender()));
}

// A widget's selection changed
void QtWindow::selectionChanged()
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) selectionChanged() sender=%s [%p]",
	m_id.c_str(),YQT_OBJECT_NAME(sender()),this);
    if (!(QtClient::self() && sender()))
	return;
    String name = YQT_OBJECT_NAME(sender());
    QtWidget w(sender());
    if (w.type() != QtWidget::Calendar) {
	String item;
	getSelect(name,item);
	Client::self()->select(this,name,item);
    }
    else {
	NamedList p("");
	QDate d = w.calendar()->selectedDate();
	p.addParam("year",String(d.year()));
	p.addParam("month",String(d.month()));
	p.addParam("day",String(d.day()));
	Client::self()->action(this,name,&p);
    }
}

// Connect an object's text changed signal to window's slot
bool QtWindow::connectTextChanged(QObject* obj)
{
    if (!(obj && QtClient::getBoolProperty(obj,"_yate_textchangednotify")))
	return false;
    QComboBox* combo = qobject_cast<QComboBox*>(obj);
    if (combo)
	return QtClient::connectObjects(combo,SIGNAL(editTextChanged(const QString&)),
	    this,SLOT(textChanged(const QString&)));
    QLineEdit* lineEdit = qobject_cast<QLineEdit*>(obj);
    if (lineEdit)
	return QtClient::connectObjects(lineEdit,SIGNAL(textChanged(const QString&)),
	    this,SLOT(textChanged(const QString&)));
    QTextEdit* textEdit = qobject_cast<QTextEdit*>(obj);
    if (textEdit)
	return QtClient::connectObjects(textEdit,SIGNAL(textChanged()),
	    this,SLOT(textChanged()));
    const QMetaObject* meta = obj->metaObject();
    Debug(DebugStub,"connectTextChanged() not implemented for class '%s'",
	meta ? meta->className() :  "");
    return false;
}

// Notify text changed to the client
void QtWindow::notifyTextChanged(QObject* obj, const QString& text)
{
    if (!(obj && QtClient::getBoolProperty(obj,"_yate_textchangednotify")))
	return;
    // Detect QtUIWidget item. Get its container identity if found
    String item;
    QtUIWidget::getListItemProp(obj,item);
    QtUIWidget* uiw = item ? QtUIWidget::container(obj) : 0;
    String name;
    if (!uiw)
	QtClient::getIdentity(obj,name);
    else
	uiw->getIdentity(obj,name);
    if (!name)
	return;
    NamedList p("");
    p.addParam("sender",name);
    if (text.size())
	QtClient::getUtf8(p,"text",text);
    Client::self()->action(this,YSTRING("textchanged"),&p);
}

// Load a widget from file
QWidget* QtWindow::loadUI(const char* fileName, QWidget* parent,
	const char* uiName, const char* path)
{
    if (Client::exiting())
	return 0;
    if (!(fileName && *fileName && parent))
	return 0;

    if (!(path && *path))
	path = Client::s_skinPath.c_str();
    UIBuffer* buf = UIBuffer::build(fileName);
    const char* err = 0;
    if (buf && buf->buffer()) {
	QBuffer b(buf->buffer());
	QUiLoader loader;
        loader.setWorkingDirectory(QDir(QtClient::setUtf8(path)));
	QWidget* w = loader.load(&b,parent);
	if (w)
	    return w;
	err = "loader failed";
    }
    else
	err = buf ? "file is empty" : "file not found";
    // Error
    TelEngine::destruct(buf);
    Debug(DebugWarn,"Failed to load widget '%s' file='%s' path='%s': %s",
        uiName,fileName,path,err);
    return 0;
}

// Clear the UI cache
void QtWindow::clearUICache(const char* fileName)
{
    if (!fileName)
	UIBuffer::s_uiCache.clear();
    else
	TelEngine::destruct(UIBuffer::s_uiCache.find(fileName));
}

// Filter events
bool QtWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (!obj)
	return false;
    // Apply dynamic properties changes
    if (event->type() == QEvent::DynamicPropertyChange) {
	String name = YQT_OBJECT_NAME(obj);
	QDynamicPropertyChangeEvent* ev = static_cast<QDynamicPropertyChangeEvent*>(event);
	String prop = ev->propertyName().constData();
	// Handle only yate dynamic properties
	if (!prop.startsWith(s_yatePropPrefix,false))
	    return QWidget::eventFilter(obj,event);
	XDebug(QtDriver::self(),DebugAll,"Window(%s) eventFilter(%s) prop=%s [%p]",
	    m_id.c_str(),YQT_OBJECT_NAME(obj),prop.c_str(),this);
	// Return false for now on: it's our property
	QtWidget w(obj);
	if (w.invalid())
	    return false;
	String value;
	if (!QtClient::getProperty(obj,prop,value))
	    return false;
	bool ok = true;
	bool handled = true;
	if (prop == s_propColWidths) {
	    if (w.type() == QtWidget::Table) {
		QHeaderView* hdr = w.table()->horizontalHeader();
		bool skipLast = hdr && hdr->stretchLastSection();
		ObjList* list = value.split(',',false);
		int col = 0;
		for (ObjList* o = list->skipNull(); o; o = o->skipNext(), col++) {
		    if (skipLast && col == w.table()->columnCount() - 1)
			break;
		    int width = (static_cast<String*>(o->get()))->toInteger(-1);
		    if (width >= 0)
			w.table()->setColumnWidth(col,width);
		}
		TelEngine::destruct(list);
	    }
	}
	else if (prop == s_propSorting) {
	    if (w.type() == QtWidget::Table) {
		ObjList* list = value.split(',',false);
		String* tmp = static_cast<String*>((*list)[0]);
		int col = tmp ? tmp->toInteger(-1) : -1;
		if (col >= 0) {
		    tmp = static_cast<String*>((*list)[1]);
		    bool asc = tmp ? tmp->toBoolean(true) : true;
		    w.table()->sortItems(col,asc ? Qt::AscendingOrder : Qt::DescendingOrder);
		}
		TelEngine::destruct(list);
	    }
	}
	else if (prop == s_propSizes) {
	    if (w.type() == QtWidget::Splitter) {
		QList<int> list = QtClient::str2IntList(value);
		w.splitter()->setSizes(list);
	    }
	}
	else if (prop == s_propWindowFlags) {
	    QWidget* wid = (name == m_id || name == m_oldId) ? this : w.widget();
	    QtClient::applyWindowFlags(wid,value);
	}
	else if (prop == s_propHHeader) {
	    // Show/hide the horizontal header
	    ok = ((w.type() == QtWidget::Table || w.type() == QtWidget::CustomTable) &&
		value.isBoolean() && w.table()->horizontalHeader());
	    if (ok)
		w.table()->horizontalHeader()->setVisible(value.toBoolean());
	}
	else
	    ok = handled = false;
	if (ok)
	    DDebug(ClientDriver::self(),DebugAll,
		"Applied dynamic property %s='%s' for object='%s'",
		prop.c_str(),value.c_str(),name.c_str());
	else if (handled)
	    Debug(ClientDriver::self(),DebugMild,
		"Failed to apply dynamic property %s='%s' for object='%s'",
		prop.c_str(),value.c_str(),name.c_str());
	return false;
    }
    if (event->type() == QEvent::KeyPress) {
	String action;
	bool filter = false;
	if (!QtClient::filterKeyEvent(obj,static_cast<QKeyEvent*>(event),
	    action,filter,this))
	    return QWidget::eventFilter(obj,event);
	if (action && Client::self())
	    Client::self()->action(this,action);
	return filter;
    }
    if (event->type() == QEvent::ContextMenu) {
	if (handleContextMenuEvent(static_cast<QContextMenuEvent*>(event),obj))
	    return false;
    }
    if (event->type() == QEvent::Enter) {
	QtClient::updateImageFromMouse(obj,true,true);
	return QWidget::eventFilter(obj,event);
    }
    if (event->type() == QEvent::Leave) {
	QtClient::updateImageFromMouse(obj,true,false);
	return QWidget::eventFilter(obj,event);
    }
    if (event->type() == QEvent::MouseButtonPress) {
	QtClient::updateImageFromMouse(obj,false,true);
	return QWidget::eventFilter(obj,event);
    }
    if (event->type() == QEvent::MouseButtonRelease) {
	QtClient::updateImageFromMouse(obj,false,false);
	return QWidget::eventFilter(obj,event);
    }
    return QWidget::eventFilter(obj,event);
}

// Handle key pressed events
void QtWindow::keyPressEvent(QKeyEvent* event)
{
    if (!(Client::self() && event)) {
	QWidget::keyPressEvent(event);
	return;
    }
    QVariant var = this->property("_yate_keypress_redirect");
    QString child = var.toString();
    if (child.size() > 0 && QtClient::sendEvent(*event,this,child)) {
	QWidget* wid = qFindChild<QWidget*>(this,child);
	if (wid)
	    wid->setFocus();
	return;
    }
    if (event->key() == Qt::Key_Backspace)
	Client::self()->backspace(m_id,this);
    QWidget::keyPressEvent(event);
}

// Show hide window. Notify the client
void QtWindow::setVisible(bool visible)
{
    // Override position for notification windows
    if (visible && isShownNormal() &&
	QtClient::getBoolProperty(wndWidget(),"_yate_notificationwindow")) {
	// Don't move
	m_moving = -1;
#ifndef Q_WS_MAC
	// Detect unavailable screen space position and move the window in the apropriate position
	// bottom/right/none: move it in the right/bottom corner.
	// top: move it in the right/top corner.
	// left: move it in the left/bottom corner.
	int pos = QtClient::PosNone;
	if (QtClient::getScreenUnavailPos(this,pos)) {
	    if (0 != (pos & (QtClient::PosBottom | QtClient::PosRight)) || pos == QtClient::PosNone)
		QtClient::moveWindow(this,QtClient::CornerBottomRight);
	    else if (0 != (pos & QtClient::PosTop))
		QtClient::moveWindow(this,QtClient::CornerTopRight);
	    else
		QtClient::moveWindow(this,QtClient::CornerBottomLeft);
	}
#else
	QtClient::moveWindow(this,QtClient::CornerTopRight);
#endif
    }
    if (visible && isMinimized())
	showNormal();
    else
	QWidget::setVisible(visible);
    // Notify the client on window visibility changes
    bool changed = (m_visible != visible);
    m_visible = visible;
    if (changed && Client::self()) {
	QVariant var;
	if (this)
	    var = this->property("dynamicUiActionVisibleChanged");
	if (!var.toBool())
	    Client::self()->toggle(this,YSTRING("window_visible_changed"),m_visible);
	else {
	    Message* m = new Message("ui.action");
	    m->addParam("action","window_visible_changed");
	    m->addParam("visible",String::boolText(m_visible));
	    m->addParam("window",m_id);
	    Engine::enqueue(m);
	}
    }
    if (!m_visible && QtClient::getBoolProperty(wndWidget(),"_yate_destroyonhide")) {
	DDebug(QtDriver::self(),DebugAll,
	    "Window(%s) setVisible(false) set delete later [%p]",m_id.c_str(),this);
	QObject::deleteLater();
    }
    // Destroy owned dialogs
    if (!m_visible) {
	QList<QDialog*> d = qFindChildren<QDialog*>(this);
	for (int i = 0; i < d.size(); i++)
	    d[i]->deleteLater();
    }
}

// Show the window
void QtWindow::show()
{
    setVisible(true);
    m_maximized = m_maximized || isMaximized();
    if (m_maximized)
	setWindowState(Qt::WindowMaximized);
}

// Hide the window
void QtWindow::hide()
{
    setVisible(false);
}

void QtWindow::size(int width, int height)
{
    Debug(QtDriver::self(),DebugStub,"QtWindow(%s)::size(%d,%d) [%p]",m_id.c_str(),width,height,this);
}

void QtWindow::move(int x, int y)
{
    DDebug(QtDriver::self(),DebugAll,"QtWindow(%s)::move(%d,%d) [%p]",m_id.c_str(),x,y,this);
    QWidget::move(x,y);
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

// Create a modal dialog
bool QtWindow::createDialog(const String& name, const String& title, const String& alias,
    const NamedList* params)
{
    QtDialog* d = new QtDialog(this);
    if (d->show(name,title,alias,params))
	return true;
    d->deleteLater();
    return false;
}

// Destroy a modal dialog
bool QtWindow::closeDialog(const String& name)
{
    QDialog* d = qFindChild<QDialog*>(this,QtClient::setUtf8(name));
    if (!d)
	return false;
    d->deleteLater();
    return true;
}

// Load UI file and setup the window
void QtWindow::doPopulate()
{
    Debug(QtDriver::self(),DebugAll,"Populating window '%s' [%p]",m_id.c_str(),this);
    QWidget* formWidget = loadUI(m_description,this,m_id);
    if (!formWidget)
	return;
    // Set window title decoration flags to avoid pos/size troubles with late decoration
    QVariant var = formWidget->property(s_propWindowFlags);
    if (var.type() == QVariant::Invalid) {
	String flgs = "title,sysmenu,minimize,close";
	// Add maximize only if allowed
	if (formWidget->maximumWidth() == QWIDGETSIZE_MAX ||
	    formWidget->maximumHeight() == QWIDGETSIZE_MAX)
	    flgs.append("maximize",",");
	formWidget->setProperty(s_propWindowFlags,QVariant(QtClient::setUtf8(flgs)));
    }
    setMinimumSize(formWidget->minimumSize().width(),formWidget->minimumSize().height());
    setMaximumSize(formWidget->maximumSize().width(),formWidget->maximumSize().height());
    m_x = formWidget->pos().x();
    m_y = formWidget->pos().y();
    m_width = formWidget->width();
    m_height = formWidget->height();
    move(m_x,m_y);
    QWidget::resize(m_width,m_height);
    QtClient::setWidget(this,formWidget);
    m_widget = YQT_OBJECT_NAME(formWidget);
    String wTitle;
    QtClient::getUtf8(wTitle,formWidget->windowTitle());
    title(wTitle);
    setWindowIcon(formWidget->windowIcon());
    setStyleSheet(formWidget->styleSheet());
}

// Initialize window
void QtWindow::doInit()
{
    DDebug(QtDriver::self(),DebugAll,"Initializing window '%s' [%p]",
	m_id.c_str(),this);

    // Create window's dynamic properties from config
    Configuration cfg(Engine::configFile(m_oldId),false);
    NamedList* sectGeneral = cfg.getSection("general");
    if (sectGeneral)
	addDynamicProps(this,*sectGeneral);

    // Load window data
    m_mainWindow = s_cfg.getBoolValue(m_oldId,"mainwindow");
    m_saveOnClose = s_cfg.getBoolValue(m_oldId,"save",true);
    if (m_id != m_oldId)
	m_saveOnClose = s_cfg.getBoolValue(m_oldId,"savealias",m_saveOnClose);
    NamedList* sect = s_save.getSection(m_id);
    if (sect) {
	m_maximized = sect->getBoolValue("maximized");
	m_x = sect->getIntValue("x",m_x);
	m_y = sect->getIntValue("y",m_y);
	m_width = sect->getIntValue("width",m_width);
	m_height = sect->getIntValue("height",m_height);
	m_visible = sect->getBoolValue("visible");
    }
    else {
	if (m_saveOnClose)
	    Debug(QtDriver::self(),DebugNote,"Window(%s) not found in config [%p]",
		m_id.c_str(),this);
	m_visible = s_cfg.getBoolValue(m_oldId,"visible");
	// Make sure the window is shown in the available geometry
	QDesktopWidget* d = QApplication::desktop();
	if (d) {
	    QRect r = d->availableGeometry(this);
	    m_x = r.x();
	    m_y = r.y();
	}
    }
    m_visible = m_mainWindow || m_visible;
    if (!m_width)
	m_width = this->width();
    if (!m_height)
	m_height = this->height();
    move(m_x,m_y);
    QWidget::resize(m_width,m_height);

    // Build custom UI widgets from frames owned by this widget
    QtClient::buildFrameUiWidgets(this);

    // Create custom widgets from
    // _yate_identity=customwidget|[separator=sep|] sep widgetclass sep widgetname [sep param=value]
    QList<QFrame*> frm = qFindChildren<QFrame*>(this);
    for (int i = 0; i < frm.size(); i++) {
	String create;
	QtClient::getProperty(frm[i],"_yate_identity",create);
	if (!create.startSkip("customwidget|",false))
	    continue;
	char sep = '|';
	// Check if we have another separator
	if (create.startSkip("separator=",false)) {
	    if (create.length() < 2)
		continue;
	    sep = create.at(0);
	    create = create.substr(2);
	}
	ObjList* list = create.split(sep,false);
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
		    params.addParam(p->toString().substr(0,pos),p->toString().substr(pos + 1));
	    }
	}
	TelEngine::destruct(list);
	params.addParam("parentwindow",m_id);
	NamedString* pw = new NamedString("parentwidget");
	QtClient::getUtf8(*pw,frm[i]->objectName());
	params.addParam(pw);
	QObject* obj = (QObject*)UIFactory::build(type,name,&params);
	if (!obj)
	    continue;
	QWidget* wid = qobject_cast<QWidget*>(obj);
	if (wid)
	    QtClient::setWidget(frm[i],wid);
	else {
	    obj->setParent(frm[i]);
	    QtCustomObject* customObj = qobject_cast<QtCustomObject*>(obj);
	    if (customObj)
		customObj->parentChanged();
	}
    }

    // Add the first menubar to layout
    QList<QMenuBar*> menuBars = qFindChildren<QMenuBar*>(this);
    if (menuBars.size() && layout()) {
	layout()->setMenuBar(menuBars[0]);
	// Decrease minimum size policy to make sure the layout is made properly
	if (wndWidget()) {
	    int h = menuBars[0]->height();
	    int min = wndWidget()->minimumHeight();
	    if (min > h)
		wndWidget()->setMinimumHeight(min - h);
	    else
		wndWidget()->setMinimumHeight(0);
	}
#ifdef Q_WS_MAC
	if (m_mainWindow) {
	    // Create a parentless menu bar to be set as the default application menu by copying it from the main window menu
	    DDebug(QtDriver::self(),DebugAll,"Setting as default menu bar the menu bar of window '%s' [%p]",
	    m_id.c_str(),this);
	    QMenuBar* mainMenu = menuBars[0];
	    QMenuBar* defaultMenu = new QMenuBar(0);
	    QList<QAction*> topActions = mainMenu->actions();
	    for (int i = 0; i < topActions.count(); i++) {
		QMenu* menu = topActions[i]->menu();
		if (menu) {
		    QMenu* m = new QMenu(menu->title(),defaultMenu);
		    String tmp;
		    QtClient::getProperty(menu,YSTRING("_yate_menuNoCopy"),tmp);
		    if (tmp.toBoolean())
			continue;
		    defaultMenu->addMenu(m);
		    QList<QAction*> actions = menu->actions();
		    for (int j = 0; j < actions.count(); j++) {
			QAction* act = actions[j];
			tmp.clear();
			QtClient::getProperty(act,YSTRING("_yate_menuNoCopy"),tmp);
			if (tmp.toBoolean())
			    continue;
			m->addAction(act);
		    }
		}
	    }
	}
#endif
    }

    // Create window's children dynamic properties from config
    unsigned int n = cfg.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* sect = cfg.getSection(i);
	if (sect && *sect && *sect != "general")
	    addDynamicProps(qFindChild<QObject*>(this,sect->c_str()),*sect);
    }

    // Process "_yate_setaction" property for our children
    QtClient::setAction(this);

    // Connect actions' signal
    QList<QAction*> actions = qFindChildren<QAction*>(this);
    for (int i = 0; i < actions.size(); i++) {
	String addToWidget;
	QtClient::getProperty(actions[i],"dynamicAddToParent",addToWidget);
	if (addToWidget && addToWidget.toBoolean())
	    QWidget::addAction(actions[i]);
	if (actions[i]->isCheckable())
	    QtClient::connectObjects(actions[i],SIGNAL(toggled(bool)),this,SLOT(toggled(bool)));
	else
	    QtClient::connectObjects(actions[i],SIGNAL(triggered()),this,SLOT(action()));
    }

    // Connect combo boxes signals
    QList<QComboBox*> combos = qFindChildren<QComboBox*>(this);
    for (int i = 0; i < combos.size(); i++) {
	QtClient::connectObjects(combos[i],SIGNAL(activated(int)),this,SLOT(selectionChanged()));
    	connectTextChanged(combos[i]);
    }

    // Connect abstract buttons (check boxes and radio/push/tool buttons) signals
    QList<QAbstractButton*> buttons = qFindChildren<QAbstractButton*>(this);
    for(int i = 0; i < buttons.size(); i++)
	if (QtClient::autoConnect(buttons[i]))
	    connectButton(buttons[i]);

    // Connect group boxes signals
    QList<QGroupBox*> grp = qFindChildren<QGroupBox*>(this);
    for(int i = 0; i < grp.size(); i++)
	if (grp[i]->isCheckable())
	    QtClient::connectObjects(grp[i],SIGNAL(toggled(bool)),this,SLOT(toggled(bool)));

    // Connect sliders signals
    QList<QSlider*> sliders = qFindChildren<QSlider*>(this);
    for (int i = 0; i < sliders.size(); i++)
	QtClient::connectObjects(sliders[i],SIGNAL(valueChanged(int)),this,SLOT(selectionChanged()));

    // Connect calendar widget signals
    QList<QCalendarWidget*> cals = qFindChildren<QCalendarWidget*>(this);
    for (int i = 0; i < cals.size(); i++)
	QtClient::connectObjects(cals[i],SIGNAL(selectionChanged()),this,SLOT(selectionChanged()));

    // Connect list boxes signals
    QList<QListWidget*> lists = qFindChildren<QListWidget*>(this);
    for (int i = 0; i < lists.size(); i++) {
	QtClient::connectObjects(lists[i],SIGNAL(itemDoubleClicked(QListWidgetItem*)),
	    this,SLOT(doubleClick()));
	QtClient::connectObjects(lists[i],SIGNAL(itemActivated(QListWidgetItem*)),
	    this,SLOT(doubleClick()));
	QtClient::connectObjects(lists[i],SIGNAL(currentRowChanged(int)),
	    this,SLOT(selectionChanged()));
    }

    // Connect tab widget signals
    QList<QTabWidget*> tabs = qFindChildren<QTabWidget*>(this);
    for (int i = 0; i < tabs.size(); i++)
	QtClient::connectObjects(tabs[i],SIGNAL(currentChanged(int)),this,SLOT(selectionChanged()));

    // Connect stacked widget signals
    QList<QStackedWidget*> sw = qFindChildren<QStackedWidget*>(this);
    for (int i = 0; i < sw.size(); i++)
	QtClient::connectObjects(sw[i],SIGNAL(currentChanged(int)),this,SLOT(selectionChanged()));

    // Connect line edit signals
    QList<QLineEdit*> le = qFindChildren<QLineEdit*>(this);
    for (int i = 0; i < le.size(); i++)
	connectTextChanged(le[i]);

    // Connect text edit signals
    QList<QTextEdit*> te = qFindChildren<QTextEdit*>(this);
    for (int i = 0; i < te.size(); i++)
	connectTextChanged(te[i]);

    // Process tables:
    // Insert a column and connect signals
    // Hide columns starting with "hidden:"
    QList<QTableWidget*> tables = qFindChildren<QTableWidget*>(this);
    for (int i = 0; i < tables.size(); i++) {
	bool nonCustom = (0 == qobject_cast<QtTable*>(tables[i]));
	// Horizontal header
	QHeaderView* hdr = tables[i]->horizontalHeader();
	// Stretch last column
	bool b = QtClient::getBoolProperty(tables[i],"_yate_horizontalstretch",true);
	hdr->setStretchLastSection(b);
	String tmp;
	QtClient::getProperty(tables[i],"_yate_horizontalheader_align",tmp);
	if (tmp) {
	    int def = hdr->defaultAlignment();
	    hdr->setDefaultAlignment((Qt::Alignment)QtClient::str2align(tmp,def));
	}
	if (!QtClient::getBoolProperty(tables[i],"_yate_horizontalheader",true))
	    hdr->hide();
	// Vertical header
	hdr = tables[i]->verticalHeader();
	int itemH = QtClient::getIntProperty(tables[i],"_yate_rowheight");
	if (itemH > 0)
	    hdr->setDefaultSectionSize(itemH);
	if (!QtClient::getBoolProperty(tables[i],"_yate_verticalheader"))
	    hdr->hide();
	else {
	    int width = QtClient::getIntProperty(tables[i],"_yate_verticalheaderwidth");
	    if (width > 0)
		hdr->setFixedWidth(width);
	    if (!QtClient::getBoolProperty(tables[i],"_yate_allowvheaderresize"))
		hdr->setResizeMode(QHeaderView::Fixed);
	}
	if (nonCustom) {
	    // Set _yate_save_props
	    QVariant var = tables[i]->property(s_propsSave);
	    if (var.type() != QVariant::StringList) {
		// Create the property if not found, ignore it if not a string list
		if (var.type() == QVariant::Invalid)
		    var = QVariant(QVariant::StringList);
		else
		    Debug(QtDriver::self(),DebugNote,
			"Window(%s) table '%s' already has a non string list property %s [%p]",
			m_id.c_str(),YQT_OBJECT_NAME(tables[i]),s_propsSave.c_str(),this);
	    }
	    if (var.type() == QVariant::StringList) {
		// Make sure saved properties exists to allow them to be restored
		QStringList sl = var.toStringList();
		bool changed = createProperty(tables[i],s_propColWidths,QVariant::String,this,&sl);
		changed = createProperty(tables[i],s_propSorting,QVariant::String,this,&sl) || changed;
		if (changed)
		    tables[i]->setProperty(s_propsSave,QVariant(sl));
	    }
	}
	TableWidget t(tables[i]);
	// Insert the column containing the ID
	t.addColumn(0,0,"hidden:id");
	// Hide columns
	for (int i = 0; i < t.columnCount(); i++) {
	    String name;
	    t.getHeaderText(i,name,false);
	    if (name.startsWith("hidden:"))
		t.table()->setColumnHidden(i,true);
	}
	// Connect signals
	QtClient::connectObjects(t.table(),SIGNAL(cellDoubleClicked(int,int)),
	    this,SLOT(doubleClick()));
#if 0
	// This would generate action() twice since QT will signal both cell and
	// table item double click
	QtClient::connectObjects(t.table(),SIGNAL(itemDoubleClicked(QTableWidgetItem*)),
	    this,SLOT(doubleClick()));
#endif
	String noSel;
	getProperty(t.name(),"dynamicNoItemSelChanged",noSel);
	if (!noSel.toBoolean())
	    QtClient::connectObjects(t.table(),SIGNAL(itemSelectionChanged()),
		this,SLOT(selectionChanged()));
	// Optionally connect cell clicked
	// This is done when we want to generate a select() or action() from cell clicked
	String cellClicked;
	getProperty(t.name(),"dynamicCellClicked",cellClicked);
	if (cellClicked) {
	    if (cellClicked == "selectionChanged")
		QtClient::connectObjects(t.table(),SIGNAL(cellClicked(int,int)),
		    this,SLOT(selectionChanged()));
	    else if (cellClicked == "doubleClick")
		QtClient::connectObjects(t.table(),SIGNAL(cellClicked(int,int)),
		    this,SLOT(doubleClick()));
	}
    }

    // Restore saved children properties
    if (sect) {
	unsigned int n = sect->length();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* ns = sect->getParam(i);
	    if (!ns)
		continue;
	    String prop(ns->name());
	    if (!prop.startSkip("property:",false))
		continue;
	    int pos = prop.find(":");
	    if (pos > 0) {
		String wName = prop.substr(0,pos);
		String pName = prop.substr(pos + 1);
		DDebug(QtDriver::self(),DebugAll,
		    "Window(%s) restoring property %s=%s for child '%s' [%p]",
		    m_id.c_str(),pName.c_str(),ns->c_str(),wName.c_str(),this);
		setProperty(wName,pName,*ns);
	    }
	}
    }

    // Install event filter and apply dynamic properties
    QList<QObject*> w = qFindChildren<QObject*>(this);
    w.append(this);
    for (int i = 0; i < w.size(); i++) {
	QList<QByteArray> props = w[i]->dynamicPropertyNames();
	// Check for our dynamic properties
	int j = 0;
	for (j = 0; j < props.size(); j++)
	    if (props[j].startsWith(s_yatePropPrefix))
		break;
	if (j == props.size())
	    continue;
	// Add event hook to be used when a dynamic property changes
	w[i]->installEventFilter(this);
	// Fake dynamic property change to apply them
	for (j = 0; j < props.size(); j++) {
	    if (!props[j].startsWith(s_yatePropPrefix))
		continue;
	    QDynamicPropertyChangeEvent ev(props[j]);
	    eventFilter(w[i],&ev);
	}
    }

    qRegisterMetaType<QModelIndex>("QModelIndex");
    qRegisterMetaType<QTextCursor>("QTextCursor");

    // Force window visibility change notification by changing the visibility flag
    // Some controls might need to be updated
    m_visible = !m_visible;
    if (m_visible) {
	// Disable _yate_destroyonhide property: avoid destroying the window now
	String tmp;
	getProperty(m_id,"_yate_destroyonhide",tmp);
	if (tmp)
	    setProperty(m_id,"_yate_destroyonhide",String::boolText(false));
	hide();
	if (tmp)
	    setProperty(m_id,"_yate_destroyonhide",tmp);
    }
    else
	show();
}

// Mouse button pressed notification
void QtWindow::mousePressEvent(QMouseEvent* event)
{
    if (m_moving >= 0 && Qt::LeftButton == event->button() && isShownNormal()) {
	m_movePos = event->globalPos();
	m_moving = 1;
    }
}

// Mouse button release notification
void QtWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_moving >= 0 && Qt::LeftButton == event->button())
	m_moving = 0;
}

// Move the window if the moving flag is set
void QtWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_moving <= 0 || Qt::LeftButton != event->buttons() || !isShownNormal())
	return;
    int cx = event->globalPos().x() - m_movePos.x();
    int cy = event->globalPos().y() - m_movePos.y();
    if (cx || cy) {
	m_movePos = event->globalPos();
	QWidget::move(x() + cx,y() + cy);
    }
}

// Handle context menu events. Return true if handled
bool QtWindow::handleContextMenuEvent(QContextMenuEvent* event, QObject* obj)
{
    if (!(event && obj))
	return false;
    String mname;
    QtClient::getProperty(obj,s_propContextMenu,mname);
    XDebug(ClientDriver::self(),DebugAll,
	"Window(%s) handleContextMenuEvent() obj=%s menu=%s [%p]",
	m_id.c_str(),YQT_OBJECT_NAME(obj),mname.c_str(),this);
    QMenu* m = mname ? qFindChild<QMenu*>(this,QtClient::setUtf8(mname)) : 0;
    if (m)
	m->exec(event->globalPos());
    return m != 0;
}


/*
 * QtDialog
 */
// Destructor. Notify the client if not exiting
QtDialog::~QtDialog()
{
    QtWindow* w = parentWindow();
    if (w && m_notifyOnClose && Client::valid())
	QtClient::self()->action(w,buildActionName(m_notifyOnClose,m_notifyOnClose));
    DDebug(QtDriver::self(),DebugAll,"QtWindow(%s) QtDialog(%s) destroyed [%p]",
	w ? w->id().c_str() : "",YQT_OBJECT_NAME(this),w);
}

// Initialize dialog. Load the widget.
// Connect non checkable actions to own slot.
// Connect checkable actions/buttons to parent window's slot
// Display the dialog on success
bool QtDialog::show(const String& name, const String& title, const String& alias,
    const NamedList* params)
{
    QtWindow* w = parentWindow();
    if (!w)
	return false;
    QWidget* widget = QtWindow::loadUI(Client::s_skinPath + s_cfg.getValue(name,"description"),this,name);
    if (!widget)
	return false;
    QtClient::getProperty(widget,"_yate_notifyonclose",m_notifyOnClose);
    setObjectName(QtClient::setUtf8(alias ? alias : name));
    setMinimumSize(widget->minimumSize().width(),widget->minimumSize().height());
    setMaximumSize(widget->maximumSize().width(),widget->maximumSize().height());
    resize(widget->width(),widget->height());
    QtClient::setWidget(this,widget);
    if (title)
	setWindowTitle(QtClient::setUtf8(title));
    else if (widget->windowTitle().length())
	setWindowTitle(widget->windowTitle());
    else
	setWindowTitle(w->windowTitle());
    // Connect abstract buttons (check boxes and radio/push/tool buttons) signals
    QList<QAbstractButton*> buttons = qFindChildren<QAbstractButton*>(widget);
    for(int i = 0; i < buttons.size(); i++) {
	if (!QtClient::autoConnect(buttons[i]))
	    continue;
	if (!buttons[i]->isCheckable())
	    QtClient::connectObjects(buttons[i],SIGNAL(clicked()),this,SLOT(action()));
	else
	    QtClient::connectObjects(buttons[i],SIGNAL(toggled(bool)),w,SLOT(toggled(bool)));
    }
    // Connect actions' signal
    QList<QAction*> actions = qFindChildren<QAction*>(widget);
    for (int i = 0; i < actions.size(); i++) {
	if (!QtClient::autoConnect(actions[i]))
	    continue;
	if (!actions[i]->isCheckable())
	    QtClient::connectObjects(actions[i],SIGNAL(triggered()),this,SLOT(action()));
	else
	    QtClient::connectObjects(actions[i],SIGNAL(toggled(bool)),w,SLOT(toggled(bool)));
    }
    String* flags = 0;
    String tmp;
    QtClient::getProperty(widget,s_propWindowFlags,tmp);
    if (tmp)
	flags = &tmp;
    if (params) {
	if (!flags)
	    flags = params->getParam(s_propWindowFlags);
	m_closable = params->getBoolValue(YSTRING("closable"),"true");
	w->setParams(*params);
    }
    if (flags)
	QtClient::applyWindowFlags(this,*flags);
    setWindowModality(Qt::WindowModal);
    QDialog::show();
    return true;
}

// Notify client
void QtDialog::action()
{
    QtWindow* w = parentWindow();
    if (!w)
	return;
    DDebug(QtDriver::self(),DebugAll,"QtWindow(%s) dialog action '%s' [%p]",
	w->id().c_str(),YQT_OBJECT_NAME(sender()),w);
    if (!QtClient::self() || QtClient::changing())
	return;
    String name;
    QtClient::getIdentity(sender(),name);
    if (name && QtClient::self()->action(w,buildActionName(name,name)))
	deleteLater();
}

// Delete the dialog
void QtDialog::closeEvent(QCloseEvent* event)
{
    if (m_closable) {
	QDialog::closeEvent(event);
	deleteLater();
    }
    else
	event->ignore();
}

// Destroy the dialog
void QtDialog::reject()
{
    if (!m_closable)
	return;
    QDialog::reject();
    deleteLater();
}


/**
 * QtClient
 */
QtClient::QtClient()
    : Client("Qt Client")
{
    m_oneThread = Engine::config().getBoolValue("client","onethread",true);

    s_save = Engine::configFile("qt4client",true);
    s_save.load();
    // Fill QT styles
    s_qtStyles.addParam("IaOraKde","iaorakde");
    s_qtStyles.addParam("QWindowsStyle","windows");
    s_qtStyles.addParam("QMacStyle","mac");
    s_qtStyles.addParam("QMotifStyle","motif");
    s_qtStyles.addParam("QCDEStyle","cde");
    s_qtStyles.addParam("QWindowsXPStyle","windowsxp");
    s_qtStyles.addParam("QCleanlooksStyle","cleanlooks");
    s_qtStyles.addParam("QPlastiqueStyle","plastique");
    s_qtStyles.addParam("QGtkStyle","gtk");
    s_qtStyles.addParam("IaOraQt","iaoraqt");
    s_qtStyles.addParam("OxygenStyle","oxygen");
    s_qtStyles.addParam("PhaseStyle","phase");
}

QtClient::~QtClient()
{
}

void QtClient::cleanup()
{
    Client::cleanup();
    m_events.clear();
    Client::save(s_save);
    QtWindow::clearUICache();
    m_app->quit();
    if (!m_app->startingUp())
	delete m_app;
}

void QtClient::run()
{
    const char* style = Engine::config().getValue("client","style");
    if (style && !QApplication::setStyle(QString::fromUtf8(style)))
	Debug(ClientDriver::self(),DebugWarn,"Could not set Qt style '%s'",style);
    int argc = 0;
    char* argv =  0;
    m_app = new QApplication(argc,&argv);
    m_app->setQuitOnLastWindowClosed(false);
    updateAppStyleSheet();
    String imgRead;
    QList<QByteArray> imgs = QImageReader::supportedImageFormats();
    for (int i = 0; i < imgs.size(); i++)
	imgRead.append(imgs[i].constData(),",");
    imgRead = "read image formats '" + imgRead + "'";
    Debug(ClientDriver::self(),DebugInfo,"QT client start running (version=%s) %s",
	qVersion(),imgRead.c_str());
    if (!QSound::isAvailable())
	Debug(ClientDriver::self(),DebugWarn,"QT sounds are not available");
    // Create events proxy
    m_events.append(new QtEventProxy(QtEventProxy::Timer));
    m_events.append(new QtEventProxy(QtEventProxy::AllHidden,m_app));
    if (Engine::exiting())
	return;
    Client::run();
}

void QtClient::main()
{
    if (!Engine::exiting())
	m_app->exec();
}

void QtClient::lock()
{}

void QtClient::unlock()
{}

void QtClient::allHidden()
{
    Debug(QtDriver::self(),DebugInfo,"QtClient::allHiden() counter=%d",s_allHiddenQuit);
    if (s_allHiddenQuit > 0)
	return;
    quit();
}

bool QtClient::createWindow(const String& name, const String& alias)
{
    String parent = s_cfg.getValue(name,"parent");
    QtWindow* parentWnd = 0;
    if (!TelEngine::null(parent)) {
	ObjList* o = m_windows.find(parent);
	if (o)
	    parentWnd = YOBJECT(QtWindow,o->get());
    }
    QtWindow* w = new QtWindow(name,s_skinPath + s_cfg.getValue(name,"description"),alias,parentWnd);
    if (w) {
	Debug(QtDriver::self(),DebugAll,"Created window name=%s alias=%s with parent=(%s [%p]) (%p)",
	    name.c_str(),alias.c_str(),parent.c_str(),parentWnd,w);
	// Remove the old window
	ObjList* o = m_windows.find(w->id());
	if (o)
	    Client::self()->closeWindow(w->id(),false);
	w->populate();
	m_windows.append(w);
	return true;
    }
    else
	Debug(QtDriver::self(),DebugGoOn,"Could not create window name=%s alias=%s",
	    name.c_str(),alias.c_str());
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
	if (l && l->getBoolValue(YSTRING("enabled"),true))
	    createWindow(*l);
    }
}

bool QtClient::isUIThread()
{
    return (QApplication::instance() && QApplication::instance()->thread() == QThread::currentThread());
}

// Open a file open dialog window
// Parameters that can be specified include 'caption',
//  'dir', 'filter', 'selectedfilter', 'confirmoverwrite', 'choosedir'
bool QtClient::chooseFile(Window* parent, NamedList& params)
{
    QtWindow* wnd = static_cast<QtWindow*>(parent);
    QFileDialog* dlg = new QFileDialog(wnd,setUtf8(params.getValue(YSTRING("caption"))),
	setUtf8(params.getValue(YSTRING("dir"))));

    if (wnd)
	dlg->setWindowIcon(wnd->windowIcon());

    // Connect signals
    String* action = params.getParam(YSTRING("action"));
    if (wnd && !null(action)) {
	dlg->setObjectName(setUtf8(*action));
	QtClient::connectObjects(dlg,SIGNAL(accepted()),wnd,SLOT(chooseFileAccepted()));
	QtClient::connectObjects(dlg,SIGNAL(rejected()),wnd,SLOT(chooseFileRejected()));
    }

    // Destroy it when closed
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // This dialog should always stay on top
    dlg->setWindowFlags(dlg->windowFlags() | Qt::WindowStaysOnTopHint);

    if (params.getBoolValue(YSTRING("modal"),true))
	dlg->setWindowModality(Qt::WindowModal);

    // Filters
    NamedString* f = params.getParam(YSTRING("filters"));
    if (f) {
	QStringList filters;
	ObjList* obj = f->split('|',false);
	for (ObjList* o = obj->skipNull(); o; o = o->skipNext())
	    filters.append(QtClient::setUtf8(o->get()->toString()));
	TelEngine::destruct(obj);
	dlg->setFilters(filters);
    }
    QString flt = QtClient::setUtf8(params.getValue(YSTRING("selectedfilter")));
    if (flt.length())
	dlg->selectFilter(flt);

    if (params.getBoolValue(YSTRING("save")))
	dlg->setAcceptMode(QFileDialog::AcceptSave);
    else
	dlg->setAcceptMode(QFileDialog::AcceptOpen);

    // Choose options
    if (params.getBoolValue(YSTRING("choosefile"),true)) {
	if (params.getBoolValue(YSTRING("chooseanyfile")))
	    dlg->setFileMode(QFileDialog::AnyFile);
	else if (params.getBoolValue(YSTRING("multiplefiles")))
	    dlg->setFileMode(QFileDialog::ExistingFiles);
	else
	    dlg->setFileMode(QFileDialog::ExistingFile);
    }
    else
	dlg->setFileMode(QFileDialog::DirectoryOnly);

    dlg->selectFile(QtClient::setUtf8(params.getValue(YSTRING("selectedfile"))));

    dlg->setVisible(true);
    return true;
}

bool QtClient::action(Window* wnd, const String& name, NamedList* params)
{
    String tmp = name;
    if (tmp.startSkip("openurl:",false))
	return openUrl(tmp);
    return Client::action(wnd,name,params);
}

// Create a sound object. Append it to the global list
bool QtClient::createSound(const char* name, const char* file, const char* device)
{
    if (!(QSound::isAvailable() && name && *name && file && *file))
	return false;
    Lock lock(ClientSound::s_soundsMutex);
    if (ClientSound::s_sounds.find(name))
	return false;
    ClientSound::s_sounds.append(new QtSound(name,file,device));
    DDebug(ClientDriver::self(),DebugAll,"Added sound=%s file=%s device=%s",
	name,file,device);
    return true;
}

// Build a date/time string from UTC time
bool QtClient::formatDateTime(String& dest, unsigned int secs,
    const char* format, bool utc)
{
    if (!(format && *format))
	return false;
    QtClient::getUtf8(dest,formatDateTime(secs,format,utc));
    return true;
}

// Build a date/time QT string from UTC time
QString QtClient::formatDateTime(unsigned int secs, const char* format, bool utc)
{
    QDateTime time;
    if (utc)
	time.setTimeSpec(Qt::UTC);
    time.setTime_t(secs);
    return time.toString(format);
}

// Retrieve an object's QtWindow parent
QtWindow* QtClient::parentWindow(QObject* obj)
{
    for (; obj; obj = obj->parent()) {
	QtWindow* w = qobject_cast<QtWindow*>(obj);
	if (w)
	    return w;
    }
    return 0;
}

// Save an object's property into parent window's section. Clear it on failure
bool QtClient::saveProperty(QObject* obj, const String& prop, QtWindow* owner)
{
    if (!obj)
	return false;
    if (!owner)
	owner = parentWindow(obj);
    if (!owner)
	return false;
    String value;
    bool ok = getProperty(obj,prop,value);
    String pName;
    pName << "property:" << YQT_OBJECT_NAME(obj) << ":" << prop;
    if (ok)
	s_save.setValue(owner->id(),pName,value);
    else
	s_save.clearKey(owner->id(),pName);
    return ok;
}

// Set or an object's property
bool QtClient::setProperty(QObject* obj, const char* name, const String& value)
{
    if (!(obj && name && *name))
	return false;
    QVariant var = obj->property(name);
    const char* err = 0;
    bool ok = false;
    switch (var.type()) {
	case QVariant::String:
	    ok = obj->setProperty(name,QVariant(QtClient::setUtf8(value)));
	    break;
	case QVariant::Bool:
	    ok = obj->setProperty(name,QVariant(value.toBoolean()));
	    break;
	case QVariant::Int:
	    ok = obj->setProperty(name,QVariant(value.toInteger()));
	    break;
	case QVariant::UInt:
	    ok = obj->setProperty(name,QVariant((unsigned int)value.toInteger()));
	    break;
	case QVariant::Icon:
	    ok = obj->setProperty(name,QVariant(QIcon(QtClient::setUtf8(value))));
	    break;
	case QVariant::Pixmap:
	    ok = obj->setProperty(name,QVariant(QPixmap(QtClient::setUtf8(value))));
	    break;
	case QVariant::Double:
	    ok = obj->setProperty(name,QVariant(value.toDouble()));
	    break;
	case QVariant::KeySequence:
	    ok = obj->setProperty(name,QVariant(QtClient::setUtf8(value)));
	    break;
	case QVariant::StringList:
	    {
		QStringList qList;
		if (value)
		    qList.append(setUtf8(value));
		ok = obj->setProperty(name,QVariant(qList));
	    }
	    break;
	case QVariant::Invalid:
	    err = "no such property";
	    break;
	default:
	    err = "unsupported type";
    }
    YIGNORE(err);
    if (ok)
	DDebug(ClientDriver::self(),DebugAll,"Set property %s=%s for object '%s'",
	    name,value.c_str(),YQT_OBJECT_NAME(obj));
    else
	DDebug(ClientDriver::self(),DebugNote,
	    "Failed to set %s=%s (type=%s) for object '%s': %s",
	    name,value.c_str(),var.typeName(),YQT_OBJECT_NAME(obj),err);
    return ok;
}

// Get an object's property
bool QtClient::getProperty(QObject* obj, const char* name, String& value)
{
    if (!(obj && name && *name))
	return false;
    QVariant var = obj->property(name);
    if (var.type() == QVariant::StringList) {
	NamedList* l = static_cast<NamedList*>(value.getObject(YATOM("NamedList")));
	if (l)
	    copyParams(*l,var.toStringList());
	else
	    getUtf8(value,var.toStringList().join(","));
	DDebug(ClientDriver::self(),DebugAll,"Got list property %s for object '%s'",
	    name,YQT_OBJECT_NAME(obj));
	return true;
    }
    if (var.canConvert(QVariant::String)) {
	QtClient::getUtf8(value,var.toString());
	DDebug(ClientDriver::self(),DebugAll,"Got property %s=%s for object '%s'",
	    name,value.c_str(),YQT_OBJECT_NAME(obj));
	return true;
    }
    DDebug(ClientDriver::self(),DebugNote,
	"Failed to get property '%s' (type=%s) for object '%s': %s",
	name,var.typeName(),YQT_OBJECT_NAME(obj),
	((var.type() == QVariant::Invalid) ? "no such property" : "unsupported type"));
    return false;
}

// Copy a string list to a list of parameters
void QtClient::copyParams(NamedList& dest, const QStringList& src)
{
    for (int i = 0; i < src.size(); i++) {
	if (!src[i].length())
	    continue;
	int pos = src[i].indexOf('=');
	String name;
	if (pos >= 0) {
	    getUtf8(name,src[i].left(pos));
	    getUtf8(dest,name,src[i].right(src[i].length() - pos - 1));
	}
	else {
	    getUtf8(name,src[i]);
	    dest.addParam(name,"");
	}
    }
}

// Copy a list of parameters to string list
void QtClient::copyParams(QStringList& dest, const NamedList& src)
{
    unsigned int n = src.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = src.getParam(i);
	if (ns)
	    dest.append(setUtf8(ns->name() + "=" + *ns));
    }
}

// Build QObject properties from list
void QtClient::buildProps(QObject* obj, const String& props)
{
    if (!(obj && props))
	return;
    ObjList* list = props.split(',',false);
    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	int pos = s->find('=');
	if (pos < 1)
	    continue;
	String ptype = s->substr(pos + 1);
	QVariant::Type t = (QVariant::Type)lookup(ptype,s_qVarType,QVariant::Invalid);
	if (t == QVariant::Invalid) {
	    Debug(ClientDriver::self(),DebugStub,
		"QtClient::buildProps() unhandled type '%s'",ptype.c_str());
	    continue;
	}
	String pname = s->substr(0,pos);
	QVariant existing = obj->property(pname);
	if (existing.type() == QVariant::Invalid) {
	    obj->setProperty(pname,QVariant(t));
	    continue;
	}
	Debug(ClientDriver::self(),DebugNote,
	    "Can't create property '%s' type=%s for object (%p,%s): already exists",
	    pname.c_str(),ptype.c_str(),obj,YQT_OBJECT_NAME(obj));
    }
    TelEngine::destruct(list);
}

// Build custom UI widgets from frames owned by a widget
void QtClient::buildFrameUiWidgets(QWidget* parent)
{
    if (!parent)
	return;
    QList<QFrame*> frm = qFindChildren<QFrame*>(parent);
    for (int i = 0; i < frm.size(); i++) {
	if (!getBoolProperty(frm[i],"_yate_uiwidget"))
	    continue;
	String name;
	String type;
	getProperty(frm[i],"_yate_uiwidget_name",name);
	getProperty(frm[i],"_yate_uiwidget_class",type);
	if (!(name && type))
	    continue;
	NamedList params("");
	getProperty(frm[i],"_yate_uiwidget_params",params);
	QtWindow* w = static_cast<QtWindow*>(parent->window());
	if (w)
	    params.setParam("parentwindow",w->id());
	getUtf8(params,"parentwidget",frm[i]->objectName(),true);
	QObject* obj = (QObject*)UIFactory::build(type,name,&params);
	if (!obj)
	    continue;
	QWidget* wid = qobject_cast<QWidget*>(obj);
	if (wid)
	    QtClient::setWidget(frm[i],wid);
	else {
	    obj->setParent(frm[i]);
	    QtCustomObject* customObj = qobject_cast<QtCustomObject*>(obj);
	    if (customObj)
		customObj->parentChanged();
	}
    }
}

// Associate actions to buttons with '_yate_setaction' property set
void QtClient::setAction(QWidget* parent)
{
    if (!parent)
	return;
    QList<QToolButton*> tb = qFindChildren<QToolButton*>(parent);
    for (int i = 0; i < tb.size(); i++) {
	QVariant var = tb[i]->property("_yate_setaction");
	if (var.toString().isEmpty())
	    continue;
	QAction* a = qFindChild<QAction*>(parent,var.toString());
	if (a)
	    tb[i]->setDefaultAction(a);
    }
}

// Build a menu object from a list of parameters
QMenu* QtClient::buildMenu(const NamedList& params, const char* text, QObject* receiver,
	 const char* triggerSlot, const char* toggleSlot, QWidget* parent,
	 const char* aboutToShowSlot)
{
    QMenu* menu = 0;
    unsigned int n = params.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* param = params.getParam(i);
	if (!(param && param->name().startsWith("item:")))
	    continue;

	if (!menu)
	    menu = new QMenu(setUtf8(text),parent);

	NamedList* p = YOBJECT(NamedList,param);
	if (p)  {
	    QMenu* subMenu = buildMenu(*p,*param ? param->c_str() : p->getValue(YSTRING("title"),*p),
		receiver,triggerSlot,toggleSlot,menu);
	    if (subMenu)
		menu->addMenu(subMenu);
	    continue;
	}
	String name = param->name().substr(5);
	if (*param) {
	    QAction* a = menu->addAction(QtClient::setUtf8(*param));
	    a->setObjectName(QtClient::setUtf8(name));
	    a->setParent(menu);
	    setImage(a,params["image:" + name]);
	}
	else if (!name)
	    menu->addSeparator()->setParent(menu);
	else {
	    // Check if the action is already there
	    QAction* a = 0;
	    if (parent && parent->window())
		a = qFindChild<QAction*>(parent->window(),QtClient::setUtf8(name));
	    if (a)
		menu->addAction(a);
	    else
		Debug(ClientDriver::self(),DebugNote,
		    "buildMenu(%s) action '%s' not found",params.c_str(),name.c_str());
	}
    }

    if (!menu)
	return 0;

    // Set name
    menu->setObjectName(setUtf8(params));
    setImage(menu,params["image:" + params]);
    // Apply properties
    // Format: property:object_name:property_name=value
    if (parent)
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* param = params.getParam(i);
	    if (!(param && param->name().startsWith("property:")))
		continue;
	    int pos = param->name().find(':',9);
	    if (pos < 9)
		continue;
	    QObject* obj = qFindChild<QObject*>(parent,setUtf8(param->name().substr(9,pos - 9)));
	    if (obj)
		setProperty(obj,param->name().substr(pos + 1),*param);
	}
    // Connect signals (direct children only: actions from sub-menus are already connected)
    QList<QAction*> list = qFindChildren<QAction*>(menu);
    for (int i = 0; i < list.size(); i++) {
	if (list[i]->isSeparator() || list[i]->parent() != menu)
	    continue;
	if (list[i]->isCheckable())
	    QtClient::connectObjects(list[i],SIGNAL(toggled(bool)),receiver,toggleSlot);
	else
	    QtClient::connectObjects(list[i],SIGNAL(triggered()),receiver,triggerSlot);
    }
    if (!TelEngine::null(aboutToShowSlot))
	QtClient::connectObjects(menu,SIGNAL(aboutToShow()),receiver,aboutToShowSlot);

    return menu;
}

// Wrapper for QObject::connect() used to put a debug mesage on failure
bool QtClient::connectObjects(QObject* sender, const char* signal,
    QObject* receiver, const char* slot)
{
    if (!(sender && signal && *signal && receiver && slot && *slot))
	return false;
    bool ok = QObject::connect(sender,signal,receiver,slot);
    if (ok)
	DDebug(QtDriver::self(),DebugAll,
	    "Connected sender=%s signal=%s to receiver=%s slot=%s",
	    YQT_OBJECT_NAME(sender),signal,YQT_OBJECT_NAME(receiver),slot);
    else
	Debug(QtDriver::self(),DebugWarn,
	    "Failed to connect sender=%s signal=%s to receiver=%s slot=%s",
	    YQT_OBJECT_NAME(sender),signal,YQT_OBJECT_NAME(receiver),slot);
    return ok;
}

// Insert a widget into another one replacing any existing children
bool QtClient::setWidget(QWidget* parent, QWidget* child)
{
    if (!(parent && child))
	return false;
    QVBoxLayout* layout = new QVBoxLayout;
    layout->setSpacing(0);
    String margins;
    QtClient::getProperty(parent,"_yate_layout_margins",margins);
    if (!margins)
	layout->setContentsMargins(0,0,0,0);
    else {
	QList<int> m = buildIntList(margins,4);
	layout->setContentsMargins(m[0],m[1],m[2],m[3]);
    }
    layout->addWidget(child);
    QLayout* l = parent->layout();
    if (l)
	delete l;
    parent->setLayout(layout);
    return true;
}

// Set an object's image property from image file
bool QtClient::setImage(QObject* obj, const String& img, bool fit)
{
    if (!obj)
	return false;
    QPixmap pixmap(setUtf8(img));
    return setImage(obj,pixmap,fit);
}

// Set an object's image property from raw data.
bool QtClient::setImage(QObject* obj, const DataBlock& data, const String& format, bool fit)
{
    if (!obj)
	return false;
    QPixmap pixmap;
    String f = format;
    f.startSkip("image/",false);
    if (!pixmap.loadFromData((const uchar*)data.data(),data.length(),f))
	return false;
    return setImage(obj,pixmap,fit);
}

// Set an object's image property from QPixmap
bool QtClient::setImage(QObject* obj, const QPixmap& img, bool fit)
{
    if (!obj)
	return false;
    if (obj->isWidgetType()) {
	QLabel* l = qobject_cast<QLabel*>(obj);
	if (l) {
	    if (fit && !l->hasScaledContents() &&
		(img.width() > l->width() || img.height() > l->height())) {
		QPixmap tmp;
		if (l->width() <= l->height())
		    tmp = img.scaledToWidth(l->width());
		else
		    tmp = img.scaledToHeight(l->height());
		l->setPixmap(tmp);
	    }
	    else
		l->setPixmap(img);
	}
	else {
	    QAbstractButton* b = qobject_cast<QAbstractButton*>(obj);
	    if (b)
		b->setIcon(img);
	    else {
		QMenu* m = qobject_cast<QMenu*>(obj);
		if (m)
		    m->setIcon(img);
		else
		    return false;
	    }
	}
	return true;
    }
    QAction* a = qobject_cast<QAction*>(obj);
    if (a) {
	a->setIcon(img);
	return true;
    }
    return false;
}

// Update a toggable object's image from properties
void QtClient::updateToggleImage(QObject* obj)
{
    QtWidget w(obj);
    QAbstractButton* b = 0;
    if (w.inherits(QtWidget::AbstractButton))
	b = w.abstractButton();
    if (!(b && b->isCheckable()))
	return;
    String icon;
    bool set = false;
    if (b->isChecked())
	set = QtClient::getProperty(w,"_yate_pressed_icon",icon);
    else
	set = QtClient::getProperty(w,"_yate_normal_icon",icon);
    if (set)
        QtClient::setImage(obj,Client::s_skinPath + icon);
}

// Update an object's image from properties on mouse events
void QtClient::updateImageFromMouse(QObject* obj, bool inOut, bool on)
{
    QtWidget w(obj);
    QAbstractButton* b = 0;
    if (w.inherits(QtWidget::AbstractButton))
	b = w.abstractButton();
    if (!b)
	return;
    if (!b->isEnabled())
	return;
    String icon;
    bool set = false;
    if (inOut) {
	if (on)
	    set = QtClient::getProperty(obj,"_yate_hover_icon",icon);
	else {
	    if (b->isCheckable() && b->isChecked())
		set = QtClient::getProperty(obj,"_yate_pressed_icon",icon);
	    set = set || QtClient::getProperty(obj,"_yate_normal_icon",icon);
	}
    }
    else {
	if (on) {
	    if (!b->isCheckable())
		set = QtClient::getProperty(obj,"_yate_pressed_icon",icon);
	}
	else {
	    set = QtClient::getProperty(obj,"_yate_hover_icon",icon);
	    if (!set && b->isCheckable() && b->isChecked())
		set = QtClient::getProperty(obj,"_yate_pressed_icon",icon);
	    set = set || QtClient::getProperty(obj,"_yate_normal_icon",icon);
	}
    }
    if (set)
	QtClient::setImage(obj,Client::s_skinPath + icon);
}

// Process a key press event. Retrieve an action associated with the key
bool QtClient::filterKeyEvent(QObject* obj, QKeyEvent* event, String& action,
    bool& filter, QObject* parent)
{
    static int mask = Qt::SHIFT | Qt::CTRL | Qt::ALT;
    if (!(obj && event))
	return false;
    // Try to match key and modifiers
    QKeySequence ks(event->key());
    String prop;
    getUtf8(prop,ks.toString());
    prop = "dynamicAction" + prop;
    // Get modifiers from property and check them against event
    QVariant v = obj->property(prop + "Modifiers");
    int tmp = 0;
    if (v.type() == QVariant::String) {
	QKeySequence ks(v.toString());
	for (unsigned int i = 0; i < ks.count(); i++)
	    tmp |= ks[i];
    }
    if (tmp != (mask & event->modifiers()))
	return false;
    // We matched the key and modifiers
    // Set filter flag
    filter = getBoolProperty(obj,prop + "Filter");
    // Retrieve the action
    getProperty(obj,prop,action);
    if (!action)
	return true;
    if (!parent)
	return true;
    parent = qFindChild<QObject*>(parent,setUtf8(action));
    if (!parent)
	return true;
    // Avoid notifying a disabled action
    bool ok = true;
    if (parent->isWidgetType())
	ok = (qobject_cast<QWidget*>(parent))->isEnabled();
    else {
	QAction* a = qobject_cast<QAction*>(parent);
	ok = !a || a->isEnabled();
    }
    if (!ok)
	action.clear();
    return true;
}

// Safely delete a QObject (reset its parent, calls it's deleteLater() method)
void QtClient::deleteLater(QObject* obj)
{
    if (!obj)
	return;
    obj->disconnect();
    if (obj->isWidgetType())
	(static_cast<QWidget*>(obj))->setParent(0);
    else
	obj->setParent(0);
    obj->deleteLater();
}

// Retrieve unavailable space position (if any) in the screen containing a given widget.
QDesktopWidget* QtClient::getScreenUnavailPos(QWidget* w, int& pos)
{
    if (!w)
	return 0;
    QDesktopWidget* d = QApplication::desktop();
    if (!d)
	return 0;
    pos = PosNone;
    QRect rScreen = d->screenGeometry(w);
    QRect rClient = d->availableGeometry(w);
    int dx = rClient.x() - rScreen.x();
    if (dx > 0)
	pos |= PosLeft;
    int dy = rClient.y() - rScreen.y();
    if (dy > 0)
	pos |= PosTop;
    int dw = rScreen.width() - rClient.width();
    if (dw > 0 && (!dx || (dx > 0 && dw > dx)))
	pos |= PosRight;
    int dh = rScreen.height() - rClient.height();
    if (dh > 0 && (!dy || (dy > 0 && dh > dy)))
	pos |= PosBottom;
    return d;
}

// Move a window to a specified position
void QtClient::moveWindow(QtWindow* w, int pos)
{
    if (!w)
	return;
    QDesktopWidget* d = QApplication::desktop();
    if (!d)
	return;
    QRect r = d->availableGeometry(w);
    int x = r.x();
    int y = r.y();
    QSize sz = w->frameSize();
    if (pos == CornerBottomRight) {
        if (r.width() > sz.width())
	    x += r.width() - sz.width();
	if (r.height() > sz.height())
	    y += r.height() - sz.height();
    }
    else if (pos == CornerTopRight) {
        if (r.width() > sz.width())
	    x += r.width() - sz.width();
    }
    else if (pos == CornerBottomLeft) {
	if (r.height() > sz.height())
	    y += r.height() - sz.height();
    }
    else if (pos != CornerTopLeft)
	return;
    w->move(x,y);
}

// Build a QStringList from a list of strings
QStringList QtClient::str2list(const String& str, char sep, bool emptyOk)
{
    QStringList l;
    if (!str)
	return l;
    ObjList* list = str.split(sep,emptyOk);
    for (ObjList* o = list->skipNull(); o; o = o->skipNext())
	l.append(setUtf8(static_cast<String*>(o->get())->c_str()));
    TelEngine::destruct(list);
    return l;
}

// Split a string. Returns a list of int values
QList<int> QtClient::str2IntList(const String& str, int defVal, bool emptyOk)
{
    QList<int> list;
    ObjList* l = str.split(',',emptyOk);
    for (ObjList* o = l->skipNull(); o; o = o->skipNext())
	list.append(o->get()->toString().toInteger(defVal));
    TelEngine::destruct(l);
    return list;
}

// Build a comma separated list of integers
void QtClient::intList2str(String& str, QList<int> list)
{
    for (int i = 0; i < list.size(); i++)
	str.append(String(list[i]),",");
}

// Get sorting from string
int QtClient::str2sort(const String& str, int defVal)
{
    return lookup(str,s_sorting,defVal);
}

// Apply a comma separated list of window flags to a widget
void QtClient::applyWindowFlags(QWidget* w, const String& value)
{
    if (!w)
	return;
    // Set window flags from enclosed widget:
    //  custom window title/border/sysmenu config
    ObjList* f = value.split(',',false);
    int flags = Qt::CustomizeWindowHint | w->windowFlags();
    // Clear settable flags
    TokenDict* dict = s_windowFlags;
    for (int i = 0; dict[i].token; i++)
	flags &= ~dict[i].value;
    // Set flags
    for (ObjList* o = f->skipNull(); o; o = o->skipNext())
	flags |= lookup(o->get()->toString(),s_windowFlags,0);
    TelEngine::destruct(f);
    w->setWindowFlags((Qt::WindowFlags)flags);
}

// Build a QT Alignment mask from a comma separated list of flags
int QtClient::str2align(const String& flags, int initVal)
{
    ObjList* list = flags.split(',',false);
    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	int val = ::lookup((static_cast<String*>(o->get()))->c_str(),s_qAlign);
	if (0 != (val & Qt::AlignHorizontal_Mask))
	    initVal &= ~Qt::AlignHorizontal_Mask;
	if (0 != (val & Qt::AlignVertical_Mask))
	    initVal &= ~Qt::AlignVertical_Mask;
	initVal |= val;
    }
    TelEngine::destruct(list);
    return initVal;
}

// Retrieve QT selection mode from a string value
QAbstractItemView::SelectionMode QtClient::str2selmode(const String& value,
    QAbstractItemView::SelectionMode defVal)
{
    if (!value)
	return defVal;
    if (value == YSTRING("none"))
	return QAbstractItemView::NoSelection;
    if (value == YSTRING("single"))
	return QAbstractItemView::SingleSelection;
    if (value == YSTRING("multi"))
	return QAbstractItemView::MultiSelection;
    if (value == YSTRING("extended"))
	return QAbstractItemView::ExtendedSelection;
    if (value == YSTRING("contiguous"))
	return QAbstractItemView::ContiguousSelection;
    return defVal;
}

// Retrieve QT edit triggers from a string value
QAbstractItemView::EditTriggers QtClient::str2editTriggers(const String& value,
    QAbstractItemView::EditTrigger defVal)
{
    return (QAbstractItemView::EditTriggers)Client::decodeFlags(s_qEditTriggers,value,defVal);
}

// Send an event to an object's child
bool QtClient::sendEvent(QEvent& e, QObject* parent, const QString& name)
{
    if (!(parent && e.isAccepted()))
	return false;
    QObject* child = qFindChild<QObject*>(parent,name);
    if (!child)
	return false;
    e.setAccepted(false);
    bool ok = QCoreApplication::sendEvent(child,&e);
    if (!ok)
	e.setAccepted(true);
    return ok;
}

// Retrieve a pixmap from global application cache.
// Load and add it to the cache if not found
bool QtClient::getPixmapFromCache(QPixmap& pixmap, const QString& file)
{
    if (file.isEmpty())
	return false;
    QPixmap* cached = QPixmapCache::find(file);
    if (cached) {
	pixmap = *cached;
	return true;
    }
    if (!pixmap.load(file))
	return false;
#ifdef XDEBUG
    String f;
    getUtf8(f,file);
    Debug(ClientDriver::self(),DebugAll,"Loaded '%s' in pixmap cache",f.c_str());
#endif
    QPixmapCache::insert(file,pixmap);
    return true;
}

// Update application style sheet from config
// Build style sheet from files:
// stylesheet.css
// stylesheet_stylename.css
// stylesheet_osname.css
// stylesheet_osname_stylename.css
void QtClient::updateAppStyleSheet()
{
    if (!qApp) {
	Debug(ClientDriver::self(),DebugWarn,"Update app stylesheet called without app");
	return;
    }
    String shf = Engine::config().getValue("client","stylesheet_file","stylesheet.css");
    if (!shf)
	return;
    QString sh;
    if (!appendStyleSheet(sh,shf))
	return;
    String styleName;
    QStyle* style = qApp->style();
    const QMetaObject* meta = style ? style->metaObject() : 0;
    if (meta) {
	styleName = s_qtStyles.getValue(meta->className());
	if (!styleName)
	    styleName = meta->className();
    }
    if (styleName)
	appendStyleSheet(sh,shf,styleName);
    String osname;
    osname << "os" << PLATFORM_LOWERCASE_NAME;
    appendStyleSheet(sh,shf,osname);
    if (styleName)
	appendStyleSheet(sh,shf,osname,styleName);
    qApp->setStyleSheet(sh);
}

// Set widget attributes from list
void QtClient::setWidgetAttributes(QWidget* w, const String& attrs)
{
    if (!(w && attrs))
	return;
    ObjList* list = attrs.split(',',false);
    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	const String& attr = *static_cast<String*>(o->get());
	bool on = (attr[0] != '!');
	const char* name = attr.c_str();
	int val = lookup(on ? name : name + 1,s_widgetAttributes);
	if (val)
	    w->setAttribute((Qt::WidgetAttribute)val,on);
    }
    TelEngine::destruct(list);
}

// Adjust widget height
void QtClient::setWidgetHeight(QWidget* w, const String& height)
{
    if (!w)
	return;
    int h = 0;
    if (height.isBoolean()) {
	h = QtClient::getIntProperty(w,"_yate_height_delta",-1);
	if (h > 0) {
	    if (height.toBoolean())
		h += w->height();
	    else if (h < w->height())
		h = w->height() - h;
	    else
		h = 0;
	}
    }
    else
	h = height.toInteger();
    if (h < 0)
	return;
    QSizePolicy sp = w->sizePolicy();
    sp.setVerticalPolicy(QSizePolicy::Fixed);
    w->setSizePolicy(sp);
    w->setMinimumHeight(h);
    w->setMaximumHeight(h);
}

// Build a busy widget child for a given widget
QWidget* QtClient::buildBusy(QWidget* parent, QWidget* target, const String& ui,
    const NamedList& params)
{
    QtBusyWidget* w = new QtBusyWidget(parent);
    w->init(ui,params,target);
    return w;
}

// Load a movie
QMovie* QtClient::loadMovie(const char* file, QObject* parent, const char* path)
{
    static NamedList s_failed("");

    if (TelEngine::null(file))
	return 0;
    String tmp = path;
    if (!path)
	tmp = Client::s_skinPath;
    else if (tmp && !tmp.endsWith(Engine::pathSeparator()))
	tmp << Engine::pathSeparator();
    tmp << file;
    QMovie* m = new QMovie(setUtf8(tmp),QByteArray(),parent);
    NamedString* ns = s_failed.getParam(tmp);
    if (m->isValid()) {
	if (ns)
	    s_failed.clearParam(ns);
	return m;
    }
    if (!ns) {
	s_failed.addParam(tmp,"");
	String error;
	error << "Failed to load movie '" << tmp << "'";
	Debug(QtDriver::self(),DebugNote,"%s",error.c_str());
	if (self())
	    self()->addToLog(error);
    }
    delete m;
    return 0;
}

// Fill a list from URL parameters
void QtClient::fillUrlParams(const QUrl& url, NamedList& list, QString* path,
    bool pathToList)
{
    safeGetUtf8(list,"protocol",url.scheme());
    safeGetUtf8(list,"host",url.host());
    if (url.port() >= 0)
	list.addParam("port",String(url.port()));
    safeGetUtf8(list,"username",url.userName());
    safeGetUtf8(list,"password",url.password());
    QString tmp;
    if (!path) {
	tmp = url.path();
	path = &tmp;
    }
    if (pathToList)
	list.assign(path->toUtf8().constData());
    else
	safeGetUtf8(list,"path",*path);
    QList<QPair<QString, QString> > items = url.queryItems();
    for (int i = 0; i < items.size(); i++)
	list.addParam(items[i].first.toUtf8().constData(),items[i].second.toUtf8().constData());
}

// Dump MIME data for debug purposes
void QtClient::dumpMime(String& buf, const QMimeData* m)
{
    static const char* indent = "\r\n    ";
    if (!m)
	return;
    QStringList fmts = m->formats();
    if (fmts.size() > 0) {
	buf.append("FORMATS:","\r\n") << indent;
	QString s = fmts.join(indent);
	buf << s.toUtf8().constData();
    }
    if (m->html().length() > 0)
	buf.append("HTML: ","\r\n") << m->html().toUtf8().constData();
    if (m->text().length() > 0)
	buf.append("TEXT: ","\r\n") << m->text().toUtf8().constData();
    QList<QUrl> urls = m->urls();
    if (urls.size() > 0) {
	buf.append("URLS:","\r\n");
	for (int i = 0; i < urls.size(); i++)
	    buf << indent << urls[i].toString().toUtf8().constData();
    }
}


/**
 * QtDriver
 */
QtDriver::QtDriver(bool buildClientThread)
    : m_init(false), m_clientThread(buildClientThread)
{
    qInstallMsgHandler(qtMsgHandler);
}

QtDriver::~QtDriver()
{
    qInstallMsgHandler(0);
}

void QtDriver::initialize()
{
    Output("Initializing module Qt4 client");
    s_device = Engine::config().getValue("client","device",DEFAULT_DEVICE);
    if (!QtClient::self()) {
	debugCopy();
	QtClient::setSelf(new QtClient);
	if (m_clientThread)
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
#define SET_NAME(n) { m_name = n; setObjectName(QtClient::setUtf8(m_name)); }
    switch (type) {
	case Timer:
	    SET_NAME("qtClientTimerProxy");
	    {
		QTimer* timer = new QTimer(this);
		timer->setObjectName("qtClientIdleTimer");
		QtClient::connectObjects(timer,SIGNAL(timeout()),this,SLOT(timerTick()));
		timer->start(0);
	    }
	    break;
	case AllHidden:
	    SET_NAME("qtClientAllHidden");
	    if (app)
		QtClient::connectObjects(app,SIGNAL(lastWindowClosed()),this,SLOT(allHidden()));
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
    Thread::idle();
}

void QtEventProxy::allHidden()
{
    if (Client::self())
	Client::self()->allHidden();
}


//
// QtUrlBuilder
//
QtUrlBuilder::QtUrlBuilder(QObject* parent, const String& format,
    const String& queryParams)
    : QObject(parent),
    m_format(format),
    m_queryParams(0)
{
    if (queryParams) {
	m_queryParams = queryParams.split(',',false);
	if (!m_queryParams->skipNull())
	    TelEngine::destruct(m_queryParams);
    }
}

QtUrlBuilder::~QtUrlBuilder()
{
    TelEngine::destruct(m_queryParams);
}

// Build URL
QUrl QtUrlBuilder::build(const NamedList& params) const
{
    String tmp;
    if (m_format) {
	tmp = m_format;
	params.replaceParams(tmp);
    }
    QUrl url(QtClient::setUtf8(tmp));
    if (m_queryParams) {
	NamedIterator iter(params);
	for (const NamedString* ns = 0; 0 != (ns = iter.get());)
	    if (m_queryParams->find(ns->name()))
		url.addQueryItem(QtClient::setUtf8(ns->name()),QtClient::setUtf8(*ns));
    }
    return url;
}


/*
 * QtUIWidget
 */
// Retrieve item type definition from [type:]value. Create it if not found
QtUIWidgetItemProps* QtUIWidget::getItemProps(QString& in, String& value)
{
    String type;
    int pos = in.indexOf(':');
    if (pos >= 0) {
	QtClient::getUtf8(type,in.left(pos));
	QtClient::getUtf8(value,in.right(in.length() - pos - 1));
    }
    else
	QtClient::getUtf8(value,in);
    QtUIWidgetItemProps* p = QtUIWidget::getItemProps(type);
    if (!p) {
	p = new QtUIWidgetItemProps(type);
	m_itemProps.append(p);
    }
    DDebug(ClientDriver::self(),DebugAll,"QtUIWidget(%s) getItemProps(%s,%s) got (%p) ui=%s [%p]",
	name().c_str(),in.toUtf8().constData(),value.c_str(),p,p->m_ui.c_str(),this);
    return p;
}

// Set widget's parameters.
// Handle an 'applyall' parameter carrying a NamedList to apply to all items
bool QtUIWidget::setParams(const NamedList& params)
{
    bool ok = false;
    NamedIterator iter(params);
    for (const NamedString* ns = 0; 0 != (ns = iter.get());) {
	if (ns->name() == YSTRING("applyall")) {
	    const NamedList* list = YOBJECT(NamedList,ns);
	    if (list) {
		ok = true;
		applyAllParams(*list);
	    }
	}
	else if (ns->name().startsWith("beginedit:"))
	    beginEdit(ns->name().substr(10),ns);
    }
    return ok;
}

// Apply a list of parameters to all container items
void QtUIWidget::applyAllParams(const NamedList& params)
{
    QList<QObject*> list = getContainerItems();
    for (int i = 0; i < list.size(); i++)
	setParams(list[i],params);
}

// Find an item widget by id
QWidget* QtUIWidget::findItem(const String& id)
{
    QString item = QtClient::setUtf8(id);
    QList<QObject*> list = getContainerItems();
    for (int i = 0; i < list.size(); i++) {
	if (!list[i]->isWidgetType())
	    continue;
	String item;
	getListItemIdProp(list[i],item);
	if (id == item)
	    return static_cast<QWidget*>(list[i]);
    }
    return 0;
}

// Retrieve the object identity from '_yate_identity' property or name
// Retrieve the object item from '_yate_widgetlistitem' property.
// Set 'identity' to object_identity[:item_name]
void QtUIWidget::getIdentity(QObject* obj, String& identity)
{
    if (!obj)
	return;
    String ident;
    QtClient::getIdentity(obj,ident);
    if (!ident)
	return;
    String item;
    getListItemProp(obj,item);
    identity.append(ident,":");
    identity.append(item,":");
}

// Update a widget and children from a list a parameters
bool QtUIWidget::setParams(QObject* parent, const NamedList& params)
{
    static const String s_property = "property";
    static const String s_active = "active";
    static const String s_image = "image";
    static const String s_show = "show";
    static const String s_display = "display";
    static const String s_check = "check";
    static const String s_select = "select";
    static const String s_addlines = "addlines";
    static const String s_setrichtext = "setrichtext";
    static const String s_updatetablerows = "updatetablerows";
    static const String s_cleartable = "cleartable";
    static const String s_rawimage = "rawimage";
    static const String s_setparams = "setparams";
    static const String s_setmenu = "setmenu";
    static const String s_height = "height";

    if (!parent)
	return false;
    QtWindow* wnd = QtClient::parentWindow(parent);
    if (!wnd)
	return false;
#ifdef DEBUG
    String tmp;
    params.dump(tmp," ");
    Debug(ClientDriver::self(),DebugAll,"QtUIWidget(%s)::setParams(%p,%s) %s",
	name().c_str(),parent,YQT_OBJECT_NAME(parent),tmp.c_str());
#endif
    String pName(YQT_OBJECT_NAME(parent));
    bool ok = true;
    unsigned int n = params.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = params.getParam(i);
	if (!ns)
	    continue;
	XDebug(ClientDriver::self(),DebugInfo,"QtUIWidget(%s)::setParams() %s=%s",
	    name().c_str(),ns->name().c_str(),ns->c_str());
	String buf;
	int pos = ns->name().find(':');
	if (pos < 0) {
	    if (ns->name() != s_setmenu)
		ok = wnd->setText(buildChildName(buf,pName,ns->name()),*ns,false) && ok;
	    else
		buildWidgetItemMenu(qobject_cast<QWidget*>(parent),YOBJECT(NamedList,ns));
	    continue;
	}
	String n(ns->name().substr(0,pos));
	String cName = ns->name().substr(pos + 1);
	if (n == s_property) {
	    // Handle property[:child]:property_name
	    int pos = cName.find(':');
	    if (pos >= 0) {
		QString tmp = buildQChildName(pName,cName.substr(0,pos));
		QObject* c = qFindChild<QObject*>(parent,tmp);
		ok = c && QtClient::setProperty(c,cName.substr(pos + 1),*ns) && ok;
	    }
	    else
		ok = QtClient::setProperty(parent,cName,*ns) && ok;
	}
	else if (n == s_active)
	    ok = wnd->setActive(buildChildName(buf,pName,cName),ns->toBoolean()) && ok;
	else if (n == s_image)
	    ok = wnd->setImage(buildChildName(buf,pName,cName),*ns) && ok;
	else if (n == s_show || n == s_display)
	    ok = wnd->setShow(buildChildName(buf,pName,cName),ns->toBoolean()) && ok;
	else if (n == s_check)
	    ok = wnd->setCheck(buildChildName(buf,pName,cName),ns->toBoolean()) && ok;
	else if (n == s_select)
	    ok = wnd->setSelect(buildChildName(buf,pName,cName),*ns) && ok;
	if (n == s_setparams) {
	    NamedList* p = YOBJECT(NamedList,ns);
	    if (!p)
		continue;
	    QtWidget w(parent,buildChildName(buf,pName,cName));
	    UIWidget* uiw = w.uiWidget();
	    ok = uiw && uiw->setParams(*p) && ok;
	}
	else if (n == s_addlines) {
	    NamedList* p = YOBJECT(NamedList,ns);
	    if (p)
		ok = wnd->addLines(buildChildName(buf,pName,cName),p,0,ns->toBoolean()) && ok;
	}
	else if (n == s_setrichtext)
	    ok = wnd->setText(buildChildName(buf,pName,cName),*ns,true) && ok;
	else if (n == s_updatetablerows) {
	    NamedList* p = YOBJECT(NamedList,ns);
	    if (p)
		ok = wnd->updateTableRows(buildChildName(buf,pName,cName),p,ns->toBoolean()) && ok;
	}
	else if (n == s_cleartable)
	    ok = wnd->clearTable(buildChildName(buf,pName,cName)) && ok;
	else if (n == s_rawimage) {
	    DataBlock* data = YOBJECT(DataBlock,ns);
	    if (data) {
		QString tmp = buildQChildName(pName,cName.substr(0,pos));
		QObject* c = qFindChild<QObject*>(parent,tmp);
		ok = c && QtClient::setImage(c,*data,*ns) && ok;
	    }
	}
	else if (n == s_setmenu)
	    buildWidgetItemMenu(qobject_cast<QWidget*>(parent),YOBJECT(NamedList,ns),cName);
	else if (n == s_height) {
	    QString tmp = buildQChildName(pName,cName);
	    QWidget* w = qFindChild<QWidget*>(qobject_cast<QWidget*>(parent),tmp);
	    QtClient::setWidgetHeight(w,*ns);
	}
	else
	    ok = wnd->setText(buildChildName(buf,pName,ns->name()),*ns,false) && ok;
    }
    // Set item parameters
    NamedString* yparams = params.getParam(YSTRING("_yate_itemparams"));
    if (!TelEngine::null(yparams)) {
	QVariant var = parent->property(yparams->name().c_str());
	if (var.type() == QVariant::Invalid || var.type() == QVariant::StringList) {
	    QStringList list;
	    if (var.type() == QVariant::StringList)
		list = var.toStringList();
	    NamedList tmp("");
	    tmp.copyParams(params,*yparams);
	    QtClient::copyParams(list,tmp);
	    parent->setProperty(yparams->name().c_str(),QVariant(list));
	}
	else
	    ok = false;
    }
    return ok;
}

// Get an item object's parameters
bool QtUIWidget::getParams(QObject* parent, NamedList& params)
{
    static const String s_property = "property";
    static const String s_getcheck = "getcheck";
    static const String s_getselect = "getselect";
    static const String s_getrichtext = "getrichtext";

    if (!parent)
	return false;
    QtWindow* wnd = QtClient::parentWindow(parent);
    if (!wnd)
	return false;
    DDebug(ClientDriver::self(),DebugAll,"QtUIWidget(%s)::getParams(%p,%s)",
	name().c_str(),parent,YQT_OBJECT_NAME(parent));
    String pName;
    QtClient::getUtf8(pName,parent->objectName());
    bool ok = true;
    unsigned int n = params.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = params.getParam(i);
	if (!ns)
	    continue;
	String buf;
	int pos = ns->name().find(':');
	if (pos < 0) {
	    ok = wnd->getText(buildChildName(buf,pName,ns->name()),*ns,false) && ok;
	    continue;
	}
	String n(ns->name().substr(0,pos));
	String cName = ns->name().substr(pos + 1);
	if (n == s_property) {
	    // Handle property[:child]:property_name
	    int pos = cName.find(':');
	    if (pos >= 0) {
		QString tmp = buildQChildName(pName,cName.substr(0,pos));
		QObject* c = qFindChild<QObject*>(parent,tmp);
		ok = c && QtClient::getProperty(c,cName.substr(pos + 1),*ns) && ok;
	    }
	    else
		ok = QtClient::getProperty(parent,cName,*ns) && ok;
	}
	else if (n == s_getselect)
	    ok = wnd->getSelect(buildChildName(buf,pName,cName),*ns) && ok;
	else if (n == s_getcheck) {
	    bool on = false;
	    ok = wnd->getCheck(buildChildName(buf,pName,cName),on) && ok;
	    *ns = String::boolText(on);
	}
	else if (n == s_getrichtext)
	    ok = wnd->getText(buildChildName(buf,pName,cName),*ns,true) && ok;
	else
	    ok = wnd->getText(buildChildName(buf,pName,ns->name()),*ns,false) && ok;
    }
    // Get item parameters
    QtClient::getProperty(parent,"_yate_itemparams",params);
    return ok;
}

// Show or hide control busy state
bool QtUIWidget::setBusy(bool on)
{
    QObject* o = getQObject();
    QWidget* w = (o && o->isWidgetType()) ? static_cast<QWidget*>(o) : 0;
    return w && QtBusyWidget::showBusyChild(w,on);
}

// Apply properties for QAbstractItemView descendents
void QtUIWidget::applyItemViewProps(const NamedList& params)
{
    static const String s_selMode = "_yate_selection_mode";
    static const String s_editTriggers = "_yate_edit_triggers";

    QObject* obj = getQObject();
    QAbstractItemView* av = qobject_cast<QAbstractItemView*>(obj);
    if (!av)
	return;
    NamedIterator iter(params);
    for (const NamedString* ns = 0; 0 != (ns = iter.get());) {
	if (ns->name() == s_selMode)
	    av->setSelectionMode(QtClient::str2selmode(*ns));
	else if (ns->name() == s_editTriggers)
	    av->setEditTriggers(QtClient::str2editTriggers(*ns));
    }
}

// Begin item edit. The default behaviour start edit for QAbstractItemView descendants
bool QtUIWidget::beginEdit(const String& item, const String* what)
{
    QObject* obj = getQObject();
    QAbstractItemView* av = qobject_cast<QAbstractItemView*>(obj);
    if (!av)
	return false;
    QModelIndex idx = modelIndex(item,what);
    if (!idx.isValid())
	return false;
    av->setCurrentIndex(idx);
    av->edit(idx);
    return true;
}

// Build item widget menu
QMenu* QtUIWidget::buildWidgetItemMenu(QWidget* w, const NamedList* params,
    const String& child, bool set)
{
    if (!(w && params))
	return 0;
    QWidget* parent = w;
    // Retrieve the item owner
    QWidget* pItem = 0;
    String item;
    getListItemIdProp(w,item);
    if (item)
	pItem = findItem(item);
    else {
	getListItemProp(w,item);
	pItem = item ? findItem(item) : 0;
    }
    XDebug(ClientDriver::self(),DebugAll,
	"QtUIWidget(%s)::buildMenu() widget=%s item=%s [%p]",
	this->name().c_str(),YQT_OBJECT_NAME(w),item.c_str(),this);
    String pName(YQT_OBJECT_NAME(w));
    const String& owner = (*params)[YSTRING("owner")];
    if (owner && owner != item) {
	QString tmp = buildQChildName(pName,owner);
	parent = qFindChild<QWidget*>(w,tmp);
	if (!parent) {
	    Debug(QtDriver::self(),DebugNote,
		"QtUIWidget(%s) buildMenu() owner '%s' not found [%p]",
		name().c_str(),owner.c_str(),this);
	    return 0;
	}
    }
    QWidget* target = parent;
    String t = child ? child : (*params)[YSTRING("target")];
    if (t) {
	QString tmp = buildQChildName(pName,t);
	target = qFindChild<QWidget*>(w,tmp);
	if (!target) {
	    Debug(QtDriver::self(),DebugNote,
		"QtUIWidget(%s) buildMenu() target '%s' not found [%p]",
		name().c_str(),t.c_str(),this);
	    return 0;
	}
    }
    QString menuName = buildQChildName(pName,t + "_menu");
    // Remove existing menu
    QMenu* menu = qFindChild<QMenu*>(parent,menuName);
    if (menu) {
	delete menu;
	menu = 0;
    }
    // Build the menu
    QObject* thisObj = getQObject();
    if (!thisObj)
	return 0;
    String actionSlot;
    String toggleSlot;
    String selectSlot;
    getSlots(actionSlot,toggleSlot,selectSlot);
    if (!(actionSlot || toggleSlot))
	return 0;
    bool addActions = set && target->contextMenuPolicy() == Qt::ActionsContextMenu;
    unsigned int n = params->length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* param = params->getParam(i);
	if (!(param && param->name().startsWith("item:")))
	    continue;
	if (!menu)
	    menu = new QMenu(QtClient::setUtf8(params->getValue(YSTRING("title"))),parent);
	NamedList* p = YOBJECT(NamedList,param);
	if (p)  {
	    QMenu* subMenu = QtClient::buildMenu(*p,
		*param ? param->c_str() : p->getValue(YSTRING("title"),*p),
		thisObj,actionSlot,toggleSlot,menu);
	    if (subMenu) {
		menu->addMenu(subMenu);
		if (addActions)
		    target->addAction(subMenu->menuAction());
	    }
	    continue;
	}
	QAction* a = 0;
	String name = param->name().substr(5);
	if (*param) {
	    a = menu->addAction(QtClient::setUtf8(*param));
	    a->setObjectName(buildQChildName(pName,name));
	    a->setParent(menu);
	    QtClient::setImage(a,(*params)["image:" + name]);
	}
	else if (!name) {
	    a = menu->addSeparator();
	    a->setParent(menu);
	}
	else if (pItem) {
	    // Check if the action is already there
	    QString aName = buildQChildName(pItem->objectName(),QtClient::setUtf8(name));
	    a = qFindChild<QAction*>(pItem,aName);
	    if (a)
		menu->addAction(a);
	}
	if (a) {
	    if (addActions)
		target->addAction(a);
	}
	else
	    Debug(ClientDriver::self(),DebugNote,
		"QtUIWidget(%s)::buildMenu() action '%s' not found for item=%s [%p]",
		this->name().c_str(),name.c_str(),item.c_str(),this);
    }
    if (!menu)
	return 0;
    // Set name
    menu->setObjectName(menuName);
    // Apply properties
    // Format: property:object_name:property_name=value
    if (parent)
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* param = params->getParam(i);
	    if (!(param && param->name().startsWith("property:")))
		continue;
	    int pos = param->name().find(':',9);
	    if (pos < 9)
		continue;
	    QString n = buildQChildName(pName,param->name().substr(9,pos - 9));
	    QObject* obj = qFindChild<QObject*>(parent,n);
	    if (obj)
		QtClient::setProperty(obj,param->name().substr(pos + 1),*param);
	}
    // Connect signals (direct children only: actions from sub-menus are already connected)
    QList<QAction*> list = qFindChildren<QAction*>(menu);
    for (int i = 0; i < list.size(); i++) {
	if (list[i]->isSeparator() || list[i]->parent() != menu)
	    continue;
	if (list[i]->isCheckable())
	    QtClient::connectObjects(list[i],SIGNAL(toggled(bool)),thisObj,toggleSlot);
	else
	    QtClient::connectObjects(list[i],SIGNAL(triggered()),thisObj,actionSlot);
    }
    if (addActions)
	return menu;
    QMenu* mOwner = qobject_cast<QMenu*>(target);
    if (mOwner)
	mOwner->insertMenu(0,menu);
    else {
	QToolButton* tb = qobject_cast<QToolButton*>(target);
	if (tb)
	    tb->setMenu(menu);
	else {
	    QPushButton* pb = qobject_cast<QPushButton*>(target);
	    if (pb)
		pb->setMenu(menu);
	    else if (!QtClient::setProperty(target,s_propContextMenu,params))
		target->addAction(menu->menuAction());
	}
    }
    return menu;
}

// Build a container child name from parent property
bool QtUIWidget::buildQChildNameProp(QString& dest, QObject* parent, const char* prop)
{
    if (!(parent && prop))
	return false;
    QVariant var = parent->property(prop);
    if (!var.isValid() || var.toString().size() <= 0)
	return false;
    dest = buildQChildName(parent->objectName(),var.toString());
    return true;
}

// Retrieve the top level QtUIWidget container parent of an object
QtUIWidget* QtUIWidget::container(QObject* obj)
{
    if (!obj)
	return 0;
    QtUIWidget* uiw = 0;
    while (0 != (obj = obj->parent())) {
	QtWidget w(obj);
	UIWidget* u = w.uiWidget();
	if (u)
	    uiw = static_cast<QtUIWidget*>(u);
    }
    return uiw;
}

// Utility used in QtUIWidget::initNavigation
static bool initNavAction(QObject* obj, const String& name, const String& actionSlot)
{
    if (!(obj && name))
	return false;
    QtWindow* wnd = QtClient::parentWindow(obj);
    QObject* child = qFindChild<QObject*>(wnd,QtClient::setUtf8(name));
    if (!child)
	return false;
    QAbstractButton* b = 0;
    QAction* a = 0;
    if (child->isWidgetType())
	b = qobject_cast<QAbstractButton*>(child);
    else
	a = qobject_cast<QAction*>(child);
    if (b || a) {
	if (b)
	    QtClient::connectObjects(b,SIGNAL(clicked()),obj,actionSlot);
	else
	    QtClient::connectObjects(a,SIGNAL(triggered()),obj,actionSlot);
    }
    return b || a;
}

// Initialize navigation controls
void QtUIWidget::initNavigation(const NamedList& params)
{
    String actionSlot;
    String toggleSlot;
    String selectSlot;
    getSlots(actionSlot,toggleSlot,selectSlot);
    QObject* qObj = getQObject();
    if (qObj && actionSlot) {
	m_prev = params.getValue(YSTRING("navigate_prev"));
	if (!initNavAction(qObj,m_prev,actionSlot))
	    m_prev = "";
	m_next = params.getValue(YSTRING("navigate_next"));
	if (!initNavAction(qObj,m_next,actionSlot))
	    m_next = "";
    }
    m_info = params.getValue(YSTRING("navigate_info"));
    m_infoFormat = params.getValue(YSTRING("navigate_info_format"));
    m_title = params.getValue(YSTRING("navigate_title"));
    updateNavigation();
}

// Update navigation controls
void QtUIWidget::updateNavigation()
{
    if (!(m_prev || m_next || m_info || m_title))
	return;
    QtWindow* wnd = QtClient::parentWindow(getQObject());
    if (!wnd)
	return;
    NamedList p("");
    int crt = currentItemIndex();
    if (crt < 0)
	crt = 0;
    else
	crt++;
    int n = itemCount();
    if (n < crt)
	n = crt;
    if (m_prev || m_next) {
	if (m_prev)
	    p.addParam("active:" + m_prev,String::boolText(crt > 1));
	if (m_next)
	    p.addParam("active:" + m_next,String::boolText(crt < n));
    }
    if (m_info) {
	String tmp = m_infoFormat;
	NamedList pp("");
	pp.addParam("index",String(crt));
	pp.addParam("count",String(n));
	pp.replaceParams(tmp);
	p.addParam(m_info,tmp);
    }
    if (m_title) {
	String crt;
	getSelect(crt);
	NamedList pp("");
	if (crt)
	    getTableRow(crt,&pp);
	p.addParam(m_title,pp[YSTRING("title")]);
    }
    wnd->setParams(p);
}

// Trigger a custom action from an item
bool QtUIWidget::triggerAction(const String& item, const String& action, QObject* sender,
    NamedList* params)
{
    if (!(Client::self() && action))
	return false;
    if (!sender)
	sender = getQObject();
    String s;
    getIdentity(sender,s);
    if (!s)
	return false;
    NamedList p("");
    if (!params)
	params = &p;
    params->addParam("item",item,false);
    params->addParam("widget",s);
    return QtClient::self()->action(QtClient::parentWindow(sender),action,params);
}

// Trigger a custom action from already built list params
bool QtUIWidget::triggerAction(const String& action, NamedList& params, QObject* sender)
{
    if (!(Client::self() && action))
	return false;
    if (!sender)
	sender = getQObject();
    String s;
    getIdentity(sender,s);
    if (!s)
	return false;
    params.setParam("widget",s);
    return QtClient::self()->action(QtClient::parentWindow(sender),action,&params);
}

// Handle a child's action
void QtUIWidget::onAction(QObject* sender)
{
    if (!Client::self())
	return;
    String s;
    getIdentity(sender,s);
    if (!s)
	return;
    int dir = 0;
    if (s == m_next)
	dir = 1;
    else if (s == m_prev)
	dir = -1;
    if (dir) {
	int crt = currentItemIndex();
	if (crt >= 0)
	    setSelectIndex(crt + dir);
	return;
    }
    DDebug(ClientDriver::self(),DebugAll,"QtUIWidget(%s) raising action %s",
	name().c_str(),s.c_str());
    Client::self()->action(QtClient::parentWindow(sender),s);
}

// Handle a child's toggle notification
void QtUIWidget::onToggle(QObject* sender, bool on)
{
    if (!Client::self())
	return;
    QtClient::updateToggleImage(sender);
    String s;
    getIdentity(sender,s);
    if (!s)
	return;
    DDebug(ClientDriver::self(),DebugAll,"QtUIWidget(%s) raising toggle %s",
	name().c_str(),s.c_str());
    Client::self()->toggle(QtClient::parentWindow(sender),s,on);
}

// Handle a child's selection change
void QtUIWidget::onSelect(QObject* sender, const String* item)
{
    if (!Client::self())
	return;
    String s;
    getIdentity(sender,s);
    if (!s)
	return;
    QtWindow* wnd = QtClient::parentWindow(sender);
    String tmp;
    if (!item) {
	item = &tmp;
	if (wnd)
	    wnd->getSelect(YQT_OBJECT_NAME(sender),tmp);
    }
    DDebug(ClientDriver::self(),DebugAll,"QtUIWidget(%s) raising select %s",
	name().c_str(),s.c_str());
    Client::self()->select(wnd,s,*item);
}

// Handle a child's multiple selection change
void QtUIWidget::onSelectMultiple(QObject* sender, const NamedList* items)
{
    if (!Client::self())
	return;
    String s;
    getIdentity(sender,s);
    if (!s)
	return;
    QtWindow* wnd = QtClient::parentWindow(sender);
    DDebug(ClientDriver::self(),DebugAll,"QtUIWidget(%s) raising select multiple",
	name().c_str());
    if (items) {
	Client::self()->select(wnd,s,*items);
	return;
    }
    NamedList tmp("");
    if (wnd)
	wnd->getSelect(YQT_OBJECT_NAME(sender),tmp);
    Client::self()->select(wnd,s,tmp);
}

// Filter wathed events for children.
// Handle child image changing on mouse events
bool QtUIWidget::onChildEvent(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Enter)
	QtClient::updateImageFromMouse(watched,true,true);
    else if (event->type() == QEvent::Leave)
	QtClient::updateImageFromMouse(watched,true,false);
    else if (event->type() == QEvent::MouseButtonPress)
	QtClient::updateImageFromMouse(watched,false,true);
    else if (event->type() == QEvent::MouseButtonRelease)
	QtClient::updateImageFromMouse(watched,false,false);
    return false;
}

// Load an item's widget. Rename children. Connect actions
QWidget* QtUIWidget::loadWidget(QWidget* parent, const String& name, const String& ui)
{
    // Build a new widget name to make sure there are no duplicates:
    //   Some containers (like QTreeWidget) calls deleteLater() for widget's
    //   set to items which might lead to wrong widget update
    // Make sure the widget name contains only 'standard' characters
    //   to avoid errors when replaced in style sheets
    MD5 md5(name);
    String wName;
    buildChildName(wName,md5.hexDigest());
    wName << "_" << (unsigned int)Time::now();
    QWidget* w = QtWindow::loadUI(Client::s_skinPath + ui,parent,ui);
    DDebug(ClientDriver::self(),w ? DebugAll : DebugNote,
	"QtUIWidget(%s)::loadWidget(%p,%s,%s) widget=%p",
	this->name().c_str(),parent,wName.c_str(),ui.c_str(),w);
    if (!w)
	return 0;
    QObject* qObj = getQObject();
    QtWindow* wnd = getWindow();
    // Install event filter in parent window
    if (!m_wndEvHooked && wnd && qObj) {
	QVariant var = w->property("_yate_keypress_redirect");
	if (var.isValid()) {
	    m_wndEvHooked = true;
	    wnd->installEventFilter(qObj);
	}
    }
    String actionSlot;
    String toggleSlot;
    String selectSlot;
    getSlots(actionSlot,toggleSlot,selectSlot);
    QString wListItem = QtClient::setUtf8(name);
    w->setObjectName(QtClient::setUtf8(wName));
    setListItemIdProp(w,wListItem);
    // Build custom UI widgets
    QtClient::buildFrameUiWidgets(w);
    // Process "_yate_setaction" property before changing names
    QtClient::setAction(w);
    // Process children
    QList<QObject*> c = qFindChildren<QObject*>(w);
    for (int i = 0; i < c.size(); i++) {
	// Set object item owner name
	setListItemProp(c[i],wListItem);
	// Rename child
	String n;
	QtClient::getUtf8(n,c[i]->objectName());
	c[i]->setObjectName(buildQChildName(wName,n));
	// Install event filters
	if (qObj && QtClient::getBoolProperty(c[i],"_yate_filterevents"))
	    c[i]->installEventFilter(qObj);
	// Connect text changed to window's slot
	bool connect = QtClient::autoConnect(c[i]);
	if (wnd && connect)
	    wnd->connectTextChanged(c[i]);
	// Connect signals
	if (!(qObj && connect && (actionSlot || toggleSlot || selectSlot)))
	    continue;
	// Use isWidgetType() (faster then qobject_cast)
	if (c[i]->isWidgetType()) {
	    // Connect abstract buttons (check boxes and radio/push/tool buttons) signals
	    QAbstractButton* b = qobject_cast<QAbstractButton*>(c[i]);
	    if (b) {
		if (!b->isCheckable())
		    QtClient::connectObjects(b,SIGNAL(clicked()),qObj,actionSlot);
		else
		    QtClient::connectObjects(b,SIGNAL(toggled(bool)),qObj,toggleSlot);
		continue;
	    }
	    // Connect group boxes
	    QGroupBox* gb = qobject_cast<QGroupBox*>(c[i]);
	    if (gb) {
		if (gb->isCheckable())
		    QtClient::connectObjects(gb,SIGNAL(toggled(bool)),qObj,toggleSlot);
		continue;
	    }
	    // Connect combo boxes
	    QComboBox* combo = qobject_cast<QComboBox*>(c[i]);
	    if (combo) {
		QtClient::connectObjects(combo,SIGNAL(activated(int)),qObj,selectSlot);
		continue;
	    }
	    // Connect list boxes
	    QListWidget* lst = qobject_cast<QListWidget*>(c[i]);
	    if (lst) {
		QtClient::connectObjects(lst,SIGNAL(currentRowChanged(int)),qObj,selectSlot);
		continue;
	    }
	    // Connect sliders
	    QSlider* sld = qobject_cast<QSlider*>(c[i]);
	    if (sld) {
		QtClient::connectObjects(sld,SIGNAL(valueChanged(int)),qObj,selectSlot);
		continue;
	    }
	    continue;
	}
	// Connect actions signals
	QAction* a = qobject_cast<QAction*>(c[i]);
	if (a) {
	    if (!a->isCheckable())
		QtClient::connectObjects(a,SIGNAL(triggered()),qObj,actionSlot);
	    else
		QtClient::connectObjects(a,SIGNAL(toggled(bool)),qObj,toggleSlot);
	    continue;
	}
    }
    return w;
}

// Apply a QWidget style sheet. Replace ${name} with widget name in style
void QtUIWidget::applyWidgetStyle(QWidget* w, const String& style)
{
    if (!(w && style))
	return;
    QString s = QtClient::setUtf8(style);
    s.replace("${name}",w->objectName());
    w->setStyleSheet(s);
}

// Filter key press events. Retrieve an action associated with the key.
// Check if the object is allowed to process the key.
// Raise the action
bool QtUIWidget::filterKeyEvent(QObject* watched, QKeyEvent* event, bool& filter)
{
    String action;
    if (!QtClient::filterKeyEvent(watched,event,action,filter))
	return false;
    if (!action)
	return true;
    String item;
    getListItemProp(watched,item);
    // Avoid raising a disabled actions
    if (item) {
	bool ok = true;
	QWidget* w = findItem(item);
	if (w) {
	    QString n = buildQChildName(w->objectName(),QtClient::setUtf8(action));
	    QObject* act = qFindChild<QObject*>(w,n);
	    if (act) {
		if (act->isWidgetType())
		    ok = (qobject_cast<QWidget*>(act))->isEnabled();
		else {
		    QAction* a = qobject_cast<QAction*>(act);
		    ok = !a || a->isEnabled();
		}
	    }
	}
	if (!ok)
	    return true;
	// Append container item to action
	action.append(item,":");
    }
    Client::self()->action(QtClient::parentWindow(getQObject()),action);
    return true;
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
    else {
	Debug(ClientDriver::self(),DebugNote,"Sound(%s) failed to start file=%s",
	    c_str(),m_file.c_str());
	return false;
    }
    m_sound->setLoops(m_repeat ? m_repeat : -1);
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


//
// QtDragAndDrop
//
// Reset data
void QtDragAndDrop::reset()
{
    m_started = false;
}

// Check a string value for 'drag', 'drop', 'both'
void QtDragAndDrop::checkEnable(const String& s, bool& drag, bool& drop)
{
    drag = (s == YSTRING("drag"));
    drop = !drag && (s == YSTRING("drop"));
    if (!(drag || drop))
	drag = drop = (s == YSTRING("both"));
}

//
// QtDrop
//
const String QtDrop::s_askClientAcceptDrop = "_yate_event_drop_accept";
const String QtDrop::s_notifyClientDrop = "_yate_event_drop";
const QString QtDrop::s_fileScheme = "file";

const TokenDict QtDrop::s_acceptDropName[] = {
    {"always", Always},
    {"ask", Ask},
    {"none", 0},
    {0,0}
};

QtDrop::QtDrop(QObject* parent, const NamedList* params)
    : QtDragAndDrop(parent),
    m_dropParams(""),
    m_acceptFiles(false),
    m_acceptDirs(false)
{
    if (!params)
	return;
    NamedIterator iter(*params);
    for (const NamedString* ns = 0; 0 != (ns = iter.get());) {
	if (ns->name() == YSTRING("_yate_accept_drop_schemes"))
	    QtClient::addStrListUnique(m_schemes,QtClient::str2list(*ns));
	else if (ns->name() == YSTRING("_yate_accept_drop_file"))
	    m_acceptFiles = ns->toBoolean();
	else if (ns->name() == YSTRING("_yate_accept_drop_dir"))
	    m_acceptDirs = ns->toBoolean();
    }
}

// Update parameters from drag enter event
bool QtDrop::start(QDragEnterEvent& e)
{
    static const String s_prefix = "drop:";

    reset();
    const QMimeData* m = e.mimeData();
    if (!(m && m->hasUrls()))
	return false;
    int nUrls = m->urls().size();
    unsigned int nItems = 0;
    for (int i = 0; i < nUrls; i++) {
	QString scheme = m->urls()[i].scheme();
	if (m_schemes.size() > 0 && !m_schemes.contains(scheme)) {
	    reset();
	    return false;
	}
	QString path = m->urls()[i].path();
	String what = scheme.toUtf8().constData();
	if (scheme == s_fileScheme) {
#ifdef _WINDOWS
	    path = path.mid(1);
#endif
	    path = QDir::toNativeSeparators(path);
	    QFileInfo fi(path);
	    if (fi.isDir()) {
		if (!m_acceptDirs) {
		    reset();
		    return false;
		}
		what = "directory";
	    }
	    else if (fi.isFile() && !m_acceptFiles) {
		reset();
		return false;
	    }
	}
	nItems++;
	NamedList* nl = new NamedList("");
	QtClient::fillUrlParams(m->urls()[i],*nl,&path);
	m_dropParams.addParam(new NamedPointer(s_prefix + what,nl,*nl));
    }
    if (!nItems) {
	reset();
	return false;
    }
    if (e.source()) {
	QtWindow* wnd = QtClient::parentWindow(e.source());
	if (wnd) {
	    m_dropParams.addParam("source_window",wnd->toString());
	    QtClient::getUtf8(m_dropParams,"source",e.source()->objectName());
	}
    }
    m_started = true;
    return true;
}

// Reset data
void QtDrop::reset()
{
    m_dropParams.clearParams();
    QtDragAndDrop::reset();
}


//
// QtListDrop
//
QtListDrop::QtListDrop(QObject* parent, const NamedList* params)
    : QtDrop(parent,params),
    m_acceptOnEmpty(None)
{
}

// Update accept
void QtListDrop::updateAcceptType(const String list, int type)
{
    if (!list)
	return;
    ObjList* l = list.split(',',false);
    for (ObjList* o = l->skipNull(); o; o = o->skipNext()) {
	NamedInt* ni = new NamedInt(*static_cast<String*>(o->get()),type);
	NamedInt::addToListUniqueName(m_acceptItemTypes,ni);
    }
    TelEngine::destruct(l);
}

// Update accept from parameters list
void QtListDrop::updateAccept(const NamedList& params)
{
    NamedIterator iter(params);
    for (const NamedString* ns = 0; 0 != (ns = iter.get());) {
	if (ns->name() == YSTRING("_yate_accept_drop_onempty"))
	    m_acceptOnEmpty = this->acceptDropType(*ns,None);
	else if (ns->name() == YSTRING("_yate_accept_drop_item_type_always"))
	    updateAcceptType(*ns,Always);
	else if (ns->name() == YSTRING("_yate_accept_drop_item_type_none"))
	    updateAcceptType(*ns,None);
	else if (ns->name() == YSTRING("_yate_accept_drop_item_type_ask"))
	    updateAcceptType(*ns,Ask);
    }
}

// Reset data
void QtListDrop::reset()
{
    m_acceptItemTypes.clear();
    QtDrop::reset();
}


//
// QtBusyWidget
//
const QString QtBusyWidget::s_busySuffix("_yate_busy_widget_generated");

// Constructor
QtBusyWidget::QtBusyWidget(QWidget* parent)
    : QtCustomWidget(0,parent),
    m_target(0), m_shown(false), m_delayMs(0), m_delayTimer(0),
    m_movieLabel(0)
{
    if (parent)
	setObjectName(parent->objectName() + s_busySuffix);
    QWidget::hide();
}

// Initialize
void QtBusyWidget::init(const String& ui, const NamedList& params, QWidget* target)
{
    hideBusy();
    m_target = target;
    m_movieLabel = 0;
    unsigned int delay = 0;
    QWidget* w = ui ? loadWidget(this,"",ui) : 0;
    if (w) {
	QtClient::setWidget(this,w);
	int tmp = QtClient::getIntProperty(w,"_yate_busywidget_delay");
	if (tmp > 0)
	    delay = tmp;
	QList<QWidget*> c = qFindChildren<QWidget*>(w);
	for (int i = 0; i < c.size(); i++) {
	    QLabel* l = qobject_cast<QLabel*>(c[i]);
	    if (l) {
		if (!m_movieLabel) {
		    String file;
		    QtClient::getProperty(l,"_yate_movie_file",file);
		    if (file) {
			l->setMovie(QtClient::loadMovie(file,l));
			if (l->movie())
			    m_movieLabel = l;
		    }
		}
	    }
	}
    }
    m_delayMs = params.getIntValue(YSTRING("_yate_busywidget_delay"),delay,0);
}

// Show the widget
void QtBusyWidget::showBusy()
{
    if (m_shown)
	return;
    m_shown = true;
    if (m_delayMs)
	m_delayTimer = startTimer(m_delayMs);
    if (!m_delayTimer)
	internalShow();
}

// Hide the widget
void QtBusyWidget::hideBusy()
{
    if (!m_shown)
	return;
    m_shown = false;
    stopDelayTimer();
    if (m_target)
	m_target->removeEventFilter(this);
    setContent(false);
    lower();
    hide();
}

// Filter wathed events
bool QtBusyWidget::onChildEvent(QObject* watched, QEvent* event)
{
    if (m_target && m_target == watched) {
	if (event->type() == QEvent::Resize)
	    resize(m_target->size());
    }
    return false;
}

void QtBusyWidget::timerEvent(QTimerEvent* ev)
{
    if (m_delayTimer && ev->timerId() == m_delayTimer) {
	stopDelayTimer();
	internalShow();
	return;
    }
    QtCustomWidget::timerEvent(ev);
}

// Show/hide busy content
void QtBusyWidget::setContent(bool on)
{
    QMovie* movie = m_movieLabel ? m_movieLabel->movie() : 0;
    if (!movie)
	return;
    if (on)
	movie->start();
    else
	movie->stop();
}

void QtBusyWidget::internalShow()
{
    if (m_target) {
	resize(m_target->size());
	m_target->installEventFilter(this);
    }
    setContent(true);
    raise();
    show();
}

/* vi: set ts=8 sw=4 sts=4 noet: */
