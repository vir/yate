/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Implementation of Inter-Asterisk eXchange
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * Paul Chitescu <paulc-devel@null.ro>
 * 
 * Diana Cionoiu <diana@voip.null.ro>
 * 
 * This program is free software, distributed under the terms of
 * the GNU Lesser General Public License (LGPL)
 */
 
#ifndef _ASTERISK_IAX_CLIENT_H
#define _ASTERISK_IAX_CLIENT_H

#ifndef LINUX
#define socklen_t int
#endif

#include "frame.h"
#include "iax2.h"
#include "iax2-parser.h"

#define MAXSTRLEN 80

#define IAX_AUTHMETHOD_PLAINTEXT 	IAX_AUTH_PLAINTEXT
#define IAX_AUTHMETHOD_MD5 			IAX_AUTH_MD5

extern char iax_errstr[];

#define IAX_EVENT_CONNECT 0			/* Connect a new call */
#define IAX_EVENT_ACCEPT  1			/* Accept a call */
#define IAX_EVENT_HANGUP  2			/* Hang up a call */
#define IAX_EVENT_REJECT  3			/* Rejected call */
#define IAX_EVENT_VOICE   4			/* Voice Data */
#define IAX_EVENT_DTMF    5			/* A DTMF Tone */
#define IAX_EVENT_TIMEOUT 6			/* Connection timeout...  session will be
									   a pointer to free()'d memory! */
#define IAX_EVENT_LAGRQ   7			/* Lag request -- Internal use only */
#define IAX_EVENT_LAGRP   8			/* Lag Measurement.  See event.lag */
#define IAX_EVENT_RINGA	  9			/* Announce we/they are ringing */
#define IAX_EVENT_PING	  10		/* Ping -- internal use only */
#define IAX_EVENT_PONG	  11		/* Pong -- internal use only */
#define IAX_EVENT_BUSY	  12		/* Report a line busy */
#define IAX_EVENT_ANSWER  13		/* Answer the line */

#define IAX_EVENT_IMAGE   14		/* Send/Receive an image */
#define IAX_EVENT_AUTHRQ  15		/* Authentication request */
#define IAX_EVENT_AUTHRP  16		/* Authentication reply */

#define IAX_EVENT_REGREQ  17		/* Registration request */
#define IAX_EVENT_REGACK  18		/* Registration reply */
#define IAX_EVENT_URL	  19		/* URL received */
#define IAX_EVENT_LDCOMPLETE 20		/* URL loading complete */

#define IAX_EVENT_TRANSFER	21		/* Transfer has taken place */

#define IAX_EVENT_DPREQ		22		/* Dialplan request */
#define IAX_EVENT_DPREP		23		/* Dialplan reply */
#define IAX_EVENT_DIAL		24		/* Dial on a TBD call */

#define IAX_EVENT_QUELCH	25		/* Quelch Audio */
#define IAX_EVENT_UNQUELCH	26		/* Unquelch Audio */

#define IAX_EVENT_UNLINK	27		/* Unlink */
#define IAX_EVENT_LINKREJECT	28		/* Link Rejection */
#define IAX_EVENT_TEXT		29		/* Text Frame :-) */
#define IAX_EVENT_REGREJ  30		/* Registration reply */
#define IAX_EVENT_LINKURL	31		/* Unlink */

#define IAX_SCHEDULE_FUZZ 0			/* ms of fuzz to drop */

#ifdef WIN32
typedef int PASCAL (*sendto_t)(SOCKET, const char *, int, int, const struct sockaddr *, int);
#else
typedef int (*sendto_t)(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
#endif

#define MEMORY_SIZE 100
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

struct iax_event {
	int etype;						/* Type of event */
	int subclass;					/* Subclass data (event specific) */
	unsigned int ts;				/* Timestamp */
	struct iax_session *session;	/* Applicable session */
	int datalen;					/* Length of raw data */
	struct iax_ies ies;				/* IE's for IAX2 frames */
	unsigned char data[0];			/* Raw data if applicable */
};

/* All functions return 0 on success and -1 on failure unless otherwise
   specified */

/* Called to initialize IAX structures and sockets.  Returns actual
   portnumber (which it will try preferred portno first, but if not
   take what it can get */
extern int iax_init(int preferredportno);

/* Get filedescriptor for IAX to use with select or gtk_input_add */
extern int iax_get_fd(void);

/* Find out how many milliseconds until the next scheduled event */
extern int iax_time_to_next_event(void);

/* Generate a new IAX session */
extern struct iax_session *iax_session_new(void);

/* Return exactly one iax event (if there is one pending).  If blocking is
   non-zero, IAX will block until some event is received */
extern struct iax_event *iax_get_event(int blocking);


extern int iax_auth_reply(struct iax_session *session, char *password, 
						char *challenge, int methods);

/* Stop iax, hangup file descriptors, free memory, etc. */
extern void iax_end(void);

/* Free an event */
extern void iax_event_free(struct iax_event *event);

struct sockaddr_in;

/* Front ends for sending events */
extern void iax_set_formats(int fmt);
extern int iax_send_dtmf(struct iax_session *session, char digit);
extern int iax_send_voice(struct iax_session *session, int format, char *data, int datalen);
extern int iax_send_image(struct iax_session *session, int format, char *data, int datalen);
extern int iax_send_url(struct iax_session *session, char *url, int link);
extern int iax_send_text(struct iax_session *session, char *text);
extern int iax_load_complete(struct iax_session *session);
extern int iax_reject(struct iax_session *session, char *reason);
extern int iax_busy(struct iax_session *session);
extern int iax_hangup(struct iax_session *session, char *byemsg);
extern int iax_call(struct iax_session *session, char *cidnum, char *cidname, char *ich, char *lang, int wait);
extern int iax_accept(struct iax_session *session,int format);
extern int iax_answer(struct iax_session *session);
extern int iax_sendurl(struct iax_session *session, char *url);
extern int iax_send_unlink(struct iax_session *session);
extern int iax_send_link_reject(struct iax_session *session);
extern int iax_ring_announce(struct iax_session *session);
extern struct sockaddr_in iax_get_peer_addr(struct iax_session *session);
extern int iax_register(struct iax_session *session, char *hostname, char *peer, char *secret, int refresh);
extern int iax_lag_request(struct iax_session *session);
extern int iax_dial(struct iax_session *session, char *number);	/* Dial on a TBD call */
extern int iax_dialplan_request(struct iax_session *session, char *number);	/* Request dialplan status for number */
extern int iax_quelch(struct iax_session *session);
extern int iax_unquelch(struct iax_session * session);
extern int iax_transfer(struct iax_session *session, char *number);  

extern void iax_destroy(struct iax_session  * session);

extern void iax_enable_debug(void);
extern void iax_disable_debug(void);

void iax_set_private(struct iax_session *s, void *pvt);
void *iax_get_private(struct iax_session *s);
void iax_set_sendto(struct iax_session *s, sendto_t sendto);

/* Handle externally received frames */
struct iax_event *iax_net_process(unsigned char *buf, int len, struct sockaddr_in *sin);
/* this function allows you to use libiax for a server */
int iax_send_regauth(struct iax_session *session, int authmethod);
int iax_send_authreq(struct iax_session *session, int authmethod);
int iax_send_regack(struct iax_session *session);
int iax_send_regrej(struct iax_session *session);

/* we need this low level function */
int iax_send(struct iax_session *pvt, struct ast_frame *f, unsigned int ts, int seqno, int now, int transfer, int final);

#endif /* _ASTERISK_IAX_CLIENT_H */
