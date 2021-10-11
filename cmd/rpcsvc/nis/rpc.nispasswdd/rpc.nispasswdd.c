/*
 *	rpc.nispasswdd.c
 *	NIS+ password update daemon.
 *
 *	Copyright (c) 1994 Sun Microsystems, Inc.
 *	All Rights Reserved.
 */

#pragma ident	"@(#)rpc.nispasswdd.c	1.5	94/10/14 SMI"

#include <stdio.h>
#include <stdlib.h>	/* getenv, exit */
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <memory.h>
#include <syslog.h>
#include <sys/resource.h>	/* rlimit */
#include <stropts.h>
#include <netdir.h>
#include <synch.h>
#include <rpc/rpc.h>
#include <rpcsvc/nis.h>
#include <rpcsvc/yppasswd.h>
#include <rpcsvc/nispasswd.h>


/* States a server can be in wrt request */
#define	_IDLE 0
#define	_SERVED 1
#define	_SERVING 2

#define	_RPCSVC_CLOSEDOWN 120
static int _rpcsvcstate = _IDLE;	/* set when a request is serviced */
mutex_t _svcstate_lock;		/* mutex lock for variable _rpcsvcstate */

int	verbose = 0;
bool_t	debug = FALSE;
/*
 * generate keys if none exist or if admin. is changing another
 * users password.
 */
bool_t	generatekeys = FALSE;
u_long	cache_time = 1800;	/* cache failed attempts for 30 mins */
int	max_attempts = 3;	/* max # of failed attempts allowed */
NIS_HASH_TABLE	upd_list;	/* cache of updates */
rwlock_t _updcache_lock;	/* read/write lock for cache */

static void __msgout();
static int __svc_priv_port_create();
static void nispasswd_prog();
static void yppasswd_prog();

extern int yppasswdproc_update_1_svc();
extern bool_t __npd_am_master();
extern nis_server *__nis_host2nis_server();


main(argc, argv)
int argc;
char **argv;
{
	pid_t	pid;
	int	i;
	int	c;
	int	mins;
	int	size;
	struct rlimit rl;
	int	status;
	nis_tag		tags[2], *tagres;
	nis_server	*srv;

	mutex_init(&_svcstate_lock, USYNC_THREAD, NULL);
	(void) memset((char *)&upd_list, 0, sizeof (upd_list));
	rwlock_init(&_updcache_lock, USYNC_THREAD, NULL);

	(void) chdir("/var/nis");	/* drop core here */
	while ((c = getopt(argc, argv, "a:c:gDv")) != -1) {
	switch (c) {
		case 'a':
			max_attempts = atoi(optarg);
			if (max_attempts < 0) {
				fprintf(stderr,
				"%s: invalid number of maximum attempts\n",
					argv[0]);
				exit(1);
			}
			break;
		case 'c':
			mins = atoi(optarg);
			if (mins <= 0) {
				fprintf(stderr,
					"%s: invalid cache time\n", argv[0]);
				exit(1);
			}
			cache_time = (u_long) mins * 60;
			break;
		case 'D':	/* debug mode */
			debug = TRUE;
			break;
		case 'g':	/* generate new keys if none exist */
			generatekeys = TRUE;
			break;
		case 'v':	/* verbose mode */
			verbose++;
			break;
		case '?':
			fprintf(stderr,
		"Usage: %s [-a attempts] [-c minutes] [-D] [-g] [-v]\n",
				argv[0]);
			exit(1);
		}
	}

	/*
	 * this check should be removed if 'root' is not the
	 * owner of the table or if 'root' is not a member of
	 * the nis+ admins. group.
	 * of course: you also don't want someone to spoof the
	 * the daemon.
	 */
	if (geteuid() != (uid_t)0) {
		__msgout("must be superuser to run %s", argv[0]);
		exit(1);
	}

	if (debug == FALSE) {
		pid = fork();
		if (pid < 0) {
			perror("cannot fork");
			exit(1);
		}
		if (pid)
			exit(0);
		rl.rlim_max = 0;
		getrlimit(RLIMIT_NOFILE, &rl);
		if ((size = rl.rlim_max) == 0)
			exit(1);
		for (i = 0; i < size; i++)
			(void) close(i);
		i = open("/dev/console", 2);
		(void) dup2(i, 1);
		(void) dup2(i, 2);
		(void) setsid();
	}
	openlog("rpc.nispasswdd", LOG_PID, LOG_DAEMON);

	/*
	 * should I be running ?
	 * Get the list of directories the local NIS+ server serves and
	 * check if it is the master server of any 'org_dir' listed.
	 */
	srv = __nis_host2nis_server(NULL, 0, &status);
	if (srv == NULL) {
		if (debug == TRUE)
			(void) fprintf(stderr,
			"no host/address information for local host, %d\n",
			status);
		else
			syslog(LOG_ERR,
			"no host/address information for local host, %d",
			status);
		__msgout("exiting ...");
		exit(1);
	}
	tags[0].tag_type = TAG_DIRLIST;
	tags[0].tag_val = "";
	tags[1].tag_type = TAG_NISCOMPAT;
	tags[1].tag_val = "";

	status = nis_stats(srv, tags, 2, &tagres);
	if (status != NIS_SUCCESS) {
		if (verbose)
			nis_perror(status, "rpc.nispasswdd");
		else
			nis_lerror(status, "rpc.nispasswdd");
		__msgout(" ... exiting ...");
		exit(1);
	}
	if ((strcmp(tagres[0].tag_val, "<Unknown Statistics>") == 0) ||
		(strcmp(tagres[1].tag_val, "<Unknown Statistics>") == 0)) {

		/* old server */
	__msgout("NIS+ server does not support the new statistics tags");
		exit(1);
	}

	if (__npd_am_master(nis_local_host(), tagres[0].tag_val) == FALSE) {
		__msgout("Local NIS+ server is not a master server");
		__msgout(" ... exiting ...");
		exit(1);
	}

	/* check if server is running in NIS compat mode */
	if (strcasecmp(tagres[1].tag_val, "ON") == 0) {
		if (rpcb_unset(YPPASSWDPROG, YPPASSWDVERS, 0) == FALSE) {
		__msgout("unable to de-register (YPPASSWDPROG, YPPASSWDVERS)");
			__msgout(" ... exiting ...");
			exit(1);
		}
		if (__svc_priv_port_create() == FALSE) {
		__msgout("unable to create (YPPASSWDPROG, YPPASSWDVERS)");
			/* continue */
		}
	}
	if (rpcb_unset(NISPASSWD_PROG, NISPASSWD_VERS, 0) == FALSE) {
	__msgout("unable to de-register (NISPASSWD_PROG, NISPASSWD_VERS)");
		__msgout(" ... exiting ...");
		exit(1);
	}
	if (svc_create(nispasswd_prog, NISPASSWD_PROG, NISPASSWD_VERS,
		"circuit_v") == 0) {
		__msgout("unable to create (NISPASSWD_PROG, NISPASSWD_VERS)");
		__msgout(" ... exiting ...");
		exit(1);
	}

	if (verbose == TRUE)
		__msgout("starting rpc.nispasswdd ...");
	svc_run();
	__msgout("svc_run returned");
	exit(1);

	/* NOTREACHED */
}

static void
__msgout(msg)
char	*msg;
{
	if (debug == TRUE)
		(void) fprintf(stderr, "%s\n", msg);
	else
		syslog(LOG_ERR, msg);
}

static bool_t
__svc_priv_port_create()
{
	struct netconfig *nconf;
	void		*handlep;
	int		fd;
	struct t_info	tinfo;
	SVCXPRT		*transp;

	if ((handlep = setnetconfig()) == (void *) NULL) {
		(void) nc_perror("cannot get any transport information");
		return (FALSE);
	}
	while (nconf = getnetconfig(handlep)) {
		if ((nconf->nc_semantics == NC_TPI_CLTS) &&
			(strcmp(nconf->nc_protofmly, NC_INET) == 0) &&
			(strcmp(nconf->nc_proto, NC_UDP) == 0))
			break;
	}
	if (nconf == (struct netconfig *) NULL) {
		(void) endnetconfig(handlep);
		return (FALSE);
	}
	fd = t_open(nconf->nc_device, O_RDWR, &tinfo);
	if (fd == -1) {
		__msgout("unable to open connection for NIS requests");
		return (FALSE);
	}

	if (netdir_options(nconf, ND_SET_RESERVEDPORT, fd, (void *) NULL)
		== -1) {
		__msgout("unable to get a reserved port");
		netdir_perror("");
		return (FALSE);
	}
	if ((transp = svc_tli_create(fd, nconf, NULL, 0, 0))
			== (SVCXPRT *) NULL) {
		__msgout("unable to create (YPPASSWDPROG, YPPASSWDVERS)");
		return (FALSE);
	}

	if (svc_reg(transp, YPPASSWDPROG, YPPASSWDVERS, yppasswd_prog,
			nconf) == 0) {
		__msgout("unable to register (YPPASSWDPROG, YPPASSWDVERS)");
		(void) svc_destroy(transp);
		return (FALSE);
	}
	return (TRUE);
}

static void
nispasswd_prog(rqstp, transp)
struct svc_req	*rqstp;
register SVCXPRT *transp;
{
	union {
		npd_request nispasswd_authenticate_1_arg;
		npd_update nispasswd_update_1_arg;
	} argument;
	union {
		nispasswd_authresult nispasswd_authenticate_1_res;
		nispasswd_updresult nispasswd_update_1_res;
	} result;
	bool_t retval;
	xdrproc_t xdr_argument, xdr_result;
	bool_t (*local)(char *, void *, struct svc_req *);

	mutex_lock(&_svcstate_lock);
	_rpcsvcstate = _SERVING;
	mutex_unlock(&_svcstate_lock);

	switch (rqstp->rq_proc) {
	case NULLPROC:
		__msgout("received NIS+ null proc call");
		(void) svc_sendreply(transp,
			(xdrproc_t) xdr_void, (char *)NULL);
		mutex_lock(&_svcstate_lock);
		_rpcsvcstate = _SERVED;
		mutex_unlock(&_svcstate_lock);
		return;

	case NISPASSWD_AUTHENTICATE:
		xdr_argument = (xdrproc_t) xdr_npd_request;
		xdr_result = (xdrproc_t) xdr_nispasswd_authresult;
		local = (bool_t (*) (char *, void *, struct svc_req *))
				nispasswd_authenticate_1_svc;
		break;

	case NISPASSWD_UPDATE:
		xdr_argument = (xdrproc_t) xdr_npd_update;
		xdr_result = (xdrproc_t) xdr_nispasswd_updresult;
		local = (bool_t (*) (char *, void *, struct svc_req *))
				nispasswd_update_1_svc;
		break;

	default:
		svcerr_noproc(transp);
		mutex_lock(&_svcstate_lock);
		_rpcsvcstate = _SERVED;
		mutex_unlock(&_svcstate_lock);
		return;
	}
	(void) memset((char *)&argument, 0, sizeof (argument));
	if (svc_getargs(transp, xdr_argument, (caddr_t) &argument) == 0) {
		svcerr_decode(transp);
		mutex_lock(&_svcstate_lock);
		_rpcsvcstate = _SERVED;
		mutex_unlock(&_svcstate_lock);
		return;
	}
	(void) memset((char *)&result, 0, sizeof (result));
	retval = (bool_t) (*local)((char *)&argument, (void *)&result, rqstp);
	if (retval == TRUE && (svc_sendreply(transp, xdr_result,
						(char *)&result) == 0)) {
		svcerr_systemerr(transp);
	}
	if (svc_freeargs(transp, xdr_argument, (caddr_t) &argument) == 0) {
		__msgout("unable to free arguments");
		exit(1);
	}
	if (nispasswd_prog_1_freeresult(transp, xdr_result,
				(caddr_t) &result) == 0)
		__msgout("unable to free results");

	mutex_lock(&_svcstate_lock);
	_rpcsvcstate = _SERVED;
	mutex_unlock(&_svcstate_lock);
	/* NOTREACHED */
}

static void
yppasswd_prog(rqstp, transp)
struct svc_req	*rqstp;
register SVCXPRT *transp;
{
	union {
		struct yppasswd yppasswdproc_update_1_arg;
	} argument;
	union {
		int yppasswdproc_update_1_res;
	} result;
	bool_t retval;
	xdrproc_t xdr_argument, xdr_result;
	bool_t (*local)(char *, void *, struct svc_req *);
#ifdef SHD_WE_ALWAYS
	struct netconfig	*nconf;
#endif

	mutex_lock(&_svcstate_lock);
	_rpcsvcstate = _SERVING;
	mutex_unlock(&_svcstate_lock);

	switch (rqstp->rq_proc) {
	case NULLPROC:
		__msgout("received NIS null proc call");
		(void) svc_sendreply(transp,
			(xdrproc_t) xdr_void, (char *)NULL);
		mutex_lock(&_svcstate_lock);
		_rpcsvcstate = _SERVED;
		mutex_unlock(&_svcstate_lock);
		return;

	case YPPASSWDPROC_UPDATE:
		xdr_argument = (xdrproc_t) xdr_yppasswd;
		xdr_result = (xdrproc_t) xdr_int;
		local = (bool_t (*) (char *, void *, struct svc_req *))
				yppasswdproc_update_1_svc;
		break;

	default:
		svcerr_noproc(transp);
		mutex_lock(&_svcstate_lock);
		_rpcsvcstate = _SERVED;
		mutex_unlock(&_svcstate_lock);
		return;
	}
#ifdef SHD_WE_ALWAYS
	/*
	 * update request received, lets just check the port
	 */
	nconf = (struct netconfig *) getnetconfigent(transp->xp_netid);
	if (nconf == (struct netconfig *) NULL) {
		(void) nc_perror("could not get transport information");
		svcerr_systemerr(transp);
		mutex_lock(&_svcstate_lock);
		_rpcsvcstate = _SERVED;
		mutex_unlock(&_svcstate_lock);
		return;
	}

	if ((strcmp(nconf->nc_protofmly, NC_INET) != 0) ||
		(strcmp(nconf->nc_proto, NC_UDP) != 0) ||
		(netdir_options(nconf, ND_CHECK_RESERVEDPORT, transp->xp_fd,
			(void *) &(transp->xp_rtaddr)) != 0)) {

		if (nconf)
			(void) freenetconfigent(nconf);
		svcerr_weakauth(transp);
		mutex_lock(&_svcstate_lock);
		_rpcsvcstate = _SERVED;
		mutex_unlock(&_svcstate_lock);
		return;
	}
	if (nconf)
		(void) freenetconfigent(nconf);
#endif
	(void) memset((char *)&argument, 0, sizeof (argument));
	if (!svc_getargs(transp, xdr_argument, (caddr_t) &argument)) {
		svcerr_decode(transp);
		mutex_lock(&_svcstate_lock);
		_rpcsvcstate = _SERVED;
		mutex_unlock(&_svcstate_lock);
		return;
	}
	(void) memset((char *)&result, 0, sizeof (result));
	retval = (bool_t) (*local)((char *)&argument, (void *)&result, rqstp);
	if (retval == TRUE && !svc_sendreply(transp, xdr_result,
						(char *)&result)) {
		svcerr_systemerr(transp);
	}
	if (!svc_freeargs(transp, xdr_argument, (caddr_t) &argument)) {
		__msgout("unable to free arguments");
		exit(1);
	}
	mutex_lock(&_svcstate_lock);
	_rpcsvcstate = _SERVED;
	mutex_unlock(&_svcstate_lock);
}
