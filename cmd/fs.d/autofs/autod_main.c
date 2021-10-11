/*
 *	autod_main.c
 *
 *	Copyright (c) 1988-1994 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)autod_main.c	1.16	94/11/14 SMI"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <memory.h>
#include <stropts.h>
#include <netconfig.h>
#include <varargs.h>
#include <sys/resource.h>
#include <sys/systeminfo.h>
#include <syslog.h>
#include <sys/sockio.h>
#include <rpc/rpc.h>
#include <rpcsvc/nfs_prot.h>
#include <net/if.h>
#include <netdir.h>
#include <string.h>
#include <thread.h>
#include "automount.h"
#include "autofs_prot.h"

extern int do_mount1(char *, char *, char *, char *, struct authunix_parms *);
extern int do_unmount1(umntrequest *);
extern int svc_create_local_service(void (*) (), u_long, u_long,
					char *, char *);
extern void ns_setup();

static void autofs_prog(struct svc_req *, SVCXPRT *);
static void autofs_mount_1_r(struct mntrequest *, struct mntres *,
					struct authunix_parms *);
static void autofs_unmount_1_r(struct umntrequest *, struct umntres *,
					struct authunix_parms *);
static void usage();
static void warn_hup(int);
static void getmyaddrs();

static char str_arch[32];
static char str_cpu[32];

struct autodir *dir_head;
struct autodir *dir_tail;
char self[64];

extern mutex_t ns_files_lock;
extern thread_t ns_files_owner;

mutex_t mt_unsafe = DEFAULTMUTEX;

main(argc, argv)
	int argc;
	char *argv[];

{
	pid_t pid;
	int c, i;
	int size;
	struct rlimit rl;
	extern char *optarg;
	int rpc_svc_mode = RPC_SVC_MT_AUTO;

	if (geteuid() != 0) {
		(void) fprintf(stderr, "%s must be run as root\n", argv[0]);
		exit(1);
	}

	while ((c = getopt(argc, argv, "vTD:")) != EOF) {
		switch (c) {
		case 'v':
			verbose++;
			break;
		case 'T':
			trace++;
			break;
		case 'D':
			(void) putenv(optarg);
			break;
		default:
			usage();
		}
	}

	/*
	 * Since the "arch" command no longer exists we
	 * have to rely on uname -m to return the closest
	 * approximation.  For backward compatibility we
	 * need to substitute "sun4" for "sun4m", "sun4c", ...
	 */
	if (getenv("ARCH") == NULL) {
		char buf[16];
		int len;
		FILE *f;

		if ((f = popen("uname -m", "r")) != NULL) {
			(void) fgets(buf, 16, f);
			(void) pclose(f);
			if (len = strlen(buf))
				buf[len - 1] = '\0';
			if (strncmp(buf, "sun4", 4) == 0)
				(void) strcpy(buf, "sun4");
			(void) sprintf(str_arch, "ARCH=%s", buf);
			(void) putenv(str_arch);
		}
	}
	if (getenv("CPU") == NULL) {
		char buf[16];
		int len;
		FILE *f;

		if ((f = popen("uname -p", "r")) != NULL) {
			(void) fgets(buf, 16, f);
			(void) pclose(f);
			if (len = strlen(buf))
				buf[len - 1] = '\0';
			(void) sprintf(str_cpu, "CPU=%s", buf);
			(void) putenv(str_cpu);
		}
	}

	(void) sysinfo(SI_HOSTNAME, self, sizeof (self));
	(void) ns_setup();

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
	for (i = 3; i < size; i++)
		(void) close(i);

	(void) setsid();
	openlog("automountd", LOG_PID, LOG_DAEMON);

	/*
	 * If we coredump it'll be /core.
	 */
	if (chdir("/") < 0)
		syslog(LOG_ERR, "chdir /: %m");

	/*
	 * Get all IP addresses for this host
	 */
	getmyaddrs();

	if (!rpc_control(RPC_SVC_MTMODE_SET, &rpc_svc_mode)) {
		syslog(LOG_ERR, "unable to set automatic MT mode");
		exit(1);
	}
	if (svc_create_local_service(autofs_prog,
		AUTOFS_PROG, AUTOFS_VERS, "netpath", "autofs") == 0) {
		syslog(LOG_ERR, "unable to create service");
		exit(1);
	}

	(void) signal(SIGHUP, warn_hup);

	svc_run();
	syslog(LOG_ERR, "svc_run returned");
	return (1);
}

/*
 * The old automounter supported a SIGHUP
 * to allow it to resynchronize internal
 * state with the /etc/mnttab.
 * This is no longer relevant, but we
 * need to catch the signal and warn
 * the user.
 */
/* ARGSUSED */
static void
warn_hup(i)
	int i;
{
	syslog(LOG_ERR, "SIGHUP received: ignored");
	(void) signal(SIGHUP, warn_hup);
}

static void
usage()
{
	(void) fprintf(stderr, "Usage: automountd\n"
		"\t[-T]\t\t(trace requests)\n"
		"\t[-v]\t\t(verbose error msgs)\n"
		"\t[-D n=s]\t(define env variable)\n");
	exit(1);
	/* NOTREACHED */
}

/*
 * Duplicate request checking.
 * Use a small fifo xid cache
 * to check for retransmitted requests.
 * Since the daemon is stateless, it will
 * attempt to mount over existing mounts
 * if it acts on a retransmited request
 * and will complain loudly about "busy"
 * mounts.
 *
 * XXX Note: this code should be removed
 * when the automountd supports a
 * connectionless transport.
 */
#define	XID_CACHE_SIZE 16
static u_long xid_cache[XID_CACHE_SIZE];
static u_long *xcpfirst = &xid_cache[0];
static u_long *xcp	 = &xid_cache[0];
static u_long *xcplast  = &xid_cache[XID_CACHE_SIZE - 1];

#define	MAX_OPT_WORDS	32

struct svc_dg_data {
	struct	netbuf optbuf;
	long	opts[MAX_OPT_WORDS];
	u_int   su_iosz;
	u_long	su_xid;
	XDR	su_xdrs;
	char	su_verfbody[MAX_AUTH_BYTES];
	char 	*su_cache;
	struct t_unitdata   su_tudata;
};

static int
dup_request(req)
	struct svc_req *req;
{
	u_long *x;
	int xid;
	static mutex_t xc_lock = DEFAULTMUTEX;

	if (req->rq_xprt->xp_p2 == NULL) {
		if (verbose)
			syslog(LOG_ERR, "dup_request: no xid");
		return (0);
	}

	xid = ((struct svc_dg_data *)(req->rq_xprt->xp_p2))->su_xid;
	/*
	 * Search the cache for the xid
	 */
	mutex_lock(&xc_lock);
	for (x = xcp; x >= xcpfirst; x--)
		if (xid == *x) {
			mutex_unlock(&xc_lock);
			return (1);
		}
	for (x = xcplast; x > xcp; x--)
		if (xid == *x) {
			mutex_unlock(&xc_lock);
			return (1);
		}

	/*
	 * Not there. Enter it.
	 */
	*xcp++ = xid;
	if (xcp > xcplast)
		xcp = xcpfirst;

	mutex_unlock(&xc_lock);
	return (0);
}

/*
 * Each RPC request will automatically spawn a new thread with this
 * as its entry point.
 */
static void
autofs_prog(rqstp, transp)
	struct svc_req *rqstp;
	register SVCXPRT *transp;
{
	union {
		mntrequest autofs_mount_1_arg;
		umntrequest autofs_umount_1_arg;
	} argument;

	union {
		mntres mount_res;
		umntres umount_res;
	} res;

	bool_t (*xdr_argument)();
	bool_t (*xdr_result)();
	void   (*local)();

	time_now = time((time_t *)NULL);

	if (dup_request(rqstp)) {
		if (verbose)
			syslog(LOG_ERR, "Duplicate request");
		return;
	}

	switch (rqstp->rq_proc) {
	case NULLPROC:
		(void) svc_sendreply(transp, xdr_void, (char *)NULL);
		return;

	case AUTOFS_MOUNT:
		xdr_argument = xdr_mntrequest;
		xdr_result = xdr_mntres;
		local = autofs_mount_1_r;
		break;

	case AUTOFS_UNMOUNT:
		xdr_argument = xdr_umntrequest;
		xdr_result = xdr_umntres;
		local = autofs_unmount_1_r;
		break;

	default:
		svcerr_noproc(transp);
		return;
	}

	(void) memset((char *)&argument, 0, sizeof (argument));
	if (!svc_getargs(transp, xdr_argument, (caddr_t)&argument)) {
		svcerr_decode(transp);
		return;
	}
	(*local)(&argument, &res, rqstp->rq_clntcred);
	if (!svc_sendreply(transp, xdr_result, (caddr_t)&res)) {
		svcerr_systemerr(transp);
	}

	if (!svc_freeargs(transp, xdr_argument, (caddr_t)&argument)) {
		syslog(LOG_ERR, "unable to free arguments");
	}

	/* Release ownership of the file name stack. */
	if (ns_files_owner == thr_self()) {
		ns_files_owner = 0;
		mutex_unlock(&ns_files_lock);
	}
}

/* ARGSUSED */
static void
autofs_unmount_1_r(m, res, cred)
	struct umntrequest *m;
	struct umntres *res;
	struct authunix_parms *cred;
{
	struct umntrequest *ul;

	mutex_lock(&mt_unsafe);

	if (trace > 0) {
		trace_prt(1, "UNMOUNT REQUEST:\n");
		for (ul = m; ul; ul = ul->next)
			trace_prt(1, "  dev=%x rdev=%x %s\n",
				ul->devid,
				ul->rdevid,
				ul->isdirect ? "direct" : "indirect");
	}

	res->status = do_unmount1(m);

	if (trace > 0)
		trace_prt(1, "UNMOUNT REPLY  : status=%d\n", res->status);

	mutex_unlock(&mt_unsafe);
}


static void
autofs_mount_1_r(m, res, cred)
	struct mntrequest *m;
	struct mntres *res;
	struct authunix_parms *cred;
{
	if (trace > 0) {
		trace_prt(1, "MOUNT REQUEST  : "
			"name=%s map=%s opts=%s path=%s\n",
			m->name, m->map, m->opts, m->path);
	}

	res->status = do_mount1(m->map, m->name, m->opts, m->path, cred);

	if (trace > 0) {
		trace_prt(1, "MOUNT REPLY    : status=%d\n", res->status);
	}

	if (res->status && verbose) {
		if (strcmp(m->path, m->name) == 0) {
			/* direct mount */
			syslog(LOG_ERR, "mount of %s failed", m->name);
		} else {
			/* indirect mount */
			syslog(LOG_ERR,
				"mount of %s/%s failed", m->path, m->name);
		}
	}
}

static struct ifconf ifc;

#define	MAXIFS 32

void
getmyaddrs()
{
	int sock;
	int numifs;
	char *buf;

	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	    syslog(LOG_ERR, "socket: %m");
	    return;
	}

	if (ioctl(sock, SIOCGIFNUM, (char *)&numifs) < 0) {
		syslog(LOG_ERR, "getmyaddrs: get number of interfaces");
		numifs = MAXIFS;
	}

	buf = (char *) malloc(numifs * sizeof (struct ifreq));
	if (buf == NULL) {
		syslog(LOG_ERR, "getmyaddrs: malloc failed: %m");
		(void) close(sock);
		return;
	}

	ifc.ifc_buf = buf;
	ifc.ifc_len = numifs * sizeof (struct ifreq);

	if (ioctl(sock, SIOCGIFCONF, (char *) &ifc) < 0) {
		syslog(LOG_ERR, "getmyaddrs: SIOCGIFCONF");
	}

	(void) close(sock);
}

int
self_check(hostname)
	char *hostname;
{
	int n;
	struct sockaddr_in *s1, *s2;
	struct ifreq *ifr;
	struct nd_hostserv hs;
	struct nd_addrlist *retaddrs;
	struct netconfig *nconfp;

	/*
	 * First do a quick compare of hostname
	 */
	if (strcmp(hostname, self) == 0)
		return (1);

	/*
	 * Get the IP address for hostname
	 */
	nconfp = getnetconfigent("udp");
	if (nconfp == NULL) {
		syslog(LOG_ERR, "getnetconfigent failed");
		return (0);
	}

	hs.h_host = hostname;
	hs.h_serv = "rpcbind";
	if (netdir_getbyname(nconfp, &hs, &retaddrs) != ND_OK) {
		freenetconfigent(nconfp);
		return (0);
	}
	freenetconfigent(nconfp);

	s1 = (struct sockaddr_in *) retaddrs->n_addrs->buf;

	/*
	 * Now compare it against the list of
	 * addresses for the interfaces on this
	 * host.
	 */
	ifr = ifc.ifc_req;
	n = ifc.ifc_len / sizeof (struct ifreq);
	s2 = NULL;
	for (; n > 0; n--, ifr++) {
		if (ifr->ifr_addr.sa_family != AF_INET)
			continue;

		s2 = (struct sockaddr_in *) &ifr->ifr_addr;

		if (memcmp((char *) &s2->sin_addr,
			(char *) &s1->sin_addr, 4) == 0) {
			netdir_free((void *) retaddrs, ND_ADDRLIST);
			return (1);	/* it's me */
		}
	}

	netdir_free((void *) retaddrs, ND_ADDRLIST);
	return (0);			/* it's not me */
}

/*
 * Used for reporting messages from code
 * shared with automount command.
 * Formats message into a buffer and
 * calls syslog.
 *
 * Print an error.
 * Works like printf (fmt string and variable args)
 * except that it will subsititute an error message
 * for a "%m" string (like syslog).
 */
void
pr_msg(fmt, va_alist)
	char *fmt;
	va_dcl
{
	va_list ap;
	char fmtbuff[BUFSIZ], buff[BUFSIZ], *p2;
	char *p1;
	extern int errno, sys_nerr;
	extern char *sys_errlist[];
	char *gettext();

	p2 = fmtbuff;
	fmt = gettext(fmt);

	for (p1 = fmt; *p1; p1++) {
		if (*p1 == '%' && *(p1+1) == 'm') {
			if (errno < sys_nerr) {
				(void) strcpy(p2, sys_errlist[errno]);
				p2 += strlen(p2);
			}
			p1++;
		} else {
			*p2++ = *p1;
		}
	}
	if (p2 > fmtbuff && *(p2-1) != '\n')
		*p2++ = '\n';
	*p2 = '\0';

	va_start(ap);
	(void) vsprintf(buff, fmtbuff, ap);
	va_end(ap);
	syslog(LOG_ERR, buff);
}
