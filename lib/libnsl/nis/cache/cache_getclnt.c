/*
 *	cache_getclnt.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)cache_getclnt.c	1.17	95/09/19 SMI"

/* This file contains functions that are only needed in case of TIRPC */

#ifndef TDRPC


#include <sys/stat.h>
#include <stdio.h>
#include <syslog.h>
#include <rpc/rpc.h>
#include <string.h>
#include <netconfig.h>
#include "tiuser.h"
#include <netdir.h>

#include <rpcsvc/nis.h>
#include <rpcsvc/nis_cache.h>
#include "../gen/nis_local.h"

#if defined(sparc)
#define _FSTAT _fstat
#else  /* !sparc */
#define _FSTAT fstat
#endif /* sparc */


/*
 *  Forward declarations for static functions.
 */
CLIENT * __getclnt(char *, struct netconfig *);
static void set_rdev(CLIENT *, dev_t *);
static int check_rdev(CLIENT *, dev_t);

/*
 * Create a CLIENT handle for the cachemgr given the uaddr and the
 * netconfig structure for the netid (loopback) that the cachemgr
 * is listening on.
 */

static CLIENT *
__getclnt(uaddr, nconf)
char *uaddr;
struct netconfig *nconf;
{
	int fd = RPC_ANYFD;
	struct netbuf *svcaddr;			/* servers address */
	CLIENT *cl;

	/* Get the address of the server */
	svcaddr = uaddr2taddr(nconf, uaddr);
	if (!svcaddr)
		return (NULL); /* should never happen */
	cl = clnt_tli_create(fd, nconf, svcaddr, CACHEPROG, CACHE_VER_1, 0, 0);
	if (!cl) {
		return (NULL);
	}
	netdir_free((char *)svcaddr, ND_ADDR);
	/* The fd should be closed while destroying the handle. */
	(void) CLNT_CONTROL(cl, CLSET_FD_CLOSE, (char *)NULL);
	if (CLNT_CONTROL(cl, CLGET_FD, (char *)&fd))
		fcntl(fd, F_SETFD, 1);	/* make it "close on exec" */
	return (cl);
}


/*
 * Return a CLIENT handle for the cachemgr listening on the loopback
 * transport at address uaddr. The cacheclnt is the earlier cache'ed client
 * handle.
 */

CLIENT *
__get_ti_clnt(uaddr, cacheclnt, olduaddr, pidp, rdevp)
	char *uaddr;
	CLIENT *cacheclnt;
	char **olduaddr;
	pid_t *pidp;
	dev_t *rdevp;
{
	struct netconfig *nconf;
	void *nc_handle;	/* Net config handle */
	bool_t found_loopback = FALSE;
	CLIENT* clnt = cacheclnt;

	if (!*olduaddr) {
		*olduaddr = strdup(uaddr);

	} else {
		if (strcmp(uaddr, *olduaddr) == 0 &&
		    check_rdev(clnt, *rdevp) &&
		    cacheclnt && *pidp == getpid()) {
			/*
			 * the uaddrs match and the process-ids match
			 * verify that the server is running by doing a
			 * connect ??
			 * check_bound() from rpcbind:check_bound.c
			 * Not needed if we go with the idea of having
			 * the cachemgr start by inetd. inetd would
			 * always listen on that endpoint and restart the
			 * cachemgr.
			 * I think we still need to do this atleast the first
			 * time as this file may be an earlier incarnation.
			 * think about it XXX!!
			 * Thought some more:
			 * make the zns_cachemgr a child of init by placing it
			 * in inittab and so it gets restarted each time it
			 * is killed.
			 * OR have it be restarted by inetd.
			 *
			 */
			return (clnt);
		}
		if (cacheclnt)
			clnt_destroy(cacheclnt);
	}
	/* This should be done only the first time and cached XXX */
	nc_handle = setnetconfig();
	if (nc_handle == (void *) NULL) {
		return (NULL);
	}
	while (nconf = getnetconfig(nc_handle))
		if ((strcmp(nconf->nc_protofmly, NC_LOOPBACK) == 0) &&
		    (nconf->nc_semantics == NC_TPI_CLTS)) {
			clnt = __getclnt(uaddr, nconf);
			if (clnt)
				break;
		}
	if (!clnt) {
		endnetconfig(nc_handle);
		return (NULL);
	}
	endnetconfig(nc_handle);
	free(*olduaddr);
	*olduaddr = strdup(uaddr);
	*pidp = getpid();
	set_rdev(clnt, rdevp);
	return (clnt);
}

static
void
set_rdev(clnt, rdevp)
	CLIENT *clnt;
	dev_t *rdevp;
{
	int fd;
	int st;
	struct stat stbuf;

	if (clnt_control(clnt, CLGET_FD, (char *)&fd) != TRUE ||
	    _FSTAT(fd, &stbuf) == -1) {
		syslog(LOG_DEBUG, "NIS+ cache client:  can't get rdev");
		*rdevp = -1;
		return;
	}
	*rdevp = stbuf.st_rdev;
}

static
int
check_rdev(clnt, rdev)
	CLIENT *clnt;
	dev_t rdev;
{
	struct stat stbuf;
	int fd;

	if (rdev == -1 || clnt_control(clnt, CLGET_FD, (char *)&fd) != TRUE)
		return (1);    /* can't check, assume it is okay */

	if (_FSTAT(fd, &stbuf) == -1) {
		syslog(LOG_DEBUG, "NIS+ cache client:  can't stat %d", fd);
		/* could be because file descriptor was closed */
		return (0);
	}
	if (rdev != stbuf.st_rdev) {
		syslog(LOG_DEBUG,
		    "NIS+ cache client:  fd %d changed, old=0x%x, new=0x%x",
		    fd, rdev, stbuf.st_rdev);
		/* it's not our file descriptor, so don't try to close it */
		clnt_control(clnt, CLSET_FD_NCLOSE, (char *)NULL);
		return (0);
	}
	return (1);    /* fd is okay */
}

#endif /* TDRPC */
