/**
 * libypri.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Common C++ base classes for PRI cards telephony drivers
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

#include <yatephone.h>

extern "C" {
#include <libpri.h>
}

namespace TelEngine {

class Fifo
{
public:
    Fifo(int buflen = 0);
    ~Fifo();
    void clear();
    void put(unsigned char value);
    unsigned char get();
private:
    int m_buflen;
    int m_head;
    int m_tail;
    unsigned char* m_buffer;
};

class PriChan;
class PriDriver;

class PriSpan : public GenObject, public Mutex
{
public:
    virtual ~PriSpan();
    inline struct pri *pri()
	{ return m_pri; }
    inline PriDriver* driver() const
	{ return m_driver; }
    inline int span() const
	{ return m_span; }
    inline bool belongs(int chan) const
	{ return (chan >= m_offs) && (chan < m_offs+m_nchans); }
    inline int chan1() const
	{ return m_offs; }
    inline int chans() const
	{ return m_nchans; }
    inline int bchans() const
	{ return m_bchans; }
    inline int dplan() const
	{ return m_dplan; }
    inline int pres() const
	{ return m_pres; }
    inline unsigned int overlapped() const
	{ return m_overlapped; }
    inline bool outOfOrder() const
	{ return !m_ok; }
    inline int buflen() const
	{ return m_buflen; }
    inline int layer1() const
	{ return m_layer1; }
    int findEmptyChan(int first = 0, int last = 65535) const;
    PriChan *getChan(int chan) const;
    void idle();

protected:
    PriSpan(struct pri *_pri, PriDriver* driver, int span, int first, int chans, int dchan, Configuration& cfg, const String& sect);
    void runEvent(bool idleRun);
    void handleEvent(pri_event &ev);
    bool validChan(int chan) const;
    void restartChan(int chan, bool outgoing, bool force = false);
    void ringChan(int chan, pri_event_ring &ev);
    void infoChan(int chan, pri_event_ring &ev);
    void hangupChan(int chan,pri_event_hangup &ev);
    void ackChan(int chan);
    void answerChan(int chan);
    void proceedingChan(int chan);
    PriDriver* m_driver;
    int m_span;
    int m_offs;
    int m_nchans;
    int m_bchans;
    int m_dplan;
    int m_pres;
    int m_buflen;
    int m_layer1;
    unsigned int m_overlapped;
    String m_callednumber;
    struct pri *m_pri;
    u_int64_t m_restart;
    u_int64_t m_restartPeriod;
    bool m_dumpEvents;
    PriChan **m_chans;
    bool m_ok;
};

class PriSource : public DataSource
{
public:
    PriSource(PriChan *owner, const char* format, unsigned int bufsize);
    virtual ~PriSource();

protected:
    PriChan *m_owner;
    DataBlock m_buffer;
};

class PriConsumer : public DataConsumer
{
public:
    PriConsumer(PriChan *owner, const char* format, unsigned int bufsize);
    virtual ~PriConsumer();

protected:
    PriChan *m_owner;
    DataBlock m_buffer;
};

class PriChan : public Channel
{
    friend class PriSource;
    friend class PriConsumer;
public:
    virtual ~PriChan();
    virtual void disconnected(bool final, const char *reason);
    virtual bool nativeConnect(DataEndpoint *peer);
    virtual bool msgRinging(Message& msg);
    virtual bool msgAnswered(Message& msg);
    virtual bool msgTone(Message& msg, const char* tone);
    virtual bool msgText(Message& msg, const char* text);
    virtual bool msgDrop(Message& msg, const char* reason);
    virtual void callAccept(Message& msg);
    virtual void callReject(const char* error, const char* reason = 0);
    inline PriSpan *span() const
	{ return m_span; }
    inline int chan() const
	{ return m_chan; }
    inline int absChan() const
	{ return m_abschan; }
    inline bool inUse() const
	{ return (m_ring || m_call); }
    void ring(pri_event_ring &ev);
    void hangup(int cause = 0);
    void sendDigit(char digit);
    void gotDigits(const char *digits);
    bool call(Message &msg, const char *called = 0);
    bool answer();
    void answered();
    void idle();
    void restart(bool outgoing = false);
    virtual bool openData(const char* format, int echoTaps = 0) = 0;
    virtual void closeData();
    virtual void goneUp();
    inline void setTimeout(u_int64_t tout)
	{ m_timeout = tout ? Time::now()+tout : 0; }
    const char *chanStatus() const;
    bool isISDN() const
	{ return m_isdn; }
protected:
    PriChan(const PriSpan *parent, int chan, unsigned int bufsize);
    PriSpan *m_span;
    int m_chan;
    bool m_ring;
    u_int64_t m_timeout;
    q931_call* m_call;
    unsigned int m_bufsize;
    int m_abschan;
    bool m_isdn;
};

class PriDriver : public Driver
{
    friend class PriSpan;
public:
    virtual ~PriDriver();
    virtual bool isBusy() const;
    virtual void dropAll();
    virtual bool msgExecute(Message& msg, String& dest);
    virtual void init(const char* configName);
    virtual PriSpan* createSpan(PriDriver* driver, int span, int first, int chans, Configuration& cfg, const String& sect) = 0;
    virtual PriChan* createChan(const PriSpan* span, int chan, unsigned int bufsize) = 0;
    static void netParams(Configuration& cfg, const String& sect, int chans, int* netType, int* swType, int* dChan);
    PriSpan *findSpan(int chan);
    PriChan *find(int first = -1, int last = -1);
    static inline u_int8_t bitswap(u_int8_t v)
	{ return s_bitswap[v]; }
protected:
    PriDriver(const char* name);
    void statusModule(String& str);
private:
    ObjList m_spans;
    static u_int8_t s_bitswap[256];
    static bool s_init;
};

}

/* vi: set ts=8 sw=4 sts=4 noet: */
