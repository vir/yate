/**
 * rmanager.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * This module gets the messages from YATE out so anyone can use an
 * administrating interface.
 */

#include <telengine.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
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
int sock = -1;

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
    Connection(int sock);
    ~Connection();

    virtual void run();
    void processLine(const char *line);
    void write(const char *str, int len = -1);
    void writeDebug(const char *str);
    void write(Message &msg,bool received);
    inline void write(const String &s)
	{ write(s.safe(),s.length()); }
    static Connection *checkCreate(int sock);
private:
    int m_socket;
    bool m_debug;
    bool m_machine;
};

class RManager : public Plugin
{
public:
    RManager();
    ~RManager();
    virtual void initialize();
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
        struct sockaddr_in sin;
	int sinlen = sizeof(sin);
	int as = ::accept(sock, (struct sockaddr *)&sin, (socklen_t *)&sinlen);
	if (as < 0) {
	    Debug("RManager",DebugWarn, "Accept error: %s\n", strerror(errno));
	    continue;
	} else {
	    if (Connection::checkCreate(as))
		Debug("RManager",DebugInfo,"Connection established from %s:%u",inet_ntoa(sin.sin_addr),ntohs(sin.sin_port));
	    else {
		Debug("RManager",DebugWarn,"Connection rejected for %s:%u",inet_ntoa(sin.sin_addr),ntohs(sin.sin_port));
		::close(as);
	    }
	}
    }
}

Connection *Connection::checkCreate(int sock)
{
    // should check IP address here
    return new Connection(sock);
}

Connection::Connection(int sock)
    : Thread("RManager Connection"), m_socket(sock), m_debug(false), m_machine(false)
{
    const char *hdr = s_cfg.getValue("general","header","YATE (http://YATE.null.ro) ready.");
    if (hdr) {
	write(hdr);
	write("\n");
    }
    connectionlist.append(this);
}

Connection::~Connection()
{
    m_debug = false;
    connectionlist.remove(this,false);
    ::close(m_socket);
}

void Connection::run()
{
    if (::fcntl(m_socket,F_SETFL,O_NONBLOCK)) {
	Debug("RManager",DebugGoOn, "Failed to set tcp socket to nonblocking mode: %s\n", strerror(errno));
	return;
    }
    // For the sake of responsiveness try to turn off the tcp assembly timer
    int arg = 1;
    if (::setsockopt(m_socket, SOL_SOCKET, TCP_NODELAY, (char *)&arg, sizeof(arg) ) < 0)
	Debug("RManager",DebugWarn, "Failed to set tcp socket to TCP_NODELAY mode: %s\n", strerror(errno));

    struct timeval timer;
    char buffer[300];
    int posinbuf = 0;
    while (posinbuf < (int)sizeof(buffer)-1) {
	timer.tv_sec = 0;
	timer.tv_usec = 30000;
	fd_set readfd;
	FD_ZERO(&readfd);
	FD_SET(m_socket, &readfd);
	fd_set errorfd;
	FD_ZERO(&errorfd);
	FD_SET(m_socket, &errorfd);
	int c = ::select(m_socket+1,&readfd,0,&errorfd,&timer);
	//	Debug(DebugInfo,"trec pasul 1");
	if (c > 0) {
	    if (FD_ISSET(m_socket,&errorfd)) {
		Debug("RManager",DebugInfo,"Socket exception condition on %d",m_socket);
		return;
	    }
	    int readsize = ::read(m_socket,buffer+posinbuf,sizeof(buffer)-posinbuf-1);
	    if (!readsize) {
		Debug("RManager",DebugInfo,"Socket condition EOF on %d",m_socket);
		return;
	    }
	    else if (readsize > 0) {
		int totalsize = readsize + posinbuf;
		buffer[totalsize]=0;
#ifdef DEBUG
		Debug("RManager",DebugInfo,"read=%d pos=%d buffer='%s'",readsize,posinbuf,buffer);
#endif
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
		    if (buffer[0])
			processLine(buffer);
		    totalsize -= eoline-buffer+1;
		    ::memmove(buffer,eoline+1,totalsize+1);
		}
		posinbuf = totalsize;
	    }
	    else if ((readsize < 0) && (errno != EINTR) && (errno != EAGAIN)) {
		Debug("RManager",DebugWarn,"Socket read error %d on %d",errno,m_socket);
		return;
	    }
	}
	else if ((c < 0) && (errno != EINTR)) {
	    Debug("RManager",DebugWarn,"socket select error %d on %d",errno,m_socket);
	    return;
	}
    }	
}

static bool startSkip(String &s, const char *keyword)
{
    if (s.startsWith(keyword,true)) {
	s >> keyword;
	s.trimBlanks();
	return true;
    }
    return false;
}

void Connection::processLine(const char *line)
{
#ifdef DEBUG
    Debug("RManager",DebugInfo,"processLine = %s",line);
#endif
    String str(line);
    str.trimBlanks();
    if (str.null())
	return;

    if (startSkip(str,"status"))
    {
	Message m("status");
	if (!str.null()) {
	    m.addParam("module",str); 
	    str = ":" + str;
	}
	Engine::dispatch(m);
	str = "%%+status" + str + "\n";
	str << m.retValue() << "%%-status\n";
	write(str);
    }
    else if (startSkip(str,"drop"))
    {
	if (str.null()) {
	    write(m_machine ? "%%=drop:fail=noarg\n" : "You must specify what connection to drop!\n");
	    return;
	}
	Message m("drop");
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
	write(str);
    }
    else if (startSkip(str,"call"))
    {
	int pos = str.find(' ');
	if (pos <= 0) {
	    write(m_machine ? "%%=call:fail=noarg\n" : "You must specify source and target!\n");
	    return;
	}
	Message m("call");
	m.addParam("callto",str.substr(0,pos));
	m.addParam("target",str.substr(pos+1));

	if (Engine::dispatch(m))
	    str = (m_machine ? "%%=call:success:" : "Called ") + str + "\n";
	else
	    str = (m_machine ? "%%=call:fail:" : "Could not call ") + str + "\n";
	write(str);
    }
    else if (startSkip(str,"debug"))
    {
	if (startSkip(str,"level")) {
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
	write(str);
    }
    else if (startSkip(str,"machine"))
    {
	str >> m_machine;
	str = "Machine mode: ";
	str += (m_machine ? "on\n" : "off\n");
	write(str);
    }
    else if (startSkip(str,"reload"))
    {
	write(m_machine ? "%%=reload\n" : "Reinitializing...\n");
	Engine::init();
    }
    else if (startSkip(str,"quit"))
    {
	write(m_machine ? "%%=quit\n" : "Goodbye!\n");
	cancel();
    }
    else if (startSkip(str,"stop"))
    {
	unsigned code = 0;
	str >> code;
	write(m_machine ? "%%=shutdown\n" : "Engine shutting down - bye!\n");
	Engine::halt(code);
    }
    else if (startSkip(str,"help") || startSkip(str,"?"))
    {
	Message m("help");
	if (!str.null())
	{
	    m.addParam("line",str);
	    if (Engine::dispatch(m))
		write(m.retValue());
	    else
		write("No help for '"+str+"'\n");
	}
	else
	{
	    m.retValue() = s_helpmsg;
	    Engine::dispatch(m);
	    write(m.retValue());
	}
    }
    else
    {
	Message m("command");
	m.addParam("line",str);
	if (Engine::dispatch(m))
	    write(m.retValue());
	else
	    write((m_machine ? "%%=syntax:" : "Cannot understand: ") + str + "\n");
    }
}

void Connection::write(Message &msg,bool received)
{
    if (!m_machine)
	return;
    String s = msg.encode(received,"");
    s << "\n";
    write(s.c_str());
}

void Connection::writeDebug(const char *str)
{
    if (m_debug && str && *str)
	write(str,::strlen(str));
}

void Connection::write(const char *str, int len)
{
    if (len < 0)
	len = ::strlen(str);
    if (int written = ::write(m_socket,str,len) != len) {
	Debug("RManager",DebugInfo,"Socket %d wrote only %d out of %d bytes",m_socket,written,len);
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
	    con->write(msg,received);
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
    if (sock != -1) {
    	::close(sock);
	sock = -1;
    }
    Engine::self()->setHook();
    Debugger::setIntOut(0);
}

void RManager::initialize()
{
    Output("Initializing module RManager");
    s_cfg = Engine::configFile("rmanager");
    s_cfg.load();

    if (sock >= 0)
	return;

/* configuration */
    int port = s_cfg.getIntValue("general","port",5038);
    const char *host = c_safe(s_cfg.getValue("general","addr","127.0.0.1"));
    if (!(port && *host))
	return;

/* starting the socket */ 
    struct sockaddr_in bindaddr;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    bindaddr.sin_family = AF_INET;
    bindaddr.sin_addr.s_addr = inet_addr(host);
    bindaddr.sin_port = htons(port);
    if (sock < 0) {
	Debug("RManager",DebugGoOn,"Unable to create the listening socket: %s",strerror(errno));
	return;
    }
    const int reuseFlag = 1;
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,(const char*)&reuseFlag,sizeof reuseFlag);
    if (::bind(sock, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) < 0) {
	Debug("RManager",DebugGoOn,"Failed to bind to %s:%u : %s",inet_ntoa(bindaddr.sin_addr),ntohs(bindaddr.sin_port),strerror(errno));
	::close(sock);
        sock = -1;
	return;
    }
    if (listen(sock, 2)) {
	    Debug("RManager",DebugGoOn,"Unable to listen on socket: %s\n", strerror(errno));
	    ::close(sock);
	    sock = -1;
	    return;
    }
    
    // don't bother to install handlers until we are listening
    if (m_first) {
	m_first = false;
	Engine::self()->setHook(postHook);
	new RManagerThread;
    }
}

INIT_PLUGIN(RManager);

/* vi: set ts=8 sw=4 sts=4 noet: */
