#include "yateice.h"

using namespace TelEngine;


/*
 * IceRtpCandidate
 */
// Create a 'a=candidate:...' line from this object
String IceRtpCandidate::toSDPAttribute(const IceRtpCandidates& container) const
{
}

// Fill this object from a candidate SDP addtribute
void IceRtpCandidate::fromSDPAttribute(const String& str, const IceRtpCandidates& container)
{
}

/*
 * IceRtpCandidates
 */

// Find a candidate by its component value
IceRtpCandidate* IceRtpCandidates::findByComponent(unsigned int component)
{
    String tmp(component);
    for (ObjList* o = skipNull(); o; o = o->skipNext()) {
	IceRtpCandidate* c = static_cast<IceRtpCandidate*>(o->get());
	if (c->m_component == tmp)
	    return c;
    }
    return 0;
}

// Generate a random password or username to be used with ICE-UDP transport
void IceRtpCandidates::generateIceToken(String& dest, bool pwd, unsigned int max)
{
    if (pwd) {
	if (max < 22)
	    max = 22;
    }
    else if (max < 4)
	max = 4;
    if (max > 256)
	max = 256;
    dest = "";
    while (dest.length() < max)
	dest << (int)Random::random();
    dest = dest.substr(0,max);
}

// Generate a random password or username to be used with old ICE-UDP transport
void IceRtpCandidates::generateOldIceToken(String& dest)
{
    dest = "";
    while (dest.length() < 16)
	dest << (int)Random::random();
    dest = dest.substr(0,16);
}


/* vi: set ts=8 sw=4 sts=4 noet: */

