/**
 * frame.cpp
 * Yet Another IAX2 Stack
 * This file is part of the YATE Project http://YATE.null.ro 
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

#include <yateiax.h>

using namespace TelEngine;


IAXFrame* IAXFrame::parse(const unsigned char* buf, unsigned int len, IAXEngine* engine, const SocketAddr* addr)
{
    if (len < 4)
	return 0;
    u_int16_t scn = (buf[0] << 8) | buf[1];
    u_int16_t dcn = (buf[2] << 8) | buf[3];
    if (scn & 0x8000) {
	// full frame
	if (len < 12)
	    return 0;
	scn &= 0x7fff;
	bool retrans = false;
	if (dcn & 0x8000) {
	    retrans = true;
	    dcn &= 0x7fff;
	}
	u_int32_t sc = buf[11];
	if (sc & 0x80)
	    sc = 1 << (sc & 0x7f);
	u_int32_t ts = (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];
	return new IAXFullFrame((IAXFrame::Type)buf[10],sc,scn,dcn,buf[8],buf[9],ts,retrans,buf+12,len-12);
    }
    if (scn == 0) {
	// meta frame
	if (dcn & 0x8000) {
	    // meta video
	    if (len < 6)
		return 0;
	    scn = (buf[4] << 8) | buf[5];
	    bool retrans = false;
	    if (scn & 0x8000) {
		retrans = true;
		scn &= 0x7fff;
	    }
	    return new IAXFrame(IAXFrame::Video,dcn & 0x7fff,scn,retrans,buf+6,len-6);
	}
	// meta trunk frame - we need to push chunks into the engine
	if (!(engine && addr))
	    return 0;
	if (len < 8)
	    return 0;
	// "meta command" should be 1
	if (buf[2] != 1)
	    return 0;
	bool tstamps = (buf[3] & 1) != 0;
	u_int32_t ts = (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];
	buf += 8;
	len -= 8;
	if (tstamps) {
	    // trunk timestamps
	    while (len >= 4) {
		u_int16_t dlen = (buf[2] << 8) | buf[3];
		if ((dlen + 4) > len)
		    return 0;
		scn = (buf[0] << 8) | buf[1];
		bool retrans = false;
		if (scn & 0x8000) {
		    retrans = true;
		    scn &= 0x7fff;
		}
		IAXFrame* frame = new IAXFrame(IAXFrame::Voice,scn,0,retrans,buf+4,dlen);
		engine->addFrame(*addr,frame);
		frame->deref();
		dlen += 4;
		buf += dlen;
		len -= dlen;
	    }
	}
	else {
	    // no trunk timestamps
	    while (len >= 6) {
		u_int16_t dlen = (buf[0] << 8) | buf[1];
		if ((dlen + 6) > len)
		    return 0;
		scn = (buf[2] << 8) | buf[3];
		bool retrans = false;
		if (scn & 0x8000) {
		    retrans = true;
		    scn &= 0x7fff;
		}
		dcn = (buf[4] << 8) | buf[5];
		IAXFrame* frame = new IAXFrame(IAXFrame::Voice,scn,dcn,retrans,buf+6,dlen);
		engine->addFrame(*addr,frame);
		frame->deref();
		dlen += 6;
		buf += dlen;
		len -= dlen;
	    }
	}
	return 0;
    }
    return new IAXFrame(IAXFrame::Voice,scn,dcn,false,buf+4,len-4);
}

const IAXFullFrame* IAXFrame::fullFrame() const
{
    return 0;
}

const IAXFullFrame* IAXFullFrame::fullFrame() const
{
    return this;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
