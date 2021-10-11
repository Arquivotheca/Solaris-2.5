#pragma ident	"@(#)pwdnm.c	1.2	92/07/20 SMI"  /* c2 secure */

#include <rpc/rpc.h>
#include <rpcsvc/pwdnm.h>


bool_t
xdr_pwdnm(xdrs,objp)
	XDR *xdrs;
	pwdnm *objp;
{
	if (! xdr_wrapstring(xdrs, &objp->name)) {
		return(FALSE);
	}
	if (! xdr_wrapstring(xdrs, &objp->password)) {
		return(FALSE);
	}
	return(TRUE);
}


