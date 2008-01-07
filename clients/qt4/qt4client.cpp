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

#include <yatecbase.h>
#include <qt4client.h>
#include <QtUiTools>

#ifdef _WINDOWS
#define DEFAULT_DEVICE "dsound/*"
#else
#define DEFAULT_DEVICE "oss//dev/dsp"
#endif

using namespace TelEngine;

static Configuration s_cfg;
static Configuration s_save;
static Configuration s_callHistory;
static String s_skinPath;


QtWindow::QtWindow()
{
    m_ringtone = new QSound("ring.wav", this);
}

QtWindow::QtWindow(const char* name, const char* description)
	: Window(name), m_description(description), m_keysVisible(true)
{
    m_ringtone = new QSound("ring.wav", this);
}

QtWindow::~QtWindow()
{
    s_save.setValue(m_id, "x", m_x);
    s_save.setValue(m_id, "y", m_y);
    s_save.setValue(m_id, "width", m_width);
    s_save.setValue(m_id, "height", m_height);
    s_save.setValue(m_id, "visible", m_visible);
    QList<QTimer*> timer = qFindChildren<QTimer*>(this);
}

void QtWindow::title(const String& text)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::title() : text %s", text.c_str());
}

void QtWindow::context(const String& text)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::context() : text %s", text.c_str());
    m_context = text;
}

bool QtWindow::setParams(const NamedList& params)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::setParams()");

    return Window::setParams(params);
}

void QtWindow::setOver(const Window* parent)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::setOver() [%p]", parent);
}

bool QtWindow::hasElement(const String& name)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::hasElement() name : %s", name.c_str());

    QWidget* wid = qFindChild<QWidget*>(this, name.c_str());
    if (wid)
	return true;
    return false;
}

bool QtWindow::setActive(const String& name, bool active)
{
    DDebug(QtDriver::self(), DebugWarn, "QtWindow::setActive() name : %s, bool : %s", name.c_str(),
		 String::boolText(active));


    if (name == "ringtone") {
	if(active) {
	    if (m_ringtone->isFinished())
		m_ringtone->play();
	}
	else
	    m_ringtone->stop();
    }

    QWidget* wid = qFindChild<QWidget*>(this, name.c_str());
    if (!wid)
	return false;
    //accept behaviour
    if (name == "accept")
    {
	setShow("accept", active);
	setShow("call", !active);
    }
    
    wid->setEnabled(active);
    return true;
}

bool QtWindow::setFocus(const String& name, bool select)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::setFocus() name : %s, select : %s", name.c_str(),
		String::boolText(select));

    QWidget* wid = qFindChild<QWidget*>(this, name.c_str());
    if (!wid)
	return false;

    QString type = wid->metaObject()->className();

    if (type == "QComboBox") {
	wid->setFocus();
	QComboBox* combo = (QComboBox*) wid;
	if (combo->isEditable() && select)
	    combo->lineEdit()->selectAll();
	return true;
    }

    return false;
}

bool QtWindow::setShow(const String& name, bool visible)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::setShow() name : %s, visible %s", name.c_str(),
		String::boolText(visible));

    if( name == "keypad") {
	QList<QAbstractButton*> keys = qFindChildren<QAbstractButton*>(this, "digit");
	if (keys.isEmpty())
	    return false;
	for(int i = 0; i < keys.size(); i++)
	    keys[i]->setVisible(visible);
	QPushButton* back = qFindChild<QPushButton*>(this, "back_callto");
	if (back)
	    back->setVisible(visible);
	QPushButton* clear = qFindChild<QPushButton*>(this, "clear_callto");
	if (clear)
	    clear->setVisible(visible);
	return true;
    }

    if (name == "transfer") {
	QLineEdit* lineEdit = qFindChild<QLineEdit*>(this, "transferedit");
	if (lineEdit) {
	    lineEdit->setVisible(visible);
	    lineEdit->clear();
	    lineEdit->setFocus();
	    }
	QPushButton* button = qFindChild<QPushButton*>(this, "transferbutton");
	if (button)
	    button->setVisible(visible);
	return true;
    }

    if (name == "conference") {
	QLineEdit* lineEdit = qFindChild<QLineEdit*>(this, "conferenceedit");
	if (lineEdit) {
	    lineEdit->setVisible(visible);
	    lineEdit->clear();
	    lineEdit->setFocus();
	    }
	QPushButton* button = qFindChild<QPushButton*>(this, "conferencebutton");
	if (button)
	    button->setVisible(visible);
	return true;
    }

   QWidget* wid = qFindChild<QWidget*>(this, name.c_str());
    if (!wid)
	return false;

    QString type = wid->metaObject()->className();

    if (type == "QPushButton") {
	((QPushButton*) wid)->setVisible(visible);
	return true;
    }
    return false;
}

bool QtWindow::setText(const String& name, const String& text)
{
   // DDebug(QtDriver::self(), DebugInfo, "QtWindow::settext() name: %s, text: <<<<< %s >>>>>", name.c_str(), text.c_str());
    if (name == "log_events")
    {
	QCheckBox* window = qFindChild<QCheckBox*>(this, "events");
	if (window && window->isChecked())
	{
	    QTextEdit* textEdit = qFindChild<QTextEdit*>(this, "log_events");
	    if (textEdit)
	    {
		textEdit->append(text.c_str());
		return true;
	    }
	}
	QCheckBox* fileDebug = qFindChild<QCheckBox*>(this, "debugtofile");
	if (fileDebug && fileDebug->isChecked())
	{
	    QLineEdit* filename = qFindChild<QLineEdit*>(this, "filename");
	    if (filename)
	    {
		QString textInEdit =  filename->text();
		if (!textInEdit.isEmpty())
		{
		    QFile file(textInEdit);
		    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append))
			return false;
		    QByteArray textToWrite(text.c_str());
		    if (file.write(textToWrite) == -1)
		        return false;
		    if (file.write("\n") == -1)
		        return false;
		    file.close();
		    return true;
		}
	    }

	}

    }
    if (name.startsWith("l:")) {
	QList<QWidget*> lines = qFindChildren<QWidget*>(this, "internalLine");
	if (lines.empty())
	    return false;
	
	QString type = lines[0]->metaObject()->className();
	
	if (type == "QPushButton") {
	    for (int i = 0; i < lines.size(); i++) {
		if (lines[i]->accessibleName() == name.c_str()) {
		    QPushButton* button = (QPushButton*) lines[i];
		    QString qtext = button->text();
		    if (text != "") {
			qtext.append(": ");
			qtext.append(text.c_str());
			button->setText(qtext);
			return true;
		    }
		    else {
			qtext.truncate(6);
			button->setText(qtext);
			return true;
		    }
		}
	    }
	}
    }
    if (name == "internalLine") {
	QList<QWidget*> lines = qFindChildren<QWidget*>(this, name.c_str());
	if (lines.empty())
	    return false;

	QString type = lines[0]->metaObject()->className();
	if (type == "QPushButton") {
	    for (int i = 0; i < lines.size(); i++) {
		QPushButton* button = (QPushButton*) lines[i];
		if (button->isChecked()) {
		    QString qtext = button->text();
		    if (text != "") {
			qtext.append(": ");
			qtext.append(text.c_str());
			button->setText(qtext);
			return true;
		    }
		    else {
			qtext.truncate(6);
			button->setText(qtext);
			return true;
		    }
		}
	    }
	}
	return false;
    }

    QWidget* wid = qFindChild<QWidget*>(this, name.c_str());
    if (!wid)
	if (!channelsList.contains(name.c_str()))
	    return false;
	else {
	    int index = -1;
	    for (int i = 0; i < channelsList.size(); i++)
		if (channelsList[i] == name.c_str())
		    index = i;
	    if (index > -1) {
		QListWidget* list = qFindChild<QListWidget*>(this, "channels");
		if (list) {
		    (list->item(index))->setText(text.c_str());
		    list->update();
		    list->dumpObjectInfo();
		    return true;
		}
	    }
	    return false;		
	}

    QString type = wid->metaObject()->className();

    if( type == "QComboBox") {
	if (((QComboBox*) wid )->lineEdit())
	    ((QComboBox*) wid )->lineEdit()->setText(text.c_str());
	QTabWidget* tabs = qFindChild<QTabWidget*>(this, "tabs");
	if (tabs)
	    tabs->setCurrentIndex(0);
	return true;
    }

    if (type == "QLabel") {
	QLabel* label = ((QLabel*) wid);
	label->setText(text.c_str());
	return true;
    }

    if (type == "QLineEdit") { 
	((QLineEdit*) wid)->setText(text.c_str());
	return true;
    }

    if (type == "QTextEdit") {
	QTextEdit* textEdit = (QTextEdit*) wid;
	if(!name.startsWith("help"))
	    textEdit->append(text.c_str());
	else
	    textEdit->setText(text.c_str());
	return true;
    }
    return false;
}

bool QtWindow::setCheck(const String& name, bool checked)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::setCheck() name : %s, visible : %s", name.c_str(),
		String::boolText(checked));

    if (name.startsWith("l:")) {
	int l = name.substr(2).toInteger();
	QList<QWidget*> lines = qFindChildren<QWidget*>(this, "internalLine");
	if (lines.empty())
	    return false;
	if (l > lines.size())
	    return false;

	QString type = lines[0]->metaObject()->className();

	if (type == "QPushButton") {
	    for (int i = 0; i < lines.size(); i++)
		if (lines[i]->accessibleName() == name.c_str()) {
		    ((QPushButton*) lines[i])->setChecked(checked);
		    return true;
		}
	}
	
	return false;
    }
    if (name == "internalLine") {
	QList<QWidget*> lines = qFindChildren<QWidget*>(this, name.c_str());
	if (lines.empty())
	    return false;

	QString type = lines[0]->metaObject()->className();
	
	if (type == "QPushButton") {
	    for (int i = 0; i < lines.size(); i++) {
		QPushButton* button = (QPushButton*) lines[i];
		if (button->isChecked()) {
		    button->setChecked(checked);
		    return true;
		}
	    }
	}
	return false;
    }

    QWidget* wid = qFindChild<QWidget*>(this, name.c_str());
    if (!wid)
	return false;

    QString type = wid->metaObject()->className();

    if (type == "QCheckBox") {
	((QCheckBox*) wid)->setChecked(checked);
	return true;
    }

    if (type == "QPushButton") {
	((QPushButton*) wid)->setChecked(checked);
	return true;
    }

    return false;
}

bool QtWindow::setSelect(const String& name, const String& item)
{
    Debug(QtDriver::self(), DebugInfo, "QtWindow::setSelect() name : %s, item %s", name.c_str(), item.c_str());

    QWidget* wid = qFindChild<QWidget*>(this, name.c_str());
    if (!wid)
	return false;

    QString type = wid->metaObject()->className();

    if (item == "")
	return true;

    if (type == "QComboBox") {
	int index = ((QComboBox*) wid)->findText(item.c_str());
	if (index == -1)
	    return false;
	((QComboBox*) wid)->setCurrentIndex(index);
	return true;
    }

    if (type == "QListWidget") {
	QListWidget* listWidget = (QListWidget*) wid;
	for (int i = 0; i < channelsList.size(); i++)
	    if (channelsList[i] == item.c_str())
		listWidget->setCurrentItem(listWidget->item(i));
	return true;
    }
    
    if (type == "QSlider") {
	((QSlider*) wid)->setValue(item.toInteger());
	return true;
    }

    if (type == "QStackedWidget") {
        QStackedWidget* stack = (QStackedWidget*)wid;
	int i = item.toInteger(-1);
	if (i >= 0) {
	    if (i < stack->count()) {
		stack->setCurrentIndex(i);
		return true;
	    }
	}
	for (i = 0; i < stack->count(); i++) {
	    QWidget* page = stack->widget(i);
	    if (page && (page->objectName().toAscii() == item)) {
		stack->setCurrentIndex(i);
		return true;
	    }
	}
	return false;
    }
    return false;
}

bool QtWindow::setUrgent(const String& name, bool urgent)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::setUrgent() name : %s, visible %s", name.c_str(),
		String::boolText(urgent));
    return false;
}

bool QtWindow::hasOption(const String& name, const String& item)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::hasOption() name : %s, item %s", name.c_str(), item.c_str());

    QWidget* wid = qFindChild<QWidget*>(this, name.c_str());
    if (!wid)
	return false;

    QString type = wid->metaObject()->className();

    if (type == "QComboBox") {
	QComboBox* combo = (QComboBox*) wid;
	if( combo->findText(item.c_str()) != -1)
	    return true;
    }

    if (type == "QTableWidget") {
	QString itemLabel(item.c_str());
	QList<QTableWidgetItem*> items =  ((QTableWidget*)wid)->findItems(itemLabel, Qt::MatchFixedString);
	if (items.isEmpty())
	    return false;
	return true;
    }

    if (type == "QListWidget") {
	if (!channelsList.empty())
	    for (int i = 0; i < channelsList.size(); i++)
		if (channelsList[i] == item.c_str())
		    return true;
    }

    return false;
}

bool QtWindow::addOption(const String& name, const String& item, bool atStart, const String& text)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::addOption() %s , %s, %s %s", name.c_str(), item.c_str(),
	text.c_str(), String::boolText(atStart));

    QWidget* wid = qFindChild<QWidget*>(this, name.c_str());
    if (!wid)
	return false;

    QString type = wid->metaObject()->className();

    if (type == "QListWidget") {
	QListWidget* list = (QListWidget*) wid;
	QString str = text.c_str();
	QString itemStr = item.c_str();
	if (atStart) {
	    list->insertItem(0, str);
	    channelsList.insert(0, itemStr);
	}
	else {
	    list->addItem(str);
	    channelsList.append(itemStr);
	}
	return true;
    }

    if (type == "QComboBox") {
	QComboBox* combo = (QComboBox*) wid;
	if (atStart) {
	    combo->insertItem(0, item.c_str());
	    if (combo->lineEdit())
		combo->lineEdit()->setText(combo->itemText(0));
	}
	else 
	    ((QComboBox*) wid)->addItem(item.c_str());
	return true;
    }

    if (type == "QTableWidget") {
	QTableWidget* table = (QTableWidget*) wid;
	QTableWidgetItem* tableItem = new QTableWidgetItem(item.c_str());
	if (atStart) {
	    table->insertRow(0);
	    table->setItem(0, 0, tableItem);
	}
	else {
	    table->insertRow(table->rowCount());
	    table->setItem(table->rowCount()-1, 0, tableItem);
	}
	return true;
    }
    return false;

}

bool QtWindow::delOption(const String& name, const String& item)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::delOption() name : %s, item %s", name.c_str(), item.c_str());
    QWidget* wid = qFindChild<QWidget*>(this, name.c_str());
    if (!wid)
	return false;

    QString type = wid->metaObject()->className();

    if (type == "QTableWidget") {

	QTableWidget* table = (QTableWidget*) wid;
	QList<QTableWidgetItem*> items = table->selectedItems();
	if (items.isEmpty())
	    return false;
	for (int i = 0; i < items.size(); i++)
	    if (items[i]->text() == item.c_str()) {
		table->removeRow(items[i]->row());
	    }
	return true;
    }

    if (type == "QComboBox") {
	int index = ((QComboBox*) wid)->findText(item.c_str());
	if (index != -1)
	    ((QComboBox*) wid)->removeItem(index);
	return true;
    }

    if (type == "QListWidget") {
	int index = -1;
	QListWidget* list = (QListWidget*) wid;
	for (int i = 0; i < channelsList.size(); i++)
	    if (channelsList[i] == item.c_str())
		index = i;
	channelsList.removeAt(index);
	QStringListModel* modelList = (QStringListModel*)list->model();
	modelList->removeRow(index);
	return true;
    }
    return false;
}

bool QtWindow::addTableRow(const String& name, const String& item, const NamedList* data, bool atStart)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::addTableRow() name : %s, item %s, data %s, atStart %s", name.c_str(),
		item.c_str(), data->toString().c_str(), String::boolText(atStart));

    QTableWidget* table = qFindChild<QTableWidget*>(this, name.c_str());
    if (!table)
	return false;
    int ncol =  table->columnCount();
    int index;
    if (atStart) {
	table->insertRow(0);
	index = 0;
    }
    else {
	table->insertRow(table->rowCount());
	index = table->rowCount() - 1;
    }

    for(int i = 0; i < ncol; i++) {
	String header = table->horizontalHeaderItem(i)->text().toAscii().data();
	header.toLower();
	const String* value;
	if (header == "time")
	{
	    value = data->getParam(header);
	    QString str= value->c_str();
	    str.chop(4);
	    uint secs = str.toUInt();
	    
	    QDateTime time = QDateTime::fromTime_t(secs);
	    
	    value = new String(time.toString("hh:mm \nMM.dd.yy").toAscii().data());
	}
	else
	    value = data->getParam(header);
	if (value) {
	    QTableWidgetItem* tableItem = new QTableWidgetItem(value->c_str());
	    table->verticalHeader()->hide();
	    table->setItem(index, i, tableItem);
	}
    }
    table->repaint();
    return true;
}

bool QtWindow::delTableRow(const String& name, const String& item)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::delTableRow() name : %s, item %s", name.c_str(), item.c_str());
    return false;
}

bool QtWindow::setTableRow(const String& name, const String& item, const NamedList* data)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::setTableRow() name : %s, item %s, data [%p]", name.c_str(),
	item.c_str(), data);
    return false;
}

bool QtWindow::getTableRow(const String& name, const String& item, NamedList* data)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::getTableRow() name : %s, item %s, data [%p]", name.c_str(),
	item.c_str(), data->toString().c_str());

    QTableWidget* table = qFindChild<QTableWidget*>(this, name.c_str());
    if (!table)
	return false;

    QList<QTableWidgetItem*> items = table->selectedItems();
    if (items.isEmpty())
	return false;

    if (data) {
	int ncol = items.size();
	for(int i = 0; i < ncol; i++) {
	    String header = table->horizontalHeaderItem(items[i]->column())->text().toAscii().constData();
	    String value = items[i]->text().toAscii().constData();
	    data->setParam(header.toLower(), value);
	}
	return true;
    }

    return false;
}

bool QtWindow::clearTable(const String& name)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::clearTable() name : %s", name.c_str());

    QWidget* wid = qFindChild<QWidget*>(this, name.c_str());
    if (!wid)
	return false;

    QString type = wid->metaObject()->className();

    if (type == "QTableWidget") {
	QTableWidget* table = (QTableWidget*) wid;
 	int n = table->rowCount();
 	for (int i = 0; i < n; i++)
	    table->removeRow(0);
	
	return true;
    }

    if (type == "QTextEdit") {
	((QTextEdit*) wid)->clear();
	return true;
    }

    return false;
}

bool QtWindow::getText(const String& name, String& text)
{
//     DDebug(QtDriver::self(),DebugInfo, "QtWindow::getText() name : %s , text : %s", name.c_str(), text.c_str());
    if (name == "internalLine") {
	QList<QWidget*> widgets = qFindChildren<QWidget*>(this, name.c_str());
	if (widgets.isEmpty())
	    return false;
	
	QString type = widgets[0]->metaObject()->className();

	if (type == "QPushButton") {
	    for (int i = 0; i < widgets.size(); i++) {
		QPushButton* button = (QPushButton*) widgets[i];
		if (button->isChecked()) {
		    text = button->accessibleName().toAscii().data();
		    return true;
		}
	    }
	}
	
	return false;
    }

    else {
	QWidget* wid = qFindChild<QWidget*>(this, name.c_str());
	if (!wid)
	    return false;

	QString type = wid->metaObject()->className();

	if (type == "QPushButton") {
	    text = ((QPushButton*) wid)->text().toAscii().data();
	    return true;
	}

	if (type == "QComboBox") {
	    text = ((QComboBox*) wid)->currentText().toAscii().data();
	    return true;
	}

	if (type == "QLineEdit") {
	    text = ((QLineEdit*) wid)->text().toAscii().data();
	    return true;
	}

	if (type == "QTextEdit") {
	    text = ((QTextEdit*) wid)->toPlainText().toAscii().data();
	    return true;
	}

	if (type == "QLabel") {
	    text = ((QLabel*) wid)->text().toAscii().data();
	    return true;
	}

	return false;
    }
}

bool QtWindow::getCheck(const String& name, bool& checked)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::getCheck() name : %s", name.c_str());

    if (name.startsWith("l:")) {
	QList<QWidget*> lines = qFindChildren<QWidget*>(this, "internalLine");
	if (lines.empty())
	    return false;

	QString type = lines[0]->metaObject()->className();
	
	if (type == "QPushButton") {
	    for (int i = 0; i < lines.size(); i++) {
		QPushButton* button = (QPushButton*) lines[i];
		if (button->accessibleName() == name.c_str()) {
		    checked = button->isChecked();
		    return true;
		}
	    }
	}
	return false;
    }

    QWidget* wid = qFindChild<QWidget*>(this, name.c_str());
    if (!wid)
	return false;

    QString type = wid->metaObject()->className();

    if (type == "QCheckBox") { 
	checked = ((QCheckBox*) wid)->isChecked();
	return true;
    }

    if (type == "QPushButton") {
	checked = ((QPushButton*) wid)->isChecked();
	return true;
    }

    return false;
}

bool QtWindow::getSelect(const String& name, String& item)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::getSelect() name : %s, item : %s", name.c_str(), item.c_str());

    QWidget* wid = qFindChild<QWidget*>(this, name.c_str());
    if (!wid)
	return false;
    QString type = wid->metaObject()->className();

    if (type == "QComboBox") {
	item = ((QComboBox*) wid)->currentText().toAscii().data();
	//
	if (((QComboBox*) wid)->lineEdit() && (((QComboBox*) wid)->lineEdit()->selectedText()).isEmpty())
	    return false;
	//
	return true;
    }

    if (type == "QTableWidget") {
	QTableWidget* table = (QTableWidget*) wid;
	QList<QTableWidgetItem*> items = table->selectedItems();
	if (items.isEmpty())
	    return false;
	item = items[0]->text().toAscii().constData();
	return true;
    }

    if (type == "QListWidget") {
	QListWidget* list = (QListWidget*) wid;
	QList<QListWidgetItem*> items = list->selectedItems();
	if (items.isEmpty())
	    return false;
	item = channelsList[list->row(items[0])].toAscii().constData();
	return true;
    }

    if (type == "QSlider") {
	item = ((QSlider*) wid)->value();
	return true;
    }
    return false;
}

bool QtWindow::select(const String& name, const String& item, const String& text)
{
    if (!QtClient::self() || QtClient::changing())
	return false;
    return QtClient::self()->select(this, name, item, text);
}

void QtWindow::closeEvent(QCloseEvent* event)
{
    QtWindow* win = (QtWindow*) QtClient::getWindow("mainwindow");
    if (m_id == "events") {
	if (win) {
	    QCheckBox* check = qFindChild<QCheckBox*>(win, "events");
	    if (check)
		check->setChecked(false);
	}
    }

    if (m_id == "help") {
	if (win) {
	    QPushButton* button = qFindChild<QPushButton*>(win, "help");
	    if (button)
		button->setChecked(false);
	}
    }

    if (m_id == "mainwindow") {
	for (ObjList* windows = QtClient::listWindows(); windows; windows = windows->next()) {
	    String* str = static_cast<String*>(windows->get());
	    QtWindow* win = (QtWindow*) Client::getWindow(str);
	    if (win->isVisible())
		if (win->m_id != "events" && win->m_id != "help" && win->m_id != "mainwindow") {
		    event->ignore();
		    return;
		}
	}
	for (ObjList* windows = QtClient::listWindows(); windows; windows = windows->next()) {
	    String* str = static_cast<String*>(windows->get());
	    QtWindow* win = (QtWindow*) Client::getWindow(str);
	    if (win->isVisible())
		win->m_visible = true;
	    else
		win->m_visible = false;
	    if (win->m_id != "mainwindow")
		win->close();
	}
    }
    QWidget::closeEvent(event);
}

void QtWindow::populate()
{
    Debug(QtDriver::self(), DebugInfo, "Populating window %s", m_id.c_str());
    QUiLoader loader;
    loader.setWorkingDirectory(QDir(s_skinPath.c_str()));
    QFile file(m_description.c_str());
    if (!file.exists()) {
	Debug(DebugWarn,"UI description file does not exist");
	return;
    }

    QWidget *formWidget = loader.load(&file, this);

    file.close();

    QSize frame = frameSize();
    setMinimumSize(formWidget->minimumSize().width(), formWidget->minimumSize().height());
    setMaximumSize(formWidget->maximumSize().width(), formWidget->maximumSize().height());
    resize(formWidget->width(), formWidget->height());
    //QWidget::move(formWidget->pos().x(), formWidget->pos().y());

    QVBoxLayout *layout = new QVBoxLayout;
    layout->setSpacing(0);
    layout->setContentsMargins(0,0,0,0);
    layout->addWidget(formWidget);
    setLayout(layout);
    setWindowTitle(formWidget->windowTitle());
    setWindowIcon(formWidget->windowIcon());
}

void QtWindow::disableCombo()
{
    QComboBox* senderWidget = (QComboBox*) sender();
    if (senderWidget->currentIndex() == 0)
	return;
    if (senderWidget->objectName() == "protocol")
	qFindChild<QComboBox*>(this, "account")->setCurrentIndex(0);
    if (senderWidget->objectName() == "account")
	qFindChild<QComboBox*>(this, "protocol")->setCurrentIndex(0);
}

void QtWindow::focus()
{
    QComboBox* combo = qFindChild<QComboBox*>(this, "callto");
    combo->lineEdit()->selectAll();
}

void QtWindow::action()
{
    DDebug(QtDriver::self(), DebugMild, "QtWindow::action() in window '%s' action is %s", m_id.c_str(),
	sender()->objectName().toAscii().data());

    QWidget* wid = (QWidget*) sender();
    String name;
    if (!QtClient::self() || QtClient::changing())
	return;
    if (wid->accessibleName().isEmpty()) 
	name = wid->objectName().toAscii().data();
    else
	name = wid->accessibleName().toAscii().data();

    if (String(wid->metaObject()->className()) == "QComboBox")
	QtClient::self()->select(this, name, ((QComboBox*)wid)->currentText().toAscii().data());

    if (name == "button_hide") {
	if (wid->parent()->objectName() == "help") {
	    QtWindow* win = (QtWindow*) QtClient::getWindow("mainwindow");
	    if (win) {
		QPushButton* button = qFindChild<QPushButton*>(win, "help");
		if (button)
		    button->setChecked(false);
	    }
	}
	hide();
    }

    if (name == "debug")
	QtClient::self()->action(this, "events");

#if 0
    if (name.startsWith("display:") || name.startsWith("debug:") || name == "autoanswer" || name == "multilines"
		|| name == "dnd" || name == "hold" || name == "log_events_debug" || name == "events" || name == "help"
		|| name == "transfer" || name == "conference" || name == "record")
#endif
    if (wid->inherits("QAbstractButton") && ((QAbstractButton*)wid)->isCheckable())
	QtClient::self()->toggle(this, name, ((QAbstractButton*)wid)->isChecked());
    else
	QtClient::self()->action(this, name);

}

void QtWindow::enableDebugOptions(bool enable)
{
    QCheckBox* window = qFindChild<QCheckBox*>(this, "events");
    if (window)
    {
	if (!enable)
	    setCheck("events", false);
	window->setEnabled(enable);
    }
    QCheckBox* file = qFindChild<QCheckBox*>(this, "debugtofile");
    if (file)
    {
	if (!enable)
	    setCheck("debugtofile", false);
	file->setEnabled(enable);
    }
}

void QtWindow::enableFileChoosing(bool enable)
{
    QLineEdit* lineEdit =  qFindChild<QLineEdit*>(this, "filename");
    if (lineEdit)
	lineEdit->setEnabled(enable);
    QPushButton* pushButton = qFindChild<QPushButton*>(this, "chooseFile");
    if (pushButton)
	pushButton->setEnabled(enable);
}

void QtWindow::chooseFile()
{
    QString filename = QFileDialog::getSaveFileName(this, "Save File", "log.txt", 0);
    if (!filename.isNull())
    {
	QLineEdit* line = qFindChild<QLineEdit*>(this, "filename");
        if (line)
	    line->setText(filename);
    }	
}

void QtWindow::openUrl(const QString& link)
{
    QDesktopServices::openUrl(QUrl(link));
}


void QtWindow::idleActions()
{
    QtClient::self()->idleActions();
}

void QtWindow::select(int value)
{
    QWidget* wid = (QWidget*) sender();
    String name = wid->objectName().toAscii().data();
    select(wid->objectName().toAscii().data(), value);
}

void QtWindow::selectionToCallto()
{
    QWidget* wid = (QWidget*) sender();
    String name = wid->objectName().toAscii().data();
    String selected;
    getSelect(name, selected);
    setText("callto", selected);
}

void QtWindow::keyPressEvent(QKeyEvent* e)
{
    QPushButton* call = qFindChild<QPushButton*>(this, "call");
    QPushButton* accept = qFindChild<QPushButton*>(this, "accept");
    String name;
    if (!call && !accept)
	e->ignore();
    if (call && call->isVisible())
	name = "call";
    else
	name = "accept";

    if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
	QtClient::self()->action(this, name);
	return;
    }
    if (e->key() == Qt::Key_Escape) {
	QtClient::self()->action(this, "hangup");
	return;
    }
    e->ignore();    
}

void QtWindow::init()
{
    DDebug(QtDriver::self(), DebugWarn, "QtWindow::init()");

    m_x = s_save.getIntValue(m_id, "x", pos().x());
    m_y = s_save.getIntValue(m_id, "y", pos().y());
    m_width = s_save.getIntValue(m_id, "width", width());
    m_height = s_save.getIntValue(m_id, "height", height());
    bool m_visible = s_save.getBoolValue(m_id, "visible", false);

    if (m_id == "mainwindow")
	m_visible = true;

    QLineEdit* transferTo = qFindChild<QLineEdit*>(this, "transferedit");
    if (transferTo)
	transferTo->hide();
    QPushButton* transferButton = qFindChild<QPushButton*>(this, "transferbutton");
    if (transferButton)
	transferButton->hide();
    QLineEdit* conferenceWith = qFindChild<QLineEdit*>(this, "conferenceedit");
    if (conferenceWith)
	conferenceWith->hide();
    QPushButton* conferenceButton = qFindChild<QPushButton*>(this, "conferencebutton");
    if (conferenceButton)
	conferenceButton->hide();

    if (m_id == "events") {
	QtWindow* win = (QtWindow*) QtClient::getWindow("mainwindow");
	if (win && m_visible) {
	    QCheckBox* check = qFindChild<QCheckBox*>(win, "events");
	    if (check)
		check->setChecked(true);
	}
    }

    if (m_id != "mainwindow" && m_id != "events" && m_id != "help")
	setWindowModality(Qt::WindowModal);

    QComboBox* callTo = qFindChild<QComboBox*>(this, "callto");

    QPushButton* hangup = qFindChild<QPushButton*>(this, "hangup");
    if (callTo && hangup)
	connect(hangup, SIGNAL(clicked()), this, SLOT(focus()));

    QComboBox* protocol = qFindChild<QComboBox*>(this,"protocol");
    QComboBox* account = qFindChild<QComboBox*>(this, "account");

    if (protocol && account) {
	connect(protocol, SIGNAL(activated(int)), this, SLOT(disableCombo()));
	connect(account, SIGNAL(activated(int)), this, SLOT(disableCombo()));
    }

    QList<QComboBox*> combos = qFindChildren<QComboBox*>(this);
    if (!combos.empty())
	for (int i = 0; i < combos.size(); i++)
	    if (combos[i]->objectName() != "callto")
		connect(combos[i], SIGNAL(activated(int)), this, SLOT(action()));

    QList<QPushButton*> buttons = qFindChildren<QPushButton*>(this);
    if (!buttons.empty())
	for(int i = 0; i < buttons.size(); i++)
	    connect(buttons[i], SIGNAL(clicked()), this, SLOT(action()));

    QList<QCheckBox*> checkBoxes = qFindChildren<QCheckBox*>(this);
    if (!checkBoxes.empty())
	for(int i = 0; i < checkBoxes.size(); i++)
	    connect(checkBoxes[i], SIGNAL(toggled(bool)), this, SLOT(action()));

    QList<QRadioButton*> radios = qFindChildren<QRadioButton*>(this);
    if (!radios.empty())
	for(int i = 0; i < radios.size(); i++)
	    connect(radios[i], SIGNAL(clicked(bool)), this, SLOT(action()));

//debug alterations
    QCheckBox* debug =  qFindChild<QCheckBox*>(this, "debug");
    if (debug)
	connect(debug, SIGNAL(toggled(bool)), this, SLOT(enableDebugOptions(bool)));

    QCheckBox* debugFileOption = qFindChild<QCheckBox*>(this, "debugtofile");
    if (debugFileOption)
    	connect(debugFileOption, SIGNAL(toggled(bool)), this, SLOT(enableFileChoosing(bool)));
	
    QPushButton* chooseFile = qFindChild<QPushButton*>(this, "chooseFile");
    if (chooseFile)
	connect(chooseFile, SIGNAL(clicked()), this, SLOT(chooseFile()));

//
// volume control alterations
    QList<QSlider*> sliders = qFindChildren<QSlider*>(this);
    if (!sliders.empty())
	for (int i = 0; i < sliders.size(); i++)
	    connect(sliders[i], SIGNAL(valueChanged(int)), this, SLOT(select(int)));

// accept button behaviour changed
    QPushButton* accept = qFindChild<QPushButton*>(this, "accept");
    if (accept)
	setShow("accept", false);

// putting in the callto the number selected from a log
    QList<QTableWidget*> tables = qFindChildren<QTableWidget*>(this);
    if (!tables.empty()) {
	
	for (int i = 0; i < tables.size(); i++)
	    connect(tables[i], SIGNAL(itemDoubleClicked(QTableWidgetItem*)), this, SLOT(selectionToCallto()));
	for (int i = 0; i < tables.size(); i++)
	    for (int j = 0; j < tables[i]->columnCount(); j++) {
	//	tables[i]->setColumnWidth(j, 60);
		if (j == 0) tables[i]->setColumnWidth(j, 90);
		if (j == 1) tables[i]->setColumnWidth(j, 50);
		if (j == 2) tables[i]->setColumnWidth(j, 60);
	    }
    }

    m_ringtone->setLoops(-1);

    qRegisterMetaType<QModelIndex>("QModelIndex");
    qRegisterMetaType<QTextCursor>("QTextCursor");

    if (m_visible)
	show();
}

void QtWindow::setVisible(bool visible)
{
    if (visible) {
	QWidget::move(m_x, m_y);
	resize(m_width, m_height);	
    }
    else {
	QPoint point = pos();
	m_x = point.x();
	m_y = point.y();
	m_width = width();
	m_height = height();
    }
    QWidget::setVisible(visible);
}

void QtWindow::show()
{
    setVisible(true);
}

void QtWindow::hide()
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::hide()");

    QList<QComboBox*> boxes = qFindChildren<QComboBox*>(this);
    if (!boxes.isEmpty())
	for (int i = 0; i < boxes.size(); i++)
	    boxes[i]->setCurrentIndex(0);

    if (m_id == "events") {
	QtWindow* mainWind = (QtWindow*) Client::getWindow("mainwindow");
	QCheckBox* checkBox = qFindChild<QCheckBox*>(mainWind, "events");
	if (checkBox)
	    checkBox->setChecked(false);
    }

    setVisible(false);
}

void QtWindow::size(int width, int height)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::size() width : %d, height : %d", width, height);
}

void QtWindow::move(int x, int y)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::move() x : %d, y : %d", x, y);
}

void QtWindow::moveRel(int dx, int dy)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::moveRel() dx : %d, dy : %d", dx, dy);
}

bool QtWindow::related(const Window* wnd) const
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::related() wnd : [%p]", wnd);
    return false;
}

void QtWindow::menu(int x, int y)
{
    DDebug(QtDriver::self(), DebugInfo, "QtWindow::menu() x : %d, y : %d", x, y);
}


QtClient::QtClient()
	: Client("QtClient")
{
    m_oneThread = Engine::config().getBoolValue("client","onethread",true);

    s_skinPath = Engine::config().getValue("client","skinbase");
    if (s_skinPath.null())
	s_skinPath << Engine::modulePath() << Engine::pathSeparator() << "skin";
	s_skinPath << Engine::pathSeparator();
    String skin(Engine::config().getValue("client","skin","default")); 

    if (skin)
	s_skinPath << skin;
    if (!s_skinPath.endsWith(Engine::pathSeparator()))
	s_skinPath << Engine::pathSeparator();
    s_cfg = s_skinPath + "qt4client.rc";
    s_cfg.load();
    s_save = Engine::configFile("qt4client",true);
    s_save.load();
}

QtClient::~QtClient()
{
   // m_windows.clear();
    s_save.save();
    m_app->quit();
    delete m_app;
}

void QtClient::run()
{
    int argc = 0;
    char* argv =  0;
    m_app = new QApplication(argc, &argv);

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
    Debug(QtDriver::self(), DebugInfo, "QtClient::allHiden()");
    for (ObjList* windows = listWindows(); windows; windows = windows->next()) {
	String* str = static_cast<String*>(windows->get());
	QtWindow* win = (QtWindow*) Client::getWindow(str);
	if (win)
	    win->hide();
    }
}

bool QtClient::createWindow(const String& name)
{
    Debug(QtDriver::self(), DebugInfo, "Creating window %s", name.c_str());
    QtWindow* w;
    if ((w = new QtWindow(name, s_skinPath + s_cfg.getValue(name, "description")))) {
	w->populate();
	m_windows.append(w);
	return true;
    }
    else
	Debug(QtDriver::self(), DebugGoOn, "Could not create window %s ", name.c_str());
    return false;

}

void QtClient::loadWindows()
{
    Debug(QtDriver::self(), DebugInfo, "Loading Windows");
    unsigned int n = s_cfg.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* l = s_cfg.getSection(i);
	if (l && l->getBoolValue("enabled",true))
	    createWindow(*l);
    }
    QtWindow* win = (QtWindow*) getWindow("mainwindow");
    if (win) {
	QTimer* timer = new QTimer(win);
	QObject::connect(timer, SIGNAL(timeout()), win, SLOT(idleActions()));
	timer->start(1);
    }
}

bool QtClient::action(Window* wnd, const String& name)
{
    String tmp = name;
    if (tmp.startSkip("openurl:",false))
	return QDesktopServices::openUrl(QUrl(tmp.safe()));
    return Client::action(wnd,name);
}

QtDriver::QtDriver()
{
}

QtDriver::~QtDriver()
{
}

void QtDriver::initialize()
{
    Output("Initializing module Qt4 client");
    s_device = Engine::config().getValue("client","device",DEFAULT_DEVICE);
    if (!QtClient::self())
    {
	debugCopy();
	new QtClient;
	QtClient::self()->startup();
    }
    setup();
}

/* vi: set ts=8 sw=4 sts=4 noet: */
