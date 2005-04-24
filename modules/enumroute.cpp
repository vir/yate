/**
 * enumroute.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * ENUM routing module
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
#error Not yet ready!

#include <yatengine.h>

#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>

#include <stdio.h>

class NAPTR : public GenObject
{
private:
    int m_order;
    int m_pref;
    String m_flags;
    String m_service;
    Regexp m_regexp;
    String m_replace;
}

unsigned char buf[2048];

// copy one string (not domain) from response
static int dn_string(const unsigned char* end, const unsigned char* src, char *dest, int maxlen)
{
    int n = src[0];
    maxlen--;
    if (maxlen > n)
	maxlen = n;
    if (dest && (maxlen > 0)) {
	while ((maxlen-- > 0) && (src < end))
	    *dest++ = *++src;
	*dest = 0;
    }
    return n+1;
}

int main()
{
    int r,i,q,a;
    unsigned char *p, *e;
    r = res_init();
    printf("res_init %d\n",r);
    r = res_query("0.0.1.2.4.0.0.0.9.9.2.8.8.e164.org",ns_c_in,ns_t_naptr,
	buf,sizeof(buf));
    printf("res_query %d\n",r);
    if ((r < 0) || (r > sizeof(buf)))
	return 1;
    p = buf+NS_QFIXEDSZ;
    NS_GET16(q,p);
    NS_GET16(a,p);
    printf("questions: %d, answers: %d\n",q,a);
    p = buf + NS_HFIXEDSZ;
    e = buf + r;
    for (; q > 0; q--) {
	int n = dn_skipname(p,e);
	if (n < 0)
	    return 1;
	p += (n + NS_QFIXEDSZ);
    }
    printf("skipped questions\n");
    for (; a > 0; a--) {
	int ty,cl,sz;
	long int tt;
	char name[NS_MAXLABEL+1];
	unsigned char* l;
	int n = dn_expand(buf,e,p,name,sizeof(name));
	if ((n <= 0) || (n > NS_MAXLABEL))
	    return 1;
	buf[n] = 0;
	p += n;
	NS_GET16(ty,p);
	NS_GET16(cl,p);
	NS_GET32(tt,p);
	NS_GET16(sz,p);
	printf("found '%s' type %d size %d\n",name,ty,sz);
	l = p;
	p += sz;
	if (ty == ns_t_naptr) {
	    int or,pr;
	    char fla[NS_MAXLABEL+1];
	    char ser[NS_MAXLABEL+1];
	    char reg[NS_MAXLABEL+1];
	    char rep[NS_MAXLABEL+1];
	    NS_GET16(or,l);
	    NS_GET16(pr,l);
	    n = dn_string(e,l,fla,sizeof(fla));
	    l += n;
	    n = dn_string(e,l,ser,sizeof(ser));
	    l += n;
	    n = dn_string(e,l,reg,sizeof(reg));
	    l += n;
	    n = dn_expand(buf,e,l,rep,sizeof(rep));
	    l += n;
	    printf("order=%d pref=%d flags='%s' serv='%s' regexp='%s' replace='%s'\n",
		or,pr,fla,ser,reg,rep);
	}
    }
    return 0;
}

INIT_PLUGIN(EnumPlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
