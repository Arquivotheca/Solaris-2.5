/*
 *	cache_svc.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)cache_svc.c	1.6	93/04/17 SMI"

#include <stdio.h>
#include <rpc/rpc.h>
#include <signal.h>
#include <rpcsvc/nis.h>
#include <rpcsvc/nis_cache.h>

extern bool_t xdr_directory_obj(), xdr_fd_result();
extern void hold_signals();
extern void release_signals();



#ifdef TDRPC
/*
 * returns TRUE is caller is using loopback (local) transport.
 * otherwise returns FALSE
 */

bool_t
chklocal (xprt)
	SVCXPRT *xprt;
{
	struct sockaddr_in *who;
	struct in_addr taddr;

	who = svc_getcaller(xprt);
	taddr = ntohl(who->sin_addr);

	if (taddr.s_addr == INADDR_LOOPBACK) {
		return (TRUE);
	} else {
		return (FALSE);
	}
}

#endif /* TDRPC */


void
cacheprog_1(rqstp, transp)
	struct svc_req *rqstp;
	SVCXPRT *transp;
{
	union {
		fd_result add_entry_1_arg;
		directory_obj remove_entry_1_arg;
		char *nis_cache_refresh_entry_1_arg;
	} argument;
	char *result;
	bool_t (*xdr_argument)(), (*xdr_result)();
	char *(*local)();

	hold_signals();

	switch (rqstp->rq_proc) {
	case NULLPROC:
		(void) svc_sendreply(transp, xdr_void, (char *)NULL);
		release_signals();
		return;

	case NIS_CACHE_ADD_ENTRY:
		xdr_argument = xdr_fd_result;
		xdr_result = xdr_void;
		local = (char *(*)()) nis_cache_add_entry_1_svc;
		break;


	case NIS_CACHE_REMOVE_ENTRY:
		xdr_argument = xdr_directory_obj;
		xdr_result = xdr_void;
		local = (char *(*)()) nis_cache_remove_entry_1_svc;
		break;

	case NIS_CACHE_READ_COLDSTART:
		xdr_argument = xdr_void;
		xdr_result = xdr_void;
		local = (char *(*)()) nis_cache_read_coldstart_1_svc;
		break;

	case NIS_CACHE_REFRESH_ENTRY:
		xdr_argument = xdr_wrapstring;
		xdr_result = xdr_void;
		local = (char *(*)()) nis_cache_refresh_entry_1_svc;
		break;


	default:
		svcerr_noproc(transp);
		release_signals();
		return;
	}
	memset((char *)&argument, 0, sizeof (argument));
	if (!svc_getargs(transp, xdr_argument, (char *) &argument)) {
		svcerr_decode(transp);
		release_signals();
		return;
	}
	result = (*local)(&argument, rqstp);
	if (result != NULL && !svc_sendreply(transp, xdr_result, result)) {
		svcerr_systemerr(transp);
	}
	if (!svc_freeargs(transp, xdr_argument, (char *) &argument)) {
		(void) fprintf(stderr, "unable to free arguments\n");
		exit(1);
	}
	release_signals();
}
