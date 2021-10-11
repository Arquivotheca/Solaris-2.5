/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */


/* NOTE: This file is copied from /usr/src/lib/nsswitch/nis/switch_err.c */
/*       to make use of useful nisplus programming routines. It should track modifications */
/*       to the original file */


#include <rpcsvc/ypclnt.h>
#include <nsswitch.h>

/*
 * maps errors returned by libnsl/yp routines into switch errors
 */

int
switch_err(ypclnt_err)
	int ypclnt_err;
{
	int serr;

	switch (ypclnt_err) {
	case YPERR_BADARGS:
	case YPERR_KEY:
	case YPERR_NOMORE:
		serr = __NSW_NOTFOUND;
		break;
	case YPERR_RPC:
	case YPERR_DOMAIN:
	case YPERR_MAP:
	case YPERR_YPERR:
	case YPERR_RESRC:
	case YPERR_PMAP:
	case YPERR_YPBIND:
	case YPERR_YPSERV:
	case YPERR_NODOM:
	case YPERR_BADDB:
	case YPERR_VERS:
	case YPERR_ACCESS:
		serr = __NSW_UNAVAIL;
		break;
	case YPERR_BUSY:
		serr = __NSW_TRYAGAIN; /* :-) */
	}

	return(serr);
}
