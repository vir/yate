#ifndef __YATEICE_H
#define __YATEICE_H

#include <yateclass.h>

#ifdef _WINDOWS

#ifdef LIBYXML_EXPORTS
#define YICE_API __declspec(dllexport)
#else
#ifndef LIBYXML_STATIC
#define YICE_API __declspec(dllimport)
#endif
#endif

#endif /* _WINDOWS */

#ifndef YICE_API
#define YICE_API
#endif

/**
 * Holds all Telephony Engine related classes.
 */
namespace TelEngine {

class IceRtpCandidate;                    // A RTP transport candidate
class IceRtpCandidates;                   // A list of RTP transport candidates

/**
 * This class holds a RTP transport candidate
 * @short A RTP transport candidate
 */
class YICE_API IceRtpCandidate : public String
{
public:
    /**
     * Constructor
     */
    inline IceRtpCandidate(const char* id, const char* component = "1",
	    unsigned int generation = 0, unsigned int net = 0, int prio = 0)
	: String(id),
	m_port(0), m_component(component), m_generation(generation),
	m_network(net), m_priority(prio), m_protocol("udp"), m_type("host")
    {}

    /**
     * Constructor. Build a candidate from received SDP data
     * @param str Received SDP attribute
     * @param container The transport container
     */
    inline IceRtpCandidate(const String& str, const IceRtpCandidates& container)
	{ fromSDPAttribute(str,container); }

    /**
     * Create a 'candidate' SDP attribute rom this object using local address/port
     * @param container The transport container
     * @return SDP attribute text
     */
    virtual String toSDPAttribute(const IceRtpCandidates& container) const;

    /**
     * Fill this object from a candidate SDP addtribute using remote address/port
     * @param str Received SDP attribute
     * @param container The transport container
     */
    virtual void fromSDPAttribute(const String& str, const IceRtpCandidates& container);

    /**
     * Utility function needed for debug: dump a candidate to a string
     * @param buf String buffer
     * @param sep Parameters separator character
     */
    virtual void dump(String& buf, char sep = ' ');

    /**
     * Update candidate's foundation and priority fields
     */
    virtual void Update();

    String m_address;
    String m_port;
    String m_component;                  // Candidate component
    String m_generation;                 // Candidate generation
    String m_network;                    // NIC card (diagnostic only)
    String m_priority;                   // Candidate priority
    String m_protocol;                   // The only allowable value is "udp"
    String m_type;                       // A Candidate Type as defined in ICE-CORE
};

class YICE_API IceRtpCandidates : public ObjList
{
public:
    /**
     * Fill password and ufrag data
     */
    inline void generateIceAuth() {
	    generateIceToken(m_password,true);
	    generateIceToken(m_ufrag,false);
	}

    /**
     * Fill password and ufrag data using old transport restrictions (16 bytes length)
     */
    inline void generateOldIceAuth() {
	    generateOldIceToken(m_password);
	    generateOldIceToken(m_ufrag);
	}

    /**
     * Find a candidate by its component value
     * @param component The value to search
     * @return IceRtpCandidate pointer or 0
     */
    IceRtpCandidate* findByComponent(unsigned int component);
    const IceRtpCandidate* findByComponent(unsigned int component) const;

    /**
     * Generate a random password or username to be used with ICE-UDP transport
     * @param dest Destination string
     * @param pwd True to generate a password, false to generate an username (ufrag)
     * @param max Maximum number of characters. The maxmimum value is 256.
     *  The minimum value is 22 for password and 4 for username
     */
    static void generateIceToken(String& dest, bool pwd, unsigned int max = 0);

    /**
     * Generate a random password or username to be used with old ICE-UDP transport
     * @param dest Destination string
     */
    static void generateOldIceToken(String& dest);

    virtual String toSDPAttribute(bool password) const;

    String m_password;
    String m_ufrag;
};

}; /* namespace TelEngine */

#endif /* __YATEICE_H */

/* vi: set ts=8 sw=4 sts=4 noet: */



