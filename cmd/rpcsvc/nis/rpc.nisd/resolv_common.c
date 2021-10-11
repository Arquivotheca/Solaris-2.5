/* Copyright (c) 1993 Sun Microsystems Inc */

/*
 * Routines used by rpc.nisd and by rpc.nisd_resolv
 */

#include <rpc/rpc.h>
#include <netdb.h>
#include <rpcsvc/yp_prot.h>
#include <errno.h>
#include <sys/types.h>
#include "resolv_common.h"

#ifdef TDRPC
extern int sys_nerr;
extern char *sys_errlist[];
extern int errno;

char* strerror(err) /* no 4.1.3 strerror() */
int err;
{
	if (err > 0 && err < sys_nerr)
		return (sys_errlist[err]);
	else
		return ((char*) NULL);
}
#endif

bool_t xdr_ypfwdreq_key(xdrs, ps)
XDR *xdrs;
struct ypfwdreq_key *ps;
{
	return (xdr_ypmap_wrap_string(xdrs, &ps->map) &&
		xdr_datum(xdrs, &ps->keydat) &&
		xdr_u_long(xdrs, &ps->xid) &&
		xdr_u_long(xdrs, &ps->ip) &&
		xdr_u_short(xdrs, &ps->port));
}


u_long svc_getxid(xprt)
register SVCXPRT *xprt;
{
	register struct bogus_data *su = getbogus_data(xprt);
	if (su == NULL) return (0);
	return (su->su_xid);
}
