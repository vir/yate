/**
 * rmanager.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * This module gets the messages from YATE out so anyone can use an
 * administrating interface.
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004, 2005 Null Team
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
"  quit\n"
"  help [command]\n"
"  status [module]\n"
"  machine [on|off]\n"
"  auth password\n"
"Authenticated commands:\n"
"  debug [level|on|off]\n"
"  drop {chan|*|all}\n"
"  call chan target\n"
"  reload\n"
"  stop [exitcode]\n";

static Configuration s_cfg;

/* I need this here because i'm gonna use it in both classes */
Socket s_sock;
Mutex s_mutex(true);

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
    void writeDebug(const char *str, int level);
    void writeStr(Message &msg,bool received);
    inline void writeStr(const String &s)
	{ writeStr(s.safe(),s.length()); }
    inline const String& address() const
	{ return m_address; }
    static Connection *checkCreate(Socket* sock, const char* addr = 0);
private:
    bool m_auth;
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

static void dbg_remote_func(const char *buf, int level)
{
    s_mutex.lock();
    ObjList *p = &connectionlist;
    for (; p; p=p->next()) {
	Connection *con = static_cast<Connection *>(p->get());
	if (con)
	    con->writeDebug(buf,level);
    }
    s_mutex.unlock();
}

void RManagerThread::run()
{
    for (;;)
    {
	Thread::msleep(10,true);
	SocketAddr sa;
	Socket* as = s_sock.accept(sa);
	if (!as) {
	    if (!s_sock.canRetry())
		Debug("RManager",DebugWarn, "Accept error: %s\n", strerror(s_sock.error()));
	    continue;
	} else {
	    String addr(sa.host());
	    addr << ":" << sa.port();
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
      m_auth(false), m_debug(false), m_machine(false), m_socket(sock), m_address(addr)
{
    s_mutex.lock();
    connectionlist.append(this);
    s_mutex.unlock();
}

Connection::~Connection()
{
    m_debug = false;
    s_mutex.lock();
    connectionlist.remove(this,false);
    s_mutex.unlock();
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
    m_auth = !s_cfg.getValue("general","password");
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
	return false;
    }
    else if (str.startSkip("machine"))
    {
	str >> m_machine;
	str = "Machine mode: ";
	str += (m_machine ? "on\n" : "off\n");
	writeStr(str);
	return false;
    }
    else if (str.startSkip("quit"))
    {
	writeStr(m_machine ? "%%=quit\n" : "Goodbye!\n");
	return true;
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
	return false;
    }
    else if (str.startSkip("auth"))
    {
	if (m_auth) {
	    writeStr(m_machine ? "%%=auth:success\n" : "You are already authenticated!\n");
	    return false;
	}
	if (str == s_cfg.getValue("general","password")) {
	    Output("Authenticated connection %s",m_address.c_str());
	    m_auth = true;
	    writeStr(m_machine ? "%%=auth:success\n" : "Authenticated successfully!\n");
	}
	else
	    writeStr(m_machine ? "%%=auth:fail=badpass\n" : "Bad authentication password!\n");
	return false;
    }
    if (!m_auth) {
	writeStr(m_machine ? "%%=*:fail=noauth\n" : "Not authenticated!\n");
	return false;
    }
    if (str.startSkip("drop"))
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
	String target = str.substr(pos+1);
	Message m("call.execute");
	m.addParam("callto",str.substr(0,pos));
	m.addParam((target.find('/') > 0) ? "direct" : "target",target);

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
	else if (str.isBoolean()) {
	    str >> m_debug;
	    if (m_debug)
		Debugger::enableOutput(true);
	}
	else if (str) {
	    String l;
	    int pos = str.find(' ');
	    if (pos > 0) {
		l = str.substr(pos+1);
		str = str.substr(0,pos);
		str.trimBlanks();
	    }
	    if (str.null()) {
		writeStr(m_machine ? "%%=debug:fail=noarg\n" : "You must specify debug module name!\n");
		return false;
	    }
	    Message m("engine.debug");
	    m.addParam("module",str);
	    if (l)
		m.addParam("line",l);
	    if (Engine::dispatch(m))
		writeStr(m.retValue());
	    else
		writeStr((m_machine ? "%%=debug:fail:" : "Cannot set debug: ") + str + " " + l + "\n");
	    return false;
	}
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

void Connection::writeDebug(const char *str, int level)
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
    s_mutex.lock();
    ObjList *p = &connectionlist;
    for (; p; p=p->next()) {
	Connection *con = static_cast<Connection *>(p->get());
	if (con)
	    con->writeStr(msg,received);
    }
    s_mutex.unlock();
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

    SocketAddr sa(AF_INET);
    sa.host(host);
    sa.port(port);
    if (!s_sock.bind(sa)) {
	Debug("RManager",DebugGoOn,"Failed to bind to %s:%u : %s",sa.host().c_str(),sa.port(),strerror(s_sock.error()));
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
