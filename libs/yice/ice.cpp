#include "yateice.h"

using namespace TelEngine;


/*
 * IceRtpCandidate
 */
// Create a 'a=candidate:...' line from this object
String IceRtpCandidate::toSDPAttribute(const IceRtpCandidates& container) const
{
    String s = "candidate:";
    s << m_generation << " " << m_component << " "; // foundation
    s << m_protocol << " "; // transport
    s << m_priority << " ";
    s << m_address << " ";
    s << m_port;
    s << " typ " << m_type;
#if 0
    s << " " << m_rel_addr;
    s << " " << m_rel_port;
#endif
    return s;
}

// Fill this object from a candidate SDP addtribute
void IceRtpCandidate::fromSDPAttribute(const String& str, const IceRtpCandidates& container)
{
    String s(str);
    s.extractTo(" ", m_generation); // foundation
    s.extractTo(" ", m_component);
    s.extractTo(" ", m_protocol); // transport
    s.extractTo(" ", m_priority);
    s.extractTo(" ", m_address);
    s.extractTo(" ", m_port);
    s.extractTo(" ", m_type);
}

// Utility function needed for debug: dump a candidate to a string
void IceRtpCandidate::dump(String& buf, char sep)
{
    buf << "name=" << *this;
    buf << sep << "addr=" << m_address;
    buf << sep << "port=" << m_port;
    buf << sep << "component=" << m_component;
    buf << sep << "generation=" << m_generation;
    buf << sep << "network=" << m_network;
    buf << sep << "priority=" << m_priority;
    buf << sep << "protocol=" << m_protocol;
    buf << sep << "type=" << m_type;
}

// Update candidate's foundation and priority fields
void IceRtpCandidate::Update()
{
    // Simple implementation for ICE-Lite (See rfc5245 section 4.2)
    m_generation = m_address.hash();
    const unsigned int IP_precedence = 65535;
    m_priority = (126 << 24) + (IP_precedence << 8) + (256 - m_component.toInteger());
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
const IceRtpCandidate* IceRtpCandidates::findByComponent(unsigned int component) const
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

String IceRtpCandidates::toSDPAttribute(bool password) const
{
    if(password)
	return YSTRING("ice-pwd:") + m_password;
    else
	return YSTRING("ice-ufrag:") + m_ufrag;

}

/* vi: set ts=8 sw=4 sts=4 noet: */

