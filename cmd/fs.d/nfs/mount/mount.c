/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)mount.c	1.39	95/08/28 SMI"	/* SVr4.0 1.18	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *	PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 *	Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 *  (c) 1986,1987,1988.1989  Sun Microsystems, Inc
 *  (c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *	All rights reserved.
 *
 */
/*
 * nfs mount
 */

#define	NFSCLIENT
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <varargs.h>
#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/param.h>
#include <rpc/rpc.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <netdb.h>
#include <sys/mount.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <nfs/nfs.h>
#include <nfs/mount.h>
#include <rpcsvc/mount.h>
#include <sys/pathconf.h>
#include <netdir.h>
#include <netconfig.h>
#include <rpcsvc/nlm_prot.h>
#include <syslog.h>
#include <fslib.h>
#include <sys/utsname.h>

#define	RET_OK		0
#define	RET_RETRY	32
#define	RET_ERR		33

#define	MNT_PREF_LISTLEN	2

#define	BIGRETRY	10000

/* maximum length of RPC header for NFS messages */
#define	NFS_RPC_HDR	432

extern int errno;
extern int __clnt_bindresvport();

int remote_lock();
int retry();
int set_args();
int get_fh();
int getaddr_nfs();
int make_secure();
int mount_nfs();
int get_knconf();
int getaddr_nfs();
void
#ifdef __STDC__
pr_err(const char *fmt, ...);
#else
pr_err(char *fmt, va_dcl);
#endif
void usage();
struct netbuf *get_addr(), *get_the_addr();
void fix_remount();

char *myname;
char typename[64];
struct t_info	tinfo;

int bg;
int posix = 0;
int retries = BIGRETRY;
u_short nfs_port = 0;
char *nfs_proto = NULL;

int mflg = 0;
int Oflg = 0;	/* Overlay mounts */

char *fstype = MNTTYPE_NFS;
/*
 * These two variables control the NFS version number to be used.
 *
 * nfsvers defaults to 0 which means to use the highest number that
 * both the client and the server support.  It can also be set to
 * a particular value, either 2 or 3, to indicate the version
 * number of choice.  If the server (or the client) do not support
 * the version indicated, then the mount attempt will be failed.
 *
 * nfsvers_to_use is the actual version number found to use.  It
 * is determined in get_fh by pinging the various versions of the
 * NFS service on the server to see which responds positively.
 */
u_long nfsvers = 0;
u_long nfsvers_to_use = 0;

main(argc, argv)
	int argc;
	char **argv;
{
	struct mnttab mnt, *mntp;
	extern char *optarg;
	extern int optind;
	char optbuf[256];
	int ro = 0;
	int r, c;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN	"SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	myname = strrchr(argv[0], '/');
	myname = myname ? myname+1 : argv[0];
	(void) sprintf(typename, "%s %s", MNTTYPE_NFS, myname);
	argv[0] = typename;

	mnt.mnt_mntopts = optbuf;
	(void) strcpy(optbuf, "rw");

	/*
	 * Set options
	 */
	while ((c = getopt(argc, argv, "ro:mO")) != EOF) {
		switch (c) {
		case 'r':
			ro++;
			break;
		case 'o':
			strcpy(mnt.mnt_mntopts, optarg);
#ifdef LATER					/* XXX */
			if (strstr(optarg, MNTOPT_REMOUNT)) {
				/*
				 * If remount is specified, only rw is allowed.
				 */
				if ((strcmp(optarg, MNTOPT_REMOUNT) != 0) &&
				    (strcmp(optarg, "remount,rw") != 0) &&
				    (strcmp(optarg, "rw,remount") != 0)) {
					pr_err(gettext("Invalid options\n"));
					exit(RET_ERR);
				}
			}
#endif /* LATER */				/* XXX */
			break;
		case 'm':
			mflg++;
			break;
		case 'O':
			Oflg++;
			break;
		default:
			usage();
			exit(RET_ERR);
		}
	}
	if (argc - optind != 2) {
		usage();
		exit(RET_ERR);
	}

	mnt.mnt_special = argv[optind];
	mnt.mnt_mountp  = argv[optind+1];
	mntp = &mnt;

	if (geteuid() != 0) {
		pr_err(gettext("not super user\n"));
		exit(RET_ERR);
	}

	r = mount_nfs(mntp, ro);
	if (r == RET_RETRY && retries)
		r = retry(mntp, ro);

	/*
	 * exit(r);
	 */
	return (r);
}

void
#ifdef __STDC__
pr_err(const char *fmt, ...)
#else
pr_err(fmt, va_alist)
char *fmt;
va_dcl
#endif
{
	va_list ap;

	va_start(ap);
	(void) fprintf(stderr, "%s: ", typename);
	(void) vfprintf(stderr, fmt, ap);
	(void) fflush(stderr);
	va_end(ap);
}

void
usage()
{
	(void) fprintf(stderr,
	    gettext("Usage: nfs mount [-r] [-o opts] server:path dir\n"));
	exit(RET_ERR);
}

int
mount_nfs(mntp, ro)
	struct mnttab *mntp;
	int ro;
{
	struct nfs_args nfs_args;
	char *p, *fsname, *fshost, *fspath;
	struct netconfig *nconf = NULL;
	int mntflags = 0;
	int r;

	memset(&nfs_args, 0, sizeof (struct nfs_args));

	mntp->mnt_fstype = MNTTYPE_NFS;

	/*
	 * split server:/dir into two strings: fshost & fspath
	 */
	fsname = (char *) strdup(mntp->mnt_special);
	if (fsname == NULL) {
		pr_err(gettext("no memory"));
		return (RET_ERR);
	}
	p = (char *) strchr(fsname, ':');
	if (p == NULL) {
		pr_err(gettext("nfs file system; use host:path\n"));
		return (RET_ERR);
	}
	*p++ = '\0';
	fshost = fsname;
	fspath = p;
	if (!strlen(fshost)) {
		pr_err(gettext("hostname not specified; use host:path\n"));
		return (RET_ERR);
	}

	if (r = set_args(&mntflags, &nfs_args, fshost, mntp))
		return (r);

	if (ro) {
		mntflags |= MS_RDONLY;
		if (p = strstr(mntp->mnt_mntopts, "rw"))	/* "rw"->"ro" */
			if (*(p+2) == ',' || *(p+2) == '\0')
				*(p+1) = 'o';
	}

	if (Oflg)
		mntflags |= MS_OVERLAY;

	if (r = get_fh(&nfs_args, fshost, fspath))
		return (r);

	/* decide whether to use remote host's lockd or do local locking */
	if ((nfs_args.flags & NFSMNT_LLOCK) == 0 &&
	    strcmp(fstype, MNTTYPE_NFS) == 0) {
		if (!remote_lock(fshost, nfs_args.fh)) {
printf(
"WARNING:No network locking on %s:%s: contact admin to install server change\n",
fshost, fspath);
			nfs_args.flags |= NFSMNT_LLOCK;
		}
	}

	if (r = getaddr_nfs(&nfs_args, fshost, &nconf))
		return (r);

	if (make_secure(&nfs_args, fshost, &nconf) < 0)
		return (RET_ERR);

	if (mount("", mntp->mnt_mountp, mntflags | MS_DATA, fstype,
		&nfs_args, sizeof (nfs_args)) < 0) {
		pr_err(gettext("mount: %s: %s\n"),
			mntp->mnt_mountp, strerror(errno));
		return (RET_ERR);
	}
	if (!mflg) {
		if (mntflags & MS_REMOUNT)
			fix_remount(mntp->mnt_mountp);
		else
			if (fsaddtomtab(mntp))
				exit(RET_ERR);
	}
	free(fsname);

	return (RET_OK);
}

char *optlist[] = {
#define	OPT_RO		0
	MNTOPT_RO,
#define	OPT_RW		1
	MNTOPT_RW,
#define	OPT_QUOTA	2
	MNTOPT_QUOTA,
#define	OPT_NOQUOTA	3
	MNTOPT_NOQUOTA,
#define	OPT_SOFT	4
	MNTOPT_SOFT,
#define	OPT_HARD	5
	MNTOPT_HARD,
#define	OPT_SUID	6
	MNTOPT_SUID,
#define	OPT_NOSUID	7
	MNTOPT_NOSUID,
#define	OPT_GRPID	8
	MNTOPT_GRPID,
#define	OPT_REMOUNT	9
	MNTOPT_REMOUNT,
#define	OPT_NOSUB	10
	MNTOPT_NOSUB,
#define	OPT_INTR	11
	MNTOPT_INTR,
#define	OPT_NOINTR	12
	MNTOPT_NOINTR,
#define	OPT_PORT	13
	MNTOPT_PORT,
#define	OPT_SECURE	14
	MNTOPT_SECURE,
#define	OPT_RSIZE	15
	MNTOPT_RSIZE,
#define	OPT_WSIZE	16
	MNTOPT_WSIZE,
#define	OPT_TIMEO	17
	MNTOPT_TIMEO,
#define	OPT_RETRANS	18
	MNTOPT_RETRANS,
#define	OPT_ACTIMEO	19
	MNTOPT_ACTIMEO,
#define	OPT_ACREGMIN	20
	MNTOPT_ACREGMIN,
#define	OPT_ACREGMAX	21
	MNTOPT_ACREGMAX,
#define	OPT_ACDIRMIN	22
	MNTOPT_ACDIRMIN,
#define	OPT_ACDIRMAX	23
	MNTOPT_ACDIRMAX,
#define	OPT_BG		24
	MNTOPT_BG,
#define	OPT_FG		25
	MNTOPT_FG,
#define	OPT_RETRY	26
	MNTOPT_RETRY,
#define	OPT_NOAC	27
	MNTOPT_NOAC,
#define	OPT_KERB	28
	MNTOPT_KERB,
#define	OPT_NOCTO	29
	MNTOPT_NOCTO,
#define	OPT_LLOCK	30
	MNTOPT_LLOCK,
#define	OPT_POSIX	31
	MNTOPT_POSIX,
#define	OPT_VERS	32
	MNTOPT_VERS,
#define	OPT_PROTO	33
	MNTOPT_PROTO,
	NULL
};

#define	bad(val) (val == NULL || !isdigit(*val))

/*
 * This function is added to detect compatibility problem with SunOS4.x.
 * The compatibility problem exists when fshost cannot decode the request
 * arguments for NLM_GRANTED procedure.
 * Only in this case  we use local locking.
 * In any other case we use fshost's lockd for remote file locking.
 */
int
remote_lock(fshost, fh)
	char *fshost;
	caddr_t fh;
{
	static nlm_testargs rlm_args;
	static nlm_res rlm_res;
	struct timeval timeout = { 5, 0};
	CLIENT *cl;
	enum clnt_stat rpc_stat;
	struct utsname myid;

	/*
	 * Assign the hostname and the file handle for the
	 * NLM_GRANTED request below.  If for some reason the uname call fails,
	 * list the server as the caller so that caller_name has some
	 * reasonable value.
	 */
	if (uname(&myid) == -1)  {
		rlm_args.alock.caller_name = fshost;
	} else {
		rlm_args.alock.caller_name = myid.nodename;
	}
	rlm_args.alock.fh.n_len = sizeof (fhandle_t);
	rlm_args.alock.fh.n_bytes = fh;

	if ((cl = clnt_create(fshost, NLM_PROG, NLM_VERS,
		"datagram_v")) == NULL)
		return (1);

	rpc_stat = clnt_call(cl, NLM_GRANTED,
			xdr_nlm_testargs, (caddr_t)&rlm_args,
			xdr_nlm_res, (caddr_t)&rlm_res, timeout);
	clnt_destroy(cl);

	if (rpc_stat == RPC_CANTDECODEARGS)
		return (0);
	return (1);
}

int
set_args(mntflags, args, fshost, mnt)
	int *mntflags;
	struct nfs_args *args;
	char *fshost;
	struct mnttab *mnt;
{
	char *saveopt, *optstr, *opts, *val;

	args->flags =  NFSMNT_INT;	/* default is "intr" */
	args->flags |= NFSMNT_HOSTNAME;
	args->hostname = fshost;
	optstr = opts = strdup(mnt->mnt_mntopts);
	if (opts == NULL) {
		pr_err(gettext("no memory"));
		return (RET_ERR);
	}
	while (*opts) {
		saveopt = opts;
		switch (getsubopt(&opts, optlist, &val)) {
		case OPT_RO:
			*mntflags |= MS_RDONLY;
			break;
		case OPT_RW:
			*mntflags &= ~(MS_RDONLY);
			break;
		case OPT_QUOTA:
		case OPT_NOQUOTA:
			break;
		case OPT_SOFT:
			args->flags |= NFSMNT_SOFT;
			break;
		case OPT_HARD:
			args->flags &= ~(NFSMNT_SOFT);
			break;
		case OPT_SUID:
			*mntflags &= ~(MS_NOSUID);
			break;
		case OPT_NOSUID:
			*mntflags |= MS_NOSUID;
			break;
		case OPT_GRPID:
			args->flags |= NFSMNT_GRPID;
			break;
		case OPT_REMOUNT:
			*mntflags |= MS_REMOUNT;
			break;
		case OPT_INTR:
			args->flags |= NFSMNT_INT;
			break;
		case OPT_NOINTR:
			args->flags &= ~(NFSMNT_INT);
			break;
		case OPT_NOAC:
			args->flags |= NFSMNT_NOAC;
			break;
		case OPT_PORT:
			if (bad(val))
				goto badopt;
			nfs_port = htons(atoi(val));
			break;

		case OPT_SECURE:
			args->flags |= NFSMNT_SECURE;
			break;

		case OPT_KERB:
			args->flags |= NFSMNT_KERBEROS;
			break;

		case OPT_NOCTO:
			args->flags |= NFSMNT_NOCTO;
			break;

		case OPT_RSIZE:
			args->flags |= NFSMNT_RSIZE;
			if (bad(val))
				goto badopt;
			args->rsize = atoi(val);
			break;
		case OPT_WSIZE:
			args->flags |= NFSMNT_WSIZE;
			if (bad(val))
				goto badopt;
			args->wsize = atoi(val);
			break;
		case OPT_TIMEO:
			args->flags |= NFSMNT_TIMEO;
			if (bad(val))
				goto badopt;
			args->timeo = atoi(val);
			break;
		case OPT_RETRANS:
			args->flags |= NFSMNT_RETRANS;
			if (bad(val))
				goto badopt;
			args->retrans = atoi(val);
			break;
		case OPT_ACTIMEO:
			args->flags |= NFSMNT_ACDIRMAX;
			args->flags |= NFSMNT_ACREGMAX;
			args->flags |= NFSMNT_ACDIRMIN;
			args->flags |= NFSMNT_ACREGMIN;
			if (bad(val))
				goto badopt;
			args->acdirmin = args->acregmin = args->acdirmax
				= args->acregmax = atoi(val);
			break;
		case OPT_ACREGMIN:
			args->flags |= NFSMNT_ACREGMIN;
			if (bad(val))
				goto badopt;
			args->acregmin = atoi(val);
			break;
		case OPT_ACREGMAX:
			args->flags |= NFSMNT_ACREGMAX;
			if (bad(val))
				goto badopt;
			args->acregmax = atoi(val);
			break;
		case OPT_ACDIRMIN:
			args->flags |= NFSMNT_ACDIRMIN;
			if (bad(val))
				goto badopt;
			args->acdirmin = atoi(val);
			break;
		case OPT_ACDIRMAX:
			args->flags |= NFSMNT_ACDIRMAX;
			if (bad(val))
				goto badopt;
			args->acdirmax = atoi(val);
			break;
		case OPT_BG:
			bg++;
			break;
		case OPT_FG:
			bg = 0;
			break;
		case OPT_RETRY:
			if (bad(val))
				goto badopt;
			retries = atoi(val);
			break;
		case OPT_LLOCK:
			args->flags |= NFSMNT_LLOCK;
			break;
		case OPT_POSIX:
			posix = 1;
			break;
		case OPT_VERS:
			if (bad(val))
				goto badopt;
			nfsvers = (u_long)atoi(val);
			break;
		case OPT_PROTO:
			nfs_proto = (char *)malloc(strlen(val)+1);
			strcpy(nfs_proto, val);
			break;

		default:
			goto badopt;
		}
	}
	free(optstr);

	/* ensure that only one secure mode is requested */
	if ((args->flags & NFSMNT_SECURE) && (args->flags & NFSMNT_KERBEROS)) {
		pr_err(gettext("\"%s\" and \"%s\" options conflict\n"),
			MNTOPT_SECURE, MNTOPT_KERB);
		return (RET_ERR);
	}
	return (RET_OK);

badopt:
	pr_err(gettext("invalid option: \"%s\"\n"), saveopt);
	free(optstr);
	return (RET_ERR);
}

#include <netinet/in.h>

int
make_secure(args, hostname, nconfp)
	struct nfs_args *args;
	char *hostname;
	struct netconfig **nconfp;
{
	static char netname[MAXNETNAMELEN+1];

	/* check to see if any secure mode is requested */
	if ((args->flags & (NFSMNT_SECURE | NFSMNT_KERBEROS)) == 0)
		return (0);

	if (args->flags & NFSMNT_KERBEROS) {
		(void) sprintf(netname, "nfs.%s", hostname);
	} else {
		/* NFSMNT_SECURE */
		/*
		 * XXX: need to support other netnames
		 * outside domain and not always just use
		 * the default conversion.
		 */
		if (!host2netname(netname, hostname, NULL)) {
			pr_err(gettext("host2netname: %s: unknown"),
				hostname);
			return (-1);
		}
	}
	args->netname = netname;

	/*
	 * Get the network address for the time service on
	 * the server.  If an RPC based time service is
	 * not available then try the old IP time service
	 * if it's a UDP transport.
	 *
	 * The reason we only use UDP for IP time service
	 * here is because in the kernel the rtime() routine
	 * (in rpc/authdesubr.c) will establish CLTS connection
	 * if the NFSMNT_RPCTIMESYNC flag is not set.
	 *
	 * This is not a total solution because mount will fail
	 * if the remote server does not support rpcbind-based
	 * time service over udp.
	 *
	 * This problem should be revisited in the future.
	 *
	 */
	args->syncaddr = get_the_addr(hostname, RPCBPROG, RPCBVERS, 0, *nconfp);
	if (args->syncaddr != NULL) {
		args->flags |= NFSMNT_RPCTIMESYNC;
	} else if ((strcmp((*nconfp)->nc_protofmly, NC_INET) == 0) &&
			(strcmp((*nconfp)->nc_proto, NC_UDP) == 0)) {
		struct nd_hostserv hs;
		struct nd_addrlist *retaddrs;

		hs.h_host = hostname;
		hs.h_serv = "rpcbind";
		if (netdir_getbyname(*nconfp, &hs, &retaddrs) != ND_OK) {
			goto err;
		}
		args->syncaddr = retaddrs->n_addrs;
		/* LINTED pointer alignment */
		((struct sockaddr_in *) args->syncaddr->buf)->sin_port
			= IPPORT_TIMESERVER;
	} else
		goto err;

	return (0);

err:
	pr_err(gettext("%s: secure: no time service\n"), hostname);
	return (-1);
}

/*
 * Get the network address on "hostname" for program "prog"
 * with version "vers" by using the nconf configuration data
 * passed in.
 *
 * If the address of a netconfig pointer is null then
 * information is not sufficient and no netbuf will be returned.
 *
 * Finally, ping the null procedure of that service.
 */
struct netbuf *
get_the_addr(hostname, prog, vers, port, nconf)
	char *hostname;
	int prog, vers, port;
	struct netconfig *nconf;
{
	struct netbuf *nb = NULL;
	struct t_bind *tbind = NULL;
	enum clnt_stat cs;
	CLIENT *cl = NULL;
	struct timeval tv;
	int fd = -1;

	if (nconf == NULL) {
		return (NULL);
	}

	if ((fd = t_open(nconf->nc_device, O_RDWR, &tinfo)) == -1) {
		    goto done;
	}

	/* LINTED pointer alignment */
	if ((tbind = (struct t_bind *) t_alloc(fd, T_BIND, T_ADDR))
		== NULL) {
		goto done;
	}

	if (rpcb_getaddr(prog, vers, nconf, &tbind->addr, hostname) == 0) {
		goto done;
	}

	if (port) {
		/* LINTED pointer alignment */
		((struct sockaddr_in *) tbind->addr.buf)->sin_port = port;
	}

	cl = clnt_tli_create(fd, nconf, &tbind->addr, prog, vers, 0, 0);
	if (cl == NULL) {
		goto done;
	}
	tv.tv_sec = 5;
	tv.tv_usec = 0;
	cs = clnt_call(cl, NULLPROC, xdr_void, 0, xdr_void, 0, tv);
	if (cs != RPC_SUCCESS) {
		goto done;
	}

	/*
	 * Make a copy of the netbuf to return
	 */
	nb = (struct netbuf *) malloc(sizeof (struct netbuf));
	if (nb == NULL) {
		pr_err(gettext("no memory\n"));
		goto done;
	}
	*nb = tbind->addr;
	nb->buf = (char *)malloc(nb->maxlen);
	if (nb->buf == NULL) {
		pr_err(gettext("no memory\n"));
		free(nb);
		nb = NULL;
		goto done;
	}
	(void) memcpy(nb->buf, tbind->addr.buf, nb->len);

done:
	if (cl) {
		clnt_destroy(cl);
		cl = NULL;
	}
	if (tbind) {
		t_free((char *) tbind, T_BIND);
		tbind = NULL;
	}
	if (fd >= 0)
		(void) t_close(fd);
	return (nb);
}


/*
 * Get a network address on "hostname" for program "prog"
 * with version "vers".  If the port number is specified (non zero)
 * then try for a TCP/UDP transport and set the port number of the
 * resulting IP address.
 *
 * If the address of a netconfig pointer was passed then
 * if it's not null use it as the netconfig otherwise
 * assign the address of the netconfig that was used to
 * establish contact with the service.
 */
struct netbuf *
get_addr(hostname, prog, vers, port, nconfp)
	char *hostname;
	int prog, vers, port;
	struct netconfig **nconfp;
{
	struct netbuf *nb = NULL;
	struct netconfig *nconf;
	NCONF_HANDLE *nc = NULL;
	int i;

	i = 1;

	if (nconfp && *nconfp) {
		nconf = *nconfp;
	} else {

	/*
	 * First choice is COTS, second is CLTS.  When we retry,
	 * we reset the the netconfig again, so that we search the
	 * whole list for our second choice, not just the part
	 * after the first choice.
	 */
startover:
		nc = setnetpath();
		if (nc == NULL)
			goto done;

retry:
		/*
		 * If the port number is specified then TCP/UDP is needed.
		 * Otherwise any connection/connectionless transport will do.
		 */
		if (nfs_proto) {
		    while (nconf = getnetpath(nc)) {
			if (strcmp(nconf->nc_netid, nfs_proto) == 0) {
			    if (port == 0)
				break;
			    if ((strcmp(nconf->nc_protofmly, NC_INET) == 0) &&
				((strcmp(nconf->nc_proto, NC_TCP) == 0) ||
				(strcmp(nconf->nc_proto, NC_UDP) == 0)))
					break;
			    else {
				nconf = NULL;
				break;
			    }
			}
		    }
		} else {
		    while (nconf = getnetpath(nc)) {
			if (nconf->nc_flag & NC_VISIBLE) {
			    if (i == 1) {
				if ((nconf->nc_semantics == NC_TPI_COTS_ORD) ||
				(nconf->nc_semantics == NC_TPI_COTS)) {
				    if (port == 0)
					break;
				    if ((strcmp(nconf->nc_protofmly,
					NC_INET) == 0) &&
					(strcmp(nconf->nc_proto, NC_TCP) == 0))
					break;
				}
			    }
			    if (i == 2) {
				if (nconf->nc_semantics == NC_TPI_CLTS) {
				    if (port == 0)
					break;
				    if ((strcmp(nconf->nc_protofmly,
					NC_INET) == 0) &&
					(strcmp(nconf->nc_proto, NC_UDP)
					== 0))
					break;
				}
			    }
			}
		    } /* while */
		} /* else */
		if (nconf == NULL) {
			if (nfs_proto)
				goto done;
			if (++i <= MNT_PREF_LISTLEN)
				goto startover;
			else
				goto done;
		}
	} /* else */

	/*
	 * Get the network address based on the given prog/vers/port/nconf.
	 */
	if ((nb = get_the_addr(hostname, prog, vers, port, nconf)) == NULL) {
		if (nfs_proto)
		    goto done;
		else
		    goto retry;
	}

	/*
	 * Get the netconfig data
	 */
	if (nconfp && *nconfp == NULL) {
		*nconfp = getnetconfigent(nconf->nc_netid);
		if (*nconfp == NULL) {
			pr_err(gettext("no memory\n"));
			free(nb);
			nb = NULL;
			goto done;
		}
	}

done:
	if (nc)
		endnetconfig(nc);
	return (nb);
}

/*
 * get fhandle of remote path from server's mountd
 */
int
get_fh(args, fshost, fspath)
	struct nfs_args *args;
	char *fshost, *fspath;
{
	static struct fhstatus fhs;
	static struct mountres3 mountres3;
	static nfs_fh3 fh3;
	static struct pathcnf p;
	struct timeval timeout = { 25, 0};
	CLIENT *cl;
	enum clnt_stat rpc_stat;
	u_long outvers = 0;
	u_long vers_to_try;
	static int printed = 0;
	int count, i;
	char *msg;
	int *auths;

	if (nfsvers == 2)
		vers_to_try = MOUNTVERS_POSIX;
	else
		vers_to_try = MOUNTVERS3;

	while ((cl = clnt_create_vers(fshost, MOUNTPROG, &outvers,
			MOUNTVERS, vers_to_try, "datagram_v")) == NULL) {
		if (rpc_createerr.cf_stat == RPC_UNKNOWNHOST) {
			pr_err("%s:%s\n", fshost, clnt_spcreateerror(""));
			return (RET_ERR);
		}

		/*
		 * back off and try the previous version - patch to the
		 * problem of version numbers not being contigous and
		 * clnt_create_vers failing (SunOS4.1 clients & SGI servers)
		 * The problem happens with most non-Sun servers who
		 * don't support mountd protocol #2. So, in case the
		 * call fails, we re-try the call anyway.
		 */
		vers_to_try--;
		if (vers_to_try >= MOUNTVERS)
			continue;
		if (!printed) {
			pr_err("%s:%s\n", fshost, clnt_spcreateerror(""));
			printed = 1;
		}
		return (RET_RETRY);
	}
	if (posix && outvers < MOUNTVERS_POSIX) {
		pr_err("%s:%s: no pathconf info\n",
			fshost, clnt_sperror(cl, ""));
		clnt_destroy(cl);
		return (RET_ERR);
	}

	if (__clnt_bindresvport(cl) < 0) {
		pr_err(gettext("Couldn't bind to reserved port\n"));
		clnt_destroy(cl);
		return (RET_RETRY);
	}

	cl->cl_auth = authsys_create_default();

	switch (outvers) {
	case MOUNTVERS:
	case MOUNTVERS_POSIX:
		if (nfsvers != 0 && nfsvers != NFS_VERSION) {
			pr_err("%s:%s: NFS Version %d not supported\n",
			    fshost, fspath, nfsvers);
			clnt_destroy(cl);
			return (RET_ERR);
		}
		nfsvers_to_use = NFS_VERSION;
		rpc_stat = clnt_call(cl, MOUNTPROC_MNT, xdr_dirpath,
			(caddr_t)&fspath, xdr_fhstatus, (caddr_t)&fhs, timeout);
		if (rpc_stat != RPC_SUCCESS) {
			pr_err("%s:%s server not responding %s\n",
				fshost, fspath, clnt_sperror(cl, ""));
			clnt_destroy(cl);
			return (RET_RETRY);
		}

		if ((errno = fhs.fhs_status) != MNT_OK) {
			if (errno == EACCES) {
				pr_err("%s:%s: access denied\n", fshost,
				fspath);
			} else {
				pr_err("%s:%s: ", fshost, fspath);
				perror("");
			}
			clnt_destroy(cl);
			return (RET_ERR);
		}
		args->fh = (caddr_t) &fhs.fhstatus_u.fhs_fhandle;
		if (!errno && posix) {
			rpc_stat = clnt_call(cl, MOUNTPROC_PATHCONF,
				xdr_dirpath, (caddr_t)&fspath, xdr_ppathcnf,
				(caddr_t)&p, timeout);
			if (rpc_stat != RPC_SUCCESS) {
				pr_err("%s:%s: server not responding %s\n",
					fshost, fspath, clnt_sperror(cl, ""));
				clnt_destroy(cl);
				return (RET_RETRY);
			} else {
				if (_PC_ISSET(_PC_ERROR, p.pc_mask)) {
					pr_err(
					    "%s:%s: mount: no pathconf info.\n",
					    fshost, fspath);
					clnt_destroy(cl);
					return (RET_ERR);
				}
				args->pathconf = &p;
				args->flags |= NFSMNT_POSIX;
			}
		}
		break;

	case MOUNTVERS3:
		if (nfsvers != 0 && nfsvers != NFS_V3) {
			pr_err("%s:%s: NFS Version %d not supported\n",
				fshost, fspath, nfsvers);
			clnt_destroy(cl);
			return (RET_ERR);
		}
		nfsvers_to_use = NFS_V3;
		rpc_stat = clnt_call(cl, MOUNTPROC_MNT, xdr_dirpath,
				(caddr_t)&fspath,
		    xdr_mountres3, (caddr_t)&mountres3, timeout);
		if (rpc_stat != RPC_SUCCESS) {
			pr_err(gettext("%s%s: server not responding %s\n"),
				fshost, fspath, clnt_sperror(cl, ""));
			clnt_destroy(cl);
			return (RET_RETRY);
		}

		/*
		 * Assume here that most of the MNT3ERR_*
		 * codes map into E* errors.
		 */
		if ((errno = mountres3.fhs_status) != MNT_OK) {
			switch (errno) {
			case MNT3ERR_NAMETOOLONG:
				msg = "path name is too long";
				break;
			case MNT3ERR_NOTSUPP:
				msg = "operation not supported";
				break;
			case MNT3ERR_SERVERFAULT:
				msg = "server fault";
				break;
			default:
				msg = NULL;
				break;
			}
			if (msg)
				pr_err("%s:%s: ", fshost, fspath);
			else
				perror("");
			clnt_destroy(cl);
			return (RET_ERR);
		}

		fh3.fh3_length =
			mountres3.mountres3_u.mountinfo.fhandle.fhandle3_len;
		memcpy(fh3.fh3_u.data,
			mountres3.mountres3_u.mountinfo.fhandle.fhandle3_val,
			fh3.fh3_length);
		args->fh = (caddr_t)&fh3;

		/*
		 * Now set the authentication.
		 * If "secure" or "kerberos" is a mount option
		 * then make sure the server supports it otherwise
		 * it's an error.
		 * If no mount option is given then just use
		 * the first in the list.
		 */
		auths =
		mountres3.mountres3_u.mountinfo.auth_flavors.auth_flavors_val;
		count =
		mountres3.mountres3_u.mountinfo.auth_flavors.auth_flavors_len;

		if (args->flags & NFSMNT_SECURE) {
			for (i = 0; i < count; i++)
				if (auths[i] == AUTH_DES)
					break;
			if (i >= count) {
				pr_err("secure unsupported for %s:%s\n",
					fshost, fspath);
				clnt_destroy(cl);
				return (RET_ERR);
			}
		} else if (args->flags & NFSMNT_KERBEROS) {
			for (i = 0; i < count; i++)
				if (auths[i] == AUTH_KERB)
					break;
			if (i >= count) {
				pr_err("kerberos unsupported for %s:%s\n",
					fshost, fspath);
				clnt_destroy(cl);
				return (RET_ERR);
			}
		} else if (count > 0) {
			/*
			 * Find the first recognized auth flavor
			 * from the auth list returned by the server.
			 */
			for (i = 0; i < count; i++) {
			    switch (auths[i]) {
			    case AUTH_NONE:
			    case AUTH_UNIX:
			    case AUTH_SHORT:
				goto success;
			    case AUTH_DES:
				args->flags |= NFSMNT_SECURE;
				goto success;
			    case AUTH_KERB:
				args->flags |= NFSMNT_KERBEROS;
				goto success;
			    default:
				break;
			    }
			}
			/*
			 *  If none is found, print out the 1st unrecognized
			 *  flavor number information.
			 */
			pr_err("Unknown authentication flavor #%d for %s:%s\n",
			    auths[0], fshost, fspath);
			clnt_destroy(cl);
			return (RET_ERR);
		}
success:
		fstype = MNTTYPE_NFS3;
		break;
	default:
		pr_err("%s:%s: Unknown MOUNT version %d\n",
			fshost, fspath, outvers);
		clnt_destroy(cl);
		return (RET_ERR);
	}

	clnt_destroy(cl);
	return (RET_OK);
}

/*
 * Fill in the address for the server's NFS service and
 * fill in a knetconfig structure for the transport that
 * the service is available on.
 */
int
getaddr_nfs(args, fshost, nconfp)
	struct nfs_args *args;
	struct netconfig **nconfp;
{
	struct stat sb;
	struct netconfig *nconf;
	static struct knetconfig knconf;
	static int printed = 0;


	args->addr = get_addr((char *)fshost, (int) NFS_PROGRAM,
		(int) nfsvers_to_use, nfs_port, nconfp);

	if (args->addr == NULL) {
		if (!printed) {
			pr_err(gettext(
				"%s: NFS service not responding\n"), fshost);
			printed = 1;
		}
		return (RET_RETRY);
	}
	nconf = *nconfp;

	if (stat(nconf->nc_device, &sb) < 0) {
		pr_err(gettext("get_knconf: couldn't stat: %s: %s\n"),
				nconf->nc_device, strerror(errno));
		return (RET_ERR);
	}

	knconf.knc_semantics = nconf->nc_semantics;
	knconf.knc_protofmly = nconf->nc_protofmly;
	knconf.knc_proto = nconf->nc_proto;
	knconf.knc_rdev = sb.st_rdev;

	/* make sure we don't overload the transport */
	if (tinfo.tsdu > 0 && tinfo.tsdu < NFS_MAXDATA + NFS_RPC_HDR) {
		args->flags |= (NFSMNT_RSIZE | NFSMNT_WSIZE);
		if (args->rsize == 0 || args->rsize > tinfo.tsdu - NFS_RPC_HDR)
			args->rsize = tinfo.tsdu - NFS_RPC_HDR;
		if (args->wsize == 0 || args->wsize > tinfo.tsdu - NFS_RPC_HDR)
			args->wsize = tinfo.tsdu - NFS_RPC_HDR;
	}

	args->flags |= NFSMNT_KNCONF;
	args->knconf = &knconf;
	return (RET_OK);
}

#define	TIME_MAX 16


int
retry(mntp, ro)
	struct mnttab *mntp;
	int ro;
{
	int delay = 5;
	int count = retries;
	int r;

	if (bg) {
		if (fork() > 0)
			return (RET_OK);
		pr_err(gettext("backgrounding: %s\n"), mntp->mnt_mountp);
	} else
		pr_err(gettext("retrying: %s\n"), mntp->mnt_mountp);

	while (count--) {
		if ((r = mount_nfs(mntp, ro)) == RET_OK) {
			pr_err(gettext("%s: mounted OK\n"), mntp->mnt_mountp);
			return (RET_OK);
		}
		if (r != RET_RETRY)
			break;

		sleep(delay);
		delay *= 2;
		if (delay > 120)
			delay = 120;
	}
	pr_err(gettext("giving up on: %s\n"), mntp->mnt_mountp);
	return (RET_ERR);
}



/*
 * Fix remount entry in /etc/mnttab.
 * This routine is modified from delete_mnttab in umount.
 */
void
fix_remount(mntpnt)
	char *mntpnt;
{
	FILE *fp;
	struct mnttab mnt;
	mntlist_t *mntl_head = NULL;
	mntlist_t *mntl_prev = NULL, *mntl;
	mntlist_t *modify = NULL;
	int mlock;

	mlock = fslock_mnttab();

	fp = fopen(MNTTAB, "r+");
	if (fp == NULL) {
		perror(MNTTAB);
		exit(RET_ERR);
	}

	(void) lockf(fileno(fp), F_LOCK, 0L);

	/*
	 * Read the entire mnttab into memory.
	 * Remember the *last* instance of the mounted
	 * mount point (have to take stacked mounts into
	 * account) and make sure that it's updated.
	 */
	while (getmntent(fp, &mnt) == 0) {
		mntl = (mntlist_t *) malloc(sizeof (*mntl));
		if (mntl == NULL) {
			pr_err(gettext("no mem\n"));
			(void) fclose(fp);
			exit(RET_ERR);
		}
		if (mntl_head == NULL)
			mntl_head = mntl;
		else
			mntl_prev->mntl_next = mntl;
		mntl_prev = mntl;
		mntl->mntl_next = NULL;
		mntl->mntl_mnt = fsdupmnttab(&mnt);
		if (mntl->mntl_mnt == NULL) {
			(void) fclose(fp);
			exit(RET_ERR);
		}
		if (strcmp(mnt.mnt_mountp, mntpnt) == 0)
			modify = mntl;
	}

	/* Now truncate the mnttab and write it back with the modified entry. */

	signal(SIGHUP,  SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGINT,  SIG_IGN);

	rewind(fp);
	if (ftruncate(fileno(fp), 0) < 0) {
		pr_err(gettext("truncate %s: %s\n"), MNTTAB, strerror(errno));
		(void) fclose(fp);
		exit(RET_ERR);
	}

	for (mntl = mntl_head; mntl; mntl = mntl->mntl_next) {
		if (mntl == modify) {
			char	*p;

			/* 'ro' -> 'rw' */
			if (p = strstr((modify->mntl_mnt)->mnt_mntopts, "ro"))
				if (*(p+2) == ',' || *(p+2) == '\0')
					*(p+1) = 'w';
			(void) sprintf((modify->mntl_mnt)->mnt_time, "%ld",
				time(0L));
		}

		if (putmntent(fp, mntl->mntl_mnt) <= 0) {
			pr_err("putmntent");
			perror("");
			(void) fclose(fp);
			exit(RET_ERR);
		}
	}

	(void) fclose(fp);
	fsunlock_mnttab(mlock);
}
