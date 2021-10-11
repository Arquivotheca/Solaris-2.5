/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)inetd.c	1.28	95/09/07 SMI"	/* SVr4.0 1.21	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * 		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 * 		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 * 	(c) 1986, 1987, 1988.1989  Sun Microsystems, Inc
 * 	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *		All rights reserved.
 *
 */


/*
 * Inetd - Internet super-server
 *
 * This program invokes all internet services as needed.
 * connection-oriented services are invoked each time a
 * connection is made, by creating a process.  This process
 * is passed the connection as file descriptor 0 and is
 * expected to do a getpeername to find out the source host
 * and port.
 *
 * Datagram oriented services are invoked when a datagram
 * arrives; a process is created and passed a pending message
 * on file descriptor 0.  Datagram servers may either connect
 * to their peer, freeing up the original socket for inetd
 * to receive further messages on, or ``take over the socket'',
 * processing all arriving datagrams and, eventually, timing
 * out.	 The first type of server is said to be ``multi-threaded'';
 * the second type of server ``single-threaded''.
 *
 * Inetd uses a configuration file which is read at startup
 * and, possibly, at some later time in response to a hangup signal.
 * The configuration file is ``free format'' with fields given in the
 * order shown below.  Continuation lines for an entry must begin with
 * a space or tab.  All fields must be present in each entry.
 *
 *	service name		must be in /etc/services
 *	socket type		stream/dgram/raw/rdm/seqpacket
 *	protocol		must be in /etc/protocols
 *	wait/nowait		single-threaded/multi-threaded
 *	user			user to run daemon as
 *	server program		full path name
 *	server program arguments	maximum of MAXARGS (5)
 *
 * for rpc services:
 *	service name/version	must be in the RPC map with version = 1-x
 *	socket type		stream/tli/dgram
 *	protocol		rpc/<nettype|netid>{[,<nettype|netid>]}|*
 *  				first treat the string as a nettype
 *  				and then as a netid; if it is "*" then
 *				it is taken to mean a "visible" nettype
 *	wait/nowait		single-threaded/multi-threaded
 *	user			user to run daemon as
 *	server program		full path name
 *	server program arguments	maximum of MAXARGS (5)
 *
 * Comment lines are indicated by a `#' in column 1.
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <netdb.h>
#include <rpc/rpcent.h>
#include <rpc/nettype.h>
#include <syslog.h>
#include <pwd.h>
#include <fcntl.h>
#include <tiuser.h>
#include <netdir.h>

#ifdef SYSV
#include <sac.h>

#define	rindex(s, c)	strrchr(s, c)
#define	index(s, c)	strchr(s, c)
#define	bcopy(a, b, c)	memcpy(b, a, c)
#define	bzero(s, n)	memset((s), 0, (n))

#ifndef sigmask
#define	sigmask(m)	  (1 << ((m)-1))
#endif

#define	set2mask(setp) ((setp)->__sigbits[0])
#define	mask2set(mask, setp) \
	((mask) == -1 ? sigfillset(setp) : (((setp)->__sigbits[0]) = (mask)))


static sigsetmask(mask)
	int mask;
{
	sigset_t oset;
	sigset_t nset;

	(void) sigprocmask(0, (sigset_t *)0, &nset);
	mask2set(mask, &nset);
	(void) sigprocmask(SIG_SETMASK, &nset, &oset);
	return (set2mask(&oset));
}

static sigblock(mask)
	int mask;
{
	sigset_t oset;
	sigset_t nset;

	(void) sigprocmask(0, (sigset_t *)0, &nset);
	mask2set(mask, &nset);
	(void) sigprocmask(SIG_BLOCK, &nset, &oset);
	return (set2mask(&oset));
}

#endif

#define	TOOMANY		40		/* don't start more than TOOMANY */
#define	CNT_INTVL	60		/* servers in CNT_INTVL sec. */
#define	RETRYTIME	(60*10)		/* retry after bind or server fail */

#define	MASKCHLDALRMHUP (sigmask(SIGCHLD)|sigmask(SIGHUP)|sigmask(SIGALRM))

#ifndef MIN
#define	MIN(a, b)	(((a) < (b)) ? (a) : (b))
#endif

extern	int errno;

void	reapchild(), retry(), config();
void	termserv();
void	unregister();
void	setup(), setuprpc();
void	endconfig();
void	print_service();
struct proto_list *getprotolist();
extern	char	*malloc();
long	strtol();

#ifndef SYSV
char	*index();
#else
void	sacmesg();
char	*getenv();
char	*strchr();
void	sigterm();

int	standalone = 0;		/* run without SAC */
#endif SYSV

int	debug = 0;
FILE	*debugfp;
int	ndescriptors;
int	nsock, maxsock;
fd_set	allsock;
int	options;
int	timingout;
int	nservers = 0;
int	maxservers;
struct	servent *sp;
int	calltrace = 0;		/* trace all connections */
int	toomany = TOOMANY;	/* "broken server" detection parameter */
int	cnt_intvl = CNT_INTVL;	/* "broken server" detection parameter */


/*
 * Each service requires one file descriptor for the socket we listen on
 * for requests for that service.  We don't allow more services than
 * we can allocate file descriptors for.  OTHERDESCRIPTORS is the number
 * of descriptors needed for other purposes; this number was determined
 * experimentally.
 */
#ifdef SYSV
#define	OTHERDESCRIPTORS	11
#else
#define	OTHERDESCRIPTORS	8
#endif SYSV

/*
 * Design note: The basic inetd design is to block doing a select()
 * on a set of file descriptors for the services specified in the
 * inetd configuration file.
 * For connection-oriented services, on detecting an incoming connection
 * request(s), select() returns and the set of services being served
 * by inetd is scanned in sequence and if the incoming connection request
 * is present for a service, inetd performs whatever is needed to start
 * that service. For "nowait" connection-oriented services, this involves
 * acccepting the incoming connection request.
 * Inetd has also been extended to handle TLI interfaces based
 * services. The TLI connection-oriented services connection
 * accepting interfaces have a broken brain-damaged design and
 * present a special problem. In TLI is not possible to accept
 * a connection while there are more incoming connection requests
 * when the t_accept() call is executed.
 * Instead they are merely expected to be removed from the file
 * descriptor (by t_listen()) and the connection accept is supposed
 * to be retried.
 * This can lead to (in the case of a service with many incoming
 * requests), a number of accept attempts upto the transport
 * provider "backlog" (or "qlen" limit) before a single connection
 * is accepted. To be fair to other services, we queue the incoming
 * connection requests in user space if this happens and defer the
 * accept attempt. Evantually it gets a chance to be serviced again
 * along with other services after the file descriptors have been
 * polled. Even when there are no incoming requests at the file
 * descriptors we continue to service the pending TLI
 * connection-oriented service requests till they are all done. Then
 * inetd can block indefinitely in select() awaiting more incoming
 * connection requests at the file descriptors.
 * XXX long term we should create a consolidation private interface
 * to accept TLI connections despite incoming connection indications
 * and use that interface to accept TLI connections.
 */
struct tlx_coninds {
	struct t_call *tc_callp;
	struct tlx_coninds *tc_next;
};

/*
 * Additional information carried in various fields:
 *
 * If "se_wait" is neither 0 nor 1, it's the PID of the server process
 * currently running for that service.
 * If "se_fd" is -1, it means that the service could not be set up,
 * e.g. the entry for that service couldn't be found in "/etc/services".
 */
struct	servtab {
	char	*se_service;		/* name of service */
	int	se_socktype;		/* type of socket to use */
	char	*se_proto;		/* protocol used */
	char	*se_dev;		/* device name for TLI services */
	struct	t_info	se_info;	/* transport provider info for */
					/* TLI services */
	struct tlx_coninds *se_tlx_coninds;
					/* TLI services list of conns */
					/* waiting to be t_accept'd */
	char	se_isrpc;		/* service is RPC-based */
	char	se_checked;		/* looked at during merge */
	pid_t	se_wait;		/* single threaded server */
	char	*se_user;		/* user name to run as */
	struct	biltin *se_bi;		/* if built-in, description */
	char	*se_server;		/* server program */
#define	MAXARGV 5
	char	*se_argv[MAXARGV+1];	/* program arguments */
	int	se_fd;			/* open descriptor */
	union {
		struct	sockaddr_in ctrladdr;	/* bound address */
		struct	netbuf ctrlnetbuf;	/* bound address */
		struct {
			unsigned prog;	/* program number */
			unsigned lowvers;	/* lowest version supported */
			unsigned highvers;	/* highest version supported */
		} rpcnum;
	} se_un;
	int	se_count;		/* number started since se_time */
	struct	timeval se_time;	/* start of se_count */
	struct	servtab *se_next;
} *servtab;

/*
 * We define SOCK_TLI as a sort of pseudo socket type to differentiate
 * TLI services.  Lets hope no one ever defines a socket type with this
 * number!
 */
#define	SOCK_TLI 1000

#define	se_ctrladdr	se_un.ctrladdr
#define	se_ctrlnetbuf	se_un.ctrlnetbuf
#define	se_rpc se_un.rpcnum

struct	netconfig	*getnetconfigent();

int echo_stream(), discard_stream(), machtime_stream();
int daytime_stream(), chargen_stream();
int echo_dg(), discard_dg(), machtime_dg(), daytime_dg(), chargen_dg();

/* list the protocols supported by a service */
struct proto_list {
	char	*pr_proto;	/* a protocol entry */
	struct	proto_list *pr_next;	/* next proto entry */
} *proto_list;

struct biltin {
	char	*bi_service;		/* internally provided service name */
	int	bi_socktype;		/* type of socket supported */
	short	bi_fork;		/* 1 if should fork before call */
	short	bi_wait;		/* 1 if should wait for child */
	int	(*bi_fn)();		/* function which performs it */
} biltins[] = {
	/* Echo received data */
	"echo",		SOCK_STREAM,	1, 0,	echo_stream,
	"echo",		SOCK_DGRAM,	0, 0,	echo_dg,

	/* Internet /dev/null */
	"discard",	SOCK_STREAM,	1, 0,	discard_stream,
	"discard",	SOCK_DGRAM,	0, 0,	discard_dg,

	/* Return 32 bit time since 1970 */
	"time",		SOCK_STREAM,	0, 0,	machtime_stream,
	"time",		SOCK_DGRAM,	0, 0,	machtime_dg,

	/* Return human-readable time */
	"daytime",	SOCK_STREAM,	0, 0,	daytime_stream,
	"daytime",	SOCK_DGRAM,	0, 0,	daytime_dg,

	/* Familiar character generator */
	"chargen",	SOCK_STREAM,	1, 0,	chargen_stream,
	"chargen",	SOCK_DGRAM,	0, 0,	chargen_dg,
	0
};

#define	NUMINT	(sizeof (intab) / sizeof (struct inent))
char	*CONFIG = "/etc/inetd.conf";
char	**Argv;
char 	*LastArg;
#ifdef SYSV
struct	pmmsg	Pmmsg;
char	Tag[PMTAGSIZE];
char	State = PM_STARTING;
int	Sfd, Pfd;
#endif SYSV

main(argc, argv, envp)
	int argc;
	char *argv[], *envp[];
{
	register struct servtab *sep;
	register struct passwd *pwd;
	char *cp, buf[50];
	int i, dofork;
	pid_t pid;
	struct rlimit rlimit, orlimit;
	struct timeval do_nonblocking_poll;
	int tlx_pending_current;
	int tlx_pending_counter;
	int omask, hupmask;


	char *cmdname;
#ifndef SYSV
	struct sigvec sv;
#else
	char *istate;
	int pidfd;
	int ret;
	char pidstring[BUFSIZ];
#endif /* SYSV */

	cmdname = argv[0];
	Argv = argv;
	if (envp == 0 || *envp == 0)
		envp = argv;
	while (*envp)
		envp++;
	LastArg = envp[-1] + strlen(envp[-1]);
	argc--, argv++;

	/*
	 * Can't use getopt, since getopt sends unknown flag diagnostics
	 * to stderr regardless.
	 */
	while (argc > 0 && *argv[0] == '-') {
		for (cp = &argv[0][1]; *cp; cp++) switch (*cp) {

		case 'd':
			debug = 1;
			options |= SO_DEBUG;
			break;

		case 'r':
			if (argc < 3) {
				usage(cmdname);
				exit(1);
			}
			toomany = atoi(argv[1]);
			argc--, argv++;
			cnt_intvl = atoi(argv[1]);
			argc--, argv++;
			break;

		case 's':
			standalone = 1;
			break;

		case 't':
			calltrace = 1;
			break;

		default:
			if (standalone)
				fprintf(stderr,
				    "inetd: Unknown flag -%c ignored.\n", *cp);
			else
				syslog(LOG_WARNING,
				    "inetd: Unknown flag -%c ignored.\n", *cp);
			break;
		}
		argc--, argv++;
	}
	if (debug)
		if (standalone)
			debugfp = stderr;
		else
			debugfp = fopen("/var/saf/inetd/log", "a");

	if (argc > 0)
		CONFIG = argv[0];

	if (getrlimit(RLIMIT_NOFILE, &rlimit) == 0) {
		orlimit = rlimit;
		rlimit.rlim_cur = MIN(rlimit.rlim_max, FD_SETSIZE);
		setrlimit(RLIMIT_NOFILE, &rlimit);
	}

	ndescriptors = getdtablesize();
	maxservers = ndescriptors - OTHERDESCRIPTORS;

#ifdef SYSV
	if (!standalone) {
		char *s;

		istate = getenv("ISTATE");
		if (istate == (char *) 0) {
			/*
			 * Print friendly error message for poor BSD system
			 * administrator who naively expects same command
			 * line interface in SVR4 inetd.
			 */
			fprintf(stderr,
"inetd: couldn't find ISTATE environment variable. Try -s flag.\n");
			syslog(LOG_ERR, "ISTATE not in environment");
			exit(1);
		}
		if (!strcmp(istate, "enabled"))
			State = PM_ENABLED;
		else if (!strcmp(istate, "disabled"))
			State = PM_DISABLED;
		else {
			syslog(LOG_ERR, "Invalid initial state");
			exit(-1);
		}
		s = getenv("PMTAG");
		if (s == (char *) 0) {
			fprintf(stderr,
"inetd: couldn't find PMTAG environment variable. Try -s flag.\n");
			syslog(LOG_ERR, "PMTAG not in environment");
			exit(1);
		}
		strcpy(Tag, s);
		/* fill in things that don't change */
		Pmmsg.pm_size = 0;
		Pmmsg.pm_maxclass = 1;
		strcpy(Pmmsg.pm_tag, Tag);
		if (debug)
			fprintf (debugfp, "Istate = %s, Tag = <%s>\n",
				    istate, Tag);
		if (!debug) {
			(void) open("/", O_RDONLY);
			(void) dup2(0, 1);
			(void) dup2(0, 2);
			setsid();
		}
		if ((Sfd = open("../_sacpipe", O_RDWR)) < 0) {
			syslog(LOG_ERR, "Could not open _sacpipe file");
			exit(-1);
		}
		if ((Pfd = open("_pmpipe", O_RDWR)) < 0) {
			syslog(LOG_ERR, "Could not open _pmpipe file");
			exit(-1);
		}
		FD_SET(Pfd, &allsock);
		nsock++;
		if ((pidfd = open("_pid", O_WRONLY | O_CREAT, 0644)) < 0) {
			syslog(LOG_ERR, "Could not create _pid file");
			exit(-1);
		}
		if (lockf(pidfd, 2, 0L) < 0) {
			syslog(LOG_ERR, "Could not lock _pid file");
			exit(-1);
		}
		pid = getpid();
		i = sprintf(pidstring, "%ld", pid) + 1;	/* +1 for null */
		ftruncate(pidfd, 0);
		while ((ret = write(pidfd, pidstring, i)) != i) {
			if (errno == EINTR)
				continue;
			if (ret < 0) {
				syslog(LOG_ERR, "Could not write _pid file");
				exit(-1);
			}
		}
	} /* if !standalone */
	else if (!debug) {
		if (fork())
			exit(0);
		(void) close(0);
		(void) close(1);
		(void) close(2);
		(void) open("/", O_RDONLY);
		(void) dup2(0, 1);
		(void) dup2(0, 2);
		setsid();
	}
	openlog("inetd", LOG_PID | LOG_NOWAIT, LOG_DAEMON);
	(void) sigset(SIGALRM, retry);
	if (!standalone)
		(void) sigset(SIGTERM, sigterm);
	(void) config();
	(void) sigset(SIGHUP, config);
	(void) sigset(SIGCHLD, reapchild);

#else

	if (!debug) {
		if (fork())
			exit(0);
		(void) close(0);
		(void) close(1);
		(void) close(2);
		(void) open("/", O_RDONLY);
		(void) dup2(0, 1);
		(void) dup2(0, 2);
		{
			int tt = open("/dev/tty", O_RDWR);
			if (tt > 0) {
				ioctl(tt, TIOCNOTTY, (char *)0);
				(void) close(tt);
		    }
		}
	}
	openlog("inetd", LOG_PID | LOG_NOWAIT, LOG_DAEMON);
	bzero((char *)&sv, sizeof (sv));
	sv.sv_mask = MASKCHLDALRMHUP;
	sv.sv_handler = retry;
	sigvec(SIGALRM, &sv, (struct sigvec *)0);
	(void) config();
	sv.sv_handler = config;
	sigvec(SIGHUP, &sv, (struct sigvec *)0);
	sv.sv_handler = reapchild;
	sigvec(SIGCHLD, &sv, (struct sigvec *)0);

#endif SYSV

	bzero((caddr_t)&do_nonblocking_poll, sizeof (struct timeval));
	tlx_pending_counter = 0;
	for (;;) {
		int ctrl, n;
		fd_set readable;
		struct timeval *select_timeout;

		struct sockaddr_in rem_addr;
		int remaddrlen = sizeof (rem_addr);

		if (nsock == 0) {
			sigset_t nullsigset;

			sigemptyset (&nullsigset);
			sigblock(MASKCHLDALRMHUP);
			while (nsock == 0)
				sigsuspend (&nullsigset);
			sigsetmask(0L);

		}

		tlx_pending_current = tlx_pending_counter;
		tlx_pending_counter = 0;	/* reset to zero */
		if (tlx_pending_current > 0)
			/* block zero time */
			select_timeout = &do_nonblocking_poll;
		else
			select_timeout = NULL; /* block infinite time */

		readable = allsock;
		if ((n = select(maxsock + 1, &readable, (fd_set *)0,
		(fd_set *)0, select_timeout)) < 0) {
			if (n < 0 && errno != EINTR)
				syslog(LOG_WARNING, "select: %m\n");
			if (n < 0 && errno == EBADF) {
				syslog(LOG_WARNING,
				    "readable:  %x %x %x %x %x %x %x %x \n",
				    readable.fds_bits[0],
				    readable.fds_bits[1],
				    readable.fds_bits[2],
				    readable.fds_bits[3],
				    readable.fds_bits[4],
				    readable.fds_bits[5],
				    readable.fds_bits[6],
				    readable.fds_bits[7]);
				syslog(LOG_WARNING,
				    "allsock:  %x %x %x %x %x %x %x %x \n",
				    allsock.fds_bits[0],
				    allsock.fds_bits[1],
				    allsock.fds_bits[2],
				    allsock.fds_bits[3],
				    allsock.fds_bits[4],
				    allsock.fds_bits[5],
				    allsock.fds_bits[6],
				    allsock.fds_bits[7]);
			}
			sleep(1);
			continue;
		}
#ifdef SYSV
		if (!standalone && n && FD_ISSET(Pfd, &readable)) {
		sacmesg();
		n--;
		}
#endif

		/*
		 * We block SIGHUP in this loop where we traverse the
		 * linked list of services. (This list is modfied by the
		 * SIGHUP handler). References to embedded contents of
		 * servtab structure (like tlx_conind list) are also
		 * protected by this.
		 */
		hupmask = sigblock(sigmask(SIGHUP));
		for (sep = servtab; (n || tlx_pending_current) && sep;
			sep = sep->se_next)
		if (sep->se_fd != -1 && (FD_ISSET(sep->se_fd, &readable) ||
				(sep->se_tlx_coninds != NULL))) {
		int ctrlisnew = 0;
		/*
		 * Decrement by one counts that track services that have
		 * outstanding requests ready.
		 */
		if (FD_ISSET(sep->se_fd, &readable))
			n--;
		if (sep->se_tlx_coninds != NULL)
			tlx_pending_current--;

		if (debug) {
			if (sep->se_tlx_coninds != NULL)
				fprintf(debugfp,
				"attempt to accept tli pending service %s\n",
				sep->se_service);
			else {
			fprintf(debugfp, "attempting incoming service %s\n",
					sep->se_service);
			}
		}

		if (!sep->se_wait && sep->se_socktype == SOCK_STREAM) {
			ctrl = accept(sep->se_fd, (struct sockaddr *)&rem_addr,
					&remaddrlen);
			if (debug)
				fprintf(debugfp, "accept, ctrl %d\n", ctrl);
			if (ctrl < 0) {
				if (errno == EINTR)
					continue;
				syslog(LOG_WARNING, "accept: %m");
				continue;
			}
			ctrlisnew = 1;
		} else if (!sep->se_wait && (sep->se_socktype == SOCK_TLI) &&
			    (sep->se_info.servtype != T_CLTS)) {

			ctrl = tli_service_accept(sep, &rem_addr, &remaddrlen);
			/*
			 * account for pending connections regardless of
			 * success/failure of tli_service_accept() call.
			 */
			if (sep->se_tlx_coninds != NULL)
				tlx_pending_counter++;

			if (debug)
				fprintf(debugfp, "tli_accept, ctrl %d\n", ctrl);
			if (ctrl < 0)
				continue;
			ctrlisnew = 1;
		} else
			ctrl = sep->se_fd;
		/*
		 * Note: Following sigblock assumes that SIGHUP is already
		 * blocked
		 */
		omask = sigblock(sigmask(SIGCHLD)|sigmask(SIGALRM));
		pid = 0;
		dofork = (sep->se_bi == 0 || sep->se_bi->bi_fork);
		if (dofork) {
			/*
			 * Detect broken servers. If a datagram service
			 * exits without consuming (reading) the datagram,
			 * that service's descriptor will select "readable"
			 * again, and inetd will fork another instance of the
			 * server.  XXX - There should be a better way of
			 * detecting this condition.
			 */
			if ((sep->se_socktype == SOCK_DGRAM) ||
				((sep->se_socktype == SOCK_TLI) &&
				    (sep->se_info.servtype == T_CLTS))) {
				if (sep->se_count++ == 0)
					(void) gettimeofday(&sep->se_time,
					    (struct timezone *) 0);
				else if (sep->se_count >= toomany) {
					struct timeval now;

					(void) gettimeofday(&now,
						(struct timezone *) 0);
					if (now.tv_sec - sep->se_time.tv_sec >
						cnt_intvl) {
						sep->se_time = now;
						sep->se_count = 1;
					} else {
						syslog(LOG_ERR,
		    "%s/%s server failing (looping), service terminated",
						    sep->se_service,
						    sep->se_proto);
						termserv(sep);
						sep->se_count = 0;
						sigsetmask(omask);
						if (!timingout) {
							timingout = 1;
							alarm(RETRYTIME);
						}
						continue;
					}
				}
			}
			pid = fork();
		}
		if (pid < 0) {
			if (ctrlisnew)
				(void) close(ctrl);
			sigsetmask(omask);
			sleep(1);
			continue;
		}
		if (pid && sep->se_wait) {
			sep->se_wait = pid;
			FD_CLR(sep->se_fd, &allsock);
			nsock--;
		}

		if (calltrace && (!dofork || pid)) {
			if ((sep->se_socktype == SOCK_STREAM) ||
				((sep->se_socktype == SOCK_TLI) &&
				    (sep->se_info.servtype != T_CLTS)))
				/* don't waste time doing a gethostbyaddr */
				(pid) ?	syslog(LOG_NOTICE,
					    "%s[%d] from %s %d",
					    sep->se_service, pid,
					    inet_ntoa (rem_addr.sin_addr),
					    rem_addr.sin_port) :
				syslog(LOG_NOTICE,
					    "%s from %s %d",
					    sep->se_service,
					    inet_ntoa (rem_addr.sin_addr),
					    rem_addr.sin_port);
		}

		sigsetmask(omask);
		if (pid == 0) {
			/*
			 * Child process or builtin inetd service that
			 * does not require a fork
			 */
			char addrbuf[32];
			int tt;

			if (dofork) {
				/*
				 * For the child process, there should
				 * be no possibility of reconfiguring
				 * services. We do not want SIGHUP handled.
				 * Restore SIGHUP to its default action
				 * and restore the signal mask. (Child
				 * process should begin with signal mask
				 * in the default state).
				 */
#ifdef SYSV
				sigset(SIGHUP, SIG_DFL);
#else
				signal(SIGHUP, SIG_DFL);
#endif SYSV
				(void) sigsetmask(hupmask);
			}
			if (debug)
#ifdef SYSV
				if (dofork)
					setsid();
#else
				if (dofork &&
					(tt = open("/dev/tty", O_RDWR)) > 0) {
					ioctl(tt, TIOCNOTTY, 0);
					(void) close(tt);
				}
#endif SYSV

			sprintf(addrbuf, "%x.%d",
				ntohl(rem_addr.sin_addr.s_addr),
				ntohs(rem_addr.sin_port));
			if (sep->se_bi) {
				audit_inetd_service(sep->se_bi->bi_service,
					&rem_addr.sin_addr,
					rem_addr.sin_port);	/* BSM */
				(*sep->se_bi->bi_fn)(ctrl, sep);
			} else {
				(void) dup2(ctrl, 0);
				(void) close(ctrl);
				(void) dup2(0, 1);
				(void) dup2(0, 2);
				if ((pwd = getpwnam(sep->se_user)) == NULL) {
					syslog(LOG_ERR,
						"getpwnam: %s: No such user",
						sep->se_user);
					if (sep->se_socktype != SOCK_STREAM)
						recv(0, buf, sizeof (buf), 0);
					_exit(1);
				}
				if (pwd->pw_uid) {
					if (setgid((gid_t)pwd->pw_gid) < 0) {
					    syslog(LOG_ERR,
						"setgid(%d): %m", pwd->pw_gid);
					    if (sep->se_socktype != SOCK_STREAM)
						recv(0, buf, sizeof (buf), 0);
					    _exit(1);
					}
					initgroups(pwd->pw_name, pwd->pw_gid);
					if (setuid((uid_t)pwd->pw_uid) < 0) {
					    syslog(LOG_ERR,
						"setuid(%d): %m", pwd->pw_uid);
					    if (sep->se_socktype != SOCK_STREAM)
						recv(0, buf, sizeof (buf), 0);
					    _exit(1);
					}
				}

				/*
				 * only close those descriptors that might
				 * actually be open.
				 */
				i = maxsock + 5;
				while (--i > 2)
					if (i != ctrl)	/* ctrl closed above */
						(void) close(i);
				setrlimit(RLIMIT_NOFILE, &orlimit);

				if (sep->se_argv[0] != NULL) {
					audit_inetd_service(sep->se_service,
					    &rem_addr.sin_addr,
					    rem_addr.sin_port);	/* BSM */
					if (!strcmp(sep->se_argv[0], "%A"))
					execl(sep->se_server,
					    rindex(sep->se_server, '/')+1,
					    sep->se_socktype == SOCK_DGRAM
					    ? (char *)0 : addrbuf, (char *)0);
					else
					execv(sep->se_server, sep->se_argv);
					} else
					execv(sep->se_server, sep->se_argv);
				if ((sep->se_socktype != SOCK_STREAM) &&
					(sep->se_socktype != SOCK_TLI))
					recv(0, buf, sizeof (buf), 0);
				if ((sep->se_socktype == SOCK_TLI) &&
					(sep->se_info.servtype == T_CLTS)) {
					int flag;

					t_rcv(0, buf, sizeof (buf), &flag);
				}
				syslog(LOG_ERR, "execv %s: %m", sep->se_server);
				_exit(1);
			}
		}
		if (ctrlisnew)
			(void) close(ctrl);
	}
	(void) sigsetmask(hupmask);
	}
}

void
reapchild()
{

	pid_t pid;
	register struct servtab *sep;
	int omask;
/*
 * XXX - bogus... why are we jumpint through hoops to try and maintain 4.x
 * source compatibility. This is already *so* far removed....
 */
#ifndef SYSV
	extern const char *sys_siglist[];
#endif SYSV

	/*
	 * Block SIGHUP because we traverse the services linked list which
	 * is modified in SIGHUP handling. Since we are in SIGCHLD handler,
	 * it is already blocked.
	 */
	omask = sigblock(sigmask(SIGHUP));
#ifdef SYSV
	for (;;) {
	    int options;
	    int error;
	    siginfo_t info;

	    options = WNOHANG | WEXITED;
	    bzero ((char *) &info, sizeof (info));
	    error = waitid(P_ALL, 0, &info, options);
	    if ((error == -1) || (info.si_pid == 0))
		break;
	    if (debug)
		fprintf(debugfp, "%d reaped\n", info.si_pid);
	    for (sep = servtab; sep; sep = sep->se_next)
		if (sep->se_wait == info.si_pid) {
		    if (info.si_status) {
			if (info.si_signo) {
			    char *sigstr, *strsignal();
			    if ((sigstr = strsignal(info.si_signo)) != NULL)
				syslog(LOG_WARNING, "%s: %s%s",
					sep->se_server, sigstr,
					((info.si_code == CLD_DUMPED) ?
					" - core dumped" : ""));
			    else
				syslog(LOG_WARNING, "%s: Signal %d%s",
					sep->se_server, info.si_signo,
					((info.si_code == CLD_DUMPED) ?
					" - core dumped" : ""));
			} else {
				syslog(LOG_WARNING, "%s: exit status %d",
					sep->se_server, info.si_status);
			}
		    }
#else
	for (;;) {
		union wait status;
		pid = wait3(&status, WNOHANG, (struct rusage *)0);
		if (pid <= 0)
			break;
		if (debug)
			fprintf(debugfp, "%d reaped\n", pid);
		for (sep = servtab; sep; sep = sep->se_next)
		    if (sep->se_wait == pid) {
			if (status.w_status) {
			    if (status.w_termsig) {
				if (status.w_termsig > NSIG)
				    syslog(LOG_WARNING, "%s: Signal %d%s",
					sep->se_server, status.w_termsig,
					(status.w_coredump ?
					" - core dumped" : ""));
				else
				    syslog(LOG_WARNING,
					"%s: %s%s", sep->se_server,
					sys_siglist[status.w_termsig],
					(status.w_coredump ?
					" - core dumped" : ""));
			    } else {
				syslog(LOG_WARNING,
					"%s: exit status %d",
					sep->se_server, status.w_retcode);
			    }
			}
#endif /* SYSV */
			if (debug)
				fprintf(debugfp, "restored %s, fd %d\n",
					sep->se_service, sep->se_fd);
			FD_SET(sep->se_fd, &allsock);
			nsock++;
			sep->se_wait = 1;
		}
	}
	(void) sigsetmask(omask);
}

void
config()
{
	register struct servtab *sep, *cp, **sepp;
	struct servtab *getconfigent(), *enter();
	int omask;

	audit_inetd_config();	/* BSM */

	/*
	 * Block all signal handlers, the world of services rendered
	 * by inetd is changing. We are handling SIGHUP (except for the
	 * intial call on inetd startup) so that is already
	 * blocked.
	 */
	omask = sigblock(sigmask(SIGCHLD)|sigmask(SIGALRM));

	if (!setconfig()) {
		syslog(LOG_ERR, "%s: %m", CONFIG);
		return;
	}
	for (sep = servtab; sep; sep = sep->se_next)
		sep->se_checked = 0;
	while (cp = getconfigent()) {
		for (sep = servtab; sep; sep = sep->se_next) {
			if (sep->se_isrpc) {
				if (cp->se_isrpc &&
					(sep->se_rpc.prog == cp->se_rpc.prog) &&
					!strcmp(sep->se_proto, cp->se_proto))
						break;
			} else {
				if (!cp->se_isrpc &&
				    !strcmp(sep->se_service, cp->se_service) &&
				    !strcmp(sep->se_proto, cp->se_proto))
					break;
			}
		}
		if (sep != 0) {
			int i;

			/*
			 * sep->se_wait may be holding the pid of a daemon
			 * that we're waiting for.  If so, don't overwrite
			 * it unless the config file explicitly says don't
			 * wait.
			 */
			if (cp->se_bi == 0 &&
				(sep->se_wait == 1 || cp->se_wait == 0))
					sep->se_wait = cp->se_wait;
#define	SWAP(a, b) { char *c = a; a = b; b = c; }
			if (cp->se_user)
				SWAP(sep->se_user, cp->se_user);
			if (cp->se_server)
				SWAP(sep->se_server, cp->se_server);
			for (i = 0; i < MAXARGV; i++)
				SWAP(sep->se_argv[i], cp->se_argv[i]);
			freeconfig(cp);
			if (debug)
				print_service("REDO", sep);
		} else {
			sep = enter(cp);
			if (debug)
				print_service("ADD ", sep);
		}
		sep->se_checked = 1;
		if (sep->se_isrpc) {
		    if (sep->se_wait != 0 && sep->se_wait != 1) {
			if (debug)
			    fprintf(debugfp,
	"won't reregister RPC svc %s/%s [%d] (still active)\n",
				sep->se_service, sep->se_proto, sep->se_wait);
			syslog(LOG_NOTICE,
	"config: %s/%s still active and was not reconfigured. ",
				sep->se_service, sep->se_proto);
		    } else {
			if (sep->se_fd != -1) {
				termserv(sep);
				/* Note: unregister() call within termserv() */
			} else
				unregister(sep);	/* just in case */
			if (nservers >= maxservers) {
				syslog(LOG_ERR,
					"%s/%s: too many services (max %d)",
					sep->se_service, sep->se_proto,
					maxservers);
				sep->se_fd = -1;
				continue;
			}
			/*
			 * RPC version numbers may have changed.
			 * We unregistered from old version numbers above
			 * Now we change to potentially new version numbers
			 * before we register again in setuprpc()
			 */
			sep->se_rpc.lowvers = cp->se_rpc.lowvers;
			sep->se_rpc.highvers = cp->se_rpc.highvers;

			setuprpc(sep);
		    }
		} else {
			sp = getservbyname(sep->se_service, sep->se_proto);
			if (sp == NULL) {
				syslog(LOG_ERR, "%s/%s: unknown service",
					sep->se_service, sep->se_proto);
				if (sep->se_fd != -1)
					termserv(sep);
				continue;
			}
			if (sep->se_socktype == SOCK_TLI) {
				/*
				 * For TLI, we need allocate and
				 * hang a buffer off the address netbuf.
				 * This is freed later when not needed
				 * anymore
				 */
				struct sockaddr_in *s_in;

				s_in = (struct sockaddr_in *)
					malloc(sizeof (struct sockaddr_in));
				if (s_in == NULL) {
					syslog(LOG_ERR,
					"%s/%s: out of memory for addr\n",
					sep->se_service, sep->se_proto);
					if (sep->se_fd != -1)
						termserv(sep);
					continue;
				}
				s_in->sin_family = AF_INET;
				s_in->sin_port = sp->s_port;
				s_in->sin_addr.s_addr = htonl(INADDR_ANY);

				sep->se_ctrlnetbuf.buf = (char *) s_in;
				sep->se_ctrlnetbuf.maxlen =
					sep->se_ctrlnetbuf.len =
					sizeof (struct sockaddr_in);

				if (sep->se_fd != -1)
					termserv(sep);
			} else if (sp->s_port != sep->se_ctrladdr.sin_port) {
				sep->se_ctrladdr.sin_port = sp->s_port;
				if (sep->se_fd != -1)
					termserv(sep);
			}
			if (sep->se_fd == -1) {
				if (nservers >= maxservers) {
				    syslog(LOG_ERR,
					"%s/%s: too many services (max %d)",
					sep->se_service, sep->se_proto,
					maxservers);
				    sep->se_fd = -1;
				    continue;
				}
				setup(sep);
			}
		}
	}
	endconfig();
	/*
	 * Purge anything not looked at above.
	 * XXX - if we add a service and delete one, the new service
	 * will be added to the count of services before the deleted one
	 * is subtracted.  This means you may run out of services if this
	 * happens; however, we can't do much about that, since we get
	 * the socket for the new one before we close the one for the
	 * deleted service, and if we allowed that extra service we might
	 * run out of descriptors.
	 */
	sepp = &servtab;
	while (sep = *sepp) {
		if (sep->se_checked) {
			sepp = &sep->se_next;
			continue;
		}
		*sepp = sep->se_next;
		if (sep->se_fd != -1)
			termserv(sep);
		if (debug)
			print_service("FREE", sep);
		freeconfig(sep);
		free((char *)sep);
	}
#ifdef SYSV
	/* in case we are disabled */
	if (!standalone && (State == PM_DISABLED)) {
		for (sep = servtab; sep; sep = sep->se_next) {
			if (sep->se_fd != -1)
				termserv(sep);
		}
	}
#endif SYSV
	(void) sigsetmask(omask);
}

/*
 * Try again to establish sockets on which to listen for requests
 * for non-RPC-based services (if the attempt failed before, it was either
 * because a socket could not be created, or more likely because the socket
 * could not be bound to the service's address - probably because there was
 * already a daemon out there with a socket bound to that address).
 */
void
retry()
{
	register struct servtab *sep;
	int omask;

	/*
	 * Block SIGHUP because we traverse services linked list which is
	 * modified in SIGHUP handling. Since we are in the SIGALRM handler,
	 * it is already blocked.
	 */
	omask = sigblock(sigmask(SIGHUP));
	timingout = 0;
	for (sep = servtab; sep; sep = sep->se_next) {
		if (sep->se_fd == -1 && !sep->se_isrpc) {
			if (nservers < maxservers)
				setup(sep);
		}
	}
	(void) sigsetmask(omask);
}

/*
 * Set up to accept requests for a non-RPC-based service.  Create a socket
 * to listen for connections or datagrams, bind it to the address of that
 * service, add its socket descriptor to the list of descriptors to poll,
 * and increment the number of socket descriptors and active services.
 */
void
setup(sep)
	register struct servtab *sep;
{
	int on = 1;

	if (sep->se_socktype == SOCK_TLI) {
		int qlen;
		struct netbuf addr_bound;

		if ((sep->se_fd = tli_socket(sep->se_dev, &sep->se_info))
			< 0) {
			syslog(LOG_ERR, "%s/%s: tli_socket: %m",
				    sep->se_service, sep->se_proto);
			return;
		}

		if (sep->se_info.servtype == T_CLTS)
			qlen = 0;
		else
			qlen = 10;
		/*
		 * TLI t_bind() semantics do not guarantee that address
		 * requested is the one that will be bound. So we hope
		 * for the best but if desired address was busy, the
		 * bound address may be different. We compare after
		 * succesful tli_bind() request and retry later if
		 * a different address was bound.
		 */
		if (tli_bind(sep->se_fd, &sep->se_ctrlnetbuf, &addr_bound,
				qlen) < 0) {
			syslog(LOG_ERR, "%s/%s: tli_bind: t_errno = %d:%m",
				    sep->se_service, sep->se_proto, t_errno);
			(void) close(sep->se_fd);
			sep->se_fd = -1;
			if (!timingout) {
				timingout = 1;
				alarm(RETRYTIME);
			}
			return;
		}
		if (netbufcmp(&addr_bound, &sep->se_ctrlnetbuf) != 0) {
			/*
			 * Bound to address different than desired.
			 * close fd and try again later
			 */
			syslog(LOG_ERR, "%s/%s: tli addr was busy, retry later",
				    sep->se_service, sep->se_proto);
			if (debug)
				fprintf(debugfp,
			"setup: tli addr desired was busy, retry later\n");
			(void) close(sep->se_fd);
			sep->se_fd = -1;
			if (!timingout) {
				timingout = 1;
				alarm(RETRYTIME);
			}
			free(addr_bound.buf); /* allocated by tli_bind() */
			return;
		}
		/*
		 * Successfully bound address to TLI server,
		 * free address buffers attached to netbuf in
		 * service structure now
		 */
		free(sep->se_ctrlnetbuf.buf); /* allocated by config()  */
		sep->se_ctrlnetbuf.buf = NULL;
		free(addr_bound.buf); /* allocated by tli_bind() */
	} else {
		if ((sep->se_fd = socket(AF_INET, sep->se_socktype, 0)) < 0) {
			syslog(LOG_ERR, "%s/%s: socket: %m",
				    sep->se_service, sep->se_proto);
			return;
		}

#define	turnon(fd, opt) \
setsockopt(fd, SOL_SOCKET, opt, (char *)&on, sizeof (on))
		if (strcmp(sep->se_proto, "tcp") == 0 && (options & SO_DEBUG) &&
			turnon(sep->se_fd, SO_DEBUG) < 0)
			syslog(LOG_ERR, "setsockopt (SO_DEBUG): %m");
		if (turnon(sep->se_fd, SO_REUSEADDR) < 0)
			syslog(LOG_ERR, "setsockopt (SO_REUSEADDR): %m");
#undef turnon

		if (bind(sep->se_fd, (struct sockaddr *)&sep->se_ctrladdr,
			    sizeof (sep->se_ctrladdr)) < 0) {
			syslog(LOG_ERR, "%s/%s: bind: %m",
				    sep->se_service, sep->se_proto);
			(void) close(sep->se_fd);
			sep->se_fd = -1;
			if (!timingout) {
				timingout = 1;
				alarm(RETRYTIME);
			}
			return;
		}
		if (sep->se_socktype == SOCK_STREAM)
			listen(sep->se_fd, 10);
	}
	FD_SET(sep->se_fd, &allsock);
	nsock++;
	if (sep->se_fd > maxsock)
		maxsock = sep->se_fd;
	nservers++;
}

/*
 * Set up to accept requests for an RPC-based service.  Create a socket
 * to listen for connections or datagrams, bind it to a system-chosen
 * address, register it with the portmapper under that address, add its
 * socket descriptor to the list of descriptors to poll, and increment
 * the number of socket descriptors and active services.
 */
void
setuprpc(sep)
	register struct servtab *sep;
{
	register int i;
	short port;
	struct netbuf *na;
	struct sockaddr_in inaddr;
	struct netbuf addr_req, addr_ret;
	struct netconfig *nconf;
	char buf[32];
	int len = sizeof (struct sockaddr_in);

	if (sep->se_socktype == SOCK_TLI) {
		int qlen;

		if ((sep->se_fd = tli_socket(sep->se_dev, &sep->se_info)) < 0) {
			syslog(LOG_ERR, "%s/%s: tli_socket: %m",
				    sep->se_service, sep->se_proto);
			return;
		}

		if (sep->se_info.servtype == T_CLTS)
			qlen = 0;
		else
			qlen = 10;
		/*
		 * Note: With RPC, it is not important what address it
		 * got bound to. Whatever it gets bound to is registered
		 * with rpcbind. We request binding to a "wildard" address
		 */
		bzero((char *)&addr_req, sizeof (addr_req));
		if (tli_bind(sep->se_fd, &addr_req, &addr_ret, qlen) < 0) {
			syslog(LOG_ERR, "%s/%s: tli_bind: %m",
				    sep->se_service, sep->se_proto);
			(void) close(sep->se_fd);
			sep->se_fd = -1;
			return;
		}
	} else {
		if ((sep->se_fd = socket(AF_INET, sep->se_socktype, 0)) < 0) {
			syslog(LOG_ERR, "%s/%s: socket: %m",
				    sep->se_service, sep->se_proto);
			return;
		}
		inaddr.sin_family = AF_INET;
		inaddr.sin_port = 0;
		inaddr.sin_addr.s_addr = htonl(INADDR_ANY);
		if (bind(sep->se_fd, (struct sockaddr *)&inaddr,
			sizeof (inaddr)) < 0) {
			syslog(LOG_ERR, "%s/%s: bind: %m",
				    sep->se_service, sep->se_proto);
			(void) close(sep->se_fd);
			sep->se_fd = -1;
			return;
		}
		if (getsockname(sep->se_fd, (struct sockaddr *)&inaddr,
				&len) != 0) {
			syslog(LOG_ERR, "%s/%s: getsockname: %m",
				    sep->se_service, sep->se_proto);
			(void) close(sep->se_fd);
			sep->se_fd = -1;
			return;
		}
	}
	nconf = getnetconfigent(sep->se_proto + strlen ("rpc/"));
	if (nconf) {
		for (i = sep->se_rpc.lowvers; i <= sep->se_rpc.highvers; i++) {
			/*
			 * sep->se_proto is of the form "rpc/ticots" for rpc
			 * services
			 */
			if (sep->se_socktype == SOCK_TLI) {
				(void) rpcb_set(sep->se_rpc.prog, i, nconf,
				&addr_ret);
			} else {
				port = ntohs(inaddr.sin_port);
				(void) sprintf(buf, "0.0.0.0.%d.%d",
				    ((port >> 8) & 0xff), (port & 0xff));
				na = uaddr2taddr(nconf, buf);
				if (!na)
					continue;	/* for */

				(void) rpcb_set(sep->se_rpc.prog, i, nconf, na);
				netdir_free((char *)na, ND_ADDR);
			}
		}
		freenetconfigent(nconf);
	}
	if (sep->se_socktype == SOCK_TLI)
		free(addr_ret.buf);	/* allocated by tli_bind() */

	if (sep->se_socktype == SOCK_STREAM)
		listen(sep->se_fd, 10);
	FD_SET(sep->se_fd, &allsock);
	nsock++;
	if (sep->se_fd > maxsock)
		maxsock = sep->se_fd;
	nservers++;
}

/*
 * Shut down a service.  Unregister it if it's an RPC service, remove
 * its socket descriptor from the list of descriptors to poll, close
 * that socket descriptor, set it to -1 (to indicate that it's shut down),
 * and decrement the number of socket descriptors and active services.
 */
void
termserv(sep)
	register struct servtab *sep;
{
	if (sep->se_isrpc) {
		/*
		 * Have to do this here because there might be multiple
		 * registers of the same prognum.
		 */
		unregister(sep);
	}
	FD_CLR(sep->se_fd, &allsock);
	nsock--;
	(void) close(sep->se_fd);
	sep->se_fd = -1;
	nservers--;
}

/*
 * Unregister an RPC service.
 */
void
unregister(sep)
	register struct servtab *sep;
{
	register int i;
	register int prog;
	register struct servtab *s;

	/*
	 * Unregister the service only if it is the only RPC program
	 * with this program number.
	 */
	prog = sep->se_rpc.prog;
	for (s = servtab; s; s = s->se_next) {
		if (s == sep)	/* Ignore this one */
			continue;
		if ((s->se_checked == 0) || !s->se_isrpc ||
			(prog != s->se_rpc.prog))
			continue;
		/* Found an existing entry for that prog number */
		return;
	}

	for (i = sep->se_rpc.lowvers; i <= sep->se_rpc.highvers; i++)
		(void) rpcb_unset(sep->se_rpc.prog, i, (struct netconfig *) 0);
}

struct servtab *
enter(cp)
	struct servtab *cp;
{
	register struct servtab *sep;
	int omask;

	sep = (struct servtab *)malloc(sizeof (*sep));
	if (sep == (struct servtab *)0) {
		syslog(LOG_ERR, "Out of memory.");
		exit(-1);
	}
	*sep = *cp;
	sep->se_fd = -1;
	omask = sigblock(MASKCHLDALRMHUP);
	sep->se_next = servtab;
	servtab = sep;
	sigsetmask(omask);
	return (sep);
}

FILE	*fconfig = NULL;
struct	servtab serv;
char	line[256];
char	savedline[256];
char	*skip(), *nextline();

setconfig()
{

	if (fconfig != NULL) {
		fseek(fconfig, 0L, L_SET);
		return (1);
	}
	fconfig = fopen(CONFIG, "r");
	return (fconfig != NULL);
}

void
endconfig()
{

	if (fconfig == NULL)
		return;
	fclose(fconfig);
	fconfig = NULL;
}

struct servtab *
getconfigent()
{
	register struct servtab *sep = &serv;
	char *cp, *arg, *tp, *strp, *proto = NULL;
	static	struct proto_list *plist = NULL, *plisthead = NULL;
	char *inetd_strdup();
	int argc;
	struct rpcent *rpc;

more:
	bzero((char *) sep, sizeof (struct servtab));
	if (plist == NULL) {
		cp = nextline(fconfig);
		if (cp == (char *) 0)
			return ((struct servtab *)0);
		(void) sprintf(savedline, "%s", cp);
	} else {
		(void) sprintf(line, "%s", savedline);
		cp = line;
	}

	sep->se_service = inetd_strdup(skip(&cp));
	if (cp == (char *) 0) goto mal_formed;		/* Bad syntax */
	/* Internet service syntax must have more than 5 entries in a line. */
	/* cp == (char *) 0 means no more entries in that line.	*/
	arg = skip(&cp);
	if (cp == (char *) 0) goto mal_formed; /* Bad syntax */
	if (strcmp(arg, "stream") == 0)
		sep->se_socktype = SOCK_STREAM;
	else if (strcmp(arg, "dgram") == 0)
		sep->se_socktype = SOCK_DGRAM;
	else if (strcmp(arg, "rdm") == 0)
		sep->se_socktype = SOCK_RDM;
	else if (strcmp(arg, "seqpacket") == 0)
		sep->se_socktype = SOCK_SEQPACKET;
	else if (strcmp(arg, "raw") == 0)
		sep->se_socktype = SOCK_RAW;
	else if (strcmp(arg, "tli") == 0)
		sep->se_socktype = SOCK_TLI;
	else
		sep->se_socktype = 0;

	proto = inetd_strdup(skip(&cp));
	if (cp == (char *) 0) goto mal_formed; /* Bad syntax */

	/*
	 * If this is a TLI service, we overload the "proto" field
	 * to identify the transport provider.  If the name begins with
	 * a "/", we assume it is a full pathname to the device.
	 * Otherwise, we assume it names the final component under
	 * "/dev".
	 */
	if (sep->se_socktype == SOCK_TLI) {
		if (proto[0] != '/') {
			char buf[MAXPATHLEN];

			if (strncmp(proto, "rpc/", 4) == 0) {
				if (plist == NULL)	/* first time */
				    plisthead = plist = getprotolist(proto +
					strlen("rpc/"));
					/*
					 * keep a ptr to the head of the list
					 * so that we can free it later
					 */
				sep->se_proto = inetd_strdup(plist->pr_proto);
				if (plist->pr_next == NULL)
							/* reached the last */
					plist = NULL;
				else
					plist = plist->pr_next;
				sprintf(buf,
				    "/dev/%s", sep->se_proto + strlen("rpc/"));
			} else {
				sep->se_proto = inetd_strdup(proto);
				sprintf(buf, "/dev/%s", sep->se_proto);
			}
			sep->se_dev = inetd_strdup(buf);
		} else {
			sep->se_proto = inetd_strdup(proto);
			sep->se_dev = inetd_strdup(sep->se_proto);
		}
	}
	if (sep->se_proto == NULL)
		sep->se_proto = inetd_strdup(proto);

	if (strncmp(sep->se_proto, "rpc/", 4) == 0) {
		sep->se_isrpc = 1;
		sep->se_rpc.lowvers = 0;
		sep->se_rpc.highvers = 0;
		tp = sep->se_service;
		while (*tp != '/') {
			if (*tp == '\0') {
				sep->se_rpc.lowvers = 1;
				sep->se_rpc.highvers = 1;
				break;
			} else
				tp++;
		}
		*tp = '\0';
		if ((rpc = getrpcbyname(sep->se_service)) != NULL)
			sep->se_rpc.prog = rpc->r_number;
		else {
			strp = sep->se_service;
			sep->se_rpc.prog = strtol(strp, &strp, 10);
			if (strp != tp) {
				/*
				 * Service name isn't all-numeric, so
				 * since we didn't find it it must not
				 * be known.
				 */
				syslog(LOG_ERR, "%s/%s: unknown service",
					sep->se_service, sep->se_proto);
				freeconfig(sep);
				goto more;
			}
		}
		if (sep->se_rpc.lowvers == 0) {
			/*
			 * The service name ended with a slash, so the
			 * version number(s) are explicitly specified.
			 */
			tp++;
			strp = tp;
			sep->se_rpc.lowvers = strtol(tp, &strp, 10);
			tp = strp;
			if (*tp == '-') {
				tp++;
				strp = tp;
				sep->se_rpc.highvers = strtol(tp, &strp, 10);
				if (*strp != '\0') {
					syslog(LOG_ERR,
					    "%s/%s: bad high version number",
					    sep->se_service, sep->se_proto);
					freeconfig(sep);
					goto more;
				}
			} else if (*tp == '\0')
				sep->se_rpc.highvers = sep->se_rpc.lowvers;
			else {
				syslog(LOG_ERR, "%s/%s: bad version number",
					sep->se_service, sep->se_proto);
				freeconfig(sep);
				goto more;
			}
		if (debug && sep->se_socktype == SOCK_TLI)
			fprintf(debugfp,
"getconfigent: service=%s, socktype TLI, proto=%s, dev=%s, vers=[%d, %d]\n",
				sep->se_service, sep->se_proto, sep->se_dev,
				sep->se_rpc.lowvers, sep->se_rpc.highvers);
		}
	} else
		sep->se_isrpc = 0;

	arg = skip(&cp);
	if (cp == (char *) 0) goto mal_formed; /* Bad syntax */
	sep->se_wait = strcmp(arg, "wait") == 0;

	/*
	 * don't allow protocol UDP and flags nowait -- it will
	 * cause a race condition between inetd selecting on the
	 * socket and the service reading from it.
	 */
	if ((strcmp(sep->se_proto, "udp") == 0) && (sep->se_wait == 0)) {
		syslog(LOG_ERR,
			"mis-configured service: %s -- udp/nowait not allowed",
			sep->se_service, sep->se_proto);
		goto more;
	}

	sep->se_user = inetd_strdup(skip(&cp));
	if (cp == (char *) 0) goto mal_formed; /* Bad syntax */
	sep->se_server = inetd_strdup(skip(&cp));
	if (cp == (char *) 0) goto mal_formed; /* Bad syntax */
	if (strcmp(sep->se_server, "internal") == 0) {
		register struct biltin *bi;

		for (bi = biltins; bi->bi_service; bi++)
			if (bi->bi_socktype == sep->se_socktype &&
				strcmp(bi->bi_service, sep->se_service) == 0)
				break;
		if (bi->bi_service == 0) {
			syslog(LOG_ERR, "internal service %s/%s unknown",
				sep->se_service, sep->se_proto);
			freeconfig(sep);
			goto more;
		}
		sep->se_bi = bi;
		sep->se_wait = bi->bi_wait;
	} else
		sep->se_bi = NULL;
	argc = 0;
	for (arg = skip(&cp); cp; arg = skip(&cp))
		if (argc < MAXARGV)
			sep->se_argv[argc++] = inetd_strdup(arg);
	while (argc <= MAXARGV)
		sep->se_argv[argc++] = NULL;
	if (proto)
		free (proto);
	if (plisthead != NULL && plist == NULL) { /* free the list */
		struct proto_list *pp;
		for (pp = plisthead; pp; pp = pp->pr_next)
			if (pp->pr_proto)
				free (pp->pr_proto);
		plisthead = NULL;
	}
	return (sep);
mal_formed:
	syslog(LOG_ERR, "Bad service syntax in config file (%s)\n", CONFIG);
	goto more;   /* continue to search next entry */
}

static struct tlx_coninds *
alloc_tlx_conind(sep)
struct servtab *sep;
{
	struct tlx_coninds *conind;

	conind = (struct tlx_coninds *)
		malloc(sizeof (struct tlx_coninds));
	if (conind == NULL) {
		syslog(LOG_ERR,
			"alloc_tlx_conind:memory allocation failure: %m");
		return (NULL);
	}
	conind->tc_callp = (struct t_call *)
		t_alloc(sep->se_fd, T_CALL, T_ALL);
	if (conind->tc_callp == NULL) {
		free((char *) conind);
		syslog(LOG_ERR,
			"alloc_tlx_conind:t_call/t_alloc failure: %s:%m",
			t_strerror(t_errno));
		return (NULL);
	}
	return (conind);
}

static void
free_tlx_coninds(cp)
struct tlx_coninds *cp;
{
	if (cp != NULL) {
		(void) t_free((char *)cp->tc_callp, T_CALL);
		free((char *) cp);
	}
}

/*
 * Compare contents of netbuf for equality. Return 0 on a match
 * and 1 for mismatch
 */
static int
netbufcmp(n1, n2)
struct netbuf *n1, *n2;
{
	if (n1->len != n2-> len)
		return (1);
	if (memcmp((void *) n1->buf, (void *) n2-> buf,
	    (size_t) n1->len) != 0)
		return (1);
	return (0);
}


freeconfig(cp)
	register struct servtab *cp;
{
	int i;
	struct tlx_coninds *cip, *free_cip;

	if (cp->se_service)
		free(cp->se_service);
	if (cp->se_proto)
		free(cp->se_proto);
	if (cp->se_dev)
		free(cp->se_dev);
	if (cp->se_tlx_coninds != NULL) {
		cip = cp->se_tlx_coninds;
		while (cip != NULL) {
			free_cip = cip;
			cip = cip->tc_next;
			free_tlx_coninds(free_cip);
		}
	}
	if (cp->se_user)
		free(cp->se_user);
	if (cp->se_server)
		free(cp->se_server);
	for (i = 0; i < MAXARGV; i++)
		if (cp->se_argv[i])
			free(cp->se_argv[i]);
}

char *
skip(cpp)
	char **cpp;
{
	register char *cp = *cpp;
	char *start;

again:
	while (*cp == ' ' || *cp == '\t')
		cp++;
	if (*cp == '\0') {
	/*
	 * skip() will not check any characters on next line.  Each server
	 * entry is composed of a single line.
	 */
		*cpp = (char *)0;
		return ((char *)0);
	}
	start = cp;
	while (*cp && *cp != ' ' && *cp != '\t')
		cp++;
	if (*cp != '\0')
		*cp++ = '\0';
	*cpp = cp;
	return (start);
}

char *
nextline(fd)
	FILE *fd;
{
	char *cp, *ll;

	while ((ll = fgets(line, sizeof (line), fd)) &&
	    (*line == '\n' || *line == '#' || *line == '\t' || *line == ' '));
	/*
	 * Skip the blank line, comment line, tab and space character
	 */
	if (ll == NULL)
		return ((char *)0);
	cp = index(line, '\n');
	if (cp)
		*cp = '\0';
	return (line);
}

char *
inetd_strdup(cp)
	char *cp;
{
	char *new;

	if (cp == NULL)
		cp = "";
	new = malloc ((unsigned)(strlen(cp) + 1));
	if (new == (char *)0) {
		syslog(LOG_ERR, "Out of memory.");
		exit(-1);
	}
	(void) strcpy(new, cp);
	return (new);
}

setproctitle(a, s)
	char *a;
	int s;
{
	int size;
	register char *cp;
	struct sockaddr_in sin;
	char buf[80];

	cp = Argv[0];
	size = sizeof (sin);
	if (getpeername(s, (struct sockaddr *)&sin, &size) == 0)
		sprintf(buf, "-%s [%s]", a, inet_ntoa(sin.sin_addr));
	else
		sprintf(buf, "-%s", a);
	strncpy(cp, buf, LastArg - cp);
	cp += strlen(cp);
	while (cp < LastArg)
		*cp++ = ' ';
}

/*
 * Internet services provided internally by inetd:
 */

/* ARGSUSED */
echo_stream(s, sep)		/* Echo service -- echo data back */
	int s;
	struct servtab *sep;
{
	char buffer[BUFSIZ];
	int i;

	setproctitle("echo", s);
	while ((i = read(s, buffer, sizeof (buffer))) > 0 &&
		write(s, buffer, i) > 0)
		;
	exit(0);
}

/* ARGSUSED */
echo_dg(s, sep)			/* Echo service -- echo data back */
	int s;
	struct servtab *sep;
{
	char buffer[BUFSIZ];
	int i, size;
	struct sockaddr sa;

	/*
	 * This test was included to keep two datagram echo servers (using
	 * the  well known port number 7) from talking to each other and
	 * causing a loop - gcm
	 */
	if (((struct sockaddr_in *)&sa)->sin_port == 7)
		return;

	size = sizeof (sa);
	if ((i = recvfrom(s, buffer, sizeof (buffer), 0, &sa, &size)) < 0)
		return;
	(void) sendto(s, buffer, i, 0, &sa, sizeof (sa));
}

/* ARGSUSED */
discard_stream(s, sep)		/* Discard service -- ignore data */
	int s;
	struct servtab *sep;
{
	char buffer[BUFSIZ];

	setproctitle("discard", s);
	while (read(s, buffer, sizeof (buffer)) > 0)
		;
	exit(0);
}

/* ARGSUSED */
discard_dg(s, sep)		/* Discard service -- ignore data */
	int s;
	struct servtab *sep;
{
	char buffer[BUFSIZ];

	(void) read(s, buffer, sizeof (buffer));
}

#include <ctype.h>
#define	LINESIZ 72
char ring[128];
char *endring;

initring()
{
	register int i;

	endring = ring;

	for (i = 0; i <= 128; ++i)
		if (isprint(i))
			*endring++ = i;
}

/* ARGSUSED */
chargen_stream(s, sep)		/* Character generator */
	int s;
	struct servtab *sep;
{
	char text[LINESIZ+2];
	register int i;
	register char *rp, *rs, *dp;

	setproctitle("discard", s);
	if (endring == 0)
		initring();

	for (rs = ring; /* cstyle */; ++rs) {
		if (rs >= endring)
			rs = ring;
		rp = rs;
		dp = text;
		i = MIN(LINESIZ, endring - rp);
		bcopy(rp, dp, i);
		dp += i;
		if ((rp += i) >= endring)
			rp = ring;
		if (i < LINESIZ) {
			i = LINESIZ - i;
			bcopy(rp, dp, i);
			dp += i;
			if ((rp += i) >= endring)
				rp = ring;
		}
		*dp++ = '\r';
		*dp++ = '\n';

		if (write(s, text, dp - text) != dp - text)
			break;
	}
	exit(0);
}

/* ARGSUSED */
chargen_dg(s, sep)		/* Character generator */
	int s;
	struct servtab *sep;
{
	char text[LINESIZ+2];
	register int i;
	register char *rp;
	static char *rs = ring;
	struct sockaddr sa;
	int size;

	if (endring == 0)
		initring();

	size = sizeof (sa);
	if (recvfrom(s, text, sizeof (text), 0, &sa, &size) < 0)
		return;
	rp = rs;
	if (rs++ >= endring)
		rs = ring;
	i = MIN(LINESIZ - 2, endring - rp);
	bcopy(rp, text, i);
	if ((rp += i) >= endring)
		rp = ring;
	if (i < LINESIZ - 2) {
		bcopy(rp, text, i);
		if ((rp += i) >= endring)
			rp = ring;
	}
	text[LINESIZ - 2] = '\r';
	text[LINESIZ - 1] = '\n';

	(void) sendto(s, text, sizeof (text), 0, &sa, sizeof (sa));
}

/*
 * Return a machine readable date and time, in the form of the
 * number of seconds since midnight, Jan 1, 1900.  Since gettimeofday
 * returns the number of seconds since midnight, Jan 1, 1970,
 * we must add 2208988800 seconds to this figure to make up for
 * some seventy years Bell Labs was asleep.
 */

long
machtime()
{
	struct timeval tv;

	if (gettimeofday(&tv, (struct timezone *)0) < 0) {
#ifdef SYSV
		syslog(LOG_INFO, "Unable to get time of day\n");
#else
		fprintf(debugfp, "Unable to get time of day\n");
#endif SYSV
		return (0L);
	}
	return (htonl((long)tv.tv_sec + 2208988800));
}

/* ARGSUSED */
machtime_stream(s, sep)
	int s;
	struct servtab *sep;
{
	long result;

	result = machtime();
	(void) write(s, (char *) &result, sizeof (result));
}

/* ARGSUSED */
machtime_dg(s, sep)
	int s;
	struct servtab *sep;
{
	long result;
	struct sockaddr sa;
	int size;

	size = sizeof (sa);
	if (recvfrom(s, (char *)&result, sizeof (result), 0, &sa, &size) < 0)
		return;
	result = machtime();
	(void) sendto(s, (char *) &result, sizeof (result), 0, &sa,
		sizeof (sa));
}

/* ARGSUSED */
daytime_stream(s, sep)		/* Return human-readable time of day */
	int s;
	struct servtab *sep;
{
	char buffer[256];
	time_t time(), clock;
	char *ctime();

	clock = time((time_t *) 0);

	sprintf(buffer, "%s\r", ctime(&clock));
	(void) write(s, buffer, strlen(buffer));
}

/* ARGSUSED */
daytime_dg(s, sep)		/* Return human-readable time of day */
	int s;
	struct servtab *sep;
{
	char buffer[256];
	time_t time(), clock;
	struct sockaddr sa;
	int size;
	char *ctime();

	clock = time((time_t *) 0);

	size = sizeof (sa);
	if (recvfrom(s, buffer, sizeof (buffer), 0, &sa, &size) < 0)
		return;
	sprintf(buffer, "%s\r", ctime(&clock));
	(void) sendto(s, buffer, strlen(buffer), 0, &sa, sizeof (sa));
}

/*
 * print_service:
 *	Dump relevant information to stderr
 */
void
print_service(action, sep)
	char *action;
	struct servtab *sep;
{
	fprintf(debugfp,
		"%s: %s proto=%s, wait=%d, user=%s, builtin=%x, server=%s\n",
		action, sep->se_service, sep->se_proto, sep->se_wait,
		sep->se_user, sep->se_bi, sep->se_server);
}


/*
 * This call attempts to t_accept() a incoming/pending TLI connection.
 * If it is thwarted by a TLOOK, it is deferred and whatever is on the
 * file descriptor, removed after a t_look. (Incoming connect indications
 * get queued for later processing and disconnect indications remove a
 * a queued connection request if a match found.
 */
static int
tli_service_accept(sep, addr, len)
	struct servtab *sep;
	char *addr;
	int *len;
{
	struct tlx_coninds *free_conind;
	struct t_call *call;
	int newfd;

	if ((newfd = t_open(sep->se_dev, O_RDWR,
			    (struct t_info *) 0)) == -1) {
		syslog(LOG_ERR, "tli_accept: t_open");
		return (-1);
	}
	if (t_bind(newfd, (struct t_bind *) 0,
			(struct t_bind *) 0) == -1) {
		syslog(LOG_ERR, "tli_accept: t_bind");
		t_close(newfd);
		return (-1);
	}
	if (sep->se_tlx_coninds != NULL) {
		/*
		 * Attempt to t_accept first connection on list
		 */
		call = sep->se_tlx_coninds->tc_callp;
	} else {
		if ((call = (struct t_call *)
			t_alloc(sep->se_fd, T_CALL, T_ALL)) == NULL) {
			syslog(LOG_ERR, "tli_accept: t_alloc");
			return (-1);
		}
		if (t_listen(sep->se_fd, call) == -1) {
			syslog(LOG_ERR, "tli_accept: t_listen");
			(void) t_free((char *)call, T_CALL);
			(void) t_close(newfd);
			return (-1);
		}
	}
	if (t_accept(sep->se_fd, newfd, call) == -1) {
		struct tlx_coninds *free_conind;
		if (t_errno == TLOOK) {
			if (sep->se_tlx_coninds == NULL) {
				/*
				 * We are first one to have to defer accepting
				 * and start the pending connections list
				 */
				struct tlx_coninds *conind;

				if (debug)
					fprintf(debugfp,
			"First TLOOK while t_accept'ing tli service %s\n",
						sep->se_service);
				conind = (struct tlx_coninds *)
					malloc(sizeof (struct tlx_coninds));
				if (conind == NULL) {
					syslog(LOG_ERR,
			"tlx_service_accept:memory allocation failure: %m");
					(void) t_free((char *)call, T_CALL);
					(void) t_close(newfd);
					return (-1);
				}
				conind->tc_callp = call;
				conind->tc_next = NULL;
				sep->se_tlx_coninds = conind;
			} else {
				if (debug)
					fprintf(debugfp,
			"Yet another TLOOK while t_accept'ing tli service %s\n",
						sep->se_service);
			}
			(void) do_tlook(sep);
		} else {
			if (debug)
				fprintf(debugfp,
					"tli_accept:t_accept failed:%s\n",
					t_strerror(t_errno));
			syslog(LOG_ERR, "tli_accept: t_accept failed %s:%m",
				t_strerror(t_errno));
			/*
			 * dequeue from list if it was a pending
			 * queued connection.
			 */
			if (sep->se_tlx_coninds != NULL) {
				free_conind = sep->se_tlx_coninds;
				sep->se_tlx_coninds  = free_conind->tc_next;
				free_tlx_coninds(free_conind);
			} else {
				(void) t_free((char *)call, T_CALL);
			}
		}
		t_close(newfd);
		return (-1);
	}
	/*
	 * Sucessful accept - initialize return parameters
	 */
	bcopy(call->addr.buf, addr,
		MIN(call->addr.len, sizeof (struct sockaddr_in)));
	*len = call->addr.len;
	/*
	 * dequeue from list if it was a pending queued connection.
	 * Note: If it is a pending connection, it is the first one
	 * on the list that is accepted.
	 */
	if (sep->se_tlx_coninds != NULL) {
		free_conind = sep->se_tlx_coninds;
		sep->se_tlx_coninds  = free_conind->tc_next;
		free_tlx_coninds(free_conind);
		if (debug)
			fprintf(debugfp, "accepted pending tli service %s\n",
				sep->se_service);
	} else {
		(void) t_free((char *)call, T_CALL);
		if (debug)
			fprintf(debugfp,
				"accepted new incoming tli service %s\n",
				sep->se_service);
	}
	return (newfd);
}


static int
do_tlook(sep)
struct servtab *sep;
{
	int event;
	switch (event = t_look(sep->se_fd)) {
	case T_LISTEN:
		{
			struct tlx_coninds *conind, *cip;

			if (debug)
				fprintf(debugfp, "do_tlook: T_LISTEN event\n");

			conind = alloc_tlx_conind(sep);
			if (conind == NULL) {
				if (debug)
					fprintf(debugfp,
					"do_tlook: alloc_tlx_conind failed\n");
				syslog(LOG_WARNING,
					"do_tlook:alloc_tlx_conind failed: %m");
				return (-1);
			}
			if (t_listen(sep->se_fd, conind->tc_callp) == -1) {
				syslog(LOG_WARNING,
	"do_tlook: t_listen failure after t_look detects T_LISTEN event");
				free_tlx_coninds(conind);
				return (-1);
			}
			/*
			 * Append incoming connection request to list
			 * pending connections
			 */
			conind->tc_next = NULL;
			if (sep->se_tlx_coninds != NULL) {
				cip = sep->se_tlx_coninds;
				while (cip->tc_next != NULL)
					cip = cip->tc_next;
				cip->tc_next = conind;
			} else {
				/*
				 * Mild form of assertion failure.
				 * We should not be in
				 * do_tlook() if there is nothing on pending
				 * list ! Free the incoming request, log
				 * error and return.
				 */
				free_tlx_coninds(conind);
				if (debug)
					fprintf(debugfp,
			"do_tlook: nothing on pending connections list\n");
				syslog(LOG_ERR,
			"do_tlook: nothing on pending connections list\n");
				return (-1);
			}
		}
		break;

	case T_DISCONNECT:
		{
			/*
			 * Note: In Solaris 2.X (SunOS 5.X) bundled
			 * connection-oriented transport drivers
			 * [ e.g /dev/tcp and /dev/ticots and
			 * /dev/ticotsord (tl)] we do not send disconnect
			 * indications to listening endpoints.
			 * So this will not be seen with endpoints on Solaris
			 * bundled transport devices. However, Streams TPI
			 * allows for this (broken?) behavior and so we account
			 * for it here because of the possibility of unbundled
			 * transport drivers causing this.
			 */
			struct t_discon *discon;
			struct tlx_coninds *cip, **prevcip_nextpp;

			if (debug)
				fprintf(debugfp,
					"do_tlook: T_DISCONNECT event\n");

			discon = (struct t_discon *)
				t_alloc(sep->se_fd, T_DIS, T_ALL);
			if (discon == NULL) {
				syslog(LOG_ERR,
					"discon:t_alloc failure: %m");
				return (-1);
			}
			if (t_rcvdis(sep->se_fd, discon) == -1) {
				syslog(LOG_WARNING,
	"do_tlook: t_rcvdis failure after t_look detects T_DISCONNECT event");
				(void) t_free((char *) discon, T_DIS);
				return (-1);
			}
			/*
			 * Find any connection pending that matches this
			 * disconnect request and remove from list.
			 */
			prevcip_nextpp = &sep->se_tlx_coninds;
			cip = sep->se_tlx_coninds;
			while ((cip != NULL) &&
				(cip->tc_callp->sequence != discon->sequence)) {

				prevcip_nextpp = &cip->tc_next;
				cip = cip->tc_next;
			}
			if (cip) {
				/*
				 * remove this matching connection request
				 * from list of pending connections
				 */
				*prevcip_nextpp = cip->tc_next;
				free_tlx_coninds(cip);
			}
			(void) t_free((char *) discon, T_DIS);
		}
		break;
	case -1:
		syslog(LOG_ERR, "do_tlook: t_look failed\n");
		if (debug)
			fprintf(debugfp,
				"do_tlook:t_look failed\n");
		return (-1);

	default:
		syslog(LOG_ERR, "do_tlook: unexpected t_look event:%d", event);
		if (debug)
			fprintf(debugfp,
				"do_tlook:unexpected event:%d", event);
		return (-1);
	}
	return (0);		/* OK return */
}

/*
 * A routine that doesn't look a whole lot like socket(), but
 * has the same effect, and uses tli.
 */
int
tli_socket(dev, info)
	char *dev;
	struct t_info *info;
{
	return (t_open(dev, O_RDWR, info));
}


/*
 * Routine that does stuff needed for TLI address binding.
 * Note: The address requested is passed in the "reqaddr" netbuf.
 * The address actually bound is returned in "retaddr" netbuf.
 * The buffer attached to "retaddr" netbuf (retaddr->buf)
 * is allocated in this routine
 */
int
tli_bind(fd, reqaddr, retaddr, qlen)
	int fd;
	struct netbuf *reqaddr, *retaddr;
	int qlen;
{
	struct t_bind req, *ret;

	if (((ret = (struct t_bind *) t_alloc (fd, T_BIND, T_ALL)) ==
		    (struct t_bind *) NULL)) {
		if ((t_errno == TSYSERR) || (t_errno > t_nerr))
			syslog(LOG_ERR, "tli_bind: t_alloc: %m");
		else
			syslog(LOG_ERR, "tli_bind: t_alloc: %s",
				    t_errlist[t_errno]);
		return (-1);
	}
	if (reqaddr->len) {
		req.addr.len = reqaddr->len;
		req.addr.maxlen = reqaddr->maxlen;
		req.addr.buf = (char *)reqaddr->buf;
	} else {
		req.addr.len = req.addr.maxlen = 0;
		req.addr.buf = (char *) 0;
	}

	req.qlen = qlen;
	if (t_bind (fd, &req, ret) == -1) {
		if (debug) {
			fprintf(debugfp,
			    "tli_bind:  t_bind failed (t_errno %d", t_errno);
			if (t_errno == TSYSERR)
				fprintf(debugfp, ", errno %d", errno);
			fprintf(debugfp, ")\n");
		}
		if ((t_errno == TSYSERR) || (t_errno > t_nerr))
			syslog(LOG_ERR, "tli_bind:  t_bind: %m");
		else
			syslog(LOG_ERR, "tli_bind: t_bind: %s",
				    t_errlist[t_errno]);
		(void) t_free ((char *) ret, T_BIND);
		return (-1);
	} else
		if (debug)
			fprintf(debugfp, "tli_bind:  t_bind succeeded\n");

	/*
	 * return bound address to user, unlike bind()
	 */
	memcpy(retaddr, &ret->addr, sizeof (struct netbuf));
	ret->addr.buf = (char *) 0;
	(void) t_free ((char *) ret, T_BIND);
	return (0);
}

#ifdef SYSV

#include <sys/resource.h>

#define	NOFILES 20	/* just in case */

int
getdtablesize()
{
	struct rlimit	rl;

	if (getrlimit(RLIMIT_NOFILE, &rl) == 0)
		return (rl.rlim_max);
	else
		return (NOFILES);
}


void
sacmesg()
{
	struct sacmsg sacmsg;
	register struct servtab *sep;

	if (read(Pfd, &sacmsg, sizeof (sacmsg)) < 0) {
		syslog(LOG_ERR, "Could not read _pmpipe");
		exit(-1);
	}
	switch (sacmsg.sc_type) {
	case SC_STATUS:
		Pmmsg.pm_type = PM_STATUS;
		Pmmsg.pm_state = State;
		break;
	case SC_ENABLE:
		syslog(LOG_INFO, "got SC_ENABLE message");
		if (State != PM_ENABLED) {
			for (sep = servtab; sep; sep = sep->se_next) {
				if ((sep->se_fd == -1) && (nservers <
				    maxservers)) {
					if (sep->se_isrpc)
						setuprpc(sep);
					else
						setup(sep);
				}
			}
		}
		State = PM_ENABLED;
		Pmmsg.pm_type = PM_STATUS;
		Pmmsg.pm_state = State;
		break;
	case SC_DISABLE:
		syslog(LOG_INFO, "got SC_DISABLE message");
		if (State != PM_DISABLED) {
			for (sep = servtab; sep; sep = sep->se_next) {
				if (sep->se_fd != -1)
					termserv(sep);
			}
		}
		State = PM_DISABLED;
		Pmmsg.pm_type = PM_STATUS;
		Pmmsg.pm_state = State;
		break;
	case SC_READDB:
		/* ignore these, inetd doesn't use _pmtab's right now */
		syslog(LOG_INFO, "got SC_READDB message");
		Pmmsg.pm_type = PM_STATUS;
		Pmmsg.pm_state = State;
		break;
	default:
		syslog(LOG_WARNING, "got unknown message <%d> from sac",
			sacmsg.sc_type);
		Pmmsg.pm_type = PM_UNKNOWN;
		Pmmsg.pm_state = State;
		break;
	}
	if (write(Sfd, &Pmmsg, sizeof (Pmmsg)) != sizeof (Pmmsg))
		syslog(LOG_WARNING, "sanity response failed");
}

void
sigterm()
{
	syslog(LOG_INFO, "inetd terminating");
	exit(0);
}
#endif SYSV


usage(cmdname)
char *cmdname;
{
	fprintf(stderr,
	    "usage: %s -s [-d] [-t] [-r <count> <interval>] [<config_file>]\n",
		cmdname);
}

struct proto_list *
getprotolist(proto)
char *proto;
{
	register	struct	proto_list	*pl;
	struct	proto_list	*plisttail, *plisthead = NULL;
	struct	netconfig	*nconf;
	void	*handle;
	char	buf[BUFSIZ], buf2[MAXPATHLEN],
			*ix, *start;
	char	*inetd_strdup();

	(void) sprintf(buf, "%s", proto);
	if (buf[0] == '*')
		(void) sprintf (buf, "visible");
	for (start = &buf[0]; (ix = strchr(start, ', ')) || *start;
					start = ix + 1) {
		if (ix != NULL)
			*ix = '\0';
		if ((handle = __rpc_setconf(start)) != NULL) {
							/* valid nettype */
			while (nconf = __rpc_getconf(handle)) {
				pl = (struct proto_list *)
					malloc(sizeof (struct proto_list));
				(void) sprintf (buf2,
					"rpc/%s", nconf->nc_netid);
				pl->pr_proto = inetd_strdup(buf2);
				pl->pr_next = NULL;
				if (plisthead == NULL)
					plisthead = plisttail = pl;
				else {
					plisttail->pr_next = pl;
					plisttail = plisttail->pr_next;
				}
			}
			__rpc_endconf(handle);
		} else {	/* a netid */
			pl = (struct proto_list *)
				malloc(sizeof (struct proto_list));
			(void) sprintf (buf2, "rpc/%s", start);
			pl->pr_proto = inetd_strdup(buf2);
			pl->pr_next = NULL;
			if (plisthead == NULL)
				plisthead = plisttail = pl;
			else {
				plisttail->pr_next = pl;
				plisttail = plisttail->pr_next;
			}
		}
		if (ix == NULL)	/* no more */
			break;
	}
	if (debug && plisthead != NULL)
	    for (pl = plisthead; pl; pl = pl->pr_next)
		fprintf(debugfp, "plisthead->pr_proto %s \n", pl->pr_proto);
	return (plisthead);
}
