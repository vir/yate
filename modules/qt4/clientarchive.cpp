/**
 * clientarchive.cpp
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 *
 * Client archive management and UI logic
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

/**
 * Chat log file format
 *
 * Header:
 *  versionNULLaccountNULLcontactNULLcontact_nameNULL{MARKUP_CHAT|MARKUP_ROOMCHAT|MARKUP_ROOMCHATPRIVATE}NULLNULL
 * Session:
 *  MARKUP_SESSIONSTARTsession_timeMARKUP_SESSIONDESCdescNULLNULL
 * Session items:
 *   item_time{MARKUP_SENT|MARKUP_RECEIVED|MARKUP_DELAYED}sender_nameNULLchat_textNULLNULL
*/

#include "clientarchive.h"

namespace { //anonymous
using namespace TelEngine;

class CASearchThread;                    // Archive search worker thread
class CARefreshThread;                   // Archive refresh worker thread
class ChatSession;                       // A chat session entry
class ChatItem;                          // A chat session item
class ChatFile;                          // A contact's chat file
class ChatArchive;                       // Chat archive management
class CALogic;

#define READ_BUFFER 8192                 // File read buffer

// Markups used in archive files
#define MARKUP_SESSION_START '%'         // Session start
#define MARKUP_SESSION_DESC '!'          // Session description start
#define MARKUP_SENT '>'                  // Sent item
#define MARKUP_RECV '<'                  // Received item
#define MARKUP_DELAYED '|'               // Delayed item
#define MARKUP_CHAT 'c'                  // Regular chat
#define MARKUP_ROOMCHAT 'r'              // MUC room chat
#define MARKUP_ROOMCHATPRIVATE 'p'       // MUC private chat

enum CASearchRange {
    CASearchRangeInvalid = 0,
    CASearchRangeSession,
    CASearchRangeContact,
    CASearchRangeAll
};

// Archive search worker thread
class CASearchThread : public Thread
{
public:
    CASearchThread();
    ~CASearchThread();
    void startSearching(const String& text, bool next);
    virtual void run();
private:
    void resetSearch();
    void searchAll(const String& what);
    void searchCurrentContact(const String& what);
    bool searchContact(ChatFile* f, const String& what, bool changed);

    bool m_startSearch;                  // Start search flag
    bool m_searching;                    // Currently searching
    bool m_next;
    String m_what;
    CASearchRange m_range;
    String m_currentContact;
    String m_currentSession;
    bool m_currentSessionFull;
    bool m_currentContactFull;
};

// Archive refresh worker thread
class CARefreshThread : public Thread
{
public:
    CARefreshThread();
    ~CARefreshThread();
    virtual void run();
};

// A chat session entry
class ChatSession : public String
{
public:
    inline ChatSession(const String& id, const String& name, int64_t offset)
	: String(id), m_name(name), m_offset(offset), m_length(0)
	{}
    String m_name;
    String m_desc;                       // Description
    int64_t m_offset;                    // File offset
    int64_t m_length;                    // Session length (including header)
};

// A chat session entry
class ChatItem : public GenObject
{
public:
    inline ChatItem(unsigned int time, int t)
	: m_time(time), m_type(t)
	{}
    unsigned int m_time;                 // Entry time
    int m_type;                          // Type
    String m_senderName;                 // Sender name
    String m_text;                       // Content
    QString m_search;                    // QString to be used when searching
};

// A contact's chat (including the file)
class ChatFile : public Mutex, public RefObject
{
    friend class ChatArchive;
public:
    // File version. Old versions must be inserted before Current
    enum Version {
	Invalid = 0,
	Current,
    };
    // Init object
    ChatFile(const String& dir, const String& fileName);
    // Retrieve the file type
    inline char type() const
	{ return m_type; }
    // Retrieve the file account. Lock it before use
    inline const String& account() const
	{ return m_account; }
    // Retrieve the file contact. Lock it before use
    inline const String& contact() const
	{ return m_contact; }
    // Retrieve the file contact name. Lock it before use
    inline const String& contactName() const
	{ return m_contactName; }
    // Retrieve the file contact display name. Lock it before use
    inline const String& contactDisplayName() const
	{ return m_contactName ? m_contactName : m_contact; }
    // Retrieve the id of the room owning a private chat. Lock it before use
    inline const String& roomId() const
	{ return m_roomId; }
    // Retrieve the file sessions. Lock it before use
    inline const ObjList& sessions() const
	{ return m_sessions; }
    // Load the file. Created it if not found and params are given
    // This method is thread safe
    virtual bool loadFile(const NamedList* params, String* error);
    // Write chat to file
    // This method is thread safe
    virtual bool writeChat(const NamedList& params);
    // Load sessions from file
    // This method is thread safe
    virtual bool loadSessions(bool forceLoad = false, String* error = 0);
    // Load a session from file
    // This method is thread safe
    virtual bool loadSession(const String& id, ObjList& list, String* error = 0,
	QString* search = 0);
    // Retrieve the last session. Lock the object before use
    virtual ChatSession* lastSession();
    // Close current write session. Load it if sessions were loaded
    // This method is thread safe
    virtual bool closeSession();
    // Decode a ChatItem from a given buffer. Return it on success
    ChatItem* decodeChat(bool search, int64_t offset, void* buffer, unsigned int len);
    // Retrieve the id
    virtual const String& toString() const
	{ return m_fileName; }
protected:
    virtual void destroyed() {
	    closeSession();
	    RefObject::destroyed();
	}
    // Set file last error. Close it if requested. Return false
    bool setFileError(String* error, const char* oper, bool close = false,
	bool del = false);
    // Show a chat entry format error
    inline void showEntryError(int level, const char* oper, int64_t offset) {
	    Debug(ClientDriver::self(),level,
		"File '%s' chat entry (offset " FMT64 ") error: %s",
		m_full.c_str(),offset,oper);
	}
    // Set file pos
    inline bool seekFile(int64_t offset, String* error) {
	    bool ok = m_file.seek(Socket::SeekBegin,offset) >= 0;
	    if (!ok)
		setFileError(0,"seek");
	    return ok;
	}
    // Write a buffer to the file
    int writeData(const void* buf, unsigned int len, String* error);
    // Write file header. Close the file if fails
    virtual bool readFileHeader(String* error);
    // Update data. Write file header. Close the file and delete it if fails
    virtual bool writeFileHeader(const NamedList& params, String* error);

    int m_version;
    char m_type;
    String m_account;
    String m_contact;
    String m_contactName;
    String m_roomId;                     // Parent room id if this is a private room chat
    String m_fileName;
    String m_full;
    File m_file;
    unsigned int m_hdrLen;
    int64_t m_newSessionOffset;          // Recording session file offset
    DataBlock m_writeBuffer;
    bool m_sessionsLoaded;
    ObjList m_sessions;
};

// The chat archive container
class ChatArchive : public Mutex
{
public:
    ChatArchive();
    inline bool loaded() const
	{ return m_loaded; }
    // Retrieve the files list. Lock it before use
    inline const ObjList& items() const
	{ return m_items; }
    // Init data when engine starts. Return the index file
    void init();
    // Refresh the list. Re-load all archive
    void refresh();
    // Clear all
    void clear(bool memoryOnly);
    // Clear all logs belonging to a given account
    void clearAccount(const String& account, ObjList& removedItems);
    // Remove an item and it's file
    void delFile(const String& id);
    // Retrieve a chat file. Return a referenced object
    ChatFile* loadChatFile(const String& file, bool forceLoad = false);
    // Retrieve a chat file. Return a referenced object
    ChatFile* getChatFile(const String& id);
    // Retrieve a chat file. Return a refferenced object
    inline ChatFile* getChatFile(const NamedList& params) {
	    String id;
	    if (buildChatFileName(id,params))
		return getChatFile(id);
	    return 0;
	}
    // Retrieve a chat file from session id. Return a refferenced object
    inline ChatFile* getChatFileBySession(const String& id) {
	    int pos = id.find('/');
	    return (pos > 0) ? getChatFile(id.substr(0,pos)) : 0;
	}
    // Retrieve a chat file. Return a referenced object
    ChatFile* getChatFile(const NamedList& params, const NamedList* createParams);
    // Add a chat message to log
    bool logChat(NamedList& params);
    // Close a chat session. Return a referenced pointer if the item's last
    //  session was loaded into memory
    ChatFile* closeChat(const NamedList& params);
    // Build a file name from a list of parameters
    static inline void buildChatFileName(String& buf, char type, const String& account,
	const String& contact, const String& nick = String::empty());
    // Build a file name from a list of parameters
    static inline bool buildChatFileName(String& buf, const NamedList& params);
protected:
    bool m_loaded;                       // Archive loaded
    String m_dir;                        // Directory containing the archive
    Configuration m_index;               // Index file
    ObjList m_items;
};

// The logic
class CALogic : public ClientLogic
{
public:
    CALogic(int prio = 0);
    ~CALogic();
    // Load notifications
    virtual bool initializedClient();
    virtual void exitingClient();
    // Engine start notification
    void engineStart(Message& msg);
    // Actions from UI
    virtual bool action(Window* wnd, const String& name, NamedList* params = 0);
    virtual bool select(Window* wnd, const String& name, const String& item,
	const String& text = String::empty());
    virtual bool toggle(Window* wnd, const String& name, bool active);
    // Stop the search thread and wait for terminate
    void searchStop();
    // Search thread terminated
    void searchTerminated()
	{ m_searchThread = 0; }
    // Start archive refresh
    void refreshStart(const String* selected = 0);
    // Archive refresh terminated. Refresh UI
    void refreshTerminated();
    // Stop the refresh thread and wait for terminate
    void refreshStop();
    // Set control highlight
    bool setSearchHistory(const String& what, bool next);
    // Reset control highlight
    bool resetSearchHistory(bool reset = true);
    // Select and set search history. Return true on success
    bool setSearch(bool reset, const String& file, const String& session,
	const String& what, bool next);
protected:
    // Load a chat item into UI
    bool loadChat(const NamedList& params);
    // Close a chat session
    bool closeChat(const NamedList& params);
    // Update sessions related to a given item
    bool updateSessions(const String& id, Window* wnd);
    // Update session content in UI
    bool updateSession(const String& id, Window* wnd);
    // Save current session
    bool saveSession(Window* wnd, NamedList* params = 0);
    // Delete selected contact
    bool delContact(Window* wnd);
    // Clear all archive
    bool clearLog(Window* wnd);

    bool m_resetSearchOnSel;             // Reset search when session selection changes
    CASearchThread* m_searchThread;
    CARefreshThread* m_refreshThread;
    String m_selectAfterRefresh;
    String m_searchText;
};


/*
 * Module data
 */
// UI controls
static const String s_wndArch = "archive";
// Prefixes
static const String s_archPrefix = "archive:";
// Widgets
static const String s_logList = "archive_logs_list";
static const String s_sessList = "archive_session_list";
static const String s_sessHistory = "archive_session_history";
static const String s_searchShow = "archive_search_show";
static const String s_searchHide = "archive_search_hide";
static const String s_searchEdit = "archive_search_edit";
static const String s_searchStart = "archive_search_start";
static const String s_searchPrev = "archive_search_prev";
static const String s_searchNext = "archive_search_next";
static const String s_searchRange = "archive_search_range";
static const String s_searchMatchCase = "archive_search_opt_matchcase";
static const String s_searchHighlightAll = "archive_search_opt_highlightall";
// Actions
static const String& s_actionLogChat = "logchat";
static const String& s_actionSelectChat = "showchat";
static const String& s_actionCloseChat = "closechatsession";
static const String& s_actionRefresh = "archive_refresh";
static const String& s_actionClear = "clear";
static const String& s_actionClearNow = "clearnow";
static const String& s_actionClearAccNow = "clearaccountnow";
static const String& s_actionDelContact = "delcontact";
static const String& s_actionDelContactNow = "delcontactnow";
// Data
static const DataBlock s_zeroDb(0,1);
static const String s_crlf = "\r\n";
static Mutex s_mutex(true,"CALogic");
static CALogic s_logic(-50);             // The logic
static ChatArchive s_chatArchive;        // Archive holder
static CASearchRange s_range = CASearchRangeContact;
static bool s_matchCase = false;
static bool s_highlightAll = false;
// Search range values
static const TokenDict s_searchListRange[] = {
    {"Current contact", CASearchRangeContact},
    {"Current session", CASearchRangeSession},
    {"All archive",     CASearchRangeAll},
    {0,0},
};

// Check if exiting: client is exiting or thread cancel requested
static bool exiting()
{
    return Client::exiting() || Thread::check(false);
}

// Retrieve the window
static inline Window* getWindow()
{
    return Client::self() ? Client::self()->getWindow(s_wndArch) : 0;
}

// Retrieve the chat type from a list of parameters
static inline char chatType(const NamedList& params)
{
    if (!params.getBoolValue("muc"))
	return MARKUP_CHAT;
    if (params.getBoolValue("roomchat",true))
	return MARKUP_ROOMCHAT;
    return MARKUP_ROOMCHATPRIVATE;
}

// Show a confirm dialog box in a given window
static bool showConfirm(Window* wnd, const char* text, const char* context)
{
    static const String name = "archive_confirm";
    if (!Client::valid())
	return false;
    NamedList p("");
    p.addParam("text",text);
    p.addParam("property:" + name + ":_yate_context",context);
    return Client::self()->createDialog("confirm",wnd,String::empty(),name,&p);
}

// Show an error dialog box in a given window
static void showError(Window* wnd, const char* text)
{
    static const String name = "archive_error";
    if (!Client::valid())
	return;
    NamedList p("");
    p.addParam("text",text);
    Client::self()->createDialog("message",wnd,String::empty(),name,&p);
}

// Show a dialog used to notify a status and freeze the window
static void showFreezeDlg(Window* w, const String& name, const char* text)
{
    NamedList p("");
    p.addParam("text",text);
    p.addParam("show:button_hide",String::boolText(false));
    p.addParam("_yate_windowflags","title");
    p.addParam("closable","false");
    Client::self()->createDialog("message",w,"Archive",name,&p);
}

// Retrieve the previuos item from a list
static ObjList* getListPrevItem(const ObjList& list, const String& value)
{
    ObjList* last = 0;
    ObjList* o = list.skipNull();
    for (; o; o = o->skipNext()) {
	if (o->get()->toString() == value)
	    break;
	last = o;
    }
    return o ? last : 0;
}

// Retrieve the last item from a list
static ObjList* getListLastItem(const ObjList& list)
{
    ObjList* last = 0;
    for (ObjList* o = list.skipNull(); o; o = o->skipNext())
	last = o;
    return last;
}

// Retrieve the chat type string
inline const String& chatType(int type)
{
    static const String s_out = "chat_out";
    static const String s_in = "chat_in";
    static const String s_delayed = "chat_delayed";
    if (type == MARKUP_SENT)
	return s_out;
    if (type == MARKUP_RECV)
	return s_in;
    if (type == MARKUP_DELAYED)
	return s_delayed;
    return String::empty();
}

// Retrieve the UI item type from chat file type
static inline const char* uiItemType(char type)
{
    if (type == MARKUP_CHAT)
	return "chat";
    if (type == MARKUP_ROOMCHAT)
	return "roomchat";
    return "roomprivchat";
}

// Find 2 NULL values in a buffer. Return buffer len if not found
unsigned int find2Null(unsigned char* buf, unsigned int len)
{
    for (unsigned int n = 0; n < len; n++) {
	if (buf[n] == 0 && (n < len - 1) && (buf[n + 1] == 0))
	    return n;
    }
    return len;
}

// Find a line in text buffer (until CR/LF, single CR or LF).
// Return the line length, excluding the line terminator
unsigned int findLine(const char* buf, unsigned int len, unsigned int& eolnLen)
{
    eolnLen = 0;
    if (!buf)
	return 0;
    unsigned int i = 0;
    for (; i < len; i++) {
	if (buf[i] == '\r') {
	    if (i < len - 1 && buf[i + 1] == '\n')
		eolnLen = 2;
	    else
		eolnLen = 1;
	    return i;
	}
	if (buf[i] == '\n') {
	    eolnLen = 1;
	    return i;
	}
    }
    return i;
}

// Append a string to data block including the terminator
static void appendString(DataBlock& buf, const String& src)
{
    if (src) {
	DataBlock tmp;
	tmp.assign((void*)src.c_str(),src.length() + 1,false);
	buf += tmp;
	tmp.clear(false);
    }
    else
	buf += s_zeroDb;
}

// Append an integer value to a data block including a null terminator
static inline void appendInt(DataBlock& buf, int value)
{
    String tmp(value);
    appendString(buf,tmp);
}

// Build chat file UI params
static NamedList* chatFileUiParams(ChatFile* f)
{
    if (!f)
	return 0;
    Lock lock(f);
    NamedList* upd = new NamedList(f->toString());
    upd->addParam("item_type",uiItemType(f->type()));
    upd->addParam("account",f->account());
    upd->addParam("contact",f->contact());
    if (f->type() == MARKUP_CHAT)
	upd->addParam("name",f->contactDisplayName());
    else if (f->type() == MARKUP_ROOMCHAT)
	upd->addParam("name",f->contact());
    else {
        upd->addParam("parent",f->roomId());
	upd->addParam("name",f->contactDisplayName());
    }
    return upd;
}

// Build a chat session UI params
static NamedList* chatSessionUiParams(ChatSession* s)
{
    if (!s)
	return 0;
    NamedList* upd = new NamedList(s->toString());
    String time;
    Client::self()->formatDateTime(time,(unsigned int)s->m_name.toInteger(),
	"yyyy.MM.dd hh:mm:ss",false);
    // Show the first 2 lines from description
    unsigned int len = s->m_desc.length();
    unsigned int tmp = 0;
    unsigned int ln = findLine(s->m_desc.c_str(),len,tmp);
    if (ln != len) {
	len = ln + tmp;
	unsigned int tmp2 = 0;
	ln = findLine(s->m_desc.c_str() + len,s->m_desc.length() - len,tmp2);
	if (!ln)
	    len -= tmp;
	else
	    len += ln;
    }
    String desc;
    if (len == s->m_desc.length())
	desc = s->m_desc;
    else
	desc = s->m_desc.substr(0,len);
    desc.trimBlanks();
    upd->addParam("datetime",time);
    upd->addParam("description",desc);
    upd->addParam("property:toolTip",time + "\r\n" + s->m_desc);
    return upd;
}

// Enable/disable search
static void enableSearch(bool ok)
{
    Window* w = getWindow();
    if (!w)
	return;
    const char* text = String::boolText(ok);
    NamedList p("");
    p.addParam("active:" + s_searchShow,text);
    p.addParam("active:" + s_searchHide,text);
    p.addParam("active:" + s_searchEdit,text);
    p.addParam("active:" + s_searchStart,text);
    p.addParam("active:" + s_searchPrev,text);
    p.addParam("active:" + s_searchNext,text);
    p.addParam("active:" + s_searchRange,text);
    p.addParam("active:" + s_searchMatchCase,text);
    p.addParam("active:" + s_searchHighlightAll,text);
    p.addParam("active:" + s_actionRefresh,text);
    Client::self()->setParams(&p,w);
}


/*
 * ChatFile
 */
// Init object
ChatFile::ChatFile(const String& dir, const String& fileName)
    : Mutex(true,"Archive::ChatFile"),
    m_version(Current),
    m_type(MARKUP_CHAT),
    m_fileName(fileName),
    m_full(dir + "/" + fileName),
    m_hdrLen(0),
    m_newSessionOffset(0),
    m_sessionsLoaded(false)
{
}

// Load the file. Created it if not found and params are given
bool ChatFile::loadFile(const NamedList* params, String* error)
{
    Lock lock(this);
    closeSession();
    m_file.terminate();
    m_sessionsLoaded = false;
    m_sessions.clear();
    bool ok = m_file.openPath(m_full,true,true,params != 0,true,true);
    if (!ok)
	return setFileError(error,"open",true);
    int64_t sz = m_file.length();
    if (sz < 0)
	return setFileError(error,"get length",true);
    // Read/write file header
    if (sz) {
	if (!readFileHeader(error))
	    return false;
    }
    else if (!(params && writeFileHeader(*params,error)))
	return false;
    m_roomId.clear();
    // Build the room id if this is a private chat
    if (m_type == MARKUP_ROOMCHATPRIVATE)
	ChatArchive::buildChatFileName(m_roomId,MARKUP_ROOMCHAT,m_account,m_contact);
    return true;
}

// Write chat to file
bool ChatFile::writeChat(const NamedList& params)
{
    Lock lock(this);
    const String& text = params["text"];
    if (!text)
	return false;
    String time = params["time"];
    if (!time)
	time = (int)Time::now();
    if (!m_newSessionOffset) {
	m_newSessionOffset = m_file.seek(Socket::SeekEnd);
	if (m_newSessionOffset < m_hdrLen)
	    return false;
	String tmp;
	tmp << MARKUP_SESSION_START << time;
	tmp << MARKUP_SESSION_DESC << text;
	m_writeBuffer.append(tmp);
	m_writeBuffer += s_zeroDb;
	m_writeBuffer += s_zeroDb;
    }
    m_writeBuffer.append(time);
    String type;
    if (params.getBoolValue("send"))
	type = MARKUP_SENT;
    else if (!params.getBoolValue("delayed"))
	type = MARKUP_RECV;
    else
	type = MARKUP_DELAYED;
    m_writeBuffer.append(type);
    appendString(m_writeBuffer,params["sender"]);
    appendString(m_writeBuffer,text);
    m_writeBuffer += s_zeroDb;
    int wr = writeData(m_writeBuffer.data(),m_writeBuffer.length(),0);
    if (wr < 0)
	return false;
    if (wr) {
	if (wr != (int)m_writeBuffer.length())
	    m_writeBuffer.cut(-wr);
	else
	    m_writeBuffer.clear();
    }
    return true;
}

// Load sessions from file
bool ChatFile::loadSessions(bool forceLoad, String* error)
{
    Lock lock(this);
    if (m_sessionsLoaded && !forceLoad)
	return true;
    m_sessionsLoaded = true;
    m_sessions.clear();
    int64_t offset = m_hdrLen;
    if (!seekFile(offset,error))
	return false;
    String prefix(toString() + "/");
    unsigned int index = 0;
    char rdBuf[READ_BUFFER];
    DataBlock buf;
    ChatSession* s = 0;
    bool ok = true;
    while (true) {
	int rd = m_file.readData(rdBuf,sizeof(rdBuf));
	if (rd < 0) {
	    ok = setFileError(error,"read");
	    break;
	}
	if (!rd)
	    break;
	if (exiting())
	    break;
	buf.append(rdBuf,rd);
	unsigned int n = find2Null((unsigned char*)buf.data(),buf.length());
	while (n < buf.length()) {
	    if (exiting())
		break;
	    String str((const char*)buf.data(),n);
	    if ((str.length() > 1) && (str[0] == MARKUP_SESSION_START)) {
		if (s)
		    s->m_length = offset - s->m_offset;
		int pos = str.find(MARKUP_SESSION_DESC);
		s = new ChatSession(prefix + String(++index),
		    str.substr(1,pos > 0 ? pos - 1 : 0),offset);
		if (pos > 0)
		    s->m_desc = str.substr(pos + 1);
		m_sessions.append(s);
	    }
	    n += 2;
	    offset += n;
	    buf.cut(-(int)n);
	    n = find2Null((unsigned char*)buf.data(),buf.length());
	}
    }
    if (!exiting()) {
	// Finalize the last session
	if (s)
	    s->m_length = offset + buf.length() - s->m_offset;
    }
    else {
	m_sessionsLoaded = false;
	m_sessions.clear();
    }
    return ok;
}

// Load a session from file
// This method is thread safe
bool ChatFile::loadSession(const String& id, ObjList& list, String* error,
    QString* search)
{
    if (!id)
	return false;
    Lock lock(this);
    ObjList* o = m_sessions.find(id);
    if (!o)
	return false;
    ChatSession* s = static_cast<ChatSession*>(o->get());
    if (!seekFile(s->m_offset,error))
	return false;
    bool find = search != 0;
    Qt::CaseSensitivity cs = s_matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
    char rdBuf[READ_BUFFER];
    DataBlock buf;
    bool hdrFound = false;
    bool ok = !find;
    int64_t processed = 0;
    while (processed < s->m_length && !exiting()) {
	int rd = m_file.readData(rdBuf,sizeof(rdBuf));
	if (rd < 0) {
	    ok = setFileError(error,"read");
	    break;
	}
	if (!rd)
	    break;
	buf.append(rdBuf,rd);
	unsigned int n = find2Null((unsigned char*)buf.data(),buf.length());
	while (n < buf.length()) {
	    if (exiting())
		break;
	    if (hdrFound) {
		ChatItem* entry = decodeChat(find,s->m_offset + processed,buf.data(),n);
		if (entry) {
		    if (!find)
			list.append(entry);
		    else {
			int pos = entry->m_search.indexOf(*search,0,cs);
			TelEngine::destruct(entry);
			if (pos >= 0) {
			    ok = true;
			    break;
			}
		    }
		}
	    }
	    else
		hdrFound = true;
	    n += 2;
	    processed += n;
	    buf.cut(-(int)n);
	    if (processed >= s->m_length)
		break;
	    n = find2Null((unsigned char*)buf.data(),buf.length());
	}
	if (find && ok)
	    break;
    }
    if (!exiting()) {
	if (processed < s->m_length && !(find && ok))
	    Debug(ClientDriver::self(),DebugNote,
		"File '%s' unexpected end of session at offset " FMT64U,
		m_full.c_str(),s->m_offset + processed);
    }
    else
	list.clear();
    return ok;
}

// Retrieve the last session. Lock the object before use
ChatSession* ChatFile::lastSession()
{
    if (!m_sessionsLoaded)
	loadSessions();
    ObjList* o = getListLastItem(m_sessions);
    return o ? static_cast<ChatSession*>(o->get()) : 0;
}

// Close current write session. Load it if sessions were loaded
bool ChatFile::closeSession()
{
    Lock lock(this);
    if (m_newSessionOffset && m_writeBuffer.length())
	writeData(m_writeBuffer.data(),m_writeBuffer.length(),0);
    m_writeBuffer.clear();
    bool ok = m_sessionsLoaded && m_newSessionOffset;
    if (ok) {
	m_sessionsLoaded = false;
	m_sessions.clear();
	loadSessions();
    }
    m_newSessionOffset = 0;
    return ok;
}

// Decode a ChatItem from a given buffer. Return it on success
ChatItem* ChatFile::decodeChat(bool search, int64_t offset, void* buffer,
    unsigned int len)
{
    unsigned char* buf = (unsigned char*)buffer;
    if (!(buf && len))
	return 0;
    unsigned int i = 0;
    // Get time
    for (; i < len; i++) {
	switch (buf[i]) {
	    case '0':
	    case '1':
	    case '2':
	    case '3':
	    case '4':
	    case '5':
	    case '6':
	    case '7':
	    case '8':
	    case '9':
		continue;
	}
	break;
    }
    int time = 0;
    if (i) {
	String tmp((const char*)buf,i);
	time = tmp.toInteger();
    }
    else
	showEntryError(DebugNote,"Invalid time",offset);
    if (i == len) {
	showEntryError(DebugNote,"Missing type",offset);
	return 0;
    }
    int type = buf[i++];
    switch (type) {
	case MARKUP_SENT:
	case MARKUP_RECV:
	case MARKUP_DELAYED:
	    break;
	case 0:
	    showEntryError(DebugNote,"Missing type",offset);
	    return 0;
	default:
	    showEntryError(DebugStub,"Unknown type",offset);
    }
    if (i == len) {
	showEntryError(DebugNote,"Unexpected end of entry after type",offset);
	return 0;
    }
    ChatItem* entry = new ChatItem(time,type);
    entry->m_senderName.assign((const char*)buf + i,len - i);
    i += entry->m_senderName.length();
    if (i >= len) {
	showEntryError(DebugNote,"Unexpected end of chat item after sender name",offset);
	return entry;
    }
    if (buf[i++] != 0) {
	showEntryError(DebugMild,"Expecting NULL after sender name",offset);
	return entry;
    }
    if (i == len)
	return entry;
    if (!search) {
	entry->m_text.assign((const char*)buf + i,len - i);
	i += entry->m_text.length();
    }
    else {
	unsigned int start = i;
	while (i < len && buf[i])
	    i++;
	QByteArray a((const char*)buf + start,i - start);
	entry->m_search = a;
    }
    if (i < len)
	showEntryError(DebugStub,"Got garbage after text",offset);
    return entry;
}

// Set file last error. Close it if requested. Return false
bool ChatFile::setFileError(String* error, const char* oper, bool close, bool del)
{
    String tmp;
    if (!error)
	error = &tmp;
    int code = Thread::lastError();
    Thread::errorString(*error,code);
    Debug(ClientDriver::self(),DebugNote,"File '%s' %s error: %d %s",m_full.c_str(),
	oper,code,error->c_str());
    if (close) {
	Debug(ClientDriver::self(),DebugInfo,"Closing file '%s'",m_full.c_str());
	m_file.terminate();
    }
    if (del) {
	Debug(ClientDriver::self(),DebugInfo,"Removing file '%s'",m_full.c_str());
	File::remove(m_full);
    }
    return false;
}

// Write a string to the file
int ChatFile::writeData(const void* buf, unsigned int len, String* error)
{
    if (m_file.seek(Stream::SeekEnd) <= 0) {
	setFileError(error,"seek");
	return -1;
    }
    int wr = m_file.writeData(buf,len);
    if (wr != (int)len && !m_file.canRetry())
	setFileError(error,"write");
    return wr;
}

// Write file header. Close the file if fails
bool ChatFile::readFileHeader(String* error)
{
    m_hdrLen = 0;
    m_version = Invalid;
    if (!seekFile(0,error)) {
	m_file.terminate();
	return false;
    }
    DataBlock buf;
    unsigned char b[1024];
    while (true) {
	int rd = m_file.readData(b,sizeof(b));
	if (rd < 0)
	    return setFileError(error,"read",true,false);
	if (!rd)
	    return setFileError(error,"short header",true,false);
	unsigned int n = find2Null(b,rd);
	buf.append(b,n);
	if (n < (unsigned int)rd)
	    break;
    }
    if (!buf.length())
	return setFileError(error,"short header",true,false);
    unsigned int len = buf.length();
    const char* s = (const char*)buf.data();
    String str;
    bool acc = false;
    bool cont = false;
    bool contName = false;
    while (s) {
	String str(s,len);
	if (str.length() != len) {
	    len = len - str.length() - 1;
	    if (len)
		s += str.length() + 1;
	    else
		s = 0;
	}
	else {
	    len = 0;
	    s = 0;
	}
	if (m_version == Invalid) {
	    if (!str)
		return setFileError(error,"invalid header",true,false);
	    m_version = str.toInteger();
	    if (m_version == Invalid || m_version > Current)
		return setFileError(error,"unsupported version",true,false);
	}
	else if (!acc) {
	    m_account = str;
	    acc = true;
	}
	else if (!cont) {
	    m_contact = str;
	    cont = true;
	}
	else if (!contName) {
	    m_contactName = str;
	    contName = true;
	}
	else {
	    m_type = 0;
	    if (str.length() == 1)
		m_type = str[0];
	    if (m_type != MARKUP_CHAT && m_type != MARKUP_ROOMCHAT &&
		m_type != MARKUP_ROOMCHATPRIVATE)
		return setFileError(error,"unsupported chat type",true,false);
	    break;
	}
    }
    m_hdrLen = buf.length() + 2;
    return true;
}

// Write file header. Close the file and delete it if fails
bool ChatFile::writeFileHeader(const NamedList& params, String* error)
{
    m_account = params["account"];
    m_contact = params["contact"];
    m_contactName = params["contactname"];
    m_type = chatType(params);
    DataBlock buf;
    appendInt(buf,m_version);
    appendString(buf,m_account);
    appendString(buf,m_contact);
    appendString(buf,m_contactName);
    buf.append(&m_type,1);
    buf += s_zeroDb;
    buf += s_zeroDb;
    if (m_file.writeData(buf.data(),buf.length()) != (int)buf.length())
	return setFileError(error,"write",true,true);
    m_hdrLen = buf.length();
    return true;
}


/*
 * ChatArchive
 */
ChatArchive::ChatArchive()
    : Mutex(true,"ChatArchive"),
    m_loaded(false)
{
}

// Init data when client starts
void ChatArchive::init()
{
    m_dir = Engine::runParams().getValue("usercfgpath");
    m_dir << "/archive";
    if (!File::exists(m_dir))
	File::mkDir(m_dir);
    m_index = m_dir + "/index.conf";
    m_index.load();
}

// Refresh the list. Re-load all archive
void ChatArchive::refresh()
{
    Lock lock(this);
    m_loaded = true;
    unsigned int n = m_index.sections();
    for (unsigned int i = 0; i < n; i++) {
	if (exiting())
	    break;
	NamedList* sect = m_index.getSection(i);
	if (!sect)
	    continue;
	const String& type = (*sect)["type"];
	if (type.length() != 1)
	    continue;
	if (type[0] != MARKUP_CHAT && type[0] != MARKUP_ROOMCHAT &&
	    type[0] != MARKUP_ROOMCHATPRIVATE)
	    continue;
	ChatFile* f = loadChatFile(*sect,true);
	TelEngine::destruct(f);
    }
}

// Clear all
void ChatArchive::clear(bool memoryOnly)
{
    Lock lock(this);
    m_items.clear();
    if (memoryOnly)
	return;
    unsigned int n = m_index.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* f = m_index.getSection(i);
	if (f)
	    File::remove(m_dir + "/" + *f);
    }
    m_index.clearSection();
    m_index.save();
}

// Clear all logs belonging to a given account
void ChatArchive::clearAccount(const String& account, ObjList& removedItems)
{
    if (!account)
	return;
    Lock lock(this);
    String prefix("chat_" + String(account.hash()) + "_");
    unsigned int n = m_index.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* f = m_index.getSection(i);
	if (f && f->startsWith(prefix,false)) {
	    m_items.remove(*f);
	    removedItems.append(new String(*f));
	    File::remove(m_dir + "/" + *f);
	}
    }
    for (ObjList* o = removedItems.skipNull(); o; o = o->skipNext())
	m_index.clearSection(o->get()->toString());
    m_index.save();
}

// Remove an item and it's file
void ChatArchive::delFile(const String& id)
{
    if (!id)
	return;
    Lock lock(this);
    m_items.remove(id);
    File::remove(m_dir + "/" + id);
    m_index.clearSection(id);
    m_index.save();
}

// Retrieve a chat file. Return a referenced object
ChatFile* ChatArchive::loadChatFile(const String& file, bool forceLoad)
{
    Lock lock(this);
    ChatFile* f = getChatFile(file);
    if (!f) {
	f = new ChatFile(m_dir,file);
	if (!f->loadFile(0,0)) {
	    TelEngine::destruct(f);
	    return 0;
	}
	f->ref();
	m_items.append(f);
    }
    lock.drop();
    f->loadSessions(forceLoad);
    return f;
}

// Retrieve a chat file. Return a refferenced object
ChatFile* ChatArchive::getChatFile(const String& id)
{
   Lock lock(this);
   ObjList* o = m_items.find(id);
   if (!o)
       return 0;
   ChatFile* f = static_cast<ChatFile*>(o->get());
   f->ref();
   return f;
 }

// Retrieve a chat file. Return a refferenced object
ChatFile* ChatArchive::getChatFile(const NamedList& params,
    const NamedList* createParams)
{
    String fn;
    buildChatFileName(fn,params);
    Lock lock(this);
    ChatFile* f = getChatFile(fn);
    if (f)
	return f;
    f = new ChatFile(m_dir,fn);
    if (!f->loadFile(createParams,0)) {
	TelEngine::destruct(f);
	return 0;
    }
    f->lock();
    m_index.setValue(fn,"type",String(f->type()));
    m_index.setValue(fn,"account",f->account());
    m_index.setValue(fn,"contact",f->contact());
    if (f->contactName() && f->contactName() != m_index.getValue(fn,"contactname"))
	m_index.setValue(fn,"contactname",f->m_contactName);
    if (f->type() != MARKUP_ROOMCHATPRIVATE)
	m_index.clearKey(fn,"room");
    else
	m_index.setValue(fn,"room",f->roomId());
    f->unlock();
    m_index.save();
    m_items.append(f);
    f->ref();
    return f;
}

// Add a chat message to log
bool ChatArchive::logChat(NamedList& params)
{
    ChatFile* f = getChatFile(params,&params);
    bool ok = f && f->writeChat(params);
    TelEngine::destruct(f);
    return ok;
}

// Close a chat session. Add it to the ui if the contact is shown
ChatFile* ChatArchive::closeChat(const NamedList& params)
{
    ChatFile* f = getChatFile(params);
    if (f && f->closeSession())
	return f;
    TelEngine::destruct(f);
    return 0;
}

// Build a file name from a list of parameters
void ChatArchive::buildChatFileName(String& buf, char type, const String& account,
    const String& contact, const String& nick)
{
    buf = "chat_";
    buf << account.hash() << "_" << String(contact).toLower().hash();
    if (type == MARKUP_ROOMCHATPRIVATE)
	buf << "_" << nick.hash();
    buf << "_" << type;
}

// Build a file name from a list of parameters
bool ChatArchive::buildChatFileName(String& buf, const NamedList& params)
{
    const String& account = params["account"];
    const String& contact = params["contact"];
    if (!(account && contact))
	return false;
    char type = chatType(params);
    const String& nick = (type != MARKUP_ROOMCHATPRIVATE) ?
	String::empty() : params["contactname"];
    if (type == MARKUP_ROOMCHATPRIVATE && !nick)
	return false;
    buildChatFileName(buf,type,account,contact,nick);
    return true;
}


/*
 * CALogic
 */
CALogic::CALogic(int prio)
    : ClientLogic("clientarchive",prio),
    m_resetSearchOnSel(true),
    m_searchThread(0),
    m_refreshThread(0)
{
}

CALogic::~CALogic()
{
}

bool CALogic::initializedClient()
{
    Window* w = getWindow();
    // Update archive search range
    for (const TokenDict* d = s_searchListRange; d->value; d++)
	Client::self()->addOption(s_searchRange,d->token,false,String::empty(),w);
    Client::self()->setSelect(s_searchRange,lookup(s_range,s_searchListRange),w);
    // Load options
    NamedList dummy("");
    NamedList* arch = Client::s_settings.getSection("clientarchive");
    if (!arch)
	arch = &dummy;
    // Setup window
    if (w) {
	const char* no = String::boolText(false);
	NamedList p("");
	p.addParam("show:archive_frame_search",no);
	Client::self()->setParams(&p,w);
    }
    return false;
}

void CALogic::exitingClient()
{
    Client::self()->setVisible(s_wndArch,false);
    // Clear data now: close sessions
    s_chatArchive.clear(true);
    // Stop workers
    searchStop();
    refreshStop();
}

void CALogic::engineStart(Message& msg)
{
    s_chatArchive.init();
}

bool CALogic::action(Window* wnd, const String& name, NamedList* params)
{
    String act = name;
    if (act.startSkip(s_archPrefix,false)) {
	// Chat log actions nedding parameters
	if (params) {
	    if (act == s_actionLogChat)
		return s_chatArchive.logChat(*params);
	    if (act == s_actionCloseChat)
		return closeChat(*params);
	    if (act == s_actionSelectChat) {
		Window* w = getWindow();
		if (w) {
		    String id;
		    ChatArchive::buildChatFileName(id,*params);
		    if (s_chatArchive.loaded())
			Client::self()->setSelect(s_logList,id,w);
		    else
			refreshStart(&id);
		    Client::self()->setVisible(s_wndArch,true,true);
		}
		return w != 0;
	    }
	    if (act == s_actionClearAccNow) {
		ObjList removed;
		s_chatArchive.clearAccount((*params)["account"],removed);
		Window* w = getWindow();
		if (w)
		    for (ObjList* o = removed.skipNull(); o; o = o->skipNext())
			Client::self()->delTableRow(s_logList,o->get()->toString(),w);
		return true;
	    }
	    if (act == "savesession")
		return saveSession(wnd,params);
	    return false;
	}
	bool confirm = (act == s_actionClear);
	if (confirm || act == s_actionClearNow)
	    return clearLog(confirm ? wnd : 0);
	confirm = (act == s_actionDelContact);
	if (confirm || act == s_actionDelContactNow)
	    return delContact(confirm ? wnd : 0);
    }
    // Refresh all
    if (name == s_actionRefresh) {
	refreshStart();
	return true;
    }
    // Search
    bool next = (name == s_searchNext || name == s_searchStart);
    if (next || name == s_searchPrev) {
	String tmp;
	Client::self()->getText(s_searchEdit,tmp,false,wnd);
	Lock lock(s_mutex);
	if (m_searchThread) {
	    if (m_searchText != tmp) {
		resetSearchHistory();
		m_searchText = tmp;
	    }
	    m_searchThread->startSearching(m_searchText,next);
	}
	return true;
    }
    bool showSearch = (name == s_searchShow);
    if (showSearch || name == s_searchHide) {
	searchStop();
	Window* w = getWindow();
	if (showSearch) {
	    if (!w)
		return false;
	    Client::self()->setFocus(s_searchEdit,false,w);
	    Lock lock(s_mutex);
	    m_searchThread = new CASearchThread;
	    m_searchThread->startup();
 	}
	else
	    resetSearchHistory();
	Client::self()->setShow("archive_frame_search",showSearch,w);
	return true;
    }
    if (name == "archive_save_session")
	return saveSession(wnd);
    return false;
}

bool CALogic::select(Window* wnd, const String& name, const String& item,
    const String& text)
{
    // Selection changed in log list
    if (name == s_logList) {
	updateSessions(item,wnd);
	return true;
    }
    // Selection changed in sessions list
    if (name == s_sessList) {
	if (m_resetSearchOnSel)
	    resetSearchHistory(false);
	return updateSession(item,wnd);
    }
    // Search range
    if (name == s_searchRange) {
	int r = lookup(item,s_searchListRange);
	if (r)
	    s_range = (CASearchRange)r;
	return true;
    }
    return false;
}

bool CALogic::toggle(Window* wnd, const String& name, bool active)
{
    // Search options
    if (name == s_searchMatchCase) {
	s_matchCase = active;
	return true;
    }
    if (name == s_searchHighlightAll) {
	s_highlightAll = active;
	return true;
    }
    // Window visibility changed
    if (name == "window_visible_changed") {
	if (wnd && wnd->id() == s_wndArch) {
	    if (active && !s_chatArchive.loaded())
		refreshStart();
	}
	return false;
    }
    return false;
}

// Stop the search thread and wait for terminate
void CALogic::searchStop()
{
    s_mutex.lock();
    if (m_searchThread)
	m_searchThread->cancel(false);
    s_mutex.unlock();
    while (m_searchThread)
	Thread::idle();
}

// Start archive refresh
void CALogic::refreshStart(const String* selected)
{
    Window* w = getWindow();
    if (!w)
	return;
    Lock lock(s_mutex);
    if (selected)
	m_selectAfterRefresh = *selected;
    if (m_refreshThread)
	return;
    m_refreshThread = new CARefreshThread;
    lock.drop();
    showFreezeDlg(w,"archive_refresh","Refreshing ....");
    m_refreshThread->startup();
}

// Archive refresh terminated. Refresh UI
void CALogic::refreshTerminated()
{
    s_mutex.lock();
    String sel = m_selectAfterRefresh;
    m_refreshThread = 0;
    m_selectAfterRefresh.clear();
    Window* w = !exiting() ? getWindow() : 0;
    s_mutex.unlock();
    if (!w)
	return;
    // Update UI
    int count = 10;
    s_chatArchive.lock();
    NamedList p("");
    for (ObjList* o = s_chatArchive.items().skipNull(); o; o = o->skipNext()) {
	if (exiting())
	    break;
	ChatFile* f = static_cast<ChatFile*>(o->get());
	Lock lock(f);
	f->loadSessions();
	NamedList* upd = chatFileUiParams(f);
	// Check if the room is already displayed. Create it if not found
	if (f->type() == MARKUP_ROOMCHATPRIVATE && f->roomId() &&
	    !(p.getParam(f->roomId()) ||
	    Client::self()->getTableRow(s_logList,f->roomId(),0,w))) {
	    NamedList* upd2 = 0;
	    ChatFile* parent = s_chatArchive.getChatFile(f->roomId());
	    if (parent)
		upd2 = chatFileUiParams(parent);
	    else {
		upd2 = new NamedList("");
		upd2->addParam("item_type",uiItemType(MARKUP_ROOMCHAT));
		upd2->addParam("account",f->account());
		upd2->addParam("contact",f->contact());
		upd2->addParam("name",f->contact());
	    }
	    p.addParam(new NamedPointer(f->roomId(),upd2,String::boolText(true)));
	    TelEngine::destruct(parent);
	}
	p.addParam(new NamedPointer(f->toString(),upd,String::boolText(true)));
	count--;
	if (!count) {
	    count = 10;
	    Client::self()->updateTableRows(s_logList,&p,false,w);
	    p.clear();
	}
    }
    s_chatArchive.unlock();
    if (!exiting()) {
	Client::self()->updateTableRows(s_logList,&p,false,w);
	if (sel)
	    Client::self()->setSelect(s_logList,sel,w);
    }
    Client::self()->closeDialog("archive_refresh",w);
}

// Stop the refresh thread and wait for terminate
void CALogic::refreshStop()
{
    s_mutex.lock();
    if (m_refreshThread)
	m_refreshThread->cancel(false);
    s_mutex.unlock();
    while (m_refreshThread)
	Thread::idle();
}

// Close a chat session
bool CALogic::closeChat(const NamedList& params)
{
    ChatFile* f = s_chatArchive.closeChat(params);
    Window* w = f ? getWindow() : 0;
    if (w) {
	String tmp;
	Client::self()->getSelect(s_logList,tmp,w);
	if (tmp == f->toString()) {
	    NamedList p("");
	    f->lock();
	    ChatSession* s = f->lastSession();
	    if (s) {
		NamedList* upd = chatSessionUiParams(s);
		p.addParam(new NamedPointer(s->toString(),upd,String::boolText(true)));
	    }
	    f->unlock();
	    Client::self()->updateTableRows(s_sessList,&p,false,w);
	}
    }
    TelEngine::destruct(f);
    return true;
}

// Update sessions related to a given item
bool CALogic::updateSessions(const String& id, Window* wnd)
{
    if (!Client::self())
	return false;
    Client::self()->clearTable(s_sessList,wnd);
    ChatFile* f = id ? s_chatArchive.getChatFile(id) : 0;
    if (!f)
	return true;
    f->lock();
    NamedList p("");
    for (ObjList* o = f->sessions().skipNull(); o; o = o->skipNext()) {
	ChatSession* s = static_cast<ChatSession*>(o->get());
	NamedList* upd = chatSessionUiParams(s);
	p.addParam(new NamedPointer(s->toString(),upd,String::boolText(true)));
    }
    f->unlock();
    TelEngine::destruct(f);
    Client::self()->updateTableRows(s_sessList,&p,false,wnd);
    return true;
}

// Update session content in UI
bool CALogic::updateSession(const String& id, Window* wnd)
{
    if (!Client::self())
	return false;
    Client::self()->clearTable(s_sessHistory,wnd);
    ChatFile* f = s_chatArchive.getChatFileBySession(id);
    if (!f)
	return true;
    f->lock();
    ObjList list;
    f->loadSession(id,list);
    NamedList p("");
    for (ObjList* o = list.skipNull(); o; o = o->skipNext()) {
	ChatItem* e = static_cast<ChatItem*>(o->get());
	NamedList* upd = new NamedList("");
	String time;
	if (e->m_type != MARKUP_DELAYED)
	    Client::self()->formatDateTime(time,(unsigned int)e->m_time,"hh:mm:ss",false);
	else
	    Client::self()->formatDateTime(time,(unsigned int)e->m_time,"dd.MM.yyyy hh:mm:ss",false);
	upd->addParam("time",time);
	upd->addParam("text",e->m_text);
	NamedString* sender = new NamedString("sender",e->m_senderName);
	if (sender->null()) {
	    if (e->m_type == MARKUP_SENT)
		*sender = "me";
	    else
		*sender = f->contactDisplayName();
	}
	upd->addParam(sender);
	p.addParam(new NamedPointer(chatType(e->m_type),upd,String::boolText(true)));
    }
    f->unlock();
    TelEngine::destruct(f);
    Client::self()->addLines(s_sessHistory,&p,0,false,wnd);
    return true;
}

// Set control highlight
bool CALogic::setSearchHistory(const String& what, bool next)
{
    Window* w = getWindow();
    if (!w)
	return false;
    NamedList p(s_sessHistory);
    NamedList* upd = new NamedList("");
    p.addParam(new NamedPointer("search",upd,String::boolText(true)));
    upd->addParam("find",what);
    upd->addParam("matchcase",String::boolText(s_matchCase));
    upd->addParam("all",String::boolText(s_highlightAll));
    upd->addParam("next",String::boolText(next));
    return Client::self()->setParams(&p,w);
}

// Reset control highlight
bool CALogic::resetSearchHistory(bool reset)
{
    Window* w = getWindow();
    if (!w)
	return false;
    NamedList p(s_sessHistory);
    NamedList* upd = new NamedList("");
    p.addParam(new NamedPointer("search",upd,String::boolText(false)));
    upd->addParam("reset",String::boolText(reset));
    return Client::self()->setParams(&p,w);
}

// Select and set search history. Return true on success
bool CALogic::setSearch(bool reset, const String& file, const String& session,
    const String& what, bool next)
{
    Window* w = getWindow();
    if (!w)
	return false;
    m_resetSearchOnSel = reset;
    Client::self()->setSelect(s_logList,file,w);
    bool ok = Client::self()->setSelect(s_sessList,session,w) && setSearchHistory(what,next);
    m_resetSearchOnSel = true;
    return ok;
}

// Save current session
bool CALogic::saveSession(Window* wnd, NamedList* params)
{
    if (!Client::valid())
	return false;
    String id;
    Window* w = getWindow();
    if (!w)
	return false;
    Client::self()->getSelect(s_sessList,id,w);
    if (!id)
	return false;
    if (!params && wnd) {
	NamedList p("");
	p.addParam("action",s_archPrefix + "savesession");
	p.addParam("save",String::boolText(true));
	p.addParam("filters","Text files (*.txt)|All files (*)");
	p.addParam("chooseanyfile",String::boolText(true));
	return Client::self()->chooseFile(wnd,p);
    }
    if (!params)
	return false;
    const String& file = (*params)["file"];
    if (!file)
	return true;
    const char* oper = 0;
    while (true) {
	File::remove(file);
	File f;
	if (!f.openPath(file,true,false,true)) {
	    oper = "open";
	    break;
	}
	String data;
	Client::self()->getText(s_sessHistory,data,false,w);
	int retry = 10;
	unsigned int len = data.length();
	const char* s = data.c_str();
	String lineBuf;
	while (retry && (len || lineBuf)) {
	    if (!lineBuf) {
		unsigned int eolnLen = 0;
		unsigned int ln = findLine(s,len,eolnLen);
		if (eolnLen == 2)
		    lineBuf.assign(s,ln + 2);
		else {
		    lineBuf.assign(s,ln);
		    lineBuf << "\r\n";
		}
		ln += eolnLen;
		s += ln;
		len -= ln;
	    }
	    int wr = f.writeData(lineBuf.c_str(),lineBuf.length());
	    if (wr > 0) {
		if ((unsigned int)wr == lineBuf.length())
		    lineBuf.clear();
		else
		    lineBuf = lineBuf.substr(wr);
	    }
	    else if (!wr)
		Thread::msleep(2);
	    else if (f.canRetry())
		retry--;
	    else {
		oper = "write";
		break;
	    }
	}
	break;
    }
    if (!oper)
	return true;
    String error;
    Thread::errorString(error);
    String text;
    text << "Failed to " << oper << " '" << file << "'";
    text.append(error,"\r\n");
    showError(wnd,text);
    return false;
}

// Clear all archive
bool CALogic::delContact(Window* wnd)
{
    String id;
    Window* w = getWindow();
    if (!w)
	return false;
    Client::self()->getSelect(s_logList,id,w);
    if (!id)
	return false;
    if (wnd &&
	showConfirm(wnd,"Confirm selected contact log delete?",s_archPrefix + s_actionDelContactNow))
	return true;
    s_chatArchive.delFile(id);
    Client::self()->delTableRow(s_logList,id,w);
    return true;
}

// Clear all archive
bool CALogic::clearLog(Window* wnd)
{
    if (wnd &&
	showConfirm(wnd,"Confirm archive clear?",s_archPrefix + s_actionClearNow))
	return true;
    refreshStop();
    Window* w = getWindow();
    if (w) {
	// This will stop the search thread
	Client::self()->setShow("archive_frame_search",false,w);
	Client::self()->clearTable(s_logList,w);
	Client::self()->clearTable(s_sessList,w);
	Client::self()->clearTable(s_sessHistory,w);
    }
    s_chatArchive.clear(false);
    return true;
}


/*
 * CASearchThread
 */
CASearchThread::CASearchThread()
    : Thread("CASearchThread"),
    m_startSearch(false),
    m_searching(false),
    m_next(true),
    m_range(CASearchRangeInvalid),
    m_currentSessionFull(false),
    m_currentContactFull(false)
{
}

CASearchThread::~CASearchThread()
{
    s_logic.searchTerminated();
}

void CASearchThread::startSearching(const String& text, bool next)
{
    CASearchRange old = m_range;
    resetSearch();
    Lock lock(s_mutex);
    m_next = next;
    m_range = s_range;
    // Reset data if range changed
    if (old != s_range || m_what != text) {
	m_currentContact.clear();
	m_currentSession.clear();
	m_currentSessionFull = false;
	m_currentContactFull = false;
    }
    m_what = text;
    m_startSearch = true;
}

void CASearchThread::run()
{
    Debug(ClientDriver::self(),DebugAll,"%s start running",currentName());
    while (true) {
	if (exiting())
	    break;
	Lock lock(s_mutex);
	if (!(m_what && m_startSearch)) {
	    lock.drop();
	    Thread::yield();
	    continue;
	}
	String what = m_what;
	m_startSearch = false;
	lock.drop();
	enableSearch(false);
	m_searching = true;
	switch (m_range) {
	    case CASearchRangeSession:
		s_logic.setSearchHistory(what,m_next);
		break;
	    case CASearchRangeContact:
		searchCurrentContact(what);
		break;
	    case CASearchRangeAll:
		searchAll(what);
		break;
	    default:
		Debug(DebugStub,"%s range %d not implemented",currentName(),m_range);
	}
	m_searching = false;
	enableSearch(true);
    }
    Debug(ClientDriver::self(),DebugAll,"%s stop running",currentName());
};

void CASearchThread::resetSearch()
{
    m_range = CASearchRangeInvalid;
    while (m_searching)
	Thread::yield();
}

// Search all archive
void CASearchThread::searchAll(const String& what)
{
    bool changed = false;
    ObjList items;
    Window* w = getWindow();
    if (w) {
	NamedList p("");
	Client::self()->getOptions(s_logList,&p,w);
	unsigned int n = p.length();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* ns = p.getParam(i);
	    if (ns)
 		items.append(new String(ns->name()));
	}
    }
    if (m_currentContact && !items.find(m_currentContact)) {
	changed = true;
	m_currentContact.clear();
	m_currentSession.clear();
	m_currentSessionFull = false;
	m_currentContactFull = false;
    }
    if (!m_currentContact) {
	changed = true;
	m_currentSession.clear();
	m_currentSessionFull = false;
	m_currentContactFull = false;
	ObjList* o = m_next ? items.skipNull() : getListLastItem(items);
	if (o)
	    m_currentContact = o->get()->toString();
	else
	    return;
    }
    bool found = false;
    String start = m_currentContact;
    while (!found) {
	ChatFile* f = 0;
	while (!f) {
	    if (m_currentContactFull) {
		m_currentContactFull = false;
		if (exiting() || m_range == CASearchRangeInvalid)
		    break;
		ObjList* o = 0;
		if (m_next) {
		    o = items.find(m_currentContact);
		    if (o)
			o = o->skipNext();
		}
		else
		    o = getListPrevItem(items,m_currentContact);
		if (!o) {
		    if (m_next)
			o = items.skipNull();
		    else
			o = getListLastItem(items);
		}
		if (!o || o->get()->toString() == start)
		    break;
		m_currentContact = o->get()->toString();
		m_currentSession.clear();
		changed = true;
	    }
	    f = s_chatArchive.getChatFile(m_currentContact);
	}
	if (!f)
	    break;
	// Retrieve the starting session if don't have one
	if (!m_currentSession) {
	    changed = true;
	    m_currentSessionFull = false;
	    ObjList* o = m_next ? f->sessions().skipNull() : getListLastItem(f->sessions());
	    if (o)
		m_currentSession = o->get()->toString();
	}
	if (m_currentSession)
	    found = searchContact(f,what,changed);
	TelEngine::destruct(f);
	if (found)
	    break;
	m_currentSession.clear();
	m_currentContactFull = true;
    }
    if (!found) {
	m_currentContact.clear();
	m_currentSession.clear();
	m_currentSessionFull = true;
	m_currentContactFull = true;
    }
}

// Search in the current contact
void CASearchThread::searchCurrentContact(const String& what)
{
    ChatFile* f = 0;
    bool changed = false;
    if (m_currentSession) {
	f = s_chatArchive.getChatFileBySession(m_currentSession);
	if (f) {
	    String tmp = m_currentSession;
	    Window* w = getWindow();
	    if (w)
		Client::self()->getSelect(s_sessList,tmp,w);
	    changed = (tmp != m_currentSession);
	}
	else
	    m_currentSession.clear();
    }
    if (!m_currentSession) {
	changed = true;
	m_currentSessionFull = false;
	Window* w = getWindow();
	if (w) {
	    Client::self()->getSelect(s_sessList,m_currentSession,w);
	    // Select the first or last session if any
	    if (!m_currentSession) {
		NamedList p("");
		Client::self()->getOptions(s_sessList,&p,w);
		unsigned int n = p.length();
		NamedString* ns = 0;
		for (unsigned int i = 0; i < n; i++) {
		    ns = p.getParam(i);
		    if (ns && m_next)
			break;
		}
		if (ns)
		    m_currentSession = ns->name();
	    }
	}
	f = s_chatArchive.getChatFileBySession(m_currentSession);
    }
    if (!f)
	return;
    searchContact(f,what,changed);
    TelEngine::destruct(f);
}

// Search in given contact contact
bool CASearchThread::searchContact(ChatFile* f, const String& what, bool changed)
{
    if (!f)
	return false;
    QString* search = new QString;
    *search = QtClient::setUtf8(what);
    f->lock();
    bool found = false;
    String start = m_currentSession;
    while (true) {
	if (m_currentSessionFull) {
	    if (exiting() || m_range == CASearchRangeInvalid)
		break;
	    ObjList* o = 0;
	    if (m_next) {
		o = f->sessions().find(m_currentSession);
		if (o)
		    o = o->skipNext();
	    }
	    else
		o = getListPrevItem(f->sessions(),m_currentSession);
	    if (!o && m_range == CASearchRangeContact) {
		if (m_next)
		    o = f->sessions().skipNull();
		else
		    o = getListLastItem(f->sessions());
	    }
	    if (!o || o->get()->toString() == start) {
		m_currentContactFull = true;
		break;
	    }
	    m_currentSession = o->get()->toString();
	    m_currentSessionFull = false;
	    changed = true;
	}
	if (exiting() || m_range == CASearchRangeInvalid)
	    break;
	ObjList list;
	found = f->loadSession(m_currentSession,list,0,search);
	if (exiting() || m_range == CASearchRangeInvalid) {
	    found = false;
	    break;
	}
	if (found) {
	    f->unlock();
	    found = s_logic.setSearch(changed,f->toString(),m_currentSession,what,m_next);
	    f->lock();
	    if (found) {
		m_currentSessionFull = s_highlightAll;
		break;
	    }
	}
	m_currentSessionFull = true;
    }
    f->unlock();
    if (!found) {
	m_currentSession.clear();
	m_currentSessionFull = false;
    }
    if (search)
	delete search;
    return found;
}


/*
 * CARefreshThread
 */
CARefreshThread::CARefreshThread()
    : Thread("CARefreshThread")
{
}

CARefreshThread::~CARefreshThread()
{
    s_logic.refreshTerminated();
}

void CARefreshThread::run()
{
    Debug(ClientDriver::self(),DebugAll,"%s start running",currentName());
    s_chatArchive.refresh();
    Debug(ClientDriver::self(),DebugAll,"%s stop running",currentName());
}

} // namespace anonymous

#include "clientarchive.moc"

/* vi: set ts=8 sw=4 sts=4 noet: */
