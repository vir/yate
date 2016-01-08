/**
 * XML.cpp
 * This file is part of the YATE Project http://YATE.null.ro
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

#include <yatexml.h>
#include <string.h>

using namespace TelEngine;


const String XmlElement::s_ns = "xmlns";
const String XmlElement::s_nsPrefix = "xmlns:";
static const String s_type("type");
static const String s_name("name");


// Return a replacement char for the given string
char replace(const char* str, const XmlEscape* esc)
{
    if (!str)
	return 0;
    if (esc) {
	for (; esc->value; esc++)
	    if (!::strcmp(str,esc->value))
		return esc->replace;
    }
    return 0;
}

// Return a replacement string for the given char
const char* replace(char replace, const XmlEscape* esc)
{
    if (esc) {
	for (; esc->value; esc++)
	    if (replace == esc->replace)
		return esc->value;
    }
    return 0;
}

// XmlEscape a string or replace it if found in a list of restrictions
static inline void addAuth(String& buf, const String& comp, const String& value,
    bool esc, const String* auth)
{
    if (auth) {
	for (; !auth->null(); auth++)
	    if (*auth == comp) {
		buf << "***";
		return;
	    }
    }
    if (esc)
	XmlSaxParser::escape(buf,value);
    else
	buf << value;
}


/*
 * XmlSaxParser
 */
const TokenDict XmlSaxParser::s_errorString[] = {
	{"No error",                      NoError},
	{"Error",                         Unknown},
	{"Not well formed",               NotWellFormed},
	{"I/O error",                     IOError},
	{"Error parsing Element",         ElementParse},
	{"Failed to read Element name",   ReadElementName},
	{"Bad element name",              InvalidElementName},
	{"Error reading Attributes",      ReadingAttributes},
	{"Error reading end tag",         ReadingEndTag},
	{"Error parsing Comment",         CommentParse},
	{"Error parsing Declaration",     DeclarationParse},
	{"Error parsing Definition",      DefinitionParse},
	{"Error parsing CDATA",           CDataParse},
	{"Incomplete",                    Incomplete},
	{"Invalid encoding",              InvalidEncoding},
	{"Unsupported encoding",          UnsupportedEncoding},
	{"Unsupported version",           UnsupportedVersion},
	{0,0}
};

const XmlEscape XmlSaxParser::s_escape[] = {
	{"&lt;",   '<'},
	{"&gt;",   '>'},
	{"&amp;",  '&'},
	{"&quot;", '\"'},
	{"&apos;", '\''},
	{0,0}
};


XmlSaxParser::XmlSaxParser(const char* name)
    : m_offset(0), m_row(1), m_column(1), m_error(NoError),
    m_parsed(""), m_unparsed(None)
{
    debugName(name);
}

XmlSaxParser::~XmlSaxParser()
{
}

// Parse a given string
bool XmlSaxParser::parse(const char* text)
{
    if (TelEngine::null(text))
	return m_error == NoError;
#ifdef XDEBUG
    String tmp;
    m_parsed.dump(tmp," ");
    if (tmp)
	tmp = " parsed=" + tmp;
    XDebug(this,DebugAll,"XmlSaxParser::parse(%s) unparsed=%u%s buf=%s [%p]",
	text,unparsed(),tmp.safe(),m_buf.safe(),this);
#endif
    char car;
    setError(NoError);
    String auxData;
    m_buf << text;
    if (m_buf.lenUtf8() == -1) {
	//FIXME this should not be here in case we have a different encoding
	DDebug(this,DebugNote,"Request to parse invalid utf-8 data [%p]",this);
	return setError(Incomplete);
    }
    if (unparsed()) {
	if (unparsed() != Text) {
	    if (!auxParse())
		return false;
	}
	else
	    auxData = m_parsed;
	resetParsed();
	setUnparsed(None);
    }
    unsigned int len = 0;
    while (m_buf.at(len) && !error()) {
	car = m_buf.at(len);
	if (car != '<' ) { // We have a new child check what it is
	    if (car == '>' || !checkDataChar(car)) {
		Debug(this,DebugNote,"XML text contains unescaped '%c' character [%p]",
		    car,this);
		return setError(Unknown);
	    }
	    len++; // Append xml Text
	    continue;
	}
	if (len > 0) {
	    auxData << m_buf.substr(0,len);
	}
	if (auxData.c_str()) {  // We have an end of tag or another child is riseing
	    if (!processText(auxData))
		return false;
	    m_buf = m_buf.substr(len);
	    len = 0;
	    auxData = "";
	}
	char auxCar = m_buf.at(1);
	if (!auxCar)
	    return setError(Incomplete);
	if (auxCar == '?') {
	    m_buf = m_buf.substr(2);
	    if (!parseInstruction())
		return false;
	    continue;
	}
	if (auxCar == '!') {
	    m_buf = m_buf.substr(2);
	    if (!parseSpecial())
		return false;
	    continue;
	}
	if (auxCar == '/') {
	    m_buf = m_buf.substr(2);
	    if (!parseEndTag())
		return false;
	    continue;
	}
	// If we are here mens that we have a element
	// process an xml element
	m_buf = m_buf.substr(1);
	if (!parseElement())
	    return false;
    }
    // Incomplete text
    if ((unparsed() == None || unparsed() == Text) && (auxData || m_buf)) {
	if (!auxData)
	    m_parsed.assign(m_buf);
	else {
	    auxData << m_buf;
	    m_parsed.assign(auxData);
	}
	m_buf = "";
	setUnparsed(Text);
	return setError(Incomplete);
    }
    if (error()) {
	DDebug(this,DebugNote,"Got error while parsing %s [%p]",getError(),this);
	return false;
    }
    m_buf = "";
    resetParsed();
    setUnparsed(None);
    return true;
}

// Process incomplete text
bool XmlSaxParser::completeText()
{
    if (!completed() || unparsed() != Text || error() != Incomplete)
	return error() == NoError;
    String tmp = m_parsed;
    return processText(tmp);
}

// Parse an unfinished xml object
bool XmlSaxParser::auxParse()
{
    switch (unparsed()) {
	case Element:
	    return parseElement();
	case CData:
	    return parseCData();
	case Comment:
	    return parseComment();
	case Declaration:
	    return parseDeclaration();
	case Instruction:
	    return parseInstruction();
	case EndTag:
	    return parseEndTag();
	case Special:
	    return parseSpecial();
	default:
	    return false;
    }
}

// Set the error code and destroys a child if error code is not NoError
bool XmlSaxParser::setError(Error error, XmlChild* child)
{
    m_error = error;
    if (child && error)
	TelEngine::destruct(child);
    return m_error == XmlSaxParser::NoError;
}

// Parse an endtag form the main buffer
bool XmlSaxParser::parseEndTag()
{
    bool aux = false;
    String* name = extractName(aux);
    // We don't check aux flag because we don't look for attributes here
    if (!name) {
	if (error() && error() == Incomplete)
	    setUnparsed(EndTag);
	return false;
    }
    if (!aux || m_buf.at(0) == '/') { // The end tag has attributes or contains / char at the end of name
	setError(ReadingEndTag);
	Debug(this,DebugNote,"Got bad end tag </%s/> [%p]",name->c_str(),this);
	setUnparsed(EndTag);
	m_buf = *name + m_buf;
	return false;
    }
    resetError();
    endElement(*name);
    if (error()) {
	setUnparsed(EndTag);
	m_buf = *name + ">";
	TelEngine::destruct(name);
	return false;
    }
    m_buf = m_buf.substr(1);
    TelEngine::destruct(name);
    return true;
}

// Parse an instruction form the main buffer
bool XmlSaxParser::parseInstruction()
{
    XDebug(this,DebugAll,"XmlSaxParser::parseInstruction() buf len=%u [%p]",m_buf.length(),this);
    setUnparsed(Instruction);
    if (!m_buf.c_str())
	return setError(Incomplete);
    // extract the name
    String name;
    char c;
    int len = 0;
    if (!m_parsed) {
	bool nameComplete = false;
	bool endDecl = false;
	while (0 != (c = m_buf.at(len))) {
	    nameComplete = blank(c);
	    if (!nameComplete) {
		// Check for instruction end: '?>'
		if (c == '?') {
		    char next = m_buf.at(len + 1);
		    if (!next)
			return setError(Incomplete);
		    if (next == '>') {
			nameComplete = endDecl = true;
			break;
		    }
		}
		if (checkNameCharacter(c)) {
		    len++;
		    continue;
		}
		Debug(this,DebugNote,"Instruction name contains bad character '%c' [%p]",c,this);
		return setError(InvalidElementName);
	    }
	    // Blank found
	    if (len)
	        break;
	    Debug(this,DebugNote,"Instruction with empty name [%p]",this);
	    return setError(InvalidElementName);
	}
	if (!len) {
	    if (!endDecl)
		return setError(Incomplete);
	    // Remove instruction end from buffer
	    m_buf = m_buf.substr(2);
	    Debug(this,DebugNote,"Instruction with empty name [%p]",this);
	    return setError(InvalidElementName);
	}
	if (!nameComplete)
	    return setError(Incomplete);
	name = m_buf.substr(0,len);
	m_buf = m_buf.substr(!endDecl ? len : len + 2);
	if (name == YSTRING("xml")) {
	    if (!endDecl)
		return parseDeclaration();
	    resetParsed();
	    resetError();
	    setUnparsed(None);
	    gotDeclaration(NamedList::empty());
	    return error() == NoError;
	}
	// Instruction name can't be xml case insensitive
	if (name.length() == 3 && name.startsWith("xml",false,true)) {
	    Debug(this,DebugNote,"Instruction name '%s' reserved [%p]",name.c_str(),this);
	    return setError(InvalidElementName);
	}
    }
    else {
	name = m_parsed;
	resetParsed();
    }
    // Retrieve instruction content
    skipBlanks();
    len = 0;
    while (0 != (c = m_buf.at(len))) {
	if (c != '?') {
	    if (c == 0x0c) {
		setError(Unknown);
		Debug(this,DebugNote,"Xml instruction with unaccepted character '%c' [%p]",
		    c,this);
		return false;
	    }
	    len++;
	    continue;
	}
	char ch = m_buf.at(len + 1);
	if (!ch)
	    break;
	if (ch == '>') { // end of instruction
	    NamedString inst(name,m_buf.substr(0,len));
	    // Parsed instruction: remove instruction end from buffer and reset parsed
	    m_buf = m_buf.substr(len + 2);
	    resetParsed();
	    resetError();
	    setUnparsed(None);
	    gotProcessing(inst);
	    return error() == NoError;
	}
	len ++;
    }
    // If we are here mens that text has reach his bounds is an error or we need to receive more data
    m_parsed.assign(name);
    return setError(Incomplete);
}

// Parse a declaration form the main buffer
bool XmlSaxParser::parseDeclaration()
{
    XDebug(this,DebugAll,"XmlSaxParser::parseDeclaration() buf len=%u [%p]",m_buf.length(),this);
    setUnparsed(Declaration);
    if (!m_buf.c_str())
	return setError(Incomplete);
    NamedList dc("xml");
    if (m_parsed.count()) {
	dc.copyParams(m_parsed);
	resetParsed();
    }
    char c;
    skipBlanks();
    int len = 0;
    while (m_buf.at(len)) {
	c = m_buf.at(len);
	if (c != '?') {
	    skipBlanks();
	    NamedString* s = getAttribute();
	    if (!s) {
		if (error() == Incomplete)
		    m_parsed = dc;
		return false;
	    }
	    len = 0;
	    if (dc.getParam(s->name())) {
		Debug(this,DebugNote,"Duplicate attribute '%s' in declaration [%p]",
		    s->name().c_str(),this);
		TelEngine::destruct(s);
		return setError(DeclarationParse);
	    }
	    dc.addParam(s);
	    char ch = m_buf.at(len);
	    if (ch && !blank(ch) && ch != '?') {
		Debug(this,DebugNote,"No blanks between attributes in declaration [%p]",this);
		return setError(DeclarationParse);
	    }
	    skipBlanks();
	    continue;
	}
	if (!m_buf.at(++len))
	    break;
	char ch = m_buf.at(len);
	if (ch == '>') { // end of declaration
	    // Parsed declaration: remove declaration end from buffer and reset parsed
	    resetError();
	    resetParsed();
	    setUnparsed(None);
	    m_buf = m_buf.substr(len + 1);
	    gotDeclaration(dc);
	    return error() == NoError;
	}
	Debug(this,DebugNote,"Invalid declaration ending char '%c' [%p]",ch,this);
	return setError(DeclarationParse);
    }
    m_parsed.copyParams(dc);
    setError(Incomplete);
    return false;
}

// Parse a CData section form the main buffer
bool XmlSaxParser::parseCData()
{
    if (!m_buf.c_str()) {
	setUnparsed(CData);
	setError(Incomplete);
	return false;
    }
    String cdata = "";
    if (m_parsed.c_str()) {
	cdata = m_parsed;
	resetParsed();
    }
    char c;
    int len = 0;
    while (m_buf.at(len)) {
	c = m_buf.at(len);
	if (c != ']') {
	    len ++;
	    continue;
	}
	if (m_buf.substr(++len,2) == "]>") { // End of CData section
	    cdata += m_buf.substr(0,len - 1);
	    resetError();
	    gotCdata(cdata);
	    resetParsed();
	    if (error())
		return false;
	    m_buf = m_buf.substr(len + 2);
	    return true;
	}
    }
    cdata += m_buf;
    m_buf = "";
    setUnparsed(CData);
    int length = cdata.length();
    m_buf << cdata.substr(length - 2);
    if (length > 1)
	m_parsed.assign(cdata.substr(0,length - 2));
    setError(Incomplete);
    return false;
}

// Helper method to classify the Xml objects starting with "<!" sequence
bool XmlSaxParser::parseSpecial()
{
    if (m_buf.length() < 2) {
	setUnparsed(Special);
	return setError(Incomplete);
    }
    if (m_buf.startsWith("--")) {
	m_buf = m_buf.substr(2);
	if (!parseComment())
	    return false;
	return true;
    }
    if (m_buf.length() < 7) {
	setUnparsed(Special);
	return setError(Incomplete);
    }
    if (m_buf.startsWith("[CDATA[")) {
	m_buf = m_buf.substr(7);
	if (!parseCData())
	    return false;
	return true;
    }
    if (m_buf.startsWith("DOCTYPE")) {
	m_buf = m_buf.substr(7);
	if (!parseDoctype())
	    return false;
	return true;
    }
    Debug(this,DebugNote,"Can't parse unknown special starting with '%s' [%p]",
	m_buf.c_str(),this);
    setError(Unknown);
    return false;
}


// Extract from the given buffer an comment and check if is valid
bool XmlSaxParser::parseComment()
{
    String comment;
    if (m_parsed.c_str()) {
	comment = m_parsed;
	resetParsed();
    }
    char c;
    int len = 0;
    while (m_buf.at(len)) {
	c = m_buf.at(len);
	if (c != '-') {
	    if (c == 0x0c) {
		Debug(this,DebugNote,"Xml comment with unaccepted character '%c' [%p]",c,this);
		return setError(NotWellFormed);
	    }
	    len++;
	    continue;
	}
	if (m_buf.at(len + 1) == '-' && m_buf.at(len + 2) == '>') { // End of comment
	    comment << m_buf.substr(0,len);
	    m_buf = m_buf.substr(len + 3);
#ifdef DEBUG
	    if (comment.at(0) == '-' || comment.at(comment.length() - 1) == '-')
		DDebug(this,DebugInfo,"Comment starts or ends with '-' character [%p]",this);
	    if (comment.find("--") >= 0)
		DDebug(this,DebugInfo,"Comment contains '--' char sequence [%p]",this);
#endif
	    gotComment(comment);
	    resetParsed();
	    // The comment can apear anywhere sow SaxParser never
	    // sets an error when receive a comment
	    return true;
	}
	len++;
    }
    // If we are here we haven't detect the end of comment
    comment << m_buf;
    int length = comment.length();
    // Keep the last 2 charaters in buffer because if the input buffer ends
    // between "--" and ">"
    m_buf = comment.substr(length - 2);
    setUnparsed(Comment);
    if (length > 1)
	m_parsed.assign(comment.substr(0,length - 2));
    return setError(Incomplete);
}

// Parse an element form the main buffer
bool XmlSaxParser::parseElement()
{
    XDebug(this,DebugAll,"XmlSaxParser::parseElement() buf len=%u [%p]",m_buf.length(),this);
    if (!m_buf.c_str()) {
	setUnparsed(Element);
	return setError(Incomplete);
    }
    bool empty = false;
    if (!m_parsed.c_str()) {
	String* name = extractName(empty);
	if (!name) {
	    if (error() == Incomplete)
		setUnparsed(Element);
	    return false;
	}
#ifdef XML_STRICT
	// http://www.w3.org/TR/REC-xml/
	// Names starting with 'xml' (case insensitive) are reserved
	if (name->startsWith("xml",false,true)) {
	    Debug(this,DebugNote,"Element tag starts with 'xml' [%p]",this);
	    TelEngine::destruct(name);
	    return setError(ReadElementName);
	}
#endif
	m_parsed.assign(*name);
	TelEngine::destruct(name);
    }
    if (empty) { // empty flag means that the element does not have attributes
	// check if the element is empty
	bool aux = m_buf.at(0) == '/';
	if (!processElement(m_parsed,aux))
	    return false;
	if (aux)
	    m_buf = m_buf.substr(2); // go back where we were
	else
	    m_buf = m_buf.substr(1); // go back where we were
	return true;
    }
    char c;
    skipBlanks();
    int len = 0;
    while (m_buf.at(len)) {
	c = m_buf.at(len);
	if (c == '/' || c == '>') { // end of element declaration
	    if (c == '>') {
		if (!processElement(m_parsed,false))
		    return false;
		m_buf = m_buf.substr(1);
		return true;
	    }
	    if (!m_buf.at(++len))
		break;
	    char ch = m_buf.at(len);
	    if (ch != '>') {
		Debug(this,DebugNote,"Element attribute name contains '/' character [%p]",this);
		return setError(ReadingAttributes);
	    }
	    if (!processElement(m_parsed,true))
		return false;
	    m_buf = m_buf.substr(len + 1);
	    return true;
	}
	NamedString* ns = getAttribute();
	if (!ns) { // Attribute is invalid
	    if (error() == Incomplete)
		break;
	    return false;
	}
	if (m_parsed.getParam(ns->name())) {
	    Debug(this,DebugNote,"Duplicate attribute '%s' [%p]",ns->name().c_str(),this);
	    TelEngine::destruct(ns);
	    return setError(NotWellFormed);
	}
	XDebug(this,DebugAll,"Parser adding attribute %s='%s' to '%s' [%p]",
	    ns->name().c_str(),ns->c_str(),m_parsed.c_str(),this);
	m_parsed.setParam(ns);
	char ch = m_buf.at(len);
	if (ch && !blank(ch) && (ch != '/' && ch != '>')) {
	    Debug(this,DebugNote,"Element without blanks between attributes [%p]",this);
	    return setError(NotWellFormed);
	}
	skipBlanks();
    }
    setUnparsed(Element);
    return setError(Incomplete);
}

// Parse a doctype form the main buffer
bool XmlSaxParser::parseDoctype()
{
    if (!m_buf.c_str()) {
	setUnparsed(Doctype);
	setError(Incomplete);
	return false;
    }
    unsigned int len = 0;
    skipBlanks();
    while (m_buf.at(len) && !blank(m_buf.at(len)))
	len++;
    // Use a while() to break to the end
    while (m_buf.at(len)) {
	while (m_buf.at(len) && blank(m_buf.at(len)))
	    len++;
	if (len >= m_buf.length())
	   break;
	if (m_buf[len++] == '[') {
	    while (len < m_buf.length()) {
		if (m_buf[len] != ']') {
		    len ++;
		    continue;
		}
		if (m_buf.at(++len) != '>')
		    continue;
		gotDoctype(m_buf.substr(0,len));
		resetParsed();
		m_buf = m_buf.substr(len + 1);
		return true;
	    }
	    break;
	}
	while (len < m_buf.length()) {
	    if (m_buf[len] != '>') {
		len++;
		continue;
	    }
	    gotDoctype(m_buf.substr(0,len));
	    resetParsed();
	    m_buf = m_buf.substr(len + 1);
	    return true;
	}
	break;
    }
    setUnparsed(Doctype);
    return setError(Incomplete);
}

// Extract the name of tag
String* XmlSaxParser::extractName(bool& empty)
{
    skipBlanks();
    unsigned int len = 0;
    bool ok = false;
    empty = false;
    while (len < m_buf.length()) {
	char c = m_buf[len];
	if (blank(c)) {
	    if (checkFirstNameCharacter(m_buf[0])) {
		ok = true;
		break;
	    }
	    Debug(this,DebugNote,"Element tag starting with invalid char %c [%p]",
		m_buf[0],this);
	    setError(ReadElementName);
	    return 0;
	}
	if (c == '/' || c == '>') { // end of element declaration
	    if (c == '>') {
		if (checkFirstNameCharacter(m_buf[0])) {
		    empty = true;
		    ok = true;
		    break;
		}
		Debug(this,DebugNote,"Element tag starting with invalid char %c [%p]",
		    m_buf[0],this);
		setError(ReadElementName);
		return 0;
	    }
	    char ch = m_buf.at(len + 1);
	    if (!ch)
		break;
	    if (ch != '>') {
		Debug(this,DebugNote,"Element tag contains '/' character [%p]",this);
		setError(ReadElementName);
		return 0;
	    }
	    if (checkFirstNameCharacter(m_buf[0])) {
		empty = true;
		ok = true;
		break;
	    }
	    Debug(this,DebugNote,"Element tag starting with invalid char %c [%p]",
		m_buf[0],this);
	    setError(ReadElementName);
	    return 0;
	}
	if (checkNameCharacter(c))
	    len++;
	else {
	    Debug(this,DebugNote,"Element tag contains invalid char %c [%p]",c,this);
	    setError(ReadElementName);
	    return 0;
	}
    }
    if (ok) {
	String* name = new String(m_buf.substr(0,len));
	m_buf = m_buf.substr(len);
	if (!empty) {
	    skipBlanks();
	    empty = (m_buf && m_buf[0] == '>') ||
		(m_buf.length() > 1 && m_buf[0] == '/' && m_buf[1] == '>');
	}
	return name;
    }
    setError(Incomplete);
    return 0;
}

// Extract an attribute
NamedString* XmlSaxParser::getAttribute()
{
    String name = "";
    skipBlanks();
    char c,sep = 0;
    unsigned int len = 0;

    while (len < m_buf.length()) { // Circle until we find attribute value startup character (["]|['])
	c = m_buf[len];
	if (blank(c) || c == '=') {
	    if (!name.c_str())
		name = m_buf.substr(0,len);
	    len++;
	    continue;
	}
	if (!name.c_str()) {
	    if (!checkNameCharacter(c)) {
		Debug(this,DebugNote,"Attribute name contains %c character [%p]",c,this);
		setError(ReadingAttributes);
		return 0;
	    }
	    len++;
	    continue;
	}
	if (c != '\'' && c != '\"') {
	    Debug(this,DebugNote,"Unenclosed attribute value [%p]",this);
	    setError(ReadingAttributes);
	    return 0;
	}
	sep = c;
	break;
    }

    if (!sep) {
	setError(Incomplete);
	return 0;
    }
    if (!checkFirstNameCharacter(name[0])) {
	Debug(this,DebugNote,"Attribute name starting with bad character %c [%p]",
	    name.at(0),this);
	setError(ReadingAttributes);
	return 0;
    }
    int pos = ++len;

    while (len < m_buf.length()) {
	c = m_buf[len];
	if (c != sep && !badCharacter(c)) {
	    len ++;
	    continue;
	}
	if (badCharacter(c)) {
	    Debug(this,DebugNote,"Attribute value with unescaped character '%c' [%p]",
		c,this);
	    setError(ReadingAttributes);
	    return 0;
	}
	NamedString* ns = new NamedString(name,m_buf.substr(pos,len - pos));
	m_buf = m_buf.substr(len + 1);
	// End of attribute value
	unEscape(*ns);
	if (error()) {
	    TelEngine::destruct(ns);
	    return 0;
	}
	return ns;
    }

    setError(Incomplete);
    return 0;
}

// Reset this parser
void XmlSaxParser::reset()
{
    m_offset = 0;
    m_row = 1;
    m_column = 1;
    m_error = NoError;
    m_buf.clear();
    resetParsed();
    m_unparsed = None;
}

// Verify if the given character is in the range allowed
bool XmlSaxParser::checkFirstNameCharacter(unsigned char ch)
{
    return ch == ':' || (ch >= 'A' && ch <= 'Z') || ch == '_' || (ch >= 'a' && ch <= 'z')
	|| (ch >= 0xc0 && ch <= 0xd6) || (ch >= 0xd8 && ch <= 0xf6) || (ch >= 0xf8);
}

// Check if the given character is in the range allowed for an xml char
bool XmlSaxParser::checkDataChar(unsigned char c)
{
    return  c == 0x9 || c == 0xA || c == 0xD || (c >= 0x20);
}

// Verify if the given character is in the range allowed for a xml name
bool XmlSaxParser::checkNameCharacter(unsigned char ch)
{
    return checkFirstNameCharacter(ch) || ch == '-' || ch == '.' || (ch >= '0' && ch <= '9')
	|| ch == 0xB7;
}

// Remove blank characters from the beginning of the buffer
void XmlSaxParser::skipBlanks()
{
    unsigned int len = 0;
    while (len < m_buf.length() && blank(m_buf[len]))
	len++;
    if (len != 0)
	m_buf = m_buf.substr(len);
}

// Obtain a char from an ascii decimal char declaration
inline unsigned char getDec(String& dec)
{
    if (dec.length() > 6) {
	DDebug(DebugNote,"Decimal number '%s' too long",dec.c_str());
	return 0;
    }
    int num = dec.substr(2,dec.length() - 3).toInteger(-1);
    if (num > 0 && num < 256)
	return num;
    DDebug(DebugNote,"Invalid decimal number '%s'",dec.c_str());
    return 0;
}

// Unescape the given text
void XmlSaxParser::unEscape(String& text)
{
    const char* str = text.c_str();
    if (!str)
	return;
    String buf;
    String aux = "&";
    unsigned int len = 0;
    int found = -1;
    while (str[len]) {
	if (str[len] == '&' && found < 0) {
	    found = len++;
	    continue;
	}
	if (found < 0) {
	    len++;
	    continue;
	}
	if (str[len] == '&') {
	    Debug(this,DebugNote,"Unescape. Duplicate '&' in expression [%p]",this);
	    setError(NotWellFormed);
	    return;
	}
	if (str[len] != ';')
	    len++;
	else { // We have a candidate for escaping
	    len += 1; // Append ';' character
	    String aux(str + found,len - found);
	    char re = 0;
	    if (aux.startsWith("&#")) {
		if (aux.at(2) == 'x') {
		    if (aux.length() > 4 && aux.length() <= 12) {
			int esc = aux.substr(3,aux.length() - 4).toInteger(-1,16);
			if (esc != -1) {
			    UChar uc(esc);
			    buf.append(str,found) << uc.c_str();
			    str += len;
			    len = 0;
			    found = -1;
			    continue;
			}
		    }
		} else
		    re = getDec(aux);
	    }
	    if (re == '&') {
		if (str[len] == '#') {
		    aux = String(str + len,4);
		    if (aux == "#60;") {
			re = '<';
			len += 4;
		    }
		    if (aux == "#38;") {
			re = '&';
			len += 4;
		    }
		}
	    }
	    else if (!re)
		re = replace(aux,s_escape);
	    if (re) { // We have an valid escape character
		buf << String(str,found) << re;
		str += len;
		len = 0;
		found = -1;
	    }
	    else {
		Debug(this,DebugNote,"Unescape. No replacement found for '%s' [%p]",
		    String(str + found,len - found).c_str(),this);
		setError(NotWellFormed);
		return;
	    }
	}
    }
    if (found >= 0) {
	Debug(this,DebugNote,"Unescape. Unexpected end of expression [%p]",this);
	setError(NotWellFormed);
	return;
    }
    if (len) {
	if (str != text.c_str()) {
	    buf << String(str,len);
	    text = buf;
	}
    }
    else
	text = buf;
}

// Check if a given string is a valid xml tag name
bool XmlSaxParser::validTag(const String& buf)
{
    if (!(buf && checkFirstNameCharacter(buf[0])))
	return false;
    for (unsigned int i = 1; i < buf.length(); i++)
	if (!checkNameCharacter(buf[i]))
	    return false;
    return true;
}

// XmlEscape the given text
void XmlSaxParser::escape(String& buf, const String& text)
{
    const char* str = text.c_str();
    if (!str)
	return;
    char c;
    while ((c = *str++)) {
	const char* rep = replace(c,XmlSaxParser::s_escape);
	if (!rep) {
	    buf += c;
	    continue;
	}
	buf += rep;
    }
}

// Calls gotElement(). Reset parsed if ok
bool XmlSaxParser::processElement(NamedList& list, bool empty)
{
    gotElement(list,empty);
    if (error() == XmlSaxParser::NoError) {
	resetParsed();
	return true;
    }
    return false;
}

// Calls gotText() and reset parsed on success
bool XmlSaxParser::processText(String& text)
{
    resetError();
    unEscape(text);
    if (!error())
	gotText(text);
    else
	setUnparsed(Text);
    if (!error()) {
	resetParsed();
	setUnparsed(None);
    }
    return error() == NoError;
}


/*
 * XmlDomPareser
 */
XmlDomParser::XmlDomParser(const char* name, bool fragment)
    : XmlSaxParser(name),
    m_current(0), m_data(0), m_ownData(true)
{
    if (fragment)
	m_data = new XmlFragment();
    else
	m_data = new XmlDocument();
}

XmlDomParser::XmlDomParser(XmlParent* fragment, bool takeOwnership)
    : m_current(0), m_data(0), m_ownData(takeOwnership)
{
    m_data = fragment;
}

XmlDomParser::~XmlDomParser()
{
    if (m_ownData) {
	reset();
	if (m_data)
	    delete m_data;
    }
}

// Create a new xml comment and append it in the xml three
void XmlDomParser::gotComment(const String& text)
{
    XmlComment* com = new XmlComment(text);
    if (m_current)
	setError(m_current->addChild(com),com);
    else
	setError(m_data->addChild(com),com);

}

// Append a new xml doctype to main xml parent
void XmlDomParser::gotDoctype(const String& doc)
{
    m_data->addChild(new XmlDoctype(doc));
}

// TODO implement it see what to do
void XmlDomParser::gotProcessing(const NamedString& instr)
{
    DDebug(this,DebugStub,"gotProcessing(%s=%s) not implemented [%p]",
	instr.name().c_str(),instr.safe(),this);
}

// Create a new xml declaration, verifies the version and encoding
// and append it in the main xml parent
void XmlDomParser::gotDeclaration(const NamedList& decl)
{
    if (m_current) {
	setError(DeclarationParse);
	Debug(this,DebugNote,"Received declaration inside element bounds [%p]",this);
	return;
    }
    Error err = NoError;
    while (true) {
	String* version = decl.getParam("version");
	if (version) {
	    int ver = version->substr(0,version->find('.')).toInteger();
	    if (ver != 1) {
		err = UnsupportedVersion;
		break;
	    }
	}
	String* enc = decl.getParam("encoding");
	if (enc && !(*enc &= "utf-8")) {
	    err = UnsupportedEncoding;
	    break;
	}
	break;
    }
    if (err == NoError) {
	XmlDeclaration* dec = new XmlDeclaration(decl);
	setError(m_data->addChild(dec),dec);
    }
    else {
	setError(err);
	Debug(this,DebugNote,
	    "Received unacceptable declaration version='%s' encoding='%s' error '%s' [%p]",
	    decl.getValue("version"),decl.getValue("encoding"),getError(),this);
    }
}

// Create a new xml text and append it in the xml tree
void XmlDomParser::gotText(const String& text)
{
    XmlText* tet = new XmlText(text);
    if (m_current)
	m_current->addChild(tet);
    else
	setError(m_data->addChild(tet),tet);
}

// Create a new xml Cdata and append it in the xml tree
void XmlDomParser::gotCdata(const String& data)
{
    XmlCData* cdata = new XmlCData(data);
    if (!m_current) {
	if (m_data->document()) {
	    Debug(this,DebugNote,"Document got CDATA outside element [%p]",this);
	    setError(NotWellFormed);
	    TelEngine::destruct(cdata);
	    return;
	}
	setError(m_data->addChild(cdata),cdata);
	return;
    }
    setError(m_current->addChild(cdata),cdata);
}

// Create a new xml element and append it in the xml tree
void XmlDomParser::gotElement(const NamedList& elem, bool empty)
{
    XmlElement* element = 0;
    if (!m_current) {
	// If we don't have curent element menns that the main fragment
	// should hold it
	element = new XmlElement(elem,empty);
	setError(m_data->addChild(element),element);
	if (!empty && error() == XmlSaxParser::NoError)
	    m_current = element;
    }
    else {
	if (empty) {
	    element = new XmlElement(elem,empty);
	    setError(m_current->addChild(element),element);
	}
	else {
	    element = new XmlElement(elem,empty,m_current);
	    setError(m_current->addChild(element),element);
	    if (error() == XmlSaxParser::NoError)
		m_current = element;
	}
    }
}

// Verify if is the closeing tag for the current element
// Complete th current element and make current the current parent
void XmlDomParser::endElement(const String& name)
{
    if (!m_current) {
	setError(ReadingEndTag);
	Debug(this,DebugNote,"Unexpected element end tag %s [%p]",name.c_str(),this);
	return;
    }
    if (m_current->getName() != name) {
	setError(ReadingEndTag);
	Debug(this,DebugNote,
	    "Received end element for %s, but the expected one is for %s [%p]",
	    name.c_str(),m_current->getName().c_str(),this);
	return;
    }
    m_current->setCompleted();
    XDebug(this,DebugInfo,"End element for %s [%p]",m_current->getName().c_str(),this);
    m_current = static_cast<XmlElement*>(m_current->getParent());
}

// Reset this parser
void XmlDomParser::reset()
{
    m_data->reset();
    m_current = 0;
    XmlSaxParser::reset();
}


/*
 * XmlDeclaration
 */
// Create a new XmlDeclaration from version and encoding
XmlDeclaration::XmlDeclaration(const char* version, const char* enc)
    : m_declaration("")
{
    XDebug(DebugAll,"XmlDeclaration::XmlDeclaration(%s,%s) [%p]",version,enc,this);
    if (!TelEngine::null(version))
	m_declaration.addParam("version",version);
    if (!TelEngine::null(enc))
	m_declaration.addParam("encoding",enc);
}

// Constructor
XmlDeclaration::XmlDeclaration(const NamedList& decl)
    : m_declaration(decl)
{
    XDebug(DebugAll,"XmlDeclaration::XmlDeclaration(%s) [%p]",m_declaration.c_str(),this);
}

// Copy Constructor
XmlDeclaration::XmlDeclaration(const XmlDeclaration& decl)
    : m_declaration(decl.getDec())
{
}

// Destructor
XmlDeclaration::~XmlDeclaration()
{
    XDebug(DebugAll,"XmlDeclaration::~XmlDeclaration() ( %s| %p )",
	m_declaration.c_str(),this);
}

// Create a String from this Xml Declaration
void XmlDeclaration::toString(String& dump, bool esc) const
{
    dump << "<?" << "xml";
    int n = m_declaration.count();
    for (int i = 0;i < n;i ++) {
	NamedString* ns = m_declaration.getParam(i);
	if (!ns)
	    continue;
	dump += " ";
	dump += ns->name();
	dump << "=\"";
	if (esc)
	    XmlSaxParser::escape(dump,*ns);
	else
	    dump += *ns;
	dump << "\"";
    }
    dump << "?>";
}


/*
 * XmlFragment
 */
// Constructor
XmlFragment::XmlFragment()
    : m_list()
{
    XDebug(DebugAll,"XmlFragment::XmlFragment() ( %p )",this);
}

// Copy Constructor
XmlFragment::XmlFragment(const XmlFragment& orig)
{
    ObjList* ob = orig.getChildren().skipNull();
    for (;ob;ob = ob->skipNext()) {
	XmlChild* obj = static_cast<XmlChild*>(ob->get());
	if (obj->xmlElement()) {
	    XmlElement* el = obj->xmlElement();
	    if (el)
		addChild(new XmlElement(*el));
	    continue;
	}
	else if (obj->xmlCData()) {
	    XmlCData* cdata = obj->xmlCData();
	    if (cdata)
		addChild(new XmlCData(*cdata));
	    continue;
	}
	else if (obj->xmlText()) {
	    const XmlText* text = obj->xmlText();
	    if (text)
		addChild(new XmlText(*text));
	    continue;
	}
	else if (obj->xmlComment()) {
	    XmlComment* comm = obj->xmlComment();
	    if (comm)
		addChild(new XmlComment(*comm));
	    continue;
	}
	else if (obj->xmlDeclaration()) {
	    XmlDeclaration* decl = obj->xmlDeclaration();
	    if (decl)
		addChild(new XmlDeclaration(*decl));
	    continue;
	}
	else if (obj->xmlDoctype()) {
	    XmlDoctype* doctype = obj->xmlDoctype();
	    if (doctype)
		addChild(new XmlDoctype(*doctype));
	    continue;
	}
    }
}

// Destructor
XmlFragment::~XmlFragment()
{
    m_list.clear();
    XDebug(DebugAll,"XmlFragment::~XmlFragment() ( %p )",this);
}

// Reset. Clear children list
void XmlFragment::reset()
{
    m_list.clear();
}

// Append a new child
XmlSaxParser::Error XmlFragment::addChild(XmlChild* child)
{
    if (child)
	m_list.append(child);
    return XmlSaxParser::NoError;
}

// Remove the first XmlElement from list and returns it if completed
XmlElement* XmlFragment::popElement()
{
    for (ObjList* o = m_list.skipNull(); o; o = o->skipNext()) {
	XmlChild* c = static_cast<XmlChild*>(o->get());
	XmlElement* x = c->xmlElement();
	if (x) {
	     if (x->completed()) {
		o->remove(false);
		return x;
	     }
	     return 0;
	}
    }
    return 0;
}

// Remove a child
XmlChild* XmlFragment::removeChild(XmlChild* child, bool delObj)
{
    XmlChild* ch = static_cast<XmlChild*>(m_list.remove(child,delObj));
    if (ch && ch->xmlElement())
	ch->xmlElement()->setParent(0);
    return ch;
}

// Create a String from this XmlFragment
void XmlFragment::toString(String& dump, bool escape, const String& indent,
    const String& origIndent, bool completeOnly, const String* auth,
    const XmlElement* parent) const
{
    ObjList* ob = m_list.skipNull();
    if (!ob)
	return;
    ObjList buffers;
    for (;ob;ob = ob->skipNext()) {
	String* s = new String;
	XmlChild* obj = static_cast<XmlChild*>(ob->get());
	if (obj->xmlElement())
	    obj->xmlElement()->toString(*s,escape,indent,origIndent,completeOnly,auth);
	else if (obj->xmlText())
	    obj->xmlText()->toString(*s,escape,indent,auth,parent);
	else if (obj->xmlCData())
	    obj->xmlCData()->toString(*s,indent);
	else if (obj->xmlComment())
	    obj->xmlComment()->toString(*s,indent);
	else if (obj->xmlDeclaration())
	    obj->xmlDeclaration()->toString(*s,escape);
	else if (obj->xmlDoctype())
	    obj->xmlDoctype()->toString(*s,origIndent);
	else
	    Debug(DebugStub,"XmlFragment::toString() unhandled element type!");
	if (!TelEngine::null(s))
	    buffers.append(s);
	else
	    TelEngine::destruct(s);
    }
    dump.append(buffers);
}

// Find a completed xml element in a list
XmlElement* XmlFragment::findElement(ObjList* list, const String* name, const String* ns,
    bool noPrefix)
{
    XmlElement* e = 0;
    for (; list; list = list->skipNext()) {
	e = (static_cast<XmlChild*>(list->get()))->xmlElement();
	if (!(e && e->completed()))
	    continue;
	if (name || ns) {
	    if (!ns) {
		if (noPrefix) {
		    if (*name == e->unprefixedTag())
			break;
		}
		else if (*name == e->toString())
		    break;
	    }
	    else if (name) {
		const String* t = 0;
		const String* n = 0;
		if (e->getTag(t,n) && *t == *name && n && *n == *ns)
		    break;
	    }
	    else {
		const String* n = e->xmlns();
		if (n && *n == *ns)
		    break;
	    }
	}
	else
	    break;
	e = 0;
    }
    return e;
}


/*
 * XmlDocument
 */
// Constructor
XmlDocument::XmlDocument()
    : m_root(0)
{

}

// Destructor
XmlDocument::~XmlDocument()
{
    reset();
}

// Append a new child to this document
// Set the root to an XML element if not already set. If we already have a completed root
//  the element will be added to the root, otherwise an error will be returned.
// If we don't have a root non xml elements (other then text) will be added the list
//  of elements before root
XmlSaxParser::Error XmlDocument::addChild(XmlChild* child)
{
    if (!child)
	return XmlSaxParser::NoError;

    XmlElement* element = child->xmlElement();
    if (!m_root) {
	if (element) {
	    m_root = element;
	    return XmlSaxParser::NoError;
	}
	XmlDeclaration* decl = child->xmlDeclaration();
	if (decl && declaration()) {
	    DDebug(DebugNote,"XmlDocument. Request to add duplicate declaration [%p]",this);
	    return XmlSaxParser::NotWellFormed;
	}
	// Text outside root: ignore empty, raise error otherwise
	XmlText* text = child->xmlText();
	if (text) {
	    if (text->onlySpaces()) {
		m_beforeRoot.addChild(text);
		return XmlSaxParser::NoError;
	    }
	    Debug(DebugNote,"XmlDocument. Got text outside element [%p]",this);
	    return XmlSaxParser::NotWellFormed;
	}
	return m_beforeRoot.addChild(child);
    }
    // We have a root
    if (element) {
	if (m_root->completed())
	    return m_root->addChild(child);
	DDebug(DebugStub,"XmlDocument. Request to add xml element child to incomplete root [%p]",this);
	return XmlSaxParser::NotWellFormed;
    }
    if ((child->xmlText() && child->xmlText()->onlySpaces()) || child->xmlComment())
	return m_afterRoot.addChild(child);
    // TODO: check what xml we can add after the root or if we can add
    //  anything after an incomplete root
    Debug(DebugStub,"XmlDocument. Request to add non element while having a root [%p]",this);
    return XmlSaxParser::NotWellFormed;
}

// Retrieve the document declaration
XmlDeclaration* XmlDocument::declaration() const
{
    for (ObjList* o = m_beforeRoot.getChildren().skipNull(); o; o = o->skipNext()) {
	XmlDeclaration* d = (static_cast<XmlChild*>(o->get()))->xmlDeclaration();
	if (d)
	    return d;
    }
    return 0;
}

// Obtain root element completed ot not
XmlElement* XmlDocument::root(bool completed) const
{
    return (m_root && (m_root->completed() || !completed)) ? m_root : 0;
}

void XmlDocument::toString(String& dump, bool escape, const String& indent, const String& origIndent) const
{
    m_beforeRoot.toString(dump,escape,indent,origIndent);
    if (m_root) {
	dump << origIndent;
	m_root->toString(dump,escape,indent,origIndent);
    }
    m_afterRoot.toString(dump,escape,indent,origIndent);
}

// Reset this XmlDocument. Destroys root and clear the others xml objects
void XmlDocument::reset()
{
    TelEngine::destruct(m_root);
    m_beforeRoot.clearChildren();
    m_afterRoot.clearChildren();
    m_file.clear();
}

// Load this document from data stream and parse it
XmlSaxParser::Error XmlDocument::read(Stream& in, int* error)
{
    XmlDomParser parser(static_cast<XmlParent*>(this),false);
    char buf[8096];
    bool start = true;
    while (true) {
	int rd = in.readData(buf,sizeof(buf) - 1);
	if (rd > 0) {
	    buf[rd] = 0;
	    const char* text = buf;
	    if (start) {
		String::stripBOM(text);
		start = false;
	    }
	    if (parser.parse(text) || parser.error() == XmlSaxParser::Incomplete)
		continue;
	    break;
	}
	break;
    }
    parser.completeText();
    if (parser.error() != XmlSaxParser::NoError) {
	DDebug(DebugNote,"XmlDocument error loading stream. Parser error %d '%s' [%p]",
	    parser.error(),parser.getError(),this);
	return parser.error();
    }
    if (in.error()) {
	if (error)
	    *error = in.error();
#ifdef DEBUG
	String tmp;
	Thread::errorString(tmp,in.error());
	Debug(DebugNote,"XmlDocument error loading stream. I/O error %d '%s' [%p]",
	    in.error(),tmp.c_str(),this);
#endif
	return XmlSaxParser::IOError;
    }
    return XmlSaxParser::NoError;
}

// Write this document to a data stream
int XmlDocument::write(Stream& out, bool escape, const String& indent,
    const String& origIndent, bool completeOnly) const
{
    String dump;
    m_beforeRoot.toString(dump,escape,indent,origIndent);
    if (m_root)
	m_root->toString(dump,escape,indent,origIndent,completeOnly);
    m_afterRoot.toString(dump,escape,indent,origIndent);
    return out.writeData(dump);
}

// Load a file and parse it
XmlSaxParser::Error XmlDocument::loadFile(const char* file, int* error)
{
    reset();
    if (TelEngine::null(file))
	return XmlSaxParser::NoError;
    m_file = file;
    File f;
    if (f.openPath(file))
	return read(f,error);
    if (error)
	*error = f.error();
#ifdef DEBUG
    String tmp;
    Thread::errorString(tmp,f.error());
    Debug(DebugNote,"XmlDocument error opening file '%s': %d '%s' [%p]",
	file,f.error(),tmp.c_str(),this);
#endif
    return XmlSaxParser::IOError;
}

// Save this xml document in a file
int XmlDocument::saveFile(const char* file, bool esc, const String& indent,
    bool completeOnly) const
{
    if (!file)
	file = m_file;
    if (!file)
	return 0;
    File f;
    int err = 0;
    if (f.openPath(file,true,false,true,false)) {
	String eol("\r\n");
	write(f,esc,eol,indent,completeOnly);
	err = f.error();
	// Add an empty line
	if (err >= 0)
	    f.writeData((void*)eol.c_str(),eol.length());
    }
    else
	err = f.error();
    if (!err) {
	XDebug(DebugAll,"XmlDocument saved file '%s' [%p]",file,this);
	return 0;
    }
#ifdef DEBUG
    String error;
    Thread::errorString(error,err);
    Debug(DebugNote,"Error saving XmlDocument to file '%s'. %d '%s' [%p]",
	file,err,error.c_str(),this);
#endif
    return f.error();
}


/*
 * XmlChild
 */
XmlChild::XmlChild()
{
}


/*
 * XmlElement
 */
XmlElement::XmlElement(const NamedList& element, bool empty, XmlParent* parent)
    : m_element(element), m_prefixed(0),
    m_parent(0), m_inheritedNs(0),
    m_empty(empty), m_complete(empty)
{
    XDebug(DebugAll,"XmlElement::XmlElement(%s,%u,%p) [%p]",
	element.c_str(),empty,parent,this);
    setPrefixed();
    setParent(parent);
}

// Copy constructor
XmlElement::XmlElement(const XmlElement& el)
    : m_children(el.m_children),
    m_element(el.getElement()), m_prefixed(0),
    m_parent(0), m_inheritedNs(0),
    m_empty(el.empty()), m_complete(el.completed())
{
    setPrefixed();
    setInheritedNs(&el,true);
}

// Create an empty xml element
XmlElement::XmlElement(const char* name, bool complete)
    : m_element(name), m_prefixed(0),
    m_parent(0), m_inheritedNs(0),
    m_empty(true), m_complete(complete)
{
    setPrefixed();
    XDebug(DebugAll,"XmlElement::XmlElement(%s) [%p]",
	m_element.c_str(),this);
}

// Create a new element with a text child
XmlElement::XmlElement(const char* name, const char* value, bool complete)
    : m_element(name), m_prefixed(0),
    m_parent(0), m_inheritedNs(0),
    m_empty(true), m_complete(complete)
{
    setPrefixed();
    addText(value);
    XDebug(DebugAll,"XmlElement::XmlElement(%s) [%p]",
	m_element.c_str(),this);
}

// Destructor
XmlElement::~XmlElement()
{
    setInheritedNs();
    TelEngine::destruct(m_prefixed);
    XDebug(DebugAll,"XmlElement::~XmlElement() ( %s| %p )",
	m_element.c_str(),this);
}

// Set element's unprefixed tag, don't change namespace prefix
void XmlElement::setUnprefixedTag(const String& s)
{
    if (!s || s == unprefixedTag())
	return;
    if (TelEngine::null(m_prefixed))
	m_element.assign(s);
    else
	m_element.assign(*m_prefixed + ":" + s);
    setPrefixed();
}

// Set inherited namespaces from a given element. Reset them anyway
void XmlElement::setInheritedNs(const XmlElement* xml, bool inherit)
{
    XDebug(DebugAll,"XmlElement(%s) setInheritedNs(%p,%s) [%p]",
	tag(),xml,String::boolText(inherit),this);
    TelEngine::destruct(m_inheritedNs);
    if (!xml)
	return;
    addInheritedNs(xml->attributes());
    if (!inherit)
	return;
    XmlElement* p = xml->parent();
    bool xmlAdd = (p == 0);
    while (p) {
	addInheritedNs(p->attributes());
	const NamedList* i = p->inheritedNs();
	p = p->parent();
	if (!p && i)
	    addInheritedNs(*i);
    }
    if (xmlAdd && xml->inheritedNs())
	addInheritedNs(*xml->inheritedNs());
}

// Add inherited namespaces from a list
void XmlElement::addInheritedNs(const NamedList& list)
{
    XDebug(DebugAll,"XmlElement(%s) addInheritedNs(%s) [%p]",tag(),list.c_str(),this);
    unsigned int n = list.count();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = list.getParam(i);
	if (!(ns && isXmlns(ns->name())))
	    continue;
	// Avoid adding already overridden namespaces
	if (m_element.getParam(ns->name()))
	    continue;
	if (m_inheritedNs && m_inheritedNs->getParam(ns->name()))
	    continue;
	// TODO: Check if attribute names are unique after adding the namespace
	//       See http://www.w3.org/TR/xml-names/ Section 6.3
	if (!m_inheritedNs)
	    m_inheritedNs = new NamedList("");
	XDebug(DebugAll,"XmlElement(%s) adding inherited %s=%s [%p]",
	    tag(),ns->name().c_str(),ns->c_str(),this);
	m_inheritedNs->addParam(ns->name(),*ns);
    }
}

// Obtain the first text of this xml element
const String& XmlElement::getText() const
{
    const XmlText* txt = 0;
    for (ObjList* ob = getChildren().skipNull(); ob && !txt; ob = ob->skipNext())
	txt = (static_cast<XmlChild*>(ob->get()))->xmlText();
    return txt ? txt->getText() : String::empty();
}

XmlChild* XmlElement::getFirstChild()
{
    if (!m_children.getChildren().skipNull())
	return 0;
    return static_cast<XmlChild*>(m_children.getChildren().skipNull()->get());
}

XmlText* XmlElement::setText(const char* text)
{
    XmlText* txt = 0;
    for (ObjList* o = getChildren().skipNull(); o; o = o->skipNext()) {
	txt = (static_cast<XmlChild*>(o->get()))->xmlText();
	if (txt)
	    break;
    }
    if (txt) {
	if (!text)
	    return static_cast<XmlText*>(removeChild(txt));
	txt->setText(text);
    }
    else if (text) {
	txt = new XmlText(text);
	addChild(txt);
    }
    return txt;
}

// Add a text child
void XmlElement::addText(const char* text)
{
    if (!TelEngine::null(text))
	addChild(new XmlText(text));
}

// Retrieve the element's tag (without prefix) and namespace
bool XmlElement::getTag(const String*& tag, const String*& ns) const
{
    if (!m_prefixed) {
	tag = &m_element;
	ns = xmlns();
	return true;
    }
    // Prefixed element
    tag = &m_prefixed->name();
    ns = xmlns();
    return ns != 0;
}

// Append a new child
XmlSaxParser::Error XmlElement::addChild(XmlChild* child)
{
    if (!child)
	return XmlSaxParser::NoError;
    // TODO: Check if a child element's attribute names are unique in the new context
    //       See http://www.w3.org/TR/xml-names/ Section 6.3
    XmlSaxParser::Error err = m_children.addChild(child);
    if (err == XmlSaxParser::NoError)
	child->setParent(this);
    return err;
}

// Remove a child
XmlChild* XmlElement::removeChild(XmlChild* child, bool delObj)
{
    return m_children.removeChild(child,delObj);
}

// Set this element's parent. Update inherited namespaces
void XmlElement::setParent(XmlParent* parent)
{
    XDebug(DebugAll,"XmlElement(%s) setParent(%p) element=%s [%p]",
	tag(),parent,String::boolText(parent != 0),this);
    if (m_parent && m_parent->element()) {
	// Reset inherited namespaces if the new parent is an element
	// Otherwise set them from the old parent
	if (parent && parent->element())
	    setInheritedNs(0);
	else
	    setInheritedNs(m_parent->element());
    }
    m_parent = parent;
}

// Obtain a string from this xml element
void XmlElement::toString(String& dump, bool esc, const String& indent,
    const String& origIndent, bool completeOnly, const String* auth) const
{
    XDebug(DebugAll,"XmlElement(%s) toString(%u,%s,%s,%u,%p) complete=%u [%p]",
	tag(),esc,indent.c_str(),origIndent.c_str(),completeOnly,auth,m_complete,this);
    if (!m_complete && completeOnly)
	return;
    String auxDump;
    auxDump << indent << "<" << m_element;
    int n = m_element.count();
    for (int i = 0; i < n; i++) {
	NamedString* ns = m_element.getParam(i);
	if (!ns)
	    continue;
	auxDump << " " << ns->name() << "=\"";
	addAuth(auxDump,ns->name(),*ns,esc,auth);
	auxDump << "\"";
    }
    int m = getChildren().count();
    if (m_complete && !m)
	auxDump << "/";
    auxDump << ">";
    if (m) {
	// Avoid adding text on new line when text is the only child
	XmlText* text = 0;
	if (m == 1)
	    text = static_cast<XmlChild*>(getChildren().skipNull()->get())->xmlText();
	if (!text)
	    m_children.toString(auxDump,esc,indent + origIndent,origIndent,completeOnly,auth,this);
	else
	    text->toString(auxDump,esc,String::empty(),auth,this);
	if (m_complete)
	    auxDump << (!text ? indent : String::empty()) << "</" << getName() << ">";
    }
    dump << auxDump;
}

// Copy element attributes to a list of parameters
unsigned int XmlElement::copyAttributes(NamedList& list, const String& prefix) const
{
    unsigned int copy = 0;
    unsigned int n = m_element.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = m_element.getParam(i);
	if (!(ns && ns->name()))
	    continue;
	list.addParam(prefix + ns->name(),*ns);
	copy++;
    }
    return copy;
}

void XmlElement::setAttributes(NamedList& list, const String& prefix, bool skipPrefix)
{
    if (prefix)
	m_element.copySubParams(list,prefix,skipPrefix);
    else
	m_element.copyParams(list);
}

// Retrieve a namespace attribute. Search in parent or inherited for it
String* XmlElement::xmlnsAttribute(const String& name) const
{
    String* tmp = getAttribute(name);
    if (tmp)
	return tmp;
    XmlElement* p = parent();
    if (p)
	return p->xmlnsAttribute(name);
    return m_inheritedNs ? m_inheritedNs->getParam(name) : 0;
}

// Set the element's namespace
bool XmlElement::setXmlns(const String& name, bool addAttr, const String& value)
{
    const String* cmp = name ? &name : &s_ns;
    XDebug(DebugAll,"XmlElement(%s)::setXmlns(%s,%u,%s) [%p]",
	tag(),cmp->c_str(),addAttr,value.c_str(),this);
    if (*cmp == s_ns) {
	if (m_prefixed) {
	    m_element.assign(m_prefixed->name());
	    setPrefixed();
	    // TODO: remove children and attributes prefixes
	}
    }
    else if (!m_prefixed || *m_prefixed != cmp) {
	if (!m_prefixed)
	    m_element.assign(*cmp + ":" + tag());
	else
	    m_element.assign(*cmp + ":" + m_prefixed->name());
	setPrefixed();
	// TODO: change children and attributes prefixes
    }
    if (!(addAttr && value))
	return true;
    String attr;
    if (*cmp == s_ns)
	attr = s_ns;
    else
	attr << s_nsPrefix << *cmp;
    NamedString* ns = m_element.getParam(attr);
    if (!ns && m_inheritedNs && m_inheritedNs->getParam(attr))
	m_inheritedNs->clearParam(attr);
    // TODO: Check if attribute names are unique after adding the namespace
    //       See http://www.w3.org/TR/xml-names/ Section 6.3
    if (!ns)
	m_element.addParam(attr,value);
    else
	*ns = value;
    return true;
}

// Build an XML element from a list parameter
XmlElement* XmlElement::param2xml(NamedString* param, const String& tag, bool copyXml)
{
    if (!(param && param->name() && tag))
	return 0;
    XmlElement* xml = new XmlElement(tag);
    xml->setAttribute(s_name,param->name());
    xml->setAttributeValid(YSTRING("value"),*param);
    NamedPointer* np = YOBJECT(NamedPointer,param);
    if (!(np && np->userData()))
	return xml;
    DataBlock* db = YOBJECT(DataBlock,np->userData());
    if (db) {
	xml->setAttribute(s_type,"DataBlock");
	Base64 b(db->data(),db->length(),false);
	String tmp;
	b.encode(tmp);
	b.clear(false);
	xml->addText(tmp);
	return xml;
    }
    XmlElement* element = YOBJECT(XmlElement,np->userData());
    if (element) {
	xml->setAttribute(s_type,"XmlElement");
	if (!copyXml) {
	    np->takeData();
	    xml->addChild(element);
	}
	else
	    xml->addChild(new XmlElement(*element));
	return xml;
    }
    NamedList* list = YOBJECT(NamedList,np->userData());
    if (list) {
	xml->setAttribute(s_type,"NamedList");
	xml->addText(list->c_str());
	unsigned int n = list->length();
	for (unsigned int i = 0; i < n; i++)
	    xml->addChild(param2xml(list->getParam(i),tag,copyXml));
	return xml;
    }
    return xml;
}

// Build a list parameter from xml element
NamedString* XmlElement::xml2param(XmlElement* xml, const String* tag, bool copyXml)
{
    const char* name = xml ? xml->attribute(s_name) : 0;
    if (TelEngine::null(name))
	return 0;
    GenObject* gen = 0;
    String* type = xml->getAttribute(s_type);
    if (type) {
	if (*type == YSTRING("DataBlock")) {
	    gen = new DataBlock;
	    const String& text = xml->getText();
	    Base64 b((void*)text.c_str(),text.length(),false);
	    b.decode(*(static_cast<DataBlock*>(gen)));
	    b.clear(false);
	}
	else if (*type == YSTRING("XmlElement")) {
	    if (!copyXml)
		gen = xml->pop();
	    else {
		XmlElement* tmp = xml->findFirstChild();
		if (tmp)
		    gen = new XmlElement(*tmp);
	    }
	}
	else if (*type == YSTRING("NamedList")) {
	    gen = new NamedList(xml->getText());
	    xml2param(*(static_cast<NamedList*>(gen)),xml,tag,copyXml);
	}
	else
	    Debug(DebugStub,"XmlElement::xml2param: unhandled type=%s",type->c_str());
    }
    if (!gen)
	return new NamedString(name,xml->attribute(YSTRING("value")));
    return new NamedPointer(name,gen,xml->attribute(YSTRING("value")));
}

// Build and add list parameters from XML element children
void XmlElement::xml2param(NamedList& list, XmlElement* parent, const String* tag,
    bool copyXml)
{
    if (!parent)
	return;
    XmlElement* ch = 0;
    while (0 != (ch = parent->findNextChild(ch,tag))) {
	NamedString* ns = xml2param(ch,tag,copyXml);
	if (ns)
	    list.addParam(ns);
    }
}


/*
 * XmlComment
 */
// Constructor
XmlComment::XmlComment(const String& comm)
    : m_comment(comm)
{
    XDebug(DebugAll,"XmlComment::XmlComment(const String& comm) ( %s| %p )",
	m_comment.c_str(),this);
}

// Copy Constructor
XmlComment::XmlComment(const XmlComment& comm)
    : m_comment(comm.getComment())
{
}

// Destructor
XmlComment::~XmlComment()
{
    XDebug(DebugAll,"XmlComment::~XmlComment() ( %s| %p )",
	m_comment.c_str(),this);
}

// Obtain string representation of this xml comment
void XmlComment::toString(String& dump, const String& indent) const
{
    dump << indent << "<!--" << getComment() << "-->";
}


/*
 * XmlCData
 */
// Constructor
XmlCData::XmlCData(const String& data)
    : m_data(data)
{
    XDebug(DebugAll,"XmlCData::XmlCData(const String& data) ( %s| %p )",
	m_data.c_str(),this);
}

// Copy Constructor
XmlCData::XmlCData(const XmlCData& data)
    : m_data(data.getCData())
{
}

// Destructor
XmlCData::~XmlCData()
{
    XDebug(DebugAll,"XmlCData::~XmlCData() ( %s| %p )",
	m_data.c_str(),this);
}

// Obtain string representation of this xml Cdata
void XmlCData::toString(String& dump, const String& indent) const
{
    dump << indent << "<![CDATA[" << getCData() << "]]>";
}


/*
 * XmlText
 */
// Constructor
XmlText::XmlText(const String& text)
    : m_text(text)
{
    XDebug(DebugAll,"XmlText::XmlText(%s) [%p]",m_text.c_str(),this);
}

// Copy Constructor
XmlText::XmlText(const XmlText& text)
    : m_text(text.getText())
{
    XDebug(DebugAll,"XmlText::XmlText(%p,%s) [%p]",
	&text,TelEngine::c_safe(text.getText()),this);
}

// Destructor
XmlText::~XmlText()
{
    XDebug(DebugAll,"XmlText::~XmlText [%p]",this);
}

// Obtain string representation of this xml text
void XmlText::toString(String& dump, bool esc, const String& indent,
    const String* auth, const XmlElement* parent) const
{
    dump << indent;
    if (auth)
	addAuth(dump,parent ? parent->toString() : String::empty(),m_text,esc,auth);
    else if (esc)
        XmlSaxParser::escape(dump,m_text);
    else
	dump << m_text;
}

bool XmlText::onlySpaces()
{
    if (!m_text)
	return true;
    const char *s = m_text;
    unsigned int i = 0;
    for (;i < m_text.length();i++) {
	if (s[i] == ' ' || s[i] == '\t' || s[i] == '\v' || s[i] == '\f' || s[i] == '\r' || s[i] == '\n')
	    continue;
	return false;
    }
    return true;
}

/*
 * XmlDoctype
 */
// Constructor
XmlDoctype::XmlDoctype(const String& doctype)
    : m_doctype(doctype)
{
    XDebug(DebugAll,"XmlDoctype::XmlDoctype(const String& doctype) ( %s| %p )",
	m_doctype.c_str(),this);
}

// Copy Constructor
XmlDoctype::XmlDoctype(const XmlDoctype& doctype)
    : m_doctype(doctype.getDoctype())
{
}

// Destructor
XmlDoctype::~XmlDoctype()
{
    XDebug(DebugAll,"XmlDoctype::~XmlDoctype() ( %s| %p )",
	m_doctype.c_str(),this);
}

// Obtain string representation of this xml doctype
void XmlDoctype::toString(String& dump, const String& indent) const
{
    dump << indent << "<!DOCTYPE " << m_doctype << ">";
}

/* vi: set ts=8 sw=4 sts=4 noet: */
