/**
 * rmanager.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * This module gets the messages from YATE out so anyone can use an
 * administrating interface.
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004 Null Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <yatengine.h>

#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

using namespace TelEngine;

static const char s_helpmsg[] =
"Available commands:\n"
"  debug [level|on|off]\n"
"  machine [on|off]\n"
"  status [module]\n"
"  drop {chan|*|all}\n"
"  call chan target\n"
"  reload\n"
"  quit\n"
"  stop [exitcode]\n";

static Configuration s_cfg;

/* I need this here because i'm gonna use it in both classes */
Socket s_sock;

//we gonna create here the list with all the new connections.
static ObjList connectionlist;
    
class RManagerThread : public Thread
{
public:
    RManagerThread() : Thread("RManager Listener") { }
    virtual void run();
private:
};

class Connection : public GenObject, public Thread
{
public:
    Connection(Socket* sock, const char* addr);
    ~Connection();

    virtual void run();
    bool processLine(const char *line);
    void writeStr(const char *str, int len = -1);
    void writeDebug(const char *str);
    void writeStr(Message &msg,bool received);
    inline void writeStr(const String &s)
	{ writeStr(s.safe(),s.length()); }
    inline const String& address() const
	{ return m_address; }
    static Connection *checkCreate(Socket* sock, const char* addr = 0);
private:
    bool m_debug;
    bool m_machine;
    Socket* m_socket;
    String m_address;
};

class RManager : public Plugin
{
public:
    RManager();
    ~RManager();
    virtual void initialize();
    virtual bool isBusy() const;
private:
    bool m_first;
};

static void dbg_remote_func(const char *buf)
{
    ObjList *p = &connectionlist;
    for (; p; p=p->next()) {
	Connection *con = static_cast<Connection *>(p->get());
	if (con)
	    con->writeDebug(buf);
    }
}

void RManagerThread::run()
{
    for (;;)
    {
	Thread::msleep(10,true);
        struct sockaddr_in sin;
	int sinlen = sizeof(sin);
	Socket* as = s_sock.accept((struct sockaddr *)&sin, (socklen_t *)&sinlen);
	if (!as) {
	    if (!s_sock.canRetry())
		Debug("RManager",DebugWarn, "Accept error: %s\n", strerror(s_sock.error()));
	    continue;
	} else {
	    String addr(inet_ntoa(sin.sin_addr));
	    addr << ":" << ntohs(sin.sin_port);
	    if (!Connection::checkCreate(as,addr))
		Debug("RManager",DebugWarn,"Connection rejected for %s",addr.c_str());
	}
    }
}

Connection *Connection::checkCreate(Socket* sock, const char* addr)
{
    if (!sock)
	return 0;
    if (!sock->valid()) {
	delete sock;
	return 0;
    }
    // should check IP address here
    Connection *conn = new Connection(sock,addr);
    if (conn->error()) {
	delete conn;
	return 0;
    }
    conn->startup();
    return conn;
}

Connection::Connection(Socket* sock, const char* addr)
    : Thread("RManager Connection"),
      m_debug(false), m_machine(false), m_socket(sock), m_address(addr)
{
    connectionlist.append(this);
}

Connection::~Connection()
{
    m_debug = false;
    connectionlist.remove(this,false);
    Output("Closing connection to %s",m_address.c_str());
    delete m_socket;
    m_socket = 0;
}

void Connection::run()
{
    if (!m_socket)
	return;
    if (!m_socket->setBlocking(false)) {
	Debug("RManager",DebugGoOn, "Failed to set tcp socket to nonblocking mode: %s\n",strerror(m_socket->error()));
	return;
    }

    // For the sake of responsiveness try to turn off the tcp assembly timer
    int arg = 1;
    if (!m_socket->setOption(SOL_SOCKET, TCP_NODELAY, &arg, sizeof(arg)))
	Debug("RManager",DebugWarn, "Failed to set tcp socket to TCP_NODELAY mode: %s\n", strerror(m_socket->error()));

    Output("Remote connection from %s",m_address.c_str());
    const char *hdr = s_cfg.getValue("general","header","YATE (http://YATE.null.ro) ready.");
    if (hdr) {
	writeStr(hdr);
	writeStr("\n");
	hdr = 0;
    }
    struct timeval timer;
    char buffer[300];
    int posinbuf = 0;
    while (posinbuf < (int)sizeof(buffer)-1) {
	Thread::check();
	timer.tv_sec = 0;
	timer.tv_usec = 10000;
	bool readok = false;
	bool error = false;
	if (m_socket->select(&readok,0,&error,&timer)) {
	    if (error) {
		Debug("RManager",DebugInfo,"Socket exception condition on %d",m_socket->handle());
		return;
	    }
	    if (!readok)
		continue;
	    int readsize = m_socket->readData(buffer+posinbuf,sizeof(buffer)-posinbuf-1);
	    if (!readsize) {
		Debug("RManager",DebugInfo,"Socket condition EOF on %d",m_socket->handle());
		return;
	    }
	    else if (readsize > 0) {
		int totalsize = readsize + posinbuf;
		buffer[totalsize]=0;
		XDebug("RManager",DebugInfo,"read=%d pos=%d buffer='%s'",readsize,posinbuf,buffer);
		for (;;) {
		    // Try to accomodate various telnet modes
		    char *eoline = ::strchr(buffer,'\r');
		    if (!eoline)
			eoline = ::strchr(buffer,'\n');
		    if (!eoline && ((int)::strlen(buffer) < totalsize))
			eoline=buffer+::strlen(buffer);
		    if (!eoline)
			break;
		    *eoline=0;
		    if (buffer[0] && processLine(buffer))
			return;
		    totalsize -= eoline-buffer+1;
		    ::memmove(buffer,eoline+1,totalsize+1);
		}
		posinbuf = totalsize;
	    }
	    else if (!m_socket->canRetry()) {
		Debug("RManager",DebugWarn,"Socket read error %d on %d",errno,m_socket->handle());
		return;
	    }
	}
	else if (!m_socket->canRetry()) {
	    Debug("RManager",DebugWarn,"socket select error %d on %d",errno,m_socket->handle());
	    return;
	}
    }	
}

bool Connection::processLine(const char *line)
{
    DDebug("RManager",DebugInfo,"processLine = %s",line);
    String str(line);
    str.trimBlanks();
    if (str.null())
	return false;

    if (str.startSkip("status"))
    {
	Message m("engine.status");
	if (str.null() || (str == "rmanager"))
	    m.retValue() << "name=rmanager,type=misc;conn=" << connectionlist.count() << "\n";
	if (!str.null()) {
	    m.addParam("module",str); 
	    str = ":" + str;
	}
	Engine::dispatch(m);
	str = "%%+status" + str + "\n";
	str << m.retValue() << "%%-status\n";
	writeStr(str);
    }
    else if (str.startSkip("quit"))
    {
	writeStr(m_machine ? "%%=quit\n" : "Goodbye!\n");
	return true;
    }
    else if (str.startSkip("drop"))
    {
	if (str.null()) {
	    writeStr(m_machine ? "%%=drop:fail=noarg\n" : "You must specify what connection to drop!\n");
	    return false;
	}
	Message m("call.drop");
	bool all = false;
	if (str == "*" || str == "all") {
	    all = true;
	    str = "all calls";
	}
	else
	    m.addParam("id",str); 
	if (Engine::dispatch(m))
	    str = (m_machine ? "%%=drop:success:" : "Dropped ") + str + "\n";
	else if (all)
	    str = (m_machine ? "%%=drop:unknown:" : "Tried to drop ") + str + "\n";
	else
	    str = (m_machine ? "%%=drop:fail:" : "Could not drop ") + str + "\n";
	writeStr(str);
    }
    else if (str.startSkip("call"))
    {
	int pos = str.find(' ');
	if (pos <= 0) {
	    writeStr(m_machine ? "%%=call:fail=noarg\n" : "You must specify source and target!\n");
	    return false;
	}
	Message m("call.execute");
	m.addParam("callto",str.substr(0,pos));
	m.addParam("target",str.substr(pos+1));

	if (Engine::dispatch(m))
	    str = (m_machine ? "%%=call:success:" : "Called ") + str + "\n";
	else
	    str = (m_machine ? "%%=call:fail:" : "Could not call ") + str + "\n";
	writeStr(str);
    }
    else if (str.startSkip("debug"))
    {
	if (str.startSkip("level")) {
	    int dbg = debugLevel();
	    str >> dbg;
	    dbg = debugLevel(dbg);
	}
	else
	    str >> m_debug;
	if (m_machine) {
	    str = "%%=debug:level=";
	    str << debugLevel() << ":local=" << m_debug << "\n";
	}
	else {
	    str = "Debug level: ";
	    str << debugLevel() << " local: " << (m_debug ? "on\n" : "off\n");
	}
	writeStr(str);
    }
    else if (str.startSkip("machine"))
    {
	str >> m_machine;
	str = "Machine mode: ";
	str += (m_machine ? "on\n" : "off\n");
	writeStr(str);
    }
    else if (str.startSkip("reload"))
    {
	writeStr(m_machine ? "%%=reload\n" : "Reinitializing...\n");
	Engine::init();
    }
    else if (str.startSkip("stop"))
    {
	unsigned code = 0;
	str >> code;
	writeStr(m_machine ? "%%=shutdown\n" : "Engine shutting down - bye!\n");
	Engine::halt(code);
    }
    else if (str.startSkip("help") || str.startSkip("?"))
    {
	Message m("engine.help");
	if (!str.null())
	{
	    m.addParam("line",str);
	    if (Engine::dispatch(m))
		writeStr(m.retValue());
	    else
		writeStr("No help for '"+str+"'\n");
	}
	else
	{
	    m.retValue() = s_helpmsg;
	    Engine::dispatch(m);
	    writeStr(m.retValue());
	}
    }
    else
    {
	Message m("engine.command");
	m.addParam("line",str);
	if (Engine::dispatch(m))
	    writeStr(m.retValue());
	else
	    writeStr((m_machine ? "%%=syntax:" : "Cannot understand: ") + str + "\n");
    }
    return false;
}

void Connection::writeStr(Message &msg,bool received)
{
    if (!m_machine)
	return;
    String s = msg.encode(received,"");
    s << "\n";
    writeStr(s.c_str());
}

void Connection::writeDebug(const char *str)
{
    if (m_debug && !null(str))
	writeStr(str,::strlen(str));
}

void Connection::writeStr(const char *str, int len)
{
    if (len < 0)
	len = ::strlen(str);
    if (int written = m_socket->writeData(str,len) != len) {
	Debug("RManager",DebugInfo,"Socket %d wrote only %d out of %d bytes",m_socket->handle(),written,len);
	// Destroy the thread, will kill the connection
	cancel();
    }
}

static void postHook(Message &msg, bool received)
{
    ObjList *p = &connectionlist;
    for (; p; p=p->next()) {
	Connection *con = static_cast<Connection *>(p->get());
	if (con)
	    con->writeStr(msg,received);
    }
};


RManager::RManager()
    : m_first(true)
{
    Output("Loaded module RManager");
    Debugger::setIntOut(dbg_remote_func);
}

RManager::~RManager()
{
    Output("Unloading module RManager");
    s_sock.terminate();
    Engine::self()->setHook();
    Debugger::setIntOut(0);
}

bool RManager::isBusy() const
{
    return (connectionlist.count() != 0);
}

void RManager::initialize()
{
    Output("Initializing module RManager");
    s_cfg = Engine::configFile("rmanager");
    s_cfg.load();

    if (s_sock.valid())
	return;

    // check configuration
    int port = s_cfg.getIntValue("general","port",5038);
    const char *host = c_safe(s_cfg.getValue("general","addr","127.0.0.1"));
    if (!(port && *host))
	return;

    s_sock.create(AF_INET, SOCK_STREAM);
    if (!s_sock.valid()) {
	Debug("RManager",DebugGoOn,"Unable to create the listening socket: %s",strerror(s_sock.error()));
	return;
    }

    if (!s_sock.setBlocking(false)) {
	Debug("RManager",DebugGoOn, "Failed to set listener to nonblocking mode: %s\n",strerror(s_sock.error()));
	return;
    }

    const int reuseFlag = 1;
    s_sock.setOption(SOL_SOCKET,SO_REUSEADDR,&reuseFlag,sizeof(reuseFlag));

    struct sockaddr_in bindaddr;
    bindaddr.sin_family = AF_INET;
    bindaddr.sin_addr.s_addr = inet_addr(host);
    bindaddr.sin_port = htons(port);

    if (!s_sock.bind((struct sockaddr *)&bindaddr, sizeof(bindaddr))) {
	Debug("RManager",DebugGoOn,"Failed to bind to %s:%u : %s",inet_ntoa(bindaddr.sin_addr),ntohs(bindaddr.sin_port),strerror(s_sock.error()));
	s_sock.terminate();
	return;
    }
    if (!s_sock.listen(2)) {
	Debug("RManager",DebugGoOn,"Unable to listen on socket: %s\n", strerror(s_sock.error()));
	s_sock.terminate();
	return;
    }
    
    // don't bother to install handlers until we are listening
    if (m_first) {
	m_first = false;
	Engine::self()->setHook(postHook);
	RManagerThread *mt = new RManagerThread;
	mt->startup();
    }
}

INIT_PLUGIN(RManager);

/* vi: set ts=8 sw=4 sts=4 noet: */
