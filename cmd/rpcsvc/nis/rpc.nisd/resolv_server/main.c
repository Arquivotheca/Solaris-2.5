/* Copyright (c) 1993 Sun Microsystems Inc */

#pragma	ident	"@(#)main.c	1.3	94/10/14 SMI"

#include <varargs.h>
#ifdef TDRPC
#include <fcntl.h>
#else
#include <sys/systeminfo.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/errno.h>
#include <sys/resource.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <rpc/rpc.h>
#include <unistd.h>
#include <syslog.h>	/* 5.x includes stdarg.h, varargs.h */
#include <rpcsvc/yp_prot.h>
#include "prnt.h"

#define	YPDNSPROG	123456L /* default prognum: not used */
#define	YPDNSVERS	2L

extern int optind, opterr;
extern char *optarg;
extern void dispatch();

struct timeval start_time;	/* Time service started running.	*/
int verbose = 0;		/* Verbose mode, 0=off, 1=on		*/
int verbose_out = 0;		/* Verbose ouput, 0=log, 1=stdout	*/
int background = TRUE;		/* Forground or bkgrd			*/
int dtbsize;			/* for select				*/
pid_t ppid = 0;			/* for terminating if nisd dies		*/
long prognum = YPDNSPROG;	/* for cleanup (may want transient)	*/
SVCXPRT *reply_xprt;		/* used for sendreply			*/
#ifndef TDRPC
struct netconfig *udp_nconf;	/* used to set xprt caller in dispatch  */
#endif

/* The -v -V is used to distinguiish between verbose to stdout
 * versus verbose to syslog rather then the background flag
 * because this may be started  from a daemon (nisd). When started
 * from a daemon we can't use forground to print to stdout.
 */
static void usage()
{
	fprintf(stderr,
	"usage: rpc.nisd_resolv [-v|-V] [-F[-C xx]] [-t xx] [-p yy]\n");
	fprintf(stderr, "Options supported by this version :\n");
	fprintf(stderr, "\tF - run in forground\n");
	fprintf(stderr, "\tC fd - use fd for service xprt (from nisd)\n");
	fprintf(stderr, "\tv - verbose syslog\n");
	fprintf(stderr, "\tV - verbose stdout\n");
	fprintf(stderr, "\tt xx - use transport xxx\n");
	fprintf(stderr, "\tp yy - use transient program# yyy\n");
	fprintf(stderr, "Use SIGUSR1 to toggle verbose mode.\n");
	exit(1);
}

void prnt(info_or_err, format, va_alist)
int info_or_err;
char *format;
va_dcl
{
	va_list ap;

	va_start(ap);
	if (info_or_err == 1 /* error */) {
		if (verbose_out) {
			(void) vfprintf(stderr, format, ap);
		} else
			(void) vsyslog(LOG_ERR, format, ap);
	} else if (verbose) {
		if (verbose_out) {
			(void) vfprintf(stdout, format, ap);
		} else
			(void) vsyslog(LOG_INFO, format, ap);
	}

	va_end(ap);
}

void cleanup(sig)
int sig;
{
	/* unreg prog */
	if (sig) prnt(P_INFO, "unregistering resolv server and exiting.\n");
#ifdef TDRPC
	(void) pmap_unset(prognum, YPDNSVERS);
#else
	(void) rpcb_unset(prognum, YPDNSVERS, NULL);
#endif
	exit(0);
}

void toggle_verbose(sig)
int sig;
{
	signal (SIGUSR1, toggle_verbose);

	if (verbose) {
		prnt(P_INFO, "verbose turned off.\n");
		verbose = FALSE;
	} else {
		verbose = TRUE;
		prnt(P_INFO, "verbose turned on.\n");
	}
}

main(argc, argv)
	int	argc;
	char	*argv[];
{
	int			i;
	int			use_fd = -2; /* not set */
	short			port;
	char			buf[80];
	fd_set 			readfds;
	int			c;
	struct rlimit		rl;
	time_t			t;
	SVCXPRT 		*transp;
#ifdef TDRPC
	char			*t_type = "tcp";
	int 			using_xp = -1;
	struct sockaddr_in	addr;
#else
	char			*t_type = "ticots";
	struct netconfig	*nc;
	struct netbuf		svcaddr;
#endif
	/*
	 * Process the command line arguments
	 */
	opterr = 0;
	chdir("/var/nis");
	while ((c = getopt(argc, argv, "vVFC:p:t:")) != -1) {
		switch (c) {
			case 'v' : /* syslog */
				verbose = TRUE;
				verbose_out = 0;
				break;
			case 'V' : /* stdout */
				verbose = TRUE;
				verbose_out = 1;
				break;
			case 'F' :
				background = FALSE;
				break;
			case 'C' :
				use_fd = atoi(optarg);
				break;
			case 't' :
				t_type = optarg;
				break;
			case 'p' :
				prognum = atol(optarg);
				break;
			case '?':
				usage();
		}
	}

	getrlimit(RLIMIT_NOFILE, &rl);
	if (background)  {
		switch (fork()) {
		case -1:
			fprintf(stderr, "Couldn't fork a process: exiting.\n");
			exit(1);
		case 0:
			break;
		default:
			exit(0);
		}

		for (i = 0; i < rl.rlim_max; i++)
			(void) close(i);

		(void) open("/dev/null", O_RDONLY);
		(void) open("/dev/null", O_WRONLY);
		(void) dup(1);
	} else { /* forground */
		/* pardon the mess: due to transient p# requirement */
		switch (use_fd){
		case -2: /* -C not used: just close stdin */
			(void) close (0);
			break;
		case -1: /* close all (nisd non transient) */
			for (i = 0; i < rl.rlim_max; i++)
				(void) close(i);
			break;
		default: /* use use_fd as svc fd; close others (nisd trans) */
			ppid = getppid();
			for (i = 0; i < rl.rlim_max; i++)
				if (i != use_fd)
					(void) close(i);
			break;
		}
	}

	if (!verbose_out)
		openlog("rpc.nisd_resolv", LOG_PID+LOG_NOWAIT, LOG_DAEMON);

#ifndef TDRPC
	rl.rlim_cur = 1024;
	rl.rlim_max = 1024;
	setrlimit(RLIMIT_NOFILE, &rl);
#endif

#ifdef TDRPC
	/* fix for bug# 1107607 */
	signal(SIGPIPE, SIG_IGN);
#endif

	/* Initialize the various parameters for select */
	getrlimit(RLIMIT_NOFILE, &rl);
	dtbsize = rl.rlim_cur;

	signal (SIGUSR1, toggle_verbose);
	signal (SIGINT, cleanup);
	signal (SIGQUIT, cleanup);
	signal (SIGTERM, cleanup);

	gettimeofday(&start_time, 0);
	prnt(P_INFO, "Starting nisd resolv server: %s",
					ctime((time_t*)&start_time.tv_sec));

	/* no check for already running since using transient */

#ifdef TDRPC
	if (use_fd != -1 && use_fd != -2) {
		using_xp = 0; /* parent did pmap_set(): just add to callout */
	} else {
		use_fd = RPC_ANYSOCK;
		pmap_unset(prognum, YPDNSVERS);
	}

	/* if TDRPC use tcp or udp xprt for rpc.nisd reqs */
	if (!strcmp(t_type, "udp")) {
		if (using_xp != 0) using_xp = IPPROTO_UDP;
		transp = svcudp_bufcreate(use_fd, YPMSGSZ, YPMSGSZ);
	} else {
		if (using_xp != 0) using_xp = IPPROTO_TCP;
		transp = svctcp_create(use_fd, YPMSGSZ, YPMSGSZ);
	}
	if (transp == NULL) {
		prnt(P_ERR, "cannot create resolv server transport.\n");
		exit(1);
	}

	if (!svc_register(transp, prognum, YPDNSVERS, dispatch, using_xp)){
		prnt(P_ERR, "cannot register resolv service xprt.\n");
		exit(1);
	}

	/* Need udp xprt for sendreply()s, but don't reg it. */
	reply_xprt = svcudp_bufcreate(RPC_ANYSOCK, YPMSGSZ, YPMSGSZ);
	if (reply_xprt == NULL) {
		prnt(P_ERR, "cannot create udp resolv service xprt.\n");
		exit(1);
	} else
		prnt(P_INFO, "created sendreply handle.\n");
#else
	if ((nc = getnetconfigent (t_type)) == NULL){
		prnt(P_ERR, "cannot get %s netconf.\n", t_type);
		exit(1);
	}
	if (use_fd != -1 && use_fd != -2) {
		/* use passed in fd = use_fd */
		transp = svc_tli_create(use_fd, nc, NULL, YPMSGSZ, YPMSGSZ);
		if (transp == NULL){
			prnt(P_ERR, "cannot create service xprt.\n");
			exit(1);
		}
		/* parent did rpcb_set(): just add to callout */
		if (!svc_reg(transp, prognum, YPDNSVERS, dispatch, NULL)){
			prnt(P_ERR, "cannot register service xprt.\n");
			exit(1);
		}
	} else {
		rpcb_unset(prognum, YPDNSVERS, NULL);
		if (!svc_tp_create(dispatch, prognum, YPDNSVERS, nc)){
			prnt(P_ERR, "cannot create resolv service.\n");
			exit(1);
		}
	}
	freenetconfigent(nc);

	/* Need udp xprt for sendreply()s, but don't reg it. */
	if ((udp_nconf = getnetconfigent("udp")) == NULL){
		prnt(P_ERR, "cannot get udp netconf.\n");
		exit(1);
	}
	reply_xprt = svc_tli_create(RPC_ANYFD, udp_nconf, NULL,
						YPMSGSZ, YPMSGSZ);
	if (reply_xprt == NULL) {
		prnt(P_ERR, "cannot create udp xprt.\n");
		exit(1);
	} else
		prnt(P_INFO, "created sendreply handle.\n");

	/* keep udp_nconf which is used later to create nbuf from uaddr */
#endif

	prnt(P_INFO, "entering loop.\n");

	svc_run_as();
}
