/*
 * libiax: An implementation of Inter-Asterisk eXchange
 *
 * Copyright (C) 2001, Linux Support Services, Inc.
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU Lesser (Library) General Public License
 */
 
#ifdef	WIN32

#include <string.h>
#include <process.h>
#include <windows.h>
#include <winsock.h>
#include <time.h>
#include <stdlib.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <io.h>
#include <errno.h>
#include <winpoop.h>

#else

#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#ifndef MACOSX
#include <malloc.h>
#include <error.h>
#endif

#endif


#include "frame.h" 
#include "iax2.h"
#include "iax2-parser.h"
#include "iax-client.h"
#include "md5.h"

/* Define socket options for IAX2 sockets, based on platform
 * availability of flags */
#ifdef WIN32
#define IAX_SOCKOPTS 0
#else
#ifdef MACOSX
#define IAX_SOCKOPTS MSG_DONTWAIT
#else  /* Linux and others */
#define IAX_SOCKOPTS MSG_DONTWAIT | MSG_NOSIGNAL
#endif
#endif


#ifdef SNOM_HACK
/* The snom phone seems to improperly execute memset in some cases */
#include "../../snom_phonecore2/include/snom_memset.h"
#endif

#define IAX_EVENT_REREQUEST	999
#define IAX_EVENT_TXREPLY	1000
#define IAX_EVENT_TXREJECT	1001
#define IAX_EVENT_TXACCEPT  1002
#define IAX_EVENT_TXREADY	1003

/* Define Voice Smoothing to try to make some judgements and adjust timestamps
   on incoming packets to what they "ought to be" */

#define VOICE_SMOOTHING
#undef VOICE_SMOOTHING

/* Define Drop Whole Frames to make IAX shrink its jitter buffer by dropping entire
   frames rather than simply delivering them faster.  Dropping encoded frames, 
   before they're decoded, usually leads to better results than dropping 
   decoded frames. */

#define DROP_WHOLE_FRAMES

#define MIN_RETRY_TIME 10
#define MAX_RETRY_TIME 10000

#define TRANSFER_NONE  0
#define TRANSFER_BEGIN 1
#define TRANSFER_READY 2

/* No more than 4 seconds of jitter buffer */
static int max_jitterbuffer = 4000;
/* No more than 50 extra milliseconds of jitterbuffer than needed */
static int max_extra_jitterbuffer = 50;
/* To use or not to use the jitterbuffer */
static int iax_use_jitterbuffer = 0;

/* UDP Socket (file descriptor) */
static int netfd = -1;

/* Max timeouts */
static int maxretries = 10;

/* Dropcount (in per-MEMORY_SIZE) usually percent */
static int iax_dropcount = 3;

#if 0
struct iax_session {
	/* Private data */
	void *pvt;
	/* Sendto function */
	sendto_t sendto;
	/* Is voice quelched (e.g. hold) */
	int quelch;
	/* Last received voice format */
	int voiceformat;
	/* Last transmitted voice format */
	int svoiceformat;
	/* Last received timestamp */
	unsigned int last_ts;
	/* Last transmitted timestamp */
	unsigned int lastsent;
	/* Last transmitted voice timestamp */
	unsigned int lastvoicets;
	/* Our last measured ping time */
	unsigned int pingtime;
	/* Address of peer */
	struct sockaddr_in peeraddr;
	/* Our call number */
	int callno;
	/* Peer's call number */
	int peercallno;
	/* Our next outgoing sequence number */
	unsigned char oseqno;
	/* Next sequence number they have not yet acknowledged */
	unsigned char rseqno;
	/* Our last received incoming sequence number */
	unsigned char iseqno;
	/* Last acknowledged sequence number */
	unsigned char aseqno;
	/* Peer supported formats */
	int peerformats;
	/* Time value that we base our transmission on */
	struct timeval offset;
	/* Time value we base our delivery on */
	struct timeval rxcore;
	/* History of lags */
	int history[MEMORY_SIZE];
	/* Current base jitterbuffer */
	int jitterbuffer;
	/* Informational jitter */
	int jitter;
	/* Measured lag */
	int lag;
	/* Current link state */
	int state;
	/* Peer name */
	char peer[MAXSTRLEN];
	/* Default Context */
	char context[MAXSTRLEN];
	/* Caller ID if available */
	char callerid[MAXSTRLEN];
	/* DNID */
	char dnid[MAXSTRLEN];
	/* Requested Extension */
	char exten[MAXSTRLEN];
	/* Expected Username */
	char username[MAXSTRLEN];
	/* Expected Secret */
	char secret[MAXSTRLEN];
	/* permitted authentication methods */
	char methods[MAXSTRLEN];
	/* MD5 challenge */
	char challenge[12];
#ifdef VOICE_SMOOTHING
	unsigned int lastts;
#endif
	/* Refresh if applicable */
	int refresh;
	
	/* Transfer stuff */
	struct sockaddr_in transfer;
	int transferring;
	int transfercallno;
	int transferid;
	
	/* For linking if there are multiple connections */
	struct iax_session *next;
};
extern struct iax_session;
#endif
#ifdef	WIN32

void gettimeofday(struct timeval *tv, struct timezone *tz);

#define	snprintf _snprintf

#endif

char iax_errstr[256];

static int sformats = 0;

#define IAXERROR snprintf(iax_errstr, sizeof(iax_errstr), 

#ifdef DEBUG_SUPPORT

#ifdef DEBUG_DEFAULT
static int debug = 1;
#else
static int debug = 0;
#endif

void iax_enable_debug(void)
{
	debug = 1;
}

void iax_disable_debug(void)
{
	debug = 0;
}

void iax_set_private(struct iax_session *s, void *ptr)
{
	s->pvt = ptr;
}

void *iax_get_private(struct iax_session *s)
{
	return s->pvt;
}

void iax_set_sendto(struct iax_session *s, sendto_t ptr)
{
	s->sendto = ptr;
}

/* This is a little strange, but to debug you call DEBU(G "Hello World!\n"); */ 
#ifdef	WIN32
#define G __FILE__, __LINE__,
#else
#define G __FILE__, __LINE__, __PRETTY_FUNCTION__, 
#endif

#define DEBU __debug 
#ifdef	WIN32
static int __debug(char *file, int lineno, char *fmt, ...) 
{
	va_list args;
	va_start(args, fmt);
	if (debug) {
		fprintf(stderr, "%s line %d: ", file, lineno);
		vfprintf(stderr, fmt, args);
	}
	va_end(args);
	return 0;
}
#else
static int __debug(char *file, int lineno, char *func, char *fmt, ...) 
{
	va_list args;
	va_start(args, fmt);
	if (debug) {
		fprintf(stderr, "%s line %d in %s: ", file, lineno, func);
		vfprintf(stderr, fmt, args);
	}
	va_end(args);
	return 0;
}
#endif
#else /* No debug support */

#ifdef	WIN32
#define	DEBU
#else
#define DEBU(...)
#endif
#define G
#endif

struct iax_sched {
	/* These are scheduled things to be delivered */
	struct timeval when;
	/* If event is non-NULL then we're delivering an event */
	struct iax_event *event;
	/* If frame is non-NULL then we're transmitting a frame */
	struct iax_frame *frame;
	/* Easy linking */
	struct iax_sched *next;
};

#ifdef	WIN32

void bzero(void *b, size_t len)
{
	memset(b,0,len);
}

#endif

static struct iax_sched *schedq = NULL;
static struct iax_session *sessions = NULL;
static int callnums = 1;

static int inaddrcmp(struct sockaddr_in *sin1, struct sockaddr_in *sin2)
{
	return (sin1->sin_addr.s_addr != sin2->sin_addr.s_addr) || (sin1->sin_port != sin2->sin_port);
}

static int iax_sched_event(struct iax_event *event, struct iax_frame *frame, int ms)
{

	/* Schedule event to be delivered to the client
	   in ms milliseconds from now, or a reliable frame to be retransmitted */
	struct iax_sched *sched, *cur, *prev = NULL;
	
	if (!event && !frame) {
		DEBU(G "No event, no frame?  what are we scheduling?\n");
		return -1;
	}
	

	sched = (struct iax_sched*)malloc(sizeof(struct iax_sched));
	if (sched) {
		bzero(sched, sizeof(struct iax_sched));
		gettimeofday(&sched->when, NULL);
		sched->when.tv_sec += (ms / 1000);
		ms = ms % 1000;
		sched->when.tv_usec += (ms * 1000);
		if (sched->when.tv_usec > 1000000) {
			sched->when.tv_usec -= 1000000;
			sched->when.tv_sec++;
		}
		sched->event = event;
		sched->frame = frame;
		/* Put it in the list, in order */
		cur = schedq;
		while(cur && ((cur->when.tv_sec < sched->when.tv_sec) || 
					 ((cur->when.tv_usec <= sched->when.tv_usec) &&
					  (cur->when.tv_sec == sched->when.tv_sec)))) {
				prev = cur;
				cur = cur->next;
		}
		sched->next = cur;
		if (prev) {
			prev->next = sched;
		} else {
			schedq = sched;
		}
		return 0;
	} else {
		DEBU(G "Out of memory!\n");
		return -1;
	}
}

int iax_time_to_next_event(void)
{
	struct timeval tv;
	struct iax_sched *cur = schedq;
	int ms, min = 999999999;
	
	/* If there are no pending events, we don't need to timeout */
	if (!cur)
		return -1;
	gettimeofday(&tv, NULL);
	while(cur) {
		ms = (cur->when.tv_sec - tv.tv_sec) * 1000 +
		     (cur->when.tv_usec - tv.tv_usec) / 1000;
		if (ms < min)
			min = ms;
		cur = cur->next;
	}
	if (min < 0)
		min = 0;
	return min;
}

struct iax_session *iax_session_new(void)
{
	struct iax_session *s;
	s = (struct iax_session *)malloc(sizeof(struct iax_session));
	if (s) {
		memset(s, 0, sizeof(struct iax_session));
		/* Initialize important fields */
		s->voiceformat = -1;
		s->svoiceformat = -1;
		/* Default pingtime to 30 ms */
		s->pingtime = 30;
		/* XXX Not quite right -- make sure it's not in use, but that won't matter
	           unless you've had at least 65k calls.  XXX */
		s->callno = callnums++;
		if (callnums > 32767)
			callnums = 1;
		s->peercallno = 0;
		s->next = sessions;
		s->sendto = sendto;
		sessions = s;
	}
	return s;
}

static int iax_session_valid(struct iax_session *session)
{
	/* Return -1 on a valid iax session pointer, 0 on a failure */
	struct iax_session *cur = sessions;
	while(cur) {
		if (session == cur)
			return -1;
		cur = cur->next;
	}
	return 0;
}

static int calc_timestamp(struct iax_session *session, unsigned int ts)
{
	int ms;
	struct timeval tv;
	
	/* If this is the first packet we're sending, get our
	   offset now. */
	if (!session->offset.tv_sec && !session->offset.tv_usec)
		gettimeofday(&session->offset, NULL);

	/* If the timestamp is specified, just use their specified
	   timestamp no matter what.  Usually this is done for
	   special cases.  */
	if (ts)
		return ts;
	
	/* Otherwise calculate the timestamp from the current time */
	gettimeofday(&tv, NULL);
		
	/* Calculate the number of milliseconds since we sent the first packet */
	ms = (tv.tv_sec - session->offset.tv_sec) * 1000 +
		 (tv.tv_usec - session->offset.tv_usec) / 1000;

	/* Never send a packet with the same timestamp since timestamps can be used
	   to acknowledge certain packets */
    	if ((unsigned) ms <= session->lastsent)
		ms = session->lastsent + 1;

	/* Record the last sent packet for future reference */
	session->lastsent = ms;

	return ms;
}

static int iax_xmit_frame(struct iax_frame *f)
{
	struct ast_iax2_full_hdr *h = (f->data);
	/* Send the frame raw */
#ifdef DEBUG_SUPPORT
	if (ntohs(h->scallno) & IAX_FLAG_FULL)
		iax_showframe(f, NULL, 0, f->transfer ? 
						&(f->session->transfer) :
					&(f->session->peeraddr), f->datalen - sizeof(struct ast_iax2_full_hdr));
#endif

	return f->session->sendto(netfd, (const char *) f->data, f->datalen,
		IAX_SOCKOPTS,
					f->transfer ? 
						(struct sockaddr *)&(f->session->transfer) :
					(struct sockaddr *)&(f->session->peeraddr), sizeof(f->session->peeraddr));
}

static int iax_reliable_xmit(struct iax_frame *f)
{
	struct iax_frame *fc;
	struct ast_iax2_full_hdr *fh;
	fh = (struct ast_iax2_full_hdr *) f->data;
	if (!fh->type) {
		DEBU(G "Asked to reliably transmit a non-packet.  Crashing.\n");
		*((char *)0)=0;
	}
	fc = (struct iax_frame *)malloc(sizeof(struct iax_frame));
	if (fc) {
		/* Make a copy of the frame */
		memcpy(fc, f, sizeof(struct iax_frame));
		/* And a copy of the data if applicable */
		if (!fc->data || !fc->datalen) {
			IAXERROR "No frame data?");
			DEBU(G "No frame data?\n");
			return -1;
		} else {
			fc->data = (char *)malloc(fc->datalen);
			if (!fc->data) {
				DEBU(G "Out of memory\n");
				IAXERROR "Out of memory\n");
				return -1;
			}
			memcpy(fc->data, f->data, f->datalen);
			iax_sched_event(NULL, fc, fc->retrytime);
			return iax_xmit_frame(fc);
		}
	} else
		return -1;
}

int iax_init(int preferredportno)
{
	int portno = preferredportno;
	struct sockaddr_in sin;
	int sinlen;
	int flags;
	
	if (netfd > -1) {
		/* Sokay, just don't do anything */
		DEBU(G "Already initialized.");
		return 0;
	}
	netfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (netfd < 0) {
		DEBU(G "Unable to allocate UDP socket\n");
		IAXERROR "Unable to allocate UDP socket\n");
		return -1;
	}
	
	if (preferredportno == 0) 
		preferredportno = IAX_DEFAULT_PORTNO;
		
	if (preferredportno > 0) {
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = 0;
		sin.sin_port = htons((short)preferredportno);
		if (bind(netfd, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
			DEBU(G "Unable to bind to preferred port.  Using random one instead.");
		}
	}
	sinlen = sizeof(sin);
	if (getsockname(netfd, (struct sockaddr *) &sin, &sinlen) < 0) {
		close(netfd);
		netfd = -1;
		DEBU(G "Unable to figure out what I'm bound to.");
		IAXERROR "Unable to determine bound port number.");
	}
#ifdef	WIN32
	flags = 1;
	if (ioctlsocket(netfd,FIONBIO,(unsigned long *) &flags)) {
		_close(netfd);
		netfd = -1;
		DEBU(G "Unable to set non-blocking mode.");
		IAXERROR "Unable to set non-blocking mode.");
	}
	
#else
	if ((flags = fcntl(netfd, F_GETFL)) < 0) {
		close(netfd);
		netfd = -1;
		DEBU(G "Unable to retrieve socket flags.");
		IAXERROR "Unable to retrieve socket flags.");
	}
	if (fcntl(netfd, F_SETFL, flags | O_NONBLOCK) < 0) {
		close(netfd);
		netfd = -1;
		DEBU(G "Unable to set non-blocking mode.");
		IAXERROR "Unable to set non-blocking mode.");
	}
#endif
	portno = ntohs(sin.sin_port);
	srand(time(NULL));
	callnums = rand() % 32767 + 1;
	DEBU(G "Started on port %d\n", portno);
	return portno;	
}

static void destroy_session(struct iax_session *session);

static void convert_reply(char *out, unsigned char *in)
{
	int x;
	for (x=0;x<16;x++)
		out += sprintf(out, "%2.2x", (int)in[x]);
}

static unsigned char compress_subclass(int subclass)
{
	int x;
	int power=-1;
	/* If it's 128 or smaller, just return it */
	if (subclass < IAX_FLAG_SC_LOG)
		return subclass;
	/* Otherwise find its power */
	for (x = 0; x < IAX_MAX_SHIFT; x++) {
		if (subclass & (1 << x)) {
			if (power > -1) {
				DEBU(G "Can't compress subclass %d\n", subclass);
				return 0;
			} else
				power = x;
		}
	}
	return power | IAX_FLAG_SC_LOG;
}

int iax_send(struct iax_session *pvt, struct ast_frame *f, unsigned int ts, int seqno, int now, int transfer, int final) 
{
	/* Queue a packet for delivery on a given private structure.  Use "ts" for
	   timestamp, or calculate if ts is 0.  Send immediately without retransmission
	   or delayed, with retransmission */
	struct ast_iax2_full_hdr *fh;
	struct ast_iax2_mini_hdr *mh;
	struct {
		struct iax_frame fr2;
		unsigned char buffer[4096];		/* Buffer -- must preceed fr2 */
	} buf;
	struct iax_frame *fr;
	int res;
	int sendmini=0;
	unsigned int lastsent;
	unsigned int fts;
	
	/* Shut up GCC */
	buf.buffer[0] = 0;
	
	if (!pvt) {
		IAXERROR "No private structure for packet?\n");
		return -1;
	}
	
	/* Calculate actual timestamp */
	lastsent = pvt->lastsent;
	fts = calc_timestamp(pvt, ts);

	if (((fts & 0xFFFF0000L) == (lastsent & 0xFFFF0000L))
		/* High two bits are the same on timestamp, or sending on a trunk */ &&
	    (f->frametype == AST_FRAME_VOICE) 
		/* is a voice frame */ &&
		(f->subclass == pvt->svoiceformat) 
		/* is the same type */ ) {
			/* Force immediate rather than delayed transmission */
			now = 1;
			/* Mark that mini-style frame is appropriate */
			sendmini = 1;
	}
	/* Allocate an iax_frame */
	if (now) {
		fr = &buf.fr2;
	} else
		fr = iax_frame_new(DIRECTION_OUTGRESS, f->datalen);
	if (!fr) {
		IAXERROR "Out of memory\n");
		return -1;
	}
	/* Copy our prospective frame into our immediate or retransmitted wrapper */
	iax_frame_wrap(fr, f);

	fr->ts = fts;
	if (!fr->ts) {
		IAXERROR "timestamp is 0?\n");
		if (!now)
			iax_frame_free(fr);
		return -1;
	}
	fr->callno = pvt->callno;
	fr->transfer = transfer;
	fr->final = final;
	fr->session = pvt;
	if (!sendmini) {
		/* We need a full frame */
		if (seqno > -1)
			fr->oseqno = seqno;
		else
			fr->oseqno = pvt->oseqno++;
		fr->iseqno = pvt->iseqno;
		fh = (struct ast_iax2_full_hdr *)(((char *)fr->af.data) - sizeof(struct ast_iax2_full_hdr));
		fh->scallno = htons(fr->callno | IAX_FLAG_FULL);
		fh->ts = htonl(fr->ts);
		fh->oseqno = fr->oseqno;
		if (transfer) {
			fh->iseqno = 0;
		} else
			fh->iseqno = fr->iseqno;
		/* Keep track of the last thing we've acknowledged */
		pvt->aseqno = fr->iseqno;
		fh->type = fr->af.frametype & 0xFF;
		fh->csub = compress_subclass(fr->af.subclass);
		if (transfer) {
			fr->dcallno = pvt->transfercallno;
		} else
			fr->dcallno = pvt->peercallno;
		fh->dcallno = htons(fr->dcallno);
		fr->datalen = fr->af.datalen + sizeof(struct ast_iax2_full_hdr);
		fr->data = fh;
		fr->retries = maxretries;
		/* Retry after 2x the ping time has passed */
		fr->retrytime = pvt->pingtime * 2;
		if (fr->retrytime < MIN_RETRY_TIME)
			fr->retrytime = MIN_RETRY_TIME;
		if (fr->retrytime > MAX_RETRY_TIME)
			fr->retrytime = MAX_RETRY_TIME;
		/* Acks' don't get retried */
		if ((f->frametype == AST_FRAME_IAX) && (f->subclass == IAX_COMMAND_ACK))
			fr->retries = -1;
		if (f->frametype == AST_FRAME_VOICE) {
			pvt->svoiceformat = f->subclass;
		}
		if (now) {
			res = iax_xmit_frame(fr);
		} else
			res = iax_reliable_xmit(fr);
	} else {
		/* Mini-frames have no sequence number */
		fr->oseqno = -1;
		fr->iseqno = -1;
		/* Mini frame will do */
		mh = (struct ast_iax2_mini_hdr *)(((char *)fr->af.data) - sizeof(struct ast_iax2_mini_hdr));
		mh->callno = htons(fr->callno);
		mh->ts = htons(fr->ts & 0xFFFF);
		fr->datalen = fr->af.datalen + sizeof(struct ast_iax2_mini_hdr);
		fr->data = mh;
		fr->retries = -1;
		res = iax_xmit_frame(fr);
	}
	return res;
}

#if 0
static int iax_predestroy(struct iax_session *pvt)
{
	if (!pvt) {
		return -1;
	}
	if (!pvt->alreadygone) {
		/* No more pings or lagrq's */
		if (pvt->pingid > -1)
			ast_sched_del(sched, pvt->pingid);
		if (pvt->lagid > -1)
			ast_sched_del(sched, pvt->lagid);
		if (pvt->autoid > -1)
			ast_sched_del(sched, pvt->autoid);
		if (pvt->initid > -1)
			ast_sched_del(sched, pvt->initid);
		pvt->pingid = -1;
		pvt->lagid = -1;
		pvt->autoid = -1;
		pvt->initid = -1;
		pvt->alreadygone = 1;
	}
	return 0;
}
#endif

static int __send_command(struct iax_session *i, char type, int command, unsigned int ts, char *data, int datalen, int seqno, 
		int now, int transfer, int final)
{
	struct ast_frame f;
	f.frametype = type;
	f.subclass = command;
	f.datalen = datalen;
	f.samples = 0;
	f.mallocd = 0;
	f.offset = 0;
#ifdef __GNUC__
	f.src = __FUNCTION__;
#else
	f.src = __FILE__;
#endif
	f.data = data;
	return iax_send(i, &f, ts, seqno, now, transfer, final);
}

static int send_command(struct iax_session *i, char type, int command, unsigned int ts, char *data, int datalen, int seqno)
{
	return __send_command(i, type, command, ts, data, datalen, seqno, 0, 0, 0);
}

static int send_command_final(struct iax_session *i, char type, int command, unsigned int ts, char *data, int datalen, int seqno)
{
#if 0
	/* It is assumed that the callno has already been locked */
	iax_predestroy(i);
#endif	
	return __send_command(i, type, command, ts, data, datalen, seqno, 0, 0, 1);
}

static int send_command_immediate(struct iax_session *i, char type, int command, unsigned int ts, char *data, int datalen, int seqno)
{
	return __send_command(i, type, command, ts, data, datalen, seqno, 1, 0, 0);
}

static int send_command_transfer(struct iax_session *i, char type, int command, unsigned int ts, char *data, int datalen)
{
	return __send_command(i, type, command, ts, data, datalen, 0, 0, 1, 0);
}

int iax_transfer(struct iax_session *session, char *number)
{	
	static int res;				//Return Code
	struct iax_ie_data ied;			//IE Data Structure (Stuff To Send)

	// Clear The Memory Used For IE Buffer
	memset(&ied, 0, sizeof(ied));
	
	// Copy The Transfer Destination Into The IE Structure
	iax_ie_append_str(&ied, IAX_IE_CALLED_NUMBER, number);
	
	// Send The Transfer Command - Asterisk Will Handle The Rest!			
	res = send_command(session, AST_FRAME_IAX, IAX_COMMAND_TRANSFER, 0, ied.buf, ied.pos, -1);
	
	// Return Success
	return 0;	
}

int try_transfer(struct iax_session *session, struct iax_ies *ies)
{
	int newcall = 0;
	char newip[256] = "";
	struct iax_ie_data ied;
	struct sockaddr_in new;
	
	memset(&ied, 0, sizeof(ied));
	if (ies->apparent_addr)
		memcpy(&new, ies->apparent_addr, sizeof(new));
	if (ies->callno)
		newcall = ies->callno;
	if (!newcall || !new.sin_addr.s_addr || !new.sin_port) {
		return -1;
	}
	session->transfercallno = newcall;
	memcpy(&session->transfer, &new, sizeof(session->transfer));
	inet_aton(newip, &session->transfer.sin_addr);
	session->transfer.sin_family = AF_INET;
	session->transferring = TRANSFER_BEGIN;
	session->transferid = ies->transferid;
	if (ies->transferid)
		iax_ie_append_int(&ied, IAX_IE_TRANSFERID, ies->transferid);
	send_command_transfer(session, AST_FRAME_IAX, IAX_COMMAND_TXCNT, 0, ied.buf, ied.pos);
	return 0; 
}



static void destroy_session(struct iax_session *session)
{
	struct iax_session *cur, *prev=NULL;
	struct iax_sched *curs, *prevs=NULL, *nexts=NULL;
	int    loop_cnt=0;
	curs = schedq;
	while(curs) {
		nexts = curs->next;
		if (curs->frame && curs->frame->session == session) {
			/* Just mark these frames as if they've been sent */
			curs->frame->retries = -1;
		} else if (curs->event && curs->event->session == session) {
			if (prevs)
				prevs->next = nexts;
			else
				schedq = nexts;
			if (curs->event)
				iax_event_free(curs->event);
			free(curs);
		} else {
			prevs = curs;
		}
		curs = nexts;
		loop_cnt++;
	}
		
	cur = sessions;
	while(cur) {
		if (cur == session) {
			if (prev)
				prev->next = session->next;
			else
				sessions = session->next;
			free(session);
			return;
		}
		prev = cur;
		cur = cur->next;
	}
}

static int iax_send_lagrp(struct iax_session *session, unsigned int ts);
static int iax_send_pong(struct iax_session *session, unsigned int ts);

static struct iax_event *handle_event(struct iax_event *event)
{
	/* We have a candidate event to be delievered.  Be sure
	   the session still exists. */
	if (event) {
		if (iax_session_valid(event->session)) {
			/* Lag requests are never actually sent to the client, but
			   other than that are handled as normal packets */
			switch(event->etype) {
			case IAX_EVENT_REJECT:
			case IAX_EVENT_HANGUP:
				/* Destroy this session -- it's no longer valid */
				destroy_session(event->session);
				return event;
			case IAX_EVENT_LAGRQ:
				event->etype = IAX_EVENT_LAGRP;
				iax_send_lagrp(event->session, event->ts);
				iax_event_free(event);
				break;
			case IAX_EVENT_PING:
				event->etype = IAX_EVENT_PONG;
				iax_send_pong(event->session, event->ts);
				iax_event_free(event);
				break;
			default:
				return event;
			}
		} else 
			iax_event_free(event);
	}
	return NULL;
}

static int iax2_vnak(struct iax_session *session)
{
	return send_command_immediate(session, AST_FRAME_IAX, IAX_COMMAND_VNAK, 0, NULL, 0, session->iseqno);
}

int iax_send_dtmf(struct iax_session *session, char digit)
{
	return send_command(session, AST_FRAME_DTMF, digit, 0, NULL, 0, -1);
}

static unsigned int iax2_datetime(void)
{
	time_t t;
	struct tm tm;
	unsigned int tmp;
	time(&t);
	localtime_r(&t, &tm);
	tmp  = (tm.tm_sec >> 1) & 0x1f;   /* 5 bits of seconds */
	tmp |= (tm.tm_min & 0x3f) << 5;   /* 6 bits of minutes */
	tmp |= (tm.tm_hour & 0x1f) << 11;   /* 5 bits of hours */
	tmp |= (tm.tm_mday & 0x1f) << 16; /* 5 bits of day of month */
	tmp |= ((tm.tm_mon + 1) & 0xf) << 21; /* 4 bits of month */
	tmp |= ((tm.tm_year - 100) & 0x7f) << 25; /* 7 bits of year */
	return tmp;
}

int iax_send_authreq(struct iax_session *session,int authmethods)
{
	struct iax_ie_data ied;
	memset(&ied, 0, sizeof(ied));
	iax_ie_append_short(&ied, IAX_IE_AUTHMETHODS,authmethods);
	if (authmethods & (IAX_AUTH_MD5)) {
		if (session->challenge)
			iax_ie_append_str(&ied, IAX_IE_CHALLENGE, session->challenge);
	}
	iax_ie_append_str(&ied,IAX_IE_USERNAME, session->username);
	return send_command(session, AST_FRAME_IAX, IAX_COMMAND_AUTHREQ,0,ied.buf, ied.pos, -1);
}

int iax_send_regauth(struct iax_session *session,int authmethods)
{
	struct iax_ie_data ied;
	memset(&ied, 0, sizeof(ied));
	iax_ie_append_short(&ied, IAX_IE_AUTHMETHODS,authmethods);
	if (authmethods & (IAX_AUTH_MD5)) {
		if (session->challenge)
			iax_ie_append_str(&ied, IAX_IE_CHALLENGE, session->challenge);
	}
	iax_ie_append_str(&ied,IAX_IE_USERNAME, session->username);
	return send_command(session, AST_FRAME_IAX, IAX_COMMAND_REGAUTH,0,ied.buf, ied.pos, -1);
}

int iax_send_regack(struct iax_session *session)
{
	struct iax_ie_data ied;
	memset(&ied, 0, sizeof(ied));
	if (!session->username)
		return 0;
	iax_ie_append_str(&ied, IAX_IE_USERNAME, session->username);
	iax_ie_append_int(&ied, IAX_IE_DATETIME, iax2_datetime());
	iax_ie_append_short(&ied, IAX_IE_REFRESH, session->refresh);
	iax_ie_append_addr(&ied, IAX_IE_APPARENT_ADDR, &session->peeraddr);
	return send_command_final(session, AST_FRAME_IAX, IAX_COMMAND_REGACK, 0, ied.buf, ied.pos, -1);
}

int iax_send_regrej(struct iax_session *session)
{
	struct iax_ie_data ied;
	memset(&ied, 0, sizeof(ied));
	iax_ie_append_str(&ied, IAX_IE_CAUSE, "Registration Refused");
	return send_command_final(session, AST_FRAME_IAX, IAX_COMMAND_REGREJ, 0, ied.buf, ied.pos, -1);
}

int iax_send_voice(struct iax_session *session, int format, char *data, int datalen)
{
	
	/* Send a (possibly compressed) voice frame */
	if (!session->quelch)
		return send_command(session, AST_FRAME_VOICE, format, 0, data, datalen, -1);
	return 0;
}

int iax_send_image(struct iax_session *session, int format, char *data, int datalen)
{
	/* Send an image frame */
	return send_command(session, AST_FRAME_IMAGE, format, 0, data, datalen, -1);
}

int iax_register(struct iax_session *session, char *server, char *peer, char *secret, int refresh)
{
	/* Send a registration request */
	char tmp[256];
	char *p;
	int res;
	int portno = IAX_DEFAULT_PORTNO;
	struct iax_ie_data ied;
	struct hostent *hp;
	
	tmp[255] = '\0';
	strncpy(tmp, server, sizeof(tmp) - 1);
	p = strchr(tmp, ':');
	if (p)
		portno = atoi(p);
	
	memset(&ied, 0, sizeof(ied));
	if (secret)
		strncpy(session->secret, secret, sizeof(session->secret) - 1);
	else
		strcpy(session->secret, "");

	/* Connect first */
	hp = gethostbyname(tmp);
	if (!hp) {
		snprintf(iax_errstr, sizeof(iax_errstr), "Invalid hostname: %s", tmp);
		return -1;
	}
	memcpy(&session->peeraddr.sin_addr, hp->h_addr, sizeof(session->peeraddr.sin_addr));
	session->peeraddr.sin_port = htons(portno);
	session->peeraddr.sin_family = AF_INET;
	strncpy(session->username, peer, sizeof(session->username) - 1);
	session->refresh = refresh;
	iax_ie_append_str(&ied, IAX_IE_USERNAME, peer);
	iax_ie_append_short(&ied, IAX_IE_REFRESH, refresh);
	res = send_command(session, AST_FRAME_IAX, IAX_COMMAND_REGREQ, 0, ied.buf, ied.pos, -1);
	return res;
}

int iax_reject(struct iax_session *session, char *reason)
{
	struct iax_ie_data ied;
	memset(&ied, 0, sizeof(ied));
	iax_ie_append_str(&ied, IAX_IE_CAUSE, reason ? reason : "Unspecified");
	return send_command_final(session, AST_FRAME_IAX, IAX_COMMAND_REJECT, 0, ied.buf, ied.pos, -1);
}

int iax_hangup(struct iax_session *session, char *byemsg)
{
	struct iax_ie_data ied;
	memset(&ied, 0, sizeof(ied));
	iax_ie_append_str(&ied, IAX_IE_CAUSE, byemsg ? byemsg : "Normal clearing");
	return send_command_final(session, AST_FRAME_IAX, IAX_COMMAND_HANGUP, 0, ied.buf, ied.pos, -1);
}

int iax_sendurl(struct iax_session *session, char *url)
{
	return send_command(session, AST_FRAME_HTML, AST_HTML_URL, 0, url, strlen(url), -1);
}

int iax_ring_announce(struct iax_session *session)
{
	return send_command(session, AST_FRAME_CONTROL, AST_CONTROL_RINGING, 0, NULL, 0, -1);
}

int iax_lag_request(struct iax_session *session)
{
	return send_command(session, AST_FRAME_IAX, IAX_COMMAND_LAGRQ, 0, NULL, 0, -1);
}

int iax_busy(struct iax_session *session)
{
	return send_command(session, AST_FRAME_CONTROL, AST_CONTROL_BUSY, 0, NULL, 0, -1);
}

int iax_accept(struct iax_session *session,int format)
{
	struct iax_ie_data ied;
	memset(&ied, 0, sizeof(ied));
	iax_ie_append_int(&ied, IAX_IE_FORMAT, format);
	return send_command(session, AST_FRAME_IAX, IAX_COMMAND_ACCEPT, 0, ied.buf, ied.pos, -1);
}

int iax_answer(struct iax_session *session)
{
	return send_command(session, AST_FRAME_CONTROL, AST_CONTROL_ANSWER, 0, NULL, 0, -1);
}

int iax_load_complete(struct iax_session *session)
{
	return send_command(session, AST_FRAME_HTML, AST_HTML_LDCOMPLETE, 0, NULL, 0, -1);
}

int iax_send_url(struct iax_session *session, char *url, int link)
{
	return send_command(session, AST_FRAME_HTML, link ? AST_HTML_LINKURL : AST_HTML_URL, 0, url, strlen(url), -1);
}

int iax_send_text(struct iax_session *session, char *text)
{
	return send_command(session, AST_FRAME_TEXT, 0, 0, text, strlen(text) + 1, -1);
}

int iax_send_unlink(struct iax_session *session)
{
	return send_command(session, AST_FRAME_HTML, AST_HTML_UNLINK, 0, NULL, 0, -1);
}

int iax_send_link_reject(struct iax_session *session)
{
	return send_command(session, AST_FRAME_HTML, AST_HTML_LINKREJECT, 0, NULL, 0, -1);
}

static int iax_send_pong(struct iax_session *session, unsigned int ts)
{
	return send_command(session, AST_FRAME_IAX, IAX_COMMAND_PONG, ts, NULL, 0, -1);
}

int iax_send_ping(struct iax_session *session)
{
	return send_command(session, AST_FRAME_IAX, IAX_COMMAND_PING, 0, NULL, 0, -1);
}

static int iax_send_lagrp(struct iax_session *session, unsigned int ts)
{
	return send_command(session, AST_FRAME_IAX, IAX_COMMAND_LAGRP, ts, NULL, 0, -1);
}

static int iax_send_txcnt(struct iax_session *session)
{
	struct iax_ie_data ied;
	memset(&ied, 0, sizeof(ied));
	iax_ie_append_int(&ied, IAX_IE_TRANSFERID, session->transferid);
	return send_command_transfer(session, AST_FRAME_IAX, IAX_COMMAND_TXCNT, 0, ied.buf, ied.pos);
}

static int iax_send_txrej(struct iax_session *session)
{
	struct iax_ie_data ied;
	memset(&ied, 0, sizeof(ied));
	iax_ie_append_int(&ied, IAX_IE_TRANSFERID, session->transferid);
	return send_command_transfer(session, AST_FRAME_IAX, IAX_COMMAND_TXREJ, 0, ied.buf, ied.pos);
}

static int iax_send_txaccept(struct iax_session *session)
{
	struct iax_ie_data ied;
	memset(&ied, 0, sizeof(ied));
	iax_ie_append_int(&ied, IAX_IE_TRANSFERID, session->transferid);
	return send_command_transfer(session, AST_FRAME_IAX, IAX_COMMAND_TXACC, 0, ied.buf, ied.pos);
}

static int iax_send_txready(struct iax_session *session)
{
	struct iax_ie_data ied;
	memset(&ied, 0, sizeof(ied));
	iax_ie_append_int(&ied, IAX_IE_TRANSFERID, session->transferid);
	return send_command(session, AST_FRAME_IAX, IAX_COMMAND_TXREADY, 0, ied.buf, ied.pos, -1);
}

int iax_auth_reply(struct iax_session *session, char *password, char *challenge, int methods)
{
	char reply[16];
	struct MD5Context md5;
	char realreply[256];
	struct iax_ie_data ied;
	memset(&ied, 0, sizeof(ied));
	if ((methods & IAX_AUTH_MD5) && challenge) {
		MD5Init(&md5);
		MD5Update(&md5, (const unsigned char *) challenge, strlen(challenge));
		MD5Update(&md5, (const unsigned char *) password, strlen(password));
		MD5Final((unsigned char *) reply, &md5);
		bzero(realreply, sizeof(realreply));
		convert_reply(realreply, (unsigned char *) reply);
		iax_ie_append_str(&ied, IAX_IE_MD5_RESULT, realreply);
	} else {
		iax_ie_append_str(&ied, IAX_IE_MD5_RESULT, password);
	}
	return send_command(session, AST_FRAME_IAX, IAX_COMMAND_AUTHREP, 0, ied.buf, ied.pos, -1);
}

static int iax_regauth_reply(struct iax_session *session, char *password, char *challenge, int methods)
{
	char reply[16];
	struct MD5Context md5;
	char realreply[256];
	struct iax_ie_data ied;
	memset(&ied, 0, sizeof(ied));
	iax_ie_append_str(&ied, IAX_IE_USERNAME, session->username);
	iax_ie_append_short(&ied, IAX_IE_REFRESH, session->refresh);
	if ((methods & IAX_AUTHMETHOD_MD5) && challenge) {
		MD5Init(&md5);
		MD5Update(&md5, (const unsigned char *) challenge, strlen(challenge));
		MD5Update(&md5, (const unsigned char *) password, strlen(password));
		MD5Final((unsigned char *) reply, &md5);
		bzero(realreply, sizeof(realreply));
		convert_reply(realreply, (unsigned char *) reply);
		iax_ie_append_str(&ied, IAX_IE_MD5_RESULT, realreply);
	} else {
		iax_ie_append_str(&ied, IAX_IE_MD5_RESULT, password);
	}
	return send_command(session, AST_FRAME_IAX, IAX_COMMAND_REGREQ, 0, ied.buf, ied.pos, -1);
}

void iax_set_formats(int fmt)
{
	sformats = fmt;
}

int iax_dial(struct iax_session *session, char *number)
{
	struct iax_ie_data ied;
	memset(&ied, 0, sizeof(ied));
	iax_ie_append_str(&ied, IAX_IE_CALLED_NUMBER, number);
	return send_command(session, AST_FRAME_IAX, IAX_COMMAND_DIAL, 0, ied.buf, ied.pos, -1);
}

int iax_quelch(struct iax_session *session)
{
	return send_command(session, AST_FRAME_IAX, IAX_COMMAND_QUELCH, 0, NULL, 0, -1);
}

int iax_unquelch(struct iax_session *session)
{
	return send_command(session, AST_FRAME_IAX, IAX_COMMAND_UNQUELCH, 0, NULL, 0, -1);
}

int iax_dialplan_request(struct iax_session *session, char *number)
{
	struct iax_ie_data ied;
	memset(&ied, 0, sizeof(ied));
	iax_ie_append_str(&ied, IAX_IE_CALLED_NUMBER, number);
	return send_command(session, AST_FRAME_IAX, IAX_COMMAND_DPREQ, 0, ied.buf, ied.pos, -1);
}

int iax_call(struct iax_session *session, char *cidnum, char *cidname, char *ich, char *lang, int wait)
{
	char tmp[256]="";
	char *part1, *part2;
	int res;
	int portno;
	char *username, *hostname, *secret, *context, *exten, *dnid;
	struct iax_ie_data ied;
	struct hostent *hp;
	/* We start by parsing up the temporary variable which is of the form of:
	   [user@]peer[:portno][/exten[@context]] */
	if (!ich) {
		IAXERROR "Invalid IAX Call Handle\n");
		DEBU(G "Invalid IAX Call Handle\n");
		return -1;
	}
	memset(&ied, 0, sizeof(ied));
	strncpy(tmp, ich, sizeof(tmp) - 1);	
	iax_ie_append_short(&ied, IAX_IE_VERSION, IAX_PROTO_VERSION);
	if (cidnum)
		iax_ie_append_str(&ied, IAX_IE_CALLING_NUMBER, cidnum);
	if (cidname)
		iax_ie_append_str(&ied, IAX_IE_CALLING_NAME, cidname);
	
	/* XXX We should have a preferred format XXX */
	iax_ie_append_int(&ied, IAX_IE_FORMAT, sformats);
	iax_ie_append_int(&ied, IAX_IE_CAPABILITY, sformats);
	if (lang)
		iax_ie_append_str(&ied, IAX_IE_LANGUAGE, lang);
	
	/* Part 1 is [user[:password]@]peer[:port] */
	part1 = strtok(tmp, "/");

	/* Part 2 is exten[@context] if it is anything all */
	part2 = strtok(NULL, "/");
	
	if (strchr(part1, '@')) {
		username = strtok(part1, "@");
		hostname = strtok(NULL, "@");
	} else {
		username = NULL;
		hostname = part1;
	}
	
	if (username && strchr(username, ':')) {
		username = strtok(username, ":");
		secret = strtok(NULL, ":");
	} else
		secret = NULL;

	if(username)
	  strncpy(session->username, username, sizeof(session->username) - 1);

	if(secret)
	  strncpy(session->secret, secret, sizeof(session->secret) - 1);
	
	if (strchr(hostname, ':')) {
		strtok(hostname, ":");
		portno = atoi(strtok(NULL, ":"));
	} else {
		portno = IAX_DEFAULT_PORTNO;
	}
	if (part2) {
		exten = strtok(part2, "@");
		dnid = exten;
		context = strtok(NULL, "@");
	} else {
		exten = NULL;
		dnid = NULL;
		context = NULL;
	}
	if (username)
		iax_ie_append_str(&ied, IAX_IE_USERNAME, username);
	if (exten && strlen(exten))
		iax_ie_append_str(&ied, IAX_IE_CALLED_NUMBER, exten);
	if (dnid && strlen(dnid))
		iax_ie_append_str(&ied, IAX_IE_DNID, dnid);
	if (context && strlen(context))
		iax_ie_append_str(&ied, IAX_IE_CALLED_CONTEXT, context);

	/* Setup host connection */
	hp = gethostbyname(hostname);
	if (!hp) {
		snprintf(iax_errstr, sizeof(iax_errstr), "Invalid hostname: %s", hostname);
		return -1;
	}
	memcpy(&session->peeraddr.sin_addr, hp->h_addr, sizeof(session->peeraddr.sin_addr));
	session->peeraddr.sin_port = htons(portno);
	session->peeraddr.sin_family = AF_INET;
	res = send_command(session, AST_FRAME_IAX, IAX_COMMAND_NEW, 0, ied.buf, ied.pos, -1);
	if (res < 0)
		return res;
	if (wait) {
		DEBU(G "Waiting not yet implemented\n");
		return -1;
	}
	return res;
}

static int calc_rxstamp(struct iax_session *session)
{
	struct timeval tv;
	int ms;

	if (!session->rxcore.tv_sec && !session->rxcore.tv_usec) {
		gettimeofday(&session->rxcore, NULL);
	}	
	gettimeofday(&tv, NULL);

	ms = (tv.tv_sec - session->rxcore.tv_sec) * 1000 +
		 (tv.tv_usec - session->rxcore.tv_usec) / 1000;
		return ms;
}

static int match(struct sockaddr_in *sin, short callno, short dcallno, struct iax_session *cur)
{
	if ((cur->peeraddr.sin_addr.s_addr == sin->sin_addr.s_addr) &&
		(cur->peeraddr.sin_port == sin->sin_port)) {
		/* This is the main host */
		if ((cur->peercallno == callno) || 
			((dcallno == cur->callno) && !cur->peercallno)) {
			/* That's us.  Be sure we keep track of the peer call number */
			cur->peercallno = callno;
			return 1;
		}
	}
	if ((cur->transfer.sin_addr.s_addr == sin->sin_addr.s_addr) &&
	    (cur->transfer.sin_port == sin->sin_port) && (cur->transferring)) {
		/* We're transferring */
		if (dcallno == cur->callno)
			return 1;
	}
	return 0;
}

static struct iax_session *iax_find_session(struct sockaddr_in *sin, 
											short callno, 
											short dcallno,
											int makenew)
{
	struct iax_session *cur = sessions;
	while(cur) {
		if (match(sin, callno, dcallno, cur))
			return cur;
		cur = cur->next;
	}
	if (makenew && !dcallno) {
		cur = iax_session_new();
		cur->peercallno = callno;
		cur->peeraddr.sin_addr.s_addr = sin->sin_addr.s_addr;
		cur->peeraddr.sin_port = sin->sin_port;
		cur->peeraddr.sin_family = AF_INET;
		DEBU(G "Making new session, peer callno %d, our callno %d\n", callno, cur->callno);
	} else {
		DEBU(G "No session, peer = %d, us = %d\n", callno, dcallno);
	}
	return cur;	
}

#ifdef EXTREME_DEBUG
static int display_time(int ms)
{
	static int oldms = -1;
	if (oldms < 0) {
		DEBU(G "First measure\n");
		oldms = ms;
		return 0;
	}
	DEBU(G "Time from last frame is %d ms\n", ms - oldms);
	oldms = ms;
	return 0;
}
#endif

#define FUDGE 1

static struct iax_event *schedule_delivery(struct iax_event *e, unsigned int ts)
{
	/* 
	 * This is the core of the IAX jitterbuffer delivery mechanism: 
	 * Dynamically adjust the jitterbuffer and decide how long to wait
	 * before delivering the packet.
	 */
	int ms, x;
	int drops[MEMORY_SIZE];
	int min, max=0, maxone=0, y, z, match;


#ifdef EXTREME_DEBUG	
	DEBU(G "[%p] We are at %d, packet is for %d\n", e->session, calc_rxstamp(e->session), ts);
#endif
	
#ifdef VOICE_SMOOTHING
	if (e->etype == IAX_EVENT_VOICE) {
		/* Smooth voices if we know enough about the format */
		switch(e->event.voice.format) {
		case AST_FORMAT_GSM:
			/* GSM frames are 20 ms long, although there could be periods of 
			   silence.  If the time is < 50 ms, assume it ought to be 20 ms */
			if (ts - e->session->lastts < 50)  
				ts = e->session->lastts + 20;
#ifdef EXTREME_DEBUG
			display_time(ts);
#endif
			break;
		default:
			/* Can't do anything */
		}
		e->session->lastts = ts;
	}
#endif
	
	/* How many ms from now should this packet be delivered? (remember
	   this can be a negative number, too */
	ms = calc_rxstamp(e->session) - ts;
	if (ms > 32768) {
		/* What likely happened here is that our counter has circled but we haven't
		   gotten the update from the main packet.  We'll just pretend that we did, and
		   update the timestamp appropriately. */
		ms -= 65536;
	}
	if (ms < -32768) {
		/* We got this packet out of order.  Lets add 65536 to it to bring it into our new
		   time frame */
		ms += 65536;
	}

#if 0	
	printf("rxstamp is %d, timestamp is %d, ms is %d\n", calc_rxstamp(e->session), ts, ms);
#endif
	/* Rotate history queue.  Leading 0's are irrelevant. */
	for (x=0; x < MEMORY_SIZE - 1; x++) 
		e->session->history[x] = e->session->history[x+1];
	
	/* Add new entry for this time */
	e->session->history[x] = ms;
	
	/* We have to find the maximum and minimum time delay we've had to deliver. */
	min = e->session->history[0];
	for (z=0;z < iax_dropcount + 1; z++) {
		/* We drop the top iax_dropcount entries.  iax_dropcount represents
		   a tradeoff between quality of voice and latency.  3% drop seems to
		   be unnoticable to the client and can significantly improve latency.  
		   We add one more to our droplist, but that's the one we actually use, 
		   and don't drop.  */
		max = -99999999;
		for (x=0;x<MEMORY_SIZE;x++) {
			if (max < e->session->history[x]) {
				/* New candidate value.  Make sure we haven't dropped it. */
				match=0;
				for(y=0;!match && (y<z); y++) 
					match |= (drops[y] == x);
				/* If there is no match, this is our new maximum */
				if (!match) {
					max = e->session->history[x];
					maxone = x;
				}
			}
			if (!z) {
				/* First pass, calcualte our minimum, too */
				if (min > e->session->history[x])
					min = e->session->history[x];
			}
		}
		drops[z] = maxone;
	}
	/* Again, just for reference.  The "jitter buffer" is the max.  The difference
	   is the perceived jitter correction. */
	e->session->jitter = max - min;
	
	/* If the jitter buffer is substantially too large, shrink it, slowly enough
	   that the client won't notice ;-) . */
	if (max < e->session->jitterbuffer - max_extra_jitterbuffer) {
#ifdef EXTREME_DEBUG
		DEBU(G "Shrinking jitterbuffer (target = %d, current = %d...\n", max, e->session->jitterbuffer);
#endif
		e->session->jitterbuffer -= 2;
	}
		
	/* Keep the jitter buffer from becoming unreasonably large */
	if (max > min + max_jitterbuffer) {
		DEBU(G "Constraining jitter buffer (min = %d, max = %d)...\n", min, max);
		max = min + max_jitterbuffer;
	}
	
	/* If the jitter buffer is too small, we immediately grow our buffer to
	   accomodate */
	if (max > e->session->jitterbuffer)
		e->session->jitterbuffer = max;
	
	/* Start with our jitter buffer delay, and subtract the lateness (or earliness).
	   Remember these times are all relative to the first packet, so their absolute
	   values are really irrelevant. */
	ms = e->session->jitterbuffer - ms - IAX_SCHEDULE_FUZZ;
	
	/* If the jitterbuffer is disabled, always deliver immediately */
	if (!iax_use_jitterbuffer)
		ms = 0;
	
	if (ms < 1) {
#ifdef EXTREME_DEBUG
		DEBU(G "Calculated delay is only %d\n", ms);
#endif
		if ((ms > -4) || (e->etype != IAX_EVENT_VOICE)) {
			/* Return the event immediately if it's it's less than 3 milliseconds
			   too late, or if it's not voice (believe me, you don't want to
			   just drop a hangup frame because it's late, or a ping, or some such.
			   That kinda ruins retransmissions too ;-) */
			/* Queue for immediate delivery */
			iax_sched_event(e, NULL, 0);
			return NULL;
			//return e;
		}
		DEBU(G "(not so) Silently dropping a packet (ms = %d)\n", ms);
		/* Silently discard this as if it were to be delivered */
		free(e);
		return NULL;
	}
	/* We need this to be delivered in the future, so we use our scheduler */
	iax_sched_event(e, NULL, ms);
#ifdef EXTREME_DEBUG
	DEBU(G "Delivering packet in %d ms\n", ms);
#endif
	return NULL;
	
}

static int uncompress_subclass(unsigned char csub)
{
	/* If the SC_LOG flag is set, return 2^csub otherwise csub */
	if (csub & IAX_FLAG_SC_LOG)
		return 1 << (csub & ~IAX_FLAG_SC_LOG & IAX_MAX_SHIFT);
	else
		return csub;
}

static
#ifndef	WIN32
inline
#endif
char *extract(char *src, char *string)
{
	/* Extract and duplicate what we need from a string */
	char *s, *t;
	s = strstr(src, string);
	if (s) {
		s += strlen(string);
		s = strdup(s);
		/* End at ; */
		t = strchr(s, ';');
		if (t) {
			*t = '\0';
		}
	}
	return s;
		
}

static struct iax_event *iax_header_to_event(struct iax_session *session,
											 struct ast_iax2_full_hdr *fh,
											 int datalen, struct sockaddr_in *sin)
{
	struct iax_event *e;
	struct iax_sched *sch;
	unsigned int ts;
	int subclass = uncompress_subclass(fh->csub);
	int nowts;
	int updatehistory = 1;
	ts = ntohl(fh->ts);
	session->last_ts = ts;
	e = (struct iax_event *)malloc(sizeof(struct iax_event) + datalen + 1);

#ifdef DEBUG_SUPPORT
	iax_showframe(NULL, fh, 1, sin, datalen);
#endif

	/* Get things going with it, timestamp wise, if we
	   haven't already. */

		/* Handle implicit ACKing unless this is an INVAL, and only if this is 
		   from the real peer, not the transfer peer */
		if (!inaddrcmp(sin, &session->peeraddr) && 
			(((subclass != IAX_COMMAND_INVAL)) ||
			(fh->type != AST_FRAME_IAX))) {
			unsigned char x;
			/* XXX This code is not very efficient.  Surely there is a better way which still
			       properly handles boundary conditions? XXX */
			/* First we have to qualify that the ACKed value is within our window */
			for (x=session->rseqno; x != session->oseqno; x++)
				if (fh->iseqno == x)
					break;
			if ((x != session->oseqno) || (session->oseqno == fh->iseqno)) {
				/* The acknowledgement is within our window.  Time to acknowledge everything
				   that it says to */
				for (x=session->rseqno; x != fh->iseqno; x++) {
					/* Ack the packet with the given timestamp */
					DEBU(G "Cancelling transmission of packet %d\n", x);
					sch = schedq;
					while(sch) {
						if (sch->frame && (sch->frame->session == session) && 
						    (sch->frame->oseqno == x)) 
							sch->frame->retries = -1;
						sch = sch->next;
					}
				}
				/* Note how much we've received acknowledgement for */
				session->rseqno = fh->iseqno;
			} else
				DEBU(G "Received iseqno %d not within window %d->%d\n", fh->iseqno, session->rseqno, session->oseqno);
		}

	/* Check where we are */
		if (ntohs(fh->dcallno) & IAX_FLAG_RETRANS)
			updatehistory = 0;
		if ((session->iseqno != fh->oseqno) &&
			(session->iseqno ||
				((subclass != IAX_COMMAND_TXCNT) &&
				(subclass != IAX_COMMAND_TXACC)) ||
				(subclass != AST_FRAME_IAX))) {
			if (
			 ((subclass != IAX_COMMAND_ACK) &&
			  (subclass != IAX_COMMAND_INVAL) &&
			  (subclass != IAX_COMMAND_TXCNT) &&
			  (subclass != IAX_COMMAND_TXACC) &&
			  (subclass != IAX_COMMAND_VNAK)) ||
			  (fh->type != AST_FRAME_IAX)) {
			 	/* If it's not an ACK packet, it's out of order. */
				DEBU(G "Packet arrived out of order (expecting %d, got %d) (frametype = %d, subclass = %d)\n", 
					session->iseqno, fh->oseqno, fh->type, subclass);
				if (session->iseqno > fh->oseqno) {
					/* If we've already seen it, ack it XXX There's a border condition here XXX */
					if ((fh->type != AST_FRAME_IAX) || 
							((subclass != IAX_COMMAND_ACK) && (subclass != IAX_COMMAND_INVAL))) {
						DEBU(G "Acking anyway\n");
						/* XXX Maybe we should handle its ack to us, but then again, it's probably outdated anyway, and if
						   we have anything to send, we'll retransmit and get an ACK back anyway XXX */
						send_command_immediate(session, AST_FRAME_IAX, IAX_COMMAND_ACK, ts, NULL, 0,fh->iseqno);
					}
				} else {
					/* Send a VNAK requesting retransmission */
					iax2_vnak(session);
				}
				return NULL;
			}
		} else {
			/* Increment unless it's an ACK or VNAK */
			if (((subclass != IAX_COMMAND_ACK) &&
			    (subclass != IAX_COMMAND_INVAL) &&
			    (subclass != IAX_COMMAND_TXCNT) &&
			    (subclass != IAX_COMMAND_TXACC) &&
				(subclass != IAX_COMMAND_VNAK)) ||
			    (fh->type != AST_FRAME_IAX))
				session->iseqno++;
		}
			
	if (e) {
		memset(e, 0, sizeof(struct iax_event) + datalen);
		e->session = session;
		switch(fh->type) {
		case AST_FRAME_DTMF:
			e->etype = IAX_EVENT_DTMF;
			e->subclass = subclass;
			return schedule_delivery(e, ts);
		case AST_FRAME_VOICE:
			e->etype = IAX_EVENT_VOICE;
			e->subclass = subclass;
			session->voiceformat = subclass;
			if (datalen) {
				memcpy(e->data, fh->iedata, datalen);
				e->datalen = datalen;
			}
			return schedule_delivery(e, ts);
		case AST_FRAME_IAX:
			/* Parse IE's */
			if (datalen) {
				memcpy(e->data, fh->iedata, datalen);
				e->datalen = datalen;
			}
			if (iax_parse_ies(&e->ies, e->data, e->datalen)) {
				IAXERROR "Unable to parse IE's");
				free(e);
				e = NULL;
				break;
			}
			switch(subclass) {
			case IAX_COMMAND_NEW:
				/* This is a new, incoming call */
				e->etype = IAX_EVENT_CONNECT;
				e = schedule_delivery(e, ts);
				break;
			case IAX_COMMAND_AUTHREQ:
				/* This is a request for a call */
				e->etype = IAX_EVENT_AUTHRQ;
				if (strlen(session->username) && !strcmp(e->ies.username, session->username) &&
					strlen(session->secret)) {
						/* Hey, we already know this one */
						iax_auth_reply(session, session->secret, e->ies.challenge, e->ies.authmethods);
						free(e);
						e = NULL;
						break;
				}
				e = schedule_delivery(e, ts);
				break;
			case IAX_COMMAND_AUTHREP:
				e->etype = IAX_EVENT_AUTHRP;
				e = schedule_delivery(e, ts);
				break;
			case IAX_COMMAND_HANGUP:
				e->etype = IAX_EVENT_HANGUP;
				e = schedule_delivery(e, ts);
				break;
			case IAX_COMMAND_INVAL:
				e->etype = IAX_EVENT_HANGUP;
				e = schedule_delivery(e, ts);
				break;
			case IAX_COMMAND_REJECT:
				e->etype = IAX_EVENT_REJECT;
				e = schedule_delivery(e, ts);
				break;
			case IAX_COMMAND_ACK:
				e =  NULL;
				break;
			case IAX_COMMAND_LAGRQ:
				/* Pass this along for later handling */
				e->etype = IAX_EVENT_LAGRQ;
				e->ts = ts;
				e = schedule_delivery(e, ts);
				break;
			case IAX_COMMAND_PING:
				/* Just immediately reply */
				e->etype = IAX_EVENT_PING;
				e->ts = ts;
				e = schedule_delivery(e, ts);
				break;
			case IAX_COMMAND_PONG:
				e->etype = IAX_EVENT_PONG;
				e = schedule_delivery(e, ts);
				break;
			case IAX_COMMAND_ACCEPT:
				e->etype = IAX_EVENT_ACCEPT;
				e = schedule_delivery(e, ts);
				break;
			case IAX_COMMAND_REGACK:
				e->etype = IAX_EVENT_REGACK;
				e = schedule_delivery(e, ts);
				break;
			case IAX_COMMAND_REGREQ:
				e->etype = IAX_EVENT_REGREQ;
				e = schedule_delivery(e, ts);
				break;
			case IAX_COMMAND_REGAUTH:
				iax_regauth_reply(session, session->secret, e->ies.challenge, e->ies.authmethods);
				free(e);
				e = NULL;
				break;
			case IAX_COMMAND_REGREJ:
				e->etype = IAX_EVENT_REGREJ;
				e = schedule_delivery(e, ts);
				break;
			case IAX_COMMAND_LAGRP:
				e->etype = IAX_EVENT_LAGRP;
				nowts = calc_timestamp(session, 0);
				e->ts = nowts - ts;
				e->subclass = session->jitter;
				/* Can't call schedule_delivery since timestamp is non-normal */
				break;;
			case IAX_COMMAND_TXREQ:
				session->transfer = *e->ies.apparent_addr;
				session->transfer.sin_family = AF_INET;
				session->transfercallno = e->ies.callno;
				session->transferring = TRANSFER_BEGIN;
				session->transferid = e->ies.transferid;
				iax_send_txcnt(session);
				free(e);
				e = NULL;
				break;
			case IAX_COMMAND_DPREP:
				/* Received dialplan reply */
				e->etype = IAX_EVENT_DPREP;
				/* Return immediately, makes no sense to schedule */
				break;
			case IAX_COMMAND_TXCNT:
				/* Received a transfer connect.  Accept it if we're transferring */
				e->etype = IAX_EVENT_TXACCEPT;
				if (session->transferring) 
					iax_send_txaccept(session);
				free(e);
				e = NULL;
				break;
			case IAX_COMMAND_TXACC:
				e->etype = IAX_EVENT_TXREADY;
				if (session->transferring) {
					/* Cancel any more connect requests */
					sch = schedq;
					while(sch) {
						if (sch->frame && sch->frame->transfer)
								sch->frame->retries = -1;
						sch = sch->next;
					}
					session->transferring = TRANSFER_READY;
					iax_send_txready(session);
				}
				free(e);
				return NULL;
			case IAX_COMMAND_TXREL:
				/* Release the transfer */
				session->peercallno = e->ies.callno;
				/* Change from transfer to session now */
				memcpy(&session->peeraddr, &session->transfer, sizeof(session->peeraddr));
				memset(&session->transfer, 0, sizeof(session->transfer));
				session->transferring = TRANSFER_NONE;
				/* Force retransmission of a real voice packet, and reset all timing */
				session->svoiceformat = -1;
				session->voiceformat = 0;
				memset(&session->rxcore, 0, sizeof(session->rxcore));
				memset(&session->offset, 0, sizeof(session->offset));
				memset(&session->history, 0, sizeof(session->history));
				session->jitterbuffer = 0;
				session->jitter = 0;
				session->lag = 0;
				/* Reset sequence numbers */
				session->oseqno = 0;
				session->iseqno = 0;
				session->lastsent = 0;
				session->last_ts = 0;
				session->lastvoicets = 0;
				session->pingtime = 30;
				e->etype = IAX_EVENT_TRANSFER;
				/* We have to dump anything we were going to (re)transmit now that we've been
				   transferred since they're all invalid and for the old host. */
				sch = schedq;
				while(sch) {
					if (sch->frame && (sch->frame->session == session))
								sch->frame->retries = -1;
					sch = sch->next;
				}
				return e;
			case IAX_COMMAND_QUELCH:
				e->etype = IAX_EVENT_QUELCH;
				session->quelch = 1;
				return e;
			case IAX_COMMAND_UNQUELCH:
				e->etype = IAX_EVENT_UNQUELCH;
				session->quelch = 0;
				return e;
			default:
				DEBU(G "Don't know what to do with IAX command %d\n", subclass);
				free(e);
				e = NULL;
			}
			if (session->aseqno != session->iseqno)
				send_command_immediate(session, AST_FRAME_IAX, IAX_COMMAND_ACK, ts, NULL, 0, fh->iseqno);
			break;
		case AST_FRAME_CONTROL:
			switch(subclass) {
			case AST_CONTROL_ANSWER:
				e->etype = IAX_EVENT_ANSWER;
				return schedule_delivery(e, ts);
			case AST_CONTROL_CONGESTION:
			case AST_CONTROL_BUSY:
				e->etype = IAX_EVENT_BUSY;
				return schedule_delivery(e, ts);
			case AST_CONTROL_RINGING:
				e->etype = IAX_EVENT_RINGA;
				return schedule_delivery(e, ts);
			default:
				DEBU(G "Don't know what to do with AST control %d\n", subclass);
				free(e);
				return NULL;
			}
			break;
		case AST_FRAME_IMAGE:
			e->etype = IAX_EVENT_IMAGE;
			e->subclass = subclass;
			if (datalen) {
				memcpy(e->data, fh->iedata, datalen);
			}
			return schedule_delivery(e, ts);

		case AST_FRAME_TEXT:
			e->etype = IAX_EVENT_TEXT;
			if (datalen) {
				memcpy(e->data, fh->iedata, datalen);
				/* some stupid clients are not sending 
				 * the terminal NUL */
				if (e->data[datalen-1])
					e->data[datalen] = 0;
			}
			return schedule_delivery(e, ts);

		case AST_FRAME_HTML:
			switch(fh->csub) {
			case AST_HTML_LINKURL:
				e->etype = IAX_EVENT_LINKURL;
				/* Fall through */
			case AST_HTML_URL:
				if (!e->etype)
					e->etype = IAX_EVENT_URL;
				if (datalen) {
					memcpy(e->data, fh->iedata, datalen);
				}
				return schedule_delivery(e, ts);
			case AST_HTML_LDCOMPLETE:
				e->etype = IAX_EVENT_LDCOMPLETE;
				return schedule_delivery(e, ts);
			case AST_HTML_UNLINK:
				e->etype = IAX_EVENT_UNLINK;
				return schedule_delivery(e, ts);
			case AST_HTML_LINKREJECT:
				e->etype = IAX_EVENT_LINKREJECT;
				return schedule_delivery(e, ts);
			default:
				DEBU(G "Don't know how to handle HTML type %d frames\n", fh->csub);
				free(e);
				return NULL;
			}
			break;
		default:
			DEBU(G "Don't know what to do with frame type %d\n", fh->type);
			free(e);
			return NULL;
		}
	} else
		DEBU(G "Out of memory\n");
	return NULL;
}

static struct iax_event *iax_miniheader_to_event(struct iax_session *session,
						struct ast_iax2_mini_hdr *mh,
						int datalen)
{
	struct iax_event *e;
	unsigned int ts;
	e = (struct iax_event *)malloc(sizeof(struct iax_event) + datalen);
	if (e) {
		if (session->voiceformat > 0) {
			e->etype = IAX_EVENT_VOICE;
			e->session = session;
			e->subclass = session->voiceformat;
			if (datalen) {
#ifdef EXTREME_DEBUG
				DEBU(G "%d bytes of voice\n", datalen);
#endif
				memcpy(e->data, mh->data, datalen);
				e->datalen = datalen;
			}
			ts = (session->last_ts & 0xFFFF0000) | ntohs(mh->ts);
			return schedule_delivery(e, ts);
		} else {
			DEBU(G "No last format received on session %d\n", session->callno);
			free(e);
			e = NULL;
		}
	} else
		DEBU(G "Out of memory\n");
	return e;
}

void iax_destroy(struct iax_session *session)
{
	destroy_session(session);
}

static struct iax_event *iax_net_read(void)
{
	char buf[65536];
	int res;
	struct sockaddr_in sin;
	int sinlen;
	sinlen = sizeof(sin);
	res = recvfrom(netfd, buf, sizeof(buf), 0, (struct sockaddr *) &sin, &sinlen);
	if (res < 0) {
#ifdef	WIN32
		if (WSAGetLastError() != WSAEWOULDBLOCK) {
			DEBU(G "Error on read: %d\n", WSAGetLastError());
			IAXERROR "Read error on network socket: %s", strerror(errno));
		}
#else
		if (errno != EAGAIN) {
			DEBU(G "Error on read: %s\n", strerror(errno));
			IAXERROR "Read error on network socket: %s", strerror(errno));
		}
#endif
		return NULL;
	}
	return iax_net_process(buf, res, &sin);
}

struct iax_event *iax_net_process(unsigned char *buf, int len, struct sockaddr_in *sin)
{
	struct ast_iax2_full_hdr *fh = (struct ast_iax2_full_hdr *)buf;
	struct ast_iax2_mini_hdr *mh = (struct ast_iax2_mini_hdr *)buf;
	struct iax_session *session;
	
	if (ntohs(fh->scallno) & IAX_FLAG_FULL) {
		/* Full size header */
		if (len < sizeof(struct ast_iax2_full_hdr)) {
			DEBU(G "Short header received from %s\n", inet_ntoa(sin->sin_addr));
			IAXERROR "Short header received from %s\n", inet_ntoa(sin->sin_addr));
		}
		/* We have a full header, process appropriately */
		session = iax_find_session(sin, ntohs(fh->scallno) & ~IAX_FLAG_FULL, ntohs(fh->dcallno) & ~IAX_FLAG_RETRANS, 1);
		if (session) 
			return iax_header_to_event(session, fh, len - sizeof(struct ast_iax2_full_hdr), sin);
		DEBU(G "No session?\n");
		return NULL;
	} else {
		if (len < sizeof(struct ast_iax2_mini_hdr)) {
			DEBU(G "Short header received from %s\n", inet_ntoa(sin->sin_addr));
			IAXERROR "Short header received from %s\n", inet_ntoa(sin->sin_addr));
		}
		/* Miniature, voice frame */
		session = iax_find_session(sin, ntohs(fh->scallno), 0, 0);
		if (session)
			return iax_miniheader_to_event(session, mh, len - sizeof(struct ast_iax2_mini_hdr));
		DEBU(G "No session?\n");
		return NULL;
	}
}

static struct iax_sched *iax_get_sched(struct timeval tv)
{
	struct iax_sched *cur, *prev=NULL;
	cur = schedq;
	/* Check the event schedule first. */
	while(cur) {
		if ((tv.tv_sec > cur->when.tv_sec) ||
		    ((tv.tv_sec == cur->when.tv_sec) && 
			(tv.tv_usec >= cur->when.tv_usec))) {
				/* Take it out of the event queue */
				if (prev) {
					prev->next = cur->next;
				} else {
					schedq = cur->next;
				}
				return cur;
		}
		cur = cur->next;
	}
	return NULL;
}

struct iax_event *iax_get_event(int blocking)
{
	struct iax_event *event;
	struct iax_frame *frame;
	struct timeval tv;
	struct iax_sched *cur;
	
	gettimeofday(&tv, NULL);
	
	while((cur = iax_get_sched(tv))) {
		event = cur->event;
		frame = cur->frame;
		if (event) {

			/* See if this is an event we need to handle */
			event = handle_event(event);
			if (event) {
				free(cur);
				return event;
			}
		} else {
			/* It's a frame, transmit it and schedule a retry */
			if (frame->retries < 0) {
				/* It's been acked.  No need to send it.   Destroy the old
				   frame */
				if (frame->data)
					free(frame->data);
				free(frame);
			} else if (frame->retries == 0) {
				if (frame->transfer) {
					/* Send a transfer reject since we weren't able to connect */
					iax_send_txrej(frame->session);
					free(cur);
					break;
				} else {
					/* We haven't been able to get an ACK on this packet.  We should
					   destroy its session */
					event = (struct iax_event *)malloc(sizeof(struct iax_event));
					if (event) {
						event->etype = IAX_EVENT_TIMEOUT;
						event->session = frame->session;
						free(cur);
						return handle_event(event);
					}
				}
			} else {
				struct ast_iax2_full_hdr *fh;
				/* Decrement remaining retries */
				frame->retries--;
				/* Multiply next retry time by 4, not above MAX_RETRY_TIME though */
				frame->retrytime *= 4;
				/* Keep under 1000 ms if this is a transfer packet */
				if (!frame->transfer) {
					if (frame->retrytime > MAX_RETRY_TIME)
						frame->retrytime = MAX_RETRY_TIME;
				} else if (frame->retrytime > 1000)
					frame->retrytime = 1000;
				fh = (struct ast_iax2_full_hdr *)(frame->data);
				fh->dcallno = htons(IAX_FLAG_RETRANS | frame->dcallno);
				iax_xmit_frame(frame);
				/* Schedule another retransmission */
				printf("Scheduling retransmission %d\n", frame->retries);
				iax_sched_event(NULL, frame, frame->retrytime);
			}
		}
		free(cur);
	}

	/* Now look for networking events */
	if (blocking) {
		/* Block until there is data if desired */
		fd_set fds;
		int nextEventTime;

		FD_ZERO(&fds);
		FD_SET(netfd, &fds);
	
		nextEventTime = iax_time_to_next_event(); 

		if(nextEventTime < 0) 
			select(netfd + 1, &fds, NULL, NULL,NULL); 
		else 
		{ 
			struct timeval nextEvent; 

			nextEvent.tv_sec = nextEventTime / 1000; 
			nextEvent.tv_usec = (nextEventTime % 1000) * 1000;

			select(netfd + 1, &fds, NULL, NULL, &nextEvent); 
		} 

	}
	event = iax_net_read();
	
	return handle_event(event);
}

struct sockaddr_in iax_get_peer_addr(struct iax_session *session)
{
	return session->peeraddr;
}

void iax_event_free(struct iax_event *event)
{
	free(event);
}

int iax_get_fd(void) 
{
	/* Return our network file descriptor.  The client can select on this (probably with other
	   things, or can add it to a network add sort of gtk_input_add for example */
	return netfd;
}
