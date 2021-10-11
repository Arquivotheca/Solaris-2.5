/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#ident	"@(#)forward.c 1.0 91/02/10 SMI"

#ident	"@(#)forward.c 1.17 92/04/13"

#include "operd.h"
#include <netdb.h>
#include <syslog.h>
#include <signal.h>
#include <rpc/pmap_clnt.h>

struct fwdent forwardlist;		/* [login call] forward list */
struct fwdent destinations[NFWD];	/* [msg] destination list */

static struct fwdent *lastfwd = &forwardlist;	/* last on forward list */
static struct fwdent *freefwd;			/* free list */

#ifdef __STDC__
static int cmp_dest(msg_dest *, msg_dest *);
static int hash_dest(msg_dest *);
#else
static int cmp_dest();
static int hash_dest();
#endif

extern sigjmp_buf	connbuf;

/*
 * Public routines
 */
void
advertise(prog)
	u_long	prog;
{
	msg_dest dest;
	int	saveb, saveg;

	(void) strcpy(dest.md_host, thishost);
	(void) strcpy(dest.md_domain, thisdomain);
	dest.md_callback = OPERMSG_PROG;
	(void) time(&dest.md_gen);
	saveb = dobroadcast++;		/* force broadcast */
	saveg = dogateway++;		/* force delivery to all on fwd list */
	forward_log(&dest, prog);
	dobroadcast = saveb;		/* restore state */
	dogateway = saveg;
}

void
make_fwd(line)
	char	*line;
{
	char		*hoststring;
	msg_dest	dest;

	hoststring = malloc(strlen(line));
	if (hoststring == NULL) {
		perror("malloc");
		finish(-1);
	}
	(void) sscanf(line, "%s", hoststring);

	dest.md_callback = OPERMSG_PROG;
	(void) strcpy(dest.md_host, hoststring);
	(void) strcpy(dest.md_domain, thisdomain);	/* assume our domain */
	(void) time(&dest.md_gen);
	(void) add_fwd(&dest, 1);
	free(hoststring);
}

add_fwd(dest, dologin)
	msg_dest *dest;			/* destination to add */
	int	dologin;		/* add to login call fwd list */
{
	struct fwdent *f, *fb;

	/*
	 * Don't add ourself
	 */
	if (strcmp(dest->md_host, thishost) == 0 &&
	    (dest->md_callback == 0 || dest->md_callback == OPERMSG_PROG)) {
#ifdef DEBUG
		debug("FILTER login %lu.%lu@%s: local call\n",
		    dest->md_callback, dest->md_gen, dest->md_host);
#endif
		return (-1);
	}
	/*
	 * Reject requests originating outside our
	 * domain if not in the list of domains to
	 * be considered equivalent (specified via
	 * "domain" commands in "operd.conf").
	 */
	if (strcmp(dest->md_domain, thisdomain)) {
		register char **dp = (char **)0;

		if (domainlist) {
			for (dp = domainlist; *dp; dp++)
				if (strcmp(dest->md_domain, *dp) == 0)
					break;
		}
		if (dp == (char **)0) {
#ifdef DEBUG
			debug("FILTER login %lu.%lu@%s: foreign domain\n",
				dest->md_callback,
				dest->md_gen,
				dest->md_host);
#endif
			return (-1);
		}
	}

	f = find_fwd(dest);
	if (f) {
		if (f->f_dest.md_gen == dest->md_gen) {
#ifdef DEBUG
			debug("FILTER login %lu.%lu@%s: duplicate call\n",
			    dest->md_callback, dest->md_gen, dest->md_host);
#endif
			return (-1);
		}
		f->f_dest.md_gen = dest->md_gen;	/* update */
		if (f->f_clnt)
			clnt_destroy(f->f_clnt); 	/* reconnect */
		f->f_clnt = oper_connect(dest->md_host, dest->md_callback);
		if (f->f_clnt == NULL) {
			(void) fprintf(stderr,
			    gettext("cannot RE-connect to program %lu@%s\n"),
			    dest->md_callback, dest->md_host);
			rm_fwd(f);
			return (-1);
		}
#ifdef DEBUG
		debug("return connection to %lu@%s RE-established\n",
		    dest->md_callback, dest->md_host);
#endif
		/*
		 * Already on at least the destination list.
		 * If not adding to forward list or if already
		 * on that list, just return.
		 */
		if (dologin == 0 || f->f_prevfwd)
			return (0);
		/*
		 * Add to forward list and return.
		 * This shouldn't ever happen.
		 */
#ifdef DEBUG
		debug("Adding %lu@%s to forward list\n",
		    dest->md_callback, dest->md_host);
#endif
		f->f_prevfwd = lastfwd;
		f->f_nextfwd = NULL;
		lastfwd->f_nextfwd = f;
		lastfwd = f;
		return (0);
	}
	if (freefwd) {
		f = freefwd;
		freefwd = freefwd->f_nextfwd;
	} else {
		f = (struct fwdent *) malloc(sizeof (struct fwdent));
		if (f == NULL) {
			perror("malloc");
			return (-1);
		}
	}
	f->f_dest = *dest;
	if (f->f_dest.md_callback == 0)
		f->f_dest.md_callback = OPERMSG_PROG;
	dest = &f->f_dest;
	f->f_errors = 0L;
	/*
	 * Add to destination list
	 */
	fb = &destinations[hash_dest(dest)];
	f->f_nextdest = fb->f_nextdest;
	fb->f_nextdest = f;
	if (f->f_nextdest)
		f->f_nextdest->f_prevdest = f;
	f->f_prevdest = fb;
	/*
	 * Add to forward list, if applicable
	 */
	if (dologin) {
		f->f_prevfwd = lastfwd;
		f->f_nextfwd = NULL;
		lastfwd->f_nextfwd = f;
		lastfwd = f;
	} else {
		f->f_prevfwd = NULL;
		f->f_nextfwd = NULL;
	}
	f->f_clnt = oper_connect(dest->md_host, dest->md_callback);
	if (f->f_clnt == NULL) {
		(void) fprintf(stderr,
		    gettext("cannot connect to program %lu on host %s\n"),
		    dest->md_callback, dest->md_host);
		rm_fwd(f);
		return (-1);
	}
#ifdef DEBUG
	debug("return connection to %lu@%s established\n",
	    dest->md_callback, dest->md_host);
	debug("Added %lu@%s to bucket %d\n",
	    dest->md_callback, dest->md_host, fb - destinations);
#endif
	return (0);
}

/*
 * Retransmit a login or logout call to all hosts on the forward
 * list (if retransmission is in effect) and rebroadcast
 * to all connected networks (if rebroadcast is in effect).
 */
void
forward_log(dest, func)
	msg_dest *dest;		/* contents of message */
	u_long	func;		/* OPER_LOGIN or OPER_LOGOUT */
{
	register struct fwdent *f, *next;
	struct sigvec sv, osv;
#ifdef DEBUG
	char	*str = (func == OPER_LOGIN) ? "login" : "logout";
#endif
	pid_t	pid;
	int	*res;

	if (!dogateway && !dobroadcast)
		return;
	pid = fork();
	if (pid < 0) {
		(void) syslog(LOG_CRIT, gettext("fork failed\n"));
		finish(-1);
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

	if (dogateway) {
#ifdef lint
		next = forwardlist.f_nextfwd;
#endif
		for (f = forwardlist.f_nextfwd; f; f = next) {
			next = f->f_nextfwd;
			if (cmp_dest(dest, &f->f_dest) == 0) {
#ifdef DEBUG
				debug("FILTER %s %lu.%lu@%s: same host\n",
				    str, f->f_dest.md_callback,
				    f->f_dest.md_gen, f->f_dest.md_host);
#endif
				continue;
			}
			itv.it_interval.tv_sec = 0;
			itv.it_interval.tv_usec = 0;
			itv.it_value.tv_sec = RPCTIMEOUT;
			itv.it_value.tv_usec = 0;
			(void) setitimer(ITIMER_REAL, &itv, &otv);
#ifdef USG
			(void) sigemptyset(&sv.sv_mask);
			sv.sv_flags = SA_RESTART;
#else
			sv.sv_mask = 0;
			sv.sv_flags = 0;
#endif
			sv.sv_handler = badconn;
			(void) sigvec(SIGALRM, &sv, &osv);
			if (sigsetjmp(connbuf, 1) == 0) {
				ready = 1;
				res = NULL;
#ifdef DEBUG
				debug("SEND %s %lu.%lu@%s: to %lu@%s ",
				    str, dest->md_callback, dest->md_gen,
				    dest->md_host, f->f_dest.md_callback,
				    f->f_dest.md_host);
#endif
				res = func == OPER_LOGIN ?
				    oper_login_1(dest, f->f_clnt) :
				    oper_logout_1(dest, f->f_clnt);
			}
			(void) setitimer(ITIMER_REAL, &otv, NULL);
			(void) sigvec(SIGALRM, &osv, (struct sigvec *)0);
			ready = 0;
#ifdef DEBUG
			if (res && *res == 0)
				debug("(succeeded)\n");
			else
				debug("(failed%s)\n",
				    clnt_sperror(f->f_clnt, ""));
#endif
		}
	}
	if (dobroadcast) {
#ifdef USG
		(void) rpc_broadcast(OPERMSG_PROG, OPERMSG_VERS, func,
		    xdr_msg_dest, (char *)dest,
		    xdr_int, (char *)&res,
		    (resultproc_t)0, "udp");
#else
		(void) clnt_broadcast(OPERMSG_PROG, OPERMSG_VERS,
		    func, xdr_msg_dest, dest, xdr_int, &res, NULL);
#endif
#ifdef DEBUG
		debug("BROADCAST %s %lu.%lu@%s\n",
		    str, dest->md_callback, dest->md_gen, dest->md_host);
#endif
	}
	exit(0);
}

/*
 * Send a message to all appropriate registered destinations.
 * Client programs (non-daemons) always get the new message.
 * Daemon will be sent the message if the message's MSG_FORWARD
 * bit was set (as indicated by the "forward" parameter).
 */
void
forward_msg(msgp)
	msg_t	*msgp;
{
	register struct fwdent *f, *fb, *next;
	register int i;
	int *res, broken;
	struct sigvec sv, osv;
	int	forward = msgp->msg_type & MSG_BEENFWD ? 0 : 1;

	msgp->msg_type |= MSG_BEENFWD;	/* mark it processed */

	for (i = 0, fb = destinations; i < NFWD; i++, fb++) {
#ifdef lint
		next = fb->f_nextdest;
#endif
		for (f = fb->f_nextdest; f; f = next) {
			msg_dest *dest = &f->f_dest;
			next = f->f_nextdest;
			/*
			 * If the message has already been forwarded
			 * by a daemon and this destination is another
			 * daemon, don't relay the message.
			 */
			if (!forward && dest->md_callback == OPERMSG_PROG)
				continue;
#ifdef DEBUG
			{
				struct timeval tmout;
				clnt_control(f->f_clnt, CLGET_TIMEOUT, &tmout);
				debug(
			"Forward msg %lu@%s[%lu] to %lu@%s (%lu secs)",
				    msgp->msg_ident.mid_seq,
				    msgp->msg_ident.mid_host,
				    (u_long)msgp->msg_ident.mid_pid,
				    f->f_dest.md_callback, f->f_dest.md_host,
				    tmout.tv_sec);
			}
#endif
			itv.it_interval.tv_sec = 0;
			itv.it_interval.tv_usec = 0;
			itv.it_value.tv_sec = RPCTIMEOUT;
			itv.it_value.tv_usec = 0;
			(void) setitimer(ITIMER_REAL, &itv, &otv);
#ifdef USG
			(void) sigemptyset(&sv.sv_mask);
			sv.sv_flags = SA_RESTART;
#else
			sv.sv_mask = 0;
			sv.sv_flags = 0;
#endif
			sv.sv_handler = badconn;
			(void) sigvec(SIGALRM, &sv, &osv);
			if (sigsetjmp(connbuf, 1) == 0) {
				ready = 1;
				broken = 0;
				res = NULL;
				res = oper_send_1(msgp, f->f_clnt);
			} else
				broken++;
			(void) setitimer(ITIMER_REAL, &otv, NULL);
			(void) sigvec(SIGALRM, &osv, (struct sigvec *)0);
			ready = 0;
			if (res == NULL || *res < 0) {
#ifdef DEBUG
				debug(" failed (%d)%s\n",
				    f->f_errors+1,
				    clnt_sperror(f->f_clnt, ""));
#endif
				if (++(f->f_errors) > MAXERRS || broken)
					rm_fwd(f);
			} else {
#ifdef DEBUG
				debug(" succeeded\n");
#endif
				f->f_errors = 0L;
			}
		}
	}
}

struct fwdent *
find_fwd(dest)
	msg_dest *dest;
{
	register struct fwdent *f;

	for (f = &destinations[hash_dest(dest)]; f; f = f->f_nextdest)
		if (cmp_dest(dest, &f->f_dest) == 0)
			break;
	return (f);
}

void
rm_fwd(f)
	register struct fwdent *f;
{
	if (f == NULL)
		return;
#ifdef DEBUG
	debug("removing forward entry %lu on %s\n",
	    f->f_dest.md_callback, f->f_dest.md_host);
#endif
	if (f->f_prevfwd) {
		/*
		 * on forward list -- remove
		 */
		f->f_prevfwd->f_nextfwd = f->f_nextfwd;
		if (f->f_nextfwd)
			f->f_nextfwd->f_prevfwd = f->f_prevfwd;
	}
	/*
	 * remove from destination list
	 */
	f->f_prevdest->f_nextdest = f->f_nextdest;
	if (f->f_nextdest)
		f->f_nextdest->f_prevdest = f->f_prevdest;
	if (f->f_clnt) {
		clnt_destroy(f->f_clnt);
		f->f_clnt = NULL;
	}
	f->f_prevdest = NULL;
	f->f_nextdest = NULL;
	f->f_prevfwd = NULL;
	f->f_nextfwd = freefwd;
	freefwd = f;
}

static int
cmp_dest(dest1, dest2)
	msg_dest *dest1, *dest2;
{
	int	cb1, cb2;

	if (strcmp(dest1->md_host, dest2->md_host))
		return (1);
	if (strcmp(dest1->md_domain, dest2->md_domain))
		return (1);
	cb1 = dest1->md_callback == 0 ? OPERMSG_PROG : dest1->md_callback;
	cb2 = dest2->md_callback == 0 ? OPERMSG_PROG : dest2->md_callback;
	if (cb1 != cb2)
		return (1);
	return (0);
}

static int
hash_dest(dest)
	msg_dest	*dest;
{
	register char *p;
	int	sum = 0;

	for (p = dest->md_host; *p; p++)
		sum = (sum << 3) + *p;
	sum += (dest->md_callback == 0 ? OPERMSG_PROG : dest->md_callback);
	return ((sum & 0x7fffffff) % NFWD);
}
