/**
 * Resolver.cpp
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

/*
 Preprocessor definitions:
 WINDOWS:
   HAVE_DNS_NAPTR_DATA: Define it if DNS_NAPTR_DATA is defined in windns.h
     If defined, NAPTR query will work at runtime on all supported versions
     If not defined NAPTR query will work only if Windows major version is less then 6
 Non Windows
   NO_RESOLV: Defined if the resolver is not available at compile time
   NO_DN_SKIPNAME: Define it if __dn_skipname() is not available at link time
*/

#include "yateclass.h"

#ifdef _WINDOWS
#include <windns.h>
#elif !defined(NO_RESOLV)
#include <resolv.h>
#include <arpa/nameser.h>
#endif // _WINDOWS

using namespace TelEngine;

// Resolver type names
const TokenDict Resolver::s_types[] = {
    { "SRV", Srv },
    { "NAPTR", Naptr },
    { "A", A4 },
    { "AAAA", A6 },
    { "TXT", Txt },
    { 0, 0 },
};

#ifdef _WINDOWS

class WindowsVersion
{
public:
    WindowsVersion();
    inline unsigned int major() const
	{ return m_major; }
private:
    unsigned int m_major;
};

WindowsVersion::WindowsVersion()
    : m_major(0)
{
    OSVERSIONINFOA ver;
    ::ZeroMemory(&ver,sizeof(OSVERSIONINFOA));
    ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    if (::GetVersionEx(&ver))
	m_major = ver.dwMajorVersion;
    if (!m_major)
	Debug(DebugWarn,"Resolver failed to detect Windows version");
}

static WindowsVersion s_winVer;

#endif

// Utility: print the result of dns query
// Return the code
static int printResult(int type, int code, const char* dname, ObjList& result, String* error)
{
#ifdef DEBUG
    if (!code) {
	String s;
	int crt = 0;
	for (ObjList* o = result.skipNull(); o; o = o->skipNext()) {
	    DnsRecord* rec = static_cast<DnsRecord*>(o->get());
	    if (!s)
		s << "\r\n-----";
	    s << "\r\n" << ++crt << ":";
	    rec->dump(s);
	}
	if (s)
	    s << "\r\n-----";
	Debug(DebugAll,"%s query for '%s' got %d records%s",
	    lookup(type,Resolver::s_types),dname,result.count(),s.safe());
    }
    else {
	String dummy;
	if (!error) {
	    error = &dummy;
#ifdef _WINDOWS
	    Thread::errorString(dummy,code);
#elif defined(__NAMESER)
	    dummy = hstrerror(code);
#endif
	}
	Debug(DebugNote,"%s query for '%s' failed with code %d error=%s",
	    lookup(type,Resolver::s_types),dname,code,TelEngine::c_safe(error));
    }
#endif
    return code;
}

// Utility: print a record and insert it in list
static bool insertRecord(ObjList& result, DnsRecord* rec, bool ascPref, const char* str)
{
#ifdef XDEBUG
    if (rec) {
	String s;
	rec->dump(s);
	Debug(DebugAll,"%s inserting %s",str,s.c_str());
    }
#endif
    return DnsRecord::insert(result,rec,ascPref);
}


/*
 * __dn_skipname() not available at link time
*/
#ifdef NO_DN_SKIPNAME

#define RESOLVER_NAME_COMPRESSED 0xc0      // Name compressed mask
#define RESOLVER_NAME_COMPRESSED_EXT 0x40  // RFC 2671 extended label type
#define RESOLVER_NAME_COMPRESSED_EXT_NEXT 0x41  // RFC 2671 extended label type with length in the next byte

#ifndef dn_skipname
#define dn_skipname __dn_skipname
#endif

// Retrieve the length of an extended compressed or uncompressed name
static int dn_namelen_ext(const unsigned char* buf)
{
    if (!buf)
	return -1;
    unsigned char val = *buf;
    unsigned char comp = (val & RESOLVER_NAME_COMPRESSED);
    // Compressed but not using extension: error
    if (comp == RESOLVER_NAME_COMPRESSED)
	return -1;
    // Not using extended compression
    if (comp != RESOLVER_NAME_COMPRESSED_EXT)
	return val;
    // Extended compression with length in the next byte
    if (val == RESOLVER_NAME_COMPRESSED_EXT_NEXT) {
	int nBits = buf[1];
	if (!nBits)
	    nBits = 256;
	return (nBits + 7) / 8 + 1;
    }
    DDebug(DebugNote,"dn_namelen_ext(%p) unknown extended compression %xd",buf,val);
    return -1;
}

extern "C" int __dn_skipname(const unsigned char* start, const unsigned char* end)
{
    XDebug(DebugNote,"__dn_skipname(%p,%p)",start,end);
    const unsigned char* buf = start;
    bool ok = true;
    while (buf < end) {
	unsigned char c = *buf++;
	if (!c)
	    break;
	unsigned char comp = (c & RESOLVER_NAME_COMPRESSED);
	// Not compressed
	if (!comp) {
	    buf += c;
	    continue;
	}
	// Compressed
	if (comp == RESOLVER_NAME_COMPRESSED) {
	    buf++;
	    break;
	}
	// Extended compression
	if (comp == RESOLVER_NAME_COMPRESSED_EXT) {
	    int len = dn_namelen_ext(buf - 1);
	    if (len >= 0) {
		buf += len;
		continue;
	    }
	    ok = false;
	    break;
	}
	DDebug(DebugNote,"__dn_skipname: unknown compression type %xd",comp);
	// Unknown compression
	ok = false;
	break;
    }
    if (!ok || buf > end) {
	h_errno = EMSGSIZE;
	return -1;
    }
    return buf - start;
}

#endif // NO_DN_SKIPNAME

// weird but NS_MAXSTRING and dn_string() are NOT part of the resolver API...
#ifndef NS_MAXSTRING
#define NS_MAXSTRING 255
#endif

// copy one string (not domain) from response
static int dn_string(const unsigned char* end, const unsigned char* src, char* dest, int maxlen)
{
    if (!src)
	return 0;
    int n = src[0];
    if (!dest)
	return n + 1;
    maxlen--;
    if (maxlen > n)
	maxlen = n;
    while ((maxlen-- > 0) && (src < end))
	*dest++ = *++src;
    *dest = 0;
    return n + 1;
}

// Dump a record for debug purposes
void DnsRecord::dump(String& buf, const char* sep)
{
    buf.append("ttl=",sep) << m_ttl;
    if (m_order >= 0)
	buf << sep << "order=" << m_order;
    if (m_pref >= 0)
	buf << sep << "pref=" << m_pref;
}

// Insert a record into a list in the proper location
// Order records ascending by their order
// Look at ascPref for records with the same order
bool DnsRecord::insert(ObjList& list, DnsRecord* rec, bool ascPref)
{
    if (!rec || list.find(rec))
	return false;
    XDebug(DebugAll,"DnsRecord::insert(%p) ttl=%d order=%d pref=%d",
	rec,rec->ttl(),rec->order(),rec->pref());
    ObjList* a = &list;
    ObjList* o = list.skipNull();
    for (; o; o = o->skipNext()) {
	a = o;
	DnsRecord* crt = static_cast<DnsRecord*>(o->get());
	if (rec->order() > crt->order())
	    continue;
	if (rec->order() == crt->order()) {
	    for (; o; o = o->skipNext()) {
		DnsRecord* crt = static_cast<DnsRecord*>(o->get());
		if (crt->order() != rec->order())
		    break;
		if (crt->pref() == rec->pref())
		    continue;
		if (ascPref == (rec->pref() < crt->pref()))
		    break;
	    }
	}
	break;
    }
    if (o)
	o->insert(rec);
    else
	a->append(rec);
    return true;
}


// Dump a record for debug purposes
void TxtRecord::dump(String& buf, const char* sep)
{
    DnsRecord::dump(buf,sep);
    buf.append("text=",sep) << "'" << m_text << "'";
}

// Copy a TxtRecord list into another one
void TxtRecord::copy(ObjList& dest, const ObjList& src)
{
    dest.clear();
    for (ObjList* o = src.skipNull(); o; o = o->skipNext()) {
	TxtRecord* rec = static_cast<TxtRecord*>(o->get());
	dest.append(new TxtRecord(rec->ttl(),rec->text()));
    }
}


// Dump a record for debug purposes
void SrvRecord::dump(String& buf, const char* sep)
{
    DnsRecord::dump(buf,sep);
    buf.append("address=",sep) << "'" << m_address << "'";
    buf << sep << "port=" << m_port;
}

// Copy a SrvRecord list into another one
void SrvRecord::copy(ObjList& dest, const ObjList& src)
{
    dest.clear();
    for (ObjList* o = src.skipNull(); o; o = o->skipNext()) {
	SrvRecord* rec = static_cast<SrvRecord*>(o->get());
	dest.append(new SrvRecord(rec->ttl(),rec->order(),rec->pref(),rec->address(),rec->port()));
    }
}


NaptrRecord::NaptrRecord(int ttl, int ord, int pref, const char* flags, const char* serv,
    const char* regexp, const char* next)
    : DnsRecord(ttl,ord,pref),
    m_flags(flags), m_service(serv), m_next(next)
{
    // use case-sensitive extended regular expressions
    m_regmatch.setFlags(true,false);
    if (!null(regexp)) {
	// look for <sep>regexp<sep>template<sep>
	char sep[2] = { regexp[0], 0 };
	String tmp(regexp+1);
	if (tmp.endsWith(sep)) {
	    int pos = tmp.find(sep);
	    if (pos > 0) {
		m_regmatch = tmp.substr(0,pos);
		m_template = tmp.substr(pos+1,tmp.length()-pos-2);
		XDebug(DebugAll,"NaptrRecord match '%s' template '%s'",
		    m_regmatch.c_str(),m_template.c_str());
	    }
	}
    }
}

// Perform the Regexp replacement, return true if succeeded
bool NaptrRecord::replace(String& str) const
{
    if (m_regmatch && str.matches(m_regmatch)) {
	str = str.replaceMatches(m_template);
	return true;
    }
    return false;
}

// Dump a record for debug purposes
void NaptrRecord::dump(String& buf, const char* sep)
{
    DnsRecord::dump(buf,sep);
    buf.append("flags=",sep) << "'" << m_flags << "'";
    buf << sep << "service=" << "'" << m_service << "'";
    buf << sep << "regmatch=" << "'" << m_regmatch << "'";
    buf << sep << "template=" << "'" << m_template << "'";
    buf << sep << "next=" << "'" << m_next << "'";
}


// Runtime check for resolver availability
bool Resolver::available(Type t)
{
    if (t == A6)
	return SocketAddr::IPv6 < SocketAddr::AfUnsupported;
#ifdef _WINDOWS
    if (t == Naptr) {
	if (!s_winVer.major())
	    return false;
#ifdef HAVE_DNS_NAPTR_DATA
	return true;
#else
	return s_winVer.major() < 6;
#endif
    }
    return true;
#elif defined(__NAMESER)
    return true;
#endif
    return false;
}

// Initialize the resolver in the current thread
bool Resolver::init(int timeout, int retries)
{
    if (!available())
	return false;
#ifdef _WINDOWS
    return true;
#elif defined(__NAMESER)
    if ((_res.options & RES_INIT) == 0) {
	// need to initialize in current thread
	if (res_init())
	    return false;
    }
    // always set variables
    if (timeout >= 0)
	_res.retrans = timeout;
    if (retries >= 0)
	_res.retry = retries;
    return true;
#endif
    return false;
}

// Make a query
int Resolver::query(Type type, const char* dname, ObjList& result, String* error)
{
    switch (type) {
	case Srv:
	    return srvQuery(dname,result,error);
	case Naptr:
	    return naptrQuery(dname,result,error);
	case A4:
	    return a4Query(dname,result,error);
	case A6:
	    return a6Query(dname,result,error);
	case Txt:
	    return txtQuery(dname,result,error);
	default:
	    Debug(DebugStub,"Resolver query not implemented for type %d",type);
    }
    return 0;
}

// Make a SRV query
int Resolver::srvQuery(const char* dname, ObjList& result, String* error)
{
    int code = 0;
    XDebug(DebugAll,"Starting %s query for '%s'",lookup(Srv,s_types),dname);
#ifdef _WINDOWS
    DNS_RECORD* srv = 0;
    code = (int)::DnsQuery_UTF8(dname,DNS_TYPE_SRV,DNS_QUERY_STANDARD,NULL,&srv,NULL);
    if (code == ERROR_SUCCESS) {
    	for (DNS_RECORD* dr = srv; dr; dr = dr->pNext) {
	    if (dr->wType != DNS_TYPE_SRV || dr->wDataLength != sizeof(DNS_SRV_DATA))
		continue;
	    DNS_SRV_DATA& d = dr->Data.SRV;
	    insertRecord(result,new SrvRecord(dr->dwTtl,d.wPriority,d.wWeight,
		d.pNameTarget,d.wPort),false,"srvQuery");
	}
    }
    else if (error)
	Thread::errorString(*error,code);
    if (srv)
	::DnsRecordListFree(srv,DnsFreeRecordList);
#elif defined(__NAMESER)
    unsigned char buf[512];
    int r = res_query(dname,ns_c_in,ns_t_srv,buf,sizeof(buf));
    if (r <= 0 || r > (int)sizeof(buf)) {
	if (r) {
	    code = h_errno;
	    if (error)
		*error = hstrerror(code);
	}
	return printResult(Srv,code,dname,result,error);
    }
    int queryCount = 0;
    int answerCount = 0;
    unsigned char* p = buf + NS_QFIXEDSZ;
    unsigned char* e = buf + r;
    NS_GET16(queryCount,p);
    NS_GET16(answerCount,p);
    p = buf + NS_HFIXEDSZ;
    // Skip queries
    for (; queryCount > 0; queryCount--) {
	int n = dn_skipname(p,e);
	if (n < 0)
	    break;
	p += (n + NS_QFIXEDSZ);
    }
    for (int i = 0; i < answerCount; i++) {
	char name[NS_MAXLABEL + 1];
	// Skip this answer's query
	int n = dn_expand(buf,e,p,name,sizeof(name));
	if ((n <= 0) || (n > NS_MAXLABEL))
	    break;
	buf[n] = 0;
	p += n;
	// Get record type, class, ttl, length
	int rrType, rrClass, rrTtl, rrLen;
	NS_GET16(rrType,p);
	NS_GET16(rrClass,p);
	NS_GET32(rrTtl,p);
	NS_GET16(rrLen,p);
	// Remember current pointer and skip to next answer
	unsigned char* l = p;
	p += rrLen;
	// Skip non SRV answers
	if (rrType != (int)ns_t_srv)
	    continue;
	// Now get record priority, weight, port, label
	int prio, weight, port;
	NS_GET16(prio,l);
	NS_GET16(weight,l);
	NS_GET16(port,l);
	n = dn_expand(buf,e,l,name,sizeof(name));
	if ((n <= 0) || (n > NS_MAXLABEL))
	    break;
	insertRecord(result,new SrvRecord(rrTtl,prio,weight,name,port),false,"srvQuery");
	YIGNORE(rrClass);
    }
#endif
    return printResult(Srv,code,dname,result,error);
}

// Make a NAPTR query
int Resolver::naptrQuery(const char* dname, ObjList& result, String* error)
{
    int code = 0;
    XDebug(DebugAll,"Starting %s query for '%s'",lookup(Naptr,s_types),dname);
#ifdef _WINDOWS
    DNS_RECORD* naptr = 0;
    if (available(Naptr))
	code = (int)::DnsQuery_UTF8(dname,DNS_TYPE_NAPTR,DNS_QUERY_STANDARD,NULL,&naptr,NULL);
    if (code == ERROR_SUCCESS) {
    	for (DNS_RECORD* dr = naptr; dr; dr = dr->pNext) {
	    if (dr->wType != DNS_TYPE_NAPTR)
		continue;
	    // version >= 6: DnsQuery will set the fields in DNS_NAPTR_DATA
	    // else: DnsQuery puts the raw response
	    if (s_winVer.major() >= 6) {
#ifdef HAVE_DNS_NAPTR_DATA
		if (dr->wDataLength != sizeof(DNS_NAPTR_DATA))
		    continue;
		DNS_NAPTR_DATA& d = dr->Data.NAPTR;
		insertRecord(result,new NaptrRecord(dr->dwTtl,d.wOrder,d.wPreference,d.pFlags,
		    d.pService,d.pRegularExpression,d.pReplacement),true,"naptrQuery");
#endif
		continue;
	    }
	    int len = dr->wDataLength - 4;
	    if (len <= 0)
		continue;
	    unsigned char* b = (unsigned char*)&dr->Data;
	    int ord = (b[0] << 8) | b[1];
	    int pr = (b[2] << 8) | b[3];
	    unsigned char* tmp = new unsigned char[len + 1];
	    ::memcpy(tmp,b + 4,len);
	    tmp[len] = 0;
	    unsigned char* buf = tmp;
	    unsigned char* end = buf + len;
	    if (!buf)
		continue;
	    char fla[DNS_MAX_NAME_BUFFER_LENGTH];
	    char ser[DNS_MAX_NAME_BUFFER_LENGTH];
	    char reg[DNS_MAX_NAME_BUFFER_LENGTH];
	    buf += dn_string(end,buf,fla,sizeof(fla));;
	    buf += dn_string(end,buf,ser,sizeof(ser));
	    buf += dn_string(end,buf,reg,sizeof(reg));
	    insertRecord(result,new NaptrRecord(dr->dwTtl,ord,pr,fla,ser,reg,0),true,"naptrQuery");
	}
    }
    else if (error)
	Thread::errorString(*error,code);
    if (naptr)
	::DnsRecordListFree(naptr,DnsFreeRecordList);
#elif defined(__NAMESER)
    unsigned char buf[2048];
    int r,q,a;
    unsigned char *p, *e;
    r = res_query(dname,ns_c_in,ns_t_naptr,buf,sizeof(buf));
    if ((r < 0) || (r > (int)sizeof(buf))) {
	code = h_errno;
	if (error)
	    *error = hstrerror(code);
	return printResult(Naptr,code,dname,result,error);
    }
    p = buf+NS_QFIXEDSZ;
    NS_GET16(q,p);
    NS_GET16(a,p);
    XDebug(DebugAll,"Resolver::naptrQuery(%s) questions: %d, answers: %d",dname,q,a);
    p = buf + NS_HFIXEDSZ;
    e = buf + r;
    for (; q > 0; q--) {
	int n = dn_skipname(p,e);
	if (n < 0)
	    return printResult(Naptr,code,dname,result,error);
	p += (n + NS_QFIXEDSZ);
    }
    XDebug(DebugAll,"Resolver::naptrQuery(%s) skipped questions",dname);
    for (; a > 0; a--) {
	int ty,cl,sz;
	long int tt;
	char name[NS_MAXLABEL+1];
	unsigned char* l;
	int n = dn_expand(buf,e,p,name,sizeof(name));
	if ((n <= 0) || (n > NS_MAXLABEL))
	    break;
	buf[n] = 0;
	p += n;
	NS_GET16(ty,p);
	NS_GET16(cl,p);
	NS_GET32(tt,p);
	NS_GET16(sz,p);
	XDebug(DebugAll,"Resolver::naptrQuery(%s) found '%s' type %d size %d",dname,name,ty,sz);
	l = p;
	p += sz;
	if (ty != ns_t_naptr)
	    continue;
	int ord,pr;
	char fla[NS_MAXSTRING+1];
	char ser[NS_MAXSTRING+1];
	char reg[NS_MAXSTRING+1];
	char rep[NS_MAXLABEL+1];
	NS_GET16(ord,l);
	NS_GET16(pr,l);
	n = dn_string(e,l,fla,sizeof(fla));
	l += n;
	n = dn_string(e,l,ser,sizeof(ser));
	l += n;
	n = dn_string(e,l,reg,sizeof(reg));
	l += n;
	n = dn_expand(buf,e,l,rep,sizeof(rep));
	l += n;
	insertRecord(result,new NaptrRecord(tt,ord,pr,fla,ser,reg,rep),true,"naptrQuery");
	YIGNORE(cl);
    }
#endif
    return printResult(Naptr,code,dname,result,error);
}

// Make an A query
int Resolver::a4Query(const char* dname, ObjList& result, String* error)
{
    int code = 0;
    XDebug(DebugAll,"Starting %s query for '%s'",lookup(A4,s_types),dname);
#ifdef _WINDOWS
    DNS_RECORD* adr = 0;
    code = (int)::DnsQuery_UTF8(dname,DNS_TYPE_A,DNS_QUERY_STANDARD,NULL,&adr,NULL);
    if (code == ERROR_SUCCESS) {
    	for (DNS_RECORD* dr = adr; dr; dr = dr->pNext) {
	    if (dr->wType != DNS_TYPE_A || dr->wDataLength != sizeof(DNS_A_DATA))
		continue;
	    SocketAddr addr(SocketAddr::IPv4,&dr->Data.A.IpAddress);
	    result.append(new TxtRecord(dr->dwTtl,addr.host()));
	}
    }
    else if (error)
	Thread::errorString(*error,code);
    if (adr)
	::DnsRecordListFree(adr,DnsFreeRecordList);
#elif defined(__NAMESER)
    unsigned char buf[512];
    int r = res_query(dname,ns_c_in,ns_t_a,buf,sizeof(buf));
    if (r <= 0 || r > (int)sizeof(buf)) {
	if (r) {
	    code = h_errno;
	    if (error)
		*error = hstrerror(code);
	}
	return printResult(A4,code,dname,result,error);
    }
    int queryCount = 0;
    int answerCount = 0;
    unsigned char* p = buf + NS_QFIXEDSZ;
    unsigned char* e = buf + r;
    NS_GET16(queryCount,p);
    NS_GET16(answerCount,p);
    p = buf + NS_HFIXEDSZ;
    // Skip queries
    for (; queryCount > 0; queryCount--) {
	int n = dn_skipname(p,e);
	if (n < 0)
	    break;
	p += (n + NS_QFIXEDSZ);
    }
    for (int i = 0; i < answerCount; i++) {
	char name[NS_MAXLABEL + 1];
	// Skip this answer's query
	int n = dn_expand(buf,e,p,name,sizeof(name));
	if ((n <= 0) || (n > NS_MAXLABEL))
	    break;
	buf[n] = 0;
	p += n;
	// Get record type, class, ttl, length
	int rrType, rrClass, rrTtl, rrLen;
	NS_GET16(rrType,p);
	NS_GET16(rrClass,p);
	NS_GET32(rrTtl,p);
	NS_GET16(rrLen,p);
	// Remember current pointer and skip to next answer
	unsigned char* l = p;
	p += rrLen;
	// Skip non A answers
	if (rrType != (int)ns_t_a)
	    continue;
	// Now get record address
	SocketAddr addr(SocketAddr::IPv4,l);
	result.append(new TxtRecord(rrTtl,addr.host()));
	YIGNORE(rrClass);
    }
#endif
    return printResult(A4,code,dname,result,error);
}

// Make an AAAA query
int Resolver::a6Query(const char* dname, ObjList& result, String* error)
{
    int code = 0;
    XDebug(DebugAll,"Starting %s query for '%s'",lookup(A6,s_types),dname);
    if (!available(A6))
	return printResult(A6,code,dname,result,error);
#ifdef _WINDOWS
    DNS_RECORD* adr = 0;
    code = (int)::DnsQuery_UTF8(dname,DNS_TYPE_AAAA,DNS_QUERY_STANDARD,NULL,&adr,NULL);
    if (code == ERROR_SUCCESS) {
    	for (DNS_RECORD* dr = adr; dr; dr = dr->pNext) {
	    if (dr->wType != DNS_TYPE_AAAA || dr->wDataLength != sizeof(DNS_AAAA_DATA))
		continue;
	    SocketAddr addr(SocketAddr::IPv6,&dr->Data.AAAA.Ip6Address);
	    result.append(new TxtRecord(dr->dwTtl,addr.host()));
	}
    }
    else if (error)
	Thread::errorString(*error,code);
    if (adr)
	::DnsRecordListFree(adr,DnsFreeRecordList);
#elif defined(__NAMESER)
    unsigned char buf[512];
    int r = res_query(dname,ns_c_in,ns_t_aaaa,buf,sizeof(buf));
    if (r <= 0 || r > (int)sizeof(buf)) {
	if (r) {
	    code = h_errno;
	    if (error)
		*error = hstrerror(code);
	}
	return printResult(A6,code,dname,result,error);
    }
    int queryCount = 0;
    int answerCount = 0;
    unsigned char* p = buf + NS_QFIXEDSZ;
    unsigned char* e = buf + r;
    NS_GET16(queryCount,p);
    NS_GET16(answerCount,p);
    p = buf + NS_HFIXEDSZ;
    // Skip queries
    for (; queryCount > 0; queryCount--) {
	int n = dn_skipname(p,e);
	if (n < 0)
	    break;
	p += (n + NS_QFIXEDSZ);
    }
    for (int i = 0; i < answerCount; i++) {
	char name[NS_MAXLABEL + 1];
	// Skip this answer's query
	int n = dn_expand(buf,e,p,name,sizeof(name));
	if ((n <= 0) || (n > NS_MAXLABEL))
	    break;
	buf[n] = 0;
	p += n;
	// Get record type, class, ttl, length
	int rrType, rrClass, rrTtl, rrLen;
	NS_GET16(rrType,p);
	NS_GET16(rrClass,p);
	NS_GET32(rrTtl,p);
	NS_GET16(rrLen,p);
	// Remember current pointer and skip to next answer
	unsigned char* l = p;
	p += rrLen;
	// Skip non AAAA answers
	if (rrType != (int)ns_t_aaaa)
	    continue;
	// Now get record address
	SocketAddr addr(SocketAddr::IPv6,l);
	result.append(new TxtRecord(rrTtl,addr.host()));
	YIGNORE(rrClass);
    }
#endif
    return printResult(A6,code,dname,result,error);
}

// Make a TXT query
int Resolver::txtQuery(const char* dname, ObjList& result, String* error)
{
    int code = 0;
    XDebug(DebugAll,"Starting %s query for '%s'",lookup(Txt,s_types),dname);
#ifdef _WINDOWS
    DNS_RECORD* adr = 0;
    code = (int)::DnsQuery_UTF8(dname,DNS_TYPE_TEXT,DNS_QUERY_STANDARD,NULL,&adr,NULL);
    if (code == ERROR_SUCCESS) {
    	for (DNS_RECORD* dr = adr; dr; dr = dr->pNext) {
	    if (dr->wType != DNS_TYPE_TEXT || dr->wDataLength < sizeof(DNS_TXT_DATA))
		continue;
	    DNS_TXT_DATA& d = dr->Data.TXT;
	    for (DWORD i = 0; i < d.dwStringCount; i++)
		result.append(new TxtRecord(dr->dwTtl,d.pStringArray[i]));
	}
    }
    else if (error)
	Thread::errorString(*error,code);
    if (adr)
	::DnsRecordListFree(adr,DnsFreeRecordList);
#elif defined(__NAMESER)
    unsigned char buf[512];
    int r = res_query(dname,ns_c_in,ns_t_txt,buf,sizeof(buf));
    if (r <= 0 || r > (int)sizeof(buf)) {
	if (r) {
	    code = h_errno;
	    if (error)
		*error = hstrerror(code);
	}
	return printResult(Txt,code,dname,result,error);
    }
    int queryCount = 0;
    int answerCount = 0;
    unsigned char* p = buf + NS_QFIXEDSZ;
    unsigned char* e = buf + r;
    NS_GET16(queryCount,p);
    NS_GET16(answerCount,p);
    p = buf + NS_HFIXEDSZ;
    // Skip queries
    for (; queryCount > 0; queryCount--) {
	int n = dn_skipname(p,e);
	if (n < 0)
	    break;
	p += (n + NS_QFIXEDSZ);
    }
    for (int i = 0; i < answerCount; i++) {
	char name[NS_MAXLABEL + 1];
	// Skip this answer's query
	int n = dn_expand(buf,e,p,name,sizeof(name));
	if ((n <= 0) || (n > NS_MAXLABEL))
	    break;
	buf[n] = 0;
	p += n;
	// Get record type, class, ttl, length
	int rrType, rrClass, rrTtl, rrLen;
	NS_GET16(rrType,p);
	NS_GET16(rrClass,p);
	NS_GET32(rrTtl,p);
	NS_GET16(rrLen,p);
	// Remember current pointer and skip to next answer
	unsigned char* l = p;
	p += rrLen;
	// Skip non TXT answers
	if (rrType != (int)ns_t_txt)
	    continue;
	// Now get record address
	char txt[NS_MAXSTRING+1];
	dn_string(e,l,txt,sizeof(txt));
	result.append(new TxtRecord(rrTtl,txt));
	YIGNORE(rrClass);
    }
#endif
    return printResult(Txt,code,dname,result,error);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
