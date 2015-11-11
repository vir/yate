/**
 * Socket.cpp
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

#ifdef FDSIZE_HACK
#include <features.h>
#if (__GLIBC__ > 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 2)
#include <bits/types.h>
#undef __FD_SETSIZE
#define __FD_SETSIZE FDSIZE_HACK
#else
#error Cannot set FD_SETSIZE on this platform - please ./configure --without-fdsize and hope it works
#endif
#endif

#include "yateclass.h"

#include <string.h>

#ifdef HAVE_POLL
#include <poll.h>
#ifndef POLLRDHUP
#define POLLRDHUP 0
#endif
#endif

#undef HAS_AF_UNIX

#ifndef _WINDOWS

#include <net/if.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/un.h>
#define HAS_AF_UNIX
#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX (sizeof(((struct sockaddr_un *)0)->sun_path))
#endif

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <utime.h>
#endif

#ifndef SHUT_RD
#define SHUT_RD 0
#endif

#ifndef SHUT_WR
#define SHUT_WR 1
#endif

#ifndef SHUT_RDWR
#define SHUT_RDWR 2
#endif

#define MAX_SOCKLEN 1024
#define MAX_RESWAIT 5000000

using namespace TelEngine;

static Mutex s_mutex(false,"SocketAddr");

static const TokenDict s_tosValues[] = {
    // TOS
    { "normal",      Socket::Normal },
    { "lowdelay",    Socket::LowDelay },
    { "throughput",  Socket::MaxThroughput },
    { "reliability", Socket::MaxReliability },
    { "mincost",     Socket::MinCost },
    // DSCP
    { "expedited",   Socket::ExpeditedFwd },
    { "voice",       Socket::VoiceAdmit },
    { "af11",        Socket::AF11 },
    { "af12",        Socket::AF12 },
    { "af13",        Socket::AF13 },
    { "af21",        Socket::AF21 },
    { "af22",        Socket::AF22 },
    { "af23",        Socket::AF23 },
    { "af31",        Socket::AF31 },
    { "af32",        Socket::AF32 },
    { "af33",        Socket::AF33 },
    { "af41",        Socket::AF41 },
    { "af42",        Socket::AF42 },
    { "af43",        Socket::AF43 },
    { "cs0",         Socket::CS0 },
    { "cs1",         Socket::CS1 },
    { "cs2",         Socket::CS2 },
    { "cs3",         Socket::CS3 },
    { "cs4",         Socket::CS4 },
    { "cs5",         Socket::CS5 },
    { "cs6",         Socket::CS6 },
    { "cs7",         Socket::CS7 },
    { 0, 0 }
};

#ifdef _WINDOWS

// The number of seconds from January 1, 1601 (Windows FILETIME)
//  to EPOCH January 1, 1970
#define FILETIME_EPOCH_SEC 11644473600

// Convert from FILETIME (100 nsec units since January 1, 1601)
//  to time_t (seconds since January 1, 1970)
static inline unsigned int ftToEpoch(FILETIME& ft)
{
    // FILETIME in seconds
    u_int64_t rval = ((ULARGE_INTEGER*)&ft)->QuadPart / 10000000;
    // EPOCH time in seconds
    rval -= FILETIME_EPOCH_SEC;
    return (unsigned int)rval;
}

// Convert from time_t (seconds since January 1, 1970)
//  to FILETIME (100 nsec units since January 1, 1601)
static void epochToFt(unsigned int secEpoch, FILETIME& ft)
{
    u_int64_t time = (secEpoch + FILETIME_EPOCH_SEC) * 10000000;
    ft.dwLowDateTime = (DWORD)time;
    ft.dwHighDateTime = (DWORD)(time >> 32);
}

#endif


//
// IPv6 module functions
//
#ifdef AF_INET6

#if defined(HAVE_GHBN2_R) || defined(HAVE_GHBN2)
#define YATE_SOCKET_GHBN2_AVAILABLE

static inline bool ghbn2Set(struct sockaddr* addr, hostent* he, int family)
{
    if (!(he && he->h_addrtype == family && he->h_addr_list))
	return false;
    char* val = he->h_addr_list[0];
    if (!val)
	return false;
    if (family == AF_INET6) {
	((struct sockaddr_in6*)addr)->sin6_addr = *(in6_addr*)val;
	return true;
    }
    return false;
}

// Resolve an address using gethostbyname2_r or gethostbyname2
// Return 1 on success, 0 on failure, -1 if not available
static int resolveGHBN2(struct sockaddr* addr, const char* name)
{
    if (!addr || TelEngine::null(name))
	return 0;
    int family = AF_INET6;
#ifdef HAVE_GHBN2_R
    char buf[576];
    struct hostent h;
    struct hostent* hr = 0;
    int errn = 0;
    int r = gethostbyname2_r(name,family,&h,buf,sizeof(buf),&hr,&errn);
    if (r != ERANGE) {
	if (!r && ghbn2Set(addr,hr,family))
	    return 1;
	return 0;
    }
    // Buffer too short: fallback to gethostbyname2 if available
#endif
#ifdef HAVE_GHBN2
    Lock lck(s_mutex,MAX_RESWAIT);
    if (lck.locked()) {
	if (ghbn2Set(addr,gethostbyname2(name,family),family))
	    return 1;
    }
    else
	Alarm("engine","socket",DebugWarn,"Resolver was busy, failing '%s'",name);
#else
#ifndef HAVE_GHBN2_R
    return -1;
#endif
#endif
    return 0;
}

#endif // defined(HAVE_GHBN2_R) || defined(HAVE_GHBN2)

// Resolve a domain to IPv6 address
static inline bool resolveIPv6(struct sockaddr* addr, const char* name)
{
    static bool s_noIPv6 = true;
#ifdef YATE_SOCKET_GHBN2_AVAILABLE
    int res = resolveGHBN2(addr,name);
    if (res >= 0)
	return res > 0;
#endif
    // TODO: implement AF_INET6 resolving
    if (s_noIPv6) {
	s_noIPv6 = false;
	Alarm("engine","socket",DebugWarn,"Resolver for %s is not available",
	    SocketAddr::lookupFamily(SocketAddr::IPv6));
    }
    return false;
}

#endif // AF_INET6


const String s_ipv4NullAddr = "0.0.0.0";
const String s_ipv6NullAddr = "::";

const TokenDict SocketAddr::s_familyName[] = {
    {"Unknown", Unknown},
    {"IPv4",    IPv4},
    {"IPv6",    IPv6},
    {"Unix",    Unix},
    {0,0},
};

SocketAddr::SocketAddr(const struct sockaddr* addr, socklen_t len)
    : m_address(0), m_length(0)
{
    assign(addr,len);
}

SocketAddr::SocketAddr(int family, const void* raw)
    : m_address(0), m_length(0)
{
    assign(family);
    if (raw && m_address) {
	switch (family) {
	    case AF_INET:
		::memcpy(&((struct sockaddr_in*)m_address)->sin_addr,raw,4);
		break;
#ifdef AF_INET6
	    case AF_INET6:
		::memcpy(&((struct sockaddr_in6*)m_address)->sin6_addr,raw,16);
		break;
#endif
	}
	SocketAddr::stringify();
    }
}

SocketAddr::~SocketAddr()
{
    clear();
}

void SocketAddr::clear()
{
    m_length = 0;
    m_host.clear();
    m_addr.clear();
    void* tmp = m_address;
    m_address = 0;
    if (tmp)
	::free(tmp);
}

bool SocketAddr::assign(int family)
{
    clear();
    switch (family) {
	case AF_INET:
	    m_length = sizeof(struct sockaddr_in);
	    break;
#ifdef AF_INET6
	case AF_INET6:
	    m_length = sizeof(struct sockaddr_in6);
	    break;
#endif
#ifdef HAS_AF_UNIX
	case AF_UNIX:
	    m_length = sizeof(struct sockaddr_un);
	    break;
#endif
    }
    if (m_length)
	m_address = (struct sockaddr*) ::calloc(m_length,1);
    if (m_address) {
	m_address->sa_family = family;
#ifdef HAVE_SOCKADDR_LEN
	m_address->sa_len = m_length;
#endif
	return true;
    }
    return false;
}

void SocketAddr::assign(const struct sockaddr* addr, socklen_t len)
{
    if (addr == m_address)
	return;
#ifdef HAVE_SOCKADDR_LEN
    if (addr && !len)
	len = addr->sa_len;
#endif
    if (addr && !len) {
	switch (addr->sa_family) {
	    case AF_INET:
		len = sizeof(struct sockaddr_in);
		break;
#ifdef AF_INET6
	    case AF_INET6:
		len = sizeof(struct sockaddr_in6);
		break;
#endif
#ifdef HAS_AF_UNIX
	    case AF_UNIX:
		len = sizeof(struct sockaddr_un);
		break;
#endif
	}
    }
    if (addr && m_address && (len == m_length) && !::memcmp(addr,m_address,len))
	return;
    clear();
    if (addr && (len >= (socklen_t)sizeof(struct sockaddr))) {
	void* tmp = ::malloc(len);
	::memcpy(tmp,addr,len);
	m_address = (struct sockaddr*)tmp;
	m_length = len;
	stringify();
    }
}

bool SocketAddr::assign(const DataBlock& addr)
{
    clear();
    switch (addr.length()) {
	case 4:
	    if (assign(AF_INET)) {
		::memcpy(&((struct sockaddr_in*)m_address)->sin_addr,addr.data(),addr.length());
		stringify();
		return true;
	    }
	    break;
#ifdef AF_INET6
	case 8: // IPv6 /64 prefix
	case 16:
	    if (assign(AF_INET6)) {
		::memcpy(&((struct sockaddr_in6*)m_address)->sin6_addr,addr.data(),addr.length());
		stringify();
		return true;
	    }
	    break;
#endif
    }
    return false;
}

int SocketAddr::copyAddr(DataBlock& addr) const
{
    if (!m_address)
	return false;
    switch (family()) {
	case AF_INET:
	    addr.assign(&((struct sockaddr_in*)m_address)->sin_addr,4);
	    return IPv4;
#ifdef AF_INET6
	case AF_INET6:
	    addr.assign(&((struct sockaddr_in6*)m_address)->sin6_addr,16);
	    return IPv6;
#endif
    }
    return Unknown;
}

bool SocketAddr::local(const SocketAddr& remote)
{
    if (!remote.valid())
	return false;
    SocketAddr tmp(remote);
    if (!tmp.port())
	tmp.port(16384);
    Socket sock(tmp.family(),SOCK_DGRAM);
    if (sock.valid() && sock.connect(tmp) && sock.getSockName(*this)) {
	port(0);
	return true;
    }
    return false;
}

bool SocketAddr::host(const String& name)
{
    if (name.null())
	return false;
    if (name == m_host)
	return true;
    if (!m_address) {
	int f = family(name);
	switch (f) {
	    case Unix:
#ifdef HAS_AF_UNIX
		if (assign(AF_UNIX) && host(name))
		    return true;
#endif
		break;
	    case Unknown:
		// fall through to set IP host
	    case IPv6:
#ifdef AF_INET6
		if (assign(AF_INET6) && host(name))
		    return true;
#endif
		if (f == IPv6)
		    break;
		// fall through to IPv4
	    case IPv4:
		if (assign(AF_INET) && host(name))
		    return true;
		break;
	}
	// Restore data
	clear();
	return false;
    }
    switch (family()) {
	case AF_INET:
	    {
		in_addr_t a = inet_addr(name);
		if (a == INADDR_NONE) {
#ifdef HAVE_GHBN_R
		    char buf[576];
		    struct hostent h;
		    struct hostent* hr = 0;
		    int errn = 0;
		    int r = gethostbyname_r(name,&h,buf,sizeof(buf),&hr,&errn);
		    if (r != ERANGE) {
			if ((r == 0) && hr && (hr->h_addrtype == AF_INET))
			    a = *((in_addr_t*)(hr->h_addr_list[0]));
		    }
		    else // fallback to single threaded version
#endif
		    if (s_mutex.lock(MAX_RESWAIT)) {
			struct hostent* he = gethostbyname(name);
			if (he && (he->h_addrtype == AF_INET))
			    a = *((in_addr_t*)(he->h_addr_list[0]));
			s_mutex.unlock();
		    }
		    else
			Alarm("engine","socket",DebugWarn,"Resolver was busy, failing '%s'",name.c_str());
		}
		if (a != INADDR_NONE) {
		    ((struct sockaddr_in*)m_address)->sin_addr.s_addr = a;
		    stringify();
		    return true;
		}
	    }
	    break;
#ifdef AF_INET6
	case AF_INET6:
	    if (name.find('%') >= 0) {
		String tmp, iface;
		splitIface(name,tmp,&iface);
		if (!host(tmp))
		    return false;
		if (iface)
#ifndef _WINDOWS
		    scopeId(if_nametoindex(iface));
#else
		    scopeId(iface.toInteger(0,0,0));
#endif
		return true;
	    }
#ifdef HAVE_PTON
	    if (inet_pton(family(),name,&((struct sockaddr_in6*)m_address)->sin6_addr) > 0) {
		stringify();
		return true;
	    }
#endif
	    if (resolveIPv6(m_address,name)) {
		stringify();
		return true;
	    }
	    break;
#endif // AF_INET6
#ifdef HAS_AF_UNIX
	case AF_UNIX:
	    if (name.length() >= (UNIX_PATH_MAX-1))
		return false;
	    ::strcpy(((struct sockaddr_un*)m_address)->sun_path,name.c_str());
	    stringify();
	    return true;
#endif
    }
    return false;
}

// Retrieve the family of an address
int SocketAddr::family(const String& addr)
{
    if (!addr)
	return Unknown;
    bool ipv6 = false;
    for (unsigned int i = 0; i < addr.length(); i++) {
	if (addr[i] == '/')
	    return Unix;
	if (addr[i] == ':')
	    ipv6 = true;
    }
    if (ipv6)
	return IPv6;
    in_addr_t a = inet_addr(addr);
    if (a != INADDR_NONE || addr == YSTRING("255.255.255.255"))
	return IPv4;
    return Unknown;
}

// Convert the host address to a String
bool SocketAddr::stringify(String& s, struct sockaddr* addr)
{
    if (!addr)
	return false;
    switch (addr->sa_family) {
	case AF_INET:
#ifdef HAVE_NTOP
	    {
		char buf[16];
		buf[0] = '\0';
		s = inet_ntop(addr->sa_family,&((struct sockaddr_in*)addr)->sin_addr,
		    buf,sizeof(buf));
	    }
#else
	    s_mutex.lock();
	    s = inet_ntoa(((struct sockaddr_in*)addr)->sin_addr);
	    s_mutex.unlock();
#endif
	    return true;
#ifdef AF_INET6
	case AF_INET6:
#ifdef HAVE_NTOP
	    {
		char buf[48];
		buf[0] = '\0';
		s = inet_ntop(addr->sa_family,&((struct sockaddr_in6*)addr)->sin6_addr,
		    buf,sizeof(buf));
	    }
	    return true;
#endif
	    break;
#endif // AF_INET6
#ifdef HAS_AF_UNIX
	case AF_UNIX:
	    s = ((struct sockaddr_un*)addr)->sun_path;
	    return true;
#endif
    }
    return false;
}

// Copy a host address to a buffer
int SocketAddr::copyAddr(uint8_t* buf, struct sockaddr* addr)
{
    if (!(buf && addr))
	return Unknown;
    switch (addr->sa_family) {
	case AF_INET:
	    ::memcpy(buf,&((struct sockaddr_in*)addr)->sin_addr,4);
	    return IPv4;
#ifdef AF_INET6
	case AF_INET6:
	    ::memcpy(buf,&((struct sockaddr_in6*)addr)->sin6_addr,16);
	    return IPv6;
#endif
    }
    return Unknown;
}

// Append an address to a buffer
String& SocketAddr::appendAddr(String& buf, const String& addr, int family)
{
    if (!addr)
	return buf;
    // Address already starts with [
    if (addr[0] == '[') {
	buf << addr;
	return buf;
    }
    if (family == Unknown) {
	// Match ip::v6 or ::ffff:ip.v4 but not ip.v4:port
	int col = addr.rfind(':');
	if (col >= 0) {
	    int dot = addr.find('.');
	    if ((dot < 0) || (dot > col))
		family = IPv6;
	}
    }
    if (family != IPv6)
	buf << addr;
    else
	buf << "[" << addr << "]";
    return buf;
}

// Check if an address is empty or null
bool SocketAddr::isNullAddr(const String& addr, int family)
{
    if (!addr)
	return true;
    switch (family) {
	case IPv4:
	    return addr == s_ipv4NullAddr;
	case IPv6:
	    return addr == s_ipv6NullAddr;
    }
    return addr == s_ipv4NullAddr || addr == s_ipv6NullAddr;
}

// Split an interface from address
// An interface may be present in addr after a percent char (e.g. fe80::23%eth0)
void SocketAddr::splitIface(const String& buf, String& addr, String* iface)
{
    if (!buf) {
	addr.clear();
	if (iface)
	    iface->clear();
	return;
    }
    int pos = buf.find('%');
    if (pos < 0) {
	if (iface)
	    iface->clear();
	addr = buf;
    }
    else {
	if (iface)
	    *iface = buf.substr(pos + 1);
	addr = buf.substr(0,pos);
    }
}

// Split an address into ip/port
// Handle addr, addr:port, [addr], [addr]:port
void SocketAddr::split(const String& buf, String& addr, int& port, bool portPresent)
{
    if (!buf) {
	addr.clear();
	return;
    }
    if (buf[0] == '[') {
	int p = buf.find(']',1);
	if (p >= 1) {
	    if (p < ((int)buf.length() - 1) && buf[p + 1] == ':')
		port = buf.substr(p + 2).toInteger();
	    addr.assign(buf.c_str() + 1,p - 1);
	    return;
	}
    }
    int p = buf.find(':');
    if (p >= 0) {
	// Check for a second ':': it may be an IPv6 address
	// or we expect a port at the end of an IPv6 address
	int p2 = buf.rfind(':');
	if (p == p2 || portPresent) {
	    port = buf.substr(p2 + 1).toInteger();
	    addr.assign(buf.c_str(),p2);
	}
	else
	    addr = buf;
    }
    else
	addr = buf;
}

const String& SocketAddr::ipv4NullAddr()
{
    return s_ipv4NullAddr;
}

const String& SocketAddr::ipv6NullAddr()
{
    return s_ipv6NullAddr;
}

const TokenDict* SocketAddr::dictFamilyName()
{
    return s_familyName;
}

void SocketAddr::stringify()
{
    m_host.clear();
    m_addr.clear();
    if (m_length && m_address)
	stringify(m_host,m_address);
}

// Store host:port in m_addr
void SocketAddr::updateAddr() const
{
    m_addr.clear();
    appendTo(m_addr,host(),port(),family());
}

int SocketAddr::port() const
{
    switch (family()) {
	case AF_INET:
	    return ntohs(((struct sockaddr_in*)m_address)->sin_port);
#ifdef AF_INET6
	case AF_INET6:
	    return ntohs(((struct sockaddr_in6*)m_address)->sin6_port);
#endif
    }
    return 0;
}

bool SocketAddr::port(int newport)
{
    switch (family()) {
	case AF_INET:
	    ((struct sockaddr_in*)m_address)->sin_port = ntohs(newport);
	    break;
#ifdef AF_INET6
	case AF_INET6:
	    ((struct sockaddr_in6*)m_address)->sin6_port = ntohs(newport);
	    break;
#endif
#ifdef HAS_AF_UNIX
	case AF_UNIX:
	    break;
#endif
	default:
	    return false;
    }
    m_addr.clear();
    return true;
}

bool SocketAddr::operator==(const SocketAddr& other) const
{
    if (m_length != other.length())
	return false;
    if (m_address == other.address())
	return true;
    if (m_address && other.address())
	return !::memcmp(m_address,other.address(),m_length);
    return false;
}

bool SocketAddr::supports(int family)
{
    switch (family) {
	case AF_INET:
	    return true;
#ifdef AF_INET6
	case AF_INET6:
	    return true;
#endif
#ifdef HAS_AF_UNIX
	case AF_UNIX:
	    return true;
#endif
	default:
	    return false;
    }
}


SocketFilter::SocketFilter()
    : m_socket(0)
{
}

SocketFilter::~SocketFilter()
{
    if (m_socket)
	m_socket->removeFilter(this);
}

void* SocketFilter::getObject(const String& name) const
{
    if (name == YATOM("SocketFilter"))
	return const_cast<SocketFilter*>(this);
    return GenObject::getObject(name);
}

void SocketFilter::timerTick(const Time& when)
{
}

bool SocketFilter::valid() const
{
    return m_socket && m_socket->valid();
}


Stream::~Stream()
{
}

bool Stream::canRetry() const
{
    return false;
}

bool Stream::inProgress() const
{
    return false;
}

bool Stream::setBlocking(bool block)
{
    return false;
}

int64_t Stream::length()
{
    return 0;
}

int64_t Stream::seek(SeekPos pos, int64_t offset)
{
    return -1;
}

int Stream::writeData(const char* str)
{
    if (null(str))
	return 0;
    int len = ::strlen(str);
    return writeData(str,len);
}

bool Stream::allocPipe(Stream*& reader, Stream*& writer)
{
    if (supportsPipes()) {
	File* r = new File;
	File* w = new File;
	if (File::createPipe(*r,*w)) {
	    reader = r;
	    writer = w;
	    return true;
	}
	delete r;
	delete w;
    }
    reader = writer = 0;
    return false;
}

bool Stream::allocPair(Stream*& str1, Stream*& str2)
{
    if (supportsPairs()) {
	Socket* s1 = new Socket;
	Socket* s2 = new Socket;
	if (Socket::createPair(*s1,*s2)) {
	    str1 = s1;
	    str2 = s2;
	    return true;
	}
	delete s1;
	delete s2;
    }
    str1 = str2 = 0;
    return false;
}

bool Stream::supportsPipes()
{
    return true;
}

bool Stream::supportsPairs()
{
#ifdef _WINDOWS
    return false;
#else
    return true;
#endif
}


int MemoryStream::writeData(const void* buffer, int len)
{
    if ((len < 0) || !buffer)
	return -1;
    if (!len)
	return 0;
    int ovr = m_data.length() - m_offset;
    if (ovr < 0)
	ovr = 0;
    else if (ovr > len)
	ovr = len;
    if (ovr > 0) {
	// overwrite part of the existing data
	void* dest = m_data.data(m_offset,ovr);
	if (!dest)
	    return -1;
	::memcpy(dest,buffer,ovr);
	m_offset += ovr;
	len -= ovr;
	buffer = static_cast<const char*>(buffer) + ovr;
    }
    if (len > 0) {
	DataBlock tmp(const_cast<void*>(buffer),len,false);
	m_data += tmp;
	m_offset += len;
	tmp.clear(false);
    }
    return len + ovr;
}

int MemoryStream::readData(void* buffer, int len)
{
    if ((len <= 0) || !buffer)
	return -1;
    if (len + m_offset > m_data.length())
	len = m_data.length() - m_offset;
    if (len <= 0)
	return 0;
    void* src = m_data.data(m_offset,len);
    if (!src)
	return -1;
    ::memcpy(buffer,src,len);
    m_offset += len;
    return len;
}

int64_t MemoryStream::seek(SeekPos pos, int64_t offset)
{
    switch (pos) {
	case SeekBegin:
	    break;
	case SeekEnd:
	    offset += length();
	    break;
	case SeekCurrent:
	    offset += m_offset;
	    break;
    }
    if ((offset < 0) || (offset > length()))
	return -1;
    m_offset = offset;
    return offset;
}


File::File()
    : m_handle(invalidHandle())
{
    DDebug(DebugAll,"File::File() [%p]",this);
}

File::File(HANDLE handle)
    : m_handle(handle)
{
    DDebug(DebugAll,"File::File(%d) [%p]",(int)handle,this);
}

File::~File()
{
    DDebug(DebugAll,"File::~File() handle=%d [%p]",(int)m_handle,this);
    terminate();
}

bool File::valid() const
{
    return (m_handle != invalidHandle());
}

bool File::terminate()
{
    bool ret = true;
    HANDLE tmp = m_handle;
    if (tmp != invalidHandle()) {
	DDebug(DebugAll,"File::terminate() handle=%d [%p]",(int)m_handle,this);
	m_handle = invalidHandle();
#ifdef _WINDOWS
	ret = CloseHandle(tmp) != 0;
#else
	ret = !::close(tmp);
#endif
    }
    if (ret)
	clearError();
    else {
	copyError();
	// put back the handle, we may have another chance later
	m_handle = tmp;
    }
    return ret;
}

void File::attach(HANDLE handle)
{
    DDebug(DebugAll,"File::attach(%d) [%p]",(int)handle,this);
    if (handle == m_handle)
	return;
    terminate();
    m_handle = handle;
    clearError();
}

HANDLE File::detach()
{
    DDebug(DebugAll,"File::detach() handle=%d [%p]",(int)m_handle,this);
    HANDLE tmp = m_handle;
    m_handle = invalidHandle();
    clearError();
    return tmp;
}

HANDLE File::invalidHandle()
{
#ifdef _WINDOWS
    return INVALID_HANDLE_VALUE;
#else
    return -1;
#endif
}

void File::copyError()
{
#ifdef _WINDOWS
    m_error = (int)GetLastError();
#else
    m_error = errno;
#endif
}

bool File::canRetry() const
{
    if (!valid())
	return false;
    if (!m_error)
	return true;
    return (m_error == EAGAIN) || (m_error == EINTR)
#ifndef _WINDOWS
	|| (m_error == EWOULDBLOCK)
#endif
	;
}

bool File::setBlocking(bool block)
{
#ifdef _WINDOWS
    return false;
#else
    unsigned long flags = 1;
    flags = ::fcntl(m_handle,F_GETFL);
    if ((signed long)flags < 0) {
	copyError();
	return false;
    }
    if (block)
	flags &= !O_NONBLOCK;
    else
	flags |= O_NONBLOCK;
    if (::fcntl(m_handle,F_SETFL,flags) < 0) {
	copyError();
	return false;
    }
    clearError();
    return true;
#endif
}

bool File::openPath(const char* name, bool canWrite, bool canRead,
    bool create, bool append, bool binary, bool pubReadable, bool pubWritable)
{
    if (!terminate())
	return false;
    if (null(name) || !(canWrite || canRead))
	return false;
#ifdef _WINDOWS
    DWORD access = 0;
    if (canWrite)
	access |= GENERIC_WRITE;
    if (canRead)
	access |= GENERIC_READ;
    DWORD createMode;
    if (create)
	createMode = (!canRead && !append) ? CREATE_ALWAYS : OPEN_ALWAYS;
    else
	createMode = OPEN_EXISTING;
    DWORD share = 0;
    if (!canWrite && canRead)
	share |= FILE_SHARE_READ;
    HANDLE h = CreateFile(name,access,share,NULL,createMode,FILE_ATTRIBUTE_NORMAL,NULL);
    if (h == invalidHandle()) {
	copyError();
	return false;
    }
    // Move file pointer if append. Result might be the same as the error code
    if (append &&
	::SetFilePointer(h,0,NULL,FILE_END) == INVALID_SET_FILE_POINTER &&
	::GetLastError() != NO_ERROR) {
	copyError();
	::CloseHandle(h);
	return false;
    }
#else
    int flags = O_LARGEFILE;
    if (canWrite)
	flags |= canRead ? O_RDWR : O_WRONLY;
    else if (canRead)
	flags = O_RDONLY;
    if (create)
	flags |= O_CREAT;
    if (append)
	flags |= O_APPEND;
    else if (!canRead)
	flags |= O_TRUNC;
    if (binary)
	flags |= O_BINARY;
    int mode = S_IRUSR|S_IWUSR;
    if (pubReadable)
	mode |= S_IRGRP|S_IROTH;
    if (pubWritable)
	mode |= S_IWGRP|S_IWOTH;
    HANDLE h = ::open(name,flags,mode);
    if (h == invalidHandle()) {
	copyError();
	return false;
    }
#endif
    attach(h);
    clearError();
    return true;
}

int64_t File::length()
{
    if (!valid())
	return 0;
#ifdef _WINDOWS
    LARGE_INTEGER li;
    li.LowPart = ::GetFileSize(m_handle,(LPDWORD)(&li.HighPart));
    if (li.LowPart == INVALID_FILE_SIZE && ::GetLastError() != NO_ERROR) {
	copyError();
	return -1;
    }
    return li.QuadPart;
#else
    int64_t pos = seek(SeekCurrent);
    if (pos < 0) {
	copyError();
	return 0;
    }
    int64_t len = seek(SeekEnd);
    seek(SeekBegin,pos);
    return len;
#endif
}

// Set the file read/write pointer
int64_t File::seek(SeekPos pos, int64_t offset)
{
    if (!valid())
	return -1;
#ifdef _WINDOWS
    int whence = (pos == SeekBegin) ? FILE_BEGIN : ((pos == SeekEnd) ? FILE_END : FILE_CURRENT);
    LARGE_INTEGER li;
    li.QuadPart = offset;
    li.LowPart = ::SetFilePointer(m_handle,li.LowPart,&li.HighPart,whence);
    // Check low 32bit value and the last error
    // It might have the same as the error code
    if (li.LowPart == INVALID_SET_FILE_POINTER && ::GetLastError() != NO_ERROR) {
	copyError();
	return -1;
    }
    return li.QuadPart;
#else
    int whence = (pos == SeekBegin) ? SEEK_SET : ((pos == SeekEnd) ? SEEK_END : SEEK_CUR);
    off_t p = ::lseek(m_handle,(off_t)offset,whence);
    if (p == (off_t)-1) {
	copyError();
	return -1;
    }
    return (int64_t)p;
#endif
}

int File::writeData(const void* buffer, int length)
{
    if (!buffer)
	length = 0;
#ifdef _WINDOWS
    DWORD nbytes = 0;
    if (WriteFile(m_handle,buffer,length,&nbytes,0)) {
	clearError();
	return nbytes;
    }
    copyError();
    return -1;
#else
    int res = ::write(m_handle,buffer,length);
    if (res >= 0)
	clearError();
    else
	copyError();
    return res;
#endif
}

int File::readData(void* buffer, int length)
{
    if (!buffer)
	length = 0;
#ifdef _WINDOWS
    DWORD nbytes = 0;
    if (ReadFile(m_handle,buffer,length,&nbytes,0)) {
	clearError();
	return nbytes;
    }
    else if (::GetLastError() == ERROR_HANDLE_EOF) {
	clearError();
	return 0;
    }
    copyError();
    return -1;
#else
    int res = ::read(m_handle,buffer,length);
    if (res >= 0)
	clearError();
    else
	copyError();
    return res;
#endif
}

bool File::createPipe(File& reader, File& writer)
{
#ifdef _WINDOWS
    HANDLE rd, wr;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    if (::CreatePipe(&rd,&wr,&sa,0)) {
	reader.attach(rd);
	writer.attach(wr);
	return true;
    }
#else
    HANDLE fifo[2];
    if (!::pipe(fifo)) {
	reader.attach(fifo[0]);
	writer.attach(fifo[1]);
	return true;
    }
#endif
    return false;
}

// Retrieve the file's modification time (the file must be already opened)
bool File::getFileTime(unsigned int& secEpoch)
{
#ifdef _WINDOWS
    FILETIME ftWrite;
    if (::GetFileTime(handle(),NULL,NULL,&ftWrite)) {
	clearError();
	secEpoch = ftToEpoch(ftWrite);
	return true;
    }
#else
    struct stat st;
    if (0 == ::fstat(handle(),&st)) {
	clearError();
	secEpoch = st.st_mtime;
	return true;
    }
#endif
    copyError();
    return false;
}

// Build the MD5 hex digest of an opened file.
bool File::md5(String& buffer)
{
    if (-1 == Stream::seek(0))
	return false;
    MD5 md5;
    unsigned char buf[65536];
    bool ok = false;
    unsigned int retry = 3;
    while (retry) {
	int n = readData(buf,sizeof(buf));
	if (n < 0) {
	    if (canRetry())
		retry--;
	    else
		retry = 0;
	    continue;
	}
	if (n == 0) {
	    ok = true;
	    break;
	}
	DataBlock tmp(buf,n,false);
	md5 << tmp;
	tmp.clear(false);
    }
    if (ok)
	buffer = md5.hexDigest();
    else
	buffer = "";
    return ok;
}


// Set last error and return false
static inline bool getLastError(int* error)
{
    if (error)
	*error = Thread::lastError();
    return false;
}

// Check if a file name is non null
// Set error and return false if it is
static inline bool fileNameOk(const char* name, int* error)
{
    if (!null(name))
	return true;
    if (error)
#ifdef _WINDOWS
	*error = ERROR_INVALID_PARAMETER;
#else
	*error = EINVAL;
#endif
    return false;
}

// Set a file's modification time
bool File::setFileTime(const char* name, unsigned int secEpoch, int* error)
{
    if (!fileNameOk(name,error))
	return false;
#ifdef _WINDOWS
    File f;
    if (f.openPath(name,true)) {
	FILETIME ftWrite;
	epochToFt(secEpoch,ftWrite);
	bool ok = (0 != ::SetFileTime(f.handle(),NULL,NULL,&ftWrite));
	if (!ok && error)
	    *error = ::GetLastError();
	f.terminate();
	return ok;
    }
#else
    struct stat st;
    if (0 == ::stat(name,&st)) {
	struct utimbuf tb;
	tb.actime = st.st_atime;
	tb.modtime = secEpoch;
	if (0 == ::utime(name,&tb))
	    return true;
    }
#endif
    return getLastError(error);
}

// Retrieve a file's modification time
bool File::getFileTime(const char* name, unsigned int& secEpoch, int* error)
{
    if (!fileNameOk(name,error))
	return false;
#ifdef _WINDOWS
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (::GetFileAttributesExA(name,GetFileExInfoStandard,&fa)) {
	secEpoch = ftToEpoch(fa.ftLastWriteTime);
	return true;
    }
#else
    struct stat st;
    if (0 == ::stat(name,&st)) {
	secEpoch = st.st_mtime;
	return true;
    }
#endif
    return getLastError(error);
}

// Check if a file exists
bool File::exists(const char* name, int* error)
{
    if (!fileNameOk(name,error))
	return false;
#ifdef _WINDOWS
    WIN32_FIND_DATA d;
    HANDLE h = ::FindFirstFile(name,&d);
    if (h != invalidHandle()) {
	::FindClose(h);
	return true;
    }
#else
    if (0 == ::access(name,F_OK))
	return true;
#endif
    return getLastError(error);
}

// Rename (move) a file (or directory) entry from the filesystem
bool File::rename(const char* oldFile, const char* newFile, int* error)
{
    if (!(fileNameOk(oldFile,error) && fileNameOk(newFile,error)))
	return false;
#ifdef _WINDOWS
    DWORD flags = MOVEFILE_COPY_ALLOWED |  // Allow moving file on another volume
	MOVEFILE_REPLACE_EXISTING |        // Replace existing
	MOVEFILE_WRITE_THROUGH;            // Don't return until copy/delete is performed
    if (::MoveFileExA(oldFile,newFile,flags))
	return true;
#else
    if (0 == ::rename(oldFile,newFile))
	return true;
#endif
    return getLastError(error);
}

bool File::remove(const char* name, int* error)
{
    if (!fileNameOk(name,error))
	return false;
#ifdef _WINDOWS
    if (::DeleteFileA(name))
	return true;
#else
    if (0 == ::unlink(name))
	return true;
#endif
    return getLastError(error);
}

// Build the MD5 hex digest of a file.
bool File::md5(const char* name, String& buffer, int* error)
{
    File f;
    bool ok = false;
    if (f.openPath(name,false,true) && f.md5(buffer))
	ok = true;
    else if (error)
        *error = f.error();
    f.terminate();
    return ok;
}

// Create a folder (directory)
bool File::mkDir(const char* path, int* error, int mode)
{
    if (!fileNameOk(path,error))
	return false;
#ifdef _WINDOWS
    if (::CreateDirectoryA(path,NULL))
	return true;
#else
    if (0 == ::mkdir(path,(mode_t)mode))
	return true;
#endif
    return getLastError(error);
}

// Remove an empty folder (directory)
bool File::rmDir(const char* path, int* error)
{
    if (!fileNameOk(path,error))
	return false;
#ifdef _WINDOWS
    if (::RemoveDirectoryA(path))
	return true;
#else
    if (0 == ::rmdir(path))
	return true;
#endif
    return getLastError(error);
}

// Skip special directories (. or ..)
static inline bool skipSpecial(const char* s)
{
    return *s && *s == '.' && (!s[1] || (s[1] == '.' && !s[2]));
}

// Enumerate a folder (directory) content
bool File::listDirectory(const char* path, ObjList* dirs, ObjList* files, int* error)
{
    if (!(dirs || files))
	return true;
    if (!fileNameOk(path,error))
	return false;
    bool ok = false;
#ifdef _WINDOWS
    String name(path);
    if (!name.endsWith("\\"))
	name << "\\";
    name << "*";
    // Init find
    WIN32_FIND_DATAA d;
    HANDLE hFind = ::FindFirstFileA(name,&d);
    if (hFind == INVALID_HANDLE_VALUE) {
	if (::GetLastError() == ERROR_NO_MORE_FILES)
	    return true;
	return getLastError(error);
    }
    // Enumerate content
    ::SetLastError(0);
    do {
        if (d.dwFileAttributes & FILE_ATTRIBUTE_DEVICE ||
	    skipSpecial(d.cFileName))
	    continue;
        if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
	    if (dirs)
		dirs->append(new String(d.cFileName));
	}
	else if (files)
	    files->append(new String(d.cFileName));
    }
    while (::FindNextFileA(hFind,&d));
    int code = ::GetLastError();
    ok = !code || code == ERROR_NO_MORE_FILES;
    // Get error before closing the handle to avoid having a wrong one
    if (!ok && error)
	*error = code;
    ::FindClose(hFind);
#else
    errno = 0;
    DIR* dir = ::opendir(path);
    if (!dir) {
	if (!errno)
	    return true;
	return getLastError(error);
    }
    struct dirent* entry;
    while ((entry = ::readdir(dir)) != 0) {
	if (skipSpecial(entry->d_name))
	    continue;
#ifdef _DIRENT_HAVE_D_TYPE
	if (entry->d_type == DT_DIR) {
	    if (dirs)
		dirs->append(new String(entry->d_name));
	}
	else if (entry->d_type == DT_REG && files)
	    files->append(new String(entry->d_name));
#else
	struct stat stat_buf;
	String p;
	p << path << "/" << entry->d_name;
	if (::stat(p,&stat_buf))
	    break;
	if (S_ISDIR(stat_buf.st_mode)) {
	    if (dirs)
		dirs->append(new String(entry->d_name));
	}
	else if (S_ISREG(stat_buf.st_mode) && files)
	    files->append(new String(entry->d_name));
#endif // _DIRENT_HAVE_D_TYPE
    }
    ok = !errno;
    // Get error before closing DIR to avoid having a wrong one
    if (!ok && error)
	*error = errno;
    ::closedir(dir);
#endif // _WINDOWS
    return ok;
}


Socket::Socket()
    : m_handle(invalidHandle())
{
    DDebug(DebugAll,"Socket::Socket() [%p]",this);
}

Socket::Socket(SOCKET handle)
    : m_handle(handle)
{
    DDebug(DebugAll,"Socket::Socket(%d) [%p]",handle,this);
}

Socket::Socket(int domain, int type, int protocol)
    : m_handle(invalidHandle())
{
    DDebug(DebugAll,"Socket::Socket(%d,%d,%d) [%p]",domain,type,protocol,this);
    m_handle = ::socket(domain,type,protocol);
    if (!valid())
	copyError();
}

Socket::~Socket()
{
    DDebug(DebugAll,"Socket::~Socket() handle=%d [%p]",m_handle,this);
    clearFilters();
    terminate();
}

bool Socket::valid() const
{
    return (m_handle != invalidHandle());
}

bool Socket::create(int domain, int type, int protocol)
{
    DDebug(DebugAll,"Socket::create(%d,%d,%d) [%p]",domain,type,protocol,this);
    terminate();
    m_handle = ::socket(domain,type,protocol);
    if (valid()) {
	clearError();
	return true;
    }
    else {
	copyError();
	return false;
    }
}

bool Socket::terminate()
{
    bool ret = true;
    SOCKET tmp = m_handle;
    if (tmp != invalidHandle()) {
	DDebug(DebugAll,"Socket::terminate() handle=%d [%p]",m_handle,this);
	m_handle = invalidHandle();
#ifdef _WINDOWS
	ret = !::closesocket(tmp);
#else
	ret = !::close(tmp);
#endif
    }
    if (ret)
	clearError();
    else {
	copyError();
	// put back the handle, we may have another chance later
	m_handle = tmp;
    }
    return ret;
}

void Socket::attach(SOCKET handle)
{
    DDebug(DebugAll,"Socket::attach(%d) [%p]",handle,this);
    if (handle == m_handle)
	return;
    terminate();
    m_handle = handle;
    clearError();
}

SOCKET Socket::detach()
{
    DDebug(DebugAll,"Socket::detach() handle=%d [%p]",m_handle,this);
    SOCKET tmp = m_handle;
    m_handle = invalidHandle();
    clearError();
    return tmp;
}

SOCKET Socket::invalidHandle()
{
#ifdef _WINDOWS
    return INVALID_SOCKET;
#else
    return -1;
#endif
}

const TokenDict* Socket::tosValues()
{
    return s_tosValues;
}

int Socket::socketError()
{
#ifdef _WINDOWS
    return SOCKET_ERROR;
#else
    return -1;
#endif
}

void Socket::copyError()
{
#ifdef _WINDOWS
    m_error = WSAGetLastError();
#else
    m_error = errno;
#endif
}

bool Socket::checkError(int retcode, bool strict)
{
    if (strict && (retcode != socketError()))
	retcode = 0;
    if (retcode) {
	copyError();
	return false;
    }
    else {
	clearError();
	return true;
    }
}

bool Socket::canRetry() const
{
    if (!valid())
	return false;
    if (!m_error)
	return true;
    return (m_error == EAGAIN) || (m_error == EINTR) ||
#ifdef _WINDOWS
	(m_error == WSAEWOULDBLOCK);
#else
	(m_error == EWOULDBLOCK);
#endif
}

// Check if the last error code indicates a non blocking operation in progress
bool Socket::inProgress() const
{
    if (!valid())
	return false;
#ifdef _WINDOWS
    return m_error == WSAEWOULDBLOCK;
#else
    return m_error == EINPROGRESS;
#endif
}

bool Socket::canSelect() const
{
    return canSelect(m_handle);
}

bool Socket::bind(struct sockaddr* addr, socklen_t addrlen)
{
    return checkError(::bind(m_handle,addr,addrlen));
}

bool Socket::listen(unsigned int backlog)
{
    if ((backlog == 0) || (backlog > SOMAXCONN))
	backlog = SOMAXCONN;
    return checkError(::listen(m_handle,backlog));
}

Socket* Socket::accept(struct sockaddr* addr, socklen_t* addrlen)
{
    SOCKET sock = acceptHandle(addr,addrlen);
    return (sock == invalidHandle()) ? 0 : new Socket(sock);
}

Socket* Socket::accept(SocketAddr& addr)
{
    char buf[MAX_SOCKLEN];
    socklen_t len = sizeof(buf);
    Socket* sock = accept((struct sockaddr*)buf,&len);
    if (sock)
	addr.assign((struct sockaddr*)buf,len);
    return sock;
}

SOCKET Socket::acceptHandle(struct sockaddr* addr, socklen_t* addrlen)
{
    if (addrlen && !addr)
	*addrlen = 0;
    SOCKET res = ::accept(m_handle,addr,addrlen);
    if (res == invalidHandle())
	copyError();
    else
	clearError();
    return res;
}

// Update socket error from socket options
bool Socket::updateError()
{
    int error = 0;
    socklen_t len = sizeof(error);
    if (getOption(SOL_SOCKET,SO_ERROR,&error,&len)) {
	m_error = error;
	return true;
    }
    return false;
}

bool Socket::connect(struct sockaddr* addr, socklen_t addrlen)
{
    if (addrlen && !addr)
	addrlen = 0;
    return checkError(::connect(m_handle,addr,addrlen));
}

// Asynchronously connects the socket to a remote address
bool Socket::connectAsync(struct sockaddr* addr, socklen_t addrlen, unsigned int toutUs, bool* timeout)
{
    if (!canSelect())
	return false;
    if (connect(addr,addrlen))
	return true;
    if (!inProgress())
	return false;
    unsigned int intervals = toutUs / Thread::idleUsec();
    // Make sure we wait for at least 1 timeout interval
    if (!intervals)
	intervals = 1;
    clearError();
    while (intervals) {
	bool done = false;
	bool event = false;
	if (!select(0,&done,&event,Thread::idleUsec()))
	    return false;
	if (done || event) {
	    updateError();
	    return error() == 0;
	}
	if (Thread::check(false))
	    return false;
	intervals--;
    }
    if (timeout)
	*timeout = true;
    return false;
}

bool Socket::shutdown(bool stopReads, bool stopWrites)
{
    int how;
    if (stopReads) {
	if (stopWrites)
	    how = SHUT_RDWR;
	else
	    how = SHUT_RD;
    }
    else {
	if (stopWrites)
	    how = SHUT_WR;
	else
	    // nothing to do - no error
	    return true;
    }
    return checkError(::shutdown(m_handle,how));
}

bool Socket::getSockName(struct sockaddr* addr, socklen_t* addrlen)
{
    if (addrlen && !addr)
	*addrlen = 0;
    return checkError(::getsockname(m_handle,addr,addrlen));
}

bool Socket::getSockName(SocketAddr& addr)
{
    char buf[MAX_SOCKLEN];
    socklen_t len = sizeof(buf);
    bool ok = getSockName((struct sockaddr*)buf,&len);
    if (ok)
	addr.assign((struct sockaddr*)buf,len);
    return ok;
}

bool Socket::getPeerName(struct sockaddr* addr, socklen_t* addrlen)
{
    if (addrlen && !addr)
	*addrlen = 0;
    return checkError(::getpeername(m_handle,addr,addrlen));
}

bool Socket::getPeerName(SocketAddr& addr)
{
    char buf[MAX_SOCKLEN];
    socklen_t len = sizeof(buf);
    bool ok = getPeerName((struct sockaddr*)buf,&len);
    if (ok)
	addr.assign((struct sockaddr*)buf,len);
    return ok;
}

int Socket::sendTo(const void* buffer, int length, const struct sockaddr* addr, socklen_t adrlen, int flags)
{
    if (!addr)
	return send(buffer,length,flags);
    if (!buffer)
	length = 0;
    int res = ::sendto(m_handle,(const char*)buffer,length,flags,addr,adrlen);
    checkError(res,true);
    return res;
}

int Socket::send(const void* buffer, int length, int flags)
{
    if (!buffer)
	length = 0;
    int res = ::send(m_handle,(const char*)buffer,length,flags);
    checkError(res,true);
    return res;
}

int Socket::writeData(const void* buffer, int length)
{
#ifdef _WINDOWS
    return send(buffer,length);
#else
    if (!buffer)
	length = 0;
    int res = ::write(m_handle,buffer,length);
    checkError(res,true);
    return res;
#endif
}

int Socket::recvFrom(void* buffer, int length, struct sockaddr* addr, socklen_t* adrlen, int flags)
{
    if (!buffer)
	length = 0;
    if (adrlen && !addr)
	*adrlen = 0;
    int res = ::recvfrom(m_handle,(char*)buffer,length,flags,addr,adrlen);
    if (checkError(res,true) && applyFilters(buffer,res,flags,addr,(adrlen ? *adrlen : 0))) {
	m_error = EAGAIN;
	res = socketError();
    }
    return res;
}

int Socket::recvFrom(void* buffer, int length, SocketAddr& addr, int flags)
{
    char buf[MAX_SOCKLEN];
    socklen_t len = sizeof(buf);
    int res = recvFrom(buffer,length,(struct sockaddr*)buf,&len,flags);
    if (res != socketError())
	addr.assign((struct sockaddr*)buf,len);
    return res;
}

int Socket::recv(void* buffer, int length, int flags)
{
    if (!buffer)
	length = 0;
    int res = ::recv(m_handle,(char*)buffer,length,flags);
    if (checkError(res,true) && applyFilters(buffer,res,flags)) {
	m_error = EAGAIN;
	res = socketError();
    }
    return res;
}

int Socket::readData(void* buffer, int length)
{
#ifdef _WINDOWS
    return recv(buffer,length);
#else
    if (!buffer)
	length = 0;
    int res = ::read(m_handle,buffer,length);
    checkError(res,true);
    return res;
#endif
}

bool Socket::efficientSelect()
{
#if defined(_WINDOWS) || defined(HAVE_POLL)
    return true;
#else
    return false;
#endif
}

bool Socket::canSelect(SOCKET handle)
{
    if (handle == invalidHandle())
	return false;
#ifdef FD_SETSIZE
#ifndef _WINDOWS
#ifndef HAVE_POLL
    if (handle >= (SOCKET)FD_SETSIZE)
	return false;
#endif
#endif
#endif
    return true;
}

bool Socket::select(bool* readok, bool* writeok, bool* except, struct timeval* timeout)
{
    SOCKET tmp = m_handle;
    if (!valid())
	return false;

#ifdef HAVE_POLL
    struct pollfd fds;
    fds.fd = tmp;
    fds.events = 0;
    fds.revents = 0;
    if (readok)
	fds.events |= POLLIN;
    if (writeok)
	fds.events |= POLLOUT;
    if (except)
	fds.events |= POLLRDHUP;
    int tout = -1;
    if (timeout)
	tout = (timeout->tv_sec * 1000) + (timeout->tv_usec / 1000);
    if (checkError(::poll(&fds,1,tout),true)) {
	if (readok)
	    *readok = (fds.revents & POLLIN) != 0;
	if (writeok)
	    *writeok = (fds.revents & POLLOUT) != 0;
	if (except)
	    *except = (fds.revents & (POLLRDHUP|POLLERR|POLLHUP|POLLNVAL)) != 0;
	return true;
    }

#else // HAVE_POLL

#ifdef FD_SETSIZE
#ifndef _WINDOWS
    static bool localFail = true;
    if (tmp >= (SOCKET)FD_SETSIZE) {
	if (localFail) {
	    localFail = false;
	    Alarm("engine","socket",DebugGoOn,"Socket::select: handle %d larger than compiled in maximum %d",
		tmp,FD_SETSIZE);
	}
	return false;
    }
#endif
#endif
    fd_set readfd,writefd,exceptfd;
    fd_set *rfds = 0;
    fd_set *wfds = 0;
    fd_set *efds = 0;
    if (readok) {
	rfds = &readfd;
	FD_ZERO(rfds);
	FD_SET(tmp,rfds);
    }
    if (writeok) {
	wfds = &writefd;
	FD_ZERO(wfds);
	FD_SET(tmp,wfds);
    }
    if (except) {
	efds = &exceptfd;
	FD_ZERO(efds);
	FD_SET(tmp,efds);
    }
    if (checkError(::select(tmp+1,rfds,wfds,efds,timeout),true)) {
	if (readok)
	    *readok = (FD_ISSET(tmp,rfds) != 0);
	if (writeok)
	    *writeok = (FD_ISSET(tmp,wfds) != 0);
	if (except)
	    *except = (FD_ISSET(tmp,efds) != 0);
	return true;
    }
#endif // HAVE_POLL

    if (tmp != m_handle) {
	if (except)
	    *except = true;
	return true;
    }
    return false;
}

bool Socket::select(bool* readok, bool* writeok, bool* except, int64_t timeout)
{
    if (timeout < 0)
	return select(readok,writeok,except);
    struct timeval tm;
    Time::toTimeval(&tm,timeout);
    return select(readok,writeok,except,&tm);
}

bool Socket::setOption(int level, int name, const void* value, socklen_t length)
{
    if (!value)
	length = 0;
    return checkError(::setsockopt(m_handle,level,name,(const char*)value,length));
}

bool Socket::getOption(int level, int name, void* buffer, socklen_t* length)
{
    if (length && !buffer)
	*length = 0;
    return checkError(::getsockopt(m_handle,level,name,(char*)buffer,length));
}

bool Socket::setTOS(int tos)
{
#if defined(AF_INET6) && defined(IPV6_TCLASS)
    SocketAddr addr;
    if (getSockName(addr) && addr.family() == AF_INET6)
	return setOption(IPPROTO_IPV6,IPV6_TCLASS,&tos,sizeof(tos));
#endif
#ifdef IP_TOS
    return setOption(IPPROTO_IP,IP_TOS,&tos,sizeof(tos));
#else
    if (tos != Normal) {
	m_error = ENOTIMPL;
	return false;
    }
    else {
	m_error = 0;
	return true;
    }
#endif
}

int Socket::getTOS()
{
    int tos = Normal;
    socklen_t len = sizeof(tos);
#if defined(AF_INET6) && defined(IPV6_TCLASS)
    SocketAddr addr;
    if (getSockName(addr) && addr.family() == AF_INET6) {
	if (getOption(IPPROTO_IPV6,IPV6_TCLASS,&tos,&len))
	    return tos;
	tos = Normal;
	len = sizeof(tos);
    }
#endif
#ifdef IP_TOS
    getOption(IPPROTO_IP,IP_TOS,&tos,&len);
#else
    m_error = ENOTIMPL;
#endif
    return tos;
}

bool Socket::setBlocking(bool block)
{
    unsigned long flags = 1;
#ifdef _WINDOWS
    if (block)
	flags = 0;
    return checkError(::ioctlsocket(m_handle,FIONBIO,(unsigned long *) &flags));
#else
    flags = ::fcntl(m_handle,F_GETFL);
    if ((signed long)flags < 0) {
	copyError();
	return false;
    }
    if (block)
	flags &= !O_NONBLOCK;
    else
	flags |= O_NONBLOCK;
    return checkError(::fcntl(m_handle,F_SETFL,flags));
#endif
}

bool Socket::setReuse(bool reuse, bool exclusive)
{
    int i = reuse ? 1 : 0;
    if (!setOption(SOL_SOCKET,SO_REUSEADDR,&i,sizeof(i)))
	return false;
#ifdef SO_EXCLUSIVEADDRUSE
    i = exclusive ? 1 : 0;
    if (!setOption(SOL_SOCKET,SO_EXCLUSIVEADDRUSE,&i,sizeof(i)) && exclusive)
	return false;
#else
    if (exclusive) {
	Debug(DebugMild,"Socket SO_EXCLUSIVEADDRUSE not supported on this platform");
	return false;
    }
#endif
    return true;
}

bool Socket::setLinger(int seconds)
{
#ifdef SO_DONTLINGER
    if (seconds < 0) {
	int i = 1;
	return setOption(SOL_SOCKET,SO_DONTLINGER,&i,sizeof(i));
    }
#endif
    linger l;
    l.l_onoff = (seconds >= 0) ? 1 : 0;
    l.l_linger = (seconds >= 0) ? seconds : 0;
    return setOption(SOL_SOCKET,SO_LINGER,&l,sizeof(l));
}

bool Socket::createPair(Socket& sock1, Socket& sock2, int domain)
{
#ifndef _WINDOWS
    SOCKET pair[2];
    if (!::socketpair(domain,SOCK_STREAM,0,pair)) {
	sock1.attach(pair[0]);
	sock2.attach(pair[1]);
	return true;
    }
#endif
    return false;
}

bool Socket::installFilter(SocketFilter* filter)
{
    if (!filter || filter->socket())
	return false;
    if (m_filters.find(filter))
	return false;
    filter->m_socket = this;
    m_filters.append(filter);
    return true;
}

void Socket::removeFilter(SocketFilter* filter, bool delobj)
{
    if (m_filters.remove(filter,delobj))
	filter->m_socket = 0;
}

void Socket::clearFilters()
{
    m_filters.clear();
}

bool Socket::applyFilters(void* buffer, int length, int flags, const struct sockaddr* addr, socklen_t adrlen)
{
    if ((length <= 0) || !buffer)
	return false;
    if (!addr)
	adrlen = 0;
    for (ObjList* l = &m_filters; l; l = l->next()) {
	SocketFilter* filter = static_cast<SocketFilter*>(l->get());
	if (filter && filter->received(buffer,length,flags,addr,adrlen))
	    return true;
    }
    return false;
}

void Socket::timerTick(const Time& when)
{
    for (ObjList* l = &m_filters; l; l = l->next()) {
	SocketFilter* filter = static_cast<SocketFilter*>(l->get());
	if (filter)
	    filter->timerTick(when);
    }
}


SctpSocket::~SctpSocket()
{
}

/* vi: set ts=8 sw=4 sts=4 noet: */
