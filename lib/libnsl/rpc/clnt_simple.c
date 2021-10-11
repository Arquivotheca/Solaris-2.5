/*
 * Copyright (c) 1986-1991 by Sun Microsystems Inc.
 */

#ident	"@(#)clnt_simple.c	1.18	94/10/24 SMI"

#if !defined(lint) && defined(SCCSIDS)
static char sccsid[] = "@(#)clnt_simple.c 1.49 89/01/31 Copyr 1984 Sun Micro";
#endif

/*
 * clnt_simple.c
 * Simplified front end to client rpc.
 *
 */

#include "rpc_mt.h"
#include <stdio.h>
#include <errno.h>
#include <rpc/rpc.h>
#include <rpc/trace.h>
#include <string.h>
#include <sys/param.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef MAXHOSTNAMELEN
#define	MAXHOSTNAMELEN 64
#endif

#ifndef NETIDLEN
#define	NETIDLEN 32
#endif

struct rpc_call_private {
	int	valid;			/* Is this entry valid ? */
	CLIENT	*client;		/* Client handle */
	pid_t	pid;			/* process-id at moment of creation */
	u_long	prognum, versnum;	/* Program, version */
	char	host[MAXHOSTNAMELEN];	/* Servers host */
	char	nettype[NETIDLEN];	/* Network type */
};
static struct rpc_call_private *rpc_call_private_main;

static void
rpc_call_destroy(void *vp)
{
	register struct rpc_call_private *rcp = (struct rpc_call_private *)vp;

	if (rcp) {
		if (rcp->client)
			CLNT_DESTROY(rcp->client);
		free(rcp);
	}
}

/*
 * This is the simplified interface to the client rpc layer.
 * The client handle is not destroyed here and is reused for
 * the future calls to same prog, vers, host and nettype combination.
 *
 * The total time available is 25 seconds.
 */
enum clnt_stat
rpc_call(host, prognum, versnum, procnum, inproc, in, outproc, out, netclass)
	const char *host;			/* host name */
	u_long prognum;			/* program number */
	u_long versnum;			/* version number */
	u_long procnum;			/* procedure number */
	xdrproc_t inproc, outproc;	/* in/out XDR procedures */
	const char *in;
	char  *out;			/* recv/send data */
	const char *netclass;			/* nettype */
{
	struct rpc_call_private *rcp = (struct rpc_call_private *) 0;
	enum clnt_stat clnt_stat;
	struct timeval timeout, tottimeout;
	static thread_key_t rpc_call_key;
	int main_thread;
	extern mutex_t tsd_lock;
	char nettype_array[NETIDLEN];
	char *nettype = &nettype_array[0];

	trace4(TR_rpc_call, 0, prognum, versnum, procnum);


	if (netclass == NULL)
		nettype = NULL;
	else 
		strcpy(nettype, netclass);

	if ((main_thread = _thr_main())) {
		rcp = rpc_call_private_main;
	} else {
		if (rpc_call_key == 0) {
			mutex_lock(&tsd_lock);
			if (rpc_call_key == 0)
				thr_keycreate(&rpc_call_key, rpc_call_destroy);
			mutex_unlock(&tsd_lock);
		}
		thr_getspecific(rpc_call_key, (void **) &rcp);
	}
	if (rcp == (struct rpc_call_private *)NULL) {
		rcp = (struct rpc_call_private *)malloc(sizeof (*rcp));
		if (rcp == (struct rpc_call_private *)NULL) {
			rpc_createerr.cf_stat = RPC_SYSTEMERROR;
			rpc_createerr.cf_error.re_errno = errno;
			trace4(TR_rpc_call, 1, prognum, versnum, procnum);
			return (rpc_createerr.cf_stat);
		}
		if (main_thread)
			rpc_call_private_main = rcp;
		else
			thr_setspecific(rpc_call_key, (void *) rcp);
		rcp->valid = 0;
		rcp->client = NULL;
	}
	if ((nettype == NULL) || (nettype[0] == NULL))
		nettype = "netpath";
	if (!(rcp->valid && rcp->pid == getpid() &&
		(rcp->prognum == prognum) &&
		(rcp->versnum == versnum) &&
		(!strcmp(rcp->host, host)) &&
		(!strcmp(rcp->nettype, nettype)))) {
		int fd;

		rcp->valid = 0;
		if (rcp->client)
			CLNT_DESTROY(rcp->client);
		/*
		 * Using the first successful transport for that type
		 */
		rcp->client = clnt_create(host, prognum, versnum, nettype);
		rcp->pid = getpid();
		if (rcp->client == (CLIENT *)NULL) {
			trace4(TR_rpc_call, 1, prognum, versnum, procnum);
			return (rpc_createerr.cf_stat);
		}
		/*
		 * Set time outs for connectionless case.  Do it
		 * unconditionally.  Faster than doing a t_getinfo()
		 * and then doing the right thing.
		 */
		timeout.tv_usec = 0;
		timeout.tv_sec = 5;
		(void) CLNT_CONTROL(rcp->client,
				CLSET_RETRY_TIMEOUT, (char *) &timeout);
		if (CLNT_CONTROL(rcp->client, CLGET_FD, (char *)&fd))
			fcntl(fd, F_SETFD, 1);	/* make it "close on exec" */
		rcp->prognum = prognum;
		rcp->versnum = versnum;
		if ((strlen(host) < (size_t)MAXHOSTNAMELEN) &&
		    (strlen(nettype) < (size_t)NETIDLEN)) {
			(void) strcpy(rcp->host, host);
			(void) strcpy(rcp->nettype, nettype);
			rcp->valid = 1;
		} else {
			rcp->valid = 0;
		}
	} /* else reuse old client */
	tottimeout.tv_sec = 25;
	tottimeout.tv_usec = 0;
	clnt_stat = CLNT_CALL(rcp->client, procnum, inproc, (char *) in,
				outproc, out, tottimeout);
	/*
	 * if call failed, empty cache
	 */
	if (clnt_stat != RPC_SUCCESS)
		rcp->valid = 0;
	trace4(TR_rpc_call, 1, prognum, versnum, procnum);
	return (clnt_stat);
}
