/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

/*	@(#)operd.h 1.0 91/02/10 SMI */

/*	@(#)operd.h 1.16 93/10/05 */

#include <stdio.h>
#include <locale.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <rpc/rpc.h>
#include <setjmp.h>
#include <config.h>
#include "operator.h"

/*
 * The message cache structure.  All messages received by the daemon
 * are cached.  The cache structure is actually two separate lists:
 * a hashed list for quick look-up and a time ordered list for sending
 * to newly logged-in processes.
 */
struct msg_cache {
	time_t		mc_rcvd;	/* time received */
	msg_t		mc_msg;		/* the cached message */
	int		mc_status;	/* status flags, see below */
	struct msg_cache *mc_nexthash;	/* next in this bucket */
	struct msg_cache *mc_prevhash;	/* previous in this bucket */
	struct msg_cache *mc_nextrcvd;	/* next in time order */
	struct msg_cache *mc_prevrcvd;	/* previous in time order */
};
/*
 * Status flags
 */
#define	FORWARD		0x1		/* reflects MSG_FORWARD on receipt */
#define	GOTREPLY	0x2		/* =1 when 1st response received */
#define	EXPIRED		0x4		/* expired but not yet removed */
#define	REMOVED		0x8		/* removed and on free list */

/*
 * Message status flags
 */
#define	ACKED		1	/* acknowleged */

/*
 * The forward list structure.  There are actually two lists:  a
 * list of clients to whom login calls should be forwarded (the forward
 * list) and a list of clients to whom incoming messages should be sent
 * (the destination list).  The former contains a small number of
 * operator daemons (specified via "forward" entries in this daemon's
 * configuration file and the latter includes the forward list plus
 * any logged-in programs (such as status monitors).
 */
struct fwdent {
	CLIENT		*f_clnt;	/* client for RPC connection */
	msg_dest	f_dest;		/* destination identifier */
	u_long		f_errors;	/* error count */
	struct fwdent	*f_nextdest;	/* next on message destination list */
	struct fwdent	*f_prevdest;	/* previous on destination list */
	struct fwdent	*f_nextfwd;	/* next on login call forward list */
	struct fwdent	*f_prevfwd;	/* previous on forward list */
};

#define	MAXERRS		20		/* remove after n consequtive errors */
#define	MAXRETRY	5		/* maximum number of retry attempts */

#ifdef DEBUG
#define	NPIDS		0x10	/* must be power of 2 */
#define	NSEQ		0x10	/* must be power of 2 */
#else
#define	NPIDS		0x100	/* must be power of 2 */
#define	NSEQ		0x100	/* must be power of 2 */
#endif
#define	PIDMASK		NPIDS-1
#define	SEQMASK		NSEQ-1
#define	HASHMSG(id)	\
		msgcache[((id)->mid_pid)&PIDMASK][((id)->mid_seq)&SEQMASK]

#define	NFWD		1019	/* should be prime */

#define	OPGRENT		"operator"
#ifdef DEBUG
#undef TMPDIR
#define	TMPDIR		"."
#endif
#define	DUMPFILE	"operd.dump"

#ifdef USG
#define	sigvec		sigaction	/* struct and function */
#define	sv_flags	sa_flags
#define	sv_mask		sa_mask
#define	sv_handler	sa_handler
#define	setjmp(b)	sigsetjmp((b), 1)
#define	longjmp		siglongjmp
#define	jmp_buf		sigjmp_buf

#define	bzero(s, len)	memset((s), 0, (len))
#endif

struct fwdent forwardlist;		/* [login call] forward list */
struct fwdent destinations[NFWD];	/* [msg] destination list */
struct msg_cache *msgcache[NPIDS][NSEQ];	/* message cache */

char	thishost[BCHOSTNAMELEN];	/* our host name */
char	thisdomain[BCHOSTNAMELEN];	/* our domain name */
char	thisprogram[MAXIDLEN];		/* our program name */
int	dobroadcast;			/* enables login rebroadcasting */
int	dogateway;			/* enables login forwarding */
int	maxcache;			/* upper bound on cached messages */
int	msgcnt;				/* number of cached messages */
int	busy;				/* linked list lock */
char	**namelist;			/* list of names we go by */
char	**domainlist;			/* list of equivalent domains */
int	xflag;				/* turn on output messages */
char	*tmpdir;			/* temporary directory */
int	ready;

struct	itimerval itv, otv;		/* for interval timer */
#define	RPCTIMEOUT	5		/* RPC timeout */

#ifdef __STDC__
extern void advertise(u_long);
extern void make_fwd(char *);
extern int add_fwd(msg_dest *, int);
extern void forward_log(msg_dest *, u_long);
extern void forward_msg(msg_t *);
extern struct fwdent *find_fwd(msg_dest *);
extern void rm_fwd(struct fwdent *);
extern void init(int, char **);
extern void finish(int);
extern void errormsg(int, const char *, ...);
#ifdef DEBUG
extern void debug(const char *, ...);
#endif
extern void badconn(int);
extern void init_msg(void);
extern void domsg(msg_t *);
extern void send_all(msg_dest *);
extern int add_msg(msg_t *);
extern void expire_all(void);
extern void dump_all(void);
/*
 * XXX broken header files
 */
extern void openlog(const char *, int, int);
extern void syslog(int, const char *, ...);
extern int socket(int domain, int type, int protocol);
#else
extern void advertise();
extern void make_fwd();
extern int add_fwd();
extern void forward_log();
extern void forward_msg();
extern struct fwdent *find_fwd();
extern void rm_fwd();
extern void init();
extern void finish();
extern void errormsg();
#ifdef DEBUG
extern void debug();
#endif
extern void badconn();
extern void init_msg();
extern void domsg();
extern void send_all();
extern int add_msg();
extern void expire_all();
extern void dump_all();
#endif
