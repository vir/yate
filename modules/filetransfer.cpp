/**
 * filetransfer.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * File transfer Driver
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

#include <yatephone.h>

using namespace TelEngine;
namespace { // anonymous

class FileHolder;                        // A file info holder
class FileSource;                        // File data source
class FileConsumer;                      // File data consumer
class FileSourceWorker;                  // Worker for data source
class FileChan;                          // A file transfer channel
class FileDriver;                        // The driver

// NOTE: This module's semantic of data/channel direction is from the point
//       of view of the local machine:
//       outgoing/send means from storage to engine

// Minimum value allowed for send chunk buffer
#define SEND_CHUNK_MIN 4096
// Minimum/Default value for send interval
#define SEND_SLEEP_MIN 10
#define SEND_SLEEP_DEF 50

// A file info holder
class FileHolder
{
public:
    inline FileHolder(const String& name, const String& dropChan)
	: m_fileName(name), m_fileTime(0), m_fileSize(-1), m_transferred(0),
	m_params(""), m_dropChan(dropChan), m_waitOnDropMs(0)
	{}
    // Get file name
    inline const String& fileName() const
	{ return m_fileName; }
    // Retrieve MD5 digest
    inline const String& md5() const
	{ return m_md5HexDigest; }
    // Retrieve file info
    inline int64_t fileSize(bool update = false) {
	    if (update || m_fileSize < 0)
		m_fileSize = m_file.length();
	    return m_fileSize;
	}
    inline unsigned int fileTime(bool update = false) {
	    if (update || !m_fileTime) {
		m_fileTime = 0;
		m_file.getFileTime(m_fileTime);
	    }
	    return m_fileTime;
	}
    // Set drop chan id
    inline void setDropChan(const String& id)
	{ m_dropChan = id; }
    // Build drop message. Reset drop chan
    inline Message* dropMessage() {
	    if (!m_dropChan)
		return 0;
	    Message* m = new Message("call.drop");
	    m->addParam("id",m_dropChan);
	    m_dropChan = "";
	    return m;
	}
    // Add MD5 and/or file info parameters
    void addFileInfo(NamedList& params, bool md5, bool extra);
    // Add saved params to another list
    void addParams(NamedList& params);
protected:
    File m_file;                         // Source file
    String m_fileName;                   // File name and location
    unsigned int m_fileTime;             // File time
    int64_t m_fileSize;                  // File size
    int64_t m_transferred;               // Transferred bytes
    String m_md5HexDigest;               // MD5 digest of the file
    NamedList m_params;                  // Parameters to copy in notifications
    String m_dropChan;                   // Channel to drop on termination
    unsigned int m_waitOnDropMs;         // Time to wait to drop channel

};

// A file data source
class FileSource : public DataSource, public FileHolder
{
    friend class FileDriver;
    friend class FileSourceWorker;
public:
    // Create the data source, and init it
    FileSource(const String& file, NamedList* params = 0, const char* chan = 0,
	const char* format = 0);
    // Check if this data source is connected
    inline bool connected() {
	    Lock mylock(this);
	    return 0 != m_consumers.skipNull();
	}
    // Initialize and start worker
    // Return true on success
    bool init(bool buildMd5, String& error);
    // Wait for a consumer to be attached. Send the file
    void run();
private:
    // Release memory
    virtual void destroyed();

    String m_notify;                     // Target id to notify
    bool m_notifyProgress;               // Notify file transfer progress
    bool m_notifyPercent;                // Notify percent changes only
    int m_percent;                       // Notify current percent
    unsigned int m_buflen;               // Transfer buffer length
    unsigned int m_sleepMs;              // Sleep between data transfer
    unsigned int m_retryableReadErrors;  // How many retryable read erros occured
    DataBlock m_buffer;                  // Read buffer
    FileSourceWorker* m_worker;          // The worker thread
};

// A file data consumer
class FileConsumer : public DataConsumer, public FileHolder
{
    friend class FileDriver;
public:
    FileConsumer(const String& file, NamedList* params = 0, const char* chan = 0,
	const char* format = "data");
    // Check if file should be overwritten
    inline bool overWrite() const
	{ return m_overWrite; }
    // Check if this data consumer is connected
    inline bool connected() const
	{ return 0 != getConnSource(); }
    // Check file(s) existence
    inline bool fileExists(bool tmp = true, bool file = true) {
	    return (tmp && File::exists(m_tmpFileName)) ||
		(file && File::exists(m_fileName));
	}
    virtual unsigned long Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags);
protected:
    // Release memory
    virtual void destroyed();
    // Terminate: close file, notify, check MD5 (if used)
    void terminate(const char* error = 0);
    // Make sure a file path exists
    bool createPath(String* error);
private:
    String m_notify;                     // Target id to notify
    String m_tmpFileName;
    bool m_notifyProgress;               // Notify file transfer progress
    bool m_notifyPercent;                // Notify percent changes only
    int m_percent;                       // Notify current percent
    MD5 m_md5;                           // Calculate the MD5 if used
    u_int64_t m_startTime;
    bool m_terminated;
    bool m_delTemp;                      // Delete temporary file
    bool m_createPath;                   // Create file path
    bool m_overWrite;                    // Overwright existing file
};

// File source worker
class FileSourceWorker : public Thread
{
public:
    inline FileSourceWorker(FileSource* src, Thread::Priority prio = Thread::Normal)
	: Thread("FileSource Worker",prio), m_source(src)
	{}
    virtual void cleanup();
    virtual void run();
private:
    FileSource* m_source;
};

class FileChan : public Channel
{
public:
    // Build a file transfer channel
    FileChan(FileSource* src, FileConsumer* cons, bool autoclose);
    ~FileChan();
};

// The plugin
class FileDriver : public Driver
{
public:
    enum {
	ChanAttach = Private,
    };
    FileDriver();
    virtual ~FileDriver();
    virtual bool msgExecute(Message& msg, String& dest);
    virtual void initialize();
    virtual bool received(Message& msg, int id);
    // Process chan.attach messages
    bool chanAttach(Message& msg);
    // Uninstall the relays
    bool unload();
    // Notify file transfer status
    static bool notifyStatus(bool send, const String& id, const char* status,
        const char* filename, int64_t transferred, int64_t total,
	const char* error = 0, const NamedList* params = 0,
	const char* chan = 0);
    // Copy params
    inline void copyParams(NamedList& dest, const NamedList& src, bool exec = false) {
	    Lock lock(this);
	    const String& list = !exec ? m_copyParams : m_copyExecParams;
	    if (list)
		dest.copyParams(src,list);
	}
    // Attach default path to a file if file path is missing
    void getPath(String& file);
    // Add/remove sources and consumers from list
    // The driver doesn't own the objects: the lists are used only
    //  to show them in status output
    inline void addSource(FileSource* src) {
	    if (!src)
		return;
	    Lock lock(this);
	    m_sources.append(src)->setDelete(false);
	}
    inline void removeSource(FileSource* src, bool delObj = false) {
	    if (!src)
		return;
	    Lock lock(this);
	    m_sources.remove(src,delObj);
	}
    inline void addConsumer(FileConsumer* cons) {
	    if (!cons)
		return;
	    Lock lock(this);
	    m_consumers.append(cons)->setDelete(false);
	}
    inline void removeConsumer(FileConsumer* cons, bool delObj = false) {
	    if (!cons)
		return;
	    Lock lock(this);
	    m_consumers.remove(cons,delObj);
	}
protected:
    // Execute commands
    virtual bool commandExecute(String& retVal, const String& line);
    // Handle command complete requests
    virtual bool commandComplete(Message& msg, const String& partLine,
	const String& partWord);
private:
    bool m_init;
    String m_copyParams;                 // Parameters to send in notifications
    String m_copyExecParams;             // Parameters to copy when a call.execute without
                                         // call endpoint is handled
    ObjList m_sources;
    ObjList m_consumers;
};


/*
 * Local data and functions
 */
INIT_PLUGIN(FileDriver);
static unsigned int s_sendChunk = 4096;      // Buffer size used when sending data
static unsigned int s_sendIntervalMs = SEND_SLEEP_DEF;   // Interval to send packets
static unsigned int s_srcLingerIntervals = 10;           // How many intervals to wait before terminating
                                                         // an autoclose source
static int s_retryableReadErrors = 1000;     // How many retryable read errors are
                                             //  allowed when sending a file (-1 to retry forever)
static bool s_notifyProgress = true;         // Notify file transfer progress
static bool s_notifyPercent = true;          // Notify transfer percent
static bool s_srcFileInfo = true;            // Set file info params in call.execute
static bool s_srcFileMd5 = true;             // Set file MD5 call.execute
static String s_path;                        // Default path to save files
static String s_dirSend = "send";            // Methods allowed in chan.attach and call.execute
static String s_dirRecv = "receive";


static String s_statusCmd = "status filetransfer";
// Status commands handled by this module
static String s_statusCmds[] = {
    "send",                              // Show data sources
    "receive",                           // Show data consumers
    "all",                               // Show all
    ""
};
// Commands handled by this module
static const char* s_cmds[] = {
    "send",                              // Send a file
    "receive",                           // Receive a file
    0
};
// Command line
static const char* s_cmdsLine = "  filetransfer {{send|receive} filename [callto:]target [[paramname=value]...]}";


UNLOAD_PLUGIN(unloadNow)
{
    if (unloadNow && !__plugin.unload())
	return false;
    return true;
}

// Get an integer value from a parameter
// Check it to be in requested interval
static unsigned int getIntValue(const NamedList& params, const char* param, unsigned int defVal,
    unsigned int minVal, bool allowZero)
{
    int tmp = params.getIntValue(param,defVal);
    if (!tmp && allowZero)
	return 0;
    return (tmp >= (int)minVal) ? tmp : minVal;
}

// Get the string indicating data direction
inline String& dirStr(bool outgoing)
{
    return outgoing ? s_dirSend : s_dirRecv;
}

// Make sure a path contains only current system path separators
static void toNativeSeparators(String& path)
{
    char repl = (*Engine::pathSeparator() == '/') ? '\\' : '/';
    char* s = (char*)path.c_str();
    for (unsigned int i = 0; i < path.length(); i++, s++)
	if (*s == repl)
	    *s = *Engine::pathSeparator();
}


/*
 * FileHolder
 */
void FileHolder::addFileInfo(NamedList& params, bool md5, bool extra)
{
    params.setParam("file_name",m_fileName);
    if (md5)
	params.setParam("file_md5",m_md5HexDigest);
    if (!extra)
	return;
    if (m_fileSize >= 0)
	params.setParam("file_size",String((unsigned int)m_fileSize));
    else
	params.clearParam("file_size");
    fileTime();
    params.setParam("file_time",String(m_fileTime));
}


/*
 * FileSource
 */
FileSource::FileSource(const String& file, NamedList* params, const char* chan,
    const char* format)
    : DataSource(!null(format) ? format : "data"),
    FileHolder(file,chan),
    m_notifyProgress(s_notifyProgress),
    m_notifyPercent(s_notifyPercent),
    m_percent(0),
    m_buflen(s_sendChunk), m_sleepMs(s_sendIntervalMs),
    m_retryableReadErrors(0), m_worker(0)
{
    if (params) {
	m_notify = params->getValue("notify");
	m_notifyProgress = params->getBoolValue("notify_progress",m_notifyProgress);
	m_buflen = getIntValue(*params,"send_chunk_size",s_sendChunk,SEND_CHUNK_MIN,true);
	m_sleepMs = getIntValue(*params,"send_interval",s_sendIntervalMs,SEND_SLEEP_MIN,false);
	m_waitOnDropMs = params->getIntValue("wait_on_drop",0,0);
	__plugin.copyParams(m_params,*params);
    }
    if (!m_sleepMs)
	m_sleepMs = SEND_SLEEP_DEF;
    Debug(&__plugin,DebugAll,"FileSource('%s') [%p]",file.c_str(),this);
}

// Initialize and start worker
bool FileSource::init(bool buildMd5, String& error)
{
    XDebug(&__plugin,DebugAll,"FileSource('%s') init [%p]",m_fileName.c_str(),this);
    if (!m_file.openPath(m_fileName,false,true,false,false,true)) {
	Thread::errorString(error,m_file.error());
	return false;
    }
    if (fileSize() < 0) {
	Thread::errorString(error,m_file.error());
	m_fileSize = 0;
	return false;
    }
    if (!m_buflen)
	m_buflen = (unsigned int)m_fileSize;
    m_buffer.assign(0,m_buflen);
    if (buildMd5 && !m_file.md5(m_md5HexDigest)) {
	Thread::errorString(error,m_file.error());
        return false;
    }
    m_worker = new FileSourceWorker(this);
    if (m_worker->startup())
	return true;
    error = "Failed to start thread";
    m_worker = 0;
    return false;
}

// Wait for a consumer to be attached. Send the file
void FileSource::run()
{
    DDebug(&__plugin,DebugAll,"FileSource(%s) start running [%p]",
	m_fileName.c_str(),this);
    m_transferred = 0;
    FileDriver::notifyStatus(true,m_notify,"pending",m_fileName,0,m_fileSize,0,&m_params);

    String error;
    u_int64_t start = 0;
    // Use a while() to break to the end to cleanup properly
    while (true) {
	// Wait until at least one consumer is attached
	while (true) {
	    if (Thread::check(false)) {
		error = "cancelled";
		break;
	    }
	    if (!lock(100000))
		continue;
	    bool cons = (0 != m_consumers.skipNull());
	    unlock();
	    Thread::idle();
	    if (cons)
		break;
	}
	if (error)
	    break;

	DDebug(&__plugin,DebugAll,
	    "FileSource(%s) starting size=" FMT64 " buflen=%u interval=%u [%p]",
	    m_fileName.c_str(),m_fileSize,m_buflen,m_sleepMs,this);

	FileDriver::notifyStatus(true,m_notify,"start",m_fileName,0,m_fileSize,0,
	    &m_params,m_dropChan);

	unsigned long tStamp = 0;
	start = Time::msecNow();
	if (!m_fileSize)
	    break;
	// Set file pos at start
	if (-1 == m_file.Stream::seek(0)) {
	    Thread::errorString(error,m_file.error());
	    break;
	}
	unsigned char* buf = 0;
	unsigned int len = 0;
	while (true) {
	    if (Thread::check(false)) {
		error = "cancelled";
		break;
	    }
	    if (!buf) {
		int rd = m_file.readData(m_buffer.data(),m_buffer.length());
		if (rd <= 0) {
		    if (m_file.canRetry()) {
			m_retryableReadErrors++;
			if (m_retryableReadErrors != (unsigned int)s_retryableReadErrors)
			    continue;
		    }
		    Thread::errorString(error,m_file.error());
		    break;
		}
		buf = (unsigned char*)m_buffer.data();
		len = rd;
	    }
	    DataBlock tmp(buf,len,false);
	    XDebug(&__plugin,DebugAll,"FileSource(%s) forwarding %u bytes [%p]",
		m_fileName.c_str(),len,this);
	    unsigned int sent = Forward(tmp,tStamp);
	    tmp.clear(false);
	    if (sent && sent != invalidStamp()) {
		m_transferred += sent;
		if (m_notifyProgress) {
		    bool notif = true;
		    if (m_notifyPercent) {
			int tmp = (int)((int64_t)m_transferred * 100 / m_fileSize);
			notif = (m_percent != tmp);
			if (notif)
			    m_percent = tmp;
		    }
		    if (notif)
			FileDriver::notifyStatus(true,m_notify,"progressing",m_fileName,
			    m_transferred,m_fileSize);
		}
		if (sent == len) {
		    buf = 0;
		    len = 0;
		}
		else {
		    buf += sent;
		    len -= sent;
		}
		if (m_transferred >= m_fileSize)
		    break;
	    }
	    tStamp += m_sleepMs;
	    Thread::msleep(m_sleepMs,false);
	}
	break;
    }

    YIGNORE(start);
    if (error.null())
	DDebug(&__plugin,DebugAll,
	    "FileSource(%s) terminated. Transferred " FMT64 " bytes in " FMT64 "ms [%p]",
	    m_fileName.c_str(),m_fileSize,Time::msecNow() - start,this);
    else {
	int dbg = DebugMild;
	if (error == "cancelled")
	    dbg = DebugInfo;
	Debug(&__plugin,dbg,"FileSource(%s) terminated error='%s' [%p]",
	    m_fileName.c_str(),error.c_str(),this);
    }

    m_file.terminate();
    FileDriver::notifyStatus(true,m_notify,"terminated",m_fileName,
	m_transferred,m_fileSize,error,&m_params);

    Message* m = dropMessage();
    if (m) {
	// Wait for a while to give some time to the remote party to receive the data
	unsigned int n = 0;
	if (!error) {
	    if (m_waitOnDropMs) {
		n = m_waitOnDropMs / m_sleepMs;
		if (!n)
		    n = 1;
	    }
	    else
		n = s_srcLingerIntervals;
	}
	XDebug(&__plugin,DebugAll,
	    "FileSource(%s) dropping chan '%s' waiting %u intervals of %ums [%p]",
	    m_fileName.c_str(),m->getValue("id"),n,m_sleepMs,this);
	for (; n && !Thread::check(false); n--)
	    Thread::msleep(m_sleepMs,false);
	// Drop channel
	if (error) {
	    if (error == "cancelled")
		m->addParam("reason","cancelled");
	    else {
		m->addParam("reason","failure");
		m->addParam("error",error);
	    }
	}
	Engine::enqueue(m);
    }
}

// Release memory
void FileSource::destroyed()
{
    lock();
    Thread* th = m_worker;
    if (m_worker) {
	Debug(&__plugin,DebugInfo,"FileSource terminating worker [%p]",this);
	m_worker->cancel(false);
    }
    unlock();
    while (m_worker)
	Thread::yield(false);
    if (th)
	Debug(&__plugin,DebugInfo,"FileSource worker terminated [%p]",this);
    FileDriver::notifyStatus(true,m_notify,"destroyed",m_fileName,
	m_transferred,m_fileSize,0,&m_params);
    __plugin.removeSource(this);
    Debug(&__plugin,DebugAll,
	"FileSource('%s') destroyed transferred " FMT64 "/" FMT64 " [%p]",
	m_fileName.c_str(),m_transferred,m_fileSize,this);
    DataSource::destroyed();
}


/*
 * FileConsumer
 */
FileConsumer::FileConsumer(const String& file, NamedList* params, const char* chan,
    const char* format)
    : DataConsumer(!null(format) ? format : "data"),
    FileHolder(file,chan),
    m_notifyProgress(s_notifyProgress),
    m_notifyPercent(s_notifyPercent),
    m_percent(0),
    m_startTime(0), m_terminated(false), m_delTemp(true),
    m_createPath(false), m_overWrite(false)
{
    toNativeSeparators(m_fileName);
    __plugin.getPath(m_fileName);
    if (params) {
	m_notify = params->getValue("notify");
	m_notifyProgress = params->getBoolValue("notify_progress",m_notifyProgress);
	m_fileSize = params->getIntValue("file_size",0);
	m_md5HexDigest = params->getValue("file_md5");
	m_fileTime = params->getIntValue("file_time");
	m_createPath = params->getBoolValue(YSTRING("create_path"));
	m_overWrite = params->getBoolValue(YSTRING("overwrite"));
	__plugin.copyParams(m_params,*params);
    }
    Debug(&__plugin,DebugAll,"FileConsumer('%s') [%p]",m_fileName.c_str(),this);
    if (m_fileName && m_fileName[m_fileName.length() - 1] != *Engine::pathSeparator()) {
	m_tmpFileName << m_fileName << ".tmp";
	m_delTemp = !File::exists(m_tmpFileName);
    }
    else
	m_delTemp = false;
}

unsigned long FileConsumer::Consume(const DataBlock& data, unsigned long tStamp, unsigned long flags)
{
    if (m_terminated)
	return 0;

    if (!m_startTime) {
	m_startTime = Time::now();
	FileDriver::notifyStatus(false,m_notify,"start",m_fileName,0,m_fileSize,0,
	    &m_params,m_dropChan);
	// Check file existence
	if (fileExists(true,false)) {
	    if (!m_overWrite) {
		terminate("File exists");
		Debug(&__plugin,DebugNote,
		    "FileConsumer(%s) failed to start: temporary file already exists! [%p]",
		    m_fileName.c_str(),this);
		return 0;
	    }
	    int code = 0;
	    if (!File::remove(m_tmpFileName,&code)) {
		String error;
		Thread::errorString(error,code);
		terminate(error);
		Debug(&__plugin,DebugNote,
		    "FileConsumer(%s) failed to delete temporary file. %d: '%s' [%p]",
		    m_fileName.c_str(),code,error.c_str(),this);
		return 0;
	    }
	}
	else if (m_createPath) {
	    String error;
	    if (!createPath(&error)) {
		terminate(error);
		return 0;
	    }
	}
	m_delTemp = true;
	if (!m_file.openPath(m_tmpFileName,true,false,true,true,true)) {
	    String error;
	    Thread::errorString(error,m_file.error());
	    terminate(error);
	    Debug(&__plugin,DebugNote,
		"FileConsumer(%s) failed to create temporary file. %d: '%s' [%p]",
		m_fileName.c_str(),m_file.error(),error.c_str(),this);
	    return 0;
	}
    }

    XDebug(&__plugin,DebugAll,"FileConsumer(%s) consuming %u bytes [%p]",
	m_fileName.c_str(),data.length(),this);

    if (data.length() && m_file.valid()) {
	if (m_file.writeData(data.data(),data.length())) {
	    if (m_md5HexDigest)
		m_md5 << data;
	    if (m_notifyProgress) {
		bool notif = true;
		if (m_notifyPercent) {
		    int tmp = (int)((int64_t)m_transferred * 100 / m_fileSize);
		    notif = (m_percent != tmp);
		    if (notif)
			m_percent = tmp;
		}
		if (notif)
		    FileDriver::notifyStatus(false,m_notify,"progressing",m_fileName,
			m_transferred,m_fileSize);
	    }
	}
	else {
	    String error;
	    Thread::errorString(error,m_file.error());
	    terminate(error);
	}
    }

    m_transferred += data.length();
    if (m_transferred >= m_fileSize)
	terminate();
    return data.length();
}

// Release memory
void FileConsumer::destroyed()
{
    terminate("cancelled");
    FileDriver::notifyStatus(false,m_notify,"destroyed",m_fileName,
	m_transferred,m_fileSize,0,&m_params);
    __plugin.removeConsumer(this);
    Debug(&__plugin,DebugAll,
	"FileConsumer('%s') destroyed transferred " FMT64 "/" FMT64 " [%p]",
	m_fileName.c_str(),m_transferred,m_fileSize,this);
    DataConsumer::destroyed();
}

// Terminate: close file, notify, check MD5 (if used)
void FileConsumer::terminate(const char* error)
{
    m_file.terminate();
    if (m_terminated)
	return;
    m_terminated = true;
    String err = error;
    while (!err) {
	// Check MD5
	if (m_md5HexDigest && m_md5HexDigest != m_md5.hexDigest()) {
	    err = "Invalid checksum";
	    break;
	}
	// Check file existence
	if (!m_overWrite && fileExists(false,true)) {
	    err = "File exists";
	    break;
	}
	// Rename file and set its modification time
	// Don't set error if failed to set file time
	int code = 0;
	if (File::rename(m_tmpFileName,m_fileName,&code)) {
	    if (m_fileTime)
		File::setFileTime(m_fileName,m_fileTime);
	}
	else {
	    File::remove(m_fileName);
	    // Avoid error=No error
	    if (code)
		Thread::errorString(err,code);
	    else
		err = "Unknown error";
	}
	break;
    }
    if (m_delTemp)
	File::remove(m_tmpFileName);
    // Notify and terminate drop the channel
    FileDriver::notifyStatus(false,m_notify,"terminated",m_fileName,
	m_transferred,m_fileSize,err,&m_params);
    Message* m = dropMessage();
    if (m) {
	if (err) {
	    m->addParam("reason","failure");
	    m->addParam("error",err);
	}
	Engine::enqueue(m);
    }
}

// Make sure a file path exists
bool FileConsumer::createPath(String* error)
{
    const String& orig = m_tmpFileName;
    if (!orig)
	return true;
    char sep = *Engine::pathSeparator();
    int pos = orig.rfind(sep);
    if (pos <= 0)
	return true;
    String path = orig.substr(0,pos);
    ObjList list;
    bool exists = false;
    while (path) {
	exists = File::exists(path);
	if (exists)
	    break;
	int pos = path.rfind(sep);
	if (pos < 0)
	    break;
	String* s = new String(path.substr(pos + 1));
	if (!TelEngine::null(s))
	    list.insert(s);
	else
	    TelEngine::destruct(s);
	path = path.substr(0,pos);
    }
    int code = 0;
    bool ok = true;
    if (path && !exists)
	ok = File::mkDir(path,&code);
    while (ok) {
	ObjList* o = list.skipNull();
	if (!o)
	    break;
	path.append(*static_cast<String*>(o->get()),Engine::pathSeparator());
	o->remove();
	ok = File::mkDir(path,&code);
    }
    if (ok)
	return true;
    String tmp;
    if (!error)
	error = &tmp;
    Thread::errorString(*error,code);
    Debug(&__plugin,DebugNote,
	"FileConsumer(%s) failed to create path for '%s'. %d: '%s' [%p]",
	m_fileName.c_str(),orig.c_str(),code,error->c_str(),this);
    return false;
}


/*
 * FileSourceWorker
 */
void FileSourceWorker::cleanup()
{
    if (!m_source)
	return;
    Debug(&__plugin,DebugWarn,"FileSource worker destroyed while holding source (%p)",m_source);
    m_source->m_worker = 0;
    m_source = 0;
}

void FileSourceWorker::run()
{
    if (!m_source)
	return;
    m_source->run();
    m_source->m_worker = 0;
    m_source = 0;
}


/*
 * FileChan
 */
FileChan::FileChan(FileSource* src, FileConsumer* cons, bool autoclose)
    : Channel(__plugin,0,src != 0)
{
    if (src)
	m_address = src->fileName();
    else if (cons)
	m_address = cons->fileName();
    Debug(this,DebugAll,"FileChan(%s,%s) [%p]",
	dirStr(isOutgoing()).c_str(),m_address.c_str(),this);
    if (src)
	setSource(src,src->getFormat());
    else
	setConsumer(cons,cons->getFormat());
    if (autoclose) {
	if (src)
	    src->setDropChan(id());
	else if (cons)
	    cons->setDropChan(id());
    }
    TelEngine::destruct(src);
    TelEngine::destruct(cons);
}

FileChan::~FileChan()
{
    Debug(this,DebugAll,"FileChan(%s,%s) destroyed [%p]",
	dirStr(isOutgoing()).c_str(),m_address.c_str(),this);
}


/*
 * FileDriver
 */
FileDriver::FileDriver()
    : Driver("filetransfer","misc"), m_init(false)
{
    Output("Loaded module File Transfer");
    Engine::pluginMode(Engine::LoadEarly);
}

FileDriver::~FileDriver()
{
    Output("Unloading module File Transfer");
}

// Execute/accept file transfer requests
bool FileDriver::msgExecute(Message& msg, String& dest)
{
    static const Regexp r("^\\([^/]*\\)/\\(.*\\)$");
    if (!dest.matches(r))
	return false;

    bool outgoing = (dest.matchString(1) == s_dirSend);
    if (!outgoing && dest.matchString(1) != s_dirRecv) {
	Debug(this,DebugWarn,"Invalid file transfer method '%s', use '%s' or '%s'",
	    dest.matchString(1).c_str(),s_dirSend.c_str(),s_dirRecv.c_str());
	return false;
    }

    const char* format = msg.getValue("format","data");

    // Call execute request from a call endpoint
    CallEndpoint* ch = YOBJECT(CallEndpoint,msg.userData());
    if (ch) {
	Debug(this,DebugInfo,"%s file '%s'",(outgoing ? "Sending" : "Receiving"),
	    dest.matchString(2).c_str());
	// Build source/consumer
	FileSource* src = 0;
	FileConsumer* cons = 0;
	String error;
	bool ok = true;
	if (outgoing) {
	    src = new FileSource(dest.matchString(2),&msg,0,format);
	    bool md5 = msg.getBoolValue("getfilemd5");
	    ok = src->init(md5,error);
	    if (ok) {
		addSource(src);
		src->addFileInfo(msg,md5,msg.getBoolValue("getfileinfo"));
	    }
	}
	else {
	    cons = new FileConsumer(dest.matchString(2),&msg,0,format);
	    ok = cons->overWrite() || !cons->fileExists();
	    if (ok)
		addConsumer(cons);
	    else
		error = "File exists";
	}
	if (!ok) {
	    Debug(this,DebugWarn,"File %s ('%s') failed error='%s'!",dirStr(outgoing).c_str(),
		src ? src->fileName().c_str() : cons->fileName().c_str(),error.c_str());
	    TelEngine::destruct(src);
	    TelEngine::destruct(cons);
	    msg.setParam("error",error);
	    return false;
	}

	// Build channel
	FileChan* c = new FileChan(src,cons,msg.getBoolValue("autoclose"));
	c->initChan();
	ok = ch->connect(c,msg.getValue("reason"));
	if (ok) {
	    c->callConnect(msg);
	    msg.setParam("peerid",c->id());
	}
	TelEngine::destruct(c);
	return ok;
    }

    // Init call from here
    Message m("call.route");
    m.addParam("module",name());
    copyParams(m,msg,true);
    const String& cp = msg[YSTRING("copyparams")];
    if (cp)
	m.copyParams(msg,cp);
    String callto(msg.getValue("direct"));
    if (callto.null()) {
	const char* targ = msg.getValue("target");
	if (!targ) {
	    Debug(this,DebugWarn,"No target to %s file!",dirStr(outgoing).c_str());
	    return false;
	}
	callto = msg.getValue("caller");
	if (callto.null())
	    callto << prefix() << dest;
	m.addParam("called",targ);
	m.addParam("caller",callto);
	if (!Engine::dispatch(m)) {
	    Debug(this,DebugWarn,"No route to %s file!",dirStr(outgoing).c_str());
	    return false;
	}
	callto = m.retValue();
	m.retValue().clear();
    }

    m = "call.execute";
    m.addParam("callto",callto);
    // Build source/consumer
    FileSource* src = 0;
    FileConsumer* cons = 0;
    FileHolder* fileHolder = 0;
    bool copyMD5 = msg.getBoolValue("getfilemd5",s_srcFileMd5);
    String error;
    if (outgoing) {
	src = new FileSource(dest.matchString(2),&msg,0,format);
	if (src->init(copyMD5,error)) {
	    addSource(src);
	    fileHolder = static_cast<FileHolder*>(src);
	}
    }
    else {
	cons = new FileConsumer(dest.matchString(2),&msg,0,format);
	if (cons->overWrite() || !cons->fileExists()) {
	    addConsumer(cons);
	    fileHolder = static_cast<FileHolder*>(cons);
	}
	else
	    error = "File exists";
    }
    if (!fileHolder) {
	Debug(this,DebugWarn,"File %s ('%s') failed error='%s'!",dirStr(outgoing).c_str(),
	    src ? src->fileName().c_str() : cons->fileName().c_str(),error.c_str());
	TelEngine::destruct(src);
	TelEngine::destruct(cons);
	msg.setParam("error",error);
	return false;
    }

    // Build message and dispatch it
    FileChan* c = new FileChan(src,cons,msg.getBoolValue("autoclose"));
    c->initChan();
    m.setParam("id",c->id());
    m.userData(c);
    m.addParam("format",format);
    m.addParam("operation",dirStr(outgoing));
    fileHolder->addFileInfo(m,copyMD5,msg.getBoolValue("getfileinfo",s_srcFileInfo));
    const String& remoteFile = msg[YSTRING("remote_file")];
    if (remoteFile)
	m.setParam(YSTRING("file_name"),remoteFile);
    m.addParam("cdrtrack","false");
    bool ok = Engine::dispatch(m);
    if (ok)
	msg.setParam("id",c->id());
    else {
	msg.copyParams(m,"error");
	Debug(this,DebugWarn,"File %s not accepted!",dirStr(outgoing).c_str());
    }
    TelEngine::destruct(c);
    return ok;
}

// Process chan.attach messages
bool FileDriver::chanAttach(Message& msg)
{
    // Expect file/[send|receive]/filename
    static const Regexp r("^filetransfer/\\([^/]*\\)/\\(.*\\)$");

    String file(msg.getValue("source"));
    // Direction
    bool src = !file.null();
    if (!file)
	file = msg.getValue("consumer");
    if (!file)
	return false;
    if (file.matches(r)) {
	if (file.matchString(1) == dirStr(src))
	    file = file.matchString(2);
	else {
	    Debug(this,DebugWarn,"Could not attach %s with method '%s', use '%s'",
		src ? "source" : "consumer",file.matchString(1).c_str(),
		dirStr(src).c_str());
	    return false;
	}
    }
    else
	return false;

    if (!file) {
	DDebug(this,DebugNote,"File %s attach request with no file!",
	    src ? "source" : "consumer");
	return false;
    }

    CallEndpoint* ch = static_cast<CallEndpoint*>(msg.userData());
    if (!ch) {
	Debug(this,DebugWarn,"File %s '%s' attach request with no data channel!",
	    src ? "source" : "consumer",file.c_str());
	return false;
    }

    const char* dropChan = 0;
    if (msg.getBoolValue("autoclose",true))
	dropChan = ch->id();

    bool ok = false;
    const char* format = msg.getValue("format");
    if (src) {
	FileSource* s = new FileSource(file,&msg,dropChan,format);
	String error;
	bool md5 = msg.getBoolValue("getfilemd5");
	ok = s->init(md5 != 0,error);
	if (ok) {
	    addSource(s);
	    s->addFileInfo(msg,md5,msg.getBoolValue("getfileinfo"));
	    ch->setSource(s,s->getFormat());
	}
	else
	    msg.setParam("error",error);
	TelEngine::destruct(s);
	msg.clearParam("source");
    }
    else {
	FileConsumer* c = new FileConsumer(file,&msg,dropChan,format);
	ch->setConsumer(c,c->getFormat());
	addConsumer(c);
	TelEngine::destruct(c);
	msg.clearParam("consumer");
    }

    return ok;
}

void FileDriver::initialize()
{
    Output("Initializing module File Transfer");
    Configuration cfg(Engine::configFile("filetransfer"));

    if (!m_init) {
	setup();
	installRelay(Halt);
	installRelay(Execute);
	installRelay(Help);
	installRelay(ChanAttach,"chan.attach",100);
    }

    NamedList dummy("");
    NamedList* general = cfg.getSection("general");
    if (!general)
	general = &dummy;

    lock();
    m_copyExecParams = "line,account,caller,username,password,subject";
    m_copyParams = general->getValue("parameters");
    s_sendChunk = getIntValue(*general,"send_chunk_size",4096,SEND_CHUNK_MIN,true);
    s_sendIntervalMs = getIntValue(*general,"send_interval",
	SEND_SLEEP_DEF,SEND_SLEEP_MIN,false);
    s_srcLingerIntervals = getIntValue(*general,"send_linger_intervals",20,1,false);
    s_notifyProgress = general->getBoolValue("notify_progress",Engine::clientMode());
    s_srcFileInfo = general->getBoolValue("source_file_info",true);
    s_srcFileMd5 = general->getBoolValue("source_file_md5",true);
    s_path = general->getValue("path",".");
    if (s_path && !s_path.endsWith(Engine::pathSeparator()))
	s_path << Engine::pathSeparator();
    unlock();

    if (debugAt(DebugInfo)) {
	String s;
	s << "send_chunk_size=" << s_sendChunk;
	s << " send_interval=" << s_sendIntervalMs << "ms";
	s << " send_linger_intervals=" << s_srcLingerIntervals;
	s << " notify_progress=" << String::boolText(s_notifyProgress);
	Debug(this,DebugInfo,"Initialized %s",s.c_str());
    }

    m_init = true;
}

// Common message relay handler
bool FileDriver::received(Message& msg, int id)
{
    if (id == ChanAttach)
	return chanAttach(msg);
    if (id == Help) {
	String line = msg.getValue("line");
	if (line.null()) {
	    msg.retValue() << s_cmdsLine << "\r\n";
	    return false;
	}
	if (line != name())
	    return false;
	msg.retValue() << s_cmdsLine << "\r\n";
	msg.retValue() << "Commands used to control the File Transfer module\r\n";
	return true;
    }
    if (id == Status) {
	String target = msg.getValue("module");
	// Target is the driver or channel
	if (!target || target == name() || target.startsWith(prefix()))
	    return Driver::received(msg,id);
	// Check additional commands
	if (!target.startSkip(name(),false))
	    return false;
	target.trimBlanks();
	bool all = (target == "all");
	bool src = all || (target == "send");
	bool cons = all || (target == "receive");
	if (!(src || cons))
	    return false;
	Lock lock(this);
	msg.retValue() << "name=" << name() << ",type=" << type();
	unsigned int count = 0;
	if (src)
	    count += m_sources.count();
	if (cons)
	    count += m_consumers.count();
	msg.retValue() << ";count=" << count;
	msg.retValue() << ";format=Direction|Total|Transferred|Connected";
	if (src)
	    for (ObjList* os = m_sources.skipNull(); os; os = os->skipNext()) {
		FileSource* s = static_cast<FileSource*>(os->get());
		msg.retValue() << ";" << s->m_fileName << "=" << dirStr(true) <<
		    "|" << (unsigned int)s->m_fileSize <<
		    "|" << (unsigned int)s->m_transferred <<
		    "|" << String::boolText(s->connected());
	    }
	if (cons)
	    for (ObjList* oc = m_consumers.skipNull(); oc; oc = oc->skipNext()) {
		FileConsumer* c = static_cast<FileConsumer*>(oc->get());
		msg.retValue() << ";" << c->m_fileName << "=" << dirStr(false) <<
		    "|" << (unsigned int)c->m_fileSize <<
		    "|" << (unsigned int)c->m_transferred <<
		    "|" << String::boolText(c->connected());
	    }
	msg.retValue() << "\r\n";
	return true;
    }
    if (id == Halt)
	unload();
    return Driver::received(msg,id);
}

// Unload the Driver: uninstall the relays
bool FileDriver::unload()
{
    DDebug(this,DebugAll,"Unloading...");
    if (!lock(500000))
	return false;
    uninstallRelays();
    unlock();
    return true;
}

// Notify file transfer status
bool FileDriver::notifyStatus(bool send, const String& id, const char* status,
    const char* filename, int64_t transferred, int64_t total, const char* error,
    const NamedList* params, const char* chan)
{
    Message* m = new Message("transfer.notify");
    m->addParam("targetid",id);
    m->addParam("send",String::boolText(send));
    m->addParam("status",status);
    if (!null(filename))
	m->addParam("file",filename);
    if (transferred >= 0)
	m->addParam("transferred",String((unsigned int)transferred));
    if (total >= 0)
	m->addParam("total",String((unsigned int)total));
    if (error)
	m->addParam("error",error);
    if (chan)
	m->addParam("channelid",chan);
    // Add params
    if (params) {
	unsigned int n = params->length();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* ns = params->getParam(i);
	    if (ns)
		m->addParam(ns->name(),*ns);
	}
    }
    return Engine::enqueue(m);
}

// Attach default path to a file if file path is missing
void FileDriver::getPath(String& file)
{
    // Check if the file already have a path separator
    if (-1 != file.find(*Engine::pathSeparator()))
	return;
    Lock lock(this);
    if (s_path)
	file = s_path + file;
}

// Execute commands
bool FileDriver::commandExecute(String& retVal, const String& line)
{
    String l = line;
    l.startSkip(name());
    l.trimSpaces();
    bool outgoing = l.startSkip("send");
    if (outgoing || l.startSkip("receive")) {
	l.trimSpaces();
	String filename, target;
	int posFile = l.find(' ');
	int posTarget = -1;
	bool direct = false;
	if (posFile > 0) {
	    filename = l.substr(0,posFile);
	    posTarget = l.find(' ',posFile + 1);
	    target = l.substr(posFile + 1,posTarget - posFile - 1);
	    direct = target.startSkip("callto:",false);
	}
	if (!(filename && target)) {
	    retVal << "Invalid parameters\r\n";
	    return true;
	}

	Message m("call.execute");
	m.addParam(direct ? "direct" : "target",target);
	// Set parameters
	if (posTarget > 0) {
	    l.trimSpaces();
	    ObjList* list = l.substr(posTarget + 1).split(' ',false);
	    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
		int pos = o->get()->toString().find('=');
		if (pos > 0) {
		    String pname = o->get()->toString().substr(0,pos);
		    String pval = o->get()->toString().substr(pos + 1);
		    Debug(this,DebugAll,"commandExecute() adding param %s=%s",
			pname.c_str(),pval.c_str());
		    m.addParam(pname,pval);
		}
	    }
	    TelEngine::destruct(list);
	}

	String dest;
	dest << dirStr(outgoing) << "/" << filename;
	if (msgExecute(m,dest))
	    retVal << (outgoing ? "Sending" : "Receiving");
	else
	    retVal << "Failed to " << dirStr(outgoing);
	retVal << " '" << filename << "' " <<
	    (outgoing ? "to " : "from ") << target;
	retVal << "\r\n";
    }
    else
	return false;
    return true;
}

// Handle command complete requests
bool FileDriver::commandComplete(Message& msg, const String& partLine,
    const String& partWord)
{
    if (partLine.null() && partWord.null())
	return false;

    if (partLine.null() || (partLine == "help"))
	Module::itemComplete(msg.retValue(),name(),partWord);
    else if (partLine == name()) {
	for (const char** list = s_cmds; *list; list++)
	    Module::itemComplete(msg.retValue(),*list,partWord);
	return true;
    }

    bool status = partLine.startsWith("status");
    bool drop = !status && partLine.startsWith("drop");
    if (!(status || drop))
	return Driver::commandComplete(msg,partLine,partWord);

    // 'status' command
    Lock lock(this);
    // line='status filetransfer': add additional commands
    if (partLine == s_statusCmd) {
	for (String* list = s_statusCmds; !null(list); list++)
	    if (!partWord || list->startsWith(partWord))
		Module::itemComplete(msg.retValue(),*list,partWord);
	return true;
    }

    lock.drop();
    return Driver::commandComplete(msg,partLine,partWord);
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
