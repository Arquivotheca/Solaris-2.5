/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#ident	"@(#)message.c 1.0 91/02/10 SMI"

#ident	"@(#)message.c 1.27 94/08/10"

#include "operd.h"
#include <time.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <syslog.h>
#include <grp.h>
#include <pwd.h>
#include <utmp.h>

struct msg_cache *msgcache[NPIDS][NSEQ];	/* message cache */

static struct msg_cache	timeorder;		/* time ordered list */
static struct msg_cache	*msgfreelist;		/* free-list */
static struct group *gp;			/* group operator entry */

int	busy;					/* lock on msg cache (global) */

#ifdef __STDC__
static void doack(int, msg_t *, CLIENT *);
static void doreply(msg_t *);
static void rm_msg(struct msg_cache *);
static struct msg_cache *find_msg(msg_id *);
static int cmp_msg(msg_id *, msg_id *);
static void set_operators(void);
#ifndef USG
static void setutent(void);
static struct utmp *getutent(void);
static void endutent(void);
#endif
static void unhang(int);
static void broadcast(msg_t *);
static void sendmes(char *, msg_t *);
static void badreply(char *, msg_dest *, msg_t *, char *);
static char *lctime(time_t *);
#else
static void doack();
static void doreply();
static void rm_msg();
static struct msg_cache *find_msg();
static int cmp_msg();
static void set_operators();
#ifndef USG
static void setutent();
static struct utmp *getutent();
static void endutent();
#endif
static void unhang();
static void broadcast();
static void sendmes();
static void badreply();
static char *lctime();
#endif

extern sigjmp_buf	connbuf;

/*ARGSUSED*/
void
badconn(sig)
	int	sig;
{
	if (ready)
		siglongjmp(connbuf, 1);
	(void) syslog(LOG_CRIT, gettext("Received SIGPIPE: aborting!\n"));
	finish(-1);
}

/*
 * Intialize message cache
 */
void
init_msg(void)
{
	struct sigvec sv;

	ready = 0;
#ifdef USG
	(void) sigemptyset(&sv.sv_mask);
	sv.sv_flags = SA_RESTART;
#else
	sv.sv_mask = 0;
	sv.sv_flags = 0;
#endif
	sv.sv_handler = badconn;
	(void) sigvec(SIGPIPE, &sv, (struct sigvec *)0);
	timeorder.mc_nextrcvd = &timeorder;
	timeorder.mc_prevrcvd = &timeorder;
}

/*
 * Handle an incoming message.
 * NB:  Most of the message type bits are not exclusive.
 */
void
domsg(msgp)
	msg_t	*msgp;
{
	struct msg_cache *mc = find_msg(&msgp->msg_ident);

	/*
	 * Forward messages not directed at specific destinations
	 * to logged-in operator daemons and user processes.
	 * Replies are "directed" messages and are delivered
	 * to their destinations individually (in doreply).
	 * NB: an operator daemon should never see an ACK,
	 * NACK or NOTOP.
	 */
	if (mc && (msgp->msg_type & MSG_FORWARD))
		forward_msg(&mc->mc_msg);
	if (msgp->msg_type & MSG_OPERATOR)
		broadcast(msgp);
	if (msgp->msg_type & MSG_SYSLOG)
		(void) syslog(msgp->msg_level, msgp->msg_data);
	if (msgp->msg_type & MSG_REPLY)
		doreply(msgp);
	else if (msgp->msg_type & MSG_CANCEL) {
		if (msgp->msg_type & MSG_FORWARD)
			forward_msg(msgp);
		mc = find_msg(&msgp->msg_target);
		if (mc)
			rm_msg(mc);
	}
}

static void
doack(type, target, clnt)
	int	type;		/* MSG_ACK, MSG_NACK, or MSG_NOTOP */
	msg_t	*target;	/* target msg of ack/nack */
	CLIENT	*clnt;		/* connection on which to send */
{
	msg_t ack;
	int *res;

	(void) time(&ack.msg_time);
	ack.msg_ttl = 1;
	ack.msg_target = target->msg_target;	/* ack to original message */
	ack.msg_ident.mid_seq = 0L;		/* not applicable */
	ack.msg_ident.mid_pid = (u_long)getpid();
	(void) strncpy(ack.msg_ident.mid_host, thishost,
	    sizeof (ack.msg_ident.mid_host));
	(void) strncpy(ack.msg_progname, thisprogram,
	    sizeof (ack.msg_progname));
	(void) strncpy(ack.msg_arbiter, thishost, sizeof (ack.msg_arbiter));
	ack.msg_callback = 0;
	ack.msg_auth = 0;
	ack.msg_level = 0;
	ack.msg_type = type;
	ack.msg_len = 0;

#ifdef DEBUG
	debug("Outgoing %s: %lu@%s[%lu]; target ID: %lu@%s[%lu]\n",
	    ack.msg_type & MSG_ACK ? "ACK" :
		ack.msg_type & MSG_NACK ? "NACK" : "NOTOP",
	    ack.msg_ident.mid_seq,
	    ack.msg_ident.mid_host,
	    (u_long)ack.msg_ident.mid_pid,
	    ack.msg_target.mid_seq,
	    ack.msg_target.mid_host,
	    (u_long)ack.msg_target.mid_pid);
	debug("Type %x; Arbiter %s; Program %s\n",
	    ack.msg_type, ack.msg_arbiter, ack.msg_progname);
#endif
	if (sigsetjmp(connbuf, 1) == 0) {
		ready = 1;
		res = NULL;
		res = oper_send_1(&ack, clnt);
	}
	ready = 0;
	if (res == NULL || *res < 0) {
		errormsg(LOG_WARNING, gettext(
		    "WARNING: cannot send acknowlegement to %lu@%s %s\n"),
		    (u_long)ack.msg_ident.mid_pid,
		    ack.msg_ident.mid_host,
		    clnt_sperror(clnt, ""));
		return;
	}
}

/*
 * Receive and process a reply -- if the target of the reply has
 * not yet been responded to and the owner of the reply is allowed
 * to respond to the target, forward the reply and send an ACK,
 * otherwise send either a NOTOP or NACK and return.
 */
static void
doreply(reply)
	msg_t	*reply;
{
	time_t current_time = time((time_t *)0);
	struct msg_cache *request = find_msg(&reply->msg_target);
	struct fwdent *respfwd;
	struct rpc_err rerror;
	msg_dest responder;
	CLIENT *prompt_clnt;	/* client handle for guy issuing prompt */
	CLIENT *resp_clnt;	/* client handle for guy issuing response */
	register char **cpp;
	char buf[256];
	int retries = 0;
	int *res;

	if (!request) {
#ifdef DEBUG
		debug("Message %d@%s[%lu] deleted prior to reply\n",
		    reply->msg_target.mid_seq,
		    reply->msg_target.mid_host,
		    (u_long)reply->msg_target.mid_pid);
#endif
		return;		/* target not in cache */
	}

	if ((request->mc_status & EXPIRED) ||
	    (request->mc_msg.msg_ttl &&
	    current_time >= (request->mc_rcvd + request->mc_msg.msg_ttl))) {
		/*
		 * Too late -- message has expired
		 */
#ifdef DEBUG
		debug("Message %lu@%s[%lu] expired prior to reply\n",
		    reply->msg_target.mid_seq,
		    reply->msg_target.mid_host,
		    (u_long)reply->msg_target.mid_pid);
#endif
		rm_msg(request);
		return;
	}

	responder.md_callback = reply->msg_callback;
	(void) strncpy(responder.md_host, reply->msg_ident.mid_host,
		sizeof (responder.md_host));
	(void) strncpy(responder.md_domain, reply->msg_ident.mid_domain,
		sizeof (responder.md_domain));

	/*
	 * We only handle replies to messages for
	 * which we are the designated arbitrator.
	 * Check all our various names.
	 */
	for (cpp = namelist; cpp && *cpp; cpp++) {
		if (strcmp(*cpp, request->mc_msg.msg_arbiter) == 0)
			break;
	}
	if (cpp == NULL) {
		(void) sprintf(buf,
		    gettext(": %s specified"), request->mc_msg.msg_arbiter);
		badreply(gettext("arbiter"), &responder, reply, buf);
		return;
	}

	/*
	 * Replies originating outside the target
	 * message's domain are not forwarded unless they're
	 * in the domainlist.
	 */
	if (strcmp(reply->msg_ident.mid_domain,
	    request->mc_msg.msg_ident.mid_domain)) {
		char **dp = (char **)0;

		if (domainlist)
			for (dp = domainlist; *dp; dp++)
				if (strcmp(reply->msg_ident.mid_domain,
				    *dp) == 0)
					break;
		if (dp == (char **)0) {
			(void) sprintf(buf, gettext(": %s specified"),
			    request->mc_msg.msg_ident.mid_domain);
			badreply(gettext("domain"), &responder, reply, buf);
			return;
		}
	}

	/*
	 * Attempt to forward the reply if it is
	 * the first to be received.
	 */
	prompt_clnt = (CLIENT *)0;
	if ((request->mc_status & GOTREPLY) == 0) {
		prompt_clnt = oper_connect(request->mc_msg.msg_ident.mid_host,
		    request->mc_msg.msg_callback);
		if (prompt_clnt == NULL) {
			errormsg(LOG_WARNING, gettext(
		    "REPLY ERROR (%d@%s.%s: %lu@%s[%lu]->%lu@%s[%lu])%s\n"),
			    responder.md_callback,
			    responder.md_host,
			    responder.md_domain,
			    reply->msg_ident.mid_seq,
			    reply->msg_ident.mid_host,
			    (u_long)reply->msg_ident.mid_pid,
			    reply->msg_target.mid_seq,
			    reply->msg_target.mid_host,
			    (u_long)reply->msg_target.mid_pid,
			    clnt_spcreateerror(" "));
			return;
		}
#ifdef DEBUG
		debug("Outgoing reply: %lu@%s[%lu]; target ID: %lu@%s[%lu]\n",
		    reply->msg_ident.mid_seq,
		    reply->msg_ident.mid_host,
		    (u_long)reply->msg_ident.mid_pid,
		    reply->msg_target.mid_seq,
		    reply->msg_target.mid_host,
		    (u_long)reply->msg_target.mid_pid);
		debug("Type %x; Arbiter %s; Program %s\n",
		    reply->msg_type, reply->msg_arbiter, reply->msg_progname);
		debug("Reply text: %s", reply->msg_data);
#endif

retry:
		if (sigsetjmp(connbuf, 1) == 0) {
			ready = 1;
			res = NULL;
			res = oper_send_1(reply, prompt_clnt);
			if (res == NULL || *res < 0)
				clnt_geterr(prompt_clnt, &rerror);
		}
		ready = 0;
	}

	respfwd = find_fwd(&responder);

	if (respfwd == NULL || respfwd->f_clnt == NULL) {
		resp_clnt =
		    oper_connect(responder.md_host, responder.md_callback);
		if (!resp_clnt) {
			badreply(gettext("ack/nack"), &responder,
			    reply, clnt_spcreateerror(" "));
		} else if (respfwd)
			respfwd->f_clnt = resp_clnt;
	} else
		resp_clnt = respfwd->f_clnt;

	if (resp_clnt && (request->mc_status & GOTREPLY) == 0) {
		if (res == NULL || *res < 0) {
			if (rerror.re_status == RPC_AUTHERROR) {
				(void) sprintf(buf,
				    gettext(": user-ID %ld, group-ID %ld"),
				    (long)reply->msg_uid, (long)reply->msg_gid);
				badreply(gettext("authentication"), &responder,
				    reply, buf);
				doack(MSG_NOTOP, reply, resp_clnt);
			} else if (rerror.re_status == RPC_TIMEDOUT) {
#ifdef DEBUG
				debug("Reply timeout to %d@%s[%lu]: retrying\n",
				    reply->msg_target.mid_seq,
				    reply->msg_target.mid_host,
				    (u_long)reply->msg_target.mid_pid);
#endif
				(void) syslog(LOG_DEBUG,
				    gettext("reply timed out: retry %d\n"),
					retries);
				if (++retries <= MAXRETRY)
					goto retry;
			} else {
				badreply(gettext("forward"), &responder,
				    reply, clnt_sperror(resp_clnt, ""));
				doack(MSG_NACK, reply, resp_clnt);
			}
		} else {
			doack(MSG_ACK, reply, resp_clnt);
			request->mc_status |= GOTREPLY;
		}
	} else
		doack(MSG_NACK, reply, resp_clnt);
	/*
	 * Break down one-time connections
	 */
	if (respfwd == NULL && resp_clnt)
		clnt_destroy(resp_clnt);
	if (prompt_clnt)
		clnt_destroy(prompt_clnt);
}

/*
 * Send all cached messages (in time order) received
 * more recently than the supplied timestamp to a client.
 * This occurs after a client logs in, or when an RPC
 * SENDALL command is received.
 */
void
send_all(dest)
	msg_dest *dest;
{
	time_t current_time = time((time_t *)0);
	struct fwdent *f = find_fwd(dest);
	register struct msg_cache *mc, *next;
	struct sigvec sv;
	msg_t	*msgp;
	int	isdaemon =
	    dest->md_callback == 0 || dest->md_callback == OPERMSG_PROG ? 1 : 0;
	int	*res;
	pid_t	pid;

	if (f == NULL || f->f_clnt == NULL)
		return;

	pid = fork();
	if (pid < 0) {
		(void) syslog(LOG_CRIT, gettext("fork failed\n"));
		exit(-1);
	} else if (pid)
		return;
	/*
	 * Don't expire in child
	 */
#ifdef USG
	(void) sigemptyset(&sv.sv_mask);
	sv.sv_flags = SA_RESTART;
#else
	sv.sv_mask = 0;
	sv.sv_flags = 0;
#endif
	sv.sv_handler = SIG_IGN;
	(void) sigvec(SIGALRM, &sv, (struct sigvec *)0);
	itv.it_interval.tv_sec = 0;
	itv.it_interval.tv_usec = 0;
	itv.it_value.tv_sec = 0;
	itv.it_value.tv_usec = 0;
	(void) setitimer(ITIMER_REAL, &itv, (struct itimerval *)0);

#ifdef DEBUG
	debug("SENDALL to %d@%s\n", dest->md_callback, dest->md_host);
#endif
	if (!isdaemon)
		for (mc = timeorder.mc_nextrcvd;
		    mc != &timeorder && mc->mc_rcvd <= dest->md_gen;
		    mc = mc->mc_nextrcvd)
			;
	else
		mc = timeorder.mc_nextrcvd;

	(void) bzero((char *)&itv, sizeof (itv));
	(void) setitimer(ITIMER_REAL, &itv, (struct itimerval *)0);

	itv.it_value.tv_sec = RPCTIMEOUT;

#ifdef lint
	next = timeorder.mc_nextrcvd;
#endif
	for (; mc != &timeorder; mc = next) {
		next = mc->mc_nextrcvd;
		msgp = &mc->mc_msg;
		if ((mc->mc_status & EXPIRED) || (msgp->msg_ttl &&
		    current_time >= (mc->mc_rcvd + msgp->msg_ttl))) {
			rm_msg(mc);
			continue;
		}
#ifdef DEBUG
		debug("Forward msg %lu@%s[%lu] ",
		    msgp->msg_ident.mid_seq,
		    msgp->msg_ident.mid_host,
		    (u_long)msgp->msg_ident.mid_pid);
#endif
		sv.sv_handler = badconn;
		(void) sigvec(SIGALRM, &sv, (struct sigvec *)0);
		(void) setitimer(ITIMER_REAL, &itv, NULL);
		if (sigsetjmp(connbuf, 1) == 0) {
			ready = 1;
			res = oper_send_1(msgp, f->f_clnt);
		} else {
			/*
			 * If the connection is broken, abort
			 * the message transfer process.
			 */
#ifdef DEBUG
			debug("Aborting cache transfer to daemon %d@%s\n",
			    dest->md_callback, dest->md_host);
#endif
			break;
		}
		ready = 0;
		if (res == NULL || *res < 0) {
#ifdef DEBUG
			debug("failed%s\n", clnt_sperror(f->f_clnt, ""));
#endif
			if (isdaemon) {
#ifdef DEBUG
				debug(gettext(
				    "Aborting cache transfer to %d@%s\n"),
					dest->md_callback, dest->md_host);
#endif
				break;
			}
		}
#ifdef DEBUG
		else
			debug("succeeded\n");
#endif
	}
	exit(0);
}

/*
 * Add a message to the cache
 */
int
add_msg(msgp)
	msg_t	*msgp;
{
	struct msg_cache *mc, **bucket;

#ifdef DEBUG
	debug("Incoming ID: %lu@%s[%lu]; target ID: %lu@%s[%lu]\n",
	    msgp->msg_ident.mid_seq,
	    msgp->msg_ident.mid_host,
	    (u_long)msgp->msg_ident.mid_pid,
	    msgp->msg_target.mid_seq,
	    msgp->msg_target.mid_host,
	    (u_long)msgp->msg_target.mid_pid);
	debug("Type %x; Arbiter %s; Program %s\n",
	    msgp->msg_type, msgp->msg_arbiter, msgp->msg_progname);
	debug("Text: %s", msgp->msg_data);
#endif
	/*
	 * Only messages not directed at specific targets
	 * are added to the cache.
	 */
	if (HASTARGET(msgp->msg_type))
		return (0);

	if (find_msg(&msgp->msg_ident)) {	/* duplicate */
#ifdef DEBUG
		debug("Duplicate message %lu@%s[%lu] received\n",
		    msgp->msg_ident.mid_seq,
		    msgp->msg_ident.mid_host,
		    (u_long)msgp->msg_ident.mid_pid);
#endif
		return (-1);
	}

	/*
	 * There are three places we can obtain storage:
	 *   - the free list of cancelled/expired messages
	 *   - dynamic memory allocation
	 *   - the oldest active message
	 * The oldest active message is only used if the
	 * number of cached messages is equal to or greater
	 * than the specified maximum.
	 */
	if (msgfreelist) {
		mc = msgfreelist;
		msgfreelist = msgfreelist->mc_nextrcvd;
	} else if (maxcache == 0 || msgcnt < maxcache) {
		msgcnt++;
		mc = (struct msg_cache *)
		    malloc((unsigned)sizeof (struct msg_cache));
		if (mc == NULL) {
			perror("malloc");
			return (-1);
		}
	} else {
		mc = timeorder.mc_nextrcvd;
		/*
		 * remove from time-ordered list
		 */
		mc->mc_nextrcvd->mc_prevrcvd = mc->mc_prevrcvd;
		mc->mc_prevrcvd->mc_nextrcvd = mc->mc_nextrcvd;
		/*
		 * remove from hash bucket
		 */
		if (mc->mc_prevhash)
			mc->mc_prevhash->mc_nexthash = mc->mc_nexthash;
		if (mc->mc_nexthash)
			mc->mc_nexthash->mc_prevhash = mc->mc_prevhash;
		bucket = &HASHMSG(&mc->mc_msg.msg_ident);
		if (*bucket == mc)
			*bucket = NULL;
	}
	mc->mc_msg = *msgp;
	(void) time(&mc->mc_rcvd);
	mc->mc_status = 0;
	msgp = &mc->mc_msg;
	/*
	 * Add to cache at beginning of appropriate bucket
	 */
	bucket = &HASHMSG(&msgp->msg_ident);
	if (*bucket == NULL) {
		mc->mc_nexthash = NULL;
		mc->mc_prevhash = NULL;
		*bucket = mc;
	} else {
		mc->mc_nexthash = (*bucket)->mc_nexthash;
		mc->mc_prevhash = *bucket;
		(*bucket)->mc_nexthash = mc;
	}
	/*
	 * Add to time-ordered receipt list
	 */
	mc->mc_nextrcvd = &timeorder;
	mc->mc_prevrcvd = timeorder.mc_prevrcvd;
	timeorder.mc_prevrcvd->mc_nextrcvd = mc;
	timeorder.mc_prevrcvd = mc;
#ifdef DEBUG
	debug("Message %lu@%s[%lu] added to cache\n",
	    msgp->msg_ident.mid_seq,
	    msgp->msg_ident.mid_host,
	    (u_long)msgp->msg_ident.mid_pid);
#endif
	return (0);
}

/*
 * Make a pass through all cached messages and throw away
 * those that have expired.  This occurs every 60 seconds.
 */
void
expire_all(void)
{
	register struct msg_cache *mc, *next;
	register msg_t	*msgp;
	time_t current_time = time((time_t *)0);

#ifdef DEBUG
	debug("expire_all() %sing expired messages\n",
	    busy ? "mark" : "remov");
#endif
#ifdef lint
	next = timeorder.mc_nextrcvd;
#endif
	for (mc = timeorder.mc_nextrcvd; mc != &timeorder; mc = next) {
		next = mc->mc_nextrcvd;
		msgp = &mc->mc_msg;
		if (msgp->msg_ttl &&
		    current_time >= (mc->mc_rcvd + msgp->msg_ttl)) {
			mc->mc_status |= EXPIRED;
			if (!busy)
				rm_msg(mc);
		}
	}
}

/*
 * Delete a message from the cache
 */
static void
rm_msg(mc)
	struct msg_cache *mc;
{
	struct msg_cache **bucket;

#ifdef DEBUG
	debug("removing message %lu@%s[%lu]\n",
	    mc->mc_msg.msg_ident.mid_seq,
	    mc->mc_msg.msg_ident.mid_host,
	    (u_long)mc->mc_msg.msg_ident.mid_pid);
#endif
	busy = 1;
	if (mc->mc_status & REMOVED) {
		busy = 0;
		return;
	}
	/*
	 * remove from time-ordered list
	 */
	mc->mc_nextrcvd->mc_prevrcvd = mc->mc_prevrcvd;
	mc->mc_prevrcvd->mc_nextrcvd = mc->mc_nextrcvd;
	/*
	 * remove from active cache
	 */
	if (mc->mc_prevhash)
		mc->mc_prevhash->mc_nexthash = mc->mc_nexthash;
	if (mc->mc_nexthash)
		mc->mc_nexthash->mc_prevhash = mc->mc_prevhash;
	bucket = &HASHMSG(&mc->mc_msg.msg_ident);
	if (*bucket == mc)
		*bucket = NULL;
	/*
	 * add to free list
	 */
	mc->mc_nextrcvd = msgfreelist;
	msgfreelist = mc;
	mc->mc_status |= REMOVED;
	busy = 0;
}

/*
 * Locate a message in the cache
 */
static struct msg_cache *
find_msg(id)
	msg_id *id;
{
	register struct msg_cache *mc = NULL;
	struct msg_cache *bucket;

	busy = 1;
	bucket = HASHMSG(id);
	if (bucket) {
		for (mc = bucket; mc; mc = mc->mc_nexthash)
			if (cmp_msg(id, &mc->mc_msg.msg_ident) == 0)
				break;
	}
	busy = 0;
	if (mc == &timeorder)
		mc = NULL;
	return (mc);
}

static int
cmp_msg(id1, id2)
	msg_id *id1, *id2;
{
	if (id1->mid_pid != id2->mid_pid)
		return (1);
	if (id1->mid_seq != id2->mid_seq)
		return (1);
	if (strcmp(id1->mid_host, id2->mid_host))
		return (1);
	return (0);
}

/*
 * Get the names of operators to notify from
 * the group entry "operator".
 */
static void
set_operators(void)
{
	gp = getgrnam(OPGRENT);
	(void) endgrent();
	if (gp == (struct group *) 0) {
		errormsg(LOG_ERR, gettext("No entry in /etc/group for %s.\n"),
		    OPGRENT);
		return;
	}
}

#ifndef USG
static FILE *f_utmp = NULL;

static void
setutent(void)
{
	f_utmp = fopen("/etc/utmp", "r");
}

static struct utmp *
getutent(void)
{
	static struct utmp utmp;

	if (gp == 0)
		return (NULL);
	if (f_utmp == NULL) {
		perror("/etc/utmp");
		return (NULL);
	}
	if (fread((char *)&utmp, sizeof (utmp), 1, f_utmp) == 1)
		return (&utmp);
	return (NULL);
}

static void
endutent(void)
{
	(void) fclose(f_utmp);
	f_utmp = NULL;
}
#endif

struct tm *localtime();
static struct tm *localclock;

#include <setjmp.h>
static sigjmp_buf sjbuf;

/*ARGSUSED*/
static void
unhang(sig)
	int	sig;
{
	siglongjmp(sjbuf, 1);
}

/*
 * We fork a child to do the actual broadcasting, so
 * that the process control groups are not messed up
 */
static void
broadcast(msgp)
	msg_t	*msgp;
{
	struct sigvec sv;
	struct utmp *utp;
	char	**np;

	set_operators();
	if (gp == 0)
		return;
	switch (fork()) {
	case -1:
		return;
	case 0:
		/*
		 * Don't expire in child
		 */
#ifdef USG
		(void) sigemptyset(&sv.sv_mask);
		sv.sv_flags = SA_RESTART;
#else
		sv.sv_mask = 0;
		sv.sv_flags = 0;
#endif
		sv.sv_handler = SIG_IGN;
		(void) sigvec(SIGALRM, &sv, (struct sigvec *)0);
		itv.it_interval.tv_sec = 0;
		itv.it_interval.tv_usec = 0;
		itv.it_value.tv_sec = 0;
		itv.it_value.tv_usec = 0;
		(void) setitimer(ITIMER_REAL, &itv, (struct itimerval *)0);
		break;
	default:
		/* don't wait for completion */
		return;
	}

	localclock = localtime(&msgp->msg_time);

	setutent();
	while ((utp = getutent()) != NULL) {
		if (utp->ut_name[0] == 0)
			continue;
		for (np = gp->gr_mem; *np; np++) {
			if (strncmp(*np, utp->ut_name,
			    sizeof (utp->ut_name)) != 0)
				continue;
#ifdef DEBUG
			(void) fprintf(stderr, "Message to %s at %s\n",
			    utp->ut_name, utp->ut_line);
#endif /* DEBUG */
			if (sigsetjmp(sjbuf, 1) != 0)
				continue;
			sv.sv_handler = unhang;
			(void) sigvec(SIGALRM, &sv, (struct sigvec *)0);
			(void) alarm(10);
			sendmes(utp->ut_line, msgp);
			(void) alarm(0);
		}
	}
	endutent();
	exit(0);	/* the wait in the dispatch routine will catch this */
	/* NOTREACHED */
}

static void
sendmes(tty, msgp)
	char *tty;
	msg_t *msgp;
{
	char t[50], buf[BUFSIZ];
	register char *cp;
	register int c, ch;
	int	msize;
	FILE *f_tty;

	msize = msgp->msg_len;
	(void) strcpy(t, "/dev/");
	(void) strcat(t, tty);

	if ((f_tty = fopen(t, "w")) != NULL) {
		setbuf(f_tty, buf);
		(void) fprintf(f_tty, gettext(
"\nMessage from program %s on %s to all operators at %d:%02d ...\r\n\n"),
		    msgp->msg_progname, msgp->msg_ident.mid_host,
		    localclock->tm_hour, localclock->tm_min);
		for (cp = msgp->msg_data, c = msize; c-- > 0; cp++) {
			ch = *cp;
			if (ch == '\n')
				if (putc('\r', f_tty) == EOF)
					break;
			if (putc(ch, f_tty) == EOF)
				break;
		}
		(void) fclose(f_tty);
	}
}

/*
 * Dump all cached messages (in time order) to
 * file in /var/tmp.
 */
void
dump_all(void)
{
	char dumpfile[MAXPATHLEN];
	register struct msg_cache *mc;
	time_t current_time = time((time_t *)0);
	time_t exp_time;
	char   timestr[3][26];
	FILE *fp;

#ifdef DEBUG
	debug("Dumping msg cache\n");
#endif
	(void) sprintf(dumpfile, "%s/%s", tmpdir, DUMPFILE);
	if ((fp = fopen(dumpfile, "w")) == NULL) {
		perror("fopen operd.dump");
		return;
	}
	(void) fprintf(fp, gettext("Message cache (%d) as of %s"),
	    msgcnt, lctime(&current_time));
	busy = 1;
	for (mc = timeorder.mc_nextrcvd;
	    mc != &timeorder; mc = mc->mc_nextrcvd) {
		register msg_t *msgp = &mc->mc_msg;

		(void) fprintf(fp, "--------\n");
		(void) fprintf(fp,
		    gettext("Msg ID: %lu@%s[%lu]; target ID: %lu@%s[%lu]\n"),
			msgp->msg_ident.mid_seq,
			msgp->msg_ident.mid_host,
			(u_long)msgp->msg_ident.mid_pid,
			msgp->msg_target.mid_seq,
			msgp->msg_target.mid_host,
			(u_long)msgp->msg_target.mid_pid);
		(void) fprintf(fp,
		    gettext("Type %lx; Arbiter %s; Program %s\n"),
			msgp->msg_type, msgp->msg_arbiter, msgp->msg_progname);
		exp_time = mc->mc_rcvd + msgp->msg_ttl;
		(void) strcpy(timestr[0], lctime(&msgp->msg_time));
		(void) strcpy(timestr[1], lctime(&mc->mc_rcvd));
		(void) strcpy(timestr[2], lctime(&exp_time));
		(void) fprintf(fp, gettext(
		    "Times:\n\tSent:\t\t%s\tReceived:\t%s\tExpires:\t%s"),
			timestr[0], timestr[1], timestr[2]);
		(void) fprintf(fp, gettext(
		    "Cache stats: forward %s, replied %s, expired %s\n"),
			mc->mc_status & FORWARD ? "yes" : "no",
			mc->mc_status & GOTREPLY ? "yes" : "no",
			mc->mc_status & EXPIRED ? "yes" : "no");
		if (msgp->msg_type & MSG_DISPLAY)
			(void) fprintf(fp, gettext("Text: %s"), msgp->msg_data);
	}
	busy = 0;
	(void) fclose(fp);
}

static void
badreply(str1, responder, reply, str2)
	char	*str1, *str2;
	msg_dest	*responder;
	msg_t		*reply;
{
	errormsg(LOG_WARNING, gettext(
	    "REPLY (%s) ERROR! (%lu@%s.%s: %lu@%s[%lu]->%lu@%s[%lu])%s\n"),
	    str1,
	    responder->md_callback,
	    responder->md_host,
	    responder->md_domain,
	    reply->msg_ident.mid_seq,
	    reply->msg_ident.mid_host,
	    (u_long)reply->msg_ident.mid_pid,
	    reply->msg_target.mid_seq,
	    reply->msg_target.mid_host,
	    (u_long)reply->msg_target.mid_pid,
	    str2);
}

static char *
lctime(timep)
	time_t	*timep;
{
	static char buf[256];
	struct tm *tm;

	tm = localtime(timep);
	(void) strftime(buf, sizeof (buf), "%c\n", tm);
	return (buf);
}
