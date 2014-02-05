/**
 * customtext.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Custom text edit objects
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

#ifndef __CUSTOMTEXT_H
#define __CUSTOMTEXT_H

#include "qt4client.h"

using namespace TelEngine;
namespace { // anonymous

class CustomTextFormat;                  // Custom QTextEdit format entry
class CustomTextEditUrl;                 // Custom text edit url
class TextFragment;                      // A formatted text document fragment
class TextFragmentList;                  // A text fragment container
class CustomTextEdit;                    // Custom QTextEdit

/**
 * Implements interfaces used to add/insert text into a CustomTextEdit widget
 * The value of the NamedString may contain a template used to replace parameters
 * @short A custom QTextEdit format entry
 */
class CustomTextFormat : public NamedString
{
    YCLASS(CustomTextFormat,NamedString)
public:
    /**
     * Text format type enumeration
     */
    enum Type {
	Html,                            // HTML formatted text
	Plain,                           // Plain text
	Block,                           // Use QT format class(es)
    };

    /**
     * Constructor. Build a Block type
     */
    CustomTextFormat(const String& id, const char* color, const char* bgcolor = 0);

    /**
     * Constructor. Build a Html/Plain type
     */
    CustomTextFormat(const String& id, const char* value, bool html);

    /**
     * Destructor
     */
    virtual ~CustomTextFormat();

    /**
     * Retrieve this object's type
     */
    inline Type type() const
	{ return m_type; }

    /**
     * Add/insert text into an edit widget
     * @param edit Edit widget
     * @param text Text buffer
     * @param atStart True to insert at start, false to append
     * @param blocks The number of blocks to skip if inserted at start or insert before if added
     * @return The number of blocks added
     */
    int insertText(QTextEdit* edit, const String& text, bool atStart, int blocks);

    /**
     * Set text from value. Replace text parameters if not empty
     * @param text Text buffer
     * @param params Parameters to replace
     * @param owner Text edit owner
     * @param lineBrBefore True to append a libe break before it
     */
    void buildText(String& text, const NamedList* params, CustomTextEdit* owner,
	bool lineBrBefore);

private:
    Type m_type;
    QTextBlockFormat* m_blockFormat;
    QTextCharFormat* m_charFormat;
};

/**
 * This class holds an url definition with an optional scheme
 * NamedString's value may contain optional formatting template
 * @short Custom text edit url
 */
class CustomTextEditUrl : public NamedString
{
public:
    inline CustomTextEditUrl(const char* name, const char* value = 0, const char* scheme = 0)
	: NamedString(name,value),
	m_scheme(scheme)
	{}
    String m_scheme;
};

/**
 * This class keeps a formatted text document fragment along
 *  with document position
 * @short A formatted text document fragment
 */
class TextFragment : public QTextDocumentFragment
{
public:
    /**
     * Constructor. Build a text fragment from a cursor's selection
     * @param c The cursor
     */
    inline TextFragment(const QTextCursor& c)
	: QTextDocumentFragment(c),
	m_docPos(c.selectionStart())
	{}

    /**
     * Copy constructor
     * @param other Source text fragment
     */
    inline TextFragment(const TextFragment& other)
	: QTextDocumentFragment(other), m_docPos(other.m_docPos)
	{}

    /**
     * The position of this fragment in the document
     */
    int m_docPos;

private:
    TextFragment() {};
};

/**
 * This class implements a TextFragment container
 * @short A text fragment container
 */
class TextFragmentList
{
public:
    /**
     * Restore all fragments in the document. Clear the list
     * @param doc The document
     */
    void restore(QTextDocument* doc);

    /**
     * Build and append a text fragment from a cursor's selection
     * @param c The cursor
     */
    inline void add(QTextCursor& c)
	{ m_list.append(TextFragment(c)); }

    /**
     * The fragments owned by this container
     */
    QList<TextFragment> m_list;
};

/**
 * This class holds custom text edit widget with abilities to add pre-formated
 *  parameterized text
 * @short A custom text edit widget
 */
class CustomTextEdit : public QtCustomWidget
{
    YCLASS(CustomTextEdit,QtCustomWidget)
    Q_CLASSINFO("CustomTextEdit","Yate")
    Q_OBJECT
    Q_PROPERTY(bool _yate_followurl READ followUrl WRITE setFollowUrl(bool))
    Q_PROPERTY(QString _yate_tempitemname READ tempItemName WRITE setTempItemName(QString))
    Q_PROPERTY(int _yate_tempitemcount READ tempItemCount WRITE setTempItemCount(int))
    Q_PROPERTY(bool _yate_tempitemreplace READ tempItemReplace WRITE setTempItemReplace(bool))
public:
    /**
     * Constructor
     * @param name Object name
     * @param params Object parameters
     * @param parent Optional parent
     */
    CustomTextEdit(const char* name, const NamedList& params, QWidget* parent);

    /**
     * Set parameters. Add text
     * @param params Parameter list
     * @return True on success
     */
    virtual bool setParams(const NamedList& params);

    /**
     * Clear the edit widget
     * @return True
     */
    virtual bool clearTable() {
	    m_edit->clear();
	    return true;
	}

    /**
     * Append or insert text lines to this widget
     * @param name The name of the widget
     * @param lines List containing the lines
     * @param max The maximum number of lines allowed to be displayed. Set to 0 to ignore
     * @param atStart True to insert, false to append
     * @return True on success
     */
    virtual bool addLines(const NamedList& lines, unsigned int max, bool atStart = false);

    /**
     * Set the displayed text of this widget
     * @param text Text value to set
     * @param richText True if the text contains format data
     * @return True on success
     */
    virtual bool setText(const String& text, bool richText = false);

    /**
     * Retrieve the displayed text of this widget
     * @param text Text value
     * @param richText True to retrieve formatted data
     * @return True on success
     */
    virtual bool getText(String& text, bool richText = false);

    /**
     * Add/change/clear a pre-formatted item (item must be name[:[value])
     * @param value Formatted item to set or clear
     * @param html True to add rich text, false to add plain text
     */
    void setItem(const String& value, bool html);

    /**
     * Set/reset search text highlight
     * @param on True to set, false to reset
     * @param params Parameters. Ignored it reset
     * @return True if reset or a match was found. False otherwise
     */
    bool setSearchHighlight(bool on, NamedList* params);

    /**
     * Ensure the character at a given position is visible
     * @param pos The position in the document
     */
    void ensureCharVisible(int pos);

    /**
     * Replace string sequences with formatted text
     * @param text Text buffer
     */
    void replace(String& text);

    /**
     * Insert text using a given format. Update temporary item length if appropriate
     * @param fmt Format to use
     * @param text Text to insert
     * @param atStart Insert at start or append
     */
    void insert(CustomTextFormat& fmt, const String& text, bool atStart);

    /**
     * Remove blocks from edit widget
     * @param blocks The number of blocks to remove, negative to remove from start
     */
    void removeBlocks(int blocks);

    /**
     * Retrieve the value of _yate_followurl property
     * @return The value of _yate_followurl property
     */
    bool followUrl()
	{ return m_followUrl; }

    /**
     * Set the value of _yate_followurl property
     * @param value The new value of _yate_followurl property
     */
    void setFollowUrl(bool value)
	{ m_followUrl = value; }

    /**
     * Retrieve the value of _yate_tempitemname property
     * @return The value of _yate_tempitemname property
     */
    QString tempItemName()
	{ return QtClient::setUtf8(m_tempItemName); }

    /**
     * Set the value of _yate_tempitemname property
     * @param value The new value of _yate_tempitemname property
     */
    void setTempItemName(QString value)
	{ QtClient::getUtf8(m_tempItemName,value); }

    /**
     * Retrieve the value of _yate_tempitemcount property
     * @return The value of _yate_tempitemcount property
     */
    int tempItemCount()
	{ return m_tempItemCount; }

    /**
     * Set the value of _yate_tempitemcount property
     * @param value The new value of _yate_tempitemcount property
     */
    void setTempItemCount(int value) {
	    if (!value && m_tempItemCount)
		removeBlocks(m_tempItemCount);
	    m_tempItemCount = value;
	}

    /**
     * Retrieve the value of _yate_tempitemreplace property
     * @return The value of _yate_tempitemreplace property
     */
    bool tempItemReplace()
	{ return m_tempItemReplace; }

    /**
     * Set the value of _yate_tempitemreplace property
     * @param value The new value of _yate_tempitemreplace property
     */
    void setTempItemReplace(bool value)
	{ m_tempItemReplace = value; }

public slots:
    /**
     * URL clicked notification
     * Use this slot instead of QT open external links:
     *  displayed text will be cleared if the link is not handled
     */
    void urlTrigerred(const QUrl& url);

protected:
    /**
     * Handle found item. Add data to found items. Set formatting
     * @param pos The position in document
     * @param len Found text length
     */
    void handleFound(int pos, int len);

    /**
     * Retrieve a custom text format object
     * @param name Item name
     * @return CustomTextFormat pointer or 0 if not found
     */
    inline CustomTextFormat* find(const String& name)
	{ return YOBJECT(CustomTextFormat,m_items.getParam(name)); }

    QTextBrowser* m_edit;                // The edit widget
    bool m_debug;                        // This is a debug widget
    NamedList m_items;                   // Formatted items
    CustomTextFormat m_defItem;          // Default text format used to add plain text
                                         //  when an item is not found
    bool m_followUrl;                    // Follow URLs
    NamedList m_urlHandlers;             // List specific URL handlers
    String m_tempItemName;               // Temporary last item name
    int m_tempItemCount;                 // Temporary last item count
                                         //  negative: start, positive: end, 0: none
    bool m_tempItemReplace;              // Replace (delete) temporary item(s)
    // Search
    TextFragmentList m_searchFound;      // Last found data: restore it on request
    QTextCharFormat m_searchFoundFormat; // Found item(s) formatting
    int m_lastFoundPos;                  // Last found position in document
};

}; // anonymous namespace

#endif // __CUSTOMTEXT_H

/* vi: set ts=8 sw=4 sts=4 noet: */
