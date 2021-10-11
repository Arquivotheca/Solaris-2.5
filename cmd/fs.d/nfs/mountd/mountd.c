/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)mountd.c	1.30	95/07/10 SMI"	/* SVr4.0 1.11	*/

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
 *		All rights reserved.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <syslog.h>
#include <sys/param.h>
#include <rpc/rpc.h>
#include <sys/stat.h>
#include <netconfig.h>
#include <netdir.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/errno.h>
#include <sys/resource.h>
#include <nfs/nfs.h>
#include <rpcsvc/mount.h>
#include <sys/pathconf.h>
#include <sys/systeminfo.h>
#include <sys/utsname.h>
#include <signal.h>
#include <locale.h>
#include <unistd.h>
#include <sys/mnttab.h>
#include "../../fslib.h"
#include "sharetab.h"


#define	MAXHOSTNAMELEN		64
#define	MAXRMTABLINELEN		(MAXPATHLEN + MAXHOSTNAMELEN + 2)
#define	MIN(a, b)	((a) < (b) ? (a) : (b))

extern int errno;

static char RMTAB[] = "/etc/rmtab";
static FILE *rmtabf = NULL;

/*
 * mountd's version of a "struct mountlist". It is the same except
 * for the added ml_pos field.
 */
struct mountdlist {
/* same as XDR mountlist */
	char *ml_name;
	char *ml_path;
	struct mountdlist *ml_nxt;
/* private to mountd */
	long ml_pos;		/* position of mount entry in RMTAB */
};

struct mountdlist *mntlist;

struct sh_list {		/* cached share list */
	struct sh_list *shl_next;
	struct share    shl_sh;
} *sh_list;

int nfs_portmon = 1;

static void check_sharetab(void);
static char *exmalloc(int);
static void export(struct svc_req *);
static struct share *findentry(char *);
static struct share *find_lofsentry(char *, int *);
static void freeexports(struct exportnode *);
static struct nd_hostservlist *getclientsnames(SVCXPRT *);
static char *in_access_list(struct nd_hostservlist *, char *, int *, int);
static int in_mount_list(struct mountdlist *, char *, char *);
static char *in_netgroups(struct nd_hostservlist *, int, char *);
static void log_cant_reply(SVCXPRT *);
static void mnt(struct svc_req *, SVCXPRT *);
static void mnt_pathconf(struct svc_req *);
static void mount(struct svc_req *r);
static struct exportnode **newexport(char *, struct groupnode *,
				struct exportnode **);
static void rmtab_rewrite(void);
static struct groupnode **newgroup(char *, struct groupnode **);
static void rmtab_load(void);
static void rmtab_delete(long);
static long rmtab_insert(char *, char *);
static void sh_free(struct sh_list *);
static void umount(struct svc_req *);
static void umountall(struct svc_req *);
static void usage(void);

static int rmtab_inuse;
static int rmtab_deleted;
/*
 * There is nothing magic about the value selected here. Too low,
 * and mountd might spend too much time rewriting the rmtab file.
 * Too high, it won't do it frequently enough.
 */
static int rmtab_del_thresh = 250;

#define	RMTAB_TOOMANY_DELETED()	\
	((rmtab_deleted > rmtab_del_thresh) && (rmtab_deleted > rmtab_inuse))

main(argc, argv)
	int argc;
	char **argv;
{
	register int i;
	struct rlimit rl;

	(void) setlocale(LC_ALL, "");

#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	if (geteuid() != 0) {
		(void) fprintf(stderr, "%s must be run as root\n", argv[0]);
		exit(1);
	}

	if (argc == 2) {
		if (strcmp(argv[1], "-n") == 0) {
			nfs_portmon = 0;
		} else {
			usage();
		}
	} else if (argc > 2) {
		usage();
	}

	/* Don't drop core if the NFS module isn't loaded. */
	signal(SIGSYS, SIG_IGN);

	switch (fork()) {
	case 0:		/* child */
		break;
	case -1:
		perror("mountd: can't fork");
		exit(1);
	default:	/* parent */
		exit(0);
	}

	/*
	 * Close existing file descriptors, open "/dev/null" as
	 * standard input, output, and error, and detach from
	 * controlling terminal.
	 */
	getrlimit(RLIMIT_NOFILE, &rl);
	for (i = 0; i < rl.rlim_max; i++)
		(void) close(i);
	(void) open("/dev/null", O_RDONLY);
	(void) open("/dev/null", O_WRONLY);
	(void) dup(1);
	(void) setsid();

	openlog("mountd", LOG_PID, LOG_DAEMON);
	audit_mountd_setup();	/* BSM */

	/*
	 * Create datagram service
	 */
	if (svc_create(mnt, MOUNTPROG, MOUNTVERS, "datagram_v") == 0) {
		syslog(LOG_ERR, "couldn't register datagram_v MOUNTVERS");
		exit(1);
	}
	if (svc_create(mnt, MOUNTPROG, MOUNTVERS_POSIX, "datagram_v") == 0) {
		syslog(LOG_ERR, "couldn't register datagram_v MOUNTVERS_POSIX");
		exit(1);
	}

	if (svc_create(mnt, MOUNTPROG, MOUNTVERS3, "datagram_v") == 0) {
		syslog(LOG_ERR, "couldn't register datagram_v MOUNTVERS3");
		exit(1);
	}

	/*
	 * Create connection oriented service
	 */
	if (svc_create(mnt, MOUNTPROG, MOUNTVERS, "circuit_v") == 0) {
		syslog(LOG_ERR, "couldn't register circuit_v MOUNTVERS");
		exit(1);
	}
	if (svc_create(mnt, MOUNTPROG, MOUNTVERS_POSIX, "circuit_v") == 0) {
		syslog(LOG_ERR, "couldn't register circuit_v MOUNTVERS_POSIX");
		exit(1);
	}

	if (svc_create(mnt, MOUNTPROG, MOUNTVERS3, "circuit_v") == 0) {
		syslog(LOG_ERR, "couldn't register circuit_v MOUNTVERS3");
		exit(1);
	}

	/*
	 * Start serving
	 */
	rmtab_load();
	svc_run();
	syslog(LOG_ERR, "Error: svc_run shouldn't have returned");
	abort();
	/* NOTREACHED */
}

/*
 * Server procedure switch routine
 */
void
mnt(rqstp, transp)
	struct svc_req *rqstp;
	SVCXPRT *transp;
{
	switch (rqstp->rq_proc) {
	case NULLPROC:
		errno = 0;
		if (!svc_sendreply(transp, xdr_void, (char *)0))
			log_cant_reply(transp);
		return;
	case MOUNTPROC_MNT:
		mount(rqstp);
		return;
	case MOUNTPROC_DUMP:
		errno = 0;
		if (!svc_sendreply(transp, xdr_mountlist, (char *)&mntlist))
			log_cant_reply(transp);
		return;
	case MOUNTPROC_UMNT:
		umount(rqstp);
		if (RMTAB_TOOMANY_DELETED())
			rmtab_rewrite();
		return;
	case MOUNTPROC_UMNTALL:
		umountall(rqstp);
		if (RMTAB_TOOMANY_DELETED())
			rmtab_rewrite();
		return;
	case MOUNTPROC_EXPORT:
	case MOUNTPROC_EXPORTALL:
		export(rqstp);
		return;
	case MOUNTPROC_PATHCONF:
		if (rqstp->rq_vers == MOUNTVERS_POSIX) {
			mnt_pathconf(rqstp);
			return;
		}
		/* else fall through to error out */
	default:
		svcerr_noproc(transp);
		return;
	}
}

/*
 * Get the client's hostname from the transport handle
 * If the name is not available then return "(anon)".
 */
static struct nd_hostservlist *
getclientsnames(transp)
	SVCXPRT *transp;
{
	struct netbuf *nbuf;
	struct netconfig *nconf;
	static struct nd_hostservlist *serv;
	static struct nd_hostservlist anon_hsl;
	static struct nd_hostserv anon_hs;
	static char anon_hname[] = "(anon)";
	static char anon_sname[] = "";

	/* Set up anonymous client */
	anon_hs.h_host = anon_hname;
	anon_hs.h_serv = anon_sname;
	anon_hsl.h_cnt = 1;
	anon_hsl.h_hostservs = &anon_hs;

	if (serv) {
		netdir_free((char *) serv, ND_HOSTSERVLIST);
		serv = NULL;
	}
	nconf = getnetconfigent(transp->xp_netid);
	if (nconf == NULL) {
		syslog(LOG_ERR, "%s: getnetconfigent failed",
			transp->xp_netid);
		return (&anon_hsl);
	}

	nbuf = svc_getrpccaller(transp);
	if (nbuf == NULL) {
		freenetconfigent(nconf);
		return (&anon_hsl);
	}
	if (netdir_getbyaddr(nconf, &serv, nbuf)) {
		freenetconfigent(nconf);
		return (&anon_hsl);
	}
	freenetconfigent(nconf);
	return (serv);
}

static void
log_cant_reply(transp)
	SVCXPRT *transp;
{
	int saverrno;
	struct nd_hostservlist *clnames;
	register char *name;

	saverrno = errno;	/* save error code */
	clnames = getclientsnames(transp);
	if (clnames == NULL)
		return;
	name = clnames->h_hostservs->h_host;

	errno = saverrno;
	if (errno == 0)
		syslog(LOG_ERR, "couldn't send reply to %s", name);
	else
		syslog(LOG_ERR, "couldn't send reply to %s: %m", name);
}

/*
 * Answer pathconf questions for the mount point fs
 */
static void
mnt_pathconf(rqstp)
	struct svc_req *rqstp;
{
	SVCXPRT *transp;
	struct pathcnf p;
	char *path, rpath[MAXPATHLEN];
	struct stat st;
	struct nd_hostservlist *clnames;

	transp = rqstp->rq_xprt;
	path = NULL;
	memset((caddr_t)&p, 0, sizeof (p));
	clnames = getclientsnames(transp);
	if (clnames == NULL) {
		_PC_SET(_PC_ERROR, p.pc_mask);
		goto done;
	}
	if (!svc_getargs(transp, xdr_dirpath, (caddr_t) &path)) {
		svcerr_decode(transp);
		return;
	}
	if (lstat(path, &st) < 0) {
		_PC_SET(_PC_ERROR, p.pc_mask);
		goto done;
	}
	/*
	 * Get a path without symbolic links.
	 */
	if (realpath(path, rpath) == NULL) {
		syslog(LOG_DEBUG,
			"mount request: realpath failed on %s: %m",
			path);
		_PC_SET(_PC_ERROR, p.pc_mask);
		goto done;
	}
	memset((caddr_t)&p, 0, sizeof (p));
	/*
	 * can't ask about devices over NFS
	 */
	_PC_SET(_PC_MAX_CANON, p.pc_mask);
	_PC_SET(_PC_MAX_INPUT, p.pc_mask);
	_PC_SET(_PC_PIPE_BUF, p.pc_mask);
	_PC_SET(_PC_VDISABLE, p.pc_mask);

	errno = 0;
	p.pc_link_max = pathconf(rpath, _PC_LINK_MAX);
	if (errno)
		_PC_SET(_PC_LINK_MAX, p.pc_mask);
	p.pc_name_max = pathconf(rpath, _PC_NAME_MAX);
	if (errno)
		_PC_SET(_PC_NAME_MAX, p.pc_mask);
	p.pc_path_max = pathconf(rpath, _PC_PATH_MAX);
	if (errno)
		_PC_SET(_PC_PATH_MAX, p.pc_mask);
	if (pathconf(rpath, _PC_NO_TRUNC) == 1)
		_PC_SET(_PC_NO_TRUNC, p.pc_mask);
	if (pathconf(rpath, _PC_CHOWN_RESTRICTED) == 1)
		_PC_SET(_PC_CHOWN_RESTRICTED, p.pc_mask);

done:
	errno = 0;
	if (!svc_sendreply(transp, xdr_ppathcnf, (char *)&p))
		log_cant_reply(transp);
	if (path != NULL)
		svc_freeargs(transp, xdr_dirpath, (caddr_t) &path);
}


/*
 * Check mount requests, add to mounted list if ok
 */
static void
mount(rqstp)
	struct svc_req *rqstp;
{
	SVCXPRT *transp;
	int version;
	struct fhstatus fhs;
	struct mountres3 mountres3;
	char fh3[FHSIZE3];
	char *path, rpath[MAXPATHLEN];
	struct mountdlist *ml;
	struct share *sh;
	struct nd_hostservlist *clnames;
	int i, restricted;
	char *name;
	int error = 0, lofs_tried = 0;
	int flavor;
	char *fhp;

	transp = rqstp->rq_xprt;
	version = rqstp->rq_vers;
	path = NULL;
	clnames = getclientsnames(transp);
	if (clnames == NULL) {
		error = EACCES;
		goto done;
	}
	name = clnames->h_hostservs[0].h_host;
	if (!svc_getargs(transp, xdr_dirpath, (caddr_t) &path)) {
		svcerr_decode(transp);
		return;
	}

	/*
	 * Get the real path (no symbolic links in it)
	 */
	if (realpath(path, rpath) == NULL) {
		error = errno;
		syslog(LOG_DEBUG, "mount request: realpath: %s: %m",
			path);
		goto done;
	}

	if ((sh = findentry(rpath)) == NULL &&
		(sh = find_lofsentry(rpath, &lofs_tried)) == NULL) {
		error = EACCES;
		goto done;
	}

	/*
	 * We assume here that the filehandle returned from
	 * nfs_getfh() is only 32 bytes.
	 * NFS V2 clients get only the 32 byte filehandle.
	 * NFS V3 clients get a 64 byte filehandle consisting
	 * of a 32 byte filehandle followed by 32 bytes of nulls.
	 */
	if (version == MOUNTVERS3) {
		mountres3.mountres3_u.mountinfo.fhandle.fhandle3_len = 32;
		mountres3.mountres3_u.mountinfo.fhandle.fhandle3_val = fh3;
		fhp = fh3;
		memset(fhp + 32, 0, 32);
	} else {
		fhp = (char *) &fhs.fhstatus_u.fhs_fhandle;
	}
	while (nfs_getfh(rpath, fhp) < 0) {
		if (errno == EINVAL && (sh = find_lofsentry(rpath, &lofs_tried)) != NULL) {
			errno = 0;
			continue;
		}
		error = errno == EINVAL ? EACCES : errno;
		syslog(LOG_DEBUG,
			"mount request: getfh failed on %s: %m",
			path);
		goto done;
	}

	/*
	 * Check "ro" list (used to be "access" list)
	 * Try hostnames first - then netgroups.
	 */

	restricted = 0;
	if ((name = in_access_list(clnames,
				getshareopt(sh->sh_opts, SHOPT_RO),
				&restricted, 0)) == NULL &&
	    (name = in_access_list(clnames,
				getshareopt(sh->sh_opts, SHOPT_RW),
				&restricted, 0)) == NULL &&
	    (name = in_access_list(clnames,
				getshareopt(sh->sh_opts, SHOPT_ROOT),
				NULL, 1)) == NULL) {
		if (restricted) {
			error = EACCES;
			audit_mountd_mount(clnames->h_hostservs[0].h_host,
					path, 0); /* BSM */
		} else {
			name = clnames->h_hostservs[0].h_host;
		}
	}


done:
	switch (version) {
	case MOUNTVERS:
	case MOUNTVERS_POSIX:
		if (error == EINVAL)
			fhs.fhs_status = NFSERR_ACCES;
		else if (error == EREMOTE)
			fhs.fhs_status = NFSERR_REMOTE;
		else
			fhs.fhs_status = error;
		if (!svc_sendreply(transp, xdr_fhstatus, (char *)&fhs))
			log_cant_reply(transp);
		if (!fhs.fhs_status)
			audit_mountd_mount(clnames->h_hostservs[0].h_host,
					path, 1);	/* BSM */
		break;

	case MOUNTVERS3:
		if (!error) {
			if (getshareopt(sh->sh_opts, SHOPT_SECURE))
				flavor = AUTH_DES;
			else if (getshareopt(sh->sh_opts, SHOPT_KERBEROS))
				flavor = AUTH_KERB;
			else
				flavor = AUTH_UNIX;
		mountres3.mountres3_u.mountinfo.auth_flavors.auth_flavors_val =
								&flavor;
		mountres3.mountres3_u.mountinfo.auth_flavors.auth_flavors_len =
								1;
		} else if (error == ENAMETOOLONG)
			error = MNT3ERR_NAMETOOLONG;
		mountres3.fhs_status = error;
		if (!svc_sendreply(transp, xdr_mountres3, (char *)&mountres3))
			log_cant_reply(transp);
		if (!mountres3.fhs_status)
			audit_mountd_mount(clnames->h_hostservs[0].h_host,
					path, 1);	/* BSM */

		break;
	}

	if (path != NULL)
		svc_freeargs(transp, xdr_dirpath, (caddr_t) &path);
	if (error)
		return;

	/*
	 *  Add an entry for this mount to the mount list.
	 *  First check whether it's there already - the client
	 *  may have crashed and be rebooting.
	 */
	if (in_mount_list(mntlist, name, rpath))
		return;

	/*
	 * Add this mount to the mount list.
	 */
	ml = (struct mountdlist *) exmalloc(sizeof (struct mountdlist));
	ml->ml_path = (char *) exmalloc(strlen(rpath) + 1);
	(void) strcpy(ml->ml_path, rpath);
	ml->ml_name = (char *) exmalloc(strlen(name) + 1);
	(void) strcpy(ml->ml_name, name);
	ml->ml_nxt = mntlist;
	ml->ml_pos = rmtab_insert(name, rpath);
	rmtab_inuse++;
	mntlist = ml;
}

static struct share *
findentry(path)
	char *path;
{
	struct share *sh;
	struct sh_list *shp;
	register char *p1, *p2;

	check_sharetab();

	for (shp = sh_list; shp; shp = shp->shl_next) {
		sh = &shp->shl_sh;
		for (p1 = sh->sh_path, p2 = path; *p1 == *p2; p1++, p2++)
			if (*p1 == '\0')
				return (sh);	/* exact match */

		if ((*p1 == '\0' && *p2 == '/') ||
		    (*p1 == '\0' && *(p1-1) == '/') ||
		    (*p2 == '\0' && *p1 == '/' && *(p1+1) == '\0')) {
			if (issubdir(path, sh->sh_path))
				return (sh);
		}
	}
	return ((struct share *) 0);
}


static int
is_substring(char **mntp, char **path)
{
	char *p1 = *mntp, *p2 = *path;

	if (*p1 == '\0' && *p2 == '\0') /* exact match */
		return (1);
	else if (*p1 == '\0' && *p2 == '/')
		return (1);
	else if (*p1 == '\0' && *(p1-1) == '/') {
		*path = --p2; /* we need the slash in p2 */
		return (1);
	} else if (*p2 == '\0') {
		while (*p1 == '/')
			p1++;
		if (*p1 == '\0') /* exact match */
			return (1);
	}
	return (0);
}


/*
 * Refer Bugid 1210409
 * stat the rpath
 * gets the mnttab and skips the entries that
 *   are not lofs
 * looks for the mountpoint which is a substring of
 *   the rpath
 * if it is a substring further check if the mountpoint is
 *   having the same dev and rdev as the rpath
 * if the matching entry is found and the devids match
 *  construct a new path by concatenating the mnt_special and
 *  the remaining of rpath and does a findentry on that
 *
 */
static struct share *
find_lofsentry(char *rpath, int *done_flag)
{
	struct stat r_stbuf, mnt_stbuf;
	mntlist_t *ml, *mntl;
	struct share *retcode = NULL;
	char lofs_path[MAXPATHLEN];

	if (*done_flag++)
		return (retcode);

	if (stat(rpath, &r_stbuf) != 0)
		return (retcode);

	mntl = fsgetmntlist();
	for (ml = mntl; ml; ml = ml->mntl_next) {
		char *p1, *p2;

		if (strcmp(ml->mntl_mnt->mnt_fstype, "lofs"))
			continue;

		for (p1 = ml->mntl_mnt->mnt_mountp, p2 = rpath;
				*p1 == *p2 && *p1; p1++, p2++);

		if (is_substring(&p1, &p2)) {

			if (stat(ml->mntl_mnt->mnt_mountp, &mnt_stbuf) != 0 ||
				r_stbuf.st_dev != mnt_stbuf.st_dev ||
				r_stbuf.st_rdev != mnt_stbuf.st_rdev)
				continue;

			if ((strlen(ml->mntl_mnt->mnt_special)+strlen(p2))
					< MAXPATHLEN) {
				strcpy(lofs_path, ml->mntl_mnt->mnt_special);
				strcat(lofs_path, p2);
				if (retcode = findentry(lofs_path)) {
					strcpy(rpath, lofs_path);
					break;
				}
			}
		}
	}
	fsfreemntlist(mntl);
	return (retcode);

}

/*
 * Determine whether an access list grants rights to a particular host.
 *
 * We match on aliases of the hostname as well as on the canonical name.
 *
 * Names in the access list may be either hosts or netgroups;  they're
 * not distinguished syntactically.  We check for hosts first because it's
 * cheaper (just M*N strcmp()s), then try netgroups.
 *
 * We do grungy special-casing for the root access-list, which doesn't
 * allow netgroups and which doesn't let the access list be set to null.
 */
static char *
in_access_list(clnames, access_list, restrictp, is_root)
	struct nd_hostservlist
		*clnames;
	char	*access_list;	/* N.B. we clobber this "input" parameter */
	int	*restrictp;
	int	is_root;
{
	int	nentries;
	char	*gr;
	int	i;

	if (access_list == NULL) {		/* Access list not specified */
		return (NULL);
	}

	if (!is_root && access_list[0] == '\0') { /* Access list set to null */
		return (clnames->h_hostservs[0].h_host);
	}

	if (restrictp != NULL) {
		(*restrictp)++;
	}

	for (gr = strtok(access_list, ":"), nentries = 0;
		gr != NULL; gr = strtok(NULL, ":"), nentries++) {
		for (i = 0; i < clnames->h_cnt; i++) {
			char *hname = clnames->h_hostservs[i].h_host;
			if (strcasecmp(gr, hname) == 0) {
				return (hname);	/* Matched a hostname */
			}
		}
	}
	if (is_root) {
		return (NULL);
	} else {
		return (in_netgroups(clnames, nentries, access_list));
	}
}

static char *
in_netgroups(clnames, ngroups, grl)
	struct nd_hostservlist
		*clnames;
	int	ngroups;
	char	*grl;	/* Contains (ngroups) strings separated by '\0' */
{
	char	**pgroups;
	char	*gr;
	int	nhosts = clnames->h_cnt;
	int	i;
	static char
		*domain;

	extern	int __multi_innetgr();	/* Private interface to innetgr(), */
					/* Accepts N strings rather than 1 */

	if (domain == NULL) {
		int	ssize;
		domain = exmalloc(SYS_NMLN);
		ssize = sysinfo(SI_SRPC_DOMAIN, domain, SYS_NMLN);
		if (ssize > SYS_NMLN) {
			free(domain);
			domain = exmalloc(ssize);
			ssize = sysinfo(SI_SRPC_DOMAIN, domain, ssize);
		}
		/* Check for error in syscall or NULL domain name */
		if (ssize <= 1) {
			syslog(LOG_ERR, "No default domain set");
			return (NULL);
		}
	}

	if ((pgroups = (char **)calloc(ngroups, sizeof (char *))) == NULL) {
		return (0);
	}
	for (i = 0, gr = grl; i < ngroups; i++, gr += strlen(gr) + 1) {
		pgroups[i] = gr;
	}
	for (i = 0; i < nhosts; i++) {
		char *hname = clnames->h_hostservs[i].h_host;
		if (__multi_innetgr(ngroups,	pgroups,
				    1,		&hname,
				    0,		NULL,
				    1,		&domain)) {
			free(pgroups);
			return (hname);
		}
	}
	free(pgroups);
	return (NULL);
}

static void
check_sharetab()
{
	FILE *f;
	struct stat st;
	static long last_sharetab_time;
	struct share *sh;
	struct sh_list *shp, *shp_prev;
	int res, c = 0;
	char rpath[MAXPATHLEN+1];

	/*
	 *  read in /etc/dfs/sharetab if it has changed
	 */

	if (stat(SHARETAB, &st) != 0) {
		syslog(LOG_ERR, "Cannot stat %s: %m", SHARETAB);
		return;
	}
	if (st.st_mtime == last_sharetab_time)
		return;				/* no change */

	sh_free(sh_list);			/* free old list */
	sh_list = NULL;

	f = fopen(SHARETAB, "r");
	if (f == NULL)
		return;

	while ((res = getshare(f, &sh)) > 0) {
		c++;
		if (strcmp(sh->sh_fstype, "nfs") != 0)
			continue;


		/* Remove symbolic links from export path */
		if (realpath(sh->sh_path, rpath) == NULL) {
			syslog(LOG_ERR,
				"check_sharetab: realpath: %s: %m",
				sh->sh_path);
			continue;
		}
		shp = (struct sh_list *)malloc(sizeof (struct sh_list));
		if (shp == NULL)
			goto alloc_failed;
		if (sh_list == NULL)
			sh_list = shp;
		else
			shp_prev->shl_next = shp;
		shp_prev = shp;
		memset((char *)shp, 0, sizeof (struct sh_list));

		shp->shl_sh.sh_path = strdup(rpath);
		if (shp->shl_sh.sh_path == NULL)
			goto alloc_failed;
		if (sh->sh_opts) {
			shp->shl_sh.sh_opts = strdup(sh->sh_opts);
			if (shp->shl_sh.sh_opts == NULL)
				goto alloc_failed;
		}
	}
	if (res < 0)
		syslog(LOG_ERR, "%s: invalid at line %d\n",
			SHARETAB, c + 1);

	(void) fclose(f);
	last_sharetab_time = st.st_mtime;
	return;

alloc_failed:
	syslog(LOG_ERR, "check_sharetab: no memory");
	sh_free(sh_list);
	sh_list = NULL;
	(void) fclose(f);
}

static void
sh_free(shp)
	struct sh_list *shp;
{
	register struct sh_list *next;

	while (shp) {
		if (shp->shl_sh.sh_path)
			free(shp->shl_sh.sh_path);
		if (shp->shl_sh.sh_opts)
			free(shp->shl_sh.sh_opts);
		next = shp->shl_next;
		free((char *)shp);
		shp = next;
	}
}


/*
 * Remove an entry from mounted list
 */
static void
umount(rqstp)
	struct svc_req *rqstp;
{
	char *path;
	struct mountdlist *ml, *oldml;
	struct nd_hostservlist *clnames;
	SVCXPRT *transp;

	transp = rqstp->rq_xprt;
	path = NULL;
	if (!svc_getargs(transp, xdr_dirpath, (caddr_t) &path)) {
		svcerr_decode(transp);
		return;
	}
	errno = 0;
	if (!svc_sendreply(transp, xdr_void, (char *)NULL))
		log_cant_reply(transp);

	clnames = getclientsnames(transp);
	if (clnames != NULL) {
		audit_mountd_umount(clnames->h_hostservs[0].h_host,
				path); /* BSM */
		oldml = mntlist;
		for (ml = mntlist; ml != NULL; oldml = ml, ml = ml->ml_nxt) {
			if (strcmp(ml->ml_path, path) == 0 &&
			    strcmp(ml->ml_name,
				clnames->h_hostservs->h_host) == 0) {
				if (ml == mntlist) {
					mntlist = ml->ml_nxt;
				} else {
					oldml->ml_nxt = ml->ml_nxt;
				}
				rmtab_delete(ml->ml_pos);
				free(ml->ml_path);
				free(ml->ml_name);
				free((char *)ml);
				rmtab_inuse--;
				rmtab_deleted++;
				break;
			}
		}
	}
	svc_freeargs(transp, xdr_dirpath, (caddr_t) &path);
}

/*
 * Remove all entries for one machine from mounted list
 */
static void
umountall(rqstp)
	struct svc_req *rqstp;
{
	struct mountdlist *ml, *oldml;
	struct nd_hostservlist *clnames;
	SVCXPRT *transp;

	transp = rqstp->rq_xprt;
	if (!svc_getargs(transp, xdr_void, NULL)) {
		svcerr_decode(transp);
		return;
	}
	/*
	 * We assume that this call is asynchronous and made via rpcbind
	 * callit routine.  Therefore return control immediately. The error
	 * causes rpcbind to remain silent, as apposed to every machine
	 * on the net blasting the requester with a response.
	 */
	svcerr_systemerr(transp);
	clnames = getclientsnames(transp);
	if (clnames == NULL) {
		return;
	}
	oldml = mntlist;
	for (ml = mntlist; ml != NULL; ml = ml->ml_nxt) {
		if (strcmp(ml->ml_name, clnames->h_hostservs->h_host) == 0) {
			if (ml == mntlist) {
				mntlist = ml->ml_nxt;
				oldml = mntlist;
			} else {
				oldml->ml_nxt = ml->ml_nxt;
			}
			rmtab_delete(ml->ml_pos);
			free(ml->ml_path);
			free(ml->ml_name);
			free((char *)ml);
			rmtab_inuse--;
			rmtab_deleted++;
		} else {
			oldml = ml;
		}
	}
}

/*
 * send current export list
 */
static void
export(rqstp)
	struct svc_req *rqstp;
{
	struct exportnode *ex;
	struct exportnode **tail;
	char *grl_ro, *grl_rw;
	char *gr;
	struct groupnode *groups;
	struct groupnode **grtail;
	SVCXPRT *transp;
	struct share *sh;
	struct sh_list *shp;

	transp = rqstp->rq_xprt;
	if (!svc_getargs(transp, xdr_void, NULL)) {
		svcerr_decode(transp);
		return;
	}

	check_sharetab();

	ex = NULL;
	tail = &ex;
	for (shp = sh_list; shp; shp = shp->shl_next) {
		sh = &shp->shl_sh;

		grl_ro = getshareopt(sh->sh_opts, SHOPT_RO);
		grl_rw = getshareopt(sh->sh_opts, SHOPT_RW);

		if ((grl_ro && (*grl_ro == '\0')) ||
		    (grl_rw && (*grl_rw == '\0'))) {
			tail = newexport(sh->sh_path, NULL, tail);
		} else {
			groups = NULL;
			grtail = &groups;
			if (grl_ro != NULL) {
				while ((gr = strtok(grl_ro, ":")) != NULL) {
					grl_ro = NULL;
					grtail = newgroup(gr, grtail);
				}
			}

			if (grl_rw != NULL) {
				while ((gr = strtok(grl_rw, ":")) != NULL) {
					grl_rw = NULL;
					grtail = newgroup(gr, grtail);
				}
			}
			tail = newexport(sh->sh_path, groups, tail);
		}
	}

	errno = 0;
	if (!svc_sendreply(transp, xdr_exports, (char *)&ex))
		log_cant_reply(transp);
	freeexports(ex);
}


static void
freeexports(ex)
	struct exportnode *ex;
{
	struct groupnode *groups, *tmpgroups;
	struct exportnode *tmpex;

	while (ex) {
		groups = ex->ex_groups;
		while (groups) {
			tmpgroups = groups->gr_next;
			free(groups->gr_name);
			free((char *)groups);
			groups = tmpgroups;
		}
		tmpex = ex->ex_next;
		free(ex->ex_dir);
		free((char *)ex);
		ex = tmpex;
	}
}


static struct groupnode **
newgroup(grname, tail)
	char *grname;
	struct groupnode **tail;
{
	struct groupnode *new;
	char *newname;

	new = (struct groupnode *) exmalloc(sizeof (*new));
	newname = (char *) exmalloc(strlen(grname) + 1);
	(void) strcpy(newname, grname);

	new->gr_name = newname;
	new->gr_next = NULL;
	*tail = new;
	return (&new->gr_next);
}


static struct exportnode **
newexport(grname, grplist, tail)
	char *grname;
	struct groupnode *grplist;
	struct exportnode **tail;
{
	struct exportnode *new;
	char *newname;

	new = (struct exportnode *) exmalloc(sizeof (*new));
	newname = (char *) exmalloc(strlen(grname) + 1);
	(void) strcpy(newname, grname);

	new->ex_dir = newname;
	new->ex_groups = grplist;
	new->ex_next = NULL;
	*tail = new;
	return (&new->ex_next);
}

static char *
exmalloc(size)
	int size;
{
	char *ret;

	if ((ret = (char *) malloc((u_int)size)) == 0) {
		syslog(LOG_ERR, "Out of memory");
		exit(1);
	}
	return (ret);
}

static void
usage()
{
	(void) fprintf(stderr, gettext("Usage: mountd [-n]\n"));
	exit(1);
}

/*
 * Read in and internalize the contents of rmtab.  Rewrites the file to get
 * rid of unused entries.
 */

static void
rmtab_load()
{
	char *path;
	char *name;
	char *end;
	struct mountdlist *ml;
	char line[MAXRMTABLINELEN];

	mntlist = NULL;
	rmtabf = fopen(RMTAB, "r");
	if (rmtabf != NULL) {
		while (fgets(line, sizeof (line), rmtabf) != NULL) {
			name = line;
			path = strchr(name, ':');
			if (*name != '#' && path != NULL) {
				*path = 0;
				path++;
				end = strchr(path, '\n');
				if (end != NULL) {
					*end = 0;
				}
				if (in_mount_list(mntlist, name, path)) {
					continue; /* skip duplicates */
				}
				ml = (struct mountdlist *)
					exmalloc(sizeof (struct mountdlist));
				ml->ml_path = (char *)
					exmalloc(strlen(path) + 1);
				(void) strcpy(ml->ml_path, path);
				ml->ml_name = (char *)
					exmalloc(strlen(name) + 1);
				(void) strcpy(ml->ml_name, name);
				ml->ml_nxt = mntlist;
				mntlist = ml;
			}
		}
		(void) fclose(rmtabf);
		rmtabf = NULL;
	}
	rmtab_rewrite();
}

static void
rmtab_rewrite()
{
	struct mountdlist *ml;

	if (rmtabf != NULL) {
		(void) fclose(rmtabf);
	}

	(void) truncate(RMTAB, (off_t)0);

	/* Rewrite the file. */
	rmtabf = fopen(RMTAB, "w+");
	if (rmtabf != NULL) {
		rmtab_inuse = rmtab_deleted = 0;
		for (ml = mntlist; ml != NULL; ml = ml->ml_nxt) {
			ml->ml_pos = rmtab_insert(ml->ml_name, ml->ml_path);
			rmtab_inuse++;
		}
	}
}

/*
 * Check whether the given client/path combination already appears in the
 * mount list.  Returns 1 if yes, 0 if no.
 */

static int
in_mount_list(list, hostname, directory)
	struct mountdlist *list;
	char *hostname;			/* client to check for */
	char *directory;		/* path to check for */
{
	struct mountdlist *ml;

	for (ml = list; ml != NULL; ml = ml->ml_nxt) {
		if (strcmp(hostname, ml->ml_name) == 0 &&
		    strcmp(directory, ml->ml_path) == 0) {
			return (1);
		}
	}

	return (0);
}

/*
 * Write an entry at the current location in rmtab for the given client and
 * path.
 *
 * Returns the starting position of the entry, -1 if there was an error.
 */

long
rmtab_insert(hostname, path)
	char *hostname;			/* name of client */
	char *path;
{
	long pos;

	if (rmtabf == NULL || fseek(rmtabf, 0L, 2) == -1) {
		return (-1);
	}
	pos = ftell(rmtabf);
	if (fprintf(rmtabf, "%s:%s\n", hostname, path) == EOF) {
		return (-1);
	}
	if (fflush(rmtabf) == EOF) {
		return (-1);
	}
	return (pos);
}

/*
 * Mark as unused the rmtab entry at the given offset in the file.
 */

void
rmtab_delete(pos)
	long pos;
{
	if (rmtabf != NULL && pos != -1 && fseek(rmtabf, pos, 0) == 0) {
		(void) fprintf(rmtabf, "#");
		(void) fflush(rmtabf);
	}
}
