/*
 * Copyright (c) 1993 Sun Microsystems, Inc.
 */

#ident	"@(#)rpc_sztypes.c	1.2	93/11/12 SMI"

/*
 * XDR routines for generic types that have explicit sizes.
 */

#include <rpc/rpc_sztypes.h>

/*
 * The new NFS protocol uses typedefs to name objects according to their
 * length (32 bits, 64 bits).  These objects appear in both the NFS and KLM
 * code, so the xdr routines live here.
 */

bool_t
xdr_uint64(xdrs, objp)
	register XDR *xdrs;
	u_longlong_t *objp;
{

	if (!xdr_u_longlong_t(xdrs, objp))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_int64(xdrs, objp)
	register XDR *xdrs;
	longlong_t *objp;
{

	if (!xdr_longlong_t(xdrs, objp))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_uint32(xdrs, objp)
	register XDR *xdrs;
	unsigned long *objp;
{

	if (!xdr_u_long(xdrs, objp))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_int32(xdrs, objp)
	register XDR *xdrs;
	long *objp;
{

	if (!xdr_long(xdrs, objp))
		return (FALSE);
	return (TRUE);
}
