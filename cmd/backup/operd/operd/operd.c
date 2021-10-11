/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#ident	"@(#)operd.c 1.0 91/02/10 SMI"

#ident	"@(#)operd.c 1.14 93/06/23"

#include "operd.h"
#include <config.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#ifdef USG
#include <rpc/svc_soc.h>
#endif
#include <rpc/pmap_clnt.h>

#ifdef __STDC__
static void operd_dispatch(struct svc_req *, SVCXPRT *);
#else
static void operd_dispatch();
#endif

main(argc, argv)
	int	argc;
	char	**argv;
{
	register SVCXPRT *transp;
	struct rlimit rl;
	char *progname;

	progname = strrchr(argv[0], '/');
	if (progname == (char *)0)
		progname = argv[0];
	else
		progname++;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	/*
	 * In order to have operd scale better (not well) to large sites,
	 * we increase the file descriptor limit to the max.  This allows
	 * more clients and file systems to be connected at once.
	 */
	if (getrlimit(RLIMIT_NOFILE, &rl) != -1) {
		rl.rlim_cur = rl.rlim_max;
		(void) setrlimit(RLIMIT_NOFILE, &rl);
	}

#ifndef DEBUG
	if (geteuid()) {
		(void) fprintf(stderr, gettext("must be run as root\n"));
		exit(1);
	}

	{
		register int t;
		int maxfds = (int)sysconf(_SC_OPEN_MAX);
		int pid;

		if (maxfds == -1)
			maxfds = 20;
		pid = fork();
		if (pid == -1) {
			perror("fork");
			exit(-1);
		}
		if (pid != 0)
			exit(0);
		for (t = 0; t < maxfds; t++)
			(void) close(t);
		(void) open("/", 0);
		(void) dup2(0, 1);
		(void) dup2(0, 2);
		(void) setsid();
	}
#endif
	(void) pmap_unset(OPERMSG_PROG, OPERMSG_VERS);

	transp = svctcp_create(RPC_ANYSOCK, 0, 0);
	if (transp == NULL) {
		(void) fprintf(stderr, gettext("cannot create tcp service.\n"));
		exit(1);
	}
	if (!svc_register(transp, OPERMSG_PROG, OPERMSG_VERS,
	    operd_dispatch, IPPROTO_TCP)) {
		(void) fprintf(stderr, gettext(
		    "unable to register (OPERMSG_PROG, OPERMSG_VERS, tcp).\n"));
		exit(1);
	}

	transp = svcudp_create(RPC_ANYSOCK);
	if (transp == NULL) {
		(void) fprintf(stderr, gettext("cannot create udp service.\n"));
		exit(1);
	}
	if (!svc_register(transp, OPERMSG_PROG, OPERMSG_VERS,
	    operd_dispatch, IPPROTO_UDP)) {
		(void) fprintf(stderr, gettext(
		    "unable to register (OPERMSG_PROG, OPERMSG_VERS, udp).\n"));
		exit(1);
	}

	init(argc, argv);

	svc_run();
	(void) fprintf(stderr, gettext("svc_run returned"));
	exit(1);
#ifdef lint
	return (1);
#endif
}

sigjmp_buf	connbuf;

static void
operd_dispatch(rqstp, transp)
	struct svc_req *rqstp;
	register SVCXPRT *transp;
{
	struct authunix_parms *unix_cred;
	struct fwdent *f;
	msg_dest dest;
	msg_t	 msg;
	int	result;
	int	uid, gid;
	xdrproc_t proc;
	char	*arg;

	switch (rqstp->rq_proc) {
	case NULLPROC:
		if (sigsetjmp(connbuf, 1) == 0) {
			ready = 1;
			(void) svc_sendreply(transp, xdr_void, (char *)NULL);
			return;
		}
		ready = 0;
		break;

	case OPER_LOGIN:
		(void) bzero((char *)&dest, sizeof (dest));
		proc = xdr_msg_dest;
		arg = (char *)&dest;
		if (!svc_getargs(transp, xdr_msg_dest, (caddr_t)&dest)) {
			svcerr_decode(transp);
			return;
		}
#ifdef DEBUG
		debug("RCVD login %d.%d@%s\n",
		    dest.md_callback, dest.md_gen, dest.md_host);
#endif
		result = add_fwd(&dest, 0);
		if (sigsetjmp(connbuf, 1)) {
			rm_fwd(find_fwd(&dest));
			ready = 0;
			break;
		}
		ready = 1;
		if (!svc_sendreply(transp, xdr_int, (caddr_t)&result)) {
			svcerr_systemerr(transp);
			break;
		}
		ready = 0;
		if (result >= 0) {
			if (dest.md_callback == OPERMSG_PROG) {
				/*
				 * Initial login message from another
				 * daemon; do reverse login then
				 * forward login to other daemons.
				 */
				result = oper_login(dest.md_host, 0);
#ifdef DEBUG
				debug("LOGIN to %d@%s %s\n",
				    dest.md_callback, dest.md_host,
				    result >= 0 ? "(succeeded)" :  "(failed)");
#endif
				forward_log(&dest, OPER_LOGIN);
			} else if (dest.md_callback == 0) {
				/*
				 * Response to our initial login
				 * message; if we don't have any
				 * messages in our cache, try to
				 * get some.
				 */
				if (msgcnt == 0) {
					result = oper_getall(dest.md_host);
#ifdef DEBUG
					debug("GETALL from %s %s\n",
					    dest.md_host, result >= 0 ?
						"(succeeded)" :  "(failed)");
#endif
				}
			} else {
				/*
				 * Login message from a user program
				 * (e.g., opermon); send it our cache.
				 */
				dest.md_gen = (time_t)0;
				send_all(&dest);
			}
		}
		break;

	case OPER_LOGOUT:
		proc = xdr_msg_dest;
		arg = (char *)&dest;
		(void) bzero((char *)&dest, sizeof (dest));
		if (!svc_getargs(transp, xdr_msg_dest, (caddr_t)&dest)) {
			svcerr_decode(transp);
			return;
		}
#ifdef DEBUG
		debug("RCVD logout %d.%d@%s\n",
		    dest.md_callback, dest.md_gen, dest.md_host);
#endif
		f = find_fwd(&dest);
		if (f) {
			rm_fwd(f);
			result = 0;
		} else
			result = -1;
		if (sigsetjmp(connbuf, 1) == 0) {
			ready = 1;
			if (!svc_sendreply(transp, xdr_int, (caddr_t)&result)) {
				svcerr_systemerr(transp);
				break;
			}
		}
		ready = 0;
		if (result >= 0 && dest.md_callback == OPERMSG_PROG) {
			/*
			 * Logout message from another daemon;
			 * forward to other daemons
			 */
			forward_log(&dest, OPER_LOGOUT);
		}
		break;

	case OPER_SEND:
		proc = xdr_msg_t;
		arg = (char *)&msg;
		(void) bzero((char *)&msg, sizeof (msg));
		if (!svc_getargs(transp, xdr_msg_t, (caddr_t)&msg)) {
			svcerr_decode(transp);
			return;
		}
		/*
		 * Check for forged messages:  if the incoming
		 * message's RPC credentials indicate it came
		 * from someone other than a super-user, make
		 * sure the uid and gid in the message match the
		 * RPC credentials.
		 */
		switch (rqstp->rq_cred.oa_flavor) {
		case AUTH_UNIX:
			/*LINTED [aligned ok]*/
			unix_cred = (struct authunix_parms *)rqstp->rq_clntcred;
			uid = unix_cred->aup_uid;
			gid = unix_cred->aup_gid;
			break;
		case AUTH_NULL:
		default:
			svcerr_weakauth(transp);
			goto sendout;
		}
		if (uid && (uid != msg.msg_uid || gid != msg.msg_gid)) {
			svcerr_weakauth(transp);
			goto sendout;
		}
		busy = 1;
		result = add_msg(&msg);
		busy = 0;
		if (sigsetjmp(connbuf, 1) == 0) {
			ready = 1;
			if (!svc_sendreply(transp, xdr_int, (caddr_t)&result)) {
				svcerr_systemerr(transp);
				break;
			}
		}
		ready = 0;
		if (result >= 0)
			domsg(&msg);
sendout:
		break;

	case OPER_SENDALL:
		proc = xdr_msg_dest;
		arg = (char *)&dest;
		(void) bzero((char *)&dest, sizeof (msg_dest));
		if (!svc_getargs(transp, xdr_msg_dest, (caddr_t)&dest)) {
			svcerr_decode(transp);
			return;
		}
#ifdef DEBUG
		debug("RCVD sendall %d.%d@%s\n",
		    dest.md_callback, dest.md_gen, dest.md_host);
#endif
		result = 0;
		if (sigsetjmp(connbuf, 1) == 0) {
			ready = 1;
			if (!svc_sendreply(transp, xdr_int, (caddr_t)&result)) {
				svcerr_systemerr(transp);
				break;
			}
		}
		ready = 0;
		send_all(&dest);
		break;

	default:
		svcerr_noproc(transp);
		return;
	}
	if (!svc_freeargs(transp, proc, arg)) {
		(void) fprintf(stderr, gettext("unable to free arguments\n"));
		exit(1);
	}
	(void) waitpid(-1, 0, WNOHANG);
}
