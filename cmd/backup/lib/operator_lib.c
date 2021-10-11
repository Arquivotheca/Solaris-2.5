/*LINTLIBRARY*/
#ident	"@(#)operator_lib.c 1.0 91/01/28 SMI"

#ident	"@(#)operator_lib.c 1.44 93/04/28"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <locale.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <pwd.h>
#include <grp.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#define	PORTMAP
#include <rpc/rpc.h>
#include <rpc/pmap_prot.h>
#include <rpc/pmap_clnt.h>
#include <sys/time.h>
#include <config.h>
#include "operator.h"

#ifdef USG
#include <limits.h>
#include <stdlib.h>
#include <netconfig.h>
#include <unistd.h>
#include <netdir.h>
#include <tiuser.h>
#include <fcntl.h>
#include <sys/utsname.h>
#include <sys/systeminfo.h>

#define	NGROUPS		NGROUPS_MAX
#define	bcmp(s1, s2, len)	memcmp((s1), (s2), (len))
#define	bcopy(s1, s2, len)	memcpy((s2), (s1), (len))
#define	bzero(s, len)		memset((s), 0, (len))
#define	gethostname(name, len)  \
		((sysinfo(SI_HOSTNAME, (name), (len)) < 0) ? -1 : 0)
#define	getdomainname(name, len)  \
		((sysinfo(SI_SRPC_DOMAIN, (name), (len)) < 0) ? -1 : 0)
#define	sigvec		sigaction	/* struct and func */
#define	sv_handler	sa_handler
#define	sv_mask		sa_mask
#define	sv_flags	sa_flags
#endif

static msg_t	msgbuf;			/* incoming message buffer */
static msg_t	*in = &msgbuf;		/* ptr to above (or user-supplied) */

static pid_t	init;			/* =pid if init routine called */
static msg_id	mid;			/* message id for outgoing msgs */
static msg_dest	us;			/* dest == us (for replies/receipt) */
static char	arbiter[BCHOSTNAMELEN]; /* server name */
static char	progname[MAXIDLEN];	/* our program name (from argv) */
static char	*thishost;		/* our host name */
static enum_t	auth_flavor;		/* authtication flavor for callbacks */
static int	daemon;			/* operate in daemon mode */
static int	verbose;		/* print error messages if set */
static int	pending;		/* piggybacked cancellation pending */

static pid_t	pid;			/* our process-ID */
static uid_t	uid;			/* our user-ID */
static gid_t	gid;			/* our group-ID */
static gid_t	gidset[NGROUPS];	/* list of groups to which we belong */
static struct group opergrp;		/* operator group info */
/* static struct sockaddr_in myaddress;	*/

static CLIENT	*clnt;			/* RPC connection, if not daemon */

static msg_id	cancel;			/* piggybacked cancellation */

static char *domainname = "hsm_libdump";	/* for dgettext() */

#ifdef __STDC__
static u_long gettransient(u_long);
static void getreply(struct svc_req *, SVCXPRT *);
static int senderok(uid_t, gid_t, uid_t);
#ifdef USG
/*
 * XXX broken header files
 */
extern int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
#endif
#else
static u_long gettransient();
static void getreply();
static int senderok();
#endif
/*
 * Initialize the operator message system.
 *	The third argument controls the printing
 *	of error messages; this should be turned
 *	off by programs using curses or other
 *	special output functions.
 * Returns:
 *	 1 : initialized and connected to specified server
 *	 0 : initialized but not connected to server
 *		(daemon mode returns this value on success)
 *	-1 : error; not initialized
 */
oper_init(servername, name, setverbose)
	const char *servername;	/* dest for messages - NULL if in daemon mode */
	const char *name;	/* our program name */
	int	setverbose;	/* print error messages if true */
{
	register char *cp, **cpp;
	struct group *g;

	pid = getpid();

	if (init == pid) {
		if (clnt)
			clnt_destroy(clnt);
		goto connect;
	} else
		init = 0;

	verbose = setverbose;

	/*
	 * Get host info
	 */
/*	get_myaddress(&myaddress); */
	(void) gethostname(mid.mid_host, BCHOSTNAMELEN);
	(void) strcpy(us.md_host, mid.mid_host);
	thishost = mid.mid_host;

	if (servername) {
		daemon = 0;
		us.md_callback = 0L;

		while (isspace(*servername))
			servername++;
		if (strcmp(servername, "localhost") == 0)
			(void) strcpy(arbiter, thishost);
		else
			(void) strcpy(arbiter, servername);
	} else {
		daemon = 1;
		us.md_callback = OPERMSG_PROG;
	}
	/*
	 * Get authentication info
	 */
	uid = (int)getuid();
	gid = (int)getgid();
	if (getgroups(NGROUPS, gidset) < 0) {
		if (verbose)
			perror("oper_init() getgroups");
		if (!daemon)
			svc_unregister(us.md_callback, OPERMSG_VERS);
		return (OPERMSG_ERROR);
	}
	g = getgrnam(OPER_GROUP);
	if (g == NULL) {
		if (verbose)
			(void) fprintf(stderr, dgettext(domainname,
		    "no entry for operator group `%s', using group 0\n"),
			    OPER_GROUP);
		g = getgrgid(0);
	}
	if (g)
		opergrp = *g;
	/*
	 * Get our domain name by trying the following in order:
	 *    - the NIS domain set with setdomainname()
	 *    - any domain specified with sethostname()
	 *    - any domain associated with any obtainable local name
	 */
	if (getdomainname(mid.mid_domain, BCHOSTNAMELEN) < 0 ||
	    mid.mid_domain[0] == '\0') {
		cp = strchr(thishost, '.');
		if (cp != NULL)
			(void) strcpy(mid.mid_domain, &cp[1]);
		else {
			struct hostent *h = gethostbyname(thishost);
			if (h == NULL)
				mid.mid_domain[0] = '\0';
			else {
				cp = strchr(h->h_name, '.');
				if (cp != NULL)
					(void) strcpy(mid.mid_domain, &cp[1]);
				else {
					for (cpp = h->h_aliases; *cpp; cpp++) {
						cp = strchr(*cpp, '.');
						if (cp != NULL) {
							(void)
						    strcpy(mid.mid_domain,
							    &cp[1]);
							break;
						}
					}
					if (*cpp == NULL)
						mid.mid_domain[0] = '\0';
				}
			}
		}
	}
	cp = strrchr(name, '/');
	if (cp)
		name = ++cp;
	(void) strncpy(progname, name, MAXIDLEN);
	(void) strncpy(us.md_domain, mid.mid_domain, sizeof (us.md_domain));
	mid.mid_seq = 0;
	mid.mid_pid = pid;
connect:
	if (!daemon)
		clnt = oper_connect(servername, OPERMSG_PROG);
	init = pid;
	if (!daemon && clnt)
		return (OPERMSG_CONNECTED);
	return (OPERMSG_SUCCESS);
}


static boolean_t
rpcb_ping(const char *hostname)
{
	void *nc_handle;
	struct netconfig *nconf;
	struct nd_hostserv rpcbind_hs;
	struct nd_addrlist *nas;
	int fd;
	boolean_t svc_found;
	struct t_call sndcall;
	char *lhost = (char *)hostname;
	extern int t_errno;


	if ((nc_handle = setnetconfig()) == NULL) {
		/* failed to open netconfig database */
		return (B_FALSE);
	}
	svc_found = B_FALSE;
	while (!svc_found && (nconf = getnetconfig(nc_handle))) {
		if (lhost == NULL) {
			if ((strcmp(nconf->nc_protofmly, NC_LOOPBACK) != 0) ||
			    (nconf->nc_semantics != NC_TPI_COTS))
					continue;
		} else {
			if ((strcmp(nconf->nc_protofmly, NC_INET) != 0) ||
			    (strcmp(nconf->nc_proto, NC_TCP) != 0))
					continue;
		}

		if (lhost == NULL)
			rpcbind_hs.h_host = HOST_SELF_CONNECT;
		else
			rpcbind_hs.h_host = lhost;
		rpcbind_hs.h_serv = "rpcbind";
		if (netdir_getbyname(nconf, &rpcbind_hs, &nas) != ND_OK) {
			/*
			 * if the local rpcbind is not running and a name
			 * service is running, we will fail here.
			 */
			break;
		}

		if ((fd = t_open(nconf->nc_device, O_NONBLOCK|O_RDWR, NULL))
		    == -1) {
			netdir_free((char *)nas, ND_ADDRLIST);
			break;
		}

		if (t_bind(fd, (struct t_bind *)NULL,
		    (struct t_bind *)NULL) == -1) {
			netdir_free((char *)nas, ND_ADDRLIST);
			(void) t_close(fd);
			break;
		}

		sndcall.addr = *(nas->n_addrs);
		sndcall.opt.len = 0;
		sndcall.udata.len = 0;
		/*
		 * Attempt a connection. Since the descriptor is opened
		 * O_NONBLOCK, it will probably fail with TNODATA.
		 */
		if (t_connect(fd, &sndcall, NULL) == -1) {
			if (t_errno == TNODATA) {
				/*
				 * Quick peek for connection before sleeping.
				 */
				if (t_look(fd) != T_CONNECT) {
					/*
					 * Sleep for 2 seconds then retry.
					 * Don't use sleep() because
					 * the alarm handler may already be
					 * in use.
					 */
					(void) poll(0, 0, 2000);
					if (t_look(fd) != T_CONNECT) {
						netdir_free((char *)nas,
							ND_ADDRLIST);
						(void) t_close(fd);
						break;
					}
				}
			} else {
				/*
				 * some other connect error.
				 */
				netdir_free((char *)nas, ND_ADDRLIST);
				(void) t_close(fd);
				break;
			}
		}
		svc_found = B_TRUE;
		netdir_free((char *)nas, ND_ADDRLIST);
		(void) t_close(fd);
	}
	endnetconfig(nc_handle);
	return (svc_found);
}

CLIENT *
oper_connect(hostname, program)
	const char *hostname;
	u_long	program;
{
	CLIENT	*newclnt;
	struct timeval tv;

	tv.tv_usec = 0;
	tv.tv_sec = daemon ? DAEMON_TIMEOUT : USER_TIMEOUT;

	if (hostname == NULL)
		return (NULL);
	while (isspace(*hostname))
		hostname++;
	if (program == 0)
		program = OPERMSG_PROG;

	/*
	 * Avoid silly timeout in clnt_create() if rpcbind is not
	 * running locally or if the remote host is down.
	 * Note that this is not a guarantee since things can happen between
	 * the rpcb_ping() and the clnt_create(), but it catches the most
	 * common situations. The timeout is not fatal (but very annoying).
	 */
	if (rpcb_ping(hostname) == B_FALSE) {
		return (NULL);
	}
	newclnt = clnt_create((char *)hostname, program, OPERMSG_VERS, "tcp");
	if (!newclnt) {
		return (NULL);
	}
	if (clnt_control(newclnt, CLSET_TIMEOUT, (char *)&tv) != TRUE) {
		clnt_destroy(newclnt);
		return (NULL);
	}
	newclnt->cl_auth = authunix_create(thishost, uid, gid, NGROUPS, gidset);
	if (newclnt->cl_auth == NULL) {
		clnt_destroy(newclnt);
		return (NULL);
	}
	return (newclnt);
}

oper_login(hostname, reversereq)
	const char *hostname;
	int	reversereq;	/* reverse login requested (daemon mode only) */
{
	msg_dest dest;
	enum clnt_stat	status;
	int	*resp, result;
	struct sigvec ignore, savepipe;

	if (!init) {
		if (verbose)
			(void) fprintf(stderr, dgettext(domainname,
			    "%s: called before initialization\n"),
			    "oper_login");
		return (OPERMSG_ERROR);
	}
	if (!daemon) {
		us.md_callback = gettransient(us.md_callback);
		if (us.md_callback == 0)
			return (OPERMSG_ERROR);
	}
	dest = us;
	(void) time(&dest.md_gen);
	if (daemon && !reversereq)
		dest.md_callback = 0;
	if (hostname) {
		clnt = oper_connect(hostname, OPERMSG_PROG);
		if (!clnt)
			return (OPERMSG_ERROR);
	} else if (!clnt) {
		if (!daemon) {
			if (verbose)
				(void) fprintf(stderr,
				    dgettext(domainname,
				    "%s: called without specifying a host\n"),
				    "oper_login");
			return (OPERMSG_ERROR);
		}
		/*
		 * Used by operator daemons to advertise
		 * themselves via broadcast at start-up
		 */
#ifdef USG
		status = rpc_broadcast(OPERMSG_PROG, OPERMSG_VERS, OPER_LOGIN,
		    xdr_msg_dest, (char *)&dest, xdr_int, (char *)&result,
		    (resultproc_t)0, "udp");
#else
		status = clnt_broadcast(OPERMSG_PROG, OPERMSG_VERS, OPER_LOGIN,
		    xdr_msg_dest, (char *)&dest,
		    xdr_int, (char *)&result, NULL);
#endif
		if (status != RPC_SUCCESS && status != RPC_TIMEDOUT)
			return (OPERMSG_ERROR);
		return (OPERMSG_SUCCESS);
	}
	ignore.sv_handler = SIG_IGN;
#ifdef USG
	(void) sigemptyset(&ignore.sv_mask);
	ignore.sv_flags = SA_RESTART;
#else
	ignore.sv_mask = 0;
	ignore.sv_flags = 0;
#endif
	(void) sigvec(SIGPIPE, &ignore, &savepipe);
	resp = oper_login_1(&dest, clnt);
	(void) sigvec(SIGPIPE, &savepipe, (struct sigvec *)0);
	if (daemon && clnt) {
		(void) clnt_destroy(clnt);
		clnt = NULL;
	}
	if (resp)
		return (*resp);
	else
		return (OPERMSG_ERROR);
}

oper_logout(hostname)
	const char *hostname;
{
	msg_dest dest;
	enum clnt_stat	status;
	int	*resp, result;
	struct sigvec ignore, savepipe;

	if (!init) {
		if (verbose)
			(void) fprintf(stderr, dgettext(domainname,
				"%s: called before initialization\n"),
				"oper_logout");
		return (OPERMSG_ERROR);
	}
	dest = us;
	(void) time(&dest.md_gen);
	if (hostname) {
		if (!clnt)
			clnt = oper_connect(hostname, OPERMSG_PROG);
		if (!clnt)
			return (OPERMSG_ERROR);
	} else if (!clnt) {
		if (!daemon) {
			if (verbose)
				(void) fprintf(stderr, dgettext(domainname,
				    "%s: called without specifying a host\n"),
				    "oper_logout");
			return (OPERMSG_ERROR);
		}
		/*
		 * Used by operator daemons to
		 * advertise shut-down
		 */
#ifdef USG
		status = rpc_broadcast(OPERMSG_PROG, OPERMSG_VERS, OPER_LOGOUT,
		    xdr_msg_dest, (caddr_t)&dest, xdr_int, (caddr_t)&result,
		    (resultproc_t)0, "udp");
#else
		status = clnt_broadcast(OPERMSG_PROG, OPERMSG_VERS, OPER_LOGIN,
		    xdr_msg_dest, (char *)&dest,
		    xdr_int, (char *)&result, NULL);
#endif
		if (status != RPC_SUCCESS && status != RPC_TIMEDOUT)
			return (OPERMSG_ERROR);
		return (OPERMSG_SUCCESS);
	}
	ignore.sv_handler = SIG_IGN;
#ifdef USG
	(void) sigemptyset(&ignore.sv_mask);
	ignore.sv_flags = SA_RESTART;
#else
	ignore.sv_mask = 0;
	ignore.sv_flags = 0;
#endif
	(void) sigvec(SIGPIPE, &ignore, &savepipe);
	resp = oper_logout_1(&dest, clnt);
	(void) sigvec(SIGPIPE, &savepipe, (struct sigvec *)0);
	if (clnt)
		(void) clnt_destroy(clnt);
	clnt = NULL;
	if (resp)
		return (*resp);
	else
		return (OPERMSG_ERROR);
}

oper_getall(hostname)
	const char *hostname;
{
	struct sigvec ignore, savepipe;
	int	*resp;

	if (!init) {
		if (verbose)
			(void) fprintf(stderr, dgettext(domainname,
				"%s: called before initialization\n"),
				"oper_getall");
		return (OPERMSG_ERROR);
	}
	if (hostname) {
		if (!clnt)
			clnt = oper_connect(hostname, OPERMSG_PROG);
		if (!clnt)
			return (OPERMSG_ERROR);
	} else if (!clnt) {
		if (verbose)
			(void) fprintf(stderr, dgettext(domainname,
				"%s: called without specifying a host\n"),
				"oper_getall");
		return (OPERMSG_ERROR);
	}
	if (!daemon) {
		us.md_callback = gettransient(us.md_callback);
		if (us.md_callback == 0)
			return (OPERMSG_ERROR);
	}
	ignore.sv_handler = SIG_IGN;
#ifdef USG
	(void) sigemptyset(&ignore.sv_mask);
	ignore.sv_flags = SA_RESTART;
#else
	ignore.sv_mask = 0;
	ignore.sv_flags = 0;
#endif
	(void) sigvec(SIGPIPE, &ignore, &savepipe);
	resp = oper_sendall_1(&us, clnt);
	(void) sigvec(SIGPIPE, &savepipe, (struct sigvec *)0);
	if (daemon && clnt) {
		(void) clnt_destroy(clnt);
		clnt = NULL;
	}
	if (resp)
		return (*resp);
	else
		return (OPERMSG_ERROR);
}

u_long
oper_send(ttl, level, flags, text)
	time_t	ttl;		/* time to live from receipt */
	int	level;		/* importance level, ala syslog */
	int	flags;		/* message type, etc. */
	const char *text;	/* message text */
{
	struct sigvec ignore, savepipe;
	static msg_t out;
	int *res;

	if (!init) {
		if (verbose)
			(void) fprintf(stderr, dgettext(domainname,
				"%s: called before initialization\n"),
				"oper_send");
		return (0L);
	} else if (!clnt) {
		if (verbose)
			(void) fprintf(stderr, dgettext(domainname,
				"%s: no server connection established\n"),
				"oper_send");
		return (0L);
	}

	(void) time(&out.msg_time);
	out.msg_ttl = ttl;
	++mid.mid_seq;
	mid.mid_pid = getpid();
	out.msg_ident = mid;
	(void) strncpy(out.msg_progname, progname, sizeof (out.msg_progname));
	(void) strncpy(out.msg_arbiter, arbiter, sizeof (out.msg_arbiter));
	out.msg_auth = auth_flavor;
	out.msg_uid = uid;
	out.msg_gid = gid;
	out.msg_level = level;
	out.msg_type = flags | MSG_FORWARD;
	if (flags & MSG_NEEDREPLY) {
		us.md_callback = gettransient(us.md_callback);
		if (us.md_callback == 0)
			return (0L);
		out.msg_callback = us.md_callback;
	} else
		out.msg_callback = 0L;
	out.msg_data[MAXMSGLEN-1] = '\0';
	(void) strncpy(out.msg_data, text, MAXMSGLEN);
	out.msg_len = text[0] == '\0' ? 0 :
	    out.msg_data[MAXMSGLEN-1] ? MAXMSGLEN : strlen(out.msg_data);

	if (pending) {
		pending = 0;
		out.msg_type |= MSG_CANCEL;
		out.msg_target = cancel;
	} else
		(void) bzero(&out.msg_target, sizeof (msg_id));

	ignore.sv_handler = SIG_IGN;
#ifdef USG
	(void) sigemptyset(&ignore.sv_mask);
	ignore.sv_flags = SA_RESTART;
#else
	ignore.sv_mask = 0;
	ignore.sv_flags = 0;
#endif
	(void) sigvec(SIGPIPE, &ignore, &savepipe);
	res = oper_send_1(&out, clnt);
	(void) sigvec(SIGPIPE, &savepipe, (struct sigvec *)0);
	if (res && *res == 0)
		return (out.msg_ident.mid_seq);
	else
		return (0L);
}

u_long
oper_reply(hostname, target, text)
	const char *hostname;	/* hostname of arbitrator */
	const msg_id *target;	/* target of reply */
	const char *text;	/* text of reply */
{
	struct sigvec ignore, savepipe;
	static msg_t	reply;
	CLIENT	*clnt;
	int	*res;

	if (!init) {
		if (verbose)
			(void) fprintf(stderr, dgettext(domainname,
				"%s: called before initialization\n"),
				"oper_reply");
		return (0L);
	}

	if (target == NULL) {
		errno = EINVAL;
		return (0L);
	}
	reply.msg_ttl = 0;		/* not applicable */
	(void) time(&reply.msg_time);
	++mid.mid_seq;
	mid.mid_pid = getpid();
	reply.msg_ident = mid;
	(void) strncpy(reply.msg_progname, progname,
	    sizeof (reply.msg_progname));
	(void) strncpy(reply.msg_arbiter, hostname, sizeof (reply.msg_arbiter));
	(void) bcopy((char *)target, (char *)&reply.msg_target,
	    sizeof (msg_id));
	us.md_callback = gettransient(us.md_callback);
	if (us.md_callback == 0)
		return (0L);
	reply.msg_callback = us.md_callback;
	reply.msg_auth = AUTH_NONE;	/* not applicable */
	reply.msg_uid = uid;
	reply.msg_gid = gid;
	reply.msg_level = 0;		/* not applicable */
	reply.msg_type = MSG_REPLY|MSG_FORWARD;
	(void) strncpy(reply.msg_data, text, MAXMSGLEN);
	reply.msg_len = text[0] == '\0' ? 0 :
	    reply.msg_data[MAXMSGLEN-1] ? MAXMSGLEN : strlen(reply.msg_data);

	clnt = oper_connect(hostname, OPERMSG_PROG);
	if (clnt == NULL)
		return (0L);
	ignore.sv_handler = SIG_IGN;
#ifdef USG
	(void) sigemptyset(&ignore.sv_mask);
	ignore.sv_flags = SA_RESTART;
#else
	ignore.sv_mask = 0;
	ignore.sv_flags = 0;
#endif
	(void) sigvec(SIGPIPE, &ignore, &savepipe);
	res = oper_send_1(&reply, clnt);
	(void) sigvec(SIGPIPE, &savepipe, (struct sigvec *)0);
	(void) clnt_destroy(clnt);
	clnt = NULL;
	if (res && *res == 0)
		return (reply.msg_ident.mid_seq);
	else
		return (0L);
}

u_long
oper_cancel(seq, now)
	u_long	seq;
	int	now;		/* send immediately, else piggyback */
{
	struct sigvec ignore, savepipe;
	static msg_t out;
	int *res;

	if (!init) {
		if (verbose)
			(void) fprintf(stderr, dgettext(domainname,
				"%s: called before initialization\n"),
				"oper_cancel");
		return (0L);
	} else if (!clnt) {
		if (verbose)
			(void) fprintf(stderr, dgettext(domainname,
				"%s: no server connection established\n"),
				"oper_cancel");
		return (0L);
	}

	if (!now) {
		cancel.mid_pid = getpid();
		cancel.mid_seq = seq;
		(void) strncpy(cancel.mid_host, thishost, BCHOSTNAMELEN);
		pending++;
		return (seq);
	}

	(void) time(&out.msg_time);
	out.msg_ttl = 0;
	++mid.mid_seq;
	mid.mid_pid = getpid();
	out.msg_ident = mid;
	(void) strncpy(out.msg_progname, progname, sizeof (out.msg_progname));
	(void) strncpy(out.msg_arbiter, arbiter, sizeof (out.msg_arbiter));

	out.msg_target.mid_pid = getpid();
	out.msg_target.mid_seq = seq;
	(void) strncpy(out.msg_target.mid_host, thishost, BCHOSTNAMELEN);

	out.msg_callback = 0;
	out.msg_auth = AUTH_NONE;
	out.msg_uid = uid;
	out.msg_gid = gid;
	out.msg_level = 0;
	out.msg_type = MSG_CANCEL|MSG_FORWARD;
	out.msg_len = 0;

	ignore.sv_handler = SIG_IGN;
#ifdef USG
	(void) sigemptyset(&ignore.sv_mask);
	ignore.sv_flags = SA_RESTART;
#else
	ignore.sv_mask = 0;
	ignore.sv_flags = 0;
#endif
	(void) sigvec(SIGPIPE, &ignore, &savepipe);
	res = oper_send_1(&out, clnt);
	(void) sigvec(SIGPIPE, &savepipe, (struct sigvec *)0);
	if (res && *res == 0)
		return (out.msg_ident.mid_seq);
	else
		return (0L);
}

/*
 * Find an RPC program number we can register with
 * the portmapper for our transient use.  Much of
 * this code is taken from pmap_set, which does not
 * normally provide a way of differentiating between
 * an inuse RPC number and an unreachable portmapper.
 */
static u_long
gettransient(program)
	u_long program;
{
	static SVCXPRT	*transp;		/* RPC handle for replies */
	u_long	prognum;
#ifdef USG
	struct netconfig *network;
#endif

	if (!transp) {
		transp = svctcp_create(RPC_ANYSOCK, 0, 0);
		if (!transp) {
			if (verbose)
				(void) fprintf(stderr, dgettext(domainname,
					"%s: cannot create tcp service.\n"),
					"gettransient");
			return (0);
		}
#ifdef DEBUG
		else if (verbose)
			fprintf(stderr, dgettext(domainname,
				"%s: created tcp service\n"), "gettransient");
#endif
	}
	prognum = program == 0 ? 0x40000000 : program;
#ifdef USG
	network = getnetconfigent("tcp");
	while (svc_reg(transp, prognum, OPERMSG_VERS,
		getreply, network) == 0) {
#else
	while (svc_register(transp, prognum, OPERMSG_VERS, getreply, 0) == 0) {
#endif
		prognum++;
		if (prognum >= 0x60000000) {
			if (verbose)
				(void) fprintf(stderr, dgettext(domainname,
					"%s: no available rpc programs.\n"),
					"gettransient");
#ifdef USG
			freenetconfigent(network);
#endif
			return (0);
		}
	}
#ifdef USG
	freenetconfigent(network);
#endif
#ifdef DEBUG
	if (verbose)
		(void) fprintf(stderr, dgettext(domainname,
			"%s: program %lu registered\n"),
			"gettransient", prognum);
#endif
	return (prognum);
}

/*
 * Perform some control operation on the running
 * message system.  Returns 0 if the command was
 * successful, -1 on error.  The commands currently
 * supported are:
 *
 *	request			argument
 *
 *	OPER_SETMSGBUF		address of msg_t
 *
 */
oper_control(request, info)
	int	request;
	caddr_t	info;
{
	if (!init) {
		if (verbose)
			(void) fprintf(stderr, dgettext(domainname,
				"%s: called before initialization\n"),
				"oper_control");
		return (-1);
	}

	switch (request) {

	case OPER_SETMSGBUF:
		if (info == (caddr_t)0)
			in = &msgbuf;		/* reset */
		else
			/*LINTED [alignment ok]*/
			in = (msg_t *)info;
		break;

	default:
		if (verbose)
			(void) fprintf(stderr, dgettext(domainname,
				"%s: unsupported command %d.\n"),
				"oper_control", request);
		return (-1);
	}

	return (0);
}

/*
 * Get input, either from a file descriptor or
 * a message from the server operator daemon
 *
 * Arguments:
 *    altfds - a set of file descriptors to monitor
 *	as alternate sources of input, or NULL.
 *    buf - the location in which to place the text
 *	of an incoming message.
 *    len - the amount of storage pointed to by buf.
 *	(should be MAXMSGLEN).
 *    seqp - the address of a long (for sequence number)
 *
 * Returns:
 *	-1:	error or no input
 *	0:	input from file descriptor in set ready to be read
 *	1:	message from operator daemon received
 *
 */
oper_receive(altfds, buf, len, seqp)
	const fd_set *altfds;	/* alternate file descriptors to monitor */
	char	*buf;		/* RETURN: buffer in which to place text */
	int	len;		/* length of buffer */
	u_long	*seqp;		/* RETURN: message sequence number */
{
	struct sigvec ignore, savepipe;
	fd_set readfds;
	register u_char *ap, *rp;
	register int i;

	FD_ZERO(&readfds);
	if (!init && (altfds == NULL ||
	    bcmp((char *)altfds, (char *)&readfds, sizeof (fd_set)) == 0)) {
		if (verbose)
			(void) fprintf(stderr, dgettext(domainname,
				"%s: called before initialization\n"),
				"oper_receive");
		errno = ENODEV;
		return (OPERMSG_ERROR);
	}

	if (!seqp || !buf || len <= 0) {
		errno = EINVAL;
		return (OPERMSG_ERROR);
	}
	for (;;) {
		/*
		 * The set we look at is the union
		 * of the alternates and the set
		 * being used by RPC -- this must
		 * be recomputed each time because
		 * RPC's set may have changed.
		 */
		if (init)
			readfds = svc_fdset;
		if (altfds) {
			ap = (u_char *)altfds;
			rp = (u_char *)&readfds;
			for (i = 0; i < sizeof (fd_set)/NBBY; i++)
				*rp++ |= *ap++;
		}
		if (select(FD_SETSIZE, &readfds, 0, 0, 0) < 0) {
			if (errno == EINTR)
				continue;
			else if (verbose)
				perror("oper_recieve() select");
			return (OPERMSG_ERROR);
		}
		/*
		 * Examine the set of file ready descriptors
		 * to see if any of the alternates are ready.
		 */
		ap = (u_char *)altfds;
		rp = (u_char *)&readfds;
		for (i = 0; i < sizeof (fd_set)/NBBY; i++)
			if (*ap++ & *rp++)
				return (OPERMSG_READY);
		in->msg_time = 0;		/* sentinal */
		ignore.sv_handler = SIG_IGN;
#ifdef USG
		(void) sigemptyset(&ignore.sv_mask);
		ignore.sv_flags = SA_RESTART;
#else
		ignore.sv_mask = 0;
		ignore.sv_flags = 0;
#endif
		(void) sigvec(SIGPIPE, &ignore, &savepipe);
		svc_getreqset(&readfds);
		(void) sigvec(SIGPIPE, &savepipe, (struct sigvec *)0);
		if (in->msg_time)
			break;
	}
	/*
	 * If message has a target
	 * (REPLY, ACK, NACK, CANCEL),
	 * return sequence number of
	 * target otherwise return
	 * number of message.
	 */
	if (HASTARGET(in->msg_type))
		*seqp = in->msg_target.mid_seq;
	else
		*seqp = in->msg_ident.mid_seq;
	(void) strncpy(buf, in->msg_data, len);
	return (OPERMSG_RCVD);
}

/*
 * Like oper_recieve, but an alternate interface
 * for programs requiring retrieval of complete
 * message structures.
 *
 * Arguments:
 *    altfds - a set of file descriptors to monitor
 *	as alternate sources of input, or NULL.
 *    msgp - location in which to copy message
 *
 * Returns:
 *	-1:	error or no input
 *	0:	input from file descriptor in set ready to be read
 *	1:	message from operator daemon received
 *
 */
oper_msg(altfds, msgp)
	const fd_set *altfds;	/* alternate file descriptors to monitor */
	msg_t	*msgp;		/* RETURN: message */
{
	struct sigvec ignore, savepipe;
	fd_set readfds;
	register u_char *ap, *rp;
	register int i;

	FD_ZERO(&readfds);
	if (!init && (altfds == NULL ||
	    bcmp((char *)altfds, (char *)&readfds, sizeof (fd_set)) == 0)) {
		if (verbose)
			(void) fprintf(stderr, dgettext(domainname,
				"%s: called before initialization\n"),
				"oper_msg");
		errno = ENODEV;
		return (OPERMSG_ERROR);
	}

	if (!msgp) {
		errno = EINVAL;
		return (OPERMSG_ERROR);
	}
	for (;;) {
		/*
		 * The set we look at is the union
		 * of the alternates and the set
		 * being used by RPC -- this must
		 * be recomputed each time because
		 * RPC's set may have changed.
		 */
		if (init)
			readfds = svc_fdset;
		if (altfds) {
			ap = (u_char *)altfds;
			rp = (u_char *)&readfds;
			for (i = 0; i < sizeof (fd_set)/NBBY; i++)
				*rp++ |= *ap++;
		}
		if (select(FD_SETSIZE, &readfds, 0, 0, 0) < 0) {
			if (errno == EINTR)
				continue;
			else if (verbose)
				perror("oper_msg() select");
			return (OPERMSG_ERROR);
		}
		/*
		 * Examine the set of file ready descriptors
		 * to see if any of the alternates are ready.
		 */
		ap = (u_char *)altfds;
		rp = (u_char *)&readfds;
		for (i = 0; i < sizeof (fd_set)/NBBY; i++)
			if (*ap++ & *rp++)
				return (OPERMSG_READY);
		in->msg_time = 0;		/* sentinal */
		ignore.sv_handler = SIG_IGN;
#ifdef USG
		(void) sigemptyset(&ignore.sv_mask);
		ignore.sv_flags = SA_RESTART;
#else
		ignore.sv_mask = 0;
		ignore.sv_flags = 0;
#endif
		(void) sigvec(SIGPIPE, &ignore, &savepipe);
		svc_getreqset(&readfds);
		(void) sigvec(SIGPIPE, &savepipe, (struct sigvec *)0);
		if (in->msg_time)
			break;
	}
	*msgp = *in;	/* block copy */
	return (OPERMSG_RCVD);
}

static void
getreply(rqstp, transp)
	struct svc_req	*rqstp;
	SVCXPRT	*transp;
{
	msg_t	*msgp = in;
	int	error = 0;

	if (rqstp->rq_proc == NULLPROC) {	/* rpcinfo */
		(void) svc_sendreply(transp, xdr_void, (char *)0);
		return;
	}
	if (rqstp->rq_proc != OPER_SEND) {
		if (verbose)
			(void) fprintf(stderr, dgettext(domainname,
				"Bad RPC call to operator client %s.\n"),
				progname);
		svcerr_noproc(transp);
		return;
	}
	if (!svc_getargs(transp, xdr_msg_t, (caddr_t)msgp)) {
		svcerr_decode(transp);
		return;
	}
	if (msgp->msg_type & MSG_REPLY) {
		if (!senderok((uid_t)msgp->msg_uid,
				(gid_t)msgp->msg_gid, uid)) {
			/*
			 * not operator/owner
			 */
			error = 1;
			msgp->msg_time = 0;		/* tell local caller */
			svcerr_weakauth(transp);	/* tell remote caller */
		} else if (msgp->msg_target.mid_pid != getpid()) {
			/*
			 * not our reply
			 */
			error = 1;
			msgp->msg_time = 0;
			svcerr_noproc(transp);		/* best fit... */
		}
	}
	if (!error && !svc_sendreply(transp, xdr_u_long,
	    (caddr_t)&msgp->msg_ident.mid_seq)) {
		/*
		 * reply error
		 */
		svcerr_systemerr(transp);
		return;
	}
	if (!svc_freeargs(transp, xdr_msg_t, (caddr_t)msgp))
		(void) fprintf(stderr,
			dgettext(domainname, "unable to free arguments\n"));
}

#ifdef __STDC__
void
oper_end(void)
#else
void
oper_end()
#endif
{
	if (init == pid && clnt) {
		clnt_destroy(clnt);
		clnt = NULL;
	}
	if (us.md_callback && us.md_callback != OPERMSG_PROG)
		svc_unregister(us.md_callback, OPERMSG_VERS);
	init = 0;
}

/*
 * Check a message's sender identification info
 * to determine whether a cancellation or reply
 * should be allowed to proceed.  One of the
 * following conditions must be true:
 *	request_uid == ROOT
 *	request_uid == target_uid
 *	request_gid == operator_gid
 *	operator_gid in gidset(request_uid)
 * Returns 1 if OK to proceed, 0 otherwise.
 */
static int
senderok(ruid, rgid, vuid)
	uid_t	ruid;	   /* user-ID of requestor */
	gid_t	rgid;	   /* group-ID of requestor */
	uid_t	vuid;	   /* user-ID considered valid */
{
	struct passwd *user = getpwuid(ruid);
	struct group *g;
	register char **mem;

	if (!ruid || ruid == vuid)
		return (1);

	if (rgid == (int)opergrp.gr_gid)
		return (1);

	if (user == NULL)
		return (0);

	/*
	 * There may be more than one entry for
	 * the operator group in the file (i.e.,
	 * one local and one from NIS).  Sigh.
	 */
	(void) setgrent();
	while (g = getgrent()) {
		if (g->gr_gid != opergrp.gr_gid)
			continue;
		for (mem = g->gr_mem; *mem; mem++)
			if (strcmp(*mem, user->pw_name) == 0) {
				endgrent();
				return (1);
			}
	}
	(void) endgrent();
	return (0);
}
