/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)umount.c	1.12	95/08/28 SMI"	/* SVr4.0 1.7	*/

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
 *     Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 *  (c) 1986,1987,1988.1989  Sun Microsystems, Inc
 *  (c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *            All rights reserved.
 *
 */
/*
 * nfs umount
 */

#include <stdio.h>
#include <string.h>
#include <varargs.h>
#include <signal.h>
#include <unistd.h>
#include <rpc/rpc.h>
#include <sys/mnttab.h>
#include <nfs/nfs.h>
#include <rpcsvc/mount.h>
#include <errno.h>
#include <locale.h>
#include <fslib.h>

#define	RET_OK	0
#define	RET_ERR	32

void	pr_err();
void	usage();
int	nfs_unmount();
void	inform_server();
struct mnttab *mnttab_find();

char *myname;
char typename[64];

extern int errno;

main(argc, argv)
	int argc;
	char **argv;
{
	extern int optind;
	int c;

	(void) setlocale(LC_ALL, "");

#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	myname = strrchr(argv[0], '/');
	myname = myname ? myname+1 : argv[0];
	(void) sprintf(typename, "nfs %s", myname);
	argv[0] = typename;

	/*
	 * Set options
	 */
	while ((c = getopt(argc, argv, "")) != EOF) {
		switch (c) {
		default:
			usage();
			exit(RET_ERR);
		}
	}
	if (argc - optind != 1) {
		usage();
		exit(RET_ERR);
	}

	if (geteuid() != 0) {
		pr_err(gettext("not super user\n"));
		exit(RET_ERR);
	}

	exit(nfs_unmount(argv[optind]));
}

void
pr_err(fmt, va_alist)
	char *fmt;
	va_dcl
{
	va_list ap;

	va_start(ap);
	(void) fprintf(stderr, "%s: ", typename);
	(void) vfprintf(stderr, fmt, ap);
	va_end(ap);
}

void
usage()
{
	(void) fprintf(stderr,
	    gettext("Usage: nfs umount [-o opts] {server:path | dir}\n"));
	exit(RET_ERR);
}

int
nfs_unmount(pathname)
	char *pathname;
{
	struct mnttab *mntp;

	mntp = mnttab_find(pathname);
	if (mntp) {
		pathname = mntp->mnt_mountp;
	}
	
	if (umount(pathname) < 0) {
		if (errno == EBUSY) {
			pr_err(gettext("%s: is busy\n"), pathname);
		} else {
			pr_err(gettext("%s: not mounted\n"), pathname);
			if (mntp)
				fsrmfrommtab(mntp);
		}
		return (RET_ERR);
	}

	if (mntp) {
		inform_server(mntp->mnt_special);
		fsrmfrommtab(mntp);
	}

	return (RET_OK);
}

/*
 *  Find the mnttab entry that corresponds to "name".
 *  We're not sure what the name represents: either
 *  a mountpoint name, or a special name (server:/path).
 *  Return the last entry in the file that matches.
 */
struct mnttab *
mnttab_find(dirname)
	char *dirname;
{
	FILE *fp;
	struct mnttab mnt;
	struct mnttab *res = NULL;
	int mlock = fslock_mnttab();

	fp = fopen(MNTTAB, "r");
	if (fp == NULL) {
		pr_err("%s: %s\n", MNTTAB, strerror(errno));
		fsunlock_mnttab(mlock);
		return (NULL);
	}
	while (getmntent(fp, &mnt) == 0) {
		if (strcmp(mnt.mnt_mountp , dirname) == 0 ||
		    strcmp(mnt.mnt_special, dirname) == 0) {
			if (res)
				fsfreemnttab(res);
			res = fsdupmnttab(&mnt);
		}
	}

	fclose(fp);
	fsunlock_mnttab(mlock);
	return (res);
}


void
inform_server(fsname)
	char *fsname;
{
	char *host, *path;
	struct timeval timeout;
	CLIENT *cl;
	enum clnt_stat rpc_stat;

	host = strdup(fsname);
	if (host == NULL) {
		pr_err(gettext("no mem\n"));
		return;
	}
	path = strchr(host, ':');
	if (path == NULL) {
		pr_err(gettext("%s is not hostname:path format\n"));
		return;
	}
	*path++ = '\0';

	cl = clnt_create(host, MOUNTPROG, MOUNTVERS, "datagram_n");
	if (cl == NULL) {
		pr_err("%s:%s %s\n", host, path,
		    clnt_spcreateerror("server not responding"));
		return;
	}
	if (__clnt_bindresvport(cl) < 0) {
		pr_err(gettext("couldn't bind to reserved port\n"));
		clnt_destroy(cl);
		return;
	}
	timeout.tv_usec = 0;
	timeout.tv_sec = 5;
	clnt_control(cl, CLSET_RETRY_TIMEOUT, (char *)&timeout);
	cl->cl_auth = authsys_create_default();
	timeout.tv_sec = 25;
	rpc_stat = clnt_call(cl, MOUNTPROC_UMNT, xdr_dirpath, (char *)&path,
	    xdr_void, (char *)NULL, timeout);
	AUTH_DESTROY(cl->cl_auth);
	clnt_destroy(cl);
	if (rpc_stat != RPC_SUCCESS)
		pr_err("%s\n", clnt_sperror(cl, "unmount"));
}
