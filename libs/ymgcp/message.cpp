/**
 * message.cpp
 * Yet Another MGCP Stack
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

#include <yatemgcp.h>
#include <stdio.h>

using namespace TelEngine;

#ifdef XDEBUG
#define PARSER_DEBUG
#endif

// Ensure response code string representation is 3 digit long
inline void setCode(String& dest, unsigned int code)
{
    char c[4];
    sprintf(c,"%03u",code);
    dest = c;
}


/**
 * MGCPMessage
 */
// Construct an outgoing command message
MGCPMessage::MGCPMessage(MGCPEngine* engine, const char* name, const char* ep, const char* ver)
    : params(""),
    m_name(name),
    m_valid(false),
    m_code(-1),
    m_transaction(0),
    m_endpoint(ep),
    m_version(ver)
{
    if (!(engine && (engine->allowUnkCmd() || engine->knownCommand(m_name)))) {
	Debug(engine,DebugNote,"MGCPMessage. Unknown cmd=%s [%p]",name,this);
	return;
    }
    // Command names MUST be 4 character long
    if (m_name.length() != 4) {
	Debug(engine,DebugNote,
	    "MGCPMessage. Invalid command length cmd=%s len=%u [%p]",
	    m_name.c_str(),m_name.length(),this);
	return;
    }

    m_transaction = engine->getNextId();
    m_valid = true;
    DDebug(engine,DebugAll,"MGCPMessage. cmd=%s trans=%u ep=%s [%p]",
	name,m_transaction,ep,this);
}

// Construct a response message
MGCPMessage::MGCPMessage(MGCPTransaction* trans, unsigned int code, const char* comment)
    : params(""),
    m_valid(false),
    m_code(code),
    m_transaction(0),
    m_comment(comment)
{
    if (!trans) {
	Debug(DebugNote,
	    "MGCPMessage. Can't create response without transaction [%p]",this);
	return;
    }
    if (code > 999) {
	Debug(trans->engine(),DebugNote,
	    "MGCPMessage. Invalid response code=%u [%p]",code,this);
	return;
    }
    setCode(m_name,code);
    m_transaction = trans->id();
    if (!m_comment.length())
	m_comment = lookup(code,MGCPEngine::mgcp_responses);
    m_valid = true;
    DDebug(trans->engine(),DebugAll,"MGCPMessage code=%d trans=%u comment=%s [%p]",
	code,m_transaction,m_comment.c_str(),this);
}

// Constructor. Used by the parser to construct an incoming message
MGCPMessage::MGCPMessage(MGCPEngine* engine, const char* name, int code,
	unsigned int transId, const char* epId, const char* ver)
    : params(""),
    m_valid(true),
    m_code(code),
    m_transaction(transId),
    m_endpoint(epId),
    m_version(ver)
{
    if (code < 0)
	m_name = name;
    else {
	setCode(m_name,code);
	m_comment = name;
	if (!m_comment.length())
	    m_comment = lookup(code,MGCPEngine::mgcp_responses);
    }
    DDebug(engine,DebugAll,
	"Incoming MGCPMessage %s=%s trans=%u ep=%s ver=%s comment=%s [%p]",
	isCommand()?"cmd":"rsp",this->name().c_str(),
	transId,epId,ver,m_comment.safe(),this);
}

MGCPMessage::~MGCPMessage()
{
    DDebug(DebugAll,"MGCPMessage::~MGCPMessage [%p]",this);
}

// Convert this message to a string representation
void MGCPMessage::toString(String& dest) const
{
    // Construct first line
    dest << name() << " " << transactionId();
    if (isCommand())
	dest << " " << endpointId() << " " << m_version;
    else if (m_comment)
	dest << " " << m_comment;
    dest << "\r\n";

    // Append message parameters
    unsigned int n = params.count();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = params.getParam(i);
	if (!ns)
	    continue;
	dest << ns->name() << ": " << *ns << "\r\n";
    }

    // Append SDP(s)
    for (ObjList* obj = sdp.skipNull(); obj; obj = obj->skipNext()) {
	String s;
	MimeSdpBody* tmp = static_cast<MimeSdpBody*>(obj->get());
	for (ObjList* o = tmp->lines().skipNull(); o; o = o->skipNext()) {
	    NamedString* ns = static_cast<NamedString*>(o->get());
	    if (*ns)
		s << ns->name() << "=" << *ns << "\r\n";
	}
	if (s)
	    dest << "\r\n" << s;
    }
}

// Check if a character is an end-of-line one
static inline bool isEoln(char c)
{
    return c == '\r' || c == '\n';
}

// Check if a character is blank: space or tab
static inline bool isBlank(char c)
{
    return c == ' ' || c == '\t';
}

// Skip blank characters. The buffer is assumed to be valid
// Return false if the end of line was reached
static inline bool skipBlanks(const char*& buffer, unsigned int& len)
{
    for (; len && isBlank(*buffer); buffer++, len--)
	;
    return (len != 0);
}

// Get a line from a buffer until the first valid end-of-line or end of buffer was reached,
//  starting with current index in buffer
// Set the current index to the first character after the end-of-line or at the end of the buffer
// The buffer is assumed to be valid
// Return the line and length or 0 if an invalid end-of-line was found
// RFC 3435 3.1: end-of-line may be CR/LF or LF
static inline const char* getLine(const unsigned char* buffer, unsigned int len,
	unsigned int& crt, unsigned int& count, bool skip = true)
{
    count = 0;
    const char* line = (const char*)buffer + crt;
    while (crt < len && !isEoln(buffer[crt])) {
	crt++;
	count++;
    }
    if (skip)
	skipBlanks(line,count);

    // Check end of buffer or end-of-line
    if (crt == len)
	return line;
    if (buffer[crt] == '\r' && (++crt) == len)
	return 0;
    return buffer[crt++] == '\n' ? line : 0;
}

// Parse a received buffer according to RFC 3435
// See Appendix A for the grammar
bool MGCPMessage::parse(MGCPEngine* engine, ObjList& dest,
	const unsigned char* buffer, unsigned int len, const char* sdpType)
{
    if (!buffer)
	return false;

#ifdef PARSER_DEBUG
    String t((const char*)buffer,len);
    Debug(engine,DebugAll,"Parse received buffer\r\n%s",t.c_str());
#endif

    int errorCode = 510;              // Protocol error
    unsigned int trans = 0;
    String error;
    unsigned int crt = 0;

    while (crt < len && !error) {
	unsigned int count = 0;
	const char* line = 0;

	// Skip empty lines before a message line and skip trailing blanks on the message line
	while (crt < len) {
	    line = getLine(buffer,len,crt,count);
	    if (!line) {
		error = "Invalid end-of-line";
		break;
	    }
	    // Exit loop if the line is not empty
	    if (count)
		break;
	}
	if (!count || error)
	    break;

	// *** Decode the message line
	MGCPMessage* msg = decodeMessage(line,count,trans,error,engine);
	if (!msg)
	    break;
	dest.append(msg);

#ifdef PARSER_DEBUG
	String m((const char*)line,count);
	Debug(engine,DebugAll,"Decoded message: %s",m.c_str());
#endif

	// *** Decode parameters
	if (decodeParams(buffer,len,crt,msg,error,engine))
	    continue;
	if (error) {
	    if (msg->isCommand())
		trans = msg->transactionId();
	    break;
	}
	if (crt >= len)
	    break;

	// *** Decode SDP
	// Decode SDPs until the end of buffer or
        //  a line containing a dot (message separator in a piggybacked block)
	// SDPs are separated by an empty line
	int empty = 0;
	while (empty < 2) {
	    // Skip until an empty line, a line containing a dot or end of buffer
	    unsigned int start = crt;
	    unsigned int sdpLen = 0;
	    while (true) {
		line = getLine(buffer,len,crt,count);
		if (!line) {
		    error = "Invalid end-of-line";
		    break;
		}
		if (!count || (count == 1 && (*line == '.' || !*line))) {
		    if (!count)
			empty++;
		    else
			empty = 3;
		    break;
		}
		empty = 0;
		sdpLen = crt - start;
	    }
	    if (error)
		break;
	    if (sdpLen)
		msg->sdp.append(new MimeSdpBody(sdpType,(const char*)buffer+start,sdpLen));
	}

	// Found 2 empty lines: skip until end of buffer or line containing '.' or non empty line
	if (empty == 2) {
	    unsigned int start = crt;
	    while (true) {
		line = getLine(buffer,len,crt,count);
		if (!line) {
		    error = "Invalid end-of-line";
		    break;
		}
		if (!count) {
		    if (crt == len)
			break;
		    continue;
		}
		// Fallback with current index if found non empty line which doesn't start with '.'
		if (*line && *line != '.')
		    crt = start;
		break;
	    }
	}
    }
    if (!error)
	return true;

    dest.clear();
    if (trans && trans <= 999999999)
	dest.append(new MGCPMessage(engine,0,errorCode,trans,0,0));
    Debug(engine,DebugNote,"Parser error: %s",error.c_str());
    return false;
}

// Decode the message line
// Command: verb transaction endpoint proto_name proto_version ...
// Response: code transaction comment ...
MGCPMessage* MGCPMessage::decodeMessage(const char* line, unsigned int len, unsigned int& trans,
	String& error, MGCPEngine* engine)
{
    String name, ver;
    int code = -1;
    unsigned int trID = 0;
    MGCPEndpointId id;

#ifdef PARSER_DEBUG
    String msgLine(line,len);
    Debug(engine,DebugAll,"Parse message line (len=%u): %s",
	msgLine.length(),msgLine.c_str());
#endif

    for (unsigned int item = 1; true; item++) {
	if (item == 6) {
#ifdef DEBUG
	    if (len) {
		String rest(line,len);
		Debug(engine,DebugAll,"Unparsed data on message line: '%s'",rest.c_str());
	    }
#endif
	    break;
	}

	// Response: the 3rd item is the comment
	bool comment = (item == 3) && (code != -1);

	// Get current item
	if (!(skipBlanks(line,len) || comment)) {
	    error = "Unexpected end of line";
	    return 0;
	}

	unsigned int itemLen = 0;
	if (comment)
	    itemLen = len;
	else
	    for (; itemLen < len && !isBlank(line[itemLen]); itemLen++)
		;
	String tmp(line,itemLen);
	len -= itemLen;
	line += itemLen;

	switch (item) {
	    // 1st item: verb (command or notification) or response code
	    // Verbs must be 4-character long. Responses must be numbers in the interval [0..999]
	    case 1:
		if (tmp.length() == 3) {
		    code = tmp.toInteger(-1,10);
		    if (code < 0 || code > 999)
			error << "Invalid response code " << tmp;
		}
		else if (tmp.length() == 4)
		    name = tmp.toUpper();
		else
		    error << "Invalid first item '" << tmp << "' length " << tmp.length();
		break;

	    // 2nd item: the transaction id
	    // Restriction: must be a number in the interval [1..999999999]
	    case 2:
		trID = tmp.toInteger(-1,10);
		if (!trID || trID > 999999999)
		    error << "Invalid transaction id '" << tmp << "'";
		// Set trans for command messages so they can be responded on error
		else if (code == -1)
		    trans = trID;
		break;

	    // 3rd item: endpoint id (code is -1) or response comment (code != -1)
	    case 3:
		if (code != -1)
		    name = tmp;
		else {
		    id.set(tmp);
		    if (!id.valid())
			error << "Invalid endpoint id '" << tmp << "'";
		}
		break;

	    // 4th item: protocol name if this is a verb (command)
	    case 4:
		ver = tmp.toUpper();
		if (ver != "MGCP")
		    error << "Invalid protocol '" << tmp << "'";
		break;

	    // 5th item: protocol version if this is a verb (command)
	    case 5:
		{
		    static const Regexp r("^[0-9]\\.[0-9]\\+$");
		    if (!r.matches(tmp))
			error << "Invalid protocol version '" << tmp << "'";
		}
		ver << " " << tmp;
		break;
	}
	if (error)
	    return 0;
	// Stop parse the rest if this is a response
	if (comment)
	    break;
    }
    // Check known commands
    if (code == -1 &&
	!(engine && (engine->allowUnkCmd() || engine->knownCommand(name)))) {
	error << "Unknown cmd '" << name << "'";
	return 0;
    }
    return new MGCPMessage(engine,name,code,trID,id.id(),ver);
}

// Decode message parameters. Return true if found a line containing a dot
// Decode parameters until the end of buffer, empty line or
//  a line containing a dot (message separator in a piggybacked block)
// Parameters names and values are separated by a ':' character
// See RFC 3435 3.1
bool MGCPMessage::decodeParams(const unsigned char* buffer, unsigned int len,
	unsigned int& crt, MGCPMessage* msg, String& error, MGCPEngine* engine)
{
    while (crt < len) {
	unsigned int count = 0;
	const char* line = getLine(buffer,len,crt,count);
	if (!line) {
	    error = "Invalid end-of-line";
	    break;
	}

	// Terminate if the line is empty or is a message separator
	if (!count)
	    break;
	if (count == 1 && (*line == '.' || !*line))
	    return true;

#ifdef PARSER_DEBUG
	String paramLine(line,count);
	Debug(engine,DebugAll,"Parse parameter line(len=%u): %s",count,paramLine.c_str());
#endif

	// Decode parameter
	int pos = -1;
	for (int i = 0; i < (int)count; i++)
	    if (line[i] == ':')
		pos = i;
	if (pos == -1) {
	    error = "Parameter separator is missing";
	    break;
	}
	String param(line,pos);
	param.trimBlanks();
	if (!param) {
	    error = "Parameter name is empty";
	    break;
	}
	String value(line+pos+1,count-pos-1);
	value.trimBlanks();
	if (engine && engine->parseParamToLower())
	    msg->params.addParam(param.toLower(),value);
	else
	    msg->params.addParam(param,value);
    }
    return false;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
