/**
 * customtext.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Custom text edit objects
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

#include "customtext.h"

using namespace TelEngine;
namespace { // anonymous

// The factory
class CustomTextFactory : public UIFactory
{
public:
    inline CustomTextFactory(const char* name = "CustomFactory")
	: UIFactory(name)
	{ m_types.append(new String("CustomTextEdit")); }
    virtual void* create(const String& type, const char* name, NamedList* params = 0);
};

// Scroll an area to the end if has a vertical scroll bar
class ScrollToEnd
{
public:
    inline ScrollToEnd(QAbstractScrollArea* area)
	: m_area(area)
	{}
    inline ~ScrollToEnd() {
	    QScrollBar* bar = m_area ? m_area->verticalScrollBar() : 0;
	    if (bar)
		bar->setSliderPosition(bar->maximum());
	}
private:
    QAbstractScrollArea* m_area;
};

static CustomTextFactory s_factory;
// Global list of URL handlers
static NamedList s_urlHandlers("");


// Check if a char is a word break one (including NULL)
static inline bool isWordBreak(char c)
{
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n' || !c);
}

// Check if a char should be ignored from URL end (including NULL)
static inline bool isIgnoreUrlEnd(char c)
{
    return (c == '.' || c == ';' || c == ':' || c == '?' || c == '!');
}

// Move a cursor at document start/end.
// Adjust position by 'blocks' count
// Select if required and blocks is not 0
static void moveCursor(QTextCursor& c, bool atStart, int blocks = 0,
    bool select = false)
{
    c.movePosition(!atStart ? QTextCursor::End : QTextCursor::Start);
    if (!blocks)
	return;
    c.movePosition(!atStart ? QTextCursor::PreviousBlock : QTextCursor::NextBlock,
	select ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor,
	blocks > 0 ? blocks : -blocks);
}

/*
 * CustomTextFormat
 */
// Constructor. Build a Block type
CustomTextFormat::CustomTextFormat(const String& id, const char* color, const char* bgcolor)
    : NamedString(id),
    m_type(Block), m_blockFormat(0), m_charFormat(0)
{
    m_blockFormat = new QTextBlockFormat;
    if (bgcolor)
	m_blockFormat->setBackground(QColor(bgcolor));
    m_charFormat = new QTextCharFormat;
    if (color)
	m_charFormat->setForeground(QColor(color));
}

// Constructor. Build a Html/Plain type
CustomTextFormat::CustomTextFormat(const String& id, const char* value, bool html)
    : NamedString(id,value),
    m_type(html ? Html : Plain), m_blockFormat(0), m_charFormat(0)
{
}

CustomTextFormat::~CustomTextFormat()
{
    if (m_blockFormat)
	delete m_blockFormat;
    if (m_charFormat)
	delete m_charFormat;
}

// Add/insert text into an edit widget
int CustomTextFormat::insertText(QTextEdit* edit, const String& text, bool atStart, int blocks)
{
    QTextDocument* doc = edit ? edit->document() : 0;
    if (!doc)
	return 0;
    QTextCursor c(doc);
    moveCursor(c,atStart,blocks,false);
    int oldBlocks = doc->blockCount();
    c.insertBlock();
    c.movePosition(QTextCursor::PreviousBlock,QTextCursor::MoveAnchor);
    // Insert text
    if (type() == Html)
	c.insertHtml(QtClient::setUtf8(text));
    else {
	if (m_blockFormat)
	    c.setBlockFormat(*m_blockFormat);
	if (m_charFormat)
	    c.setCharFormat(*m_charFormat);
	c.insertText(QtClient::setUtf8(text));
    }
    return doc->blockCount() - oldBlocks;
}

// Set text from value. Replace text parameters if not empty
void CustomTextFormat::buildText(String& text, const NamedList* params,
    CustomTextEdit* owner, bool lineBrBefore)
{
    if (null())
	return;
    if (lineBrBefore)
	text = ((type() == Html) ? "<br>" : "\r\n");
    text << *this;
    NamedList dummy("");
    const NamedList* repl = &dummy;
    if (params) {
	// Escape or replace HTML markups.
	// Make a copy of the input list if we are going to change it
	if (type() == Html) {
	    dummy = *params;
	    unsigned int n = dummy.length();
	    for (unsigned int i = 0; i < n; i++) {
		String* s = dummy.getParam(i);
		if (!TelEngine::null(s)) {
		    Client::plain2html(*s);
		    if (owner)
			owner->replace(*s);
		}
	    }
	}
	else
	    repl = params;
    }
    repl->replaceParams(text);
}


/*
 * TextFragmentList
 */
// Restore this list in the document
void TextFragmentList::restore(QTextDocument* doc)
{
    if (doc) {
	for (int i = 0; i < m_list.size(); i++) {
	    QTextCursor c(doc);
	    c.movePosition(QTextCursor::NextCharacter,QTextCursor::MoveAnchor,
		m_list[i].m_docPos);
	    c.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor,
		m_list[i].toPlainText().length());
	    c.removeSelectedText();
	    c.insertHtml(m_list[i].toHtml());
	}
    }
    m_list.clear();
};


/*
 * CustomTextEdit
 */
// Constructor
CustomTextEdit::CustomTextEdit(const char* name, const NamedList& params, QWidget* parent)
    : QtCustomWidget(name,parent),
    m_edit(0),
    m_debug(false),
    m_items(""),
    m_defItem(String::empty(),"",false),
    m_followUrl(true),
    m_urlHandlers(""),
    m_tempItemCount(0),
    m_tempItemReplace(true),
    m_lastFoundPos(-1)
{
    // Build properties
    QtClient::buildProps(this,params["buildprops"]);
    m_edit = new QTextBrowser(this);
    m_edit->setObjectName(params.getValue("textedit_name",this->name() + "_textedit"));
    m_edit->setOpenLinks(false);
    m_edit->setOpenExternalLinks(false);
    m_edit->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    QtClient::setWidget(this,m_edit);
    m_searchFoundFormat.setBackground(QBrush(QColor("darkgreen")));
    m_searchFoundFormat.setForeground(QBrush(QColor("white")));
    m_debug = params.getBoolValue("_yate_debug_widget");
    if (m_debug) {
	m_items.addParam(new CustomTextFormat(String(-1),"white"));       // Output() or client set status
	m_items.addParam(new CustomTextFormat(String(0),"yellow","red")); // DebugFail - blinking yellow on red
	m_items.addParam(new CustomTextFormat(String(1),"yellow","red")); // Unnamed   - yellow on red
	m_items.addParam(new CustomTextFormat(String(2),"white","red"));  // DebugGoOn - white on red
	m_items.addParam(new CustomTextFormat(String(3),"lightgrey","red")); // DebugConf - gray on red
	m_items.addParam(new CustomTextFormat(String(4),"red"));          // DebugStub - red on black
	m_items.addParam(new CustomTextFormat(String(5),"orangered"));    // DebugWarn - light red on black
	m_items.addParam(new CustomTextFormat(String(6),"yellow"));       // DebugMild - yellow on black
	m_items.addParam(new CustomTextFormat(String(7),"white"));        // DebugCall - white on black
	m_items.addParam(new CustomTextFormat(String(8),"lightgreen"));   // DebugNote - light green on black
	m_items.addParam(new CustomTextFormat(String(9),"cyan"));         // DebugInfo - light cyan on black
	m_items.addParam(new CustomTextFormat(String(10),"teal"));        // DebugAll  - cyan on black
    }
    setParams(params);
    // Connect signals
    QtClient::connectObjects(m_edit,SIGNAL(anchorClicked(const QUrl&)),this,SLOT(urlTrigerred(const QUrl&)));
}

// Set parameters
bool CustomTextEdit::setParams(const NamedList& params)
{
    static const String s_setRichItem = "set_richtext_item";
    static const String s_setPlainItem = "set_plaintext_item";
    static const String s_search = "search";
    unsigned int n = params.length();
    bool ok = true;
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = params.getParam(i);
	if (!(ns && ns->name()))
	    continue;
	if (ns->name() == s_setRichItem)
	    setItem(*ns,true);
	else if (ns->name() == s_setPlainItem)
	    setItem(*ns,false);
	else if (ns->name() == s_search)
	    ok = setSearchHighlight(ns->toBoolean(),YOBJECT(NamedList,ns)) && ok;
	else {
	    // Prefixed parameters
	    String tmp(ns->name());
	    if (tmp.startSkip("set_url_handler:",false)) {
		// Set handler from prefix[{scheme}]=formatting_template
		if (!tmp)
		    continue;
		if (!m_urlHandlers.c_str())
		    m_urlHandlers.assign(s_urlHandlers.c_str());
		// Check for optional scheme
		int pos = tmp.find('{');
		if (pos <= 0 || tmp[tmp.length() - 1] != '}')
		    m_urlHandlers.setParam(new CustomTextEditUrl(tmp,*ns));
		else
		    m_urlHandlers.setParam(new CustomTextEditUrl(tmp.substr(0,pos),*ns,
			tmp.substr(pos + 1,tmp.length() - pos - 2)));
	    }
	    else if (tmp.startSkip("property:",false)) {
		QObject* target = m_edit;
		if (tmp.startSkip(name() + ":",false))
		    target = this;
		if (!QtClient::setProperty(target,tmp,*ns))
		    ok = false;
	    }
	}
    }
    return ok;
}

// Append or insert text lines to this widget
bool CustomTextEdit::addLines(const NamedList& lines, unsigned int max, bool atStart)
{
    unsigned int n = lines.length();
    if (!n)
	return true;
    ScrollToEnd scroll(m_edit);
    // Remove the temporary item(s)
    if (m_tempItemCount && m_tempItemReplace) {
	removeBlocks(m_tempItemCount);
	m_tempItemCount = 0;
    }
    if (!m_debug) {
	String text;
	CustomTextFormat* last = 0;
	// Line format: item=
	// Each parameter may contain an optional list of parameters to be replaced in item
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* ns = lines.getParam(i);
	    if (!ns)
		continue;
	    CustomTextFormat* crt = find(ns->name());
	    if (!crt)
		crt = &m_defItem;
	    if (last && last->type() != crt->type() && text) {
		// Format changed: insert text now and reset it
		insert(*last,text,atStart);
		text.clear();
	    }
	    last = crt;
	    if (last != &m_defItem) {
		String tmp;
		last->buildText(tmp,YOBJECT(NamedList,ns),this,!text.null());
		text << tmp;
	    }
	    else
		text << ns->name();
	}
	if (last && text)
	    insert(*last,text,atStart);
    }
    else {
	// Handle 'max'
	QTextDocument* doc = m_edit->document();
	if (doc)
	    doc->setMaximumBlockCount((int)max);
	// Line format: text=debuglevel
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* ns = lines.getParam(i);
	    if (!ns)
		continue;
	    CustomTextFormat* f = find(*ns);
	    // Use default output if not found
	    if (!f)
		f = find(String("-1"));
	    if (f) {
		// Ignore CR, LF or CR/LF at text end: we are adding a block
		unsigned int n = 0;
		if (ns->name().endsWith("\r\n"))
		    n = 2;
		else if (ns->name().length()) {
		    int pos = ns->name().length() - 1;
		    if (ns->name()[pos] == '\r' || ns->name()[pos] == '\n')
			n = 1;
		}
		if (n)
		    insert(*f,ns->name().substr(0,ns->name().length() - n),atStart);
		else
		    insert(*f,ns->name(),atStart);
	    }
	}
    }
    return true;
}

// Set the displayed text of this widget
bool CustomTextEdit::setText(const String& text, bool richText)
{
    ScrollToEnd scroll(m_edit);
    m_edit->clear();
    if (richText)
	m_edit->insertHtml(QtClient::setUtf8(text));
    else
	m_edit->insertPlainText(QtClient::setUtf8(text));
    return true;
}

// Retrieve the displayed text of this widget
bool CustomTextEdit::getText(String& text, bool richText)
{
    if (richText)
	QtClient::getUtf8(text,m_edit->toHtml());
    else
	QtClient::getUtf8(text,m_edit->toPlainText());
    return true;
}

// Add/change/clear a pre-formatted item (item must be name[:[value])
void CustomTextEdit::setItem(const String& value, bool html)
{
    if (!value)
	return;
    int pos = value.find(':');
    if (pos > 0 && pos != (int)value.length() - 1) {
	String id = value.substr(0,pos);
	String val = value.substr(pos + 1);
	CustomTextFormat* f = find(id);
	// Remove existing if format changes
	if (f && ((html && f->type() != CustomTextFormat::Html) ||
	    (!html && f->type() == CustomTextFormat::Plain))) {
	    m_items.clearParam(f);
	    f = 0;
	}
	if (!f)
	    m_items.addParam(new CustomTextFormat(id,val,html));
	else
	    f->assign(val);
    }
    else if (pos < 0)
	m_items.clearParam(value);
    else if (pos > 0)
	m_items.clearParam(value.substr(0,pos));
}

// Set/reset text highlight
bool CustomTextEdit::setSearchHighlight(bool on, NamedList* params)
{
    if (!on) {
	m_lastFoundPos = -1;
	if (params && params->getBoolValue("reset",true))
	    m_searchFound.restore(m_edit->document());
	else
	    m_searchFound.m_list.clear();
	return true;
    }
    if (!params)
	return false;
    QTextDocument* doc = m_edit->document();
    if (!doc)
	return false;
    QString find = QtClient::setUtf8(params->getValue("find"));
    if (!find.length())
	return false;
    Qt::CaseSensitivity cs = params->getBoolValue("matchcase") ?
	Qt::CaseSensitive : Qt::CaseInsensitive;
    bool found = false;
    QString text = doc->toPlainText();
    if (params->getBoolValue("all")) {
	m_lastFoundPos = -1;
	m_searchFound.restore(doc);
	int pos = -1;
	do {
	    pos = text.indexOf(find,pos + 1,cs);
	    if (pos >= 0)
		handleFound(pos,find.length());
	}
	while (pos >= 0);
	if (m_searchFound.m_list.size()) {
	    found = true;
	    ensureCharVisible(m_searchFound.m_list[0].m_docPos);
	}
    }
    else {
	if (params->getBoolValue("next"))
	    m_lastFoundPos = text.indexOf(find,m_lastFoundPos >= 0 ? m_lastFoundPos + 1 : 0,cs);
	else if (m_lastFoundPos < 0)
	    m_lastFoundPos = text.lastIndexOf(find,-1,cs);
	else if (m_lastFoundPos)
	    m_lastFoundPos = text.lastIndexOf(find,m_lastFoundPos - 1,cs);
	if (m_lastFoundPos >= 0) {
	    found = true;
	    m_searchFound.restore(doc);
	    handleFound(m_lastFoundPos,find.length());
	    ensureCharVisible(m_lastFoundPos);
	}
    }
    return found;
}

// Ensure the character at a given position is visible
void CustomTextEdit::ensureCharVisible(int pos)
{
    QTextCursor show(m_edit->document());
    show.movePosition(QTextCursor::NextCharacter,QTextCursor::MoveAnchor,pos);
    m_edit->setTextCursor(show);
    m_edit->ensureCursorVisible();
}

// Replace string sequences with formatted text
void CustomTextEdit::replace(String& text)
{
    if (!text)
	return;
    // Replace URLs ?
    if (m_followUrl) {
	const NamedList& urls = m_urlHandlers.c_str() ? m_urlHandlers : s_urlHandlers;
	unsigned int n = urls.length();
	for (int start = 0; start < (int)text.length();) {
	    int len = 1;
	    for (unsigned int i = 0; i < n; i++) {
		const CustomTextEditUrl* ns = static_cast<CustomTextEditUrl*>(urls.getParam(i));
		// Parameter name is the URL prefix
		if (!(ns && ns->name()))
		    continue;
		if (ns->name().length() >= text.length() - start)
		    continue;
		// Get html template from parameter value or list name
		const char* templ = *ns ? ns->c_str() : urls.c_str();
		if (TelEngine::null(templ))
		    continue;
		// Check for prefix match
		if (::strncmp(text.c_str() + start,ns->name().c_str(),ns->name().length()))
		    continue;
		// Detect url end
		int end = start + (int)ns->name().length();
		while (!isWordBreak(text[end]))
		    end++;
		// Go back 1 char if the last one should be ignored
		if ((end > start + (int)ns->name().length()) && isIgnoreUrlEnd(text[end - 1]))
		    end--;
		len = end - start;
		// Replace the URL if have something after prefix
		if (len <= (int)ns->name().length()) {
		    len++;
		    break;
		}
		// Check if we have a scheme to prepend for this one
		String url = text.substr(start,len);
		NamedList p("");
		p.addParam("url-display",url);
		p.addParam("url",ns->m_scheme ? (ns->m_scheme + url) : url);
		String u = templ;
		p.replaceParams(u);
		text = text.substr(0,start) + u + text.substr(end);
		len = (int)u.length();
		break;
	    }
	    start += len;
	}
    }
}

// Insert text using a given format. Update temporary item length if appropriate
void CustomTextEdit::insert(CustomTextFormat& fmt, const String& text, bool atStart)
{
    int n = fmt.insertText(m_edit,text,atStart,m_tempItemReplace ? 0 : m_tempItemCount);
    if (m_tempItemName != fmt.toString()) {
	// Reset counter if temporary item was replaced
	if (m_tempItemReplace)
	    m_tempItemCount = 0;
    }
    else
	m_tempItemCount = !atStart ? n : -n;
}

// Remove blocks from edit widget
void CustomTextEdit::removeBlocks(int blocks)
{
    if (!blocks)
	return;
    QTextDocument* doc = m_edit->document();
    if (!doc)
	return;
    QTextCursor c(doc);
    moveCursor(c,blocks < 0,blocks,true);
    c.removeSelectedText();
}

// URL clicked notification
void CustomTextEdit::urlTrigerred(const QUrl& url)
{
    if (!(m_followUrl && Client::valid()))
	return;
    String tmp;
    QtClient::getUtf8(tmp,url.toString());
    XDebug(ClientDriver::self(),DebugAll,"CustomTextEdit(%s)::urlTrigerred(%s)",
	name().c_str(),tmp.c_str());
    Client::self()->openUrl(tmp);
}

// Handle found item. Add data to found items. Set formatting
void CustomTextEdit::handleFound(int pos, int len)
{
    QTextCursor c(m_edit->document());
    c.movePosition(QTextCursor::NextCharacter,QTextCursor::MoveAnchor,pos);
    c.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor,len);
    m_searchFound.add(c);
    QString sel = c.selectedText();
    c.removeSelectedText();
    c.insertText(sel,m_searchFoundFormat);
}


/*
 * CustomTextFactory
 */
// Build objects
void* CustomTextFactory::create(const String& type, const char* name, NamedList* params)
{
    // Init URL handlers
    if (!s_urlHandlers.c_str()) {
	s_urlHandlers.assign("<a href=\"${url}\"><span style=\"text-decoration: underline; color:#0000ff;\">${url-display}</span></a>");
	s_urlHandlers.addParam(new CustomTextEditUrl("http://"));
	s_urlHandlers.addParam(new CustomTextEditUrl("https://"));
	s_urlHandlers.addParam(new CustomTextEditUrl("www.","","http://"));
    }
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
    if (type == "CustomTextEdit")
	return new CustomTextEdit(name,*params,parentWidget);
    return 0;
}

}; // anonymous namespace

#include "customtext.moc"

/* vi: set ts=8 sw=4 sts=4 noet: */
