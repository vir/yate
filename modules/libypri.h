/**
 * libypri.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Common C++ base classes for PRI cards telephony drivers
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

#include <yatephone.h>

namespace TelEngine {

#define bitswap(v) bitswap_table[v]

static unsigned char bitswap_table[256];

static void bitswap_init()
{
    for (unsigned int c = 0; c <= 255; c++) {
	unsigned char v = 0;
	for (int b = 0; b <= 7; b++)
	    if (c & (1 << b))
		v |= (0x80 >> b);
	bitswap_table[c] = v;
    }
}


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

class PriSpan : public GenObject, public Thread
{
    friend class WpData;
public:
    static PriSpan *create(int span, int chan1, int nChans, int dChan, int netType,
			   int switchType, int dialPlan, int presentation,
			   int overlapDial, int nsf = YATE_NSF_DEFAULT);
    virtual ~PriSpan();
    virtual void run();
    inline struct pri *pri()
	{ return m_pri; }
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
    int findEmptyChan(int first = 0, int last = 65535) const;
    WpChan *getChan(int chan) const;
    void idle();
    static u_int64_t restartPeriod;
    static bool dumpEvents;

private:
    PriSpan(struct pri *_pri, int span, int first, int chans, int dchan, int fd, int dplan, int pres, int overlapDial);
    static struct pri *makePri(int fd, int dchan, int nettype, int swtype, int overlapDial, int nsf);
    void handleEvent(pri_event &ev);
    bool validChan(int chan) const;
    void restartChan(int chan, bool outgoing, bool force = false);
    void ringChan(int chan, pri_event_ring &ev);
    void infoChan(int chan, pri_event_ring &ev);
    void hangupChan(int chan,pri_event_hangup &ev);
    void ackChan(int chan);
    void answerChan(int chan);
    void proceedingChan(int chan);
    int m_span;
    int m_offs;
    int m_nchans;
    int m_bchans;
    int m_fd;
    int m_dplan;
    int m_pres;
    unsigned int m_overlapped;
    struct pri *m_pri;
    u_int64_t m_restart;
    WpChan **m_chans;
    WpData *m_data;
    bool m_ok;
};

class WpSource : public DataSource
{
public:
    WpSource(WpChan *owner,unsigned int bufsize,const char* format);
    ~WpSource();
    void put(unsigned char val);

private:
    WpChan *m_owner;
    unsigned int m_bufpos;
    DataBlock m_buf;
};

class WpConsumer : public DataConsumer, public Fifo
{
public:
    WpConsumer(WpChan *owner,unsigned int bufsize,const char* format);
    ~WpConsumer();

    virtual void Consume(const DataBlock &data, unsigned long timeDelta);

private:
    WpChan *m_owner;
    DataBlock m_buffer;
};

class PriChan : public DataEndpoint
{
    friend class WpSource;
    friend class WpConsumer;
    friend class WpData;
public:
    WpChan(PriSpan *parent, int chan, unsigned int bufsize);
    virtual ~WpChan();
    virtual void disconnected(bool final, const char *reason);
    virtual bool nativeConnect(DataEndpoint *peer);
    inline PriSpan *span() const
	{ return m_span; }
    inline int chan() const
	{ return m_chan; }
    inline int absChan() const
	{ return m_abschan; }
    inline bool inUse() const
	{ return (m_ring || m_call); }
    void ring(q931_call *call = 0);
    void hangup(int cause = PRI_CAUSE_INVALID_MSG_UNSPECIFIED);
    void sendDigit(char digit);
    void gotDigits(const char *digits);
    bool call(Message &msg, const char *called = 0);
    bool answer();
    void answered();
    void idle();
    void restart(bool outgoing = false);
    bool openData(const char* format);
    void closeData();
    inline void setTimeout(u_int64_t tout)
	{ m_timeout = tout ? Time::now()+tout : 0; }
    const char *status() const;
    const String& id() const
	{ return m_id; }
    bool isISDN() const
	{ return m_isdn; }
    inline void setTarget(const char *target = 0)
	{ m_targetid = target; }
    inline const String& getTarget() const
	{ return m_targetid; }
private:
    PriSpan *m_span;
    int m_chan;
    bool m_ring;
    u_int64_t m_timeout;
    q931_call *m_call;
    unsigned int m_bufsize;
    int m_abschan;
    bool m_isdn;
    String m_id;
    String m_targetid;
    WpSource* m_wp_s;
    WpConsumer* m_wp_c;
};

class WpData : public Thread
{
public:
    WpData(PriSpan* span);
    ~WpData();
    virtual void run();
private:
    PriSpan* m_span;
    int m_fd;
    unsigned char* m_buffer;
    WpChan **m_chans;
};

class WpHandler : public MessageHandler
{
public:
    WpHandler() : MessageHandler("call.execute") { }
    virtual bool received(Message &msg);
};

class WpDropper : public MessageHandler
{
public:
    WpDropper() : MessageHandler("call.drop") { }
    virtual bool received(Message &msg);
};

class StatusHandler : public MessageHandler
{
public:
    StatusHandler() : MessageHandler("engine.status") { }
    virtual bool received(Message &msg);
};

class WpChanHandler : public MessageReceiver
{
public:
    enum {
	Ringing,
	Answered,
	DTMF,
    };
    virtual bool received(Message &msg, int id);
};

class WanpipePlugin : public Plugin
{
    friend class PriSpan;
    friend class WpHandler;
public:
    WanpipePlugin();
    virtual ~WanpipePlugin();
    virtual void initialize();
    virtual bool isBusy() const;
    PriSpan *findSpan(int chan);
    WpChan *findChan(const char *id);
    WpChan *findChan(int first = -1, int last = -1);
    ObjList m_spans;
    Mutex mutex;
};

}

/* vi: set ts=8 sw=4 sts=4 noet: */
