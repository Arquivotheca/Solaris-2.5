#ident	"@(#)dbserv_svc.c 1.34 93/10/05"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include <signal.h>
#include <setjmp.h>
#include <sys/wait.h>
#include <config.h>
#include "defs.h"
#include "rpcdefs.h"
#include "dboper.h"
#include <rpc/pmap_clnt.h>
#include <rpc/auth.h>
#ifdef USG
#include <netdir.h>
#include <sys/socket.h>
#include <rpc/svc_soc.h>
#endif
#include <sys/time.h>
#include <sys/resource.h>

char	*myname;
char	opserver[BCHOSTNAMELEN];
char	mydomain[MAXHOSTNAMELEN];

int	updatecnt;
time_t	dbupdatetime;
int	nmapblocks = 100;

static jmp_buf pipebuf;
static int schedule_closefiles;

#ifdef __STDC__
static void pipehandler(int);
static void exitonsig(void);
static void usage(void);
static void dbserv_1(struct svc_req *, SVCXPRT *);
static char *returnargs(char *);
static int *nullproc(void);
static struct readdata *noreads(void);
static void sigchld(int);
#else
static void pipehandler();
static void exitonsig();
static void usage();
static void dbserv_1();
static char *returnargs();
static int *nullproc();
static struct readdata *noreads();
static void sigchld();
#endif

/*ARGSUSED*/
static void
pipehandler(sig)
	int sig;
{
	longjmp(pipebuf, 1);
}

/*ARGSUSED*/
static void
#ifdef __STDC__
exitonsig(void)
#else
exitonsig()
#endif
{
	exit(0);
}

static void
#ifdef __STDC__
usage(void)
#else
usage()
#endif
{
	(void) fprintf(stderr, gettext(
		"usage: %s [ -m mapsize ] dbroot_dir\n"), myname);
	exit(1);
}

main(argc, argv)
	int argc;
	char *argv[];
{
	register SVCXPRT *transp;
	struct sigvec pipevec, chldvec;
	struct rlimit rl;
	int n, i, c, badopt;
	extern char *optarg;
	int nblks;
	extern int optind;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	if (myname = (char *)strrchr(argv[0], '/'))
		myname++;
	else
		myname = argv[0];

	/*
	 * since we catch SIGCHLD, we do a LOG_NOWAIT.
	 */
	openlog(myname, LOG_CONS | LOG_NOWAIT, LOG_DAEMON);

	if (getuid() != (int)0) {
		(void) fprintf(stderr,
			gettext("%s: Must be run by root\n"), myname);
		exit(1);
	}

	if (getdomainname(mydomain, MAXHOSTNAMELEN) < 0)
		mydomain[0] = '\0';

	/*
	 * In order to have dumpdbd scale better (not well) to large sites,
	 * we increase the file descriptor limit to the max.  This allows
	 * more clients and file systems to be connected at once.
	 */
	if (getrlimit(RLIMIT_NOFILE, &rl) != -1) {
		rl.rlim_cur = rl.rlim_max;
		(void) setrlimit(RLIMIT_NOFILE, &rl);
	}

	badopt = 0;
	while ((c = getopt(argc, argv, "m:")) != -1) {
		switch (c) {
		case 'm':
			nblks = atoi(optarg);
			if (nblks <= 0 || nblks > 500) {
				(void) fprintf(stderr, gettext(
					"bad map block specification\n"));
				(void) fprintf(stderr, gettext(
					"using %d megabyte blocks\n"),
					nmapblocks);
			} else {
				nmapblocks = nblks;
			}
			break;
		default:
			badopt++;
			break;
		}
	}

	if (badopt || (optind >= argc))
		usage();

	if (chdir(argv[optind]) == -1) {
		perror("chdir");
		(void) fprintf(stderr,
		    gettext("%s: cannot cd to database root `%s'\n"),
			myname, argv[optind]);
		exit(1);
	}

#ifdef __STDC__
	(void) readconfig((char *)0, (void (*)(const char *, ...))0);
#else
	(void) readconfig((char *)0, (void (*)())0);
#endif

#ifndef DEBUG
	if ((n = fork()) == -1) {
		perror("dbserv/fork");
	} else if (n) {
		exit(0);
	}
	n = sysconf(_SC_OPEN_MAX);
	if (n == -1) {
		perror("dbserv/sysconf");
		n = 20;
	}
	yp_unbind(mydomain);
	for (i = 0; i < n; i++)
		(void) close(i);
	(void) open("/", 0);
	(void) dup2(0, 1);
	(void) dup2(0, 2);
	(void) setsid();
#endif
	(void) getopserver(opserver, sizeof (opserver));
	if (oper_init(opserver, myname, 0) != OPERMSG_CONNECTED) {
		(void) fprintf(stderr, gettext(
			"Warning: cannot initialize operator service\n"));
	}

#ifdef USG
	chldvec.sa_flags = SA_RESTART;
	(void) sigemptyset(&chldvec.sa_mask);
#else
	chldvec.sv_flags = 0;
	chldvec.sv_mask = 0;
#endif
	chldvec.sv_handler = sigchld;
	(void) sigvec(SIGCHLD, &chldvec, (struct sigvec *)0);

	startup();
	dbupdatetime = time((time_t *)0);

	/*
	 * we get SIGPIPE when using RPC to write to a connection
	 * which has gone away...
	 */
#ifdef USG
	pipevec.sa_flags = SA_RESTART;
	(void) sigemptyset(&pipevec.sa_mask);
#else
	pipevec.sv_flags = 0;
	pipevec.sv_mask = 0;
#endif
	pipevec.sv_handler = pipehandler;
	(void) sigvec(SIGPIPE, &pipevec, (struct sigvec *)NULL);
	setjmp(pipebuf);

	(void) pmap_unset(DBSERV, DBVERS);

#ifdef notdef
	/*
	 * XXX should use TI-RPC here
	 */
	{
		struct sockaddr_in sin;
		int	len = sizeof (struct sockaddr_in);
		int	s;

		s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (s < 0) {
			perror(gettext("cannot create socket"));
			exit(1);
		}
		sin.sin_addr.s_addr = htonl(INADDR_ANY);
		sin.sin_family = AF_INET;
		sin.sin_port = 0;		/* any port */
		if (bind(s, (struct sockaddr *)&sin, len) < 0) {
			perror(gettext("cannot bind socket"));
			exit(1);
		}
#ifdef DEBUG
		if (getsockname(s, (struct sockaddr *)&sin, &len) < 0) {
			perror("getsockname");
			exit(1);
		}
		fprintf(stderr,
		    gettext("%s.dumpdbd: socket is %d on port %u\n"),
			myname, s, (u_int)sin.sin_port);
#endif
		transp = svc_vc_create(s, 0, 0);
	}
#else
	transp = svctcp_create(RPC_ANYSOCK, 0, 0);
#endif
	if (transp == NULL) {
		(void) fprintf(stderr, gettext("cannot create tcp service.\n"));
		exit(1);
	}
	if (!svc_register(transp, DBSERV, DBVERS, dbserv_1, IPPROTO_TCP)) {
		(void) fprintf(stderr, gettext(
			"unable to register (DBSERV, DBVERS, tcp).\n"));
		exit(1);
	}

	svc_run();
	(void) fprintf(stderr, gettext("%s returned\n"), "svc_run");
	exit(1);
	/* NOTREACHED */
#ifdef lint
	return (0);
#endif
}

/*ARGSUSED*/
static void
sigchld(sig)
	int sig;
{
	int rc;

#ifdef USG
	while (rc = waitpid(-1, (int *)0, WNOHANG)) {
#else
	while (rc = wait3((union wait *)0, WNOHANG, (struct rusage *)0)) {
#endif
		if (rc == -1) {
			if (errno != ECHILD)
				perror("wait3");
			break;
		}
		/*
		 * the isupdatepid() function should protect us in case
		 * we get a SIGCHLD from another source, e.g. syslog()
		 * invoked in LOG_NOWAIT mode.
		 */
		if (isupdatepid(rc)) {
			if (--updatecnt < 0) {
				(void) fprintf(stderr, "updatecnt < 0!\n");
				updatecnt = 0;
			}
			dbupdatetime = time((time_t *)0);
			schedule_closefiles = 1;
		}
	}
}

static struct readdata *
#ifdef __STDC__
noreads(void)
#else
noreads()
#endif
{
	static struct readdata notnow = { DBREAD_SERVERDOWN };

	return (&notnow);
}

static int *
#ifdef __STDC__
nullproc(void)
#else
nullproc()
#endif
{
	static int zero;

	return (&zero);
}

bool_t
xdr_unavailable(xdrs)
	XDR *xdrs;
{
	u_int size;
	char *msg;

	msg = gettext("Database server unavailable\n");
	size = strlen(msg)+1;
	if (!xdr_bytes(xdrs, &msg, &size, size))
		return (FALSE);
	return (TRUE);
}

static char *
returnargs(p)
	char *p;
{
	return (p);
}

static void
dbserv_1(rqstp, transp)
	struct svc_req *rqstp;
	register SVCXPRT *transp;
{
	union {
		char *start_update_1_arg;
		process process_update_1_arg;
		struct blk_readargs blk_readargs;
		struct dnode_readargs dnode_readargs;
		struct header_readargs header_readargs;
		struct fsheader_readargs fsheader_readargs;
		struct tape_readargs tape_readargs;
		struct db_findargs db_findargs;
	} argument;
	char *result;
	bool_t (*xdr_argument)(), (*xdr_result)();
	char *(*local)();
#ifdef USG
	struct netbuf *caller;
	struct netconfig *network;
#else
	struct sockaddr_in *caller;
#endif
	static int quiesced;
	static int negone = -1;
	int dofork;

	struct authdes_cred *des_cred;
	uid_t uid;
	gid_t gid, gidlist[10];
	int gidlen;

	struct authunix_parms *unix_cred;

	/*
	 * no checks for credentials or privileged port on NULLPROC
	 * call (rpcinfo uses NULLPROC)
	 */
	if (rqstp->rq_proc == NULLPROC) {
		(void) svc_sendreply(transp, xdr_void, (char *)NULL);
		return;
	}
	dofork = 0;
	switch (rqstp->rq_cred.oa_flavor) {
	case AUTH_DES:
		/*LINTED [alignment ok]*/
		des_cred = (struct authdes_cred *)rqstp->rq_clntcred;
		if (!netname2user(des_cred->adc_fullname.name, &uid, &gid,
				&gidlen, gidlist)) {
			(void) fprintf(stderr, gettext("unknown user: %s\n"),
				des_cred->adc_fullname.name);
			svcerr_systemerr(transp);
			return;
		}
		break;
	case AUTH_UNIX:
		/*LINTED [alignment ok]*/
		unix_cred = (struct authunix_parms *)rqstp->rq_clntcred;
		uid = unix_cred->aup_uid;
		if (uid != 0) {
#ifdef DEBUG
			(void) fprintf(stderr, gettext(
				"unauthorized user: %d\n"), unix_cred->aup_uid);
#endif
			svcerr_weakauth(transp);
			return;
		}
		break;
	default:
		svcerr_weakauth(transp);
		return;
	}

	/*
	 * ensure that caller has a privileged port
	 */
#ifdef USG
	caller = svc_getrpccaller(transp);
	network = getnetconfigent("tcp");
	if (network == (struct netconfig *)0) {
		svcerr_systemerr(transp);
		return;
	}
	if (netdir_options(network, ND_CHECK_RESERVEDPORT,
	    0, (char *)caller) != 0) {
#else
	caller = svc_getcaller(transp);
	if (caller->sin_port > IPPORT_RESERVED) {
#endif
#ifdef DEBUG
		(void) fprintf(stderr,
			gettext("origin is non-priveleged port\n"));
#endif
#ifdef USG
		freenetconfigent(network);
#endif
		svcerr_weakauth(transp);
		return;
	}
#ifdef USG
	freenetconfigent(network);
#endif

	/*
	 * To be safe, after an "update" process finishes, we clear our
	 * cache of file descriptors.
	 */
	if (schedule_closefiles) {
		closefiles();
		schedule_closefiles = 0;
	}

	switch (rqstp->rq_proc) {
	case RESUME_OPERATION:
		if (quiesced) {
			/*
			 * perform startup() in case we left any update
			 * stuff hanging when we shut down
			 */
			quiesced = 0;
			startup();
		}
		(void) svc_sendreply(transp, xdr_void, (char *)NULL);
		closefiles();
		return;

	case QUIESCE_OPERATION:
		if (!quiesced) {
			int rc, status;

			while (rc = wait(&status)) {
				if (rc == -1) {
					if (errno != ECHILD)
						perror("wait");
					break;
				}
			}
			quiesced = 1;
		}
		(void) svc_sendreply(transp, xdr_void, (char *)NULL);
		closefiles();
		return;

	case START_UPDATE:
		if (quiesced) {
			(void) svc_sendreply(transp, xdr_int, (char *)&negone);
			return;
		}
		xdr_argument = xdr_wrapstring;
		xdr_result = xdr_int;
		local = (char *(*)())start_update_1;
		break;

	case BLAST_FILE:
		if (quiesced) {
			(void) svc_sendreply(transp, xdr_int, (char *)&negone);
			return;
		}
		xdr_argument = xdr_datafile;
		xdr_result = xdr_int;
		local = (char *(*)())nullproc;
		break;

	case PROCESS_UPDATE:
		if (quiesced) {
			(void) svc_sendreply(transp, xdr_int, (char *)&negone);
			return;
		}
		xdr_argument = xdr_process;
		xdr_result = xdr_int;
		local = (char *(*)()) process_update_1;
		break;

	case READ_DIR:
		xdr_argument = xdr_blkread;
		xdr_result = xdr_dirread;
		if (quiesced || updatecnt) {
			local = (char *(*)()) noreads;
		} else {
			local = (char *(*)()) read_dir_1;
		}
		break;

	case READ_INST:
		xdr_argument = xdr_blkread;
		xdr_result = xdr_instread;
		if (quiesced || updatecnt) {
			local = (char *(*)()) noreads;
		} else {
			local = (char *(*)()) read_inst_1;
		}
		break;

	case READ_DNODE:
		xdr_argument = xdr_dnodeargs;
		xdr_result = xdr_dnoderead;
		if (quiesced || updatecnt) {
			local = (char *(*)()) noreads;
		} else {
			local = (char *(*)()) read_dnode_1;
		}
		break;

	case READ_DNODEBLK:
		xdr_argument = xdr_dnodeargs;
		xdr_result = xdr_dnodeblkread;
		if (quiesced || updatecnt) {
			local = (char *(*)()) noreads;
		} else {
			local = (char *(*)()) read_dnodeblk_1;
		}
		break;

	case DELETE_BYTAPE:
		if (quiesced) {
			(void) svc_sendreply(transp, xdr_int, (char *)&negone);
			return;
		}
		dofork = 1;
		xdr_argument = xdr_tapelabel;
		xdr_result = xdr_int;
		local = (char *(*)()) delete_tape_1;
		break;

	case READ_HEADER:
		xdr_argument = xdr_headerargs;
		xdr_result = xdr_dheaderread;
		if (quiesced || updatecnt) {
			local = (char *(*)()) noreads;
		} else {
			local = (char *(*)()) read_header_1;
		}
		break;

	case READ_FULLHEADER:
		xdr_argument = xdr_headerargs;
		xdr_result = xdr_fullheaderread;
		if (quiesced || updatecnt) {
			local = (char *(*)()) noreads;
		} else {
			local = (char *(*)()) read_fullheader_1;
		}
		break;

	case READ_TAPE:
		xdr_argument = xdr_tapeargs;
		xdr_result = xdr_acttaperead;
		if (quiesced || updatecnt) {
			local = (char *(*)()) noreads;
		} else {
			local = (char *(*)()) read_tape_1;
		}
		break;

	case READ_FSHEADER:
		xdr_argument = xdr_fsheaderargs;
		xdr_result = xdr_dheaderread;
		if (quiesced || updatecnt) {
			local = (char *(*)()) noreads;
		} else {
			local = (char *(*)()) read_fsheader_1;
		}
		break;

	case READ_FULLFSHEADER:
		xdr_argument = xdr_fsheaderargs;
		xdr_result = xdr_fullheaderread;
		if (quiesced || updatecnt) {
			local = (char *(*)()) noreads;
		} else {
			local = (char *(*)()) read_fullfsheader_1;
		}
		break;

	case READ_DUMPS:
		xdr_argument = xdr_fsheaderargs;
		xdr_result = xdr_headerlist;
		if (quiesced || updatecnt) {
			local = (char *(*)()) noreads;
		} else {
			dofork = 1;
			local = (char *(*)()) read_dumps_1;
		}
		break;

	case DB_FIND:
		xdr_argument = xdr_dbfindargs;
		if (quiesced || updatecnt) {
			xdr_result = xdr_unavailable;
		} else {
			dofork = 1;
			xdr_result = xdr_fastfind;
		}
		local = (char *(*)()) returnargs;
		break;

	case READ_LINKVAL:
		xdr_argument = xdr_dnodeargs;
		xdr_result = xdr_linkval;
		if (quiesced || updatecnt) {
			local = (char *(*)()) noreads;
		} else {
			local = (char *(*)()) read_linkval_1;
		}
		break;

	case DB_TAPELIST:
		xdr_argument = xdr_tapelistargs;
		if (quiesced || updatecnt) {
			xdr_result = xdr_unavailable;
		} else {
			dofork = 1;
			xdr_result = xdr_listem;
		}
		local = (char *(*)()) returnargs;
		break;

	case CHECK_MNTPT:
		xdr_argument = xdr_fsheaderargs;
		xdr_result = xdr_mntptlist;
		if (quiesced || updatecnt) {
			local = (char *(*)()) noreads;
		} else {
			dofork = 1;
			local = (char *(*)()) check_mntpt_1;
		}
		break;

	case DB_DBINFO:
		dofork = 1;
		xdr_argument = xdr_void;
		xdr_result = xdr_dbinfo;
		local = (char *(*)()) returnargs;
		break;

	default:
		svcerr_noproc(transp);
		return;
	}
	(void) bzero((char *)&argument, sizeof (argument));
	if (!svc_getargs(transp, xdr_argument, (caddr_t)&argument)) {
		svcerr_decode(transp);
		return;
	}

#ifndef DONTFORK
	if (dofork) {
		int newpid;
		struct sigvec exitvec;

		/*
		 * run this RPC in a sub-process...
		 */
		newpid = fork();
		if (newpid == -1) {
			dofork = 0;
			perror("fork");
		} else if (newpid != 0) {
			svc_freeargs(transp, xdr_argument, (caddr_t)&argument);
			return;
		} else {
			yp_unbind(mydomain);
			closefiles();
#ifdef USG
			(void) sigemptyset(&exitvec.sa_mask);
			exitvec.sa_flags = SA_RESTART;
#else
			exitvec.sv_mask = 0;
			exitvec.sv_flags = 0;
#endif
			exitvec.sv_handler = exitonsig;
			(void) sigvec(SIGPIPE, &exitvec, (struct sigvec *)NULL);
			(void) oper_init(opserver, myname, 0);
		}
	}
#endif

	result = (*local)(&argument, rqstp);
	if (result != NULL && !svc_sendreply(transp, xdr_result, result)) {
		svcerr_systemerr(transp);
	}
	if (!svc_freeargs(transp, xdr_argument, (caddr_t)&argument)) {
		(void) fprintf(stderr, gettext("unable to free arguments"));
		exit(1);
	}
#ifndef DONTFORK
	if (dofork) {
		oper_end();
		exit(0);
	}
#endif
}
