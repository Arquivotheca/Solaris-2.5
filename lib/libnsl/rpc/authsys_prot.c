/*
 * Copyright (c) 1986-1991 by Sun Microsystems Inc.
 */

#ident	"@(#)authsys_prot.c	1.9	94/10/19 SMI"

#if !defined(lint) && defined(SCCSIDS)
static char sccsid[] = "@(#)authsys_prot.c 1.24 89/02/07 Copyr 1984 Sun Micro";
#endif

/*
 * authsys_prot.c
 * XDR for UNIX style authentication parameters for RPC
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 */

#ifdef KERNEL
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#endif

#include <rpc/types.h>
#include <rpc/trace.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/auth_sys.h>

bool_t xdr_uid_t();
bool_t xdr_gid_t();

/*
 * XDR for unix authentication parameters.
 */
bool_t
xdr_authsys_parms(xdrs, p)
	register XDR *xdrs;
	register struct authsys_parms *p;
{
	trace1(TR_xdr_authsys_parms, 0);
	if (xdr_u_long(xdrs, &(p->aup_time)) &&
	    xdr_string(xdrs, &(p->aup_machname), MAX_MACHINE_NAME) &&
	    xdr_uid_t(xdrs, (uid_t *)&(p->aup_uid)) &&
	    xdr_gid_t(xdrs, (gid_t *)&(p->aup_gid)) &&
	    xdr_array(xdrs, (caddr_t *)&(p->aup_gids),
			&(p->aup_len), NGRPS, sizeof (gid_t),
			(xdrproc_t) xdr_gid_t)) {
		trace1(TR_xdr_authsys_parms, 1);
		return (TRUE);
	}
	trace1(TR_xdr_authsys_parms, 1);
	return (FALSE);
}

/*
 * XDR user id types (uid_t)
 */
bool_t
xdr_uid_t(xdrs, ip)
	XDR *xdrs;
	uid_t *ip;
{
	bool_t dummy;

	trace1(TR_xdr_uid_t, 0);
#ifdef lint
	(void) (xdr_short(xdrs, (short *)ip));
	dummy = xdr_long(xdrs, (long *)ip);
	trace1(TR_xdr_uid_t, 1);
	return (dummy);
#else
	if (sizeof (uid_t) == sizeof (long)) {
		dummy = xdr_long(xdrs, (long *)ip);
		trace1(TR_xdr_uid_t, 1);
		return (dummy);
	} else {
		dummy = xdr_short(xdrs, (short *)ip);
		trace1(TR_xdr_uid_t, 1);
		return (dummy);
	}
#endif
}

/*
 * XDR group id types (gid_t)
 */
bool_t
xdr_gid_t(xdrs, ip)
	XDR *xdrs;
	gid_t *ip;
{
	bool_t dummy;

	trace1(TR_xdr_gid_t, 0);
#ifdef lint
	(void) (xdr_short(xdrs, (short *)ip));
	dummy = xdr_long(xdrs, (long *)ip);
	trace1(TR_xdr_gid_t, 1);
	return (dummy);
#else
	if (sizeof (gid_t) == sizeof (long)) {
		dummy = xdr_long(xdrs, (long *)ip);
		trace1(TR_xdr_gid_t, 1);
		return (dummy);
	} else {
		dummy = xdr_short(xdrs, (short *)ip);
		trace1(TR_xdr_gid_t, 1);
		return (dummy);
	}
#endif
}

#ifdef KERNEL
/*
 * XDR kernel unix auth parameters.
 * Goes out of the u struct directly.
 * NOTE: this is an XDR_ENCODE only routine.
 */
xdr_authkern(xdrs)
	register XDR *xdrs;
{
	int	*gp;
	uid_t	uid = getuid();
	gid_t	gid = getgid();
	int	len;
	caddr_t	groups;
	char	*name = hostname;

	trace1(TR_xdr_authkern, 0);
	if (xdrs->x_op != XDR_ENCODE) {
		trace1(TR_xdr_authkern, 1);
		return (FALSE);
	}

	for (gp = &u.u_groups[NGROUPS]; gp > u.u_groups; gp--) {
		if (gp[-1] >= 0) {
			break;
		}
	}
	len = gp - u.u_groups;
	groups = (caddr_t)u.u_groups;
	if (xdr_u_long(xdrs, (u_long *)&time.tv_sec) &&
	    xdr_string(xdrs, &name, MAX_MACHINE_NAME) &&
	    xdr_uid_t(xdrs, &uid) &&
	    xdr_gid_t(xdrs, &gid) &&
	    xdr_array(xdrs, &groups, (u_int *)&len, NGRPS,
			sizeof (gid_t), xdr_gid_t)) {
		trace1(TR_xdr_authkern, 1);
		return (TRUE);
	}
	trace1(TR_xdr_authkern, 1);
	return (FALSE);
}
#endif
